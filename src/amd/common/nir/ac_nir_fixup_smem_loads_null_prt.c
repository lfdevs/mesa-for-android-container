/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "nir/nir_builder.h"

/* SMEM reads on a NULL PRT page fail or hang depending on the hw gen.
 *
 * To workaround that, the driver splits the total VA space in half, so that a single bit controls
 * whether it's the "HIGH" or the "LOW" address space. Every sparse residency buffer allocations
 * that might be used with SMEM get two allocations:
 *
 * - the "HIGH" address space is mapped normally and its VA is returned to the application.
 * - the "LOW" address space is explicitly mapped to a zero-initialized buffer when it's allocated
 *   or when it's unmapped.
 *
 * Other buffer allocations are always allocated in the "LOW" address space, so that control bit is
 * always 0.
 *
 * Mask out the bit that controls whether it's the "HIGH" or the "LOW" address space to implement
 * the workaround for sparse residency buffer allocations.
 */
typedef struct {
   uint8_t control_bit;
} fixup_smem_loads_null_prt_state;

static bool
fixup_smem_loads_null_prt(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   fixup_smem_loads_null_prt_state *state = (fixup_smem_loads_null_prt_state *)data;

   if (intrin->intrinsic != nir_intrinsic_load_global_amd && intrin->intrinsic != nir_intrinsic_load_ssbo)
      return false;

   const unsigned access = nir_intrinsic_access(intrin);

   if (!(access & ACCESS_SMEM_AMD))
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *src = intrin->src[0].ssa;
   nir_def *new_src;

   if (intrin->intrinsic == nir_intrinsic_load_global_amd) {
      new_src = nir_iand_imm(b, src, ~(1ull << state->control_bit));
   } else {
      assert(state->control_bit >= 32);
      nir_def *new_addr_hi = nir_iand_imm(b, nir_channel(b, src, 1), ~(1u << (state->control_bit - 32)));
      new_src = nir_vec4(b, nir_channel(b, src, 0), new_addr_hi, nir_channel(b, src, 2), nir_channel(b, src, 3));
   }

   nir_src_rewrite(&intrin->src[0], new_src);

   return true;
}

bool
ac_nir_fixup_smem_loads_null_prt(nir_shader *nir, uint8_t address_prt_wa_control_bit)
{
   fixup_smem_loads_null_prt_state s = {
      .control_bit = address_prt_wa_control_bit,
   };

   return nir_shader_intrinsics_pass(nir, fixup_smem_loads_null_prt, nir_metadata_all, &s);
}
