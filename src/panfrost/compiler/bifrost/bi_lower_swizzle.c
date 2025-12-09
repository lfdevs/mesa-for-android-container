/*
 * Copyright (C) 2020 Collabora Ltd.
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
#include "bi_swizzles.h"
#include "compiler.h"
#include "valhall.h"
#include "panfrost/lib/pan_props.h"

/* Not all 8-bit and 16-bit instructions support all swizzles on all sources.
 * These passes, intended to run after NIR->BIR but before scheduling/RA, lower
 * away swizzles that cannot be represented. In the future, we should try to
 * recombine swizzles where we can as an optimization.
 */

static uint32_t
va_op_swizzles(enum bi_opcode op, unsigned src)
{
   /* This is a bifrost-only instruction that is lowered on valhall */
   if (!valhall_opcodes[op].exact)
      return bi_op_swizzles[op][src];

   uint32_t swizzles = 0;
   struct va_src_info info = va_src_info(op, src);

   if (info.swizzle) {
      assert(info.size == VA_SIZE_16 || info.size == VA_SIZE_32);
      if (info.size == VA_SIZE_16)
         swizzles |= (1 << BI_SWIZZLE_H00) | (1 << BI_SWIZZLE_H10) |
                     (1 << BI_SWIZZLE_H01) | (1 << BI_SWIZZLE_H11);
      else if (info.size == VA_SIZE_32)
         swizzles |= (1 << BI_SWIZZLE_H01) | (1 << BI_SWIZZLE_H0) |
                     (1 << BI_SWIZZLE_H1);
   }

   if (info.lane) {
      if (info.size == VA_SIZE_8)
         swizzles |= (1 << BI_SWIZZLE_B0) | (1 << BI_SWIZZLE_B1) |
                     (1 << BI_SWIZZLE_B2) | (1 << BI_SWIZZLE_B3);
      if (info.size == VA_SIZE_16)
         swizzles |= (1 << BI_SWIZZLE_H0) | (1 << BI_SWIZZLE_H1);
   }

   if (info.lanes) {
      assert(info.size == VA_SIZE_8);
      swizzles |= (1 << BI_SWIZZLE_B00) | (1 << BI_SWIZZLE_B11) |
                  (1 << BI_SWIZZLE_B22) | (1 << BI_SWIZZLE_B33);
   }

   if (info.halfswizzle) {
      assert(info.size == VA_SIZE_8);
      swizzles |= (1 << BI_SWIZZLE_B00) | (1 << BI_SWIZZLE_B10) |
                  (1 << BI_SWIZZLE_B20) | (1 << BI_SWIZZLE_B30) |
                  (1 << BI_SWIZZLE_B01) | (1 << BI_SWIZZLE_B11) |
                  (1 << BI_SWIZZLE_B21) | (1 << BI_SWIZZLE_B31) |
                  (1 << BI_SWIZZLE_B02) | (1 << BI_SWIZZLE_B12) |
                  (1 << BI_SWIZZLE_B22) | (1 << BI_SWIZZLE_B32) |
                  (1 << BI_SWIZZLE_B03) | (1 << BI_SWIZZLE_B13) |
                  (1 << BI_SWIZZLE_B23) | (1 << BI_SWIZZLE_B33);
   }

   if (info.widen) {
      if (info.size == VA_SIZE_8)
         swizzles |= (1 << BI_SWIZZLE_B0123) | (1 << BI_SWIZZLE_B3210) |
                     (1 << BI_SWIZZLE_B0101) | (1 << BI_SWIZZLE_B2323) |
                     (1 << BI_SWIZZLE_B0000) | (1 << BI_SWIZZLE_B1111) |
                     (1 << BI_SWIZZLE_B2222) | (1 << BI_SWIZZLE_B3333) |
                     (1 << BI_SWIZZLE_B2301) | (1 << BI_SWIZZLE_B1032) |
                     (1 << BI_SWIZZLE_B0011) | (1 << BI_SWIZZLE_B2233);
      else if (info.size == VA_SIZE_16)
         swizzles |= (1 << BI_SWIZZLE_H00) | (1 << BI_SWIZZLE_H10) |
                     (1 << BI_SWIZZLE_H01) | (1 << BI_SWIZZLE_H11) |
                     (1 << BI_SWIZZLE_B00) | (1 << BI_SWIZZLE_B11) |
                     (1 << BI_SWIZZLE_B22) | (1 << BI_SWIZZLE_B33) |
                     (1 << BI_SWIZZLE_B01) | (1 << BI_SWIZZLE_B20) |
                     (1 << BI_SWIZZLE_B02) | (1 << BI_SWIZZLE_B31) |
                     (1 << BI_SWIZZLE_B13) | (1 << BI_SWIZZLE_B23);
      else if (info.size == VA_SIZE_32)
         swizzles |= (1 << BI_SWIZZLE_H01) | (1 << BI_SWIZZLE_H0) |
                     (1 << BI_SWIZZLE_H1) | (1 << BI_SWIZZLE_B0) |
                     (1 << BI_SWIZZLE_B1) | (1 << BI_SWIZZLE_B2) |
                     (1 << BI_SWIZZLE_B3);
   }

   if (info.combine) {
      assert(info.size == VA_SIZE_32);
      swizzles |= (1 << BI_SWIZZLE_H01) | (1 << BI_SWIZZLE_H0) | (1 << BI_SWIZZLE_H1);
   }

   if (swizzles == 0)
      swizzles = 1 << BI_SWIZZLE_H01;

   return swizzles;
}

static void
lower_swizzle(bi_context *ctx, bi_instr *ins, unsigned src)
{
   /* We don't want to deal with reswizzling logic in modifier prop. Move
    * the swizzle outside, it's easier for clamp propagation. */
   if (ins->op == BI_OPCODE_FCLAMP_V2F16) {
      bi_builder b = bi_init_builder(ctx, bi_after_instr(ins));
      bi_index dest = ins->dest[0];
      bi_index tmp = bi_temp(ctx);

      bi_index swizzled_src = bi_replace_index(ins->src[0], tmp);
      ins->src[0].swizzle = BI_SWIZZLE_H01;
      ins->dest[0] = tmp;
      bi_swz_v2i16_to(&b, dest, swizzled_src);
      return;
   }

   uint32_t supported_swizzles = pan_arch(ctx->inputs->gpu_id) >= 9 ?
      va_op_swizzles(ins->op, src) : bi_op_swizzles[ins->op][src];
   if (supported_swizzles & (1 << ins->src[src].swizzle))
      return;

   /* First, try to apply a given swizzle to a constant to clear the
    * runtime swizzle. This is less heavy-handed than ignoring the
    * swizzle for scalar destinations, since it maintains
    * replication of the destination.
    */
   if (ins->src[src].type == BI_INDEX_CONSTANT) {
      ins->src[src].value =
         bi_apply_swizzle(ins->src[src].value, ins->src[src].swizzle);
      ins->src[src].swizzle = BI_SWIZZLE_H01;
      return;
   }

   /* Even if the source does not replicate, if the consuming instruction
    * produces a 16-bit scalar, we can ignore the other component.
    */
   if (ins->dest[0].swizzle == BI_SWIZZLE_H00 &&
       ins->src[src].swizzle == BI_SWIZZLE_H00) {
      ins->src[src].swizzle = BI_SWIZZLE_H01;
      return;
   }

   /* Lower it away */
   bi_builder b = bi_init_builder(ctx, bi_before_instr(ins));

   bool is_8 = (bi_get_opcode_props(ins)->size == BI_SIZE_8) ||
               (bi_get_opcode_props(ins)->size == BI_SIZE_32 &&
                ins->src[src].swizzle >= BI_SWIZZLE_B0000);

   bi_index orig = ins->src[src];
   bi_index stripped = bi_replace_index(bi_null(), orig);
   stripped.swizzle = ins->src[src].swizzle;

   bi_index swz = is_8 ? bi_swz_v4i8(&b, stripped) : bi_swz_v2i16(&b, stripped);

   bi_replace_src(ins, src, swz);
   ins->src[src].swizzle = BI_SWIZZLE_H01;
}

static bool
bi_instr_replicates(bi_instr *I, BITSET_WORD *replicates_16)
{
   switch (I->op) {

   /* Instructions that construct vectors have replicated output if their
    * sources are identical. Check this case first.
    */
   case BI_OPCODE_MKVEC_V2I16:
   case BI_OPCODE_V2F32_TO_V2F16:
      return bi_is_value_equiv(I->src[0], I->src[1]);

   case BI_OPCODE_V2F16_TO_V2S16:
   case BI_OPCODE_V2F16_TO_V2U16:
   case BI_OPCODE_V2S16_TO_V2F16:
   case BI_OPCODE_V2S8_TO_V2F16:
   case BI_OPCODE_V2S8_TO_V2S16:
   case BI_OPCODE_V2U16_TO_V2F16:
   case BI_OPCODE_V2U8_TO_V2F16:
   case BI_OPCODE_V2U8_TO_V2U16:
      return true;

   /* 16-bit transcendentals are defined to output zero in their
    * upper half, so they do not replicate
    */
   case BI_OPCODE_FRCP_F16:
   case BI_OPCODE_FRSQ_F16:
      return false;

   /* Not sure, be conservative, we don't use these.. */
   case BI_OPCODE_VN_ASST1_F16:
   case BI_OPCODE_FPCLASS_F16:
   case BI_OPCODE_FPOW_SC_DET_F16:
      return false;

   default:
      break;
   }

   /* Replication analysis only makes sense for ALU instructions */
   if (bi_get_opcode_props(I)->message != BIFROST_MESSAGE_NONE)
      return false;

   /* We only analyze 16-bit instructions for 16-bit replication. We could
    * maybe do better.
    */
   if (bi_get_opcode_props(I)->size != BI_SIZE_16)
      return false;

   bi_foreach_src(I, s) {
      if (bi_is_null(I->src[s]))
         continue;

      /* Replicated swizzles */
      if (bi_swizzle_replicates_16(I->src[s].swizzle))
         continue;

      /* Replicated values */
      if (bi_is_ssa(I->src[s]) && BITSET_TEST(replicates_16, I->src[s].value))
         continue;

      /* Replicated constants */
      if (I->src[s].type == BI_INDEX_CONSTANT &&
          (I->src[s].value & 0xFFFF) == (I->src[s].value >> 16))
         continue;

      return false;
   }

   return true;
}

void
bi_lower_swizzle(bi_context *ctx)
{
   bi_foreach_instr_global_safe(ctx, ins) {
      bi_foreach_src(ins, s) {
         if (bi_is_null(ins->src[s]))
            continue;
         if (ins->src[s].swizzle == BI_SWIZZLE_H01)
            continue;

         lower_swizzle(ctx, ins, s);
      }
   }

   /* Now that we've lowered swizzles, clean up the mess */
   BITSET_WORD *replicates_16 = calloc(sizeof(bi_index), ctx->ssa_alloc);

   bi_foreach_instr_global(ctx, ins) {
      if (ins->nr_dests && bi_instr_replicates(ins, replicates_16))
         BITSET_SET(replicates_16, ins->dest[0].value);

      if (ins->op == BI_OPCODE_SWZ_V2I16 && bi_is_ssa(ins->src[0]) &&
          BITSET_TEST(replicates_16, ins->src[0].value)) {
         bi_set_opcode(ins, BI_OPCODE_MOV_I32);
         ins->src[0].swizzle = BI_SWIZZLE_H01;
      }

      /* On Valhall, if the instruction does some conversion depending on
       * swizzle, we should not touch it. */
      if (ctx->arch >= 9 && va_op_dest_modifier_does_convert(ins->op))
         continue;

      /* The above passes rely on replicating destinations.  For
       * Valhall, we will want to optimize this. For now, default
       * to Bifrost compatible behaviour.
       */
      if (ins->nr_dests)
         ins->dest[0].swizzle = BI_SWIZZLE_H01;
   }

   free(replicates_16);
}
