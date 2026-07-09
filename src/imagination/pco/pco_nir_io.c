/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_io.c
 *
 * \brief PCO NIR I/O lowering pass.
 */

#include "nir.h"
#include "nir_builder.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * \brief Lowers an I/O instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in] cb_data User callback data.
 * \return True if progress was made.
 */
static bool
lower_io(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *cb_data)
{
   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_push_constant:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_store_shared:
      break;

   default:
      return false;
   }

   nir_src *offset_src = nir_get_io_offset_src(intr);

   ASSERTED unsigned base = nir_intrinsic_base(intr);
   assert(!base);

   /* Byte offset to DWORD offset. */
   nir_src_rewrite(offset_src, nir_ushr_imm(b, offset_src->ssa, 2));

   return true;
}

/**
 * \brief I/O lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_io(nir_shader *shader)
{
   bool progress = false;

   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_io,
                                          nir_metadata_control_flow,
                                          NULL);

   return progress;
}

static bool lower_shared_io_to_global(nir_builder *b,
                                      nir_intrinsic_instr *intr,
                                      void *cb_data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      break;

   default:
      return false;
   }

   struct shader_info *info = &b->shader->info;
   unsigned local_size = info->workgroup_size[0] *
                         info->workgroup_size[1] *
                         info->workgroup_size[2];

   unsigned usc_slots = *((unsigned *)cb_data);
   assert(usc_slots);

   b->cursor = nir_before_instr(&intr->instr);

   /* If each wg fits inside a slot, then we run one wg per _slot_, so:
    *
    * wg_idx = (cluster_num * slots_per_cluster) + slot_num
    *
    * otherwise if > 1 slot is required, we run one wg per _cluster_:
    *
    * wg_idx = cluster_num
    */
   nir_def *wg_idx = nir_load_cluster_num_pco(b);
   if (local_size <= ROGUE_MAX_INSTANCES_PER_TASK) {
      wg_idx = nir_imul_imm(b, wg_idx, usc_slots);
      wg_idx = nir_iadd(b, wg_idx, nir_load_slot_num_pco(b));
   }

   nir_src *offset_src = nir_get_io_offset_src(intr);
   nir_def *shmem_buf_offset = nir_imul_imm(b, wg_idx, info->shared_size);
   nir_def *offset = nir_iadd(b, offset_src->ssa, shmem_buf_offset);

   nir_def *shared_base_ptr = nir_load_shared_base_ptr(b, 2, 32);
   nir_def *shared_base_ptr_lo = nir_channel(b, shared_base_ptr, 0);
   nir_def *shared_base_ptr_hi = nir_channel(b, shared_base_ptr, 1);
   nir_def *addr =
      nir_uadd64_32(b, shared_base_ptr_lo, shared_base_ptr_hi, offset);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_shared: {
      nir_def *repl =
         nir_load_global_2x32(b,
                              intr->def.num_components,
                              intr->def.bit_size,
                              addr,
                              .align_mul = nir_intrinsic_align_mul(intr),
                              .align_offset = nir_intrinsic_align_offset(intr));

      nir_def_replace(&intr->def, repl);
      nir_instr_free(&intr->instr);
      break;
   }

   case nir_intrinsic_store_shared: {
      nir_def *value = intr->src[0].ssa;
      nir_store_global_2x32(b,
                            value,
                            addr,
                            .write_mask = nir_intrinsic_write_mask(intr),
                            .align_mul = nir_intrinsic_align_mul(intr),
                            .align_offset = nir_intrinsic_align_offset(intr));

      nir_instr_remove(&intr->instr);
      break;
   }

   case nir_intrinsic_shared_atomic: {
      nir_def *data = intr->src[1].ssa;
      nir_def *repl =
         nir_global_atomic_2x32(b,
                              intr->def.bit_size,
                              addr,
                              data,
                              .atomic_op = nir_intrinsic_atomic_op(intr));

      nir_def_replace(&intr->def, repl);
      nir_instr_free(&intr->instr);
      break;
   }

   case nir_intrinsic_shared_atomic_swap: {
      nir_def *data = intr->src[1].ssa;
      nir_def *data2 = intr->src[2].ssa;
      nir_def *repl =
         nir_global_atomic_swap_2x32(b,
                              intr->def.bit_size,
                              addr,
                              data,
                              data2,
                              .atomic_op = nir_intrinsic_atomic_op(intr));

      nir_def_replace(&intr->def, repl);
      nir_instr_free(&intr->instr);
      break;
   }

   default:
      UNREACHABLE("Unexpected intrinsic.");
   }

   return true;
}

bool pco_nir_lower_shared_io_to_global(nir_shader *shader, unsigned usc_slots)
{
   return nir_shader_intrinsics_pass(shader,
                                     lower_shared_io_to_global,
                                     nir_metadata_control_flow,
                                     &usc_slots);
}

/**
 * \brief Propagages coherent/volatile access qualifiers on deref intrinsics to
 *        the intrinsics respective nir_variable.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in] cb_data User callback data.
 * \return True if progress was made.
 */
static bool prop_intr_access_to_vars(nir_builder *b,
                                     nir_intrinsic_instr *intr,
                                     void *cb_data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_deref_atomic_swap:
      break;

   default:
      return false;
   }

   assert(nir_intrinsic_has_access(intr));

   struct util_dynarray *intrs = cb_data;

   nir_variable *var =
      nir_get_binding_variable(b->shader, nir_chase_binding(intr->src[0]));

   enum gl_access_qualifier intr_access = nir_intrinsic_access(intr);

   util_dynarray_append(intrs, intr);

   bool progress = false;

   if (intr_access & ACCESS_COHERENT) {
      var->data.access |= ACCESS_COHERENT;
      progress = true;
   }

   if (intr_access & ACCESS_VOLATILE) {
      var->data.access |= ACCESS_VOLATILE;
      progress = true;
   }

   return progress;
}

/**
 * \brief Access qualifier propagating pass.
 *
 * \param[in,out] shader NIR shader.
 * \param[in,out] data Shader data.
 * \return True if the pass made progress.
 */
bool pco_nir_prop_access(nir_shader *shader)
{
   struct util_dynarray intrs = UTIL_DYNARRAY_INIT;

   bool progress = false;

   progress |= nir_shader_intrinsics_pass(shader,
                                          prop_intr_access_to_vars,
                                          nir_metadata_none,
                                          &intrs);

   /* Propagate coherent access qualifier from nir_variable's to their
    * respective deref intrinsics. Avoid propagating any other access
    * qualifiers that could have other effects.
    */
   util_dynarray_foreach (&intrs, nir_intrinsic_instr *, pintr) {
      nir_intrinsic_instr *intr = *pintr;

      nir_variable *var =
         nir_get_binding_variable(shader, nir_chase_binding(intr->src[0]));

      nir_intrinsic_set_access(intr,
                               nir_intrinsic_access(intr) | var->data.access);
      progress = true;
   }

   util_dynarray_fini(&intrs);

   return progress;
}
