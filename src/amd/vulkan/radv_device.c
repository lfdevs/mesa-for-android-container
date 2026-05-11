/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <stdbool.h>
#include <string.h>

#ifdef __FreeBSD__
#include <sys/types.h>
#endif
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

#ifdef __linux__
#include <sys/inotify.h>
#endif

#include "layers/radv_app_workarounds.h"
#include "meta/radv_meta.h"
#include "util/u_debug.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_debug_nir.h"
#include "radv_entrypoints.h"
#include "radv_formats.h"
#include "radv_physical_device.h"
#include "radv_rmv.h"
#include "radv_shader.h"
#include "radv_spm.h"
#include "radv_sqtt.h"
#include "vk_common_entrypoints.h"
#include "vk_pipeline_cache.h"
#include "vk_util.h"
#ifdef _WIN32
typedef void *drmDevicePtr;
#include <io.h>
#else
#include <xf86drm.h>
#endif
#include "util/mesa-blake3.h"
#include "util/u_atomic.h"
#include "util/u_process.h"
#include "git_sha1.h"
#include "vk_sync.h"

#include "aco_interface.h"

static bool
radv_trap_handler_enabled()
{
   return !!os_get_option("RADV_TRAP_HANDLER");
}

bool
radv_device_should_clear_vram(const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   /* Ignore drirc radv_zero_vram=true if the feature is enabled to let applications take control. */
   return instance->drirc.debug.zero_vram && !device->vk.enabled_features.zeroInitializeDeviceMemory;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetMemoryHostPointerPropertiesEXT(VkDevice _device, VkExternalMemoryHandleTypeFlagBits handleType,
                                       const void *pHostPointer,
                                       VkMemoryHostPointerPropertiesEXT *pMemoryHostPointerProperties)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT: {
      uint32_t memoryTypeBits = 0;
      for (int i = 0; i < pdev->memory_properties.memoryTypeCount; i++) {
         if (pdev->memory_domains[i] == RADEON_DOMAIN_GTT && !(pdev->memory_flags[i] & RADEON_FLAG_GTT_WC)) {
            memoryTypeBits = (1 << i);
            break;
         }
      }
      pMemoryHostPointerProperties->memoryTypeBits = memoryTypeBits;
      return VK_SUCCESS;
   }
   default:
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
}

static VkResult
radv_device_init_border_color(struct radv_device *device)
{
   VkResult result;

   result = radv_bo_create(device, NULL, RADV_BORDER_COLOR_BUFFER_SIZE, 4096, RADEON_DOMAIN_VRAM,
                           RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_READ_ONLY | RADEON_FLAG_NO_INTERPROCESS_SHARING,
                           RADV_BO_PRIORITY_SHADER, 0, true, &device->border_color_data.bo);

   if (result != VK_SUCCESS)
      return vk_error(device, result);

   radv_rmv_log_border_color_palette_create(device, device->border_color_data.bo);

   result = device->ws->buffer_make_resident(device->ws, device->border_color_data.bo, true);
   if (result != VK_SUCCESS)
      return vk_error(device, result);

   device->border_color_data.colors_gpu_ptr = radv_buffer_map(device->ws, device->border_color_data.bo);
   if (!device->border_color_data.colors_gpu_ptr)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   mtx_init(&device->border_color_data.mutex, mtx_plain);

   return VK_SUCCESS;
}

static void
radv_device_finish_border_color(struct radv_device *device)
{
   if (device->border_color_data.bo) {
      radv_rmv_log_border_color_palette_destroy(device, device->border_color_data.bo);
      device->ws->buffer_make_resident(device->ws, device->border_color_data.bo, false);
      radv_bo_destroy(device, NULL, device->border_color_data.bo);

      mtx_destroy(&device->border_color_data.mutex);
   }
}

static struct radv_shader_part *
_radv_create_vs_prolog(struct radv_device *device, const void *_key)
{
   struct radv_vs_prolog_key *key = (struct radv_vs_prolog_key *)_key;
   return radv_create_vs_prolog(device, key);
}

static uint32_t
radv_hash_vs_prolog(const void *key_)
{
   const struct radv_vs_prolog_key *key = key_;
   return _mesa_hash_data(key, sizeof(*key));
}

static bool
radv_cmp_vs_prolog(const void *a_, const void *b_)
{
   const struct radv_vs_prolog_key *a = a_;
   const struct radv_vs_prolog_key *b = b_;

   return memcmp(a, b, sizeof(*a)) == 0;
}

static struct radv_shader_part_cache_ops vs_prolog_ops = {
   .create = _radv_create_vs_prolog,
   .hash = radv_hash_vs_prolog,
   .equals = radv_cmp_vs_prolog,
};

static VkResult
radv_device_init_vs_prologs(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   radv_shader_part_cache_init(&device->vs_prologs, &vs_prolog_ops);

   /* don't pre-compile prologs if we want to print them */
   if (instance->debug_flags & RADV_DEBUG_DUMP_PROLOGS)
      return VK_SUCCESS;

   struct radv_vs_prolog_key key;
   memset(&key, 0, sizeof(key));
   key.as_ls = false;
   key.is_ngg = pdev->use_ngg;
   key.next_stage = MESA_SHADER_VERTEX;
   key.wave32 = pdev->ge_wave_size == 32;

   for (unsigned i = 1; i <= MAX_VERTEX_ATTRIBS; i++) {
      key.instance_rate_inputs = 0;
      key.num_attributes = i;

      device->simple_vs_prologs[i - 1] = radv_create_vs_prolog(device, &key);
      if (!device->simple_vs_prologs[i - 1])
         return vk_error(instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   unsigned idx = 0;
   for (unsigned num_attributes = 1; num_attributes <= 16; num_attributes++) {
      for (unsigned count = 1; count <= num_attributes; count++) {
         for (unsigned start = 0; start <= (num_attributes - count); start++) {
            key.instance_rate_inputs = BITFIELD_RANGE(start, count);
            key.num_attributes = num_attributes;

            struct radv_shader_part *prolog = radv_create_vs_prolog(device, &key);
            if (!prolog)
               return vk_error(instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);

            assert(idx == radv_instance_rate_prolog_index(num_attributes, key.instance_rate_inputs));
            device->instance_rate_vs_prologs[idx++] = prolog;
         }
      }
   }
   assert(idx == ARRAY_SIZE(device->instance_rate_vs_prologs));

   return VK_SUCCESS;
}

static void
radv_device_finish_vs_prologs(struct radv_device *device)
{
   if (device->vs_prologs.ops)
      radv_shader_part_cache_finish(device, &device->vs_prologs);

   for (unsigned i = 0; i < ARRAY_SIZE(device->simple_vs_prologs); i++) {
      if (!device->simple_vs_prologs[i])
         continue;

      radv_shader_part_unref(device, device->simple_vs_prologs[i]);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(device->instance_rate_vs_prologs); i++) {
      if (!device->instance_rate_vs_prologs[i])
         continue;

      radv_shader_part_unref(device, device->instance_rate_vs_prologs[i]);
   }
}

static struct radv_shader_part *
_radv_create_ps_epilog(struct radv_device *device, const void *_key)
{
   struct radv_ps_epilog_key *key = (struct radv_ps_epilog_key *)_key;
   return radv_create_ps_epilog(device, key, NULL);
}

static uint32_t
radv_hash_ps_epilog(const void *key_)
{
   const struct radv_ps_epilog_key *key = key_;
   return _mesa_hash_data(key, sizeof(*key));
}

static bool
radv_cmp_ps_epilog(const void *a_, const void *b_)
{
   const struct radv_ps_epilog_key *a = a_;
   const struct radv_ps_epilog_key *b = b_;

   return memcmp(a, b, sizeof(*a)) == 0;
}

static struct radv_shader_part_cache_ops ps_epilog_ops = {
   .create = _radv_create_ps_epilog,
   .hash = radv_hash_ps_epilog,
   .equals = radv_cmp_ps_epilog,
};

VkResult
radv_device_init_vrs_state(struct radv_device *device)
{
   VkDeviceMemory mem;
   VkBuffer buffer;
   VkResult result;
   VkImage image;

   VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_D16_UNORM,
      .extent = {MAX_FRAMEBUFFER_WIDTH, MAX_FRAMEBUFFER_HEIGHT, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   result =
      radv_image_create(radv_device_to_handle(device), &(struct radv_image_create_info){.vk_info = &image_create_info},
                        &device->meta_state.alloc, &image, true);
   if (result != VK_SUCCESS)
      return result;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext =
         &(VkBufferUsageFlags2CreateInfo){
            .sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO,
            .usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT,
         },
      .size = radv_image_from_handle(image)->planes[0].surface.meta_size,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };

   result = radv_create_buffer(device, &buffer_create_info, &device->meta_state.alloc, &buffer, true);
   if (result != VK_SUCCESS)
      goto fail_create;

   VkDeviceBufferMemoryRequirements buffer_mem_req_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
      .pCreateInfo = &buffer_create_info,
   };
   VkMemoryRequirements2 mem_req = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };

   radv_GetDeviceBufferMemoryRequirements(radv_device_to_handle(device), &buffer_mem_req_info, &mem_req);

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_req.memoryRequirements.size,
   };

   result = radv_alloc_memory(device, &alloc_info, &device->meta_state.alloc, &mem, true);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   VkBindBufferMemoryInfo bind_info = {.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
                                       .buffer = buffer,
                                       .memory = mem,
                                       .memoryOffset = 0};

   result = radv_BindBufferMemory2(radv_device_to_handle(device), 1, &bind_info);
   if (result != VK_SUCCESS)
      goto fail_bind;

   device->vrs.image = radv_image_from_handle(image);
   device->vrs.buffer = radv_buffer_from_handle(buffer);
   device->vrs.mem = radv_device_memory_from_handle(mem);

   return VK_SUCCESS;

fail_bind:
   radv_FreeMemory(radv_device_to_handle(device), mem, &device->meta_state.alloc);
fail_alloc:
   radv_DestroyBuffer(radv_device_to_handle(device), buffer, &device->meta_state.alloc);
fail_create:
   radv_DestroyImage(radv_device_to_handle(device), image, &device->meta_state.alloc);

   return result;
}

static void
radv_device_finish_vrs_image(struct radv_device *device)
{
   if (!device->vrs.image)
      return;

   radv_FreeMemory(radv_device_to_handle(device), radv_device_memory_to_handle(device->vrs.mem),
                   &device->meta_state.alloc);
   radv_DestroyBuffer(radv_device_to_handle(device), radv_buffer_to_handle(device->vrs.buffer),
                      &device->meta_state.alloc);
   radv_DestroyImage(radv_device_to_handle(device), radv_image_to_handle(device->vrs.image), &device->meta_state.alloc);
}

static enum radv_force_vrs
radv_parse_vrs_rates(const char *str)
{
   if (!strcmp(str, "2x2")) {
      return RADV_FORCE_VRS_2x2;
   } else if (!strcmp(str, "2x1")) {
      return RADV_FORCE_VRS_2x1;
   } else if (!strcmp(str, "1x2")) {
      return RADV_FORCE_VRS_1x2;
   } else if (!strcmp(str, "1x1")) {
      return RADV_FORCE_VRS_1x1;
   }

   fprintf(stderr, "radv: Invalid VRS rates specified (valid values are 2x2, 2x1, 1x2 and 1x1)\n");
   return RADV_FORCE_VRS_1x1;
}

static const char *
radv_get_force_vrs_config_file(void)
{
   return os_get_option("RADV_FORCE_VRS_CONFIG_FILE");
}

static enum radv_force_vrs
radv_parse_force_vrs_config_file(const char *config_file)
{
   enum radv_force_vrs force_vrs = RADV_FORCE_VRS_1x1;
   char buf[4];
   FILE *f;

   f = fopen(config_file, "r");
   if (!f) {
      fprintf(stderr, "radv: Can't open file: '%s'.\n", config_file);
      return force_vrs;
   }

   if (fread(buf, sizeof(buf), 1, f) == 1) {
      buf[3] = '\0';
      force_vrs = radv_parse_vrs_rates(buf);
   }

   fclose(f);
   return force_vrs;
}

#ifdef __linux__

#define BUF_LEN ((10 * (sizeof(struct inotify_event) + NAME_MAX + 1)))

static int
radv_notifier_thread_run(void *data)
{
   struct radv_device *device = data;
   struct radv_notifier *notifier = &device->notifier;
   char buf[BUF_LEN];

   while (!notifier->quit) {
      const char *file = radv_get_force_vrs_config_file();
      struct timespec tm = {.tv_nsec = 100000000}; /* 1OOms */
      int length, i = 0;

      length = read(notifier->fd, buf, BUF_LEN);
      while (i < length) {
         struct inotify_event *event = (struct inotify_event *)&buf[i];

         i += sizeof(struct inotify_event) + event->len;
         if (event->mask & IN_MODIFY || event->mask & IN_DELETE_SELF) {
            /* Sleep 100ms for editors that use a temporary file and delete the original. */
            thrd_sleep(&tm, NULL);
            device->force_vrs = radv_parse_force_vrs_config_file(file);

            fprintf(stderr, "radv: Updated the per-vertex VRS rate to '%d'.\n", device->force_vrs);

            if (event->mask & IN_DELETE_SELF) {
               inotify_rm_watch(notifier->fd, notifier->watch);
               notifier->watch = inotify_add_watch(notifier->fd, file, IN_MODIFY | IN_DELETE_SELF);
            }
         }
      }

      thrd_sleep(&tm, NULL);
   }

   return 0;
}

#endif

static int
radv_device_init_notifier(struct radv_device *device)
{
#ifndef __linux__
   return true;
#else
   struct radv_notifier *notifier = &device->notifier;
   const char *file = radv_get_force_vrs_config_file();
   int ret;

   notifier->fd = inotify_init1(IN_NONBLOCK);
   if (notifier->fd < 0)
      return false;

   notifier->watch = inotify_add_watch(notifier->fd, file, IN_MODIFY | IN_DELETE_SELF);
   if (notifier->watch < 0)
      goto fail_watch;

   ret = thrd_create(&notifier->thread, radv_notifier_thread_run, device);
   if (ret)
      goto fail_thread;

   return true;

fail_thread:
   inotify_rm_watch(notifier->fd, notifier->watch);
fail_watch:
   close(notifier->fd);

   return false;
#endif
}

static void
radv_device_finish_notifier(struct radv_device *device)
{
#ifdef __linux__
   struct radv_notifier *notifier = &device->notifier;

   if (!notifier->thread)
      return;

   notifier->quit = true;
   thrd_join(notifier->thread, NULL);
   inotify_rm_watch(notifier->fd, notifier->watch);
   close(notifier->fd);
#endif
}

static VkResult
radv_device_init_perf_counter(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const size_t bo_size = PERF_CTR_BO_PASS_OFFSET + sizeof(uint64_t) * PERF_CTR_MAX_PASSES;
   VkResult result;

   result = radv_bo_create(device, NULL, bo_size, 4096, RADEON_DOMAIN_GTT,
                           RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING, RADV_BO_PRIORITY_UPLOAD_BUFFER,
                           0, true, &device->perf_counter_bo);
   if (result != VK_SUCCESS)
      return result;

   device->perf_counter_lock_cs = calloc(sizeof(struct radv_cmd_stream *), 2 * PERF_CTR_MAX_PASSES);
   if (!device->perf_counter_lock_cs)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (!pdev->ac_perfcounters.blocks)
      return VK_ERROR_INITIALIZATION_FAILED;

   return VK_SUCCESS;
}

static void
radv_device_finish_perf_counter(struct radv_device *device)
{
   if (device->perf_counter_bo)
      radv_bo_destroy(device, NULL, device->perf_counter_bo);

   if (!device->perf_counter_lock_cs)
      return;

   for (unsigned i = 0; i < 2 * PERF_CTR_MAX_PASSES; ++i) {
      if (device->perf_counter_lock_cs[i])
         radv_destroy_cmd_stream(device, device->perf_counter_lock_cs[i]);
   }

   free(device->perf_counter_lock_cs);
}

static VkResult
radv_device_init_memory_cache(struct radv_device *device)
{
   struct vk_pipeline_cache_create_info info = {.weak_ref = true};

   device->mem_cache = vk_pipeline_cache_create(&device->vk, &info, NULL);
   if (!device->mem_cache)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   return VK_SUCCESS;
}

static void
radv_device_finish_memory_cache(struct radv_device *device)
{
   if (device->mem_cache)
      vk_pipeline_cache_destroy(device->mem_cache, NULL);
}

static VkResult
radv_device_init_rgp(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   if (!(instance->vk.trace_mode & RADV_TRACE_MODE_RGP))
      return VK_SUCCESS;

   if (pdev->info.gfx_level < GFX8 || pdev->info.gfx_level > GFX12) {
      fprintf(stderr, "GPU hardware not supported: refer to "
                      "the RGP documentation for the list of "
                      "supported GPUs!\n");
      abort();
   }

   if (!radv_sqtt_init(device))
      return VK_ERROR_INITIALIZATION_FAILED;

   fprintf(stderr,
           "radv: Thread trace support is enabled (initial buffer size: %u MiB, "
           "instruction timing: %s, cache counters: %s, queue events: %s).\n",
           device->sqtt.buffer_size / (1024 * 1024), radv_is_instruction_timing_enabled() ? "enabled" : "disabled",
           radv_spm_trace_enabled(pdev) ? "enabled" : "disabled",
           radv_sqtt_queue_events_enabled() ? "enabled" : "disabled");

   if (radv_spm_trace_enabled(pdev)) {
      if (pdev->info.gfx_level >= GFX10 && pdev->info.gfx_level <= GFX12) {
         if (!radv_spm_init(device))
            return VK_ERROR_INITIALIZATION_FAILED;
      } else {
         fprintf(stderr, "radv: SPM isn't supported for this GPU (%s)!\n", pdev->name);
      }
   }

   return VK_SUCCESS;
}

static void
radv_device_finish_rgp(struct radv_device *device)
{
   radv_sqtt_finish(device);
   radv_spm_finish(device);
}

static void
radv_device_init_rmv(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   if (!(instance->vk.trace_mode & VK_TRACE_MODE_RMV))
      return;

   struct vk_rmv_device_info info;
   memset(&info, 0, sizeof(struct vk_rmv_device_info));
   radv_rmv_fill_device_info(pdev, &info);
   vk_memory_trace_init(&device->vk, &info);
   radv_memory_trace_init(device);
}

static VkResult
radv_device_init_trap_handler(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (!pdev->info.has_trap_handler_support)
      return VK_SUCCESS;

   if (!radv_trap_handler_enabled())
      return VK_SUCCESS;

   fprintf(stderr, "**********************************************************************\n");
   fprintf(stderr, "* WARNING: RADV_TRAP_HANDLER is experimental and only for debugging! *\n");
   fprintf(stderr, "**********************************************************************\n");

   if (!radv_trap_handler_init(device))
      return VK_ERROR_INITIALIZATION_FAILED;

   return VK_SUCCESS;
}

static VkResult
radv_device_init_device_fault_detection(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   if (!radv_device_fault_detection_enabled(device))
      return VK_SUCCESS;

   if (!radv_init_trace(device))
      return VK_ERROR_INITIALIZATION_FAILED;

   fprintf(stderr, "*****************************************************************************\n");
   fprintf(stderr, "* WARNING: RADV_DEBUG=hang is costly and should only be used for debugging! *\n");
   fprintf(stderr, "*****************************************************************************\n");

   /* Wait for idle after every draw/dispatch to identify the
    * first bad call.
    */
   instance->debug_flags |= RADV_DEBUG_SYNC_SHADERS;

   radv_dump_enabled_options(device, stderr);

   return VK_SUCCESS;
}

static void
radv_device_finish_device_fault_detection(struct radv_device *device)
{
   radv_finish_trace(device);
   ralloc_free(device->gpu_hang_report);
}

static VkResult
radv_device_init_tools(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   VkResult result;

   result = radv_device_init_device_fault_detection(device);
   if (result != VK_SUCCESS)
      return result;

   if (instance->debug_flags & RADV_DEBUG_VALIDATE_VAS) {
      result = radv_init_va_validation(device);
      if (result != VK_SUCCESS)
         return result;
   }

   result = radv_device_init_rgp(device);
   if (result != VK_SUCCESS)
      return result;

   radv_device_init_rmv(device);

   result = radv_device_init_trap_handler(device);
   if (result != VK_SUCCESS)
      return result;

   if (radv_bvh_dumping_enabled(instance) && radv_enable_rt(pdev)) {
      result = radv_rra_trace_init(device);
      if (result != VK_SUCCESS)
         return result;
   }

   result = radv_printf_data_init(device);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static void
radv_device_finish_tools(struct radv_device *device)
{
   radv_printf_data_finish(device);
   radv_rra_trace_finish(radv_device_to_handle(device), &device->rra_trace);
   radv_trap_handler_finish(device);
   radv_memory_trace_finish(device);
   radv_device_finish_rgp(device);
   radv_finish_va_validation(device);
   radv_device_finish_device_fault_detection(device);
}

struct dispatch_table_builder {
   struct vk_device_dispatch_table *tables[RADV_DISPATCH_TABLE_COUNT];
   bool used[RADV_DISPATCH_TABLE_COUNT];
   bool initialized[RADV_DISPATCH_TABLE_COUNT];
};

static void
add_entrypoints(struct dispatch_table_builder *b, const struct vk_device_entrypoint_table *entrypoints,
                enum radv_dispatch_table table)
{
   for (int32_t i = table - 1; i >= RADV_DEVICE_DISPATCH_TABLE; i--) {
      if (i == RADV_DEVICE_DISPATCH_TABLE || b->used[i]) {
         vk_device_dispatch_table_from_entrypoints(b->tables[i], entrypoints, !b->initialized[i]);
         b->initialized[i] = true;
      }
   }

   if (table < RADV_DISPATCH_TABLE_COUNT)
      b->used[table] = true;
}

static void
init_app_workarounds_entrypoints(struct radv_device *device, struct dispatch_table_builder *b)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct vk_device_entrypoint_table table = {0};

#define SET_ENTRYPOINT(app_layer, entrypoint) table.entrypoint = app_layer##_##entrypoint;
   if (!strcmp(instance->drirc.debug.app_layer, "metroexodus")) {
      SET_ENTRYPOINT(metro_exodus, GetSemaphoreCounterValue);
   } else if (!strcmp(instance->drirc.debug.app_layer, "rage2")) {
      SET_ENTRYPOINT(rage2, CmdBeginRenderPass);
   } else if (!strcmp(instance->drirc.debug.app_layer, "quanticdream")) {
      SET_ENTRYPOINT(quantic_dream, UnmapMemory2);
   } else if (!strcmp(instance->drirc.debug.app_layer, "no_mans_sky")) {
      SET_ENTRYPOINT(no_mans_sky, CreateImageView);
   } else if (!strcmp(instance->drirc.debug.app_layer, "strange_brigade")) {
      SET_ENTRYPOINT(strange_brigade, CmdPipelineBarrier2);
   }
#undef SET_ENTRYPOINT

   add_entrypoints(b, &table, RADV_APP_DISPATCH_TABLE);
}

static void
init_dispatch_tables(struct radv_device *device, struct radv_physical_device *pdev)
{
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct dispatch_table_builder b = {0};
   b.tables[RADV_DEVICE_DISPATCH_TABLE] = &device->vk.dispatch_table;
   b.tables[RADV_ANNOTATE_DISPATCH_TABLE] = &device->layer_dispatch.annotate;
   b.tables[RADV_APP_DISPATCH_TABLE] = &device->layer_dispatch.app;
   b.tables[RADV_RGP_DISPATCH_TABLE] = &device->layer_dispatch.rgp;
   b.tables[RADV_RRA_DISPATCH_TABLE] = &device->layer_dispatch.rra;
   b.tables[RADV_RMV_DISPATCH_TABLE] = &device->layer_dispatch.rmv;
   b.tables[RADV_CTX_ROLL_DISPATCH_TABLE] = &device->layer_dispatch.ctx_roll;

   bool gather_ctx_rolls = instance->vk.trace_mode & RADV_TRACE_MODE_CTX_ROLLS;
   if (radv_device_fault_detection_enabled(device) || gather_ctx_rolls)
      add_entrypoints(&b, &annotate_device_entrypoints, RADV_ANNOTATE_DISPATCH_TABLE);

   init_app_workarounds_entrypoints(device, &b);

   if (instance->vk.trace_mode & RADV_TRACE_MODE_RGP)
      add_entrypoints(&b, &sqtt_device_entrypoints, RADV_RGP_DISPATCH_TABLE);

   if (radv_bvh_dumping_enabled(instance) && radv_enable_rt(pdev))
      add_entrypoints(&b, &rra_device_entrypoints, RADV_RRA_DISPATCH_TABLE);

#ifndef _WIN32
   if (instance->vk.trace_mode & VK_TRACE_MODE_RMV)
      add_entrypoints(&b, &rmv_device_entrypoints, RADV_RMV_DISPATCH_TABLE);
#endif

   if (gather_ctx_rolls)
      add_entrypoints(&b, &ctx_roll_device_entrypoints, RADV_CTX_ROLL_DISPATCH_TABLE);

   add_entrypoints(&b, &radv_device_entrypoints, RADV_DISPATCH_TABLE_COUNT);
   add_entrypoints(&b, &wsi_device_entrypoints, RADV_DISPATCH_TABLE_COUNT);
   add_entrypoints(&b, &vk_common_device_entrypoints, RADV_DISPATCH_TABLE_COUNT);
}

static VkResult
get_timestamp(struct vk_device *_device, uint64_t *timestamp)
{
   struct radv_device *device = container_of(_device, struct radv_device, vk);
   *timestamp = device->ws->query_value(device->ws, RADEON_TIMESTAMP);
   return VK_SUCCESS;
}

static VkResult
capture_trace(VkQueue _queue)
{
   VK_FROM_HANDLE(radv_queue, queue, _queue);
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   VkResult result = VK_SUCCESS;

   if (instance->vk.trace_mode & RADV_TRACE_MODE_RRA)
      device->rra_trace.triggered = true;

   if (device->vk.memory_trace_data.is_enabled) {
      simple_mtx_lock(&device->vk.memory_trace_data.token_mtx);
      radv_rmv_collect_trace_events(device);
      vk_dump_rmv_capture(&device->vk.memory_trace_data);
      simple_mtx_unlock(&device->vk.memory_trace_data.token_mtx);
   }

   if (instance->vk.trace_mode & RADV_TRACE_MODE_RGP)
      device->sqtt_triggered = true;

   if (instance->vk.trace_mode & RADV_TRACE_MODE_CTX_ROLLS) {
      char filename[2048];
      time_t t = time(NULL);
      struct tm now = *localtime(&t);
      snprintf(filename, sizeof(filename), "/tmp/%s_%04d.%02d.%02d_%02d.%02d.%02d.ctxroll", util_get_process_name(),
               1900 + now.tm_year, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);

      simple_mtx_lock(&device->ctx_roll_mtx);

      device->ctx_roll_file = fopen(filename, "w");
      if (device->ctx_roll_file)
         fprintf(stderr, "radv: Writing context rolls to '%s'...\n", filename);

      simple_mtx_unlock(&device->ctx_roll_mtx);
   }

   return result;
}

static void
radv_device_init_cache_key(struct radv_device *device)
{
   STATIC_ASSERT(sizeof(device->compiler_info.hw) == 8);
   STATIC_ASSERT(sizeof(device->compiler_info.key) == 20);

   uint32_t ptr_size = sizeof(void *);

   struct mesa_blake3 ctx;
   _mesa_blake3_init(&ctx);
   _mesa_blake3_update(&ctx, &ptr_size, sizeof(ptr_size));
   _mesa_blake3_update(&ctx, device->compiler_info.ac, sizeof(struct ac_compiler_info));
   _mesa_blake3_update(&ctx, &device->compiler_info.hw, sizeof(device->compiler_info.hw));
   _mesa_blake3_update(&ctx, &device->compiler_info.key, sizeof(device->compiler_info.key));
   _mesa_blake3_final(&ctx, device->cache_hash);
}

static void
radv_create_gfx_preamble(struct radv_device *device)
{
   struct radv_cmd_stream *cs;
   VkResult result;

   result = radv_create_cmd_stream(device, AMD_IP_GFX, false, &cs);
   if (result != VK_SUCCESS)
      return;

   radeon_check_space(device->ws, cs->b, 512);

   radv_emit_graphics(device, cs);

   device->ws->cs_pad(cs->b, 0);

   result = radv_bo_create(
      device, NULL, cs->b->cdw * 4, 4096, device->ws->cs_domain(device->ws),
      RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY | RADEON_FLAG_GTT_WC,
      RADV_BO_PRIORITY_CS, 0, true, &device->gfx_init);
   if (result != VK_SUCCESS)
      goto fail;

   void *map = radv_buffer_map(device->ws, device->gfx_init);
   if (!map) {
      radv_bo_destroy(device, NULL, device->gfx_init);
      device->gfx_init = NULL;
      goto fail;
   }
   memcpy(map, cs->b->buf, cs->b->cdw * 4);

   device->ws->buffer_unmap(device->ws, device->gfx_init, false);
   device->gfx_init_size_dw = cs->b->cdw;
fail:
   radv_destroy_cmd_stream(device, cs);
}

/* For MSAA sample positions. */
#define FILL_SREG(s0x, s0y, s1x, s1y, s2x, s2y, s3x, s3y)                                                              \
   ((((unsigned)(s0x) & 0xf) << 0) | (((unsigned)(s0y) & 0xf) << 4) | (((unsigned)(s1x) & 0xf) << 8) |                 \
    (((unsigned)(s1y) & 0xf) << 12) | (((unsigned)(s2x) & 0xf) << 16) | (((unsigned)(s2y) & 0xf) << 20) |              \
    (((unsigned)(s3x) & 0xf) << 24) | (((unsigned)(s3y) & 0xf) << 28))

/* For obtaining location coordinates from registers */
#define SEXT4(x)               ((int)((x) | ((x) & 0x8 ? 0xfffffff0 : 0)))
#define GET_SFIELD(reg, index) SEXT4(((reg) >> ((index) * 4)) & 0xf)
#define GET_SX(reg, index)     GET_SFIELD((reg)[(index) / 4], ((index) % 4) * 2)
#define GET_SY(reg, index)     GET_SFIELD((reg)[(index) / 4], ((index) % 4) * 2 + 1)

/* 1x MSAA */
static const uint32_t sample_locs_1x = FILL_SREG(0, 0, 0, 0, 0, 0, 0, 0);
static const unsigned max_dist_1x = 0;
static const uint64_t centroid_priority_1x = 0x0000000000000000ull;

/* 2xMSAA */
static const uint32_t sample_locs_2x = FILL_SREG(4, 4, -4, -4, 0, 0, 0, 0);
static const unsigned max_dist_2x = 4;
static const uint64_t centroid_priority_2x = 0x1010101010101010ull;

/* 4xMSAA */
static const uint32_t sample_locs_4x = FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6);
static const unsigned max_dist_4x = 6;
static const uint64_t centroid_priority_4x = 0x3210321032103210ull;

/* 8xMSAA */
static const uint32_t sample_locs_8x[] = {
   FILL_SREG(1, -3, -1, 3, 5, 1, -3, -5),
   FILL_SREG(-5, 5, -7, -1, 3, 7, 7, -7),
   /* The following are unused by hardware, but we emit them to IBs
    * instead of multiple SET_CONTEXT_REG packets. */
   0,
   0,
};
static const unsigned max_dist_8x = 7;
static const uint64_t centroid_priority_8x = 0x7654321076543210ull;

unsigned
radv_get_default_max_sample_dist(int log_samples)
{
   unsigned max_dist[] = {
      max_dist_1x,
      max_dist_2x,
      max_dist_4x,
      max_dist_8x,
   };
   return max_dist[log_samples];
}

void
radv_emit_default_sample_locations(const struct radv_physical_device *pdev, struct radv_cmd_stream *cs, int nr_samples)
{
   uint64_t centroid_priority;

   radeon_begin(cs);

   switch (nr_samples) {
   default:
   case 1:
      centroid_priority = centroid_priority_1x;

      radeon_set_context_reg(R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs_1x);
      radeon_set_context_reg(R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs_1x);
      radeon_set_context_reg(R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs_1x);
      radeon_set_context_reg(R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs_1x);
      break;
   case 2:
      centroid_priority = centroid_priority_2x;

      radeon_set_context_reg(R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs_2x);
      radeon_set_context_reg(R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs_2x);
      radeon_set_context_reg(R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs_2x);
      radeon_set_context_reg(R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs_2x);
      break;
   case 4:
      centroid_priority = centroid_priority_4x;

      radeon_set_context_reg(R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs_4x);
      radeon_set_context_reg(R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs_4x);
      radeon_set_context_reg(R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs_4x);
      radeon_set_context_reg(R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs_4x);
      break;
   case 8:
      centroid_priority = centroid_priority_8x;

      radeon_set_context_reg_seq(R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 14);
      radeon_emit_array(sample_locs_8x, 4);
      radeon_emit_array(sample_locs_8x, 4);
      radeon_emit_array(sample_locs_8x, 4);
      radeon_emit_array(sample_locs_8x, 2);
      break;
   }

   /* The exclusion bits can be set to improve rasterization efficiency if no sample lies on the
    * pixel boundary (-8 sample offset). It's currently always TRUE because the driver doesn't
    * support 16 samples.
    */
   if (pdev->info.gfx_level >= GFX7 && pdev->info.gfx_level < GFX12) {
      radeon_set_context_reg(R_02882C_PA_SU_PRIM_FILTER_CNTL,
                             S_02882C_XMAX_RIGHT_EXCLUSION(1) | S_02882C_YMAX_BOTTOM_EXCLUSION(1));
   }

   if (pdev->info.gfx_level >= GFX12) {
      radeon_set_context_reg_seq(R_028BF0_PA_SC_CENTROID_PRIORITY_0, 2);
   } else {
      radeon_set_context_reg_seq(R_028BD4_PA_SC_CENTROID_PRIORITY_0, 2);
   }
   radeon_emit(centroid_priority);
   radeon_emit(centroid_priority >> 32);

   radeon_end();
}

static void
radv_get_sample_position(struct radv_device *device, unsigned sample_count, unsigned sample_index, float *out_value)
{
   const uint32_t *sample_locs;

   switch (sample_count) {
   case 1:
   default:
      sample_locs = &sample_locs_1x;
      break;
   case 2:
      sample_locs = &sample_locs_2x;
      break;
   case 4:
      sample_locs = &sample_locs_4x;
      break;
   case 8:
      sample_locs = sample_locs_8x;
      break;
   }

   out_value[0] = (GET_SX(sample_locs, sample_index) + 8) / 16.0f;
   out_value[1] = (GET_SY(sample_locs, sample_index) + 8) / 16.0f;
}

static void
radv_device_init_msaa(struct radv_device *device)
{
   int i;

   radv_get_sample_position(device, 1, 0, device->sample_locations_1x[0]);

   for (i = 0; i < 2; i++)
      radv_get_sample_position(device, 2, i, device->sample_locations_2x[i]);
   for (i = 0; i < 4; i++)
      radv_get_sample_position(device, 4, i, device->sample_locations_4x[i]);
   for (i = 0; i < 8; i++)
      radv_get_sample_position(device, 8, i, device->sample_locations_8x[i]);
}

static bool
radv_device_is_cache_disabled(const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   /* The buffer address used for debug printf is hardcoded. */
   if (device->debug_nir.printf.buffer_addr)
      return true;

   /* The buffer address used for validating VAs is hardcoded. */
   if (device->debug_nir.valid_va.buffer_addr)
      return true;

   /* Pipeline caches can be disabled with RADV_DEBUG=nocache, with MESA_GLSL_CACHE_DISABLE=1 and
    * when ACO_DEBUG is used. MESA_GLSL_CACHE_DISABLE is done elsewhere.
    */
   if ((instance->debug_flags & RADV_DEBUG_NO_CACHE) || (pdev->use_llvm ? 0 : aco_get_codegen_flags()))
      return true;

   return false;
}

static void
radv_device_init_compiler_info(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   VkShaderStageFlags dump_shaders = 0;
   uint32_t nggc_max_ps_params = 0;

   if (instance->debug_flags & RADV_DEBUG_DUMP_VS)
      dump_shaders |= VK_SHADER_STAGE_VERTEX_BIT;
   if (instance->debug_flags & RADV_DEBUG_DUMP_TCS)
      dump_shaders |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
   if (instance->debug_flags & RADV_DEBUG_DUMP_TES)
      dump_shaders |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
   if (instance->debug_flags & RADV_DEBUG_DUMP_GS)
      dump_shaders |= VK_SHADER_STAGE_GEOMETRY_BIT;
   if (instance->debug_flags & RADV_DEBUG_DUMP_PS)
      dump_shaders |= VK_SHADER_STAGE_FRAGMENT_BIT;
   if (instance->debug_flags & RADV_DEBUG_DUMP_TASK)
      dump_shaders |= VK_SHADER_STAGE_TASK_BIT_EXT;
   if (instance->debug_flags & RADV_DEBUG_DUMP_MESH)
      dump_shaders |= VK_SHADER_STAGE_MESH_BIT_EXT;
   if (instance->debug_flags & RADV_DEBUG_DUMP_CS)
      dump_shaders |= VK_SHADER_STAGE_COMPUTE_BIT | RADV_RT_STAGE_BITS;

   if (pdev->use_ngg_culling) {
      /* Shader based culling efficiency can depend on PS throughput.
       * Estimate an upper limit for PS input param count based on GPU info.
       */
      nggc_max_ps_params = pdev->info.has_dedicated_vram ? 12 : 8;
   }

   bool image_2d_view_of_3d = device->vk.enabled_features.image2DViewOf3D && pdev->info.gfx_level == GFX9;
   bool mesh_shader_queries = device->vk.enabled_features.meshShaderQueries && pdev->emulate_mesh_shader_queries;
   bool primitives_generated_query = radv_uses_primitives_generated_query(device);

   /* The Vulkan spec says:
    *  "Binary shaders retrieved from a physical device with a certain shaderBinaryUUID are
    *   guaranteed to be compatible with all other physical devices reporting the same
    *   shaderBinaryUUID and the same or higher shaderBinaryVersion."
    *
    * That means the driver should compile shaders for the "worst" case of all features being
    * enabled, regardless of what features are actually enabled on the logical device.
    */
   if (device->vk.enabled_features.shaderObject) {
      image_2d_view_of_3d = pdev->info.gfx_level == GFX9;
      primitives_generated_query = true;
   }

   struct radv_compiler_info info = {
      /* Hardware info */
      .ac = &pdev->info.compiler_info,
      .hw =
         {
            .address32_hi = pdev->info.address32_hi,
            .address_prt_wa_control_bit = pdev->info.address_prt_wa_control_bit,
            .rbplus_allowed = pdev->info.rbplus_allowed,
         },
      /* Misc values included as part of the cache key */
      .key =
         {
            /* Shader features */
            .use_llvm = pdev->use_llvm,
            .use_ngg = pdev->use_ngg,
            .use_ngg_culling = pdev->use_ngg_culling,
            .nggc_max_ps_params = nggc_max_ps_params,
            .no_ngg_gs = instance->drirc.performance.disable_ngg_gs,
            .load_grid_size_from_user_sgpr = pdev->load_grid_size_from_user_sgpr,
            .emulate_ngg_gs_query_pipeline_stat = pdev->emulate_ngg_gs_query_pipeline_stat,
            .primitives_generated_query = primitives_generated_query,
            .mesh_shader_queries = mesh_shader_queries,
            .image_2d_view_of_3d = image_2d_view_of_3d,
            .use_fmask = pdev->use_fmask,
            .robust_buffer_access = pdev->use_llvm && (device->vk.enabled_features.robustBufferAccess2 ||
                                                       device->vk.enabled_features.robustBufferAccess),
            .mitigate_smem_oob = pdev->info.compiler_info.has_smem_oob_access_bug &&
                                 !(instance->debug_flags & RADV_DEBUG_NO_SMEM_MITIGATION),
            .mitigate_smem_with_null_prt =
               pdev->info.compiler_info.has_smem_with_null_prt_bug && radv_sparse_enabled(pdev),
            .bvh8 = radv_use_bvh8(pdev),
            .no_rt = !!(instance->debug_flags & RADV_DEBUG_NO_RT),
            .rt_cps = !!(instance->perftest_flags & RADV_PERFTEST_RT_CPS),
            .clear_lds = instance->drirc.misc.clear_lds,
            .disable_aniso_single_level = instance->drirc.debug.disable_aniso_single_level,
            .disable_shrink_image_store = instance->drirc.debug.disable_shrink_image_store,
            .disable_sinking_load_input_fs = instance->drirc.debug.disable_sinking_load_input_fs,
            .disable_trunc_coord = instance->drirc.debug.disable_trunc_coord,
            .enable_mrt_output_nan_fixup = instance->drirc.debug.enable_mrt_output_nan_fixup,
            .emulate_rt = radv_emulate_rt(pdev),
            .invariant_geom = instance->drirc.debug.invariant_geom,
            .split_fma = instance->drirc.debug.split_fma,
            .ssbo_non_uniform = instance->drirc.debug.ssbo_non_uniform,
            .tex_non_uniform = instance->drirc.debug.tex_non_uniform,
            .lower_terminate_to_discard = instance->drirc.debug.lower_terminate_to_discard,
            .no_implicit_varying_subgroup_size = instance->drirc.debug.no_implicit_varying_subgroup_size,
            .force_aniso = device->force_aniso,
            /* Use CHIP_UNKNOWN for increased compatiblity between caches. */
            .family = pdev->use_llvm ? pdev->info.family : CHIP_UNKNOWN,

            /* Wave/subgroup sizes */
            .ge_wave_size = pdev->ge_wave_size,
            .ps_wave_size = pdev->ps_wave_size,
            .cs_wave_size = pdev->cs_wave_size,
            .rt_wave_size = pdev->rt_wave_size,
         },
      /* Debug/tracing */
      .debug =
         {
            .dump_spirv = !!(instance->debug_flags & RADV_DEBUG_DUMP_SPIRV),
            .dump_backend_ir = !!(instance->debug_flags & RADV_DEBUG_DUMP_BACKEND_IR),
            .dump_preopt_ir = !!(instance->debug_flags & RADV_DEBUG_DUMP_PREOPT_IR),
            .dump_nir = !!(instance->debug_flags & RADV_DEBUG_DUMP_NIR),
            .dump_asm = !!(instance->debug_flags & RADV_DEBUG_DUMP_ASM),
            .dump_meta_shaders = !!(instance->debug_flags & RADV_DEBUG_DUMP_META_SHADERS),
            .dump_shader_stats = !!(instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS),
            .nir_debug_info = !!(instance->debug_flags & RADV_DEBUG_NIR_DEBUG_INFO),
            .dump_shaders = dump_shaders,
            .check_ir = !!(instance->debug_flags & RADV_DEBUG_CHECKIR),
            .printf_enabled = !!device->debug_nir.printf.buffer_addr,
            .trap_enabled = !!device->trap_handler_shader,
            .trap_excp_flags = instance->trap_excp_flags,
            .debug_report = &instance->vk.debug_report,
            .debug_nir = &device->debug_nir,
            .shader_dump_mtx = &instance->shader_dump_mtx,
            .keep_shader_info = device->keep_shader_info,
            .capture_shaders = (instance->debug_flags & RADV_DEBUG_DUMP_SHADERS) || device->keep_shader_info,
            /* Capture shader statistics when RGP is enabled to correlate shader hashes with Fossilize. */
            .capture_shader_stats = (instance->debug_flags & (RADV_DEBUG_DUMP_SHADER_STATS | RADV_DEBUG_PSO_HISTORY)) ||
                                    device->keep_shader_info || (instance->vk.trace_mode & RADV_TRACE_MODE_RGP),
            .family = pdev->info.family,
         },
      .rra_trace = &device->rra_trace,
      /* Cache */
      .cache_disabled = radv_device_is_cache_disabled(device),
      .enable_nir_cache = !!(instance->debug_flags & RADV_PERFTEST_NIR_CACHE),
      .mem_cache = device->mem_cache,
      .override_graphics_shader_version = instance->drirc.misc.override_graphics_shader_version,
      .override_ray_tracing_shader_version = instance->drirc.misc.override_ray_tracing_shader_version,
      .override_compute_shader_version = instance->drirc.misc.override_compute_shader_version,
      /* Descriptors */
      .sampled_image_desc_size = radv_get_sampled_image_desc_size(pdev),
      .combined_image_sampler_desc_size = radv_get_combined_image_sampler_desc_size(pdev),
      .combined_image_sampler_offset = radv_get_combined_image_sampler_offset(pdev),
      .sampler_descriptor_size = pdev->vk.properties.samplerDescriptorSize,
      .sampler_descriptor_alignment = pdev->vk.properties.samplerDescriptorAlignment,
      .image_descriptor_size = pdev->vk.properties.imageDescriptorSize,
      .image_descriptor_alignment = pdev->vk.properties.imageDescriptorAlignment,
      .buffer_descriptor_size = pdev->vk.properties.bufferDescriptorSize,
      .buffer_descriptor_alignment = pdev->vk.properties.bufferDescriptorAlignment,
      /* Shader features, included as part of the pipeline key */
      .device_robustness_state = &device->vk.robustness_state,
      .smooth_lines = device->vk.enabled_features.smoothLines,
      .force_vrs_enabled = device->force_vrs_enabled,
      /* Wave/subgroup sizes */
      .subgroup_size = device->vk.physical->properties.subgroupSize,
      .min_subgroup_size = device->vk.physical->properties.minSubgroupSize,
      .max_subgroup_size = device->vk.physical->properties.maxSubgroupSize,
      /* NIR/SPIR-V */
      .spirv_caps = vk_physical_device_get_spirv_capabilities(device->vk.physical),
   };

   radv_get_nir_options(&info);

   device->compiler_info = info;
}

static void
radv_destroy_device(struct radv_device *device, const VkAllocationCallbacks *pAllocator)
{
   radv_device_finish_perf_counter(device);

   if (device->zero_bo) {
      device->ws->buffer_make_resident(device->ws, device->zero_bo, false);
      radv_bo_destroy(device, NULL, device->zero_bo);
   }

   if (device->gfx_init)
      radv_bo_destroy(device, NULL, device->gfx_init);

   radv_device_finish_notifier(device);
   radv_device_finish_vs_prologs(device);
   if (device->ps_epilogs.ops)
      radv_shader_part_cache_finish(device, &device->ps_epilogs);
   radv_device_finish_border_color(device);
   radv_device_finish_vrs_image(device);

   for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         radv_queue_finish(&device->queues[i][q]);
      for (unsigned q = 0; q < device->queue_count_protected[i]; q++)
         radv_queue_finish(&device->queues_protected[i][q]);
      if (device->queue_count[i])
         vk_free(&device->vk.alloc, device->queues[i]);
      if (device->queue_count_protected[i])
         vk_free(&device->vk.alloc, device->queues_protected[i]);
   }
   if (device->private_sdma_queue != VK_NULL_HANDLE) {
      radv_queue_finish(device->private_sdma_queue);
      vk_free(&device->vk.alloc, device->private_sdma_queue);
   }

   _mesa_hash_table_destroy(device->rt_handles, NULL);

   radv_device_finish_meta(device);
   radv_device_finish_tools(device);
   radv_device_finish_memory_cache(device);

   radv_destroy_shader_upload_queue(device);

   for (unsigned i = 0; i < RADV_NUM_HW_CTX; i++) {
      if (device->hw_ctx[i])
         device->ws->ctx_destroy(device->hw_ctx[i]);
   }
   if (device->hw_vcn_enc_ctx)
      device->ws->ctx_destroy(device->hw_vcn_enc_ctx);

   mtx_destroy(&device->overallocation_mutex);
   simple_mtx_destroy(&device->ctx_roll_mtx);
   simple_mtx_destroy(&device->pstate_mtx);
   simple_mtx_destroy(&device->trace_mtx);
   simple_mtx_destroy(&device->rt_handles_mtx);
   simple_mtx_destroy(&device->pso_cache_stats_mtx);
   simple_mtx_destroy(&device->blit_queue_mtx);

   radv_destroy_shader_arenas(device);
   if (device->capture_replay_arena_vas)
      _mesa_hash_table_u64_destroy(device->capture_replay_arena_vas);

   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VK_FROM_HANDLE(radv_physical_device, pdev, physicalDevice);
   struct radv_instance *instance = radv_physical_device_instance(pdev);
   VkResult result;
   struct radv_device *device;

   bool overallocation_disallowed = false;

   vk_foreach_struct_const (ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD: {
         const VkDeviceMemoryOverallocationCreateInfoAMD *overallocation = (const void *)ext;
         if (overallocation->overallocationBehavior == VK_MEMORY_OVERALLOCATION_BEHAVIOR_DISALLOWED_AMD)
            overallocation_disallowed = true;
         break;
      }
      default:
         break;
      }
   }

   device = vk_zalloc2(&instance->vk.alloc, pAllocator, sizeof(*device), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = vk_device_init(&device->vk, &pdev->vk, NULL, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return result;
   }

   device->vk.get_timestamp = get_timestamp;
   device->vk.capture_trace = capture_trace;

   device->vk.command_buffer_ops = &radv_cmd_buffer_ops;

   init_dispatch_tables(device, pdev);

   /* Initialize everything required for compilation, first. */

   simple_mtx_init(&device->ctx_roll_mtx, mtx_plain);
   simple_mtx_init(&device->trace_mtx, mtx_plain);
   simple_mtx_init(&device->pstate_mtx, mtx_plain);
   simple_mtx_init(&device->rt_handles_mtx, mtx_plain);
   simple_mtx_init(&device->pso_cache_stats_mtx, mtx_plain);
   simple_mtx_init(&device->blit_queue_mtx, mtx_plain);

   device->rt_handles = _mesa_hash_table_create(NULL, _mesa_hash_u32, _mesa_key_u32_equal);

   radv_init_shader_arenas(device);

   if (!device->vk.disable_internal_cache) {
      result = radv_device_init_memory_cache(device);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (pdev->info.gfx_level == GFX10_3) {
      if (os_get_option("RADV_FORCE_VRS_CONFIG_FILE")) {
         const char *file = radv_get_force_vrs_config_file();

         device->force_vrs = radv_parse_force_vrs_config_file(file);

         if (radv_device_init_notifier(device)) {
            device->force_vrs_enabled = true;
         } else {
            fprintf(stderr, "radv: Failed to initialize the notifier for RADV_FORCE_VRS_CONFIG_FILE!\n");
         }
      } else if (os_get_option("RADV_FORCE_VRS")) {
         const char *vrs_rates = os_get_option("RADV_FORCE_VRS");

         device->force_vrs = radv_parse_vrs_rates(vrs_rates);
         device->force_vrs_enabled = device->force_vrs != RADV_FORCE_VRS_1x1;
      }
   }

   device->force_aniso = MIN2(16, (int)debug_get_num_option("RADV_TEX_ANISO", -1));
   if (device->force_aniso >= 0) {
      fprintf(stderr, "radv: Forcing anisotropy filter to %ix\n", 1 << util_logbase2(device->force_aniso));
   }

   device->ws = pdev->ws;
   device->vk.sync = device->ws->get_sync_provider(device->ws);

   /* Disable unordered submits when SQTT queue events are enabled because queue present events
    * might be missing otherwise.
    */
   device->vk.copy_sync_payloads = ((instance->vk.trace_mode & RADV_TRACE_MODE_RGP) && radv_sqtt_queue_events_enabled())
                                      ? NULL
                                      : pdev->ws->copy_sync_payloads;

   /* VM_ALWAYS_VALID must be supported. */
   assert(pdev->info.has_vm_always_valid);

   device->overallocation_disallowed = overallocation_disallowed;
   mtx_init(&device->overallocation_mutex, mtx_plain);

   if (pdev->info.has_kernelq_reg_shadowing || instance->debug_flags & RADV_DEBUG_SHADOW_REGS)
      device->uses_shadow_regs = true;

   bool video_dec_queue = false;
   bool video_enc_queue = false;

   /* Create one context per queue priority. */
   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
      const VkDeviceQueueGlobalPriorityCreateInfo *global_priority =
         vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO);
      enum radeon_ctx_priority priority = radv_get_queue_global_priority(global_priority);
      enum radv_queue_family qf = vk_queue_to_radv(pdev, queue_create->queueFamilyIndex);

      if (qf == RADV_QUEUE_VIDEO_DEC)
         video_dec_queue = true;
      else if (qf == RADV_QUEUE_VIDEO_ENC)
         video_enc_queue = true;

      if (device->hw_ctx[priority])
         continue;

      result = device->ws->ctx_create(device->ws, priority, &device->hw_ctx[priority]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   /* Use extra context to allow use of both VCN instances for transcoding. */
   if (video_dec_queue && video_enc_queue && pdev->info.ip[AMD_IP_VCN_ENC].num_instances > 1) {
      result = device->ws->ctx_create(device->ws, RADEON_CTX_PRIORITY_MEDIUM, &device->hw_vcn_enc_ctx);
      if (result != VK_SUCCESS)
         return result;
   }

   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
      uint32_t qfi = queue_create->queueFamilyIndex;
      const VkDeviceQueueGlobalPriorityCreateInfo *global_priority =
         vk_find_struct_const(queue_create->pNext, DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO);

      struct radv_queue **queues = queue_create->flags & VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT
                                      ? &device->queues_protected[qfi]
                                      : &device->queues[qfi];
      *queues = vk_zalloc(&device->vk.alloc, queue_create->queueCount * sizeof(struct radv_queue), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!*queues) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      if (queue_create->flags & VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT)
         device->queue_count_protected[qfi] = queue_create->queueCount;
      else
         device->queue_count[qfi] = queue_create->queueCount;

      for (unsigned q = 0; q < queue_create->queueCount; q++) {
         result = radv_queue_init(device, &(*queues)[q], q, queue_create, global_priority);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }
   device->private_sdma_queue = VK_NULL_HANDLE;

   device->shader_use_invisible_vram = (instance->perftest_flags & RADV_PERFTEST_DMA_SHADERS) &&
                                       /* SDMA buffer copy is only implemented for GFX7+. */
                                       pdev->info.gfx_level >= GFX7;
   result = radv_init_shader_upload_queue(device);
   if (result != VK_SUCCESS)
      goto fail;

   device->pbb_allowed = pdev->info.gfx_level >= GFX9 && !(instance->debug_flags & RADV_DEBUG_NOBINNING);

   /* The maximum number of scratch waves. Scratch space isn't divided
    * evenly between CUs. The number is only a function of the number of CUs.
    * We can decrease the constant to decrease the scratch buffer size.
    *
    * sctx->scratch_waves must be >= the maximum possible size of
    * 1 threadgroup, so that the hw doesn't hang from being unable
    * to start any.
    *
    * The recommended value is 4 per CU at most. Higher numbers don't
    * bring much benefit, but they still occupy chip resources (think
    * async compute). I've seen ~2% performance difference between 4 and 32.
    */
   uint32_t max_threads_per_block = 2048;
   device->scratch_waves = MAX2(32 * pdev->info.num_cu, max_threads_per_block / 64);

   device->dispatch_initiator = S_00B800_COMPUTE_SHADER_EN(1);

   if (pdev->info.gfx_level >= GFX7 && (pdev->info.family < CHIP_GFX940 || pdev->info.has_graphics)) {
      /* If the KMD allows it (there is a KMD hw register for it),
       * allow launching waves out-of-order.
       */
      device->dispatch_initiator |= S_00B800_ORDER_MODE(1);
   }
   if (pdev->info.gfx_level >= GFX10) {
      /* Enable asynchronous compute tunneling. The KMD restricts this feature
       * to high-priority compute queues, so setting the bit on any other queue
       * is a no-op. PAL always sets this bit as well.
       */
      device->dispatch_initiator |= S_00B800_TUNNEL_ENABLE(1);
   }

   /* Disable partial preemption for task shaders.
    * The kernel may not support preemption, but PAL always sets this bit,
    * so let's also set it here for consistency.
    */
   device->dispatch_initiator_task = device->dispatch_initiator | S_00B800_DISABLE_DISP_PREMPT_EN(1);

   /* Keep shader info for GPU hangs debugging. */
   device->keep_shader_info = radv_device_fault_detection_enabled(device) || radv_trap_handler_enabled();

   result = radv_device_init_tools(device);
   if (result != VK_SUCCESS)
      goto fail;

   result = radv_device_init_meta(device);
   if (result != VK_SUCCESS)
      goto fail;

   radv_device_init_msaa(device);

   /* If the border color extension is enabled, let's create the buffer we need. */
   if (device->vk.enabled_features.customBorderColors) {
      result = radv_device_init_border_color(device);
      if (result != VK_SUCCESS)
         goto fail;
   }

   radv_device_init_compiler_info(device);

   radv_device_init_cache_key(device);

   if (device->vk.enabled_features.vertexInputDynamicState || device->vk.enabled_features.graphicsPipelineLibrary ||
       device->vk.enabled_features.shaderObject) {
      result = radv_device_init_vs_prologs(device);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (device->vk.enabled_features.graphicsPipelineLibrary || device->vk.enabled_features.shaderObject ||
       device->vk.enabled_features.extendedDynamicState3ColorBlendEnable ||
       device->vk.enabled_features.extendedDynamicState3ColorWriteMask ||
       device->vk.enabled_features.extendedDynamicState3AlphaToCoverageEnable ||
       device->vk.enabled_features.extendedDynamicState3ColorBlendEquation)
      radv_shader_part_cache_init(&device->ps_epilogs, &ps_epilog_ops);

   if (pdev->info.has_zero_index_buffer_bug || device->compiler_info.key.mitigate_smem_oob) {
      result = radv_bo_create(device, NULL, 4096, 4096, RADEON_DOMAIN_VRAM,
                              RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY |
                                 RADEON_FLAG_ZERO_VRAM | RADEON_FLAG_32BIT,
                              RADV_BO_PRIORITY_VIRTUAL, 0, true, &device->zero_bo);
      if (result != VK_SUCCESS)
         goto fail;

      result = device->ws->buffer_make_resident(device->ws, device->zero_bo, true);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (pdev->info.has_graphics && !(instance->debug_flags & RADV_DEBUG_NO_IB_CHAINING))
      radv_create_gfx_preamble(device);

   if (device->vk.enabled_features.performanceCounterQueryPools) {
      result = radv_device_init_perf_counter(device);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (device->vk.enabled_features.rayTracingPipelineShaderGroupHandleCaptureReplay) {
      device->capture_replay_arena_vas = _mesa_hash_table_u64_create(NULL);
   }

   *pDevice = radv_device_to_handle(device);
   return VK_SUCCESS;

fail:
   radv_destroy_device(device, pAllocator);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   if (!device)
      return;

   radv_destroy_device(device, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetImageMemoryRequirements2(VkDevice _device, const VkImageMemoryRequirementsInfo2 *pInfo,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_image, image, pInfo->image);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint32_t alignment;
   uint64_t size;

   const VkImagePlaneMemoryRequirementsInfo *plane_info =
      vk_find_struct_const(pInfo->pNext, IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO);

   if (plane_info) {
      const uint32_t plane = radv_plane_from_aspect(plane_info->planeAspect);

      size = image->planes[plane].surface.total_size;
      alignment = 1 << image->planes[plane].surface.alignment_log2;
   } else {
      size = image->size;
      alignment = image->alignment;
   }

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      ((1u << pdev->memory_properties.memoryTypeCount) - 1u) & ~pdev->memory_types_32bit;

   if (image->vk.create_flags & VK_IMAGE_CREATE_PROTECTED_BIT)
      pMemoryRequirements->memoryRequirements.memoryTypeBits &= pdev->memory_types_protected;

   if (image->vk.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) {
      /* Only expose host visible memory types for images that need to be mapped on the CPU. */
      pMemoryRequirements->memoryRequirements.memoryTypeBits &= pdev->memory_types_host_visible;
   }

   pMemoryRequirements->memoryRequirements.size = size;
   pMemoryRequirements->memoryRequirements.alignment = alignment;

   vk_foreach_struct (ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req = (VkMemoryDedicatedRequirements *)ext;
         req->requiresDedicatedAllocation =
            image->vk.external_handle_types && image->vk.tiling != VK_IMAGE_TILING_LINEAR;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceImageMemoryRequirements(VkDevice device, const VkDeviceImageMemoryRequirements *pInfo,
                                      VkMemoryRequirements2 *pMemoryRequirements)
{
   UNUSED VkResult result;
   VkImage image;

   /* Determining the image size/alignment require to create a surface, which isn't really possible
    * without creating an image.
    */
   result =
      radv_image_create(device, &(struct radv_image_create_info){.vk_info = pInfo->pCreateInfo}, NULL, &image, true);
   assert(result == VK_SUCCESS);

   VkImageMemoryRequirementsInfo2 info2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
   };

   radv_GetImageMemoryRequirements2(device, &info2, pMemoryRequirements);

   radv_DestroyImage(device, image, NULL);
}

void
radv_gfx11_set_db_render_control(const struct radv_device *device, unsigned num_samples, unsigned *db_render_control)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   unsigned max_allowed_tiles_in_wave = 0;

   if (pdev->info.has_dedicated_vram) {
      if (num_samples == 8)
         max_allowed_tiles_in_wave = 6;
      else if (num_samples == 4)
         max_allowed_tiles_in_wave = 13;
      else
         max_allowed_tiles_in_wave = 0;
   } else {
      if (num_samples == 8)
         max_allowed_tiles_in_wave = 7;
      else if (num_samples == 4)
         max_allowed_tiles_in_wave = 15;
      else
         max_allowed_tiles_in_wave = 0;
   }

   *db_render_control |= S_028000_MAX_ALLOWED_TILES_IN_WAVE(max_allowed_tiles_in_wave);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetMemoryFdKHR(VkDevice _device, const VkMemoryGetFdInfoKHR *pGetFdInfo, int *pFD)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_device_memory, memory, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   /* At the moment, we support only the below handle types. */
   assert(pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
          pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   /* Set BO metadata for dedicated image allocations.  We don't need it for import when the image
    * tiling is VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, but we set it anyway for foreign consumers.
    */
   if (memory->image) {
      struct radv_image *image = memory->image;

      radv_image_bo_set_metadata(device, image, memory->bo);
   }

   bool ret = device->ws->buffer_get_fd(device->ws, memory->bo, pFD);
   if (ret == false)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   return VK_SUCCESS;
}

static uint32_t
radv_compute_valid_memory_types_attempt(struct radv_physical_device *pdev, enum radeon_bo_domain domains,
                                        enum radeon_bo_flag flags, enum radeon_bo_flag ignore_flags)
{
   /* Don't count GTT/CPU as relevant:
    *
    * - We're not fully consistent between the two.
    * - Sometimes VRAM gets VRAM|GTT.
    */
   const enum radeon_bo_domain relevant_domains = RADEON_DOMAIN_VRAM | RADEON_DOMAIN_GDS | RADEON_DOMAIN_OA;
   uint32_t bits = 0;
   for (unsigned i = 0; i < pdev->memory_properties.memoryTypeCount; ++i) {
      if ((domains & relevant_domains) != (pdev->memory_domains[i] & relevant_domains))
         continue;

      if ((flags & ~ignore_flags) != (pdev->memory_flags[i] & ~ignore_flags))
         continue;

      bits |= 1u << i;
   }

   return bits;
}

static uint32_t
radv_compute_valid_memory_types(struct radv_physical_device *pdev, enum radeon_bo_domain domains,
                                enum radeon_bo_flag flags)
{
   enum radeon_bo_flag ignore_flags = ~(RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_GTT_WC);
   uint32_t bits = radv_compute_valid_memory_types_attempt(pdev, domains, flags, ignore_flags);

   if (!bits) {
      ignore_flags |= RADEON_FLAG_GTT_WC;
      bits = radv_compute_valid_memory_types_attempt(pdev, domains, flags, ignore_flags);
   }

   if (!bits) {
      ignore_flags |= RADEON_FLAG_NO_CPU_ACCESS;
      bits = radv_compute_valid_memory_types_attempt(pdev, domains, flags, ignore_flags);
   }

   /* Avoid 32-bit memory types for shared memory. */
   bits &= ~pdev->memory_types_32bit;

   return bits;
}
VKAPI_ATTR VkResult VKAPI_CALL
radv_GetMemoryFdPropertiesKHR(VkDevice _device, VkExternalMemoryHandleTypeFlagBits handleType, int fd,
                              VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_physical_device *pdev = radv_device_physical(device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT: {
      enum radeon_bo_domain domains;
      enum radeon_bo_flag flags;
      if (!device->ws->buffer_get_flags_from_fd(device->ws, fd, &domains, &flags))
         return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);

      pMemoryFdProperties->memoryTypeBits = radv_compute_valid_memory_types(pdev, domains, flags);
      return VK_SUCCESS;
   }
   default:
      /* The valid usage section for this function says:
       *
       *    "handleType must not be one of the handle types defined as
       *    opaque."
       *
       * So opaque handle types fall into the default "unsupported" case.
       */
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

bool
radv_device_set_pstate(struct radv_device *device, bool enable)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct radeon_winsys *ws = device->ws;
   enum radeon_ctx_pstate pstate = enable ? instance->profile_pstate : RADEON_CTX_PSTATE_NONE;

   /* pstate is per-device; setting it for one ctx is sufficient. We pick the first initialized one
    * below. */
   for (unsigned i = 0; i < RADV_NUM_HW_CTX; i++)
      if (device->hw_ctx[i])
         return ws->ctx_set_pstate(device->hw_ctx[i], pstate) >= 0;

   return true;
}

bool
radv_device_acquire_performance_counters(struct radv_device *device)
{
   bool result = true;
   simple_mtx_lock(&device->pstate_mtx);

   if (device->pstate_cnt == 0) {
      result = radv_device_set_pstate(device, true);
      if (result)
         ++device->pstate_cnt;
   }

   simple_mtx_unlock(&device->pstate_mtx);
   return result;
}

void
radv_device_release_performance_counters(struct radv_device *device)
{
   simple_mtx_lock(&device->pstate_mtx);

   if (--device->pstate_cnt == 0)
      radv_device_set_pstate(device, false);

   simple_mtx_unlock(&device->pstate_mtx);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_AcquireProfilingLockKHR(VkDevice _device, const VkAcquireProfilingLockInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   bool result = radv_device_acquire_performance_counters(device);
   return result ? VK_SUCCESS : VK_ERROR_UNKNOWN;
}

VKAPI_ATTR void VKAPI_CALL
radv_ReleaseProfilingLockKHR(VkDevice _device)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   radv_device_release_performance_counters(device);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceImageSubresourceLayout(VkDevice device, const VkDeviceImageSubresourceInfo *pInfo,
                                     VkSubresourceLayout2 *pLayout)
{
   UNUSED VkResult result;
   VkImage image;

   result =
      radv_image_create(device, &(struct radv_image_create_info){.vk_info = pInfo->pCreateInfo}, NULL, &image, true);
   assert(result == VK_SUCCESS);

   radv_GetImageSubresourceLayout2(device, image, pInfo->pSubresource, pLayout);

   radv_DestroyImage(device, image, NULL);
}
