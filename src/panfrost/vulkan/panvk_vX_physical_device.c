/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_device.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <sys/sysmacros.h>

#include "git_sha1.h"

#include "vk_android.h"
#include "vk_device.h"
#include "vk_limits.h"
#include "vk_shader_module.h"

#include "panvk_instance.h"
#include "panvk_buffer.h"
#include "panvk_cmd_draw.h"
#include "panvk_descriptor_set_layout.h"
#include "panvk_physical_device.h"
#include "panvk_wsi.h"

#include "pan_format.h"
#include "pan_props.h"

/* We reserve one ubo for push constant, one for sysvals and one per-set for the
 * descriptor metadata  */
#define RESERVED_UBO_COUNT                   6
#define MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS (32 - RESERVED_UBO_COUNT)

void
panvk_per_arch(get_physical_device_extensions)(
   const struct panvk_physical_device *device,
   struct vk_device_extension_table *ext)
{
   bool has_vk1_1 = PAN_ARCH >= 10;
   bool has_vk1_2 = PAN_ARCH >= 10;
   bool has_gralloc = vk_android_get_ugralloc() != NULL;

   *ext = (struct vk_device_extension_table){
      .KHR_8bit_storage = true,
      .KHR_16bit_storage = true,
      .KHR_shader_atomic_int64 = PAN_ARCH >= 9,
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_calibrated_timestamps = device->kmod.props.gpu_can_query_timestamp,
      .KHR_copy_commands2 = true,
      .KHR_create_renderpass2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_descriptor_update_template = true,
      .KHR_depth_clamp_zero_one = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_device_group = true,
      .KHR_draw_indirect_count = PAN_ARCH >= 10,
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
      .KHR_get_memory_requirements2 = true,
      .KHR_global_priority = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
      .KHR_index_type_uint8 = true,
      .KHR_line_rasterization = true,
      .KHR_load_store_op_none = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_maintenance4 = has_vk1_1,
      .KHR_maintenance5 = has_vk1_1,
      .KHR_maintenance6 = has_vk1_1,
      .KHR_maintenance7 = has_vk1_1,
      .KHR_maintenance8 = has_vk1_1,
      .KHR_maintenance9 = true,
      .KHR_map_memory2 = true,
      .KHR_multiview = true,
      .KHR_pipeline_binary = true,
      .KHR_pipeline_executable_properties = true,
      .KHR_pipeline_library = true,
      .KHR_push_descriptor = true,
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_clock = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_expect_assume = true,
      .KHR_shader_float_controls = true,
      .KHR_shader_float_controls2 = has_vk1_1,
      .KHR_shader_float16_int8 = true,
      .KHR_shader_integer_dot_product = true,
      .KHR_shader_maximal_reconvergence = has_vk1_1,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_quad_control = has_vk1_2,
      .KHR_shader_relaxed_extended_instruction = true,
      .KHR_shader_subgroup_extended_types = has_vk1_1,
      .KHR_shader_subgroup_rotate = true,
      .KHR_shader_subgroup_uniform_control_flow = has_vk1_1,
      .KHR_shader_terminate_invocation = true,
      .KHR_spirv_1_4 = PAN_ARCH >= 10,
      .KHR_storage_buffer_storage_class = true,
#ifdef PANVK_USE_WSI_PLATFORM
      .KHR_present_id2 = true,
      .KHR_present_wait2 = true,
      .KHR_swapchain = true,
#endif
      .KHR_synchronization2 = true,
      .KHR_timeline_semaphore = true,
      .KHR_unified_image_layouts = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .KHR_vertex_attribute_divisor = true,
      .KHR_vulkan_memory_model = true,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats = true,
      .EXT_border_color_swizzle = true,
      .EXT_buffer_device_address = true,
      .EXT_calibrated_timestamps = device->kmod.props.gpu_can_query_timestamp,
      .EXT_custom_border_color = true,
      .EXT_depth_bias_control = true,
      .EXT_depth_clamp_zero_one = true,
      .EXT_depth_clip_enable = true,
      .EXT_depth_clip_control = true,
      .EXT_device_memory_report = true,
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
      .EXT_display_control = true,
#endif
      .EXT_descriptor_indexing = PAN_ARCH >= 9,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_external_memory_acquire_unmodified = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_global_priority = true,
      .EXT_global_priority_query = true,
      .EXT_graphics_pipeline_library = true,
      .EXT_hdr_metadata = true,
      .EXT_host_image_copy = true,
      .EXT_host_query_reset = true,
      .EXT_image_2d_view_of_3d = true,
      /* EXT_image_drm_format_modifier depends on KHR_sampler_ycbcr_conversion */
      .EXT_image_drm_format_modifier = true,
      .EXT_image_robustness = true,
      .EXT_index_type_uint8 = true,
      .EXT_line_rasterization = true,
      .EXT_load_store_op_none = true,
      .EXT_non_seamless_cube_map = true,
      .EXT_mutable_descriptor_type = PAN_ARCH >= 9,
      .EXT_physical_device_drm = true,
      .EXT_pipeline_creation_cache_control = true,
      .EXT_pipeline_creation_feedback = true,
      .EXT_pipeline_robustness = true,
      .EXT_private_data = true,
      .EXT_primitive_topology_list_restart = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_robustness2 = PAN_ARCH >= 10,
      .EXT_sampler_filter_minmax = PAN_ARCH >= 10,
      .EXT_scalar_block_layout = true,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_module_identifier = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_replicated_composites = true,
      .EXT_shader_subgroup_ballot = true,
      .EXT_shader_subgroup_vote = true,
      .EXT_subgroup_size_control = has_vk1_1,
      .EXT_texel_buffer_alignment = true,
      .EXT_texture_compression_astc_hdr = true,
      .EXT_tooling_info = true,
      .EXT_vertex_attribute_divisor = true,
      .EXT_vertex_input_dynamic_state = true,
      .EXT_ycbcr_2plane_444_formats = PAN_ARCH >= 10,
      .EXT_ycbcr_image_arrays = PAN_ARCH >= 10,
      .EXT_inline_uniform_block = true,
      .ANDROID_external_memory_android_hardware_buffer = has_gralloc,
      .ANDROID_native_buffer = has_gralloc,
      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,

      .ARM_shader_core_builtins = true,
      .ARM_shader_core_properties = has_vk1_1,
   };
}

static bool
has_compressed_formats(const struct panvk_physical_device *physical_device,
                       const uint32_t required_formats)
{
   uint32_t supported_compr_fmts =
      pan_query_compressed_formats(&physical_device->kmod.props);

   return (supported_compr_fmts & required_formats) == required_formats;
}

static bool
has_texture_compression_etc2(const struct panvk_physical_device *physical_device)
{
   return has_compressed_formats(physical_device,
      BITFIELD_BIT(MALI_ETC2_RGB8) |
      BITFIELD_BIT(MALI_ETC2_RGB8A1) | BITFIELD_BIT(MALI_ETC2_RGBA8) |
      BITFIELD_BIT(MALI_ETC2_R11_UNORM) | BITFIELD_BIT(MALI_ETC2_R11_SNORM) |
      BITFIELD_BIT(MALI_ETC2_RG11_UNORM) | BITFIELD_BIT(MALI_ETC2_RG11_SNORM));
}

static bool
has_texture_compression_astc_ldr(const struct panvk_physical_device *physical_device)
{
   return has_compressed_formats(physical_device, BITFIELD_BIT(MALI_ASTC_2D_LDR));
}

static bool
has_texture_compression_astc_hdr(const struct panvk_physical_device *physical_device)
{
   return has_compressed_formats(physical_device, BITFIELD_BIT(MALI_ASTC_2D_HDR));
}

static bool
has_texture_compression_bc(const struct panvk_physical_device *physical_device)
{
   return has_compressed_formats(physical_device,
      BITFIELD_BIT(MALI_BC1_UNORM) | BITFIELD_BIT(MALI_BC2_UNORM) |
      BITFIELD_BIT(MALI_BC3_UNORM) | BITFIELD_BIT(MALI_BC4_UNORM) |
      BITFIELD_BIT(MALI_BC4_SNORM) | BITFIELD_BIT(MALI_BC5_UNORM) |
      BITFIELD_BIT(MALI_BC5_SNORM) | BITFIELD_BIT(MALI_BC6H_SF16) |
      BITFIELD_BIT(MALI_BC6H_UF16) | BITFIELD_BIT(MALI_BC7_UNORM));
}

void
panvk_per_arch(get_physical_device_features)(
   const struct panvk_instance *instance,
   const struct panvk_physical_device *device, struct vk_features *features)
{
   bool has_sparse = PAN_ARCH >= 10;

   *features = (struct vk_features){
      /* Vulkan 1.0 */
      .robustBufferAccess = true,
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = false,
      .tessellationShader = false,
      .sampleRateShading = true,
      .dualSrcBlend = true,
      .logicOp = true,
      .multiDrawIndirect = PAN_ARCH >= 10,
      .drawIndirectFirstInstance = true,
      .depthClamp = true,
      .depthBiasClamp = true,
      .fillModeNonSolid = false,
      .depthBounds = false,
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = false,
      .multiViewport = false,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = has_texture_compression_etc2(device),
      .textureCompressionASTC_LDR = has_texture_compression_astc_ldr(device),
      .textureCompressionBC = has_texture_compression_bc(device),
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = false,
      /* On v13+, the hardware isn't speculatively referencing to invalid
         indices anymore. */
      .vertexPipelineStoresAndAtomics =
         (PAN_ARCH >= 13 && instance->enable_vertex_pipeline_stores_atomics) ||
         instance->force_enable_shader_atomics,
      .fragmentStoresAndAtomics = (PAN_ARCH >= 10) ||
          instance->force_enable_shader_atomics,
      .shaderTessellationAndGeometryPointSize = false,
      .shaderImageGatherExtended = true,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageMultisample = false,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderClipDistance = false,
      .shaderCullDistance = false,
      .shaderFloat64 = false,
      .shaderInt64 = true,
      .shaderInt16 = true,
      .shaderResourceResidency = false,
      .shaderResourceMinLod = false,
      .sparseBinding = has_sparse,
      .sparseResidencyBuffer = has_sparse,
      .sparseResidencyImage2D = has_sparse,
      .sparseResidencyImage3D = false, /* https://gitlab.freedesktop.org/panfrost/mesa/-/issues/242 */
      .sparseResidency2Samples = false,
      .sparseResidency4Samples = false,
      .sparseResidency8Samples = false,
      .sparseResidency16Samples = false,
      .sparseResidencyAliased = false, /* https://gitlab.freedesktop.org/panfrost/mesa/-/issues/237 */
      .variableMultisampleRate = false,
      .inheritedQueries = false,

      /* Vulkan 1.1 */
      .storageBuffer16BitAccess = true,
      .uniformAndStorageBuffer16BitAccess = true,
      .storagePushConstant16 = true,
      .storageInputOutput16 = true,
      .multiview = true,
      .multiviewGeometryShader = false,
      .multiviewTessellationShader = false,
      .variablePointersStorageBuffer = true,
      .variablePointers = true,
      .protectedMemory = false,
      .samplerYcbcrConversion = true,
      .shaderDrawParameters = true,

      /* Vulkan 1.2 */
      .samplerMirrorClampToEdge = true,
      .drawIndirectCount = PAN_ARCH >= 10,
      .storageBuffer8BitAccess = true,
      .uniformAndStorageBuffer8BitAccess = true,
      .storagePushConstant8 = true,
      .shaderBufferInt64Atomics = PAN_ARCH >= 9,
      .shaderSharedInt64Atomics = PAN_ARCH >= 9,
      .shaderFloat16 = PAN_ARCH >= 10,
      .shaderInt8 = true,
      /* In theory, update-after-bind is supported on bifrost, but the
       * descriptor limits would be too low for the descriptorIndexing feature.
       */
      .descriptorIndexing = PAN_ARCH >= 9,
      .shaderInputAttachmentArrayDynamicIndexing = true,
      .shaderUniformTexelBufferArrayDynamicIndexing = true,
      .shaderStorageTexelBufferArrayDynamicIndexing = true,
      .shaderUniformBufferArrayNonUniformIndexing = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .shaderStorageBufferArrayNonUniformIndexing = true,
      .shaderStorageImageArrayNonUniformIndexing = true,
      .shaderInputAttachmentArrayNonUniformIndexing = true,
      .shaderUniformTexelBufferArrayNonUniformIndexing = true,
      .shaderStorageTexelBufferArrayNonUniformIndexing = true,
      .descriptorBindingUniformBufferUpdateAfterBind = PAN_ARCH >= 9,
      .descriptorBindingSampledImageUpdateAfterBind = PAN_ARCH >= 9,
      .descriptorBindingStorageImageUpdateAfterBind = PAN_ARCH >= 9,
      .descriptorBindingStorageBufferUpdateAfterBind = PAN_ARCH >= 9,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = PAN_ARCH >= 9,
      .descriptorBindingStorageTexelBufferUpdateAfterBind = PAN_ARCH >= 9,
      .descriptorBindingUpdateUnusedWhilePending = PAN_ARCH >= 9,
      .descriptorBindingPartiallyBound = PAN_ARCH >= 9,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      .samplerFilterMinmax = PAN_ARCH >= 10,
      .scalarBlockLayout = true,
      .imagelessFramebuffer = true,
      .uniformBufferStandardLayout = true,
      .shaderSubgroupExtendedTypes = true,
      .separateDepthStencilLayouts = true,
      .hostQueryReset = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
      .bufferDeviceAddressCaptureReplay = false,
      .bufferDeviceAddressMultiDevice = false,
      .vulkanMemoryModel = true,
      .vulkanMemoryModelDeviceScope = true,
      .vulkanMemoryModelAvailabilityVisibilityChains = true,
      .shaderOutputViewportIndex = false,
      .shaderOutputLayer = false,
      .subgroupBroadcastDynamicId = true,

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
      .textureCompressionASTC_HDR = has_texture_compression_astc_hdr(device),
      .shaderZeroInitializeWorkgroupMemory = true,
      .dynamicRendering = true,
      .shaderIntegerDotProduct = true,
      .maintenance4 = true,

      /* Vulkan 1.4 */
      .globalPriorityQuery = true,
      .shaderSubgroupRotate = true,
      .shaderSubgroupRotateClustered = true,
      .shaderFloatControls2 = true,
      .shaderExpectAssume = true,
      .rectangularLines = true,
      .bresenhamLines = true,
      .smoothLines = false,
      .stippledRectangularLines = false,
      .stippledBresenhamLines = false,
      .stippledSmoothLines = false,
      .vertexAttributeInstanceRateDivisor = true,
      .vertexAttributeInstanceRateZeroDivisor = true,
      .indexTypeUint8 = true,
      .dynamicRenderingLocalRead = true,
      .maintenance5 = true,
      .maintenance6 = true,
      .pipelineProtectedAccess = false,
      .pipelineRobustness = true,
      .hostImageCopy = true,
      .pushDescriptor = true,

      /* VK_KHR_depth_clamp_zero_one */
      .depthClampZeroOne = true,

      /* VK_KHR_maintenance7 */
      .maintenance7 = true,

      /* VK_KHR_maintenance8 */
      .maintenance8 = true,

      /* VK_KHR_maintenance9 */
      .maintenance9 = true,

      /* VK_EXT_graphics_pipeline_library */
      .graphicsPipelineLibrary = true,

      /* VK_EXT_vertex_input_dynamic_state */
      .vertexInputDynamicState = true,

      /* VK_EXT_depth_bias_control */
      .depthBiasControl = true,
      .leastRepresentableValueForceUnormRepresentation = false,
      .floatRepresentation = false,
      .depthBiasExact = true,

      /* VK_EXT_depth_clip_control */
      .depthClipControl = true,

      /* VK_EXT_depth_clip_enable */
      .depthClipEnable = true,

      /* VK_EXT_extended_dynamic_state */
      .extendedDynamicState = true,

      /* VK_EXT_extended_dynamic_state2 */
      .extendedDynamicState2 = true,
      .extendedDynamicState2LogicOp = true,
      .extendedDynamicState2PatchControlPoints = false,

      /* VK_EXT_4444_formats */
      .formatA4R4G4B4 = true,
      .formatA4B4G4R4 = true,

      /* VK_EXT_custom_border_color */
      .customBorderColors = true,

      /* VK_EXT_border_color_swizzle */
      .borderColorSwizzle = true,
      .borderColorSwizzleFromImage = true,

      /* VK_EXT_image_2d_view_of_3d */
      .image2DViewOf3D = true,
      .sampler2DViewOf3D = true,

      /* VK_EXT_primitive_topology_list_restart */
      .primitiveTopologyListRestart = true,
      .primitiveTopologyPatchListRestart = false,

      /* VK_EXT_provoking_vertex */
      .provokingVertexLast = true,
      .transformFeedbackPreservesProvokingVertex = false,

      /* v7 doesn't support AFBC(BGR). We need to tweak the texture swizzle to
       * make it work, which forces us to apply the same swizzle on the border
       * color, meaning we need to know the format when preparing the border
       * color.
       */
      .customBorderColorWithoutFormat = PAN_ARCH != 7,

      /* VK_KHR_pipeline_binary */
      .pipelineBinaries = true,

      /* VK_KHR_pipeline_executable_properties */
      .pipelineExecutableInfo = true,

      /* VK_EXT_robustness2 */
      .robustBufferAccess2 = PAN_ARCH >= 11,
      .robustImageAccess2 = false,
      .nullDescriptor = PAN_ARCH >= 10,

      /* VK_KHR_shader_clock */
      .shaderSubgroupClock = device->kmod.props.gpu_can_query_timestamp,
      .shaderDeviceClock = device->kmod.props.timestamp_device_coherent,

      /* VK_KHR_shader_quad_control */
      .shaderQuadControl = true,

      /* VK_KHR_shader_relaxed_extended_instruction */
      .shaderRelaxedExtendedInstruction = true,

      /* VK_KHR_shader_maximal_reconvergence */
      .shaderMaximalReconvergence = true,

      /* VK_KHR_shader_subgroup_uniform_control_flow */
      .shaderSubgroupUniformControlFlow = true,

      /* VK_EXT_shader_module_identifier */
      .shaderModuleIdentifier = true,

      /* VK_EXT_shader_replicated_composites */
      .shaderReplicatedComposites = true,

      /* VK_EXT_texel_buffer_alignment */
      .texelBufferAlignment = true,

      /* VK_EXT_ycbcr_2plane_444_formats */
      .ycbcr2plane444Formats = PAN_ARCH >= 10,

      /* VK_EXT_ycbcr_image_arrays */
      .ycbcrImageArrays = PAN_ARCH >= 10,

      /* VK_EXT_non_seamless_cube_map */
      .nonSeamlessCubeMap = true,

      /* VK_KHR_unified_image_layouts */
      .unifiedImageLayouts = true,
      /* Video is not currently supported, so set to false */
      .unifiedImageLayoutsVideo = false,

      /* VK_EXT_mutable_descriptor_type */
      .mutableDescriptorType = PAN_ARCH >= 9,

#ifdef PANVK_USE_WSI_PLATFORM
      /* VK_KHR_present_id2 */
      .presentId2 = true,

      /* VK_KHR_present_wait2 */
      .presentWait2 = true,
#endif

      /* VK_EXT_device_memory_report */
      .deviceMemoryReport = true,

      /* VK_ARM_shader_core_builtins */
      .shaderCoreBuiltins = PAN_ARCH >= 9,
   };
}

static uint32_t
get_api_version()
{
   const uint32_t version_override = vk_get_version_override();
   if (version_override)
      return version_override;

   if (PAN_ARCH >= 10)
      return VK_MAKE_API_VERSION(0, 1, 4, VK_HEADER_VERSION);

   return VK_MAKE_API_VERSION(0, 1, 0, VK_HEADER_VERSION);
}

static VkConformanceVersion
get_conformance_version()
{
   if (PAN_ARCH == 10)
      return (VkConformanceVersion){1, 4, 1, 2};

   return (VkConformanceVersion){0, 0, 0, 0};
}

void
panvk_per_arch(get_physical_device_properties)(
   const struct panvk_instance *instance,
   const struct panvk_physical_device *device, struct vk_properties *properties)
{
   unsigned max_tib_size = pan_query_tib_size(device->model);
   const unsigned max_cbuf_format = 16; /* R32G32B32A32 */

   unsigned max_cbuf_atts = pan_get_max_cbufs(PAN_ARCH, max_tib_size);
   VkSampleCountFlags sample_counts =
       panvk_get_sample_counts(PAN_ARCH, max_tib_size, max_cbuf_atts,
                               max_cbuf_format);

   uint64_t os_page_size = 4096;
   os_get_page_size(&os_page_size);

   const bool has_disk_cache = device->vk.disk_cache != NULL;

   /* Ensure that the max threads count per workgroup is valid for Bifrost */
   assert(PAN_ARCH > 8 || device->kmod.props.max_threads_per_wg <= 1024);

   float pointSizeRangeMin;
   float pointSizeRangeMax;

   /* On v13+, point size handling changed entirely */
   if (PAN_ARCH >= 13) {
      pointSizeRangeMin = 1.0;
      pointSizeRangeMax = 1024.0;
   } else {
      pointSizeRangeMin = 0.125;
      pointSizeRangeMax = 4095.9375;
   }

   *properties = (struct vk_properties){
      .apiVersion = get_api_version(),
      .driverVersion = vk_get_driver_version(),
      .vendorID = instance->force_vk_vendor ? instance->force_vk_vendor :
                                              ARM_VENDOR_ID,

      /* Collect arch_major, arch_minor, arch_rev and product_major,
       * as done by the Arm driver.
       */
      .deviceID =
         device->kmod.props.gpu_id & (ARCH_MAJOR | ARCH_MINOR | ARCH_REV | PRODUCT_MAJOR),
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,

      /* Vulkan 1.0 limits */
      /* Maximum texture dimension is 2^16, but we're limited by the
       * size/surface-stride fields. The size/surface_stride field is 32-bit
       * on v10-, so let's take that as a reference for now.
       * The following limits are chosen so we don't overflow these
       * size/surface_stride fields. We choose them so they are a power-of-two,
       * except for 2D/Cube dimensions where taking a power-of-two would be
       * too limiting, so we pick power-of-two-minus-one, which makes things
       * fit exactly in our 32-bit budget.
       *
       * TODO: increase the limit on v11+ once we have all the necessary bits
       * patched to handle the size/stride field extension.
       */
      .maxImageDimension1D = (1 << 16),
      .maxImageDimension2D = PAN_ARCH <= 10 ? (1 << 14) - 1 : (1 << 16),
      .maxImageDimension3D = PAN_ARCH <= 10 ? (1 << 9) : (1 << 14),
      .maxImageDimensionCube = PAN_ARCH <= 10 ? (1 << 14) - 1 : (1 << 16),
      .maxImageArrayLayers = (1 << 16),
      /* Currently Bifrost is limited by the 1D texture size, which is 2^16,
         while pre-v11 is limited to 2^27 elements of 16 byte formats due to
         size fields of 32 bits. */
      .maxTexelBufferElements = PAN_ARCH >= 11  ? PANVK_MAX_BUFFER_SIZE
                                : PAN_ARCH >= 9 ? (1 << 27)
                                                : (1 << 16),
      /* Each uniform entry is 16-byte and the number of entries is encoded in a
       * 12-bit field, with the minus(1) modifier, which gives 2^20.
       */
      .maxUniformBufferRange = 1 << 20,
      /* Storage buffer access is lowered to globals, so there's no limit here,
       * except for the SW-descriptor we use to encode storage buffer
       * descriptors, where the size is a 32-bit field.
       */
      .maxStorageBufferRange = UINT32_MAX,
      /* Vulkan 1.4 minimum. We currently implement push constants in terms of
       * FAUs so we're limited by how many user-defined FAUs the hardware
       * offers, minus driver-internal needs. If we ever need go to higher,
       * we'll have to implement push constants in terms of both FAUs and global
       * loads.
       */
      .maxPushConstantsSize = 256,
      /* On our kernel drivers we're limited by the available memory rather
       * than available allocations. This is better expressed through memory
       * properties and budget queries, and by returning
       * VK_ERROR_OUT_OF_DEVICE_MEMORY when applicable, rather than
       * this limit.
       */
      .maxMemoryAllocationCount = UINT32_MAX,
      /* On Mali, VkSampler objects do not use any resources other than host
       * memory and host address space, availability of which can change
       * significantly over time.
       */
      .maxSamplerAllocationCount = UINT32_MAX,
      /* A cache line. */
      .bufferImageGranularity = 64,
      /* The entire user-allocatable VA range. */
      .sparseAddressSpaceSize =
         pan_kmod_dev_query_user_va_range(device->kmod.dev).size,
      .maxBoundDescriptorSets = MAX_SETS,
      .maxPerStageDescriptorSamplers = MAX_PER_STAGE_SAMPLERS,
      .maxPerStageDescriptorUniformBuffers = MAX_PER_STAGE_UNIFORM_BUFFERS,
      .maxPerStageDescriptorStorageBuffers = MAX_PER_STAGE_STORAGE_BUFFERS,
      .maxPerStageDescriptorSampledImages = MAX_PER_STAGE_SAMPLED_IMAGES,
      .maxPerStageDescriptorStorageImages = MAX_PER_STAGE_STORAGE_IMAGES,
      .maxPerStageDescriptorInputAttachments = MAX_PER_STAGE_INPUT_ATTACHMENTS,
      .maxPerStageResources = MAX_PER_STAGE_RESOURCES,
      .maxDescriptorSetSamplers = MAX_PER_SET_SAMPLERS,
      .maxDescriptorSetUniformBuffers = MAX_PER_SET_UNIFORM_BUFFERS,
      /* Software limit to keep VkCommandBuffer tracking sane. */
      .maxDescriptorSetUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS,
      .maxDescriptorSetStorageBuffers = MAX_PER_SET_STORAGE_BUFFERS,
      /* Software limit to keep VkCommandBuffer tracking sane. */
      .maxDescriptorSetStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS,
      .maxDescriptorSetSampledImages = MAX_PER_SET_SAMPLED_IMAGES,
      .maxDescriptorSetStorageImages = MAX_PER_SET_STORAGE_IMAGES,
      .maxDescriptorSetInputAttachments = MAX_PER_SET_INPUT_ATTACHMENTS,
      /* Software limit to keep VkCommandBuffer tracking sane. The HW supports
       * up to 2^9 vertex attributes.
       */
      .maxVertexInputAttributes = MAX_VBS,
      .maxVertexInputBindings = MAX_VBS,
      /* MALI_ATTRIBUTE::offset is 32-bit. */
      .maxVertexInputAttributeOffset = UINT32_MAX,
      /* MALI_ATTRIBUTE_BUFFER::stride is 32-bit. */
      .maxVertexInputBindingStride = MESA_VK_MAX_VERTEX_BINDING_STRIDE,
      /* 32 vec4 varyings. */
      .maxVertexOutputComponents = 128,
      /* Tesselation shaders not supported. */
      .maxTessellationGenerationLevel = 0,
      .maxTessellationPatchSize = 0,
      .maxTessellationControlPerVertexInputComponents = 0,
      .maxTessellationControlPerVertexOutputComponents = 0,
      .maxTessellationControlPerPatchOutputComponents = 0,
      .maxTessellationControlTotalOutputComponents = 0,
      .maxTessellationEvaluationInputComponents = 0,
      .maxTessellationEvaluationOutputComponents = 0,
      /* Geometry shaders not supported. */
      .maxGeometryShaderInvocations = 0,
      .maxGeometryInputComponents = 0,
      .maxGeometryOutputComponents = 0,
      .maxGeometryOutputVertices = 0,
      .maxGeometryTotalOutputComponents = 0,
      /* 32 vec4 varyings. */
      .maxFragmentInputComponents = 128,
      /* 8 render targets. */
      .maxFragmentOutputAttachments = MAX_RTS,
      .maxFragmentDualSrcAttachments = max_cbuf_atts,
      /* 8 render targets, 2^12 storage buffers and 2^8 storage images (see
       * above).
       */
      .maxFragmentCombinedOutputResources = MAX_RTS + (1 << 12) + (1 << 8),
      /* MALI_LOCAL_STORAGE::wls_size_{base,scale} allows us to have up to
       * (7 << 30) bytes of shared memory, but we cap it to 32K as it doesn't
       * really make sense to expose this amount of memory, especially since
       * it's backed by global memory anyway.
       */
      .maxComputeSharedMemorySize = 32768,
      /* Software limit to meet Vulkan 1.0 requirements. We split the
       * dispatch in several jobs if it's too big.
       */
      .maxComputeWorkGroupCount = {65535, 65535, 65535},
      /* We could also split into serveral jobs but this has many limitations.
       * As such we limit to the max threads per workgroup supported by the GPU.
       */
      .maxComputeWorkGroupInvocations = device->kmod.props.max_threads_per_wg,
      .maxComputeWorkGroupSize = {device->kmod.props.max_threads_per_wg,
                                  device->kmod.props.max_threads_per_wg,
                                  device->kmod.props.max_threads_per_wg},
      /* 8-bit subpixel precision. */
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .mipmapPrecisionBits = 8,
      /* Software limit. */
      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = PAN_ARCH >= 10 ? UINT32_MAX : 1,
      .maxSamplerLodBias = (float)INT16_MAX / 256.0f,
      .maxSamplerAnisotropy = 16,
      .maxViewports = 1,
      /* Same as the framebuffer limit. */
      .maxViewportDimensions = {(1 << 14), (1 << 14)},
      /* Encoded in a 16-bit signed integer. */
      .viewportBoundsRange = {INT16_MIN, INT16_MAX},
      .viewportSubPixelBits = 0,
      /* Align on a page. */
      .minMemoryMapAlignment = os_page_size,
      /* Some compressed texture formats require 128-byte alignment. */
      .minTexelBufferOffsetAlignment = 64,
      /* Always aligned on a uniform slot (vec4). */
      .minUniformBufferOffsetAlignment = 16,
      /* Lowered to global accesses, which happen at the 32-bit granularity. */
      .minStorageBufferOffsetAlignment = 4,
      /* Signed 4-bit value. */
      .minTexelOffset = -8,
      .maxTexelOffset = 7,
      .minTexelGatherOffset = -8,
      .maxTexelGatherOffset = 7,
      .minInterpolationOffset = -0.5,
      .maxInterpolationOffset = 0.5,
      .subPixelInterpolationOffsetBits = 8,
      .maxFramebufferWidth = (1 << 14),
      .maxFramebufferHeight = (1 << 14),
      .maxFramebufferLayers = 256,
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .maxColorAttachments = max_cbuf_atts,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = sample_counts,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT,
      .maxSampleMaskWords = 1,
      .timestampComputeAndGraphics = PAN_ARCH >= 10 && device->kmod.props.gpu_can_query_timestamp,
      .timestampPeriod = PAN_ARCH >= 10 ? panvk_get_gpu_system_timestamp_period(device) : 0,
      .maxClipDistances = 0,
      .maxCullDistances = 0,
      .maxCombinedClipAndCullDistances = 0,
      .discreteQueuePriorities = 2,
      .pointSizeRange = {pointSizeRangeMin, pointSizeRangeMax},
      .lineWidthRange = {0.0, 7.9921875},
      .pointSizeGranularity = (1.0 / 16.0),
      .lineWidthGranularity = (1.0 / 128.0),
      .strictLines = true,
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 64,
      .optimalBufferCopyRowPitchAlignment = 64,
      .nonCoherentAtomSize = 64,

      /* Vulkan 1.0 sparse properties */
      .sparseResidencyNonResidentStrict = false,
      .sparseResidencyAlignedMipSize = false,
      .sparseResidencyStandard2DBlockShape = true,
      .sparseResidencyStandard2DMultisampleBlockShape = false,
      .sparseResidencyStandard3DBlockShape = false,

      /* Vulkan 1.1 properties */
      .subgroupSize = pan_subgroup_size(PAN_ARCH),
      /* We only support VS, FS, and CS.
       *
       * The HW may spawn VS invocations for non-existing indices, which could
       * be observed through subgroup ops (though the user can observe them
       * through infinte loops anyway), so subgroup ops can't be supported in
       * VS.
       */
      .subgroupSupportedStages =
         VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
      .subgroupSupportedOperations =
         VK_SUBGROUP_FEATURE_BASIC_BIT |
         VK_SUBGROUP_FEATURE_VOTE_BIT |
         VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
         VK_SUBGROUP_FEATURE_BALLOT_BIT |
         VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
         VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
         VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
         VK_SUBGROUP_FEATURE_QUAD_BIT |
         VK_SUBGROUP_FEATURE_ROTATE_BIT |
         VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT,
      .subgroupQuadOperationsInAllStages = false,
      .pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES,
      .maxMultiviewViewCount = 8,
      .maxMultiviewInstanceIndex = UINT32_MAX,
      .protectedNoFault = false,
      .maxPerSetDescriptors = UINT16_MAX,
      /* Our buffer size fields allow only this much */
      .maxMemoryAllocationSize = UINT32_MAX,

      /* Vulkan 1.2 properties */
      .driverID = VK_DRIVER_ID_MESA_PANVK,
      .conformanceVersion = get_conformance_version(),
      .denormBehaviorIndependence = PAN_ARCH >= 9 ?
         VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE :
         VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
      .roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
      .shaderSignedZeroInfNanPreserveFloat16 = true,
      .shaderSignedZeroInfNanPreserveFloat32 = true,
      .shaderSignedZeroInfNanPreserveFloat64 = false,
      .shaderDenormPreserveFloat16 = true,
      .shaderDenormPreserveFloat32 = true,
      .shaderDenormPreserveFloat64 = true,
      .shaderDenormFlushToZeroFloat16 = true,
      .shaderDenormFlushToZeroFloat32 = true,
      .shaderDenormFlushToZeroFloat64 = true,
      .shaderRoundingModeRTEFloat16 = true,
      .shaderRoundingModeRTEFloat32 = true,
      .shaderRoundingModeRTEFloat64 = false,
      .shaderRoundingModeRTZFloat16 = true,
      .shaderRoundingModeRTZFloat32 = true,
      .shaderRoundingModeRTZFloat64 = false,
      .maxUpdateAfterBindDescriptorsInAllPools =
         PAN_ARCH >= 9 ? UINT32_MAX : 0,
      .shaderUniformBufferArrayNonUniformIndexingNative = false,
      .shaderSampledImageArrayNonUniformIndexingNative = false,
      .shaderStorageBufferArrayNonUniformIndexingNative = false,
      .shaderStorageImageArrayNonUniformIndexingNative = false,
      .shaderInputAttachmentArrayNonUniformIndexingNative = false,
      .robustBufferAccessUpdateAfterBind = PAN_ARCH >= 9,
      .quadDivergentImplicitLod = false,
      .maxPerStageDescriptorUpdateAfterBindSamplers =
         PAN_ARCH >= 9 ? MAX_PER_STAGE_SAMPLERS : 0,
      .maxPerStageDescriptorUpdateAfterBindUniformBuffers =
         PAN_ARCH >= 9 ? MAX_PER_STAGE_UNIFORM_BUFFERS : 0,
      .maxPerStageDescriptorUpdateAfterBindStorageBuffers =
         PAN_ARCH >= 9 ? MAX_PER_STAGE_STORAGE_BUFFERS : 0,
      .maxPerStageDescriptorUpdateAfterBindSampledImages =
         PAN_ARCH >= 9 ? MAX_PER_STAGE_SAMPLED_IMAGES : 0,
      .maxPerStageDescriptorUpdateAfterBindStorageImages =
         PAN_ARCH >= 9 ? MAX_PER_STAGE_STORAGE_IMAGES : 0,
      .maxPerStageDescriptorUpdateAfterBindInputAttachments =
         PAN_ARCH >= 9 ? MAX_PER_STAGE_INPUT_ATTACHMENTS : 0,
      .maxPerStageUpdateAfterBindResources =
         PAN_ARCH >= 9 ? MAX_PER_STAGE_RESOURCES : 0,
      .maxDescriptorSetUpdateAfterBindSamplers =
         PAN_ARCH >= 9 ? MAX_PER_SET_SAMPLERS : 0,
      .maxDescriptorSetUpdateAfterBindUniformBuffers =
         PAN_ARCH >= 9 ? MAX_PER_SET_UNIFORM_BUFFERS : 0,
      .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic =
         PAN_ARCH >= 9 ? MAX_DYNAMIC_UNIFORM_BUFFERS : 0,
      .maxDescriptorSetUpdateAfterBindStorageBuffers =
         PAN_ARCH >= 9 ? MAX_PER_SET_STORAGE_BUFFERS : 0,
      .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic =
         PAN_ARCH >= 9 ? MAX_DYNAMIC_STORAGE_BUFFERS : 0,
      .maxDescriptorSetUpdateAfterBindSampledImages =
         PAN_ARCH >= 9 ? MAX_PER_SET_SAMPLED_IMAGES : 0,
      .maxDescriptorSetUpdateAfterBindStorageImages =
         PAN_ARCH >= 9 ? MAX_PER_SET_STORAGE_IMAGES : 0,
      .maxDescriptorSetUpdateAfterBindInputAttachments =
         PAN_ARCH >= 9 ? MAX_PER_SET_INPUT_ATTACHMENTS : 0,
      .supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                    VK_RESOLVE_MODE_AVERAGE_BIT |
                                    VK_RESOLVE_MODE_MIN_BIT |
                                    VK_RESOLVE_MODE_MAX_BIT,
      .supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                      VK_RESOLVE_MODE_MIN_BIT |
                                      VK_RESOLVE_MODE_MAX_BIT,
      .independentResolveNone = true,
      .independentResolve = true,
      .filterMinmaxSingleComponentFormats = PAN_ARCH >= 10,
      .filterMinmaxImageComponentMapping = PAN_ARCH >= 10,
      .maxTimelineSemaphoreValueDifference = INT64_MAX,
      .framebufferIntegerColorSampleCounts = sample_counts,

      /* Vulkan 1.3 properties */
      .minSubgroupSize = pan_subgroup_size(PAN_ARCH),
      .maxSubgroupSize = pan_subgroup_size(PAN_ARCH),
      .maxComputeWorkgroupSubgroups =
         device->kmod.props.max_threads_per_wg / pan_subgroup_size(PAN_ARCH),
      .requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT,
      .maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE,
      .maxPerStageDescriptorInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxDescriptorSetInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxInlineUniformTotalSize =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS * MAX_INLINE_UNIFORM_BLOCK_SIZE,
      .integerDotProduct8BitUnsignedAccelerated = false,
      .integerDotProduct8BitSignedAccelerated = false,
      .integerDotProduct8BitMixedSignednessAccelerated = false,
      .integerDotProduct4x8BitPackedUnsignedAccelerated = PAN_ARCH >= 9,
      .integerDotProduct4x8BitPackedSignedAccelerated = PAN_ARCH >= 9,
      .integerDotProduct16BitUnsignedAccelerated = false,
      .integerDotProduct16BitSignedAccelerated = false,
      .integerDotProduct16BitMixedSignednessAccelerated = false,
      .integerDotProduct32BitUnsignedAccelerated = false,
      .integerDotProduct32BitSignedAccelerated = false,
      .integerDotProduct32BitMixedSignednessAccelerated = false,
      .integerDotProduct64BitUnsignedAccelerated = false,
      .integerDotProduct64BitSignedAccelerated = false,
      .integerDotProduct64BitMixedSignednessAccelerated = false,
      .integerDotProductAccumulatingSaturating8BitUnsignedAccelerated = false,
      .integerDotProductAccumulatingSaturating8BitSignedAccelerated = false,
      .integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated = false,
      .integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = PAN_ARCH >= 9,
      .integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated = PAN_ARCH >= 9,
      .integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated = false,
      .integerDotProductAccumulatingSaturating16BitUnsignedAccelerated = false,
      .integerDotProductAccumulatingSaturating16BitSignedAccelerated = false,
      .integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated = false,
      .integerDotProductAccumulatingSaturating32BitUnsignedAccelerated = false,
      .integerDotProductAccumulatingSaturating32BitSignedAccelerated = false,
      .integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated = false,
      .integerDotProductAccumulatingSaturating64BitUnsignedAccelerated = false,
      .integerDotProductAccumulatingSaturating64BitSignedAccelerated = false,
      .integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated = false,
      .storageTexelBufferOffsetAlignmentBytes = 64,
      .storageTexelBufferOffsetSingleTexelAlignment = false,
      .uniformTexelBufferOffsetAlignmentBytes = PAN_ARCH >= 9 ? 4 : 64,
      .uniformTexelBufferOffsetSingleTexelAlignment = PAN_ARCH >= 9,
      .maxBufferSize = PANVK_MAX_BUFFER_SIZE,

      /* Vulkan 1.4 properties */
      .lineSubPixelPrecisionBits = 8,
      /* We will have to restrict this a bit for multiview */
      .maxVertexAttribDivisor = UINT32_MAX,
      .supportsNonZeroFirstInstance = true,
      .maxPushDescriptors = MAX_PUSH_DESCS,
      .dynamicRenderingLocalReadDepthStencilAttachments = true,
      .dynamicRenderingLocalReadMultisampledAttachments = true,
      .earlyFragmentMultisampleCoverageAfterSampleCounting = true,
      .earlyFragmentSampleMaskTestBeforeSampleCounting = true,
      .depthStencilSwizzleOneSupport = true,
      .polygonModePointSize = false,
      .nonStrictSinglePixelWideLinesUseParallelogram = false,
      .nonStrictWideLinesUseParallelogram = false,
      .blockTexelViewCompatibleMultipleLayers = true,
      .maxCombinedImageSamplerDescriptorCount = 1,
      /* We don't implement VK_KHR_fragment_shading_rate */
      .fragmentShadingRateClampCombinerInputs = false,
      .defaultRobustnessStorageBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT,
      .defaultRobustnessUniformBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT,
      .defaultRobustnessVertexInputs =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT,
      .defaultRobustnessImages =
         VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_EXT,
      .identicalMemoryTypeRequirements = true,

      /* VK_KHR_pipeline_binary */
      .pipelineBinaryInternalCache = has_disk_cache,
      .pipelineBinaryInternalCacheControl = has_disk_cache,
      .pipelineBinaryPrefersInternalCache = has_disk_cache,
      .pipelineBinaryPrecompiledInternalCache = has_disk_cache,
      .pipelineBinaryCompressedData = false,

      /* VK_EXT_robustness2 */
      .robustStorageBufferAccessSizeAlignment = 1,
      .robustUniformBufferAccessSizeAlignment = 1,

      /* VK_EXT_shader_object */
      /* We do not currently support VK_EXT_shader_object but this is used
       * internally by vk_shader
       */
      .shaderBinaryVersion = 0,

      /* VK_KHR_maintenance7 */
      /* We don't implement VK_KHR_fragment_shading_rate */
      .robustFragmentShadingRateAttachmentAccess = false,
      .separateDepthStencilAttachmentAccess = false,
      .maxDescriptorSetTotalUniformBuffersDynamic = MAX_DYNAMIC_UNIFORM_BUFFERS,
      .maxDescriptorSetTotalStorageBuffersDynamic = MAX_DYNAMIC_STORAGE_BUFFERS,
      .maxDescriptorSetTotalBuffersDynamic = MAX_DYNAMIC_BUFFERS,
      .maxDescriptorSetUpdateAfterBindTotalUniformBuffersDynamic =
         PAN_ARCH >= 9 ? MAX_DYNAMIC_UNIFORM_BUFFERS : 0,
      .maxDescriptorSetUpdateAfterBindTotalStorageBuffersDynamic =
         PAN_ARCH >= 9 ? MAX_DYNAMIC_STORAGE_BUFFERS : 0,
      .maxDescriptorSetUpdateAfterBindTotalBuffersDynamic =
         PAN_ARCH >= 9 ? MAX_DYNAMIC_BUFFERS : 0,

      /* VK_KHR_maintenance9 */
      /* Sparse binding not supported yet. */
      .image2DViewOf3DSparse = false,
      .defaultVertexAttributeValue = VK_DEFAULT_VERTEX_ATTRIBUTE_VALUE_ZERO_ZERO_ZERO_ZERO_KHR,

      /* VK_EXT_custom_border_color */
      .maxCustomBorderColorSamplers = 32768,

      /* VK_EXT_graphics_pipeline_library */
      .graphicsPipelineLibraryFastLinking = true,
      .graphicsPipelineLibraryIndependentInterpolationDecoration = true,

      /* VK_EXT_provoking_vertex */
      .provokingVertexModePerPipeline = false,
      .transformFeedbackPreservesTriangleFanProvokingVertex = false,

      /* VK_ANDROID_native_buffer */
      .sharedImage = vk_android_get_front_buffer_usage() != 0,

      /* VK_ARM_shader_core_properties */
      .pixelRate = device->model->rates.pixel,
      .texelRate = device->model->rates.texel,
      .fmaRate = device->model->rates.fma,

      /* VK_ARM_shader_core_builtins */
      .shaderCoreMask = device->kmod.props.shader_present,
      .shaderCoreCount = util_bitcount(device->kmod.props.shader_present),
      .shaderWarpsPerCore = device->kmod.props.max_threads_per_core /
                            (pan_subgroup_size(PAN_ARCH) * 2),
   };

   snprintf(properties->deviceName, sizeof(properties->deviceName), "%s",
            device->name);

   memcpy(properties->pipelineCacheUUID, device->cache_uuid, VK_UUID_SIZE);
   memcpy(properties->shaderBinaryUUID, device->cache_uuid, VK_UUID_SIZE);

   const struct {
      uint16_t vendor_id;
      uint32_t device_id;
      uint8_t pad[8];
   } dev_uuid = {
      .vendor_id = ARM_VENDOR_ID,
      .device_id = properties->deviceID,
   };

   STATIC_ASSERT(sizeof(dev_uuid) == VK_UUID_SIZE);
   memcpy(properties->deviceUUID, &dev_uuid, VK_UUID_SIZE);
   STATIC_ASSERT(sizeof(instance->driver_build_sha) >= VK_UUID_SIZE);
   memcpy(properties->driverUUID, instance->driver_build_sha, VK_UUID_SIZE);

   snprintf(properties->driverName, VK_MAX_DRIVER_NAME_SIZE, "panvk");
   snprintf(properties->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);

   /* VK_EXT_physical_device_drm */
   if (device->drm.primary_rdev) {
      properties->drmHasPrimary = true;
      properties->drmPrimaryMajor = major(device->drm.primary_rdev);
      properties->drmPrimaryMinor = minor(device->drm.primary_rdev);
   }
   if (device->drm.render_rdev) {
      properties->drmHasRender = true;
      properties->drmRenderMajor = major(device->drm.render_rdev);
      properties->drmRenderMinor = minor(device->drm.render_rdev);
   }

   /* VK_EXT_shader_module_identifier */
   STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                 sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
   memcpy(properties->shaderModuleIdentifierAlgorithmUUID,
          vk_shaderModuleIdentifierAlgorithmUUID,
          sizeof(properties->shaderModuleIdentifierAlgorithmUUID));

   /* VK_EXT_host_image_copy */
   /* We don't use image layouts, advertise all of them */
   static VkImageLayout supported_host_copy_layouts[] = {
      VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_PREINITIALIZED,

      /* Only if vk1.1+ is supported */
#if PAN_ARCH >= 10
      /*  Vulkan 1.1 */
      VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,

      /*  Vulkan 1.2 */
      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL,

      /* Vulkan 1.3 */
      VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,

      /* Vulkan 1.4 */
      VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ,
#endif
   };
   properties->pCopySrcLayouts = supported_host_copy_layouts;
   properties->copySrcLayoutCount = ARRAY_SIZE(supported_host_copy_layouts);
   properties->pCopyDstLayouts = supported_host_copy_layouts;
   properties->copyDstLayoutCount = ARRAY_SIZE(supported_host_copy_layouts);
   /* All HW has the same tiling layout, key off build hash only */
   STATIC_ASSERT(sizeof(instance->driver_build_sha) >= VK_UUID_SIZE);
   memcpy(properties->optimalTilingLayoutUUID, instance->driver_build_sha,
          VK_UUID_SIZE);

   if (PANVK_DEBUG(STARTUP)) {
      mesa_logi("%s (%s) %s", properties->driverName, properties->deviceName,
                properties->driverInfo);
   }
}
