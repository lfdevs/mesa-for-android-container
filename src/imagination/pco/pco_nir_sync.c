/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_sync.c
 *
 * \brief PCO NIR sync-related passes.
 */

#include "hwdef/rogue_hw_defs.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "pco_usclib.h"
#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * \brief Lowers a barrier instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in] cb_data User callback data.
 * \return True if progress was made.
 */
static bool
lower_barrier(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_barrier)
      return false;

   struct shader_info *info = &b->shader->info;
   bool *uses_usclib = cb_data;

   mesa_scope exec_scope = nir_intrinsic_execution_scope(intr);

   unsigned wg_size = info->workgroup_size[0] * info->workgroup_size[1] *
                      info->workgroup_size[2];

   if (wg_size <= ROGUE_MAX_INSTANCES_PER_TASK || exec_scope == SCOPE_NONE ||
       exec_scope == SCOPE_SUBGROUP) {
      nir_instr_remove(&intr->instr);
      return true;
   }

   /* TODO: We might be able to re-use barrier counters. */
   unsigned counter_offset = info->shared_size;
   info->shared_size += sizeof(uint32_t);
   info->zero_initialize_shared_memory = true;

   *uses_usclib = true;

   unsigned num_slots = DIV_ROUND_UP(wg_size, ROGUE_MAX_INSTANCES_PER_TASK);

   b->cursor = nir_before_instr(&intr->instr);
   usclib_barrier(b, nir_imm_int(b, num_slots), nir_imm_int(b, counter_offset));
   nir_instr_remove(&intr->instr);
   return true;
}

/**
 * \brief Barrier lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_barriers(nir_shader *shader, pco_data *data)
{
   bool progress = nir_shader_intrinsics_pass(shader,
                                              lower_barrier,
                                              nir_metadata_none,
                                              &data->common.uses.usclib);

   data->common.uses.barriers |= progress;

   return progress;
}

static bool
lower_usclib_atomic(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_ssbo_atomic_swap &&
       intr->intrinsic != nir_intrinsic_global_atomic_swap_pco)
      return false;

   bool *uses_usclib = cb_data;

   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_ssbo_atomic_swap) {
      nir_def *buffer = intr->src[0].ssa;
      nir_def *offset = intr->src[1].ssa;
      nir_def *value = intr->src[2].ssa;
      nir_def *value_swap = intr->src[3].ssa;

      ASSERTED enum gl_access_qualifier access = nir_intrinsic_access(intr);
      ASSERTED unsigned num_components = intr->def.num_components;
      ASSERTED unsigned bit_size = intr->def.bit_size;
      assert(access & ACCESS_COHERENT);
      assert(num_components == 1 && bit_size == 32);

      *uses_usclib = true;
      nir_def *emulated =
         usclib_emu_ssbo_atomic_comp_swap(b, buffer, offset, value, value_swap);
      nir_def_rewrite_uses(&intr->def, emulated);
      nir_instr_remove(&intr->instr);
      return true;
   }

   nir_def *addr_data = intr->src[0].ssa;
   nir_def *addr = nir_channels(b, addr_data, BITFIELD_RANGE(0, 2));
   nir_def *value = nir_channel(b, addr_data, 2);
   nir_def *value_swap = nir_channel(b, addr_data, 3);

   ASSERTED unsigned num_components = intr->def.num_components;
   ASSERTED unsigned bit_size = intr->def.bit_size;
   assert(num_components == 1 && bit_size == 32);

   *uses_usclib = true;

   nir_def *emulated =
      usclib_emu_global_atomic_comp_swap(b, addr, value, value_swap);
   nir_def_rewrite_uses(&intr->def, emulated);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool lower_global_atomic_intrinsic(nir_builder *b,
                                          nir_intrinsic_instr *intrin,
                                          UNUSED void *data)
{
   nir_intrinsic_op op;
   bool is_swap = false;
   switch (intrin->intrinsic) {
   case nir_intrinsic_global_atomic_2x32:
      op = nir_intrinsic_global_atomic_pco;
      break;
   case nir_intrinsic_global_atomic_swap_2x32:
      op = nir_intrinsic_global_atomic_swap_pco;
      is_swap = true;
      break;
   default:
      return false;
   }

   nir_src *addr_src = &intrin->src[0];
   nir_src *data_src = &intrin->src[1];
   nir_src *data2_src;
   if (is_swap)
      data2_src = &intrin->src[2];

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *new_src = nir_pad_vector(b, addr_src->ssa, is_swap ? 4 : 3);
   new_src = nir_vector_insert_imm(b, new_src, data_src->ssa, 2);
   if (is_swap)
      new_src = nir_vector_insert_imm(b, new_src, data2_src->ssa, 3);

   nir_intrinsic_instr *new_intrin = nir_intrinsic_instr_create(b->shader, op);
   new_intrin->num_components = intrin->num_components;
   nir_def_init(&new_intrin->instr,
                &new_intrin->def,
                intrin->def.num_components,
                intrin->def.bit_size);
   new_intrin->src[0] = nir_src_for_ssa(new_src);
   assert(nir_intrinsic_has_atomic_op(intrin));
   nir_intrinsic_set_atomic_op(new_intrin, nir_intrinsic_atomic_op(intrin));

   nir_builder_instr_insert(b, &new_intrin->instr);
   nir_def_rewrite_uses(&intrin->def, &new_intrin->def);
   nir_instr_remove(&intrin->instr);

   return true;
}

/**
 * \brief Atomics lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_atomics(nir_shader *shader, pco_data *data)
{
   bool progress = false;

   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_global_atomic_intrinsic,
                                          nir_metadata_control_flow,
                                          NULL);
   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_usclib_atomic,
                                          nir_metadata_none,
                                          &data->common.uses.usclib);

   return progress;
}

struct subgroup_state {
   nir_variable *per_subgroup_var;
   nir_variable *per_invoc_var;
};

static nir_variable *
subgroup_var(nir_shader *shader, struct subgroup_state *state, bool per_invoc)
{
   struct shader_info *info = &shader->info;

   unsigned num_invocations =
      info->workgroup_size[0] *
      info->workgroup_size[1] *
      info->workgroup_size[2];

   unsigned num_subgroups =
      DIV_ROUND_UP(num_invocations, ROGUE_MAX_INSTANCES_PER_TASK);

   if (!per_invoc) {
      /* Allocate a 32-bit var for each subgroup. */
      if (!state->per_subgroup_var) {
         const glsl_type *var_type =
            glsl_array_type(glsl_uint_type(), num_subgroups, 0);

         state->per_subgroup_var = nir_variable_create(shader,
                                                       nir_var_mem_shared,
                                                       var_type,
                                                       "per_subgroup_var");
      }

      return state->per_subgroup_var;
   }

   /* Allocate a 32-bit var for each subgroup invocation
    * aligned to the subgroup size.
    */
   if (!state->per_invoc_var) {
      unsigned num_invocations_aligned =
         ALIGN_POT(num_invocations, ROGUE_MAX_INSTANCES_PER_TASK);

      const glsl_type *var_type =
         glsl_array_type(glsl_uint_type(), num_invocations_aligned, 0);

      state->per_invoc_var = nir_variable_create(shader,
                                                 nir_var_mem_shared,
                                                 var_type,
                                                 "per_invoc_var");
   }

   return state->per_invoc_var;
}

static nir_def *
lower_read_invocation(nir_builder *b,
                      nir_intrinsic_instr *intr,
                      struct subgroup_state *state)
{
   nir_def *subgroup_id =
      nir_udiv_imm(b,
                   nir_load_local_invocation_index(b),
                   ROGUE_MAX_INSTANCES_PER_TASK);

   nir_def *value = intr->src[0].ssa;
   nir_def *invoc = intr->src[1].ssa;

   nir_variable *var = subgroup_var(b->shader, state, false);
   nir_deref_instr *deref = nir_build_deref_var(b, var);
   deref = nir_build_deref_array(b, deref, subgroup_id);

   nir_def *invocation_id = nir_load_instance_num_pco(b);

   nir_if *nif = nir_push_if(b, nir_ieq(b, invoc, invocation_id));
   {
      nir_store_deref(b, deref, value, 1);
   }
   nir_pop_if(b, nif);

   /* Retrieve the value. */
   return nir_load_deref(b, deref);
}

static inline nir_def *elect(nir_builder *b)
{
   return nir_ieq(b, nir_load_instance_num_pco(b), nir_first_invocation(b));
}

static nir_def *
lower_ballot(nir_builder *b,
             nir_intrinsic_instr *intr,
             struct subgroup_state *state)
{
   nir_def *subgroup_id =
      nir_udiv_imm(b,
                   nir_load_local_invocation_index(b),
                   ROGUE_MAX_INSTANCES_PER_TASK);

   nir_variable *var = subgroup_var(b->shader, state, false);
   nir_deref_instr *deref = nir_build_deref_var(b, var);
   deref = nir_build_deref_array(b, deref, subgroup_id);

   /* Initialize the ballot shared var to zero. */
   nir_if *nif = nir_push_if(b, elect(b));
   {
      nir_store_deref(b, deref, nir_imm_int(b, 0), 1);
   }
   nir_pop_if(b, nif);

   /* Each mask = value << invocation_num */
   nir_def *ballot_mask =
      nir_ishl(b, nir_b2i32(b, intr->src[0].ssa), nir_load_instance_num_pco(b));

   /* OR all the masks together. */
   nir_deref_atomic(b,
                    intr->def.bit_size,
                    &deref->def,
                    ballot_mask,
                    .atomic_op = nir_atomic_op_ior);

   /* Retrieve the value. */
   return nir_load_deref(b, deref);
}

static inline nir_def *
reduction_op_init_val(nir_op reduction_op, nir_builder *b, unsigned bit_size)
{
   switch (reduction_op) {
   case nir_op_iadd:
   case nir_op_umax:
      return nir_imm_intN_t(b, 0, bit_size);

   case nir_op_imul:
      return nir_imm_intN_t(b, 1, bit_size);

   case nir_op_imin:
      return nir_imm_intN_t(b, u_intN_max(bit_size), bit_size);

   case nir_op_imax:
      return nir_imm_intN_t(b, u_intN_min(bit_size), bit_size);

   case nir_op_umin:
      return nir_imm_intN_t(b, u_uintN_max(bit_size), bit_size);

   case nir_op_iand:
      return nir_imm_boolN_t(b, true, bit_size);

   case nir_op_ior:
   case nir_op_ixor:
      return nir_imm_boolN_t(b, false, bit_size);

   case nir_op_fadd:
      return nir_imm_floatN_t(b, 0.0f, bit_size);

   case nir_op_fmul:
      return nir_imm_floatN_t(b, 1.0f, bit_size);

   case nir_op_fmin:
      return nir_imm_floatN_t(b, INFINITY, bit_size);

   case nir_op_fmax:
      return nir_imm_floatN_t(b, -INFINITY, bit_size);

   default:
      break;
   }

   UNREACHABLE("Unknown reduction op.");
}

static inline nir_def *
do_reduction_op(nir_builder *b, nir_op op, nir_def *accum, nir_def *value)
{
   switch (op) {
   case nir_op_iadd:
      return nir_iadd(b, accum, value);

   case nir_op_imul:
      return nir_imul(b, accum, value);

   case nir_op_umin:
      return nir_umin(b, accum, value);

   case nir_op_imin:
      return nir_imin(b, accum, value);

   case nir_op_umax:
      return nir_umax(b, accum, value);

   case nir_op_imax:
      return nir_imax(b, accum, value);

   case nir_op_iand:
      return nir_iand(b, accum, value);

   case nir_op_ior:
      return nir_ior(b, accum, value);

   case nir_op_ixor:
      return nir_ixor(b, accum, value);

   case nir_op_fadd:
      return nir_fadd(b, accum, value);

   case nir_op_fmul:
      return nir_fmul(b, accum, value);

   case nir_op_fmin:
      return nir_fmin(b, accum, value);

   case nir_op_fmax:
      return nir_fmax(b, accum, value);

   default:
      break;
   }

   UNREACHABLE("Unknown reduction op.");
}

static nir_def *
lower_reduction(nir_builder *b,
                nir_intrinsic_instr *intr,
                struct subgroup_state *state)
{
   nir_def *value = intr->src[0].ssa;

   unsigned bit_size = value->bit_size;
   assert(bit_size == 32);

   nir_op reduction_op = nir_intrinsic_reduction_op(intr);

   nir_def *subgroup_id =
      nir_udiv_imm(b,
                   nir_load_local_invocation_index(b),
                   ROGUE_MAX_INSTANCES_PER_TASK);

   nir_def *invocation_id = nir_load_instance_num_pco(b);

   unsigned cluster_size = intr->intrinsic == nir_intrinsic_reduce ?
      nir_intrinsic_cluster_size(intr) :
      0;

   if (!cluster_size)
      cluster_size = ROGUE_MAX_INSTANCES_PER_TASK;

   nir_def *cluster_num = nir_udiv_imm(b, invocation_id, cluster_size);
   nir_def *cluster_id = nir_umod_imm(b, invocation_id, cluster_size);

   nir_variable *var = subgroup_var(b->shader, state, true);
   nir_deref_instr *deref = nir_build_deref_var(b, var);

   nir_def *reduction_base =
      nir_imul_imm(b, subgroup_id, ROGUE_MAX_INSTANCES_PER_TASK);

   nir_def *init_val = reduction_op_init_val(reduction_op, b, bit_size);

   nir_variable *i_var =
      nir_local_variable_create(b->impl, glsl_uint_type(), "reduction_iter");

   /* Initialise the reduction vars to the initial value in each subgroup. */
   nir_if *nif = nir_push_if(b, elect(b));
   {
      nir_store_var(b, i_var, nir_imm_int(b, 0), 1);

      nir_loop *loop = nir_push_loop(b);
      {
         nir_def *i = nir_load_var(b, i_var);
         nir_break_if(b, nir_uge_imm(b, i, ROGUE_MAX_INSTANCES_PER_TASK));

         nir_def *invoc_index = nir_iadd(b, reduction_base, i);

         nir_deref_instr *deref_invoc =
            nir_build_deref_array(b, deref, invoc_index);

         nir_store_deref(b, deref_invoc, init_val, 1);

         nir_store_var(b, i_var, nir_iadd_imm(b, i, 1), 1);
      }
      nir_pop_loop(b, loop);
   }
   nir_pop_if(b, nif);

   /* Have each running invocation store its value. */
   nir_deref_instr *deref_invoc =
      nir_build_deref_array(b,
                            deref,
                            nir_iadd(b, reduction_base, invocation_id));

   nir_store_deref(b, deref_invoc, value, 1);

   nir_def *cluster_base = nir_iadd(b,
                                    reduction_base,
                                    nir_imul_imm(b, cluster_num, cluster_size));

   /* Reset iterator. */
   nir_store_var(b, i_var, nir_imm_int(b, 0), 1);

   /* Create accumulator. */
   nir_variable *accum_var =
      nir_local_variable_create(b->impl, glsl_uint_type(), "reduction_accum");
   nir_store_var(b, accum_var, init_val, 1);

   /* Perform reduction op and accumulate. */
   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *i = nir_load_var(b, i_var);

      nir_def *break_cond;

      switch (intr->intrinsic) {
      case nir_intrinsic_reduce:
         break_cond = nir_uge_imm(b, i, cluster_size);
         break;

      case nir_intrinsic_inclusive_scan:
         break_cond = nir_uge(b, i, nir_iadd_imm(b, cluster_id, 1));
         break;

      case nir_intrinsic_exclusive_scan:
         break_cond = nir_uge(b, i, cluster_id);
         break;

      default:
         UNREACHABLE("Unexpected intrinsic.");
      }

      nir_break_if(b, break_cond);

      nir_def *accum = nir_load_var(b, accum_var);

      nir_def *invoc_index = nir_iadd(b, cluster_base, i);

      nir_deref_instr *deref_invoc =
         nir_build_deref_array(b, deref, invoc_index);

      nir_def *iter_value = nir_load_deref(b, deref_invoc);

      accum = do_reduction_op(b, reduction_op, accum, iter_value);

      nir_store_var(b, accum_var, accum, 1);

      nir_store_var(b, i_var, nir_iadd_imm(b, i, 1), 1);
   }
   nir_pop_loop(b, loop);

   return nir_load_var(b, accum_var);
}

static nir_def *
lower_shuffle(nir_builder *b,
              nir_intrinsic_instr *intr,
              struct subgroup_state *state)
{
   nir_def *value = intr->src[0].ssa;
   nir_def *index = intr->src[1].ssa;

   unsigned bit_size = value->bit_size;
   assert(bit_size == 32);

   nir_def *subgroup_id =
      nir_udiv_imm(b,
                   nir_load_local_invocation_index(b),
                   ROGUE_MAX_INSTANCES_PER_TASK);

   nir_def *invocation_id = nir_load_instance_num_pco(b);

   nir_variable *var = subgroup_var(b->shader, state, true);
   nir_deref_instr *deref = nir_build_deref_var(b, var);

   nir_def *subgroup_base =
      nir_imul_imm(b, subgroup_id, ROGUE_MAX_INSTANCES_PER_TASK);

   /* Have each running invocation store its value. */
   nir_deref_instr *deref_invoc =
      nir_build_deref_array(b,
                            deref,
                            nir_iadd(b, subgroup_base, invocation_id));

   nir_store_deref(b, deref_invoc, value, 1);

   /* Load the value from the specified invocation. */
   nir_def *invoc_index = nir_iadd(b, subgroup_base, index);

   deref_invoc = nir_build_deref_array(b, deref, invoc_index);

   nir_def *iter_value = nir_load_deref(b, deref_invoc);

   return iter_value;
}

static bool lower_subgroup_intrinsic(nir_builder *b,
                                     nir_intrinsic_instr *intr,
                                     void *cb_data)
{
   const struct shader_info *info = &b->shader->info;
   struct subgroup_state *state = cb_data;
   nir_def *new_def;

   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_subgroup_size:
      new_def = nir_imm_int(b, info->api_subgroup_size);
      break;

   case nir_intrinsic_load_num_subgroups:
      new_def = nir_imm_int(b,
                            DIV_ROUND_UP(info->workgroup_size[0] *
                                            info->workgroup_size[1] *
                                            info->workgroup_size[2],
                                         ROGUE_MAX_INSTANCES_PER_TASK));
      break;

   case nir_intrinsic_load_subgroup_invocation:
      new_def = nir_load_instance_num_pco(b);
      break;

   case nir_intrinsic_load_subgroup_id:
      new_def = nir_udiv_imm(b,
                             nir_load_local_invocation_index(b),
                             ROGUE_MAX_INSTANCES_PER_TASK);
      break;

   case nir_intrinsic_read_invocation:
      new_def = lower_read_invocation(b, intr, state);
      break;

   case nir_intrinsic_ballot:
   case nir_intrinsic_ballot_relaxed:
      new_def = lower_ballot(b, intr, state);
      break;

   case nir_intrinsic_reduce:
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan:
      new_def = lower_reduction(b, intr, state);
      break;

   case nir_intrinsic_shuffle:
      new_def = lower_shuffle(b, intr, state);
      break;

   default:
      return false;
   }

   nir_def_rewrite_uses(&intr->def, new_def);
   nir_instr_remove(&intr->instr);
   assert(intr->def.num_components == 1);
   return true;
}

bool pco_nir_lower_subgroups(nir_shader *shader)
{
   if (shader->info.internal)
      return false;

   assert(!shader->info.api_subgroup_size ||
          shader->info.api_subgroup_size == ROGUE_MAX_INSTANCES_PER_TASK);

   struct subgroup_state state = { 0 };
   return nir_shader_intrinsics_pass(shader,
                                     lower_subgroup_intrinsic,
                                     nir_metadata_none,
                                     &state);
}
