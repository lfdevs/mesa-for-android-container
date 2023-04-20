/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "layout.h"

static void
ail_initialize_linear(struct ail_layout *layout)
{
   /* Select the optimal stride if none is forced */
   if (layout->linear_stride_B == 0) {
      uint32_t minimum_stride_B =
         util_format_get_stride(layout->format, layout->width_px);

      layout->linear_stride_B = ALIGN_POT(minimum_stride_B, AIL_CACHELINE);
   }

   assert((layout->linear_stride_B % 16) == 0 && "Strides must be aligned");

   /* Layer stride must be cache line aligned to pack linear 2D arrays */
   layout->layer_stride_B =
      ALIGN_POT(layout->linear_stride_B * layout->height_px, AIL_CACHELINE);

   layout->size_B = layout->layer_stride_B * layout->depth_px;
}

/*
 * Calculate the minimum integer l such that x 2^-l < y, where x is an integer
 * and y is a power-of-two.
 */
static unsigned
ail_min_mip_below(unsigned x, unsigned y)
{
   assert(util_is_power_of_two_nonzero(y));

   if (x < y)
      return 0;
   else if (util_is_power_of_two_or_zero(x))
      return util_logbase2(x) - util_logbase2(y) + 1;
   else
      return util_logbase2_ceil(x) - util_logbase2(y);
}

/*
 * Get the maximum tile size possible for a given block size. This satisfy
 * width * height * blocksize = 16384 = page size, so each tile is one page.
 */
static inline struct ail_tile
ail_get_max_tile_size(unsigned blocksize_B)
{
   /* clang-format off */
   switch (blocksize_B) {
   case  1: return (struct ail_tile) { 128, 128 };
   case  2: return (struct ail_tile) { 128,  64 };
   case  4: return (struct ail_tile) {  64,  64 };
   case  8: return (struct ail_tile) {  64,  32 };
   case 16: return (struct ail_tile) {  32,  32 };
   default: unreachable("Invalid blocksize");
   }
   /* clang-format on */
}

/*
 * Calculate the number of bytes in a block. This must take both block
 * dimensions and multisampling into account.
 */
static uint32_t
ail_get_block_size_B(struct ail_layout *layout)
{
   ASSERTED const struct util_format_description *desc =
      util_format_description(layout->format);

   assert(((layout->sample_count_sa == 1) ||
           (desc->block.width == 1 && desc->block.height == 1)) &&
          "multisampling and block-compression are mutually-exclusive");

   return util_format_get_blocksize(layout->format) * layout->sample_count_sa;
}

static void
ail_initialize_twiddled(struct ail_layout *layout)
{
   unsigned offset_B = 0;
   unsigned blocksize_B = ail_get_block_size_B(layout);
   unsigned w_el = util_format_get_nblocksx(layout->format, layout->width_px);
   unsigned h_el = util_format_get_nblocksy(layout->format, layout->height_px);

   /* Calculate the tile size used for the large miptree, and the dimensions of
    * level 0 given that tile size.
    */
   struct ail_tile tilesize_el = ail_get_max_tile_size(blocksize_B);
   unsigned stx_tiles = DIV_ROUND_UP(w_el, tilesize_el.width_el);
   unsigned sty_tiles = DIV_ROUND_UP(h_el, tilesize_el.height_el);
   unsigned sarea_tiles = stx_tiles * sty_tiles;

   /* Calculate which level the small power-of-two miptree begins at. The
    * power-of-two miptree is used when either the width or the height is
    * smaller than a single large tile.
    */
   unsigned pot_level = MIN2(ail_min_mip_below(w_el, tilesize_el.width_el),
                             ail_min_mip_below(h_el, tilesize_el.height_el));

   /* First allocate the large miptree. All tiles in the large miptree are of
    * size tilesize_el and have their dimensions given by stx/sty/sarea.
    */
   for (unsigned l = 0; l < MIN2(pot_level, layout->levels); ++l) {
      unsigned tiles = (sarea_tiles >> (2 * l));

      bool pad_left = (stx_tiles & BITFIELD_MASK(l));
      bool pad_bottom = (sty_tiles & BITFIELD_MASK(l));
      bool pad_corner = pad_left && pad_bottom;

      if (pad_left)
         tiles += (sty_tiles >> l);

      if (pad_bottom)
         tiles += (stx_tiles >> l);

      if (pad_corner)
         tiles += 1;

      unsigned size_el = tiles * tilesize_el.width_el * tilesize_el.height_el;
      layout->level_offsets_B[l] = offset_B;
      offset_B = ALIGN_POT(offset_B + (blocksize_B * size_el), AIL_CACHELINE);

      layout->tilesize_el[l] = tilesize_el;
   }

   /* Then begin the POT miptree. Note that we round up to a power-of-two
    * outside the loop. That ensures correct handling of cases like 33x33
    * images, where the round-down error of right-shifting could cause incorrect
    * tile size calculations.
    */
   unsigned potw_el = util_next_power_of_two(u_minify(w_el, pot_level));
   unsigned poth_el = util_next_power_of_two(u_minify(h_el, pot_level));

   /* Finally we allocate the POT miptree, starting at level pot_level. Each
    * level uses the largest power-of-two tile that fits the level.
    */
   for (unsigned l = pot_level; l < layout->levels; ++l) {
      unsigned size_el = potw_el * poth_el;
      layout->level_offsets_B[l] = offset_B;
      offset_B = ALIGN_POT(offset_B + (blocksize_B * size_el), AIL_CACHELINE);

      /* The tilesize is based on the true mipmap level size, not the POT
       * rounded size */
      unsigned tilesize_el =
         util_next_power_of_two(u_minify(MIN2(w_el, h_el), l));
      layout->tilesize_el[l] = (struct ail_tile){tilesize_el, tilesize_el};

      potw_el = u_minify(potw_el, 1);
      poth_el = u_minify(poth_el, 1);
   }

   /* Align layer size if we have mipmaps and one miptree is larger than one
    * page */
   layout->page_aligned_layers = layout->levels != 1 && offset_B > AIL_PAGESIZE;

   /* Single-layer images are not padded unless they are Z/S */
   if (layout->depth_px == 1 &&
       !util_format_is_depth_or_stencil(layout->format))
      layout->page_aligned_layers = false;

   if (layout->page_aligned_layers)
      layout->layer_stride_B = ALIGN_POT(offset_B, AIL_PAGESIZE);
   else
      layout->layer_stride_B = offset_B;

   layout->size_B = layout->layer_stride_B * layout->depth_px;
}

static void
ail_initialize_compression(struct ail_layout *layout)
{
   assert(!util_format_is_compressed(layout->format) &&
          "Compressed pixel formats not supported");
   assert(util_format_get_blockwidth(layout->format) == 1);
   assert(util_format_get_blockheight(layout->format) == 1);
   assert(layout->width_px >= 16 && "Small textures are never compressed");
   assert(layout->height_px >= 16 && "Small textures are never compressed");

   layout->metadata_offset_B = layout->size_B;

   unsigned width_px = ALIGN_POT(layout->width_px, 16);
   unsigned height_px = ALIGN_POT(layout->height_px, 16);

   unsigned compbuf_B = 0;

   for (unsigned l = 0; l < layout->levels; ++l) {
      if (width_px < 16 && height_px < 16)
         break;

      layout->level_offsets_compressed_B[l] = compbuf_B;

      /* The compression buffer seems to have 8 bytes per 16 x 16 pixel block. */
      unsigned cmpw_el = DIV_ROUND_UP(util_next_power_of_two(width_px), 16);
      unsigned cmph_el = DIV_ROUND_UP(util_next_power_of_two(height_px), 16);
      compbuf_B += ALIGN_POT(cmpw_el * cmph_el * 8, AIL_CACHELINE);

      width_px = DIV_ROUND_UP(width_px, 2);
      height_px = DIV_ROUND_UP(height_px, 2);
   }

   layout->compression_layer_stride_B = compbuf_B;
   layout->size_B += layout->compression_layer_stride_B * layout->depth_px;
}

void
ail_make_miptree(struct ail_layout *layout)
{
   assert(layout->width_px >= 1 && "Invalid dimensions");
   assert(layout->height_px >= 1 && "Invalid dimensions");
   assert(layout->depth_px >= 1 && "Invalid dimensions");

   if (layout->tiling == AIL_TILING_LINEAR) {
      assert(layout->levels == 1 && "Invalid linear layout");
      assert(layout->sample_count_sa == 1 &&
             "Multisampled linear layouts not supported");
      assert(util_format_get_blockwidth(layout->format) == 1 &&
             "Strided linear block formats unsupported");
      assert(util_format_get_blockheight(layout->format) == 1 &&
             "Strided linear block formats unsupported");
   } else {
      assert(layout->linear_stride_B == 0 && "Invalid nonlinear layout");
      assert(layout->levels >= 1 && "Invalid dimensions");
      assert(layout->sample_count_sa >= 1 && "Invalid sample count");
   }

   /* Hardware strides are based on the maximum number of levels, so always
    * allocate them all.
    */
   if (layout->levels > 1) {
      unsigned major_axis_px = MAX2(layout->width_px, layout->height_px);

      if (layout->mipmapped_z)
         major_axis_px = MAX2(major_axis_px, layout->depth_px);

      layout->levels = util_logbase2(major_axis_px) + 1;
   }

   assert(util_format_get_blockdepth(layout->format) == 1 &&
          "Deep formats unsupported");

   switch (layout->tiling) {
   case AIL_TILING_LINEAR:
      ail_initialize_linear(layout);
      break;
   case AIL_TILING_TWIDDLED:
      ail_initialize_twiddled(layout);
      break;
   case AIL_TILING_TWIDDLED_COMPRESSED:
      ail_initialize_twiddled(layout);
      ail_initialize_compression(layout);
      break;
   default:
      unreachable("Unsupported tiling");
   }

   layout->size_B = ALIGN_POT(layout->size_B, AIL_CACHELINE);
   assert(layout->size_B > 0 && "Invalid dimensions");
}
