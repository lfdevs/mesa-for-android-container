/*
 * Copyright (C) 2021 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "bi_builder.h"
#include "va_compiler.h"

/* Valhall specific instruction selection optimizations */

static enum bi_opcode
va_op_add_imm(enum bi_opcode op)
{
   switch (op) {
   case BI_OPCODE_FADD_F32:
      return BI_OPCODE_FADD_IMM_F32;
   case BI_OPCODE_FADD_V2F16:
      return BI_OPCODE_FADD_IMM_V2F16;
   case BI_OPCODE_IADD_S32:
   case BI_OPCODE_IADD_U32:
      return BI_OPCODE_IADD_IMM_I32;
   case BI_OPCODE_IADD_V2S16:
   case BI_OPCODE_IADD_V2U16:
      return BI_OPCODE_IADD_IMM_V2I16;
   case BI_OPCODE_IADD_V4S8:
   case BI_OPCODE_IADD_V4U8:
      return BI_OPCODE_IADD_IMM_V4I8;
   default:
      return 0;
   }
}

static bool
va_is_add_imm(bi_instr *I, unsigned s)
{
   assert(s < I->nr_srcs);

   return I->src[s].swizzle == BI_SWIZZLE_H01 && !I->src[s].abs &&
          !I->src[s].neg && !I->clamp && !I->round;
}

static unsigned
va_choose_imm(bi_instr *I)
{
   for (unsigned i = 0; i < 2; ++i) {
      if (I->src[i].type == BI_INDEX_CONSTANT)
         return i;
   }

   return ~0;
}

/* Lower MOV.i32 #constant --> IADD_IMM.i32 0x0, #constant */
static void
va_lower_mov_imm(bi_instr *I)
{
   assert(I->nr_srcs == 1);

   if (I->src[0].type == BI_INDEX_CONSTANT) {
      bi_set_opcode(I, BI_OPCODE_IADD_IMM_I32);
      I->index = I->src[0].value;
      I->src[0] = bi_zero();
   }
}

void
va_fuse_add_imm(bi_instr *I)
{
   if (I->op == BI_OPCODE_MOV_I32) {
      va_lower_mov_imm(I);
      return;
   }

   /* If the instruction does some conversion depending on swizzle, we should
    * not touch it unless the swizzle is H01. */
   if (va_op_dest_modifier_does_convert(I->op) &&
       I->dest->swizzle != BI_SWIZZLE_H01)
      return;

   enum bi_opcode op = va_op_add_imm(I->op);
   if (!op)
      return;

   unsigned s = va_choose_imm(I);
   if (s > 1)
      return;
   if (!va_is_add_imm(I, 1 - s))
      return;

   bi_set_opcode(I, op);
   I->index = bi_apply_swizzle(I->src[s].value, I->src[s].swizzle);

   assert(!I->src[s].abs && "redundant .abs set");

   /* If the constant is negated, flip the sign bit */
   if (I->src[s].neg) {
      if (I->op == BI_OPCODE_FADD_IMM_F32)
         I->index ^= (1u << 31);
      else if (I->op == BI_OPCODE_FADD_IMM_V2F16)
         I->index ^= (1u << 31) | (1u << 15);
      else
         UNREACHABLE("unexpected .neg");
   }

   I->src[0] = I->src[1 - s];
   bi_drop_srcs(I, 1);
}

enum va_cmp_type {
   VA_CMP_TYPE_INVALID,
   VA_CMP_TYPE_F,
   VA_CMP_TYPE_S,
   VA_CMP_TYPE_U,
};

static enum bi_opcode
va_remap_logical_to_logical_cmp(enum bi_opcode op, enum va_cmp_type type)
{
   if (type == VA_CMP_TYPE_F) {
      switch (op) {
      case BI_OPCODE_LSHIFT_OR_I32:
         return BI_OPCODE_FCMP_OR_F32;
      case BI_OPCODE_LSHIFT_OR_V2I16:
         return BI_OPCODE_FCMP_OR_V2F16;
      case BI_OPCODE_LSHIFT_AND_I32:
         return BI_OPCODE_FCMP_AND_F32;
      case BI_OPCODE_LSHIFT_AND_V2I16:
         return BI_OPCODE_FCMP_AND_V2F16;
      default:
         return 0;
      }
   } else if (type == VA_CMP_TYPE_S) {
      switch (op) {
      case BI_OPCODE_LSHIFT_OR_I32:
         return BI_OPCODE_ICMP_OR_S32;
      case BI_OPCODE_LSHIFT_OR_V2I16:
         return BI_OPCODE_ICMP_OR_V2S16;
      case BI_OPCODE_LSHIFT_OR_V4I8:
         return BI_OPCODE_ICMP_OR_V4S8;
      case BI_OPCODE_LSHIFT_AND_I32:
         return BI_OPCODE_ICMP_AND_S32;
      case BI_OPCODE_LSHIFT_AND_V2I16:
         return BI_OPCODE_ICMP_AND_V2S16;
      case BI_OPCODE_LSHIFT_AND_V4I8:
         return BI_OPCODE_ICMP_AND_V4S8;
      default:
         return 0;
      }
   } else if (type == VA_CMP_TYPE_U) {
      switch (op) {
      case BI_OPCODE_LSHIFT_OR_I32:
         return BI_OPCODE_ICMP_OR_U32;
      case BI_OPCODE_LSHIFT_OR_V2I16:
         return BI_OPCODE_ICMP_OR_V2U16;
      case BI_OPCODE_LSHIFT_OR_V4I8:
         return BI_OPCODE_ICMP_OR_V4U8;
      case BI_OPCODE_LSHIFT_AND_I32:
         return BI_OPCODE_ICMP_AND_U32;
      case BI_OPCODE_LSHIFT_AND_V2I16:
         return BI_OPCODE_ICMP_AND_V2U16;
      case BI_OPCODE_LSHIFT_AND_V4I8:
         return BI_OPCODE_ICMP_AND_V4U8;
      default:
         return 0;
      }
   }

   assert(0 && "invalid va_cmp_type");
   return 0;
}

static bool
va_cmp_can_fuse(enum bi_opcode op)
{
   /* We only allow fusing with OR variants */
   switch (op) {
   case BI_OPCODE_FCMP_OR_F32:
   case BI_OPCODE_FCMP_OR_V2F16:
   case BI_OPCODE_ICMP_OR_S32:
   case BI_OPCODE_ICMP_OR_V2S16:
   case BI_OPCODE_ICMP_OR_V4S8:
   case BI_OPCODE_ICMP_OR_U32:
   case BI_OPCODE_ICMP_OR_V2U16:
   case BI_OPCODE_ICMP_OR_V4U8:
      return true;
   default:
      return false;
   }
}

static enum va_cmp_type
va_cmp_opcode_to_cmp_type(enum bi_opcode op)
{
   switch (op) {
   case BI_OPCODE_FCMP_AND_F32:
   case BI_OPCODE_FCMP_AND_V2F16:
   case BI_OPCODE_FCMP_OR_F32:
   case BI_OPCODE_FCMP_OR_V2F16:
      return VA_CMP_TYPE_F;
   case BI_OPCODE_ICMP_AND_S32:
   case BI_OPCODE_ICMP_AND_V2S16:
   case BI_OPCODE_ICMP_OR_S32:
   case BI_OPCODE_ICMP_OR_V2S16:
   case BI_OPCODE_ICMP_OR_V4S8:
      return VA_CMP_TYPE_S;
   case BI_OPCODE_ICMP_AND_U32:
   case BI_OPCODE_ICMP_AND_V2U16:
   case BI_OPCODE_ICMP_OR_U32:
   case BI_OPCODE_ICMP_OR_V2U16:
   case BI_OPCODE_ICMP_OR_V4U8:
      return VA_CMP_TYPE_U;
   default:
      return VA_CMP_TYPE_INVALID;
   }
}

/* LSHIFT_X_F32(FCMP_OR_F32(a, b, 0), FCMP_Y_F32(c, d, e), 0) -> FCMP_X_F32(a,
 * b, FCMP_Y_F32(c, d, e))) */
static bool
va_fuse_cmp(bi_context *ctx, bi_instr **lut, const BITSET_WORD *multiple,
            bi_instr *I)
{
   /* Expect SSA values on other sources */
   if (I->nr_srcs != 3 || !bi_is_ssa(I->src[0]) || !bi_is_ssa(I->src[1]))
      return false;

   bi_instr *src0_ins = lut[I->src[0].value];
   bi_instr *src1_ins = lut[I->src[1].value];

   enum va_cmp_type cmp_type = va_cmp_opcode_to_cmp_type(src0_ins->op);

   /* Expect both side to use the same form type */
   if (cmp_type == VA_CMP_TYPE_INVALID ||
       cmp_type != va_cmp_opcode_to_cmp_type(src1_ins->op))
      return false;

   /* Expect both side to use the same result type */
   if (src0_ins->result_type != src1_ins->result_type)
      return false;

   /* Ensure we really have a LSHIFT that we can remap (so without shift) */
   if (!va_remap_logical_to_logical_cmp(I->op, cmp_type) ||
       !bi_is_zero(I->src[2]))
      return false;

   bi_instr *old_ins;
   bi_index src2;

   /* Try to fuse general case of LSHIFT_X_F32(FCMP_OR_F32(a, b, 0),
    * FCMP_Y_F32(c, d, e), 0), otherwise try to fuse LSHIFT_OR_F32(FCMP_Y_F32(c,
    * d, e), FCMP_OR_F32(a, b, 0), 0) */
   if (va_cmp_can_fuse(src0_ins->op) &&
       !BITSET_TEST(multiple, src0_ins->dest[0].value) &&
       bi_is_zero(src0_ins->src[2])) {
      old_ins = src0_ins;
      src2 = src1_ins->dest[0];
   } else if ((I->op == BI_OPCODE_LSHIFT_OR_I32 ||
               I->op == BI_OPCODE_LSHIFT_OR_V2I16) &&
              va_cmp_can_fuse(src1_ins->op) &&
              !BITSET_TEST(multiple, src1_ins->dest[0].value) &&
              bi_is_zero(src1_ins->src[2])) {
      old_ins = src1_ins;
      src2 = src0_ins->dest[0];
   } else {
      return false;
   }

   /* Replace old LSHIFT logic op with the CMP with correct logical op and
    * accumulate other src */
   bi_builder b = bi_init_builder(ctx, bi_before_instr(I));
   bi_instr *new_ins =
      bi_fcmp_or_f32_to(&b, I->dest[0], old_ins->src[0], old_ins->src[1], src2,
                        old_ins->cmpf, old_ins->result_type);
   bi_set_opcode(new_ins, va_remap_logical_to_logical_cmp(I->op, cmp_type));

   /* Remove the old instructions */
   lut[old_ins->dest[0].value] = NULL;
   lut[new_ins->dest[0].value] = new_ins;
   bi_remove_instruction(old_ins);
   bi_remove_instruction(I);
   return true;
}

static bool
va_propagate_replicate_wide(bi_context *ctx, bi_instr **lut, bi_instr *I)
{
   struct va_opcode_info info = valhall_opcodes[I->op];
   bool progress = false;

   bi_foreach_ssa_src(I, s) {
      if (!info.srcs[s].widen)
         continue;

      bi_index *src = &I->src[s];
      bi_instr *src_ins = lut[src->value];

      assert(src_ins && "src has no corresponding instruction");

      bi_index new_src = bi_null();
      unsigned tmp[4];

      /* If we have a MKVEC.v2i8 and current instruction only replicate, we
       * should propagate */
      if (src_ins->op == BI_OPCODE_MKVEC_V2I8 &&
          bi_swizzle_replicates_8(src->swizzle) &&
          bi_swizzle_to_byte_channels(src->swizzle, tmp)) {
         unsigned byte_idx = *tmp;

         /* In case of the top 16-bit, src2 contains the value we want without
          * any swizzles */
         if (byte_idx >= 2) {
            /* src2 should not have non identity swizzle */
            assert(src_ins->src[2].swizzle == BI_SWIZZLE_H01);

            new_src = src_ins->src[2];
            new_src.swizzle = BI_SWIZZLE_B0 + (byte_idx - 2);
         } else {
            new_src = src_ins->src[byte_idx];
         }
      }
      /* In case of 16-bit source, attempt to propagate trivial conversions from
         8-bit */
      else if (bi_swizzle_replicates_16(src->swizzle) &&
               !bi_swizzle_replicates_8(src->swizzle) &&
               ((src_ins->op == BI_OPCODE_V2S8_TO_V2S16 && info.is_signed) ||
                (src_ins->op == BI_OPCODE_V2U8_TO_V2U16 && !info.is_signed)) &&
               bi_swizzle_replicates_8(src_ins->src[0].swizzle)) {
         new_src = src_ins->src[0];
      }

      if (!bi_is_null(new_src)) {
         *src = new_src;
         progress = true;
      }
   }

   return progress;
}

static void
va_optimize_forward(bi_context *ctx)
{
   bool progress;

   do {
      progress = false;

      unsigned count = ctx->ssa_alloc;
      bi_instr **lut = rzalloc_array(ctx, bi_instr *, count);
      bi_instr **uses = rzalloc_array(ctx, bi_instr *, count);
      BITSET_WORD *multiple = BITSET_RZALLOC(ctx, count);

      if (!lut || !uses || !multiple)
         goto out;

      /* Record usage across blocks */
      bi_foreach_block(ctx, block) {
         bi_foreach_instr_in_block(block, I) {
            bi_foreach_dest(I, d) {
               lut[I->dest[d].value] = I;
            }

            bi_foreach_ssa_src(I, s) {
               bi_record_use(uses, multiple, I, s);
            }
         }
      }

      bi_foreach_instr_global_safe(ctx, I) {
         progress |= va_propagate_replicate_wide(ctx, lut, I);
         progress |= va_fuse_cmp(ctx, lut, multiple, I);
      }

   out:
      ralloc_free(uses);
      ralloc_free(lut);
      ralloc_free(multiple);
   } while (progress);
}

void
va_optimize(bi_context *ctx)
{
   bi_foreach_instr_global(ctx, I) {
      va_fuse_add_imm(I);
   }

   va_optimize_forward(ctx);
}
