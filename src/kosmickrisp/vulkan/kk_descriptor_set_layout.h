/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_DESCRIPTOR_SET_LAYOUT
#define KK_DESCRIPTOR_SET_LAYOUT 1

#include "kk_private.h"

#include "vk_descriptor_set_layout.h"
#include "vk_object.h"

#include "util/bitset.h"

struct kk_device;
struct kk_physical_device;
struct kk_sampler;
struct vk_pipeline_layout;

struct kk_descriptor_set_binding_layout {
   /* The type of the descriptors in this binding */
   VkDescriptorType type;

   /* Flags provided when this binding was created */
   VkDescriptorBindingFlags flags;

   /* Number of array elements in this binding (or size in bytes for inline
    * uniform data)
    */
   uint32_t array_size;

   /* Number of actual descriptors per element */
   uint32_t count_per_element;

   /* Offset into the descriptor buffer where this descriptor lives */
   uint32_t offset;

   /* Stride between array elements in the descriptor buffer */
   uint8_t stride;

   /* Index into the dynamic buffer binding array */
   uint8_t dynamic_buffer_index;

   /* Immutable samplers (or NULL if no immutable samplers) */
   struct kk_sampler **immutable_samplers;
};

struct kk_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;

   VkDescriptorSetLayoutCreateFlagBits flags;

   /* Size of the descriptor buffer for this descriptor set */
   /* Does not contain the size needed for variable count descriptors */
   uint32_t non_variable_descriptor_buffer_size;

   /* Maximum possible buffer size for this descriptor set */
   uint32_t max_buffer_size;

   /* Number of dynamic UBO bindings in this set */
   uint8_t dynamic_buffer_count;

   /* Which dynamic buffers are UBOs */
   BITSET_DECLARE(dynamic_ubos, KK_MAX_DYNAMIC_BUFFERS);

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Number of descriptors in the layout */
   uint32_t descriptor_count;

   /* Address to the embedded sampler descriptor buffer.
    *
    * This is allocated from nvk_device::heap and has the size
    * non_variable_descriptor_buffer_size.
    */
   uint64_t embedded_samplers_addr;

   /* Bindings in this descriptor set */
   struct kk_descriptor_set_binding_layout binding[0];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_descriptor_set_layout, vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

void kk_descriptor_stride_align_for_type(
   VkDescriptorType type, const VkMutableDescriptorTypeListEXT *type_list,
   uint32_t *stride, uint32_t *alignment);

static inline struct kk_descriptor_set_layout *
vk_to_kk_descriptor_set_layout(struct vk_descriptor_set_layout *layout)
{
   return container_of(layout, struct kk_descriptor_set_layout, vk);
}

#endif /* KK_DESCRIPTOR_SET_LAYOUT */
