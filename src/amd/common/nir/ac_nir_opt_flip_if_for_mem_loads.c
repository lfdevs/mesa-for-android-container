/*
 * Copyright 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This pass flips divergent branches if the then-side contains a memory load,
 * and the else-side does not. This is useful because VMEM/LDS->VALU WaW on
 * GFX11+ requires a waitcnt, even if the two writes have no lanes in common.
 * By flipping the branch, it becomes a VALU->VMEM/LDS WaW, which requires no
 * waitcnt.
 *
 * A typical case is a VMEM load and a constant:
 *    if (divergent_condition) {
 *       a = tex()
 *    } else {
 *       a = 0.0;
 *    }
 * which becomes:
 *    if (!divergent_condition) {
 *       a = 0.0;
 *    } else {
 *       a = tex()
 *    }
 *
 * Note that it's best to run this before nir_opt_algebraic, to optimize out
 * the inot, and after nir_opt_if, because opt_if_simplification can undo this
 * optimization.
 */

#include "ac_nir.h"
#include "nir_builder.h"

enum {
   is_vmem_lds = 1 << 0,
   is_other = 1 << 1,
};

static unsigned
is_vmem_or_lds_load(nir_def *def, unsigned depth, unsigned begin, unsigned end)
{
   /* Undef phi sources do not create any instructions. */
   if (nir_def_is_undef(def))
      return 0;

   /* This likely requires a copy. */
   if (depth == 0 && !list_is_singular(&def->uses))
      return is_other;

   /* ACO always combines constants into phi operands, so return is_other even if it's
    * outside begin/end.
    */
   if (depth == 0 && nir_def_is_const(def))
      return is_other;

   if (nir_def_instr(def)->block->index < begin ||
       nir_def_instr(def)->block->index > end ||
       depth > 4)
      return 0;

   switch (nir_def_instr(def)->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_def_as_alu(def);
      /* ACO has an optimization to combine u2u32 into a load instruction, so treat it like a mov. */
      if (!nir_op_is_vec_or_mov(alu->op) &&
          !(alu->op == nir_op_u2u32 && alu->src[0].src.ssa->bit_size < 32))
         return is_other;

      unsigned res = 0;
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
         res |= is_vmem_or_lds_load(alu->src[i].src.ssa, depth + 1, begin, end);
      return res;
   }
   case nir_instr_type_phi: {
      unsigned res = 0;
      nir_foreach_phi_src (src, nir_def_as_phi(def))
         res |= is_vmem_or_lds_load(src->src.ssa, depth + 1, begin, end);
      return res;
   }
   case nir_instr_type_tex:
      return is_vmem_lds;
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_def_as_intrinsic(def);
      if (nir_intrinsic_has_access(intrin) && (nir_intrinsic_access(intrin) & ACCESS_SMEM_AMD))
         return is_other;

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_load_global:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_load_global_amd:
      case nir_intrinsic_load_scratch:
      case nir_intrinsic_load_shared:
      case nir_intrinsic_load_shared2_amd:
      case nir_intrinsic_shared_append_amd:
      case nir_intrinsic_shared_consume_amd:
      case nir_intrinsic_load_constant:
      case nir_intrinsic_bindless_image_load:
      case nir_intrinsic_bindless_image_sparse_load:
      case nir_intrinsic_bindless_image_fragment_mask_load_amd:
      case nir_intrinsic_image_heap_load:
      case nir_intrinsic_image_heap_sparse_load:
      case nir_intrinsic_image_heap_fragment_mask_load_amd:
      case nir_intrinsic_load_buffer_amd:
      case nir_intrinsic_load_typed_buffer_amd:
      case nir_intrinsic_ssbo_atomic:
      case nir_intrinsic_ssbo_atomic_swap:
      case nir_intrinsic_global_atomic:
      case nir_intrinsic_global_atomic_swap:
      case nir_intrinsic_global_atomic_amd:
      case nir_intrinsic_global_atomic_swap_amd:
      case nir_intrinsic_shared_atomic:
      case nir_intrinsic_shared_atomic_swap:
      case nir_intrinsic_bindless_image_atomic:
      case nir_intrinsic_bindless_image_atomic_swap:
      case nir_intrinsic_image_heap_atomic:
      case nir_intrinsic_image_heap_atomic_swap:
         return is_vmem_lds;
      default:
         return is_other;
      }
   }
   case nir_instr_type_undef:
      return 0;
   default:
      return is_other;
   }
}

static bool
opt_flip_if_for_mem_loads_impl(nir_function_impl*impl)
{
   nir_metadata_require(impl, nir_metadata_block_index | nir_metadata_divergence);

   nir_builder b = nir_builder_create(impl);

   bool progress = false;
   nir_foreach_block(block, impl) {
      nir_if *nif = nir_block_get_following_if(block);
      if (!nif || !nir_src_is_divergent(&nif->condition))
         continue;

      nir_block *merge = nir_cf_node_cf_tree_next(&nif->cf_node);
      nir_block *then_block = nir_if_last_then_block(nif);
      nir_block *else_block = nir_if_last_else_block(nif);
      if (nir_block_ends_in_jump(then_block) || nir_block_ends_in_jump(else_block))
         continue;

      uint32_t then_first = nir_if_first_then_block(nif)->index;
      uint32_t then_last = nir_if_last_then_block(nif)->index;
      uint32_t else_first = nir_if_first_else_block(nif)->index;
      uint32_t else_last = nir_if_last_else_block(nif)->index;

      bool then_loads = false;
      bool else_loads = false;
      nir_foreach_phi(phi, merge) {
         nir_phi_src *s_then = nir_phi_get_src_from_block(phi, then_block);
         nir_phi_src *s_else = nir_phi_get_src_from_block(phi, else_block);
         unsigned then_src = is_vmem_or_lds_load(s_then->src.ssa, 0, then_first, then_last);
         unsigned else_src = is_vmem_or_lds_load(s_else->src.ssa, 0, else_first, else_last);
         then_loads |= (then_src & is_vmem_lds) && (else_src & is_other);
         else_loads |= (else_src & is_vmem_lds) && (then_src & is_other);
      }
      if (!then_loads || else_loads)
         continue;

      /* invert the condition */
      b.cursor = nir_before_src(&nif->condition);
      nir_src_rewrite(&nif->condition, nir_inot(&b, nif->condition.ssa));

      /* rewrite phi predecessors */
      nir_foreach_phi(phi, merge) {
         nir_foreach_phi_src(src, phi)
            src->pred = src->pred == then_block ? else_block : then_block;
      }

      /* swap the cf_lists */
      nir_cf_list then_list, else_list;
      nir_cf_extract(&then_list, nir_before_cf_list(&nif->then_list),
                     nir_after_cf_list(&nif->then_list));
      nir_cf_extract(&else_list, nir_before_cf_list(&nif->else_list),
                     nir_after_cf_list(&nif->else_list));

      nir_cf_reinsert(&then_list, nir_before_cf_list(&nif->else_list));
      nir_cf_reinsert(&else_list, nir_before_cf_list(&nif->then_list));

      progress = true;
   }

   return nir_progress(progress, impl, 0);
}

bool
ac_nir_opt_flip_if_for_mem_loads(nir_shader *shader)
{
   bool progress = false;
   nir_foreach_function_impl(impl, shader)
      progress |= opt_flip_if_for_mem_loads_impl(impl);
   return progress;
}
