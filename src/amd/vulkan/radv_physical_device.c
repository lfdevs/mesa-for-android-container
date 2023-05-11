/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
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

#include <fcntl.h>

#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

#include "util/disk_cache.h"
#include "util/u_debug.h"
#include "radv_debug.h"
#include "radv_private.h"

#ifdef _WIN32
typedef void *drmDevicePtr;
#include <io.h>
#else
#include <amdgpu.h>
#include <xf86drm.h>
#include "drm-uapi/amdgpu_drm.h"
#include "winsys/amdgpu/radv_amdgpu_winsys_public.h"
#endif
#include "git_sha1.h"
#include "winsys/null/radv_null_winsys_public.h"

#ifdef LLVM_AVAILABLE
#include "ac_llvm_util.h"
#endif

bool
radv_thread_trace_enabled(void)
{
   return radv_get_int_debug_option("RADV_THREAD_TRACE", -1) >= 0 ||
          getenv("RADV_THREAD_TRACE_TRIGGER");
}

static bool
radv_perf_query_supported(const struct radv_physical_device *pdev)
{
   /* SQTT / SPM interfere with the register states for perf counters, and
    * the code has only been tested on GFX10.3 */
   return pdev->rad_info.gfx_level == GFX10_3 && !radv_thread_trace_enabled();
}

static bool
radv_taskmesh_enabled(const struct radv_physical_device *pdevice)
{
   return pdevice->use_ngg && !pdevice->use_llvm && pdevice->rad_info.gfx_level >= GFX10_3 &&
          !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE) &&
          pdevice->rad_info.has_gang_submit;
}

static bool
radv_vrs_attachment_enabled(const struct radv_physical_device *pdevice)
{
   return pdevice->rad_info.gfx_level >= GFX11 ||
          !(pdevice->instance->debug_flags & RADV_DEBUG_NO_HIZ);
}

static bool
radv_NV_device_generated_commands_enabled(const struct radv_physical_device *device)
{
   return device->rad_info.gfx_level >= GFX7 &&
          !(device->instance->debug_flags & RADV_DEBUG_NO_IBS) &&
          driQueryOptionb(&device->instance->dri_options, "radv_dgc");
}

static bool
radv_is_conformant(const struct radv_physical_device *pdevice)
{
   return pdevice->rad_info.gfx_level >= GFX8;
}

static void
parse_hex(char *out, const char *in, unsigned length)
{
   for (unsigned i = 0; i < length; ++i)
      out[i] = 0;

   for (unsigned i = 0; i < 2 * length; ++i) {
      unsigned v =
         in[i] <= '9' ? in[i] - '0' : (in[i] >= 'a' ? (in[i] - 'a' + 10) : (in[i] - 'A' + 10));
      out[i / 2] |= v << (4 * (1 - i % 2));
   }
}

static int
radv_device_get_cache_uuid(struct radv_physical_device *pdevice, void *uuid)
{
   enum radeon_family family = pdevice->rad_info.family;
   struct mesa_sha1 ctx;
   unsigned char sha1[20];
   unsigned ptr_size = sizeof(void *);

   memset(uuid, 0, VK_UUID_SIZE);
   _mesa_sha1_init(&ctx);

#ifdef RADV_BUILD_ID_OVERRIDE
   {
      unsigned size = strlen(RADV_BUILD_ID_OVERRIDE) / 2;
      char *data = alloca(size);
      parse_hex(data, RADV_BUILD_ID_OVERRIDE, size);
      _mesa_sha1_update(&ctx, data, size);
   }
#else
   if (!disk_cache_get_function_identifier(radv_device_get_cache_uuid, &ctx))
      return -1;
#endif

#ifdef LLVM_AVAILABLE
   if (pdevice->use_llvm &&
       !disk_cache_get_function_identifier(LLVMInitializeAMDGPUTargetInfo, &ctx))
      return -1;
#endif

   _mesa_sha1_update(&ctx, &family, sizeof(family));
   _mesa_sha1_update(&ctx, &ptr_size, sizeof(ptr_size));
   _mesa_sha1_final(&ctx, sha1);

   memcpy(uuid, sha1, VK_UUID_SIZE);
   return 0;
}

static void
radv_get_driver_uuid(void *uuid)
{
   ac_compute_driver_uuid(uuid, VK_UUID_SIZE);
}

static void
radv_get_device_uuid(struct radeon_info *info, void *uuid)
{
   ac_compute_device_uuid(info, uuid, VK_UUID_SIZE);
}

static void
radv_physical_device_init_queue_table(struct radv_physical_device *pdevice)
{
   int idx = 0;
   pdevice->vk_queue_to_radv[idx] = RADV_QUEUE_GENERAL;
   idx++;

   for (unsigned i = 1; i < RADV_MAX_QUEUE_FAMILIES; i++)
      pdevice->vk_queue_to_radv[i] = RADV_MAX_QUEUE_FAMILIES + 1;

   if (pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues > 0 &&
       !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
      pdevice->vk_queue_to_radv[idx] = RADV_QUEUE_COMPUTE;
      idx++;
   }

   if (pdevice->instance->perftest_flags & RADV_PERFTEST_VIDEO_DECODE) {
      if (pdevice->rad_info.ip[AMD_IP_VCN_DEC].num_queues > 0) {
         pdevice->vk_queue_to_radv[idx] = RADV_QUEUE_VIDEO_DEC;
         idx++;
      }

      if (radv_has_uvd(pdevice)) {
         pdevice->vk_queue_to_radv[idx] = RADV_QUEUE_VIDEO_DEC;
         idx++;
      }
   }
   pdevice->num_queues = idx;
}

enum radv_heap {
   RADV_HEAP_VRAM = 1 << 0,
   RADV_HEAP_GTT = 1 << 1,
   RADV_HEAP_VRAM_VIS = 1 << 2,
   RADV_HEAP_MAX = 1 << 3,
};

static uint64_t
radv_get_adjusted_vram_size(struct radv_physical_device *device)
{
   int ov = driQueryOptioni(&device->instance->dri_options, "override_vram_size");
   if (ov >= 0)
      return MIN2((uint64_t)device->rad_info.vram_size_kb * 1024, (uint64_t)ov << 20);
   return (uint64_t)device->rad_info.vram_size_kb * 1024;
}

static uint64_t
radv_get_visible_vram_size(struct radv_physical_device *device)
{
   return MIN2(radv_get_adjusted_vram_size(device),
               (uint64_t)device->rad_info.vram_vis_size_kb * 1024);
}

static uint64_t
radv_get_vram_size(struct radv_physical_device *device)
{
   uint64_t total_size = radv_get_adjusted_vram_size(device);
   return total_size - MIN2(total_size, (uint64_t)device->rad_info.vram_vis_size_kb * 1024);
}

static void
radv_physical_device_init_mem_types(struct radv_physical_device *device)
{
   uint64_t visible_vram_size = radv_get_visible_vram_size(device);
   uint64_t vram_size = radv_get_vram_size(device);
   uint64_t gtt_size = (uint64_t)device->rad_info.gart_size_kb * 1024;
   int vram_index = -1, visible_vram_index = -1, gart_index = -1;

   device->memory_properties.memoryHeapCount = 0;
   device->heaps = 0;

   if (!device->rad_info.has_dedicated_vram) {
      const uint64_t total_size = gtt_size + visible_vram_size;

      if (device->instance->enable_unified_heap_on_apu) {
         /* Some applications seem better when the driver exposes only one heap of VRAM on APUs. */
         visible_vram_size = total_size;
         gtt_size = 0;
      } else {
         /* On APUs, the carveout is usually too small for games that request a minimum VRAM size
          * greater than it. To workaround this, we compute the total available memory size (GTT +
          * visible VRAM size) and report 2/3 as VRAM and 1/3 as GTT.
          */
         visible_vram_size = align64((total_size * 2) / 3, device->rad_info.gart_page_size);
         gtt_size = total_size - visible_vram_size;
      }

      vram_size = 0;
   }

   /* Only get a VRAM heap if it is significant, not if it is a 16 MiB
    * remainder above visible VRAM. */
   if (vram_size > 0 && vram_size * 9 >= visible_vram_size) {
      vram_index = device->memory_properties.memoryHeapCount++;
      device->heaps |= RADV_HEAP_VRAM;
      device->memory_properties.memoryHeaps[vram_index] = (VkMemoryHeap){
         .size = vram_size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      };
   }

   if (gtt_size > 0) {
      gart_index = device->memory_properties.memoryHeapCount++;
      device->heaps |= RADV_HEAP_GTT;
      device->memory_properties.memoryHeaps[gart_index] = (VkMemoryHeap){
         .size = gtt_size,
         .flags = 0,
      };
   }

   if (visible_vram_size) {
      visible_vram_index = device->memory_properties.memoryHeapCount++;
      device->heaps |= RADV_HEAP_VRAM_VIS;
      device->memory_properties.memoryHeaps[visible_vram_index] = (VkMemoryHeap){
         .size = visible_vram_size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      };
   }

   unsigned type_count = 0;

   if (vram_index >= 0 || visible_vram_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_VRAM;
      device->memory_flags[type_count] = RADEON_FLAG_NO_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
         .heapIndex = vram_index >= 0 ? vram_index : visible_vram_index,
      };

      device->memory_domains[type_count] = RADEON_DOMAIN_VRAM;
      device->memory_flags[type_count] = RADEON_FLAG_NO_CPU_ACCESS | RADEON_FLAG_32BIT;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
         .heapIndex = vram_index >= 0 ? vram_index : visible_vram_index,
      };
   }

   if (gart_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_GTT;
      device->memory_flags[type_count] = RADEON_FLAG_GTT_WC | RADEON_FLAG_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         .heapIndex = gart_index,
      };
   }
   if (visible_vram_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_VRAM;
      device->memory_flags[type_count] = RADEON_FLAG_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         .heapIndex = visible_vram_index,
      };

      device->memory_domains[type_count] = RADEON_DOMAIN_VRAM;
      device->memory_flags[type_count] = RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_32BIT;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         .heapIndex = visible_vram_index,
      };
   }

   if (gart_index >= 0) {
      device->memory_domains[type_count] = RADEON_DOMAIN_GTT;
      device->memory_flags[type_count] = RADEON_FLAG_CPU_ACCESS;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
         .heapIndex = gart_index,
      };

      device->memory_domains[type_count] = RADEON_DOMAIN_GTT;
      device->memory_flags[type_count] = RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_32BIT;
      device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
         .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
         .heapIndex = gart_index,
      };
   }
   device->memory_properties.memoryTypeCount = type_count;

   if (device->rad_info.has_l2_uncached) {
      for (int i = 0; i < device->memory_properties.memoryTypeCount; i++) {
         VkMemoryType mem_type = device->memory_properties.memoryTypes[i];

         if (((mem_type.propertyFlags &
               (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) ||
              mem_type.propertyFlags == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
             !(device->memory_flags[i] & RADEON_FLAG_32BIT)) {

            VkMemoryPropertyFlags property_flags = mem_type.propertyFlags |
                                                   VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD |
                                                   VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;

            device->memory_domains[type_count] = device->memory_domains[i];
            device->memory_flags[type_count] = device->memory_flags[i] | RADEON_FLAG_VA_UNCACHED;
            device->memory_properties.memoryTypes[type_count++] = (VkMemoryType){
               .propertyFlags = property_flags,
               .heapIndex = mem_type.heapIndex,
            };
         }
      }
      device->memory_properties.memoryTypeCount = type_count;
   }

   for (unsigned i = 0; i < type_count; ++i) {
      if (device->memory_flags[i] & RADEON_FLAG_32BIT)
         device->memory_types_32bit |= BITFIELD_BIT(i);
   }
}

uint32_t
radv_find_memory_index(struct radv_physical_device *pdevice, VkMemoryPropertyFlags flags)
{
   VkPhysicalDeviceMemoryProperties *mem_properties = &pdevice->memory_properties;
   for (uint32_t i = 0; i < mem_properties->memoryTypeCount; ++i) {
      if (mem_properties->memoryTypes[i].propertyFlags == flags) {
         return i;
      }
   }
   unreachable("invalid memory properties");
}

static void
radv_get_binning_settings(const struct radv_physical_device *pdevice,
                          struct radv_binning_settings *settings)
{
   settings->context_states_per_bin = 1;
   settings->persistent_states_per_bin = 1;
   settings->fpovs_per_batch = 63;
}

static void
radv_physical_device_get_supported_extensions(const struct radv_physical_device *device,
                                              struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table){
      .KHR_8bit_storage = true,
      .KHR_16bit_storage = true,
      .KHR_acceleration_structure = radv_enable_rt(device, false),
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_copy_commands2 = true,
      .KHR_create_renderpass2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_deferred_host_operations = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_descriptor_update_template = true,
      .KHR_device_group = true,
      .KHR_draw_indirect_count = true,
      .KHR_driver_properties = true,
      .KHR_dynamic_rendering = true,
      .KHR_external_fence = true,
      .KHR_external_fence_fd = true,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = true,
      .KHR_external_semaphore_fd = true,
      .KHR_format_feature_flags2 = true,
      .KHR_fragment_shading_rate = device->rad_info.gfx_level >= GFX10_3,
      .KHR_get_memory_requirements2 = true,
      .KHR_global_priority = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
#ifdef RADV_USE_WSI_PLATFORM
      .KHR_incremental_present = true,
#endif
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_maintenance4 = true,
      .KHR_map_memory2 = true,
      .KHR_multiview = true,
      .KHR_performance_query = radv_perf_query_supported(device),
      .KHR_pipeline_executable_properties = true,
      .KHR_pipeline_library = !device->use_llvm,
      /* Hide these behind dri configs for now since we cannot implement it reliably on
       * all surfaces yet. There is no surface capability query for present wait/id,
       * but the feature is useful enough to hide behind an opt-in mechanism for now.
       * If the instance only enables surface extensions that unconditionally support present wait,
       * we can also expose the extension that way. */
      .KHR_present_id = driQueryOptionb(&device->instance->dri_options, "vk_khr_present_wait") ||
                        wsi_common_vk_instance_supports_present_wait(&device->instance->vk),
      .KHR_present_wait = driQueryOptionb(&device->instance->dri_options, "vk_khr_present_wait") ||
                          wsi_common_vk_instance_supports_present_wait(&device->instance->vk),
      .KHR_push_descriptor = true,
      .KHR_ray_query = radv_enable_rt(device, false),
      .KHR_ray_tracing_maintenance1 = radv_enable_rt(device, false),
      .KHR_ray_tracing_pipeline = radv_enable_rt(device, true),
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_atomic_int64 = true,
      .KHR_shader_clock = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_float16_int8 = true,
      .KHR_shader_float_controls = true,
      .KHR_shader_integer_dot_product = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_subgroup_extended_types = true,
      .KHR_shader_subgroup_uniform_control_flow = true,
      .KHR_shader_terminate_invocation = true,
      .KHR_spirv_1_4 = true,
      .KHR_storage_buffer_storage_class = true,
#ifdef RADV_USE_WSI_PLATFORM
      .KHR_swapchain = true,
      .KHR_swapchain_mutable_format = true,
#endif
      .KHR_synchronization2 = true,
      .KHR_timeline_semaphore = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .KHR_video_queue = !!(device->instance->perftest_flags & RADV_PERFTEST_VIDEO_DECODE),
      .KHR_video_decode_queue = !!(device->instance->perftest_flags & RADV_PERFTEST_VIDEO_DECODE),
      .KHR_video_decode_h264 =
         VIDEO_CODEC_H264DEC && !!(device->instance->perftest_flags & RADV_PERFTEST_VIDEO_DECODE),
      .KHR_video_decode_h265 =
         VIDEO_CODEC_H265DEC && !!(device->instance->perftest_flags & RADV_PERFTEST_VIDEO_DECODE),
      .KHR_vulkan_memory_model = true,
      .KHR_workgroup_memory_explicit_layout = true,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats = true,
      .EXT_attachment_feedback_loop_layout = true,
      .EXT_border_color_swizzle = device->rad_info.gfx_level >= GFX10,
      .EXT_buffer_device_address = true,
      .EXT_calibrated_timestamps = RADV_SUPPORT_CALIBRATED_TIMESTAMPS,
      .EXT_color_write_enable = true,
      .EXT_conditional_rendering = true,
      .EXT_conservative_rasterization = device->rad_info.gfx_level >= GFX9,
      .EXT_custom_border_color = true,
      .EXT_debug_marker = radv_thread_trace_enabled(),
      .EXT_depth_clip_control = true,
      .EXT_depth_clip_enable = true,
      .EXT_depth_range_unrestricted = true,
      .EXT_descriptor_buffer = true,
      .EXT_descriptor_indexing = true,
      .EXT_discard_rectangles = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
      .EXT_display_control = true,
#endif
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_extended_dynamic_state3 = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_external_memory_host = device->rad_info.has_userptr,
      .EXT_global_priority = true,
      .EXT_global_priority_query = true,
      .EXT_graphics_pipeline_library =
         !device->use_llvm && !(device->instance->debug_flags & RADV_DEBUG_NO_GPL),
      .EXT_host_query_reset = true,
      .EXT_image_2d_view_of_3d = true,
      .EXT_image_drm_format_modifier = device->rad_info.gfx_level >= GFX9,
      .EXT_image_robustness = true,
      .EXT_image_sliced_view_of_3d = device->rad_info.gfx_level >= GFX10,
      .EXT_image_view_min_lod = true,
      .EXT_index_type_uint8 = device->rad_info.gfx_level >= GFX8,
      .EXT_inline_uniform_block = true,
      .EXT_line_rasterization = true,
      .EXT_load_store_op_none = true,
      .EXT_memory_budget = true,
      .EXT_memory_priority = true,
      .EXT_mesh_shader = radv_taskmesh_enabled(device),
      .EXT_multi_draw = true,
      .EXT_mutable_descriptor_type = true, /* Trivial promotion from VALVE. */
      .EXT_non_seamless_cube_map = true,
      .EXT_pci_bus_info = true,
#ifndef _WIN32
      .EXT_physical_device_drm = true,
#endif
      .EXT_pipeline_creation_cache_control = true,
      .EXT_pipeline_creation_feedback = true,
      .EXT_pipeline_library_group_handles = radv_enable_rt(device, true),
      .EXT_post_depth_coverage = device->rad_info.gfx_level >= GFX10,
      .EXT_primitive_topology_list_restart = true,
      .EXT_primitives_generated_query = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_robustness2 = true,
      .EXT_sample_locations = device->rad_info.gfx_level < GFX10,
      .EXT_sampler_filter_minmax = true,
      .EXT_scalar_block_layout = device->rad_info.gfx_level >= GFX7,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_atomic_float = true,
#ifdef LLVM_AVAILABLE
      .EXT_shader_atomic_float2 = !device->use_llvm || LLVM_VERSION_MAJOR >= 14,
#else
      .EXT_shader_atomic_float2 = true,
#endif
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_image_atomic_int64 = true,
      .EXT_shader_module_identifier = true,
      .EXT_shader_stencil_export = true,
      .EXT_shader_subgroup_ballot = true,
      .EXT_shader_subgroup_vote = true,
      .EXT_shader_viewport_index_layer = true,
      .EXT_subgroup_size_control = true,
#ifdef RADV_USE_WSI_PLATFORM
      .EXT_swapchain_maintenance1 = true,
#endif
      .EXT_texel_buffer_alignment = true,
      .EXT_transform_feedback = true,
      .EXT_vertex_attribute_divisor = true,
      .EXT_vertex_input_dynamic_state =
         !device->use_llvm && !radv_NV_device_generated_commands_enabled(device),
      .EXT_ycbcr_image_arrays = true,
      .AMD_buffer_marker = true,
      .AMD_device_coherent_memory = true,
      .AMD_draw_indirect_count = true,
      .AMD_gcn_shader = true,
      .AMD_gpu_shader_half_float = device->rad_info.has_packed_math_16bit,
      .AMD_gpu_shader_int16 = device->rad_info.has_packed_math_16bit,
      .AMD_memory_overallocation_behavior = true,
      .AMD_mixed_attachment_samples = true,
      .AMD_rasterization_order = device->rad_info.has_out_of_order_rast,
      .AMD_shader_ballot = true,
      .AMD_shader_core_properties = true,
      .AMD_shader_core_properties2 = true,
      .AMD_shader_early_and_late_fragment_tests = true,
      .AMD_shader_explicit_vertex_parameter = true,
      .AMD_shader_fragment_mask = device->use_fmask,
      .AMD_shader_image_load_store_lod = true,
      .AMD_shader_trinary_minmax = true,
      .AMD_texture_gather_bias_lod = device->rad_info.gfx_level < GFX11,
#ifdef ANDROID
      .ANDROID_external_memory_android_hardware_buffer = RADV_SUPPORT_ANDROID_HARDWARE_BUFFER,
      .ANDROID_native_buffer = true,
#endif
      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,
      .INTEL_shader_integer_functions2 = true,
      .NV_compute_shader_derivatives = true,
      .NV_device_generated_commands = radv_NV_device_generated_commands_enabled(device),
      /* Undocumented extension purely for vkd3d-proton. This check is to prevent anyone else from
       * using it.
       */
      .VALVE_descriptor_set_host_mapping =
         device->vk.instance->app_info.engine_name &&
         strcmp(device->vk.instance->app_info.engine_name, "vkd3d") == 0,
      .VALVE_mutable_descriptor_type = true,
   };
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   memset(pFeatures, 0, sizeof(*pFeatures));

   *pFeatures = (VkPhysicalDeviceFeatures){
      .robustBufferAccess = true,
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = true,
      .tessellationShader = true,
      .sampleRateShading = true,
      .dualSrcBlend = true,
      .logicOp = true,
      .multiDrawIndirect = true,
      .drawIndirectFirstInstance = true,
      .depthClamp = true,
      .depthBiasClamp = true,
      .fillModeNonSolid = true,
      .depthBounds = true,
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = false,
      .multiViewport = true,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = radv_device_supports_etc(pdevice) || pdevice->emulate_etc2,
      .textureCompressionASTC_LDR = false,
      .textureCompressionBC = true,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = true,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = true,
      .shaderImageGatherExtended = true,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageMultisample = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      .shaderFloat64 = true,
      .shaderInt64 = true,
      .shaderInt16 = true,
      .sparseBinding = true,
      .sparseResidencyBuffer = pdevice->rad_info.family >= CHIP_POLARIS10,
      .sparseResidencyImage2D = pdevice->rad_info.family >= CHIP_POLARIS10,
      .sparseResidencyImage3D = pdevice->rad_info.gfx_level >= GFX9,
      .sparseResidencyAliased = pdevice->rad_info.family >= CHIP_POLARIS10,
      .variableMultisampleRate = true,
      .shaderResourceMinLod = true,
      .shaderResourceResidency = true,
      .inheritedQueries = true,
   };
}

static void
radv_get_physical_device_features_1_1(struct radv_physical_device *pdevice,
                                      VkPhysicalDeviceVulkan11Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);

   f->storageBuffer16BitAccess = true;
   f->uniformAndStorageBuffer16BitAccess = true;
   f->storagePushConstant16 = true;
   f->storageInputOutput16 = pdevice->rad_info.has_packed_math_16bit;
   f->multiview = true;
   f->multiviewGeometryShader = true;
   f->multiviewTessellationShader = true;
   f->variablePointersStorageBuffer = true;
   f->variablePointers = true;
   f->protectedMemory = false;
   f->samplerYcbcrConversion = true;
   f->shaderDrawParameters = true;
}

static void
radv_get_physical_device_features_1_2(struct radv_physical_device *pdevice,
                                      VkPhysicalDeviceVulkan12Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);

   f->samplerMirrorClampToEdge = true;
   f->drawIndirectCount = true;
   f->storageBuffer8BitAccess = true;
   f->uniformAndStorageBuffer8BitAccess = true;
   f->storagePushConstant8 = true;
   f->shaderBufferInt64Atomics = true;
   f->shaderSharedInt64Atomics = true;
   f->shaderFloat16 = pdevice->rad_info.has_packed_math_16bit;
   f->shaderInt8 = true;

   f->descriptorIndexing = true;
   f->shaderInputAttachmentArrayDynamicIndexing = true;
   f->shaderUniformTexelBufferArrayDynamicIndexing = true;
   f->shaderStorageTexelBufferArrayDynamicIndexing = true;
   f->shaderUniformBufferArrayNonUniformIndexing = true;
   f->shaderSampledImageArrayNonUniformIndexing = true;
   f->shaderStorageBufferArrayNonUniformIndexing = true;
   f->shaderStorageImageArrayNonUniformIndexing = true;
   f->shaderInputAttachmentArrayNonUniformIndexing = true;
   f->shaderUniformTexelBufferArrayNonUniformIndexing = true;
   f->shaderStorageTexelBufferArrayNonUniformIndexing = true;
   f->descriptorBindingUniformBufferUpdateAfterBind = true;
   f->descriptorBindingSampledImageUpdateAfterBind = true;
   f->descriptorBindingStorageImageUpdateAfterBind = true;
   f->descriptorBindingStorageBufferUpdateAfterBind = true;
   f->descriptorBindingUniformTexelBufferUpdateAfterBind = true;
   f->descriptorBindingStorageTexelBufferUpdateAfterBind = true;
   f->descriptorBindingUpdateUnusedWhilePending = true;
   f->descriptorBindingPartiallyBound = true;
   f->descriptorBindingVariableDescriptorCount = true;
   f->runtimeDescriptorArray = true;

   f->samplerFilterMinmax = true;
   f->scalarBlockLayout = pdevice->rad_info.gfx_level >= GFX7;
   f->imagelessFramebuffer = true;
   f->uniformBufferStandardLayout = true;
   f->shaderSubgroupExtendedTypes = true;
   f->separateDepthStencilLayouts = true;
   f->hostQueryReset = true;
   f->timelineSemaphore = true, f->bufferDeviceAddress = true;
   f->bufferDeviceAddressCaptureReplay = true;
   f->bufferDeviceAddressMultiDevice = false;
   f->vulkanMemoryModel = true;
   f->vulkanMemoryModelDeviceScope = true;
   f->vulkanMemoryModelAvailabilityVisibilityChains = false;
   f->shaderOutputViewportIndex = true;
   f->shaderOutputLayer = true;
   f->subgroupBroadcastDynamicId = true;
}

static void
radv_get_physical_device_features_1_3(struct radv_physical_device *pdevice,
                                      VkPhysicalDeviceVulkan13Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);

   f->robustImageAccess = true;
   f->inlineUniformBlock = true;
   f->descriptorBindingInlineUniformBlockUpdateAfterBind = true;
   f->pipelineCreationCacheControl = true;
   f->privateData = true;
   f->shaderDemoteToHelperInvocation = true;
   f->shaderTerminateInvocation = true;
   f->subgroupSizeControl = true;
   f->computeFullSubgroups = true;
   f->synchronization2 = true;
   f->textureCompressionASTC_HDR = false;
   f->shaderZeroInitializeWorkgroupMemory = true;
   f->dynamicRendering = true;
   f->shaderIntegerDotProduct = true;
   f->maintenance4 = true;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                VkPhysicalDeviceFeatures2 *pFeatures)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   radv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   VkPhysicalDeviceVulkan11Features core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
   };
   radv_get_physical_device_features_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Features core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   radv_get_physical_device_features_1_2(pdevice, &core_1_2);

   VkPhysicalDeviceVulkan13Features core_1_3 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
   };
   radv_get_physical_device_features_1_3(pdevice, &core_1_3);

#define CORE_FEATURE(major, minor, feature) features->feature = core_##major##_##minor.feature

   vk_foreach_struct(ext, pFeatures->pNext)
   {
      if (vk_get_physical_device_core_1_1_feature_ext(ext, &core_1_1))
         continue;
      if (vk_get_physical_device_core_1_2_feature_ext(ext, &core_1_2))
         continue;
      if (vk_get_physical_device_core_1_3_feature_ext(ext, &core_1_3))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: {
         VkPhysicalDeviceConditionalRenderingFeaturesEXT *features =
            (VkPhysicalDeviceConditionalRenderingFeaturesEXT *)ext;
         features->conditionalRendering = true;
         features->inheritedConditionalRendering = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *features =
            (VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *)ext;
         features->vertexAttributeInstanceRateDivisor = true;
         features->vertexAttributeInstanceRateZeroDivisor = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
         VkPhysicalDeviceTransformFeedbackFeaturesEXT *features =
            (VkPhysicalDeviceTransformFeedbackFeaturesEXT *)ext;
         features->transformFeedback = true;
         features->geometryStreams = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: {
         VkPhysicalDeviceScalarBlockLayoutFeatures *features =
            (VkPhysicalDeviceScalarBlockLayoutFeatures *)ext;
         CORE_FEATURE(1, 2, scalarBlockLayout);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT: {
         VkPhysicalDeviceMemoryPriorityFeaturesEXT *features =
            (VkPhysicalDeviceMemoryPriorityFeaturesEXT *)ext;
         features->memoryPriority = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT: {
         VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *features =
            (VkPhysicalDeviceBufferDeviceAddressFeaturesEXT *)ext;
         CORE_FEATURE(1, 2, bufferDeviceAddress);
         CORE_FEATURE(1, 2, bufferDeviceAddressCaptureReplay);
         CORE_FEATURE(1, 2, bufferDeviceAddressMultiDevice);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: {
         VkPhysicalDeviceDepthClipEnableFeaturesEXT *features =
            (VkPhysicalDeviceDepthClipEnableFeaturesEXT *)ext;
         features->depthClipEnable = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV: {
         VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *features =
            (VkPhysicalDeviceComputeShaderDerivativesFeaturesNV *)ext;
         features->computeDerivativeGroupQuads = false;
         features->computeDerivativeGroupLinear = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_YCBCR_IMAGE_ARRAYS_FEATURES_EXT: {
         VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *features =
            (VkPhysicalDeviceYcbcrImageArraysFeaturesEXT *)ext;
         features->ycbcrImageArrays = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT: {
         VkPhysicalDeviceIndexTypeUint8FeaturesEXT *features =
            (VkPhysicalDeviceIndexTypeUint8FeaturesEXT *)ext;
         features->indexTypeUint8 = pdevice->rad_info.gfx_level >= GFX8;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR: {
         VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *features =
            (VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR *)ext;
         features->pipelineExecutableInfo = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR: {
         VkPhysicalDeviceShaderClockFeaturesKHR *features =
            (VkPhysicalDeviceShaderClockFeaturesKHR *)ext;
         features->shaderSubgroupClock = true;
         features->shaderDeviceClock = pdevice->rad_info.gfx_level >= GFX8;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT: {
         VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *features =
            (VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT *)ext;
         features->texelBufferAlignment = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COHERENT_MEMORY_FEATURES_AMD: {
         VkPhysicalDeviceCoherentMemoryFeaturesAMD *features =
            (VkPhysicalDeviceCoherentMemoryFeaturesAMD *)ext;
         features->deviceCoherentMemory = pdevice->rad_info.has_l2_uncached;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT: {
         VkPhysicalDeviceLineRasterizationFeaturesEXT *features =
            (VkPhysicalDeviceLineRasterizationFeaturesEXT *)ext;
         features->rectangularLines = true;
         features->bresenhamLines = true;
         features->smoothLines = false;
         features->stippledRectangularLines = false;
         /* FIXME: Some stippled Bresenham CTS fails on Vega10
          * but work on Raven.
          */
         features->stippledBresenhamLines = pdevice->rad_info.gfx_level != GFX9;
         features->stippledSmoothLines = false;
         break;
      }
      case VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD: {
         VkDeviceMemoryOverallocationCreateInfoAMD *features =
            (VkDeviceMemoryOverallocationCreateInfoAMD *)ext;
         features->overallocationBehavior = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT: {
         VkPhysicalDeviceRobustness2FeaturesEXT *features =
            (VkPhysicalDeviceRobustness2FeaturesEXT *)ext;
         features->robustBufferAccess2 = true;
         features->robustImageAccess2 = true;
         features->nullDescriptor = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
         VkPhysicalDeviceCustomBorderColorFeaturesEXT *features =
            (VkPhysicalDeviceCustomBorderColorFeaturesEXT *)ext;
         features->customBorderColors = true;
         features->customBorderColorWithoutFormat = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)ext;
         features->extendedDynamicState = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT: {
         VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *features =
            (VkPhysicalDeviceShaderAtomicFloatFeaturesEXT *)ext;
         features->shaderBufferFloat32Atomics = true;
         features->shaderBufferFloat32AtomicAdd = pdevice->rad_info.gfx_level >= GFX11;
         features->shaderBufferFloat64Atomics = true;
         features->shaderBufferFloat64AtomicAdd = false;
         features->shaderSharedFloat32Atomics = true;
         features->shaderSharedFloat32AtomicAdd = pdevice->rad_info.gfx_level >= GFX8;
         features->shaderSharedFloat64Atomics = true;
         features->shaderSharedFloat64AtomicAdd = false;
         features->shaderImageFloat32Atomics = true;
         features->shaderImageFloat32AtomicAdd = false;
         features->sparseImageFloat32Atomics = true;
         features->sparseImageFloat32AtomicAdd = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: {
         VkPhysicalDevice4444FormatsFeaturesEXT *features =
            (VkPhysicalDevice4444FormatsFeaturesEXT *)ext;
         features->formatA4R4G4B4 = true;
         features->formatA4B4G4R4 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT: {
         VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *features =
            (VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT *)ext;
         features->shaderImageInt64Atomics = true;
         features->sparseImageInt64Atomics = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT: {
         VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *features =
            (VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT *)ext;
         features->mutableDescriptorType = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR: {
         VkPhysicalDeviceFragmentShadingRateFeaturesKHR *features =
            (VkPhysicalDeviceFragmentShadingRateFeaturesKHR *)ext;
         features->pipelineFragmentShadingRate = true;
         features->primitiveFragmentShadingRate = true;
         features->attachmentFragmentShadingRate = radv_vrs_attachment_enabled(pdevice);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR: {
         VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *features =
            (VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR *)ext;
         features->workgroupMemoryExplicitLayout = true;
         features->workgroupMemoryExplicitLayoutScalarBlockLayout = true;
         features->workgroupMemoryExplicitLayout8BitAccess = true;
         features->workgroupMemoryExplicitLayout16BitAccess = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: {
         VkPhysicalDeviceProvokingVertexFeaturesEXT *features =
            (VkPhysicalDeviceProvokingVertexFeaturesEXT *)ext;
         features->provokingVertexLast = true;
         features->transformFeedbackPreservesProvokingVertex = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *)ext;
         features->extendedDynamicState2 = true;
         features->extendedDynamicState2LogicOp = true;
         features->extendedDynamicState2PatchControlPoints = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GLOBAL_PRIORITY_QUERY_FEATURES_KHR: {
         VkPhysicalDeviceGlobalPriorityQueryFeaturesKHR *features =
            (VkPhysicalDeviceGlobalPriorityQueryFeaturesKHR *)ext;
         features->globalPriorityQuery = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR: {
         VkPhysicalDeviceAccelerationStructureFeaturesKHR *features =
            (VkPhysicalDeviceAccelerationStructureFeaturesKHR *)ext;
         features->accelerationStructure = true;
         features->accelerationStructureCaptureReplay = true;
         features->accelerationStructureIndirectBuild = false;
         features->accelerationStructureHostCommands = false;
         features->descriptorBindingAccelerationStructureUpdateAfterBind = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR: {
         VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *features =
            (VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR *)ext;
         features->shaderSubgroupUniformControlFlow = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT: {
         VkPhysicalDeviceMultiDrawFeaturesEXT *features =
            (VkPhysicalDeviceMultiDrawFeaturesEXT *)ext;
         features->multiDraw = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
         VkPhysicalDeviceColorWriteEnableFeaturesEXT *features =
            (VkPhysicalDeviceColorWriteEnableFeaturesEXT *)ext;
         features->colorWriteEnable = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_2_FEATURES_EXT: {
         VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *features =
            (VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT *)ext;
         bool has_shader_image_float_minmax = pdevice->rad_info.gfx_level != GFX8 &&
                                              pdevice->rad_info.gfx_level != GFX9 &&
                                              pdevice->rad_info.gfx_level != GFX11;
         features->shaderBufferFloat16Atomics = false;
         features->shaderBufferFloat16AtomicAdd = false;
         features->shaderBufferFloat16AtomicMinMax = false;
         features->shaderBufferFloat32AtomicMinMax =
            radv_has_shader_buffer_float_minmax(pdevice, 32);
         features->shaderBufferFloat64AtomicMinMax =
            radv_has_shader_buffer_float_minmax(pdevice, 64);
         features->shaderSharedFloat16Atomics = false;
         features->shaderSharedFloat16AtomicAdd = false;
         features->shaderSharedFloat16AtomicMinMax = false;
         features->shaderSharedFloat32AtomicMinMax = true;
         features->shaderSharedFloat64AtomicMinMax = true;
         features->shaderImageFloat32AtomicMinMax = has_shader_image_float_minmax;
         features->sparseImageFloat32AtomicMinMax = has_shader_image_float_minmax;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR: {
         VkPhysicalDevicePresentIdFeaturesKHR *features =
            (VkPhysicalDevicePresentIdFeaturesKHR *)ext;
         features->presentId = pdevice->vk.supported_extensions.KHR_present_id;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR: {
         VkPhysicalDevicePresentWaitFeaturesKHR *features =
            (VkPhysicalDevicePresentWaitFeaturesKHR *)ext;
         features->presentWait = pdevice->vk.supported_extensions.KHR_present_wait;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT: {
         VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *features =
            (VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *)ext;
         features->primitiveTopologyListRestart = true;
         features->primitiveTopologyPatchListRestart = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR: {
         VkPhysicalDeviceRayQueryFeaturesKHR *features = (VkPhysicalDeviceRayQueryFeaturesKHR *)ext;
         features->rayQuery = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_EXT: {
         VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT *features =
            (VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT *)ext;
         features->pipelineLibraryGroupHandles = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR: {
         VkPhysicalDeviceRayTracingPipelineFeaturesKHR *features =
            (VkPhysicalDeviceRayTracingPipelineFeaturesKHR *)ext;
         features->rayTracingPipeline = true;
         features->rayTracingPipelineShaderGroupHandleCaptureReplay = true;
         features->rayTracingPipelineShaderGroupHandleCaptureReplayMixed = false;
         features->rayTracingPipelineTraceRaysIndirect = true;
         features->rayTraversalPrimitiveCulling = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR: {
         VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *features =
            (VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR *)ext;
         features->rayTracingMaintenance1 = true;
         features->rayTracingPipelineTraceRaysIndirect2 = radv_enable_rt(pdevice, true);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: {
         VkPhysicalDeviceMaintenance4Features *features =
            (VkPhysicalDeviceMaintenance4Features *)ext;
         features->maintenance4 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *)ext;
         features->vertexInputDynamicState = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT: {
         VkPhysicalDeviceImageViewMinLodFeaturesEXT *features =
            (VkPhysicalDeviceImageViewMinLodFeaturesEXT *)ext;
         features->minLod = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: {
         VkPhysicalDeviceSynchronization2Features *features =
            (VkPhysicalDeviceSynchronization2Features *)ext;
         features->synchronization2 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: {
         VkPhysicalDeviceDynamicRenderingFeatures *features =
            (VkPhysicalDeviceDynamicRenderingFeatures *)ext;
         features->dynamicRendering = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT: {
         VkPhysicalDeviceMeshShaderFeaturesEXT *features =
            (VkPhysicalDeviceMeshShaderFeaturesEXT *)ext;
         bool taskmesh_en = radv_taskmesh_enabled(pdevice);
         features->meshShader = taskmesh_en;
         features->taskShader = taskmesh_en;
         features->multiviewMeshShader = taskmesh_en;
         features->primitiveFragmentShadingRateMeshShader = taskmesh_en;
         features->meshShaderQueries = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES: {
         VkPhysicalDeviceTextureCompressionASTCHDRFeatures *features =
            (VkPhysicalDeviceTextureCompressionASTCHDRFeatures *)ext;
         features->textureCompressionASTC_HDR = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_SET_HOST_MAPPING_FEATURES_VALVE: {
         VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *features =
            (VkPhysicalDeviceDescriptorSetHostMappingFeaturesVALVE *)ext;
         features->descriptorSetHostMapping = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT: {
         VkPhysicalDeviceDepthClipControlFeaturesEXT *features =
            (VkPhysicalDeviceDepthClipControlFeaturesEXT *)ext;
         features->depthClipControl = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_2D_VIEW_OF_3D_FEATURES_EXT: {
         VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *features =
            (VkPhysicalDeviceImage2DViewOf3DFeaturesEXT *)ext;
         features->image2DViewOf3D = true;
         features->sampler2DViewOf3D = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_FUNCTIONS_2_FEATURES_INTEL: {
         VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *features =
            (VkPhysicalDeviceShaderIntegerFunctions2FeaturesINTEL *)ext;
         features->shaderIntegerFunctions2 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVES_GENERATED_QUERY_FEATURES_EXT: {
         VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *features =
            (VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT *)ext;
         features->primitivesGeneratedQuery = true;
         features->primitivesGeneratedQueryWithRasterizerDiscard = true;
         features->primitivesGeneratedQueryWithNonZeroStreams = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT: {
         VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *features =
            (VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT *)ext;
         features->nonSeamlessCubeMap = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BORDER_COLOR_SWIZZLE_FEATURES_EXT: {
         VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *features =
            (VkPhysicalDeviceBorderColorSwizzleFeaturesEXT *)ext;
         features->borderColorSwizzle = true;
         features->borderColorSwizzleFromImage = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_FEATURES_EXT: {
         VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *features =
            (VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT *)ext;
         features->shaderModuleIdentifier = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_FEATURES_KHR: {
         VkPhysicalDevicePerformanceQueryFeaturesKHR *features =
            (VkPhysicalDevicePerformanceQueryFeaturesKHR *)ext;
         features->performanceCounterQueryPools = radv_perf_query_supported(pdevice);
         features->performanceCounterMultipleQueryPools = features->performanceCounterQueryPools;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_FEATURES_NV: {
         VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *features =
            (VkPhysicalDeviceDeviceGeneratedCommandsFeaturesNV *)ext;
         features->deviceGeneratedCommands = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT: {
         VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *features =
            (VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT *)ext;
         features->attachmentFeedbackLoopLayout = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT: {
         VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *features =
            (VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT *)ext;
         features->graphicsPipelineLibrary = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicState3FeaturesEXT *)ext;
         features->extendedDynamicState3TessellationDomainOrigin = true;
         features->extendedDynamicState3PolygonMode = true;
         features->extendedDynamicState3SampleMask = true;
         features->extendedDynamicState3AlphaToCoverageEnable = pdevice->rad_info.gfx_level < GFX11;
         features->extendedDynamicState3LogicOpEnable = true;
         features->extendedDynamicState3LineStippleEnable = true;
         features->extendedDynamicState3ColorBlendEnable = true;
         features->extendedDynamicState3DepthClipEnable = true;
         features->extendedDynamicState3ConservativeRasterizationMode = pdevice->rad_info.gfx_level >= GFX9;
         features->extendedDynamicState3DepthClipNegativeOneToOne = true;
         features->extendedDynamicState3ProvokingVertexMode = true;
         features->extendedDynamicState3DepthClampEnable = true;
         features->extendedDynamicState3ColorWriteMask = true;
         features->extendedDynamicState3RasterizationSamples = true;
         features->extendedDynamicState3ColorBlendEquation = true;
         features->extendedDynamicState3SampleLocationsEnable = false; /* TODO */
         features->extendedDynamicState3LineRasterizationMode = true;
         features->extendedDynamicState3ExtraPrimitiveOverestimationSize = false;
         features->extendedDynamicState3AlphaToOneEnable = false;
         features->extendedDynamicState3RasterizationStream = false;
         features->extendedDynamicState3ColorBlendAdvanced = false;
         features->extendedDynamicState3ViewportWScalingEnable = false;
         features->extendedDynamicState3ViewportSwizzle = false;
         features->extendedDynamicState3CoverageToColorEnable = false;
         features->extendedDynamicState3CoverageToColorLocation = false;
         features->extendedDynamicState3CoverageModulationMode = false;
         features->extendedDynamicState3CoverageModulationTableEnable = false;
         features->extendedDynamicState3CoverageModulationTable = false;
         features->extendedDynamicState3CoverageReductionMode = false;
         features->extendedDynamicState3RepresentativeFragmentTestEnable = false;
         features->extendedDynamicState3ShadingRateImageEnable = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT: {
         VkPhysicalDeviceDescriptorBufferFeaturesEXT *features =
            (VkPhysicalDeviceDescriptorBufferFeaturesEXT *)ext;
         features->descriptorBuffer = true;
         features->descriptorBufferCaptureReplay = false;
         features->descriptorBufferImageLayoutIgnored = true;
         features->descriptorBufferPushDescriptors = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_EARLY_AND_LATE_FRAGMENT_TESTS_FEATURES_AMD: {
         VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *features =
            (VkPhysicalDeviceShaderEarlyAndLateFragmentTestsFeaturesAMD *)ext;
         features->shaderEarlyAndLateFragmentTests = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT: {
         VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *features =
            (VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT *)ext;
         features->imageSlicedViewOf3D = true;
         break;
      }
#ifdef RADV_USE_WSI_PLATFORM
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT: {
         VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *features =
            (VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT *)ext;
         features->swapchainMaintenance1 = true;
         break;
      }
#endif
      default:
         break;
      }
   }
}

static size_t
radv_max_descriptor_set_size()
{
   /* make sure that the entire descriptor set is addressable with a signed
    * 32-bit int. So the sum of all limits scaled by descriptor size has to
    * be at most 2 GiB. the combined image & samples object count as one of
    * both. This limit is for the pipeline layout, not for the set layout, but
    * there is no set limit, so we just set a pipeline limit. I don't think
    * any app is going to hit this soon. */
   return ((1ull << 31) - 16 * MAX_DYNAMIC_BUFFERS -
           MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_INLINE_UNIFORM_BLOCK_COUNT) /
          (32 /* uniform buffer, 32 due to potential space wasted on alignment */ +
           32 /* storage buffer, 32 due to potential space wasted on alignment */ +
           32 /* sampler, largest when combined with image */ + 64 /* sampled image */ +
           64 /* storage image */);
}

static uint32_t
radv_uniform_buffer_offset_alignment(const struct radv_physical_device *pdevice)
{
   uint32_t uniform_offset_alignment =
      driQueryOptioni(&pdevice->instance->dri_options, "radv_override_uniform_offset_alignment");
   if (!util_is_power_of_two_or_zero(uniform_offset_alignment)) {
      fprintf(stderr,
              "ERROR: invalid radv_override_uniform_offset_alignment setting %d:"
              "not a power of two\n",
              uniform_offset_alignment);
      uniform_offset_alignment = 0;
   }

   /* Take at least the hardware limit. */
   return MAX2(uniform_offset_alignment, 4);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceProperties *pProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   VkSampleCountFlags sample_counts = 0xf;

   size_t max_descriptor_set_size = radv_max_descriptor_set_size();

   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D = (1 << 14),
      .maxImageDimension2D = (1 << 14),
      .maxImageDimension3D = (1 << 11),
      .maxImageDimensionCube = (1 << 14),
      .maxImageArrayLayers = (1 << 11),
      .maxTexelBufferElements = UINT32_MAX,
      .maxUniformBufferRange = UINT32_MAX,
      .maxStorageBufferRange = UINT32_MAX,
      .maxPushConstantsSize = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount = UINT32_MAX,
      .maxSamplerAllocationCount = 64 * 1024,
      .bufferImageGranularity = 1,
      .sparseAddressSpaceSize = RADV_MAX_MEMORY_ALLOCATION_SIZE, /* buffer max size */
      .maxBoundDescriptorSets = MAX_SETS,
      .maxPerStageDescriptorSamplers = max_descriptor_set_size,
      .maxPerStageDescriptorUniformBuffers = max_descriptor_set_size,
      .maxPerStageDescriptorStorageBuffers = max_descriptor_set_size,
      .maxPerStageDescriptorSampledImages = max_descriptor_set_size,
      .maxPerStageDescriptorStorageImages = max_descriptor_set_size,
      .maxPerStageDescriptorInputAttachments = max_descriptor_set_size,
      .maxPerStageResources = max_descriptor_set_size,
      .maxDescriptorSetSamplers = max_descriptor_set_size,
      .maxDescriptorSetUniformBuffers = max_descriptor_set_size,
      .maxDescriptorSetUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS,
      .maxDescriptorSetStorageBuffers = max_descriptor_set_size,
      .maxDescriptorSetStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS,
      .maxDescriptorSetSampledImages = max_descriptor_set_size,
      .maxDescriptorSetStorageImages = max_descriptor_set_size,
      .maxDescriptorSetInputAttachments = max_descriptor_set_size,
      .maxVertexInputAttributes = MAX_VERTEX_ATTRIBS,
      .maxVertexInputBindings = MAX_VBS,
      .maxVertexInputAttributeOffset = UINT32_MAX,
      .maxVertexInputBindingStride = 2048,
      .maxVertexOutputComponents = 128,
      .maxTessellationGenerationLevel = 64,
      .maxTessellationPatchSize = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 120,
      .maxTessellationControlTotalOutputComponents = 4096,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations = 127,
      .maxGeometryInputComponents = 64,
      .maxGeometryOutputComponents = 128,
      .maxGeometryOutputVertices = 256,
      .maxGeometryTotalOutputComponents = 1024,
      .maxFragmentInputComponents = 128,
      .maxFragmentOutputAttachments = 8,
      .maxFragmentDualSrcAttachments = 1,
      .maxFragmentCombinedOutputResources = max_descriptor_set_size,
      .maxComputeSharedMemorySize = pdevice->max_shared_size,
      .maxComputeWorkGroupCount = {65535, 65535, 65535},
      .maxComputeWorkGroupInvocations = 1024,
      .maxComputeWorkGroupSize = {1024, 1024, 1024},
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .mipmapPrecisionBits = 8,
      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = UINT32_MAX,
      .maxSamplerLodBias = 16,
      .maxSamplerAnisotropy = 16,
      .maxViewports = MAX_VIEWPORTS,
      .maxViewportDimensions = {(1 << 14), (1 << 14)},
      .viewportBoundsRange = {INT16_MIN, INT16_MAX},
      .viewportSubPixelBits = 8,
      .minMemoryMapAlignment = 4096, /* A page */
      .minTexelBufferOffsetAlignment = 4,
      .minUniformBufferOffsetAlignment = radv_uniform_buffer_offset_alignment(pdevice),
      .minStorageBufferOffsetAlignment = 4,
      .minTexelOffset = -32,
      .maxTexelOffset = 31,
      .minTexelGatherOffset = -32,
      .maxTexelGatherOffset = 31,
      .minInterpolationOffset = -2,
      .maxInterpolationOffset = 2,
      .subPixelInterpolationOffsetBits = 8,
      .maxFramebufferWidth = MAX_FRAMEBUFFER_WIDTH,
      .maxFramebufferHeight = MAX_FRAMEBUFFER_HEIGHT,
      .maxFramebufferLayers = (1 << 10),
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .maxColorAttachments = MAX_RTS,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = sample_counts,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = sample_counts,
      .maxSampleMaskWords = 1,
      .timestampComputeAndGraphics = true,
      .timestampPeriod = 1000000.0 / pdevice->rad_info.clock_crystal_freq,
      .maxClipDistances = 8,
      .maxCullDistances = 8,
      .maxCombinedClipAndCullDistances = 8,
      .discreteQueuePriorities = 2,
      .pointSizeRange = {0.0, 8191.875},
      .lineWidthRange = {0.0, 8.0},
      .pointSizeGranularity = (1.0 / 8.0),
      .lineWidthGranularity = (1.0 / 8.0),
      .strictLines = false, /* FINISHME */
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 1,
      .optimalBufferCopyRowPitchAlignment = 1,
      .nonCoherentAtomSize = 64,
   };

   VkPhysicalDeviceType device_type;

   if (pdevice->rad_info.has_dedicated_vram) {
      device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
   } else {
      device_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
   }

   *pProperties = (VkPhysicalDeviceProperties){
      .apiVersion = RADV_API_VERSION,
      .driverVersion = vk_get_driver_version(),
      .vendorID = ATI_VENDOR_ID,
      .deviceID = pdevice->rad_info.pci_id,
      .deviceType = device_type,
      .limits = limits,
      .sparseProperties =
         {
            .residencyNonResidentStrict = pdevice->rad_info.family >= CHIP_POLARIS10,
            .residencyStandard2DBlockShape = pdevice->rad_info.family >= CHIP_POLARIS10,
            .residencyStandard3DBlockShape = pdevice->rad_info.gfx_level >= GFX9,
         },
   };

   strcpy(pProperties->deviceName, pdevice->marketing_name);
   memcpy(pProperties->pipelineCacheUUID, pdevice->cache_uuid, VK_UUID_SIZE);
}

static void
radv_get_physical_device_properties_1_1(struct radv_physical_device *pdevice,
                                        VkPhysicalDeviceVulkan11Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);

   memcpy(p->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
   memcpy(p->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
   memset(p->deviceLUID, 0, VK_LUID_SIZE);
   /* The LUID is for Windows. */
   p->deviceLUIDValid = false;
   p->deviceNodeMask = 0;

   p->subgroupSize = RADV_SUBGROUP_SIZE;
   p->subgroupSupportedStages = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
   if (radv_taskmesh_enabled(pdevice))
      p->subgroupSupportedStages |= VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT;

   if (radv_enable_rt(pdevice, true))
      p->subgroupSupportedStages |= RADV_RT_STAGE_BITS;
   p->subgroupSupportedOperations =
      VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
      VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
      VK_SUBGROUP_FEATURE_CLUSTERED_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT |
      VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
   p->subgroupQuadOperationsInAllStages = true;

   p->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
   p->maxMultiviewViewCount = MAX_VIEWS;
   p->maxMultiviewInstanceIndex = INT_MAX;
   p->protectedNoFault = false;
   p->maxPerSetDescriptors = RADV_MAX_PER_SET_DESCRIPTORS;
   p->maxMemoryAllocationSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
}

static const char *
radv_get_compiler_string(struct radv_physical_device *pdevice)
{
   if (!pdevice->use_llvm) {
      /* Some games like SotTR apply shader workarounds if the LLVM
       * version is too old or if the LLVM version string is
       * missing. This gives 2-5% performance with SotTR and ACO.
       */
      if (driQueryOptionb(&pdevice->instance->dri_options, "radv_report_llvm9_version_string")) {
         return " (LLVM 9.0.1)";
      }

      return "";
   }

#ifdef LLVM_AVAILABLE
   return " (LLVM " MESA_LLVM_VERSION_STRING ")";
#else
   unreachable("LLVM is not available");
#endif
}

static void
radv_get_physical_device_properties_1_2(struct radv_physical_device *pdevice,
                                        VkPhysicalDeviceVulkan12Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);

   p->driverID = VK_DRIVER_ID_MESA_RADV;
   snprintf(p->driverName, VK_MAX_DRIVER_NAME_SIZE, "radv");
   snprintf(p->driverInfo, VK_MAX_DRIVER_INFO_SIZE, "Mesa " PACKAGE_VERSION MESA_GIT_SHA1 "%s",
            radv_get_compiler_string(pdevice));

   if (radv_is_conformant(pdevice)) {
      if (pdevice->rad_info.gfx_level >= GFX10_3) {
         p->conformanceVersion = (VkConformanceVersion){
            .major = 1,
            .minor = 3,
            .subminor = 0,
            .patch = 0,
         };
      } else {
         p->conformanceVersion = (VkConformanceVersion){
            .major = 1,
            .minor = 2,
            .subminor = 7,
            .patch = 1,
         };
      }
   } else {
      p->conformanceVersion = (VkConformanceVersion){
         .major = 0,
         .minor = 0,
         .subminor = 0,
         .patch = 0,
      };
   }

   /* On AMD hardware, denormals and rounding modes for fp16/fp64 are
    * controlled by the same config register.
    */
   if (pdevice->rad_info.has_packed_math_16bit) {
      p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY;
      p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY;
   } else {
      p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
      p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
   }

   /* With LLVM, do not allow both preserving and flushing denorms because
    * different shaders in the same pipeline can have different settings and
    * this won't work for merged shaders. To make it work, this requires LLVM
    * support for changing the register. The same logic applies for the
    * rounding modes because they are configured with the same config
    * register.
    */
   p->shaderDenormFlushToZeroFloat32 = true;
   p->shaderDenormPreserveFloat32 = !pdevice->use_llvm;
   p->shaderRoundingModeRTEFloat32 = true;
   p->shaderRoundingModeRTZFloat32 = !pdevice->use_llvm;
   p->shaderSignedZeroInfNanPreserveFloat32 = true;

   p->shaderDenormFlushToZeroFloat16 =
      pdevice->rad_info.has_packed_math_16bit && !pdevice->use_llvm;
   p->shaderDenormPreserveFloat16 = pdevice->rad_info.has_packed_math_16bit;
   p->shaderRoundingModeRTEFloat16 = pdevice->rad_info.has_packed_math_16bit;
   p->shaderRoundingModeRTZFloat16 = pdevice->rad_info.has_packed_math_16bit && !pdevice->use_llvm;
   p->shaderSignedZeroInfNanPreserveFloat16 = pdevice->rad_info.has_packed_math_16bit;

   p->shaderDenormFlushToZeroFloat64 = pdevice->rad_info.gfx_level >= GFX8 && !pdevice->use_llvm;
   p->shaderDenormPreserveFloat64 = pdevice->rad_info.gfx_level >= GFX8;
   p->shaderRoundingModeRTEFloat64 = pdevice->rad_info.gfx_level >= GFX8;
   p->shaderRoundingModeRTZFloat64 = pdevice->rad_info.gfx_level >= GFX8 && !pdevice->use_llvm;
   p->shaderSignedZeroInfNanPreserveFloat64 = pdevice->rad_info.gfx_level >= GFX8;

   p->maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX / 64;
   p->shaderUniformBufferArrayNonUniformIndexingNative = false;
   p->shaderSampledImageArrayNonUniformIndexingNative = false;
   p->shaderStorageBufferArrayNonUniformIndexingNative = false;
   p->shaderStorageImageArrayNonUniformIndexingNative = false;
   p->shaderInputAttachmentArrayNonUniformIndexingNative = false;
   p->robustBufferAccessUpdateAfterBind = true;
   p->quadDivergentImplicitLod = false;

   size_t max_descriptor_set_size = radv_max_descriptor_set_size();

   p->maxPerStageDescriptorUpdateAfterBindSamplers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindUniformBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindSampledImages = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageImages = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindInputAttachments = max_descriptor_set_size;
   p->maxPerStageUpdateAfterBindResources = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindSamplers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS;
   p->maxDescriptorSetUpdateAfterBindStorageBuffers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS;
   p->maxDescriptorSetUpdateAfterBindSampledImages = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageImages = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindInputAttachments = max_descriptor_set_size;

   /* We support all of the depth resolve modes */
   p->supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT | VK_RESOLVE_MODE_AVERAGE_BIT |
                                   VK_RESOLVE_MODE_MIN_BIT | VK_RESOLVE_MODE_MAX_BIT;

   /* Average doesn't make sense for stencil so we don't support that */
   p->supportedStencilResolveModes =
      VK_RESOLVE_MODE_SAMPLE_ZERO_BIT | VK_RESOLVE_MODE_MIN_BIT | VK_RESOLVE_MODE_MAX_BIT;

   p->independentResolveNone = true;
   p->independentResolve = true;

   /* GFX6-8 only support single channel min/max filter. */
   p->filterMinmaxImageComponentMapping = pdevice->rad_info.gfx_level >= GFX9;
   p->filterMinmaxSingleComponentFormats = true;

   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;

   p->framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
}

static void
radv_get_physical_device_properties_1_3(struct radv_physical_device *pdevice,
                                        VkPhysicalDeviceVulkan13Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES);

   p->minSubgroupSize = 64;
   p->maxSubgroupSize = 64;
   p->maxComputeWorkgroupSubgroups = UINT32_MAX;
   p->requiredSubgroupSizeStages = 0;
   if (pdevice->rad_info.gfx_level >= GFX10) {
      /* Only GFX10+ supports wave32. */
      p->minSubgroupSize = 32;
      p->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT;
   }

   p->maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE;
   p->maxPerStageDescriptorInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
   p->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
      MAX_INLINE_UNIFORM_BLOCK_SIZE * MAX_SETS;
   p->maxDescriptorSetInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
   p->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UNIFORM_BLOCK_COUNT;
   p->maxInlineUniformTotalSize = UINT16_MAX;

   bool accel = pdevice->rad_info.has_accelerated_dot_product;
   bool gfx11plus = pdevice->rad_info.gfx_level >= GFX11;
   p->integerDotProduct8BitUnsignedAccelerated = accel;
   p->integerDotProduct8BitSignedAccelerated = accel;
   p->integerDotProduct8BitMixedSignednessAccelerated = accel && gfx11plus;
   p->integerDotProduct4x8BitPackedUnsignedAccelerated = accel;
   p->integerDotProduct4x8BitPackedSignedAccelerated = accel;
   p->integerDotProduct4x8BitPackedMixedSignednessAccelerated = accel && gfx11plus;
   p->integerDotProduct16BitUnsignedAccelerated = accel && !gfx11plus;
   p->integerDotProduct16BitSignedAccelerated = accel && !gfx11plus;
   p->integerDotProduct16BitMixedSignednessAccelerated = false;
   p->integerDotProduct32BitUnsignedAccelerated = false;
   p->integerDotProduct32BitSignedAccelerated = false;
   p->integerDotProduct32BitMixedSignednessAccelerated = false;
   p->integerDotProduct64BitUnsignedAccelerated = false;
   p->integerDotProduct64BitSignedAccelerated = false;
   p->integerDotProduct64BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating8BitSignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = accel && gfx11plus;
   p->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = accel;
   p->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated =
      accel && gfx11plus;
   p->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = accel && !gfx11plus;
   p->integerDotProductAccumulatingSaturating16BitSignedAccelerated = accel && !gfx11plus;
   p->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = false;

   p->storageTexelBufferOffsetAlignmentBytes = 4;
   p->storageTexelBufferOffsetSingleTexelAlignment = true;
   p->uniformTexelBufferOffsetAlignmentBytes = 4;
   p->uniformTexelBufferOffsetSingleTexelAlignment = true;

   p->maxBufferSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceProperties2 *pProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   radv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   VkPhysicalDeviceVulkan11Properties core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
   };
   radv_get_physical_device_properties_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Properties core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
   };
   radv_get_physical_device_properties_1_2(pdevice, &core_1_2);

   VkPhysicalDeviceVulkan13Properties core_1_3 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES,
   };
   radv_get_physical_device_properties_1_3(pdevice, &core_1_3);

   vk_foreach_struct(ext, pProperties->pNext)
   {
      if (vk_get_physical_device_core_1_1_property_ext(ext, &core_1_1))
         continue;
      if (vk_get_physical_device_core_1_2_property_ext(ext, &core_1_2))
         continue;
      if (vk_get_physical_device_core_1_3_property_ext(ext, &core_1_3))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *)ext;
         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DISCARD_RECTANGLE_PROPERTIES_EXT: {
         VkPhysicalDeviceDiscardRectanglePropertiesEXT *properties =
            (VkPhysicalDeviceDiscardRectanglePropertiesEXT *)ext;
         properties->maxDiscardRectangles = MAX_DISCARD_RECTANGLES;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT: {
         VkPhysicalDeviceExternalMemoryHostPropertiesEXT *properties =
            (VkPhysicalDeviceExternalMemoryHostPropertiesEXT *)ext;
         properties->minImportedHostPointerAlignment = 4096;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_AMD: {
         VkPhysicalDeviceShaderCorePropertiesAMD *properties =
            (VkPhysicalDeviceShaderCorePropertiesAMD *)ext;

         /* Shader engines. */
         properties->shaderEngineCount = pdevice->rad_info.max_se;
         properties->shaderArraysPerEngineCount = pdevice->rad_info.max_sa_per_se;
         properties->computeUnitsPerShaderArray = pdevice->rad_info.min_good_cu_per_sa;
         properties->simdPerComputeUnit = pdevice->rad_info.num_simd_per_compute_unit;
         properties->wavefrontsPerSimd = pdevice->rad_info.max_wave64_per_simd;
         properties->wavefrontSize = 64;

         /* SGPR. */
         properties->sgprsPerSimd = pdevice->rad_info.num_physical_sgprs_per_simd;
         properties->minSgprAllocation = pdevice->rad_info.min_sgpr_alloc;
         properties->maxSgprAllocation = pdevice->rad_info.max_sgpr_alloc;
         properties->sgprAllocationGranularity = pdevice->rad_info.sgpr_alloc_granularity;

         /* VGPR. */
         properties->vgprsPerSimd = pdevice->rad_info.num_physical_wave64_vgprs_per_simd;
         properties->minVgprAllocation = pdevice->rad_info.min_wave64_vgpr_alloc;
         properties->maxVgprAllocation = pdevice->rad_info.max_vgpr_alloc;
         properties->vgprAllocationGranularity = pdevice->rad_info.wave64_vgpr_alloc_granularity;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD: {
         VkPhysicalDeviceShaderCoreProperties2AMD *properties =
            (VkPhysicalDeviceShaderCoreProperties2AMD *)ext;

         properties->shaderCoreFeatures = 0;
         properties->activeComputeUnitCount = pdevice->rad_info.num_cu;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *properties =
            (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
         properties->maxVertexAttribDivisor = UINT32_MAX;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceConservativeRasterizationPropertiesEXT *properties =
            (VkPhysicalDeviceConservativeRasterizationPropertiesEXT *)ext;
         properties->primitiveOverestimationSize = 0;
         properties->maxExtraPrimitiveOverestimationSize = 0;
         properties->extraPrimitiveOverestimationSizeGranularity = 0;
         properties->primitiveUnderestimation = true;
         properties->conservativePointAndLineRasterization = false;
         properties->degenerateTrianglesRasterized = true;
         properties->degenerateLinesRasterized = false;
         properties->fullyCoveredFragmentShaderInputVariable = true;
         properties->conservativeRasterizationPostDepthCoverage = false;
         break;
      }
#ifndef _WIN32
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT: {
         VkPhysicalDevicePCIBusInfoPropertiesEXT *properties =
            (VkPhysicalDevicePCIBusInfoPropertiesEXT *)ext;
         properties->pciDomain = pdevice->bus_info.domain;
         properties->pciBus = pdevice->bus_info.bus;
         properties->pciDevice = pdevice->bus_info.dev;
         properties->pciFunction = pdevice->bus_info.func;
         break;
      }
#endif
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
         VkPhysicalDeviceTransformFeedbackPropertiesEXT *properties =
            (VkPhysicalDeviceTransformFeedbackPropertiesEXT *)ext;
         properties->maxTransformFeedbackStreams = MAX_SO_STREAMS;
         properties->maxTransformFeedbackBuffers = MAX_SO_BUFFERS;
         properties->maxTransformFeedbackBufferSize = UINT32_MAX;
         properties->maxTransformFeedbackStreamDataSize = 512;
         properties->maxTransformFeedbackBufferDataSize = 512;
         properties->maxTransformFeedbackBufferDataStride = 512;
         properties->transformFeedbackQueries = true;
         properties->transformFeedbackStreamsLinesTriangles = true;
         properties->transformFeedbackRasterizationStreamSelect = false;
         properties->transformFeedbackDraw = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT: {
         VkPhysicalDeviceSampleLocationsPropertiesEXT *properties =
            (VkPhysicalDeviceSampleLocationsPropertiesEXT *)ext;
         properties->sampleLocationSampleCounts =
            VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
         properties->maxSampleLocationGridSize = (VkExtent2D){2, 2};
         properties->sampleLocationCoordinateRange[0] = 0.0f;
         properties->sampleLocationCoordinateRange[1] = 0.9375f;
         properties->sampleLocationSubPixelBits = 4;
         properties->variableSampleLocations = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceLineRasterizationPropertiesEXT *props =
            (VkPhysicalDeviceLineRasterizationPropertiesEXT *)ext;
         props->lineSubPixelPrecisionBits = 4;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT: {
         VkPhysicalDeviceRobustness2PropertiesEXT *properties =
            (VkPhysicalDeviceRobustness2PropertiesEXT *)ext;
         properties->robustStorageBufferAccessSizeAlignment = 4;
         properties->robustUniformBufferAccessSizeAlignment = 4;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: {
         VkPhysicalDeviceCustomBorderColorPropertiesEXT *props =
            (VkPhysicalDeviceCustomBorderColorPropertiesEXT *)ext;
         props->maxCustomBorderColorSamplers = RADV_BORDER_COLOR_COUNT;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR: {
         VkPhysicalDeviceFragmentShadingRatePropertiesKHR *props =
            (VkPhysicalDeviceFragmentShadingRatePropertiesKHR *)ext;
         if (radv_vrs_attachment_enabled(pdevice)) {
            props->minFragmentShadingRateAttachmentTexelSize = (VkExtent2D){8, 8};
            props->maxFragmentShadingRateAttachmentTexelSize = (VkExtent2D){8, 8};
         } else {
            props->minFragmentShadingRateAttachmentTexelSize = (VkExtent2D){0, 0};
            props->maxFragmentShadingRateAttachmentTexelSize = (VkExtent2D){0, 0};
         }
         props->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = 1;
         props->primitiveFragmentShadingRateWithMultipleViewports = true;
         props->layeredShadingRateAttachments = false; /* TODO */
         props->fragmentShadingRateNonTrivialCombinerOps = true;
         props->maxFragmentSize = (VkExtent2D){2, 2};
         props->maxFragmentSizeAspectRatio = 2;
         props->maxFragmentShadingRateCoverageSamples = 32;
         props->maxFragmentShadingRateRasterizationSamples = VK_SAMPLE_COUNT_8_BIT;
         props->fragmentShadingRateWithShaderDepthStencilWrites =
            !pdevice->rad_info.has_vrs_ds_export_bug;
         props->fragmentShadingRateWithSampleMask = true;
         props->fragmentShadingRateWithShaderSampleMask = false;
         props->fragmentShadingRateWithConservativeRasterization = true;
         props->fragmentShadingRateWithFragmentShaderInterlock = false;
         props->fragmentShadingRateWithCustomSampleLocations = false;
         props->fragmentShadingRateStrictMultiplyCombiner = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT: {
         VkPhysicalDeviceProvokingVertexPropertiesEXT *props =
            (VkPhysicalDeviceProvokingVertexPropertiesEXT *)ext;
         props->provokingVertexModePerPipeline = true;
         props->transformFeedbackPreservesTriangleFanProvokingVertex = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR: {
         VkPhysicalDeviceAccelerationStructurePropertiesKHR *props =
            (VkPhysicalDeviceAccelerationStructurePropertiesKHR *)ext;
         props->maxGeometryCount = (1 << 24) - 1;
         props->maxInstanceCount = (1 << 24) - 1;
         props->maxPrimitiveCount = (1 << 29) - 1;
         props->maxPerStageDescriptorAccelerationStructures =
            pProperties->properties.limits.maxPerStageDescriptorStorageBuffers;
         props->maxPerStageDescriptorUpdateAfterBindAccelerationStructures =
            pProperties->properties.limits.maxPerStageDescriptorStorageBuffers;
         props->maxDescriptorSetAccelerationStructures =
            pProperties->properties.limits.maxDescriptorSetStorageBuffers;
         props->maxDescriptorSetUpdateAfterBindAccelerationStructures =
            pProperties->properties.limits.maxDescriptorSetStorageBuffers;
         props->minAccelerationStructureScratchOffsetAlignment = 128;
         break;
      }
#ifndef _WIN32
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT: {
         VkPhysicalDeviceDrmPropertiesEXT *props = (VkPhysicalDeviceDrmPropertiesEXT *)ext;
         if (pdevice->available_nodes & (1 << DRM_NODE_PRIMARY)) {
            props->hasPrimary = true;
            props->primaryMajor = (int64_t)major(pdevice->primary_devid);
            props->primaryMinor = (int64_t)minor(pdevice->primary_devid);
         } else {
            props->hasPrimary = false;
         }
         if (pdevice->available_nodes & (1 << DRM_NODE_RENDER)) {
            props->hasRender = true;
            props->renderMajor = (int64_t)major(pdevice->render_devid);
            props->renderMinor = (int64_t)minor(pdevice->render_devid);
         } else {
            props->hasRender = false;
         }
         break;
      }
#endif
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT: {
         VkPhysicalDeviceMultiDrawPropertiesEXT *props =
            (VkPhysicalDeviceMultiDrawPropertiesEXT *)ext;
         props->maxMultiDrawCount = 2048;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR: {
         VkPhysicalDeviceRayTracingPipelinePropertiesKHR *props =
            (VkPhysicalDeviceRayTracingPipelinePropertiesKHR *)ext;
         props->shaderGroupHandleSize = RADV_RT_HANDLE_SIZE;
         props->maxRayRecursionDepth = 31;    /* Minimum allowed for DXR. */
         props->maxShaderGroupStride = 16384; /* dummy */
         /* This isn't strictly necessary, but Doom Eternal breaks if the
          * alignment is any lower. */
         props->shaderGroupBaseAlignment = RADV_RT_HANDLE_SIZE;
         props->shaderGroupHandleCaptureReplaySize = RADV_RT_HANDLE_SIZE;
         props->maxRayDispatchInvocationCount = 1024 * 1024 * 64;
         props->shaderGroupHandleAlignment = 16;
         props->maxRayHitAttributeSize = RADV_MAX_HIT_ATTRIB_SIZE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: {
         VkPhysicalDeviceMaintenance4Properties *properties =
            (VkPhysicalDeviceMaintenance4Properties *)ext;
         properties->maxBufferSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MODULE_IDENTIFIER_PROPERTIES_EXT: {
         VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *properties =
            (VkPhysicalDeviceShaderModuleIdentifierPropertiesEXT *)ext;
         STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                       sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
         memcpy(properties->shaderModuleIdentifierAlgorithmUUID,
                vk_shaderModuleIdentifierAlgorithmUUID,
                sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PERFORMANCE_QUERY_PROPERTIES_KHR: {
         VkPhysicalDevicePerformanceQueryPropertiesKHR *properties =
            (VkPhysicalDevicePerformanceQueryPropertiesKHR *)ext;
         properties->allowCommandBufferQueryCopies = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_GENERATED_COMMANDS_PROPERTIES_NV: {
         VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *properties =
            (VkPhysicalDeviceDeviceGeneratedCommandsPropertiesNV *)ext;
         properties->maxIndirectCommandsStreamCount = 1;
         properties->maxIndirectCommandsStreamStride = UINT32_MAX;
         properties->maxIndirectCommandsTokenCount = UINT32_MAX;
         properties->maxIndirectCommandsTokenOffset = UINT16_MAX;
         properties->minIndirectCommandsBufferOffsetAlignment = 4;
         properties->minSequencesCountBufferOffsetAlignment = 4;
         properties->minSequencesIndexBufferOffsetAlignment = 4;

         /* Don't support even a shader group count = 1 until we support shader
          * overrides during pipeline creation. */
         properties->maxGraphicsShaderGroupCount = 0;

         properties->maxIndirectSequenceCount = UINT32_MAX;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT: {
         VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *props =
            (VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT *)ext;
         props->graphicsPipelineLibraryFastLinking = true;
         props->graphicsPipelineLibraryIndependentInterpolationDecoration = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
         VkPhysicalDeviceMeshShaderPropertiesEXT *properties =
            (VkPhysicalDeviceMeshShaderPropertiesEXT *)ext;

         properties->maxTaskWorkGroupTotalCount = 4194304; /* 2^22 min required */
         properties->maxTaskWorkGroupCount[0] = 65535;
         properties->maxTaskWorkGroupCount[1] = 65535;
         properties->maxTaskWorkGroupCount[2] = 65535;
         properties->maxTaskWorkGroupInvocations = 1024;
         properties->maxTaskWorkGroupSize[0] = 1024;
         properties->maxTaskWorkGroupSize[1] = 1024;
         properties->maxTaskWorkGroupSize[2] = 1024;
         properties->maxTaskPayloadSize = 16384; /* 16K min required */
         properties->maxTaskSharedMemorySize = 65536;
         properties->maxTaskPayloadAndSharedMemorySize = 65536;

         properties->maxMeshWorkGroupTotalCount = 4194304; /* 2^22 min required */
         properties->maxMeshWorkGroupCount[0] = 65535;
         properties->maxMeshWorkGroupCount[1] = 65535;
         properties->maxMeshWorkGroupCount[2] = 65535;
         properties->maxMeshWorkGroupInvocations = 256; /* Max NGG HW limit */
         properties->maxMeshWorkGroupSize[0] = 256;
         properties->maxMeshWorkGroupSize[1] = 256;
         properties->maxMeshWorkGroupSize[2] = 256;
         properties->maxMeshOutputMemorySize = 32 * 1024; /* 32K min required */
         properties->maxMeshSharedMemorySize = 28672;     /* 28K min required */
         properties->maxMeshPayloadAndSharedMemorySize =
            properties->maxTaskPayloadSize +
            properties->maxMeshSharedMemorySize; /* 28K min required */
         properties->maxMeshPayloadAndOutputMemorySize =
            properties->maxTaskPayloadSize +
            properties->maxMeshOutputMemorySize;    /* 47K min required */
         properties->maxMeshOutputComponents = 128; /* 32x vec4 min required */
         properties->maxMeshOutputVertices = 256;
         properties->maxMeshOutputPrimitives = 256;
         properties->maxMeshOutputLayers = 8;
         properties->maxMeshMultiviewViewCount = MAX_VIEWS;
         properties->meshOutputPerVertexGranularity = 1;
         properties->meshOutputPerPrimitiveGranularity = 1;

         properties->maxPreferredTaskWorkGroupInvocations = 64;
         properties->maxPreferredMeshWorkGroupInvocations = 128;
         properties->prefersLocalInvocationVertexOutput = true;
         properties->prefersLocalInvocationPrimitiveOutput = true;
         properties->prefersCompactVertexOutput = true;
         properties->prefersCompactPrimitiveOutput = false;

         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT: {
         VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *properties =
            (VkPhysicalDeviceExtendedDynamicState3PropertiesEXT *)ext;
         properties->dynamicPrimitiveTopologyUnrestricted = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT: {
         VkPhysicalDeviceDescriptorBufferPropertiesEXT *properties =
            (VkPhysicalDeviceDescriptorBufferPropertiesEXT *)ext;
         properties->combinedImageSamplerDescriptorSingleArray = true;
         properties->bufferlessPushDescriptors = true;
         properties->allowSamplerImageViewPostSubmitCreation = false;
         properties->descriptorBufferOffsetAlignment = 4;
         properties->maxDescriptorBufferBindings = MAX_SETS;
         properties->maxResourceDescriptorBufferBindings = MAX_SETS;
         properties->maxSamplerDescriptorBufferBindings = MAX_SETS;
         properties->maxEmbeddedImmutableSamplerBindings = MAX_SETS;
         properties->maxEmbeddedImmutableSamplers = radv_max_descriptor_set_size();
         properties->bufferCaptureReplayDescriptorDataSize = 0;
         properties->imageCaptureReplayDescriptorDataSize = 0;
         properties->imageViewCaptureReplayDescriptorDataSize = 0;
         properties->samplerCaptureReplayDescriptorDataSize = 0;
         properties->accelerationStructureCaptureReplayDescriptorDataSize = 0;
         properties->samplerDescriptorSize = 16;
         properties->combinedImageSamplerDescriptorSize = 96;
         properties->sampledImageDescriptorSize = 64;
         properties->storageImageDescriptorSize = 32;
         properties->uniformTexelBufferDescriptorSize = 16;
         properties->robustUniformTexelBufferDescriptorSize = 16;
         properties->storageTexelBufferDescriptorSize = 16;
         properties->robustStorageTexelBufferDescriptorSize = 16;
         properties->uniformBufferDescriptorSize = 16;
         properties->robustUniformBufferDescriptorSize = 16;
         properties->storageBufferDescriptorSize = 16;
         properties->robustStorageBufferDescriptorSize = 16;
         properties->inputAttachmentDescriptorSize = 64;
         properties->accelerationStructureDescriptorSize = 16;
         properties->maxSamplerDescriptorBufferRange = UINT32_MAX;
         properties->maxResourceDescriptorBufferRange = UINT32_MAX;
         properties->samplerDescriptorBufferAddressSpaceSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
         properties->resourceDescriptorBufferAddressSpaceSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
         properties->descriptorBufferAddressSpaceSize = RADV_MAX_MEMORY_ALLOCATION_SIZE;
         break;
      }
      default:
         break;
      }
   }
}

static VkResult
radv_physical_device_try_create(struct radv_instance *instance, drmDevicePtr drm_device,
                                struct radv_physical_device **device_out)
{
   VkResult result;
   int fd = -1;
   int master_fd = -1;

#ifdef _WIN32
   assert(drm_device == NULL);
#else
   if (drm_device) {
      const char *path = drm_device->nodes[DRM_NODE_RENDER];
      drmVersionPtr version;

      fd = open(path, O_RDWR | O_CLOEXEC);
      if (fd < 0) {
         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER, "Could not open device %s: %m",
                          path);
      }

      version = drmGetVersion(fd);
      if (!version) {
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Could not get the kernel driver version for device %s: %m", path);
      }

      if (strcmp(version->name, "amdgpu")) {
         drmFreeVersion(version);
         close(fd);

         return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "Device '%s' is not using the AMDGPU kernel driver: %m", path);
      }
      drmFreeVersion(version);

      if (instance->debug_flags & RADV_DEBUG_STARTUP)
         fprintf(stderr, "radv: info: Found compatible device '%s'.\n", path);
   }
#endif

   struct radv_physical_device *device = vk_zalloc2(&instance->vk.alloc, NULL, sizeof(*device), 8,
                                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_fd;
   }

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &radv_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk, NULL, &dispatch_table);
   if (result != VK_SUCCESS) {
      goto fail_alloc;
   }

   device->instance = instance;

#ifdef _WIN32
   device->ws = radv_null_winsys_create();
#else
   if (drm_device) {
      bool reserve_vmid = radv_thread_trace_enabled();

      device->ws = radv_amdgpu_winsys_create(fd, instance->debug_flags, instance->perftest_flags,
                                             reserve_vmid);
   } else {
      device->ws = radv_null_winsys_create();
   }
#endif

   if (!device->ws) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "failed to initialize winsys");
      goto fail_base;
   }

   device->vk.supported_sync_types = device->ws->get_sync_types(device->ws);

#ifndef _WIN32
   if (drm_device && instance->vk.enabled_extensions.KHR_display) {
      master_fd = open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         uint32_t accel_working = 0;
         struct drm_amdgpu_info request = {.return_pointer = (uintptr_t)&accel_working,
                                           .return_size = sizeof(accel_working),
                                           .query = AMDGPU_INFO_ACCEL_WORKING};

         if (drmCommandWrite(master_fd, DRM_AMDGPU_INFO, &request, sizeof(struct drm_amdgpu_info)) <
                0 ||
             !accel_working) {
            close(master_fd);
            master_fd = -1;
         }
      }
   }
#endif

   device->master_fd = master_fd;
   device->local_fd = fd;
   device->ws->query_info(device->ws, &device->rad_info);

   device->use_llvm = instance->debug_flags & RADV_DEBUG_LLVM;
#ifndef LLVM_AVAILABLE
   if (device->use_llvm) {
      fprintf(stderr, "ERROR: LLVM compiler backend selected for radv, but LLVM support was not "
                      "enabled at build time.\n");
      abort();
   }
#endif

#ifdef ANDROID
   device->emulate_etc2 = !radv_device_supports_etc(device);
#else
   device->emulate_etc2 = !radv_device_supports_etc(device) &&
                          driQueryOptionb(&device->instance->dri_options, "radv_require_etc2");
#endif

   snprintf(device->name, sizeof(device->name), "AMD RADV %s%s", device->rad_info.name,
            radv_get_compiler_string(device));

   const char *marketing_name = device->ws->get_chip_name(device->ws);
   snprintf(device->marketing_name, sizeof(device->name), "%s (RADV %s%s)",
            marketing_name ? marketing_name : "AMD Unknown", device->rad_info.name,
            radv_get_compiler_string(device));

#ifdef ENABLE_SHADER_CACHE
   if (radv_device_get_cache_uuid(device, device->cache_uuid)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "cannot generate UUID");
      goto fail_wsi;
   }

   /* The gpu id is already embedded in the uuid so we just pass "radv"
    * when creating the cache.
    */
   char buf[VK_UUID_SIZE * 2 + 1];
   disk_cache_format_hex_id(buf, device->cache_uuid, VK_UUID_SIZE * 2);
   device->vk.disk_cache = disk_cache_create(device->name, buf, 0);
#endif

   if (!radv_is_conformant(device))
      vk_warn_non_conformant_implementation("radv");

   radv_get_driver_uuid(&device->driver_uuid);
   radv_get_device_uuid(&device->rad_info, &device->device_uuid);

   device->dcc_msaa_allowed = (device->instance->perftest_flags & RADV_PERFTEST_DCC_MSAA);

   device->use_fmask =
      device->rad_info.gfx_level < GFX11 && !(device->instance->debug_flags & RADV_DEBUG_NO_FMASK);

   device->use_ngg =
      (device->rad_info.gfx_level >= GFX10 && device->rad_info.family != CHIP_NAVI14 &&
       !(device->instance->debug_flags & RADV_DEBUG_NO_NGG)) ||
      device->rad_info.gfx_level >= GFX11;

   /* TODO: Investigate if NGG culling helps on GFX11. */
   device->use_ngg_culling = device->use_ngg && device->rad_info.max_render_backends > 1 &&
                             (device->rad_info.gfx_level == GFX10_3 ||
                              (device->instance->perftest_flags & RADV_PERFTEST_NGGC)) &&
                             !(device->instance->debug_flags & RADV_DEBUG_NO_NGGC);

   device->use_ngg_streamout =
      device->use_ngg && (device->rad_info.gfx_level >= GFX11 ||
                          (device->instance->perftest_flags & RADV_PERFTEST_NGG_STREAMOUT));

   device->emulate_ngg_gs_query_pipeline_stat =
      device->use_ngg && device->rad_info.gfx_level < GFX11;

   /* Determine the number of threads per wave for all stages. */
   device->cs_wave_size = 64;
   device->ps_wave_size = 64;
   device->ge_wave_size = 64;
   device->rt_wave_size = 64;

   if (device->rad_info.gfx_level >= GFX10) {
      if (device->instance->perftest_flags & RADV_PERFTEST_CS_WAVE_32)
         device->cs_wave_size = 32;

      /* For pixel shaders, wave64 is recommanded. */
      if (device->instance->perftest_flags & RADV_PERFTEST_PS_WAVE_32)
         device->ps_wave_size = 32;

      if (device->instance->perftest_flags & RADV_PERFTEST_GE_WAVE_32)
         device->ge_wave_size = 32;

      /* Default to 32 on RDNA1-2 as that gives better perf due to less issues with divergence.
       * However, on GFX11 default to wave64 as ACO does not support VOPD yet, and with the VALU
       * dependence wave32 would likely be a net-loss (as well as the SALU count becoming more
       * problematic)
       */
      if (!(device->instance->perftest_flags & RADV_PERFTEST_RT_WAVE_64) &&
          device->rad_info.gfx_level < GFX11)
         device->rt_wave_size = 32;
   }

   device->max_shared_size = device->rad_info.gfx_level >= GFX7 ? 65536 : 32768;

   radv_physical_device_init_mem_types(device);

   radv_physical_device_get_supported_extensions(device, &device->vk.supported_extensions);

   radv_get_nir_options(device);

#ifndef _WIN32
   if (drm_device) {
      struct stat primary_stat = {0}, render_stat = {0};

      device->available_nodes = drm_device->available_nodes;
      device->bus_info = *drm_device->businfo.pci;

      if ((drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) &&
          stat(drm_device->nodes[DRM_NODE_PRIMARY], &primary_stat) != 0) {
         result =
            vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                      "failed to stat DRM primary node %s", drm_device->nodes[DRM_NODE_PRIMARY]);
         goto fail_perfcounters;
      }
      device->primary_devid = primary_stat.st_rdev;

      if ((drm_device->available_nodes & (1 << DRM_NODE_RENDER)) &&
          stat(drm_device->nodes[DRM_NODE_RENDER], &render_stat) != 0) {
         result =
            vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED, "failed to stat DRM render node %s",
                      drm_device->nodes[DRM_NODE_RENDER]);
         goto fail_perfcounters;
      }
      device->render_devid = render_stat.st_rdev;
   }
#endif

   if ((device->instance->debug_flags & RADV_DEBUG_INFO))
      ac_print_gpu_info(&device->rad_info, stdout);

   radv_physical_device_init_queue_table(device);

   /* We don't check the error code, but later check if it is initialized. */
   ac_init_perfcounters(&device->rad_info, false, false, &device->ac_perfcounters);

   radv_init_physical_device_decoder(device);

   /* The WSI is structured as a layer on top of the driver, so this has
    * to be the last part of initialization (at least until we get other
    * semi-layers).
    */
   result = radv_init_wsi(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail_perfcounters;
   }

   device->gs_table_depth =
      ac_get_gs_table_depth(device->rad_info.gfx_level, device->rad_info.family);

   ac_get_hs_info(&device->rad_info, &device->hs);
   ac_get_task_info(&device->rad_info, &device->task_info);
   radv_get_binning_settings(device, &device->binning_settings);

   *device_out = device;

   return VK_SUCCESS;

fail_perfcounters:
   ac_destroy_perfcounters(&device->ac_perfcounters);
   disk_cache_destroy(device->vk.disk_cache);
#ifdef ENABLE_SHADER_CACHE
fail_wsi:
#endif
   device->ws->destroy(device->ws);
fail_base:
   vk_physical_device_finish(&device->vk);
fail_alloc:
   vk_free(&instance->vk.alloc, device);
fail_fd:
   if (fd != -1)
      close(fd);
   if (master_fd != -1)
      close(master_fd);
   return result;
}

VkResult
create_null_physical_device(struct vk_instance *vk_instance)
{
   struct radv_instance *instance = container_of(vk_instance, struct radv_instance, vk);
   struct radv_physical_device *pdevice;

   VkResult result = radv_physical_device_try_create(instance, NULL, &pdevice);
   if (result != VK_SUCCESS)
      return result;

   list_addtail(&pdevice->vk.link, &instance->vk.physical_devices.list);
   return VK_SUCCESS;
}

VkResult
create_drm_physical_device(struct vk_instance *vk_instance, struct _drmDevice *device,
                           struct vk_physical_device **out)
{
#ifndef _WIN32
   if (!(device->available_nodes & (1 << DRM_NODE_RENDER)) || device->bustype != DRM_BUS_PCI ||
       device->deviceinfo.pci->vendor_id != ATI_VENDOR_ID)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   return radv_physical_device_try_create((struct radv_instance *)vk_instance, device,
                                          (struct radv_physical_device **)out);
#else
   return VK_SUCCESS;
#endif
}

void
radv_physical_device_destroy(struct vk_physical_device *vk_device)
{
   struct radv_physical_device *device = container_of(vk_device, struct radv_physical_device, vk);

   radv_finish_wsi(device);
   ac_destroy_perfcounters(&device->ac_perfcounters);
   device->ws->destroy(device->ws);
   disk_cache_destroy(device->vk.disk_cache);
   if (device->local_fd != -1)
      close(device->local_fd);
   if (device->master_fd != -1)
      close(device->master_fd);
   vk_physical_device_finish(&device->vk);
   vk_free(&device->instance->vk.alloc, device);
}

static void
radv_get_physical_device_queue_family_properties(struct radv_physical_device *pdevice,
                                                 uint32_t *pCount,
                                                 VkQueueFamilyProperties **pQueueFamilyProperties)
{
   int num_queue_families = 1;
   int idx;
   if (pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues > 0 &&
       !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE))
      num_queue_families++;

   if (pdevice->instance->perftest_flags & RADV_PERFTEST_VIDEO_DECODE) {
      if (pdevice->rad_info.ip[AMD_IP_VCN_DEC].num_queues > 0)
         num_queue_families++;

      if (radv_has_uvd(pdevice))
         num_queue_families++;
   }

   if (pQueueFamilyProperties == NULL) {
      *pCount = num_queue_families;
      return;
   }

   if (!*pCount)
      return;

   idx = 0;
   if (*pCount >= 1) {
      *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
         .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT |
                       VK_QUEUE_SPARSE_BINDING_BIT,
         .queueCount = 1,
         .timestampValidBits = 64,
         .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
      };
      idx++;
   }

   if (pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues > 0 &&
       !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
      if (*pCount > idx) {
         *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
            .queueFlags =
               VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT,
            .queueCount = pdevice->rad_info.ip[AMD_IP_COMPUTE].num_queues,
            .timestampValidBits = 64,
            .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
         };
         idx++;
      }
   }

   if (pdevice->instance->perftest_flags & RADV_PERFTEST_VIDEO_DECODE) {
      if (pdevice->rad_info.ip[AMD_IP_VCN_DEC].num_queues > 0) {
         if (*pCount > idx) {
            *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
               .queueFlags = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
               .queueCount = pdevice->rad_info.ip[AMD_IP_VCN_DEC].num_queues,
               .timestampValidBits = 64,
               .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
            };
            idx++;
         }
      }

      if (radv_has_uvd(pdevice)) {
         if (*pCount > idx) {
            *pQueueFamilyProperties[idx] = (VkQueueFamilyProperties){
               .queueFlags = VK_QUEUE_VIDEO_DECODE_BIT_KHR,
               .queueCount = pdevice->rad_info.ip[AMD_IP_UVD].num_queues,
               .timestampValidBits = 64,
               .minImageTransferGranularity = (VkExtent3D){1, 1, 1},
            };
            idx++;
         }
      }
   }

   *pCount = idx;
}

static const VkQueueGlobalPriorityKHR radv_global_queue_priorities[] = {
   VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR,
   VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
   VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,
   VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR,
};

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice, uint32_t *pCount,
                                             VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   if (!pQueueFamilyProperties) {
      radv_get_physical_device_queue_family_properties(pdevice, pCount, NULL);
      return;
   }
   VkQueueFamilyProperties *properties[] = {
      &pQueueFamilyProperties[0].queueFamilyProperties,
      &pQueueFamilyProperties[1].queueFamilyProperties,
      &pQueueFamilyProperties[2].queueFamilyProperties,
   };
   radv_get_physical_device_queue_family_properties(pdevice, pCount, properties);
   assert(*pCount <= 3);

   for (uint32_t i = 0; i < *pCount; i++) {
      vk_foreach_struct(ext, pQueueFamilyProperties[i].pNext)
      {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR: {
            VkQueueFamilyGlobalPriorityPropertiesKHR *prop =
               (VkQueueFamilyGlobalPriorityPropertiesKHR *)ext;
            STATIC_ASSERT(ARRAY_SIZE(radv_global_queue_priorities) <=
                          VK_MAX_GLOBAL_PRIORITY_SIZE_KHR);
            prop->priorityCount = ARRAY_SIZE(radv_global_queue_priorities);
            memcpy(&prop->priorities, radv_global_queue_priorities,
                   sizeof(radv_global_queue_priorities));
            break;
         }
         case VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR: {
            VkQueueFamilyQueryResultStatusPropertiesKHR *prop =
               (VkQueueFamilyQueryResultStatusPropertiesKHR *)ext;
            prop->queryResultStatusSupport = VK_FALSE;
            break;
         }
         case VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR: {
            VkQueueFamilyVideoPropertiesKHR *prop = (VkQueueFamilyVideoPropertiesKHR *)ext;
            if (pQueueFamilyProperties[i].queueFamilyProperties.queueFlags &
                VK_QUEUE_VIDEO_DECODE_BIT_KHR)
               prop->videoCodecOperations = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR |
                                            VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
            break;
         }
         default:
            break;
         }
      }
   }
}

static void
radv_get_memory_budget_properties(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceMemoryBudgetPropertiesEXT *memoryBudget)
{
   RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
   VkPhysicalDeviceMemoryProperties *memory_properties = &device->memory_properties;

   /* For all memory heaps, the computation of budget is as follow:
    *	heap_budget = heap_size - global_heap_usage + app_heap_usage
    *
    * The Vulkan spec 1.1.97 says that the budget should include any
    * currently allocated device memory.
    *
    * Note that the application heap usages are not really accurate (eg.
    * in presence of shared buffers).
    */
   if (!device->rad_info.has_dedicated_vram) {
      if (device->instance->enable_unified_heap_on_apu) {
         /* When the heaps are unified, only the visible VRAM heap is exposed on APUs. */
         assert(device->heaps == RADV_HEAP_VRAM_VIS);
         assert(device->memory_properties.memoryHeaps[0].flags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
         const uint8_t vram_vis_heap_idx = 0;

         /* Get the total heap size which is the visible VRAM heap size. */
         uint64_t total_heap_size = device->memory_properties.memoryHeaps[vram_vis_heap_idx].size;

         /* Get the different memory usages. */
         uint64_t vram_vis_internal_usage =
            device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM_VIS) +
            device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM);
         uint64_t gtt_internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_GTT);
         uint64_t total_internal_usage = vram_vis_internal_usage + gtt_internal_usage;
         uint64_t total_system_usage = device->ws->query_value(device->ws, RADEON_VRAM_VIS_USAGE) +
                                       device->ws->query_value(device->ws, RADEON_GTT_USAGE);
         uint64_t total_usage = MAX2(total_internal_usage, total_system_usage);

         /* Compute the total free space that can be allocated for this process accross all heaps. */
         uint64_t total_free_space = total_heap_size - MIN2(total_heap_size, total_usage);

         memoryBudget->heapBudget[vram_vis_heap_idx] = total_free_space + total_internal_usage;
         memoryBudget->heapUsage[vram_vis_heap_idx] = total_internal_usage;
      } else {
         /* On APUs, the driver exposes fake heaps to the application because usually the carveout
          * is too small for games but the budgets need to be redistributed accordingly.
          */
         assert(device->heaps == (RADV_HEAP_GTT | RADV_HEAP_VRAM_VIS));
         assert(device->memory_properties.memoryHeaps[0].flags == 0); /* GTT */
         assert(device->memory_properties.memoryHeaps[1].flags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT);
         const uint8_t gtt_heap_idx = 0, vram_vis_heap_idx = 1;

         /* Get the visible VRAM/GTT heap sizes and internal usages. */
         uint64_t gtt_heap_size = device->memory_properties.memoryHeaps[gtt_heap_idx].size;
         uint64_t vram_vis_heap_size =
            device->memory_properties.memoryHeaps[vram_vis_heap_idx].size;

         uint64_t vram_vis_internal_usage =
            device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM_VIS) +
            device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM);
         uint64_t gtt_internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_GTT);

         /* Compute the total heap size, internal and system usage. */
         uint64_t total_heap_size = vram_vis_heap_size + gtt_heap_size;
         uint64_t total_internal_usage = vram_vis_internal_usage + gtt_internal_usage;
         uint64_t total_system_usage = device->ws->query_value(device->ws, RADEON_VRAM_VIS_USAGE) +
                                       device->ws->query_value(device->ws, RADEON_GTT_USAGE);

         uint64_t total_usage = MAX2(total_internal_usage, total_system_usage);

         /* Compute the total free space that can be allocated for this process accross all heaps. */
         uint64_t total_free_space = total_heap_size - MIN2(total_heap_size, total_usage);

         /* Compute the remaining visible VRAM size for this process. */
         uint64_t vram_vis_free_space =
            vram_vis_heap_size - MIN2(vram_vis_heap_size, vram_vis_internal_usage);

         /* Distribute the total free space (2/3rd as VRAM and 1/3rd as GTT) to match the heap
          * sizes, and align down to the page size to be conservative.
          */
         vram_vis_free_space = ROUND_DOWN_TO(MIN2((total_free_space * 2) / 3, vram_vis_free_space),
                                             device->rad_info.gart_page_size);
         uint64_t gtt_free_space = total_free_space - vram_vis_free_space;

         memoryBudget->heapBudget[vram_vis_heap_idx] =
            vram_vis_free_space + vram_vis_internal_usage;
         memoryBudget->heapUsage[vram_vis_heap_idx] = vram_vis_internal_usage;
         memoryBudget->heapBudget[gtt_heap_idx] = gtt_free_space + gtt_internal_usage;
         memoryBudget->heapUsage[gtt_heap_idx] = gtt_internal_usage;
      }
   } else {
      unsigned mask = device->heaps;
      unsigned heap = 0;
      while (mask) {
         uint64_t internal_usage = 0, system_usage = 0;
         unsigned type = 1u << u_bit_scan(&mask);

         switch (type) {
         case RADV_HEAP_VRAM:
            internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM);
            system_usage = device->ws->query_value(device->ws, RADEON_VRAM_USAGE);
            break;
         case RADV_HEAP_VRAM_VIS:
            internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM_VIS);
            if (!(device->heaps & RADV_HEAP_VRAM))
               internal_usage += device->ws->query_value(device->ws, RADEON_ALLOCATED_VRAM);
            system_usage = device->ws->query_value(device->ws, RADEON_VRAM_VIS_USAGE);
            break;
         case RADV_HEAP_GTT:
            internal_usage = device->ws->query_value(device->ws, RADEON_ALLOCATED_GTT);
            system_usage = device->ws->query_value(device->ws, RADEON_GTT_USAGE);
            break;
         }

         uint64_t total_usage = MAX2(internal_usage, system_usage);

         uint64_t free_space = device->memory_properties.memoryHeaps[heap].size -
                               MIN2(device->memory_properties.memoryHeaps[heap].size, total_usage);
         memoryBudget->heapBudget[heap] = free_space + internal_usage;
         memoryBudget->heapUsage[heap] = internal_usage;
         ++heap;
      }

      assert(heap == memory_properties->memoryHeapCount);
   }

   /* The heapBudget and heapUsage values must be zero for array elements
    * greater than or equal to
    * VkPhysicalDeviceMemoryProperties::memoryHeapCount.
    */
   for (uint32_t i = memory_properties->memoryHeapCount; i < VK_MAX_MEMORY_HEAPS; i++) {
      memoryBudget->heapBudget[i] = 0;
      memoryBudget->heapUsage[i] = 0;
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                        VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);

   pMemoryProperties->memoryProperties = pdevice->memory_properties;

   VkPhysicalDeviceMemoryBudgetPropertiesEXT *memory_budget =
      vk_find_struct(pMemoryProperties->pNext, PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
   if (memory_budget)
      radv_get_memory_budget_properties(physicalDevice, memory_budget);
}

static const VkTimeDomainEXT radv_time_domains[] = {
   VK_TIME_DOMAIN_DEVICE_EXT,
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT,
#ifdef CLOCK_MONOTONIC_RAW
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_EXT,
#endif
};

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPhysicalDeviceCalibrateableTimeDomainsEXT(VkPhysicalDevice physicalDevice,
                                                  uint32_t *pTimeDomainCount,
                                                  VkTimeDomainEXT *pTimeDomains)
{
   int d;
   VK_OUTARRAY_MAKE_TYPED(VkTimeDomainEXT, out, pTimeDomains, pTimeDomainCount);

   for (d = 0; d < ARRAY_SIZE(radv_time_domains); d++) {
      vk_outarray_append_typed(VkTimeDomainEXT, &out, i)
      {
         *i = radv_time_domains[d];
      }
   }

   return vk_outarray_status(&out);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetPhysicalDeviceMultisamplePropertiesEXT(VkPhysicalDevice physicalDevice,
                                               VkSampleCountFlagBits samples,
                                               VkMultisamplePropertiesEXT *pMultisampleProperties)
{
   VkSampleCountFlagBits supported_samples =
      VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;

   if (samples & supported_samples) {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){2, 2};
   } else {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){0, 0};
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPhysicalDeviceFragmentShadingRatesKHR(
   VkPhysicalDevice physicalDevice, uint32_t *pFragmentShadingRateCount,
   VkPhysicalDeviceFragmentShadingRateKHR *pFragmentShadingRates)
{
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceFragmentShadingRateKHR, out, pFragmentShadingRates,
                          pFragmentShadingRateCount);

#define append_rate(w, h, s)                                                                       \
   {                                                                                               \
      VkPhysicalDeviceFragmentShadingRateKHR rate = {                                              \
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR,          \
         .sampleCounts = s,                                                                        \
         .fragmentSize = {.width = w, .height = h},                                                \
      };                                                                                           \
      vk_outarray_append_typed(VkPhysicalDeviceFragmentShadingRateKHR, &out, r) *r = rate;         \
   }

   for (uint32_t x = 2; x >= 1; x--) {
      for (uint32_t y = 2; y >= 1; y--) {
         VkSampleCountFlagBits samples;

         if (x == 1 && y == 1) {
            samples = ~0;
         } else {
            samples = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT |
                      VK_SAMPLE_COUNT_8_BIT;
         }

         append_rate(x, y, samples);
      }
   }
#undef append_rate

   return vk_outarray_status(&out);
}
