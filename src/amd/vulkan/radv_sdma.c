/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2015-2021 Advanced Micro Devices, Inc.
 * Copyright 2023 Valve Corporation
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_sdma.h"
#include "meta/radv_meta.h"
#include "util/macros.h"
#include "radv_cs.h"
#include "radv_formats.h"

#include "ac_cmdbuf_sdma.h"

struct radv_sdma_chunked_copy_info {
   unsigned extent_horizontal_blocks;
   unsigned extent_vertical_blocks;
   unsigned aligned_row_pitch;
   unsigned num_rows_per_copy;
};

static const VkExtent3D radv_sdma_t2t_alignment_2d_and_planar[] = {
   {16, 16, 1}, /* 1 bpp */
   {16, 8, 1},  /* 2 bpp */
   {8, 8, 1},   /* 4 bpp */
   {8, 4, 1},   /* 8 bpp */
   {4, 4, 1},   /* 16 bpp */
};

static const VkExtent3D radv_sdma_t2t_alignment_3d[] = {
   {8, 4, 8}, /* 1 bpp */
   {4, 4, 8}, /* 2 bpp */
   {4, 4, 4}, /* 4 bpp */
   {4, 2, 4}, /* 8 bpp */
   {2, 2, 4}, /* 16 bpp */
};

ALWAYS_INLINE static unsigned
radv_sdma_pitch_alignment(const struct radv_device *device, const unsigned bpp)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->info.sdma_ip_version >= SDMA_5_0)
      return MAX2(1, 4 / bpp);

   return 4;
}

ALWAYS_INLINE static uint32_t
radv_sdma_surf_type_from_aspect_mask(const VkImageAspectFlags aspectMask)
{
   if (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
      return 1;
   else if (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
      return 2;

   return 0;
}

static struct radv_sdma_chunked_copy_info
radv_sdma_get_chunked_copy_info(const struct radv_device *const device, const struct ac_sdma_surf *const img,
                                const VkExtent3D extent)
{
   const uint32_t blk_h = util_format_get_blockheight(img->img_format);
   const unsigned extent_horizontal_blocks = extent.width;
   const unsigned extent_vertical_blocks = extent.height;
   const unsigned aligned_row_pitch = align(extent_horizontal_blocks, 4);
   const unsigned aligned_row_bytes = aligned_row_pitch * img->bpp;

   /* Assume that we can always copy at least one full row at a time. */
   const unsigned max_num_rows_per_copy =
      MIN2(RADV_SDMA_TRANSFER_TEMP_BYTES / aligned_row_bytes, extent.height * blk_h);
   assert(max_num_rows_per_copy);

   /* Ensure that the number of rows copied at a time is a power of two. */
   const unsigned num_rows_per_copy = MAX2(1, util_next_power_of_two(max_num_rows_per_copy + 1) / 2);

   const struct radv_sdma_chunked_copy_info r = {
      .extent_horizontal_blocks = extent_horizontal_blocks,
      .extent_vertical_blocks = extent_vertical_blocks,
      .aligned_row_pitch = aligned_row_pitch,
      .num_rows_per_copy = num_rows_per_copy,
   };

   return r;
}

static uint32_t
radv_sdma_get_bpe(const struct radv_image *const image, const VkImageSubresourceLayers *subresource)
{
   VkFormat format = vk_format_get_aspect_format(image->vk.format, subresource->aspectMask);

   if (vk_format_is_96bit(format))
      format = radv_meta_get_96bit_channel_format(format);

   return vk_format_get_blocksize(format);
}

struct ac_sdma_surf
radv_sdma_get_buf_surf(const struct radv_image *const image, const VkDeviceMemoryImageCopyKHR *const region)
{
   const struct vk_image_buffer_layout layout = vk_image_memory_copy_layout(&image->vk, region);

   assert(util_bitcount(region->imageSubresource.aspectMask) == 1);

   const uint32_t texel_scale = radv_sdma_get_texel_scale(image);
   const unsigned pitch = (layout.row_stride_B / layout.element_size_B) * texel_scale;
   const unsigned slice_pitch = layout.image_stride_B / layout.element_size_B;

   const uint32_t bpe = radv_sdma_get_bpe(image, &region->imageSubresource);

   const struct ac_sdma_surf info = {
      .va = region->addressRange.address,
      .pitch = pitch,
      .slice_pitch = slice_pitch,
      .bpp = bpe,
      .is_secure = image->vk.create_flags & VK_IMAGE_CREATE_2_PROTECTED_BIT_KHR,
   };

   return info;
}

struct ac_sdma_surf
radv_sdma_get_surf(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *const image, VkImageLayout image_layout,
                   const VkImageSubresourceLayers subresource, const VkOffset3D offset)
{
   assert(util_bitcount(subresource.aspectMask) == 1);

   const struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const unsigned plane_idx = radv_plane_from_aspect(subresource.aspectMask);
   const unsigned binding_idx = image->disjoint ? plane_idx : 0;
   const struct radeon_surf *const surf = &image->planes[plane_idx].surface;
   const struct radv_image_binding *binding = &image->bindings[binding_idx];
   const uint64_t va = binding->addr;
   const uint32_t bpe = radv_sdma_get_bpe(image, &subresource);
   const uint32_t blk_w = vk_format_get_blockwidth(image->vk.format);

   struct ac_sdma_surf info = {
      .surf = surf,
      .img_format = radv_format_to_pipe_format(image->vk.format),
      .format = radv_format_to_pipe_format(vk_format_get_aspect_format(image->vk.format, subresource.aspectMask)),
      .extent =
         {
            .width = vk_format_get_plane_width(image->vk.format, plane_idx, image->vk.extent.width),
            .height = vk_format_get_plane_height(image->vk.format, plane_idx, image->vk.extent.height),
         },
      .bpp = bpe,
      .first_level = subresource.mipLevel,
      .num_levels = image->vk.mip_levels,
      .is_stencil = subresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT,
      .is_secure = image->vk.create_flags & VK_IMAGE_CREATE_2_PROTECTED_BIT_KHR,
   };

   info.offset.x = offset.x * radv_sdma_get_texel_scale(image);
   info.offset.y = offset.y;
   info.offset.z = image->vk.image_type == VK_IMAGE_TYPE_3D ? offset.z : subresource.baseArrayLayer;

   const VkExtent3D img_extent = {
      .width = vk_format_get_plane_width(image->vk.format, plane_idx, image->vk.extent.width),
      .height = vk_format_get_plane_height(image->vk.format, plane_idx, image->vk.extent.height),
   };

   const VkExtent3D img_extent_el = vk_image_extent_to_elements(&image->vk, img_extent);

   info.extent.width = img_extent_el.width;
   info.extent.height = img_extent_el.height;
   info.extent.depth = image->vk.image_type == VK_IMAGE_TYPE_3D ? image->vk.extent.depth : image->vk.array_layers;

   const uint64_t surf_offset = (subresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) ? surf->u.gfx9.zs.stencil_offset
                                                                                        : surf->u.gfx9.surf_offset;

   if (surf->is_linear) {
      info.va = va + surf_offset + surf->u.gfx9.offset[subresource.mipLevel];
      info.pitch = surf->u.gfx9.pitch[subresource.mipLevel] / blk_w;
      info.slice_pitch = surf->u.gfx9.surf_slice_size / bpe;
   } else {
      const uint32_t queue_mask = radv_image_queue_family_mask(image, cmd_buffer->qf, cmd_buffer->qf);
      const bool htile_compressed =
         radv_layout_is_htile_compressed(device, image, subresource.mipLevel, image_layout, queue_mask);
      const bool dcc_compressed =
         radv_layout_dcc_compressed(device, image, subresource.mipLevel, image_layout, queue_mask);

      /* 1D resources should be linear. */
      assert(surf->u.gfx9.resource_type != RADEON_RESOURCE_1D);

      info.va = (va + surf_offset) | surf->tile_swizzle << 8;

      if (pdev->info.sdma_supports_compression && (dcc_compressed || htile_compressed)) {
         assert(pdev->info.gfx_level < GFX12);
         info.is_compressed = true;
      }

      if (info.is_compressed) {
         info.meta_va = va + surf->meta_offset;
         info.surf_type = radv_sdma_surf_type_from_aspect_mask(subresource.aspectMask);
         info.htile_enabled = htile_compressed;
      }
   }

   if (pdev->info.gfx_level >= GFX12)
      info.is_compressed = binding->bo && binding->bo->gfx12_allow_dcc;

   return info;
}

void
radv_sdma_emit_nop(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   radeon_check_space(device->ws, cs->b, 1);
   ac_emit_sdma_nop(cs->b);
}

void
radv_sdma_copy_memory(const struct radv_device *device, struct radv_cmd_stream *cs, uint64_t src_va, uint64_t dst_va,
                      uint64_t size, bool tmz)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   while (size > 0) {
      radeon_check_space(device->ws, cs->b, 7);
      uint64_t bytes_written = ac_emit_sdma_copy_linear(cs->b, pdev->info.sdma_ip_version, src_va, dst_va, size, tmz);

      size -= bytes_written;
      src_va += bytes_written;
      dst_va += bytes_written;
   }
}

void
radv_sdma_fill_memory(const struct radv_device *device, struct radv_cmd_stream *cs, uint64_t va, uint64_t size,
                      const uint32_t value)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   while (size > 0) {
      radeon_check_space(device->ws, cs->b, 5);
      uint64_t bytes_written = ac_emit_sdma_constant_fill(cs->b, pdev->info.sdma_ip_version, va, size, value);

      size -= bytes_written;
      va += bytes_written;
   }
}

static void
radv_sdma_emit_copy_linear_sub_window(const struct radv_device *device, struct radv_cmd_stream *cs,
                                      const struct ac_sdma_surf *const src, const struct ac_sdma_surf *const dst,
                                      const VkExtent3D extent)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   radeon_check_space(device->ws, cs->b, 13);
   ac_emit_sdma_copy_linear_sub_window(cs->b, pdev->info.sdma_ip_version, src, dst, extent.width, extent.height,
                                       extent.depth);
}

static void
radv_sdma_emit_copy_tiled_sub_window(const struct radv_device *device, struct radv_cmd_stream *cs,
                                     const struct ac_sdma_surf *const tiled, const struct ac_sdma_surf *const linear,
                                     const VkExtent3D extent, const bool detile)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   bool tmz = tiled->is_secure;
   assert(!tmz || linear->is_secure);

   radeon_check_space(device->ws, cs->b, 17);
   ac_emit_sdma_copy_tiled_sub_window(cs->b, &pdev->info, linear, tiled, detile, extent.width, extent.height,
                                      extent.depth, tmz);
}

static void
radv_sdma_emit_copy_t2t_sub_window(const struct radv_device *device, struct radv_cmd_stream *cs,
                                   const struct ac_sdma_surf *const src, const struct ac_sdma_surf *const dst,
                                   const VkExtent3D extent)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   radeon_check_space(device->ws, cs->b, 18);
   ac_emit_sdma_copy_t2t_sub_window(cs->b, &pdev->info, src, dst, extent.width, extent.height, extent.depth);
}

void
radv_sdma_copy_buffer_image(const struct radv_device *device, struct radv_cmd_stream *cs,
                            const struct ac_sdma_surf *buf, const struct ac_sdma_surf *img, const VkExtent3D extent,
                            bool to_image)
{
   if (img->surf->is_linear) {
      if (to_image)
         radv_sdma_emit_copy_linear_sub_window(device, cs, buf, img, extent);
      else
         radv_sdma_emit_copy_linear_sub_window(device, cs, img, buf, extent);
   } else {
      radv_sdma_emit_copy_tiled_sub_window(device, cs, img, buf, extent, !to_image);
   }
}

bool
radv_sdma_use_unaligned_buffer_image_copy(const struct radv_device *device, const struct ac_sdma_surf *buf,
                                          const struct ac_sdma_surf *img, const VkExtent3D ext)
{
   if (!util_is_aligned(buf->pitch, radv_sdma_pitch_alignment(device, img->bpp)))
      return true;

   const bool uses_depth = img->offset.z != 0 || ext.depth != 1;
   if (!img->surf->is_linear && uses_depth) {
      if (!util_is_aligned(buf->slice_pitch, 4))
         return true;
   }

   return false;
}

void
radv_sdma_copy_buffer_image_unaligned(const struct radv_device *device, struct radv_cmd_stream *cs,
                                      const struct ac_sdma_surf *buf, const struct ac_sdma_surf *img_in,
                                      const VkExtent3D base_extent, struct radeon_winsys_bo *temp_bo, bool to_image)
{
   const struct radv_sdma_chunked_copy_info info = radv_sdma_get_chunked_copy_info(device, img_in, base_extent);
   struct ac_sdma_surf img = *img_in;
   struct ac_sdma_surf tmp = {
      .surf = img.surf,
      .va = radv_buffer_get_va(temp_bo),
      .format = img.format,
      .bpp = img.bpp,
      .pitch = info.aligned_row_pitch,
      .slice_pitch = info.aligned_row_pitch * info.extent_vertical_blocks,
   };

   bool tmz = buf->is_secure;
   assert(!tmz || img_in->is_secure);

   VkExtent3D extent = base_extent;
   extent.depth = 1;

   for (unsigned slice = 0; slice < base_extent.depth; ++slice) {
      for (unsigned row = 0; row < info.extent_vertical_blocks; row += info.num_rows_per_copy) {
         const unsigned rows = MIN2(info.extent_vertical_blocks - row, info.num_rows_per_copy);

         img.offset.y = img_in->offset.y + row;
         img.offset.z = img_in->offset.z + slice;
         extent.height = rows;
         tmp.slice_pitch = tmp.pitch * rows;

         if (!to_image) {
            /* Copy the rows from the source image to the temporary buffer. */
            if (img.surf->is_linear)
               radv_sdma_emit_copy_linear_sub_window(device, cs, &img, &tmp, extent);
            else
               radv_sdma_emit_copy_tiled_sub_window(device, cs, &img, &tmp, extent, true);

            /* Wait for the copy to finish. */
            radv_sdma_emit_nop(device, cs);
         }

         /* buffer to image: copy each row from source buffer to temporary buffer.
          * image to buffer: copy each row from temporary buffer to destination buffer.
          */
         for (unsigned r = 0; r < rows; ++r) {
            const uint64_t buf_va = buf->va + slice * buf->slice_pitch * img.bpp + (row + r) * buf->pitch * img.bpp;
            const uint64_t tmp_va = tmp.va + r * info.aligned_row_pitch * img.bpp;
            radv_sdma_copy_memory(device, cs, to_image ? buf_va : tmp_va, to_image ? tmp_va : buf_va,
                                  info.extent_horizontal_blocks * img.bpp, tmz);
         }

         /* Wait for the copy to finish. */
         radv_sdma_emit_nop(device, cs);

         if (to_image) {
            /* Copy the rows from the temporary buffer to the destination image. */
            if (img.surf->is_linear)
               radv_sdma_emit_copy_linear_sub_window(device, cs, &tmp, &img, extent);
            else
               radv_sdma_emit_copy_tiled_sub_window(device, cs, &img, &tmp, extent, false);

            /* Wait for the copy to finish. */
            radv_sdma_emit_nop(device, cs);
         }
      }
   }
}

void
radv_sdma_copy_image(const struct radv_device *device, struct radv_cmd_stream *cs, const struct ac_sdma_surf *src,
                     const struct ac_sdma_surf *dst, const VkExtent3D extent)
{
   if (src->surf->is_linear) {
      if (dst->surf->is_linear) {
         radv_sdma_emit_copy_linear_sub_window(device, cs, src, dst, extent);
      } else {
         radv_sdma_emit_copy_tiled_sub_window(device, cs, dst, src, extent, false);
      }
   } else {
      if (dst->surf->is_linear) {
         radv_sdma_emit_copy_tiled_sub_window(device, cs, src, dst, extent, true);
      } else {
         radv_sdma_emit_copy_t2t_sub_window(device, cs, src, dst, extent);
      }
   }
}

bool
radv_sdma_use_t2t_scanline_copy(const struct radv_device *device, const struct ac_sdma_surf *src,
                                const struct ac_sdma_surf *dst, const VkExtent3D extent)
{
   bool needs_3d_alignment = src->surf->u.gfx9.resource_type == RADEON_RESOURCE_3D;

   /* These need a linear-to-linear / linear-to-tiled copy. */
   if (src->surf->is_linear || dst->surf->is_linear)
      return false;

   /* SDMA can't do format conversion. */
   assert(src->bpp == dst->bpp);

   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum sdma_version ver = pdev->info.sdma_ip_version;
   if (ver < SDMA_5_0) {
      /* SDMA v4.x and older doesn't support proper mip level selection. */
      if (src->num_levels > 1 || dst->num_levels > 1)
         return true;
   }

   /* The T2T subwindow copy packet only has fields for one metadata configuration.
    * It can either compress or decompress, or copy uncompressed images, but it
    * can't copy from a compressed image to another.
    */
   if (src->is_compressed && dst->is_compressed)
      return true;

   if (ver >= SDMA_7_0) {
      /* No support for tiling format transformation at all. */
      if (src->surf->u.gfx9.swizzle_mode != dst->surf->u.gfx9.swizzle_mode)
         return true;
   } else {
      /* The two images can have a different block size, but must have the same swizzle mode. */
      if (src->surf->micro_tile_mode != dst->surf->micro_tile_mode)
         return true;

      /* Only for 3D standard/displayable swizzle modes. */
      needs_3d_alignment &= (src->surf->micro_tile_mode == RADEON_MICRO_MODE_DISPLAY ||
                             src->surf->micro_tile_mode == RADEON_MICRO_MODE_STANDARD);
   }

   const unsigned log2bpp = util_logbase2(src->bpp);
   const VkExtent3D *const alignment =
      needs_3d_alignment ? &radv_sdma_t2t_alignment_3d[log2bpp] : &radv_sdma_t2t_alignment_2d_and_planar[log2bpp];

   if (!util_is_aligned(extent.width, alignment->width) || !util_is_aligned(extent.height, alignment->height) ||
       !util_is_aligned(extent.depth, alignment->depth))
      return true;

   if (!util_is_aligned(src->offset.x, alignment->width) || !util_is_aligned(src->offset.y, alignment->height) ||
       !util_is_aligned(src->offset.z, alignment->depth))
      return true;

   if (!util_is_aligned(dst->offset.x, alignment->width) || !util_is_aligned(dst->offset.y, alignment->height) ||
       !util_is_aligned(dst->offset.z, alignment->depth))
      return true;

   if (ver < SDMA_6_0 &&
       ((src->img_format == PIPE_FORMAT_S8_UINT && vk_format_is_color(vk_format_from_pipe_format(dst->img_format))) ||
        (vk_format_is_color(vk_format_from_pipe_format(src->img_format)) && dst->img_format == PIPE_FORMAT_S8_UINT))) {
      /* For weird reasons, color<->stencil only T2T subwindow copies on SDMA4-5 don't work as
       * expected, and the driver needs to fallback to scanline copies to workaround them.
       */
      return true;
   }

   return false;
}

void
radv_sdma_copy_image_t2t_scanline(const struct radv_device *device, struct radv_cmd_stream *cs,
                                  const struct ac_sdma_surf *src, const struct ac_sdma_surf *dst,
                                  const VkExtent3D extent, struct radeon_winsys_bo *temp_bo)
{
   const struct radv_sdma_chunked_copy_info info = radv_sdma_get_chunked_copy_info(device, src, extent);
   struct ac_sdma_surf t2l_src = *src;
   struct ac_sdma_surf t2l_dst = {
      .surf = src->surf,
      .va = radv_buffer_get_va(temp_bo),
      .format = src->format,
      .bpp = src->bpp,
      .is_compressed = src->is_compressed,
      .pitch = info.aligned_row_pitch,
   };
   struct ac_sdma_surf l2t_dst = *dst;
   struct ac_sdma_surf l2t_src = {
      .surf = dst->surf,
      .va = radv_buffer_get_va(temp_bo),
      .format = dst->format,
      .bpp = dst->bpp,
      .is_compressed = dst->is_compressed,
      .pitch = info.aligned_row_pitch,
   };

   for (unsigned slice = 0; slice < extent.depth; ++slice) {
      for (unsigned row = 0; row < info.extent_vertical_blocks; row += info.num_rows_per_copy) {
         const unsigned rows = MIN2(info.extent_vertical_blocks - row, info.num_rows_per_copy);

         const VkExtent3D new_extent = {
            .width = info.extent_horizontal_blocks,
            .height = rows,
            .depth = 1,
         };

         t2l_src.offset.y = src->offset.y + row;
         t2l_src.offset.z = src->offset.z + slice;
         t2l_dst.slice_pitch = t2l_dst.pitch * rows;

         radv_sdma_emit_copy_tiled_sub_window(device, cs, &t2l_src, &t2l_dst, new_extent, true);
         radv_sdma_emit_nop(device, cs);

         l2t_dst.offset.y = dst->offset.y + row;
         l2t_dst.offset.z = dst->offset.z + slice;
         l2t_src.slice_pitch = l2t_src.pitch * rows;

         radv_sdma_emit_copy_tiled_sub_window(device, cs, &l2t_dst, &l2t_src, new_extent, false);
         radv_sdma_emit_nop(device, cs);
      }
   }
}

bool
radv_sdma_supports_image(const struct radv_device *device, const struct radv_image *image, bool to_image)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (radv_is_format_emulated(pdev, image->vk.format))
      return to_image ? false : true;

   if (!pdev->info.sdma_supports_sparse && (image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_RESIDENCY_BIT_KHR))
      return false;

   if (image->vk.samples != VK_SAMPLE_COUNT_1_BIT)
      return false;

   return true;
}
