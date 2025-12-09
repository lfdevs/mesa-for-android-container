/*
 * Copyright © 2021 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Timur Kristóf
 *
 */

#include "nir.h"
#include "nir_builder.h"

typedef struct
{
   struct hash_table *range_ht;
   const nir_opt_offsets_options *options;
   bool progress;
} opt_offsets_state;

static bool
try_extract_const_addition(nir_builder *b, opt_offsets_state *state, nir_scalar *out_val, unsigned *out_const,
                           uint32_t max, bool need_nuw)
{
   nir_scalar val = nir_scalar_chase_movs(*out_val);

   if (!nir_scalar_is_alu(val))
      return false;

   nir_alu_instr *alu = nir_def_as_alu(val.def);
   if (alu->op != nir_op_iadd)
      return false;

   nir_scalar src[2] = {
      { alu->src[0].src.ssa, alu->src[0].swizzle[val.comp] },
      { alu->src[1].src.ssa, alu->src[1].swizzle[val.comp] },
   };

   /* Make sure that we aren't taking out an addition that could trigger
    * unsigned wrapping in a way that would change the semantics of the load.
    * Ignored for ints-as-floats (lower_bitops is a proxy for that), where
    * unsigned wrapping doesn't make sense.
    */
   if (need_nuw && !alu->no_unsigned_wrap &&
       !b->shader->options->lower_bitops) {
      if (!state->range_ht) {
         /* Cache for nir_unsigned_upper_bound */
         state->range_ht = _mesa_pointer_hash_table_create(NULL);
      }

      /* Check if there can really be an unsigned wrap. */
      uint32_t ub0 = nir_unsigned_upper_bound(b->shader, state->range_ht, src[0]);
      uint32_t ub1 = nir_unsigned_upper_bound(b->shader, state->range_ht, src[1]);

      if ((UINT32_MAX - ub0) < ub1)
         return false;

      /* We proved that unsigned wrap won't be possible, so we can set the flag too. */
      alu->no_unsigned_wrap = true;
      state->progress = true;
   }

   for (unsigned i = 0; i < 2; ++i) {
      src[i] = nir_scalar_chase_movs(src[i]);
      if (nir_scalar_is_const(src[i])) {
         uint32_t offset = nir_scalar_as_uint(src[i]);
         if (offset + *out_const <= max) {
            *out_const += offset;
            try_extract_const_addition(b, state, &src[1 - i], out_const, max, need_nuw);
            *out_val = src[1 - i];
            return true;
         }
      }
   }

   bool changed_src0 = try_extract_const_addition(b, state, &src[0], out_const, max, need_nuw);
   bool changed_src1 = try_extract_const_addition(b, state, &src[1], out_const, max, need_nuw);
   if (!changed_src0 && !changed_src1)
      return false;

   state->progress = true;
   b->cursor = nir_before_instr(&alu->instr);
   nir_def *r = nir_iadd(b, nir_mov_scalar(b, src[0]),
                         nir_mov_scalar(b, src[1]));
   *out_val = nir_get_scalar(r, 0);
   return true;
}

static bool
try_fold_load_store(nir_builder *b,
                    nir_intrinsic_instr *intrin,
                    opt_offsets_state *state,
                    unsigned offset_src_idx,
                    uint32_t max,
                    bool need_nuw)
{
   /* Assume that BASE is the constant offset of a load/store.
    * Try to constant-fold additions to the offset source
    * into the actual const offset of the instruction.
    */

   unsigned off_const = nir_intrinsic_base(intrin);
   nir_src *off_src = &intrin->src[offset_src_idx];
   nir_def *replace_src = NULL;

   if (off_src->ssa->bit_size != 32)
      return false;

   if (off_const > max)
      return false;

   if (!nir_src_is_const(*off_src)) {
      uint32_t add_offset = 0;
      nir_scalar val = { .def = off_src->ssa, .comp = 0 };
      if (!try_extract_const_addition(b, state, &val, &add_offset, max - off_const, need_nuw))
         return false;
      off_const += add_offset;
      b->cursor = nir_before_instr(&intrin->instr);
      replace_src = nir_mov_scalar(b, val);
   } else if (nir_src_as_uint(*off_src) && nir_src_as_uint(*off_src) <= max - off_const) {
      off_const += nir_src_as_uint(*off_src);
      b->cursor = nir_before_instr(&intrin->instr);
      replace_src = nir_imm_zero(b, off_src->ssa->num_components, off_src->ssa->bit_size);
   }

   if (!replace_src)
      return false;

   nir_src_rewrite(&intrin->src[offset_src_idx], replace_src);

   assert(off_const <= max);
   nir_intrinsic_set_base(intrin, off_const);
   return true;
}

static bool
decrease_shared2_offsets(uint32_t offset0, uint32_t offset1, uint32_t stride, uint32_t *excess)
{
   /* Make the offsets a multiple of the stride. */
   if (offset0 % stride != offset1 % stride)
      return false;
   *excess = offset0 % stride;

   /* Ensure both offsets are not too large. */
   uint32_t range = 256 * stride;
   if (offset0 / range != offset1 / range) {
      *excess += ROUND_DOWN_TO(MIN2(offset0, offset1), stride);
      if (offset0 - *excess >= range || offset1 - *excess >= range)
         return false;
   } else {
      *excess += ROUND_DOWN_TO(offset0, range);
   }

   return true;
}

static bool
try_fold_shared2(nir_builder *b,
                 nir_intrinsic_instr *intrin,
                 opt_offsets_state *state,
                 unsigned offset_src_idx,
                 bool need_nuw)
{
   bool is_load = intrin->intrinsic == nir_intrinsic_load_shared2_amd;
   unsigned comp_size = (is_load ? intrin->def.bit_size : intrin->src[0].ssa->bit_size) / 8;
   unsigned stride = (nir_intrinsic_st64(intrin) ? 64 : 1) * comp_size;
   uint32_t offset0 = nir_intrinsic_offset0(intrin) * stride;
   uint32_t offset1 = nir_intrinsic_offset1(intrin) * stride;
   nir_src *off_src = &intrin->src[offset_src_idx];

   uint32_t const_offset = 0;
   nir_scalar replace_src = { NULL, 0 };
   bool modified_shader = false;
   if (!nir_src_is_const(*off_src)) {
      uint32_t max = INT32_MAX - MAX2(offset0, offset1); /* Avoid negative offsets. */
      replace_src = nir_get_scalar(off_src->ssa, 0);
      if (!try_extract_const_addition(b, state, &replace_src, &const_offset, max, need_nuw))
         return false;

      modified_shader = true;
   } else {
      const_offset = nir_src_as_uint(*off_src);
   }

   offset0 += const_offset;
   offset1 += const_offset;

   uint32_t excess_normal = 0, excess_st64 = 0;
   bool normal = decrease_shared2_offsets(offset0, offset1, comp_size, &excess_normal);
   bool st64 = decrease_shared2_offsets(offset0, offset1, 64 * comp_size, &excess_st64);
   /* Use ST64 if the normal mode is impossible or using ST64 saves an addition. */
   st64 &= !normal || (excess_normal > 0 && excess_st64 == 0);
   uint32_t excess = st64 ? excess_st64 : excess_normal;
   assert(st64 || normal);

   if (excess == const_offset && !modified_shader)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   /* Even if the constant offset doesn't fit in offset0/offset1, this addition is likely to be CSE'd. */
   if (replace_src.def)
      nir_src_rewrite(off_src, nir_iadd_imm(b, nir_mov_scalar(b, replace_src), excess));
   else
      nir_src_rewrite(off_src, nir_imm_int(b, excess));

   stride = (st64 ? 64 : 1) * comp_size;
   nir_intrinsic_set_offset0(intrin, (offset0 - excess) / stride);
   nir_intrinsic_set_offset1(intrin, (offset1 - excess) / stride);
   nir_intrinsic_set_st64(intrin, st64);

   return true;
}

static uint32_t
get_max(opt_offsets_state *state, nir_intrinsic_instr *intrin, uint32_t default_val)
{
   if (default_val)
      return default_val;
   if (state->options->max_offset_cb)
      return state->options->max_offset_cb(intrin, state->options->cb_data);
   return 0;
}

static bool
allow_offset_wrap(opt_offsets_state *state, nir_intrinsic_instr *intr)
{
   if (state->options->allow_offset_wrap_cb)
      return state->options->allow_offset_wrap_cb(intr, state->options->cb_data);
   return false;
}

static bool
process_instr(nir_builder *b, nir_instr *instr, void *s)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   opt_offsets_state *state = (opt_offsets_state *)s;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   bool need_nuw = !allow_offset_wrap(state, intrin);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_uniform:
   case nir_intrinsic_load_const_ir3:
      return try_fold_load_store(b, intrin, state, 0, get_max(state, intrin, state->options->uniform_max), need_nuw);
   case nir_intrinsic_load_ubo_vec4:
      return try_fold_load_store(b, intrin, state, 1, get_max(state, intrin, state->options->ubo_vec4_max), need_nuw);
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      return try_fold_load_store(b, intrin, state, 0, get_max(state, intrin, state->options->shared_atomic_max), need_nuw);
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_shared_ir3:
      return try_fold_load_store(b, intrin, state, 0, get_max(state, intrin, state->options->shared_max), need_nuw);
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_shared_ir3:
      return try_fold_load_store(b, intrin, state, 1, get_max(state, intrin, state->options->shared_max), need_nuw);
   case nir_intrinsic_load_shared2_amd:
      return try_fold_shared2(b, intrin, state, 0, need_nuw);
   case nir_intrinsic_store_shared2_amd:
      return try_fold_shared2(b, intrin, state, 1, need_nuw);
   case nir_intrinsic_load_buffer_amd:
      need_nuw &= !!(nir_intrinsic_access(intrin) & ACCESS_IS_SWIZZLED_AMD);
      return try_fold_load_store(b, intrin, state, 1, get_max(state, intrin, state->options->buffer_max), need_nuw);
   case nir_intrinsic_store_buffer_amd:
      need_nuw &= !!(nir_intrinsic_access(intrin) & ACCESS_IS_SWIZZLED_AMD);
      return try_fold_load_store(b, intrin, state, 2, get_max(state, intrin, state->options->buffer_max), need_nuw);
   case nir_intrinsic_load_ssbo_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_ubo_uniform_block_intel:
      return try_fold_load_store(b, intrin, state, 1, get_max(state, intrin, state->options->buffer_max), need_nuw);
   case nir_intrinsic_store_ssbo_intel:
      return try_fold_load_store(b, intrin, state, 2, get_max(state, intrin, state->options->buffer_max), need_nuw);
   case nir_intrinsic_load_ssbo_ir3:
      return try_fold_load_store(b, intrin, state, 2, get_max(state, intrin, state->options->buffer_max), need_nuw);
   case nir_intrinsic_store_ssbo_ir3:
      return try_fold_load_store(b, intrin, state, 3, get_max(state, intrin, state->options->buffer_max), need_nuw);
   default:
      return false;
   }

   UNREACHABLE("Can't reach here.");
}

bool
nir_opt_offsets(nir_shader *shader, const nir_opt_offsets_options *options)
{
   opt_offsets_state state;
   state.range_ht = NULL;
   state.options = options;
   state.progress = false;

   bool p = nir_shader_instructions_pass(shader, process_instr,
                                         nir_metadata_control_flow,
                                         &state);

   if (state.range_ht)
      _mesa_hash_table_destroy(state.range_ht, NULL);

   return p || state.progress;
}
