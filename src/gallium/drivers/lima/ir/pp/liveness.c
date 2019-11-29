/*
 * Copyright (c) 2019 Lima Project
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

static void
ppir_liveness_setup_def_use(ppir_compiler *comp)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_instr, instr, &block->instr_list, list) {
         for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++) {
            ppir_node *node = instr->slots[i];
            if (!node)
               continue;
            switch (node->op) {
            case ppir_op_const:
               continue;
            default:
               break;
            }

            for (int i = 0; i < ppir_node_get_src_num(node); i++) {
               ppir_src *src = ppir_node_get_src(node, i);
               if (!src)
                  continue;
               ppir_reg *reg = ppir_src_get_reg(src);
               if (!reg)
                  continue;

               reg->live_in = MIN2(reg->live_in, instr->seq);
               reg->live_out = MAX2(reg->live_out, instr->seq);

               if (BITSET_TEST(block->def, reg->regalloc_index))
                  continue;
               BITSET_SET(block->use, reg->regalloc_index);
            }

            ppir_dest *dest = ppir_node_get_dest(node);
            if (!dest)
               continue;
            ppir_reg *reg = ppir_dest_get_reg(dest);
            if (!reg)
               continue;

            reg->live_in = MIN2(reg->live_in, instr->seq);
            reg->live_out = MAX2(reg->live_out, instr->seq);

            if (BITSET_TEST(block->use, reg->regalloc_index))
               continue;
            BITSET_SET(block->def, reg->regalloc_index);
         }
      }
   }
}

static bool
ppir_liveness_setup_live_in_out(ppir_compiler *comp, int bitset_words)
{
   bool cont = false;
   list_for_each_entry_rev(ppir_block, block, &comp->block_list, list) {
      /* Update live_out: Any successor using the variable
       * on entrance needs us to have the variable live on
       * exit.
       */
      for (int i = 0; i < 2; i++) {
         ppir_block *succ = block->successors[i];
         if (!succ)
            continue;
         for (int i = 0; i < bitset_words; i++) {
            BITSET_WORD new_live_out = (succ->live_in[i] &
                                        ~block->live_out[i]);
            if (new_live_out) {
               block->live_out[i] |= new_live_out;
               cont = true;
            }
         }
      }
      /* Update live_in */
      for (int i = 0; i < bitset_words; i++) {
         BITSET_WORD new_live_in = (block->use[i] |
                                    (block->live_out[i] &
                                     ~block->def[i]));
         if (new_live_in & ~block->live_in[i]) {
            block->live_in[i] |= new_live_in;
            cont = true;
         }
      }
   }

   return cont;
}

static void
ppir_liveness_compute_start_end(ppir_compiler *comp)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      list_for_each_entry(ppir_reg, reg, &comp->reg_list, list) {
         if (!list_length(&block->instr_list))
            continue;

         if (BITSET_TEST(block->live_in, reg->regalloc_index)) {
            ppir_instr *first = list_first_entry(&block->instr_list,
                                   ppir_instr, list);
            reg->live_in = MIN2(reg->live_in, first->seq);
            reg->live_out = MAX2(reg->live_out, first->seq);
         }

         if (BITSET_TEST(block->live_out, reg->regalloc_index)) {
            ppir_instr *last = list_last_entry(&block->instr_list,
                                  ppir_instr, list);
            reg->live_in = MIN2(reg->live_in, last->seq);
            reg->live_out = MAX2(reg->live_out, last->seq);
         }
      }
   }
}

/* Liveness analysis is based on https://en.wikipedia.org/wiki/Live_variable_analysis
 * 1) Compute def and use for each block. 'Def' is variables that are set
 *    before they are read in block, 'set' is variables that are read before
 *    they're set in the block. Initial live_in and live_out values are set
 *    accordingly.
 * 2) Compute live_in and live_out of blocks:
 *    live_in(block) = use(block) + (live_out(block) - set(block))
 *    live_out(block) = live_in(successors[0]) + live_in(successors[1])
 *    Loop walks blocks in reverse order and computes live_in/live_out of each
 *    block, loop is terminated when no live_in or live_out is updated.
 * 3) Adjust live_in and live_out of variables to block boundaries if they
 *    appear in live_in or live_out.
 */
void
ppir_liveness_analysis(ppir_compiler *comp)
{
   int bitset_words = BITSET_WORDS(list_length(&comp->reg_list));

   ppir_liveness_setup_def_use(comp);

   while (ppir_liveness_setup_live_in_out(comp, bitset_words))
      ;

   ppir_liveness_compute_start_end(comp);
}
