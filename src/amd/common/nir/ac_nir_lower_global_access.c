/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "nir.h"
#include "nir_builder.h"

static inline bool
is_u2u64(nir_scalar scalar)
{
   return nir_scalar_is_alu(scalar) && nir_scalar_alu_op(scalar) == nir_op_u2u64;
}

static nir_def *
try_extract_additions(nir_builder *b, nir_scalar scalar, uint64_t *out_const,
                      nir_def **out_offset, bool require_nuw)
{
   if (!nir_scalar_is_alu(scalar) || nir_scalar_alu_op(scalar) != nir_op_iadd)
      return NULL;

   nir_alu_instr *alu = nir_def_as_alu(scalar.def);
   if (require_nuw && !alu->no_unsigned_wrap)
      return NULL;

   nir_scalar src0 = nir_scalar_chase_alu_src(scalar, 0);
   nir_scalar src1 = nir_scalar_chase_alu_src(scalar, 1);

   for (unsigned i = 0; i < 2; ++i) {
      nir_scalar src = i ? src1 : src0;
      if (nir_scalar_is_const(src)) {
         *out_const += nir_scalar_as_uint(src);
      } else if (is_u2u64(src) && *out_offset == NULL) {
         nir_scalar offset_scalar = nir_scalar_chase_alu_src(src, 0);
         if (offset_scalar.def->bit_size != 32)
            continue;

         *out_offset = nir_mov_scalar(b, offset_scalar);
         nir_def *replace_offset = try_extract_additions(b, offset_scalar, out_const, out_offset, true);
         *out_offset = replace_offset ? replace_offset : *out_offset;
      } else {
         continue;
      }

      nir_def *replace_src =
         try_extract_additions(b, i == 1 ? src0 : src1, out_const, out_offset, require_nuw);
      return replace_src ? replace_src : nir_ssa_for_alu_src(b, alu, 1 - i);
   }

   nir_def *replace_src0 = try_extract_additions(b, src0, out_const, out_offset, require_nuw);
   nir_def *replace_src1 = try_extract_additions(b, src1, out_const, out_offset, require_nuw);
   if (!replace_src0 && !replace_src1)
      return NULL;

   replace_src0 = replace_src0 ? replace_src0 : nir_mov_scalar(b, src0);
   replace_src1 = replace_src1 ? replace_src1 : nir_mov_scalar(b, src1);
   return nir_iadd(b, replace_src0, replace_src1);
}

static bool
process_instr(nir_builder *b, nir_intrinsic_instr *intrin, void *_)
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
   nir_scalar src = {addr_src->ssa, 0};
   b->cursor = nir_after_def(addr_src->ssa);
   nir_def *addr = try_extract_additions(b, src, &off_const, &offset, false);
   addr = addr ? addr : addr_src->ssa;

   b->cursor = nir_before_instr(&intrin->instr);

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
   return nir_shader_intrinsics_pass(shader, process_instr,
                                       nir_metadata_control_flow, NULL);
}
