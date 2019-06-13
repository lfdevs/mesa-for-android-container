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

/*
 * GP schedule algorithm (by Connor Abbott <cwabbott0@gmail.com>)
 *
 * Pre schedule phase:
 * 1. order all nodes in a sequence
 * 2. convert the real reg read/write to GP load/store node, now all
 *    variable is SSA
 * 3. do reg alloc for all SSA with 11 reg (value reg) and spill with
 *    load/store to real reg if needed
 * 4. add fake dependency like this:
 *    after step 3, node sequence is
 *      01: r1=r2+r3
 *      02: r4=r1+r2
 *      03: r1=r5+r6
 *    we should add a fake dependency of node 3 to node 2 like a
 *    write-after-read dep. But this is not really write-after-read
 *    dep because there's no r1 really, because it's a value register.
 *    We need this fake dep in the schedule phase to make sure in any
 *    schedule point, there're only <=11 input needed by the past
 *    scheduled nodes.
 * 5. build DAG according to all the real and fake dep
 *
 * Schedule phase:
 * 1. Compute the nodes ready to schedule, if no nodes, exit
 * 2. Create a new GP instruction, and call it as current instr
 * 3. For any nodes with a use 2 cycles ago with a definition ready to
 *    schedule, schedule that definition immediately if possible, or else
 *    schedule a move.
 * 4. For any nodes with a use 2 cycles ago but the definition not
 *    scheduled and not ready to schedule, schedule a move immediately
 *    to prevent the value from falling off the queue.
 * 5. Calculate the number of remaining nodes with a use 1 cycle ago but
 *    the definition not yet scheduled, and if there are more than 5,
 *    schedule moves or definitions for the rest now.
 * 6. Schedule the rest of the available nodes using your favorite heuristic
 *    to current instr.
 * 7. go to step 1
 *
 * Step 5 for the current instruction guarantees that steps 3 and 4 for
 * the next instruction will always succeed, so it's only step 5 that can
 * possibly fail. Now, note that the nodes whose definitions have not yet
 * been scheduled but one or more use has been scheduled, are exactly the
 * nodes that are live in the final schedule. Therefore there will never
 * be more than 11 of them (guarenteed by the 11 value reg alloc and the
 * fake dep added before schedule). The worst case for step 5 is that all of
 * these nodes had a use 1 cycle ago, which means that none of them hit
 * case 3 or 4 already, so there are 6 slots still available so step 5
 * will always succeed. In general, even if there are exactly 11 values
 * live, if n are scheduled in steps 3 and 4, there are 11-n left in step
 * 4 so at most 11-n-5 = 6-n are scheduled in step 5 and therefore 6 are
 * scheduled total, below the limit. So the algorithm will always succeed.
 */

static int gpir_min_dist_alu(gpir_dep *dep)
{
   switch (dep->pred->op) {
   case gpir_op_load_uniform:
   case gpir_op_load_temp:
   case gpir_op_load_reg:
   case gpir_op_load_attribute:
      return 0;

   case gpir_op_complex1:
      return 2;

   default:
      return 1;
   }
}

static int gpir_get_min_dist(gpir_dep *dep)
{
   switch (dep->type) {
   case GPIR_DEP_INPUT:
      switch (dep->succ->op) {
      case gpir_op_store_temp:
      case gpir_op_store_reg:
      case gpir_op_store_varying:
         /* store must use alu node as input */
         if (dep->pred->type == gpir_node_type_load)
            return INT_MAX >> 2;
         else
            return 0;

      default:
         return gpir_min_dist_alu(dep);
      }

   case GPIR_DEP_OFFSET:
      assert(dep->succ->op == gpir_op_store_temp);
      return gpir_min_dist_alu(dep);

   case GPIR_DEP_READ_AFTER_WRITE:
      switch (dep->succ->op) {
      case gpir_op_load_temp:
         assert(dep->pred->op == gpir_op_store_temp);
         return 4;
      case gpir_op_load_reg:
         assert(dep->pred->op == gpir_op_store_reg);
         return 3;
      case gpir_op_load_uniform:
         assert(dep->pred->op == gpir_op_store_temp_load_off0 ||
                dep->pred->op == gpir_op_store_temp_load_off1 ||
                dep->pred->op == gpir_op_store_temp_load_off2);
         return 4;
      default:
         assert(0);
      }

   case GPIR_DEP_WRITE_AFTER_READ:
      switch (dep->pred->op) {
      case gpir_op_load_temp:
         assert(dep->succ->op == gpir_op_store_temp);
         return -3;
      case gpir_op_load_reg:
         assert(dep->succ->op == gpir_op_store_reg);
         return -2;
      case gpir_op_load_uniform:
         assert(dep->succ->op == gpir_op_store_temp_load_off0 ||
                dep->succ->op == gpir_op_store_temp_load_off1 ||
                dep->succ->op == gpir_op_store_temp_load_off2);
         return -3;
      default:
         assert(0);
      }

   case GPIR_DEP_VREG_WRITE_AFTER_READ:
      return 0;

   case GPIR_DEP_VREG_READ_AFTER_WRITE:
      assert(0); /* not possible, this is GPIR_DEP_INPUT */
   }

   return 0;
}

static int gpir_max_dist_alu(gpir_dep *dep)
{
   switch (dep->pred->op) {
   case gpir_op_load_uniform:
   case gpir_op_load_temp:
      return 0;
   case gpir_op_load_attribute:
      return 1;
   case gpir_op_load_reg:
      if (dep->pred->sched.pos < GPIR_INSTR_SLOT_REG0_LOAD0 ||
          dep->pred->sched.pos > GPIR_INSTR_SLOT_REG0_LOAD3)
         return 0;
      else
         return 1;
   case gpir_op_exp2_impl:
   case gpir_op_log2_impl:
   case gpir_op_rcp_impl:
   case gpir_op_rsqrt_impl:
   case gpir_op_store_temp_load_off0:
   case gpir_op_store_temp_load_off1:
   case gpir_op_store_temp_load_off2:
      return 1;
   case gpir_op_mov:
      if (dep->pred->sched.pos == GPIR_INSTR_SLOT_COMPLEX)
         return 1;
      else
         return 2;
   default:
      return 2;
   }
}

static int gpir_get_max_dist(gpir_dep *dep)
{
   switch (dep->type) {
   case GPIR_DEP_INPUT:
      switch (dep->succ->op) {
      case gpir_op_store_temp:
      case gpir_op_store_reg:
      case gpir_op_store_varying:
         return 0;

      default:
         return gpir_max_dist_alu(dep);
      }

   case GPIR_DEP_OFFSET:
      assert(dep->succ->op == gpir_op_store_temp);
      return gpir_max_dist_alu(dep);

   default:
      return INT_MAX >> 2; /* Don't want to overflow... */
   }
}

static void schedule_update_distance(gpir_node *node)
{
   if (gpir_node_is_leaf(node)) {
      node->sched.dist = 0;
      return;
   }

   gpir_node_foreach_pred(node, dep) {
      gpir_node *pred = dep->pred;

      if (pred->sched.dist < 0)
         schedule_update_distance(pred);

      int dist = pred->sched.dist + 1;
      if (node->sched.dist < dist)
         node->sched.dist = dist;
   }
}

static void schedule_insert_ready_list(struct list_head *ready_list,
                                       gpir_node *insert_node)
{
   /* if this node is fully ready or partially ready
    *   fully ready: all successors have been scheduled
    *   partially ready: part of input successors have been scheduled
    *
    * either fully ready or partially ready node need be inserted to
    * the ready list, but we only schedule a move node for partially
    * ready node.
    */
   bool ready = true, insert = false;
   gpir_node_foreach_succ(insert_node, dep) {
      gpir_node *succ = dep->succ;
      if (succ->sched.instr >= 0) {
         if (dep->type == GPIR_DEP_INPUT)
            insert = true;
      }
      else
         ready = false;
   }

   insert_node->sched.ready = ready;
   /* for root node */
   insert |= ready;

   if (!insert || insert_node->sched.inserted)
      return;

   struct list_head *insert_pos = ready_list;
   list_for_each_entry(gpir_node, node, ready_list, list) {
      if (insert_node->sched.dist > node->sched.dist) {
         insert_pos = &node->list;
         break;
      }
   }

   list_addtail(&insert_node->list, insert_pos);
   insert_node->sched.inserted = true;
}

static int gpir_get_max_start(gpir_node *node)
{
   int max_start = 0;

   /* find the max start instr constrainted by all successors */
   gpir_node_foreach_succ(node, dep) {
      gpir_node *succ = dep->succ;
      if (succ->sched.instr < 0)
         continue;

      int start = succ->sched.instr + gpir_get_min_dist(dep);
      if (start > max_start)
         max_start = start;
   }

   return max_start;
}

static int gpir_get_min_end(gpir_node *node)
{
   int min_end = INT_MAX;

   /* find the min end instr constrainted by all successors */
   gpir_node_foreach_succ(node, dep) {
      gpir_node *succ = dep->succ;
      if (succ->sched.instr < 0)
         continue;

      int end = succ->sched.instr + gpir_get_max_dist(dep);
      if (end < min_end)
         min_end = end;
   }

   return min_end;
}

static gpir_node *gpir_sched_instr_has_load(gpir_instr *instr, gpir_node *node)
{
   gpir_load_node *load = gpir_node_to_load(node);

   for (int i = GPIR_INSTR_SLOT_REG0_LOAD0; i <= GPIR_INSTR_SLOT_MEM_LOAD3; i++) {
      if (!instr->slots[i])
         continue;

      gpir_load_node *iload = gpir_node_to_load(instr->slots[i]);
      if (load->node.op == iload->node.op &&
          load->index == iload->index &&
          load->component == iload->component)
         return &iload->node;
   }
   return NULL;
}

static bool schedule_try_place_node(gpir_instr *instr, gpir_node *node)
{
   if (node->type == gpir_node_type_load) {
      gpir_node *load = gpir_sched_instr_has_load(instr, node);
      if (load) {
         gpir_debug("same load %d in instr %d for node %d\n",
                    load->index, instr->index, node->index);

         /* not really merge two node, just fake scheduled same place */
         node->sched.instr = load->sched.instr;
         node->sched.pos = load->sched.pos;
         return true;
      }
   }

   node->sched.instr = instr->index;

   int *slots = gpir_op_infos[node->op].slots;
   for (int i = 0; slots[i] != GPIR_INSTR_SLOT_END; i++) {
      node->sched.pos = slots[i];
      if (node->sched.instr >= gpir_get_max_start(node) &&
          node->sched.instr <= gpir_get_min_end(node) &&
          gpir_instr_try_insert_node(instr, node))
         return true;
   }

   node->sched.instr = -1;
   node->sched.pos = -1;
   return false;
}

static gpir_node *schedule_create_move_node(gpir_node *node)
{
   gpir_alu_node *move = gpir_node_create(node->block, gpir_op_mov);
   if (unlikely(!move))
      return NULL;

   move->children[0] = node;
   move->num_child = 1;

   move->node.sched.instr = -1;
   move->node.sched.pos = -1;
   move->node.sched.dist = node->sched.dist;

   gpir_debug("create move %d for %d\n", move->node.index, node->index);
   return &move->node;
}

static gpir_node *gpir_sched_node(gpir_instr *instr, gpir_node *node)
{
   if (node->op == gpir_op_mov) {
      gpir_node *child = gpir_node_to_alu(node)->children[0];
      gpir_node_foreach_succ_safe(node, dep) {
         gpir_node *succ = dep->succ;
         if (succ->sched.instr < 0 ||
             instr->index < succ->sched.instr + gpir_get_min_dist(dep)) {
            gpir_node_replace_pred(dep, child);
            if (dep->type == GPIR_DEP_INPUT)
               gpir_node_replace_child(succ, node, child);
         }
      }
      MAYBE_UNUSED bool result = schedule_try_place_node(instr, node);
      assert(result);
      return node;
   }
   else {
      gpir_node *move = schedule_create_move_node(node);
      list_del(&node->list);
      node->sched.ready = false;
      node->sched.inserted = false;
      gpir_node_replace_succ(move, node);
      gpir_node_add_dep(move, node, GPIR_DEP_INPUT);
      return move;
   }
}

static bool gpir_is_input_node(gpir_node *node)
{
   gpir_node_foreach_succ(node, dep) {
      if (dep->type == GPIR_DEP_INPUT)
         return true;
   }
   return false;
}

static int gpir_get_min_scheduled_succ(gpir_node *node)
{
   int min = INT_MAX;
   gpir_node_foreach_succ(node, dep) {
      gpir_node *succ = dep->succ;
      if (succ->sched.instr >= 0 && dep->type == GPIR_DEP_INPUT) {
         if (min > succ->sched.instr)
            min = succ->sched.instr;
      }
   }
   return min;
}

static gpir_node *gpir_sched_instr_pass(gpir_instr *instr,
                                        struct list_head *ready_list)
{
   /* fully ready node reach its max dist with any of its successor */
   list_for_each_entry_safe(gpir_node, node, ready_list, list) {
      if (node->sched.ready) {
         int end = gpir_get_min_end(node);
         assert(end >= instr->index);
         if (instr->index < end)
            continue;

         gpir_debug("fully ready max node %d\n", node->index);

         if (schedule_try_place_node(instr, node))
            return node;

         return gpir_sched_node(instr, node);
      }
   }

   /* partially ready node reach its max dist with any of its successor */
   list_for_each_entry_safe(gpir_node, node, ready_list, list) {
      if (!node->sched.ready) {
         int end = gpir_get_min_end(node);
         assert(end >= instr->index);
         if (instr->index < end)
            continue;

         gpir_debug("partially ready max node %d\n", node->index);

         return gpir_sched_node(instr, node);
      }
   }

   /* schedule node used by previous instr when count > 5 */
   int count = 0;
   gpir_node *two_slot_node = NULL;
   list_for_each_entry(gpir_node, node, ready_list, list) {
      if (gpir_is_input_node(node)) {
         int min = gpir_get_min_scheduled_succ(node);
         assert(min >= instr->index - 1);
         if (min == instr->index - 1) {
            if (gpir_op_infos[node->op].may_consume_two_slots) {
               two_slot_node = node;
               count += 2;
            }
            else
               count++;
         }
      }
   }

   if (count > 5) {
      /* When no slot avaible, must schedule a move for two slot node
       * to reduce the count. This results from the dummy_m/f method.
       */
      if (gpir_instr_alu_slot_is_full(instr)) {
         assert(two_slot_node);
         gpir_debug("instr is full, schedule move node for two slot node %d\n",
                    two_slot_node->index);
         return gpir_sched_node(instr, two_slot_node);
      }

      /* schedule fully ready node first */
      list_for_each_entry(gpir_node, node, ready_list, list) {
         if (gpir_is_input_node(node)) {
            int min = gpir_get_min_scheduled_succ(node);
            if (min == instr->index - 1 && node->sched.ready) {
               gpir_debug(">5 ready node %d\n", node->index);

               if (schedule_try_place_node(instr, node))
                  return node;
            }
         }
      }

      /* no fully ready node be scheduled, schedule partially ready node */
      list_for_each_entry_safe(gpir_node, node, ready_list, list) {
         if (gpir_is_input_node(node)) {
            int min = gpir_get_min_scheduled_succ(node);
            if (min == instr->index - 1 && !node->sched.ready) {
               gpir_debug(">5 partially ready node %d\n", node->index);

               return gpir_sched_node(instr, node);
            }
         }
      }

      /* finally schedule move for fully ready node */
      list_for_each_entry_safe(gpir_node, node, ready_list, list) {
         if (gpir_is_input_node(node)) {
            int min = gpir_get_min_scheduled_succ(node);
            if (min == instr->index - 1 && node->sched.ready) {
               gpir_debug(">5 fully ready move node %d\n", node->index);

               return gpir_sched_node(instr, node);
            }
         }
      }
   }

   /* schedule remain fully ready nodes */
   list_for_each_entry(gpir_node, node, ready_list, list) {
      if (node->sched.ready) {
         gpir_debug("remain fully ready node %d\n", node->index);

         if (schedule_try_place_node(instr, node))
            return node;
      }
   }

   return NULL;
}

static void schedule_print_pre_one_instr(gpir_instr *instr,
                                         struct list_head *ready_list)
{
   if (!(lima_debug & LIMA_DEBUG_GP))
      return;

   printf("instr %d for ready list:", instr->index);
   list_for_each_entry(gpir_node, node, ready_list, list) {
      printf(" %d/%c", node->index, node->sched.ready ? 'r' : 'p');
   }
   printf("\n");
}

static void schedule_print_post_one_instr(gpir_instr *instr)
{
   if (!(lima_debug & LIMA_DEBUG_GP))
      return;

   printf("post schedule instr");
   for (int i = 0; i < GPIR_INSTR_SLOT_NUM; i++) {
      if (instr->slots[i])
         printf(" %d/%d", i, instr->slots[i]->index);
   }
   printf("\n");
}


static bool schedule_one_instr(gpir_block *block, struct list_head *ready_list)
{
   gpir_instr *instr = gpir_instr_create(block);
   if (unlikely(!instr))
      return false;

   schedule_print_pre_one_instr(instr, ready_list);

   while (true) {
      gpir_node *node = gpir_sched_instr_pass(instr, ready_list);
      if (!node)
         break;

      if (node->sched.instr < 0)
         schedule_insert_ready_list(ready_list, node);
      else {
         list_del(&node->list);
         list_add(&node->list, &block->node_list);

         gpir_node_foreach_pred(node, dep) {
            gpir_node *pred = dep->pred;
            schedule_insert_ready_list(ready_list, pred);
         }
      }
   }

   schedule_print_post_one_instr(instr);
   return true;
}

static bool schedule_block(gpir_block *block)
{
   /* calculate distance */
   list_for_each_entry(gpir_node, node, &block->node_list, list) {
      if (gpir_node_is_root(node))
         schedule_update_distance(node);
   }

   struct list_head ready_list;
   list_inithead(&ready_list);

   /* construct the ready list from root nodes */
   list_for_each_entry_safe(gpir_node, node, &block->node_list, list) {
      if (gpir_node_is_root(node))
         schedule_insert_ready_list(&ready_list, node);
   }

   list_inithead(&block->node_list);
   while (!list_empty(&ready_list)) {
      if (!schedule_one_instr(block, &ready_list))
         return false;
   }

   return true;
}

static void schedule_build_vreg_dependency(gpir_block *block)
{
   gpir_node *regs[GPIR_VALUE_REG_NUM] = {0};
   list_for_each_entry(gpir_node, node, &block->node_list, list) {
      /* store node has no value reg assigned */
      if (node->value_reg < 0)
         continue;

      gpir_node *reg = regs[node->value_reg];
      if (reg) {
         gpir_node_foreach_succ(reg, dep) {
            /* write after read dep should only apply to real 'read' */
            if (dep->type != GPIR_DEP_INPUT)
               continue;

            gpir_node *succ = dep->succ;
            gpir_node_add_dep(node, succ, GPIR_DEP_VREG_WRITE_AFTER_READ);
         }
      }
      regs[node->value_reg] = node;
   }

   /* merge dummy_f/m to the node created from */
   list_for_each_entry_safe(gpir_node, node, &block->node_list, list) {
      if (node->op == gpir_op_dummy_m) {
         gpir_alu_node *alu = gpir_node_to_alu(node);
         gpir_node *origin = alu->children[0];
         gpir_node *dummy_f = alu->children[1];

         gpir_node_foreach_succ(node, dep) {
            gpir_node *succ = dep->succ;
            /* origin and node may have same succ (by VREG/INPUT or
             * VREG/VREG dep), so use gpir_node_add_dep() instead of
             * gpir_node_replace_pred() */
            gpir_node_add_dep(succ, origin, dep->type);
            gpir_node_replace_child(succ, node, origin);
         }
         gpir_node_delete(dummy_f);
         gpir_node_delete(node);
      }
   }
}

static void schedule_build_preg_dependency(gpir_compiler *comp)
{
   /* merge reg with the same index */
   gpir_reg *regs[GPIR_VALUE_REG_NUM] = {0};
   list_for_each_entry(gpir_reg, reg, &comp->reg_list, list) {
      if (!regs[reg->index])
         regs[reg->index] = reg;
      else {
         list_splicetail(&reg->defs_list, &regs[reg->index]->defs_list);
         list_splicetail(&reg->uses_list, &regs[reg->index]->uses_list);
      }
   }

   /* calculate physical reg read/write dependency for load/store nodes */
   for (int i = 0; i < GPIR_VALUE_REG_NUM; i++) {
      gpir_reg *reg = regs[i];
      if (!reg)
         continue;

      /* sort reg write */
      struct list_head tmp_list;
      list_replace(&reg->defs_list, &tmp_list);
      list_inithead(&reg->defs_list);
      list_for_each_entry_safe(gpir_store_node, store, &tmp_list, reg_link) {
         struct list_head *insert_pos = &reg->defs_list;
         list_for_each_entry(gpir_store_node, st, &reg->defs_list, reg_link) {
            if (st->node.sched.index > store->node.sched.index) {
               insert_pos = &st->reg_link;
               break;
            }
         }
         list_del(&store->reg_link);
         list_addtail(&store->reg_link, insert_pos);
      }

      /* sort reg read */
      list_replace(&reg->uses_list, &tmp_list);
      list_inithead(&reg->uses_list);
      list_for_each_entry_safe(gpir_load_node, load, &tmp_list, reg_link) {
         struct list_head *insert_pos = &reg->uses_list;
         list_for_each_entry(gpir_load_node, ld, &reg->uses_list, reg_link) {
            if (ld->node.sched.index > load->node.sched.index) {
               insert_pos = &ld->reg_link;
               break;
            }
         }
         list_del(&load->reg_link);
         list_addtail(&load->reg_link, insert_pos);
      }

      /* insert dependency */
      gpir_store_node *store =
         list_first_entry(&reg->defs_list, gpir_store_node, reg_link);
      gpir_store_node *next = store->reg_link.next != &reg->defs_list ?
         list_first_entry(&store->reg_link, gpir_store_node, reg_link) : NULL;

      list_for_each_entry(gpir_load_node, load, &reg->uses_list, reg_link) {
         /* loop until load is between store and next */
         while (next && next->node.sched.index < load->node.sched.index) {
            store = next;
            next = store->reg_link.next != &reg->defs_list ?
               list_first_entry(&store->reg_link, gpir_store_node, reg_link) : NULL;
         }

         gpir_node_add_dep(&load->node, &store->node, GPIR_DEP_READ_AFTER_WRITE);
         if (next)
            gpir_node_add_dep(&next->node, &load->node, GPIR_DEP_WRITE_AFTER_READ);
      }
   }
}

static void print_statistic(gpir_compiler *comp, int save_index)
{
   int num_nodes[gpir_op_num] = {0};
   int num_created_nodes[gpir_op_num] = {0};

   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      list_for_each_entry(gpir_node, node, &block->node_list, list) {
         num_nodes[node->op]++;
         if (node->index >= save_index)
            num_created_nodes[node->op]++;
      }
   }

   printf("====== gpir scheduler statistic ======\n");
   printf("---- how many nodes are scheduled ----\n");
   int n = 0, l = 0;
   for (int i = 0; i < gpir_op_num; i++) {
      if (num_nodes[i]) {
         printf("%10s:%-6d", gpir_op_infos[i].name, num_nodes[i]);
         n += num_nodes[i];
         if (!(++l % 4))
            printf("\n");
      }
   }
   if (l % 4)
      printf("\n");
   printf("\ntotal: %d\n", n);

   printf("---- how many nodes are created ----\n");
   n = l = 0;
   for (int i = 0; i < gpir_op_num; i++) {
      if (num_created_nodes[i]) {
         printf("%10s:%-6d", gpir_op_infos[i].name, num_created_nodes[i]);
         n += num_created_nodes[i];
         if (!(++l % 4))
            printf("\n");
      }
   }
   if (l % 4)
      printf("\n");
   printf("\ntotal: %d\n", n);
   printf("------------------------------------\n");
}

bool gpir_schedule_prog(gpir_compiler *comp)
{
   int save_index = comp->cur_index;

   /* init schedule info */
   int index = 0;
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      block->sched.instr_index = 0;
      list_for_each_entry(gpir_node, node, &block->node_list, list) {
         node->sched.instr = -1;
         node->sched.pos = -1;
         node->sched.index = index++;
         node->sched.dist = -1;
         node->sched.ready = false;
         node->sched.inserted = false;
      }
   }

   /* build fake/virtual dependency */
   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      schedule_build_vreg_dependency(block);
   }
   schedule_build_preg_dependency(comp);

   //gpir_debug("after scheduler build reg dependency\n");
   //gpir_node_print_prog_dep(comp);

   list_for_each_entry(gpir_block, block, &comp->block_list, list) {
      if (!schedule_block(block)) {
         gpir_error("fail schedule block\n");
         return false;
      }
   }

   if (lima_debug & LIMA_DEBUG_GP) {
      print_statistic(comp, save_index);
      gpir_instr_print_prog(comp);
   }

   return true;
}
