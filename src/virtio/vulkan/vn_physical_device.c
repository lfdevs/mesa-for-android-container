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
#include "venus-protocol/vn_protocol_driver_device.h"

#include "vn_android.h"
#include "vn_instance.h"

#define VN_EXTENSION_TABLE_INDEX(tbl, ext)                                   \
   ((const bool *)((const void *)(&(tbl)) +                                  \
                   offsetof(__typeof__(tbl), ext)) -                         \
    (tbl).extensions)

#define VN_PREFIX_STYPE(stype) (VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_##stype)
#define VN_ADD_TO_PNEXT(elem, s_type, head)                                  \
   do {                                                                      \
      (elem).sType = VN_PREFIX_STYPE(s_type);                                \
      (elem).pNext = (head).pNext;                                           \
      (head).pNext = &(elem);                                                \
   } while (0)
#define VN_ADD_EXT_TO_PNEXT(ext, elem, s_type, head)                         \
   if (ext) VN_ADD_TO_PNEXT(elem, s_type, head)

static void
vn_physical_device_init_features(struct vn_physical_device *physical_dev)
{
   struct vn_physical_device_features *feats = &physical_dev->features;
   struct vn_instance *instance = physical_dev->instance;
   const struct vk_device_extension_table *exts =
      &physical_dev->renderer_extensions;
   VkPhysicalDeviceFeatures2 features2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
   };
   struct {
      /* Vulkan 1.1 */
      VkPhysicalDevice16BitStorageFeatures sixteen_bit_storage;
      VkPhysicalDeviceMultiviewFeatures multiview;
      VkPhysicalDeviceVariablePointersFeatures variable_pointers;
      VkPhysicalDeviceProtectedMemoryFeatures protected_memory;
      VkPhysicalDeviceSamplerYcbcrConversionFeatures sampler_ycbcr_conversion;
      VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters;

      /* Vulkan 1.2 */
      VkPhysicalDevice8BitStorageFeatures eight_bit_storage;
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
   } local_feats;

   if (physical_dev->renderer_version >= VK_API_VERSION_1_2) {
      VN_ADD_TO_PNEXT(feats->vulkan_1_1, VULKAN_1_1_FEATURES, features2);
      VN_ADD_TO_PNEXT(feats->vulkan_1_2, VULKAN_1_2_FEATURES, features2);
   } else {
      /* Vulkan 1.1 */
      VN_ADD_TO_PNEXT(local_feats.sixteen_bit_storage, 16BIT_STORAGE_FEATURES,
                      features2);
      VN_ADD_TO_PNEXT(local_feats.multiview, MULTIVIEW_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.variable_pointers,
                      VARIABLE_POINTERS_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.protected_memory, PROTECTED_MEMORY_FEATURES,
                      features2);
      VN_ADD_TO_PNEXT(local_feats.sampler_ycbcr_conversion,
                      SAMPLER_YCBCR_CONVERSION_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.shader_draw_parameters,
                      SHADER_DRAW_PARAMETERS_FEATURES, features2);

      /* Vulkan 1.2 */
      VN_ADD_TO_PNEXT(local_feats.eight_bit_storage, 8BIT_STORAGE_FEATURES,
                      features2);
      VN_ADD_TO_PNEXT(local_feats.shader_atomic_int64,
                      SHADER_ATOMIC_INT64_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.shader_float16_int8,
                      SHADER_FLOAT16_INT8_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.descriptor_indexing,
                      DESCRIPTOR_INDEXING_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.scalar_block_layout,
                      SCALAR_BLOCK_LAYOUT_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.imageless_framebuffer,
                      IMAGELESS_FRAMEBUFFER_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.uniform_buffer_standard_layout,
                      UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.shader_subgroup_extended_types,
                      SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.separate_depth_stencil_layouts,
                      SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.host_query_reset, HOST_QUERY_RESET_FEATURES,
                      features2);
      VN_ADD_TO_PNEXT(local_feats.timeline_semaphore,
                      TIMELINE_SEMAPHORE_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.buffer_device_address,
                      BUFFER_DEVICE_ADDRESS_FEATURES, features2);
      VN_ADD_TO_PNEXT(local_feats.vulkan_memory_model,
                      VULKAN_MEMORY_MODEL_FEATURES, features2);
   }

   /* Vulkan 1.3 */
   VN_ADD_EXT_TO_PNEXT(exts->EXT_4444_formats, feats->argb_4444_formats,
                       4444_FORMATS_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_extended_dynamic_state,
                       feats->extended_dynamic_state,
                       EXTENDED_DYNAMIC_STATE_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_extended_dynamic_state2,
                       feats->extended_dynamic_state_2,
                       EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_image_robustness, feats->image_robustness,
                       IMAGE_ROBUSTNESS_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_inline_uniform_block,
                       feats->inline_uniform_block,
                       INLINE_UNIFORM_BLOCK_FEATURES, features2);
   VN_ADD_EXT_TO_PNEXT(exts->KHR_dynamic_rendering, feats->dynamic_rendering,
                       DYNAMIC_RENDERING_FEATURES, features2);
   VN_ADD_EXT_TO_PNEXT(exts->KHR_maintenance4, feats->maintenance4,
                       MAINTENANCE_4_FEATURES, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_shader_demote_to_helper_invocation,
                       feats->shader_demote_to_helper_invocation,
                       SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES,
                       features2);

   /* EXT */
   VN_ADD_EXT_TO_PNEXT(exts->EXT_conditional_rendering,
                       feats->conditional_rendering,
                       CONDITIONAL_RENDERING_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_custom_border_color,
                       feats->custom_border_color,
                       CUSTOM_BORDER_COLOR_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_depth_clip_enable, feats->depth_clip_enable,
                       DEPTH_CLIP_ENABLE_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_image_view_min_lod, feats->image_view_min_lod,
                       IMAGE_VIEW_MIN_LOD_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_index_type_uint8, feats->index_type_uint8,
                       INDEX_TYPE_UINT8_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_line_rasterization,
                       feats->line_rasterization,
                       LINE_RASTERIZATION_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_provoking_vertex, feats->provoking_vertex,
                       PROVOKING_VERTEX_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_robustness2, feats->robustness_2,
                       ROBUSTNESS_2_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_transform_feedback,
                       feats->transform_feedback,
                       TRANSFORM_FEEDBACK_FEATURES_EXT, features2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_vertex_attribute_divisor,
                       feats->vertex_attribute_divisor,
                       VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT, features2);

   vn_call_vkGetPhysicalDeviceFeatures2(
      instance, vn_physical_device_to_handle(physical_dev), &features2);

   feats->vulkan_1_0 = features2.features;

   /* TODO allow sparse resource along with sync feedback
    *
    * vkQueueBindSparse relies on explicit sync primitives. To intercept the
    * timeline semaphores within each bind info to write the feedback buffer,
    * we have to split the call into bindInfoCount number of calls while
    * inserting vkQueueSubmit to wait on the signal timeline semaphores before
    * filling the feedback buffer. To intercept the fence to be signaled, we
    * have to relocate the fence to another vkQueueSubmit call and potentially
    * have to use an internal timeline semaphore to synchronize between them.
    * Those would make the code overly complex, so we disable sparse binding
    * for simplicity.
    */
   if (!VN_PERF(NO_FENCE_FEEDBACK)) {
      feats->vulkan_1_0.sparseBinding = false;
      feats->vulkan_1_0.sparseResidencyBuffer = false;
      feats->vulkan_1_0.sparseResidencyImage2D = false;
      feats->vulkan_1_0.sparseResidencyImage3D = false;
      feats->vulkan_1_0.sparseResidency2Samples = false;
      feats->vulkan_1_0.sparseResidency4Samples = false;
      feats->vulkan_1_0.sparseResidency8Samples = false;
      feats->vulkan_1_0.sparseResidency16Samples = false;
      feats->vulkan_1_0.sparseResidencyAliased = false;
   }

   struct VkPhysicalDeviceVulkan11Features *vk11_feats = &feats->vulkan_1_1;
   struct VkPhysicalDeviceVulkan12Features *vk12_feats = &feats->vulkan_1_2;

   if (physical_dev->renderer_version < VK_API_VERSION_1_2) {
      vk11_feats->storageBuffer16BitAccess =
         local_feats.sixteen_bit_storage.storageBuffer16BitAccess;
      vk11_feats->uniformAndStorageBuffer16BitAccess =
         local_feats.sixteen_bit_storage.uniformAndStorageBuffer16BitAccess;
      vk11_feats->storagePushConstant16 =
         local_feats.sixteen_bit_storage.storagePushConstant16;
      vk11_feats->storageInputOutput16 =
         local_feats.sixteen_bit_storage.storageInputOutput16;

      vk11_feats->multiview = local_feats.multiview.multiview;
      vk11_feats->multiviewGeometryShader =
         local_feats.multiview.multiviewGeometryShader;
      vk11_feats->multiviewTessellationShader =
         local_feats.multiview.multiviewTessellationShader;

      vk11_feats->variablePointersStorageBuffer =
         local_feats.variable_pointers.variablePointersStorageBuffer;
      vk11_feats->variablePointers =
         local_feats.variable_pointers.variablePointers;

      vk11_feats->protectedMemory =
         local_feats.protected_memory.protectedMemory;

      vk11_feats->samplerYcbcrConversion =
         local_feats.sampler_ycbcr_conversion.samplerYcbcrConversion;

      vk11_feats->shaderDrawParameters =
         local_feats.shader_draw_parameters.shaderDrawParameters;

      vk12_feats->samplerMirrorClampToEdge =
         exts->KHR_sampler_mirror_clamp_to_edge;
      vk12_feats->drawIndirectCount = exts->KHR_draw_indirect_count;

      if (exts->KHR_8bit_storage) {
         vk12_feats->storageBuffer8BitAccess =
            local_feats.eight_bit_storage.storageBuffer8BitAccess;
         vk12_feats->uniformAndStorageBuffer8BitAccess =
            local_feats.eight_bit_storage.uniformAndStorageBuffer8BitAccess;
         vk12_feats->storagePushConstant8 =
            local_feats.eight_bit_storage.storagePushConstant8;
      }
      if (exts->KHR_shader_atomic_int64) {
         vk12_feats->shaderBufferInt64Atomics =
            local_feats.shader_atomic_int64.shaderBufferInt64Atomics;
         vk12_feats->shaderSharedInt64Atomics =
            local_feats.shader_atomic_int64.shaderSharedInt64Atomics;
      }
      if (exts->KHR_shader_float16_int8) {
         vk12_feats->shaderFloat16 =
            local_feats.shader_float16_int8.shaderFloat16;
         vk12_feats->shaderInt8 = local_feats.shader_float16_int8.shaderInt8;
      }
      if (exts->EXT_descriptor_indexing) {
         vk12_feats->descriptorIndexing = true;
         vk12_feats->shaderInputAttachmentArrayDynamicIndexing =
            local_feats.descriptor_indexing
               .shaderInputAttachmentArrayDynamicIndexing;
         vk12_feats->shaderUniformTexelBufferArrayDynamicIndexing =
            local_feats.descriptor_indexing
               .shaderUniformTexelBufferArrayDynamicIndexing;
         vk12_feats->shaderStorageTexelBufferArrayDynamicIndexing =
            local_feats.descriptor_indexing
               .shaderStorageTexelBufferArrayDynamicIndexing;
         vk12_feats->shaderUniformBufferArrayNonUniformIndexing =
            local_feats.descriptor_indexing
               .shaderUniformBufferArrayNonUniformIndexing;
         vk12_feats->shaderSampledImageArrayNonUniformIndexing =
            local_feats.descriptor_indexing
               .shaderSampledImageArrayNonUniformIndexing;
         vk12_feats->shaderStorageBufferArrayNonUniformIndexing =
            local_feats.descriptor_indexing
               .shaderStorageBufferArrayNonUniformIndexing;
         vk12_feats->shaderStorageImageArrayNonUniformIndexing =
            local_feats.descriptor_indexing
               .shaderStorageImageArrayNonUniformIndexing;
         vk12_feats->shaderInputAttachmentArrayNonUniformIndexing =
            local_feats.descriptor_indexing
               .shaderInputAttachmentArrayNonUniformIndexing;
         vk12_feats->shaderUniformTexelBufferArrayNonUniformIndexing =
            local_feats.descriptor_indexing
               .shaderUniformTexelBufferArrayNonUniformIndexing;
         vk12_feats->shaderStorageTexelBufferArrayNonUniformIndexing =
            local_feats.descriptor_indexing
               .shaderStorageTexelBufferArrayNonUniformIndexing;
         vk12_feats->descriptorBindingUniformBufferUpdateAfterBind =
            local_feats.descriptor_indexing
               .descriptorBindingUniformBufferUpdateAfterBind;
         vk12_feats->descriptorBindingSampledImageUpdateAfterBind =
            local_feats.descriptor_indexing
               .descriptorBindingSampledImageUpdateAfterBind;
         vk12_feats->descriptorBindingStorageImageUpdateAfterBind =
            local_feats.descriptor_indexing
               .descriptorBindingStorageImageUpdateAfterBind;
         vk12_feats->descriptorBindingStorageBufferUpdateAfterBind =
            local_feats.descriptor_indexing
               .descriptorBindingStorageBufferUpdateAfterBind;
         vk12_feats->descriptorBindingUniformTexelBufferUpdateAfterBind =
            local_feats.descriptor_indexing
               .descriptorBindingUniformTexelBufferUpdateAfterBind;
         vk12_feats->descriptorBindingStorageTexelBufferUpdateAfterBind =
            local_feats.descriptor_indexing
               .descriptorBindingStorageTexelBufferUpdateAfterBind;
         vk12_feats->descriptorBindingUpdateUnusedWhilePending =
            local_feats.descriptor_indexing
               .descriptorBindingUpdateUnusedWhilePending;
         vk12_feats->descriptorBindingPartiallyBound =
            local_feats.descriptor_indexing.descriptorBindingPartiallyBound;
         vk12_feats->descriptorBindingVariableDescriptorCount =
            local_feats.descriptor_indexing
               .descriptorBindingVariableDescriptorCount;
         vk12_feats->runtimeDescriptorArray =
            local_feats.descriptor_indexing.runtimeDescriptorArray;
      }

      vk12_feats->samplerFilterMinmax = exts->EXT_sampler_filter_minmax;

      if (exts->EXT_scalar_block_layout) {
         vk12_feats->scalarBlockLayout =
            local_feats.scalar_block_layout.scalarBlockLayout;
      }
      if (exts->KHR_imageless_framebuffer) {
         vk12_feats->imagelessFramebuffer =
            local_feats.imageless_framebuffer.imagelessFramebuffer;
      }
      if (exts->KHR_uniform_buffer_standard_layout) {
         vk12_feats->uniformBufferStandardLayout =
            local_feats.uniform_buffer_standard_layout
               .uniformBufferStandardLayout;
      }
      if (exts->KHR_shader_subgroup_extended_types) {
         vk12_feats->shaderSubgroupExtendedTypes =
            local_feats.shader_subgroup_extended_types
               .shaderSubgroupExtendedTypes;
      }
      if (exts->KHR_separate_depth_stencil_layouts) {
         vk12_feats->separateDepthStencilLayouts =
            local_feats.separate_depth_stencil_layouts
               .separateDepthStencilLayouts;
      }
      if (exts->EXT_host_query_reset) {
         vk12_feats->hostQueryReset =
            local_feats.host_query_reset.hostQueryReset;
      }
      if (exts->KHR_timeline_semaphore) {
         vk12_feats->timelineSemaphore =
            local_feats.timeline_semaphore.timelineSemaphore;
      }
      if (exts->KHR_buffer_device_address) {
         vk12_feats->bufferDeviceAddress =
            local_feats.buffer_device_address.bufferDeviceAddress;
         vk12_feats->bufferDeviceAddressCaptureReplay =
            local_feats.buffer_device_address.bufferDeviceAddressCaptureReplay;
         vk12_feats->bufferDeviceAddressMultiDevice =
            local_feats.buffer_device_address.bufferDeviceAddressMultiDevice;
      }
      if (exts->KHR_vulkan_memory_model) {
         vk12_feats->vulkanMemoryModel =
            local_feats.vulkan_memory_model.vulkanMemoryModel;
         vk12_feats->vulkanMemoryModelDeviceScope =
            local_feats.vulkan_memory_model.vulkanMemoryModelDeviceScope;
         vk12_feats->vulkanMemoryModelAvailabilityVisibilityChains =
            local_feats.vulkan_memory_model
               .vulkanMemoryModelAvailabilityVisibilityChains;
      }

      vk12_feats->shaderOutputViewportIndex =
         exts->EXT_shader_viewport_index_layer;
      vk12_feats->shaderOutputLayer = exts->EXT_shader_viewport_index_layer;
      vk12_feats->subgroupBroadcastDynamicId = false;
   }
}

static void
vn_physical_device_init_uuids(struct vn_physical_device *physical_dev)
{
   struct vn_physical_device_properties *props = &physical_dev->properties;
   struct VkPhysicalDeviceProperties *vk10_props = &props->vulkan_1_0;
   struct VkPhysicalDeviceVulkan11Properties *vk11_props = &props->vulkan_1_1;
   struct VkPhysicalDeviceVulkan12Properties *vk12_props = &props->vulkan_1_2;
   struct mesa_sha1 sha1_ctx;
   uint8_t sha1[SHA1_DIGEST_LENGTH];

   static_assert(VK_UUID_SIZE <= SHA1_DIGEST_LENGTH, "");

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, &vk10_props->pipelineCacheUUID,
                     sizeof(vk10_props->pipelineCacheUUID));
   _mesa_sha1_final(&sha1_ctx, sha1);

   memcpy(vk10_props->pipelineCacheUUID, sha1, VK_UUID_SIZE);

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, &vk10_props->vendorID,
                     sizeof(vk10_props->vendorID));
   _mesa_sha1_update(&sha1_ctx, &vk10_props->deviceID,
                     sizeof(vk10_props->deviceID));
   _mesa_sha1_final(&sha1_ctx, sha1);

   memcpy(vk11_props->deviceUUID, sha1, VK_UUID_SIZE);

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, vk12_props->driverName,
                     strlen(vk12_props->driverName));
   _mesa_sha1_update(&sha1_ctx, vk12_props->driverInfo,
                     strlen(vk12_props->driverInfo));
   _mesa_sha1_final(&sha1_ctx, sha1);

   memcpy(vk11_props->driverUUID, sha1, VK_UUID_SIZE);

   memset(vk11_props->deviceLUID, 0, VK_LUID_SIZE);
   vk11_props->deviceNodeMask = 0;
   vk11_props->deviceLUIDValid = false;
}

static void
vn_physical_device_init_properties(struct vn_physical_device *physical_dev)
{
   struct vn_physical_device_properties *props = &physical_dev->properties;
   struct vn_instance *instance = physical_dev->instance;
   const struct vk_device_extension_table *exts =
      &physical_dev->renderer_extensions;
   VkPhysicalDeviceProperties2 properties2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };
   struct {
      /* Vulkan 1.1 */
      VkPhysicalDeviceIDProperties id;
      VkPhysicalDeviceSubgroupProperties subgroup;
      VkPhysicalDevicePointClippingProperties point_clipping;
      VkPhysicalDeviceMultiviewProperties multiview;
      VkPhysicalDeviceProtectedMemoryProperties protected_memory;
      VkPhysicalDeviceMaintenance3Properties maintenance_3;

      /* Vulkan 1.2 */
      VkPhysicalDeviceDriverProperties driver;
      VkPhysicalDeviceFloatControlsProperties float_controls;
      VkPhysicalDeviceDescriptorIndexingProperties descriptor_indexing;
      VkPhysicalDeviceDepthStencilResolveProperties depth_stencil_resolve;
      VkPhysicalDeviceSamplerFilterMinmaxProperties sampler_filter_minmax;
      VkPhysicalDeviceTimelineSemaphoreProperties timeline_semaphore;
   } local_props;

   if (physical_dev->renderer_version >= VK_API_VERSION_1_2) {
      VN_ADD_TO_PNEXT(props->vulkan_1_1, VULKAN_1_1_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(props->vulkan_1_2, VULKAN_1_2_PROPERTIES, properties2);
   } else {
      /* Vulkan 1.1 */
      VN_ADD_TO_PNEXT(local_props.id, ID_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(local_props.subgroup, SUBGROUP_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(local_props.point_clipping, POINT_CLIPPING_PROPERTIES,
                      properties2);
      VN_ADD_TO_PNEXT(local_props.multiview, MULTIVIEW_PROPERTIES,
                      properties2);
      VN_ADD_TO_PNEXT(local_props.protected_memory,
                      PROTECTED_MEMORY_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(local_props.maintenance_3, MAINTENANCE_3_PROPERTIES,
                      properties2);

      /* Vulkan 1.2 */
      VN_ADD_TO_PNEXT(local_props.driver, DRIVER_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(local_props.float_controls, FLOAT_CONTROLS_PROPERTIES,
                      properties2);
      VN_ADD_TO_PNEXT(local_props.descriptor_indexing,
                      DESCRIPTOR_INDEXING_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(local_props.depth_stencil_resolve,
                      DEPTH_STENCIL_RESOLVE_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(local_props.sampler_filter_minmax,
                      SAMPLER_FILTER_MINMAX_PROPERTIES, properties2);
      VN_ADD_TO_PNEXT(local_props.timeline_semaphore,
                      TIMELINE_SEMAPHORE_PROPERTIES, properties2);
   }

   /* Vulkan 1.3 */
   VN_ADD_EXT_TO_PNEXT(exts->EXT_inline_uniform_block,
                       props->inline_uniform_block,
                       INLINE_UNIFORM_BLOCK_PROPERTIES, properties2);

   /* EXT */
   VN_ADD_EXT_TO_PNEXT(
      exts->EXT_conservative_rasterization, props->conservative_rasterization,
      CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, properties2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_custom_border_color,
                       props->custom_border_color,
                       CUSTOM_BORDER_COLOR_PROPERTIES_EXT, properties2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_line_rasterization,
                       props->line_rasterization,
                       LINE_RASTERIZATION_PROPERTIES_EXT, properties2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_provoking_vertex, props->provoking_vertex,
                       PROVOKING_VERTEX_PROPERTIES_EXT, properties2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_robustness2, props->robustness_2,
                       ROBUSTNESS_2_PROPERTIES_EXT, properties2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_transform_feedback,
                       props->transform_feedback,
                       TRANSFORM_FEEDBACK_PROPERTIES_EXT, properties2);
   VN_ADD_EXT_TO_PNEXT(exts->KHR_maintenance4, props->maintenance4,
                       MAINTENANCE_4_PROPERTIES, properties2);
   VN_ADD_EXT_TO_PNEXT(exts->EXT_vertex_attribute_divisor,
                       props->vertex_attribute_divisor,
                       VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT, properties2);

   vn_call_vkGetPhysicalDeviceProperties2(
      instance, vn_physical_device_to_handle(physical_dev), &properties2);

   props->vulkan_1_0 = properties2.properties;

   /* TODO allow sparse resource along with sync feedback */
   if (!VN_PERF(NO_FENCE_FEEDBACK)) {
      props->vulkan_1_0.limits.sparseAddressSpaceSize = 0;
      props->vulkan_1_0.sparseProperties =
         (VkPhysicalDeviceSparseProperties){ 0 };
   }

   struct VkPhysicalDeviceProperties *vk10_props = &props->vulkan_1_0;
   struct VkPhysicalDeviceVulkan11Properties *vk11_props = &props->vulkan_1_1;
   struct VkPhysicalDeviceVulkan12Properties *vk12_props = &props->vulkan_1_2;

   if (physical_dev->renderer_version < VK_API_VERSION_1_2) {
      memcpy(vk11_props->deviceUUID, local_props.id.deviceUUID,
             sizeof(vk11_props->deviceUUID));
      memcpy(vk11_props->driverUUID, local_props.id.driverUUID,
             sizeof(vk11_props->driverUUID));
      memcpy(vk11_props->deviceLUID, local_props.id.deviceLUID,
             sizeof(vk11_props->deviceLUID));
      vk11_props->deviceNodeMask = local_props.id.deviceNodeMask;
      vk11_props->deviceLUIDValid = local_props.id.deviceLUIDValid;

      vk11_props->subgroupSize = local_props.subgroup.subgroupSize;
      vk11_props->subgroupSupportedStages =
         local_props.subgroup.supportedStages;
      vk11_props->subgroupSupportedOperations =
         local_props.subgroup.supportedOperations;
      vk11_props->subgroupQuadOperationsInAllStages =
         local_props.subgroup.quadOperationsInAllStages;

      vk11_props->pointClippingBehavior =
         local_props.point_clipping.pointClippingBehavior;

      vk11_props->maxMultiviewViewCount =
         local_props.multiview.maxMultiviewViewCount;
      vk11_props->maxMultiviewInstanceIndex =
         local_props.multiview.maxMultiviewInstanceIndex;

      vk11_props->protectedNoFault =
         local_props.protected_memory.protectedNoFault;

      vk11_props->maxPerSetDescriptors =
         local_props.maintenance_3.maxPerSetDescriptors;
      vk11_props->maxMemoryAllocationSize =
         local_props.maintenance_3.maxMemoryAllocationSize;

      if (exts->KHR_driver_properties) {
         vk12_props->driverID = local_props.driver.driverID;
         memcpy(vk12_props->driverName, local_props.driver.driverName,
                VK_MAX_DRIVER_NAME_SIZE);
         memcpy(vk12_props->driverInfo, local_props.driver.driverInfo,
                VK_MAX_DRIVER_INFO_SIZE);
         vk12_props->conformanceVersion =
            local_props.driver.conformanceVersion;
      }
      if (exts->KHR_shader_float_controls) {
         vk12_props->denormBehaviorIndependence =
            local_props.float_controls.denormBehaviorIndependence;
         vk12_props->roundingModeIndependence =
            local_props.float_controls.roundingModeIndependence;
         vk12_props->shaderSignedZeroInfNanPreserveFloat16 =
            local_props.float_controls.shaderSignedZeroInfNanPreserveFloat16;
         vk12_props->shaderSignedZeroInfNanPreserveFloat32 =
            local_props.float_controls.shaderSignedZeroInfNanPreserveFloat32;
         vk12_props->shaderSignedZeroInfNanPreserveFloat64 =
            local_props.float_controls.shaderSignedZeroInfNanPreserveFloat64;
         vk12_props->shaderDenormPreserveFloat16 =
            local_props.float_controls.shaderDenormPreserveFloat16;
         vk12_props->shaderDenormPreserveFloat32 =
            local_props.float_controls.shaderDenormPreserveFloat32;
         vk12_props->shaderDenormPreserveFloat64 =
            local_props.float_controls.shaderDenormPreserveFloat64;
         vk12_props->shaderDenormFlushToZeroFloat16 =
            local_props.float_controls.shaderDenormFlushToZeroFloat16;
         vk12_props->shaderDenormFlushToZeroFloat32 =
            local_props.float_controls.shaderDenormFlushToZeroFloat32;
         vk12_props->shaderDenormFlushToZeroFloat64 =
            local_props.float_controls.shaderDenormFlushToZeroFloat64;
         vk12_props->shaderRoundingModeRTEFloat16 =
            local_props.float_controls.shaderRoundingModeRTEFloat16;
         vk12_props->shaderRoundingModeRTEFloat32 =
            local_props.float_controls.shaderRoundingModeRTEFloat32;
         vk12_props->shaderRoundingModeRTEFloat64 =
            local_props.float_controls.shaderRoundingModeRTEFloat64;
         vk12_props->shaderRoundingModeRTZFloat16 =
            local_props.float_controls.shaderRoundingModeRTZFloat16;
         vk12_props->shaderRoundingModeRTZFloat32 =
            local_props.float_controls.shaderRoundingModeRTZFloat32;
         vk12_props->shaderRoundingModeRTZFloat64 =
            local_props.float_controls.shaderRoundingModeRTZFloat64;
      }
      if (exts->EXT_descriptor_indexing) {
         vk12_props->maxUpdateAfterBindDescriptorsInAllPools =
            local_props.descriptor_indexing
               .maxUpdateAfterBindDescriptorsInAllPools;
         vk12_props->shaderUniformBufferArrayNonUniformIndexingNative =
            local_props.descriptor_indexing
               .shaderUniformBufferArrayNonUniformIndexingNative;
         vk12_props->shaderSampledImageArrayNonUniformIndexingNative =
            local_props.descriptor_indexing
               .shaderSampledImageArrayNonUniformIndexingNative;
         vk12_props->shaderStorageBufferArrayNonUniformIndexingNative =
            local_props.descriptor_indexing
               .shaderStorageBufferArrayNonUniformIndexingNative;
         vk12_props->shaderStorageImageArrayNonUniformIndexingNative =
            local_props.descriptor_indexing
               .shaderStorageImageArrayNonUniformIndexingNative;
         vk12_props->shaderInputAttachmentArrayNonUniformIndexingNative =
            local_props.descriptor_indexing
               .shaderInputAttachmentArrayNonUniformIndexingNative;
         vk12_props->robustBufferAccessUpdateAfterBind =
            local_props.descriptor_indexing.robustBufferAccessUpdateAfterBind;
         vk12_props->quadDivergentImplicitLod =
            local_props.descriptor_indexing.quadDivergentImplicitLod;
         vk12_props->maxPerStageDescriptorUpdateAfterBindSamplers =
            local_props.descriptor_indexing
               .maxPerStageDescriptorUpdateAfterBindSamplers;
         vk12_props->maxPerStageDescriptorUpdateAfterBindUniformBuffers =
            local_props.descriptor_indexing
               .maxPerStageDescriptorUpdateAfterBindUniformBuffers;
         vk12_props->maxPerStageDescriptorUpdateAfterBindStorageBuffers =
            local_props.descriptor_indexing
               .maxPerStageDescriptorUpdateAfterBindStorageBuffers;
         vk12_props->maxPerStageDescriptorUpdateAfterBindSampledImages =
            local_props.descriptor_indexing
               .maxPerStageDescriptorUpdateAfterBindSampledImages;
         vk12_props->maxPerStageDescriptorUpdateAfterBindStorageImages =
            local_props.descriptor_indexing
               .maxPerStageDescriptorUpdateAfterBindStorageImages;
         vk12_props->maxPerStageDescriptorUpdateAfterBindInputAttachments =
            local_props.descriptor_indexing
               .maxPerStageDescriptorUpdateAfterBindInputAttachments;
         vk12_props->maxPerStageUpdateAfterBindResources =
            local_props.descriptor_indexing
               .maxPerStageUpdateAfterBindResources;
         vk12_props->maxDescriptorSetUpdateAfterBindSamplers =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindSamplers;
         vk12_props->maxDescriptorSetUpdateAfterBindUniformBuffers =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindUniformBuffers;
         vk12_props->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic;
         vk12_props->maxDescriptorSetUpdateAfterBindStorageBuffers =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindStorageBuffers;
         vk12_props->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic;
         vk12_props->maxDescriptorSetUpdateAfterBindSampledImages =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindSampledImages;
         vk12_props->maxDescriptorSetUpdateAfterBindStorageImages =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindStorageImages;
         vk12_props->maxDescriptorSetUpdateAfterBindInputAttachments =
            local_props.descriptor_indexing
               .maxDescriptorSetUpdateAfterBindInputAttachments;
      }
      if (exts->KHR_depth_stencil_resolve) {
         vk12_props->supportedDepthResolveModes =
            local_props.depth_stencil_resolve.supportedDepthResolveModes;
         vk12_props->supportedStencilResolveModes =
            local_props.depth_stencil_resolve.supportedStencilResolveModes;
         vk12_props->independentResolveNone =
            local_props.depth_stencil_resolve.independentResolveNone;
         vk12_props->independentResolve =
            local_props.depth_stencil_resolve.independentResolve;
      }
      if (exts->EXT_sampler_filter_minmax) {
         vk12_props->filterMinmaxSingleComponentFormats =
            local_props.sampler_filter_minmax
               .filterMinmaxSingleComponentFormats;
         vk12_props->filterMinmaxImageComponentMapping =
            local_props.sampler_filter_minmax
               .filterMinmaxImageComponentMapping;
      }
      if (exts->KHR_timeline_semaphore) {
         vk12_props->maxTimelineSemaphoreValueDifference =
            local_props.timeline_semaphore.maxTimelineSemaphoreValueDifference;
      }

      vk12_props->framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
   }

   const uint32_t version_override = vk_get_version_override();
   if (version_override) {
      vk10_props->apiVersion = version_override;
   } else {
      /* cap the advertised api version */
      uint32_t ver = MIN3(vk10_props->apiVersion, VN_MAX_API_VERSION,
                          instance->renderer->info.vk_xml_version);
      if (VK_VERSION_PATCH(ver) > VK_VERSION_PATCH(vk10_props->apiVersion)) {
         ver = ver - VK_VERSION_PATCH(ver) +
               VK_VERSION_PATCH(vk10_props->apiVersion);
      }
      vk10_props->apiVersion = ver;
   }

   vk10_props->driverVersion = vk_get_driver_version();

   char device_name[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
   int device_name_len =
      snprintf(device_name, sizeof(device_name), "Virtio-GPU Venus (%s)",
               vk10_props->deviceName);
   if (device_name_len >= VK_MAX_PHYSICAL_DEVICE_NAME_SIZE) {
      memcpy(device_name + VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 5, "...)", 4);
      device_name_len = VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1;
   }
   memcpy(vk10_props->deviceName, device_name, device_name_len + 1);

   vk12_props->driverID = VK_DRIVER_ID_MESA_VENUS;
   snprintf(vk12_props->driverName, sizeof(vk12_props->driverName), "venus");
   snprintf(vk12_props->driverInfo, sizeof(vk12_props->driverInfo),
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);
   vk12_props->conformanceVersion = (VkConformanceVersion){
      .major = 1,
      .minor = 2,
      .subminor = 7,
      .patch = 1,
   };

   vn_physical_device_init_uuids(physical_dev);
}

static VkResult
vn_physical_device_init_queue_family_properties(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;
   uint32_t count;

   vn_call_vkGetPhysicalDeviceQueueFamilyProperties2(
      instance, vn_physical_device_to_handle(physical_dev), &count, NULL);

   VkQueueFamilyProperties2 *props =
      vk_alloc(alloc, sizeof(*props) * count, VN_DEFAULT_ALIGN,
               VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!props)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   for (uint32_t i = 0; i < count; i++) {
      props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
      props[i].pNext = NULL;
   }
   vn_call_vkGetPhysicalDeviceQueueFamilyProperties2(
      instance, vn_physical_device_to_handle(physical_dev), &count, props);

   physical_dev->queue_family_properties = props;
   physical_dev->queue_family_count = count;

   return VK_SUCCESS;
}

static void
vn_physical_device_init_memory_properties(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;

   physical_dev->memory_properties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;

   vn_call_vkGetPhysicalDeviceMemoryProperties2(
      instance, vn_physical_device_to_handle(physical_dev),
      &physical_dev->memory_properties);

   if (!instance->renderer->info.has_cache_management) {
      VkPhysicalDeviceMemoryProperties *props =
         &physical_dev->memory_properties.memoryProperties;
      const uint32_t host_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                  VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

      for (uint32_t i = 0; i < props->memoryTypeCount; i++) {
         const bool coherent = props->memoryTypes[i].propertyFlags &
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
         if (!coherent)
            props->memoryTypes[i].propertyFlags &= ~host_flags;
      }
   }
}

static void
vn_physical_device_init_external_memory(
   struct vn_physical_device *physical_dev)
{
   /* When a renderer VkDeviceMemory is exportable, we can create a
    * vn_renderer_bo from it.  The vn_renderer_bo can be freely exported as an
    * opaque fd or a dma-buf.
    *
    * However, to know if a rendender VkDeviceMemory is exportable, we have to
    * start from VkPhysicalDeviceExternalImageFormatInfo (or
    * vkGetPhysicalDeviceExternalBufferProperties).  That means we need to
    * know the handle type that the renderer will use to make those queries.
    *
    * XXX We also assume that a vn_renderer_bo can be created as long as the
    * renderer VkDeviceMemory has a mappable memory type.  That is plain
    * wrong.  It is impossible to fix though until some new extension is
    * created and supported by the driver, and that the renderer switches to
    * the extension.
    */

   if (!physical_dev->instance->renderer->info.has_dma_buf_import)
      return;

   /* TODO We assume the renderer uses dma-bufs here.  This should be
    * negotiated by adding a new function to VK_MESA_venus_protocol.
    */
   if (physical_dev->renderer_extensions.EXT_external_memory_dma_buf) {
      physical_dev->external_memory.renderer_handle_type =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

#ifdef ANDROID
      physical_dev->external_memory.supported_handle_types =
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
    * External fence is not possible currently.  At best, we could cheat by
    * translating vkGetFenceFdKHR to vkWaitForFences and returning -1, when
    * the handle type is sync file.
    *
    * We would like to create a vn_renderer_sync from a host-side VkFence,
    * similar to how a vn_renderer_bo is created from a host-side
    * VkDeviceMemory.  That would require kernel support and tons of works on
    * the host side.  If we had that, and we kept both the vn_renderer_sync
    * and the host-side VkFence in sync, we would have the freedom to use
    * either of them depending on the occasions, and support external fences
    * and idle waiting.
    */
   physical_dev->external_fence_handles = 0;

#ifdef ANDROID
   if (physical_dev->instance->experimental.globalFencing) {
      physical_dev->external_fence_handles =
         VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   }
#endif
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
    * External semaphore is not possible currently.  We could cheat when the
    * semaphore is binary and the handle type is sync file, but that would
    * require associating a fence with the semaphore and doing vkWaitForFences
    * in vkGetSemaphoreFdKHR.
    *
    * We would like to create a vn_renderer_sync from a host-side VkSemaphore,
    * similar to how a vn_renderer_bo is created from a host-side
    * VkDeviceMemory.  The reasoning is the same as that for fences.
    * Additionally, we would like the sync file exported from the
    * vn_renderer_sync to carry the necessary information to identify the
    * host-side VkSemaphore.  That would allow the consumers to wait on the
    * host side rather than the guest side.
    */
   physical_dev->external_binary_semaphore_handles = 0;
   physical_dev->external_timeline_semaphore_handles = 0;

#ifdef ANDROID
   if (physical_dev->instance->experimental.globalFencing) {
      physical_dev->external_binary_semaphore_handles =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
   }
#endif
}

static void
vn_physical_device_get_native_extensions(
   const struct vn_physical_device *physical_dev,
   struct vk_device_extension_table *exts)
{
   const struct vn_instance *instance = physical_dev->instance;
   const struct vk_device_extension_table *renderer_exts =
      &physical_dev->renderer_extensions;

   memset(exts, 0, sizeof(*exts));

   /* see vn_physical_device_init_external_memory */
   const bool can_external_mem = renderer_exts->EXT_external_memory_dma_buf &&
                                 instance->renderer->info.has_dma_buf_import;

#ifdef ANDROID
   if (can_external_mem && renderer_exts->EXT_image_drm_format_modifier &&
       renderer_exts->EXT_queue_family_foreign &&
       instance->experimental.memoryResourceAllocationSize == VK_TRUE) {
      exts->ANDROID_external_memory_android_hardware_buffer = true;
      exts->ANDROID_native_buffer = true;
   }

   /* we have a very poor implementation */
   if (instance->experimental.globalFencing) {
      exts->KHR_external_fence_fd = true;
      exts->KHR_external_semaphore_fd = true;
   }
#else /* ANDROID */
   if (can_external_mem) {
      exts->KHR_external_memory_fd = true;
      exts->EXT_external_memory_dma_buf = true;
   }

#ifdef VN_USE_WSI_PLATFORM
   /* XXX we should check for EXT_queue_family_foreign */
   exts->KHR_incremental_present = true;
   exts->KHR_swapchain = true;
   exts->KHR_swapchain_mutable_format = true;
#endif
#endif /* ANDROID */

   exts->EXT_physical_device_drm = true;
}

static void
vn_physical_device_get_passthrough_extensions(
   const struct vn_physical_device *physical_dev,
   struct vk_device_extension_table *exts)
{
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
#ifndef ANDROID
      /* xxx remove the #ifndef after venus has a driver id */
      .KHR_driver_properties = true,
#endif
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
      .EXT_4444_formats = true,
      .EXT_extended_dynamic_state = true,
      .EXT_extended_dynamic_state2 = true,
      .EXT_image_robustness = true,
      .EXT_inline_uniform_block = true,
      .EXT_shader_demote_to_helper_invocation = true,
      .KHR_copy_commands2 = true,
      .KHR_dynamic_rendering = true,
      .KHR_maintenance4 = true,

      /* EXT */
      .EXT_calibrated_timestamps = true,
      .EXT_conditional_rendering = true,
      .EXT_conservative_rasterization = true,
      .EXT_custom_border_color = true,
      .EXT_depth_clip_enable = true,
#ifndef ANDROID
      .EXT_image_drm_format_modifier = true,
#endif
      .EXT_image_view_min_lod = true,
      .EXT_index_type_uint8 = true,
      .EXT_line_rasterization = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_robustness2 = true,
      .EXT_shader_stencil_export = true,
      .EXT_transform_feedback = true,
      .EXT_vertex_attribute_divisor = true,
   };
}

static void
vn_physical_device_init_supported_extensions(
   struct vn_physical_device *physical_dev)
{
   struct vk_device_extension_table native;
   struct vk_device_extension_table passthrough;
   vn_physical_device_get_native_extensions(physical_dev, &native);
   vn_physical_device_get_passthrough_extensions(physical_dev, &passthrough);

   for (uint32_t i = 0; i < VK_DEVICE_EXTENSION_COUNT; i++) {
      const VkExtensionProperties *props = &vk_device_extensions[i];

#ifdef ANDROID
      if (!vk_android_allowed_device_extensions.extensions[i])
         continue;
#endif

      if (native.extensions[i]) {
         physical_dev->base.base.supported_extensions.extensions[i] = true;
         physical_dev->extension_spec_versions[i] = props->specVersion;
      } else if (passthrough.extensions[i] &&
                 physical_dev->renderer_extensions.extensions[i]) {
         physical_dev->base.base.supported_extensions.extensions[i] = true;
         physical_dev->extension_spec_versions[i] = MIN2(
            physical_dev->extension_spec_versions[i], props->specVersion);
      }
   }

   /* override VK_ANDROID_native_buffer spec version */
   if (native.ANDROID_native_buffer) {
      const uint32_t index =
         VN_EXTENSION_TABLE_INDEX(native, ANDROID_native_buffer);
      physical_dev->extension_spec_versions[index] =
         VN_ANDROID_NATIVE_BUFFER_SPEC_VERSION;
   }
}

static VkResult
vn_physical_device_init_renderer_extensions(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;

   /* get renderer extensions */
   uint32_t count;
   VkResult result = vn_call_vkEnumerateDeviceExtensionProperties(
      instance, vn_physical_device_to_handle(physical_dev), NULL, &count,
      NULL);
   if (result != VK_SUCCESS)
      return result;

   VkExtensionProperties *exts = NULL;
   if (count) {
      exts = vk_alloc(alloc, sizeof(*exts) * count, VN_DEFAULT_ALIGN,
                      VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!exts)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      result = vn_call_vkEnumerateDeviceExtensionProperties(
         instance, vn_physical_device_to_handle(physical_dev), NULL, &count,
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

   return VK_SUCCESS;
}

static VkResult
vn_physical_device_init_renderer_version(
   struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;

   /*
    * We either check and enable VK_KHR_get_physical_device_properties2, or we
    * must use vkGetPhysicalDeviceProperties to get the device-level version.
    */
   VkPhysicalDeviceProperties props;
   vn_call_vkGetPhysicalDeviceProperties(
      instance, vn_physical_device_to_handle(physical_dev), &props);
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

static VkResult
vn_physical_device_init(struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;
   VkResult result;

   result = vn_physical_device_init_renderer_extensions(physical_dev);
   if (result != VK_SUCCESS)
      return result;

   vn_physical_device_init_supported_extensions(physical_dev);

   /* TODO query all caps with minimal round trips */
   vn_physical_device_init_features(physical_dev);
   vn_physical_device_init_properties(physical_dev);

   result = vn_physical_device_init_queue_family_properties(physical_dev);
   if (result != VK_SUCCESS)
      goto fail;

   vn_physical_device_init_memory_properties(physical_dev);

   vn_physical_device_init_external_memory(physical_dev);
   vn_physical_device_init_external_fence_handles(physical_dev);
   vn_physical_device_init_external_semaphore_handles(physical_dev);

   result = vn_wsi_init(physical_dev);
   if (result != VK_SUCCESS)
      goto fail;

   simple_mtx_init(&physical_dev->format_update_mutex, mtx_plain);
   util_sparse_array_init(&physical_dev->format_properties,
                          sizeof(struct vn_format_properties_entry), 64);

   return VK_SUCCESS;

fail:
   vk_free(alloc, physical_dev->extension_spec_versions);
   vk_free(alloc, physical_dev->queue_family_properties);
   return result;
}

void
vn_physical_device_fini(struct vn_physical_device *physical_dev)
{
   struct vn_instance *instance = physical_dev->instance;
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;

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
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;
   VkResult result;

   uint32_t count;
   result = vn_call_vkEnumeratePhysicalDeviceGroups(instance, instance_handle,
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
         temp_obj->base.base.type = VK_OBJECT_TYPE_PHYSICAL_DEVICE;
         group->physicalDevices[j] = (VkPhysicalDevice)temp_obj;
      }
   }

   result = vn_call_vkEnumeratePhysicalDeviceGroups(instance, instance_handle,
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
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;
   struct vn_physical_device *physical_devs = NULL;
   VkPhysicalDevice *handles = NULL;
   VkResult result;

   uint32_t count;
   result = vn_call_vkEnumeratePhysicalDevices(
      instance, vn_instance_to_handle(instance), &count, NULL);
   if (result != VK_SUCCESS || !count)
      return result;

   physical_devs =
      vk_zalloc(alloc, sizeof(*physical_devs) * count, VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!physical_devs)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   handles = vk_alloc(alloc, sizeof(*handles) * count, VN_DEFAULT_ALIGN,
                      VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!handles) {
      vk_free(alloc, physical_devs);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

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
      instance, vn_instance_to_handle(instance), &count, handles);
   if (result != VK_SUCCESS)
      goto fail;

   vk_free(alloc, handles);
   *out_physical_devs = physical_devs;
   *out_count = count;

   return VK_SUCCESS;

fail:
   for (uint32_t i = 0; i < count; i++)
      vn_physical_device_base_fini(&physical_devs[i].base);
   vk_free(alloc, physical_devs);
   vk_free(alloc, handles);
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

      if (supported_count < i)
         physical_devs[supported_count] = *physical_dev;
      supported_count++;
   }

   return supported_count;
}

static VkResult
vn_instance_enumerate_physical_devices_and_groups(struct vn_instance *instance)
{
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;
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

   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDevice, out, pPhysicalDevices, pPhysicalDeviceCount);
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

   VK_OUTARRAY_MAKE_TYPED(VkExtensionProperties, out, pProperties, pPropertyCount);
   for (uint32_t i = 0; i < VK_DEVICE_EXTENSION_COUNT; i++) {
      if (physical_dev->base.base.supported_extensions.extensions[i]) {
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

static struct vn_format_properties_entry *
vn_physical_device_get_format_properties(
   struct vn_physical_device *physical_dev, VkFormat format)
{
   return util_sparse_array_get(&physical_dev->format_properties, format);
}

static void
vn_physical_device_add_format_properties(
   struct vn_physical_device *physical_dev,
   struct vn_format_properties_entry *entry,
   const VkFormatProperties *props)
{
   simple_mtx_lock(&physical_dev->format_update_mutex);
   if (!entry->valid) {
      entry->properties = *props;
      entry->valid = true;
   }
   simple_mtx_unlock(&physical_dev->format_update_mutex);
}

void
vn_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                              VkPhysicalDeviceFeatures2 *pFeatures)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   const struct vn_physical_device_features *feats = &physical_dev->features;
   const struct VkPhysicalDeviceVulkan11Features *vk11_feats =
      &feats->vulkan_1_1;
   const struct VkPhysicalDeviceVulkan12Features *vk12_feats =
      &feats->vulkan_1_2;
   union {
      VkBaseOutStructure *pnext;

      VkPhysicalDeviceFeatures2 *features2;
      VkPhysicalDeviceVulkan11Features *vulkan_1_1;
      VkPhysicalDeviceVulkan12Features *vulkan_1_2;

      /* Vulkan 1.1 */
      VkPhysicalDevice16BitStorageFeatures *sixteen_bit_storage;
      VkPhysicalDeviceMultiviewFeatures *multiview;
      VkPhysicalDeviceVariablePointersFeatures *variable_pointers;
      VkPhysicalDeviceProtectedMemoryFeatures *protected_memory;
      VkPhysicalDeviceSamplerYcbcrConversionFeatures *sampler_ycbcr_conversion;
      VkPhysicalDeviceShaderDrawParametersFeatures *shader_draw_parameters;

      /* Vulkan 1.2 */
      VkPhysicalDevice8BitStorageFeatures *eight_bit_storage;
      VkPhysicalDeviceShaderAtomicInt64Features *shader_atomic_int64;
      VkPhysicalDeviceShaderFloat16Int8Features *shader_float16_int8;
      VkPhysicalDeviceDescriptorIndexingFeatures *descriptor_indexing;
      VkPhysicalDeviceScalarBlockLayoutFeatures *scalar_block_layout;
      VkPhysicalDeviceImagelessFramebufferFeatures *imageless_framebuffer;
      VkPhysicalDeviceUniformBufferStandardLayoutFeatures
         *uniform_buffer_standard_layout;
      VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures
         *shader_subgroup_extended_types;
      VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures
         *separate_depth_stencil_layouts;
      VkPhysicalDeviceHostQueryResetFeatures *host_query_reset;
      VkPhysicalDeviceTimelineSemaphoreFeatures *timeline_semaphore;
      VkPhysicalDeviceBufferDeviceAddressFeatures *buffer_device_address;
      VkPhysicalDeviceVulkanMemoryModelFeatures *vulkan_memory_model;

      /* Vulkan 1.3 */
      VkPhysicalDevice4444FormatsFeaturesEXT *argb_4444_formats;
      VkPhysicalDeviceDynamicRenderingFeatures *dynamic_rendering;
      VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *extended_dynamic_state;
      VkPhysicalDeviceExtendedDynamicState2FeaturesEXT
         *extended_dynamic_state2;
      VkPhysicalDeviceImageRobustnessFeatures *image_robustness;
      VkPhysicalDeviceInlineUniformBlockFeatures *inline_uniform_block;
      VkPhysicalDeviceMaintenance4Features *maintenance4;
      VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures
         *shader_demote_to_helper_invocation;

      /* EXT */
      VkPhysicalDeviceConditionalRenderingFeaturesEXT *conditional_rendering;
      VkPhysicalDeviceCustomBorderColorFeaturesEXT *custom_border_color;
      VkPhysicalDeviceDepthClipEnableFeaturesEXT *depth_clip_enable;
      VkPhysicalDeviceIndexTypeUint8FeaturesEXT *index_type_uint8;
      VkPhysicalDeviceLineRasterizationFeaturesEXT *line_rasterization;
      VkPhysicalDeviceProvokingVertexFeaturesEXT *provoking_vertex;
      VkPhysicalDeviceRobustness2FeaturesEXT *robustness_2;
      VkPhysicalDeviceTransformFeedbackFeaturesEXT *transform_feedback;
      VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT
         *vertex_attribute_divisor;
   } u;

   u.pnext = (VkBaseOutStructure *)pFeatures;
   while (u.pnext) {
      void *saved = u.pnext->pNext;
      switch (u.pnext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
         u.features2->features = feats->vulkan_1_0;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
         *u.vulkan_1_1 = *vk11_feats;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
         *u.vulkan_1_2 = *vk12_feats;
         break;

      /* Vulkan 1.1 */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES:
         u.sixteen_bit_storage->storageBuffer16BitAccess =
            vk11_feats->storageBuffer16BitAccess;
         u.sixteen_bit_storage->uniformAndStorageBuffer16BitAccess =
            vk11_feats->uniformAndStorageBuffer16BitAccess;
         u.sixteen_bit_storage->storagePushConstant16 =
            vk11_feats->storagePushConstant16;
         u.sixteen_bit_storage->storageInputOutput16 =
            vk11_feats->storageInputOutput16;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES:
         u.multiview->multiview = vk11_feats->multiview;
         u.multiview->multiviewGeometryShader =
            vk11_feats->multiviewGeometryShader;
         u.multiview->multiviewTessellationShader =
            vk11_feats->multiviewTessellationShader;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES:
         u.variable_pointers->variablePointersStorageBuffer =
            vk11_feats->variablePointersStorageBuffer;
         u.variable_pointers->variablePointers = vk11_feats->variablePointers;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES:
         u.protected_memory->protectedMemory = vk11_feats->protectedMemory;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES:
         u.sampler_ycbcr_conversion->samplerYcbcrConversion =
            vk11_feats->samplerYcbcrConversion;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES:
         u.shader_draw_parameters->shaderDrawParameters =
            vk11_feats->shaderDrawParameters;
         break;

      /* Vulkan 1.2 */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES:
         u.eight_bit_storage->storageBuffer8BitAccess =
            vk12_feats->storageBuffer8BitAccess;
         u.eight_bit_storage->uniformAndStorageBuffer8BitAccess =
            vk12_feats->uniformAndStorageBuffer8BitAccess;
         u.eight_bit_storage->storagePushConstant8 =
            vk12_feats->storagePushConstant8;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES:
         u.shader_atomic_int64->shaderBufferInt64Atomics =
            vk12_feats->shaderBufferInt64Atomics;
         u.shader_atomic_int64->shaderSharedInt64Atomics =
            vk12_feats->shaderSharedInt64Atomics;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
         u.shader_float16_int8->shaderFloat16 = vk12_feats->shaderFloat16;
         u.shader_float16_int8->shaderInt8 = vk12_feats->shaderInt8;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
         u.descriptor_indexing->shaderInputAttachmentArrayDynamicIndexing =
            vk12_feats->shaderInputAttachmentArrayDynamicIndexing;
         u.descriptor_indexing->shaderUniformTexelBufferArrayDynamicIndexing =
            vk12_feats->shaderUniformTexelBufferArrayDynamicIndexing;
         u.descriptor_indexing->shaderStorageTexelBufferArrayDynamicIndexing =
            vk12_feats->shaderStorageTexelBufferArrayDynamicIndexing;
         u.descriptor_indexing->shaderUniformBufferArrayNonUniformIndexing =
            vk12_feats->shaderUniformBufferArrayNonUniformIndexing;
         u.descriptor_indexing->shaderSampledImageArrayNonUniformIndexing =
            vk12_feats->shaderSampledImageArrayNonUniformIndexing;
         u.descriptor_indexing->shaderStorageBufferArrayNonUniformIndexing =
            vk12_feats->shaderStorageBufferArrayNonUniformIndexing;
         u.descriptor_indexing->shaderStorageImageArrayNonUniformIndexing =
            vk12_feats->shaderStorageImageArrayNonUniformIndexing;
         u.descriptor_indexing->shaderInputAttachmentArrayNonUniformIndexing =
            vk12_feats->shaderInputAttachmentArrayNonUniformIndexing;
         u.descriptor_indexing
            ->shaderUniformTexelBufferArrayNonUniformIndexing =
            vk12_feats->shaderUniformTexelBufferArrayNonUniformIndexing;
         u.descriptor_indexing
            ->shaderStorageTexelBufferArrayNonUniformIndexing =
            vk12_feats->shaderStorageTexelBufferArrayNonUniformIndexing;
         u.descriptor_indexing->descriptorBindingUniformBufferUpdateAfterBind =
            vk12_feats->descriptorBindingUniformBufferUpdateAfterBind;
         u.descriptor_indexing->descriptorBindingSampledImageUpdateAfterBind =
            vk12_feats->descriptorBindingSampledImageUpdateAfterBind;
         u.descriptor_indexing->descriptorBindingStorageImageUpdateAfterBind =
            vk12_feats->descriptorBindingStorageImageUpdateAfterBind;
         u.descriptor_indexing->descriptorBindingStorageBufferUpdateAfterBind =
            vk12_feats->descriptorBindingStorageBufferUpdateAfterBind;
         u.descriptor_indexing
            ->descriptorBindingUniformTexelBufferUpdateAfterBind =
            vk12_feats->descriptorBindingUniformTexelBufferUpdateAfterBind;
         u.descriptor_indexing
            ->descriptorBindingStorageTexelBufferUpdateAfterBind =
            vk12_feats->descriptorBindingStorageTexelBufferUpdateAfterBind;
         u.descriptor_indexing->descriptorBindingUpdateUnusedWhilePending =
            vk12_feats->descriptorBindingUpdateUnusedWhilePending;
         u.descriptor_indexing->descriptorBindingPartiallyBound =
            vk12_feats->descriptorBindingPartiallyBound;
         u.descriptor_indexing->descriptorBindingVariableDescriptorCount =
            vk12_feats->descriptorBindingVariableDescriptorCount;
         u.descriptor_indexing->runtimeDescriptorArray =
            vk12_feats->runtimeDescriptorArray;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
         u.scalar_block_layout->scalarBlockLayout =
            vk12_feats->scalarBlockLayout;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES:
         u.imageless_framebuffer->imagelessFramebuffer =
            vk12_feats->imagelessFramebuffer;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES:
         u.uniform_buffer_standard_layout->uniformBufferStandardLayout =
            vk12_feats->uniformBufferStandardLayout;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES:
         u.shader_subgroup_extended_types->shaderSubgroupExtendedTypes =
            vk12_feats->shaderSubgroupExtendedTypes;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES:
         u.separate_depth_stencil_layouts->separateDepthStencilLayouts =
            vk12_feats->separateDepthStencilLayouts;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES:
         u.host_query_reset->hostQueryReset = vk12_feats->hostQueryReset;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
         u.timeline_semaphore->timelineSemaphore =
            vk12_feats->timelineSemaphore;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
         u.buffer_device_address->bufferDeviceAddress =
            vk12_feats->bufferDeviceAddress;
         u.buffer_device_address->bufferDeviceAddressCaptureReplay =
            vk12_feats->bufferDeviceAddressCaptureReplay;
         u.buffer_device_address->bufferDeviceAddressMultiDevice =
            vk12_feats->bufferDeviceAddressMultiDevice;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES:
         u.vulkan_memory_model->vulkanMemoryModel =
            vk12_feats->vulkanMemoryModel;
         u.vulkan_memory_model->vulkanMemoryModelDeviceScope =
            vk12_feats->vulkanMemoryModelDeviceScope;
         u.vulkan_memory_model->vulkanMemoryModelAvailabilityVisibilityChains =
            vk12_feats->vulkanMemoryModelAvailabilityVisibilityChains;
         break;

      /* Vulkan 1.3 */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT:
         *u.argb_4444_formats = feats->argb_4444_formats;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
         *u.dynamic_rendering = feats->dynamic_rendering;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT:
         *u.extended_dynamic_state = feats->extended_dynamic_state;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT:
         *u.extended_dynamic_state2 = feats->extended_dynamic_state_2;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES:
         *u.image_robustness = feats->image_robustness;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES:
         *u.inline_uniform_block = feats->inline_uniform_block;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES:
         *u.shader_demote_to_helper_invocation =
            feats->shader_demote_to_helper_invocation;
         break;

      /* EXT */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT:
         *u.conditional_rendering = feats->conditional_rendering;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT:
         *u.custom_border_color = feats->custom_border_color;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT:
         *u.depth_clip_enable = feats->depth_clip_enable;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT:
         *u.index_type_uint8 = feats->index_type_uint8;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT:
         *u.line_rasterization = feats->line_rasterization;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT:
         *u.provoking_vertex = feats->provoking_vertex;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT:
         *u.robustness_2 = feats->robustness_2;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT:
         *u.transform_feedback = feats->transform_feedback;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT:
         *u.vertex_attribute_divisor = feats->vertex_attribute_divisor;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES:
         *u.maintenance4 = feats->maintenance4;
         break;
      default:
         break;
      }

      u.pnext->pNext = saved;
      u.pnext = u.pnext->pNext;
   }
}

void
vn_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                VkPhysicalDeviceProperties2 *pProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   const struct vn_physical_device_properties *props =
      &physical_dev->properties;
   const struct VkPhysicalDeviceVulkan11Properties *vk11_props =
      &props->vulkan_1_1;
   const struct VkPhysicalDeviceVulkan12Properties *vk12_props =
      &props->vulkan_1_2;
   union {
      VkBaseOutStructure *pnext;

      VkPhysicalDeviceProperties2 *properties2;
      VkPhysicalDeviceVulkan11Properties *vulkan_1_1;
      VkPhysicalDeviceVulkan12Properties *vulkan_1_2;

      /* Vulkan 1.1 */
      VkPhysicalDeviceIDProperties *id;
      VkPhysicalDeviceSubgroupProperties *subgroup;
      VkPhysicalDevicePointClippingProperties *point_clipping;
      VkPhysicalDeviceMultiviewProperties *multiview;
      VkPhysicalDeviceProtectedMemoryProperties *protected_memory;
      VkPhysicalDeviceMaintenance3Properties *maintenance_3;

      /* Vulkan 1.2 */
      VkPhysicalDeviceDriverProperties *driver;
      VkPhysicalDeviceFloatControlsProperties *float_controls;
      VkPhysicalDeviceDescriptorIndexingProperties *descriptor_indexing;
      VkPhysicalDeviceDepthStencilResolveProperties *depth_stencil_resolve;
      VkPhysicalDeviceSamplerFilterMinmaxProperties *sampler_filter_minmax;
      VkPhysicalDeviceTimelineSemaphoreProperties *timeline_semaphore;

      /* Vulkan 1.3 */
      VkPhysicalDeviceInlineUniformBlockProperties *inline_uniform_block;

      /* EXT */
      VkPhysicalDeviceConservativeRasterizationPropertiesEXT
         *conservative_rasterization;
      VkPhysicalDeviceCustomBorderColorPropertiesEXT *custom_border_color;
      VkPhysicalDeviceDrmPropertiesEXT *drm;
      VkPhysicalDeviceLineRasterizationPropertiesEXT *line_rasterization;
      VkPhysicalDevicePCIBusInfoPropertiesEXT *pci_bus_info;
      VkPhysicalDevicePresentationPropertiesANDROID *presentation_properties;
      VkPhysicalDeviceProvokingVertexPropertiesEXT *provoking_vertex;
      VkPhysicalDeviceRobustness2PropertiesEXT *robustness_2;
      VkPhysicalDeviceMaintenance4Properties *maintenance4;
      VkPhysicalDeviceTransformFeedbackPropertiesEXT *transform_feedback;
      VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT
         *vertex_attribute_divisor;
   } u;

   u.pnext = (VkBaseOutStructure *)pProperties;
   while (u.pnext) {
      void *saved = u.pnext->pNext;
      switch ((int32_t)u.pnext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2:
         u.properties2->properties = props->vulkan_1_0;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES:
         *u.vulkan_1_1 = *vk11_props;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
         *u.vulkan_1_2 = *vk12_props;
         break;

      /* Vulkan 1.1 */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES:
         memcpy(u.id->deviceUUID, vk11_props->deviceUUID,
                sizeof(vk11_props->deviceUUID));
         memcpy(u.id->driverUUID, vk11_props->driverUUID,
                sizeof(vk11_props->driverUUID));
         memcpy(u.id->deviceLUID, vk11_props->deviceLUID,
                sizeof(vk11_props->deviceLUID));
         u.id->deviceNodeMask = vk11_props->deviceNodeMask;
         u.id->deviceLUIDValid = vk11_props->deviceLUIDValid;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES:
         u.subgroup->subgroupSize = vk11_props->subgroupSize;
         u.subgroup->supportedStages = vk11_props->subgroupSupportedStages;
         u.subgroup->supportedOperations =
            vk11_props->subgroupSupportedOperations;
         u.subgroup->quadOperationsInAllStages =
            vk11_props->subgroupQuadOperationsInAllStages;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES:
         u.point_clipping->pointClippingBehavior =
            vk11_props->pointClippingBehavior;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES:
         u.multiview->maxMultiviewViewCount =
            vk11_props->maxMultiviewViewCount;
         u.multiview->maxMultiviewInstanceIndex =
            vk11_props->maxMultiviewInstanceIndex;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES:
         u.protected_memory->protectedNoFault = vk11_props->protectedNoFault;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES:
         u.maintenance_3->maxPerSetDescriptors =
            vk11_props->maxPerSetDescriptors;
         u.maintenance_3->maxMemoryAllocationSize =
            vk11_props->maxMemoryAllocationSize;
         break;

      /* Vulkan 1.2 */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES:
         u.driver->driverID = vk12_props->driverID;
         memcpy(u.driver->driverName, vk12_props->driverName,
                sizeof(vk12_props->driverName));
         memcpy(u.driver->driverInfo, vk12_props->driverInfo,
                sizeof(vk12_props->driverInfo));
         u.driver->conformanceVersion = vk12_props->conformanceVersion;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES:
         u.float_controls->denormBehaviorIndependence =
            vk12_props->denormBehaviorIndependence;
         u.float_controls->roundingModeIndependence =
            vk12_props->roundingModeIndependence;
         u.float_controls->shaderSignedZeroInfNanPreserveFloat16 =
            vk12_props->shaderSignedZeroInfNanPreserveFloat16;
         u.float_controls->shaderSignedZeroInfNanPreserveFloat32 =
            vk12_props->shaderSignedZeroInfNanPreserveFloat32;
         u.float_controls->shaderSignedZeroInfNanPreserveFloat64 =
            vk12_props->shaderSignedZeroInfNanPreserveFloat64;
         u.float_controls->shaderDenormPreserveFloat16 =
            vk12_props->shaderDenormPreserveFloat16;
         u.float_controls->shaderDenormPreserveFloat32 =
            vk12_props->shaderDenormPreserveFloat32;
         u.float_controls->shaderDenormPreserveFloat64 =
            vk12_props->shaderDenormPreserveFloat64;
         u.float_controls->shaderDenormFlushToZeroFloat16 =
            vk12_props->shaderDenormFlushToZeroFloat16;
         u.float_controls->shaderDenormFlushToZeroFloat32 =
            vk12_props->shaderDenormFlushToZeroFloat32;
         u.float_controls->shaderDenormFlushToZeroFloat64 =
            vk12_props->shaderDenormFlushToZeroFloat64;
         u.float_controls->shaderRoundingModeRTEFloat16 =
            vk12_props->shaderRoundingModeRTEFloat16;
         u.float_controls->shaderRoundingModeRTEFloat32 =
            vk12_props->shaderRoundingModeRTEFloat32;
         u.float_controls->shaderRoundingModeRTEFloat64 =
            vk12_props->shaderRoundingModeRTEFloat64;
         u.float_controls->shaderRoundingModeRTZFloat16 =
            vk12_props->shaderRoundingModeRTZFloat16;
         u.float_controls->shaderRoundingModeRTZFloat32 =
            vk12_props->shaderRoundingModeRTZFloat32;
         u.float_controls->shaderRoundingModeRTZFloat64 =
            vk12_props->shaderRoundingModeRTZFloat64;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES:
         u.descriptor_indexing->maxUpdateAfterBindDescriptorsInAllPools =
            vk12_props->maxUpdateAfterBindDescriptorsInAllPools;
         u.descriptor_indexing
            ->shaderUniformBufferArrayNonUniformIndexingNative =
            vk12_props->shaderUniformBufferArrayNonUniformIndexingNative;
         u.descriptor_indexing
            ->shaderSampledImageArrayNonUniformIndexingNative =
            vk12_props->shaderSampledImageArrayNonUniformIndexingNative;
         u.descriptor_indexing
            ->shaderStorageBufferArrayNonUniformIndexingNative =
            vk12_props->shaderStorageBufferArrayNonUniformIndexingNative;
         u.descriptor_indexing
            ->shaderStorageImageArrayNonUniformIndexingNative =
            vk12_props->shaderStorageImageArrayNonUniformIndexingNative;
         u.descriptor_indexing
            ->shaderInputAttachmentArrayNonUniformIndexingNative =
            vk12_props->shaderInputAttachmentArrayNonUniformIndexingNative;
         u.descriptor_indexing->robustBufferAccessUpdateAfterBind =
            vk12_props->robustBufferAccessUpdateAfterBind;
         u.descriptor_indexing->quadDivergentImplicitLod =
            vk12_props->quadDivergentImplicitLod;
         u.descriptor_indexing->maxPerStageDescriptorUpdateAfterBindSamplers =
            vk12_props->maxPerStageDescriptorUpdateAfterBindSamplers;
         u.descriptor_indexing
            ->maxPerStageDescriptorUpdateAfterBindUniformBuffers =
            vk12_props->maxPerStageDescriptorUpdateAfterBindUniformBuffers;
         u.descriptor_indexing
            ->maxPerStageDescriptorUpdateAfterBindStorageBuffers =
            vk12_props->maxPerStageDescriptorUpdateAfterBindStorageBuffers;
         u.descriptor_indexing
            ->maxPerStageDescriptorUpdateAfterBindSampledImages =
            vk12_props->maxPerStageDescriptorUpdateAfterBindSampledImages;
         u.descriptor_indexing
            ->maxPerStageDescriptorUpdateAfterBindStorageImages =
            vk12_props->maxPerStageDescriptorUpdateAfterBindStorageImages;
         u.descriptor_indexing
            ->maxPerStageDescriptorUpdateAfterBindInputAttachments =
            vk12_props->maxPerStageDescriptorUpdateAfterBindInputAttachments;
         u.descriptor_indexing->maxPerStageUpdateAfterBindResources =
            vk12_props->maxPerStageUpdateAfterBindResources;
         u.descriptor_indexing->maxDescriptorSetUpdateAfterBindSamplers =
            vk12_props->maxDescriptorSetUpdateAfterBindSamplers;
         u.descriptor_indexing->maxDescriptorSetUpdateAfterBindUniformBuffers =
            vk12_props->maxDescriptorSetUpdateAfterBindUniformBuffers;
         u.descriptor_indexing
            ->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic =
            vk12_props->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic;
         u.descriptor_indexing->maxDescriptorSetUpdateAfterBindStorageBuffers =
            vk12_props->maxDescriptorSetUpdateAfterBindStorageBuffers;
         u.descriptor_indexing
            ->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic =
            vk12_props->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic;
         u.descriptor_indexing->maxDescriptorSetUpdateAfterBindSampledImages =
            vk12_props->maxDescriptorSetUpdateAfterBindSampledImages;
         u.descriptor_indexing->maxDescriptorSetUpdateAfterBindStorageImages =
            vk12_props->maxDescriptorSetUpdateAfterBindStorageImages;
         u.descriptor_indexing
            ->maxDescriptorSetUpdateAfterBindInputAttachments =
            vk12_props->maxDescriptorSetUpdateAfterBindInputAttachments;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES:
         u.depth_stencil_resolve->supportedDepthResolveModes =
            vk12_props->supportedDepthResolveModes;
         u.depth_stencil_resolve->supportedStencilResolveModes =
            vk12_props->supportedStencilResolveModes;
         u.depth_stencil_resolve->independentResolveNone =
            vk12_props->independentResolveNone;
         u.depth_stencil_resolve->independentResolve =
            vk12_props->independentResolve;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES:
         u.sampler_filter_minmax->filterMinmaxSingleComponentFormats =
            vk12_props->filterMinmaxSingleComponentFormats;
         u.sampler_filter_minmax->filterMinmaxImageComponentMapping =
            vk12_props->filterMinmaxImageComponentMapping;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES:
         u.timeline_semaphore->maxTimelineSemaphoreValueDifference =
            vk12_props->maxTimelineSemaphoreValueDifference;
         break;

      /* Vulkan 1.3 */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES:
         *u.inline_uniform_block = props->inline_uniform_block;
         break;

      /* EXT */
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT:
         *u.conservative_rasterization = props->conservative_rasterization;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT:
         *u.custom_border_color = props->custom_border_color;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT:
         u.drm->hasPrimary =
            physical_dev->instance->renderer->info.drm.has_primary;
         u.drm->primaryMajor =
            physical_dev->instance->renderer->info.drm.primary_major;
         u.drm->primaryMinor =
            physical_dev->instance->renderer->info.drm.primary_minor;
         u.drm->hasRender =
            physical_dev->instance->renderer->info.drm.has_render;
         u.drm->renderMajor =
            physical_dev->instance->renderer->info.drm.render_major;
         u.drm->renderMinor =
            physical_dev->instance->renderer->info.drm.render_minor;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT:
         *u.line_rasterization = props->line_rasterization;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT:
         /* this is used by WSI */
         if (physical_dev->instance->renderer->info.pci.has_bus_info) {
            u.pci_bus_info->pciDomain =
               physical_dev->instance->renderer->info.pci.domain;
            u.pci_bus_info->pciBus =
               physical_dev->instance->renderer->info.pci.bus;
            u.pci_bus_info->pciDevice =
               physical_dev->instance->renderer->info.pci.device;
            u.pci_bus_info->pciFunction =
               physical_dev->instance->renderer->info.pci.function;
         }
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENTATION_PROPERTIES_ANDROID:
         u.presentation_properties->sharedImage =
            vn_android_gralloc_get_shared_present_usage() ? VK_TRUE
                                                          : VK_FALSE;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT:
         *u.provoking_vertex = props->provoking_vertex;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT:
         *u.robustness_2 = props->robustness_2;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT:
         *u.transform_feedback = props->transform_feedback;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT:
         *u.vertex_attribute_divisor = props->vertex_attribute_divisor;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES:
         *u.maintenance4 = props->maintenance4;
         break;
      default:
         break;
      }

      u.pnext->pNext = saved;
      u.pnext = u.pnext->pNext;
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
         *props = physical_dev->queue_family_properties[i];
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

   pMemoryProperties->memoryProperties =
      physical_dev->memory_properties.memoryProperties;
}

void
vn_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                      VkFormat format,
                                      VkFormatProperties2 *pFormatProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   struct vn_format_properties_entry *entry = NULL;
   if (!pFormatProperties->pNext) {
      entry = vn_physical_device_get_format_properties(physical_dev, format);
      if (entry->valid) {
         pFormatProperties->formatProperties = entry->properties;
         return;
      }
   }

   vn_call_vkGetPhysicalDeviceFormatProperties2(
      physical_dev->instance, physicalDevice, format, pFormatProperties);

   if (entry) {
      vn_physical_device_add_format_properties(
         physical_dev, entry, &pFormatProperties->formatProperties);
   }
}

struct vn_physical_device_image_format_info {
   VkPhysicalDeviceImageFormatInfo2 format;
   VkPhysicalDeviceExternalImageFormatInfo external;
   VkImageFormatListCreateInfo list;
   VkImageStencilUsageCreateInfo stencil_usage;
   VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier;
};

static const VkPhysicalDeviceImageFormatInfo2 *
vn_physical_device_fix_image_format_info(
   struct vn_physical_device *physical_dev,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   struct vn_physical_device_image_format_info *local_info)
{
   local_info->format = *info;
   VkBaseOutStructure *dst = (void *)&local_info->format;

   bool is_ahb = false;
   bool has_format_list = false;
   /* we should generate deep copy functions... */
   vk_foreach_struct_const(src, info->pNext) {
      void *pnext = NULL;
      switch (src->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         memcpy(&local_info->external, src, sizeof(local_info->external));
         is_ahb =
            local_info->external.handleType ==
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
         local_info->external.handleType =
            physical_dev->external_memory.renderer_handle_type;
         pnext = &local_info->external;
         break;
      case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO:
         has_format_list = true;
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
      default:
         break;
      }

      if (pnext) {
         dst->pNext = pnext;
         dst = pnext;
      }
   }

   if (is_ahb) {
      assert(local_info->format.tiling !=
             VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
      local_info->format.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      if (!vn_android_get_drm_format_modifier_info(&local_info->format,
                                                   &local_info->modifier))
         return NULL;

      dst->pNext = (void *)&local_info->modifier;
      dst = dst->pNext;

      if ((info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
          !local_info->list.viewFormatCount) {
         /* 12.3. Images
          *
          * If tiling is VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT and flags
          * contains VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, then the pNext chain
          * must include a VkImageFormatListCreateInfo structure with non-zero
          * viewFormatCount.
          */
         VkImageFormatListCreateInfo *list = &local_info->list;
         uint32_t vcount = 0;
         const VkFormat *vformats =
            vn_android_format_to_view_formats(info->format, &vcount);
         if (!vformats) {
            /* local_info persists through the image format query call */
            vformats = &local_info->format.format;
            vcount = 1;
         }

         list->sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
         list->viewFormatCount = vcount;
         list->pViewFormats = vformats;

         if (!has_format_list) {
            dst->pNext = (void *)list;
            dst = dst->pNext;
         }
      }
   }

   dst->pNext = NULL;

   return &local_info->format;
}

VkResult
vn_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   const VkExternalMemoryHandleTypeFlagBits renderer_handle_type =
      physical_dev->external_memory.renderer_handle_type;
   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      physical_dev->external_memory.supported_handle_types;

   const VkPhysicalDeviceExternalImageFormatInfo *external_info =
      vk_find_struct_const(pImageFormatInfo->pNext,
                           PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
   if (external_info && !external_info->handleType)
      external_info = NULL;

   struct vn_physical_device_image_format_info local_info;
   if (external_info) {
      if (!(external_info->handleType & supported_handle_types)) {
         return vn_error(physical_dev->instance,
                         VK_ERROR_FORMAT_NOT_SUPPORTED);
      }

      if (external_info->handleType != renderer_handle_type) {
         pImageFormatInfo = vn_physical_device_fix_image_format_info(
            physical_dev, pImageFormatInfo, &local_info);
         if (!pImageFormatInfo) {
            return vn_error(physical_dev->instance,
                            VK_ERROR_FORMAT_NOT_SUPPORTED);
         }
      }
   }

   VkResult result;
   /* TODO per-device cache */
   result = vn_call_vkGetPhysicalDeviceImageFormatProperties2(
      physical_dev->instance, physicalDevice, pImageFormatInfo,
      pImageFormatProperties);
   if (result != VK_SUCCESS || !external_info)
      return vn_result(physical_dev->instance, result);

   if (external_info->handleType ==
       VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
      VkAndroidHardwareBufferUsageANDROID *ahb_usage =
         vk_find_struct(pImageFormatProperties->pNext,
                        ANDROID_HARDWARE_BUFFER_USAGE_ANDROID);
      if (ahb_usage) {
         ahb_usage->androidHardwareBufferUsage = vn_android_get_ahb_usage(
            pImageFormatInfo->usage, pImageFormatInfo->flags);
      }

      /* AHBs with mipmap usage will ignore this property */
      pImageFormatProperties->imageFormatProperties.maxMipLevels = 1;
   }

   VkExternalImageFormatProperties *img_props = vk_find_struct(
      pImageFormatProperties->pNext, EXTERNAL_IMAGE_FORMAT_PROPERTIES);
   if (!img_props)
      return VK_SUCCESS;

   VkExternalMemoryProperties *mem_props =
      &img_props->externalMemoryProperties;

   if (external_info->handleType ==
       VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
      /* AHB backed image requires renderer to support import bit */
      if (!(mem_props->externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
         return vn_error(physical_dev->instance,
                         VK_ERROR_FORMAT_NOT_SUPPORTED);

      mem_props->externalMemoryFeatures =
         VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT |
         VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      mem_props->exportFromImportedHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
      mem_props->compatibleHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
   } else {
      mem_props->compatibleHandleTypes = supported_handle_types;
      mem_props->exportFromImportedHandleTypes =
         (mem_props->exportFromImportedHandleTypes & renderer_handle_type)
            ? supported_handle_types
            : 0;
   }

   return VK_SUCCESS;
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

   /* TODO allow sparse resource along with sync feedback
    *
    * If VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT is not supported for the given
    * arguments, pPropertyCount will be set to zero upon return, and no data
    * will be written to pProperties.
    */
   if (!VN_PERF(NO_FENCE_FEEDBACK)) {
      *pPropertyCount = 0;
      return;
   }

   /* TODO per-device cache */
   vn_call_vkGetPhysicalDeviceSparseImageFormatProperties2(
      physical_dev->instance, physicalDevice, pFormatInfo, pPropertyCount,
      pProperties);
}

void
vn_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
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

   VkPhysicalDeviceExternalBufferInfo local_info;
   if (pExternalBufferInfo->handleType != renderer_handle_type) {
      local_info = *pExternalBufferInfo;
      local_info.handleType = renderer_handle_type;
      pExternalBufferInfo = &local_info;
   }

   /* TODO per-device cache */
   vn_call_vkGetPhysicalDeviceExternalBufferProperties(
      physical_dev->instance, physicalDevice, pExternalBufferInfo,
      pExternalBufferProperties);

   if (is_ahb) {
      props->compatibleHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
      /* AHB backed buffer requires renderer to support import bit while it
       * also requires the renderer to must not advertise dedicated only bit
       */
      if (!(props->externalMemoryFeatures &
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ||
          (props->externalMemoryFeatures &
           VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT)) {
         props->externalMemoryFeatures = 0;
         props->exportFromImportedHandleTypes = 0;
         return;
      }
      props->externalMemoryFeatures =
         VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      props->exportFromImportedHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
   } else {
      props->compatibleHandleTypes = supported_handle_types;
      props->exportFromImportedHandleTypes =
         (props->exportFromImportedHandleTypes & renderer_handle_type)
            ? supported_handle_types
            : 0;
   }
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
vn_GetPhysicalDeviceCalibrateableTimeDomainsEXT(
   VkPhysicalDevice physicalDevice,
   uint32_t *pTimeDomainCount,
   VkTimeDomainEXT *pTimeDomains)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   return vn_call_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT(
      physical_dev->instance, physicalDevice, pTimeDomainCount, pTimeDomains);
}
