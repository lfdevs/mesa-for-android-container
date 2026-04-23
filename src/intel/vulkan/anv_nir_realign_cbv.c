/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_nir.h"
#include "nir/nir_builder.h"

/**
 * This file implements a pass that looks for global read-only loads, from a
 * pointer in the push constant data and based on the block size (64KiB
 * indicating a CBV resource), align the load to 256B which the alignment
 * guarantee the applications should make. This alignment guarantee can later
 * be used to promote those 64bit pointers to push buffers (HW needs 32B
 * alignment).
 */

static bool
realign_cbv(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_deref)
      return false;

   /* If writable, it's not CBV. */
   if ((nir_intrinsic_access(intrin) & ACCESS_NON_WRITEABLE) == 0)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   /* Find the root of the deref to see if it's a pointer in the push constant
    * data.
    */
   while (true) {
      if (deref->deref_type == nir_deref_type_var)
         return false;

      nir_deref_instr *parent = nir_src_as_deref(deref->parent);
      if (!parent)
         break;

      deref = parent;
   }
   assert(deref->deref_type == nir_deref_type_cast);

   /* This is the magic value vkd3d-proton puts allowing us to recognize a
    * CBV.
    */
   if (glsl_get_explicit_size(deref->type, true) != 64 * 1024)
      return false;

   nir_scalar val = { deref->parent.ssa, 0 };

   if (nir_scalar_is_alu(val)) {
      nir_alu_instr *pack_alu = nir_def_as_alu(val.def);
      if (pack_alu->op != nir_op_pack_64_2x32_split)
         return false;

      val = (nir_scalar){ pack_alu->src[0].src.ssa, pack_alu->src[0].swizzle[0] };
   }

   if (!nir_scalar_is_intrinsic(val))
      return false;

   /* If it's not a value coming from the push constant data, give up. */
   nir_intrinsic_instr *push_intrin = nir_def_as_intrinsic(val.def);
   if (push_intrin->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   /* Realign to the CBV requirement */
   deref = nir_src_as_deref(intrin->src[0]);
   deref->cast.align_mul = 256;

   return true;
}

bool
anv_nir_realign_cbv(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, realign_cbv, nir_metadata_all, NULL);
}
