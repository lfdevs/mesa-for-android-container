/*
 * Copyright 2026 Intel Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "util/ralloc.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/* Validatation doesn't make sense in release builds */
#ifndef NDEBUG
#define NUM_VALIDATE_FILES (ACCUM + 1)

struct regfile {
   /* For each register in each file, records the SSA index currently stored
    * in that register (or zero if undefined contents).
    */
   uint32_t *r[NUM_VALIDATE_FILES];

   /* Size of each register file */
   size_t n[NUM_VALIDATE_FILES];
};

static uint32_t *
reg(struct regfile *rf, enum jay_file file, uint32_t reg)
{
   /* FLAG and UFLAG share their registers. TODO: Rework? */
   if (file == UFLAG) {
      file = FLAG;
   }

   assert(file < NUM_VALIDATE_FILES);
   assert(reg < rf->n[file]);
   return &rf->r[file][reg];
}

static uint32_t *
def_reg(struct regfile *rf, jay_def x, uint32_t component)
{
   return reg(rf, x.file, x.reg + component);
}

static void
print_regfile(struct regfile *rf, FILE *fp)
{
   fprintf(fp, "regfile: \n");
   jay_foreach_ssa_file(file) {
      for (unsigned i = 0; i < rf->n[file]; ++i) {
         uint32_t v = *reg(rf, file, i);
         const char *prefixes = "ruf"; /* XXX: share with jay_print */

         if (v) {
            fprintf(fp, "   %c%u = %u\n", prefixes[file], i, v);
         }
      }
   }
   fprintf(fp, "\n");
}

static bool
validate_src(struct jay_partition *partition,
             jay_inst *I,
             unsigned s,
             struct regfile *rf,
             jay_def def)
{
   jay_foreach_comp(def, c) {
      uint32_t actual = *def_reg(rf, def, c);

      if (def.file == GPR) {
         assert(jay_gpr_to_stride(partition, def.reg) ==
                jay_gpr_to_stride(partition, def.reg + c));
      }

      if (actual == 0 || actual != jay_channel(def, c)) {
         fprintf(stderr, "invalid RA for source %u, channel %u.\n", s, c);

         fprintf(stderr, "expected index %u but", jay_channel(def, c));
         if (actual)
            fprintf(stderr, " got index %u\n", actual);
         else
            fprintf(stderr, " register is undefined\n");

         jay_print_inst(stderr, I);
         print_regfile(rf, stderr);
         return false;
      }
   }

   return true;
}

static bool
validate_block(jay_function *func, jay_block *block, struct regfile *blocks)
{
   struct regfile *rf = &blocks[block->index];
   bool success = true;

   /* Pathological shaders can end up with loop headers that have only a
    * single predecessor and act like normal blocks. Validate them as such,
    * since RA treats them as such implicitly. Affects:
    *
    * dEQP-VK.graphicsfuzz.spv-stable-mergesort-dead-code
    */
   bool loop_header = block->loop_header && jay_num_predecessors(block) > 1;

   /* Initialize the register file based on predecessors. */
   /* Initialize with the exit state of any one predecessor */
   jay_block *first_pred = jay_first_predecessor(block);
   if (first_pred) {
      struct regfile *pred_rf = &blocks[first_pred->index];

      jay_foreach_ssa_file(f) {
         memcpy(rf->r[f], pred_rf->r[f], rf->n[f] * sizeof(uint32_t));
      }
   }

   /* TODO: Handle loop header validation better */
   if (!loop_header) {
      /* Intersect with the other predecessor. If a register has different
       * values coming in from each block, it is considered undefined at the
       * start of the block.
       */
      jay_foreach_predecessor(block, pred) {
         struct regfile *pred_rf = &blocks[(*pred)->index];

         jay_foreach_ssa_file(file) {
            for (unsigned r = 0; r < rf->n[file]; ++r) {
               if (*reg(rf, file, r) != *reg(pred_rf, file, r)) {
                  *reg(rf, file, r) = 0;
               }
            }
         }
      }
   }

   jay_foreach_inst_in_block(block, I) {
      /* Validate sources */
      jay_foreach_ssa_src(I, s) {
         if (jay_channel(I->src[s], 0) != JAY_SENTINEL) {
            success &=
               validate_src(&func->shader->partition, I, s, rf, I->src[s]);
         }
      }

      /* Record destinations */
      jay_foreach_dst(I, dst) {
         if (jay_channel(dst, 0) != JAY_SENTINEL) {
            jay_foreach_comp(dst, c) {
               *def_reg(rf, dst, c) = jay_channel(dst, c);

               if (dst.file == GPR) {
                  struct jay_partition *p = &func->shader->partition;
                  assert(jay_gpr_to_stride(p, dst.reg) ==
                         jay_gpr_to_stride(p, dst.reg + c));
               }
            }
         }
      }

      if (I->op == JAY_OPCODE_MOV &&
          jay_channel(I->dst, 0) == JAY_SENTINEL &&
          I->src[0].file < NUM_VALIDATE_FILES &&
          jay_channel(I->src[0], 0) == JAY_SENTINEL) {

         /* Lowered shuffles don't have SSA indices, handle as registers */
         assert(jay_num_values(I->dst) == jay_num_values(I->src[0]));

         jay_foreach_comp(I->dst, c) {
            *def_reg(rf, I->dst, c) = *def_reg(rf, I->src[0], c);
         }
      }
   }

   return success;
}

void
jay_validate_ra(jay_function *func)
{
   bool succ = true;
   linear_ctx *lin_ctx = linear_context(func->shader);
   struct regfile *blocks =
      linear_zalloc_array(lin_ctx, struct regfile, func->num_blocks);

   jay_foreach_block(func, block) {
      struct regfile *b = &blocks[block->index];
      assert(block->index < func->num_blocks);

      for (unsigned file = 0; file < NUM_VALIDATE_FILES; ++file) {
         b->n[file] = file == ACCUM  ? 8 / jay_grf_per_gpr(func->shader) :
                      file == UACCUM ? 4 * jay_ugpr_per_grf(func->shader) :
                                       jay_num_regs(func->shader, file);
         b->r[file] = linear_zalloc_array(lin_ctx, uint32_t, b->n[file]);
      }
   }

   jay_foreach_block(func, block) {
      succ &= validate_block(func, block, blocks);
   }

   if (!succ) {
      jay_print_func(stderr, func);
      UNREACHABLE("invalid RA");
   }

   linear_free_context(lin_ctx);
}

#endif /* NDEBUG */
