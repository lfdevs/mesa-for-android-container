/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/drm.h"
#include <xf86drm.h>

#include "anv_private.h"
#include "util/debug.h"
#include "util/build_id.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/os_file.h"
#include "util/os_misc.h"
#include "util/u_atomic.h"
#include "util/u_string.h"
#include "util/driconf.h"
#include "git_sha1.h"
#include "vk_util.h"
#include "common/gen_aux_map.h"
#include "common/gen_defines.h"
#include "common/gen_uuid.h"
#include "compiler/glsl_types.h"

#include "genxml/gen7_pack.h"

static const driOptionDescription anv_dri_options[] = {
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_DEBUG
      DRI_CONF_ALWAYS_FLUSH_CACHE(false)
      DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(false)
   DRI_CONF_SECTION_END
};

/* This is probably far to big but it reflects the max size used for messages
 * in OpenGLs KHR_debug.
 */
#define MAX_DEBUG_MESSAGE_LENGTH    4096

/* Render engine timestamp register */
#define TIMESTAMP 0x2358

/* The "RAW" clocks on Linux are called "FAST" on FreeBSD */
#if !defined(CLOCK_MONOTONIC_RAW) && defined(CLOCK_MONOTONIC_FAST)
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC_FAST
#endif

static void
compiler_debug_log(void *data, const char *fmt, ...)
{
   char str[MAX_DEBUG_MESSAGE_LENGTH];
   struct anv_device *device = (struct anv_device *)data;
   struct anv_instance *instance = device->physical->instance;

   if (list_is_empty(&instance->debug_report_callbacks.callbacks))
      return;

   va_list args;
   va_start(args, fmt);
   (void) vsnprintf(str, MAX_DEBUG_MESSAGE_LENGTH, fmt, args);
   va_end(args);

   vk_debug_report(&instance->debug_report_callbacks,
                   VK_DEBUG_REPORT_DEBUG_BIT_EXT,
                   VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT,
                   0, 0, 0, "anv", str);
}

static void
compiler_perf_log(void *data, const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);

   if (INTEL_DEBUG & DEBUG_PERF)
      mesa_logd_v(fmt, args);

   va_end(args);
}

static uint64_t
anv_compute_heap_size(int fd, uint64_t gtt_size)
{
   /* Query the total ram from the system */
   uint64_t total_ram;
   if (!os_get_total_physical_memory(&total_ram))
      return 0;

   /* We don't want to burn too much ram with the GPU.  If the user has 4GiB
    * or less, we use at most half.  If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ull * 1024ull * 1024ull * 1024ull)
      available_ram = total_ram / 2;
   else
      available_ram = total_ram * 3 / 4;

   /* We also want to leave some padding for things we allocate in the driver,
    * so don't go over 3/4 of the GTT either.
    */
   uint64_t available_gtt = gtt_size * 3 / 4;

   return MIN2(available_ram, available_gtt);
}

static VkResult
anv_physical_device_init_heaps(struct anv_physical_device *device, int fd)
{
   if (anv_gem_get_context_param(fd, 0, I915_CONTEXT_PARAM_GTT_SIZE,
                                 &device->gtt_size) == -1) {
      /* If, for whatever reason, we can't actually get the GTT size from the
       * kernel (too old?) fall back to the aperture size.
       */
      anv_perf_warn(NULL, NULL,
                    "Failed to get I915_CONTEXT_PARAM_GTT_SIZE: %m");

      if (gen_get_aperture_size(fd, &device->gtt_size) == -1) {
         return vk_errorfi(device->instance, NULL,
                           VK_ERROR_INITIALIZATION_FAILED,
                           "failed to get aperture size: %m");
      }
   }

   /* We only allow 48-bit addresses with softpin because knowing the actual
    * address is required for the vertex cache flush workaround.
    */
   device->supports_48bit_addresses = (device->info.gen >= 8) &&
                                      device->has_softpin &&
                                      device->gtt_size > (4ULL << 30 /* GiB */);

   uint64_t heap_size = anv_compute_heap_size(fd, device->gtt_size);

   if (heap_size > (2ull << 30) && !device->supports_48bit_addresses) {
      /* When running with an overridden PCI ID, we may get a GTT size from
       * the kernel that is greater than 2 GiB but the execbuf check for 48bit
       * address support can still fail.  Just clamp the address space size to
       * 2 GiB if we don't have 48-bit support.
       */
      mesa_logw("%s:%d: The kernel reported a GTT size larger than 2 GiB but "
                        "not support for 48-bit addresses",
                        __FILE__, __LINE__);
      heap_size = 2ull << 30;
   }

   device->memory.heap_count = 1;
   device->memory.heaps[0] = (struct anv_memory_heap) {
      .size = heap_size,
      .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
   };

   uint32_t type_count = 0;
   for (uint32_t heap = 0; heap < device->memory.heap_count; heap++) {
      if (device->info.has_llc) {
         /* Big core GPUs share LLC with the CPU and thus one memory type can be
          * both cached and coherent at the same time.
          */
         device->memory.types[type_count++] = (struct anv_memory_type) {
            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            .heapIndex = heap,
         };
      } else {
         /* The spec requires that we expose a host-visible, coherent memory
          * type, but Atom GPUs don't share LLC. Thus we offer two memory types
          * to give the application a choice between cached, but not coherent and
          * coherent but uncached (WC though).
          */
         device->memory.types[type_count++] = (struct anv_memory_type) {
            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .heapIndex = heap,
         };
         device->memory.types[type_count++] = (struct anv_memory_type) {
            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            .heapIndex = heap,
         };
      }
   }
   device->memory.type_count = type_count;

   return VK_SUCCESS;
}

static VkResult
anv_physical_device_init_uuids(struct anv_physical_device *device)
{
   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(anv_physical_device_init_uuids);
   if (!note) {
      return vk_errorfi(device->instance, NULL,
                        VK_ERROR_INITIALIZATION_FAILED,
                        "Failed to find build-id");
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < 20) {
      return vk_errorfi(device->instance, NULL,
                        VK_ERROR_INITIALIZATION_FAILED,
                        "build-id too short.  It needs to be a SHA");
   }

   memcpy(device->driver_build_sha1, build_id_data(note), 20);

   struct mesa_sha1 sha1_ctx;
   uint8_t sha1[20];
   STATIC_ASSERT(VK_UUID_SIZE <= sizeof(sha1));

   /* The pipeline cache UUID is used for determining when a pipeline cache is
    * invalid.  It needs both a driver build and the PCI ID of the device.
    */
   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, build_id_data(note), build_id_len);
   _mesa_sha1_update(&sha1_ctx, &device->info.chipset_id,
                     sizeof(device->info.chipset_id));
   _mesa_sha1_update(&sha1_ctx, &device->always_use_bindless,
                     sizeof(device->always_use_bindless));
   _mesa_sha1_update(&sha1_ctx, &device->has_a64_buffer_access,
                     sizeof(device->has_a64_buffer_access));
   _mesa_sha1_update(&sha1_ctx, &device->has_bindless_images,
                     sizeof(device->has_bindless_images));
   _mesa_sha1_update(&sha1_ctx, &device->has_bindless_samplers,
                     sizeof(device->has_bindless_samplers));
   _mesa_sha1_final(&sha1_ctx, sha1);
   memcpy(device->pipeline_cache_uuid, sha1, VK_UUID_SIZE);

   gen_uuid_compute_driver_id(device->driver_uuid, &device->info, VK_UUID_SIZE);
   gen_uuid_compute_device_id(device->device_uuid, &device->isl_dev, VK_UUID_SIZE);

   return VK_SUCCESS;
}

static void
anv_physical_device_init_disk_cache(struct anv_physical_device *device)
{
#ifdef ENABLE_SHADER_CACHE
   char renderer[10];
   ASSERTED int len = snprintf(renderer, sizeof(renderer), "anv_%04x",
                               device->info.chipset_id);
   assert(len == sizeof(renderer) - 2);

   char timestamp[41];
   _mesa_sha1_format(timestamp, device->driver_build_sha1);

   const uint64_t driver_flags =
      brw_get_compiler_config_value(device->compiler);
   device->disk_cache = disk_cache_create(renderer, timestamp, driver_flags);
#else
   device->disk_cache = NULL;
#endif
}

static void
anv_physical_device_free_disk_cache(struct anv_physical_device *device)
{
#ifdef ENABLE_SHADER_CACHE
   if (device->disk_cache)
      disk_cache_destroy(device->disk_cache);
#else
   assert(device->disk_cache == NULL);
#endif
}

static VkResult
anv_physical_device_try_create(struct anv_instance *instance,
                               drmDevicePtr drm_device,
                               struct anv_physical_device **device_out)
{
   const char *primary_path = drm_device->nodes[DRM_NODE_PRIMARY];
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   VkResult result;
   int fd;
   int master_fd = -1;

   brw_process_intel_debug_variable();

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      if (errno == ENOMEM) {
         return vk_errorfi(instance, NULL, VK_ERROR_OUT_OF_HOST_MEMORY,
                        "Unable to open device %s: out of memory", path);
      }
      return vk_errorfi(instance, NULL, VK_ERROR_INCOMPATIBLE_DRIVER,
                        "Unable to open device %s: %m", path);
   }

   struct gen_device_info devinfo;
   if (!gen_get_device_info_from_fd(fd, &devinfo)) {
      result = vk_error(VK_ERROR_INCOMPATIBLE_DRIVER);
      goto fail_fd;
   }

   const char *device_name = gen_get_device_name(devinfo.chipset_id);

   if (devinfo.is_haswell) {
      mesa_logw("Haswell Vulkan support is incomplete");
   } else if (devinfo.gen == 7 && !devinfo.is_baytrail) {
      mesa_logw("Ivy Bridge Vulkan support is incomplete");
   } else if (devinfo.gen == 7 && devinfo.is_baytrail) {
      mesa_logw("Bay Trail Vulkan support is incomplete");
   } else if (devinfo.gen >= 8 && devinfo.gen <= 12) {
      /* Gen8-12 fully supported */
   } else {
      result = vk_errorfi(instance, NULL, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Vulkan not yet supported on %s", device_name);
      goto fail_fd;
   }

   struct anv_physical_device *device =
      vk_alloc(&instance->alloc, sizeof(*device), 8,
               VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (device == NULL) {
      result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_fd;
   }

   vk_object_base_init(NULL, &device->base, VK_OBJECT_TYPE_PHYSICAL_DEVICE);
   device->instance = instance;

   assert(strlen(path) < ARRAY_SIZE(device->path));
   snprintf(device->path, ARRAY_SIZE(device->path), "%s", path);

   device->info = devinfo;
   device->name = device_name;

   device->no_hw = device->info.no_hw;
   if (getenv("INTEL_NO_HW") != NULL)
      device->no_hw = true;

   device->pci_info.domain = drm_device->businfo.pci->domain;
   device->pci_info.bus = drm_device->businfo.pci->bus;
   device->pci_info.device = drm_device->businfo.pci->dev;
   device->pci_info.function = drm_device->businfo.pci->func;

   device->cmd_parser_version = -1;
   if (device->info.gen == 7) {
      device->cmd_parser_version =
         anv_gem_get_param(fd, I915_PARAM_CMD_PARSER_VERSION);
      if (device->cmd_parser_version == -1) {
         result = vk_errorfi(device->instance, NULL,
                             VK_ERROR_INITIALIZATION_FAILED,
                             "failed to get command parser version");
         goto fail_alloc;
      }
   }

   if (!anv_gem_get_param(fd, I915_PARAM_HAS_WAIT_TIMEOUT)) {
      result = vk_errorfi(device->instance, NULL,
                          VK_ERROR_INITIALIZATION_FAILED,
                          "kernel missing gem wait");
      goto fail_alloc;
   }

   if (!anv_gem_get_param(fd, I915_PARAM_HAS_EXECBUF2)) {
      result = vk_errorfi(device->instance, NULL,
                          VK_ERROR_INITIALIZATION_FAILED,
                          "kernel missing execbuf2");
      goto fail_alloc;
   }

   if (!device->info.has_llc &&
       anv_gem_get_param(fd, I915_PARAM_MMAP_VERSION) < 1) {
      result = vk_errorfi(device->instance, NULL,
                          VK_ERROR_INITIALIZATION_FAILED,
                          "kernel missing wc mmap");
      goto fail_alloc;
   }

   device->has_softpin = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_SOFTPIN);
   device->has_exec_async = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_ASYNC);
   device->has_exec_capture = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_CAPTURE);
   device->has_exec_fence = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_FENCE);
   device->has_syncobj = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_FENCE_ARRAY);
   device->has_syncobj_wait = device->has_syncobj &&
                              anv_gem_supports_syncobj_wait(fd);
   device->has_syncobj_wait_available =
      anv_gem_get_drm_cap(fd, DRM_CAP_SYNCOBJ_TIMELINE) != 0;

   device->has_context_priority = anv_gem_has_context_priority(fd);

   result = anv_physical_device_init_heaps(device, fd);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   device->use_softpin = device->has_softpin &&
                         device->supports_48bit_addresses;

   device->has_context_isolation =
      anv_gem_get_param(fd, I915_PARAM_HAS_CONTEXT_ISOLATION);

   device->has_exec_timeline =
      anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_TIMELINE_FENCES);
   if (env_var_as_boolean("ANV_QUEUE_THREAD_DISABLE", false))
      device->has_exec_timeline = false;

   device->has_thread_submit =
      device->has_syncobj_wait_available && device->has_exec_timeline;

   device->always_use_bindless =
      env_var_as_boolean("ANV_ALWAYS_BINDLESS", false);

   device->use_call_secondary =
      device->use_softpin &&
      !env_var_as_boolean("ANV_DISABLE_SECONDARY_CMD_BUFFER_CALLS", false);

   /* We first got the A64 messages on broadwell and we can only use them if
    * we can pass addresses directly into the shader which requires softpin.
    */
   device->has_a64_buffer_access = device->info.gen >= 8 &&
                                   device->use_softpin;

   /* We first get bindless image access on Skylake and we can only really do
    * it if we don't have any relocations so we need softpin.
    */
   device->has_bindless_images = device->info.gen >= 9 &&
                                 device->use_softpin;

   /* We've had bindless samplers since Ivy Bridge (forever in Vulkan terms)
    * because it's just a matter of setting the sampler address in the sample
    * message header.  However, we've not bothered to wire it up for vec4 so
    * we leave it disabled on gen7.
    */
   device->has_bindless_samplers = device->info.gen >= 8;

   device->has_implicit_ccs = device->info.has_aux_map;

   /* Check if we can read the GPU timestamp register from the CPU */
   uint64_t u64_ignore;
   device->has_reg_timestamp = anv_gem_reg_read(fd, TIMESTAMP | I915_REG_READ_8B_WA,
                                                &u64_ignore) == 0;

   uint64_t avail_mem;
   device->has_mem_available = os_get_available_system_memory(&avail_mem);

   device->always_flush_cache =
      driQueryOptionb(&instance->dri_options, "always_flush_cache");

   device->has_mmap_offset =
      anv_gem_get_param(fd, I915_PARAM_MMAP_GTT_VERSION) >= 4;

   /* GENs prior to 8 do not support EU/Subslice info */
   if (device->info.gen >= 8) {
      device->subslice_total = anv_gem_get_param(fd, I915_PARAM_SUBSLICE_TOTAL);
      device->eu_total = anv_gem_get_param(fd, I915_PARAM_EU_TOTAL);

      /* Without this information, we cannot get the right Braswell
       * brandstrings, and we have to use conservative numbers for GPGPU on
       * many platforms, but otherwise, things will just work.
       */
      if (device->subslice_total < 1 || device->eu_total < 1) {
         mesa_logw("Kernel 4.1 required to properly query GPU properties");
      }
   } else if (device->info.gen == 7) {
      device->subslice_total = 1 << (device->info.gt - 1);
   }

   if (device->info.is_cherryview &&
       device->subslice_total > 0 && device->eu_total > 0) {
      /* Logical CS threads = EUs per subslice * num threads per EU */
      uint32_t max_cs_threads =
         device->eu_total / device->subslice_total * device->info.num_thread_per_eu;

      /* Fuse configurations may give more threads than expected, never less. */
      if (max_cs_threads > device->info.max_cs_threads)
         device->info.max_cs_threads = max_cs_threads;
   }

   device->compiler = brw_compiler_create(NULL, &device->info);
   if (device->compiler == NULL) {
      result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_alloc;
   }
   device->compiler->shader_debug_log = compiler_debug_log;
   device->compiler->shader_perf_log = compiler_perf_log;
   device->compiler->supports_pull_constants = false;
   device->compiler->constant_buffer_0_is_relative =
      device->info.gen < 8 || !device->has_context_isolation;
   device->compiler->supports_shader_constants = true;
   device->compiler->compact_params = false;
   device->compiler->indirect_ubos_use_sampler = device->info.gen < 12;

   /* Broadwell PRM says:
    *
    *   "Before Gen8, there was a historical configuration control field to
    *    swizzle address bit[6] for in X/Y tiling modes. This was set in three
    *    different places: TILECTL[1:0], ARB_MODE[5:4], and
    *    DISP_ARB_CTL[14:13].
    *
    *    For Gen8 and subsequent generations, the swizzle fields are all
    *    reserved, and the CPU's memory controller performs all address
    *    swizzling modifications."
    */
   bool swizzled =
      device->info.gen < 8 && anv_gem_get_bit6_swizzle(fd, I915_TILING_X);

   isl_device_init(&device->isl_dev, &device->info, swizzled);

   result = anv_physical_device_init_uuids(device);
   if (result != VK_SUCCESS)
      goto fail_compiler;

   anv_physical_device_init_disk_cache(device);

   if (instance->enabled_extensions.KHR_display) {
      master_fd = open(primary_path, O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         /* prod the device with a GETPARAM call which will fail if
          * we don't have permission to even render on this device
          */
         if (anv_gem_get_param(master_fd, I915_PARAM_CHIPSET_ID) == 0) {
            close(master_fd);
            master_fd = -1;
         }
      }
   }
   device->master_fd = master_fd;

   result = anv_init_wsi(device);
   if (result != VK_SUCCESS)
      goto fail_disk_cache;

   device->perf = anv_get_perf(&device->info, fd);

   anv_physical_device_get_supported_extensions(device,
                                                &device->supported_extensions);


   device->local_fd = fd;

   *device_out = device;

   return VK_SUCCESS;

fail_disk_cache:
   anv_physical_device_free_disk_cache(device);
fail_compiler:
   ralloc_free(device->compiler);
fail_alloc:
   vk_free(&instance->alloc, device);
fail_fd:
   close(fd);
   if (master_fd != -1)
      close(master_fd);
   return result;
}

static void
anv_physical_device_destroy(struct anv_physical_device *device)
{
   anv_finish_wsi(device);
   anv_physical_device_free_disk_cache(device);
   ralloc_free(device->compiler);
   ralloc_free(device->perf);
   close(device->local_fd);
   if (device->master_fd >= 0)
      close(device->master_fd);
   vk_object_base_finish(&device->base);
   vk_free(&device->instance->alloc, device);
}

static void *
default_alloc_func(void *pUserData, size_t size, size_t align,
                   VkSystemAllocationScope allocationScope)
{
   return malloc(size);
}

static void *
default_realloc_func(void *pUserData, void *pOriginal, size_t size,
                     size_t align, VkSystemAllocationScope allocationScope)
{
   return realloc(pOriginal, size);
}

static void
default_free_func(void *pUserData, void *pMemory)
{
   free(pMemory);
}

static const VkAllocationCallbacks default_alloc = {
   .pUserData = NULL,
   .pfnAllocation = default_alloc_func,
   .pfnReallocation = default_realloc_func,
   .pfnFree = default_free_func,
};

VkResult anv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < ANV_INSTANCE_EXTENSION_COUNT; i++) {
      if (anv_instance_extensions_supported.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = anv_instance_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

static void
anv_init_dri_options(struct anv_instance *instance)
{
   driParseOptionInfo(&instance->available_dri_options, anv_dri_options,
                      ARRAY_SIZE(anv_dri_options));
   driParseConfigFiles(&instance->dri_options,
                       &instance->available_dri_options, 0, "anv", NULL,
                       instance->app_info.app_name,
                       instance->app_info.app_version,
                       instance->app_info.engine_name,
                       instance->app_info.engine_version);
}

VkResult anv_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
   struct anv_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   struct anv_instance_extension_table enabled_extensions = {};
   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < ANV_INSTANCE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    anv_instance_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= ANV_INSTANCE_EXTENSION_COUNT)
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      if (!anv_instance_extensions_supported.extensions[idx])
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      enabled_extensions.extensions[idx] = true;
   }

   instance = vk_alloc2(&default_alloc, pAllocator, sizeof(*instance), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(NULL, &instance->base, VK_OBJECT_TYPE_INSTANCE);

   if (pAllocator)
      instance->alloc = *pAllocator;
   else
      instance->alloc = default_alloc;

   instance->app_info = (struct anv_app_info) { .api_version = 0 };
   if (pCreateInfo->pApplicationInfo) {
      const VkApplicationInfo *app = pCreateInfo->pApplicationInfo;

      instance->app_info.app_name =
         vk_strdup(&instance->alloc, app->pApplicationName,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      instance->app_info.app_version = app->applicationVersion;

      instance->app_info.engine_name =
         vk_strdup(&instance->alloc, app->pEngineName,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      instance->app_info.engine_version = app->engineVersion;

      instance->app_info.api_version = app->apiVersion;
   }

   if (instance->app_info.api_version == 0)
      instance->app_info.api_version = VK_API_VERSION_1_0;

   instance->enabled_extensions = enabled_extensions;

   for (unsigned i = 0; i < ARRAY_SIZE(instance->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!anv_instance_entrypoint_is_enabled(i, instance->app_info.api_version,
                                              &instance->enabled_extensions)) {
         instance->dispatch.entrypoints[i] = NULL;
      } else {
         instance->dispatch.entrypoints[i] =
            anv_instance_dispatch_table.entrypoints[i];
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(instance->physical_device_dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!anv_physical_device_entrypoint_is_enabled(i, instance->app_info.api_version,
                                                     &instance->enabled_extensions)) {
         instance->physical_device_dispatch.entrypoints[i] = NULL;
      } else {
         instance->physical_device_dispatch.entrypoints[i] =
            anv_physical_device_dispatch_table.entrypoints[i];
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(instance->device_dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!anv_device_entrypoint_is_enabled(i, instance->app_info.api_version,
                                            &instance->enabled_extensions, NULL)) {
         instance->device_dispatch.entrypoints[i] = NULL;
      } else {
         instance->device_dispatch.entrypoints[i] =
            anv_device_dispatch_table.entrypoints[i];
      }
   }

   instance->physical_devices_enumerated = false;
   list_inithead(&instance->physical_devices);

   result = vk_debug_report_instance_init(&instance->debug_report_callbacks);
   if (result != VK_SUCCESS) {
      vk_free2(&default_alloc, pAllocator, instance);
      return vk_error(result);
   }

   instance->pipeline_cache_enabled =
      env_var_as_boolean("ANV_ENABLE_PIPELINE_CACHE", true);

   glsl_type_singleton_init_or_ref();

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   anv_init_dri_options(instance);

   *pInstance = anv_instance_to_handle(instance);

   return VK_SUCCESS;
}

void anv_DestroyInstance(
    VkInstance                                  _instance,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   if (!instance)
      return;

   list_for_each_entry_safe(struct anv_physical_device, pdevice,
                            &instance->physical_devices, link)
      anv_physical_device_destroy(pdevice);

   vk_free(&instance->alloc, (char *)instance->app_info.app_name);
   vk_free(&instance->alloc, (char *)instance->app_info.engine_name);

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   vk_debug_report_instance_destroy(&instance->debug_report_callbacks);

   glsl_type_singleton_decref();

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vk_object_base_finish(&instance->base);
   vk_free(&instance->alloc, instance);
}

static VkResult
anv_enumerate_physical_devices(struct anv_instance *instance)
{
   if (instance->physical_devices_enumerated)
      return VK_SUCCESS;

   instance->physical_devices_enumerated = true;

   /* TODO: Check for more devices ? */
   drmDevicePtr devices[8];
   int max_devices;

   max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   if (max_devices < 1)
      return VK_SUCCESS;

   VkResult result = VK_SUCCESS;
   for (unsigned i = 0; i < (unsigned)max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PCI &&
          devices[i]->deviceinfo.pci->vendor_id == 0x8086) {

         struct anv_physical_device *pdevice;
         result = anv_physical_device_try_create(instance, devices[i],
                                                 &pdevice);
         /* Incompatible DRM device, skip. */
         if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
            result = VK_SUCCESS;
            continue;
         }

         /* Error creating the physical device, report the error. */
         if (result != VK_SUCCESS)
            break;

         list_addtail(&pdevice->link, &instance->physical_devices);
      }
   }
   drmFreeDevices(devices, max_devices);

   /* If we successfully enumerated any devices, call it success */
   return result;
}

VkResult anv_EnumeratePhysicalDevices(
    VkInstance                                  _instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   VK_OUTARRAY_MAKE(out, pPhysicalDevices, pPhysicalDeviceCount);

   VkResult result = anv_enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   list_for_each_entry(struct anv_physical_device, pdevice,
                       &instance->physical_devices, link) {
      vk_outarray_append(&out, i) {
         *i = anv_physical_device_to_handle(pdevice);
      }
   }

   return vk_outarray_status(&out);
}

VkResult anv_EnumeratePhysicalDeviceGroups(
    VkInstance                                  _instance,
    uint32_t*                                   pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties*            pPhysicalDeviceGroupProperties)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   VK_OUTARRAY_MAKE(out, pPhysicalDeviceGroupProperties,
                         pPhysicalDeviceGroupCount);

   VkResult result = anv_enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   list_for_each_entry(struct anv_physical_device, pdevice,
                       &instance->physical_devices, link) {
      vk_outarray_append(&out, p) {
         p->physicalDeviceCount = 1;
         memset(p->physicalDevices, 0, sizeof(p->physicalDevices));
         p->physicalDevices[0] = anv_physical_device_to_handle(pdevice);
         p->subsetAllocation = false;

         vk_foreach_struct(ext, p->pNext)
            anv_debug_ignored_stype(ext->sType);
      }
   }

   return vk_outarray_status(&out);
}

void anv_GetPhysicalDeviceFeatures(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);

   *pFeatures = (VkPhysicalDeviceFeatures) {
      .robustBufferAccess                       = true,
      .fullDrawIndexUint32                      = true,
      .imageCubeArray                           = true,
      .independentBlend                         = true,
      .geometryShader                           = true,
      .tessellationShader                       = true,
      .sampleRateShading                        = true,
      .dualSrcBlend                             = true,
      .logicOp                                  = true,
      .multiDrawIndirect                        = true,
      .drawIndirectFirstInstance                = true,
      .depthClamp                               = true,
      .depthBiasClamp                           = true,
      .fillModeNonSolid                         = true,
      .depthBounds                              = pdevice->info.gen >= 12,
      .wideLines                                = true,
      .largePoints                              = true,
      .alphaToOne                               = true,
      .multiViewport                            = true,
      .samplerAnisotropy                        = true,
      .textureCompressionETC2                   = pdevice->info.gen >= 8 ||
                                                  pdevice->info.is_baytrail,
      .textureCompressionASTC_LDR               = pdevice->info.gen >= 9, /* FINISHME CHV */
      .textureCompressionBC                     = true,
      .occlusionQueryPrecise                    = true,
      .pipelineStatisticsQuery                  = true,
      .fragmentStoresAndAtomics                 = true,
      .shaderTessellationAndGeometryPointSize   = true,
      .shaderImageGatherExtended                = true,
      .shaderStorageImageExtendedFormats        = true,
      .shaderStorageImageMultisample            = false,
      .shaderStorageImageReadWithoutFormat      = false,
      .shaderStorageImageWriteWithoutFormat     = true,
      .shaderUniformBufferArrayDynamicIndexing  = true,
      .shaderSampledImageArrayDynamicIndexing   = true,
      .shaderStorageBufferArrayDynamicIndexing  = true,
      .shaderStorageImageArrayDynamicIndexing   = true,
      .shaderClipDistance                       = true,
      .shaderCullDistance                       = true,
      .shaderFloat64                            = pdevice->info.gen >= 8 &&
                                                  pdevice->info.has_64bit_float,
      .shaderInt64                              = pdevice->info.gen >= 8 &&
                                                  pdevice->info.has_64bit_int,
      .shaderInt16                              = pdevice->info.gen >= 8,
      .shaderResourceMinLod                     = pdevice->info.gen >= 9,
      .variableMultisampleRate                  = true,
      .inheritedQueries                         = true,
   };

   /* We can't do image stores in vec4 shaders */
   pFeatures->vertexPipelineStoresAndAtomics =
      pdevice->compiler->scalar_stage[MESA_SHADER_VERTEX] &&
      pdevice->compiler->scalar_stage[MESA_SHADER_GEOMETRY];

   struct anv_app_info *app_info = &pdevice->instance->app_info;

   /* The new DOOM and Wolfenstein games require depthBounds without
    * checking for it.  They seem to run fine without it so just claim it's
    * there and accept the consequences.
    */
   if (app_info->engine_name && strcmp(app_info->engine_name, "idTech") == 0)
      pFeatures->depthBounds = true;
}

static void
anv_get_physical_device_features_1_1(struct anv_physical_device *pdevice,
                                     VkPhysicalDeviceVulkan11Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);

   f->storageBuffer16BitAccess            = pdevice->info.gen >= 8;
   f->uniformAndStorageBuffer16BitAccess  = pdevice->info.gen >= 8;
   f->storagePushConstant16               = pdevice->info.gen >= 8;
   f->storageInputOutput16                = false;
   f->multiview                           = true;
   f->multiviewGeometryShader             = true;
   f->multiviewTessellationShader         = true;
   f->variablePointersStorageBuffer       = true;
   f->variablePointers                    = true;
   f->protectedMemory                     = false;
   f->samplerYcbcrConversion              = true;
   f->shaderDrawParameters                = true;
}

static void
anv_get_physical_device_features_1_2(struct anv_physical_device *pdevice,
                                     VkPhysicalDeviceVulkan12Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);

   f->samplerMirrorClampToEdge            = true;
   f->drawIndirectCount                   = true;
   f->storageBuffer8BitAccess             = pdevice->info.gen >= 8;
   f->uniformAndStorageBuffer8BitAccess   = pdevice->info.gen >= 8;
   f->storagePushConstant8                = pdevice->info.gen >= 8;
   f->shaderBufferInt64Atomics            = pdevice->info.gen >= 9 &&
                                            pdevice->use_softpin;
   f->shaderSharedInt64Atomics            = false;
   f->shaderFloat16                       = pdevice->info.gen >= 8;
   f->shaderInt8                          = pdevice->info.gen >= 8;

   bool descIndexing = pdevice->has_a64_buffer_access &&
                       pdevice->has_bindless_images;
   f->descriptorIndexing                                 = descIndexing;
   f->shaderInputAttachmentArrayDynamicIndexing          = false;
   f->shaderUniformTexelBufferArrayDynamicIndexing       = descIndexing;
   f->shaderStorageTexelBufferArrayDynamicIndexing       = descIndexing;
   f->shaderUniformBufferArrayNonUniformIndexing         = false;
   f->shaderSampledImageArrayNonUniformIndexing          = descIndexing;
   f->shaderStorageBufferArrayNonUniformIndexing         = descIndexing;
   f->shaderStorageImageArrayNonUniformIndexing          = descIndexing;
   f->shaderInputAttachmentArrayNonUniformIndexing       = false;
   f->shaderUniformTexelBufferArrayNonUniformIndexing    = descIndexing;
   f->shaderStorageTexelBufferArrayNonUniformIndexing    = descIndexing;
   f->descriptorBindingUniformBufferUpdateAfterBind      = false;
   f->descriptorBindingSampledImageUpdateAfterBind       = descIndexing;
   f->descriptorBindingStorageImageUpdateAfterBind       = descIndexing;
   f->descriptorBindingStorageBufferUpdateAfterBind      = descIndexing;
   f->descriptorBindingUniformTexelBufferUpdateAfterBind = descIndexing;
   f->descriptorBindingStorageTexelBufferUpdateAfterBind = descIndexing;
   f->descriptorBindingUpdateUnusedWhilePending          = descIndexing;
   f->descriptorBindingPartiallyBound                    = descIndexing;
   f->descriptorBindingVariableDescriptorCount           = descIndexing;
   f->runtimeDescriptorArray                             = descIndexing;

   f->samplerFilterMinmax                 = pdevice->info.gen >= 9;
   f->scalarBlockLayout                   = true;
   f->imagelessFramebuffer                = true;
   f->uniformBufferStandardLayout         = true;
   f->shaderSubgroupExtendedTypes         = true;
   f->separateDepthStencilLayouts         = true;
   f->hostQueryReset                      = true;
   f->timelineSemaphore                   = true;
   f->bufferDeviceAddress                 = pdevice->has_a64_buffer_access;
   f->bufferDeviceAddressCaptureReplay    = pdevice->has_a64_buffer_access;
   f->bufferDeviceAddressMultiDevice      = false;
   f->vulkanMemoryModel                   = true;
   f->vulkanMemoryModelDeviceScope        = true;
   f->vulkanMemoryModelAvailabilityVisibilityChains = true;
   f->shaderOutputViewportIndex           = true;
   f->shaderOutputLayer                   = true;
   f->subgroupBroadcastDynamicId          = true;
}

void anv_GetPhysicalDeviceFeatures2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2*                  pFeatures)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);
   anv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   VkPhysicalDeviceVulkan11Features core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
   };
   anv_get_physical_device_features_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Features core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   anv_get_physical_device_features_1_2(pdevice, &core_1_2);

#define CORE_FEATURE(major, minor, feature) \
   features->feature = core_##major##_##minor.feature


   vk_foreach_struct(ext, pFeatures->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: {
         VkPhysicalDevice4444FormatsFeaturesEXT *features =
            (VkPhysicalDevice4444FormatsFeaturesEXT *)ext;
         features->formatA4R4G4B4 = true;
         features->formatA4B4G4R4 = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR: {
         VkPhysicalDevice8BitStorageFeaturesKHR *features =
            (VkPhysicalDevice8BitStorageFeaturesKHR *)ext;
         CORE_FEATURE(1, 2, storageBuffer8BitAccess);
         CORE_FEATURE(1, 2, uniformAndStorageBuffer8BitAccess);
         CORE_FEATURE(1, 2, storagePushConstant8);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
         VkPhysicalDevice16BitStorageFeatures *features =
            (VkPhysicalDevice16BitStorageFeatures *)ext;
         CORE_FEATURE(1, 1, storageBuffer16BitAccess);
         CORE_FEATURE(1, 1, uniformAndStorageBuffer16BitAccess);
         CORE_FEATURE(1, 1, storagePushConstant16);
         CORE_FEATURE(1, 1, storageInputOutput16);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: {
         VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *features = (void *)ext;
         features->bufferDeviceAddress = pdevice->has_a64_buffer_access;
         features->bufferDeviceAddressCaptureReplay = false;
         features->bufferDeviceAddressMultiDevice = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR: {
         VkPhysicalDeviceBufferDeviceAddressFeaturesKHR *features = (void *)ext;
         CORE_FEATURE(1, 2, bufferDeviceAddress);
         CORE_FEATURE(1, 2, bufferDeviceAddressCaptureReplay);
         CORE_FEATURE(1, 2, bufferDeviceAddressMultiDevice);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV: {
         VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *features =
            (VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *)ext;
         features->computeDerivativeGroupQuads = true;
         features->computeDerivativeGroupLinear = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: {
         VkPhysicalDeviceConditionalRenderingFeaturesEXT *features =
            (VkPhysicalDeviceConditionalRenderingFeaturesEXT*)ext;
         features->conditionalRendering = pdevice->info.gen >= 8 ||
                                          pdevice->info.is_haswell;
         features->inheritedConditionalRendering = pdevice->info.gen >= 8 ||
                                                   pdevice->info.is_haswell;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
         VkPhysicalDeviceCustomBorderColorFeaturesEXT *features =
            (VkPhysicalDeviceCustomBorderColorFeaturesEXT *)ext;
         features->customBorderColors = pdevice->info.gen >= 8;
         features->customBorderColorWithoutFormat = pdevice->info.gen >= 8;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: {
         VkPhysicalDeviceDepthClipEnableFeaturesEXT *features =
            (VkPhysicalDeviceDepthClipEnableFeaturesEXT *)ext;
         features->depthClipEnable = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR: {
         VkPhysicalDeviceFloat16Int8FeaturesKHR *features = (void *)ext;
         CORE_FEATURE(1, 2, shaderFloat16);
         CORE_FEATURE(1, 2, shaderInt8);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT: {
         VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *features =
            (VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT *)ext;
         features->fragmentShaderSampleInterlock = pdevice->info.gen >= 9;
         features->fragmentShaderPixelInterlock = pdevice->info.gen >= 9;
         features->fragmentShaderShadingRateInterlock = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT: {
         VkPhysicalDeviceHostQueryResetFeaturesEXT *features =
            (VkPhysicalDeviceHostQueryResetFeaturesEXT *)ext;
         CORE_FEATURE(1, 2, hostQueryReset);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT: {
         VkPhysicalDeviceDescriptorIndexingFeaturesEXT *features =
            (VkPhysicalDeviceDescriptorIndexingFeaturesEXT *)ext;
         CORE_FEATURE(1, 2, shaderInputAttachmentArrayDynamicIndexing);
         CORE_FEATURE(1, 2, shaderUniformTexelBufferArrayDynamicIndexing);
         CORE_FEATURE(1, 2, shaderStorageTexelBufferArrayDynamicIndexing);
         CORE_FEATURE(1, 2, shaderUniformBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderSampledImageArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderStorageBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderStorageImageArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderInputAttachmentArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderUniformTexelBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, shaderStorageTexelBufferArrayNonUniformIndexing);
         CORE_FEATURE(1, 2, descriptorBindingUniformBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingSampledImageUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingStorageImageUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingStorageBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingUniformTexelBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingStorageTexelBufferUpdateAfterBind);
         CORE_FEATURE(1, 2, descriptorBindingUpdateUnusedWhilePending);
         CORE_FEATURE(1, 2, descriptorBindingPartiallyBound);
         CORE_FEATURE(1, 2, descriptorBindingVariableDescriptorCount);
         CORE_FEATURE(1, 2, runtimeDescriptorArray);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES_EXT: {
         VkPhysicalDeviceImageRobustnessFeaturesEXT *features =
            (VkPhysicalDeviceImageRobustnessFeaturesEXT *)ext;
         features->robustImageAccess = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT: {
         VkPhysicalDeviceIndexTypeUint8FeaturesEXT *features =
            (VkPhysicalDeviceIndexTypeUint8FeaturesEXT *)ext;
         features->indexTypeUint8 = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT: {
         VkPhysicalDeviceInlineUniformBlockFeaturesEXT *features =
            (VkPhysicalDeviceInlineUniformBlockFeaturesEXT *)ext;
         features->inlineUniformBlock = true;
         features->descriptorBindingInlineUniformBlockUpdateAfterBind = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT: {
         VkPhysicalDeviceLineRasterizationFeaturesEXT *features =
            (VkPhysicalDeviceLineRasterizationFeaturesEXT *)ext;
         features->rectangularLines = true;
         features->bresenhamLines = true;
         /* Support for Smooth lines with MSAA was removed on gen11.  From the
          * BSpec section "Multisample ModesState" table for "AA Line Support
          * Requirements":
          *
          *    GEN10:BUG:######## 	NUM_MULTISAMPLES == 1
          *
          * Fortunately, this isn't a case most people care about.
          */
         features->smoothLines = pdevice->info.gen < 10;
         features->stippledRectangularLines = false;
         features->stippledBresenhamLines = true;
         features->stippledSmoothLines = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
         VkPhysicalDeviceMultiviewFeatures *features =
            (VkPhysicalDeviceMultiviewFeatures *)ext;
         CORE_FEATURE(1, 1, multiview);
         CORE_FEATURE(1, 1, multiviewGeometryShader);
         CORE_FEATURE(1, 1, multiviewTessellationShader);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES_KHR: {
         VkPhysicalDeviceImagelessFramebufferFeaturesKHR *features =
            (VkPhysicalDeviceImagelessFramebufferFeaturesKHR *)ext;
         CORE_FEATURE(1, 2, imagelessFramebuffer);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: {
         VkPhysicalDevicePerformanceQueryFeaturesKHR *feature =
            (VkPhysicalDevicePerformanceQueryFeaturesKHR *)ext;
         feature->performanceCounterQueryPools = true;
         /* HW only supports a single configuration at a time. */
         feature->performanceCounterMultipleQueryPools = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT: {
         VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT *features =
            (VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT *)ext;
         features->pipelineCreationCacheControl = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: {
         VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *features =
            (VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *)ext;
         features->pipelineExecutableInfo = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES_EXT: {
         VkPhysicalDevicePrivateDataFeaturesEXT *features = (void *)ext;
         features->privateData = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: {
         VkPhysicalDeviceProtectedMemoryFeatures *features = (void *)ext;
         CORE_FEATURE(1, 1, protectedMemory);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
         VkPhysicalDeviceRobustness2FeaturesEXT *features = (void *)ext;
         features->robustBufferAccess2 = true;
         features->robustImageAccess2 = true;
         features->nullDescriptor = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
         VkPhysicalDeviceSamplerYcbcrConversionFeatures *features =
            (VkPhysicalDeviceSamplerYcbcrConversionFeatures *) ext;
         CORE_FEATURE(1, 1, samplerYcbcrConversion);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT: {
         VkPhysicalDeviceScalarBlockLayoutFeaturesEXT *features =
            (VkPhysicalDeviceScalarBlockLayoutFeaturesEXT *)ext;
         CORE_FEATURE(1, 2, scalarBlockLayout);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES_KHR: {
         VkPhysicalDeviceSeparateDepthStencilLayoutsFeaturesKHR *features =
            (VkPhysicalDeviceSeparateDepthStencilLayoutsFeaturesKHR *)ext;
         CORE_FEATURE(1, 2, separateDepthStencilLayouts);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT: {
         VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *features = (void *)ext;
         features->shaderBufferFloat32Atomics =    true;
         features->shaderBufferFloat32AtomicAdd =  false;
         features->shaderBufferFloat64Atomics =    false;
         features->shaderBufferFloat64AtomicAdd =  false;
         features->shaderSharedFloat32Atomics =    true;
         features->shaderSharedFloat32AtomicAdd =  false;
         features->shaderSharedFloat64Atomics =    false;
         features->shaderSharedFloat64AtomicAdd =  false;
         features->shaderImageFloat32Atomics =     true;
         features->shaderImageFloat32AtomicAdd =   false;
         features->sparseImageFloat32Atomics =     false;
         features->sparseImageFloat32AtomicAdd =   false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES_KHR: {
         VkPhysicalDeviceShaderAtomicInt64FeaturesKHR *features = (void *)ext;
         CORE_FEATURE(1, 2, shaderBufferInt64Atomics);
         CORE_FEATURE(1, 2, shaderSharedInt64Atomics);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES_EXT: {
         VkPhysicalDeviceShaderDemoteToHelperInvocationFeaturesEXT *features = (void *)ext;
         features->shaderDemoteToHelperInvocation = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR: {
         VkPhysicalDeviceShaderClockFeaturesKHR *features =
            (VkPhysicalDeviceShaderClockFeaturesKHR *)ext;
         features->shaderSubgroupClock = true;
         features->shaderDeviceClock = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: {
         VkPhysicalDeviceShaderDrawParametersFeatures *features = (void *)ext;
         CORE_FEATURE(1, 1, shaderDrawParameters);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_FUNCTIONS_2_FEATURES_INTEL: {
         VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *features =
            (VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *)ext;
         features->shaderIntegerFunctions2 = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES_KHR: {
         VkPhysicalDeviceShaderSubgroupExtendedTypesFeaturesKHR *features =
            (VkPhysicalDeviceShaderSubgroupExtendedTypesFeaturesKHR *)ext;
         CORE_FEATURE(1, 2, shaderSubgroupExtendedTypes);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES_KHR: {
         VkPhysicalDeviceShaderTerminateInvocationFeaturesKHR *features =
            (VkPhysicalDeviceShaderTerminateInvocationFeaturesKHR *)ext;
         features->shaderTerminateInvocation = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT: {
         VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *features =
            (VkPhysicalDeviceSubgroupSizeControlFeaturesEXT *)ext;
         features->subgroupSizeControl = true;
         features->computeFullSubgroups = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *features =
            (VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *)ext;
         features->texelBufferAlignment = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR: {
         VkPhysicalDeviceTimelineSemaphoreFeaturesKHR *features =
            (VkPhysicalDeviceTimelineSemaphoreFeaturesKHR *) ext;
         CORE_FEATURE(1, 2, timelineSemaphore);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
         VkPhysicalDeviceVariablePointersFeatures *features = (void *)ext;
         CORE_FEATURE(1, 1, variablePointersStorageBuffer);
         CORE_FEATURE(1, 1, variablePointers);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
         VkPhysicalDeviceTransformFeedbackFeaturesEXT *features =
            (VkPhysicalDeviceTransformFeedbackFeaturesEXT *)ext;
         features->transformFeedback = true;
         features->geometryStreams = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR: {
         VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR *features =
            (VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR *)ext;
         CORE_FEATURE(1, 2, uniformBufferStandardLayout);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *features =
            (VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *)ext;
         features->vertexAttributeInstanceRateDivisor = true;
         features->vertexAttributeInstanceRateZeroDivisor = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
         anv_get_physical_device_features_1_1(pdevice, (void *)ext);
         break;

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
         anv_get_physical_device_features_1_2(pdevice, (void *)ext);
         break;

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR: {
         VkPhysicalDeviceVulkanMemoryModelFeaturesKHR *features = (void *)ext;
         CORE_FEATURE(1, 2, vulkanMemoryModel);
         CORE_FEATURE(1, 2, vulkanMemoryModelDeviceScope);
         CORE_FEATURE(1, 2, vulkanMemoryModelAvailabilityVisibilityChains);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT: {
         VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *features =
            (VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *)ext;
         features->ycbcrImageArrays = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)ext;
         features->extendedDynamicState = true;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

#undef CORE_FEATURE
}

#define MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BUFFERS   64

#define MAX_PER_STAGE_DESCRIPTOR_INPUT_ATTACHMENTS 64
#define MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS       256

#define MAX_CUSTOM_BORDER_COLORS                   4096

void anv_GetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);
   const struct gen_device_info *devinfo = &pdevice->info;

   /* See assertions made when programming the buffer surface state. */
   const uint32_t max_raw_buffer_sz = devinfo->gen >= 7 ?
                                      (1ul << 30) : (1ul << 27);

   const uint32_t max_ssbos = pdevice->has_a64_buffer_access ? UINT16_MAX : 64;
   const uint32_t max_textures =
      pdevice->has_bindless_images ? UINT16_MAX : 128;
   const uint32_t max_samplers =
      pdevice->has_bindless_samplers ? UINT16_MAX :
      (devinfo->gen >= 8 || devinfo->is_haswell) ? 128 : 16;
   const uint32_t max_images =
      pdevice->has_bindless_images ? UINT16_MAX : MAX_IMAGES;

   /* If we can use bindless for everything, claim a high per-stage limit,
    * otherwise use the binding table size, minus the slots reserved for
    * render targets and one slot for the descriptor buffer. */
   const uint32_t max_per_stage =
      pdevice->has_bindless_images && pdevice->has_a64_buffer_access
      ? UINT32_MAX : MAX_BINDING_TABLE_SIZE - MAX_RTS - 1;

   /* Limit max_threads to 64 for the GPGPU_WALKER command */
   const uint32_t max_workgroup_size = 32 * MIN2(64, devinfo->max_cs_threads);

   VkSampleCountFlags sample_counts =
      isl_device_get_sample_counts(&pdevice->isl_dev);


   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D                      = (1 << 14),
      .maxImageDimension2D                      = (1 << 14),
      .maxImageDimension3D                      = (1 << 11),
      .maxImageDimensionCube                    = (1 << 14),
      .maxImageArrayLayers                      = (1 << 11),
      .maxTexelBufferElements                   = 128 * 1024 * 1024,
      .maxUniformBufferRange                    = (1ul << 27),
      .maxStorageBufferRange                    = max_raw_buffer_sz,
      .maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount                 = UINT32_MAX,
      .maxSamplerAllocationCount                = 64 * 1024,
      .bufferImageGranularity                   = 64, /* A cache line */
      .sparseAddressSpaceSize                   = 0,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = max_samplers,
      .maxPerStageDescriptorUniformBuffers      = MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BUFFERS,
      .maxPerStageDescriptorStorageBuffers      = max_ssbos,
      .maxPerStageDescriptorSampledImages       = max_textures,
      .maxPerStageDescriptorStorageImages       = max_images,
      .maxPerStageDescriptorInputAttachments    = MAX_PER_STAGE_DESCRIPTOR_INPUT_ATTACHMENTS,
      .maxPerStageResources                     = max_per_stage,
      .maxDescriptorSetSamplers                 = 6 * max_samplers, /* number of stages * maxPerStageDescriptorSamplers */
      .maxDescriptorSetUniformBuffers           = 6 * MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BUFFERS,           /* number of stages * maxPerStageDescriptorUniformBuffers */
      .maxDescriptorSetUniformBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetStorageBuffers           = 6 * max_ssbos,    /* number of stages * maxPerStageDescriptorStorageBuffers */
      .maxDescriptorSetStorageBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetSampledImages            = 6 * max_textures, /* number of stages * maxPerStageDescriptorSampledImages */
      .maxDescriptorSetStorageImages            = 6 * max_images,   /* number of stages * maxPerStageDescriptorStorageImages */
      .maxDescriptorSetInputAttachments         = MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS,
      .maxVertexInputAttributes                 = MAX_VBS,
      .maxVertexInputBindings                   = MAX_VBS,
      .maxVertexInputAttributeOffset            = 2047,
      .maxVertexInputBindingStride              = 2048,
      .maxVertexOutputComponents                = 128,
      .maxTessellationGenerationLevel           = 64,
      .maxTessellationPatchSize                 = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 128,
      .maxTessellationControlTotalOutputComponents = 2048,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations             = 32,
      .maxGeometryInputComponents               = 64,
      .maxGeometryOutputComponents              = 128,
      .maxGeometryOutputVertices                = 256,
      .maxGeometryTotalOutputComponents         = 1024,
      .maxFragmentInputComponents               = 116, /* 128 components - (PSIZ, CLIP_DIST0, CLIP_DIST1) */
      .maxFragmentOutputAttachments             = 8,
      .maxFragmentDualSrcAttachments            = 1,
      .maxFragmentCombinedOutputResources       = 8,
      .maxComputeSharedMemorySize               = 64 * 1024,
      .maxComputeWorkGroupCount                 = { 65535, 65535, 65535 },
      .maxComputeWorkGroupInvocations           = max_workgroup_size,
      .maxComputeWorkGroupSize = {
         max_workgroup_size,
         max_workgroup_size,
         max_workgroup_size,
      },
      .subPixelPrecisionBits                    = 8,
      .subTexelPrecisionBits                    = 8,
      .mipmapPrecisionBits                      = 8,
      .maxDrawIndexedIndexValue                 = UINT32_MAX,
      .maxDrawIndirectCount                     = UINT32_MAX,
      .maxSamplerLodBias                        = 16,
      .maxSamplerAnisotropy                     = 16,
      .maxViewports                             = MAX_VIEWPORTS,
      .maxViewportDimensions                    = { (1 << 14), (1 << 14) },
      .viewportBoundsRange                      = { INT16_MIN, INT16_MAX },
      .viewportSubPixelBits                     = 13, /* We take a float? */
      .minMemoryMapAlignment                    = 4096, /* A page */
      /* The dataport requires texel alignment so we need to assume a worst
       * case of R32G32B32A32 which is 16 bytes.
       */
      .minTexelBufferOffsetAlignment            = 16,
      .minUniformBufferOffsetAlignment          = ANV_UBO_ALIGNMENT,
      .minStorageBufferOffsetAlignment          = ANV_SSBO_ALIGNMENT,
      .minTexelOffset                           = -8,
      .maxTexelOffset                           = 7,
      .minTexelGatherOffset                     = -32,
      .maxTexelGatherOffset                     = 31,
      .minInterpolationOffset                   = -0.5,
      .maxInterpolationOffset                   = 0.4375,
      .subPixelInterpolationOffsetBits          = 4,
      .maxFramebufferWidth                      = (1 << 14),
      .maxFramebufferHeight                     = (1 << 14),
      .maxFramebufferLayers                     = (1 << 11),
      .framebufferColorSampleCounts             = sample_counts,
      .framebufferDepthSampleCounts             = sample_counts,
      .framebufferStencilSampleCounts           = sample_counts,
      .framebufferNoAttachmentsSampleCounts     = sample_counts,
      .maxColorAttachments                      = MAX_RTS,
      .sampledImageColorSampleCounts            = sample_counts,
      .sampledImageIntegerSampleCounts          = sample_counts,
      .sampledImageDepthSampleCounts            = sample_counts,
      .sampledImageStencilSampleCounts          = sample_counts,
      .storageImageSampleCounts                 = VK_SAMPLE_COUNT_1_BIT,
      .maxSampleMaskWords                       = 1,
      .timestampComputeAndGraphics              = true,
      .timestampPeriod                          = 1000000000.0 / devinfo->timestamp_frequency,
      .maxClipDistances                         = 8,
      .maxCullDistances                         = 8,
      .maxCombinedClipAndCullDistances          = 8,
      .discreteQueuePriorities                  = 2,
      .pointSizeRange                           = { 0.125, 255.875 },
      .lineWidthRange                           = {
         0.0,
         (devinfo->gen >= 9 || devinfo->is_cherryview) ?
            2047.9921875 : 7.9921875,
      },
      .pointSizeGranularity                     = (1.0 / 8.0),
      .lineWidthGranularity                     = (1.0 / 128.0),
      .strictLines                              = false,
      .standardSampleLocations                  = true,
      .optimalBufferCopyOffsetAlignment         = 128,
      .optimalBufferCopyRowPitchAlignment       = 128,
      .nonCoherentAtomSize                      = 64,
   };

   *pProperties = (VkPhysicalDeviceProperties) {
      .apiVersion = anv_physical_device_api_version(pdevice),
      .driverVersion = vk_get_driver_version(),
      .vendorID = 0x8086,
      .deviceID = pdevice->info.chipset_id,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
      .limits = limits,
      .sparseProperties = {0}, /* Broadwell doesn't do sparse. */
   };

   snprintf(pProperties->deviceName, sizeof(pProperties->deviceName),
            "%s", pdevice->name);
   memcpy(pProperties->pipelineCacheUUID,
          pdevice->pipeline_cache_uuid, VK_UUID_SIZE);
}

static void
anv_get_physical_device_properties_1_1(struct anv_physical_device *pdevice,
                                       VkPhysicalDeviceVulkan11Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);

   memcpy(p->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
   memcpy(p->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
   memset(p->deviceLUID, 0, VK_LUID_SIZE);
   p->deviceNodeMask = 0;
   p->deviceLUIDValid = false;

   p->subgroupSize = BRW_SUBGROUP_SIZE;
   VkShaderStageFlags scalar_stages = 0;
   for (unsigned stage = 0; stage < MESA_SHADER_STAGES; stage++) {
      if (pdevice->compiler->scalar_stage[stage])
         scalar_stages |= mesa_to_vk_shader_stage(stage);
   }
   p->subgroupSupportedStages = scalar_stages;
   p->subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT |
                                    VK_SUBGROUP_FEATURE_VOTE_BIT |
                                    VK_SUBGROUP_FEATURE_BALLOT_BIT |
                                    VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                                    VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                                    VK_SUBGROUP_FEATURE_QUAD_BIT;
   if (pdevice->info.gen >= 8) {
      /* TODO: There's no technical reason why these can't be made to
       * work on gen7 but they don't at the moment so it's best to leave
       * the feature disabled than enabled and broken.
       */
      p->subgroupSupportedOperations |= VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
                                        VK_SUBGROUP_FEATURE_CLUSTERED_BIT;
   }
   p->subgroupQuadOperationsInAllStages = pdevice->info.gen >= 8;

   p->pointClippingBehavior      = VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY;
   p->maxMultiviewViewCount      = 16;
   p->maxMultiviewInstanceIndex  = UINT32_MAX / 16;
   p->protectedNoFault           = false;
   /* This value doesn't matter for us today as our per-stage descriptors are
    * the real limit.
    */
   p->maxPerSetDescriptors       = 1024;
   p->maxMemoryAllocationSize    = MAX_MEMORY_ALLOCATION_SIZE;
}

static void
anv_get_physical_device_properties_1_2(struct anv_physical_device *pdevice,
                                       VkPhysicalDeviceVulkan12Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);

   p->driverID = VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA_KHR;
   memset(p->driverName, 0, sizeof(p->driverName));
   snprintf(p->driverName, VK_MAX_DRIVER_NAME_SIZE_KHR,
            "Intel open-source Mesa driver");
   memset(p->driverInfo, 0, sizeof(p->driverInfo));
   snprintf(p->driverInfo, VK_MAX_DRIVER_INFO_SIZE_KHR,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);
   p->conformanceVersion = (VkConformanceVersionKHR) {
      .major = 1,
      .minor = 2,
      .subminor = 0,
      .patch = 0,
   };

   p->denormBehaviorIndependence =
      VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL_KHR;
   p->roundingModeIndependence =
      VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE_KHR;

   /* Broadwell does not support HF denorms and there are restrictions
    * other gens. According to Kabylake's PRM:
    *
    * "math - Extended Math Function
    * [...]
    * Restriction : Half-float denorms are always retained."
    */
   p->shaderDenormFlushToZeroFloat16         = false;
   p->shaderDenormPreserveFloat16            = pdevice->info.gen > 8;
   p->shaderRoundingModeRTEFloat16           = true;
   p->shaderRoundingModeRTZFloat16           = true;
   p->shaderSignedZeroInfNanPreserveFloat16  = true;

   p->shaderDenormFlushToZeroFloat32         = true;
   p->shaderDenormPreserveFloat32            = true;
   p->shaderRoundingModeRTEFloat32           = true;
   p->shaderRoundingModeRTZFloat32           = true;
   p->shaderSignedZeroInfNanPreserveFloat32  = true;

   p->shaderDenormFlushToZeroFloat64         = true;
   p->shaderDenormPreserveFloat64            = true;
   p->shaderRoundingModeRTEFloat64           = true;
   p->shaderRoundingModeRTZFloat64           = true;
   p->shaderSignedZeroInfNanPreserveFloat64  = true;

   /* It's a bit hard to exactly map our implementation to the limits
    * described by Vulkan.  The bindless surface handle in the extended
    * message descriptors is 20 bits and it's an index into the table of
    * RENDER_SURFACE_STATE structs that starts at bindless surface base
    * address.  This means that we can have at must 1M surface states
    * allocated at any given time.  Since most image views take two
    * descriptors, this means we have a limit of about 500K image views.
    *
    * However, since we allocate surface states at vkCreateImageView time,
    * this means our limit is actually something on the order of 500K image
    * views allocated at any time.  The actual limit describe by Vulkan, on
    * the other hand, is a limit of how many you can have in a descriptor set.
    * Assuming anyone using 1M descriptors will be using the same image view
    * twice a bunch of times (or a bunch of null descriptors), we can safely
    * advertise a larger limit here.
    */
   const unsigned max_bindless_views = 1 << 20;
   p->maxUpdateAfterBindDescriptorsInAllPools            = max_bindless_views;
   p->shaderUniformBufferArrayNonUniformIndexingNative   = false;
   p->shaderSampledImageArrayNonUniformIndexingNative    = false;
   p->shaderStorageBufferArrayNonUniformIndexingNative   = true;
   p->shaderStorageImageArrayNonUniformIndexingNative    = false;
   p->shaderInputAttachmentArrayNonUniformIndexingNative = false;
   p->robustBufferAccessUpdateAfterBind                  = true;
   p->quadDivergentImplicitLod                           = false;
   p->maxPerStageDescriptorUpdateAfterBindSamplers       = max_bindless_views;
   p->maxPerStageDescriptorUpdateAfterBindUniformBuffers = MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BUFFERS;
   p->maxPerStageDescriptorUpdateAfterBindStorageBuffers = UINT32_MAX;
   p->maxPerStageDescriptorUpdateAfterBindSampledImages  = max_bindless_views;
   p->maxPerStageDescriptorUpdateAfterBindStorageImages  = max_bindless_views;
   p->maxPerStageDescriptorUpdateAfterBindInputAttachments = MAX_PER_STAGE_DESCRIPTOR_INPUT_ATTACHMENTS;
   p->maxPerStageUpdateAfterBindResources                = UINT32_MAX;
   p->maxDescriptorSetUpdateAfterBindSamplers            = max_bindless_views;
   p->maxDescriptorSetUpdateAfterBindUniformBuffers      = 6 * MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BUFFERS;
   p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = MAX_DYNAMIC_BUFFERS / 2;
   p->maxDescriptorSetUpdateAfterBindStorageBuffers      = UINT32_MAX;
   p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = MAX_DYNAMIC_BUFFERS / 2;
   p->maxDescriptorSetUpdateAfterBindSampledImages       = max_bindless_views;
   p->maxDescriptorSetUpdateAfterBindStorageImages       = max_bindless_views;
   p->maxDescriptorSetUpdateAfterBindInputAttachments    = MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS;

   /* We support all of the depth resolve modes */
   p->supportedDepthResolveModes    = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR |
                                      VK_RESOLVE_MODE_AVERAGE_BIT_KHR |
                                      VK_RESOLVE_MODE_MIN_BIT_KHR |
                                      VK_RESOLVE_MODE_MAX_BIT_KHR;
   /* Average doesn't make sense for stencil so we don't support that */
   p->supportedStencilResolveModes  = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR;
   if (pdevice->info.gen >= 8) {
      /* The advanced stencil resolve modes currently require stencil
       * sampling be supported by the hardware.
       */
      p->supportedStencilResolveModes |= VK_RESOLVE_MODE_MIN_BIT_KHR |
                                         VK_RESOLVE_MODE_MAX_BIT_KHR;
   }
   p->independentResolveNone  = true;
   p->independentResolve      = true;

   p->filterMinmaxSingleComponentFormats  = pdevice->info.gen >= 9;
   p->filterMinmaxImageComponentMapping   = pdevice->info.gen >= 9;

   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;

   p->framebufferIntegerColorSampleCounts =
      isl_device_get_sample_counts(&pdevice->isl_dev);
}

void anv_GetPhysicalDeviceProperties2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2*                pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);

   anv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   VkPhysicalDeviceVulkan11Properties core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
   };
   anv_get_physical_device_properties_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Properties core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
   };
   anv_get_physical_device_properties_1_2(pdevice, &core_1_2);

#define CORE_RENAMED_PROPERTY(major, minor, ext_property, core_property) \
   memcpy(&properties->ext_property, &core_##major##_##minor.core_property, \
          sizeof(core_##major##_##minor.core_property))

#define CORE_PROPERTY(major, minor, property) \
   CORE_RENAMED_PROPERTY(major, minor, property, property)

   vk_foreach_struct(ext, pProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: {
         VkPhysicalDeviceCustomBorderColorPropertiesEXT *properties =
            (VkPhysicalDeviceCustomBorderColorPropertiesEXT *)ext;
         properties->maxCustomBorderColorSamplers = MAX_CUSTOM_BORDER_COLORS;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES_KHR: {
         VkPhysicalDeviceDepthStencilResolvePropertiesKHR *properties =
            (VkPhysicalDeviceDepthStencilResolvePropertiesKHR *)ext;
         CORE_PROPERTY(1, 2, supportedDepthResolveModes);
         CORE_PROPERTY(1, 2, supportedStencilResolveModes);
         CORE_PROPERTY(1, 2, independentResolveNone);
         CORE_PROPERTY(1, 2, independentResolve);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT: {
         VkPhysicalDeviceDescriptorIndexingPropertiesEXT *properties =
            (VkPhysicalDeviceDescriptorIndexingPropertiesEXT *)ext;
         CORE_PROPERTY(1, 2, maxUpdateAfterBindDescriptorsInAllPools);
         CORE_PROPERTY(1, 2, shaderUniformBufferArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderSampledImageArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderStorageBufferArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderStorageImageArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, shaderInputAttachmentArrayNonUniformIndexingNative);
         CORE_PROPERTY(1, 2, robustBufferAccessUpdateAfterBind);
         CORE_PROPERTY(1, 2, quadDivergentImplicitLod);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindSamplers);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindUniformBuffers);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindStorageBuffers);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindSampledImages);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindStorageImages);
         CORE_PROPERTY(1, 2, maxPerStageDescriptorUpdateAfterBindInputAttachments);
         CORE_PROPERTY(1, 2, maxPerStageUpdateAfterBindResources);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindSamplers);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindUniformBuffers);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindStorageBuffers);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindSampledImages);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindStorageImages);
         CORE_PROPERTY(1, 2, maxDescriptorSetUpdateAfterBindInputAttachments);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR: {
         VkPhysicalDeviceDriverPropertiesKHR *properties =
            (VkPhysicalDeviceDriverPropertiesKHR *) ext;
         CORE_PROPERTY(1, 2, driverID);
         CORE_PROPERTY(1, 2, driverName);
         CORE_PROPERTY(1, 2, driverInfo);
         CORE_PROPERTY(1, 2, conformanceVersion);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT: {
         VkPhysicalDeviceExternalMemoryHostPropertiesEXT *props =
            (VkPhysicalDeviceExternalMemoryHostPropertiesEXT *) ext;
         /* Userptr needs page aligned memory. */
         props->minImportedHostPointerAlignment = 4096;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
         VkPhysicalDeviceIDProperties *properties =
            (VkPhysicalDeviceIDProperties *)ext;
         CORE_PROPERTY(1, 1, deviceUUID);
         CORE_PROPERTY(1, 1, driverUUID);
         CORE_PROPERTY(1, 1, deviceLUID);
         CORE_PROPERTY(1, 1, deviceLUIDValid);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES_EXT: {
         VkPhysicalDeviceInlineUniformBlockPropertiesEXT *props =
            (VkPhysicalDeviceInlineUniformBlockPropertiesEXT *)ext;
         props->maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE;
         props->maxPerStageDescriptorInlineUniformBlocks =
            MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
         props->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
            MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
         props->maxDescriptorSetInlineUniformBlocks =
            MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
         props->maxDescriptorSetUpdateAfterBindInlineUniformBlocks =
            MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceLineRasterizationPropertiesEXT *props =
            (VkPhysicalDeviceLineRasterizationPropertiesEXT *)ext;
         /* In the Skylake PRM Vol. 7, subsection titled "GIQ (Diamond)
          * Sampling Rules - Legacy Mode", it says the following:
          *
          *    "Note that the device divides a pixel into a 16x16 array of
          *    subpixels, referenced by their upper left corners."
          *
          * This is the only known reference in the PRMs to the subpixel
          * precision of line rasterization and a "16x16 array of subpixels"
          * implies 4 subpixel precision bits.  Empirical testing has shown
          * that 4 subpixel precision bits applies to all line rasterization
          * types.
          */
         props->lineSubPixelPrecisionBits = 4;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
         VkPhysicalDeviceMaintenance3Properties *properties =
            (VkPhysicalDeviceMaintenance3Properties *)ext;
         /* This value doesn't matter for us today as our per-stage
          * descriptors are the real limit.
          */
         CORE_PROPERTY(1, 1, maxPerSetDescriptors);
         CORE_PROPERTY(1, 1, maxMemoryAllocationSize);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
         VkPhysicalDeviceMultiviewProperties *properties =
            (VkPhysicalDeviceMultiviewProperties *)ext;
         CORE_PROPERTY(1, 1, maxMultiviewViewCount);
         CORE_PROPERTY(1, 1, maxMultiviewInstanceIndex);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT: {
         VkPhysicalDevicePCIBusInfoPropertiesEXT *properties =
            (VkPhysicalDevicePCIBusInfoPropertiesEXT *)ext;
         properties->pciDomain = pdevice->pci_info.domain;
         properties->pciBus = pdevice->pci_info.bus;
         properties->pciDevice = pdevice->pci_info.device;
         properties->pciFunction = pdevice->pci_info.function;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR: {
         VkPhysicalDevicePerformanceQueryPropertiesKHR *properties =
            (VkPhysicalDevicePerformanceQueryPropertiesKHR *)ext;
         /* We could support this by spawning a shader to do the equation
          * normalization.
          */
         properties->allowCommandBufferQueryCopies = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: {
         VkPhysicalDevicePointClippingProperties *properties =
            (VkPhysicalDevicePointClippingProperties *) ext;
         CORE_PROPERTY(1, 1, pointClippingBehavior);
         break;
      }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENTATION_PROPERTIES_ANDROID: {
         VkPhysicalDevicePresentationPropertiesANDROID *props =
            (VkPhysicalDevicePresentationPropertiesANDROID *)ext;
         props->sharedImage = VK_FALSE;
         break;
      }
#pragma GCC diagnostic pop

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: {
         VkPhysicalDeviceProtectedMemoryProperties *properties =
            (VkPhysicalDeviceProtectedMemoryProperties *)ext;
         CORE_PROPERTY(1, 1, protectedNoFault);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *) ext;
         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT: {
         VkPhysicalDeviceRobustness2PropertiesEXT *properties = (void *)ext;
         properties->robustStorageBufferAccessSizeAlignment =
            ANV_SSBO_BOUNDS_CHECK_ALIGNMENT;
         properties->robustUniformBufferAccessSizeAlignment =
            ANV_UBO_ALIGNMENT;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES_EXT: {
         VkPhysicalDeviceSamplerFilterMinmaxPropertiesEXT *properties =
            (VkPhysicalDeviceSamplerFilterMinmaxPropertiesEXT *)ext;
         CORE_PROPERTY(1, 2, filterMinmaxImageComponentMapping);
         CORE_PROPERTY(1, 2, filterMinmaxSingleComponentFormats);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
         VkPhysicalDeviceSubgroupProperties *properties = (void *)ext;
         CORE_PROPERTY(1, 1, subgroupSize);
         CORE_RENAMED_PROPERTY(1, 1, supportedStages,
                                     subgroupSupportedStages);
         CORE_RENAMED_PROPERTY(1, 1, supportedOperations,
                                     subgroupSupportedOperations);
         CORE_RENAMED_PROPERTY(1, 1, quadOperationsInAllStages,
                                     subgroupQuadOperationsInAllStages);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES_EXT: {
         VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *props =
            (VkPhysicalDeviceSubgroupSizeControlPropertiesEXT *)ext;
         STATIC_ASSERT(8 <= BRW_SUBGROUP_SIZE && BRW_SUBGROUP_SIZE <= 32);
         props->minSubgroupSize = 8;
         props->maxSubgroupSize = 32;
         /* Limit max_threads to 64 for the GPGPU_WALKER command. */
         props->maxComputeWorkgroupSubgroups = MIN2(64, pdevice->info.max_cs_threads);
         props->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR : {
         VkPhysicalDeviceFloatControlsPropertiesKHR *properties = (void *)ext;
         CORE_PROPERTY(1, 2, denormBehaviorIndependence);
         CORE_PROPERTY(1, 2, roundingModeIndependence);
         CORE_PROPERTY(1, 2, shaderDenormFlushToZeroFloat16);
         CORE_PROPERTY(1, 2, shaderDenormPreserveFloat16);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTEFloat16);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTZFloat16);
         CORE_PROPERTY(1, 2, shaderSignedZeroInfNanPreserveFloat16);
         CORE_PROPERTY(1, 2, shaderDenormFlushToZeroFloat32);
         CORE_PROPERTY(1, 2, shaderDenormPreserveFloat32);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTEFloat32);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTZFloat32);
         CORE_PROPERTY(1, 2, shaderSignedZeroInfNanPreserveFloat32);
         CORE_PROPERTY(1, 2, shaderDenormFlushToZeroFloat64);
         CORE_PROPERTY(1, 2, shaderDenormPreserveFloat64);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTEFloat64);
         CORE_PROPERTY(1, 2, shaderRoundingModeRTZFloat64);
         CORE_PROPERTY(1, 2, shaderSignedZeroInfNanPreserveFloat64);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *props =
            (VkPhysicalDeviceTexelBufferAlignmentPropertiesEXT *)ext;

         /* From the SKL PRM Vol. 2d, docs for RENDER_SURFACE_STATE::Surface
          * Base Address:
          *
          *    "For SURFTYPE_BUFFER non-rendertarget surfaces, this field
          *    specifies the base address of the first element of the surface,
          *    computed in software by adding the surface base address to the
          *    byte offset of the element in the buffer. The base address must
          *    be aligned to element size."
          *
          * The typed dataport messages require that things be texel aligned.
          * Otherwise, we may just load/store the wrong data or, in the worst
          * case, there may be hangs.
          */
         props->storageTexelBufferOffsetAlignmentBytes = 16;
         props->storageTexelBufferOffsetSingleTexelAlignment = true;

         /* The sampler, however, is much more forgiving and it can handle
          * arbitrary byte alignment for linear and buffer surfaces.  It's
          * hard to find a good PRM citation for this but years of empirical
          * experience demonstrate that this is true.
          */
         props->uniformTexelBufferOffsetAlignmentBytes = 1;
         props->uniformTexelBufferOffsetSingleTexelAlignment = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR: {
         VkPhysicalDeviceTimelineSemaphorePropertiesKHR *properties =
            (VkPhysicalDeviceTimelineSemaphorePropertiesKHR *) ext;
         CORE_PROPERTY(1, 2, maxTimelineSemaphoreValueDifference);
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
         VkPhysicalDeviceTransformFeedbackPropertiesEXT *props =
            (VkPhysicalDeviceTransformFeedbackPropertiesEXT *)ext;

         props->maxTransformFeedbackStreams = MAX_XFB_STREAMS;
         props->maxTransformFeedbackBuffers = MAX_XFB_BUFFERS;
         props->maxTransformFeedbackBufferSize = (1ull << 32);
         props->maxTransformFeedbackStreamDataSize = 128 * 4;
         props->maxTransformFeedbackBufferDataSize = 128 * 4;
         props->maxTransformFeedbackBufferDataStride = 2048;
         props->transformFeedbackQueries = true;
         props->transformFeedbackStreamsLinesTriangles = false;
         props->transformFeedbackRasterizationStreamSelect = false;
         /* This requires MI_MATH */
         props->transformFeedbackDraw = pdevice->info.is_haswell ||
                                        pdevice->info.gen >= 8;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *props =
            (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
         /* We have to restrict this a bit for multiview */
         props->maxVertexAttribDivisor = UINT32_MAX / 16;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES:
         anv_get_physical_device_properties_1_1(pdevice, (void *)ext);
         break;

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
         anv_get_physical_device_properties_1_2(pdevice, (void *)ext);
         break;

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

#undef CORE_RENAMED_PROPERTY
#undef CORE_PROPERTY
}

/* We support exactly one queue family. */
static const VkQueueFamilyProperties
anv_queue_family_properties = {
   .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                 VK_QUEUE_COMPUTE_BIT |
                 VK_QUEUE_TRANSFER_BIT,
   .queueCount = 1,
   .timestampValidBits = 36, /* XXX: Real value here */
   .minImageTransferGranularity = { 1, 1, 1 },
};

void anv_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE(out, pQueueFamilyProperties, pCount);

   vk_outarray_append(&out, p) {
      *p = anv_queue_family_properties;
   }
}

void anv_GetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2*                   pQueueFamilyProperties)
{

   VK_OUTARRAY_MAKE(out, pQueueFamilyProperties, pQueueFamilyPropertyCount);

   vk_outarray_append(&out, p) {
      p->queueFamilyProperties = anv_queue_family_properties;

      vk_foreach_struct(s, p->pNext) {
         anv_debug_ignored_stype(s->sType);
      }
   }
}

void anv_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);

   pMemoryProperties->memoryTypeCount = physical_device->memory.type_count;
   for (uint32_t i = 0; i < physical_device->memory.type_count; i++) {
      pMemoryProperties->memoryTypes[i] = (VkMemoryType) {
         .propertyFlags = physical_device->memory.types[i].propertyFlags,
         .heapIndex     = physical_device->memory.types[i].heapIndex,
      };
   }

   pMemoryProperties->memoryHeapCount = physical_device->memory.heap_count;
   for (uint32_t i = 0; i < physical_device->memory.heap_count; i++) {
      pMemoryProperties->memoryHeaps[i] = (VkMemoryHeap) {
         .size    = physical_device->memory.heaps[i].size,
         .flags   = physical_device->memory.heaps[i].flags,
      };
   }
}

static void
anv_get_memory_budget(VkPhysicalDevice physicalDevice,
                      VkPhysicalDeviceMemoryBudgetPropertiesEXT *memoryBudget)
{
   ANV_FROM_HANDLE(anv_physical_device, device, physicalDevice);
   uint64_t sys_available;
   ASSERTED bool has_available_memory =
      os_get_available_system_memory(&sys_available);
   assert(has_available_memory);

   VkDeviceSize total_heaps_size = 0;
   for (size_t i = 0; i < device->memory.heap_count; i++)
         total_heaps_size += device->memory.heaps[i].size;

   for (size_t i = 0; i < device->memory.heap_count; i++) {
      VkDeviceSize heap_size = device->memory.heaps[i].size;
      VkDeviceSize heap_used = device->memory.heaps[i].used;
      VkDeviceSize heap_budget;

      double heap_proportion = (double) heap_size / total_heaps_size;
      VkDeviceSize sys_available_prop = sys_available * heap_proportion;

      /*
       * Let's not incite the app to starve the system: report at most 90% of
       * available system memory.
       */
      uint64_t heap_available = sys_available_prop * 9 / 10;
      heap_budget = MIN2(heap_size, heap_used + heap_available);

      /*
       * Round down to the nearest MB
       */
      heap_budget &= ~((1ull << 20) - 1);

      /*
       * The heapBudget value must be non-zero for array elements less than
       * VkPhysicalDeviceMemoryProperties::memoryHeapCount. The heapBudget
       * value must be less than or equal to VkMemoryHeap::size for each heap.
       */
      assert(0 < heap_budget && heap_budget <= heap_size);

      memoryBudget->heapUsage[i] = heap_used;
      memoryBudget->heapBudget[i] = heap_budget;
   }

   /* The heapBudget and heapUsage values must be zero for array elements
    * greater than or equal to VkPhysicalDeviceMemoryProperties::memoryHeapCount
    */
   for (uint32_t i = device->memory.heap_count; i < VK_MAX_MEMORY_HEAPS; i++) {
      memoryBudget->heapBudget[i] = 0;
      memoryBudget->heapUsage[i] = 0;
   }
}

void anv_GetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties2*          pMemoryProperties)
{
   anv_GetPhysicalDeviceMemoryProperties(physicalDevice,
                                         &pMemoryProperties->memoryProperties);

   vk_foreach_struct(ext, pMemoryProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT:
         anv_get_memory_budget(physicalDevice, (void*)ext);
         break;
      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void
anv_GetDeviceGroupPeerMemoryFeatures(
    VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlags*                   pPeerMemoryFeatures)
{
   assert(localDeviceIndex == 0 && remoteDeviceIndex == 0);
   *pPeerMemoryFeatures = VK_PEER_MEMORY_FEATURE_COPY_SRC_BIT |
                          VK_PEER_MEMORY_FEATURE_COPY_DST_BIT |
                          VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT |
                          VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT;
}

PFN_vkVoidFunction anv_GetInstanceProcAddr(
    VkInstance                                  _instance,
    const char*                                 pName)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (pName == NULL)
      return NULL;

#define LOOKUP_ANV_ENTRYPOINT(entrypoint) \
   if (strcmp(pName, "vk" #entrypoint) == 0) \
      return (PFN_vkVoidFunction)anv_##entrypoint

   LOOKUP_ANV_ENTRYPOINT(EnumerateInstanceExtensionProperties);
   LOOKUP_ANV_ENTRYPOINT(EnumerateInstanceLayerProperties);
   LOOKUP_ANV_ENTRYPOINT(EnumerateInstanceVersion);
   LOOKUP_ANV_ENTRYPOINT(CreateInstance);

   /* GetInstanceProcAddr() can also be called with a NULL instance.
    * See https://gitlab.khronos.org/vulkan/vulkan/issues/2057
    */
   LOOKUP_ANV_ENTRYPOINT(GetInstanceProcAddr);

#undef LOOKUP_ANV_ENTRYPOINT

   if (instance == NULL)
      return NULL;

   int idx = anv_get_instance_entrypoint_index(pName);
   if (idx >= 0)
      return instance->dispatch.entrypoints[idx];

   idx = anv_get_physical_device_entrypoint_index(pName);
   if (idx >= 0)
      return instance->physical_device_dispatch.entrypoints[idx];

   idx = anv_get_device_entrypoint_index(pName);
   if (idx >= 0)
      return instance->device_dispatch.entrypoints[idx];

   return NULL;
}

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName)
{
   return anv_GetInstanceProcAddr(instance, pName);
}

PFN_vkVoidFunction anv_GetDeviceProcAddr(
    VkDevice                                    _device,
    const char*                                 pName)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (!device || !pName)
      return NULL;

   int idx = anv_get_device_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return device->dispatch.entrypoints[idx];
}

/* With version 4+ of the loader interface the ICD should expose
 * vk_icdGetPhysicalDeviceProcAddr()
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
    VkInstance  _instance,
    const char* pName);

PFN_vkVoidFunction vk_icdGetPhysicalDeviceProcAddr(
    VkInstance  _instance,
    const char* pName)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   if (!pName || !instance)
      return NULL;

   int idx = anv_get_physical_device_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return instance->physical_device_dispatch.entrypoints[idx];
}


VkResult
anv_CreateDebugReportCallbackEXT(VkInstance _instance,
                                 const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkDebugReportCallbackEXT* pCallback)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   return vk_create_debug_report_callback(&instance->debug_report_callbacks,
                                          pCreateInfo, pAllocator, &instance->alloc,
                                          pCallback);
}

void
anv_DestroyDebugReportCallbackEXT(VkInstance _instance,
                                  VkDebugReportCallbackEXT _callback,
                                  const VkAllocationCallbacks* pAllocator)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   vk_destroy_debug_report_callback(&instance->debug_report_callbacks,
                                    _callback, pAllocator, &instance->alloc);
}

void
anv_DebugReportMessageEXT(VkInstance _instance,
                          VkDebugReportFlagsEXT flags,
                          VkDebugReportObjectTypeEXT objectType,
                          uint64_t object,
                          size_t location,
                          int32_t messageCode,
                          const char* pLayerPrefix,
                          const char* pMessage)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   vk_debug_report(&instance->debug_report_callbacks, flags, objectType,
                   object, location, messageCode, pLayerPrefix, pMessage);
}

static struct anv_state
anv_state_pool_emit_data(struct anv_state_pool *pool, size_t size, size_t align, const void *p)
{
   struct anv_state state;

   state = anv_state_pool_alloc(pool, size, align);
   memcpy(state.map, p, size);

   return state;
}

static void
anv_device_init_border_colors(struct anv_device *device)
{
   if (device->info.is_haswell) {
      static const struct hsw_border_color border_colors[] = {
         [VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK] =  { .float32 = { 0.0, 0.0, 0.0, 0.0 } },
         [VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK] =       { .float32 = { 0.0, 0.0, 0.0, 1.0 } },
         [VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE] =       { .float32 = { 1.0, 1.0, 1.0, 1.0 } },
         [VK_BORDER_COLOR_INT_TRANSPARENT_BLACK] =    { .uint32 = { 0, 0, 0, 0 } },
         [VK_BORDER_COLOR_INT_OPAQUE_BLACK] =         { .uint32 = { 0, 0, 0, 1 } },
         [VK_BORDER_COLOR_INT_OPAQUE_WHITE] =         { .uint32 = { 1, 1, 1, 1 } },
      };

      device->border_colors =
         anv_state_pool_emit_data(&device->dynamic_state_pool,
                                  sizeof(border_colors), 512, border_colors);
   } else {
      static const struct gen8_border_color border_colors[] = {
         [VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK] =  { .float32 = { 0.0, 0.0, 0.0, 0.0 } },
         [VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK] =       { .float32 = { 0.0, 0.0, 0.0, 1.0 } },
         [VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE] =       { .float32 = { 1.0, 1.0, 1.0, 1.0 } },
         [VK_BORDER_COLOR_INT_TRANSPARENT_BLACK] =    { .uint32 = { 0, 0, 0, 0 } },
         [VK_BORDER_COLOR_INT_OPAQUE_BLACK] =         { .uint32 = { 0, 0, 0, 1 } },
         [VK_BORDER_COLOR_INT_OPAQUE_WHITE] =         { .uint32 = { 1, 1, 1, 1 } },
      };

      device->border_colors =
         anv_state_pool_emit_data(&device->dynamic_state_pool,
                                  sizeof(border_colors), 64, border_colors);
   }
}

static VkResult
anv_device_init_trivial_batch(struct anv_device *device)
{
   VkResult result = anv_device_alloc_bo(device, 4096,
                                         ANV_BO_ALLOC_MAPPED,
                                         0 /* explicit_address */,
                                         &device->trivial_batch_bo);
   if (result != VK_SUCCESS)
      return result;

   struct anv_batch batch = {
      .start = device->trivial_batch_bo->map,
      .next = device->trivial_batch_bo->map,
      .end = device->trivial_batch_bo->map + 4096,
   };

   anv_batch_emit(&batch, GEN7_MI_BATCH_BUFFER_END, bbe);
   anv_batch_emit(&batch, GEN7_MI_NOOP, noop);

   if (!device->info.has_llc)
      gen_clflush_range(batch.start, batch.next - batch.start);

   return VK_SUCCESS;
}

VkResult anv_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, device, physicalDevice);
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < ANV_DEVICE_EXTENSION_COUNT; i++) {
      if (device->supported_extensions.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = anv_device_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

static int
vk_priority_to_gen(int priority)
{
   switch (priority) {
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_EXT:
      return GEN_CONTEXT_LOW_PRIORITY;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT:
      return GEN_CONTEXT_MEDIUM_PRIORITY;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_EXT:
      return GEN_CONTEXT_HIGH_PRIORITY;
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_EXT:
      return GEN_CONTEXT_REALTIME_PRIORITY;
   default:
      unreachable("Invalid priority");
   }
}

static VkResult
anv_device_init_hiz_clear_value_bo(struct anv_device *device)
{
   VkResult result = anv_device_alloc_bo(device, 4096,
                                         ANV_BO_ALLOC_MAPPED,
                                         0 /* explicit_address */,
                                         &device->hiz_clear_bo);
   if (result != VK_SUCCESS)
      return result;

   union isl_color_value hiz_clear = { .u32 = { 0, } };
   hiz_clear.f32[0] = ANV_HZ_FC_VAL;

   memcpy(device->hiz_clear_bo->map, hiz_clear.u32, sizeof(hiz_clear.u32));

   if (!device->info.has_llc)
      gen_clflush_range(device->hiz_clear_bo->map, sizeof(hiz_clear.u32));

   return VK_SUCCESS;
}

static bool
get_bo_from_pool(struct gen_batch_decode_bo *ret,
                 struct anv_block_pool *pool,
                 uint64_t address)
{
   anv_block_pool_foreach_bo(bo, pool) {
      uint64_t bo_address = gen_48b_address(bo->offset);
      if (address >= bo_address && address < (bo_address + bo->size)) {
         *ret = (struct gen_batch_decode_bo) {
            .addr = bo_address,
            .size = bo->size,
            .map = bo->map,
         };
         return true;
      }
   }
   return false;
}

/* Finding a buffer for batch decoding */
static struct gen_batch_decode_bo
decode_get_bo(void *v_batch, bool ppgtt, uint64_t address)
{
   struct anv_device *device = v_batch;
   struct gen_batch_decode_bo ret_bo = {};

   assert(ppgtt);

   if (get_bo_from_pool(&ret_bo, &device->dynamic_state_pool.block_pool, address))
      return ret_bo;
   if (get_bo_from_pool(&ret_bo, &device->instruction_state_pool.block_pool, address))
      return ret_bo;
   if (get_bo_from_pool(&ret_bo, &device->binding_table_pool.block_pool, address))
      return ret_bo;
   if (get_bo_from_pool(&ret_bo, &device->surface_state_pool.block_pool, address))
      return ret_bo;

   if (!device->cmd_buffer_being_decoded)
      return (struct gen_batch_decode_bo) { };

   struct anv_batch_bo **bo;

   u_vector_foreach(bo, &device->cmd_buffer_being_decoded->seen_bbos) {
      /* The decoder zeroes out the top 16 bits, so we need to as well */
      uint64_t bo_address = (*bo)->bo->offset & (~0ull >> 16);

      if (address >= bo_address && address < bo_address + (*bo)->bo->size) {
         return (struct gen_batch_decode_bo) {
            .addr = bo_address,
            .size = (*bo)->bo->size,
            .map = (*bo)->bo->map,
         };
      }
   }

   return (struct gen_batch_decode_bo) { };
}

struct gen_aux_map_buffer {
   struct gen_buffer base;
   struct anv_state state;
};

static struct gen_buffer *
gen_aux_map_buffer_alloc(void *driver_ctx, uint32_t size)
{
   struct gen_aux_map_buffer *buf = malloc(sizeof(struct gen_aux_map_buffer));
   if (!buf)
      return NULL;

   struct anv_device *device = (struct anv_device*)driver_ctx;
   assert(device->physical->supports_48bit_addresses &&
          device->physical->use_softpin);

   struct anv_state_pool *pool = &device->dynamic_state_pool;
   buf->state = anv_state_pool_alloc(pool, size, size);

   buf->base.gpu = pool->block_pool.bo->offset + buf->state.offset;
   buf->base.gpu_end = buf->base.gpu + buf->state.alloc_size;
   buf->base.map = buf->state.map;
   buf->base.driver_bo = &buf->state;
   return &buf->base;
}

static void
gen_aux_map_buffer_free(void *driver_ctx, struct gen_buffer *buffer)
{
   struct gen_aux_map_buffer *buf = (struct gen_aux_map_buffer*)buffer;
   struct anv_device *device = (struct anv_device*)driver_ctx;
   struct anv_state_pool *pool = &device->dynamic_state_pool;
   anv_state_pool_free(pool, buf->state);
   free(buf);
}

static struct gen_mapped_pinned_buffer_alloc aux_map_allocator = {
   .alloc = gen_aux_map_buffer_alloc,
   .free = gen_aux_map_buffer_free,
};

static VkResult
check_physical_device_features(VkPhysicalDevice physicalDevice,
                               const VkPhysicalDeviceFeatures *features)
{
   VkPhysicalDeviceFeatures supported_features;
   anv_GetPhysicalDeviceFeatures(physicalDevice, &supported_features);
   VkBool32 *supported_feature = (VkBool32 *)&supported_features;
   VkBool32 *enabled_feature = (VkBool32 *)features;
   unsigned num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
   for (uint32_t i = 0; i < num_features; i++) {
      if (enabled_feature[i] && !supported_feature[i])
         return vk_error(VK_ERROR_FEATURE_NOT_PRESENT);
   }

   return VK_SUCCESS;
}

VkResult anv_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   VkResult result;
   struct anv_device *device;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   struct anv_device_extension_table enabled_extensions = { };
   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < ANV_DEVICE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    anv_device_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= ANV_DEVICE_EXTENSION_COUNT)
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      if (!physical_device->supported_extensions.extensions[idx])
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      enabled_extensions.extensions[idx] = true;
   }

   /* Check enabled features */
   bool robust_buffer_access = false;
   if (pCreateInfo->pEnabledFeatures) {
      result = check_physical_device_features(physicalDevice,
                                              pCreateInfo->pEnabledFeatures);
      if (result != VK_SUCCESS)
         return result;

      if (pCreateInfo->pEnabledFeatures->robustBufferAccess)
         robust_buffer_access = true;
   }

   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
         const VkPhysicalDeviceFeatures2 *features = (const void *)ext;
         result = check_physical_device_features(physicalDevice,
                                                 &features->features);
         if (result != VK_SUCCESS)
            return result;

         if (features->features.robustBufferAccess)
            robust_buffer_access = true;
         break;
      }

      default:
         /* Don't warn */
         break;
      }
   }

   /* Check requested queues and fail if we are requested to create any
    * queues with flags we don't support.
    */
   assert(pCreateInfo->queueCreateInfoCount > 0);
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      if (pCreateInfo->pQueueCreateInfos[i].flags != 0)
         return vk_error(VK_ERROR_INITIALIZATION_FAILED);
   }

   /* Check if client specified queue priority. */
   const VkDeviceQueueGlobalPriorityCreateInfoEXT *queue_priority =
      vk_find_struct_const(pCreateInfo->pQueueCreateInfos[0].pNext,
                           DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_EXT);

   VkQueueGlobalPriorityEXT priority =
      queue_priority ? queue_priority->globalPriority :
         VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT;

   device = vk_alloc2(&physical_device->instance->alloc, pAllocator,
                       sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_device_init(&device->vk, pCreateInfo,
                  &physical_device->instance->alloc, pAllocator);

   if (INTEL_DEBUG & DEBUG_BATCH) {
      const unsigned decode_flags =
         GEN_BATCH_DECODE_FULL |
         ((INTEL_DEBUG & DEBUG_COLOR) ? GEN_BATCH_DECODE_IN_COLOR : 0) |
         GEN_BATCH_DECODE_OFFSETS |
         GEN_BATCH_DECODE_FLOATS;

      gen_batch_decode_ctx_init(&device->decoder_ctx,
                                &physical_device->info,
                                stderr, decode_flags, NULL,
                                decode_get_bo, NULL, device);
   }

   device->physical = physical_device;
   device->no_hw = physical_device->no_hw;
   device->_lost = false;

   /* XXX(chadv): Can we dup() physicalDevice->fd here? */
   device->fd = open(physical_device->path, O_RDWR | O_CLOEXEC);
   if (device->fd == -1) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_device;
   }

   device->context_id = anv_gem_create_context(device);
   if (device->context_id == -1) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_fd;
   }

   device->has_thread_submit = physical_device->has_thread_submit;

   result = anv_queue_init(device, &device->queue);
   if (result != VK_SUCCESS)
      goto fail_context_id;

   if (physical_device->use_softpin) {
      if (pthread_mutex_init(&device->vma_mutex, NULL) != 0) {
         result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
         goto fail_queue;
      }

      /* keep the page with address zero out of the allocator */
      util_vma_heap_init(&device->vma_lo,
                         LOW_HEAP_MIN_ADDRESS, LOW_HEAP_SIZE);

      util_vma_heap_init(&device->vma_cva, CLIENT_VISIBLE_HEAP_MIN_ADDRESS,
                         CLIENT_VISIBLE_HEAP_SIZE);

      /* Leave the last 4GiB out of the high vma range, so that no state
       * base address + size can overflow 48 bits. For more information see
       * the comment about Wa32bitGeneralStateOffset in anv_allocator.c
       */
      util_vma_heap_init(&device->vma_hi, HIGH_HEAP_MIN_ADDRESS,
                         physical_device->gtt_size - (1ull << 32) -
                         HIGH_HEAP_MIN_ADDRESS);
   }

   list_inithead(&device->memory_objects);

   /* As per spec, the driver implementation may deny requests to acquire
    * a priority above the default priority (MEDIUM) if the caller does not
    * have sufficient privileges. In this scenario VK_ERROR_NOT_PERMITTED_EXT
    * is returned.
    */
   if (physical_device->has_context_priority) {
      int err = anv_gem_set_context_param(device->fd, device->context_id,
                                          I915_CONTEXT_PARAM_PRIORITY,
                                          vk_priority_to_gen(priority));
      if (err != 0 && priority > VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_EXT) {
         result = vk_error(VK_ERROR_NOT_PERMITTED_EXT);
         goto fail_vmas;
      }
   }

   device->info = physical_device->info;
   device->isl_dev = physical_device->isl_dev;

   /* On Broadwell and later, we can use batch chaining to more efficiently
    * implement growing command buffers.  Prior to Haswell, the kernel
    * command parser gets in the way and we have to fall back to growing
    * the batch.
    */
   device->can_chain_batches = device->info.gen >= 8;

   device->robust_buffer_access = robust_buffer_access;
   device->enabled_extensions = enabled_extensions;

   const struct anv_instance *instance = physical_device->instance;
   for (unsigned i = 0; i < ARRAY_SIZE(device->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!anv_device_entrypoint_is_enabled(i, instance->app_info.api_version,
                                            &instance->enabled_extensions,
                                            &device->enabled_extensions)) {
         device->dispatch.entrypoints[i] = NULL;
      } else {
         device->dispatch.entrypoints[i] =
            anv_resolve_device_entrypoint(&device->info, i);
      }
   }

   if (pthread_mutex_init(&device->mutex, NULL) != 0) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_queue;
   }

   pthread_condattr_t condattr;
   if (pthread_condattr_init(&condattr) != 0) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   if (pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   if (pthread_cond_init(&device->queue_submit, &condattr) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   pthread_condattr_destroy(&condattr);

   result = anv_bo_cache_init(&device->bo_cache);
   if (result != VK_SUCCESS)
      goto fail_queue_cond;

   anv_bo_pool_init(&device->batch_bo_pool, device);

   result = anv_state_pool_init(&device->dynamic_state_pool, device,
                                DYNAMIC_STATE_POOL_MIN_ADDRESS, 0, 16384);
   if (result != VK_SUCCESS)
      goto fail_batch_bo_pool;

   if (device->info.gen >= 8) {
      /* The border color pointer is limited to 24 bits, so we need to make
       * sure that any such color used at any point in the program doesn't
       * exceed that limit.
       * We achieve that by reserving all the custom border colors we support
       * right off the bat, so they are close to the base address.
       */
      anv_state_reserved_pool_init(&device->custom_border_colors,
                                   &device->dynamic_state_pool,
                                   MAX_CUSTOM_BORDER_COLORS,
                                   sizeof(struct gen8_border_color), 64);
   }

   result = anv_state_pool_init(&device->instruction_state_pool, device,
                                INSTRUCTION_STATE_POOL_MIN_ADDRESS, 0, 16384);
   if (result != VK_SUCCESS)
      goto fail_dynamic_state_pool;

   result = anv_state_pool_init(&device->surface_state_pool, device,
                                SURFACE_STATE_POOL_MIN_ADDRESS, 0, 4096);
   if (result != VK_SUCCESS)
      goto fail_instruction_state_pool;

   if (physical_device->use_softpin) {
      int64_t bt_pool_offset = (int64_t)BINDING_TABLE_POOL_MIN_ADDRESS -
                               (int64_t)SURFACE_STATE_POOL_MIN_ADDRESS;
      assert(INT32_MIN < bt_pool_offset && bt_pool_offset < 0);
      result = anv_state_pool_init(&device->binding_table_pool, device,
                                   SURFACE_STATE_POOL_MIN_ADDRESS,
                                   bt_pool_offset, 4096);
      if (result != VK_SUCCESS)
         goto fail_surface_state_pool;
   }

   if (device->info.has_aux_map) {
      device->aux_map_ctx = gen_aux_map_init(device, &aux_map_allocator,
                                             &physical_device->info);
      if (!device->aux_map_ctx)
         goto fail_binding_table_pool;
   }

   result = anv_device_alloc_bo(device, 4096,
                                ANV_BO_ALLOC_CAPTURE | ANV_BO_ALLOC_MAPPED /* flags */,
                                0 /* explicit_address */,
                                &device->workaround_bo);
   if (result != VK_SUCCESS)
      goto fail_surface_aux_map_pool;

   device->workaround_address = (struct anv_address) {
      .bo = device->workaround_bo,
      .offset = align_u32(
         intel_debug_write_identifiers(device->workaround_bo->map,
                                       device->workaround_bo->size,
                                       "Anv") + 8, 8),
   };

   device->debug_frame_desc =
      intel_debug_get_identifier_block(device->workaround_bo->map,
                                       device->workaround_bo->size,
                                       GEN_DEBUG_BLOCK_TYPE_FRAME);

   result = anv_device_init_trivial_batch(device);
   if (result != VK_SUCCESS)
      goto fail_workaround_bo;

   /* Allocate a null surface state at surface state offset 0.  This makes
    * NULL descriptor handling trivial because we can just memset structures
    * to zero and they have a valid descriptor.
    */
   device->null_surface_state =
      anv_state_pool_alloc(&device->surface_state_pool,
                           device->isl_dev.ss.size,
                           device->isl_dev.ss.align);
   isl_null_fill_state(&device->isl_dev, device->null_surface_state.map,
                       isl_extent3d(1, 1, 1) /* This shouldn't matter */);
   assert(device->null_surface_state.offset == 0);

   if (device->info.gen >= 10) {
      result = anv_device_init_hiz_clear_value_bo(device);
      if (result != VK_SUCCESS)
         goto fail_trivial_batch_bo;
   }

   anv_scratch_pool_init(device, &device->scratch_pool);

   switch (device->info.gen) {
   case 7:
      if (!device->info.is_haswell)
         result = gen7_init_device_state(device);
      else
         result = gen75_init_device_state(device);
      break;
   case 8:
      result = gen8_init_device_state(device);
      break;
   case 9:
      result = gen9_init_device_state(device);
      break;
   case 11:
      result = gen11_init_device_state(device);
      break;
   case 12:
      result = gen12_init_device_state(device);
      break;
   default:
      /* Shouldn't get here as we don't create physical devices for any other
       * gens. */
      unreachable("unhandled gen");
   }
   if (result != VK_SUCCESS)
      goto fail_clear_value_bo;

   anv_pipeline_cache_init(&device->default_pipeline_cache, device,
                           true /* cache_enabled */, false /* external_sync */);

   anv_device_init_blorp(device);

   anv_device_init_border_colors(device);

   anv_device_perf_init(device);

   *pDevice = anv_device_to_handle(device);

   return VK_SUCCESS;

 fail_clear_value_bo:
   if (device->info.gen >= 10)
      anv_device_release_bo(device, device->hiz_clear_bo);
   anv_scratch_pool_finish(device, &device->scratch_pool);
 fail_trivial_batch_bo:
   anv_device_release_bo(device, device->trivial_batch_bo);
 fail_workaround_bo:
   anv_device_release_bo(device, device->workaround_bo);
 fail_surface_aux_map_pool:
   if (device->info.has_aux_map) {
      gen_aux_map_finish(device->aux_map_ctx);
      device->aux_map_ctx = NULL;
   }
 fail_binding_table_pool:
   if (physical_device->use_softpin)
      anv_state_pool_finish(&device->binding_table_pool);
 fail_surface_state_pool:
   anv_state_pool_finish(&device->surface_state_pool);
 fail_instruction_state_pool:
   anv_state_pool_finish(&device->instruction_state_pool);
 fail_dynamic_state_pool:
   if (device->info.gen >= 8)
      anv_state_reserved_pool_finish(&device->custom_border_colors);
   anv_state_pool_finish(&device->dynamic_state_pool);
 fail_batch_bo_pool:
   anv_bo_pool_finish(&device->batch_bo_pool);
   anv_bo_cache_finish(&device->bo_cache);
 fail_queue_cond:
   pthread_cond_destroy(&device->queue_submit);
 fail_mutex:
   pthread_mutex_destroy(&device->mutex);
 fail_vmas:
   if (physical_device->use_softpin) {
      util_vma_heap_finish(&device->vma_hi);
      util_vma_heap_finish(&device->vma_cva);
      util_vma_heap_finish(&device->vma_lo);
   }
 fail_queue:
   anv_queue_finish(&device->queue);
 fail_context_id:
   anv_gem_destroy_context(device, device->context_id);
 fail_fd:
   close(device->fd);
 fail_device:
   vk_free(&device->vk.alloc, device);

   return result;
}

void anv_DestroyDevice(
    VkDevice                                    _device,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (!device)
      return;

   anv_queue_finish(&device->queue);

   anv_device_finish_blorp(device);

   anv_pipeline_cache_finish(&device->default_pipeline_cache);

#ifdef HAVE_VALGRIND
   /* We only need to free these to prevent valgrind errors.  The backing
    * BO will go away in a couple of lines so we don't actually leak.
    */
   if (device->info.gen >= 8)
      anv_state_reserved_pool_finish(&device->custom_border_colors);
   anv_state_pool_free(&device->dynamic_state_pool, device->border_colors);
   anv_state_pool_free(&device->dynamic_state_pool, device->slice_hash);
#endif

   anv_scratch_pool_finish(device, &device->scratch_pool);

   anv_device_release_bo(device, device->workaround_bo);
   anv_device_release_bo(device, device->trivial_batch_bo);
   if (device->info.gen >= 10)
      anv_device_release_bo(device, device->hiz_clear_bo);

   if (device->info.has_aux_map) {
      gen_aux_map_finish(device->aux_map_ctx);
      device->aux_map_ctx = NULL;
   }

   if (device->physical->use_softpin)
      anv_state_pool_finish(&device->binding_table_pool);
   anv_state_pool_finish(&device->surface_state_pool);
   anv_state_pool_finish(&device->instruction_state_pool);
   anv_state_pool_finish(&device->dynamic_state_pool);

   anv_bo_pool_finish(&device->batch_bo_pool);

   anv_bo_cache_finish(&device->bo_cache);

   if (device->physical->use_softpin) {
      util_vma_heap_finish(&device->vma_hi);
      util_vma_heap_finish(&device->vma_cva);
      util_vma_heap_finish(&device->vma_lo);
   }

   pthread_cond_destroy(&device->queue_submit);
   pthread_mutex_destroy(&device->mutex);

   anv_gem_destroy_context(device, device->context_id);

   if (INTEL_DEBUG & DEBUG_BATCH)
      gen_batch_decode_ctx_finish(&device->decoder_ctx);

   close(device->fd);

   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VkResult anv_EnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult anv_EnumerateDeviceLayerProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(VK_ERROR_LAYER_NOT_PRESENT);
}

void anv_GetDeviceQueue(
    VkDevice                                    _device,
    uint32_t                                    queueNodeIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue)
{
   const VkDeviceQueueInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndex = queueNodeIndex,
      .queueIndex = queueIndex,
   };

   anv_GetDeviceQueue2(_device, &info, pQueue);
}

void anv_GetDeviceQueue2(
    VkDevice                                    _device,
    const VkDeviceQueueInfo2*                   pQueueInfo,
    VkQueue*                                    pQueue)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   assert(pQueueInfo->queueIndex == 0);

   if (pQueueInfo->flags == device->queue.flags)
      *pQueue = anv_queue_to_handle(&device->queue);
   else
      *pQueue = NULL;
}

void
_anv_device_report_lost(struct anv_device *device)
{
   assert(p_atomic_read(&device->_lost) > 0);

   device->lost_reported = true;

   struct anv_queue *queue = &device->queue;

   __vk_errorf(device->physical->instance, device,
               VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT,
               VK_ERROR_DEVICE_LOST,
               queue->error_file, queue->error_line,
               "%s", queue->error_msg);
}

VkResult
_anv_device_set_lost(struct anv_device *device,
                     const char *file, int line,
                     const char *msg, ...)
{
   VkResult err;
   va_list ap;

   if (p_atomic_read(&device->_lost) > 0)
      return VK_ERROR_DEVICE_LOST;

   p_atomic_inc(&device->_lost);
   device->lost_reported = true;

   va_start(ap, msg);
   err = __vk_errorv(device->physical->instance, device,
                     VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT,
                     VK_ERROR_DEVICE_LOST, file, line, msg, ap);
   va_end(ap);

   if (env_var_as_boolean("ANV_ABORT_ON_DEVICE_LOSS", false))
      abort();

   return err;
}

VkResult
_anv_queue_set_lost(struct anv_queue *queue,
                     const char *file, int line,
                     const char *msg, ...)
{
   va_list ap;

   if (queue->lost)
      return VK_ERROR_DEVICE_LOST;

   queue->lost = true;

   queue->error_file = file;
   queue->error_line = line;
   va_start(ap, msg);
   vsnprintf(queue->error_msg, sizeof(queue->error_msg),
             msg, ap);
   va_end(ap);

   p_atomic_inc(&queue->device->_lost);

   if (env_var_as_boolean("ANV_ABORT_ON_DEVICE_LOSS", false))
      abort();

   return VK_ERROR_DEVICE_LOST;
}

VkResult
anv_device_query_status(struct anv_device *device)
{
   /* This isn't likely as most of the callers of this function already check
    * for it.  However, it doesn't hurt to check and it potentially lets us
    * avoid an ioctl.
    */
   if (anv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   uint32_t active, pending;
   int ret = anv_gem_gpu_get_reset_stats(device, &active, &pending);
   if (ret == -1) {
      /* We don't know the real error. */
      return anv_device_set_lost(device, "get_reset_stats failed: %m");
   }

   if (active) {
      return anv_device_set_lost(device, "GPU hung on one of our command buffers");
   } else if (pending) {
      return anv_device_set_lost(device, "GPU hung with commands in-flight");
   }

   return VK_SUCCESS;
}

VkResult
anv_device_bo_busy(struct anv_device *device, struct anv_bo *bo)
{
   /* Note:  This only returns whether or not the BO is in use by an i915 GPU.
    * Other usages of the BO (such as on different hardware) will not be
    * flagged as "busy" by this ioctl.  Use with care.
    */
   int ret = anv_gem_busy(device, bo->gem_handle);
   if (ret == 1) {
      return VK_NOT_READY;
   } else if (ret == -1) {
      /* We don't know the real error. */
      return anv_device_set_lost(device, "gem wait failed: %m");
   }

   /* Query for device status after the busy call.  If the BO we're checking
    * got caught in a GPU hang we don't want to return VK_SUCCESS to the
    * client because it clearly doesn't have valid data.  Yes, this most
    * likely means an ioctl, but we just did an ioctl to query the busy status
    * so it's no great loss.
    */
   return anv_device_query_status(device);
}

VkResult
anv_device_wait(struct anv_device *device, struct anv_bo *bo,
                int64_t timeout)
{
   int ret = anv_gem_wait(device, bo->gem_handle, &timeout);
   if (ret == -1 && errno == ETIME) {
      return VK_TIMEOUT;
   } else if (ret == -1) {
      /* We don't know the real error. */
      return anv_device_set_lost(device, "gem wait failed: %m");
   }

   /* Query for device status after the wait.  If the BO we're waiting on got
    * caught in a GPU hang we don't want to return VK_SUCCESS to the client
    * because it clearly doesn't have valid data.  Yes, this most likely means
    * an ioctl, but we just did an ioctl to wait so it's no great loss.
    */
   return anv_device_query_status(device);
}

VkResult anv_DeviceWaitIdle(
    VkDevice                                    _device)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (anv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   return anv_queue_submit_simple_batch(&device->queue, NULL);
}

uint64_t
anv_vma_alloc(struct anv_device *device,
              uint64_t size, uint64_t align,
              enum anv_bo_alloc_flags alloc_flags,
              uint64_t client_address)
{
   pthread_mutex_lock(&device->vma_mutex);

   uint64_t addr = 0;

   if (alloc_flags & ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS) {
      if (client_address) {
         if (util_vma_heap_alloc_addr(&device->vma_cva,
                                      client_address, size)) {
            addr = client_address;
         }
      } else {
         addr = util_vma_heap_alloc(&device->vma_cva, size, align);
      }
      /* We don't want to fall back to other heaps */
      goto done;
   }

   assert(client_address == 0);

   if (!(alloc_flags & ANV_BO_ALLOC_32BIT_ADDRESS))
      addr = util_vma_heap_alloc(&device->vma_hi, size, align);

   if (addr == 0)
      addr = util_vma_heap_alloc(&device->vma_lo, size, align);

done:
   pthread_mutex_unlock(&device->vma_mutex);

   assert(addr == gen_48b_address(addr));
   return gen_canonical_address(addr);
}

void
anv_vma_free(struct anv_device *device,
             uint64_t address, uint64_t size)
{
   const uint64_t addr_48b = gen_48b_address(address);

   pthread_mutex_lock(&device->vma_mutex);

   if (addr_48b >= LOW_HEAP_MIN_ADDRESS &&
       addr_48b <= LOW_HEAP_MAX_ADDRESS) {
      util_vma_heap_free(&device->vma_lo, addr_48b, size);
   } else if (addr_48b >= CLIENT_VISIBLE_HEAP_MIN_ADDRESS &&
              addr_48b <= CLIENT_VISIBLE_HEAP_MAX_ADDRESS) {
      util_vma_heap_free(&device->vma_cva, addr_48b, size);
   } else {
      assert(addr_48b >= HIGH_HEAP_MIN_ADDRESS);
      util_vma_heap_free(&device->vma_hi, addr_48b, size);
   }

   pthread_mutex_unlock(&device->vma_mutex);
}

VkResult anv_AllocateMemory(
    VkDevice                                    _device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMem)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_physical_device *pdevice = device->physical;
   struct anv_device_memory *mem;
   VkResult result = VK_SUCCESS;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   /* The Vulkan 1.0.33 spec says "allocationSize must be greater than 0". */
   assert(pAllocateInfo->allocationSize > 0);

   VkDeviceSize aligned_alloc_size =
      align_u64(pAllocateInfo->allocationSize, 4096);

   if (aligned_alloc_size > MAX_MEMORY_ALLOCATION_SIZE)
      return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);

   assert(pAllocateInfo->memoryTypeIndex < pdevice->memory.type_count);
   struct anv_memory_type *mem_type =
      &pdevice->memory.types[pAllocateInfo->memoryTypeIndex];
   assert(mem_type->heapIndex < pdevice->memory.heap_count);
   struct anv_memory_heap *mem_heap =
      &pdevice->memory.heaps[mem_type->heapIndex];

   uint64_t mem_heap_used = p_atomic_read(&mem_heap->used);
   if (mem_heap_used + aligned_alloc_size > mem_heap->size)
      return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);

   mem = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*mem), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(pAllocateInfo->memoryTypeIndex < pdevice->memory.type_count);
   vk_object_base_init(&device->vk, &mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY);
   mem->type = mem_type;
   mem->map = NULL;
   mem->map_size = 0;
   mem->ahw = NULL;
   mem->host_ptr = NULL;

   enum anv_bo_alloc_flags alloc_flags = 0;

   const VkExportMemoryAllocateInfo *export_info = NULL;
   const VkImportAndroidHardwareBufferInfoANDROID *ahw_import_info = NULL;
   const VkImportMemoryFdInfoKHR *fd_info = NULL;
   const VkImportMemoryHostPointerInfoEXT *host_ptr_info = NULL;
   const VkMemoryDedicatedAllocateInfo *dedicated_info = NULL;
   VkMemoryAllocateFlags vk_flags = 0;
   uint64_t client_address = 0;

   vk_foreach_struct_const(ext, pAllocateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         export_info = (void *)ext;
         break;

      case VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID:
         ahw_import_info = (void *)ext;
         break;

      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         fd_info = (void *)ext;
         break;

      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT:
         host_ptr_info = (void *)ext;
         break;

      case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO: {
         const VkMemoryAllocateFlagsInfo *flags_info = (void *)ext;
         vk_flags = flags_info->flags;
         break;
      }

      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:
         dedicated_info = (void *)ext;
         break;

      case VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO_KHR: {
         const VkMemoryOpaqueCaptureAddressAllocateInfoKHR *addr_info =
            (const VkMemoryOpaqueCaptureAddressAllocateInfoKHR *)ext;
         client_address = addr_info->opaqueCaptureAddress;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* By default, we want all VkDeviceMemory objects to support CCS */
   if (device->physical->has_implicit_ccs)
      alloc_flags |= ANV_BO_ALLOC_IMPLICIT_CCS;

   if (vk_flags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR)
      alloc_flags |= ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS;

   if ((export_info && export_info->handleTypes) ||
       (fd_info && fd_info->handleType) ||
       (host_ptr_info && host_ptr_info->handleType)) {
      /* Anything imported or exported is EXTERNAL */
      alloc_flags |= ANV_BO_ALLOC_EXTERNAL;

      /* We can't have implicit CCS on external memory with an AUX-table.
       * Doing so would require us to sync the aux tables across processes
       * which is impractical.
       */
      if (device->info.has_aux_map)
         alloc_flags &= ~ANV_BO_ALLOC_IMPLICIT_CCS;
   }

   /* Check if we need to support Android HW buffer export. If so,
    * create AHardwareBuffer and import memory from it.
    */
   bool android_export = false;
   if (export_info && export_info->handleTypes &
       VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)
      android_export = true;

   if (ahw_import_info) {
      result = anv_import_ahw_memory(_device, mem, ahw_import_info);
      if (result != VK_SUCCESS)
         goto fail;

      goto success;
   } else if (android_export) {
      result = anv_create_ahw_memory(_device, mem, pAllocateInfo);
      if (result != VK_SUCCESS)
         goto fail;

      const VkImportAndroidHardwareBufferInfoANDROID import_info = {
         .buffer = mem->ahw,
      };
      result = anv_import_ahw_memory(_device, mem, &import_info);
      if (result != VK_SUCCESS)
         goto fail;

      goto success;
   }

   /* The Vulkan spec permits handleType to be 0, in which case the struct is
    * ignored.
    */
   if (fd_info && fd_info->handleType) {
      /* At the moment, we support only the below handle types. */
      assert(fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      result = anv_device_import_bo(device, fd_info->fd, alloc_flags,
                                    client_address, &mem->bo);
      if (result != VK_SUCCESS)
         goto fail;

      /* For security purposes, we reject importing the bo if it's smaller
       * than the requested allocation size.  This prevents a malicious client
       * from passing a buffer to a trusted client, lying about the size, and
       * telling the trusted client to try and texture from an image that goes
       * out-of-bounds.  This sort of thing could lead to GPU hangs or worse
       * in the trusted client.  The trusted client can protect itself against
       * this sort of attack but only if it can trust the buffer size.
       */
      if (mem->bo->size < aligned_alloc_size) {
         result = vk_errorf(device, device, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                            "aligned allocationSize too large for "
                            "VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT: "
                            "%"PRIu64"B > %"PRIu64"B",
                            aligned_alloc_size, mem->bo->size);
         anv_device_release_bo(device, mem->bo);
         goto fail;
      }

      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
      goto success;
   }

   if (host_ptr_info && host_ptr_info->handleType) {
      if (host_ptr_info->handleType ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_MAPPED_FOREIGN_MEMORY_BIT_EXT) {
         result = vk_error(VK_ERROR_INVALID_EXTERNAL_HANDLE);
         goto fail;
      }

      assert(host_ptr_info->handleType ==
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);

      result = anv_device_import_bo_from_host_ptr(device,
                                                  host_ptr_info->pHostPointer,
                                                  pAllocateInfo->allocationSize,
                                                  alloc_flags,
                                                  client_address,
                                                  &mem->bo);
      if (result != VK_SUCCESS)
         goto fail;

      mem->host_ptr = host_ptr_info->pHostPointer;
      goto success;
   }

   /* Regular allocate (not importing memory). */

   result = anv_device_alloc_bo(device, pAllocateInfo->allocationSize,
                                alloc_flags, client_address, &mem->bo);
   if (result != VK_SUCCESS)
      goto fail;

   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      ANV_FROM_HANDLE(anv_image, image, dedicated_info->image);

      /* Some legacy (non-modifiers) consumers need the tiling to be set on
       * the BO.  In this case, we have a dedicated allocation.
       */
      if (image->needs_set_tiling) {
         const uint32_t i915_tiling =
            isl_tiling_to_i915_tiling(image->planes[0].surface.isl.tiling);
         int ret = anv_gem_set_tiling(device, mem->bo->gem_handle,
                                      image->planes[0].surface.isl.row_pitch_B,
                                      i915_tiling);
         if (ret) {
            anv_device_release_bo(device, mem->bo);
            result = vk_errorf(device, device, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                               "failed to set BO tiling: %m");
            goto fail;
         }
      }
   }

 success:
   mem_heap_used = p_atomic_add_return(&mem_heap->used, mem->bo->size);
   if (mem_heap_used > mem_heap->size) {
      p_atomic_add(&mem_heap->used, -mem->bo->size);
      anv_device_release_bo(device, mem->bo);
      result = vk_errorf(device, device, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                         "Out of heap memory");
      goto fail;
   }

   pthread_mutex_lock(&device->mutex);
   list_addtail(&mem->link, &device->memory_objects);
   pthread_mutex_unlock(&device->mutex);

   *pMem = anv_device_memory_to_handle(mem);

   return VK_SUCCESS;

 fail:
   vk_free2(&device->vk.alloc, pAllocator, mem);

   return result;
}

VkResult anv_GetMemoryFdKHR(
    VkDevice                                    device_h,
    const VkMemoryGetFdInfoKHR*                 pGetFdInfo,
    int*                                        pFd)
{
   ANV_FROM_HANDLE(anv_device, dev, device_h);
   ANV_FROM_HANDLE(anv_device_memory, mem, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   assert(pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
          pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   return anv_device_export_bo(dev, mem->bo, pFd);
}

VkResult anv_GetMemoryFdPropertiesKHR(
    VkDevice                                    _device,
    VkExternalMemoryHandleTypeFlagBits          handleType,
    int                                         fd,
    VkMemoryFdPropertiesKHR*                    pMemoryFdProperties)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      /* dma-buf can be imported as any memory type */
      pMemoryFdProperties->memoryTypeBits =
         (1 << device->physical->memory.type_count) - 1;
      return VK_SUCCESS;

   default:
      /* The valid usage section for this function says:
       *
       *    "handleType must not be one of the handle types defined as
       *    opaque."
       *
       * So opaque handle types fall into the default "unsupported" case.
       */
      return vk_error(VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

VkResult anv_GetMemoryHostPointerPropertiesEXT(
   VkDevice                                    _device,
   VkExternalMemoryHandleTypeFlagBits          handleType,
   const void*                                 pHostPointer,
   VkMemoryHostPointerPropertiesEXT*           pMemoryHostPointerProperties)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   assert(pMemoryHostPointerProperties->sType ==
          VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      /* Host memory can be imported as any memory type. */
      pMemoryHostPointerProperties->memoryTypeBits =
         (1ull << device->physical->memory.type_count) - 1;

      return VK_SUCCESS;

   default:
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
}

void anv_FreeMemory(
    VkDevice                                    _device,
    VkDeviceMemory                              _mem,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   pthread_mutex_lock(&device->mutex);
   list_del(&mem->link);
   pthread_mutex_unlock(&device->mutex);

   if (mem->map)
      anv_UnmapMemory(_device, _mem);

   p_atomic_add(&device->physical->memory.heaps[mem->type->heapIndex].used,
                -mem->bo->size);

   anv_device_release_bo(device, mem->bo);

#if defined(ANDROID) && ANDROID_API_LEVEL >= 26
   if (mem->ahw)
      AHardwareBuffer_release(mem->ahw);
#endif

   vk_object_base_finish(&mem->base);
   vk_free2(&device->vk.alloc, pAllocator, mem);
}

VkResult anv_MapMemory(
    VkDevice                                    _device,
    VkDeviceMemory                              _memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, _memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   if (mem->host_ptr) {
      *ppData = mem->host_ptr + offset;
      return VK_SUCCESS;
   }

   if (size == VK_WHOLE_SIZE)
      size = mem->bo->size - offset;

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */
   assert(size > 0);
   assert(offset + size <= mem->bo->size);

   /* FIXME: Is this supposed to be thread safe? Since vkUnmapMemory() only
    * takes a VkDeviceMemory pointer, it seems like only one map of the memory
    * at a time is valid. We could just mmap up front and return an offset
    * pointer here, but that may exhaust virtual memory on 32 bit
    * userspace. */

   uint32_t gem_flags = 0;

   if (!device->info.has_llc &&
       (mem->type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      gem_flags |= I915_MMAP_WC;

   /* GEM will fail to map if the offset isn't 4k-aligned.  Round down. */
   uint64_t map_offset;
   if (!device->physical->has_mmap_offset)
      map_offset = offset & ~4095ull;
   else
      map_offset = 0;
   assert(offset >= map_offset);
   uint64_t map_size = (offset + size) - map_offset;

   /* Let's map whole pages */
   map_size = align_u64(map_size, 4096);

   void *map = anv_gem_mmap(device, mem->bo->gem_handle,
                            map_offset, map_size, gem_flags);
   if (map == MAP_FAILED)
      return vk_error(VK_ERROR_MEMORY_MAP_FAILED);

   mem->map = map;
   mem->map_size = map_size;

   *ppData = mem->map + (offset - map_offset);

   return VK_SUCCESS;
}

void anv_UnmapMemory(
    VkDevice                                    _device,
    VkDeviceMemory                              _memory)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, _memory);

   if (mem == NULL || mem->host_ptr)
      return;

   anv_gem_munmap(device, mem->map, mem->map_size);

   mem->map = NULL;
   mem->map_size = 0;
}

static void
clflush_mapped_ranges(struct anv_device         *device,
                      uint32_t                   count,
                      const VkMappedMemoryRange *ranges)
{
   for (uint32_t i = 0; i < count; i++) {
      ANV_FROM_HANDLE(anv_device_memory, mem, ranges[i].memory);
      if (ranges[i].offset >= mem->map_size)
         continue;

      gen_clflush_range(mem->map + ranges[i].offset,
                        MIN2(ranges[i].size, mem->map_size - ranges[i].offset));
   }
}

VkResult anv_FlushMappedMemoryRanges(
    VkDevice                                    _device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (device->info.has_llc)
      return VK_SUCCESS;

   /* Make sure the writes we're flushing have landed. */
   __builtin_ia32_mfence();

   clflush_mapped_ranges(device, memoryRangeCount, pMemoryRanges);

   return VK_SUCCESS;
}

VkResult anv_InvalidateMappedMemoryRanges(
    VkDevice                                    _device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (device->info.has_llc)
      return VK_SUCCESS;

   clflush_mapped_ranges(device, memoryRangeCount, pMemoryRanges);

   /* Make sure no reads get moved up above the invalidate. */
   __builtin_ia32_mfence();

   return VK_SUCCESS;
}

void anv_GetBufferMemoryRequirements(
    VkDevice                                    _device,
    VkBuffer                                    _buffer,
    VkMemoryRequirements*                       pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   ANV_FROM_HANDLE(anv_device, device, _device);

   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    */
   uint32_t memory_types = (1ull << device->physical->memory.type_count) - 1;

   /* Base alignment requirement of a cache line */
   uint32_t alignment = 16;

   if (buffer->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
      alignment = MAX2(alignment, ANV_UBO_ALIGNMENT);

   pMemoryRequirements->size = buffer->size;
   pMemoryRequirements->alignment = alignment;

   /* Storage and Uniform buffers should have their size aligned to
    * 32-bits to avoid boundary checks when last DWord is not complete.
    * This would ensure that not internal padding would be needed for
    * 16-bit types.
    */
   if (device->robust_buffer_access &&
       (buffer->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ||
        buffer->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
      pMemoryRequirements->size = align_u64(buffer->size, 4);

   pMemoryRequirements->memoryTypeBits = memory_types;
}

void anv_GetBufferMemoryRequirements2(
    VkDevice                                    _device,
    const VkBufferMemoryRequirementsInfo2*      pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   anv_GetBufferMemoryRequirements(_device, pInfo->buffer,
                                   &pMemoryRequirements->memoryRequirements);

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *requirements = (void *)ext;
         requirements->prefersDedicatedAllocation = false;
         requirements->requiresDedicatedAllocation = false;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void anv_GetImageMemoryRequirements(
    VkDevice                                    _device,
    VkImage                                     _image,
    VkMemoryRequirements*                       pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_image, image, _image);
   ANV_FROM_HANDLE(anv_device, device, _device);

   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    *
    * All types are currently supported for images.
    */
   uint32_t memory_types = (1ull << device->physical->memory.type_count) - 1;

   pMemoryRequirements->size = image->size;
   pMemoryRequirements->alignment = image->alignment;
   pMemoryRequirements->memoryTypeBits = memory_types;
}

void anv_GetImageMemoryRequirements2(
    VkDevice                                    _device,
    const VkImageMemoryRequirementsInfo2*       pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pInfo->image);

   anv_GetImageMemoryRequirements(_device, pInfo->image,
                                  &pMemoryRequirements->memoryRequirements);

   vk_foreach_struct_const(ext, pInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO: {
         const VkImagePlaneMemoryRequirementsInfo *plane_reqs =
            (const VkImagePlaneMemoryRequirementsInfo *) ext;
         uint32_t plane = anv_image_aspect_to_plane(image->aspects,
                                                    plane_reqs->planeAspect);

         assert(image->planes[plane].offset == 0);

         /* The Vulkan spec (git aaed022) says:
          *
          *    memoryTypeBits is a bitfield and contains one bit set for every
          *    supported memory type for the resource. The bit `1<<i` is set
          *    if and only if the memory type `i` in the
          *    VkPhysicalDeviceMemoryProperties structure for the physical
          *    device is supported.
          *
          * All types are currently supported for images.
          */
         pMemoryRequirements->memoryRequirements.memoryTypeBits =
               (1ull << device->physical->memory.type_count) - 1;

         pMemoryRequirements->memoryRequirements.size = image->planes[plane].size;
         pMemoryRequirements->memoryRequirements.alignment =
            image->planes[plane].alignment;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *requirements = (void *)ext;
         if (image->needs_set_tiling || image->external_format) {
            /* If we need to set the tiling for external consumers, we need a
             * dedicated allocation.
             *
             * See also anv_AllocateMemory.
             */
            requirements->prefersDedicatedAllocation = true;
            requirements->requiresDedicatedAllocation = true;
         } else {
            requirements->prefersDedicatedAllocation = false;
            requirements->requiresDedicatedAllocation = false;
         }
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void anv_GetImageSparseMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void anv_GetImageSparseMemoryRequirements2(
    VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2*           pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void anv_GetDeviceMemoryCommitment(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize*                               pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

static void
anv_bind_buffer_memory(const VkBindBufferMemoryInfo *pBindInfo)
{
   ANV_FROM_HANDLE(anv_device_memory, mem, pBindInfo->memory);
   ANV_FROM_HANDLE(anv_buffer, buffer, pBindInfo->buffer);

   assert(pBindInfo->sType == VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO);

   if (mem) {
      buffer->address = (struct anv_address) {
         .bo = mem->bo,
         .offset = pBindInfo->memoryOffset,
      };
   } else {
      buffer->address = ANV_NULL_ADDRESS;
   }
}

VkResult anv_BindBufferMemory(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset)
{
   anv_bind_buffer_memory(
      &(VkBindBufferMemoryInfo) {
         .sType         = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
         .buffer        = buffer,
         .memory        = memory,
         .memoryOffset  = memoryOffset,
      });

   return VK_SUCCESS;
}

VkResult anv_BindBufferMemory2(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfo*               pBindInfos)
{
   for (uint32_t i = 0; i < bindInfoCount; i++)
      anv_bind_buffer_memory(&pBindInfos[i]);

   return VK_SUCCESS;
}

VkResult anv_QueueBindSparse(
    VkQueue                                     _queue,
    uint32_t                                    bindInfoCount,
    const VkBindSparseInfo*                     pBindInfo,
    VkFence                                     fence)
{
   ANV_FROM_HANDLE(anv_queue, queue, _queue);
   if (anv_device_is_lost(queue->device))
      return VK_ERROR_DEVICE_LOST;

   return vk_error(VK_ERROR_FEATURE_NOT_PRESENT);
}

// Event functions

VkResult anv_CreateEvent(
    VkDevice                                    _device,
    const VkEventCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkEvent*                                    pEvent)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_event *event;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);

   event = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*event), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (event == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &event->base, VK_OBJECT_TYPE_EVENT);
   event->state = anv_state_pool_alloc(&device->dynamic_state_pool,
                                       sizeof(uint64_t), 8);
   *(uint64_t *)event->state.map = VK_EVENT_RESET;

   *pEvent = anv_event_to_handle(event);

   return VK_SUCCESS;
}

void anv_DestroyEvent(
    VkDevice                                    _device,
    VkEvent                                     _event,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_event, event, _event);

   if (!event)
      return;

   anv_state_pool_free(&device->dynamic_state_pool, event->state);

   vk_object_base_finish(&event->base);
   vk_free2(&device->vk.alloc, pAllocator, event);
}

VkResult anv_GetEventStatus(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_event, event, _event);

   if (anv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   return *(uint64_t *)event->state.map;
}

VkResult anv_SetEvent(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
   ANV_FROM_HANDLE(anv_event, event, _event);

   *(uint64_t *)event->state.map = VK_EVENT_SET;

   return VK_SUCCESS;
}

VkResult anv_ResetEvent(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
   ANV_FROM_HANDLE(anv_event, event, _event);

   *(uint64_t *)event->state.map = VK_EVENT_RESET;

   return VK_SUCCESS;
}

// Buffer functions

VkResult anv_CreateBuffer(
    VkDevice                                    _device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_buffer *buffer;

   /* Don't allow creating buffers bigger than our address space.  The real
    * issue here is that we may align up the buffer size and we don't want
    * doing so to cause roll-over.  However, no one has any business
    * allocating a buffer larger than our GTT size.
    */
   if (pCreateInfo->size > device->physical->gtt_size)
      return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   buffer = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*buffer), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &buffer->base, VK_OBJECT_TYPE_BUFFER);
   buffer->size = pCreateInfo->size;
   buffer->usage = pCreateInfo->usage;
   buffer->address = ANV_NULL_ADDRESS;

   *pBuffer = anv_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

void anv_DestroyBuffer(
    VkDevice                                    _device,
    VkBuffer                                    _buffer,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   if (!buffer)
      return;

   vk_object_base_finish(&buffer->base);
   vk_free2(&device->vk.alloc, pAllocator, buffer);
}

VkDeviceAddress anv_GetBufferDeviceAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfoKHR*         pInfo)
{
   ANV_FROM_HANDLE(anv_buffer, buffer, pInfo->buffer);

   assert(!anv_address_is_null(buffer->address));
   assert(buffer->address.bo->flags & EXEC_OBJECT_PINNED);

   return anv_address_physical(buffer->address);
}

uint64_t anv_GetBufferOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfoKHR*         pInfo)
{
   return 0;
}

uint64_t anv_GetDeviceMemoryOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfoKHR* pInfo)
{
   ANV_FROM_HANDLE(anv_device_memory, memory, pInfo->memory);

   assert(memory->bo->flags & EXEC_OBJECT_PINNED);
   assert(memory->bo->has_client_visible_address);

   return gen_48b_address(memory->bo->offset);
}

void
anv_fill_buffer_surface_state(struct anv_device *device, struct anv_state state,
                              enum isl_format format,
                              isl_surf_usage_flags_t usage,
                              struct anv_address address,
                              uint32_t range, uint32_t stride)
{
   isl_buffer_fill_state(&device->isl_dev, state.map,
                         .address = anv_address_physical(address),
                         .mocs = isl_mocs(&device->isl_dev, usage),
                         .size_B = range,
                         .format = format,
                         .swizzle = ISL_SWIZZLE_IDENTITY,
                         .stride_B = stride);
}

void anv_DestroySampler(
    VkDevice                                    _device,
    VkSampler                                   _sampler,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_sampler, sampler, _sampler);

   if (!sampler)
      return;

   if (sampler->bindless_state.map) {
      anv_state_pool_free(&device->dynamic_state_pool,
                          sampler->bindless_state);
   }

   if (sampler->custom_border_color.map) {
      anv_state_reserved_pool_free(&device->custom_border_colors,
                                   sampler->custom_border_color);
   }

   vk_object_base_finish(&sampler->base);
   vk_free2(&device->vk.alloc, pAllocator, sampler);
}

VkResult anv_CreateFramebuffer(
    VkDevice                                    _device,
    const VkFramebufferCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFramebuffer*                              pFramebuffer)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_framebuffer *framebuffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   size_t size = sizeof(*framebuffer);

   /* VK_KHR_imageless_framebuffer extension says:
    *
    *    If flags includes VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT_KHR,
    *    parameter pAttachments is ignored.
    */
   if (!(pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT_KHR)) {
      size += sizeof(struct anv_image_view *) * pCreateInfo->attachmentCount;
      framebuffer = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (framebuffer == NULL)
         return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

      for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
         ANV_FROM_HANDLE(anv_image_view, iview, pCreateInfo->pAttachments[i]);
         framebuffer->attachments[i] = iview;
      }
      framebuffer->attachment_count = pCreateInfo->attachmentCount;
   } else {
      framebuffer = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (framebuffer == NULL)
         return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

      framebuffer->attachment_count = 0;
   }

   vk_object_base_init(&device->vk, &framebuffer->base,
                       VK_OBJECT_TYPE_FRAMEBUFFER);

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;

   *pFramebuffer = anv_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;
}

void anv_DestroyFramebuffer(
    VkDevice                                    _device,
    VkFramebuffer                               _fb,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_framebuffer, fb, _fb);

   if (!fb)
      return;

   vk_object_base_finish(&fb->base);
   vk_free2(&device->vk.alloc, pAllocator, fb);
}

static const VkTimeDomainEXT anv_time_domains[] = {
   VK_TIME_DOMAIN_DEVICE_EXT,
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT,
#ifdef CLOCK_MONOTONIC_RAW
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT,
#endif
};

VkResult anv_GetPhysicalDeviceCalibrateableTimeDomainsEXT(
   VkPhysicalDevice                             physicalDevice,
   uint32_t                                     *pTimeDomainCount,
   VkTimeDomainEXT                              *pTimeDomains)
{
   int d;
   VK_OUTARRAY_MAKE(out, pTimeDomains, pTimeDomainCount);

   for (d = 0; d < ARRAY_SIZE(anv_time_domains); d++) {
      vk_outarray_append(&out, i) {
         *i = anv_time_domains[d];
      }
   }

   return vk_outarray_status(&out);
}

static uint64_t
anv_clock_gettime(clockid_t clock_id)
{
   struct timespec current;
   int ret;

   ret = clock_gettime(clock_id, &current);
#ifdef CLOCK_MONOTONIC_RAW
   if (ret < 0 && clock_id == CLOCK_MONOTONIC_RAW)
      ret = clock_gettime(CLOCK_MONOTONIC, &current);
#endif
   if (ret < 0)
      return 0;

   return (uint64_t) current.tv_sec * 1000000000ULL + current.tv_nsec;
}

VkResult anv_GetCalibratedTimestampsEXT(
   VkDevice                                     _device,
   uint32_t                                     timestampCount,
   const VkCalibratedTimestampInfoEXT           *pTimestampInfos,
   uint64_t                                     *pTimestamps,
   uint64_t                                     *pMaxDeviation)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   uint64_t timestamp_frequency = device->info.timestamp_frequency;
   int  ret;
   int d;
   uint64_t begin, end;
   uint64_t max_clock_period = 0;

#ifdef CLOCK_MONOTONIC_RAW
   begin = anv_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   begin = anv_clock_gettime(CLOCK_MONOTONIC);
#endif

   for (d = 0; d < timestampCount; d++) {
      switch (pTimestampInfos[d].timeDomain) {
      case VK_TIME_DOMAIN_DEVICE_EXT:
         ret = anv_gem_reg_read(device->fd, TIMESTAMP | I915_REG_READ_8B_WA,
                                &pTimestamps[d]);

         if (ret != 0) {
            return anv_device_set_lost(device, "Failed to read the TIMESTAMP "
                                               "register: %m");
         }
         uint64_t device_period = DIV_ROUND_UP(1000000000, timestamp_frequency);
         max_clock_period = MAX2(max_clock_period, device_period);
         break;
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT:
         pTimestamps[d] = anv_clock_gettime(CLOCK_MONOTONIC);
         max_clock_period = MAX2(max_clock_period, 1);
         break;

#ifdef CLOCK_MONOTONIC_RAW
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT:
         pTimestamps[d] = begin;
         break;
#endif
      default:
         pTimestamps[d] = 0;
         break;
      }
   }

#ifdef CLOCK_MONOTONIC_RAW
   end = anv_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   end = anv_clock_gettime(CLOCK_MONOTONIC);
#endif

    /*
     * The maximum deviation is the sum of the interval over which we
     * perform the sampling and the maximum period of any sampled
     * clock. That's because the maximum skew between any two sampled
     * clock edges is when the sampled clock with the largest period is
     * sampled at the end of that period but right at the beginning of the
     * sampling interval and some other clock is sampled right at the
     * begining of its sampling period and right at the end of the
     * sampling interval. Let's assume the GPU has the longest clock
     * period and that the application is sampling GPU and monotonic:
     *
     *                               s                 e
     *			 w x y z 0 1 2 3 4 5 6 7 8 9 a b c d e f
     *	Raw              -_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-
     *
     *                               g
     *		  0         1         2         3
     *	GPU       -----_____-----_____-----_____-----_____
     *
     *                                                m
     *					    x y z 0 1 2 3 4 5 6 7 8 9 a b c
     *	Monotonic                           -_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-
     *
     *	Interval                     <----------------->
     *	Deviation           <-------------------------->
     *
     *		s  = read(raw)       2
     *		g  = read(GPU)       1
     *		m  = read(monotonic) 2
     *		e  = read(raw)       b
     *
     * We round the sample interval up by one tick to cover sampling error
     * in the interval clock
     */

   uint64_t sample_interval = end - begin + 1;

   *pMaxDeviation = sample_interval + max_clock_period;

   return VK_SUCCESS;
}

/* vk_icd.h does not declare this function, so we declare it here to
 * suppress Wmissing-prototypes.
 */
PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion)
{
   /* For the full details on loader interface versioning, see
    * <https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/loader/LoaderAndLayerInterface.md>.
    * What follows is a condensed summary, to help you navigate the large and
    * confusing official doc.
    *
    *   - Loader interface v0 is incompatible with later versions. We don't
    *     support it.
    *
    *   - In loader interface v1:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdGetInstanceProcAddr(). The ICD must statically expose this
    *         entrypoint.
    *       - The ICD must statically expose no other Vulkan symbol unless it is
    *         linked with -Bsymbolic.
    *       - Each dispatchable Vulkan handle created by the ICD must be
    *         a pointer to a struct whose first member is VK_LOADER_DATA. The
    *         ICD must initialize VK_LOADER_DATA.loadMagic to ICD_LOADER_MAGIC.
    *       - The loader implements vkCreate{PLATFORM}SurfaceKHR() and
    *         vkDestroySurfaceKHR(). The ICD must be capable of working with
    *         such loader-managed surfaces.
    *
    *    - Loader interface v2 differs from v1 in:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdNegotiateLoaderICDInterfaceVersion(). The ICD must
    *         statically expose this entrypoint.
    *
    *    - Loader interface v3 differs from v2 in:
    *        - The ICD must implement vkCreate{PLATFORM}SurfaceKHR(),
    *          vkDestroySurfaceKHR(), and other API which uses VKSurfaceKHR,
    *          because the loader no longer does so.
    *
    *    - Loader interface v4 differs from v3 in:
    *        - The ICD must implement vk_icdGetPhysicalDeviceProcAddr().
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 4u);
   return VK_SUCCESS;
}

VkResult anv_CreatePrivateDataSlotEXT(
    VkDevice                                    _device,
    const VkPrivateDataSlotCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPrivateDataSlotEXT*                       pPrivateDataSlot)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   return vk_private_data_slot_create(&device->vk, pCreateInfo, pAllocator,
                                      pPrivateDataSlot);
}

void anv_DestroyPrivateDataSlotEXT(
    VkDevice                                    _device,
    VkPrivateDataSlotEXT                        privateDataSlot,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   vk_private_data_slot_destroy(&device->vk, privateDataSlot, pAllocator);
}

VkResult anv_SetPrivateDataEXT(
    VkDevice                                    _device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlotEXT                        privateDataSlot,
    uint64_t                                    data)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   return vk_object_base_set_private_data(&device->vk,
                                          objectType, objectHandle,
                                          privateDataSlot, data);
}

void anv_GetPrivateDataEXT(
    VkDevice                                    _device,
    VkObjectType                                objectType,
    uint64_t                                    objectHandle,
    VkPrivateDataSlotEXT                        privateDataSlot,
    uint64_t*                                   pData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   vk_object_base_get_private_data(&device->vk,
                                   objectType, objectHandle,
                                   privateDataSlot, pData);
}
