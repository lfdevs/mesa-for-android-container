/*
 * Copyright (C) 2021 Collabora Ltd.
 * Copyright (C) 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "bi_builder.h"
#include "va_compiler.h"

/*
 * Bifrost uses split 64-bit addresses, specified as two consecutive sources.
 * Valhall uses contiguous 64-bit addresses, specified as a single source with
 * an aligned register pair. This simple pass inserts moves to lower split
 * 64-bit sources into a collect, ensuring the register constraints will be
 * respected by RA. This could be optimized, but this is deferred for now.
 */
static void
lower_split_src(bi_context *ctx, bi_instr *I, unsigned s, bi_instr** lut)
{
   /* Skip sources that are already split properly */
   bi_index offset_fau = I->src[s];
   offset_fau.offset++;

   if (I->src[s].type == BI_INDEX_FAU && I->src[s].offset == 0 &&
       bi_is_value_equiv(offset_fau, I->src[s + 1])) {
      return;
   }

   /* Check if the source regs are already coming from a split/collect pair. */
   bi_index *src_a = &I->src[s];
   bi_index *src_b = &I->src[s + 1];
   if (bi_is_ssa(*src_a) && bi_is_ssa(*src_b)) {
      bi_instr *src_ins_a = lut[src_a->value];
      bi_instr *src_ins_b = lut[src_b->value];
      if (src_ins_a->op == BI_OPCODE_SPLIT_I32 && src_ins_a == src_ins_b) {
         bi_index split_src = src_ins_a->src[0];
         if (!bi_is_ssa(split_src) ||
             lut[split_src.value]->op == BI_OPCODE_COLLECT_I32)
            return;
      }
   }

   /* Allocate temporary before the instruction */
   bi_builder b = bi_init_builder(ctx, bi_before_instr(I));
   bi_index vec = bi_temp(ctx);
   bi_instr *collect = bi_collect_i32_to(&b, vec, 2);
   bi_instr *split = bi_split_i32_to(&b, 2, vec);

   /* Emit collect */
   for (unsigned w = 0; w < 2; ++w) {
      collect->src[w] = I->src[s + w];

      split->dest[w] = bi_temp(ctx);
      I->src[s + w] = split->dest[w];
   }
}

void
va_lower_split_64bit(bi_context *ctx)
{
   unsigned count = ctx->ssa_alloc;
   bi_instr **lut = rzalloc_array(ctx, bi_instr *, count);

   if (!lut)
      return;

   /* Record producers */
   bi_foreach_block(ctx, block) {
      bi_foreach_instr_in_block(block, I) {
         bi_foreach_dest(I, d) {
            lut[I->dest[d].value] = I;
         }
      }
   }

   bi_foreach_instr_global(ctx, I) {
      bi_foreach_src(I, s) {
         if (bi_is_null(I->src[s]) || s >= 4)
            continue;

         struct va_src_info info = va_src_info(I->op, s);

         /* Only split if the instruction expects 64-bit inputs as two separate
          * sources. */
         if (info.size == VA_SIZE_64 && bi_count_read_registers(I, s) == 1)
            lower_split_src(ctx, I, s, lut);
      }
   }

   ralloc_free(lut);
}
