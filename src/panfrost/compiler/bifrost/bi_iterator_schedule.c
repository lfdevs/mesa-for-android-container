/*
 * Copyright (C) 2025 Google LLC.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Google):
 *      Romaric Jodin <rjodin@google.com>
 */

#include "util/u_dynarray.h"
#include "compiler.h"

struct iterator {
   bi_block *block;
   bi_instr *instr;
};

static bool
phi_src_is_simple_iterator(bi_instr *phi, bi_instr *phi_src, bi_block *block)
{
   if (phi_src == NULL || block == NULL || phi_src->nr_dests != 1 ||
       phi_src->op == BI_OPCODE_PHI) {
      return false;
   }
   bi_foreach_src(phi_src, idx) {
      if (bi_is_equiv(phi->dest[0], phi_src->src[idx]))
         /* If phi_src uses phi, then phi_src is a simple iterator */
         return true;
   }
   return false;
}

static bi_instr *
get_new_iterator_position(bi_block *block, bi_instr *instr)
{
   bi_instr *prev = NULL;
   bi_foreach_instr_in_block_from(block, I, instr) {
      if (bi_is_branch(I))
         return prev;
      bi_foreach_src(I, s) {
         if (bi_is_equiv(instr->dest[0], I->src[s]))
            return prev;
      }
      prev = I;
   }
   return prev;
}

/* Try to move iterator as close to the end of the block as possible. The goal
 * is to avoid the iterator being used after being updated, to prevent the need
 * for an extra move instruction.
 *
 * Example:
 *   1 = PHI <>, 2
 *   ...
 *   2 = IADD_IMM.i32 1, index:1
 *   3 = ICMP_OR.u32.ge.m1 1, ...
 *
 * Without this pass, after register allocation it can lead to:
 *   r1 = IADD_IMM.i32 r0, index:1
 *   r2 = ICMP_OR.u32.ge.m1 r0, ...
 *   r0 = MOV.i32 r1
 *
 * With this pass, we can get:
 *   r1 = ICMP_OR.u32.ge.m1 r0, ...
 *   r0 = IADD_IMM.i32 r0, index:1
 *
 */
void
bi_iterator_schedule(bi_context *ctx)
{
   bi_instr **bi_index_to_instr = calloc(ctx->ssa_alloc, sizeof(bi_instr *));
   bi_block **bi_index_to_block = calloc(ctx->ssa_alloc, sizeof(bi_block *));
   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block(block, instr) {
         bi_foreach_ssa_dest(instr, idx) {
            uint32_t value = instr->dest[idx].value;
            bi_index_to_instr[value] = instr;
            bi_index_to_block[value] = block;
         }
      }
   }

   struct util_dynarray iterators = UTIL_DYNARRAY_INIT;
   bi_foreach_instr_global(ctx, phi) {
      if (phi->op != BI_OPCODE_PHI || phi->nr_dests != 1)
         continue;

      bi_foreach_ssa_src(phi, phi_src_idx) {
         uint32_t value = phi->src[phi_src_idx].value;
         struct iterator iterator = {.instr = bi_index_to_instr[value],
                                 .block = bi_index_to_block[value]};
         if (!phi_src_is_simple_iterator(phi, iterator.instr, iterator.block))
            continue;
         util_dynarray_append(&iterators, iterator);
      }
   }

   util_dynarray_foreach(&iterators, struct iterator, iterator)
   {
      bi_instr *instr = iterator->instr;
      bi_instr *prev = get_new_iterator_position(iterator->block, instr);
      if (prev == NULL || prev == instr)
         continue;
      bi_remove_instruction(instr);
      list_add(&instr->link, &prev->link);
   }
   util_dynarray_clear(&iterators);

   free(bi_index_to_instr);
   free(bi_index_to_block);
}
