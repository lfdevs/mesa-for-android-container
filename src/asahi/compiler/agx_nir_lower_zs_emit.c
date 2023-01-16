/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

#define ALL_SAMPLES 0xFF
#define BASE_Z      1
#define BASE_S      2

static bool
lower(nir_function_impl *impl, nir_block *block)
{
   nir_intrinsic_instr *zs_emit = NULL;
   bool progress = false;

   nir_foreach_instr_reverse_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_store_output)
         continue;

      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      if (sem.location != FRAG_RESULT_DEPTH &&
          sem.location != FRAG_RESULT_STENCIL)
         continue;

      if (zs_emit == NULL) {
         nir_builder b;
         nir_builder_init(&b, impl);
         b.cursor = nir_before_instr(instr);

         /* Multisampling will get lowered later if needed, default to broadcast
          */
         nir_ssa_def *sample_mask = nir_imm_intN_t(&b, ALL_SAMPLES, 16);
         zs_emit = nir_store_zs_agx(&b, sample_mask,
                                    nir_ssa_undef(&b, 1, 32) /* depth */,
                                    nir_ssa_undef(&b, 1, 16) /* stencil */);
      }

      nir_ssa_def *value = intr->src[0].ssa;

      bool z = (sem.location == FRAG_RESULT_DEPTH);
      unsigned src_idx = z ? 1 : 2;
      unsigned base = z ? BASE_Z : BASE_S;

      assert((nir_intrinsic_base(zs_emit) & base) == 0 &&
             "each of depth/stencil may only be written once");

      nir_instr_rewrite_src_ssa(&zs_emit->instr, &zs_emit->src[src_idx], value);
      nir_intrinsic_set_base(zs_emit, nir_intrinsic_base(zs_emit) | base);

      nir_instr_remove(instr);
      progress = true;
   }

   return progress;
}

static bool
lower_discard_to_z(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_discard &&
       intr->intrinsic != nir_intrinsic_discard_if)
      return false;

   b->cursor = nir_before_instr(instr);

   if (intr->intrinsic == nir_intrinsic_discard_if)
      nir_push_if(b, intr->src[0].ssa);

   bool stencil_written =
      b->shader->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL);

   nir_store_zs_agx(b, nir_imm_intN_t(b, ALL_SAMPLES, 16),
                    nir_imm_float(b, NAN),
                    stencil_written ? nir_imm_intN_t(b, 0, 16)
                                    : nir_ssa_undef(b, 1, 16) /* stencil */,
                    .base = BASE_Z | (stencil_written ? BASE_S : 0));

   if (intr->intrinsic == nir_intrinsic_discard_if)
      nir_push_else(b, NULL);

   nir_instr_remove(instr);
   return false;
}

bool
agx_nir_lower_zs_emit(nir_shader *s)
{
   bool any_progress = false;

   if (!(s->info.outputs_written & (BITFIELD64_BIT(FRAG_RESULT_STENCIL) |
                                    BITFIELD64_BIT(FRAG_RESULT_DEPTH))))
      return false;

   nir_foreach_function(function, s) {
      if (!function->impl)
         continue;

      bool progress = false;

      nir_foreach_block(block, function->impl) {
         progress |= lower(function->impl, block);
      }

      if (progress) {
         nir_metadata_preserve(
            function->impl, nir_metadata_block_index | nir_metadata_dominance);
      } else {
         nir_metadata_preserve(function->impl, nir_metadata_all);
      }

      any_progress |= progress;
   }

   any_progress |= nir_shader_instructions_pass(
      s, lower_discard_to_z, nir_metadata_block_index | nir_metadata_dominance,
      NULL);
   s->info.fs.uses_discard = false;
   return any_progress;
}
