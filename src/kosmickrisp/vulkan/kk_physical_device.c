/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_physical_device.h"

#include "kk_entrypoints.h"
#include "kk_image.h"
#include "kk_instance.h"
#include "kk_nir_lower_vbo.h"
#include "kk_sync.h"
#include "kk_wsi.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "git_sha1.h"

#include "vulkan/wsi/wsi_common.h"
#include "vk_device.h"
#include "vk_drm_syncobj.h"
#include "vk_shader_module.h"

static uint32_t
kk_get_vk_version()
{
   /* Version override takes priority */
   const uint32_t version_override = vk_get_version_override();
   if (version_override)
      return version_override;

   return VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION);
}

static void
kk_get_device_extensions(const struct kk_instance *instance,
                         struct vk_device_extension_table *ext)
{
   *ext = (struct vk_device_extension_table){
      /* Vulkan 1.1 */
      .KHR_16bit_storage = true,
      .KHR_bind_memory2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_descriptor_update_template = true,
      .KHR_device_group = true,
      .KHR_external_fence = true,
      .KHR_external_memory = true,
      .KHR_external_semaphore = true,
      .KHR_get_memory_requirements2 = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_multiview = true,
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_shader_draw_parameters = true,
      .KHR_storage_buffer_storage_class = true,
      .KHR_variable_pointers = true,

      /* Vulkan 1.2 */
      .KHR_8bit_storage = true,
      .KHR_buffer_device_address = true, /* Required in Vulkan 1.3 */
      .KHR_create_renderpass2 = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_draw_indirect_count = false,
      .KHR_driver_properties = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_atomic_int64 = false,
      .KHR_shader_float_controls = true,
      .KHR_shader_float16_int8 =
         false, /* TODO_KOSMICKRISP shaderInt8 shaderFloat16 */
      .KHR_shader_subgroup_extended_types = true,
      .KHR_spirv_1_4 = true,
      .KHR_timeline_semaphore = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_vulkan_memory_model = true, /* Required in Vulkan 1.3 */
      .EXT_descriptor_indexing = true,
      .EXT_host_query_reset = true,
      .EXT_sampler_filter_minmax = false,
      .EXT_scalar_block_layout = true,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_viewport_index_layer = true,

      /* Vulkan 1.3 */
      .KHR_copy_commands2 = true,
      .KHR_dynamic_rendering = true,
      .KHR_format_feature_flags2 = true,
      .KHR_maintenance4 = true,
      .KHR_shader_integer_dot_product = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_terminate_invocation = true,
      .KHR_synchronization2 = true,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats = true,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = false,
      .EXT_image_robustness = true,
      .EXT_inline_uniform_block = true,
      .EXT_pipeline_creation_cache_control = true,
      .EXT_pipeline_creation_feedback = true,
      .EXT_private_data = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_stencil_export = true,
      .EXT_subgroup_size_control = true,
      .EXT_texel_buffer_alignment = false,
      .EXT_texture_compression_astc_hdr = true,
      .EXT_tooling_info = true,
      .EXT_ycbcr_2plane_444_formats = true,

      /* Vulkan 1.4 */
      .KHR_load_store_op_none = true,
      .KHR_map_memory2 = true,
      .KHR_push_descriptor = true,
      .KHR_shader_expect_assume = true,
      .KHR_vertex_attribute_divisor = true,
      .EXT_vertex_attribute_divisor = true,

      /* Optional extensions */
      .KHR_shader_maximal_reconvergence = true,
      .KHR_shader_relaxed_extended_instruction = true,
      .KHR_shader_subgroup_uniform_control_flow = true,
#ifdef KK_USE_WSI_PLATFORM
      .KHR_swapchain = true,
      .KHR_swapchain_mutable_format = true,
#endif
      .KHR_workgroup_memory_explicit_layout = true,

      .EXT_external_memory_metal = true,
      .EXT_load_store_op_none = true,
      .EXT_mutable_descriptor_type = true,
      .EXT_shader_atomic_float = true,
      .EXT_shader_replicated_composites = true,

      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,
   };
}

static void
kk_get_device_features(
   const struct vk_device_extension_table *supported_extensions,
   struct vk_features *features)
{
   *features = (struct vk_features){
      /* Vulkan 1.0 */
      .alphaToOne = true,
      .depthClamp = true,
      .drawIndirectFirstInstance = true,
      .dualSrcBlend = true,
      /* TODO_KOSMICKRISP
       * Enabling fragmentStoresAndAtomics fails the following CTS tests, need
       * to investigate:
       * dEQP-VK.fragment_operations.early_fragment.discard_no_early_fragment_tests_depth
       * dEQP-VK.robustness.image_robustness.bind.notemplate.*i.unroll.nonvolatile.sampled_image.no_fmt_qual.img.samples_1.*d_array.frag
       */
      .fragmentStoresAndAtomics = false,
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .inheritedQueries = true,
      .logicOp = true,
      .multiViewport = true,
      .robustBufferAccess = true,
      .samplerAnisotropy = true,
      .shaderClipDistance = true,
      .shaderImageGatherExtended = true,
      .shaderInt16 = true,
      .shaderInt64 = true,
      .shaderResourceMinLod = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .textureCompressionASTC_LDR = true,
      .textureCompressionBC = true,
      .textureCompressionETC2 = true,

      /* Vulkan 1.1 */
      .multiview = true,
      .samplerYcbcrConversion = true,
      .shaderDrawParameters = true,
      .storageBuffer16BitAccess = true,
      /* TODO KOSMICKRISP
       * Disabled due to failing tests (vertex fragment interface mismatch):
       * dEQP-VK.spirv_assembly.instruction.graphics.16bit_storage.*
       */
      .storageInputOutput16 = false,
      .storagePushConstant16 = true,
      .uniformAndStorageBuffer16BitAccess = true,
      .variablePointers = true,
      .variablePointersStorageBuffer = true,

      /* Vulkan 1.2 */
      .descriptorBindingInlineUniformBlockUpdateAfterBind = true,
      .descriptorBindingPartiallyBound = true,
      .descriptorBindingSampledImageUpdateAfterBind = true,
      .descriptorBindingStorageBufferUpdateAfterBind = true,
      .descriptorBindingStorageImageUpdateAfterBind = true,
      .descriptorBindingStorageTexelBufferUpdateAfterBind = true,
      .descriptorBindingUniformBufferUpdateAfterBind = true,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = true,
      .descriptorBindingUpdateUnusedWhilePending = true,
      .descriptorBindingVariableDescriptorCount = true,
      .descriptorIndexing = true,
      .hostQueryReset = true,
      .imagelessFramebuffer = true,
      .multiDrawIndirect = true,
      .runtimeDescriptorArray = true,
      .samplerMirrorClampToEdge = true,
      .scalarBlockLayout = true,
      .separateDepthStencilLayouts = true,
      /* TODO_KOSMICKRISP shaderFloat16
       * Failing:
       * dEQP-VK.spirv_assembly.instruction.*.float16.opcompositeinsert.*
       * dEQP-VK.memory_model.shared.16bit.nested_structs_arrays.*
       */
      .shaderFloat16 = false,
      .shaderInputAttachmentArrayDynamicIndexing = true,
      .shaderInputAttachmentArrayNonUniformIndexing = true,
      /* TODO_KOSMICKRISP shaderInt8
       * Multiple MSL compiler crashes if we enable shaderInt8, need to
       * understand why and a workaround:
       * dEQP-VK.memory_model.shared.8bit.vector_types.9
       * dEQP-VK.memory_model.shared.8bit.basic_types.8
       * dEQP-VK.memory_model.shared.8bit.basic_arrays.2
       * dEQP-VK.memory_model.shared.8bit.arrays_of_arrays.1
       * dEQP-VK.memory_model.shared.8bit.arrays_of_arrays.8
       * Probably more
       */
      .shaderInt8 = false,
      .shaderOutputLayer = true,
      .shaderOutputViewportIndex = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .shaderStorageBufferArrayNonUniformIndexing = true,
      .shaderStorageImageArrayNonUniformIndexing = true,
      .shaderStorageTexelBufferArrayDynamicIndexing = true,
      .shaderStorageTexelBufferArrayNonUniformIndexing = true,
      .shaderSubgroupExtendedTypes = true,
      .shaderUniformBufferArrayNonUniformIndexing = true,
      .shaderUniformTexelBufferArrayDynamicIndexing = true,
      .shaderUniformTexelBufferArrayNonUniformIndexing = true,
      .storageBuffer8BitAccess = true,
      .storagePushConstant8 = true,
      .subgroupBroadcastDynamicId = true,
      .timelineSemaphore = true,
      .uniformAndStorageBuffer8BitAccess = true,
      .uniformBufferStandardLayout = true,

      /* Vulkan 1.3 */
      .bufferDeviceAddress = true,
      .computeFullSubgroups = true,
      .dynamicRendering = true,
      .inlineUniformBlock = true,
      .maintenance4 = true,
      .pipelineCreationCacheControl = true,
      .privateData = true,
      .robustImageAccess = true,
      .shaderDemoteToHelperInvocation = true,
      .shaderIntegerDotProduct = true,
      .shaderTerminateInvocation = true,
      .shaderZeroInitializeWorkgroupMemory = true,
      .subgroupSizeControl = true,
      .synchronization2 = true,
      .textureCompressionASTC_HDR = true,
      .vulkanMemoryModel = true,
      .vulkanMemoryModelDeviceScope = true,

      /* Vulkan 1.4 */
      .vertexAttributeInstanceRateDivisor = true,
      .vertexAttributeInstanceRateZeroDivisor = true,

      /* VK_EXT_mutable_descriptor_type */
      .mutableDescriptorType = true,

      /* VK_KHR_shader_expect_assume */
      .shaderExpectAssume = true,

      /* VK_KHR_shader_maximal_reconvergence */
      .shaderMaximalReconvergence = true,

      /* VK_KHR_shader_relaxed_extended_instruction */
      .shaderRelaxedExtendedInstruction = true,

      /* VK_KHR_workgroup_memory_explicit_layout */
      .workgroupMemoryExplicitLayout = true,
      .workgroupMemoryExplicitLayoutScalarBlockLayout = true,
      .workgroupMemoryExplicitLayout8BitAccess = true,
      .workgroupMemoryExplicitLayout16BitAccess = true,

      /* EXT_4444_formats */
      .formatA4R4G4B4 = true,
      .formatA4B4G4R4 = true,

      /* VK_EXT_shader_replicated_composites */
      .shaderReplicatedComposites = true,

      /* VK_KHR_shader_subgroup_uniform_control_flow */
      .shaderSubgroupUniformControlFlow = true,

      /* VK_EXT_shader_atomic_float */
      .shaderBufferFloat32Atomics = true,
      .shaderBufferFloat32AtomicAdd = true,
      .shaderSharedFloat32Atomics = true,

      /* EXT_ycbcr_2plane_444_formats */
      .ycbcr2plane444Formats = true,
   };
}

static void
kk_get_device_properties(const struct kk_physical_device *pdev,
                         const struct kk_instance *instance,
                         struct vk_properties *properties)
{
   const VkSampleCountFlagBits sample_counts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT |
      // TODO_KOSMICKRISP Modify sample count based on what pdev supports
      VK_SAMPLE_COUNT_4_BIT /* |
       VK_SAMPLE_COUNT_8_BIT */
      ;

   assert(sample_counts <= (KK_MAX_SAMPLES << 1) - 1);

   uint64_t os_page_size = 4096;
   os_get_page_size(&os_page_size);

   *properties = (struct vk_properties){
      .apiVersion = kk_get_vk_version(),
      .driverVersion = vk_get_driver_version(),
      .vendorID =
         instance->force_vk_vendor != 0 ? instance->force_vk_vendor : 0x106b,
      .deviceID = 100,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,

      /* Vulkan 1.0 limits */
      /* Values taken from Apple7
         https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf */
      .maxImageDimension1D = kk_image_max_dimension(VK_IMAGE_TYPE_2D),
      .maxImageDimension2D = kk_image_max_dimension(VK_IMAGE_TYPE_2D),
      .maxImageDimension3D = kk_image_max_dimension(VK_IMAGE_TYPE_3D),
      .maxImageDimensionCube = 16384,
      .maxImageArrayLayers = 2048,
      .maxTexelBufferElements = 256 * 1024 * 1024,
      .maxUniformBufferRange = 65536,
      .maxStorageBufferRange = UINT32_MAX,
      .maxPushConstantsSize = KK_MAX_PUSH_SIZE,
      .maxMemoryAllocationCount = 4096,
      .maxSamplerAllocationCount = 4000,
      .bufferImageGranularity = 16,
      .sparseAddressSpaceSize = KK_SPARSE_ADDR_SPACE_SIZE,
      .maxBoundDescriptorSets = KK_MAX_SETS,
      .maxPerStageDescriptorSamplers = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUniformBuffers = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorStorageBuffers = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorSampledImages = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorStorageImages = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorInputAttachments = KK_MAX_DESCRIPTORS,
      .maxPerStageResources = UINT32_MAX,
      .maxDescriptorSetSamplers = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetUniformBuffers = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetUniformBuffersDynamic = KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetStorageBuffers = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetStorageBuffersDynamic = KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetSampledImages = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetStorageImages = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetInputAttachments = KK_MAX_DESCRIPTORS,
      .maxVertexInputAttributes = KK_MAX_ATTRIBS,
      .maxVertexInputBindings = KK_MAX_VBUFS,
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
      .maxFragmentOutputAttachments = KK_MAX_RTS,
      .maxFragmentDualSrcAttachments = 1,
      .maxFragmentCombinedOutputResources = 16,
      .maxComputeSharedMemorySize = KK_MAX_SHARED_SIZE,
      .maxComputeWorkGroupCount = {0x7fffffff, 65535, 65535},
      .maxComputeWorkGroupInvocations = pdev->info.max_workgroup_invocations,
      .maxComputeWorkGroupSize = {pdev->info.max_workgroup_count[0],
                                  pdev->info.max_workgroup_count[1],
                                  pdev->info.max_workgroup_count[2]},
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .mipmapPrecisionBits = 8,
      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = UINT32_MAX,
      .maxSamplerLodBias = 15,
      .maxSamplerAnisotropy = 16,
      .maxViewports = KK_MAX_VIEWPORTS,
      .maxViewportDimensions = {32768, 32768},
      .viewportBoundsRange = {-65536, 65536},
      .viewportSubPixelBits = 8,
      .minMemoryMapAlignment = os_page_size,
      .minTexelBufferOffsetAlignment = KK_MIN_TEXEL_BUFFER_ALIGNMENT,
      .minUniformBufferOffsetAlignment = KK_MIN_UBO_ALIGNMENT,
      .minStorageBufferOffsetAlignment = KK_MIN_SSBO_ALIGNMENT,
      .minTexelOffset = -8,
      .maxTexelOffset = 7,
      .minTexelGatherOffset = -8,
      .maxTexelGatherOffset = 7,
      .minInterpolationOffset = -0.5,
      .maxInterpolationOffset = 0.4375,
      .subPixelInterpolationOffsetBits = 4,
      .maxFramebufferHeight = 16384,
      .maxFramebufferWidth = 16384,
      .maxFramebufferLayers = 2048,
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .maxColorAttachments = KK_MAX_RTS,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = sample_counts,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = sample_counts,
      .maxSampleMaskWords = 1,
      .timestampComputeAndGraphics = false,
      .timestampPeriod = 1,
      .maxClipDistances = 8,
      .maxCullDistances = 8,
      .maxCombinedClipAndCullDistances = 8,
      .discreteQueuePriorities = 2,
      .pointSizeRange = {1.0f, 1.0f},
      .lineWidthRange = {1.0f, 1.0f},
      .pointSizeGranularity = 0.0f,
      .lineWidthGranularity = 0.0f,
      .strictLines = false,
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 1,
      .optimalBufferCopyRowPitchAlignment = 1,
      .nonCoherentAtomSize = 64,

      /* Vulkan 1.0 sparse properties */
      .sparseResidencyNonResidentStrict = false,
      .sparseResidencyAlignedMipSize = false,
      .sparseResidencyStandard2DBlockShape = false,
      .sparseResidencyStandard2DMultisampleBlockShape = false,
      .sparseResidencyStandard3DBlockShape = false,

      /* Vulkan 1.1 properties */
      .subgroupSize = 32,
      .subgroupSupportedStages =
         VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
      .subgroupSupportedOperations =
         VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |
         VK_SUBGROUP_FEATURE_VOTE_BIT | VK_SUBGROUP_FEATURE_QUAD_BIT |
         VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
         VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
         VK_SUBGROUP_FEATURE_ROTATE_BIT_KHR, // | TODO_KOSMICKRISP
      // VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
      // VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
      // VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT_KHR,
      .subgroupQuadOperationsInAllStages = true,
      .pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY,
      .maxMultiviewViewCount = KK_MAX_MULTIVIEW_VIEW_COUNT,
      .maxMultiviewInstanceIndex = UINT32_MAX,
      .maxPerSetDescriptors = UINT32_MAX,
      .maxMemoryAllocationSize = (1u << 31),

      /* Vulkan 1.2 properties */
      .supportedDepthResolveModes =
         VK_RESOLVE_MODE_SAMPLE_ZERO_BIT | VK_RESOLVE_MODE_AVERAGE_BIT |
         VK_RESOLVE_MODE_MIN_BIT | VK_RESOLVE_MODE_MAX_BIT,
      .supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                      VK_RESOLVE_MODE_MIN_BIT |
                                      VK_RESOLVE_MODE_MAX_BIT,
      .independentResolveNone = true,
      .independentResolve = true,
      .driverID = VK_DRIVER_ID_MESA_KOSMICKRISP,
      .conformanceVersion = (VkConformanceVersion){1, 4, 3, 2},
      .denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE,
      .roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE,
      .shaderSignedZeroInfNanPreserveFloat16 = false,
      .shaderSignedZeroInfNanPreserveFloat32 = false,
      .shaderSignedZeroInfNanPreserveFloat64 = false,
      .shaderDenormPreserveFloat16 = false,
      .shaderDenormPreserveFloat32 = false,
      .shaderDenormPreserveFloat64 = false,
      .shaderDenormFlushToZeroFloat16 = false,
      .shaderDenormFlushToZeroFloat32 = false,
      .shaderDenormFlushToZeroFloat64 = false,
      .shaderRoundingModeRTEFloat16 = false,
      .shaderRoundingModeRTEFloat32 = false,
      .shaderRoundingModeRTEFloat64 = false,
      .shaderRoundingModeRTZFloat16 = false,
      .shaderRoundingModeRTZFloat32 = false,
      .shaderRoundingModeRTZFloat64 = false,
      .maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX,
      .shaderUniformBufferArrayNonUniformIndexingNative = true,
      .shaderSampledImageArrayNonUniformIndexingNative = true,
      .shaderStorageBufferArrayNonUniformIndexingNative = true,
      .shaderStorageImageArrayNonUniformIndexingNative = true,
      .shaderInputAttachmentArrayNonUniformIndexingNative = true,
      .robustBufferAccessUpdateAfterBind = true,
      .quadDivergentImplicitLod = false,
      .maxPerStageDescriptorUpdateAfterBindSamplers = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindUniformBuffers = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindStorageBuffers = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindSampledImages = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindStorageImages = KK_MAX_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindInputAttachments =
         KK_MAX_DESCRIPTORS,
      .maxPerStageUpdateAfterBindResources = UINT32_MAX,
      .maxDescriptorSetUpdateAfterBindSamplers = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindUniformBuffers = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic =
         KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindStorageBuffers = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic =
         KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindSampledImages = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindStorageImages = KK_MAX_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindInputAttachments = KK_MAX_DESCRIPTORS,
      .filterMinmaxSingleComponentFormats = false,
      .filterMinmaxImageComponentMapping = false,
      .maxTimelineSemaphoreValueDifference = UINT64_MAX,
      .framebufferIntegerColorSampleCounts = sample_counts,

      /* Vulkan 1.3 properties */
      .minSubgroupSize = 32,
      .maxSubgroupSize = 32,
      .maxComputeWorkgroupSubgroups = pdev->info.max_workgroup_invocations / 32,
      .requiredSubgroupSizeStages = 0,
      .maxInlineUniformBlockSize = 1 << 16,
      .maxPerStageDescriptorInlineUniformBlocks = 32,
      .maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks = 32,
      .maxDescriptorSetInlineUniformBlocks = 6 * 32,
      .maxDescriptorSetUpdateAfterBindInlineUniformBlocks = 6 * 32,
      .maxInlineUniformTotalSize = 1 << 16,
      .integerDotProduct4x8BitPackedUnsignedAccelerated = false,
      .integerDotProduct4x8BitPackedSignedAccelerated = false,
      .integerDotProduct4x8BitPackedMixedSignednessAccelerated = false,
      .storageTexelBufferOffsetAlignmentBytes = KK_MIN_TEXEL_BUFFER_ALIGNMENT,
      .storageTexelBufferOffsetSingleTexelAlignment = false,
      .uniformTexelBufferOffsetAlignmentBytes = KK_MIN_TEXEL_BUFFER_ALIGNMENT,
      .uniformTexelBufferOffsetSingleTexelAlignment = false,
      .maxBufferSize = KK_MAX_BUFFER_SIZE,

      /* VK_KHR_push_descriptor */
      .maxPushDescriptors = KK_MAX_PUSH_DESCRIPTORS,

      /* VK_EXT_custom_border_color */
      .maxCustomBorderColorSamplers = 4000,

      /* VK_EXT_extended_dynamic_state3 */
      .dynamicPrimitiveTopologyUnrestricted = false,

      /* VK_EXT_graphics_pipeline_library */
      .graphicsPipelineLibraryFastLinking = true,
      .graphicsPipelineLibraryIndependentInterpolationDecoration = true,

      /* VK_KHR_line_rasterization */
      .lineSubPixelPrecisionBits = 8,

      /* VK_KHR_maintenance5 */
      .earlyFragmentMultisampleCoverageAfterSampleCounting = false,
      .earlyFragmentSampleMaskTestBeforeSampleCounting = true,
      .depthStencilSwizzleOneSupport = false,
      .polygonModePointSize = false,
      .nonStrictSinglePixelWideLinesUseParallelogram = false,
      .nonStrictWideLinesUseParallelogram = false,

      /* VK_KHR_maintenance6 */
      .blockTexelViewCompatibleMultipleLayers = false,
      .maxCombinedImageSamplerDescriptorCount = 3,
      .fragmentShadingRateClampCombinerInputs = false, /* TODO */

      /* VK_KHR_maintenance7 */
      .robustFragmentShadingRateAttachmentAccess = false,
      .separateDepthStencilAttachmentAccess = false,
      .maxDescriptorSetTotalUniformBuffersDynamic = KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetTotalStorageBuffersDynamic = KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetTotalBuffersDynamic = KK_MAX_DYNAMIC_BUFFERS,
      .maxDescriptorSetUpdateAfterBindTotalUniformBuffersDynamic =
         KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindTotalStorageBuffersDynamic =
         KK_MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetUpdateAfterBindTotalBuffersDynamic =
         KK_MAX_DYNAMIC_BUFFERS,

      /* VK_EXT_legacy_vertex_attributes */
      .nativeUnalignedPerformance = true,

      /* VK_EXT_map_memory_placed */
      .minPlacedMemoryMapAlignment = os_page_size,

      /* VK_EXT_multi_draw */
      .maxMultiDrawCount = UINT32_MAX,

      /* VK_EXT_nested_command_buffer */
      .maxCommandBufferNestingLevel = UINT32_MAX,

      /* VK_EXT_pipeline_robustness */
      .defaultRobustnessStorageBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .defaultRobustnessUniformBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .defaultRobustnessVertexInputs =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .defaultRobustnessImages =
         VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2_EXT,

      /* VK_EXT_physical_device_drm gets populated later */

      /* VK_EXT_provoking_vertex */
      .provokingVertexModePerPipeline = true,
      .transformFeedbackPreservesTriangleFanProvokingVertex = true,

      /* VK_EXT_robustness2 */
      .robustStorageBufferAccessSizeAlignment = KK_SSBO_BOUNDS_CHECK_ALIGNMENT,
      .robustUniformBufferAccessSizeAlignment = KK_MIN_UBO_ALIGNMENT,

      /* VK_EXT_sample_locations */
      .sampleLocationSampleCounts = sample_counts,
      .maxSampleLocationGridSize = (VkExtent2D){1, 1},
      .sampleLocationCoordinateRange[0] = 0.0f,
      .sampleLocationCoordinateRange[1] = 0.9375f,
      .sampleLocationSubPixelBits = 4,
      .variableSampleLocations = false,

      /* VK_EXT_shader_object */
      .shaderBinaryVersion = 0,

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

      /* VK_KHR_vertex_attribute_divisor */
      .maxVertexAttribDivisor = UINT32_MAX,
      .supportsNonZeroFirstInstance = true,

      /* VK_KHR_fragment_shader_barycentric */
      .triStripVertexOrderIndependentOfProvokingVertex = false,
   };

   char gpu_name[256u];
   mtl_device_get_name(pdev->mtl_dev_handle, gpu_name);
   snprintf(properties->deviceName, sizeof(properties->deviceName), "%s",
            gpu_name);

   /* Not sure if there are layout specific things, so for now just reporting
    * all layouts from extensions.
    */
   static const VkImageLayout supported_layouts[] = {
      VK_IMAGE_LAYOUT_GENERAL, /* this one is required by spec */
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

   properties->pCopySrcLayouts = (VkImageLayout *)supported_layouts;
   properties->copySrcLayoutCount = ARRAY_SIZE(supported_layouts);
   properties->pCopyDstLayouts = (VkImageLayout *)supported_layouts;
   properties->copyDstLayoutCount = ARRAY_SIZE(supported_layouts);

   STATIC_ASSERT(sizeof(instance->driver_build_sha) >= VK_UUID_SIZE);
   memcpy(properties->optimalTilingLayoutUUID, instance->driver_build_sha,
          VK_UUID_SIZE);

   properties->identicalMemoryTypeRequirements = false;

   /* VK_EXT_shader_module_identifier */
   STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                 sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
   memcpy(properties->shaderModuleIdentifierAlgorithmUUID,
          vk_shaderModuleIdentifierAlgorithmUUID,
          sizeof(properties->shaderModuleIdentifierAlgorithmUUID));

   const struct {
      uint64_t registry_id;
      uint64_t pad;
   } dev_uuid = {
      .registry_id = mtl_device_get_registry_id(pdev->mtl_dev_handle),
   };
   STATIC_ASSERT(sizeof(dev_uuid) == VK_UUID_SIZE);
   memcpy(properties->deviceUUID, &dev_uuid, VK_UUID_SIZE);
   STATIC_ASSERT(sizeof(instance->driver_build_sha) >= VK_UUID_SIZE);
   memcpy(properties->driverUUID, instance->driver_build_sha, VK_UUID_SIZE);

   snprintf(properties->driverName, VK_MAX_DRIVER_NAME_SIZE, "KosmicKrisp");
   snprintf(properties->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);
}

static void
kk_physical_device_init_pipeline_cache(struct kk_physical_device *pdev)
{
   struct kk_instance *instance = kk_physical_device_instance(pdev);

   struct mesa_sha1 sha_ctx;
   _mesa_sha1_init(&sha_ctx);

   _mesa_sha1_update(&sha_ctx, instance->driver_build_sha,
                     sizeof(instance->driver_build_sha));

   unsigned char sha[SHA1_DIGEST_LENGTH];
   _mesa_sha1_final(&sha_ctx, sha);

   STATIC_ASSERT(SHA1_DIGEST_LENGTH >= VK_UUID_SIZE);
   memcpy(pdev->vk.properties.pipelineCacheUUID, sha, VK_UUID_SIZE);
   memcpy(pdev->vk.properties.shaderBinaryUUID, sha, VK_UUID_SIZE);
}

static void
kk_physical_device_free_disk_cache(struct kk_physical_device *pdev)
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

static uint64_t
kk_get_sysmem_heap_size(void)
{
   uint64_t sysmem_size_B = 0;
   if (!os_get_total_physical_memory(&sysmem_size_B))
      return 0;

   /* Use 3/4 of total size to avoid swapping */
   return ROUND_DOWN_TO(sysmem_size_B * 3 / 4, 1 << 20);
}

static uint64_t
kk_get_sysmem_heap_available(struct kk_physical_device *pdev)
{
   uint64_t sysmem_size_B = 0;
   if (!os_get_available_system_memory(&sysmem_size_B)) {
      vk_loge(VK_LOG_OBJS(pdev), "Failed to query available system memory");
      return 0;
   }

   /* Use 3/4 of available to avoid swapping */
   return ROUND_DOWN_TO(sysmem_size_B * 3 / 4, 1 << 20);
}

static void
get_metal_limits(struct kk_physical_device *pdev)
{
   struct mtl_size workgroup_size =
      mtl_device_max_threads_per_threadgroup(pdev->mtl_dev_handle);
   pdev->info.max_workgroup_count[0] = workgroup_size.x;
   pdev->info.max_workgroup_count[1] = workgroup_size.y;
   pdev->info.max_workgroup_count[2] = workgroup_size.z;
   pdev->info.max_workgroup_invocations =
      MAX3(workgroup_size.x, workgroup_size.y, workgroup_size.z);
}

VkResult
kk_enumerate_physical_devices(struct vk_instance *_instance)
{
   struct kk_instance *instance = (struct kk_instance *)_instance;
   VkResult result;

   struct kk_physical_device *pdev =
      vk_zalloc(&instance->vk.alloc, sizeof(*pdev), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   if (pdev == NULL) {
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   pdev->mtl_dev_handle = mtl_device_create();
   if (!pdev->mtl_dev_handle) {
      result = VK_SUCCESS;
      goto fail_alloc;
   }
   get_metal_limits(pdev);

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &kk_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   struct vk_device_extension_table supported_extensions;
   kk_get_device_extensions(instance, &supported_extensions);

   struct vk_features supported_features;
   kk_get_device_features(&supported_extensions, &supported_features);

   struct vk_properties properties;
   kk_get_device_properties(pdev, instance, &properties);

   properties.drmHasRender = false;

   result = vk_physical_device_init(&pdev->vk, &instance->vk,
                                    &supported_extensions, &supported_features,
                                    &properties, &dispatch_table);
   if (result != VK_SUCCESS)
      goto fail_mtl_dev;

   uint64_t sysmem_size_B = kk_get_sysmem_heap_size();
   if (sysmem_size_B == 0) {
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to query total system memory");
      goto fail_disk_cache;
   }

   uint32_t sysmem_heap_idx = pdev->mem_heap_count++;
   pdev->mem_heaps[sysmem_heap_idx] = (struct kk_memory_heap){
      .size = sysmem_size_B,
      .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      .available = kk_get_sysmem_heap_available,
   };

   pdev->mem_types[pdev->mem_type_count++] = (VkMemoryType){
      .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .heapIndex = sysmem_heap_idx,
   };

   assert(pdev->mem_heap_count <= ARRAY_SIZE(pdev->mem_heaps));
   assert(pdev->mem_type_count <= ARRAY_SIZE(pdev->mem_types));

   pdev->queue_families[pdev->queue_family_count++] = (struct kk_queue_family){
      .queue_flags =
         VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
      .queue_count = 1,
   };
   assert(pdev->queue_family_count <= ARRAY_SIZE(pdev->queue_families));

   pdev->sync_binary_type = vk_sync_binary_get_type(&kk_sync_type);
   unsigned st_idx = 0;
   pdev->sync_types[st_idx++] = &kk_sync_type;
   pdev->sync_types[st_idx++] = &pdev->sync_binary_type.sync;
   pdev->sync_types[st_idx++] = NULL;
   assert(st_idx <= ARRAY_SIZE(pdev->sync_types));
   pdev->vk.supported_sync_types = pdev->sync_types;

   result = kk_init_wsi(pdev);
   if (result != VK_SUCCESS)
      goto fail_disk_cache;

   list_add(&pdev->vk.link, &instance->vk.physical_devices.list);

   return VK_SUCCESS;

fail_disk_cache:
   vk_physical_device_finish(&pdev->vk);
fail_mtl_dev:
   mtl_release(pdev->mtl_dev_handle);
fail_alloc:
   vk_free(&instance->vk.alloc, pdev);
   return result;
}

void
kk_physical_device_destroy(struct vk_physical_device *vk_pdev)
{
   struct kk_physical_device *pdev =
      container_of(vk_pdev, struct kk_physical_device, vk);

   kk_finish_wsi(pdev);
   kk_physical_device_free_disk_cache(pdev);
   vk_physical_device_finish(&pdev->vk);
   mtl_release(pdev->mtl_dev_handle);
   vk_free(&pdev->vk.instance->alloc, pdev);
}

VKAPI_ATTR void VKAPI_CALL
kk_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   VK_FROM_HANDLE(kk_physical_device, pdev, physicalDevice);

   pMemoryProperties->memoryProperties.memoryHeapCount = pdev->mem_heap_count;
   for (int i = 0; i < pdev->mem_heap_count; i++) {
      pMemoryProperties->memoryProperties.memoryHeaps[i] = (VkMemoryHeap){
         .size = pdev->mem_heaps[i].size,
         .flags = pdev->mem_heaps[i].flags,
      };
   }

   pMemoryProperties->memoryProperties.memoryTypeCount = pdev->mem_type_count;
   for (int i = 0; i < pdev->mem_type_count; i++) {
      pMemoryProperties->memoryProperties.memoryTypes[i] = pdev->mem_types[i];
   }

   vk_foreach_struct(ext, pMemoryProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT: {
         VkPhysicalDeviceMemoryBudgetPropertiesEXT *p = (void *)ext;

         for (unsigned i = 0; i < pdev->mem_heap_count; i++) {
            const struct kk_memory_heap *heap = &pdev->mem_heaps[i];
            uint64_t used = p_atomic_read(&heap->used);

            /* From the Vulkan 1.3.278 spec:
             *
             *    "heapUsage is an array of VK_MAX_MEMORY_HEAPS VkDeviceSize
             *    values in which memory usages are returned, with one element
             *    for each memory heap. A heap’s usage is an estimate of how
             *    much memory the process is currently using in that heap."
             *
             * TODO: Include internal allocations?
             */
            p->heapUsage[i] = used;

            uint64_t available = heap->size;
            if (heap->available)
               available = heap->available(pdev);

            /* From the Vulkan 1.3.278 spec:
             *
             *    "heapBudget is an array of VK_MAX_MEMORY_HEAPS VkDeviceSize
             *    values in which memory budgets are returned, with one
             *    element for each memory heap. A heap’s budget is a rough
             *    estimate of how much memory the process can allocate from
             *    that heap before allocations may fail or cause performance
             *    degradation. The budget includes any currently allocated
             *    device memory."
             *
             * and
             *
             *    "The heapBudget value must be less than or equal to
             *    VkMemoryHeap::size for each heap."
             *
             * available (queried above) is the total amount free memory
             * system-wide and does not include our allocations so we need
             * to add that in.
             */
            uint64_t budget = MIN2(available + used, heap->size);

            /* Set the budget at 90% of available to avoid thrashing */
            p->heapBudget[i] = ROUND_DOWN_TO(budget * 9 / 10, 1 << 20);
         }

         /* From the Vulkan 1.3.278 spec:
          *
          *    "The heapBudget and heapUsage values must be zero for array
          *    elements greater than or equal to
          *    VkPhysicalDeviceMemoryProperties::memoryHeapCount. The
          *    heapBudget value must be non-zero for array elements less than
          *    VkPhysicalDeviceMemoryProperties::memoryHeapCount."
          */
         for (unsigned i = pdev->mem_heap_count; i < VK_MAX_MEMORY_HEAPS; i++) {
            p->heapBudget[i] = 0u;
            p->heapUsage[i] = 0u;
         }
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_FROM_HANDLE(kk_physical_device, pdev, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out, pQueueFamilyProperties,
                          pQueueFamilyPropertyCount);

   for (uint8_t i = 0; i < pdev->queue_family_count; i++) {
      const struct kk_queue_family *queue_family = &pdev->queue_families[i];

      vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p)
      {
         p->queueFamilyProperties.queueFlags = queue_family->queue_flags;
         p->queueFamilyProperties.queueCount = queue_family->queue_count;
         p->queueFamilyProperties.timestampValidBits =
            0; /* TODO_KOSMICKRISP Timestamp queries */
         p->queueFamilyProperties.minImageTransferGranularity =
            (VkExtent3D){1, 1, 1};
      }
   }
}

static const VkTimeDomainKHR kk_time_domains[] = {
   VK_TIME_DOMAIN_DEVICE_KHR,
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
#ifdef CLOCK_MONOTONIC_RAW
   VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR,
#endif
};

VKAPI_ATTR VkResult VKAPI_CALL
kk_GetPhysicalDeviceCalibrateableTimeDomainsKHR(VkPhysicalDevice physicalDevice,
                                                uint32_t *pTimeDomainCount,
                                                VkTimeDomainKHR *pTimeDomains)
{
   VK_OUTARRAY_MAKE_TYPED(VkTimeDomainKHR, out, pTimeDomains, pTimeDomainCount);

   for (int d = 0; d < ARRAY_SIZE(kk_time_domains); d++) {
      vk_outarray_append_typed(VkTimeDomainKHR, &out, i)
      {
         *i = kk_time_domains[d];
      }
   }

   return vk_outarray_status(&out);
}

VKAPI_ATTR void VKAPI_CALL
kk_GetPhysicalDeviceMultisamplePropertiesEXT(
   VkPhysicalDevice physicalDevice, VkSampleCountFlagBits samples,
   VkMultisamplePropertiesEXT *pMultisampleProperties)
{
   VK_FROM_HANDLE(kk_physical_device, pdev, physicalDevice);

   if (samples & pdev->vk.properties.sampleLocationSampleCounts) {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){1, 1};
   } else {
      pMultisampleProperties->maxSampleLocationGridSize = (VkExtent2D){0, 0};
   }
}
