/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_cmd_buffer.h"

#include "kk_bo.h"
#include "kk_buffer.h"
#include "kk_device.h"
#include "kk_encoder.h"
#include "kk_entrypoints.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"

#include "util/format/u_format.h"

VKAPI_ATTR void VKAPI_CALL
kk_CmdCopyBuffer2(VkCommandBuffer commandBuffer,
                  const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, src, pCopyBufferInfo->srcBuffer);
   VK_FROM_HANDLE(kk_buffer, dst, pCopyBufferInfo->dstBuffer);

   mtl_blit_encoder *blit = kk_blit_encoder(cmd);
   for (uint32_t i = 0; i < pCopyBufferInfo->regionCount; i++) {
      const VkBufferCopy2 *region = &pCopyBufferInfo->pRegions[i];
      mtl_copy_from_buffer_to_buffer(blit, src->mtl_handle, region->srcOffset,
                                     dst->mtl_handle, region->dstOffset,
                                     region->size);
   }
}

struct kk_buffer_image_copy_info {
   struct mtl_buffer_image_copy mtl_data;
   size_t buffer_slice_size_B;
};

static struct kk_buffer_image_copy_info
vk_buffer_image_copy_to_mtl_buffer_image_copy(
   const VkBufferImageCopy2 *region, const struct kk_image_plane *plane)
{
   struct kk_buffer_image_copy_info copy;
   enum pipe_format p_format = plane->layout.format.pipe;
   if (region->imageSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
      copy.mtl_data.options = MTL_BLIT_OPTION_DEPTH_FROM_DEPTH_STENCIL;
      p_format = util_format_get_depth_only(p_format);
   } else if (region->imageSubresource.aspectMask ==
              VK_IMAGE_ASPECT_STENCIL_BIT) {
      copy.mtl_data.options = MTL_BLIT_OPTION_STENCIL_FROM_DEPTH_STENCIL;
      p_format = PIPE_FORMAT_S8_UINT;
   } else
      copy.mtl_data.options = MTL_BLIT_OPTION_NONE;

   const uint32_t buffer_width = region->bufferRowLength
                                    ? region->bufferRowLength
                                    : region->imageExtent.width;
   const uint32_t buffer_height = region->bufferImageHeight
                                     ? region->bufferImageHeight
                                     : region->imageExtent.height;

   const uint32_t buffer_stride_B =
      util_format_get_stride(p_format, buffer_width);
   const uint32_t buffer_size_2d_B =
      util_format_get_2d_size(p_format, buffer_stride_B, buffer_height);

   /* Metal requires this value to be 0 for 2D images, otherwise the number of
    * bytes between each 2D image of a 3D texture */
   copy.mtl_data.buffer_2d_image_size_B =
      plane->layout.depth_px == 1u ? 0u : buffer_size_2d_B;
   copy.mtl_data.buffer_stride_B = buffer_stride_B;
   copy.mtl_data.image_size = vk_extent_3d_to_mtl_size(&region->imageExtent);
   copy.mtl_data.image_origin =
      vk_offset_3d_to_mtl_origin(&region->imageOffset);
   copy.mtl_data.image_level = region->imageSubresource.mipLevel;
   copy.buffer_slice_size_B = buffer_size_2d_B;

   return copy;
}

#define kk_foreach_slice(ndx, image, subresource_member)                       \
   for (uint32_t ndx = region->subresource_member.baseArrayLayer;              \
        ndx < (region->subresource_member.baseArrayLayer +                     \
               vk_image_subresource_layer_count(&image->vk,                    \
                                                &region->subresource_member)); \
        ++ndx)

VKAPI_ATTR void VKAPI_CALL
kk_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                         const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, pCopyBufferToImageInfo->srcBuffer);
   VK_FROM_HANDLE(kk_image, image, pCopyBufferToImageInfo->dstImage);

   mtl_blit_encoder *blit = kk_blit_encoder(cmd);
   for (int r = 0; r < pCopyBufferToImageInfo->regionCount; r++) {
      const VkBufferImageCopy2 *region = &pCopyBufferToImageInfo->pRegions[r];
      const uint8_t plane_index = kk_image_memory_aspects_to_plane(
         image, region->imageSubresource.aspectMask);
      struct kk_image_plane *plane = &image->planes[plane_index];
      struct kk_buffer_image_copy_info info =
         vk_buffer_image_copy_to_mtl_buffer_image_copy(region, plane);
      info.mtl_data.buffer = buffer->mtl_handle;
      info.mtl_data.image = plane->mtl_handle;
      size_t buffer_offset = region->bufferOffset;

      kk_foreach_slice(slice, image, imageSubresource)
      {
         info.mtl_data.image_slice = slice;
         info.mtl_data.buffer_offset_B = buffer_offset;
         mtl_copy_from_buffer_to_texture(blit, &info.mtl_data);
         buffer_offset += info.buffer_slice_size_B;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                         const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_image, image, pCopyImageToBufferInfo->srcImage);
   VK_FROM_HANDLE(kk_buffer, buffer, pCopyImageToBufferInfo->dstBuffer);

   mtl_blit_encoder *blit = kk_blit_encoder(cmd);
   for (unsigned r = 0; r < pCopyImageToBufferInfo->regionCount; r++) {
      const VkBufferImageCopy2 *region = &pCopyImageToBufferInfo->pRegions[r];
      const uint8_t plane_index = kk_image_memory_aspects_to_plane(
         image, region->imageSubresource.aspectMask);
      struct kk_image_plane *plane = &image->planes[plane_index];
      struct kk_buffer_image_copy_info info =
         vk_buffer_image_copy_to_mtl_buffer_image_copy(region, plane);
      info.mtl_data.buffer = buffer->mtl_handle;
      info.mtl_data.image = plane->mtl_handle;
      size_t buffer_offset = region->bufferOffset;

      kk_foreach_slice(slice, image, imageSubresource)
      {
         info.mtl_data.image_slice = slice;
         info.mtl_data.buffer_offset_B = buffer_offset;
         mtl_copy_from_texture_to_buffer(blit, &info.mtl_data);
         buffer_offset += info.buffer_slice_size_B;
      }
   }
}

/* Copies images by doing a texture->buffer->texture transfer. Returns the new
 * buffer offset for subsequent copies. */
static size_t
copy_through_buffer(struct kk_cmd_buffer *cmd, struct kk_image *src,
                    uint32_t src_index, struct kk_image *dst,
                    uint32_t dst_index, mtl_buffer *buffer,
                    size_t buffer_offset, const VkImageCopy2 *region)
{
   struct kk_image_plane *src_plane = &src->planes[src_index];
   struct kk_image_plane *dst_plane = &dst->planes[dst_index];
   enum pipe_format src_format = src_plane->layout.format.pipe;
   enum pipe_format dst_format = dst_plane->layout.format.pipe;
   bool is_src_compressed = util_format_is_compressed(src_format);
   bool is_dst_compressed = util_format_is_compressed(dst_format);
   /* We shouldn't do any depth/stencil through this path */
   assert(!util_format_is_depth_or_stencil(src_format) ||
          !util_format_is_depth_or_stencil(dst_format));
   mtl_blit_encoder *blit = kk_blit_encoder(cmd);

   uint32_t mip_level = region->srcSubresource.mipLevel;
   const uint32_t mip_width = u_minify(src_plane->layout.width_px, mip_level);
   const uint32_t mip_height = u_minify(src_plane->layout.height_px, mip_level);
   const uint32_t stride_B = util_format_get_stride(src_format, mip_width);
   const uint32_t size_2d_B =
      util_format_get_2d_size(src_format, stride_B, mip_height);
   const uint32_t buffer_stride_B =
      util_format_get_stride(src_format, region->extent.width);
   const uint32_t buffer_size_2d_B = util_format_get_2d_size(
      src_format, buffer_stride_B, region->extent.height);

   struct kk_buffer_image_copy_info info;

   /* Metal requires this value to be 0 for 2D images, otherwise the number
    * of bytes between each 2D image of a 3D texture */
   info.mtl_data.buffer_2d_image_size_B =
      src_plane->layout.depth_px == 1u ? 0u : size_2d_B;
   info.mtl_data.buffer_stride_B = buffer_stride_B;
   info.mtl_data.image_level = mip_level;
   info.mtl_data.buffer = buffer;
   info.mtl_data.options = MTL_BLIT_OPTION_NONE;
   info.buffer_slice_size_B = buffer_size_2d_B;
   struct mtl_size src_size = vk_extent_3d_to_mtl_size(&region->extent);
   struct mtl_size dst_size = vk_extent_3d_to_mtl_size(&region->extent);
   /* Need to adjust size to block dimensions */
   if (is_src_compressed) {
      dst_size.x /= util_format_get_blockwidth(src_format);
      dst_size.y /= util_format_get_blockheight(src_format);
      dst_size.z /= util_format_get_blockdepth(src_format);
   }
   if (is_dst_compressed) {
      dst_size.x *= util_format_get_blockwidth(dst_format);
      dst_size.y *= util_format_get_blockheight(dst_format);
      dst_size.z *= util_format_get_blockdepth(dst_format);
   }
   struct mtl_origin src_origin =
      vk_offset_3d_to_mtl_origin(&region->srcOffset);
   struct mtl_origin dst_origin =
      vk_offset_3d_to_mtl_origin(&region->dstOffset);

   /* Texture->Buffer->Texture */
   // TODO_KOSMICKRISP We don't handle 3D to 2D array nor vice-versa in this
   // path. Unsure if it's even needed, can compressed textures be 3D?
   kk_foreach_slice(slice, src, srcSubresource)
   {
      info.mtl_data.image = src_plane->mtl_handle;
      info.mtl_data.image_size = src_size;
      info.mtl_data.image_origin = src_origin;
      info.mtl_data.image_slice = slice;
      info.mtl_data.buffer_offset_B = buffer_offset;
      mtl_copy_from_texture_to_buffer(blit, &info.mtl_data);

      info.mtl_data.image = dst_plane->mtl_handle;
      info.mtl_data.image_size = dst_size;
      info.mtl_data.image_origin = dst_origin;
      mtl_copy_from_buffer_to_texture(blit, &info.mtl_data);

      buffer_offset += info.buffer_slice_size_B;
   }

   return buffer_offset;
}

static bool
can_do_image_to_image_copy(struct kk_image *src, uint32_t src_index,
                           struct kk_image *dst, uint32_t dst_index)
{
   struct kk_image_plane *src_plane = &src->planes[src_index];
   struct kk_image_plane *dst_plane = &dst->planes[dst_index];
   enum pipe_format src_format = src_plane->layout.format.pipe;
   enum pipe_format dst_format = dst_plane->layout.format.pipe;

   return src_format == dst_format && src_plane->layout.sample_count_sa ==
                                         dst_plane->layout.sample_count_sa;
}

/* Copies images through Metal's texture->texture copy mechanism */
static void
copy_image(struct kk_cmd_buffer *cmd, struct kk_image *src, uint32_t src_index,
           struct kk_image *dst, uint32_t dst_index, const VkImageCopy2 *region)
{
   mtl_blit_encoder *blit = kk_blit_encoder(cmd);
   struct kk_image_plane *src_plane = &src->planes[src_index];
   struct kk_image_plane *dst_plane = &dst->planes[dst_index];

   /* From the Vulkan 1.3.217 spec:
    *
    *    "When copying between compressed and uncompressed formats the
    *    extent members represent the texel dimensions of the source image
    *    and not the destination."
    */
   const VkExtent3D extent_px =
      vk_image_sanitize_extent(&src->vk, region->extent);

   size_t src_slice = region->srcSubresource.baseArrayLayer;
   size_t src_level = region->srcSubresource.mipLevel;
   struct mtl_origin src_origin =
      vk_offset_3d_to_mtl_origin(&region->srcOffset);
   struct mtl_size size = {.x = extent_px.width,
                           .y = extent_px.height,
                           .z = extent_px.depth};
   size_t dst_slice = region->dstSubresource.baseArrayLayer;
   size_t dst_level = region->dstSubresource.mipLevel;
   struct mtl_origin dst_origin =
      vk_offset_3d_to_mtl_origin(&region->dstOffset);

   /* When copying 3D to 2D layered or vice-versa, we need to change the 3D
    * size to 2D and iterate on the layer count of the 2D image (which is the
    * same as the depth of the 3D) and adjust origin and slice accordingly */
   uint32_t layer_count =
      vk_image_subresource_layer_count(&src->vk, &region->srcSubresource);
   const uint32_t dst_layer_count =
      vk_image_subresource_layer_count(&dst->vk, &region->dstSubresource);
   size_t *src_increase = &src_slice;
   size_t *dst_increase = &dst_slice;

   if (layer_count < dst_layer_count) { /* 3D to 2D layered */
      layer_count = dst_layer_count;
      src_increase = &src_origin.z;
      size.z = 1u;
   } else if (dst_layer_count < layer_count) { /* 2D layered to 3D */
      dst_increase = &dst_origin.z;
      size.z = 1u;
   }
   for (uint32_t l = 0; l < layer_count;
        ++l, ++(*src_increase), ++(*dst_increase)) {
      mtl_copy_from_texture_to_texture(
         blit, src_plane->mtl_handle, src_slice, src_level, src_origin, size,
         dst_plane->mtl_handle, dst_slice, dst_level, dst_origin);
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdCopyImage2(VkCommandBuffer commandBuffer,
                 const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_image, src, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(kk_image, dst, pCopyImageInfo->dstImage);

   size_t buffer_size = 0u;

   /* Copy as much as we can through Metal's image to image copy that only
    * supports same format and sample count while getting the required buffer
    * size for image->buffer->image copy. */
   for (uint32_t i = 0u; i < pCopyImageInfo->regionCount; ++i) {
      const VkImageCopy2 *region = &pCopyImageInfo->pRegions[i];
      uint8_t src_index =
         kk_image_aspects_to_plane(src, region->srcSubresource.aspectMask);
      uint8_t dst_index =
         kk_image_aspects_to_plane(dst, region->dstSubresource.aspectMask);

      if (can_do_image_to_image_copy(src, src_index, dst, dst_index))
         copy_image(cmd, src, src_index, dst, dst_index, region);
      else {
         struct kk_image_plane *src_plane = &src->planes[src_index];
         enum pipe_format src_format = src_plane->layout.format.pipe;
         const uint32_t buffer_stride_B =
            util_format_get_stride(src_format, region->extent.width);
         const uint32_t buffer_size_2d_B = util_format_get_2d_size(
            src_format, buffer_stride_B, region->extent.height);
         const uint32_t layer_count =
            vk_image_subresource_layer_count(&src->vk, &region->srcSubresource);
         buffer_size += buffer_size_2d_B * layer_count;
      }
   }

   /* Copy source image to buffer then to the destination image for those
    * regions that image to image was not possible. */
   if (buffer_size) {
      struct kk_bo *bo = kk_cmd_allocate_buffer(cmd, buffer_size, 8);
      size_t buffer_offset = 0u;
      for (uint32_t i = 0u; i < pCopyImageInfo->regionCount; ++i) {
         const VkImageCopy2 *region = &pCopyImageInfo->pRegions[i];
         uint8_t src_index =
            kk_image_aspects_to_plane(src, region->srcSubresource.aspectMask);
         uint8_t dst_index =
            kk_image_aspects_to_plane(dst, region->dstSubresource.aspectMask);
         if (!can_do_image_to_image_copy(src, src_index, dst, dst_index))
            buffer_offset =
               copy_through_buffer(cmd, src, src_index, dst, dst_index, bo->map,
                                   buffer_offset, region);
      }
   }
}
