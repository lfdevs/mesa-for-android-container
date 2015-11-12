/*
 * Copyright © 2014 Intel Corporation
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
 *
 * Authors:
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#include "nir.h"

/*
 * Implements a simple pass that lowers vecN instructions to a series of
 * moves with partial writes.
 */

struct vec_to_movs_state {
   nir_function_impl *impl;
   bool progress;
};

static bool
src_matches_dest_reg(nir_dest *dest, nir_src *src)
{
   if (dest->is_ssa || src->is_ssa)
      return false;

   return (dest->reg.reg == src->reg.reg &&
           dest->reg.base_offset == src->reg.base_offset &&
           !dest->reg.indirect &&
           !src->reg.indirect);
}

/**
 * For a given starting writemask channel and corresponding source index in
 * the vec instruction, insert a MOV to the vec instruction's dest of all the
 * writemask channels that get read from the same src reg.
 *
 * Returns the writemask of our MOV, so the parent loop calling this knows
 * which ones have been processed.
 */
static unsigned
insert_mov(nir_alu_instr *vec, unsigned start_channel,
           unsigned start_src_idx, nir_shader *shader)
{
   unsigned src_idx = start_src_idx;
   assert(src_idx < nir_op_infos[vec->op].num_inputs);

   nir_alu_instr *mov = nir_alu_instr_create(shader, nir_op_imov);
   nir_alu_src_copy(&mov->src[0], &vec->src[src_idx], mov);
   nir_alu_dest_copy(&mov->dest, &vec->dest, mov);

   mov->dest.write_mask = (1u << start_channel);
   mov->src[0].swizzle[start_channel] = vec->src[src_idx].swizzle[0];
   src_idx++;

   for (unsigned i = start_channel + 1; i < 4; i++) {
      if (!(vec->dest.write_mask & (1 << i)))
         continue;

      if (nir_srcs_equal(vec->src[src_idx].src, vec->src[start_src_idx].src)) {
         mov->dest.write_mask |= (1 << i);
         mov->src[0].swizzle[i] = vec->src[src_idx].swizzle[0];
      }
      src_idx++;
   }

   nir_instr_insert_before(&vec->instr, &mov->instr);

   return mov->dest.write_mask;
}

static bool
lower_vec_to_movs_block(nir_block *block, void *void_state)
{
   struct vec_to_movs_state *state = void_state;
   nir_function_impl *impl = state->impl;
   nir_shader *shader = impl->overload->function->shader;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *vec = nir_instr_as_alu(instr);

      switch (vec->op) {
      case nir_op_vec2:
      case nir_op_vec3:
      case nir_op_vec4:
         break;
      default:
         continue; /* The loop */
      }

      /* Since we insert multiple MOVs, we have to be non-SSA. */
      assert(!vec->dest.dest.is_ssa);

      unsigned finished_write_mask = 0;

      /* First, emit a MOV for all the src channels that are in the
       * destination reg, in case other values we're populating in the dest
       * might overwrite them.
       */
      for (unsigned i = 0, src_idx = 0; i < 4; i++) {
         if (!(vec->dest.write_mask & (1 << i)))
            continue;

         if (src_matches_dest_reg(&vec->dest.dest, &vec->src[src_idx].src)) {
            finished_write_mask |= insert_mov(vec, i, src_idx, shader);
            break;
         }
         src_idx++;
      }

      /* Now, emit MOVs for all the other src channels. */
      for (unsigned i = 0, src_idx = 0; i < 4; i++) {
         if (!(vec->dest.write_mask & (1 << i)))
            continue;

         if (!(finished_write_mask & (1 << i)))
            finished_write_mask |= insert_mov(vec, i, src_idx, shader);

         src_idx++;
      }

      nir_instr_remove(&vec->instr);
      ralloc_free(vec);
      state->progress = true;
   }

   return true;
}

static bool
nir_lower_vec_to_movs_impl(nir_function_impl *impl)
{
   struct vec_to_movs_state state = { impl, false };

   nir_foreach_block(impl, lower_vec_to_movs_block, &state);

   if (state.progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   }

   return state.progress;
}

bool
nir_lower_vec_to_movs(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         progress = nir_lower_vec_to_movs_impl(overload->impl) || progress;
   }

   return progress;
}
