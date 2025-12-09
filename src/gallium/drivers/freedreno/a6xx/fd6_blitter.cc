/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include "util/format_srgb.h"
#include "util/half_float.h"
#include "util/u_dump.h"
#include "util/u_helpers.h"
#include "util/u_log.h"
#include "util/u_transfer.h"
#include "util/u_surface.h"

#include "freedreno_blitter.h"
#include "freedreno_fence.h"
#include "freedreno_resource.h"
#include "freedreno_tracepoints.h"

#include "fd6_barrier.h"
#include "fd6_blitter.h"
#include "fd6_emit.h"
#include "fd6_pack.h"
#include "fd6_resource.h"

static inline enum a6xx_2d_ifmt
fd6_ifmt(enum a6xx_format fmt)
{
   switch (fmt) {
   case FMT6_A8_UNORM:
   case FMT6_8_UNORM:
   case FMT6_8_SNORM:
   case FMT6_8_8_UNORM:
   case FMT6_8_8_SNORM:
   case FMT6_8_8_8_8_UNORM:
   case FMT6_8_8_8_X8_UNORM:
   case FMT6_8_8_8_8_SNORM:
   case FMT6_4_4_4_4_UNORM:
   case FMT6_5_5_5_1_UNORM:
   case FMT6_5_6_5_UNORM:
      return R2D_UNORM8;

   case FMT6_32_UINT:
   case FMT6_32_SINT:
   case FMT6_32_32_UINT:
   case FMT6_32_32_SINT:
   case FMT6_32_32_32_32_UINT:
   case FMT6_32_32_32_32_SINT:
      return R2D_INT32;

   case FMT6_16_UINT:
   case FMT6_16_SINT:
   case FMT6_16_16_UINT:
   case FMT6_16_16_SINT:
   case FMT6_16_16_16_16_UINT:
   case FMT6_16_16_16_16_SINT:
   case FMT6_10_10_10_2_UINT:
      return R2D_INT16;

   case FMT6_8_UINT:
   case FMT6_8_SINT:
   case FMT6_8_8_UINT:
   case FMT6_8_8_SINT:
   case FMT6_8_8_8_8_UINT:
   case FMT6_8_8_8_8_SINT:
   case FMT6_Z24_UNORM_S8_UINT:
   case FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8:
      return R2D_INT8;

   case FMT6_16_UNORM:
   case FMT6_16_SNORM:
   case FMT6_16_16_UNORM:
   case FMT6_16_16_SNORM:
   case FMT6_16_16_16_16_UNORM:
   case FMT6_16_16_16_16_SNORM:
   case FMT6_32_FLOAT:
   case FMT6_32_32_FLOAT:
   case FMT6_32_32_32_32_FLOAT:
      return R2D_FLOAT32;

   case FMT6_16_FLOAT:
   case FMT6_16_16_FLOAT:
   case FMT6_16_16_16_16_FLOAT:
   case FMT6_11_11_10_FLOAT:
   case FMT6_10_10_10_2_UNORM_DEST:
      return R2D_FLOAT16;

   default:
      UNREACHABLE("bad format");
      return (enum a6xx_2d_ifmt)0;
   }
}

/* Make sure none of the requested dimensions extend beyond the size of the
 * resource.  Not entirely sure why this happens, but sometimes it does, and
 * w/ 2d blt doesn't have wrap modes like a sampler, so force those cases
 * back to u_blitter
 */
static bool
ok_dims(const struct pipe_resource *r, const struct pipe_box *b, int lvl)
{
   int last_layer =
      r->target == PIPE_TEXTURE_3D ? u_minify(r->depth0, lvl) : r->array_size;

   return (b->x >= 0) && (b->x + b->width <= u_minify(r->width0, lvl)) &&
          (b->y >= 0) && (b->y + b->height <= u_minify(r->height0, lvl)) &&
          (b->z >= 0) && (b->z + b->depth <= last_layer);
}

static bool
ok_format(enum pipe_format pfmt)
{
   enum a6xx_format fmt = fd6_color_format(pfmt, TILE6_LINEAR);

   if (util_format_is_compressed(pfmt))
      return true;

   switch (pfmt) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z16_UNORM:
   case PIPE_FORMAT_Z32_UNORM:
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
   case PIPE_FORMAT_S8_UINT:
      return true;
   default:
      break;
   }

   if (fmt == FMT6_NONE)
      return false;

   return true;
}

#define DEBUG_BLIT          0
#define DEBUG_BLIT_FALLBACK 0

#define fail_if(cond)                                                          \
   do {                                                                        \
      if (cond) {                                                              \
         if (DEBUG_BLIT_FALLBACK) {                                            \
            fprintf(stderr, "falling back: %s for blit:\n", #cond);            \
            dump_blit_info(info);                                              \
         }                                                                     \
         return false;                                                         \
      }                                                                        \
   } while (0)

static bool
is_ubwc(struct pipe_resource *prsc, unsigned level)
{
   return fd_resource_ubwc_enabled(fd_resource(prsc), level);
}

static void
dump_blit_info(const struct pipe_blit_info *info)
{
   util_dump_blit_info(stderr, info);
   fprintf(stderr, "\n\tdst resource: ");
   util_dump_resource(stderr, info->dst.resource);
   if (is_ubwc(info->dst.resource, info->dst.level))
      fprintf(stderr, " (ubwc)");
   fprintf(stderr, "\n\tsrc resource: ");
   util_dump_resource(stderr, info->src.resource);
   if (is_ubwc(info->src.resource, info->src.level))
      fprintf(stderr, " (ubwc)");
   fprintf(stderr, "\n\n");
}

static bool
can_do_blit(const struct pipe_blit_info *info)
{
   /* I think we can do scaling, but not in z dimension since that would
    * require blending..
    */
   fail_if(info->dst.box.depth != info->src.box.depth);

   /* Fail if unsupported format: */
   fail_if(!ok_format(info->src.format));
   fail_if(!ok_format(info->dst.format));

   /* using the 2d path seems to canonicalize NaNs when the source format
    * is a 16-bit floating point format, likely because it implicitly
    * converts to 32 bits.
    */
   fail_if(util_format_is_float16(info->src.format) &&
           util_format_is_float16(info->dst.format));

   assert(!util_format_is_compressed(info->src.format));
   assert(!util_format_is_compressed(info->dst.format));

   fail_if(!ok_dims(info->src.resource, &info->src.box, info->src.level));

   /* We _shouldn't_ be getting negative dst coords, but do as a result of
    * y-flip in do_blit_framebuffer().  See
    * dEQP-GLES31.functional.primitive_bounding_box.blit_fbo.blit_fbo_to_default
    */
   fail_if(info->dst.box.x < 0);
   fail_if(info->dst.box.y < 0);

   assert(info->dst.box.width >= 0);
   assert(info->dst.box.height >= 0);
   assert(info->dst.box.depth >= 0);

   fail_if(info->dst.resource->nr_samples > 1);
   fail_if(info->src.resource->nr_samples > 1);

   fail_if(info->window_rectangle_include);

   /* The blitter can't handle the needed swizzle gymnastics to convert
    * to/from L/A formats:
    */
   fail_if(info->swizzle_enable);
   if (info->src.format != info->dst.format) {
      fail_if(util_format_is_luminance(info->dst.format));
      fail_if(util_format_is_alpha(info->dst.format));
      fail_if(util_format_is_luminance_alpha(info->dst.format));
      fail_if(util_format_is_luminance(info->src.format));
      fail_if(util_format_is_alpha(info->src.format));
      fail_if(util_format_is_luminance_alpha(info->src.format));
   }

   const struct util_format_description *src_desc =
      util_format_description(info->src.format);
   const struct util_format_description *dst_desc =
      util_format_description(info->dst.format);
   const int common_channels =
      MIN2(src_desc->nr_channels, dst_desc->nr_channels);

   if (info->mask & PIPE_MASK_RGBA) {
      for (int i = 0; i < common_channels; i++) {
         fail_if(memcmp(&src_desc->channel[i], &dst_desc->channel[i],
                        sizeof(src_desc->channel[0])));
      }
   }

   fail_if(info->alpha_blend);

   return true;
}

static bool
can_do_clear(const struct pipe_resource *prsc, unsigned level,
             const struct pipe_box *box)
{
   return ok_format(prsc->format) &&
          ok_dims(prsc, box, level) &&
          (fd_resource_nr_samples(prsc) == 1);

   return true;
}

template <chip CHIP>
static void
emit_setup(struct fd_context *ctx, fd_cs &cs)
{
   fd6_emit_flushes<CHIP>(ctx, cs,
                          FD6_FLUSH_CCU_COLOR |
                          FD6_INVALIDATE_CCU_COLOR |
                          FD6_FLUSH_CCU_DEPTH |
                          FD6_INVALIDATE_CCU_DEPTH);

   fd6_set_render_mode<CHIP>(cs, {RM6_BLIT2DSCALE});

   /* normal BLIT_OP_SCALE operation needs bypass RB_CCU_CNTL */
   fd6_emit_gmem_cache_cntl<CHIP>(cs, ctx->screen, false);
}

template <chip CHIP>
static void
emit_blit_fini(struct fd_context *ctx, fd_cs &cs)
{
   fd6_event_write<CHIP>(ctx, cs, FD_LABEL);

   fd6_set_rb_dbg_eco_mode<CHIP>(ctx, cs, true);

   fd_pkt7(cs, CP_BLIT, 1)
      .add(CP_BLIT_0(.op = BLIT_OP_SCALE));

   fd6_set_rb_dbg_eco_mode<CHIP>(ctx, cs, false);
}

/* nregs: 5 */
template <chip CHIP>
static void
emit_blit_setup(fd_ncrb<CHIP> &ncrb, enum pipe_format pfmt,
                bool scissor_enable, union pipe_color_union *color,
                uint32_t unknown_8c01, enum a6xx_rotation rotate)
{
   enum a6xx_format fmt = fd6_color_format(pfmt, TILE6_LINEAR);
   bool is_srgb = util_format_is_srgb(pfmt);
   enum a6xx_2d_ifmt ifmt = fd6_ifmt(fmt);

   if (is_srgb) {
      assert(ifmt == R2D_UNORM8);
      ifmt = R2D_UNORM8_SRGB;
   }

   uint32_t blit_cntl = A6XX_RB_A2D_BLT_CNTL_MASK(0xf) |
                        A6XX_RB_A2D_BLT_CNTL_COLOR_FORMAT(fmt) |
                        A6XX_RB_A2D_BLT_CNTL_IFMT(ifmt) |
                        A6XX_RB_A2D_BLT_CNTL_ROTATE(rotate) |
                        COND(color, A6XX_RB_A2D_BLT_CNTL_SOLID_COLOR) |
                        COND(scissor_enable, A6XX_RB_A2D_BLT_CNTL_SCISSOR);

   ncrb.add(A6XX_RB_A2D_BLT_CNTL(.dword = blit_cntl));
   ncrb.add(GRAS_A2D_BLT_CNTL(CHIP, .dword = blit_cntl));

   if (CHIP >= A7XX) {
      ncrb.add(TPL1_A2D_BLT_CNTL(CHIP,
         .raw_copy = false,
         .start_offset_texels = 0,
         .type = A6XX_TEX_2D,
      ));
   }

   if (fmt == FMT6_10_10_10_2_UNORM_DEST)
      fmt = FMT6_16_16_16_16_FLOAT;

   enum a6xx_sp_a2d_output_ifmt_type output_ifmt_type;
   if (util_format_is_pure_uint(pfmt))
      output_ifmt_type = OUTPUT_IFMT_2D_UINT;
   else if (util_format_is_pure_sint(pfmt))
      output_ifmt_type = OUTPUT_IFMT_2D_SINT;
   else
      output_ifmt_type = OUTPUT_IFMT_2D_FLOAT;

   /* This register is probably badly named... it seems that it's
    * controlling the internal/accumulator format or something like
    * that. It's certainly not tied to only the src format.
    */
   ncrb.add(SP_A2D_OUTPUT_INFO(CHIP,
      .ifmt_type = output_ifmt_type,
      .color_format = fmt,
      .srgb = is_srgb,
      .mask = 0xf,
   ));

   ncrb.add(A6XX_RB_A2D_PIXEL_CNTL(.dword = unknown_8c01));
}

/* nregs: 4 */
template <chip CHIP>
static void
emit_blit_buffer_dst(fd_ncrb<CHIP> &ncrb, struct fd_resource *dst,
                     unsigned off, unsigned size, a6xx_format color_format)
{
   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_INFO(
      .color_format = color_format,
      .tile_mode = TILE6_LINEAR,
      .color_swap = WZYX,
   ));
   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_BASE(
      .bo = dst->bo,
      .bo_offset = off,
   ));
   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_PITCH(size));
}

/* buffers need to be handled specially since x/width can exceed the bounds
 * supported by hw.. if necessary decompose into (potentially) two 2D blits
 */
template <chip CHIP>
static void
emit_blit_buffer(struct fd_context *ctx, fd_cs &cs, const struct pipe_blit_info *info)
{
   const struct pipe_box *sbox = &info->src.box;
   const struct pipe_box *dbox = &info->dst.box;
   struct fd_resource *src, *dst;
   unsigned sshift, dshift;

   if (DEBUG_BLIT) {
      fprintf(stderr, "buffer blit: ");
      dump_blit_info(info);
   }

   src = fd_resource(info->src.resource);
   dst = fd_resource(info->dst.resource);

   assert(src->layout.cpp == 1);
   assert(dst->layout.cpp == 1);
   assert(info->src.resource->format == info->dst.resource->format);
   assert((sbox->y == 0) && (sbox->height == 1));
   assert((dbox->y == 0) && (dbox->height == 1));
   assert((sbox->z == 0) && (sbox->depth == 1));
   assert((dbox->z == 0) && (dbox->depth == 1));
   assert(sbox->width == dbox->width);
   assert(info->src.level == 0);
   assert(info->dst.level == 0);

   /*
    * Buffers can have dimensions bigger than max width, remap into
    * multiple 1d blits to fit within max dimension
    *
    * Note that blob uses .ARRAY_PITCH=128 for blitting buffers, which
    * seems to prevent overfetch related faults.  Not quite sure what
    * the deal is there.
    *
    * Low 6 bits of SRC/DST addresses need to be zero (ie. address
    * aligned to 64) so we need to shift src/dst x1/x2 to make up the
    * difference.  On top of already splitting up the blit so width
    * isn't > 16k.
    *
    * We perhaps could do a bit better, if src and dst are aligned but
    * in the worst case this means we have to split the copy up into
    * 16k (0x4000) minus 64 (0x40).
    */

   sshift = sbox->x & 0x3f;
   dshift = dbox->x & 0x3f;

   with_ncrb (cs, 5)
      emit_blit_setup<CHIP>(ncrb, PIPE_FORMAT_R8_UNORM, false, NULL, 0, ROTATE_0);

   for (unsigned off = 0; off < sbox->width; off += (0x4000 - 0x40)) {
      unsigned soff, doff, w, p;

      soff = (sbox->x + off) & ~0x3f;
      doff = (dbox->x + off) & ~0x3f;

      w = MIN2(sbox->width - off, (0x4000 - 0x40));
      p = align(w, 64);

      assert((soff + w) <= fd_bo_size(src->bo));
      assert((doff + w) <= fd_bo_size(dst->bo));

      with_ncrb (cs, 15) {
         /*
          * Emit source:
          */
         ncrb.add(TPL1_A2D_SRC_TEXTURE_INFO(CHIP,
            .color_format = FMT6_8_UNORM,
            .tile_mode = TILE6_LINEAR,
            .color_swap = WZYX,
            .unk20 = true,
            .unk22 = true,
         ));
         ncrb.add(TPL1_A2D_SRC_TEXTURE_SIZE(CHIP,
            .width = sshift + w,
            .height = 1,
         ));
         ncrb.add(TPL1_A2D_SRC_TEXTURE_BASE(CHIP,
            .bo = src->bo,
            .bo_offset = soff,
         ));
         ncrb.add(TPL1_A2D_SRC_TEXTURE_PITCH(CHIP, .pitch = p));

         /*
          * Emit destination:
          */
         emit_blit_buffer_dst<CHIP>(ncrb, dst, doff, p, FMT6_8_UNORM);

         ncrb.add(GRAS_A2D_SRC_XMIN(CHIP, sshift));
         ncrb.add(GRAS_A2D_SRC_XMAX(CHIP, sshift + w - 1));
         ncrb.add(GRAS_A2D_SRC_YMIN(CHIP, 0));
         ncrb.add(GRAS_A2D_SRC_YMAX(CHIP, 0));

         ncrb.add(GRAS_A2D_DEST_TL(CHIP, .x = dshift));
         ncrb.add(GRAS_A2D_DEST_BR(CHIP, .x = dshift + w - 1));
      }

      /*
       * Blit command:
       */
      emit_blit_fini<CHIP>(ctx, cs);
   }
}

template <chip CHIP>
static void
clear_ubwc_setup(fd_cs &cs)
{
   union pipe_color_union color = {};
   fd_ncrb<CHIP> ncrb(cs, 18);

   emit_blit_setup<CHIP>(ncrb, PIPE_FORMAT_R8_UNORM, false, &color, 0, ROTATE_0);

   ncrb.add(TPL1_A2D_SRC_TEXTURE_INFO(CHIP));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_SIZE(CHIP));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_BASE(CHIP));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_PITCH(CHIP));

   ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW0());
   ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW1());
   ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW2());
   ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW3());

   ncrb.add(GRAS_A2D_SRC_XMIN(CHIP, 0));
   ncrb.add(GRAS_A2D_SRC_XMAX(CHIP, 0));
   ncrb.add(GRAS_A2D_SRC_YMIN(CHIP, 0));
   ncrb.add(GRAS_A2D_SRC_YMAX(CHIP, 0));
}

template <chip CHIP>
static void
fd6_clear_ubwc(struct fd_batch *batch, struct fd_resource *rsc) assert_dt
{
   fd_cs cs(fd_batch_get_prologue(batch));

   fd6_set_render_mode<CHIP>(cs, {RM6_BLIT2DSCALE});

   clear_ubwc_setup<CHIP>(cs);

   unsigned size = rsc->layout.slices[0].offset;
   unsigned offset = 0;

   /* We could be more clever here and realize that we could use a
    * larger width if the size is aligned to something more than a
    * single page.. or even use a format larger than r8 in those
    * cases. But for normal sized textures and even up to 16k x 16k
    * at <= 4byte/pixel, we'll only go thru the loop once
    */
   const unsigned w = 0x1000;

   /* ubwc size should always be page aligned: */
   assert((size % w) == 0);

   while (size > 0) {
      const unsigned h = MIN2(0x4000, size / w);
      /* width is already aligned to a suitable pitch: */
      const unsigned p = w;

      with_ncrb (cs, 6) {
         /*
          * Emit destination:
          */
         emit_blit_buffer_dst<CHIP>(ncrb, rsc, offset, p, FMT6_8_UNORM);

         ncrb.add(GRAS_A2D_DEST_TL(CHIP, .x = 0,     .y = 0));
         ncrb.add(GRAS_A2D_DEST_BR(CHIP, .x = w - 1, .y = h - 1));
      }

      /*
       * Blit command:
       */
      emit_blit_fini<CHIP>(batch->ctx, cs);
      offset += w * h;
      size -= w * h;
   }

   fd6_emit_flushes<CHIP>(batch->ctx, cs,
                          FD6_FLUSH_CCU_COLOR |
                          FD6_FLUSH_CCU_DEPTH |
                          FD6_FLUSH_CACHE |
                          FD6_WAIT_FOR_IDLE);
}

/* nregs: 10 */
template <chip CHIP>
static void
emit_blit_dst(fd_ncrb<CHIP> &ncrb, struct pipe_resource *prsc,
              enum pipe_format pfmt, unsigned level, unsigned layer)
{
   struct fd_resource *dst = fd_resource(prsc);
   enum a6xx_format fmt =
         fd6_color_format(pfmt, (enum a6xx_tile_mode)dst->layout.tile_mode);
   enum a6xx_tile_mode tile =
         (enum a6xx_tile_mode)fd_resource_tile_mode(prsc, level);
   enum a3xx_color_swap swap =
         fd6_color_swap(pfmt, (enum a6xx_tile_mode)dst->layout.tile_mode,
                        false);
   uint32_t pitch = fd_resource_pitch(dst, level);
   bool ubwc_enabled = fd_resource_ubwc_enabled(dst, level);
   unsigned off = fd_resource_offset(dst, level, layer);

   if (fmt == FMT6_Z24_UNORM_S8_UINT)
      fmt = FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;

   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_INFO(
      .color_format = fmt,
      .tile_mode = tile,
      .color_swap = swap,
      .flags = ubwc_enabled,
      .srgb = util_format_is_srgb(pfmt),
   ));
   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_BASE(
      .bo = dst->bo,
      .bo_offset = off,
   ));
   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_PITCH(pitch));

   if (ubwc_enabled) {
      ncrb.add(A6XX_RB_A2D_DEST_FLAG_BUFFER_BASE(
         dst->bo, fd_resource_ubwc_offset(dst, level, layer)
      ));
      ncrb.add(A6XX_RB_A2D_DEST_FLAG_BUFFER_PITCH(
         .pitch = fdl_ubwc_pitch(&dst->layout, level),
         .array_pitch = dst->layout.ubwc_layer_size >> 2,
      ));
      ncrb.add(A6XX_RB_A2D_DEST_FLAG_BUFFER_BASE_1());
      ncrb.add(A6XX_RB_A2D_DEST_FLAG_BUFFER_PITCH_1());
   }
}

/* nregs: 8 */
template <chip CHIP>
static void
emit_blit_src(fd_ncrb<CHIP> &ncrb, const struct pipe_blit_info *info,
              unsigned layer, unsigned nr_samples)
{
   struct fd_resource *src = fd_resource(info->src.resource);
   enum a6xx_format sfmt =
      fd6_texture_format(info->src.format, (enum a6xx_tile_mode)src->layout.tile_mode, false);
   enum a6xx_tile_mode stile =
      (enum a6xx_tile_mode)fd_resource_tile_mode(info->src.resource, info->src.level);
   enum a3xx_color_swap sswap =
      fd6_texture_swap(info->src.format, (enum a6xx_tile_mode)src->layout.tile_mode, false);
   uint32_t pitch = fd_resource_pitch(src, info->src.level);
   bool subwc_enabled = fd_resource_ubwc_enabled(src, info->src.level);
   unsigned soff = fd_resource_offset(src, info->src.level, layer);
   uint32_t width = u_minify(src->b.b.width0, info->src.level) * nr_samples;
   uint32_t height = u_minify(src->b.b.height0, info->src.level);
   enum a3xx_msaa_samples samples = fd_msaa_samples(src->b.b.nr_samples);

   if (info->src.format == PIPE_FORMAT_A8_UNORM)
      sfmt = FMT6_A8_UNORM;

   ncrb.add(TPL1_A2D_SRC_TEXTURE_INFO(CHIP,
      .color_format = sfmt,
      .tile_mode = stile,
      .color_swap = sswap,
      .flags = subwc_enabled,
      .srgb  = util_format_is_srgb(info->src.format),
      .samples = samples,
      .filter = (info->filter == PIPE_TEX_FILTER_LINEAR),
      .samples_average = (samples > MSAA_ONE) && !info->sample0_only,
      .unk20 = true,
      .unk22 = true,
   ));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_SIZE(CHIP, .width = width, .height = height));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_BASE(CHIP, .bo = src->bo, .bo_offset = soff));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_PITCH(CHIP, .pitch = pitch));

   if (subwc_enabled && fd_resource_ubwc_enabled(src, info->src.level)) {
      ncrb.add(TPL1_A2D_SRC_TEXTURE_FLAG_BASE(CHIP,
         .bo = src->bo,
         .bo_offset = fd_resource_ubwc_offset(src, info->src.level, layer),
      ));
      ncrb.add(TPL1_A2D_SRC_TEXTURE_FLAG_PITCH(CHIP,
         fdl_ubwc_pitch(&src->layout, info->src.level),
      ));
   }
}

template <chip CHIP>
static void
emit_blit_texture_setup(fd_cs &cs, const struct pipe_blit_info *info)
{
   const struct pipe_box *sbox = &info->src.box;
   const struct pipe_box *dbox = &info->dst.box;
   struct fd_resource *dst;
   int sx1, sy1, sx2, sy2;
   int dx1, dy1, dx2, dy2;

   dst = fd_resource(info->dst.resource);

   uint32_t nr_samples = fd_resource_nr_samples(&dst->b.b);

   sx1 = sbox->x * nr_samples;
   sy1 = sbox->y;
   sx2 = (sbox->x + sbox->width) * nr_samples;
   sy2 = sbox->y + sbox->height;

   dx1 = dbox->x * nr_samples;
   dy1 = dbox->y;
   dx2 = (dbox->x + dbox->width) * nr_samples;
   dy2 = dbox->y + dbox->height;

   static const enum a6xx_rotation rotates[2][2] = {
      {ROTATE_0, ROTATE_HFLIP},
      {ROTATE_VFLIP, ROTATE_180},
   };
   bool mirror_x = (sx2 < sx1) != (dx2 < dx1);
   bool mirror_y = (sy2 < sy1) != (dy2 < dy1);

   enum a6xx_rotation rotate = rotates[mirror_y][mirror_x];

   fd_ncrb<CHIP> ncrb(cs, 13);

   ncrb.add(GRAS_A2D_SRC_XMIN(CHIP, MIN2(sx1, sx2)));
   ncrb.add(GRAS_A2D_SRC_XMAX(CHIP, MAX2(sx1, sx2) - 1));
   ncrb.add(GRAS_A2D_SRC_YMIN(CHIP, MIN2(sy1, sy2)));
   ncrb.add(GRAS_A2D_SRC_YMAX(CHIP, MAX2(sy1, sy2) - 1));

   ncrb.add(GRAS_A2D_DEST_TL(CHIP, .x = MIN2(dx1, dx2), .y = MIN2(dy1, dy2)));
   ncrb.add(GRAS_A2D_DEST_BR(CHIP, .x = MAX2(dx1, dx2) - 1, .y = MAX2(dy1, dy2) - 1));

   if (info->scissor_enable) {
      ncrb.add(GRAS_A2D_SCISSOR_TL(CHIP,
         .x = info->scissor.minx,
         .y = info->scissor.miny,
      ));
      ncrb.add(GRAS_A2D_SCISSOR_BR(CHIP,
         .x = info->scissor.maxx - 1,
         .y = info->scissor.maxy - 1,
      ));
   }

   emit_blit_setup<CHIP>(ncrb, info->dst.format, info->scissor_enable, NULL, 0, rotate);
}

template <chip CHIP>
static void
emit_blit_texture(struct fd_context *ctx, fd_cs &cs, const struct pipe_blit_info *info)
{
   const struct pipe_box *sbox = &info->src.box;
   const struct pipe_box *dbox = &info->dst.box;
   struct fd_resource *dst;

   if (DEBUG_BLIT) {
      fprintf(stderr, "texture blit: ");
      dump_blit_info(info);
   }

   emit_blit_texture_setup<CHIP>(cs, info);

   dst = fd_resource(info->dst.resource);

   uint32_t nr_samples = fd_resource_nr_samples(&dst->b.b);

   for (unsigned i = 0; i < info->dst.box.depth; i++) {
      with_ncrb (cs, 18) {
         emit_blit_src<CHIP>(ncrb, info, sbox->z + i, nr_samples);
         emit_blit_dst(ncrb, info->dst.resource, info->dst.format, info->dst.level,
                       dbox->z + i);
      }

      emit_blit_fini<CHIP>(ctx, cs);
   }
}

static inline uint32_t
float_to_sbyte(float f)
{
   return util_iround(CLAMP(f, -1.0f, 1.0f) * 0x7f) & 0xff;
}

/* nregs: 4 */
template <chip CHIP>
static void
emit_clear_color(fd_ncrb<CHIP> &ncrb, enum pipe_format pfmt,
                 union pipe_color_union *color)
{
   switch (pfmt) {
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_X24S8_UINT: {
      uint32_t depth_unorm24 = color->f[0] * ((1u << 24) - 1);
      uint8_t stencil = color->ui[1];
      color->ui[0] = depth_unorm24 & 0xff;
      color->ui[1] = (depth_unorm24 >> 8) & 0xff;
      color->ui[2] = (depth_unorm24 >> 16) & 0xff;
      color->ui[3] = stencil;
      break;
   }
   default:
      break;
   }

   switch (fd6_ifmt(fd6_color_format(pfmt, TILE6_LINEAR))) {
   case R2D_UNORM8:
   case R2D_UNORM8_SRGB:
      /* The r2d ifmt is badly named, it also covers the signed case: */
      if (util_format_is_snorm(pfmt)) {
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW0(float_to_sbyte(color->f[0])));
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW1(float_to_sbyte(color->f[1])));
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW2(float_to_sbyte(color->f[2])));
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW3(float_to_sbyte(color->f[3])));
      } else {
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW0(float_to_ubyte(color->f[0])));
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW1(float_to_ubyte(color->f[1])));
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW2(float_to_ubyte(color->f[2])));
         ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW3(float_to_ubyte(color->f[3])));
      }
      break;
   case R2D_FLOAT16:
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW0(_mesa_float_to_half(color->f[0])));
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW1(_mesa_float_to_half(color->f[1])));
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW2(_mesa_float_to_half(color->f[2])));
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW3(_mesa_float_to_half(color->f[3])));
      break;
   case R2D_FLOAT32:
   case R2D_INT32:
   case R2D_INT16:
   case R2D_INT8:
   default:
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW0(color->ui[0]));
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW1(color->ui[1]));
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW2(color->ui[2]));
      ncrb.add(A6XX_RB_A2D_CLEAR_COLOR_DW3(color->ui[3]));
      break;
   }
}

template <chip CHIP>
static void
clear_lrz_setup(fd_cs &cs, struct fd_resource *zsbuf, struct fd_bo *lrz, double depth)
{
   fd_ncrb<CHIP> ncrb(cs, 15);

   ncrb.add(GRAS_A2D_DEST_TL(CHIP, .x = 0, .y = 0));
   ncrb.add(GRAS_A2D_DEST_BR(CHIP,
      .x = zsbuf->lrz_layout.lrz_pitch - 1,
      .y = zsbuf->lrz_layout.lrz_height - 1,
   ));

   union pipe_color_union clear_color = { .f = {depth} };

   emit_clear_color<CHIP>(ncrb, PIPE_FORMAT_Z16_UNORM, &clear_color);
   emit_blit_setup<CHIP>(ncrb, PIPE_FORMAT_Z16_UNORM, false, &clear_color, 0, ROTATE_0);

   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_INFO(
      .color_format = FMT6_16_UNORM,
      .tile_mode = TILE6_LINEAR,
      .color_swap = WZYX,
   ));
   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_BASE(.bo = lrz));
   ncrb.add(A6XX_RB_A2D_DEST_BUFFER_PITCH(zsbuf->lrz_layout.lrz_pitch * 2));
}

template <chip CHIP>
void
fd6_clear_lrz(fd_cs &cs, struct fd_resource *zsbuf,
              struct fd_bo *lrz, double depth)
{
   if (DEBUG_BLIT) {
      fprintf(stderr, "lrz clear:\ndst resource: ");
      util_dump_resource(stderr, &zsbuf->b.b);
      fprintf(stderr, "\n");
   }

   clear_lrz_setup<CHIP>(cs, zsbuf, lrz, depth);

   /*
    * Blit command:
    */

   fd_pkt7(cs, CP_BLIT, 1)
      .add(CP_BLIT_0(.op = BLIT_OP_SCALE));
}
FD_GENX(fd6_clear_lrz);

/**
 * Handle conversion of clear color
 */
static union pipe_color_union
convert_color(enum pipe_format format, union pipe_color_union *pcolor)
{
   const struct util_format_description *desc = util_format_description(format);
   union pipe_color_union color = *pcolor;

   for (unsigned i = 0; i < 4; i++) {
      unsigned channel = desc->swizzle[i];

      if (channel >= 4) /* PIPE_SWIZZLE_0/_1 */
         continue;

      if (desc->channel[channel].normalized)
         continue;

      switch (desc->channel[channel].type) {
      case UTIL_FORMAT_TYPE_SIGNED:
         color.i[i] = MAX2(color.i[i], -(1<<(desc->channel[channel].size - 1)));
         color.i[i] = MIN2(color.i[i], (1 << (desc->channel[channel].size - 1)) - 1);
         break;
      case UTIL_FORMAT_TYPE_UNSIGNED:
         color.ui[i] = MIN2(color.ui[i], BITFIELD_MASK(desc->channel[channel].size));
         break;
      }
   }

   /* For solid-fill blits, the hw isn't going to convert from
    * linear to srgb for us:
    */
   if (util_format_is_srgb(format)) {
      for (int i = 0; i < 3; i++)
         color.f[i] = util_format_linear_to_srgb_float(color.f[i]);
   }

   if (util_format_is_snorm(format)) {
      for (int i = 0; i < 3; i++)
         color.f[i] = CLAMP(color.f[i], -1.0f, 1.0f);
   }

   return color;
}

template <chip CHIP>
static void
fd6_clear_buffer(struct pipe_context *pctx,
                 struct pipe_resource *prsc,
                 unsigned offset, unsigned size,
                 const void *clear_value, int clear_value_size)
{
   enum pipe_format dst_fmt;
   union pipe_color_union color;

   switch (clear_value_size) {
   case 16:
      dst_fmt = PIPE_FORMAT_R32G32B32A32_UINT;
      memcpy(&color.ui, clear_value, 16);
      break;
   case 8:
      dst_fmt = PIPE_FORMAT_R32G32_UINT;
      memcpy(&color.ui, clear_value, 8);
      memset(&color.ui[2], 0, 8);
      break;
   case 4:
      dst_fmt = PIPE_FORMAT_R32_UINT;
      memcpy(&color.ui, clear_value, 4);
      memset(&color.ui[1], 0, 12);
      break;
   case 2:
      dst_fmt = PIPE_FORMAT_R16_UINT;
      color.ui[0] = *(unsigned short *)clear_value;
      memset(&color.ui[1], 0, 12);
      break;
   case 1:
      dst_fmt = PIPE_FORMAT_R8_UINT;
      color.ui[0] = *(unsigned char *)clear_value;
      memset(&color.ui[1], 0, 12);
      break;
   default:
      dst_fmt = PIPE_FORMAT_NONE;
      break;
   }

   /* unsupported clear_value_size and when alignment doesn't match, fallback */
   if ((dst_fmt == PIPE_FORMAT_NONE) ||
       (offset % clear_value_size) ||
       (size % clear_value_size)) {
      u_default_clear_buffer(pctx, prsc, offset, size, clear_value, clear_value_size);
      return;
   }

   if (DEBUG_BLIT) {
      fprintf(stderr, "buffer clear:\ndst resource: ");
      util_dump_resource(stderr, prsc);
      fprintf(stderr, "\n");
   }

   struct fd_context *ctx = fd_context(pctx);
   struct fd_resource *rsc = fd_resource(prsc);
   struct fd_batch *batch = fd_bc_alloc_batch(ctx, true);
   fd_cs cs(batch->draw);

   fd_screen_lock(ctx->screen);
   fd_batch_resource_write(batch, rsc);
   fd_screen_unlock(ctx->screen);

   assert(!batch->flushed);

   /* Marking the batch as needing flush must come after the batch
    * dependency tracking (resource_read()/resource_write()), as that
    * can trigger a flush
    */
   fd_batch_needs_flush(batch);

   fd_batch_update_queries(batch);

   emit_setup<CHIP>(batch->ctx, cs);

   with_ncrb (cs, 9) {
      emit_clear_color(ncrb, dst_fmt, &color);
      emit_blit_setup<CHIP>(ncrb, dst_fmt, false, &color, 0, ROTATE_0);
   }

   /*
    * Buffers can have dimensions bigger than max width (0x4000), so
    * remap into multiple 1d blits to fit within max dimension
    *
    * Additionally, the low 6 bits of DST addresses need to be zero (ie.
    * address aligned to 64 (0x40)) so we need to shift dst x1/x2 to make
    * up the difference, on top of already splitting up the blit so width
    * isn't > 16k.
    */

    /* # of pixels, ie blocks of clear_value_size: */
   unsigned blocks = size / clear_value_size;

   enum a6xx_format fmt = fd6_color_format(dst_fmt, TILE6_LINEAR);

   while (blocks) {
      uint32_t dst_x = (offset & 0x3f) / clear_value_size;
      uint32_t doff  = offset & ~0x3f;
      uint32_t width = MIN2(blocks, 0x4000 - dst_x);

      with_ncrb (cs, 6) {
         emit_blit_buffer_dst(ncrb, rsc, doff, 0, fmt);

         ncrb.add(GRAS_A2D_DEST_TL(CHIP, .x = dst_x));
         ncrb.add(GRAS_A2D_DEST_BR(CHIP, .x = dst_x + width - 1));
      }

      emit_blit_fini<CHIP>(ctx, cs);

      offset += width * clear_value_size;
      blocks -= width;
   }

   fd6_emit_flushes<CHIP>(batch->ctx, cs,
                          FD6_FLUSH_CCU_COLOR |
                          FD6_FLUSH_CCU_DEPTH |
                          FD6_FLUSH_CACHE |
                          FD6_WAIT_FOR_IDLE);

   fd_batch_flush(batch);
   fd_batch_reference(&batch, NULL);

   /* Acc query state will have been dirtied by our fd_batch_update_queries, so
    * the ctx->batch may need to turn its queries back on.
    */
   fd_context_dirty(ctx, FD_DIRTY_QUERY);
}

template <chip CHIP>
static void
clear_surface_setup(fd_cs &cs, struct pipe_surface *psurf,
                    const struct pipe_box *box2d, union pipe_color_union *color,
                    uint32_t unknown_8c01)
{
   uint32_t nr_samples = fd_resource_nr_samples(psurf->texture);
   fd_ncrb<CHIP> ncrb(cs, 11);

   ncrb.add(GRAS_A2D_DEST_TL(CHIP,
      .x = box2d->x * nr_samples,
      .y = box2d->y,
   ));
   ncrb.add(GRAS_A2D_DEST_BR(CHIP,
      .x = (box2d->x + box2d->width) * nr_samples - 1,
      .y = box2d->y + box2d->height - 1,
   ));

   union pipe_color_union clear_color = convert_color(psurf->format, color);

   emit_clear_color(ncrb, psurf->format, &clear_color);
   emit_blit_setup<CHIP>(ncrb, psurf->format, false, &clear_color, unknown_8c01, ROTATE_0);
}

template <chip CHIP>
void
fd6_clear_surface(struct fd_context *ctx, fd_cs &cs,
                  struct pipe_surface *psurf, const struct pipe_box *box2d,
                  union pipe_color_union *color, uint32_t unknown_8c01)
{
   if (DEBUG_BLIT) {
      fprintf(stderr, "surface clear:\ndst resource: ");
      util_dump_resource(stderr, psurf->texture);
      fprintf(stderr, "\n");
   }

   clear_surface_setup<CHIP>(cs, psurf, box2d, color, unknown_8c01);

   for (unsigned i = psurf->first_layer; i <= psurf->last_layer; i++) {
      with_ncrb (cs, 10)
         emit_blit_dst(ncrb, psurf->texture, psurf->format, psurf->level, i);

      emit_blit_fini<CHIP>(ctx, cs);
   }
}
FD_GENX(fd6_clear_surface);

template <chip CHIP>
static void
fd6_clear_texture(struct pipe_context *pctx, struct pipe_resource *prsc,
                  unsigned level, const struct pipe_box *box, const void *data)
   assert_dt
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_resource *rsc = fd_resource(prsc);

   if (DEBUG_BLIT) {
      fprintf(stderr, "surface texture:\ndst resource: ");
      util_dump_resource(stderr, prsc);
      fprintf(stderr, "\n");
   }

   if (!can_do_clear(prsc, level, box)) {
      u_default_clear_texture(pctx, prsc, level, box, data);
      return;
   }

   union pipe_color_union color;

   if (util_format_is_depth_or_stencil(prsc->format)) {
      const struct util_format_description *desc =
             util_format_description(prsc->format);
      float depth = 0.0f;
      uint8_t stencil = 0;

      if (util_format_has_depth(desc))
         util_format_unpack_z_float(prsc->format, &depth, data, 1);

      if (util_format_has_stencil(desc))
         util_format_unpack_s_8uint(prsc->format, &stencil, data, 1);

      if (rsc->stencil)
         fd6_clear_texture<CHIP>(pctx, &rsc->stencil->b.b, level, box, &stencil);

      color.f[0] = depth;
      color.ui[1] = stencil;
   } else {
      util_format_unpack_rgba(prsc->format, color.ui, data, 1);
   }

   struct fd_batch *batch = fd_bc_alloc_batch(ctx, true);

   fd_screen_lock(ctx->screen);
   fd_batch_resource_write(batch, rsc);
   fd_screen_unlock(ctx->screen);

   assert(!batch->flushed);

   /* Marking the batch as needing flush must come after the batch
    * dependency tracking (resource_read()/resource_write()), as that
    * can trigger a flush
    */
   fd_batch_needs_flush(batch);

   fd_batch_update_queries(batch);

   fd_cs cs(batch->draw);

   emit_setup<CHIP>(batch->ctx, cs);

   struct pipe_surface surf = {
         .format = prsc->format,
         .first_layer = box->z,
         .last_layer = box->depth + box->z - 1,
         .level = level,
         .texture = prsc,
   };

   fd6_clear_surface<CHIP>(ctx, cs, &surf, box, &color, 0);

   fd6_emit_flushes<CHIP>(batch->ctx, cs,
                          FD6_FLUSH_CCU_COLOR |
                          FD6_FLUSH_CCU_DEPTH |
                          FD6_FLUSH_CACHE |
                          FD6_WAIT_FOR_IDLE);

   fd_batch_flush(batch);
   fd_batch_reference(&batch, NULL);

   /* Acc query state will have been dirtied by our fd_batch_update_queries, so
    * the ctx->batch may need to turn its queries back on.
    */
   fd_context_dirty(ctx, FD_DIRTY_QUERY);
}

template <chip CHIP>
static void
resolve_tile_setup(struct fd_batch *batch, fd_cs &cs, uint32_t base,
                   struct pipe_surface *psurf, uint32_t unknown_8c01)
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   uint64_t gmem_base = batch->ctx->screen->gmem_base + base;
   uint32_t gmem_pitch = gmem->bin_w * batch->framebuffer.samples *
                         util_format_get_blocksize(psurf->format);
   unsigned width = pipe_surface_width(psurf);
   unsigned height = pipe_surface_height(psurf);
   fd_ncrb<CHIP> ncrb(cs, 26);

   ncrb.add(GRAS_A2D_DEST_TL(CHIP, .x = 0, .y = 0));
   ncrb.add(GRAS_A2D_DEST_BR(CHIP, .x = width - 1, .y = height - 1));

   ncrb.add(GRAS_A2D_SRC_XMIN(CHIP, 0));
   ncrb.add(GRAS_A2D_SRC_XMAX(CHIP, width - 1));
   ncrb.add(GRAS_A2D_SRC_YMIN(CHIP, 0));
   ncrb.add(GRAS_A2D_SRC_YMAX(CHIP, height - 1));

   /* Enable scissor bit, which will take into account the window scissor
    * which is set per-tile
    */
   emit_blit_setup<CHIP>(ncrb, psurf->format, true, NULL, unknown_8c01, ROTATE_0);

   /* We shouldn't be using GMEM in the layered rendering case: */
   assert(psurf->first_layer == psurf->last_layer);

   emit_blit_dst(ncrb, psurf->texture, psurf->format, psurf->level,
                 psurf->first_layer);

   enum a6xx_format sfmt = fd6_color_format(psurf->format, TILE6_LINEAR);
   enum a3xx_msaa_samples samples = fd_msaa_samples(batch->framebuffer.samples);

   ncrb.add(TPL1_A2D_SRC_TEXTURE_INFO(CHIP,
      .color_format = sfmt,
      .tile_mode = TILE6_2,
      .color_swap = WZYX,
      .srgb = util_format_is_srgb(psurf->format),
      .samples = samples,
      .samples_average = samples > MSAA_ONE,
      .unk20 = true,
      .unk22 = true,
   ));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_SIZE(CHIP,
      .width = width,
      .height = height,
   ));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_BASE(CHIP, .qword = gmem_base));
   ncrb.add(TPL1_A2D_SRC_TEXTURE_PITCH(CHIP, .pitch = gmem_pitch));
}

template <chip CHIP>
void
fd6_resolve_tile(struct fd_batch *batch, fd_cs &cs, uint32_t base,
                 struct pipe_surface *psurf, uint32_t unknown_8c01)
{
   resolve_tile_setup<CHIP>(batch, cs, base, psurf, unknown_8c01);

   /* sync GMEM writes with CACHE. */
   fd6_cache_inv<CHIP>(batch->ctx, cs);

   /* Wait for CACHE_INVALIDATE to land */
   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   fd_pkt7(cs, CP_BLIT, 1)
      .add(CP_BLIT_0(.op = BLIT_OP_SCALE));

   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   /* CP_BLIT writes to the CCU, unlike CP_EVENT_WRITE::BLIT which writes to
    * sysmem, and we generally assume that GMEM renderpasses leave their
    * results in sysmem, so we need to flush manually here.
    */
   fd6_emit_flushes<CHIP>(batch->ctx, cs, FD6_FLUSH_CCU_COLOR | FD6_WAIT_FOR_IDLE);
}
FD_GENX(fd6_resolve_tile);

template <chip CHIP>
static bool
handle_rgba_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
   assert_dt
{
   struct fd_batch *batch;

   assert(!(info->mask & PIPE_MASK_ZS));

   if (!can_do_blit(info))
      return false;

   struct fd_resource *src = fd_resource(info->src.resource);
   struct fd_resource *dst = fd_resource(info->dst.resource);

   fd6_validate_format(ctx, src, info->src.format);
   fd6_validate_format(ctx, dst, info->dst.format);

   batch = fd_bc_alloc_batch(ctx, true);

   fd_screen_lock(ctx->screen);

   fd_batch_resource_read(batch, src);
   fd_batch_resource_write(batch, dst);

   fd_screen_unlock(ctx->screen);

   assert(!batch->flushed);

   /* Marking the batch as needing flush must come after the batch
    * dependency tracking (resource_read()/resource_write()), as that
    * can trigger a flush
    */
   fd_batch_needs_flush(batch);

   fd_batch_update_queries(batch);

   fd_cs cs(batch->draw);

   emit_setup<CHIP>(batch->ctx, cs);

   DBG_BLIT(info, batch);

   trace_start_blit(&batch->trace, cs, info->src.resource->target,
                    info->dst.resource->target);

   if ((info->src.resource->target == PIPE_BUFFER) &&
       (info->dst.resource->target == PIPE_BUFFER)) {
      assert(src->layout.tile_mode == TILE6_LINEAR);
      assert(dst->layout.tile_mode == TILE6_LINEAR);
      emit_blit_buffer<CHIP>(ctx, cs, info);
   } else {
      /* I don't *think* we need to handle blits between buffer <-> !buffer */
      assert(info->src.resource->target != PIPE_BUFFER);
      assert(info->dst.resource->target != PIPE_BUFFER);
      emit_blit_texture<CHIP>(ctx, cs, info);
   }

   trace_end_blit(&batch->trace, cs);

   fd6_emit_flushes<CHIP>(batch->ctx, cs,
                          FD6_FLUSH_CCU_COLOR |
                          FD6_FLUSH_CCU_DEPTH |
                          FD6_FLUSH_CACHE |
                          FD6_WAIT_FOR_IDLE);

   fd_batch_flush(batch);
   fd_batch_reference(&batch, NULL);

   /* Acc query state will have been dirtied by our fd_batch_update_queries, so
    * the ctx->batch may need to turn its queries back on.
    */
   fd_context_dirty(ctx, FD_DIRTY_QUERY);

   return true;
}

/**
 * Re-written z/s blits can still fail for various reasons (for example MSAA).
 * But we want to do the fallback blit with the re-written pipe_blit_info,
 * in particular as u_blitter cannot blit stencil.  So handle the fallback
 * ourself and never "fail".
 */
template <chip CHIP>
static bool
do_rewritten_blit(struct fd_context *ctx, const struct pipe_blit_info *info)
   assert_dt
{
   bool success = handle_rgba_blit<CHIP>(ctx, info);
   if (!success) {
      success = fd_blitter_blit(ctx, info);
   }
   assert(success); /* fallback should never fail! */
   return success;
}

/**
 * Handle depth/stencil blits either via u_blitter and/or re-writing the
 * blit into an equivilant format that we can handle
 */
template <chip CHIP>
static bool
handle_zs_blit(struct fd_context *ctx,
               const struct pipe_blit_info *info) assert_dt
{
   struct pipe_blit_info blit = *info;

   if (DEBUG_BLIT) {
      fprintf(stderr, "---- handle_zs_blit: ");
      dump_blit_info(info);
   }

   fail_if(info->src.format != info->dst.format);

   struct fd_resource *src = fd_resource(info->src.resource);
   struct fd_resource *dst = fd_resource(info->dst.resource);

   switch (info->dst.format) {
   case PIPE_FORMAT_S8_UINT:
      assert(info->mask == PIPE_MASK_S);
      blit.mask = PIPE_MASK_R;
      blit.src.format = PIPE_FORMAT_R8_UINT;
      blit.dst.format = PIPE_FORMAT_R8_UINT;
      blit.sample0_only = true;
      return do_rewritten_blit<CHIP>(ctx, &blit);

   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      if (info->mask & PIPE_MASK_Z) {
         blit.mask = PIPE_MASK_R;
         blit.src.format = PIPE_FORMAT_R32_FLOAT;
         blit.dst.format = PIPE_FORMAT_R32_FLOAT;
         blit.sample0_only = true;
         do_rewritten_blit<CHIP>(ctx, &blit);
      }

      if (info->mask & PIPE_MASK_S) {
         blit.mask = PIPE_MASK_R;
         blit.src.format = PIPE_FORMAT_R8_UINT;
         blit.dst.format = PIPE_FORMAT_R8_UINT;
         blit.src.resource = &src->stencil->b.b;
         blit.dst.resource = &dst->stencil->b.b;
         blit.sample0_only = true;
         do_rewritten_blit<CHIP>(ctx, &blit);
      }

      return true;

   case PIPE_FORMAT_Z16_UNORM:
      blit.mask = PIPE_MASK_R;
      blit.src.format = PIPE_FORMAT_R16_UNORM;
      blit.dst.format = PIPE_FORMAT_R16_UNORM;
      blit.sample0_only = true;
      return do_rewritten_blit<CHIP>(ctx, &blit);

   case PIPE_FORMAT_Z32_UNORM:
   case PIPE_FORMAT_Z32_FLOAT:
      assert(info->mask == PIPE_MASK_Z);
      blit.mask = PIPE_MASK_R;
      blit.src.format = PIPE_FORMAT_R32_UINT;
      blit.dst.format = PIPE_FORMAT_R32_UINT;
      blit.sample0_only = true;
      return do_rewritten_blit<CHIP>(ctx, &blit);

   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      blit.mask = 0;
      if (info->mask & PIPE_MASK_Z)
         blit.mask |= PIPE_MASK_R | PIPE_MASK_G | PIPE_MASK_B;
      if (info->mask & PIPE_MASK_S)
         blit.mask |= PIPE_MASK_A;
      blit.src.format = PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
      blit.dst.format = PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
      /* non-UBWC Z24_UNORM_S8_UINT_AS_R8G8B8A8 is broken on a630, fall back to
       * 8888_unorm.
       */
      if (!ctx->screen->info->props.has_z24uint_s8uint) {
         if (!src->layout.ubwc && !dst->layout.ubwc) {
            blit.src.format = PIPE_FORMAT_RGBA8888_UINT;
            blit.dst.format = PIPE_FORMAT_RGBA8888_UINT;
         } else {
            if (!src->layout.ubwc)
               blit.src.format = PIPE_FORMAT_RGBA8888_UNORM;
            if (!dst->layout.ubwc)
               blit.dst.format = PIPE_FORMAT_RGBA8888_UNORM;
         }
      }
      if (info->src.resource->nr_samples > 1 && blit.src.format != PIPE_FORMAT_RGBA8888_UINT)
         blit.sample0_only = true;
      return fd_blitter_blit(ctx, &blit);

   default:
      return false;
   }
}

template <chip CHIP>
static bool
handle_compressed_blit(struct fd_context *ctx,
                       const struct pipe_blit_info *info) assert_dt
{
   struct pipe_blit_info blit = *info;

   if (DEBUG_BLIT) {
      fprintf(stderr, "---- handle_compressed_blit: ");
      dump_blit_info(info);
   }

   if (info->src.format != info->dst.format)
      return fd_blitter_blit(ctx, info);

   if (util_format_get_blocksize(info->src.format) == 8) {
      blit.src.format = blit.dst.format = PIPE_FORMAT_R16G16B16A16_UINT;
   } else {
      assert(util_format_get_blocksize(info->src.format) == 16);
      blit.src.format = blit.dst.format = PIPE_FORMAT_R32G32B32A32_UINT;
   }

   int bw = util_format_get_blockwidth(info->src.format);
   int bh = util_format_get_blockheight(info->src.format);

   /* NOTE: x/y *must* be aligned to block boundary (ie. in
    * glCompressedTexSubImage2D()) but width/height may not
    * be:
    */

   assert((blit.src.box.x % bw) == 0);
   assert((blit.src.box.y % bh) == 0);

   blit.src.box.x /= bw;
   blit.src.box.y /= bh;
   blit.src.box.width = DIV_ROUND_UP(blit.src.box.width, bw);
   blit.src.box.height = DIV_ROUND_UP(blit.src.box.height, bh);

   assert((blit.dst.box.x % bw) == 0);
   assert((blit.dst.box.y % bh) == 0);

   blit.dst.box.x /= bw;
   blit.dst.box.y /= bh;
   blit.dst.box.width = DIV_ROUND_UP(blit.dst.box.width, bw);
   blit.dst.box.height = DIV_ROUND_UP(blit.dst.box.height, bh);

   return do_rewritten_blit<CHIP>(ctx, &blit);
}

/**
 * For SNORM formats, copy them as the equivalent UNORM format.  If we treat
 * them as snorm then the 0x80 (-1.0 snorm8) value will get clamped to 0x81
 * (also -1.0), when we're supposed to be memcpying the bits. See
 * https://gitlab.khronos.org/Tracker/vk-gl-cts/-/issues/2917 for discussion.
 */
template <chip CHIP>
static bool
handle_snorm_copy_blit(struct fd_context *ctx,
                       const struct pipe_blit_info *info)
   assert_dt
{
   /* If we're interpolating the pixels, we can't just treat the values as unorm. */
   fail_if(info->filter == PIPE_TEX_FILTER_LINEAR);

   struct pipe_blit_info blit = *info;

   blit.src.format = blit.dst.format = util_format_snorm_to_unorm(info->src.format);

   return handle_rgba_blit<CHIP>(ctx, &blit);
}

template <chip CHIP>
static bool
fd6_blit(struct fd_context *ctx, const struct pipe_blit_info *info) assert_dt
{
   if (info->mask & PIPE_MASK_ZS)
      return handle_zs_blit<CHIP>(ctx, info);

   if (util_format_is_compressed(info->src.format) ||
       util_format_is_compressed(info->dst.format))
      return handle_compressed_blit<CHIP>(ctx, info);

   if ((info->src.format == info->dst.format) &&
       util_format_is_snorm(info->src.format))
      return handle_snorm_copy_blit<CHIP>(ctx, info);

   return handle_rgba_blit<CHIP>(ctx, info);
}

template <chip CHIP>
void
fd6_blitter_init(struct pipe_context *pctx)
   disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->clear_ubwc = fd6_clear_ubwc<CHIP>;
   ctx->validate_format = fd6_validate_format;

   if (FD_DBG(NOBLIT))
      return;

   pctx->clear_buffer = fd6_clear_buffer<CHIP>;
   pctx->clear_texture = fd6_clear_texture<CHIP>;
   ctx->blit = fd6_blit<CHIP>;
}
FD_GENX(fd6_blitter_init);

unsigned
fd6_tile_mode_for_format(enum pipe_format pfmt)
{
   if (!util_is_power_of_two_nonzero(util_format_get_blocksize(pfmt)))
      return TILE6_LINEAR;

   /* basically just has to be a format we can blit, so uploads/downloads
    * via linear staging buffer works:
    */
   if (ok_format(pfmt))
      return TILE6_3;

   return TILE6_LINEAR;
}

unsigned
fd6_tile_mode(const struct pipe_resource *tmpl)
{
   return fd6_tile_mode_for_format(tmpl->format);
}
