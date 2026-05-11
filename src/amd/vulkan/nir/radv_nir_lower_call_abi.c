/*
 * Copyright © 2023 Valve Corporation
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

#include "aco_nir_call_attribs.h"
#include "nir_builder.h"
#include "radv_nir.h"

void
radv_nir_lower_callee_signature(nir_function *function)
{
   nir_parameter *old_params = function->params;
   unsigned old_num_params = function->num_params;

   function->num_params += ACO_NIR_CALL_SYSTEM_ARG_COUNT;
   function->params = rzalloc_array_size(function->shader, function->num_params, sizeof(nir_parameter));

   memcpy(function->params + ACO_NIR_CALL_SYSTEM_ARG_COUNT, old_params, old_num_params * sizeof(nir_parameter));

   /* These are not return params, but each callee will modify these registers
    * as part of the next callee selection. Make sure modification is allowed by
    * marking the parameters as DISCARDABLE. Unlike other discardable parameters,
    * ACO makes sure correct values are always written to them.
    */
   function->params[ACO_NIR_CALL_SYSTEM_ARG_DIVERGENT_PC].num_components = 1;
   function->params[ACO_NIR_CALL_SYSTEM_ARG_DIVERGENT_PC].bit_size = 64;
   function->params[ACO_NIR_CALL_SYSTEM_ARG_DIVERGENT_PC].driver_attributes = ACO_NIR_PARAM_ATTRIB_DISCARDABLE;
   function->params[ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC].num_components = 1;
   function->params[ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC].bit_size = 64;
   function->params[ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC].is_uniform = true;
   function->params[ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC].driver_attributes = ACO_NIR_PARAM_ATTRIB_DISCARDABLE;

   for (unsigned i = ACO_NIR_CALL_SYSTEM_ARG_COUNT; i < function->num_params; ++i) {
      if (!function->params[i].is_return)
         continue;

      function->params[i].bit_size = glsl_get_bit_size(function->params[i].type);
      function->params[i].num_components = glsl_get_vector_elements(function->params[i].type);
   }

   nir_function_impl *impl = function->impl;

   if (!impl)
      return;

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

         if (intr->intrinsic == nir_intrinsic_load_param)
            nir_intrinsic_set_param_idx(intr, nir_intrinsic_param_idx(intr) + ACO_NIR_CALL_SYSTEM_ARG_COUNT);
      }
   }
}

/* Checks if caller can call callee using tail calls.
 *
 * If the ABIs mismatch, we might need to insert move instructions to move return values from callee return registers to
 * caller return registers after the call. In that case, tail-calls are impossible to do correctly.
 */
static bool
is_tail_call_compatible(nir_function *caller, nir_function *callee)
{
   /* If the caller doesn't return at all, we don't need to care if return params are compatible */
   if (caller->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_NORETURN)
      return true;
   /* The same ABI can't mismatch */
   if ((caller->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK) ==
       (callee->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK))
      return true;
   /* The recursive shader ABI and the traversal shader ABI are built so that return parameters occupy exactly
    * the same registers, to allow tail calls from the traversal shader. */
   if ((caller->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK) == ACO_NIR_CALL_ABI_TRAVERSAL &&
       (callee->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK) == ACO_NIR_CALL_ABI_RT_RECURSIVE)
      return true;
   return false;
}

static void
gather_tail_call_instrs_block(nir_function *caller, const struct nir_block *block, struct set *tail_calls)
{
   nir_foreach_instr_reverse (instr, block) {
      /* Making an instruction a tail call effectively moves it beyond the last block. If there are any instructions
       * in the way, this reordering may be incorrect.
       */
      if (instr->type != nir_instr_type_call)
         return;
      nir_call_instr *call = nir_instr_as_call(instr);

      if (!is_tail_call_compatible(caller, call->callee))
         return;
      if (call->callee->num_params != caller->num_params)
         return;

      for (unsigned i = 0; i < call->callee->num_params; ++i) {
         if (call->callee->params[i].is_return != caller->params[i].is_return)
            return;
         if ((call->callee->params[i].driver_attributes & ACO_NIR_PARAM_ATTRIB_DISCARDABLE) &&
             !(caller->params[i].driver_attributes & ACO_NIR_PARAM_ATTRIB_DISCARDABLE))
            return;
         bool has_preserved_regs =
            (caller->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_ABI_MASK) == ACO_NIR_CALL_ABI_AHIT_ISEC;
         if (has_preserved_regs && ((call->callee->params[i].driver_attributes & ACO_NIR_PARAM_ATTRIB_DISCARDABLE) !=
                                    (caller->params[i].driver_attributes & ACO_NIR_PARAM_ATTRIB_DISCARDABLE)))
            return;
         if (call->callee->params[i].is_uniform != caller->params[i].is_uniform)
            return;
         if (call->callee->params[i].bit_size != caller->params[i].bit_size)
            return;
         if (call->callee->params[i].num_components != caller->params[i].num_components)
            return;
      }

      /* The call instruction itself has not been lowered to the new signature yet, so do this in a separate loop and
       * adjust parameter indices for the caller.
       */
      for (unsigned i = 0; i < call->num_params; ++i) {
         unsigned caller_param_idx = i + ACO_NIR_CALL_SYSTEM_ARG_COUNT;
         /* We can only do tail calls if the caller returns exactly the callee return values */
         if (caller->params[caller_param_idx].is_return) {
            assert(nir_def_as_deref_or_null(call->params[i].ssa));
            nir_deref_instr *deref_root = nir_def_as_deref(call->params[i].ssa);
            while (nir_deref_instr_parent(deref_root))
               deref_root = nir_deref_instr_parent(deref_root);

            if (!deref_root->parent.ssa)
               return;
            nir_intrinsic_instr *intrin = nir_def_as_intrinsic_or_null(deref_root->parent.ssa);
            if (!intrin || intrin->intrinsic != nir_intrinsic_load_param)
               return;
            if (nir_intrinsic_param_idx(intrin) != caller_param_idx)
               return;
         } else if (!(caller->params[caller_param_idx].driver_attributes & ACO_NIR_PARAM_ATTRIB_DISCARDABLE)) {
            /* If the parameter is not marked as discardable, then we have to preserve the caller's value. Passing
             * a modified value to a tail call leaves us unable to restore the original value, so bail out if we have
             * modified parameters.
             */
            nir_intrinsic_instr *intrin = nir_def_as_intrinsic_or_null(call->params[i].ssa);
            if (!intrin || intrin->intrinsic != nir_intrinsic_load_param ||
                nir_intrinsic_param_idx(intrin) != caller_param_idx)
               return;
         }
      }

      _mesa_set_add(tail_calls, instr);
   }

   nir_foreach_pred (pred, block)
      gather_tail_call_instrs_block(caller, pred, tail_calls);
}

struct lower_param_info {
   nir_def *return_deref;
   nir_variable *param_var;
};

static void
rewrite_return_param_uses(nir_def *def, unsigned param_idx, struct lower_param_info *param_defs)
{
   nir_foreach_use_safe (use, def) {
      nir_instr *use_instr = nir_src_use_instr(use);

      if (use_instr->type == nir_instr_type_deref) {
         assert(nir_instr_as_deref(use_instr)->deref_type == nir_deref_type_cast);
         rewrite_return_param_uses(&nir_instr_as_deref(use_instr)->def, param_idx, param_defs);
         nir_instr_remove(use_instr);
      }
   }
   nir_def_rewrite_uses(def, param_defs[param_idx].return_deref);
}

static void
lower_call_abi_for_callee(nir_function *function, unsigned wave_size)
{
   nir_function_impl *impl = function->impl;

   nir_builder b = nir_builder_create(impl);
   b.cursor = nir_before_impl(impl);

   nir_variable *tail_call_pc =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint64_t_type(), "_tail_call_pc");

   struct set *tail_call_instrs = _mesa_set_create(b.shader, _mesa_hash_pointer, _mesa_key_pointer_equal);
   gather_tail_call_instrs_block(function, nir_impl_last_block(impl), tail_call_instrs);

   /* guard the shader, so that only the correct invocations execute it */

   nir_def *guard_condition = NULL;
   nir_def *shader_addr;
   nir_def *uniform_shader_addr;
   if (function->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL) {
      nir_cf_list list;
      nir_cf_extract(&list, nir_before_impl(impl), nir_after_impl(impl));

      b.cursor = nir_before_impl(impl);

      shader_addr = nir_load_param(&b, ACO_NIR_CALL_SYSTEM_ARG_DIVERGENT_PC);
      uniform_shader_addr = nir_load_param(&b, ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC);

      nir_store_var(&b, tail_call_pc, shader_addr, 0x1);

      guard_condition = nir_ieq(&b, uniform_shader_addr, shader_addr);
      nir_if *shader_guard = nir_push_if(&b, guard_condition);
      shader_guard->control = nir_selection_control_divergent_always_taken;
      nir_store_var(&b, tail_call_pc, nir_imm_int64(&b, 0), 0x1);
      nir_cf_reinsert(&list, b.cursor);
      nir_pop_if(&b, shader_guard);
   } else {
      nir_store_var(&b, tail_call_pc, nir_imm_int64(&b, 0), 0x1);
   }

   b.cursor = nir_before_impl(impl);
   struct lower_param_info *param_infos = ralloc_size(b.shader, function->num_params * sizeof(struct lower_param_info));

   for (unsigned i = ACO_NIR_CALL_SYSTEM_ARG_COUNT; i < function->num_params; ++i) {
      param_infos[i].param_var = nir_local_variable_create(impl, function->params[i].type, "_param");

      if (function->params[i].is_return) {
         assert(!glsl_type_is_array(function->params[i].type) && !glsl_type_is_struct(function->params[i].type));

         param_infos[i].return_deref = &nir_build_deref_var(&b, param_infos[i].param_var)->def;
      } else {
         param_infos[i].return_deref = NULL;
      }
   }

   /* Lower everything related to call parameters and dispatch, particularly return parameters and tail calls.
    *
    * Return parameters in NIR are represented by having the parameter value actually be a deref. Callers pass
    * a deref value to the call, and the callee can cast the parameter value back to a deref. Replace these deref_casts
    * with a deref of a variable we declared further above, so the shader can be lowered to SSA.
    * One simple example:
    *
    * %1 = load_param 0
    * %2 = deref_cast %1
    * %3 = load_deref %2
    * %4 = inot %3
    * store_deref %2, %4
    *
    * becomes
    *
    * decl_var _param;
    *
    * %1 = load_param 0
    * store_var _param, %1
    * %3 = load_var _param
    * %4 = inot %3
    * store_var _param, %4
    * %5 = load_var _param
    * store_param_amd %5
    *
    * If tail calls are detected, the call instruction is replaced with a sequence of writing the parameters to the new
    * values they should have at callee entry, and updating the tail_call_pc value so that the callee is jumped to next.
    */
   bool has_tail_call = false;
   nir_foreach_block (block, impl) {
      bool progress;
      /* rewrite_return_param_uses may remove multiple instructions (not just the current one), which not even
       * nir_foreach_instr_safe can safely iterate over. Therefore, if we made progress, we need to restart iteration.
       */
      do {
         progress = false;
         nir_foreach_instr (instr, block) {
            if (instr->type == nir_instr_type_call && _mesa_set_search(tail_call_instrs, instr)) {
               nir_call_instr *call = nir_instr_as_call(instr);
               b.cursor = nir_before_instr(instr);

               for (unsigned i = 0; i < call->num_params; ++i) {
                  if (call->callee->params[i + ACO_NIR_CALL_SYSTEM_ARG_COUNT].is_return)
                     nir_store_var(&b, param_infos[i + ACO_NIR_CALL_SYSTEM_ARG_COUNT].param_var,
                                   nir_load_deref(&b, nir_def_as_deref(call->params[i].ssa)),
                                   (0x1 << glsl_get_vector_elements(
                                       call->callee->params[i + ACO_NIR_CALL_SYSTEM_ARG_COUNT].type)) -
                                      1);
                  else
                     nir_store_var(&b, param_infos[i + ACO_NIR_CALL_SYSTEM_ARG_COUNT].param_var, call->params[i].ssa,
                                   (0x1 << call->params[i].ssa->num_components) - 1);
               }

               nir_store_var(&b, tail_call_pc, call->indirect_callee.ssa, 0x1);

               nir_instr_remove(instr);

               has_tail_call = true;
               progress = true;
               break;
            }

            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_param) {
               unsigned param_idx = nir_intrinsic_param_idx(intr);

               if (param_idx >= ACO_NIR_CALL_SYSTEM_ARG_COUNT && function->params[param_idx].is_return) {
                  rewrite_return_param_uses(&intr->def, param_idx, param_infos);
                  nir_instr_remove(instr);
                  progress = true;
                  break;
               } else if (param_idx >= ACO_NIR_CALL_SYSTEM_ARG_COUNT) {
                  b.cursor = nir_before_instr(instr);
                  nir_def_replace(&intr->def, nir_load_var(&b, param_infos[param_idx].param_var));
                  progress = true;
                  break;
               }
            }
         }
      } while (progress);
   }

   b.cursor = nir_before_impl(impl);

   for (unsigned i = ACO_NIR_CALL_SYSTEM_ARG_COUNT; i < function->num_params; ++i) {
      unsigned num_components = glsl_get_vector_elements(function->params[i].type);
      nir_store_var(&b, param_infos[i].param_var, nir_load_param(&b, i), (0x1 << num_components) - 1);
   }

   /* Setup a jump to a different shader in the cases where there is a next shader to be called. */
   if (!(function->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_NORETURN) ||
       (function->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL) || has_tail_call) {
      b.cursor = nir_after_impl(impl);

      for (unsigned i = ACO_NIR_CALL_SYSTEM_ARG_COUNT; i < function->num_params; ++i)
         nir_store_param_amd(&b, nir_load_var(&b, param_infos[i].param_var), .param_idx = i);
      shader_addr = nir_load_var(&b, tail_call_pc);
      nir_def *ballot = nir_ballot(&b, 1, wave_size, nir_ine_imm(&b, shader_addr, 0));
      nir_def *ballot_addr = nir_read_invocation(&b, shader_addr, nir_find_lsb(&b, ballot));

      nir_def *no_next_shader = nir_ieq_imm(&b, ballot, 0);
      nir_def *terminate_cond;

      /* In functions marked noreturn, we don't need to bother checking the call return address. */
      if (function->driver_attributes & ACO_NIR_FUNCTION_ATTRIB_NORETURN) {
         uniform_shader_addr = ballot_addr;
         terminate_cond = no_next_shader;
      } else {
         nir_def *return_address = nir_load_call_return_address_amd(&b);
         uniform_shader_addr = nir_bcsel(&b, no_next_shader, return_address, ballot_addr);
         /* If the next shader address is zero for every invocation, return. */
         terminate_cond = nir_ieq_imm(&b, uniform_shader_addr, 0);
      }

      nir_push_if(&b, terminate_cond);
      nir_terminate(&b);
      nir_pop_if(&b, NULL);

      nir_set_next_call_pc_amd(&b, shader_addr, uniform_shader_addr);
   }
}

static void
lower_call_abi_for_call(nir_builder *b, nir_call_instr *call, unsigned *cur_call_idx)
{
   unsigned call_idx = (*cur_call_idx)++;

   for (unsigned i = 0; i < call->num_params; ++i) {
      unsigned callee_param_idx = i + ACO_NIR_CALL_SYSTEM_ARG_COUNT;

      if (!call->callee->params[callee_param_idx].is_return)
         continue;

      b->cursor = nir_before_instr(&call->instr);

      nir_src *old_src = &call->params[i];

      assert(nir_def_as_deref_or_null(old_src->ssa));
      nir_deref_instr *param_deref = nir_def_as_deref(old_src->ssa);
      assert(param_deref->deref_type == nir_deref_type_var);

      nir_src_rewrite(old_src, nir_load_deref(b, param_deref));

      b->cursor = nir_after_instr(&call->instr);

      unsigned num_components = glsl_get_vector_elements(param_deref->type);

      nir_store_deref(
         b, param_deref,
         nir_load_return_param_amd(b, num_components, glsl_base_type_get_bit_size(param_deref->type->base_type),
                                   .call_idx = call_idx, .param_idx = callee_param_idx),
         (1u << num_components) - 1);
   }

   b->cursor = nir_before_instr(&call->instr);

   nir_call_instr *new_call = nir_call_instr_create(b->shader, call->callee);
   new_call->indirect_callee = nir_src_for_ssa(call->indirect_callee.ssa);
   new_call->params[ACO_NIR_CALL_SYSTEM_ARG_DIVERGENT_PC] = nir_src_for_ssa(call->indirect_callee.ssa);
   new_call->params[ACO_NIR_CALL_SYSTEM_ARG_UNIFORM_PC] =
      nir_src_for_ssa(nir_read_first_invocation(b, call->indirect_callee.ssa));
   for (unsigned i = ACO_NIR_CALL_SYSTEM_ARG_COUNT; i < new_call->num_params; ++i)
      new_call->params[i] = nir_src_for_ssa(call->params[i - ACO_NIR_CALL_SYSTEM_ARG_COUNT].ssa);

   nir_builder_instr_insert(b, &new_call->instr);
   nir_instr_remove(&call->instr);
}

static bool
lower_call_abi_for_caller(nir_function_impl *impl)
{
   bool progress = false;
   unsigned cur_call_idx = 0;

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_call)
            continue;
         nir_call_instr *call = nir_instr_as_call(instr);
         if (call->callee->impl)
            continue;

         nir_builder b = nir_builder_create(impl);
         lower_call_abi_for_call(&b, call, &cur_call_idx);
         progress = true;
      }
   }

   return progress;
}

bool
radv_nir_lower_call_abi(nir_shader *shader, unsigned wave_size)
{
   bool progress = false;
   nir_foreach_function (function, shader) {
      if (function->is_exported) {
         radv_nir_lower_callee_signature(function);
         if (function->impl)
            progress |= nir_progress(true, function->impl, nir_metadata_none);
      }
   }

   nir_foreach_function_with_impl (function, impl, shader) {
      bool func_progress = false;
      if (function->is_exported) {
         lower_call_abi_for_callee(function, wave_size);
         func_progress = true;
      }
      func_progress |= lower_call_abi_for_caller(impl);

      progress |= nir_progress(func_progress, impl, nir_metadata_none);
   }

   return progress;
}
