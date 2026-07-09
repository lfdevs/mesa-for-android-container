/*
 * Copyright © 2025 Valve Corporation
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
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"
#include "nir_phi_builder.h"

/* This pass optimizes workgroup shared memory access to subgroup operations
 * in single subgroup workgroups.
 *
 * The only currently implemented optimization handles constant index access,
 * which can always be lowered to subgroup operations.
 * The general idea is to replace loads with the value that was previously
 * stored to the same memory position.
 * For this, we have to:
 *  - detect which variables are only used with constant indexing
 *  - gather where each component of the variables is written
 *  - linearize divergent control flow to make it possible to keep
 *    values uniform in registers
 *  - do a per component into SSA pass, handling load/store and some
 *    atomics
 *  - when divergent control reconverges, re-uniformize phis so that
 *    all invocations have access to the last value written by any lane
 *
 * This will likely not make sense on all hardware. For optimal effect, the backend
 * should:
 *  - have uniform registers, even in divergent control flow
 *  - use nir_divergence_ignore_undef_if_phi_srcs inside the backend
 *  - make read_invocation(uniform, idx) a nop
 *  - have fast reductions
 *  - set min_subgroup_size as high as possible before this pass
 *
 * Continues and returns must be lowered before this pass. Function calls
 * are handled, but only by disabling the optimization for variables accessed
 * in non-entry point functions.
 *
 * To support untyped pointers and aliased shared memory, variable components
 * are tracked as 32bit values. This makes it possible to support any type,
 * with additional code to pack/unpack on store/load if needed.
 *
 * Future work could involve optimizing non constant access to shuffles.
 */

struct shared_u32 {
   BITSET_WORD *written_in_blks;
   struct nir_phi_builder_value *val;
};

struct var_to_subgroup_state {
   void *mem_ctx;
   struct hash_table *shared_var_infos;

   unsigned ballot_num_components;
   unsigned ballot_size;

   nir_function_impl *impl;

   nir_builder b;
   struct nir_phi_builder *pb;

   struct shared_u32 *values;
   unsigned values_len;
};

struct shared_var_info {
   unsigned offset;
};

static struct shared_var_info *
get_shared_var_info(struct var_to_subgroup_state *state, nir_variable *var)
{
   struct hash_entry *entry =
      _mesa_hash_table_search(state->shared_var_infos, var);
   return entry ? entry->data : NULL;
}

static void
remove_shared_var_info(struct var_to_subgroup_state *state, nir_variable *var)
{
   if (var->data.aliased_shared_memory) {
      hash_table_foreach(state->shared_var_infos, entry) {
         nir_variable *iter_var = (nir_variable *)entry->key;
         if (iter_var->data.aliased_shared_memory)
            _mesa_hash_table_remove(state->shared_var_infos, entry);
      }
   } else {
      _mesa_hash_table_remove_key(state->shared_var_infos, var);
   }
}

static unsigned
explict_deref_offset(nir_deref_instr *deref)
{
   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   unsigned offset = 0;
   unsigned stride = path.path[0]->type->explicit_stride;
   for (nir_deref_instr **p = &path.path[1]; *p; p++) {
      switch ((*p)->deref_type) {
      case nir_deref_type_array: {
         unsigned idx = nir_src_as_uint((*p)->arr.index);
         offset += idx * stride;
         stride = (*p)->type->explicit_stride;
         break;
      }
      case nir_deref_type_struct: {
         /* p starts at path[1], so this is safe */
         nir_deref_instr *parent = *(p - 1);
         int member_offset = glsl_get_struct_field_data(parent->type, (*p)->strct.index)->offset;
         assert(member_offset != -1);
         offset += member_offset;
         stride = (*p)->type->explicit_stride;
         break;
      }
      case nir_deref_type_cast:
         /* A cast doesn't contribute to the offset */
         stride = (*p)->cast.ptr_stride;
         break;
      default:
         UNREACHABLE("Unsupported deref type");
      }
   }

   nir_deref_path_finish(&path);

   return offset;
}

static unsigned
get_shared_deref_offset(struct var_to_subgroup_state *state, nir_deref_instr *deref)
{
   if (!nir_deref_mode_may_be(deref, nir_var_mem_shared))
      return UINT32_MAX;

   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (!var)
      return UINT32_MAX;

   struct shared_var_info *info = get_shared_var_info(state, var);
   if (!info)
      return UINT32_MAX;

   unsigned base = info->offset;
   unsigned offset;

   if (state->b.shader->info.shared_memory_explicit_layout) {
      /* nir_deref_instr_get_const_offset can't handle explicit layouts,
       * so we have to handroll our own.
       * Amazing.
       */
      offset = explict_deref_offset(deref);
   } else {
      offset = nir_deref_instr_get_const_offset(deref, glsl_get_natural_size_align_bytes);
   }

   return base + offset;
}

static void
replace_update_divergence(nir_def *old, nir_def *new)
{
   new->divergent = old->divergent;
   nir_def_replace(old, new);
}

static void
reduce_data(nir_builder *b, nir_op op, nir_def *data,
            nir_def **reduce, nir_def **scan, nir_def *mem)
{
   if (scan && reduce && (op == nir_op_iadd || op == nir_op_ixor)) {
      *scan = nir_inclusive_scan(b, data, .reduction_op = op);
      *scan = nir_build_alu2(b, op, *scan, mem);
      nir_def *last_lane = nir_last_invocation(b);
      *reduce = nir_read_invocation(b, *scan, last_lane);
      *scan = nir_build_alu2(b, op == nir_op_iadd ? nir_op_isub : op, *scan, data);
   } else if (scan) {
      *scan = nir_exclusive_scan(b, data, .reduction_op = op);
      *scan = nir_build_alu2(b, op, *scan, mem);
      if (reduce) {
         nir_def *last_lane = nir_last_invocation(b);
         nir_def *last = nir_read_invocation(b, data, last_lane);
         *reduce = nir_read_invocation(b, *scan, last_lane);
         *reduce = nir_build_alu2(b, op, *reduce, last);
      }
   } else {
      *reduce = nir_reduce(b, data, .reduction_op = op);
      *reduce = nir_build_alu2(b, op, *reduce, mem);
   }
}

static nir_def *
read_invocation_cond(nir_builder *b,
                     nir_def *val,
                     nir_def *cond,
                     unsigned num_ballot_components,
                     unsigned ballot_size)
{
   nir_def *read_idx = nir_ballot(b, num_ballot_components, ballot_size, cond);
   if (num_ballot_components == 1)
      read_idx = nir_find_lsb(b, read_idx);
   else
      read_idx = nir_ballot_find_lsb(b, read_idx);

   return nir_read_invocation(b, val, read_idx);
}

static void
uniformize_block_def(struct var_to_subgroup_state *state, struct shared_u32 *value, nir_block *block)
{
   /* This block can only have a phi if it is a loop-header which is always uniform. */
   if (nir_cf_node_is_first(&block->cf_node))
      return;

   /* Re-uniformize the phi result if the previous control flow is divergent. */
   struct nir_phi_builder_value *val = value->val;
   nir_def *def = nir_phi_builder_value_get_block_def(val, block);
   nir_builder *b = &state->b;
   nir_cf_node *prev_node = nir_cf_node_prev(&block->cf_node);
   assert(prev_node);

   if (prev_node->type == nir_cf_node_loop) {
      nir_loop *loop = nir_cf_node_as_loop(prev_node);
      assert(!loop->divergent_continue);

      /* Uniformize if the block-def is from a previous divergent loop. */
      if (nir_def_instr(def)->block->index >= nir_loop_first_block(loop)->index && loop->divergent_break) {
         assert(nir_block_num_preds(block) == 1);
         assert(!loop->divergent_break || nir_block_num_preds(block) == 1);

         b->cursor = nir_before_block_after_phis(nir_loop_first_block(loop));
         nir_def *c_true = nir_imm_true(b);
         nir_def *loop_active = nir_ballot(b, state->ballot_num_components, state->ballot_size, c_true);

         nir_foreach_pred(pred, block)
            b->cursor = nir_after_block_before_jump(pred);
         nir_def *break_active = nir_ballot(b, state->ballot_num_components, state->ballot_size, c_true);

         nir_def *last_break = nir_ball_iequal(b, loop_active, break_active);

         b->cursor = nir_before_block_after_phis(block);

         nir_def *res = read_invocation_cond(b, def, last_break,
                                             state->ballot_num_components, state->ballot_size);
         nir_phi_builder_value_set_block_def(val, block, res);
      }
   } else if (prev_node->type == nir_cf_node_if) {
      nir_if *nif = nir_cf_node_as_if(prev_node);

      /* Uniformize phis after divergent IF. */
      if (nir_def_is_phi(def) && nir_def_instr(def)->block == block && nir_src_is_divergent(&nif->condition)) {
         assert(nir_block_num_preds(block) == 2);

         nir_def *then_src = nir_phi_builder_value_get_block_def(val, nir_if_last_then_block(nif));
         nir_def *else_src = nir_phi_builder_value_get_block_def(val, nir_if_last_else_block(nif));

         bool else_dominates = nir_block_dominates(nir_def_block(else_src), block);
         assert(else_dominates || nir_block_dominates(nir_def_block(then_src), block));

         b->cursor = nir_before_block_after_phis(block);

         nir_def *dom_src = else_dominates ? else_src : then_src;
         nir_def *cond = else_dominates ? nif->condition.ssa : nir_inot(b, nif->condition.ssa);

         nir_def *res = read_invocation_cond(b, def, cond,
                                             state->ballot_num_components, state->ballot_size);

         res = nir_bcsel(b, nir_vote_any(b, 1, cond), res, dom_src);
         nir_phi_builder_value_set_block_def(val, block, res);

         /* Rewrite dominator source to undef, this allows the phi to be uniform
          * if the other source is uniform. Requires nir_divergence_ignore_undef_if_phi_srcs.
          */
         nir_block *dominator = else_dominates ? nir_if_last_else_block(nif) : nir_if_last_then_block(nif);
         b->cursor = nir_after_block(dominator);
         nir_def *undef = nir_undef(b, def->num_components, def->bit_size);
         nir_phi_builder_value_set_block_def(val, dominator, undef);
      }
   } else {
      UNREACHABLE("unhandled cf node type");
   }
}

static void
uniformize_vars(struct var_to_subgroup_state *state, nir_block *block)
{
   for (unsigned i = 0; i < state->values_len; i++)
      uniformize_block_def(state, &state->values[i], block);
}

static nir_def *
read_shared_data(struct var_to_subgroup_state *state, unsigned offset, unsigned bit_size)
{
   if (offset >= state->values_len * 4)
      return nir_undef(&state->b, 1, bit_size);

   nir_block *block = nir_cursor_current_block(state->b.cursor);

   nir_def *u32_vals[3];
   unsigned start = offset / 4;
   unsigned end = DIV_ROUND_UP(offset + bit_size / 8, 4);
   unsigned count = end - start;
   assert(count <= ARRAY_SIZE(u32_vals));

   for (unsigned i = 0; i < count; i++) {
      if (start + i >= state->values_len)
         u32_vals[i] = nir_undef(&state->b, 1, 32);
      else
         u32_vals[i] = nir_phi_builder_value_get_block_def(state->values[start + i].val, block);
   }

   return nir_extract_bits(&state->b, u32_vals, count, (offset % 4) * 8, 1, bit_size);
}

static void
write_shared_data(struct var_to_subgroup_state *state, unsigned offset, nir_def *data)
{
   if (offset >= state->values_len * 4)
      return;

   nir_block *block = nir_cursor_current_block(state->b.cursor);

   if (offset % 4 == 0 && data->bit_size >= 32) {
      data = nir_bitcast_vector(&state->b, data, 32);
      for (unsigned comp = 0; comp < data->num_components; comp++) {
         unsigned idx = offset / 4 + comp;
         if (idx < state->values_len) {
            nir_def *val = nir_channel(&state->b, data, comp);
            nir_phi_builder_value_set_block_def(state->values[idx].val, block, val);
         }
      }
      return;
   }

   nir_def *vals[3 * sizeof(uint32_t)];
   unsigned start = offset / 4;
   unsigned end = DIV_ROUND_UP(offset + data->bit_size / 8, 4);
   unsigned count = end - start;

   unsigned bit_size = data->bit_size;

   if (offset % 4 != 0)
      bit_size = MIN2(bit_size, (1u << (ffs(offset % 4) - 1)) * 8);

   assert(count * bit_size / 8 <= ARRAY_SIZE(vals));

   for (unsigned i = 0; i < count; i++) {
      nir_def *val;
      if (start + i >= state->values_len)
         val = nir_undef(&state->b, 1, 32);
      else
         val = nir_phi_builder_value_get_block_def(state->values[start + i].val, block);

      val = nir_bitcast_vector(&state->b, val, bit_size);

      for (unsigned j = 0; j < val->num_components; j++) {
         vals[i * val->num_components + j] = nir_channel(&state->b, val, j);
      }
   }

   data = nir_bitcast_vector(&state->b, data, bit_size);

   for (unsigned i = 0; i < data->num_components; i++) {
      unsigned base = (offset % 4) / (bit_size / 8);
      vals[base + i] = nir_channel(&state->b, data, i);
   }

   for (unsigned comp = 0; comp < count; comp++) {
      unsigned idx = offset / 4 + comp;
      if (idx < state->values_len) {
         nir_def *val = nir_extract_bits(&state->b, vals, count * 32 / bit_size, comp * 32, 1, 32);
         nir_phi_builder_value_set_block_def(state->values[idx].val, block, val);
      }
   }
}

static void
lower_shared_access(struct var_to_subgroup_state *state, nir_instr *instr)
{
   /* Clean up dead derefs, to allows us to safely remove variables. */
   if (instr->type == nir_instr_type_deref)
      nir_deref_instr_remove_if_unused(nir_instr_as_deref(instr));

   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_deref_atomic_swap: break;
   default: return;
   }

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   unsigned offset = get_shared_deref_offset(state, deref);
   if (offset == UINT32_MAX)
      return;

   state->b.cursor = nir_before_instr(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_deref: {
      unsigned bit_size = intr->def.bit_size == 1 ? 32 : intr->def.bit_size;

      nir_def *comps[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < intr->def.num_components; i++) {
         comps[i] = read_shared_data(state, offset + i * bit_size / 8, bit_size);

         if (intr->def.bit_size == 1)
            comps[i] = nir_i2b(&state->b, comps[i]);
      }

      nir_def *load = nir_vec(&state->b, comps, intr->def.num_components);
      replace_update_divergence(&intr->def, load);
      break;
   }
   case nir_intrinsic_store_deref: {
      unsigned bit_size = intr->src[1].ssa->bit_size == 1 ? 32 : intr->src[1].ssa->bit_size;

      u_foreach_bit(i, nir_intrinsic_write_mask(intr)) {
         nir_def *write = nir_channel(&state->b, intr->src[1].ssa, i);

         if (intr->src[1].ssa->bit_size == 1)
            write = nir_b2i32(&state->b, write);

         if (nir_src_is_divergent(&intr->src[1])) {
            /* Which invocation is written is undefined
             * AMD HW writes the last invocation, but reading
             * it is also more complicated than the first.
             */
            write = nir_read_first_invocation(&state->b, write);
         }
         write_shared_data(state, offset + i * bit_size / 8, write);
      }
      nir_instr_remove(instr);
      break;
   }
   case nir_intrinsic_deref_atomic: {
      unsigned bit_size = intr->src[1].ssa->bit_size;
      assert(bit_size != 1);
      nir_op op = nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intr));
      bool return_prev = !nir_def_is_unused(&intr->def);
      bool combined_scan_reduce = return_prev && nir_src_is_divergent(&intr->src[1]);

      nir_def *comps[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < intr->src[1].ssa->num_components; i++) {
         unsigned comp_offset = offset + i * bit_size / 8;

         nir_def *data = nir_channel(&state->b, intr->src[1].ssa, i);
         nir_def *load = read_shared_data(state, comp_offset, bit_size);

         nir_def *reduce = NULL;
         reduce_data(&state->b, op, data, &reduce, combined_scan_reduce ? &comps[i] : NULL, load);
         if (!combined_scan_reduce && return_prev)
            reduce_data(&state->b, op, data, NULL, &comps[i], load);

         write_shared_data(state, comp_offset, reduce);
      }

      if (return_prev) {
         nir_def *scan = nir_vec(&state->b, comps, intr->def.num_components);
         replace_update_divergence(&intr->def, scan);
      } else {
         nir_instr_remove(instr);
      }
      break;
   }
   case nir_intrinsic_deref_atomic_swap:
   default: UNREACHABLE("invalid intrinsic");
   }

   nir_deref_instr_remove_if_unused(deref);
}

static bool
deref_can_be_optimized(nir_deref_instr *deref)
{
   nir_foreach_use_including_if(use_src, &deref->def) {
      if (nir_src_is_if(use_src))
         return false;

      nir_instr *use_instr = nir_src_use_instr(use_src);

      switch (use_instr->type) {
      case nir_instr_type_deref: {
         nir_deref_instr *use_deref = nir_instr_as_deref(use_instr);

         /* If a deref shows up in an array index or something like that, it's
          * a complex use.
          */
         if (use_src != &use_deref->parent)
            return false;

         switch (use_deref->deref_type) {
         case nir_deref_type_array:
            if (!nir_src_is_const(use_deref->arr.index))
               return false;
            break;
         case nir_deref_type_cast:
         case nir_deref_type_struct:
            break;
         default:
            return false;
         }

         if (!deref_can_be_optimized(use_deref))
            return false;

         continue;
      }

      case nir_instr_type_intrinsic: {
         nir_intrinsic_instr *use_intrin = nir_instr_as_intrinsic(use_instr);
         switch (use_intrin->intrinsic) {
         case nir_intrinsic_load_deref:
            assert(use_src == &use_intrin->src[0]);
            continue;

         case nir_intrinsic_store_deref:
            if (use_src == &use_intrin->src[0])
               continue;
            return false;

         case nir_intrinsic_deref_atomic:
         case nir_intrinsic_deref_atomic_swap:
            switch (nir_intrinsic_atomic_op(use_intrin)) {
            case nir_atomic_op_iadd:
            case nir_atomic_op_imin:
            case nir_atomic_op_umin:
            case nir_atomic_op_imax:
            case nir_atomic_op_umax:
            case nir_atomic_op_iand:
            case nir_atomic_op_ior:
            case nir_atomic_op_ixor:
            case nir_atomic_op_fmin:
            case nir_atomic_op_fmax:
               if (use_src == &use_intrin->src[0])
                  continue;
               return false;
            case nir_atomic_op_xchg:
            case nir_atomic_op_cmpxchg:
            case nir_atomic_op_fcmpxchg: /* TODO */
            default:
               return false;
            }

         default:
            return false;
         }
      }

      default:
         return false;
      }
   }

   return true;
}

static void
check_complex_uses_instr(nir_instr *instr, struct var_to_subgroup_state *state, bool inside_call)
{
   if (instr->type != nir_instr_type_deref)
      return;

   nir_deref_instr *deref = nir_instr_as_deref(instr);
   if (deref->deref_type != nir_deref_type_var)
      return;

   if (!(deref->var->data.mode & nir_var_mem_shared))
      return;

   if (!get_shared_var_info(state, deref->var))
      return;

   if (inside_call || !deref_can_be_optimized(deref))
      remove_shared_var_info(state, deref->var);
}

static void
check_complex_uses(struct var_to_subgroup_state *state)
{
   nir_foreach_function_impl(impl, state->b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            check_complex_uses_instr(instr, state, impl != state->impl);
         }
      }

      if (impl != state->impl)
         nir_no_progress(impl);
   }
}

static void
calculate_var_offsets(struct var_to_subgroup_state *state)
{
   /* Iterate over the shader variable list instead of the hash table
    * for determinism.
    */
   unsigned next_offset = 0;
   bool has_aliased = false;
   bool explicit_layout = state->b.shader->info.shared_memory_explicit_layout;
   nir_foreach_variable_with_modes(var, state->b.shader, nir_var_mem_shared) {
      struct shared_var_info *info = get_shared_var_info(state, var);

      if (!info)
         continue;

      if (var->data.aliased_shared_memory) {
         has_aliased = true;
         assert(explicit_layout);
         continue;
      }

      info->offset = next_offset;

      unsigned var_size;
      unsigned var_align;
      if (explicit_layout)
         var_size = glsl_get_explicit_size(var->type, false);
      else
         glsl_get_natural_size_align_bytes(var->type, &var_size, &var_align);

      next_offset += align(var_size, 4);
   }

   if (has_aliased) {
      unsigned max_aliased_size = 0;
      nir_foreach_variable_with_modes(var, state->b.shader, nir_var_mem_shared) {
         if (!var->data.aliased_shared_memory)
            continue;

         struct shared_var_info *info = get_shared_var_info(state, var);

         if (!info)
            continue;

         info->offset = next_offset;

         unsigned var_size = glsl_get_explicit_size(var->type, false);
         max_aliased_size = MAX2(max_aliased_size, var_size);
      }

      next_offset += align(max_aliased_size, 4);
   }

   state->values_len = DIV_ROUND_UP(next_offset, 4);
   state->values = rzalloc_array(state->mem_ctx, struct shared_u32, state->values_len);
}

static void
gather_val_write_instr(nir_instr *instr, struct var_to_subgroup_state *state)
{
   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   unsigned write_mask;

   switch (intr->intrinsic) {
   case nir_intrinsic_store_deref:
      write_mask = nir_intrinsic_write_mask(intr);
      break;
   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_deref_atomic_swap:
      write_mask = BITFIELD_MASK(intr->src[1].ssa->num_components);
      break;
   default:
      return;
   }

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   unsigned offset = get_shared_deref_offset(state, deref);
   if (offset >= state->values_len * 4)
      return;

   unsigned block_idx = instr->block->index;
   unsigned bit_size = intr->src[1].ssa->bit_size == 1 ? 32 : intr->src[1].ssa->bit_size;

   u_foreach_bit(comp, write_mask) {
      unsigned comp_offset = offset + comp * (bit_size / 8);
      unsigned start = comp_offset / 4;
      unsigned end = DIV_ROUND_UP(comp_offset + bit_size / 8, 4);
      end = MIN2(end, state->values_len);

      for (unsigned i = start; i < end; i++) {
         BITSET_SET(state->values[i].written_in_blks, block_idx);
      }
   }
}

static void
gather_val_write_blocks(struct var_to_subgroup_state *state)
{
   unsigned num_blk_words = BITSET_WORDS(state->impl->num_blocks);
   for (unsigned i = 0; i < state->values_len; i++) {
      state->values[i].written_in_blks =
         rzalloc_array(state->mem_ctx, BITSET_WORD, num_blk_words);
   }

   nir_foreach_block(block, state->impl) {
      nir_foreach_instr(instr, block) {
         gather_val_write_instr(instr, state);
      }
   }
}

/* For the pass to work correctly, we require that:
 *  - in divergent ifs, only one side writes a value
 *  - loops with writes must not have multiple divergent breaks
 *  - there must be no writes in blocks which can reach divergent
 *    breaks before the next time remaining loop control flow converges
 */
static bool
linearize_write_cfg(struct exec_list *list, struct var_to_subgroup_state *state)
{
   bool progress = false;
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         /* Nothing to do, we already gathered the info we need. */
         break;
      }
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);

         if (!nir_src_is_divergent(&nif->condition)) {
            progress |= linearize_write_cfg(&nif->then_list, state);
            progress |= linearize_write_cfg(&nif->else_list, state);
            break;
         }

         bool linearize = false;
         bool visit_then = false;
         bool visit_else = false;
         unsigned first_then = nir_if_first_then_block(nif)->index;
         unsigned last_then = nir_if_last_then_block(nif)->index;
         unsigned first_else = nir_if_first_else_block(nif)->index;
         unsigned last_else = nir_if_last_else_block(nif)->index;

         for (unsigned i = 0; i < state->values_len; i++) {
            struct shared_u32 *val = &state->values[i];

            bool then_write = BITSET_TEST_RANGE(val->written_in_blks, first_then, last_then);
            bool else_write = BITSET_TEST_RANGE(val->written_in_blks, first_else, last_else);

            linearize |= then_write && else_write;
            visit_then |= then_write;
            visit_else |= else_write;
         }

         if (visit_then)
            progress |= linearize_write_cfg(&nif->then_list, state);
         if (visit_else)
            progress |= linearize_write_cfg(&nif->else_list, state);

         if (linearize) {
            /*  Split ifs with atomics/writes in both then and else because the second
             *  atomic needs to be able to read the result of the first atomic.
             *  In:
             *  if (cond) {
             *     atomic(var);
             *  } else {
             *     atomic(var);
             *  }
             *
             *  Out:
             *  if (cond) {
             *     atomic(var);
             *  } else {
             *  }
             *  if (cond){
             *  } else {
             *     atomic(var);
             *  }
             */

            nir_lower_phis_to_regs_block(nir_cf_node_cf_tree_next(&nif->cf_node), true);

            state->b.cursor = nir_before_cf_node(node);
            nir_if *new_if = nir_push_if(&state->b, nif->condition.ssa);

            nir_cf_list then_content;
            nir_cf_list_extract(&then_content, &nif->then_list);
            nir_cf_reinsert(&then_content, nir_after_block(nir_if_last_then_block(new_if)));

            progress = true;
         }

         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);
         /* This pass doesn't support continues. */
         assert(exec_list_is_empty(&loop->continue_list));

         nir_block *loop_exit = nir_cf_node_cf_tree_next(node);

         if (!loop->divergent_break) {
            progress |= linearize_write_cfg(&loop->body, state);
            break;
         }

         unsigned first_block = nir_loop_first_block(loop)->index;
         unsigned last_block = nir_loop_last_block(loop)->index;

         if (nir_block_num_preds(loop_exit) == 1) {
            /* If there is a single divergent break, we still have to ensure
             * that no writes happen in divergent control flow that can reach
             * the break before reconverging. That is required to update the
             * replacement value even in lanes that don't exit the loop.
             *
             * We could seperate if vs else blocks here, but for similicity
             * track all writes from the start of divergent control flow
             * to the break.
             */
            nir_block *break_block;
            nir_foreach_pred(pred, loop_exit)
               break_block = pred;

            last_block = break_block->index;

            nir_cf_node *parent = break_block->cf_node.parent;
            while (parent->type != nir_cf_node_loop) {
               nir_if *nif = nir_cf_node_as_if(parent);
               if (nir_src_is_divergent(&nif->condition))
                  first_block = nir_if_first_then_block(nif)->index;
               parent = nif->cf_node.parent;
            }

         }

         bool has_write = false;
         for (unsigned i = 0; i < state->values_len; i++) {
            struct shared_u32 *val = &state->values[i];

            if (BITSET_TEST_RANGE(val->written_in_blks, first_block, last_block)) {
               has_write = true;
               break;
            }
         }

         if (!has_write) {
            if (nir_block_num_preds(loop_exit) == 1)
               progress |= linearize_write_cfg(&loop->body, state);
            break;
         }

         progress |= linearize_write_cfg(&loop->body, state);

         nir_convert_loop_to_lcssa(loop);
         nir_lower_phis_to_regs_block(loop_exit, true);
         nir_lower_phis_to_regs_block(nir_loop_first_block(loop), true);

         /* Simplify the loop to only have one divergent break at the end. */
         nir_simplify_loop(loop, nir_jump_break);
         progress = true;
         break;
      }
      case nir_cf_node_function:
         UNREACHABLE("Unsupported cf_node type.");
      }
   }

   return progress;
}

static void
init_shared_values(struct var_to_subgroup_state *state)
{
   bool explicit_layout = state->b.shader->info.shared_memory_explicit_layout;
   hash_table_foreach(state->shared_var_infos, entry) {
      struct shared_var_info *info = entry->data;
      const nir_variable *var = entry->key;

      if (!var->constant_initializer)
         continue;

      unsigned var_size;
      unsigned var_align;
      if (explicit_layout)
         var_size = glsl_get_explicit_size(var->type, false);
      else
         glsl_get_natural_size_align_bytes(var->type, &var_size, &var_align);

      unsigned count = DIV_ROUND_UP(var_size, 4);

      for (unsigned i = 0; i < count; i++) {
         BITSET_SET(state->values[info->offset / 4 + i].written_in_blks, 0);
      }
   }

   state->pb = nir_phi_builder_create(state->impl);

   for (unsigned i = 0; i < state->values_len; i++) {
      struct shared_u32 *value = &state->values[i];

      value->val = nir_phi_builder_add_value(state->pb, 1, 32, value->written_in_blks);
   }

   state->b.cursor = nir_before_block(nir_start_block(state->impl));
   nir_def *zero = NULL;

   hash_table_foreach(state->shared_var_infos, entry) {
      struct shared_var_info *info = entry->data;
      const nir_variable *var = entry->key;

      if (!var->constant_initializer)
         continue;
      assert(var->constant_initializer->is_null_constant);

      if (!zero)
         zero = nir_imm_zero(&state->b, 1, 32);

      unsigned var_size;
      unsigned var_align;
      if (explicit_layout)
         var_size = glsl_get_explicit_size(var->type, false);
      else
         glsl_get_natural_size_align_bytes(var->type, &var_size, &var_align);

      unsigned count = DIV_ROUND_UP(var_size, 4);

      nir_block *block = nir_start_block(state->impl);

      for (unsigned i = 0; i < count; i++) {
         struct shared_u32 *value = &state->values[info->offset / 4 + i];
         nir_phi_builder_value_set_block_def(value->val, block, zero);
      }
   }
}

bool
nir_opt_shared_vars_to_subgroup(nir_shader *shader,
                                unsigned ballot_num_components,
                                unsigned ballot_size)
{
   if (!mesa_shader_stage_uses_workgroup(shader->info.stage)) {
      nir_shader_preserve_all_metadata(shader);
      return false;
   }

   if (shader->info.workgroup_size_variable) {
      nir_shader_preserve_all_metadata(shader);
      return false;
   }

   unsigned workgroup_size = shader->info.workgroup_size[0] *
                             shader->info.workgroup_size[1] *
                             shader->info.workgroup_size[2];

   if (workgroup_size > shader->info.min_subgroup_size) {
      nir_shader_preserve_all_metadata(shader);
      return false;
   }

   struct var_to_subgroup_state state = {0};
   state.mem_ctx = ralloc_context(NULL);
   state.ballot_num_components = ballot_num_components;
   state.ballot_size = ballot_size;
   state.impl = nir_shader_get_entrypoint(shader);
   state.b = nir_builder_create(state.impl);
   state.shared_var_infos = _mesa_pointer_hash_table_create(state.mem_ctx);

   nir_foreach_variable_with_modes(var, shader, nir_var_mem_shared) {
      struct shared_var_info *info = rzalloc(state.mem_ctx, struct shared_var_info);

      _mesa_hash_table_insert(state.shared_var_infos, var, info);
   }

   if (_mesa_hash_table_num_entries(state.shared_var_infos) == 0) {
      nir_shader_preserve_all_metadata(shader);
      ralloc_free(state.mem_ctx);
      return false;
   }

   /* Check which (if any) variables can be optimized by this pass. */
   check_complex_uses(&state);

   if (_mesa_hash_table_num_entries(state.shared_var_infos) == 0) {
      nir_shader_preserve_all_metadata(shader);
      ralloc_free(state.mem_ctx);
      return false;
   }

   /* For each variable, assign an offset in the value
    * array where the data is stored.
    */
   calculate_var_offsets(&state);

   /* Gather where each value is written, both for control flow handling
    * and the phi builder.
    */
   gather_val_write_blocks(&state);

   nir_metadata_require(state.impl, nir_metadata_block_index | nir_metadata_dominance | nir_metadata_divergence);

   /* Linearize control flow, to allow values to be kept in uniform registers.
    *
    * Linearization is done in a loop, because with loop handling there can be
    * rare cases where new divergent ifs with writes on both sides can appear.
    * Even in the worst case, this will converge - at some point we will be out
    * of breaks to remove. In the common case we will only make progess once.
    */
   while (linearize_write_cfg(&state.impl->body, &state)) {
      nir_progress(true, state.impl, nir_metadata_none);
      nir_lower_reg_intrinsics_to_ssa_impl(state.impl);

      nir_metadata_require(state.impl, nir_metadata_block_index | nir_metadata_dominance | nir_metadata_divergence);

      /* We need to do this again because written_in_blks needs to consider new blocks. */
      gather_val_write_blocks(&state);
   }

   /* Create phi builder values, handle zero init. */
   init_shared_values(&state);

   nir_foreach_block(block, state.impl) {
      /* Re-uniformize variable values when control flow reconverges. */
      uniformize_vars(&state, block);

      nir_foreach_instr_safe(instr, block) {
         /* Lower any access to the shared variables to replace
          * them with uniform registers.
          */
         lower_shared_access(&state, instr);
      }
   }

   nir_phi_builder_finish(state.pb);

   /* Remove the variables that we optimized. */
   hash_table_foreach(state.shared_var_infos, entry) {
      nir_variable *var = (void *)entry->key;
      exec_node_remove(&var->node);
   }

   ralloc_free(state.mem_ctx);

   return nir_progress(true, state.impl, nir_metadata_control_flow);
}
