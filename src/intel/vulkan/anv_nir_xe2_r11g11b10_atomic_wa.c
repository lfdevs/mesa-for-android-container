/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_nir.h"

#include "nir/nir_builder.h"
#include "nir/nir_builtin_builder.h"

/* Up until Xe2, the dataport returned a RAW32 value for accesses on
 * R11G11B10_FLOAT (see ATSM PRMs, Volume 9: Render Engine, Supported Typed
 * Surface Read Formats).
 *
 * On Xe2, it seems the read goes through as 3 different components and the
 * first component is R11 scaled to a 32bit float (see BSpec 57051).
 *
 * Some applications are doing atomic 32bits operations on a R11G11B10_FLOAT
 * surface (which as far as we know is not legal). To workaround this
 * particular case of atomic cmpxchg, we set the compare value to -NaN which
 * hopefully isn't going to be in the image. This (again hopefully) prevents
 * the exchange to take place. We also return the compare value which lets the
 * shader think that's what was in memory.
 */

static bool
r11g11b10_atomic_swap_wa_instr(nir_builder *b,
                               nir_intrinsic_instr *intrin,
                               void *data)
{
   if (intrin->intrinsic != nir_intrinsic_image_deref_atomic_swap)
      return false;

   if (nir_intrinsic_atomic_op(intrin) != nir_atomic_op_cmpxchg &&
       nir_intrinsic_atomic_op(intrin) != nir_atomic_op_fcmpxchg)
      return false;

   /* R11G11B10_FLOAT only maps to a 32bit atomic */
   if (intrin->def.bit_size != 32)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *isl_format = nir_image_deref_load_param_intel(
      b, 1, 32, intrin->src[0].ssa, .base = ISL_SURF_PARAM_FORMAT);
   nir_def *is_r11g11b10 =
      nir_ieq_imm(b, isl_format, ISL_FORMAT_R11G11B10_FLOAT);
   nir_def *orig_cmp_val = intrin->src[3].ssa;


   nir_def *cmp_val = nir_bcsel(b, is_r11g11b10,
                                nir_imm_int(b, 0xffffffff),
                                orig_cmp_val);

   nir_src_rewrite(&intrin->src[3], cmp_val);

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *new_val = nir_bcsel(b, is_r11g11b10, orig_cmp_val, &intrin->def);

   nir_def_rewrite_uses_after_instr(&intrin->def,
                                    new_val, nir_def_instr(new_val));

   return true;
}

bool
anv_nir_xe2_r11g11b10_atomic_swap_wa(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir,
                                     r11g11b10_atomic_swap_wa_instr,
                                     nir_metadata_control_flow,
                                     NULL);
}
