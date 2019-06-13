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

#include "gpir.h"

/* Linear scan register alloc for value reg alloc of each node */

static int regalloc_spill_active_node(gpir_node *active[])
{
   gpir_node *spill = NULL;
   for (int i = 0; i < GPIR_VALUE_REG_NUM; i++) {
      if (gpir_op_infos[active[i]->op].spillless)
         continue;

      /* spill farest node */
      if (!spill ||
          spill->vreg.last->vreg.index < active[i]->vreg.last->vreg.index) {
         spill = active[i];
      }
   }

   assert(spill);
   gpir_debug("value regalloc spill node %d for value reg %d\n",
              spill->index, spill->value_reg);

   /* create store node for spilled node */
   gpir_store_node *store = gpir_node_create(spill->block, gpir_op_store_reg);
   store->child = spill;
   /* no need to calculate other vreg values because store & spill won't
    * be used in the following schedule again */
   store->node.value_reg = spill->value_reg;
   list_addtail(&store->node.list, &spill->list);

   gpir_reg *reg = gpir_create_reg(spill->block->comp);
   store->reg = reg;
   list_addtail(&store->reg_link, &reg->defs_list);

   gpir_node_foreach_succ_safe(spill, dep) {
      gpir_node *succ = dep->succ;
      gpir_load_node *load = gpir_node_create(succ->block, gpir_op_load_reg);
      gpir_node_replace_pred(dep, &load->node);
      gpir_node_replace_child(succ, spill, &load->node);
      list_addtail(&load->node.list, &succ->list);

      /* only valid for succ already scheduled, succ not scheduled will
       * re-write this value */
      load->node.value_reg = spill->value_reg;
      load->node.vreg.index =
         (list_first_entry(&load->node.list, gpir_node, list)->vreg.index +
          list_last_entry(&load->node.list, gpir_node, list)->vreg.index) / 2.0f;
      load->node.vreg.last = succ;

      load->reg = reg;
      list_addtail(&load->reg_link, &reg->uses_list);
   }

   gpir_node_add_dep(&store->node, spill, GPIR_DEP_INPUT);
   return spill->value_reg;
}

static void regalloc_block(gpir_block *block)
{
   /* build each node sequence index in the block node list */
   int index = 0;
   list_for_each_entry(gpir_node, node, &block->node_list, list) {
      node->vreg.index = index++;
   }

   /* find the last successor of each node by the sequence index */
   list_for_each_entry(gpir_node, node, &block->node_list, list) {
      node->vreg.last = NULL;
      gpir_node_foreach_succ(node, dep) {
         gpir_node *succ = dep->succ;
         if (!node->vreg.last || node->vreg.last->vreg.index < succ->vreg.index)
            node->vreg.last = succ;
      }
   }

   /* do linear scan regalloc */
   int reg_search_start = 0;
   gpir_node *active[GPIR_VALUE_REG_NUM] = {0};
   list_for_each_entry(gpir_node, node, &block->node_list, list) {
      /* if some reg is expired */
      gpir_node_foreach_pred(node, dep) {
         gpir_node *pred = dep->pred;
         if (pred->vreg.last == node)
            active[pred->value_reg] = NULL;
      }

      /* no need to alloc value reg for root node */
      if (gpir_node_is_root(node)) {
         node->value_reg = -1;
         continue;
      }

      /* find a free reg for this node */
      int i;
      for (i = 0; i < GPIR_VALUE_REG_NUM; i++) {
         /* round robin reg select to reduce false dep when schedule */
         int reg = (reg_search_start + i) % GPIR_VALUE_REG_NUM;
         if (!active[reg]) {
            active[reg] = node;
            node->value_reg = reg;
            reg_search_start++;
            break;
         }
      }

      /* need spill */
      if (i == GPIR_VALUE_REG_NUM) {
         int spilled_reg = regalloc_spill_active_node(active);
         active[spilled_reg] = node;
         node->value_reg = spilled_reg;
         gpir_debug("value regalloc node %d reuse reg %d\n",
                    node->index, spilled_reg);
      }
   }
}

static void regalloc_print_result(gpir_compiler *comp)
{
   if (!(lima_debug & LIMA_DEBUG_GP))
      return;

   int index = 0;
   printf("======== value regalloc ========\n");
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      list_for_each_entry(gpir_node, node, &block->node_list, list) {
         printf("%03d: %d/%d %s ", index++, node->index, node->value_reg,
                gpir_op_infos[node->op].name);
         gpir_node_foreach_pred(node, dep) {
            gpir_node *pred = dep->pred;
            printf(" %d/%d", pred->index, pred->value_reg);
         }
         printf("\n");
      }
      printf("----------------------------\n");
   }
}

bool gpir_value_regalloc_prog(gpir_compiler *comp)
{
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      regalloc_block(block);
   }

   regalloc_print_result(comp);
   return true;
}
