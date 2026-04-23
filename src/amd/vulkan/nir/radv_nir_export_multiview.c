/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "radv_nir.h"

static void
export_multiview(nir_builder *b)
{
   nir_io_semantics io_sem = {
      .location = VARYING_SLOT_LAYER,
      .num_slots = 1,
      .no_varying = true,
   };

   nir_store_output(b, nir_load_view_index(b), nir_imm_int(b, 0), .component = 0, .src_type = nir_type_int32,
                    .io_semantics = io_sem);

   b->shader->info.outputs_written |= VARYING_BIT_LAYER;
}

static bool
export_multiview_per_emit(nir_builder *b, nir_intrinsic_instr *intr, void *unused)
{
   if (intr->intrinsic != nir_intrinsic_emit_vertex_with_counter)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   export_multiview(b);
   return true;
}

bool
radv_nir_export_multiview(nir_shader *nir)
{
   /* This pass is not suitable for mesh shaders, because it can't know the mapping between API mesh
    * shader invocations and output primitives. Needs to be handled in ac_nir_lower_ngg.
    */
   assert(nir->info.stage == MESA_SHADER_VERTEX || nir->info.stage == MESA_SHADER_TESS_EVAL ||
          nir->info.stage == MESA_SHADER_GEOMETRY);

   /* The shader shouldn't write gl_Layer itself. */
   assert(!(nir->info.outputs_written & VARYING_BIT_LAYER));

   if (nir->info.stage == MESA_SHADER_GEOMETRY) {
      /* For geometry shaders, the layer is injected right before every emit_vertex_with_counter. */
      return nir_shader_intrinsics_pass(nir, export_multiview_per_emit, nir_metadata_control_flow, NULL);
   } else {
      nir_function_impl *impl = nir_shader_get_entrypoint(nir);

      nir_builder b = nir_builder_at(nir_after_impl(impl));
      export_multiview(&b);

      return nir_progress(true, impl, nir_metadata_control_flow);
   }
}
