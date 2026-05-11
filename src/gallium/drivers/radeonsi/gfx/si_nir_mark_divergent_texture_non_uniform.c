/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "nir.h"

bool si_nir_mark_divergent_texture_non_uniform(struct nir_shader *nir)
{
   /* sampler_non_uniform and texture_non_uniform are always false in GLSL,
    * but this can lead to unexpected behavior if texture/sampler index come from
    * a vertex attribute.
    *
    * For instance, 2 consecutive draws using 2 different index values,
    * could be squashed together by the hw - producing a single draw with
    * non-dynamically uniform index.
    *
    * To avoid this, detect divergent indexing, mark them as non-uniform,
    * so that we can apply waterfall loop on these index later (either llvm
    * backend or nir_lower_non_uniform_access).
    *
    * See https://gitlab.freedesktop.org/mesa/mesa/-/issues/2253
    */

   bool divergence_changed = false;

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_metadata_require(impl, nir_metadata_divergence);

   nir_foreach_block_safe(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_tex)
            continue;

         nir_tex_instr *tex = nir_instr_as_tex(instr);
         for (int i = 0; i < tex->num_srcs; i++) {
            bool divergent = nir_src_is_divergent(&tex->src[i].src);

            switch (tex->src[i].src_type) {
            case nir_tex_src_texture_deref:
            case nir_tex_src_texture_handle:
               tex->texture_non_uniform |= divergent;
               break;
            case nir_tex_src_sampler_deref:
            case nir_tex_src_sampler_handle:
               tex->sampler_non_uniform |= divergent;
               break;
            default:
               break;
            }
         }

         /* If dest is already divergent, divergence won't change. */
         divergence_changed |= !tex->def.divergent &&
            (tex->texture_non_uniform || tex->sampler_non_uniform);
      }
   }
   return nir_progress(divergence_changed, impl,
                       nir_metadata_all & ~nir_metadata_divergence);
}
