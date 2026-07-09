/*
 * Copyright (C) 2020 Collabora, Ltd.
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright © 2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/u_memory.h"
#include "compiler.h"

void
bi_liveness_ins_update_ssa(BITSET_WORD *live, const bi_instr *I)
{
   bi_foreach_dest(I, d)
      BITSET_CLEAR(live, I->dest[d].value);

   bi_foreach_ssa_src(I, s) {
      /* If the source is not live after this instruction, but becomes live
       * at this instruction, this is the use that kills the source
       */
      I->src[s].kill_ssa = !BITSET_TEST(live, I->src[s].value);
      BITSET_SET(live, I->src[s].value);
   }
}

void
bi_compute_liveness_ssa(bi_context *ctx)
{
   u_worklist worklist;
   u_worklist_init(&worklist, ctx->num_blocks, NULL);

   /* Free any previous liveness, and allocate */
   unsigned words = BITSET_WORDS(ctx->ssa_alloc);

   bi_foreach_block(ctx, block) {
      ralloc_free(block->ssa_live_in);
      ralloc_free(block->ssa_live_out);

      block->ssa_live_in = rzalloc_array(block, BITSET_WORD, words);
      block->ssa_live_out = rzalloc_array(block, BITSET_WORD, words);

      bi_worklist_push_head(&worklist, block);
   }

   /* Iterate the work list */
   while (!u_worklist_is_empty(&worklist)) {
      /* Pop in reverse order since liveness is a backwards pass */
      bi_block *blk = bi_worklist_pop_head(&worklist);

      /* Update its liveness information */
      __bitset_copy(blk->ssa_live_in, blk->ssa_live_out, words);

      bi_foreach_instr_in_block_rev(blk, I) {
         /* Phi nodes are handled separately, so we skip them. As phi nodes are
          * at the beginning and we're iterating backwards, we stop as soon as
          * we hit a phi node.
          */
         if (I->op == BI_OPCODE_PHI)
            break;

         blk->ssa_max_live =
            MAX2(__bitset_count(blk->ssa_live_in, words), blk->ssa_max_live);
         bi_liveness_ins_update_ssa(blk->ssa_live_in, I);
      }

      /* Propagate the live in of the successor (blk) to the live out of
       * predecessors.
       *
       * Phi nodes are logically on the control flow edge and act in parallel.
       * To handle when propagating, we kill writes from phis and make live the
       * corresponding sources.
       */
      bi_foreach_predecessor(blk, pred) {
         BITSET_WORD *live = ralloc_array(blk, BITSET_WORD, words);
         __bitset_copy(live, blk->ssa_live_in, words);

         /* Kill write */
         bi_foreach_phi_in_block(blk, I) {
            BITSET_CLEAR(live, I->dest[0].value);
         }

         /* Make live the corresponding source */
         bi_foreach_phi_in_block(blk, I) {
            bi_index operand = I->src[bi_predecessor_index(blk, *pred)];
            if (bi_is_ssa(operand)) {
               BITSET_SET(live, operand.value);
               I->src[bi_predecessor_index(blk, *pred)].kill_ssa = false;
            }
         }

         BITSET_WORD progress = 0;

         for (unsigned i = 0; i < words; ++i) {
            progress |= live[i] & ~((*pred)->ssa_live_out[i]);
            (*pred)->ssa_live_out[i] |= live[i];
         }

         if (progress != 0)
            bi_worklist_push_tail(&worklist, *pred);
      }
   }

   u_worklist_fini(&worklist);
}
