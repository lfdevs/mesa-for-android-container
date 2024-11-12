/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DESCRIPTOR_TYPES
#define NVK_DESCRIPTOR_TYPES 1

#include "nvk_private.h"

#include "nvk_physical_device.h"

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
   unsigned _pad:8;

   /* A 32-bit integer which acts as a map from sample index to x/y position
    * within a pixel.  Each nibble is a sample with x in the low 2 bits and y
    * in the high 2 bits.
    */
   unsigned sample_map:32;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct nvk_storage_image_descriptor) == 8,
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

#define NVK_BUFFER_ADDRESS_NULL ((struct nvk_buffer_address) { .size = 0 })

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

#endif /* NVK_DESCRIPTOR_TYPES */
