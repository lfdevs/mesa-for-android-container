/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/*
 * If default != dest, we need to lower. Predicated moves generalize as SEL,
 * with default in src0 to allow for immediates.
 *
 * For anything else, we have to insert a copy.
 */
static void
lower_non_tied_default(jay_builder *b, jay_inst *I, jay_def default_)
{
   jay_def not_pred = jay_negate(*jay_inst_get_predicate(I));
   assert(default_.file != FLAG && "we don't support this");

   if (I->op == JAY_OPCODE_MOV) {
      jay_SEL(b, I->type, I->dst, default_, I->src[0], not_pred);
      jay_remove_instruction(I);
   } else {
      jay_foreach_comp(I->dst, c) {
         jay_def dst = jay_extract_post_ra(I->dst, c);
         jay_def src = jay_extract_post_ra(default_, c);

         jay_add_predicate(b, jay_MOV(b, dst, src), not_pred);
      }
   }
}

static bool
lower(jay_builder *b, jay_inst *I)
{
   switch (I->op) {
   case JAY_OPCODE_PRELOAD:
   case JAY_OPCODE_PHI_DST:
   case JAY_OPCODE_INDETERMINATE:
      /* Delete instructions that only exist for RA. Uninitialized register
       * contents is a perfectly cromulent indeterminate value.
       */
      return true;

   case JAY_OPCODE_MOV: {
      /* Delete trivial moves */
      if (jay_regs_equal(I->dst, I->src[0]) && !I->predication)
         return true;

      if (I->dst.file == GPR && I->src[0].file == GPR) {
         jay_def dst = I->dst, src = I->src[0];
         enum jay_stride dst_stride = jay_def_stride(b->shader, dst);
         enum jay_stride src_stride = jay_def_stride(b->shader, src);

         /* Lower 4B<-->2B copies. To pack the register file, RA
          * sometimes inserts 32-bit copies involving 16-bit strided sources like
          * "mov.u32 r4 <32-bit>, r50 <16-bit>". This cannot be implemented in a
          * single hardware instruction, so we split into two 16-bit copies.
          */
         enum jay_stride min_stride = MIN2(dst_stride, src_stride);
         unsigned stride_sz = jay_stride_to_bits(min_stride);
         unsigned type_sz = jay_type_size_bits(I->type);

         if (stride_sz < type_sz) {
            assert(stride_sz == 16 && type_sz == 32 && "no other case hit");
            I->type = JAY_TYPE_U16;

            dst.hi = true;
            src.hi = true;
            jay_MOV(b, dst, src)->type = JAY_TYPE_U16;
         }
      }

      return false;
   }

   case JAY_OPCODE_ZERO_FLAG: {
      jay_MOV(b, jay_bare_reg(FLAG, jay_zero_flag_reg(I)), 0)->type =
         JAY_TYPE_U32;
      return true;
   }

   case JAY_OPCODE_DESWIZZLE: {
      unsigned size = jay_deswizzle_size(I);
      assert(b->shader->partition.blocks[GPR][0].start == 1);

      /* Odd: copy both halves to contiguous pair after payload */
      for (unsigned i = 0; i < (size / 2); ++i) {
         jay_DESWIZZLE_ODD(b, jay_bare_reg(GPR, size + i), jay_bare_reg(GPR, i),
                           jay_bare_reg(GPR, i + ((size + 1) / 2)),
                           !(size & 1));
      }

      /* Even: leave the bottom half in place, copy top half. If size=1 (rare
       * but possible), this would be a no-op move so skip it.
       */
      if (size > 1) {
         for (unsigned i = 0; i < DIV_ROUND_UP(size, 2); ++i) {
            jay_DESWIZZLE_EVEN(b, jay_bare_reg(GPR, i),
                               jay_bare_reg(GPR, (size / 2) + i), size & 1);
         }
      }

      return true;
   }

   default:
      return false;
   }
}

void
jay_lower_post_ra(jay_shader *s)
{
   jay_foreach_inst_in_shader_safe(s, func, I) {
      jay_builder b = jay_init_builder(func, jay_before_inst(I));

      if (jay_inst_has_default(I)) {
         if (!jay_regs_equal(I->dst, *jay_inst_get_default(I))) {
            lower_non_tied_default(&b, I, *jay_inst_get_default(I));
         }

         /* Now just drop the default source */
         jay_shrink_sources(I, I->num_srcs - 1);
         I->predication = JAY_PREDICATED;
      }

      if (lower(&b, I)) {
         jay_remove_instruction(I);
      }
   }
}
