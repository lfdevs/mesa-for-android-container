/*
 * Copyright (c) 2017 Lima Project
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

#include "ppir.h"


static bool create_new_instr(ppir_block *block, ppir_node *node)
{
   ppir_instr *instr = ppir_instr_create(block);
   if (unlikely(!instr))
      return false;

   if (!ppir_instr_insert_node(instr, node))
      return false;

   return true;
}

static bool insert_to_load_tex(ppir_block *block, ppir_node *load_coords, ppir_node *ldtex)
{
   ppir_dest *dest = ppir_node_get_dest(ldtex);
   ppir_node *move = NULL;

   ppir_load_node *load = ppir_node_to_load(load_coords);
   load->dest.type = ppir_target_pipeline;
   load->dest.pipeline = ppir_pipeline_reg_discard;

   ppir_load_texture_node *load_texture = ppir_node_to_load_texture(ldtex);
   load_texture->src_coords.type = ppir_target_pipeline;
   load_texture->src_coords.pipeline = ppir_pipeline_reg_discard;

   /* Insert load_coords to ldtex instruction */
   if (!ppir_instr_insert_node(ldtex->instr, load_coords))
      return false;

   /* Create move node */
   move = ppir_node_create(block, ppir_op_mov, -1 , 0);
   if (unlikely(!move))
      return false;

   ppir_debug("insert_load_tex: create move %d for %d\n",
              move->index, ldtex->index);

   ppir_alu_node *alu = ppir_node_to_alu(move);
   alu->dest = *dest;

   ppir_node_replace_all_succ(move, ldtex);

   dest->type = ppir_target_pipeline;
   dest->pipeline = ppir_pipeline_reg_sampler;

   alu->num_src = 1;
   ppir_node_target_assign(&alu->src[0], dest);
   for (int i = 0; i < 4; i++)
      alu->src->swizzle[i] = i;

   ppir_node_add_dep(move, ldtex);
   list_addtail(&move->list, &ldtex->list);

   if (!ppir_instr_insert_node(ldtex->instr, move))
      return false;

   return true;
}

static bool insert_to_each_succ_instr(ppir_block *block, ppir_node *node)
{
   ppir_dest *dest = ppir_node_get_dest(node);
   assert(dest->type == ppir_target_ssa);

   ppir_node *move = NULL;

   ppir_node_foreach_succ_safe(node, dep) {
      ppir_node *succ = dep->succ;
      assert(succ->type == ppir_node_type_alu);

      if (!ppir_instr_insert_node(succ->instr, node)) {
         /* create a move node to insert for failed node */
         if (!move) {
            move = ppir_node_create(block, ppir_op_mov, -1, 0);
            if (unlikely(!move))
               return false;

            ppir_debug("node_to_instr create move %d for %d\n",
                       move->index, node->index);

            ppir_alu_node *alu = ppir_node_to_alu(move);
            alu->dest = *dest;
            alu->num_src = 1;
            ppir_node_target_assign(alu->src, dest);
            for (int i = 0; i < 4; i++)
               alu->src->swizzle[i] = i;
         }

         ppir_node_replace_pred(dep, move);
         ppir_node_replace_child(succ, node, move);
      }
   }

   if (move) {
      if (!create_new_instr(block, move))
         return false;

      MAYBE_UNUSED bool insert_result =
         ppir_instr_insert_node(move->instr, node);
      assert(insert_result);

      ppir_node_add_dep(move, node);
      list_addtail(&move->list, &node->list);
   }

   /* dupliacte node for each successor */

   bool first = true;
   struct list_head dup_list;
   list_inithead(&dup_list);

   ppir_node_foreach_succ_safe(node, dep) {
      ppir_node *succ = dep->succ;

      if (first) {
         first = false;
         node->instr = succ->instr;
         continue;
      }

      if (succ->instr == node->instr)
         continue;

      list_for_each_entry(ppir_node, dup, &dup_list, list) {
         if (succ->instr == dup->instr) {
            ppir_node_replace_pred(dep, dup);
            continue;
         }
      }

      ppir_node *dup = ppir_node_create(block, node->op, -1, 0);
      if (unlikely(!dup))
         return false;
      list_addtail(&dup->list, &dup_list);

      ppir_debug("node_to_instr duplicate %s %d from %d\n",
                 ppir_op_infos[dup->op].name, dup->index, node->index);

      ppir_instr *instr = succ->instr;
      dup->instr = instr;
      dup->instr_pos = node->instr_pos;
      ppir_node_replace_pred(dep, dup);

      if ((node->op == ppir_op_load_uniform) || (node->op == ppir_op_load_temp)) {
         ppir_load_node *load = ppir_node_to_load(node);
         ppir_load_node *dup_load = ppir_node_to_load(dup);
         dup_load->dest = load->dest;
         dup_load->index = load->index;
         dup_load->num_components = load->num_components;
         instr->slots[node->instr_pos] = dup;
      }
   }

   list_splicetail(&dup_list, &node->list);

   return true;
}

static bool ppir_do_node_to_instr(ppir_block *block, ppir_node *node)
{
   switch (node->type) {
   case ppir_node_type_alu:
   {
      /* merge pred mul and succ add in the same instr can save a reg
       * by using pipeline reg ^vmul/^fmul */
      ppir_alu_node *alu = ppir_node_to_alu(node);
      if (alu->dest.type == ppir_target_ssa &&
          ppir_node_has_single_succ(node)) {
         ppir_node *succ = ppir_node_first_succ(node);
         if (succ->instr_pos == PPIR_INSTR_SLOT_ALU_VEC_ADD) {
            node->instr_pos = PPIR_INSTR_SLOT_ALU_VEC_MUL;
            /* select instr's condition must be inserted to fmul slot */
            if (succ->op == ppir_op_select &&
                ppir_node_first_pred(succ) == node) {
               assert(alu->dest.ssa.num_components == 1);
               node->instr_pos = PPIR_INSTR_SLOT_ALU_SCL_MUL;
            }
            ppir_instr_insert_mul_node(succ, node);
         }
         else if (succ->instr_pos == PPIR_INSTR_SLOT_ALU_SCL_ADD &&
                  alu->dest.ssa.num_components == 1) {
            node->instr_pos = PPIR_INSTR_SLOT_ALU_SCL_MUL;
            ppir_instr_insert_mul_node(succ, node);
         }
      }

      /* can't inserted to any existing instr, create one */
      if (!node->instr && !create_new_instr(block, node))
         return false;

      break;
   }
   case ppir_node_type_load:
      if ((node->op == ppir_op_load_uniform) || (node->op == ppir_op_load_temp)) {
         /* merge pred load_uniform into succ instr can save a reg
          * by using pipeline reg */
         if (!insert_to_each_succ_instr(block, node))
            return false;

         ppir_load_node *load = ppir_node_to_load(node);
         load->dest.type = ppir_target_pipeline;
         load->dest.pipeline = ppir_pipeline_reg_uniform;
      }
      else if (node->op == ppir_op_load_temp) {
         /* merge pred load_temp into succ instr can save a reg
          * by using pipeline reg */
         if (!insert_to_each_succ_instr(block, node))
            return false;

         ppir_load_node *load = ppir_node_to_load(node);
         load->dest.type = ppir_target_pipeline;
         load->dest.pipeline = ppir_pipeline_reg_uniform;
      }
      else if (node->op == ppir_op_load_varying ||
               node->op == ppir_op_load_fragcoord) {
         /* delay the load varying dup to scheduler */
         if (!create_new_instr(block, node))
            return false;
      }
      else if (node->op == ppir_op_load_coords) {
         ppir_node *ldtex = ppir_node_first_succ(node);
         if (!insert_to_load_tex(block, node, ldtex))
            return false;
      }
      else {
         /* not supported yet */
         assert(0);
         return false;
      }
      break;
   case ppir_node_type_load_texture:
      if (!create_new_instr(block, node))
         return false;
      break;
   case ppir_node_type_const:
      if (!insert_to_each_succ_instr(block, node))
         return false;
      break;
   case ppir_node_type_store:
   {
      if (node->op == ppir_op_store_temp) {
         if (!create_new_instr(block, node))
            return false;
         break;
      }

      /* Only the store color node should appear here.
       * Currently we always insert a move node as the end instr.
       * But it should only be done when:
       *   1. store a const node
       *   2. store a load node
       *   3. store a reg assigned in another block like loop/if
       */

      assert(node->op == ppir_op_store_color);

      ppir_node *move = ppir_node_create(block, ppir_op_mov, -1, 0);
      if (unlikely(!move))
         return false;

      ppir_debug("node_to_instr create move %d from store %d\n",
                 move->index, node->index);

      ppir_node_foreach_pred_safe(node, dep) {
         ppir_node *pred = dep->pred;
         /* we can't do this in this function except here as this
          * store is the root of this recursion */
         ppir_node_remove_dep(dep);
         ppir_node_add_dep(move, pred);
      }

      ppir_node_add_dep(node, move);
      list_addtail(&move->list, &node->list);

      ppir_alu_node *alu = ppir_node_to_alu(move);
      ppir_store_node *store = ppir_node_to_store(node);
      alu->src[0] = store->src;
      alu->num_src = 1;

      alu->dest.type = ppir_target_ssa;
      alu->dest.ssa.num_components = 4;
      alu->dest.ssa.live_in = INT_MAX;
      alu->dest.ssa.live_out = 0;
      alu->dest.write_mask = 0xf;

      store->src.type = ppir_target_ssa;
      store->src.ssa = &alu->dest.ssa;

      if (!create_new_instr(block, move))
         return false;

      move->instr->is_end = true;
      node->instr = move->instr;

      /* use move for the following recursion */
      node = move;
      break;
   }
   default:
      return false;
   }

   /* we have to make sure the dep not be destroyed (due to
    * succ change) in ppir_do_node_to_instr, otherwise we can't
    * do recursion like this */
   ppir_node_foreach_pred(node, dep) {
      ppir_node *pred = dep->pred;
      bool ready = true;

      /* pred may already be processed by the previous pred
       * (this pred may be both node and previous pred's child) */
      if (pred->instr)
         continue;

      /* insert pred only when all its successors have been inserted */
      ppir_node_foreach_succ(pred, dep) {
         ppir_node *succ = dep->succ;
         if (!succ->instr) {
            ready = false;
            break;
         }
      }

      if (ready) {
         if (!ppir_do_node_to_instr(block, pred))
            return false;
      }
   }

   return true;
}

static bool ppir_create_instr_from_node(ppir_compiler *comp)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_node, node, &block->node_list, list) {
         if (ppir_node_is_root(node)) {
            if (!ppir_do_node_to_instr(block, node))
               return false;
         }
      }
   }

   return true;
}

static void ppir_build_instr_dependency(ppir_compiler *comp)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++) {
            ppir_node *node = instr->slots[i];
            if (node) {
               ppir_node_foreach_pred(node, dep) {
                  ppir_node *pred = dep->pred;
                  if (pred->instr && pred->instr != instr)
                     ppir_instr_add_dep(instr, pred->instr);
               }
            }
         }
      }
   }
}

bool ppir_node_to_instr(ppir_compiler *comp)
{
   if (!ppir_create_instr_from_node(comp))
      return false;
   ppir_instr_print_list(comp);

   ppir_build_instr_dependency(comp);
   ppir_instr_print_dep(comp);

   return true;
}
