/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_range_analysis.h"

typedef struct {
   nir_builder *b;
   struct hash_table *range_ht;
   struct hash_table *numlsb_ht;
   enum amd_gfx_level gfx_level;
   nir_cursor addr_cursor;
   uint8_t required_align; /* Required alignment for offsets and final address */
   uint64_t out_const;
   nir_def *out_offset;
} lower_state;

static uint32_t
get_max_smem_const_offset(lower_state *state)
{
   /* GFX9+ allows both a constant and scalar offset using
    * the SOE bit (scalar offset enable).
    * Older generations may only have either a constant or
    * a scalar offset depending on the IMM bit.
    */
   if (state->gfx_level < GFX9 && state->out_offset)
      return 0;

   if (state->gfx_level >= GFX12)
      return 0x7fffff;
   else if (state->gfx_level >= GFX8)
      return 0xfffff;
   else if (state->gfx_level >= GFX7)
      return UINT32_MAX; /* GFX7 can use SQ_SRC_LITERAL. */
   else
      return 0x3ff;
}

static bool
offset_add_might_overflow(nir_shader *shader, lower_state *state)
{
   if (state->out_const > UINT32_MAX)
      return true;

   if (!state->range_ht)
      state->range_ht = _mesa_pointer_hash_table_create(NULL);

   nir_scalar offset_scalar = nir_get_scalar(state->out_offset, 0);
   return nir_addition_might_overflow(shader, state->range_ht, offset_scalar, state->out_const);
}

static bool
is_nuw(lower_state *state, nir_alu_instr *alu, nir_scalar src0, nir_scalar src1)
{
   if (alu && alu->no_unsigned_wrap)
      return true;

   if (!state->range_ht)
      state->range_ht = _mesa_pointer_hash_table_create(NULL);

   assert(src0.def->bit_size == 32 && src1.def->bit_size == 32);
   uint32_t ub0 = nir_unsigned_upper_bound(state->b->shader, state->range_ht, src0);
   uint32_t ub1 = nir_unsigned_upper_bound(state->b->shader, state->range_ht, src1);
   if ((UINT32_MAX - ub0) < ub1)
      return false;

   if (alu)
      alu->no_unsigned_wrap = true;
   return true;
}

static bool
scalar_is_aligned(nir_scalar src, lower_state *state, uint64_t mul)
{
   if (state->required_align == 1)
      return true;

   if (nir_scalar_is_const(src))
      return util_is_aligned(nir_scalar_as_uint(src) * mul, state->required_align);

   if (!state->numlsb_ht)
      state->numlsb_ht = _mesa_pointer_hash_table_create(NULL);

   unsigned num_lsb_zero = nir_def_num_lsb_zero(state->numlsb_ht, src) + util_logbase2(mul);
   return num_lsb_zero >= util_logbase2(state->required_align);
}

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
try_extract_additions(lower_state *state, nir_scalar *scalar, bool require_nuw, uint64_t mul)
{
   if (nir_scalar_is_const(*scalar) && scalar_is_aligned(*scalar, state, mul)) {
      state->out_const += nir_scalar_as_uint(*scalar) * mul;
      scalar->def = NULL;
      return true;
   }

   if (!nir_scalar_is_alu(*scalar))
      return false;
   nir_alu_instr *alu = nir_def_as_alu(scalar->def);

   uint64_t c;
   nir_scalar src = *scalar;
   nir_builder *b = state->b;
   if (parse_imul(&src, &c, require_nuw)) {
      if (try_extract_additions(state, &src, require_nuw, mul * c)) {
         b->cursor = nir_after_instr(&alu->instr);
         *scalar = nir_get_scalar(src.def ? nir_imul_imm(b, nir_mov_scalar(b, src), c) : NULL, 0);
         return true;
      }
      return false;
   } else if (is_u2u64(&src)) {
      bool rewrite_src = try_extract_additions(state, &src, true, mul);
      b->cursor = nir_after_instr(&alu->instr);
      if (src.def && mul == 1 && state->out_offset &&
          is_nuw(state, NULL, src, nir_get_scalar(state->out_offset, 0))) {
         b->cursor = state->addr_cursor;
         state->out_offset = nir_iadd_nuw(b, nir_mov_scalar(b, src), state->out_offset);
      } else if (src.def && mul == 1 && state->out_offset == NULL) {
         state->out_offset = nir_mov_scalar(b, src);
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

      /* Only one source has to be aligned, assuming the addition in total was aligned too. */
      if (!scalar_is_aligned(src0, state, mul) && !scalar_is_aligned(src1, state, mul))
         return false;

      /* Visit u2u64 sources first. This prioritizes u2u64 later in the chain over those earlier. */
      nir_scalar src1_conv = src1;
      bool swap = is_u2u64(&src1_conv);

      bool rewrite_src0 = try_extract_additions(state, swap ? &src1 : &src0, require_nuw, mul);
      bool rewrite_src1 = try_extract_additions(state, swap ? &src0 : &src1, require_nuw, mul);
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
process_instr(nir_builder *b, nir_intrinsic_instr *intrin, void *s)
{
   lower_state *state = (lower_state *)s;
   nir_intrinsic_op op;
   unsigned access = 0;
   bool is_smem = false;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_global_constant:
      access |= ACCESS_NON_WRITEABLE |
                (nir_intrinsic_access(intrin) & ACCESS_VOLATILE ? 0 : ACCESS_CAN_REORDER);
      FALLTHROUGH;
   case nir_intrinsic_load_global:
      is_smem = nir_intrinsic_access(intrin) & ACCESS_SMEM_AMD;
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


   state->b = b;
   uint32_t bit_size = op == nir_intrinsic_store_global_amd ? intrin->src[0].ssa->bit_size : intrin->def.bit_size;
   state->required_align = !is_smem ? 1 : bit_size >= 32 ? 4 : bit_size / 8;
   state->out_const = 0;
   state->out_offset = NULL;
   nir_scalar src = nir_get_scalar(addr_src->ssa, 0);
   state->addr_cursor = nir_before_instr(nir_def_instr(addr_src->ssa));
   try_extract_additions(state, &src, false, 1);

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *addr = src.def ? nir_mov_scalar(b, src) : nir_imm_int64(b, 0);

   const uint64_t max_offset = is_smem ? get_max_smem_const_offset(state) : UINT32_MAX;
   if (state->out_const > max_offset) {
      if (!state->out_offset && state->out_const <= UINT32_MAX) {
         state->out_offset = nir_imm_int(b, state->out_const);
         state->out_const = 0;
      } else if (offset_add_might_overflow(b->shader, state)) {
         addr = nir_iadd_imm(b, addr, state->out_const & ~max_offset);
         state->out_const = state->out_const & max_offset;
      } else {
         state->out_offset = nir_iadd_imm_nuw(b, state->out_offset, state->out_const & ~max_offset);
         state->out_const = state->out_const & max_offset;
      }
   }

   nir_intrinsic_instr *new_intrin = nir_intrinsic_instr_create(b->shader, op);

   new_intrin->num_components = intrin->num_components;

   if (op != nir_intrinsic_store_global_amd)
      nir_def_init(&new_intrin->instr, &new_intrin->def,
                   intrin->def.num_components, intrin->def.bit_size);

   unsigned num_src = nir_intrinsic_infos[intrin->intrinsic].num_srcs;
   for (unsigned i = 0; i < num_src; i++)
      new_intrin->src[i] = nir_src_for_ssa(intrin->src[i].ssa);
   new_intrin->src[num_src] = nir_src_for_ssa(state->out_offset ? state->out_offset : nir_imm_zero(b, 1, 32));
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
   nir_intrinsic_set_base(new_intrin, state->out_const);

   nir_builder_instr_insert(b, &new_intrin->instr);
   if (op != nir_intrinsic_store_global_amd)
      nir_def_rewrite_uses(&intrin->def, &new_intrin->def);
   nir_instr_remove(&intrin->instr);

   return true;
}

bool
ac_nir_lower_global_access(nir_shader *shader, enum amd_gfx_level gfx_level)
{
   lower_state state;
   state.range_ht = NULL;
   state.numlsb_ht = NULL;
   state.gfx_level = gfx_level;

   bool progress = nir_shader_intrinsics_pass(shader, process_instr,
                                              nir_metadata_control_flow, &state);

   _mesa_hash_table_destroy(state.range_ht, NULL);
   _mesa_hash_table_destroy(state.numlsb_ht, NULL);

   return progress;
}
