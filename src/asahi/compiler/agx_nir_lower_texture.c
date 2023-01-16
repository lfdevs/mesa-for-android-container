/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2020 Collabora Ltd.
 * Copyright © 2016 Broadcom
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

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_builtin_builder.h"
#include "agx_compiler.h"

static nir_ssa_def *
steal_tex_src(nir_tex_instr *tex, nir_tex_src_type type_)
{
   int idx = nir_tex_instr_src_index(tex, type_);

   if (idx < 0)
      return NULL;

   nir_ssa_def *ssa = tex->src[idx].src.ssa;
   nir_tex_instr_remove_src(tex, idx);
   return ssa;
}

/*
 * NIR indexes into array textures with unclamped floats (integer for txf). AGX
 * requires the index to be a clamped integer. Lower tex_src_coord into
 * tex_src_backend1 for array textures by type-converting and clamping.
 */
static bool
lower_array_texture(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   if (nir_tex_instr_is_query(tex))
      return false;

   /* Get the coordinates */
   nir_ssa_def *coord = steal_tex_src(tex, nir_tex_src_coord);
   nir_ssa_def *ms_idx = steal_tex_src(tex, nir_tex_src_ms_index);

   /* The layer is always the last component of the NIR coordinate, split it off
    * because we'll need to swizzle.
    */
   nir_ssa_def *layer = NULL;

   if (tex->is_array) {
      unsigned lidx = coord->num_components - 1;
      nir_ssa_def *unclamped_layer = nir_channel(b, coord, lidx);
      coord = nir_trim_vector(b, coord, lidx);

      /* Round layer to nearest even */
      if (tex->op != nir_texop_txf && tex->op != nir_texop_txf_ms)
         unclamped_layer = nir_f2u32(b, nir_fround_even(b, unclamped_layer));

      /* Clamp to max layer = (# of layers - 1) for out-of-bounds handling.
       * Layer must be 16-bits for the hardware, drop top bits after clamping.
       */
      nir_ssa_def *txs = nir_get_texture_size(b, tex);
      nir_ssa_def *nr_layers = nir_channel(b, txs, lidx);
      nir_ssa_def *max_layer = nir_iadd_imm(b, nr_layers, -1);
      layer = nir_u2u16(b, nir_umin(b, unclamped_layer, max_layer));
   }

   /* Combine layer and multisample index into 32-bit so we don't need a vec5 or
    * vec6 16-bit coordinate tuple, which would be inconvenient in NIR for
    * little benefit (a minor optimization, I guess).
    */
   nir_ssa_def *sample_array = (ms_idx && layer)
                                  ? nir_pack_32_2x16_split(b, ms_idx, layer)
                               : ms_idx ? nir_u2u32(b, ms_idx)
                               : layer  ? nir_u2u32(b, layer)
                                        : NULL;

   /* Combine into the final 32-bit tuple */
   if (sample_array != NULL) {
      unsigned end = coord->num_components;
      coord = nir_pad_vector(b, coord, end + 1);
      coord = nir_vector_insert_imm(b, coord, sample_array, end);
   }

   nir_tex_instr_add_src(tex, nir_tex_src_backend1, nir_src_for_ssa(coord));
   return true;
}

bool
agx_nir_lower_array_texture(nir_shader *s)
{
   return nir_shader_instructions_pass(
      s, lower_array_texture, nir_metadata_block_index | nir_metadata_dominance,
      NULL);
}
