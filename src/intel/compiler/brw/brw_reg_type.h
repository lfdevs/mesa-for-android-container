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

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_PURE
#define ATTRIBUTE_PURE __attribute__((__pure__))
#else
#define ATTRIBUTE_PURE
#endif

enum brw_reg_file;
struct intel_device_info;

/**
 * Enum for register/value types throughout the compiler.
 *
 * Bits 1:0 is the size of the type as a U2 'n' where size = 8 * 2^n.
 * Bits 3:4 is set to identify base type: unsigned integer, signed integer,
 * regular floating point and bfloat.
 * Bit 5 is set for vector immediates.
 *
 * While this is inspired by the Tigerlake encodings (and nir_alu_type),
 * it doesn't match any particular hardware generation's encoding.  In
 * particular, hardware encodings are 4-bit values while this is 5-bit
 * so that vector immediate types are distinguishable without considering
 * the register file (immediate or register).  We also encode the size
 * of vector immediates in a way that makes type size calculations simple.
 */
enum PACKED brw_reg_type {
   /* Unsigned integer types: 8, 16, 32, and 64-bit @{ */
   BRW_TYPE_UB = 0b00000,
   BRW_TYPE_UW = 0b00001,
   BRW_TYPE_UD = 0b00010,
   BRW_TYPE_UQ = 0b00011,
   /** @} */

   /* Signed integer types: 8, 16, 32, and 64-bit @{ */
   BRW_TYPE_B  = 0b00100,
   BRW_TYPE_W  = 0b00101,
   BRW_TYPE_D  = 0b00110,
   BRW_TYPE_Q  = 0b00111,
   /** @} */

   /** Floating point types: 16 (half), 32, and 64-bit (double) @{ */
   BRW_TYPE_HF = 0b01001,
   BRW_TYPE_F  = 0b01010,
   BRW_TYPE_DF = 0b01011,
   /** @} */

   /** Floating point types (bfloat variants): 16-bit @{ */
   BRW_TYPE_BF  = 0b01101,
   /** @} */

   /** Vector immediate types */
   BRW_TYPE_UV = 0b10001,
   BRW_TYPE_V  = 0b10101,
   BRW_TYPE_VF = 0b11010,
   /** @} */

   BRW_TYPE_SIZE_MASK   = 0b00011, /* type is (8 << x) bits */
   BRW_TYPE_BASE_MASK   = 0b01100, /* base types expressed in these bits */
   BRW_TYPE_BASE_UINT   = 0b00000, /* unsigned types have no base bits set */
   BRW_TYPE_BASE_SINT   = 0b00100, /* type has a signed integer base type */
   BRW_TYPE_BASE_FLOAT  = 0b01000, /* type has a floating point base type */
   BRW_TYPE_BASE_BFLOAT = 0b01100, /* type has a floating point (bfloat variant) base type */
   BRW_TYPE_VECTOR      = 0b10000, /* type is a vector immediate */

   BRW_TYPE_INVALID    = 0b11111,
   BRW_TYPE_LAST       = BRW_TYPE_INVALID,
};

static inline bool
brw_type_is_float(enum brw_reg_type t)
{
   return (t & BRW_TYPE_BASE_MASK) == BRW_TYPE_BASE_FLOAT;
}

static inline bool
brw_type_is_bfloat(enum brw_reg_type t)
{
   return (t & BRW_TYPE_BASE_MASK) == BRW_TYPE_BASE_BFLOAT;
}

static inline bool
brw_type_is_float_or_bfloat(enum brw_reg_type t)
{
   return brw_type_is_float(t) || brw_type_is_bfloat(t);
}

static inline bool
brw_type_is_uint(enum brw_reg_type t)
{
   return (t & BRW_TYPE_BASE_MASK) == BRW_TYPE_BASE_UINT;
}

static inline bool
brw_type_is_sint(enum brw_reg_type t)
{
   return (t & BRW_TYPE_BASE_MASK) == BRW_TYPE_BASE_SINT;
}

static inline bool
brw_type_is_int(enum brw_reg_type t)
{
   return brw_type_is_uint(t) || brw_type_is_sint(t);
}

static inline bool
brw_type_is_vector_imm(enum brw_reg_type t)
{
   return t & BRW_TYPE_VECTOR;
}

static inline unsigned
brw_type_size_bits(enum brw_reg_type t)
{
   /* [U]V components are 4-bit, but HW unpacks them to 16-bit.
    * Similarly, VF is expanded to 32-bit.
    */
   return 8 << (t & BRW_TYPE_SIZE_MASK);
}

static inline unsigned
brw_type_size_bytes(enum brw_reg_type t)
{
   return brw_type_size_bits(t) / 8;
}

/**
 * Returns a type based on a base type and a bit size.
 */
static inline enum brw_reg_type
brw_type_with_size(enum brw_reg_type ref_type, unsigned bit_size)
{
   assert(bit_size == 8  || bit_size == 16 ||
          bit_size == 32 || bit_size == 64);
   assert(brw_type_is_int(ref_type) || bit_size >= 16);
   unsigned base_field = ref_type & BRW_TYPE_BASE_MASK;
   unsigned size_field = ffs(bit_size) - 4;
   return (enum brw_reg_type)(base_field | size_field);
}

/**
 * Returns the larger of two types (i.e. W and D -> D).
 *
 * The base types must be identical.  Not usable on vector immediates.
 */
static inline enum brw_reg_type
brw_type_larger_of(enum brw_reg_type a, enum brw_reg_type b)
{
   if (a == b)
      return a;

   assert((a & ~BRW_TYPE_SIZE_MASK) == (b & ~BRW_TYPE_SIZE_MASK));
   assert(!brw_type_is_vector_imm(a) && !brw_type_is_vector_imm(b));
   return (enum brw_reg_type) ((a & BRW_TYPE_BASE_MASK) |
          MAX2((a & BRW_TYPE_SIZE_MASK), (b & BRW_TYPE_SIZE_MASK)));
}

/* -------------------------------------------------------------- */

unsigned
brw_type_encode(const struct intel_device_info *devinfo,
                enum brw_reg_file file, enum brw_reg_type type);

enum brw_reg_type ATTRIBUTE_PURE
brw_type_decode(const struct intel_device_info *devinfo,
                enum brw_reg_file file, unsigned hw_type);

unsigned
brw_type_encode_for_3src(const struct intel_device_info *devinfo,
                         enum brw_reg_type type);

enum brw_reg_type
brw_type_decode_for_3src(const struct intel_device_info *devinfo,
                         unsigned hw_type, unsigned exec_type);

const char *
brw_reg_type_to_letters(enum brw_reg_type type);

#define INVALID_HW_REG_TYPE 0b1111

#ifdef __cplusplus
}
#endif
