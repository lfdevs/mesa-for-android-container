/*
 * Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "util/half_float.h"
#include "util/rand_xor.h"
#include "util/u_math.h"
#include "nir.h"
#include "nir_constant_expressions.h"

/* Doesn't test anything itself, but outputs a CSV with the results of
 * evaluating constant expressions of FP ops with various edge case FP
 * values and execution modes.
 * Running it on different CPU architectures and comparing the results can help
 * identify the discrepancies in constant folding behavior between them.
 */

#define FP_GENERATED_ORDINARY_COUNT 8
#define FP_GENERATED_NAN_COUNT      4
#define FP_GENERATED_INF_COUNT      4

static const uint64_t int_values[] = {
   0,
   1,
   2,
   7,
   8,
   15,
   31,
   63,
   127,
   128,
   255,
   1024,
   65535,
   2147483647ull,
   2147483648ull,
   UINT64_MAX,
};

static const unsigned exec_modes[] = {
   0,
   FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16,
   FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32,
   FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64,
   FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16,
   FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32,
   FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP64,
   FLOAT_CONTROLS_DENORM_PRESERVE_FP16,
   FLOAT_CONTROLS_DENORM_PRESERVE_FP32,
   FLOAT_CONTROLS_DENORM_PRESERVE_FP64,
};

enum fp_value_kind {
   FP_VALUE_POS_ZERO,
   FP_VALUE_NEG_ZERO,
   FP_VALUE_POS_MIN_SUBNORMAL,
   FP_VALUE_NEG_MIN_SUBNORMAL,
   FP_VALUE_POS_MAX_SUBNORMAL,
   FP_VALUE_NEG_MAX_SUBNORMAL,
   FP_VALUE_POS_MIN_NORMAL,
   FP_VALUE_NEG_MIN_NORMAL,
   FP_VALUE_POS_ONE,
   FP_VALUE_NEG_ONE,
   FP_VALUE_POS_ONE_PREV,
   FP_VALUE_POS_ONE_NEXT,
   FP_VALUE_NEG_ONE_PREV,
   FP_VALUE_NEG_ONE_NEXT,
   FP_VALUE_POS_PI_PREV,
   FP_VALUE_POS_PI,
   FP_VALUE_POS_PI_NEXT,
   FP_VALUE_NEG_PI_PREV,
   FP_VALUE_NEG_PI,
   FP_VALUE_NEG_PI_NEXT,
   FP_VALUE_POS_MAX_FINITE_PREV,
   FP_VALUE_POS_MAX_FINITE,
   FP_VALUE_NEG_MAX_FINITE_PREV,
   FP_VALUE_NEG_MAX_FINITE,
   FP_VALUE_POS_INF,
   FP_VALUE_NEG_INF,
   FP_VALUE_POS_QNAN,
   FP_VALUE_NEG_QNAN,
   FP_VALUE_POS_SNAN,
   FP_VALUE_NEG_SNAN,
   FP_VALUE_NEG_INT_MIN_PREV,
   FP_VALUE_NEG_INT_MIN,
   FP_VALUE_NEG_INT_MIN_NEXT,
   FP_VALUE_POS_INT_MAX_PREV,
   FP_VALUE_POS_INT_MAX,
   FP_VALUE_POS_INT_MAX_NEXT,
   FP_VALUE_POS_INT_MAX_PLUS_ONE_PREV,
   FP_VALUE_POS_INT_MAX_PLUS_ONE,
   FP_VALUE_POS_INT_MAX_PLUS_ONE_NEXT,
   FP_VALUE_POS_UINT_MAX_PREV,
   FP_VALUE_POS_UINT_MAX,
   FP_VALUE_POS_UINT_MAX_NEXT,
   FP_VALUE_POS_UINT_MAX_PLUS_ONE_PREV,
   FP_VALUE_POS_UINT_MAX_PLUS_ONE,
   FP_VALUE_POS_UINT_MAX_PLUS_ONE_NEXT,
   FP_VALUE_NEG_UINT_ZERO_PREV,
   FP_VALUE_NEG_UINT_ZERO,
   FP_VALUE_POS_UINT_ZERO,
   FP_VALUE_POS_UINT_ZERO_NEXT,
};

struct fp_value_case {
   const char *name;
   enum fp_value_kind kind;
};

struct source_value {
   uint64_t bits;
   char name[32];
};

static const struct fp_value_case fp_values[] = {
   { "+zero", FP_VALUE_POS_ZERO },
   { "-zero", FP_VALUE_NEG_ZERO },
   { "+min_subnormal", FP_VALUE_POS_MIN_SUBNORMAL },
   { "-min_subnormal", FP_VALUE_NEG_MIN_SUBNORMAL },
   { "+max_subnormal", FP_VALUE_POS_MAX_SUBNORMAL },
   { "-max_subnormal", FP_VALUE_NEG_MAX_SUBNORMAL },
   { "+min_normal", FP_VALUE_POS_MIN_NORMAL },
   { "-min_normal", FP_VALUE_NEG_MIN_NORMAL },
   { "+one", FP_VALUE_POS_ONE },
   { "-one", FP_VALUE_NEG_ONE },
   { "+one_prev", FP_VALUE_POS_ONE_PREV },
   { "+one_next", FP_VALUE_POS_ONE_NEXT },
   { "-one_prev", FP_VALUE_NEG_ONE_PREV },
   { "-one_next", FP_VALUE_NEG_ONE_NEXT },
   { "+pi_prev", FP_VALUE_POS_PI_PREV },
   { "+pi", FP_VALUE_POS_PI },
   { "+pi_next", FP_VALUE_POS_PI_NEXT },
   { "-pi_prev", FP_VALUE_NEG_PI_PREV },
   { "-pi", FP_VALUE_NEG_PI },
   { "-pi_next", FP_VALUE_NEG_PI_NEXT },
   { "+max_finite_prev", FP_VALUE_POS_MAX_FINITE_PREV },
   { "+max_finite", FP_VALUE_POS_MAX_FINITE },
   { "-max_finite_prev", FP_VALUE_NEG_MAX_FINITE_PREV },
   { "-max_finite", FP_VALUE_NEG_MAX_FINITE },
   { "+inf", FP_VALUE_POS_INF },
   { "-inf", FP_VALUE_NEG_INF },
   { "+qnan", FP_VALUE_POS_QNAN },
   { "-qnan", FP_VALUE_NEG_QNAN },
   { "+snan", FP_VALUE_POS_SNAN },
   { "-snan", FP_VALUE_NEG_SNAN },
   { "-int_min_prev", FP_VALUE_NEG_INT_MIN_PREV },
   { "-int_min", FP_VALUE_NEG_INT_MIN },
   { "-int_min_next", FP_VALUE_NEG_INT_MIN_NEXT },
   { "+int_max_prev", FP_VALUE_POS_INT_MAX_PREV },
   { "+int_max", FP_VALUE_POS_INT_MAX },
   { "+int_max_next", FP_VALUE_POS_INT_MAX_NEXT },
   { "+int_max_plus_one_prev", FP_VALUE_POS_INT_MAX_PLUS_ONE_PREV },
   { "+int_max_plus_one", FP_VALUE_POS_INT_MAX_PLUS_ONE },
   { "+int_max_plus_one_next", FP_VALUE_POS_INT_MAX_PLUS_ONE_NEXT },
   { "+uint_max_prev", FP_VALUE_POS_UINT_MAX_PREV },
   { "+uint_max", FP_VALUE_POS_UINT_MAX },
   { "+uint_max_next", FP_VALUE_POS_UINT_MAX_NEXT },
   { "+uint_max_plus_one_prev", FP_VALUE_POS_UINT_MAX_PLUS_ONE_PREV },
   { "+uint_max_plus_one", FP_VALUE_POS_UINT_MAX_PLUS_ONE },
   { "+uint_max_plus_one_next", FP_VALUE_POS_UINT_MAX_PLUS_ONE_NEXT },
   { "-uint_zero_prev", FP_VALUE_NEG_UINT_ZERO_PREV },
   { "-uint_zero", FP_VALUE_NEG_UINT_ZERO },
   { "+uint_zero", FP_VALUE_POS_UINT_ZERO },
   { "+uint_zero_next", FP_VALUE_POS_UINT_ZERO_NEXT },
};

static unsigned
alu_base_type(nir_alu_type type)
{
   return nir_alu_type_get_base_type(type);
}

static unsigned
alu_bit_size(nir_alu_type type, unsigned bit_size)
{
   unsigned type_size = nir_alu_type_get_type_size(type);
   if (type_size)
      return type_size;

   return alu_base_type(type) == nir_type_bool ? 1 : bit_size;
}

static bool
is_float_op(const nir_op_info *info)
{
   if (alu_base_type(info->output_type) == nir_type_float)
      return true;

   for (unsigned i = 0; i < info->num_inputs; i++) {
      if (alu_base_type(info->input_types[i]) == nir_type_float)
         return true;
   }

   return false;
}

static unsigned
float_fraction_bits(unsigned bit_size)
{
   switch (bit_size) {
   case 16:
      return 10;
   case 32:
      return 23;
   case 64:
      return 52;
   default:
      UNREACHABLE("invalid float bit size");
   }
}

static unsigned
float_exponent_bits(unsigned bit_size)
{
   switch (bit_size) {
   case 16:
      return 5;
   case 32:
      return 8;
   case 64:
      return 11;
   default:
      UNREACHABLE("invalid float bit size");
   }
}

static uint64_t
float_sign_bit(unsigned bit_size)
{
   return UINT64_C(1) << (bit_size - 1);
}

static uint64_t
float_fraction_mask(unsigned bit_size)
{
   return (UINT64_C(1) << float_fraction_bits(bit_size)) - 1;
}

static uint64_t
float_pack(unsigned bit_size, bool sign, uint64_t exponent, uint64_t fraction)
{
   uint64_t sign_bit = sign ? float_sign_bit(bit_size) : 0;

   return sign_bit | (exponent << float_fraction_bits(bit_size)) | fraction;
}

static uint64_t
float_max_exponent(unsigned bit_size)
{
   return (UINT64_C(1) << float_exponent_bits(bit_size)) - 1;
}

static uint64_t
float_nan_bits(unsigned bit_size, bool sign, bool quiet)
{
   unsigned fraction_bits = float_fraction_bits(bit_size);
   uint64_t quiet_bit = UINT64_C(1) << (fraction_bits - 1);
   uint64_t fraction = quiet ? quiet_bit : 1;

   return float_pack(bit_size, sign, float_max_exponent(bit_size), fraction);
}

static bool
float_bits_is_nan(unsigned bit_size, uint64_t bits)
{
   switch (bit_size) {
   case 16:
      return util_is_half_nan(bits);
   case 32:
      return util_is_nan(uif(bits));
   case 64:
      return util_is_double_nan(uid(bits));
   default:
      UNREACHABLE("invalid float bit size");
   }
}

static bool
float_bits_is_inf(unsigned bit_size, uint64_t bits)
{
   switch (bit_size) {
   case 16:
      return util_is_half_inf_or_nan(bits) && !util_is_half_nan(bits);
   case 32:
      return util_is_inf_or_nan(uif(bits)) && !util_is_nan(uif(bits));
   case 64:
      return util_is_double_inf_or_nan(uid(bits)) &&
             !util_is_double_nan(uid(bits));
   default:
      UNREACHABLE("invalid float bit size");
   }
}

static float
fp16_max_finite(void)
{
   return ldexpf(2.0f - ldexpf(1.0f, -10), 15);
}

static uint64_t
fp16_value_bits(enum fp_value_kind kind)
{
   const unsigned bit_size = 16;
   const uint64_t sign_bit = float_sign_bit(16);
   const uint64_t min_subnormal = 1;
   const uint64_t max_subnormal = float_pack(16, false, 0,
                                             float_fraction_mask(16));
   const uint64_t max_finite =
      float_pack(16, false, float_max_exponent(16) - 1,
                 float_fraction_mask(16));
   const uint64_t pos_one = _mesa_float_to_half(1.0f);
   const uint64_t neg_one = _mesa_float_to_half(-1.0f);
   const uint64_t pos_pi = _mesa_float_to_half((float)M_PI);
   const uint64_t neg_pi = _mesa_float_to_half((float)-M_PI);
   const uint64_t pos_max_finite = _mesa_float_to_half(fp16_max_finite());
   const uint64_t neg_max_finite = _mesa_float_to_half(-fp16_max_finite());
   const uint64_t neg_int_min = _mesa_float_to_half((float)INT16_MIN);
   const uint64_t pos_int_max = _mesa_float_to_half((float)INT16_MAX);
   const uint64_t pos_int_max_plus_one = _mesa_float_to_half(ldexpf(1.0f, 15));
   const uint64_t pos_uint_max = _mesa_float_to_half((float)UINT16_MAX);
   const uint64_t pos_uint_max_plus_one = _mesa_float_to_half(ldexpf(1.0f, 16));

   switch (kind) {
   case FP_VALUE_POS_ZERO:
      return _mesa_float_to_half(0.0f);
   case FP_VALUE_NEG_ZERO:
      return _mesa_float_to_half(-0.0f);
   case FP_VALUE_POS_MIN_SUBNORMAL:
      return min_subnormal;
   case FP_VALUE_NEG_MIN_SUBNORMAL:
      return sign_bit | min_subnormal;
   case FP_VALUE_POS_MAX_SUBNORMAL:
      return max_subnormal;
   case FP_VALUE_NEG_MAX_SUBNORMAL:
      return sign_bit | max_subnormal;
   case FP_VALUE_POS_MIN_NORMAL:
      return _mesa_float_to_half(ldexpf(1.0f, -14));
   case FP_VALUE_NEG_MIN_NORMAL:
      return _mesa_float_to_half(-ldexpf(1.0f, -14));
   case FP_VALUE_POS_ONE:
      return pos_one;
   case FP_VALUE_NEG_ONE:
      return neg_one;
   case FP_VALUE_POS_ONE_PREV:
      return pos_one - 1;
   case FP_VALUE_POS_ONE_NEXT:
      return pos_one + 1;
   case FP_VALUE_NEG_ONE_PREV:
      return neg_one + 1;
   case FP_VALUE_NEG_ONE_NEXT:
      return neg_one - 1;
   case FP_VALUE_POS_PI_PREV:
      return pos_pi - 1;
   case FP_VALUE_POS_PI:
      return pos_pi;
   case FP_VALUE_POS_PI_NEXT:
      return pos_pi + 1;
   case FP_VALUE_NEG_PI_PREV:
      return neg_pi + 1;
   case FP_VALUE_NEG_PI:
      return neg_pi;
   case FP_VALUE_NEG_PI_NEXT:
      return neg_pi - 1;
   case FP_VALUE_POS_MAX_FINITE_PREV:
      return max_finite - 1;
   case FP_VALUE_POS_MAX_FINITE:
      return pos_max_finite;
   case FP_VALUE_NEG_MAX_FINITE_PREV:
      return (sign_bit | max_finite) - 1;
   case FP_VALUE_NEG_MAX_FINITE:
      return neg_max_finite;
   case FP_VALUE_POS_INF:
      return float_pack(16, false, float_max_exponent(16), 0);
   case FP_VALUE_NEG_INF:
      return float_pack(16, true, float_max_exponent(16), 0);
   case FP_VALUE_POS_QNAN:
      return float_nan_bits(bit_size, false, true);
   case FP_VALUE_NEG_QNAN:
      return float_nan_bits(bit_size, true, true);
   case FP_VALUE_POS_SNAN:
      return float_nan_bits(bit_size, false, false);
   case FP_VALUE_NEG_SNAN:
      return float_nan_bits(bit_size, true, false);
   case FP_VALUE_NEG_INT_MIN_PREV:
      return neg_int_min + 1;
   case FP_VALUE_NEG_INT_MIN:
      return neg_int_min;
   case FP_VALUE_NEG_INT_MIN_NEXT:
      return neg_int_min - 1;
   case FP_VALUE_POS_INT_MAX_PREV:
      return pos_int_max - 1;
   case FP_VALUE_POS_INT_MAX:
      return pos_int_max;
   case FP_VALUE_POS_INT_MAX_NEXT:
      return pos_int_max + 1;
   case FP_VALUE_POS_INT_MAX_PLUS_ONE_PREV:
      return pos_int_max_plus_one - 1;
   case FP_VALUE_POS_INT_MAX_PLUS_ONE:
      return pos_int_max_plus_one;
   case FP_VALUE_POS_INT_MAX_PLUS_ONE_NEXT:
      return pos_int_max_plus_one + 1;
   case FP_VALUE_POS_UINT_MAX_PREV:
      return pos_uint_max - 1;
   case FP_VALUE_POS_UINT_MAX:
      return pos_uint_max;
   case FP_VALUE_POS_UINT_MAX_NEXT:
      return pos_uint_max + 1;
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE_PREV:
      return pos_uint_max_plus_one - 1;
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE:
      return pos_uint_max_plus_one;
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE_NEXT:
      return pos_uint_max_plus_one + 1;
   case FP_VALUE_NEG_UINT_ZERO_PREV:
      return sign_bit | min_subnormal;
   case FP_VALUE_NEG_UINT_ZERO:
      return _mesa_float_to_half(-0.0f);
   case FP_VALUE_POS_UINT_ZERO:
      return _mesa_float_to_half(0.0f);
   case FP_VALUE_POS_UINT_ZERO_NEXT:
      return min_subnormal;
   default:
      UNREACHABLE("invalid FP value kind");
   }
}

static uint64_t
fp32_value_bits(enum fp_value_kind kind)
{
   switch (kind) {
   case FP_VALUE_POS_ZERO:
      return fui(0.0f);
   case FP_VALUE_NEG_ZERO:
      return fui(-0.0f);
   case FP_VALUE_POS_MIN_SUBNORMAL:
      return fui(ldexpf(1.0f, FLT_MIN_EXP - FLT_MANT_DIG));
   case FP_VALUE_NEG_MIN_SUBNORMAL:
      return fui(-ldexpf(1.0f, FLT_MIN_EXP - FLT_MANT_DIG));
   case FP_VALUE_POS_MAX_SUBNORMAL:
      return fui(nextafterf(FLT_MIN, 0.0f));
   case FP_VALUE_NEG_MAX_SUBNORMAL:
      return fui(-nextafterf(FLT_MIN, 0.0f));
   case FP_VALUE_POS_MIN_NORMAL:
      return fui(FLT_MIN);
   case FP_VALUE_NEG_MIN_NORMAL:
      return fui(-FLT_MIN);
   case FP_VALUE_POS_ONE:
      return fui(1.0f);
   case FP_VALUE_NEG_ONE:
      return fui(-1.0f);
   case FP_VALUE_POS_ONE_PREV:
      return fui(nextafterf(1.0f, 0.0f));
   case FP_VALUE_POS_ONE_NEXT:
      return fui(nextafterf(1.0f, INFINITY));
   case FP_VALUE_NEG_ONE_PREV:
      return fui(nextafterf(-1.0f, -INFINITY));
   case FP_VALUE_NEG_ONE_NEXT:
      return fui(nextafterf(-1.0f, 0.0f));
   case FP_VALUE_POS_PI_PREV:
      return fui(nextafterf((float)M_PI, 0.0f));
   case FP_VALUE_POS_PI:
      return fui((float)M_PI);
   case FP_VALUE_POS_PI_NEXT:
      return fui(nextafterf((float)M_PI, INFINITY));
   case FP_VALUE_NEG_PI_PREV:
      return fui(nextafterf((float)-M_PI, -INFINITY));
   case FP_VALUE_NEG_PI:
      return fui((float)-M_PI);
   case FP_VALUE_NEG_PI_NEXT:
      return fui(nextafterf((float)-M_PI, 0.0f));
   case FP_VALUE_POS_MAX_FINITE_PREV:
      return fui(nextafterf(FLT_MAX, 0.0f));
   case FP_VALUE_POS_MAX_FINITE:
      return fui(FLT_MAX);
   case FP_VALUE_NEG_MAX_FINITE_PREV:
      return fui(nextafterf(-FLT_MAX, 0.0f));
   case FP_VALUE_NEG_MAX_FINITE:
      return fui(-FLT_MAX);
   case FP_VALUE_POS_INF:
      return fui(INFINITY);
   case FP_VALUE_NEG_INF:
      return fui(-INFINITY);
   case FP_VALUE_POS_QNAN:
      return float_nan_bits(32, false, true);
   case FP_VALUE_NEG_QNAN:
      return float_nan_bits(32, true, true);
   case FP_VALUE_POS_SNAN:
      return float_nan_bits(32, false, false);
   case FP_VALUE_NEG_SNAN:
      return float_nan_bits(32, true, false);
   case FP_VALUE_NEG_INT_MIN_PREV:
      return fui(nextafterf((float)INT32_MIN, -INFINITY));
   case FP_VALUE_NEG_INT_MIN:
      return fui((float)INT32_MIN);
   case FP_VALUE_NEG_INT_MIN_NEXT:
      return fui(nextafterf((float)INT32_MIN, INFINITY));
   case FP_VALUE_POS_INT_MAX_PREV:
      return fui(nextafterf((float)INT32_MAX, -INFINITY));
   case FP_VALUE_POS_INT_MAX:
      return fui((float)INT32_MAX);
   case FP_VALUE_POS_INT_MAX_NEXT:
      return fui(nextafterf((float)INT32_MAX, INFINITY));
   case FP_VALUE_POS_INT_MAX_PLUS_ONE_PREV:
      return fui(nextafterf(ldexpf(1.0f, 31), -INFINITY));
   case FP_VALUE_POS_INT_MAX_PLUS_ONE:
      return fui(ldexpf(1.0f, 31));
   case FP_VALUE_POS_INT_MAX_PLUS_ONE_NEXT:
      return fui(nextafterf(ldexpf(1.0f, 31), INFINITY));
   case FP_VALUE_POS_UINT_MAX_PREV:
      return fui(nextafterf((float)UINT32_MAX, -INFINITY));
   case FP_VALUE_POS_UINT_MAX:
      return fui((float)UINT32_MAX);
   case FP_VALUE_POS_UINT_MAX_NEXT:
      return fui(nextafterf((float)UINT32_MAX, INFINITY));
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE_PREV:
      return fui(nextafterf(ldexpf(1.0f, 32), -INFINITY));
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE:
      return fui(ldexpf(1.0f, 32));
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE_NEXT:
      return fui(nextafterf(ldexpf(1.0f, 32), INFINITY));
   case FP_VALUE_NEG_UINT_ZERO_PREV:
      return fui(-0x1p-149f);
   case FP_VALUE_NEG_UINT_ZERO:
      return fui(-0.0f);
   case FP_VALUE_POS_UINT_ZERO:
      return fui(0.0f);
   case FP_VALUE_POS_UINT_ZERO_NEXT:
      return fui(0x1p-149f);
   default:
      UNREACHABLE("invalid FP value kind");
   }
}

static uint64_t
fp64_value_bits(enum fp_value_kind kind)
{
   switch (kind) {
   case FP_VALUE_POS_ZERO:
      return dui(0.0);
   case FP_VALUE_NEG_ZERO:
      return dui(-0.0);
   case FP_VALUE_POS_MIN_SUBNORMAL:
      return dui(ldexp(1.0, DBL_MIN_EXP - DBL_MANT_DIG));
   case FP_VALUE_NEG_MIN_SUBNORMAL:
      return dui(-ldexp(1.0, DBL_MIN_EXP - DBL_MANT_DIG));
   case FP_VALUE_POS_MAX_SUBNORMAL:
      return dui(nextafter(DBL_MIN, 0.0));
   case FP_VALUE_NEG_MAX_SUBNORMAL:
      return dui(-nextafter(DBL_MIN, 0.0));
   case FP_VALUE_POS_MIN_NORMAL:
      return dui(DBL_MIN);
   case FP_VALUE_NEG_MIN_NORMAL:
      return dui(-DBL_MIN);
   case FP_VALUE_POS_ONE:
      return dui(1.0);
   case FP_VALUE_NEG_ONE:
      return dui(-1.0);
   case FP_VALUE_POS_ONE_PREV:
      return dui(nextafter(1.0, 0.0));
   case FP_VALUE_POS_ONE_NEXT:
      return dui(nextafter(1.0, INFINITY));
   case FP_VALUE_NEG_ONE_PREV:
      return dui(nextafter(-1.0, -INFINITY));
   case FP_VALUE_NEG_ONE_NEXT:
      return dui(nextafter(-1.0, 0.0));
   case FP_VALUE_POS_PI_PREV:
      return dui(nextafter(M_PI, 0.0));
   case FP_VALUE_POS_PI:
      return dui(M_PI);
   case FP_VALUE_POS_PI_NEXT:
      return dui(nextafter(M_PI, INFINITY));
   case FP_VALUE_NEG_PI_PREV:
      return dui(nextafter(-M_PI, -INFINITY));
   case FP_VALUE_NEG_PI:
      return dui(-M_PI);
   case FP_VALUE_NEG_PI_NEXT:
      return dui(nextafter(-M_PI, 0.0));
   case FP_VALUE_POS_MAX_FINITE_PREV:
      return dui(nextafter(DBL_MAX, 0.0));
   case FP_VALUE_POS_MAX_FINITE:
      return dui(DBL_MAX);
   case FP_VALUE_NEG_MAX_FINITE_PREV:
      return dui(nextafter(-DBL_MAX, 0.0));
   case FP_VALUE_NEG_MAX_FINITE:
      return dui(-DBL_MAX);
   case FP_VALUE_POS_INF:
      return dui(INFINITY);
   case FP_VALUE_NEG_INF:
      return dui(-INFINITY);
   case FP_VALUE_POS_QNAN:
      return float_nan_bits(64, false, true);
   case FP_VALUE_NEG_QNAN:
      return float_nan_bits(64, true, true);
   case FP_VALUE_POS_SNAN:
      return float_nan_bits(64, false, false);
   case FP_VALUE_NEG_SNAN:
      return float_nan_bits(64, true, false);
   case FP_VALUE_NEG_INT_MIN_PREV:
      return dui(nextafter((double)INT64_MIN, -INFINITY));
   case FP_VALUE_NEG_INT_MIN:
      return dui((double)INT64_MIN);
   case FP_VALUE_NEG_INT_MIN_NEXT:
      return dui(nextafter((double)INT64_MIN, INFINITY));
   case FP_VALUE_POS_INT_MAX_PREV:
      return dui(nextafter((double)INT64_MAX, -INFINITY));
   case FP_VALUE_POS_INT_MAX:
      return dui((double)INT64_MAX);
   case FP_VALUE_POS_INT_MAX_NEXT:
      return dui(nextafter((double)INT64_MAX, INFINITY));
   case FP_VALUE_POS_INT_MAX_PLUS_ONE_PREV:
      return dui(nextafter(ldexp(1.0, 63), -INFINITY));
   case FP_VALUE_POS_INT_MAX_PLUS_ONE:
      return dui(ldexp(1.0, 63));
   case FP_VALUE_POS_INT_MAX_PLUS_ONE_NEXT:
      return dui(nextafter(ldexp(1.0, 63), INFINITY));
   case FP_VALUE_POS_UINT_MAX_PREV:
      return dui(nextafter((double)UINT64_MAX, -INFINITY));
   case FP_VALUE_POS_UINT_MAX:
      return dui((double)UINT64_MAX);
   case FP_VALUE_POS_UINT_MAX_NEXT:
      return dui(nextafter((double)UINT64_MAX, INFINITY));
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE_PREV:
      return dui(nextafter(ldexp(1.0, 64), -INFINITY));
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE:
      return dui(ldexp(1.0, 64));
   case FP_VALUE_POS_UINT_MAX_PLUS_ONE_NEXT:
      return dui(nextafter(ldexp(1.0, 64), INFINITY));
   case FP_VALUE_NEG_UINT_ZERO_PREV:
      return dui(-ldexp(1.0, DBL_MIN_EXP - DBL_MANT_DIG));
   case FP_VALUE_NEG_UINT_ZERO:
      return dui(-0.0);
   case FP_VALUE_POS_UINT_ZERO:
      return dui(0.0);
   case FP_VALUE_POS_UINT_ZERO_NEXT:
      return dui(ldexp(1.0, DBL_MIN_EXP - DBL_MANT_DIG));
   default:
      UNREACHABLE("invalid FP value kind");
   }
}

static uint64_t
fp_value_bits(enum fp_value_kind kind, unsigned bit_size)
{
   switch (bit_size) {
   case 16:
      return fp16_value_bits(kind);
   case 32:
      return fp32_value_bits(kind);
   case 64:
      return fp64_value_bits(kind);
   default:
      UNREACHABLE("invalid float bit size");
   }
}

static uint64_t
fp_random_u64(unsigned bit_size, unsigned idx, unsigned stream)
{
   uint64_t seed[2];

   s_rand_xorshift128plus(seed, false);
   seed[0] ^= ((uint64_t)bit_size << 32) ^ idx;
   seed[1] ^= stream;

   return rand_xorshift128plus(seed);
}

static uint64_t
fp_generated_ordinary_bits(unsigned bit_size, unsigned idx)
{
   uint64_t max_exponent = float_max_exponent(bit_size);
   uint64_t exponent =
      2 + fp_random_u64(bit_size, idx, 0) % (max_exponent - 3);
   uint64_t fraction =
      fp_random_u64(bit_size, idx, 1) & float_fraction_mask(bit_size);
   bool sign = fp_random_u64(bit_size, idx, 2) & 1;

   return float_pack(bit_size, sign, exponent, fraction);
}

static uint64_t
fp_generated_nan_bits(unsigned bit_size, unsigned idx)
{
   unsigned fraction_bits = float_fraction_bits(bit_size);
   uint64_t quiet_bit = UINT64_C(1) << (fraction_bits - 1);
   uint64_t fraction =
      fp_random_u64(bit_size, idx, 3) & float_fraction_mask(bit_size);
   bool sign = fp_random_u64(bit_size, idx, 4) & 1;
   bool quiet = fp_random_u64(bit_size, idx, 5) & 1;

   if (quiet) {
      fraction |= quiet_bit;
   } else {
      fraction &= ~quiet_bit;
      if (fraction == 0)
         fraction = 1;
   }

   uint64_t bits =
      float_pack(bit_size, sign, float_max_exponent(bit_size), fraction);

   assert(float_bits_is_nan(bit_size, bits));
   return bits;
}

static uint64_t
fp_generated_inf_bits(unsigned bit_size, unsigned idx)
{
   bool sign = fp_random_u64(bit_size, idx, 6) & 1;
   uint64_t bits = float_pack(bit_size, sign, float_max_exponent(bit_size), 0);

   assert(float_bits_is_inf(bit_size, bits));
   return bits;
}

static unsigned
fp_case_index(enum fp_value_kind kind)
{
   for (unsigned i = 0; i < ARRAY_SIZE(fp_values); i++) {
      if (fp_values[i].kind == kind)
         return i;
   }

   UNREACHABLE("missing FP case");
}

static struct source_value
source_value_make(uint64_t bits, const char *name)
{
   struct source_value value = {};
   value.bits = bits;

   snprintf(value.name, sizeof(value.name), "%s", name);
   return value;
}

static unsigned
float_value_count(unsigned bit_size)
{
   switch (bit_size) {
   case 16:
   case 32:
   case 64:
      return ARRAY_SIZE(fp_values) + FP_GENERATED_ORDINARY_COUNT +
             FP_GENERATED_NAN_COUNT + FP_GENERATED_INF_COUNT;
   default:
      UNREACHABLE("invalid float bit size");
   }
}

static struct source_value
float_value(unsigned bit_size, unsigned idx)
{
   idx %= float_value_count(bit_size);

   if (idx < ARRAY_SIZE(fp_values)) {
      const struct fp_value_case *value = &fp_values[idx];

      return source_value_make(fp_value_bits(value->kind, bit_size),
                               value->name);
   }

   idx -= ARRAY_SIZE(fp_values);

   if (idx < FP_GENERATED_ORDINARY_COUNT) {
      struct source_value value = {};
      value.bits = fp_generated_ordinary_bits(bit_size, idx);

      snprintf(value.name, sizeof(value.name), "rand_ordinary_%02u", idx);
      return value;
   }

   idx -= FP_GENERATED_ORDINARY_COUNT;

   if (idx < FP_GENERATED_NAN_COUNT) {
      struct source_value value = {};
      value.bits = fp_generated_nan_bits(bit_size, idx);
      bool quiet =
         value.bits & (UINT64_C(1) << (float_fraction_bits(bit_size) - 1));

      snprintf(value.name, sizeof(value.name), "rand_%cnan_%02u",
               quiet ? 'q' : 's', idx);
      return value;
   }

   idx -= FP_GENERATED_NAN_COUNT;

   if (idx < FP_GENERATED_INF_COUNT) {
      struct source_value value = {};
      value.bits = fp_generated_inf_bits(bit_size, idx);

      snprintf(value.name, sizeof(value.name), "rand_inf_%02u", idx);
      return value;
   }

   UNREACHABLE("invalid generated FP value index");
}

static struct source_value
typed_value(nir_alu_type type, unsigned bit_size, unsigned idx)
{
   unsigned base = alu_base_type(type);
   unsigned bits = alu_bit_size(type, bit_size);

   if (base == nir_type_float)
      return float_value(bits, idx);

   if (base == nir_type_bool)
      return source_value_make(idx & 1, (idx & 1) ? "true" : "false");

   return source_value_make(int_values[idx % ARRAY_SIZE(int_values)], "");
}

static unsigned
typed_value_count(nir_alu_type type, unsigned bit_size)
{
   if (alu_base_type(type) == nir_type_float)
      return float_value_count(alu_bit_size(type, bit_size));

   if (alu_base_type(type) == nir_type_bool)
      return 2;

   return ARRAY_SIZE(int_values);
}

static void
set_const_value(nir_const_value *value, nir_alu_type type, unsigned bit_size,
                uint64_t raw)
{
   unsigned base = alu_base_type(type);
   unsigned bits = alu_bit_size(type, bit_size);

   memset(value, 0, sizeof(*value));

   if (base == nir_type_bool || bits == 1) {
      value->b = raw != 0;
   } else if (bits == 8) {
      value->u8 = raw;
   } else if (bits == 16) {
      value->u16 = raw;
   } else if (bits == 32) {
      value->u32 = raw;
   } else {
      value->u64 = raw;
   }
}

static uint64_t
get_const_value(nir_const_value value, nir_alu_type type, unsigned bit_size)
{
   unsigned base = alu_base_type(type);
   unsigned bits = alu_bit_size(type, bit_size);

   if (base == nir_type_bool || bits == 1)
      return value.b ? 1 : 0;

   switch (bits) {
   case 8:
      return value.u8;
   case 16:
      return value.u16;
   case 32:
      return value.u32;
   case 64:
      return value.u64;
   default:
      UNREACHABLE("invalid bit size");
   }
}

static void
print_hex(uint64_t value, unsigned bit_size)
{
   if (bit_size <= 8)
      printf("%02" PRIx64, (uint64_t)(value & UINT64_C(0xff)));
   else if (bit_size <= 16)
      printf("%04" PRIx64, (uint64_t)(value & UINT64_C(0xffff)));
   else if (bit_size <= 32)
      printf("%08" PRIx64, (uint64_t)(value & UINT64_C(0xffffffff)));
   else
      printf("%016" PRIx64, value);
}

static unsigned
component_count(const nir_op_info *info)
{
   unsigned count = MAX2(info->output_size, 1);

   for (unsigned i = 0; i < info->num_inputs; i++)
      count = MAX2(count, info->input_sizes[i]);

   return count;
}

static unsigned
src_components(const nir_op_info *info, unsigned src, unsigned num_components)
{
   return info->input_sizes[src] ? info->input_sizes[src] : num_components;
}

static unsigned
dst_components(const nir_op_info *info, unsigned num_components)
{
   return info->output_size ? info->output_size : num_components;
}

static bool
mode_applies(unsigned mode, unsigned bit_size)
{
   if (mode == 0)
      return true;

   if (bit_size == 16)
      return mode & (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 |
                     FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16 |
                     FLOAT_CONTROLS_DENORM_PRESERVE_FP16);
   if (bit_size == 32)
      return mode & (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32 |
                     FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32 |
                     FLOAT_CONTROLS_DENORM_PRESERVE_FP32);
   if (bit_size == 64)
      return mode & (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64 |
                     FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP64 |
                     FLOAT_CONTROLS_DENORM_PRESERVE_FP64);

   return false;
}

static void
fill_source_pattern(const nir_op_info *info, unsigned src_idx,
                    unsigned bit_size, unsigned num_components,
                    unsigned pattern, nir_const_value *src,
                    struct source_value *values)
{
   unsigned comps = src_components(info, src_idx, num_components);
   nir_alu_type type = info->input_types[src_idx];

   for (unsigned c = 0; c < comps; c++) {
      unsigned idx = pattern + src_idx * 7 + c * 13;

      values[c] = typed_value(type, bit_size, idx);
      set_const_value(&src[c], type, bit_size, values[c].bits);
   }
}

static void
print_source_value(const struct source_value *value, unsigned bit_size)
{
   if (value->name[0]) {
      printf("%s/", value->name);
      print_hex(value->bits, bit_size);
   } else {
      print_hex(value->bits, bit_size);
   }
}

static void
evaluate_case(nir_op op, unsigned bit_size, unsigned exec_mode,
              unsigned case_id, const nir_const_value srcs[][NIR_MAX_VEC_COMPONENTS],
              struct source_value src_values[][NIR_MAX_VEC_COMPONENTS])
{
   const nir_op_info *info = &nir_op_infos[op];
   nir_const_value dest[NIR_MAX_VEC_COMPONENTS];
   nir_const_value *src_ptrs[NIR_ALU_MAX_INPUTS];
   uint16_t poison = 0;
   unsigned num_components = component_count(info);
   unsigned dest_components = dst_components(info, num_components);
   unsigned dest_bits = alu_bit_size(info->output_type, bit_size);

   memset(dest, 0, sizeof(dest));
   for (unsigned i = 0; i < info->num_inputs; i++)
      src_ptrs[i] = (nir_const_value *)srcs[i];

   nir_eval_const_opcode(op, dest, &poison, num_components, bit_size, src_ptrs,
                         exec_mode);

   printf("%s,%u,%u,%u,%u,",
          info->name, bit_size, exec_mode, case_id, poison);

   for (unsigned c = 0; c < dest_components; c++) {
      if (c)
         printf(":");
      print_hex(get_const_value(dest[c], info->output_type, bit_size),
                dest_bits);
   }

   printf(",");
   for (unsigned s = 0; s < info->num_inputs; s++) {
      unsigned comps = src_components(info, s, num_components);
      unsigned src_bits = alu_bit_size(info->input_types[s], bit_size);

      if (s)
         printf(";");
      printf("s%u=", s);

      for (unsigned c = 0; c < comps; c++) {
         if (c)
            printf(":");
         print_source_value(&src_values[s][c], src_bits);
      }
   }

   printf("\n");
}

static unsigned
bit_width_count(const nir_op_info *info, unsigned bit_widths[3])
{
   bool has_generic_float = false;
   unsigned fixed_float = 0;

   auto scan_type = [&](nir_alu_type type) {
      if (alu_base_type(type) != nir_type_float)
         return;

      unsigned type_size = nir_alu_type_get_type_size(type);
      if (type_size == 0)
         has_generic_float = true;
      else if (fixed_float == 0)
         fixed_float = type_size;
   };

   scan_type(info->output_type);
   for (unsigned i = 0; i < info->num_inputs; i++)
      scan_type(info->input_types[i]);

   if (has_generic_float) {
      bit_widths[0] = 16;
      bit_widths[1] = 32;
      bit_widths[2] = 64;
      return 3;
   }

   if (fixed_float) {
      bit_widths[0] = fixed_float;
      return 1;
   }

   return 0;
}

static unsigned
pattern_case_count(const nir_op_info *info, unsigned bit_size)
{
   unsigned count = 1;

   for (unsigned i = 0; i < info->num_inputs; i++)
      count = MAX2(count, typed_value_count(info->input_types[i], bit_size));

   return count;
}

static void
run_scalar_binary_cases(nir_op op, unsigned bit_size, unsigned exec_mode,
                        unsigned *case_id)
{
   const nir_op_info *info = &nir_op_infos[op];
   nir_const_value srcs[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
   struct source_value src_values[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
   unsigned c0 = typed_value_count(info->input_types[0], bit_size);
   unsigned c1 = typed_value_count(info->input_types[1], bit_size);

   for (unsigned i = 0; i < c0; i++) {
      for (unsigned j = 0; j < c1; j++) {
         src_values[0][0] = typed_value(info->input_types[0], bit_size, i);
         src_values[1][0] = typed_value(info->input_types[1], bit_size, j);
         set_const_value(&srcs[0][0], info->input_types[0], bit_size,
                         src_values[0][0].bits);
         set_const_value(&srcs[1][0], info->input_types[1], bit_size,
                         src_values[1][0].bits);
         evaluate_case(op, bit_size, exec_mode, (*case_id)++, srcs,
                       src_values);
      }
   }
}

static void
run_pattern_cases(nir_op op, unsigned bit_size, unsigned exec_mode,
                  unsigned *case_id)
{
   const nir_op_info *info = &nir_op_infos[op];
   nir_const_value srcs[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
   struct source_value src_values[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
   const unsigned patterns = pattern_case_count(info, bit_size);
   unsigned num_components = component_count(info);

   for (unsigned p = 0; p < patterns; p++) {
      memset(srcs, 0, sizeof(srcs));
      memset(src_values, 0, sizeof(src_values));
      for (unsigned s = 0; s < info->num_inputs; s++) {
         fill_source_pattern(info, s, bit_size, num_components,
                             p + s * 3, srcs[s], src_values[s]);
      }

      evaluate_case(op, bit_size, exec_mode, (*case_id)++, srcs, src_values);
   }

   if (info->num_inputs == 3 &&
       alu_base_type(info->input_types[0]) == nir_type_float &&
       alu_base_type(info->input_types[1]) == nir_type_float &&
       alu_base_type(info->input_types[2]) == nir_type_float) {
      const unsigned triples[][3] = {
         {
            fp_case_index(FP_VALUE_POS_ZERO),
            fp_case_index(FP_VALUE_POS_INF),
            fp_case_index(FP_VALUE_POS_ONE),
         },
         {
            fp_case_index(FP_VALUE_NEG_ZERO),
            fp_case_index(FP_VALUE_POS_INF),
            fp_case_index(FP_VALUE_NEG_ONE),
         },
         {
            fp_case_index(FP_VALUE_POS_ONE),
            fp_case_index(FP_VALUE_POS_INF),
            fp_case_index(FP_VALUE_POS_QNAN),
         },
         {
            fp_case_index(FP_VALUE_POS_QNAN),
            fp_case_index(FP_VALUE_POS_ONE),
            fp_case_index(FP_VALUE_POS_ZERO),
         },
         {
            fp_case_index(FP_VALUE_POS_ONE),
            fp_case_index(FP_VALUE_NEG_ONE),
            fp_case_index(FP_VALUE_POS_ONE),
         },
      };

      for (unsigned t = 0; t < ARRAY_SIZE(triples); t++) {
         memset(srcs, 0, sizeof(srcs));
         memset(src_values, 0, sizeof(src_values));

         for (unsigned s = 0; s < 3; s++) {
            src_values[s][0] =
               typed_value(info->input_types[s], bit_size, triples[t][s]);
            set_const_value(&srcs[s][0], info->input_types[s], bit_size,
                            src_values[s][0].bits);
         }

         evaluate_case(op, bit_size, exec_mode, (*case_id)++, srcs,
                       src_values);
      }
   }
}

int
main(int argc, char **argv)
{
   const char *only_op = NULL;

   for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--op") == 0 && i + 1 < argc) {
         only_op = argv[++i];
      } else {
         fprintf(stderr, "usage: %s [--op opcode]\n", argv[0]);
         return 2;
      }
   }

   printf("opcode,bit_size,exec_mode,case_id,poison,result,sources\n");

   for (unsigned op_idx = 0; op_idx < nir_num_opcodes; op_idx++) {
      nir_op op = (nir_op)op_idx;
      const nir_op_info *info = &nir_op_infos[op];

      if (info->name[0] != 'f')
         continue;

      if (only_op && strcmp(info->name, only_op) != 0)
         continue;

      if (!is_float_op(info))
         continue;

      unsigned bit_widths[3];
      unsigned num_bit_widths = bit_width_count(info, bit_widths);

      for (unsigned b = 0; b < num_bit_widths; b++) {
         unsigned bit_size = bit_widths[b];
         for (unsigned m = 0; m < ARRAY_SIZE(exec_modes); m++) {
            unsigned exec_mode = exec_modes[m];
            unsigned case_id = 0;

            if (!mode_applies(exec_mode, bit_size))
               continue;

            if (info->num_inputs == 2 &&
                src_components(info, 0, component_count(info)) == 1 &&
                src_components(info, 1, component_count(info)) == 1)
               run_scalar_binary_cases(op, bit_size, exec_mode, &case_id);
            else
               run_pattern_cases(op, bit_size, exec_mode, &case_id);
         }
      }
   }

   return 0;
}
