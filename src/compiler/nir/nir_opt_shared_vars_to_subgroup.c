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
#include "nir_range_analysis.h"

/* This pass optimizes workgroup shared memory access to subgroup operations.
 *
 * The first optimization handles array stores with local_invocation_index,
 * followed by loads with an index that stays within the subgroup and replaces
 * the loads with shuffles.
 * We could try to handle stores with other indices as well, but it becomes
 * harder to map to shuffle index. And there would be issues with control flow
 * or multiple writes to one element.
 *
 * The second optimization handles constant index access in single subgroup
 * workgroups, which can always be lowered to subgroup operations.
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
 */

struct shared_u32 {
   BITSET_WORD *written_in_blks;
   struct nir_phi_builder_value *val;
};

struct var_to_uniform_state {
   void *mem_ctx;
   struct hash_table *uniform_var_infos;

   unsigned ballot_num_components;
   unsigned ballot_size;

   nir_function_impl *impl;

   nir_builder b;
   struct nir_phi_builder *pb;

   struct shared_u32 *values;
   unsigned values_len;
};

struct uniform_var_info {
   unsigned offset;
};

static struct uniform_var_info *
get_uniform_var_info(struct var_to_uniform_state *state, nir_variable *var)
{
   struct hash_entry *entry =
      _mesa_hash_table_search(state->uniform_var_infos, var);
   return entry ? entry->data : NULL;
}

static void
remove_uniform_var_info(struct var_to_uniform_state *state, nir_variable *var)
{
   if (var->data.aliased_shared_memory) {
      hash_table_foreach(state->uniform_var_infos, entry) {
         nir_variable *iter_var = (nir_variable *)entry->key;
         if (iter_var->data.aliased_shared_memory)
            _mesa_hash_table_remove(state->uniform_var_infos, entry);
      }
   } else {
      _mesa_hash_table_remove_key(state->uniform_var_infos, var);
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
get_shared_deref_offset(struct var_to_uniform_state *state, nir_deref_instr *deref)
{
   if (!nir_deref_mode_may_be(deref, nir_var_mem_shared))
      return UINT32_MAX;

   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (!var)
      return UINT32_MAX;

   struct uniform_var_info *info = get_uniform_var_info(state, var);
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
uniformize_block_def(struct var_to_uniform_state *state, struct shared_u32 *value, nir_block *block)
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
uniformize_vars(struct var_to_uniform_state *state, nir_block *block)
{
   for (unsigned i = 0; i < state->values_len; i++)
      uniformize_block_def(state, &state->values[i], block);
}

static nir_def *
read_shared_data_uniform(struct var_to_uniform_state *state, unsigned offset, unsigned bit_size)
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
write_shared_data_uniform(struct var_to_uniform_state *state, unsigned offset, nir_def *data)
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
lower_shared_access_uniform(struct var_to_uniform_state *state, nir_instr *instr)
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
         comps[i] = read_shared_data_uniform(state, offset + i * bit_size / 8, bit_size);

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
         write_shared_data_uniform(state, offset + i * bit_size / 8, write);
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
         nir_def *load = read_shared_data_uniform(state, comp_offset, bit_size);

         nir_def *reduce = NULL;
         reduce_data(&state->b, op, data, &reduce, combined_scan_reduce ? &comps[i] : NULL, load);
         if (!combined_scan_reduce && return_prev)
            reduce_data(&state->b, op, data, NULL, &comps[i], load);

         write_shared_data_uniform(state, comp_offset, reduce);
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
deref_is_uniformizable(nir_deref_instr *deref)
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

         if (!deref_is_uniformizable(use_deref))
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
check_non_uniformizable_uses_instr(nir_instr *instr,
                                   struct var_to_uniform_state *state,
                                   bool inside_call)
{
   if (instr->type != nir_instr_type_deref)
      return;

   nir_deref_instr *deref = nir_instr_as_deref(instr);
   if (deref->deref_type != nir_deref_type_var)
      return;

   if (!(deref->var->data.mode & nir_var_mem_shared))
      return;

   if (!get_uniform_var_info(state, deref->var))
      return;

   if (inside_call || !deref_is_uniformizable(deref))
      remove_uniform_var_info(state, deref->var);
}

static void
check_non_uniformizable_uses(struct var_to_uniform_state *state)
{
   nir_foreach_function_impl(impl, state->b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            bool is_call = impl != state->impl;
            check_non_uniformizable_uses_instr(instr, state, is_call);
         }
      }
   }
}

static void
calculate_uniform_var_offsets(struct var_to_uniform_state *state)
{
   /* Iterate over the shader variable list instead of the hash table
    * for determinism.
    */
   unsigned next_offset = 0;
   bool has_aliased = false;
   bool explicit_layout = state->b.shader->info.shared_memory_explicit_layout;
   nir_foreach_variable_with_modes(var, state->b.shader, nir_var_mem_shared) {
      struct uniform_var_info *info = get_uniform_var_info(state, var);

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

         struct uniform_var_info *info = get_uniform_var_info(state, var);

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
gather_uniform_write_instr(nir_instr *instr, struct var_to_uniform_state *state)
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
gather_uniform_write_blocks(struct var_to_uniform_state *state)
{
   unsigned num_blk_words = BITSET_WORDS(state->impl->num_blocks);
   for (unsigned i = 0; i < state->values_len; i++) {
      state->values[i].written_in_blks =
         rzalloc_array(state->mem_ctx, BITSET_WORD, num_blk_words);
   }

   nir_foreach_block(block, state->impl) {
      nir_foreach_instr(instr, block) {
         gather_uniform_write_instr(instr, state);
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
linearize_write_cfg(struct exec_list *list, struct var_to_uniform_state *state)
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
init_shared_values(struct var_to_uniform_state *state)
{
   bool explicit_layout = state->b.shader->info.shared_memory_explicit_layout;
   hash_table_foreach(state->uniform_var_infos, entry) {
      struct uniform_var_info *info = entry->data;
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

   hash_table_foreach(state->uniform_var_infos, entry) {
      struct uniform_var_info *info = entry->data;
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

static bool
has_single_subgroup_workgroup(nir_shader *shader)
{
   if (shader->info.workgroup_size_variable)
      return false;

   return nir_static_workgroup_size(shader) <= shader->info.min_subgroup_size;
}

static bool
optimize_constant_access_to_uniform(nir_shader *shader,
                                    const nir_opt_shared_vars_to_subgroup_options *options)
{
   if (!has_single_subgroup_workgroup(shader))
      return false;

   struct var_to_uniform_state state = { 0 };
   state.mem_ctx = ralloc_context(NULL);
   state.ballot_num_components = options->ballot_num_components;
   state.ballot_size = options->ballot_size;
   state.impl = nir_shader_get_entrypoint(shader);
   state.b = nir_builder_create(state.impl);
   state.uniform_var_infos = _mesa_pointer_hash_table_create(state.mem_ctx);

   nir_foreach_variable_with_modes(var, shader, nir_var_mem_shared) {
      struct uniform_var_info *info = rzalloc(state.mem_ctx, struct uniform_var_info);

      _mesa_hash_table_insert(state.uniform_var_infos, var, info);
   }

   if (_mesa_hash_table_num_entries(state.uniform_var_infos) == 0) {
      ralloc_free(state.mem_ctx);
      return false;
   }

   /* Check which (if any) variables can be optimized by this path. */
   check_non_uniformizable_uses(&state);

   if (_mesa_hash_table_num_entries(state.uniform_var_infos) == 0) {
      ralloc_free(state.mem_ctx);
      return false;
   }

   /* For each variable, assign an offset in the value
    * array where the data is stored.
    */
   calculate_uniform_var_offsets(&state);

   /* Gather where each value is written, both for control flow handling
    * and the phi builder.
    */
   gather_uniform_write_blocks(&state);

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
      gather_uniform_write_blocks(&state);
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
         lower_shared_access_uniform(&state, instr);
      }
   }

   nir_phi_builder_finish(state.pb);

   /* Remove the variables that we optimized. */
   hash_table_foreach(state.uniform_var_infos, entry) {
      nir_variable *var = (void *)entry->key;
      exec_node_remove(&var->node);
   }

   ralloc_free(state.mem_ctx);

   return nir_progress(true, state.impl, nir_metadata_control_flow);
}

struct var_to_shuffle_state {
   void *mem_ctx;
   struct hash_table *shuffle_var_infos;
   struct hash_table *range_ht;
   struct hash_table *num_lsb_zero_ht;
   uint32_t num_var_components;

   bool linear_workgroup_ids;

   nir_function_impl *impl;
   nir_builder b;

   /* For each block, whether a variable is
    * the same as the register of workgroup linear writes,
    * but only in active invocations.
    */
   BITSET_WORD *var_in_register;
};

struct shuffle_var_info {
   uint32_t index;
   uint32_t num_components;
   /* As long as we haven't found a shuffle,
    * shuffle_data_reg will be NULL and all workgroup linear
    * writes are added to tracked_writes.
    */
   nir_def *shuffle_data_reg;
   struct util_dynarray tracked_writes;
   nir_block *last_write_block;
};

static struct shuffle_var_info *
get_shuffle_var_info(struct var_to_shuffle_state *state, nir_variable *var)
{
   struct hash_entry *entry =
      _mesa_hash_table_search(state->shuffle_var_infos, var);

   return entry ? entry->data : NULL;
}

static BITSET_WORD *
block_bitset(struct var_to_shuffle_state *state, nir_block *block)
{
   unsigned vars_bitset_words = BITSET_WORDS(state->num_var_components);
   return &state->var_in_register[vars_bitset_words * block->index];
}

static void
mark_not_in_reg(struct var_to_shuffle_state *state,
                nir_variable *var,
                BITSET_WORD *vars_in_reg)
{
   if (!var) {
      BITSET_CLEAR_COUNT(vars_in_reg, 0, state->num_var_components);
      return;
   }

   struct shuffle_var_info *info = get_shuffle_var_info(state, var);
   if (!info)
      return;

   BITSET_CLEAR_COUNT(vars_in_reg, info->index, info->num_components);
}

enum index_src {
   SRC_LOCAL_ID_X = 0,
   SRC_LOCAL_ID_Y,
   SRC_LOCAL_ID_Z,
   SRC_LOCAL_INDEX,
   SRC_SUBGROUP_ID,
   SRC_SUBGROUP_INVOCATION,

   INDEX_SRC_COUNT,
};

#define INDEX_SRC_RECURSION_LIMIT 8

/* Parse an expression only using adds, multiplication/shifts with constants
 * and the intrinsics from `enum index_src`. Returns false if scalar is
 * not such an expression or the recursion limit is reached.
 *
 * scalar: the root of the remaining chain
 * factor: an array of INDEX_SRC_COUNT multiplication factors for each relevant intrinsic
 * mul: the current factor from multiplications/shifts later in the chain
 * depth: recursion limit
 */
static bool
parse_mul_add_chain(nir_scalar scalar, uint32_t *factors, uint32_t mul, uint32_t depth)
{
   if (depth++ > INDEX_SRC_RECURSION_LIMIT)
      return false;

   if (nir_scalar_is_intrinsic(scalar)) {
      switch (nir_scalar_intrinsic_op(scalar)) {
      case nir_intrinsic_load_local_invocation_id:
         factors[SRC_LOCAL_ID_X + scalar.comp] += mul;
         return true;
      case nir_intrinsic_load_local_invocation_index:
         factors[SRC_LOCAL_INDEX] += mul;
         return true;
      case nir_intrinsic_load_subgroup_id:
         factors[SRC_SUBGROUP_ID] += mul;
         return true;
      case nir_intrinsic_load_subgroup_invocation:
         factors[SRC_SUBGROUP_INVOCATION] += mul;
         return true;
      default:
         return false;
      }
   } else if (nir_scalar_is_alu(scalar)) {
      switch (nir_scalar_alu_op(scalar)) {
      case nir_op_iadd:
      case nir_op_imul:
      case nir_op_ishl:
         break;
      default:
         return false;
      }

      nir_scalar src0 = nir_scalar_chase_alu_src(scalar, 0);
      nir_scalar src1 = nir_scalar_chase_alu_src(scalar, 1);

      switch (nir_scalar_alu_op(scalar)) {
      case nir_op_iadd:
         return parse_mul_add_chain(src0, factors, mul, depth) &&
                parse_mul_add_chain(src1, factors, mul, depth);
      case nir_op_imul:
         if (!nir_scalar_is_const(src1))
            SWAP(src0, src1);
         if (!nir_scalar_is_const(src1))
            return false;
         mul *= nir_scalar_as_uint(src1);
         return parse_mul_add_chain(src0, factors, mul, depth);
      case nir_op_ishl:
         if (!nir_scalar_is_const(src1))
            return false;
         mul *= 1u << (nir_scalar_as_uint(src1) & 0x1f);
         return parse_mul_add_chain(src0, factors, mul, depth);
      default:
         UNREACHABLE("bad op");
      }
   } else {
      return false;
   }
}

/* Returns whether scalar is equal to
 * subgroup_id * subgroup_size + subgroup_invocation_id
 * if invocation_mask is NULL
 *
 * Returns the invocation_mask bitfield and whether scalar is equal to
 * subgroup_id * subgroup_size + (subgroup_invocation_id & *invocation_mask)
 * if invocation_mask is not NULL.
 */
static bool
is_linear_invocation_index(struct var_to_shuffle_state *state,
                           nir_scalar scalar, uint32_t *invocation_mask)
{
   nir_shader *shader = state->b.shader;

   if (invocation_mask)
      *invocation_mask = -1;

   uint32_t factors[INDEX_SRC_COUNT] = { 0 };
   if (!parse_mul_add_chain(scalar, factors, 1, 0))
      return false;

   uint32_t src_used = 0;
   for (unsigned i = 0; i < INDEX_SRC_COUNT; i++) {
      if (factors[i])
         src_used |= BITFIELD_BIT(i);
   }

   /* Simple case: scalar is only local_invocation_index and it's linear. */
   if (src_used & BITFIELD_BIT(SRC_LOCAL_INDEX)) {
      return (src_used & ~BITFIELD_BIT(SRC_LOCAL_INDEX)) == 0 &&
             factors[SRC_LOCAL_INDEX] == 1 &&
             state->linear_workgroup_ids;
   }

   uint32_t local_ids = BITFIELD_RANGE(SRC_LOCAL_ID_X, 3);
   if (src_used & local_ids) {
      if (src_used & ~local_ids)
         return false;
      if (!state->linear_workgroup_ids || shader->info.workgroup_size_variable)
         return false;

      /* Check if the scalar is calculated as
       * local_id_x + dim_x * local_id_y + dim_x * dim_y * local_id_z
       */
      unsigned expected_factor = 1;
      for (unsigned i = 0; i < 3; i++) {
         if (shader->info.workgroup_size[i] == 1) {
            continue; /* local_id[i] is always zero in this case. */
         } else if (expected_factor != factors[SRC_LOCAL_ID_X + i]) {
            if (!invocation_mask)
               return false;

            /* The component must only change the value inside the subgroup. */
            if (expected_factor * shader->info.workgroup_size[i] > shader->info.min_subgroup_size)
               return false;

            /* Check if we can cleanly represent the difference with a mask. */
            if (factors[SRC_LOCAL_ID_X + i] != 0)
               return false; /* XXX Maybe we could improve this case? */
            if (!util_is_power_of_two_nonzero(expected_factor))
               return false;
            if (!util_is_power_of_two_nonzero(shader->info.workgroup_size[i]))
               return false;

            *invocation_mask &= ~((shader->info.workgroup_size[i] - 1) * expected_factor);
         }
         expected_factor *= shader->info.workgroup_size[i];
      }
      return true;
   }

   /* Check if scalar is subgroup_id/subgroup_invocation based. */
   if (factors[SRC_SUBGROUP_INVOCATION] > 1)
      return false;

   if (factors[SRC_SUBGROUP_INVOCATION] == 1 && has_single_subgroup_workgroup(shader))
      return true;

   if (shader->info.min_subgroup_size != shader->info.max_subgroup_size)
      return false;

   if (factors[SRC_SUBGROUP_ID] != shader->info.max_subgroup_size)
      return false;

   if (factors[SRC_SUBGROUP_INVOCATION] == 1)
      return true;

   if (!invocation_mask)
      return false;

   /* This expression computes subgroup_id * subgroup_size */
   *invocation_mask = 0;
   return true;
}

struct can_move_to_block_state {
   nir_block *block;
   unsigned depth;
};

static bool can_move_to_block_cb(nir_src *src, void *_state);

static bool
can_move_to_block(nir_def *def, nir_block *block, unsigned depth)
{
   if (depth++ > 6)
      return false;

   nir_instr *instr = nir_def_instr(def);

   if (nir_block_dominates(instr->block, block))
      return true;

   if (!nir_instr_can_speculate(instr))
      return false;

   struct can_move_to_block_state state = {
      .block = block,
      .depth = depth,
   };

   return nir_foreach_src(instr, can_move_to_block_cb, &state);
}

static bool
can_move_to_block_cb(nir_src *src, void *_state)
{
   struct can_move_to_block_state *state = _state;
   return can_move_to_block(src->ssa, state->block, state->depth);
}

static bool move_to_block_cb(nir_src *src, void *_state);

static void
move_instr_to_block(nir_def *def, nir_block *block)
{
   nir_instr *instr = nir_def_instr(def);

   if (nir_block_dominates(instr->block, block))
      return;

   nir_foreach_src(instr, move_to_block_cb, block);

   nir_instr_move(nir_after_block(block), instr);
}

static bool
move_to_block_cb(nir_src *src, void *_state)
{
   nir_block *block = _state;
   move_instr_to_block(src->ssa, block);
   return true;
}

struct shuffle_idx_op {
   nir_op op;
   nir_scalar src;
};

static bool
parse_iand(struct var_to_shuffle_state *state, nir_scalar *src, struct shuffle_idx_op *res)
{
   if (nir_scalar_alu_op(*src) != nir_op_iand)
      return false;

   nir_shader *shader = state->b.shader;

   unsigned max_workgroup_size = nir_static_workgroup_size(shader);
   if (shader->info.workgroup_size_variable) {
      if (shader->options->max_workgroup_invocations)
         max_workgroup_size = shader->options->max_workgroup_invocations;
      else
         max_workgroup_size = UINT16_MAX;
   }

   uint32_t required_bits = (util_next_power_of_two(max_workgroup_size) - 1) &
                            ~(shader->info.min_subgroup_size - 1);

   for (unsigned i = 0; i < 2; i++) {
      nir_scalar other = nir_scalar_chase_alu_src(*src, i);
      if (!nir_scalar_is_const(other))
         continue;

      /* Check that the iand keeps the high bits intact. */
      if ((nir_scalar_as_uint(other) & required_bits) != required_bits)
         return false;

      res->src = other;
      res->op = nir_op_iand;
      *src = nir_scalar_chase_alu_src(*src, !i);
      return true;
   }

   return false;
}

static bool
parse_ior_ixor(struct var_to_shuffle_state *state, nir_scalar *src, struct shuffle_idx_op *res)
{
   nir_op op = nir_scalar_alu_op(*src);
   if (op != nir_op_ior && op != nir_op_ixor && op != nir_op_iadd)
      return false;

   nir_shader *shader = state->b.shader;

   for (unsigned i = 0; i < 2; i++) {
      nir_scalar other = nir_scalar_chase_alu_src(*src, i);
      nir_scalar next = nir_scalar_chase_alu_src(*src, !i);
      uint32_t uub = nir_unsigned_upper_bound(shader, state->range_ht, other);

      /* If the upper bound is larger than the subgroup size, we could modify
       * the high bits.
       */
      if (uub >= shader->info.min_subgroup_size)
         continue;

      if (op == nir_op_iadd) {
         /* If the zero lsb cover all of the uub bits, this iadd is effectively an ior
          * and we know it doesn't overflow into the high bits
          */
         unsigned required_num_lsb = util_last_bit(uub);
         unsigned num_lsb = nir_def_num_lsb_zero(state->num_lsb_zero_ht, next);
         if (required_num_lsb > num_lsb)
            return false;
      }

      res->src = other;
      res->op = op;
      *src = next;
      return true;
   }

   return false;
}

static nir_def *
load_deref_shuffle_index(struct var_to_shuffle_state *state,
                         nir_deref_instr *deref,
                         nir_block *shuffle_block)
{
   if (deref->deref_type != nir_deref_type_array)
      return NULL;

   nir_deref_instr *deref_var = nir_src_as_deref(deref->parent);
   if (deref_var->deref_type != nir_deref_type_var)
      return NULL;

   nir_shader *shader = state->b.shader;

   nir_def *shuffle_idx = deref->arr.index.ssa;

   if (!can_move_to_block(shuffle_idx, shuffle_block, 0))
      return NULL;

   if (has_single_subgroup_workgroup(shader)) {
      /* Any index is ok, as long as it can only access written elements. */
      unsigned workgroup_size = nir_static_workgroup_size(shader);

      if (glsl_array_size(deref_var->var->type) > workgroup_size) {

         nir_scalar src = nir_scalar_resolved(shuffle_idx, 0);

         if (nir_unsigned_upper_bound(shader, state->range_ht, src) >= workgroup_size)
            return NULL;
      }
   } else {
      struct shuffle_idx_op index_ops[6];

      uint32_t invocation_mask = 0;
      nir_scalar src = nir_scalar_resolved(shuffle_idx, 0);

      /* Check that the index stays within each subgroup. */
      unsigned num_ops = 0;
      for (;; num_ops++) {
         if (is_linear_invocation_index(state, src, &invocation_mask))
            break;

         if (num_ops >= ARRAY_SIZE(index_ops))
            return NULL;

         if (!nir_scalar_is_alu(src))
            return NULL;

         /* Allow an arbitrary chain of iand/ior/ixor/iadd as long as
          * the upper bits (subgroup_id * subgroup_size) remain intact.
          */
         if (!parse_iand(state, &src, &index_ops[num_ops]) &&
             !parse_ior_ixor(state, &src, &index_ops[num_ops]))
            return NULL;
      }

      state->b.cursor = nir_before_instr(&deref->instr);

      /* Reconstruct the shuffle_idx, based on subgroup invocation id,
       * with the upper bits (subgroup_id * subgroup_size) masked out
       * and the ALU-chain re-applied.
       */
      if (invocation_mask != 0) {
         /* is_linear_invocation_index() guarantees that for the invocation_mask
          * at least all bits > min_subgroup_size are being set and the resulting
          * shuffle_idx will always remain within each part of min_subgroup_size.
          */
         shuffle_idx = nir_load_subgroup_invocation(&state->b);
         shuffle_idx = nir_iand_imm(&state->b, shuffle_idx, invocation_mask);
      } else {
         /* is_linear_invocation_index() guarantees that the upper bits
          * of the shuffle_idx are exactly subgroup_id * subgroup_size
          * and min_subgroup_size == max_subgroup_size.
          * The reconstructed shuffle_idx is solely composed of the ALU-chain.
          */
         shuffle_idx = nir_imm_int(&state->b, 0);
      }

      for (int i = num_ops - 1; i >= 0; i--) {
         nir_def *other = nir_mov_scalar(&state->b, index_ops[i].src);
         shuffle_idx = nir_build_alu2(&state->b, index_ops[i].op, shuffle_idx, other);
      }
   }

   move_instr_to_block(shuffle_idx, shuffle_block);

   return shuffle_idx;
}


static bool
store_deref_can_use_shuffle(struct var_to_shuffle_state *state,
                            nir_intrinsic_instr *store)
{
   /* Only accept stores where all active invocations access the element
    * that's at the index of subgroup_id * subgroup_size + subgroup_invocation.
    */
   nir_deref_instr *deref = nir_src_as_deref(store->src[0]);
   if (deref->deref_type != nir_deref_type_array)
      return false;

   nir_deref_instr *deref_var = nir_src_as_deref(deref->parent);
   if (deref_var->deref_type != nir_deref_type_var)
      return false;

   nir_scalar array_idx = nir_scalar_resolved(deref->arr.index.ssa, 0);

   if (nir_scalar_is_const(array_idx)) {
      /* If the array index is constant, check if only
       * that exact invocation is active.
       */
      nir_cf_node *nif = store->instr.block->cf_node.parent;
      if (nif->type != nir_cf_node_if)
         return false;

      nir_block *first_then = nir_if_first_then_block(nir_cf_node_as_if(nif));
      nir_block *last_then = nir_if_last_then_block(nir_cf_node_as_if(nif));
      bool within_then = store->instr.block->index >= first_then->index &&
                         store->instr.block->index <= last_then->index;

      nir_op cmp_op = within_then ? nir_op_ieq : nir_op_ine;

      nir_scalar if_cond = nir_scalar_resolved(nir_cf_node_as_if(nif)->condition.ssa, 0);
      if (!nir_scalar_is_alu(if_cond) || nir_scalar_alu_op(if_cond) != cmp_op)
         return false;

      nir_scalar src0 = nir_scalar_chase_alu_src(if_cond, 0);
      nir_scalar src1 = nir_scalar_chase_alu_src(if_cond, 1);

      if (nir_scalar_equal(array_idx, src0))
         return is_linear_invocation_index(state, src1, NULL);
      if (nir_scalar_equal(array_idx, src1))
         return is_linear_invocation_index(state, src0, NULL);
      return false;
   }

   return is_linear_invocation_index(state, array_idx, NULL);
}

static void
store_shared_shuffle(struct var_to_shuffle_state *state,
                     struct shuffle_var_info *info,
                     nir_intrinsic_instr *store)
{
   state->b.cursor = nir_before_instr(&store->instr);

   nir_intrinsic_instr *reg_store =
      nir_store_reg(&state->b, store->src[1].ssa, info->shuffle_data_reg);
   nir_intrinsic_set_write_mask(reg_store, nir_intrinsic_write_mask(store));
}

static void
prepare_shuffle_data_reg(struct var_to_shuffle_state *state,
                         struct shuffle_var_info *info,
                         nir_variable *var)
{
   if (info->shuffle_data_reg)
      return;

   /* Declare a register with the element type of the array. */
   assert(glsl_type_is_array_or_matrix(var->type));

   const glsl_type *element_type = glsl_get_array_element(var->type);
   assert(glsl_type_is_vector_or_scalar(element_type));

   unsigned bit_size = glsl_get_bit_size(element_type);
   unsigned num_components = glsl_get_components(element_type);

   info->shuffle_data_reg = nir_decl_reg(&state->b, num_components, bit_size, 0);

   /* Zero init the register if the shared variable is zero initialized. */
   if (var->constant_initializer) {
      assert(var->constant_initializer->is_null_constant);
      state->b.cursor = nir_after_reg_decls(state->impl);
      nir_def *zero = nir_imm_zero(&state->b, num_components, bit_size);
      nir_store_reg(&state->b, zero, info->shuffle_data_reg);
   }

   /* Now that we know we will make progress, store all of the previous
    * tracked writes to the register.
    */
   util_dynarray_foreach(&info->tracked_writes, nir_intrinsic_instr *, store)
      store_shared_shuffle(state, info, *store);

   util_dynarray_clear(&info->tracked_writes);
}

static bool
lower_shared_access_shuffle(struct var_to_shuffle_state *state,
                            nir_instr *instr,
                            BITSET_WORD *vars_in_reg)
{
   if (instr->type == nir_instr_type_call ||
       instr->type == nir_instr_type_cmat_call)
      mark_not_in_reg(state, NULL, vars_in_reg);

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_deref_atomic_swap: {
      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

      if (!nir_deref_mode_may_be(deref, nir_var_mem_shared))
         return false;

      nir_variable *var = nir_deref_instr_get_variable(deref);
      mark_not_in_reg(state, var, vars_in_reg);
      return false;
   }
   case nir_intrinsic_load_deref: {
      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

      if (!nir_deref_mode_must_be(deref, nir_var_mem_shared))
         return false;

      nir_variable *var = nir_deref_instr_get_variable(deref);
      if (!var)
         return false;

      struct shuffle_var_info *info = get_shuffle_var_info(state, var);
      if (!info)
         return false;

      unsigned components_used = nir_def_components_read(&intrin->def);

      u_foreach_bit(i, components_used) {
         if (!BITSET_TEST(vars_in_reg, info->index + i))
            return false;
      }

      /* Find the first previous uniform block to insert the shuffle in.
       * It has to be uniform, because we can't read from inactive invocations.
       */
      nir_block *shuffle_block = intrin->instr.block;
      while (shuffle_block->divergent) {
         if (shuffle_block->cf_node.parent->type != nir_cf_node_if)
            return false;

         shuffle_block = nir_cf_node_cf_tree_prev(shuffle_block->cf_node.parent);

         if (shuffle_block->index < info->last_write_block->index)
            return false;
      }

      nir_def *shuffle_idx = load_deref_shuffle_index(state, deref, shuffle_block);
      if (!shuffle_idx)
         return false;

      prepare_shuffle_data_reg(state, info, var);

      if (shuffle_block == intrin->instr.block) {
         state->b.cursor = nir_before_instr(&intrin->instr);
      } else {
         state->b.cursor = nir_after_block(shuffle_block);
      }

      nir_def *replace = nir_load_reg(&state->b, info->shuffle_data_reg);
      replace = nir_shuffle(&state->b, replace, shuffle_idx);
      nir_def_replace(&intrin->def, replace);

      return true;
   }
   case nir_intrinsic_store_deref: {
      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

      if (!nir_deref_mode_may_be(deref, nir_var_mem_shared))
         return false;

      nir_variable *var = nir_deref_instr_get_variable(deref);
      if (!var) {
         mark_not_in_reg(state, var, vars_in_reg);
         return false;
      }

      struct shuffle_var_info *info = get_shuffle_var_info(state, var);
      if (!info)
         return false;

      nir_component_mask_t write_mask = nir_intrinsic_write_mask(intrin);

      if (!store_deref_can_use_shuffle(state, intrin)) {
         if (deref->deref_type != nir_deref_type_array) {
            mark_not_in_reg(state, var, vars_in_reg);
         } else {
            nir_deref_instr *deref_var = nir_src_as_deref(deref->parent);
            if (deref_var->deref_type != nir_deref_type_var) {
               mark_not_in_reg(state, var, vars_in_reg);
            } else {
               u_foreach_bit(i, write_mask)
                  BITSET_CLEAR(vars_in_reg, info->index + i);
            }
         }
         return false;
      }

      if (info->shuffle_data_reg) {
         store_shared_shuffle(state, info, intrin);
      } else {
         /* If not yet used by a load,
          * just add this store to the list of tracked writes.
          */
         util_dynarray_append(&info->tracked_writes, intrin);
      }

      info->last_write_block = intrin->instr.block;
      u_foreach_bit(i, write_mask)
         BITSET_SET(vars_in_reg, info->index + i);

      return false;
   }
   default: {
      return false;
   }
   }
}

static bool
optimize_divergent_access_to_shuffle(nir_shader *shader,
                                     const nir_opt_shared_vars_to_subgroup_options *options)
{
   struct var_to_shuffle_state state = { 0 };
   state.mem_ctx = ralloc_context(NULL);
   state.impl = nir_shader_get_entrypoint(shader);
   state.b = nir_builder_create(state.impl);
   state.shuffle_var_infos = _mesa_pointer_hash_table_create(state.mem_ctx);
   state.range_ht = _mesa_pointer_hash_table_create(state.mem_ctx);
   state.num_lsb_zero_ht = _mesa_pointer_hash_table_create(state.mem_ctx);
   state.linear_workgroup_ids = options->linear_workgroup_ids;

   nir_foreach_variable_with_modes(var, shader, nir_var_mem_shared) {
      if (var->data.aliased_shared_memory || !glsl_type_is_array(var->type))
         continue;
      const glsl_type *element_type = glsl_get_array_element(var->type);
      if (!glsl_type_is_vector_or_scalar(element_type))
         continue;

      struct shuffle_var_info *info = rzalloc(state.mem_ctx, struct shuffle_var_info);
      info->index = state.num_var_components;
      info->num_components = glsl_get_components(element_type);
      state.num_var_components += info->num_components;
      util_dynarray_init(&info->tracked_writes, state.mem_ctx);
      info->last_write_block = nir_start_block(state.impl);

      _mesa_hash_table_insert(state.shuffle_var_infos, var, info);
   }

   if (!state.num_var_components) {
      ralloc_free(state.mem_ctx);
      return false;
   }

   nir_metadata_require(state.impl, nir_metadata_block_index | nir_metadata_dominance | nir_metadata_divergence);

   unsigned vars_bitset_words = BITSET_WORDS(state.num_var_components);
   state.var_in_register = rzalloc_array(state.mem_ctx, BITSET_WORD, vars_bitset_words * state.impl->num_blocks);

   bool progress = false;
   nir_foreach_block(block, state.impl) {

      BITSET_WORD *vars_in_reg = block_bitset(&state, block);
      BITSET_SET_COUNT(vars_in_reg, 0, state.num_var_components);

      /* TODO handle loops better, at the moment we assume the backedge
       * overwrites everything.
       */
      nir_foreach_pred(pred, block) {
         BITSET_WORD *other = block_bitset(&state, pred);

         __bitset_and(vars_in_reg, vars_in_reg, other, vars_bitset_words);
      }

      nir_foreach_instr_safe(instr, block) {
         progress |= lower_shared_access_shuffle(&state, instr, vars_in_reg);
      }
   }

   ralloc_free(state.mem_ctx);

   if (progress)
      nir_lower_reg_intrinsics_to_ssa_impl(state.impl);

   return nir_progress(progress, state.impl, nir_metadata_control_flow);
}

bool
nir_opt_shared_vars_to_subgroup(nir_shader *shader,
                                const nir_opt_shared_vars_to_subgroup_options *options)
{
   assert(mesa_shader_stage_uses_workgroup(shader->info.stage));

   bool progress = false;

   if (options->optimize_divergent_access_to_shuffle)
      progress |= optimize_divergent_access_to_shuffle(shader, options);

   if (options->optimize_constant_access_to_uniform)
      progress |= optimize_constant_access_to_uniform(shader, options);

   if (progress) {
      nir_function_impl *entry = nir_shader_get_entrypoint(shader);

      nir_foreach_function_impl(impl, shader) {
         if (impl != entry)
            nir_no_progress(impl);
      }
   } else {
      nir_shader_preserve_all_metadata(shader);
   }

   return progress;
}
