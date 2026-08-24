/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DESCRIPTOR_TYPES
#define NVK_DESCRIPTOR_TYPES 1

#include "nvk_private.h"

#include "nvk_instance.h"
#include "nvk_physical_device.h"

#include "nil.h"

#define NVK_IMAGE_DESCRIPTOR_IMAGE_INDEX_MASK   0x000fffff
#define NVK_IMAGE_DESCRIPTOR_SAMPLER_INDEX_MASK 0xfff00000

/** Hardware(ish) descriptor type used for images and buffer views.
 *
 * For all textures and Maxwell+ storage images, this is the hardware
 * descriptor placed in the texture header pool.  For Kepler storage images,
 * it's the su_info produced by NIL and consumed by NAK.
 *
 * All other image descriptor types used by NVK are for descriptor sets and
 * contain indices which indirectly reference the texture header pool.  The
 * only exception is that, on Kepler, we can put the su_info directly in the
 * descriptor set because it's all software anyway.
 */
union nvk_image_descriptor {
   struct nil_descriptor desc;
   struct nil_su_info su_info;
};
static_assert(sizeof(struct nil_descriptor) == NVK_TEXTURE_HEADER_SIZE,
              "All image heap descriptors are 32 bytes");
static_assert(sizeof(struct nil_su_info) == NVK_TEXTURE_HEADER_SIZE,
              "All image heap descriptors are 32 bytes");
static_assert(sizeof(union nvk_image_descriptor) == NVK_TEXTURE_HEADER_SIZE,
              "All image descriptors are 32 bytes");

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
   unsigned _pad:12;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_storage_image_descriptor) == 4,
              "nvk_storage_image_descriptor has no holes");

struct nvk_kepler_storage_image_descriptor {
   struct nil_su_info su_info;
};

struct nvk_kepler_storage_buffer_view_descriptor {
   struct nil_su_info su_info;
};

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
/** See also nvk_edb_bview_cache */
struct nvk_edb_buffer_view_descriptor {
   /** Index of the HW descriptor in the texture/image table */
   uint32_t index;
   /** Offset into the HW descriptor in surface elements */
   uint32_t offset_el;
   /** Size of the virtual descriptor in surface elements */
   uint32_t size_el;
   /** Value returned in the alpha channel for OOB buffer access */
   uint32_t oob_alpha;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_edb_buffer_view_descriptor) == 16,
              "nvk_edb_buffer_view_descriptor has no holes");

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_bindless_cbuf {
   uint64_t base_addr_shift_4:45;
   uint64_t size_shift_4:19;
   /* For descriptor buffers, avoid returning garbage data.
    * The descriptor payload must be invariant. */
   uint64_t padding;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_bindless_cbuf) == 16,
              "nvk_bindless_cbuf has no holes");

/* Hopper+ uses a new cbuf format */
PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct nvk_bindless_cbuf_2 {
   uint64_t base_addr_shift_6:51;
   uint64_t size_shift_4:13;
   /* For descriptor buffers, avoid returning garbage data.
    * The descriptor payload must be invariant. */
   uint64_t padding;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_bindless_cbuf_2) == 16,
              "nvk_bindless_cbuf_2 has no holes");

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

#define NVK_BUFFER_ADDRESS_NULL ((struct nvk_buffer_address) { .size = 0 })

union nvk_buffer_descriptor {
   struct nvk_buffer_address addr;
   struct nvk_bindless_cbuf cbuf;
   struct nvk_bindless_cbuf_2 cbuf2;
   uint32_t values[4];
};

static inline bool
nvk_use_bindless_cbuf(const struct nv_device_info *info)
{
   return info->cls_eng3d >= 0xC597 /* TURING_A */;
}

static inline bool
nvk_use_bindless_cbuf_2(const struct nv_device_info *info)
{
   return info->cls_eng3d >= 0xCB97 /* HOPPER_A */;
}

static inline union nvk_buffer_descriptor
nvk_ubo_descriptor(const struct nvk_physical_device *pdev,
                   VkDeviceAddressRangeEXT addr_range)
{
   const uint32_t min_cbuf_alignment = nvk_min_cbuf_alignment(&pdev->info);

   assert(addr_range.address % min_cbuf_alignment == 0);
   assert(addr_range.size <= NVK_MAX_CBUF_SIZE);

   addr_range.address = ROUND_DOWN_TO(addr_range.address, min_cbuf_alignment);
   addr_range.size = align(addr_range.size, min_cbuf_alignment);

   if (nvk_use_bindless_cbuf_2(&pdev->info)) {
      return (union nvk_buffer_descriptor) { .cbuf2 = {
         .base_addr_shift_6 = addr_range.address >> 6,
         .size_shift_4 = addr_range.size >> 4,
      }};
   } else if (nvk_use_bindless_cbuf(&pdev->info)) {
      return (union nvk_buffer_descriptor) { .cbuf = {
         .base_addr_shift_4 = addr_range.address >> 4,
         .size_shift_4 = addr_range.size >> 4,
      }};
   } else {
      return (union nvk_buffer_descriptor) { .addr = {
         .base_addr = addr_range.address,
         .size = addr_range.size,
      }};
   }
}

static inline struct nvk_buffer_address
nvk_ubo_descriptor_addr(const struct nvk_physical_device *pdev,
                        union nvk_buffer_descriptor desc)
{
   if (nvk_use_bindless_cbuf_2(&pdev->info)) {
      return (struct nvk_buffer_address) {
         .base_addr = desc.cbuf2.base_addr_shift_6 << 6,
         .size = desc.cbuf2.size_shift_4 << 4,
      };
   } else if (nvk_use_bindless_cbuf(&pdev->info)) {
      return (struct nvk_buffer_address) {
         .base_addr = desc.cbuf.base_addr_shift_4 << 4,
         .size = desc.cbuf.size_shift_4 << 4,
      };
   } else {
      return desc.addr;
   }
}

static inline union nvk_buffer_descriptor
nvk_ssbo_descriptor(const struct nvk_physical_device *pdev,
                    VkDeviceAddressRangeEXT addr_range)
{
   const struct nvk_instance *instance = nvk_physical_device_instance(pdev);
   const uint32_t min_ssbo_alignment = nvk_min_ssbo_alignment(instance);
   assert(addr_range.address % min_ssbo_alignment == 0);
   assert(addr_range.size <= UINT32_MAX);

   addr_range.address = ROUND_DOWN_TO(addr_range.address, min_ssbo_alignment);
   addr_range.size = align(addr_range.size, NVK_SSBO_BOUNDS_CHECK_ALIGNMENT);

   return (union nvk_buffer_descriptor) { .addr = {
      .base_addr = addr_range.address,
      .size = addr_range.size,
   }};
}

#endif /* NVK_DESCRIPTOR_TYPES */
