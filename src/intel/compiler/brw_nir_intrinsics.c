/*
 * Copyright (c) 2016 Intel Corporation
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

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"

struct lower_intrinsics_state {
   nir_shader *nir;
   union {
      struct brw_stage_prog_data *prog_data;
      struct brw_cs_prog_data *cs_prog_data;
   };
   nir_function_impl *impl;
   bool progress;
   nir_builder builder;
   bool cs_thread_id_used;
};

static nir_ssa_def *
read_thread_local_id(struct lower_intrinsics_state *state)
{
   nir_builder *b = &state->builder;
   nir_shader *nir = state->nir;
   const unsigned *sizes = nir->info->cs.local_size;
   const unsigned group_size = sizes[0] * sizes[1] * sizes[2];

   /* Some programs have local_size dimensions so small that the thread local
    * ID will always be 0.
    */
   if (group_size <= 8)
      return nir_imm_int(b, 0);

   assert(state->cs_prog_data->thread_local_id_index >= 0);
   state->cs_thread_id_used = true;
   const int id_index = state->cs_prog_data->thread_local_id_index;

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(nir, nir_intrinsic_load_uniform);
   load->num_components = 1;
   load->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
   nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, NULL);
   nir_intrinsic_set_base(load, id_index * sizeof(uint32_t));
   nir_intrinsic_set_range(load, sizeof(uint32_t));
   nir_builder_instr_insert(b, &load->instr);
   return &load->dest.ssa;
}

static bool
lower_cs_intrinsics_convert_block(struct lower_intrinsics_state *state,
                                  nir_block *block)
{
   bool progress = false;
   nir_builder *b = &state->builder;
   nir_shader *nir = state->nir;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

      b->cursor = nir_after_instr(&intrinsic->instr);

      nir_ssa_def *sysval;
      switch (intrinsic->intrinsic) {
      case nir_intrinsic_load_local_invocation_index: {
         assert(nir->stage == MESA_SHADER_COMPUTE);
         /* We construct the local invocation index from:
          *
          *    gl_LocalInvocationIndex =
          *       cs_thread_local_id + channel_num;
          */
         nir_ssa_def *thread_local_id = read_thread_local_id(state);
         nir_ssa_def *channel = nir_load_channel_num(b);
         sysval = nir_iadd(b, channel, thread_local_id);
         break;
      }

      case nir_intrinsic_load_local_invocation_id: {
         assert(nir->stage == MESA_SHADER_COMPUTE);
         /* We lower gl_LocalInvocationID from gl_LocalInvocationIndex based
          * on this formula:
          *
          *    gl_LocalInvocationID.x =
          *       gl_LocalInvocationIndex % gl_WorkGroupSize.x;
          *    gl_LocalInvocationID.y =
          *       (gl_LocalInvocationIndex / gl_WorkGroupSize.x) %
          *       gl_WorkGroupSize.y;
          *    gl_LocalInvocationID.z =
          *       (gl_LocalInvocationIndex /
          *        (gl_WorkGroupSize.x * gl_WorkGroupSize.y)) %
          *       gl_WorkGroupSize.z;
          */
         unsigned *size = nir->info->cs.local_size;

         nir_ssa_def *local_index = nir_load_local_invocation_index(b);

         nir_const_value uvec3;
         uvec3.u32[0] = 1;
         uvec3.u32[1] = size[0];
         uvec3.u32[2] = size[0] * size[1];
         nir_ssa_def *div_val = nir_build_imm(b, 3, 32, uvec3);
         uvec3.u32[0] = size[0];
         uvec3.u32[1] = size[1];
         uvec3.u32[2] = size[2];
         nir_ssa_def *mod_val = nir_build_imm(b, 3, 32, uvec3);

         sysval = nir_umod(b, nir_udiv(b, local_index, div_val), mod_val);
         break;
      }

      default:
         continue;
      }

      nir_ssa_def_rewrite_uses(&intrinsic->dest.ssa, nir_src_for_ssa(sysval));
      nir_instr_remove(&intrinsic->instr);

      state->progress = true;
   }

   return progress;
}

static void
lower_cs_intrinsics_convert_impl(struct lower_intrinsics_state *state)
{
   nir_builder_init(&state->builder, state->impl);

   nir_foreach_block(block, state->impl) {
      lower_cs_intrinsics_convert_block(state, block);
   }

   nir_metadata_preserve(state->impl,
                         nir_metadata_block_index | nir_metadata_dominance);
}

bool
brw_nir_lower_intrinsics(nir_shader *nir, struct brw_stage_prog_data *prog_data)
{
   /* Currently we only lower intrinsics for compute shaders */
   if (nir->stage != MESA_SHADER_COMPUTE)
      return false;

   bool progress = false;
   struct lower_intrinsics_state state;
   memset(&state, 0, sizeof(state));
   state.nir = nir;
   state.prog_data = prog_data;

   do {
      state.progress = false;
      nir_foreach_function(function, nir) {
         if (function->impl) {
            state.impl = function->impl;
            lower_cs_intrinsics_convert_impl(&state);
         }
      }
      progress |= state.progress;
   } while (state.progress);

   if (nir->stage == MESA_SHADER_COMPUTE && !state.cs_thread_id_used)
      state.cs_prog_data->thread_local_id_index = -1;

   return progress;
}
