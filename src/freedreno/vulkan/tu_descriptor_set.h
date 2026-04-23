/*
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_DESCRIPTOR_SET_H
#define TU_DESCRIPTOR_SET_H

#include "tu_common.h"

#include "util/vma.h"
#include "vk_descriptor_set_layout.h"

#include "common/freedreno_pm4.h"
#include "fdl/fd6_format_table.h"

/* The hardware supports up to 8 descriptor sets since A7XX.
 * Note: This is the maximum across generations, not the maximum for a
 * particular generation so it should only be used for allocation.
 */
#define MAX_SETS 8

/* I have no idea what the maximum size is, but the hardware supports very
 * large numbers of descriptors (at least 2^16). This limit is based on
 * CP_LOAD_STATE6, which has a 28-bit field for the DWORD offset, so that
 * we don't have to think about what to do if that overflows, but really
 * nothing is likely to get close to this.
 */
#define MAX_SET_SIZE ((1 << 28) * 4)

/* The vma heap reserves 0 to mean NULL; we have to offset by some amount to
 * ensure we can allocate the entire BO without hitting zero. The actual
 * amount doesn't matter.
 */
#define TU_POOL_HEAP_OFFSET 32

struct tu_descriptor_set_binding_layout
{
   VkDescriptorType type;

   /* Number of array elements in this binding */
   uint32_t array_size;

   /* The size in bytes of each Vulkan descriptor. */
   uint32_t size;

   uint32_t offset;

   /* Byte offset in the array of dynamic descriptors (offsetted by
    * tu_pipeline_layout::set::dynamic_offset_start).
    */
   uint32_t dynamic_offset_offset;

   /* Offset in the tu_descriptor_set_layout of the immutable samplers, or 0
    * if there are no immutable samplers. */
   uint32_t immutable_samplers_offset;

   /* Offset in the tu_descriptor_set_layout of the ycbcr samplers, or 0
    * if there are no immutable samplers. */
   uint32_t ycbcr_samplers_offset;

   /* Shader stages that use this binding */
   uint32_t shader_stages;
};

struct tu_descriptor_set_layout
{
   struct vk_descriptor_set_layout vk;

   /* The create flags for this descriptor set layout */
   VkDescriptorSetLayoutCreateFlags flags;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Total size of the descriptor set with room for all array entries */
   uint32_t size;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;

   /* Size of dynamic offset descriptors used by this descriptor set */
   uint16_t dynamic_offset_size;

   bool has_immutable_samplers;
   bool has_variable_descriptors;
   bool has_inline_uniforms;

   struct tu_bo *embedded_samplers;

   /* Bindings in this descriptor set */
   struct tu_descriptor_set_binding_layout binding[0];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set_layout, vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

struct tu_pipeline_layout
{
   struct vk_object_base base;

   struct
   {
      struct tu_descriptor_set_layout *layout;
      uint32_t size;
   } set[MAX_SETS];

   uint32_t num_sets;
   uint32_t push_constant_size;

   unsigned char blake3[BLAKE3_KEY_LEN];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_pipeline_layout, base, VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)

void tu_pipeline_layout_init(struct tu_pipeline_layout *layout);

struct tu_descriptor_set
{
   struct vk_object_base base;

   /* Link to descriptor pool's desc_sets list . */
   struct list_head pool_link;

   struct tu_descriptor_set_layout *layout;
   struct tu_descriptor_pool *pool;
   uint32_t offset;
   uint32_t size;

   uint64_t va;
   /* Pointer to the GPU memory for the set for non-push descriptors, or pointer
    * to a host memory copy for push descriptors.
    */
   uint32_t *mapped_ptr;

   /* Size of the host memory allocation for push descriptors */
   uint32_t host_size;

   uint32_t *dynamic_descriptors;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

struct tu_descriptor_pool
{
   struct vk_object_base base;

   struct tu_bo *bo;
   uint64_t current_offset;
   uint64_t size;

   uint8_t *host_memory_base;
   uint8_t *host_memory_ptr;
   uint8_t *host_memory_end;
   uint8_t *host_bo;

   struct list_head desc_sets;

   struct util_vma_heap bo_heap;

   uint32_t entry_count;
   uint32_t max_entry_count;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

struct tu_descriptor_update_template_entry
{
   VkDescriptorType descriptor_type;

   /* The number of descriptors to update */
   uint32_t descriptor_count;

   /* Into mapped_ptr or dynamic_descriptors, in units of the respective array
    */
   uint32_t dst_offset;

   /* In dwords. Not valid/used for dynamic descriptors */
   uint32_t dst_stride;

   uint32_t buffer_offset;

   /* Only valid for combined image samplers and samplers */
   const struct tu_sampler *immutable_samplers;

   /* In bytes */
   size_t src_offset;
   size_t src_stride;

   /* For push descriptors */
   bool copy_immutable_samplers;
};

struct tu_descriptor_update_template
{
   struct vk_object_base base;

   uint32_t entry_count;
   VkPipelineBindPoint bind_point;
   struct tu_descriptor_update_template_entry entry[0];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(tu_descriptor_update_template, base,
                               VkDescriptorUpdateTemplate,
                               VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE)

template <chip CHIP>
void
tu_update_descriptor_sets(const struct tu_device *device,
                          VkDescriptorSet overrideSet,
                          uint32_t descriptorWriteCount,
                          const VkWriteDescriptorSet *pDescriptorWrites,
                          uint32_t descriptorCopyCount,
                          const VkCopyDescriptorSet *pDescriptorCopies);

template <chip CHIP>
void
tu_update_descriptor_set_with_template(
   const struct tu_device *device,
   struct tu_descriptor_set *set,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
   const void *pData);

static inline const struct tu_sampler *
tu_immutable_samplers(const struct tu_descriptor_set_layout *set,
                      const struct tu_descriptor_set_binding_layout *binding)
{
   if (!binding->immutable_samplers_offset)
      return NULL;

   return (struct tu_sampler *) ((const char *) set +
                                 binding->immutable_samplers_offset);
}

static inline const struct vk_ycbcr_conversion_state *
tu_immutable_ycbcr_samplers(const struct tu_descriptor_set_layout *set,
                            const struct tu_descriptor_set_binding_layout *binding)
{
   if (!binding->ycbcr_samplers_offset)
      return NULL;

   return (
      struct vk_ycbcr_conversion_state *) ((const char *) set +
                                           binding->ycbcr_samplers_offset);
}

/*
 * Helpers for modifying descriptors:
 */

#define tu_swiz(x, y, z, w) (uint8_t[]){ PIPE_SWIZZLE_ ##x, PIPE_SWIZZLE_ ##y, PIPE_SWIZZLE_ ##z, PIPE_SWIZZLE_ ##w }

template <chip CHIP>
static inline void
tu_desc_set_swiz(uint32_t *desc, const uint8_t (&swiz)[4])
{
   if (CHIP >= A8XX) {
      desc[3] = pkt_field_set(A8XX_TEX_MEMOBJ_3_SWIZ_X, desc[3], fdl8_swiz(swiz[0]));
      desc[3] = pkt_field_set(A8XX_TEX_MEMOBJ_3_SWIZ_Y, desc[3], fdl8_swiz(swiz[1]));
      desc[3] = pkt_field_set(A8XX_TEX_MEMOBJ_3_SWIZ_Z, desc[3], fdl8_swiz(swiz[2]));
      desc[3] = pkt_field_set(A8XX_TEX_MEMOBJ_3_SWIZ_W, desc[3], fdl8_swiz(swiz[3]));
   } else {
      desc[0] = pkt_field_set(A6XX_TEX_MEMOBJ_0_SWIZ_X, desc[0], fdl6_swiz(swiz[0]));
      desc[0] = pkt_field_set(A6XX_TEX_MEMOBJ_0_SWIZ_Y, desc[0], fdl6_swiz(swiz[1]));
      desc[0] = pkt_field_set(A6XX_TEX_MEMOBJ_0_SWIZ_Z, desc[0], fdl6_swiz(swiz[2]));
      desc[0] = pkt_field_set(A6XX_TEX_MEMOBJ_0_SWIZ_W, desc[0], fdl6_swiz(swiz[3]));
   }
}

template <chip CHIP>
static inline enum a6xx_format
tu_desc_get_format(uint32_t *desc)
{
   if (CHIP >= A8XX) {
      return (enum a6xx_format)pkt_field_get(A8XX_TEX_MEMOBJ_3_FMT, desc[3]);
   } else {
      return (enum a6xx_format)pkt_field_get(A6XX_TEX_MEMOBJ_0_FMT, desc[0]);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_format(uint32_t *desc, enum a6xx_format fmt)
{
   if (CHIP >= A8XX) {
      desc[3] = pkt_field_set(A8XX_TEX_MEMOBJ_3_FMT, desc[3], fmt);
   } else {
      desc[0] = pkt_field_set(A6XX_TEX_MEMOBJ_0_FMT, desc[0], fmt);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_swap(uint32_t *desc, enum a3xx_color_swap swap)
{
   if (CHIP >= A8XX) {
      desc[3] = pkt_field_set(A8XX_TEX_MEMOBJ_3_SWAP, desc[3], swap);
   } else {
      desc[0] = pkt_field_set(A6XX_TEX_MEMOBJ_0_SWAP, desc[0], swap);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_tile_mode(uint32_t *desc, enum a6xx_tile_mode tile_mode)
{
   if (CHIP >= A8XX) {
      desc[4] = pkt_field_set(A8XX_TEX_MEMOBJ_4_TILE_MODE, desc[4], tile_mode);
   } else {
      desc[0] = pkt_field_set(A6XX_TEX_MEMOBJ_0_TILE_MODE, desc[0], tile_mode);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_tile_all(uint32_t *desc, bool tile_all)
{
   if (CHIP >= A8XX) {
      if (tile_all) {
         desc[4] |= A8XX_TEX_MEMOBJ_4_TILE_ALL;
      } else {
         desc[4] &= ~A8XX_TEX_MEMOBJ_4_TILE_ALL;
      }
   } else {
      if (tile_all) {
         desc[3] |= A6XX_TEX_MEMOBJ_3_TILE_ALL;
      } else {
         desc[3] &= ~A6XX_TEX_MEMOBJ_3_TILE_ALL;
      }
   }
}

template <chip CHIP>
static inline enum a6xx_tex_type
tu_desc_get_type(uint32_t *desc)
{
   if (CHIP >= A8XX) {
     return (enum a6xx_tex_type)pkt_field_get(A8XX_TEX_MEMOBJ_1_TYPE, desc[1]);
   } else {
     return (enum a6xx_tex_type)pkt_field_get(A6XX_TEX_MEMOBJ_2_TYPE, desc[2]);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_type(uint32_t *desc, enum a6xx_tex_type type)
{
   if (CHIP >= A8XX) {
      desc[1] = pkt_field_set(A8XX_TEX_MEMOBJ_1_TYPE, desc[1], type);
   } else {
      desc[2] = pkt_field_set(A6XX_TEX_MEMOBJ_2_TYPE, desc[2], type);
   }
}

template <chip CHIP>
static inline void
tu_desc_get_dim(uint32_t *desc, uint32_t *width, uint32_t *height)
{
   if (CHIP >= A8XX) {
      *width  = pkt_field_get(A8XX_TEX_MEMOBJ_2_WIDTH, desc[2]);
      *height = pkt_field_get(A8XX_TEX_MEMOBJ_2_HEIGHT, desc[2]);
   } else {
      *width  = pkt_field_get(A6XX_TEX_MEMOBJ_1_WIDTH, desc[1]);
      *height = pkt_field_get(A6XX_TEX_MEMOBJ_1_HEIGHT, desc[1]);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_dim(uint32_t *desc, uint32_t width, uint32_t height)
{
   if (CHIP >= A8XX) {
      desc[2] = pkt_field_set(A8XX_TEX_MEMOBJ_2_WIDTH, desc[2], width);
      desc[2] = pkt_field_set(A8XX_TEX_MEMOBJ_2_HEIGHT, desc[2], height);
   } else {
      desc[1] = pkt_field_set(A6XX_TEX_MEMOBJ_1_WIDTH, desc[1], width);
      desc[1] = pkt_field_set(A6XX_TEX_MEMOBJ_1_HEIGHT, desc[1], height);
   }
}

template <chip CHIP>
static inline uint32_t
tu_desc_get_depth(uint32_t *desc)
{
   if (CHIP >= A8XX) {
      return pkt_field_get(A8XX_TEX_MEMOBJ_1_DEPTH, desc[1]);
   } else {
      return pkt_field_get(A6XX_TEX_MEMOBJ_5_DEPTH, desc[5]);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_depth(uint32_t *desc, uint32_t depth)
{
   if (CHIP >= A8XX) {
      desc[1] = pkt_field_set(A8XX_TEX_MEMOBJ_1_DEPTH, desc[1], depth);
   } else {
      desc[5] = pkt_field_set(A6XX_TEX_MEMOBJ_5_DEPTH, desc[5], depth);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_struct_size_texels(uint32_t *desc, uint32_t struct_size_texels)
{
   if (CHIP >= A8XX) {
      desc[1] = pkt_field_set(A8XX_TEX_MEMOBJ_1_STRUCTSIZETEXELS, desc[1], struct_size_texels);
   } else {
      desc[2] = pkt_field_set(A6XX_TEX_MEMOBJ_2_STRUCTSIZETEXELS, desc[2], struct_size_texels);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_min_line_offset(uint32_t *desc, uint32_t min_line_offset)
{
   if (CHIP >= A8XX) {
      desc[6] = pkt_field_set(A8XX_TEX_MEMOBJ_6_MIN_LINE_OFFSET, desc[6], min_line_offset);
   } else {
      desc[2] = pkt_field_set(A6XX_TEX_MEMOBJ_2_PITCHALIGN, desc[2], min_line_offset);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_tex_line_offset(uint32_t *desc, uint32_t tex_line_offset)
{
   if (CHIP >= A8XX) {
      desc[6] = pkt_field_set(A8XX_TEX_MEMOBJ_6_TEX_LINE_OFFSET, desc[6], tex_line_offset);
   } else {
      desc[2] = pkt_field_set(A6XX_TEX_MEMOBJ_2_PITCH, desc[2], tex_line_offset);
   }
}

template <chip CHIP>
static inline void
tu_desc_set_array_slice_offset(uint32_t *desc, uint32_t array_slice_offset)
{
   if (CHIP >= A8XX) {
      desc[7] = pkt_field_set(A8XX_TEX_MEMOBJ_7_ARRAY_SLICE_OFFSET, desc[7], array_slice_offset);
   } else {
      desc[3] = pkt_field_set(A6XX_TEX_MEMOBJ_3_ARRAY_PITCH, desc[3], array_slice_offset);
   }
}

template <chip CHIP>
static inline uint64_t
tu_desc_get_addr(uint32_t *desc)
{
   uint64_t addr = 0;

   if (CHIP >= A8XX) {
      addr = pkt_field_get(A8XX_TEX_MEMOBJ_0_BASE_LO, desc[0]) << 6;
      addr |= (uint64_t)pkt_field_get(A8XX_TEX_MEMOBJ_1_BASE_HI, desc[1]) << 32;
   } else {
      addr = pkt_field_get(A6XX_TEX_MEMOBJ_4_BASE_LO, desc[4]) << 5;
      addr |= (uint64_t)pkt_field_get(A6XX_TEX_MEMOBJ_5_BASE_HI, desc[5]) << 32;
   }

   return addr;
}

template <chip CHIP>
static inline void
tu_desc_set_addr(uint32_t *desc, uint64_t addr)
{
   if (CHIP >= A8XX) {
      desc[0] = pkt_field_set(A8XX_TEX_MEMOBJ_0_BASE_LO, desc[0], addr);
      desc[1] = pkt_field_set(A8XX_TEX_MEMOBJ_1_BASE_HI, desc[1], addr >> 32);
   } else {
      desc[4] = pkt_field_set(A6XX_TEX_MEMOBJ_4_BASE_LO, desc[4], addr);
      desc[5] = pkt_field_set(A6XX_TEX_MEMOBJ_5_BASE_HI, desc[5], addr >> 32);
   }
}

template <chip CHIP>
static inline uint64_t
tu_desc_get_buffer_addr(uint32_t *desc)
{
   if (CHIP < A8XX)
      return tu_desc_get_addr<CHIP>(desc);

   uint64_t addr = pkt_field_get(A8XX_TEX_MEMOBJ_0_INSTANCE_DESC_BASE_LO, desc[0]);
   addr |= (uint64_t)pkt_field_get(A8XX_TEX_MEMOBJ_1_INSTANCE_DESC_BASE_HI, desc[1]) << 32;
   return addr;
}

template <chip CHIP>
static inline void
tu_desc_set_buffer_addr(uint32_t *desc, uint64_t addr)
{
   if (CHIP < A8XX)
      return tu_desc_set_addr<CHIP>(desc, addr);

   desc[0] = pkt_field_set(A8XX_TEX_MEMOBJ_0_INSTANCE_DESC_BASE_LO, desc[0], addr);
   desc[1] = pkt_field_set(A8XX_TEX_MEMOBJ_1_INSTANCE_DESC_BASE_HI, desc[1], addr >> 32);
}

template <chip CHIP>
static inline uint64_t
tu_desc_get_ubwc(uint32_t *desc)
{
   uint64_t addr = 0;

   if ((CHIP >= A8XX) && (desc[4] & A8XX_TEX_MEMOBJ_4_FLAG)) {
      addr = pkt_field_get(A8XX_TEX_MEMOBJ_4_FLAG_LO, desc[4]) << 6;
      addr |= (uint64_t)pkt_field_get(A8XX_TEX_MEMOBJ_5_FLAG_HI, desc[5]) << 32;
   } else if ((CHIP <= A7XX) && (desc[3] & A6XX_TEX_MEMOBJ_3_FLAG)) {
      addr = pkt_field_get(A6XX_TEX_MEMOBJ_7_FLAG_LO, desc[7]) << 5;
      addr |= (uint64_t)pkt_field_get(A6XX_TEX_MEMOBJ_8_FLAG_HI, desc[8]) << 32;
   }

   return addr;
}

template <chip CHIP>
static inline void
tu_desc_set_ubwc(uint32_t *desc, uint64_t addr)
{
   if (CHIP >= A8XX) {
      if (addr) {
         desc[4] = pkt_field_set(A8XX_TEX_MEMOBJ_4_FLAG_LO, desc[4], addr);
         desc[5] = pkt_field_set(A8XX_TEX_MEMOBJ_5_FLAG_HI, desc[5], addr >> 32);
         desc[4] |= A8XX_TEX_MEMOBJ_4_FLAG;
      } else {
         desc[4] &= ~A8XX_TEX_MEMOBJ_4_FLAG;
      }
   } else {
      if (addr) {
         desc[7] = pkt_field_set(A6XX_TEX_MEMOBJ_7_FLAG_LO, desc[7], addr);
         desc[8] = pkt_field_set(A6XX_TEX_MEMOBJ_8_FLAG_HI, desc[8], addr >> 32);
         desc[3] |= A6XX_TEX_MEMOBJ_3_FLAG;
      } else {
         desc[3] &= ~A6XX_TEX_MEMOBJ_3_FLAG;
      }
   }
}

#endif /* TU_DESCRIPTOR_SET_H */
