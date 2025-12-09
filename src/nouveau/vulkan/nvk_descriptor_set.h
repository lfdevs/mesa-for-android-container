/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DESCRIPTOR_SET
#define NVK_DESCRIPTOR_SET 1

#include "nvk_private.h"

#include "nvk_descriptor_types.h"
#include "nvk_device.h"
#include "nvk_physical_device.h"
#include "vk_object.h"
#include "vk_descriptor_update_template.h"

#include "util/vma.h"
#include "util/list.h"

struct nvk_descriptor_set_layout;
struct nvkmd_mem;

struct nvk_descriptor_pool {
   struct vk_object_base base;

   struct list_head sets;

   uint64_t mem_size_B;
   struct nvkmd_mem *mem;
   void *host_mem;
   struct util_vma_heap heap;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

struct nvk_descriptor_set {
   struct vk_object_base base;

   struct nvk_descriptor_pool *pool;

   /* Link in nvk_descriptor_pool::sets */
   struct list_head link;

   struct nvk_descriptor_set_layout *layout;
   void *map;
   uint64_t mem_offset_B;
   uint32_t size_B;

   union nvk_buffer_descriptor dynamic_buffers[];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_descriptor_set, base, VkDescriptorSet,
                       VK_OBJECT_TYPE_DESCRIPTOR_SET)

static inline struct nvk_buffer_address
nvk_descriptor_set_addr(const struct nvk_descriptor_set *set)
{
   return (struct nvk_buffer_address) {
      .base_addr = set->pool->mem->va->addr + set->mem_offset_B,
      .size = set->size_B,
   };
}

struct nvk_push_descriptor_set {
   uint8_t data[NVK_PUSH_DESCRIPTOR_SET_SIZE];
};

void
nvk_push_descriptor_set_update(struct nvk_device *dev,
                               struct nvk_push_descriptor_set *push_set,
                               struct nvk_descriptor_set_layout *layout,
                               uint32_t write_count,
                               const VkWriteDescriptorSet *writes);

void
nvk_push_descriptor_set_update_template(
   struct nvk_device *dev,
   struct nvk_push_descriptor_set *push_set,
   struct nvk_descriptor_set_layout *layout,
   const struct vk_descriptor_update_template *template,
   const void *data);

#endif
