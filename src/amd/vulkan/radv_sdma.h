/*
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_SDMA_H
#define RADV_SDMA_H

#include "ac_cmdbuf_sdma.h"

#include "radv_formats.h"
#include "radv_image.h"

struct radv_cmd_stream;
struct radv_cmd_buffer;

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t
radv_sdma_get_texel_scale(const struct radv_image *image)
{
   if (vk_format_is_96bit(image->vk.format)) {
      return 3;
   } else {
      return 1;
   }
}

ALWAYS_INLINE static VkExtent3D
radv_sdma_get_copy_extent(const struct radv_image *const image, const VkImageSubresourceLayers subresource,
                          VkExtent3D extent)
{
   const uint8_t texel_scale = radv_sdma_get_texel_scale(image);

   extent.width *= texel_scale;

   if (image->vk.image_type != VK_IMAGE_TYPE_3D)
      extent.depth = vk_image_subresource_layer_count(&image->vk, &subresource);

   return extent;
}

struct ac_sdma_surf radv_sdma_get_buf_surf(const struct radv_image *const image,
                                           const VkDeviceMemoryImageCopyKHR *const region);
struct ac_sdma_surf radv_sdma_get_surf(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *const image,
                                       VkImageLayout image_layout, const VkImageSubresourceLayers subresource,
                                       const VkOffset3D offset);
void radv_sdma_copy_buffer_image(const struct radv_device *device, struct radv_cmd_stream *cs,
                                 const struct ac_sdma_surf *buf, const struct ac_sdma_surf *img,
                                 const VkExtent3D extent, bool to_image);
bool radv_sdma_use_unaligned_buffer_image_copy(const struct radv_device *device, const struct ac_sdma_surf *buf,
                                               const struct ac_sdma_surf *img, const VkExtent3D ext);
void radv_sdma_copy_buffer_image_unaligned(const struct radv_device *device, struct radv_cmd_stream *cs,
                                           const struct ac_sdma_surf *buf, const struct ac_sdma_surf *img_in,
                                           const VkExtent3D copy_extent, struct radeon_winsys_bo *temp_bo,
                                           bool to_image);
void radv_sdma_copy_image(const struct radv_device *device, struct radv_cmd_stream *cs, const struct ac_sdma_surf *src,
                          const struct ac_sdma_surf *dst, const VkExtent3D extent);
bool radv_sdma_use_t2t_scanline_copy(const struct radv_device *device, const struct ac_sdma_surf *src,
                                     const struct ac_sdma_surf *dst, const VkExtent3D extent);
void radv_sdma_copy_image_t2t_scanline(const struct radv_device *device, struct radv_cmd_stream *cs,
                                       const struct ac_sdma_surf *src, const struct ac_sdma_surf *dst,
                                       const VkExtent3D extent, struct radeon_winsys_bo *temp_bo);
void radv_sdma_copy_memory(const struct radv_device *device, struct radv_cmd_stream *cs, uint64_t src_va,
                           uint64_t dst_va, uint64_t size, bool tmz);
void radv_sdma_fill_memory(const struct radv_device *device, struct radv_cmd_stream *cs, const uint64_t va,
                           const uint64_t size, const uint32_t value);
bool radv_sdma_supports_image(const struct radv_device *device, const struct radv_image *image, bool to_image);

void radv_sdma_emit_nop(const struct radv_device *device, struct radv_cmd_stream *cs);

#ifdef __cplusplus
}
#endif

#endif /* RADV_SDMA_H */
