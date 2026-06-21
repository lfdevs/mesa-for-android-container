/*
 * Copyright © 2026 Valve Corporation.
 * Copyright © 2025 Lorenzo Rossi
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
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
lower_atomic_op(nir_builder *b, nir_intrinsic_instr *intr, nir_def *loaded)
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
      alu->fp_math_ctrl = nir_op_valid_fp_math_ctrl(alu->op, nir_fp_no_fast_math);
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
build_kepler_atomic(nir_builder *b, nir_intrinsic_instr *intr)
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
         nir_def *new_data = lower_atomic_op(b, intr, loaded_data);
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
   nir_def_replace(&intrin->def, build_kepler_atomic(b, intrin));
   return true;
}

bool
nak_nir_lower_kepler_shared_atomics(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, nak_nir_lower_kepler_atomics_intrin,
                                     nir_metadata_none, NULL);
}

static nir_def *
build_mesh_atomic(nir_builder *b, nir_intrinsic_instr *intrin)
{
   nir_def *current_value;
   nir_def *offset = intrin->src[0].ssa;
   nir_def *current_invocation = nir_load_subgroup_invocation(b);

   /* Basic spin lock implementation */
   nir_loop *loop = nir_push_loop(b);
   {
      /* First we check what active threads match our offset value */
      nir_def *active_thread_mask = nir_match_any_nv(b, 32, offset);

      /* Then we elect a thread to work in this loop iteration */
      nir_def *elected_thread = nir_ufind_msb(b, active_thread_mask);

      /* Check if the current invocation won and if so do the operation */
      nir_if *if_body =
         nir_push_if(b, nir_ieq(b, elected_thread, current_invocation));
      {
         current_value = nir_load_shared(b, intrin->def.num_components, intrin->def.bit_size, offset,
                                         .base = nir_intrinsic_base(intrin));
         nir_def *new_value = lower_atomic_op(b, intrin, current_value);
         nir_store_shared(b, new_value, offset,
                          .base = nir_intrinsic_base(intrin));
         nir_jump(b, nir_jump_break);
      }
      nir_pop_if(b, if_body);
   }
   nir_pop_loop(b, loop);

   return current_value;
}

static bool
nak_nir_lower_mesh_stages_shared_atomics_intrin(nir_builder *b,
                                                nir_intrinsic_instr *intrin,
                                                UNUSED void *_data)
{
   if (intrin->intrinsic != nir_intrinsic_shared_atomic &&
       intrin->intrinsic != nir_intrinsic_shared_atomic_swap)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def_replace(&intrin->def, build_mesh_atomic(b, intrin));
   return true;
}

bool
nak_nir_lower_mesh_stages_shared_atomics(nir_shader *nir)
{
   if (!mesa_shader_stage_is_mesh(nir->info.stage))
      return false;

   /* The local workgroup should have been lowered to a single subgroup. */
   ASSERTED uint16_t wg_sz = nir->info.workgroup_size[0] *
                             nir->info.workgroup_size[1] *
                             nir->info.workgroup_size[2];
   assert(wg_sz <= NAK_SUBGROUP_SIZE);

   return nir_shader_intrinsics_pass(
      nir, nak_nir_lower_mesh_stages_shared_atomics_intrin, nir_metadata_none,
      NULL);
}

static nir_def *
build_f16vec2_cas_atomic(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *addr = intr->src[0].ssa;
   nir_def *data = intr->src[1].ssa;
   nir_atomic_op atomic_op = nir_intrinsic_atomic_op(intr);

   /* We treat the f16vec2 as a single u32 for the CAS loop */
   unsigned bit_size = 32;

   /* FAST PATH: xchg blindly overwrites data, so it doesn't need a CAS loop.
    * We can just pack to u32, run a 32-bit atomic exchange, and unpack.
    */
   if (atomic_op == nir_atomic_op_xchg) {
      nir_def *packed_data = nir_pack_bits(b, data, bit_size);
      nir_def *res = nir_shared_atomic(b, bit_size, addr, packed_data,
                                       .atomic_op = nir_atomic_op_xchg);
      return nir_unpack_bits(b, res, 16);
   }

   nir_def *initial_val = nir_load_shared(b, 1, bit_size, addr,
                                          .align_mul = 4,
                                          .access = ACCESS_ATOMIC);

   nir_loop *loop = nir_push_loop(b);
   {
      /* phi node to track the value in memory through loop iterations */
      nir_phi_instr *phi = nir_phi_instr_create(b->shader);
      nir_def_init(&phi->instr, &phi->def, 1, bit_size);
      nir_phi_instr_add_src(phi, nir_def_block(initial_val), initial_val);
      nir_def *before = &phi->def;

      /* unpack u32 -> f16vec2 */
      nir_def *before_f16 = nir_unpack_bits(b, before, 16);
      nir_def *calculated_f16 = NULL;

      switch (atomic_op) {
         case nir_atomic_op_cmpxchg:
            UNREACHABLE("f16vec2 cmpxchg is not supported");

         default: {
            /* standard arithmetic atomics (add, min, max, etc.) */
            nir_op alu_op = nir_atomic_op_to_alu(atomic_op);

            /* ensure we don't accidentally pass nir_num_opcodes again */
            assert(alu_op != nir_num_opcodes && "Unsupported atomic opcode!");

            calculated_f16 = nir_build_alu2(b, alu_op, before_f16, data);

            if (nir_op_infos[alu_op].output_type & nir_type_float) {
               nir_alu_instr *alu = nir_def_as_alu(calculated_f16);
               alu->fp_math_ctrl = nir_op_valid_fp_math_ctrl(alu->op, nir_fp_no_fast_math);
            }
            break;
         }
      }

      /* pack f16vec2 -> u32 */
      nir_def *new_val = nir_pack_bits(b, calculated_f16, bit_size);

      /* attempt CAS: shared_atomic_swap with cmpxchg */
      nir_def *cas_val = nir_shared_atomic_swap(b, bit_size, addr, before, new_val,
                                                .atomic_op = nir_atomic_op_cmpxchg);

      nir_break_if(b, nir_ieq(b, cas_val, before));

      nir_phi_instr_add_src(phi, nir_loop_last_block(loop), cas_val);
      b->cursor = nir_before_block(nir_loop_first_block(loop));
      nir_builder_instr_insert(b, &phi->instr);

      nir_pop_loop(b, loop);
      return nir_unpack_bits(b, cas_val, 16);
   }
}

static bool
nak_nir_lower_f16vec2_shared_atomics_intrin(nir_builder *b,
                                            nir_intrinsic_instr *intrin,
                                            UNUSED void *_data)
{
   if (intrin->intrinsic != nir_intrinsic_shared_atomic &&
      intrin->intrinsic != nir_intrinsic_shared_atomic_swap)
      return false;

   /* Only target f16vec2 (2 components of 16-bit) */
   if (intrin->def.bit_size != 16 || intrin->def.num_components != 2)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def_replace(&intrin->def, build_f16vec2_cas_atomic(b, intrin));
   return true;
}

bool
nak_nir_lower_f16vec2_shared_atomics(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir,
                                     nak_nir_lower_f16vec2_shared_atomics_intrin,
                                     nir_metadata_none,
                                     NULL);
}
