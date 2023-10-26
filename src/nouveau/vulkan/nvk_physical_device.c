/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_physical_device.h"

#include "nvk_buffer.h"
#include "nvk_entrypoints.h"
#include "nvk_format.h"
#include "nvk_image.h"
#include "nvk_instance.h"
#include "nvk_shader.h"
#include "nvk_wsi.h"
#include "git_sha1.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"

#include "vulkan/runtime/vk_device.h"
#include "vulkan/runtime/vk_drm_syncobj.h"
#include "vulkan/wsi/wsi_common.h"

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <xf86drm.h>

#include "cl90c0.h"
#include "cl91c0.h"
#include "cla097.h"
#include "cla0c0.h"
#include "cla1c0.h"
#include "clb097.h"
#include "clb0c0.h"
#include "clb197.h"
#include "clb1c0.h"
#include "clc0c0.h"
#include "clc1c0.h"
#include "clc397.h"
#include "clc3c0.h"
#include "clc597.h"
#include "clc5c0.h"
#include "clc997.h"

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(nvk_instance, instance, _instance);
   return vk_instance_get_physical_device_proc_addr(&instance->vk, pName);
}

static void
nvk_get_device_extensions(const struct nv_device_info *info,
                          struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table) {
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_copy_commands2 = true,
      .KHR_create_renderpass2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_descriptor_update_template = true,
      .KHR_device_group = true,
      .KHR_draw_indirect_count = info->cls_eng3d >= TURING_A,
      .KHR_driver_properties = true,
      .KHR_dynamic_rendering = true,
      .KHR_external_fence = true,
      .KHR_external_fence_fd = true,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = true,
      .KHR_external_semaphore_fd = true,
      .KHR_format_feature_flags2 = true,
      .KHR_get_memory_requirements2 = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_maintenance4 = true,
      .KHR_map_memory2 = true,
      .KHR_multiview = true,
      .KHR_push_descriptor = true,
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_clock = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_spirv_1_4 = true,
      .KHR_storage_buffer_storage_class = true,
      .KHR_timeline_semaphore = true,
#ifdef NVK_USE_WSI_PLATFORM
      .KHR_swapchain = true,
      .KHR_swapchain_mutable_format = true,
#endif
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .KHR_workgroup_memory_explicit_layout = true,
      .EXT_4444_formats = true,
      .EXT_attachment_feedback_loop_layout = true,
      .EXT_border_color_swizzle = true,
      .EXT_buffer_device_address = true,
      .EXT_conditional_rendering = true,
      .EXT_custom_border_color = true,
      .EXT_depth_bias_control = true,
      .EXT_depth_clip_control = true,
      .EXT_depth_clip_enable = true,
      .EXT_descriptor_indexing = true,
      .EXT_dynamic_rendering_unused_attachments = true,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_extended_dynamic_state3 = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_host_query_reset = true,
      .EXT_image_2d_view_of_3d = true,
      .EXT_image_robustness = true,
      .EXT_image_sliced_view_of_3d = true,
      .EXT_image_view_min_lod = true,
      .EXT_index_type_uint8 = true,
      .EXT_inline_uniform_block = true,
      .EXT_line_rasterization = true,
      .EXT_load_store_op_none = true,
      .EXT_mutable_descriptor_type = true,
      .EXT_non_seamless_cube_map = true,
      .EXT_pci_bus_info = info->type == NV_DEVICE_TYPE_DIS,
      .EXT_physical_device_drm = true,
      .EXT_primitive_topology_list_restart = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_robustness2 = true,
      .EXT_sample_locations = info->cls_eng3d >= MAXWELL_B,
      .EXT_sampler_filter_minmax = info->cls_eng3d >= MAXWELL_B,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_viewport_index_layer = info->cls_eng3d >= MAXWELL_B,
      .EXT_tooling_info = true,
      .EXT_transform_feedback = true,
      .EXT_vertex_attribute_divisor = true,
      .EXT_vertex_input_dynamic_state = true,
      .EXT_ycbcr_2plane_444_formats = true,
      .EXT_ycbcr_image_arrays = true,
   };
}

static void
nvk_get_device_features(const struct nv_device_info *info,
                        struct vk_features *features)
{
   *features = (struct vk_features) {
      /* Vulkan 1.0 */
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
      .alphaToOne = true,
      .multiViewport = true,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = false,
      .textureCompressionBC = true,
      .textureCompressionASTC_LDR = false,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = true,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = true,
      .shaderImageGatherExtended = true,
      .shaderStorageImageExtendedFormats = true,
      /* TODO: shaderStorageImageMultisample */
      .shaderStorageImageReadWithoutFormat = info->cls_eng3d >= MAXWELL_A,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      /* TODO: shaderFloat64 */
      /* TODO: shaderInt64 */
      /* TODO: shaderInt16 */
      /* TODO: shaderResourceResidency */
      .shaderResourceMinLod = true,
      .sparseBinding = true,
      .sparseResidencyBuffer = info->cls_eng3d >= MAXWELL_A,
      /* TODO: sparseResidency* */
      /* TODO: variableMultisampleRate */
      /* TODO: inheritedQueries */
      .inheritedQueries = true,

      /* Vulkan 1.1 */
      .multiview = true,
      .multiviewGeometryShader = true,
      .multiviewTessellationShader = true,
      .variablePointersStorageBuffer = true,
      .variablePointers = true,
      .shaderDrawParameters = true,
      .samplerYcbcrConversion = true,

      /* Vulkan 1.2 */
      .samplerMirrorClampToEdge = true,
      .descriptorIndexing = true,
      .drawIndirectCount = info->cls_eng3d >= TURING_A,
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
      .descriptorBindingUniformBufferUpdateAfterBind = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingStorageImageUpdateAfterBind = true,
      .descriptorBindingStorageBufferUpdateAfterBind = true,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = true,
      .descriptorBindingStorageTexelBufferUpdateAfterBind = true,
      .descriptorBindingUpdateUnusedWhilePending = true,
      .descriptorBindingPartiallyBound = true,
      .descriptorBindingVariableDescriptorCount = true,
      .runtimeDescriptorArray = true,
      .samplerFilterMinmax = info->cls_eng3d >= MAXWELL_B,
      .imagelessFramebuffer = true,
      .uniformBufferStandardLayout = true,
      .separateDepthStencilLayouts = true,
      .hostQueryReset = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
      .bufferDeviceAddressCaptureReplay = false,
      .bufferDeviceAddressMultiDevice = false,
      .shaderOutputViewportIndex = info->cls_eng3d >= MAXWELL_B,
      .shaderOutputLayer = info->cls_eng3d >= MAXWELL_B,

      /* Vulkan 1.3 */
      .robustImageAccess = true,
      .inlineUniformBlock = true,
      .descriptorBindingInlineUniformBlockUpdateAfterBind = true,
      .privateData = true,
      .shaderDemoteToHelperInvocation = true,
      .dynamicRendering = true,
      .maintenance4 = true,

      /* VK_KHR_shader_clock */
      .shaderSubgroupClock = true,
      .shaderDeviceClock = true,

      /* VK_KHR_workgroup_memory_explicit_layout */
      .workgroupMemoryExplicitLayout = true,
      .workgroupMemoryExplicitLayoutScalarBlockLayout = true,
      .workgroupMemoryExplicitLayout8BitAccess = false,
      .workgroupMemoryExplicitLayout16BitAccess = false,

      /* VK_EXT_4444_formats */
      .formatA4R4G4B4 = true,
      .formatA4B4G4R4 = true,

      /* VK_EXT_attachment_feedback_loop_layout */
      .attachmentFeedbackLoopLayout = true,

      /* VK_EXT_border_color_swizzle */
      .borderColorSwizzle = true,
      .borderColorSwizzleFromImage = false,

      /* VK_EXT_buffer_device_address */
      .bufferDeviceAddressCaptureReplayEXT = false,

      /* VK_EXT_conditional_rendering */
      .conditionalRendering = true,
      .inheritedConditionalRendering = true,

      /* VK_EXT_custom_border_color */
      .customBorderColors = true,
      .customBorderColorWithoutFormat = true,

      /* VK_EXT_depth_bias_control */
      .depthBiasControl = true,
      .leastRepresentableValueForceUnormRepresentation = true,
      .floatRepresentation = false,
      .depthBiasExact = true,

      /* VK_EXT_depth_clip_control */
      .depthClipControl = info->cls_eng3d >= VOLTA_A,

      /* VK_EXT_depth_clip_enable */
      .depthClipEnable = true,

      /* VK_EXT_dynamic_rendering_unused_attachments */
      .dynamicRenderingUnusedAttachments = true,

      /* VK_EXT_extended_dynamic_state */
      .extendedDynamicState = true,

      /* VK_EXT_extended_dynamic_state2 */
      .extendedDynamicState2 = true,
      .extendedDynamicState2LogicOp = true,
      .extendedDynamicState2PatchControlPoints = true,

      /* VK_EXT_extended_dynamic_state3 */
      .extendedDynamicState3TessellationDomainOrigin = false,
      .extendedDynamicState3DepthClampEnable = true,
      .extendedDynamicState3PolygonMode = true,
      .extendedDynamicState3RasterizationSamples = false,
      .extendedDynamicState3SampleMask = false,
      .extendedDynamicState3AlphaToCoverageEnable = false,
      .extendedDynamicState3AlphaToOneEnable = false,
      .extendedDynamicState3LogicOpEnable = true,
      .extendedDynamicState3ColorBlendEnable = false,
      .extendedDynamicState3ColorBlendEquation = false,
      .extendedDynamicState3ColorWriteMask = false,
      .extendedDynamicState3RasterizationStream = false,
      .extendedDynamicState3ConservativeRasterizationMode = false,
      .extendedDynamicState3ExtraPrimitiveOverestimationSize = false,
      .extendedDynamicState3DepthClipEnable = true,
      .extendedDynamicState3SampleLocationsEnable = info->cls_eng3d >= MAXWELL_B,
      .extendedDynamicState3ColorBlendAdvanced = false,
      .extendedDynamicState3ProvokingVertexMode = true,
      .extendedDynamicState3LineRasterizationMode = true,
      .extendedDynamicState3LineStippleEnable = true,
      .extendedDynamicState3DepthClipNegativeOneToOne = true,
      .extendedDynamicState3ViewportWScalingEnable = false,
      .extendedDynamicState3ViewportSwizzle = false,
      .extendedDynamicState3CoverageToColorEnable = false,
      .extendedDynamicState3CoverageToColorLocation = false,
      .extendedDynamicState3CoverageModulationMode = false,
      .extendedDynamicState3CoverageModulationTableEnable = false,
      .extendedDynamicState3CoverageModulationTable = false,
      .extendedDynamicState3CoverageReductionMode = false,
      .extendedDynamicState3RepresentativeFragmentTestEnable = false,
      .extendedDynamicState3ShadingRateImageEnable = false,

      /* VK_EXT_image_2d_view_of_3d */
      .image2DViewOf3D = true,
      .sampler2DViewOf3D = true,

      /* VK_EXT_image_sliced_view_of_3d */
      .imageSlicedViewOf3D = true,

      /* VK_EXT_image_view_min_lod */
      .minLod = true,

      /* VK_EXT_index_type_uint8 */
      .indexTypeUint8 = true,

      /* VK_EXT_line_rasterization */
      .rectangularLines = true,
      .bresenhamLines = true,
      .smoothLines = true,
      .stippledRectangularLines = true,
      .stippledBresenhamLines = true,
      .stippledSmoothLines = true,

      /* VK_EXT_non_seamless_cube_map */
      .nonSeamlessCubeMap = true,

      /* VK_EXT_primitive_topology_list_restart */
      .primitiveTopologyListRestart = true,
      .primitiveTopologyPatchListRestart = true,

      /* VK_EXT_provoking_vertex */
      .provokingVertexLast = true,
      .transformFeedbackPreservesProvokingVertex = true,

      /* VK_EXT_robustness2 */
      .robustBufferAccess2 = true,
      .robustImageAccess2 = true,
      .nullDescriptor = true,

      /* VK_EXT_transform_feedback */
      .transformFeedback = true,
      .geometryStreams = true,

      /* VK_EXT_vertex_attribute_divisor */
      .vertexAttributeInstanceRateDivisor = true,
      .vertexAttributeInstanceRateZeroDivisor = true,

      /* VK_EXT_vertex_input_dynamic_state */
      .vertexInputDynamicState = true,

      /* VK_EXT_ycbcr_2plane_444_formats */
      .ycbcr2plane444Formats = true,

      /* VK_EXT_ycbcr_image_arrays */
      .ycbcrImageArrays = true,

      /* VK_VALVE_mutable_descriptor_type */
      .mutableDescriptorType = true,
   };
}

static void
nvk_get_device_properties(const struct nvk_instance *instance,
                          const struct nv_device_info *info,
                          struct vk_properties *properties)
{
   const VkSampleCountFlagBits sample_counts = VK_SAMPLE_COUNT_1_BIT |
                                               VK_SAMPLE_COUNT_2_BIT |
                                               VK_SAMPLE_COUNT_4_BIT |
                                               VK_SAMPLE_COUNT_8_BIT;

   *properties = (struct vk_properties) {
      .apiVersion = VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION),
      .driverVersion = vk_get_driver_version(),
      .vendorID = NVIDIA_VENDOR_ID,
      .deviceID = info->device_id,
      .deviceType = info->type == NV_DEVICE_TYPE_DIS ?
                    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU :
                    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,

      /* Vulkan 1.0 limits */
      .maxImageDimension1D = nvk_image_max_dimension(info, VK_IMAGE_TYPE_1D),
      .maxImageDimension2D = nvk_image_max_dimension(info, VK_IMAGE_TYPE_2D),
      .maxImageDimension3D = nvk_image_max_dimension(info, VK_IMAGE_TYPE_3D),
      .maxImageDimensionCube = 0x8000,
      .maxImageArrayLayers = 2048,
      .maxTexelBufferElements = 128 * 1024 * 1024,
      .maxUniformBufferRange = 65536,
      .maxStorageBufferRange = UINT32_MAX,
      .maxPushConstantsSize = NVK_MAX_PUSH_SIZE,
      .maxMemoryAllocationCount = 4096,
      .maxSamplerAllocationCount = 4000,
      .bufferImageGranularity = info->chipset >= 0x120 ? 0x400 : 0x10000,
      .sparseAddressSpaceSize = NVK_SPARSE_ADDR_SPACE_SIZE,
      .maxBoundDescriptorSets = NVK_MAX_SETS,
      .maxPerStageDescriptorSamplers = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUniformBuffers = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorStorageBuffers = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorSampledImages = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorStorageImages = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorInputAttachments = NVK_MAX_DESCRIPTORS,
      .maxPerStageResources = UINT32_MAX,
      .maxDescriptorSetSamplers = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetUniformBuffers = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetUniformBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetStorageBuffers = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetStorageBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetSampledImages = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetStorageImages = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetInputAttachments = NVK_MAX_DESCRIPTORS,
      .maxVertexInputAttributes = 32,
      .maxVertexInputBindings = 32,
      .maxVertexInputAttributeOffset = 2047,
      .maxVertexInputBindingStride = 2048,
      .maxVertexOutputComponents = 128,
      .maxTessellationGenerationLevel = 64,
      .maxTessellationPatchSize = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 120,
      .maxTessellationControlTotalOutputComponents = 4216,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations = 32,
      .maxGeometryInputComponents = 128,
      .maxGeometryOutputComponents = 128,
      .maxGeometryOutputVertices = 1024,
      .maxGeometryTotalOutputComponents = 1024,
      .maxFragmentInputComponents = 128,
      .maxFragmentOutputAttachments = NVK_MAX_RTS,
      .maxFragmentDualSrcAttachments = 1,
      .maxFragmentCombinedOutputResources = 16,
      .maxComputeSharedMemorySize = 49152,
      .maxComputeWorkGroupCount = {0x7fffffff, 65535, 65535},
      .maxComputeWorkGroupInvocations = 1024,
      .maxComputeWorkGroupSize = {1024, 1024, 64},
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .mipmapPrecisionBits = 8,
      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = UINT32_MAX,
      .maxSamplerLodBias = 15,
      .maxSamplerAnisotropy = 16,
      .maxViewports = NVK_MAX_VIEWPORTS,
      .maxViewportDimensions = { 32768, 32768 },
      .viewportBoundsRange = { -65536, 65536 },
      .viewportSubPixelBits = 8,
      .minMemoryMapAlignment = 64,
      .minTexelBufferOffsetAlignment =
         nvk_get_buffer_alignment(info, VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT_KHR |
                                        VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT_KHR,
                                  0),
      .minUniformBufferOffsetAlignment =
         nvk_get_buffer_alignment(info, VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR, 0),
      .minStorageBufferOffsetAlignment =
         nvk_get_buffer_alignment(info, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR, 0),
      .minTexelOffset = -8,
      .maxTexelOffset = 7,
      .minTexelGatherOffset = -32,
      .maxTexelGatherOffset = 31,
      .minInterpolationOffset = -0.5,
      .maxInterpolationOffset = 0.4375,
      .subPixelInterpolationOffsetBits = 4,
      .maxFramebufferHeight = info->chipset >= 0x130 ? 0x8000 : 0x4000,
      .maxFramebufferWidth = info->chipset >= 0x130 ? 0x8000 : 0x4000,
      .maxFramebufferLayers = 2048,
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .maxColorAttachments = NVK_MAX_RTS,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = sample_counts,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT,
      .maxSampleMaskWords = 1,
      .timestampComputeAndGraphics = true,
      .timestampPeriod = 1,
      .maxClipDistances = 8,
      .maxCullDistances = 8,
      .maxCombinedClipAndCullDistances = 8,
      .discreteQueuePriorities = 2,
      .pointSizeRange = { 1.0, 2047.94 },
      .lineWidthRange = { 1, 64 },
      .pointSizeGranularity = 0.0625,
      .lineWidthGranularity = 0.0625,
      .strictLines = true,
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 1,
      .optimalBufferCopyRowPitchAlignment = 1,
      .nonCoherentAtomSize = 64,

      /* Vulkan 1.0 sparse properties */
      .sparseResidencyNonResidentStrict = true,

      /* Vulkan 1.1 properties */
      .pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY,
      .maxMultiviewViewCount = NVK_MAX_MULTIVIEW_VIEW_COUNT,
      .maxMultiviewInstanceIndex = UINT32_MAX,
      .maxPerSetDescriptors = UINT32_MAX,
      .maxMemoryAllocationSize = (1u << 31),

      /* Vulkan 1.2 properties */
      .supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                    VK_RESOLVE_MODE_AVERAGE_BIT |
                                    VK_RESOLVE_MODE_MIN_BIT |
                                    VK_RESOLVE_MODE_MAX_BIT,
      .supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                      VK_RESOLVE_MODE_MIN_BIT |
                                      VK_RESOLVE_MODE_MAX_BIT,
      .independentResolveNone = true,
      .independentResolve = true,
      .driverID = VK_DRIVER_ID_MESA_NVK,
      .conformanceVersion = (VkConformanceVersion) { /* TODO: conf version */
         .major = 0,
         .minor = 0,
         .subminor = 0,
         .patch = 0,
      },
      .maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX,
      .shaderUniformBufferArrayNonUniformIndexingNative = false,
      .shaderSampledImageArrayNonUniformIndexingNative = info->cls_eng3d >= TURING_A,
      .shaderStorageBufferArrayNonUniformIndexingNative = true,
      .shaderStorageImageArrayNonUniformIndexingNative = info->cls_eng3d >= TURING_A,
      .shaderInputAttachmentArrayNonUniformIndexingNative = false,
      .robustBufferAccessUpdateAfterBind = true,
      .quadDivergentImplicitLod = info->cls_eng3d >= TURING_A,
      .maxPerStageDescriptorUpdateAfterBindSamplers = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindUniformBuffers = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindStorageBuffers = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindSampledImages = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindStorageImages = NVK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindInputAttachments = NVK_MAX_DESCRIPTORS,
      .maxPerStageUpdateAfterBindResources = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindSamplers = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindUniformBuffers = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindStorageBuffers = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = NVK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindSampledImages = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindStorageImages = NVK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindInputAttachments = NVK_MAX_DESCRIPTORS,
      .filterMinmaxSingleComponentFormats = true,
      .filterMinmaxImageComponentMapping = true,
      .maxTimelineSemaphoreValueDifference = UINT64_MAX,

      /* Vulkan 1.3 properties */
      .maxInlineUniformBlockSize = 1 << 16,
      .maxPerStageDescriptorInlineUniformBlocks = 32,
      .maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = 32,
      .maxDescriptorSetInlineUniformBlocks = 6 * 32,
      .maxDescriptorSetUpdateAfterBindInlineUniformBlocks = 6 * 32,
      .maxBufferSize = NVK_MAX_BUFFER_SIZE,

      /* VK_KHR_push_descriptor */
      .maxPushDescriptors = NVK_MAX_PUSH_DESCRIPTORS,

      /* VK_EXT_custom_border_color */
      .maxCustomBorderColorSamplers = 4000,

      /* VK_EXT_extended_dynamic_state3 */
      .dynamicPrimitiveTopologyUnrestricted = true,

      /* VK_EXT_line_rasterization */
      .lineSubPixelPrecisionBits = 8,

      /* VK_EXT_pci_bus_info */
      .pciDomain   = info->pci.domain,
      .pciBus      = info->pci.bus,
      .pciDevice   = info->pci.dev,
      .pciFunction = info->pci.func,

      /* VK_EXT_physical_device_drm gets populated later */

      /* VK_EXT_provoking_vertex */
      .provokingVertexModePerPipeline = true,
      .transformFeedbackPreservesTriangleFanProvokingVertex = true,

      /* VK_EXT_robustness2 */
      .robustStorageBufferAccessSizeAlignment = NVK_SSBO_BOUNDS_CHECK_ALIGNMENT,
      .robustUniformBufferAccessSizeAlignment = NVK_MIN_UBO_ALIGNMENT,

      /* VK_EXT_sample_locations */
      .sampleLocationSampleCounts = sample_counts,
      .maxSampleLocationGridSize = (VkExtent2D){ 1, 1 },
      .sampleLocationCoordinateRange[0] = 0.0f,
      .sampleLocationCoordinateRange[1] = 0.9375f,
      .sampleLocationSubPixelBits = 4,
      .variableSampleLocations = true,

      /* VK_EXT_transform_feedback */
      .maxTransformFeedbackStreams = 4,
      .maxTransformFeedbackBuffers = 4,
      .maxTransformFeedbackBufferSize = UINT32_MAX,
      .maxTransformFeedbackStreamDataSize = 2048,
      .maxTransformFeedbackBufferDataSize = 512,
      .maxTransformFeedbackBufferDataStride = 2048,
      .transformFeedbackQueries = true,
      .transformFeedbackStreamsLinesTriangles = false,
      .transformFeedbackRasterizationStreamSelect = true,
      .transformFeedbackDraw = true,

      /* VK_EXT_vertex_attribute_divisor */
      .maxVertexAttribDivisor = UINT32_MAX,
   };

   snprintf(properties->deviceName, sizeof(properties->deviceName),
            "%s", info->device_name);

   const struct {
      uint16_t vendor_id;
      uint16_t device_id;
      uint8_t pad[12];
   } dev_uuid = {
      .vendor_id = NVIDIA_VENDOR_ID,
      .device_id = info->device_id,
   };
   STATIC_ASSERT(sizeof(dev_uuid) == VK_UUID_SIZE);
   memcpy(properties->deviceUUID, &dev_uuid, VK_UUID_SIZE);
   STATIC_ASSERT(sizeof(instance->driver_build_sha) >= VK_UUID_SIZE);
   memcpy(properties->driverUUID, instance->driver_build_sha, VK_UUID_SIZE);

   snprintf(properties->driverName, VK_MAX_DRIVER_NAME_SIZE, "NVK");
   snprintf(properties->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);
}

static void
nvk_physical_device_init_pipeline_cache(struct nvk_physical_device *pdev)
{
   struct nvk_instance *instance = nvk_physical_device_instance(pdev);

   struct mesa_sha1 sha_ctx;
   _mesa_sha1_init(&sha_ctx);

   _mesa_sha1_update(&sha_ctx, instance->driver_build_sha,
                     sizeof(instance->driver_build_sha));

   const uint64_t compiler_flags = nvk_physical_device_compiler_flags(pdev);
   _mesa_sha1_update(&sha_ctx, &compiler_flags, sizeof(compiler_flags));

   unsigned char sha[SHA1_DIGEST_LENGTH];
   _mesa_sha1_final(&sha_ctx, sha);

   STATIC_ASSERT(SHA1_DIGEST_LENGTH >= VK_UUID_SIZE);
   memcpy(pdev->vk.properties.pipelineCacheUUID, sha, VK_UUID_SIZE);

#ifdef ENABLE_SHADER_CACHE
   char renderer[10];
   ASSERTED int len = snprintf(renderer, sizeof(renderer), "nvk_%04x",
                               pdev->info.chipset);
   assert(len == sizeof(renderer) - 2);

   char timestamp[41];
   _mesa_sha1_format(timestamp, instance->driver_build_sha);

   const uint64_t driver_flags = nvk_physical_device_compiler_flags(pdev);
   pdev->vk.disk_cache = disk_cache_create(renderer, timestamp, driver_flags);
#endif
}

static void
nvk_physical_device_free_disk_cache(struct nvk_physical_device *pdev)
{
#ifdef ENABLE_SHADER_CACHE
   if (pdev->vk.disk_cache) {
      disk_cache_destroy(pdev->vk.disk_cache);
      pdev->vk.disk_cache = NULL;
   }
#else
   assert(pdev->vk.disk_cache == NULL);
#endif
}

VkResult
nvk_create_drm_physical_device(struct vk_instance *_instance,
                               drmDevicePtr drm_device,
                               struct vk_physical_device **pdev_out)
{
   struct nvk_instance *instance = (struct nvk_instance *)_instance;
   VkResult result;

   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER)))
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   switch (drm_device->bustype) {
   case DRM_BUS_PCI:
      if (drm_device->deviceinfo.pci->vendor_id != NVIDIA_VENDOR_ID)
         return VK_ERROR_INCOMPATIBLE_DRIVER;
      break;

   case DRM_BUS_PLATFORM: {
      const char *compat_prefix = "nvidia,";
      bool found = false;
      for (int i = 0; drm_device->deviceinfo.platform->compatible[i] != NULL; i++) {
         if (strncmp(drm_device->deviceinfo.platform->compatible[0], compat_prefix, strlen(compat_prefix)) == 0) {
            found = true;
            break;
         }
      }
      if (!found)
         return VK_ERROR_INCOMPATIBLE_DRIVER;
      break;
   }

   default:
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }

   struct nouveau_ws_device *ws_dev = nouveau_ws_device_new(drm_device);
   if (!ws_dev)
      return vk_error(instance, VK_ERROR_INCOMPATIBLE_DRIVER);

   const struct nv_device_info info = ws_dev->info;
   const bool has_vm_bind = ws_dev->has_vm_bind;
   const struct vk_sync_type syncobj_sync_type =
      vk_drm_syncobj_get_type(ws_dev->fd);

   nouveau_ws_device_destroy(ws_dev);

   /* We don't support anything pre-Kepler */
   if (info.cls_eng3d < KEPLER_A)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   if ((info.cls_eng3d < TURING_A || info.cls_eng3d > ADA_A) &&
       !debug_get_bool_option("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", false)) {
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "WARNING: NVK is not well-tested on %s, pass "
                       "NVK_I_WANT_A_BROKEN_VULKAN_DRIVER=1 "
                       "if you know what you're doing.",
                       info.device_name);
   }

   if (!has_vm_bind) {
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "NVK Requires a Linux kernel version 6.6 or later");
   }

   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER))) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "NVK requires a render node");
   }

   struct stat st;
   if (stat(drm_device->nodes[DRM_NODE_RENDER], &st)) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "fstat() failed on %s: %m",
                       drm_device->nodes[DRM_NODE_RENDER]);
   }
   const dev_t render_dev = st.st_rdev;

   vk_warn_non_conformant_implementation("NVK");

   struct nvk_physical_device *pdev =
      vk_zalloc(&instance->vk.alloc, sizeof(*pdev),
                8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   if (pdev == NULL)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &nvk_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   struct vk_device_extension_table supported_extensions;
   nvk_get_device_extensions(&info, &supported_extensions);

   struct vk_features supported_features;
   nvk_get_device_features(&info, &supported_features);

   struct vk_properties properties;
   nvk_get_device_properties(instance, &info, &properties);

   properties.drmHasRender = true;
   properties.drmRenderMajor = major(render_dev);
   properties.drmRenderMinor = minor(render_dev);

   /* DRM primary is optional */
   if ((drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) &&
       !stat(drm_device->nodes[DRM_NODE_PRIMARY], &st)) {
      assert(st.st_rdev != 0);
      properties.drmHasPrimary = true;
      properties.drmPrimaryMajor = major(st.st_rdev);
      properties.drmPrimaryMinor = minor(st.st_rdev);
   }

   result = vk_physical_device_init(&pdev->vk, &instance->vk,
                                    &supported_extensions,
                                    &supported_features,
                                    &properties,
                                    &dispatch_table);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   pdev->render_dev = render_dev;
   pdev->info = info;

   nvk_physical_device_init_pipeline_cache(pdev);

   pdev->mem_heaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
   pdev->mem_types[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   pdev->mem_types[0].heapIndex = 0;

   uint64_t sysmem_size_B = 0;
   if (!os_get_available_system_memory(&sysmem_size_B)) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to query available system memory");
      goto fail_disk_cache;
   }

   if (pdev->info.vram_size_B) {
      pdev->mem_type_cnt = 2;
      pdev->mem_heap_cnt = 2;

      pdev->mem_heaps[0].size = pdev->info.vram_size_B;
      pdev->mem_heaps[1].size = sysmem_size_B;
      pdev->mem_heaps[1].flags = 0;
      pdev->mem_types[1].heapIndex = 1;
      pdev->mem_types[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   } else {
      pdev->mem_type_cnt = 1;
      pdev->mem_heap_cnt = 1;

      pdev->mem_heaps[0].size = sysmem_size_B;
      pdev->mem_types[0].propertyFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   }

   unsigned st_idx = 0;
   pdev->syncobj_sync_type = syncobj_sync_type;
   pdev->sync_types[st_idx++] = &pdev->syncobj_sync_type;
   pdev->sync_types[st_idx++] = NULL;
   assert(st_idx <= ARRAY_SIZE(pdev->sync_types));
   pdev->vk.supported_sync_types = pdev->sync_types;

   result = nvk_init_wsi(pdev);
   if (result != VK_SUCCESS)
      goto fail_disk_cache;

   *pdev_out = &pdev->vk;

   return VK_SUCCESS;

fail_disk_cache:
   nvk_physical_device_free_disk_cache(pdev);
   vk_physical_device_finish(&pdev->vk);
fail_alloc:
   vk_free(&instance->vk.alloc, pdev);
   return result;
}

void
nvk_physical_device_destroy(struct vk_physical_device *vk_pdev)
{
   struct nvk_physical_device *pdev =
      container_of(vk_pdev, struct nvk_physical_device, vk);

   nvk_finish_wsi(pdev);
   nvk_physical_device_free_disk_cache(pdev);
   vk_physical_device_finish(&pdev->vk);
   vk_free(&pdev->vk.instance->alloc, pdev);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);

   pMemoryProperties->memoryProperties.memoryHeapCount = pdev->mem_heap_cnt;
   for (int i = 0; i < pdev->mem_heap_cnt; i++) {
      pMemoryProperties->memoryProperties.memoryHeaps[i] = pdev->mem_heaps[i];
   }

   pMemoryProperties->memoryProperties.memoryTypeCount = pdev->mem_type_cnt;
   for (int i = 0; i < pdev->mem_type_cnt; i++) {
      pMemoryProperties->memoryProperties.memoryTypes[i] = pdev->mem_types[i];
   }

   vk_foreach_struct(ext, pMemoryProperties->pNext)
   {
      switch (ext->sType) {
      default:
         nvk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice,
   uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   // VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(
      VkQueueFamilyProperties2, out, pQueueFamilyProperties, pQueueFamilyPropertyCount);

   vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
      p->queueFamilyProperties.queueFlags = VK_QUEUE_GRAPHICS_BIT |
                                            VK_QUEUE_COMPUTE_BIT |
                                            VK_QUEUE_TRANSFER_BIT;
      p->queueFamilyProperties.queueFlags |= VK_QUEUE_SPARSE_BINDING_BIT;
      p->queueFamilyProperties.queueCount = 1;
      p->queueFamilyProperties.timestampValidBits = 64;
      p->queueFamilyProperties.minImageTransferGranularity = (VkExtent3D){1, 1, 1};
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetPhysicalDeviceMultisamplePropertiesEXT(
   VkPhysicalDevice physicalDevice,
   VkSampleCountFlagBits samples,
   VkMultisamplePropertiesEXT *pMultisampleProperties)
{
   VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);

   if (samples & pdev->vk.properties.sampleLocationSampleCounts) {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){1, 1};
   } else {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){0, 0};
   }
}
