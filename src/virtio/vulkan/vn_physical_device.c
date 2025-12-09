/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_physical_device.h"

#include <stdio.h>

#include "git_sha1.h"
#include "util/mesa-sha1.h"
#include "util/os_misc.h"
#include "venus-protocol/vn_protocol_driver_device.h"
#include "vk_android.h"
#include "vk_common_entrypoints.h"

#include "vn_android.h"
#include "vn_instance.h"

#define IMAGE_FORMAT_CACHE_MAX_ENTRIES 100

/** Add `elem` to the pNext chain of `head`. */
#define VN_ADD_PNEXT(head, s_type, elem)                                     \
   do {                                                                      \
      (elem).sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_##s_type;             \
      (elem).pNext = (head).pNext;                                           \
      (head).pNext = &(elem);                                                \
   } while (0)

/**
 * If the renderer supports the extension, add `elem` to the pNext chain of
 * `head`.
 */
#define VN_ADD_PNEXT_EXT(head, s_type, elem, ext_cond)                       \
   do {                                                                      \
      if (ext_cond)                                                          \
         VN_ADD_PNEXT((head), s_type, (elem));                               \
   } while (0)

/**
 * Copy vk struct members to common vk properties.
 */
#define VN_SET_VK_PROPS(vk_props, vk_struct)                                 \
   do {                                                                      \
      vk_set_physical_device_properties_struct(                              \
         (vk_props), (const VkBaseInStructure *)(vk_struct));                \
   } while (0)

/**
 * Copy vk struct members to common vk properties if extension is supported.
 */
#define VN_SET_VK_PROPS_EXT(vk_props, vk_struct, ext_cond)                   \
   do {                                                                      \
      if (ext_cond)                                                          \
         VN_SET_VK_PROPS(vk_props, vk_struct);                               \
   } while (0)

/**
 * Copy vk struct without sType or pNext.
 */
static inline void
_vn_copy_struct_guts(VkBaseOutStructure *dst,
                     const VkBaseInStructure *src,
                     size_t struct_size)
{
   STATIC_ASSERT(sizeof(*dst) == sizeof(*src));
   memcpy(dst + 1, src + 1, struct_size - sizeof(VkBaseOutStructure));
}

#define VN_COPY_STRUCT_GUTS(_dst, _src, _struct_size)                        \
   do {                                                                      \
      _vn_copy_struct_guts((VkBaseOutStructure *)(_dst),                     \
                           (const VkBaseInStructure *)(_src),                \
                           (_struct_size));                                  \
   } while (0)

static void
vn_physical_device_init_features(struct vn_physical_device *physical_dev)
{
   const uint32_t renderer_version = physical_dev->renderer_version;
   const struct vk_device_extension_table *exts =
      &physical_dev->renderer_extensions;
   struct vn_ring *ring = physical_dev->instance->ring.ring;
   VkPhysicalDeviceFeatures2 feats2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   struct {
      VkPhysicalDeviceFeatures vulkan_1_0;
      VkPhysicalDeviceVulkan11Features vulkan_1_1;
      VkPhysicalDeviceVulkan12Features vulkan_1_2;
      VkPhysicalDeviceVulkan13Features vulkan_1_3;
      VkPhysicalDeviceVulkan14Features vulkan_1_4;

      /* Vulkan 1.1 */
      VkPhysicalDevice16BitStorageFeatures _16bit_storage;
      VkPhysicalDeviceMultiviewFeatures multiview;
      VkPhysicalDeviceVariablePointersFeatures variable_pointers;
      VkPhysicalDeviceProtectedMemoryFeatures protected_memory;
      VkPhysicalDeviceSamplerYcbcrConversionFeatures sampler_ycbcr_conversion;
      VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters;

      /* Vulkan 1.2 */
      VkPhysicalDevice8BitStorageFeatures _8bit_storage;
      VkPhysicalDeviceShaderAtomicInt64Features shader_atomic_int64;
      VkPhysicalDeviceShaderFloat16Int8Features shader_float16_int8;
      VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing;
      VkPhysicalDeviceScalarBlockLayoutFeatures scalar_block_layout;
      VkPhysicalDeviceImagelessFramebufferFeatures imageless_framebuffer;
      VkPhysicalDeviceUniformBufferStandardLayoutFeatures
         uniform_buffer_standard_layout;
      VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures
         shader_subgroup_extended_types;
      VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures
         separate_depth_stencil_layouts;
      VkPhysicalDeviceHostQueryResetFeatures host_query_reset;
      VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore;
      VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address;
      VkPhysicalDeviceVulkanMemoryModelFeatures vulkan_memory_model;

      /* Vulkan 1.3 */
      VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering;
      VkPhysicalDeviceImageRobustnessFeatures image_robustness;
      VkPhysicalDeviceInlineUniformBlockFeatures inline_uniform_block;
      VkPhysicalDeviceMaintenance4Features maintenance_4;
      VkPhysicalDevicePipelineCreationCacheControlFeatures
         pipeline_creation_cache_control;
      VkPhysicalDevicePrivateDataFeatures private_data;
      VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures
         shader_demote_to_helper_invocation;
      VkPhysicalDeviceShaderIntegerDotProductFeatures
         shader_integer_dot_product;
      VkPhysicalDeviceShaderTerminateInvocationFeatures
         shader_terminate_invocation;
      VkPhysicalDeviceSynchronization2Features synchronization2;
      VkPhysicalDeviceSubgroupSizeControlFeatures subgroup_size_control;
      VkPhysicalDeviceTextureCompressionASTCHDRFeatures
         texture_compression_astc_hdr;
      VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures
         zero_initialize_workgroup_memory;

      /* Vulkan 1.3: The extensions for the below structs were promoted, but
       * some struct members were omitted from
       * VkPhysicalDeviceVulkan13Features.
       */
      VkPhysicalDevice4444FormatsFeaturesEXT _4444_formats;
      VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state;
      VkPhysicalDeviceExtendedDynamicState2FeaturesEXT
         extended_dynamic_state_2;
      VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT texel_buffer_alignment;
      VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT
         ycbcr_2plane_444_formats;

      /* Vulkan 1.4 */
      VkPhysicalDeviceDynamicRenderingLocalReadFeatures
         dynamic_rendering_local_read;
      VkPhysicalDeviceGlobalPriorityQueryFeatures global_priority_query;
      VkPhysicalDeviceHostImageCopyFeatures host_image_copy;
      VkPhysicalDeviceIndexTypeUint8Features index_type_uint8;
      VkPhysicalDeviceLineRasterizationFeatures line_rasterization;
      VkPhysicalDeviceMaintenance5Features maintenance_5;
      VkPhysicalDeviceMaintenance6Features maintenance_6;
      VkPhysicalDevicePipelineProtectedAccessFeatures
         pipeline_protected_access;
      VkPhysicalDevicePipelineRobustnessFeatures pipeline_robustness;
      VkPhysicalDeviceShaderExpectAssumeFeatures shader_expect_assume;
      VkPhysicalDeviceShaderFloatControls2Features shader_float_controls_2;
      VkPhysicalDeviceShaderSubgroupRotateFeatures shader_subgroup_rotate;
      VkPhysicalDeviceVertexAttributeDivisorFeatures vertex_attribute_divisor;

      /* KHR */
      VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure;
      VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR
         compute_shader_derivatives;
      VkPhysicalDeviceCooperativeMatrixFeaturesKHR cooperative_matrix;
      VkPhysicalDeviceDepthClampZeroOneFeaturesKHR depth_clamp_zero_one;
      VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR
         fragment_shader_barycentric;
      VkPhysicalDeviceFragmentShadingRateFeaturesKHR fragment_shading_rate;
      VkPhysicalDeviceMaintenance7FeaturesKHR maintenance_7;
      VkPhysicalDeviceRayQueryFeaturesKHR ray_query;
      VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR
         ray_tracing_maintenance_1;
      VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline;
      VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR
         ray_tracing_position_fetch;
      VkPhysicalDeviceRobustness2FeaturesKHR robustness_2;
      VkPhysicalDeviceShaderBfloat16FeaturesKHR shader_bfloat16;
      VkPhysicalDeviceShaderClockFeaturesKHR shader_clock;
      VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR
         shader_maximal_reconvergence;
      VkPhysicalDeviceShaderQuadControlFeaturesKHR shader_quad_control;
      VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR
         shader_relaxed_extended_instruction;
      VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR
         shader_subgroup_uniform_control_flow;
      VkPhysicalDeviceShaderUntypedPointersFeaturesKHR shader_untyped_pointers;
      VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR
         workgroup_memory_explicit_layout;

      /* EXT */
      VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT
         attachment_feedback_loop_dynamic_state;
      VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT
         attachment_feedback_loop_layout;
      VkPhysicalDeviceBlendOperationAdvancedFeaturesEXT
         blend_operation_advanced;
      VkPhysicalDeviceBorderColorSwizzleFeaturesEXT border_color_swizzle;
      VkPhysicalDeviceBufferDeviceAddressFeaturesEXT buffer_device_address_ext;
      VkPhysicalDeviceColorWriteEnableFeaturesEXT color_write_enable;
      VkPhysicalDeviceConditionalRenderingFeaturesEXT conditional_rendering;
      VkPhysicalDeviceCustomBorderColorFeaturesEXT custom_border_color;
      VkPhysicalDeviceDepthBiasControlFeaturesEXT depth_bias_control;
      VkPhysicalDeviceDepthClampControlFeaturesEXT depth_clamp_control;
      VkPhysicalDeviceDepthClipControlFeaturesEXT depth_clip_control;
      VkPhysicalDeviceDepthClipEnableFeaturesEXT depth_clip_enable;
      VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT
         dynamic_rendering_unused_attachments;
      VkPhysicalDeviceExtendedDynamicState3FeaturesEXT
         extended_dynamic_state_3;
      VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT
         fragment_shader_interlock;
      VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT
         graphics_pipeline_library;
      VkPhysicalDeviceImage2DViewOf3DFeaturesEXT image_2d_view_of_3d;
      VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT image_sliced_view_of_3d;
      VkPhysicalDeviceImageViewMinLodFeaturesEXT image_view_min_lod;
      VkPhysicalDeviceLegacyDitheringFeaturesEXT legacy_dithering;
      VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT
         legacy_vertex_attributes;
      VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader;
      VkPhysicalDeviceMultiDrawFeaturesEXT multi_draw;
      VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT
         multisampled_render_to_single_sampled;
      VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutable_descriptor_type;
      VkPhysicalDeviceNestedCommandBufferFeaturesEXT nested_command_buffer;
      VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT non_seamless_cube_map;
      VkPhysicalDevicePipelineLibraryGroupHandlesFeaturesEXT
         pipeline_library_group_handles;
      VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT
         primitive_topology_list_restart;
      VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT
         primitives_generated_query;
      VkPhysicalDeviceProvokingVertexFeaturesEXT provoking_vertex;
      VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT
         rasterization_order_attachment_access;
      VkPhysicalDeviceShaderAtomicFloatFeaturesEXT shader_atomic_float;
      VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT shader_atomic_float_2;
      VkPhysicalDeviceShaderFloat8FeaturesEXT shader_float8;
      VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT
         shader_image_atomic_int64;
      VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT
         shader_replicated_composites;
      VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT
         shader_uniform_buffer_unsized_array;
      VkPhysicalDeviceTransformFeedbackFeaturesEXT transform_feedback;
      VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT
         vertex_input_dynamic_state;
      VkPhysicalDeviceYcbcrImageArraysFeaturesEXT ycbcr_image_arrays;
   } local_feats;

   /* Clear the struct so that all unqueried features will be VK_FALSE. */
   memset(&local_feats, 0, sizeof(local_feats));

   assert(renderer_version >= VK_API_VERSION_1_1);

   /* clang-format off */

   if (renderer_version >= VK_API_VERSION_1_2) {
      VN_ADD_PNEXT(feats2, VULKAN_1_1_FEATURES, local_feats.vulkan_1_1);
      VN_ADD_PNEXT(feats2, VULKAN_1_2_FEATURES, local_feats.vulkan_1_2);
   } else {
      /* Vulkan 1.1 */
      VN_ADD_PNEXT(feats2, 16BIT_STORAGE_FEATURES, local_feats._16bit_storage);
      VN_ADD_PNEXT(feats2, MULTIVIEW_FEATURES, local_feats.multiview);
      VN_ADD_PNEXT(feats2, PROTECTED_MEMORY_FEATURES, local_feats.protected_memory);
      VN_ADD_PNEXT(feats2, SAMPLER_YCBCR_CONVERSION_FEATURES, local_feats.sampler_ycbcr_conversion);
      VN_ADD_PNEXT(feats2, SHADER_DRAW_PARAMETERS_FEATURES, local_feats.shader_draw_parameters);
      VN_ADD_PNEXT(feats2, VARIABLE_POINTERS_FEATURES, local_feats.variable_pointers);

      /* Vulkan 1.2 */
      VN_ADD_PNEXT_EXT(feats2, 8BIT_STORAGE_FEATURES, local_feats._8bit_storage, exts->KHR_8bit_storage);
      VN_ADD_PNEXT_EXT(feats2, BUFFER_DEVICE_ADDRESS_FEATURES, local_feats.buffer_device_address, exts->KHR_buffer_device_address);
      VN_ADD_PNEXT_EXT(feats2, DESCRIPTOR_INDEXING_FEATURES, local_feats.descriptor_indexing, exts->EXT_descriptor_indexing);
      VN_ADD_PNEXT_EXT(feats2, HOST_QUERY_RESET_FEATURES, local_feats.host_query_reset, exts->EXT_host_query_reset);
      VN_ADD_PNEXT_EXT(feats2, IMAGELESS_FRAMEBUFFER_FEATURES, local_feats.imageless_framebuffer, exts->KHR_imageless_framebuffer);
      VN_ADD_PNEXT_EXT(feats2, SCALAR_BLOCK_LAYOUT_FEATURES, local_feats.scalar_block_layout, exts->EXT_scalar_block_layout);
      VN_ADD_PNEXT_EXT(feats2, SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES, local_feats.separate_depth_stencil_layouts, exts->KHR_separate_depth_stencil_layouts);
      VN_ADD_PNEXT_EXT(feats2, SHADER_ATOMIC_INT64_FEATURES, local_feats.shader_atomic_int64, exts->KHR_shader_atomic_int64);
      VN_ADD_PNEXT_EXT(feats2, SHADER_FLOAT16_INT8_FEATURES, local_feats.shader_float16_int8, exts->KHR_shader_float16_int8);
      VN_ADD_PNEXT_EXT(feats2, SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES, local_feats.shader_subgroup_extended_types, exts->KHR_shader_subgroup_extended_types);
      VN_ADD_PNEXT_EXT(feats2, TIMELINE_SEMAPHORE_FEATURES, local_feats.timeline_semaphore, exts->KHR_timeline_semaphore);
      VN_ADD_PNEXT_EXT(feats2, UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES, local_feats.uniform_buffer_standard_layout, exts->KHR_uniform_buffer_standard_layout);
      VN_ADD_PNEXT_EXT(feats2, VULKAN_MEMORY_MODEL_FEATURES, local_feats.vulkan_memory_model, exts->KHR_vulkan_memory_model);
   }

   if (renderer_version >= VK_API_VERSION_1_3) {
      VN_ADD_PNEXT(feats2, VULKAN_1_3_FEATURES, local_feats.vulkan_1_3);
   } else {
      VN_ADD_PNEXT_EXT(feats2, DYNAMIC_RENDERING_FEATURES, local_feats.dynamic_rendering, exts->KHR_dynamic_rendering);
      VN_ADD_PNEXT_EXT(feats2, IMAGE_ROBUSTNESS_FEATURES, local_feats.image_robustness, exts->EXT_image_robustness);
      VN_ADD_PNEXT_EXT(feats2, INLINE_UNIFORM_BLOCK_FEATURES, local_feats.inline_uniform_block, exts->EXT_inline_uniform_block);
      VN_ADD_PNEXT_EXT(feats2, MAINTENANCE_4_FEATURES, local_feats.maintenance_4, exts->KHR_maintenance4);
      VN_ADD_PNEXT_EXT(feats2, PIPELINE_CREATION_CACHE_CONTROL_FEATURES, local_feats.pipeline_creation_cache_control, exts->EXT_pipeline_creation_cache_control);
      VN_ADD_PNEXT_EXT(feats2, PRIVATE_DATA_FEATURES, local_feats.private_data, exts->EXT_private_data);
      VN_ADD_PNEXT_EXT(feats2, SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES, local_feats.shader_demote_to_helper_invocation, exts->EXT_shader_demote_to_helper_invocation);
      VN_ADD_PNEXT_EXT(feats2, SHADER_INTEGER_DOT_PRODUCT_FEATURES, local_feats.shader_integer_dot_product, exts->KHR_shader_integer_dot_product);
      VN_ADD_PNEXT_EXT(feats2, SHADER_TERMINATE_INVOCATION_FEATURES, local_feats.shader_terminate_invocation, exts->KHR_shader_terminate_invocation);
      VN_ADD_PNEXT_EXT(feats2, SUBGROUP_SIZE_CONTROL_FEATURES, local_feats.subgroup_size_control, exts->EXT_subgroup_size_control);
      VN_ADD_PNEXT_EXT(feats2, SYNCHRONIZATION_2_FEATURES, local_feats.synchronization2, exts->KHR_synchronization2);
      VN_ADD_PNEXT_EXT(feats2, TEXTURE_COMPRESSION_ASTC_HDR_FEATURES, local_feats.texture_compression_astc_hdr, exts->EXT_texture_compression_astc_hdr);
      VN_ADD_PNEXT_EXT(feats2, ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES, local_feats.zero_initialize_workgroup_memory, exts->KHR_zero_initialize_workgroup_memory);
   }

   /* Vulkan 1.3: The extensions for the below structs were promoted, but some
    * struct members were omitted from VkPhysicalDeviceVulkan13Features.
    */
   VN_ADD_PNEXT_EXT(feats2, 4444_FORMATS_FEATURES_EXT, local_feats._4444_formats, exts->EXT_4444_formats);
   VN_ADD_PNEXT_EXT(feats2, EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT, local_feats.extended_dynamic_state_2, exts->EXT_extended_dynamic_state2);
   VN_ADD_PNEXT_EXT(feats2, EXTENDED_DYNAMIC_STATE_FEATURES_EXT, local_feats.extended_dynamic_state, exts->EXT_extended_dynamic_state);
   VN_ADD_PNEXT_EXT(feats2, TEXEL_BUFFER_ALIGNMENT_FEATURES_EXT, local_feats.texel_buffer_alignment, exts->EXT_texel_buffer_alignment);
   VN_ADD_PNEXT_EXT(feats2, YCBCR_2_PLANE_444_FORMATS_FEATURES_EXT, local_feats.ycbcr_2plane_444_formats, exts->EXT_ycbcr_2plane_444_formats);

   /* Vulkan 1.4 */
   if (renderer_version >= VK_API_VERSION_1_4) {
      VN_ADD_PNEXT(feats2, VULKAN_1_4_FEATURES, local_feats.vulkan_1_4);
   } else {
      VN_ADD_PNEXT_EXT(feats2, GLOBAL_PRIORITY_QUERY_FEATURES, local_feats.global_priority_query, exts->KHR_global_priority || exts->EXT_global_priority_query);
      VN_ADD_PNEXT_EXT(feats2, HOST_IMAGE_COPY_FEATURES, local_feats.host_image_copy, exts->EXT_host_image_copy);
      VN_ADD_PNEXT_EXT(feats2, INDEX_TYPE_UINT8_FEATURES, local_feats.index_type_uint8, exts->KHR_index_type_uint8 || exts->EXT_index_type_uint8);
      VN_ADD_PNEXT_EXT(feats2, LINE_RASTERIZATION_FEATURES, local_feats.line_rasterization, exts->KHR_line_rasterization || exts->EXT_line_rasterization);
      VN_ADD_PNEXT_EXT(feats2, MAINTENANCE_5_FEATURES, local_feats.maintenance_5, exts->KHR_maintenance5);
      VN_ADD_PNEXT_EXT(feats2, MAINTENANCE_6_FEATURES, local_feats.maintenance_6, exts->KHR_maintenance6);
      VN_ADD_PNEXT_EXT(feats2, PIPELINE_PROTECTED_ACCESS_FEATURES, local_feats.pipeline_protected_access, exts->EXT_pipeline_protected_access);
      VN_ADD_PNEXT_EXT(feats2, PIPELINE_ROBUSTNESS_FEATURES, local_feats.pipeline_robustness, exts->EXT_pipeline_robustness);
      VN_ADD_PNEXT_EXT(feats2, SHADER_EXPECT_ASSUME_FEATURES, local_feats.shader_expect_assume, exts->KHR_shader_expect_assume);
      VN_ADD_PNEXT_EXT(feats2, SHADER_FLOAT_CONTROLS_2_FEATURES, local_feats.shader_float_controls_2, exts->KHR_shader_float_controls2);
      VN_ADD_PNEXT_EXT(feats2, SHADER_SUBGROUP_ROTATE_FEATURES, local_feats.shader_subgroup_rotate, exts->KHR_shader_subgroup_rotate);
      VN_ADD_PNEXT_EXT(feats2, VERTEX_ATTRIBUTE_DIVISOR_FEATURES, local_feats.vertex_attribute_divisor, exts->KHR_vertex_attribute_divisor || exts->EXT_vertex_attribute_divisor);
   }

   /* Vulkan 1.4: dynamicRenderingLocalRead feature is partially promoted
    */
   VN_ADD_PNEXT_EXT(feats2, DYNAMIC_RENDERING_LOCAL_READ_FEATURES, local_feats.dynamic_rendering_local_read, exts->KHR_dynamic_rendering_local_read);

   /* KHR */
   VN_ADD_PNEXT_EXT(feats2, ACCELERATION_STRUCTURE_FEATURES_KHR, local_feats.acceleration_structure, exts->KHR_acceleration_structure);
   VN_ADD_PNEXT_EXT(feats2, COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR, local_feats.compute_shader_derivatives, exts->KHR_compute_shader_derivatives || exts->NV_compute_shader_derivatives);
   VN_ADD_PNEXT_EXT(feats2, COOPERATIVE_MATRIX_FEATURES_KHR, local_feats.cooperative_matrix, exts->KHR_cooperative_matrix);
   VN_ADD_PNEXT_EXT(feats2, DEPTH_CLAMP_ZERO_ONE_FEATURES_KHR, local_feats.depth_clamp_zero_one, exts->KHR_depth_clamp_zero_one || exts->EXT_depth_clamp_zero_one);
   VN_ADD_PNEXT_EXT(feats2, FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR, local_feats.fragment_shader_barycentric, exts->KHR_fragment_shader_barycentric);
   VN_ADD_PNEXT_EXT(feats2, FRAGMENT_SHADING_RATE_FEATURES_KHR, local_feats.fragment_shading_rate, exts->KHR_fragment_shading_rate);
   VN_ADD_PNEXT_EXT(feats2, MAINTENANCE_7_FEATURES_KHR, local_feats.maintenance_7, exts->KHR_maintenance7);
   VN_ADD_PNEXT_EXT(feats2, RAY_QUERY_FEATURES_KHR, local_feats.ray_query, exts->KHR_ray_query);
   VN_ADD_PNEXT_EXT(feats2, RAY_TRACING_MAINTENANCE_1_FEATURES_KHR, local_feats.ray_tracing_maintenance_1, exts->KHR_ray_tracing_maintenance1);
   VN_ADD_PNEXT_EXT(feats2, RAY_TRACING_PIPELINE_FEATURES_KHR, local_feats.ray_tracing_pipeline, exts->KHR_ray_tracing_pipeline);
   VN_ADD_PNEXT_EXT(feats2, RAY_TRACING_POSITION_FETCH_FEATURES_KHR, local_feats.ray_tracing_position_fetch, exts->KHR_ray_tracing_position_fetch);
   VN_ADD_PNEXT_EXT(feats2, ROBUSTNESS_2_FEATURES_KHR, local_feats.robustness_2, exts->KHR_robustness2 || exts->EXT_robustness2);
   VN_ADD_PNEXT_EXT(feats2, SHADER_BFLOAT16_FEATURES_KHR, local_feats.shader_bfloat16, exts->KHR_shader_bfloat16);
   VN_ADD_PNEXT_EXT(feats2, SHADER_CLOCK_FEATURES_KHR, local_feats.shader_clock, exts->KHR_shader_clock);
   VN_ADD_PNEXT_EXT(feats2, SHADER_MAXIMAL_RECONVERGENCE_FEATURES_KHR, local_feats.shader_maximal_reconvergence, exts->KHR_shader_maximal_reconvergence);
   VN_ADD_PNEXT_EXT(feats2, SHADER_RELAXED_EXTENDED_INSTRUCTION_FEATURES_KHR, local_feats.shader_relaxed_extended_instruction, exts->KHR_shader_relaxed_extended_instruction);
   VN_ADD_PNEXT_EXT(feats2, SHADER_SUBGROUP_UNIFORM_CONTROL_FLOW_FEATURES_KHR, local_feats.shader_subgroup_uniform_control_flow, exts->KHR_shader_subgroup_uniform_control_flow);
   VN_ADD_PNEXT_EXT(feats2, SHADER_QUAD_CONTROL_FEATURES_KHR, local_feats.shader_quad_control, exts->KHR_shader_quad_control);
   VN_ADD_PNEXT_EXT(feats2, SHADER_UNTYPED_POINTERS_FEATURES_KHR, local_feats.shader_untyped_pointers, exts->KHR_shader_untyped_pointers);
   VN_ADD_PNEXT_EXT(feats2, WORKGROUP_MEMORY_EXPLICIT_LAYOUT_FEATURES_KHR, local_feats.workgroup_memory_explicit_layout, exts->KHR_workgroup_memory_explicit_layout);

   /* EXT */
   VN_ADD_PNEXT_EXT(feats2, ATTACHMENT_FEEDBACK_LOOP_DYNAMIC_STATE_FEATURES_EXT, local_feats.attachment_feedback_loop_dynamic_state, exts->EXT_attachment_feedback_loop_dynamic_state);
   VN_ADD_PNEXT_EXT(feats2, ATTACHMENT_FEEDBACK_LOOP_LAYOUT_FEATURES_EXT, local_feats.attachment_feedback_loop_layout, exts->EXT_attachment_feedback_loop_layout);
   VN_ADD_PNEXT_EXT(feats2, BLEND_OPERATION_ADVANCED_FEATURES_EXT, local_feats.blend_operation_advanced, exts->EXT_blend_operation_advanced);
   VN_ADD_PNEXT_EXT(feats2, BORDER_COLOR_SWIZZLE_FEATURES_EXT, local_feats.border_color_swizzle, exts->EXT_border_color_swizzle);
   VN_ADD_PNEXT_EXT(feats2, BUFFER_DEVICE_ADDRESS_FEATURES_EXT, local_feats.buffer_device_address_ext, exts->EXT_buffer_device_address);
   VN_ADD_PNEXT_EXT(feats2, COLOR_WRITE_ENABLE_FEATURES_EXT, local_feats.color_write_enable, exts->EXT_color_write_enable);
   VN_ADD_PNEXT_EXT(feats2, CONDITIONAL_RENDERING_FEATURES_EXT, local_feats.conditional_rendering, exts->EXT_conditional_rendering);
   VN_ADD_PNEXT_EXT(feats2, CUSTOM_BORDER_COLOR_FEATURES_EXT, local_feats.custom_border_color, exts->EXT_custom_border_color);
   VN_ADD_PNEXT_EXT(feats2, DEPTH_BIAS_CONTROL_FEATURES_EXT, local_feats.depth_bias_control, exts->EXT_depth_bias_control);
   VN_ADD_PNEXT_EXT(feats2, DEPTH_CLAMP_CONTROL_FEATURES_EXT, local_feats.depth_clamp_control, exts->EXT_depth_clamp_control);
   VN_ADD_PNEXT_EXT(feats2, DEPTH_CLIP_CONTROL_FEATURES_EXT, local_feats.depth_clip_control, exts->EXT_depth_clip_control);
   VN_ADD_PNEXT_EXT(feats2, DEPTH_CLIP_ENABLE_FEATURES_EXT, local_feats.depth_clip_enable, exts->EXT_depth_clip_enable);
   VN_ADD_PNEXT_EXT(feats2, DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT, local_feats.dynamic_rendering_unused_attachments, exts->EXT_dynamic_rendering_unused_attachments);
   VN_ADD_PNEXT_EXT(feats2, EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, local_feats.extended_dynamic_state_3, exts->EXT_extended_dynamic_state3);
   VN_ADD_PNEXT_EXT(feats2, FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT, local_feats.fragment_shader_interlock, exts->EXT_fragment_shader_interlock);
   VN_ADD_PNEXT_EXT(feats2, GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT, local_feats.graphics_pipeline_library, exts->EXT_graphics_pipeline_library);
   VN_ADD_PNEXT_EXT(feats2, IMAGE_2D_VIEW_OF_3D_FEATURES_EXT, local_feats.image_2d_view_of_3d, exts->EXT_image_2d_view_of_3d);
   VN_ADD_PNEXT_EXT(feats2, IMAGE_SLICED_VIEW_OF_3D_FEATURES_EXT, local_feats.image_sliced_view_of_3d, exts->EXT_image_sliced_view_of_3d);
   VN_ADD_PNEXT_EXT(feats2, IMAGE_VIEW_MIN_LOD_FEATURES_EXT, local_feats.image_view_min_lod, exts->EXT_image_view_min_lod);
   VN_ADD_PNEXT_EXT(feats2, LEGACY_DITHERING_FEATURES_EXT, local_feats.legacy_dithering, exts->EXT_legacy_dithering);
   VN_ADD_PNEXT_EXT(feats2, LEGACY_VERTEX_ATTRIBUTES_FEATURES_EXT, local_feats.legacy_vertex_attributes, exts->EXT_legacy_vertex_attributes);
   VN_ADD_PNEXT_EXT(feats2, MESH_SHADER_FEATURES_EXT, local_feats.mesh_shader, exts->EXT_mesh_shader);
   VN_ADD_PNEXT_EXT(feats2, MULTI_DRAW_FEATURES_EXT, local_feats.multi_draw, exts->EXT_multi_draw);
   VN_ADD_PNEXT_EXT(feats2, MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT, local_feats.multisampled_render_to_single_sampled, exts->EXT_multisampled_render_to_single_sampled);
   VN_ADD_PNEXT_EXT(feats2, MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT, local_feats.mutable_descriptor_type, exts->EXT_mutable_descriptor_type || exts->VALVE_mutable_descriptor_type);
   VN_ADD_PNEXT_EXT(feats2, NESTED_COMMAND_BUFFER_FEATURES_EXT, local_feats.nested_command_buffer, exts->EXT_nested_command_buffer);
   VN_ADD_PNEXT_EXT(feats2, NON_SEAMLESS_CUBE_MAP_FEATURES_EXT, local_feats.non_seamless_cube_map, exts->EXT_non_seamless_cube_map);
   VN_ADD_PNEXT_EXT(feats2, PIPELINE_LIBRARY_GROUP_HANDLES_FEATURES_EXT, local_feats.pipeline_library_group_handles, exts->EXT_pipeline_library_group_handles);
   VN_ADD_PNEXT_EXT(feats2, PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT, local_feats.primitive_topology_list_restart, exts->EXT_primitive_topology_list_restart);
   VN_ADD_PNEXT_EXT(feats2, PRIMITIVES_GENERATED_QUERY_FEATURES_EXT, local_feats.primitives_generated_query, exts->EXT_primitives_generated_query);
   VN_ADD_PNEXT_EXT(feats2, PROVOKING_VERTEX_FEATURES_EXT, local_feats.provoking_vertex, exts->EXT_provoking_vertex);
   VN_ADD_PNEXT_EXT(feats2, RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT, local_feats.rasterization_order_attachment_access, exts->EXT_rasterization_order_attachment_access || exts->ARM_rasterization_order_attachment_access);
   VN_ADD_PNEXT_EXT(feats2, SHADER_ATOMIC_FLOAT_FEATURES_EXT, local_feats.shader_atomic_float, exts->EXT_shader_atomic_float);
   VN_ADD_PNEXT_EXT(feats2, SHADER_ATOMIC_FLOAT_2_FEATURES_EXT, local_feats.shader_atomic_float_2, exts->EXT_shader_atomic_float2);
   VN_ADD_PNEXT_EXT(feats2, SHADER_FLOAT8_FEATURES_EXT, local_feats.shader_float8, exts->EXT_shader_float8);
   VN_ADD_PNEXT_EXT(feats2, SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT, local_feats.shader_image_atomic_int64, exts->EXT_shader_image_atomic_int64);
   VN_ADD_PNEXT_EXT(feats2, SHADER_REPLICATED_COMPOSITES_FEATURES_EXT, local_feats.shader_replicated_composites, exts->EXT_shader_replicated_composites);
   VN_ADD_PNEXT_EXT(feats2, SHADER_UNIFORM_BUFFER_UNSIZED_ARRAY_FEATURES_EXT, local_feats.shader_uniform_buffer_unsized_array, exts->EXT_shader_uniform_buffer_unsized_array);
   VN_ADD_PNEXT_EXT(feats2, TRANSFORM_FEEDBACK_FEATURES_EXT, local_feats.transform_feedback, exts->EXT_transform_feedback);
   VN_ADD_PNEXT_EXT(feats2, VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT, local_feats.vertex_input_dynamic_state, exts->EXT_vertex_input_dynamic_state);
   VN_ADD_PNEXT_EXT(feats2, YCBCR_IMAGE_ARRAYS_FEATURES_EXT, local_feats.ycbcr_image_arrays, exts->EXT_ycbcr_image_arrays);

   /* clang-format on */

   vn_call_vkGetPhysicalDeviceFeatures2(
      ring, vn_physical_device_to_handle(physical_dev), &feats2);

   struct vk_features *feats = &physical_dev->base.vk.supported_features;
   vk_set_physical_device_features(feats, &feats2);

   /* Enable features for extensions natively implemented in Venus driver.
    * See vn_physical_device_get_native_extensions.
    */
   feats->deviceMemoryReport = true;

   /* VK_EXT_map_memory_placed */
   feats->memoryMapPlaced = true;
   feats->memoryMapRangePlaced = false;
   feats->memoryUnmapReserve = true;

#ifdef VN_USE_WSI_PLATFORM
   feats->presentId = true;
   feats->presentId2 = true;
   feats->presentWait = true;
   feats->presentWait2 = true;
   feats->swapchainMaintenance1 = true;
#endif

   /* Host commands require custom protocol level support. Disable it
    * since it'd be non-trivial to make it performant.
    */
   if (exts->KHR_acceleration_structure)
      feats->accelerationStructureHostCommands = false;

   /* Disable unsupported ExtendedDynamicState3Features */
   if (exts->EXT_extended_dynamic_state3) {
      /* VK_NV_* extensions required */
      feats->extendedDynamicState3ViewportWScalingEnable = false;
      feats->extendedDynamicState3ViewportSwizzle = false;
      feats->extendedDynamicState3CoverageToColorEnable = false;
      feats->extendedDynamicState3CoverageToColorLocation = false;
      feats->extendedDynamicState3CoverageModulationMode = false;
      feats->extendedDynamicState3CoverageModulationTableEnable = false;
      feats->extendedDynamicState3CoverageModulationTable = false;
      feats->extendedDynamicState3CoverageReductionMode = false;
      feats->extendedDynamicState3RepresentativeFragmentTestEnable = false;
      feats->extendedDynamicState3ShadingRateImageEnable = false;
   }
}

static void
vn_physical_device_init_uuids(struct vn_physical_device *physical_dev)
{
   struct vk_properties *props = &physical_dev->base.vk.properties;
   struct mesa_sha1 sha1_ctx;
   uint8_t sha1[SHA1_DIGEST_LENGTH];

   static_assert(VK_UUID_SIZE <= SHA1_DIGEST_LENGTH, "");

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, &props->pipelineCacheUUID,
                     sizeof(props->pipelineCacheUUID));
   _mesa_sha1_final(&sha1_ctx, sha1);

   memcpy(props->pipelineCacheUUID, sha1, VK_UUID_SIZE);

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, &props->vendorID, sizeof(props->vendorID));
   _mesa_sha1_update(&sha1_ctx, &props->deviceID, sizeof(props->deviceID));
   _mesa_sha1_final(&sha1_ctx, sha1);

   memcpy(props->deviceUUID, sha1, VK_UUID_SIZE);

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, props->driverName, strlen(props->driverName));
   _mesa_sha1_update(&sha1_ctx, props->driverInfo, strlen(props->driverInfo));
   _mesa_sha1_final(&sha1_ctx, sha1);

   memcpy(props->driverUUID, sha1, VK_UUID_SIZE);

   memset(props->deviceLUID, 0, VK_LUID_SIZE);
   props->deviceNodeMask = 0;
   props->deviceLUIDValid = false;
}

static void
vn_physical_device_sanitize_properties(struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   struct vk_properties *props = &physical_dev->base.vk.properties;

   const uint32_t version_override = vk_get_version_override();
   if (version_override) {
      props->apiVersion = version_override;
   } else {
      /* cap the advertised api version */
      uint32_t ver = MIN3(props->apiVersion, VN_MAX_API_VERSION,
                          instance->renderer->info.vk_xml_version);
      if (VK_VERSION_PATCH(ver) > VK_VERSION_PATCH(props->apiVersion)) {
         ver =
            ver - VK_VERSION_PATCH(ver) + VK_VERSION_PATCH(props->apiVersion);
      }

      /* Clamp to 1.3 if proper passthrough for VK_EXT_host_image_copy is not
       * supported at the protocol level. It requires venus protocol v3.
       * See vn_physical_device_get_passthrough_extensions()
       */
      if (instance->renderer->info.vk_mesa_venus_protocol_spec_version < 3)
         ver = MIN2(VK_API_VERSION_1_3, ver);

      /* Clamp to 1.2 if we disabled VK_KHR_synchronization2 since it
       * is required for 1.3.
       * See vn_physical_device_get_passthrough_extensions()
       */
      if (!physical_dev->base.vk.supported_extensions.KHR_synchronization2)
         ver = MIN2(VK_API_VERSION_1_2, ver);

      props->apiVersion = ver;
   }

   /* ANGLE relies on ARM proprietary driver version for workarounds */
   const char *engine_name = instance->base.vk.app_info.engine_name;
   const bool forward_driver_version =
      props->driverID == VK_DRIVER_ID_ARM_PROPRIETARY && engine_name &&
      strcmp(engine_name, "ANGLE") == 0;
   if (!forward_driver_version)
      props->driverVersion = vk_get_driver_version();

   physical_dev->wa_min_fb_align = strstr(props->deviceName, "JSL") ? 128 : 1;

   char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   int device_name_len = snprintf(device_name, sizeof(device_name),
                                  "Virtio-GPU Venus (%s)", props->deviceName);
   if (device_name_len >= VK_MAX_PHYSICAL_DEVICE_NAME_SIZE) {
      memcpy(device_name + VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 5, "...)", 4);
      device_name_len = VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1;
   }
   memcpy(props->deviceName, device_name, device_name_len + 1);

   /* store renderer VkDriverId for implementation specific workarounds */
   physical_dev->renderer_driver_id = props->driverID;
   props->driverID = VK_DRIVER_ID_MESA_VENUS;

   snprintf(props->driverName, sizeof(props->driverName), "venus");
   snprintf(props->driverInfo, sizeof(props->driverInfo),
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);

   props->conformanceVersion.major = 1;
   props->conformanceVersion.minor = 4;
   props->conformanceVersion.subminor = 0;
   props->conformanceVersion.patch = 0;

   vn_physical_device_init_uuids(physical_dev);
}

static void
vn_physical_device_init_properties(struct vn_physical_device *physical_dev)
{
   const uint32_t renderer_version = physical_dev->renderer_version;
   struct vn_instance *instance = physical_dev->instance;
   const struct vn_renderer_info *renderer_info = &instance->renderer->info;
   struct vk_properties *props = &physical_dev->base.vk.properties;
   const struct vk_device_extension_table *exts =
      &physical_dev->renderer_extensions;
   VkPhysicalDeviceProperties2 props2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };
   struct {
      /* Vulkan 1.1 */
      VkPhysicalDeviceVulkan11Properties vulkan_1_1;
      VkPhysicalDeviceIDProperties id;
      VkPhysicalDeviceSubgroupProperties subgroup;
      VkPhysicalDevicePointClippingProperties point_clipping;
      VkPhysicalDeviceMultiviewProperties multiview;
      VkPhysicalDeviceProtectedMemoryProperties protected_memory;
      VkPhysicalDeviceMaintenance3Properties maintenance_3;

      /* Vulkan 1.2 */
      VkPhysicalDeviceVulkan12Properties vulkan_1_2;
      VkPhysicalDeviceDriverProperties driver;
      VkPhysicalDeviceFloatControlsProperties float_controls;
      VkPhysicalDeviceDescriptorIndexingProperties descriptor_indexing;
      VkPhysicalDeviceDepthStencilResolveProperties depth_stencil_resolve;
      VkPhysicalDeviceSamplerFilterMinmaxProperties sampler_filter_minmax;
      VkPhysicalDeviceTimelineSemaphoreProperties timeline_semaphore;

      /* Vulkan 1.3 */
      VkPhysicalDeviceVulkan13Properties vulkan_1_3;
      VkPhysicalDeviceInlineUniformBlockProperties inline_uniform_block;
      VkPhysicalDeviceMaintenance4Properties maintenance_4;
      VkPhysicalDeviceShaderIntegerDotProductProperties
         shader_integer_dot_product;
      VkPhysicalDeviceSubgroupSizeControlProperties subgroup_size_control;
      VkPhysicalDeviceTexelBufferAlignmentProperties texel_buffer_alignment;

      /* Vulkan 1.4 */
      VkPhysicalDeviceVulkan14Properties vulkan_1_4;
      VkPhysicalDeviceHostImageCopyProperties host_image_copy;
      VkPhysicalDeviceLineRasterizationProperties line_rasterization;
      VkPhysicalDeviceMaintenance5Properties maintenance_5;
      VkPhysicalDeviceMaintenance6Properties maintenance_6;
      VkPhysicalDevicePipelineRobustnessProperties pipeline_robustness;
      VkPhysicalDevicePushDescriptorProperties push_descriptor;
      VkPhysicalDeviceVertexAttributeDivisorProperties
         vertex_attribute_divisor;

      /* KHR */
      VkPhysicalDeviceAccelerationStructurePropertiesKHR
         acceleration_structure;
      VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR
         compute_shader_derivatives;
      VkPhysicalDeviceCooperativeMatrixPropertiesKHR cooperative_matrix;
      VkPhysicalDeviceFragmentShaderBarycentricPropertiesKHR
         fragment_shader_barycentric;
      VkPhysicalDeviceFragmentShadingRatePropertiesKHR fragment_shading_rate;
      VkPhysicalDeviceMaintenance7PropertiesKHR maintenance_7;
      VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline;
      VkPhysicalDeviceRobustness2PropertiesKHR robustness_2;

      /* EXT */
      VkPhysicalDeviceBlendOperationAdvancedPropertiesEXT
         blend_operation_advanced;
      VkPhysicalDeviceConservativeRasterizationPropertiesEXT
         conservative_rasterization;
      VkPhysicalDeviceCustomBorderColorPropertiesEXT custom_border_color;
      VkPhysicalDeviceExtendedDynamicState3PropertiesEXT
         extended_dynamic_state_3;
      VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT
         graphics_pipeline_library;
      VkPhysicalDeviceLegacyVertexAttributesPropertiesEXT
         legacy_vertex_attributes;
      VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader;
      VkPhysicalDeviceMultiDrawPropertiesEXT multi_draw;
      VkPhysicalDeviceNestedCommandBufferPropertiesEXT nested_command_buffer;
      VkPhysicalDevicePCIBusInfoPropertiesEXT pci_bus_info;
      VkPhysicalDeviceProvokingVertexPropertiesEXT provoking_vertex;
      VkPhysicalDeviceSampleLocationsPropertiesEXT sample_locations;
      VkPhysicalDeviceTransformFeedbackPropertiesEXT transform_feedback;
      VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT
         vertex_attribute_divisor_ext;
   } local_props;

   /* Clear the structs so all unqueried properties will be well-defined. */
   memset(props, 0, sizeof(*props));
   memset(&local_props, 0, sizeof(local_props));

   assert(renderer_version >= VK_API_VERSION_1_1);

   /* clang-format off */
   if (renderer_version >= VK_API_VERSION_1_2) {
      VN_ADD_PNEXT(props2, VULKAN_1_1_PROPERTIES, local_props.vulkan_1_1);
      VN_ADD_PNEXT(props2, VULKAN_1_2_PROPERTIES, local_props.vulkan_1_2);
   } else {
      /* Vulkan 1.1 */
      VN_ADD_PNEXT(props2, ID_PROPERTIES, local_props.id);
      VN_ADD_PNEXT(props2, MAINTENANCE_3_PROPERTIES, local_props.maintenance_3);
      VN_ADD_PNEXT(props2, MULTIVIEW_PROPERTIES, local_props.multiview);
      VN_ADD_PNEXT(props2, POINT_CLIPPING_PROPERTIES, local_props.point_clipping);
      VN_ADD_PNEXT(props2, PROTECTED_MEMORY_PROPERTIES, local_props.protected_memory);
      VN_ADD_PNEXT(props2, SUBGROUP_PROPERTIES, local_props.subgroup);

      /* Vulkan 1.2 */
      VN_ADD_PNEXT_EXT(props2, DEPTH_STENCIL_RESOLVE_PROPERTIES, local_props.depth_stencil_resolve, exts->KHR_depth_stencil_resolve);
      VN_ADD_PNEXT_EXT(props2, DESCRIPTOR_INDEXING_PROPERTIES, local_props.descriptor_indexing, exts->EXT_descriptor_indexing);
      VN_ADD_PNEXT_EXT(props2, DRIVER_PROPERTIES, local_props.driver, exts->KHR_driver_properties);
      VN_ADD_PNEXT_EXT(props2, FLOAT_CONTROLS_PROPERTIES, local_props.float_controls, exts->KHR_shader_float_controls);
      VN_ADD_PNEXT_EXT(props2, SAMPLER_FILTER_MINMAX_PROPERTIES, local_props.sampler_filter_minmax, exts->EXT_sampler_filter_minmax);
      VN_ADD_PNEXT_EXT(props2, TIMELINE_SEMAPHORE_PROPERTIES, local_props.timeline_semaphore, exts->KHR_timeline_semaphore);
   }

   if (renderer_version >= VK_API_VERSION_1_3) {
      VN_ADD_PNEXT(props2, VULKAN_1_3_PROPERTIES, local_props.vulkan_1_3);
   } else {
      VN_ADD_PNEXT_EXT(props2, INLINE_UNIFORM_BLOCK_PROPERTIES, local_props.inline_uniform_block, exts->EXT_inline_uniform_block);
      VN_ADD_PNEXT_EXT(props2, MAINTENANCE_4_PROPERTIES, local_props.maintenance_4, exts->KHR_maintenance4);
      VN_ADD_PNEXT_EXT(props2, SHADER_INTEGER_DOT_PRODUCT_PROPERTIES, local_props.shader_integer_dot_product, exts->KHR_shader_integer_dot_product);
      VN_ADD_PNEXT_EXT(props2, SUBGROUP_SIZE_CONTROL_PROPERTIES, local_props.subgroup_size_control, exts->EXT_subgroup_size_control);
      VN_ADD_PNEXT_EXT(props2, TEXEL_BUFFER_ALIGNMENT_PROPERTIES, local_props.texel_buffer_alignment, exts->EXT_texel_buffer_alignment);
   }

   /* Vulkan 1.4 */
   if (renderer_version >= VK_API_VERSION_1_4) {
      local_props.vulkan_1_4.copySrcLayoutCount = ARRAY_SIZE(physical_dev->copy_src_layouts);
      local_props.vulkan_1_4.pCopySrcLayouts = physical_dev->copy_src_layouts;
      local_props.vulkan_1_4.copyDstLayoutCount = ARRAY_SIZE(physical_dev->copy_dst_layouts);
      local_props.vulkan_1_4.pCopyDstLayouts = physical_dev->copy_dst_layouts;

      VN_ADD_PNEXT(props2, VULKAN_1_4_PROPERTIES, local_props.vulkan_1_4);
   } else {
      if (exts->EXT_host_image_copy) {
         local_props.host_image_copy.copySrcLayoutCount = ARRAY_SIZE(physical_dev->copy_src_layouts);
         local_props.host_image_copy.pCopySrcLayouts = physical_dev->copy_src_layouts;
         local_props.host_image_copy.copyDstLayoutCount = ARRAY_SIZE(physical_dev->copy_dst_layouts);
         local_props.host_image_copy.pCopyDstLayouts = physical_dev->copy_dst_layouts;
      }

      VN_ADD_PNEXT_EXT(props2, HOST_IMAGE_COPY_PROPERTIES, local_props.host_image_copy, exts->EXT_host_image_copy);
      VN_ADD_PNEXT_EXT(props2, LINE_RASTERIZATION_PROPERTIES, local_props.line_rasterization, exts->KHR_line_rasterization || exts->EXT_line_rasterization);
      VN_ADD_PNEXT_EXT(props2, MAINTENANCE_5_PROPERTIES, local_props.maintenance_5, exts->KHR_maintenance5);
      VN_ADD_PNEXT_EXT(props2, MAINTENANCE_6_PROPERTIES, local_props.maintenance_6, exts->KHR_maintenance6);
      VN_ADD_PNEXT_EXT(props2, PIPELINE_ROBUSTNESS_PROPERTIES, local_props.pipeline_robustness, exts->EXT_pipeline_robustness);
      VN_ADD_PNEXT_EXT(props2, PUSH_DESCRIPTOR_PROPERTIES, local_props.push_descriptor, exts->KHR_push_descriptor);
      VN_ADD_PNEXT_EXT(props2, VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES, local_props.vertex_attribute_divisor, exts->KHR_vertex_attribute_divisor);
   }

   /* KHR */
   VN_ADD_PNEXT_EXT(props2, ACCELERATION_STRUCTURE_PROPERTIES_KHR, local_props.acceleration_structure, exts->KHR_acceleration_structure);
   VN_ADD_PNEXT_EXT(props2, COMPUTE_SHADER_DERIVATIVES_PROPERTIES_KHR, local_props.compute_shader_derivatives, exts->KHR_compute_shader_derivatives);
   VN_ADD_PNEXT_EXT(props2, COOPERATIVE_MATRIX_PROPERTIES_KHR, local_props.cooperative_matrix, exts->KHR_cooperative_matrix);
   VN_ADD_PNEXT_EXT(props2, FRAGMENT_SHADER_BARYCENTRIC_PROPERTIES_KHR, local_props.fragment_shader_barycentric, exts->KHR_fragment_shader_barycentric);
   VN_ADD_PNEXT_EXT(props2, FRAGMENT_SHADING_RATE_PROPERTIES_KHR, local_props.fragment_shading_rate, exts->KHR_fragment_shading_rate);
   VN_ADD_PNEXT_EXT(props2, MAINTENANCE_7_PROPERTIES_KHR, local_props.maintenance_7, exts->KHR_maintenance7);
   VN_ADD_PNEXT_EXT(props2, RAY_TRACING_PIPELINE_PROPERTIES_KHR, local_props.ray_tracing_pipeline, exts->KHR_ray_tracing_pipeline);
   VN_ADD_PNEXT_EXT(props2, ROBUSTNESS_2_PROPERTIES_KHR, local_props.robustness_2, exts->KHR_robustness2 || exts->EXT_robustness2);

   /* EXT */
   VN_ADD_PNEXT_EXT(props2, BLEND_OPERATION_ADVANCED_PROPERTIES_EXT, local_props.blend_operation_advanced, exts->EXT_blend_operation_advanced);
   VN_ADD_PNEXT_EXT(props2, CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, local_props.conservative_rasterization, exts->EXT_conservative_rasterization);
   VN_ADD_PNEXT_EXT(props2, CUSTOM_BORDER_COLOR_PROPERTIES_EXT, local_props.custom_border_color, exts->EXT_custom_border_color);
   VN_ADD_PNEXT_EXT(props2, EXTENDED_DYNAMIC_STATE_3_PROPERTIES_EXT, local_props.extended_dynamic_state_3, exts->EXT_extended_dynamic_state3);
   VN_ADD_PNEXT_EXT(props2, GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT, local_props.graphics_pipeline_library, exts->EXT_graphics_pipeline_library);
   VN_ADD_PNEXT_EXT(props2, LEGACY_VERTEX_ATTRIBUTES_PROPERTIES_EXT, local_props.legacy_vertex_attributes, exts->EXT_legacy_vertex_attributes);
   VN_ADD_PNEXT_EXT(props2, MESH_SHADER_PROPERTIES_EXT, local_props.mesh_shader, exts->EXT_mesh_shader);
   VN_ADD_PNEXT_EXT(props2, MULTI_DRAW_PROPERTIES_EXT, local_props.multi_draw, exts->EXT_multi_draw);
   VN_ADD_PNEXT_EXT(props2, NESTED_COMMAND_BUFFER_PROPERTIES_EXT, local_props.nested_command_buffer, exts->EXT_nested_command_buffer);
   VN_ADD_PNEXT_EXT(props2, PCI_BUS_INFO_PROPERTIES_EXT, local_props.pci_bus_info, exts->EXT_pci_bus_info);
   VN_ADD_PNEXT_EXT(props2, PROVOKING_VERTEX_PROPERTIES_EXT, local_props.provoking_vertex, exts->EXT_provoking_vertex);
   VN_ADD_PNEXT_EXT(props2, SAMPLE_LOCATIONS_PROPERTIES_EXT, local_props.sample_locations, exts->EXT_sample_locations);
   VN_ADD_PNEXT_EXT(props2, TRANSFORM_FEEDBACK_PROPERTIES_EXT, local_props.transform_feedback, exts->EXT_transform_feedback);
   VN_ADD_PNEXT_EXT(props2, VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT, local_props.vertex_attribute_divisor_ext, !exts->KHR_vertex_attribute_divisor && exts->EXT_vertex_attribute_divisor);

   /* clang-format on */

   vn_call_vkGetPhysicalDeviceProperties2(
      instance->ring.ring, vn_physical_device_to_handle(physical_dev),
      &props2);

   /* clang-format off */

   /* Vulkan 1.0 */
   VN_SET_VK_PROPS(props, &props2);

   /* Vulkan 1.1 and 1.2 */
   if (renderer_version >= VK_API_VERSION_1_2) {
      VN_SET_VK_PROPS(props, &local_props.vulkan_1_1);
      VN_SET_VK_PROPS(props, &local_props.vulkan_1_2);
   } else {
      /* Vulkan 1.1 */
      VN_SET_VK_PROPS(props, &local_props.id);
      VN_SET_VK_PROPS(props, &local_props.subgroup);
      VN_SET_VK_PROPS(props, &local_props.point_clipping);
      VN_SET_VK_PROPS(props, &local_props.multiview);
      VN_SET_VK_PROPS(props, &local_props.protected_memory);
      VN_SET_VK_PROPS(props, &local_props.maintenance_3);

      /* Vulkan 1.2 */
      VN_SET_VK_PROPS_EXT(props, &local_props.driver, exts->KHR_driver_properties);
      VN_SET_VK_PROPS_EXT(props, &local_props.float_controls, exts->KHR_shader_float_controls);
      VN_SET_VK_PROPS_EXT(props, &local_props.descriptor_indexing, exts->EXT_descriptor_indexing);
      VN_SET_VK_PROPS_EXT(props, &local_props.depth_stencil_resolve, exts->KHR_depth_stencil_resolve);
      VN_SET_VK_PROPS_EXT(props, &local_props.sampler_filter_minmax, exts->EXT_sampler_filter_minmax);
      VN_SET_VK_PROPS_EXT(props, &local_props.timeline_semaphore, exts->KHR_timeline_semaphore);
   }

   /* Vulkan 1.3 */
   if (renderer_version >= VK_API_VERSION_1_3) {
      VN_SET_VK_PROPS(props, &local_props.vulkan_1_3);
   } else {
      VN_SET_VK_PROPS_EXT(props, &local_props.subgroup_size_control, exts->EXT_subgroup_size_control);
      VN_SET_VK_PROPS_EXT(props, &local_props.inline_uniform_block, exts->EXT_inline_uniform_block);
      VN_SET_VK_PROPS_EXT(props, &local_props.shader_integer_dot_product, exts->KHR_shader_integer_dot_product);
      VN_SET_VK_PROPS_EXT(props, &local_props.texel_buffer_alignment, exts->EXT_texel_buffer_alignment);
      VN_SET_VK_PROPS_EXT(props, &local_props.maintenance_4, exts->KHR_maintenance4);
   }

   /* Vulkan 1.4 */
   if (renderer_version >= VK_API_VERSION_1_4) {
      VN_SET_VK_PROPS(props, &local_props.vulkan_1_4);
   } else {
      VN_SET_VK_PROPS_EXT(props, &local_props.host_image_copy, exts->EXT_host_image_copy);
      VN_SET_VK_PROPS_EXT(props, &local_props.line_rasterization, exts->KHR_line_rasterization || exts->EXT_line_rasterization);
      VN_SET_VK_PROPS_EXT(props, &local_props.maintenance_5, exts->KHR_maintenance5);
      VN_SET_VK_PROPS_EXT(props, &local_props.maintenance_6, exts->KHR_maintenance6);
      VN_SET_VK_PROPS_EXT(props, &local_props.pipeline_robustness, exts->EXT_pipeline_robustness);
      VN_SET_VK_PROPS_EXT(props, &local_props.push_descriptor, exts->KHR_push_descriptor);
      VN_SET_VK_PROPS_EXT(props, &local_props.vertex_attribute_divisor, exts->KHR_vertex_attribute_divisor);
   }

   /* KHR */
   VN_SET_VK_PROPS_EXT(props, &local_props.acceleration_structure, exts->KHR_acceleration_structure);
   VN_SET_VK_PROPS_EXT(props, &local_props.compute_shader_derivatives, exts->KHR_compute_shader_derivatives);
   VN_SET_VK_PROPS_EXT(props, &local_props.cooperative_matrix, exts->KHR_cooperative_matrix);
   VN_SET_VK_PROPS_EXT(props, &local_props.fragment_shader_barycentric, exts->KHR_fragment_shader_barycentric);
   VN_SET_VK_PROPS_EXT(props, &local_props.fragment_shading_rate, exts->KHR_fragment_shading_rate);
   VN_SET_VK_PROPS_EXT(props, &local_props.maintenance_7, exts->KHR_maintenance7);
   VN_SET_VK_PROPS_EXT(props, &local_props.ray_tracing_pipeline, exts->KHR_ray_tracing_pipeline);
   VN_SET_VK_PROPS_EXT(props, &local_props.robustness_2, exts->KHR_robustness2 || exts->EXT_robustness2);

   /* EXT */
   VN_SET_VK_PROPS_EXT(props, &local_props.blend_operation_advanced, exts->EXT_blend_operation_advanced);
   VN_SET_VK_PROPS_EXT(props, &local_props.conservative_rasterization, exts->EXT_conservative_rasterization);
   VN_SET_VK_PROPS_EXT(props, &local_props.custom_border_color, exts->EXT_custom_border_color);
   VN_SET_VK_PROPS_EXT(props, &local_props.extended_dynamic_state_3, exts->EXT_extended_dynamic_state3);
   VN_SET_VK_PROPS_EXT(props, &local_props.graphics_pipeline_library, exts->EXT_graphics_pipeline_library);
   VN_SET_VK_PROPS_EXT(props, &local_props.legacy_vertex_attributes, exts->EXT_legacy_vertex_attributes);
   VN_SET_VK_PROPS_EXT(props, &local_props.mesh_shader, exts->EXT_mesh_shader);
   VN_SET_VK_PROPS_EXT(props, &local_props.multi_draw, exts->EXT_multi_draw);
   VN_SET_VK_PROPS_EXT(props, &local_props.nested_command_buffer, exts->EXT_nested_command_buffer);
   VN_SET_VK_PROPS_EXT(props, &local_props.pci_bus_info, exts->EXT_pci_bus_info);
   VN_SET_VK_PROPS_EXT(props, &local_props.provoking_vertex, exts->EXT_provoking_vertex);
   VN_SET_VK_PROPS_EXT(props, &local_props.sample_locations, exts->EXT_sample_locations);
   VN_SET_VK_PROPS_EXT(props, &local_props.transform_feedback, exts->EXT_transform_feedback);
   VN_SET_VK_PROPS_EXT(props, &local_props.vertex_attribute_divisor_ext, !exts->KHR_vertex_attribute_divisor && exts->EXT_vertex_attribute_divisor);

   /* clang-format on */

   /* maint7 layered api props will be filled upon query time */
   if (exts->KHR_maintenance7) {
      struct vn_layered_api_properties *layer =
         &physical_dev->layered_properties;

      /* VkPhysicalDeviceLayeredApiPropertiesKHR */
      layer->api.vendorID = props->vendorID;
      layer->api.deviceID = props->deviceID;
      layer->api.layeredAPI = VK_PHYSICAL_DEVICE_LAYERED_API_VULKAN_KHR;
      strcpy(layer->api.deviceName, props->deviceName);

      /* VkPhysicalDeviceLayeredApiVulkanPropertiesKHR */
      layer->vk.properties.properties = props2.properties;
      memset(&layer->vk.properties.properties.limits, 0,
             sizeof(layer->vk.properties.properties.limits));
      memset(&layer->vk.properties.properties.sparseProperties, 0,
             sizeof(layer->vk.properties.properties.sparseProperties));

      /* VkPhysicalDeviceDriverProperties */
      layer->driver.driverID = props->driverID;
      strcpy(layer->driver.driverName, props->driverName);
      strcpy(layer->driver.driverInfo, props->driverInfo);
      layer->driver.conformanceVersion = props->conformanceVersion;

      /* VkPhysicalDeviceIDProperties */
      memcpy(layer->id.deviceUUID, props->deviceUUID,
             sizeof(props->deviceUUID));
      memcpy(layer->id.driverUUID, props->driverUUID,
             sizeof(props->driverUUID));
      memcpy(layer->id.deviceLUID, props->deviceLUID,
             sizeof(props->deviceLUID));
      layer->id.deviceNodeMask = props->deviceNodeMask;
      layer->id.deviceLUIDValid = props->deviceLUIDValid;
   }

   /* initialize native properties */

   /* VK_EXT_map_memory_placed */
   uint64_t os_page_size = 4096;
   os_get_page_size(&os_page_size);
   props->minPlacedMemoryMapAlignment = os_page_size;

   /* VK_EXT_physical_device_drm */
   VN_SET_VK_PROPS(props, &renderer_info->drm.props);

   /* VK_EXT_pci_bus_info */
   if (renderer_info->pci.has_bus_info)
      VN_SET_VK_PROPS(props, &renderer_info->pci.props);

   /* VK_ANDROID_native_buffer */
   if (vk_android_get_front_buffer_usage())
      props->sharedImage = true;

   vn_physical_device_sanitize_properties(physical_dev);
}

static VkResult
vn_physical_device_init_queue_family_properties(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   struct vn_ring *ring = instance->ring.ring;
   const VkAllocationCallbacks *alloc = &instance->base.vk.alloc;
   uint32_t count = 0;

   vn_call_vkGetPhysicalDeviceQueueFamilyProperties2(
      ring, vn_physical_device_to_handle(physical_dev), &count, NULL);

   const bool can_query_prio =
      physical_dev->base.vk.supported_features.globalPriorityQuery;
   VkQueueFamilyProperties2 *props;
   VkQueueFamilyGlobalPriorityProperties *prio_props = NULL;

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &props, __typeof__(*props), count);
   if (can_query_prio)
      vk_multialloc_add(&ma, &prio_props, __typeof__(*prio_props), count);

   if (!vk_multialloc_zalloc(&ma, alloc, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE))
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < count; i++) {
      props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
      if (can_query_prio) {
         prio_props[i].sType =
            VK_STRUCTURE_TYPE_QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES;
         props[i].pNext = &prio_props[i];
      }
   }
   vn_call_vkGetPhysicalDeviceQueueFamilyProperties2(
      ring, vn_physical_device_to_handle(physical_dev), &count, props);

#if defined(VK_USE_PLATFORM_ANDROID_KHR) && ANDROID_API_LEVEL >= 34
   /* Starting from Android 14 (Android U), framework HWUI has required a
    * second graphics queue to avoid racing between webview and skiavk.
    */
   const char *engine_name = instance->base.vk.app_info.engine_name;
   const bool require_second_queue =
      engine_name && strcmp(engine_name, "android framework") == 0;
#else
   static const bool require_second_queue = false;
#endif
   physical_dev->emulate_second_queue = -1;
   for (uint32_t i = 0; i < count; i++) {
      if (props[i].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
         if (require_second_queue && !VN_DEBUG(NO_SECOND_QUEUE) &&
             props[i].queueFamilyProperties.queueCount < 2) {
            props[i].queueFamilyProperties.queueCount = 2;
            physical_dev->emulate_second_queue = i;
         }

         break;
      }
   }

   if (VN_DEBUG(NO_SPARSE))
      physical_dev->sparse_binding_disabled = true;

   physical_dev->queue_family_properties = props;
   physical_dev->global_priority_properties = prio_props;
   physical_dev->queue_family_count = count;

   return VK_SUCCESS;
}

static void
vn_physical_device_init_memory_properties(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   struct vn_ring *ring = instance->ring.ring;
   VkPhysicalDeviceMemoryProperties2 props2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
   };
   vn_call_vkGetPhysicalDeviceMemoryProperties2(
      ring, vn_physical_device_to_handle(physical_dev), &props2);

   physical_dev->memory_properties = props2.memoryProperties;

   /* Kernel makes every mapping coherent. If a memory type is truly
    * incoherent, it's better to remove the host-visible flag than silently
    * making it coherent. However, for app compatibility purpose, when
    * coherent-cached memory type is unavailable, we append the cached bit to
    * the first coherent memory type.
    */
   bool has_coherent_cached = false;
   uint32_t first_coherent = VK_MAX_MEMORY_TYPES;
   VkPhysicalDeviceMemoryProperties *props = &physical_dev->memory_properties;
   for (uint32_t i = 0; i < props->memoryTypeCount; i++) {
      VkMemoryPropertyFlags *flags = &props->memoryTypes[i].propertyFlags;
      const bool coherent = *flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      const bool cached = *flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      if (coherent) {
         if (first_coherent == VK_MAX_MEMORY_TYPES)
            first_coherent = i;
         if (cached)
            has_coherent_cached = true;
      } else if (cached) {
         *flags &= ~(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
      }
   }

   if (!has_coherent_cached) {
      props->memoryTypes[first_coherent].propertyFlags |=
         VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
   }
}

static void
vn_physical_device_init_external_memory(
   struct vn_physical_device *physical_dev)
{
   /* When a renderer VkDeviceMemory is exportable, we can create a
    * vn_renderer_bo from it. The vn_renderer_bo can be freely exported as an
    * opaque fd or a dma-buf.
    *
    * When an external memory can be imported as a vn_renderer_bo, that bo
    * might be imported as a renderer side VkDeviceMemory.
    *
    * However, to know if a rendender VkDeviceMemory is exportable or if a bo
    * can be imported as a renderer VkDeviceMemory. We have to start from
    * physical device external image and external buffer properties queries,
    * which requires to know the renderer supported external handle types. For
    * such info, we can reliably retrieve from the external memory extensions
    * advertised by the renderer.
    *
    * We require VK_EXT_external_memory_dma_buf to expose driver side external
    * memory support for a renderer running on Linux. As a comparison, when
    * the renderer runs on Windows, VK_KHR_external_memory_win32 might be
    * required for the same.
    *
    * For vtest, the protocol does not support external memory import. So we
    * only mask out the importable bit so that wsi over vtest can be supported.
    */
   if (physical_dev->renderer_extensions.EXT_external_memory_dma_buf) {
      physical_dev->external_memory.renderer_handle_type =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

#ifdef VK_USE_PLATFORM_ANDROID_KHR
      physical_dev->external_memory.supported_handle_types |=
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
#else
      physical_dev->external_memory.supported_handle_types =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
#endif
   }
}

static void
vn_physical_device_init_external_fence_handles(
   struct vn_physical_device *physical_dev)
{
   /* The current code manipulates the host-side VkFence directly.
    * vkWaitForFences is translated to repeated vkGetFenceStatus.
    *
    * External fence is not possible currently.  Instead, we cheat by
    * translating vkGetFenceFdKHR to an empty renderer submission for the
    * out fence, along with a venus protocol command to fix renderer side
    * fence payload.
    *
    * We would like to create a vn_renderer_sync from a host-side VkFence,
    * similar to how a vn_renderer_bo is created from a host-side
    * VkDeviceMemory.  That would require kernel support and tons of works on
    * the host side.  If we had that, and we kept both the vn_renderer_sync
    * and the host-side VkFence in sync, we would have the freedom to use
    * either of them depending on the occasions, and support external fences
    * and idle waiting.
    */
   if (physical_dev->renderer_extensions.KHR_external_fence_fd) {
      struct vn_ring *ring = physical_dev->instance->ring.ring;
      const VkPhysicalDeviceExternalFenceInfo info = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO,
         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
      };
      VkExternalFenceProperties props = {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES,
      };
      vn_call_vkGetPhysicalDeviceExternalFenceProperties(
         ring, vn_physical_device_to_handle(physical_dev), &info, &props);

      physical_dev->renderer_sync_fd.fence_exportable =
         props.externalFenceFeatures &
         VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT;
   }

   physical_dev->external_fence_handles = 0;

   if (physical_dev->instance->renderer->info.has_external_sync) {
      physical_dev->external_fence_handles =
         VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   }
}

static void
vn_physical_device_init_external_semaphore_handles(
   struct vn_physical_device *physical_dev)
{
   /* The current code manipulates the host-side VkSemaphore directly.  It
    * works very well for binary semaphores because there is no CPU operation.
    * But for timeline semaphores, the situation is similar to that of fences.
    * vkWaitSemaphores is translated to repeated vkGetSemaphoreCounterValue.
    *
    * External semaphore is not possible currently.  Instead, we cheat when
    * the semaphore is binary and the handle type is sync file. We do an empty
    * renderer submission for the out fence, along with a venus protocol
    * command to fix renderer side semaphore payload.
    *
    * We would like to create a vn_renderer_sync from a host-side VkSemaphore,
    * similar to how a vn_renderer_bo is created from a host-side
    * VkDeviceMemory.  The reasoning is the same as that for fences.
    * Additionally, we would like the sync file exported from the
    * vn_renderer_sync to carry the necessary information to identify the
    * host-side VkSemaphore.  That would allow the consumers to wait on the
    * host side rather than the guest side.
    */
   if (physical_dev->renderer_extensions.KHR_external_semaphore_fd) {
      struct vn_ring *ring = physical_dev->instance->ring.ring;
      const VkPhysicalDeviceExternalSemaphoreInfo info = {
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
      };
      VkExternalSemaphoreProperties props = {
         .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
      };
      vn_call_vkGetPhysicalDeviceExternalSemaphoreProperties(
         ring, vn_physical_device_to_handle(physical_dev), &info, &props);

      physical_dev->renderer_sync_fd.semaphore_exportable =
         props.externalSemaphoreFeatures &
         VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT;
      physical_dev->renderer_sync_fd.semaphore_importable =
         props.externalSemaphoreFeatures &
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   }

   physical_dev->external_binary_semaphore_handles = 0;
   physical_dev->external_timeline_semaphore_handles = 0;

   if (physical_dev->instance->renderer->info.has_external_sync) {
      physical_dev->external_binary_semaphore_handles =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
   }
}

static void
vn_physical_device_get_native_extensions(
   const struct vn_physical_device *physical_dev,
   struct vk_device_extension_table *exts)
{
   const struct vk_device_extension_table *renderer_exts =
      &physical_dev->renderer_extensions;

   memset(exts, 0, sizeof(*exts));

   if (physical_dev->instance->renderer->info.has_external_sync &&
       physical_dev->renderer_sync_fd.fence_exportable)
      exts->KHR_external_fence_fd = true;

   if (physical_dev->instance->renderer->info.has_external_sync &&
       physical_dev->renderer_sync_fd.semaphore_importable &&
       physical_dev->renderer_sync_fd.semaphore_exportable)
      exts->KHR_external_semaphore_fd = true;

#ifdef VK_USE_PLATFORM_ANDROID_KHR
   if (physical_dev->external_memory.renderer_handle_type &&
       renderer_exts->EXT_image_drm_format_modifier &&
       renderer_exts->EXT_queue_family_foreign) {
      exts->ANDROID_external_memory_android_hardware_buffer = true;

      /* For wsi, we require renderer:
       * - semaphore sync fd import for queue submission to skip scrubbing the
       *   wsi wait semaphores.
       * - fence sync fd export for QueueSignalReleaseImageANDROID to export a
       *   sync fd.
       *
       * TODO: relax these requirements by:
       * - properly scrubbing wsi wait semaphores
       * - not creating external fence but exporting sync fd directly
       */
      if (physical_dev->renderer_sync_fd.semaphore_importable &&
          physical_dev->renderer_sync_fd.fence_exportable)
         exts->ANDROID_native_buffer = true;
   }
#else  /* VK_USE_PLATFORM_ANDROID_KHR */
   if (physical_dev->external_memory.renderer_handle_type) {
      exts->KHR_external_memory_fd = true;
      exts->EXT_external_memory_dma_buf = true;
   }
#endif /* VK_USE_PLATFORM_ANDROID_KHR */

#ifdef VN_USE_WSI_PLATFORM
   if (physical_dev->renderer_sync_fd.semaphore_importable) {
      exts->KHR_incremental_present = true;
      exts->KHR_present_id = true;
      exts->KHR_present_id2 = true;
      exts->KHR_present_wait = true;
      exts->KHR_present_wait2 = true;
      exts->KHR_swapchain = true;
      exts->KHR_swapchain_maintenance1 = true;
      exts->KHR_swapchain_mutable_format = true;
      exts->EXT_hdr_metadata = true;
      exts->EXT_swapchain_maintenance1 = true;
   }

   /* VK_EXT_pci_bus_info is required by common wsi to decide whether native
    * image or prime blit is used. Meanwhile, venus must stay on native image
    * path for proper fencing.
    * - For virtgpu, VK_EXT_pci_bus_info is natively supported.
    * - For vtest, pci bus info must be queried from the renderer side physical
    *   device to be compared against the render node opened by common wsi.
    */
   exts->EXT_pci_bus_info =
      physical_dev->instance->renderer->info.pci.has_bus_info ||
      renderer_exts->EXT_pci_bus_info;
#endif

   /* Use common implementation but enable only when the renderer supports
    * VK_KHR_acceleration_structure because VK_KHR_deferred_host_operations is
    * not passthrough from the renderer side.
    */
   exts->KHR_deferred_host_operations =
      physical_dev->ray_tracing && renderer_exts->KHR_acceleration_structure;
   exts->KHR_map_memory2 = true;
   exts->EXT_map_memory_placed = true;
   exts->EXT_physical_device_drm = true;
   /* use common implementation */
   exts->EXT_tooling_info = true;
   exts->EXT_device_memory_report = true;
}

static void
vn_physical_device_get_passthrough_extensions(
   const struct vn_physical_device *physical_dev,
   struct vk_device_extension_table *exts)
{
   struct vn_renderer *renderer = physical_dev->instance->renderer;

#if defined(VK_USE_PLATFORM_ANDROID_KHR) || defined(VN_USE_WSI_PLATFORM)
   /* WSI support currently requires semaphore sync fd import for
    * VK_KHR_synchronization2 for code simplicity. This requirement can be
    * dropped by implementing external semaphore purely on the driver side
    * (aka no corresponding renderer side object).
    */
   const bool can_sync2 = physical_dev->renderer_sync_fd.semaphore_importable;
#else
   static const bool can_sync2 = true;
#endif

   *exts = (struct vk_device_extension_table){
      /* promoted to VK_VERSION_1_1 */
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

      /* promoted to VK_VERSION_1_2 */
      .KHR_8bit_storage = true,
      .KHR_buffer_device_address = true,
      .KHR_create_renderpass2 = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_draw_indirect_count = true,
      .KHR_driver_properties = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_atomic_int64 = true,
      .KHR_shader_float16_int8 = true,
      .KHR_shader_float_controls = true,
      .KHR_shader_subgroup_extended_types = true,
      .KHR_spirv_1_4 = true,
      .KHR_timeline_semaphore = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_vulkan_memory_model = true,
      .EXT_descriptor_indexing = true,
      .EXT_host_query_reset = true,
      .EXT_sampler_filter_minmax = true,
      .EXT_scalar_block_layout = true,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_viewport_index_layer = true,

      /* promoted to VK_VERSION_1_3 */
      .KHR_copy_commands2 = true,
      .KHR_dynamic_rendering = true,
      .KHR_format_feature_flags2 = true,
      .KHR_maintenance4 = true,
      .KHR_shader_integer_dot_product = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_terminate_invocation = true,
      .KHR_synchronization2 = can_sync2,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats = true,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_image_robustness = true,
      .EXT_inline_uniform_block = true,
      .EXT_pipeline_creation_cache_control = true,
      /* hide behind renderer support to allow structs passing through */
      .EXT_pipeline_creation_feedback = true,
      /* hide behind renderer support to allow structs passing through */
      .EXT_private_data = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .EXT_subgroup_size_control = true,
      .EXT_texel_buffer_alignment = true,
      .EXT_texture_compression_astc_hdr = true,
      .EXT_ycbcr_2plane_444_formats = true,

      /* promoted to VK_VERSION_1_4 */
      .KHR_dynamic_rendering_local_read = true,
      .KHR_global_priority = true,
      .KHR_index_type_uint8 = true,
      .KHR_line_rasterization = true,
      .KHR_load_store_op_none = true,
      .KHR_maintenance5 = true,
      .KHR_maintenance6 = true,
      .KHR_push_descriptor = true,
      .KHR_shader_expect_assume = true,
      .KHR_shader_float_controls2 = true,
      .KHR_shader_subgroup_rotate = true,
      .KHR_vertex_attribute_divisor = true,
      .EXT_host_image_copy =
         renderer->info.vk_mesa_venus_protocol_spec_version >= 3,
      .EXT_pipeline_protected_access = true,
      .EXT_pipeline_robustness = true,

      /* KHR */
      .KHR_acceleration_structure = physical_dev->ray_tracing,
      .KHR_calibrated_timestamps = true,
      .KHR_compute_shader_derivatives = true,
      .KHR_cooperative_matrix = true,
      .KHR_depth_clamp_zero_one = true,
      .KHR_fragment_shader_barycentric = true,
      .KHR_fragment_shading_rate = true,
      .KHR_maintenance7 =
         physical_dev->renderer_version >= VK_API_VERSION_1_2 ||
         physical_dev->renderer_extensions.KHR_driver_properties,
      .KHR_pipeline_library = true,
      .KHR_ray_query = physical_dev->ray_tracing,
      .KHR_ray_tracing_maintenance1 = physical_dev->ray_tracing,
      .KHR_ray_tracing_pipeline = physical_dev->ray_tracing,
      .KHR_ray_tracing_position_fetch = physical_dev->ray_tracing,
      .KHR_robustness2 = true,
      .KHR_shader_bfloat16 = true,
      .KHR_shader_clock = true,
      .KHR_shader_maximal_reconvergence = true,
      .KHR_shader_quad_control = true,
      .KHR_shader_relaxed_extended_instruction = true,
      .KHR_shader_subgroup_uniform_control_flow = true,
      .KHR_shader_untyped_pointers = true,
      .KHR_workgroup_memory_explicit_layout = true,

      /* EXT */
      .EXT_attachment_feedback_loop_dynamic_state = true,
      .EXT_attachment_feedback_loop_layout = true,
      .EXT_blend_operation_advanced = true,
      .EXT_border_color_swizzle = true,
      .EXT_buffer_device_address = true,
      .EXT_calibrated_timestamps = true,
      .EXT_color_write_enable = true,
      .EXT_conditional_rendering = true,
      .EXT_conservative_rasterization = true,
      .EXT_custom_border_color = true,
      .EXT_depth_bias_control = true,
      .EXT_depth_clamp_control = true,
      .EXT_depth_clamp_zero_one = true,
      .EXT_depth_clip_control = true,
      .EXT_depth_clip_enable = true,
      .EXT_depth_range_unrestricted = true,
      .EXT_extended_dynamic_state3 = true,
      .EXT_dynamic_rendering_unused_attachments = true,
      .EXT_external_memory_acquire_unmodified = true,
      .EXT_filter_cubic = true,
      .EXT_fragment_shader_interlock = true,
      .EXT_global_priority = true,
      .EXT_global_priority_query = true,
      .EXT_graphics_pipeline_library = !VN_DEBUG(NO_GPL),
      .EXT_image_2d_view_of_3d = true,
      .EXT_image_drm_format_modifier = true,
      .EXT_image_sliced_view_of_3d = true,
      .EXT_image_view_min_lod = true,
      .EXT_index_type_uint8 = true,
      .EXT_legacy_dithering = true,
      .EXT_legacy_vertex_attributes = true,
      .EXT_line_rasterization = true,
      .EXT_load_store_op_none = true,
      .EXT_memory_budget = VN_DEBUG(MEM_BUDGET),
      .EXT_mesh_shader = true,
      .EXT_multi_draw = true,
      .EXT_multisampled_render_to_single_sampled = true,
      .EXT_mutable_descriptor_type = true,
      .EXT_nested_command_buffer = true,
      .EXT_non_seamless_cube_map = true,
      .EXT_pipeline_library_group_handles = physical_dev->ray_tracing,
      .EXT_post_depth_coverage = true,
      .EXT_primitive_topology_list_restart = true,
      .EXT_primitives_generated_query = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_rasterization_order_attachment_access = true,
      .EXT_robustness2 = true,
      .EXT_sample_locations = true,
      .EXT_shader_atomic_float = true,
      .EXT_shader_atomic_float2 = true,
      .EXT_shader_float8 = true,
      .EXT_shader_image_atomic_int64 = true,
      .EXT_shader_replicated_composites = true,
      .EXT_shader_stencil_export = true,
      .EXT_shader_subgroup_ballot = true,
      .EXT_shader_subgroup_vote = true,
      .EXT_shader_uniform_buffer_unsized_array = true,
      .EXT_transform_feedback = true,
      .EXT_vertex_attribute_divisor = true,
      .EXT_vertex_input_dynamic_state = true,

      /* vendor */
      .ARM_rasterization_order_attachment_access = true,
      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,
      .IMG_filter_cubic = true,
      .NV_compute_shader_derivatives = true,
      .VALVE_mutable_descriptor_type = true,
   };
}

static void
vn_physical_device_init_supported_extensions(
   struct vn_physical_device *physical_dev)
{
   struct vk_device_extension_table native;
   struct vk_device_extension_table passthrough;

   physical_dev->ray_tracing = !VN_DEBUG(NO_RAY_TRACING);

   vn_physical_device_get_native_extensions(physical_dev, &native);
   vn_physical_device_get_passthrough_extensions(physical_dev, &passthrough);

   for (uint32_t i = 0; i < VK_DEVICE_EXTENSION_COUNT; i++) {
      const VkExtensionProperties *props = &vk_device_extensions[i];

#ifdef ANDROID_STRICT
      if (!vk_android_allowed_device_extensions.extensions[i])
         continue;
#endif

      if (native.extensions[i]) {
         physical_dev->base.vk.supported_extensions.extensions[i] = true;
         physical_dev->extension_spec_versions[i] = props->specVersion;
      } else if (passthrough.extensions[i] &&
                 physical_dev->renderer_extensions.extensions[i]) {
         physical_dev->base.vk.supported_extensions.extensions[i] = true;
         physical_dev->extension_spec_versions[i] = MIN2(
            physical_dev->extension_spec_versions[i], props->specVersion);
      }
   }
}

static VkResult
vn_physical_device_init_renderer_extensions(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   struct vn_ring *ring = instance->ring.ring;
   const VkAllocationCallbacks *alloc = &instance->base.vk.alloc;

   /* get renderer extensions */
   uint32_t count = 0;
   VkResult result = vn_call_vkEnumerateDeviceExtensionProperties(
      ring, vn_physical_device_to_handle(physical_dev), NULL, &count, NULL);
   if (result != VK_SUCCESS)
      return result;

   VkExtensionProperties *exts = NULL;
   if (count) {
      exts = vk_alloc(alloc, sizeof(*exts) * count, VN_DEFAULT_ALIGN,
                      VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!exts)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      result = vn_call_vkEnumerateDeviceExtensionProperties(
         ring, vn_physical_device_to_handle(physical_dev), NULL, &count,
         exts);
      if (result < VK_SUCCESS) {
         vk_free(alloc, exts);
         return result;
      }
   }

   physical_dev->extension_spec_versions =
      vk_zalloc(alloc,
                sizeof(*physical_dev->extension_spec_versions) *
                   VK_DEVICE_EXTENSION_COUNT,
                VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!physical_dev->extension_spec_versions) {
      vk_free(alloc, exts);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t i = 0; i < VK_DEVICE_EXTENSION_COUNT; i++) {
      const VkExtensionProperties *props = &vk_device_extensions[i];
      for (uint32_t j = 0; j < count; j++) {
         if (strcmp(props->extensionName, exts[j].extensionName))
            continue;

         /* check encoder support */
         const uint32_t enc_ext_spec_version =
            vn_extension_get_spec_version(props->extensionName);
         if (!enc_ext_spec_version)
            continue;

         physical_dev->renderer_extensions.extensions[i] = true;
         physical_dev->extension_spec_versions[i] =
            MIN2(exts[j].specVersion, enc_ext_spec_version);

         break;
      }
   }

   vk_free(alloc, exts);

   /* VK_KHR_external_memory_fd is required for venus memory mapping */
   if (!physical_dev->renderer_extensions.KHR_external_memory_fd) {
      vk_free(alloc, physical_dev->extension_spec_versions);
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }

   return VK_SUCCESS;
}

static VkResult
vn_physical_device_init_renderer_version(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   struct vn_ring *ring = instance->ring.ring;

   /*
    * We either check and enable VK_KHR_get_physical_device_properties2, or we
    * must use vkGetPhysicalDeviceProperties to get the device-level version.
    */
   VkPhysicalDeviceProperties props;
   vn_call_vkGetPhysicalDeviceProperties(
      ring, vn_physical_device_to_handle(physical_dev), &props);
   if (props.apiVersion < VN_MIN_RENDERER_VERSION) {
      if (VN_DEBUG(INIT)) {
         vn_log(instance, "%s has unsupported renderer device version %d.%d",
                props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
                VK_VERSION_MINOR(props.apiVersion));
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* device version for internal use is capped */
   physical_dev->renderer_version =
      MIN3(props.apiVersion, instance->renderer_api_version,
           instance->renderer->info.vk_xml_version);

   return VK_SUCCESS;
}

static void
vn_image_format_cache_debug_dump(
   struct vn_image_format_properties_cache *cache)
{
   vn_log(NULL, "  hit %u\n", cache->debug.cache_hit_count);
   vn_log(NULL, "  miss %u\n", cache->debug.cache_miss_count);
   vn_log(NULL, "  skip %u\n", cache->debug.cache_skip_count);
}

static void
vn_image_format_cache_init(struct vn_physical_device *physical_dev)
{
   struct vn_image_format_properties_cache *cache =
      &physical_dev->image_format_cache;

   if (VN_PERF(NO_ASYNC_IMAGE_FORMAT))
      return;

   cache->ht = _mesa_hash_table_create(NULL, vn_cache_key_hash_function,
                                       vn_cache_key_equal_function);
   if (!cache->ht)
      return;

   simple_mtx_init(&cache->mutex, mtx_plain);
   list_inithead(&cache->lru);
}

static void
vn_image_format_cache_fini(struct vn_physical_device *physical_dev)
{
   const VkAllocationCallbacks *alloc =
      &physical_dev->base.vk.instance->alloc;
   struct vn_image_format_properties_cache *cache =
      &physical_dev->image_format_cache;

   if (!cache->ht)
      return;

   hash_table_foreach(cache->ht, hash_entry) {
      struct vn_image_format_cache_entry *cache_entry = hash_entry->data;
      list_del(&cache_entry->head);
      vk_free(alloc, cache_entry);
   }
   assert(list_is_empty(&cache->lru));

   _mesa_hash_table_destroy(cache->ht, NULL);

   simple_mtx_destroy(&cache->mutex);

   if (VN_DEBUG(CACHE))
      vn_image_format_cache_debug_dump(cache);
}

static void
vn_physical_device_disable_sparse_binding(
   struct vn_physical_device *physical_dev)
{
   struct vk_features *feats = &physical_dev->base.vk.supported_features;
   feats->sparseBinding = false;
   feats->sparseResidencyBuffer = false;
   feats->sparseResidencyImage2D = false;
   feats->sparseResidencyImage3D = false;
   feats->sparseResidency2Samples = false;
   feats->sparseResidency4Samples = false;
   feats->sparseResidency8Samples = false;
   feats->sparseResidency16Samples = false;
   feats->sparseResidencyAliased = false;

   struct vk_properties *props = &physical_dev->base.vk.properties;
   props->sparseAddressSpaceSize = 0;
   props->sparseResidencyStandard2DBlockShape = 0;
   props->sparseResidencyStandard2DMultisampleBlockShape = 0;
   props->sparseResidencyStandard3DBlockShape = 0;
   props->sparseResidencyAlignedMipSize = 0;
   props->sparseResidencyNonResidentStrict = 0;
}

static VkResult
vn_physical_device_init(struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   const VkAllocationCallbacks *alloc = &instance->base.vk.alloc;
   VkResult result;

   vn_physical_device_init_external_memory(physical_dev);
   vn_physical_device_init_external_fence_handles(physical_dev);
   vn_physical_device_init_external_semaphore_handles(physical_dev);

   vn_physical_device_init_supported_extensions(physical_dev);
   vn_physical_device_init_features(physical_dev);
   vn_physical_device_init_properties(physical_dev);

   /* global priority props caching relies on initialized features */
   result = vn_physical_device_init_queue_family_properties(physical_dev);
   if (result != VK_SUCCESS)
      goto fail;

   if (physical_dev->sparse_binding_disabled)
      vn_physical_device_disable_sparse_binding(physical_dev);

   vn_physical_device_init_memory_properties(physical_dev);

   result = vn_wsi_init(physical_dev);
   if (result != VK_SUCCESS)
      goto fail;

   simple_mtx_init(&physical_dev->format_update_mutex, mtx_plain);
   util_sparse_array_init(&physical_dev->format_properties,
                          sizeof(struct vn_format_properties_entry), 64);

   vn_image_format_cache_init(physical_dev);

   return VK_SUCCESS;

fail:
   vk_free(alloc, physical_dev->queue_family_properties);
   return result;
}

void
vn_physical_device_fini(struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   const VkAllocationCallbacks *alloc = &instance->base.vk.alloc;

   vn_image_format_cache_fini(physical_dev);

   simple_mtx_destroy(&physical_dev->format_update_mutex);
   util_sparse_array_finish(&physical_dev->format_properties);

   vn_wsi_fini(physical_dev);
   vk_free(alloc, physical_dev->extension_spec_versions);
   vk_free(alloc, physical_dev->queue_family_properties);

   vn_physical_device_base_fini(&physical_dev->base);
}

static struct vn_physical_device *
find_physical_device(struct vn_physical_device *physical_devs,
                     uint32_t count,
                     vn_object_id id)
{
   for (uint32_t i = 0; i < count; i++) {
      if (physical_devs[i].base.id == id)
         return &physical_devs[i];
   }
   return NULL;
}

static VkResult
vn_instance_enumerate_physical_device_groups_locked(
   struct vn_instance *instance,
   struct vn_physical_device *physical_devs,
   uint32_t physical_dev_count)
{
   VkInstance instance_handle = vn_instance_to_handle(instance);
   struct vn_ring *ring = instance->ring.ring;
   const VkAllocationCallbacks *alloc = &instance->base.vk.alloc;
   VkResult result;

   uint32_t count = 0;
   result = vn_call_vkEnumeratePhysicalDeviceGroups(ring, instance_handle,
                                                    &count, NULL);
   if (result != VK_SUCCESS)
      return result;

   VkPhysicalDeviceGroupProperties *groups =
      vk_alloc(alloc, sizeof(*groups) * count, VN_DEFAULT_ALIGN,
               VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!groups)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* VkPhysicalDeviceGroupProperties::physicalDevices is treated as an input
    * by the encoder.  Each VkPhysicalDevice must point to a valid object.
    * Each object must have id 0 as well, which is interpreted as a query by
    * the renderer.
    */
   struct vn_physical_device_base *temp_objs =
      vk_zalloc(alloc, sizeof(*temp_objs) * VK_MAX_DEVICE_GROUP_SIZE * count,
                VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!temp_objs) {
      vk_free(alloc, groups);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t i = 0; i < count; i++) {
      VkPhysicalDeviceGroupProperties *group = &groups[i];
      group->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
      group->pNext = NULL;
      for (uint32_t j = 0; j < VK_MAX_DEVICE_GROUP_SIZE; j++) {
         struct vn_physical_device_base *temp_obj =
            &temp_objs[VK_MAX_DEVICE_GROUP_SIZE * i + j];
         temp_obj->vk.base.type = VK_OBJECT_TYPE_PHYSICAL_DEVICE;
         group->physicalDevices[j] = (VkPhysicalDevice)temp_obj;
      }
   }

   result = vn_call_vkEnumeratePhysicalDeviceGroups(ring, instance_handle,
                                                    &count, groups);
   if (result != VK_SUCCESS) {
      vk_free(alloc, groups);
      vk_free(alloc, temp_objs);
      return result;
   }

   /* fix VkPhysicalDeviceGroupProperties::physicalDevices to point to
    * physical_devs and discard unsupported ones
    */
   uint32_t supported_count = 0;
   for (uint32_t i = 0; i < count; i++) {
      VkPhysicalDeviceGroupProperties *group = &groups[i];

      uint32_t group_physical_dev_count = 0;
      for (uint32_t j = 0; j < group->physicalDeviceCount; j++) {
         struct vn_physical_device_base *temp_obj =
            (struct vn_physical_device_base *)group->physicalDevices[j];
         struct vn_physical_device *physical_dev = find_physical_device(
            physical_devs, physical_dev_count, temp_obj->id);
         if (!physical_dev)
            continue;

         group->physicalDevices[group_physical_dev_count++] =
            vn_physical_device_to_handle(physical_dev);
      }

      group->physicalDeviceCount = group_physical_dev_count;
      if (!group->physicalDeviceCount)
         continue;

      if (supported_count < i)
         groups[supported_count] = *group;
      supported_count++;
   }

   count = supported_count;
   assert(count);

   vk_free(alloc, temp_objs);

   instance->physical_device.groups = groups;
   instance->physical_device.group_count = count;

   return VK_SUCCESS;
}

static VkResult
enumerate_physical_devices(struct vn_instance *instance,
                           struct vn_physical_device **out_physical_devs,
                           uint32_t *out_count)
{
   const VkAllocationCallbacks *alloc = &instance->base.vk.alloc;
   struct vn_ring *ring = instance->ring.ring;
   struct vn_physical_device *physical_devs = NULL;
   VkResult result;

   if (!instance->renderer) {
      *out_count = 0;
      return VK_SUCCESS;
   }
   uint32_t count = 0;
   result = vn_call_vkEnumeratePhysicalDevices(
      ring, vn_instance_to_handle(instance), &count, NULL);
   if (result != VK_SUCCESS || !count)
      return result;

   physical_devs =
      vk_zalloc(alloc, sizeof(*physical_devs) * count, VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!physical_devs)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   STACK_ARRAY(VkPhysicalDevice, handles, count);

   for (uint32_t i = 0; i < count; i++) {
      struct vn_physical_device *physical_dev = &physical_devs[i];

      struct vk_physical_device_dispatch_table dispatch_table;
      vk_physical_device_dispatch_table_from_entrypoints(
         &dispatch_table, &vn_physical_device_entrypoints, true);
      vk_physical_device_dispatch_table_from_entrypoints(
         &dispatch_table, &wsi_physical_device_entrypoints, false);
      result = vn_physical_device_base_init(
         &physical_dev->base, &instance->base, NULL, &dispatch_table);
      if (result != VK_SUCCESS) {
         count = i;
         goto fail;
      }

      physical_dev->instance = instance;

      handles[i] = vn_physical_device_to_handle(physical_dev);
   }

   result = vn_call_vkEnumeratePhysicalDevices(
      ring, vn_instance_to_handle(instance), &count, handles);
   if (result != VK_SUCCESS)
      goto fail;

   STACK_ARRAY_FINISH(handles);
   *out_physical_devs = physical_devs;
   *out_count = count;

   return VK_SUCCESS;

fail:
   for (uint32_t i = 0; i < count; i++)
      vn_physical_device_base_fini(&physical_devs[i].base);
   vk_free(alloc, physical_devs);
   STACK_ARRAY_FINISH(handles);
   return result;
}

static uint32_t
filter_physical_devices(struct vn_physical_device *physical_devs,
                        uint32_t count)
{
   uint32_t supported_count = 0;
   for (uint32_t i = 0; i < count; i++) {
      struct vn_physical_device *physical_dev = &physical_devs[i];

      /* init renderer version and discard unsupported devices */
      VkResult result =
         vn_physical_device_init_renderer_version(physical_dev);
      if (result != VK_SUCCESS) {
         vn_physical_device_base_fini(&physical_dev->base);
         continue;
      }

      result = vn_physical_device_init_renderer_extensions(physical_dev);
      if (result != VK_SUCCESS) {
         vn_physical_device_base_fini(&physical_dev->base);
         continue;
      }

      if (supported_count < i)
         physical_devs[supported_count] = *physical_dev;
      supported_count++;
   }

   return supported_count;
}

static VkResult
vn_instance_enumerate_physical_devices_and_groups(struct vn_instance *instance)
{
   const VkAllocationCallbacks *alloc = &instance->base.vk.alloc;
   struct vn_physical_device *physical_devs = NULL;
   uint32_t count = 0;
   VkResult result = VK_SUCCESS;

   mtx_lock(&instance->physical_device.mutex);

   if (instance->physical_device.initialized)
      goto unlock;
   instance->physical_device.initialized = true;

   result = enumerate_physical_devices(instance, &physical_devs, &count);
   if (result != VK_SUCCESS)
      goto unlock;

   count = filter_physical_devices(physical_devs, count);
   if (!count) {
      vk_free(alloc, physical_devs);
      goto unlock;
   }

   /* fully initialize physical devices */
   for (uint32_t i = 0; i < count; i++) {
      struct vn_physical_device *physical_dev = &physical_devs[i];

      result = vn_physical_device_init(physical_dev);
      if (result != VK_SUCCESS) {
         for (uint32_t j = 0; j < i; j++)
            vn_physical_device_fini(&physical_devs[j]);
         for (uint32_t j = i; j < count; j++)
            vn_physical_device_base_fini(&physical_devs[j].base);
         vk_free(alloc, physical_devs);
         goto unlock;
      }
   }

   result = vn_instance_enumerate_physical_device_groups_locked(
      instance, physical_devs, count);
   if (result != VK_SUCCESS) {
      for (uint32_t i = 0; i < count; i++)
         vn_physical_device_fini(&physical_devs[i]);
      vk_free(alloc, physical_devs);
      goto unlock;
   }

   instance->physical_device.devices = physical_devs;
   instance->physical_device.device_count = count;

unlock:
   mtx_unlock(&instance->physical_device.mutex);
   return result;
}

/* physical device commands */

VkResult
vn_EnumeratePhysicalDevices(VkInstance _instance,
                            uint32_t *pPhysicalDeviceCount,
                            VkPhysicalDevice *pPhysicalDevices)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);

   VkResult result =
      vn_instance_enumerate_physical_devices_and_groups(instance);
   if (result != VK_SUCCESS)
      return vn_error(instance, result);

   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDevice, out, pPhysicalDevices,
                          pPhysicalDeviceCount);
   for (uint32_t i = 0; i < instance->physical_device.device_count; i++) {
      vk_outarray_append_typed(VkPhysicalDevice, &out, physical_dev) {
         *physical_dev = vn_physical_device_to_handle(
            &instance->physical_device.devices[i]);
      }
   }

   return vk_outarray_status(&out);
}

VkResult
vn_EnumeratePhysicalDeviceGroups(
   VkInstance _instance,
   uint32_t *pPhysicalDeviceGroupCount,
   VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);

   VkResult result =
      vn_instance_enumerate_physical_devices_and_groups(instance);
   if (result != VK_SUCCESS)
      return vn_error(instance, result);

   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceGroupProperties, out,
                          pPhysicalDeviceGroupProperties,
                          pPhysicalDeviceGroupCount);
   for (uint32_t i = 0; i < instance->physical_device.group_count; i++) {
      vk_outarray_append_typed(VkPhysicalDeviceGroupProperties, &out, props) {
         *props = instance->physical_device.groups[i];
      }
   }

   return vk_outarray_status(&out);
}

VkResult
vn_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                      const char *pLayerName,
                                      uint32_t *pPropertyCount,
                                      VkExtensionProperties *pProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   if (pLayerName)
      return vn_error(physical_dev->instance, VK_ERROR_LAYER_NOT_PRESENT);

   VK_OUTARRAY_MAKE_TYPED(VkExtensionProperties, out, pProperties,
                          pPropertyCount);
   for (uint32_t i = 0; i < VK_DEVICE_EXTENSION_COUNT; i++) {
      if (physical_dev->base.vk.supported_extensions.extensions[i]) {
         vk_outarray_append_typed(VkExtensionProperties, &out, prop) {
            *prop = vk_device_extensions[i];
            prop->specVersion = physical_dev->extension_spec_versions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

VkResult
vn_EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                  uint32_t *pPropertyCount,
                                  VkLayerProperties *pProperties)
{
   *pPropertyCount = 0;
   return VK_SUCCESS;
}

void
vn_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                VkPhysicalDeviceProperties2 *pProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   const struct vn_layered_api_properties *layered_props =
      &physical_dev->layered_properties;

   vk_common_GetPhysicalDeviceProperties2(physicalDevice, pProperties);

   /* Properly fill maint7 layered api properties. */
   VkPhysicalDeviceLayeredApiPropertiesListKHR *layered_props_list =
      vk_find_struct(pProperties->pNext,
                     PHYSICAL_DEVICE_LAYERED_API_PROPERTIES_LIST_KHR);
   if (!layered_props_list)
      return;

   layered_props_list->layeredApiCount = 1;
   if (!layered_props_list->pLayeredApis)
      return;

   VN_COPY_STRUCT_GUTS(layered_props_list->pLayeredApis, &layered_props->api,
                       sizeof(layered_props->api));

   VkPhysicalDeviceLayeredApiVulkanPropertiesKHR *layered_vk_props =
      vk_find_struct(layered_props_list->pLayeredApis->pNext,
                     PHYSICAL_DEVICE_LAYERED_API_VULKAN_PROPERTIES_KHR);
   if (!layered_vk_props)
      return;

   VN_COPY_STRUCT_GUTS(&layered_vk_props->properties,
                       &layered_props->vk.properties,
                       sizeof(layered_props->vk.properties));
   vk_foreach_struct(layered_vk_pnext, layered_vk_props->properties.pNext) {
      switch (layered_vk_pnext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES:
         VN_COPY_STRUCT_GUTS(layered_vk_pnext, &layered_props->driver,
                             sizeof(layered_props->driver));
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES:
         VN_COPY_STRUCT_GUTS(layered_vk_pnext, &layered_props->id,
                             sizeof(layered_props->id));
         break;
      default:
         break;
      }
   }
}

void
vn_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice,
   uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out,
                          pQueueFamilyProperties, pQueueFamilyPropertyCount);
   for (uint32_t i = 0; i < physical_dev->queue_family_count; i++) {
      vk_outarray_append_typed(VkQueueFamilyProperties2, &out, props) {
         props->queueFamilyProperties =
            physical_dev->queue_family_properties[i].queueFamilyProperties;

         if (physical_dev->base.vk.supported_features.globalPriorityQuery) {
            VkQueueFamilyGlobalPriorityProperties *prio_props =
               vk_find_struct(props->pNext,
                              QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES);
            if (prio_props) {
               void *pnext = prio_props->pNext;
               *prio_props = physical_dev->global_priority_properties[i];
               prio_props->pNext = pnext;
            }
         }
      }
   }
}

void
vn_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;
   VkPhysicalDeviceMemoryBudgetPropertiesEXT *memory_budget = NULL;

   /* Don't waste time searching for unsupported structs. */
   if (physical_dev->base.vk.supported_extensions.EXT_memory_budget) {
      memory_budget =
         vk_find_struct(pMemoryProperties->pNext,
                        PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT);
   }

   /* When the app queries invariant memory properties, we return a cached
    * copy. For dynamic properties, we must query the server.
    */
   if (memory_budget) {
      vn_call_vkGetPhysicalDeviceMemoryProperties2(ring, physicalDevice,
                                                   pMemoryProperties);
   }

   /* Even when we query the server for memory properties, we must still
    * overwrite the invariant memory properties returned from the server with
    * our cached version.  Our cached version may differ from the server's
    * version due to workarounds.
    */
   pMemoryProperties->memoryProperties = physical_dev->memory_properties;
}

static inline void
vn_sanitize_format_properties(VkFormat format,
                              VkFormatProperties *props,
                              VkFormatProperties3 *props3)
{
   // YCbCr formats only support a subset of format feature flags
   static const VkFormatFeatureFlags allowed_ycbcr_feats =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT |
      VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
      VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
      VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT |
      VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_CHROMA_RECONSTRUCTION_EXPLICIT_FORCEABLE_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT |
      VK_FORMAT_FEATURE_DISJOINT_BIT;

   /* TODO drop rgba10x6 after supporting VK_EXT_rgba10x6_formats */
   switch (format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
   case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
   case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
      props->linearTilingFeatures &= allowed_ycbcr_feats;
      props->optimalTilingFeatures &= allowed_ycbcr_feats;
      if (props3) {
         props3->linearTilingFeatures &= allowed_ycbcr_feats;
         props3->optimalTilingFeatures &= allowed_ycbcr_feats;
      }
      break;
   default:
      break;
   }
}

void
vn_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                      VkFormat format,
                                      VkFormatProperties2 *pFormatProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;
   VkFormatProperties *props = &pFormatProperties->formatProperties;
   VkFormatProperties3 *props3 = NULL;
   VkBool32 *srpq = NULL;

   bool cacheable = true;
   uint64_t key = (uint64_t)format;
   vk_foreach_struct_const(src, pFormatProperties->pNext) {
      switch (src->sType) {
      case VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3:
         props3 = (VkFormatProperties3 *)src;
         key |= 1ull << 32;
         break;
      case VK_STRUCTURE_TYPE_SUBPASS_RESOLVE_PERFORMANCE_QUERY_EXT:
         srpq = &((VkSubpassResolvePerformanceQueryEXT *)src)->optimal;
         key |= 1ull << 33;
         break;
      default:
         cacheable = false;
         break;
      }
   }

   struct vn_format_properties_entry *entry = NULL;
   if (cacheable) {
      entry = util_sparse_array_get(&physical_dev->format_properties, key);
      if (entry->valid) {
         *props = entry->props;
         if (props3)
            VN_COPY_STRUCT_GUTS(props3, &entry->props3, sizeof(*props3));
         if (srpq)
            *srpq = entry->srpq;
         return;
      }
   }

   vn_call_vkGetPhysicalDeviceFormatProperties2(ring, physicalDevice, format,
                                                pFormatProperties);
   vn_sanitize_format_properties(format, props, props3);

   simple_mtx_lock(&physical_dev->format_update_mutex);
   if (entry && !entry->valid) {
      assert(cacheable);
      entry->props = *props;
      if (props3)
         VN_COPY_STRUCT_GUTS(&entry->props3, props3, sizeof(*props3));
      if (srpq)
         entry->srpq = *srpq;
      entry->valid = true;
   }
   simple_mtx_unlock(&physical_dev->format_update_mutex);
}

struct vn_physical_device_image_format_info {
   VkPhysicalDeviceImageFormatInfo2 format;
   VkPhysicalDeviceExternalImageFormatInfo external;
   VkImageFormatListCreateInfo list;
   VkImageStencilUsageCreateInfo stencil_usage;
   VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier;
   VkPhysicalDeviceImageViewImageFormatInfoEXT filter_cubic;
};

static const VkPhysicalDeviceImageFormatInfo2 *
vn_physical_device_fix_image_format_info(
   const VkPhysicalDeviceImageFormatInfo2 *info,
   const VkExternalMemoryHandleTypeFlagBits renderer_handle_type,
   struct vn_physical_device_image_format_info *local_info)
{
   local_info->format = *info;
   VkBaseOutStructure *dst = (void *)&local_info->format;

   vk_foreach_struct_const(src, info->pNext) {
      void *pnext = NULL;
      switch (src->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         memcpy(&local_info->external, src, sizeof(local_info->external));
         local_info->external.handleType = renderer_handle_type;
         pnext = &local_info->external;
         break;
      case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO:
         memcpy(&local_info->list, src, sizeof(local_info->list));
         pnext = &local_info->list;
         break;
      case VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO:
         memcpy(&local_info->stencil_usage, src,
                sizeof(local_info->stencil_usage));
         pnext = &local_info->stencil_usage;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT:
         memcpy(&local_info->modifier, src, sizeof(local_info->modifier));
         pnext = &local_info->modifier;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT:
         memcpy(&local_info->filter_cubic, src,
                sizeof(local_info->filter_cubic));
         pnext = &local_info->filter_cubic;
         break;
      default:
         break;
      }

      if (pnext) {
         dst->pNext = pnext;
         dst = pnext;
      }
   }

   dst->pNext = NULL;

   return &local_info->format;
}

static bool
vn_image_get_image_format_key(
   struct vn_physical_device *physical_dev,
   const VkPhysicalDeviceImageFormatInfo2 *format_info,
   const VkImageFormatProperties2 *format_props,
   uint8_t *key)
{
   struct mesa_sha1 sha1_ctx;

   if (!physical_dev->image_format_cache.ht)
      return false;

   _mesa_sha1_init(&sha1_ctx);

   /* VUID-VkPhysicalDeviceImageFormatInfo2-pNext-pNext
    * Each pNext member of any structure (including this one) in the pNext
    * chain must be either NULL or a pointer to a valid instance of
    * VkImageCompressionControlEXT, VkImageFormatListCreateInfo,
    * VkImageStencilUsageCreateInfo, VkOpticalFlowImageFormatInfoNV,
    * VkPhysicalDeviceExternalImageFormatInfo,
    * VkPhysicalDeviceImageDrmFormatModifierInfoEXT,
    * VkPhysicalDeviceImageViewImageFormatInfoEXT, or VkVideoProfileListInfoKHR
    *
    * Exclude VkOpticalFlowImageFormatInfoNV and VkVideoProfileListInfoKHR
    */
   if (format_info->pNext) {
      vk_foreach_struct_const(src, format_info->pNext) {
         switch (src->sType) {
         case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT: {
            VkImageCompressionControlEXT *compression_control =
               (VkImageCompressionControlEXT *)src;
            _mesa_sha1_update(&sha1_ctx, &compression_control->flags,
                              sizeof(VkImageCompressionFlagsEXT));
            _mesa_sha1_update(
               &sha1_ctx, compression_control->pFixedRateFlags,
               sizeof(uint32_t) *
                  compression_control->compressionControlPlaneCount);
            break;
         }
         case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO: {
            VkImageFormatListCreateInfo *format_list =
               (VkImageFormatListCreateInfo *)src;
            _mesa_sha1_update(
               &sha1_ctx, format_list->pViewFormats,
               sizeof(VkFormat) * format_list->viewFormatCount);

            break;
         }
         case VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO: {
            VkImageStencilUsageCreateInfo *stencil_usage =
               (VkImageStencilUsageCreateInfo *)src;
            _mesa_sha1_update(&sha1_ctx, &stencil_usage->stencilUsage,
                              sizeof(VkImageUsageFlags));
            break;
         }
         case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO: {
            VkPhysicalDeviceExternalImageFormatInfo *ext_image =
               (VkPhysicalDeviceExternalImageFormatInfo *)src;
            _mesa_sha1_update(&sha1_ctx, &ext_image->handleType,
                              sizeof(VkExternalMemoryHandleTypeFlagBits));
            break;
         }
         case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT: {
            VkPhysicalDeviceImageDrmFormatModifierInfoEXT *modifier_info =
               (VkPhysicalDeviceImageDrmFormatModifierInfoEXT *)src;
            _mesa_sha1_update(&sha1_ctx, &modifier_info->drmFormatModifier,
                              sizeof(uint64_t));
            if (modifier_info->sharingMode == VK_SHARING_MODE_CONCURRENT) {
               _mesa_sha1_update(
                  &sha1_ctx, modifier_info->pQueueFamilyIndices,
                  sizeof(uint32_t) * modifier_info->queueFamilyIndexCount);
            }
            break;
         }
         case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT: {
            VkPhysicalDeviceImageViewImageFormatInfoEXT *view_image =
               (VkPhysicalDeviceImageViewImageFormatInfoEXT *)src;
            _mesa_sha1_update(&sha1_ctx, &view_image->imageViewType,
                              sizeof(VkImageViewType));
            break;
         }
         default:
            physical_dev->image_format_cache.debug.cache_skip_count++;
            return false;
         }
      }
   }

   /* Hash pImageFormatProperties pNext as well since some of them are
    * optional in that they can be attached without a corresponding pNext
    * in pImageFormatInfo.
    *
    * VUID-VkImageFormatProperties2-pNext-pNext
    * Each pNext member of any structure (including this one) in the pNext
    * chain must be either NULL or a pointer to a valid instance of
    * VkAndroidHardwareBufferUsageANDROID, VkExternalImageFormatProperties,
    * VkFilterCubicImageViewImageFormatPropertiesEXT,
    * VkHostImageCopyDevicePerformanceQueryEXT,
    * VkImageCompressionPropertiesEXT,
    * VkSamplerYcbcrConversionImageFormatProperties, or
    * VkTextureLODGatherFormatPropertiesAMD
    *
    * VkAndroidHardwareBufferUsageANDROID is handled outside of the cache.
    * VkFilterCubicImageViewImageFormatPropertiesEXT,
    * VkTextureLODGatherFormatPropertiesAMD are not supported
    */
   if (format_props->pNext) {
      vk_foreach_struct_const(src, format_props->pNext) {
         switch (src->sType) {
         case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY:
         case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT:
         case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT:
            _mesa_sha1_update(&sha1_ctx, &src->sType,
                              sizeof(VkStructureType));
            break;
         default:
            physical_dev->image_format_cache.debug.cache_skip_count++;
            return false;
         }
      }
   }

   static const size_t format_info_2_hash_block_size =
      sizeof(VkFormat) + sizeof(VkImageType) + sizeof(VkImageTiling) +
      sizeof(VkImageUsageFlags) + sizeof(VkImageCreateFlags);

   _mesa_sha1_update(&sha1_ctx, &format_info->format,
                     format_info_2_hash_block_size);
   _mesa_sha1_final(&sha1_ctx, key);

   return true;
}

static bool
vn_image_init_format_from_cache(
   struct vn_physical_device *physical_dev,
   VkImageFormatProperties2 *pImageFormatProperties,
   VkResult *cached_result,
   uint8_t *key)
{
   struct vn_image_format_properties_cache *cache =
      &physical_dev->image_format_cache;

   assert(cache->ht);

   simple_mtx_lock(&cache->mutex);
   struct hash_entry *hash_entry = _mesa_hash_table_search(cache->ht, key);
   if (hash_entry) {
      struct vn_image_format_cache_entry *cache_entry = hash_entry->data;

      /* Copy the properties even if the cached_result is not supported.
       * Per spec 1.3.275 "If the combination of parameters to
       * vkGetPhysicalDeviceImageFormatProperties2 is not supported by the
       * implementation for use in vkCreateImage, then all members of
       * imageFormatProperties will be filled with zero."
       */
      pImageFormatProperties->imageFormatProperties =
         cache_entry->properties.format.imageFormatProperties;
      *cached_result = cache_entry->properties.cached_result;

      if (pImageFormatProperties->pNext) {
         vk_foreach_struct_const(src, pImageFormatProperties->pNext) {
            switch (src->sType) {
            case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES: {
               VkExternalImageFormatProperties *ext_image =
                  (VkExternalImageFormatProperties *)src;
               ext_image->externalMemoryProperties =
                  cache_entry->properties.ext_image.externalMemoryProperties;
               break;
            }
            case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY: {
               VkHostImageCopyDevicePerformanceQuery *host_copy =
                  (VkHostImageCopyDevicePerformanceQuery *)src;
               host_copy->optimalDeviceAccess =
                  cache_entry->properties.host_copy.optimalDeviceAccess;
               host_copy->identicalMemoryLayout =
                  cache_entry->properties.host_copy.identicalMemoryLayout;
               break;
            }
            case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT: {
               VkImageCompressionPropertiesEXT *compression =
                  (VkImageCompressionPropertiesEXT *)src;
               compression->imageCompressionFlags =
                  cache_entry->properties.compression.imageCompressionFlags;
               compression->imageCompressionFixedRateFlags =
                  cache_entry->properties.compression
                     .imageCompressionFixedRateFlags;
               break;
            }
            case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: {
               VkSamplerYcbcrConversionImageFormatProperties *ycbcr_conversion =
                  (VkSamplerYcbcrConversionImageFormatProperties *)src;
               ycbcr_conversion->combinedImageSamplerDescriptorCount =
                  cache_entry->properties.ycbcr_conversion
                     .combinedImageSamplerDescriptorCount;
               break;
            }
            case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT: {
               VkFilterCubicImageViewImageFormatPropertiesEXT *filter_cubic =
                  (VkFilterCubicImageViewImageFormatPropertiesEXT *)src;
               filter_cubic->filterCubic =
                  cache_entry->properties.filter_cubic.filterCubic;
               filter_cubic->filterCubicMinmax =
                  cache_entry->properties.filter_cubic.filterCubicMinmax;
               break;
            }
            default:
               UNREACHABLE("unexpected format props pNext");
            }
         }
      }

      list_move_to(&cache_entry->head, &cache->lru);
      p_atomic_inc(&cache->debug.cache_hit_count);
   } else {
      p_atomic_inc(&cache->debug.cache_miss_count);
   }
   simple_mtx_unlock(&cache->mutex);

   return !!hash_entry;
}

static void
vn_image_store_format_in_cache(
   struct vn_physical_device *physical_dev,
   uint8_t *key,
   VkImageFormatProperties2 *pImageFormatProperties,
   VkResult cached_result)
{
   const VkAllocationCallbacks *alloc =
      &physical_dev->base.vk.instance->alloc;
   struct vn_image_format_properties_cache *cache =
      &physical_dev->image_format_cache;
   struct vn_image_format_cache_entry *cache_entry = NULL;

   assert(cache->ht);

   simple_mtx_lock(&cache->mutex);

   /* Check if entry was added before lock */
   if (_mesa_hash_table_search(cache->ht, key)) {
      simple_mtx_unlock(&cache->mutex);
      return;
   }

   if (_mesa_hash_table_num_entries(cache->ht) ==
       IMAGE_FORMAT_CACHE_MAX_ENTRIES) {
      /* Evict/use the last entry in the lru list for this new entry */
      cache_entry = list_last_entry(&cache->lru,
                                    struct vn_image_format_cache_entry, head);

      _mesa_hash_table_remove_key(cache->ht, cache_entry->key);
      list_del(&cache_entry->head);
   } else {
      cache_entry = vk_zalloc(alloc, sizeof(*cache_entry), VN_DEFAULT_ALIGN,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!cache_entry) {
         simple_mtx_unlock(&cache->mutex);
         return;
      }
   }

   if (pImageFormatProperties->pNext) {
      vk_foreach_struct_const(src, pImageFormatProperties->pNext) {
         switch (src->sType) {
         case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES: {
            cache_entry->properties.ext_image =
               *((VkExternalImageFormatProperties *)src);
            break;
         }
         case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY: {
            cache_entry->properties.host_copy =
               *((VkHostImageCopyDevicePerformanceQuery *)src);
            break;
         }
         case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_PROPERTIES_EXT: {
            cache_entry->properties.compression =
               *((VkImageCompressionPropertiesEXT *)src);
            break;
         }
         case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: {
            cache_entry->properties.ycbcr_conversion =
               *((VkSamplerYcbcrConversionImageFormatProperties *)src);
            break;
         }
         case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT: {
            cache_entry->properties.filter_cubic =
               *((VkFilterCubicImageViewImageFormatPropertiesEXT *)src);
            break;
         }
         default:
            UNREACHABLE("unexpected format props pNext");
         }
      }
   }

   cache_entry->properties.format = *pImageFormatProperties;
   cache_entry->properties.cached_result = cached_result;

   memcpy(cache_entry->key, key, SHA1_DIGEST_LENGTH);

   _mesa_hash_table_insert(cache->ht, cache_entry->key, cache_entry);
   list_add(&cache_entry->head, &cache->lru);

   simple_mtx_unlock(&cache->mutex);
}

static inline void
vn_sanitize_image_format_properties(
   struct vn_physical_device *physical_dev,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   const VkPhysicalDeviceExternalImageFormatInfo *external_info,
   VkImageFormatProperties2 *props)
{
   const VkExternalMemoryHandleTypeFlagBits renderer_handle_type =
      physical_dev->external_memory.renderer_handle_type;
   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      physical_dev->external_memory.supported_handle_types;

   /* TODO drop this after supporting VK_EXT_rgba10x6_formats */
   if (info->format == VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16) {
      props->imageFormatProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   }

   /* sanitize VkExternalMemoryProperties */
   VkExternalImageFormatProperties *external_props =
      vk_find_struct(props->pNext, EXTERNAL_IMAGE_FORMAT_PROPERTIES);
   if (external_props) {
      VkExternalMemoryProperties *mem_props =
         &external_props->externalMemoryProperties;
      /* export support is via virtgpu but import relies on the renderer */
      if (!physical_dev->instance->renderer->info.has_dma_buf_import) {
         mem_props->externalMemoryFeatures &=
            ~VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      }

      mem_props->compatibleHandleTypes = supported_handle_types;
      mem_props->exportFromImportedHandleTypes =
         (mem_props->exportFromImportedHandleTypes & renderer_handle_type)
            ? supported_handle_types
            : 0;
   }
}

static VkResult
vn_get_ahb_image_props(struct vn_physical_device *physical_dev,
                       const VkPhysicalDeviceImageFormatInfo2 *info,
                       VkImageFormatProperties2 *props)
{
   /* Only advertise support for optimal tiling since venus is unable to
    * detiling if the AHB allocated outside is tiled. Internally venus always
    * overrides the tiling to VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT.
    */
   if (info->tiling != VK_IMAGE_TILING_OPTIMAL)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* No need to support AHB + VK_EXT_separate_stencil_usage. */
   if (vk_find_struct_const(info->pNext, IMAGE_STENCIL_USAGE_CREATE_INFO))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* No need to support AHB + VK_EXT_filter_cubic. */
   if (vk_find_struct_const(info->pNext,
                            PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* No need to check mutable format list info since the mutable bit does not
    * make a difference of the AHB allocation. In practice, AHB image would
    * only mutate between unorm and srgb, and later spec has allowed the impl
    * to fail the non-srgb mutation. For simplicity, we skip the additional
    * check here.
    */
   VkResult result = vk_android_get_ahb_image_properties(
      vn_physical_device_to_handle(physical_dev), info, props);
   if (result != VK_SUCCESS)
      return result;

   /* Now properly fill all the default props. */

   const struct vk_properties *vk_props = &physical_dev->base.vk.properties;
   const uint32_t extent = info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
                              ? vk_props->maxImageDimensionCube
                              : vk_props->maxImageDimension2D;
   props->imageFormatProperties = (VkImageFormatProperties){
      .maxExtent =
         (VkExtent3D){
            .width = extent,
            .height = extent,
            .depth = 1,
         },
      .maxMipLevels = 1,
      .maxArrayLayers = 1,
      .sampleCounts = VK_SAMPLE_COUNT_1_BIT,
      .maxResourceSize = 1 << 31,
   };

   /* AHB + VK_KHR_sampler_ycbcr_conversion */
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr = vk_find_struct(
      props->pNext, SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES);
   if (ycbcr)
      ycbcr->combinedImageSamplerDescriptorCount = 1;

   /* AHB + VK_EXT_host_image_copy */
   VkHostImageCopyDevicePerformanceQuery *host_copy =
      vk_find_struct(props->pNext, HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY);
   if (host_copy) {
      /* Venus always prefers device side async blit to host side copy. */
      host_copy->optimalDeviceAccess = VK_FALSE;
      host_copy->identicalMemoryLayout = VK_FALSE;
   }

   return VK_SUCCESS;
}

VkResult
vn_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;
   const VkExternalMemoryHandleTypeFlagBits renderer_handle_type =
      physical_dev->external_memory.renderer_handle_type;
   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      physical_dev->external_memory.supported_handle_types;

   const struct wsi_image_create_info *wsi_info = vk_find_struct_const(
      pImageFormatInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   if (wsi_info &&
       !vn_wsi_validate_image_format_info(physical_dev, pImageFormatInfo)) {
      return vn_error(physical_dev->instance, VK_ERROR_FORMAT_NOT_SUPPORTED);
   }

   const VkPhysicalDeviceExternalImageFormatInfo *external_info =
      vk_find_struct_const(pImageFormatInfo->pNext,
                           PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
   if (external_info) {
      if (!external_info->handleType) {
         external_info = NULL;
      } else if (!(external_info->handleType & supported_handle_types)) {
         return vn_error(physical_dev->instance,
                         VK_ERROR_FORMAT_NOT_SUPPORTED);
      }

      /* Fully resolve AHB image format query on the driver side. */
      if (external_info->handleType ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
         assert(physical_dev->instance->renderer->info.has_dma_buf_import);
         return vn_get_ahb_image_props(physical_dev, pImageFormatInfo,
                                       pImageFormatProperties);
      }
   }

   struct vn_physical_device_image_format_info local_info;
   if (external_info) {
      assert(
         external_info->handleType !=
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID);

      /* venus doesn't support legacy tiling for scanout purpose */
      if (renderer_handle_type ==
             VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT &&
          pImageFormatInfo->tiling !=
             VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
         return vn_error(physical_dev->instance,
                         VK_ERROR_FORMAT_NOT_SUPPORTED);
      }

      if (external_info->handleType != renderer_handle_type) {
         pImageFormatInfo = vn_physical_device_fix_image_format_info(
            pImageFormatInfo, renderer_handle_type, &local_info);
         if (!pImageFormatInfo) {
            return vn_error(physical_dev->instance,
                            VK_ERROR_FORMAT_NOT_SUPPORTED);
         }
      }
   }

   /* Since venus-protocol doesn't pass the wsi_image_create_info struct, we
    * must remove the ALIAS_BIT here and in vn_wsi_create_image().
    * ANV rejects the bit for external+nonlinear images that don't have WSI
    * info chained.
    */
   if (wsi_info && physical_dev->renderer_driver_id ==
                      VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA) {
      assert(!external_info);
      local_info.format = *pImageFormatInfo;
      local_info.format.flags &= ~VK_IMAGE_CREATE_ALIAS_BIT;
      pImageFormatInfo = &local_info.format;
   }

   /* Check if image format props is in the cache. */
   uint8_t key[SHA1_DIGEST_LENGTH] = { 0 };
   const bool cacheable = vn_image_get_image_format_key(
      physical_dev, pImageFormatInfo, pImageFormatProperties, key);

   VkResult result = VK_SUCCESS;
   if (!(cacheable &&
         vn_image_init_format_from_cache(physical_dev, pImageFormatProperties,
                                         &result, key))) {
      result = vn_call_vkGetPhysicalDeviceImageFormatProperties2(
         ring, physicalDevice, pImageFormatInfo, pImageFormatProperties);

      if (result == VK_SUCCESS) {
         vn_sanitize_image_format_properties(physical_dev, pImageFormatInfo,
                                             external_info,
                                             pImageFormatProperties);
      }

      /* If cacheable, cache successful and unsupported results. */
      if (cacheable &&
          (result == VK_SUCCESS || result == VK_ERROR_FORMAT_NOT_SUPPORTED ||
           result == VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR)) {
         vn_image_store_format_in_cache(physical_dev, key,
                                        pImageFormatProperties, result);
      }
   }

   return vn_result(physical_dev->instance, result);
}

void
vn_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{

   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;
   /* If VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT is not supported for the given
    * arguments, pPropertyCount will be set to zero upon return, and no data
    * will be written to pProperties.
    */
   if (physical_dev->sparse_binding_disabled) {
      *pPropertyCount = 0;
      return;
   }

   /* TODO per-device cache */
   vn_call_vkGetPhysicalDeviceSparseImageFormatProperties2(
      ring, physicalDevice, pFormatInfo, pPropertyCount, pProperties);
}

void
vn_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;
   const VkExternalMemoryHandleTypeFlagBits renderer_handle_type =
      physical_dev->external_memory.renderer_handle_type;
   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      physical_dev->external_memory.supported_handle_types;
   const bool is_ahb =
      pExternalBufferInfo->handleType ==
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

   VkExternalMemoryProperties *props =
      &pExternalBufferProperties->externalMemoryProperties;
   if (!(pExternalBufferInfo->handleType & supported_handle_types)) {
      props->compatibleHandleTypes = pExternalBufferInfo->handleType;
      props->exportFromImportedHandleTypes = 0;
      props->externalMemoryFeatures = 0;
      return;
   }

   if (is_ahb) {
      assert(physical_dev->instance->renderer->info.has_dma_buf_import);
      vk_android_get_ahb_buffer_properties(
         physicalDevice, pExternalBufferInfo, pExternalBufferProperties);
      return;
   }

   VkPhysicalDeviceExternalBufferInfo local_info;
   if (pExternalBufferInfo->handleType != renderer_handle_type) {
      local_info = *pExternalBufferInfo;
      local_info.handleType = renderer_handle_type;
      pExternalBufferInfo = &local_info;
   }

   /* TODO per-device cache */
   vn_call_vkGetPhysicalDeviceExternalBufferProperties(
      ring, physicalDevice, pExternalBufferInfo, pExternalBufferProperties);

   if (renderer_handle_type ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT &&
       !physical_dev->instance->renderer->info.has_dma_buf_import) {
      props->externalMemoryFeatures &=
         ~VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
   }

   props->compatibleHandleTypes = supported_handle_types;
   props->exportFromImportedHandleTypes =
      (props->exportFromImportedHandleTypes & renderer_handle_type)
         ? supported_handle_types
         : 0;
}

void
vn_GetPhysicalDeviceExternalFenceProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
   VkExternalFenceProperties *pExternalFenceProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   if (pExternalFenceInfo->handleType &
       physical_dev->external_fence_handles) {
      pExternalFenceProperties->compatibleHandleTypes =
         physical_dev->external_fence_handles;
      pExternalFenceProperties->exportFromImportedHandleTypes =
         physical_dev->external_fence_handles;
      pExternalFenceProperties->externalFenceFeatures =
         VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT;
   } else {
      pExternalFenceProperties->compatibleHandleTypes = 0;
      pExternalFenceProperties->exportFromImportedHandleTypes = 0;
      pExternalFenceProperties->externalFenceFeatures = 0;
   }
}

void
vn_GetPhysicalDeviceExternalSemaphoreProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
   VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   const VkSemaphoreTypeCreateInfo *type_info = vk_find_struct_const(
      pExternalSemaphoreInfo->pNext, SEMAPHORE_TYPE_CREATE_INFO);
   const VkSemaphoreType sem_type =
      type_info ? type_info->semaphoreType : VK_SEMAPHORE_TYPE_BINARY;
   const VkExternalSemaphoreHandleTypeFlags valid_handles =
      sem_type == VK_SEMAPHORE_TYPE_BINARY
         ? physical_dev->external_binary_semaphore_handles
         : physical_dev->external_timeline_semaphore_handles;
   if (pExternalSemaphoreInfo->handleType & valid_handles) {
      pExternalSemaphoreProperties->compatibleHandleTypes = valid_handles;
      pExternalSemaphoreProperties->exportFromImportedHandleTypes =
         valid_handles;
      pExternalSemaphoreProperties->externalSemaphoreFeatures =
         VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   } else {
      pExternalSemaphoreProperties->compatibleHandleTypes = 0;
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
   }
}

VkResult
vn_GetPhysicalDeviceCalibrateableTimeDomainsKHR(
   VkPhysicalDevice physicalDevice,
   uint32_t *pTimeDomainCount,
   VkTimeDomainKHR *pTimeDomains)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;

   return vn_call_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR(
      ring, physicalDevice, pTimeDomainCount, pTimeDomains);
}

VkResult
vn_GetPhysicalDeviceCooperativeMatrixPropertiesKHR(
   VkPhysicalDevice physicalDevice,
   uint32_t *pPropertyCount,
   VkCooperativeMatrixPropertiesKHR *pProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;

   return vn_call_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR(
      ring, physicalDevice, pPropertyCount, pProperties);
}

VkResult
vn_GetPhysicalDeviceFragmentShadingRatesKHR(
   VkPhysicalDevice physicalDevice,
   uint32_t *pFragmentShadingRateCount,
   VkPhysicalDeviceFragmentShadingRateKHR *pFragmentShadingRates)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;

   return vn_call_vkGetPhysicalDeviceFragmentShadingRatesKHR(
      ring, physicalDevice, pFragmentShadingRateCount, pFragmentShadingRates);
}

void
vn_GetPhysicalDeviceMultisamplePropertiesEXT(
   VkPhysicalDevice physicalDevice,
   VkSampleCountFlagBits samples,
   VkMultisamplePropertiesEXT *pMultisampleProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   struct vn_ring *ring = physical_dev->instance->ring.ring;

   /* TODO per-device cache */
   vn_call_vkGetPhysicalDeviceMultisamplePropertiesEXT(
      ring, physicalDevice, samples, pMultisampleProperties);
}
