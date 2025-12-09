/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_nir_rt.h"

static bool
add_intrinsics_to_set(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      void *data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ray_world_origin:
   case nir_intrinsic_load_ray_world_direction:
   case nir_intrinsic_load_ray_object_origin:
   case nir_intrinsic_load_ray_object_direction:
   case nir_intrinsic_load_ray_t_min:
   case nir_intrinsic_load_ray_t_max:
   case nir_intrinsic_load_ray_object_to_world:
   case nir_intrinsic_load_ray_flags: {
      struct set *intrinsics = data;
      _mesa_set_add(intrinsics, intrin);
      return false;
   }

   default:
      return false;
   }
}

/** Move some RT intrinsics pre shader calls
 *
 * Some RT intrinsics are implemented by reading the ray structure in memory.
 * This structure is written just before triggering a tracing call. Those
 * intrinsics should be considered as input values to the shader (kind of like
 * thread payload in legacy stages).
 *
 * If the values are read after a bindless shader call, we need to move the
 * read from memory prior to the call because called shaders could overwrite
 * the memory location if they trigger another tracing call.
 *
 * Call this pass before nir_lower_shader_calls().
 */
bool
brw_nir_lower_rt_intrinsics_pre_trace(nir_shader *nir)
{
   /* According to spec, only those stages can do a recursing traceRayEXT().
    */
   if (nir->info.stage != MESA_SHADER_CLOSEST_HIT &&
       nir->info.stage != MESA_SHADER_MISS)
      return false;

   /* Find all the intrinsics we might need to move */
   struct set *intrinsics = _mesa_set_create(NULL,
                                             _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);

   nir_shader_intrinsics_pass(nir,
                              add_intrinsics_to_set,
                              nir_metadata_all,
                              intrinsics);

   bool progress = false;

   if (intrinsics->entries > 0) {
      nir_foreach_function_with_impl(func, impl, nir) {
         nir_metadata_require(impl, nir_metadata_dominance |
                                    nir_metadata_dominance_lca);

         /* Going in reverse order of blocks, move the intrinsics gather above
          * in the LCA block to trace calls.
          */
         nir_foreach_block_reverse(block, impl) {
            nir_foreach_instr_reverse_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *trace = nir_instr_as_intrinsic(instr);
               if (trace->intrinsic != nir_intrinsic_trace_ray)
                  continue;

               set_foreach(intrinsics, entry) {
                  nir_intrinsic_instr *intrin = (void *)entry->key;

                  /* Coming from a different function, ignore */
                  if (nir_cf_node_get_function(&intrin->instr.block->cf_node) != impl)
                     continue;

                  /* The trace dominates the intrinsic, move it before */
                  nir_block *move_block = nir_dominance_lca(trace->instr.block,
                                                            intrin->instr.block);
                  if (move_block == trace->instr.block) {
                     if (nir_instr_is_before(&trace->instr, &intrin->instr)) {
                        nir_instr_move(nir_before_instr(&trace->instr),
                                       &intrin->instr);
                        progress = true;
                     }
                  } else {
                     nir_instr_move(nir_before_block_after_phis(move_block),
                                    &intrin->instr);
                     progress = true;
                  }
               }
            }
         }
      }
   }

   _mesa_set_destroy(intrinsics, NULL);

   return progress;
}
