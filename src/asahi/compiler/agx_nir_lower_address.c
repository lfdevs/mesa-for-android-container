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

/*
 * Try to rewrite (a << (#b + #c)) + #d as ((a << #b) + #d') << #c,
 * assuming that #d is a multiple of 1 << #c. This takes advantage of
 * the hardware's implicit << #c and avoids a right-shift.
 *
 * This pattern occurs with a struct-of-array layout.
 */
static bool
match_soa(nir_builder *b, struct match *match, unsigned format_shift)
{
   if (!nir_ssa_scalar_is_alu(match->offset) ||
       nir_ssa_scalar_alu_op(match->offset) != nir_op_iadd)
      return false;

   nir_ssa_scalar summands[] = {
      nir_ssa_scalar_chase_alu_src(match->offset, 0),
      nir_ssa_scalar_chase_alu_src(match->offset, 1),
   };

   for (unsigned i = 0; i < ARRAY_SIZE(summands); ++i) {
      if (!nir_ssa_scalar_is_const(summands[i]))
         continue;

      unsigned offset = nir_ssa_scalar_as_uint(summands[i]);
      unsigned offset_shifted = offset >> format_shift;

      /* If the constant offset is not aligned, we can't rewrite safely */
      if (offset != (offset_shifted << format_shift))
         return false;

      /* The other operand must be ishl */
      if (!nir_ssa_scalar_is_alu(summands[1 - i]) ||
          nir_ssa_scalar_alu_op(summands[1 - i]) != nir_op_ishl)
         return false;

      nir_ssa_scalar shifted = nir_ssa_scalar_chase_alu_src(summands[1 - i], 0);
      nir_ssa_scalar shift = nir_ssa_scalar_chase_alu_src(summands[1 - i], 1);

      /* The explicit shift must be at least as big as the implicit shift */
      if (!nir_ssa_scalar_is_const(shift) ||
          nir_ssa_scalar_as_uint(shift) < format_shift)
         return false;

      /* All conditions met, rewrite! */
      nir_ssa_def *shifted_ssa = nir_vec_scalars(b, &shifted, 1);
      uint32_t shift_u32 = nir_ssa_scalar_as_uint(shift);

      nir_ssa_def *rewrite =
         nir_iadd_imm(b, nir_ishl_imm(b, shifted_ssa, shift_u32 - format_shift),
                      offset_shifted);

      match->offset = nir_get_ssa_scalar(rewrite, 0);
      match->shift = 0;
      return true;
   }

   return false;
}

/* Try to pattern match address calculation */
static struct match
match_address(nir_builder *b, nir_ssa_scalar base, int8_t format_shift)
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
      /* We can add a small constant to the 64-bit base for free */
      if (nir_ssa_scalar_is_const(summands[i]) &&
          nir_ssa_scalar_as_uint(summands[i]) < (1ull << 32)) {

         uint32_t value = nir_ssa_scalar_as_uint(summands[i]);

         return (struct match){
            .base = summands[1 - i],
            .offset = nir_get_ssa_scalar(nir_imm_int(b, value), 0),
            .shift = -format_shift,
            .sign_extend = false,
         };
      }

      /* Otherwise, we can only add an offset extended from 32-bits */
      if (!nir_ssa_scalar_is_alu(summands[i]))
         continue;

      nir_op op = nir_ssa_scalar_alu_op(summands[i]);

      if (op != nir_op_u2u64 && op != nir_op_i2i64)
         continue;

      /* We've found a summand, commit to it */
      match.base = summands[1 - i];
      match.offset = nir_ssa_scalar_chase_alu_src(summands[i], 0);
      match.sign_extend = (op == nir_op_i2i64);

      /* Undo the implicit shift from using as offset */
      match.shift = -format_shift;
      break;
   }

   /* If we didn't find something to fold in, there's nothing else we can do */
   if (!match.offset.def)
      return match;

   /* But if we did, we can try to fold in an ishl from the offset */
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
         } else if (new_shift > 0) {
            /* For large shifts, we do need an ishl instruction but we can
             * shrink the shift to avoid generating an ishr.
             */
            assert(new_shift >= 3);

            nir_ssa_def *rewrite =
               nir_ishl_imm(b, nir_vec_scalars(b, &shifted, 1), new_shift);

            match.offset = nir_get_ssa_scalar(rewrite, 0);
            match.shift = 0;
         }
      }
   } else {
      /* Try to match struct-of-arrays pattern, updating match if possible */
      match_soa(b, &match, format_shift);
   }

   return match;
}

static enum pipe_format
format_for_bitsize(unsigned bitsize)
{
   switch (bitsize) {
   case 8:
      return PIPE_FORMAT_R8_UINT;
   case 16:
      return PIPE_FORMAT_R16_UINT;
   case 32:
      return PIPE_FORMAT_R32_UINT;
   default:
      unreachable("should have been lowered");
   }
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
   enum pipe_format format = format_for_bitsize(bitsize);
   unsigned format_shift = util_logbase2(util_format_get_blocksize(format));

   nir_src *orig_offset = nir_get_io_offset_src(intr);
   nir_ssa_scalar base = nir_ssa_scalar_resolved(orig_offset->ssa, 0);
   struct match match = match_address(b, base, format_shift);

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

   /* Hardware offsets must be 32-bits. Upconvert if the source code used
    * smaller integers.
    */
   if (offset->bit_size != 32) {
      assert(offset->bit_size < 32);

      if (match.sign_extend)
         offset = nir_i2i32(b, offset);
      else
         offset = nir_u2u32(b, offset);
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
