/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "si_gfx.h"
#include "nir.h"
#include "nir/nir_xfb_info.h"

static bool si_nir_can_lower_mediump_io(mesa_shader_stage prev_stage, bool prev_stage_has_xfb,
                                 mesa_shader_stage next_stage, bool config_option)
{
   /* This is the filter that determines when mediump IO is lowered.
    *
    * NOTE: LLVM fails to compile this test if VS inputs are 16-bit:
    * dEQP-GLES31.functional.shaders.builtin_functions.integer.bitfieldinsert.uvec3_lowp_geometry
    */
   return (prev_stage == MESA_SHADER_VERTEX && next_stage == MESA_SHADER_FRAGMENT &&
           !prev_stage_has_xfb && config_option) ||
          prev_stage == MESA_SHADER_FRAGMENT;
}

static void si_nir_lower_mediump_io(nir_shader *nir, bool config_option)
{
   nir_variable_mode modes = 0;

   if (si_nir_can_lower_mediump_io(nir->info.stage, nir->xfb_info != NULL, nir->info.next_stage,
                            config_option))
      modes |= nir_var_shader_out;

   if (si_nir_can_lower_mediump_io(nir->info.prev_stage, nir->info.prev_stage_has_xfb, nir->info.stage,
                            config_option))
      modes |= nir_var_shader_in;

   if (modes) {
      bool progress = false;

      NIR_PASS(progress, nir, nir_lower_mediump_io, modes,
               VARYING_BIT_PNTC | BITFIELD64_RANGE(VARYING_SLOT_VAR0, 32), true);

      /* Update xfb info after mediump IO lowering. */
      if (progress && nir->xfb_info)
         nir_gather_xfb_info_from_intrinsics(nir);
   }
   NIR_PASS(_, nir, nir_clear_mediump_io_flag);
}

void si_nir_lower_mediump_io_default(nir_shader *nir)
{
   si_nir_lower_mediump_io(nir, false);
}

void si_nir_lower_mediump_io_option(nir_shader *nir)
{
   si_nir_lower_mediump_io(nir, true);
}
