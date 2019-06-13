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

#include "util/ralloc.h"
#include "util/register_allocate.h"
#include "util/u_debug.h"

#include "ppir.h"
#include "lima_context.h"

#define PPIR_FULL_REG_NUM  6

#define PPIR_VEC1_REG_NUM       (PPIR_FULL_REG_NUM * 4) /* x, y, z, w */
#define PPIR_VEC2_REG_NUM       (PPIR_FULL_REG_NUM * 3) /* xy, yz, zw */
#define PPIR_VEC3_REG_NUM       (PPIR_FULL_REG_NUM * 2) /* xyz, yzw */
#define PPIR_VEC4_REG_NUM       PPIR_FULL_REG_NUM       /* xyzw */
#define PPIR_HEAD_VEC1_REG_NUM  PPIR_FULL_REG_NUM       /* x */
#define PPIR_HEAD_VEC2_REG_NUM  PPIR_FULL_REG_NUM       /* xy */
#define PPIR_HEAD_VEC3_REG_NUM  PPIR_FULL_REG_NUM       /* xyz */
#define PPIR_HEAD_VEC4_REG_NUM  PPIR_FULL_REG_NUM       /* xyzw */

#define PPIR_VEC1_REG_BASE       0
#define PPIR_VEC2_REG_BASE       (PPIR_VEC1_REG_BASE + PPIR_VEC1_REG_NUM)
#define PPIR_VEC3_REG_BASE       (PPIR_VEC2_REG_BASE + PPIR_VEC2_REG_NUM)
#define PPIR_VEC4_REG_BASE       (PPIR_VEC3_REG_BASE + PPIR_VEC3_REG_NUM)
#define PPIR_HEAD_VEC1_REG_BASE  (PPIR_VEC4_REG_BASE + PPIR_VEC4_REG_NUM)
#define PPIR_HEAD_VEC2_REG_BASE  (PPIR_HEAD_VEC1_REG_BASE + PPIR_HEAD_VEC1_REG_NUM)
#define PPIR_HEAD_VEC3_REG_BASE  (PPIR_HEAD_VEC2_REG_BASE + PPIR_HEAD_VEC2_REG_NUM)
#define PPIR_HEAD_VEC4_REG_BASE  (PPIR_HEAD_VEC3_REG_BASE + PPIR_HEAD_VEC3_REG_NUM)
#define PPIR_REG_COUNT           (PPIR_HEAD_VEC4_REG_BASE + PPIR_HEAD_VEC4_REG_NUM)

enum ppir_ra_reg_class {
   ppir_ra_reg_class_vec1,
   ppir_ra_reg_class_vec2,
   ppir_ra_reg_class_vec3,
   ppir_ra_reg_class_vec4,

   /* 4 reg class for load/store instr regs:
    * load/store instr has no swizzle field, so the (virtual) register
    * must be allocated at the beginning of a (physical) register,
    */
   ppir_ra_reg_class_head_vec1,
   ppir_ra_reg_class_head_vec2,
   ppir_ra_reg_class_head_vec3,
   ppir_ra_reg_class_head_vec4,

   ppir_ra_reg_class_num,
};

static const int ppir_ra_reg_base[ppir_ra_reg_class_num + 1] = {
   [ppir_ra_reg_class_vec1]       = PPIR_VEC1_REG_BASE,
   [ppir_ra_reg_class_vec2]       = PPIR_VEC2_REG_BASE,
   [ppir_ra_reg_class_vec3]       = PPIR_VEC3_REG_BASE,
   [ppir_ra_reg_class_vec4]       = PPIR_VEC4_REG_BASE,
   [ppir_ra_reg_class_head_vec1]  = PPIR_HEAD_VEC1_REG_BASE,
   [ppir_ra_reg_class_head_vec2]  = PPIR_HEAD_VEC2_REG_BASE,
   [ppir_ra_reg_class_head_vec3]  = PPIR_HEAD_VEC3_REG_BASE,
   [ppir_ra_reg_class_head_vec4]  = PPIR_HEAD_VEC4_REG_BASE,
   [ppir_ra_reg_class_num]        = PPIR_REG_COUNT,
};

static unsigned int *
ppir_ra_reg_q_values[ppir_ra_reg_class_num] = {
   (unsigned int []) {1, 2, 3, 4, 1, 2, 3, 4},
   (unsigned int []) {2, 3, 3, 3, 1, 2, 3, 3},
   (unsigned int []) {2, 2, 2, 2, 1, 2, 2, 2},
   (unsigned int []) {1, 1, 1, 1, 1, 1, 1, 1},
   (unsigned int []) {1, 1, 1, 1, 1, 1, 1, 1},
   (unsigned int []) {1, 1, 1, 1, 1, 1, 1, 1},
   (unsigned int []) {1, 1, 1, 1, 1, 1, 1, 1},
   (unsigned int []) {1, 1, 1, 1, 1, 1, 1, 1},
};

struct ra_regs *ppir_regalloc_init(void *mem_ctx)
{
   struct ra_regs *ret = ra_alloc_reg_set(mem_ctx, PPIR_REG_COUNT, false);
   if (!ret)
      return NULL;

   /* (x, y, z, w) (xy, yz, zw) (xyz, yzw) (xyzw) (x) (xy) (xyz) (xyzw) */
   static const int class_reg_num[ppir_ra_reg_class_num] = {
      4, 3, 2, 1, 1, 1, 1, 1,
   };
   /* base reg (x, y, z, w) confliction with other regs */
   for (int h = 0; h < 4; h++) {
      int base_reg_mask = 1 << h;
      for (int i = 1; i < ppir_ra_reg_class_num; i++) {
         int class_reg_base_mask = (1 << ((i % 4) + 1)) - 1;
         for (int j = 0; j < class_reg_num[i]; j++) {
            if (base_reg_mask & (class_reg_base_mask << j)) {
               for (int k = 0; k < PPIR_FULL_REG_NUM; k++) {
                  ra_add_reg_conflict(ret, k * 4 + h,
                     ppir_ra_reg_base[i] + k * class_reg_num[i] + j);
               }
            }
         }
      }
   }
   /* build all other confliction by the base reg confliction */
   for (int i = 0; i < PPIR_VEC1_REG_NUM; i++)
      ra_make_reg_conflicts_transitive(ret, i);

   for (int i = 0; i < ppir_ra_reg_class_num; i++)
      ra_alloc_reg_class(ret);

   int reg_index = 0;
   for (int i = 0; i < ppir_ra_reg_class_num; i++) {
      while (reg_index < ppir_ra_reg_base[i + 1])
         ra_class_add_reg(ret, i, reg_index++);
   }

   ra_set_finalize(ret, ppir_ra_reg_q_values);
   return ret;
}

static ppir_reg *get_src_reg(ppir_src *src)
{
   switch (src->type) {
   case ppir_target_ssa:
      return src->ssa;
   case ppir_target_register:
      return src->reg;
   default:
      return NULL;
   }
}

static void ppir_regalloc_update_reglist_ssa(ppir_compiler *comp)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_node, node, &block->node_list, list) {
         if (node->op == ppir_op_store_color)
            continue;

         if (!node->instr || node->op == ppir_op_const)
            continue;

         ppir_dest *dest = ppir_node_get_dest(node);
         if (dest) {
            ppir_reg *reg = NULL;

            if (dest->type == ppir_target_ssa) {
               reg = &dest->ssa;
               list_addtail(&reg->list, &comp->reg_list);
            }
         }
      }
   }
}

static ppir_reg *ppir_regalloc_build_liveness_info(ppir_compiler *comp)
{
   ppir_reg *ret = NULL;

   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_node, node, &block->node_list, list) {
         if (node->op == ppir_op_store_color) {
            ppir_store_node *store = ppir_node_to_store(node);
            if (store->src.type == ppir_target_ssa)
               ret = store->src.ssa;
            else
               ret = store->src.reg;
            ret->live_out = INT_MAX;
            continue;
         }

         if (!node->instr || node->op == ppir_op_const)
            continue;

         /* update reg live_in from node dest (write) */
         ppir_dest *dest = ppir_node_get_dest(node);
         if (dest) {
            ppir_reg *reg = NULL;

            if (dest->type == ppir_target_ssa) {
               reg = &dest->ssa;
            }
            else if (dest->type == ppir_target_register)
               reg = dest->reg;

            if (reg && node->instr->seq < reg->live_in)
               reg->live_in = node->instr->seq;
         }

         /* update reg live_out from node src (read) */
         switch (node->type) {
         case ppir_node_type_alu:
         {
            ppir_alu_node *alu = ppir_node_to_alu(node);
            for (int i = 0; i < alu->num_src; i++) {
               ppir_reg *reg = get_src_reg(alu->src + i);
               if (reg && node->instr->seq > reg->live_out)
                  reg->live_out = node->instr->seq;
            }
            break;
         }
         case ppir_node_type_store:
         {
            ppir_store_node *store = ppir_node_to_store(node);
            ppir_reg *reg = get_src_reg(&store->src);
            if (reg && node->instr->seq > reg->live_out)
               reg->live_out = node->instr->seq;
            break;
         }
         case ppir_node_type_load:
         {
            ppir_load_node *load = ppir_node_to_load(node);
            ppir_reg *reg = get_src_reg(&load->src);
            if (reg && node->instr->seq > reg->live_out)
               reg->live_out = node->instr->seq;
            break;
         }
         case ppir_node_type_load_texture:
         {
            ppir_load_texture_node *load_tex = ppir_node_to_load_texture(node);
            ppir_reg *reg = get_src_reg(&load_tex->src_coords);
            if (reg && node->instr->seq > reg->live_out)
               reg->live_out = node->instr->seq;
            break;
         }
         default:
            break;
         }
      }
   }

   return ret;
}

static int get_phy_reg_index(int reg)
{
   int i;

   for (i = 0; i < ppir_ra_reg_class_num; i++) {
      if (reg < ppir_ra_reg_base[i + 1]) {
         reg -= ppir_ra_reg_base[i];
         break;
      }
   }

   if (i < ppir_ra_reg_class_head_vec1)
      return reg / (4 - i) * 4 + reg % (4 - i);
   else
      return reg * 4;
}

static void ppir_regalloc_print_result(ppir_compiler *comp)
{
   printf("======ppir regalloc result======\n");
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         printf("%03d:", instr->index);
         for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++) {
            ppir_node *node = instr->slots[i];
            if (!node)
               continue;

            printf(" (%d|", node->index);

            ppir_dest *dest = ppir_node_get_dest(node);
            if (dest)
               printf("%d", ppir_target_get_dest_reg_index(dest));

            printf("|");

            switch (node->type) {
            case ppir_node_type_alu:
            {
               ppir_alu_node *alu = ppir_node_to_alu(node);
               for (int j = 0; j < alu->num_src; j++) {
                  if (j)
                     printf(" ");

                  printf("%d", ppir_target_get_src_reg_index(alu->src + j));
               }
               break;
            }
            case ppir_node_type_store:
            {
               ppir_store_node *store = ppir_node_to_store(node);
               printf("%d", ppir_target_get_src_reg_index(&store->src));
               break;
            }
            case ppir_node_type_load:
            {
               ppir_load_node *load = ppir_node_to_load(node);
               if (!load->num_components)
                  printf("%d", ppir_target_get_src_reg_index(&load->src));
               break;
            }
            case ppir_node_type_load_texture:
            {
               ppir_load_texture_node *load_tex = ppir_node_to_load_texture(node);
               printf("%d", ppir_target_get_src_reg_index(&load_tex->src_coords));
               break;
            }
            default:
               break;
            }

            printf(")");
         }
         printf("\n");
      }
   }
   printf("--------------------------\n");
}

static bool create_new_instr_after(ppir_block *block, ppir_instr *ref,
                                   ppir_node *node)
{
   ppir_instr *newinstr = ppir_instr_create(block);
   if (unlikely(!newinstr))
      return false;

   list_del(&newinstr->list);
   list_add(&newinstr->list, &ref->list);

   if (!ppir_instr_insert_node(newinstr, node))
      return false;

   list_for_each_entry_from(ppir_instr, instr, ref, &block->instr_list, list) {
      instr->seq++;
   }
   newinstr->seq = ref->seq+1;
   newinstr->scheduled = true;
   return true;
}

static bool create_new_instr_before(ppir_block *block, ppir_instr *ref,
                                    ppir_node *node)
{
   ppir_instr *newinstr = ppir_instr_create(block);
   if (unlikely(!newinstr))
      return false;

   list_del(&newinstr->list);
   list_addtail(&newinstr->list, &ref->list);

   if (!ppir_instr_insert_node(newinstr, node))
      return false;

   list_for_each_entry_from(ppir_instr, instr, ref, &block->instr_list, list) {
      instr->seq++;
   }
   newinstr->seq = ref->seq-1;
   newinstr->scheduled = true;
   return true;
}

static ppir_alu_node* ppir_update_spilled_src(ppir_compiler *comp,
                                              ppir_block *block,
                                              ppir_node *node, ppir_src *src,
                                              ppir_alu_node *move_alu)
{
   /* alu nodes may have multiple references to the same value.
    * try to avoid unnecessary loads for the same alu node by
    * saving the node resulting from the temporary load */
   if (move_alu)
      goto update_src;

   /* alloc new node to load value */
   ppir_node *load_node = ppir_node_create(block, ppir_op_load_temp, -1, 0);
   if (!load_node)
      return NULL;
   list_addtail(&load_node->list, &node->list);

   ppir_load_node *load = ppir_node_to_load(load_node);

   load->index = -comp->prog->stack_size; /* index sizes are negative */
   load->num_components = src->reg->num_components;

   ppir_dest *ld_dest = &load->dest;
   ld_dest->type = ppir_target_pipeline;
   ld_dest->pipeline = ppir_pipeline_reg_uniform;
   ld_dest->write_mask = 0xf;

   create_new_instr_before(block, node->instr, load_node);

   /* Create move node */
   ppir_node *move_node = ppir_node_create(block, ppir_op_mov, -1 , 0);
   if (unlikely(!move_node))
      return false;
   list_addtail(&move_node->list, &node->list);

   move_alu = ppir_node_to_alu(move_node);

   move_alu->num_src = 1;
   move_alu->src->type = ppir_target_pipeline;
   move_alu->src->pipeline = ppir_pipeline_reg_uniform;
   for (int i = 0; i < 4; i++)
      move_alu->src->swizzle[i] = i;

   ppir_dest *alu_dest = &move_alu->dest;
   alu_dest->type = ppir_target_ssa;
   alu_dest->ssa.num_components = 4;
   alu_dest->ssa.live_in = INT_MAX;
   alu_dest->ssa.live_out = 0;
   alu_dest->write_mask = 0xf;

   list_addtail(&alu_dest->ssa.list, &comp->reg_list);

   if (!ppir_instr_insert_node(load_node->instr, move_node))
      return false;

   /* insert the new node as predecessor */
   ppir_node_foreach_pred_safe(node, dep) {
      ppir_node *pred = dep->pred;
      ppir_node_remove_dep(dep);
      ppir_node_add_dep(load_node, pred);
   }
   ppir_node_add_dep(node, move_node);
   ppir_node_add_dep(move_node, load_node);

update_src:
   /* switch node src to use the new ssa instead */
   src->type = ppir_target_ssa;
   src->ssa = &move_alu->dest.ssa;

   return move_alu;
}

static ppir_reg *create_reg(ppir_compiler *comp, int num_components)
{
   ppir_reg *r = rzalloc(comp, ppir_reg);
   if (!r)
      return NULL;

   r->num_components = num_components;
   r->live_in = INT_MAX;
   r->live_out = 0;
   r->is_head = false;
   list_addtail(&r->list, &comp->reg_list);

   return r;
}

static bool ppir_update_spilled_dest(ppir_compiler *comp, ppir_block *block,
                                     ppir_node *node, ppir_dest *dest)
{
   assert(dest != NULL);
   ppir_reg *reg = NULL;
   if (dest->type == ppir_target_register) {
      reg = dest->reg;
      reg->num_components = 4;
      reg->spilled = true;
   }
   else {
      reg = create_reg(comp, 4);
      reg->spilled = true;
      list_del(&dest->ssa.list);
   }

   /* alloc new node to load value */
   ppir_node *load_node = ppir_node_create(block, ppir_op_load_temp, -1, 0);
   if (!load_node)
      return NULL;
   list_addtail(&load_node->list, &node->list);

   ppir_load_node *load = ppir_node_to_load(load_node);

   load->index = -comp->prog->stack_size; /* index sizes are negative */
   load->num_components = 4;

   load->dest.type = ppir_target_pipeline;
   load->dest.pipeline = ppir_pipeline_reg_uniform;
   load->dest.write_mask = 0xf;

   create_new_instr_before(block, node->instr, load_node);

   /* Create move node */
   ppir_node *move_node = ppir_node_create(block, ppir_op_mov, -1 , 0);
   if (unlikely(!move_node))
      return false;
   list_addtail(&move_node->list, &node->list);

   ppir_alu_node *move_alu = ppir_node_to_alu(move_node);

   move_alu->num_src = 1;
   move_alu->src->type = ppir_target_pipeline;
   move_alu->src->pipeline = ppir_pipeline_reg_uniform;
   for (int i = 0; i < 4; i++)
      move_alu->src->swizzle[i] = i;

   move_alu->dest.type = ppir_target_register;
   move_alu->dest.reg = reg;
   move_alu->dest.write_mask = 0x0f;

   if (!ppir_instr_insert_node(load_node->instr, move_node))
      return false;

   ppir_node_foreach_pred_safe(node, dep) {
      ppir_node *pred = dep->pred;
      ppir_node_remove_dep(dep);
      ppir_node_add_dep(load_node, pred);
   }
   ppir_node_add_dep(node, move_node);
   ppir_node_add_dep(move_node, load_node);

   dest->type = ppir_target_register;
   dest->reg = reg;

   /* alloc new node to store value */
   ppir_node *store_node = ppir_node_create(block, ppir_op_store_temp, -1, 0);
   if (!store_node)
      return false;
   list_addtail(&store_node->list, &node->list);

   ppir_store_node *store = ppir_node_to_store(store_node);

   store->index = -comp->prog->stack_size; /* index sizes are negative */
   store->num_components = 4;

   store->src.type = ppir_target_register;
   store->src.reg = dest->reg;

   /* insert the new node as successor */
   ppir_node_foreach_succ_safe(node, dep) {
      ppir_node *succ = dep->succ;
      ppir_node_remove_dep(dep);
      ppir_node_add_dep(succ, store_node);
   }
   ppir_node_add_dep(store_node, node);

   create_new_instr_after(block, node->instr, store_node);

   return true;
}

static bool ppir_regalloc_spill_reg(ppir_compiler *comp, ppir_reg *chosen)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_node, node, &block->node_list, list) {

         ppir_dest *dest = ppir_node_get_dest(node);
         ppir_reg *reg = NULL;
         if (dest) {
            if (dest->type == ppir_target_ssa)
               reg = &dest->ssa;
            else if (dest->type == ppir_target_register)
               reg = dest->reg;

            if (reg == chosen)
               ppir_update_spilled_dest(comp, block, node, dest);
         }

         switch (node->type) {
         case ppir_node_type_alu:
         {
            /* alu nodes may have multiple references to the same value.
             * try to avoid unnecessary loads for the same alu node by
             * saving the node resulting from the temporary load */
            ppir_alu_node *move_alu = NULL;
            ppir_alu_node *alu = ppir_node_to_alu(node);
            for (int i = 0; i < alu->num_src; i++) {
               reg = get_src_reg(alu->src + i);
               if (reg == chosen) {
                  move_alu = ppir_update_spilled_src(comp, block, node,
                                                     alu->src + i, move_alu);
               }
            }
            break;
         }
         case ppir_node_type_store:
         {
            ppir_store_node *store = ppir_node_to_store(node);
            reg = get_src_reg(&store->src);
            if (reg == chosen) {
               ppir_update_spilled_src(comp, block, node, &store->src, NULL);
            }
            break;
         }
         case ppir_node_type_load:
         {
            ppir_load_node *load = ppir_node_to_load(node);
            reg = get_src_reg(&load->src);
            if (reg == chosen) {
               ppir_update_spilled_src(comp, block, node, &load->src, NULL);
            }
            break;
         }
         case ppir_node_type_load_texture:
         {
            ppir_load_texture_node *load_tex = ppir_node_to_load_texture(node);
            reg = get_src_reg(&load_tex->src_coords);
            if (reg == chosen) {
               ppir_update_spilled_src(comp, block, node, &load_tex->src_coords,
                                       NULL);
            }
            break;
         }
         default:
            break;
         }
      }
   }

   return true;
}

static ppir_reg *ppir_regalloc_choose_spill_node(ppir_compiler *comp,
                                                 struct ra_graph *g)
{
   int max_range = -1;
   ppir_reg *chosen = NULL;

   list_for_each_entry(ppir_reg, reg, &comp->reg_list, list) {
      int range = reg->live_out - reg->live_in;

      if (!reg->spilled && reg->live_out != INT_MAX && range > max_range) {
         chosen = reg;
         max_range = range;
      }
   }

   if (chosen)
      chosen->spilled = true;

   return chosen;
}

static void ppir_regalloc_reset_liveness_info(ppir_compiler *comp)
{
   list_for_each_entry(ppir_reg, reg, &comp->reg_list, list) {
      reg->live_in = INT_MAX;
      reg->live_out = 0;
   }
}

int lima_ppir_force_spilling = 0;

static bool ppir_regalloc_prog_try(ppir_compiler *comp, bool *spilled)
{
   ppir_reg *end_reg;

   ppir_regalloc_reset_liveness_info(comp);
   end_reg = ppir_regalloc_build_liveness_info(comp);

   struct ra_graph *g = ra_alloc_interference_graph(
      comp->ra, list_length(&comp->reg_list));

   int n = 0, end_reg_index = 0;
   list_for_each_entry(ppir_reg, reg, &comp->reg_list, list) {
      int c = ppir_ra_reg_class_vec1 + (reg->num_components - 1);
      if (reg->is_head)
         c += 4;
      if (reg == end_reg)
         end_reg_index = n;
      ra_set_node_class(g, n++, c);
   }

   int n1 = 0;
   list_for_each_entry(ppir_reg, reg1, &comp->reg_list, list) {
      int n2 = n1 + 1;
      list_for_each_entry_from(ppir_reg, reg2, reg1->list.next,
                               &comp->reg_list, list) {
         bool interference = false;
         if (reg1->live_in < reg2->live_in) {
            if (reg1->live_out > reg2->live_in)
               interference = true;
         }
         else if (reg1->live_in > reg2->live_in) {
            if (reg2->live_out > reg1->live_in)
               interference = true;
         }
         else
            interference = true;

         if (interference)
            ra_add_node_interference(g, n1, n2);

         n2++;
      }
      n1++;
   }

   ra_set_node_reg(g, end_reg_index, ppir_ra_reg_base[ppir_ra_reg_class_vec4]);

   *spilled = false;
   bool ok = ra_allocate(g);
   if (!ok || (comp->force_spilling-- > 0)) {
      ppir_reg *chosen = ppir_regalloc_choose_spill_node(comp, g);
      if (chosen) {
         /* stack_size will be used to assemble the frame reg in lima_draw.
          * It is also be used in the spilling code, as negative indices
          * starting from -1, to create stack addresses. */
         comp->prog->stack_size++;
         ppir_regalloc_spill_reg(comp, chosen);
         /* Ask the outer loop to call back in. */
         *spilled = true;

         ppir_debug("ppir: spilled register\n");
         goto err_out;
      }

      ppir_error("ppir: regalloc fail\n");
      goto err_out;
   }

   n = 0;
   list_for_each_entry(ppir_reg, reg, &comp->reg_list, list) {
      int reg_index = ra_get_node_reg(g, n++);
      reg->index = get_phy_reg_index(reg_index);
   }

   ralloc_free(g);

   if (lima_debug & LIMA_DEBUG_PP)
      ppir_regalloc_print_result(comp);

   return true;

err_out:
   ralloc_free(g);
   return false;
}

bool ppir_regalloc_prog(ppir_compiler *comp)
{
   bool spilled = false;
   comp->prog->stack_size = 0;

   /* Set from an environment variable to force spilling
    * for debugging purposes, see lima_screen.c */
   comp->force_spilling = lima_ppir_force_spilling;

   ppir_regalloc_update_reglist_ssa(comp);

   /* this will most likely succeed in the first
    * try, except for very complicated shaders */
   while (!ppir_regalloc_prog_try(comp, &spilled))
      if (!spilled)
         return false;

   return true;
}
