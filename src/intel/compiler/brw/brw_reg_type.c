/*
 * Copyright © 2017 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_reg.h"
#include "brw_eu_defines.h"
#include "dev/intel_device_info.h"

/**
 * Convert a brw_reg_type enumeration value into the hardware representation.
 *
 * The hardware encoding may depend on whether the value is an immediate.
 */
unsigned
brw_type_encode(const struct intel_device_info *devinfo,
                enum brw_reg_file file, enum brw_reg_type type)
{
   assert(file != IMM ||
          brw_type_is_vector_imm(type) ||
          brw_type_size_bits(type) >= 16);

   if (type == BRW_TYPE_INVALID)
      return INVALID_HW_REG_TYPE;

   if (brw_type_size_bits(type) == 64 &&
       !(brw_type_is_int(type) ? devinfo->has_64bit_int
                               : devinfo->has_64bit_float))
      return INVALID_HW_REG_TYPE;

   if (brw_type_is_bfloat(type) && !devinfo->has_bfloat16)
      return INVALID_HW_REG_TYPE;

   if (devinfo->ver >= 12) {
      if (brw_type_is_vector_imm(type))
         return type & ~(BRW_TYPE_VECTOR | BRW_TYPE_SIZE_MASK);

      return type & (BRW_TYPE_BASE_MASK | BRW_TYPE_SIZE_MASK);
   } else if (devinfo->ver >= 11) {
      if (brw_type_is_vector_imm(type)) {
         if (type == BRW_TYPE_VF)
            return 11;
         /* UV/V is the same encoding as UB/B */
         type &= ~(BRW_TYPE_VECTOR | BRW_TYPE_SIZE_MASK);
      }

      if (brw_type_is_float(type)) {
         /* HF: 8, F: 9 */
         return 8 + (type & BRW_TYPE_SIZE_MASK) - 1;
      }

      /* UB: 4, UW: 2, UD: 0
       *  B: 5,  W: 3,  D: 1
       */
      return 4 - 2 * (type & BRW_TYPE_SIZE_MASK) +
             (brw_type_is_sint(type) ? 1 : 0);
   } else {
      if (brw_type_is_vector_imm(type)) {
         return type == BRW_TYPE_UV ? 4 :
                type == BRW_TYPE_VF ? 5 :
                     /* BRW_TYPE_V */ 6;
      } else if (brw_type_is_float(type)) {
         static const unsigned imm_tbl[] = {
            [0b00] = 5,  /* VF */
            [0b01] = 11, /* HF */
            [0b10] = 7,  /*  F */
            [0b11] = 10, /* DF */
         };
         static const unsigned reg_tbl[] = {
            [0b01] = 10, /* HF */
            [0b10] = 7,  /*  F */
            [0b11] = 6,  /* DF */
         };
         const unsigned *tbl = file == IMM ? imm_tbl : reg_tbl;
         return tbl[type & BRW_TYPE_SIZE_MASK];
      } else {
         static const unsigned tbl[] = {
            [0b00] = 4, /* UB/UV */
            [0b01] = 2, /* UW */
            [0b10] = 0, /* UD */
            [0b11] = 8, /* UQ */
         };
         return tbl[type & BRW_TYPE_SIZE_MASK] |
                (brw_type_is_sint(type) ? 1 : 0);
      }
   }
}

/**
 * Convert the hardware representation into a brw_reg_type enumeration value.
 *
 * The hardware encoding may depend on whether the value is an immediate.
 */
enum brw_reg_type
brw_type_decode(const struct intel_device_info *devinfo,
                enum brw_reg_file file, unsigned hw_type)
{
   if (hw_type >= (1 << 4))
      return BRW_TYPE_INVALID;

   if (devinfo->ver >= 12) {
      enum brw_reg_type t = (enum brw_reg_type) hw_type;
      if (brw_type_size_bits(t) == 8) {
         if (brw_type_is_float(t))
            return file == IMM ? BRW_TYPE_VF : BRW_TYPE_INVALID;
         else if (file == IMM)
            return (t & BRW_TYPE_BASE_SINT) ? BRW_TYPE_V : BRW_TYPE_UV;
      }
      if (brw_type_is_bfloat(t) && !devinfo->has_bfloat16)
         return BRW_TYPE_INVALID;
      if (brw_type_is_float_or_bfloat(t) && brw_type_size_bits(t) < 16)
         return BRW_TYPE_INVALID;
      return t;
   } else if (devinfo->ver >= 11) {
      static const enum brw_reg_type tbl[] = {
         [0] = BRW_TYPE_UD,
         [1] = BRW_TYPE_D,
         [2] = BRW_TYPE_UW,
         [3] = BRW_TYPE_W,
         [4] = BRW_TYPE_UB, /* or UV */
         [5] = BRW_TYPE_B,  /* or V */
         [6] = BRW_TYPE_UQ,
         [7] = BRW_TYPE_Q,
         [8] = BRW_TYPE_HF,
         [9] = BRW_TYPE_F,
         [10] = BRW_TYPE_INVALID, /* no DF */
         [11] = BRW_TYPE_VF,
         [12 ... 15] = BRW_TYPE_INVALID,
      };
      enum brw_reg_type t = tbl[hw_type];
      if (file == IMM && brw_type_size_bits(t) == 8)
         return (t & BRW_TYPE_BASE_SINT) ? BRW_TYPE_V : BRW_TYPE_UV;
      if (file != IMM && brw_type_is_vector_imm(t))
         return BRW_TYPE_INVALID;
      return t;
   } else {
      static const enum brw_reg_type imm_tbl[] = {
         [0] = BRW_TYPE_UD,
         [1] = BRW_TYPE_D,
         [2] = BRW_TYPE_UW,
         [3] = BRW_TYPE_W,
         [4] = BRW_TYPE_UV,
         [5] = BRW_TYPE_VF,
         [6] = BRW_TYPE_V,
         [7] = BRW_TYPE_F,
         [8] = BRW_TYPE_UQ,
         [9] = BRW_TYPE_Q,
         [10] = BRW_TYPE_DF,
         [11] = BRW_TYPE_HF,
         [12 ... 15] = BRW_TYPE_INVALID,
      };
      static const enum brw_reg_type reg_tbl[] = {
         [0] = BRW_TYPE_UD,
         [1] = BRW_TYPE_D,
         [2] = BRW_TYPE_UW,
         [3] = BRW_TYPE_W,
         [4] = BRW_TYPE_UB,
         [5] = BRW_TYPE_B,
         [6] = BRW_TYPE_DF,
         [7] = BRW_TYPE_F,
         [8] = BRW_TYPE_UQ,
         [9] = BRW_TYPE_Q,
         [10] = BRW_TYPE_HF,
         [11 ... 15] = BRW_TYPE_INVALID,
      };
      const enum brw_reg_type *tbl = file == IMM ? imm_tbl : reg_tbl;
      return tbl[hw_type];
   }

   return BRW_TYPE_INVALID;
}

/**
 * Convert a brw_reg_type into the hardware encoding for a 3-src instruction.
 */
unsigned
brw_type_encode_for_3src(const struct intel_device_info *devinfo,
                         enum brw_reg_type type)
{
   if (brw_type_is_bfloat(type) && !devinfo->has_bfloat16)
      return INVALID_HW_REG_TYPE;

   if (devinfo->ver >= 12) {
      /* size mask and SINT type bit match exactly */
      return type & 0b111;
   } else if (devinfo->ver >= 11) {
      if (brw_type_is_float(type)) {
         /* HF: 0b000 | F: 0b001 | DF: 0b010; subtract 1 from our size mask */
         return (type & BRW_TYPE_SIZE_MASK) - 1;
      }

      /* Bit 0 is the sign bit, bits 1-2 are our size mask reversed.
       * UD: 0b000 | D: 0b001
       * UW: 0b010 | W: 0b011
       * UB: 0b100 | B: 0b101
       */
      return ((2 - (type & BRW_TYPE_SIZE_MASK)) << 1) |
             (brw_type_is_sint(type) ? 1 : 0);
   } else {
      /* align16 encodings */
      static const unsigned tbl[] = {
         [0 ... BRW_TYPE_LAST] = BRW_TYPE_INVALID,
         [BRW_TYPE_F] = 0,
         [BRW_TYPE_D] = 1,
         [BRW_TYPE_UD] = 2,
         [BRW_TYPE_DF] = 3,
         [BRW_TYPE_HF] = 4,
      };
      assert(type < ARRAY_SIZE(tbl));
      return tbl[type];
   }
}

/**
 * Convert the hardware encoding for a 3-src instruction into a brw_reg_type.
 */
enum brw_reg_type
brw_type_decode_for_3src(const struct intel_device_info *devinfo,
                         unsigned hw_type, unsigned exec_type)
{
   STATIC_ASSERT(BRW_ALIGN1_3SRC_EXEC_TYPE_INT == 0);
   STATIC_ASSERT(BRW_ALIGN1_3SRC_EXEC_TYPE_FLOAT == 1);
   assert(exec_type == 0 || exec_type == 1);

   if (devinfo->ver >= 12) {
      unsigned size_field = hw_type & BRW_TYPE_SIZE_MASK;
      unsigned base_field = hw_type & BRW_TYPE_BASE_MASK;
      if (exec_type == BRW_ALIGN1_3SRC_EXEC_TYPE_FLOAT) {
         base_field |= BRW_TYPE_BASE_FLOAT;
         if (base_field == BRW_TYPE_BASE_BFLOAT && !devinfo->has_bfloat16)
            return BRW_TYPE_INVALID;
      }
      return (enum brw_reg_type) (base_field | size_field);
   } else if (devinfo->ver >= 11) {
      if (exec_type == BRW_ALIGN1_3SRC_EXEC_TYPE_FLOAT) {
         return hw_type > 1 ? BRW_TYPE_INVALID :
                hw_type ? BRW_TYPE_F : BRW_TYPE_HF;
      }

      unsigned size_field = 2 >> (hw_type >> 1);
      unsigned base_field = (hw_type & 1) << 2;
      return (enum brw_reg_type) (base_field | size_field);
   } else {
      /* align16 encodings */
      static const enum brw_reg_type tbl[] = {
         [0] = BRW_TYPE_F,
         [1] = BRW_TYPE_D,
         [2] = BRW_TYPE_UD,
         [3] = BRW_TYPE_DF,
         [4] = BRW_TYPE_HF,
      };
      return hw_type < ARRAY_SIZE(tbl) ? tbl[hw_type] : BRW_TYPE_INVALID;
   }
}

/**
 * Converts a BRW_TYPE_* enum to a short string (F, UD, and so on).
 *
 * This is different than reg_encoding from brw_disasm.c in that it operates
 * on the abstract enum values, rather than the generation-specific encoding.
 */
const char *
brw_reg_type_to_letters(enum brw_reg_type type)
{
   static const char letters[][3] = {
      [BRW_TYPE_UB] = "UB",
      [BRW_TYPE_UW] = "UW",
      [BRW_TYPE_UD] = "UD",
      [BRW_TYPE_UQ] = "UQ",

      [BRW_TYPE_B]  = "B",
      [BRW_TYPE_W]  = "W",
      [BRW_TYPE_D]  = "D",
      [BRW_TYPE_Q]  = "Q",

      [BRW_TYPE_HF] = "HF",
      [BRW_TYPE_F]  = "F",
      [BRW_TYPE_DF] = "DF",

      [BRW_TYPE_BF] = "BF",

      [BRW_TYPE_UV] = "UV",
      [BRW_TYPE_V]  = "V",
      [BRW_TYPE_VF] = "VF",
   };

   const char *l = type < ARRAY_SIZE(letters) ? letters[type] : NULL;
   return l ? l : "INVALID";
}
