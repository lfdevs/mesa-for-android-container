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

#include "util/bitscan.h"
#include "util/ralloc.h"

#include "ppir.h"

static bool ppir_lower_const(ppir_block *block, ppir_node *node)
{
   if (ppir_node_is_root(node)) {
      ppir_node_delete(node);
      return true;
   }

   assert(ppir_node_has_single_succ(node));

   ppir_node *succ = ppir_node_first_succ(node);
   ppir_src *src = ppir_node_get_src_for_pred(succ, node);
   ppir_dest *dest = ppir_node_get_dest(node);
   assert(src != NULL);

   switch (succ->type) {
   case ppir_node_type_alu:
   case ppir_node_type_branch:
      /* ALU and branch can consume consts directly */
      dest->type = src->type = ppir_target_pipeline;
      /* Reg will be updated in node_to_instr later */
      dest->pipeline = src->pipeline = ppir_pipeline_reg_const0;
      return true;
   default:
      /* Create a move for everyone else */
      break;
   }

   ppir_node *move = ppir_node_insert_mov(node);
   if (unlikely(!move))
      return false;

   ppir_debug("lower const create move %d for %d\n",
              move->index, node->index);

   /* Need to be careful with changing src/dst type here:
    * it has to be done *after* successors have their children
    * replaced, otherwise ppir_node_replace_child() won't find
    * matching src/dst and as result won't work
    */
   ppir_src *mov_src = ppir_node_get_src(move, 0);
   mov_src->type = dest->type = ppir_target_pipeline;
   mov_src->pipeline = dest->pipeline = ppir_pipeline_reg_const0;

   return true;
}

static bool ppir_lower_swap_args(ppir_block *block, ppir_node *node)
{
   /* swapped op must be the next op */
   node->op++;

   assert(node->type == ppir_node_type_alu);
   ppir_alu_node *alu = ppir_node_to_alu(node);
   assert(alu->num_src == 2);

   ppir_src tmp = alu->src[0];
   alu->src[0] = alu->src[1];
   alu->src[1] = tmp;
   return true;
}

static bool ppir_lower_load(ppir_block *block, ppir_node *node)
{
   ppir_dest *dest = ppir_node_get_dest(node);
   if (ppir_node_is_root(node) && dest->type == ppir_target_ssa) {
      ppir_node_delete(node);
      return true;
   }

   /* load can have multiple successors in case if we duplicated load node
    * that has load node in source
    */
   if ((ppir_node_has_single_src_succ(node) || ppir_node_is_root(node)) &&
      dest->type != ppir_target_register) {
      ppir_node *succ = ppir_node_first_succ(node);
      switch (succ->type) {
      case ppir_node_type_alu:
      case ppir_node_type_branch: {
         ppir_src *src = ppir_node_get_src_for_pred(succ, node);
         /* Can consume uniforms directly */
         src->type = dest->type = ppir_target_pipeline;
         src->pipeline = dest->pipeline = ppir_pipeline_reg_uniform;
         return true;
      }
      default:
         /* Create mov for everyone else */
         break;
      }
   }

   ppir_node *move = ppir_node_insert_mov(node);
   if (unlikely(!move))
      return false;

   ppir_src *mov_src = ppir_node_get_src(move, 0);
   mov_src->type = dest->type = ppir_target_pipeline;
   mov_src->pipeline = dest->pipeline = ppir_pipeline_reg_uniform;

   return true;
}

static bool ppir_lower_ddxy(ppir_block *block, ppir_node *node)
{
   assert(node->type == ppir_node_type_alu);
   ppir_alu_node *alu = ppir_node_to_alu(node);

   alu->src[1] = alu->src[0];
   if (node->op == ppir_op_ddx)
      alu->src[1].negate = !alu->src[1].negate;
   else if (node->op == ppir_op_ddy)
      alu->src[0].negate = !alu->src[0].negate;
   else
      assert(0);

   alu->num_src = 2;

   return true;
}

static bool ppir_lower_texture(ppir_block *block, ppir_node *node)
{
   ppir_load_texture_node *load_tex = ppir_node_to_load_texture(node);
   ppir_dest *dest = ppir_node_get_dest(node);

   if (ppir_node_is_root(node) && dest->type == ppir_target_ssa) {
      ppir_node_delete(node);
      return true;
   }

   ppir_node *src_coords = ppir_node_get_src(node, 0)->node;
   ppir_load_node *load = NULL;
   if (src_coords && ppir_node_has_single_src_succ(src_coords) &&
       (src_coords->op == ppir_op_load_coords))
      load = ppir_node_to_load(src_coords);
   else {
      /* Create load_coords node */
      load = ppir_node_create(block, ppir_op_load_coords, -1, 0);
      if (!load)
         return false;
      list_addtail(&load->node.list, &node->list);

      load->src = load_tex->src_coords;
      load->num_src = 1;

      ppir_debug("%s create load_coords node %d for %d\n",
                 __FUNCTION__, load->node.index, node->index);

      ppir_node_foreach_pred_safe(node, dep) {
         ppir_node *pred = dep->pred;
         ppir_node_remove_dep(dep);
         ppir_node_add_dep(&load->node, pred, ppir_dep_src);
      }
      ppir_node_add_dep(node, &load->node, ppir_dep_src);
   }

   assert(load);
   load_tex->src_coords.type = load->dest.type = ppir_target_pipeline;
   load_tex->src_coords.pipeline = load->dest.pipeline = ppir_pipeline_reg_discard;

   if (ppir_node_has_single_src_succ(node)) {
      ppir_node *succ = ppir_node_first_succ(node);
      switch (succ->type) {
      case ppir_node_type_alu:
      case ppir_node_type_branch: {
         for (int i = 0; i < ppir_node_get_src_num(succ); i++) {
            ppir_src *src = ppir_node_get_src(succ, i);
            if (src->node == node) {
               /* Can consume samplers directly */
               src->type = dest->type = ppir_target_pipeline;
               src->pipeline = dest->pipeline = ppir_pipeline_reg_sampler;
            }
         }
         return true;
      }
      default:
         /* Create mov for everyone else */
         break;
      }
   }

   /* Create move node */
   ppir_node *move = ppir_node_insert_mov(node);
   if (unlikely(!move))
      return false;

   ppir_debug("lower texture create move %d for %d\n",
              move->index, node->index);

   ppir_src *mov_src=  ppir_node_get_src(move, 0);
   mov_src->type = dest->type = ppir_target_pipeline;
   mov_src->pipeline = dest->pipeline = ppir_pipeline_reg_sampler;

   return true;
}

/* insert a move as the select condition to make sure it can
 * be inserted to select instr float mul slot
 */
static bool ppir_lower_select(ppir_block *block, ppir_node *node)
{
   ppir_alu_node *alu = ppir_node_to_alu(node);

   ppir_node *move = ppir_node_create(block, ppir_op_sel_cond, -1, 0);
   if (!move)
      return false;
   list_addtail(&move->list, &node->list);

   ppir_alu_node *move_alu = ppir_node_to_alu(move);
   ppir_src *move_src = move_alu->src, *src = alu->src;
   move_src->type = src->type;
   move_src->ssa = src->ssa;
   move_src->swizzle[0] = src->swizzle[0];
   move_alu->num_src = 1;

   ppir_dest *move_dest = &move_alu->dest;
   move_dest->type = ppir_target_pipeline;
   move_dest->pipeline = ppir_pipeline_reg_fmul;
   move_dest->write_mask = 1;

   ppir_node *pred = alu->src[0].node;
   ppir_dep *dep = ppir_dep_for_pred(node, pred);
   if (dep)
      ppir_node_replace_pred(dep, move);
   else
      ppir_node_add_dep(node, move, ppir_dep_src);

   /* pred can be a register */
   if (pred)
      ppir_node_add_dep(move, pred, ppir_dep_src);

   src->swizzle[0] = 0;
   ppir_node_target_assign(alu->src, move);

   return true;
}

static bool ppir_lower_trunc(ppir_block *block, ppir_node *node)
{
   /* Turn it into a mov with a round to integer output modifier */
   ppir_alu_node *alu = ppir_node_to_alu(node);
   ppir_dest *move_dest = &alu->dest;
   move_dest->modifier = ppir_outmod_round;
   node->op = ppir_op_mov;

   return true;
}

static bool ppir_lower_abs(ppir_block *block, ppir_node *node)
{
   /* Turn it into a mov and set the absolute modifier */
   ppir_alu_node *alu = ppir_node_to_alu(node);

   assert(alu->num_src == 1);

   alu->src[0].absolute = true;
   alu->src[0].negate = false;
   node->op = ppir_op_mov;

   return true;
}

static bool ppir_lower_neg(ppir_block *block, ppir_node *node)
{
   /* Turn it into a mov and set the negate modifier */
   ppir_alu_node *alu = ppir_node_to_alu(node);

   assert(alu->num_src == 1);

   alu->src[0].negate = !alu->src[0].negate;
   node->op = ppir_op_mov;

   return true;
}

static bool ppir_lower_sat(ppir_block *block, ppir_node *node)
{
   /* Turn it into a mov with the saturate output modifier */
   ppir_alu_node *alu = ppir_node_to_alu(node);

   assert(alu->num_src == 1);

   ppir_dest *move_dest = &alu->dest;
   move_dest->modifier = ppir_outmod_clamp_fraction;
   node->op = ppir_op_mov;

   return true;
}

static bool ppir_lower_branch(ppir_block *block, ppir_node *node)
{
   ppir_branch_node *branch = ppir_node_to_branch(node);

   /* Unconditional branch */
   if (branch->num_src == 0)
      return true;

   ppir_const_node *zero = ppir_node_create(block, ppir_op_const, -1, 0);

   if (!zero)
      return false;

   zero->constant.value[0].f = 0;
   zero->constant.num = 1;
   zero->dest.type = ppir_target_pipeline;
   zero->dest.pipeline = ppir_pipeline_reg_const0;
   zero->dest.ssa.num_components = 1;
   zero->dest.ssa.live_in = INT_MAX;
   zero->dest.ssa.live_out = 0;
   zero->dest.write_mask = 0x01;

   /* For now we're just comparing branch condition with 0,
    * in future we should look whether it's possible to move
    * comparision node into branch itself and use current
    * way as a fallback for complex conditions.
    */
   ppir_node_target_assign(&branch->src[1], &zero->node);

   if (branch->negate)
      branch->cond_eq = true;
   else {
      branch->cond_gt = true;
      branch->cond_lt = true;
   }

   branch->num_src = 2;

   ppir_node_add_dep(&branch->node, &zero->node, ppir_dep_src);
   list_addtail(&zero->node.list, &node->list);

   return true;
}

static bool (*ppir_lower_funcs[ppir_op_num])(ppir_block *, ppir_node *) = {
   [ppir_op_abs] = ppir_lower_abs,
   [ppir_op_neg] = ppir_lower_neg,
   [ppir_op_const] = ppir_lower_const,
   [ppir_op_ddx] = ppir_lower_ddxy,
   [ppir_op_ddy] = ppir_lower_ddxy,
   [ppir_op_lt] = ppir_lower_swap_args,
   [ppir_op_le] = ppir_lower_swap_args,
   [ppir_op_load_texture] = ppir_lower_texture,
   [ppir_op_select] = ppir_lower_select,
   [ppir_op_trunc] = ppir_lower_trunc,
   [ppir_op_sat] = ppir_lower_sat,
   [ppir_op_branch] = ppir_lower_branch,
   [ppir_op_load_uniform] = ppir_lower_load,
   [ppir_op_load_temp] = ppir_lower_load,
};

bool ppir_lower_prog(ppir_compiler *comp)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry_safe(ppir_node, node, &block->node_list, list) {
         if (ppir_lower_funcs[node->op] &&
             !ppir_lower_funcs[node->op](block, node))
            return false;
      }
   }

   return true;
}
