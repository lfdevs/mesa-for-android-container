/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2021 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

static nir_def *
alpha_to_coverage(nir_builder *b, nir_def *alpha, uint8_t nr_samples, bool has_intrinsic)
{
   if (has_intrinsic)
      return nir_alpha_to_coverage(b, alpha);

   /* Calculate a coverage mask (alpha * nr_samples) bits set. The way we do
    * this isn't particularly clever:
    *
    *    # of bits = (unsigned int) (alpha * nr_samples)
    *    mask = (1 << (# of bits)) - 1
    */
   nir_def *bits = nir_f2u32(b, nir_fmul_imm(b, alpha, nr_samples));
   return nir_iadd_imm(b, nir_ishl(b, nir_imm_intN_t(b, 1, 16), bits), -1);
}

/*
 * Lower alpha-to-coverage to sample_mask and some math. May run on either a
 * monolithic pixel shader or a fragment epilogue.
 */
bool
nir_lower_alpha_to_coverage(nir_shader *shader, uint8_t nr_samples, bool has_intrinsic, nir_def *dyn_enable)
{
   /* If we are not using the intrinsic we need to know the sample count. */
   assert(has_intrinsic || nr_samples);

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
   nir_def *mask = alpha_to_coverage(b, alpha, nr_samples, has_intrinsic);

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
