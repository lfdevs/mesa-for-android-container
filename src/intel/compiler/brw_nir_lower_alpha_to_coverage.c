/*
 * Copyright © 2019 Intel Corporation
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

#include "compiler/nir/nir_builder.h"
#include "brw_nir.h"

/**
 * We need to compute alpha to coverage dithering manually in shader
 * and replace sample mask store with the bitwise-AND of sample mask and
 * alpha to coverage dithering.
 *
 * The following formula is used to compute final sample mask:
 *  m = int(16.0 * clamp(src0_alpha, 0.0, 1.0))
 *  dither_mask = 0x1111 * ((0xfea80 >> (m & ~3)) & 0xf) |
 *     0x0808 * (m & 2) | 0x0100 * (m & 1)
 *  sample_mask = sample_mask & dither_mask
 *
 * It gives a number of ones proportional to the alpha for 2, 4, 8 or 16
 * least significant bits of the result:
 *  0.0000 0000000000000000
 *  0.0625 0000000100000000
 *  0.1250 0001000000010000
 *  0.1875 0001000100010000
 *  0.2500 1000100010001000
 *  0.3125 1000100110001000
 *  0.3750 1001100010011000
 *  0.4375 1001100110011000
 *  0.5000 1010101010101010
 *  0.5625 1010101110101010
 *  0.6250 1011101010111010
 *  0.6875 1011101110111010
 *  0.7500 1110111011101110
 *  0.8125 1110111111101110
 *  0.8750 1111111011111110
 *  0.9375 1111111111111110
 *  1.0000 1111111111111111
 */
static nir_ssa_def *
build_dither_mask(nir_builder b, nir_intrinsic_instr *store_instr)
{
   nir_ssa_def *alpha =
      nir_channel(&b, nir_ssa_for_src(&b, store_instr->src[0], 4), 3);

   nir_ssa_def *m =
      nir_f2i32(&b, nir_fmul_imm(&b, nir_fsat(&b, alpha), 16.0));

   nir_ssa_def *part_a =
      nir_iand(&b,
               nir_imm_int(&b, 0xf),
               nir_ushr(&b,
                        nir_imm_int(&b, 0xfea80),
                        nir_iand(&b, m, nir_imm_int(&b, ~3))));

   nir_ssa_def *part_b = nir_iand(&b, m, nir_imm_int(&b, 2));

   nir_ssa_def *part_c = nir_iand(&b, m, nir_imm_int(&b, 1));

   return nir_ior(&b,
                  nir_imul_imm(&b, part_a, 0x1111),
                  nir_ior(&b,
                          nir_imul_imm(&b, part_b, 0x0808),
                          nir_imul_imm(&b, part_c, 0x0100)));
}

void
brw_nir_lower_alpha_to_coverage(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   /* Bail out early if we don't have gl_SampleMask */
   bool is_sample_mask = false;
   nir_foreach_variable(var, &shader->outputs) {
      if (var->data.location == FRAG_RESULT_SAMPLE_MASK) {
         is_sample_mask = true;
         break;
      }
   }

   if (!is_sample_mask)
      return;

   nir_foreach_function(function, shader) {
      nir_function_impl *impl = function->impl;
      nir_builder b;
      nir_builder_init(&b, impl);

      nir_foreach_block(block, impl) {
         nir_intrinsic_instr *sample_mask_instr = NULL;
         nir_intrinsic_instr *store_instr = NULL;

         nir_foreach_instr_safe(instr, block) {
            if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
               nir_variable *out = NULL;

               switch (intr->intrinsic) {
               case nir_intrinsic_store_output:
                  nir_foreach_variable(var, &shader->outputs) {
                     int drvloc = var->data.driver_location;
                     if (nir_intrinsic_base(intr) == drvloc) {
                        out = var;
                        break;
                     }
                  }

                  if (out->data.mode != nir_var_shader_out)
                     continue;

                  /* save gl_SampleMask instruction pointer */
                  if (out->data.location == FRAG_RESULT_SAMPLE_MASK) {
                     assert(!sample_mask_instr);
                     sample_mask_instr = intr;
                  }

                  /* save out_color[0] instruction pointer */
                  if ((out->data.location == FRAG_RESULT_COLOR ||
                      out->data.location == FRAG_RESULT_DATA0)) {
                     nir_src *offset_src = nir_get_io_offset_src(intr);
                     if (nir_src_is_const(*offset_src) && nir_src_as_uint(*offset_src) == 0) {
                        assert(!store_instr);
                        store_instr = intr;
                     }
                  }
                  break;
               default:
                  continue;
               }
            }
         }

         if (sample_mask_instr && store_instr) {
            b.cursor = nir_before_instr(&store_instr->instr);
            nir_ssa_def *dither_mask = build_dither_mask(b, store_instr);

            /* Combine dither_mask and reorder gl_SampleMask store instruction
             * after render target 0 store instruction.
             */
            nir_instr_remove(&sample_mask_instr->instr);
            dither_mask = nir_iand(&b, sample_mask_instr->src[0].ssa, dither_mask);
            nir_instr_insert_after(&store_instr->instr, &sample_mask_instr->instr);
            nir_instr_rewrite_src(&sample_mask_instr->instr,
                                  &sample_mask_instr->src[0],
                                  nir_src_for_ssa(dither_mask));
         }
      }
      nir_metadata_preserve(impl, nir_metadata_block_index |
                            nir_metadata_dominance);
   }
}
