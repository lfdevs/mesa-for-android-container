/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_nir.h"

struct active_thread_lowering_state {
   nir_function_impl *impl;

   /** Tracks the number of barriers issued. */
   nir_variable *num_barriers_var;

   /** Tracks the number of active threads. */
   nir_variable *slm_atomic_vars;

   /** If there are multiple subgroups. */
   nir_def *is_multiple_subgroups;

   /** If the SLM writes in the prologue are synced. */
   bool is_prologue_synced;
};

static void
workgroup_barrier(nir_builder *b)
{
   nir_barrier(b,
               .execution_scope = SCOPE_WORKGROUP,
               .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL,
               .memory_modes = nir_var_mem_shared);
}

static bool
loop_has_divergent_halt(nir_cf_node *first_node)
{
   if (!exec_node_is_head_sentinel(first_node->node.prev) ||
       first_node->parent->type != nir_cf_node_loop)
      return false;

   nir_foreach_block_in_cf_node(loop_block, first_node->parent) {
      if (loop_block->divergent &&
          nir_block_ends_in_return_or_halt(loop_block)) {
         return true;
      }
   }

   return false;
}

static bool
lower_active_thread_barriers_counters(struct active_thread_lowering_state *state)
{
   /* We need to do some extra work to track divergence from halt
    * instructions because they are ignored by divergence analysis.
    */
   bool divergent_halt = false;

   nir_foreach_block_safe(block, state->impl) {
      if (!divergent_halt && loop_has_divergent_halt(&block->cf_node))
         divergent_halt = true;

      if (!block->divergent && !divergent_halt && state->is_prologue_synced)
         continue;

      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_barrier ||
             nir_intrinsic_execution_scope(intrin) != SCOPE_WORKGROUP)
            continue;

         if (!block->divergent && !divergent_halt) {
            state->is_prologue_synced = true;
            nir_intrinsic_set_memory_modes(intrin,
               nir_intrinsic_memory_modes(intrin) | nir_var_mem_shared);
            continue;
         }

         if (!state->num_barriers_var) {
            state->num_barriers_var =
               nir_local_variable_create(state->impl, glsl_uint_type(),
                                         "brw_num_barriers");
         }

         nir_builder b = nir_builder_at(nir_before_instr(instr));
         nir_def *invocation = nir_load_subgroup_invocation(&b);

         /* Only increment on a single lane per subgroup so the reduction at
          * the end of the program counts each barrier exactly once.
          */
         nir_push_if(&b, nir_ieq(&b, invocation, nir_first_invocation(&b)));
         {
            nir_def *value = nir_load_var(&b, state->num_barriers_var);
            value = nir_iadd_imm(&b, value, 1);
            nir_store_var(&b, state->num_barriers_var, value, 0x1);
         }
         nir_pop_if(&b, NULL);
      }

      if (!divergent_halt && block->divergent)
         divergent_halt = nir_block_ends_in_return_or_halt(block);
   }

   return state->num_barriers_var;
}

static void
insert_active_thread_barriers_prologue(struct active_thread_lowering_state *state)
{
   nir_shader *nir = state->impl->function->shader;
   unsigned wg_size = nir_static_workgroup_size(nir);
   nir_builder b = nir_builder_at(nir_before_impl(state->impl));

   state->slm_atomic_vars =
      nir_variable_create(nir, nir_var_mem_shared,
                          glsl_array_type(glsl_uint_type(), 2, 4),
                          "brw_barrier_sync");
   state->slm_atomic_vars->data.how_declared = nir_var_hidden;

   /* Set this up so DCE removes the prologue/epilogue if there's one subgroup. */
   state->is_multiple_subgroups =
      wg_size <= nir->info.max_subgroup_size ?
         nir_ilt_imm(&b, nir_load_simd_width_intel(&b), wg_size) :
         nir_imm_true(&b);

   nir_push_if(&b, state->is_multiple_subgroups);
   {
      nir_push_if(&b, nir_ieq_imm(&b, nir_load_local_invocation_index(&b), 0));
      {
         for (unsigned phase = 0; phase <= 1; ++phase) {
            nir_store_array_var_imm(&b, state->slm_atomic_vars, phase,
                                    nir_load_num_subgroups(&b), 0x1);
         }
      }
      nir_pop_if(&b, NULL);
   }
   nir_pop_if(&b, NULL);

   nir_store_var(&b, state->num_barriers_var, nir_imm_int(&b, 0), 0x1);
}

static void
insert_active_thread_barriers_epilogue(struct active_thread_lowering_state *state)
{
   /* Make sure nothing jumps past the last block */
   if (nir_lower_halt_to_return(state->impl->function->shader)) {
      nir_progress(true, state->impl, nir_metadata_none);
      nir_lower_returns_impl(state->impl);
   }

   nir_builder b = nir_builder_at(nir_after_impl(state->impl));

   nir_push_if(&b, state->is_multiple_subgroups);
   {
      nir_def *total_barriers =
         nir_reduce(&b, nir_load_var(&b, state->num_barriers_var),
                    .reduction_op = nir_op_iadd);

      /* Make sure the writes in the prologue have landed. */
      if (!state->is_prologue_synced) {
         nir_push_if(&b, nir_ieq_imm(&b, total_barriers, 0));
         {
            workgroup_barrier(&b);
         }
         nir_pop_if(&b, NULL);
      }

      nir_deref_instr *counter_vars[2];

      for (unsigned phase = 0; phase <= 1; ++phase) {
         /* Select the counters in SLM to sync with, we alternate between two
          * locations depending on if we're at an even or odd barrier to avoid
          * a WaR hazard while checking the result.
          */
         counter_vars[phase] =
            nir_build_deref_array(&b,
                                 nir_build_deref_var(&b, state->slm_atomic_vars),
                                 nir_ixor(&b,
                                          nir_iand_imm(&b, total_barriers, 1),
                                          nir_imm_int(&b, phase)));

         /* Decrement the SLM counter. */
         nir_push_if(&b, nir_ieq(&b, nir_load_subgroup_invocation(&b),
                                 nir_first_invocation(&b)));
         {
            nir_atomic_deref(&b, 32, counter_vars[phase], nir_imm_int(&b, -1),
                             nir_atomic_op_iadd);
         }
         nir_pop_if(&b, NULL);

         workgroup_barrier(&b);

         /* Halt if the counter has reached zero. */
         nir_def *cnt = nir_load_deref(&b, counter_vars[phase]);
         nir_halt_if(&b, nir_ieq_imm(&b, cnt, 0));
      }

      /* Keep issuing barriers until the counter reaches zero. */
      nir_push_loop(&b);
      {
         for (unsigned phase = 0; phase <= 1; ++phase) {
            workgroup_barrier(&b);

            nir_def *cnt = nir_load_deref(&b, counter_vars[phase]);
            nir_halt_if(&b, nir_ieq_imm(&b, cnt, 0));
         }
      }
      nir_pop_loop(&b, NULL);
   }
   nir_pop_if(&b, NULL);
}

/**
 * Emulates Xe2+ active thread barriers on Gfx125 and below. This is comparable
 * to IGC's DivergentBarrierPass, except its more compact and efficient.
 */
bool
brw_nir_lower_active_thread_barriers(nir_shader *nir,
                                     const struct intel_device_info *devinfo)
{
   assert(exec_list_length(&nir->functions) == 1);

   struct active_thread_lowering_state state = {
      .impl = nir_shader_get_entrypoint(nir),
   };

   if (devinfo->ver >= 20)
      return false;

   if (!nir->info.uses_control_barrier)
      return false;

   if (nir_static_workgroup_size(nir) <= nir->info.min_subgroup_size)
      return false;

   nir_divergence_analysis_impl(state.impl, nir_divergence_across_subgroups);

   if (!lower_active_thread_barriers_counters(&state))
      return false;

   insert_active_thread_barriers_prologue(&state);
   insert_active_thread_barriers_epilogue(&state);

   return nir_progress(true, state.impl, nir_metadata_none);
}

struct divergent_lowering_state {
   nir_function_impl *impl;

   /** The array of divergent barriers. Index = barrier IP */
   struct util_dynarray barriers;

   /** The number of LSBs of the signaling dword to store the barrier IP in. */
   unsigned num_barriers_log2;

   /** The pair of dwords in SLM to sync with. */
   nir_variable *slm_atomic_vars;

   /** If an extra barrier should be inserted at the end of the program. */
   bool requires_join;
};

/**
 * Finds barriers in divergent control flow and adds them to the array in their
 * source code order. This part is less than optimal because NIR currently does
 * not have support for checking post dominance to determine when the subgroups
 * become convergent again, but we'll save that for a permanent workaround.
 */
static bool
collect_divergent_barriers(struct divergent_lowering_state *state)
{
   bool divergent_cf = false;
   bool divergent_halt = false;
   nir_foreach_block(block, state->impl) {
      if (!divergent_halt && loop_has_divergent_halt(&block->cf_node)) {
         divergent_cf = true;
         divergent_halt = true;
      } else if (block->divergent) {
         divergent_cf = true;
      }

      nir_foreach_instr(instr, block) {
         if (!divergent_cf && !(state->requires_join && divergent_halt))
            continue;

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic == nir_intrinsic_barrier &&
             nir_intrinsic_execution_scope(intrin) == SCOPE_WORKGROUP) {
            util_dynarray_append(&state->barriers, intrin);
         }
      }

      if (!divergent_halt && block->divergent)
         divergent_halt = nir_block_ends_in_return_or_halt(block);
   }

   unsigned num_barriers =
      util_dynarray_num_elements(&state->barriers, nir_intrinsic_instr*);
   if (num_barriers <= (state->requires_join ? 0 : 1))
      return false;

   /* Allocate one extra IP for the barrier at the end of the program. */
   if (state->requires_join)
      ++num_barriers;

   state->num_barriers_log2 = util_logbase2_ceil(num_barriers);

   return true;
}

static void
emit_divergent_barrier(nir_builder *b,
                       struct divergent_lowering_state *state,
                       unsigned barrier_ip)
{
   nir_push_loop(b);
   {
      /* Get and decrement the subgroup's counter. */
      nir_def *counter_index = nir_subgroup_barrier_index_intel(b,
                                 .base = -1 << state->num_barriers_log2);

      /* Build the deref to the atomic in SLM to sync with. We alternate
       * between 2 different locations to avoid a WaR hazard while reading
       * the result after the barrier.
       */
      nir_def *atomic_index =
         nir_ubfe_imm(b, counter_index, state->num_barriers_log2, 1);
      nir_deref_instr *atomic_var =
         nir_build_deref_array(b, nir_build_deref_var(b, state->slm_atomic_vars),
                               atomic_index);

      /* Combine the MSBs of the counter with the barrier IP, this allows each
       * round of atomicMin to also automatically clear the previous value.
       */
      nir_def *atomic_val = nir_ior_imm(b, counter_index, barrier_ip);

      /* Calculate the minimum barrier IP of all active threads. */
      nir_push_if(b, nir_ieq(b, nir_load_subgroup_invocation(b),
                             nir_first_invocation(b)));
      {
         nir_atomic_deref(b, 32, atomic_var, atomic_val, nir_atomic_op_umin);
      }
      nir_pop_if(b, NULL);

      workgroup_barrier(b);

      /* Retrieve the combined minimum barrier IP of all active threads,
       * unblock only the threads that have a barrier IP equal to the minimum.
       * The rest of the threads will continue to retry the barrier until
       * the combined minimum barrier IP reaches their position.
       */
      nir_break_if(b, nir_ieq(b, atomic_val, nir_load_deref(b, atomic_var)));
   }
   nir_pop_loop(b, NULL);
}

static void
insert_divergent_barriers_prologue(struct divergent_lowering_state *state)
{
   state->slm_atomic_vars =
      nir_variable_create(state->impl->function->shader, nir_var_mem_shared,
                          glsl_array_type(glsl_uint_type(), 2, 4),
                          "brw_barrier_sync");
   state->slm_atomic_vars->data.how_declared = nir_var_hidden;

   nir_builder b = nir_builder_at(nir_before_impl(state->impl));

   /* Set up the subgroup counter's initial value */
   nir_subgroup_barrier_index_intel(&b,
      .base = -1 << state->num_barriers_log2);

   /* Initialize the SLM dwords with UINT32_MAX */
   nir_push_if(&b, nir_ieq_imm(&b, nir_load_local_invocation_index(&b), 0));
   {
      for (unsigned phase = 0; phase <= 1; ++phase) {
         nir_store_array_var_imm(&b, state->slm_atomic_vars, phase,
                                 nir_imm_int(&b, UINT32_MAX), 0x1);
      }
   }
   nir_pop_if(&b, NULL);

   workgroup_barrier(&b);
}

static void
lower_divergent_barriers_instrs(struct divergent_lowering_state *state)
{
   unsigned ip = 0;
   util_dynarray_foreach(&state->barriers, nir_intrinsic_instr*, intrin) {
      nir_builder b = nir_builder_at(nir_before_instr(&(*intrin)->instr));

      unsigned other_memory_modes =
         nir_intrinsic_memory_modes(*intrin) & ~nir_var_mem_shared;
      if (other_memory_modes) {
         /* Apply any of the non-SLM flushes separately */
         nir_barrier(&b,
                     .execution_scope = SCOPE_NONE,
                     .memory_scope = nir_intrinsic_memory_scope(*intrin),
                     .memory_semantics = nir_intrinsic_memory_semantics(*intrin),
                     .memory_modes = other_memory_modes);
      }

      emit_divergent_barrier(&b, state, ip++);

      nir_instr_remove(&(*intrin)->instr);
   }
}

static void
insert_divergent_barriers_epilogue(struct divergent_lowering_state *state)
{
   if (!state->requires_join)
      return;

   /* Make sure nothing jumps past the last block */
   if (nir_lower_halt_to_return(state->impl->function->shader)) {
      nir_progress(true, state->impl, nir_metadata_none);
      nir_lower_returns_impl(state->impl);
   }

   unsigned epilogue_barrier_ip =
      util_dynarray_num_elements(&state->barriers, nir_intrinsic_instr*);

   nir_builder b = nir_builder_at(nir_after_impl(state->impl));
   emit_divergent_barrier(&b, state, epilogue_barrier_ip);
}

/**
 * Temporary workaround for a 'broken' shader in RE Engine until we can ship a
 * XeSS shim in proton to avoid hiding the Intel vendor id from those titles.
 * This approach is not based on any spec and is entirely just the result of
 * intuition and some blind luck.
 */
bool
brw_nir_lower_divergent_barriers(nir_shader *nir,
                                 const struct intel_device_info *devinfo)
{
   assert(!intel_use_jay(devinfo, nir->info.stage));
   assert(exec_list_length(&nir->functions) == 1);

   struct divergent_lowering_state state = {
      .impl = nir_shader_get_entrypoint(nir),
      /* Before Xe2, barriers do not automatically exclude threads that have
       * already retired. We need an extra barrier at the end of the program
       * to keep things from hanging.
       */
      .requires_join = devinfo->ver < 20,
   };

   if (!nir->info.uses_control_barrier)
      return false;

   if (nir_static_workgroup_size(nir) <= nir->info.min_subgroup_size)
      return false;

   nir_divergence_analysis_impl(state.impl, nir_divergence_across_subgroups);

   if (!collect_divergent_barriers(&state))
      return false;

   insert_divergent_barriers_prologue(&state);

   lower_divergent_barriers_instrs(&state);

   insert_divergent_barriers_epilogue(&state);

   util_dynarray_fini(&state.barriers);

   return nir_progress(true, state.impl, nir_metadata_none);
}
