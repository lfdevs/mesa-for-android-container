/*
 * Copyright 2023-2024 Alyssa Rosenzweig
 * Copyright 2023-2024 Valve Corporation
 * Copyright 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "util/list.h"
#include "util/set.h"
#include "util/u_memory.h"
#include "bifrost_compile.h"
#include "bifrost_nir.h"
#include "compiler.h"

/*
 * RA treats the nesting counter, the divergent shuffle temporary, and the
 * spiller temporaries as alive throughout if used anywhere. This could be
 * optimized. Using a single power-of-two reserved region at the start ensures
 * these registers are never shuffled.
 */
static unsigned
reserved_size(bi_context *ctx)
{
   if (ctx->has_spill_pcopy_reserved)
      return 8;
   else
      return 0;
}

/*
 * Calculate register demand in registers, while gathering widths and
 * classes. Becuase we allocate in SSA, this calculation is exact in
 * linear-time. Depends on SSA liveness information.
 */
unsigned
bi_calc_register_demand(bi_context *ctx)
{
   /* Print detailed demand calculation, helpful to debug spilling */
   bool debug = false;

   if (debug) {
      bi_print_shader(ctx, stdout);
   }

   uint8_t *widths = calloc(ctx->ssa_alloc, sizeof(uint8_t));
   enum ra_class *classes = calloc(ctx->ssa_alloc, sizeof(enum ra_class));

   bi_foreach_instr_global(ctx, I) {
      bi_foreach_ssa_dest(I, d) {
         unsigned v = I->dest[d].value;
         assert(widths[v] == 0 && "broken SSA");
         /* Round up vectors for easier live range splitting */
         widths[v] = bi_count_write_registers(I, d);
         classes[v] = ra_class_for_index(I->dest[d]);
      }
   }
   /* now that we know the rest of the sizes, find the sizes for PHI nodes */
   bi_foreach_block(ctx, block) {
      bi_foreach_phi_in_block(block, I) {
         if (I->dest[0].type != BI_INDEX_NORMAL)
            continue;
         unsigned idx = I->dest[0].value;
         widths[idx] = 1;
         bi_foreach_ssa_src(I, s) {
            widths[idx] = MAX2(widths[idx], widths[I->src[s].value]);
         }
      }
   }

   /* Calculate demand at the start of each block based on live-in, then update
    * for each instruction processed. Calculate rolling maximum.
    */
   unsigned max_demand = 0;

   bi_foreach_block(ctx, block) {
      unsigned demand = reserved_size(ctx);

      /* Everything live-in */
      {
         int i;
         BITSET_FOREACH_SET(i, block->ssa_live_in, ctx->ssa_alloc) {
            if (classes[i] == RA_GPR)
               demand += widths[i];
         }
      }

      max_demand = MAX2(demand, max_demand);

      /* To handle non-power-of-two vectors, sometimes live range splitting
       * needs extra registers for 1 instruction. This counter tracks the number
       * of registers to be freed after 1 extra instruction.
       */
      unsigned late_kill_count = 0;

      if (debug) {
         printf("\n");
      }

      bi_foreach_instr_in_block(block, I) {
         /* Phis happen in parallel and are already accounted for in the live-in
          * set, just skip them so we don't double count.
          */
         if (I->op == BI_OPCODE_PHI)
            continue;

         if (debug) {
            printf("%u: ", demand);
            bi_print_instr(I, stdout);
         }

         /* Handle late-kill registers from last instruction */
         demand -= late_kill_count;
         late_kill_count = 0;

         /* Kill sources the first time we see them */
         bi_foreach_src(I, s) {
            if (!I->src[s].kill_ssa)
               continue;
            assert(I->src[s].type == BI_INDEX_NORMAL);
            if (ra_class_for_index(I->src[s]) != RA_GPR)
               continue;

            bool skip = false;

            for (unsigned backwards = 0; backwards < s; ++backwards) {
               if (bi_is_equiv(I->src[backwards], I->src[s])) {
                  skip = true;
                  break;
               }
            }

            if (!skip)
               demand -= widths[I->src[s].value];
         }

         /* Make destinations live */
         bi_foreach_ssa_dest(I, d) {
            if (ra_class_for_index(I->dest[d]) != RA_GPR)
               continue;

            /* Live range splits allocate at power-of-two granularity. Round up
             * destination sizes (temporarily) to powers-of-two.
             */
            unsigned real_width = widths[I->dest[d].value];
            unsigned pot_width = util_next_power_of_two(real_width);

            demand += pot_width;
            late_kill_count += (pot_width - real_width);
         }

         max_demand = MAX2(demand, max_demand);
      }

      demand -= late_kill_count;
   }

   free(widths);
   free(classes);
   return max_demand;
}
