/*
 * Copyright (c) 2021 Intel Corporation
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

/*
 * Lower the shading rate output from the bit field format described in the
 * SPIRV spec :
 *
 * bit | name              | description
 *   0 | Vertical2Pixels   | Fragment invocation covers 2 pixels vertically
 *   1 | Vertical4Pixels   | Fragment invocation covers 4 pixels vertically
 *   2 | Horizontal2Pixels | Fragment invocation covers 2 pixels horizontally
 *   3 | Horizontal4Pixels | Fragment invocation covers 4 pixels horizontally
 *
 * into a single dword composed of 2 fp16 to be stored in the dword 0 of the
 * VUE header.
 *
 * When no horizontal/vertical bits are set, the size in pixel size in that
 * dimension is assumed to be 1.
 *
 * According to the specification, the shading rate output can be read &
 * written. A read after a write should report a different value if the
 * implemention decides on different primitive shading rate for some reason.
 * This is never the case in our implementation.
 */

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"

static bool
lower_shading_rate_output_instr(nir_builder *b, nir_instr *instr,
                                UNUSED void *_state)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_output &&
       intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   if (nir_intrinsic_base(intrin) != VARYING_SLOT_PRIMITIVE_SHADING_RATE)
      return false;

   b->cursor = intrin->intrinsic == nir_intrinsic_load_output ?
      nir_after_instr(instr) : nir_before_instr(instr);

   if (intrin->intrinsic == nir_intrinsic_store_output) {
      assert(intrin->src[0].is_ssa);
      nir_ssa_def *bit_field = intrin->src[0].ssa;
      nir_ssa_def *fp16_x =
         nir_i2f16(b,
                   nir_ishl(b, nir_imm_int(b, 1),
                            nir_ishr_imm(b, bit_field, 2)));
      nir_ssa_def *fp16_y =
         nir_i2f16(b,
                   nir_ishl(b, nir_imm_int(b, 1),
                            nir_iand_imm(b, bit_field, 0x3)));
      nir_ssa_def *packed_fp16_xy = nir_pack_32_2x16_split(b, fp16_x, fp16_y);

      nir_instr_rewrite_src(instr, &intrin->src[0],
                            nir_src_for_ssa(packed_fp16_xy));
   } else {
      assert(intrin->intrinsic == nir_intrinsic_load_output);
      nir_ssa_def *packed_fp16_xy = &intrin->dest.ssa;

      nir_ssa_def *u32_x =
         nir_i2i32(b, nir_unpack_32_2x16_split_x(b, packed_fp16_xy));
      nir_ssa_def *u32_y =
         nir_i2i32(b, nir_unpack_32_2x16_split_y(b, packed_fp16_xy));

      nir_ssa_def *bit_field =
         nir_ior(b, nir_ishl_imm(b, nir_ushr_imm(b, u32_x, 1), 2),
                    nir_ushr_imm(b, u32_y, 1));

      nir_ssa_def_rewrite_uses_after(packed_fp16_xy, bit_field,
                                     bit_field->parent_instr);
   }

   return true;
}

bool
brw_nir_lower_shading_rate_output(nir_shader *nir)
{
   /* TODO(mesh): Add Shading Rate support. */
   assert(nir->info.stage != MESA_SHADER_MESH);

   return nir_shader_instructions_pass(nir, lower_shading_rate_output_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance, NULL);
}
