/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_DESCRIPTOR_SET
#define KK_DESCRIPTOR_SET 1

#include "kk_private.h"

#include "kk_descriptor_types.h"
#include "kk_device.h"

#include "vk_descriptor_update_template.h"
#include "vk_object.h"

#include "util/list.h"
#include "util/vma.h"

struct kk_descriptor_set_layout;
struct kk_bo;

struct kk_descriptor_pool {
   struct vk_object_base base;

   struct list_head sets;

   struct kk_bo *bo;
   struct util_vma_heap heap;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

struct kk_descriptor_set {
   struct vk_object_base base;

   /* Link in kk_descriptor_pool::sets */
   struct list_head link;

   struct kk_descriptor_set_layout *layout;
   void *mapped_ptr;
   uint64_t addr;
   uint32_t size;

   struct kk_buffer_address dynamic_buffers[];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

static inline struct kk_buffer_address
kk_descriptor_set_addr(const struct kk_descriptor_set *set)
{
   return (struct kk_buffer_address){
      .base_addr = set->addr,
      .size = set->size,
   };
}

struct kk_push_descriptor_set {
   uint8_t data[KK_PUSH_DESCRIPTOR_SET_SIZE];
   struct kk_descriptor_set_layout *layout;
};

void kk_push_descriptor_set_update(struct kk_push_descriptor_set *push_set,
                                   uint32_t write_count,
                                   const VkWriteDescriptorSet *writes);

void kk_push_descriptor_set_update_template(
   struct kk_push_descriptor_set *push_set,
   struct kk_descriptor_set_layout *layout,
   const struct vk_descriptor_update_template *template, const void *data);

#endif
