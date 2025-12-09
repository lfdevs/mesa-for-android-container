/*
 * Copyright (c) 2025 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/list.h"
#include "util/ralloc.h"
#include "ppir.h"

/* Checks is successor is sequential, as in there is no branch in the block
 * that points to the successor */
static bool ppir_block_succ_is_seq(ppir_block *pred, ppir_block *succ)
{
   list_for_each_entry_safe(ppir_node, node, &pred->node_list, list) {
      if (node->op == ppir_op_branch) {
         ppir_branch_node *branch = ppir_node_to_branch(node);
         if (branch->target == succ)
            return false;
      }
   }

   return true;
}

static void ppir_block_update_successor(ppir_block *pred,
                                        ppir_block *old_succ,
                                        ppir_block *new_succ,
                                        bool invert_cond)
{
   list_for_each_entry_safe(ppir_node, node, &pred->node_list, list) {
      if (node->op == ppir_op_branch) {
         ppir_branch_node *branch = ppir_node_to_branch(node);
         if (branch->target == old_succ)
            branch->target = new_succ;
         if (invert_cond) {
            assert(ppir_node_get_src_num(node) == 2);
            branch->cond_eq = !branch->cond_eq;
            branch->cond_gt = !branch->cond_gt;
            branch->cond_lt = !branch->cond_lt;
         }
         if (branch->target == NULL) {
            /* We can only remove unconditional branches */
            assert(ppir_node_get_src_num(node) == 0);
            ppir_debug("ppir_block_update_successor: deleting branch %d\n", node->index);
            ppir_node_delete(node);
         }
      }
   }

   for (int i = 0; i < 2; i++) {
      if (pred->successors[i] == old_succ)
        pred->successors[i] = new_succ;
   }

   if (!pred->successors[0] && !pred->successors[1])
      pred->stop = true;
}

static bool ppir_propagate_block_successors(ppir_compiler *comp)
{
   bool progress = false;
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      for (int i = 0; i < 2; i++) {
         if (block->successors[i] && ppir_block_is_empty(block->successors[i])) {
            ppir_block *succ_block = block->successors[i];
            assert(!succ_block->successors[1]);
            ppir_block_update_successor(block, block->successors[i], succ_block->successors[0], false);
            progress = true;
         }
      }
   }
   return progress;
}

static void ppir_renumber_blocks(ppir_compiler *comp)
{
   int index = 0;
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      block->index = index++;
   }
}

/* Removes empty blocks */
static bool ppir_remove_empty_blocks(ppir_compiler *comp)
{
   bool progress = false;
   if (list_is_singular(&comp->block_list))
      return progress;

   do {
      progress = ppir_propagate_block_successors(comp);
   } while (progress);

   list_for_each_entry_safe(ppir_block, block, &comp->block_list, list) {
      if (ppir_block_is_empty(block)) {
         progress = true;
         list_del(&block->list);
         ralloc_free(block);
      }
   }

   if (progress)
      ppir_renumber_blocks(comp);

   return progress;
}

static ppir_node *ppir_block_first_node(ppir_block *block)
{
   return list_first_entry(&block->node_list, ppir_node, list);
}

static bool ppir_node_is_identity_reg_mov(ppir_node *node)
{
   if (node->op != ppir_op_mov)
      return false;

   ppir_src *src = ppir_node_get_src(node, 0);
   if (src->type != ppir_target_register)
      return false;

   if (src->absolute || src->negate)
      return false;

   ppir_dest *dest = ppir_node_get_dest(node);
   if (dest->modifier != ppir_outmod_none)
      return false;

   return ppir_src_swizzle_is_identity(ppir_node_get_src(node, 0));
}

/* Find "stop" block, if it contains a single instruction that is store
 * output, check if all the predecessor blocks are either sequential or branch
 * to this block unconditionally. If it is the case, we can drop the mov and
 * mark source register as output
 *
 * If it is not a single instruction, it can be dropped unconditionally
 *
 * Returns true, if needs to be run again
 */
static bool ppir_opt_store_output(ppir_compiler *comp)
{
   bool progress = false;
   bool single_block = list_is_singular(&comp->block_list);
   ppir_block *stop = NULL;

   /* Can't optimize store output for programs that use discard */
   if (comp->uses_discard)
      return false;

   /* We are assuming that there is only one "stop" block at the moment */
   list_for_each_entry_safe(ppir_block, block, &comp->block_list, list) {
      if (block->stop) {
         assert(!stop);
         stop = block;
      }
   }

   /* No stop block in empty program */
   if (!stop)
      return false;

   ppir_node *out_node = NULL;
   if (list_is_singular(&stop->node_list)) {
      if (single_block)
         return false;
      ppir_node *out_node = ppir_block_first_node(stop);
      if (!ppir_node_is_identity_reg_mov(out_node))
         return false;

      list_for_each_entry(ppir_block, block, &comp->block_list, list) {
         list_for_each_entry(ppir_node, node, &block->node_list, list) {
            if (node->op == ppir_op_branch) {
               ppir_branch_node *branch = ppir_node_to_branch(node);
               if (branch->target == stop && ppir_node_get_src_num(node) != 0)
                  return false;
            }
         }
      }
   } else {
      list_for_each_entry_safe(ppir_node, node, &stop->node_list, list) {
         if (node->is_out && ppir_node_is_identity_reg_mov(node)) {
            out_node = node;
            progress = true;
         }
      }
   }

   if (!out_node)
      return false;

   ppir_reg *dest_reg = ppir_dest_get_reg(ppir_node_get_dest(out_node));
   ppir_reg *src_reg = ppir_src_get_reg(ppir_node_get_src(out_node, 0));

   src_reg->out_type = dest_reg->out_type;
   src_reg->out_reg = true;

   ppir_node_delete(out_node);

   return progress;
}

static bool ppir_node_has_succ(ppir_compiler *comp, ppir_node *node)
{
   ppir_dest *dest = ppir_node_get_dest(node);
   if (!dest)
      return true;

   if (node->is_out)
      return true;

   /* Output registers do not have explicit reads in program */
   if (dest->type == ppir_target_register && dest->reg->out_reg)
      return true;

   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_node, n, &block->node_list, list) {
         for (int i = 0; i < ppir_node_get_src_num(n); i++) {
            ppir_dest *ndest = ppir_node_get_dest(n);
            ppir_src *src = ppir_node_get_src(n, i);
            if (src->type != dest->type)
               continue;
            switch (src->type) {
            case ppir_target_pipeline:
               continue;
            case ppir_target_ssa:
               if (src->node == node)
                  return true;
               break;
            case ppir_target_register:
               if (src->reg->index != dest->reg->index)
                  continue;
               /* Check only components that are written by the node */
               for (int i = 0; i < 4; i++) {
                  if (!(dest->write_mask & (1 << i)))
                     continue;
                  if (ndest) {
                     /* Match only components that are read by n */
                     for (int j = 0; j < 4; j++) {
                        if (!(ndest->write_mask & (1 << j)))
                           continue;
                        if (src->swizzle[j] == i)
                           return true;
                     }
                  } else {
                     for (int j = 0; j < src->reg->num_components; i++) {
                        if (src->swizzle[j] == i)
                           return true;
                     }
                  }
               }
               break;
            }
         }
      }
   }

   return false;
}

/* Must be run after ppir_renumber_blocks() */
static bool ppir_opt_cond_branch(ppir_compiler *comp)
{
   /* Walk blocks in order, find a block with two successors. It's always a
    * block with an conditional branch. One of the successors is a target of
    * the branch, and the other is sequential successor. Let's call them
    * seq_succ and branch_succ. If branch_succ->index == seq_succ->index + 1,
    * and branch_succ contains just an unconditional branch (let's call it
    * branch2), we can inverse condition of branch, update its target to
    * target of branch2 and drop seq_succ block.
    */
   bool progress = false;
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      if (!block->successors[1] || !block->successors[0])
         continue;

      if ((block->index + 1) != block->successors[0]->index)
         continue;

      if ((block->successors[0]->index + 1) != block->successors[1]->index)
         continue;

      if (!list_is_singular(&block->successors[0]->node_list))
         continue;

      ppir_node *node = ppir_block_first_node(block->successors[0]);
      if (node->op != ppir_op_branch)
         continue;

      if (ppir_node_get_src_num(node) != 0)
         continue;

      ppir_block *seq_succ = block->successors[0];
      ppir_block *branch_succ = block->successors[1];
      ppir_branch_node *branch2 = ppir_node_to_branch(node);
      ppir_block_update_successor(block, branch_succ, branch2->target, true);
      ppir_block_update_successor(block, seq_succ, branch_succ, false);
      ppir_debug("ppir_opt_cond_branch: deleting branch %d\n", node->index);
      ppir_node_delete(node);
      ppir_block *old_succ = branch2->target;
      ppir_block_update_successor(seq_succ, old_succ, branch_succ, false);

      progress = true;
   }

   return progress;
}

static bool ppir_opt_propagate_branch_target(ppir_compiler *comp)
{
   bool progress = false;

   /* Check if branch targets a block that has a single unconditional branch
    * and update branch target in this case
    */
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_node, node, &block->node_list, list) {
         if (node->op != ppir_op_branch)
            continue;

         ppir_branch_node *branch = ppir_node_to_branch(node);
         ppir_block *target = branch->target;

         if (!list_is_singular(&target->node_list))
            continue;

         ppir_node *node2 = ppir_block_first_node(target);
         if (node2->op != ppir_op_branch)
            continue;
         if (ppir_node_get_src_num(node2))
            continue;

         ppir_branch_node *branch2 = ppir_node_to_branch(node2);
         ppir_block_update_successor(block, branch->target, branch2->target, false);
         progress = true;
      }
   }

   return progress;
}

/* Dead code elimination */
static bool ppir_dce(ppir_compiler *comp)
{
   /* Delete root nodes with no successors */
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry_safe(ppir_node, node, &block->node_list, list) {
         if (ppir_node_is_root(node) && !ppir_node_has_succ(comp, node)) {
            ppir_debug("DCE: deleting node %d\n", node->index);
            ppir_node_delete(node);
            return true;
         }
      }
   }

   if (list_is_singular(&comp->block_list))
      return false;

   /* Delete unreacheable blocks */
   int num_blocks = list_length(&comp->block_list);
   BITSET_WORD *reachable = rzalloc_array(comp, BITSET_WORD, num_blocks);
   /* Block 0 is entry, it is always reachable */
   BITSET_SET(reachable, 0);
   /* Discard block is always reachable */
   if (comp->discard_block)
      BITSET_SET(reachable, comp->discard_block->index);
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      for (int i = 0; i < 2; i++) {
         if (!block->successors[i])
            continue;
         BITSET_SET(reachable, block->successors[i]->index);
      }
   }

   bool progress = false;
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      if (BITSET_TEST(reachable, block->index))
         continue;
      list_for_each_entry_safe_rev(ppir_node, node, &block->node_list, list) {
         ppir_debug("DCE: deleting node %d\n", node->index);
         ppir_node_delete(node);
         progress = true;
      }
   }

   ralloc_free(reachable);
   return progress;
}


bool ppir_opt_prog(ppir_compiler *comp)
{
   bool progress;

   do {
      progress  = ppir_remove_empty_blocks(comp);
      progress |= ppir_opt_store_output(comp);
   } while (progress);

   do {
      progress  = ppir_opt_propagate_branch_target(comp);
      progress |= ppir_opt_cond_branch(comp);
      progress |= ppir_remove_empty_blocks(comp);
   } while (progress);

   do {
      progress = ppir_remove_empty_blocks(comp);
      progress |= ppir_dce(comp);
   } while (progress);

   return true;
}
