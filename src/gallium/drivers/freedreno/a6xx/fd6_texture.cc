/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/hash_table.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "freedreno_dev_info.h"
#include "fd6_emit.h"
#include "fd6_resource.h"
#include "fd6_screen.h"
#include "fd6_texture.h"

#define XXH_INLINE_ALL
#include "util/xxhash.h"

static void fd6_texture_state_destroy(struct fd6_texture_state *state);

static void
remove_tex_entry(struct fd6_context *fd6_ctx, struct hash_entry *entry)
{
   struct fd6_texture_state *tex = (struct fd6_texture_state *)entry->data;
   _mesa_hash_table_remove(fd6_ctx->tex_cache, entry);
   fd6_texture_state_destroy(tex);
}

static enum a6xx_tex_clamp
tex_clamp(unsigned wrap, bool *needs_border)
{
   switch (wrap) {
   case PIPE_TEX_WRAP_REPEAT:
      return A6XX_TEX_REPEAT;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
      return A6XX_TEX_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      *needs_border = true;
      return A6XX_TEX_CLAMP_TO_BORDER;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
      /* only works for PoT.. need to emulate otherwise! */
      return A6XX_TEX_MIRROR_CLAMP;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      return A6XX_TEX_MIRROR_REPEAT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP:
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
      /* these two we could perhaps emulate, but we currently
       * just don't advertise pipe_caps.texture_mirror_clamp
       */
   default:
      DBG("invalid wrap: %u", wrap);
      return (enum a6xx_tex_clamp)0;
   }
}

static enum a6xx_tex_filter
tex_filter(unsigned filter, bool aniso)
{
   switch (filter) {
   case PIPE_TEX_FILTER_NEAREST:
      return A6XX_TEX_NEAREST;
   case PIPE_TEX_FILTER_LINEAR:
      return aniso ? A6XX_TEX_ANISO : A6XX_TEX_LINEAR;
   default:
      DBG("invalid filter: %u", filter);
      return (enum a6xx_tex_filter)0;
   }
}

static enum a6xx_reduction_mode
reduction_mode(unsigned reduction_mode)
{
   switch (reduction_mode) {
   default:
   case PIPE_TEX_REDUCTION_WEIGHTED_AVERAGE:
      return A6XX_REDUCTION_MODE_AVERAGE;
   case PIPE_TEX_REDUCTION_MIN:
      return A6XX_REDUCTION_MODE_MIN;
   case PIPE_TEX_REDUCTION_MAX:
      return A6XX_REDUCTION_MODE_MAX;
   }
}

static void
setup_border_color(struct fd_screen *screen,
                   const struct pipe_sampler_state *sampler,
                   struct fd6_bcolor_entry *e)
{
   STATIC_ASSERT(sizeof(struct fd6_bcolor_entry) == FD6_BORDER_COLOR_SIZE);
   const bool has_z24uint_s8uint = screen->info->props.has_z24uint_s8uint;
   const union pipe_color_union *bc = &sampler->border_color;

   enum pipe_format format = sampler->border_color_format;
   const struct util_format_description *desc =
      util_format_description(format);

   e->rgb565 = 0;
   e->rgb5a1 = 0;
   e->rgba4 = 0;
   e->rgb10a2 = 0;
   e->z24 = 0;

   unsigned char swiz[4];

   fdl6_format_swiz(format, false, swiz);

   for (unsigned j = 0; j < 4; j++) {
      int c = swiz[j];
      int cd = c;

      /*
       * HACK: for PIPE_FORMAT_X24S8_UINT we end up w/ the
       * stencil border color value in bc->ui[0] but according
       * to desc->swizzle and desc->channel, the .x/.w component
       * is NONE and the stencil value is in the y component.
       * Meanwhile the hardware wants this in the .x component
       * for x24s8 and x32_s8x24, or the .y component for x24s8 with the
       * special Z24UINT_S8UINT format.
       */
      if ((format == PIPE_FORMAT_X24S8_UINT) ||
          (format == PIPE_FORMAT_X32_S8X24_UINT)) {
         if (j == 0) {
            c = 1;
            cd = (format == PIPE_FORMAT_X24S8_UINT && has_z24uint_s8uint) ? 1 : 0;
         } else {
            continue;
         }
      }

      if (c >= 4)
         continue;

      if (desc->channel[c].pure_integer) {
         uint16_t clamped;
         switch (desc->channel[c].size) {
         case 2:
            assert(desc->channel[c].type == UTIL_FORMAT_TYPE_UNSIGNED);
            clamped = CLAMP(bc->ui[j], 0, 0x3);
            break;
         case 8:
            if (desc->channel[c].type == UTIL_FORMAT_TYPE_SIGNED)
               clamped = CLAMP(bc->i[j], -128, 127);
            else
               clamped = CLAMP(bc->ui[j], 0, 255);
            break;
         case 10:
            assert(desc->channel[c].type == UTIL_FORMAT_TYPE_UNSIGNED);
            clamped = CLAMP(bc->ui[j], 0, 0x3ff);
            break;
         case 16:
            if (desc->channel[c].type == UTIL_FORMAT_TYPE_SIGNED)
               clamped = CLAMP(bc->i[j], -32768, 32767);
            else
               clamped = CLAMP(bc->ui[j], 0, 65535);
            break;
         default:
            UNREACHABLE("Unexpected bit size");
         case 32:
            clamped = 0;
            break;
         }
         e->fp32[cd] = bc->ui[j];
         e->fp16[cd] = clamped;
      } else {
         float f = bc->f[j];
         float f_u = CLAMP(f, 0, 1);
         float f_s = CLAMP(f, -1, 1);

         e->fp32[c] = fui(f);
         e->fp16[c] = _mesa_float_to_half(f);
         e->srgb[c] = _mesa_float_to_half(f_u);
         e->ui16[c] = f_u * 0xffff;
         e->si16[c] = f_s * 0x7fff;
         e->ui8[c] = f_u * 0xff;
         e->si8[c] = f_s * 0x7f;

         if (c == 1)
            e->rgb565 |= (int)(f_u * 0x3f) << 5;
         else if (c < 3)
            e->rgb565 |= (int)(f_u * 0x1f) << (c ? 11 : 0);
         if (c == 3)
            e->rgb5a1 |= (f_u > 0.5f) ? 0x8000 : 0;
         else
            e->rgb5a1 |= (int)(f_u * 0x1f) << (c * 5);
         if (c == 3)
            e->rgb10a2 |= (int)(f_u * 0x3) << 30;
         else
            e->rgb10a2 |= (int)(f_u * 0x3ff) << (c * 10);
         e->rgba4 |= (int)(f_u * 0xf) << (c * 4);
         if (c == 0)
            e->z24 = f_u * 0xffffff;
      }
   }
}

static uint32_t
bcolor_key_hash(const void *_key)
{
   const struct fd6_bcolor_entry *key = (const struct fd6_bcolor_entry *)_key;
   return XXH32(key, sizeof(*key), 0);
}

static bool
bcolor_key_equals(const void *_a, const void *_b)
{
   const struct fd6_bcolor_entry *a = (const struct fd6_bcolor_entry *)_a;
   const struct fd6_bcolor_entry *b = (const struct fd6_bcolor_entry *)_b;
   return memcmp(a, b, sizeof(struct fd6_bcolor_entry)) == 0;
}

static unsigned
get_bcolor_offset(struct fd_context *ctx, const struct pipe_sampler_state *sampler)
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd6_bcolor_entry *entries =
         (struct fd6_bcolor_entry *)fd_bo_map(fd6_ctx->bcolor_mem);
   struct fd6_bcolor_entry key = {};

   setup_border_color(ctx->screen, sampler, &key);

   uint32_t hash = bcolor_key_hash(&key);

   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(fd6_ctx->bcolor_cache, hash, &key);

   if (entry) {
      return (unsigned)(uintptr_t)entry->data;
   }

   unsigned idx = fd6_ctx->bcolor_cache->entries;
   if (idx >= FD6_MAX_BORDER_COLORS) {
      mesa_loge("too many border colors");
      return 0;
   }

   entries[idx] = key;

   _mesa_hash_table_insert_pre_hashed(fd6_ctx->bcolor_cache, hash,
                                      &entries[idx], (void *)(uintptr_t)idx);

   return idx;
}

template <chip CHIP>
static void *
fd6_sampler_state_create(struct pipe_context *pctx,
                         const struct pipe_sampler_state *cso)
{
   struct fd6_sampler_stateobj *so = CALLOC_STRUCT(fd6_sampler_stateobj);
   struct fd_context *ctx = fd_context(pctx);
   unsigned aniso = util_last_bit(MIN2(cso->max_anisotropy >> 1, 8));
   bool miplinear = false;

   if (!so)
      return NULL;

   so->base = *cso;
   so->seqno = util_idalloc_alloc(&fd6_context(ctx)->tex_ids);

   if (cso->min_mip_filter == PIPE_TEX_MIPFILTER_LINEAR)
      miplinear = true;

   /* We don't know if the format is going to be YUV.  Setting CHROMA_LINEAR
    * unconditionally seems fine.
    */
   bool chroma_linear =
      cso->mag_img_filter == PIPE_TEX_FILTER_LINEAR &&
      cso->min_img_filter == PIPE_TEX_FILTER_LINEAR;

   bool needs_border = false;

   if (CHIP <= A7XX) {
      so->descriptor[0] =
         COND(miplinear, A6XX_TEX_SAMP_0_MIPFILTER_LINEAR_NEAR) |
         A6XX_TEX_SAMP_0_XY_MAG(tex_filter(cso->mag_img_filter, aniso)) |
         A6XX_TEX_SAMP_0_XY_MIN(tex_filter(cso->min_img_filter, aniso)) |
         A6XX_TEX_SAMP_0_ANISO((enum a6xx_tex_aniso)aniso) |
         A6XX_TEX_SAMP_0_WRAP_S(tex_clamp(cso->wrap_s, &needs_border)) |
         A6XX_TEX_SAMP_0_WRAP_T(tex_clamp(cso->wrap_t, &needs_border)) |
         A6XX_TEX_SAMP_0_WRAP_R(tex_clamp(cso->wrap_r, &needs_border)) |
         A6XX_TEX_SAMP_0_LOD_BIAS(cso->lod_bias);

      so->descriptor[1] =
         COND(cso->compare_mode, A6XX_TEX_SAMP_1_COMPARE_FUNC((enum adreno_compare_func)cso->compare_func)) |
         COND(cso->min_mip_filter == PIPE_TEX_MIPFILTER_NONE,
            A6XX_TEX_SAMP_1_MIPFILTER_LINEAR_FAR) |
         COND(!cso->seamless_cube_map, A6XX_TEX_SAMP_1_CUBEMAPSEAMLESSFILTOFF) |
         COND(cso->unnormalized_coords, A6XX_TEX_SAMP_1_UNNORM_COORDS) |
         A6XX_TEX_SAMP_1_MIN_LOD(cso->min_lod) |
         A6XX_TEX_SAMP_1_MAX_LOD(cso->max_lod);

      so->descriptor[2] =
         A6XX_TEX_SAMP_2_REDUCTION_MODE(reduction_mode(cso->reduction_mode)) |
         COND(chroma_linear, A6XX_TEX_SAMP_2_CHROMA_LINEAR);

   } else if (CHIP >= A8XX) {
      const float MAX_LOD = 4095.0f / 256.0f;

      so->descriptor[0] =
         COND(miplinear, A8XX_TEX_SAMP_0_MIPFILTER_LINEAR_NEAR) |
         COND(cso->min_mip_filter == PIPE_TEX_MIPFILTER_NONE,
            A8XX_TEX_SAMP_0_MIPMAPING_DIS) |
         A8XX_TEX_SAMP_0_XY_MAG(tex_filter(cso->mag_img_filter, aniso)) |
         A8XX_TEX_SAMP_0_XY_MIN(tex_filter(cso->min_img_filter, aniso)) |
         A8XX_TEX_SAMP_0_WRAP_S(tex_clamp(cso->wrap_s, &needs_border)) |
         A8XX_TEX_SAMP_0_WRAP_T(tex_clamp(cso->wrap_t, &needs_border)) |
         A8XX_TEX_SAMP_0_WRAP_R(tex_clamp(cso->wrap_r, &needs_border)) |
         A8XX_TEX_SAMP_0_LOD_BIAS(CLAMP(cso->lod_bias, -16, MAX_LOD)) |
         A8XX_TEX_SAMP_0_ANISO((enum a6xx_tex_aniso)aniso);

      float min_lod, max_lod;

      if (cso->unnormalized_coords) {
         min_lod = max_lod = 0;
      } else if (cso->min_mip_filter != PIPE_TEX_MIPFILTER_NONE) {
         min_lod = CLAMP(cso->min_lod, 0.0f, MAX_LOD);
         max_lod = CLAMP(cso->max_lod, 0.0f, MAX_LOD);
      } else {
         min_lod = CLAMP(cso->min_lod, 0.0f, 0.25f);
         max_lod = CLAMP(cso->max_lod, 0.0f, 0.25f);
      }

      so->descriptor[1] =
         A8XX_TEX_SAMP_1_MAX_LOD(max_lod) |
         A8XX_TEX_SAMP_1_MIN_LOD(min_lod) |
         A8XX_TEX_SAMP_1_REDUCTION_MODE(reduction_mode(cso->reduction_mode)) |
         COND(cso->compare_mode, A8XX_TEX_SAMP_1_COMPARE_FUNC((enum adreno_compare_func)cso->compare_func)) |
         COND(chroma_linear, A8XX_TEX_SAMP_1_CHROMA_LINEAR) |
         COND(!cso->seamless_cube_map, A8XX_TEX_SAMP_1_CUBEMAPSEAMLESSFILTOFF) |
         COND(cso->unnormalized_coords, A8XX_TEX_SAMP_1_UNNORM_COORDS);
   }

   if (needs_border) {
      bool fast_border_color_enable = false;
      enum a6xx_fast_border_color fast_border_color = A6XX_BORDER_COLOR_0_0_0_0;
      if (cso->border_color.ui[0] == 0 &&
          cso->border_color.ui[1] == 0 &&
          cso->border_color.ui[2] == 0 &&
          cso->border_color.ui[3] == 0) {
         fast_border_color_enable = true;
         fast_border_color = A6XX_BORDER_COLOR_0_0_0_0;
      } else if (cso->border_color.ui[0] == 0 &&
                 cso->border_color.ui[1] == 0 &&
                 cso->border_color.ui[2] == 0 &&
                 cso->border_color.ui[3] == 0x3F800000) {
         fast_border_color_enable = true;
         fast_border_color = A6XX_BORDER_COLOR_0_0_0_1;
      } else if (cso->border_color.ui[0] == 0x3F800000 &&
                 cso->border_color.ui[1] == 0x3F800000 &&
                 cso->border_color.ui[2] == 0x3F800000 &&
                 cso->border_color.ui[3] == 0) {
         fast_border_color_enable = true;
         fast_border_color = A6XX_BORDER_COLOR_1_1_1_0;
      } else if (cso->border_color.ui[0] == 0x3F800000 &&
                 cso->border_color.ui[1] == 0x3F800000 &&
                 cso->border_color.ui[2] == 0x3F800000 &&
                 cso->border_color.ui[3] == 0x3F800000) {
         fast_border_color_enable = true;
         fast_border_color = A6XX_BORDER_COLOR_1_1_1_1;
      }

      if (CHIP <= A7XX) {
         if (fast_border_color_enable) {
            so->descriptor[2] |= A6XX_TEX_SAMP_2_FASTBORDERCOLOR(fast_border_color) |
                                 A6XX_TEX_SAMP_2_FASTBORDERCOLOREN;
         } else {
            so->descriptor[2] |= A6XX_TEX_SAMP_2_BCOLOR(get_bcolor_offset(ctx, cso));
         }
      } else if (CHIP >= A8XX) {
         if (fast_border_color_enable) {
            so->descriptor[2] |= A8XX_TEX_SAMP_2_FASTBORDERCOLOR(fast_border_color) |
                                 A8XX_TEX_SAMP_2_FASTBORDERCOLOREN;
         } else {
            so->descriptor[2] |= A8XX_TEX_SAMP_2_BCOLOR(get_bcolor_offset(ctx, cso));
         }
      }
   }

   return so;
}

static void
fd6_sampler_state_delete(struct pipe_context *pctx, void *hwcso)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd6_sampler_stateobj *samp = (struct fd6_sampler_stateobj *)hwcso;

   fd_screen_lock(ctx->screen);

   hash_table_foreach (fd6_ctx->tex_cache, entry) {
      struct fd6_texture_state *state = (struct fd6_texture_state *)entry->data;

      for (unsigned i = 0; i < ARRAY_SIZE(state->key.samp_seqno); i++) {
         if (samp->seqno == state->key.samp_seqno[i]) {
            remove_tex_entry(fd6_ctx, entry);
            break;
         }
      }
   }

   fd_screen_unlock(ctx->screen);

   util_idalloc_free(&fd6_ctx->tex_ids, samp->seqno);

   free(hwcso);
}

static struct pipe_sampler_view *
fd6_sampler_view_create(struct pipe_context *pctx, struct pipe_resource *prsc,
                        const struct pipe_sampler_view *cso)
{
   struct fd6_context *fd6_ctx = fd6_context(fd_context(pctx));
   struct fd6_pipe_sampler_view *so = CALLOC_STRUCT(fd6_pipe_sampler_view);

   if (!so)
      return NULL;

   so->base = *cso;
   so->seqno = util_idalloc_alloc(&fd6_ctx->tex_ids);
   pipe_reference(NULL, &prsc->reference);
   so->base.texture = prsc;
   so->base.reference.count = 1;
   so->base.context = pctx;

   return &so->base;
}

/**
 * Remove any texture state entries that reference the specified sampler
 * view.
 */
static void
fd6_sampler_view_invalidate(struct fd_context *ctx,
                            struct fd6_pipe_sampler_view *view)
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);

   fd_screen_lock(ctx->screen);

   hash_table_foreach (fd6_ctx->tex_cache, entry) {
      struct fd6_texture_state *state = (struct fd6_texture_state *)entry->data;

      for (unsigned i = 0; i < ARRAY_SIZE(state->key.view_seqno); i++) {
         if (view->seqno == state->key.view_seqno[i]) {
            remove_tex_entry(fd6_ctx, entry);
            break;
         }
      }
   }

   fd_screen_unlock(ctx->screen);
}

template <chip CHIP>
static void
fd6_sampler_view_update(struct fd_context *ctx,
                        struct fd6_pipe_sampler_view *so)
   assert_dt
{
   const struct pipe_sampler_view *cso = &so->base;
   struct pipe_resource *prsc = cso->texture;
   struct fd_resource *rsc = fd_resource(prsc);
   enum pipe_format format = cso->format;

   fd6_assert_valid_format(rsc, cso->format);

   /* If texture has not had a layout change, then no update needed: */
   if (so->rsc_seqno == rsc->seqno)
      return;

   fd6_sampler_view_invalidate(ctx, so);

   so->rsc_seqno = rsc->seqno;

   if (format == PIPE_FORMAT_X32_S8X24_UINT) {
      rsc = rsc->stencil;
      format = rsc->b.b.format;
   }

   if (cso->is_tex2d_from_buf) {
      struct fdl_view_args args = {
         .iova = fd_bo_get_iova(rsc->bo),
         .base_miplevel = 0,
         .level_count = 1,
         .base_array_layer = 0,
         .layer_count = 1,
         .swiz = {cso->swizzle_r, cso->swizzle_g, cso->swizzle_b,
                  cso->swizzle_a},
         .format = format,

         .type = FDL_VIEW_TYPE_2D,
         .chroma_offsets = {FDL_CHROMA_LOCATION_COSITED_EVEN,
                            FDL_CHROMA_LOCATION_COSITED_EVEN},
      };

      struct fdl_layout layout;
      const struct fdl_layout *layouts = &layout;

      fd6_layout_tex2d_from_buf(&layout, ctx->screen->info, format,
                                &cso->u.tex2d_from_buf);

      struct fdl6_view view;
      fdl6_view_init<CHIP>(&view, &layouts, &args,
                           ctx->screen->info->props.has_z24uint_s8uint);

      memcpy(so->descriptor, view.descriptor, sizeof(so->descriptor));
   } else if (cso->target == PIPE_BUFFER) {
      uint8_t swiz[4] = {cso->swizzle_r, cso->swizzle_g, cso->swizzle_b,
                         cso->swizzle_a};

      uint64_t iova = cso->u.buf.offset + fd_bo_get_iova(rsc->bo);

      uint32_t size = fd_clamp_buffer_size(cso->format, cso->u.buf.size,
                                           A4XX_MAX_TEXEL_BUFFER_ELEMENTS_UINT);

      fdl6_buffer_view_init<CHIP>(so->descriptor, cso->format, swiz, iova, size);
   } else {
      struct fdl_view_args args = {
         .iova = fd_bo_get_iova(rsc->bo),

         .base_miplevel = fd_sampler_first_level(cso),
         .level_count =
            fd_sampler_last_level(cso) - fd_sampler_first_level(cso) + 1,

         .base_array_layer = cso->u.tex.first_layer,
         .layer_count = cso->u.tex.last_layer - cso->u.tex.first_layer + 1,

         .swiz = {cso->swizzle_r, cso->swizzle_g, cso->swizzle_b,
                  cso->swizzle_a},
         .format = format,

         .type = fdl_type_from_pipe_target(cso->target),
         .chroma_offsets = {FDL_CHROMA_LOCATION_COSITED_EVEN,
                            FDL_CHROMA_LOCATION_COSITED_EVEN},
      };

      if (rsc->b.b.format == PIPE_FORMAT_R8_G8B8_420_UNORM) {
         args.chroma_offsets[0] = FDL_CHROMA_LOCATION_MIDPOINT;
         args.chroma_offsets[1] = FDL_CHROMA_LOCATION_MIDPOINT;
      }

      struct fd_resource *plane1 = fd_resource(rsc->b.b.next);
      struct fd_resource *plane2 =
         plane1 ? fd_resource(plane1->b.b.next) : NULL;
      static const struct fdl_layout dummy_layout = {};
      const struct fdl_layout *layouts[3] = {
         &rsc->layout,
         plane1 ? &plane1->layout : &dummy_layout,
         plane2 ? &plane2->layout : &dummy_layout,
      };
      struct fdl6_view view;
      fdl6_view_init<CHIP>(&view, layouts, &args,
                           ctx->screen->info->props.has_z24uint_s8uint);
      memcpy(so->descriptor, view.descriptor, sizeof(so->descriptor));
   }
}

template <chip CHIP>
static void
fd6_set_sampler_views(struct pipe_context *pctx, mesa_shader_stage shader,
                      unsigned start, unsigned nr,
                      unsigned unbind_num_trailing_slots,
                      struct pipe_sampler_view **views)
   in_dt
{
   struct fd_context *ctx = fd_context(pctx);

   fd_set_sampler_views(pctx, shader, start, nr, unbind_num_trailing_slots,
                        views);

   if (!views)
      return;

   for (unsigned i = 0; i < nr; i++) {
      struct fd6_pipe_sampler_view *so = fd6_pipe_sampler_view(views[i + start]);

      if (!so)
         continue;

      struct fd_resource *rsc = fd_resource(so->base.texture);

      fd6_validate_format(ctx, rsc, so->base.format);
      fd6_sampler_view_update<CHIP>(ctx, so);
   }
}

/* NOTE this can be called in either driver thread or frontend thread
 * depending on where the last unref comes from
 */
static void
fd6_sampler_view_destroy(struct pipe_context *pctx,
                         struct pipe_sampler_view *_view)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd6_pipe_sampler_view *view = fd6_pipe_sampler_view(_view);

   fd6_sampler_view_invalidate(ctx, view);

   pipe_resource_reference(&view->base.texture, NULL);

   util_idalloc_free(&fd6_context(ctx)->tex_ids, view->seqno);

   free(view);
}

static uint32_t
tex_key_hash(const void *_key)
{
   const struct fd6_texture_key *key = (const struct fd6_texture_key *)_key;
   return XXH32(key, sizeof(*key), 0);
}

static bool
tex_key_equals(const void *_a, const void *_b)
{
   const struct fd6_texture_key *a = (const struct fd6_texture_key *)_a;
   const struct fd6_texture_key *b = (const struct fd6_texture_key *)_b;
   return memcmp(a, b, sizeof(struct fd6_texture_key)) == 0;
}

static enum a6xx_state_block
stage2sb(mesa_shader_stage type)
{
   switch (type) {
   case MESA_SHADER_VERTEX:     return SB6_VS_TEX;
   case MESA_SHADER_TESS_CTRL:  return SB6_HS_TEX;
   case MESA_SHADER_TESS_EVAL:  return SB6_DS_TEX;
   case MESA_SHADER_GEOMETRY:   return SB6_GS_TEX;
   case MESA_SHADER_FRAGMENT:   return SB6_FS_TEX;
   case MESA_SHADER_COMPUTE:    return SB6_CS_TEX;
   default:
      UNREACHABLE("bad state block");
   }
}

static struct fd_ringbuffer *
build_texture_state(struct fd_context *ctx, mesa_shader_stage type,
                    struct fd_texture_stateobj *tex)
   assert_dt
{
   struct fd_bo *tex_desc = NULL, *samp_desc = NULL;
   fd_cs cs(ctx->pipe, 32 * 4);

   if (tex->num_samplers > 0) {
      samp_desc = fd_bo_new(ctx->dev, tex->num_samplers * 4 * 4,
                            FD_BO_GPUREADONLY | FD_BO_HINT_COMMAND,
                            "samp desc");
      uint32_t *buf = (uint32_t *)fd_bo_map(samp_desc);
      fd_bo_mark_for_dump(samp_desc);

      for (unsigned i = 0; i < tex->num_samplers; i++) {
         static const struct fd6_sampler_stateobj dummy_sampler = {};
         const struct fd6_sampler_stateobj *sampler =
            tex->samplers[i] ? fd6_sampler_stateobj(tex->samplers[i])
                             : &dummy_sampler;
         memcpy(buf, sampler->descriptor, 4 * 4);
         buf += 4;
      }

      cs.attach_bo(samp_desc);
   }

   if (tex->num_textures > 0) {
      tex_desc = fd_bo_new(ctx->dev, tex->num_textures * 16 * 4,
                           FD_BO_GPUREADONLY | FD_BO_HINT_COMMAND,
                           "tex desc");
      uint32_t *buf = (uint32_t *)fd_bo_map(tex_desc);
      fd_bo_mark_for_dump(tex_desc);

      for (unsigned i = 0; i < tex->num_textures; i++) {
         const struct fd6_pipe_sampler_view *view;

         if (tex->textures[i]) {
            view = fd6_pipe_sampler_view(tex->textures[i]);
            struct fd_resource *rsc = fd_resource(view->base.texture);
            fd6_assert_valid_format(rsc, view->base.format);
            assert(view->rsc_seqno == rsc->seqno);
         } else {
            static const struct fd6_pipe_sampler_view dummy_view = {};
            view = &dummy_view;
         }

         memcpy(buf, view->descriptor, 16 * 4);
         buf += 16;
      }

      cs.attach_bo(tex_desc);
   }

   with_crb (cs, 5) {
      switch (type) {
      case MESA_SHADER_VERTEX:
         crb.add(A6XX_SP_VS_SAMPLER_BASE(samp_desc));
         crb.add(A6XX_SP_VS_TEXMEMOBJ_BASE(tex_desc));
         crb.add(A6XX_SP_VS_TSIZE(tex->num_textures));
         break;
      case MESA_SHADER_TESS_CTRL:
         crb.add(A6XX_SP_HS_SAMPLER_BASE(samp_desc));
         crb.add(A6XX_SP_HS_TEXMEMOBJ_BASE(tex_desc));
         crb.add(A6XX_SP_HS_TSIZE(tex->num_textures));
         break;
      case MESA_SHADER_TESS_EVAL:
         crb.add(A6XX_SP_DS_SAMPLER_BASE(samp_desc));
         crb.add(A6XX_SP_DS_TEXMEMOBJ_BASE(tex_desc));
         crb.add(A6XX_SP_DS_TSIZE(tex->num_textures));
         break;
      case MESA_SHADER_GEOMETRY:
         crb.add(A6XX_SP_GS_SAMPLER_BASE(samp_desc));
         crb.add(A6XX_SP_GS_TEXMEMOBJ_BASE(tex_desc));
         crb.add(A6XX_SP_GS_TSIZE(tex->num_textures));
         break;
      case MESA_SHADER_FRAGMENT:
         crb.add(A6XX_SP_PS_SAMPLER_BASE(samp_desc));
         crb.add(A6XX_SP_PS_TEXMEMOBJ_BASE(tex_desc));
         crb.add(A6XX_SP_PS_TSIZE(tex->num_textures));
         break;
      case MESA_SHADER_COMPUTE:
         crb.add(A6XX_SP_CS_SAMPLER_BASE(samp_desc));
         crb.add(A6XX_SP_CS_TEXMEMOBJ_BASE(tex_desc));
         crb.add(A6XX_SP_CS_TSIZE(tex->num_textures));
         break;
      default:
         UNREACHABLE("bad state block");
      }
   }

   if (samp_desc) {
      fd_pkt7(cs, fd6_stage2opcode(type), 3)
         .add(CP_LOAD_STATE6_0(
            .state_type = ST6_SHADER,
            .state_src = SS6_INDIRECT,
            .state_block = stage2sb(type),
            .num_unit = tex->num_samplers,
         ))
         .add(CP_LOAD_STATE6_EXT_SRC_ADDR(samp_desc));

         fd_bo_del(samp_desc);
   }

   if (tex_desc) {
      fd_pkt7(cs, fd6_stage2opcode(type), 3)
         .add(CP_LOAD_STATE6_0(
            .state_type = ST6_CONSTANTS,
            .state_src = SS6_INDIRECT,
            .state_block = stage2sb(type),
            .num_unit = tex->num_textures,
         ))
         .add(CP_LOAD_STATE6_EXT_SRC_ADDR(tex_desc));

         fd_bo_del(tex_desc);
   }

   return cs;
}

/**
 * Handle invalidates potentially coming from other contexts.  By
 * flagging the invalidate rather than handling it immediately, we
 * avoid needing to refcnt (with it's associated atomics) the tex
 * state.  And furthermore, this avoids cross-ctx (thread) sharing
 * of fd_ringbuffer's, avoiding their need for atomic refcnts.
 */
template <chip CHIP>
static void
handle_invalidates(struct fd_context *ctx)
   assert_dt
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);

   fd_screen_lock(ctx->screen);

   hash_table_foreach (fd6_ctx->tex_cache, entry) {
      struct fd6_texture_state *state = (struct fd6_texture_state *)entry->data;

      if (state->invalidate)
         remove_tex_entry(fd6_ctx, entry);
   }

   fd_screen_unlock(ctx->screen);

   for (unsigned type = 0; type < ARRAY_SIZE(ctx->tex); type++) {
      struct fd_texture_stateobj *tex = &ctx->tex[type];

      for (unsigned i = 0; i < tex->num_textures; i++) {
         struct fd6_pipe_sampler_view *so =
               fd6_pipe_sampler_view(tex->textures[i]);

         if (!so)
            continue;

         fd6_sampler_view_update<CHIP>(ctx, so);
      }
   }

   fd6_ctx->tex_cache_needs_invalidate = false;
}

template <chip CHIP>
struct fd6_texture_state *
fd6_texture_state(struct fd_context *ctx, mesa_shader_stage type)
{
   struct fd_texture_stateobj *tex = &ctx->tex[type];
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd6_texture_state *state = NULL;
   struct fd6_texture_key key;

   if (unlikely(fd6_ctx->tex_cache_needs_invalidate))
      handle_invalidates<CHIP>(ctx);

   memset(&key, 0, sizeof(key));

   for (unsigned i = 0; i < tex->num_textures; i++) {
      if (!tex->textures[i])
         continue;

      struct fd6_pipe_sampler_view *view =
         fd6_pipe_sampler_view(tex->textures[i]);

      key.view_seqno[i] = view->seqno;
   }

   for (unsigned i = 0; i < tex->num_samplers; i++) {
      if (!tex->samplers[i])
         continue;

      struct fd6_sampler_stateobj *sampler =
         fd6_sampler_stateobj(tex->samplers[i]);

      key.samp_seqno[i] = sampler->seqno;
   }

   key.type = type;

   uint32_t hash = tex_key_hash(&key);
   fd_screen_lock(ctx->screen);

   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(fd6_ctx->tex_cache, hash, &key);

   if (entry) {
      state = (struct fd6_texture_state *)entry->data;
      for (unsigned i = 0; i < tex->num_textures; i++) {
         uint16_t seqno = tex->textures[i] ?
               fd_resource(tex->textures[i]->texture)->seqno : 0;

         assert(state->view_rsc_seqno[i] == seqno);
      }
      goto out_unlock;
   }

   state = CALLOC_STRUCT(fd6_texture_state);

   for (unsigned i = 0; i < tex->num_textures; i++) {
      if (!tex->textures[i])
         continue;

      struct fd_resource *rsc = fd_resource(tex->textures[i]->texture);

      assert(rsc->dirty & FD_DIRTY_TEX);

      state->view_rsc_seqno[i] = rsc->seqno;
   }

   state->key = key;
   state->stateobj = build_texture_state(ctx, type, tex);

   /* NOTE: uses copy of key in state obj, because pointer passed by caller
    * is probably on the stack
    */
   _mesa_hash_table_insert_pre_hashed(fd6_ctx->tex_cache, hash, &state->key,
                                      state);

out_unlock:
   fd_screen_unlock(ctx->screen);
   return state;
}
FD_GENX(fd6_texture_state);

static void
fd6_texture_state_destroy(struct fd6_texture_state *state)
{
   fd_ringbuffer_del(state->stateobj);
   free(state);
}

static void
fd6_rebind_resource(struct fd_context *ctx, struct fd_resource *rsc) assert_dt
{
   fd_screen_assert_locked(ctx->screen);

   if (!(rsc->dirty & FD_DIRTY_TEX))
      return;

   struct fd6_context *fd6_ctx = fd6_context(ctx);

   hash_table_foreach (fd6_ctx->tex_cache, entry) {
      struct fd6_texture_state *state = (struct fd6_texture_state *)entry->data;

      STATIC_ASSERT(ARRAY_SIZE(state->view_rsc_seqno) == ARRAY_SIZE(state->key.view_seqno));

      for (unsigned i = 0; i < ARRAY_SIZE(state->view_rsc_seqno); i++) {
         if (rsc->seqno == state->view_rsc_seqno[i]) {
            struct fd6_texture_state *tex =
                  (struct fd6_texture_state *)entry->data;
            tex->invalidate = true;
            fd6_ctx->tex_cache_needs_invalidate = true;
         }
      }
   }
}

template <chip CHIP>
void
fd6_texture_init(struct pipe_context *pctx) disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd6_context *fd6_ctx = fd6_context(ctx);

   pctx->create_sampler_state = fd6_sampler_state_create<CHIP>;
   pctx->delete_sampler_state = fd6_sampler_state_delete;
   pctx->bind_sampler_states = fd_sampler_states_bind;

   pctx->create_sampler_view = fd6_sampler_view_create;
   pctx->sampler_view_destroy = fd6_sampler_view_destroy;
   pctx->set_sampler_views = fd6_set_sampler_views<CHIP>;

   ctx->rebind_resource = fd6_rebind_resource;

   fd6_ctx->bcolor_cache =
         _mesa_hash_table_create(NULL, bcolor_key_hash, bcolor_key_equals);
   fd6_ctx->bcolor_mem = fd_bo_new(ctx->screen->dev,
                                   FD6_MAX_BORDER_COLORS * FD6_BORDER_COLOR_SIZE,
                                   0, "bcolor");

   fd_context_add_private_bo(ctx, fd6_ctx->bcolor_mem);

   fd6_ctx->tex_cache = _mesa_hash_table_create(NULL, tex_key_hash, tex_key_equals);
   util_idalloc_init(&fd6_ctx->tex_ids, 256);
}
FD_GENX(fd6_texture_init);

void
fd6_texture_fini(struct pipe_context *pctx)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd6_context *fd6_ctx = fd6_context(ctx);

   fd_screen_lock(ctx->screen);

   hash_table_foreach (fd6_ctx->tex_cache, entry) {
      remove_tex_entry(fd6_ctx, entry);
   }

   fd_screen_unlock(ctx->screen);

   util_idalloc_fini(&fd6_ctx->tex_ids);
   ralloc_free(fd6_ctx->tex_cache);
   fd_bo_del(fd6_ctx->bcolor_mem);
   ralloc_free(fd6_ctx->bcolor_cache);
}
