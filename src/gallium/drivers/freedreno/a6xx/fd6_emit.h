/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_EMIT_H
#define FD6_EMIT_H

#include "pipe/p_context.h"

#include "fd6_context.h"
#include "fd6_program.h"
#include "fdl/fd6_format_table.h"
#include "freedreno_context.h"
#include "freedreno_gpu_event.h"
#include "ir3_gallium.h"

struct fd_ringbuffer;

/* To collect all the state objects to emit in a single CP_SET_DRAW_STATE
 * packet, the emit tracks a collection of however many state_group's that
 * need to be emit'd.
 */
enum fd6_state_id {
   FD6_GROUP_PROG_CONFIG,
   FD6_GROUP_PROG,
   FD6_GROUP_PROG_BINNING,
   FD6_GROUP_PROG_INTERP,
   FD6_GROUP_PROG_FB_RAST,
   FD6_GROUP_LRZ,
   FD6_GROUP_VTXSTATE,
   FD6_GROUP_VBO,
   FD6_GROUP_CONST,
   FD6_GROUP_DRIVER_PARAMS,
   FD6_GROUP_PRIMITIVE_PARAMS,
   FD6_GROUP_VS_TEX,
   FD6_GROUP_HS_TEX,
   FD6_GROUP_DS_TEX,
   FD6_GROUP_GS_TEX,
   FD6_GROUP_FS_TEX,
   FD6_GROUP_RASTERIZER,
   FD6_GROUP_ZSA,
   FD6_GROUP_BLEND,
   FD6_GROUP_SCISSOR,
   FD6_GROUP_BLEND_COLOR,
   FD6_GROUP_SAMPLE_LOCATIONS,
   FD6_GROUP_SO,
   FD6_GROUP_VS_BINDLESS,
   FD6_GROUP_HS_BINDLESS,
   FD6_GROUP_DS_BINDLESS,
   FD6_GROUP_GS_BINDLESS,
   FD6_GROUP_FS_BINDLESS,
   FD6_GROUP_PRIM_MODE_SYSMEM,
   FD6_GROUP_PRIM_MODE_GMEM,

   /*
    * Virtual state-groups, which don't turn into a CP_SET_DRAW_STATE group
    */

   FD6_GROUP_PROG_KEY,  /* Set for any state which could change shader key */
   FD6_GROUP_NON_GROUP, /* placeholder group for state emit in IB2, keep last */

   /*
    * Note that since we don't interleave draws and grids in the same batch,
    * the compute vs draw state groups can overlap:
    */
   FD6_GROUP_CS_TEX = FD6_GROUP_VS_TEX,
   FD6_GROUP_CS_BINDLESS = FD6_GROUP_VS_BINDLESS,
};

/**
 * Pipeline type, Ie. is just plain old VS+FS (which can be high draw rate and
 * should be a fast-path) or is it a pipeline that uses GS and/or tess to
 * amplify geometry.
 *
 * TODO split GS and TESS?
 */
enum fd6_pipeline_type {
   NO_TESS_GS,   /* Only has VS+FS */
   HAS_TESS_GS,  /* Has tess and/or GS */
};

#define ENABLE_ALL                                                             \
   (CP_SET_DRAW_STATE__0_BINNING | CP_SET_DRAW_STATE__0_GMEM |                 \
    CP_SET_DRAW_STATE__0_SYSMEM)
#define ENABLE_DRAW (CP_SET_DRAW_STATE__0_GMEM | CP_SET_DRAW_STATE__0_SYSMEM)

struct fd6_state_group {
   struct fd_ringbuffer *stateobj;
   enum fd6_state_id group_id;
   /* enable_mask controls which states the stateobj is evaluated in,
    * b0 is binning pass b1 and/or b2 is draw pass
    */
   uint32_t enable_mask;
};

struct fd6_state {
   struct fd6_state_group groups[32];
   unsigned num_groups;
};

static inline void
fd6_state_emit(struct fd6_state *state, fd_cs &cs)
{
   if (!state->num_groups)
      return;

   fd_pkt7 pkt(cs, CP_SET_DRAW_STATE, 3 * state->num_groups);

   for (unsigned i = 0; i < state->num_groups; i++) {
      struct fd6_state_group *g = &state->groups[i];

      assert((g->enable_mask & ~ENABLE_ALL) == 0);

      if (g->stateobj) {
         unsigned n = fd_ringbuffer_size(g->stateobj) / 4;

         pkt.add(CP_SET_DRAW_STATE__0(i,
            .count = n,
            .group_id = g->group_id,
            .dword = g->enable_mask,
         ));
         pkt.add(g->stateobj, 0, NULL);
      } else {
         pkt.add(CP_SET_DRAW_STATE__0(i,
            .disable = true,
            .group_id = g->group_id,
            .dword = g->enable_mask,
         ));
         pkt.add(CP_SET_DRAW_STATE__ADDR(i));
      }

      if (g->stateobj)
         fd_ringbuffer_del(g->stateobj);
   }
}

static inline unsigned
enable_mask(enum fd6_state_id group_id)
{
   switch (group_id) {
   case FD6_GROUP_PROG: return ENABLE_DRAW;
   case FD6_GROUP_PROG_BINNING: return CP_SET_DRAW_STATE__0_BINNING;
   case FD6_GROUP_PROG_INTERP: return ENABLE_DRAW;
   case FD6_GROUP_FS_TEX: return ENABLE_DRAW;
   case FD6_GROUP_FS_BINDLESS: return ENABLE_DRAW;
   case FD6_GROUP_PRIM_MODE_SYSMEM: return CP_SET_DRAW_STATE__0_SYSMEM | CP_SET_DRAW_STATE__0_BINNING;
   case FD6_GROUP_PRIM_MODE_GMEM: return CP_SET_DRAW_STATE__0_GMEM;
   default: return ENABLE_ALL;
   }
}

static inline void
fd6_state_take_group(struct fd6_state *state, struct fd_ringbuffer *stateobj,
                     enum fd6_state_id group_id)
{
   assert(state->num_groups < ARRAY_SIZE(state->groups));
   struct fd6_state_group *g = &state->groups[state->num_groups++];
   g->stateobj = stateobj;
   g->group_id = group_id;
   g->enable_mask = enable_mask(group_id);
}

static inline void
fd6_state_add_group(struct fd6_state *state, struct fd_ringbuffer *stateobj,
                    enum fd6_state_id group_id)
{
   fd6_state_take_group(state, fd_ringbuffer_ref(stateobj), group_id);
}

/* grouped together emit-state for prog/vertex/state emit: */
struct fd6_emit {
   struct fd_context *ctx;
   const struct pipe_draw_info *info;
   const struct pipe_draw_indirect_info *indirect;
   const struct pipe_draw_start_count_bias *draw;
   uint32_t dirty_groups;

   uint32_t sprite_coord_enable; /* bitmask */
   bool sprite_coord_mode : 1;
   bool rasterflat : 1;
   bool primitive_restart : 1;
   uint8_t streamout_mask;
   uint32_t draw_id;

   /* cached to avoid repeated lookups: */
   const struct fd6_program_state *prog;

   const struct ir3_shader_variant *vs;
   const struct ir3_shader_variant *hs;
   const struct ir3_shader_variant *ds;
   const struct ir3_shader_variant *gs;
   const struct ir3_shader_variant *fs;

   struct fd6_state state;
};

static inline const struct fd6_program_state *
fd6_emit_get_prog(struct fd6_emit *emit)
{
   return emit->prog;
}

template <chip CHIP>
static inline void
__event_write(fd_cs &cs, enum fd_gpu_event event,
              enum event_write_src esrc, enum event_write_dst edst,
              uint32_t val, struct fd_bo *bo, uint32_t offset)
{
   struct fd_gpu_event_info info = fd_gpu_events<CHIP>[event];
   unsigned len = info.needs_seqno ? 4 : 1;

   if ((CHIP >= A7XX) && (event == FD_RB_DONE))
      len--;

   fd_pkt7 pkt(cs, CP_EVENT_WRITE, len);

   if (CHIP == A6XX) {
      pkt.add(CP_EVENT_WRITE_0_EVENT(info.raw_event) |
               COND(info.needs_seqno, CP_EVENT_WRITE_0_TIMESTAMP));
   } else if (CHIP >= A7XX) {
      pkt.add(CP_EVENT_WRITE7_0_EVENT(info.raw_event) |
              CP_EVENT_WRITE7_0_WRITE_SRC(esrc) |
              CP_EVENT_WRITE7_0_WRITE_DST(edst) |
              COND(info.needs_seqno, CP_EVENT_WRITE7_0_WRITE_ENABLED));
   }

   if (info.needs_seqno) {
      pkt.add(CP_EVENT_WRITE_ADDR(
         .bo = bo,
         .bo_offset = offset,
      )); /* ADDR_LO/HI */
      if (len == 4)
         pkt.add(val);
   }
}

template <chip CHIP>
static inline void
fd6_record_ts(fd_cs &cs, struct fd_bo *bo, uint32_t offset)
{
   __event_write<CHIP>(cs, FD_RB_DONE, EV_WRITE_ALWAYSON, EV_DST_RAM, 0, bo, offset);
}

template <chip CHIP>
static inline void
fd6_fence_write(fd_cs &cs, uint32_t val, struct fd_bo *bo, uint32_t offset)
{
   __event_write<CHIP>(cs, FD_CACHE_CLEAN, EV_WRITE_USER_32B, EV_DST_RAM, val, bo, offset);
}

template <chip CHIP>
static inline unsigned
fd6_event_write(struct fd_context *ctx, fd_cs &cs, enum fd_gpu_event event)
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd_gpu_event_info info = fd_gpu_events<CHIP>[event];
   unsigned seqno = 0;

   if (info.needs_seqno) {
      struct fd6_context *fd6_ctx = fd6_context(ctx);
      seqno = ++fd6_ctx->seqno;
   }

   __event_write<CHIP>(cs, event, EV_WRITE_USER_32B, EV_DST_RAM, seqno,
                       control_ptr(fd6_ctx, seqno));

   return seqno;
}

template <chip CHIP>
static inline void
fd6_cache_inv(struct fd_context *ctx, fd_cs &cs)
{
   fd6_event_write<CHIP>(ctx, cs, FD_CCU_INVALIDATE_COLOR);
   fd6_event_write<CHIP>(ctx, cs, FD_CCU_INVALIDATE_DEPTH);
   fd6_event_write<CHIP>(ctx, cs, FD_CACHE_INVALIDATE);
}

template <chip CHIP>
static inline void
fd6_lrz_inv(struct fd_context *ctx, fd_cs &cs)
{
   with_crb (cs, 3) {
      crb.add(GRAS_LRZ_CNTL(CHIP, .enable = true));
      crb.add(GRAS_LRZ_CNTL2(CHIP,
         .disable_on_wrong_dir = true,
         .fc_enable = true,
      ));
      crb.add(RB_LRZ_CNTL2(CHIP));
   }

   fd6_event_write<CHIP>(ctx, cs, FD_LRZ_FLUSH);

   with_crb (cs, 3) {
      crb.add(GRAS_LRZ_CNTL(CHIP));
      crb.add(GRAS_LRZ_CNTL2(CHIP));
      crb.add(RB_LRZ_CNTL2(CHIP));
   }
}

template <chip CHIP>
static inline void
fd6_set_rb_dbg_eco_mode(struct fd_context *ctx, fd_cs &cs, bool blit)
{
   /* Later things do not make this accessible to UMD: */
   if (CHIP >= A7XX)
      return;

   const struct fd_dev_info *info = ctx->screen->info;

   if (info->magic.RB_DBG_ECO_CNTL == info->magic.RB_DBG_ECO_CNTL_blit)
      return;

   uint32_t dword = blit ? info->magic.RB_DBG_ECO_CNTL_blit :
                           info->magic.RB_DBG_ECO_CNTL;

   fd_pkt7(cs, CP_WAIT_FOR_IDLE, 0);
   fd_pkt4(cs, 1)
      .add(A6XX_RB_DBG_ECO_CNTL(.dword = dword));
}

struct fd6_set_render_mode {
   enum a6xx_marker mode;
   bool uses_gmem;
};

template <chip CHIP>
static inline void
fd6_set_render_mode(fd_cs &cs, struct fd6_set_render_mode args)
{
   if (CHIP >= A8XX) {
      fd_pkt7(cs, CP_SET_MARKER, 1)
         .add(A8XX_CP_SET_MARKER_0(.mode = args.mode, .uses_gmem = args.uses_gmem));
   } else {
      fd_pkt7(cs, CP_SET_MARKER, 1)
         .add(A6XX_CP_SET_MARKER_0(.mode = args.mode, .uses_gmem = args.uses_gmem));
   }
}

static inline bool
fd6_geom_stage(mesa_shader_stage type)
{
   switch (type) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
      return true;
   case MESA_SHADER_FRAGMENT:
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return false;
   default:
      UNREACHABLE("bad shader type");
   }
}

static inline enum adreno_pm4_type3_packets
fd6_stage2opcode(mesa_shader_stage type)
{
   return fd6_geom_stage(type) ? CP_LOAD_STATE6_GEOM : CP_LOAD_STATE6_FRAG;
}

static inline enum a6xx_state_block
fd6_stage2shadersb(mesa_shader_stage type)
{
   switch (type) {
   case MESA_SHADER_VERTEX:
      return SB6_VS_SHADER;
   case MESA_SHADER_TESS_CTRL:
      return SB6_HS_SHADER;
   case MESA_SHADER_TESS_EVAL:
      return SB6_DS_SHADER;
   case MESA_SHADER_GEOMETRY:
      return SB6_GS_SHADER;
   case MESA_SHADER_FRAGMENT:
      return SB6_FS_SHADER;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return SB6_CS_SHADER;
   default:
      UNREACHABLE("bad shader type");
      return (enum a6xx_state_block)~0;
   }
}

static inline enum a6xx_tess_spacing
fd6_gl2spacing(enum gl_tess_spacing spacing)
{
   switch (spacing) {
   case TESS_SPACING_EQUAL:
      return TESS_EQUAL;
   case TESS_SPACING_FRACTIONAL_ODD:
      return TESS_FRACTIONAL_ODD;
   case TESS_SPACING_FRACTIONAL_EVEN:
      return TESS_FRACTIONAL_EVEN;
   case TESS_SPACING_UNSPECIFIED:
   default:
      UNREACHABLE("spacing must be specified");
   }
}

template <fd6_pipeline_type PIPELINE, chip CHIP>
void fd6_emit_3d_state(fd_cs &cs, struct fd6_emit *emit) assert_dt;

struct fd6_compute_state;
template <chip CHIP>
void fd6_emit_cs_state(struct fd_context *ctx, fd_cs &cs,
                       struct fd6_compute_state *cp) assert_dt;

template <chip CHIP>
void fd6_emit_gmem_cache_cntl(fd_cs &cs, struct fd_screen *screen, bool gmem);

template <chip CHIP>
void fd6_emit_static_regs(fd_cs &cs, struct fd_context *ctx);

template <chip CHIP>
void fd6_emit_restore(fd_cs &cs, struct fd_batch *batch);

void fd6_emit_init_screen(struct pipe_screen *pscreen);

template <chip CHIP>
static inline void
fd6_emit_ib(fd_cs &cs, struct fd_ringbuffer *target)
{
   if (target->cur == target->start)
      return;

   unsigned count = fd_ringbuffer_cmd_count(target);

   for (unsigned i = 0; i < count; i++) {
      uint32_t dwords;

      fd_pkt7(cs, CP_INDIRECT_BUFFER, 3)
         .add(target, i, &dwords)
         .add(A5XX_CP_INDIRECT_BUFFER_2(.ib_size = dwords));

      assert(dwords > 0);
   }
}

#endif /* FD6_EMIT_H */
