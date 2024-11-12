/*
 * Copyright © 2024 Collabora, Ltd.
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

/* Adapted from intel_nir_lower_conversions.c */

#include "nak_private.h"
#include "nir_builder.h"

static nir_rounding_mode
op_rounding_mode(nir_op op)
{
   switch (op) {
   case nir_op_f2f16_rtne: return nir_rounding_mode_rtne;
   case nir_op_f2f16_rtz: return nir_rounding_mode_rtz;
   default: return nir_rounding_mode_undef;
   }
}

static bool
split_64bit_conversion(nir_builder *b, nir_instr *instr, UNUSED void *_data)
{
   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);

   if (!nir_op_infos[alu->op].is_conversion)
      return false;

   unsigned src_bit_size = nir_src_bit_size(alu->src[0].src);
   nir_alu_type src_type = nir_op_infos[alu->op].input_types[0];
   nir_alu_type src_full_type = (nir_alu_type) (src_type | src_bit_size);

   unsigned dst_bit_size = alu->def.bit_size;
   nir_alu_type dst_full_type = nir_op_infos[alu->op].output_type;
   assert(nir_alu_type_get_type_size(dst_full_type) == dst_bit_size);
   nir_alu_type dst_type = nir_alu_type_get_base_type(dst_full_type);

   /* We can't cross the 64-bit boundary in one conversion */
   if ((src_bit_size <= 32 && dst_bit_size <= 32) ||
       (src_bit_size >= 32 && dst_bit_size >= 32))
      return false;

   nir_alu_type tmp_type;
   if ((src_full_type == nir_type_float16 && dst_bit_size == 64) ||
       (src_bit_size == 64 && dst_full_type == nir_type_float16)) {
      /* It is important that the intermediate conversion happens through a
       * 32-bit float type so we don't lose range when we convert to/from
       * a 64-bit integer.
       */
      tmp_type = nir_type_float32;
   } else {
      /* For fp64 to integer conversions, using an integer intermediate type
       * ensures that rounding happens as part of the first conversion,
       * avoiding any chance of rtne rounding happening before the conversion
       * to integer (which is expected to round towards zero).
       *
       * NOTE: NVIDIA hardware saturates conversions by default and the second
       * conversion will not saturate in this case.  However, GLSL makes OOB
       * values in conversions undefiend.
       *
       * For all other conversions, the conversion from int to int is either
       * lossless or just as lossy as the final conversion.
       */
      tmp_type = dst_type | 32;
   }

   b->cursor = nir_before_instr(&alu->instr);
   nir_def *src = nir_ssa_for_alu_src(b, alu, 0);
   nir_def *tmp = nir_type_convert(b, src, src_type, tmp_type,
                                   nir_rounding_mode_undef);
   nir_def *res = nir_type_convert(b, tmp, tmp_type, dst_full_type,
                                   op_rounding_mode(alu->op));
   nir_def_replace(&alu->def, res);

   return true;
}

bool
nak_nir_split_64bit_conversions(nir_shader *nir)
{
   return nir_shader_instructions_pass(nir, split_64bit_conversion,
                                       nir_metadata_control_flow,
                                       NULL);
}
