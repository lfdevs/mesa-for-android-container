/*
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DESCRIPTOR_POOL_H
#define RADV_DESCRIPTOR_POOL_H

#include "vk_object.h"

#include <vulkan/vulkan.h>

#include "util/list.h"
#include "util/vma.h"

/* The vma heap reserves 0 to mean NULL; we have to offset by some amount to ensure we can allocate
 * the entire BO without hitting zero. The actual amount doesn't matter.
 */
#define RADV_POOL_HEAP_OFFSET 32

struct radv_descriptor_set;

struct radv_descriptor_pool {
   struct vk_object_base base;
   struct radeon_winsys_bo *bo;
   uint8_t *host_bo;
   uint8_t *mapped_ptr;
   uint64_t current_offset;
   uint64_t size;

   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;

   struct list_head sets;

   struct util_vma_heap bo_heap;

   uint32_t entry_count;
   uint32_t max_entry_count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_descriptor_pool, base, VkDescriptorPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL)

#endif /* RADV_DESCRIPTOR_POOL_H */
