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

#include "util/u_math.h"
#include "util/ralloc.h"
#include "util/bitscan.h"

#include "ppir.h"

const ppir_op_info ppir_op_infos[] = {
   [ppir_op_mov] = {
      .name = "mov",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_SCL_MUL,
         PPIR_INSTR_SLOT_ALU_VEC_ADD, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_mul] = {
      .name = "mul",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_add] = {
      .name = "add",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_dot2] = {
      .name = "dot2",
   },
   [ppir_op_dot3] = {
      .name = "dot3",
   },
   [ppir_op_dot4] = {
      .name = "dot4",
   },
   [ppir_op_sum3] = {
      .name = "sum3",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_VEC_ADD, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_sum4] = {
      .name = "sum4",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_VEC_ADD, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_rsqrt] = {
      .name = "rsqrt",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_COMBINE, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_log2] = {
      .name = "log2",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_COMBINE, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_exp2] = {
      .name = "exp2",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_COMBINE, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_sqrt] = {
      .name = "sqrt",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_COMBINE, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_sin] = {
      .name = "sin",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_COMBINE, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_cos] = {
      .name = "cos",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_COMBINE, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_max] = {
      .name = "max",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_SCL_MUL,
         PPIR_INSTR_SLOT_ALU_VEC_ADD, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_min] = {
      .name = "min",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_SCL_MUL,
         PPIR_INSTR_SLOT_ALU_VEC_ADD, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_floor] = {
      .name = "floor",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_ceil] = {
      .name = "ceil",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_fract] = {
      .name = "fract",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_and] = {
      .name = "and",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_or] = {
      .name = "or",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_xor] = {
      .name = "xor",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_not] = {
      .name = "not",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_VEC_MUL,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_lt] = {
      .name = "lt",
   },
   [ppir_op_le] = {
      .name = "le",
   },
   [ppir_op_gt] = {
      .name = "gt",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_SCL_ADD,
         PPIR_INSTR_SLOT_ALU_VEC_MUL, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_ge] = {
      .name = "ge",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_SCL_ADD,
         PPIR_INSTR_SLOT_ALU_VEC_MUL, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_eq] = {
      .name = "eq",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_SCL_ADD,
         PPIR_INSTR_SLOT_ALU_VEC_MUL, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_ne] = {
      .name = "ne",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_MUL, PPIR_INSTR_SLOT_ALU_SCL_ADD,
         PPIR_INSTR_SLOT_ALU_VEC_MUL, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_select] = {
      .name = "select",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_SCL_ADD, PPIR_INSTR_SLOT_ALU_VEC_ADD,
         PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_rcp] = {
      .name = "rcp",
      .slots = (int []) {
         PPIR_INSTR_SLOT_ALU_COMBINE, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_load_varying] = {
      .name = "ld_var",
      .type = ppir_node_type_load,
      .slots = (int []) {
         PPIR_INSTR_SLOT_VARYING, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_load_coords] = {
      .name = "ld_coords",
      .type = ppir_node_type_load,
      .slots = (int []) {
         PPIR_INSTR_SLOT_VARYING, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_load_fragcoord] = {
      .name = "ld_fragcoord",
      .type = ppir_node_type_load,
      .slots = (int []) {
         PPIR_INSTR_SLOT_VARYING, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_load_uniform] = {
      .name = "ld_uni",
      .type = ppir_node_type_load,
      .slots = (int []) {
         PPIR_INSTR_SLOT_UNIFORM, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_load_texture] = {
      .name = "ld_tex",
      .type = ppir_node_type_load_texture,
      .slots = (int []) {
         PPIR_INSTR_SLOT_TEXLD, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_load_temp] = {
      .name = "ld_temp",
      .type = ppir_node_type_load,
      .slots = (int []) {
         PPIR_INSTR_SLOT_UNIFORM, PPIR_INSTR_SLOT_END
      },
   },
   [ppir_op_const] = {
      .name = "const",
      .type = ppir_node_type_const,
   },
   [ppir_op_store_color] = {
      .name = "st_col",
      .type = ppir_node_type_store,
   },
   [ppir_op_store_temp] = {
      .name = "st_temp",
      .type = ppir_node_type_store,
      .slots = (int []) {
         PPIR_INSTR_SLOT_STORE_TEMP, PPIR_INSTR_SLOT_END
      },
   },
};

void *ppir_node_create(ppir_block *block, ppir_op op, int index, unsigned mask)
{
   ppir_compiler *comp = block->comp;
   static const int node_size[] = {
      [ppir_node_type_alu] = sizeof(ppir_alu_node),
      [ppir_node_type_const] = sizeof(ppir_const_node),
      [ppir_node_type_load] = sizeof(ppir_load_node),
      [ppir_node_type_store] = sizeof(ppir_store_node),
      [ppir_node_type_load_texture] = sizeof(ppir_load_texture_node),
   };

   ppir_node_type type = ppir_op_infos[op].type;
   int size = node_size[type];
   ppir_node *node = rzalloc_size(block, size);
   if (!node)
      return NULL;

   list_inithead(&node->succ_list);
   list_inithead(&node->pred_list);

   if (index >= 0) {
      if (mask) {
         /* reg has 4 slots for each componemt write node */
         while (mask)
            comp->var_nodes[(index << 2) + comp->reg_base + u_bit_scan(&mask)] = node;
         snprintf(node->name, sizeof(node->name), "reg%d", index);
      } else {
         comp->var_nodes[index] = node;
         snprintf(node->name, sizeof(node->name), "ssa%d", index);
      }
   }
   else
      snprintf(node->name, sizeof(node->name), "new");

   node->op = op;
   node->type = type;
   node->index = comp->cur_index++;
   node->block = block;

   return node;
}

void ppir_node_add_dep(ppir_node *succ, ppir_node *pred)
{
   /* don't add dep for two nodes from different block */
   if (succ->block != pred->block)
      return;

   /* don't add duplicated dep */
   ppir_node_foreach_pred(succ, dep) {
      if (dep->pred == pred)
         return;
   }

   ppir_dep *dep = ralloc(succ, ppir_dep);
   dep->pred = pred;
   dep->succ = succ;
   list_addtail(&dep->pred_link, &succ->pred_list);
   list_addtail(&dep->succ_link, &pred->succ_list);
}

void ppir_node_remove_dep(ppir_dep *dep)
{
   list_del(&dep->succ_link);
   list_del(&dep->pred_link);
   ralloc_free(dep);
}

static void _ppir_node_replace_child(ppir_src *src, ppir_node *old_child, ppir_node *new_child)
{
   ppir_dest *od = ppir_node_get_dest(old_child);
   if (ppir_node_target_equal(src, od)) {
      ppir_dest *nd = ppir_node_get_dest(new_child);
      ppir_node_target_assign(src, nd);
   }
}

void ppir_node_replace_child(ppir_node *parent, ppir_node *old_child, ppir_node *new_child)
{
   if (parent->type == ppir_node_type_alu) {
      ppir_alu_node *alu = ppir_node_to_alu(parent);
      for (int i = 0; i < alu->num_src; i++)
         _ppir_node_replace_child(alu->src + i, old_child, new_child);
   }
   else if (parent->type == ppir_node_type_store) {
      ppir_store_node *store = ppir_node_to_store(parent);
      _ppir_node_replace_child(&store->src, old_child, new_child);
   }
}

void ppir_node_replace_pred(ppir_dep *dep, ppir_node *new_pred)
{
   list_del(&dep->succ_link);
   dep->pred = new_pred;
   list_addtail(&dep->succ_link, &new_pred->succ_list);
}

void ppir_node_replace_all_succ(ppir_node *dst, ppir_node *src)
{
   ppir_node_foreach_succ_safe(src, dep) {
      ppir_node_replace_pred(dep, dst);
      ppir_node_replace_child(dep->succ, src, dst);
   }
}

void ppir_node_delete(ppir_node *node)
{
   ppir_node_foreach_succ_safe(node, dep)
      ppir_node_remove_dep(dep);

   ppir_node_foreach_pred_safe(node, dep)
      ppir_node_remove_dep(dep);

   list_del(&node->list);
   ralloc_free(node);
}

static void ppir_node_print_node(ppir_node *node, int space)
{
   for (int i = 0; i < space; i++)
      printf(" ");
   printf("%s%s %d %s\n", node->printed && !ppir_node_is_leaf(node) ? "+" : "",
          ppir_op_infos[node->op].name, node->index, node->name);

   if (!node->printed) {
      ppir_node_foreach_pred(node, dep) {
         ppir_node *pred = dep->pred;
         ppir_node_print_node(pred, space + 2);
      }

      node->printed = true;
   }
}

void ppir_node_print_prog(ppir_compiler *comp)
{
   if (!(lima_debug & LIMA_DEBUG_PP))
      return;

   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_node, node, &block->node_list, list) {
         node->printed = false;
      }
   }

   printf("========prog========\n");
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      printf("-------block------\n");
      list_for_each_entry(ppir_node, node, &block->node_list, list) {
         if (ppir_node_is_root(node))
            ppir_node_print_node(node, 0);
      }
   }
   printf("====================\n");
}
