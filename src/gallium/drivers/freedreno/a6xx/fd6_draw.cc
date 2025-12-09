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
#include "util/u_memory.h"
#include "util/u_prim.h"
#include "util/u_string.h"

#include "freedreno_blitter.h"
#include "freedreno_resource.h"
#include "freedreno_state.h"

#include "fd6_barrier.h"
#include "fd6_blend.h"
#include "fd6_blitter.h"
#include "fd6_context.h"
#include "fd6_draw.h"
#include "fd6_emit.h"
#include "fd6_program.h"
#include "fd6_screen.h"
#include "fd6_vsc.h"
#include "fd6_zsa.h"

#include "fd6_pack.h"

enum draw_type {
   DRAW_DIRECT_OP_NORMAL,
   DRAW_DIRECT_OP_INDEXED,
   DRAW_INDIRECT_OP_XFB,
   DRAW_INDIRECT_OP_INDIRECT_COUNT_INDEXED,
   DRAW_INDIRECT_OP_INDIRECT_COUNT,
   DRAW_INDIRECT_OP_INDEXED,
   DRAW_INDIRECT_OP_NORMAL,
};

static inline bool
is_indirect(enum draw_type type)
{
   return type >= DRAW_INDIRECT_OP_XFB;
}

static inline bool
is_indexed(enum draw_type type)
{
   switch (type) {
   case DRAW_DIRECT_OP_INDEXED:
   case DRAW_INDIRECT_OP_INDIRECT_COUNT_INDEXED:
   case DRAW_INDIRECT_OP_INDEXED:
      return true;
   default:
      return false;
   }
}

static void
draw_emit_xfb(fd_cs &cs, struct CP_DRAW_INDX_OFFSET_0 *draw0,
              const struct pipe_draw_info *info,
              const struct pipe_draw_indirect_info *indirect)
{
   struct fd_stream_output_target *target =
      fd_stream_output_target(indirect->count_from_stream_output);
   struct fd_resource *offset = fd_resource(target->offset_buf);

   fd_pkt7(cs, CP_DRAW_AUTO, 6)
      .add(pack_CP_DRAW_INDX_OFFSET_0(*draw0))
      .add(CP_DRAW_AUTO_1(info->instance_count))
      .add(CP_DRAW_AUTO_NUM_VERTICES_BASE(offset->bo, 0))
      /* byte counter offset subtraced from the value read from above: */
      .add(CP_DRAW_AUTO_4(0))
      .add(CP_DRAW_AUTO_5(target->stride));
}

static inline unsigned
max_indices(const struct pipe_draw_info *info, unsigned index_offset)
{
   struct pipe_resource *idx = info->index.resource;

   assert((info->index_size == 1) ||
          (info->index_size == 2) ||
          (info->index_size == 4));

   /* Conceptually we divide by the index_size.  But if we had
    * log2(index_size) we could convert that into a right-shift
    * instead.  Conveniently the index_size will only be 1, 2,
    * or 4.  And dividing by two (right-shift by one) gives us
    * the same answer for those three values.  So instead of
    * divide we can do two right-shifts.
    */
   unsigned index_size_shift = info->index_size >> 1;
   return (idx->width0 - index_offset) >> index_size_shift;
}

template <draw_type DRAW>
static void
draw_emit_indirect(fd_cs &cs, struct CP_DRAW_INDX_OFFSET_0 *draw0,
                   const struct pipe_draw_info *info,
                   const struct pipe_draw_indirect_info *indirect,
                   unsigned index_offset, uint32_t driver_param)
{
   struct fd_resource *ind = fd_resource(indirect->buffer);

   if (DRAW == DRAW_INDIRECT_OP_INDIRECT_COUNT_INDEXED) {
      struct fd_resource *count_buf = fd_resource(indirect->indirect_draw_count);
      struct pipe_resource *idx = info->index.resource;

      fd_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 11)
         .add(pack_CP_DRAW_INDX_OFFSET_0(*draw0))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_1(
            .opcode = INDIRECT_OP_INDIRECT_COUNT_INDEXED,
            .dst_off = driver_param,
         ))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_DRAW_COUNT(indirect->draw_count))
         .add(INDIRECT_OP_INDIRECT_COUNT_INDEXED_CP_DRAW_INDIRECT_MULTI_INDEX(
            fd_resource(idx)->bo, index_offset
         ))
         .add(INDIRECT_OP_INDIRECT_COUNT_INDEXED_CP_DRAW_INDIRECT_MULTI_MAX_INDICES(
            max_indices(info, index_offset)
         ))
         .add(INDIRECT_OP_INDIRECT_COUNT_INDEXED_CP_DRAW_INDIRECT_MULTI_INDIRECT(
            ind->bo, indirect->offset
         ))
         .add(INDIRECT_OP_INDIRECT_COUNT_INDEXED_CP_DRAW_INDIRECT_MULTI_INDIRECT_COUNT(
            count_buf->bo, indirect->indirect_draw_count_offset
         ))
         .add(INDIRECT_OP_INDIRECT_COUNT_INDEXED_CP_DRAW_INDIRECT_MULTI_STRIDE(
            indirect->stride
         ));
   } else if (DRAW == DRAW_INDIRECT_OP_INDEXED) {
      struct pipe_resource *idx = info->index.resource;

      fd_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 9)
         .add(pack_CP_DRAW_INDX_OFFSET_0(*draw0))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_1(
            .opcode = INDIRECT_OP_INDEXED,
            .dst_off = driver_param,
         ))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_DRAW_COUNT(indirect->draw_count))
         //index va
         .add(INDIRECT_OP_INDEXED_CP_DRAW_INDIRECT_MULTI_INDEX(
            fd_resource(idx)->bo, index_offset
         ))
         //max indices
         .add(INDIRECT_OP_INDEXED_CP_DRAW_INDIRECT_MULTI_MAX_INDICES(
            max_indices(info, index_offset)
         ))
         .add(INDIRECT_OP_INDEXED_CP_DRAW_INDIRECT_MULTI_INDIRECT(
            ind->bo, indirect->offset
         ))
         .add(INDIRECT_OP_INDEXED_CP_DRAW_INDIRECT_MULTI_STRIDE(
            indirect->stride
         ));
   }  else if(DRAW == DRAW_INDIRECT_OP_INDIRECT_COUNT) {
      struct fd_resource *count_buf = fd_resource(indirect->indirect_draw_count);

      fd_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 8)
         .add(pack_CP_DRAW_INDX_OFFSET_0(*draw0))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_1(
            .opcode = INDIRECT_OP_INDIRECT_COUNT,
            .dst_off = driver_param,
         ))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_DRAW_COUNT(indirect->draw_count))
         .add(INDIRECT_OP_INDIRECT_COUNT_CP_DRAW_INDIRECT_MULTI_INDIRECT(
            ind->bo, indirect->offset
         ))
         .add(INDIRECT_OP_INDIRECT_COUNT_CP_DRAW_INDIRECT_MULTI_INDIRECT_COUNT(
            count_buf->bo, indirect->indirect_draw_count_offset
         ))
         .add(INDIRECT_OP_INDIRECT_COUNT_CP_DRAW_INDIRECT_MULTI_STRIDE(
            indirect->stride
         ));
   } else if (DRAW == DRAW_INDIRECT_OP_NORMAL) {
      fd_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 6)
         .add(pack_CP_DRAW_INDX_OFFSET_0(*draw0))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_1(
            .opcode = INDIRECT_OP_NORMAL,
            .dst_off = driver_param,
         ))
         .add(A6XX_CP_DRAW_INDIRECT_MULTI_DRAW_COUNT(indirect->draw_count))
         .add(INDIRECT_OP_NORMAL_CP_DRAW_INDIRECT_MULTI_INDIRECT(
            ind->bo, indirect->offset
         ))
         .add(INDIRECT_OP_NORMAL_CP_DRAW_INDIRECT_MULTI_STRIDE(
            indirect->stride
         ));
   }
}

template <draw_type DRAW>
static void
draw_emit(fd_cs &cs, struct CP_DRAW_INDX_OFFSET_0 *draw0,
          const struct pipe_draw_info *info,
          const struct pipe_draw_start_count_bias *draw, unsigned index_offset)
{
   if (DRAW == DRAW_DIRECT_OP_INDEXED) {
      assert(!info->has_user_indices);

      struct pipe_resource *idx_buffer = info->index.resource;

      fd_pkt7(cs, CP_DRAW_INDX_OFFSET, 7)
         .add(pack_CP_DRAW_INDX_OFFSET_0(*draw0))
         .add(CP_DRAW_INDX_OFFSET_1(.num_instances = info->instance_count))
         .add(CP_DRAW_INDX_OFFSET_2(.num_indices = draw->count))
         .add(CP_DRAW_INDX_OFFSET_3(.first_indx = draw->start))
         .add(A5XX_CP_DRAW_INDX_OFFSET_INDX_BASE(
            fd_resource(idx_buffer)->bo,
            index_offset
         ))
         .add(A5XX_CP_DRAW_INDX_OFFSET_6(.max_indices = max_indices(info, index_offset)));
   } else if (DRAW == DRAW_DIRECT_OP_NORMAL) {
      fd_pkt7(cs, CP_DRAW_INDX_OFFSET, 3)
         .add(pack_CP_DRAW_INDX_OFFSET_0(*draw0))
         .add(CP_DRAW_INDX_OFFSET_1(.num_instances = info->instance_count))
         .add(CP_DRAW_INDX_OFFSET_2(.num_indices = draw->count));
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

template <fd6_pipeline_type PIPELINE>
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
         .patch_vertices = HAS_TESS_GS ? ctx->patch_vertices : 0,
   };

   /* Some gcc versions get confused about designated order, so workaround
    * by not initializing these inline:
    */
   key.key.ucp_enables = ctx->rasterizer->clip_plane_enable;
   key.key.msaa = (ctx->framebuffer.samples > 1);
   key.key.rasterflat = ctx->rasterizer->flatshade;

   if (unlikely(ctx->screen->driconf.dual_color_blend_by_location)) {
      struct fd6_blend_stateobj *blend = fd6_blend_stateobj(ctx->blend);
      key.key.force_dual_color_blend = blend->use_dual_src_blend;
   }

   if (PIPELINE == HAS_TESS_GS) {
      if (info->mode == MESA_PRIM_PATCHES) {
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
   }

   ir3_fixup_shader_state(&ctx->base, &key.key);

   if (ctx->gen_dirty & BIT(FD6_GROUP_PROG)) {
      struct ir3_program_state *s = ir3_cache_lookup(
            ctx->shader_cache, &key, &ctx->debug);
      fd6_ctx->prog = fd6_program_state(s);
   }

   return fd6_ctx->prog;
}

template <chip CHIP>
static void
flush_streamout(struct fd_context *ctx, fd_cs &cs, struct fd6_emit *emit)
   assert_dt
{
   if (!emit->streamout_mask)
      return;

   for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
      if (emit->streamout_mask & (1 << i)) {
         enum fd_gpu_event evt = (enum fd_gpu_event)(FD_FLUSH_SO_0 + i);
         fd6_event_write<CHIP>(ctx, cs, evt);
      }
   }
}

template <chip CHIP, fd6_pipeline_type PIPELINE, draw_type DRAW>
static void
draw_vbos(struct fd_context *ctx, const struct pipe_draw_info *info,
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
   emit.primitive_restart = info->primitive_restart && is_indexed(DRAW);
   emit.state.num_groups = 0;
   emit.streamout_mask = 0;
   emit.prog = NULL;
   emit.draw_id = 0;

   if (!(ctx->prog.vs && ctx->prog.fs))
      return;

   if (PIPELINE == HAS_TESS_GS) {
      if ((info->mode == MESA_PRIM_PATCHES) || ctx->prog.gs) {
         ctx->gen_dirty |= BIT(FD6_GROUP_PRIMITIVE_PARAMS);
      }
   }

   if ((PIPELINE == NO_TESS_GS) && !is_indirect(DRAW)) {
      fd6_vsc_update_sizes(ctx->batch, info, &draws[0]);
   }

   /* If PROG state (which will mark PROG_KEY dirty) or any state that the
    * key depends on, is dirty, then we actually need to construct the shader
    * key, figure out if we need a new variant, and lookup the PROG state.
    * Otherwise we can just use the previous prog state.
    */
   if (unlikely(ctx->gen_dirty & BIT(FD6_GROUP_PROG_KEY))) {
      emit.prog = get_program_state<PIPELINE>(ctx, info);
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
   if (PIPELINE == HAS_TESS_GS) {
      emit.hs = fd6_emit_get_prog(&emit)->hs;
      emit.ds = fd6_emit_get_prog(&emit)->ds;
      emit.gs = fd6_emit_get_prog(&emit)->gs;
   }
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
      if (PIPELINE == HAS_TESS_GS) {
         ctx->stats.hs_regs += COND(emit.hs, ir3_shader_halfregs(emit.hs));
         ctx->stats.ds_regs += COND(emit.ds, ir3_shader_halfregs(emit.ds));
         ctx->stats.gs_regs += COND(emit.gs, ir3_shader_halfregs(emit.gs));
      }
      ctx->stats.fs_regs += ir3_shader_halfregs(emit.fs);
   }

   fd_cs cs(ctx->batch->draw);

   struct CP_DRAW_INDX_OFFSET_0 draw0 = {
      .prim_type = ctx->screen->primtypes[info->mode],
      .vis_cull = USE_VISIBILITY,
      .gs_enable = !!ctx->prog.gs,
   };

   if (DRAW == DRAW_INDIRECT_OP_XFB) {
      draw0.source_select = DI_SRC_SEL_AUTO_XFB;
   } else if (DRAW == DRAW_DIRECT_OP_INDEXED ||
              DRAW == DRAW_INDIRECT_OP_INDIRECT_COUNT_INDEXED ||
              DRAW == DRAW_INDIRECT_OP_INDEXED) {
      draw0.source_select = DI_SRC_SEL_DMA;
      draw0.index_size = fd4_size2indextype(info->index_size);
   } else {
      draw0.source_select = DI_SRC_SEL_AUTO_INDEX;
   }

   if ((PIPELINE == HAS_TESS_GS) && (info->mode == MESA_PRIM_PATCHES)) {
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
      uint32_t subdraw_size = MIN2(FD6_TESS<CHIP>::FACTOR_SIZE / factor_stride,
                                   FD6_TESS<CHIP>::PARAM_SIZE / (emit.hs->output_size * 4));
      /* convert from # of patches to draw count */
      subdraw_size *= ctx->patch_vertices;

      /* For gen8 tess_bo is sized for two draws, adjust subdraw size accordingly: */
      if (CHIP >= A8XX)
         subdraw_size /= 2;

      fd_pkt7(cs, CP_SET_SUBDRAW_SIZE, 1)
         .add(subdraw_size);

      ctx->batch->tessellation = true;
   }

   with_crb (cs, 3) {
      uint32_t index_start = is_indexed(DRAW) ? draws[0].index_bias : draws[0].start;
      if (ctx->last.dirty || (ctx->last.index_start != index_start)) {
         crb.add(A6XX_VFD_INDEX_OFFSET(index_start));
         ctx->last.index_start = index_start;
      }

      if (ctx->last.dirty || (ctx->last.instance_start != info->start_instance)) {
         crb.add(A6XX_VFD_INSTANCE_START_OFFSET(info->start_instance));
         ctx->last.instance_start = info->start_instance;
      }

      uint32_t restart_index =
         info->primitive_restart ? info->restart_index : 0xffffffff;
      if (ctx->last.dirty || (ctx->last.restart_index != restart_index)) {
         crb.add(PC_RESTART_INDEX(CHIP, restart_index));
         ctx->last.restart_index = restart_index;
      }
   }

   if (emit.dirty_groups)
      fd6_emit_3d_state<PIPELINE, CHIP>(cs, &emit);

   /* All known firmware versions do not wait for WFI's with CP_DRAW_AUTO.
    * Plus, for the common case where the counter buffer is written by
    * vkCmdEndTransformFeedback, we need to wait for the CP_WAIT_MEM_WRITES to
    * complete which means we need a WAIT_FOR_ME anyway.
    *
    * Also, on some firmwares CP_DRAW_INDIRECT_MULTI waits for WFIs before
    * reading the draw parameters but after reading the count, so commands
    * that use indirect draw count need a WFM anyway.
    */
   if (DRAW == DRAW_INDIRECT_OP_XFB ||
       DRAW == DRAW_INDIRECT_OP_INDIRECT_COUNT_INDEXED ||
       DRAW == DRAW_INDIRECT_OP_INDIRECT_COUNT)
      ctx->batch->barrier |= FD6_WAIT_FOR_ME;

   fd6_barrier_flush<CHIP>(cs, ctx->batch);

   if (is_indirect(DRAW)) {
      assert(num_draws == 1);  /* only >1 for direct draws */
      if (DRAW == DRAW_INDIRECT_OP_XFB) {
         draw_emit_xfb(cs, &draw0, info, indirect);
      } else {
         const struct ir3_const_state *const_state = ir3_const_state(emit.vs);
         uint32_t dst_offset_dp =
            const_state->allocs.consts[IR3_CONST_ALLOC_DRIVER_PARAMS].offset_vec4;

         /* If unused, pass 0 for DST_OFF: */
         if (!ir3_const_can_upload(&const_state->allocs,
                                   IR3_CONST_ALLOC_DRIVER_PARAMS,
                                   emit.vs->constlen))
            dst_offset_dp = 0;

         draw_emit_indirect<DRAW>(cs, &draw0, info, indirect, index_offset, dst_offset_dp);
      }
   } else {
      draw_emit<DRAW>(cs, &draw0, info, &draws[0], index_offset);

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
            flush_streamout<CHIP>(ctx, cs, &emit);

            fd6_vsc_update_sizes(ctx->batch, info, &draws[i]);

            uint32_t index_start = is_indexed(DRAW) ? draws[i].index_bias : draws[i].start;
            if (last_index_start != index_start) {
               fd_pkt4(cs, 1)
                  .add(A6XX_VFD_INDEX_OFFSET(index_start));
               last_index_start = index_start;
            }

            if (emit.dirty_groups) {
               emit.state.num_groups = 0;
               emit.draw = &draws[i];
               emit.draw_id = info->increment_draw_id ? i : 0;
               fd6_emit_3d_state<PIPELINE, CHIP>(cs, &emit);
            }

            assert(!index_offset); /* handled by util_draw_multi() */

            draw_emit<DRAW>(cs, &draw0, info, &draws[i], 0);
         }

         ctx->last.index_start = last_index_start;
      }
   }

   flush_streamout<CHIP>(ctx, cs, &emit);

   fd_context_all_clean(ctx);
}

template <chip CHIP, fd6_pipeline_type PIPELINE>
static void
fd6_draw_vbos(struct fd_context *ctx, const struct pipe_draw_info *info,
              unsigned drawid_offset,
              const struct pipe_draw_indirect_info *indirect,
              const struct pipe_draw_start_count_bias *draws,
              unsigned num_draws,
              unsigned index_offset)
   assert_dt
{
   /* Non-indirect case is where we are more likely to see a high draw rate: */
   if (likely(!indirect)) {
      if (info->index_size) {
         draw_vbos<CHIP, PIPELINE, DRAW_DIRECT_OP_INDEXED>(
               ctx, info, drawid_offset, NULL, draws, num_draws, index_offset);
      } else {
         draw_vbos<CHIP, PIPELINE, DRAW_DIRECT_OP_NORMAL>(
               ctx, info, drawid_offset, NULL, draws, num_draws, index_offset);
      }
   } else if (indirect->count_from_stream_output) {
      draw_vbos<CHIP, PIPELINE, DRAW_INDIRECT_OP_XFB>(
            ctx, info, drawid_offset, indirect, draws, num_draws, index_offset);
   } else if (indirect->indirect_draw_count && info->index_size) {
      draw_vbos<CHIP, PIPELINE, DRAW_INDIRECT_OP_INDIRECT_COUNT_INDEXED>(
            ctx, info, drawid_offset, indirect, draws, num_draws, index_offset);
   } else if (indirect->indirect_draw_count) {
      draw_vbos<CHIP, PIPELINE, DRAW_INDIRECT_OP_INDIRECT_COUNT>(
            ctx, info, drawid_offset, indirect, draws, num_draws, index_offset);
   } else if (info->index_size) {
      draw_vbos<CHIP, PIPELINE, DRAW_INDIRECT_OP_INDEXED>(
            ctx, info, drawid_offset, indirect, draws, num_draws, index_offset);
   } else {
      draw_vbos<CHIP, PIPELINE, DRAW_INDIRECT_OP_NORMAL>(
            ctx, info, drawid_offset, indirect, draws, num_draws, index_offset);
   }
}

template <chip CHIP>
static void
fd6_update_draw(struct fd_context *ctx)
{
   const uint32_t gs_tess_stages = BIT(MESA_SHADER_TESS_CTRL) |
         BIT(MESA_SHADER_TESS_EVAL) | BIT(MESA_SHADER_GEOMETRY);

   if (ctx->bound_shader_stages & gs_tess_stages) {
      ctx->draw_vbos = fd6_draw_vbos<CHIP, HAS_TESS_GS>;
   } else {
      ctx->draw_vbos = fd6_draw_vbos<CHIP, NO_TESS_GS>;
   }
}

static bool
do_lrz_clear(struct fd_context *ctx, enum fd_buffer_mask buffers)
{
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;

   if (!pfb->zsbuf.texture)
      return false;

   struct fd_resource *zsbuf = fd_resource(pfb->zsbuf.texture);

   return (buffers & FD_BUFFER_DEPTH) && zsbuf->lrz;
}

static bool
fd6_clear(struct fd_context *ctx, enum fd_buffer_mask buffers,
          const union pipe_color_union *color, double depth,
          unsigned stencil) assert_dt
{
   struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
   struct fd_batch_subpass *subpass = ctx->batch->subpass;
   unsigned color_buffers = buffers >> 2;

   if (pfb->samples > 1) {
      /* we need to do multisample clear on 3d pipe, so fallback to u_blitter.
       * But we do this ourselves so that we can still benefit from LRZ, as
       * normally zfunc==ALWAYS would invalidate LRZ.  So we want to mark the
       * LRZ state as valid *after* the fallback clear.
       */
      fd_blitter_clear(&ctx->base, (unsigned)buffers, color, depth, stencil);
   }

   /* If we are clearing after draws, split out a new subpass:
    */
   if (subpass->num_draws > 0) {
      /* If we won't be able to do any fast-clears, avoid pointlessly
       * splitting out a new subpass:
       */
      if (pfb->samples > 1 && !do_lrz_clear(ctx, buffers))
         return true;

      subpass = fd_batch_create_subpass(ctx->batch);

      /* If doing an LRZ clear, replace the existing LRZ buffer with a
       * freshly allocated one so that we have valid LRZ state for the
       * new pass.  Otherwise unconditional writes to the depth buffer
       * would cause LRZ state to be invalid.
       */
      if (do_lrz_clear(ctx, buffers)) {
         struct fd_resource *zsbuf = fd_resource(pfb->zsbuf.texture);

         fd_bo_del(subpass->lrz);
         subpass->lrz = fd_bo_new(ctx->screen->dev, fd_bo_size(zsbuf->lrz),
                                  FD_BO_NOMAP, "lrz");
         fd_bo_del(zsbuf->lrz);
         zsbuf->lrz = fd_bo_ref(subpass->lrz);
      }
   }

   if (do_lrz_clear(ctx, buffers)) {
      struct fd_resource *zsbuf = fd_resource(pfb->zsbuf.texture);

      zsbuf->lrz_valid = true;
      zsbuf->lrz_direction = FD_LRZ_UNKNOWN;
      subpass->clear_depth = depth;
      subpass->fast_cleared |= FD_BUFFER_LRZ;

      STATIC_ASSERT((FD_BUFFER_LRZ & FD_BUFFER_ALL) == 0);
   }

   /* We've already done the fallback 3d clear: */
   if (pfb->samples > 1)
      return true;

   u_foreach_bit (i, color_buffers)
      subpass->clear_color[i] = *color;
   if (buffers & FD_BUFFER_DEPTH)
      subpass->clear_depth = depth;
   if (buffers & FD_BUFFER_STENCIL)
      subpass->clear_stencil = stencil;

   subpass->fast_cleared |= buffers;

   return true;
}

template <chip CHIP>
void
fd6_draw_init(struct pipe_context *pctx)
   disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);
   ctx->clear = fd6_clear;
   ctx->update_draw = fd6_update_draw<CHIP>;
   fd6_update_draw<CHIP>(ctx);
}
FD_GENX(fd6_draw_init);
