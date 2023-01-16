/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

/* Results of pattern matching */
struct match {
   nir_ssa_scalar base, offset;
   bool has_offset;
   bool sign_extend;

   /* Signed shift. A negative shift indicates that the offset needs ushr
    * applied. It's cheaper to fold iadd and materialize an extra ushr, than
    * to leave the iadd untouched, so this is good.
    */
   int8_t shift;
};

/* Try to pattern match address calculation */
static struct match
match_address(nir_ssa_scalar base, int8_t format_shift)
{
   struct match match = {.base = base};

   /* All address calculations are iadd at the root */
   if (!nir_ssa_scalar_is_alu(base) ||
       nir_ssa_scalar_alu_op(base) != nir_op_iadd)
      return match;

   /* Only 64+32 addition is supported, look for an extension */
   nir_ssa_scalar summands[] = {
      nir_ssa_scalar_chase_alu_src(base, 0),
      nir_ssa_scalar_chase_alu_src(base, 1),
   };

   for (unsigned i = 0; i < ARRAY_SIZE(summands); ++i) {
      if (!nir_ssa_scalar_is_alu(summands[i]))
         continue;

      nir_op op = nir_ssa_scalar_alu_op(summands[i]);

      if (op != nir_op_u2u64 && op != nir_op_i2i64)
         continue;

      match.base = summands[1 - i];
      match.offset = nir_ssa_scalar_chase_alu_src(summands[i], 0);
      match.sign_extend = (op == nir_op_i2i64);

      /* Undo the implicit shift from using as offset */
      match.shift = -format_shift;

      /* Now try to fold in an ishl from the offset */
      if (nir_ssa_scalar_is_alu(match.offset) &&
          nir_ssa_scalar_alu_op(match.offset) == nir_op_ishl) {

         nir_ssa_scalar shifted = nir_ssa_scalar_chase_alu_src(match.offset, 0);
         nir_ssa_scalar shift = nir_ssa_scalar_chase_alu_src(match.offset, 1);

         if (nir_ssa_scalar_is_const(shift)) {
            int8_t new_shift = match.shift + nir_ssa_scalar_as_uint(shift);

            /* Only fold in if we wouldn't overflow the lsl field */
            if (new_shift <= 2) {
               match.offset = shifted;
               match.shift = new_shift;
            }
         }
      }
   }

   return match;
}

static bool
pass(struct nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_global &&
       intr->intrinsic != nir_intrinsic_load_global_constant &&
       intr->intrinsic != nir_intrinsic_store_global)
      return false;

   b->cursor = nir_before_instr(instr);

   unsigned bitsize = intr->intrinsic == nir_intrinsic_store_global
                         ? nir_src_bit_size(intr->src[0])
                         : nir_dest_bit_size(intr->dest);

   /* TODO: Handle more sizes */
   assert(bitsize == 16 || bitsize == 32);
   enum pipe_format format =
      bitsize == 32 ? PIPE_FORMAT_R32_UINT : PIPE_FORMAT_R16_UINT;

   unsigned format_shift = util_logbase2(util_format_get_blocksize(format));

   nir_src *orig_offset = nir_get_io_offset_src(intr);
   nir_ssa_scalar base = nir_ssa_scalar_resolved(orig_offset->ssa, 0);
   struct match match = match_address(base, format_shift);

   nir_ssa_def *offset =
      match.offset.def != NULL
         ? nir_channel(b, match.offset.def, match.offset.comp)
         : nir_imm_int(b, 0);

   /* If we were unable to fold in the shift, insert a right-shift now to undo
    * the implicit left shift of the instruction.
    */
   if (match.shift < 0) {
      if (match.sign_extend)
         offset = nir_ishr_imm(b, offset, -match.shift);
      else
         offset = nir_ushr_imm(b, offset, -match.shift);

      match.shift = 0;
   }

   assert(match.shift >= 0);
   nir_ssa_def *new_base = nir_channel(b, match.base.def, match.base.comp);

   if (intr->intrinsic == nir_intrinsic_load_global) {
      nir_ssa_def *repl =
         nir_load_agx(b, nir_dest_num_components(intr->dest),
                      nir_dest_bit_size(intr->dest), new_base, offset,
                      .access = nir_intrinsic_access(intr), .base = match.shift,
                      .format = format, .sign_extend = match.sign_extend);

      nir_ssa_def_rewrite_uses(&intr->dest.ssa, repl);
   } else if (intr->intrinsic == nir_intrinsic_load_global_constant) {
      nir_ssa_def *repl = nir_load_constant_agx(
         b, nir_dest_num_components(intr->dest), nir_dest_bit_size(intr->dest),
         new_base, offset, .access = nir_intrinsic_access(intr),
         .base = match.shift, .format = format,
         .sign_extend = match.sign_extend);

      nir_ssa_def_rewrite_uses(&intr->dest.ssa, repl);
   } else {
      nir_store_agx(b, intr->src[0].ssa, new_base, offset,
                    .access = nir_intrinsic_access(intr), .base = match.shift,
                    .format = format, .sign_extend = match.sign_extend);
   }

   nir_instr_remove(instr);
   return true;
}

bool
agx_nir_lower_address(nir_shader *shader)
{
   return nir_shader_instructions_pass(
      shader, pass, nir_metadata_block_index | nir_metadata_dominance, NULL);
}
