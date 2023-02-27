/*
 * Copyright © 2022 Pavel Ondračka
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

class nir_opt_shrink_vectors_test : public ::testing::Test {
protected:
   nir_opt_shrink_vectors_test();
   ~nir_opt_shrink_vectors_test();

   nir_builder bld;

   nir_ssa_def *in_def;
   nir_variable *out_var;
};

nir_opt_shrink_vectors_test::nir_opt_shrink_vectors_test()
{
   glsl_type_singleton_init_or_ref();

   static const nir_shader_compiler_options options = { };
   bld = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, &options, "opt shrink vectors test");

   nir_variable *var = nir_variable_create(bld.shader, nir_var_shader_in, glsl_vec_type(2), "in");
   in_def = nir_load_var(&bld, var);

   out_var = nir_variable_create(bld.shader, nir_var_shader_out, glsl_vec_type(1), "out");
}

nir_opt_shrink_vectors_test::~nir_opt_shrink_vectors_test()
{
   ralloc_free(bld.shader);
   glsl_type_singleton_decref();
}

static unsigned translate_swizzle(char swz)
{
   const char *swizzles_dict = "xyzw";
   const char *extended_swizzles_dict = "abcdefghijklmnop";

   const char *ptr = strchr(swizzles_dict, swz);
   if (ptr)
      return ptr - swizzles_dict;
   else
      return strchr(extended_swizzles_dict, swz) - extended_swizzles_dict;
}

static void set_swizzle(nir_alu_src * src, const char * swizzle)
{
   unsigned i = 0;
   while (swizzle[i]) {
      src->swizzle[i] = translate_swizzle(swizzle[i]);
      i++;
   }
}

static void check_swizzle(nir_alu_src * src, const char * swizzle)
{
   unsigned i = 0;
   while (swizzle[i]) {
      ASSERT_TRUE(src->swizzle[i] == translate_swizzle(swizzle[i]));
      i++;
   }
}

TEST_F(nir_opt_shrink_vectors_test, opt_shrink_vectors_load_const_trailing_component_only)
{
   /* Test that opt_shrink_vectors correctly removes unused trailing channels
    * of load_const.
    *
    * vec4 32 ssa_1 = load_const (1.0, 2.0, 3.0, 4.0)
    * vec1 32 ssa_2 = fmov ssa_1.x
    *
    * to
    *
    * vec1 32 ssa_1 = load_const (1.0)
    * vec1 32 ssa_2 = fmov ssa_1.x
    */

   nir_ssa_def *imm_vec = nir_imm_vec4(&bld, 1.0, 2.0, 3.0, 4.0);

   nir_ssa_def *alu_result = nir_build_alu1(&bld, nir_op_mov, imm_vec);
   nir_alu_instr *alu_instr = nir_instr_as_alu(alu_result->parent_instr);
   set_swizzle(&alu_instr->src[0], "x");
   alu_result->num_components = 1;
   alu_instr->dest.write_mask = BITFIELD_MASK(1);

   nir_store_var(&bld, out_var, alu_result, 1);

   ASSERT_TRUE(nir_opt_shrink_vectors(bld.shader));

   nir_validate_shader(bld.shader, NULL);

   ASSERT_TRUE(imm_vec->num_components == 1);
   nir_load_const_instr * imm_vec_instr = nir_instr_as_load_const(imm_vec->parent_instr);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[0], 32) == 1.0);

   ASSERT_FALSE(nir_opt_shrink_vectors(bld.shader));
}

TEST_F(nir_opt_shrink_vectors_test, opt_shrink_vectors_alu_trailing_component_only)
{
   /* Test that opt_shrink_vectors correctly removes unused trailing channels
    * of alus.
    *
    * vec4 32 ssa_1 = fmov ssa_0.xyzx
    * vec1 32 ssa_2 = fmov ssa_1.x
    *
    * to
    *
    * vec1 32 ssa_1 = fmov ssa_0.x
    * vec1 32 ssa_2 = fmov ssa_1.x
    */

   nir_ssa_def *alu_result = nir_build_alu1(&bld, nir_op_mov, in_def);
   nir_alu_instr *alu_instr = nir_instr_as_alu(alu_result->parent_instr);
   alu_result->num_components = 4;
   alu_instr->dest.write_mask = BITFIELD_MASK(4);
   set_swizzle(&alu_instr->src[0], "xyxx");

   nir_ssa_def *alu2_result = nir_build_alu1(&bld, nir_op_mov, alu_result);
   nir_alu_instr *alu2_instr = nir_instr_as_alu(alu2_result->parent_instr);
   set_swizzle(&alu2_instr->src[0], "x");
   alu2_result->num_components = 1;
   alu2_instr->dest.write_mask = BITFIELD_MASK(1);

   nir_store_var(&bld, out_var, alu2_result, 1);

   ASSERT_TRUE(nir_opt_shrink_vectors(bld.shader));

   nir_validate_shader(bld.shader, NULL);

   check_swizzle(&alu_instr->src[0], "x");
   ASSERT_TRUE(alu_result->num_components == 1);

   ASSERT_FALSE(nir_opt_shrink_vectors(bld.shader));
}

TEST_F(nir_opt_shrink_vectors_test, opt_shrink_vectors_simple)
{
   /* Tests that opt_shrink_vectors correctly shrinks a simple case.
    *
    * vec4 32 ssa_2 = load_const (3.0, 1.0, 2.0, 1.0)
    * vec4 32 ssa_3 = fadd ssa_1.xxxy, ssa_2.ywyz
    * vec1 32 ssa_4 = fdot3 ssa_3.xzw ssa_3.xzw
    *
    * to
    *
    * vec2 32 ssa_2 = load_const (1.0, 2.0)
    * vec2 32 ssa_3 = fadd ssa_1, ssa_2
    * vec1 32 ssa_4 = fdot3 ssa_3.xxy ssa_3.xxy
    */

   nir_ssa_def *imm_vec = nir_imm_vec4(&bld, 3.0, 1.0, 2.0, 1.0);

   nir_ssa_def *alu_result = nir_build_alu2(&bld, nir_op_fadd, in_def, imm_vec);
   nir_alu_instr *alu_instr = nir_instr_as_alu(alu_result->parent_instr);
   alu_result->num_components = 4;
   alu_instr->dest.write_mask = BITFIELD_MASK(4);
   set_swizzle(&alu_instr->src[0], "xxxy");
   set_swizzle(&alu_instr->src[1], "ywyz");

   nir_ssa_def *alu2_result = nir_build_alu2(&bld, nir_op_fdot3, alu_result, alu_result);
   nir_alu_instr *alu2_instr = nir_instr_as_alu(alu2_result->parent_instr);
   set_swizzle(&alu2_instr->src[0], "xzw");
   set_swizzle(&alu2_instr->src[1], "xzw");

   nir_store_var(&bld, out_var, alu2_result, 1);

   ASSERT_TRUE(nir_opt_shrink_vectors(bld.shader));

   nir_validate_shader(bld.shader, NULL);

   ASSERT_TRUE(imm_vec->num_components == 2);
   nir_load_const_instr * imm_vec_instr = nir_instr_as_load_const(imm_vec->parent_instr);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[0], 32) == 1.0);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[1], 32) == 2.0);

   ASSERT_TRUE(alu_result->num_components == 2);
   check_swizzle(&alu_instr->src[0], "xy");
   check_swizzle(&alu_instr->src[1], "xy");

   check_swizzle(&alu2_instr->src[0], "xxy");
   check_swizzle(&alu2_instr->src[1], "xxy");

   ASSERT_FALSE(nir_opt_shrink_vectors(bld.shader));

   nir_validate_shader(bld.shader, NULL);
}

TEST_F(nir_opt_shrink_vectors_test, opt_shrink_vectors_vec8)
{
   /* Tests that opt_shrink_vectors correctly shrinks a case
    * dealing with vec8 shrinking. The shrinking would result in
    * vec6 for load const and vec7 for fadd and is therefore not allowed,
    * but check that we still properly reuse the channels and move
    * the unused channels to the end.
    *
    * vec8 32 ssa_2 = load_const (1.0, 1.0, 2.0, 3.0, 4.0, 5.0, 2.0, 6.0)
    * vec8 32 ssa_3 = fadd ssa_1.xxxxxxxy, ssa_2.afhdefgh
    * vec1 32 ssa_4 = fdot8 ssa_3.accdefgh ssa_3.accdefgh
    *
    * to
    *
    * vec8 32 ssa_2 = load_const (1.0, 3.0, 4.0, 5.0, 2.0, 6.0, .., ..))
    * vec8 32 ssa_3 = fadd ssa_1.xxxxxxy_ ssa_2.afbcdef_
    * vec1 32 ssa_4 = fdot8 ssa_3.abbcdefg ssa_3.abbcdefg
    */

   nir_const_value v[8] = {
      nir_const_value_for_float(1.0, 32),
      nir_const_value_for_float(1.0, 32),
      nir_const_value_for_float(2.0, 32),
      nir_const_value_for_float(3.0, 32),
      nir_const_value_for_float(4.0, 32),
      nir_const_value_for_float(5.0, 32),
      nir_const_value_for_float(2.0, 32),
      nir_const_value_for_float(6.0, 32),
   };
   nir_ssa_def *imm_vec = nir_build_imm(&bld, 8, 32, v);

   nir_ssa_def *alu_result = nir_build_alu2(&bld, nir_op_fadd, in_def, imm_vec);
   nir_alu_instr *alu_instr = nir_instr_as_alu(alu_result->parent_instr);
   alu_result->num_components = 8;
   alu_instr->dest.write_mask = BITFIELD_MASK(8);
   set_swizzle(&alu_instr->src[0], "xxxxxxxy");
   set_swizzle(&alu_instr->src[1], "afhdefgh");

   nir_ssa_def *alu2_result = nir_build_alu2(&bld, nir_op_fdot8, alu_result, alu_result);
   nir_alu_instr *alu2_instr = nir_instr_as_alu(alu2_result->parent_instr);
   set_swizzle(&alu2_instr->src[0], "accdefgh");
   set_swizzle(&alu2_instr->src[1], "accdefgh");

   nir_store_var(&bld, out_var, alu2_result, 1);

   ASSERT_TRUE(nir_opt_shrink_vectors(bld.shader));

   nir_validate_shader(bld.shader, NULL);

   ASSERT_TRUE(imm_vec->num_components == 8);
   nir_load_const_instr * imm_vec_instr = nir_instr_as_load_const(imm_vec->parent_instr);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[0], 32) == 1.0);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[1], 32) == 3.0);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[2], 32) == 4.0);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[3], 32) == 5.0);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[4], 32) == 2.0);
   ASSERT_TRUE(nir_const_value_as_float(imm_vec_instr->value[5], 32) == 6.0);

   ASSERT_TRUE(alu_result->num_components == 8);
   check_swizzle(&alu_instr->src[0], "xxxxxxy");
   check_swizzle(&alu_instr->src[1], "afbcdef");

   check_swizzle(&alu2_instr->src[0], "abbcdefg");
   check_swizzle(&alu2_instr->src[1], "abbcdefg");

   ASSERT_FALSE(nir_opt_shrink_vectors(bld.shader));

   nir_validate_shader(bld.shader, NULL);
}
