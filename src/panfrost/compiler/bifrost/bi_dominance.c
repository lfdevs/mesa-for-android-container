/*
 * Copyright © 2014 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "compiler.h"

/*
 * Implements the algorithms for computing the dominance tree and the
 * dominance frontier from "A Simple, Fast Dominance Algorithm" by Cooper,
 * Harvey, and Kennedy.
 */

static bool
init_block(bi_block *block, bi_context *ctx)
{
   if (block == bi_entry_block(ctx))
      block->imm_dom = block;
   else
      block->imm_dom = NULL;
   block->num_dom_children = 0;

   /* See bi_block_dominates */
   block->dom_pre_index = UINT32_MAX;
   block->dom_post_index = 0;

   _mesa_set_clear(&block->dom_frontier, NULL);

   return true;
}

static bi_block *
intersect(bi_block *b1, bi_block *b2)
{
   while (b1 != b2) {
      /*
       * Note, the comparisons here are the opposite of what the paper says
       * because we index blocks from beginning -> end (i.e. reverse
       * post-order) instead of post-order like they assume.
       */
      while (b1->index > b2->index)
         b1 = b1->imm_dom;
      while (b2->index > b1->index)
         b2 = b2->imm_dom;
   }

   return b1;
}

static bool
calc_dominance(bi_block *block)
{
   bi_block *new_idom = NULL;
   bi_foreach_predecessor(block, p) {
      bi_block *pred = (*p);

      if (pred->imm_dom) {
         if (new_idom)
            new_idom = intersect(pred, new_idom);
         else
            new_idom = pred;
      }
   }

   if (block->imm_dom != new_idom) {
      block->imm_dom = new_idom;
      return true;
   }

   return false;
}

static bool
calc_dom_frontier(bi_block *block)
{
   if (block->predecessors.size > 1) {
      bi_foreach_predecessor(block, p) {
         bi_block *runner = (*p);

         /* Skip unreachable predecessors */
         if (runner->imm_dom == NULL)
            continue;

         while (runner != block->imm_dom) {
            _mesa_set_add(&runner->dom_frontier, block);
            runner = runner->imm_dom;
         }
      }
   }

   return true;
}

/*
 * Compute each node's children in the dominance tree from the immediate
 * dominator information. We do this in three stages:
 *
 * 1. Calculate the number of children each node has
 * 2. Allocate arrays, setting the number of children to 0 again
 * 3. For each node, add itself to its parent's list of children, using
 *    num_dom_children as an index - at the end of this step, num_dom_children
 *    for each node will be the same as it was at the end of step #1.
 */

static void
calc_dom_children(bi_context *ctx)
{
   void *mem_ctx = ctx;

   bi_foreach_block(ctx, block) {
      if (block->imm_dom)
         block->imm_dom->num_dom_children++;
   }

   bi_foreach_block(ctx, block) {
      if (!block->num_dom_children) {
         block->dom_children = NULL;
         continue;
      }

      if (block->num_dom_children <= 3) {
         block->dom_children = block->_dom_children_storage;
      } else {
         block->dom_children =
            ralloc_array(mem_ctx, bi_block *, block->num_dom_children);
      }
      block->num_dom_children = 0;
   }

   bi_foreach_block(ctx, block) {
      if (block->imm_dom) {
         block->imm_dom->dom_children[block->imm_dom->num_dom_children++] =
            block;
      }
   }
}

static void
calc_dfs_indices(bi_block *block, uint32_t *index)
{
   /* UINT32_MAX has special meaning. See bi_block_dominates. */
   assert(*index < UINT32_MAX - 2);

   block->dom_pre_index = (*index)++;

   for (unsigned i = 0; i < block->num_dom_children; i++)
      calc_dfs_indices(block->dom_children[i], index);

   block->dom_post_index = (*index)++;
}

void
bi_calc_dominance(bi_context *ctx)
{
   bi_foreach_block(ctx, block) {
      init_block(block, ctx);
   }

   bool progress = true;
   while (progress) {
      progress = false;
      bi_foreach_block(ctx, block) {
         if (block != bi_entry_block(ctx))
            progress |= calc_dominance(block);
      }
   }

   bi_foreach_block(ctx, block) {
      calc_dom_frontier(block);
   }

   bi_block *start_block = bi_entry_block(ctx);
   start_block->imm_dom = NULL;

   calc_dom_children(ctx);

   uint32_t dfs_index = 1;
   calc_dfs_indices(start_block, &dfs_index);
}

/**
 * Returns true if parent dominates child according to the following
 * definition:
 *
 *    "The block A dominates the block B if every path from the start block
 *    to block B passes through A."
 *
 * This means, in particular, that any unreachable block is dominated by every
 * other block and an unreachable block does not dominate anything except
 * another unreachable block.
 */
bool
bi_block_dominates(bi_block *parent, bi_block *child)
{
   /* If a block is unreachable, then nir_block::dom_pre_index == UINT32_MAX
    * and nir_block::dom_post_index == 0.  This allows us to trivially handle
    * unreachable blocks here with zero extra work.
    */
   return child->dom_pre_index >= parent->dom_pre_index &&
          child->dom_post_index <= parent->dom_post_index;
}
