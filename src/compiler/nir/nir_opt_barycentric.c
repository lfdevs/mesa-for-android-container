/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

/* This pass attempts to optimize load_barycentric_at_{sample,offset} with
 * simpler load_barycentric_* equivalents where possible, and optionally
 * lowers load_barycentric_at_sample to load_barycentric_at_offset with a
 * position derived from the sample ID instead.
 */

static bool
opt_bary_at_sample(nir_builder *b, nir_intrinsic_instr *intr, bool lower_sample_to_pos)
{
   /* Check for and handle simple replacement cases:
    * - Sample num is current sample.
    */
   enum glsl_interp_mode interp_mode = nir_intrinsic_interp_mode(intr);
   nir_intrinsic_instr *sample = nir_src_as_intrinsic(intr->src[0]);

   assert(interp_mode != INTERP_MODE_FLAT);
   if (sample && sample->intrinsic == nir_intrinsic_load_sample_id) {
      nir_def *repl = nir_load_barycentric_sample(
         b,
         intr->def.bit_size,
         .interp_mode = interp_mode);
      nir_def_replace(&intr->def, repl);
      nir_instr_free(&intr->instr);
      return true;
   }

   if (!lower_sample_to_pos)
      return false;

   /* Turn the sample id into a position. */
   nir_def *offset =
      nir_load_sample_pos_from_id(b, intr->def.bit_size, intr->src[0].ssa);
   offset = nir_fadd_imm(b, offset, -0.5f);

   nir_def *repl = nir_load_barycentric_at_offset(
      b,
      intr->def.bit_size,
      offset,
      .interp_mode = interp_mode);

   nir_def_replace(&intr->def, repl);
   nir_instr_free(&intr->instr);
   return true;
}

static bool
src_is_vec2_sample_pos_minus_half(nir_src src)
{
   nir_alu_instr *alu = nir_src_as_alu(src);
   if (!alu || alu->op != nir_op_vec2)
      return false;

   /* Check both vec2 components. */
   for (unsigned u = 0; u < 2; ++u) {
      nir_scalar comp = nir_get_scalar(&alu->def, u);
      comp = nir_scalar_chase_movs(comp);

      if (!nir_scalar_is_alu(comp))
         return false;

      /* Look for fadd(sample_pos.x/y, -0.5f) or fsub(sample_pos.x/y, +0.5f) */
      nir_op op = nir_scalar_alu_op(comp);
      if (op != nir_op_fadd && op != nir_op_fsub)
         return false;

      float half_val = op == nir_op_fadd ? -0.5f : +0.5f;
      unsigned sample_pos_srcn = ~0U;
      unsigned half_srcn = ~0U;

      /* Check both fadd/fsub sources. */
      for (unsigned n = 0; n < 2; ++n) {
         nir_scalar src = nir_scalar_chase_alu_src(comp, n);

         if (nir_scalar_is_intrinsic(src) &&
             nir_scalar_intrinsic_op(src) == nir_intrinsic_load_sample_pos) {
            sample_pos_srcn = n;
         } else if (nir_scalar_is_const(src) &&
                    nir_scalar_as_const_value(src).f32 == half_val) {
            half_srcn = n;
         }
      }

      /* One or more operands not found. */
      if (sample_pos_srcn == ~0U || half_srcn == ~0U)
         return false;

      /* fsub is not commutative. */
      if (op == nir_op_fsub && (sample_pos_srcn != 0 || half_srcn != 1))
         return false;

      /* vec2.{x,y} needs to be referencing load_sample_pos.{x,y}. */
      nir_scalar sample_pos_src =
         nir_scalar_chase_alu_src(comp, sample_pos_srcn);
      if (sample_pos_src.comp != u)
         return false;
   }

   return true;
}

static bool
opt_bary_at_offset(nir_builder *b, nir_intrinsic_instr *intr)
{
   /* Check for and handle simple replacement cases:
    * - Flat interpolation - don't care about offset, will get consumed.
    * - Offset is zero.
    */
   enum glsl_interp_mode interp_mode = nir_intrinsic_interp_mode(intr);
   nir_src src = intr->src[0];

   assert(interp_mode != INTERP_MODE_FLAT);
   if (nir_src_is_const(src) && !nir_src_comp_as_int(src, 0) &&
       !nir_src_comp_as_int(src, 1)) {
      nir_def *repl = nir_load_barycentric_pixel(
         b,
         intr->def.bit_size,
         .interp_mode = interp_mode);
      nir_def_replace(&intr->def, repl);
      nir_instr_free(&intr->instr);
      return true;
   }

   /* Offset is vec2(sample_pos - 0.5f). */
   if (src_is_vec2_sample_pos_minus_half(src)) {
      nir_def *repl = nir_load_barycentric_sample(
         b,
         intr->def.bit_size,
         .interp_mode = interp_mode);
      nir_def_replace(&intr->def, repl);
      nir_instr_free(&intr->instr);
      return true;
   }

   return false;
}

static bool
opt_bary(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   bool *lower_sample_to_pos = cb_data;
   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_at_sample:
      return opt_bary_at_sample(b, intr, *lower_sample_to_pos);

   case nir_intrinsic_load_barycentric_at_offset:
      return opt_bary_at_offset(b, intr);

   default:
      break;
   }

   return false;
}

bool
nir_opt_barycentric(nir_shader *shader, bool lower_sample_to_pos)
{
   return nir_shader_intrinsics_pass(shader,
                                     opt_bary,
                                     nir_metadata_control_flow,
                                     &lower_sample_to_pos);
}
