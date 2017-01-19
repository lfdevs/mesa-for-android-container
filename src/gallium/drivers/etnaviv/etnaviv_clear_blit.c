/*
 * Copyright (c) 2012-2015 Etnaviv Project
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

#include "etnaviv_clear_blit.h"

#include "hw/common.xml.h"

#include "etnaviv_context.h"
#include "etnaviv_emit.h"
#include "etnaviv_emit.h"
#include "etnaviv_format.h"
#include "etnaviv_resource.h"
#include "etnaviv_surface.h"
#include "etnaviv_translate.h"

#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/u_blitter.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_surface.h"

/* Save current state for blitter operation */
static void
etna_blit_save_state(struct etna_context *ctx)
{
   util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vertex_buffer.vb);
   util_blitter_save_vertex_elements(ctx->blitter, ctx->vertex_elements);
   util_blitter_save_vertex_shader(ctx->blitter, ctx->vs);
   util_blitter_save_rasterizer(ctx->blitter, ctx->rasterizer);
   util_blitter_save_viewport(ctx->blitter, &ctx->viewport_s);
   util_blitter_save_scissor(ctx->blitter, &ctx->scissor_s);
   util_blitter_save_fragment_shader(ctx->blitter, ctx->fs);
   util_blitter_save_blend(ctx->blitter, ctx->blend);
   util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->zsa);
   util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref_s);
   util_blitter_save_sample_mask(ctx->blitter, ctx->sample_mask);
   util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer_s);
   util_blitter_save_fragment_sampler_states(ctx->blitter,
         ctx->num_fragment_samplers, (void **)ctx->sampler);
   util_blitter_save_fragment_sampler_views(ctx->blitter,
         ctx->num_fragment_sampler_views, ctx->sampler_view);
}

/* Generate clear command for a surface (non-fast clear case) */
void
etna_rs_gen_clear_surface(struct etna_context *ctx, struct etna_surface *surf,
                          uint32_t clear_value)
{
   struct etna_resource *dst = etna_resource(surf->base.texture);
   uint32_t format = translate_rs_format(surf->base.format);

   if (format == ETNA_NO_MATCH) {
      BUG("etna_rs_gen_clear_surface: Unhandled clear fmt %s", util_format_name(surf->base.format));
      format = RS_FORMAT_A8R8G8B8;
      assert(0);
   }

   /* use tiled clear if width is multiple of 16 */
   bool tiled_clear = (surf->surf.padded_width & ETNA_RS_WIDTH_MASK) == 0 &&
                      (surf->surf.padded_height & ETNA_RS_HEIGHT_MASK) == 0;

   etna_compile_rs_state( ctx, &surf->clear_command, &(struct rs_state) {
      .source_format = format,
      .dest_format = format,
      .dest = dst->bo,
      .dest_offset = surf->surf.offset,
      .dest_stride = surf->surf.stride,
      .dest_padded_height = surf->surf.padded_height,
      .dest_tiling = tiled_clear ? dst->layout : ETNA_LAYOUT_LINEAR,
      .dither = {0xffffffff, 0xffffffff},
      .width = surf->surf.padded_width, /* These must be padded to 16x4 if !LINEAR, otherwise RS will hang */
      .height = surf->surf.padded_height,
      .clear_value = {clear_value},
      .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_ENABLED1,
      .clear_bits = 0xffff
   });
}

static void
etna_blit_clear_color(struct pipe_context *pctx, struct pipe_surface *dst,
                      const union pipe_color_union *color)
{
   struct etna_context *ctx = etna_context(pctx);
   struct etna_surface *surf = etna_surface(dst);
   uint32_t new_clear_value = translate_clear_color(surf->base.format, color);

   if (surf->surf.ts_size) { /* TS: use precompiled clear command */
      ctx->framebuffer.TS_COLOR_CLEAR_VALUE = new_clear_value;

      if (!DBG_ENABLED(ETNA_DBG_NO_AUTODISABLE) &&
          VIV_FEATURE(ctx->screen, chipMinorFeatures1, AUTO_DISABLE)) {
         /* Set number of color tiles to be filled */
         etna_set_state(ctx->stream, VIVS_TS_COLOR_AUTO_DISABLE_COUNT,
                        surf->surf.padded_width * surf->surf.padded_height / 16);
         ctx->framebuffer.TS_MEM_CONFIG |= VIVS_TS_MEM_CONFIG_COLOR_AUTO_DISABLE;
      }

      ctx->dirty |= ETNA_DIRTY_TS;
   } else if (unlikely(new_clear_value != surf->level->clear_value)) { /* Queue normal RS clear for non-TS surfaces */
      /* If clear color changed, re-generate stored command */
      etna_rs_gen_clear_surface(ctx, surf, new_clear_value);
   }

   etna_submit_rs_state(ctx, &surf->clear_command);
   surf->level->clear_value = new_clear_value;
   resource_written(ctx, surf->base.texture);
   etna_resource(surf->base.texture)->seqno++;
}

static void
etna_blit_clear_zs(struct pipe_context *pctx, struct pipe_surface *dst,
                   unsigned buffers, double depth, unsigned stencil)
{
   struct etna_context *ctx = etna_context(pctx);
   struct etna_surface *surf = etna_surface(dst);
   uint32_t new_clear_value = translate_clear_depth_stencil(surf->base.format, depth, stencil);
   uint32_t new_clear_bits = 0, clear_bits_depth, clear_bits_stencil;

   /* Get the channels to clear */
   switch (surf->base.format) {
   case PIPE_FORMAT_Z16_UNORM:
      clear_bits_depth = 0xffff;
      clear_bits_stencil = 0;
      break;
   case PIPE_FORMAT_X8Z24_UNORM:
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      clear_bits_depth = 0xeeee;
      clear_bits_stencil = 0x1111;
      break;
   default:
      clear_bits_depth = clear_bits_stencil = 0xffff;
      break;
   }

   if (buffers & PIPE_CLEAR_DEPTH)
      new_clear_bits |= clear_bits_depth;
   if (buffers & PIPE_CLEAR_STENCIL)
      new_clear_bits |= clear_bits_stencil;

   /* FIXME: when tile status is enabled, this becomes more complex as
    * we may separately clear the depth from the stencil.  In this case,
    * we want to resolve the surface, and avoid using the tile status.
    * We may be better off recording the pending clear operation,
    * delaying the actual clear to the first use.  This way, we can merge
    * consecutive clears together. */
   if (surf->surf.ts_size) { /* TS: use precompiled clear command */
      /* Set new clear depth value */
      ctx->framebuffer.TS_DEPTH_CLEAR_VALUE = new_clear_value;
      if (!DBG_ENABLED(ETNA_DBG_NO_AUTODISABLE) &&
          VIV_FEATURE(ctx->screen, chipMinorFeatures1, AUTO_DISABLE)) {
         /* Set number of depth tiles to be filled */
         etna_set_state(ctx->stream, VIVS_TS_DEPTH_AUTO_DISABLE_COUNT,
                        surf->surf.padded_width * surf->surf.padded_height / 16);
         ctx->framebuffer.TS_MEM_CONFIG |= VIVS_TS_MEM_CONFIG_DEPTH_AUTO_DISABLE;
      }

      ctx->dirty |= ETNA_DIRTY_TS;
   } else {
      if (unlikely(new_clear_value != surf->level->clear_value)) { /* Queue normal RS clear for non-TS surfaces */
         /* If clear depth value changed, re-generate stored command */
         etna_rs_gen_clear_surface(ctx, surf, new_clear_value);
      }
      /* Update the channels to be cleared */
      etna_modify_rs_clearbits(&surf->clear_command, new_clear_bits);
   }

   etna_submit_rs_state(ctx, &surf->clear_command);
   surf->level->clear_value = new_clear_value;
   resource_written(ctx, surf->base.texture);
   etna_resource(surf->base.texture)->seqno++;
}

static void
etna_clear(struct pipe_context *pctx, unsigned buffers,
           const union pipe_color_union *color, double depth, unsigned stencil)
{
   struct etna_context *ctx = etna_context(pctx);

   /* Flush color and depth cache before clearing anything.
    * This is especially important when coming from another surface, as
    * otherwise it may clear part of the old surface instead. */
   etna_set_state(ctx->stream, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);
   etna_stall(ctx->stream, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);

   /* Preparation: Flush the TS if needed. This must be done after flushing
    * color and depth, otherwise it can result in crashes */
   bool need_ts_flush = false;
   if ((buffers & PIPE_CLEAR_COLOR) && ctx->framebuffer_s.nr_cbufs) {
      struct etna_surface *surf = etna_surface(ctx->framebuffer_s.cbufs[0]);
      if (surf->surf.ts_size)
         need_ts_flush = true;
   }
   if ((buffers & PIPE_CLEAR_DEPTHSTENCIL) && ctx->framebuffer_s.zsbuf != NULL) {
      struct etna_surface *surf = etna_surface(ctx->framebuffer_s.zsbuf);

      if (surf->surf.ts_size)
         need_ts_flush = true;
   }

   if (need_ts_flush)
      etna_set_state(ctx->stream, VIVS_TS_FLUSH_CACHE, VIVS_TS_FLUSH_CACHE_FLUSH);

   /* No need to set up the TS here as RS clear operations (in contrast to
    * resolve and copy) do not require the TS state.
    */
   if (buffers & PIPE_CLEAR_COLOR) {
      for (int idx = 0; idx < ctx->framebuffer_s.nr_cbufs; ++idx) {
         etna_blit_clear_color(pctx, ctx->framebuffer_s.cbufs[idx],
                               &color[idx]);
      }
   }

   /* Flush the color and depth caches before each RS clear operation
    * This fixes a hang on GC600. */
   if (buffers & PIPE_CLEAR_DEPTHSTENCIL && buffers & PIPE_CLEAR_COLOR)
      etna_set_state(ctx->stream, VIVS_GL_FLUSH_CACHE,
                     VIVS_GL_FLUSH_CACHE_COLOR | VIVS_GL_FLUSH_CACHE_DEPTH);

   if ((buffers & PIPE_CLEAR_DEPTHSTENCIL) && ctx->framebuffer_s.zsbuf != NULL)
      etna_blit_clear_zs(pctx, ctx->framebuffer_s.zsbuf, buffers, depth, stencil);

   etna_stall(ctx->stream, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
}

static void
etna_clear_render_target(struct pipe_context *pctx, struct pipe_surface *dst,
                         const union pipe_color_union *color, unsigned dstx,
                         unsigned dsty, unsigned width, unsigned height,
                         bool render_condition_enabled)
{
   struct etna_context *ctx = etna_context(pctx);

   /* XXX could fall back to RS when target area is full screen / resolveable
    * and no TS. */
   etna_blit_save_state(ctx);
   util_blitter_clear_render_target(ctx->blitter, dst, color, dstx, dsty, width, height);
}

static void
etna_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *dst,
                         unsigned clear_flags, double depth, unsigned stencil,
                         unsigned dstx, unsigned dsty, unsigned width,
                         unsigned height, bool render_condition_enabled)
{
   struct etna_context *ctx = etna_context(pctx);

   /* XXX could fall back to RS when target area is full screen / resolveable
    * and no TS. */
   etna_blit_save_state(ctx);
   util_blitter_clear_depth_stencil(ctx->blitter, dst, clear_flags, depth,
                                    stencil, dstx, dsty, width, height);
}

static void
etna_resource_copy_region(struct pipe_context *pctx, struct pipe_resource *dst,
                          unsigned dst_level, unsigned dstx, unsigned dsty,
                          unsigned dstz, struct pipe_resource *src,
                          unsigned src_level, const struct pipe_box *src_box)
{
   struct etna_context *ctx = etna_context(pctx);

   /* The resource must be of the same format. */
   assert(src->format == dst->format);
   /* Resources with nr_samples > 1 are not allowed. */
   assert(src->nr_samples <= 1 && dst->nr_samples <= 1);

   /* XXX we can use the RS as a literal copy engine here
    * the only complexity is tiling; the size of the boxes needs to be aligned
    * to the tile size
    * how to handle the case where a resource is copied from/to a non-aligned
    * position?
    * from non-aligned: can fall back to rendering-based copy?
    * to non-aligned: can fall back to rendering-based copy?
    * XXX this goes wrong when source surface is supertiled.
    */
   if (util_blitter_is_copy_supported(ctx->blitter, dst, src)) {
      etna_blit_save_state(ctx);
      util_blitter_copy_texture(ctx->blitter, dst, dst_level, dstx, dsty, dstz,
                                src, src_level, src_box);
   } else {
      util_resource_copy_region(pctx, dst, dst_level, dstx, dsty, dstz, src,
                                src_level, src_box);
   }
}

static bool
etna_manual_blit(struct etna_resource *dst, struct etna_resource_level *dst_lev,
                 unsigned int dst_offset, struct etna_resource *src,
                 struct etna_resource_level *src_lev, unsigned int src_offset,
                 const struct pipe_blit_info *blit_info)
{
   void *smap, *srow, *dmap, *drow;
   size_t tile_size;

   assert(src->layout == ETNA_LAYOUT_TILED);
   assert(dst->layout == ETNA_LAYOUT_TILED);
   assert(src->base.nr_samples == 0);
   assert(dst->base.nr_samples == 0);

   tile_size = util_format_get_blocksize(blit_info->src.format) * 4 * 4;

   smap = etna_bo_map(src->bo);
   if (!smap)
      return false;

   dmap = etna_bo_map(dst->bo);
   if (!dmap)
      return false;

   srow = smap + src_offset;
   drow = dmap + dst_offset;

   etna_bo_cpu_prep(src->bo, DRM_ETNA_PREP_READ);
   etna_bo_cpu_prep(dst->bo, DRM_ETNA_PREP_WRITE);

   for (int y = 0; y < blit_info->src.box.height; y += 4) {
      memcpy(drow, srow, tile_size * blit_info->src.box.width);
      srow += src_lev->stride * 4;
      drow += dst_lev->stride * 4;
   }

   etna_bo_cpu_fini(dst->bo);
   etna_bo_cpu_fini(src->bo);

   return true;
}

static bool
etna_try_rs_blit(struct pipe_context *pctx,
                 const struct pipe_blit_info *blit_info)
{
   struct etna_context *ctx = etna_context(pctx);
   struct etna_resource *src = etna_resource(blit_info->src.resource);
   struct etna_resource *dst = etna_resource(blit_info->dst.resource);
   struct compiled_rs_state copy_to_screen;
   uint32_t ts_mem_config = 0;
   int msaa_xscale = 1, msaa_yscale = 1;

   /* Ensure that the level is valid */
   assert(blit_info->src.level <= src->base.last_level);
   assert(blit_info->dst.level <= dst->base.last_level);

   if (!translate_samples_to_xyscale(src->base.nr_samples, &msaa_xscale, &msaa_yscale, NULL))
      return FALSE;

   /* The width/height are in pixels; they do not change as a result of
    * multi-sampling. So, when blitting from a 4x multisampled surface
    * to a non-multisampled surface, the width and height will be
    * identical. As we do not support scaling, reject different sizes. */
   if (blit_info->dst.box.width != blit_info->src.box.width ||
       blit_info->dst.box.height != blit_info->src.box.height) {
      DBG("scaling requested: source %dx%d destination %dx%d",
          blit_info->src.box.width, blit_info->src.box.height,
          blit_info->dst.box.width, blit_info->dst.box.height);
      return FALSE;
   }

   /* No masks - RS can't copy specific channels */
   unsigned mask = util_format_get_mask(blit_info->dst.format);
   if ((blit_info->mask & mask) != mask) {
      DBG("sub-mask requested: 0x%02x vs format mask 0x%02x", blit_info->mask, mask);
      return FALSE;
   }

   unsigned src_format = etna_compatible_rs_format(blit_info->src.format);
   unsigned dst_format = etna_compatible_rs_format(blit_info->src.format);
   if (translate_rs_format(src_format) == ETNA_NO_MATCH ||
       translate_rs_format(dst_format) == ETNA_NO_MATCH ||
       blit_info->scissor_enable || blit_info->src.box.x != 0 ||
       blit_info->src.box.y != 0 || blit_info->dst.box.x != 0 ||
       blit_info->dst.box.y != 0 ||
       blit_info->dst.box.depth != blit_info->src.box.depth ||
       blit_info->dst.box.depth != 1) {
      return FALSE;
   }

   /* Ensure that the Z coordinate is sane */
   if (dst->base.target != PIPE_TEXTURE_CUBE)
      assert(blit_info->dst.box.z == 0);
   if (src->base.target != PIPE_TEXTURE_CUBE)
      assert(blit_info->src.box.z == 0);

   assert(blit_info->src.box.z < src->base.array_size);
   assert(blit_info->dst.box.z < dst->base.array_size);

   struct etna_resource_level *src_lev = &src->levels[blit_info->src.level];
   struct etna_resource_level *dst_lev = &dst->levels[blit_info->dst.level];

   /* we may be given coordinates up to the padded width to avoid
    * any alignment issues with different tiling formats */
   assert((blit_info->src.box.x + blit_info->src.box.width) * msaa_xscale <= src_lev->padded_width);
   assert((blit_info->src.box.y + blit_info->src.box.height) * msaa_yscale <= src_lev->padded_height);
   assert(blit_info->dst.box.x + blit_info->dst.box.width <= dst_lev->padded_width);
   assert(blit_info->dst.box.y + blit_info->dst.box.height <= dst_lev->padded_height);

   unsigned src_offset =
      src_lev->offset + blit_info->src.box.z * src_lev->layer_stride;
   unsigned dst_offset =
      dst_lev->offset + blit_info->dst.box.z * dst_lev->layer_stride;

   if (src_lev->padded_width <= ETNA_RS_WIDTH_MASK ||
       dst_lev->padded_width <= ETNA_RS_WIDTH_MASK ||
       src_lev->padded_height <= ETNA_RS_HEIGHT_MASK ||
       dst_lev->padded_height <= ETNA_RS_HEIGHT_MASK)
      goto manual;

   /* If the width is not aligned to the RS width, but is within our
    * padding, adjust the width to suite the RS width restriction.
    * Note: the RS width/height are converted to source samples here. */
   unsigned int width = blit_info->src.box.width * msaa_xscale;
   unsigned int height = blit_info->src.box.height * msaa_yscale;
   unsigned int w_align = ETNA_RS_WIDTH_MASK + 1;
   unsigned int h_align = (ETNA_RS_HEIGHT_MASK + 1) * ctx->specs.pixel_pipes;

   if (width & (w_align - 1) && width >= src_lev->width * msaa_xscale && width >= dst_lev->width)
      width = align(width, w_align);

   if (height & (h_align - 1) && height >= src_lev->height * msaa_yscale && height >= dst_lev->height)
      height = align(height, h_align);

   /* The padded dimensions are in samples */
   if (width > src_lev->padded_width ||
       width > dst_lev->padded_width * msaa_xscale ||
       height > src_lev->padded_height ||
       height > dst_lev->padded_height * msaa_yscale)
      goto manual;

   if (src->base.nr_samples > 1) {
      uint32_t msaa_format = translate_msaa_format(src_format);
      assert(msaa_format != ETNA_NO_MATCH);
      ts_mem_config |= VIVS_TS_MEM_CONFIG_MSAA | msaa_format;
   }

   uint32_t to_flush = 0;

   if (src->base.bind & PIPE_BIND_RENDER_TARGET)
      to_flush |= VIVS_GL_FLUSH_CACHE_COLOR;
   if (src->base.bind & PIPE_BIND_DEPTH_STENCIL)
      to_flush |= VIVS_GL_FLUSH_CACHE_DEPTH;

   if (to_flush) {
      etna_set_state(ctx->stream, VIVS_GL_FLUSH_CACHE, to_flush);
      etna_stall(ctx->stream, SYNC_RECIPIENT_RA, SYNC_RECIPIENT_PE);
   }

   /* Set up color TS to source surface before blit, if needed */
   if (src->levels[blit_info->src.level].ts_size) {
      struct etna_reloc reloc;
      unsigned ts_offset =
         src_lev->ts_offset + blit_info->src.box.z * src_lev->ts_layer_stride;

      etna_set_state(ctx->stream, VIVS_TS_MEM_CONFIG,
                     VIVS_TS_MEM_CONFIG_COLOR_FAST_CLEAR | ts_mem_config);

      memset(&reloc, 0, sizeof(struct etna_reloc));
      reloc.bo = src->ts_bo;
      reloc.offset = ts_offset;
      reloc.flags = ETNA_RELOC_READ;
      etna_set_state_reloc(ctx->stream, VIVS_TS_COLOR_STATUS_BASE, &reloc);

      memset(&reloc, 0, sizeof(struct etna_reloc));
      reloc.bo = src->bo;
      reloc.offset = src_offset;
      reloc.flags = ETNA_RELOC_READ;
      etna_set_state_reloc(ctx->stream, VIVS_TS_COLOR_SURFACE_BASE, &reloc);

      etna_set_state(ctx->stream, VIVS_TS_COLOR_CLEAR_VALUE,
                     src->levels[blit_info->src.level].clear_value);
   } else {
      etna_set_state(ctx->stream, VIVS_TS_MEM_CONFIG, ts_mem_config);
   }
   ctx->dirty |= ETNA_DIRTY_TS;

   /* Kick off RS here */
   etna_compile_rs_state(ctx, &copy_to_screen, &(struct rs_state) {
      .source_format = translate_rs_format(src_format),
      .source_tiling = src->layout,
      .source = src->bo,
      .source_offset = src_offset,
      .source_stride = src_lev->stride,
      .source_padded_height = src_lev->padded_height,
      .dest_format = translate_rs_format(dst_format),
      .dest_tiling = dst->layout,
      .dest = dst->bo,
      .dest_offset = dst_offset,
      .dest_stride = dst_lev->stride,
      .dest_padded_height = dst_lev->padded_height,
      .downsample_x = msaa_xscale > 1,
      .downsample_y = msaa_yscale > 1,
      .swap_rb = translate_rb_src_dst_swap(src->base.format, dst->base.format),
      .dither = {0xffffffff, 0xffffffff}, // XXX dither when going from 24 to 16 bit?
      .clear_mode = VIVS_RS_CLEAR_CONTROL_MODE_DISABLED,
      .width = width,
      .height = height
   });

   etna_submit_rs_state(ctx, &copy_to_screen);
   resource_written(ctx, &dst->base);
   dst->seqno++;

   return TRUE;

manual:
   if (src->layout == ETNA_LAYOUT_TILED && dst->layout == ETNA_LAYOUT_TILED) {
      etna_resource_wait(pctx, dst);
      etna_resource_wait(pctx, src);
      return etna_manual_blit(dst, dst_lev, dst_offset, src, src_lev, src_offset, blit_info);
   }

   return FALSE;
}

static void
etna_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
   /* This is a more extended version of resource_copy_region */
   /* TODO Some cases can be handled by RS; if not, fall back to rendering or
    * even CPU copy block of pixels from info->src to info->dst
    * (resource, level, box, format);
    * function is used for scaling, flipping in x and y direction (negative
    * width/height), format conversion, mask and filter and even a scissor rectangle
    *
    * What can the RS do for us:
    *   convert between tiling formats (layouts)
    *   downsample 2x in x and y
    *   convert between a limited number of pixel formats
    *
    * For the rest, fall back to util_blitter
    * XXX this goes wrong when source surface is supertiled. */
   struct etna_context *ctx = etna_context(pctx);
   struct pipe_blit_info info = *blit_info;

   if (info.src.resource->nr_samples > 1 &&
       info.dst.resource->nr_samples <= 1 &&
       !util_format_is_depth_or_stencil(info.src.resource->format) &&
       !util_format_is_pure_integer(info.src.resource->format)) {
      DBG("color resolve unimplemented");
      return;
   }

   if (etna_try_rs_blit(pctx, blit_info))
      return;

   if (util_try_blit_via_copy_region(pctx, blit_info))
      return;

   if (info.mask & PIPE_MASK_S) {
      DBG("cannot blit stencil, skipping");
      info.mask &= ~PIPE_MASK_S;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
      DBG("blit unsupported %s -> %s",
          util_format_short_name(info.src.resource->format),
          util_format_short_name(info.dst.resource->format));
      return;
   }

   etna_blit_save_state(ctx);
   util_blitter_blit(ctx->blitter, &info);
}

static void
etna_flush_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
   struct etna_resource *rsc = etna_resource(prsc);

   if (rsc->scanout)
      etna_copy_resource(pctx, rsc->scanout->prime, prsc, 0, 0);
}

void
etna_copy_resource(struct pipe_context *pctx, struct pipe_resource *dst,
                   struct pipe_resource *src, int first_level, int last_level)
{
   struct etna_resource *src_priv = etna_resource(src);
   struct etna_resource *dst_priv = etna_resource(dst);

   assert(src->format == dst->format);
   assert(src->array_size == dst->array_size);
   assert(last_level <= dst->last_level && last_level <= src->last_level);

   struct pipe_blit_info blit = {};
   blit.mask = util_format_get_mask(dst->format);
   blit.filter = PIPE_TEX_FILTER_NEAREST;
   blit.src.resource = src;
   blit.src.format = src->format;
   blit.dst.resource = dst;
   blit.dst.format = dst->format;
   blit.dst.box.depth = blit.src.box.depth = 1;

   /* Copy each level and each layer */
   for (int level = first_level; level <= last_level; level++) {
      blit.src.level = blit.dst.level = level;
      blit.src.box.width = blit.dst.box.width =
         MIN2(src_priv->levels[level].width, dst_priv->levels[level].width);
      blit.src.box.height = blit.dst.box.height =
         MIN2(src_priv->levels[level].height, dst_priv->levels[level].height);

      for (int layer = 0; layer < dst->array_size; layer++) {
         blit.src.box.z = blit.dst.box.z = layer;
         pctx->blit(pctx, &blit);
      }
   }
}

void
etna_clear_blit_init(struct pipe_context *pctx)
{
   pctx->clear = etna_clear;
   pctx->clear_render_target = etna_clear_render_target;
   pctx->clear_depth_stencil = etna_clear_depth_stencil;
   pctx->resource_copy_region = etna_resource_copy_region;
   pctx->blit = etna_blit;
   pctx->flush_resource = etna_flush_resource;
}
