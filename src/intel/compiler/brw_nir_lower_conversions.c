/*
 * Copyright © 2018 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"

static nir_rounding_mode
get_opcode_rounding_mode(nir_op op, nir_alu_type dst_type,
                         unsigned execution_mode)
{
   switch (op) {
   case nir_op_f2f16_rtz:
      return nir_rounding_mode_rtz;
   case nir_op_f2f16_rtne:
      return nir_rounding_mode_rtne;
   default:
      return nir_get_rounding_mode_from_float_controls(execution_mode,
                                                       dst_type);
   }
}

static void
split_conversion(nir_builder *b, nir_alu_instr *alu, nir_alu_type src_type,
                 nir_alu_type tmp_type, nir_alu_type dst_type,
                 nir_rounding_mode rnd)
{
   b->cursor = nir_before_instr(&alu->instr);
   nir_def *src = nir_ssa_for_alu_src(b, alu, 0);
   nir_def *tmp = nir_type_convert(b, src, src_type, tmp_type, nir_rounding_mode_undef);

   if (dst_type == nir_type_float16 && rnd == nir_rounding_mode_rtne) {
      /* We round down from double to half float by going through float in
       * between, but this can give us inaccurate results in some cases. One
       * such case is 0x40ee6a0000000001, which should round to 0x7b9b, but
       * going through float first turns into 0x7b9a instead. This is because
       * the first non-fitting bit is set, so we get a tie, but with the least
       * significant bit of the original number set, the tie should break
       * rounding up. The cast to float, however, turns into 0x47735000, which
       * when going to half still ties, but now we lost the tie-up bit, and
       * instead we round to the nearest even, which in this case is down.
       *
       * To fix this, we check if the original would have tied, and if the tie
       * would have rounded up, and if both are true, set the least
       * significant bit of the intermediate float to 1, so that a tie on the
       * next cast rounds up as well. If the rounding already got rid of the
       * tie, that set bit will just be truncated anyway and the end result
       * doesn't change.
       *
       * Another failing case is 0x40effdffffffffff. This one doesn't have the
       * tie from double to half, so it just rounds down to 0x7bff (65504.0),
       * but going through float first, it turns into 0x477ff000, which does
       * have the tie bit for half set, and when that one gets rounded it
       * turns into 0x7c00 (Infinity).
       * The fix for that one is to make sure the intermediate float does not
       * have the tie bit set if the original didn't have it.
       *
       * For the RTZ case, we don't need to do anything, as the intermediate
       * float should be ok already.
       */
      int significand_bits16 = 10;
      int significand_bits32 = 23;
      int significand_bits64 = 52;
      int f64_to_16_tie_bit = significand_bits64 - significand_bits16 - 1;
      int f32_to_16_tie_bit = significand_bits32 - significand_bits16 - 1;
      uint64_t f64_rounds_up_mask = ((1ULL << f64_to_16_tie_bit) - 1);

      nir_def *would_tie = nir_iand_imm(b, src, 1ULL << f64_to_16_tie_bit);
      nir_def *would_rnd_up = nir_iand_imm(b, src, f64_rounds_up_mask);

      nir_def *tie_up = nir_b2i32(b, nir_ine_imm(b, would_rnd_up, 0));

      nir_def *break_tie = nir_bcsel(b,
                                     nir_ine_imm(b, would_tie, 0),
                                     nir_imm_int(b, ~0),
                                     nir_imm_int(b, ~(1U << f32_to_16_tie_bit)));

      tmp = nir_ior(b, tmp, tie_up);
      tmp = nir_iand(b, tmp, break_tie);
   }

   nir_def *res = nir_type_convert(b, tmp, tmp_type, dst_type, rnd);
   nir_def_rewrite_uses(&alu->def, res);
   nir_instr_remove(&alu->instr);
}

static bool
lower_alu_instr(nir_builder *b, nir_alu_instr *alu)
{
   unsigned src_bit_size = nir_src_bit_size(alu->src[0].src);
   nir_alu_type src_type = nir_op_infos[alu->op].input_types[0];
   nir_alu_type src_full_type = (nir_alu_type) (src_type | src_bit_size);

   unsigned dst_bit_size = alu->def.bit_size;
   nir_alu_type dst_full_type = nir_op_infos[alu->op].output_type;
   nir_alu_type dst_type = nir_alu_type_get_base_type(dst_full_type);

   /* BDW PRM, vol02, Command Reference Instructions, mov - MOVE:
    *
    *   "There is no direct conversion from HF to DF or DF to HF.
    *    Use two instructions and F (Float) as an intermediate type.
    *
    *    There is no direct conversion from HF to Q/UQ or Q/UQ to HF.
    *    Use two instructions and F (Float) or a word integer type
    *    or a DWord integer type as an intermediate type."
    *
    * It is important that the intermediate conversion happens through a
    * 32-bit float type so we don't lose range when we convert from
    * a 64-bit integer.
    */
   if ((src_full_type == nir_type_float16 && dst_bit_size == 64) ||
       (src_bit_size == 64 && dst_full_type == nir_type_float16)) {
      split_conversion(b, alu, src_type, nir_type_float | 32,
                       dst_type | dst_bit_size,
                       get_opcode_rounding_mode(alu->op, dst_full_type,
                                                b->shader->info.float_controls_execution_mode));
      return true;
   }

   /* SKL PRM, vol 02a, Command Reference: Instructions, Move:
    *
    *   "There is no direct conversion from B/UB to DF or DF to B/UB. Use
    *    two instructions and a word or DWord intermediate type."
    *
    *   "There is no direct conversion from B/UB to Q/UQ or Q/UQ to B/UB.
    *    Use two instructions and a word or DWord intermediate integer
    *    type."
    *
    * It is important that we use a 32-bit integer matching the sign of the
    * destination as the intermediate type so we avoid any chance of rtne
    * rounding happening before the conversion to integer (which is expected
    * to round towards zero) in double to byte conversions.
    */
   if ((src_bit_size == 8 && dst_bit_size == 64) ||
       (src_bit_size == 64 && dst_bit_size == 8)) {
      split_conversion(b, alu, src_type, dst_type | 32, dst_type | dst_bit_size,
                       nir_rounding_mode_undef);
      return true;
   }

   return false;
}

static bool
lower_instr(nir_builder *b, nir_instr *instr, UNUSED void *cb_data)
{
   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);

   if (!nir_op_infos[alu->op].is_conversion)
      return false;

   return lower_alu_instr(b, alu);
}

bool
brw_nir_lower_conversions(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader, lower_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       NULL);
}
