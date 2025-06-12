/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_api_version.h"
#include "anv_measure.h"

#include "i915/anv_device.h"
#include "xe/anv_device.h"

#include "common/intel_common.h"
#include "common/intel_uuid.h"

#include "perf/intel_perf.h"

#include "git_sha1.h"

#include "util/disk_cache.h"
#include "util/mesa-sha1.h"

#include <xf86drm.h>
#include <fcntl.h>
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif

/* This is probably far to big but it reflects the max size used for messages
 * in OpenGLs KHR_debug.
 */
#define MAX_DEBUG_MESSAGE_LENGTH    4096

static void
compiler_debug_log(void *data, UNUSED unsigned *id, const char *fmt, ...)
{
   char str[MAX_DEBUG_MESSAGE_LENGTH];
   struct anv_device *device = (struct anv_device *)data;
   UNUSED struct anv_instance *instance = device->physical->instance;

   va_list args;
   va_start(args, fmt);
   (void) vsnprintf(str, MAX_DEBUG_MESSAGE_LENGTH, fmt, args);
   va_end(args);

   //vk_logd(VK_LOG_NO_OBJS(&instance->vk), "%s", str);
}

static void
compiler_perf_log(UNUSED void *data, UNUSED unsigned *id, const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);

   if (INTEL_DEBUG(DEBUG_PERF))
      mesa_logd_v(fmt, args);

   va_end(args);
}

struct anv_descriptor_limits {
   uint32_t max_ubos;
   uint32_t max_ssbos;
   uint32_t max_samplers;
   uint32_t max_images;
   uint32_t max_resources;
};

static void
get_device_descriptor_limits(const struct anv_physical_device *device,
                             struct anv_descriptor_limits *limits)
{
   memset(limits, 0, sizeof(*limits));

   /* It's a bit hard to exactly map our implementation to the limits
    * described by Vulkan. The bindless surface handle in the extended message
    * descriptors is 20 bits on <= Gfx12.0, 26 bits on >= Gfx12.5 and it's an
    * index into the table of RENDER_SURFACE_STATE structs that starts at
    * bindless surface base address. On <= Gfx12.0, this means that we can
    * have at must 1M surface states allocated at any given time. Since most
    * image views take two descriptors, this means we have a limit of about
    * 500K image views. On >= Gfx12.5, we do not need 2 surfaces per
    * descriptors and we have 33M+ descriptors (we have a 2GB limit, due to
    * overlapping heaps for workarounds, but HW can do 4GB).
    *
    * However, on <= Gfx12.0, since we allocate surface states at
    * vkCreateImageView time, this means our limit is actually something on
    * the order of 500K image views allocated at any time. The actual limit
    * describe by Vulkan, on the other hand, is a limit of how many you can
    * have in a descriptor set. Assuming anyone using 1M descriptors will be
    * using the same image view twice a bunch of times (or a bunch of null
    * descriptors), we can safely advertise a larger limit here.
    *
    * Here we use the size of the heap in which the descriptors are stored and
    * divide by the size of the descriptor to get a limit value.
    */
   const uint64_t descriptor_heap_size =
      device->indirect_descriptors ?
      device->va.indirect_descriptor_pool.size :
      device->va.bindless_surface_state_pool.size;;

   const uint32_t buffer_descriptor_size =
      device->indirect_descriptors ?
      sizeof(struct anv_address_range_descriptor) :
      ANV_SURFACE_STATE_SIZE;
   const uint32_t image_descriptor_size =
      device->indirect_descriptors ?
      sizeof(struct anv_address_range_descriptor) :
      ANV_SURFACE_STATE_SIZE;
   const uint32_t sampler_descriptor_size =
      device->indirect_descriptors ?
      sizeof(struct anv_sampled_image_descriptor) :
      ANV_SAMPLER_STATE_SIZE;

   limits->max_ubos = descriptor_heap_size / buffer_descriptor_size;
   limits->max_ssbos = descriptor_heap_size / buffer_descriptor_size;
   limits->max_images = descriptor_heap_size / image_descriptor_size;
   limits->max_samplers = descriptor_heap_size / sampler_descriptor_size;

   limits->max_resources = UINT32_MAX;
   limits->max_resources = MIN2(limits->max_resources, limits->max_ubos);
   limits->max_resources = MIN2(limits->max_resources, limits->max_ssbos);
   limits->max_resources = MIN2(limits->max_resources, limits->max_images);
   limits->max_resources = MIN2(limits->max_resources, limits->max_samplers);
}

static void
get_device_extensions(const struct anv_physical_device *device,
                      struct vk_device_extension_table *ext)
{
   const bool has_syncobj_wait =
      (device->sync_syncobj_type.features & VK_SYNC_FEATURE_CPU_WAIT) != 0;

   const bool rt_enabled = ANV_SUPPORT_RT && device->info.has_ray_tracing;

   *ext = (struct vk_device_extension_table) {
      .KHR_8bit_storage                      = true,
      .KHR_16bit_storage                     = !device->instance->no_16bit,
      .KHR_acceleration_structure            = rt_enabled,
      .KHR_bind_memory2                      = true,
      .KHR_buffer_device_address             = true,
      .KHR_calibrated_timestamps             = device->has_reg_timestamp,
      .KHR_compute_shader_derivatives        = true,
      .KHR_cooperative_matrix                = anv_has_cooperative_matrix(device),
      .KHR_copy_commands2                    = true,
      .KHR_create_renderpass2                = true,
      .KHR_dedicated_allocation              = true,
      .KHR_deferred_host_operations          = true,
      .KHR_depth_stencil_resolve             = true,
      .KHR_descriptor_update_template        = true,
      .KHR_device_group                      = true,
      .KHR_draw_indirect_count               = true,
      .KHR_driver_properties                 = true,
      .KHR_dynamic_rendering                 = true,
      .KHR_dynamic_rendering_local_read      = true,
      .KHR_external_fence                    = has_syncobj_wait,
      .KHR_external_fence_fd                 = has_syncobj_wait,
      .KHR_external_memory                   = true,
      .KHR_external_memory_fd                = true,
      .KHR_external_semaphore                = true,
      .KHR_external_semaphore_fd             = true,
      .KHR_format_feature_flags2             = true,
      .KHR_fragment_shading_rate             = device->info.ver >= 11,
      .KHR_get_memory_requirements2          = true,
      .KHR_global_priority                   = device->max_context_priority >=
                                               VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
      .KHR_image_format_list                 = true,
      .KHR_imageless_framebuffer             = true,
#ifdef ANV_USE_WSI_PLATFORM
      .KHR_incremental_present               = true,
#endif
      .KHR_index_type_uint8                  = true,
      .KHR_line_rasterization                = true,
      .KHR_load_store_op_none                = true,
      .KHR_maintenance1                      = true,
      .KHR_maintenance2                      = true,
      .KHR_maintenance3                      = true,
      .KHR_maintenance4                      = true,
      .KHR_maintenance5                      = true,
      .KHR_maintenance6                      = true,
      .KHR_maintenance7                      = true,
      .KHR_map_memory2                       = true,
      .KHR_multiview                         = true,
      .KHR_performance_query =
         device->perf &&
         (intel_perf_has_hold_preemption(device->perf) ||
          INTEL_DEBUG(DEBUG_NO_OACONFIG)) &&
         device->use_call_secondary,
      .KHR_pipeline_executable_properties    = true,
      .KHR_pipeline_library                  = true,
      /* Hide these behind dri configs for now since we cannot implement it reliably on
       * all surfaces yet. There is no surface capability query for present wait/id,
       * but the feature is useful enough to hide behind an opt-in mechanism for now.
       * If the instance only enables surface extensions that unconditionally support present wait,
       * we can also expose the extension that way. */
      .KHR_present_id =
         driQueryOptionb(&device->instance->dri_options, "vk_khr_present_wait") ||
         wsi_common_vk_instance_supports_present_wait(&device->instance->vk),
      .KHR_present_wait =
         driQueryOptionb(&device->instance->dri_options, "vk_khr_present_wait") ||
         wsi_common_vk_instance_supports_present_wait(&device->instance->vk),
      .KHR_push_descriptor                   = true,
      .KHR_ray_query                         = rt_enabled,
      .KHR_ray_tracing_maintenance1          = rt_enabled,
      .KHR_ray_tracing_pipeline              = rt_enabled,
      .KHR_ray_tracing_position_fetch        = rt_enabled,
      .KHR_relaxed_block_layout              = true,
      .KHR_sampler_mirror_clamp_to_edge      = true,
      .KHR_sampler_ycbcr_conversion          = true,
      .KHR_separate_depth_stencil_layouts    = true,
      .KHR_shader_atomic_int64               = true,
      .KHR_shader_clock                      = true,
      .KHR_shader_draw_parameters            = true,
      .KHR_shader_expect_assume              = true,
      .KHR_shader_float16_int8               = !device->instance->no_16bit,
      .KHR_shader_float_controls             = true,
      .KHR_shader_float_controls2            = true,
      .KHR_shader_integer_dot_product        = true,
      .KHR_shader_maximal_reconvergence      = true,
      .KHR_shader_non_semantic_info          = true,
      .KHR_shader_quad_control               = true,
      .KHR_shader_relaxed_extended_instruction = true,
      .KHR_shader_subgroup_extended_types    = true,
      .KHR_shader_subgroup_rotate            = true,
      .KHR_shader_subgroup_uniform_control_flow = true,
      .KHR_shader_terminate_invocation       = true,
      .KHR_spirv_1_4                         = true,
      .KHR_storage_buffer_storage_class      = true,
#ifdef ANV_USE_WSI_PLATFORM
      .KHR_swapchain                         = true,
      .KHR_swapchain_mutable_format          = true,
#endif
      .KHR_synchronization2                  = true,
      .KHR_timeline_semaphore                = true,
      .KHR_uniform_buffer_standard_layout    = true,
      .KHR_variable_pointers                 = true,
      .KHR_vertex_attribute_divisor          = true,
      .KHR_video_queue                       = device->video_decode_enabled || device->video_encode_enabled,
      .KHR_video_decode_queue                = device->video_decode_enabled,
      .KHR_video_decode_h264                 = VIDEO_CODEC_H264DEC && device->video_decode_enabled,
      .KHR_video_decode_h265                 = VIDEO_CODEC_H265DEC && device->video_decode_enabled,
      .KHR_video_decode_av1                  = device->info.ver >= 12 && VIDEO_CODEC_AV1DEC && device->video_decode_enabled,
      .KHR_video_encode_queue                = device->video_encode_enabled,
      .KHR_video_encode_h264                 = VIDEO_CODEC_H264ENC && device->video_encode_enabled,
      .KHR_video_encode_h265                 = device->info.ver >= 12 && VIDEO_CODEC_H265ENC && device->video_encode_enabled,
      .KHR_video_maintenance1                = (device->video_decode_enabled &&
                                               (VIDEO_CODEC_H264DEC || VIDEO_CODEC_H265DEC)) ||
                                               (device->video_encode_enabled &&
                                               (VIDEO_CODEC_H264ENC || VIDEO_CODEC_H265ENC)),
      .KHR_vulkan_memory_model               = true,
      .KHR_workgroup_memory_explicit_layout  = true,
      .KHR_zero_initialize_workgroup_memory  = true,
      .EXT_4444_formats                      = true,
      .EXT_attachment_feedback_loop_layout   = true,
      .EXT_attachment_feedback_loop_dynamic_state = true,
      .EXT_border_color_swizzle              = true,
      .EXT_buffer_device_address             = true,
      .EXT_calibrated_timestamps             = device->has_reg_timestamp,
      .EXT_color_write_enable                = true,
      .EXT_conditional_rendering             = true,
      .EXT_conservative_rasterization        = true,
      .EXT_custom_border_color               = true,
      .EXT_depth_bias_control                = true,
      .EXT_depth_clamp_control               = true,
      .EXT_depth_clamp_zero_one              = true,
      .EXT_depth_clip_control                = true,
      .EXT_depth_clip_enable                 = true,
      .EXT_depth_range_unrestricted          = device->info.ver >= 20,
      .EXT_descriptor_buffer                 = true,
      .EXT_descriptor_indexing               = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
      .EXT_display_control                   = true,
#endif
      .EXT_dynamic_rendering_unused_attachments = true,
      .EXT_extended_dynamic_state            = true,
      .EXT_extended_dynamic_state2           = true,
      .EXT_extended_dynamic_state3           = true,
      .EXT_external_memory_dma_buf           = true,
      .EXT_external_memory_host              = true,
      .EXT_fragment_shader_interlock         = true,
      .EXT_global_priority                   = device->max_context_priority >=
                                               VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
      .EXT_global_priority_query             = device->max_context_priority >=
                                               VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
      .EXT_graphics_pipeline_library         = !debug_get_bool_option("ANV_NO_GPL", false),
      .EXT_host_image_copy                   = !device->emu_astc_ldr,
      .EXT_host_query_reset                  = true,
      .EXT_image_2d_view_of_3d               = true,
      /* Because of Xe2 PAT selected compression and the Vulkan spec
       * requirement to always return the same memory types for Images with
       * same properties we can't support EXT_image_compression_control on Xe2+
       */
      .EXT_image_compression_control         = device->instance->compression_control_enabled &&
                                               device->info.ver < 20,
      .EXT_image_drm_format_modifier         = true,
      .EXT_image_robustness                  = true,
      .EXT_image_sliced_view_of_3d           = true,
      .EXT_image_view_min_lod                = true,
      .EXT_index_type_uint8                  = true,
      .EXT_inline_uniform_block              = true,
      .EXT_legacy_dithering                  = true,
      .EXT_legacy_vertex_attributes          = true,
      .EXT_line_rasterization                = true,
      .EXT_load_store_op_none                = true,
      .EXT_map_memory_placed                 = device->info.has_mmap_offset,
      /* Enable the extension only if we have support on both the local &
       * system memory
       */
      .EXT_memory_budget                     = (!device->info.has_local_mem ||
                                                device->vram_mappable.available > 0) &&
                                               device->sys.available,
      .EXT_mesh_shader                       = device->info.has_mesh_shading,
      .EXT_multi_draw                        = true,
      .EXT_mutable_descriptor_type           = true,
      .EXT_nested_command_buffer             = true,
      .EXT_non_seamless_cube_map             = true,
      .EXT_pci_bus_info                      = true,
      .EXT_physical_device_drm               = true,
      .EXT_pipeline_creation_cache_control   = true,
      .EXT_pipeline_creation_feedback        = true,
      .EXT_pipeline_library_group_handles    = rt_enabled,
      .EXT_pipeline_protected_access         = device->has_protected_contexts,
      .EXT_pipeline_robustness               = true,
      .EXT_post_depth_coverage               = true,
      .EXT_primitive_topology_list_restart   = true,
      .EXT_primitives_generated_query        = true,
      .EXT_private_data                      = true,
      .EXT_provoking_vertex                  = true,
      .EXT_queue_family_foreign              = true,
      .EXT_robustness2                       = true,
      .EXT_sample_locations                  = true,
      .EXT_sampler_filter_minmax             = true,
      .EXT_scalar_block_layout               = true,
      .EXT_separate_stencil_usage            = true,
      .EXT_shader_atomic_float               = true,
      .EXT_shader_atomic_float2              = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_module_identifier          = true,
      .EXT_shader_replicated_composites      = true,
      .EXT_shader_stencil_export             = true,
      .EXT_shader_subgroup_ballot            = true,
      .EXT_shader_subgroup_vote              = true,
      .EXT_shader_viewport_index_layer       = true,
      .EXT_subgroup_size_control             = true,
#ifdef ANV_USE_WSI_PLATFORM
      .EXT_swapchain_maintenance1            = true,
#endif
      .EXT_texel_buffer_alignment            = true,
      .EXT_tooling_info                      = true,
      .EXT_transform_feedback                = true,
      .EXT_vertex_attribute_divisor          = true,
      .EXT_vertex_input_dynamic_state        = true,
      .EXT_ycbcr_2plane_444_formats          = true,
      .EXT_ycbcr_image_arrays                = true,
      .AMD_buffer_marker                     = true,
      .AMD_texture_gather_bias_lod           = device->info.ver >= 20,
#if DETECT_OS_ANDROID
      .ANDROID_external_memory_android_hardware_buffer = true,
      .ANDROID_native_buffer                 = true,
#endif
      .GOOGLE_decorate_string                = true,
      .GOOGLE_hlsl_functionality1            = true,
      .GOOGLE_user_type                      = true,
      .INTEL_performance_query               = device->perf &&
                                               intel_perf_has_hold_preemption(device->perf),
      .INTEL_shader_integer_functions2       = true,
      .MESA_image_alignment_control          = true,
      .NV_compute_shader_derivatives         = true,
      .VALVE_mutable_descriptor_type         = true,
   };
}

static void
get_features(const struct anv_physical_device *pdevice,
             struct vk_features *features)
{
   struct vk_app_info *app_info = &pdevice->instance->vk.app_info;

   const bool rt_enabled = ANV_SUPPORT_RT && pdevice->info.has_ray_tracing;

   const bool mesh_shader =
      pdevice->vk.supported_extensions.EXT_mesh_shader;

   const bool has_sparse_or_fake = pdevice->sparse_type != ANV_SPARSE_TYPE_NOT_SUPPORTED;

   *features = (struct vk_features) {
      /* Vulkan 1.0 */
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
      .depthBounds                              = pdevice->info.ver >= 12,
      .wideLines                                = true,
      .largePoints                              = true,
      .alphaToOne                               = true,
      .multiViewport                            = true,
      .samplerAnisotropy                        = true,
      .textureCompressionETC2                   = true,
      .textureCompressionASTC_LDR               = pdevice->has_astc_ldr ||
                                                  pdevice->emu_astc_ldr,
      .textureCompressionBC                     = true,
      .occlusionQueryPrecise                    = true,
      .pipelineStatisticsQuery                  = true,
      .vertexPipelineStoresAndAtomics           = true,
      .fragmentStoresAndAtomics                 = true,
      .shaderTessellationAndGeometryPointSize   = true,
      .shaderImageGatherExtended                = true,
      .shaderStorageImageExtendedFormats        = true,
      .shaderStorageImageMultisample            = false,
      /* Gfx12.5 has all the required format supported in HW for typed
       * read/writes
       */
      .shaderStorageImageReadWithoutFormat      = pdevice->info.verx10 >= 125,
      .shaderStorageImageWriteWithoutFormat     = true,
      .shaderUniformBufferArrayDynamicIndexing  = true,
      .shaderSampledImageArrayDynamicIndexing   = true,
      .shaderStorageBufferArrayDynamicIndexing  = true,
      .shaderStorageImageArrayDynamicIndexing   = true,
      .shaderClipDistance                       = true,
      .shaderCullDistance                       = true,
      .shaderFloat64                            = pdevice->info.has_64bit_float ||
                                                  pdevice->instance->fp64_workaround_enabled,
      .shaderInt64                              = true,
      .shaderInt16                              = true,
      .shaderResourceMinLod                     = true,
      .shaderResourceResidency                  = has_sparse_or_fake,
      .sparseBinding                            = has_sparse_or_fake,
      .sparseResidencyAliased                   = has_sparse_or_fake,
      .sparseResidencyBuffer                    = has_sparse_or_fake,
      .sparseResidencyImage2D                   = has_sparse_or_fake,
      .sparseResidencyImage3D                   = has_sparse_or_fake,
      .sparseResidency2Samples                  = has_sparse_or_fake,
      .sparseResidency4Samples                  = has_sparse_or_fake,
      .sparseResidency8Samples                  = has_sparse_or_fake &&
                                                  pdevice->info.verx10 != 125,
      .sparseResidency16Samples                 = has_sparse_or_fake &&
                                                  pdevice->info.verx10 != 125,
      .variableMultisampleRate                  = true,
      .inheritedQueries                         = true,

      /* Vulkan 1.1 */
      .storageBuffer16BitAccess            = !pdevice->instance->no_16bit,
      .uniformAndStorageBuffer16BitAccess  = !pdevice->instance->no_16bit,
      .storagePushConstant16               = true,
      .storageInputOutput16                = false,
      .multiview                           = true,
      .multiviewGeometryShader             = true,
      .multiviewTessellationShader         = true,
      .variablePointersStorageBuffer       = true,
      .variablePointers                    = true,
      .protectedMemory                     = pdevice->has_protected_contexts,
      .samplerYcbcrConversion              = true,
      .shaderDrawParameters                = true,

      /* Vulkan 1.2 */
      .samplerMirrorClampToEdge            = true,
      .drawIndirectCount                   = true,
      .storageBuffer8BitAccess             = true,
      .uniformAndStorageBuffer8BitAccess   = true,
      .storagePushConstant8                = true,
      .shaderBufferInt64Atomics            = true,
      .shaderSharedInt64Atomics            = false,
      .shaderFloat16                       = !pdevice->instance->no_16bit,
      .shaderInt8                          = !pdevice->instance->no_16bit,

      .descriptorIndexing                                 = true,
      .shaderInputAttachmentArrayDynamicIndexing          = false,
      .shaderUniformTexelBufferArrayDynamicIndexing       = true,
      .shaderStorageTexelBufferArrayDynamicIndexing       = true,
      .shaderUniformBufferArrayNonUniformIndexing         = true,
      .shaderSampledImageArrayNonUniformIndexing          = true,
      .shaderStorageBufferArrayNonUniformIndexing         = true,
      .shaderStorageImageArrayNonUniformIndexing          = true,
      .shaderInputAttachmentArrayNonUniformIndexing       = false,
      .shaderUniformTexelBufferArrayNonUniformIndexing    = true,
      .shaderStorageTexelBufferArrayNonUniformIndexing    = true,
      .descriptorBindingUniformBufferUpdateAfterBind      = true,
      .descriptorBindingSampledImageUpdateAfterBind       = true,
      .descriptorBindingStorageImageUpdateAfterBind       = true,
      .descriptorBindingStorageBufferUpdateAfterBind      = true,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = true,
      .descriptorBindingStorageTexelBufferUpdateAfterBind = true,
      .descriptorBindingUpdateUnusedWhilePending          = true,
      .descriptorBindingPartiallyBound                    = true,
      .descriptorBindingVariableDescriptorCount           = true,
      .runtimeDescriptorArray                             = true,

      .samplerFilterMinmax                 = true,
      .scalarBlockLayout                   = true,
      .imagelessFramebuffer                = true,
      .uniformBufferStandardLayout         = true,
      .shaderSubgroupExtendedTypes         = true,
      .separateDepthStencilLayouts         = true,
      .hostQueryReset                      = true,
      .timelineSemaphore                   = true,
      .bufferDeviceAddress                 = true,
      .bufferDeviceAddressCaptureReplay    = true,
      .bufferDeviceAddressMultiDevice      = false,
      .vulkanMemoryModel                   = true,
      .vulkanMemoryModelDeviceScope        = true,
      .vulkanMemoryModelAvailabilityVisibilityChains = true,
      .shaderOutputViewportIndex           = true,
      .shaderOutputLayer                   = true,
      .subgroupBroadcastDynamicId          = true,

      /* Vulkan 1.3 */
      .robustImageAccess = true,
      .inlineUniformBlock = true,
      .descriptorBindingInlineUniformBlockUpdateAfterBind = true,
      .pipelineCreationCacheControl = true,
      .privateData = true,
      .shaderDemoteToHelperInvocation = true,
      .shaderTerminateInvocation = true,
      .subgroupSizeControl = true,
      .computeFullSubgroups = true,
      .synchronization2 = true,
      .textureCompressionASTC_HDR = false,
      .shaderZeroInitializeWorkgroupMemory = true,
      .dynamicRendering = true,
      .shaderIntegerDotProduct = true,
      .maintenance4 = true,

      /* Vulkan 1.4 */
      .pushDescriptor = true,

      /* VK_EXT_4444_formats */
      .formatA4R4G4B4 = true,
      .formatA4B4G4R4 = false,

      /* VK_KHR_acceleration_structure */
      .accelerationStructure = rt_enabled,
      .accelerationStructureCaptureReplay = false, /* TODO */
      .accelerationStructureIndirectBuild = false, /* TODO */
      .accelerationStructureHostCommands = false,
      .descriptorBindingAccelerationStructureUpdateAfterBind = rt_enabled,

      /* VK_EXT_border_color_swizzle */
      .borderColorSwizzle = true,
      .borderColorSwizzleFromImage = true,

      /* VK_EXT_color_write_enable */
      .colorWriteEnable = true,

      /* VK_EXT_image_2d_view_of_3d  */
      .image2DViewOf3D = true,
      .sampler2DViewOf3D = true,

      /* VK_EXT_image_sliced_view_of_3d */
      .imageSlicedViewOf3D = true,

      /* VK_KHR_compute_shader_derivatives */
      .computeDerivativeGroupQuads = true,
      .computeDerivativeGroupLinear = true,

      /* VK_EXT_conditional_rendering */
      .conditionalRendering = true,
      .inheritedConditionalRendering = true,

      /* VK_EXT_custom_border_color */
      .customBorderColors = true,
      .customBorderColorWithoutFormat =
         pdevice->instance->custom_border_colors_without_format,

      /* VK_EXT_depth_clamp_zero_one */
      .depthClampZeroOne = true,

      /* VK_EXT_depth_clip_enable */
      .depthClipEnable = true,

      /* VK_EXT_fragment_shader_interlock */
      .fragmentShaderSampleInterlock = true,
      .fragmentShaderPixelInterlock = true,
      .fragmentShaderShadingRateInterlock = false,

      /* VK_EXT_global_priority_query */
      .globalPriorityQuery = true,

      /* VK_EXT_graphics_pipeline_library */
      .graphicsPipelineLibrary =
         pdevice->vk.supported_extensions.EXT_graphics_pipeline_library,

      /* VK_KHR_fragment_shading_rate */
      .pipelineFragmentShadingRate = true,
      .primitiveFragmentShadingRate =
         pdevice->info.has_coarse_pixel_primitive_and_cb,
      .attachmentFragmentShadingRate =
         pdevice->info.has_coarse_pixel_primitive_and_cb,

      /* VK_EXT_image_view_min_lod */
      .minLod = true,

      /* VK_EXT_index_type_uint8 */
      .indexTypeUint8 = true,

      /* VK_EXT_line_rasterization */
      /* Rectangular lines must use the strict algorithm, which is not
       * supported for wide lines prior to ICL.  See rasterization_mode for
       * details and how the HW states are programmed.
       */
      .rectangularLines = pdevice->info.ver >= 10,
      .bresenhamLines = true,
      /* Support for Smooth lines with MSAA was removed on gfx11.  From the
       * BSpec section "Multisample ModesState" table for "AA Line Support
       * Requirements":
       *
       *    GFX10:BUG:######## 	NUM_MULTISAMPLES == 1
       *
       * Fortunately, this isn't a case most people care about.
       */
      .smoothLines = pdevice->info.ver < 10,
      .stippledRectangularLines = false,
      .stippledBresenhamLines = true,
      .stippledSmoothLines = false,

      /* VK_NV_mesh_shader */
      .taskShaderNV = false,
      .meshShaderNV = false,

      /* VK_EXT_mesh_shader */
      .taskShader = mesh_shader,
      .meshShader = mesh_shader,
      .multiviewMeshShader = false,
      .primitiveFragmentShadingRateMeshShader = mesh_shader,
      .meshShaderQueries = mesh_shader,

      /* VK_EXT_mutable_descriptor_type */
      .mutableDescriptorType = true,

      /* VK_KHR_performance_query */
      .performanceCounterQueryPools = true,
      /* HW only supports a single configuration at a time. */
      .performanceCounterMultipleQueryPools = false,

      /* VK_KHR_pipeline_executable_properties */
      .pipelineExecutableInfo = true,

      /* VK_EXT_primitives_generated_query */
      .primitivesGeneratedQuery = true,
      .primitivesGeneratedQueryWithRasterizerDiscard = false,
      .primitivesGeneratedQueryWithNonZeroStreams = false,

      /* VK_EXT_pipeline_library_group_handles */
      .pipelineLibraryGroupHandles = true,

      /* VK_EXT_provoking_vertex */
      .provokingVertexLast = true,
      .transformFeedbackPreservesProvokingVertex = true,

      /* VK_KHR_ray_query */
      .rayQuery = rt_enabled,

      /* VK_KHR_ray_tracing_maintenance1 */
      .rayTracingMaintenance1 = rt_enabled,
      .rayTracingPipelineTraceRaysIndirect2 = rt_enabled,

      /* VK_KHR_ray_tracing_pipeline */
      .rayTracingPipeline = rt_enabled,
      .rayTracingPipelineShaderGroupHandleCaptureReplay = false,
      .rayTracingPipelineShaderGroupHandleCaptureReplayMixed = false,
      .rayTracingPipelineTraceRaysIndirect = rt_enabled,
      .rayTraversalPrimitiveCulling = rt_enabled,

      /* VK_EXT_robustness2 */
      .robustBufferAccess2 = true,
      .robustImageAccess2 = true,
      .nullDescriptor = true,

      /* VK_EXT_shader_replicated_composites */
      .shaderReplicatedComposites = true,

      /* VK_EXT_shader_atomic_float */
      .shaderBufferFloat32Atomics =    true,
      .shaderBufferFloat32AtomicAdd =  pdevice->info.has_lsc,
      .shaderBufferFloat64Atomics =
         pdevice->info.has_64bit_float && pdevice->info.has_lsc,
      .shaderBufferFloat64AtomicAdd =  pdevice->info.ver >= 20,
      .shaderSharedFloat32Atomics =    true,
      .shaderSharedFloat32AtomicAdd =  false,
      .shaderSharedFloat64Atomics =    false,
      .shaderSharedFloat64AtomicAdd =  false,
      .shaderImageFloat32Atomics =     true,
      .shaderImageFloat32AtomicAdd =   pdevice->info.ver >= 20,
      .sparseImageFloat32Atomics =     false,
      .sparseImageFloat32AtomicAdd =   false,

      /* VK_EXT_shader_atomic_float2 */
      .shaderBufferFloat16Atomics      = pdevice->info.has_lsc,
      .shaderBufferFloat16AtomicAdd    = false,
      .shaderBufferFloat16AtomicMinMax = pdevice->info.has_lsc,
      .shaderBufferFloat32AtomicMinMax = true,
      .shaderBufferFloat64AtomicMinMax =
         pdevice->info.has_64bit_float && pdevice->info.has_lsc &&
         pdevice->info.ver < 20,
      .shaderSharedFloat16Atomics      = pdevice->info.has_lsc,
      .shaderSharedFloat16AtomicAdd    = false,
      .shaderSharedFloat16AtomicMinMax = pdevice->info.has_lsc,
      .shaderSharedFloat32AtomicMinMax = true,
      .shaderSharedFloat64AtomicMinMax = false,
      .shaderImageFloat32AtomicMinMax  = false,
      .sparseImageFloat32AtomicMinMax  = false,

      /* VK_KHR_shader_clock */
      .shaderSubgroupClock = true,
      .shaderDeviceClock = false,

      /* VK_INTEL_shader_integer_functions2 */
      .shaderIntegerFunctions2 = true,

      /* VK_EXT_shader_module_identifier */
      .shaderModuleIdentifier = true,

      /* VK_KHR_shader_subgroup_uniform_control_flow */
      .shaderSubgroupUniformControlFlow = true,

      /* VK_EXT_texel_buffer_alignment */
      .texelBufferAlignment = true,

      /* VK_EXT_transform_feedback */
      .transformFeedback = true,
      .geometryStreams = true,

      /* VK_KHR_vertex_attribute_divisor */
      .vertexAttributeInstanceRateDivisor = true,
      .vertexAttributeInstanceRateZeroDivisor = true,

      /* VK_KHR_workgroup_memory_explicit_layout */
      .workgroupMemoryExplicitLayout = true,
      .workgroupMemoryExplicitLayoutScalarBlockLayout = true,
      .workgroupMemoryExplicitLayout8BitAccess = true,
      .workgroupMemoryExplicitLayout16BitAccess = true,

      /* VK_EXT_ycbcr_image_arrays */
      .ycbcrImageArrays = true,

      /* VK_EXT_ycbcr_2plane_444_formats */
      .ycbcr2plane444Formats = true,

      /* VK_EXT_extended_dynamic_state */
      .extendedDynamicState = true,

      /* VK_EXT_extended_dynamic_state2 */
      .extendedDynamicState2 = true,
      .extendedDynamicState2LogicOp = true,
      .extendedDynamicState2PatchControlPoints = true,

      /* VK_EXT_extended_dynamic_state3 */
      .extendedDynamicState3PolygonMode = true,
      .extendedDynamicState3TessellationDomainOrigin = true,
      .extendedDynamicState3RasterizationStream = true,
      .extendedDynamicState3LineStippleEnable = true,
      .extendedDynamicState3LineRasterizationMode = true,
      .extendedDynamicState3LogicOpEnable = true,
      .extendedDynamicState3AlphaToOneEnable = true,
      .extendedDynamicState3DepthClipEnable = true,
      .extendedDynamicState3DepthClampEnable = true,
      .extendedDynamicState3DepthClipNegativeOneToOne = true,
      .extendedDynamicState3ProvokingVertexMode = true,
      .extendedDynamicState3ColorBlendEnable = true,
      .extendedDynamicState3ColorWriteMask = true,
      .extendedDynamicState3ColorBlendEquation = true,
      .extendedDynamicState3SampleLocationsEnable = true,
      .extendedDynamicState3SampleMask = true,
      .extendedDynamicState3ConservativeRasterizationMode = true,
      .extendedDynamicState3AlphaToCoverageEnable = true,
      .extendedDynamicState3RasterizationSamples = true,

      .extendedDynamicState3ExtraPrimitiveOverestimationSize = false,
      .extendedDynamicState3ViewportWScalingEnable = false,
      .extendedDynamicState3ViewportSwizzle = false,
      .extendedDynamicState3ShadingRateImageEnable = false,
      .extendedDynamicState3CoverageToColorEnable = false,
      .extendedDynamicState3CoverageToColorLocation = false,
      .extendedDynamicState3CoverageModulationMode = false,
      .extendedDynamicState3CoverageModulationTableEnable = false,
      .extendedDynamicState3CoverageModulationTable = false,
      .extendedDynamicState3CoverageReductionMode = false,
      .extendedDynamicState3RepresentativeFragmentTestEnable = false,
      .extendedDynamicState3ColorBlendAdvanced = false,

      /* VK_EXT_multi_draw */
      .multiDraw = true,

      /* VK_EXT_non_seamless_cube_map */
      .nonSeamlessCubeMap = true,

      /* VK_EXT_primitive_topology_list_restart */
      .primitiveTopologyListRestart = true,
      .primitiveTopologyPatchListRestart = true,

      /* VK_EXT_depth_clamp_control */
      .depthClampControl = true,

      /* VK_EXT_depth_clip_control */
      .depthClipControl = true,

      /* VK_KHR_present_id */
      .presentId = pdevice->vk.supported_extensions.KHR_present_id,

      /* VK_KHR_present_wait */
      .presentWait = pdevice->vk.supported_extensions.KHR_present_wait,

      /* VK_EXT_vertex_input_dynamic_state */
      .vertexInputDynamicState = true,

      /* VK_KHR_ray_tracing_position_fetch */
      .rayTracingPositionFetch = rt_enabled,

      /* VK_EXT_dynamic_rendering_unused_attachments */
      .dynamicRenderingUnusedAttachments = true,

      /* VK_EXT_depth_bias_control */
      .depthBiasControl = true,
      .floatRepresentation = true,
      .leastRepresentableValueForceUnormRepresentation = false,
      .depthBiasExact = true,

      /* VK_EXT_pipeline_robustness */
      .pipelineRobustness = true,

      /* VK_KHR_maintenance5 */
      .maintenance5 = true,

      /* VK_KHR_maintenance6 */
      .maintenance6 = true,

      /* VK_EXT_nested_command_buffer */
      .nestedCommandBuffer = true,
      .nestedCommandBufferRendering = true,
      .nestedCommandBufferSimultaneousUse = false,

      /* VK_KHR_cooperative_matrix */
      .cooperativeMatrix = anv_has_cooperative_matrix(pdevice),

      /* VK_KHR_shader_maximal_reconvergence */
      .shaderMaximalReconvergence = true,

      /* VK_KHR_shader_subgroup_rotate */
      .shaderSubgroupRotate = true,
      .shaderSubgroupRotateClustered = true,

      /* VK_EXT_attachment_feedback_loop_layout */
      .attachmentFeedbackLoopLayout = true,

      /* VK_EXT_attachment_feedback_loop_dynamic_state */
      .attachmentFeedbackLoopDynamicState = true,

      /* VK_KHR_shader_expect_assume */
      .shaderExpectAssume = true,

      /* VK_EXT_descriptor_buffer */
      .descriptorBuffer = true,
      .descriptorBufferCaptureReplay = true,
      .descriptorBufferImageLayoutIgnored = false,
      .descriptorBufferPushDescriptors = true,

      /* VK_EXT_map_memory_placed */
      .memoryMapPlaced = true,
      .memoryMapRangePlaced = false,
      .memoryUnmapReserve = true,

      /* VK_KHR_shader_quad_control */
      .shaderQuadControl = true,

#ifdef ANV_USE_WSI_PLATFORM
      /* VK_EXT_swapchain_maintenance1 */
      .swapchainMaintenance1 = true,
#endif

      /* VK_KHR_video_maintenance1 */
      .videoMaintenance1 = true,

      /* VK_EXT_image_compression_control */
      .imageCompressionControl = true,

      /* VK_KHR_shader_float_controls2 */
      .shaderFloatControls2 = true,

      /* VK_EXT_legacy_vertex_attributes */
      .legacyVertexAttributes = true,

      /* VK_EXT_legacy_dithering */
      .legacyDithering = true,

      /* VK_MESA_image_alignment_control */
      .imageAlignmentControl = true,

      /* VK_KHR_maintenance7 */
      .maintenance7 = true,

      /* VK_KHR_shader_relaxed_extended_instruction */
      .shaderRelaxedExtendedInstruction = true,

      /* VK_KHR_dynamic_rendering_local_read */
      .dynamicRenderingLocalRead = true,

      /* VK_EXT_pipeline_protected_access */
      .pipelineProtectedAccess = pdevice->has_protected_contexts,

      /* VK_EXT_host_image_copy */
      .hostImageCopy = true,
   };

   /* The new DOOM and Wolfenstein games require depthBounds without
    * checking for it.  They seem to run fine without it so just claim it's
    * there and accept the consequences.
    */
   if (app_info->engine_name && strcmp(app_info->engine_name, "idTech") == 0)
      features->depthBounds = true;
}

#define MAX_PER_STAGE_DESCRIPTOR_UNIFORM_BUFFERS   64

#define MAX_PER_STAGE_DESCRIPTOR_INPUT_ATTACHMENTS 64

static VkDeviceSize
anx_get_physical_device_max_heap_size(const struct anv_physical_device *pdevice)
{
   VkDeviceSize ret = 0;

   for (uint32_t i = 0; i < pdevice->memory.heap_count; i++) {
      if (pdevice->memory.heaps[i].size > ret)
         ret = pdevice->memory.heaps[i].size;
   }

   return ret;
}

static void
get_properties_1_1(const struct anv_physical_device *pdevice,
                   struct vk_properties *p)
{
   memcpy(p->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
   memcpy(p->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
   memset(p->deviceLUID, 0, VK_LUID_SIZE);
   p->deviceNodeMask = 0;
   p->deviceLUIDValid = false;

   p->subgroupSize = BRW_SUBGROUP_SIZE;
   VkShaderStageFlags scalar_stages = 0;
   for (unsigned stage = 0; stage < MESA_SHADER_STAGES; stage++) {
      scalar_stages |= mesa_to_vk_shader_stage(stage);
   }
   if (pdevice->vk.supported_extensions.KHR_ray_tracing_pipeline) {
      scalar_stages |= VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                       VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                       VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                       VK_SHADER_STAGE_MISS_BIT_KHR |
                       VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
                       VK_SHADER_STAGE_CALLABLE_BIT_KHR;
   }
   if (pdevice->vk.supported_extensions.EXT_mesh_shader) {
      scalar_stages |= VK_SHADER_STAGE_TASK_BIT_EXT |
                       VK_SHADER_STAGE_MESH_BIT_EXT;
   }
   p->subgroupSupportedStages = scalar_stages;
   p->subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT |
                                    VK_SUBGROUP_FEATURE_VOTE_BIT |
                                    VK_SUBGROUP_FEATURE_BALLOT_BIT |
                                    VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                                    VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                                    VK_SUBGROUP_FEATURE_QUAD_BIT |
                                    VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
                                    VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
                                    VK_SUBGROUP_FEATURE_ROTATE_BIT_KHR |
                                    VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT_KHR;
   p->subgroupQuadOperationsInAllStages = true;

   p->pointClippingBehavior      = VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY;
   p->maxMultiviewViewCount      = 16;
   p->maxMultiviewInstanceIndex  = UINT32_MAX / 16;
   /* Our protected implementation is a memory encryption mechanism, it
    * shouldn't page fault, but it hangs the HW so in terms of user visibility
    * it's similar to a fault.
    */
   p->protectedNoFault           = false;
   /* This value doesn't matter for us today as our per-stage descriptors are
    * the real limit.
    */
   p->maxPerSetDescriptors       = 1024;

   for (uint32_t i = 0; i < pdevice->memory.heap_count; i++) {
      p->maxMemoryAllocationSize = MAX2(p->maxMemoryAllocationSize,
                                        pdevice->memory.heaps[i].size);
   }
}

static void
get_properties_1_2(const struct anv_physical_device *pdevice,
                   struct vk_properties *p)
{
   p->driverID = VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA;
   memset(p->driverName, 0, sizeof(p->driverName));
   snprintf(p->driverName, VK_MAX_DRIVER_NAME_SIZE,
            "Intel open-source Mesa driver");
   memset(p->driverInfo, 0, sizeof(p->driverInfo));
   snprintf(p->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);

   p->conformanceVersion = (VkConformanceVersion) {
      .major = 1,
      .minor = 4,
      .subminor = 0,
      .patch = 0,
   };

   p->denormBehaviorIndependence =
      VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
   p->roundingModeIndependence =
      VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;

   /* Broadwell does not support HF denorms and there are restrictions
    * other gens. According to Kabylake's PRM:
    *
    * "math - Extended Math Function
    * [...]
    * Restriction : Half-float denorms are always retained."
    */
   p->shaderDenormFlushToZeroFloat16         = false;
   p->shaderDenormPreserveFloat16            = true;
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

   struct anv_descriptor_limits desc_limits;
   get_device_descriptor_limits(pdevice, &desc_limits);

   p->maxUpdateAfterBindDescriptorsInAllPools            = desc_limits.max_resources;
   p->shaderUniformBufferArrayNonUniformIndexingNative   = false;
   p->shaderSampledImageArrayNonUniformIndexingNative    = false;
   p->shaderStorageBufferArrayNonUniformIndexingNative   = true;
   p->shaderStorageImageArrayNonUniformIndexingNative    = false;
   p->shaderInputAttachmentArrayNonUniformIndexingNative = false;
   p->robustBufferAccessUpdateAfterBind                  = true;
   p->quadDivergentImplicitLod                           = false;
   p->maxPerStageDescriptorUpdateAfterBindSamplers       = desc_limits.max_samplers;
   p->maxPerStageDescriptorUpdateAfterBindUniformBuffers = desc_limits.max_ubos;
   p->maxPerStageDescriptorUpdateAfterBindStorageBuffers = desc_limits.max_ssbos;
   p->maxPerStageDescriptorUpdateAfterBindSampledImages  = desc_limits.max_images;
   p->maxPerStageDescriptorUpdateAfterBindStorageImages  = desc_limits.max_images;
   p->maxPerStageDescriptorUpdateAfterBindInputAttachments = MAX_PER_STAGE_DESCRIPTOR_INPUT_ATTACHMENTS;
   p->maxPerStageUpdateAfterBindResources                = desc_limits.max_resources;
   p->maxDescriptorSetUpdateAfterBindSamplers            = desc_limits.max_samplers;
   p->maxDescriptorSetUpdateAfterBindUniformBuffers      = desc_limits.max_ubos;
   p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = MAX_DYNAMIC_BUFFERS / 2;
   p->maxDescriptorSetUpdateAfterBindStorageBuffers      = desc_limits.max_ssbos;
   p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = MAX_DYNAMIC_BUFFERS / 2;
   p->maxDescriptorSetUpdateAfterBindSampledImages       = desc_limits.max_images;
   p->maxDescriptorSetUpdateAfterBindStorageImages       = desc_limits.max_images;
   p->maxDescriptorSetUpdateAfterBindInputAttachments    = MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS;

   /* We support all of the depth resolve modes */
   p->supportedDepthResolveModes    = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                      VK_RESOLVE_MODE_AVERAGE_BIT |
                                      VK_RESOLVE_MODE_MIN_BIT |
                                      VK_RESOLVE_MODE_MAX_BIT;
   /* Average doesn't make sense for stencil so we don't support that */
   p->supportedStencilResolveModes  = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                      VK_RESOLVE_MODE_MIN_BIT |
                                      VK_RESOLVE_MODE_MAX_BIT;
   p->independentResolveNone  = true;
   p->independentResolve      = true;

   p->filterMinmaxSingleComponentFormats  = true;
   p->filterMinmaxImageComponentMapping   = true;

   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;

   p->framebufferIntegerColorSampleCounts =
      isl_device_get_sample_counts(&pdevice->isl_dev);
}

static void
get_properties_1_3(const struct anv_physical_device *pdevice,
                   struct vk_properties *p)
{
   if (pdevice->info.ver >= 20)
      p->minSubgroupSize = 16;
   else
      p->minSubgroupSize = 8;
   p->maxSubgroupSize = 32;
   p->maxComputeWorkgroupSubgroups = pdevice->info.max_cs_workgroup_threads;
   p->requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT |
                                   VK_SHADER_STAGE_TASK_BIT_EXT |
                                   VK_SHADER_STAGE_MESH_BIT_EXT;

   p->maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE;
   p->maxPerStageDescriptorInlineUniformBlocks =
      MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
   p->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
      MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
   p->maxDescriptorSetInlineUniformBlocks =
      MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
   p->maxDescriptorSetUpdateAfterBindInlineUniformBlocks =
      MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS;
   p->maxInlineUniformTotalSize = UINT16_MAX;

   p->integerDotProduct8BitUnsignedAccelerated = false;
   p->integerDotProduct8BitSignedAccelerated = false;
   p->integerDotProduct8BitMixedSignednessAccelerated = false;
   p->integerDotProduct4x8BitPackedUnsignedAccelerated = pdevice->info.ver >= 12;
   p->integerDotProduct4x8BitPackedSignedAccelerated = pdevice->info.ver >= 12;
   p->integerDotProduct4x8BitPackedMixedSignednessAccelerated = pdevice->info.ver >= 12;
   p->integerDotProduct16BitUnsignedAccelerated = false;
   p->integerDotProduct16BitSignedAccelerated = false;
   p->integerDotProduct16BitMixedSignednessAccelerated = false;
   p->integerDotProduct32BitUnsignedAccelerated = false;
   p->integerDotProduct32BitSignedAccelerated = false;
   p->integerDotProduct32BitMixedSignednessAccelerated = false;
   p->integerDotProduct64BitUnsignedAccelerated = false;
   p->integerDotProduct64BitSignedAccelerated = false;
   p->integerDotProduct64BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating8BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = pdevice->info.ver >= 12;
   p->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = pdevice->info.ver >= 12;
   p->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated = pdevice->info.ver >= 12;
   p->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating16BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = false;

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
   p->storageTexelBufferOffsetAlignmentBytes = 16;
   p->storageTexelBufferOffsetSingleTexelAlignment = true;

   /* The sampler, however, is much more forgiving and it can handle
    * arbitrary byte alignment for linear and buffer surfaces.  It's
    * hard to find a good PRM citation for this but years of empirical
    * experience demonstrate that this is true.
    */
   p->uniformTexelBufferOffsetAlignmentBytes = 1;
   p->uniformTexelBufferOffsetSingleTexelAlignment = true;

   p->maxBufferSize = pdevice->isl_dev.max_buffer_size;
}

static void
get_properties(const struct anv_physical_device *pdevice,
               struct vk_properties *props)
{

      const struct intel_device_info *devinfo = &pdevice->info;

   const VkDeviceSize max_heap_size = anx_get_physical_device_max_heap_size(pdevice);

   const uint32_t max_workgroup_size =
      MIN2(1024, 32 * devinfo->max_cs_workgroup_threads);

   const bool has_sparse_or_fake = pdevice->sparse_type != ANV_SPARSE_TYPE_NOT_SUPPORTED;
   const bool sparse_uses_trtt = pdevice->sparse_type == ANV_SPARSE_TYPE_TRTT;

   uint64_t sparse_addr_space_size =
      !has_sparse_or_fake ? 0 :
      sparse_uses_trtt ? pdevice->va.trtt.size :
      pdevice->va.high_heap.size;

   VkSampleCountFlags sample_counts =
      isl_device_get_sample_counts(&pdevice->isl_dev);

#if DETECT_OS_ANDROID
   /* Used to fill struct VkPhysicalDevicePresentationPropertiesANDROID */
   uint64_t front_rendering_usage = 0;
   struct u_gralloc *gralloc = u_gralloc_create(U_GRALLOC_TYPE_AUTO);
   if (gralloc != NULL) {
      u_gralloc_get_front_rendering_usage(gralloc, &front_rendering_usage);
      u_gralloc_destroy(&gralloc);
   }
#endif /* DETECT_OS_ANDROID */

   struct anv_descriptor_limits desc_limits;
   get_device_descriptor_limits(pdevice, &desc_limits);

   *props = (struct vk_properties) {
      .apiVersion = ANV_API_VERSION,
      .driverVersion = vk_get_driver_version(),
      .vendorID = pdevice->instance->force_vk_vendor != 0 ?
                  pdevice->instance->force_vk_vendor : 0x8086,
      .deviceID = pdevice->info.pci_device_id,
      .deviceType = pdevice->info.has_local_mem ?
                    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU :
                    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,

      /* Limits: */
      .maxImageDimension1D                      = (1 << 14),
      .maxImageDimension2D                      = (1 << 14),
      .maxImageDimension3D                      = (1 << 11),
      .maxImageDimensionCube                    = (1 << 14),
      .maxImageArrayLayers                      = (1 << 11),
      .maxTexelBufferElements                   = 128 * 1024 * 1024,
      .maxUniformBufferRange                    = pdevice->compiler->indirect_ubos_use_sampler ? (1u << 27) : (1u << 30),
      .maxStorageBufferRange                    = MIN3(pdevice->isl_dev.max_buffer_size, max_heap_size, UINT32_MAX),
      .maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount                 = UINT32_MAX,
      .maxSamplerAllocationCount                = 64 * 1024,
      .bufferImageGranularity                   = 1,
      .sparseAddressSpaceSize                   = sparse_addr_space_size,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = desc_limits.max_samplers,
      .maxPerStageDescriptorUniformBuffers      = desc_limits.max_ubos,
      .maxPerStageDescriptorStorageBuffers      = desc_limits.max_ssbos,
      .maxPerStageDescriptorSampledImages       = desc_limits.max_images,
      .maxPerStageDescriptorStorageImages       = desc_limits.max_images,
      .maxPerStageDescriptorInputAttachments    = MAX_PER_STAGE_DESCRIPTOR_INPUT_ATTACHMENTS,
      .maxPerStageResources                     = desc_limits.max_resources,
      .maxDescriptorSetSamplers                 = desc_limits.max_samplers,
      .maxDescriptorSetUniformBuffers           = desc_limits.max_ubos,
      .maxDescriptorSetUniformBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetStorageBuffers           = desc_limits.max_ssbos,
      .maxDescriptorSetStorageBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetSampledImages            = desc_limits.max_images,
      .maxDescriptorSetStorageImages            = desc_limits.max_images,
      .maxDescriptorSetInputAttachments         = MAX_DESCRIPTOR_SET_INPUT_ATTACHMENTS,
      .maxVertexInputAttributes                 = MAX_VES,
      .maxVertexInputBindings                   = MAX_VBS,
      /* Broadwell PRMs: Volume 2d: Command Reference: Structures:
       *
       * VERTEX_ELEMENT_STATE::Source Element Offset: [0,2047]
       */
      .maxVertexInputAttributeOffset            = 2047,
      /* Skylake PRMs: Volume 2d: Command Reference: Structures:
       *
       * VERTEX_BUFFER_STATE::Buffer Pitch: [0,4095]
       */
      .maxVertexInputBindingStride              = 4095,
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
      .maxGeometryInputComponents               = 128,
      .maxGeometryOutputComponents              = 128,
      .maxGeometryOutputVertices                = 256,
      .maxGeometryTotalOutputComponents         = 1024,
      .maxFragmentInputComponents               = 116, /* 128 components - (PSIZ, CLIP_DIST0, CLIP_DIST1) */
      .maxFragmentOutputAttachments             = 8,
      .maxFragmentDualSrcAttachments            = 1,
      .maxFragmentCombinedOutputResources       = MAX_RTS + desc_limits.max_ssbos +
                                                  desc_limits.max_images,
      .maxComputeSharedMemorySize               = intel_device_info_get_max_slm_size(&pdevice->info),
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
      /* While SKL and up support much wider lines than we are setting here,
       * in practice we run into conformance issues if we go past this limit.
       * Since the Windows driver does the same, it's probably fair to assume
       * that no one needs more than this.
       */
      .lineWidthRange                           = { 0.0, 8.0 },
      .pointSizeGranularity                     = (1.0 / 8.0),
      .lineWidthGranularity                     = (1.0 / 128.0),
      .strictLines                              = false,
      .standardSampleLocations                  = true,
      .optimalBufferCopyOffsetAlignment         = 128,
      .optimalBufferCopyRowPitchAlignment       = 128,
      .nonCoherentAtomSize                      = 64,

      /* Sparse: */
      .sparseResidencyStandard2DBlockShape = has_sparse_or_fake,
      .sparseResidencyStandard2DMultisampleBlockShape = false,
      .sparseResidencyStandard3DBlockShape = has_sparse_or_fake,
      .sparseResidencyAlignedMipSize = false,
      .sparseResidencyNonResidentStrict = has_sparse_or_fake,

      /* VK_KHR_cooperative_matrix */
      .cooperativeMatrixSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT,

      /* Vulkan 1.4 */
      .dynamicRenderingLocalReadDepthStencilAttachments = true,
      .dynamicRenderingLocalReadMultisampledAttachments = true,
   };

   snprintf(props->deviceName, sizeof(props->deviceName),
            "%s", pdevice->info.name);
   memcpy(props->pipelineCacheUUID,
          pdevice->pipeline_cache_uuid, VK_UUID_SIZE);

   get_properties_1_1(pdevice, props);
   get_properties_1_2(pdevice, props);
   get_properties_1_3(pdevice, props);

   /* VK_KHR_acceleration_structure */
   {
      props->maxGeometryCount = (1u << 24) - 1;
      props->maxInstanceCount = (1u << 24) - 1;
      props->maxPrimitiveCount = (1u << 29) - 1;
      props->maxPerStageDescriptorAccelerationStructures = UINT16_MAX;
      props->maxPerStageDescriptorUpdateAfterBindAccelerationStructures = UINT16_MAX;
      props->maxDescriptorSetAccelerationStructures = UINT16_MAX;
      props->maxDescriptorSetUpdateAfterBindAccelerationStructures = UINT16_MAX;
      props->minAccelerationStructureScratchOffsetAlignment = 64;
   }

   /* VK_KHR_compute_shader_derivatives */
   {
      props->meshAndTaskShaderDerivatives = pdevice->info.has_mesh_shading;
   }

   /* VK_KHR_fragment_shading_rate */
   {
      props->primitiveFragmentShadingRateWithMultipleViewports =
         pdevice->info.has_coarse_pixel_primitive_and_cb;
      props->layeredShadingRateAttachments =
      pdevice->info.has_coarse_pixel_primitive_and_cb;
      props->fragmentShadingRateNonTrivialCombinerOps =
         pdevice->info.has_coarse_pixel_primitive_and_cb;
      props->maxFragmentSize = (VkExtent2D) { 4, 4 };
      props->maxFragmentSizeAspectRatio =
         pdevice->info.has_coarse_pixel_primitive_and_cb ?
         2 : 4;
      props->maxFragmentShadingRateCoverageSamples = 4 * 4 *
         (pdevice->info.has_coarse_pixel_primitive_and_cb ? 4 : 16);
      props->maxFragmentShadingRateRasterizationSamples =
      pdevice->info.has_coarse_pixel_primitive_and_cb ?
         VK_SAMPLE_COUNT_4_BIT :  VK_SAMPLE_COUNT_16_BIT;
      props->fragmentShadingRateWithShaderDepthStencilWrites = false;
      props->fragmentShadingRateWithSampleMask = true;
      props->fragmentShadingRateWithShaderSampleMask = false;
      props->fragmentShadingRateWithConservativeRasterization = true;
      props->fragmentShadingRateWithFragmentShaderInterlock = true;
      props->fragmentShadingRateWithCustomSampleLocations = true;
      props->fragmentShadingRateStrictMultiplyCombiner = true;

      if (pdevice->info.has_coarse_pixel_primitive_and_cb) {
         props->minFragmentShadingRateAttachmentTexelSize = (VkExtent2D) { 8, 8 };
         props->maxFragmentShadingRateAttachmentTexelSize = (VkExtent2D) { 8, 8 };
         props->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = 1;
      } else {
         /* Those must be 0 if attachmentFragmentShadingRate is not supported. */
         props->minFragmentShadingRateAttachmentTexelSize = (VkExtent2D) { 0, 0 };
         props->maxFragmentShadingRateAttachmentTexelSize = (VkExtent2D) { 0, 0 };
         props->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = 0;
      }
   }

   /* VK_KHR_maintenance5 */
   {
      props->earlyFragmentMultisampleCoverageAfterSampleCounting = false;
      props->earlyFragmentSampleMaskTestBeforeSampleCounting = false;
      props->depthStencilSwizzleOneSupport = true;
      props->polygonModePointSize = true;
      props->nonStrictSinglePixelWideLinesUseParallelogram = false;
      props->nonStrictWideLinesUseParallelogram = false;
   }

   /* VK_KHR_maintenance6 */
   {
      props->blockTexelViewCompatibleMultipleLayers = true;
      props->maxCombinedImageSamplerDescriptorCount = 3;
      props->fragmentShadingRateClampCombinerInputs = true;
   }

   /* VK_KHR_maintenance7 */
   {
      props->robustFragmentShadingRateAttachmentAccess = true;
      props->separateDepthStencilAttachmentAccess = true;
      props->maxDescriptorSetTotalUniformBuffersDynamic = MAX_DYNAMIC_BUFFERS;
      props->maxDescriptorSetTotalStorageBuffersDynamic = MAX_DYNAMIC_BUFFERS;
      props->maxDescriptorSetTotalBuffersDynamic = MAX_DYNAMIC_BUFFERS;
      props->maxDescriptorSetUpdateAfterBindTotalUniformBuffersDynamic = MAX_DYNAMIC_BUFFERS;
      props->maxDescriptorSetUpdateAfterBindTotalStorageBuffersDynamic = MAX_DYNAMIC_BUFFERS;
      props->maxDescriptorSetUpdateAfterBindTotalBuffersDynamic = MAX_DYNAMIC_BUFFERS;
   }

   /* VK_KHR_performance_query */
   {
      props->allowCommandBufferQueryCopies = false;
   }

   /* VK_KHR_push_descriptor */
   {
      props->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
   }

   /* VK_KHR_ray_tracing_pipeline */
   {
      /* TODO */
      props->shaderGroupHandleSize = 32;
      props->maxRayRecursionDepth = 31;
      /* MemRay::hitGroupSRStride is 16 bits */
      props->maxShaderGroupStride = UINT16_MAX;
      /* MemRay::hitGroupSRBasePtr requires 16B alignment */
      props->shaderGroupBaseAlignment = 16;
      props->shaderGroupHandleAlignment = 16;
      props->shaderGroupHandleCaptureReplaySize = 32;
      props->maxRayDispatchInvocationCount = 1U << 30; /* required min limit */
      props->maxRayHitAttributeSize = BRW_RT_SIZEOF_HIT_ATTRIB_DATA;
   }

   /* VK_KHR_vertex_attribute_divisor */
   {
      props->maxVertexAttribDivisor = UINT32_MAX / 16;
      props->supportsNonZeroFirstInstance = true;
   }

   /* VK_EXT_conservative_rasterization */
   {
      /* There's nothing in the public docs about this value as far as I can
       * tell. However, this is the value the Windows driver reports and
       * there's a comment on a rejected HW feature in the internal docs that
       * says:
       *
       *    "This is similar to conservative rasterization, except the
       *    primitive area is not extended by 1/512 and..."
       *
       * That's a bit of an obtuse reference but it's the best we've got for
       * now.
       */
      props->primitiveOverestimationSize = 1.0f / 512.0f;
      props->maxExtraPrimitiveOverestimationSize = 0.0f;
      props->extraPrimitiveOverestimationSizeGranularity = 0.0f;
      props->primitiveUnderestimation = false;
      props->conservativePointAndLineRasterization = false;
      props->degenerateTrianglesRasterized = true;
      props->degenerateLinesRasterized = false;
      props->fullyCoveredFragmentShaderInputVariable = false;
      props->conservativeRasterizationPostDepthCoverage = true;
   }

   /* VK_EXT_custom_border_color */
   {
      props->maxCustomBorderColorSamplers = MAX_CUSTOM_BORDER_COLORS;
   }

   /* VK_EXT_descriptor_buffer */
   {
      props->combinedImageSamplerDescriptorSingleArray = true;
      props->bufferlessPushDescriptors = true;
      /* Written to the buffer before a timeline semaphore is signaled, but
       * after vkQueueSubmit().
       */
      props->allowSamplerImageViewPostSubmitCreation = true;
      props->descriptorBufferOffsetAlignment = ANV_SURFACE_STATE_SIZE;

      if (pdevice->uses_ex_bso) {
         props->maxDescriptorBufferBindings = MAX_SETS;
         props->maxResourceDescriptorBufferBindings = MAX_SETS;
         props->maxSamplerDescriptorBufferBindings = MAX_SETS;
         props->maxEmbeddedImmutableSamplerBindings = MAX_SETS;
      } else {
         props->maxDescriptorBufferBindings = 3; /* resources, samplers, push (we don't care about push) */
         props->maxResourceDescriptorBufferBindings = 1;
         props->maxSamplerDescriptorBufferBindings = 1;
         props->maxEmbeddedImmutableSamplerBindings = 1;
      }
      props->maxEmbeddedImmutableSamplers = MAX_EMBEDDED_SAMPLERS;

      /* Storing a 64bit address */
      props->bufferCaptureReplayDescriptorDataSize = 8;
      props->imageCaptureReplayDescriptorDataSize = 8;
      /* Offset inside the reserved border color pool */
      props->samplerCaptureReplayDescriptorDataSize = 4;

      /* Not affected by replay */
      props->imageViewCaptureReplayDescriptorDataSize = 0;
      /* The acceleration structure virtual address backing is coming from a
       * buffer, so as long as that buffer is captured/replayed correctly we
       * should always get the same address.
       */
      props->accelerationStructureCaptureReplayDescriptorDataSize = 0;

      props->samplerDescriptorSize = ANV_SAMPLER_STATE_SIZE;
      props->combinedImageSamplerDescriptorSize = align(ANV_SURFACE_STATE_SIZE + ANV_SAMPLER_STATE_SIZE,
                                                        ANV_SURFACE_STATE_SIZE);
      props->sampledImageDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->storageImageDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->uniformTexelBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->robustUniformTexelBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->storageTexelBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->robustStorageTexelBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->uniformBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->robustUniformBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->storageBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->robustStorageBufferDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->inputAttachmentDescriptorSize = ANV_SURFACE_STATE_SIZE;
      props->accelerationStructureDescriptorSize = sizeof(struct anv_address_range_descriptor);
      props->maxSamplerDescriptorBufferRange = pdevice->va.dynamic_visible_pool.size;
      props->maxResourceDescriptorBufferRange = anv_physical_device_bindless_heap_size(pdevice,
                                                                                       true);
      props->resourceDescriptorBufferAddressSpaceSize = pdevice->va.dynamic_visible_pool.size;
      props->descriptorBufferAddressSpaceSize = pdevice->va.dynamic_visible_pool.size;
      props->samplerDescriptorBufferAddressSpaceSize = pdevice->va.dynamic_visible_pool.size;
   }

   /* VK_EXT_extended_dynamic_state3 */
   {
      props->dynamicPrimitiveTopologyUnrestricted = true;
   }

   /* VK_EXT_external_memory_host */
   {
      props->minImportedHostPointerAlignment = 4096;
   }

   /* VK_EXT_graphics_pipeline_library */
   {
      props->graphicsPipelineLibraryFastLinking = true;
      props->graphicsPipelineLibraryIndependentInterpolationDecoration = true;
   }

   /* VK_EXT_host_image_copy */
   {
      static const VkImageLayout supported_layouts[] = {
         VK_IMAGE_LAYOUT_GENERAL,
         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
         VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
         VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT,
         VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR,
      };

      props->pCopySrcLayouts = (VkImageLayout *) supported_layouts;
      props->copySrcLayoutCount = ARRAY_SIZE(supported_layouts);
      props->pCopyDstLayouts = (VkImageLayout *) supported_layouts;
      props->copyDstLayoutCount = ARRAY_SIZE(supported_layouts);

      /* This UUID essentially tells you if you can share an optimially tiling
       * image with another driver. Much of the tiling decisions are based on :
       *
       *    - device generation (different tilings based on generations)
       *    - device workarounds
       *    - driver build (as we implement workarounds or performance tunings,
       *      the tiling decision changes)
       *
       * So we're using a hash of the verx10 field + driver_build_sha1.
       *
       * Unfortunately there is a HW issue on SKL GT4 that makes it use some
       * different tilings sometimes (see isl_gfx7.c).
       */
      {
         struct mesa_sha1 sha1_ctx;
         uint8_t sha1[20];

         _mesa_sha1_init(&sha1_ctx);
         _mesa_sha1_update(&sha1_ctx, pdevice->driver_build_sha1,
                           sizeof(pdevice->driver_build_sha1));
         _mesa_sha1_update(&sha1_ctx, &pdevice->info.platform,
                           sizeof(pdevice->info.platform));
         if (pdevice->info.platform == INTEL_PLATFORM_SKL &&
             pdevice->info.gt == 4) {
            _mesa_sha1_update(&sha1_ctx, &pdevice->info.gt,
                              sizeof(pdevice->info.gt));
         }
         _mesa_sha1_final(&sha1_ctx, sha1);

         assert(ARRAY_SIZE(sha1) >= VK_UUID_SIZE);
         memcpy(props->optimalTilingLayoutUUID, sha1, VK_UUID_SIZE);
      }

      /* System without ReBAR cannot map all memory types on the host and that
       * affects the memory types an image can use for host memory copies.
       *
       * System with compressed memory types also cannot expose all image
       * memory types for host image copies.
       */
      props->identicalMemoryTypeRequirements = pdevice->has_small_bar ||
         pdevice->memory.compressed_mem_types != 0;
   }

   /* VK_EXT_legacy_vertex_attributes */
   {
      props->nativeUnalignedPerformance = true;
   }

   /* VK_EXT_line_rasterization */
   {
      /* In the Skylake PRM Vol. 7, subsection titled "GIQ (Diamond) Sampling
       * Rules - Legacy Mode", it says the following:
       *
       *    "Note that the device divides a pixel into a 16x16 array of
       *     subpixels, referenced by their upper left corners."
       *
       * This is the only known reference in the PRMs to the subpixel
       * precision of line rasterization and a "16x16 array of subpixels"
       * implies 4 subpixel precision bits. Empirical testing has shown that 4
       * subpixel precision bits applies to all line rasterization types.
       */
      props->lineSubPixelPrecisionBits = 4;
   }

   /* VK_EXT_map_memory_placed */
   {
      props->minPlacedMemoryMapAlignment = 4096;
   }

   /* VK_EXT_mesh_shader */
   {
      /* Bounded by the maximum representable size in
       * 3DSTATE_MESH_SHADER_BODY::SharedLocalMemorySize.  Same for Task.
       */
      const uint32_t max_slm_size = intel_device_info_get_max_slm_size(devinfo);

      /* Bounded by the maximum representable size in
       * 3DSTATE_MESH_SHADER_BODY::LocalXMaximum.  Same for Task.
       */
      const uint32_t max_workgroup_size = 1 << 10;

      /* 3DMESH_3D limitation. */
      const uint32_t max_threadgroup_count = 1 << 22;

      /* 3DMESH_3D limitation. */
      const uint32_t max_threadgroup_xyz = 65535;

      const uint32_t max_urb_size = 64 * 1024;

      props->maxTaskWorkGroupTotalCount = max_threadgroup_count;
      props->maxTaskWorkGroupCount[0] = max_threadgroup_xyz;
      props->maxTaskWorkGroupCount[1] = max_threadgroup_xyz;
      props->maxTaskWorkGroupCount[2] = max_threadgroup_xyz;

      props->maxTaskWorkGroupInvocations = max_workgroup_size;
      props->maxTaskWorkGroupSize[0] = max_workgroup_size;
      props->maxTaskWorkGroupSize[1] = max_workgroup_size;
      props->maxTaskWorkGroupSize[2] = max_workgroup_size;

      /* TUE header with padding */
      const uint32_t task_payload_reserved = 32;

      props->maxTaskPayloadSize = max_urb_size - task_payload_reserved;
      props->maxTaskSharedMemorySize = max_slm_size;
      props->maxTaskPayloadAndSharedMemorySize =
         props->maxTaskPayloadSize +
         props->maxTaskSharedMemorySize;

      props->maxMeshWorkGroupTotalCount = max_threadgroup_count;
      props->maxMeshWorkGroupCount[0] = max_threadgroup_xyz;
      props->maxMeshWorkGroupCount[1] = max_threadgroup_xyz;
      props->maxMeshWorkGroupCount[2] = max_threadgroup_xyz;

      props->maxMeshWorkGroupInvocations = max_workgroup_size;
      props->maxMeshWorkGroupSize[0] = max_workgroup_size;
      props->maxMeshWorkGroupSize[1] = max_workgroup_size;
      props->maxMeshWorkGroupSize[2] = max_workgroup_size;

      props->maxMeshSharedMemorySize = max_slm_size;
      props->maxMeshPayloadAndSharedMemorySize =
         props->maxTaskPayloadSize +
         props->maxMeshSharedMemorySize;

      /* Unfortunately spec's formula for the max output size doesn't match our hardware
       * (because some per-primitive and per-vertex attributes have alignment restrictions),
       * so we have to advertise the minimum value mandated by the spec to not overflow it.
       */
      props->maxMeshOutputPrimitives = 256;
      props->maxMeshOutputVertices = 256;

      /* NumPrim + Primitive Data List */
      const uint32_t max_indices_memory =
         ALIGN(sizeof(uint32_t) +
               sizeof(uint32_t) * props->maxMeshOutputVertices, 32);

      props->maxMeshOutputMemorySize = MIN2(max_urb_size - max_indices_memory, 32768);

      props->maxMeshPayloadAndOutputMemorySize =
         props->maxTaskPayloadSize +
         props->maxMeshOutputMemorySize;

      props->maxMeshOutputComponents = 128;

      /* RTAIndex is 11-bits wide */
      props->maxMeshOutputLayers = 1 << 11;

      props->maxMeshMultiviewViewCount = 1;

      /* Elements in Vertex Data Array must be aligned to 32 bytes (8 dwords). */
      props->meshOutputPerVertexGranularity = 8;
      /* Elements in Primitive Data Array must be aligned to 32 bytes (8 dwords). */
      props->meshOutputPerPrimitiveGranularity = 8;

      /* SIMD16 */
      props->maxPreferredTaskWorkGroupInvocations = 16;
      props->maxPreferredMeshWorkGroupInvocations = 16;

      props->prefersLocalInvocationVertexOutput = false;
      props->prefersLocalInvocationPrimitiveOutput = false;
      props->prefersCompactVertexOutput = false;
      props->prefersCompactPrimitiveOutput = false;

      /* Spec minimum values */
      assert(props->maxTaskWorkGroupTotalCount >= (1U << 22));
      assert(props->maxTaskWorkGroupCount[0] >= 65535);
      assert(props->maxTaskWorkGroupCount[1] >= 65535);
      assert(props->maxTaskWorkGroupCount[2] >= 65535);

      assert(props->maxTaskWorkGroupInvocations >= 128);
      assert(props->maxTaskWorkGroupSize[0] >= 128);
      assert(props->maxTaskWorkGroupSize[1] >= 128);
      assert(props->maxTaskWorkGroupSize[2] >= 128);

      assert(props->maxTaskPayloadSize >= 16384);
      assert(props->maxTaskSharedMemorySize >= 32768);
      assert(props->maxTaskPayloadAndSharedMemorySize >= 32768);


      assert(props->maxMeshWorkGroupTotalCount >= (1U << 22));
      assert(props->maxMeshWorkGroupCount[0] >= 65535);
      assert(props->maxMeshWorkGroupCount[1] >= 65535);
      assert(props->maxMeshWorkGroupCount[2] >= 65535);

      assert(props->maxMeshWorkGroupInvocations >= 128);
      assert(props->maxMeshWorkGroupSize[0] >= 128);
      assert(props->maxMeshWorkGroupSize[1] >= 128);
      assert(props->maxMeshWorkGroupSize[2] >= 128);

      assert(props->maxMeshSharedMemorySize >= 28672);
      assert(props->maxMeshPayloadAndSharedMemorySize >= 28672);
      assert(props->maxMeshOutputMemorySize >= 32768);
      assert(props->maxMeshPayloadAndOutputMemorySize >= 48128);

      assert(props->maxMeshOutputComponents >= 128);

      assert(props->maxMeshOutputVertices >= 256);
      assert(props->maxMeshOutputPrimitives >= 256);
      assert(props->maxMeshOutputLayers >= 8);
      assert(props->maxMeshMultiviewViewCount >= 1);
   }

   /* VK_EXT_multi_draw */
   {
      props->maxMultiDrawCount = 2048;
   }

   /* VK_EXT_nested_command_buffer */
   {
      props->maxCommandBufferNestingLevel = UINT32_MAX;
   }

   /* VK_EXT_pci_bus_info */
   {
      props->pciDomain = pdevice->info.pci_domain;
      props->pciBus = pdevice->info.pci_bus;
      props->pciDevice = pdevice->info.pci_dev;
      props->pciFunction = pdevice->info.pci_func;
   }

   /* VK_EXT_physical_device_drm */
   {
      props->drmHasPrimary = pdevice->has_master;
      props->drmPrimaryMajor = pdevice->master_major;
      props->drmPrimaryMinor = pdevice->master_minor;
      props->drmHasRender = pdevice->has_local;
      props->drmRenderMajor = pdevice->local_major;
      props->drmRenderMinor = pdevice->local_minor;
   }

   /* VK_EXT_pipeline_robustness */
   {
      props->defaultRobustnessStorageBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT;
      props->defaultRobustnessUniformBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT;
      props->defaultRobustnessVertexInputs =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT;
      props->defaultRobustnessImages =
         VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2_EXT;
   }

   /* VK_EXT_provoking_vertex */
   {
      props->provokingVertexModePerPipeline = true;
      props->transformFeedbackPreservesTriangleFanProvokingVertex = false;
   }

   /* VK_EXT_robustness2 */
   {
      props->robustStorageBufferAccessSizeAlignment =
         ANV_SSBO_BOUNDS_CHECK_ALIGNMENT;
      props->robustUniformBufferAccessSizeAlignment =
         ANV_UBO_ALIGNMENT;
   }

   /* VK_EXT_sample_locations */
   {
      props->sampleLocationSampleCounts =
         isl_device_get_sample_counts(&pdevice->isl_dev);

      /* See also anv_GetPhysicalDeviceMultisamplePropertiesEXT */
      props->maxSampleLocationGridSize.width = 1;
      props->maxSampleLocationGridSize.height = 1;

      props->sampleLocationCoordinateRange[0] = 0;
      props->sampleLocationCoordinateRange[1] = 0.9375;
      props->sampleLocationSubPixelBits = 4;

      props->variableSampleLocations = true;
   }

   /* VK_EXT_shader_module_identifier */
   {
      STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                    sizeof(props->shaderModuleIdentifierAlgorithmUUID));
      memcpy(props->shaderModuleIdentifierAlgorithmUUID,
             vk_shaderModuleIdentifierAlgorithmUUID,
             sizeof(props->shaderModuleIdentifierAlgorithmUUID));
   }

   /* VK_EXT_transform_feedback */
   {
      props->maxTransformFeedbackStreams = MAX_XFB_STREAMS;
      props->maxTransformFeedbackBuffers = MAX_XFB_BUFFERS;
      props->maxTransformFeedbackBufferSize = (1ull << 32);
      props->maxTransformFeedbackStreamDataSize = 128 * 4;
      props->maxTransformFeedbackBufferDataSize = 128 * 4;
      props->maxTransformFeedbackBufferDataStride = 2048;
      props->transformFeedbackQueries = true;
      props->transformFeedbackStreamsLinesTriangles = false;
      props->transformFeedbackRasterizationStreamSelect = false;
      props->transformFeedbackDraw = true;
   }

   /* VK_ANDROID_native_buffer */
#if DETECT_OS_ANDROID
   {
      props->sharedImage = front_rendering_usage ? VK_TRUE : VK_FALSE;
   }
#endif /* DETECT_OS_ANDROID */


   /* VK_MESA_image_alignment_control */
   {
      /* We support 4k/64k tiling alignments on most platforms */
      props->supportedImageAlignmentMask = (1 << 12) | (1 << 16);
   }
}

static VkResult MUST_CHECK
anv_init_meminfo(struct anv_physical_device *device, int fd)
{
   const struct intel_device_info *devinfo = &device->info;

   device->sys.region = &devinfo->mem.sram.mem;
   device->sys.size = devinfo->mem.sram.mappable.size;
   device->sys.available = devinfo->mem.sram.mappable.free;

   device->vram_mappable.region = &devinfo->mem.vram.mem;
   device->vram_mappable.size = devinfo->mem.vram.mappable.size;
   device->vram_mappable.available = devinfo->mem.vram.mappable.free;

   device->vram_non_mappable.region = &devinfo->mem.vram.mem;
   device->vram_non_mappable.size = devinfo->mem.vram.unmappable.size;
   device->vram_non_mappable.available = devinfo->mem.vram.unmappable.free;

   return VK_SUCCESS;
}

static void
anv_update_meminfo(struct anv_physical_device *device, int fd)
{
   if (!intel_device_info_update_memory_info(&device->info, fd))
      return;

   const struct intel_device_info *devinfo = &device->info;
   device->sys.available = devinfo->mem.sram.mappable.free;
   device->vram_mappable.available = devinfo->mem.vram.mappable.free;
   device->vram_non_mappable.available = devinfo->mem.vram.unmappable.free;
}

static VkResult
anv_physical_device_init_heaps(struct anv_physical_device *device, int fd)
{
   VkResult result = anv_init_meminfo(device, fd);
   if (result != VK_SUCCESS)
      return result;

   assert(device->sys.size != 0);

   if (anv_physical_device_has_vram(device)) {
      /* We can create 2 or 3 different heaps when we have local memory
       * support, first heap with local memory size and second with system
       * memory size and the third is added only if part of the vram is
       * mappable to the host.
       */
      device->memory.heap_count = 2;
      device->memory.heaps[0] = (struct anv_memory_heap) {
         /* If there is a vram_non_mappable, use that for the device only
          * heap. Otherwise use the vram_mappable.
          */
         .size = device->vram_non_mappable.size != 0 ?
                 device->vram_non_mappable.size : device->vram_mappable.size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
         .is_local_mem = true,
      };
      device->memory.heaps[1] = (struct anv_memory_heap) {
         .size = device->sys.size,
         .flags = 0,
         .is_local_mem = false,
      };
      /* Add an additional smaller vram mappable heap if we can't map all the
       * vram to the host.
       */
      if (device->vram_non_mappable.size > 0) {
         device->memory.heap_count++;
         device->memory.heaps[2] = (struct anv_memory_heap) {
            .size = device->vram_mappable.size,
            .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
            .is_local_mem = true,
         };
      }
   } else {
      device->memory.heap_count = 1;
      device->memory.heaps[0] = (struct anv_memory_heap) {
         .size = device->sys.size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
         .is_local_mem = false,
      };
   }

   switch (device->info.kmd_type) {
   case INTEL_KMD_TYPE_XE:
      result = anv_xe_physical_device_init_memory_types(device);
      break;
   case INTEL_KMD_TYPE_I915:
   default:
      result = anv_i915_physical_device_init_memory_types(device);
      break;
   }

   assert(device->memory.type_count < ARRAY_SIZE(device->memory.types));

   if (result != VK_SUCCESS)
      return result;

   /* Some games (e.g., Total War: WARHAMMER III) sometimes seem to expect to
    * find memory types both with and without
    * VK_MEMORY_TYPE_PROPERTY_DEVICE_LOCAL_BIT. So here we duplicate all our
    * memory types just to make these games happy.
    * This behavior is not spec-compliant as we still only have one heap that
    * is now inconsistent with some of the memory types, but the game doesn't
    * seem to care about it.
    */
   if (device->instance->anv_fake_nonlocal_memory &&
       !anv_physical_device_has_vram(device)) {
      const uint32_t base_types_count = device->memory.type_count;
      for (int i = 0; i < base_types_count; i++) {
         if (!(device->memory.types[i].propertyFlags &
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            continue;

         assert(device->memory.type_count < ARRAY_SIZE(device->memory.types));
         struct anv_memory_type *new_type =
            &device->memory.types[device->memory.type_count++];
         *new_type = device->memory.types[i];

         device->memory.types[i].propertyFlags &=
            ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      }
   }

   /* Replicate all non protected memory types for descriptor buffers because
    * we want to identify memory allocations to place them in the right memory
    * heap.
    */
   device->memory.default_buffer_mem_types =
      BITFIELD_RANGE(0, device->memory.type_count);
   device->memory.protected_mem_types = 0;
   device->memory.dynamic_visible_mem_types = 0;
   device->memory.compressed_mem_types = 0;

   const uint32_t base_types_count = device->memory.type_count;
   for (int i = 0; i < base_types_count; i++) {
      bool skip = false;

      if (device->memory.types[i].propertyFlags &
          VK_MEMORY_PROPERTY_PROTECTED_BIT) {
         device->memory.protected_mem_types |= BITFIELD_BIT(i);
         device->memory.default_buffer_mem_types &= (~BITFIELD_BIT(i));
         skip = true;
      }

      if (device->memory.types[i].compressed) {
         device->memory.compressed_mem_types |= BITFIELD_BIT(i);
         device->memory.default_buffer_mem_types &= (~BITFIELD_BIT(i));
         skip = true;
      }

      if (skip)
         continue;

      device->memory.dynamic_visible_mem_types |=
         BITFIELD_BIT(device->memory.type_count);

      assert(device->memory.type_count < ARRAY_SIZE(device->memory.types));
      struct anv_memory_type *new_type =
         &device->memory.types[device->memory.type_count++];
      *new_type = device->memory.types[i];
      new_type->dynamic_visible = true;
   }

   assert(device->memory.type_count <= VK_MAX_MEMORY_TYPES);

   for (unsigned i = 0; i < device->memory.type_count; i++) {
      VkMemoryPropertyFlags props = device->memory.types[i].propertyFlags;
      if ((props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          !(props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
         device->memory.need_flush = true;
#else
         return vk_errorf(device, VK_ERROR_INITIALIZATION_FAILED,
                          "Memory configuration requires flushing, but it's not implemented for this architecture");
#endif
   }

   return VK_SUCCESS;
}

static VkResult
anv_physical_device_init_uuids(struct anv_physical_device *device)
{
   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(anv_physical_device_init_uuids);
   if (!note) {
      return vk_errorf(device, VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to find build-id");
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < 20) {
      return vk_errorf(device, VK_ERROR_INITIALIZATION_FAILED,
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
   brw_device_sha1_update(&sha1_ctx, &device->info);
   _mesa_sha1_update(&sha1_ctx, &device->always_use_bindless,
                     sizeof(device->always_use_bindless));
   _mesa_sha1_final(&sha1_ctx, sha1);
   memcpy(device->pipeline_cache_uuid, sha1, VK_UUID_SIZE);

   intel_uuid_compute_driver_id(device->driver_uuid, &device->info, VK_UUID_SIZE);
   intel_uuid_compute_device_id(device->device_uuid, &device->info, VK_UUID_SIZE);

   return VK_SUCCESS;
}

static void
anv_physical_device_init_disk_cache(struct anv_physical_device *device)
{
#ifdef ENABLE_SHADER_CACHE
   char renderer[10];
   ASSERTED int len = snprintf(renderer, sizeof(renderer), "anv_%04x",
                               device->info.pci_device_id);
   assert(len == sizeof(renderer) - 2);

   char timestamp[41];
   _mesa_sha1_format(timestamp, device->driver_build_sha1);

   const uint64_t driver_flags =
      brw_get_compiler_config_value(device->compiler);
   device->vk.disk_cache = disk_cache_create(renderer, timestamp, driver_flags);
#endif
}

static void
anv_physical_device_free_disk_cache(struct anv_physical_device *device)
{
#ifdef ENABLE_SHADER_CACHE
   if (device->vk.disk_cache) {
      disk_cache_destroy(device->vk.disk_cache);
      device->vk.disk_cache = NULL;
   }
#else
   assert(device->vk.disk_cache == NULL);
#endif
}

/* The ANV_QUEUE_OVERRIDE environment variable is a comma separated list of
 * queue overrides.
 *
 * To override the number queues:
 *  * "gc" is for graphics queues with compute support
 *  * "g" is for graphics queues with no compute support
 *  * "c" is for compute queues with no graphics support
 *  * "v" is for video queues with no graphics support
 *  * "b" is for copy (blitter) queues with no graphics support
 *
 * For example, ANV_QUEUE_OVERRIDE=gc=2,c=1 would override the number of
 * advertised queues to be 2 queues with graphics+compute support, and 1 queue
 * with compute-only support.
 *
 * ANV_QUEUE_OVERRIDE=c=1 would override the number of advertised queues to
 * include 1 queue with compute-only support, but it will not change the
 * number of graphics+compute queues.
 *
 * ANV_QUEUE_OVERRIDE=gc=0,c=1 would override the number of advertised queues
 * to include 1 queue with compute-only support, and it would override the
 * number of graphics+compute queues to be 0.
 */
static void
anv_override_engine_counts(int *gc_count, int *g_count, int *c_count, int *v_count, int *blit_count)
{
   int gc_override = -1;
   int g_override = -1;
   int c_override = -1;
   int v_override = -1;
   int blit_override = -1;
   const char *env_ = os_get_option("ANV_QUEUE_OVERRIDE");

   /* Override queues for Android HWUI that expects min 2 queues. */
#if DETECT_OS_ANDROID
   *gc_count = 2;
#endif

   if (env_ == NULL)
      return;

   char *env = strdup(env_);
   char *save = NULL;
   char *next = strtok_r(env, ",", &save);
   while (next != NULL) {
      if (strncmp(next, "gc=", 3) == 0) {
         gc_override = strtol(next + 3, NULL, 0);
      } else if (strncmp(next, "g=", 2) == 0) {
         g_override = strtol(next + 2, NULL, 0);
      } else if (strncmp(next, "c=", 2) == 0) {
         c_override = strtol(next + 2, NULL, 0);
      } else if (strncmp(next, "v=", 2) == 0) {
         v_override = strtol(next + 2, NULL, 0);
      } else if (strncmp(next, "b=", 2) == 0) {
         blit_override = strtol(next + 2, NULL, 0);
      } else {
         mesa_logw("Ignoring unsupported ANV_QUEUE_OVERRIDE token: %s", next);
      }
      next = strtok_r(NULL, ",", &save);
   }
   free(env);
   if (gc_override >= 0)
      *gc_count = gc_override;
   if (g_override >= 0)
      *g_count = g_override;
   if (*g_count > 0 && *gc_count <= 0 && (gc_override >= 0 || g_override >= 0))
      mesa_logw("ANV_QUEUE_OVERRIDE: gc=0 with g > 0 violates the "
                "Vulkan specification");
   if (c_override >= 0)
      *c_count = c_override;
   if (v_override >= 0)
      *v_count = v_override;
   if (blit_override >= 0)
      *blit_count = blit_override;
}

static void
anv_physical_device_init_queue_families(struct anv_physical_device *pdevice)
{
   uint32_t family_count = 0;
   VkQueueFlags sparse_flags = pdevice->sparse_type != ANV_SPARSE_TYPE_NOT_SUPPORTED ?
                               VK_QUEUE_SPARSE_BINDING_BIT : 0;
   VkQueueFlags protected_flag = pdevice->has_protected_contexts ?
                                 VK_QUEUE_PROTECTED_BIT : 0;

   if (pdevice->engine_info) {
      int gc_count =
         intel_engines_count(pdevice->engine_info,
                             INTEL_ENGINE_CLASS_RENDER);
      int v_count =
         intel_engines_count(pdevice->engine_info, INTEL_ENGINE_CLASS_VIDEO);
      int g_count = 0;
      int c_count = 0;
      /* Not only the Kernel needs to have vm_control, but it also needs to
       * have a new enough GuC and the interface to tell us so. This is
       * implemented in the common layer by is_guc_semaphore_functional() and
       * results in devinfo->engine_class_supported_count being adjusted,
       * which we read below.
       */
      const bool kernel_supports_non_render_engines = pdevice->has_vm_control;
      /* For now we're choosing to not expose non-render engines on i915.ko
       * even when the Kernel allows it. We have data suggesting it's not an
       * obvious win in terms of performance.
       */
      const bool can_use_non_render_engines =
         kernel_supports_non_render_engines &&
         pdevice->info.kmd_type == INTEL_KMD_TYPE_XE;

      if (can_use_non_render_engines) {
         c_count = pdevice->info.engine_class_supported_count[INTEL_ENGINE_CLASS_COMPUTE];
      }

      int blit_count = 0;
      if (pdevice->info.verx10 >= 125 && can_use_non_render_engines) {
         blit_count = pdevice->info.engine_class_supported_count[INTEL_ENGINE_CLASS_COPY];
      }

      anv_override_engine_counts(&gc_count, &g_count, &c_count, &v_count, &blit_count);

      enum intel_engine_class compute_class =
         pdevice->info.engine_class_supported_count[INTEL_ENGINE_CLASS_COMPUTE] &&
         c_count >= 1 ? INTEL_ENGINE_CLASS_COMPUTE :
                        INTEL_ENGINE_CLASS_RENDER;

      if (gc_count > 0) {
         pdevice->queue.families[family_count++] = (struct anv_queue_family) {
            .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                          VK_QUEUE_COMPUTE_BIT |
                          VK_QUEUE_TRANSFER_BIT |
                          sparse_flags |
                          protected_flag,
            .queueCount = gc_count,
            .engine_class = INTEL_ENGINE_CLASS_RENDER,
            .supports_perf = true,
         };
      }
      if (g_count > 0) {
         pdevice->queue.families[family_count++] = (struct anv_queue_family) {
            .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                          VK_QUEUE_TRANSFER_BIT |
                          sparse_flags |
                          protected_flag,
            .queueCount = g_count,
            .engine_class = INTEL_ENGINE_CLASS_RENDER,
         };
      }
      if (c_count > 0) {
         pdevice->queue.families[family_count++] = (struct anv_queue_family) {
            .queueFlags = VK_QUEUE_COMPUTE_BIT |
                          VK_QUEUE_TRANSFER_BIT |
                          sparse_flags |
                          protected_flag,
            .queueCount = c_count,
            .engine_class = compute_class,
         };
      }
      if (v_count > 0 && (pdevice->video_decode_enabled || pdevice->video_encode_enabled)) {
         /* HEVC support on Gfx9 is only available on VCS0. So limit the number of video queues
          * to the first VCS engine instance.
          *
          * We should be able to query HEVC support from the kernel using the engine query uAPI,
          * but this appears to be broken :
          *    https://gitlab.freedesktop.org/drm/intel/-/issues/8832
          *
          * When this bug is fixed we should be able to check HEVC support to determine the
          * correct number of queues.
          */
         /* TODO: enable protected content on video queue */
         pdevice->queue.families[family_count++] = (struct anv_queue_family) {
            .queueFlags = (pdevice->video_decode_enabled ? VK_QUEUE_VIDEO_DECODE_BIT_KHR : 0) |
                          (pdevice->video_encode_enabled ? VK_QUEUE_VIDEO_ENCODE_BIT_KHR : 0),
            .queueCount = pdevice->info.ver == 9 ? MIN2(1, v_count) : v_count,
            .engine_class = INTEL_ENGINE_CLASS_VIDEO,
         };
      }
      if (blit_count > 0) {
         pdevice->queue.families[family_count++] = (struct anv_queue_family) {
            .queueFlags = VK_QUEUE_TRANSFER_BIT |
                          protected_flag,
            .queueCount = blit_count,
            .engine_class = INTEL_ENGINE_CLASS_COPY,
         };
      }
   } else {
      /* Default to a single render queue */
      pdevice->queue.families[family_count++] = (struct anv_queue_family) {
         .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                       VK_QUEUE_COMPUTE_BIT |
                       VK_QUEUE_TRANSFER_BIT |
                       sparse_flags,
         .queueCount = 1,
         .engine_class = INTEL_ENGINE_CLASS_RENDER,
      };
      family_count = 1;
   }
   assert(family_count <= ANV_MAX_QUEUE_FAMILIES);
   pdevice->queue.family_count = family_count;
}

static VkResult
anv_physical_device_get_parameters(struct anv_physical_device *device)
{
   switch (device->info.kmd_type) {
   case INTEL_KMD_TYPE_I915:
      return anv_i915_physical_device_get_parameters(device);
   case INTEL_KMD_TYPE_XE:
      return anv_xe_physical_device_get_parameters(device);
   default:
      unreachable("Missing");
      return VK_ERROR_UNKNOWN;
   }
}

VkResult
anv_physical_device_try_create(struct vk_instance *vk_instance,
                               struct _drmDevice *drm_device,
                               struct vk_physical_device **out)
{
   struct anv_instance *instance =
      container_of(vk_instance, struct anv_instance, vk);

   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER)) ||
       drm_device->bustype != DRM_BUS_PCI ||
       drm_device->deviceinfo.pci->vendor_id != 0x8086)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   const char *primary_path = drm_device->nodes[DRM_NODE_PRIMARY];
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   VkResult result;
   int fd;
   int master_fd = -1;

   process_intel_debug_variable();

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      if (errno == ENOMEM) {
         return vk_errorf(instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "Unable to open device %s: out of memory", path);
      }
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "Unable to open device %s: %m", path);
   }

   struct intel_device_info devinfo;
   if (!intel_get_device_info_from_fd(fd, &devinfo, 9, -1)) {
      result = VK_ERROR_INCOMPATIBLE_DRIVER;
      goto fail_fd;
   }

   if (devinfo.ver < 9) {
      /* Silently fail here, hasvk should pick up this device. */
      result = VK_ERROR_INCOMPATIBLE_DRIVER;
      goto fail_fd;
   } else if (devinfo.probe_forced) {
      /* If INTEL_FORCE_PROBE was used, then the user has opted-in for
       * unsupported device support. No need to print a warning message.
       */
   } else if (devinfo.ver > 20) {
      result = vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                         "Vulkan not yet supported on %s", devinfo.name);
      goto fail_fd;
   }

   if (!devinfo.has_context_isolation) {
      result = vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                         "Vulkan requires context isolation for %s", devinfo.name);
      goto fail_fd;
   }

   struct anv_physical_device *device =
      vk_zalloc(&instance->vk.alloc, sizeof(*device), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (device == NULL) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_fd;
   }

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &anv_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk,
                                    NULL, NULL, NULL, /* We set up extensions later */
                                    &dispatch_table);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail_alloc;
   }
   device->instance = instance;

   assert(strlen(path) < ARRAY_SIZE(device->path));
   snprintf(device->path, ARRAY_SIZE(device->path), "%s", path);

   device->info = devinfo;

   device->local_fd = fd;
   result = anv_physical_device_get_parameters(device);
   if (result != VK_SUCCESS)
      goto fail_base;

   device->gtt_size = device->info.gtt_size ? device->info.gtt_size :
                                              device->info.aperture_bytes;

   if (device->gtt_size < (4ULL << 30 /* GiB */)) {
      vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                "GTT size too small: 0x%016"PRIx64, device->gtt_size);
      goto fail_base;
   }

   /* We currently only have the right bits for instructions in Gen12+. If the
    * kernel ever starts supporting that feature on previous generations,
    * we'll need to edit genxml prior to enabling here.
    */
   device->has_protected_contexts = device->info.ver >= 12 &&
      intel_gem_supports_protected_context(fd, device->info.kmd_type);

   /* Just pick one; they're all the same */
   device->has_astc_ldr =
      isl_format_supports_sampling(&device->info,
                                   ISL_FORMAT_ASTC_LDR_2D_4X4_FLT16);
   if (!device->has_astc_ldr &&
       driQueryOptionb(&device->instance->dri_options, "vk_require_astc"))
      device->emu_astc_ldr = true;
   if (devinfo.ver == 9 && !intel_device_info_is_9lp(&devinfo)) {
      device->flush_astc_ldr_void_extent_denorms =
         device->has_astc_ldr && !device->emu_astc_ldr;
   }
   device->disable_fcv = device->info.verx10 >= 125 ||
                         instance->disable_fcv;

   result = anv_physical_device_init_heaps(device, fd);
   if (result != VK_SUCCESS)
      goto fail_base;

   if (debug_get_bool_option("ANV_QUEUE_THREAD_DISABLE", false))
      device->has_exec_timeline = false;

   device->has_cooperative_matrix =
      device->info.cooperative_matrix_configurations[0].scope != INTEL_CMAT_SCOPE_NONE;

   unsigned st_idx = 0;

   device->sync_syncobj_type = vk_drm_syncobj_get_type(fd);
   if (!device->has_exec_timeline)
      device->sync_syncobj_type.features &= ~VK_SYNC_FEATURE_TIMELINE;
   device->sync_types[st_idx++] = &device->sync_syncobj_type;

   /* anv_bo_sync_type is only supported with i915 for now  */
   if (device->info.kmd_type == INTEL_KMD_TYPE_I915) {
      if (!(device->sync_syncobj_type.features & VK_SYNC_FEATURE_CPU_WAIT))
         device->sync_types[st_idx++] = &anv_bo_sync_type;

      if (!(device->sync_syncobj_type.features & VK_SYNC_FEATURE_TIMELINE)) {
         device->sync_timeline_type = vk_sync_timeline_get_type(&anv_bo_sync_type);
         device->sync_types[st_idx++] = &device->sync_timeline_type.sync;
      }
   } else {
      assert(vk_sync_type_is_drm_syncobj(&device->sync_syncobj_type));
      assert(device->sync_syncobj_type.features & VK_SYNC_FEATURE_TIMELINE);
      assert(device->sync_syncobj_type.features & VK_SYNC_FEATURE_CPU_WAIT);
   }

   device->sync_types[st_idx++] = NULL;
   assert(st_idx <= ARRAY_SIZE(device->sync_types));
   device->vk.supported_sync_types = device->sync_types;

   device->vk.pipeline_cache_import_ops = anv_cache_import_ops;

   device->always_use_bindless =
      debug_get_bool_option("ANV_ALWAYS_BINDLESS", false);

   device->use_call_secondary =
      !debug_get_bool_option("ANV_DISABLE_SECONDARY_CMD_BUFFER_CALLS", false);

   device->video_decode_enabled = debug_get_bool_option("ANV_VIDEO_DECODE", false);
   device->video_encode_enabled = debug_get_bool_option("ANV_VIDEO_ENCODE", false);

   device->uses_ex_bso = device->info.verx10 >= 125;

   /* For now always use indirect descriptors. We'll update this
    * to !uses_ex_bso when all the infrastructure is built up.
    */
   device->indirect_descriptors =
      !device->uses_ex_bso ||
      driQueryOptionb(&instance->dri_options, "force_indirect_descriptors");

   device->alloc_aux_tt_mem =
      device->info.has_aux_map && device->info.verx10 >= 125;
   /* Check if we can read the GPU timestamp register from the CPU */
   uint64_t u64_ignore;
   device->has_reg_timestamp = intel_gem_read_render_timestamp(fd,
                                                               device->info.kmd_type,
                                                               &u64_ignore);

   device->uses_relocs = device->info.kmd_type != INTEL_KMD_TYPE_XE;

   /* While xe.ko can use both vm_bind and TR-TT, i915.ko only has TR-TT. */
   if (debug_get_bool_option("ANV_SPARSE", true)) {
      if (device->info.kmd_type == INTEL_KMD_TYPE_XE) {
         if (debug_get_bool_option("ANV_SPARSE_USE_TRTT", false))
            device->sparse_type = ANV_SPARSE_TYPE_TRTT;
         else
            device->sparse_type = ANV_SPARSE_TYPE_VM_BIND;
      } else {
         if (device->info.ver >= 12 && device->has_exec_timeline)
            device->sparse_type = ANV_SPARSE_TYPE_TRTT;
      }
   }
   if (device->sparse_type == ANV_SPARSE_TYPE_NOT_SUPPORTED) {
      if (instance->has_fake_sparse)
         device->sparse_type = ANV_SPARSE_TYPE_FAKE;
   }

   device->always_flush_cache = INTEL_DEBUG(DEBUG_STALL) ||
      driQueryOptionb(&instance->dri_options, "always_flush_cache");

   device->compiler = brw_compiler_create(NULL, &device->info);
   if (device->compiler == NULL) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_base;
   }
   device->compiler->shader_debug_log = compiler_debug_log;
   device->compiler->shader_perf_log = compiler_perf_log;
   device->compiler->extended_bindless_surface_offset = device->uses_ex_bso;
   device->compiler->use_bindless_sampler_offset = false;
   device->compiler->spilling_rate =
      driQueryOptioni(&instance->dri_options, "shader_spilling_rate");

   isl_device_init(&device->isl_dev, &device->info);
   device->isl_dev.buffer_length_in_aux_addr = !intel_needs_workaround(device->isl_dev.info, 14019708328);
   device->isl_dev.sampler_route_to_lsc =
      driQueryOptionb(&instance->dri_options, "intel_sampler_route_to_lsc");

   result = anv_physical_device_init_uuids(device);
   if (result != VK_SUCCESS)
      goto fail_compiler;

   anv_physical_device_init_va_ranges(device);

   anv_physical_device_init_disk_cache(device);

   if (instance->vk.enabled_extensions.KHR_display) {
      master_fd = open(primary_path, O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         /* fail if we don't have permission to even render on this device */
         if (!intel_gem_can_render_on_fd(master_fd, device->info.kmd_type)) {
            close(master_fd);
            master_fd = -1;
         }
      }
   }
   device->master_fd = master_fd;

   device->engine_info = intel_engine_get_info(fd, device->info.kmd_type);
   intel_common_update_device_info(fd, &device->info);

   anv_physical_device_init_queue_families(device);

   anv_physical_device_init_perf(device, fd);

   /* Gather major/minor before WSI. */
   struct stat st;

   if (stat(primary_path, &st) == 0) {
      device->has_master = true;
      device->master_major = major(st.st_rdev);
      device->master_minor = minor(st.st_rdev);
   } else {
      device->has_master = false;
      device->master_major = 0;
      device->master_minor = 0;
   }

   if (stat(path, &st) == 0) {
      device->has_local = true;
      device->local_major = major(st.st_rdev);
      device->local_minor = minor(st.st_rdev);
   } else {
      device->has_local = false;
      device->local_major = 0;
      device->local_minor = 0;
   }

   device->has_small_bar = anv_physical_device_has_vram(device) &&
                           device->vram_non_mappable.size != 0;

   get_device_extensions(device, &device->vk.supported_extensions);
   get_features(device, &device->vk.supported_features);
   get_properties(device, &device->vk.properties);

   result = anv_init_wsi(device);
   if (result != VK_SUCCESS)
      goto fail_perf;

   anv_measure_device_init(device);

   anv_genX(&device->info, init_physical_device_state)(device);

   *out = &device->vk;

   return VK_SUCCESS;

fail_perf:
   intel_perf_free(device->perf);
   free(device->engine_info);
   anv_physical_device_free_disk_cache(device);
fail_compiler:
   ralloc_free(device->compiler);
fail_base:
   vk_physical_device_finish(&device->vk);
fail_alloc:
   vk_free(&instance->vk.alloc, device);
fail_fd:
   close(fd);
   if (master_fd != -1)
      close(master_fd);
   return result;
}

void
anv_physical_device_destroy(struct vk_physical_device *vk_device)
{
   struct anv_physical_device *device =
      container_of(vk_device, struct anv_physical_device, vk);

   anv_finish_wsi(device);
   anv_measure_device_destroy(device);
   free(device->engine_info);
   anv_physical_device_free_disk_cache(device);
   ralloc_free(device->compiler);
   intel_perf_free(device->perf);
   close(device->local_fd);
   if (device->master_fd >= 0)
      close(device->master_fd);
   vk_physical_device_finish(&device->vk);
   vk_free(&device->instance->vk.alloc, device);
}

static const VkQueueFamilyProperties
get_anv_queue_family_properties_template(const struct anv_physical_device *device)
{

   /*
    * For Xe2+:
    * Bspec 60411: Timestamp register can hold 64-bit value
    *
    * Platforms < Xe2:
    * Bpsec 46111: Timestamp register can hold only 36-bit
    *              value
    */
   const VkQueueFamilyProperties anv_queue_family_properties_template =
   {
      .timestampValidBits = device->info.ver >= 20 ? 64 : 36,
      .minImageTransferGranularity = { 1, 1, 1 },
   };

   return anv_queue_family_properties_template;
}

static VkQueueFamilyProperties
anv_device_physical_get_queue_properties(const struct anv_physical_device *device,
                                         uint32_t family_index)
{
   const struct anv_queue_family *family = &device->queue.families[family_index];
   VkQueueFamilyProperties properties =
      get_anv_queue_family_properties_template(device);

   properties.queueFlags = family->queueFlags;
   properties.queueCount = family->queueCount;
   return properties;
}

void anv_GetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2*                   pQueueFamilyProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out,
                          pQueueFamilyProperties, pQueueFamilyPropertyCount);

   for (uint32_t i = 0; i < pdevice->queue.family_count; i++) {
      struct anv_queue_family *queue_family = &pdevice->queue.families[i];
      vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
         p->queueFamilyProperties =
            anv_device_physical_get_queue_properties(pdevice, i);

         vk_foreach_struct(ext, p->pNext) {
            switch (ext->sType) {
            case VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR: {
               VkQueueFamilyGlobalPriorityPropertiesKHR *properties =
                  (VkQueueFamilyGlobalPriorityPropertiesKHR *)ext;

               /* Deliberately sorted low to high */
               VkQueueGlobalPriorityKHR all_priorities[] = {
                  VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR,
                  VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR,
                  VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR,
                  VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR,
               };

               uint32_t count = 0;
               for (unsigned i = 0; i < ARRAY_SIZE(all_priorities); i++) {
                  if (all_priorities[i] > pdevice->max_context_priority)
                     break;

                  properties->priorities[count++] = all_priorities[i];
               }
               properties->priorityCount = count;
               break;
            }
            case VK_STRUCTURE_TYPE_QUEUE_FAMILY_QUERY_RESULT_STATUS_PROPERTIES_KHR: {
               VkQueueFamilyQueryResultStatusPropertiesKHR *prop =
                  (VkQueueFamilyQueryResultStatusPropertiesKHR *)ext;
               prop->queryResultStatusSupport = VK_TRUE;
               break;
            }
            case VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR: {
               VkQueueFamilyVideoPropertiesKHR *prop =
                  (VkQueueFamilyVideoPropertiesKHR *)ext;
               if (queue_family->queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {
                  prop->videoCodecOperations = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR |
                                               VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
                  if (pdevice->info.ver >= 12)
                     prop->videoCodecOperations |= VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR;
               }

               if (queue_family->queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) {
                  prop->videoCodecOperations |= VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR |
                                                VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR;
               }
               break;
            }
            default:
               vk_debug_ignored_stype(ext->sType);
            }
         }
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

   if (!device->vk.supported_extensions.EXT_memory_budget)
      return;

   anv_update_meminfo(device, device->local_fd);

   VkDeviceSize total_sys_heaps_size = 0, total_vram_heaps_size = 0;
   for (size_t i = 0; i < device->memory.heap_count; i++) {
      if (device->memory.heaps[i].is_local_mem) {
         total_vram_heaps_size += device->memory.heaps[i].size;
      } else {
         total_sys_heaps_size += device->memory.heaps[i].size;
      }
   }

   for (size_t i = 0; i < device->memory.heap_count; i++) {
      VkDeviceSize heap_size = device->memory.heaps[i].size;
      VkDeviceSize heap_used = device->memory.heaps[i].used;
      VkDeviceSize heap_budget, total_heaps_size;
      uint64_t mem_available = 0;

      if (device->memory.heaps[i].is_local_mem) {
         total_heaps_size = total_vram_heaps_size;
         if (device->vram_non_mappable.size > 0 && i == 0) {
            mem_available = device->vram_non_mappable.available;
         } else {
            mem_available = device->vram_mappable.available;
         }
      } else {
         total_heaps_size = total_sys_heaps_size;
         mem_available = MIN2(device->sys.available, total_heaps_size);
      }

      double heap_proportion = (double) heap_size / total_heaps_size;
      VkDeviceSize available_prop = mem_available * heap_proportion;

      /*
       * Let's not incite the app to starve the system: report at most 90% of
       * the available heap memory.
       */
      uint64_t heap_available = available_prop * 9 / 10;
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
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void anv_GetPhysicalDeviceMultisamplePropertiesEXT(
    VkPhysicalDevice                            physicalDevice,
    VkSampleCountFlagBits                       samples,
    VkMultisamplePropertiesEXT*                 pMultisampleProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);

   assert(pMultisampleProperties->sType ==
          VK_STRUCTURE_TYPE_MULTISAMPLE_PROPERTIES_EXT);

   VkExtent2D grid_size;
   if (samples & isl_device_get_sample_counts(&physical_device->isl_dev)) {
      grid_size.width = 1;
      grid_size.height = 1;
   } else {
      grid_size.width = 0;
      grid_size.height = 0;
   }
   pMultisampleProperties->maxSampleLocationGridSize = grid_size;

   vk_foreach_struct(ext, pMultisampleProperties->pNext)
      vk_debug_ignored_stype(ext->sType);
}

VkResult anv_GetPhysicalDeviceFragmentShadingRatesKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pFragmentShadingRateCount,
    VkPhysicalDeviceFragmentShadingRateKHR*     pFragmentShadingRates)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceFragmentShadingRateKHR, out,
                          pFragmentShadingRates, pFragmentShadingRateCount);

#define append_rate(_samples, _width, _height)                                      \
   do {                                                                             \
      vk_outarray_append_typed(VkPhysicalDeviceFragmentShadingRateKHR, &out, __r) { \
         __r->sampleCounts = _samples;                                              \
         __r->fragmentSize = (VkExtent2D) {                                         \
            .width = _width,                                                        \
            .height = _height,                                                      \
         };                                                                         \
      }                                                                             \
   } while (0)

   VkSampleCountFlags sample_counts =
      isl_device_get_sample_counts(&physical_device->isl_dev);

   /* BSpec 47003: There are a number of restrictions on the sample count
    * based off the coarse pixel size.
    */
   static const VkSampleCountFlags cp_size_sample_limits[] = {
      [1]  = ISL_SAMPLE_COUNT_16_BIT | ISL_SAMPLE_COUNT_8_BIT |
             ISL_SAMPLE_COUNT_4_BIT | ISL_SAMPLE_COUNT_2_BIT | ISL_SAMPLE_COUNT_1_BIT,
      [2]  = ISL_SAMPLE_COUNT_4_BIT | ISL_SAMPLE_COUNT_2_BIT | ISL_SAMPLE_COUNT_1_BIT,
      [4]  = ISL_SAMPLE_COUNT_4_BIT | ISL_SAMPLE_COUNT_2_BIT | ISL_SAMPLE_COUNT_1_BIT,
      [8]  = ISL_SAMPLE_COUNT_2_BIT | ISL_SAMPLE_COUNT_1_BIT,
      [16] = ISL_SAMPLE_COUNT_1_BIT,
   };

   for (uint32_t x = 4; x >= 1; x /= 2) {
       for (uint32_t y = 4; y >= 1; y /= 2) {
          if (physical_device->info.has_coarse_pixel_primitive_and_cb) {
             /* BSpec 47003:
              *   "CPsize 1x4 and 4x1 are not supported"
              */
             if ((x == 1 && y == 4) || (x == 4 && y == 1))
                continue;

             /* For size {1, 1}, the sample count must be ~0
              *
              * 4x2 is also a specially case.
              */
             if (x == 1 && y == 1)
                append_rate(~0, x, y);
             else if (x == 4 && y == 2)
                append_rate(ISL_SAMPLE_COUNT_1_BIT, x, y);
             else
                append_rate(cp_size_sample_limits[x * y], x, y);
          } else {
             /* For size {1, 1}, the sample count must be ~0 */
             if (x == 1 && y == 1)
                append_rate(~0, x, y);
             else
                append_rate(sample_counts, x, y);
          }
       }
   }

#undef append_rate

   return vk_outarray_status(&out);
}

static VkComponentTypeKHR
convert_component_type(enum intel_cooperative_matrix_component_type t)
{
   switch (t) {
   case INTEL_CMAT_FLOAT16: return VK_COMPONENT_TYPE_FLOAT16_KHR;
   case INTEL_CMAT_FLOAT32: return VK_COMPONENT_TYPE_FLOAT32_KHR;
   case INTEL_CMAT_SINT32:  return VK_COMPONENT_TYPE_SINT32_KHR;
   case INTEL_CMAT_SINT8:   return VK_COMPONENT_TYPE_SINT8_KHR;
   case INTEL_CMAT_UINT32:  return VK_COMPONENT_TYPE_UINT32_KHR;
   case INTEL_CMAT_UINT8:   return VK_COMPONENT_TYPE_UINT8_KHR;
   }
   unreachable("invalid cooperative matrix component type in configuration");
}

static VkScopeKHR
convert_scope(enum intel_cmat_scope scope)
{
   switch (scope) {
   case INTEL_CMAT_SCOPE_SUBGROUP: return VK_SCOPE_SUBGROUP_KHR;
   default:
      unreachable("invalid cooperative matrix scope in configuration");
   }
}

VkResult anv_GetPhysicalDeviceCooperativeMatrixPropertiesKHR(
   VkPhysicalDevice                            physicalDevice,
   uint32_t*                                   pPropertyCount,
   VkCooperativeMatrixPropertiesKHR*           pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);
   const struct intel_device_info *devinfo = &pdevice->info;

   assert(anv_has_cooperative_matrix(pdevice));

   VK_OUTARRAY_MAKE_TYPED(VkCooperativeMatrixPropertiesKHR, out, pProperties, pPropertyCount);

   for (int i = 0; i < ARRAY_SIZE(devinfo->cooperative_matrix_configurations); i++) {
      const struct intel_cooperative_matrix_configuration *cfg =
         &devinfo->cooperative_matrix_configurations[i];

      if (cfg->scope == INTEL_CMAT_SCOPE_NONE)
         break;

      vk_outarray_append_typed(VkCooperativeMatrixPropertiesKHR, &out, prop) {
         prop->sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;

         prop->MSize = cfg->m;
         prop->NSize = cfg->n;
         prop->KSize = cfg->k;

         prop->AType      = convert_component_type(cfg->a);
         prop->BType      = convert_component_type(cfg->b);
         prop->CType      = convert_component_type(cfg->c);
         prop->ResultType = convert_component_type(cfg->result);

         prop->saturatingAccumulation = VK_FALSE;
         prop->scope = convert_scope(cfg->scope);
      }

      /* VUID-RuntimeSpirv-saturatingAccumulation-08983 says:
       *
       *    For OpCooperativeMatrixMulAddKHR, the SaturatingAccumulation
       *    cooperative matrix operand must be present if and only if
       *    VkCooperativeMatrixPropertiesKHR::saturatingAccumulation is
       *    VK_TRUE.
       *
       * As a result, we have to advertise integer configs both with and
       * without this flag set.
       *
       * The DPAS instruction does not support the .sat modifier, so only
       * advertise the configurations when the DPAS would be lowered.
       *
       * FINISHME: It should be possible to do better than full lowering on
       * platforms that support DPAS. Emit a DPAS with a NULL accumulator
       * argument, then perform the correct sequence of saturating add
       * instructions.
       */
      if (cfg->a != INTEL_CMAT_FLOAT16 &&
          (devinfo->verx10 < 125 || debug_get_bool_option("INTEL_LOWER_DPAS", false))) {
         vk_outarray_append_typed(VkCooperativeMatrixPropertiesKHR, &out, prop) {
            prop->sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR;

            prop->MSize = cfg->m;
            prop->NSize = cfg->n;
            prop->KSize = cfg->k;

            prop->AType      = convert_component_type(cfg->a);
            prop->BType      = convert_component_type(cfg->b);
            prop->CType      = convert_component_type(cfg->c);
            prop->ResultType = convert_component_type(cfg->result);

            prop->saturatingAccumulation = VK_TRUE;
            prop->scope = convert_scope(cfg->scope);
         }
      }
   }

   return vk_outarray_status(&out);
}
