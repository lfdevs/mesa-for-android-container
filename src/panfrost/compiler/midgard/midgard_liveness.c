/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2019-2020 Collabora, Ltd.
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
 */

#include "compiler.h"

/* Routines for liveness analysis. Liveness is tracked per byte per node. Per
 * byte granularity is necessary for proper handling of int8 */

static void
mir_liveness_gen(uint16_t *live, unsigned node, unsigned max, uint16_t mask)
{
   if (node >= max)
      return;

   live[node] |= mask;
}

static void
mir_liveness_kill(uint16_t *live, unsigned node, unsigned max, uint16_t mask)
{
   if (node >= max)
      return;

   live[node] &= ~mask;
}

static bool
mir_liveness_get(uint16_t *live, unsigned node, uint16_t max)
{
   if (node >= max)
      return false;

   return live[node];
}

void
mir_liveness_ins_update(uint16_t *live, const midgard_instruction *ins,
                        unsigned max)
{
   /* live_in[s] = GEN[s] + (live_out[s] - KILL[s]) */

   mir_liveness_kill(live, ins->dest, max, mir_bytemask(ins));

   mir_foreach_src(ins, src) {
      unsigned node = ins->src[src];
      unsigned bytemask = mir_bytemask_of_read_components(ins, node);

      mir_liveness_gen(live, node, max, bytemask);
   }
}

/* live_out[s] = sum { p in succ[s] } ( live_in[p] ) */

static void
liveness_block_live_out(midgard_block *blk, unsigned temp_count)
{
   mir_foreach_successor(blk, succ) {
      for (unsigned i = 0; i < temp_count; ++i)
         blk->live_out[i] |= succ->live_in[i];
   }
}

/* Liveness analysis is a backwards-may dataflow analysis pass. Within a block,
 * we compute live_out from live_in. The intrablock pass is linear-time. It
 * returns whether progress was made. */

static bool
liveness_block_update(midgard_block *blk, unsigned temp_count)
{
   bool progress = false;

   liveness_block_live_out(blk, temp_count);

   uint16_t *live = ralloc_array(blk, uint16_t, temp_count);
   memcpy(live, blk->live_out, temp_count * sizeof(uint16_t));

   mir_foreach_instr_in_block_rev(blk, ins)
      mir_liveness_ins_update(live, (midgard_instruction *)ins, temp_count);

   /* To figure out progress, diff live_in */

   for (unsigned i = 0; (i < temp_count) && !progress; ++i)
      progress |= (blk->live_in[i] != live[i]);

   ralloc_free(blk->live_in);
   blk->live_in = live;

   return progress;
}

/* Once liveness data is no longer valid, call this */

static void
mir_free_liveness(compiler_context *ctx)
{
   mir_foreach_block(ctx, _block) {
      midgard_block *block = (midgard_block *)_block;

      ralloc_free(block->live_in);
      ralloc_free(block->live_out);

      block->live_in = NULL;
      block->live_out = NULL;
   }
}

/* Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */

void
mir_compute_liveness(compiler_context *ctx)
{
   /* If we already have fresh liveness, nothing to do */
   if (ctx->metadata & MIDGARD_METADATA_LIVENESS)
      return;

   mir_compute_temp_count(ctx);

   /* Set of midgard_block */
   struct set *work_list =
      _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

   struct set *visited =
      _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

   /* Free any previous liveness, and allocate */

   mir_free_liveness(ctx);

   mir_foreach_block(ctx, _block) {
      midgard_block *block = (midgard_block *)_block;
      block->live_in = rzalloc_array(block, uint16_t, ctx->temp_count);
      block->live_out = rzalloc_array(block, uint16_t, ctx->temp_count);
   }

   /* Initialize the work list with the exit block */
   struct set_entry *cur;

   cur = _mesa_set_add(work_list, mir_exit_block(&ctx->blocks));

   /* Iterate the work list */

   do {
      /* Pop off a block */
      midgard_block *blk = (struct midgard_block *)cur->key;
      _mesa_set_remove(work_list, cur);

      /* Update its liveness information */
      bool progress = liveness_block_update(blk, ctx->temp_count);

      /* If we made progress, we need to process the predecessors */

      if (progress || !_mesa_set_search(visited, blk)) {
         mir_foreach_predecessor(blk, pred)
            _mesa_set_add(work_list, pred);
      }

      _mesa_set_add(visited, blk);
   } while ((cur = _mesa_set_next_entry(work_list, NULL)) != NULL);

   _mesa_set_destroy(visited, NULL);
   _mesa_set_destroy(work_list, NULL);

   /* Liveness is now valid */
   ctx->metadata |= MIDGARD_METADATA_LIVENESS;
}

void
mir_invalidate_liveness(compiler_context *ctx)
{
   /* If we didn't already compute liveness, there's nothing to do */
   if (!(ctx->metadata & MIDGARD_METADATA_LIVENESS))
      return;

   mir_free_liveness(ctx);

   /* It's now invalid regardless */
   ctx->metadata &= ~MIDGARD_METADATA_LIVENESS;
}

bool
mir_is_live_after(compiler_context *ctx, const midgard_block *block,
                  const midgard_instruction *start, int src)
{
   mir_compute_liveness(ctx);

   /* Check whether we're live in the successors */

   if (mir_liveness_get(block->live_out, src, ctx->temp_count))
      return true;

   /* Check the rest of the block for liveness */

   mir_foreach_instr_in_block_from(block, ins, mir_next_op(start)) {
      if (mir_has_arg(ins, src))
         return true;
   }

   return false;
}
