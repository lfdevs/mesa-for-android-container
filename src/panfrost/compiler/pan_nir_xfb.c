/*
 * Copyright (c) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"

static bool
remove_xfb(UNUSED nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

   if (sem.no_varying) {
      nir_instr_remove(&intr->instr);
   } else {
      nir_io_xfb xfb = {0};
      nir_intrinsic_set_io_xfb(intr, xfb);
   }

   return true;
}

bool
pan_nir_remove_xfb(nir_shader *nir)
{
   if (!nir->info.has_transform_feedback_varyings)
      return false;

   nir_shader_intrinsics_pass(
      nir, remove_xfb, nir_metadata_control_flow, NULL);

   /* Strip remaining XFB info */
   nir->info.has_transform_feedback_varyings = false;
   ralloc_free(nir->xfb_info);
   nir->xfb_info = NULL;

   memset(nir->info.xfb_stride, 0, sizeof(nir->info.xfb_stride));

   return true;
}
