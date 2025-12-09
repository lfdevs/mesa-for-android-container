/*
 * Copyright (C) 2019-2025 Collabora, Ltd.
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 *
 * SPDX-License-Identifier: MIT
 */

#include "pan_mod.h"
#include "pan_afbc.h"
#include "pan_afrc.h"
#include "pan_desc.h"
#include "pan_format.h"
#include "pan_image.h"
#include "pan_layout.h"
#include "pan_props.h"
#include "pan_texture.h"

#include "util/format/u_format.h"

#if PAN_ARCH <= 10
#define MAX_SIZE_B         u_uintN_max(32)
#define MAX_SLICE_STRIDE_B u_uintN_max(32)
#else
#define MAX_SIZE_B         u_uintN_max(48)
#define MAX_SLICE_STRIDE_B u_uintN_max(37)
#endif

static bool
pan_mod_afbc_match(uint64_t mod)
{
   return drm_is_afbc(mod);
}

static uint32_t
pan_mod_afbc_get_wsi_row_pitch(const struct pan_image *image,
                               unsigned plane_idx, unsigned mip_level)
{
   const struct pan_image_props *props = &image->props;
   const struct pan_image_layout *layout = &image->planes[plane_idx]->layout;
   const unsigned header_row_stride_B =
      layout->slices[mip_level].afbc.header.row_stride_B;
   const struct pan_image_block_size tile_extent_el =
      pan_afbc_superblock_size_el(props->format, props->modifier);
   const unsigned tile_payload_size_B =
      tile_extent_el.width * tile_extent_el.height *
      pan_format_get_plane_blocksize(props->format, plane_idx);
   const unsigned tile_row_payload_size_B =
      pan_afbc_stride_blocks(props->format,
                             props->modifier, header_row_stride_B) *
      tile_payload_size_B;

   return tile_row_payload_size_B / pan_afbc_superblock_height(props->modifier);
}

static bool
pan_mod_afbc_init_plane_layout(const struct pan_image_props *props,
                               unsigned plane_idx,
                               struct pan_image_layout *plane_layout)
{
   enum pan_afbc_mode mode =
      pan_afbc_format(PAN_ARCH, props->format, plane_idx);
   if (mode == PAN_AFBC_MODE_INVALID)
      return false;

   plane_layout->afbc.mode = mode;
   return true;
}

static bool
pan_mod_afbc_init_slice_layout(
   const struct pan_image_props *props, unsigned plane_idx,
   struct pan_image_extent mip_extent_px,
   const struct pan_image_layout_constraints *layout_constraints,
   struct pan_image_slice_layout *slayout)
{
   /* Use explicit layout only when wsi_row_pitch_B is non-zero */
   const bool use_explicit_layout =
      layout_constraints && layout_constraints->wsi_row_pitch_B;
   struct pan_image_block_size afbc_tile_extent_px =
      pan_afbc_superblock_size(props->modifier);
   unsigned offset_align_mask =
      pan_afbc_header_align(PAN_ARCH, props->modifier) - 1;
   unsigned row_align_mask = pan_afbc_header_row_stride_align(
                                PAN_ARCH, props->format, props->modifier) -
                             1;
   struct pan_image_block_size afbc_tile_extent_el =
      pan_afbc_superblock_size_el(props->format, props->modifier);
   unsigned afbc_tile_payload_size_B =
      afbc_tile_extent_el.width * afbc_tile_extent_el.height *
      pan_format_get_plane_blocksize(props->format, plane_idx);

   struct pan_image_block_size align_px =
      pan_afbc_renderblock_size(props->modifier);

   /* If superblock tiling is used, align on a superblock tile. */
   if (props->modifier & AFBC_FORMAT_MOD_TILED) {
      align_px.width =
         ALIGN_POT(align_px.width, afbc_tile_extent_px.width *
                   pan_afbc_tile_size(props->format, props->modifier));
      align_px.height =
         ALIGN_POT(align_px.height, afbc_tile_extent_px.height *
                   pan_afbc_tile_size(props->format, props->modifier));
   }

   struct pan_image_extent aligned_extent_px = {
      .width = ALIGN_POT(mip_extent_px.width, align_px.width),
      .height = ALIGN_POT(mip_extent_px.height, align_px.height),
      .depth = mip_extent_px.depth,
   };

   if (use_explicit_layout) {
      unsigned afbc_tile_payload_row_stride_B =
         layout_constraints->wsi_row_pitch_B *
         pan_afbc_superblock_height(props->modifier);

      /* For quite some time, we've been accepting WSI row pitch that
       * didn't match exactly the image size and have been assuming tightly
       * packed tile rows instead of using the explicit stride in that case.
       * This is something we can't change without risking breaking existing
       * users, so we enforce this explicit tile alignment only if we were
       * asked to. */
      if (layout_constraints->strict &&
          (afbc_tile_payload_row_stride_B % afbc_tile_payload_size_B)) {
         mesa_loge("WSI pitch is not aligned on an AFBC tile");
         return false;
      }

      unsigned width_from_wsi_row_stride =
         (afbc_tile_payload_row_stride_B / afbc_tile_payload_size_B) *
         pan_afbc_superblock_width(props->modifier);

      if (width_from_wsi_row_stride < mip_extent_px.width) {
         mesa_loge("WSI pitch too small");
         return false;
      }

      slayout->afbc.header.row_stride_B =
         pan_afbc_row_stride(props->format, props->modifier, width_from_wsi_row_stride);
      if (slayout->afbc.header.row_stride_B & row_align_mask) {
         mesa_loge("WSI pitch not properly aligned");
         return false;
      }

      slayout->offset_B = layout_constraints->offset_B;
      if (slayout->offset_B & offset_align_mask) {
         mesa_loge("WSI offset not properly aligned");
         return false;
      }

      /* If this is not a strict import, ignore the WSI row pitch and use
       * the resource width to get the size. */
      if (!layout_constraints->strict) {
         slayout->afbc.header.row_stride_B = ALIGN_POT(
            pan_afbc_row_stride(props->format, props->modifier, aligned_extent_px.width),
            row_align_mask + 1);
      }
   } else {
      slayout->offset_B =
         ALIGN_POT(layout_constraints ? layout_constraints->offset_B : 0,
                   offset_align_mask + 1);
      slayout->afbc.header.row_stride_B = ALIGN_POT(
         pan_afbc_row_stride(props->format, props->modifier, aligned_extent_px.width),
         row_align_mask + 1);
   }

   const unsigned row_stride_sb = pan_afbc_stride_blocks(props->format,
                                                         props->modifier,
                                                         slayout->afbc.header.row_stride_B);
   const unsigned surface_stride_sb =
      row_stride_sb * (aligned_extent_px.height / afbc_tile_extent_px.height);

   uint64_t hdr_surf_size_B =
      (uint64_t)surface_stride_sb * AFBC_HEADER_BYTES_PER_TILE;
   uint64_t body_offset_B =
      pan_afbc_body_offset(PAN_ARCH, props->modifier, hdr_surf_size_B);
   uint64_t surf_stride_B =
      body_offset_B + ((uint64_t)surface_stride_sb * afbc_tile_payload_size_B);

   slayout->afbc.header.surface_size_B = hdr_surf_size_B;
   slayout->afbc.surface_stride_B = surf_stride_B;
   slayout->size_B = surf_stride_B * mip_extent_px.depth;

   if (hdr_surf_size_B > UINT32_MAX || surf_stride_B > UINT32_MAX ||
       slayout->size_B > UINT32_MAX)
      return false;

   return true;
}

static enum pan_mod_support
pan_mod_afbc_test_props(const struct pan_kmod_dev_props *dprops,
                        const struct pan_image_props *iprops,
                        const struct pan_image_usage *iusage)
{
   /* No image store. */
   if (iusage && iusage->bind & PAN_BIND_STORAGE_IMAGE)
      return PAN_MOD_NOT_SUPPORTED;

   /* AFBC not supported. */
   if (!pan_query_afbc(dprops))
      return PAN_MOD_NOT_SUPPORTED;

   unsigned plane_count = util_format_get_num_planes(iprops->format);
   const struct util_format_description *fdesc =
      util_format_description(iprops->format);

   /* Check if the format is supported first. */
   enum pan_afbc_mode plane_modes[3];
   for (unsigned p = 0; p < plane_count; p++) {
      plane_modes[p] = pan_afbc_format(PAN_ARCH, iprops->format, p);
      if (plane_modes[p] == PAN_AFBC_MODE_INVALID)
         return PAN_MOD_NOT_SUPPORTED;
   }

   /* AFBC can't do multisampling. */
   if (iprops->nr_samples > 1)
      return PAN_MOD_NOT_SUPPORTED;

   /* AFBC(2D) or AFBC(3D) on v7+ only. */
   if ((iprops->dim == MALI_TEXTURE_DIMENSION_3D && PAN_ARCH < 7) ||
       iprops->dim != MALI_TEXTURE_DIMENSION_2D)
      return PAN_MOD_NOT_SUPPORTED;

   /* ZS buffer descriptors can't pass split/wide/YTR modifiers. */
   if (iusage && (iusage->bind & PAN_BIND_DEPTH_STENCIL) &&
       (pan_afbc_superblock_width(iprops->modifier) != 16 ||
        (iprops->modifier & (AFBC_FORMAT_MOD_SPLIT | AFBC_FORMAT_MOD_YTR))))
      return PAN_MOD_NOT_SUPPORTED;

   /* YTR is only useful on RGB formats. */
   if ((iprops->modifier & AFBC_FORMAT_MOD_YTR) &&
       (pan_format_is_yuv(iprops->format) || fdesc->nr_channels < 3))
      return PAN_MOD_NOT_SUPPORTED;

   /* Make sure all planes support split mode. */
   if ((iprops->modifier & AFBC_FORMAT_MOD_SPLIT)) {
      for (unsigned p = 0; p < plane_count; p++) {
         if (!pan_afbc_can_split(PAN_ARCH, plane_modes[p], iprops->modifier))
            return PAN_MOD_NOT_SUPPORTED;
      }
   }

   /* Make sure tiled mode is supported. */
   if ((iprops->modifier & AFBC_FORMAT_MOD_TILED) &&
       !pan_afbc_can_tile(PAN_ARCH))
      return PAN_MOD_NOT_SUPPORTED;

   /* For one tile, AFBC is a loss compared to u-interleaved */
   if (iprops->extent_px.width <= 16 && iprops->extent_px.height <= 16)
      return PAN_MOD_NOT_OPTIMAL;

   /* Reserve 32x8 tiles for WSI images. */
   if (iusage && !iusage->wsi &&
       pan_afbc_superblock_width(iprops->modifier) != 16)
      return PAN_MOD_NOT_OPTIMAL;

   /* Prefer YTR when available. */
   if (pan_afbc_can_ytr(iprops->format) &&
       !(iprops->modifier & AFBC_FORMAT_MOD_YTR))
      return PAN_MOD_NOT_OPTIMAL;

   if (iprops->modifier & (AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC))
      return PAN_MOD_NOT_SUPPORTED;

   bool is_tiled = iprops->modifier & AFBC_FORMAT_MOD_TILED;
   bool can_tile = pan_afbc_can_tile(PAN_ARCH);

   if (is_tiled && !can_tile)
      return PAN_MOD_NOT_SUPPORTED;

   /* Packing/unpacking AFBC payload requires a COMPUTE job which we'd rather
    * avoid.
    */
   if (iusage &&
       (iusage->bind & (PAN_BIND_DEPTH_STENCIL | PAN_BIND_RENDER_TARGET)) &&
       !(iprops->modifier & AFBC_FORMAT_MOD_SPARSE))
      return PAN_MOD_NOT_OPTIMAL;

   return PAN_MOD_OPTIMAL;
}

#define pan_mod_afbc_emit_tex_payload_entry                                    \
   GENX(pan_tex_emit_afbc_payload_entry)
#define pan_mod_afbc_emit_color_attachment GENX(pan_emit_afbc_color_attachment)
#define pan_mod_afbc_emit_zs_attachment    GENX(pan_emit_afbc_zs_attachment)
#define pan_mod_afbc_emit_s_attachment     GENX(pan_emit_afbc_s_attachment)

#if PAN_ARCH >= 10
static bool
pan_mod_afrc_match(uint64_t mod)
{
   return drm_is_afrc(mod);
}

static enum pan_mod_support
pan_mod_afrc_test_props(const struct pan_kmod_dev_props *dprops,
                        const struct pan_image_props *iprops,
                        const struct pan_image_usage *iusage)
{
   /* AFRC not supported. */
   if (!pan_query_afrc(dprops))
      return PAN_MOD_NOT_SUPPORTED;

   /* Format not AFRC-able. */
   if (!pan_afrc_supports_format(iprops->format))
      return PAN_MOD_NOT_SUPPORTED;

   /* AFRC does not support layered multisampling. */
   if (iprops->nr_samples > 1)
      return PAN_MOD_NOT_SUPPORTED;

   /* No image store. */
   if (iusage && iusage->bind & PAN_BIND_STORAGE_IMAGE)
      return PAN_MOD_NOT_SUPPORTED;

   /* We can't write to an AFRC resource directly. */
   if (iusage && iusage->host_copy)
      return PAN_MOD_NOT_SUPPORTED;

   /* Host updates require an extra blit which we would rather avoid. */
   if (iusage && iusage->frequent_host_updates)
      return PAN_MOD_NOT_OPTIMAL;

   /* There's nothing preventing 1D AFRC, but it's pointless. */
   if (iprops->dim == MALI_TEXTURE_DIMENSION_1D)
      return PAN_MOD_NOT_OPTIMAL;

   return PAN_MOD_OPTIMAL;
}

static uint32_t
pan_mod_afrc_get_wsi_row_pitch(const struct pan_image *image,
                               unsigned plane_idx, unsigned mip_level)
{
   const struct pan_image_props *props = &image->props;
   const struct pan_image_layout *layout = &image->planes[plane_idx]->layout;
   const struct pan_image_block_size tile_extent_px =
      pan_afrc_tile_size(props->format, props->modifier);

   return layout->slices[mip_level].tiled_or_linear.row_stride_B /
          tile_extent_px.height;
}

#define pan_mod_afrc_init_plane_layout NULL

static bool
pan_mod_afrc_init_slice_layout(
   const struct pan_image_props *props, unsigned plane_idx,
   struct pan_image_extent mip_extent_px,
   const struct pan_image_layout_constraints *layout_constraints,
   struct pan_image_slice_layout *slayout)
{
   /* Use explicit layout only when wsi_row_pitch_B is non-zero */
   const bool use_explicit_layout =
      layout_constraints && layout_constraints->wsi_row_pitch_B;
   const unsigned align_mask =
      pan_afrc_buffer_alignment_from_modifier(props->modifier) - 1;
   struct pan_image_block_size tile_extent_px =
      pan_afrc_tile_size(props->format, props->modifier);
   struct pan_image_extent aligned_extent_px = {
      .width = ALIGN_POT(mip_extent_px.width, tile_extent_px.width),
      .height = ALIGN_POT(mip_extent_px.height, tile_extent_px.height),
      .depth = mip_extent_px.depth,
   };

   if (use_explicit_layout) {
      slayout->tiled_or_linear.row_stride_B =
         layout_constraints->wsi_row_pitch_B * tile_extent_px.height;
      if (slayout->tiled_or_linear.row_stride_B & align_mask) {
         mesa_loge("WSI pitch not properly aligned");
         return false;
      }

      slayout->offset_B = layout_constraints->offset_B;
      if (slayout->offset_B & align_mask) {
         mesa_loge("WSI offset not properly aligned");
         return false;
      }

      unsigned afrc_blk_size_B =
         pan_afrc_block_size_from_modifier(props->modifier) *
         AFRC_CLUMPS_PER_TILE;
      unsigned width_from_wsi_row_stride =
         (slayout->tiled_or_linear.row_stride_B / afrc_blk_size_B) *
         tile_extent_px.width;

      if (width_from_wsi_row_stride < mip_extent_px.width) {
         mesa_loge("WSI pitch too small");
         return false;
      }

      /* If this is not a strict import, ignore the WSI row pitch and use
       * the resource width to get the size. */
      if (!layout_constraints->strict) {
         slayout->tiled_or_linear.row_stride_B = pan_afrc_row_stride(
            props->format, props->modifier, mip_extent_px.width);
      }
   } else {
      /* Align levels to cache-line as a performance improvement for
       * linear/tiled and as a requirement for AFBC */
      slayout->offset_B = ALIGN_POT(
         layout_constraints ? layout_constraints->offset_B : 0, align_mask + 1);
      slayout->tiled_or_linear.row_stride_B =
         ALIGN_POT(pan_afrc_row_stride(props->format, props->modifier,
                                       mip_extent_px.width),
                   align_mask + 1);
   }

   uint64_t surf_stride_B =
      (uint64_t)slayout->tiled_or_linear.row_stride_B *
      DIV_ROUND_UP(aligned_extent_px.height, aligned_extent_px.height);

   slayout->tiled_or_linear.surface_stride_B = surf_stride_B;
   slayout->size_B =
      surf_stride_B * aligned_extent_px.depth * props->nr_samples;

   /* Make sure the stride/size fits in the descriptor fields. */
   if (slayout->size_B > MAX_SIZE_B ||
       slayout->tiled_or_linear.surface_stride_B > MAX_SLICE_STRIDE_B)
      return false;

   return true;
}

#define pan_mod_afrc_emit_tex_payload_entry                                    \
   GENX(pan_tex_emit_afrc_payload_entry)
#define pan_mod_afrc_emit_color_attachment GENX(pan_emit_afrc_color_attachment)
#define pan_mod_afrc_emit_zs_attachment    NULL
#define pan_mod_afrc_emit_s_attachment     NULL
#endif

static bool
pan_mod_u_tiled_match(uint64_t mod)
{
   return mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED;
}

static enum pan_mod_support
pan_mod_u_tiled_test_props(const struct pan_kmod_dev_props *dprops,
                           const struct pan_image_props *iprops,
                           const struct pan_image_usage *iusage)
{
   assert(GENX(pan_format_from_pipe_format)(iprops->format)->hw);

   /* YUV not supported. */
   if (pan_format_is_yuv(iprops->format))
      return PAN_MOD_NOT_SUPPORTED;

   /* The purpose of tiling is improving locality in both X- and
    * Y-directions. If there is only a single pixel in either direction,
    * tiling does not make sense; using a linear layout instead is optimal
    * for both memory usage and performance.
    */
   if (MIN2(iprops->extent_px.width, iprops->extent_px.height) < 2)
      return PAN_MOD_NOT_OPTIMAL;

   return PAN_MOD_OPTIMAL;
}

static uint32_t
pan_mod_u_tiled_get_wsi_row_pitch(const struct pan_image *image,
                                  unsigned plane_idx, unsigned mip_level)
{
   const struct pan_image_props *props = &image->props;
   const struct pan_image_layout *layout = &image->planes[plane_idx]->layout;

   return layout->slices[mip_level].tiled_or_linear.row_stride_B /
          pan_u_interleaved_tile_size_el(props->format).height;
}

#define pan_mod_u_tiled_init_plane_layout NULL

static bool
pan_mod_u_tiled_init_slice_layout(
   const struct pan_image_props *props, unsigned plane_idx,
   struct pan_image_extent mip_extent_px,
   const struct pan_image_layout_constraints *layout_constraints,
   struct pan_image_slice_layout *slayout)
{
   /* Use explicit layout only when wsi_row_pitch_B is non-zero */
   const bool use_explicit_layout =
      layout_constraints && layout_constraints->wsi_row_pitch_B;
   unsigned align_mask =
      pan_linear_or_tiled_row_align_req(PAN_ARCH, props->format, plane_idx) - 1;
   struct pan_image_block_size tile_extent_el =
      pan_u_interleaved_tile_size_el(props->format);
   struct pan_image_extent mip_extent_el;
   unsigned tile_size_B;

   if (util_format_is_compressed(props->format)) {
      assert(util_format_get_num_planes(props->format) == 1);
      mip_extent_el.width = DIV_ROUND_UP(
         mip_extent_px.width, util_format_get_blockwidth(props->format));
      mip_extent_el.height = DIV_ROUND_UP(
         mip_extent_px.height, util_format_get_blockheight(props->format));
      mip_extent_el.depth = DIV_ROUND_UP(
         mip_extent_px.depth, util_format_get_blockdepth(props->format));
      tile_size_B = tile_extent_el.width * tile_extent_el.height *
                    pan_format_get_plane_blocksize(props->format, plane_idx);
   } else {
      /* Block-based YUV needs special care, because the U-tile extent
       * is in pixels, not blocks in that case. */
      assert(tile_extent_el.width % util_format_get_blockwidth(props->format) ==
             0);
      assert(tile_extent_el.height %
                util_format_get_blockheight(props->format) ==
             0);
      mip_extent_el = mip_extent_px;
      tile_size_B =
         (tile_extent_el.width / util_format_get_blockwidth(props->format)) *
         (tile_extent_el.height / util_format_get_blockheight(props->format)) *
         pan_format_get_plane_blocksize(props->format, plane_idx);
   }

   if (use_explicit_layout) {
      slayout->tiled_or_linear.row_stride_B =
         layout_constraints->wsi_row_pitch_B * tile_extent_el.height;
      if (slayout->tiled_or_linear.row_stride_B & align_mask) {
         mesa_loge("WSI pitch not properly aligned");
         return false;
      }

      const unsigned width_from_wsi_row_stride =
         (slayout->tiled_or_linear.row_stride_B / tile_size_B) *
         tile_extent_el.width;

      if (width_from_wsi_row_stride < mip_extent_el.width) {
         mesa_loge("WSI pitch too small");
         return false;
      }

      slayout->offset_B = layout_constraints->offset_B;
      if (slayout->offset_B & align_mask) {
         mesa_loge("WSI offset not properly aligned");
         return false;
      }
   } else {
      /* When we can decide of the layout, we want things aligned on at least a
       * cacheline for performance reasons. */
      align_mask = MAX2(align_mask, 63);
      slayout->offset_B = ALIGN_POT(
         layout_constraints ? layout_constraints->offset_B : 0,
         MAX2(align_mask + 1, pan_image_slice_align(props->modifier)));
      slayout->tiled_or_linear.row_stride_B = ALIGN_POT(
         tile_size_B * DIV_ROUND_UP(mip_extent_el.width, tile_extent_el.width),
         align_mask + 1);
   }

   if (layout_constraints && layout_constraints->u_tiled.row_align_B) {
      slayout->tiled_or_linear.row_stride_B =
         ALIGN_POT(slayout->tiled_or_linear.row_stride_B,
                   layout_constraints->u_tiled.row_align_B);
   }

   uint64_t surf_stride_B =
      (uint64_t)slayout->tiled_or_linear.row_stride_B *
      DIV_ROUND_UP(mip_extent_el.height, tile_extent_el.height);
   surf_stride_B = ALIGN_POT(surf_stride_B, (uint64_t)align_mask + 1);

   slayout->tiled_or_linear.surface_stride_B = surf_stride_B;
   slayout->size_B = surf_stride_B * mip_extent_el.depth * props->nr_samples;

   /* Make sure the stride/size fits in the descriptor fields. */
   if (slayout->size_B > MAX_SIZE_B ||
       slayout->tiled_or_linear.surface_stride_B > MAX_SLICE_STRIDE_B)
      return false;

   return true;
}

#define pan_mod_u_tiled_emit_tex_payload_entry                                 \
   GENX(pan_tex_emit_u_tiled_payload_entry)
#define pan_mod_u_tiled_emit_color_attachment                                  \
   GENX(pan_emit_u_tiled_color_attachment)
#define pan_mod_u_tiled_emit_zs_attachment GENX(pan_emit_u_tiled_zs_attachment)
#define pan_mod_u_tiled_emit_s_attachment  GENX(pan_emit_u_tiled_s_attachment)

static bool
pan_mod_linear_match(uint64_t mod)
{
   return mod == DRM_FORMAT_MOD_LINEAR;
}

static enum pan_mod_support
pan_mod_linear_test_props(const struct pan_kmod_dev_props *dprops,
                          const struct pan_image_props *iprops,
                          const struct pan_image_usage *iusage)
{
   assert(GENX(pan_format_from_pipe_format)(iprops->format)->hw);

   switch (iprops->format) {
   /* AFBC-only formats. */
   case PIPE_FORMAT_R8G8B8_420_UNORM_PACKED:
   case PIPE_FORMAT_R10G10B10_420_UNORM_PACKED:
   case PIPE_FORMAT_X6R10X6G10_X6R10X6B10_422_UNORM:
      return PAN_MOD_NOT_SUPPORTED;
   default:
      /* We assume that all "better" mods have been tested before linear, and
       * declare it as optimal so it's always picked when tested, unless it's
       * not supported.
       */
      return PAN_MOD_OPTIMAL;
   }
}

static uint32_t
pan_mod_linear_get_wsi_row_pitch(const struct pan_image *image,
                                 unsigned plane_idx, unsigned mip_level)
{
   const struct pan_image_layout *layout = &image->planes[plane_idx]->layout;

   return layout->slices[mip_level].tiled_or_linear.row_stride_B;
}

#define pan_mod_linear_init_plane_layout NULL

static bool
pan_mod_linear_init_slice_layout(
   const struct pan_image_props *props, unsigned plane_idx,
   struct pan_image_extent mip_extent_px,
   const struct pan_image_layout_constraints *layout_constraints,
   struct pan_image_slice_layout *slayout)
{
   /* Use explicit layout only when wsi_row_pitch_B is non-zero */
   const bool use_explicit_layout =
      layout_constraints && layout_constraints->wsi_row_pitch_B;
   unsigned align_mask =
      pan_linear_or_tiled_row_align_req(PAN_ARCH, props->format, plane_idx) - 1;
   const unsigned fmt_blksize_B =
      pan_format_get_plane_blocksize(props->format, plane_idx);
   struct pan_image_extent mip_extent_el;

   if (util_format_is_compressed(props->format)) {
      assert(util_format_get_num_planes(props->format) == 1);
      mip_extent_el.width = DIV_ROUND_UP(
         mip_extent_px.width, util_format_get_blockwidth(props->format));
      mip_extent_el.height = DIV_ROUND_UP(
         mip_extent_px.height, util_format_get_blockheight(props->format));
      mip_extent_el.depth = DIV_ROUND_UP(
         mip_extent_px.depth, util_format_get_blockdepth(props->format));
   } else {
      mip_extent_el = mip_extent_px;
   }

   if (use_explicit_layout) {
      unsigned width_from_wsi_row_stride =
         layout_constraints->wsi_row_pitch_B / fmt_blksize_B;

      if (!util_format_is_compressed(props->format))
         width_from_wsi_row_stride *= util_format_get_blockwidth(props->format);

      if (width_from_wsi_row_stride < mip_extent_el.width) {
         mesa_loge("WSI pitch too small");
         return false;
      }

      slayout->tiled_or_linear.row_stride_B =
         layout_constraints->wsi_row_pitch_B;
      if (slayout->tiled_or_linear.row_stride_B & align_mask) {
         mesa_loge("WSI pitch not properly aligned");
         return false;
      }

      slayout->offset_B = layout_constraints->offset_B;
      if (slayout->offset_B & align_mask) {
         mesa_loge("WSI offset not properly aligned");
         return false;
      }
   } else {
      /* When we can decide of the layout, we want things aligned on at least a
       * cacheline for performance reasons. */
      align_mask = MAX2(align_mask, 63);
      slayout->offset_B = ALIGN_POT(
         layout_constraints ? layout_constraints->offset_B : 0,
         MAX2(align_mask + 1, pan_image_slice_align(props->modifier)));
      slayout->tiled_or_linear.row_stride_B =
         ALIGN_POT(mip_extent_el.width * fmt_blksize_B, align_mask + 1);
   }

   uint64_t surf_stride_B =
      (uint64_t)slayout->tiled_or_linear.row_stride_B * mip_extent_el.height;
   surf_stride_B = ALIGN_POT(surf_stride_B, (uint64_t)align_mask + 1);

   /* Surface stride is passed as a 32-bit unsigned integer to RT/ZS and texture
    * descriptors, make sure it fits. */
   if (surf_stride_B > UINT32_MAX)
      return false;

   slayout->tiled_or_linear.surface_stride_B = surf_stride_B;
   slayout->size_B = surf_stride_B * mip_extent_el.depth * props->nr_samples;
   return true;
}

#define pan_mod_linear_emit_tex_payload_entry                                  \
   GENX(pan_tex_emit_linear_payload_entry)
#define pan_mod_linear_emit_color_attachment                                   \
   GENX(pan_emit_linear_color_attachment)
#define pan_mod_linear_emit_zs_attachment GENX(pan_emit_linear_zs_attachment)
#define pan_mod_linear_emit_s_attachment  GENX(pan_emit_linear_s_attachment)

#if PAN_ARCH >= 5
#define EMIT_ATT(__name)                                                       \
   .emit_color_attachment = pan_mod_##__name##_emit_color_attachment,          \
   .emit_zs_attachment = pan_mod_##__name##_emit_zs_attachment,                \
   .emit_s_attachment = pan_mod_##__name##_emit_s_attachment
#else
#define EMIT_ATT(__name)                                                       \
   .emit_color_attachment = NULL, .emit_zs_attachment = NULL,                  \
   .emit_s_attachment = NULL
#endif

#define PAN_MOD_DEF(__name)                                                    \
   {                                                                           \
      .match = pan_mod_##__name##_match,                                       \
      .test_props = pan_mod_##__name##_test_props,                             \
      .get_wsi_row_pitch = pan_mod_##__name##_get_wsi_row_pitch,               \
      .init_plane_layout = pan_mod_##__name##_init_plane_layout,               \
      .init_slice_layout = pan_mod_##__name##_init_slice_layout,               \
      .emit_tex_payload_entry = pan_mod_##__name##_emit_tex_payload_entry,     \
      EMIT_ATT(__name),                                                        \
   }

static const struct pan_mod_handler pan_mod_handlers[] = {
   PAN_MOD_DEF(afbc),
   PAN_MOD_DEF(u_tiled),
   PAN_MOD_DEF(linear),
#if PAN_ARCH >= 10
   PAN_MOD_DEF(afrc),
#endif
};

const struct pan_mod_handler *
GENX(pan_mod_get_handler)(uint64_t modifier)
{
   for (unsigned i = 0; i < ARRAY_SIZE(pan_mod_handlers); i++) {
      if (pan_mod_handlers[i].match(modifier))
         return &pan_mod_handlers[i];
   }

   return NULL;
}
