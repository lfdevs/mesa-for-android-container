/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2021 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

static nir_def *
alpha_to_coverage(nir_builder *b, nir_def *alpha, bool has_intrinsic)
{
   if (has_intrinsic)
      return nir_alpha_to_coverage(b, alpha);

   /* The following formula is used to compute final sample mask:
    *  m = int(round(16.0 * clamp(src0_alpha, 0.0, 1.0)))
    *  dither_mask = 0x1111 * ((0xf7540 >> (m & ~3)) & 0xf) |
    *     0x0404 * (m & 2) | 0x0080 * (m & 1)
    *  sample_mask = sample_mask & dither_mask
    *
    * It gives a number of ones proportional to the alpha for 2, 4, 8 or 16
    * least significant bits of the result:
    *
    *  0.0000 0000000000000000
    *  0.0625 0000000010000000
    *  0.1250 0000100000001000
    *  0.1875 0000100010001000
    *  0.2500 0100010001000100
    *  0.3125 0100010011000100
    *  0.3750 0100110001001100
    *  0.4375 0100110011001100
    *  0.5000 0101010101010101
    *  0.5625 0101010111010101
    *  0.6250 0101110101011101
    *  0.6875 0101110111011101
    *  0.7500 0111011101110111
    *  0.8125 0111011111110111
    *  0.8750 0111111101111111
    *  0.9375 0111111111111111
    *  1.0000 1111111111111111
    *
    *  We use 16-bit math for the multiplies because the result always fits
    *  into 16 bits and that is typically way cheaper than full 32-bit
    *  multiplies.
    */
   nir_def *m =
      nir_f2i32(b, nir_fround_even(b, nir_fmul_imm(b, nir_fsat(b, alpha), 16.0)));

   nir_def *part_a =
      nir_u2u16(b, nir_iand_imm(b, nir_ushr(b, nir_imm_int(b, 0xf7540),
                                  nir_iand_imm(b, m, ~3)),
                                0xf));

   nir_def *part_b = nir_iand_imm(b, nir_u2u16(b, m), 2);
   nir_def *part_c = nir_iand_imm(b, nir_u2u16(b, m), 1);

   nir_def *mask = nir_ior(b, nir_imul_imm(b, part_a, 0x1111),
                           nir_ior(b, nir_imul_imm(b, part_b, 0x0404),
                                   nir_imul_imm(b, part_c, 0x0080)));

   /* Rotate the mask based on (pixel.x % 2) + (pixel.y % 2). This provides
    * dithering and randomizes the sample locations.
    */
   nir_def *pixel = nir_f2u32(b, nir_build_frag_coord(b, 2));
   nir_def *rotate_amount =
      nir_iadd(b, nir_iand_imm(b, nir_channel(b, pixel, 0), 0x1),
                  nir_iand_imm(b, nir_channel(b, pixel, 1), 0x1));
   mask = nir_ior(b, nir_ushr(b, mask, rotate_amount),
                  nir_ishl(b, mask, nir_isub_imm(b, 16, rotate_amount)));
   return nir_u2u32(b, mask);
}

/*
 * Lower alpha-to-coverage to sample_mask and some math. May run on either a
 * monolithic pixel shader or a fragment epilogue.
 */
bool
nir_lower_alpha_to_coverage(nir_shader *shader, bool has_intrinsic, nir_def *dyn_enable)
{
   /* nir_lower_io_to_temporaries ensures that stores are in the last block */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_block *block = nir_impl_last_block(impl);

   /* The store is probably at the end of the block, so search in reverse. */
   nir_intrinsic_instr *store = NULL;
   nir_foreach_instr_reverse(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_store_output)
         continue;

      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      if (sem.location != FRAG_RESULT_DATA0)
         continue;
      if (sem.dual_source_blend_index != 0)
         continue;

      store = intr;
      break;
   }

   /* If render target 0 isn't written, the alpha value input to
    * alpha-to-coverage is undefined. We assume that the alpha would be 1.0,
    * which would effectively disable alpha-to-coverage, skipping the lowering.
    *
    * Similarly, if there are less than 4 components, alpha is undefined.
    */
   nir_def *rgba = store ? store->src[0].ssa : NULL;
   if (!rgba || rgba->num_components < 4) {
      return nir_no_progress(impl);
   }

   nir_builder _b = nir_builder_at(nir_before_instr(&store->instr));
   nir_builder *b = &_b;

   nir_def *alpha = nir_channel(b, rgba, 3);
   if (dyn_enable)
      alpha = nir_bcsel(b, dyn_enable, alpha, nir_imm_floatN_t(b, 1.0f, alpha->bit_size));
   nir_def *mask = alpha_to_coverage(b, alpha, has_intrinsic);

   /* Discard samples that aren't covered */
   nir_demote_samples(b, nir_inot(b, mask));
   shader->info.fs.uses_discard = true;
   return nir_progress(true, impl, nir_metadata_control_flow);
}

/*
 * Modify the inputs to store_output instructions in a pixel shader when
 * alpha-to-one is used. May run on either a monolithic pixel shader or a
 * fragment epilogue.
 */
bool
nir_lower_alpha_to_one(nir_shader *shader)
{
   bool progress = false;

   /* nir_lower_io_to_temporaries ensures that stores are in the last block */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_block *block = nir_impl_last_block(impl);

   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_store_output)
         continue;

      /* The OpenGL spec is a bit confusing here, but seemingly alpha-to-one
       * applies to all render targets. Piglit
       * ext_framebuffer_multisample-draw-buffers-alpha-to-one checks this.
       *
       * Even more confusingly, it seems to apply to dual-source blending too.
       * ext_framebuffer_multisample-alpha-to-one-dual-src-blend checks this.
       */
      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      if (sem.location < FRAG_RESULT_DATA0)
         continue;

      nir_def *rgba = intr->src[0].ssa;
      if (rgba->num_components < 4)
         continue;

      nir_builder b = nir_builder_at(nir_before_instr(instr));
      nir_def *rgb1 = nir_vector_insert_imm(
         &b, rgba, nir_imm_floatN_t(&b, 1.0, rgba->bit_size), 3);

      nir_src_rewrite(&intr->src[0], rgb1);
      progress = true;
   }

   return nir_progress(progress, impl, nir_metadata_control_flow);
}
