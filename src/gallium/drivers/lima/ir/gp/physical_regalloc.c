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

#include <limits.h>

#include "gpir.h"

/* Linear scan register alloc for physical reg alloc of each
 * load/store node
 */

static void regalloc_print_result(gpir_compiler *comp)
{
   if (!(lima_debug & LIMA_DEBUG_GP))
      return;

   int index = 0;
   printf("======== physical regalloc ========\n");
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      list_for_each_entry(gpir_node, node, &block->node_list, list) {
         if (node->op == gpir_op_load_reg) {
            gpir_load_node *load = gpir_node_to_load(node);
            printf("%03d: load %d use reg %d\n", index, node->index, load->reg->index);
         }
         else if (node->op == gpir_op_store_reg) {
            gpir_store_node *store = gpir_node_to_store(node);
            printf("%03d: store %d use reg %d\n", index, node->index, store->reg->index);
         }
         index++;
      }
      printf("----------------------------\n");
   }
}

bool gpir_physical_regalloc_prog(gpir_compiler *comp)
{
   int index = 0;
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      list_for_each_entry(gpir_node, node, &block->node_list, list) {
         node->preg.index = index++;
      }
   }

   /* calculate each reg liveness interval */
   list_for_each_entry(gpir_reg, reg, &comp->reg_list, list) {
      reg->start = INT_MAX;
      list_for_each_entry(gpir_store_node, store, &reg->defs_list, reg_link) {
         if (store->node.preg.index < reg->start)
            reg->start = store->node.preg.index;
      }

      reg->end = 0;
      list_for_each_entry(gpir_load_node, load, &reg->uses_list, reg_link) {
         if (load->node.preg.index > reg->end)
            reg->end = load->node.preg.index;
      }
   }

   /* sort reg list by start value */
   struct list_head reg_list;
   list_replace(&comp->reg_list, &reg_list);
   list_inithead(&comp->reg_list);
   list_for_each_entry_safe(gpir_reg, reg, &reg_list, list) {
      struct list_head *insert_pos = &comp->reg_list;
      list_for_each_entry(gpir_reg, creg, &comp->reg_list, list) {
         if (creg->start > reg->start) {
            insert_pos = &creg->list;
            break;
         }
      }
      list_del(&reg->list);
      list_addtail(&reg->list, insert_pos);
   }

   /* do linear scan reg alloc */
   gpir_reg *active[GPIR_PHYSICAL_REG_NUM] = {0};
   list_for_each_entry(gpir_reg, reg, &comp->reg_list, list) {
      int i;

      /* if some reg is expired */
      for (i = 0; i < GPIR_PHYSICAL_REG_NUM; i++) {
         if (active[i] && active[i]->end <= reg->start)
            active[i] = NULL;
      }

      /* find a free reg value for this reg */
      for (i = 0; i < GPIR_PHYSICAL_REG_NUM; i++) {
         if (!active[i]) {
            active[i] = reg;
            reg->index = i;
            break;
         }
      }

      /* TODO: support spill to temp memory */
      assert(i < GPIR_PHYSICAL_REG_NUM);
   }

   /* update load/store node info for the real reg */
   list_for_each_entry(gpir_reg, reg, &comp->reg_list, list) {
      list_for_each_entry(gpir_store_node, store, &reg->defs_list, reg_link) {
         store->index = reg->index >> 2;
         store->component = reg->index % 4;
      }

      list_for_each_entry(gpir_load_node, load, &reg->uses_list, reg_link) {
         load->index = reg->index >> 2;
         load->index = reg->index % 4;
      }
   }

   regalloc_print_result(comp);
   return true;
}
