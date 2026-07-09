/*
 * Copyright © 2020 Valve Corporation
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
 */

/*
 * Replaces make availability/visible semantics on barriers with
 * ACCESS_COHERENT on memory loads/stores
 */

#include "nir.h"
#include "nir_builder.h"
#include "shader_enums.h"

static bool
get_intrinsic_info(nir_intrinsic_instr *intrin, nir_variable_mode *modes,
                   bool *reads, bool *writes)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_sparse_load:
      *modes = nir_src_as_deref(intrin->src[0])->modes;
      *reads = true;
      break;
   case nir_intrinsic_image_deref_store:
      *modes = nir_src_as_deref(intrin->src[0])->modes;
      *writes = true;
      break;
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
      *modes = nir_src_as_deref(intrin->src[0])->modes;
      *reads = true;
      *writes = true;
      break;
   case nir_intrinsic_load_ssbo:
      *modes = nir_var_mem_ssbo;
      *reads = true;
      break;
   case nir_intrinsic_store_ssbo:
      *modes = nir_var_mem_ssbo;
      *writes = true;
      break;
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      *modes = nir_var_mem_ssbo;
      *reads = true;
      *writes = true;
      break;
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_transpose_amd:
      *modes = nir_var_mem_global;
      *reads = true;
      break;
   case nir_intrinsic_store_global:
      *modes = nir_var_mem_global;
      *writes = true;
      break;
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_swap:
      *modes = nir_var_mem_global;
      *reads = true;
      *writes = true;
      break;
   case nir_intrinsic_load_deref:
   case nir_intrinsic_load_deref_transpose_amd:
      *modes = nir_src_as_deref(intrin->src[0])->modes;
      *reads = true;
      break;
   case nir_intrinsic_store_deref:
      *modes = nir_src_as_deref(intrin->src[0])->modes;
      *writes = true;
      break;
   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_deref_atomic_swap:
      *modes = nir_src_as_deref(intrin->src[0])->modes;
      *reads = true;
      *writes = true;
      break;
   default:
      return false;
   }
   return true;
}

static bool
visit_instr(nir_instr *instr, uint32_t *cur_modes, unsigned vis_avail_sem)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic == nir_intrinsic_barrier &&
       (nir_intrinsic_memory_semantics(intrin) & vis_avail_sem)) {
      *cur_modes |= nir_intrinsic_memory_modes(intrin);

      unsigned semantics = nir_intrinsic_memory_semantics(intrin);
      nir_intrinsic_set_memory_semantics(
         intrin, semantics & ~vis_avail_sem);

      bool is_split_control_barrier =
         util_bitcount(semantics & NIR_MEMORY_CONTROL_ARRIVE_WAIT) == 1;
      mesa_scope rm_scope = is_split_control_barrier ? SCOPE_NONE : SCOPE_INVOCATION;

      if (nir_intrinsic_memory_semantics(intrin) == 0 &&
          nir_intrinsic_execution_scope(intrin) <= rm_scope)
         nir_instr_remove(instr);

      return true;
   }

   if (!*cur_modes)
      return false; /* early exit */

   nir_variable_mode modes;
   bool reads = false, writes = false;
   if (!get_intrinsic_info(intrin, &modes, &reads, &writes))
      return false;

   if (!reads && vis_avail_sem == NIR_MEMORY_MAKE_VISIBLE)
      return false;
   if (!writes && vis_avail_sem == NIR_MEMORY_MAKE_AVAILABLE)
      return false;

   if (!nir_intrinsic_has_access(intrin))
      return false;

   unsigned access = nir_intrinsic_access(intrin);

   if (access & (ACCESS_NON_READABLE | ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER | ACCESS_COHERENT))
      return false;

   if (*cur_modes & modes) {
      nir_intrinsic_set_access(intrin, access | ACCESS_COHERENT);
      return true;
   }

   return false;
}

static bool
lower_make_visible(nir_cf_node *cf_node, uint32_t *cur_modes)
{
   bool progress = false;
   switch (cf_node->type) {
   case nir_cf_node_block: {
      nir_block *block = nir_cf_node_as_block(cf_node);
      nir_foreach_instr_safe(instr, block)
         progress |= visit_instr(instr, cur_modes, NIR_MEMORY_MAKE_VISIBLE);
      break;
   }
   case nir_cf_node_if: {
      nir_if *nif = nir_cf_node_as_if(cf_node);
      uint32_t cur_modes_then = *cur_modes;
      uint32_t cur_modes_else = *cur_modes;
      foreach_list_typed(nir_cf_node, if_node, node, &nif->then_list)
         progress |= lower_make_visible(if_node, &cur_modes_then);
      foreach_list_typed(nir_cf_node, if_node, node, &nif->else_list)
         progress |= lower_make_visible(if_node, &cur_modes_else);
      *cur_modes |= cur_modes_then | cur_modes_else;
      break;
   }
   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(cf_node);
      assert(!nir_loop_has_continue_construct(loop));
      bool loop_progress;
      do {
         loop_progress = false;
         foreach_list_typed(nir_cf_node, loop_node, node, &loop->body)
            loop_progress |= lower_make_visible(loop_node, cur_modes);
         progress |= loop_progress;
      } while (loop_progress);
      break;
   }
   case nir_cf_node_function:
      UNREACHABLE("Invalid cf type");
   }
   return progress;
}

static bool
lower_make_available(nir_cf_node *cf_node, uint32_t *cur_modes)
{
   bool progress = false;
   switch (cf_node->type) {
   case nir_cf_node_block: {
      nir_block *block = nir_cf_node_as_block(cf_node);
      nir_foreach_instr_reverse_safe(instr, block)
         progress |= visit_instr(instr, cur_modes, NIR_MEMORY_MAKE_AVAILABLE);
      break;
   }
   case nir_cf_node_if: {
      nir_if *nif = nir_cf_node_as_if(cf_node);
      uint32_t cur_modes_then = *cur_modes;
      uint32_t cur_modes_else = *cur_modes;
      foreach_list_typed_reverse(nir_cf_node, if_node, node, &nif->then_list)
         progress |= lower_make_available(if_node, &cur_modes_then);
      foreach_list_typed_reverse(nir_cf_node, if_node, node, &nif->else_list)
         progress |= lower_make_available(if_node, &cur_modes_else);
      *cur_modes |= cur_modes_then | cur_modes_else;
      break;
   }
   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(cf_node);
      assert(!nir_loop_has_continue_construct(loop));
      bool loop_progress;
      do {
         loop_progress = false;
         foreach_list_typed_reverse(nir_cf_node, loop_node, node, &loop->body)
            loop_progress |= lower_make_available(loop_node, cur_modes);
         progress |= loop_progress;
      } while (loop_progress);
      break;
   }
   case nir_cf_node_function:
      UNREACHABLE("Invalid cf type");
   }
   return progress;
}

bool
nir_lower_memory_model(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      bool impl_progress = false;
      struct exec_list *cf_list = &impl->body;

      uint32_t modes = 0;
      foreach_list_typed(nir_cf_node, cf_node, node, cf_list)
         impl_progress |= lower_make_visible(cf_node, &modes);

      modes = 0;
      foreach_list_typed_reverse(nir_cf_node, cf_node, node, cf_list)
         impl_progress |= lower_make_available(cf_node, &modes);
      progress |= nir_progress(impl_progress, impl,
                               nir_metadata_control_flow);
   }

   return progress;
}

static void
nir_lower_disordered_control_barriers_impl(nir_function_impl *impl,
                                           nir_variable *barrier_arrive_without_wait,
                                           nir_variable *skip_next_barrier_wait)
{
   nir_builder b = nir_builder_create(impl);
   nir_foreach_block_safe(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_barrier ||
             nir_intrinsic_execution_scope(intrin) != SCOPE_WORKGROUP)
            continue;

         b.cursor = nir_before_instr(instr);

         unsigned semantics =
            nir_intrinsic_memory_semantics(intrin) & NIR_MEMORY_CONTROL_ARRIVE_WAIT;
         if (semantics == NIR_MEMORY_CONTROL_ARRIVE) {
            nir_store_var(&b, barrier_arrive_without_wait, nir_imm_true(&b), 0x1);
         } else if (semantics == NIR_MEMORY_CONTROL_WAIT) {
            nir_push_if(&b, nir_load_var(&b, skip_next_barrier_wait));
            nir_barrier(&b, .execution_scope = SCOPE_NONE,
                        .memory_scope = nir_intrinsic_memory_scope(intrin),
                        .memory_semantics = nir_intrinsic_memory_semantics(intrin) & ~semantics,
                        .memory_modes = nir_intrinsic_memory_modes(intrin));
            nir_push_else(&b, NULL);
            nir_instr_move(b.cursor, instr);
            nir_pop_if(&b, NULL);
            nir_store_var(&b, skip_next_barrier_wait, nir_imm_false(&b), 0x1);
            nir_store_var(&b, barrier_arrive_without_wait, nir_imm_false(&b), 0x1);
         } else {
            nir_push_if(&b, nir_load_var(&b, barrier_arrive_without_wait));
            nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_NONE,
                        .memory_semantics = NIR_MEMORY_CONTROL_WAIT, .memory_modes = 0);
            nir_store_var(&b, skip_next_barrier_wait, nir_imm_true(&b), 0x1);
            nir_pop_if(&b, NULL);
            nir_store_var(&b, barrier_arrive_without_wait, nir_imm_false(&b), 0x1);
         }
      }
   }

   nir_progress(true, impl, nir_metadata_none);
}

/*
 * Oddly, VK_EXT_shader_split_barrier disallows a
 * ControlBarrierArrive/ControlBarrierWait in-between the two instructions,
 * but it allows a ControlBarrier. If a backend implements workgroup-scope
 * ControlBarrier as ControlBarrierArrive/ControlBarrierWait, this situation
 * would be invalid.
 *
 * This pass fixes this by lowering:
 *    ControlBarrierArrive
 *    ControlBarrier
 *    ControlBarrierWait
 * to:
 *    ControlBarrierArrive
 *    ControlBarrierWait
 *    ControlBarrier
 *    MemoryBarrier
 * if the control barriers involved are workgroup-scope. This works because
 * the wait for the ControlBarrierWait in the original code would not be
 * necessary because of the ControlBarrier in-between.
 *
 * This pass also ensures that any workgroup-scope ControlBarrierArrive is
 * eventually followed by a ControlBarrierWait.
 *
 * nir_lower_global_vars_to_local() and nir_lower_vars_to_ssa() should be run
 * after this pass, if it makes progress. It also assumes returns are lowered.
 */
bool
nir_lower_disordered_control_barriers(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_COMPUTE ||
       !shader->info.cs.has_split_control_barriers)
      return false;

   nir_variable *barrier_arrive_without_wait =
      nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "barrier_arrive_without_wait");
   nir_variable *skip_next_barrier_wait =
      nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "skip_next_barrier_wait");

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_builder b = nir_builder_at(nir_before_impl(impl));
   nir_store_var(&b, barrier_arrive_without_wait, nir_imm_false(&b), 0x1);
   nir_store_var(&b, skip_next_barrier_wait, nir_imm_false(&b), 0x1);

   nir_foreach_function_impl(impl, shader) {
      nir_lower_disordered_control_barriers_impl(
         impl, barrier_arrive_without_wait, skip_next_barrier_wait);
   }

   b.cursor = nir_after_impl(impl);
   nir_push_if(&b, nir_load_var(&b, barrier_arrive_without_wait));
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_NONE,
               .memory_semantics = NIR_MEMORY_CONTROL_WAIT, .memory_modes = 0);
   nir_pop_if(&b, NULL);

   return true;
}
