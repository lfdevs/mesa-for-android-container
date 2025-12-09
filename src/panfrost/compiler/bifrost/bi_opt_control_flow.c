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

#include "util/set.h"
#include "compiler.h"

/* A simple block is defined here as:
 * - a block with only 1 predecessor and 1 successor
 * - a block with no instruction or a single branch instruction (in which case
 *   the branch condition does not matter as the block has only 1 successor).
 */
static bool
is_simple_block(bi_block *block)
{
   assert(block != NULL);
   if (bi_num_successors(block) != 1 || bi_num_predecessors(block) != 1)
      return false;
   if (list_is_empty(&block->instructions))
      return true;
   if (!list_is_singular(&block->instructions))
      return false;
   return bi_is_branch(list_first_entry(&block->instructions, bi_instr, link));
}

static bi_block *
get_simple_block_predecessor(bi_block *block)
{
   assert(is_simple_block(block));
   bi_foreach_predecessor(block, pred) {
      return *pred;
   }
   /* Because block is a 'simple block' we should not be able to get here. */
   UNREACHABLE("We should not get here");
}

static void
remove_simple_block(bi_block *block)
{
   assert(is_simple_block(block));
   bi_block *pred = get_simple_block_predecessor(block);
   bi_block *succ = block->successors[0];

   uint8_t block_idx = pred->successors[0] == block ? 0 : 1;
   pred->successors[block_idx] = succ;
   if (pred->successors[block_idx ^ 0x1] == succ)
      pred->successors[1] = NULL;

   struct util_dynarray new_preds;
   util_dynarray_init(&new_preds, succ);
   bi_foreach_predecessor(succ, succ_pred) {
      bi_block *append = *succ_pred;
      /* replace 'block' by 'pred'. If 'pred' is seen, do not add it as it will
       * already be added in replacement of 'block'. */
      if (*succ_pred == block)
         append = pred;
      else if (*succ_pred == pred)
         continue;
      util_dynarray_append(&new_preds, append);
   }
   succ->predecessors = new_preds;
   block->link.prev->next = block->link.next;
   block->link.next->prev = block->link.prev;
}

static bool
inverse_cmpf(enum bi_cmpf *cond)
{
   switch (*cond) {
   case BI_CMPF_EQ:
      *cond = BI_CMPF_NE;
      break;
   case BI_CMPF_GT:
      *cond = BI_CMPF_LE;
      break;
   case BI_CMPF_GE:
      *cond = BI_CMPF_LT;
      break;
   case BI_CMPF_NE:
      *cond = BI_CMPF_EQ;
      break;
   case BI_CMPF_LT:
      *cond = BI_CMPF_GE;
      break;
   case BI_CMPF_LE:
      *cond = BI_CMPF_GT;
      break;
   case BI_CMPF_GTLT:
      return false;
   default:
      UNREACHABLE("unexpected condition");
   }
   return true;
}

static bool
try_remove_simple_block(bi_block *prev, bi_block *block, bi_block *next,
                        struct set *blocks_seen)
{
   if (block == NULL)
      return false;
   _mesa_set_add(blocks_seen, block);

   if (!is_simple_block(block))
      return false;
   bi_block *pred = get_simple_block_predecessor(block);
   bi_block *succ = block->successors[0];

   bi_instr *last_instr =
      list_last_entry(&(pred->instructions), bi_instr, link);
   bool last_instr_is_branch = bi_is_branch(last_instr);

   /* if 'succ' has already been seen, it means that the jump between 'block'
    * and 'succ' is a backward jump. This kind of jump can lead to issue
    * regarding reconvergence
    * (https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/36021#note_3020690).
    * Thus we don't remove those simple block to avoid reconvergence issue.
    */
   bool succ_seen = _mesa_set_search(blocks_seen, succ) != NULL;

   if (last_instr_is_branch && !succ_seen &&
       (last_instr->branch_target == block ||
        (pred == prev && last_instr->branch_target == next &&
         inverse_cmpf(&last_instr->cmpf)))) {
      /* Remove 'simple_block' when either:
       * - 'pred' jumps to 'simple_block'
       * - or:
       *   - 'pred' is the previous block thus it falls through 'simple block'
       *   - 'pred' jumps to 'next'
       *   - We can rework the condition to have:
       *     - 'pred' jumping to 'simple block' successor
       *     - 'pred' falls through 'next'.
       */
      last_instr->branch_target = succ;
      remove_simple_block(block);
      return true;
   } else if (!last_instr_is_branch && pred == prev && succ == next) {
      /* Remove 'simple block' when it is between its predecessor and successor
       * when predecessor fall through 'simple block' (without a branch).
       */
      remove_simple_block(block);
      return true;
   }
   return false;
}

static bool
try_remove_simple_blocks(bi_context *ctx)
{
   bool changed = false;
   struct set *blocks_seen =
      _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
   bi_block *prev = NULL;
   bi_block *block = NULL;
   bi_foreach_block(ctx, next) {
      bool removed = try_remove_simple_block(prev, block, next, blocks_seen);
      changed |= removed;
      if (!removed) {
         prev = block;
      }
      block = next;
   }
   _mesa_set_destroy(blocks_seen, NULL);
   return changed;
}

static void
remove_branch_to_next_block(bi_context *ctx)
{
   bi_block *block = NULL;
   bi_foreach_block(ctx, next) {
      if (block != NULL) {
         bi_instr *last_instr =
            list_last_entry(&(block->instructions), bi_instr, link);
         if (bi_num_successors(block) == 1 && last_instr &&
             bi_is_branch(last_instr) && last_instr->branch_target == next) {
            bi_remove_instruction(last_instr);
         }
      }
      block = next;
   }
}

void
bi_opt_control_flow(bi_context *ctx)
{
   while (try_remove_simple_blocks(ctx))
      ;
   remove_branch_to_next_block(ctx);
}
