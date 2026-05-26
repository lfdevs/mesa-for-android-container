/*
 * Copyright (c) 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "brw_nir.h"

static nir_def *
load_expected_mask(nir_builder *b)
{
   nir_def *sample_count = nir_load_msaa_rate_intel(b);

   /* A coarse fragment is fully covered if all the pixels that compose it are
    * fully covered themselves.
    * If we are not dealing with coarse pixels, frag_size will be 1x1, and so
    * coarse_sample_count == sample_count.
    */
   nir_def *frag_size = nir_load_frag_shading_rate_intel(b);
   nir_def *coarse_sample_count = nir_imul(b,
                                           sample_count,
                                           nir_imul(b,
                                                    nir_channel(b, frag_size, 0),
                                                    nir_channel(b, frag_size, 1)));

   return nir_bfm(b, coarse_sample_count, nir_imm_int(b, 0));
}

static bool
lower_fully_covered(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_fully_covered)
      return false;

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *coverage_mask = nir_load_coverage_mask_intel(b);
   nir_def *expected_mask = load_expected_mask(b);

   nir_def *fully_covered = nir_ieq(b, coverage_mask, expected_mask);

   nir_def *cons_raster_on = nir_test_fs_config_intel(
      b, 1, INTEL_FS_CONFIG_CONSERVATIVE_RASTER);

   fully_covered = nir_bcsel(b, cons_raster_on, fully_covered, nir_imm_false(b));

   nir_def_replace(&intrin->def, fully_covered);

   return true;
}

bool
brw_nir_lower_fully_covered(nir_shader *nir)
{
   if (!BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FULLY_COVERED))
      return false;

   return nir_shader_intrinsics_pass(nir, lower_fully_covered,
                                     nir_metadata_control_flow, NULL);
}
