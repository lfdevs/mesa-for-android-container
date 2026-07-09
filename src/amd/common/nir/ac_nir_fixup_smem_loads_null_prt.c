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

static uint64_t bits_may_be_1(nir_scalar scalar, unsigned depth)
{
   assert(scalar.def->bit_size == 64);
   scalar = nir_scalar_chase_movs(scalar);

   if (nir_scalar_is_const(scalar))
      return nir_scalar_as_uint(scalar);
   else if (!nir_scalar_is_alu(scalar) || depth >= 4)
      return UINT64_MAX;

   nir_op op = nir_scalar_alu_op(scalar);
   switch (op) {
   case nir_op_u2u64:
      return UINT32_MAX;
   case nir_op_pack_64_2x32_split: {
      nir_scalar src1 = nir_scalar_chase_movs(nir_scalar_chase_alu_src(scalar, 1));
      if (nir_scalar_is_const(src1))
         return nir_scalar_as_uint(src1) << 32 | UINT32_MAX;
      break;
   }
   case nir_op_iadd: {
      nir_scalar src0 = nir_scalar_chase_alu_src(scalar, 0);
      nir_scalar src1 = nir_scalar_chase_alu_src(scalar, 1);
      uint64_t src0_bits = bits_may_be_1(src0, depth + 1);
      uint64_t src1_bits = bits_may_be_1(src1, depth + 1);
      uint64_t res = src0_bits | src1_bits;
      bool carry_in = false;
      for (unsigned i = 0; i < 64; i++) {
         uint64_t bit = BITFIELD64_BIT(i);
         bool carry_out = (src0_bits & src1_bits & bit) || (carry_in && (res & bit));
         res |= carry_in ? bit : 0;
         carry_in = carry_out;
      }
      return res;
   }
   default:
      break;
   }

   return UINT64_MAX;
}

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
      /* Check if the control bit is definitely zero. This can happen if the address is a 32-bit one,
       * which are created using pack_64_2x32_split. The high 32-bits are a constant, and so in this
       * case, we can usually omit masking out the control bit.
       *
       * Usually, this check is unnecessary and the mask is later optimized out, but that doesn't
       * happen if the pack is followed by an addition.
       */
      uint64_t may_be_1 = bits_may_be_1(nir_get_scalar(src, 0), 0);
      uint64_t bit = (1ull << state->control_bit);
      if (!(may_be_1 & bit))
         return false;
      new_src = nir_iand_imm(b, src, ~bit);
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
