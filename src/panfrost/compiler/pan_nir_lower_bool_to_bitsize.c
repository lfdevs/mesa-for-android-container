/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "nir_builder.h"

static bool
assert_ssa_def_is_not_1bit(nir_def *def, UNUSED void *unused)
{
   assert(def->bit_size > 1);
   return true;
}

static bool
rewrite_1bit_ssa_def_to_32bit(nir_def *def, void *_progress)
{
   bool *progress = _progress;
   if (def->bit_size == 1) {
      def->bit_size = 32;
      *progress = true;
   }
   return true;
}

static uint32_t
get_bool_convert_opcode(uint32_t dst_bit_size)
{
   switch (dst_bit_size) {
   case 32:
      return nir_op_i2i32;
   case 16:
      return nir_op_i2i16;
   case 8:
      return nir_op_i2i8;
   default:
      UNREACHABLE("invalid boolean bit-size");
   }
}

static void
resize_bool_alu_source(nir_builder *b, nir_alu_instr *alu,
                       uint32_t src_idx, uint32_t bit_size)
{
   if (nir_src_bit_size(alu->src[src_idx].src) == bit_size)
      return;

   b->cursor = nir_before_instr(&alu->instr);
   nir_op convert_op = get_bool_convert_opcode(bit_size);

   /* Retain the number of components and swizzle of the original
    * instruction so that we don’t unnecessarily create a vectorized
    * instruction.
    */
   nir_def *new_src =
      nir_build_alu1(b, convert_op, nir_ssa_for_alu_src(b, alu, src_idx));

   nir_src_rewrite(&alu->src[src_idx].src, new_src);

   /* The swizzle will have been handled by the conversion instruction
    * so we can reset it back to the default
    */
   for (unsigned j = 0; j < NIR_MAX_VEC_COMPONENTS; j++)
      alu->src[src_idx].swizzle[j] = j;
}

static void
make_sources_canonical(nir_builder *b, nir_alu_instr *alu, uint32_t start_idx)
{
   const nir_op_info *op_info = &nir_op_infos[alu->op];

   /* TODO: for now we take the bit-size of the first non-const source as the
    * canonical form but we could try to be smarter.
    */
   uint32_t bit_size = 0;
   for (uint32_t i = start_idx; i < op_info->num_inputs; i++) {
      if (!nir_src_is_const(alu->src[i].src)) {
         bit_size = nir_src_bit_size(alu->src[i].src);
         break;
      }
   }
   if (bit_size == 0)
      bit_size = nir_src_bit_size(alu->src[start_idx].src);

   for (uint32_t i = start_idx; i < op_info->num_inputs; i++)
      resize_bool_alu_source(b, alu, i, bit_size);
}

#define CASE_CMP(cmp)                                                          \
   case nir_op_##cmp:                                                          \
      opcode = nir_op_##cmp##_pan;                                             \
      break;

static bool
lower_alu_instr(nir_builder *b, nir_alu_instr *alu)
{
   const nir_op_info *op_info = &nir_op_infos[alu->op];

   /* For operations that can take multiple boolean sources we need to ensure
    * that all booleans have the same bit-size
    */
   switch (alu->op) {
   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec5:
   case nir_op_vec8:
   case nir_op_vec16:
   case nir_op_inot:
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
      if (alu->def.bit_size > 1)
         return false; /* Not a boolean instruction */
      FALLTHROUGH;

   case nir_op_ieq:
   case nir_op_ine:
      make_sources_canonical(b, alu, 0);
      break;

   case nir_op_bcsel:
      /* bcsel may be choosing between boolean sources too */
      if (alu->def.bit_size == 1)
         make_sources_canonical(b, alu, 0);
      else
         resize_bool_alu_source(b, alu, 0, alu->def.bit_size);
      break;

   default:
      break;
   }

   /* Now that we have a canonical boolean bit-size, go on and rewrite the
    * instruction to match the canonical bit-size.
    */
   uint32_t bit_size = nir_src_bit_size(alu->src[0].src);
   assert(bit_size > 1);

   nir_op opcode = alu->op;
   switch (opcode) {
   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec5:
   case nir_op_vec8:
   case nir_op_vec16:
   case nir_op_inot:
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
      /* Nothing to do here, we do not specialize these opcodes by bit-size */
      break;

   case nir_op_b2b1:
      /* Since the canonical bit size is the size of the src, it's a no-op */
      opcode = nir_op_mov;
      break;

   case nir_op_b2b32:
      /* For up-converting booleans, sign-extend */
      opcode = nir_op_i2i32;
      break;

   case nir_op_bcsel:
      opcode = nir_op_bcsel_pan;

      /* The destination of the selection may have a different bit-size from
       * the bcsel condition.
       */
      bit_size = nir_src_bit_size(alu->src[1].src);
      break;

      CASE_CMP(flt);
      CASE_CMP(fge);
      CASE_CMP(feq);
      CASE_CMP(fneu);
      CASE_CMP(ilt);
      CASE_CMP(ige);
      CASE_CMP(ieq);
      CASE_CMP(ine);
      CASE_CMP(ult);
      CASE_CMP(uge);

   default:
      assert(alu->def.bit_size > 1);
      for (unsigned i = 0; i < op_info->num_inputs; i++)
         assert(alu->src[i].src.ssa->bit_size > 1);
      return false;
   }

   alu->op = opcode;
   alu->fp_math_ctrl = nir_op_valid_fp_math_ctrl(opcode, alu->fp_math_ctrl);

   if (alu->def.bit_size == 1)
      alu->def.bit_size = bit_size;

   return true;
}

#undef CASE_CMP

static bool
lower_load_const_instr(nir_load_const_instr *load)
{
   bool progress = false;

   if (load->def.bit_size > 1)
      return progress;

   /* TODO: It is not clear if there is any case in which we can ever hit
    * this path, so for now we just provide a 32-bit default.
    *
    * TODO2: after some changed on nir_const_value and other on upstream, we
    * removed the initialization of a general value like this:
    *   nir_const_value value = load->value
    *
    * to initialize per value component. Need to confirm if that is correct,
    * but look at the TOO before.
    */
   for (unsigned i = 0; i < load->def.num_components; i++) {
      load->value[i].u32 = load->value[i].b ? NIR_TRUE : NIR_FALSE;
      load->def.bit_size = 32;
      progress = true;
   }

   return progress;
}

static bool
lower_phi_instr(nir_builder *b, nir_phi_instr *phi)
{
   if (phi->def.bit_size != 1)
      return false;

   /* Ensure all phi sources have a canonical bit-size. We choose the
    * bit-size of the first phi already visted phi source as the canonical form.
    *
    * TODO: maybe we can be smarter about how we choose the canonical form.
    */
   uint32_t dst_bit_size = 0;
   nir_foreach_phi_src(phi_src, phi) {
      uint32_t src_bit_size = nir_src_bit_size(phi_src->src);
      if (src_bit_size != 1) {
         dst_bit_size = src_bit_size;
         break;
      }
   }

   nir_foreach_phi_src(phi_src, phi) {
      uint32_t src_bit_size = nir_src_bit_size(phi_src->src);
      if (src_bit_size != dst_bit_size) {
         b->cursor = nir_before_src(&phi_src->src);
         nir_op convert_op = get_bool_convert_opcode(dst_bit_size);
         nir_def *new_src =
            nir_build_alu(b, convert_op, phi_src->src.ssa, NULL, NULL, NULL);
         nir_src_rewrite(&phi_src->src, new_src);
      }
   }

   phi->def.bit_size = dst_bit_size;

   return true;
}

static bool
lower_tex_instr(nir_tex_instr *tex)
{
   bool progress = false;
   rewrite_1bit_ssa_def_to_32bit(&tex->def, &progress);
   if (tex->dest_type == nir_type_bool1) {
      tex->dest_type = nir_type_bool32;
      progress = true;
   }
   return progress;
}

static bool
lower_bool_to_bitsize_instr(nir_builder *b,
                            nir_instr *instr,
                            UNUSED void *cb_data)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      return lower_alu_instr(b, nir_instr_as_alu(instr));

   case nir_instr_type_load_const:
      return lower_load_const_instr(nir_instr_as_load_const(instr));

   case nir_instr_type_phi:
      return lower_phi_instr(b, nir_instr_as_phi(instr));

   case nir_instr_type_undef:
   case nir_instr_type_intrinsic: {
      bool progress = false;
      nir_foreach_def(instr, rewrite_1bit_ssa_def_to_32bit, &progress);
      return progress;
   }

   case nir_instr_type_tex:
      return lower_tex_instr(nir_instr_as_tex(instr));

   default:
      nir_foreach_def(instr, assert_ssa_def_is_not_1bit, NULL);
      return false;
   }
}

bool
pan_nir_lower_bool_to_bitsize(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader, lower_bool_to_bitsize_instr,
                                       nir_metadata_control_flow,
                                       NULL);
}
