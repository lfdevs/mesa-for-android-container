/*
 * Copyright (C) 2025 Arm Ltd.
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
#include "valhall.h"

/* Only some special immediates are available, as specified in the Table of
 * Immediates in the specification. Other immediates must be lowered, either to
 * uniforms or to moves.
 */

static bi_index
va_mov_imm(bi_builder *b, uint32_t imm)
{
   bi_index zero = bi_fau(BIR_FAU_IMMEDIATE | 0, false);
   return bi_iadd_imm_i32(b, zero, imm);
}

static bi_index
va_lut_index_32(uint32_t imm)
{
   for (unsigned i = 0; i < ARRAY_SIZE(valhall_immediates); ++i) {
      if (valhall_immediates[i] == imm)
         return va_lut(i);
   }

   return bi_null();
}

static bi_index
va_lut_index_16(uint16_t imm)
{
   uint16_t *arr16 = (uint16_t *)valhall_immediates;

   for (unsigned i = 0; i < (2 * ARRAY_SIZE(valhall_immediates)); ++i) {
      if (arr16[i] == imm)
         return bi_half(va_lut(i >> 1), i & 1);
   }

   return bi_null();
}

UNUSED static bi_index
va_lut_index_8(uint8_t imm)
{
   uint8_t *arr8 = (uint8_t *)valhall_immediates;

   for (unsigned i = 0; i < (4 * ARRAY_SIZE(valhall_immediates)); ++i) {
      if (arr8[i] == imm)
         return bi_byte(va_lut(i >> 2), i & 3);
   }

   return bi_null();
}

static bi_index
va_demote_constant_fp16(uint32_t value)
{
   uint16_t fp16 = _mesa_float_to_half(uif(value));

   /* Only convert if it is exact */
   if (fui(_mesa_half_to_float(fp16)) == value)
      return va_lut_index_16(fp16);
   else
      return bi_null();
}

/*
 * Test if a 32-bit word arises as a sign or zero extension of some 8/16-bit
 * value.
 */
static bool
is_extension_of_8(uint32_t x, bool is_signed)
{
   if (is_signed)
      return (x <= INT8_MAX) || ((x >> 7) == BITFIELD_MASK(24 + 1));
   else
      return (x <= UINT8_MAX);
}

static bool
is_extension_of_16(uint32_t x, bool is_signed)
{
   if (is_signed)
      return (x <= INT16_MAX) || ((x >> 15) == BITFIELD_MASK(16 + 1));
   else
      return (x <= UINT16_MAX);
}

static bi_index
va_move_const_to_fau(bi_builder *b, uint32_t value)
{
   const unsigned push_base = b->shader->inputs->fau_consts.offset;
   uint32_t *values = b->shader->inputs->fau_consts.values;

   assert(b->shader->inputs->fau_consts.max_amount == 0 || values != NULL);
   assert(b->shader->fau_consts_count <=
          b->shader->inputs->fau_consts.max_amount);

   for (unsigned i = 0; i < b->shader->fau_consts_count; ++i) {
      if (values[i] == value) {
         const unsigned idx = push_base + i;
         return bi_fau((enum bir_fau)(BIR_FAU_UNIFORM | (idx >> 1)), idx & 1);
      }
   }

   if (b->shader->fau_consts_count < b->shader->inputs->fau_consts.max_amount) {
      const unsigned int idx = push_base + b->shader->fau_consts_count;
      values[b->shader->fau_consts_count++] = value;
      return bi_fau((enum bir_fau)(BIR_FAU_UNIFORM | (idx >> 1)), idx & 1);
   }

   return bi_null();
}

static bi_index
va_lookup_constant(uint32_t value, struct va_src_info info, bool is_signed)
{
   /* Try the constant as-is */
   {
      bi_index lut = va_lut_index_32(value);
      if (!bi_is_null(lut))
         return lut;

      /* ...or negated as a FP32 constant */
      if (info.absneg && info.size == VA_SIZE_32) {
         lut = bi_neg(va_lut_index_32(fui(-uif(value))));
         if (!bi_is_null(lut))
            return lut;
      }

      /* ...or negated as a FP16 constant */
      if (info.absneg && info.size == VA_SIZE_16) {
         lut = bi_neg(va_lut_index_32(value ^ 0x80008000));
         if (!bi_is_null(lut))
            return lut;
      }
   }

   /* Try using a single half of a FP16 constant */
   bool replicated_halves = (value & 0xFFFF) == (value >> 16);
   if (info.swizzle && info.size == VA_SIZE_16 && replicated_halves) {
      bi_index lut = va_lut_index_16(value & 0xFFFF);
      if (!bi_is_null(lut))
         return lut;

      /* ...possibly negated */
      if (info.absneg) {
         lut = bi_neg(va_lut_index_16((value & 0xFFFF) ^ 0x8000));
         if (!bi_is_null(lut))
            return lut;
      }
   }

   /* Try extending a byte */
   if ((info.widen || info.lanes || info.lane) &&
       is_extension_of_8(value, is_signed)) {

      bi_index lut = va_lut_index_8(value & 0xFF);
      if (!bi_is_null(lut))
         return lut;
   }

   /* Try extending a halfword */
   if (info.widen && is_extension_of_16(value, is_signed)) {

      bi_index lut = va_lut_index_16(value & 0xFFFF);
      if (!bi_is_null(lut))
         return lut;
   }

   /* Try demoting the constant to FP16 */
   if (info.swizzle && info.size == VA_SIZE_32) {
      bi_index lut = va_demote_constant_fp16(value);
      if (!bi_is_null(lut))
         return lut;

      if (info.absneg) {
         bi_index lut = bi_neg(va_demote_constant_fp16(fui(-uif(value))));
         if (!bi_is_null(lut))
            return lut;
      }
   }

   return bi_null();
}

static bi_index
va_resolve_constant(bi_builder *b, uint32_t value, struct va_src_info info,
                    bool is_signed, bool staging, bool try_move_to_fau)
{
   if (!staging) {
      bi_index lut = va_lookup_constant(value, info, is_signed);
      if (!bi_is_null(lut))
         return lut;
   }

   if (!staging && try_move_to_fau) {
      bi_index c = va_move_const_to_fau(b, value);
      if (!bi_is_null(c))
         return c;
   }

   return va_mov_imm(b, value);
}

static uint32_t
va_resolve_swizzles(bi_context *ctx, bi_instr *I, unsigned s)
{
   struct va_src_info info = va_src_info(I->op, s);
   uint32_t value = I->src[s].value;
   enum bi_swizzle swz = I->src[s].swizzle;

   /* Resolve any swizzle, keeping in mind the different interpretations
    * swizzles in different contexts.
    */
   if (info.size == VA_SIZE_32) {
      /* Extracting a half from the 32-bit value */
      if (swz == BI_SWIZZLE_H00)
         value = (value & 0xFFFF);
      else if (swz == BI_SWIZZLE_H11)
         value = (value >> 16);
      else
         assert(swz == BI_SWIZZLE_H01);

      /* FP16 -> FP32 */
      if (info.swizzle && swz != BI_SWIZZLE_H01)
         value = fui(_mesa_half_to_float(value));
   } else if (info.size == VA_SIZE_16) {
      assert(swz >= BI_SWIZZLE_H00 && swz <= BI_SWIZZLE_H11);
      value = bi_apply_swizzle(value, swz);
   } else if (info.size == VA_SIZE_8 && (info.lane || info.lanes)) {
      /* 8-bit extract */
      unsigned chan = (swz - BI_SWIZZLE_B0000);
      assert(chan < 4);

      value = (value >> (8 * chan)) & 0xFF;
   } else {
      /* TODO: Any other special handling? */
      value = bi_apply_swizzle(value, swz);
   }

   return value;
}

void
va_lower_constants(bi_context *ctx, bi_instr *I, struct hash_table_u64 *counts, uint32_t min_fau_count)
{
   bi_builder b = bi_init_builder(ctx, bi_before_instr(I));

   bi_foreach_src(I, s) {
      if (I->src[s].type == BI_INDEX_CONSTANT) {
         /* abs(#c) is pointless, but -#c occurs in transcendental sequences */
         assert(!I->src[s].abs && "redundant .abs modifier");

         bool is_signed = valhall_opcodes[I->op].is_signed;
         bool staging = (s < valhall_opcodes[I->op].nr_staging_srcs);
         struct va_src_info info = va_src_info(I->op, s);
         const uint32_t value = va_resolve_swizzles(ctx, I, s);

         const uint32_t count = (uintptr_t)_mesa_hash_table_u64_search(counts, value);
         const bool move_to_fau = count >= min_fau_count;

         bi_index cons =
            va_resolve_constant(&b, value, info, is_signed, staging, move_to_fau);
         cons.neg ^= I->src[s].neg;
         I->src[s] = cons;

         /* If we're selecting a single lane, we should return a single lane
          * to ensure the result is encodeable. By convention, applying the
          * lane select puts the desired constant (at least) in the bottom
          * byte/half, so we can always select the bottom byte/half.
          */
         if ((info.lane || info.lanes || info.halfswizzle) &&
             I->src[s].swizzle == BI_SWIZZLE_H01) {
            assert(info.size == VA_SIZE_8 || info.size == VA_SIZE_16);
            if (info.size == VA_SIZE_8)
               I->src[s] = bi_byte(I->src[s], 0);
            else if (info.size == VA_SIZE_16)
               I->src[s] = bi_half(I->src[s], false);
         }
      }
   }
}

void
va_count_constants(bi_context *ctx, bi_instr *I, struct hash_table_u64 *counts)
{
   bi_foreach_src(I, s) {
      if (I->src[s].type != BI_INDEX_CONSTANT)
         continue;

      const bool staging = (s < valhall_opcodes[I->op].nr_staging_srcs);
      if (staging)
         continue;

      bool is_signed = valhall_opcodes[I->op].is_signed;
      struct va_src_info info = va_src_info(I->op, s);
      uint32_t value = va_resolve_swizzles(ctx, I, s);

      bi_index cons = va_lookup_constant(value, info, is_signed);

      const bool can_lut = !bi_is_null(cons);

      /* We want to move constants that can't be created from built-in
       * constants into the FAU if they are not staging register sources. */
      if (!can_lut) {
         uint32_t count = (uintptr_t)_mesa_hash_table_u64_search(counts, value);
         _mesa_hash_table_u64_insert(counts, value, (void*)(uintptr_t)(count + 1));
      }
   }
}
