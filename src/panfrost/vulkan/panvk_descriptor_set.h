/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DESCRIPTOR_SET_H
#define PANVK_DESCRIPTOR_SET_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "util/bitset.h"
#include "util/vma.h"

#include "panvk_macros.h"

#include "vk_descriptor_update_template.h"
#include "vk_object.h"

#include "panvk_descriptor_set_layout.h"

struct panvk_priv_bo;
struct panvk_sysvals;
struct panvk_descriptor_pool;
struct panvk_descriptor_set_layout;

struct panvk_opaque_desc {
   uint32_t data[PANVK_DESCRIPTOR_SIZE / sizeof(uint32_t)];
};

#if PAN_ARCH < 9
struct panvk_ssbo_addr {
   uint64_t base_addr;
   uint32_t size;
   uint32_t zero[5]; /* Must be zero! */
};
#endif

struct panvk_descriptor_set {
   struct vk_object_base base;
   struct panvk_descriptor_pool *pool;
   const struct panvk_descriptor_set_layout *layout;
   struct {
      uint64_t dev;
      void *host;

      /* Dirty range, or SIZE_MAX, 0 for clean */
      size_t dirty_min, dirty_max;
   } descs;

   struct {
      uint64_t dev_addr;
      uint64_t size;
   } dyn_bufs[MAX_DYNAMIC_BUFFERS];

   /* Includes adjustment for variable-sized descriptors */
   unsigned desc_count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

struct panvk_descriptor_pool {
   struct vk_object_base base;
   struct panvk_priv_bo *desc_bo;
   /* Set if the pool was created with VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT. */
   uintptr_t host_only_mem;
   struct util_vma_heap desc_heap;

   /* Initialize to ones */
   BITSET_WORD *free_sets;

   /* Every free_sets word below this index is 0, so the first-free search
    * starts here (PANVK_DESC_POOL_HINT). */
   uint32_t free_sets_hint;

   uint32_t max_sets;
   struct panvk_descriptor_set *sets;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

VkResult panvk_per_arch(descriptor_set_write)(struct panvk_descriptor_set *set,
                                              const VkWriteDescriptorSet *write,
                                              bool write_immutable_samplers);

void panvk_per_arch(descriptor_set_write_template)(
   struct panvk_descriptor_set *set,
   const struct vk_descriptor_update_template *template, const void *data,
   bool write_immutable_samplers);

void panvk_per_arch(descriptor_set_flush)(struct panvk_descriptor_set *set);

#endif /* PANVK_VX_DESCRIPTOR_SET_H */
