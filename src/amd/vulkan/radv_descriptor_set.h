/*
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DESCRIPTOR_SET_H
#define RADV_DESCRIPTOR_SET_H

#include "util/list.h"
#include "util/mesa-blake3.h"

#include "radv_constants.h"

#include "vk_descriptor_set_layout.h"
#include "vk_object.h"

#include <vulkan/vulkan.h>

struct radv_cmd_buffer;
struct radv_descriptor_pool;
struct radv_device;

struct radv_descriptor_set_binding_layout {
   VkDescriptorType type;

   /* Number of array elements in this binding */
   uint32_t array_size;

   uint32_t offset;
   uint32_t buffer_offset;
   uint16_t dynamic_offset_offset;

   uint16_t dynamic_offset_count;
   /* redundant with the type, each for a single array element */
   uint32_t size;

   /* Offset in the radv_descriptor_set_layout of the immutable samplers, or 0
    * if there are no immutable samplers. */
   uint32_t immutable_samplers_offset;

   bool has_ycbcr_sampler;
};

struct radv_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;

   /* Hash of all fields below */
   blake3_hash hash;

   /* Everything below is hashed and shouldn't contain any pointers. Be careful when modifying this
    * structure.
    */

   /* The create flags for this descriptor set layout */
   VkDescriptorSetLayoutCreateFlags flags;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Total size of the descriptor set with room for all array entries */
   uint32_t size;

   /* Shader stages affected by this descriptor set */
   uint16_t dynamic_shader_stages;

   /* Number of buffers in this descriptor set */
   uint32_t buffer_count;

   /* Number of dynamic offsets used by this descriptor set */
   uint16_t dynamic_offset_count;

   bool has_immutable_samplers;
   bool has_variable_descriptors;

   uint32_t ycbcr_sampler_offsets_offset;

   /* Bindings in this descriptor set */
   struct radv_descriptor_set_binding_layout binding[0];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_set_layout, vk.base, VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

struct radv_descriptor_range {
   uint64_t va;
   uint32_t size;
};

struct radv_descriptor_set_header {
   struct vk_object_base base;
   struct radv_descriptor_set_layout *layout;
   uint32_t offset;
   uint32_t size;
   uint32_t buffer_count;

   struct radeon_winsys_bo *bo;
   uint64_t va;
   uint32_t *mapped_ptr;
   struct radv_descriptor_range *dynamic_descriptors;
};

struct radv_descriptor_set {
   struct radv_descriptor_set_header header;

   /* Link in radv_descriptor_pool::sets */
   struct list_head link;

   struct radeon_winsys_bo *descriptors[];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_set, header.base, VkDescriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET)

static inline const uint32_t *
radv_immutable_samplers(const struct radv_descriptor_set_layout *set,
                        const struct radv_descriptor_set_binding_layout *binding)
{
   return (const uint32_t *)((const char *)set + binding->immutable_samplers_offset);
}

static inline const struct vk_ycbcr_conversion_state *
radv_immutable_ycbcr_samplers(const struct radv_descriptor_set_layout *set, unsigned binding_index)
{
   if (!set->ycbcr_sampler_offsets_offset)
      return NULL;

   const uint32_t *offsets = (const uint32_t *)((const char *)set + set->ycbcr_sampler_offsets_offset);

   if (offsets[binding_index] == 0)
      return NULL;
   return (const struct vk_ycbcr_conversion_state *)((const char *)set + offsets[binding_index]);
}

void radv_cmd_update_descriptor_sets(struct radv_device *device, struct radv_cmd_buffer *cmd_buffer,
                                     VkDescriptorSet overrideSet, uint32_t descriptorWriteCount,
                                     const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
                                     const VkCopyDescriptorSet *pDescriptorCopies);

void radv_descriptor_set_destroy(struct radv_device *device, struct radv_descriptor_pool *pool,
                                 struct radv_descriptor_set *set);

#endif /* RADV_DESCRIPTOR_SET_H */
