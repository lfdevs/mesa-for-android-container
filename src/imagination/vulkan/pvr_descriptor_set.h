/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_DESCRIPTOR_SET_H
#define PVR_DESCRIPTOR_SET_H

#include "util/list.h"
#include "util/vma.h"

#include "vk_descriptor_set_layout.h"

#include "pvr_common.h"
#include "pvr_types.h"

struct pvr_descriptor_set_layout_binding {
   VkDescriptorType type;
   VkDescriptorBindingFlags flags;

   uint32_t stage_flags; /** Which stages can use this binding. */

   uint32_t descriptor_count;
   uint32_t immutable_sampler_count;
   struct pvr_sampler **immutable_samplers;

   unsigned offset; /** Offset within the descriptor set. */
   unsigned dynamic_buffer_idx;
   unsigned stride; /** Stride of each descriptor in this binding. */
};

struct pvr_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;
   VkDescriptorSetLayoutCreateFlagBits flags;

   uint32_t descriptor_count;
   uint32_t dynamic_buffer_count;

   uint32_t binding_count;
   struct pvr_descriptor_set_layout_binding *bindings;

   uint32_t immutable_sampler_count;
   struct pvr_sampler **immutable_samplers;

   uint32_t stage_flags; /** Which stages can use this binding. */

   unsigned size; /** Size in bytes. */
};

struct pvr_descriptor_pool {
   struct vk_object_base base;

   VkDescriptorType type;
   VkAllocationCallbacks alloc;
   VkDescriptorPoolCreateFlags flags;

   /** List of the descriptor sets created using this pool. */
   struct list_head desc_sets;

   struct pvr_suballoc_bo *pvr_bo; /** Pool buffer object. */
   void *mapping; /** Pool buffer CPU mapping. */
   struct util_vma_heap heap; /** Pool (sub)allocation heap. */
};

struct pvr_descriptor {
   VkDescriptorType type;

   union {
      struct {
         struct pvr_buffer_view *bview;
         pvr_dev_addr_t buffer_dev_addr;
         VkDeviceSize buffer_desc_range;
         VkDeviceSize buffer_whole_range;
      };

      struct {
         VkImageLayout layout;
         const struct pvr_image_view *iview;
         const struct pvr_sampler *sampler;
      };
   };
};

struct pvr_descriptor_set {
   struct vk_object_base base;
   struct list_head link; /** Link in pvr_descriptor_pool::desc_sets. */

   struct pvr_descriptor_set_layout *layout;
   struct pvr_descriptor_pool *pool;

   unsigned size; /** Descriptor set size. */
   pvr_dev_addr_t dev_addr; /** Descriptor set device address. */
   void *mapping; /** Descriptor set CPU mapping. */

   struct pvr_buffer_descriptor dynamic_buffers[];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_descriptor_set_layout,
                               vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_descriptor_set,
                               base,
                               VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_descriptor_pool,
                               base,
                               VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

static inline struct pvr_descriptor_set_layout *
vk_to_pvr_descriptor_set_layout(struct vk_descriptor_set_layout *layout)
{
   return container_of(layout, struct pvr_descriptor_set_layout, vk);
}

#endif /* PVR_DESCRIPTOR_SET_H */
