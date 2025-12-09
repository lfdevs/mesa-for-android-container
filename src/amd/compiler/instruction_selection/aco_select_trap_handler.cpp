/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_instruction_selection.h"
#include "aco_ir.h"

#include "amdgfxregs.h"

namespace aco {
namespace {

void
dump_sgpr_to_mem(isel_context* ctx, Operand rsrc, Operand data, uint32_t offset)
{
   Builder bld(ctx->program, ctx->block);

   ac_hw_cache_flags cache_glc;
   cache_glc.value = ac_glc;

   if (ctx->program->gfx_level >= GFX9) {
      bld.copy(Definition(PhysReg{256}, v1) /* v0 */, data);

      bld.mubuf(aco_opcode::buffer_store_dword, Operand(rsrc), Operand(v1), Operand::c32(0u),
                Operand(PhysReg{256}, v1) /* v0 */, offset, false /* offen */, false /* idxen */,
                /* addr64 */ false, /* disable_wqm */ false, cache_glc);
   } else {
      bld.smem(aco_opcode::s_buffer_store_dword, Operand(rsrc), Operand::c32(offset), data,
               memory_sync_info(), cache_glc);
   }
}

void
enable_thread_indexing(isel_context* ctx, Operand rsrc)
{
   Builder bld(ctx->program, ctx->block);
   PhysReg rsrc_word3(rsrc.physReg() + 3);

   bld.sop2(aco_opcode::s_or_b32, Definition(rsrc_word3, s1), bld.def(s1, scc),
            Operand(rsrc_word3, s1), Operand::c32(S_008F0C_ADD_TID_ENABLE(1)));
   if (ctx->program->gfx_level < GFX10) {
      /* This is part of the stride if ADD_TID_ENABLE=1. */
      bld.sop2(aco_opcode::s_and_b32, Definition(rsrc_word3, s1), bld.def(s1, scc),
               Operand(rsrc_word3, s1), Operand::c32(C_008F0C_DATA_FORMAT));
   }
}

void
disable_thread_indexing(isel_context* ctx, Operand rsrc)
{
   Builder bld(ctx->program, ctx->block);
   PhysReg rsrc_word3(rsrc.physReg() + 3);

   bld.sop2(aco_opcode::s_and_b32, Definition(rsrc_word3, s1), bld.def(s1, scc),
            Operand(rsrc_word3, s1), Operand::c32(C_008F0C_ADD_TID_ENABLE));
   if (ctx->program->gfx_level < GFX10) {
      bld.sop2(aco_opcode::s_or_b32, Definition(rsrc_word3, s1), bld.def(s1, scc),
               Operand(rsrc_word3, s1),
               Operand::c32(S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32)));
   }
}

void
save_or_restore_vgprs(isel_context* ctx, Operand rsrc, bool save)
{
   Builder bld(ctx->program, ctx->block);
   uint32_t offset = offsetof(struct aco_trap_handler_layout, saved_vgprs[0]);

   ac_hw_cache_flags cache_glc;
   cache_glc.value = ac_glc;

   enable_thread_indexing(ctx, rsrc);

   for (uint32_t i = 0; i < NUM_SAVED_VGPRS; i++) {
      if (save) {
         bld.mubuf(aco_opcode::buffer_store_dword, Operand(rsrc), Operand(v1), Operand::c32(0u),
                   Operand(PhysReg{256 + i}, v1) /* v0 */, offset, false /* offen */,
                   false /* idxen */,
                   /* addr64 */ false, /* disable_wqm */ false, cache_glc);
      } else {
         bld.mubuf(aco_opcode::buffer_load_dword, Definition(PhysReg{256 + i}, v1), Operand(rsrc),
                   Operand(v1), Operand::c32(0u), offset, false /* offen */, false /* idxen */,
                   /* addr64 */ false, /* disable_wqm */ false, cache_glc);
      }

      offset += 256;
   }

   disable_thread_indexing(ctx, rsrc);
}

void
save_vgprs_to_mem(isel_context* ctx, Operand rsrc)
{
   save_or_restore_vgprs(ctx, rsrc, true);
}

void
restore_vgprs_from_mem(isel_context* ctx, Operand rsrc)
{
   save_or_restore_vgprs(ctx, rsrc, false);
}

void
dump_vgprs_to_mem(isel_context* ctx, Builder& bld, Operand rsrc)
{
   const uint32_t ttmp0_idx = ctx->program->gfx_level >= GFX9 ? 108 : 112;
   const uint32_t base_offset = offsetof(struct aco_trap_handler_layout, vgprs[0]);

   ac_hw_cache_flags cache_glc;
   cache_glc.value = ac_glc;

   PhysReg num_vgprs{ttmp0_idx + 2};
   PhysReg soffset{ttmp0_idx + 3};

   enable_thread_indexing(ctx, rsrc);

   /* Determine the number of vgprs to dump in a 4-VGPR granularity. */
   const uint32_t vgpr_size_offset = ctx->program->gfx_level >= GFX11 ? 12 : 8;
   const uint32_t vgpr_size_width = ctx->program->gfx_level >= GFX10 ? 8 : 6;

   bld.sopk(aco_opcode::s_getreg_b32, Definition(num_vgprs, s1),
            ((32 - 1) << 11) | 5 /* GPR_ALLOC */);
   bld.sop2(aco_opcode::s_bfe_u32, Definition(num_vgprs, s1), bld.def(s1, scc),
            Operand(num_vgprs, s1), Operand::c32((vgpr_size_width << 16) | vgpr_size_offset));
   bld.sop2(aco_opcode::s_add_u32, Definition(num_vgprs, s1), bld.def(s1, scc),
            Operand(num_vgprs, s1), Operand::c32(1u));
   bld.sop2(aco_opcode::s_lshl_b32, Definition(num_vgprs, s1), bld.def(s1, scc),
            Operand(num_vgprs, s1), Operand::c32(2u));
   bld.sop2(aco_opcode::s_mul_i32, Definition(num_vgprs, s1), Operand::c32(256),
            Operand(num_vgprs, s1));

   /* Initialize m0/soffset to zero. */
   bld.copy(Definition(m0, s1), Operand::c32(0u));
   bld.copy(Definition(soffset, s1), Operand::c32(0u));

   if (ctx->program->gfx_level < GFX10) {
      /* Enable VGPR indexing with m0 as source index. */
      bld.sopc(aco_opcode::s_set_gpr_idx_on, Definition(m0, s1), Operand(m0, s1),
               Operand(PhysReg{1}, s1) /* SRC0 mode */);
   }

   loop_context lc;
   begin_loop(ctx, &lc);
   {
      bld.reset(ctx->block);

      /* Move from a relative source addr (v0 = v[0 + m0]). */
      if (ctx->program->gfx_level >= GFX10) {
         bld.vop1(aco_opcode::v_movrels_b32, Definition(PhysReg{256}, v1),
                  Operand(PhysReg{256}, v1), Operand(m0, s1));
      } else {
         bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{256}, v1), Operand(PhysReg{256}, v1));
      }

      bld.mubuf(aco_opcode::buffer_store_dword, Operand(rsrc), Operand(v1),
                Operand(PhysReg{soffset}, s1), Operand(PhysReg{256}, v1) /* v0 */, base_offset,
                false /* offen */, false /* idxen */,
                /* addr64 */ false, /* disable_wqm */ false, cache_glc);

      /* Increase m0 and the offset assuming it's wave64. */
      bld.sop2(aco_opcode::s_add_u32, Definition(m0, s1), bld.def(s1, scc), Operand(m0, s1),
               Operand::c32(1u));
      bld.sop2(aco_opcode::s_add_u32, Definition(soffset, s1), bld.def(s1, scc),
               Operand(soffset, s1), Operand::c32(256u));

      const Temp cond = bld.sopc(aco_opcode::s_cmp_ge_u32, bld.def(s1, scc), Operand(soffset, s1),
                                 Operand(num_vgprs, s1));

      if_context loop_break;
      begin_uniform_if_then(ctx, &loop_break, cond);
      {
         emit_loop_break(ctx);
      }
      begin_uniform_if_else(ctx, &loop_break);
      end_uniform_if(ctx, &loop_break);
   }
   end_loop(ctx, &lc);
   bld.reset(ctx->block);

   if (ctx->program->gfx_level < GFX10) {
      /* Disable VGPR indexing. */
      bld.sopp(aco_opcode::s_set_gpr_idx_off);
   }

   disable_thread_indexing(ctx, rsrc);
}

void
dump_lds_to_mem(isel_context* ctx, Builder& bld, Operand rsrc)
{
   const uint32_t ttmp0_idx = ctx->program->gfx_level >= GFX9 ? 108 : 112;
   const uint32_t base_offset = offsetof(struct aco_trap_handler_layout, lds[0]);

   ac_hw_cache_flags cache_glc;
   cache_glc.value = ac_glc;

   PhysReg lds_size{ttmp0_idx + 2};
   PhysReg soffset{ttmp0_idx + 3};

   enable_thread_indexing(ctx, rsrc);

   /* Determine the LDS size. */
   const uint32_t lds_size_offset = 12;
   const uint32_t lds_size_width = 9;

   bld.sopk(aco_opcode::s_getreg_b32, Definition(lds_size, s1),
            ((lds_size_width - 1) << 11) | (lds_size_offset << 6) | 6 /* LDS_ALLOC */);
   Temp lds_size_non_zero =
      bld.sopc(aco_opcode::s_cmp_lg_i32, bld.def(s1, scc), Operand(lds_size, s1), Operand::c32(0));

   if_context ic;
   begin_uniform_if_then(ctx, &ic, lds_size_non_zero);
   {
      bld.reset(ctx->block);

      /* Wait for other waves in the same threadgroup. */
      bld.sopp(aco_opcode::s_barrier, 0u);

      /* Compute the LDS size in bytes (64 dw * 4). */
      bld.sop2(aco_opcode::s_lshl_b32, Definition(lds_size, s1), bld.def(s1, scc),
               Operand(lds_size, s1), Operand::c32(8u));

      /* Add the base offset because this is used to exit the loop. */
      bld.sop2(aco_opcode::s_add_u32, Definition(lds_size, s1), bld.def(s1, scc),
               Operand(lds_size, s1), Operand::c32(base_offset));

      /* Initialize soffset to base offset. */
      bld.copy(Definition(soffset, s1), Operand::c32(base_offset));

      /* Compute the LDS offset from the thread ID. */
      bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, Definition(PhysReg{256}, v1), Operand::c32(-1u),
               Operand::c32(0u));
      bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32_e64, Definition(PhysReg{256}, v1), Operand::c32(-1u),
               Operand(PhysReg{256}, v1));
      bld.vop2(aco_opcode::v_mul_u32_u24, Definition(PhysReg{256}, v1), Operand::c32(4u),
               Operand(PhysReg{256}, v1));

      Operand m = load_lds_size_m0(bld);

      loop_context lc;
      begin_loop(ctx, &lc);
      {
         bld.reset(ctx->block);

         if (ctx->program->gfx_level >= GFX9) {
            bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg{257}, v1), Operand(PhysReg{256}, v1),
                   0);
         } else {
            bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg{257}, v1), Operand(PhysReg{256}, v1),
                   m, 0);
         }

         bld.mubuf(aco_opcode::buffer_store_dword, Operand(rsrc), Operand(v1),
                   Operand(PhysReg{soffset}, s1), Operand(PhysReg{257}, v1) /* v0 */,
                   0 /* offset */, false /* offen */, false /* idxen */,
                   /* addr64 */ false, /* disable_wqm */ false, cache_glc);

         /* Increase v0 and the offset assuming it's wave64. */
         bld.vop3(aco_opcode::v_mad_u32_u24, Definition(PhysReg{256}, v1), Operand::c32(4u),
                  Operand::c32(64u), Operand(PhysReg{256}, v1));
         bld.sop2(aco_opcode::s_add_u32, Definition(soffset, s1), bld.def(s1, scc),
                  Operand(soffset, s1), Operand::c32(256u));

         const Temp cond = bld.sopc(aco_opcode::s_cmp_ge_u32, bld.def(s1, scc),
                                    Operand(soffset, s1), Operand(lds_size, s1));

         if_context loop_break;
         begin_uniform_if_then(ctx, &loop_break, cond);
         {
            emit_loop_break(ctx);
         }
         begin_uniform_if_else(ctx, &loop_break);
         end_uniform_if(ctx, &loop_break);
      }
      end_loop(ctx, &lc);
      bld.reset(ctx->block);
   }
   begin_uniform_if_else(ctx, &ic);
   end_uniform_if(ctx, &ic);
   bld.reset(ctx->block);

   disable_thread_indexing(ctx, rsrc);
}

} // namespace

void
select_trap_handler_shader(Program* program, ac_shader_config* config,
                           const struct aco_compiler_options* options,
                           const struct aco_shader_info* info, const struct ac_shader_args* args)
{
   uint32_t offset = 0;

   assert(options->gfx_level >= GFX8 && options->gfx_level <= GFX12);

   init_program(program, compute_cs, info, options->gfx_level, options->family, options->wgp_mode,
                config);

   isel_context ctx = {};
   ctx.program = program;
   ctx.args = args;
   ctx.options = options;
   ctx.stage = program->stage;

   ctx.block = ctx.program->create_and_insert_block();
   ctx.block->kind = block_kind_top_level;

   program->workgroup_size = 1; /* XXX */

   add_startpgm(&ctx);
   append_logical_start(ctx.block);

   Builder bld(ctx.program, ctx.block);

   ac_hw_cache_flags cache_glc;
   cache_glc.value = ac_glc;

   const uint32_t ttmp0_idx = ctx.program->gfx_level >= GFX9 ? 108 : 112;
   PhysReg ttmp0_reg{ttmp0_idx};
   PhysReg ttmp1_reg{ttmp0_idx + 1};
   PhysReg ttmp2_reg{ttmp0_idx + 2};
   PhysReg ttmp3_reg{ttmp0_idx + 3};
   PhysReg tma_rsrc{ttmp0_idx + 4};             /* s4 */
   PhysReg save_wave_status{ttmp0_idx + 8};     /* GFX8-GFX11.5 */
   PhysReg save_wave_state_priv{ttmp0_idx + 8}; /* GFX12+ */
   PhysReg save_m0{ttmp0_idx + 9};
   PhysReg save_exec{ttmp0_idx + 10}; /* s2 */

   if (options->gfx_level >= GFX12) {
      /* Save SQ_WAVE_STATE_PRIV because SCC needs to be restored. */
      bld.sopk(aco_opcode::s_getreg_b32, Definition(save_wave_state_priv, s1),
               ((32 - 1) << 11) | 4);
   } else {
      /* Save SQ_WAVE_STATUS because SCC needs to be restored. */
      bld.sopk(aco_opcode::s_getreg_b32, Definition(save_wave_status, s1), ((32 - 1) << 11) | 2);
   }

   /* Save m0. */
   bld.copy(Definition(save_m0, s1), Operand(m0, s1));

   /* Save exec and use all invocations from the wave. */
   bld.sop1(Builder::s_or_saveexec, Definition(save_exec, bld.lm), Definition(scc, s1),
            Definition(exec, bld.lm), Operand::c32_or_c64(-1u, bld.lm == s2),
            Operand(exec, bld.lm));

   if (options->gfx_level < GFX11) {
      /* Clear the current wave exception, this is required to re-enable VALU
       * instructions in this wave. Seems to be only needed for float exceptions.
       */
      bld.vop1(aco_opcode::v_clrexcp);
   }

   offset = offsetof(struct aco_trap_handler_layout, ttmp0);

   if (ctx.program->gfx_level >= GFX9) {
      /* Get TMA. */
      if (ctx.program->gfx_level >= GFX11) {
         bld.sop1(aco_opcode::s_sendmsg_rtn_b32, Definition(ttmp2_reg, s1),
                  Operand::c32(sendmsg_rtn_get_tma));
      } else {
         bld.sopk(aco_opcode::s_getreg_b32, Definition(ttmp2_reg, s1), ((32 - 1) << 11) | 18);
      }

      bld.sop2(aco_opcode::s_lshl_b32, Definition(ttmp2_reg, s1), Definition(scc, s1),
               Operand(ttmp2_reg, s1), Operand::c32(8u));
      bld.copy(Definition(ttmp3_reg, s1), Operand::c32((unsigned)ctx.options->address32_hi));

      /* Load the buffer descriptor from TMA. */
      bld.smem(aco_opcode::s_load_dwordx4, Definition(tma_rsrc, s4), Operand(ttmp2_reg, s2),
               Operand::c32(0u));

      /* Save VGPRS that needs to be restored. */
      save_vgprs_to_mem(&ctx, Operand(tma_rsrc, s4));

      /* Dump VGPRs. */
      dump_vgprs_to_mem(&ctx, bld, Operand(tma_rsrc, s4));

      /* Store TTMP0-TTMP1. */
      bld.copy(Definition(PhysReg{256}, v2) /* v[0-1] */, Operand(ttmp0_reg, s2));

      bld.mubuf(aco_opcode::buffer_store_dwordx2, Operand(tma_rsrc, s4), Operand(v1),
                Operand::c32(0u), Operand(PhysReg{256}, v2) /* v[0-1] */, offset /* offset */,
                false /* offen */, false /* idxen */, /* addr64 */ false,
                /* disable_wqm */ false, cache_glc);
   } else {
      /* Load the buffer descriptor from TMA. */
      bld.smem(aco_opcode::s_load_dwordx4, Definition(tma_rsrc, s4), Operand(PhysReg{tma_lo}, s2),
               Operand::zero());

      /* Save VGPRS that needs to be restored. */
      save_vgprs_to_mem(&ctx, Operand(tma_rsrc, s4));

      /* Dump VGPRs. */
      dump_vgprs_to_mem(&ctx, bld, Operand(tma_rsrc, s4));

      /* Store TTMP0-TTMP1. */
      bld.smem(aco_opcode::s_buffer_store_dwordx2, Operand(tma_rsrc, s4), Operand::c32(offset),
               Operand(ttmp0_reg, s2), memory_sync_info(), cache_glc);
   }

   /* Store some hardware registers. */
   if (options->gfx_level >= GFX12) {
      const uint32_t hw_regs_idx[] = {
         1,  /* HW_REG_MODE */
         2,  /* HW_REG_STATUS */
         5,  /* WH_REG_GPR_ALLOC */
         6,  /* WH_REG_LDS_ALLOC */
         7,  /* HW_REG_IB_STS */
         17, /* HW_REG_EXCP_FLAG_PRIV */
         18, /* HW_REG_EXCP_FLAG_USER */
         19, /* HW_REG_TRAP_CTRL */
         23, /* HW_REG_HW_ID */
      };

      offset = offsetof(struct aco_trap_handler_layout, sq_wave_regs.gfx12.state_priv);

      /* Store saved SQ_WAVE_STATE_PRIV which can change inside the trap. */
      dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(save_wave_state_priv, s1), offset);
      offset += 4;

      for (unsigned i = 0; i < ARRAY_SIZE(hw_regs_idx); i++) {
         /* "((size - 1) << 11) | register" */
         bld.sopk(aco_opcode::s_getreg_b32, Definition(ttmp0_reg, s1),
                  ((32 - 1) << 11) | hw_regs_idx[i]);

         dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(ttmp0_reg, s1), offset);
         offset += 4;
      }
   } else {
      const uint32_t hw_regs_idx[] = {
         1, /* HW_REG_MODE */
         3, /* HW_REG_TRAP_STS */
         4, /* HW_REG_HW_ID */
         5, /* WH_REG_GPR_ALLOC */
         6, /* WH_REG_LDS_ALLOC */
         7, /* HW_REG_IB_STS */
      };

      offset = offsetof(struct aco_trap_handler_layout, sq_wave_regs.gfx8.status);

      /* Store saved SQ_WAVE_STATUS which can change inside the trap. */
      dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(save_wave_status, s1), offset);
      offset += 4;

      for (unsigned i = 0; i < ARRAY_SIZE(hw_regs_idx); i++) {
         /* "((size - 1) << 11) | register" */
         bld.sopk(aco_opcode::s_getreg_b32, Definition(ttmp0_reg, s1),
                  ((32 - 1) << 11) | hw_regs_idx[i]);

         dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(ttmp0_reg, s1), offset);
         offset += 4;
      }

      /* Skip space "reserved regs". */
      offset += 12;
   }

   assert(offset == offsetof(struct aco_trap_handler_layout, m0));

   /* Dump shader registers (m0, exec). */
   dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(save_m0, s1), offset);
   offset += 4;
   dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(save_exec, s1), offset);
   offset += 4;
   dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(save_exec.advance(4), s1), offset);
   offset += 4;

   assert(offset == offsetof(struct aco_trap_handler_layout, sgprs[0]));

   /* Dump all SGPRs. */
   for (uint32_t i = 0; i < program->dev.sgpr_limit; i++) {
      dump_sgpr_to_mem(&ctx, Operand(tma_rsrc, s4), Operand(PhysReg{i}, s1), offset);
      offset += 4;
   }

   /* Dump LDS. */
   dump_lds_to_mem(&ctx, bld, Operand(tma_rsrc, s4));

   /* Restore VGPRS. */
   restore_vgprs_from_mem(&ctx, Operand(tma_rsrc, s4));

   /* Restore m0 and exec. */
   bld.copy(Definition(m0, s1), Operand(save_m0, s1));
   bld.copy(Definition(exec, bld.lm), Operand(save_exec, bld.lm));

   if (options->gfx_level >= GFX12) {
      /* Restore SCC which is the bit 9 of SQ_WAVE_STATE_PRIV. */
      bld.sopc(aco_opcode::s_bitcmp1_b32, bld.def(s1, scc), Operand(save_wave_state_priv, s1),
               Operand::c32(9u));
   } else {
      /* Restore SCC which is the first bit of SQ_WAVE_STATUS. */
      bld.sopc(aco_opcode::s_bitcmp1_b32, bld.def(s1, scc), Operand(save_wave_status, s1),
               Operand::c32(0u));
   }

   program->config->float_mode = program->blocks[0].fp_mode.val;

   append_logical_end(&ctx);
   ctx.block->kind |= block_kind_uniform;
   bld.sopp(aco_opcode::s_endpgm);

   finish_program(&ctx);
}

} // namespace aco
