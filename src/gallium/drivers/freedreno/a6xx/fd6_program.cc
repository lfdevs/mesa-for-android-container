/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include <initializer_list>

#include "pipe/p_state.h"
#include "util/bitset.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "freedreno_program.h"

#include "fd6_const.h"
#include "fd6_emit.h"
#include "fd6_pack.h"
#include "fd6_program.h"
#include "fd6_screen.h"
#include "fd6_texture.h"

/**
 * Temporary program building state.
 */
struct program_builder {
   struct fd6_program_state *state;
   struct fd_context *ctx;
   const struct ir3_cache_key *key;
   const struct ir3_shader_variant *vs;
   const struct ir3_shader_variant *hs;
   const struct ir3_shader_variant *ds;
   const struct ir3_shader_variant *gs;
   const struct ir3_shader_variant *fs;
   const struct ir3_shader_variant *last_shader;
   bool binning_pass;
};

template <chip CHIP>
static void
emit_shader_regs(struct fd_context *ctx, fd_cs &cs, const struct ir3_shader_variant *so)
{
   fd_crb crb(cs, 12);

   mesa_shader_stage type = so->type;
   if (type == MESA_SHADER_KERNEL)
      type = MESA_SHADER_COMPUTE;

   enum a6xx_threadsize thrsz =
      so->info.double_threadsize ? THREAD128 : THREAD64;

   ir3_get_private_mem(ctx, so);

   uint32_t per_sp_size = ctx->pvtmem[so->pvtmem_per_wave].per_sp_size;
   struct fd_bo *pvtmem_bo = NULL;

   if (so->pvtmem_size > 0) { /* SP_xS_PVT_MEM_ADDR */
      pvtmem_bo = ctx->pvtmem[so->pvtmem_per_wave].bo;
      crb.attach_bo(pvtmem_bo);
   }

   crb.attach_bo(so->bo);

   switch (type) {
   case MESA_SHADER_VERTEX:
      crb.add(A6XX_SP_VS_CNTL_0(
         .halfregfootprint = so->info.max_half_reg + 1,
         .fullregfootprint = so->info.max_reg + 1,
         .branchstack = ir3_shader_branchstack_hw(so),
         .mergedregs = so->mergedregs,
         .earlypreamble = so->early_preamble,
      ));
      crb.add(A6XX_SP_VS_INSTR_SIZE(so->instrlen));
      crb.add(A6XX_SP_VS_PROGRAM_COUNTER_OFFSET());
      crb.add(A6XX_SP_VS_BASE(so->bo));
      crb.add(A6XX_SP_VS_PVT_MEM_PARAM(
         .memsizeperitem = ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size,
      ));
      crb.add(A6XX_SP_VS_PVT_MEM_BASE(pvtmem_bo));
      crb.add(A6XX_SP_VS_PVT_MEM_SIZE(
         .totalpvtmemsize = per_sp_size,
         .perwavememlayout = so->pvtmem_per_wave,
      ));
      crb.add(A6XX_SP_VS_PVT_MEM_STACK_OFFSET(.offset = per_sp_size));
      if (CHIP >= A7XX)
         crb.add(SP_VS_VGS_CNTL(CHIP));
      break;
   case MESA_SHADER_TESS_CTRL:
      crb.add(A6XX_SP_HS_CNTL_0(
         .halfregfootprint = so->info.max_half_reg + 1,
         .fullregfootprint = so->info.max_reg + 1,
         .branchstack = ir3_shader_branchstack_hw(so),
         .earlypreamble = so->early_preamble,
      ));
      crb.add(A6XX_SP_HS_INSTR_SIZE(so->instrlen));
      crb.add(A6XX_SP_HS_PROGRAM_COUNTER_OFFSET());
      crb.add(A6XX_SP_HS_BASE(so->bo));
      crb.add(A6XX_SP_HS_PVT_MEM_PARAM(
         .memsizeperitem = ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size,
      ));
      crb.add(A6XX_SP_HS_PVT_MEM_BASE(pvtmem_bo));
      crb.add(A6XX_SP_HS_PVT_MEM_SIZE(
         .totalpvtmemsize = per_sp_size,
         .perwavememlayout = so->pvtmem_per_wave,
      ));
      crb.add(A6XX_SP_HS_PVT_MEM_STACK_OFFSET(.offset = per_sp_size));
      if (CHIP >= A7XX)
         crb.add(SP_HS_VGS_CNTL(CHIP));
      break;
   case MESA_SHADER_TESS_EVAL:
      crb.add(A6XX_SP_DS_CNTL_0(
         .halfregfootprint = so->info.max_half_reg + 1,
         .fullregfootprint = so->info.max_reg + 1,
         .branchstack = ir3_shader_branchstack_hw(so),
         .earlypreamble = so->early_preamble,
      ));
      crb.add(A6XX_SP_DS_INSTR_SIZE(so->instrlen));
      crb.add(A6XX_SP_DS_PROGRAM_COUNTER_OFFSET());
      crb.add(A6XX_SP_DS_BASE(so->bo));
      crb.add(A6XX_SP_DS_PVT_MEM_PARAM(
         .memsizeperitem = ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size,
      ));
      crb.add(A6XX_SP_DS_PVT_MEM_BASE(pvtmem_bo));
      crb.add(A6XX_SP_DS_PVT_MEM_SIZE(
         .totalpvtmemsize = per_sp_size,
         .perwavememlayout = so->pvtmem_per_wave,
      ));
      crb.add(A6XX_SP_DS_PVT_MEM_STACK_OFFSET(.offset = per_sp_size));
      if (CHIP >= A7XX)
         crb.add(SP_DS_VGS_CNTL(CHIP));
      break;
   case MESA_SHADER_GEOMETRY:
      crb.add(A6XX_SP_GS_CNTL_0(
         .halfregfootprint = so->info.max_half_reg + 1,
         .fullregfootprint = so->info.max_reg + 1,
         .branchstack = ir3_shader_branchstack_hw(so),
         .earlypreamble = so->early_preamble,
      ));
      crb.add(A6XX_SP_GS_INSTR_SIZE(so->instrlen));
      crb.add(A6XX_SP_GS_PROGRAM_COUNTER_OFFSET());
      crb.add(A6XX_SP_GS_BASE(so->bo));
      crb.add(A6XX_SP_GS_PVT_MEM_PARAM(
         .memsizeperitem = ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size,
      ));
      crb.add(A6XX_SP_GS_PVT_MEM_BASE(pvtmem_bo));
      crb.add(A6XX_SP_GS_PVT_MEM_SIZE(
         .totalpvtmemsize = per_sp_size,
         .perwavememlayout = so->pvtmem_per_wave,
      ));
      crb.add(A6XX_SP_GS_PVT_MEM_STACK_OFFSET(.offset = per_sp_size));
      if (CHIP >= A7XX)
         crb.add(SP_GS_VGS_CNTL(CHIP));
      break;
   case MESA_SHADER_FRAGMENT:
      crb.add(A6XX_SP_PS_CNTL_0(
         .halfregfootprint = so->info.max_half_reg + 1,
         .fullregfootprint = so->info.max_reg + 1,
         .branchstack = ir3_shader_branchstack_hw(so),
         .threadsize = thrsz,
         .varying = so->total_in != 0,
         .lodpixmask = so->need_full_quad,
         .inoutregoverlap = true,
         .pixlodenable = so->need_pixlod,
         .earlypreamble = so->early_preamble,
         .mergedregs = so->mergedregs,
      ));
      if (CHIP >= A8XX) {
         crb.add(RB_PS_CNTL(CHIP,
            .pixlodenable = so->need_pixlod,
            .lodpixmask = so->need_full_quad,
         ));
      }
      crb.add(A6XX_SP_PS_INSTR_SIZE(so->instrlen));
      crb.add(A6XX_SP_PS_PROGRAM_COUNTER_OFFSET());
      crb.add(A6XX_SP_PS_BASE(so->bo));
      crb.add(A6XX_SP_PS_PVT_MEM_PARAM(
         .memsizeperitem = ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size,
      ));
      crb.add(A6XX_SP_PS_PVT_MEM_BASE(pvtmem_bo));
      crb.add(A6XX_SP_PS_PVT_MEM_SIZE(
         .totalpvtmemsize = per_sp_size,
         .perwavememlayout = so->pvtmem_per_wave,
      ));
      crb.add(A6XX_SP_PS_PVT_MEM_STACK_OFFSET(.offset = per_sp_size));
      if (CHIP >= A7XX)
         crb.add(SP_PS_VGS_CNTL(CHIP));
      break;
   case MESA_SHADER_COMPUTE:
      thrsz = ctx->screen->info->props.supports_double_threadsize ? thrsz : THREAD128;
      crb.add(A6XX_SP_CS_CNTL_0(
         .halfregfootprint = so->info.max_half_reg + 1,
         .fullregfootprint = so->info.max_reg + 1,
         .branchstack = ir3_shader_branchstack_hw(so),
         .threadsize = thrsz,
         .earlypreamble = so->early_preamble,
         .mergedregs = so->mergedregs,
      ));
      crb.add(A6XX_SP_CS_INSTR_SIZE(so->instrlen));
      crb.add(A6XX_SP_CS_PROGRAM_COUNTER_OFFSET());
      crb.add(A6XX_SP_CS_BASE(so->bo));
      crb.add(A6XX_SP_CS_PVT_MEM_PARAM(
         .memsizeperitem = ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size,
      ));
      crb.add(A6XX_SP_CS_PVT_MEM_BASE(pvtmem_bo));
      crb.add(A6XX_SP_CS_PVT_MEM_SIZE(
         .totalpvtmemsize = per_sp_size,
         .perwavememlayout = so->pvtmem_per_wave,
      ));
      crb.add(A6XX_SP_CS_PVT_MEM_STACK_OFFSET(.offset = per_sp_size));
      if (CHIP >= A7XX)
         crb.add(SP_CS_VGS_CNTL(CHIP));
      break;
   default:
      UNREACHABLE("bad shader stage");
   }
}

template <chip CHIP>
void
fd6_emit_shader(struct fd_context *ctx, fd_cs &cs, const struct ir3_shader_variant *so)
{
   if (!so) {
      /* shader stage disabled */
      return;
   }

#if MESA_DEBUG
   /* Name should generally match what you get with MESA_SHADER_CAPTURE_PATH: */
   const char *name = so->name;
   if (name)
      fd_emit_string5(cs, name, strlen(name));
#endif

   emit_shader_regs<CHIP>(ctx, cs, so);

   if (CHIP == A6XX) {
      uint32_t shader_preload_size =
         MIN2(so->instrlen, ctx->screen->info->props.instr_cache_size);

      fd_pkt7(cs, fd6_stage2opcode(so->type), 3)
         .add(CP_LOAD_STATE6_0(
            .state_type = ST6_SHADER,
            .state_src = SS6_INDIRECT,
            .state_block = fd6_stage2shadersb(so->type),
            .num_unit = shader_preload_size,
         ))
         .add(CP_LOAD_STATE6_EXT_SRC_ADDR(.bo = so->bo));
   }

   fd6_emit_immediates<CHIP>(so, cs);
}
FD_GENX(fd6_emit_shader);

/**
 * Build a pre-baked state-obj to disable SO, so that we aren't dynamically
 * building this at draw time whenever we transition from SO enabled->disabled
 */
template <chip CHIP>
static void
setup_stream_out_disable(struct fd_context *ctx)
{
   unsigned nreg = 2;

   if (ctx->screen->info->props.tess_use_shared)
      nreg++;

   fd_crb crb(ctx->pipe, nreg);

   crb.add(VPC_SO_MAPPING_WPTR(CHIP));
   crb.add(VPC_SO_CNTL(CHIP));

   if (ctx->screen->info->props.tess_use_shared) {
      crb.add(PC_DGEN_SO_CNTL(CHIP));
   }

   fd6_context(ctx)->streamout_disable_stateobj = crb;
}

template <chip CHIP>
static void
setup_stream_out(struct fd_context *ctx, struct fd6_program_state *state,
                 const struct ir3_shader_variant *v,
                 struct ir3_shader_linkage *l)
{
   const struct ir3_stream_output_info *strmout = &v->stream_output;

   /* Note: 64 here comes from the HW layout of the program RAM. The program
    * for stream N is at DWORD 64 * N.
    */
#define A6XX_SO_PROG_DWORDS 64
   uint32_t prog[A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS] = {};
   BITSET_DECLARE(valid_dwords, A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) = {0};

   memset(prog, 0, sizeof(prog));

   for (unsigned i = 0; i < strmout->num_outputs; i++) {
      const struct ir3_stream_output *out = &strmout->output[i];
      unsigned k = out->register_index;
      unsigned idx;

      /* linkage map sorted by order frag shader wants things, so
       * a bit less ideal here..
       */
      for (idx = 0; idx < l->cnt; idx++)
         if (l->var[idx].slot == v->outputs[k].slot)
            break;

      assert(idx < l->cnt);

      for (unsigned j = 0; j < out->num_components; j++) {
         unsigned c = j + out->start_component;
         unsigned loc = l->var[idx].loc + c;
         unsigned off = j + out->dst_offset; /* in dwords */

         unsigned dword = out->stream * A6XX_SO_PROG_DWORDS + loc/2;
         if (loc & 1) {
            prog[dword] |= A6XX_VPC_SO_MAPPING_PORT_B_EN |
                           A6XX_VPC_SO_MAPPING_PORT_B_BUF(out->output_buffer) |
                           A6XX_VPC_SO_MAPPING_PORT_B_OFF(off * 4);
         } else {
            prog[dword] |= A6XX_VPC_SO_MAPPING_PORT_A_EN |
                           A6XX_VPC_SO_MAPPING_PORT_A_BUF(out->output_buffer) |
                           A6XX_VPC_SO_MAPPING_PORT_A_OFF(off * 4);
         }
         BITSET_SET(valid_dwords, dword);
      }
   }

   unsigned prog_count = 0;
   unsigned start, end;
   BITSET_FOREACH_RANGE (start, end, valid_dwords,
                         A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      prog_count += end - start + 1;
   }

   const bool emit_pc_so_stream_cntl =
         ctx->screen->info->props.tess_use_shared &&
         v->type == MESA_SHADER_TESS_EVAL;

   unsigned nreg = 5 + prog_count;
   if (emit_pc_so_stream_cntl)
      nreg++;

   fd_crb crb(ctx->pipe, nreg);

   crb.add(VPC_SO_CNTL(CHIP,
      .buf0_stream = 1 + strmout->output[0].stream,
      .buf1_stream = 1 + strmout->output[1].stream,
      .buf2_stream = 1 + strmout->output[2].stream,
      .buf3_stream = 1 + strmout->output[3].stream,
      .stream_enable = strmout->streams_written,
   ));

   for (unsigned i = 0; i < 4; i++)
      crb.add(VPC_SO_BUFFER_STRIDE(CHIP, i, strmout->stride[i]));

   bool first = true;
   BITSET_FOREACH_RANGE (start, end, valid_dwords,
                         A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      crb.add(VPC_SO_MAPPING_WPTR(CHIP, .addr = start, .reset = first));
      for (unsigned i = start; i < end; i++) {
         crb.add(VPC_SO_MAPPING_PORT(CHIP, .dword = prog[i]));
      }
      first = false;
   }

   if (emit_pc_so_stream_cntl) {
      /* Possibly not tess_use_shared related, but the combination of
       * tess + xfb fails some tests if we don't emit this.
       */
      crb.add(PC_DGEN_SO_CNTL(CHIP, .stream_enable = true));
   }

   state->streamout_stateobj = crb;
}

static uint32_t
sp_xs_config(const struct ir3_shader_variant *v)
{
   if (!v)
      return 0;

   return A6XX_SP_VS_CONFIG_ENABLED |
         COND(v->bindless_tex, A6XX_SP_VS_CONFIG_BINDLESS_TEX) |
         COND(v->bindless_samp, A6XX_SP_VS_CONFIG_BINDLESS_SAMP) |
         COND(v->bindless_ibo, A6XX_SP_VS_CONFIG_BINDLESS_UAV) |
         COND(v->bindless_ubo, A6XX_SP_VS_CONFIG_BINDLESS_UBO) |
         A6XX_SP_VS_CONFIG_NUAV(ir3_shader_num_uavs(v)) |
         A6XX_SP_VS_CONFIG_NTEX(v->num_samp) |
         A6XX_SP_VS_CONFIG_NSAMP(v->num_samp);
}

template <chip CHIP>
static void
setup_config_stateobj(struct fd_context *ctx, struct fd6_program_state *state)
{
   fd_crb crb(ctx->pipe, 12);

   crb.add(SP_UPDATE_CNTL(CHIP,
         .vs_state = true, .hs_state = true,
         .ds_state = true, .gs_state = true,
         .fs_state = true, .cs_state = true,
         .cs_uav = true, .gfx_uav = true,
   ));

   assert(state->vs->constlen >= state->bs->constlen);

   crb.add(SP_VS_CONST_CONFIG(CHIP,
         .constlen = state->vs->constlen,
         .enabled = true,
   ));
   crb.add(SP_HS_CONST_CONFIG(CHIP,
         .constlen = COND(state->hs, state->hs->constlen),
         .enabled = COND(state->hs, true),
   ));
   crb.add(SP_DS_CONST_CONFIG(CHIP,
         .constlen = COND(state->ds, state->ds->constlen),
         .enabled = COND(state->ds, true),
   ));
   crb.add(SP_GS_CONST_CONFIG(CHIP,
         .constlen = COND(state->gs, state->gs->constlen),
         .enabled = COND(state->gs, true),
   ));
   crb.add(SP_PS_CONST_CONFIG(CHIP,
         .constlen = state->fs->constlen,
         .enabled = true,
   ));

   crb.add(A6XX_SP_VS_CONFIG(.dword = sp_xs_config(state->vs)));
   crb.add(A6XX_SP_HS_CONFIG(.dword = sp_xs_config(state->hs)));
   crb.add(A6XX_SP_DS_CONFIG(.dword = sp_xs_config(state->ds)));
   crb.add(A6XX_SP_GS_CONFIG(.dword = sp_xs_config(state->gs)));
   crb.add(A6XX_SP_PS_CONFIG(.dword = sp_xs_config(state->fs)));

   crb.add(SP_GFX_USIZE(CHIP, ir3_shader_num_uavs(state->fs)));

   state->config_stateobj = crb;
}

static inline uint32_t
next_regid(uint32_t reg, uint32_t increment)
{
   if (VALIDREG(reg))
      return reg + increment;
   else
      return INVALID_REG;
}

static enum a6xx_tess_output
primitive_to_tess(enum mesa_prim primitive)
{
   switch (primitive) {
   case MESA_PRIM_POINTS:
      return TESS_POINTS;
   case MESA_PRIM_LINE_STRIP:
      return TESS_LINES;
   case MESA_PRIM_TRIANGLE_STRIP:
      return TESS_CW_TRIS;
   default:
      UNREACHABLE("");
   }
}

#define MAX_VERTEX_ATTRIBS 32

template <chip CHIP>
static void
emit_vfd_dest(fd_crb &crb, const struct ir3_shader_variant *vs)
{
   uint32_t attr_count = 0;

   for (uint32_t i = 0; i < vs->inputs_count; i++)
      if (!vs->inputs[i].sysval)
         attr_count++;

   crb.add(A6XX_VFD_CNTL_0(
      .fetch_cnt = attr_count, /* decode_cnt for binning pass ? */
      .decode_cnt = attr_count
   ));

   if (CHIP >= A8XX) {
      const uint32_t vertexid_regid =
            ir3_find_sysval_regid(vs, SYSTEM_VALUE_VERTEX_ID);
      const uint32_t instanceid_regid =
            ir3_find_sysval_regid(vs, SYSTEM_VALUE_INSTANCE_ID);
      /* Note: we currently don't support multiview.
       */
      const uint32_t viewid_regid = INVALID_REG;

      unsigned sideband_count =
         (vertexid_regid != INVALID_REG) +
         (instanceid_regid != INVALID_REG) +
         (viewid_regid != INVALID_REG);

      crb.add(PC_VS_INPUT_CNTL(CHIP,
         .instr_cnt = attr_count,
         .sideband_cnt = sideband_count,
      ));
   }

   for (uint32_t i = 0; i < attr_count; i++) {
      assert(!vs->inputs[i].sysval);
      crb.add(A6XX_VFD_DEST_CNTL_INSTR(i,
         .writemask = vs->inputs[i].compmask,
         .regid = vs->inputs[i].regid,
      ));
   }
}

/* nregs: 6 */
static void
emit_vs_system_values(fd_crb &crb, const struct program_builder *b)
{
   const uint32_t vertexid_regid =
         ir3_find_sysval_regid(b->vs, SYSTEM_VALUE_VERTEX_ID);
   const uint32_t instanceid_regid =
         ir3_find_sysval_regid(b->vs, SYSTEM_VALUE_INSTANCE_ID);
   const uint32_t tess_coord_x_regid =
         ir3_find_sysval_regid(b->ds, SYSTEM_VALUE_TESS_COORD);
   const uint32_t tess_coord_y_regid = next_regid(tess_coord_x_regid, 1);
   const uint32_t hs_rel_patch_regid =
         ir3_find_sysval_regid(b->hs, SYSTEM_VALUE_REL_PATCH_ID_IR3);
   const uint32_t ds_rel_patch_regid =
         ir3_find_sysval_regid(b->ds, SYSTEM_VALUE_REL_PATCH_ID_IR3);
   const uint32_t hs_invocation_regid =
         ir3_find_sysval_regid(b->hs, SYSTEM_VALUE_TCS_HEADER_IR3);
   const uint32_t gs_primitiveid_regid =
         ir3_find_sysval_regid(b->gs, SYSTEM_VALUE_PRIMITIVE_ID);
   const uint32_t vs_primitiveid_regid = b->hs ?
         ir3_find_sysval_regid(b->hs, SYSTEM_VALUE_PRIMITIVE_ID) :
         gs_primitiveid_regid;
   const uint32_t ds_primitiveid_regid =
         ir3_find_sysval_regid(b->ds, SYSTEM_VALUE_PRIMITIVE_ID);
   const uint32_t gsheader_regid =
         ir3_find_sysval_regid(b->gs, SYSTEM_VALUE_GS_HEADER_IR3);

   /* Note: we currently don't support multiview.
    */
   const uint32_t viewid_regid = INVALID_REG;

   crb.add(A6XX_VFD_CNTL_1(
      .regid4vtx = vertexid_regid,
      .regid4inst = instanceid_regid,
      .regid4primid = vs_primitiveid_regid,
      .regid4viewid = viewid_regid,
   ));
   crb.add(A6XX_VFD_CNTL_2(
      .regid_hsrelpatchid = hs_rel_patch_regid,
      .regid_invocationid = hs_invocation_regid,
   ));
   crb.add(A6XX_VFD_CNTL_3(
      .regid_dsprimid = ds_primitiveid_regid,
      .regid_dsrelpatchid = ds_rel_patch_regid,
      .regid_tessx = tess_coord_x_regid,
      .regid_tessy = tess_coord_y_regid,
   ));
   crb.add(A6XX_VFD_CNTL_4(.unk0 = INVALID_REG));
   crb.add(A6XX_VFD_CNTL_5(
      .regid_gsheader = gsheader_regid,
      .unk8 = INVALID_REG,
   ));
   crb.add(A6XX_VFD_CNTL_6(.primid4psen = b->fs->reads_primid));
}

template <chip CHIP>
static void
emit_linkmap(fd_cs &cs, const struct program_builder *b)
{
   if (b->hs) {
      fd6_emit_link_map<CHIP>(b->ctx, cs, b->vs, b->hs);
      fd6_emit_link_map<CHIP>(b->ctx, cs, b->hs, b->ds);
   }

   if (b->gs) {
      if (b->hs) {
         fd6_emit_link_map<CHIP>(b->ctx, cs, b->ds, b->gs);
      } else {
         fd6_emit_link_map<CHIP>(b->ctx, cs, b->vs, b->gs);
      }
   }
}

template <chip CHIP>
static void
emit_vpc(fd_crb &crb, const struct program_builder *b)
{
   const struct ir3_shader_variant *last_shader = b->last_shader;
   struct ir3_shader_linkage linkage = {
      .primid_loc = 0xff,
      .clip0_loc = 0xff,
      .clip1_loc = 0xff,
   };

   /* If we have streamout, link against the real FS, rather than the
    * dummy FS used for binning pass state, to ensure the OUTLOC's
    * match.  Depending on whether we end up doing sysmem or gmem,
    * the actual streamout could happen with either the binning pass
    * or draw pass program, but the same streamout stateobj is used
    * in either case:
    */
   bool do_streamout = (b->last_shader->stream_output.num_outputs > 0);
   ir3_link_shaders(&linkage, b->last_shader,
                    do_streamout ? b->state->fs : b->fs,
                    true);

   if (do_streamout)
      ir3_link_stream_out(&linkage, b->last_shader);

   emit_vs_system_values(crb, b);

   for (unsigned i = 0; i < 4; i++)
      crb.add(VPC_VARYING_LM_TRANSFER_CNTL_DISABLE(CHIP, i, ~linkage.varmask[i]));

   /* a6xx finds position/pointsize at the end */
   const uint32_t position_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_POS);
   const uint32_t pointsize_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_PSIZ);
   const uint32_t layer_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_LAYER);
   const uint32_t view_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_VIEWPORT);
   const uint32_t clip0_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_CLIP_DIST0);
   const uint32_t clip1_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_CLIP_DIST1);
   uint32_t flags_regid = b->gs ?
      ir3_find_output_regid(b->gs, VARYING_SLOT_GS_VERTEX_FLAGS_IR3) : 0;

   uint32_t pointsize_loc = 0xff, position_loc = 0xff, layer_loc = 0xff, view_loc = 0xff;

   if (layer_regid != INVALID_REG) {
      layer_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_LAYER, layer_regid, 0x1, linkage.max_loc);
   }

   if (view_regid != INVALID_REG) {
      view_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_VIEWPORT, view_regid, 0x1, linkage.max_loc);
   }

   if (position_regid != INVALID_REG) {
      position_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_POS, position_regid, 0xf, linkage.max_loc);
   }

   if (pointsize_regid != INVALID_REG) {
      pointsize_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_PSIZ, pointsize_regid, 0x1, linkage.max_loc);
   }

   uint8_t clip_mask = last_shader->clip_mask,
           cull_mask = last_shader->cull_mask;
   uint8_t clip_cull_mask = clip_mask | cull_mask;

   clip_mask &= b->key->clip_plane_enable;

   /* Handle the case where clip/cull distances aren't read by the FS */
   uint32_t clip0_loc = linkage.clip0_loc, clip1_loc = linkage.clip1_loc;
   if (clip0_loc == 0xff && clip0_regid != INVALID_REG) {
      clip0_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_CLIP_DIST0, clip0_regid,
                   clip_cull_mask & 0xf, linkage.max_loc);
   }
   if (clip1_loc == 0xff && clip1_regid != INVALID_REG) {
      clip1_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_CLIP_DIST1, clip1_regid,
                   clip_cull_mask >> 4, linkage.max_loc);
   }

   /* If we have stream-out, we use the full shader for binning
    * pass, rather than the optimized binning pass one, so that we
    * have all the varying outputs available for xfb.  So streamout
    * state should always be derived from the non-binning pass
    * program:
    */
   if (do_streamout && !b->binning_pass) {
      setup_stream_out<CHIP>(b->ctx, b->state, b->last_shader, &linkage);

      if (!fd6_context(b->ctx)->streamout_disable_stateobj)
         setup_stream_out_disable<CHIP>(b->ctx);
   }

   /* There is a hardware bug on a750 where STRIDE_IN_VPC of 5 to 8 in GS with
    * an input primitive type with adjacency, an output primitive type of
    * points, and a high enough vertex count causes a hang.
    */
   if (b->ctx->screen->info->props.gs_vpc_adjacency_quirk &&
       b->gs && b->gs->gs.output_primitive == MESA_PRIM_POINTS &&
       linkage.max_loc > 4) {
      linkage.max_loc = MAX2(linkage.max_loc, 9);
   }

   /* The GPU hangs on some models when there are no outputs (xs_pack::CNT),
    * at least when a DS is the last stage, so add a dummy output to keep it
    * happy if there aren't any. We do this late in order to avoid emitting
    * any unused code and make sure that optimizations don't remove it.
    */
   if (linkage.cnt == 0)
      ir3_link_add(&linkage, 0, 0, 0x1, linkage.max_loc);

   /* map outputs of the last shader to VPC */
   assert(linkage.cnt <= 32);
   const uint32_t sp_out_count = DIV_ROUND_UP(linkage.cnt, 2);
   const uint32_t sp_vpc_dst_count = DIV_ROUND_UP(linkage.cnt, 4);
   uint16_t sp_out[32] = {0};
   uint8_t sp_vpc_dst[32] = {0};
   for (uint32_t i = 0; i < linkage.cnt; i++) {
      sp_out[i] =
         A6XX_SP_VS_OUTPUT_REG_A_REGID(linkage.var[i].regid) |
         A6XX_SP_VS_OUTPUT_REG_A_COMPMASK(linkage.var[i].compmask);
      sp_vpc_dst[i] =
         A6XX_SP_VS_VPC_DEST_REG_OUTLOC0(linkage.var[i].loc);
   }

   uint32_t *regs;

   switch (last_shader->type) {
   case MESA_SHADER_VERTEX:
      regs = (uint32_t *)sp_out;
      for (unsigned i = 0; i < sp_out_count; i++)
         crb.add(A6XX_SP_VS_OUTPUT_REG(i, .dword = regs[i]));

      regs = (uint32_t *)sp_vpc_dst;
      for (unsigned i = 0; i < sp_vpc_dst_count; i++)
         crb.add(A6XX_SP_VS_VPC_DEST_REG(i, .dword = regs[i]));

      crb.add(VPC_VS_CNTL(CHIP,
         .stride_in_vpc = linkage.max_loc,
         .positionloc = position_loc,
         .psizeloc = pointsize_loc,
      ));

      crb.add(VPC_VS_CLIP_CULL_CNTL(CHIP,
         .clip_mask = clip_cull_mask,
         .clip_dist_03_loc = clip0_loc,
         .clip_dist_47_loc = clip1_loc,
      ));

      if (CHIP <= A7XX) {
         crb.add(VPC_VS_CLIP_CULL_CNTL_V2(CHIP,
            .clip_mask = clip_cull_mask,
            .clip_dist_03_loc = clip0_loc,
            .clip_dist_47_loc = clip1_loc,
         ));
      }

      crb.add(GRAS_CL_VS_CLIP_CULL_DISTANCE(CHIP,
         .clip_mask = clip_mask,
         .cull_mask = cull_mask,
      ));

      break;
   case MESA_SHADER_TESS_EVAL:
      regs = (uint32_t *)sp_out;
      for (unsigned i = 0; i < sp_out_count; i++)
         crb.add(A6XX_SP_DS_OUTPUT_REG(i, .dword = regs[i]));

      regs = (uint32_t *)sp_vpc_dst;
      for (unsigned i = 0; i < sp_vpc_dst_count; i++)
         crb.add(A6XX_SP_DS_VPC_DEST_REG(i, .dword = regs[i]));

      crb.add(VPC_DS_CNTL(CHIP,
         .stride_in_vpc = linkage.max_loc,
         .positionloc = position_loc,
         .psizeloc = pointsize_loc,
      ));

      crb.add(VPC_DS_CLIP_CULL_CNTL(CHIP,
         .clip_mask = clip_cull_mask,
         .clip_dist_03_loc = clip0_loc,
         .clip_dist_47_loc = clip1_loc,
      ));

      if (CHIP <= A7XX) {
         crb.add(VPC_DS_CLIP_CULL_CNTL_V2(CHIP,
            .clip_mask = clip_cull_mask,
            .clip_dist_03_loc = clip0_loc,
            .clip_dist_47_loc = clip1_loc,
         ));
      }

      crb.add(GRAS_CL_DS_CLIP_CULL_DISTANCE(CHIP,
         .clip_mask = clip_mask,
         .cull_mask = cull_mask,
      ));

      break;
   case MESA_SHADER_GEOMETRY:
      regs = (uint32_t *)sp_out;
      for (unsigned i = 0; i < sp_out_count; i++)
         crb.add(A6XX_SP_GS_OUTPUT_REG(i, .dword = regs[i]));

      regs = (uint32_t *)sp_vpc_dst;
      for (unsigned i = 0; i < sp_vpc_dst_count; i++)
         crb.add(A6XX_SP_GS_VPC_DEST_REG(i, .dword = regs[i]));

      crb.add(VPC_GS_CNTL(CHIP,
         .stride_in_vpc = linkage.max_loc,
         .positionloc = position_loc,
         .psizeloc = pointsize_loc,
      ));

      crb.add(VPC_GS_CLIP_CULL_CNTL(CHIP,
         .clip_mask = clip_cull_mask,
         .clip_dist_03_loc = clip0_loc,
         .clip_dist_47_loc = clip1_loc,
      ));

      if (CHIP <= A7XX) {
         crb.add(VPC_GS_CLIP_CULL_CNTL_V2(CHIP,
            .clip_mask = clip_cull_mask,
            .clip_dist_03_loc = clip0_loc,
            .clip_dist_47_loc = clip1_loc,
         ));
      }

      crb.add(GRAS_CL_GS_CLIP_CULL_DISTANCE(CHIP,
         .clip_mask = clip_mask,
         .cull_mask = cull_mask,
      ));

      break;
   default:
      UNREACHABLE("bad last_shader type");
   }

   const struct ir3_shader_variant *geom_stages[] = { b->vs, b->hs, b->ds, b->gs };

   for (unsigned i = 0; i < ARRAY_SIZE(geom_stages); i++) {
      const struct ir3_shader_variant *shader = geom_stages[i];
      if (!shader)
         continue;

      bool primid = shader->type != MESA_SHADER_VERTEX &&
         VALIDREG(ir3_find_sysval_regid(shader, SYSTEM_VALUE_PRIMITIVE_ID));
      bool last = shader == last_shader;

      switch (shader->type) {
      case MESA_SHADER_VERTEX:
         crb.add(PC_VS_CNTL(CHIP,
            .stride_in_vpc = COND(last, linkage.max_loc),
            .psize = COND(last, VALIDREG(pointsize_regid)),
            .layer = COND(last, VALIDREG(layer_regid)),
            .view = COND(last, VALIDREG(view_regid)),
            .primitive_id = primid,
            .clip_mask = COND(last, clip_cull_mask),
         ));
         break;
      case MESA_SHADER_TESS_CTRL:
         assert(!last);
         crb.add(PC_HS_CNTL(CHIP,
            .primitive_id = primid,
         ));
      case MESA_SHADER_TESS_EVAL:
         crb.add(PC_DS_CNTL(CHIP,
            .stride_in_vpc = COND(last, linkage.max_loc),
            .psize = COND(last, VALIDREG(pointsize_regid)),
            .layer = COND(last, VALIDREG(layer_regid)),
            .view = COND(last, VALIDREG(view_regid)),
            .primitive_id = primid,
            .clip_mask = COND(last, clip_cull_mask),
         ));
         break;
      case MESA_SHADER_GEOMETRY:
         crb.add(PC_GS_CNTL(CHIP,
            .stride_in_vpc = COND(last, linkage.max_loc),
            .psize = COND(last, VALIDREG(pointsize_regid)),
            .layer = COND(last, VALIDREG(layer_regid)),
            .view = COND(last, VALIDREG(view_regid)),
            .primitive_id = primid,
            .clip_mask = COND(last, clip_cull_mask),
         ));
         break;
      default:
         break;
      }
   }

   /* if vertex_flags somehow gets optimized out, your gonna have a bad time: */
   assert(flags_regid != INVALID_REG);

   switch (last_shader->type) {
   case MESA_SHADER_VERTEX:
      crb.add(A6XX_SP_VS_OUTPUT_CNTL(.out = linkage.cnt));
      crb.add(VPC_VS_SIV_CNTL(CHIP,
         .layerloc = layer_loc,
         .viewloc = view_loc,
         .shadingrateloc = 0xff,
      ));
      if (CHIP <= A7XX) {
         crb.add(VPC_VS_SIV_CNTL_V2(CHIP,
            .layerloc = layer_loc,
            .viewloc = view_loc,
            .shadingrateloc = 0xff,
         ));
      }
      crb.add(GRAS_SU_VS_SIV_CNTL(CHIP,
         .writes_layer = VALIDREG(layer_regid),
         .writes_view = VALIDREG(view_regid),
      ));
      break;
   case MESA_SHADER_TESS_EVAL:
      crb.add(A6XX_SP_DS_OUTPUT_CNTL(.out = linkage.cnt));
      crb.add(VPC_DS_SIV_CNTL(CHIP,
         .layerloc = layer_loc,
         .viewloc = view_loc,
         .shadingrateloc = 0xff,
      ));
      if (CHIP <= A7XX) {
         crb.add(VPC_DS_SIV_CNTL_V2(CHIP,
            .layerloc = layer_loc,
            .viewloc = view_loc,
            .shadingrateloc = 0xff,
         ));
      }
      crb.add(GRAS_SU_DS_SIV_CNTL(CHIP,
         .writes_layer = VALIDREG(layer_regid),
         .writes_view = VALIDREG(view_regid),
      ));
      break;
   case MESA_SHADER_GEOMETRY:
      crb.add(A6XX_SP_GS_OUTPUT_CNTL(.out = linkage.cnt, .flags_regid = flags_regid));
      crb.add(VPC_GS_SIV_CNTL(CHIP,
         .layerloc = layer_loc,
         .viewloc = view_loc,
         .shadingrateloc = 0xff,
      ));
      if (CHIP <= A7XX) {
         crb.add(VPC_GS_SIV_CNTL_V2(CHIP,
            .layerloc = layer_loc,
            .viewloc = view_loc,
            .shadingrateloc = 0xff,
         ));
      }
      crb.add(GRAS_SU_GS_SIV_CNTL(CHIP,
         .writes_layer = VALIDREG(layer_regid),
         .writes_view = VALIDREG(view_regid),
      ));
      break;
   default:
      UNREACHABLE("bad last_shader type");
   }

   crb.add(PC_PS_CNTL(CHIP, b->fs->reads_primid));

   if (CHIP >= A7XX) {
      crb.add(GRAS_MODE_CNTL(CHIP, 0x2));
      crb.add(SP_RENDER_CNTL(CHIP, .fs_disable = false));
   }

   crb.add(VPC_PS_CNTL(CHIP,
      .numnonposvar = b->fs->total_in,
      .primidloc = linkage.primid_loc,
      .varying = !!b->fs->total_in,
      .viewidloc = linkage.viewid_loc,
   ));

   if (b->hs) {
      crb.add(PC_HS_PARAM_0(CHIP, b->hs->tess.tcs_vertices_out));
   }

   if (b->gs) {
      uint32_t vertices_out, invocations, vec4_size;
      uint32_t prev_stage_output_size =
         b->ds ? b->ds->output_size : b->vs->output_size;

      vertices_out = MAX2(1, b->gs->gs.vertices_out) - 1;
      enum a6xx_tess_output output =
         primitive_to_tess((enum mesa_prim)b->gs->gs.output_primitive);
      invocations = b->gs->gs.invocations - 1;
      /* Size of per-primitive alloction in ldlw memory in vec4s. */
      vec4_size = b->gs->gs.vertices_in *
                  DIV_ROUND_UP(prev_stage_output_size, 4);

      crb.add(PC_GS_PARAM_0(CHIP,
         .gs_vertices_out = vertices_out,
         .gs_invocations = invocations,
         .gs_output = output,
      ));

      if (CHIP >= A7XX) {
         crb.add(VPC_GS_PARAM_0(CHIP,
            .gs_vertices_out = vertices_out,
            .gs_invocations = invocations,
            .gs_output = output,
         ));
      } else {
         crb.add(VPC_GS_PARAM(CHIP, 0xff));
      }

      if (CHIP == A6XX) {
         crb.add(PC_PRIMITIVE_CNTL_6(CHIP, vec4_size));
      }

      uint32_t prim_size = prev_stage_output_size;
      if (prim_size > 64)
         prim_size = 64;
      else if (prim_size == 64)
         prim_size = 63;

      crb.add(A6XX_SP_GS_CNTL_1(prim_size));
   }
}

static enum a6xx_tex_prefetch_cmd
tex_opc_to_prefetch_cmd(opc_t tex_opc)
{
   switch (tex_opc) {
   case OPC_SAM:
      return TEX_PREFETCH_SAM;
   default:
      UNREACHABLE("Unknown tex opc for prefeth cmd");
   }
}

template <chip CHIP>
static void
emit_fs_inputs(fd_crb &crb, const struct program_builder *b)
{
   const struct ir3_shader_variant *fs = b->fs;
   uint32_t face_regid, coord_regid, zwcoord_regid, samp_id_regid;
   uint32_t ij_regid[IJ_COUNT];
   uint32_t smask_in_regid;

   bool sample_shading = fs->sample_shading;
   bool enable_varyings = fs->total_in > 0;

   samp_id_regid   = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_ID);
   smask_in_regid  = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_MASK_IN);
   face_regid      = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRONT_FACE);
   coord_regid     = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRAG_COORD);
   zwcoord_regid   = VALIDREG(coord_regid) ? coord_regid + 2 : INVALID_REG;
   for (unsigned i = 0; i < ARRAY_SIZE(ij_regid); i++)
      ij_regid[i] = ir3_find_sysval_regid(fs, SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL + i);

   if (fs->num_sampler_prefetch > 0) {
      /* FS prefetch reads coordinates from r0.x */
      assert(!VALIDREG(ij_regid[fs->prefetch_bary_type]) ||
             ij_regid[fs->prefetch_bary_type] == regid(0, 0));
   }

   crb.add(A6XX_SP_PS_INITIAL_TEX_LOAD_CNTL(
      .count = fs->num_sampler_prefetch,
      .ij_write_disable = !VALIDREG(ij_regid[IJ_PERSP_PIXEL]),
      .endofquad = fs->prefetch_end_of_quad,
      .constslotid = COND(CHIP >= A7XX, 0x1ff),
      .constslotid4coord = COND(CHIP >= A7XX, 0x1ff),
   ));

   for (int i = 0; i < fs->num_sampler_prefetch; i++) {
      const struct ir3_sampler_prefetch *prefetch = &fs->sampler_prefetch[i];
      crb.add(SP_PS_INITIAL_TEX_LOAD_CMD(CHIP, i,
         .src = prefetch->src,
         /* For a7xx, samp_id/tex_id is always in SP_PS_INITIAL_TEX_INDEX_CMD[n]
          * even in the non-bindless case (which probably makes the reg name
          * wrong)
          */
         .samp_id = (CHIP == A6XX) ? prefetch->samp_id : 0,
         .tex_id = (CHIP == A6XX) ? prefetch->tex_id : 0,
         .dst = prefetch->dst,
         .wrmask = prefetch->wrmask,
         .half = prefetch->half_precision,
         .bindless = prefetch->bindless,
         .cmd = tex_opc_to_prefetch_cmd(prefetch->tex_opc),
      ));
   }

   if (CHIP >= A7XX) {
      for (int i = 0; i < fs->num_sampler_prefetch; i++) {
         const struct ir3_sampler_prefetch *prefetch = &fs->sampler_prefetch[i];
         crb.add(A6XX_SP_PS_INITIAL_TEX_INDEX_CMD(i,
            .samp_id = prefetch->samp_id,
            .tex_id = prefetch->tex_id,
         ));
      }
   }

   crb.add(SP_LB_PARAM_LIMIT(CHIP, b->ctx->screen->info->props.prim_alloc_threshold));

   if (CHIP == A8XX)
      crb.add(RB_LB_PARAM_LIMIT(CHIP, b->ctx->screen->info->props.prim_alloc_threshold));

   crb.add(SP_REG_PROG_ID_0(CHIP,
      .faceregid = face_regid,
      .sampleid = samp_id_regid,
      .samplemask = smask_in_regid,
      .centerrhw = ij_regid[IJ_PERSP_CENTER_RHW],
   ));
   crb.add(SP_REG_PROG_ID_1(CHIP,
      .ij_persp_pixel = ij_regid[IJ_PERSP_PIXEL],
      .ij_linear_pixel = ij_regid[IJ_LINEAR_PIXEL],
      .ij_persp_centroid = ij_regid[IJ_PERSP_CENTROID],
      .ij_linear_centroid = ij_regid[IJ_LINEAR_CENTROID],
   ));
   crb.add(SP_REG_PROG_ID_2(CHIP,
      .ij_persp_sample = ij_regid[IJ_PERSP_SAMPLE],
      .ij_linear_sample = ij_regid[IJ_LINEAR_SAMPLE],
      .xycoordregid = coord_regid,
      .zwcoordregid = zwcoord_regid,
   ));
   crb.add(SP_REG_PROG_ID_3(CHIP,
      .linelengthregid = INVALID_REG,
      .foveationqualityregid = INVALID_REG,
   ));

   if (CHIP >= A7XX) {
      uint32_t sysval_regs = 0;
      for (unsigned i = 0; i < ARRAY_SIZE(ij_regid); i++) {
         if (VALIDREG(ij_regid[i])) {
            if (i == IJ_PERSP_CENTER_RHW)
               sysval_regs += 1;
            else
               sysval_regs += 2;
         }
      }

      for (uint32_t sysval : { face_regid, samp_id_regid, smask_in_regid }) {
         if (VALIDREG(sysval))
            sysval_regs += 1;
      }

      for (uint32_t sysval : { coord_regid, zwcoord_regid }) {
         if (VALIDREG(sysval))
            sysval_regs += 2;
      }

      crb.add(SP_PS_CNTL_1(CHIP,
         .sysval_regs_count = sysval_regs,
         .defer_wave_alloc_dis = true,
         .evict_buf_mode = 1,
      ));
   }

   enum a6xx_threadsize thrsz = fs->info.double_threadsize ? THREAD128 : THREAD64;
   crb.add(SP_PS_WAVE_CNTL(CHIP,
      .threadsize = thrsz,
      .varyings = enable_varyings,
   ));

   bool need_size = fs->frag_face || fs->fragcoord_compmask != 0;
   bool need_size_persamp = false;
   if (VALIDREG(ij_regid[IJ_PERSP_CENTER_RHW])) {
      if (sample_shading)
         need_size_persamp = true;
      else
         need_size = true;
   }

   crb.add(GRAS_CL_INTERP_CNTL(CHIP,
      .ij_persp_pixel        = VALIDREG(ij_regid[IJ_PERSP_PIXEL]),
      .ij_persp_centroid     = VALIDREG(ij_regid[IJ_PERSP_CENTROID]),
      .ij_persp_sample       = VALIDREG(ij_regid[IJ_PERSP_SAMPLE]),
      .ij_linear_pixel       = VALIDREG(ij_regid[IJ_LINEAR_PIXEL]) || need_size,
      .ij_linear_centroid    = VALIDREG(ij_regid[IJ_LINEAR_CENTROID]),
      .ij_linear_sample      = VALIDREG(ij_regid[IJ_LINEAR_SAMPLE]) || need_size_persamp,
      .coord_mask            = fs->fragcoord_compmask,
   ));
   crb.add(A6XX_RB_INTERP_CNTL(
      .ij_persp_pixel        = VALIDREG(ij_regid[IJ_PERSP_PIXEL]),
      .ij_persp_centroid     = VALIDREG(ij_regid[IJ_PERSP_CENTROID]),
      .ij_persp_sample       = VALIDREG(ij_regid[IJ_PERSP_SAMPLE]),
      .ij_linear_pixel       = VALIDREG(ij_regid[IJ_LINEAR_PIXEL]) || need_size,
      .ij_linear_centroid    = VALIDREG(ij_regid[IJ_LINEAR_CENTROID]),
      .ij_linear_sample      = VALIDREG(ij_regid[IJ_LINEAR_SAMPLE]) || need_size_persamp,
      .coord_mask            = fs->fragcoord_compmask,
      .interp_en             = enable_varyings,
   ));
   crb.add(A6XX_RB_PS_INPUT_CNTL(
      .samplemask            = VALIDREG(smask_in_regid),
      .postdepthcoverage     = fs->post_depth_coverage,
      .faceness              = fs->frag_face,
      .sampleid              = VALIDREG(samp_id_regid),
      .fragcoordsamplemode   = sample_shading ? FRAGCOORD_SAMPLE : FRAGCOORD_CENTER,
      .centerrhw             = VALIDREG(ij_regid[IJ_PERSP_CENTER_RHW])
   ));
   crb.add(A6XX_RB_PS_SAMPLEFREQ_CNTL(sample_shading));
   crb.add(GRAS_LRZ_PS_INPUT_CNTL(CHIP,
      .sampleid              = VALIDREG(samp_id_regid),
      .fragcoordsamplemode   = sample_shading ? FRAGCOORD_SAMPLE : FRAGCOORD_CENTER,
   ));
   crb.add(A6XX_GRAS_LRZ_PS_SAMPLEFREQ_CNTL(sample_shading));
}

template<chip CHIP>
static void
emit_fs_outputs(fd_crb &crb, const struct program_builder *b)
{
   const struct ir3_shader_variant *fs = b->fs;
   uint32_t smask_regid, posz_regid, stencilref_regid;

   posz_regid      = ir3_find_output_regid(fs, FRAG_RESULT_DEPTH);
   smask_regid     = ir3_find_output_regid(fs, FRAG_RESULT_SAMPLE_MASK);
   stencilref_regid = ir3_find_output_regid(fs, FRAG_RESULT_STENCIL);

   /* we can't write gl_SampleMask for !msaa..  if b0 is zero then we
    * end up masking the single sample!!
    */
   if (!b->key->key.msaa)
      smask_regid = INVALID_REG;

   int output_reg_count = 0;
   uint32_t fragdata_regid[8];
   uint32_t fragdata_aliased_components = 0;

   for (uint32_t i = 0; i < ARRAY_SIZE(fragdata_regid); i++) {
      unsigned slot = fs->color0_mrt ? FRAG_RESULT_COLOR : FRAG_RESULT_DATA0 + i;
      int output_idx = ir3_find_output(fs, (gl_varying_slot)slot);

      if (output_idx < 0) {
         fragdata_regid[i] = INVALID_REG;
         continue;
      }

      const struct ir3_shader_output *fragdata = &fs->outputs[output_idx];
      fragdata_regid[i] = ir3_get_output_regid(fragdata);

      if (VALIDREG(fragdata_regid[i]) || fragdata->aliased_components) {
         /* An invalid reg is only allowed if all components are aliased. */
         assert(
            VALIDREG(fragdata_regid[i] || fragdata->aliased_components == 0xf));

         output_reg_count = i + 1;
         fragdata_aliased_components |= fragdata->aliased_components << (i * 4);
      }
   }

   crb.add(A6XX_SP_PS_OUTPUT_CNTL(
      .dual_color_in_enable = fs->dual_src_blend,
      .depth_regid = posz_regid,
      .sampmask_regid = smask_regid,
      .stencilref_regid = stencilref_regid,
   ));

   for (uint32_t i = 0; i < output_reg_count; i++) {
      crb.add(A6XX_SP_PS_OUTPUT_REG(i,
         .regid          = fragdata_regid[i] & ~HALF_REG_ID,
         .half_precision = fragdata_regid[i] & HALF_REG_ID,
      ));

      if (VALIDREG(fragdata_regid[i]) ||
          (fragdata_aliased_components & (0xf << (i * 4)))) {
         b->state->mrt_components |= 0xf << (i * 4);
      }
   }

   if (CHIP >= A7XX) {
      crb.add(SP_PS_OUTPUT_CONST_CNTL(CHIP, .enabled = fragdata_aliased_components != 0));
      crb.add(SP_PS_OUTPUT_CONST_MASK(CHIP, .dword = fragdata_aliased_components));
   } else {
      assert(fragdata_aliased_components == 0);
   }
}

template <chip CHIP>
static void
setup_stateobj(fd_cs &cs, const struct program_builder *b)
   assert_dt
{
   fd6_emit_shader<CHIP>(b->ctx, cs, b->vs);
   fd6_emit_shader<CHIP>(b->ctx, cs, b->hs);
   fd6_emit_shader<CHIP>(b->ctx, cs, b->ds);
   fd6_emit_shader<CHIP>(b->ctx, cs, b->gs);
   if (!b->binning_pass)
      fd6_emit_shader<CHIP>(b->ctx, cs, b->fs);

   emit_linkmap<CHIP>(cs, b);

   fd_crb crb(cs, 100);

   crb.add(PC_STEREO_RENDERING_CNTL(CHIP));

   emit_vfd_dest<CHIP>(crb, b->vs);
   emit_vpc<CHIP>(crb, b);

   emit_fs_inputs<CHIP>(crb, b);
   emit_fs_outputs<CHIP>(crb, b);

   if (b->hs) {
      uint32_t patch_control_points = b->key->patch_vertices;

      uint32_t patch_local_mem_size_16b =
         patch_control_points * b->vs->output_size / 4;

      /* Total attribute slots in HS incoming patch. */
      crb.add(PC_HS_PARAM_1(CHIP, patch_local_mem_size_16b));

      const uint32_t wavesize = 64;
      const uint32_t vs_hs_local_mem_size = 16384;

      uint32_t max_patches_per_wave;
      if (b->ctx->screen->info->props.tess_use_shared) {
         /* HS invocations for a patch are always within the same wave,
         * making barriers less expensive. VS can't have barriers so we
         * don't care about VS invocations being in the same wave.
         */
         max_patches_per_wave = wavesize / b->hs->tess.tcs_vertices_out;
      } else {
      /* VS is also in the same wave */
         max_patches_per_wave =
            wavesize / MAX2(patch_control_points,
                            b->hs->tess.tcs_vertices_out);
      }


      uint32_t patches_per_wave =
         MIN2(vs_hs_local_mem_size / (patch_local_mem_size_16b * 16),
              max_patches_per_wave);

      uint32_t wave_input_size = DIV_ROUND_UP(
         patches_per_wave * patch_local_mem_size_16b * 16, 256);

      crb.add(A6XX_SP_HS_CNTL_1(wave_input_size));

      enum a6xx_tess_output output;
      if (b->ds->tess.point_mode)
         output = TESS_POINTS;
      else if (b->ds->tess.primitive_mode == TESS_PRIMITIVE_ISOLINES)
         output = TESS_LINES;
      else if (b->ds->tess.ccw)
         output = TESS_CCW_TRIS;
      else
         output = TESS_CW_TRIS;

      crb.add(PC_DS_PARAM(CHIP,
         .spacing = fd6_gl2spacing(b->ds->tess.spacing),
         .output = output,
      ));
   }
}

template <chip CHIP>
static void emit_interp_state(fd_crb &crb, const struct fd6_program_state *state,
                              bool rasterflat, bool sprite_coord_mode,
                              uint32_t sprite_coord_enable);

template <chip CHIP>
static struct fd_ringbuffer *
create_interp_stateobj(struct fd_context *ctx, struct fd6_program_state *state)
{
   fd_crb crb(ctx->pipe, 16);

   emit_interp_state<CHIP>(crb, state, false, false, 0);

   return crb;
}

/* build the program streaming state which is not part of the pre-
 * baked stateobj because of dependency on other gl state (rasterflat
 * or sprite-coord-replacement)
 */
template <chip CHIP>
struct fd_ringbuffer *
fd6_program_interp_state(struct fd6_emit *emit)
{
   const struct fd6_program_state *state = fd6_emit_get_prog(emit);

   if (!unlikely(emit->rasterflat || emit->sprite_coord_enable)) {
      /* fastpath: */
      return fd_ringbuffer_ref(state->interp_stateobj);
   } else {
      fd_crb crb(emit->ctx->batch->submit, 16);

      emit_interp_state<CHIP>(crb, state, emit->rasterflat,
                              emit->sprite_coord_mode, emit->sprite_coord_enable);

      return crb;
   }
}
FD_GENX(fd6_program_interp_state);

template <chip CHIP>
static void
emit_interp_state(fd_crb &crb, const struct fd6_program_state *state,
                  bool rasterflat, bool sprite_coord_mode,
                  uint32_t sprite_coord_enable)
{
   const struct ir3_shader_variant *fs = state->fs;
   uint32_t vinterp[8], vpsrepl[8];

   memset(vinterp, 0, sizeof(vinterp));
   memset(vpsrepl, 0, sizeof(vpsrepl));

   for (int j = -1; (j = ir3_next_varying(fs, j)) < (int)fs->inputs_count;) {

      /* NOTE: varyings are packed, so if compmask is 0xb
       * then first, third, and fourth component occupy
       * three consecutive varying slots:
       */
      unsigned compmask = fs->inputs[j].compmask;

      uint32_t inloc = fs->inputs[j].inloc;

      bool coord_mode = sprite_coord_mode;
      if (ir3_point_sprite(fs, j, sprite_coord_enable, &coord_mode)) {
         /* mask is two 2-bit fields, where:
          *   '01' -> S
          *   '10' -> T
          *   '11' -> 1 - T  (flip mode)
          */
         unsigned mask = coord_mode ? 0b1101 : 0b1001;
         uint32_t loc = inloc;
         if (compmask & 0x1) {
            vpsrepl[loc / 16] |= ((mask >> 0) & 0x3) << ((loc % 16) * 2);
            loc++;
         }
         if (compmask & 0x2) {
            vpsrepl[loc / 16] |= ((mask >> 2) & 0x3) << ((loc % 16) * 2);
            loc++;
         }
         if (compmask & 0x4) {
            /* .z <- 0.0f */
            vinterp[loc / 16] |= INTERP_ZERO << ((loc % 16) * 2);
            loc++;
         }
         if (compmask & 0x8) {
            /* .w <- 1.0f */
            vinterp[loc / 16] |= INTERP_ONE << ((loc % 16) * 2);
            loc++;
         }
      } else if (fs->inputs[j].slot == VARYING_SLOT_LAYER ||
                 fs->inputs[j].slot == VARYING_SLOT_VIEWPORT) {
         const struct ir3_shader_variant *last_shader = fd6_last_shader(state);
         uint32_t loc = inloc;

         /* If the last geometry shader doesn't statically write these, they're
          * implicitly zero and the FS is supposed to read zero.
          */
         if (ir3_find_output(last_shader, (gl_varying_slot)fs->inputs[j].slot) < 0 &&
             (compmask & 0x1)) {
            vinterp[loc / 16] |= INTERP_ZERO << ((loc % 16) * 2);
         } else {
            vinterp[loc / 16] |= INTERP_FLAT << ((loc % 16) * 2);
         }
      } else if (fs->inputs[j].flat || (fs->inputs[j].rasterflat && rasterflat)) {
         uint32_t loc = inloc;

         for (int i = 0; i < 4; i++) {
            if (compmask & (1 << i)) {
               vinterp[loc / 16] |= INTERP_FLAT << ((loc % 16) * 2);
               loc++;
            }
         }
      }
   }

   for (int i = 0; i < 8; i++)
      crb.add(VPC_VARYING_INTERP_MODE_MODE(CHIP, i, vinterp[i]));

   for (int i = 0; i < 8; i++)
      crb.add(VPC_VARYING_REPLACE_MODE_MODE(CHIP, i, vpsrepl[i]));
}

template <chip CHIP>
static struct ir3_program_state *
fd6_program_create(void *data, const struct ir3_shader_variant *bs,
                   const struct ir3_shader_variant *vs,
                   const struct ir3_shader_variant *hs,
                   const struct ir3_shader_variant *ds,
                   const struct ir3_shader_variant *gs,
                   const struct ir3_shader_variant *fs,
                   const struct ir3_cache_key *key) in_dt
{
   struct fd_context *ctx = fd_context((struct pipe_context *)data);
   struct fd_screen *screen = ctx->screen;
   struct fd6_program_state *state = CALLOC_STRUCT(fd6_program_state);

   tc_assert_driver_thread(ctx->tc);

   /* if we have streamout, use full VS in binning pass, as the
    * binning pass VS will have outputs on other than position/psize
    * stripped out:
    */
   state->bs = vs->stream_output.num_outputs ? vs : bs;
   state->vs = vs;
   state->hs = hs;
   state->ds = ds;
   state->gs = gs;
   state->fs = fs;
   state->binning_stateobj = fd_ringbuffer_new_object(ctx->pipe, 0x1000);
   state->stateobj = fd_ringbuffer_new_object(ctx->pipe, 0x1000);

   if (hs) {
      /* Allocate the fixed-size tess factor BO globally on the screen.  This
       * lets the program (which ideally we would have shared across contexts,
       * though the current ir3_cache impl doesn't do that) bake in the
       * addresses.
       */
      fd_screen_lock(screen);
      if (!screen->tess_bo)
         screen->tess_bo =
            fd_bo_new(screen->dev, FD6_TESS<CHIP>::BO_SIZE, FD_BO_NOMAP, "tessfactor");
      fd_screen_unlock(screen);
   }

   /* Dummy frag shader used for binning pass: */
   static const struct ir3_shader_variant dummy_fs = {
         .info = {
               .max_reg = -1,
               .max_half_reg = -1,
               .max_const = -1,
         },
   };
   /* The last geometry stage in use: */
   const struct ir3_shader_variant *last_shader = fd6_last_shader(state);

   setup_config_stateobj<CHIP>(ctx, state);

   struct program_builder b = {
      .state = state,
      .ctx = ctx,
      .key = key,
      .hs  = state->hs,
      .ds  = state->ds,
      .gs  = state->gs,
   };

   /*
    * Setup binning pass program state:
    */

   /* binning VS is wrong when GS is present, so use nonbinning VS
    * TODO: compile both binning VS/GS variants correctly
    *
    * If we have stream-out, we use the full shader for binning
    * pass, rather than the optimized binning pass one, so that we
    * have all the varying outputs available for xfb.  So streamout
    * state should always be derived from the non-binning pass
    * program.
    */
   b.vs  = state->gs || last_shader->stream_output.num_outputs ?
           state->vs : state->bs;
   b.fs  = &dummy_fs;
   b.last_shader  = last_shader->type != MESA_SHADER_VERTEX ?
                    last_shader : state->bs;
   b.binning_pass = true;

   fd_cs binning_cs(state->binning_stateobj);
   setup_stateobj<CHIP>(binning_cs, &b);

   /*
    * Setup draw pass program state:
    */
   b.vs = state->vs;
   b.fs = state->fs;
   b.last_shader = last_shader;
   b.binning_pass = false;

   fd_cs cs(state->stateobj);
   setup_stateobj<CHIP>(cs, &b);

   state->interp_stateobj = create_interp_stateobj<CHIP>(ctx, state);

   const struct ir3_stream_output_info *stream_output = &last_shader->stream_output;
   if (stream_output->num_outputs > 0)
      state->stream_output = stream_output;

   bool has_viewport =
      VALIDREG(ir3_find_output_regid(last_shader, VARYING_SLOT_VIEWPORT));
   state->num_viewports = has_viewport ? PIPE_MAX_VIEWPORTS : 1;

   /* Note that binning pass uses same const state as draw pass: */
   state->user_consts_cmdstream_size =
         fd6_user_consts_cmdstream_size<CHIP>(state->vs) +
         fd6_user_consts_cmdstream_size<CHIP>(state->hs) +
         fd6_user_consts_cmdstream_size<CHIP>(state->ds) +
         fd6_user_consts_cmdstream_size<CHIP>(state->gs) +
         fd6_user_consts_cmdstream_size<CHIP>(state->fs);

   unsigned num_dp = 0;
   unsigned num_ubo_dp = 0;

   if (vs->need_driver_params)
      num_dp++;

   if (gs && gs->need_driver_params)
      num_ubo_dp++;
   if (hs && hs->need_driver_params)
      num_ubo_dp++;
   if (ds && ds->need_driver_params)
      num_ubo_dp++;

   if (!fd6_load_inline_uniforms_via_preamble_ldgk<CHIP>(vs)) {
      /* On a6xx all shader stages use driver params pushed in cmdstream: */
      num_dp += num_ubo_dp;
      num_ubo_dp = 0;
   }

   state->num_driver_params = num_dp;
   state->num_ubo_driver_params = num_ubo_dp;

   /* dual source blending has an extra fs output in the 2nd slot */
   if (fs->fs.color_is_dual_source) {
      state->mrt_components |= 0xf << 4;
   }

   state->lrz_mask.val = ~0;

   if (fs->has_kill) {
      state->lrz_mask.write = false;
   }

   if (fs->no_earlyz || fs->writes_pos) {
      state->lrz_mask.enable = false;
      state->lrz_mask.write = false;
      state->lrz_mask.test = false;
   }

   if (fs->fs.early_fragment_tests) {
      state->lrz_mask.z_mode = A6XX_EARLY_Z;
   } else if (fs->no_earlyz || fs->writes_pos || fs->writes_stencilref) {
      state->lrz_mask.z_mode = A6XX_LATE_Z;
   } else {
      /* Wildcard indicates that we need to figure out at draw time: */
      state->lrz_mask.z_mode = A6XX_INVALID_ZTEST;
   }

   return &state->base;
}

static void
fd6_program_destroy(void *data, struct ir3_program_state *state)
{
   struct fd6_program_state *so = fd6_program_state(state);
   fd_ringbuffer_del(so->stateobj);
   fd_ringbuffer_del(so->binning_stateobj);
   fd_ringbuffer_del(so->config_stateobj);
   fd_ringbuffer_del(so->interp_stateobj);
   if (so->streamout_stateobj)
      fd_ringbuffer_del(so->streamout_stateobj);
   free(so);
}

template <chip CHIP>
static const struct ir3_cache_funcs cache_funcs = {
   .create_state = fd6_program_create<CHIP>,
   .destroy_state = fd6_program_destroy,
};

template <chip CHIP>
void
fd6_prog_init(struct pipe_context *pctx)
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->shader_cache = ir3_cache_create(&cache_funcs<CHIP>, ctx);

   ir3_prog_init(pctx);

   fd_prog_init(pctx);
}
FD_GENX(fd6_prog_init);
