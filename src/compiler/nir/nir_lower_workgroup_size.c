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
 *
 * Authors:
 *    Timur Kristóf
 *
 */

#include "nir.h"
#include "nir_builder.h"
#include "util/hash_table.h"
#include "util/u_math.h"
#include "util/u_vector.h"

/* State of one logical subgroup during the nir_lower_workgroup_size pass.
 *
 * A logical subgroup appears as a normal subgroup to the application.
 * In reality, two or more logical subgroups can be executed by
 * a real subgroup.
 *
 * The size of a logical subgroup is the same as a real subgroup.
 * Only one logical subgroup may be executed per real subgroup
 * at the same time. This ensures that all subgroup operations
 * keep working and the subgroup invocation ID stays the same.
 */
typedef struct
{
   /* Hash table that maps SSA indices in the original shader
    * to their equivalent in the current logical subgroup.
    */
   struct hash_table *remap_table;

   /* All instructions emitted for the current logical subgroup
    * will be wrapped in an if condition that is predicated by
    * this variable.
    * Set at the beginning of the shader and inside CF in order
    * to track which logical subgroup is active at any point.
    *
    * Divergence of the initial value:
    * - workgroup-uniform if the original workgroup size
    *   is a multiple of the target workgroup size and
    *   all logical subgroups are fully occupied.
    * - otherwise, divergent.
    *
    * Within loops and branches, this value might diverge.
    */
   nir_def *predicate;

   /* Used inside loops.
    * Determines whether the current logical subgroup needs to
    * execute the current loop or not. Set at the beginning of
    * each loop according to the predicate, and cleared when
    * the logical subgroup executes a break.
    * (Same divergence as the predicate.)
    */
   nir_variable *participates_in_current_loop;

   /* Used inside loops.
    * Determines whether the current logical subgroup needs to
    * execute the current loop iteration. Set at the beginning of
    * each loop iteration according to loop participation,
    * and cleared when the logical subgroup executes a break or continue.
    * (Same divergence as the predicate.)
    */
   nir_variable *participates_in_current_loop_iteration;

   /* Vector of instructions to be lowered after the CF
    * transformations are done. The lowering must be done afterwards
    * because we have no good way to update the remap table
    * so we can't lower the instructions early.
    */
   struct u_vector instrs_lowered_later;

   /* Value of various system values inside the logical subgroup.
    * These represent the workgroup as it looks to the application.
    * Compute system values inside the logical subgroup
    * will be lowered to use these instead.
    */
   struct {
      nir_def *local_invocation_index;
      nir_def *subgroup_id;
      nir_def *num_subgroups;
   } sysvals;

} nlwgs_logical_sg_state;

typedef struct
{
   /* Vector of extracted control flow parts.
    * We need to keep these alive until we are finished with
    * CF manipulations to keep the remap table working correctly.
    * They are freed when we finished processing each function impl.
    */
   struct u_vector extracted_cf_vec;

   /* A piece of CF that needs to be reinserted at the start
    * when the pass is finished. This is extracted to make sure
    * the pass excludes it from its CF manipulations.
    */
   nir_cf_list reinsert_at_start;

   /* Number of logical subgroups per real subgroup.
    * Same as the factor between real and logical workgroup size.
    */
   uint32_t num_logical_sg;

   /* Target workgroup size.
    * - For shaders with known exact workgroup size,
    *   this is the exact workgroup size after the lowering is done.
    * - For shaders with variable workgroup size,
    *   this is only the workgroup size hint of the shader after the lowering is done.
    */
   uint32_t target_wg_size;

   /* State of each logical subgroup.
    * Note that logical subgroups are tracked from the perspective
    * of one real subgroup.
    */
   nlwgs_logical_sg_state *logical;

   bool inside_loop;

} nlwgs_state;

static void nlwgs_augment_cf_list(nir_builder *b, struct exec_list *cf_list, nlwgs_state *s);
static bool nlwgs_cf_list_has_barrier(struct exec_list *cf_list);

static nir_def *
nlwgs_remap_def(nir_def *original, nlwgs_logical_sg_state *ls)
{
   struct hash_entry *entry = _mesa_hash_table_search(ls->remap_table, original);
   assert(entry);
   return (nir_def *)entry->data;
}

/**
 * Copy pointers to the instructions inside a block into an array.
 * This is necessary to be able to safely iterate over those instructions
 * because even nir_foreach_instr_safe is not safe enough for the
 * CF transformations we do for some instruction types.
 */
static nir_instr **
nlwgs_copy_instrs_to_array(nir_shader *shader, nir_block *block, unsigned *out_num_instr)
{
   const unsigned num_instrs = exec_list_length(&block->instr_list);
   *out_num_instr = num_instrs;

   if (!num_instrs)
      return NULL;

   nir_instr **instrs = ralloc_array(shader, nir_instr *, num_instrs);
   unsigned i = 0;

   nir_foreach_instr(instr, block) {
      instrs[i++] = instr;
   }

   assert(i == num_instrs);
   return instrs;
}

/**
 * Copy pointers to the CF nodes inside a CF list into an array.
 * This is necessary to be able to safely iterate over those CF nodes
 * because we may heavily modify the CF during the process.
 */
static nir_cf_node **
nlwgs_copy_cf_nodes_to_array(nir_shader *shader, struct exec_list *cf_list, unsigned *out_num_cf_nodes)
{
   const unsigned num_cf_nodes = exec_list_length(cf_list);
   *out_num_cf_nodes = num_cf_nodes;

   if (!num_cf_nodes)
      return NULL;

   nir_cf_node **cf_nodes = ralloc_array(shader, nir_cf_node *, num_cf_nodes);
   unsigned i = 0;

   foreach_list_typed(nir_cf_node, cf_node, node, cf_list) {
      cf_nodes[i++] = cf_node;
   }

   assert(i == num_cf_nodes);
   return cf_nodes;
}

/**
 * Checks whether the instruction is a workgroup barrier.
 * For the purposes of this pass, we need to consider every
 * instruction that depends on the execution of other subgroups
 * as a workgroup barrier.
 */
static bool
nlwgs_instr_is_barrier(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_barrier:
         if (nir_intrinsic_execution_scope(intrin) < SCOPE_WORKGROUP &&
             nir_intrinsic_memory_scope(intrin) < SCOPE_WORKGROUP)
            break;
         return true;
      case nir_intrinsic_set_vertex_and_primitive_count:
      case nir_intrinsic_launch_mesh_workgroups:
         return true;
      default:
         break;
      }
      break;
   }
   case nir_instr_type_call:
   case nir_instr_type_cmat_call: {
      /* Consider function calls as a workgroup barrier because:
       * - the function may contain a workgroup barrier
       * - each function is separately augmented to be aware of
       *   logical subgroups, so should be only called once
       */
      return true;
   }
   default:
      break;
   }

   return false;
}

static bool
nlwgs_cf_node_has_barrier(nir_cf_node *cf_node)
{
   nir_foreach_block_in_cf_node(block, cf_node) {
      nir_foreach_instr(instr, block) {
         if (nlwgs_instr_is_barrier(instr))
            return true;
      }
   }

   return false;
}

static nir_def *
nlwgs_load_predicate(nir_builder *b, nlwgs_logical_sg_state *ls, UNUSED nlwgs_state *s)
{
   if (s->inside_loop) {
      nir_def *in_iteration = nir_load_var(b, ls->participates_in_current_loop_iteration);
      return nir_iand(b, ls->predicate, in_iteration);
   }

   return ls->predicate;
}

static nir_def **
nlwgs_save_current_predicates(nir_builder *b, nlwgs_state *s)
{
   nir_def **saved = rzalloc_array(b->shader, nir_def *, s->num_logical_sg);
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      saved[i] = s->logical[i].predicate;
   }
   return saved;
}

static void
nlwgs_reload_saved_predicates_and_free(nir_builder *b, nir_def **saved, nlwgs_state *s)
{
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      s->logical[i].predicate = saved[i];
   }
   ralloc_free(saved);
}

static nir_variable **
nlwgs_save_loop_participatation(nir_builder *b, nlwgs_state *s)
{
   nir_variable **saved = rzalloc_array(b->shader, nir_variable *, s->num_logical_sg * 2);
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      saved[i] = ls->participates_in_current_loop;
      saved[s->num_logical_sg + i] = ls->participates_in_current_loop_iteration;
   }
   return saved;
}

static void
nlwgs_reload_saved_loop_participation_and_free(nir_builder *b, nir_variable **saved, nlwgs_state *s)
{
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      ls->participates_in_current_loop = saved[i];
      ls->participates_in_current_loop_iteration = saved[s->num_logical_sg + i];
   }
   ralloc_free(saved);
}

static bool
nlwgs_instr_splits_augmented_block(nir_instr *instr)
{
   if (nlwgs_instr_is_barrier(instr))
      return true;

   if (instr->type == nir_instr_type_jump) {
      nir_jump_instr *jump = nir_instr_as_jump(instr);
      switch (jump->type) {
      case nir_jump_break:
      case nir_jump_continue:
         return true;
      case nir_jump_halt:
      case nir_jump_return:
      case nir_jump_abort:
      case nir_jump_goto:
      case nir_jump_goto_if:
         UNREACHABLE("halt/return/goto should have been already lowered");
         break;
      }
   }

   return false;
}

static void
nlwgs_process_reinserted_intrin(nir_builder *b, nir_intrinsic_instr *intrin, nlwgs_logical_sg_state *ls)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_num_subgroups:
   case nir_intrinsic_load_subgroup_id:
   case nir_intrinsic_load_local_invocation_index:
      /* Add instructions to a list of instructions to be lowered later.
       * We need to lower these depending on which logical subgroup they belong to.
       * We can't lower them here, because that would mess up the remap table.
       */
      *(nir_instr **)u_vector_add(&ls->instrs_lowered_later) = &intrin->instr;
      break;
   case nir_intrinsic_decl_reg:
      /* NIR only allows to declare registers at the beginning of the function.
       * Therefore we need to move all the duplicated register definitions up.
       * We can do this here as it doesn't change the definition and therefore
       * doesn't mess up the remap table.
       */
      nir_instr_move(nir_before_impl(b->impl), &intrin->instr);
      break;
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_global_invocation_id:
   case nir_intrinsic_load_workgroup_size:
      UNREACHABLE("intrinsic should have been lowered already");
      break;
   default:
      break;
   }
}

static void
nlwgs_process_reinserted_block(nir_builder *b, nir_block *block,
                               const bool allow_splitter_instrs, nlwgs_logical_sg_state *ls)
{
   nir_foreach_instr_safe(instr, block) {
      /* Instructions that would otherwise split an augmented block are
       * not allowed here when we are augmenting a block (the block should be split),
       * but they are allowed when we are repeating a greated portion of the shader
       * that didn't contain any barriers.
       */
      if (!allow_splitter_instrs)
         assert(!nlwgs_instr_splits_augmented_block(instr));

      switch (instr->type) {
      case nir_instr_type_intrinsic:
         nlwgs_process_reinserted_intrin(b, nir_instr_as_intrinsic(instr), ls);
         break;
      case nir_instr_type_undef:
         nir_instr_move(nir_before_impl(b->impl), instr);
         break;
      case nir_instr_type_phi:
         UNREACHABLE("should have been lowered away");
         break;
      default:
         break;
      }
   }
}

static void
nlwgs_process_reinserted_cf(nir_builder *b, struct exec_list *cf_list,
                            const bool allow_splitter_instrs, nlwgs_logical_sg_state *ls)
{
   foreach_list_typed_safe(nir_cf_node, cf_node, node, cf_list) {
      nir_foreach_block_in_cf_node(block, cf_node) {
         nlwgs_process_reinserted_block(b, block, allow_splitter_instrs, ls);
      }
   }
}

/**
 * Repeats the range so it can be executed by each logical subgroup.
 * Wraps each repetition in the predicate for the current logical subgroup.
 */
static void
nlwgs_repeat_and_predicate_range(nir_builder *b, nir_cursor start, nir_cursor end,
                                 const bool allow_splitter_instrs, nlwgs_state *s)
{
   /* Don't do anything if the range is empty */
   if (nir_cursors_equal(start, end))
      return;

   /* Extract the range from the shader and save it to be freed later. */
   nir_cf_list **extracted_cf = u_vector_add(&s->extracted_cf_vec);
   *extracted_cf = rzalloc(b->shader, nir_cf_list);
   b->cursor = nir_cf_extract(*extracted_cf, start, end);

   /* Create a copy of the range for each logical subgroup. */
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      nir_def *predicate = nlwgs_load_predicate(b, ls, s);
      nir_if *predicated_if = nir_push_if(b, predicate);
      {
         nir_cf_list cloned;
         nir_cf_node *parent = &nir_cursor_current_block(b->cursor)->cf_node;
         nir_cf_list_clone(&cloned, *extracted_cf, parent, ls->remap_table);
         nir_cf_reinsert(&cloned, b->cursor);

         b->cursor = nir_after_cf_list(&predicated_if->then_list);
      }
      nir_pop_if(b, predicated_if);

      nlwgs_process_reinserted_cf(b, &predicated_if->then_list, allow_splitter_instrs, ls);
   }
}

/**
 * Augment a break or continue instruction to make them aware of logical subgroups.
 *
 * Continue is implemented as follows:
 *
 * 1. Clear participation in current loop iteration, for all active logical subgroups.
 *    These logical subgroups won't do anything anymore in the current loop iteration,
 *    because the participation is included when loading their predicate.
 * 2. We can execute a real continue when all logical subgroups can continue
 *    at the same time. This is the case when all logical subgroups are either
 *    active or don't participate in the loop iteration anymore.
 *
 * Break is implemented as follows:
 *
 * 1. Clear participation in current loop iteration, for all active logical subgroups.
 * 2. Clear participation in current loop, for all active logical subgroups.
 *    These logical subgroups won't do anything anymore in subsequent loop
 *    iterations. They basically won't care what's happening in the loop anymore.
 * 2. We can execute a real break when all logical subgroups can break
 *    at the same time. This is the case when all logical subgroups are either
 *    active or don't participate in the loop anymore.
 *
 */
static void
nlwgs_augment_break_continue(nir_builder *b, nir_jump_instr *jump, nlwgs_state *s)
{
   const nir_jump_type jump_type = jump->type;
   assert(jump_type == nir_jump_break || jump_type == nir_jump_continue);

   nir_instr_remove(&jump->instr);

   nir_def *fals = nir_imm_false(b);
   nir_def *all_logical_sg_can_jump = nir_imm_true(b);

   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      nir_def *predicate = nlwgs_load_predicate(b, ls, s);

      nir_if *if_predicate = nir_push_if(b, predicate);
      {
         nir_store_var(b, ls->participates_in_current_loop_iteration, fals, 1);
         if (jump_type == nir_jump_break)
            nir_store_var(b, ls->participates_in_current_loop, fals, 1);
      }
      nir_pop_if(b, if_predicate);

      nir_def *can_jump =
         jump_type == nir_jump_break
         ? nir_inot(b, nir_load_var(b, ls->participates_in_current_loop))
         : nir_inot(b, nir_load_var(b, ls->participates_in_current_loop_iteration));

      all_logical_sg_can_jump = nir_iand(b, all_logical_sg_can_jump, can_jump);
   }

   /* If every logical subgroup wants to break or continue, we can actually do that. */
   nir_if *if_all_logical_sg_agree = nir_push_if(b, all_logical_sg_can_jump);
   {
      nir_jump(b, jump_type);
   }
   nir_pop_if(b, if_all_logical_sg_agree);
}

/**
 * Adjusts sources of intrinsics which are specced to use
 * values from the first active invocation. Typically, these
 * intrinsics should only appear once in the shader, so we
 * shouldn't duplicate them.
 *
 * The first active invocation may be in either logical subgroup,
 * depending on which one is active at the time. So we need to
 * check the predicate of each logical subgroup.
 *
 * If neither logical subgroup is active, that means the shader
 * was out of spec. In this case use zero for the sake of simplicity.
 */
static void
nlwgs_intrin_src_first_active_logical_subgroup(nir_builder *b, nir_intrinsic_instr *intrin, nlwgs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   for (unsigned i = 0; i < nir_intrinsic_infos[intrin->intrinsic].num_srcs; ++i) {
      nir_def *original_src = intrin->src[i].ssa;
      nir_def *new_src_def = nir_imm_zero(b, original_src->num_components, original_src->bit_size);
      nir_def *found = nir_imm_false(b);

      for (unsigned i = 0; i < s->num_logical_sg; ++i) {
         nlwgs_logical_sg_state *ls = &s->logical[i];
         nir_def *ls_src = nlwgs_remap_def(original_src, ls);
         nir_def *predicate = nlwgs_load_predicate(b, ls, s);
         nir_def *found_now = nir_iand(b, nir_inot(b, found), predicate);
         new_src_def = nir_bcsel(b, found_now, ls_src, new_src_def);
         found = nir_ior(b, found, found_now);
      }

      nir_src_rewrite(&intrin->src[i], new_src_def);
   }
}

static void
nlwgs_process_splitter_instr(nir_builder *b, nir_instr *instr, nlwgs_state *s)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_set_vertex_and_primitive_count:
      case nir_intrinsic_launch_mesh_workgroups:
      case nir_intrinsic_launch_mesh_workgroups_with_payload_deref:
         /* Keep task/mesh intrinsics in spec. */
         nlwgs_intrin_src_first_active_logical_subgroup(b, intrin, s);
         break;
      default:
         break;
      }
   } else if (instr->type == nir_instr_type_jump) {
      nlwgs_augment_break_continue(b, nir_instr_as_jump(instr), s);
   }
}

/**
 * Augment a block so that it becomes aware of logical subgroups.
 * Only necessary when the block isn't repeated as part of a larger range.
 *
 * We repeat the instructions inside the block for every
 * logical subgroup. The challenge is that we need to split
 * the block along barriers and barrier-like instructions
 * to preserve the behaviour of the shader.
 */
static void
nlwgs_augment_block(nir_builder *b, nir_block *block, nlwgs_state *s)
{
   unsigned num_instrs;
   nir_instr **instrs = nlwgs_copy_instrs_to_array(b->shader, block, &num_instrs);
   assert(!num_instrs || instrs);

   if (!num_instrs)
      return;

   nir_cursor start = nir_before_instr(instrs[0]);
   unsigned num_repeatable_instrs = 0;

   for (unsigned i = 0; i < num_instrs; ++i) {
      nir_instr *instr = instrs[i];

      if (!nlwgs_instr_splits_augmented_block(instr)) {
         num_repeatable_instrs++;
         continue;
      }

      if (num_repeatable_instrs) {
         nir_cursor end = nir_before_instr(instr);
         nlwgs_repeat_and_predicate_range(b, start, end, false, s);
         num_repeatable_instrs = 0;
      }

      nlwgs_process_splitter_instr(b, instr, s);

      if (i < num_instrs - 1)
         start = nir_before_instr(instrs[i + 1]);
   }

   if (num_repeatable_instrs) {
      nir_cursor end = nir_after_instr(instrs[num_instrs - 1]);
      nlwgs_repeat_and_predicate_range(b, start, end, false, s);
   }

   ralloc_free(instrs);
}

/**
 * Augment an if so that it becomes aware of logical subgroup.
 * Only necessary when the if isn't repeated as part of a larger range.
 *
 * We augment the contents inside the then and else branches recursively,
 * while making sure that everything is only executed under the same
 * conditions as it would in the original shader.
 */
static void
nlwgs_augment_if(nir_builder *b, nir_if *the_if, nlwgs_state *s)
{
   nir_def **saved_predicates = nlwgs_save_current_predicates(b, s);
   nir_def **logical_else_predicates = rzalloc_array(b->shader, nir_def *, s->num_logical_sg);
   nir_def *original_condition = the_if->condition.ssa;

   b->cursor = nir_before_cf_node(&the_if->cf_node);
   nir_def *any_logical_subgroup_takes_then = nir_imm_false(b);
   nir_def *any_logical_subgroup_takes_else = nir_imm_false(b);

   /* Determine which logical subgroup needs to take which branch.
    * Include the branch condition in the predicate for the logical subgroup.
    * This is necessary because we take the branch if ANY logical subgroup needs to,
    * so we need to disable the logical subgroups that don't.
    */
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      nir_def *ls_condition = nlwgs_remap_def(original_condition, ls);
      nir_def *predicate = nlwgs_load_predicate(b, ls, s);
      nir_def *then_cond = nir_iand(b, ls_condition, predicate);
      nir_def *else_cond = nir_iand(b, nir_inot(b, ls_condition), predicate);

      any_logical_subgroup_takes_then = nir_ior(b, any_logical_subgroup_takes_then, then_cond);
      any_logical_subgroup_takes_else = nir_ior(b, any_logical_subgroup_takes_else, else_cond);
      ls->predicate = then_cond;
      logical_else_predicates[i] = else_cond;
   }

   nir_src_rewrite(&the_if->condition, any_logical_subgroup_takes_then);

   nlwgs_augment_cf_list(b, &the_if->then_list, s);

   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      ls->predicate = logical_else_predicates[i];
   }

   /* It is possible that some logical subgroups need to take
    * the then branch and others the else branch. To make this possible,
    * we need to extract the else branch and move it to a separate if.
    */
   nir_cf_list extracted;
   nir_cf_list_extract(&extracted, &the_if->else_list);
   b->cursor = nir_after_cf_node(&the_if->cf_node);
   nir_if *the_else = nir_push_if(b, any_logical_subgroup_takes_else);
   {
      nir_cf_reinsert(&extracted, b->cursor);
   }
   nir_pop_if(b, the_else);

   nlwgs_augment_cf_list(b, &the_else->then_list, s);

   nlwgs_reload_saved_predicates_and_free(b, saved_predicates, s);
   ralloc_free(logical_else_predicates);
}

/**
 * Augment a loop so that it becomes aware of logical subgroup.
 * Only necessary when the loop isn't repeated as part of a larger range.
 *
 * We augment the contents inside the loop recursively,
 * while making sure that everything is only executed under the same
 * conditions as it would in the original shader:
 *
 * - We use a variables called participates_in_current_loop
 *   to keep track of which logical subgroup still participates
 *   in the loop. This is set (to the predicate) before the loop
 *   and cleared when the logical subgroup executes a break.
 *
 * - We use a variable called participates_in_current_loop_iteration
 *   to keep track of which logical subgroup still participates
 *   in the current loop iteration. This is set at the beginning of
 *   each loop iteration (according to the loop participation)
 *   and cleared when the logical subgroup executes a continue.
 *
 * - When loading the predicate inside a loop, we also include
 *   participation in the current loop iteration. This ensures that
 *   loop control flow and nested loops keep working.
 *
 */
static void
nlwgs_augment_loop(nir_builder *b, nir_loop *loop, nlwgs_state *s)
{
   assert(!nir_loop_has_continue_construct(loop));

   const bool was_inside_loop = s->inside_loop;
   nir_variable **saved_lp = nlwgs_save_loop_participatation(b, s);
   nir_def **saved_predicates = nlwgs_save_current_predicates(b, s);

   b->cursor = nir_before_cf_node(&loop->cf_node);
   nir_def *const tru = nir_imm_true(b);

   /* Initialize loop participation variables for the new loop.
    * These are based on the predicate, which includes participation
    * in outer loops, if there are any.
    */
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      nir_def *predicate = nlwgs_load_predicate(b, ls, s);
      ls->participates_in_current_loop =
         nir_local_variable_create(
            b->impl,
            glsl_bool_type(),
            ralloc_asprintf(b->shader, "logical_subgroup_%u_participates_in_loop", i));
      ls->participates_in_current_loop_iteration =
         nir_local_variable_create(
            b->impl,
            glsl_bool_type(),
            ralloc_asprintf(b->shader, "logical_subgroup_%u_participates_in_loop_iteration", i));

      nir_store_var(b, ls->participates_in_current_loop, predicate, 1);
      nir_store_var(b, ls->participates_in_current_loop_iteration, predicate, 1);

      /* The loop iteration participation will already contain
       * the predicate from outside the loop, so we can set the initial
       * predicate inside the loop to just true at this point.
       */
      ls->predicate = tru;
   }

   s->inside_loop = true;

   nlwgs_augment_cf_list(b, &loop->body, s);

   b->cursor = nir_before_cf_list(&loop->body);
   nir_def *any_logical_sg_participate = nir_imm_false(b);
   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];

      /* See if any logical subgroups still participate in the loop. */
      nir_def *participate = nir_load_var(b, ls->participates_in_current_loop);
      any_logical_sg_participate = nir_ior(b, any_logical_sg_participate, participate);

      /* Set participation in the current loop iteration to
       * the participation in the loop. This is to make continue work correctly.
       */
      nir_store_var(b, ls->participates_in_current_loop_iteration, participate, 1);
   }

   /* Insert a break at the start of the loop,
    * in case none of the logical subgroups participate in the loop anymore.
    * Without this, we would risk creating infinite loops, because
    * logical subgroups can stop participating in the loop at different times
    * and at that point they wouldn't execute conditional breaks anymore.
    *
    * This is technically not necessary for workgroup-uniform loops
    * because in that case all logical subgroups would always execute breaks
    * at the same point.
    */
   nir_break_if(b, nir_inot(b, any_logical_sg_participate));

   s->inside_loop = was_inside_loop;
   nlwgs_reload_saved_predicates_and_free(b, saved_predicates, s);
   nlwgs_reload_saved_loop_participation_and_free(b, saved_lp, s);
}

static void
nlwgs_augment_cf_node(nir_builder *b, nir_cf_node *cf_node, nlwgs_state *s)
{
   switch (cf_node->type) {
   case nir_cf_node_block:
      nlwgs_augment_block(b, nir_cf_node_as_block(cf_node), s);
      break;

   case nir_cf_node_if:
      nlwgs_augment_if(b, nir_cf_node_as_if(cf_node), s);
      break;

   case nir_cf_node_loop:
      nlwgs_augment_loop(b, nir_cf_node_as_loop(cf_node), s);
      break;

   case nir_cf_node_function:
      UNREACHABLE("function calls should have been lowered already");
   }
}

/**
 * Augments the given CF list to be aware of logical subgroups.
 * There are two strategies to achieve this:
 *
 * - When the CF contains barriers, we can't just repeat
 *   the code and we need to augment each CF node individually.
 *
 * - In case parts of the CF don't contain any barriers, we can simply
 *   repeat and predicate that CF for each logical subgroup.
 *   It is technically not necessary to implement this strategy, but
 *   in practice it helps reduce the amount of branches in the shader
 *   and therefore improves compile times.
 *
 */
static void
nlwgs_augment_cf_list(nir_builder *b, struct exec_list *cf_list, nlwgs_state *s)
{
   unsigned num_cf_nodes;
   nir_cf_node **cf_nodes = nlwgs_copy_cf_nodes_to_array(b->shader, cf_list, &num_cf_nodes);
   assert(cf_nodes && num_cf_nodes);

   nir_cursor start = nir_before_cf_list(cf_list);
   unsigned num_repeatable_cf_nodes = 0;

   for (unsigned i = 0; i < num_cf_nodes; ++i) {
      nir_cf_node *cf_node = cf_nodes[i];

      if (!nlwgs_cf_node_has_barrier(cf_node)) {
         num_repeatable_cf_nodes++;
         continue;
      }

      if (num_repeatable_cf_nodes) {
         /* NIR can split/stitch blocks during CF manipulation, so it isn't
          * guaranteed that the cf_node pointer stays at the same node.
          * To work around that, insert a nop and use it to keep track
          * of where the current block was.
          */
         b->cursor = nir_before_cf_node(cf_node);
         nir_intrinsic_instr *nop = nir_nop(b);
         nir_cursor end = nir_before_instr(&nop->instr);

         nlwgs_repeat_and_predicate_range(b, start, end, true, s);

         /* Find our way back to the current block. */
         nir_cf_node_type t = cf_node->type;
         cf_node = t == nir_cf_node_block ? &nop->instr.block->cf_node : nir_cf_node_next(&nop->instr.block->cf_node);
         nir_instr_remove(&nop->instr);

         num_repeatable_cf_nodes = 0;
      }

      nlwgs_augment_cf_node(b, cf_node, s);

      if (i < num_cf_nodes - 1)
         start = nir_before_cf_node(cf_nodes[i + 1]);
   }

   if (num_repeatable_cf_nodes) {
      nir_cursor end = nir_after_cf_node(cf_nodes[num_cf_nodes - 1]);
      nlwgs_repeat_and_predicate_range(b, start, end, true, s);
   }

   ralloc_free(cf_nodes);
}

/**
 * Lower reinserted compute intrinsics.
 *
 * - We can only do it after reinsertion because they depend on
 *   which logical subgroup they are reinserted for.
 * - We can only do it after all CF is finished, because
 *   otherwise we'd mess up the remap table.
 *
 * Because each real subgroup executes only one logical subgroup
 * at a time and the subgroup size is the same between real and
 * logical subgroups, we only need to lower a small handful of
 * compute sysvals.
 *
 * All subgroup intrinsics remain intact and don't need lowering.
 */
static void
nlwgs_lower_reinserted_intrin(UNUSED nir_builder *b, nir_intrinsic_instr *intrin, nlwgs_logical_sg_state *ls)
{
   nir_def *replacement = NULL;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_num_subgroups:
      replacement = ls->sysvals.num_subgroups;
      break;
   case nir_intrinsic_load_subgroup_id:
      replacement = ls->sysvals.subgroup_id;
      break;
   case nir_intrinsic_load_local_invocation_index:
      replacement = ls->sysvals.local_invocation_index;
      break;
   default:
      return;
   }

   assert(replacement);
   nir_def_replace(&intrin->def, replacement);
}

static void
nlwgs_lower_reinserted_instrs(nir_builder *b, nlwgs_logical_sg_state *ls)
{
   nir_instr **lowerable;
   u_vector_foreach(lowerable, &ls->instrs_lowered_later) {
      nir_instr *instr = *lowerable;

      switch (instr->type) {
      case nir_instr_type_intrinsic:
         nlwgs_lower_reinserted_intrin(b, nir_instr_as_intrinsic(instr), ls);
         break;
      default:
         UNREACHABLE("unimplemented");
      }
   }
}

static uint16_t
nlwgs_calc_1d_size(uint16_t size[3])
{
   return size[0] * size[1] * size[2];
}

static void
nlwgs_adjust_size(uint16_t size[3], uint32_t target_wg_size)
{
   size[0] = target_wg_size;
   size[1] = 1;
   size[2] = 1;
}

static void
nlwgs_adjust_workgroup_size(nir_shader *shader, uint32_t target_wg_size)
{
   if (!shader->info.workgroup_size_variable)
      nlwgs_adjust_size(shader->info.workgroup_size, target_wg_size);

   nlwgs_adjust_size(shader->info.cs.workgroup_size_hint, target_wg_size);
}

static void
nlwgs_init_function_impl(nir_builder *b, nlwgs_state *s, uint32_t target_wg_size)
{
   u_vector_init(&s->extracted_cf_vec, 4, sizeof(nir_cf_list *));

   b->cursor = nir_before_impl(b->impl);

   nir_def *logical_wg_size_1d;
   nir_def *real_wg_size_1d;
   bool all_logical_sg_utilized = false;

   if (!b->shader->info.workgroup_size_variable) {
      const uint32_t original_workgroup_size =
         nlwgs_calc_1d_size(b->shader->info.workgroup_size);
      logical_wg_size_1d = nir_imm_int(b, original_workgroup_size);
      real_wg_size_1d = nir_imm_int(b, target_wg_size);
      all_logical_sg_utilized =
         target_wg_size * s->num_logical_sg == original_workgroup_size;
   } else {
      /* TODO: support variable workgroup size */
      abort();
   }

   nir_def *real_num_sg = nir_load_num_subgroups(b);
   nir_def *real_sg_id = nir_load_subgroup_id(b);
   nir_def *real_local_invocation_index = nir_load_local_invocation_index(b);
   nir_def *total_num_logical_sg = nir_imul_imm(b, real_num_sg, s->num_logical_sg);

   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      ls->remap_table = _mesa_pointer_hash_table_create(b->shader);
      u_vector_init(&ls->instrs_lowered_later, 16, sizeof(nir_instr **));

      nir_def *logical_sg_id =
         nir_iadd(b, nir_imul_imm(b, real_num_sg, i), real_sg_id);
      nir_def *logical_local_invocation_index =
         nir_iadd(b, nir_imul_imm(b, real_wg_size_1d, i), real_local_invocation_index);

      ls->sysvals.local_invocation_index = logical_local_invocation_index;
      ls->sysvals.subgroup_id = logical_sg_id;
      ls->sysvals.num_subgroups = total_num_logical_sg;

      /* Only last logical subgroup may be incative in some real subgroups.
       * At least one real subgroup definitely needs all logical subgroups.
       */
      nir_def *logical_sg_active =
         all_logical_sg_utilized
         ? nir_imm_true(b)
         : nir_ult(b, logical_local_invocation_index, logical_wg_size_1d);

      ls->predicate = logical_sg_active;
   }

   /* Extract the instructions we just emitted, to prevent them from
    * being subject to the CF manipulations in the pass. They will be
    * reinserted at the end.
    */
   nir_cf_extract(&s->reinsert_at_start, nir_before_impl(b->impl), b->cursor);
}

static void
nlwgs_finish_function_impl(nir_builder *b, nlwgs_state *s)
{
   nir_cf_reinsert(&s->reinsert_at_start, nir_before_impl(b->impl));

   for (unsigned i = 0; i < s->num_logical_sg; ++i) {
      nlwgs_logical_sg_state *ls = &s->logical[i];
      nlwgs_lower_reinserted_instrs(b, ls);
      u_vector_finish(&ls->instrs_lowered_later);
      _mesa_hash_table_destroy(ls->remap_table, NULL);
   }

   nir_cf_list **extracted_cf;
   u_vector_foreach(extracted_cf, &s->extracted_cf_vec) {
      nir_cf_delete(*extracted_cf);
      ralloc_free(*extracted_cf);
   }

   u_vector_finish(&s->extracted_cf_vec);
}

static bool
nlwgs_lower_shader(nir_shader *shader, uint32_t factor, const uint32_t target_wg_size)
{
   assert(factor > 1);
   assert(mesa_shader_stage_uses_workgroup(shader->info.stage));
   assert(!shader->info.workgroup_size_variable);

   /* Eliminate phis by lowering them to registers.
    * Thus, we don't have to care about phis while transforming CF.
    */
   nir_convert_from_ssa(shader, true, false);

   nir_foreach_function_impl(impl, shader) {
      nir_builder builder = nir_builder_create(impl);

      nlwgs_state state = {
         .num_logical_sg = factor,
         .logical = rzalloc_array(shader, nlwgs_logical_sg_state, factor),
      };

      nlwgs_init_function_impl(&builder, &state, target_wg_size);
      nlwgs_augment_cf_list(&builder, &impl->body, &state);
      nlwgs_finish_function_impl(&builder, &state);

      /* Stop derefs from going crazy. */
      nir_rematerialize_derefs_in_use_blocks_impl(impl);

      ralloc_free(state.logical);
      nir_progress(true, impl, nir_metadata_none);
   }

   /* After lowering blocks, we end up using SSA defs between
    * different blocks without phis. We need to repair that.
    */
   NIR_PASS(_, shader, nir_repair_ssa);

   /* Now it's time to get rid of registers and go back to SSA. */
   NIR_PASS(_, shader, nir_lower_reg_intrinsics_to_ssa);

   nlwgs_adjust_workgroup_size(shader, target_wg_size);

   return true;
}

/**
 * Lowers a shader to use a smaller workgroup to do the same work,
 * while it will still appear as a bigger workgroup to applications.
 *
 * Mainly intended for working around hardware limitations,
 * for example when the HW has an upper limit on the workgroup size
 * or doesn't support workgroups at all, but the API requires a
 * certain minimum.
 *
 * Only applicable to shader stages that use workgroups.
 * Creates local variables, lower them with nir_lower_vars_to_ssa.
 * Always flattens workgroup size to 1D.
 * Does not change subgroup size.
 * Does not support variable workgroup size.
 *
 * @target_wg_size - Exact target workgroup size.
 */
bool
nir_lower_workgroup_size(nir_shader *shader, const uint32_t target_wg_size)
{
   assert(mesa_shader_stage_uses_workgroup(shader->info.stage));
   assert(!shader->info.workgroup_size_variable);

   /* Eliminate local invocation ID and only rely on index.  This allows us to
    * set the real workgroup size in 1D and we won't have to deal with the 3D
    * intrinsics.
    *
    * If the caller really needs 3D invocation ID, it will need to lower it
    * back later.
    */
   nir_lower_compute_system_values_options nlcsv_options = {
      .lower_cs_local_id_to_index = true,
   };
   bool progress = nir_lower_compute_system_values(shader, &nlcsv_options);

   /* Check if shader is already at the target workgroup size.
    *
    * The call to nir_lower_compute_system_values() above already cleans up
    * metadata for us so we don't need to bother here.
    */
   if (shader->info.workgroup_size[0] == target_wg_size &&
       shader->info.workgroup_size[1] == 1 &&
       shader->info.workgroup_size[2] == 1)
      return progress;

   const uint32_t orig_wg_size = nlwgs_calc_1d_size(shader->info.workgroup_size);
   assert(orig_wg_size >= target_wg_size);
   if (orig_wg_size == target_wg_size) {
      /* Flatten it to 1D, regardless of whether or not we need lowering */
      nlwgs_adjust_workgroup_size(shader, target_wg_size);
      return true;
   }

   /* Calculate factor, ie. number of logical subgroups per real subgroup. */
   const uint32_t factor = DIV_ROUND_UP(orig_wg_size, target_wg_size);
   assert(factor > 1);

   /* Do the actual lowering */
   progress |= nlwgs_lower_shader(shader, factor, target_wg_size);

   return progress;
}
