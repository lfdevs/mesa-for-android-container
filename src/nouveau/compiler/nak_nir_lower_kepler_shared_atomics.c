/*
 * Copyright © 2025 Lorenzo Rossi
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir.h"
#include "nir_builder.h"

/*
 * Convert atomic arithmetic to regular arithmetic along with mutex locks.
 *
 * eg:
 * atomicAdd(addr, 1) ->
 *
 * uint expected = a[0];
 * bool success = false;
 * do {
 *    data, is_locked = load_locked(a[0])
 *    if (is_locked) {
 *       data = data + 1;
 *       success = store_and_unlock(&a[0], data);
 *    }
 * } while (!success);
 *
 * special_case cmp_exc and exc.
 */

static nir_def *
lower_atomic_in_lock(nir_builder *b, nir_intrinsic_instr *intr, nir_def *loaded)
{
   // Assume we have the lock, the previous value is in loaded and we must
   // compute the value to store in the address.
   // to_store = op(loaded, data)
   nir_def *data = intr->src[1].ssa;
   nir_def *to_store;

   switch (nir_intrinsic_atomic_op(intr)) {
   case nir_atomic_op_imin:
   case nir_atomic_op_umin:
   case nir_atomic_op_imax:
   case nir_atomic_op_umax:
   case nir_atomic_op_iand:
   case nir_atomic_op_ior:
   case nir_atomic_op_ixor:
   case nir_atomic_op_fadd:
   case nir_atomic_op_fmin:
   case nir_atomic_op_fmax:
   case nir_atomic_op_iadd: {
      to_store = nir_build_alu2(
         b, nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intr)), loaded, data);
      nir_alu_instr *alu = nir_def_as_alu(to_store);
      alu->exact = true;
      alu->fp_fast_math = 0;
      break;
   }
   case nir_atomic_op_xchg: {
      // op(loaded, data) = data
      to_store = data;
      break;
   }
   case nir_atomic_op_cmpxchg: {
      // op(loaded, src1, src2) = loaded == src1 ? src2 : loaded;
      nir_def *new_data = intr->src[2].ssa;
      to_store = nir_bcsel(b, nir_ieq(b, loaded, data), new_data, loaded);
      break;
   }
   case nir_atomic_op_fcmpxchg: /* TODO: shared atomic floats */
   default:
      UNREACHABLE("Invalid intrinsic");
   }

   return to_store;
}

static nir_def *
build_atomic(nir_builder *b, nir_intrinsic_instr *intr)
{
   // TODO: this is currently compiled down to ~20 instructions while
   //       CUDA can optimize the same code to only ~5.
   nir_def *loaded_data;
   nir_def *addr = intr->src[0].ssa;

   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *load = nir_load_shared_lock_nv(b, intr->def.bit_size, addr);

      loaded_data = nir_channel(b, load, 0);
      nir_def *is_locked = nir_u2u32(b, nir_channel(b, load, 1));
      nir_if *nif = nir_push_if(b, nir_ine_imm(b, is_locked, 0));
      {
         nir_def *new_data = lower_atomic_in_lock(b, intr, loaded_data);
         nir_def *success = nir_store_shared_unlock_nv(b, 32, new_data, addr);

         nir_break_if(b, nir_ine_imm(b, success, 0));
      }
      nir_pop_if(b, nif);
   }
   nir_pop_loop(b, loop);
   return loaded_data;
}

static bool
nak_nir_lower_kepler_atomics_intrin(nir_builder *b,
                                 nir_intrinsic_instr *intrin,
                                 UNUSED void *_data)
{
   if (intrin->intrinsic != nir_intrinsic_shared_atomic &&
       intrin->intrinsic != nir_intrinsic_shared_atomic_swap)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def_replace(&intrin->def, build_atomic(b, intrin));
   return true;
}

bool
nak_nir_lower_kepler_shared_atomics(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, nak_nir_lower_kepler_atomics_intrin,
                                     nir_metadata_none, NULL);
}
