/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <inttypes.h>
#include <stdio.h>

#include "freedreno_layout.h"

void
fdl_layout_buffer(struct fdl_layout *layout, uint32_t size)
{
   layout->width0 = size;
   layout->pitch0 = size;
   layout->height0 = 1;
   layout->depth0 = 1;
   layout->cpp = 1;
   layout->cpp_shift = 0;
   layout->size = size;
   layout->format = PIPE_FORMAT_R8_UINT;
   layout->nr_samples = 1;
}

const char *
fdl_tile_mode_desc(const struct fdl_layout *layout, int level)
{
   if (fdl_ubwc_enabled(layout, level))
      return "UBWC";
   else if (fdl_tile_mode(layout, level) == 0) /* TILE6_LINEAR and friends */
      return "linear";
   else
      return "tiled";
}

void
fdl_dump_layout(struct fdl_layout *layout)
{
   for (uint32_t level = 0;
        level < ARRAY_SIZE(layout->slices) && layout->slices[level].size0;
        level++) {
      struct fdl_slice *slice = &layout->slices[level];
      struct fdl_slice *ubwc_slice = &layout->ubwc_slices[level];

      fprintf(
         stderr,
         "%s: %ux%ux%u@%ux%u:\t%2u: stride=%4u, size=%6u,%6u, "
         "aligned_height=%3u, offset=0x%x,0x%x, layersz %5" PRIu64 ",%5" PRIu64 " %s %s\n",
         util_format_name(layout->format), u_minify(layout->width0, level),
         u_minify(layout->height0, level), u_minify(layout->depth0, level),
         layout->cpp, layout->nr_samples, level, fdl_pitch(layout, level),
         slice->size0, ubwc_slice->size0,
         slice->size0 / fdl_pitch(layout, level), slice->offset,
         ubwc_slice->offset, layout->layer_size, layout->ubwc_layer_size,
         fdl_tile_mode_desc(layout, level), layout->is_mutable ? "mutable" : "");
   }
}

void
fdl_get_sparse_block_size(enum pipe_format format, uint32_t nr_samples,
                          uint32_t *blockwidth, uint32_t *blockheight)
{
   /* This is taken from the table in section 33.4.3 "Standard Sparse Image
    * Block Shapes"
    */

   switch (nr_samples) {
   case 1:
      switch (util_format_get_blocksize(format)) {
      case 1:
         *blockwidth = 256;
         *blockheight = 256;
         break;
      case 2:
         *blockwidth = 256;
         *blockheight = 128;
         break;
      case 4:
         *blockwidth = 128;
         *blockheight = 128;
         break;
      case 8:
         *blockwidth = 128;
         *blockheight = 64;
         break;
      case 16:
         *blockwidth = 64;
         *blockheight = 64;
         break;
      default:
         UNREACHABLE("invalid block size");
      }
      break;
   case 2:
      switch (util_format_get_blocksize(format)) {
      case 1:
         *blockwidth = 128;
         *blockheight = 256;
         break;
      case 2:
         *blockwidth = 128;
         *blockheight = 128;
         break;
      case 4:
         *blockwidth = 64;
         *blockheight = 128;
         break;
      case 8:
         *blockwidth = 64;
         *blockheight = 64;
         break;
      case 16:
         *blockwidth = 32;
         *blockheight = 64;
         break;
      default:
         UNREACHABLE("invalid block size");
      }
      break;
   case 4:
      switch (util_format_get_blocksize(format)) {
      case 1:
         *blockwidth = 128;
         *blockheight = 128;
         break;
      case 2:
         *blockwidth = 128;
         *blockheight = 64;
         break;
      case 4:
         *blockwidth = 64;
         *blockheight = 64;
         break;
      case 8:
         *blockwidth = 64;
         *blockheight = 32;
         break;
      case 16:
         *blockwidth = 32;
         *blockheight = 32;
         break;
      default:
         UNREACHABLE("invalid block size");
      }
      break;
   case 8:
      switch (util_format_get_blocksize(format)) {
      case 1:
         *blockwidth = 64;
         *blockheight = 128;
         break;
      case 2:
         *blockwidth = 64;
         *blockheight = 64;
         break;
      case 4:
         *blockwidth = 32;
         *blockheight = 64;
         break;
      case 8:
         *blockwidth = 32;
         *blockheight = 32;
         break;
      case 16:
         *blockwidth = 16;
         *blockheight = 32;
         break;
      default:
         UNREACHABLE("invalid block size");
      }
      break;
   /* 16X MSAA is not supported */
   default:
      UNREACHABLE("invalid MSAA count");
   }
}

