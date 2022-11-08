/*
 * Copyright (C) 2022 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright © 2021 Valve Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

static bool
nir_scalarize_preamble(struct nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_preamble &&
       intr->intrinsic != nir_intrinsic_store_preamble)
      return false;

   bool is_load = (intr->intrinsic == nir_intrinsic_load_preamble);

   nir_ssa_def *v = is_load ? &intr->dest.ssa :
                    nir_ssa_for_src(b, intr->src[0], nir_src_num_components(intr->src[0]));

   if (v->num_components == 1)
      return false;

   /* Scalarize */
   b->cursor = nir_before_instr(&intr->instr);
   unsigned stride = MAX2(v->bit_size / 16, 1);
   unsigned base = nir_intrinsic_base(intr);

   if (is_load) {
      nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < v->num_components; ++i)
         comps[i] = nir_load_preamble(b, 1, v->bit_size, .base = base + (i * stride));

      nir_ssa_def_rewrite_uses(v, nir_vec(b, comps, v->num_components));
   } else {
      for (unsigned i = 0; i < v->num_components; ++i)
         nir_store_preamble(b, nir_channel(b, v, i), .base = base + (i * stride));

      nir_instr_remove(instr);
   }

   return true;
}

static void
def_size(nir_ssa_def *def, unsigned *size, unsigned *align)
{
   unsigned bit_size = MAX2(def->bit_size, 16);

   *size = (bit_size * def->num_components) / 16;
   *align = bit_size / 16;
}

static float
instr_cost(nir_instr *instr, const void *data)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic:
      switch (nir_instr_as_intrinsic(instr)->intrinsic) {
      case nir_intrinsic_load_global:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_load_ubo:
         return 10.0;
      default:
         /* Assume it's a sysval or something */
         return 0.0;
      }

   case nir_instr_type_tex:
      /* Texturing involes lots of memory bandwidth */
      return 20.0;

   case nir_instr_type_alu:
      /* We optimistically assume that moves get coalesced */
      if (nir_op_is_vec(nir_instr_as_alu(instr)->op))
         return 0.0;
      else
         return 2.0;

   default:
      return 1.0;
   }
}

static float
rewrite_cost(nir_ssa_def *def, const void *data)
{
   bool mov_needed = false;
   nir_foreach_use (use, def) {
      nir_instr *parent_instr = use->parent_instr;
      if (parent_instr->type != nir_instr_type_alu) {
         mov_needed = true;
         break;
      } else {
         nir_alu_instr *alu = nir_instr_as_alu(parent_instr);
         if (alu->op == nir_op_vec2 ||
             alu->op == nir_op_vec3 ||
             alu->op == nir_op_vec4 ||
             alu->op == nir_op_mov) {
            mov_needed = true;
            break;
         } else {
            /* Assume for non-moves that the const is folded into the src */
         }
      }
   }

   return mov_needed ? def->num_components : 0;
}

static bool
avoid_instr(const nir_instr *instr, const void *data)
{
   return false;
}

static const nir_opt_preamble_options preamble_options = {
   .drawid_uniform = true,
   .subgroup_size_uniform = true,
   .def_size = def_size,
   .instr_cost_cb = instr_cost,
   .rewrite_cost_cb = rewrite_cost,
   .avoid_instr_cb = avoid_instr,

   /* Storage size 16-bit words. Should be 512, but we push some sysvals ad hoc
    * in the backend compiler, so we need to reserve space. When we move UBO and
    * VBO lowering to NIR, this can be bumped up to 512.
    */
   .preamble_storage_size = 256,
};

bool
agx_nir_opt_preamble(nir_shader *nir, unsigned *preamble_size)
{
   bool progress = nir_opt_preamble(nir, &preamble_options, preamble_size);

   /* If nir_opt_preamble made progress, the shader now has
    * load_preamble/store_preamble intrinsics in it. These need to be
    * scalarized for the backend to process them appropriately.
    */
   if (progress) {
      nir_shader_instructions_pass(nir, nir_scalarize_preamble,
                                   nir_metadata_block_index |
                                   nir_metadata_dominance, NULL);
   }

   return progress;
}
