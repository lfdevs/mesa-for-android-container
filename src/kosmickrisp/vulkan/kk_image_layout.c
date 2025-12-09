/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_image_layout.h"

#include "kk_device.h"
#include "kk_format.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/mtl_format.h"

#include "util/format/u_format.h"

static enum mtl_texture_type
vk_image_create_info_to_mtl_texture_type(
   const struct VkImageCreateInfo *create_info)
{
   uint32_t array_layers = create_info->arrayLayers;
   uint32_t samples = create_info->samples;
   switch (create_info->imageType) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      /* We require input attachments to be arrays */
      if (array_layers > 1 ||
          (create_info->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
         return samples > 1u ? MTL_TEXTURE_TYPE_2D_ARRAY_MULTISAMPLE
                             : MTL_TEXTURE_TYPE_2D_ARRAY;
      return samples > 1u ? MTL_TEXTURE_TYPE_2D_MULTISAMPLE
                          : MTL_TEXTURE_TYPE_2D;
   case VK_IMAGE_TYPE_3D:
      return MTL_TEXTURE_TYPE_3D;
   default:
      UNREACHABLE("Invalid image type");
      return MTL_TEXTURE_TYPE_1D; /* Just return a type we don't actually use */
   }
}

static enum mtl_texture_usage
vk_image_usage_flags_to_mtl_texture_usage(VkImageUsageFlags usage_flags,
                                          VkImageCreateFlags create_flags,
                                          bool supports_atomics)
{
   enum mtl_texture_usage usage = 0u;

   const VkImageUsageFlags shader_write =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
   if (usage_flags & shader_write)
      usage |= MTL_TEXTURE_USAGE_SHADER_WRITE;

   const VkImageUsageFlags shader_read = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
   if (usage_flags & shader_read)
      usage |= MTL_TEXTURE_USAGE_SHADER_READ;

   const VkImageUsageFlags render_attachment =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT;

   if (usage_flags & render_attachment)
      usage |= MTL_TEXTURE_USAGE_RENDER_TARGET;

   if (create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
      usage |= MTL_TEXTURE_USAGE_PIXEL_FORMAT_VIEW;

   if (supports_atomics) {
      usage |= MTL_TEXTURE_USAGE_SHADER_READ;
      usage |= MTL_TEXTURE_USAGE_SHADER_WRITE;
      usage |= MTL_TEXTURE_USAGE_SHADER_ATOMIC;
   }

   return usage;
}

void
kk_image_layout_init(const struct kk_device *dev,
                     const struct VkImageCreateInfo *create_info,
                     enum pipe_format format, const uint8_t width_scale,
                     const uint8_t height_scale, struct kk_image_layout *layout)
{
   const struct kk_va_format *supported_format = kk_get_va_format(format);
   layout->type = vk_image_create_info_to_mtl_texture_type(create_info);
   layout->width_px = create_info->extent.width / width_scale;
   layout->height_px = create_info->extent.height / height_scale;
   layout->depth_px = create_info->extent.depth;
   layout->layers = create_info->arrayLayers;
   layout->levels = create_info->mipLevels;
   layout->optimized_layout = create_info->tiling == VK_IMAGE_TILING_OPTIMAL;
   layout->usage = vk_image_usage_flags_to_mtl_texture_usage(
      create_info->usage, create_info->flags, supported_format->atomic);
   layout->format.pipe = format;
   layout->format.mtl = supported_format->mtl_pixel_format;
   layout->swizzle.red = supported_format->unswizzle.red;
   layout->swizzle.green = supported_format->unswizzle.green;
   layout->swizzle.blue = supported_format->unswizzle.blue;
   layout->swizzle.alpha = supported_format->unswizzle.alpha;
   layout->sample_count_sa = create_info->samples;
   mtl_heap_texture_size_and_align_with_descriptor(dev->mtl_handle, layout);

   /*
    * Metal requires adding MTL_TEXTURE_USAGE_PIXEL_FORMAT_VIEW if we are going
    * to reinterpret the format with a different format. This seems to be the
    * only format with this issue.
    */
   if (format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
      layout->usage |= MTL_TEXTURE_USAGE_PIXEL_FORMAT_VIEW;
   }

   // TODO_KOSMICKRISP Fill remaining offsets and strides whenever possible
   if (create_info->tiling == VK_IMAGE_TILING_LINEAR) {
      const struct util_format_description *format_desc =
         util_format_description(layout->format.pipe);
      size_t bytes_per_texel = format_desc->block.bits / 8;
      layout->linear_stride_B =
         align(bytes_per_texel * layout->width_px, layout->align_B);
      layout->layer_stride_B = layout->linear_stride_B * layout->height_px;
      /* Metal only allows for 2D texture with no mipmapping. */
      layout->size_B = layout->layer_stride_B;
   }
}
