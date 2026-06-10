/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "compiler/nir/nir_defines.h"
#include "compiler/shader_enums.h"

bool
blorp_check_in_bounds(uint4 bounds_rect, uint2 pos)
{
   uint x0 = bounds_rect[0], x1 = bounds_rect[1];
   uint y0 = bounds_rect[2], y1 = bounds_rect[3];

   return pos.x >= x0 && pos.x < x1 &&
          pos.y >= y0 && pos.y < y1;
}

void nir_image_store(uint handle, int4 coords, uint sample_index,
                     uint4 colors, uint lod, uint image_dim,
                     uint image_array, uint format, uint access,
                     uint range_base, uint src_type);

/* Used by vkCmdCopyMemoryIndirectKHR. */
void
blorp_copy_memory_indirect_shader(
   global uint *indirect_buf,
   uint copy_count,
   ulong stride_bytes,
   uint global_id)
{
   for (uint c = 0; c < copy_count; c++) {
      uint idx = (uint)(c * (stride_bytes / 4));

      /* The spec says the minimum alignment is 4 instead of 8, so we have to
       * do these tricks.
       */
      VkCopyMemoryIndirectCommandKHR cmd =
         (*(global VkCopyMemoryIndirectCommandKHR*)&indirect_buf[idx]);

      uint copy_size_ints = cmd.size / 4;

      global int *src = (global int*)cmd.srcAddress;
      global int *dst = (global int*)cmd.dstAddress;

      if (global_id < copy_size_ints)
         dst[global_id] = src[global_id];
   }
}

struct img_copy_params {
   ulong src_address;
   uint2 row_size_px;
   uint base_layer;
   uint layer_count;
   uint3 offset;
   uint3 extent;
};

void
read_img_params(
   global uint *indirect_buf,
   uint copy_idx,
   ulong stride_bytes,
   uint dimensions,
   uint max_layer,
   uint3 format_block_size,
   bool is_block_compressed,
   struct img_copy_params *p)
{
   uint idx = copy_idx * (stride_bytes / 4);

   VkCopyMemoryToImageIndirectCommandKHR cmd =
         (*(global VkCopyMemoryToImageIndirectCommandKHR*)&indirect_buf[idx]);

   p->src_address = cmd.srcAddress;
   p->row_size_px = (uint2)(cmd.bufferRowLength, cmd.bufferImageHeight);

   /* We don't use imageSubresource.aspectMask and imageSubresource.mipLevel,
    * those are dealt with when the application calls
    * vkCmdCopyMemoryToImageIndirectKHR().
    */

   p->base_layer = cmd.imageSubresource.baseArrayLayer;
   p->layer_count = cmd.imageSubresource.layerCount;

   p->offset = (uint3)(cmd.imageOffset.x, cmd.imageOffset.y,
                       cmd.imageOffset.z);
   p->extent = (uint3)(cmd.imageExtent.width, cmd.imageExtent.height,
                       cmd.imageExtent.depth);

   if (p->row_size_px.x == 0)
      p->row_size_px.x = p->extent.x;
   if (p->row_size_px.y == 0)
      p->row_size_px.y = p->extent.y;

   /* Our code deals with blocks, not pixels. */
   if (is_block_compressed) {
      p->offset /= format_block_size;
      p->extent = DIV_ROUND_UP(p->extent, format_block_size);
   }

   /* Users can pass 3D images with the Z axis as an array layer. */
   if (dimensions == 3 && (p->base_layer != 0 || p->layer_count != 1)) {
      p->offset.z = p->base_layer;
      p->extent.z = p->layer_count;
      p->base_layer = 0;
      p->layer_count = 1;
   }

   /* This handles VK_REMAINING_ARRAY_LAYERS and bugs. */
   if (p->base_layer + p->layer_count > max_layer + 1)
      p->layer_count = max_layer - p->base_layer + 1;
}

uint4
get_pixel(
   ulong src_address,
   uint format_Bpb)
{
   switch (format_Bpb) {
   case 1: {
      global uchar *src_buf = (global uchar *)src_address;
      return (uint4)(src_buf[0], 0, 0, 0);
   }
   case 2: {
      global ushort *src_buf = (global ushort *)src_address;
      return (uint4)(src_buf[0], 0, 0, 0);
   }
   case 4: {
      global uint *src_buf = (global uint *)src_address;
      return (uint4)(src_buf[0], 0, 0, 0);
   }
   case 8: {
      global uint *src_buf = (global uint *)src_address;
      return (uint4)(vload2(0, src_buf), 0, 0);
   }
   case 16: {
      global uint *src_buf = (global uint *)src_address;
      return (uint4)(vload4(0, src_buf));
   }
   default:
      /* TODO: support 3, 6, 12. */
      return (uint4)(0xFF, 0, 0, 0xFF);
   }
}

int4
get_coords(
   uint3 pos,
   uint layer,
   uint2 dest_coord_offsets,
   int dimensions,
   int forced_layer_or_z)
{
   int4 ret;

   pos.xy += dest_coord_offsets;

   switch (dimensions) {
   case 1:
      ret = (int4)(pos.x, layer, 0, 0);
      break;
   case 2:
      ret = (int4)(pos.x, pos.y, layer, 0);
      break;
   case 3:
   default:
      ret = (int4)(pos.x, pos.y, pos.z, 0);
      break;
   }

   if (forced_layer_or_z != -1) {
      ret.z = 0;
      ret.w = 0;
   }

   return ret;
}

void
write_pixel(
   const int4 coords,
   const uint4 colors)
{
   /* We don't seem to need to set image_array to true if we set sampler_dim
    * to 3D.
    * Setting mip_level does not do anything on Intel, we set mip levels
    * through the bindings.
    */
   nir_image_store(0 /* The image handle. */,
                   coords, /* See get_coords(). */
                   0, /* Sample index for multi-sampling. */
                   colors, /* The RGBA pixels, in src_type. */
                   0 /* mip_level */,
                   GLSL_SAMPLER_DIM_3D,
                   false, /* image_array */
                   0, /* format */
                   ACCESS_NON_READABLE, /* access */
                   0, /* range_base */
                   nir_type_int32 /* src_type */);
}

/* Used by vkCmdCopyMemoryToImageIndirectKHR. */
void
blorp_copy_memory_to_image_indirect_shader(
   /* Actual parameters. */
   global uint *indirect_buf,
   ulong stride_bytes,
   uint copy_idx,
   uint max_layer,
   uint2 dest_coord_offsets,
   uint3 global_id,

   /* These are shader keys, they are NIR immediates. */
   uint dimensions,
   int forced_layer_or_z,
   ushort format_Bpb,
   uint3 format_block_size,
   bool is_block_compressed)
{
   /* We have one invocation per texel of a given a mip level.  This means
    * that for the pixels outside the copy area, we'll hit the 'continue'
    * below.
    */
   uint3 src_pos = global_id;
   uint layer;

   /* 'forced_layer_or_z' means that whatever slice or layer we're
    * trying to work with is set as part of our binding (view) as depth
    * 0 or layer 0, depending on the dimensionality. This can happen
    * when we're trying to pretend a format is something that it's not
    * (e.g., we're treating a block compressed 4x4 64bpp format as an
    * r32g32 format).
    */
   if (forced_layer_or_z != -1) {
      if (dimensions == 3) {
         src_pos.z = forced_layer_or_z;
         layer = 0;
      } else {
         src_pos.z = 0;
         layer = forced_layer_or_z;
      }
   } else {
      if (dimensions == 3) {
         src_pos.z = global_id.z;
         layer = 0;
      } else {
         src_pos.z = 0;
         layer = global_id.z;
      }
   }

   struct img_copy_params p;
   read_img_params(indirect_buf, copy_idx, stride_bytes, dimensions,
                   max_layer, format_block_size, is_block_compressed, &p);

   if (any(src_pos >= p.extent))
      return;

   uint3 dst_pos = src_pos + p.offset;
   if (forced_layer_or_z == -1 && dimensions != 3)
      layer = global_id.z + p.base_layer;

   if (layer > p.base_layer + p.layer_count - 1)
      return;

   uint2 row_size_blocks = p.row_size_px;
   if (is_block_compressed) {
      row_size_blocks = DIV_ROUND_UP(row_size_blocks,
                                     format_block_size.xy);
   }
   uint row_length_bytes = row_size_blocks.x * format_Bpb;
   uint row_height_bytes = row_size_blocks.y * row_length_bytes;

   uint buf_z_offset_bytes = src_pos.z * row_height_bytes;
   uint buf_y_offset_bytes = src_pos.y * row_length_bytes;
   uint buf_x_offset_bytes = src_pos.x * format_Bpb;

   uint layer_offset_bytes = (layer - p.base_layer) * row_height_bytes;
   uint buf_offset_bytes = layer_offset_bytes +
                           buf_z_offset_bytes +
                           buf_y_offset_bytes +
                           buf_x_offset_bytes;

   uint4 colors = get_pixel(p.src_address + buf_offset_bytes,
                            format_Bpb);

   int4 coords = get_coords(dst_pos, layer, dest_coord_offsets, dimensions,
                            forced_layer_or_z);

   write_pixel(coords, colors);
}
