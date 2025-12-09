/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_device.h"

#include "drm-uapi/drm_fourcc.h"
#include "fdl/freedreno_layout.h"
#include <fcntl.h>
#include <poll.h>

#include "git_sha1.h"
#include "util/cache_ops.h"
#include "util/u_debug.h"
#include "util/disk_cache.h"
#include "util/hex.h"
#include "util/driconf.h"
#include "util/os_misc.h"
#include "util/u_process.h"
#include "vk_android.h"
#include "vk_shader_module.h"
#include "vk_sampler.h"
#include "vk_util.h"

/* for fd_get_driver/device_uuid() */
#include "freedreno/common/freedreno_uuid.h"
#include "freedreno/common/freedreno_stompable_regs.h"

#include "tu_acceleration_structure.h"
#include "tu_clear_blit.h"
#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_descriptor_set.h"
#include "tu_dynamic_rendering.h"
#include "tu_image.h"
#include "tu_pass.h"
#include "tu_queue.h"
#include "tu_query_pool.h"
#include "tu_rmv.h"
#include "tu_tracepoints.h"
#include "tu_wsi.h"

#if DETECT_OS_ANDROID
#include <vndk/hardware_buffer.h>
#endif

uint64_t os_page_size = 4096;

static int
tu_device_get_cache_uuid(struct tu_physical_device *device, void *uuid)
{
   struct mesa_sha1 ctx;
   unsigned char sha1[20];
   /* Note: IR3_SHADER_DEBUG also affects compilation, but it's not
    * initialized until after compiler creation so we have to add it to the
    * shader hash instead, since the compiler is only created with the logical
    * device.
    */
   uint64_t driver_flags = TU_DEBUG(NOMULTIPOS);
   uint16_t family = fd_dev_gpu_id(&device->dev_id);

   memset(uuid, 0, VK_UUID_SIZE);
   _mesa_sha1_init(&ctx);

   if (!disk_cache_get_function_identifier((void *)tu_device_get_cache_uuid, &ctx))
      return -1;

   _mesa_sha1_update(&ctx, &family, sizeof(family));
   _mesa_sha1_update(&ctx, &driver_flags, sizeof(driver_flags));
   _mesa_sha1_update(&ctx, &device->uche_trap_base, sizeof(device->uche_trap_base));
   _mesa_sha1_final(&ctx, sha1);

   memcpy(uuid, sha1, VK_UUID_SIZE);
   return 0;
}

#define TU_API_VERSION VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION)

VKAPI_ATTR VkResult VKAPI_CALL
tu_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
    *pApiVersion = TU_API_VERSION;
    return VK_SUCCESS;
}

static const struct vk_instance_extension_table tu_instance_extensions_supported = { .table = {
   .KHR_device_group_creation           = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display                         = true,
#endif
   .KHR_external_fence_capabilities     = true,
   .KHR_external_memory_capabilities    = true,
   .KHR_external_semaphore_capabilities = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_get_display_properties2         = true,
#endif
   .KHR_get_physical_device_properties2 = true,
#ifdef TU_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2       = true,
   .KHR_surface                         = true,
   .KHR_surface_maintenance1            = true,
   .KHR_surface_protected_capabilities  = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface                 = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface                     = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface                    = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .EXT_acquire_drm_display             = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display            = true,
#endif
   .EXT_debug_report                    = true,
   .EXT_debug_utils                     = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .EXT_direct_mode_display             = true,
   .EXT_display_surface_counter         = true,
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
   .EXT_headless_surface                = true,
#endif
#ifdef TU_USE_WSI_PLATFORM
   .EXT_surface_maintenance1            = true,
   .EXT_swapchain_colorspace            = true,
#endif
} };

static bool
is_kgsl(struct tu_instance *instance)
{
   return strcmp(instance->knl->name, "kgsl") == 0;
}

static bool tu_has_multiview(const struct tu_physical_device *device)
{
   return device->info->props.has_hw_multiview || TU_DEBUG(NOCONFORM);
}

/* We are generally VK 1.1 except A702, which has no multiview */
static bool tu_is_vk_1_1(const struct tu_physical_device *device)
{
   return tu_has_multiview(device);
}

static void
get_device_extensions(const struct tu_physical_device *device,
                      struct vk_device_extension_table *ext)
{
   /* device->has_raytracing contains the value of the SW fuse. If the
    * device doesn't have a fuse (i.e. a740), we have to ignore it because
    * kgsl returns false. If it does have a fuse, enable raytracing if the
    * fuse is set and we have ray_intersection.
    */
   bool has_raytracing =
      device->info->props.has_ray_intersection &&
      (!device->info->props.has_sw_fuse || device->has_raytracing);

   *ext = (struct vk_device_extension_table) { .table = {
      .KHR_8bit_storage = device->info->props.storage_8bit,
      .KHR_16bit_storage = device->info->props.storage_16bit,
      .KHR_acceleration_structure = has_raytracing,
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_calibrated_timestamps = device->info->props.has_persistent_counter,
      .KHR_compute_shader_derivatives = device->info->chip >= 7,
      .KHR_copy_commands2 = true,
      // TODO workaround for https://github.com/KhronosGroup/VK-GL-CTS/issues/525
      .KHR_create_renderpass2 = true, // tu_has_multiview(device),
      .KHR_dedicated_allocation = true,
      .KHR_deferred_host_operations = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_descriptor_update_template = true,
      .KHR_device_group = true,
      .KHR_draw_indirect_count = true,
      .KHR_driver_properties = true,
      .KHR_dynamic_rendering = true,
      .KHR_dynamic_rendering_local_read = true,
      .KHR_external_fence = true,
      .KHR_external_fence_fd = true,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = true,
      .KHR_external_semaphore_fd = true,
      .KHR_format_feature_flags2 = true,
      .KHR_fragment_shading_rate = device->info->props.has_attachment_shading_rate,
      .KHR_get_memory_requirements2 = true,
      .KHR_global_priority = tu_is_vk_1_1(device),
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
#ifdef TU_USE_WSI_PLATFORM
      .KHR_incremental_present = true,
#endif
      .KHR_index_type_uint8 = true,
      .KHR_line_rasterization = !device->info->props.is_a702,
      .KHR_load_store_op_none = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_maintenance4 = tu_is_vk_1_1(device),
      .KHR_maintenance5 = tu_is_vk_1_1(device),
      .KHR_maintenance6 = tu_is_vk_1_1(device),
      .KHR_maintenance7 = tu_is_vk_1_1(device),
      .KHR_maintenance8 = tu_is_vk_1_1(device),
      .KHR_map_memory2 = true,
      .KHR_multiview = tu_has_multiview(device),
      .KHR_performance_query = TU_DEBUG(PERFC) || TU_DEBUG(PERFCRAW),
      .KHR_pipeline_executable_properties = true,
      .KHR_pipeline_library = true,
#ifdef TU_USE_WSI_PLATFORM
      .KHR_present_id = true,
      .KHR_present_id2 = true,
      .KHR_present_wait = true,
      .KHR_present_wait2 = true,
#endif
      .KHR_push_descriptor = true,
      .KHR_ray_query = has_raytracing,
      .KHR_ray_tracing_maintenance1 = has_raytracing,
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_atomic_int64 = device->info->props.has_64b_ssbo_atomics,
      .KHR_shader_clock = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_expect_assume = true,
      .KHR_shader_float16_int8 = true,
      .KHR_shader_float_controls = true,
      .KHR_shader_float_controls2 = tu_is_vk_1_1(device),
      .KHR_shader_integer_dot_product = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_relaxed_extended_instruction = true,
      .KHR_shader_subgroup_extended_types = tu_is_vk_1_1(device),
      .KHR_shader_subgroup_rotate = true,
      .KHR_shader_subgroup_uniform_control_flow = tu_is_vk_1_1(device),
      .KHR_shader_terminate_invocation = true,
      .KHR_spirv_1_4 = tu_is_vk_1_1(device),
      .KHR_storage_buffer_storage_class = true,
#ifdef TU_USE_WSI_PLATFORM
      .KHR_swapchain = true,
      .KHR_swapchain_maintenance1 = true,
      .KHR_swapchain_mutable_format = true,
#endif
      .KHR_synchronization2 = true,
      .KHR_timeline_semaphore = true,
      .KHR_unified_image_layouts = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .KHR_vertex_attribute_divisor = true,
      .KHR_vulkan_memory_model = true,
      .KHR_workgroup_memory_explicit_layout = true,
      .KHR_zero_initialize_workgroup_memory = true,

      .EXT_4444_formats = true,
      .EXT_attachment_feedback_loop_dynamic_state = true,
      .EXT_attachment_feedback_loop_layout = true,
      .EXT_border_color_swizzle = true,
      .EXT_calibrated_timestamps = device->info->props.has_persistent_counter,
      .EXT_color_write_enable = true,
      .EXT_conditional_rendering = true,
      .EXT_conservative_rasterization = device->info->chip >= 7,
      .EXT_custom_border_color = true,
      .EXT_custom_resolve = true,
      .EXT_depth_clamp_zero_one = true,
      .EXT_depth_clip_control = true,
      .EXT_depth_clip_enable = true,
      .EXT_descriptor_buffer = true,
      .EXT_descriptor_indexing = true,
      .EXT_device_address_binding_report = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
      .EXT_display_control = true,
#endif
      .EXT_dynamic_rendering_unused_attachments = true,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_extended_dynamic_state3 = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_filter_cubic = device->info->props.has_tex_filter_cubic,
      .EXT_fragment_density_map = true,
      .EXT_fragment_density_map_offset = true,
      .EXT_global_priority = tu_is_vk_1_1(device),
      .EXT_global_priority_query = tu_is_vk_1_1(device),
      .EXT_graphics_pipeline_library = true,
      .EXT_hdr_metadata = true,
      .EXT_host_image_copy = true,
      .EXT_host_query_reset = true,
      .EXT_image_2d_view_of_3d = true,
      .EXT_image_drm_format_modifier = true,
      .EXT_image_robustness = true,
      .EXT_image_view_min_lod = true,
      .EXT_index_type_uint8 = true,
      .EXT_inline_uniform_block = true,
      .EXT_legacy_dithering = true,
      .EXT_legacy_vertex_attributes = true,
      .EXT_line_rasterization = true,
      .EXT_load_store_op_none = true,
      .EXT_map_memory_placed = true,
      .EXT_memory_budget = true,
      .EXT_multi_draw = true,
      .EXT_multisampled_render_to_single_sampled = true,
      .EXT_mutable_descriptor_type = true,
      .EXT_nested_command_buffer = true,
      .EXT_non_seamless_cube_map = true,
      .EXT_physical_device_drm = !is_kgsl(device->instance),
      .EXT_pipeline_creation_cache_control = true,
      .EXT_pipeline_creation_feedback = true,
      .EXT_primitive_topology_list_restart = true,
      .EXT_primitives_generated_query = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_rasterization_order_attachment_access = true,
      .EXT_robustness2 = true,
      .EXT_sample_locations = device->info->props.has_sample_locations,
      .EXT_sampler_filter_minmax = device->info->props.has_sampler_minmax,
      .EXT_scalar_block_layout = true,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_atomic_float = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_module_identifier = true,
      .EXT_shader_replicated_composites = true,
      .EXT_shader_stencil_export = true,
      .EXT_shader_viewport_index_layer = tu_has_multiview(device),
      .EXT_subgroup_size_control = tu_is_vk_1_1(device),
#ifdef TU_USE_WSI_PLATFORM
      .EXT_swapchain_maintenance1 = true,
#endif
      .EXT_texel_buffer_alignment = true,
      .EXT_tooling_info = true,
      .EXT_transform_feedback = true,
      .EXT_vertex_attribute_divisor = true,
      .EXT_vertex_input_dynamic_state = true,
      .EXT_zero_initialize_device_memory = true,

      /* For Graphics Flight Recorder (GFR) */
      .AMD_buffer_marker = true,
      .ARM_rasterization_order_attachment_access = true,
      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,
      .IMG_filter_cubic = device->info->props.has_tex_filter_cubic,
      .NV_compute_shader_derivatives = device->info->chip >= 7,
      .QCOM_fragment_density_map_offset = true,
      .QCOM_render_pass_shader_resolve = true,
      .VALVE_fragment_density_map_layered = true,
      .VALVE_mutable_descriptor_type = true,
   } };

#if DETECT_OS_ANDROID
   if (vk_android_get_ugralloc() != NULL) {
      ext->ANDROID_external_memory_android_hardware_buffer = true,
      ext->ANDROID_native_buffer = true;
   }
#endif
}

static void
tu_get_features(struct tu_physical_device *pdevice,
                struct vk_features *features)
{
   *features = (struct vk_features) { false };

   /* Vulkan 1.0 */
   features->robustBufferAccess = true;
   features->fullDrawIndexUint32 = true;
   features->imageCubeArray = true;
   features->independentBlend = true;
   features->geometryShader = !pdevice->info->props.is_a702;
   features->tessellationShader = !pdevice->info->props.is_a702;
   features->sampleRateShading = true;
   features->dualSrcBlend = true;
   features->logicOp = true;
   features->multiDrawIndirect = true;
   features->drawIndirectFirstInstance = true;
   features->depthClamp = true;
   features->depthBiasClamp = !pdevice->info->props.is_a702;
   features->fillModeNonSolid = true;
   features->depthBounds = true;
   features->wideLines = pdevice->info->props.line_width_max > 1.0;
   features->largePoints = true;
   features->alphaToOne = true;
   features->multiViewport = tu_has_multiview(pdevice);
   features->samplerAnisotropy = true;
   features->textureCompressionETC2 = true;
   features->textureCompressionASTC_LDR = true;
   /* no BC6H & BC7 support on A702 */
   features->textureCompressionBC = !pdevice->info->props.is_a702;
   features->occlusionQueryPrecise = true;
   features->pipelineStatisticsQuery = true;
   features->vertexPipelineStoresAndAtomics = true;
   features->fragmentStoresAndAtomics = true;
   features->shaderTessellationAndGeometryPointSize = !pdevice->info->props.is_a702;
   features->shaderImageGatherExtended = true;
   features->shaderStorageImageExtendedFormats = true;
   features->shaderStorageImageMultisample = false;
   features->shaderStorageImageReadWithoutFormat = true;
   features->shaderStorageImageWriteWithoutFormat = true;
   features->shaderUniformBufferArrayDynamicIndexing = true;
   features->shaderSampledImageArrayDynamicIndexing = true;
   features->shaderStorageBufferArrayDynamicIndexing = true;
   features->shaderStorageImageArrayDynamicIndexing = true;
   features->shaderClipDistance = true;
   features->shaderCullDistance = true;
   features->shaderFloat64 = false;
   features->shaderInt64 = true;
   features->shaderInt16 = true;
   features->sparseBinding = pdevice->has_sparse;
   features->sparseResidencyBuffer = pdevice->has_sparse_prr;
   features->sparseResidencyImage2D = pdevice->has_sparse_prr &&
      pdevice->info->props.ubwc_all_formats_compatible;
   features->sparseResidency2Samples = features->sparseResidencyImage2D;
   features->sparseResidency4Samples = features->sparseResidencyImage2D;
   features->sparseResidency8Samples = features->sparseResidencyImage2D;
   features->sparseResidencyAliased = pdevice->has_sparse_prr;
   features->shaderResourceResidency = pdevice->has_sparse_prr;
   features->shaderResourceMinLod = true;
   features->variableMultisampleRate = true;
   features->inheritedQueries = true;

   /* Vulkan 1.1 */
   features->storageBuffer16BitAccess            = pdevice->info->props.storage_16bit;
   features->uniformAndStorageBuffer16BitAccess  = false;
   features->storagePushConstant16               = false;
   features->storageInputOutput16                = false;
   features->multiview                           = true;
   features->multiviewGeometryShader             = false;
   features->multiviewTessellationShader         = false;
   features->variablePointersStorageBuffer       = true;
   features->variablePointers                    = true;
   features->protectedMemory                     = false;
   features->samplerYcbcrConversion              = true;
   features->shaderDrawParameters                = true;

   /* Vulkan 1.2 */
   features->samplerMirrorClampToEdge            = true;
   features->drawIndirectCount                   = true;
   features->storageBuffer8BitAccess             = pdevice->info->props.storage_8bit;
   features->uniformAndStorageBuffer8BitAccess   = false;
   features->storagePushConstant8                = false;
   features->shaderBufferInt64Atomics =
      pdevice->info->props.has_64b_ssbo_atomics;
   features->shaderSharedInt64Atomics            = false;
   features->shaderFloat16                       = true;
   features->shaderInt8                          = true;

   features->descriptorIndexing                                 = true;
   features->shaderInputAttachmentArrayDynamicIndexing          = false;
   features->shaderUniformTexelBufferArrayDynamicIndexing       = true;
   features->shaderStorageTexelBufferArrayDynamicIndexing       = true;
   features->shaderUniformBufferArrayNonUniformIndexing         = true;
   features->shaderSampledImageArrayNonUniformIndexing          = true;
   features->shaderStorageBufferArrayNonUniformIndexing         = true;
   features->shaderStorageImageArrayNonUniformIndexing          = true;
   features->shaderInputAttachmentArrayNonUniformIndexing       = false;
   features->shaderUniformTexelBufferArrayNonUniformIndexing    = true;
   features->shaderStorageTexelBufferArrayNonUniformIndexing    = true;
   features->descriptorBindingUniformBufferUpdateAfterBind      = true;
   features->descriptorBindingSampledImageUpdateAfterBind       = true;
   features->descriptorBindingStorageImageUpdateAfterBind       = true;
   features->descriptorBindingStorageBufferUpdateAfterBind      = true;
   features->descriptorBindingUniformTexelBufferUpdateAfterBind = true;
   features->descriptorBindingStorageTexelBufferUpdateAfterBind = true;
   features->descriptorBindingUpdateUnusedWhilePending          = true;
   features->descriptorBindingPartiallyBound                    = true;
   features->descriptorBindingVariableDescriptorCount           = true;
   features->runtimeDescriptorArray                             = true;

   features->samplerFilterMinmax                 =
      pdevice->info->props.has_sampler_minmax;
   features->scalarBlockLayout                   = true;
   features->imagelessFramebuffer                = true;
   features->uniformBufferStandardLayout         = true;
   features->shaderSubgroupExtendedTypes         = true;
   features->separateDepthStencilLayouts         = true;
   features->hostQueryReset                      = true;
   features->timelineSemaphore                   = true;
   features->bufferDeviceAddress                 = true;
   features->bufferDeviceAddressCaptureReplay    = pdevice->has_set_iova;
   features->bufferDeviceAddressMultiDevice      = false;
   features->vulkanMemoryModel                   = true;
   features->vulkanMemoryModelDeviceScope        = true;
   features->vulkanMemoryModelAvailabilityVisibilityChains = true;
   features->shaderOutputViewportIndex           = true;
   features->shaderOutputLayer                   = true;
   features->subgroupBroadcastDynamicId          = true;

   /* Vulkan 1.3 */
   features->robustImageAccess                   = true;
   features->inlineUniformBlock                  = true;
   features->descriptorBindingInlineUniformBlockUpdateAfterBind = true;
   features->pipelineCreationCacheControl        = true;
   features->privateData                         = true;
   features->shaderDemoteToHelperInvocation      = true;
   features->shaderTerminateInvocation           = true;
   features->subgroupSizeControl                 = true;
   features->computeFullSubgroups                = true;
   features->synchronization2                    = true;
   features->textureCompressionASTC_HDR          = false;
   features->shaderZeroInitializeWorkgroupMemory = true;
   features->dynamicRendering                    = true;
   features->shaderIntegerDotProduct             = true;
   features->maintenance4                        = true;

   /* Vulkan 1.4 */
   features->pushDescriptor = true;

   /* VK_KHR_acceleration_structure */
   features->accelerationStructure = true;
   features->accelerationStructureCaptureReplay = pdevice->has_set_iova;
   features->descriptorBindingAccelerationStructureUpdateAfterBind = true;

   /* VK_KHR_compute_shader_derivatives */
   features->computeDerivativeGroupQuads = pdevice->info->chip >= 7;
   features->computeDerivativeGroupLinear = pdevice->info->chip >= 7;

   /* VK_KHR_dynamic_rendering_local_read */
   features->dynamicRenderingLocalRead = true;

   /* VK_KHR_fragment_shading_rate */
   features->pipelineFragmentShadingRate = pdevice->info->props.has_attachment_shading_rate;
   features->primitiveFragmentShadingRate = pdevice->info->props.has_primitive_shading_rate;
   features->attachmentFragmentShadingRate = pdevice->info->props.has_attachment_shading_rate;

   /* VK_KHR_index_type_uint8 */
   features->indexTypeUint8 = true;

   /* VK_KHR_line_rasterization */
   features->rectangularLines = !pdevice->info->props.is_a702;
   features->bresenhamLines = true;
   features->smoothLines = false;
   features->stippledRectangularLines = false;
   features->stippledBresenhamLines = false;
   features->stippledSmoothLines = false;

   /* VK_KHR_maintenance5 */
   features->maintenance5 = true;

   /* VK_KHR_maintenance6 */
   features->maintenance6 = true;

   /* VK_KHR_maintenance7 */
   features->maintenance7 = true;

   /* VK_KHR_maintenance8 */
   features->maintenance8 = true;

   /* VK_KHR_performance_query */
   features->performanceCounterQueryPools = true;
   features->performanceCounterMultipleQueryPools = false;

   /* VK_KHR_pipeline_executable_properties */
   features->pipelineExecutableInfo = true;

#ifdef TU_USE_WSI_PLATFORM
   /* VK_KHR_present_id */
   features->presentId = true;

   /* VK_KHR_present_wait */
   features->presentWait = true;
#endif

   /* VK_KHR_shader_clock */
   features->shaderSubgroupClock = true;
   features->shaderDeviceClock = true;

   /* VK_KHR_shader_expect_assume */
   features->shaderExpectAssume = true;

   /* VK_KHR_shader_float_controls2 */
   features->shaderFloatControls2 = true;

   /* VK_KHR_shader_subgroup_uniform_control_flow */
   features->shaderSubgroupUniformControlFlow = true;

   /* VK_KHR_vertex_attribute_divisor */
   features->vertexAttributeInstanceRateDivisor = true;
   features->vertexAttributeInstanceRateZeroDivisor = true;

   /* VK_KHR_workgroup_memory_explicit_layout */
   features->workgroupMemoryExplicitLayout = true;
   features->workgroupMemoryExplicitLayoutScalarBlockLayout = true;
   features->workgroupMemoryExplicitLayout8BitAccess = true;
   features->workgroupMemoryExplicitLayout16BitAccess = true;

   /* VK_EXT_4444_formats */
   features->formatA4R4G4B4 = true;
   features->formatA4B4G4R4 = true;

   /* VK_EXT_attachment_feedback_loop_dynamic_state */
   features->attachmentFeedbackLoopDynamicState = true;

   /* VK_EXT_attachment_feedback_loop_layout */
   features->attachmentFeedbackLoopLayout = true;

   /* VK_EXT_border_color_swizzle */
   features->borderColorSwizzle = true;
   features->borderColorSwizzleFromImage = true;

   /* VK_EXT_color_write_enable */
   features->colorWriteEnable = true;

   /* VK_EXT_conditional_rendering */
   features->conditionalRendering = true;
   features->inheritedConditionalRendering = true;

   /* VK_EXT_custom_border_color */
   features->customBorderColors = true;
   features->customBorderColorWithoutFormat = true;

   /* VK_EXT_depth_clamp_zero_one */
   features->depthClampZeroOne = true;

   /* VK_EXT_depth_clip_control */
   features->depthClipControl = true;

   /* VK_EXT_depth_clip_enable */
   features->depthClipEnable = true;

   /* VK_EXT_descriptor_buffer */
   features->descriptorBuffer = true;
   features->descriptorBufferCaptureReplay = pdevice->has_set_iova;
   features->descriptorBufferImageLayoutIgnored = true;
   features->descriptorBufferPushDescriptors = true;

   /* VK_EXT_device_address_binding_report */
   features->reportAddressBinding = true;

   /* VK_EXT_extended_dynamic_state */
   features->extendedDynamicState = true;

   /* VK_EXT_extended_dynamic_state2 */
   features->extendedDynamicState2 = true;
   features->extendedDynamicState2LogicOp = true;
   features->extendedDynamicState2PatchControlPoints = true;

   /* VK_EXT_extended_dynamic_state3 */
   features->extendedDynamicState3PolygonMode = true;
   features->extendedDynamicState3TessellationDomainOrigin = !pdevice->info->props.is_a702;
   features->extendedDynamicState3DepthClampEnable = true;
   features->extendedDynamicState3DepthClipEnable = true;
   features->extendedDynamicState3LogicOpEnable = true;
   features->extendedDynamicState3SampleMask = true;
   features->extendedDynamicState3RasterizationSamples = true;
   features->extendedDynamicState3AlphaToCoverageEnable = true;
   features->extendedDynamicState3AlphaToOneEnable = true;
   features->extendedDynamicState3DepthClipNegativeOneToOne = true;
   features->extendedDynamicState3RasterizationStream = !pdevice->info->props.is_a702;
   features->extendedDynamicState3ConservativeRasterizationMode =
      pdevice->vk.supported_extensions.EXT_conservative_rasterization;
   features->extendedDynamicState3ExtraPrimitiveOverestimationSize =
      pdevice->vk.supported_extensions.EXT_conservative_rasterization;
   features->extendedDynamicState3LineRasterizationMode = true;
   features->extendedDynamicState3LineStippleEnable = false;
   features->extendedDynamicState3ProvokingVertexMode = true;
   features->extendedDynamicState3SampleLocationsEnable =
      pdevice->info->props.has_sample_locations;
   features->extendedDynamicState3ColorBlendEnable = true;
   features->extendedDynamicState3ColorBlendEquation = true;
   features->extendedDynamicState3ColorWriteMask = true;
   features->extendedDynamicState3ViewportWScalingEnable = false;
   features->extendedDynamicState3ViewportSwizzle = false;
   features->extendedDynamicState3ShadingRateImageEnable = false;
   features->extendedDynamicState3CoverageToColorEnable = false;
   features->extendedDynamicState3CoverageToColorLocation = false;
   features->extendedDynamicState3CoverageModulationMode = false;
   features->extendedDynamicState3CoverageModulationTableEnable = false;
   features->extendedDynamicState3CoverageModulationTable = false;
   features->extendedDynamicState3CoverageReductionMode = false;
   features->extendedDynamicState3RepresentativeFragmentTestEnable = false;
   features->extendedDynamicState3ColorBlendAdvanced = false;

   /* VK_EXT_fragment_density_map */
   features->fragmentDensityMap = true;
   features->fragmentDensityMapDynamic = false;
   features->fragmentDensityMapNonSubsampledImages = true;

   /* VK_EXT_global_priority_query */
   features->globalPriorityQuery = true;

   /* VK_EXT_graphics_pipeline_library */
   features->graphicsPipelineLibrary = true;

   /* VK_EXT_host_image_copy */
   features->hostImageCopy = true;

   /* VK_EXT_image_2d_view_of_3d  */
   features->image2DViewOf3D = true;
   features->sampler2DViewOf3D = true;

   /* VK_EXT_image_view_min_lod */
   features->minLod = true;

   /* VK_EXT_legacy_vertex_attributes */
   features->legacyVertexAttributes = true;

   /* VK_EXT_legacy_dithering */
   features->legacyDithering = true;

   /* VK_EXT_map_memory_placed */
   features->memoryMapPlaced = true;
   features->memoryMapRangePlaced = false;
   features->memoryUnmapReserve = true;

   /* VK_EXT_multi_draw */
   features->multiDraw = true;

   /* VK_EXT_mutable_descriptor_type */
   features->mutableDescriptorType = true;

   /* VK_EXT_nested_command_buffer */
   features->nestedCommandBuffer = true;
   features->nestedCommandBufferRendering = true;
   features->nestedCommandBufferSimultaneousUse = true;

   /* VK_EXT_non_seamless_cube_map */
   features->nonSeamlessCubeMap = true;

   /* VK_EXT_pipeline_robustness */
   features->pipelineRobustness = true;

   /* VK_EXT_primitive_topology_list_restart */
   features->primitiveTopologyListRestart = true;
   features->primitiveTopologyPatchListRestart = false;

   /* VK_EXT_primitives_generated_query */
   features->primitivesGeneratedQuery = true;
   features->primitivesGeneratedQueryWithRasterizerDiscard = false;
   features->primitivesGeneratedQueryWithNonZeroStreams = false;

   /* VK_EXT_provoking_vertex */
   features->provokingVertexLast = true;

   /* VK_EXT_rasterization_order_attachment_access */
   features->rasterizationOrderColorAttachmentAccess = true;
   features->rasterizationOrderDepthAttachmentAccess = true;
   features->rasterizationOrderStencilAttachmentAccess = true;

   /* VK_KHR_ray_query */
   features->rayQuery = true;

   /* VK_KHR_ray_tracing_maintenance1 */
   features->rayTracingMaintenance1 = true;

   /* VK_EXT_robustness2 */
   features->robustBufferAccess2 = true;
   features->robustImageAccess2 = true;
   features->nullDescriptor = true;

   /* VK_EXT_shader_atomic_float */
   features->shaderBufferFloat32Atomics = true;
   features->shaderBufferFloat32AtomicAdd = false;
   features->shaderBufferFloat64Atomics = false;
   features->shaderBufferFloat64AtomicAdd = false;
   features->shaderSharedFloat32Atomics = true;
   features->shaderSharedFloat32AtomicAdd = false;
   features->shaderSharedFloat64Atomics = false;
   features->shaderSharedFloat64AtomicAdd = false;
   features->shaderImageFloat32Atomics = true;
   features->shaderImageFloat32AtomicAdd = false;
   features->sparseImageFloat32Atomics = false;
   features->sparseImageFloat32AtomicAdd = false;

   /* VK_EXT_shader_module_identifier */
   features->shaderModuleIdentifier = true;

   /* VK_EXT_shader_replicated_composites */
   features->shaderReplicatedComposites = true;

#ifdef TU_USE_WSI_PLATFORM
   /* VK_KHR_swapchain_maintenance1 */
   features->swapchainMaintenance1 = true;

   /* VK_KHR_present_id2 */
   features->presentId2 = true;

   /* VK_KHR_present_wait2 */
   features->presentWait2 = true;
#endif

   /* VK_EXT_texel_buffer_alignment */
   features->texelBufferAlignment = true;

   /* VK_EXT_transform_feedback */
   features->transformFeedback = true;
   features->geometryStreams = !pdevice->info->props.is_a702;

   /* VK_EXT_vertex_input_dynamic_state */
   features->vertexInputDynamicState = true;

   /* VK_KHR_shader_relaxed_extended_instruction */
   features->shaderRelaxedExtendedInstruction = true;

   /* VK_KHR_subgroup_rotate */
   features->shaderSubgroupRotate = true;
   features->shaderSubgroupRotateClustered = true;

   /* VK_EXT_fragment_density_map_offset */
   features->fragmentDensityMapOffset = true;

   /* VK_KHR_unified_layouts */
   features->unifiedImageLayouts = true;
   features->unifiedImageLayoutsVideo = false;

   /* VK_EXT_zero_initialize_device_memory */
   features->zeroInitializeDeviceMemory = true;

   /* VK_VALVE_fragment_density_map_layered */
   features->fragmentDensityMapLayered = true;

   /* VK_EXT_dynamic_rendering_unused_attachments */
   features->dynamicRenderingUnusedAttachments = true;

   /* VK_EXT_multisampled_render_to_single_sampled */
   features->multisampledRenderToSingleSampled = true;

   /* VK_EXT_custom_resolve */
   features->customResolve = true;
}

static void
tu_get_physical_device_properties_1_1(struct tu_physical_device *pdevice,
                                      struct vk_properties *p)
{
   memcpy(p->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
   memcpy(p->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
   memset(p->deviceLUID, 0, VK_LUID_SIZE);
   p->deviceNodeMask = 0;
   p->deviceLUIDValid = false;

   p->subgroupSize = pdevice->info->props.supports_double_threadsize ?
      pdevice->info->threadsize_base * 2 : pdevice->info->threadsize_base;
   p->subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
   p->subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT |
                                    VK_SUBGROUP_FEATURE_VOTE_BIT |
                                    VK_SUBGROUP_FEATURE_BALLOT_BIT |
                                    VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                                    VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
                                    VK_SUBGROUP_FEATURE_ROTATE_BIT_KHR |
                                    VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT_KHR |
                                    VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
                                    VK_SUBGROUP_FEATURE_ARITHMETIC_BIT;
   if (pdevice->info->props.has_getfiberid) {
      p->subgroupSupportedStages |= VK_SHADER_STAGE_ALL_GRAPHICS;
      p->subgroupSupportedOperations |= VK_SUBGROUP_FEATURE_QUAD_BIT;
   }

   p->subgroupQuadOperationsInAllStages = false;

   p->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
   p->maxMultiviewViewCount =
      tu_has_multiview(pdevice) ? MAX_VIEWPORTS : 1;
   p->maxMultiviewInstanceIndex = INT_MAX;
   p->protectedNoFault = false;
   /* Our largest descriptors are 2 texture descriptors, or a texture and
    * sampler descriptor.
    */
   p->maxPerSetDescriptors = MAX_SET_SIZE / (2 * A6XX_TEX_CONST_DWORDS * 4);
   /* Our buffer size fields allow only this much */
   p->maxMemoryAllocationSize = 0xFFFFFFFFull;

}


static const size_t max_descriptor_set_size = MAX_SET_SIZE / (4 * A6XX_TEX_CONST_DWORDS);
static const VkSampleCountFlags sample_counts =
   VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
static const VkSampleCountFlags sample_location_counts =
   VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;

static void
tu_get_physical_device_properties_1_2(struct tu_physical_device *pdevice,
                                      struct vk_properties *p)
{
   p->driverID = VK_DRIVER_ID_MESA_TURNIP;
   memset(p->driverName, 0, sizeof(p->driverName));
   snprintf(p->driverName, VK_MAX_DRIVER_NAME_SIZE,
            "turnip Mesa driver");
   memset(p->driverInfo, 0, sizeof(p->driverInfo));
   snprintf(p->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);
   if (pdevice->info->chip >= 7) {
      p->conformanceVersion = (VkConformanceVersion) {
         .major = 1,
         .minor = 4,
         .subminor = 0,
         .patch = 0,
      };
   } else {
      p->conformanceVersion = (VkConformanceVersion) {
         .major = 1,
         .minor = 2,
         .subminor = 7,
         .patch = 1,
      };
   }

   p->denormBehaviorIndependence =
      VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
   p->roundingModeIndependence =
      VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;

   p->shaderDenormFlushToZeroFloat16         = true;
   p->shaderDenormPreserveFloat16            = false;
   p->shaderRoundingModeRTEFloat16           = true;
   p->shaderRoundingModeRTZFloat16           = false;
   p->shaderSignedZeroInfNanPreserveFloat16  = true;

   p->shaderDenormFlushToZeroFloat32         = true;

   /* FP32 denorm preserve has to be emulated via soft-float. Normal
    * applications should not use this, and we don't want to advertize it and
    * get people confused, but vkd3d-proton cannot emulate it itself so we
    * have to allow it to use our emulation.
    */
   p->shaderDenormPreserveFloat32 = pdevice->instance->enable_softfloat32;

   p->shaderRoundingModeRTEFloat32           = true;
   p->shaderRoundingModeRTZFloat32           = false;
   p->shaderSignedZeroInfNanPreserveFloat32  = true;

   p->shaderDenormFlushToZeroFloat64         = false;
   p->shaderDenormPreserveFloat64            = false;
   p->shaderRoundingModeRTEFloat64           = false;
   p->shaderRoundingModeRTZFloat64           = false;
   p->shaderSignedZeroInfNanPreserveFloat64  = false;

   p->shaderUniformBufferArrayNonUniformIndexingNative   = true;
   p->shaderSampledImageArrayNonUniformIndexingNative    = true;
   p->shaderStorageBufferArrayNonUniformIndexingNative   = true;
   p->shaderStorageImageArrayNonUniformIndexingNative    = true;
   p->shaderInputAttachmentArrayNonUniformIndexingNative = false;
   p->robustBufferAccessUpdateAfterBind                  = true;
   p->quadDivergentImplicitLod                           = false;

   p->maxUpdateAfterBindDescriptorsInAllPools            = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindSamplers       = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindUniformBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindSampledImages  = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageImages  = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindInputAttachments = MAX_RTS;
   p->maxPerStageUpdateAfterBindResources                = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindSamplers            = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffers      = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS;
   p->maxDescriptorSetUpdateAfterBindStorageBuffers      = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS;
   p->maxDescriptorSetUpdateAfterBindSampledImages       = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageImages       = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindInputAttachments    = MAX_RTS;

   p->supportedDepthResolveModes    =
      VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
      VK_RESOLVE_MODE_CUSTOM_BIT_EXT;
   p->supportedStencilResolveModes  =
      VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
      VK_RESOLVE_MODE_CUSTOM_BIT_EXT;
   p->independentResolveNone  = false;
   p->independentResolve      = false;

   p->filterMinmaxSingleComponentFormats  = true;
   p->filterMinmaxImageComponentMapping   = true;

   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;

   p->framebufferIntegerColorSampleCounts = sample_counts;
}

static void
tu_get_physical_device_properties_1_3(struct tu_physical_device *pdevice,
                                      struct vk_properties *p)
{
   p->minSubgroupSize = pdevice->info->threadsize_base;
   p->maxSubgroupSize = pdevice->info->props.supports_double_threadsize ?
      pdevice->info->threadsize_base * 2 : pdevice->info->threadsize_base;
   p->maxComputeWorkgroupSubgroups = pdevice->info->max_waves;
   p->requiredSubgroupSizeStages = VK_SHADER_STAGE_ALL;

   p->maxInlineUniformBlockSize = MAX_INLINE_UBO_RANGE;
   p->maxPerStageDescriptorInlineUniformBlocks = MAX_INLINE_UBOS;
   p->maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UBOS;
   p->maxDescriptorSetInlineUniformBlocks = MAX_INLINE_UBOS;
   p->maxDescriptorSetUpdateAfterBindInlineUniformBlocks = MAX_INLINE_UBOS;
   p->maxInlineUniformTotalSize = MAX_INLINE_UBOS * MAX_INLINE_UBO_RANGE;

   p->integerDotProduct8BitUnsignedAccelerated = false;
   p->integerDotProduct8BitSignedAccelerated = false;
   p->integerDotProduct8BitMixedSignednessAccelerated = false;
   p->integerDotProduct4x8BitPackedUnsignedAccelerated =
      pdevice->info->props.has_dp2acc;
   /* TODO: we should be able to emulate 4x8BitPackedSigned fast enough */
   p->integerDotProduct4x8BitPackedSignedAccelerated = false;
   p->integerDotProduct4x8BitPackedMixedSignednessAccelerated =
      pdevice->info->props.has_dp2acc;
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
   p->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated =
      pdevice->info->props.has_dp2acc;
   /* TODO: we should be able to emulate Saturating4x8BitPackedSigned fast enough */
   p->integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated =
      pdevice->info->props.has_dp2acc;
   p->integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating16BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitSignedAccelerated = false;
   p->integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = false;

   p->storageTexelBufferOffsetAlignmentBytes = 64;
   p->storageTexelBufferOffsetSingleTexelAlignment = true;
   p->uniformTexelBufferOffsetAlignmentBytes = 64;
   p->uniformTexelBufferOffsetSingleTexelAlignment = true;

   /* The address space is 4GB for current kernels, so there's no point
    * allowing a larger buffer. Our buffer sizes are 64-bit though, so
    * GetBufferDeviceRequirements won't fall over if someone actually creates
    * a 4GB buffer.
    */
   p->maxBufferSize = 1ull << 32;
}

/* CP_ALWAYS_ON_COUNTER is fixed 19.2 MHz */
#define ALWAYS_ON_FREQUENCY 19200000

static void
tu_get_properties(struct tu_physical_device *pdevice,
                  struct vk_properties *props)
{
   /* Limits */
   props->maxImageDimension1D = (1 << 14);
   props->maxImageDimension2D = (1 << 14);
   props->maxImageDimension3D = (1 << 11);
   props->maxImageDimensionCube = (1 << 14);
   props->maxImageArrayLayers = (1 << (pdevice->info->props.is_a702 ? 8 : 11));
   props->maxTexelBufferElements = MAX_TEXEL_ELEMENTS;
   props->maxUniformBufferRange = MAX_UNIFORM_BUFFER_RANGE;
   props->maxStorageBufferRange = MAX_STORAGE_BUFFER_RANGE;
   props->maxPushConstantsSize = MAX_PUSH_CONSTANTS_SIZE;
   props->maxMemoryAllocationCount = UINT32_MAX;
   props->maxSamplerAllocationCount = 64 * 1024;
   props->bufferImageGranularity = 64;          /* A cache line */
   props->sparseAddressSpaceSize = pdevice->va_size;
   props->maxBoundDescriptorSets = pdevice->usable_sets;
   props->maxPerStageDescriptorSamplers = max_descriptor_set_size;
   props->maxPerStageDescriptorUniformBuffers = max_descriptor_set_size;
   props->maxPerStageDescriptorStorageBuffers = max_descriptor_set_size;
   props->maxPerStageDescriptorSampledImages = max_descriptor_set_size;
   props->maxPerStageDescriptorStorageImages = max_descriptor_set_size;
   props->maxPerStageDescriptorInputAttachments = MAX_RTS;
   props->maxPerStageResources = max_descriptor_set_size;
   props->maxDescriptorSetSamplers = max_descriptor_set_size;
   props->maxDescriptorSetUniformBuffers = max_descriptor_set_size;
   props->maxDescriptorSetUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS;
   props->maxDescriptorSetStorageBuffers = max_descriptor_set_size;
   props->maxDescriptorSetStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS;
   props->maxDescriptorSetSampledImages = max_descriptor_set_size;
   props->maxDescriptorSetStorageImages = max_descriptor_set_size;
   props->maxDescriptorSetInputAttachments = MAX_RTS;
   props->maxVertexInputAttributes = pdevice->info->props.vs_max_inputs_count;
   props->maxVertexInputBindings = pdevice->info->props.vs_max_inputs_count;
   props->maxVertexInputAttributeOffset = 4095;
   props->maxVertexInputBindingStride = 2048;
   props->maxVertexOutputComponents = pdevice->info->props.is_a702 ? 64 : 128;
   if (!pdevice->info->props.is_a702) {
      props->maxTessellationGenerationLevel = 64;
      props->maxTessellationPatchSize = 32;
      props->maxTessellationControlPerVertexInputComponents = 128;
      props->maxTessellationControlPerVertexOutputComponents = 128;
      props->maxTessellationControlPerPatchOutputComponents = 120;
      props->maxTessellationControlTotalOutputComponents = 4096;
      props->maxTessellationEvaluationInputComponents = 128;
      props->maxTessellationEvaluationOutputComponents = 128;
      props->maxGeometryShaderInvocations = 32;
      props->maxGeometryInputComponents = 64;
      props->maxGeometryOutputComponents = 128;
      props->maxGeometryOutputVertices = 256;
      props->maxGeometryTotalOutputComponents = 1024;
   }
   // probably should be props->maxVertexOutputComponents - 4 but that is
   // below the limit on a702
   props->maxFragmentInputComponents = pdevice->info->props.is_a702 ? 112 : 124;
   props->maxFragmentOutputAttachments = 8;
   props->maxFragmentDualSrcAttachments = 1;
   props->maxFragmentCombinedOutputResources = MAX_RTS + max_descriptor_set_size * 2;
   props->maxComputeSharedMemorySize = pdevice->info->cs_shared_mem_size;
   props->maxComputeWorkGroupCount[0] =
      props->maxComputeWorkGroupCount[1] =
      props->maxComputeWorkGroupCount[2] = 65535;
   props->maxComputeWorkGroupInvocations = pdevice->info->props.supports_double_threadsize ?
      pdevice->info->threadsize_base * 2 * pdevice->info->max_waves :
      pdevice->info->threadsize_base * pdevice->info->max_waves;
   if (pdevice->info->props.is_a702) {
      props->maxComputeWorkGroupSize[0] =
         props->maxComputeWorkGroupSize[1] = 512;
      props->maxComputeWorkGroupSize[2] = 64;
   } else {
      props->maxComputeWorkGroupSize[0] =
         props->maxComputeWorkGroupSize[1] =
         props->maxComputeWorkGroupSize[2] = 1024;
   }
   props->subPixelPrecisionBits = 8;
   props->subTexelPrecisionBits = 8;
   props->mipmapPrecisionBits = 8;
   props->maxDrawIndexedIndexValue = UINT32_MAX;
   props->maxDrawIndirectCount = UINT32_MAX;
   props->maxSamplerLodBias = 4095.0 / 256.0; /* [-16, 15.99609375] */
   props->maxSamplerAnisotropy = 16;
   props->maxViewports =
         tu_has_multiview(pdevice) ? MAX_VIEWPORTS : 1;
   props->maxViewportDimensions[0] =
      props->maxViewportDimensions[1] = MAX_VIEWPORT_SIZE;
   props->viewportBoundsRange[0] = INT16_MIN;
   props->viewportBoundsRange[1] = INT16_MAX;
   props->viewportSubPixelBits = 8;
   props->minMemoryMapAlignment = 4096; /* A page */
   props->minTexelBufferOffsetAlignment = 64;
   props->minUniformBufferOffsetAlignment = 64;
   props->minStorageBufferOffsetAlignment = 4;
   props->minTexelOffset = -16;
   props->maxTexelOffset = 15;
   props->minTexelGatherOffset = -32;
   props->maxTexelGatherOffset = 31;
   props->minInterpolationOffset = -0.5;
   props->maxInterpolationOffset = 0.4375;
   props->subPixelInterpolationOffsetBits = 4;
   props->maxFramebufferWidth = (1 << 14);
   props->maxFramebufferHeight = (1 << 14);
   props->maxFramebufferLayers = (1 << (pdevice->info->props.is_a702 ? 8 : 10));
   props->framebufferColorSampleCounts = sample_counts;
   props->framebufferDepthSampleCounts = sample_counts;
   props->framebufferStencilSampleCounts = sample_counts;
   props->framebufferNoAttachmentsSampleCounts = sample_counts;
   props->maxColorAttachments = MAX_RTS;
   props->sampledImageColorSampleCounts = sample_counts;
   props->sampledImageIntegerSampleCounts = sample_counts;
   props->sampledImageDepthSampleCounts = sample_counts;
   props->sampledImageStencilSampleCounts = sample_counts;
   props->storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
   props->maxSampleMaskWords = 1;
   props->timestampComputeAndGraphics = true;
   props->timestampPeriod = 1000000000.0 / (float) ALWAYS_ON_FREQUENCY;
   props->maxClipDistances = 8;
   props->maxCullDistances = 8;
   props->maxCombinedClipAndCullDistances = 8;
   props->discreteQueuePriorities = 2;
   props->pointSizeRange[0] = 1;
   props->pointSizeRange[1] = 4092;
   props->lineWidthRange[0] = pdevice->info->props.line_width_min;
   props->lineWidthRange[1] = pdevice->info->props.line_width_max;
   props->pointSizeGranularity = 	0.0625;
   props->lineWidthGranularity =
      pdevice->info->props.line_width_max == 1.0 ? 0.0 : 0.5;
   props->strictLines = !pdevice->info->props.is_a702;
   props->standardSampleLocations = true;
   props->optimalBufferCopyOffsetAlignment = 128;
   props->optimalBufferCopyRowPitchAlignment = 128;
   props->nonCoherentAtomSize = 64;

   props->apiVersion =
      tu_has_multiview(pdevice) ?
         ((pdevice->info->chip >= 7) ? TU_API_VERSION :
            VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION))
         : VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION);
   props->driverVersion = vk_get_driver_version();
   props->vendorID = pdevice->instance->force_vk_vendor != 0 ?
                     pdevice->instance->force_vk_vendor : 0x5143;
   props->deviceID = pdevice->dev_id.chip_id;
   props->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

   /* Vulkan 1.4 */
   props->dynamicRenderingLocalReadDepthStencilAttachments = true;
   props->dynamicRenderingLocalReadMultisampledAttachments = true;

   /* sparse properties */
   props->sparseResidencyStandard2DBlockShape = true;
   props->sparseResidencyStandard2DMultisampleBlockShape = true;
   props->sparseResidencyStandard3DBlockShape = false;
   props->sparseResidencyAlignedMipSize = false;
   props->sparseResidencyNonResidentStrict = true;

   strcpy(props->deviceName, pdevice->name);
   memcpy(props->pipelineCacheUUID, pdevice->cache_uuid, VK_UUID_SIZE);

   tu_get_physical_device_properties_1_1(pdevice, props);
   tu_get_physical_device_properties_1_2(pdevice, props);
   tu_get_physical_device_properties_1_3(pdevice, props);

   /* VK_KHR_compute_shader_derivatives */
   props->meshAndTaskShaderDerivatives = false;

   /* VK_KHR_fragment_shading_rate */
   if (pdevice->info->props.has_attachment_shading_rate) {
      props->minFragmentShadingRateAttachmentTexelSize = {8, 8};
      props->maxFragmentShadingRateAttachmentTexelSize = {8, 8};
   } else {
      props->minFragmentShadingRateAttachmentTexelSize = {0, 0};
      props->maxFragmentShadingRateAttachmentTexelSize = {0, 0};
   }
   props->maxFragmentShadingRateAttachmentTexelSizeAspectRatio = 1;
   props->primitiveFragmentShadingRateWithMultipleViewports =
      pdevice->info->props.has_primitive_shading_rate;
   /* A7XX TODO: dEQP-VK.fragment_shading_rate.*.srlayered.* are failing
    * for some reason.
    */
   props->layeredShadingRateAttachments = false;
   props->fragmentShadingRateNonTrivialCombinerOps = true;
   props->maxFragmentSize = {4, 4};
   props->maxFragmentSizeAspectRatio = 4;
   props->maxFragmentShadingRateCoverageSamples = 16;
   props->maxFragmentShadingRateRasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
   props->fragmentShadingRateWithShaderDepthStencilWrites = true;
   props->fragmentShadingRateWithSampleMask = true;
   props->fragmentShadingRateWithShaderSampleMask = true;
   props->fragmentShadingRateWithConservativeRasterization = true;
   props->fragmentShadingRateWithFragmentShaderInterlock = false;
   props->fragmentShadingRateWithCustomSampleLocations = true;
   props->fragmentShadingRateStrictMultiplyCombiner = true;

   /* VK_KHR_push_descriptor */
   props->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;

   /* VK_EXT_transform_feedback */
   if (pdevice->info->props.is_a702) {
       /* a702 only 32 streamout ram entries.. 1 stream, 64 components */
      props->maxTransformFeedbackStreams = 1;
   } else {
      props->maxTransformFeedbackStreams = IR3_MAX_SO_STREAMS;
   }
   props->maxTransformFeedbackBuffers = IR3_MAX_SO_BUFFERS;
   props->maxTransformFeedbackBufferSize = UINT32_MAX;
   props->maxTransformFeedbackStreamDataSize = 512;
   props->maxTransformFeedbackBufferDataSize = 512;
   props->maxTransformFeedbackBufferDataStride = 512;
   props->transformFeedbackQueries = true;
   props->transformFeedbackStreamsLinesTriangles = true;
   props->transformFeedbackRasterizationStreamSelect = true;
   props->transformFeedbackDraw = true;

   /* VK_EXT_sample_locations */
   props->sampleLocationSampleCounts =
      pdevice->vk.supported_extensions.EXT_sample_locations ? sample_location_counts : 0;
   props->maxSampleLocationGridSize = (VkExtent2D) { 1 , 1 };
   props->sampleLocationCoordinateRange[0] = SAMPLE_LOCATION_MIN;
   props->sampleLocationCoordinateRange[1] = SAMPLE_LOCATION_MAX;
   props->sampleLocationSubPixelBits = 4;
   props->variableSampleLocations = true;

   /* VK_KHR_vertex_attribute_divisor */
   props->maxVertexAttribDivisor = UINT32_MAX;
   props->supportsNonZeroFirstInstance = true;

   /* VK_EXT_custom_border_color */
   props->maxCustomBorderColorSamplers = TU_BORDER_COLOR_COUNT;

   /* VK_KHR_performance_query */
   props->allowCommandBufferQueryCopies = false;

   /* VK_EXT_robustness2 */
   /* see write_buffer_descriptor() */
   props->robustStorageBufferAccessSizeAlignment = 4;
   /* see write_ubo_descriptor() */
   props->robustUniformBufferAccessSizeAlignment = 16;

   /* VK_EXT_pipeline_robustness */
   props->defaultRobustnessStorageBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT;
   props->defaultRobustnessUniformBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT;
   props->defaultRobustnessVertexInputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT;
   props->defaultRobustnessImages = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2_EXT;

   /* VK_EXT_provoking_vertex */
   props->provokingVertexModePerPipeline = true;
   props->transformFeedbackPreservesTriangleFanProvokingVertex = false;

   /* VK_KHR_line_rasterization */
   props->lineSubPixelPrecisionBits = 8;

   /* VK_EXT_physical_device_drm */
   props->drmHasPrimary = pdevice->has_master;
   props->drmPrimaryMajor = pdevice->master_major;
   props->drmPrimaryMinor = pdevice->master_minor;

   props->drmHasRender = pdevice->has_local;
   props->drmRenderMajor = pdevice->local_major;
   props->drmRenderMinor = pdevice->local_minor;

   /* VK_EXT_shader_module_identifier */
   STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                 sizeof(props->shaderModuleIdentifierAlgorithmUUID));
   memcpy(props->shaderModuleIdentifierAlgorithmUUID,
          vk_shaderModuleIdentifierAlgorithmUUID,
          sizeof(props->shaderModuleIdentifierAlgorithmUUID));

   /* VK_EXT_map_memory_placed */
   os_get_page_size(&os_page_size);
   props->minPlacedMemoryMapAlignment = os_page_size;

   /* VK_EXT_multi_draw */
   props->maxMultiDrawCount = 2048;

   /* VK_EXT_nested_command_buffer */
   props->maxCommandBufferNestingLevel = UINT32_MAX;

   /* VK_EXT_graphics_pipeline_library */
   props->graphicsPipelineLibraryFastLinking = true;
   props->graphicsPipelineLibraryIndependentInterpolationDecoration = true;

   /* VK_EXT_extended_dynamic_state3 */
   props->dynamicPrimitiveTopologyUnrestricted = true;

   /* VK_EXT_descriptor_buffer */
   props->combinedImageSamplerDescriptorSingleArray = true;
   props->bufferlessPushDescriptors = true;
   props->allowSamplerImageViewPostSubmitCreation = true;
   props->descriptorBufferOffsetAlignment = A6XX_TEX_CONST_DWORDS * 4;
   props->maxDescriptorBufferBindings = pdevice->usable_sets;
   props->maxResourceDescriptorBufferBindings = pdevice->usable_sets;
   props->maxSamplerDescriptorBufferBindings = pdevice->usable_sets;
   props->maxEmbeddedImmutableSamplerBindings = pdevice->usable_sets;
   props->maxEmbeddedImmutableSamplers = max_descriptor_set_size;
   props->bufferCaptureReplayDescriptorDataSize = 0;
   props->imageCaptureReplayDescriptorDataSize = sizeof(uint64_t);
   props->imageViewCaptureReplayDescriptorDataSize = 0;
   props->samplerCaptureReplayDescriptorDataSize = 0;
   props->accelerationStructureCaptureReplayDescriptorDataSize = 0;
   /* Note: these sizes must match descriptor_size() */
   props->samplerDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->combinedImageSamplerDescriptorSize = 2 * A6XX_TEX_CONST_DWORDS * 4;
   props->sampledImageDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->storageImageDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->uniformTexelBufferDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->robustUniformTexelBufferDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->storageTexelBufferDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->robustStorageTexelBufferDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->uniformBufferDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->robustUniformBufferDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->storageBufferDescriptorSize = A6XX_TEX_CONST_DWORDS * 4 * (1 +
      COND(pdevice->info->props.storage_16bit && !pdevice->info->props.has_isam_v, 1) +
      COND(pdevice->info->props.storage_8bit, 1));
   props->robustStorageBufferDescriptorSize =
      props->storageBufferDescriptorSize;
   props->accelerationStructureDescriptorSize = 4 * A6XX_TEX_CONST_DWORDS;
   props->inputAttachmentDescriptorSize = A6XX_TEX_CONST_DWORDS * 4;
   props->maxSamplerDescriptorBufferRange = ~0ull;
   props->maxResourceDescriptorBufferRange = ~0ull;
   props->samplerDescriptorBufferAddressSpaceSize = ~0ull;
   props->resourceDescriptorBufferAddressSpaceSize = ~0ull;
   props->descriptorBufferAddressSpaceSize = ~0ull;
   props->combinedImageSamplerDensityMapDescriptorSize = 2 * A6XX_TEX_CONST_DWORDS * 4;

   /* VK_EXT_legacy_vertex_attributes */
   props->nativeUnalignedPerformance = true;

   /* VK_EXT_fragment_density_map*/
   props->minFragmentDensityTexelSize = (VkExtent2D) { MIN_FDM_TEXEL_SIZE, MIN_FDM_TEXEL_SIZE };
   props->maxFragmentDensityTexelSize = (VkExtent2D) { MAX_FDM_TEXEL_SIZE, MAX_FDM_TEXEL_SIZE };
   props->fragmentDensityInvocations = false;

   /* VK_KHR_maintenance5 */
   props->earlyFragmentMultisampleCoverageAfterSampleCounting = true;
   props->earlyFragmentSampleMaskTestBeforeSampleCounting = true;
   props->depthStencilSwizzleOneSupport = true;
   props->polygonModePointSize = true;
   props->nonStrictWideLinesUseParallelogram = false;
   props->nonStrictSinglePixelWideLinesUseParallelogram = false;

   /* VK_KHR_maintenance6 */
   props->blockTexelViewCompatibleMultipleLayers = true;
   props->maxCombinedImageSamplerDescriptorCount = 1;
   props->fragmentShadingRateClampCombinerInputs = true;

   /* VK_KHR_maintenance7 */
   props->robustFragmentShadingRateAttachmentAccess = true;
   /* For D24S8, storing depth or stencil forces a load and store of the other
    * component.
    */
   props->separateDepthStencilAttachmentAccess = false;
   /* Uniform and storage buffers are different sizes, so we can't allow the
    * user to freely mix them and count both against a shared limit. We have
    * to force the user to use at most MAX_DYNAMIC_UNIFORM_BUFFERS uniform
    * buffers and MAX_DYNAMIC_STORAGE_BUFFERS storage buffers and set the
    * combined limit to the sum (which makes it redundant since the user will
    * always hit the other limits first).
    */
   props->maxDescriptorSetTotalUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS;
   props->maxDescriptorSetTotalStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS;
   props->maxDescriptorSetTotalBuffersDynamic =
      MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS;
   props->maxDescriptorSetUpdateAfterBindTotalUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS;
   props->maxDescriptorSetUpdateAfterBindTotalStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS;
   props->maxDescriptorSetUpdateAfterBindTotalBuffersDynamic =
      MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS;

   /* VK_EXT_host_image_copy */

   /* We don't use the layouts ATM so just report all layouts from
    * extensions that we support as compatible.
    */
   static const VkImageLayout supported_layouts[] = {
      VK_IMAGE_LAYOUT_GENERAL, /* required by spec */
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_PREINITIALIZED,
      VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT,
      VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT,
   };

   props->pCopySrcLayouts = (VkImageLayout *)supported_layouts;
   props->copySrcLayoutCount = ARRAY_SIZE(supported_layouts);
   props->pCopyDstLayouts = (VkImageLayout *)supported_layouts;
   props->copyDstLayoutCount = ARRAY_SIZE(supported_layouts);

   /* We're a UMR so we can always map every kind of memory */
   props->identicalMemoryTypeRequirements = true;

   {
      struct mesa_sha1 sha1_ctx;
      uint8_t sha1[20];

      _mesa_sha1_init(&sha1_ctx);

      /* Make sure we don't match with other vendors */
      const char *driver = "turnip-v1";
      _mesa_sha1_update(&sha1_ctx, driver, strlen(driver));

      /* Hash in UBWC configuration */
      _mesa_sha1_update(&sha1_ctx, &pdevice->ubwc_config.highest_bank_bit,
                        sizeof(pdevice->ubwc_config.highest_bank_bit));
      _mesa_sha1_update(&sha1_ctx, &pdevice->ubwc_config.bank_swizzle_levels,
                        sizeof(pdevice->ubwc_config.bank_swizzle_levels));
      _mesa_sha1_update(&sha1_ctx, &pdevice->ubwc_config.macrotile_mode,
                        sizeof(pdevice->ubwc_config.macrotile_mode));

      _mesa_sha1_final(&sha1_ctx, sha1);

      memcpy(props->optimalTilingLayoutUUID, sha1, VK_UUID_SIZE);
   }

   /* VK_KHR_acceleration_structure */
   props->maxGeometryCount = (1 << 24) - 1;
   props->maxInstanceCount = (1 << 24) - 1;
   props->maxPrimitiveCount = (1 << 29) - 1;
   props->maxPerStageDescriptorAccelerationStructures = max_descriptor_set_size;
   props->maxPerStageDescriptorUpdateAfterBindAccelerationStructures = max_descriptor_set_size;
   props->maxDescriptorSetAccelerationStructures = max_descriptor_set_size;
   props->maxDescriptorSetUpdateAfterBindAccelerationStructures = max_descriptor_set_size;
   props->minAccelerationStructureScratchOffsetAlignment = 128;

   /* VK_EXT_conservative_rasterization */
   props->primitiveOverestimationSize = 0.5 + 1 / 256.;
   props->maxExtraPrimitiveOverestimationSize = 0.5;
   props->extraPrimitiveOverestimationSizeGranularity = 0.5;
   props->primitiveUnderestimation = false;
   props->conservativePointAndLineRasterization = false;
   props->degenerateTrianglesRasterized = true;
   props->degenerateLinesRasterized = false;
   props->fullyCoveredFragmentShaderInputVariable = false;
   props->conservativeRasterizationPostDepthCoverage = false;

   /* VK_QCOM_fragment_density_map_offset */
   props->fragmentDensityOffsetGranularity = (VkExtent2D) { 
      TU_FDM_OFFSET_GRANULARITY, TU_FDM_OFFSET_GRANULARITY
   };

   /* VK_VALVE_fragment_density_map_layered */
   props->maxFragmentDensityMapLayers = MAX_VIEWS;
}

static const struct vk_pipeline_cache_object_ops *const cache_import_ops[] = {
   &tu_shader_ops,
   &tu_nir_shaders_ops,
   NULL,
};

/* Note if we introduce more queues in a family that we may need to reduce the max
 * scope in our nir_opt_acquire_release_barriers() call.  See
 * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/33504#note_2807879
 */
static const VkQueueFamilyProperties tu_gfx_queue_family_properties = {
   .queueFlags =
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
   .queueCount = 1,
   .timestampValidBits = 48,
   .minImageTransferGranularity = { 1, 1, 1 },
};

static const VkQueueFamilyProperties tu_gfx_sparse_queue_family_properties = {
   .queueFlags =
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT |
      VK_QUEUE_SPARSE_BINDING_BIT,
   .queueCount = 1,
   .timestampValidBits = 48,
   .minImageTransferGranularity = { 1, 1, 1 },
};

static const VkQueueFamilyProperties tu_sparse_queue_family_properties = {
   .queueFlags = VK_QUEUE_SPARSE_BINDING_BIT,
   .queueCount = 1,
   .timestampValidBits = 48,
   .minImageTransferGranularity = { 1, 1, 1 },
};

VkResult
tu_physical_device_init(struct tu_physical_device *device,
                        struct tu_instance *instance)
{
   VkResult result = VK_SUCCESS;

   const char *fd_name = fd_dev_name(&device->dev_id);
   if (!fd_name) {
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "device (chip_id = %" PRIX64
                               ", gpu_id = %u) is unsupported",
                               device->dev_id.chip_id, device->dev_id.gpu_id);
   }

   const struct fd_dev_info info = fd_dev_info(&device->dev_id);
   assert(info.chip);

   /* Print a suffix if raytracing is disabled by the SW fuse, in an attempt
    * to avoid confusion when apps don't work.
    */
   bool raytracing_disabled = info.props.has_sw_fuse &&
      !device->has_raytracing;
   const char *rt_suffix = raytracing_disabled ? " (raytracing disabled)" : "";

   if (strncmp(fd_name, "FD", 2) == 0) {
      device->name = vk_asprintf(&instance->vk.alloc,
                                 VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE,
                                 "Turnip Adreno (TM) %s%s", &fd_name[2],
                                 rt_suffix);
   } else {
      device->name = vk_asprintf(&instance->vk.alloc,
                                 VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE,
                                 "%s%s", fd_name, rt_suffix);

   }
   if (!device->name) {
      return vk_startup_errorf(instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                               "device name alloc fail");
   }

   switch (fd_dev_gen(&device->dev_id)) {
   case 6:
   case 7: {
      device->dev_info = info;
      device->info = &device->dev_info;
      uint32_t depth_cache_size =
         device->info->num_ccu * device->info->props.sysmem_per_ccu_depth_cache_size;
      uint32_t color_cache_size =
         (device->info->num_ccu *
          device->info->props.sysmem_per_ccu_color_cache_size);
      uint32_t color_cache_size_gmem =
         color_cache_size /
         (1 << device->info->props.gmem_ccu_color_cache_fraction);

      device->ccu_depth_offset_bypass = 0;
      device->ccu_offset_bypass =
         device->ccu_depth_offset_bypass + depth_cache_size;

      if (device->info->props.has_gmem_vpc_attr_buf) {
         device->vpc_attr_buf_size_bypass =
            device->info->props.sysmem_vpc_attr_buf_size;
         device->vpc_attr_buf_offset_bypass =
            device->ccu_offset_bypass + color_cache_size;

         device->vpc_attr_buf_size_gmem =
            device->info->props.gmem_vpc_attr_buf_size;
         device->vpc_attr_buf_offset_gmem =
            device->gmem_size -
            (device->vpc_attr_buf_size_gmem * device->info->num_ccu);

         device->ccu_offset_gmem =
            device->vpc_attr_buf_offset_gmem - color_cache_size_gmem;

         device->usable_gmem_size_gmem = device->vpc_attr_buf_offset_gmem;
      } else {
         device->ccu_offset_gmem = device->gmem_size - color_cache_size_gmem;
         device->usable_gmem_size_gmem = device->gmem_size;
      }

      if (instance->reserve_descriptor_set) {
         device->usable_sets = device->reserved_set_idx = device->info->props.max_sets - 1;
      } else {
         device->usable_sets = device->info->props.max_sets;
         device->reserved_set_idx = -1;
      }
      break;
   }
   default:
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "device %s is unsupported", device->name);
      goto fail_free_name;
   }
   if (tu_device_get_cache_uuid(device, device->cache_uuid)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "cannot generate UUID");
      goto fail_free_name;
   }

   device->level1_dcache_size = util_cache_granularity();
   device->has_cached_non_coherent_memory =
      device->level1_dcache_size > 0 && !DETECT_ARCH_ARM;

   device->memory.type_count = 1;
   device->memory.types[0] =
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

   if (device->has_cached_coherent_memory) {
      device->memory.types[device->memory.type_count] =
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
         VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      device->memory.type_count++;
   }

   if (device->has_cached_non_coherent_memory) {
      device->memory.types[device->memory.type_count] =
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      device->memory.type_count++;
   }

   device->memory.non_lazy_type_count = device->memory.type_count;
   if (device->has_lazy_bos) {
      device->memory.types[device->memory.type_count] =
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
      device->memory.type_count++;
   }

   /* Provide fallback UBWC config values if the kernel doesn't support
    * providing them. This should match what the kernel programs.
    */
   if (!device->ubwc_config.highest_bank_bit) {
      device->ubwc_config.highest_bank_bit = info.highest_bank_bit;
   }
   if (device->ubwc_config.bank_swizzle_levels == ~0) {
      device->ubwc_config.bank_swizzle_levels = info.ubwc_swizzle;
   }
   if (device->ubwc_config.macrotile_mode == FDL_MACROTILE_INVALID) {
      device->ubwc_config.macrotile_mode =
         (enum fdl_macrotile_mode) info.macrotile_mode;
   }

   fd_get_driver_uuid(device->driver_uuid);
   fd_get_device_uuid(device->device_uuid, &device->dev_id);

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &tu_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk,
                                    NULL, NULL, NULL, /* We set up extensions later */
                                    &dispatch_table);
   if (result != VK_SUCCESS)
      goto fail_free_name;

   get_device_extensions(device, &device->vk.supported_extensions);
   tu_get_features(device, &device->vk.supported_features);
   tu_get_properties(device, &device->vk.properties);

   device->vk.supported_sync_types = device->sync_types;

   device->queue_families[device->num_queue_families++] =
      (struct tu_queue_family) {
         .type = TU_QUEUE_GFX,
         .properties = device->has_sparse ?
            &tu_gfx_sparse_queue_family_properties :
            &tu_gfx_queue_family_properties,
      };

   if (device->has_sparse) {
      device->queue_families[device->num_queue_families++] =
         (struct tu_queue_family) {
            .type = TU_QUEUE_SPARSE,
            .properties = &tu_sparse_queue_family_properties,
         };
   }

#ifdef TU_USE_WSI_PLATFORM
   result = tu_wsi_init(device);
   if (result != VK_SUCCESS) {
      vk_startup_errorf(instance, result, "WSI init failure");
      vk_physical_device_finish(&device->vk);
      goto fail_free_name;
   }
#endif

   /* The gpu id is already embedded in the uuid so we just pass "tu"
    * when creating the cache.
    */
   char buf[VK_UUID_SIZE * 2 + 1];
   mesa_bytes_to_hex(buf, device->cache_uuid, VK_UUID_SIZE);
   device->vk.disk_cache = disk_cache_create(device->name, buf, 0);

   device->vk.pipeline_cache_import_ops = cache_import_ops;

   return VK_SUCCESS;

fail_free_name:
   vk_free(&instance->vk.alloc, (void *)device->name);
   return result;
}

static void
tu_physical_device_finish(struct tu_physical_device *device)
{
#ifdef TU_USE_WSI_PLATFORM
   tu_wsi_finish(device);
#endif

   close(device->local_fd);
   if (device->master_fd != -1)
      close(device->master_fd);

   if (device->kgsl_dma_fd != -1)
      close(device->kgsl_dma_fd);

   disk_cache_destroy(device->vk.disk_cache);
   vk_free(&device->instance->vk.alloc, (void *)device->name);

   vk_physical_device_finish(&device->vk);
}

static void
tu_destroy_physical_device(struct vk_physical_device *device)
{
   tu_physical_device_finish((struct tu_physical_device *) device);
   vk_free(&device->instance->alloc, device);
}

static const driOptionDescription tu_dri_options[] = {
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
      DRI_CONF_VK_X11_ENSURE_MIN_IMAGE_COUNT(false)
      DRI_CONF_VK_XWAYLAND_WAIT_READY(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_DEBUG
      DRI_CONF_FORCE_VK_VENDOR()
      DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(false)
      DRI_CONF_VK_WSI_FORCE_SWAPCHAIN_TO_CURRENT_EXTENT(false)
      DRI_CONF_VK_X11_IGNORE_SUBOPTIMAL(false)
      DRI_CONF_VK_DONT_CARE_AS_LOAD(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_MISCELLANEOUS
      DRI_CONF_DISABLE_CONSERVATIVE_LRZ(false)
      DRI_CONF_TU_DONT_RESERVE_DESCRIPTOR_SET(false)
      DRI_CONF_TU_ALLOW_OOB_INDIRECT_UBO_LOADS(false)
      DRI_CONF_TU_DISABLE_D24S8_BORDER_COLOR_WORKAROUND(false)
      DRI_CONF_TU_USE_TEX_COORD_ROUND_NEAREST_EVEN_MODE(false)
      DRI_CONF_TU_IGNORE_FRAG_DEPTH_DIRECTION(false)
      DRI_CONF_TU_ENABLE_SOFTFLOAT32(false)
   DRI_CONF_SECTION_END
};

static void
tu_init_dri_options(struct tu_instance *instance)
{
   driParseOptionInfo(&instance->available_dri_options, tu_dri_options,
                      ARRAY_SIZE(tu_dri_options));
   driParseConfigFiles(&instance->dri_options, &instance->available_dri_options, 0, "turnip", NULL, NULL,
                       instance->vk.app_info.app_name, instance->vk.app_info.app_version,
                       instance->vk.app_info.engine_name, instance->vk.app_info.engine_version);

   instance->force_vk_vendor =
         driQueryOptioni(&instance->dri_options, "force_vk_vendor");
   instance->dont_care_as_load =
         driQueryOptionb(&instance->dri_options, "vk_dont_care_as_load");
   instance->conservative_lrz =
         !driQueryOptionb(&instance->dri_options, "disable_conservative_lrz");
   instance->reserve_descriptor_set =
         !driQueryOptionb(&instance->dri_options, "tu_dont_reserve_descriptor_set");
   instance->allow_oob_indirect_ubo_loads =
         driQueryOptionb(&instance->dri_options, "tu_allow_oob_indirect_ubo_loads");
   instance->disable_d24s8_border_color_workaround =
         driQueryOptionb(&instance->dri_options, "tu_disable_d24s8_border_color_workaround");
   instance->use_tex_coord_round_nearest_even_mode =
         driQueryOptionb(&instance->dri_options, "tu_use_tex_coord_round_nearest_even_mode");
   instance->ignore_frag_depth_direction =
         driQueryOptionb(&instance->dri_options, "tu_ignore_frag_depth_direction");
   instance->enable_softfloat32 =
         driQueryOptionb(&instance->dri_options, "tu_enable_softfloat32");
}

static uint32_t instance_count = 0;

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkInstance *pInstance)
{
   struct tu_instance *instance;
   VkResult result;

   tu_env_init();

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = (struct tu_instance *) vk_zalloc(
      pAllocator, sizeof(*instance), 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &tu_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk,
                             &tu_instance_extensions_supported,
                             &dispatch_table,
                             pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(NULL, result);
   }

   instance->vk.physical_devices.try_create_for_drm =
      tu_physical_device_try_create;
   instance->vk.physical_devices.enumerate = tu_enumerate_devices;
   instance->vk.physical_devices.destroy = tu_destroy_physical_device;

   instance->instance_idx = p_atomic_fetch_add(&instance_count, 1);
   if (TU_DEBUG(STARTUP))
      mesa_logi("Created an instance");

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   tu_init_dri_options(instance);

   *pInstance = tu_instance_to_handle(instance);

#ifdef HAVE_PERFETTO
   tu_perfetto_init();
#endif

   util_gpuvis_init();

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyInstance(VkInstance _instance,
                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_instance, instance, _instance);

   if (!instance)
      return;

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

void
tu_physical_device_get_global_priority_properties(const struct tu_physical_device *pdevice,
                                                  enum tu_queue_type type,
                                                  VkQueueFamilyGlobalPriorityPropertiesKHR *props)
{
   /* drm/msm only supports one priority for VM_BIND queues */
   if (type == TU_QUEUE_SPARSE) {
      props->priorityCount = 1;
      props->priorities[0] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
      return;
   }

   props->priorityCount = MIN2(pdevice->submitqueue_priority_count, 3);
   switch (props->priorityCount) {
   case 1:
      props->priorities[0] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
      break;
   case 2:
      props->priorities[0] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
      props->priorities[1] = VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR;
      break;
   case 3:
      props->priorities[0] = VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR;
      props->priorities[1] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
      props->priorities[2] = VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR;
      break;
   case 4:
      props->priorities[0] = VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR;
      props->priorities[1] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
      props->priorities[2] = VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR;
      props->priorities[3] = VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR;
      break;
   default:
      UNREACHABLE("unexpected priority count");
      break;
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice,
   uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_FROM_HANDLE(tu_physical_device, pdevice, physicalDevice);

   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out,
                          pQueueFamilyProperties, pQueueFamilyPropertyCount);

   for (unsigned i = 0; i < pdevice->num_queue_families; i++) {
      struct tu_queue_family *family = &pdevice->queue_families[i];

      vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
         p->queueFamilyProperties = *family->properties;

         vk_foreach_struct(ext, p->pNext) {
            switch (ext->sType) {
            case VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR: {
               VkQueueFamilyGlobalPriorityPropertiesKHR *props =
                  (VkQueueFamilyGlobalPriorityPropertiesKHR *) ext;
               tu_physical_device_get_global_priority_properties(
                  pdevice, family->type, props);
               break;
            }
            default:
               break;
            }
         }
      }
   }
}

uint64_t
tu_get_system_heap_size(struct tu_physical_device *physical_device)
{
   uint64_t total_ram = 0;
   ASSERTED bool has_physical_memory =
      os_get_total_physical_memory(&total_ram);
   assert(has_physical_memory);

   /* We don't want to burn too much ram with the GPU.  If the user has 4GiB
    * or less, we use at most half.  If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ull * 1024ull * 1024ull * 1024ull)
      available_ram = total_ram / 2;
   else
      available_ram = total_ram * 3 / 4;

   if (physical_device->va_size)
      available_ram = MIN2(available_ram, physical_device->va_size);

   return available_ram;
}

static VkDeviceSize
tu_get_budget_memory(struct tu_physical_device *physical_device)
{
   uint64_t heap_size = physical_device->heap.size;
   uint64_t heap_used = physical_device->heap.used;
   uint64_t sys_available;
   ASSERTED bool has_available_memory =
      os_get_available_system_memory(&sys_available);
   assert(has_available_memory);

   if (physical_device->va_size)
      sys_available = MIN2(sys_available, physical_device->va_size);

   /*
    * Let's not incite the app to starve the system: report at most 90% of
    * available system memory.
    */
   uint64_t heap_available = sys_available * 9 / 10;
   return MIN2(heap_size, heap_used + heap_available);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pdev,
                                      VkPhysicalDeviceMemoryProperties2 *props2)
{
   VK_FROM_HANDLE(tu_physical_device, physical_device, pdev);

   VkPhysicalDeviceMemoryProperties *props = &props2->memoryProperties;
   props->memoryHeapCount = 1;
   props->memoryHeaps[0].size = physical_device->heap.size;
   props->memoryHeaps[0].flags = physical_device->heap.flags;

   props->memoryTypeCount = physical_device->memory.type_count;
   for (uint32_t i = 0; i < physical_device->memory.type_count; i++) {
      props->memoryTypes[i] = (VkMemoryType) {
         .propertyFlags = physical_device->memory.types[i],
         .heapIndex     = 0,
      };
   }

   vk_foreach_struct(ext, props2->pNext)
   {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT: {
         VkPhysicalDeviceMemoryBudgetPropertiesEXT *memory_budget_props =
            (VkPhysicalDeviceMemoryBudgetPropertiesEXT *) ext;
         memory_budget_props->heapUsage[0] = physical_device->heap.used;
         memory_budget_props->heapBudget[0] = tu_get_budget_memory(physical_device);

         /* The heapBudget and heapUsage values must be zero for array elements
          * greater than or equal to VkPhysicalDeviceMemoryProperties::memoryHeapCount
          */
         for (unsigned i = 1; i < VK_MAX_MEMORY_HEAPS; i++) {
            memory_budget_props->heapBudget[i] = 0u;
            memory_budget_props->heapUsage[i] = 0u;
         }
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPhysicalDeviceFragmentShadingRatesKHR(
   VkPhysicalDevice physicalDevice,
   uint32_t *pFragmentShadingRateCount,
   VkPhysicalDeviceFragmentShadingRateKHR *pFragmentShadingRates)
{
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceFragmentShadingRateKHR, out,
                          pFragmentShadingRates, pFragmentShadingRateCount);

#define append_rate(w, h, s)                                                        \
   {                                                                                \
      VkPhysicalDeviceFragmentShadingRateKHR rate = {                               \
         .sType =                                                                   \
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR, \
         .sampleCounts = s,                                                         \
         .fragmentSize = { .width = w, .height = h },                               \
      };                                                                            \
      vk_outarray_append_typed(VkPhysicalDeviceFragmentShadingRateKHR, &out,        \
                               r) *r = rate;                                        \
   }

   append_rate(4, 4, VK_SAMPLE_COUNT_1_BIT);
   append_rate(4, 2, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT);
   append_rate(2, 2, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT);
   append_rate(2, 1, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT);
   append_rate(1, 2, VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT);
   append_rate(1, 1, ~0);

#undef append_rate

   return vk_outarray_status(&out);
}

uint64_t
tu_device_ticks_to_ns(struct tu_device *dev, uint64_t ts)
{
   /* This is based on the 19.2MHz always-on rbbm timer.
    *
    * TODO we should probably query this value from kernel..
    */
   return ts * (1000000000 / 19200000);
}

struct u_trace_context *
tu_device_get_u_trace(struct tu_device *device)
{
   return &device->trace_context;
}

static void*
tu_trace_create_buffer(struct u_trace_context *utctx, uint64_t size_B)
{
   struct tu_device *device =
      container_of(utctx, struct tu_device, trace_context);

   mtx_lock(&device->trace_mutex);

   if (!device->trace_suballoc) {
      device->trace_suballoc = (struct tu_suballocator *) vk_zalloc(
         &device->vk.alloc, sizeof(struct tu_suballocator), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      tu_bo_suballocator_init(device->trace_suballoc, device, 512 * 1024,
                              TU_BO_ALLOC_INTERNAL_RESOURCE, "utrace");
   }

   struct tu_suballoc_bo *suballoc_bo = (struct tu_suballoc_bo *) vk_zalloc(
      &device->vk.alloc, sizeof(struct tu_suballoc_bo), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   VkResult result =
      tu_suballoc_bo_alloc(suballoc_bo, device->trace_suballoc, size_B, 1);

   mtx_unlock(&device->trace_mutex);

   return result == VK_SUCCESS ? suballoc_bo : NULL;
}

static void
tu_trace_destroy_buffer(struct u_trace_context *utctx, void *timestamps)
{
   struct tu_device *device =
      container_of(utctx, struct tu_device, trace_context);
   struct tu_suballoc_bo *bo = (struct tu_suballoc_bo *) timestamps;

   static_assert(U_TRACE_NO_TIMESTAMP == 0);
   /* Newly allocated timestamp buffers are expected to be filled with
    * U_TRACE_NO_TIMESTAMP. Since we reuse buffers, we must clear them
    * before use. We do this here rather than in create_buffer since
    * tu_trace_create_buffer is called on a hot path.
    */
   memset(tu_suballoc_bo_map(bo), U_TRACE_NO_TIMESTAMP, bo->size);

   mtx_lock(&device->trace_mutex);
   tu_suballoc_bo_free(device->trace_suballoc, bo);
   mtx_unlock(&device->trace_mutex);

   vk_free(&device->vk.alloc, bo);
}

template <chip CHIP>
static void
tu_trace_record_ts(struct u_trace *ut, void *cs, void *timestamps,
                   uint64_t offset_B, uint32_t)
{
   struct tu_suballoc_bo *bo = (struct tu_suballoc_bo *) timestamps;
   struct tu_cs *ts_cs = (struct tu_cs *) cs;

   if (CHIP == A6XX) {
      tu_cs_emit_pkt7(ts_cs, CP_EVENT_WRITE, 4);
      tu_cs_emit(ts_cs, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS) |
                           CP_EVENT_WRITE_0_TIMESTAMP);
      tu_cs_emit_qw(ts_cs, bo->iova + offset_B);
      tu_cs_emit(ts_cs, 0x00000000);
   } else {
      tu_cs_emit_pkt7(ts_cs, CP_EVENT_WRITE7, 3);
      tu_cs_emit(ts_cs, CP_EVENT_WRITE7_0(.event = RB_DONE_TS,
                                          .write_src = EV_WRITE_ALWAYSON,
                                          .write_dst = EV_DST_RAM,
                                          .write_enabled = true)
                           .value);
      tu_cs_emit_qw(ts_cs, bo->iova + offset_B);
   }
}

static uint64_t
tu_trace_read_ts(struct u_trace_context *utctx,
                 void *timestamps, uint64_t offset_B,
                 uint32_t flags, void *flush_data)
{
   struct tu_device *device =
      container_of(utctx, struct tu_device, trace_context);
   struct tu_suballoc_bo *bo = (struct tu_suballoc_bo *) timestamps;
   struct tu_u_trace_submission_data *submission_data =
      (struct tu_u_trace_submission_data *) flush_data;

   /* Only need to stall on results for the first entry: */
   if (offset_B == 0) {
      tu_queue_wait_fence(submission_data->queue, submission_data->fence,
                          1000000000);
   }

   uint64_t *ts = (uint64_t *) ((char *) tu_suballoc_bo_map(bo) + offset_B);

   /* Don't translate the no-timestamp marker: */
   if (*ts == U_TRACE_NO_TIMESTAMP)
      return U_TRACE_NO_TIMESTAMP;

   return tu_device_ticks_to_ns(device, *ts);
}

static void
tu_trace_delete_flush_data(struct u_trace_context *utctx, void *flush_data)
{
   struct tu_device *device =
      container_of(utctx, struct tu_device, trace_context);
   struct tu_u_trace_submission_data *submission_data =
      (struct tu_u_trace_submission_data *) flush_data;

   tu_u_trace_submission_data_finish(device, submission_data);
}

void
tu_copy_buffer(struct u_trace_context *utctx, void *cmdstream,
               void *ts_from, uint64_t from_offset_B,
               void *ts_to, uint64_t to_offset_B,
               uint64_t size_B)
{
   struct tu_cs *cs = (struct tu_cs *) cmdstream;
   struct tu_suballoc_bo *bo_from = (struct tu_suballoc_bo *) ts_from;
   struct tu_suballoc_bo *bo_to = (struct tu_suballoc_bo *) ts_to;

   tu_cs_emit_pkt7(cs, CP_MEMCPY, 5);
   tu_cs_emit(cs, size_B / sizeof(uint32_t));
   tu_cs_emit_qw(cs, bo_from->iova + from_offset_B);
   tu_cs_emit_qw(cs, bo_to->iova + to_offset_B);
}

static void
tu_trace_capture_data(struct u_trace *ut,
                        void *cs,
                        void *dst_buffer,
                        uint64_t dst_offset_B,
                        void *src_buffer,
                        uint64_t src_offset_B,
                        uint32_t size_B)
{
   if (src_buffer)
      tu_copy_buffer(ut->utctx, cs, src_buffer, src_offset_B, dst_buffer,
                     dst_offset_B, size_B);
}

static const void *
tu_trace_get_data(struct u_trace_context *utctx,
                  void *buffer,
                  uint64_t offset_B,
                  uint32_t size_B)
{
   struct tu_suballoc_bo *bo = (struct tu_suballoc_bo *) buffer;
   return (char *) tu_suballoc_bo_map(bo) + offset_B;
}

VkResult
tu_create_copy_timestamp_cs(struct tu_u_trace_submission_data *submission_data,
                            struct tu_cmd_buffer **cmd_buffers,
                            uint32_t cmd_buffer_count,
                            uint32_t trace_chunks_to_copy)
{
   submission_data->last_buffer_with_tracepoints = -1;

   tu_device *device = cmd_buffers[0]->device;
   uint32_t cs_size = trace_chunks_to_copy * 6 * 2 + 3;

   mtx_lock(&device->copy_timestamp_cs_pool_mutex);
   if (!list_is_empty(&device->copy_timestamp_cs_pool)) {
      submission_data->timestamp_copy_data =
         list_first_entry(&device->copy_timestamp_cs_pool,
                          struct tu_copy_timestamp_data, node);
      list_del(&submission_data->timestamp_copy_data->node);
   }
   mtx_unlock(&device->copy_timestamp_cs_pool_mutex);

   if (!submission_data->timestamp_copy_data) {
      submission_data->timestamp_copy_data =
         (struct tu_copy_timestamp_data *) vk_zalloc(
            &device->vk.alloc, sizeof(struct tu_copy_timestamp_data), 8,
            VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      tu_cs_init(&submission_data->timestamp_copy_data->cs, device,
                 TU_CS_MODE_GROW, cs_size, "trace copy timestamp cs");
      u_trace_init(&submission_data->timestamp_copy_data->trace,
                   &device->trace_context);
   }

   tu_cs *cs = &submission_data->timestamp_copy_data->cs;

   tu_cs_begin(cs);

   tu_cs_emit_wfi(cs);
   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      struct tu_cmd_buffer *cmdbuf = cmd_buffers[i];

      if (!u_trace_has_points(&cmdbuf->trace) ||
          (cmdbuf->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
         continue;

      u_trace_clone_append(
         u_trace_begin_iterator(&cmdbuf->trace),
         u_trace_end_iterator(&cmdbuf->trace),
         &submission_data->timestamp_copy_data->trace, cs, tu_copy_buffer);
   }

   tu_cs_emit_wfi(cs);
   tu_cs_end(cs);

   return VK_SUCCESS;
}

VkResult
tu_u_trace_submission_data_create(
   struct tu_device *device,
   struct tu_cmd_buffer **cmd_buffers,
   uint32_t cmd_buffer_count,
   struct tu_u_trace_submission_data **submission_data)
{
   MESA_TRACE_FUNC();
   *submission_data = (struct tu_u_trace_submission_data *)
      vk_zalloc(&device->vk.alloc,
                sizeof(struct tu_u_trace_submission_data), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (!(*submission_data)) {
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   struct tu_u_trace_submission_data *data = *submission_data;
   uint32_t trace_chunks_to_copy = 0;

   data->trace_per_cmd_buffer = (struct u_trace **) vk_zalloc(
      &device->vk.alloc,
      cmd_buffer_count * sizeof(struct u_trace *), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (!data->trace_per_cmd_buffer) {
      goto fail;
   }

   data->cmd_buffer_count = cmd_buffer_count;
   data->last_buffer_with_tracepoints = -1;

   for (uint32_t i = 0; i < cmd_buffer_count; ++i) {
      struct tu_cmd_buffer *cmdbuf = cmd_buffers[i];

      if (!u_trace_has_points(&cmdbuf->trace))
         continue;

      data->last_buffer_with_tracepoints = i;

      if (!(cmdbuf->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) {
         /* A single command buffer could be submitted several times, but we
          * already baked timestamp iova addresses and trace points are
          * single-use. Therefor we have to copy trace points and create
          * a new timestamp buffer on every submit of reusable command buffer.
          */
         trace_chunks_to_copy += list_length(&cmdbuf->trace.trace_chunks);
      } else {
         data->trace_per_cmd_buffer[i] = &cmdbuf->trace;
      }
   }

   if (trace_chunks_to_copy > 0) {
      tu_create_copy_timestamp_cs(data, cmd_buffers, cmd_buffer_count,
                                  trace_chunks_to_copy);
   }

   return VK_SUCCESS;

fail:
   tu_u_trace_submission_data_finish(device, data);
   *submission_data = NULL;

   return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
}

void
tu_free_copy_timestamp_data(struct tu_device *device,
                            struct tu_copy_timestamp_data *data)
{
   if (list_is_linked(&data->node))
      list_del(&data->node);
   tu_cs_finish(&data->cs);
   u_trace_fini(&data->trace);
   vk_free(&device->vk.alloc, data);
}

void
tu_u_trace_submission_data_finish(
   struct tu_device *device,
   struct tu_u_trace_submission_data *submission_data)
{
   if (submission_data->timestamp_copy_data) {
      if (u_trace_enabled(&device->trace_context)) {
         tu_cs_reset(&submission_data->timestamp_copy_data->cs);
         u_trace_fini(&submission_data->timestamp_copy_data->trace);

         mtx_lock(&device->copy_timestamp_cs_pool_mutex);
         list_addtail(&submission_data->timestamp_copy_data->node,
                      &device->copy_timestamp_cs_pool);
         mtx_unlock(&device->copy_timestamp_cs_pool_mutex);
      } else {
         tu_free_copy_timestamp_data(device,
                                     submission_data->timestamp_copy_data);
      }
   }

   if (submission_data->kgsl_timestamp_bo.bo) {
      mtx_lock(&device->kgsl_profiling_mutex);
      tu_suballoc_bo_free(&device->kgsl_profiling_suballoc,
                        &submission_data->kgsl_timestamp_bo);
      mtx_unlock(&device->kgsl_profiling_mutex);
   }

   vk_free(&device->vk.alloc, submission_data->trace_per_cmd_buffer);
   vk_free(&device->vk.alloc, submission_data);
}

enum tu_reg_stomper_flags
{
   TU_DEBUG_REG_STOMP_INVERSE = 1 << 0,
   TU_DEBUG_REG_STOMP_CMDBUF = 1 << 1,
   TU_DEBUG_REG_STOMP_RENDERPASS = 1 << 2,
};

/* See freedreno.rst for usage tips */
static const struct debug_named_value tu_reg_stomper_options[] = {
   { "inverse", TU_DEBUG_REG_STOMP_INVERSE,
     "By default the range specifies the regs to stomp, with 'inverse' it "
     "specifies the regs NOT to stomp" },
   { "cmdbuf", TU_DEBUG_REG_STOMP_CMDBUF,
     "Stomp regs at the start of a cmdbuf" },
   { "renderpass", TU_DEBUG_REG_STOMP_RENDERPASS,
     "Stomp regs before a renderpass" },
   { NULL, 0 }
};

template <chip CHIP>
static inline void
tu_cs_dbg_stomp_regs(struct tu_cs *cs,
                     bool is_rp_blit,
                     uint32_t first_reg,
                     uint32_t last_reg,
                     bool inverse)
{
   const uint16_t *regs = NULL;
   size_t count = 0;

   if (is_rp_blit) {
      regs = &RP_BLIT_REGS<CHIP>[0];
      count = ARRAY_SIZE(RP_BLIT_REGS<CHIP>);
   } else {
      regs = &CMD_REGS<CHIP>[0];
      count = ARRAY_SIZE(CMD_REGS<CHIP>);
   }

   for (size_t i = 0; i < count; i++) {
      if (inverse) {
         if (regs[i] >= first_reg && regs[i] <= last_reg)
            continue;
      } else {
         if (regs[i] < first_reg || regs[i] > last_reg)
            continue;
      }

      if (fd_reg_stomp_allowed(CHIP, regs[i]))
         tu_cs_emit_write_reg(cs, regs[i], 0xffffffff);
   }
}

static void
tu_init_dbg_reg_stomper(struct tu_device *device)
{
   const char *stale_reg_range_str =
      os_get_option("TU_DEBUG_STALE_REGS_RANGE");
   if (!stale_reg_range_str)
      return;

   uint32_t first_reg, last_reg;

   if (sscanf(stale_reg_range_str, "%x,%x", &first_reg, &last_reg) != 2) {
      mesa_loge("Incorrect TU_DEBUG_STALE_REGS_RANGE");
      return;
   }

   uint64_t debug_flags = debug_get_flags_option("TU_DEBUG_STALE_REGS_FLAGS",
                                                 tu_reg_stomper_options,
                                                 TU_DEBUG_REG_STOMP_CMDBUF);

   bool inverse = debug_flags & TU_DEBUG_REG_STOMP_INVERSE;

   if (debug_flags & TU_DEBUG_REG_STOMP_CMDBUF) {
      struct tu_cs *cmdbuf_cs =
         (struct tu_cs *) calloc(1, sizeof(struct tu_cs));
      tu_cs_init(cmdbuf_cs, device, TU_CS_MODE_GROW, 4096,
                 "cmdbuf reg stomp cs");
      tu_cs_begin(cmdbuf_cs);

      TU_CALLX(device, tu_cs_dbg_stomp_regs)(cmdbuf_cs, false, first_reg, last_reg, inverse);
      tu_cs_end(cmdbuf_cs);
      device->dbg_cmdbuf_stomp_cs = cmdbuf_cs;
   }

   if (debug_flags & TU_DEBUG_REG_STOMP_RENDERPASS) {
      struct tu_cs *rp_cs = (struct tu_cs *) calloc(1, sizeof(struct tu_cs));
      tu_cs_init(rp_cs, device, TU_CS_MODE_GROW, 4096, "rp reg stomp cs");
      tu_cs_begin(rp_cs);

      TU_CALLX(device, tu_cs_dbg_stomp_regs)(rp_cs, true, first_reg, last_reg, inverse);
      tu_cs_end(rp_cs);

      device->dbg_renderpass_stomp_cs = rp_cs;
   }
}

/* It is unknown what this workaround is for and what it fixes. */
static VkResult
tu_init_cmdbuf_start_a725_quirk(struct tu_device *device)
{
   struct tu_cs shader_cs;
   tu_cs_begin_sub_stream(&device->sub_cs, 10, &shader_cs);

   uint32_t raw_shader[] = {
      0x00040000, 0x40600000, // mul.f hr0.x, hr0.x, hr1.x
      0x00050001, 0x40600001, // mul.f hr0.y, hr0.y, hr1.y
      0x00060002, 0x40600002, // mul.f hr0.z, hr0.z, hr1.z
      0x00070003, 0x40600003, // mul.f hr0.w, hr0.w, hr1.w
      0x00000000, 0x03000000, // end
   };

   tu_cs_emit_array(&shader_cs, raw_shader, ARRAY_SIZE(raw_shader));
   struct tu_cs_entry shader_entry = tu_cs_end_sub_stream(&device->sub_cs, &shader_cs);
   uint64_t shader_iova = shader_entry.bo->iova + shader_entry.offset;

   struct tu_cs sub_cs;
   tu_cs_begin_sub_stream(&device->sub_cs, 47, &sub_cs);

   tu_cs_emit_regs(&sub_cs, SP_UPDATE_CNTL(A7XX,
            .vs_state = true, .hs_state = true, .ds_state = true,
            .gs_state = true, .fs_state = true, .gfx_uav = true,
            .cs_bindless = 0xff, .gfx_bindless = 0xff));
   tu_cs_emit_regs(&sub_cs, SP_CS_CONST_CONFIG(A7XX,
            .constlen = 4,
            .enabled = true));
   tu_cs_emit_regs(&sub_cs, A6XX_SP_CS_CONFIG(.enabled = true));
   tu_cs_emit_regs(&sub_cs, A6XX_SP_CS_CNTL_0(
            .threadmode = MULTI,
            .threadsize = THREAD128,
            .mergedregs = true));
   tu_cs_emit_regs(&sub_cs, A6XX_SP_CS_CNTL_1(.shared_size = 1));
   tu_cs_emit_regs(&sub_cs, SP_CS_KERNEL_GROUP_X(A7XX, 1),
                     SP_CS_KERNEL_GROUP_Y(A7XX, 1),
                     SP_CS_KERNEL_GROUP_Z(A7XX, 1));
   tu_cs_emit_regs(&sub_cs, A6XX_SP_CS_INSTR_SIZE(.sp_cs_instr_size = 1));
   tu_cs_emit_regs(&sub_cs, A6XX_SP_CS_TSIZE(0));
   tu_cs_emit_regs(&sub_cs, A6XX_SP_CS_USIZE(0));
   tu_cs_emit_regs(&sub_cs, SP_CS_WGE_CNTL(A7XX,
            .linearlocalidregid = regid(63, 0),
            .threadsize = THREAD128,
            .workgrouprastorderzfirsten = true,
            .wgtilewidth = 4,
            .wgtileheight = 17));
   tu_cs_emit_regs(&sub_cs, A6XX_SP_CS_WIE_CNTL_0(
            .wgidconstid = regid(51, 3),
            .wgsizeconstid = regid(48, 0),
            .wgoffsetconstid = regid(63, 0),
            .localidregid = regid(63, 0)));
   tu_cs_emit_regs(&sub_cs, SP_CS_WIE_CNTL_1(A7XX,
            .linearlocalidregid = regid(63, 0),
            .threadsize = THREAD128,
            .workitemrastorder = WORKITEMRASTORDER_TILED));
   tu_cs_emit_regs(&sub_cs, SP_CS_HYSTERESIS(A7XX, 0));

   tu_cs_emit_regs(&sub_cs,
                  SP_CS_NDRANGE_0(A7XX, .kerneldim = 3,
                                          .localsizex = 255,
                                          .localsizey = 1,
                                          .localsizez = 1),
                  SP_CS_NDRANGE_1(A7XX, .globalsize_x = 3072),
                  SP_CS_NDRANGE_2(A7XX, .globaloff_x = 0),
                  SP_CS_NDRANGE_3(A7XX, .globalsize_y = 1),
                  SP_CS_NDRANGE_4(A7XX, .globaloff_y = 0),
                  SP_CS_NDRANGE_5(A7XX, .globalsize_z = 1),
                  SP_CS_NDRANGE_6(A7XX, .globaloff_z = 0));
   tu_cs_emit_regs(&sub_cs, SP_CS_NDRANGE_7(A7XX,
            .localsizex = 255,
            .localsizey = 0,
            .localsizez = 0));
   tu_cs_emit_pkt4(&sub_cs, REG_A6XX_SP_CS_PROGRAM_COUNTER_OFFSET, 3);
   tu_cs_emit(&sub_cs, 0);
   tu_cs_emit_qw(&sub_cs, shader_iova);

   tu_cs_emit_pkt7(&sub_cs, CP_EXEC_CS, 4);
   tu_cs_emit(&sub_cs, 0x00000000);
   tu_cs_emit(&sub_cs, CP_EXEC_CS_1_NGROUPS_X(12));
   tu_cs_emit(&sub_cs, CP_EXEC_CS_2_NGROUPS_Y(1));
   tu_cs_emit(&sub_cs, CP_EXEC_CS_3_NGROUPS_Z(1));

   device->cmdbuf_start_a725_quirk_entry =
      tu_cs_end_sub_stream(&device->sub_cs, &sub_cs);

   return VK_SUCCESS;
}

static VkResult
tu_device_get_timestamp(struct vk_device *vk_device, uint64_t *timestamp)
{
   struct tu_device *dev = container_of(vk_device, struct tu_device, vk);
   const int ret = tu_device_get_gpu_timestamp(dev, timestamp);
   return ret == 0 ? VK_SUCCESS : VK_ERROR_UNKNOWN;
}

void
tu_device_destroy_mutexes(struct tu_device *device)
{
   mtx_destroy(&device->bo_mutex);
   mtx_destroy(&device->pipeline_mutex);
   mtx_destroy(&device->autotune_mutex);
   mtx_destroy(&device->kgsl_profiling_mutex);
   mtx_destroy(&device->event_mutex);
   mtx_destroy(&device->trace_mutex);
   mtx_destroy(&device->radix_sort_mutex);
   mtx_destroy(&device->fiber_pvtmem_bo.mtx);
   mtx_destroy(&device->wave_pvtmem_bo.mtx);
   mtx_destroy(&device->mutex);
   mtx_destroy(&device->copy_timestamp_cs_pool_mutex);
#ifdef HAVE_PERFETTO
   mtx_destroy(&device->perfetto.pending_clocks_sync_mtx);
#endif
   for (unsigned i = 0; i < ARRAY_SIZE(device->scratch_bos); i++)
      mtx_destroy(&device->scratch_bos[i].construct_mtx);

   u_rwlock_destroy(&device->dma_bo_lock);
   u_rwlock_destroy(&device->vm_bind_fence_lock);
   pthread_mutex_destroy(&device->submit_mutex);

   if (device->physical_device->has_set_iova) {
      mtx_destroy(&device->vma_mutex);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateDevice(VkPhysicalDevice physicalDevice,
                const VkDeviceCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkDevice *pDevice)
{
   VK_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);
   VkResult result;
   struct tu_device *device;
   bool border_color_without_format = false;

   vk_foreach_struct_const (ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT:
         border_color_without_format =
            ((const VkPhysicalDeviceCustomBorderColorFeaturesEXT *) ext)
               ->customBorderColorWithoutFormat;
         break;
      default:
         break;
      }
   }

   device = (struct tu_device *) vk_zalloc2(
      &physical_device->instance->vk.alloc, pAllocator, sizeof(*device), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_startup_errorf(physical_device->instance, VK_ERROR_OUT_OF_HOST_MEMORY, "OOM");

   struct vk_device_dispatch_table dispatch_table;
   bool override_initial_entrypoints = true;

   if (physical_device->instance->vk.trace_mode & VK_TRACE_MODE_RMV) {
      vk_device_dispatch_table_from_entrypoints(
         &dispatch_table, &tu_rmv_device_entrypoints, true);
      override_initial_entrypoints = false;
   }

   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &tu_device_entrypoints, override_initial_entrypoints);

   switch (fd_dev_gen(&physical_device->dev_id)) {
   case 6:
      vk_device_dispatch_table_from_entrypoints(
         &dispatch_table, &tu_device_entrypoints_a6xx, false);
      break;
   case 7:
      vk_device_dispatch_table_from_entrypoints(
         &dispatch_table, &tu_device_entrypoints_a7xx, false);
   }

   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_device_entrypoints, false);

   const struct vk_device_entrypoint_table *knl_device_entrypoints =
         physical_device->instance->knl->device_entrypoints;
   if (knl_device_entrypoints) {
      vk_device_dispatch_table_from_entrypoints(
         &dispatch_table, knl_device_entrypoints, false);
   }

   result = vk_device_init(&device->vk, &physical_device->vk,
                           &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return vk_startup_errorf(physical_device->instance, result,
                               "vk_device_init failed");
   }

   device->instance = physical_device->instance;
   device->physical_device = physical_device;
   device->device_idx = device->physical_device->device_count++;

   result = tu_drm_device_init(device);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return result;
   }

   device->vk.command_buffer_ops = &tu_cmd_buffer_ops;
   device->vk.as_build_ops = &tu_as_build_ops;
   device->vk.check_status = tu_device_check_status;
   device->vk.get_timestamp = tu_device_get_timestamp;

   mtx_init(&device->bo_mutex, mtx_plain);
   mtx_init(&device->pipeline_mutex, mtx_plain);
   mtx_init(&device->autotune_mutex, mtx_plain);
   mtx_init(&device->kgsl_profiling_mutex, mtx_plain);
   mtx_init(&device->event_mutex, mtx_plain);
   mtx_init(&device->trace_mutex, mtx_plain);
   mtx_init(&device->radix_sort_mutex, mtx_plain);
   mtx_init(&device->fiber_pvtmem_bo.mtx, mtx_plain);
   mtx_init(&device->wave_pvtmem_bo.mtx, mtx_plain);
   mtx_init(&device->vis_stream_mtx, mtx_plain);
   mtx_init(&device->vis_stream_suballocator_mtx, mtx_plain);
   mtx_init(&device->mutex, mtx_plain);
   mtx_init(&device->copy_timestamp_cs_pool_mutex, mtx_plain);
   mtx_init(&device->softfloat_mutex, mtx_plain);
#ifdef HAVE_PERFETTO
   mtx_init(&device->perfetto.pending_clocks_sync_mtx, mtx_plain);
#endif
   for (unsigned i = 0; i < ARRAY_SIZE(device->scratch_bos); i++)
      mtx_init(&device->scratch_bos[i].construct_mtx, mtx_plain);

   u_rwlock_init(&device->dma_bo_lock);
   u_rwlock_init(&device->vm_bind_fence_lock);
   pthread_mutex_init(&device->submit_mutex, NULL);
   device->vm_bind_fence_fd = -1;

   if (physical_device->has_set_iova) {
      mtx_init(&device->vma_mutex, mtx_plain);
      util_vma_heap_init(&device->vma, physical_device->va_start,
                         ROUND_DOWN_TO(physical_device->va_size, os_page_size));
   }

   if (TU_DEBUG(BOS))
      device->bo_sizes = _mesa_hash_table_create(NULL, _mesa_hash_string, _mesa_key_string_equal);

   if (physical_device->instance->vk.trace_mode & VK_TRACE_MODE_RMV)
      tu_memory_trace_init(device);

   /* kgsl is not a drm device: */
   if (!is_kgsl(physical_device->instance) && (device->fd >= 0))
      vk_device_set_drm_fd(&device->vk, device->fd);

   struct tu6_global *global = NULL;
   uint32_t global_size = sizeof(struct tu6_global);
   struct vk_pipeline_cache_create_info pcc_info = { };

   for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      const VkDeviceQueueCreateInfo *queue_create =
         &pCreateInfo->pQueueCreateInfos[i];
      uint32_t qfi = queue_create->queueFamilyIndex;
      enum tu_queue_type type = physical_device->queue_families[qfi].type;
      device->queues[qfi] = (struct tu_queue *) vk_alloc(
         &device->vk.alloc,
         queue_create->queueCount * sizeof(struct tu_queue), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!device->queues[qfi]) {
         result = vk_startup_errorf(physical_device->instance,
                                    VK_ERROR_OUT_OF_HOST_MEMORY,
                                    "OOM");
         goto fail_queues;
      }

      memset(device->queues[qfi], 0,
             queue_create->queueCount * sizeof(struct tu_queue));

      device->queue_count[qfi] = queue_create->queueCount;

      for (unsigned q = 0; q < queue_create->queueCount; q++) {
         result = tu_queue_init(device, &device->queues[qfi][q], type, q,
                                queue_create);
         if (result != VK_SUCCESS) {
            device->queue_count[qfi] = q;
            goto fail_queues;
         }
      }
   }

   result = vk_meta_device_init(&device->vk, &device->meta);
   if (result != VK_SUCCESS)
      goto fail_queues;

   {
      struct ir3_compiler_options ir3_options = {
         .push_ubo_with_preamble = true,
         .disable_cache = true,
         .bindless_fb_read_descriptor = -1,
         .bindless_fb_read_slot = -1,
         .storage_16bit = physical_device->info->props.storage_16bit,
         .storage_8bit = physical_device->info->props.storage_8bit,
         .shared_push_consts = !TU_DEBUG(PUSH_CONSTS_PER_STAGE),
         .uche_trap_base = physical_device->uche_trap_base,
      };
      device->compiler = ir3_compiler_create(
         NULL, &physical_device->dev_id, physical_device->info, &ir3_options);
   }
   if (!device->compiler) {
      result = vk_startup_errorf(physical_device->instance,
                                 VK_ERROR_INITIALIZATION_FAILED,
                                 "failed to initialize ir3 compiler");
      goto fail_compiler;
   }

   /* Initialize sparse array for refcounting imported BOs */
   util_sparse_array_init(&device->bo_map, sizeof(struct tu_bo), 512);

   if (physical_device->has_set_iova && !physical_device->has_vm_bind) {
      if (!u_vector_init(&device->zombie_vmas, 64,
                         sizeof(struct tu_zombie_vma))) {
         result = vk_startup_errorf(physical_device->instance,
                                    VK_ERROR_INITIALIZATION_FAILED,
                                    "zombie_vmas create failed");
         goto fail_free_zombie_vma;
      }
   }

   /* initial sizes, these will increase if there is overflow */
   device->vsc_draw_strm_pitch = 0x1000 + VSC_PAD;
   device->vsc_prim_strm_pitch = 0x4000 + VSC_PAD;

   if (device->vk.enabled_features.customBorderColors)
      global_size += TU_BORDER_COLOR_COUNT * sizeof(struct bcolor_entry);

   tu_bo_suballocator_init(
      &device->pipeline_suballoc, device, 128 * 1024,
      (enum tu_bo_alloc_flags) (TU_BO_ALLOC_GPU_READ_ONLY |
                                TU_BO_ALLOC_ALLOW_DUMP |
                                TU_BO_ALLOC_INTERNAL_RESOURCE),
      "pipeline_suballoc");
   tu_bo_suballocator_init(&device->autotune_suballoc, device,
                           128 * 1024, TU_BO_ALLOC_INTERNAL_RESOURCE,
                           "autotune_suballoc");
   if (is_kgsl(physical_device->instance)) {
      tu_bo_suballocator_init(&device->kgsl_profiling_suballoc, device,
                              128 * 1024, TU_BO_ALLOC_INTERNAL_RESOURCE,
                              "kgsl_profiling_suballoc");
   }

   tu_bo_suballocator_init(&device->event_suballoc, device,
      getpagesize(), TU_BO_ALLOC_INTERNAL_RESOURCE,
      "event_suballoc");

   tu_bo_suballocator_init(
      &device->vis_stream_suballocator, device,
      getpagesize(),
      (enum tu_bo_alloc_flags)(TU_BO_ALLOC_INTERNAL_RESOURCE |
                               TU_BO_ALLOC_ALLOW_DUMP),
      "vis_stream_suballoc");

   result = tu_bo_init_new(
      device, NULL, &device->global_bo, global_size,
      (enum tu_bo_alloc_flags) (TU_BO_ALLOC_ALLOW_DUMP |
                                TU_BO_ALLOC_INTERNAL_RESOURCE),
      "global");
   if (result != VK_SUCCESS) {
      vk_startup_errorf(device->instance, result, "BO init");
      goto fail_global_bo;
   }

   result = tu_bo_map(device, device->global_bo, NULL);
   if (result != VK_SUCCESS) {
      vk_startup_errorf(device->instance, result, "BO map");
      goto fail_global_bo_map;
   }

   global = (struct tu6_global *)device->global_bo->map;
   device->global_bo_map = global;
   tu_init_clear_blit_shaders(device);

   if (device->vk.enabled_features.accelerationStructure &&
       device->vk.enabled_features.nullDescriptor) {
      result = tu_init_null_accel_struct(device);
      if (result != VK_SUCCESS) {
         vk_startup_errorf(device->instance, result, "null acceleration struct");
         goto fail_null_accel_struct;
      }
   }

   result = tu_init_empty_shaders(device);
   if (result != VK_SUCCESS) {
      vk_startup_errorf(device->instance, result, "empty shaders");
      goto fail_empty_shaders;
   }

   global->predicate = 0;
   global->vtx_stats_query_not_running = 1;
   global->dbg_one = (uint32_t)-1;
   global->dbg_gmem_total_loads = 0;
   global->dbg_gmem_taken_loads = 0;
   global->dbg_gmem_total_stores = 0;
   global->dbg_gmem_taken_stores = 0;

   /* initialize to ones so ffs can be used to find unused slots */
   BITSET_ONES(device->custom_border_color);

   result = tu_init_dynamic_rendering(device);
   if (result != VK_SUCCESS) {
      vk_startup_errorf(device->instance, result, "dynamic rendering");
      goto fail_dynamic_rendering;
   }

   device->mem_cache = vk_pipeline_cache_create(&device->vk, &pcc_info,
                                                NULL);
   if (!device->mem_cache) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      vk_startup_errorf(device->instance, result, "create pipeline cache failed");
      goto fail_pipeline_cache;
   }

   tu_cs_init(&device->sub_cs, device, TU_CS_MODE_SUB_STREAM, 1024, "device sub cs");

   if (device->vk.enabled_features.performanceCounterQueryPools) {
      /* Prepare command streams setting pass index to the PERF_CNTRS_REG
       * from 0 to 31. One of these will be picked up at cmd submit time
       * when the perf query is executed.
       */

      device->perfcntrs_pass_cs_entries =
         (struct tu_cs_entry *) calloc(32, sizeof(struct tu_cs_entry));
      if (!device->perfcntrs_pass_cs_entries) {
         result = vk_startup_errorf(device->instance,
               VK_ERROR_OUT_OF_HOST_MEMORY, "OOM");
         goto fail_perfcntrs_pass_entries_alloc;
      }

      for (unsigned i = 0; i < 32; i++) {
         struct tu_cs sub_cs;

         result = tu_cs_begin_sub_stream(&device->sub_cs, 3, &sub_cs);
         if (result != VK_SUCCESS) {
            vk_startup_errorf(device->instance, result,
                  "failed to allocate commands streams");
            goto fail_prepare_perfcntrs_pass_cs;
         }

         /* TODO: a8xx */
         tu_cs_emit_regs(&sub_cs, CP_SCRATCH_REG(A6XX, PERF_CNTRS_REG, 1 << i));
         tu_cs_emit_pkt7(&sub_cs, CP_WAIT_FOR_ME, 0);

         device->perfcntrs_pass_cs_entries[i] =
            tu_cs_end_sub_stream(&device->sub_cs, &sub_cs);
      }
   }

   result = tu_init_bin_preamble(device);
   if (result != VK_SUCCESS)
      goto fail_bin_preamble;

   if (physical_device->info->props.cmdbuf_start_a725_quirk) {
         result = tu_init_cmdbuf_start_a725_quirk(device);
         if (result != VK_SUCCESS)
            goto fail_a725_workaround;
   }

   tu_init_dbg_reg_stomper(device);

   /* Initialize a condition variable for timeline semaphore */
   pthread_condattr_t condattr;
   if (pthread_condattr_init(&condattr) != 0) {
      result = vk_startup_errorf(physical_device->instance,
                                 VK_ERROR_INITIALIZATION_FAILED,
                                 "pthread condattr init");
      goto fail_timeline_cond;
   }
   if (pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_startup_errorf(physical_device->instance,
                                 VK_ERROR_INITIALIZATION_FAILED,
                                 "pthread condattr clock setup");
      goto fail_timeline_cond;
   }
   if (pthread_cond_init(&device->timeline_cond, &condattr) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_startup_errorf(physical_device->instance,
                                 VK_ERROR_INITIALIZATION_FAILED,
                                 "pthread cond init");
      goto fail_timeline_cond;
   }
   pthread_condattr_destroy(&condattr);

   result = tu_autotune_init(&device->autotune, device);
   if (result != VK_SUCCESS) {
      goto fail_timeline_cond;
   }

   device->use_z24uint_s8uint =
      physical_device->info->props.has_z24uint_s8uint &&
      (!border_color_without_format ||
       physical_device->instance->disable_d24s8_border_color_workaround);
   device->use_lrz = !TU_DEBUG_START(NOLRZ);

   tu_gpu_tracepoint_config_variable();

   device->submit_count = 0;
   list_inithead(&device->copy_timestamp_cs_pool);
   u_trace_context_init(&device->trace_context, device,
                     sizeof(uint64_t),
                     12,
                     tu_trace_create_buffer,
                     tu_trace_destroy_buffer,
                     TU_CALLX(device, tu_trace_record_ts),
                     tu_trace_read_ts,
                     tu_trace_capture_data,
                     tu_trace_get_data,
                     tu_trace_delete_flush_data);

   tu_breadcrumbs_init(device);

   if (FD_RD_DUMP(ENABLE)) {
      struct vk_app_info *app_info = &device->instance->vk.app_info;
      const char *app_name_str = app_info->app_name ?
         app_info->app_name : util_get_process_name();
      const char *engine_name_str = app_info->engine_name ?
         app_info->engine_name : "unknown-engine";

      char app_name[64];
      snprintf(app_name, sizeof(app_name), "%s", app_name_str);

      char engine_name[32];
      snprintf(engine_name, sizeof(engine_name), "%s", engine_name_str);

      char output_name[128];
      snprintf(output_name, sizeof(output_name), "tu_%s.%s_instance%u_device%u",
               app_name, engine_name, device->instance->instance_idx,
               device->device_idx);

      fd_rd_output_init(&device->rd_output, output_name);
   }

   device->vk.cmd_dispatch_unaligned = tu_dispatch_unaligned;
   device->vk.write_buffer_cp = tu_write_buffer_cp;
   device->vk.flush_buffer_write_cp = tu_flush_buffer_write_cp;
   device->vk.cmd_fill_buffer_addr = tu_cmd_fill_buffer_addr;

   device->vis_stream_count = 0;

   *pDevice = tu_device_to_handle(device);
   return VK_SUCCESS;

fail_timeline_cond:
fail_a725_workaround:
fail_bin_preamble:
fail_prepare_perfcntrs_pass_cs:
   free(device->perfcntrs_pass_cs_entries);
fail_perfcntrs_pass_entries_alloc:
   tu_cs_finish(&device->sub_cs);
   vk_pipeline_cache_destroy(device->mem_cache, &device->vk.alloc);
fail_pipeline_cache:
   tu_destroy_dynamic_rendering(device);
fail_dynamic_rendering:
   tu_destroy_empty_shaders(device);
fail_empty_shaders:
   if (device->null_accel_struct_bo)
      tu_bo_finish(device, device->null_accel_struct_bo);
fail_null_accel_struct:
   tu_destroy_clear_blit_shaders(device);
fail_global_bo_map:
   TU_RMV(resource_destroy, device, device->global_bo);
   tu_bo_finish(device, device->global_bo);
   vk_free(&device->vk.alloc, device->submit_bo_list);
   util_dynarray_fini(&device->dump_bo_list);
fail_global_bo:
   if (physical_device->has_set_iova)
      util_vma_heap_finish(&device->vma);
fail_free_zombie_vma:
   util_sparse_array_finish(&device->bo_map);
   u_vector_finish(&device->zombie_vmas);
   ir3_compiler_destroy(device->compiler);
fail_compiler:
   vk_meta_device_finish(&device->vk, &device->meta);
fail_queues:
   for (unsigned i = 0; i < TU_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         tu_queue_finish(&device->queues[i][q]);
      if (device->queues[i])
         vk_free(&device->vk.alloc, device->queues[i]);
   }

   tu_device_destroy_mutexes(device);
   tu_drm_device_finish(device);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   if (!device)
      return;

   tu_memory_trace_finish(device);

   if (FD_RD_DUMP(ENABLE))
      fd_rd_output_fini(&device->rd_output);

   tu_breadcrumbs_finish(device);

   u_trace_context_fini(&device->trace_context);

   for (unsigned i = 0; i < ARRAY_SIZE(device->scratch_bos); i++) {
      if (device->scratch_bos[i].initialized)
         tu_bo_finish(device, device->scratch_bos[i].bo);
   }

   if (device->fiber_pvtmem_bo.bo)
      tu_bo_finish(device, device->fiber_pvtmem_bo.bo);

   if (device->wave_pvtmem_bo.bo)
      tu_bo_finish(device, device->wave_pvtmem_bo.bo);

   tu_destroy_clear_blit_shaders(device);

   tu_destroy_empty_shaders(device);

   tu_destroy_dynamic_rendering(device);

   vk_meta_device_finish(&device->vk, &device->meta);

   tu_destroy_softfloat(device);

   ir3_compiler_destroy(device->compiler);

   vk_pipeline_cache_destroy(device->mem_cache, &device->vk.alloc);

   tu_cs_finish(&device->sub_cs);

   free(device->perfcntrs_pass_cs_entries);

   if (device->dbg_cmdbuf_stomp_cs) {
      tu_cs_finish(device->dbg_cmdbuf_stomp_cs);
      free(device->dbg_cmdbuf_stomp_cs);
   }

   if (device->dbg_renderpass_stomp_cs) {
      tu_cs_finish(device->dbg_renderpass_stomp_cs);
      free(device->dbg_renderpass_stomp_cs);
   }

   tu_autotune_fini(&device->autotune, device);

   tu_bo_suballocator_finish(&device->pipeline_suballoc);
   tu_bo_suballocator_finish(&device->autotune_suballoc);
   tu_bo_suballocator_finish(&device->kgsl_profiling_suballoc);
   tu_bo_suballocator_finish(&device->event_suballoc);
   tu_bo_suballocator_finish(&device->vis_stream_suballocator);

   tu_bo_finish(device, device->global_bo);

   if (device->vm_bind_fence_fd != -1)
      close(device->vm_bind_fence_fd);

   if (device->vis_stream_bo)
      tu_bo_finish(device, device->vis_stream_bo);

   if (device->null_accel_struct_bo)
      tu_bo_finish(device, device->null_accel_struct_bo);

   list_for_each_entry_safe(struct tu_copy_timestamp_data, data,
                            &device->copy_timestamp_cs_pool, node)
   {
      tu_free_copy_timestamp_data(device, data);
   }

   if (device->trace_suballoc) {
      tu_bo_suballocator_finish(device->trace_suballoc);
      vk_free(&device->vk.alloc, device->trace_suballoc);
   }

   for (unsigned i = 0; i < TU_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++)
         tu_queue_finish(&device->queues[i][q]);
      if (device->queue_count[i])
         vk_free(&device->vk.alloc, device->queues[i]);
   }

   if (device->msrtss_color_temporary)
      tu_destroy_memory(device, device->msrtss_color_temporary);
   if (device->msrtss_depth_temporary)
      tu_destroy_memory(device, device->msrtss_depth_temporary);

   tu_drm_device_finish(device);

   if (device->physical_device->has_set_iova)
      util_vma_heap_finish(&device->vma);

   util_sparse_array_finish(&device->bo_map);
   tu_device_destroy_mutexes(device);

   u_vector_finish(&device->zombie_vmas);

   pthread_cond_destroy(&device->timeline_cond);
   _mesa_hash_table_destroy(device->bo_sizes, NULL);
   vk_free(&device->vk.alloc, device->submit_bo_list);
   util_dynarray_fini(&device->dump_bo_list);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VkResult
tu_get_scratch_bo(struct tu_device *dev, uint64_t size, struct tu_bo **bo)
{
   unsigned size_log2 = MAX2(util_logbase2_ceil64(size), MIN_SCRATCH_BO_SIZE_LOG2);
   unsigned index = size_log2 - MIN_SCRATCH_BO_SIZE_LOG2;
   assert(index < ARRAY_SIZE(dev->scratch_bos));

   for (unsigned i = index; i < ARRAY_SIZE(dev->scratch_bos); i++) {
      if (p_atomic_read(&dev->scratch_bos[i].initialized)) {
         /* Fast path: just return the already-allocated BO. */
         *bo = dev->scratch_bos[i].bo;
         return VK_SUCCESS;
      }
   }

   /* Slow path: actually allocate the BO. We take a lock because the process
    * of allocating it is slow, and we don't want to block the CPU while it
    * finishes.
   */
   mtx_lock(&dev->scratch_bos[index].construct_mtx);

   /* Another thread may have allocated it already while we were waiting on
    * the lock. We need to check this in order to avoid double-allocating.
    */
   if (dev->scratch_bos[index].initialized) {
      mtx_unlock(&dev->scratch_bos[index].construct_mtx);
      *bo = dev->scratch_bos[index].bo;
      return VK_SUCCESS;
   }

   unsigned bo_size = 1ull << size_log2;
   VkResult result = tu_bo_init_new(dev, NULL, &dev->scratch_bos[index].bo, bo_size,
                                    TU_BO_ALLOC_INTERNAL_RESOURCE, "scratch");
   if (result != VK_SUCCESS) {
      mtx_unlock(&dev->scratch_bos[index].construct_mtx);
      return result;
   }

   p_atomic_set(&dev->scratch_bos[index].initialized, true);

   mtx_unlock(&dev->scratch_bos[index].construct_mtx);

   *bo = dev->scratch_bos[index].bo;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                    VkLayerProperties *pProperties)
{
   *pPropertyCount = 0;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                        uint32_t *pPropertyCount,
                                        VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &tu_instance_extensions_supported, pPropertyCount, pProperties);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
tu_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(tu_instance, instance, _instance);
   return vk_instance_get_proc_addr(instance != NULL ? &instance->vk : NULL,
                                    &tu_instance_entrypoints,
                                    pName);
}

/* The loader wants us to expose a second GetInstanceProcAddr function
 * to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return tu_GetInstanceProcAddr(instance, pName);
}

static VkResult
tu_add_to_heap(struct tu_device *dev, struct tu_bo *bo)
{
   struct tu_memory_heap *mem_heap = &dev->physical_device->heap;
   uint64_t mem_heap_used = p_atomic_add_return(&mem_heap->used, bo->size);
   if (mem_heap_used > mem_heap->size) {
      p_atomic_add(&mem_heap->used, -bo->size);
      tu_bo_finish(dev, bo);
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Out of heap memory");
   }
   return VK_SUCCESS;
}

static VkResult
_tu_init_memory(struct tu_device *device,
                struct tu_device_memory *mem,
                VkMemoryPropertyFlags mem_property,
                enum tu_bo_alloc_flags alloc_flags,
                VkDeviceSize size,
                VkDeviceAddress client_address,
                const char *name)
{
   VkResult result;

   if (mem_property & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
      mem->lazy = true;
      mtx_init(&mem->lazy_mutex, mtx_plain);
      enum tu_sparse_vma_flags sparse_flags =
         (alloc_flags & TU_BO_ALLOC_REPLAYABLE) ?
         TU_SPARSE_VMA_REPLAYABLE : TU_SPARSE_VMA_NONE;
      result = tu_sparse_vma_init(device, &mem->vk.base,
                                  &mem->lazy_vma, &mem->iova,
                                  sparse_flags,
                                  size,
                                  client_address);
   } else {
      result = tu_bo_init_new_explicit_iova(
         device, &mem->vk.base, &mem->bo, size,
         client_address, mem_property, alloc_flags, NULL, name);
   }

   return result;
}

static VkResult
tu_create_memory(struct tu_device *device,
                 struct tu_device_memory **mem_out,
                 VkMemoryPropertyFlags mem_property,
                 enum tu_bo_alloc_flags alloc_flags,
                 VkDeviceSize size,
                 const char *name)
{
   struct tu_device_memory *mem =
      (struct tu_device_memory *) vk_object_zalloc(
         &device->vk, NULL, sizeof(*mem), VK_OBJECT_TYPE_DEVICE_MEMORY);

   if (!mem)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   mem->vk.size = size;
   mem->size = size;
   mem->refcnt = 1;

   VkResult result = _tu_init_memory(device, mem, mem_property, alloc_flags,
                                     size, 0, name);

   if (result != VK_SUCCESS) {
      vk_object_free(&device->vk, NULL, mem);
      return result;
   }

   if (!mem->lazy) {
      result = tu_add_to_heap(device, mem->bo);
      mem->iova = mem->bo->iova;

      if (result != VK_SUCCESS) {
         vk_object_free(&device->vk, NULL, mem);
         return result;
      }
   }

   *mem_out = mem;
   return VK_SUCCESS;
}

static struct tu_device_memory *
tu_memory_get_ref(struct tu_device_memory *mem)
{
   p_atomic_inc(&mem->refcnt);
   return mem;
}

static void
_tu_destroy_memory(struct tu_device *device,
                  struct tu_device_memory *mem)
{
   if (mem->bo) {
      p_atomic_add(&device->physical_device->heap.used, -mem->bo->size);
      tu_bo_finish(device, mem->bo);
   }

   if (mem->lazy) {
      tu_sparse_vma_finish(device, &mem->lazy_vma);
      mtx_destroy(&mem->lazy_mutex);
   }
}

void
tu_destroy_memory(struct tu_device *device,
                  struct tu_device_memory *mem)
{
   if (!p_atomic_dec_zero(&mem->refcnt))
      return;

   _tu_destroy_memory(device, mem);

   vk_object_free(&device->vk, NULL, mem);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_AllocateMemory(VkDevice _device,
                  const VkMemoryAllocateInfo *pAllocateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkDeviceMemory *pMem)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_device_memory *mem;
   VkResult result;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   struct tu_memory_heap *mem_heap = &device->physical_device->heap;
   uint64_t mem_heap_used = p_atomic_read(&mem_heap->used);
   if (mem_heap_used > mem_heap->size)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   mem = (struct tu_device_memory *) vk_device_memory_create(
      &device->vk, pAllocateInfo, pAllocator, sizeof(*mem));
   if (mem == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocateInfo->allocationSize == 0 && !mem->vk.ahardware_buffer) {
      vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
      /* Apparently, this is allowed */
      *pMem = VK_NULL_HANDLE;
      return VK_SUCCESS;
   }

   mem->size = pAllocateInfo->allocationSize;
   mem->refcnt = 1;

   const VkImportMemoryFdInfoKHR *fd_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);

   if (fd_info && fd_info->handleType) {
      assert(fd_info->handleType ==
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
             fd_info->handleType ==
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      /*
       * TODO Importing the same fd twice gives us the same handle without
       * reference counting.  We need to maintain a per-instance handle-to-bo
       * table and add reference count to tu_bo.
       */
      result = tu_bo_init_dmabuf(device, &mem->bo,
                                 pAllocateInfo->allocationSize, fd_info->fd);
      if (result == VK_SUCCESS) {
         /* take ownership and close the fd */
         close(fd_info->fd);
      }
   } else if (mem->vk.ahardware_buffer) {
#if DETECT_OS_ANDROID
      const native_handle_t *handle = AHardwareBuffer_getNativeHandle(mem->vk.ahardware_buffer);
      assert(handle->numFds > 0);
      size_t size = lseek(handle->data[0], 0, SEEK_END);
      result = tu_bo_init_dmabuf(device, &mem->bo, size, handle->data[0]);
#else
      result = VK_ERROR_FEATURE_NOT_PRESENT;
#endif
   } else {
      uint64_t client_address = 0;
      BITMASK_ENUM(tu_bo_alloc_flags) alloc_flags = TU_BO_ALLOC_NO_FLAGS;

      const VkMemoryOpaqueCaptureAddressAllocateInfo *replay_info =
         vk_find_struct_const(pAllocateInfo->pNext,
                              MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO);
      if (replay_info && replay_info->opaqueCaptureAddress) {
         client_address = replay_info->opaqueCaptureAddress;
         alloc_flags |= TU_BO_ALLOC_REPLAYABLE;
      }

      const VkMemoryAllocateFlagsInfo *flags_info = vk_find_struct_const(
         pAllocateInfo->pNext, MEMORY_ALLOCATE_FLAGS_INFO);
      if (flags_info &&
          (flags_info->flags &
           VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT)) {
         alloc_flags |= TU_BO_ALLOC_REPLAYABLE;
      }

      const VkExportMemoryAllocateInfo *export_info =
         vk_find_struct_const(pAllocateInfo->pNext, EXPORT_MEMORY_ALLOCATE_INFO);
      if (export_info && (export_info->handleTypes &
                          (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
                           VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)))
         alloc_flags |= TU_BO_ALLOC_SHAREABLE;

      const struct wsi_memory_allocate_info *wsi_info =
         vk_find_struct_const(pAllocateInfo->pNext, WSI_MEMORY_ALLOCATE_INFO_MESA);
      if (wsi_info && wsi_info->implicit_sync)
         alloc_flags |= TU_BO_ALLOC_IMPLICIT_SYNC;

      char name[64] = "vkAllocateMemory()";
      if (device->bo_sizes)
         snprintf(name, ARRAY_SIZE(name), "vkAllocateMemory(%ldkb)",
                  (long)DIV_ROUND_UP(pAllocateInfo->allocationSize, 1024));
      VkMemoryPropertyFlags mem_property =
         device->physical_device->memory.types[pAllocateInfo->memoryTypeIndex];

      result = _tu_init_memory(device, mem, mem_property, alloc_flags,
                               pAllocateInfo->allocationSize, client_address,
                               name);
   }

   if (result == VK_SUCCESS && !mem->lazy) {
      result = tu_add_to_heap(device, mem->bo);
      mem->iova = mem->bo->iova;
   }

   if (result != VK_SUCCESS) {
      vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
      return result;
   }

   const VkMemoryDedicatedAllocateInfo *dedicate_info =
      vk_find_struct_const(pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   if (dedicate_info) {
      mem->image = tu_image_from_handle(dedicate_info->image);
   } else {
      mem->image = NULL;
   }

   TU_RMV(heap_create, device, pAllocateInfo, mem);

   *pMem = tu_device_memory_to_handle(mem);

   return VK_SUCCESS;
}

VkResult
tu_allocate_lazy_memory(struct tu_device *dev,
                        struct tu_device_memory *mem)
{
   assert(mem->lazy);

   if (mem->lazy_initialized) {
      if (mem->bo)
         return VK_SUCCESS;
      else
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   VkResult result = VK_SUCCESS;
   mtx_lock(&mem->lazy_mutex);
   if (!mem->lazy_initialized) {
      char name[64] = "lazy vkAllocateMemory()";
      if (dev->bo_sizes)
         snprintf(name, ARRAY_SIZE(name), "lazy vkAllocateMemory(%ldkb)",
                  (long)DIV_ROUND_UP(mem->size, 1024));
      result =
         tu_bo_init_new_explicit_iova(dev, &mem->vk.base,
                                      &mem->bo, mem->size, 0, 0,
                                      TU_BO_ALLOC_NO_FLAGS,
                                      &mem->lazy_vma, name);
      mem->lazy_initialized = true;

      if (result == VK_SUCCESS) {
         result = tu_add_to_heap(dev, mem->bo);

         if (result != VK_SUCCESS) {
            tu_bo_finish(dev, mem->bo);
            mem->bo = NULL;
         }
      }
   }
   mtx_unlock(&mem->lazy_mutex);

   /* Fail if another thread won the race and failed to allocate a BO */
   if (result == VK_SUCCESS && !mem->bo) {
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   return result;
}


VKAPI_ATTR void VKAPI_CALL
tu_FreeMemory(VkDevice _device,
              VkDeviceMemory _mem,
              const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   TU_RMV(resource_destroy, device, mem);

   _tu_destroy_memory(device, mem);

   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_MapMemory2KHR(VkDevice _device, const VkMemoryMapInfoKHR *pMemoryMapInfo, void **ppData)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_device_memory, mem, pMemoryMapInfo->memory);
   VkResult result;

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   void *placed_addr = NULL;
   if (pMemoryMapInfo->flags & VK_MEMORY_MAP_PLACED_BIT_EXT) {
      const VkMemoryMapPlacedInfoEXT *placed_info =
         vk_find_struct_const(pMemoryMapInfo->pNext, MEMORY_MAP_PLACED_INFO_EXT);
      assert(placed_info != NULL);
      placed_addr = placed_info->pPlacedAddress;
   }

   result = tu_bo_map(device, mem->bo, placed_addr);
   if (result != VK_SUCCESS)
      return result;

   *ppData = (char *) mem->bo->map + pMemoryMapInfo->offset;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_UnmapMemory2KHR(VkDevice _device, const VkMemoryUnmapInfoKHR *pMemoryUnmapInfo)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_device_memory, mem, pMemoryUnmapInfo->memory);

   if (mem == NULL)
      return VK_SUCCESS;

   return tu_bo_unmap(device, mem->bo, pMemoryUnmapInfo->flags & VK_MEMORY_UNMAP_RESERVE_BIT_EXT);
}
static VkResult
sync_cache(VkDevice _device,
           enum tu_mem_sync_op op,
           uint32_t count,
           const VkMappedMemoryRange *ranges)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   if (!device->physical_device->has_cached_non_coherent_memory) {
      tu_finishme(
         "data cache clean and invalidation are unsupported on this arch!");
      return VK_SUCCESS;
   }

   for (uint32_t i = 0; i < count; i++) {
      VK_FROM_HANDLE(tu_device_memory, mem, ranges[i].memory);
      tu_bo_sync_cache(device, mem->bo, ranges[i].offset, ranges[i].size, op);
   }

   return VK_SUCCESS;
}

VkResult
tu_FlushMappedMemoryRanges(VkDevice _device,
                           uint32_t memoryRangeCount,
                           const VkMappedMemoryRange *pMemoryRanges)
{
   return sync_cache(_device, TU_MEM_SYNC_CACHE_TO_GPU, memoryRangeCount,
                     pMemoryRanges);
}

VkResult
tu_InvalidateMappedMemoryRanges(VkDevice _device,
                                uint32_t memoryRangeCount,
                                const VkMappedMemoryRange *pMemoryRanges)
{
   return sync_cache(_device, TU_MEM_SYNC_CACHE_FROM_GPU, memoryRangeCount,
                     pMemoryRanges);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceMemoryCommitment(VkDevice device,
                             VkDeviceMemory _memory,
                             VkDeviceSize *pCommittedMemoryInBytes)
{
   VK_FROM_HANDLE(tu_device_memory, memory, _memory);
   *pCommittedMemoryInBytes = memory->lazy_initialized ? memory->size : 0;
}

static VkResult
tu_get_msrtss_temporary(struct tu_device *dev,
                        struct tu_device_memory **mem_out,
                        uint64_t size, bool depth)
{
   struct tu_device_memory **msrtss_temporary =
      depth ? &dev->msrtss_depth_temporary : &dev->msrtss_color_temporary;

   mtx_lock(&dev->mutex);
   if ((*msrtss_temporary) &&
       (*msrtss_temporary)->size >= size) {
      struct tu_device_memory *mem = tu_memory_get_ref(*msrtss_temporary);
      mtx_unlock(&dev->mutex);
      *mem_out = mem;
      return VK_SUCCESS;
   }

   if (*msrtss_temporary)
      tu_destroy_memory(dev, *msrtss_temporary);

   struct tu_device_memory *mem;
   VkResult result =
      tu_create_memory(dev, &mem,
                       depth ? (VkMemoryPropertyFlags)0 :
                       VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
                       TU_BO_ALLOC_INTERNAL_RESOURCE,
                       size, depth ? "MSRTSS depth" : "MSRTSS color");
   if (result != VK_SUCCESS) {
      mtx_unlock(&dev->mutex);
      return result;
   }

   *msrtss_temporary = tu_memory_get_ref(mem);
   mtx_unlock(&dev->mutex);

   *mem_out = mem;
   return VK_SUCCESS;
}

/* Allocate lazy memory and setup images for transient attachments that are
 * implicitly created by MSRTSS. The lifetime of these are tied to the
 * framebuffer with render passes or the command buffer with dynamic
 * rendering.
 */
VkResult
tu_init_msrtss_attachments(struct tu_device *device,
                           const struct tu_render_pass *pass,
                           const struct tu_framebuffer *fb,
                           const struct VkFramebufferAttachmentImageInfo *attachment_info,
                           const struct tu_image_view **attachments,
                           struct tu_image *images,
                           struct tu_image_view *iviews,
                           struct tu_device_memory **depth_mem_out,
                           struct tu_device_memory **color_mem_out)
{
   uint64_t depth_size = 0, color_size = 0;

   /* First, create images and calculate size requirement. */
   for (unsigned i = 0; i < pass->attachment_count - pass->user_attachment_count; i++) {
      const struct tu_render_pass_attachment *att =
         &pass->attachments[pass->user_attachment_count + i];
      uint32_t user_att_idx = att->user_att;
      VkImageCreateFlags flags =
         attachments ? attachments[user_att_idx]->image->vk.create_flags :
         attachment_info[user_att_idx].flags;
      bool is_ds = vk_format_is_depth_or_stencil(att->format);
      VkImageCreateInfo image_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .flags = flags & VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_EXT,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = att->format,
         .extent = {
            fb->width, fb->height, 1
         },
         .mipLevels = 1,
         .arrayLayers = MAX2(fb->layers, pass->num_views),
         .samples = att->samples,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = is_ds ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
            (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
             VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT),
      };

      /* In case we're using dynamic rendering, reset the image struct from
       * the last use.
       */
      memset(&images[i], 0, sizeof(images[i]));

      /* Note: we don't need a corresponding vk_image_finish() when destroying
       * the framebuffer or finishing the render pass because the object is
       * internal and we never attach private data or a name to it.
       */
      vk_image_init(&device->vk, &images[i].vk, &image_info);

      tu_image_init(device, &images[i], &image_info);
      TU_CALLX(device, tu_image_update_layout)(device, &images[i], DRM_FORMAT_MOD_INVALID, NULL);

      if (is_ds) {
         depth_size = align64(depth_size, images[i].layout[0].base_align);
         depth_size += images[i].total_size;
      } else {
         color_size = align64(color_size, images[i].layout[0].base_align);
         color_size += images[i].total_size;
      }
   }

   /* Allocate memory.
    *
    * TODO: Once we support partially committing memory, we won't need to make
    * separate allocations for depth and color. For now this at least avoids
    * allocating memory for color attachments.
    */
   VkResult result = VK_SUCCESS;

   struct tu_device_memory *depth_mem = NULL, *color_mem = NULL;
   if (depth_size != 0) {
      result =
         tu_get_msrtss_temporary(device, &depth_mem, depth_size, true);
      if (result != VK_SUCCESS)
         return result;
   }

   if (color_size != 0) {
      result =
         tu_get_msrtss_temporary(device, &color_mem, color_size, false);
      if (result != VK_SUCCESS) {
         if (depth_size != 0)
            tu_destroy_memory(device, depth_mem);
         return result;
      }
   }

   *depth_mem_out = depth_mem;
   *color_mem_out = color_mem;

   /* Bind images to memory and create image views. */
   uint64_t depth_offset = 0, color_offset = 0;
   for (unsigned i = 0; i < pass->attachment_count - pass->user_attachment_count; i++) {
      const struct tu_render_pass_attachment *att =
         &pass->attachments[pass->user_attachment_count + i];
      struct tu_image *image = &images[i];
      bool is_ds = vk_format_is_depth_or_stencil(att->format);

      if (is_ds) {
         depth_offset = align64(depth_offset, image->layout[0].base_align);
         image->mem = depth_mem;
         image->mem_offset = depth_offset;
         image->iova = depth_mem->iova + depth_offset;
         depth_offset += image->total_size;
      } else {
         color_offset = align64(color_offset, image->layout[0].base_align);
         image->mem = color_mem;
         image->mem_offset = color_offset;
         image->iova = color_mem->iova + color_offset;
         color_offset += image->total_size;
      }

      VkImageViewCreateInfo iview_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = tu_image_to_handle(&images[i]),
         .viewType =
            fb->layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
         .format = att->format,
         .components = {
            VK_COMPONENT_SWIZZLE_R,
            VK_COMPONENT_SWIZZLE_G,
            VK_COMPONENT_SWIZZLE_B,
            VK_COMPONENT_SWIZZLE_A,
         },
         .subresourceRange = {
            .aspectMask = vk_format_aspects(att->format),
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = fb->layers,
         }
      };

      tu_image_view_init(device, &iviews[i], &iview_info);
   }

   assert(color_offset == color_size);
   assert(depth_offset == depth_size);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateFramebuffer(VkDevice _device,
                     const VkFramebufferCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkFramebuffer *pFramebuffer)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   if (TU_DEBUG(DYNAMIC))
      return vk_common_CreateFramebuffer(_device, pCreateInfo, pAllocator,
                                         pFramebuffer);

   VK_FROM_HANDLE(tu_render_pass, pass, pCreateInfo->renderPass);
   struct tu_framebuffer *framebuffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   bool imageless = pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;

   uint32_t msrtss_attachment_count = pass->attachment_count -
      pass->user_attachment_count;
   struct tu_image_view **attachments;
   struct tu_image *images;
   struct tu_image_view *iviews;
   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &framebuffer, struct tu_framebuffer, 1);
   vk_multialloc_add(&ma, &attachments, struct tu_image_view *,
                     (imageless ? 0 : pCreateInfo->attachmentCount) +
                     msrtss_attachment_count);
   if (msrtss_attachment_count) {
      vk_multialloc_add(&ma, &images, struct tu_image,
                        msrtss_attachment_count);
      vk_multialloc_add(&ma, &iviews, struct tu_image_view,
                        msrtss_attachment_count);
   }
   if (!vk_object_multizalloc(
      &device->vk, &ma, pAllocator, VK_OBJECT_TYPE_FRAMEBUFFER))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   framebuffer->attachment_count = pass->attachment_count;
   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;
   framebuffer->max_tile_w_constraint = ~0;
   framebuffer->max_tile_h_constraint = ~0;

   if (!imageless) {
      for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
         VkImageView _iview = pCreateInfo->pAttachments[i];
         struct tu_image_view *iview = tu_image_view_from_handle(_iview);
         framebuffer->attachments[i] = iview;
      }
   }

   if (pass->has_fdm) {
      if (imageless) {
         const VkFramebufferAttachmentsCreateInfo *fb_att_info =
            vk_find_struct_const(pCreateInfo->pNext,
                                 FRAMEBUFFER_ATTACHMENTS_CREATE_INFO);
         for (uint32_t i = 0; i < fb_att_info->attachmentImageInfoCount;
              i++) {
            const VkFramebufferAttachmentImageInfo *image_info =
               &fb_att_info->pAttachmentImageInfos[i];
            if (image_info->flags &
                   VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_EXT &&
                image_info->usage &
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
               struct fdl_lrz_fdm_extra_size extra_size =
                  TU_CALLX(device, fdl6_lrz_get_max_fdm_extra_size)(
                     device->physical_device->info, image_info->width,
                     image_info->height, pass->attachments[0].samples,
                     image_info->layerCount);
               framebuffer->max_tile_w_constraint = extra_size.extra_width;
               framebuffer->max_tile_h_constraint = extra_size.extra_height;
               break;
            }
         }
      } else {
         for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
            const struct tu_image_view *iview = framebuffer->attachments[i];
            if (iview->image->vk.create_flags &
                   VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_EXT &&
                iview->image->vk.usage &
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
               framebuffer->max_tile_w_constraint =
                  iview->image->max_tile_w_constraint_fdm;
               framebuffer->max_tile_h_constraint =
                  iview->image->max_tile_h_constraint_fdm;
               break;
            }
         }
      }
   }

   tu_framebuffer_tiling_config(framebuffer, device, pass);

   /* For MSRTSS, allocate extra images that are tied to the VkFramebuffer */
   if (msrtss_attachment_count > 0) {
      const VkFramebufferAttachmentsCreateInfo *fb_att_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              FRAMEBUFFER_ATTACHMENTS_CREATE_INFO);
      VkResult result =
         tu_init_msrtss_attachments(device,
                                    pass, framebuffer,
                                    imageless ? fb_att_info->pAttachmentImageInfos : NULL,
                                    imageless ? NULL :
                                    framebuffer->attachments,
                                    images, iviews,
                                    &framebuffer->depth_mem,
                                    &framebuffer->color_mem);
      if (result != VK_SUCCESS) {
         vk_object_free(&device->vk, pAllocator, framebuffer);
         return vk_error(device, result);
      }

      /* With imageless attachments, the only attachments in the framebuffer
       * are MSRTSS attachments. Without imageless attachments, they are after
       * the user's attachments.
       */
      for (uint32_t i = 0; i < msrtss_attachment_count; i++) {
         uint32_t fb_idx = i + (imageless ? 0 : pCreateInfo->attachmentCount);
         framebuffer->attachments[fb_idx] = &iviews[i];
      }
   }

   *pFramebuffer = tu_framebuffer_to_handle(framebuffer);
   return VK_SUCCESS;
}

void
tu_setup_dynamic_framebuffer(struct tu_cmd_buffer *cmd_buffer,
                             const VkRenderingInfo *pRenderingInfo)
{
   struct tu_render_pass *pass = &cmd_buffer->dynamic_pass;
   struct tu_framebuffer *framebuffer = &cmd_buffer->dynamic_framebuffer;

   framebuffer->attachment_count = pass->attachment_count;
   framebuffer->width = pRenderingInfo->renderArea.offset.x +
      pRenderingInfo->renderArea.extent.width;
   framebuffer->height = pRenderingInfo->renderArea.offset.y +
      pRenderingInfo->renderArea.extent.height;
   framebuffer->layers =
      pRenderingInfo->viewMask != 0 ? 1 : pRenderingInfo->layerCount;
   framebuffer->max_tile_w_constraint = ~0;
   framebuffer->max_tile_h_constraint = ~0;

   if (pass->has_fdm && pRenderingInfo->pDepthAttachment &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(tu_image_view, view,
                     pRenderingInfo->pDepthAttachment->imageView);
      framebuffer->max_tile_w_constraint =
         view->image->max_tile_w_constraint_fdm;
      framebuffer->max_tile_h_constraint =
         view->image->max_tile_h_constraint_fdm;
   }

   tu_framebuffer_tiling_config(framebuffer, cmd_buffer->device, pass);
}

VkResult
tu_setup_dynamic_msrtss(struct tu_cmd_buffer *cmd_buffer)
{
   struct tu_render_pass *pass = &cmd_buffer->dynamic_pass;
   struct tu_framebuffer *framebuffer = &cmd_buffer->dynamic_framebuffer;

   if (pass->attachment_count > pass->user_attachment_count) {
      struct tu_device_memory *depth_mem = NULL, *color_mem = NULL;

      VkResult result =
         tu_init_msrtss_attachments(cmd_buffer->device,
                                    pass, framebuffer, NULL,
                                    cmd_buffer->dynamic_attachments,
                                    cmd_buffer->dynamic_msrtss_images,
                                    cmd_buffer->dynamic_msrtss_iviews,
                                    &depth_mem, &color_mem);

      if (result != VK_SUCCESS) {
         return vk_error(cmd_buffer, result);
      }

      for (unsigned i = 0; i < pass->attachment_count -
           pass->user_attachment_count; i++) {
         cmd_buffer->dynamic_attachments[i + pass->user_attachment_count] =
            &cmd_buffer->dynamic_msrtss_iviews[i];
      }

      if (color_mem) {
         util_dynarray_append(&cmd_buffer->msrtss_color_temporaries,
                              color_mem);
      }
      if (depth_mem) {
         util_dynarray_append(&cmd_buffer->msrtss_depth_temporaries,
                              depth_mem);
      }
   }

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyFramebuffer(VkDevice _device,
                      VkFramebuffer _fb,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   if (TU_DEBUG(DYNAMIC)) {
      vk_common_DestroyFramebuffer(_device, _fb, pAllocator);
      return;
   }

   VK_FROM_HANDLE(tu_framebuffer, fb, _fb);

   if (!fb)
      return;

   if (fb->depth_mem)
      tu_destroy_memory(device, fb->depth_mem);
   if (fb->color_mem)
      tu_destroy_memory(device, fb->color_mem);

   vk_object_free(&device->vk, pAllocator, fb);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetMemoryFdKHR(VkDevice _device,
                  const VkMemoryGetFdInfoKHR *pGetFdInfo,
                  int *pFd)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_device_memory, memory, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   /* At the moment, we support only the below handle types. */
   assert(pGetFdInfo->handleType ==
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
          pGetFdInfo->handleType ==
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   int prime_fd = tu_bo_export_dmabuf(device, memory->bo);
   if (prime_fd < 0)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   *pFd = prime_fd;

   if (memory->image) {
      struct fdl_layout *l = &memory->image->layout[0];
      uint64_t modifier;
      if (l->ubwc) {
         modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      } else if (l->tile_mode == 2) {
         modifier = DRM_FORMAT_MOD_QCOM_TILED2;
      } else if (l->tile_mode == 3) {
         modifier = DRM_FORMAT_MOD_QCOM_TILED3;
      } else {
         assert(!l->tile_mode);
         modifier = DRM_FORMAT_MOD_LINEAR;
      }
      struct fdl_metadata metadata = {
         .modifier = modifier,
      };
      tu_bo_set_metadata(device, memory->bo, &metadata, sizeof(metadata));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetMemoryFdPropertiesKHR(VkDevice _device,
                            VkExternalMemoryHandleTypeFlagBits handleType,
                            int fd,
                            VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   assert(handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
   pMemoryFdProperties->memoryTypeBits =
      (1 << device->physical_device->memory.non_lazy_type_count) - 1;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceMultisamplePropertiesEXT(
   VkPhysicalDevice                            physicalDevice,
   VkSampleCountFlagBits                       samples,
   VkMultisamplePropertiesEXT*                 pMultisampleProperties)
{
   VK_FROM_HANDLE(tu_physical_device, pdevice, physicalDevice);

   if (pdevice->vk.supported_extensions.EXT_sample_locations && (samples & sample_location_counts))
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){ 1, 1 };
   else
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){ 0, 0 };
}

uint64_t tu_GetDeviceMemoryOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
   VK_FROM_HANDLE(tu_device_memory, mem, pInfo->memory);
   return mem->bo->iova;
}

struct tu_debug_bos_entry {
   uint32_t count;
   uint64_t size;
   const char *name;
};

const char *
tu_debug_bos_add(struct tu_device *dev, uint64_t size, const char *name)
{
   assert(name);

   if (likely(!dev->bo_sizes))
      return NULL;

   mtx_lock(&dev->bo_mutex);
   struct hash_entry *entry = _mesa_hash_table_search(dev->bo_sizes, name);
   struct tu_debug_bos_entry *debug_bos;

   if (!entry) {
      debug_bos = (struct tu_debug_bos_entry *) calloc(
         1, sizeof(struct tu_debug_bos_entry));
      debug_bos->name = strdup(name);
      _mesa_hash_table_insert(dev->bo_sizes, debug_bos->name, debug_bos);
   } else {
      debug_bos = (struct tu_debug_bos_entry *) entry->data;
   }

   debug_bos->count++;
   debug_bos->size += align(size, 4096);
   mtx_unlock(&dev->bo_mutex);

   return debug_bos->name;
}

void
tu_debug_bos_del(struct tu_device *dev, struct tu_bo *bo)
{
   if (likely(!dev->bo_sizes) || !bo->name)
      return;

   mtx_lock(&dev->bo_mutex);
   struct hash_entry *entry =
      _mesa_hash_table_search(dev->bo_sizes, bo->name);
   /* If we're finishing the BO, it should have been added already */
   assert(entry);

   struct tu_debug_bos_entry *debug_bos =
      (struct tu_debug_bos_entry *) entry->data;
   debug_bos->count--;
   debug_bos->size -= align(bo->size, 4096);
   if (!debug_bos->count) {
      _mesa_hash_table_remove(dev->bo_sizes, entry);
      free((void *) debug_bos->name);
      free(debug_bos);
   }
   mtx_unlock(&dev->bo_mutex);
}

static int debug_bos_count_compare(const void *in_a, const void *in_b)
{
   struct tu_debug_bos_entry *a = *(struct tu_debug_bos_entry **)in_a;
   struct tu_debug_bos_entry *b = *(struct tu_debug_bos_entry **)in_b;
   return a->count - b->count;
}

void
tu_debug_bos_print_stats(struct tu_device *dev)
{
   if (likely(!dev->bo_sizes))
      return;

   mtx_lock(&dev->bo_mutex);

   /* Put the HT's sizes data in an array so we can sort by number of allocations. */
   struct util_dynarray dyn = UTIL_DYNARRAY_INIT;

   uint32_t size = 0;
   uint32_t count = 0;
   hash_table_foreach(dev->bo_sizes, entry)
   {
      struct tu_debug_bos_entry *debug_bos =
         (struct tu_debug_bos_entry *) entry->data;
      util_dynarray_append(&dyn, debug_bos);
      size += debug_bos->size / 1024;
      count += debug_bos->count;
   }

   qsort(dyn.data,
         util_dynarray_num_elements(&dyn, struct tu_debug_bos_entry *),
         sizeof(struct tu_debug_bos_entryos_entry *), debug_bos_count_compare);

   util_dynarray_foreach(&dyn, struct tu_debug_bos_entry *, entryp)
   {
      struct tu_debug_bos_entry *debug_bos = *entryp;
      mesa_logi("%30s: %4d bos, %lld kb\n", debug_bos->name, debug_bos->count,
                (long long) (debug_bos->size / 1024));
   }

   mesa_logi("submitted %d bos (%d MB)\n", count, DIV_ROUND_UP(size, 1024));

   util_dynarray_fini(&dyn);

   mtx_unlock(&dev->bo_mutex);
}

void
tu_dump_bo_init(struct tu_device *dev, struct tu_bo *bo)
{
   bo->dump_bo_list_idx = ~0;

   if (!FD_RD_DUMP(ENABLE))
      return;

   mtx_lock(&dev->bo_mutex);
   uint32_t idx =
      util_dynarray_num_elements(&dev->dump_bo_list, struct tu_bo *);
   bo->dump_bo_list_idx = idx;
   util_dynarray_append(&dev->dump_bo_list, bo);
   mtx_unlock(&dev->bo_mutex);
}

void
tu_dump_bo_del(struct tu_device *dev, struct tu_bo *bo)
{
   if (bo->dump_bo_list_idx != ~0) {
      mtx_lock(&dev->bo_mutex);
      struct tu_bo *exchanging_bo =
         util_dynarray_pop(&dev->dump_bo_list, struct tu_bo *);
      *util_dynarray_element(&dev->dump_bo_list, struct tu_bo *,
                             bo->dump_bo_list_idx) = exchanging_bo;
      exchanging_bo->dump_bo_list_idx = bo->dump_bo_list_idx;
      mtx_unlock(&dev->bo_mutex);
   }
}

void
tu_CmdBeginDebugUtilsLabelEXT(VkCommandBuffer _commandBuffer,
                              const VkDebugUtilsLabelEXT *pLabelInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, _commandBuffer);

   vk_common_CmdBeginDebugUtilsLabelEXT(_commandBuffer, pLabelInfo);

   /* Note that the spec says:
    *
    * "An application may open a debug label region in one command buffer and
    *  close it in another, or otherwise split debug label regions across
    *  multiple command buffers or multiple queue submissions. When viewed
    * from the linear series of submissions to a single queue, the calls to
    *  vkCmdBeginDebugUtilsLabelEXT and vkCmdEndDebugUtilsLabelEXT must be
    *  matched and balanced."
    *
    * But if you're beginning labeling during a renderpass and ending outside
    * it, or vice versa, these trace ranges in perfetto will be unbalanced.  I
    * expect that u_trace and perfetto will do something like take just one of
    * the begins/ends, or drop the event entirely, but not crash.  Similarly,
    * I think we'll have problems if the tracepoints are split across cmd
    * buffers. Still, getting the simple case of cmd buffer annotation into
    * perfetto should prove useful.
    */
   const char *label = pLabelInfo->pLabelName;
   if (cmd_buffer->state.pass) {
      trace_start_cmd_buffer_annotation_rp(
         &cmd_buffer->trace, &cmd_buffer->draw_cs, cmd_buffer, strlen(label), label);
   } else {
      trace_start_cmd_buffer_annotation(&cmd_buffer->trace, &cmd_buffer->cs,
                                        cmd_buffer, strlen(label), label);
   }
}

void
tu_CmdEndDebugUtilsLabelEXT(VkCommandBuffer _commandBuffer)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, _commandBuffer);

   if (cmd_buffer->state.pass) {
      trace_end_cmd_buffer_annotation_rp(&cmd_buffer->trace,
                                          &cmd_buffer->draw_cs);
   } else {
      trace_end_cmd_buffer_annotation(&cmd_buffer->trace, &cmd_buffer->cs);
   }

   vk_common_CmdEndDebugUtilsLabelEXT(_commandBuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_SetDebugUtilsObjectNameEXT(
   VkDevice device,
   const VkDebugUtilsObjectNameInfoEXT *pNameInfo)
{
   VkResult result = vk_common_SetDebugUtilsObjectNameEXT(device, pNameInfo);

#ifdef HAVE_PERFETTO
   if (result == VK_SUCCESS)
      tu_perfetto_set_debug_utils_object_name(pNameInfo);
#endif

   return result;
}
