/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright 2019-2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/sparse_bitset.h"
#include "agx_compiler.h"

/* Liveness analysis is a backwards-may dataflow analysis pass. Within a block,
 * we compute live_out from live_in. The intrablock pass is linear-time. It
 * returns whether progress was made. */

/* live_in[s] = GEN[s] + (live_out[s] - KILL[s]) */

void
agx_liveness_ins_update(struct u_sparse_bitset *live, agx_instr *I)
{
   agx_foreach_ssa_dest(I, d)
      u_sparse_bitset_clear(live, I->dest[d].value);

   agx_foreach_ssa_src(I, s) {
      /* If the source is not live after this instruction, but becomes live
       * at this instruction, this is the use that kills the source
       */
      I->src[s].kill = !u_sparse_bitset_test(live, I->src[s].value);
      u_sparse_bitset_set(live, I->src[s].value);
   }
}

/* Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */

void
agx_compute_liveness(agx_context *ctx)
{
   u_worklist worklist;
   u_worklist_init(&worklist, ctx->num_blocks, NULL);

   /* Free any previous liveness, and allocate */
   agx_foreach_block(ctx, block) {
      u_sparse_bitset_free(&block->live_in);
      u_sparse_bitset_free(&block->live_out);

      u_sparse_bitset_init(&block->live_in, ctx->alloc, block);
      u_sparse_bitset_init(&block->live_out, ctx->alloc, block);

      agx_worklist_push_head(&worklist, block);
   }

   /* Iterate the work list */
   while (!u_worklist_is_empty(&worklist)) {
      /* Pop in reverse order since liveness is a backwards pass */
      agx_block *blk = agx_worklist_pop_head(&worklist);

      /* Update its liveness information */
      u_sparse_bitset_dup(&blk->live_in, &blk->live_out);

      agx_foreach_instr_in_block_rev(blk, I) {
         if (I->op != AGX_OPCODE_PHI)
            agx_liveness_ins_update(&blk->live_in, I);
      }

      /* Propagate the live in of the successor (blk) to the live out of
       * predecessors.
       *
       * Phi nodes are logically on the control flow edge and act in parallel.
       * To handle when propagating, we kill writes from phis and make live the
       * corresponding sources.
       */
      agx_foreach_predecessor(blk, pred) {
         struct u_sparse_bitset live;
         u_sparse_bitset_dup(&live, &blk->live_in);

         /* Kill write */
         agx_foreach_phi_in_block(blk, phi) {
            assert(phi->dest[0].type == AGX_INDEX_NORMAL);
            u_sparse_bitset_clear(&live, phi->dest[0].value);
         }

         /* Make live the corresponding source */
         agx_foreach_phi_in_block(blk, phi) {
            agx_index operand = phi->src[agx_predecessor_index(blk, *pred)];
            if (operand.type == AGX_INDEX_NORMAL) {
               u_sparse_bitset_set(&live, operand.value);
               phi->src[agx_predecessor_index(blk, *pred)].kill = false;
            }
         }

         if (u_sparse_bitset_merge(&(*pred)->live_out, &live))
            agx_worklist_push_tail(&worklist, *pred);
      }
   }

   u_worklist_fini(&worklist);
}
