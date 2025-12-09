/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018-2019 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <stdio.h>

#include "freedreno_layout.h"

void
fdl5_layout_image(struct fdl_layout *layout, const struct fdl_image_params *params)
{
   memset(layout, 0, sizeof(*layout));

   assert(params->nr_samples > 0);
   assert(!params->ubwc && !params->force_ubwc);

   layout->width0 = params->width0;
   layout->height0 = params->height0;
   layout->depth0 = params->depth0;

   layout->cpp = util_format_get_blocksize(params->format);
   layout->cpp *= params->nr_samples;
   layout->cpp_shift = ffs(layout->cpp) - 1;

   layout->format = params->format;
   layout->nr_samples = params->nr_samples;
   layout->layer_first = !params->is_3d;

   layout->tile_mode = params->tile_mode;
   layout->linear_fallback_threshold_texels = FDL_LINEAR_FALLBACK_THRESHOLD;

   uint32_t heightalign = layout->cpp == 1 ? 32 : 16;
   /* in layer_first layout, the level (slice) contains just one
    * layer (since in fact the layer contains the slices)
    */
   uint32_t layers_in_level = layout->layer_first ? 1 : params->array_size;

   /* use 128 pixel alignment for cpp=1 and cpp=2 */
   if (layout->cpp < 4 && layout->tile_mode)
      fdl_set_pitchalign(layout, fdl_cpp_shift(layout) + 7);
   else
      fdl_set_pitchalign(layout, fdl_cpp_shift(layout) + 6);

   for (uint32_t level = 0; level < params->mip_levels; level++) {
      uint32_t depth = u_minify(params->depth0, level);
      struct fdl_slice *slice = &layout->slices[level];
      uint32_t tile_mode = fdl_tile_mode(layout, level);
      uint32_t pitch = fdl_pitch(layout, level);
      uint32_t nblocksy =
         util_format_get_nblocksy(params->format, u_minify(params->height0, level));

      if (tile_mode) {
         nblocksy = align(nblocksy, heightalign);
      } else {
         /* The blits used for mem<->gmem work at a granularity of
          * 32x32, which can cause faults due to over-fetch on the
          * last level.  The simple solution is to over-allocate a
          * bit the last level to ensure any over-fetch is harmless.
          * The pitch is already sufficiently aligned, but height
          * may not be:
          */
         if (level == params->mip_levels - 1)
            nblocksy = align(nblocksy, 32);
      }

      slice->offset = layout->size;

      /* 1d array and 2d array textures must all have the same layer size
       * for each miplevel on a3xx. 3d textures can have different layer
       * sizes for high levels, but the hw auto-sizer is buggy (or at least
       * different than what this code does), so as soon as the layer size
       * range gets into range, we stop reducing it.
       */
      if (params->is_3d) {
         if (level <= 1 || layout->slices[level - 1].size0 > 0xf000) {
            slice->size0 = align(nblocksy * pitch, 4096);
         } else {
            slice->size0 = layout->slices[level - 1].size0;
         }
      } else {
         slice->size0 = nblocksy * pitch;
      }

      layout->size += slice->size0 * depth * layers_in_level;
   }

   if (layout->layer_first) {
      layout->layer_size = align64(layout->size, 4096);
      layout->size = layout->layer_size * params->array_size;
   }
}
