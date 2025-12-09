/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DESCRIPTOR_SET_LAYOUT_H
#define PANVK_DESCRIPTOR_SET_LAYOUT_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_descriptor_set_layout.h"
#include "vk_util.h"

#include "util/mesa-blake3.h"

#include "genxml/gen_macros.h"

#define PANVK_DESCRIPTOR_SIZE         32
#define MAX_DYNAMIC_UNIFORM_BUFFERS   16
#define MAX_DYNAMIC_STORAGE_BUFFERS   8
#define MAX_PUSH_DESCS                32
#define MAX_INLINE_UNIFORM_BLOCK_SIZE (1 << 16)
#define MAX_DYNAMIC_BUFFERS                                                    \
   (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)

#if PAN_ARCH < 9

/* On Bifrost, this is a software limit. We pick the minimum required by
 * Vulkan, because Bifrost GPUs don't have unified descriptor tables,
 * which forces us to aggregate all descriptors from all sets and dispatch
 * them to per-type descriptor tables emitted at draw/dispatch time. The
 * more sets we support the more copies we are likely to have to do at
 * draw time. */
#define MAX_SETS 4

/* MALI_RENDERER_STATE::sampler_count is 16-bit. */
#define MAX_PER_SET_SAMPLERS UINT16_MAX
/* MALI_RENDERER_STATE::sampler_count is 16-bit. */
#define MAX_PER_SET_SAMPLED_IMAGES UINT16_MAX
/* MALI_RENDERER_STATE::uniform_buffer_count is 8-bit. We reserve 32 slots for
 * our internal UBOs. */
#define MAX_PER_SET_UNIFORM_BUFFERS (UINT8_MAX - 32)
/* SSBOs are limited by the size of a uniform buffer which contains our
 * panvk_ssbo_addr objects. panvk_ssbo_addr is 16-byte, and each uniform entry
 * in the Mali UBO is 16-byte too. The number of entries is encoded in a
 * 12-bit field, with a minus(1) modifier, which gives a maximum of 2^12 SSBO
 * descriptors. */
#define MAX_PER_SET_STORAGE_BUFFERS (1 << 12)
/* MALI_ATTRIBUTE::buffer_index is 9-bit, and each image takes two
 * MALI_ATTRIBUTE_BUFFER slots, which gives a maximum of (1 << 8) images. */
#define MAX_PER_SET_STORAGE_IMAGES (1 << 8)

#else

/* Valhall has native support for descriptor sets, and allows a maximum
 * of 16 sets, but we reserve 9 for our internal use, so we have 7 left. */
#define MAX_SETS 7

/* Hardware limit is 2^24 each of buffer, texture, and sampler descriptors. We
 * use the same hardware descriptors for multiple kinds of vulkan descriptors,
 * and may want to reorganize these in the future, so advertise a lower limit
 * of 2^20. */
#define MAX_DESCRIPTORS (1 << 20)
#define MAX_PER_SET_SAMPLERS MAX_DESCRIPTORS
#define MAX_PER_SET_SAMPLED_IMAGES MAX_DESCRIPTORS
#define MAX_PER_SET_UNIFORM_BUFFERS MAX_DESCRIPTORS
#define MAX_PER_SET_STORAGE_BUFFERS MAX_DESCRIPTORS
#define MAX_PER_SET_STORAGE_IMAGES MAX_DESCRIPTORS

#endif

/* A maximum of 8 color render targets, and one depth-stencil render target. */
#define MAX_PER_SET_INPUT_ATTACHMENTS (MAX_RTS + 1)

struct panvk_descriptor_set_binding_layout {
   VkDescriptorType type;
   VkDescriptorBindingFlags flags;
   unsigned desc_count;
   unsigned desc_idx;

   /* if textures are present, maximum number of planes required per texture;
    * 0 otherwise
    */
   unsigned textures_per_desc;

   /* if samplers are present, maximum number of planes required per sampler;
    * 0 otherwise
    */
   unsigned samplers_per_desc;

   struct panvk_sampler **immutable_samplers;
};

struct panvk_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;
   VkDescriptorSetLayoutCreateFlagBits flags;
   unsigned desc_count;
   unsigned dyn_buf_count;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Bindings in this descriptor set */
   struct panvk_descriptor_set_binding_layout *bindings;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_set_layout, vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

static inline const struct panvk_descriptor_set_layout *
to_panvk_descriptor_set_layout(const struct vk_descriptor_set_layout *layout)
{
   return container_of(layout, const struct panvk_descriptor_set_layout, vk);
}

static inline const uint32_t
panvk_get_desc_stride(const struct panvk_descriptor_set_binding_layout *layout)
{
   /* One descriptor for each sampler plane, and one for each texture. */
   return layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
      ? layout->textures_per_desc + layout->samplers_per_desc : 1;
}

static inline const uint32_t
panvk_get_iub_desc_count(uint32_t size)
{
   /* Each inline uniform block contains an internal buffer descriptor, in
    * addition to as many descriptors as needed to contain the requested size
    * in bytes. */
   return DIV_ROUND_UP(size, PANVK_DESCRIPTOR_SIZE) + 1;
}

static inline const uint32_t
panvk_get_iub_size(uint32_t desc_count)
{
   assert(desc_count >= 1);
   return (desc_count - 1) * PANVK_DESCRIPTOR_SIZE;
}

struct panvk_subdesc_info {
   VkDescriptorType type;
   uint8_t plane;
};

#define IMPLICIT_SUBDESC_TYPE (VkDescriptorType)-1
#define NO_SUBDESC (struct panvk_subdesc_info){ \
   .type = IMPLICIT_SUBDESC_TYPE, \
}
#define TEX_SUBDESC(__plane) (struct panvk_subdesc_info){ \
   .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, \
   .plane = __plane, \
}
#define SAMPLER_SUBDESC(__plane) (struct panvk_subdesc_info){ \
   .type = VK_DESCRIPTOR_TYPE_SAMPLER, \
   .plane = __plane, \
}

static inline struct panvk_subdesc_info
get_tex_subdesc_info(VkDescriptorType type, uint8_t plane)
{
   return (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      ? TEX_SUBDESC(plane) : NO_SUBDESC;
}

static inline struct panvk_subdesc_info
get_sampler_subdesc_info(VkDescriptorType type, uint8_t plane)
{
   return (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      ? SAMPLER_SUBDESC(plane) : NO_SUBDESC;
}

static inline uint32_t
get_subdesc_idx(const struct panvk_descriptor_set_binding_layout *layout,
                struct panvk_subdesc_info subdesc)
{
   assert((subdesc.type == IMPLICIT_SUBDESC_TYPE) ||
          (layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
           (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLER ||
            subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)));

   uint32_t subdesc_idx = 0;

   /* In case of combined image-sampler, we put the texture first. */
   if (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLER)
      subdesc_idx += layout->textures_per_desc +
                     MIN2(subdesc.plane, layout->samplers_per_desc - 1);
   else if (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
      subdesc_idx += MIN2(subdesc.plane, layout->textures_per_desc - 1);

   return subdesc_idx;
}

static inline uint32_t
panvk_get_desc_index(const struct panvk_descriptor_set_binding_layout *layout,
                     uint32_t elem, struct panvk_subdesc_info subdesc)
{
   assert(!vk_descriptor_type_is_dynamic(layout->type));

   return layout->desc_idx + elem * panvk_get_desc_stride(layout) +
      get_subdesc_idx(layout, subdesc);
}

#endif /* PANVK_VX_DESCRIPTOR_SET_LAYOUT_H */
