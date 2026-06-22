/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "nir.h"
#include "nir_builder.h"

static inline bool
is_u2u64(nir_scalar *scalar)
{
   if (!nir_scalar_is_alu(*scalar) || nir_scalar_alu_op(*scalar) != nir_op_u2u64)
      return false;
   nir_scalar src = nir_scalar_chase_alu_src(*scalar, 0);
   if (src.def->bit_size != 32)
      return false;
   *scalar = src;
   return true;
}

typedef struct {
   nir_shader *shader;
   struct hash_table *range_ht;
   nir_cursor addr_cursor;
} lower_state;

static bool
is_nuw(lower_state *state, nir_alu_instr *alu, nir_scalar src0, nir_scalar src1)
{
   if (alu && alu->no_unsigned_wrap)
      return true;

   if (!state->range_ht)
      state->range_ht = _mesa_pointer_hash_table_create(NULL);

   assert(src0.def->bit_size == 32 && src1.def->bit_size == 32);
   uint32_t ub0 = nir_unsigned_upper_bound(state->shader, state->range_ht, src0);
   uint32_t ub1 = nir_unsigned_upper_bound(state->shader, state->range_ht, src1);
   if ((UINT32_MAX - ub0) < ub1)
      return false;

   if (alu)
      alu->no_unsigned_wrap = true;
   return true;
}

static bool
parse_imul(nir_scalar *def, uint64_t *c, bool require_nuw)
{
   if (!nir_scalar_is_alu(*def))
      return false;

   nir_op op = nir_scalar_alu_op(*def);
   if (op != nir_op_ishl && op != nir_op_imul)
      return false;

   if (require_nuw && !nir_def_as_alu(def->def)->no_unsigned_wrap)
      return false;

   nir_scalar src0 = nir_scalar_chase_alu_src(*def, 0);
   nir_scalar src1 = nir_scalar_chase_alu_src(*def, 1);
   if (op != nir_op_ishl && nir_scalar_is_const(src0)) {
      *c = nir_scalar_as_uint(src0);
      *def = src1;
   } else if (nir_scalar_is_const(src1)) {
      *c = nir_scalar_as_uint(src1);
      *def = src0;
   } else {
      return false;
   }
   if (op == nir_op_ishl)
      *c = UINT64_C(1) << (*c & (def->def->bit_size - 1));
   return true;
}

static bool
try_extract_additions(lower_state *state, nir_builder *b, nir_scalar *scalar, uint64_t *out_const,
                      nir_def **out_offset, bool require_nuw, uint64_t mul)
{
   if (nir_scalar_is_const(*scalar)) {
      *out_const += nir_scalar_as_uint(*scalar) * mul;
      scalar->def = NULL;
      return true;
   }

   if (!nir_scalar_is_alu(*scalar))
      return false;
   nir_alu_instr *alu = nir_def_as_alu(scalar->def);

   uint64_t c;
   nir_scalar src = *scalar;
   if (parse_imul(&src, &c, require_nuw)) {
      if (try_extract_additions(state, b, &src, out_const, out_offset, require_nuw, mul * c)) {
         b->cursor = nir_after_instr(&alu->instr);
         *scalar = nir_get_scalar(src.def ? nir_imul_imm(b, nir_mov_scalar(b, src), c) : NULL, 0);
         return true;
      }
      return false;
   } else if (is_u2u64(&src)) {
      bool rewrite_src = try_extract_additions(state, b, &src, out_const, out_offset, true, mul);
      b->cursor = nir_after_instr(&alu->instr);
      if (src.def && mul == 1 && *out_offset && is_nuw(state, NULL, src, nir_get_scalar(*out_offset, 0))) {
         b->cursor = state->addr_cursor;
         *out_offset = nir_iadd_nuw(b, nir_mov_scalar(b, src), *out_offset);
      } else if (src.def && mul == 1 && *out_offset == NULL) {
         *out_offset = nir_mov_scalar(b, src);
      } else if (src.def) {
         if (rewrite_src)
            *scalar = nir_get_scalar(nir_u2u64(b, nir_mov_scalar(b, src)), 0);
         return rewrite_src;
      }
      scalar->def = NULL;
      return true;
   } else if (nir_scalar_alu_op(*scalar) == nir_op_iadd) {
      nir_scalar src0 = nir_scalar_chase_alu_src(*scalar, 0);
      nir_scalar src1 = nir_scalar_chase_alu_src(*scalar, 1);

      if (require_nuw && !is_nuw(state, alu, src0, src1))
         return false;

      /* Visit u2u64 sources first. This prioritizes u2u64 later in the chain over those earlier. */
      nir_scalar src1_conv = src1;
      bool swap = is_u2u64(&src1_conv);

      bool rewrite_src0 = try_extract_additions(
         state, b, swap ? &src1 : &src0, out_const, out_offset, require_nuw, mul);
      bool rewrite_src1 = try_extract_additions(
         state, b, swap ? &src0 : &src1, out_const, out_offset, require_nuw, mul);
      if (!rewrite_src0 && !rewrite_src1)
         return false;

      b->cursor = nir_after_instr(&alu->instr);
      if (src0.def && src1.def)
         *scalar = nir_get_scalar(nir_iadd(b, nir_mov_scalar(b, src0), nir_mov_scalar(b, src1)), 0);
      else
         *scalar = src0.def ? src0 : src1;
      return true;
   } else {
      return false;
   }
}

static bool
process_instr(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   nir_intrinsic_op op;
   unsigned access = 0;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_global_constant:
      access |= ACCESS_NON_WRITEABLE |
                (nir_intrinsic_access(intrin) & ACCESS_VOLATILE ? 0 : ACCESS_CAN_REORDER);
      FALLTHROUGH;
   case nir_intrinsic_load_global:
      op = nir_intrinsic_load_global_amd;
      break;
   case nir_intrinsic_load_global_transpose_amd:
      op = nir_intrinsic_load_global_tr_amd;
      break;
   case nir_intrinsic_global_atomic:
      op = nir_intrinsic_global_atomic_amd;
      break;
    case nir_intrinsic_global_atomic_swap:
      op = nir_intrinsic_global_atomic_swap_amd;
      break;
   case nir_intrinsic_store_global:
      op = nir_intrinsic_store_global_amd;
      break;
   default:
      return false;
   }
   unsigned addr_src_idx = op == nir_intrinsic_store_global_amd ? 1 : 0;

   nir_src *addr_src = &intrin->src[addr_src_idx];

   uint64_t off_const = 0;
   nir_def *offset = NULL;
   nir_scalar src = nir_get_scalar(addr_src->ssa, 0);
   ((lower_state *)state)->addr_cursor = nir_before_instr(nir_def_instr(addr_src->ssa));
   try_extract_additions(state, b, &src, &off_const, &offset, false, 1);

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *addr = src.def ? nir_mov_scalar(b, src) : nir_imm_int64(b, 0);

   if (off_const > UINT32_MAX) {
      addr = nir_iadd_imm(b, addr, off_const);
      off_const = 0;
   }

   nir_intrinsic_instr *new_intrin = nir_intrinsic_instr_create(b->shader, op);

   new_intrin->num_components = intrin->num_components;

   if (op != nir_intrinsic_store_global_amd)
      nir_def_init(&new_intrin->instr, &new_intrin->def,
                   intrin->def.num_components, intrin->def.bit_size);

   unsigned num_src = nir_intrinsic_infos[intrin->intrinsic].num_srcs;
   for (unsigned i = 0; i < num_src; i++)
      new_intrin->src[i] = nir_src_for_ssa(intrin->src[i].ssa);
   new_intrin->src[num_src] = nir_src_for_ssa(offset ? offset : nir_imm_zero(b, 1, 32));
   new_intrin->src[addr_src_idx] = nir_src_for_ssa(addr);

   if (nir_intrinsic_has_access(intrin))
      nir_intrinsic_set_access(new_intrin, nir_intrinsic_access(intrin) | access);
   if (nir_intrinsic_has_align_mul(intrin))
      nir_intrinsic_set_align_mul(new_intrin, nir_intrinsic_align_mul(intrin));
   if (nir_intrinsic_has_align_offset(intrin))
      nir_intrinsic_set_align_offset(new_intrin, nir_intrinsic_align_offset(intrin));
   if (nir_intrinsic_has_write_mask(intrin))
      nir_intrinsic_set_write_mask(new_intrin, nir_intrinsic_write_mask(intrin));
   if (nir_intrinsic_has_atomic_op(intrin))
      nir_intrinsic_set_atomic_op(new_intrin, nir_intrinsic_atomic_op(intrin));
   nir_intrinsic_set_base(new_intrin, off_const);

   nir_builder_instr_insert(b, &new_intrin->instr);
   if (op != nir_intrinsic_store_global_amd)
      nir_def_rewrite_uses(&intrin->def, &new_intrin->def);
   nir_instr_remove(&intrin->instr);

   return true;
}

bool
ac_nir_lower_global_access(nir_shader *shader)
{
   lower_state state;
   state.shader = shader;
   state.range_ht = NULL;
   bool progress = nir_shader_intrinsics_pass(shader, process_instr,
                                              nir_metadata_control_flow, &state);
   _mesa_hash_table_destroy(state.range_ht, NULL);
   return progress;
}
