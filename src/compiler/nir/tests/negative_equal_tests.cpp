/*
 * Copyright © 2018 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <gtest/gtest.h>
#include "nir.h"
#include "nir_builder.h"
#include "util/half_float.h"

static void count_sequence(nir_const_value c[NIR_MAX_VEC_COMPONENTS],
                           nir_alu_type base_type, unsigned bits, int first);
static void negate(nir_const_value dst[NIR_MAX_VEC_COMPONENTS],
                   const nir_const_value src[NIR_MAX_VEC_COMPONENTS],
                   nir_alu_type base_type, unsigned bits, unsigned components);

class const_value_negative_equal_test : public ::testing::Test {
protected:
   const_value_negative_equal_test()
   {
      memset(c1, 0, sizeof(c1));
      memset(c2, 0, sizeof(c2));
   }

   ~const_value_negative_equal_test()
   {
      /* empty */
   }

   nir_const_value c1[NIR_MAX_VEC_COMPONENTS];
   nir_const_value c2[NIR_MAX_VEC_COMPONENTS];
};

class alu_srcs_negative_equal_test : public ::testing::Test {
protected:
   alu_srcs_negative_equal_test()
   {
      static const nir_shader_compiler_options options = { };
      nir_builder_init_simple_shader(&bld, NULL, MESA_SHADER_VERTEX, &options);
   }

   ~alu_srcs_negative_equal_test()
   {
      ralloc_free(bld.shader);
   }

   struct nir_builder bld;
};

TEST_F(const_value_negative_equal_test, float32_zero)
{
   /* Verify that 0.0 negative-equals 0.0. */
   EXPECT_TRUE(nir_const_value_negative_equal(c1, c1, NIR_MAX_VEC_COMPONENTS,
                                              nir_type_float, 32));
}

TEST_F(const_value_negative_equal_test, float64_zero)
{
   /* Verify that 0.0 negative-equals 0.0. */
   EXPECT_TRUE(nir_const_value_negative_equal(c1, c1, NIR_MAX_VEC_COMPONENTS,
                                              nir_type_float, 64));
}

/* Compare an object with non-zero values to itself.  This should always be
 * false.
 */
#define compare_with_self(base_type, bits) \
TEST_F(const_value_negative_equal_test, base_type ## bits ## _self)     \
{                                                                       \
   count_sequence(c1, base_type, bits, 1);                              \
   EXPECT_FALSE(nir_const_value_negative_equal(c1, c1,                  \
                                               NIR_MAX_VEC_COMPONENTS,  \
                                               base_type, bits));       \
}

compare_with_self(nir_type_float, 16)
compare_with_self(nir_type_float, 32)
compare_with_self(nir_type_float, 64)
compare_with_self(nir_type_int, 8)
compare_with_self(nir_type_uint, 8)
compare_with_self(nir_type_int, 16)
compare_with_self(nir_type_uint, 16)
compare_with_self(nir_type_int, 32)
compare_with_self(nir_type_uint, 32)
compare_with_self(nir_type_int, 64)
compare_with_self(nir_type_uint, 64)

/* Compare an object with the negation of itself.  This should always be true.
 */
#define compare_with_negation(base_type, bits) \
TEST_F(const_value_negative_equal_test, base_type ## bits ## _trivially_true) \
{                                                                       \
   count_sequence(c1, base_type, bits, 1);                              \
   negate(c2, c1, base_type, bits, NIR_MAX_VEC_COMPONENTS);             \
   EXPECT_TRUE(nir_const_value_negative_equal(c1, c2,                   \
                                              NIR_MAX_VEC_COMPONENTS,   \
                                              base_type, bits));        \
}

compare_with_negation(nir_type_float, 16)
compare_with_negation(nir_type_float, 32)
compare_with_negation(nir_type_float, 64)
compare_with_negation(nir_type_int, 8)
compare_with_negation(nir_type_uint, 8)
compare_with_negation(nir_type_int, 16)
compare_with_negation(nir_type_uint, 16)
compare_with_negation(nir_type_int, 32)
compare_with_negation(nir_type_uint, 32)
compare_with_negation(nir_type_int, 64)
compare_with_negation(nir_type_uint, 64)

/* Compare fewer than the maximum possible components.  All of the components
 * that are compared a negative-equal, but the extra components are not.
 */
#define compare_fewer_components(base_type, bits) \
TEST_F(const_value_negative_equal_test, base_type ## bits ## _fewer_components) \
{                                                                       \
   count_sequence(c1, base_type, bits, 1);                              \
   negate(c2, c1, base_type, bits, 3);                                  \
   EXPECT_TRUE(nir_const_value_negative_equal(c1, c2, 3, base_type, bits)); \
   EXPECT_FALSE(nir_const_value_negative_equal(c1, c2,                  \
                                               NIR_MAX_VEC_COMPONENTS,  \
                                               base_type, bits));       \
}

compare_fewer_components(nir_type_float, 16)
compare_fewer_components(nir_type_float, 32)
compare_fewer_components(nir_type_float, 64)
compare_fewer_components(nir_type_int, 8)
compare_fewer_components(nir_type_uint, 8)
compare_fewer_components(nir_type_int, 16)
compare_fewer_components(nir_type_uint, 16)
compare_fewer_components(nir_type_int, 32)
compare_fewer_components(nir_type_uint, 32)
compare_fewer_components(nir_type_int, 64)
compare_fewer_components(nir_type_uint, 64)

TEST_F(alu_srcs_negative_equal_test, trivial_float)
{
   nir_ssa_def *two = nir_imm_float(&bld, 2.0f);
   nir_ssa_def *negative_two = nir_imm_float(&bld, -2.0f);

   nir_ssa_def *result = nir_fadd(&bld, two, negative_two);
   nir_alu_instr *instr = nir_instr_as_alu(result->parent_instr);

   ASSERT_NE((void *) 0, instr);
   EXPECT_TRUE(nir_alu_srcs_negative_equal(instr, instr, 0, 1));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 0, 0));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 1, 1));
}

TEST_F(alu_srcs_negative_equal_test, trivial_int)
{
   nir_ssa_def *two = nir_imm_int(&bld, 2);
   nir_ssa_def *negative_two = nir_imm_int(&bld, -2);

   nir_ssa_def *result = nir_iadd(&bld, two, negative_two);
   nir_alu_instr *instr = nir_instr_as_alu(result->parent_instr);

   ASSERT_NE((void *) 0, instr);
   EXPECT_TRUE(nir_alu_srcs_negative_equal(instr, instr, 0, 1));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 0, 0));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 1, 1));
}

TEST_F(alu_srcs_negative_equal_test, trivial_negation_float)
{
   /* Cannot just do the negation of a nir_load_const_instr because
    * nir_alu_srcs_negative_equal expects that constant folding will convert
    * fneg(2.0) to just -2.0.
    */
   nir_ssa_def *two = nir_imm_float(&bld, 2.0f);
   nir_ssa_def *two_plus_two = nir_fadd(&bld, two, two);
   nir_ssa_def *negation = nir_fneg(&bld, two_plus_two);

   nir_ssa_def *result = nir_fadd(&bld, two_plus_two, negation);

   nir_alu_instr *instr = nir_instr_as_alu(result->parent_instr);

   ASSERT_NE((void *) 0, instr);
   EXPECT_TRUE(nir_alu_srcs_negative_equal(instr, instr, 0, 1));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 0, 0));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 1, 1));
}

TEST_F(alu_srcs_negative_equal_test, trivial_negation_int)
{
   /* Cannot just do the negation of a nir_load_const_instr because
    * nir_alu_srcs_negative_equal expects that constant folding will convert
    * ineg(2) to just -2.
    */
   nir_ssa_def *two = nir_imm_int(&bld, 2);
   nir_ssa_def *two_plus_two = nir_iadd(&bld, two, two);
   nir_ssa_def *negation = nir_ineg(&bld, two_plus_two);

   nir_ssa_def *result = nir_iadd(&bld, two_plus_two, negation);

   nir_alu_instr *instr = nir_instr_as_alu(result->parent_instr);

   ASSERT_NE((void *) 0, instr);
   EXPECT_TRUE(nir_alu_srcs_negative_equal(instr, instr, 0, 1));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 0, 0));
   EXPECT_FALSE(nir_alu_srcs_negative_equal(instr, instr, 1, 1));
}

static void
count_sequence(nir_const_value c[NIR_MAX_VEC_COMPONENTS], nir_alu_type base_type, unsigned bits, int first)
{
   switch (base_type) {
   case nir_type_float:
      switch (bits) {
      case 16:
         for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
            c[i].u16 = _mesa_float_to_half(float(i + first));

         break;

      case 32:
         for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
            c[i].f32 = float(i + first);

         break;

      case 64:
         for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
            c[i].f64 = double(i + first);

         break;

      default:
         unreachable("unknown bit size");
      }

      break;

   case nir_type_int:
   case nir_type_uint:
      switch (bits) {
      case 8:
         for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
            c[i].i8 = i + first;

         break;

      case 16:
         for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
            c[i].i16 = i + first;

         break;

      case 32:
         for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
            c[i].i32 = i + first;

         break;

      case 64:
         for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; i++)
            c[i].i64 = i + first;

         break;

      default:
         unreachable("unknown bit size");
      }

      break;

   case nir_type_bool:
   default:
      unreachable("invalid base type");
   }
}

static void
negate(nir_const_value dst[NIR_MAX_VEC_COMPONENTS],
       const nir_const_value src[NIR_MAX_VEC_COMPONENTS],
       nir_alu_type base_type, unsigned bits, unsigned components)
{
   switch (base_type) {
   case nir_type_float:
      switch (bits) {
      case 16:
         for (unsigned i = 0; i < components; i++)
            dst[i].u16 = _mesa_float_to_half(-_mesa_half_to_float(src[i].u16));

         break;

      case 32:
         for (unsigned i = 0; i < components; i++)
            dst[i].f32 = -src[i].f32;

         break;

      case 64:
         for (unsigned i = 0; i < components; i++)
            dst[i].f64 = -src[i].f64;

         break;

      default:
         unreachable("unknown bit size");
      }

      break;

   case nir_type_int:
   case nir_type_uint:
      switch (bits) {
      case 8:
         for (unsigned i = 0; i < components; i++)
            dst[i].i8 = -src[i].i8;

         break;

      case 16:
         for (unsigned i = 0; i < components; i++)
            dst[i].i16 = -src[i].i16;

         break;

      case 32:
         for (unsigned i = 0; i < components; i++)
            dst[i].i32 = -src[i].i32;

         break;

      case 64:
         for (unsigned i = 0; i < components; i++)
            dst[i].i64 = -src[i].i64;

         break;

      default:
         unreachable("unknown bit size");
      }

      break;

   case nir_type_bool:
   default:
      unreachable("invalid base type");
   }
}
