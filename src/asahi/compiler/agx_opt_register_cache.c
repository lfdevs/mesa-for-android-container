/*
 * Copyright 2025 Valve Corporation
 * Copyright 2019-2022 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "agx_compiler.h"
#include "shader_enums.h"

static void
postra_liveness_ins(BITSET_WORD *live, agx_instr *I)
{
   agx_foreach_reg_dest(I, d) {
      unsigned reg = I->dest[d].value;
      BITSET_CLEAR_RANGE(live, reg, reg + agx_index_size_16(I->dest[d]) - 1);
   }

   agx_foreach_reg_src(I, s) {
      unsigned reg = I->src[s].value;
      BITSET_SET_RANGE(live, reg, reg + agx_index_size_16(I->src[s]) - 1);
   }
}

/*
 * Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */
static void
postra_liveness(agx_context *ctx)
{
   u_worklist worklist;
   agx_worklist_init(ctx, &worklist);

   agx_foreach_block(ctx, block) {
      BITSET_ZERO(block->reg_live_in);
      BITSET_ZERO(block->reg_live_out);

      agx_worklist_push_tail(&worklist, block);
   }

   while (!u_worklist_is_empty(&worklist)) {
      /* Pop off in reverse order since liveness is backwards */
      agx_block *blk = agx_worklist_pop_tail(&worklist);

      /* Calculate liveness locally */
      agx_foreach_successor(blk, succ) {
         BITSET_OR(blk->reg_live_out, blk->reg_live_out, succ->reg_live_in);
      }

      BITSET_DECLARE(live, AGX_NUM_REGS);
      memcpy(live, blk->reg_live_out, sizeof(live));

      agx_foreach_instr_in_block_rev(blk, ins) {
         postra_liveness_ins(live, ins);
      }

      if (BITSET_EQUAL(blk->reg_live_in, live))
         continue;

      /* We made progress, so we need to reprocess the predecessors */
      memcpy(blk->reg_live_in, live, sizeof(live));
      agx_foreach_predecessor(blk, pred) {
         agx_worklist_push_head(&worklist, *pred);
      }
   }

   u_worklist_fini(&worklist);
}

static bool
writes_reg(const agx_instr *I, unsigned reg)
{
   agx_foreach_reg_dest(I, d) {
      unsigned count = agx_index_size_16(I->dest[d]);

      if (reg >= I->dest[d].value && (reg - I->dest[d].value) < count)
         return true;
   }

   return false;
}

/*
 * Mark last-use sources to allow the hardware to discard from the register
 * cache. Last use information follows immediately from (post-RA) liveness
 * analysis: a register is dead immediately after its last use.
 *
 * Mark cache hints on sources/destinations to encourage the hardware to make
 * better use of the register cache. This is a simple local analysis.
 */
void
agx_opt_register_cache(agx_context *ctx)
{
   /* Analyze the shader globally */
   postra_liveness(ctx);

   agx_foreach_block(ctx, block) {
      /* Live-set at each point in the program */
      BITSET_DECLARE(live, AGX_NUM_REGS);
      memcpy(live, block->reg_live_out, sizeof(live));

      /* Set of registers read "soon" by an ALU instruction. These are
       * candidates for the .cache bit.
       */
      BITSET_DECLARE(alu_reads, AGX_NUM_REGS) = {0};

      agx_foreach_instr_in_block_rev(block, I) {
         agx_foreach_reg_dest(I, d) {
            uint64_t reg = I->dest[d].value;
            unsigned nr = agx_index_size_16(I->dest[d]);

            I->dest[d].cache = BITSET_TEST(alu_reads, I->dest[d].value);
            BITSET_CLEAR_RANGE(alu_reads, reg, reg + nr - 1);
         }

         agx_foreach_reg_src(I, s) {
            I->src[s].cache = BITSET_TEST(alu_reads, I->src[s].value);
         }

         agx_foreach_reg_src(I, s) {
            uint64_t reg = I->src[s].value;
            unsigned nr = agx_index_size_16(I->src[s]);

            /* If the register dead after this instruction, it's the last use.
             * That includes if the register is overwritten this cycle, but that
             * won't show up in the liveness analysis.
             */
            bool lu = !BITSET_TEST_RANGE(live, reg, reg + nr - 1) ||
                      writes_reg(I, reg);

            /* Handling divergent blocks would require physical CFG awareness.
             * Just bail for now, skipping this pass won't affect correctness.
             */
            I->src[s].discard = lu && !block->divergent;

            /* Mark any source read by an ALU instruction in the same block as
             * wanting a .cache hint. This is better than just marking
             * everything, although it overly hints for very long blocks and
             * underhints for registers used across block boundaries. It's
             * probably good enough, though, and it's not clear how to do much
             * better given our limited understanding of the hardware.
             */
            if (agx_is_alu(I))
               BITSET_SET_RANGE(alu_reads, reg, reg + nr - 1);

            assert(!(I->src[s].discard && I->src[s].cache));
         }

         postra_liveness_ins(live, I);
      }
   }
}
