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
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return The replacement/lowered def.
 */
static nir_def *lower_barrier(nir_builder *b, nir_instr *instr, void *cb_data)
{
   struct shader_info *info = &b->shader->info;
   bool *uses_usclib = cb_data;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   mesa_scope exec_scope = nir_intrinsic_execution_scope(intr);

   unsigned wg_size = info->workgroup_size[0] * info->workgroup_size[1] *
                      info->workgroup_size[2];

   if (wg_size <= ROGUE_MAX_INSTANCES_PER_TASK || exec_scope == SCOPE_NONE ||
       exec_scope == SCOPE_SUBGROUP)
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   /* TODO: We might be able to re-use barrier counters. */
   unsigned counter_offset = info->shared_size;
   info->shared_size += sizeof(uint32_t);
   info->zero_initialize_shared_memory = true;

   *uses_usclib = true;

   unsigned num_slots = DIV_ROUND_UP(wg_size, ROGUE_MAX_INSTANCES_PER_TASK);

   b->cursor = nir_before_instr(instr);
   usclib_barrier(b, nir_imm_int(b, num_slots), nir_imm_int(b, counter_offset));

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

/**
 * \brief Filters barrier instructions.
 *
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return True if the instruction matches the filter.
 */
static bool is_barrier(const nir_instr *instr, UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   return nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_barrier;
}

/**
 * \brief Barrier lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_barriers(nir_shader *shader, pco_data *data)
{
   bool progress = nir_shader_lower_instructions(shader,
                                                 is_barrier,
                                                 lower_barrier,
                                                 &data->common.uses.usclib);

   data->common.uses.barriers |= progress;

   return progress;
}

static nir_def *lower_atomic(nir_builder *b, nir_instr *instr, void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   bool *uses_usclib = cb_data;

   b->cursor = nir_before_instr(instr);

   if (intr->intrinsic == nir_intrinsic_ssbo_atomic_swap) {
      nir_def *buffer = intr->src[0].ssa;
      nir_def *offset = intr->src[1].ssa;
      nir_def *value = intr->src[2].ssa;
      nir_def *value_swap = intr->src[3].ssa;

      ASSERTED enum gl_access_qualifier access = nir_intrinsic_access(intr);
      ASSERTED unsigned num_components = intr->def.num_components;
      ASSERTED unsigned bit_size = intr->def.bit_size;
      assert(access == ACCESS_COHERENT);
      assert(num_components == 1 && bit_size == 32);

      *uses_usclib = true;
      return usclib_emu_ssbo_atomic_comp_swap(b,
                                              buffer,
                                              offset,
                                              value,
                                              value_swap);
   }

   nir_def *addr_data = intr->src[0].ssa;
   nir_def *addr_lo = nir_channel(b, addr_data, 0);
   nir_def *addr_hi = nir_channel(b, addr_data, 1);
   nir_def *value = nir_channel(b, addr_data, 2);
   nir_def *value_swap = nir_channel(b, addr_data, 3);

   ASSERTED unsigned num_components = intr->def.num_components;
   ASSERTED unsigned bit_size = intr->def.bit_size;
   assert(num_components == 1 && bit_size == 32);

   *uses_usclib = true;
   return usclib_emu_global_atomic_comp_swap(b,
                                             addr_lo,
                                             addr_hi,
                                             value,
                                             value_swap);
}

/**
 * \brief Filters lowerable atomic instructions.
 *
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return True if the instruction matches the filter.
 */
static bool is_lowerable_atomic(const nir_instr *instr,
                                UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   return intr->intrinsic == nir_intrinsic_ssbo_atomic_swap ||
          intr->intrinsic == nir_intrinsic_global_atomic_swap_pco;
}

/**
 * \brief Atomics lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_atomics(nir_shader *shader, pco_data *data)
{
   return nir_shader_lower_instructions(shader,
                                        is_lowerable_atomic,
                                        lower_atomic,
                                        &data->common.uses.usclib);
}

static nir_def *
lower_subgroup_intrinsic(nir_builder *b, nir_instr *instr, void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   assert(intr->def.num_components == 1);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_subgroup_size:
      return nir_imm_int(b, 1);

   case nir_intrinsic_load_subgroup_invocation:
      return nir_imm_int(b, 0);

   case nir_intrinsic_load_num_subgroups:
      return nir_imm_int(b,
                         b->shader->info.workgroup_size[0] *
                            b->shader->info.workgroup_size[1] *
                            b->shader->info.workgroup_size[2]);

   case nir_intrinsic_load_subgroup_id:
      return nir_load_local_invocation_index(b);

   case nir_intrinsic_first_invocation:
      return nir_imm_int(b, 0);

   case nir_intrinsic_elect:
      return nir_imm_true(b);

   default:
      break;
   }

   UNREACHABLE("");
}

static bool is_subgroup_intrinsic(const nir_instr *instr,
                                  UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_subgroup_size:
   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_num_subgroups:
   case nir_intrinsic_load_subgroup_id:
   case nir_intrinsic_first_invocation:
   case nir_intrinsic_elect:
      return true;

   default:
      break;
   }

   return false;
}

bool pco_nir_lower_subgroups(nir_shader *shader)
{
   shader->info.api_subgroup_size = 1;
   shader->info.min_subgroup_size = 1;
   shader->info.max_subgroup_size = 1;

   return nir_shader_lower_instructions(shader,
                                        is_subgroup_intrinsic,
                                        lower_subgroup_intrinsic,
                                        NULL);
}
