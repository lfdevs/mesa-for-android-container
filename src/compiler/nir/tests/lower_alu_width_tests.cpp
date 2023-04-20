/*
 * Copyright © 2023 Valve Corporation
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

namespace {

class nir_lower_alu_width_test : public ::testing::Test {
protected:
   nir_lower_alu_width_test();
   ~nir_lower_alu_width_test();

   nir_builder b;
};

nir_lower_alu_width_test::nir_lower_alu_width_test()
{
   glsl_type_singleton_init_or_ref();

   static const nir_shader_compiler_options options = { };
   b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, &options, "nir_lower_alu_width test");
}

nir_lower_alu_width_test::~nir_lower_alu_width_test()
{
   ralloc_free(b.shader);
   glsl_type_singleton_decref();
}

TEST_F(nir_lower_alu_width_test, fdot_order)
{
   nir_variable *res_var = nir_local_variable_create(b.impl, glsl_float_type(), "res");

   b.exact = true;

   /* If this isn't done in xyz order, it evaluates to infinity. */
   nir_ssa_def *val = nir_fdot(
      &b, nir_imm_vec3(&b, 1.7014118346046923e+38, 1.7014118346046923e+38, 8.507059173023462e+37),
      nir_imm_vec3(&b, -0.5, 1.5, 1.0));
   nir_intrinsic_instr *store =
      nir_build_store_deref(&b, &nir_build_deref_var(&b, res_var)->dest.ssa, val);

   nir_lower_alu_width(b.shader, NULL, NULL);
   nir_opt_constant_folding(b.shader);

   ASSERT_TRUE(nir_src_is_const(store->src[1]));
   EXPECT_EQ(nir_src_as_float(store->src[1]), 2.5521177519070385e+38);
}

}
