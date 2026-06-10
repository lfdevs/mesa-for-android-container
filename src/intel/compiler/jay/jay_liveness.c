/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "util/macros.h"
#include "util/sparse_bitset.h"
#include "util/u_worklist.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/* LiveIn = GEN + (LiveOut - KILL) */
static void
update_liveness_for_inst(BITSET_WORD *dead_defs,
                         struct u_sparse_bitset *live_in,
                         jay_inst *I)
{
   /* No destination is live-in before the instruction, but any destination not
    * live-in after is immediately dead.
    */
   jay_foreach_dst_index(I, _, def) {
      if (u_sparse_bitset_test(live_in, def)) {
         u_sparse_bitset_clear(live_in, def);
      } else {
         BITSET_SET(dead_defs, def);
      }
   }

   if (I->op == JAY_OPCODE_PHI_SRC) {
      /* Phi sources do not require last-use bits. */
      jay_foreach_src_index(I, src_idx, comp, index) {
         u_sparse_bitset_set(live_in, index);
      }
   } else {
      BITSET_ZERO(I->last_use);
      unsigned last_use_i = 0;

      jay_foreach_src_index(I, s, comp, index) {
         /* If the source is not live after this instruction, but becomes
          * live at this instruction, this is the last use.
          */
         if (!u_sparse_bitset_test(live_in, index)) {
            assert(last_use_i < JAY_NUM_LAST_USE_BITS);
            BITSET_SET(I->last_use, last_use_i);
         }

         u_sparse_bitset_set(live_in, index);
         ++last_use_i;
      }
   }
}

/**
 * Calculate liveness information for SSA values.
 *
 * This populates the jay_block::live_in/live_out bitsets and last_use flags.
 */
void
jay_compute_liveness(jay_function *f)
{
   u_worklist worklist;
   u_worklist_init(&worklist, f->num_blocks, NULL);

   ralloc_free(f->dead_defs);
   f->dead_defs = BITSET_RZALLOC(f, f->ssa_alloc);
   BITSET_WORD *uniform = BITSET_CALLOC(f->ssa_alloc);

   jay_foreach_block(f, block) {
      u_sparse_bitset_free(&block->live_out);
      u_sparse_bitset_init(&block->live_out, f->ssa_alloc, block);

      jay_worklist_push_head(&worklist, block);
   }

   jay_foreach_inst_in_func(f, _, I) {
      jay_foreach_dst_index(I, dst, index) {
         if (jay_is_uniform(dst)) {
            BITSET_SET(uniform, index);
         }
      }
   }

   while (!u_worklist_is_empty(&worklist)) {
      /* Pop in reverse order since liveness is a backwards pass */
      jay_block *block = jay_worklist_pop_head(&worklist);

      /* Update its liveness information:
       * 1. Assume everything liveout from this block was live_in
       * 2. Clear live_in for anything defined in this block
       */
      u_sparse_bitset_free(&block->live_in);
      u_sparse_bitset_dup(&block->live_in, &block->live_out);

      jay_foreach_inst_in_block_rev(block, inst) {
         update_liveness_for_inst(f->dead_defs, &block->live_in, inst);
      }

      /* Propagate block->live_in[] to the live_out[] of predecessors. Since
       * phis are split, they are handled naturally without special cases.
       *
       * The physical control flow graph is a subset of the logical control flow
       * graph. So, edges that are in both can use the fast merge, and other
       * edges are physical-only and need to merge only UGPRs.
       */
      jay_foreach_predecessor(block, p, UGPR) {
         bool progress = false;

         if (jay_cfg_has_edge(*p, block, GPR)) {
            progress = u_sparse_bitset_merge(&(*p)->live_out, &block->live_in);
         } else {
            U_SPARSE_BITSET_FOREACH_SET(&block->live_in, i) {
               if (BITSET_TEST(uniform, i)) {
                  progress |= !u_sparse_bitset_test(&(*p)->live_out, i);
                  u_sparse_bitset_set(&(*p)->live_out, i);
               }
            }
         }

         if (progress) {
            jay_worklist_push_tail(&worklist, *p);
         }
      }
   }

#ifndef NDEBUG
   jay_block *first_block = jay_first_block(f);
   jay_block *last_block = list_last_entry(&f->blocks, jay_block, link);

   assert(u_sparse_bitset_count(&first_block->live_in) == 0 && "invariant");
   assert(u_sparse_bitset_count(&last_block->live_out) == 0 && "invariant");
#endif

   u_worklist_fini(&worklist);
   free(uniform);
}

/*
 * Calculate the register demand for each SSA file using the previously
 * calculated liveness analysis. SSA makes this exact in linear-time.
 */
void
jay_calculate_register_demands(jay_function *func)
{
   enum jay_file *files = calloc(func->ssa_alloc, sizeof(enum jay_file));
   unsigned *max_demand = func->demand;
   memset(max_demand, 0, sizeof(func->demand));

   jay_foreach_inst_in_func(func, block, I) {
      jay_foreach_dst_index(I, def, index) {
         files[index] = def.file;
      }
   }

   /* Model the implicit demand from preloading inputs. In a function like:
    *
    *    %0 = preload r10
    *    %1 = add %0, %0
    *    return %1
    *
    * ...the "true" register demand is 1, but we need to clamp the demand to 11
    * to make sure r10 actually exists.
    */
   jay_foreach_preload(func, I) {
      uint32_t max = jay_preload_reg(I) + jay_num_values(I->dst);
      max_demand[I->dst.file] = MAX2(max_demand[I->dst.file], max);
   }

   jay_foreach_block(func, block) {
      unsigned demands[JAY_NUM_SSA_FILES] = {};

      /* Everything live-in. */
      U_SPARSE_BITSET_FOREACH_SET(&block->live_in, i) {
         ++demands[files[i]];
      }

      jay_foreach_ssa_file(f) {
         max_demand[f] = MAX2(demands[f], max_demand[f]);
      }

      jay_foreach_inst_in_block(block, I) {
         /* Make destinations live */
         jay_foreach_dst(I, d) {
            demands[d.file] += jay_num_values(d);
         }

         /* Update maximum demands */
         jay_foreach_ssa_file(f) {
            max_demand[f] = MAX2(demands[f], max_demand[f]);
         }

         /* Dead destinations are those written by the instruction but killed
          * immediately after the instruction finishes.
          */
         jay_foreach_dst_index(I, d, index) {
            if (BITSET_TEST(func->dead_defs, index)) {
               assert(demands[d.file] > 0);
               --demands[d.file];
            }
         }

         /* Late-kill sources. Duplicated sources are only marked killed once,
          * so we do not need to filter out duplicates.
          */
         jay_foreach_killed(I, s, c) {
            assert(demands[I->src[s].file] > 0);
            --demands[I->src[s].file];
         }

         if (jay_debug & JAY_DBG_PRINTDEMAND) {
            printf("(LA) [G:%u\tU:%u] ", demands[GPR], demands[UGPR]);
            jay_print_inst(stdout, I);
         }
      }

      jay_foreach_ssa_file(f) {
         block->demand_max[f] = max_demand[f];
         block->demand_out[f] = demands[f];
      }
   }

   free(files);
}
