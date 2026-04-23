/*
 * Copyright (C) 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "bi_builder.h"
#include "bi_swizzles.h"
#include "compiler.h"
#include "valhall.h"

static bi_index
bi_i8_pair_as_i16(bi_index lo, bi_index hi)
{
   /* If they're both constants, we can fold them together */
   if (lo.type == BI_INDEX_CONSTANT && hi.type == BI_INDEX_CONSTANT) {
      uint32_t lo_val = bi_apply_swizzle(lo.value, lo.swizzle) & 0xff;
      uint32_t hi_val = bi_apply_swizzle(hi.value, hi.swizzle) & 0xff;
      return bi_imm_u16(lo_val | (hi_val << 8));
   }

   if (!bi_is_word_equiv(lo, hi))
      return bi_null();

   unsigned lo_byte = lo.swizzle - BI_SWIZZLE_B0;
   unsigned hi_byte = hi.swizzle - BI_SWIZZLE_B0;
   assert(lo_byte < 4 && hi_byte < 4);

   if ((lo_byte & 1) == 0 && hi_byte == lo_byte + 1) {
      bi_index i16 = lo;
      i16.swizzle = lo_byte == 0 ? BI_SWIZZLE_H00 : BI_SWIZZLE_H11;
      return i16;
   }

   return bi_null();
}

static void
compact_i8_constants(bi_index *src, unsigned nr_src)
{
   uint8_t values[4] = { 0, 0, 0, 0 };
   unsigned nr_values = 0;

   for (unsigned i = 0; i < nr_src; i++) {
      if (src[i].type != BI_INDEX_CONSTANT)
         continue;

      if (src[i].value == 0) {
         /* Sanitize zero swizzles */
         src[i].swizzle = BI_SWIZZLE_B0;
         continue;
      }

      /* Fold the swizzle (if any) and mask */
      src[i].value = bi_apply_swizzle(src[i].value, src[i].swizzle) & 0xff;

      unsigned v = 0;
      for (; v < nr_values; v++) {
         if (values[v] == src[i].value)
            break;
      }
      if (v == nr_values)
         values[nr_values++] = src[i].value;
   }

   /* If the only constants we found were zero, we're done */
   if (nr_values == 0)
      return;

   for (unsigned i = 0; i < nr_src; i++) {
      if (src[i].type != BI_INDEX_CONSTANT || src[i].value == 0)
         continue;

      unsigned v = 0;
      for (; v < nr_values; v++) {
         if (values[v] == src[i].value)
            break;
      }
      assert(v < nr_values);

      /* Fold two constants into one so that bi_schedule will see half the
       * number of unique constants.  We only have .b0 and .b2 swizzles on
       * MKVEC.i8v4 on Bifrost so we can't place them in b1 or b3.
       */
      uint32_t v32_lo = values[v & ~1];
      uint32_t v32_hi = values[v | 1];
      src[i] = bi_imm_u32(v32_lo | (v32_hi << 16));
      src[i].swizzle = (v & 1) ? BI_SWIZZLE_B2 : BI_SWIZZLE_B0;
   }
}

static bi_instr *
build_swz_v2i16_to(bi_builder *b, bi_index dst, bi_index src)
{
   if (src.swizzle == BI_SWIZZLE_H01)
      return bi_mov_i32_to(b, dst, src);

   /* On Valhall, we don't have SWZ.v2i16 but IADD has a swizzle */
   if (b->shader->arch >= 9)
      return bi_iadd_v2u16_to(b, dst, src, bi_zero(), false);
   else
      return bi_swz_v2i16_to(b, dst, src);
}

static bi_instr *
build_mkvec_v4i8_to(bi_builder *b, bi_index dst, const bi_index src[4])
{
   unsigned bytes[4];
   bool all_constant = true;
   for (unsigned i = 0; i < 4; i++) {
      STATIC_ASSERT(BI_SWIZZLE_B3 - BI_SWIZZLE_B0 == 3);
      assert(src[i].swizzle >= BI_SWIZZLE_B0);
      assert(src[i].swizzle <= BI_SWIZZLE_B3);
      bytes[i] = src[i].swizzle - BI_SWIZZLE_B0;
      if (src[i].type != BI_INDEX_CONSTANT)
         all_constant = false;
   }

   if (all_constant) {
      uint32_t v32 = 0;
      for (unsigned i = 0; i < 4; i++) {
         uint32_t v8 = bi_apply_swizzle(src[i].value, src[i].swizzle) & 0xff;
         v32 |= v8 << (i * 8);
      }
      return bi_mov_i32_to(b, dst, bi_imm_u32(v32));
   }

   /* Check for U8_TO_U32 */
   if (bi_is_zero(src[1]) && bi_is_zero(src[2]) && bi_is_zero(src[3]))
      return bi_u8_to_u32_to(b, dst, src[0]);

   /* Check for V2U8_TO_V2U16 */
   enum bi_swizzle swizzle = BI_SWIZZLE_B0123;
   if (bi_is_word_equiv(src[0], src[2]) &&
       bi_is_zero(src[1]) && bi_is_zero(src[3])) {
      unsigned v2u8_bytes[4] = { bytes[0], bytes[0], bytes[2], bytes[2] };
      bool valid_swizzle =
         bi_swizzle_from_byte_channels(v2u8_bytes, &swizzle);
      assert(valid_swizzle);

      bi_index v2u8_src = src[0];
      v2u8_src.swizzle = swizzle;

      return bi_v2u8_to_v2u16_to(b, dst, v2u8_src);
   }

   /* Check if we can do a swizzled MOV of some form */
   if (bi_is_word_equiv(src[0], src[1]) &&
       bi_is_word_equiv(src[0], src[2]) &&
       bi_is_word_equiv(src[0], src[3]) &&
       bi_swizzle_from_byte_channels(bytes, &swizzle)) {
      bi_index swz_src = src[0];
      swz_src.swizzle = swizzle;

      /* Check for MOV.i32 and SWZ.v2i16 */
      if (swizzle == BI_SWIZZLE_H00 ||
          swizzle == BI_SWIZZLE_H01 ||
          swizzle == BI_SWIZZLE_H10 ||
          swizzle == BI_SWIZZLE_H11)
         return build_swz_v2i16_to(b, dst, swz_src);

      if (b->shader->arch >= 9) {
         /* On v9 and v10, LSHIFT_OR.v4i8 has a limited swizzle */
         if (bi_op_supports_swizzle(BI_OPCODE_LSHIFT_OR_V4I8, 0, swizzle,
                                    b->shader->arch)) {
            return bi_lshift_or_v4i8_to(b, dst, swz_src,
                                        bi_imm_u8(0), bi_imm_u8(0));
         }
      } else {
         /* Check for SWZ.v4i8 */
         if (bi_op_supports_swizzle(BI_OPCODE_SWZ_V4I8, 0, swizzle,
                                    b->shader->arch)) {
            return bi_swz_v4i8_to(b, dst, swz_src);
         }
      }
   }

   bi_index v2_lo = bi_i8_pair_as_i16(src[0], src[1]);
   bi_index v2_hi = bi_i8_pair_as_i16(src[2], src[3]);
   if (!bi_is_null(v2_lo) && !bi_is_null(v2_hi)) {
      /* Check for U16_TO_U32 */
      if (bi_is_zero(v2_hi))
         return bi_u16_to_u32_to(b, dst, v2_lo);

      /* Check for MKVEC.v2i16 */
      return bi_mkvec_v2i16_to(b, dst, v2_lo, v2_hi);
   }

   /* On Valhal+, we can do any v4i8 in two instructions */
   if (b->shader->arch >= 9) {
      if (bi_is_zero(src[2]) && bi_is_zero(src[3]))
         return bi_mkvec_v2i8_to(b, dst, src[0], src[1], bi_zero());

      if (bi_is_word_equiv(src[2], src[3]) && bytes[2] == 0 && bytes[3] == 1)
         return bi_mkvec_v2i8_to(b, dst, src[0], src[1], src[2]);

      bi_index acc = bi_mkvec_v2i8(b, src[2], src[3], bi_zero());
      return bi_mkvec_v2i8_to(b, dst, src[0], src[1], acc);
   } else {
      bi_index v4_src[4];
      for (unsigned i = 0; i < 4; i++) {
         v4_src[i] = src[i];

         /* We can only swizzle to even bytes */
         if (src[i].type != BI_INDEX_CONSTANT && (bytes[i] & 1))
            v4_src[i] = bi_byte(bi_swz_v4i8(b, v4_src[i]), 0);
      }

      compact_i8_constants(v4_src, 4);

      return bi_mkvec_v4i8_to(b, dst, v4_src[0], v4_src[1],
                              v4_src[2], v4_src[3]);
   }
}

static bi_instr *
lower_mkvec_v4i8(bi_builder *b, bi_instr *I)
{
   return build_mkvec_v4i8_to(b, I->dest[0], I->src);
}

static bi_instr *
lower_swz_v4i8(bi_builder *b, bi_instr *I)
{
   unsigned bytes[4] = {0, 0, 0, 0};
   bi_swizzle_to_byte_channels(I->src[0].swizzle, bytes);

   bi_index src[4];
   for (unsigned i = 0; i < 4; i++) {
      src[i] = I->src[0];
      src[i].swizzle = BI_SWIZZLE_B0 + bytes[i];
   }

   return build_mkvec_v4i8_to(b, I->dest[0], src);
}

static bi_instr *
lower_mkvec_v2i16(bi_builder *b, bi_instr *I)
{
   for (unsigned i = 0; i < 2; i++) {
      assert(I->src[i].swizzle == BI_SWIZZLE_H0 ||
             I->src[i].swizzle == BI_SWIZZLE_H1);
   }

   if (bi_is_word_equiv(I->src[0], I->src[1])) {
      bi_index src = I->src[0];
      src.swizzle = bi_swizzle_from_half(I->src[0].swizzle == BI_SWIZZLE_H1,
                                         I->src[1].swizzle == BI_SWIZZLE_H1);
      return build_swz_v2i16_to(b, I->dest[0], src);
   }

   if (bi_is_zero(I->src[1]))
      return bi_u16_to_u32_to(b, I->dest[0], I->src[0]);

   return NULL;
}

static bi_instr *
lower_swz_v2i16(bi_builder *b, bi_instr *I)
{
   return build_swz_v2i16_to(b, I->dest[0], I->src[0]);
}

static bi_instr *
lower(bi_builder *b, bi_instr *I)
{
   switch (I->op) {
   case BI_OPCODE_MKVEC_V2I16:
      return lower_mkvec_v2i16(b, I);

   case BI_OPCODE_MKVEC_V4I8:
      return lower_mkvec_v4i8(b, I);

   case BI_OPCODE_SWZ_V2I16:
      return lower_swz_v2i16(b, I);

   case BI_OPCODE_SWZ_V4I8:
      return lower_swz_v4i8(b, I);

   default:
      return NULL;
   }
}

void
bi_lower_mkvec_swz(bi_context *ctx)
{
   bi_foreach_instr_global_safe(ctx, I) {
      bi_builder b = bi_init_builder(ctx, bi_before_instr(I));

      if (lower(&b, I))
         bi_remove_instruction(I);
   }
}
