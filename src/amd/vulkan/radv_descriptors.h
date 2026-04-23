/*
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DESCRIPTORS_H
#define RADV_DESCRIPTORS_H

#include "radv_buffer.h"
#include "radv_buffer_view.h"
#include "radv_cmd_buffer.h"
#include "radv_constants.h"
#include "radv_image_view.h"
#include "radv_sampler.h"

#include <vulkan/vulkan.h>

uint32_t radv_descriptor_alignment(VkDescriptorType type);

bool radv_mutable_descriptor_type_size_alignment(const struct radv_device *device,
                                                 const VkMutableDescriptorTypeListEXT *list, uint64_t *out_size,
                                                 uint64_t *out_align);

static ALWAYS_INLINE void
radv_write_texel_buffer_descriptor(unsigned *dst, const VkBufferView _buffer_view)
{
   VK_FROM_HANDLE(radv_buffer_view, buffer_view, _buffer_view);

   if (!buffer_view) {
      memset(dst, 0, RADV_BUFFER_DESC_SIZE);
      return;
   }

   memcpy(dst, buffer_view->state, RADV_BUFFER_DESC_SIZE);
}

static ALWAYS_INLINE void
radv_write_buffer_descriptor(struct radv_device *device, unsigned *dst, uint64_t va, uint64_t range)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (!va) {
      memset(dst, 0, RADV_BUFFER_DESC_SIZE);
      return;
   }

   /* robustBufferAccess is relaxed enough to allow this (in combination with the alignment/size
    * we return from vkGetBufferMemoryRequirements) and this allows the shader compiler to create
    * more efficient 8/16-bit buffer accesses.
    */
   ac_build_raw_buffer_descriptor(pdev->info.gfx_level, va, align(range, 4), dst);
}

static ALWAYS_INLINE void
radv_write_buffer_descriptor_impl(struct radv_device *device, unsigned *dst, const VkDescriptorBufferInfo *buffer_info)
{
   VK_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);
   uint64_t va = 0, range = 0;

   if (buffer) {
      va = vk_buffer_address(&buffer->vk, buffer_info->offset);

      range = vk_buffer_range(&buffer->vk, buffer_info->offset, buffer_info->range);
      assert(buffer->vk.size > 0 && range > 0);
   }

   radv_write_buffer_descriptor(device, dst, va, range);
}

static ALWAYS_INLINE void
radv_write_block_descriptor(void *dst, const VkWriteDescriptorSet *writeset)
{
   const VkWriteDescriptorSetInlineUniformBlock *inline_ub =
      vk_find_struct_const(writeset->pNext, WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);

   memcpy(dst, inline_ub->pData, inline_ub->dataSize);
}

static ALWAYS_INLINE void
radv_write_dynamic_buffer_descriptor(struct radv_descriptor_range *range, const VkDescriptorBufferInfo *buffer_info)
{
   VK_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);
   unsigned size;

   if (!buffer) {
      range->va = 0;
      return;
   }

   size = vk_buffer_range(&buffer->vk, buffer_info->offset, buffer_info->range);
   assert(buffer->vk.size > 0 && size > 0);

   /* robustBufferAccess is relaxed enough to allow this (in combination
    * with the alignment/size we return from vkGetBufferMemoryRequirements)
    * and this allows the shader compiler to create more efficient 8/16-bit
    * buffer accesses. */
   size = align(size, 4);

   range->va = vk_buffer_address(&buffer->vk, buffer_info->offset);
   range->size = size;
}

static ALWAYS_INLINE void
radv_write_image_descriptor(unsigned *dst, unsigned size, VkDescriptorType descriptor_type,
                            const VkDescriptorImageInfo *image_info)
{
   struct radv_image_view *iview = NULL;
   union radv_descriptor *descriptor;

   if (image_info)
      iview = radv_image_view_from_handle(image_info->imageView);

   if (!iview) {
      memset(dst, 0, size);
      return;
   }

   if (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
      descriptor = &iview->storage_descriptor;
   } else {
      descriptor = &iview->descriptor;
   }
   assert(size > 0);

   /* Encourage compilers to inline memcpy for combined image/sampler descriptors. */
   switch (size) {
   case 32:
      memcpy(dst, descriptor, 32);
      break;
   case 64:
      memcpy(dst, descriptor, 64);
      break;
   default:
      UNREACHABLE("Invalid size");
   }
}

static ALWAYS_INLINE void
radv_write_image_descriptor_ycbcr(struct radv_device *device, unsigned *dst, const VkDescriptorImageInfo *image_info,
                                  bool use_combined_image_sampler)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_image_view *iview = NULL;

   if (image_info)
      iview = radv_image_view_from_handle(image_info->imageView);

   if (!iview) {
      memset(dst, 0, 32);
      return;
   }

   const uint32_t plane_count = vk_format_get_plane_count(iview->vk.format);
   const uint32_t desc_size = use_combined_image_sampler ? radv_get_combined_image_sampler_desc_size(pdev)
                                                         : radv_get_sampled_image_desc_size(pdev);
   const uint32_t stride = desc_size / 4;

   for (uint32_t i = 0; i < plane_count; i++) {
      memcpy(dst, iview->descriptor.plane_descriptors[i], 32);

      dst += stride;
   }
}

static ALWAYS_INLINE void
radv_write_sampler_descriptor(unsigned *dst, VkSampler _sampler)
{
   VK_FROM_HANDLE(radv_sampler, sampler, _sampler);
   memcpy(dst, sampler->state, RADV_SAMPLER_DESC_SIZE);
}

static ALWAYS_INLINE void
radv_write_combined_image_sampler_descriptor(struct radv_device *device, unsigned *dst,
                                             VkDescriptorType descriptor_type, const VkDescriptorImageInfo *image_info,
                                             bool has_sampler)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const uint32_t desc_size = radv_get_sampled_image_desc_size(pdev);

   radv_write_image_descriptor(dst, desc_size, descriptor_type, image_info);
   /* copy over sampler state */
   if (has_sampler) {
      const uint32_t sampler_offset = radv_get_combined_image_sampler_offset(pdev);

      radv_write_sampler_descriptor(dst + sampler_offset / sizeof(*dst), image_info->sampler);
   }
}

static ALWAYS_INLINE void
radv_write_accel_struct_descriptor(struct radv_device *device, void *ptr, VkDeviceAddress va)
{
   uint64_t desc[2] = {va, 0};

   assert(sizeof(desc) == RADV_ACCEL_STRUCT_DESC_SIZE);
   memcpy(ptr, desc, RADV_ACCEL_STRUCT_DESC_SIZE);
}

#endif /* RADV_DESCRIPTORS_H */
