/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include <vector>

namespace aco {

namespace {

enum mode_field : uint8_t {
   mode_round32 = 0,
   mode_round16_64,
   mode_denorm32,
   mode_denorm16_64,
   mode_fp16_ovfl,

   mode_field_count,
};

using mode_mask = uint8_t;
static_assert(mode_field_count <= sizeof(mode_mask) * 8, "larger mode_mask needed");

struct fp_mode_state {
   uint8_t fields[mode_field_count] = {};
   mode_mask required = 0; /* BITFIELD_BIT(enum mode_field) */

   fp_mode_state() = default;

   fp_mode_state(float_mode mode)
   {
      fields[mode_round32] = mode.round32;
      fields[mode_round16_64] = mode.round16_64;
      fields[mode_denorm32] = mode.denorm32;
      fields[mode_denorm16_64] = mode.denorm16_64;
      fields[mode_fp16_ovfl] = 0;
   }

   /* Returns a mask of fields that cannot be joined. */
   mode_mask join(const fp_mode_state& other)
   {
      const std::array<mode_mask, 3> part_masks = {
         BITFIELD_BIT(mode_round32) | BITFIELD_BIT(mode_round16_64),
         BITFIELD_BIT(mode_denorm32) | BITFIELD_BIT(mode_denorm16_64),
         BITFIELD_BIT(mode_fp16_ovfl),
      };

      mode_mask result = 0;
      for (mode_mask part : part_masks) {
         bool can_join = true;
         u_foreach_bit (i, required & other.required & part) {
            if (fields[i] != other.fields[i])
               can_join = false;
         }

         if (!can_join) {
            result |= part;
            continue;
         }

         u_foreach_bit (i, ~required & other.required & part)
            fields[i] = other.fields[i];

         required |= other.required & part;
      }

      return result;
   }

   void require(mode_field field, uint8_t val)
   {
      fields[field] = val;
      required |= BITFIELD_BIT(field);
   }

   uint8_t round() const { return fields[mode_round32] | (fields[mode_round16_64] << 2); }

   uint8_t denorm() const { return fields[mode_denorm32] | (fields[mode_denorm16_64] << 2); }

   uint8_t round_denorm() const { return round() | (denorm() << 4); }
};

struct fp_mode_ctx {
   std::vector<fp_mode_state> block_states;

   uint32_t last_set[mode_field_count];

   Program* program;
};

void
set_mode(fp_mode_ctx* ctx, Block* block, fp_mode_state& state, unsigned idx, mode_mask mask)
{
   Builder bld(ctx->program, block);
   bld.reset(&block->instructions, block->instructions.begin() + idx);

   bool set_round = mask & (BITFIELD_BIT(mode_round32) | BITFIELD_BIT(mode_round16_64));
   bool set_denorm = mask & (BITFIELD_BIT(mode_denorm32) | BITFIELD_BIT(mode_denorm16_64));
   bool set_fp16_ovfl = mask & BITFIELD_BIT(mode_fp16_ovfl);

   if (bld.program->gfx_level >= GFX10) {
      if (set_round) {
         bld.sopp(aco_opcode::s_round_mode, state.round());
         mask |= BITFIELD_BIT(mode_round32) | BITFIELD_BIT(mode_round16_64);
      }
      if (set_denorm) {
         bld.sopp(aco_opcode::s_denorm_mode, state.denorm());
         mask |= BITFIELD_BIT(mode_denorm32) | BITFIELD_BIT(mode_denorm16_64);
      }
   } else if (set_round || set_denorm) {
      /* "((size - 1) << 11) | register" (MODE is encoded as register 1) */
      uint8_t val = state.round_denorm();
      bld.sopk(aco_opcode::s_setreg_imm32_b32, Operand::literal32(val), (7 << 11) | 1);

      mask |= BITFIELD_BIT(mode_round32) | BITFIELD_BIT(mode_round16_64);
      mask |= BITFIELD_BIT(mode_denorm32) | BITFIELD_BIT(mode_denorm16_64);
   }

   if (set_fp16_ovfl) {
      /* "((size - 1) << 11 | (offset << 6) | register" (MODE is encoded as register 1, we
       * want to set a single bit at offset 23)
       */
      bld.sopk(aco_opcode::s_setreg_imm32_b32, Operand::literal32(state.fields[mode_fp16_ovfl]),
               (0 << 11) | (23 << 6) | 1);
   }

   state.required &= ~mask;

   u_foreach_bit (i, mask)
      ctx->last_set[i] = MIN2(ctx->last_set[i], block->index);
}

mode_mask
vmem_default_needs(const Instruction* instr)
{
   switch (instr->opcode) {
   case aco_opcode::buffer_atomic_fcmpswap:
   case aco_opcode::buffer_atomic_fmin:
   case aco_opcode::buffer_atomic_fmax:
   case aco_opcode::buffer_atomic_add_f32:
   case aco_opcode::flat_atomic_fcmpswap:
   case aco_opcode::flat_atomic_fmin:
   case aco_opcode::flat_atomic_fmax:
   case aco_opcode::flat_atomic_add_f32:
   case aco_opcode::global_atomic_fcmpswap:
   case aco_opcode::global_atomic_fmin:
   case aco_opcode::global_atomic_fmax:
   case aco_opcode::global_atomic_add_f32:
   case aco_opcode::image_atomic_fcmpswap:
   case aco_opcode::image_atomic_fmin:
   case aco_opcode::image_atomic_fmax:
   case aco_opcode::image_atomic_add_flt: return BITFIELD_BIT(mode_denorm32);
   case aco_opcode::buffer_atomic_fcmpswap_x2:
   case aco_opcode::buffer_atomic_fmin_x2:
   case aco_opcode::buffer_atomic_fmax_x2:
   case aco_opcode::buffer_atomic_pk_add_f16:
   case aco_opcode::buffer_atomic_pk_add_bf16:
   case aco_opcode::flat_atomic_fcmpswap_x2:
   case aco_opcode::flat_atomic_fmin_x2:
   case aco_opcode::flat_atomic_fmax_x2:
   case aco_opcode::flat_atomic_pk_add_f16:
   case aco_opcode::flat_atomic_pk_add_bf16:
   case aco_opcode::global_atomic_fcmpswap_x2:
   case aco_opcode::global_atomic_fmin_x2:
   case aco_opcode::global_atomic_fmax_x2:
   case aco_opcode::global_atomic_pk_add_f16:
   case aco_opcode::global_atomic_pk_add_bf16:
   case aco_opcode::image_atomic_pk_add_f16:
   case aco_opcode::image_atomic_pk_add_bf16: return BITFIELD_BIT(mode_denorm16_64);
   default: return 0;
   }
}

bool
instr_ignores_round_mode(const Instruction* instr)
{
   switch (instr->opcode) {
   case aco_opcode::v_min_f64_e64:
   case aco_opcode::v_min_f64:
   case aco_opcode::v_min_f32:
   case aco_opcode::v_min_f16:
   case aco_opcode::v_max_f64_e64:
   case aco_opcode::v_max_f64:
   case aco_opcode::v_max_f32:
   case aco_opcode::v_max_f16:
   case aco_opcode::v_min3_f32:
   case aco_opcode::v_min3_f16:
   case aco_opcode::v_max3_f32:
   case aco_opcode::v_max3_f16:
   case aco_opcode::v_med3_f32:
   case aco_opcode::v_med3_f16:
   case aco_opcode::v_minmax_f32:
   case aco_opcode::v_minmax_f16:
   case aco_opcode::v_maxmin_f32:
   case aco_opcode::v_maxmin_f16:
   case aco_opcode::v_minimum_f64:
   case aco_opcode::v_minimum_f32:
   case aco_opcode::v_minimum_f16:
   case aco_opcode::v_maximum_f64:
   case aco_opcode::v_maximum_f32:
   case aco_opcode::v_maximum_f16:
   case aco_opcode::v_minimum3_f32:
   case aco_opcode::v_minimum3_f16:
   case aco_opcode::v_maximum3_f32:
   case aco_opcode::v_maximum3_f16:
   case aco_opcode::v_minimummaximum_f32:
   case aco_opcode::v_minimummaximum_f16:
   case aco_opcode::v_maximumminimum_f32:
   case aco_opcode::v_maximumminimum_f16:
   case aco_opcode::v_pk_min_f16:
   case aco_opcode::v_pk_max_f16:
   case aco_opcode::v_pk_minimum_f16:
   case aco_opcode::v_pk_maximum_f16:
   case aco_opcode::v_cvt_pkrtz_f16_f32:
   case aco_opcode::v_cvt_pkrtz_f16_f32_e64:
   case aco_opcode::v_pack_b32_f16:
   case aco_opcode::v_cvt_f32_f16:
   case aco_opcode::v_cvt_f64_f32:
   case aco_opcode::v_ceil_f64:
   case aco_opcode::v_ceil_f32:
   case aco_opcode::v_ceil_f16:
   case aco_opcode::v_trunc_f64:
   case aco_opcode::v_trunc_f32:
   case aco_opcode::v_trunc_f16:
   case aco_opcode::v_floor_f64:
   case aco_opcode::v_floor_f32:
   case aco_opcode::v_floor_f16:
   case aco_opcode::v_rndne_f64:
   case aco_opcode::v_rndne_f32:
   case aco_opcode::v_rndne_f16:
   case aco_opcode::s_min_f32:
   case aco_opcode::s_min_f16:
   case aco_opcode::s_max_f32:
   case aco_opcode::s_max_f16:
   case aco_opcode::s_minimum_f32:
   case aco_opcode::s_minimum_f16:
   case aco_opcode::s_maximum_f32:
   case aco_opcode::s_maximum_f16:
   case aco_opcode::s_cvt_pk_rtz_f16_f32:
   case aco_opcode::s_cvt_f32_f16:
   case aco_opcode::s_ceil_f32:
   case aco_opcode::s_ceil_f16:
   case aco_opcode::s_trunc_f32:
   case aco_opcode::s_trunc_f16:
   case aco_opcode::s_floor_f32:
   case aco_opcode::s_floor_f16:
   case aco_opcode::s_rndne_f32:
   case aco_opcode::s_rndne_f16: return true;
   default: return false;
   }
}

mode_mask
instr_default_needs(const fp_mode_ctx* ctx, const Instruction* instr)
{
   if ((instr->isVMEM() || instr->isFlatLike()) && ctx->program->gfx_level < GFX12)
      return vmem_default_needs(instr);

   switch (instr->opcode) {
   case aco_opcode::s_swappc_b64:
   case aco_opcode::s_setpc_b64:
   case aco_opcode::s_call_b64:
      /* Restore defaults on calls. */
      return BITFIELD_MASK(mode_field_count);
   case aco_opcode::ds_cmpst_f32:
   case aco_opcode::ds_min_f32:
   case aco_opcode::ds_max_f32:
   case aco_opcode::ds_add_f32:
   case aco_opcode::ds_min_src2_f32:
   case aco_opcode::ds_max_src2_f32:
   case aco_opcode::ds_add_src2_f32:
   case aco_opcode::ds_cmpst_rtn_f32:
   case aco_opcode::ds_min_rtn_f32:
   case aco_opcode::ds_max_rtn_f32:
   case aco_opcode::ds_add_rtn_f32: return BITFIELD_BIT(mode_denorm32);
   case aco_opcode::ds_cmpst_f64:
   case aco_opcode::ds_min_f64:
   case aco_opcode::ds_max_f64:
   case aco_opcode::ds_min_src2_f64:
   case aco_opcode::ds_max_src2_f64:
   case aco_opcode::ds_cmpst_rtn_f64:
   case aco_opcode::ds_min_rtn_f64:
   case aco_opcode::ds_max_rtn_f64:
   case aco_opcode::ds_pk_add_f16:
   case aco_opcode::ds_pk_add_rtn_f16:
   case aco_opcode::ds_pk_add_bf16:
   case aco_opcode::ds_pk_add_rtn_bf16: return BITFIELD_BIT(mode_denorm16_64);
   case aco_opcode::v_cvt_pk_u8_f32: return BITFIELD_BIT(mode_round32);
   default: break;
   }

   if (!instr->isVALU() && !instr->isSALU() && !instr->isVINTRP())
      return 0;
   if (instr->definitions.empty())
      return 0;

   const aco_alu_opcode_info& info = instr_info.alu_opcode_infos[(int)instr->opcode];

   mode_mask res = 0;

   for (unsigned i = 0; i < info.num_operands; i++) {
      aco_type type = info.op_types[i];
      if (type.base_type != aco_base_type_float && type.base_type != aco_base_type_bfloat)
         continue;

      if (type.bit_size == 32)
         res |= BITFIELD_BIT(mode_denorm32);
      else if (type.bit_size >= 16)
         res |= BITFIELD_BIT(mode_denorm16_64);
   }

   aco_type type = info.def_types[0];
   if (type.base_type == aco_base_type_float || type.base_type == aco_base_type_bfloat) {
      if (type.bit_size == 32)
         res |= BITFIELD_BIT(mode_denorm32) | BITFIELD_BIT(mode_round32);
      else if (type.bit_size >= 16)
         res |= BITFIELD_BIT(mode_denorm16_64) | BITFIELD_BIT(mode_round16_64);

      if (type.bit_size <= 16)
         res |= BITFIELD_BIT(mode_fp16_ovfl);
   }

   if (instr->opcode == aco_opcode::v_fma_mixlo_f16 || instr->opcode == aco_opcode::v_fma_mixlo_f16)
      res |= BITFIELD_BIT(mode_round32);
   else if (instr->opcode == aco_opcode::v_fma_mix_f32 && instr->valu().opsel_hi)
      res |= BITFIELD_BIT(mode_denorm16_64);

   if (instr_ignores_round_mode(instr))
      res &= ~(BITFIELD_BIT(mode_fp16_ovfl) | BITFIELD_BIT(mode_round32) |
               BITFIELD_BIT(mode_round16_64));

   return res;
}

void
emit_set_mode_block(fp_mode_ctx* ctx, Block* block)
{
   const fp_mode_state default_state(block->fp_mode);
   fp_mode_state fp_state = default_state;

   if (block->kind & block_kind_end_with_regs) {
      /* Restore default. */
      fp_state.required = BITFIELD_MASK(mode_field_count);
      assert(block->linear_succs.empty());
   } else {
      for (unsigned succ : block->linear_succs) {
         /* Skip loop headers, they are handled at the end. */
         if (succ <= block->index)
            continue;

         fp_mode_state& other = ctx->block_states[succ];
         mode_mask to_set = fp_state.join(other);

         if (to_set) {
            Block* succ_block = &ctx->program->blocks[succ];
            set_mode(ctx, succ_block, other, 0, to_set);
         }
      }
   }

   for (int idx = block->instructions.size() - 1; idx >= 0; idx--) {
      Instruction* instr = block->instructions[idx].get();

      fp_mode_state instr_state;
      if (instr->opcode == aco_opcode::p_v_cvt_f16_f32_rtne ||
          instr->opcode == aco_opcode::p_s_cvt_f16_f32_rtne) {
         instr_state.require(mode_round16_64, fp_round_ne);
         instr_state.require(mode_fp16_ovfl, default_state.fields[mode_fp16_ovfl]);
         instr_state.require(mode_denorm16_64, default_state.fields[mode_denorm16_64]);

         if (instr->opcode == aco_opcode::p_v_cvt_f16_f32_rtne)
            instr->opcode = aco_opcode::v_cvt_f16_f32;
         else
            instr->opcode = aco_opcode::s_cvt_f16_f32;
      } else if (instr->opcode ==  aco_opcode::p_v_cvt_f16_f32_rtpi ||
                 instr->opcode ==  aco_opcode::p_v_cvt_f16_f32_rtni) {
         instr_state.require(mode_round16_64, instr->opcode == aco_opcode::p_v_cvt_f16_f32_rtpi
                                                 ? fp_round_pi
                                                 : fp_round_ni);
         instr_state.require(mode_fp16_ovfl, default_state.fields[mode_fp16_ovfl]);
         instr_state.require(mode_denorm16_64, default_state.fields[mode_denorm16_64]);
         instr_state.require(mode_denorm32, default_state.fields[mode_denorm32]);

         instr->opcode = aco_opcode::v_cvt_f16_f32;
      } else if (instr->opcode == aco_opcode::p_v_cvt_pk_fp8_f32_ovfl) {
         instr_state.require(mode_fp16_ovfl, 1);
         instr->opcode = aco_opcode::v_cvt_pk_fp8_f32;
      } else if (instr->opcode == aco_opcode::p_v_fma_mixlo_f16_rtz ||
                 instr->opcode == aco_opcode::p_v_fma_mixhi_f16_rtz) {
         instr_state.require(mode_round16_64, fp_round_tz);
         instr_state.require(mode_round32, default_state.fields[mode_round32]);
         instr_state.require(mode_denorm16_64, default_state.fields[mode_denorm16_64]);
         instr_state.require(mode_denorm32, default_state.fields[mode_denorm32]);

         if (instr->opcode == aco_opcode::p_v_fma_mixlo_f16_rtz)
            instr->opcode = aco_opcode::v_fma_mixlo_f16;
         else
            instr->opcode = aco_opcode::v_fma_mixhi_f16;
      } else if (instr->opcode == aco_opcode::p_v_add_f64_rtne ||
                 instr->opcode == aco_opcode::p_v_fract_f64_rtne) {
         instr_state.require(mode_round16_64, fp_round_ne);
         instr_state.require(mode_denorm16_64, default_state.fields[mode_denorm16_64]);

         if (instr->opcode == aco_opcode::p_v_add_f64_rtne)
            instr->opcode = aco_opcode::v_add_f64_e64;
         else
            instr->opcode = aco_opcode::v_fract_f64;
      } else {
         mode_mask default_needs = instr_default_needs(ctx, instr);
         u_foreach_bit (i, default_needs)
            instr_state.require((mode_field)i, default_state.fields[i]);
      }

      mode_mask to_set = fp_state.join(instr_state);

      if (to_set) {
         /* If the mode required by the current instruction is incompatible with
          * the mode(s) required by future instructions, set the next mode after
          * the current instruction and update the required mode.
          */
         set_mode(ctx, block, fp_state, idx + 1, to_set);
         to_set = fp_state.join(instr_state);
         assert(!to_set);
      }
   }

   if (block->linear_preds.empty()) {

      if (fp_state.fields[mode_fp16_ovfl] == 0) {
         /* We always set fp16_ovfl=0 from the commmand stream */
         fp_state.required &= ~BITFIELD_BIT(mode_fp16_ovfl);
      }

      bool initial_unknown = (ctx->program->info.merged_shader_compiled_separately &&
                              ctx->program->stage.sw == SWStage::GS) ||
                             (ctx->program->info.merged_shader_compiled_separately &&
                              ctx->program->stage.sw == SWStage::TCS);

      if (ctx->program->stage == raytracing_cs || block->index) {
         /* Assume the default state is already set. */
         for (unsigned i = 0; i < mode_field_count; i++) {
            if (fp_state.fields[i] == default_state.fields[i])
               fp_state.required &= ~BITFIELD_BIT(i);
         }
      } else if (!initial_unknown) {
         /* Set what's required from the command stream. */
         ctx->program->config->float_mode = fp_state.round_denorm();
         fp_state.required &= BITFIELD_BIT(mode_fp16_ovfl);
      }

      if (fp_state.required)
         set_mode(ctx, block, fp_state, 0, fp_state.required);
   } else if (block->kind & block_kind_loop_header) {
      uint32_t max_pred = 0;
      for (uint32_t pred : block->linear_preds)
         max_pred = MAX2(max_pred, pred);

      if (max_pred >= block->index) {
         mode_mask to_set = 0;
         /* Check if the any mode was changed during the loop. */
         u_foreach_bit (i, fp_state.required) {
            if (ctx->last_set[i] <= max_pred)
               to_set |= BITFIELD_BIT(i);
         }
         if (to_set)
            set_mode(ctx, block, fp_state, 0, to_set);
      }
   }

   ctx->block_states[block->index] = fp_state;
}

} // namespace

bool
instr_is_vmem_fp_atomic(Instruction* instr)
{
   return vmem_default_needs(instr) != 0;
}

void
insert_fp_mode(Program* program)
{
   fp_mode_ctx ctx;
   ctx.program = program;
   ctx.block_states.resize(program->blocks.size());
   for (unsigned i = 0; i < mode_field_count; i++)
      ctx.last_set[i] = UINT32_MAX;

   for (int i = program->blocks.size() - 1; i >= 0; i--)
      emit_set_mode_block(&ctx, &program->blocks[i]);
}

} // namespace aco
