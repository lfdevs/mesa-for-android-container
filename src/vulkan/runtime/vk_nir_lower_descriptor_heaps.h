/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef VK_NIR_LOWER_DESCRIPTOR_HEAP_MAPPINGS
#define VK_NIR_LOWER_DESCRIPTOR_HEAP_MAPPINGS

#include "nir.h"
#include <vulkan/vulkan_core.h>
#include "util/mesa-blake3.h"

static inline const VkDescriptorSetAndBindingMappingEXT *
vk_descriptor_heap_mapping(const VkShaderDescriptorSetAndBindingMappingInfoEXT *info,
                           uint32_t set, uint32_t binding,
                           nir_resource_type resource_type)
{
   assert(util_is_power_of_two_nonzero(resource_type));

   for (uint32_t i = 0; i < info->mappingCount; i++) {
      const VkDescriptorSetAndBindingMappingEXT *mapping = &info->pMappings[i];
      const uint32_t begin_binding = mapping->firstBinding;
      const uint32_t end_binding =
         (mapping->firstBinding + mapping->bindingCount) < mapping->firstBinding ?
         UINT32_MAX : (mapping->firstBinding + mapping->bindingCount - 1) ;

      if (mapping->descriptorSet == set &&
          binding >= begin_binding && binding <= end_binding &&
          mapping->resourceMask & resource_type)
         return mapping;
   }

   return NULL;
}

static inline const VkSamplerCreateInfo *
vk_descriptor_heap_embedded_sampler(const VkDescriptorSetAndBindingMappingEXT *mapping)
{
   switch (mapping->source) {
   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT:
      return mapping->sourceData.constantOffset.pEmbeddedSampler;
   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT:
      return mapping->sourceData.pushIndex.pEmbeddedSampler;
   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_EXT:
      return mapping->sourceData.indirectIndex.pEmbeddedSampler;
   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_ARRAY_EXT:
      return mapping->sourceData.indirectIndexArray.pEmbeddedSampler;
   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_SHADER_RECORD_INDEX_EXT:
      return mapping->sourceData.shaderRecordIndex.pEmbeddedSampler;
   default:
      return NULL;
   }
}

void vk_hash_descriptor_heap_mappings(
   const VkShaderDescriptorSetAndBindingMappingInfoEXT *info,
   blake3_hash blake3_out);

struct vk_sampler_state_array {
   struct vk_sampler_state *samplers;
   uint32_t sampler_count;
};

static inline void
vk_sampler_state_array_finish(struct vk_sampler_state_array *arr)
{
   free(arr->samplers);
}

typedef struct vk_nir_lower_descriptor_heaps_options {
   /* Whether to lower non-arrayed resources backed by SRI to non-uniform. */
   bool lower_shader_record_index_to_non_uniform : 1;
} vk_nir_lower_descriptor_heaps_options;

bool vk_nir_lower_descriptor_heaps(
   nir_shader *nir,
   const VkShaderDescriptorSetAndBindingMappingInfoEXT *mapping,
   const vk_nir_lower_descriptor_heaps_options *options,
   struct vk_sampler_state_array *embedded_samplers_out);

#endif
