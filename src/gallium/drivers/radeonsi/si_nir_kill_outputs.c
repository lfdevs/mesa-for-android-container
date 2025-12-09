/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "nir_builder.h"

bool si_nir_kill_outputs(nir_shader *nir, const union si_shader_key *key)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   assert(impl);
   assert(nir->info.stage <= MESA_SHADER_GEOMETRY ||
          nir->info.stage == MESA_SHADER_MESH);

   if (!key->ge.opt.kill_outputs &&
       !key->ge.opt.kill_pointsize &&
       !key->ge.opt.kill_layer &&
       !key->ge.opt.kill_clip_distances &&
       !(nir->info.outputs_written & VARYING_BIT_LAYER) &&
       !key->ge.opt.remove_streamout &&
       !key->ge.mono.remove_streamout) {
      return nir_no_progress(impl);
   }

   bool progress = false;

   if ((key->ge.opt.remove_streamout || key->ge.mono.remove_streamout) && nir->xfb_info) {
      ralloc_free(nir->xfb_info);
      nir->xfb_info = NULL;
   }

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_store_output)
            continue;

         /* No indirect indexing allowed. */
         ASSERTED nir_src offset = *nir_get_io_offset_src(intr);
         assert(nir_src_is_const(offset) && nir_src_as_uint(offset) == 0);

         assert(intr->num_components == 1); /* only scalar stores expected */
         nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

         if ((key->ge.opt.remove_streamout || key->ge.mono.remove_streamout) &&
             nir_instr_xfb_write_mask(intr)) {
            /* Remove the output store if the output is not used as a sysval or varying. */
            if ((sem.no_sysval_output ||
                 !nir_slot_is_sysval_output(sem.location, MESA_SHADER_FRAGMENT)) &&
                (sem.no_varying ||
                 !nir_slot_is_varying(sem.location, MESA_SHADER_FRAGMENT))) {
               nir_instr_remove(instr);
               progress = true;
               continue;
            }

            /* Clear xfb info if the output is used as a sysval or varying. */
            static const nir_io_xfb zeroed;
            nir_intrinsic_set_io_xfb(intr, zeroed);
            nir_intrinsic_set_io_xfb2(intr, zeroed);
            progress = true;
         }

         if (nir_slot_is_varying(sem.location, MESA_SHADER_FRAGMENT) &&
             key->ge.opt.kill_outputs &
             (1ull << si_shader_io_get_unique_index(sem.location)))
            progress |= nir_remove_varying(intr, MESA_SHADER_FRAGMENT);

         switch (sem.location) {
         case VARYING_SLOT_PSIZ:
            if (key->ge.opt.kill_pointsize)
               progress |= nir_remove_sysval_output(intr, MESA_SHADER_FRAGMENT);
            break;

         case VARYING_SLOT_CLIP_VERTEX:
            if (key->ge.opt.kill_clip_distances == SI_USER_CLIP_PLANE_MASK)
               progress |= nir_remove_sysval_output(intr, MESA_SHADER_FRAGMENT);
            break;

         case VARYING_SLOT_CLIP_DIST0:
         case VARYING_SLOT_CLIP_DIST1:
            if (key->ge.opt.kill_clip_distances) {
               assert(nir_intrinsic_src_type(intr) == nir_type_float32);
               unsigned index = (sem.location - VARYING_SLOT_CLIP_DIST0) * 4 +
                                nir_intrinsic_component(intr);

               if (key->ge.opt.kill_clip_distances & BITFIELD_BIT(index))
                  progress |= nir_remove_sysval_output(intr, MESA_SHADER_FRAGMENT);
            }
            break;

         case VARYING_SLOT_LAYER:
            /* LAYER is never passed to FS. Instead, we load it there as a system value. */
            progress |= nir_remove_varying(intr, MESA_SHADER_FRAGMENT);

            if (key->ge.opt.kill_layer)
               progress |= nir_remove_sysval_output(intr, MESA_SHADER_FRAGMENT);
            break;
         }
      }
   }

   return nir_progress(progress, impl, nir_metadata_control_flow);
}
