/*
 * The implementations contained in this file are heavily based on the
 * implementations found in the Berkeley SoftFloat library. As such, they are
 * licensed under the same 3-clause BSD license:
 *
 * License for Berkeley SoftFloat Release 3e
 *
 * John R. Hauser
 * 2018 January 20
 *
 * The following applies to the whole of SoftFloat Release 3e as well as to
 * each source file individually.
 *
 * Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018 The Regents of the
 * University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions, and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions, and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the University nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#version 450
#extension GL_ARB_shader_bit_encoding : enable
#extension GL_EXT_shader_integer_mix : enable

/* Enable this just to suppress warnings about __ */
#extension GL_EXT_spirv_intrinsics : enable

#pragma warning(off)

/* Software IEEE floating-point rounding mode.
 * GLSL spec section "4.7.1 Range and Precision":
 * The rounding mode cannot be set and is undefined.
 * But here, we are able to define the rounding mode at the compilation time.
 */
#define FLOAT_ROUND_NEAREST_EVEN    0
#define FLOAT_ROUND_TO_ZERO         1
#define FLOAT_ROUND_DOWN            2
#define FLOAT_ROUND_UP              3
#define FLOAT_ROUNDING_MODE         FLOAT_ROUND_NEAREST_EVEN

/* Relax propagation of NaN.  Binary operations with a NaN source will still
 * produce a NaN result, but it won't follow strict IEEE rules.
 */
#define RELAXED_NAN_PROPAGATION

/* Returns the number of leading 0 bits before the most-significant 1 bit of
 * `a'.  If `a' is zero, 32 is returned.
 */
int
__countLeadingZeros32(uint a)
{
   return 31 - findMSB(a);
}

/* If a shader is in the soft-fp32 path, it almost certainly has register
 * pressure problems.  Choose a method to exchange two values that does not
 * require a temporary.
 */
#define EXCHANGE(a, b) \
   do {                \
       a ^= b;         \
       b ^= a;         \
       a ^= b;         \
   } while (false)

/* Shifts the 32-bit value `a` right by the number of bits given in `count'.
 * If any nonzero bits are shifted off, they are "jammed" into the least
 * significant bit of the result by setting the least significant bit to 1.
 * The value of `count' can be arbitrarily large; in particular, if `count' is
 * greater than 32, the result will be either 0 or 1, depending on whether `a`
 * is zero or nonzero.
 */
uint
__shift32RightJamming(uint a, int count)
{
   int negCount = (-count) & 31;

   return mix(uint(a != 0), (a >> count) | uint(a<<negCount != 0), count < 32);
}

/* Packs the sign `zSign', exponent `zExp', and significand `zFrac' into a
 * single-precision floating-point value, returning the result.  After being
 * shifted into the proper positions, the three fields are simply added
 * together to form the result.  This means that any integer portion of `zSig'
 * will be added into the exponent.  Since a properly normalized significand
 * will have an integer portion equal to 1, the `zExp' input should be 1 less
 * than the desired result exponent whenever `zFrac' is a complete, normalized
 * significand.
 */
uint
__packFloat32(uint zSign, int zExp, uint zFrac)
{
   return zSign + (uint(zExp)<<23) + zFrac;
}

/* Takes an abstract floating-point value having sign `zSign', exponent `zExp',
 * and significand `zFrac', and returns the proper single-precision floating-
 * point value corresponding to the abstract input.  Ordinarily, the abstract
 * value is simply rounded and packed into the single-precision format, with
 * the inexact exception raised if the abstract input cannot be represented
 * exactly.  However, if the abstract value is too large, the overflow and
 * inexact exceptions are raised and an infinity or maximal finite value is
 * returned.  If the abstract value is too small, the input value is rounded to
 * a subnormal number, and the underflow and inexact exceptions are raised if
 * the abstract input cannot be represented exactly as a subnormal single-
 * precision floating-point number.
 *     The input significand `zFrac' has its binary point between bits 30
 * and 29, which is 7 bits to the left of the usual location.  This shifted
 * significand must be normalized or smaller.  If `zFrac' is not normalized,
 * `zExp' must be 0; in that case, the result returned is a subnormal number,
 * and it must not require rounding.  In the usual case that `zFrac' is
 * normalized, `zExp' must be 1 less than the "true" floating-point exponent.
 * The handling of underflow and overflow follows the IEEE Standard for
 * Floating-Point Arithmetic.
 */
uint
__roundAndPackFloat32(uint zSign, int zExp, uint zFrac)
{
   bool roundNearestEven;
   int roundIncrement;
   int roundBits;

   roundNearestEven = FLOAT_ROUNDING_MODE == FLOAT_ROUND_NEAREST_EVEN;
   roundIncrement = 0x40;
   if (!roundNearestEven) {
      if (FLOAT_ROUNDING_MODE == FLOAT_ROUND_TO_ZERO) {
         roundIncrement = 0;
      } else {
         roundIncrement = 0x7F;
         if (zSign != 0u) {
            if (FLOAT_ROUNDING_MODE == FLOAT_ROUND_UP)
               roundIncrement = 0;
         } else {
            if (FLOAT_ROUNDING_MODE == FLOAT_ROUND_DOWN)
               roundIncrement = 0;
         }
      }
   }
   roundBits = int(zFrac & 0x7Fu);
   if (0xFDu <= uint(zExp)) {
      if ((0xFD < zExp) || ((zExp == 0xFD) && (int(zFrac) + roundIncrement) < 0))
         return __packFloat32(zSign, 0xFF, 0u) -
            floatBitsToUint(float(roundIncrement == 0));
      int count = -zExp;
      bool zexp_lt0 = zExp < 0;
      uint zFrac_lt0 = __shift32RightJamming(zFrac, -zExp);
      zFrac = mix(zFrac, zFrac_lt0, zexp_lt0);
      roundBits = mix(roundBits, int(zFrac) & 0x7f, zexp_lt0);
      zExp = mix(zExp, 0, zexp_lt0);
   }
   zFrac = (zFrac + uint(roundIncrement))>>7;
   zFrac &= ~uint(((roundBits ^ 0x40) == 0) && roundNearestEven);

   return __packFloat32(zSign, mix(zExp, 0, zFrac == 0u), zFrac);
}


/* Absolute value of a Float32 :
 * Clear the sign bit
 */
uint
__fabs32(uint a)
{
   return a & 0x7FFFFFFFu;
}

/* Returns 1 if the single-precision floating-point value `a' is a NaN;
 * otherwise returns 0.
 */
bool
__is_nan(uint a)
{
   /* It should be safe to use the native single-precision isnan() regardless
    * of rounding mode or denorm flushing settings.
    */
   return isnan(uintBitsToFloat(a));
}

/* Negate value of a Float32 :
 * Toggle the sign bit
 */
uint
__fneg32(uint a)
{
   return a ^ (1u << 31);
}

uint
__fsign32(uint a)
{
   return mix((a & 0x80000000u) | floatBitsToUint(1.0), 0u, (a << 1) == 0u);
}

/* Returns the fraction bits of the single-precision floating-point value `a'.*/
uint
__extractFloat32Frac(uint a)
{
   return a & 0x7FFFFF;
}

/* Returns the exponent bits of the single-precision floating-point value `a'.*/
int
__extractFloat32Exp(uint a)
{
   return int((a>>23) & 0xFFu);
}

bool
__feq32_nonnan(uint a, uint b)
{
   return (a == b) || ((a == 0u) && (((a | b)<<1) == 0u));
}

/* Returns true if the single-precision floating-point value `a' is equal to the
 * corresponding value `b', and false otherwise.  The comparison is performed
 * according to the IEEE Standard for Floating-Point Arithmetic.
 */
bool
__feq32(uint a, uint b)
{
   if (__is_nan(a) || __is_nan(b))
      return false;

   return __feq32_nonnan(a, b);
}

/* Returns true if the single-precision floating-point value `a' is not equal
 * to the corresponding value `b', and false otherwise.  The comparison is
 * performed according to the IEEE Standard for Floating-Point Arithmetic.
 */
bool
__fneu32(uint a, uint b)
{
   if (__is_nan(a) || __is_nan(b))
      return true;

   return !__feq32_nonnan(a, b);
}

/* Returns the sign bit of the single-precision floating-point value `a'.*/
uint
__extractFloat32Sign(uint a)
{
   return a & 0x80000000u;
}

bool
__flt32_nonnan(uint a, uint b)
{
   /* IEEE 754 floating point numbers are specifically designed so that, with
    * two exceptions, values can be compared by bit-casting to signed integers
    * with the same number of bits.
    *
    * From https://en.wikipedia.org/wiki/IEEE_754-1985#Comparing_floating-point_numbers:
    *
    *    When comparing as 2's-complement integers: If the sign bits differ,
    *    the negative number precedes the positive number, so 2's complement
    *    gives the correct result (except that negative zero and positive zero
    *    should be considered equal). If both values are positive, the 2's
    *    complement comparison again gives the correct result. Otherwise (two
    *    negative numbers), the correct FP ordering is the opposite of the 2's
    *    complement ordering.
    *
    * The logic implied by the above quotation is:
    *
    *    !both_are_zero(a, b) && (both_negative(a, b) ? a > b : a < b)
    *
    * This is equivalent to
    *
    *    fneu(a, b) && (both_negative(a, b) ? a >= b : a < b)
    *
    *    fneu(a, b) && (both_negative(a, b) ? !(a < b) : a < b)
    *
    *    fneu(a, b) && ((both_negative(a, b) && !(a < b)) ||
    *                  (!both_negative(a, b) && (a < b)))
    *
    * (A!|B)&(A|!B) is (A xor B) which is implemented here using !=.
    *
    *    fneu(a, b) && (both_negative(a, b) != (a < b))
    */
   bool lt = a < b;
   bool both_negative = (a & b & 0x80000000u) != 0;

   return !__feq32_nonnan(a, b) && (lt != both_negative);
}

bool
__flt32_nonnan_minmax(uint a, uint b)
{

   /* See __flt32_nonnan. For implementing fmin/fmax, we compare -0 < 0, so the
    * implied logic is a bit simpler:
    *
    *    both_negative(a, b) ? a > b : a < b
    *
    * If a == b, it doesn't matter what we return, so that's equivalent to:
    *
    *    both_negative(a, b) ? a >= b : a < b
    *    both_negative(a, b) ? !(a < b) : a < b
    *    both_negative(a, b) ^ (a < b)
    *
    * XOR is again implemented using !=.
    */
   bool lt = a < b;
   bool both_negative = (a & b & 0x80000000u) != 0;

   return (lt != both_negative);
}

/* Returns true if the single-precision floating-point value `a' is less than
 * the corresponding value `b', and false otherwise.  The comparison is performed
 * according to the IEEE Standard for Floating-Point Arithmetic.
 */
bool
__flt32(uint a, uint b)
{
   /* This weird layout matters.  Doing the "obvious" thing results in extra
    * flow control being inserted to implement the short-circuit evaluation
    * rules.  Flow control is bad!
    */
   bool x = !__is_nan(a);
   bool y = !__is_nan(b);
   bool z = __flt32_nonnan(a, b);

   return (x && y && z);
}

/* Returns true if the single-precision floating-point value `a' is greater
 * than or equal to * the corresponding value `b', and false otherwise.  The
 * comparison is performed * according to the IEEE Standard for Floating-Point
 * Arithmetic.
 */
bool
__fge32(uint a, uint b)
{
   /* This weird layout matters.  Doing the "obvious" thing results in extra
    * flow control being inserted to implement the short-circuit evaluation
    * rules.  Flow control is bad!
    */
   bool x = !__is_nan(a);
   bool y = !__is_nan(b);
   bool z = !__flt32_nonnan(a, b);

   return (x && y && z);
}

uint
fsat32(uint a)
{
   /* fsat(NaN) should be zero. */
   if (__is_nan(a) || int(a) < 0)
      return 0u;

   /* IEEE 754 floating point numbers are specifically designed so that, with
    * two exceptions, values can be compared by bit-casting to signed integers
    * with the same number of bits.
    *
    * From https://en.wikipedia.org/wiki/IEEE_754-1985#Comparing_floating-point_numbers:
    *
    *    When comparing as 2's-complement integers: If the sign bits differ,
    *    the negative number precedes the positive number, so 2's complement
    *    gives the correct result (except that negative zero and positive zero
    *    should be considered equal). If both values are positive, the 2's
    *    complement comparison again gives the correct result. Otherwise (two
    *    negative numbers), the correct FP ordering is the opposite of the 2's
    *    complement ordering.
    *
    * We know that both values are not negative, and we know that at least one
    * value is not zero.  Therefore, we can just use the 2's complement
    * comparison ordering.
    */
   if (floatBitsToUint(1.0) < a)
      return floatBitsToUint(1.0);

   return a;
}

/* Takes an abstract floating-point value having sign `zSign', exponent `zExp',
 * and significand `zSig', and returns the proper single-precision
 * floating-point value corresponding to the abstract input.  This routine is
 * just like `__roundAndPackFloat32' except that the input significand has
 * fewer bits and does not have to be normalized.  In all cases, `zExp' must be
 * 1 less than the "true" floating- point exponent.
 */
uint
__normalizeRoundAndPackFloat32(uint zSign,
                               int zExp,
                               uint zFrac)
{
   int shiftCount;

   shiftCount = __countLeadingZeros32(zFrac) - 1;
   return __roundAndPackFloat32(zSign, zExp - shiftCount, zFrac<<shiftCount);
}

uint
__propagateFloat32NaNInfAdd(uint a, uint b)
{
   return floatBitsToUint(uintBitsToFloat(a) + uintBitsToFloat(b));
}

uint
__propagateFloat32NaNInfMul(uint a, uint b)
{
   return floatBitsToUint(uintBitsToFloat(a) * uintBitsToFloat(b));
}

/* Returns the result of adding the single-precision floating-point values
 * `a' and `b'.  The operation is performed according to the IEEE Standard for
 * Floating-Point Arithmetic.
 */
uint
__fadd32(uint a, uint b)
{
   uint aSign = __extractFloat32Sign(a);
   uint bSign = __extractFloat32Sign(b);
   uint aFrac = __extractFloat32Frac(a);
   uint bFrac = __extractFloat32Frac(b);
   int aExp = __extractFloat32Exp(a);
   int bExp = __extractFloat32Exp(b);
   int expDiff = aExp - bExp;
   if (aSign == bSign) {
      uint zFrac;
      int zExp;
      aFrac <<= 6;
      bFrac <<= 6;
      if (expDiff == 0) {
         if (aExp == 0xFF)
            return __propagateFloat32NaNInfAdd(a, b);
         if (aExp == 0)
            return __packFloat32(aSign, 0, (aFrac + bFrac)>>6);
         zFrac = 0x40000000 + aFrac + bFrac;
         zExp = aExp;
      } else {
         if (expDiff < 0) {
            EXCHANGE(aFrac, bFrac);
            EXCHANGE(aExp, bExp);
         }

         if (aExp == 0xFF)
            return __propagateFloat32NaNInfAdd(a, b);

         expDiff = mix(abs(expDiff), abs(expDiff) - 1, bExp == 0);
         bFrac = mix(bFrac | 0x20000000u, bFrac, bExp == 0);
         bFrac = __shift32RightJamming(bFrac, expDiff);
         zExp = aExp;

         aFrac |= 0x20000000;
         zFrac = (aFrac + bFrac)<<1;
         --zExp;
         if (int(zFrac) < 0) {
            zFrac = aFrac + bFrac;
            ++zExp;
         }
      }
      return __roundAndPackFloat32(aSign, zExp, zFrac);
   } else {
      int zExp;

      aFrac <<= 7;
      bFrac <<= 7;
      if (expDiff != 0) {
         uint zFrac;

         if (expDiff < 0) {
            EXCHANGE(aFrac, bFrac);
            EXCHANGE(aExp, bExp);
            aSign ^= 0x80000000u;
         }
         if (aExp == 0xFF)
            return __propagateFloat32NaNInfAdd(a, b);

         expDiff = mix(abs(expDiff), abs(expDiff) - 1, bExp == 0);
         bFrac = mix(bFrac | 0x40000000u, bFrac, bExp == 0);
         bFrac = __shift32RightJamming(bFrac, expDiff);
         aFrac |= 0x40000000;
         zFrac = aFrac - bFrac;
         zExp = aExp;
         --zExp;
         return __normalizeRoundAndPackFloat32(aSign, zExp, zFrac);
      }
      if (aExp == 0xFF)
         return __propagateFloat32NaNInfAdd(a, b);
      bExp = mix(bExp, 1, aExp == 0);
      aExp = mix(aExp, 1, aExp == 0);

      uint zFrac;
      uint sign_of_difference = 0;
      if (bFrac <= aFrac) {
         /* It is possible that zFrac may be zero after this. */
         zFrac = aFrac - bFrac;
      } else {
         zFrac = bFrac - aFrac;
         sign_of_difference = 0x80000000;
      }
      zExp = mix(bExp, aExp, sign_of_difference == 0u);
      aSign ^= sign_of_difference;
      uint retval_0 = __packFloat32(uint(FLOAT_ROUNDING_MODE == FLOAT_ROUND_DOWN) << 31, 0, 0u);
      uint retval_1 = __normalizeRoundAndPackFloat32(aSign, zExp, zFrac);
      return mix(retval_0, retval_1, zFrac != 0u);
   }
}

/* Normalizes the subnormal single-precision floating-point value represented
 * by the denormalized significand `aFrac'.  The normalized exponent and
 * significand are stored at the locations pointed to by `zExpPtr' and
`* `zFracPtr', respectively.
 */
void
__normalizeFloat32Subnormal(uint aFrac,
                            out int zExpPtr,
                            out uint zFracPtr)
{
   int shiftCount;

   shiftCount = __countLeadingZeros32(aFrac) - 8;
   zFracPtr = aFrac << shiftCount;
   zExpPtr = 1 - shiftCount;
}

/* Returns the result of multiplying the single-precision floating-point values
 * `a' and `b'.  The operation is performed according to the IEEE Standard for
 * Floating-Point Arithmetic.
 */
uint
__fmul32(uint a, uint b)
{
   uint zFrac0 = 0u;
   uint zFrac1 = 0u;
   int zExp;

   uint aFrac = __extractFloat32Frac(a);
   uint bFrac = __extractFloat32Frac(b);
   int aExp = __extractFloat32Exp(a);
   uint aSign = __extractFloat32Sign(a);
   int bExp = __extractFloat32Exp(b);
   uint bSign = __extractFloat32Sign(b);
   uint zSign = aSign ^ bSign;
   if (aExp == 0xFF) {
      /* Subnormal values times infinity equals infinity, but other cases can
       * use the builtin multiply that may flush denorms to 0.
       */
      if (aFrac != 0u || ((bExp == 0xFF) && bFrac != 0) || (bExp | bFrac) == 0)
         return __propagateFloat32NaNInfMul(a, b);
      return __packFloat32(zSign, 0xFF, 0);
   }
   if (bExp == 0xFF) {
      if (bFrac != 0u || (aExp | aFrac) == 0)
         return __propagateFloat32NaNInfMul(a, b);
      return __packFloat32(zSign, 0xFF, 0u);
   }
   if (aExp == 0) {
      if (aFrac == 0u)
         return __packFloat32(zSign, 0, 0u);
      __normalizeFloat32Subnormal(aFrac, aExp, aFrac);
   }
   if (bExp == 0) {
      if (bFrac == 0u)
         return __packFloat32(zSign, 0, 0u);
      __normalizeFloat32Subnormal(bFrac, bExp, bFrac);
   }
   zExp = aExp + bExp - 0x7F;
   aFrac = ( aFrac | 0x00800000 )<<7;
   bFrac = ( bFrac | 0x00800000 )<<8;
   umulExtended(aFrac, bFrac, zFrac0, zFrac1);
   zFrac0 |= uint(zFrac1 != 0);
   if (0 < int(zFrac0 << 1)) {
      zFrac0 <<= 1;
      --zExp;
   }
   return __roundAndPackFloat32(zSign, zExp, zFrac0);
}

uint
__ffma32(uint a, uint b, uint c)
{
   return __fadd32(__fmul32(a, b), c);
}

uint
__fmin32(uint a, uint b)
{
   /* This weird layout matters.  Doing the "obvious" thing results in extra
    * flow control being inserted to implement the short-circuit evaluation
    * rules.  Flow control is bad!
    */
   bool b_nan = __is_nan(b);
   bool a_lt_b = __flt32_nonnan_minmax(a, b);
   bool a_nan = __is_nan(a);

   return (b_nan || a_lt_b) && !a_nan ? a : b;
}

uint
__fmax32(uint a, uint b)
{
   /* This weird layout matters.  Doing the "obvious" thing results in extra
    * flow control being inserted to implement the short-circuit evaluation
    * rules.  Flow control is bad!
    */
   bool b_nan = __is_nan(b);
   bool a_lt_b = __flt32_nonnan_minmax(a, b);
   bool a_nan = __is_nan(a);

   return (!b_nan && a_lt_b) || a_nan ? b : a;
}

