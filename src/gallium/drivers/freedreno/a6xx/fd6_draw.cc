/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_string.h"

#include "freedreno_resource.h"
#include "freedreno_state.h"

#include "fd6_barrier.h"
#include "fd6_context.h"
#include "fd6_draw.h"
#include "fd6_emit.h"
#include "fd6_program.h"
#include "fd6_vsc.h"
#include "fd6_zsa.h"

#include "fd6_pack.h"

static void
draw_emit_xfb(struct fd_ringbuffer *ring, struct CP_DRAW_INDX_OFFSET_0 *draw0,
              const struct pipe_draw_info *info,
              const struct pipe_draw_indirect_info *indirect)
{
   struct fd_stream_output_target *target =
      fd_stream_output_target(indirect->count_from_stream_output);
   struct fd_resource *offset = fd_resource(target->offset_buf);

   /* All known firmware versions do not wait for WFI's with CP_DRAW_AUTO.
    * Plus, for the common case where the counter buffer is written by
    * vkCmdEndTransformFeedback, we need to wait for the CP_WAIT_MEM_WRITES to
    * complete which means we need a WAIT_FOR_ME anyway.
    */
   OUT_PKT7(ring, CP_WAIT_FOR_ME, 0);

   OUT_PKT7(ring, CP_DRAW_AUTO, 6);
   OUT_RING(ring, pack_CP_DRAW_INDX_OFFSET_0(*draw0).value);
   OUT_RING(ring, info->instance_count);
   OUT_RELOC(ring, offset->bo, 0, 0, 0);
   OUT_RING(
      ring,
      0); /* byte counter offset subtraced from the value read from above */
   OUT_RING(ring, target->stride);
}

static void
draw_emit_indirect(struct fd_ringbuffer *ring,
                   struct CP_DRAW_INDX_OFFSET_0 *draw0,
                   const struct pipe_draw_info *info,
                   const struct pipe_draw_indirect_info *indirect,
                   unsigned index_offset)
{
   struct fd_resource *ind = fd_resource(indirect->buffer);

   if (info->index_size) {
      struct pipe_resource *idx = info->index.resource;
      unsigned max_indices = (idx->width0 - index_offset) / info->index_size;

      OUT_PKT(ring, CP_DRAW_INDX_INDIRECT, pack_CP_DRAW_INDX_OFFSET_0(*draw0),
              A5XX_CP_DRAW_INDX_INDIRECT_INDX_BASE(fd_resource(idx)->bo,
                                                   index_offset),
              A5XX_CP_DRAW_INDX_INDIRECT_3(.max_indices = max_indices),
              A5XX_CP_DRAW_INDX_INDIRECT_INDIRECT(ind->bo, indirect->offset));
   } else {
      OUT_PKT(ring, CP_DRAW_INDIRECT, pack_CP_DRAW_INDX_OFFSET_0(*draw0),
              A5XX_CP_DRAW_INDIRECT_INDIRECT(ind->bo, indirect->offset));
   }
}

static void
draw_emit(struct fd_ringbuffer *ring, struct CP_DRAW_INDX_OFFSET_0 *draw0,
          const struct pipe_draw_info *info,
          const struct pipe_draw_start_count_bias *draw, unsigned index_offset)
{
   if (info->index_size) {
      assert(!info->has_user_indices);

      struct pipe_resource *idx_buffer = info->index.resource;
      unsigned max_indices =
         (idx_buffer->width0 - index_offset) / info->index_size;

      OUT_PKT(ring, CP_DRAW_INDX_OFFSET, pack_CP_DRAW_INDX_OFFSET_0(*draw0),
              CP_DRAW_INDX_OFFSET_1(.num_instances = info->instance_count),
              CP_DRAW_INDX_OFFSET_2(.num_indices = draw->count),
              CP_DRAW_INDX_OFFSET_3(.first_indx = draw->start),
              A5XX_CP_DRAW_INDX_OFFSET_INDX_BASE(fd_resource(idx_buffer)->bo,
                                                 index_offset),
              A5XX_CP_DRAW_INDX_OFFSET_6(.max_indices = max_indices));
   } else {
      OUT_PKT(ring, CP_DRAW_INDX_OFFSET, pack_CP_DRAW_INDX_OFFSET_0(*draw0),
              CP_DRAW_INDX_OFFSET_1(.num_instances = info->instance_count),
              CP_DRAW_INDX_OFFSET_2(.num_indices = draw->count));
   }
}

static void
fixup_draw_state(struct fd_context *ctx, struct fd6_emit *emit) assert_dt
{
   if (ctx->last.dirty ||
       (ctx->last.primitive_restart != emit->primitive_restart)) {
      /* rasterizer state is effected by primitive-restart: */
      fd_context_dirty(ctx, FD_DIRTY_RASTERIZER);
      ctx->last.primitive_restart = emit->primitive_restart;
   }
}

static const struct fd6_program_state *
get_program_state(struct fd_context *ctx, const struct pipe_draw_info *info)
   assert_dt
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct ir3_cache_key key = {
         .vs = (struct ir3_shader_state *)ctx->prog.vs,
         .gs = (struct ir3_shader_state *)ctx->prog.gs,
         .fs = (struct ir3_shader_state *)ctx->prog.fs,
         .clip_plane_enable = ctx->rasterizer->clip_plane_enable,
         .patch_vertices = ctx->patch_vertices,
   };

   /* Some gcc versions get confused about designated order, so workaround
    * by not initializing these inline:
    */
   key.key.ucp_enables = ctx->rasterizer->clip_plane_enable;
   key.key.sample_shading = (ctx->min_samples > 1);
   key.key.msaa = (ctx->framebuffer.samples > 1);
   key.key.rasterflat = ctx->rasterizer->flatshade;

   if (info->mode == PIPE_PRIM_PATCHES) {
      struct shader_info *gs_info =
            ir3_get_shader_info((struct ir3_shader_state *)ctx->prog.gs);

      key.hs = (struct ir3_shader_state *)ctx->prog.hs;
      key.ds = (struct ir3_shader_state *)ctx->prog.ds;

      struct shader_info *ds_info = ir3_get_shader_info(key.ds);
      key.key.tessellation = ir3_tess_mode(ds_info->tess._primitive_mode);

      struct shader_info *fs_info = ir3_get_shader_info(key.fs);
      key.key.tcs_store_primid =
         BITSET_TEST(ds_info->system_values_read, SYSTEM_VALUE_PRIMITIVE_ID) ||
         (gs_info && BITSET_TEST(gs_info->system_values_read, SYSTEM_VALUE_PRIMITIVE_ID)) ||
         (fs_info && (fs_info->inputs_read & (1ull << VARYING_SLOT_PRIMITIVE_ID)));
   }

   if (key.gs) {
      key.key.has_gs = true;
   }

   ir3_fixup_shader_state(&ctx->base, &key.key);

   if (ctx->gen_dirty & BIT(FD6_GROUP_PROG)) {
      struct ir3_program_state *s = ir3_cache_lookup(
            ctx->shader_cache, &key, &ctx->debug);
      fd6_ctx->prog = fd6_program_state(s);
   }

   return fd6_ctx->prog;
}

static void
flush_streamout(struct fd_context *ctx, struct fd6_emit *emit)
   assert_dt
{
   if (!emit->streamout_mask)
      return;

   struct fd_ringbuffer *ring = ctx->batch->draw;

   for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
      if (emit->streamout_mask & (1 << i)) {
         enum vgt_event_type evt = (enum vgt_event_type)(FLUSH_SO_0 + i);
         fd6_event_write(ctx->batch, ring, evt, false);
      }
   }
}

static void
fd6_draw_vbos(struct fd_context *ctx, const struct pipe_draw_info *info,
              unsigned drawid_offset,
              const struct pipe_draw_indirect_info *indirect,
              const struct pipe_draw_start_count_bias *draws,
              unsigned num_draws,
              unsigned index_offset)
   assert_dt
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd6_emit emit;

   emit.ctx = ctx;
   emit.info = info;
   emit.indirect = indirect;
   emit.draw = NULL;
   emit.rasterflat = ctx->rasterizer->flatshade;
   emit.sprite_coord_enable = ctx->rasterizer->sprite_coord_enable;
   emit.sprite_coord_mode = ctx->rasterizer->sprite_coord_mode;
   emit.primitive_restart = info->primitive_restart && info->index_size;
   emit.state.num_groups = 0;
   emit.streamout_mask = 0;
   emit.prog = NULL;

   if (!(ctx->prog.vs && ctx->prog.fs))
      return;

   if ((info->mode == PIPE_PRIM_PATCHES) || ctx->prog.gs) {
      ctx->gen_dirty |= BIT(FD6_GROUP_PRIMITIVE_PARAMS);
   } else if (!indirect) {
      fd6_vsc_update_sizes(ctx->batch, info, &draws[0]);
   }

   /* If PROG state (which will mark PROG_KEY dirty) or any state that the
    * key depends on, is dirty, then we actually need to construct the shader
    * key, figure out if we need a new variant, and lookup the PROG state.
    * Otherwise we can just use the previous prog state.
    */
   if (unlikely(ctx->gen_dirty & BIT(FD6_GROUP_PROG_KEY))) {
      emit.prog = get_program_state(ctx, info);
   } else {
      emit.prog = fd6_ctx->prog;
   }

   /* bail if compile failed: */
   if (!emit.prog)
      return;

   fixup_draw_state(ctx, &emit);

   /* *after* fixup_shader_state(): */
   emit.dirty_groups = ctx->gen_dirty;

   emit.vs = fd6_emit_get_prog(&emit)->vs;
   emit.hs = fd6_emit_get_prog(&emit)->hs;
   emit.ds = fd6_emit_get_prog(&emit)->ds;
   emit.gs = fd6_emit_get_prog(&emit)->gs;
   emit.fs = fd6_emit_get_prog(&emit)->fs;

   if (emit.prog->num_driver_params || fd6_ctx->has_dp_state) {
      emit.draw = &draws[0];
      emit.dirty_groups |= BIT(FD6_GROUP_DRIVER_PARAMS);
   }

   /* If we are doing xfb, we need to emit the xfb state on every draw: */
   if (emit.prog->stream_output)
      emit.dirty_groups |= BIT(FD6_GROUP_SO);

   if (unlikely(ctx->stats_users > 0)) {
      ctx->stats.vs_regs += ir3_shader_halfregs(emit.vs);
      ctx->stats.hs_regs += COND(emit.hs, ir3_shader_halfregs(emit.hs));
      ctx->stats.ds_regs += COND(emit.ds, ir3_shader_halfregs(emit.ds));
      ctx->stats.gs_regs += COND(emit.gs, ir3_shader_halfregs(emit.gs));
      ctx->stats.fs_regs += ir3_shader_halfregs(emit.fs);
   }

   struct fd_ringbuffer *ring = ctx->batch->draw;

   struct CP_DRAW_INDX_OFFSET_0 draw0 = {
      .prim_type = ctx->screen->primtypes[info->mode],
      .vis_cull = USE_VISIBILITY,
      .gs_enable = !!ctx->prog.gs,
   };

   if (indirect && indirect->count_from_stream_output) {
      draw0.source_select = DI_SRC_SEL_AUTO_XFB;
   } else if (info->index_size) {
      draw0.source_select = DI_SRC_SEL_DMA;
      draw0.index_size = fd4_size2indextype(info->index_size);
   } else {
      draw0.source_select = DI_SRC_SEL_AUTO_INDEX;
   }

   if (info->mode == PIPE_PRIM_PATCHES) {
      struct shader_info *ds_info =
            ir3_get_shader_info((struct ir3_shader_state *)ctx->prog.ds);
      unsigned tessellation = ir3_tess_mode(ds_info->tess._primitive_mode);

      uint32_t factor_stride = ir3_tess_factor_stride(tessellation);

      STATIC_ASSERT(IR3_TESS_ISOLINES == TESS_ISOLINES + 1);
      STATIC_ASSERT(IR3_TESS_TRIANGLES == TESS_TRIANGLES + 1);
      STATIC_ASSERT(IR3_TESS_QUADS == TESS_QUADS + 1);
      draw0.patch_type = (enum a6xx_patch_type)(tessellation - 1);

      draw0.prim_type = (enum pc_di_primtype)(DI_PT_PATCHES0 + ctx->patch_vertices);
      draw0.tess_enable = true;

      /* maximum number of patches that can fit in tess factor/param buffers */
      uint32_t subdraw_size = MIN2(FD6_TESS_FACTOR_SIZE / factor_stride,
                                   FD6_TESS_PARAM_SIZE / (emit.hs->output_size * 4));
      /* convert from # of patches to draw count */
      subdraw_size *= ctx->patch_vertices;

      OUT_PKT7(ring, CP_SET_SUBDRAW_SIZE, 1);
      OUT_RING(ring, subdraw_size);

      ctx->batch->tessellation = true;
   }

   uint32_t index_start = info->index_size ? draws[0].index_bias : draws[0].start;
   if (ctx->last.dirty || (ctx->last.index_start != index_start)) {
      OUT_PKT4(ring, REG_A6XX_VFD_INDEX_OFFSET, 1);
      OUT_RING(ring, index_start); /* VFD_INDEX_OFFSET */
      ctx->last.index_start = index_start;
   }

   if (ctx->last.dirty || (ctx->last.instance_start != info->start_instance)) {
      OUT_PKT4(ring, REG_A6XX_VFD_INSTANCE_START_OFFSET, 1);
      OUT_RING(ring, info->start_instance); /* VFD_INSTANCE_START_OFFSET */
      ctx->last.instance_start = info->start_instance;
   }

   uint32_t restart_index =
      info->primitive_restart ? info->restart_index : 0xffffffff;
   if (ctx->last.dirty || (ctx->last.restart_index != restart_index)) {
      OUT_PKT4(ring, REG_A6XX_PC_RESTART_INDEX, 1);
      OUT_RING(ring, restart_index); /* PC_RESTART_INDEX */
      ctx->last.restart_index = restart_index;
   }

   if (emit.dirty_groups)
      fd6_emit_3d_state(ring, &emit);

   if (ctx->batch->barrier)
      fd6_barrier_flush(ctx->batch);

   /* for debug after a lock up, write a unique counter value
    * to scratch7 for each draw, to make it easier to match up
    * register dumps to cmdstream.  The combination of IB
    * (scratch6) and DRAW is enough to "triangulate" the
    * particular draw that caused lockup.
    */
   emit_marker6(ring, 7);

   if (indirect) {
      assert(num_draws == 1);  /* only >1 for direct draws */
      if (indirect->count_from_stream_output) {
         draw_emit_xfb(ring, &draw0, info, indirect);
      } else {
         draw_emit_indirect(ring, &draw0, info, indirect, index_offset);
      }
   } else {
      draw_emit(ring, &draw0, info, &draws[0], index_offset);

      if (unlikely(num_draws > 1)) {

         /*
          * Most state won't need to be re-emitted, other than xfb and
          * driver-params:
          */
         emit.dirty_groups = 0;

         if (emit.prog->num_driver_params)
            emit.dirty_groups |= BIT(FD6_GROUP_DRIVER_PARAMS);

         if (emit.prog->stream_output)
            emit.dirty_groups |= BIT(FD6_GROUP_SO);

         uint32_t last_index_start = ctx->last.index_start;

         for (unsigned i = 1; i < num_draws; i++) {
            flush_streamout(ctx, &emit);

            fd6_vsc_update_sizes(ctx->batch, info, &draws[i]);

            uint32_t index_start = info->index_size ? draws[i].index_bias : draws[i].start;
            if (last_index_start != index_start) {
               OUT_PKT4(ring, REG_A6XX_VFD_INDEX_OFFSET, 1);
               OUT_RING(ring, index_start); /* VFD_INDEX_OFFSET */
               last_index_start = index_start;
            }

            if (emit.dirty_groups) {
               emit.state.num_groups = 0;
               emit.draw = &draws[i];
               fd6_emit_3d_state(ring, &emit);
            }

            assert(!index_offset); /* handled by util_draw_multi() */

            draw_emit(ring, &draw0, info, &draws[i], 0);
         }

         ctx->last.index_start = last_index_start;
      }
   }

   emit_marker6(ring, 7);
   fd_reset_wfi(ctx->batch);

   flush_streamout(ctx, &emit);

   fd_context_all_clean(ctx);
}

static void
fd6_clear_lrz(struct fd_batch *batch, struct fd_resource *zsbuf, double depth) assert_dt
{
   struct fd_ringbuffer *ring;
   struct fd_screen *screen = batch->ctx->screen;

   ring = fd_batch_get_prologue(batch);

   emit_marker6(ring, 7);
   OUT_PKT7(ring, CP_SET_MARKER, 1);
   OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_BYPASS));
   emit_marker6(ring, 7);

   OUT_WFI5(ring);

   fd6_emit_ccu_cntl(ring, screen, false);

   OUT_REG(ring,
           A6XX_HLSQ_INVALIDATE_CMD(.vs_state = true, .hs_state = true,
                                    .ds_state = true, .gs_state = true,
                                    .fs_state = true, .cs_state = true,
                                    .cs_ibo = true, .gfx_ibo = true,
                                    .gfx_shared_const = true,
                                    .cs_bindless = 0x1f, .gfx_bindless = 0x1f));

   emit_marker6(ring, 7);
   OUT_PKT7(ring, CP_SET_MARKER, 1);
   OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));
   emit_marker6(ring, 7);

   OUT_PKT4(ring, REG_A6XX_RB_2D_UNKNOWN_8C01, 1);
   OUT_RING(ring, 0x0);

   OUT_PKT4(ring, REG_A6XX_SP_PS_2D_SRC_INFO, 13);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);

   OUT_PKT4(ring, REG_A6XX_SP_2D_DST_FORMAT, 1);
   OUT_RING(ring, 0x0000f410);

   OUT_PKT4(ring, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
   OUT_RING(ring,
            A6XX_GRAS_2D_BLIT_CNTL_COLOR_FORMAT(FMT6_16_UNORM) | 0x4f00080);

   OUT_PKT4(ring, REG_A6XX_RB_2D_BLIT_CNTL, 1);
   OUT_RING(ring, A6XX_RB_2D_BLIT_CNTL_COLOR_FORMAT(FMT6_16_UNORM) | 0x4f00080);

   fd6_event_write(batch, ring, PC_CCU_FLUSH_COLOR_TS, true);
   fd6_event_write(batch, ring, PC_CCU_INVALIDATE_COLOR, false);
   fd_wfi(batch, ring);

   OUT_PKT4(ring, REG_A6XX_RB_2D_SRC_SOLID_C0, 4);
   OUT_RING(ring, fui(depth));
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);

   OUT_PKT4(ring, REG_A6XX_RB_2D_DST_INFO, 9);
   OUT_RING(ring, A6XX_RB_2D_DST_INFO_COLOR_FORMAT(FMT6_16_UNORM) |
                     A6XX_RB_2D_DST_INFO_TILE_MODE(TILE6_LINEAR) |
                     A6XX_RB_2D_DST_INFO_COLOR_SWAP(WZYX));
   OUT_RELOC(ring, zsbuf->lrz, 0, 0, 0);
   OUT_RING(ring, A6XX_RB_2D_DST_PITCH(zsbuf->lrz_pitch * 2).value);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);
   OUT_RING(ring, 0x00000000);

   OUT_REG(ring, A6XX_GRAS_2D_SRC_TL_X(0), A6XX_GRAS_2D_SRC_BR_X(0),
           A6XX_GRAS_2D_SRC_TL_Y(0), A6XX_GRAS_2D_SRC_BR_Y(0));

   OUT_PKT4(ring, REG_A6XX_GRAS_2D_DST_TL, 2);
   OUT_RING(ring, A6XX_GRAS_2D_DST_TL_X(0) | A6XX_GRAS_2D_DST_TL_Y(0));
   OUT_RING(ring, A6XX_GRAS_2D_DST_BR_X(zsbuf->lrz_width - 1) |
                     A6XX_GRAS_2D_DST_BR_Y(zsbuf->lrz_height - 1));

   fd6_event_write(batch, ring, (enum vgt_event_type)0x3f, false);

   if (screen->info->a6xx.magic.RB_DBG_ECO_CNTL_blit != screen->info->a6xx.magic.RB_DBG_ECO_CNTL) {
      /* This a non-context register, so we have to WFI before changing. */
      OUT_WFI5(ring);
      OUT_PKT4(ring, REG_A6XX_RB_DBG_ECO_CNTL, 1);
      OUT_RING(ring, screen->info->a6xx.magic.RB_DBG_ECO_CNTL_blit);
   }

   OUT_PKT7(ring, CP_BLIT, 1);
   OUT_RING(ring, CP_BLIT_0_OP(BLIT_OP_SCALE));

   if (screen->info->a6xx.magic.RB_DBG_ECO_CNTL_blit != screen->info->a6xx.magic.RB_DBG_ECO_CNTL) {
      OUT_WFI5(ring);
      OUT_PKT4(ring, REG_A6XX_RB_DBG_ECO_CNTL, 1);
      OUT_RING(ring, screen->info->a6xx.magic.RB_DBG_ECO_CNTL);
   }

   fd6_event_write(batch, ring, PC_CCU_FLUSH_COLOR_TS, true);
   fd6_event_write(batch, ring, PC_CCU_FLUSH_DEPTH_TS, true);
   fd6_event_write(batch, ring, CACHE_FLUSH_TS, true);
   fd_wfi(batch, ring);

   fd6_cache_inv(batch, ring);
}

static bool
is_z32(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
   case PIPE_FORMAT_Z32_UNORM:
   case PIPE_FORMAT_Z32_FLOAT:
      return true;
   default:
      return false;
   }
}

static bool
fd6_clear(struct fd_context *ctx, enum fd_buffer_mask buffers,
          const union pipe_color_union *color, double depth,
          unsigned stencil) assert_dt
{
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
   const bool has_depth = pfb->zsbuf;
   unsigned color_buffers = buffers >> 2;

   /* If we're clearing after draws, fallback to 3D pipe clears.  We could
    * use blitter clears in the draw batch but then we'd have to patch up the
    * gmem offsets. This doesn't seem like a useful thing to optimize for
    * however.*/
   if (ctx->batch->num_draws > 0)
      return false;

   if (has_depth && (buffers & FD_BUFFER_DEPTH)) {
      struct fd_resource *zsbuf = fd_resource(pfb->zsbuf->texture);
      if (zsbuf->lrz && !is_z32(pfb->zsbuf->format)) {
         zsbuf->lrz_valid = true;
         zsbuf->lrz_direction = FD_LRZ_UNKNOWN;
         fd6_clear_lrz(ctx->batch, zsbuf, depth);
      }
   }

   /* we need to do multisample clear on 3d pipe, so fallback to u_blitter: */
   if (pfb->samples > 1)
      return false;

   u_foreach_bit (i, color_buffers)
      ctx->batch->clear_color[i] = *color;
   if (buffers & FD_BUFFER_DEPTH)
      ctx->batch->clear_depth = depth;
   if (buffers & FD_BUFFER_STENCIL)
      ctx->batch->clear_stencil = stencil;

   ctx->batch->fast_cleared |= buffers;

   return true;
}

void
fd6_draw_init(struct pipe_context *pctx) disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->draw_vbos = fd6_draw_vbos;
   ctx->clear = fd6_clear;
}
