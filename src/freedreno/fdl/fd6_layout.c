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

#include "fd6_hw.h"

void
fdl6_get_ubwc_blockwidth(const struct fdl_layout *layout,
                         uint32_t *blockwidth, uint32_t *blockheight)
{
   /* UBWC compression for cpp above 32 isn't supported,
    * and using zero blocksize will effectively disable it.
    */
   static const struct {
      uint8_t width;
      uint8_t height;
   } blocksize[] = {
      { 16, 4 }, /* cpp = 1 */
      { 16, 4 }, /* cpp = 2 */
      { 16, 4 }, /* cpp = 4 */
      {  8, 4 }, /* cpp = 8 */
      {  4, 4 }, /* cpp = 16 */
      {  4, 2 }, /* cpp = 32 */
      {  0, 0 }, /* cpp = 64 */
      {  0, 0 }, /* cpp = 128 */
   };

   /* special case for r8g8: */
   if (fdl6_is_r8g8_layout(layout)) {
      *blockwidth = 16;
      *blockheight = 8;
      return;
   }

   if (layout->format == PIPE_FORMAT_Y8_UNORM) {
      *blockwidth = 32;
      *blockheight = 8;
      return;
   }

   /* special case for 1bpp/2bpp + MSAA (note layout->cpp is already
    * pre-multiplied by nr_samples):
    */
   if ((layout->cpp / layout->nr_samples <= 2) && (layout->nr_samples > 1)) {
      if (layout->nr_samples == 2) {
         *blockwidth = 8;
         *blockheight = 4;
      } else if (layout->nr_samples == 4) {
         *blockwidth = 4;
         *blockheight = 4;
      } else if (layout->nr_samples == 8) {
         *blockwidth = 4;
         *blockheight = 2;
      } else {
         UNREACHABLE("bad nr_samples");
      }
      return;
   }

   uint32_t cpp = fdl_cpp_shift(layout);
   assert(cpp < ARRAY_SIZE(blocksize));
   *blockwidth = blocksize[cpp].width;
   *blockheight = blocksize[cpp].height;
}

static void
fdl6_tile_alignment(struct fdl_layout *layout, uint32_t *heightalign)
{
   layout->pitchalign = fdl_cpp_shift(layout);
   *heightalign = 16;

   if (fdl6_is_r8g8_layout(layout) || layout->cpp == 1) {
      layout->pitchalign = 1;
      *heightalign = 32;
   } else if (layout->cpp == 2) {
      layout->pitchalign = 2;
   }

   /* Empirical evidence suggests that images with UBWC could have much
    * looser alignment requirements, however the validity of alignment is
    * heavily undertested and the "officially" supported alignment is 4096b.
    */
   if (layout->ubwc || util_format_is_depth_or_stencil(layout->format) ||
       fdl6_is_r8g8_layout(layout))
      layout->base_align = 4096;
   else if (layout->cpp == 1)
      layout->base_align = 64;
   else if (layout->cpp == 2)
      layout->base_align = 128;
   else
      layout->base_align = 256;
}

/* NOTE: good way to test this is:  (for example)
 *  piglit/bin/texelFetch fs sampler3D 100x100x8
 */
bool
fdl6_layout_image(struct fdl_layout *layout, const struct fd_dev_info *info,
                  const struct fdl_image_params *params,
                  const struct fdl_explicit_layout *explicit_layout)
{
   uint32_t offset = 0, heightalign;
   uint32_t ubwc_blockwidth, ubwc_blockheight;
   uint32_t sparse_blockwidth, sparse_blockheight;

   memset(layout, 0, sizeof(*layout));

   assert(params->nr_samples > 0);

   layout->width0 = params->width0;
   layout->height0 = params->height0;
   layout->depth0 = params->depth0;
   layout->mip_levels = params->mip_levels;

   layout->cpp = util_format_get_blocksize(params->format);
   layout->cpp *= params->nr_samples;
   layout->cpp_shift = ffs(layout->cpp) - 1;

   layout->format = params->format;
   layout->nr_samples = params->nr_samples;
   layout->layer_first = !params->is_3d;
   layout->is_mutable = params->is_mutable;

   layout->ubwc = params->ubwc;
   layout->tile_mode = params->tile_mode;
   uint32_t sparse_blocksize = 65536;

   if (!util_is_power_of_two_or_zero(layout->cpp)) {
      /* R8G8B8 and other 3 component formats don't get UBWC: */
      ubwc_blockwidth = ubwc_blockheight = 0;
      layout->ubwc = false;
   } else {
      fdl6_get_ubwc_blockwidth(layout, &ubwc_blockwidth, &ubwc_blockheight);

      fdl_get_sparse_block_size(params->format, params->nr_samples,
                                &sparse_blockwidth, &sparse_blockheight);
      assert(sparse_blocksize == sparse_blockwidth * sparse_blockheight * layout->cpp);

      /* For simplicity support UBWC only for 3D images without mipmaps,
       * most d3d11 games don't use mipmaps for 3D images.
       */
      if (params->depth0 > 1 && params->mip_levels > 1)
         layout->ubwc = false;

      if (ubwc_blockwidth == 0)
         layout->ubwc = false;
   }

   assert(!params->force_ubwc || layout->ubwc);

   layout->linear_fallback_threshold_texels =
      fdl_linear_fallback_threshold_texels(layout, info);

   if (!params->force_ubwc &&
       layout->width0 < layout->linear_fallback_threshold_texels) {
      layout->ubwc = false;
      /* Linear D/S is not supported by HW. */
      if (!util_format_is_depth_or_stencil(params->format))
         layout->tile_mode = TILE6_LINEAR;
   }

   /* Linear D/S is not supported by HW. */
   if (util_format_is_depth_or_stencil(params->format))
      layout->tile_all = true;

   if (layout->ubwc && !info->props.has_ubwc_linear_mipmap_fallback)
      layout->tile_all = true;

   if (layout->tile_mode != TILE6_LINEAR &&
       params->force_disable_linear_fallback) {
      layout->tile_all = true;
   }

   /* in layer_first layout, the level (slice) contains just one
    * layer (since in fact the layer contains the slices)
    */
   uint32_t layers_in_level = layout->layer_first ? 1 : params->array_size;

   /* note: for tiled+noubwc layouts, we can use a lower pitchalign
    * which will affect the linear levels only, (the hardware will still
    * expect the tiled alignment on the tiled levels)
    */
   if (layout->tile_mode) {
      fdl6_tile_alignment(layout, &heightalign);
   } else {
      layout->base_align = 64;
      layout->pitchalign = 0;

      if (util_is_power_of_two_or_zero(layout->cpp)) {
         /* align pitch to at least 16 pixels:
          * both turnip and galium assume there is enough alignment for 16x4
          * aligned gmem store. turnip can use CP_BLIT to work without this
          * extra alignment, but gallium driver doesn't implement it yet
          */
         if (layout->cpp > 4)
            layout->pitchalign = fdl_cpp_shift(layout) - 2;

         /* when possible, use a bit more alignment than necessary
          * presumably this is better for performance?
          */
         if (!explicit_layout)
            layout->pitchalign = fdl_cpp_shift(layout);
      } else {
         /* 3 component formats have pitch aligned as their counterpart
          * 4 component formats
          */
         layout->cpp_shift = ffs(util_next_power_of_two(layout->cpp)) - 1;
         layout->pitchalign = layout->cpp_shift;
      }

      /* not used, avoid "may be used uninitialized" warning */
      heightalign = 1;
   }

   fdl_set_pitchalign(layout, layout->pitchalign + 6);

   if (explicit_layout) {
      offset = explicit_layout->offset;
      layout->pitch0 = explicit_layout->pitch;
      if (align(layout->pitch0, 1 << layout->pitchalign) != layout->pitch0)
         return false;
   }

   uint32_t ubwc_width0 = params->width0;
   uint32_t ubwc_height0 = params->height0;
   uint32_t ubwc_tile_height_alignment = RGB_TILE_HEIGHT_ALIGNMENT;
   if (params->mip_levels > 1) {
      /* With mipmapping enabled, UBWC layout is power-of-two sized,
       * specified in log2 width/height in the descriptors.  The height
       * alignment is 64 for mipmapping, but for buffer sharing (always
       * single level) other participants expect 16.
       */
      ubwc_width0 = util_next_power_of_two(params->width0);
      ubwc_height0 = util_next_power_of_two(params->height0);
      ubwc_tile_height_alignment = 64;
   }
   layout->ubwc_width0 = align(DIV_ROUND_UP(ubwc_width0, ubwc_blockwidth),
                               RGB_TILE_WIDTH_ALIGNMENT);
   ubwc_height0 = align(DIV_ROUND_UP(ubwc_height0, ubwc_blockheight),
                        ubwc_tile_height_alignment);

   uint32_t min_3d_layer_size = 0;
   bool in_sparse_miptail = false;

   for (uint32_t level = 0; level < params->mip_levels; level++) {
      uint32_t depth = u_minify(params->depth0, level);
      struct fdl_slice *slice = &layout->slices[level];
      struct fdl_slice *ubwc_slice = &layout->ubwc_slices[level];
      enum a6xx_tile_mode tile_mode = fdl_tile_mode(layout, level);
      uint32_t pitch = fdl_pitch(layout, level);
      uint32_t width = u_minify(params->width0, level);
      uint32_t height = u_minify(params->height0, level);

      uint32_t nblocksx = util_format_get_nblocksx(params->format, width);
      uint32_t nblocksy = util_format_get_nblocksy(params->format, height);

      /* Follow the Vulkan requirements for when the miptail begins. */
      if (params->sparse &&
          (nblocksx < sparse_blockwidth || nblocksy < sparse_blockheight) &&
          !in_sparse_miptail) {
         in_sparse_miptail = true;
         layout->mip_tail_first_lod = level;
         /* The algorithm here follows the HW, which should ensure that the
          * miptail is page aligned. If not we're in big trouble.
          */
         assert(layout->size % 4096 == 0);
      }

      if (tile_mode)
         nblocksy = align(nblocksy, heightalign);

      /* The blits used for mem<->gmem work at a granularity of
       * 16x4, which can cause faults due to over-fetch on the
       * last level.  The simple solution is to over-allocate a
       * bit the last level to ensure any over-fetch is harmless.
       * The pitch is already sufficiently aligned, but height
       * may not be. note this only matters if last level is linear
       */
      if (level == params->mip_levels - 1)
         nblocksy = align(nblocksy, 4);

      slice->offset = offset + layout->size;

      /* 1d array and 2d array textures must all have the same layer size for
       * each miplevel on a6xx.  For 3D, the layer size automatically reduces
       * until the value we specify in TEX_CONST_3_MIN_LAYERSZ, which is used to
       * make sure that we follow alignment requirements after minification.
       */
      if (params->is_3d) {
         if (level == 0) {
            slice->size0 = align(nblocksy * pitch, 4096);
         } else if (min_3d_layer_size) {
            slice->size0 = min_3d_layer_size;
         } else {
            /* Note: level * 2 for minifying in both X and Y. */
            slice->size0 = u_minify(layout->slices[0].size0, level * 2);

            /* If this level didn't reduce the pitch by half, then fix it up,
             * and this is the end of layer size reduction.
             */
            uint32_t pitch = fdl_pitch(layout, level);
            if (pitch != fdl_pitch(layout, level - 1) / 2)
               min_3d_layer_size = slice->size0 = nblocksy * pitch;

            /* If the height wouldn't be aligned, stay aligned instead */
            if (slice->size0 < nblocksy * pitch)
               min_3d_layer_size = slice->size0 = nblocksy * pitch;

            /* If the size would become un-page-aligned, stay aligned instead. */
            if (align(slice->size0, 4096) != slice->size0)
               min_3d_layer_size = slice->size0 = align(slice->size0, 4096);
         }
      } else {
         slice->size0 = nblocksy * pitch;
      }

      layout->size += slice->size0 * depth * layers_in_level;

      if (layout->ubwc && tile_mode != TILE6_LINEAR) {
         /* with UBWC every level is aligned to 4K */
         layout->size = align64(layout->size, 4096);

         uint32_t meta_pitch = fdl_ubwc_pitch(layout, level);
         uint32_t meta_height =
            align(u_minify(ubwc_height0, level), ubwc_tile_height_alignment);

         ubwc_slice->size0 =
            align(meta_pitch * meta_height, UBWC_PLANE_SIZE_ALIGNMENT);
         ubwc_slice->offset = offset + layout->ubwc_layer_size;
         layout->ubwc_layer_size += ubwc_slice->size0;
      }
   }

   if (layout->layer_first && !explicit_layout) {
      layout->layer_size = align64(layout->size, 4096);

      if (params->sparse) {
         if (!in_sparse_miptail) {
            layout->mip_tail_first_lod = layout->mip_levels;
            assert(layout->layer_size % 4096 == 0);
         }

         /* Honor the Vulkan requirement that the mip tail region is a
          * multiple of the sparse block size (i.e. 64k). Note that the mip
          * tail offset is *not* required to be a multiple of the sparse
          * block size, and we can't guarantee that anyway as the miplevel
          * offset is controlled by the HW. The partial block before the
          * miptail will only be partially mapped.
          */
         uint32_t mip_tail_size = fdl_sparse_miptail_size(layout);

         /* Vulkan CTS requires that as the image size decreases, the
          * memory requirements always decrease or stay the same. If there
          * is no sparse miptail, apply the same padding to the last layer
          * so that if the image becomes small enough to have a sparse
          * miptail then the padding still applies.
          */
         if (mip_tail_size == 0) {
            mip_tail_size = layout->slices[params->mip_levels - 1].size0;
         }

         assert(mip_tail_size % 4096 == 0);

         layout->layer_size +=
            (sparse_blocksize - mip_tail_size) % sparse_blocksize;
      }

      layout->size = layout->layer_size * params->array_size;
   }

   /* Place the UBWC slices before the uncompressed slices, because the
    * kernel expects UBWC to be at the start of the buffer.  In the HW, we
    * get to program the UBWC and non-UBWC offset/strides
    * independently.
    */
   if (layout->ubwc) {
      assert(!(params->depth0 > 1 && params->mip_levels > 1));
      for (uint32_t level = 0; level < params->mip_levels; level++) {
         layout->slices[level].offset +=
            layout->ubwc_layer_size * params->array_size * params->depth0;
      }
      layout->size += layout->ubwc_layer_size * params->array_size * params->depth0;
   }

   /* include explicit offset in size */
   layout->size += offset;

   return true;
}
