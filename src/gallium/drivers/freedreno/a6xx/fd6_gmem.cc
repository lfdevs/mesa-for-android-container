/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include <stdio.h>

#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "freedreno_draw.h"
#include "freedreno_resource.h"
#include "freedreno_state.h"
#include "freedreno_tracepoints.h"

#include "fd6_barrier.h"
#include "fd6_blitter.h"
#include "fd6_context.h"
#include "fd6_draw.h"
#include "fd6_emit.h"
#include "fd6_gmem.h"
#include "fd6_pack.h"
#include "fd6_program.h"
#include "fd6_resource.h"
#include "fd6_zsa.h"

template <chip CHIP>
static void
emit_mrt(fd_crb &crb, struct pipe_framebuffer_state *pfb,
         const struct fd_gmem_stateobj *gmem)
{
   unsigned srgb_cntl = 0;
   unsigned i;

   /* Note, GLES 3.2 says "If the fragment’s layer number is negative, or
    * greater than or equal to the minimum number of layers of any attachment,
    * the effects of the fragment on the framebuffer contents are undefined."
    */
   unsigned max_layer_index = 0;
   enum a6xx_format mrt0_format = FMT6_NONE;

   for (i = 0; i < pfb->nr_cbufs; i++) {
      enum a3xx_color_swap swap = WZYX;
      bool sint = false, uint = false;
      struct fd_resource *rsc = NULL;
      ASSERTED struct fdl_slice *slice = NULL;
      uint32_t stride = 0;
      uint32_t array_stride = 0;
      uint32_t offset;

      if (!pfb->cbufs[i].texture)
         continue;

      struct pipe_surface *psurf = &pfb->cbufs[i];
      enum pipe_format pformat = psurf->format;
      rsc = fd_resource(psurf->texture);

      uint32_t base = gmem ? gmem->cbuf_base[i] : 0;
      slice = fd_resource_slice(rsc, psurf->level);
      enum a6xx_tile_mode tile_mode = (enum a6xx_tile_mode)
            fd_resource_tile_mode(psurf->texture, psurf->level);
      enum a6xx_format format = fd6_color_format(pformat, tile_mode);
      sint = util_format_is_pure_sint(pformat);
      uint = util_format_is_pure_uint(pformat);

      if (util_format_is_srgb(pformat))
         srgb_cntl |= (1 << i);

      offset =
         fd_resource_offset(rsc, psurf->level, psurf->first_layer);

      stride = fd_resource_pitch(rsc, psurf->level);
      array_stride = fd_resource_layer_stride(rsc, psurf->level);
      swap = fd6_color_swap(pformat, (enum a6xx_tile_mode)rsc->layout.tile_mode, false);

      max_layer_index = psurf->last_layer - psurf->first_layer;

      assert((offset + slice->size0) <= fd_bo_size(rsc->bo));

      /* Batch with no draws? */
      crb.attach_bo(rsc->bo);

      crb.add(RB_MRT_BUF_INFO(CHIP, i,
         .color_format = format,
         .color_tile_mode = tile_mode,
         .color_swap = swap,
         .losslesscompen = fd_resource_ubwc_enabled(rsc, psurf->level),
      ));

      crb.add(A6XX_RB_MRT_PITCH(i, stride));
      crb.add(A6XX_RB_MRT_ARRAY_PITCH(i, array_stride));
      crb.add(A6XX_RB_MRT_BASE(i, .bo = rsc->bo, .bo_offset = offset));
      crb.add(A6XX_RB_MRT_BASE_GMEM(i, base));

      crb.add(A6XX_SP_PS_MRT_REG(i,
         .color_format = format,
         .color_sint = sint,
         .color_uint = uint
      ));

      crb.add(A6XX_RB_COLOR_FLAG_BUFFER_ADDR(i,
         .bo = rsc->bo,
         .bo_offset = fd_resource_ubwc_offset(rsc, psurf->level, psurf->first_layer),
      ));
      crb.add(A6XX_RB_COLOR_FLAG_BUFFER_PITCH(i,
         .pitch = fdl_ubwc_pitch(&rsc->layout, psurf->level),
         .array_pitch = rsc->layout.ubwc_layer_size >> 2,
      ));

      if (i == 0)
         mrt0_format = format;
   }
   if (pfb->zsbuf.texture)
      max_layer_index = pfb->zsbuf.last_layer - pfb->zsbuf.first_layer;

   crb.add(GRAS_LRZ_MRT_BUFFER_INFO_0(CHIP, .color_format = mrt0_format));
   crb.add(A6XX_RB_SRGB_CNTL(.dword = srgb_cntl));
   crb.add(A6XX_SP_SRGB_CNTL(.dword = srgb_cntl));

   crb.add(GRAS_CL_ARRAY_SIZE(CHIP, max_layer_index));
}

template <chip CHIP>
static void
emit_zs(fd_crb &crb, struct pipe_surface *zsbuf, const struct fd_gmem_stateobj *gmem)
{
   if (zsbuf->texture) {
      struct fd_resource *rsc = fd_resource(zsbuf->texture);
      struct fd_resource *stencil = rsc->stencil;
      uint32_t stride = fd_resource_pitch(rsc, zsbuf->level);
      uint32_t array_stride = fd_resource_layer_stride(rsc, zsbuf->level);
      uint32_t base = gmem ? gmem->zsbuf_base[0] : 0;
      uint32_t offset =
         fd_resource_offset(rsc, zsbuf->level, zsbuf->first_layer);

      /* We could have a depth buffer, but no draws with depth write/test
       * enabled, in which case it wouldn't have been part of the batch
       * resource tracking
       */
      crb.attach_bo(rsc->bo);

      if (zsbuf->format == PIPE_FORMAT_S8_UINT) {
         /* S8 is implemented as Z32_S8 minus the Z32 plane: */
         enum a6xx_depth_format fmt = DEPTH6_32;

         crb.add(RB_DEPTH_BUFFER_INFO(CHIP,
            .depth_format = fmt,
            .tilemode = TILE6_3,
            .losslesscompen = fd_resource_ubwc_enabled(rsc, zsbuf->level),
         ));
         crb.add(A6XX_RB_DEPTH_BUFFER_PITCH());
         crb.add(A6XX_RB_DEPTH_BUFFER_ARRAY_PITCH());
         crb.add(A6XX_RB_DEPTH_BUFFER_BASE());
         crb.add(A6XX_RB_DEPTH_GMEM_BASE(base));
         crb.add(GRAS_SU_DEPTH_BUFFER_INFO(CHIP, .depth_format = fmt));

         stencil = rsc;
      } else {
         enum a6xx_depth_format fmt = fd6_pipe2depth(zsbuf->format);

         crb.add(RB_DEPTH_BUFFER_INFO(CHIP,
            .depth_format = fmt,
            .tilemode = TILE6_3,
            .losslesscompen = fd_resource_ubwc_enabled(rsc, zsbuf->level),
         ));
         crb.add(A6XX_RB_DEPTH_BUFFER_PITCH(stride));
         crb.add(A6XX_RB_DEPTH_BUFFER_ARRAY_PITCH(array_stride));
         crb.add(A6XX_RB_DEPTH_BUFFER_BASE(.bo = rsc->bo, .bo_offset = offset));
         crb.add(A6XX_RB_DEPTH_GMEM_BASE(base));
         crb.add(GRAS_SU_DEPTH_BUFFER_INFO(CHIP, .depth_format = fmt));

         crb.add(A6XX_RB_DEPTH_FLAG_BUFFER_BASE(
            .bo = rsc->bo,
            .bo_offset = fd_resource_ubwc_offset(rsc, zsbuf->level, zsbuf->first_layer),
         ));
         crb.add(A6XX_RB_DEPTH_FLAG_BUFFER_PITCH(
            .pitch = fdl_ubwc_pitch(&rsc->layout, zsbuf->level),
            .array_pitch = rsc->layout.ubwc_layer_size >> 2,
         ));
      }

      if (stencil) {
         stride = fd_resource_pitch(stencil, zsbuf->level);
         array_stride = fd_resource_layer_stride(stencil, zsbuf->level);
         uint32_t base = gmem ? gmem->zsbuf_base[1] : 0;
         uint32_t offset =
            fd_resource_offset(stencil, zsbuf->level, zsbuf->first_layer);

         crb.attach_bo(stencil->bo);

         crb.add(RB_STENCIL_BUFFER_INFO(CHIP,
            .separate_stencil = true,
            .tilemode = TILE6_3,
         ));
         crb.add(A6XX_RB_STENCIL_BUFFER_PITCH(stride));
         crb.add(A6XX_RB_STENCIL_BUFFER_ARRAY_PITCH(array_stride));
         crb.add(A6XX_RB_STENCIL_BUFFER_BASE(.bo = stencil->bo, .bo_offset = offset));
         crb.add(A6XX_RB_STENCIL_GMEM_BASE(base));
      } else {
         crb.add(RB_STENCIL_BUFFER_INFO(CHIP, 0));
      }
   } else {
      crb.add(RB_DEPTH_BUFFER_INFO(CHIP,
         .depth_format = DEPTH6_NONE,
      ));
      crb.add(A6XX_RB_DEPTH_BUFFER_PITCH());
      crb.add(A6XX_RB_DEPTH_BUFFER_ARRAY_PITCH());
      crb.add(A6XX_RB_DEPTH_BUFFER_BASE());
      crb.add(A6XX_RB_DEPTH_GMEM_BASE());
      crb.add(GRAS_SU_DEPTH_BUFFER_INFO(CHIP, .depth_format = DEPTH6_NONE));
      crb.add(RB_STENCIL_BUFFER_INFO(CHIP, 0));
   }
}

template <chip CHIP>
static inline bool
lrzfc_enabled(struct fd_resource *zsbuf)
{
   if ((CHIP < A7XX) || FD_DBG(NOLRZFC) || !zsbuf)
      return false;
   return zsbuf->lrz_layout.lrz_fc_size > 0;
}

template <chip CHIP>
static void
emit_lrz(fd_cs &cs, struct fd_batch *batch, struct fd_batch_subpass *subpass)
{
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   if (!subpass->lrz) {
      fd_crb crb(cs, 7);

      crb.add(GRAS_LRZ_BUFFER_BASE(CHIP));
      crb.add(GRAS_LRZ_BUFFER_PITCH(CHIP));
      crb.add(A6XX_GRAS_LRZ_FAST_CLEAR_BUFFER_BASE());

      if (CHIP >= A7XX) {
         crb.add(GRAS_LRZ_DEPTH_BUFFER_INFO(CHIP));
         crb.add(GRAS_LRZ_CNTL2(CHIP));
      }

      return;
   }

   /* When swapping LRZ buffers we need to flush LRZ cache..
    * we possibly don't need this during the binning pass, it
    * appears that the corruption happens on the read-side, ie.
    * we change the LRZ buffer after a sub-pass, but get a
    * cache-hit on stale data from the previous LRZ buffer.
    */
   fd6_event_write<CHIP>(batch->ctx, cs, FD_LRZ_FLUSH);

   fd_crb crb(cs, 7);

   struct fd_resource *zsbuf = fd_resource(pfb->zsbuf.texture);

   crb.attach_bo(subpass->lrz);

   crb.add(GRAS_LRZ_BUFFER_BASE(CHIP, .bo = subpass->lrz));
   crb.add(GRAS_LRZ_BUFFER_PITCH(CHIP, .pitch = zsbuf->lrz_layout.lrz_pitch));
   crb.add(A6XX_GRAS_LRZ_FAST_CLEAR_BUFFER_BASE(
      .bo = zsbuf->lrz_layout.lrz_fc_size ? subpass->lrz : NULL,
      .bo_offset = zsbuf->lrz_layout.lrz_fc_offset
   ));

   if (CHIP >= A7XX) {
      crb.add(GRAS_LRZ_DEPTH_BUFFER_INFO(CHIP,
         .depth_format = fd6_pipe2depth(pfb->zsbuf.format),
      ));
      crb.add(GRAS_LRZ_CNTL2(CHIP,
         .disable_on_wrong_dir = false,
         .fc_enable = lrzfc_enabled<CHIP>(zsbuf),
      ));
   }
}

/* Emit any needed lrz clears to the prologue cmds
 */
template <chip CHIP>
static void
emit_lrz_clears(struct fd_batch *batch)
{
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   struct fd_context *ctx = batch->ctx;
   unsigned count = 0;

   if (!pfb->zsbuf.texture)
      return;

   struct fd_resource *zsbuf = fd_resource(pfb->zsbuf.texture);

   foreach_subpass (subpass, batch) {
      /* The lrz buffer isn't explicitly tracked by the batch resource
       * tracking (tracking the zsbuf is sufficient), but it still needs
       * to be attached to the ring
       */
      if (subpass->lrz)
         fd_ringbuffer_attach_bo(batch->gmem, subpass->lrz);

      if (!(subpass->fast_cleared & FD_BUFFER_LRZ))
         continue;

      subpass->fast_cleared &= ~FD_BUFFER_LRZ;

      fd_cs cs(fd_batch_get_prologue(batch));

      /* prep before first clear: */
      if (count == 0) {
         fd6_emit_gmem_cache_cntl<CHIP>(cs, ctx->screen, false);
         fd6_set_render_mode<CHIP>(cs, {RM6_BLIT2DSCALE});
         fd6_emit_flushes<CHIP>(ctx, cs, FD6_FLUSH_CACHE);
         fd6_set_rb_dbg_eco_mode<CHIP>(ctx, cs, true);
      }

      if (lrzfc_enabled<CHIP>(zsbuf)) {
         emit_lrz<CHIP>(cs, batch, subpass);
         fd_pkt4(cs, 1)
            .add(GRAS_LRZ_DEPTH_CLEAR(CHIP, subpass->clear_depth));
         fd6_event_write<CHIP>(ctx, cs, FD_LRZ_CLEAR);
      } else {
         fd6_clear_lrz<CHIP>(cs, zsbuf, subpass->lrz, subpass->clear_depth);
      }

      count++;
   }

   /* cleanup after last clear: */
   if (count > 0) {
      fd_cs cs(fd_batch_get_prologue(batch));

      fd6_set_rb_dbg_eco_mode<CHIP>(ctx, cs, false);

      /* Clearing writes via CCU color in the PS stage, and LRZ is read via
       * UCHE in the earlier GRAS stage.
       *
       * Note tu also asks for WFI but maybe that is only needed if
       * has_ccu_flush_bug (and it is added by fd6_emit_flushes() already
       * in that case)
       */
      fd6_emit_flushes<CHIP>(batch->ctx, cs,
                             FD6_FLUSH_CCU_COLOR |
                             FD6_INVALIDATE_CACHE);
   }
}

static bool
use_hw_binning(struct fd_batch *batch)
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;

   if ((gmem->maxpw * gmem->maxph) > 32)
      return false;

   return fd_binning_enabled && ((gmem->nbins_x * gmem->nbins_y) >= 2) &&
          (batch->num_draws > 0);
}

template <chip CHIP>
static void
patch_fb_read_gmem(struct fd_batch *batch)
{
   struct fd_screen *screen = batch->ctx->screen;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   unsigned num_patches = fd_patch_num_elements(&batch->fb_read_patches);
   if (!num_patches)
      return;

   for (unsigned i = 0; i < num_patches; i++) {
     struct fd_cs_patch *patch =
        fd_patch_element(&batch->fb_read_patches, i);
      int buf = patch->val;
      struct pipe_surface *psurf = &pfb->cbufs[buf];
      struct pipe_resource *prsc = psurf->texture;
      struct fd_resource *rsc = fd_resource(prsc);
      enum pipe_format format = psurf->format;

      uint8_t swiz[4];
      fdl6_format_swiz(psurf->format, false, swiz);

      /* always TILE6_2 mode in GMEM, which also means no swap: */
      if (CHIP <= A7XX) {
         uint64_t base = screen->gmem_base + gmem->cbuf_base[buf];
         uint32_t descriptor[FDL6_TEX_CONST_DWORDS] = {
               A6XX_TEX_CONST_0_FMT(fd6_texture_format(
                     format, (enum a6xx_tile_mode)rsc->layout.tile_mode, false)) |
               A6XX_TEX_CONST_0_SAMPLES(fd_msaa_samples(prsc->nr_samples)) |
               A6XX_TEX_CONST_0_SWAP(WZYX) |
               A6XX_TEX_CONST_0_TILE_MODE(TILE6_2) |
               COND(util_format_is_srgb(format), A6XX_TEX_CONST_0_SRGB) |
               A6XX_TEX_CONST_0_SWIZ_X(fdl6_swiz(swiz[0])) |
               A6XX_TEX_CONST_0_SWIZ_Y(fdl6_swiz(swiz[1])) |
               A6XX_TEX_CONST_0_SWIZ_Z(fdl6_swiz(swiz[2])) |
               A6XX_TEX_CONST_0_SWIZ_W(fdl6_swiz(swiz[3])),

            A6XX_TEX_CONST_1_WIDTH(pfb->width) |
               A6XX_TEX_CONST_1_HEIGHT(pfb->height),

            A6XX_TEX_CONST_2_PITCH(gmem->bin_w * gmem->cbuf_cpp[buf]) |
               A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D),

            A6XX_TEX_CONST_3_ARRAY_PITCH(rsc->layout.layer_size),
            A6XX_TEX_CONST_4_BASE_LO(base),

            A6XX_TEX_CONST_5_BASE_HI(base >> 32) |
               A6XX_TEX_CONST_5_DEPTH(prsc->array_size)
         };

         memcpy(patch->cs, descriptor, FDL6_TEX_CONST_DWORDS * 4);
      } else if (CHIP >= A8XX) {
         /* base address is simply GMEM location when using GMEM tiling: */
         uint64_t base = gmem->cbuf_base[buf];
         uint32_t descriptor[FDL6_TEX_CONST_DWORDS] = {
            A8XX_TEX_MEMOBJ_0_BASE_LO(base),

            A8XX_TEX_MEMOBJ_1_BASE_HI(base >> 32) |
            A8XX_TEX_MEMOBJ_1_TYPE(A6XX_TEX_2D) |
            A8XX_TEX_MEMOBJ_1_DEPTH(prsc->array_size),

            A8XX_TEX_MEMOBJ_2_WIDTH(pfb->width) |
            A8XX_TEX_MEMOBJ_2_HEIGHT(pfb->height) |
            A8XX_TEX_MEMOBJ_2_SAMPLES(fd_msaa_samples(prsc->nr_samples)),

            A8XX_TEX_MEMOBJ_3_FMT(fd6_texture_format(
                     format, (enum a6xx_tile_mode)rsc->layout.tile_mode, false)) |
            A8XX_TEX_MEMOBJ_3_SWAP(WZYX) |
            A8XX_TEX_MEMOBJ_3_SWIZ_X(fdl8_swiz(swiz[0])) |
            A8XX_TEX_MEMOBJ_3_SWIZ_Y(fdl8_swiz(swiz[1])) |
            A8XX_TEX_MEMOBJ_3_SWIZ_Z(fdl8_swiz(swiz[2])) |
            A8XX_TEX_MEMOBJ_3_SWIZ_W(fdl8_swiz(swiz[3])),

            A8XX_TEX_MEMOBJ_4_TILE_MODE(TILE6_2) |
            COND(util_format_is_srgb(format), A8XX_TEX_MEMOBJ_4_SRGB),

            0,

            A8XX_TEX_MEMOBJ_6_TEX_LINE_OFFSET(gmem->bin_w * gmem->cbuf_cpp[buf] * 8) |   /* in bits */
            A8XX_TEX_MEMOBJ_6_MIN_LINE_OFFSET(0),

            A8XX_TEX_MEMOBJ_7_ARRAY_SLICE_OFFSET(rsc->layout.layer_size),
         };

         memcpy(patch->cs, descriptor, FDL6_TEX_CONST_DWORDS * 4);
      }
   }

   util_dynarray_clear(&batch->fb_read_patches);
}

template <chip CHIP>
static void
patch_fb_read_sysmem(struct fd_batch *batch)
{
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   unsigned num_patches =
      fd_patch_num_elements(&batch->fb_read_patches);
   if (!num_patches)
      return;
   for (unsigned i = 0; i < num_patches; i++) {
     struct fd_cs_patch *patch =
        fd_patch_element(&batch->fb_read_patches, i);
      int buf = patch->val;

      struct pipe_surface *psurf = &pfb->cbufs[buf];
      if (!psurf->texture)
         return;

      struct pipe_resource *prsc = psurf->texture;
      struct fd_resource *rsc = fd_resource(prsc);

      struct fdl_view_args args = {
         .iova = fd_bo_get_iova(rsc->bo),

         .base_miplevel = psurf->level,
         .level_count = 1,

         .base_array_layer = psurf->first_layer,
         .layer_count = psurf->last_layer - psurf->first_layer + 1,

         .swiz = {PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z,
                  PIPE_SWIZZLE_W},
         .format = psurf->format,

         .type = FDL_VIEW_TYPE_2D,
         .chroma_offsets = {FDL_CHROMA_LOCATION_COSITED_EVEN,
                            FDL_CHROMA_LOCATION_COSITED_EVEN},
      };
      const struct fdl_layout *layouts[3] = {&rsc->layout, NULL, NULL};
      struct fdl6_view view;
      fdl6_view_init<CHIP>(&view, layouts, &args,
                           batch->ctx->screen->info->props.has_z24uint_s8uint);
      memcpy(patch->cs, view.descriptor, FDL6_TEX_CONST_DWORDS * 4);
   }

   util_dynarray_clear(&batch->fb_read_patches);
}

template <chip CHIP>
static void
update_render_cntl(fd_cs &cs, struct fd_screen *screen,
                   struct pipe_framebuffer_state *pfb,
                   bool binning)
{
   if (CHIP >= A7XX) {
      with_crb (cs, 2) {
         crb.add(RB_RENDER_CNTL(CHIP,
            .fs_disable = binning,
            .raster_mode = TYPE_TILED,
            .raster_direction = LR_TB
         ));
         crb.add(GRAS_SU_RENDER_CNTL(CHIP,
            .fs_disable = binning,
         ));
      }
      return;
   }

   bool depth_ubwc_enable = false;
   uint32_t mrts_ubwc_enable = 0;
   int i;

   if (pfb->zsbuf.texture) {
      struct fd_resource *rsc = fd_resource(pfb->zsbuf.texture);
      depth_ubwc_enable =
         fd_resource_ubwc_enabled(rsc, pfb->zsbuf.level);
   }

   for (i = 0; i < pfb->nr_cbufs; i++) {
      if (!pfb->cbufs[i].texture)
         continue;

      struct pipe_surface *psurf = &pfb->cbufs[i];
      struct fd_resource *rsc = fd_resource(psurf->texture);

      if (fd_resource_ubwc_enabled(rsc, psurf->level))
         mrts_ubwc_enable |= 1 << i;
   }

   struct fd_reg_pair rb_render_cntl = RB_RENDER_CNTL(
         CHIP,
         .ccusinglecachelinesize = 2,
         .fs_disable = binning,
         .flag_depth = depth_ubwc_enable,
         .flag_mrts = mrts_ubwc_enable,
   );

   if (screen->info->props.has_cp_reg_write) {
      fd_pkt7(cs, CP_REG_WRITE, 3)
         .add(CP_REG_WRITE_0(TRACK_RENDER_CNTL))
         .add(CP_REG_WRITE_1(rb_render_cntl.reg))
         .add(CP_REG_WRITE_2(rb_render_cntl.value));
   } else {
      fd_pkt4(cs, 1)
         .add(rb_render_cntl);
   }
}

template <chip CHIP>
static void
update_vsc_pipe(fd_cs &cs, struct fd_batch *batch)
{
   struct fd_context *ctx = batch->ctx;
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   unsigned max_vsc_pipes = batch->ctx->screen->info->num_vsc_pipes;
   int i;

   if (batch->draw_strm_bits / 8 > fd6_ctx->vsc_draw_strm_pitch) {
      if (fd6_ctx->vsc_draw_strm)
         fd_bo_del(fd6_ctx->vsc_draw_strm);
      fd6_ctx->vsc_draw_strm = NULL;
      /* Note: probably only need to align to 0x40, but aligning stronger
       * reduces the odds that we will have to realloc again on the next
       * frame:
       */
      fd6_ctx->vsc_draw_strm_pitch = align(batch->draw_strm_bits / 8, 0x4000);
      mesa_logd("pre-resize VSC_DRAW_STRM_PITCH to: 0x%x",
                fd6_ctx->vsc_draw_strm_pitch);
   }

   if (batch->prim_strm_bits / 8 > fd6_ctx->vsc_prim_strm_pitch) {
      if (fd6_ctx->vsc_prim_strm)
         fd_bo_del(fd6_ctx->vsc_prim_strm);
      fd6_ctx->vsc_prim_strm = NULL;
      fd6_ctx->vsc_prim_strm_pitch = align(batch->prim_strm_bits / 8, 0x4000);
      mesa_logd("pre-resize VSC_PRIM_STRM_PITCH to: 0x%x",
                fd6_ctx->vsc_prim_strm_pitch);
   }

   if (!fd6_ctx->vsc_draw_strm) {
      /* We also use four bytes per vsc pipe at the end of the draw
       * stream buffer for VSC_PIPE_DATA_DRAW_SIZE written back by hw
       * (see VSC_SIZE_BASE)
       */
      unsigned sz = (max_vsc_pipes * fd6_ctx->vsc_draw_strm_pitch) +
                    (max_vsc_pipes * 4);
      fd6_ctx->vsc_draw_strm =
         fd_bo_new(ctx->screen->dev, sz, FD_BO_NOMAP, "vsc_draw_strm");
   }

   if (!fd6_ctx->vsc_prim_strm) {
      unsigned sz = max_vsc_pipes * fd6_ctx->vsc_prim_strm_pitch;
      fd6_ctx->vsc_prim_strm =
         fd_bo_new(ctx->screen->dev, sz, FD_BO_NOMAP, "vsc_prim_strm");
   }

   cs.attach_bo(fd6_ctx->vsc_draw_strm);
   cs.attach_bo(fd6_ctx->vsc_prim_strm);

   fd_ncrb<CHIP> ncrb(cs, 12 + max_vsc_pipes);

   ncrb.add(VSC_BIN_SIZE(CHIP, .binw = gmem->bin_w, .binh = gmem->bin_h));
   ncrb.add(A6XX_VSC_SIZE_BASE(
      .bo = fd6_ctx->vsc_draw_strm,
      .bo_offset = max_vsc_pipes * fd6_ctx->vsc_draw_strm_pitch
   ));

   ncrb.add(A6XX_VSC_EXPANDED_BIN_CNTL(.nx = gmem->nbins_x, .ny = gmem->nbins_y));

   for (i = 0; i < max_vsc_pipes; i++) {
      const struct fd_vsc_pipe *pipe = &gmem->vsc_pipe[i];
      ncrb.add(A6XX_VSC_PIPE_CONFIG_REG(i,
         .x = pipe->x, .y = pipe->y,
         .w = pipe->w, .h = pipe->h));
   }

   ncrb.add(A6XX_VSC_PIPE_DATA_PRIM_BASE(.bo = fd6_ctx->vsc_prim_strm));
   ncrb.add(A6XX_VSC_PIPE_DATA_PRIM_STRIDE(.dword = fd6_ctx->vsc_prim_strm_pitch));
   ncrb.add(A6XX_VSC_PIPE_DATA_PRIM_LENGTH(.dword = fd6_ctx->vsc_prim_strm_pitch - 64));

   ncrb.add(A6XX_VSC_PIPE_DATA_DRAW_BASE(.bo = fd6_ctx->vsc_draw_strm));
   ncrb.add(A6XX_VSC_PIPE_DATA_DRAW_STRIDE(.dword = fd6_ctx->vsc_draw_strm_pitch));
   ncrb.add(A6XX_VSC_PIPE_DATA_DRAW_LENGTH(.dword = fd6_ctx->vsc_draw_strm_pitch - 64));
}

/*
 * If overflow is detected, either 0x1 (VSC_DRAW_STRM overflow) or 0x3
 * (VSC_PRIM_STRM overflow) plus the size of the overflowed buffer is
 * written to control->vsc_overflow.  This allows the CPU to
 * detect which buffer overflowed (and, since the current size is
 * encoded as well, this protects against already-submitted but
 * not executed batches from fooling the CPU into increasing the
 * size again unnecessarily).
 */
static void
emit_vsc_overflow_test(struct fd_batch *batch)
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd6_context *fd6_ctx = fd6_context(batch->ctx);
   fd_cs cs(batch->gmem);

   assert((fd6_ctx->vsc_draw_strm_pitch & 0x3) == 0);
   assert((fd6_ctx->vsc_prim_strm_pitch & 0x3) == 0);

   /* Check for overflow, write vsc_scratch if detected: */
   for (int i = 0; i < gmem->num_vsc_pipes; i++) {
      fd_pkt7(cs, CP_COND_WRITE5, 8)
         .add(CP_COND_WRITE5_0(
            .function = WRITE_GE,
            .write_memory = true,
         ))
         .add(CP_COND_WRITE5_POLL_ADDR(
            .qword = REG_A6XX_VSC_PIPE_DATA_DRAW_SIZE(i),
         ))
         .add(CP_COND_WRITE5_3(
            .ref = fd6_ctx->vsc_draw_strm_pitch - 64,
         ))
         .add(CP_COND_WRITE5_4(.mask = ~0))
         .add(CP_COND_WRITE5_WRITE_ADDR(control_ptr(fd6_ctx, vsc_overflow)))
         .add(CP_COND_WRITE5_7(.write_data = 1 + fd6_ctx->vsc_draw_strm_pitch));

      fd_pkt7(cs, CP_COND_WRITE5, 8)
         .add(CP_COND_WRITE5_0(
            .function = WRITE_GE,
            .write_memory = true,
         ))
         .add(CP_COND_WRITE5_POLL_ADDR(
            .qword = REG_A6XX_VSC_PIPE_DATA_PRIM_SIZE(i),
         ))
         .add(CP_COND_WRITE5_3(
            .ref = fd6_ctx->vsc_prim_strm_pitch - 64,
         ))
         .add(CP_COND_WRITE5_4(.mask = ~0))
         .add(CP_COND_WRITE5_WRITE_ADDR(control_ptr(fd6_ctx, vsc_overflow)))
         .add(CP_COND_WRITE5_7(.write_data = 1 + fd6_ctx->vsc_prim_strm_pitch));
   }

   fd_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
}

static void
check_vsc_overflow(struct fd_context *ctx)
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd6_control *control =
         (struct fd6_control *)fd_bo_map(fd6_ctx->control_mem);
   uint32_t vsc_overflow = control->vsc_overflow;

   if (!vsc_overflow)
      return;

   /* clear overflow flag: */
   control->vsc_overflow = 0;

   unsigned buffer = vsc_overflow & 0x3;
   unsigned size = vsc_overflow & ~0x3;

   if (buffer == 0x1) {
      /* VSC_DRAW_STRM overflow: */

      if (size < fd6_ctx->vsc_draw_strm_pitch) {
         /* we've already increased the size, this overflow is
          * from a batch submitted before resize, but executed
          * after
          */
         return;
      }

      fd_bo_del(fd6_ctx->vsc_draw_strm);
      fd6_ctx->vsc_draw_strm = NULL;
      fd6_ctx->vsc_draw_strm_pitch *= 2;

      mesa_logd("resized VSC_PIPE_DATA_DRAW_STRIDE to: 0x%x",
                fd6_ctx->vsc_draw_strm_pitch);

   } else if (buffer == 0x3) {
      /* VSC_PRIM_STRM overflow: */

      if (size < fd6_ctx->vsc_prim_strm_pitch) {
         /* we've already increased the size */
         return;
      }

      fd_bo_del(fd6_ctx->vsc_prim_strm);
      fd6_ctx->vsc_prim_strm = NULL;
      fd6_ctx->vsc_prim_strm_pitch *= 2;

      mesa_logd("resized VSC_PIPE_DATA_PRIM_STRIDE to: 0x%x",
                fd6_ctx->vsc_prim_strm_pitch);

   } else {
      /* NOTE: it's possible, for example, for overflow to corrupt the
       * control page.  I mostly just see this hit if I set initial VSC
       * buffer size extremely small.  Things still seem to recover,
       * but maybe we should pre-emptively realloc vsc_data/vsc_data2
       * and hope for different memory placement?
       */
      mesa_loge("invalid vsc_overflow value: 0x%08x", vsc_overflow);
   }
}

template <chip CHIP>
static void
emit_common_init(fd_cs &cs, struct fd_batch *batch)
{
   struct fd_context *ctx = batch->ctx;
   struct fd_autotune *at = &batch->ctx->autotune;
   struct fd_batch_result *result = batch->autotune_result;

   if (!result)
      return;

   cs.attach_bo(at->results_mem);

   fd_pkt4(cs, 1)
      .add(A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));

   if (!ctx->screen->info->props.has_event_write_sample_count) {
      fd_pkt4(cs, 2)
         .add(A6XX_RB_SAMPLE_COUNTER_BASE(
            results_ptr(at, result[result->idx].samples_start)
         ));

      fd6_event_write<CHIP>(ctx, cs, FD_ZPASS_DONE);

      /* Copied from blob's cmdstream, not sure why it is done. */
      if (CHIP == A7XX) {
         fd6_event_write<CHIP>(ctx, cs, FD_CCU_CLEAN_DEPTH);
      }
   } else {
      fd_pkt7(cs, CP_EVENT_WRITE7, 3)
         .add(CP_EVENT_WRITE7_0(
            .event = ZPASS_DONE,
            .write_sample_count = true,
         ))
         .add(EV_DST_RAM_CP_EVENT_WRITE7_1(
            results_ptr(at, result[result->idx].samples_start)
         ));
   }
}

template <chip CHIP>
static void
emit_common_fini(fd_cs &cs, struct fd_batch *batch)
{
   struct fd_context *ctx = batch->ctx;
   struct fd_autotune *at = &batch->ctx->autotune;
   struct fd_batch_result *result = batch->autotune_result;

   fd6_emit_flushes<CHIP>(batch->ctx, cs, batch->barrier);

   if (!result)
      return;

   cs.attach_bo(at->results_mem);

   if (!ctx->screen->info->props.has_event_write_sample_count) {
      with_crb (cs, 3) {
         crb.add(A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));
         crb.add(A6XX_RB_SAMPLE_COUNTER_BASE(
            results_ptr(at, result[result->idx].samples_end)
         ));
      }

      fd6_event_write<CHIP>(batch->ctx, cs, FD_ZPASS_DONE);
   } else {
      fd_pkt4(cs, 1)
         .add(A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));

      fd_pkt7(cs, CP_EVENT_WRITE7, 3)
         .add(CP_EVENT_WRITE7_0(
            .event = ZPASS_DONE,
            .write_sample_count = true,
            .sample_count_end_offset = true,
            .write_accum_sample_count_diff = true,
         ))
         .add(EV_DST_RAM_CP_EVENT_WRITE7_1(
            results_ptr(at, result[result->idx].samples_start)
         ));
   }

   fd6_fence_write<CHIP>(cs, result->fence, results_ptr(at, fence));
}

/*
 * Emit conditional CP_INDIRECT_BRANCH based on VSC_CHANNEL_VISIBILITY[p],
 * ie. the IB is skipped for tiles that have no visible geometry.
 *
 * If we aren't using binning pass, this just emits a normal IB.
 */
template <chip CHIP>
static void
emit_conditional_ib(fd_cs &cs, struct fd_batch *batch, const struct fd_tile *tile,
                    struct fd_ringbuffer *target)
{
   /* If we have fast clear, that won't count in the VSC state, so it
    * forces an unconditional IB (because we know there is something
    * to do for this tile)
    */
   if (batch->cleared || !use_hw_binning(batch)) {
      fd6_emit_ib<CHIP>(cs, target);
      return;
   }

   if (target->cur == target->start)
      return;

   unsigned count = fd_ringbuffer_cmd_count(target);

   BEGIN_RING(cs, 5 + 4 * count); /* ensure conditional doesn't get split */

   fd_pkt7(cs, CP_REG_TEST, 1)
      .add(A6XX_CP_REG_TEST_0(
         .reg = REG_A6XX_VSC_CHANNEL_VISIBILITY(tile->p),
         .bit = tile->n,
         .skip_wait_for_me = true,
      ));

   fd_pkt7(cs, CP_COND_REG_EXEC, 2)
      .add(CP_COND_REG_EXEC_0_MODE(PRED_TEST))
      .add(PRED_TEST_CP_COND_REG_EXEC_1_DWORDS(4 * count));

   for (unsigned i = 0; i < count; i++) {
      uint32_t dwords;

      fd_pkt7(cs, CP_INDIRECT_BUFFER, 3)
         .add(target, i, &dwords)
         .add(A5XX_CP_INDIRECT_BUFFER_2(.ib_size = dwords));

      assert(dwords > 0);
   }
}

template <chip CHIP>
static void
set_scissor(fd_cs &cs, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2)
{
   fd_pkt4(cs, 2)
      .add(GRAS_SC_WINDOW_SCISSOR_TL(CHIP, .x = x1, .y = y1))
      .add(GRAS_SC_WINDOW_SCISSOR_BR(CHIP, .x = x2, .y = y2));

   fd_pkt4(cs, 2)
      .add(GRAS_A2D_SCISSOR_TL(CHIP, .x = x1, .y = y1))
      .add(GRAS_A2D_SCISSOR_BR(CHIP, .x = x2, .y = y2));
}

template <chip CHIP>
static void
set_tessfactor_bo(fd_cs &cs, struct fd_batch *batch)
{
   /* This happens after all drawing has been emitted to the draw CS, so we know
    * whether we need the tess BO pointers.
    */
   if (!batch->tessellation)
      return;

   struct fd_screen *screen = batch->ctx->screen;

   assert(screen->tess_bo);
   cs.attach_bo(screen->tess_bo);

   fd_pkt4(cs, 2)
      .add(PC_TESS_BASE(CHIP, screen->tess_bo));

   /* Updating PC_TESS_BASE could race with the next draw which uses it. */
   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);
}

struct bin_size_params {
   enum a6xx_render_mode render_mode;
   bool force_lrz_write_dis;
   enum a6xx_buffers_location buffers_location;
   enum a6xx_lrz_feedback_mask lrz_feedback_zmode_mask;
};

/* nregs: 17 */
template <chip CHIP>
static void
set_bin_size(fd_crb &crb, const struct fd_gmem_stateobj *gmem, struct bin_size_params p)
{
   unsigned w = gmem ? gmem->bin_w : 0;
   unsigned h = gmem ? gmem->bin_h : 0;

   crb.add(GRAS_SC_BIN_CNTL(CHIP,
         .binw = w, .binh = h,
         .render_mode = p.render_mode,
         .force_lrz_write_dis = p.force_lrz_write_dis,
         .buffers_location = p.buffers_location,
         .lrz_feedback_zmode_mask = p.lrz_feedback_zmode_mask,
         .force_bi_dir_lrz_disable = true,
   ));
   crb.add(RB_CNTL(
         CHIP,
         .binw = w, .binh = h,
         .render_mode = p.render_mode,
         .force_lrz_write_dis = p.force_lrz_write_dis,
         .buffers_location = p.buffers_location,
         .lrz_feedback_zmode_mask = p.lrz_feedback_zmode_mask,
   ));
   crb.add(A6XX_VFD_RENDER_MODE(p.render_mode));

   if (CHIP >= A8XX) {
      crb.add(TPL1_BIN_SIZE(CHIP, .binw = w, .binh = h));
      crb.add(TPL1_A2D_BIN_SIZE(CHIP, .binw = w, .binh = h));
      crb.add(SP_BIN_SIZE(CHIP, .binw = w, .binh = h));

      for (int i = 0; i < 8; i++) {
         crb.add(RB_MRT_GMEM_DIMENSION_REG(CHIP, i,
            .width = COND(gmem && gmem->cbuf_cpp[i], w),
            .height = COND(gmem && gmem->cbuf_cpp[i], h),
         ));
      }
      crb.add(RB_DEPTH_GMEM_DIMENSION(CHIP,
         .width = COND(gmem && gmem->zsbuf_cpp[0], w),
         .height = COND(gmem && gmem->zsbuf_cpp[0], h),
      ));
      crb.add(RB_STENCIL_GMEM_DIMENSION(CHIP,
         .width = COND(gmem && (gmem->zsbuf_cpp[0] || gmem->zsbuf_cpp[1]), w),
         .height = COND(gmem && (gmem->zsbuf_cpp[0] || gmem->zsbuf_cpp[1]), h),
      ));
   }

   /* no flag for RB_RESOLVE_CNTL_3... */
   crb.add(RB_RESOLVE_CNTL_3(CHIP, .binw = w, .binh = h));
}

template <chip CHIP>
static void
emit_binning_pass(fd_cs &cs, struct fd_batch *batch) assert_dt
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;

   assert(!batch->tessellation);

   set_scissor<CHIP>(cs, 0, 0, gmem->width - 1, gmem->height - 1);

   fd6_set_render_mode<CHIP>(cs, {RM6_BIN_VISIBILITY});

   fd_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1)
      .add(0x1);

   fd_pkt7(cs, CP_SET_MODE, 1)
      .add(0x1);

   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);

   fd_pkt4(cs, 1)
      .add(A6XX_VFD_RENDER_MODE(.render_mode = BINNING_PASS));

   update_vsc_pipe<CHIP>(cs, batch);

   fd6_event_write<CHIP>(batch->ctx, cs, FD_VSC_BINNING_START);

   fd_crb(cs, 2)
      .add(A6XX_RB_WINDOW_OFFSET(.x = 0, .y = 0))
      .add(A6XX_TPL1_WINDOW_OFFSET(.x = 0, .y = 0));

   /* emit IB to binning drawcmds: */
   trace_start_binning_ib(&batch->trace, cs);
   foreach_subpass (subpass, batch) {
      emit_lrz<CHIP>(cs, batch, subpass);
      fd6_emit_ib<CHIP>(cs, subpass->draw);
   }
   trace_end_binning_ib(&batch->trace, cs);

   fd_pkt7(cs, CP_SET_DRAW_STATE, 3)
      .add(CP_SET_DRAW_STATE__0(0, .disable_all_groups = true))
      .add(CP_SET_DRAW_STATE__ADDR(0));

   fd6_event_write<CHIP>(batch->ctx, cs, FD_VSC_BINNING_END);

   /* This flush is probably required because the VSC, which produces the
    * visibility stream, is a client of UCHE, whereas the CP needs to read
    * the visibility stream (without caching) to do draw skipping. The
    * WFI+WAIT_FOR_ME combination guarantees that the binning commands
    * submitted are finished before reading the VSC regs (in
    * emit_vsc_overflow_test) or the VSC_DATA buffer directly (implicitly
    * as part of draws).
    */
   fd6_emit_flushes<CHIP>(batch->ctx, cs,
                          FD6_FLUSH_CACHE |
                          FD6_WAIT_FOR_IDLE |
                          FD6_WAIT_FOR_ME);

   trace_start_vsc_overflow_test(&batch->trace, cs);
   emit_vsc_overflow_test(batch);
   trace_end_vsc_overflow_test(&batch->trace, cs);

   fd_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1)
      .add(0x0);

   fd_pkt7(cs, CP_SET_MODE, 1)
      .add(0x0);

   if (CHIP >= A7XX) {
      fd6_set_render_mode<CHIP>(cs, {RM7_BIN_VISIBILITY_END});
   }
}

/* nregs: 7 */
template <chip CHIP>
static void
emit_msaa(fd_crb &crb, unsigned nr)
{
   enum a3xx_msaa_samples samples = fd_msaa_samples(nr);

   crb.add(A6XX_TPL1_RAS_MSAA_CNTL(.samples = samples));
   crb.add(A6XX_TPL1_DEST_MSAA_CNTL(
      .samples = samples,
      .msaa_disable = (samples == MSAA_ONE),
   ));

   crb.add(GRAS_SC_RAS_MSAA_CNTL(CHIP, .samples = samples));
   crb.add(GRAS_SC_DEST_MSAA_CNTL(CHIP,
      .samples = samples,
      .msaa_disable = (samples == MSAA_ONE),
   ));

   crb.add(A6XX_RB_RAS_MSAA_CNTL(.samples = samples));
   crb.add(A6XX_RB_DEST_MSAA_CNTL(
      .samples = samples,
      .msaa_disable = (samples == MSAA_ONE),
   ));

   crb.add(A6XX_RB_RESOLVE_GMEM_BUFFER_INFO(.samples = samples));
}

template <chip CHIP>
static void prepare_tile_setup(struct fd_batch *batch);
template <chip CHIP>
static void prepare_tile_fini(struct fd_batch *batch);

template <chip CHIP>
static void
fd7_emit_static_binning_regs(fd_cs &cs, bool gmem)
{
   bool full_in_gmem = false;  /* gen8 TODO */
   bool sysmem = !gmem;
   fd_ncrb<CHIP> ncrb(cs, 4);

   assert(CHIP >= A7XX);

   ncrb.add(RB_BUFFER_CNTL(CHIP,
      .z_sysmem = sysmem,
      .s_sysmem = sysmem,
      .rt0_sysmem = sysmem,
      .rt1_sysmem = sysmem,
      .rt2_sysmem = sysmem,
      .rt3_sysmem = sysmem,
      .rt4_sysmem = sysmem,
      .rt5_sysmem = sysmem,
      .rt6_sysmem = sysmem,
      .rt7_sysmem = sysmem,
      .z_full_in_gmem = full_in_gmem,
      .s_full_in_gmem = full_in_gmem,
      .rt0_full_in_gmem = full_in_gmem,
      .rt1_full_in_gmem = full_in_gmem,
      .rt2_full_in_gmem = full_in_gmem,
      .rt3_full_in_gmem = full_in_gmem,
      .rt4_full_in_gmem = full_in_gmem,
      .rt5_full_in_gmem = full_in_gmem,
      .rt6_full_in_gmem = full_in_gmem,
      .rt7_full_in_gmem = full_in_gmem,
   ));
   ncrb.add(GRAS_LRZ_CB_CNTL(CHIP, 0x0));
   ncrb.add(GRAS_MODE_CNTL(CHIP, 0x2));
   ncrb.add(RB_CLEAR_TARGET(CHIP,
      .clear_mode = gmem ? CLEAR_MODE_GMEM : CLEAR_MODE_SYSMEM,
   ));
}

template <chip CHIP>
struct fd_ringbuffer *
fd6_build_preemption_preamble(struct fd_context *ctx, bool gmem)
{
   struct fd_screen *screen = ctx->screen;

   fd_cs cs(ctx->pipe, 0x1000);

   fd6_emit_static_regs<CHIP>(cs, ctx);
   fd6_emit_gmem_cache_cntl<CHIP>(cs, screen, gmem);

   if (CHIP >= A7XX) {
      fd7_emit_static_binning_regs<CHIP>(cs, gmem);
   }

   /* TODO use CP_MEM_TO_SCRATCH_MEM on a7xx. The VSC scratch mem should be
    * automatically saved, unlike GPU registers, so we wouldn't have to
    * manually restore this state.
    */
   fd_pkt7(cs, CP_MEM_TO_REG, 3)
      .add(CP_MEM_TO_REG_0(
         .reg = REG_A6XX_VSC_CHANNEL_VISIBILITY(0),
         .cnt = 32,
      ))
      .add(A5XX_CP_MEM_TO_REG_SRC(
         control_ptr(fd6_context(ctx), vsc_state),
      ));

   return cs;
}
FD_GENX(fd6_build_preemption_preamble);

static void
emit_preamble(struct fd_context *ctx, fd_cs &cs, bool gmem)
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd_ringbuffer *preamble = gmem ? fd6_ctx->gmem_preamble : fd6_ctx->sysmem_preamble;
   uint32_t dwords;

   fd_pkt7(cs, CP_SET_AMBLE, 3)
      .add(preamble, 0, &dwords)
      .add(CP_SET_AMBLE_2(.dwords = dwords, .type = BIN_PREAMBLE_AMBLE_TYPE));

   fd_pkt7(cs, CP_SET_AMBLE, 3)
      .add(CP_SET_AMBLE_ADDR())
      .add(CP_SET_AMBLE_2(.type = PREAMBLE_AMBLE_TYPE));

   fd_pkt7(cs, CP_SET_AMBLE, 3)
      .add(CP_SET_AMBLE_ADDR())
      .add(CP_SET_AMBLE_2(.type = POSTAMBLE_AMBLE_TYPE));
}

/* before first tile */
template <chip CHIP>
static void
fd6_emit_tile_init(struct fd_batch *batch) assert_dt
{
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd_screen *screen = batch->ctx->screen;
   fd_cs cs(batch->gmem);

   emit_lrz_clears<CHIP>(batch);

   fd6_emit_restore<CHIP>(cs, batch);
   emit_preamble(batch->ctx, cs, true);

   fd6_event_write<CHIP>(batch->ctx, cs, FD_LRZ_FLUSH);

   if (batch->prologue) {
      trace_start_prologue(&batch->trace, cs);
      fd6_emit_ib<CHIP>(cs, batch->prologue);
      trace_end_prologue(&batch->trace, cs);
   }

   fd6_cache_inv<CHIP>(batch->ctx, cs);

   prepare_tile_setup<CHIP>(batch);
   prepare_tile_fini<CHIP>(batch);

   fd_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1)
      .add(0x0);

   /* blob controls "local" in IB2, but I think that is not required */
   fd_pkt7(cs, CP_SKIP_IB2_ENABLE_LOCAL, 1)
      .add(0x1);

   fd6_emit_gmem_cache_cntl<CHIP>(cs, screen, true);

   with_crb (cs, 150) {
      emit_zs<CHIP>(crb, &pfb->zsbuf, batch->gmem_state);
      emit_mrt<CHIP>(crb, pfb, batch->gmem_state);
      emit_msaa<CHIP>(crb, pfb->samples);
   }

   patch_fb_read_gmem<CHIP>(batch);

   if (CHIP >= A7XX) {
      fd7_emit_static_binning_regs<CHIP>(cs, true);
   }

   if (use_hw_binning(batch)) {
      /* enable stream-out during binning pass: */
      with_crb (cs, 18) {
         crb.add(VPC_SO_OVERRIDE(CHIP, false));

         set_bin_size<CHIP>(crb, gmem, {
               .render_mode = BINNING_PASS,
               .buffers_location = BUFFERS_IN_GMEM,
               .lrz_feedback_zmode_mask = LRZ_FEEDBACK_NONE,
         });
      }

      update_render_cntl<CHIP>(cs, screen, pfb, true);
      emit_binning_pass<CHIP>(cs, batch);

      with_crb (cs, 18) {
         /* and disable stream-out for draw pass: */
         crb.add(VPC_SO_OVERRIDE(CHIP, true));

         /*
          * NOTE: even if we detect VSC overflow and disable use of
          * visibility stream in draw pass, it is still safe to execute
          * the reset of these cmds:
          */

         set_bin_size<CHIP>(crb, gmem, {
               .render_mode = RENDERING_PASS,
               .force_lrz_write_dis = !screen->info->props.has_lrz_feedback,
               .buffers_location = BUFFERS_IN_GMEM,
               .lrz_feedback_zmode_mask = screen->info->props.has_lrz_feedback
                                             ? LRZ_FEEDBACK_EARLY_Z_LATE_Z
                                             : LRZ_FEEDBACK_NONE,
         });
      }

      fd_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1)
         .add(0x1);

      /* Upload state regs to memory to be restored on skipsaverestore
       * preemption.
       */
      fd_pkt7(cs, CP_REG_TO_MEM, 3)
         .add(CP_REG_TO_MEM_0(
            .reg = REG_A6XX_VSC_CHANNEL_VISIBILITY(0),
            .cnt = 32,
         ))
         .add(A5XX_CP_REG_TO_MEM_DEST(
            control_ptr(fd6_context(batch->ctx), vsc_state)
         ));
   } else {
      with_crb (cs, 18) {
         /* no binning pass, so enable stream-out for draw pass: */
         crb.add(VPC_SO_OVERRIDE(CHIP, false));

         set_bin_size<CHIP>(crb, gmem, {
               .render_mode = RENDERING_PASS,
               .force_lrz_write_dis = !screen->info->props.has_lrz_feedback,
               .buffers_location = BUFFERS_IN_GMEM,
               .lrz_feedback_zmode_mask =
                  screen->info->props.has_lrz_feedback
                     ? LRZ_FEEDBACK_EARLY_Z_OR_EARLY_Z_LATE_Z
                     : LRZ_FEEDBACK_NONE,
         });
      }
   }

   update_render_cntl<CHIP>(cs, screen, pfb, false);

   emit_common_init<CHIP>(cs, batch);
}

/* nregs: 5 */
template <chip CHIP>
static void
set_window_offset(fd_crb &crb, uint32_t x1, uint32_t y1)
{
   crb.add(A6XX_RB_WINDOW_OFFSET(.x = x1, .y = y1));
   crb.add(A6XX_RB_RESOLVE_WINDOW_OFFSET(.x = x1, .y = y1));
   crb.add(SP_WINDOW_OFFSET(CHIP, .x = x1, .y = y1));
   crb.add(A6XX_TPL1_WINDOW_OFFSET(.x = x1, .y = y1));
   if (CHIP >= A8XX)
      crb.add(TPL1_A2D_WINDOW_OFFSET(CHIP, .x = x1, .y = y1));
}

/* before mem2gmem */
template <chip CHIP>
static void
fd6_emit_tile_prep(struct fd_batch *batch, const struct fd_tile *tile)
{
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   struct fd_screen *screen = batch->ctx->screen;
   struct fd_context *ctx = batch->ctx;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   fd_cs cs(batch->gmem);

   fd6_set_render_mode<CHIP>(cs, {.mode = RM6_BIN_RENDER_START, .uses_gmem = true});

   uint32_t x1 = tile->xoff;
   uint32_t y1 = tile->yoff;
   uint32_t x2 = tile->xoff + tile->bin_w - 1;
   uint32_t y2 = tile->yoff + tile->bin_h - 1;

   set_scissor<CHIP>(cs, x1, y1, x2, y2);
   set_tessfactor_bo<CHIP>(cs, batch);

   with_crb (cs, 150) {
      emit_zs<CHIP>(crb, &pfb->zsbuf, batch->gmem_state);
      emit_mrt<CHIP>(crb, pfb, batch->gmem_state);
      emit_msaa<CHIP>(crb, pfb->samples);
   }

   if (use_hw_binning(batch)) {
      const struct fd_vsc_pipe *pipe = &gmem->vsc_pipe[tile->p];
      unsigned num_vsc_pipes = screen->info->num_vsc_pipes;

      fd_pkt7(cs, CP_WAIT_FOR_ME, 0);

      fd_pkt7(cs, CP_SET_MODE, 1)
         .add(0x0);

      fd_pkt7(cs, CP_SET_BIN_DATA5, 7)
         /* A702 also sets BIT(0) but that hangchecks */
         .add(CP_SET_BIN_DATA5_0(
            .vsc_size = pipe->w * pipe->h,
            .vsc_n = tile->n,
         ))
         .add(NO_ABS_MASK_CP_SET_BIN_DATA5_BIN_DATA_ADDR(
            .bo = fd6_ctx->vsc_draw_strm,
            .bo_offset = tile->p * fd6_ctx->vsc_draw_strm_pitch,
         ))
         .add(NO_ABS_MASK_CP_SET_BIN_DATA5_BIN_SIZE_ADDR(
            .bo = fd6_ctx->vsc_draw_strm, /* VSC_PIPE_DATA_DRAW_BASE + (p * 4) */
            .bo_offset = (tile->p * 4) + (num_vsc_pipes * fd6_ctx->vsc_draw_strm_pitch),
         ))
         .add(NO_ABS_MASK_CP_SET_BIN_DATA5_BIN_PRIM_STRM(
            .bo = fd6_ctx->vsc_prim_strm,
            .bo_offset = tile->p * fd6_ctx->vsc_prim_strm_pitch,
         ));

      fd_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1)
         .add(0x0);

      with_crb (cs, 18) {
         crb.add(VPC_SO_OVERRIDE(CHIP, true));

         /*
         * NOTE: even if we detect VSC overflow and disable use of
         * visibility stream in draw pass, it is still safe to execute
         * the reset of these cmds:
         */

         set_bin_size<CHIP>(crb, gmem, {
               .render_mode = RENDERING_PASS,
               .force_lrz_write_dis = !screen->info->props.has_lrz_feedback,
               .buffers_location = BUFFERS_IN_GMEM,
               .lrz_feedback_zmode_mask = screen->info->props.has_lrz_feedback
                                             ? LRZ_FEEDBACK_EARLY_Z_LATE_Z
                                             : LRZ_FEEDBACK_NONE,
         });
      }

      fd_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1)
         .add(0x1);
   } else {
      fd_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1)
         .add(0x1);

      /* no binning pass, so enable stream-out for draw pass:: */
      fd_pkt4(cs, 1)
         .add(VPC_SO_OVERRIDE(CHIP, false));
   }

   with_crb (cs, 22) {
      set_window_offset<CHIP>(crb, x1, y1);

      set_bin_size<CHIP>(crb, gmem, {
            .render_mode = RENDERING_PASS,
            .force_lrz_write_dis = !screen->info->props.has_lrz_feedback,
            .buffers_location = BUFFERS_IN_GMEM,
            .lrz_feedback_zmode_mask = screen->info->props.has_lrz_feedback
                                          ? LRZ_FEEDBACK_EARLY_Z_LATE_Z
                                          : LRZ_FEEDBACK_NONE,
      });
   }

   fd_pkt7(cs, CP_SET_MODE, 1)
      .add(0x0);
}

static void
set_blit_scissor(struct fd_batch *batch, fd_cs &cs)
{
   const struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   struct pipe_scissor_state blit_scissor;

   blit_scissor.minx = 0;
   blit_scissor.miny = 0;
   blit_scissor.maxx = MAX2(1, align(pfb->width, 16));
   blit_scissor.maxy = MAX2(1, align(pfb->height, 4));

   fd_pkt4(cs, 2)
      .add(A6XX_RB_RESOLVE_CNTL_1(.x = blit_scissor.minx, .y = blit_scissor.miny))
      .add(A6XX_RB_RESOLVE_CNTL_2(.x = blit_scissor.maxx - 1, .y = blit_scissor.maxy - 1));
}

/* nregs: 10 */
template <chip CHIP>
static void
emit_blit(struct fd_batch *batch, fd_crb &crb, uint32_t base,
          struct pipe_surface *psurf, bool stencil)
{
   struct fd_resource *rsc = fd_resource(psurf->texture);
   enum pipe_format pfmt = psurf->format;
   uint32_t offset;
   bool ubwc_enabled;

   assert(psurf->first_layer == psurf->last_layer);

   /* separate stencil case: */
   if (stencil) {
      rsc = rsc->stencil;
      pfmt = rsc->b.b.format;
   }

   offset =
      fd_resource_offset(rsc, psurf->level, psurf->first_layer);
   ubwc_enabled = fd_resource_ubwc_enabled(rsc, psurf->level);

   assert(psurf->first_layer == psurf->last_layer);

   enum a6xx_tile_mode tile_mode = (enum a6xx_tile_mode)
         fd_resource_tile_mode(&rsc->b.b, psurf->level);
   enum a6xx_format format = fd6_color_format(pfmt, tile_mode);
   uint32_t stride = fd_resource_pitch(rsc, psurf->level);
   uint32_t array_stride = fd_resource_layer_stride(rsc, psurf->level);
   enum a3xx_color_swap swap =
         fd6_color_swap(pfmt, (enum a6xx_tile_mode)rsc->layout.tile_mode,
                        false);
   enum a3xx_msaa_samples samples = fd_msaa_samples(rsc->b.b.nr_samples);

   crb.add(A6XX_RB_RESOLVE_SYSTEM_BUFFER_INFO(
      .tile_mode = tile_mode,
      .flags = ubwc_enabled,
      .samples = samples,
      .color_swap = swap,
      .color_format = format,
   ));
   crb.add(A6XX_RB_RESOLVE_SYSTEM_BUFFER_BASE(.bo = rsc->bo, .bo_offset = offset));
   crb.add(A6XX_RB_RESOLVE_SYSTEM_BUFFER_PITCH(stride));
   crb.add(A6XX_RB_RESOLVE_SYSTEM_BUFFER_ARRAY_PITCH(array_stride));

   crb.add(A6XX_RB_RESOLVE_GMEM_BUFFER_BASE(.dword = base));

   if (ubwc_enabled) {
      crb.add(A6XX_RB_RESOLVE_SYSTEM_FLAG_BUFFER_BASE(
         .bo = rsc->bo,
         .bo_offset = fd_resource_ubwc_offset(rsc, psurf->level, psurf->first_layer),
      ));
      crb.add(A6XX_RB_RESOLVE_SYSTEM_FLAG_BUFFER_PITCH(
         .pitch = fdl_ubwc_pitch(&rsc->layout, psurf->level),
         .array_pitch = rsc->layout.ubwc_layer_size >> 2,
      ));
   }

   if (CHIP >= A7XX)
      crb.add(RB_CLEAR_TARGET(CHIP, .clear_mode = CLEAR_MODE_GMEM));
}

template <chip CHIP>
static void
emit_restore_blit(struct fd_batch *batch, fd_cs &cs, uint32_t base,
                  struct pipe_surface *psurf, unsigned buffer)
{
   bool stencil = (buffer == FD_BUFFER_STENCIL);

   with_crb (cs, 11) {
      crb.add(A6XX_RB_RESOLVE_OPERATION(
         .type = BLIT_EVENT_LOAD,
         .sample_0 = util_format_is_pure_integer(psurf->format),
         .depth = (buffer == FD_BUFFER_DEPTH),
      ));

      emit_blit<CHIP>(batch, crb, base, psurf, stencil);
   }

   fd6_event_write<CHIP>(batch->ctx, cs, FD_CCU_RESOLVE);
}

template <chip CHIP>
static void
emit_subpass_clears(struct fd_batch *batch, fd_cs &cs, struct fd_batch_subpass *subpass)
{
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   enum a3xx_msaa_samples samples = fd_msaa_samples(pfb->samples);

   uint32_t buffers = subpass->fast_cleared;

   if (buffers & PIPE_CLEAR_COLOR) {

      for (int i = 0; i < pfb->nr_cbufs; i++) {
         union pipe_color_union *color = &subpass->clear_color[i];
         union util_color uc = {0};

         if (!pfb->cbufs[i].texture)
            continue;

         if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
            continue;

         enum pipe_format pfmt = pfb->cbufs[i].format;

         // XXX I think RB_CLEAR_COLOR_DWn wants to take into account SWAP??
         union pipe_color_union swapped;
         switch (fd6_color_swap(pfmt, TILE6_LINEAR, false)) {
         case WZYX:
            swapped.ui[0] = color->ui[0];
            swapped.ui[1] = color->ui[1];
            swapped.ui[2] = color->ui[2];
            swapped.ui[3] = color->ui[3];
            break;
         case WXYZ:
            swapped.ui[2] = color->ui[0];
            swapped.ui[1] = color->ui[1];
            swapped.ui[0] = color->ui[2];
            swapped.ui[3] = color->ui[3];
            break;
         case ZYXW:
            swapped.ui[3] = color->ui[0];
            swapped.ui[0] = color->ui[1];
            swapped.ui[1] = color->ui[2];
            swapped.ui[2] = color->ui[3];
            break;
         case XYZW:
            swapped.ui[3] = color->ui[0];
            swapped.ui[2] = color->ui[1];
            swapped.ui[1] = color->ui[2];
            swapped.ui[0] = color->ui[3];
            break;
         }

         util_pack_color_union(pfmt, &uc, &swapped);

         with_crb (cs, 9) {
            crb.add(A6XX_RB_RESOLVE_SYSTEM_BUFFER_INFO(
               .tile_mode = TILE6_LINEAR,
               .samples = samples,
               .color_format = fd6_color_format(pfmt, TILE6_LINEAR),
            ));

            crb.add(A6XX_RB_RESOLVE_OPERATION(
               .type = BLIT_EVENT_CLEAR,
               .clear_mask = 0xf,
            ));

            crb.add(A6XX_RB_RESOLVE_GMEM_BUFFER_BASE(gmem->cbuf_base[i]));
            crb.add(A6XX_RB_RESOLVE_CNTL_0());

            crb.add(A6XX_RB_RESOLVE_CLEAR_COLOR_DW0(uc.ui[0]));
            crb.add(A6XX_RB_RESOLVE_CLEAR_COLOR_DW1(uc.ui[1]));
            crb.add(A6XX_RB_RESOLVE_CLEAR_COLOR_DW2(uc.ui[2]));
            crb.add(A6XX_RB_RESOLVE_CLEAR_COLOR_DW3(uc.ui[3]));

            if (CHIP >= A7XX)
               crb.add(RB_CLEAR_TARGET(CHIP, .clear_mode = CLEAR_MODE_GMEM));
         }

         fd6_event_write<CHIP>(batch->ctx, cs, FD_CCU_RESOLVE);
      }
   }

   const bool has_depth = !!pfb->zsbuf.texture;
   const bool has_separate_stencil =
      has_depth && fd_resource(pfb->zsbuf.texture)->stencil;

   /* First clear depth or combined depth/stencil. */
   if ((has_depth && (buffers & PIPE_CLEAR_DEPTH)) ||
       (!has_separate_stencil && (buffers & PIPE_CLEAR_STENCIL))) {
      enum pipe_format pfmt = pfb->zsbuf.format;
      uint32_t clear_value;
      uint32_t mask = 0;

      if (has_separate_stencil) {
         pfmt = util_format_get_depth_only(pfb->zsbuf.format);
         clear_value = util_pack_z(pfmt, subpass->clear_depth);
      } else {
         pfmt = pfb->zsbuf.format;
         clear_value =
            util_pack_z_stencil(pfmt, subpass->clear_depth, subpass->clear_stencil);
      }

      if (buffers & PIPE_CLEAR_DEPTH)
         mask |= 0x1;

      if (!has_separate_stencil && (buffers & PIPE_CLEAR_STENCIL))
         mask |= 0x2;

      with_crb (cs, 6) {
         crb.add(A6XX_RB_RESOLVE_SYSTEM_BUFFER_INFO(
            .tile_mode = TILE6_LINEAR,
            .samples = samples,
            .color_format = fd6_color_format(pfmt, TILE6_LINEAR),
         ));

         crb.add(A6XX_RB_RESOLVE_OPERATION(
            .type = BLIT_EVENT_CLEAR,
            .depth = true,
            .clear_mask = mask,
         ));

         crb.add(A6XX_RB_RESOLVE_GMEM_BUFFER_BASE(gmem->zsbuf_base[0]));
         crb.add(A6XX_RB_RESOLVE_CNTL_0());

         crb.add(A6XX_RB_RESOLVE_CLEAR_COLOR_DW0(clear_value));

         if (CHIP >= A7XX)
            crb.add(RB_CLEAR_TARGET(CHIP, .clear_mode = CLEAR_MODE_GMEM));
      }

      fd6_event_write<CHIP>(batch->ctx, cs, FD_CCU_RESOLVE);
   }

   /* Then clear the separate stencil buffer in case of 32 bit depth
    * formats with separate stencil. */
   if (has_separate_stencil && (buffers & PIPE_CLEAR_STENCIL)) {
      with_crb (cs, 6) {
         crb.add(A6XX_RB_RESOLVE_SYSTEM_BUFFER_INFO(
            .tile_mode = TILE6_LINEAR,
            .samples = samples,
            .color_format = FMT6_8_UINT,
         ));

         crb.add(A6XX_RB_RESOLVE_OPERATION(
            .type = BLIT_EVENT_CLEAR,
            .depth = true,
            .clear_mask = 0x1,
         ));

         crb.add(A6XX_RB_RESOLVE_GMEM_BUFFER_BASE(gmem->zsbuf_base[1]));
         crb.add(A6XX_RB_RESOLVE_CNTL_0());

         crb.add(A6XX_RB_RESOLVE_CLEAR_COLOR_DW0(subpass->clear_stencil & 0xff));

         if (CHIP >= A7XX)
            crb.add(RB_CLEAR_TARGET(CHIP, .clear_mode = CLEAR_MODE_GMEM));
      }

      fd6_event_write<CHIP>(batch->ctx, cs, FD_CCU_RESOLVE);
   }
}

/*
 * transfer from system memory to gmem
 */
template <chip CHIP>
static void
emit_restore_blits(struct fd_batch *batch, fd_cs &cs)
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   if (batch->restore & FD_BUFFER_COLOR) {
      unsigned i;
      for (i = 0; i < pfb->nr_cbufs; i++) {
         if (!pfb->cbufs[i].texture)
            continue;
         if (!(batch->restore & (PIPE_CLEAR_COLOR0 << i)))
            continue;
         emit_restore_blit<CHIP>(batch, cs, gmem->cbuf_base[i], &pfb->cbufs[i],
                                 FD_BUFFER_COLOR);
      }
   }

   if (batch->restore & (FD_BUFFER_DEPTH | FD_BUFFER_STENCIL)) {
      struct fd_resource *rsc = fd_resource(pfb->zsbuf.texture);

      if (!rsc->stencil || (batch->restore & FD_BUFFER_DEPTH)) {
         emit_restore_blit<CHIP>(batch, cs, gmem->zsbuf_base[0], &pfb->zsbuf,
                                 FD_BUFFER_DEPTH);
      }
      if (rsc->stencil && (batch->restore & FD_BUFFER_STENCIL)) {
         emit_restore_blit<CHIP>(batch, cs, gmem->zsbuf_base[1], &pfb->zsbuf,
                                 FD_BUFFER_STENCIL);
      }
   }
}

template <chip CHIP>
static void
prepare_tile_setup(struct fd_batch *batch)
{
   if (batch->restore) {
      batch->tile_loads =
         fd_submit_new_ringbuffer(batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);

      fd_cs cs(batch->tile_loads);

      set_blit_scissor(batch, cs);
      emit_restore_blits<CHIP>(batch, cs);
   }

   foreach_subpass (subpass, batch) {
      if (!subpass->fast_cleared)
         continue;

      subpass->subpass_clears =
         fd_submit_new_ringbuffer(batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);

      fd_cs cs(subpass->subpass_clears);

      set_blit_scissor(batch, cs);
      emit_subpass_clears<CHIP>(batch, cs, subpass);
   }
}

/*
 * transfer from system memory to gmem
 */
static void
fd6_emit_tile_mem2gmem(struct fd_batch *batch, const struct fd_tile *tile)
{
}

/* before IB to rendering cmds: */
template <chip CHIP>
static void
fd6_emit_tile_renderprep(struct fd_batch *batch, const struct fd_tile *tile)
{
   if (batch->tile_loads) {
      fd_cs cs(batch->gmem);
      trace_start_tile_loads(&batch->trace, cs, batch->restore);
      emit_conditional_ib<CHIP>(cs, batch, tile, batch->tile_loads);
      trace_end_tile_loads(&batch->trace, cs);
   }
}

static bool
blit_can_resolve(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   /* blit event can only do resolve for simple cases:
    * averaging samples as unsigned integers or choosing only one sample
    */
   if (util_format_is_snorm(format) || util_format_is_srgb(format))
      return false;

   /* can't do formats with larger channel sizes
    * note: this includes all float formats
    * note2: single channel integer formats seem OK
    */
   if (desc->channel[0].size > 10)
      return false;

   switch (format) {
   /* for unknown reasons blit event can't msaa resolve these formats when tiled
    * likely related to these formats having different layout from other cpp=2
    * formats
    */
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R8G8_SINT:
   case PIPE_FORMAT_R8G8_SRGB:
   /* TODO: this one should be able to work? */
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return false;
   default:
      break;
   }

   return true;
}

static bool
needs_resolve(struct pipe_surface *psurf)
{
   return psurf->nr_samples &&
          (psurf->nr_samples != psurf->texture->nr_samples);
}

/**
 * Returns the UNKNOWN_8C01 value for handling partial depth/stencil
 * clear/stores to Z24S8.
 */
static uint32_t
fd6_unknown_8c01(enum pipe_format format, unsigned buffers)
{
   buffers &= FD_BUFFER_DEPTH | FD_BUFFER_STENCIL;
   if (format == PIPE_FORMAT_Z24_UNORM_S8_UINT) {
      if (buffers == FD_BUFFER_DEPTH)
         return 0x08000041;
      else if (buffers == FD_BUFFER_STENCIL)
         return 0x00084001;
   }
   return 0;
}

template <chip CHIP>
static void
emit_resolve_blit(struct fd_batch *batch, fd_cs &cs,
                  uint32_t base, struct pipe_surface *psurf,
                  unsigned buffer) assert_dt
{
   uint32_t info = 0;
   bool stencil = false;

   if (!fd_resource(psurf->texture)->valid)
      return;

   /* if we need to resolve, but cannot with BLIT event, we instead need
    * to generate per-tile CP_BLIT (r2d) commands:
    *
    * The separate-stencil is a special case, we might need to use CP_BLIT
    * for depth, but we can still resolve stencil with a BLIT event
    */
   if (needs_resolve(psurf) && !blit_can_resolve(psurf->format) &&
       (buffer != FD_BUFFER_STENCIL)) {
      /* We could potentially use fd6_unknown_8c01() to handle partial z/s
       * resolve to packed z/s, but we would need a corresponding ability in the
       * !resolve case below, so batch_draw_tracking_for_dirty_bits() has us
       * just do a restore of the other channel for partial packed z/s writes.
       */
      fd6_resolve_tile<CHIP>(batch, cs, base, psurf, 0);
      return;
   }

   switch (buffer) {
   case FD_BUFFER_COLOR:
      info = A6XX_RB_RESOLVE_OPERATION_TYPE(BLIT_EVENT_STORE);
      break;
   case FD_BUFFER_STENCIL:
      info = A6XX_RB_RESOLVE_OPERATION_TYPE(BLIT_EVENT_STORE_AND_CLEAR);
      stencil = true;
      break;
   case FD_BUFFER_DEPTH:
      info = A6XX_RB_RESOLVE_OPERATION_TYPE(BLIT_EVENT_STORE) | A6XX_RB_RESOLVE_OPERATION_DEPTH;
      break;
   }

   if (util_format_is_pure_integer(psurf->format) ||
       util_format_is_depth_or_stencil(psurf->format))
      info |= A6XX_RB_RESOLVE_OPERATION_SAMPLE_0;

   with_crb (cs, 11) {
      crb.add(A6XX_RB_RESOLVE_OPERATION(.dword = info));
      emit_blit<CHIP>(batch, crb, base, psurf, stencil);
   }

   fd6_event_write<CHIP>(batch->ctx, cs, FD_CCU_RESOLVE);
}

/*
 * transfer from gmem to system memory (ie. normal RAM)
 */

template <chip CHIP>
static void
prepare_tile_fini(struct fd_batch *batch)
   assert_dt
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   batch->tile_store =
      fd_submit_new_ringbuffer(batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);

   fd_cs cs(batch->tile_store);

   set_blit_scissor(batch, cs);

   if (batch->resolve & (FD_BUFFER_DEPTH | FD_BUFFER_STENCIL)) {
      struct fd_resource *rsc = fd_resource(pfb->zsbuf.texture);

      if (!rsc->stencil || (batch->resolve & FD_BUFFER_DEPTH)) {
         emit_resolve_blit<CHIP>(batch, cs, gmem->zsbuf_base[0],
                                 &pfb->zsbuf, FD_BUFFER_DEPTH);
      }
      if (rsc->stencil && (batch->resolve & FD_BUFFER_STENCIL)) {
         emit_resolve_blit<CHIP>(batch, cs, gmem->zsbuf_base[1],
                                 &pfb->zsbuf, FD_BUFFER_STENCIL);
      }
   }

   if (batch->resolve & FD_BUFFER_COLOR) {
      unsigned i;
      for (i = 0; i < pfb->nr_cbufs; i++) {
         if (!pfb->cbufs[i].texture)
            continue;
         if (!(batch->resolve & (PIPE_CLEAR_COLOR0 << i)))
            continue;
         emit_resolve_blit<CHIP>(batch, cs, gmem->cbuf_base[i],
                                 &pfb->cbufs[i], FD_BUFFER_COLOR);
      }
   }
}

template <chip CHIP>
static void
fd6_emit_tile(struct fd_batch *batch, const struct fd_tile *tile)
{
   fd_cs cs(batch->gmem);

   foreach_subpass (subpass, batch) {
      if (subpass->subpass_clears) {
         trace_start_clears(&batch->trace, cs, subpass->fast_cleared);
         emit_conditional_ib<CHIP>(cs, batch, tile, subpass->subpass_clears);
         trace_end_clears(&batch->trace, cs);
      }

      emit_lrz<CHIP>(cs, batch, subpass);

      fd6_emit_ib<CHIP>(cs, subpass->draw);
   }

   if (batch->tile_epilogue)
      fd6_emit_ib<CHIP>(cs, batch->tile_epilogue);
}

template <chip CHIP>
static void
fd6_emit_tile_gmem2mem(struct fd_batch *batch, const struct fd_tile *tile)
{
   fd_cs cs(batch->gmem);

   if (batch->epilogue)
      fd6_emit_ib<CHIP>(cs, batch->epilogue);

   if (use_hw_binning(batch)) {
      fd6_set_render_mode<CHIP>(cs, {.mode = RM6_BIN_END_OF_DRAWS, .uses_gmem = true});
   }

   fd_pkt7(cs, CP_SET_DRAW_STATE, 3)
      .add(CP_SET_DRAW_STATE__0(0, .disable_all_groups = true))
      .add(CP_SET_DRAW_STATE__ADDR(0));

   fd_pkt7(cs, CP_SKIP_IB2_ENABLE_LOCAL, 1)
      .add(0x0);

   fd6_set_render_mode<CHIP>(cs, {.mode = RM6_BIN_RESOLVE, .uses_gmem = true});

   if (batch->tile_store) {
      trace_start_tile_stores(&batch->trace, cs, batch->resolve);
      emit_conditional_ib<CHIP>(cs, batch, tile, batch->tile_store);
      trace_end_tile_stores(&batch->trace, cs);
   }

   fd6_set_render_mode<CHIP>(cs, {.mode = RM6_BIN_RENDER_END});
}

template <chip CHIP>
static void
fd6_emit_tile_fini(struct fd_batch *batch)
{
   fd_cs cs(batch->gmem);

   emit_common_fini<CHIP>(cs, batch);

   fd_pkt4(cs, 1)
      .add(GRAS_LRZ_CNTL(CHIP, .enable = true));

   fd6_event_write<CHIP>(batch->ctx, cs, FD_LRZ_FLUSH);
   fd6_event_write<CHIP>(batch->ctx, cs, FD_CCU_CLEAN_BLIT_CACHE);

   if (use_hw_binning(batch)) {
      check_vsc_overflow(batch->ctx);
   }
}

template <chip CHIP>
static void
emit_sysmem_clears(fd_cs &cs, struct fd_batch *batch, struct fd_batch_subpass *subpass)
   assert_dt
{
   struct fd_context *ctx = batch->ctx;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   uint32_t buffers = subpass->fast_cleared;

   assert(buffers);

   struct pipe_box box2d;
   u_box_2d(0, 0, pfb->width, pfb->height, &box2d);

   trace_start_clears(&batch->trace, cs, buffers);

   fd6_set_render_mode<CHIP>(cs, {RM6_BLIT2DSCALE});

   if (buffers & PIPE_CLEAR_COLOR) {
      for (int i = 0; i < pfb->nr_cbufs; i++) {
         union pipe_color_union color = subpass->clear_color[i];

         if (!pfb->cbufs[i].texture)
            continue;

         if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
            continue;

         fd6_clear_surface<CHIP>(ctx, cs, &pfb->cbufs[i], &box2d, &color, 0);
      }
   }

   if (buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL)) {
      union pipe_color_union value = {};

      const bool has_depth = !!pfb->zsbuf.texture;
      struct pipe_resource *separate_stencil =
         has_depth && fd_resource(pfb->zsbuf.texture)->stencil
            ? &fd_resource(pfb->zsbuf.texture)->stencil->b.b
            : NULL;

      if ((buffers & PIPE_CLEAR_DEPTH) || (!separate_stencil && (buffers & PIPE_CLEAR_STENCIL))) {
         value.f[0] = subpass->clear_depth;
         value.ui[1] = subpass->clear_stencil;
         fd6_clear_surface<CHIP>(ctx, cs, &pfb->zsbuf, &box2d,
                                 &value, fd6_unknown_8c01(pfb->zsbuf.format, buffers));
      }

      if (separate_stencil && (buffers & PIPE_CLEAR_STENCIL)) {
         value.ui[0] = subpass->clear_stencil;

         struct pipe_surface stencil_surf = pfb->zsbuf;
         stencil_surf.format = PIPE_FORMAT_S8_UINT;
         stencil_surf.texture = separate_stencil;

         fd6_clear_surface<CHIP>(ctx, cs, &stencil_surf, &box2d, &value, 0);
      }
   }

   fd6_emit_flushes<CHIP>(ctx, cs, FD6_FLUSH_CCU_COLOR | FD6_INVALIDATE_CCU_COLOR);

   trace_end_clears(&batch->trace, cs);
}

template <chip CHIP>
static void
fd6_emit_sysmem_prep(struct fd_batch *batch) assert_dt
{
   fd_cs cs(batch->gmem);

   emit_lrz_clears<CHIP>(batch);

   fd6_emit_restore<CHIP>(cs, batch);
   emit_preamble(batch->ctx, cs, true);
   fd6_event_write<CHIP>(batch->ctx, cs, FD_LRZ_FLUSH);

   if (batch->prologue) {
      if (!batch->nondraw) {
         trace_start_prologue(&batch->trace, cs);
      }
      fd6_emit_ib<CHIP>(cs, batch->prologue);
      if (!batch->nondraw) {
         trace_end_prologue(&batch->trace, cs);
      }
   }

   /* remaining setup below here does not apply to blit/compute: */
   if (batch->nondraw)
      return;

   fd6_set_render_mode<CHIP>(cs, {RM6_DIRECT_RENDER});

   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   if (pfb->width > 0 && pfb->height > 0)
      set_scissor<CHIP>(cs, 0, 0, pfb->width - 1, pfb->height - 1);
   else
      set_scissor<CHIP>(cs, 0, 0, 0, 0);
   set_blit_scissor(batch, cs);

   set_tessfactor_bo<CHIP>(cs, batch);

   if (CHIP >= A7XX) {
      /* Non-context regs: */
      fd_pkt4(cs, 1)
         .add(GRAS_MODE_CNTL(CHIP, 0x2));
   }

   with_crb (cs, 25) {
      set_window_offset<CHIP>(crb, 0, 0);

      set_bin_size<CHIP>(crb, NULL, {
            .render_mode = RENDERING_PASS,
            .buffers_location = BUFFERS_IN_SYSMEM,
            .lrz_feedback_zmode_mask = batch->ctx->screen->info->props.has_lrz_feedback
                                          ? LRZ_FEEDBACK_EARLY_Z_LATE_Z
                                          : LRZ_FEEDBACK_NONE,
      });

      if (CHIP >= A7XX) {
         crb.add(RB_BUFFER_CNTL(CHIP,
            .z_sysmem = true,
            .s_sysmem = true,
            .rt0_sysmem = true,
            .rt1_sysmem = true,
            .rt2_sysmem = true,
            .rt3_sysmem = true,
            .rt4_sysmem = true,
            .rt5_sysmem = true,
            .rt6_sysmem = true,
            .rt7_sysmem = true,
         ));
         crb.add(GRAS_LRZ_CB_CNTL(CHIP, 0x0));
      }

      /* enable stream-out, with sysmem there is only one pass: */
      crb.add(VPC_SO_OVERRIDE(CHIP, false));
   }

   fd6_set_render_mode<CHIP>(cs, {RM6_DIRECT_RENDER});

   fd_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1)
      .add(0x0);

   /* blob controls "local" in IB2, but I think that is not required */
   fd_pkt7(cs, CP_SKIP_IB2_ENABLE_LOCAL, 1)
      .add(0x1);

   fd_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1)
      .add(0x1);

   with_crb (cs, 150) {
      emit_zs<CHIP>(crb, &pfb->zsbuf, NULL);
      emit_mrt<CHIP>(crb, pfb, NULL);
      emit_msaa<CHIP>(crb, pfb->samples);
   }

   emit_common_init<CHIP>(cs, batch);

   patch_fb_read_sysmem<CHIP>(batch);
}

template <chip CHIP>
static void
fd6_emit_sysmem(struct fd_batch *batch)
   assert_dt
{
   struct fd_screen *screen = batch->ctx->screen;
   fd_cs cs(batch->gmem);

   fd6_emit_gmem_cache_cntl<CHIP>(cs, screen, false);

   if (CHIP >= A7XX) {
      fd7_emit_static_binning_regs<CHIP>(cs, false);
   }

   foreach_subpass (subpass, batch) {
      if (subpass->fast_cleared) {
         unsigned flushes = FD6_INVALIDATE_CCHE | FD6_WAIT_FOR_IDLE;
         if (subpass->fast_cleared & FD_BUFFER_COLOR)
            flushes |= FD6_INVALIDATE_CCU_COLOR;
         if (subpass->fast_cleared & (FD_BUFFER_DEPTH | FD_BUFFER_STENCIL))
            flushes |= FD6_INVALIDATE_CCU_DEPTH;

         fd6_emit_flushes<CHIP>(batch->ctx, cs, flushes);
         emit_sysmem_clears<CHIP>(cs, batch, subpass);
         fd6_emit_flushes<CHIP>(batch->ctx, cs, flushes);
         fd6_set_render_mode<CHIP>(cs, {RM6_DIRECT_RENDER});
      }

      struct pipe_framebuffer_state *pfb = &batch->framebuffer;
      update_render_cntl<CHIP>(cs, screen, pfb, false);

      emit_lrz<CHIP>(cs, batch, subpass);

      fd6_emit_ib<CHIP>(cs, subpass->draw);
   }
}

template <chip CHIP>
static void
fd6_emit_sysmem_fini(struct fd_batch *batch) assert_dt
{
   fd_cs cs(batch->gmem);

   emit_common_fini<CHIP>(cs, batch);

   if (batch->tile_epilogue)
      fd6_emit_ib<CHIP>(cs, batch->tile_epilogue);

   if (batch->epilogue)
      fd6_emit_ib<CHIP>(cs, batch->epilogue);

   fd_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1)
      .add(0x0);

   fd6_event_write<CHIP>(batch->ctx, cs, FD_LRZ_FLUSH);

   fd6_emit_flushes<CHIP>(batch->ctx, cs,
                          FD6_FLUSH_CCU_COLOR |
                          FD6_INVALIDATE_CCU_COLOR |
                          FD6_FLUSH_CCU_DEPTH |
                          FD6_INVALIDATE_CCU_DEPTH |
                          FD6_INVALIDATE_CCHE |
                          FD6_WAIT_FOR_IDLE);
}

template <chip CHIP>
void
fd6_gmem_init(struct pipe_context *pctx)
   disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->emit_tile_init = fd6_emit_tile_init<CHIP>;
   ctx->emit_tile_prep = fd6_emit_tile_prep<CHIP>;
   ctx->emit_tile_mem2gmem = fd6_emit_tile_mem2gmem;
   ctx->emit_tile_renderprep = fd6_emit_tile_renderprep<CHIP>;
   ctx->emit_tile = fd6_emit_tile<CHIP>;
   ctx->emit_tile_gmem2mem = fd6_emit_tile_gmem2mem<CHIP>;
   ctx->emit_tile_fini = fd6_emit_tile_fini<CHIP>;
   ctx->emit_sysmem_prep = fd6_emit_sysmem_prep<CHIP>;
   ctx->emit_sysmem = fd6_emit_sysmem<CHIP>;
   ctx->emit_sysmem_fini = fd6_emit_sysmem_fini<CHIP>;
}
FD_GENX(fd6_gmem_init);
