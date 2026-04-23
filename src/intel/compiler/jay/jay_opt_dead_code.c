/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/bitset.h"
#include "jay_builder.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

static void
pass(jay_function *f)
{
   BITSET_WORD *live_set = BITSET_CALLOC(f->ssa_alloc);

   jay_foreach_inst_in_func_safe_rev(f, block, I) {
      /* TODO: Allow for atomics? */
      if (!BITSET_TEST_COUNT(live_set, jay_base_index(I->dst),
                             jay_num_values(I->dst)) &&
          I->op != JAY_OPCODE_SEND) {

         if (I->predication == JAY_PREDICATED_DEFAULT && !jay_is_null(I->dst)) {
            jay_shrink_sources(I, I->num_srcs - 1);
            I->predication = JAY_PREDICATED;
         }

         I->dst = jay_null();
      }

      if (!jay_is_null(I->cond_flag) &&
          !BITSET_TEST(live_set, jay_index(I->cond_flag)) &&
          (I->op != JAY_OPCODE_CMP || jay_is_null(I->dst))) {

         I->cond_flag = jay_null();
         I->conditional_mod = 0;
      }

      bool no_dest = jay_is_null(I->dst) && jay_is_null(I->cond_flag);
      bool side_effects = jay_opcode_infos[I->op].side_effects;

      if (no_dest && !side_effects) {
         jay_remove_instruction(I);
      } else {
         jay_foreach_src_index(I, s, _, index) {
            BITSET_SET(live_set, index);
         }
      }
   }

   /* Eliminate phis. This step may leave dead code but it's good enough in
    * practice since NIR already eliminated dead phis.
    */
   jay_foreach_block(f, block) {
      jay_foreach_phi_src_in_block(block, I) {
         if (!BITSET_TEST(live_set, jay_phi_src_index(I))) {
            jay_remove_instruction(I);
         }
      }
   }

   free(live_set);
}

JAY_DEFINE_FUNCTION_PASS(jay_opt_dead_code, pass)
