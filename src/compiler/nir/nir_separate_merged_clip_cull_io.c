/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/* Turn this (assuming clip_distance_array_size=3):
 *
 *    store_output(...) (component=0, io location=VARYING_SLOT_CLIP_DIST0)
 *    store_output(...) (component=1, io location=VARYING_SLOT_CLIP_DIST0)
 *    store_output(...) (component=2, io location=VARYING_SLOT_CLIP_DIST0)
 *    store_output(...) (component=3, io location=VARYING_SLOT_CLIP_DIST0)
 *    store_output(...) (component=0, io location=VARYING_SLOT_CLIP_DIST1)
 *    store_output(...) (component=1, io location=VARYING_SLOT_CLIP_DIST1)
 *    store_output(...) (component=2, io location=VARYING_SLOT_CLIP_DIST1)
 *    store_output(...) (component=3, io location=VARYING_SLOT_CLIP_DIST1)
 *
 * into this:
 *
 *    store_output(...) (component=0, io location=VARYING_SLOT_CLIP_DIST0) - same
 *    store_output(...) (component=1, io location=VARYING_SLOT_CLIP_DIST0) - same
 *    store_output(...) (component=2, io location=VARYING_SLOT_CLIP_DIST0) - same
 *    store_output(...) (component=0, io location=VARYING_SLOT_CULL_DIST0) - relocated
 *    store_output(...) (component=1, io location=VARYING_SLOT_CULL_DIST0) - relocated
 *    store_output(...) (component=2, io location=VARYING_SLOT_CULL_DIST0) - relocated
 *    store_output(...) (component=3, io location=VARYING_SLOT_CULL_DIST0) - relocated
 *    store_output(...) (component=0, io location=VARYING_SLOT_CULL_DIST1) - relocated
 *
 * The pass trivially relocates cull distance components that were merged with
 * CLIP_DIST back to their own separate CULL_DIST slots by changing their
 * locations. IO must be scalar.
 */

#include "nir_builder.h"

static bool
split_clip_cull_arrays(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (!nir_intrinsic_has_io_semantics(intr))
      return false;

   /* Skip VS inputs. */
   if (nir_is_input_load(intr) && b->shader->info.stage == MESA_SHADER_VERTEX)
      return false;

   /* Skip FS outputs. */
   if (!nir_is_input_load(intr) && b->shader->info.stage == MESA_SHADER_FRAGMENT)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   unsigned component = nir_intrinsic_component(intr);

   /* Clip and cull arrays are expected to be merged in CLIP_DISTn. */
   assert(sem.location != VARYING_SLOT_CULL_DIST0 &&
          sem.location != VARYING_SLOT_CULL_DIST1);

   if (sem.location != VARYING_SLOT_CLIP_DIST0 &&
       sem.location != VARYING_SLOT_CLIP_DIST1)
      return false;

   /* IO must be scalar. */
   assert((nir_intrinsic_infos[intr->intrinsic].has_dest ?
              intr->def.num_components :
              intr->src[0].ssa->num_components) == 1);

   /* If there is indirect slot indexing, this is the location of first
    * element.
    */
   unsigned index = (sem.location - VARYING_SLOT_CLIP_DIST0) * 4 + component;

   /* Nothing to do if this component is a clip distance. */
   if (index < b->shader->info.clip_distance_array_size)
      return false;

   unsigned cull_dist_index = index - b->shader->info.clip_distance_array_size;

   sem.location = VARYING_SLOT_CULL_DIST0 + cull_dist_index / 4;
   component = cull_dist_index % 4;

   nir_intrinsic_set_io_semantics(intr, sem);
   nir_intrinsic_set_component(intr, component);
   return true;
}

bool
nir_separate_merged_clip_cull_io(nir_shader *nir)
{
   assert(nir->options->compact_arrays);

   return nir_shader_intrinsics_pass(nir, split_clip_cull_arrays,
                                     nir_metadata_control_flow, NULL);
}
