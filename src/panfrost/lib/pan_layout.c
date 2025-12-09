/*
 * Copyright (C) 2019-2022 Collabora, Ltd.
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "pan_layout.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "pan_afbc.h"
#include "pan_afrc.h"
#include "pan_image.h"
#include "pan_mod.h"
#include "pan_props.h"

/*
 * Computes sizes for checksumming, which is 8 bytes per 16x16 tile.
 * Checksumming is believed to be a CRC variant (CRC64 based on the size?).
 * This feature is also known as "transaction elimination".
 * CRC values are prefetched by 32x32 (64x64 on v12+) regions so size needs to
 * be aligned.
 */

#define CHECKSUM_TILE_WIDTH     16
#define CHECKSUM_TILE_HEIGHT    16
#define CHECKSUM_BYTES_PER_TILE 8

static void
init_slice_crc_info(unsigned arch, struct pan_image_slice_layout *slice,
                    unsigned width_px, unsigned height_px, uint64_t offset_B)
{
   unsigned checksum_region_size_px = pan_meta_tile_size(arch);
   unsigned checksum_x_tile_per_region =
      (checksum_region_size_px / CHECKSUM_TILE_WIDTH);
   unsigned checksum_y_tile_per_region =
      (checksum_region_size_px / CHECKSUM_TILE_HEIGHT);

   unsigned tile_count_x = checksum_x_tile_per_region *
                           DIV_ROUND_UP(width_px, checksum_region_size_px);
   unsigned tile_count_y = checksum_y_tile_per_region *
                           DIV_ROUND_UP(height_px, checksum_region_size_px);

   slice->crc.offset_B = offset_B;
   slice->crc.stride_B = tile_count_x * CHECKSUM_BYTES_PER_TILE;
   slice->crc.size_B = slice->crc.stride_B * tile_count_y;
}

static struct pan_image_extent
get_mip_level_extent(const struct pan_image_props *props, unsigned plane_idx,
                     unsigned mip_level)
{
   return (struct pan_image_extent){
      .width = u_minify(util_format_get_plane_width(props->format, plane_idx,
                                                    props->extent_px.width),
                        mip_level),
      .height = u_minify(util_format_get_plane_height(props->format, plane_idx,
                                                      props->extent_px.height),
                         mip_level),
      .depth = u_minify(props->extent_px.depth, mip_level),
   };
}

bool
pan_image_layout_init(
   unsigned arch, struct pan_image *image, unsigned plane_idx,
   const struct pan_image_layout_constraints *explicit_layout_constraints)
{
   assert(image->mod_handler);
   assert(plane_idx < ARRAY_SIZE(image->planes) &&
          plane_idx < util_format_get_num_planes(image->props.format));
   assert(image->planes[plane_idx]);

   const struct pan_mod_handler *mod_handler = image->mod_handler;
   const struct pan_image_props *props = &image->props;
   struct pan_image_layout *layout = &image->planes[plane_idx]->layout;

   /* Use explicit layout only when wsi_row_pitch_B is non-zero */
   struct pan_image_layout_constraints layout_constraints = {0};
   if (explicit_layout_constraints)
      layout_constraints = *explicit_layout_constraints;

   const bool use_explicit_layout = layout_constraints.wsi_row_pitch_B != 0;

   /* Explicit stride only work with non-mipmap, non-array, single-sample
    * 2D image without CRC.
    */
   if (use_explicit_layout &&
       (props->extent_px.depth > 1 || props->nr_samples > 1 ||
        props->array_size > 1 || props->dim != MALI_TEXTURE_DIMENSION_2D ||
        props->nr_slices > 1 || props->crc))
      return false;

   if (plane_idx >= util_format_get_num_planes(props->format))
      return false;

   /* Init plane layout data. */
   if (mod_handler->init_plane_layout &&
       !mod_handler->init_plane_layout(props, plane_idx, layout))
      return false;

   /* MSAA is implemented as a 3D texture with z corresponding to the
    * sample #, horrifyingly enough */

   assert(props->extent_px.depth == 1 || props->nr_samples == 1);

   /* Make sure the extent/sample_count is not zero. */
   assert(props->extent_px.width && props->extent_px.height &&
          props->extent_px.depth && props->nr_samples);

   struct pan_image_extent mip_extent_px = {
      .width = util_format_get_plane_width(props->format, plane_idx,
                                           props->extent_px.width),
      .height = util_format_get_plane_height(props->format, plane_idx,
                                             props->extent_px.height),
      .depth = props->extent_px.depth,
   };

   for (unsigned l = 0; l < props->nr_slices; ++l) {
      struct pan_image_slice_layout *slayout = &layout->slices[l];

      if (!mod_handler->init_slice_layout(props, plane_idx, mip_extent_px,
                                          &layout_constraints, slayout))
         return false;

      layout_constraints.offset_B += slayout->size_B;

      /* Add a checksum region if necessary */
      if (props->crc) {
         init_slice_crc_info(arch, slayout, mip_extent_px.width,
                             mip_extent_px.height, layout_constraints.offset_B);
         layout_constraints.offset_B += slayout->crc.size_B;
         slayout->size_B += slayout->crc.size_B;
      }

      mip_extent_px.width = u_minify(mip_extent_px.width, 1);
      mip_extent_px.height = u_minify(mip_extent_px.height, 1);
      mip_extent_px.depth = u_minify(mip_extent_px.depth, 1);
   }

   /* Arrays and cubemaps have the entire miptree duplicated */
   layout->array_stride_B =
      ALIGN_POT(layout_constraints.offset_B - layout->slices[0].offset_B, 64);

   if (layout_constraints.array_align_B) {
      layout->array_stride_B =
         ALIGN_POT(layout->array_stride_B, layout_constraints.array_align_B);
   }

   if (use_explicit_layout) {
      layout->data_size_B =
         layout_constraints.offset_B - explicit_layout_constraints->offset_B;
   } else {
      /* Native images start from offset 0, and the planar plane offset has
       * been at least 4K page aligned below. So the base level slice offset
       * should always be the same with the plane offset.
       */
      assert(!explicit_layout_constraints ||
             explicit_layout_constraints->offset_B ==
                layout->slices[0].offset_B);
      layout->data_size_B = ALIGN_POT(
         (uint64_t)layout->array_stride_B * (uint64_t)props->array_size, 4096);
   }

   return true;
}
