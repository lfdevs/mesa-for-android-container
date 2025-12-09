/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv driver which is:
 * Copyright © 2019 Raspberry Pi
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_physical_device.h"

#include <sys/sysmacros.h>

#include "git_sha1.h"

#include "util/disk_cache.h"
#include "util/ralloc.h"

#include "vk_util.h"
#include "vk_log.h"

#include "hwdef/rogue_hw_utils.h"

#include "pco/pco.h"
#include "pco_uscgen_programs.h"

#include "pvr_device.h"
#include "pvr_dump_info.h"
#include "pvr_entrypoints.h"
#include "pvr_winsys.h"
#include "pvr_wsi.h"

#define VK_VENDOR_ID_IMAGINATION 0x1010

void
pvr_physical_device_dump_info(const struct pvr_physical_device *pdevice,
                              char *const *comp_display,
                              char *const *comp_render)
{
   drmVersionPtr version_display = NULL, version_render;
   struct pvr_device_dump_info info = { 0 };

   if (pdevice->ws->display_fd >= 0)
      version_display = drmGetVersion(pdevice->ws->display_fd);

   version_render = drmGetVersion(pdevice->ws->render_fd);
   if (!version_render) {
      drmFreeVersion(version_display);
      return;
   }

   info.device_info = &pdevice->dev_info;
   info.device_runtime_info = &pdevice->dev_runtime_info;
   if (version_display) {
      info.drm_display.patchlevel = version_display->version_patchlevel;
      info.drm_display.major = version_display->version_major;
      info.drm_display.minor = version_display->version_minor;
      info.drm_display.name = version_display->name;
      info.drm_display.date = version_display->date;
      info.drm_display.comp = comp_display;
   }
   info.drm_render.patchlevel = version_render->version_patchlevel;
   info.drm_render.major = version_render->version_major;
   info.drm_render.minor = version_render->version_minor;
   info.drm_render.name = version_render->name;
   info.drm_render.date = version_render->date;
   info.drm_render.comp = comp_render;

   pvr_dump_physical_device_info(&info);

   drmFreeVersion(version_display);
   drmFreeVersion(version_render);
}

void
pvr_physical_device_destroy(struct vk_physical_device *vk_pdevice)
{
   struct pvr_physical_device *pdevice =
      container_of(vk_pdevice, struct pvr_physical_device, vk);

   /* Be careful here. The device might not have been initialized. This can
    * happen since initialization is done in vkEnumeratePhysicalDevices() but
    * finish is done in vkDestroyInstance(). Make sure that you check for NULL
    * before freeing or that the freeing functions accept NULL pointers.
    */

   ralloc_free(pdevice->pco_ctx);

   pvr_wsi_finish(pdevice);

   pvr_physical_device_free_pipeline_cache(pdevice);

   if (pdevice->ws)
      pvr_winsys_destroy(pdevice->ws);

   vk_free(&pdevice->vk.instance->alloc, pdevice->render_path);
   vk_free(&pdevice->vk.instance->alloc, pdevice->display_path);

   vk_physical_device_finish(&pdevice->vk);

   vk_free(&pdevice->vk.instance->alloc, pdevice);
}

void
pvr_physical_device_free_pipeline_cache(struct pvr_physical_device *const pdevice)
{
#ifdef ENABLE_SHADER_CACHE
   if (!pdevice->vk.disk_cache)
      return;

   disk_cache_destroy(pdevice->vk.disk_cache);
   pdevice->vk.disk_cache = NULL;
#else
   assert(pdevice->vk.disk_cache);
#endif /* ENABLE_SHADER_CACHE */
}

static void pvr_physical_device_get_supported_extensions(
   struct vk_device_extension_table *extensions)
{
   *extensions = (struct vk_device_extension_table){
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_copy_commands2 = true,
      .KHR_create_renderpass2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_descriptor_update_template = true,
      .KHR_device_group = true,
      .KHR_driver_properties = true,
      .KHR_external_fence = true,
      .KHR_external_fence_fd = true,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = PVR_USE_WSI_PLATFORM,
      .KHR_external_semaphore_fd = PVR_USE_WSI_PLATFORM,
      .KHR_format_feature_flags2 = false,
      .KHR_get_memory_requirements2 = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
      .KHR_index_type_uint8 = false,
      .KHR_line_rasterization = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_map_memory2 = true,
      .KHR_multiview = true,
      .KHR_present_id2 = PVR_USE_WSI_PLATFORM,
      .KHR_present_wait2 = PVR_USE_WSI_PLATFORM,
      .KHR_relaxed_block_layout = true,
      .KHR_robustness2 = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_expect_assume = false,
      .KHR_shader_float_controls = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_relaxed_extended_instruction = true,
      .KHR_shader_subgroup_extended_types = true,
      .KHR_shader_terminate_invocation = true,
      .KHR_spirv_1_4 = true,
      .KHR_storage_buffer_storage_class = true,
      .KHR_swapchain = PVR_USE_WSI_PLATFORM,
      .KHR_swapchain_mutable_format = PVR_USE_WSI_PLATFORM,
      .KHR_timeline_semaphore = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_vertex_attribute_divisor = true,
      .KHR_zero_initialize_workgroup_memory = false,
      .EXT_border_color_swizzle = true,
      .EXT_color_write_enable = true,
      .EXT_custom_border_color = true,
      .EXT_depth_clamp_zero_one = true,
      .EXT_depth_clip_enable = true,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_extended_dynamic_state3 = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_host_query_reset = true,
      .EXT_image_2d_view_of_3d = true,
      .EXT_index_type_uint8 = false,
      .EXT_line_rasterization = true,
      .EXT_map_memory_placed = true,
      .EXT_physical_device_drm = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_robustness2 = true,
      .EXT_scalar_block_layout = true,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_shader_replicated_composites = true,
      .EXT_texel_buffer_alignment = false,
      .EXT_tooling_info = true,
      .EXT_vertex_attribute_divisor = true,
      .EXT_zero_initialize_device_memory = true,
   };
}

static void pvr_physical_device_get_supported_features(
   const struct pvr_device_info *const dev_info,
   struct vk_features *const features)
{
   *features = (struct vk_features){
      /* Vulkan 1.0 */
      .robustBufferAccess = true,
      .fullDrawIndexUint32 = false,
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = false,
      .tessellationShader = false,
      .sampleRateShading = true,
      .dualSrcBlend = false,
      .logicOp = true,
      .multiDrawIndirect = false,
      .drawIndirectFirstInstance = true,
      .depthClamp = false,
      .depthBiasClamp = false,
      .fillModeNonSolid = false,
      .depthBounds = false,
      .wideLines = false,
      .largePoints = true,
      .alphaToOne = true,
      .multiViewport = false,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = true,
      .textureCompressionASTC_LDR = false,
      .textureCompressionBC = false,
      .occlusionQueryPrecise = false,
      .pipelineStatisticsQuery = false,
      .vertexPipelineStoresAndAtomics = false,
      .fragmentStoresAndAtomics = false,
      .shaderTessellationAndGeometryPointSize = false,
      .shaderImageGatherExtended = false,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageMultisample = false,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = true,
      .shaderUniformBufferArrayDynamicIndexing = false,
      .shaderSampledImageArrayDynamicIndexing = false,
      .shaderStorageBufferArrayDynamicIndexing = false,
      .shaderStorageImageArrayDynamicIndexing = false,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      .shaderFloat64 = false,
      .shaderInt64 = false,
      .shaderInt16 = false,
      .shaderResourceResidency = false,
      .shaderResourceMinLod = false,
      .sparseBinding = false,
      .sparseResidencyBuffer = false,
      .sparseResidencyImage2D = false,
      .sparseResidencyImage3D = false,
      .sparseResidency2Samples = false,
      .sparseResidency4Samples = false,
      .sparseResidency8Samples = false,
      .sparseResidency16Samples = false,
      .sparseResidencyAliased = false,
      .variableMultisampleRate = false,
      .inheritedQueries = false,

      /* Vulkan 1.1 */
      .storageBuffer16BitAccess = false,
      .uniformAndStorageBuffer16BitAccess = false,
      .storagePushConstant16 = false,
      .storageInputOutput16 = false,
      .variablePointers = false,
      .protectedMemory = false,
      .samplerYcbcrConversion = false,

      /* Vulkan 1.2 */
      .samplerMirrorClampToEdge = true,
      .drawIndirectCount = false,
      .storageBuffer8BitAccess = false,
      .uniformAndStorageBuffer8BitAccess = false,
      .storagePushConstant8 = false,
      .shaderBufferInt64Atomics = false,
      .shaderSharedInt64Atomics = false,
      .shaderFloat16 = false,
      .shaderInt8 = false,
      .descriptorIndexing = false,
      .shaderInputAttachmentArrayDynamicIndexing = false,
      .shaderUniformTexelBufferArrayDynamicIndexing = false,
      .shaderStorageTexelBufferArrayDynamicIndexing = false,
      .shaderUniformBufferArrayNonUniformIndexing = false,
      .shaderSampledImageArrayNonUniformIndexing = false,
      .shaderStorageBufferArrayNonUniformIndexing = false,
      .shaderStorageImageArrayNonUniformIndexing = false,
      .shaderInputAttachmentArrayNonUniformIndexing = false,
      .shaderUniformTexelBufferArrayNonUniformIndexing = false,
      .shaderStorageTexelBufferArrayNonUniformIndexing = false,
      .descriptorBindingUniformBufferUpdateAfterBind = false,
      .descriptorBindingSampledImageUpdateAfterBind = false,
      .descriptorBindingStorageImageUpdateAfterBind = false,
      .descriptorBindingStorageBufferUpdateAfterBind = false,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = false,
      .descriptorBindingStorageTexelBufferUpdateAfterBind = false,
      .descriptorBindingUpdateUnusedWhilePending = false,
      .descriptorBindingPartiallyBound = false,
      .descriptorBindingVariableDescriptorCount = false,
      .runtimeDescriptorArray = false,
      .samplerFilterMinmax = false,
      .vulkanMemoryModel = false,
      .vulkanMemoryModelDeviceScope = false,
      .vulkanMemoryModelAvailabilityVisibilityChains = false,
      .shaderOutputViewportIndex = false,
      .shaderOutputLayer = false,
      .subgroupBroadcastDynamicId = true,

      /* VK_EXT_depth_clamp_zero_one */
      .depthClampZeroOne = true,

      /* VK_KHR_index_type_uint8 */
      .indexTypeUint8 = true,

      /* Vulkan 1.2 / VK_KHR_imageless_framebuffer */
      .imagelessFramebuffer = true,

      /* Vulkan 1.1 / VK_KHR_multiview */
      .multiview = true,
      .multiviewGeometryShader = false,
      .multiviewTessellationShader = false,

      /* Vulkan 1.1 / VK_KHR_shader_draw_parameters */
      .shaderDrawParameters = true,

      /* Vulkan 1.2 / VK_KHR_timeline_semaphore */
      .timelineSemaphore = true,

      /* Vulkan 1.2 / VK_KHR_separate_depth_stencil_layouts */
      .separateDepthStencilLayouts = true,

      /* VK_KHR_shader_relaxed_extended_instruction */
      .shaderRelaxedExtendedInstruction = true,

      /* Vulkan 1.2 / VK_KHR_shader_subgroup_extended_types */
      .shaderSubgroupExtendedTypes = true,

      /* Vulkan 1.1 / VK_KHR_robustness2 */
      .robustBufferAccess2 = false,
      .robustImageAccess2 = false,
      .nullDescriptor = true,

      /* Vulkan 1.2 / VK_KHR_uniform_buffer_standard_layout */
      .uniformBufferStandardLayout = true,

      /* VK_EXT_color_write_enable */
      .colorWriteEnable = true,

      /* Vulkan 1.3 / VK_EXT_extended_dynamic_state */
      .extendedDynamicState = true,

      /* Vulkan 1.3 / VK_EXT_extended_dynamic_state2 */
      .extendedDynamicState2 = true,
      .extendedDynamicState2LogicOp = false,
      .extendedDynamicState2PatchControlPoints = false,

      /* VK_EXT_extended_dynamic_state3 */
      .extendedDynamicState3TessellationDomainOrigin = false,
      .extendedDynamicState3DepthClampEnable = false,
      .extendedDynamicState3PolygonMode = false,
      .extendedDynamicState3RasterizationSamples = true,
      .extendedDynamicState3SampleMask = true,
      .extendedDynamicState3AlphaToCoverageEnable = true,
      .extendedDynamicState3AlphaToOneEnable = true,
      .extendedDynamicState3LogicOpEnable = false,
      .extendedDynamicState3ColorBlendEnable = false,
      .extendedDynamicState3ColorBlendEquation = false,
      .extendedDynamicState3ColorWriteMask = false,
      .extendedDynamicState3RasterizationStream = false,
      .extendedDynamicState3ConservativeRasterizationMode = false,
      .extendedDynamicState3ExtraPrimitiveOverestimationSize = false,
      .extendedDynamicState3DepthClipEnable = false,
      .extendedDynamicState3SampleLocationsEnable = false,
      .extendedDynamicState3ColorBlendAdvanced = false,
      .extendedDynamicState3ProvokingVertexMode = false,
      .extendedDynamicState3LineRasterizationMode = false,
      .extendedDynamicState3LineStippleEnable = false,
      .extendedDynamicState3DepthClipNegativeOneToOne = false,
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

      /* Vulkan 1.2 / VK_EXT_host_query_reset */
      .hostQueryReset = true,

      /* VK_EXT_image_2d_view_of_3d */
      .image2DViewOf3D = true,
      .sampler2DViewOf3D = true,

      /* VK_EXT_map_memory_placed */
      .memoryMapPlaced = true,
      .memoryMapRangePlaced = false,
      .memoryUnmapReserve = true,

      /* Vulkan 1.3 / VK_EXT_private_data */
      .privateData = true,

      /* VK_EXT_provoking_vertex */
      .provokingVertexLast = true,
      .transformFeedbackPreservesProvokingVertex = false,

      /* Vulkan 1.2 / VK_EXT_scalar_block_layout */
      .scalarBlockLayout = true,

      /* Vulkan 1.3 / VK_EXT_texel_buffer_alignment */
      .texelBufferAlignment = true,

      /* Vulkan 1.2 / VK_KHR_buffer_device_address */
      .bufferDeviceAddress = true,
      .bufferDeviceAddressCaptureReplay = false,
      .bufferDeviceAddressMultiDevice = false,

      /* VK_KHR_shader_expect_assume */
      .shaderExpectAssume = false,

      /* VK_EXT_shader_demote_to_helper_invocation */
      .shaderDemoteToHelperInvocation = true,

      /* VK_EXT_shader_replicated_composites */
      .shaderReplicatedComposites = true,

      /* VK_KHR_shader_terminate_invocation */
      .shaderTerminateInvocation = true,

      /* VK_KHR_present_id2 */
      .presentId2 = PVR_USE_WSI_PLATFORM,

      /* VK_KHR_present_wait2 */
      .presentWait2 = PVR_USE_WSI_PLATFORM,

      /* Vulkan 1.4 / VK_EXT_vertex_attribute_divisor /
         VK_KHR_vertex_attribute_divisor */
      .vertexAttributeInstanceRateDivisor = true,
      .vertexAttributeInstanceRateZeroDivisor = true,

      /* Vulkan 1.3 / VK_KHR_zero_initialize_workgroup_memory */
      .shaderZeroInitializeWorkgroupMemory = false,

      /* VK_EXT_border_color_swizzle */
      .borderColorSwizzle = true,
      .borderColorSwizzleFromImage = true,

      /* VK_EXT_custom_border_color */
      .customBorderColors = true,

      /* VK_EXT_depth_clip_enable */
      .depthClipEnable = true,

      /* VK_KHR_line_rasterization */
      .bresenhamLines = true,

      /* VK_EXT_zero_initialize_device_memory */
      .zeroInitializeDeviceMemory = true,
   };
}

static uint32_t get_api_version(void)
{
   const uint32_t version_override = vk_get_version_override();
   if (version_override)
      return version_override;

   return VK_MAKE_API_VERSION(0, 1, 2, VK_HEADER_VERSION);
}

static unsigned
get_custom_border_color_samplers(const struct pvr_device_info *dev_info);

static bool pvr_physical_device_get_properties(
   const struct pvr_physical_device *const pdevice,
   struct vk_properties *const properties)
{
   const struct pvr_device_info *const dev_info = &pdevice->dev_info;
   const struct pvr_device_runtime_info *dev_runtime_info =
      &pdevice->dev_runtime_info;

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t max_multisample =
      PVR_GET_FEATURE_VALUE(dev_info, max_multisample, 4);

   UNUSED const uint32_t sub_pixel_precision =
      PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format) ? 4U : 8U;

   UNUSED const uint32_t max_render_size = rogue_get_render_size_max(dev_info);

   UNUSED const uint32_t max_sample_bits = ((max_multisample << 1) - 1);

   UNUSED const uint32_t max_user_vertex_components =
      pvr_get_max_user_vertex_output_components(dev_info);

   const bool usc_alu_roundingmode_rne =
      PVR_HAS_FEATURE(dev_info, usc_alu_roundingmode_rne);

   /* The workgroup invocations are limited by the case where we have a compute
    * barrier - each slot has a fixed number of invocations, the whole workgroup
    * may need to span multiple slots. As each slot will WAIT at the barrier
    * until the last invocation completes, all have to be schedulable at the
    * same time.
    *
    * Typically all Rogue cores have 16 slots. Some of the smallest cores are
    * reduced to 14.
    *
    * The compute barrier slot exhaustion scenario can be tested with:
    * dEQP-VK.memory_model.message_passing*u32.coherent.fence_fence
    *    .atomicwrite*guard*comp
    */

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t usc_slots = PVR_GET_FEATURE_VALUE(dev_info, usc_slots, 14);

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t max_instances_per_pds_task =
      PVR_GET_FEATURE_VALUE(dev_info, max_instances_per_pds_task, 32U);

   UNUSED const uint32_t max_compute_work_group_invocations =
      (usc_slots * max_instances_per_pds_task >= 512U) ? 512U : 384U;

   assert(pdevice->memory.memoryHeapCount == 1);
   const VkDeviceSize max_memory_alloc_size =
      pdevice->memory.memoryHeaps[0].size;

   const uint32_t line_sub_pixel_precision_bits =
      PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format) ? 4U : 8U;

   *properties = (struct vk_properties){
      /* Vulkan 1.0 */
      .apiVersion = get_api_version(),
      .driverVersion = vk_get_driver_version(),
      .vendorID = VK_VENDOR_ID_IMAGINATION,
      .deviceID = dev_info->ident.device_id,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
      /* deviceName and pipelineCacheUUID are filled below .*/

      .maxImageDimension1D = 4096U,
      .maxImageDimension2D = 4096U,
      .maxImageDimension3D = 256U,
      .maxImageDimensionCube = 4096U,
      .maxImageArrayLayers = 256U,
      .maxTexelBufferElements = 64U * 1024U,
      .maxUniformBufferRange = 16U * 1024U,
      .maxStorageBufferRange = 128U * 1024U * 1024U,
      .maxPushConstantsSize = 128U,
      .maxMemoryAllocationCount = 4096U,
      .maxSamplerAllocationCount = 4000U,
      .bufferImageGranularity = 1U,
      .sparseAddressSpaceSize = 0U, /* Requires sparseBinding */
      .maxBoundDescriptorSets = 4U,
      .maxPerStageDescriptorSamplers = 16,
      .maxPerStageDescriptorUniformBuffers = 12,
      .maxPerStageDescriptorStorageBuffers = 4,
      .maxPerStageDescriptorSampledImages = 16,
      .maxPerStageDescriptorStorageImages = 4,
      .maxPerStageDescriptorInputAttachments = 4,
      .maxPerStageResources = 44,
      .maxDescriptorSetSamplers = 3U * 16U,
      .maxDescriptorSetUniformBuffers = 3U * 12U,
      .maxDescriptorSetUniformBuffersDynamic = 8U,
      .maxDescriptorSetStorageBuffers = 3U * 4U,
      .maxDescriptorSetStorageBuffersDynamic = 4U,
      .maxDescriptorSetSampledImages = 3U * 16U,
      .maxDescriptorSetStorageImages = 3U * 4U,
      .maxDescriptorSetInputAttachments = 4U,

      /* Vertex Shader Limits */
      .maxVertexInputAttributes = 16U,
      .maxVertexInputBindings = 16U,
      .maxVertexInputAttributeOffset = 2048U - 1U,
      .maxVertexInputBindingStride = 2048U,
      .maxVertexOutputComponents = 64U,

      /* Tessellation Limits */
      /* Requires tessellationShader */
      .maxTessellationGenerationLevel = 0U,
      .maxTessellationPatchSize = 0U,
      .maxTessellationControlPerVertexInputComponents = 0U,
      .maxTessellationControlPerVertexOutputComponents = 0U,
      .maxTessellationControlPerPatchOutputComponents = 0U,
      .maxTessellationControlTotalOutputComponents = 0U,
      .maxTessellationEvaluationInputComponents = 0U,
      .maxTessellationEvaluationOutputComponents = 0U,

      /* Geometry Shader Limits */
      /* Requires geometryShader */
      .maxGeometryShaderInvocations = 0U,
      .maxGeometryInputComponents = 0U,
      .maxGeometryOutputComponents = 0U,
      .maxGeometryOutputVertices = 0U,
      .maxGeometryTotalOutputComponents = 0U,

      /* Fragment Shader Limits */
      .maxFragmentInputComponents = 64U,
      .maxFragmentOutputAttachments = 4U,
      .maxFragmentDualSrcAttachments = 0U, /* Requires dualSrcBlend */
      .maxFragmentCombinedOutputResources = 4U,

      /* Compute Shader Limits */
      .maxComputeSharedMemorySize = 16U * 1024U,
      .maxComputeWorkGroupCount = {
         [0] = (64U * 1024U) - 1,
         [1] = (64U * 1024U) - 1,
         [2] = (64U * 1024U) - 1,
      },
      .maxComputeWorkGroupInvocations = 128U,
      .maxComputeWorkGroupSize = {
        [0] = 128U,
        [1] = 128U,
        [2] = 64U,
      },

      /* Rasterization Limits */
      .subPixelPrecisionBits = 4U,
      .subTexelPrecisionBits = 8U,
      .mipmapPrecisionBits = 8U,

      .maxDrawIndexedIndexValue = (1U << 24) - 1U, /* Requires fullDrawIndexUint32 */
      .maxDrawIndirectCount = 1U, /* Requires multiDrawIndirect */
      .maxSamplerLodBias = 16.0f,
      .maxSamplerAnisotropy = 16.0f, /* Requires samplerAnisotropy */
      .maxViewports = 1U, /* Requires multiViewport */

      .maxViewportDimensions[0] = 4096U,
      .maxViewportDimensions[1] = 4096U,
      .viewportBoundsRange[0] = -8192.0f,
      .viewportBoundsRange[1] = 8191.0f,

      .viewportSubPixelBits = 0U,
      .minMemoryMapAlignment = pdevice->ws->page_size,
      .minTexelBufferOffsetAlignment = PVR_TEXEL_BUFFER_OFFSET_ALIGNMENT,
      .minUniformBufferOffsetAlignment = PVR_UNIFORM_BUFFER_OFFSET_ALIGNMENT,
      .minStorageBufferOffsetAlignment = PVR_STORAGE_BUFFER_OFFSET_ALIGNMENT,

      .minTexelOffset = -8,
      .maxTexelOffset = 7U,

      /* Requires shaderImageGatherExtended */
      .minTexelGatherOffset = 0,
      .maxTexelGatherOffset = 0U,

      /* Requires sampleRateShading */
      .minInterpolationOffset = -0.5f,
      .maxInterpolationOffset = 0.5f,
      .subPixelInterpolationOffsetBits = 4U,

      .maxFramebufferWidth = 4096U,
      .maxFramebufferHeight = 4096U,
      .maxFramebufferLayers = 256U,

      .framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .maxColorAttachments = 4U,
      .sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,
      .storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT, /* Requires shaderStorageImageMultisample */
      .maxSampleMaskWords = 1U,
      .timestampComputeAndGraphics = false,
      .timestampPeriod = 0.0f,

      .maxClipDistances = PVR_MAX_USER_PLANES,
      .maxCullDistances = PVR_MAX_USER_PLANES,
      .maxCombinedClipAndCullDistances = PVR_MAX_USER_PLANES,

      .discreteQueuePriorities = 2U,

      .pointSizeRange[0] = PVR_POINT_SIZE_RANGE_MIN,
      .pointSizeRange[1] = PVR_POINT_SIZE_RANGE_MAX,
      .pointSizeGranularity = PVR_POINT_SIZE_GRANULARITY,

      /* Requires wideLines */
      .lineWidthRange[0] = 1.0f,
      .lineWidthRange[1] = 1.0f,
      .lineWidthGranularity = 0.0f,

      .strictLines = false,
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = PVR_STORAGE_BUFFER_OFFSET_ALIGNMENT,
      .optimalBufferCopyRowPitchAlignment = PVR_STORAGE_BUFFER_OFFSET_ALIGNMENT,
      .nonCoherentAtomSize = 1U,

      /* Vulkan 1.1 */
      .subgroupSize = 1,
      .subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT,
      .subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT,
      .subgroupQuadOperationsInAllStages = false,
      .protectedNoFault = false,

      /* Vulkan 1.2 */
      .maxUpdateAfterBindDescriptorsInAllPools = 0,
      .shaderUniformBufferArrayNonUniformIndexingNative = false,
      .shaderSampledImageArrayNonUniformIndexingNative = false,
      .shaderStorageBufferArrayNonUniformIndexingNative = false,
      .shaderStorageImageArrayNonUniformIndexingNative = false,
      .shaderInputAttachmentArrayNonUniformIndexingNative = false,
      .robustBufferAccessUpdateAfterBind = false,
      .quadDivergentImplicitLod = false,
      .maxPerStageDescriptorUpdateAfterBindSamplers = 0,
      .maxPerStageDescriptorUpdateAfterBindUniformBuffers = 0,
      .maxPerStageDescriptorUpdateAfterBindStorageBuffers = 0,
      .maxPerStageDescriptorUpdateAfterBindSampledImages = 0,
      .maxPerStageDescriptorUpdateAfterBindStorageImages = 0,
      .maxPerStageDescriptorUpdateAfterBindInputAttachments = 0,
      .maxPerStageUpdateAfterBindResources = 0,
      .maxDescriptorSetUpdateAfterBindSamplers = 0,
      .maxDescriptorSetUpdateAfterBindUniformBuffers = 0,
      .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 0,
      .maxDescriptorSetUpdateAfterBindStorageBuffers = 0,
      .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 0,
      .maxDescriptorSetUpdateAfterBindSampledImages = 0,
      .maxDescriptorSetUpdateAfterBindStorageImages = 0,
      .maxDescriptorSetUpdateAfterBindInputAttachments = 0,
      .filterMinmaxSingleComponentFormats = false,
      .filterMinmaxImageComponentMapping = false,
      .framebufferIntegerColorSampleCounts =
         VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT,

      /* Vulkan 1.0 / VK_KHR_maintenance2 */
      .pointClippingBehavior =
         VK_POINT_CLIPPING_BEHAVIOR_USER_CLIP_PLANES_ONLY,

      /* Vulkan 1.1 / VK_KHR_maintenance3 */
      .maxPerSetDescriptors = PVR_MAX_DESCRIPTORS_PER_SET,
      .maxMemoryAllocationSize = max_memory_alloc_size,

      /* Vulkan 1.1 / VK_KHR_multiview */
      .maxMultiviewViewCount = PVR_MAX_MULTIVIEW,
      .maxMultiviewInstanceIndex = (1 << 27) - 1,

      /* Vulkan 1.2 / VK_KHR_driver_properties */
      .driverID = VK_DRIVER_ID_IMAGINATION_OPEN_SOURCE_MESA,
      .driverName = "Imagination open-source Mesa driver",
      .driverInfo = "Mesa " PACKAGE_VERSION MESA_GIT_SHA1,
      .conformanceVersion = {
         .major = 1,
         .minor = 3,
         .subminor = 8,
         .patch = 4,
      },

      /* VK_EXT_extended_dynamic_state3 */
      .dynamicPrimitiveTopologyUnrestricted = false,

      /* VK_EXT_map_memory_placed */
      .minPlacedMemoryMapAlignment = pdevice->ws->page_size,

      /* VK_EXT_provoking_vertex */
      .provokingVertexModePerPipeline = true,
      .transformFeedbackPreservesTriangleFanProvokingVertex = false,

      /* Vulkan 1.1 / VK_KHR_robustness2 */
      .robustStorageBufferAccessSizeAlignment = PVR_STORAGE_BUFFER_OFFSET_ALIGNMENT,
      .robustUniformBufferAccessSizeAlignment = PVR_STORAGE_BUFFER_OFFSET_ALIGNMENT,

      /* Vulkan 1.2 / VK_KHR_shader_float_controls */
      .denormBehaviorIndependence =
         VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_32_BIT_ONLY,
      .roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE,
      .shaderSignedZeroInfNanPreserveFloat16 = true,
      .shaderSignedZeroInfNanPreserveFloat32 = true,
      .shaderSignedZeroInfNanPreserveFloat64 = true,
      .shaderDenormPreserveFloat16 = true,
      .shaderDenormPreserveFloat32 = false,
      .shaderDenormPreserveFloat64 = true,
      .shaderDenormFlushToZeroFloat16 = false,
      .shaderDenormFlushToZeroFloat32 = false,
      .shaderDenormFlushToZeroFloat64 = false,
      .shaderRoundingModeRTEFloat16 = usc_alu_roundingmode_rne,
      .shaderRoundingModeRTEFloat32 = usc_alu_roundingmode_rne,
      .shaderRoundingModeRTEFloat64 = usc_alu_roundingmode_rne,
      .shaderRoundingModeRTZFloat16 = !usc_alu_roundingmode_rne,
      .shaderRoundingModeRTZFloat32 = !usc_alu_roundingmode_rne,
      .shaderRoundingModeRTZFloat64 = !usc_alu_roundingmode_rne,

      /* Vulkan 1.2 / VK_KHR_timeline_semaphore */
      .maxTimelineSemaphoreValueDifference = UINT64_MAX,

      /* Vulkan 1.3 / VK_EXT_texel_buffer_alignment */
      .storageTexelBufferOffsetAlignmentBytes = PVR_TEXEL_BUFFER_OFFSET_ALIGNMENT,
      .storageTexelBufferOffsetSingleTexelAlignment = true,
      .uniformTexelBufferOffsetAlignmentBytes = PVR_TEXEL_BUFFER_OFFSET_ALIGNMENT,
      .uniformTexelBufferOffsetSingleTexelAlignment = false,

      /* Vulkan 1.4 / VK_EXT_vertex_attribute_divisor / VK_KHR_vertex_attribute_divisor */
      .maxVertexAttribDivisor = UINT32_MAX,
      .supportsNonZeroFirstInstance = true,

      /* VK_EXT_custom_border_color */
      .maxCustomBorderColorSamplers =
         get_custom_border_color_samplers(&pdevice->dev_info),

      /* VkPhysicalDeviceDrmPropertiesEXT */
      .drmHasPrimary = true,
      .drmPrimaryMajor = (int64_t) major(pdevice->primary_devid),
      .drmPrimaryMinor = (int64_t) minor(pdevice->primary_devid),
      .drmHasRender = true,
      .drmRenderMajor = (int64_t) major(pdevice->render_devid),
      .drmRenderMinor = (int64_t) minor(pdevice->render_devid),

      /* Vulkan 1.2 / VK_KHR_depth_stencil_resolve */
      .supportedDepthResolveModes =
         VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
      .supportedStencilResolveModes =
         VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
      .independentResolveNone = true,
      .independentResolve = true,

      /* VK_KHR_line_rasterization */
      .lineSubPixelPrecisionBits = line_sub_pixel_precision_bits,
   };

   if (PVR_HAS_FEATURE(dev_info, gpu_multicore_support)) {
      snprintf(properties->deviceName,
               sizeof(properties->deviceName),
               "PowerVR %s %s MC%u",
               dev_info->ident.series_name,
               dev_info->ident.public_name,
               dev_runtime_info->core_count);
   } else {
      snprintf(properties->deviceName,
               sizeof(properties->deviceName),
               "PowerVR %s %s",
               dev_info->ident.series_name,
               dev_info->ident.public_name);
   }

   return true;
}

static bool pvr_physical_device_setup_pipeline_cache(
   struct pvr_physical_device *const pdevice)
{
#ifdef ENABLE_SHADER_CACHE
   const struct pvr_instance *instance = pdevice->instance;
   char device_id[SHA1_DIGEST_LENGTH * 2 + 1];
   char driver_id[SHA1_DIGEST_LENGTH * 2 + 1];

   _mesa_sha1_format(device_id, pdevice->device_uuid);
   _mesa_sha1_format(driver_id, instance->driver_build_sha);

   pdevice->vk.disk_cache = disk_cache_create(device_id, driver_id, 0U);
   return !!pdevice->vk.disk_cache;
#else
   return true;
#endif /* ENABLE_SHADER_CACHE */
}

static void
pvr_get_device_uuid(const struct pvr_device_info *dev_info,
                    uint8_t uuid_out[const static SHA1_DIGEST_LENGTH])
{
   uint64_t bvnc = pvr_get_packed_bvnc(dev_info);
   static const char *device_str = "pvr";
   struct mesa_sha1 sha1_ctx;

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, device_str, strlen(device_str));
   _mesa_sha1_update(&sha1_ctx, &bvnc, sizeof(bvnc));
   _mesa_sha1_final(&sha1_ctx, uuid_out);
}

static void
pvr_get_cache_uuid(const struct pvr_physical_device *const pdevice,
                   uint8_t uuid_out[const static SHA1_DIGEST_LENGTH])
{
   const struct pvr_instance *instance = pdevice->instance;
   static const char *cache_str = "cache";
   struct mesa_sha1 sha1_ctx;

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, cache_str, strlen(cache_str));
   _mesa_sha1_update(&sha1_ctx,
                     pdevice->device_uuid,
                     sizeof(pdevice->device_uuid));
   _mesa_sha1_update(&sha1_ctx,
                     instance->driver_build_sha,
                     sizeof(instance->driver_build_sha));
   _mesa_sha1_final(&sha1_ctx, uuid_out);
}

static void
pvr_physical_device_setup_uuids(struct pvr_physical_device *const pdevice)
{
   const struct pvr_instance *instance = pdevice->instance;

   pvr_get_device_uuid(&pdevice->dev_info, pdevice->device_uuid);
   pvr_get_cache_uuid(pdevice, pdevice->cache_uuid);

   memcpy(pdevice->vk.properties.driverUUID,
          instance->driver_build_sha,
          sizeof(pdevice->vk.properties.driverUUID));

   memcpy(pdevice->vk.properties.deviceUUID,
          pdevice->device_uuid,
          sizeof(pdevice->vk.properties.deviceUUID));

   memcpy(pdevice->vk.properties.pipelineCacheUUID,
          pdevice->cache_uuid,
          sizeof(pdevice->vk.properties.pipelineCacheUUID));

   memcpy(pdevice->vk.properties.shaderBinaryUUID,
          pdevice->cache_uuid,
          sizeof(pdevice->vk.properties.shaderBinaryUUID));
}

static bool pvr_device_is_conformant(const struct pvr_device_info *info)
{
   const uint64_t bvnc = pvr_get_packed_bvnc(info);
   switch (bvnc) {
   case PVR_BVNC_PACK(36, 53, 104, 796):
      return true;

   default:
      break;
   }

   return false;
}

/* Minimum required by the Vulkan 1.1 spec (see Table 32. Required Limits) */
#define PVR_MAX_MEMORY_ALLOCATION_SIZE (1ull << 30)

static uint64_t pvr_compute_heap_size(void)
{
   /* Query the total ram from the system */
   uint64_t total_ram;
   if (!os_get_total_physical_memory(&total_ram))
      return 0;

   if (total_ram < PVR_MAX_MEMORY_ALLOCATION_SIZE) {
      mesa_logw(
         "Warning: The available RAM is below the minimum required by the Vulkan specification!");
   }

   /* We don't want to burn too much ram with the GPU. If the user has 4GiB
    * or less, we use at most half. If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ULL * 1024ULL * 1024ULL * 1024ULL)
      available_ram = total_ram / 2U;
   else
      available_ram = total_ram * 3U / 4U;

   return MAX2(available_ram, PVR_MAX_MEMORY_ALLOCATION_SIZE);
}

VkResult pvr_physical_device_init(struct pvr_physical_device *pdevice,
                                  struct pvr_instance *instance,
                                  drmDevicePtr drm_render_device,
                                  drmDevicePtr drm_display_device)
{
   struct vk_physical_device_dispatch_table dispatch_table;
   struct vk_device_extension_table supported_extensions;
   struct vk_properties supported_properties;
   struct vk_features supported_features;
   struct pvr_winsys *ws;
   struct stat primary_stat = { 0 }, render_stat = { 0 };
   char *primary_path;
   char *display_path;
   char *render_path;
   VkResult result;

   render_path = vk_strdup(&instance->vk.alloc,
                           drm_render_device->nodes[DRM_NODE_RENDER],
                           VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!render_path) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto err_out;
   }

   if (instance->vk.enabled_extensions.KHR_display && drm_display_device) {
      display_path = vk_strdup(&instance->vk.alloc,
                               drm_display_device->nodes[DRM_NODE_PRIMARY],
                               VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      if (!display_path) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto err_vk_free_render_path;
      }
   } else {
      display_path = NULL;
   }

   primary_path = drm_render_device->nodes[DRM_NODE_PRIMARY];
   if (stat(primary_path, &primary_stat) != 0) {
      result = vk_errorf(instance,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "failed to stat DRM primary node %s",
                         primary_path);
      goto err_vk_free_display_path;
   }
   pdevice->primary_devid = primary_stat.st_rdev;

   if (stat(render_path, &render_stat) != 0) {
      result = vk_errorf(instance,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "failed to stat DRM render node %s",
                         render_path);
      goto err_vk_free_display_path;
   }
   pdevice->render_devid = render_stat.st_rdev;

   result =
      pvr_winsys_create(render_path, display_path, &instance->vk.alloc, &ws);
   if (result != VK_SUCCESS)
      goto err_vk_free_display_path;

   pdevice->instance = instance;
   pdevice->render_path = render_path;
   pdevice->display_path = display_path;
   pdevice->ws = ws;

   result = ws->ops->device_info_init(ws,
                                      &pdevice->dev_info,
                                      &pdevice->dev_runtime_info);
   if (result != VK_SUCCESS)
      goto err_pvr_winsys_destroy;

   if (!pvr_device_is_conformant(&pdevice->dev_info)) {
      if (!os_get_option("PVR_I_WANT_A_BROKEN_VULKAN_DRIVER")) {
         result = vk_errorf(instance,
                            VK_ERROR_INCOMPATIBLE_DRIVER,
                            "WARNING: powervr is not a conformant Vulkan "
                            "implementation for %s. Pass "
                            "PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1 if you know "
                            "what you're doing.",
                            pdevice->dev_info.ident.public_name);
         goto err_pvr_winsys_destroy;
      }

      vk_warn_non_conformant_implementation("powervr");
   }

   /* Setup available memory heaps and types */
   pdevice->memory.memoryHeapCount = 1;
   pdevice->memory.memoryHeaps[0].size = pvr_compute_heap_size();
   pdevice->memory.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   pdevice->memory.memoryTypeCount = 1;
   pdevice->memory.memoryTypes[0].propertyFlags =
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   pdevice->memory.memoryTypes[0].heapIndex = 0;

   pvr_physical_device_get_supported_extensions(&supported_extensions);
   pvr_physical_device_get_supported_features(&pdevice->dev_info,
                                              &supported_features);
   if (!pvr_physical_device_get_properties(pdevice, &supported_properties)) {
      result = vk_errorf(instance,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to collect physical device properties");
      goto err_pvr_winsys_destroy;
   }

   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table,
      &pvr_physical_device_entrypoints,
      true);

   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table,
      &wsi_physical_device_entrypoints,
      false);

   result = vk_physical_device_init(&pdevice->vk,
                                    &instance->vk,
                                    &supported_extensions,
                                    &supported_features,
                                    &supported_properties,
                                    &dispatch_table);
   if (result != VK_SUCCESS)
      goto err_pvr_winsys_destroy;

   pvr_physical_device_setup_uuids(pdevice);

   if (!pvr_physical_device_setup_pipeline_cache(pdevice)) {
      result = vk_errorf(NULL,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to get driver build sha.");
      goto err_vk_physical_device_finish;
   }

   pdevice->vk.supported_sync_types = ws->sync_types;

   pdevice->pco_ctx = pco_ctx_create(&pdevice->dev_info, NULL);
   if (!pdevice->pco_ctx) {
      result = vk_errorf(instance,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to initialize PCO compiler context");
      goto err_free_pipeline_cache;
   }
   pco_ctx_setup_usclib(pdevice->pco_ctx,
                        pco_usclib_0_nir,
                        sizeof(pco_usclib_0_nir));

   result = pvr_wsi_init(pdevice);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto err_free_pco_ctx;
   }

   return VK_SUCCESS;

err_free_pco_ctx:
   ralloc_free(pdevice->pco_ctx);

err_free_pipeline_cache:
   pvr_physical_device_free_pipeline_cache(pdevice);

err_vk_physical_device_finish:
   vk_physical_device_finish(&pdevice->vk);

err_pvr_winsys_destroy:
   pvr_winsys_destroy(ws);

err_vk_free_display_path:
   vk_free(&instance->vk.alloc, display_path);

err_vk_free_render_path:
   vk_free(&instance->vk.alloc, render_path);

err_out:
   return result;
}

const static VkQueueFamilyProperties pvr_queue_family_properties = {
   .queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT |
                 VK_QUEUE_TRANSFER_BIT,
   .queueCount = PVR_MAX_QUEUES,
   .timestampValidBits = 0,
   .minImageTransferGranularity = { 1, 1, 1 },
};

void pvr_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice,
   uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2,
                          out,
                          pQueueFamilyProperties,
                          pQueueFamilyPropertyCount);

   vk_outarray_append_typed (VkQueueFamilyProperties2, &out, p) {
      p->queueFamilyProperties = pvr_queue_family_properties;

      vk_foreach_struct (ext, p->pNext) {
         vk_debug_ignored_stype(ext->sType);
      }
   }
}

void pvr_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   VK_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);

   pMemoryProperties->memoryProperties = pdevice->memory;

   vk_foreach_struct (ext, pMemoryProperties->pNext) {
      vk_debug_ignored_stype(ext->sType);
   }
}

VkResult pvr_CreateDevice(VkPhysicalDevice physicalDevice,
                          const VkDeviceCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkDevice *pDevice)
{
   VK_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);
   return pvr_create_device(pdevice, pCreateInfo, pAllocator, pDevice);
}

void pvr_DestroyDevice(VkDevice _device,
                       const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);

   pvr_destroy_device(device, pAllocator);
}

/* Leave this at the very end, to avoid leakage of HW-defs here */
#include "pvr_border.h"

static unsigned
get_custom_border_color_samplers(const struct pvr_device_info *dev_info)
{
   assert(dev_info->ident.arch == PVR_DEVICE_ARCH_ROGUE);
   return PVR_BORDER_COLOR_TABLE_NR_CUSTOM_ENTRIES;
}
