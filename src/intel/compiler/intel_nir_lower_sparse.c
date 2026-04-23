/*
 * Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_nir.h"
#include "compiler/nir/nir_builder.h"

/*
 * This pass lowers a few of the sparse instructions to something HW can
 * handle.
 *
 * The image_*_sparse_load intrinsics are lowered into 2 instructions, a
 * regular image_*_load intrinsic and a sparse texture txf operation and
 * reconstructs the sparse vector of the original intrinsic using the 2 new
 * values. We need to do this because our backend implements image load/store
 * using the dataport and the dataport unit doesn't provide residency
 * information. We need to use the sampler for residency.
 *
 * The is_sparse_texels_resident intrinsic is lowered to a bit checking
 * operation as the data reported by the sampler is a single bit per lane in
 * the first component.
 *
 * The tex_* instructions with a compare value need to be lower into 2
 * instructions due to a HW limitation :
 *
 * SKL PRMs, Volume 7: 3D-Media-GPGPU, Messages, SIMD Payloads :
 *
 *    "The Pixel Null Mask field, when enabled via the Pixel Null Mask Enable
 *     will be incorect for sample_c when applied to a surface with 64-bit per
 *     texel format such as R16G16BA16_UNORM. Pixel Null mask Enable may
 *     incorrectly report pixels as referencing a Null surface."
 */

static void
lower_is_sparse_texels_resident(nir_builder *b, nir_intrinsic_instr *intr,
                                bool jay)
{
   b->cursor = nir_after_instr(&intr->instr);

   nir_def_replace(&intr->def,
      jay ? nir_inverse_ballot(b, intr->src[0].ssa)
          : nir_i2b(b, nir_iand(b, intr->src[0].ssa,
                                nir_ishl(b, nir_imm_int(b, 1),
                                         nir_load_subgroup_invocation(b)))));
}

static void
lower_sparse_residency_code_and(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def_rewrite_uses(
      &intrin->def,
      nir_iand(b, intrin->src[0].ssa, intrin->src[1].ssa));
}

static void
lower_sparse_image_load(nir_builder *b, nir_intrinsic_instr *intrin, bool jay)
{
   b->cursor = nir_instr_remove(&intrin->instr);

   const bool bindless =
      intrin->intrinsic == nir_intrinsic_bindless_image_sparse_load;
   const bool array = nir_intrinsic_image_array(intrin);
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intrin);
   const unsigned fmt = nir_intrinsic_format(intrin);
   const enum gl_access_qualifier access = nir_intrinsic_access(intrin);
   const nir_alu_type dest_type = nir_intrinsic_dest_type(intrin);
   nir_src *s = intrin->src;

   nir_def *img_load = bindless
      ? nir_bindless_image_load(b, intrin->num_components - 1,
                                intrin->def.bit_size,
                                s[0].ssa, s[1].ssa, s[2].ssa, s[3].ssa,
                                .image_dim = dim, .image_array = array,
                                .format = fmt, .access = access,
                                .dest_type = dest_type)
      : nir_image_load(b, intrin->num_components - 1, intrin->def.bit_size,
                       s[0].ssa, s[1].ssa, s[2].ssa, s[3].ssa,
                       .image_dim = dim, .image_array = array, .format = fmt,
                       .access = access, .dest_type = dest_type);

   nir_def *dests[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < intrin->num_components - 1; i++) {
      dests[i] = nir_channel(b, img_load, i);
   }

   /* Use texture instruction to compute residency */
   nir_def *coord;
   if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_CUBE && array) {
      nir_def *img_layer = nir_channel(b, intrin->src[1].ssa, 2);
      nir_def *tex_slice = nir_idiv(b, img_layer, nir_imm_int(b, 6));
      nir_def *tex_face =
         nir_iadd(b, img_layer, nir_ineg(b, nir_imul_imm(b, tex_slice, 6)));
      nir_def *comps[4] = {
         nir_channel(b, intrin->src[1].ssa, 0),
         nir_channel(b, intrin->src[1].ssa, 1),
         tex_face,
         tex_slice
      };
      coord = nir_vec(b, comps, 4);
   } else {
      const unsigned comps = nir_image_intrinsic_coord_components(intrin);
      coord = nir_channels(b, intrin->src[1].ssa, nir_component_mask(comps));
   }

   nir_def *txf =
      nir_build_tex(b,
                    jay ? nir_texop_sparse_residency_txf_intel : nir_texop_txf,
                    coord,
                    .texture_offset = bindless ? NULL : intrin->src[0].ssa,
                    .texture_handle = bindless ? intrin->src[0].ssa : NULL,
                    .dim = nir_intrinsic_image_dim(intrin),
                    .dest_type = nir_type_float32, /* dest is unused */
                    .is_array = array, .is_sparse = true);

   dests[intrin->num_components - 1] =
      nir_channel(b, txf, txf->num_components - 1);

   nir_def_rewrite_uses(
      &intrin->def,
      nir_vec(b, dests, intrin->num_components));
}

static bool
split_tex_residency(nir_builder *b, nir_tex_instr *tex, bool jay)
{
   int compare_idx = nir_tex_instr_src_index(tex, nir_tex_src_comparator);

   if (!jay && compare_idx == -1)
      return false;

   b->cursor = nir_after_instr(&tex->instr);

   /* Clone the original instruction */
   nir_tex_instr *sparse_tex =
      nir_instr_as_tex(nir_instr_clone(b->shader, &tex->instr));
   nir_def_init(&sparse_tex->instr, &sparse_tex->def, 2, tex->def.bit_size);
   nir_builder_instr_insert(b, &sparse_tex->instr);

   if (jay) {
      sparse_tex->op = tex->op == nir_texop_txf ?
                       nir_texop_sparse_residency_txf_intel :
                       nir_texop_sparse_residency_intel;
   }

   /* txl/txb/tex and tg4 both access the same pixels for residency checking
    * purposes, but using the former for residency-only queries lets us mask
    * out unwanted color components, using fewer registers.
    */
   if (tex->op == nir_texop_tg4) {
      if (!sparse_tex->is_gather_implicit_lod) {
         /* Add explicit LOD 0 */
         nir_builder bb = nir_builder_at(nir_after_instr(&tex->instr));
         nir_tex_instr_add_src(sparse_tex, nir_tex_src_lod,
                               nir_imm_int(&bb, 0));
      } else {
         assert(nir_tex_instr_src_index(sparse_tex, nir_tex_src_lod) == -1);
      }

      if (jay)
         ;
      else if (nir_tex_instr_src_index(sparse_tex, nir_tex_src_bias) >= 0)
         sparse_tex->op = nir_texop_txb;
      else if (sparse_tex->is_gather_implicit_lod)
         sparse_tex->op = nir_texop_tex;
      else
         sparse_tex->op = nir_texop_txl;

      sparse_tex->component = 0;
      sparse_tex->is_gather_implicit_lod = false;
   }

   /* Drop the compare source on the cloned instruction */
   if (compare_idx != -1)
      nir_tex_instr_remove_src(sparse_tex, compare_idx);

   /* Drop the residency query on the original tex instruction */
   tex->is_sparse = false;
   tex->def.num_components = tex->def.num_components - 1;

   nir_def *new_comps[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < tex->def.num_components; i++)
      new_comps[i] = nir_channel(b, &tex->def, i);
   new_comps[tex->def.num_components] =
      nir_channel(b, &sparse_tex->def, sparse_tex->def.num_components - 1);

   nir_def *new_vec = nir_vec(b, new_comps, tex->def.num_components + 1);

   nir_def_rewrite_uses_after(&tex->def, new_vec);
   return true;
}

static bool
lower_sparse_intrinsics(nir_builder *b, nir_instr *instr, void *cb_data)
{
   const bool jay = (uintptr_t) cb_data;

   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_image_sparse_load:
      case nir_intrinsic_bindless_image_sparse_load:
         lower_sparse_image_load(b, intrin, jay);
         return true;

      case nir_intrinsic_is_sparse_texels_resident:
         lower_is_sparse_texels_resident(b, intrin, jay);
         return true;

      case nir_intrinsic_sparse_residency_code_and:
         lower_sparse_residency_code_and(b, intrin);
         return true;

      default:
         return false;
      }
   }

   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      return tex->is_sparse && split_tex_residency(b, tex, jay);
   }

   default:
      return false;
   }
}

bool
intel_nir_lower_sparse_intrinsics(nir_shader *nir, bool jay)
{
   return nir_shader_instructions_pass(nir, lower_sparse_intrinsics,
                                       nir_metadata_control_flow,
                                       (void *)(uintptr_t)jay);
}
