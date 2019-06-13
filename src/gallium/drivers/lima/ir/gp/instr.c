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

#include <string.h>

#include "util/ralloc.h"

#include "gpir.h"

gpir_instr *gpir_instr_create(gpir_block *block)
{
   gpir_instr *instr = rzalloc(block, gpir_instr);
   if (unlikely(!instr))
      return NULL;

   instr->index = block->sched.instr_index++;
   instr->alu_num_slot_free = 6;

   list_add(&instr->list, &block->instr_list);
   return instr;
}

static gpir_node *gpir_instr_get_the_other_acc_node(gpir_instr *instr, int slot)
{
   if (slot == GPIR_INSTR_SLOT_ADD0)
      return instr->slots[GPIR_INSTR_SLOT_ADD1];
   else if (slot == GPIR_INSTR_SLOT_ADD1)
      return instr->slots[GPIR_INSTR_SLOT_ADD0];

   return NULL;
}

static bool gpir_instr_check_acc_same_op(gpir_instr *instr, gpir_node *node, int slot)
{
   /* two ACC slots must share the same op code */
   gpir_node *acc_node = gpir_instr_get_the_other_acc_node(instr, slot);

   /* spill move case may get acc_node == node */
   if (acc_node && acc_node != node &&
       !gpir_codegen_acc_same_op(node->op, acc_node->op))
      return false;

   return true;
}

static int gpir_instr_get_consume_slot(gpir_instr *instr, gpir_node *node)
{
   if (gpir_op_infos[node->op].may_consume_two_slots) {
      gpir_node *acc_node = gpir_instr_get_the_other_acc_node(instr, node->sched.pos);
      if (acc_node)
         /* at this point node must have the same acc op with acc_node,
          * so it just consumes the extra slot acc_node consumed */
         return 0;
      else
         return 2;
   }
   else
      return 1;
}

static bool gpir_instr_insert_alu_check(gpir_instr *instr, gpir_node *node)
{
   if (!gpir_instr_check_acc_same_op(instr, node, node->sched.pos))
      return false;

   int consume_slot = gpir_instr_get_consume_slot(instr, node);

   /* check if this node is child of one store node.
    * complex1 won't be any of this instr's store node's child,
    * because it has two instr latency before store can use it.
    */
   for (int i = GPIR_INSTR_SLOT_STORE0; i <= GPIR_INSTR_SLOT_STORE3; i++) {
      gpir_store_node *s = gpir_node_to_store(instr->slots[i]);
      if (s && s->child == node) {
         /* acc node may consume 2 slots, so even it's the child of a
          * store node, it may not be inserted successfully, in which
          * case we need a move node for it */
         if (instr->alu_num_slot_free - consume_slot <
             instr->alu_num_slot_needed_by_store - 1)
            return false;

         instr->alu_num_slot_needed_by_store--;
         instr->alu_num_slot_free -= consume_slot;
         return true;
      }
   }

   /* not a child of any store node, so must reserve alu slot for store node */
   if (instr->alu_num_slot_free - consume_slot <
       instr->alu_num_slot_needed_by_store)
      return false;

   instr->alu_num_slot_free -= consume_slot;
   return true;
}

static void gpir_instr_remove_alu(gpir_instr *instr, gpir_node *node)
{
   int consume_slot = gpir_instr_get_consume_slot(instr, node);

   for (int i = GPIR_INSTR_SLOT_STORE0; i <= GPIR_INSTR_SLOT_STORE3; i++) {
      gpir_store_node *s = gpir_node_to_store(instr->slots[i]);
      if (s && s->child == node) {
         instr->alu_num_slot_needed_by_store++;
         instr->alu_num_slot_free += consume_slot;
         return;
      }
   }

   instr->alu_num_slot_free += consume_slot;
}

static bool gpir_instr_insert_reg0_check(gpir_instr *instr, gpir_node *node)
{
   gpir_load_node *load = gpir_node_to_load(node);
   int i = node->sched.pos - GPIR_INSTR_SLOT_REG0_LOAD0;

   if (load->component != i)
      return false;

   if (instr->reg0_is_attr && node->op != gpir_op_load_attribute)
      return false;

   if (instr->reg0_use_count) {
       if (instr->reg0_index != load->index)
          return false;
   }
   else {
      instr->reg0_is_attr = node->op == gpir_op_load_attribute;
      instr->reg0_index = load->index;
   }

   instr->reg0_use_count++;
   return true;
}

static void gpir_instr_remove_reg0(gpir_instr *instr, gpir_node *node)
{
   instr->reg0_use_count--;
   if (!instr->reg0_use_count)
      instr->reg0_is_attr = false;
}

static bool gpir_instr_insert_reg1_check(gpir_instr *instr, gpir_node *node)
{
   gpir_load_node *load = gpir_node_to_load(node);
   int i = node->sched.pos - GPIR_INSTR_SLOT_REG1_LOAD0;

   if (load->component != i)
      return false;

   if (instr->reg1_use_count) {
       if (instr->reg1_index != load->index)
          return false;
   }
   else
      instr->reg1_index = load->index;

   instr->reg1_use_count++;
   return true;
}

static void gpir_instr_remove_reg1(gpir_instr *instr, gpir_node *node)
{
   instr->reg1_use_count--;
}

static bool gpir_instr_insert_mem_check(gpir_instr *instr, gpir_node *node)
{
   gpir_load_node *load = gpir_node_to_load(node);
   int i = node->sched.pos - GPIR_INSTR_SLOT_MEM_LOAD0;

   if (load->component != i)
      return false;

   if (instr->mem_is_temp && node->op != gpir_op_load_temp)
      return false;

   if (instr->mem_use_count) {
       if (instr->mem_index != load->index)
          return false;
   }
   else {
      instr->mem_is_temp = node->op == gpir_op_load_temp;
      instr->mem_index = load->index;
   }

   instr->mem_use_count++;
   return true;
}

static void gpir_instr_remove_mem(gpir_instr *instr, gpir_node *node)
{
   instr->mem_use_count--;
   if (!instr->mem_use_count)
      instr->mem_is_temp = false;
}

static bool gpir_instr_insert_store_check(gpir_instr *instr, gpir_node *node)
{
   gpir_store_node *store = gpir_node_to_store(node);
   int i = node->sched.pos - GPIR_INSTR_SLOT_STORE0;

   if (store->component != i)
      return false;

   i >>= 1;
   switch (instr->store_content[i]) {
   case GPIR_INSTR_STORE_NONE:
      /* store temp has only one address reg for two store unit */
      if (node->op == gpir_op_store_temp &&
          instr->store_content[!i] == GPIR_INSTR_STORE_TEMP &&
          instr->store_index[!i] != store->index)
         return false;
      break;

   case GPIR_INSTR_STORE_VARYING:
      if (node->op != gpir_op_store_varying ||
          instr->store_index[i] != store->index)
         return false;
      break;

   case GPIR_INSTR_STORE_REG:
      if (node->op != gpir_op_store_reg ||
          instr->store_index[i] != store->index)
         return false;
      break;

   case GPIR_INSTR_STORE_TEMP:
      if (node->op != gpir_op_store_temp ||
          instr->store_index[i] != store->index)
         return false;
      break;
   }

   /* check if any store node has the same child as this node */
   for (int j = GPIR_INSTR_SLOT_STORE0; j <= GPIR_INSTR_SLOT_STORE3; j++) {
      gpir_store_node *s = gpir_node_to_store(instr->slots[j]);
      if (s && s->child == store->child)
         goto out;
   }

   /* check if the child is alrady in this instr's alu slot,
    * this may happen when store an scheduled alu node to reg
    */
   for (int j = GPIR_INSTR_SLOT_ALU_BEGIN; j <= GPIR_INSTR_SLOT_ALU_END; j++) {
      if (store->child == instr->slots[j])
         goto out;
   }

   /* no store node has the same child as this node, and child is not
    * already in this instr's alu slot, so instr must have some free
    * alu slot to insert this node's child
    */
   if (gpir_instr_alu_slot_is_full(instr))
      return false;

   instr->alu_num_slot_needed_by_store++;

out:
   if (instr->store_content[i] == GPIR_INSTR_STORE_NONE) {
      if (node->op == gpir_op_store_varying)
         instr->store_content[i] = GPIR_INSTR_STORE_VARYING;
      else if (node->op == gpir_op_store_reg)
         instr->store_content[i] = GPIR_INSTR_STORE_REG;
      else
         instr->store_content[i] = GPIR_INSTR_STORE_TEMP;

      instr->store_index[i] = store->index;
   }
   return true;
}

static void gpir_instr_remove_store(gpir_instr *instr, gpir_node *node)
{
   gpir_store_node *store = gpir_node_to_store(node);
   int component = node->sched.pos - GPIR_INSTR_SLOT_STORE0;
   int other_slot = GPIR_INSTR_SLOT_STORE0 + (component ^ 1);

   for (int j = GPIR_INSTR_SLOT_STORE0; j <= GPIR_INSTR_SLOT_STORE3; j++) {
      gpir_store_node *s = gpir_node_to_store(instr->slots[j]);
      if (s && s->child == store->child)
         goto out;
   }

   for (int j = GPIR_INSTR_SLOT_ALU_BEGIN; j <= GPIR_INSTR_SLOT_ALU_END; j++) {
      if (store->child == instr->slots[j])
         goto out;
   }

   instr->alu_num_slot_needed_by_store--;

out:
   if (!instr->slots[other_slot])
      instr->store_content[component >> 1] = GPIR_INSTR_STORE_NONE;
}

static bool gpir_instr_spill_move(gpir_instr *instr, int slot, int spill_to_start)
{
   gpir_node *node = instr->slots[slot];
   if (!node)
      return true;

   if (node->op != gpir_op_mov)
      return false;

   for (int i = spill_to_start; i <= GPIR_INSTR_SLOT_DIST_TWO_END; i++) {
      if (i != slot && !instr->slots[i] &&
          gpir_instr_check_acc_same_op(instr, node, i)) {
         instr->slots[i] = node;
         instr->slots[slot] = NULL;
         node->sched.pos = i;

         gpir_debug("instr %d spill move %d from slot %d to %d\n",
                    instr->index, node->index, slot, i);
         return true;
      }
   }

   return false;
}

static bool gpir_instr_slot_free(gpir_instr *instr, gpir_node *node)
{
   if (node->op == gpir_op_mov ||
       node->sched.pos > GPIR_INSTR_SLOT_DIST_TWO_END) {
      if (instr->slots[node->sched.pos])
         return false;
   }
   else {
      /* for node needs dist two slot, if the slot has a move, we can
       * spill it to other dist two slot without any side effect */
      int spill_to_start = GPIR_INSTR_SLOT_MUL0;
      if (node->op == gpir_op_complex1 || node->op == gpir_op_select)
         spill_to_start = GPIR_INSTR_SLOT_ADD0;

      if (!gpir_instr_spill_move(instr, node->sched.pos, spill_to_start))
         return false;

      if (node->op == gpir_op_complex1 || node->op == gpir_op_select) {
         if (!gpir_instr_spill_move(instr, GPIR_INSTR_SLOT_MUL1, spill_to_start))
            return false;
      }
   }

   return true;
}

bool gpir_instr_try_insert_node(gpir_instr *instr, gpir_node *node)
{
   if (!gpir_instr_slot_free(instr, node))
      return false;

   if (node->sched.pos >= GPIR_INSTR_SLOT_ALU_BEGIN &&
       node->sched.pos <= GPIR_INSTR_SLOT_ALU_END) {
      if (!gpir_instr_insert_alu_check(instr, node))
         return false;
   }
   else if (node->sched.pos >= GPIR_INSTR_SLOT_REG0_LOAD0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_REG0_LOAD3) {
      if (!gpir_instr_insert_reg0_check(instr, node))
         return false;
   }
   else if (node->sched.pos >= GPIR_INSTR_SLOT_REG1_LOAD0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_REG1_LOAD3) {
      if (!gpir_instr_insert_reg1_check(instr, node))
         return false;
   }
   else if (node->sched.pos >= GPIR_INSTR_SLOT_MEM_LOAD0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_MEM_LOAD3) {
      if (!gpir_instr_insert_mem_check(instr, node))
         return false;
   }
   else if (node->sched.pos >= GPIR_INSTR_SLOT_STORE0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_STORE3) {
      if (!gpir_instr_insert_store_check(instr, node))
         return false;
   }

   instr->slots[node->sched.pos] = node;

   if (node->op == gpir_op_complex1 || node->op == gpir_op_select)
      instr->slots[GPIR_INSTR_SLOT_MUL1] = node;

   return true;
}

void gpir_instr_remove_node(gpir_instr *instr, gpir_node *node)
{
   if (node->sched.pos >= GPIR_INSTR_SLOT_ALU_BEGIN &&
       node->sched.pos <= GPIR_INSTR_SLOT_ALU_END)
      gpir_instr_remove_alu(instr, node);
   else if (node->sched.pos >= GPIR_INSTR_SLOT_REG0_LOAD0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_REG0_LOAD3)
      gpir_instr_remove_reg0(instr, node);
   else if (node->sched.pos >= GPIR_INSTR_SLOT_REG1_LOAD0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_REG1_LOAD3)
      gpir_instr_remove_reg1(instr, node);
   else if (node->sched.pos >= GPIR_INSTR_SLOT_MEM_LOAD0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_MEM_LOAD3)
      gpir_instr_remove_mem(instr, node);
   else if (node->sched.pos >= GPIR_INSTR_SLOT_STORE0 &&
            node->sched.pos <= GPIR_INSTR_SLOT_STORE3)
      gpir_instr_remove_store(instr, node);

   instr->slots[node->sched.pos] = NULL;

   if (node->op == gpir_op_complex1 || node->op == gpir_op_select)
      instr->slots[GPIR_INSTR_SLOT_MUL1] = NULL;
}

void gpir_instr_print_prog(gpir_compiler *comp)
{
   struct {
      int len;
      char *name;
   } fields[] = {
      [GPIR_INSTR_SLOT_MUL0] = { 4, "mul0" },
      [GPIR_INSTR_SLOT_MUL1] = { 4, "mul1" },
      [GPIR_INSTR_SLOT_ADD0] = { 4, "add0" },
      [GPIR_INSTR_SLOT_ADD1] = { 4, "add1" },
      [GPIR_INSTR_SLOT_REG0_LOAD3] = { 15, "load0" },
      [GPIR_INSTR_SLOT_REG1_LOAD3] = { 15, "load1" },
      [GPIR_INSTR_SLOT_MEM_LOAD3] = { 15, "load2" },
      [GPIR_INSTR_SLOT_BRANCH] = { 4, "bnch" },
      [GPIR_INSTR_SLOT_STORE3] = { 15, "store" },
      [GPIR_INSTR_SLOT_COMPLEX] = { 4, "cmpl" },
      [GPIR_INSTR_SLOT_PASS] = { 4, "pass" },
   };

   printf("========prog instr========\n");
   printf("     ");
   for (int i = 0; i < GPIR_INSTR_SLOT_NUM; i++) {
      if (fields[i].len)
         printf("%-*s ", fields[i].len, fields[i].name);
   }
   printf("\n");

   int index = 0;
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      list_for_each_entry(gpir_instr, instr, &block->instr_list, list) {
         printf("%03d: ", index++);

         char buff[16] = "null";
         int start = 0;
         for (int j = 0; j < GPIR_INSTR_SLOT_NUM; j++) {
            gpir_node *node = instr->slots[j];
            if (fields[j].len) {
               if (node)
                  snprintf(buff + start, sizeof(buff) - start, "%d", node->index);
               printf("%-*s ", fields[j].len, buff);

               strcpy(buff, "null");
               start = 0;
            }
            else {
               if (node)
                  start += snprintf(buff + start, sizeof(buff) - start, "%d", node->index);
               start += snprintf(buff + start, sizeof(buff) - start, "|");
            }
         }
         printf("\n");
      }
      printf("-----------------------\n");
   }
   printf("==========================\n");
}
