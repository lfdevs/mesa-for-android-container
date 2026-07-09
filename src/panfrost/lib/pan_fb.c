/*
 * Copyright (C) 2026 Collabora, Ltd.
 * Copyright (C) 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "pan_fb.h"

#include "pan_afbc.h"
#include "pan_afrc.h"
#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_format.h"
#include "pan_image.h"
#include "pan_props.h"
#include "pan_util.h"

static unsigned
pan_bytes_per_pixel_tib(enum pipe_format format)
{
   const struct pan_blendable_format *bf =
      GENX(pan_blendable_format_from_pipe_format)(format);
   return pan_format_tib_size(format, bf->internal);
}

void
GENX(pan_select_fb_tile_size)(struct pan_fb_layout *fb)
{
   uint32_t rt_B_per_sa = 0;
   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      enum pipe_format format = fb->rt_formats[rt];
      if (format == PIPE_FORMAT_NONE)
         continue;

      const struct pan_blendable_format *bf =
         GENX(pan_blendable_format_from_pipe_format)(format);
      rt_B_per_sa += pan_format_tib_size(format, bf->internal);
   }

   /* The PLS area overlaps with the color targets */
   rt_B_per_sa = MAX2(rt_B_per_sa, fb->pls_size_B);

   const uint32_t rt_B_per_px = rt_B_per_sa * fb->sample_count;

   /* We always have depth and it's is always stored in a 32-bit float.
    * Stencil requires depth to be allocated, but doesn't have it's own
    * budget; it's tied to the depth buffer.
    */
   const uint32_t z_B_per_px = sizeof(float) * fb->sample_count;

   fb->tile_size_px =
      MIN2(fb->tile_rt_budget_B >> util_logbase2_ceil(rt_B_per_px),
           fb->tile_z_budget_B >> util_logbase2_ceil(z_B_per_px));

   /* Check if we're using too much tile-memory; if we are, try disabling
    * pipelining. This works because we're starting with an optimistic half
    * of the tile-budget, so we actually have another half that can be used.
    *
    * On v6 GPUs, doing this is not allowed; they *have* to pipeline.
    */
    if (PAN_ARCH != 6 && fb->tile_size_px < 4 * 4)
       fb->tile_size_px *= 2;

   /* Clamp tile size to hardware limits */
   fb->tile_size_px =
      MIN2(fb->tile_size_px, pan_max_effective_tile_size(PAN_ARCH));
   assert(util_is_power_of_two_nonzero(fb->tile_size_px));
   assert(fb->tile_size_px >= 4 * 4);

   /* Colour buffer allocations must be 1K aligned. */
   fb->tile_rt_alloc_B = ALIGN_POT(rt_B_per_px * fb->tile_size_px, 1024);

#if PAN_ARCH == 6
   assert(fb->tile_rt_alloc_B <= fb->tile_rt_budget_B && "tile too big");
#else
   assert(fb->tile_rt_alloc_B <= fb->tile_rt_budget_B * 2 && "tile too big");
#endif
}

static void
align_fb_tiling_area_for_image_plane(struct pan_fb_layout *fb,
                                     struct pan_image_plane_ref pref)
{
   if (!pref.image)
      return;

   struct pan_image_block_size block_size;
   if (drm_is_afbc(pref.image->props.modifier)) {
      /* For AFBC render targets, the hardware always writes full superblocks.
       * In order to ensure we don't write garbage, we need to expand the
       * render area accordingly and load the border pixels.
       */
      block_size = pan_afbc_renderblock_size(pref.image->props.modifier);
      assert(block_size.width >= 16 && block_size.height >= 16);
   } else if (drm_is_afrc(pref.image->props.modifier)) {
      /* For AFRC render targets, the hardware always writes full clumps.  In
       * order to ensure we don't write garbage, we need to expand the render
       * area accordingly and load the border pixels.
       */
      bool scan = pan_afrc_is_scan(pref.image->props.modifier);
      block_size = pan_afrc_clump_size(pref.image->props.format, scan);
   } else {
      /* No alignment requirements */
      return;
   }

   fb->tiling_area_px = pan_fb_bbox_align(fb->tiling_area_px,
                                          block_size.width,
                                          block_size.height);
}

void
GENX(pan_align_fb_tiling_area)(struct pan_fb_layout *fb,
                               const struct pan_fb_store *store)
{
   if (store == NULL)
      return;

   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      if (store->rts[rt].store) {
         align_fb_tiling_area_for_image_plane(fb,
            pan_image_view_get_color_plane(store->rts[rt].iview));
      }
   }

   if (store->zs.store) {
      align_fb_tiling_area_for_image_plane(fb,
         pan_image_view_get_zs_plane(store->zs.iview));
   }

   if (store->s.store) {
      align_fb_tiling_area_for_image_plane(fb,
         pan_image_view_get_s_plane(store->s.iview));
   }
}

static void
fold_sample_0_image_load(struct pan_fb_resolve_op_msaa *rm,
                         const struct pan_image_view *iview,
                         uint8_t fb_sample_count)
{
   if (rm->resolve != PAN_FB_RESOLVE_IMAGE)
      return;

   if (rm->msaa != PAN_FB_MSAA_COPY_ALL)
      return;

   assert(pan_image_view_get_nr_samples(iview) == fb_sample_count);

   /* If we're loading all samples of the image but only storing SAMPLE_0,
    * then we don't actually need all samples in the image.  Drop it to a
    * SAMPLE_0 load to save bandwidth.
    */
   rm->msaa = PAN_FB_MSAA_COPY_SAMPLE_0;
}

static bool
resolve_msaa_can_fold(struct pan_fb_resolve_op_msaa rm,
                      enum pan_fb_resolve_op self_op,
                      enum pipe_format format)
{
   if (rm.resolve == PAN_FB_RESOLVE_IMAGE) {
      /* We already folded away COPY_ALL and all the other MSAA modes produce
       * a single per-pixel result across all the samples, i.e. COPY_IDENTICAL.
       * We can fold that with any resolve mode.
       */
      assert(rm.msaa != PAN_FB_MSAA_COPY_ALL);
      return true;
   }

   if (rm.resolve != self_op)
      return false;

   /* PAN_FB_MSAA_COPY_IDENTICAL is always foldable */
   if (rm.msaa == PAN_FB_MSAA_COPY_IDENTICAL)
      return true;

   if (self_op == PAN_FB_RESOLVE_Z || self_op == PAN_FB_RESOLVE_S) {
      /* Z/S only supports SAMPLE_0 */
      return rm.msaa == PAN_FB_MSAA_COPY_SAMPLE_0;
   } else {
      if (rm.msaa == PAN_FB_MSAA_COPY_AVERAGE &&
          !GENX(pan_format_supports_msaa_average)(format))
         return false;

      /* The color hardware supports SAMPLE_0 and AVERAGE */
      return rm.msaa == PAN_FB_MSAA_COPY_SAMPLE_0 ||
             rm.msaa == PAN_FB_MSAA_COPY_AVERAGE;
   }
}

static void
fold_resolve_into_store_target(enum pipe_format format,
                               uint8_t fb_sample_count,
                               enum pan_fb_resolve_op self_op,
                               struct pan_fb_resolve_target *resolve,
                               struct pan_fb_store_target *store)
{
   if (format == PIPE_FORMAT_NONE)
      return;

   /* We need SAMPLE_0 if we're going to fold in the resolve */
   if (store->msaa != PAN_FB_MSAA_COPY_SAMPLE_0)
      return;

   fold_sample_0_image_load(&resolve->in_bounds, resolve->iview,
                            fb_sample_count);
   fold_sample_0_image_load(&resolve->in_bounds, resolve->iview,
                            fb_sample_count);

   if (!resolve_msaa_can_fold(resolve->in_bounds, self_op, format) ||
       !resolve_msaa_can_fold(resolve->border, self_op, format))
      return;

   enum pan_fb_msaa_copy_op msaa;
   if (resolve->in_bounds.resolve == PAN_FB_RESOLVE_IMAGE) {
      /* If they're both IMAGE, there's nothing to fold */
      if (resolve->border.resolve == PAN_FB_RESOLVE_IMAGE)
         return;
      msaa = resolve->border.msaa;
   } else if (resolve->border.resolve == PAN_FB_RESOLVE_IMAGE) {
      msaa = resolve->in_bounds.msaa;
   } else {
      /* If neither is an image resolve, the MSAA copy op has to match or one
       * of them has to be PAN_FB_MSAA_COPY_IDENTICAL.
       */
      if (resolve->in_bounds.msaa != PAN_FB_MSAA_COPY_IDENTICAL) {
         if (resolve->border.msaa != PAN_FB_MSAA_COPY_IDENTICAL &&
             resolve->border.msaa != resolve->in_bounds.msaa)
            return;
         msaa = resolve->in_bounds.msaa;
      } else {
         msaa = resolve->border.msaa;
      }
   }

   /* PAN_FB_MSAA_COPY_IDENTICAL isn't real. Use SAMPLE_0 */
   if (msaa == PAN_FB_MSAA_COPY_IDENTICAL)
      msaa = PAN_FB_MSAA_COPY_SAMPLE_0;

   /* At this point, we know we can fold the resolve */
   store->msaa = msaa;

   if (resolve->in_bounds.resolve != PAN_FB_RESOLVE_IMAGE) {
      resolve->in_bounds.resolve = PAN_FB_RESOLVE_NONE;
      resolve->in_bounds.msaa = PAN_FB_MSAA_COPY_ALL;
   }

   if (resolve->border.resolve != PAN_FB_RESOLVE_IMAGE) {
      resolve->border.resolve = PAN_FB_RESOLVE_NONE;
      resolve->border.msaa = PAN_FB_MSAA_COPY_ALL;
   }
}

void
GENX(pan_fb_fold_resolve_into_store)(const struct pan_fb_layout *fb,
                                     struct pan_fb_resolve *resolve,
                                     struct pan_fb_store *store)
{
   if (fb->sample_count == 1)
      return;

   fold_resolve_into_store_target(fb->z_format, fb->sample_count,
                                  PAN_FB_RESOLVE_Z, &resolve->z, &store->zs);
   fold_resolve_into_store_target(fb->s_format, fb->sample_count,
                                  PAN_FB_RESOLVE_S, &resolve->s, &store->s);
   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      fold_resolve_into_store_target(fb->rt_formats[rt], fb->sample_count,
                                     PAN_FB_RESOLVE_RT(rt),
                                     &resolve->rts[rt], &store->rts[rt]);
   }
}

void
GENX(pan_fill_fb_info)(const struct pan_fb_desc_info *info,
                       struct pan_fb_info *fbinfo)
{
   struct pan_bbox bbox = { 0, 0, 0, 0 };
   if (info->fb->width_px > 0 && info->fb->width_px > 0) {
      const struct pan_fb_bbox fb_area_px =
         pan_fb_bbox_from_xywh(0, 0, info->fb->width_px, info->fb->height_px);

      assert(pan_fb_bbox_is_valid(info->fb->tiling_area_px));
      const struct pan_fb_bbox bbox_px =
         pan_fb_bbox_clamp(info->fb->tiling_area_px, fb_area_px);

      bbox = (struct pan_bbox) {
         .minx = bbox_px.min_x,
         .miny = bbox_px.min_y,
         .maxx = bbox_px.max_x,
         .maxy = bbox_px.max_y,
      };
   }

   *fbinfo = (struct pan_fb_info) {
      .width = info->fb->width_px,
      .height = info->fb->height_px,
      .draw_extent = bbox,
      .frame_bounding_box = bbox,
      .nr_samples = info->fb->sample_count,
      .rt_count = info->fb->rt_count,
      .pls_enabled = info->fb->pls_size_B > 0,
      .bifrost.pre_post = {
         .dcds.gpu = info->frame_shaders.dcd_pointer,
         .modes = {
            info->frame_shaders.modes[0],
            info->frame_shaders.modes[1],
            info->frame_shaders.modes[2],
         },
      },

      .tile_buf_budget = info->fb->tile_rt_budget_B,
      .z_tile_buf_budget = info->fb->tile_z_budget_B,
      .tile_size = info->fb->tile_size_px,
      .cbuf_allocation = info->fb->tile_rt_alloc_B,

      .sample_positions = info->sample_pos_array_pointer,
      .sprite_coord_origin = info->sprite_coord_origin_max_y,
      .first_provoking_vertex = info->provoking_vertex_first,
      .allow_hsr_prepass = info->provoking_vertex_first,
   };

   /* There are cases where we only want to fill out a partial fb_info */
   if (info->load == NULL && info->store == NULL)
      return;

   for (unsigned rt = 0; rt < info->fb->rt_count; rt++) {
      fbinfo->rts[rt] = (struct pan_fb_color_attachment) {
         .view = info->store->rts[rt].iview,
         .clear = info->load->rts[rt].border_load == PAN_FB_LOAD_CLEAR ||
                  info->load->rts[rt].in_bounds_load == PAN_FB_LOAD_CLEAR,
         .preload = info->load->rts[rt].in_bounds_load == PAN_FB_LOAD_IMAGE,
         .discard = !info->store->rts[rt].store,
      };

      if (fbinfo->rts[rt].clear) {
         pan_pack_color(GENX(pan_blendable_formats),
                        fbinfo->rts[rt].clear_value,
                        &info->load->rts[rt].clear.color,
                        info->fb->rt_formats[rt],
                        false /* dithered */);
      }
   }

   if (info->fb->z_format != PIPE_FORMAT_NONE) {
      fbinfo->zs.view.zs = info->store->zs.iview;
      fbinfo->zs.clear.z = info->load->z.border_load == PAN_FB_LOAD_CLEAR ||
                           info->load->z.in_bounds_load == PAN_FB_LOAD_CLEAR;
      fbinfo->zs.preload.z = info->load->z.in_bounds_load == PAN_FB_LOAD_IMAGE;
      fbinfo->zs.discard.z = !info->store->zs.store;
      if (fbinfo->zs.clear.z)
         fbinfo->zs.clear_value.depth = info->load->z.clear.depth;
   }

   if (info->fb->s_format != PIPE_FORMAT_NONE) {
      fbinfo->zs.view.s = info->store->s.iview;
      fbinfo->zs.clear.s = info->load->s.border_load == PAN_FB_LOAD_CLEAR ||
                           info->load->s.in_bounds_load == PAN_FB_LOAD_CLEAR;
      fbinfo->zs.preload.s = info->load->s.in_bounds_load == PAN_FB_LOAD_IMAGE;
      fbinfo->zs.discard.s = !info->store->s.store;
      if (fbinfo->zs.clear.s)
         fbinfo->zs.clear_value.stencil = info->load->s.clear.stencil;
   }
}

#if PAN_ARCH >= 5
static enum mali_msaa
translate_msaa_copy_op(const struct pan_fb_layout *fb,
                       const struct pan_image_view *iview,
                       enum pan_fb_msaa_copy_op msaa)
{
   switch (msaa) {
   case PAN_FB_MSAA_COPY_ALL:
      assert(pan_image_view_get_nr_samples(iview) == fb->sample_count);
      if (fb->sample_count > 1)
         return MALI_MSAA_LAYERED;
      else
         return MALI_MSAA_SINGLE;

   case PAN_FB_MSAA_COPY_SINGLE:
   case PAN_FB_MSAA_COPY_SAMPLE_0:
      return MALI_MSAA_SINGLE;

   case PAN_FB_MSAA_COPY_AVERAGE:
      /* It must be a blendable format or the hardware can't average */
      assert(GENX(pan_blendable_format_from_pipe_format)(iview->format)->internal);
      if (fb->sample_count > 1)
         return MALI_MSAA_AVERAGE;
      else
         return MALI_MSAA_SINGLE;

   default:
      UNREACHABLE("Unsupported MSAA copy op");
   }
}

static bool
pan_fb_load_target_always(const struct pan_fb_load_target *target)
{
   if (target->in_bounds_load == PAN_FB_LOAD_NONE &&
       target->border_load == PAN_FB_LOAD_NONE)
      return false;

   return target->always;
}

static bool
pan_fb_store_target_always(const struct pan_fb_store_target *target)
{
   return target->store && target->always;
}

struct pan_fb_clean_tile
GENX(pan_fb_get_clean_tile)(const struct pan_fb_desc_info *info)
{
   const struct pan_fb_layout *fb = info->fb;
   const struct pan_fb_load *load = info->load;
   const struct pan_fb_store *store = info->store;

   struct pan_fb_clean_tile ct = { };

   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      if (fb->rt_formats[rt] == PIPE_FORMAT_NONE)
         continue;

      if ((load && pan_fb_load_target_always(&load->rts[rt])) ||
          (store && pan_fb_store_target_always(&store->rts[rt])))
         ct.rts |= BITFIELD_BIT(rt);

      if (store && store->rts[rt].store) {
         const struct pan_image *img =
            pan_image_view_get_color_plane(store->rts[rt].iview).image;
         if (GENX(pan_force_clean_write_on)(img, fb->tile_size_px))
            ct.rts |= BITFIELD_BIT(rt);
      }
   }

   const bool z_always_load = load && pan_fb_load_target_always(&load->z);
   const bool s_always_load = load && pan_fb_load_target_always(&load->s);
   const bool zs_always_store = store && pan_fb_store_target_always(&store->zs);
   const bool s_always_store = store && pan_fb_store_target_always(&store->s);

   if (fb->z_format != PIPE_FORMAT_NONE) {
      ct.zs = z_always_load || zs_always_store;

      if (store && store->zs.store) {
         const struct pan_image *img =
            pan_image_view_get_zs_plane(store->zs.iview).image;
         assert(util_format_get_depth_bits(img->props.format) ==
                util_format_get_depth_bits(fb->z_format));
         assert(util_format_get_depth_bits(store->zs.iview->format) ==
                util_format_get_depth_bits(fb->z_format));
         const struct util_format_description *zs_fmt_desc =
            util_format_description(img->props.format);

         /* If ZS writes stencil, we have to also include stencil loads */
         if (util_format_has_stencil(zs_fmt_desc) && s_always_load)
            ct.zs = true;

         if (GENX(pan_force_clean_write_on)(img, fb->tile_size_px))
            ct.zs = true;
      }
   }

   if (fb->s_format != PIPE_FORMAT_NONE) {
      ct.s = s_always_load || s_always_store;

      if (store && store->s.store) {
         const struct pan_image *img =
            pan_image_view_get_s_plane(store->s.iview).image;
         if (GENX(pan_force_clean_write_on)(img, fb->tile_size_px))
            ct.s = true;
      }
   }

   return ct;
}

static void
emit_zs_crc_desc(const struct pan_fb_desc_info *info,
                 const struct pan_fb_clean_tile ct,
                 struct mali_zs_crc_extension_packed *zs_crc)
{
   const struct pan_fb_layout *fb = info->fb;
   const struct pan_fb_store *store = info->store;

   pan_pack(zs_crc, ZS_CRC_EXTENSION, cfg) {
      if (store && store->zs.store) {
         cfg.zs.msaa = translate_msaa_copy_op(fb, store->zs.iview,
                                              store->zs.msaa);

#if PAN_ARCH >= 6
         cfg.zs.clean_tile_write_enable = ct.zs;
#else
         cfg.zs.clean_pixel_write_enable = ct.zs;
#endif

      }

      if (store && store->s.store) {
         cfg.s.msaa = translate_msaa_copy_op(fb, store->s.iview,
                                             store->s.msaa);

#if PAN_ARCH >= 6
         cfg.s.clean_tile_write_enable = ct.s;
#else
         cfg.s.clean_pixel_write_enable = ct.s;
#endif
      }

      /* TODO CRC */
   }

   if (store && store->zs.store) {
      const struct pan_image_view *iview = store->zs.iview;
      const struct pan_mod_handler *mod_handler =
         pan_image_view_get_zs_plane(iview).image->mod_handler;

      assert(info->layer < pan_image_view_layer_or_3d_slice_count(iview));
      const struct pan_attachment_info att = {
         .iview = iview,
         .layer_or_z_slice = iview->first_layer_or_z_slice + info->layer,
         .fb_tile_size_px = fb->tile_size_px,
      };

      struct mali_zs_crc_extension_packed zs_part;
      mod_handler->emit_zs_attachment(&att, &zs_part);
      pan_merge(zs_crc, &zs_part, ZS_CRC_EXTENSION);
   }

   if (store && store->s.store) {
      const struct pan_image_view *iview = store->s.iview;
      const struct pan_mod_handler *mod_handler =
         pan_image_view_get_s_plane(iview).image->mod_handler;

      assert(info->layer < pan_image_view_layer_or_3d_slice_count(iview));
      const struct pan_attachment_info att = {
         .iview = iview,
         .layer_or_z_slice = iview->first_layer_or_z_slice + info->layer,
         .fb_tile_size_px = fb->tile_size_px,
      };

      struct mali_zs_crc_extension_packed s_part;
      mod_handler->emit_s_attachment(&att, &s_part);
      pan_merge(zs_crc, &s_part, ZS_CRC_EXTENSION);
   }

   /* TODO: CRC */
}

static void
emit_rgb_rt_desc(const struct pan_fb_desc_info *info,
                 const struct pan_fb_clean_tile ct,
                 unsigned rt, uint32_t tile_rt_offset_B,
                 struct mali_rgb_render_target_packed *rgb_rt)
{
   const struct pan_fb_layout *fb = info->fb;
   const struct pan_fb_load *load = info->load;
   const struct pan_fb_store *store = info->store;

   if (fb->rt_formats[rt] == PIPE_FORMAT_NONE) {
      pan_pack(rgb_rt, RGB_RENDER_TARGET, cfg) {
         /* Place non-existent targets at the end.  Hardware bounds
          * checking should just discard writes and zero reads.
          */
         cfg.internal_buffer_offset = fb->tile_rt_alloc_B;
         cfg.internal_format = MALI_COLOR_BUFFER_INTERNAL_FORMAT_R8G8B8A8;
         cfg.write_enable = false;
#if PAN_ARCH >= 7
         cfg.writeback_block_format = MALI_BLOCK_FORMAT_TILED_U_INTERLEAVED;
#endif
      }
      return;
   }

   pan_pack(rgb_rt, RGB_RENDER_TARGET, cfg) {
      cfg.internal_buffer_offset = tile_rt_offset_B;
      cfg.dithering_enable = true;

      if (store && store->rts[rt].store) {
         cfg.writeback_msaa =
            translate_msaa_copy_op(fb, store->rts[rt].iview,
                                   store->rts[rt].msaa);
      }

#if PAN_ARCH >= 6
      cfg.clean_tile_write_enable = !!(ct.rts & BITFIELD_BIT(rt));
#else
      cfg.clean_pixel_write_enable = !!(ct.rts & BITFIELD_BIT(rt));
#endif

      if (load && pan_target_has_clear(&load->rts[rt])) {
         uint32_t packed[4] = {};
         pan_pack_color(GENX(pan_blendable_formats), packed,
                        &load->rts[rt].clear.color, fb->rt_formats[rt],
                        false /* dithered */);

         cfg.clear = (struct MALI_RT_CLEAR) {
            .color_0 = packed[0],
            .color_1 = packed[1],
            .color_2 = packed[2],
            .color_3 = packed[3],
         };
      }
   }

   struct mali_render_target_packed desc;
   if (store && store->rts[rt].store) {
      const struct pan_image_view *iview = store->rts[rt].iview;
      const struct pan_mod_handler *mod_handler =
         pan_image_view_get_color_plane(iview).image->mod_handler;

      assert(info->layer < pan_image_view_layer_or_3d_slice_count(iview));
      const struct pan_attachment_info att = {
         .iview = iview,
         .layer_or_z_slice = iview->first_layer_or_z_slice + info->layer,
         .fb_tile_size_px = fb->tile_size_px,
      };

      mod_handler->emit_color_attachment(&att, &desc);
   } else {
      GENX(pan_emit_default_color_attachment)(fb->rt_formats[rt], &desc);
   }
   pan_merge(rgb_rt, &desc, RGB_RENDER_TARGET);
}

static void
emit_rts(const struct pan_fb_desc_info *info,
         struct mali_rgb_render_target_packed *rts)
{
   const struct pan_fb_layout *fb = info->fb;
   const struct pan_fb_clean_tile ct = GENX(pan_fb_get_clean_tile)(info);

   uint32_t tile_rt_offset_B = 0;
   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      emit_rgb_rt_desc(info, ct, rt, tile_rt_offset_B, rts);
      rts++;

      if (fb->rt_formats[rt] != PIPE_FORMAT_NONE) {
         tile_rt_offset_B += pan_bytes_per_pixel_tib(fb->rt_formats[rt]) *
                             fb->tile_size_px * fb->sample_count;
      }
   }
   assert(tile_rt_offset_B <= fb->tile_rt_alloc_B);
}

#if PAN_ARCH >= 14
uint32_t
GENX(pan_emit_fb_desc)(const struct pan_fb_desc_info *info,
                       const struct pan_fb_descs *out)
{
   if (pan_fb_has_zs(info->fb)) {
      emit_zs_crc_desc(info, GENX(pan_fb_get_clean_tile)(info), out->zs_crc);
   }

   emit_rts(info, out->rts);

   return 0;
}
#else /* PAN_ARCH < 14 */
uint32_t
GENX(pan_emit_fb_desc)(const struct pan_fb_desc_info *info,
                       const struct pan_fb_descs *out)
{
   const struct pan_fb_layout *fb = info->fb;
   const struct pan_fb_load *load = info->load;
   const struct pan_fb_store *store = info->store;
   const struct pan_fb_clean_tile ct = GENX(pan_fb_get_clean_tile)(info);

   const bool has_zs_crc_ext = pan_fb_has_zs(fb);

   struct mali_framebuffer_packed fbd = {};

#if PAN_ARCH <= 5
   GENX(pan_emit_tls)(info->tls,
      pan_section_ptr(&fbd, FRAMEBUFFER, LOCAL_STORAGE));
#endif

   pan_section_pack(&fbd, FRAMEBUFFER, PARAMETERS, cfg) {
#if PAN_ARCH >= 6
      cfg.pre_frame_0 = pan_fix_frame_shader_mode(info->frame_shaders.modes[0],
                                                  ct.rts || ct.zs || ct.s);
      cfg.pre_frame_1 = pan_fix_frame_shader_mode(info->frame_shaders.modes[1],
                                                  ct.rts || ct.zs || ct.s);
      cfg.post_frame = info->frame_shaders.modes[2];
      cfg.frame_shader_dcds = info->frame_shaders.dcd_pointer;

      cfg.sample_locations = info->sample_pos_array_pointer;
#endif

#if PAN_ARCH >= 13
      /* Enabling prepass without pipelineing is generally not good for
       * performance, so disable HSR in that case.
       */
      cfg.hsr_prepass_enable = info->allow_hsr_prepass &&
                               pan_fb_can_pipeline_zs(fb);
      cfg.hsr_prepass_interleaving_enable = pan_fb_can_pipeline_zs(fb);
      cfg.hsr_prepass_filter_enable = true;
      cfg.hsr_hierarchical_optimizations_enable = true;
#endif

#if PAN_ARCH >= 9
      /* internal_layer_index is used to select the right primitive list in
       * the tiler context, and frame_arg is the value that's passed to the
       * fragment shader through r62-r63, which we use to pass gl_Layer. Since
       * the layer_idx only takes 8-bits, we might use the extra 56-bits we
       * have in frame_argument to pass other information to the fragment
       * shader at some point.
       */
      assert(info->layer >= info->tiler_ctx->valhall.layer_offset);
      cfg.internal_layer_index =
         info->layer - info->tiler_ctx->valhall.layer_offset;
      cfg.frame_argument = info->layer;
#endif

      cfg.width = fb->width_px;
      cfg.height = fb->height_px;

      const struct pan_fb_bbox fb_area_px =
         pan_fb_bbox_from_xywh(0, 0, info->fb->width_px, info->fb->height_px);
      assert(pan_fb_bbox_is_valid(info->fb->tiling_area_px));
      const struct pan_fb_bbox bbox_px =
         pan_fb_bbox_clamp(info->fb->tiling_area_px, fb_area_px);
      cfg.bound_min_x = bbox_px.min_x;
      cfg.bound_min_y = bbox_px.min_y;
      cfg.bound_max_x = bbox_px.max_x;
      cfg.bound_max_y = bbox_px.max_y;

      cfg.sample_count = fb->sample_count;
      cfg.sample_pattern = pan_sample_pattern(fb->sample_count);

      /* Ensure we cover the samples on the edge for 16x MSAA */
      cfg.tie_break_rule = fb->sample_count == 16 ?
         MALI_TIE_BREAK_RULE_MINUS_180_OUT_0_IN :
         MALI_TIE_BREAK_RULE_MINUS_180_IN_0_OUT;

      cfg.effective_tile_size = fb->tile_size_px;

#if PAN_ARCH >= 9
      cfg.point_sprite_coord_origin_max_y = info->sprite_coord_origin_max_y;
      cfg.first_provoking_vertex = info->provoking_vertex_first;
#endif

      assert(fb->rt_count > 0);
      cfg.render_target_count = fb->rt_count;
      cfg.color_buffer_allocation = fb->tile_rt_alloc_B;

      if (fb->s_format != PIPE_FORMAT_NONE) {
         cfg.s_clear =
            load && pan_target_has_clear(&load->s) ? load->s.clear.stencil : 0;
         cfg.s_write_enable = store && store->s.store;
      }

      if (fb->z_format != PIPE_FORMAT_NONE) {
         cfg.z_internal_format = pan_get_z_internal_format(fb->z_format);
         cfg.z_clear =
            load && pan_target_has_clear(&load->z) ? load->z.clear.depth : 0;
         cfg.z_write_enable = store && store->zs.store;
      } else {
         /* Default to 24 bit depth if there's no surface. */
         cfg.z_internal_format = MALI_Z_INTERNAL_FORMAT_D24;

         /* If we want a stencil store and we don't have depth, that should
          * happen as a stencil store, not combined depth/stencil.
          */
         assert(!store || !store->zs.store);
      }

      cfg.has_zs_crc_extension = has_zs_crc_ext;

#if PAN_ARCH >= 6
      cfg.tiler = PAN_ARCH >= 9 ? info->tiler_ctx->valhall.desc
                                : info->tiler_ctx->bifrost.desc;
#endif
   }

#if PAN_ARCH <= 5
   /* TODO: Midgard tiler */
   assert(info->tiler_ctx == NULL);

   /* All weights set to 0, nothing to do here */
   pan_section_pack(&fbd, FRAMEBUFFER, TILER_WEIGHTS, w);
#endif

   memcpy(out->fbd, &fbd, sizeof(fbd));

   if (has_zs_crc_ext) {
      emit_zs_crc_desc(info, ct, out->zs_crc);
   }

   emit_rts(info, out->rts);

   struct mali_framebuffer_pointer_packed tag;
   pan_pack(&tag, FRAMEBUFFER_POINTER, cfg) {
      cfg.zs_crc_extension_present = has_zs_crc_ext;
      cfg.render_target_count = fb->rt_count;
   }
   return tag.opaque[0];
}
#endif /* PAN_ARCH >= 14 */
#endif /* PAN_ARCH >= 5 */
