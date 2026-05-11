/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "jay_ir.h"
#include "jay_opcodes.h"

static unsigned
max_simd_width(const jay_shader *shader, const jay_inst *I)
{
   /* Only certain "complex" quad swizzles require splitting down to SIMD4 */
   if (I->op == JAY_OPCODE_QUAD_SWIZZLE &&
       (jay_quad_swizzle_swizzle(I) == JAY_QUAD_SWIZZLE_XYXY ||
        jay_quad_swizzle_swizzle(I) == JAY_QUAD_SWIZZLE_ZWZW)) {
      return 4;
   }

   /* These special instructions need to be split for various reasons. */
   if (I->op == JAY_OPCODE_EXPAND_QUAD ||
       I->op == JAY_OPCODE_EXTRACT_LAYER ||
       I->op == JAY_OPCODE_EXTRACT_BYTE_PER_8LANES ||
       I->op == JAY_OPCODE_OFFSET_PACKED_PIXEL_COORDS ||
       I->op == JAY_OPCODE_DESWIZZLE_ODD ||
       I->op == JAY_OPCODE_MUL_32 ||
       I->op == JAY_OPCODE_SHUFFLE) {
      return 16;
   }

   if (I->op != JAY_OPCODE_SEND) {
      /* If any source/destination is 64-bit strided, we must split to avoid
       * crossing more than 2 GRFs. Note that SENDs don't have this restriction,
       * we don't have to split A64 load/store.
       */
      if (I->dst.file == GPR &&
          jay_def_stride(shader, I->dst) == JAY_STRIDE_8) {
         return 16;
      }

      jay_foreach_src(I, s) {
         if (I->src[s].file == GPR &&
             jay_def_stride(shader, I->src[s]) == JAY_STRIDE_8) {
            return 16;
         }
      }
   } else {
      /* TODO: Split SENDs, needs RA work */
   }

   return 32;
}

unsigned
jay_simd_split(const jay_shader *s, const jay_inst *I)
{
   unsigned actual = jay_simd_width_logical(s, I);
   unsigned max = max_simd_width(s, I);

   return (actual > max) ? (util_logbase2(actual) - util_logbase2(max)) : 0;
}
