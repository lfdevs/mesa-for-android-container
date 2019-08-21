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

/* Register allocation
 *
 * TODO: This needs to be rewritten when we support multiple basic blocks. We
 * need to do proper liveness analysis, combined with either linear scan,
 * graph coloring, or SSA-based allocation. We should also support spilling to
 * temporaries.
 *
 * For now, this only assigns fake registers to values, used to build the fake
 * dependencies that the scheduler relies on. In the future we should also be
 * assigning actual physreg numbers to load_reg/store_reg nodes.
 */

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
   gpir_node *active[GPIR_VALUE_REG_NUM + GPIR_PHYSICAL_REG_NUM] = {0};
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
      for (i = 0; i < GPIR_VALUE_REG_NUM + GPIR_PHYSICAL_REG_NUM; i++) {
         /* round robin reg select to reduce false dep when schedule */
         int reg = (reg_search_start + i) % (GPIR_VALUE_REG_NUM + GPIR_PHYSICAL_REG_NUM);
         if (!active[reg]) {
            active[reg] = node;
            node->value_reg = reg;
            reg_search_start++;
            break;
         }
      }

      /* TODO: spill */
      assert(i != GPIR_VALUE_REG_NUM + GPIR_PHYSICAL_REG_NUM);
   }
}

static void regalloc_print_result(gpir_compiler *comp)
{
   if (!(lima_debug & LIMA_DEBUG_GP))
      return;

   int index = 0;
   printf("======== regalloc ========\n");
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

bool gpir_regalloc_prog(gpir_compiler *comp)
{
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      regalloc_block(block);
   }

   regalloc_print_result(comp);
   return true;
}
