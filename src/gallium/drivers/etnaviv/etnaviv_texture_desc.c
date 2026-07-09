/*
 * Copyright (c) 2017 Etnaviv Project
 * Copyright (C) 2017 Zodiac Inflight Innovations
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#include "etnaviv_texture_desc.h"

#include "hw/common.xml.h"
#include "hw/texdesc_3d.xml.h"

#include "etnaviv_clear_blit.h"
#include "etnaviv_context.h"
#include "etnaviv_emit.h"
#include "etnaviv_format.h"
#include "etnaviv_translate.h"
#include "etnaviv_texture.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#include <drm_fourcc.h>

#define TEXTURE_DESC_SIZE 256

struct etna_sampler_state_desc {
   struct pipe_sampler_state base;
   uint32_t SAMP_CTRL0;
   uint32_t SAMP_CTRL1;
   uint32_t SAMP_LOD_MINMAX;
   uint32_t SAMP_LOD_BIAS;
   uint32_t SAMP_ANISOTROPY;
};

static inline struct etna_sampler_state_desc *
etna_sampler_state_desc(struct pipe_sampler_state *samp)
{
   return (struct etna_sampler_state_desc *)samp;
}

struct etna_sampler_view_desc {
   struct pipe_sampler_view base;
   /* format-dependent merged with sampler state */
   uint32_t SAMP_CTRL0;
   uint32_t SAMP_CTRL0_MASK;
   uint32_t SAMP_CTRL1;
   uint32_t templ[TEXTURE_DESC_SIZE / sizeof(uint32_t)];
   uint32_t native_format;   /* CONFIG0 format for native byte order, 0 if n/a */

   /* compose-at-emit state */
   struct pipe_resource *res;
   struct etna_reloc DESC_ADDR;
   struct etna_reloc DESC_ADDR_COMPANION;
   uint32_t composed_key;                  /* last composed variant key, ~0u = never/retry */

   struct etna_sampler_ts ts;
};

static inline struct etna_sampler_view_desc *
etna_sampler_view_desc(struct pipe_sampler_view *view)
{
   return (struct etna_sampler_view_desc *)view;
}

static inline
uint32_t etna_lod_to_fixp58(float f)
{
   f = CLAMP(f, -16.0f, 15.0f + (255.0f / 256.0f));

   return etna_float_to_fixp88(f);
}

static void *
etna_create_sampler_state_desc(struct pipe_context *pipe,
                          const struct pipe_sampler_state *ss)
{
   struct etna_sampler_state_desc *cs = CALLOC_STRUCT(etna_sampler_state_desc);
   const bool ansio = ss->max_anisotropy > 1;

   if (!cs)
      return NULL;

   cs->base = *ss;

   cs->SAMP_CTRL0 =
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_UWRAP(translate_texture_wrapmode(ss->wrap_s)) |
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_VWRAP(translate_texture_wrapmode(ss->wrap_t)) |
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_WWRAP(translate_texture_wrapmode(ss->wrap_r)) |
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_MIN(translate_texture_filter(ss->min_img_filter)) |
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_MIP(translate_texture_mipfilter(ss->min_mip_filter)) |
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_MAG(translate_texture_filter(ss->mag_img_filter)) |
      COND(ss->compare_mode, VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_COMPARE_ENABLE) |
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_COMPARE_FUNC(translate_texture_compare(ss->compare_func)) |
      VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_UNK21;
      /* no ROUND_UV bit? */
   cs->SAMP_CTRL1 = VIVS_NTE_DESCRIPTOR_SAMP_CTRL1_UNK1;
   uint32_t min_lod_fp8 = etna_lod_to_fixp58(ss->min_lod);
   uint32_t max_lod_fp8 = etna_lod_to_fixp58(ss->max_lod);
   uint32_t max_lod_min = ss->min_img_filter != ss->mag_img_filter ? 4 : 0;

   cs->SAMP_LOD_MINMAX =
      VIVS_NTE_DESCRIPTOR_SAMP_LOD_MINMAX_MAX(MAX2(max_lod_fp8, max_lod_min)) |
      VIVS_NTE_DESCRIPTOR_SAMP_LOD_MINMAX_MIN(min_lod_fp8);

   cs->SAMP_LOD_BIAS =
      VIVS_NTE_DESCRIPTOR_SAMP_LOD_BIAS_BIAS(etna_float_to_fixp88(ss->lod_bias)) |
      COND(ss->lod_bias != 0.0, VIVS_NTE_DESCRIPTOR_SAMP_LOD_BIAS_ENABLE);
   cs->SAMP_ANISOTROPY = COND(ansio, etna_log2_fixp88(ss->max_anisotropy));

   return cs;
}

static void
etna_delete_sampler_state_desc(struct pipe_context *pctx, void *ss)
{
   FREE(ss);
}

static void
etna_texture_desc_fill(struct etna_context *ctx,
                       const struct etna_sampler_view_desc *sv,
                       struct etna_resource *res,
                       uint32_t *buf)
{
   const struct util_format_description *desc =
      util_format_description(sv->base.format);
   uint32_t format = translate_texture_format(sv->base.format, ctx->screen);
   const bool ext = !!(format & EXT_FORMAT);
   const bool astc = !!(format & ASTC_FORMAT);
   const uint32_t swiz = get_texture_swiz(sv->base.format, sv->base.swizzle_r,
                                          sv->base.swizzle_g, sv->base.swizzle_b,
                                          sv->base.swizzle_a);
   uint32_t target_hw = translate_texture_target(sv->base.target);

   /** GC7000 needs the size of the BASELOD level */
   uint32_t base_width = u_minify(res->base.width0, sv->base.u.tex.first_level);
   uint32_t base_height = u_minify(res->base.height0, sv->base.u.tex.first_level);
   uint32_t base_depth = u_minify(res->base.depth0, sv->base.u.tex.first_level);
   bool is_array = false;
   bool sint = util_format_is_pure_sint(sv->base.format);

   switch(sv->base.target) {
   case PIPE_TEXTURE_1D:
      target_hw = TEXTURE_TYPE_2D;
      break;
   case PIPE_TEXTURE_1D_ARRAY:
      is_array = true;
      base_height = res->base.array_size;
      break;
   case PIPE_TEXTURE_2D_ARRAY:
      is_array = true;
      base_depth = res->base.array_size;
      break;
   default:
      break;
   }

#define DESC_SET(x, y) buf[(TEXDESC_##x)>>2] = (y)
   DESC_SET(CONFIG0, COND(!ext && !astc, VIVS_TE_SAMPLER_CONFIG0_FORMAT(format))
                   | VIVS_TE_SAMPLER_CONFIG0_TYPE(target_hw) |
                   COND(res->layout == ETNA_LAYOUT_LINEAR && !util_format_is_compressed(sv->base.format),
                        VIVS_TE_SAMPLER_CONFIG0_ADDRESSING_MODE(TEXTURE_ADDRESSING_MODE_LINEAR)));
   DESC_SET(CONFIG1, COND(ext, VIVS_TE_SAMPLER_CONFIG1_FORMAT_EXT(format)) |
                     COND(astc, VIVS_TE_SAMPLER_CONFIG1_FORMAT_EXT(TEXTURE_FORMAT_EXT_ASTC)) |
                     COND(is_array, VIVS_TE_SAMPLER_CONFIG1_TEXTURE_ARRAY) |
                     VIVS_TE_SAMPLER_CONFIG1_HALIGN(res->halign) | swiz);
   DESC_SET(CONFIG2, 0x00030000 |
         COND(sint && desc->channel[0].size == 8, TE_SAMPLER_CONFIG2_SIGNED_INT8) |
         COND(sint && desc->channel[0].size == 16, TE_SAMPLER_CONFIG2_SIGNED_INT16));
   DESC_SET(LINEAR_STRIDE, res->levels[0].stride);
   DESC_SET(VOLUME, etna_log2_fixp88(base_depth));
   DESC_SET(SLICE, res->levels[0].layer_stride);
   DESC_SET(3D_CONFIG, VIVS_TE_SAMPLER_3D_CONFIG_DEPTH(base_depth));
   DESC_SET(ASTC0, COND(astc, VIVS_NTE_SAMPLER_ASTC0_ASTC_FORMAT(format)) |
                   VIVS_NTE_SAMPLER_ASTC0_UNK8(0xc) |
                   VIVS_NTE_SAMPLER_ASTC0_UNK16(0xc) |
                   VIVS_NTE_SAMPLER_ASTC0_UNK24(0xc));
   DESC_SET(BASELOD, TEXDESC_BASELOD_BASELOD(sv->base.u.tex.first_level) |
                     TEXDESC_BASELOD_MAXLOD(MIN2(sv->base.u.tex.last_level, res->base.last_level)));
   DESC_SET(LOG_SIZE_EXT, TEXDESC_LOG_SIZE_EXT_WIDTH(etna_log2_fixp88(base_width)) |
                          TEXDESC_LOG_SIZE_EXT_HEIGHT(etna_log2_fixp88(base_height)));
   DESC_SET(SIZE, VIVS_TE_SAMPLER_SIZE_WIDTH(base_width) |
                  VIVS_TE_SAMPLER_SIZE_HEIGHT(base_height));
   for (int lod = 0; lod <= res->base.last_level; ++lod)
      DESC_SET(LOD_ADDR(lod), etna_bo_gpu_va(res->bo) + res->levels[lod].offset);
#undef DESC_SET
}

static struct pipe_sampler_view *
etna_create_sampler_view_desc(struct pipe_context *pctx, struct pipe_resource *prsc,
                         const struct pipe_sampler_view *so)
{
   struct etna_sampler_view_desc *sv = CALLOC_STRUCT(etna_sampler_view_desc);
   struct etna_context *ctx = etna_context(pctx);

   if (!sv)
      return NULL;

   /* Determine whether target supported */
   if (translate_texture_target(so->target) == ETNA_NO_MATCH) {
      BUG("Unhandled texture target");
      goto error;
   }

   struct etna_resource *res = etna_texture_handle_incompatible(pctx, prsc);
   if (!res)
      goto error;

   sv->base = *so;
   pipe_reference_init(&sv->base.reference, 1);
   sv->base.texture = NULL;
   pipe_resource_reference(&sv->base.texture, prsc);
   sv->base.context = pctx;
   sv->SAMP_CTRL0_MASK = 0xffffffff;
   sv->composed_key = ~0u;

   /* For RB_SWAP formats, pre-compute the alternative texture format for when
    * shared resources hold data in native byte order (RGBA).
    */
   if (translate_pe_format_rb_swap(so->format)) {
      uint32_t format = translate_texture_format(so->format, ctx->screen);
      sv->native_format = remap_texture_format_rb_swap(format);
   }

   /* Texture descriptor sampler bits */
   if (util_format_is_srgb(so->format))
      sv->SAMP_CTRL1 |= VIVS_NTE_DESCRIPTOR_SAMP_CTRL1_SRGB;

   if (sv->base.target == PIPE_TEXTURE_1D) {
      sv->SAMP_CTRL0_MASK = ~VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_VWRAP__MASK;
      sv->SAMP_CTRL0 = VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_VWRAP(TEXTURE_WRAPMODE_REPEAT);
   }

   if (so->format == PIPE_FORMAT_S8X24_UINT ||
       so->format == PIPE_FORMAT_X32_S8X24_UINT) {
      sv->SAMP_CTRL0_MASK &= ~VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_DEPTH_STENCIL_MODE__MASK;
      sv->SAMP_CTRL0 |= VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_DEPTH_STENCIL_MODE_STENCIL;
   }

   etna_texture_desc_fill(ctx, sv, res, sv->templ);

   return &sv->base;

error:
   pipe_resource_reference(&sv->base.texture, NULL);
   free(sv);
   return NULL;
}

static void
etna_sampler_view_desc_ref_bo(struct etna_context *ctx,
                              struct etna_cmd_stream *stream,
                              struct etna_sampler_view_desc *sv)
{
   struct etna_resource *res = etna_resource(sv->base.texture);

   if (res->texture) {
      res = etna_resource(res->texture);
   }

   /* No need to ref LOD levels individually as they'll always come from the same bo */
   etna_cmd_stream_ref_bo(stream, res->bo, ETNA_RELOC_READ);
}

static void
etna_sampler_view_desc_destroy(struct pipe_context *pctx,
                          struct pipe_sampler_view *so)
{
   struct etna_sampler_view_desc *sv = etna_sampler_view_desc(so);

   pipe_resource_reference(&sv->base.texture, NULL);
   pipe_resource_reference(&sv->res, NULL);
   FREE(sv);
}

static void
emit_desc_sampler_state(struct etna_cmd_stream *stream, unsigned slot,
                        uint32_t SAMP_CTRL0,
                        struct etna_sampler_state_desc *ss,
                        struct etna_sampler_view_desc *sv)
{
   etna_set_state(stream, VIVS_NTE_DESCRIPTOR_TX_CTRL(slot),
      COND(sv->ts.enable, VIVS_NTE_DESCRIPTOR_TX_CTRL_TS_ENABLE) |
      VIVS_NTE_DESCRIPTOR_TX_CTRL_TS_MODE(sv->ts.mode) |
      VIVS_NTE_DESCRIPTOR_TX_CTRL_TS_INDEX(slot) |
      COND(sv->ts.comp, VIVS_NTE_DESCRIPTOR_TX_CTRL_COMPRESSION) |
      COND(!sv->ts.mode, VIVS_NTE_DESCRIPTOR_TX_CTRL_128B_TILE));
   etna_set_state(stream, VIVS_NTE_DESCRIPTOR_SAMP_CTRL0(slot), SAMP_CTRL0);
   etna_set_state(stream, VIVS_NTE_DESCRIPTOR_SAMP_CTRL1(slot), ss->SAMP_CTRL1 | sv->SAMP_CTRL1);
   etna_set_state(stream, VIVS_NTE_DESCRIPTOR_SAMP_LOD_MINMAX(slot), ss->SAMP_LOD_MINMAX);
   etna_set_state(stream, VIVS_NTE_DESCRIPTOR_SAMP_LOD_BIAS(slot), ss->SAMP_LOD_BIAS);
   etna_set_state(stream, VIVS_NTE_DESCRIPTOR_SAMP_ANISOTROPY(slot), ss->SAMP_ANISOTROPY);
}

static uint32_t
emit_desc_sampler_ctrl0(struct etna_sampler_state_desc *ss,
                        struct etna_sampler_view_desc *sv)
{
   uint32_t SAMP_CTRL0 = (ss->SAMP_CTRL0 & sv->SAMP_CTRL0_MASK) | sv->SAMP_CTRL0;

   if (texture_use_int_filter(&sv->base, &ss->base, true))
      SAMP_CTRL0 |= VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_INT_FILTER;

   if (util_format_description(sv->base.format)->colorspace == UTIL_FORMAT_COLORSPACE_ZS &&
       ss->base.min_mip_filter == PIPE_TEX_MIPFILTER_LINEAR) {
      SAMP_CTRL0 &= ~VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_MIP__MASK;
      SAMP_CTRL0 |= VIVS_NTE_DESCRIPTOR_SAMP_CTRL0_MIP(TEXTURE_FILTER_NEAREST);
   }

   return SAMP_CTRL0;
}

/* Map an internal ctx->sampler[] index to its HW descriptor slot. */
static unsigned
etna_sampler_hw_slot(struct etna_context *ctx, unsigned idx)
{
   const unsigned vs_off = ctx->screen->specs.vertex_sampler_offset;

   if (idx < vs_off)
      return idx;

   return idx - vs_off + etna_vs_sampler_base(ctx);
}

#define ETNA_DESC_KEY_SEAMLESS     (1 << 0)
#define ETNA_DESC_KEY_NATIVE_ORDER (1 << 1)

/* Compose the final descriptor(s) from the template and the current
 * dynamic state. Returns true if the descriptor address changed.
 */
static bool
etna_sampler_view_desc_compose(struct etna_context *ctx,
                               struct etna_sampler_view_desc *sv,
                               const struct etna_sampler_state_desc *ss)
{
   struct etna_resource *res = etna_resource(sv->base.texture);
   uint32_t key = 0;

   if (ss->base.seamless_cube_map)
      key |= ETNA_DESC_KEY_SEAMLESS;
   if (sv->native_format && res->shared && res->shared_native_order)
      key |= ETNA_DESC_KEY_NATIVE_ORDER;

   if (key == sv->composed_key)
      return false;

   if (sv->composed_key != ~0u)
      perf_debug_ctx(ctx, "Recomposing texture descriptor (key %x -> %x)",
                     sv->composed_key, key);

   if (res->texture)
      res = etna_resource(res->texture);

   const bool is_128bit = format_is_128bit(sv->base.format);
   const unsigned num_descs = is_128bit ? 2 : 1;
   unsigned offset;

   u_suballocator_alloc(&ctx->tex_desc_allocator,
                        TEXTURE_DESC_SIZE * num_descs, 64, &offset, &sv->res);
   if (!sv->res) {
      sv->DESC_ADDR = ctx->screen->dummy_desc_reloc;
      sv->DESC_ADDR_COMPANION = ctx->screen->dummy_desc_reloc;
      sv->composed_key = ~0u;
      return true;
   }

   uint32_t *buf = etna_bo_map(etna_buffer_resource(sv->res)->bo) + offset;

   memcpy(buf, sv->templ, TEXTURE_DESC_SIZE);

   if (key & ETNA_DESC_KEY_SEAMLESS)
      buf[TEXDESC_CONFIG1 >> 2] |= VIVS_TE_SAMPLER_CONFIG1_SEAMLESS_CUBE_MAP;

   if (key & ETNA_DESC_KEY_NATIVE_ORDER) {
      buf[TEXDESC_CONFIG0 >> 2] =
         (buf[TEXDESC_CONFIG0 >> 2] & ~VIVS_TE_SAMPLER_CONFIG0_FORMAT__MASK) |
         VIVS_TE_SAMPLER_CONFIG0_FORMAT(sv->native_format);
   }

   sv->DESC_ADDR.bo = etna_buffer_resource(sv->res)->bo;
   sv->DESC_ADDR.offset = offset;
   sv->DESC_ADDR.flags = ETNA_RELOC_READ;

   if (is_128bit) {
      uint32_t *companion = buf + (TEXTURE_DESC_SIZE / sizeof(uint32_t));

      memcpy(companion, buf, TEXTURE_DESC_SIZE);
      for (int lod = 0; lod <= res->base.last_level; ++lod)
         companion[TEXDESC_LOD_ADDR(lod) >> 2] +=
            etna_resource_level_second_plane_offset(&res->levels[lod]);

      sv->DESC_ADDR_COMPANION.bo = sv->DESC_ADDR.bo;
      sv->DESC_ADDR_COMPANION.offset = offset + TEXTURE_DESC_SIZE;
      sv->DESC_ADDR_COMPANION.flags = ETNA_RELOC_READ;
   }

   sv->composed_key = key;
   return true;
}

static void
etna_emit_texture_desc(struct etna_context *ctx)
{
   struct etna_cmd_stream *stream = ctx->stream;
   const bool unified = ctx->screen->specs.unified_samplers;
   uint32_t active_samplers = active_samplers_bits(ctx);
   uint32_t dirty = ctx->dirty;
   uint32_t used_hw = 0;

   /* hw slots whose DESC_ADDR was written, they need an invalidate even
    * when not dirty. Bits are hw-slot indices, identical to the view index
    * on the legacy split, unified short-circuits via the unified term.
    */
   uint32_t updated_mask = 0;

   if (unlikely(dirty & ETNA_DIRTY_SAMPLER_VIEWS)) {
      for (int x = 0; x < VIVS_TS_SAMPLER__LEN; ++x) {
         if ((1 << x) & active_samplers) {
            struct etna_sampler_view_desc *sv = etna_sampler_view_desc(ctx->sampler_view[x]);
            struct etna_resource *res = etna_resource(sv->base.texture);
            struct etna_reloc LOD_ADDR_0;

            if (!sv->ts.enable)
               continue;

            etna_set_state(stream, VIVS_TS_SAMPLER_CONFIG(x), sv->ts.TS_SAMPLER_CONFIG);
            etna_set_state_reloc(stream, VIVS_TS_SAMPLER_STATUS_BASE(x), &sv->ts.TS_SAMPLER_STATUS_BASE);
            etna_set_state(stream, VIVS_TS_SAMPLER_CLEAR_VALUE(x), sv->ts.TS_SAMPLER_CLEAR_VALUE);
            etna_set_state(stream, VIVS_TS_SAMPLER_CLEAR_VALUE2(x), sv->ts.TS_SAMPLER_CLEAR_VALUE2);

            LOD_ADDR_0.bo = res->bo;
            LOD_ADDR_0.offset = res->levels[0].offset;
            LOD_ADDR_0.flags = ETNA_RELOC_READ;

            etna_set_state_reloc(stream, VIVS_TS_SAMPLER_SURFACE_BASE(x), &LOD_ADDR_0);
         }
      }
   }

   if (likely(!(dirty & (ETNA_DIRTY_SAMPLERS | ETNA_DIRTY_SAMPLER_VIEWS)))) {
      ctx->prev_active_samplers = active_samplers;
      return;
   }

   /* With unified samplers the fragment and vertex stages share one 32-entry
    * HW array, partitioned dynamically: the shaders use 0-based per-stage
    * indices and VS_SAMPLER_BASE supplies the vertex base.
    * etna_sampler_hw_slot() is the identity for the legacy fixed split, so
    * the body below serves both.
    *
    * The vertex base shifts with the fragment sampler count, so a slot can
    * move between draws even when its own view did not change. The loops
    * below therefore refresh the whole array under unified, not just the
    * dirty slots.
    */
   if (unified)
      etna_set_state(stream, VIVS_VS_SAMPLER_BASE, etna_vs_sampler_base(ctx));

   for (int x = 0; x < PIPE_MAX_SAMPLERS; ++x) {
      if (!((1 << x) & active_samplers))
         continue;

      const unsigned hw = etna_sampler_hw_slot(ctx, x);
      struct etna_sampler_state_desc *ss = etna_sampler_state_desc(ctx->sampler[x]);
      struct etna_sampler_view_desc *sv = etna_sampler_view_desc(ctx->sampler_view[x]);
      uint32_t SAMP_CTRL0 = emit_desc_sampler_ctrl0(ss, sv);

      const bool updated = etna_sampler_view_desc_compose(ctx, sv, ss);
      const bool emit_addr = unified || updated || ((1 << x) & ctx->dirty_sampler_views);

      emit_desc_sampler_state(stream, hw, SAMP_CTRL0, ss, sv);
      if (emit_addr) {
         etna_sampler_view_desc_ref_bo(ctx, stream, sv);
         etna_set_state_reloc(stream, VIVS_NTE_DESCRIPTOR_ADDR(hw), &sv->DESC_ADDR);
         updated_mask |= 1u << hw;
      }
      used_hw |= 1u << hw;

      /* Apply same configuration to the 128-bit companion sampler. */
      if (format_is_128bit(sv->base.format)) {
         const unsigned y = companion_slot(ctx, x);

         assert(!sv->ts.enable);

         if (y != ~0U) {
            emit_desc_sampler_state(stream, y, SAMP_CTRL0, ss, sv);
            if (emit_addr) {
               etna_set_state_reloc(stream, VIVS_NTE_DESCRIPTOR_ADDR(y), &sv->DESC_ADDR_COMPANION);
               updated_mask |= 1u << y;
            }
            used_hw |= 1u << y;
         }
      }
   }

   /* Dummy descriptors for the dirty slots not backed by a sampler/companion. */
   for (int x = 0; x < PIPE_MAX_SAMPLERS; ++x) {
      if (used_hw & (1u << x))
         continue;
      if (unified || ((1 << x) & ctx->dirty_sampler_views))
         etna_set_state_reloc(stream, VIVS_NTE_DESCRIPTOR_ADDR(x),
                              &ctx->screen->dummy_desc_reloc);
   }

   if (unlikely(dirty & ETNA_DIRTY_SAMPLER_VIEWS) || updated_mask) {
      /* Invalidate all dirty and re-addressed sampler views. */
      for (int x = 0; x < PIPE_MAX_SAMPLERS; ++x) {
         if (unified || ((1 << x) & ctx->dirty_sampler_views) ||
             ((1u << x) & updated_mask)) {
            etna_set_state(stream, VIVS_NTE_DESCRIPTOR_INVALIDATE,
                  VIVS_NTE_DESCRIPTOR_INVALIDATE_UNK29 |
                  VIVS_NTE_DESCRIPTOR_INVALIDATE_IDX(x));
         }
      }
   }

   ctx->prev_active_samplers = active_samplers;
}

static struct etna_sampler_ts*
etna_ts_for_sampler_view_state(struct pipe_sampler_view *pview)
{
   struct etna_sampler_view_desc *sv = etna_sampler_view_desc(pview);
   return &sv->ts;
}

void
etna_texture_desc_init(struct pipe_context *pctx)
{
   struct etna_context *ctx = etna_context(pctx);
   DBG("etnaviv: Using descriptor-based texturing\n");
   ctx->base.create_sampler_state = etna_create_sampler_state_desc;
   ctx->base.delete_sampler_state = etna_delete_sampler_state_desc;
   ctx->base.create_sampler_view = etna_create_sampler_view_desc;
   ctx->base.sampler_view_destroy = etna_sampler_view_desc_destroy;
   ctx->base.sampler_view_release = u_default_sampler_view_release;
   ctx->base.resource_release = u_default_resource_release;
   ctx->emit_texture_state = etna_emit_texture_desc;
   ctx->ts_for_sampler_view = etna_ts_for_sampler_view_state;
}
