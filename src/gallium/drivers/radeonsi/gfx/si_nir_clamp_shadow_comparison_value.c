/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "nir_builder.h"

static bool clamp_shadow_comparison_value(nir_builder *b, nir_tex_instr *tex,
                                          void *state)
{
   if (!tex->is_shadow || tex->op == nir_texop_lod)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   int samp_index = nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle);
   int comp_index = nir_tex_instr_src_index(tex, nir_tex_src_comparator);
   assert(samp_index >= 0 && comp_index >= 0);

   nir_def *sampler = tex->src[samp_index].src.ssa;
   nir_def *compare = tex->src[comp_index].src.ssa;
   /* Must have been lowered to descriptor. */
   assert(sampler->num_components > 1);

   nir_def *upgraded = nir_channel(b, sampler, 3);
   upgraded = nir_i2b(b, nir_ubfe_imm(b, upgraded, 29, 1));

   nir_def *clamped = nir_fsat(b, compare);
   compare = nir_bcsel(b, upgraded, clamped, compare);

   nir_src_rewrite(&tex->src[comp_index].src, compare);
   return true;
}

bool si_nir_clamp_shadow_comparison_value(nir_shader *nir)
{
   /* Section 8.23.1 (Depth Texture Comparison Mode) of the
    * OpenGL 4.5 spec says:
    *
    *    "If the texture’s internal format indicates a fixed-point
    *     depth texture, then D_t and D_ref are clamped to the
    *     range [0, 1]; otherwise no clamping is performed."
    *
    * TC-compatible HTILE promotes Z16 and Z24 to Z32_FLOAT,
    * so the depth comparison value isn't clamped for Z16 and
    * Z24 anymore. Do it manually here for GFX8-9; GFX10 has
    * an explicitly clamped 32-bit float format.
    */
   return nir_shader_tex_pass(nir, clamp_shadow_comparison_value,
                              nir_metadata_control_flow, NULL);
}
