/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DESCRIPTOR_SET
#define NVK_DESCRIPTOR_SET 1

#include "nvk_private.h"

#include "nvk_device.h"
#include "nvk_physical_device.h"
#include "vk_object.h"
#include "vk_descriptor_update_template.h"

#include "util/vma.h"
#include "util/list.h"

struct nvk_descriptor_set_layout;
struct nvkmd_mem;

#define NVK_IMAGE_DESCRIPTOR_IMAGE_INDEX_MASK   0x000fffff
#define NVK_IMAGE_DESCRIPTOR_SAMPLER_INDEX_MASK 0xfff00000

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_sampled_image_descriptor {
   unsigned image_index:20;
   unsigned sampler_index:12;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_sampled_image_descriptor) == 4,
              "nvk_sampled_image_descriptor has no holes");

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_storage_image_descriptor {
   unsigned image_index:20;
   unsigned sw_log2:2;
   unsigned sh_log2:2;
   unsigned pad:8;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_storage_image_descriptor) == 4,
              "nvk_storage_image_descriptor has no holes");

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_buffer_view_descriptor {
   unsigned image_index:20;
   unsigned pad:12;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_buffer_view_descriptor) == 4,
              "nvk_buffer_view_descriptor has no holes");

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_bindless_cbuf {
   uint64_t base_addr_shift_4:45;
   uint64_t size_shift_4:19;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_bindless_cbuf) == 8,
              "nvk_bindless_cbuf has no holes");

/* This has to match nir_address_format_64bit_bounded_global */
PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_buffer_address {
   uint64_t base_addr;
   uint32_t size;
   uint32_t zero; /* Must be zero! */
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_buffer_address) == 16,
              "nvk_buffer_address has no holes");

union nvk_buffer_descriptor {
   struct nvk_buffer_address addr;
   struct nvk_bindless_cbuf cbuf;
};

static inline bool
nvk_use_bindless_cbuf(const struct nv_device_info *info)
{
   return info->cls_eng3d >= 0xC597 /* TURING_A */;
}

static inline struct nvk_buffer_address
nvk_ubo_descriptor_addr(const struct nvk_physical_device *pdev,
                        union nvk_buffer_descriptor desc)
{
   if (nvk_use_bindless_cbuf(&pdev->info)) {
      return (struct nvk_buffer_address) {
         .base_addr = desc.cbuf.base_addr_shift_4 << 4,
         .size = desc.cbuf.size_shift_4 << 4,
      };
   } else {
      return desc.addr;
   }
}

#define NVK_BUFFER_ADDRESS_NULL ((struct nvk_buffer_address) { .size = 0 })

struct nvk_descriptor_pool {
   struct vk_object_base base;

   struct list_head sets;

   struct nvkmd_mem *mem;
   struct util_vma_heap heap;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

struct nvk_descriptor_set {
   struct vk_object_base base;

   /* Link in nvk_descriptor_pool::sets */
   struct list_head link;

   struct nvk_descriptor_set_layout *layout;
   void *mapped_ptr;
   uint64_t addr;
   uint32_t size;

   union nvk_buffer_descriptor dynamic_buffers[];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_descriptor_set, base, VkDescriptorSet,
                       VK_OBJECT_TYPE_DESCRIPTOR_SET)

static inline struct nvk_buffer_address
nvk_descriptor_set_addr(const struct nvk_descriptor_set *set)
{
   return (struct nvk_buffer_address) {
      .base_addr = set->addr,
      .size = set->size,
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
