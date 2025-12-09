/*
 * Copyright © 2020 Intel Corporation
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

#include "nir_test.h"

class nir_opt_if_test : public nir_test {
protected:
   nir_opt_if_test();

   nir_builder bld;

   nir_def *in_def;
   nir_variable *out_var;
};

nir_opt_if_test::nir_opt_if_test()
   : nir_test::nir_test("nir_opt_if_test")
{
   nir_variable *var = nir_variable_create(b->shader, nir_var_shader_in, glsl_int_type(), "in");
   in_def = nir_load_var(b, var);

   out_var = nir_variable_create(b->shader, nir_var_shader_out, glsl_int_type(), "out");
}

TEST_F(nir_opt_if_test, opt_if_simplification)
{
   /* Tests that opt_if_simplification correctly optimizes a simple case:
    *
    * vec1 1 ssa_2 = ieq ssa_0, ssa_1
    * if ssa_2 {
    *    block block_2:
    * } else {
    *    block block_3:
    *    do_work()
    * }
    */

   nir_def *one = nir_imm_int(b, 1);

   nir_def *cmp_result = nir_ieq(b, in_def, one);
   nir_if *nif = nir_push_if(b, cmp_result);

   nir_push_else(b, NULL);

   // do_work
   nir_store_var(b, out_var, one, 1);

   nir_pop_if(b, NULL);

   ASSERT_TRUE(nir_opt_if(b->shader, nir_opt_if_optimize_phi_true_false));

   nir_validate_shader(b->shader, NULL);

   ASSERT_TRUE(!exec_list_is_empty((&nir_if_first_then_block(nif)->instr_list)));
   ASSERT_TRUE(exec_list_is_empty((&nir_if_first_else_block(nif)->instr_list)));
}

TEST_F(nir_opt_if_test, opt_if_simplification_single_source_phi_after_if)
{
   /* Tests that opt_if_simplification correctly handles single-source
    * phis after the if.
    *
    * vec1 1 ssa_2 = ieq ssa_0, ssa_1
    * if ssa_2 {
    *    block block_2:
    * } else {
    *    block block_3:
    *    do_work()
    *    return
    * }
    * block block_4:
    * vec1 32 ssa_3 = phi block_2: ssa_0
    */

   nir_def *one = nir_imm_int(b, 1);

   nir_def *cmp_result = nir_ieq(b, in_def, one);
   nir_if *nif = nir_push_if(b, cmp_result);

   nir_push_else(b, NULL);

   // do_work
   nir_store_var(b, out_var, one, 1);

   nir_jump_instr *jump = nir_jump_instr_create(b->shader, nir_jump_return);
   nir_builder_instr_insert(b, &jump->instr);

   nir_pop_if(b, NULL);

   nir_block *then_block = nir_if_last_then_block(nif);

   nir_phi_instr *const phi = nir_phi_instr_create(b->shader);

   nir_phi_instr_add_src(phi, then_block, one);

   nir_def_init(&phi->instr, &phi->def,
                one->num_components, one->bit_size);

   nir_builder_instr_insert(b, &phi->instr);

   ASSERT_TRUE(nir_opt_if(b->shader, nir_opt_if_optimize_phi_true_false));

   nir_validate_shader(b->shader, NULL);

   ASSERT_TRUE(nir_block_ends_in_jump(nir_if_last_then_block(nif)));
   ASSERT_TRUE(exec_list_is_empty((&nir_if_first_else_block(nif)->instr_list)));
}

TEST_F(nir_opt_if_test, opt_if_alu_of_phi_progress)
{
   nir_def *two = nir_imm_int(b, 2);
   nir_def *x = nir_imm_int(b, 0);

   nir_phi_instr *phi = nir_phi_instr_create(b->shader);

   nir_loop *loop = nir_push_loop(b);
   {
      nir_def_init(&phi->instr, &phi->def,
                   x->num_components, x->bit_size);

      nir_phi_instr_add_src(phi, nir_def_block(x), x);

      nir_def *y = nir_iadd(b, &phi->def, two);
      nir_store_var(b, out_var,
                    nir_imul(b, &phi->def, two), 1);

      nir_phi_instr_add_src(phi, nir_cursor_current_block(b->cursor), y);
   }
   nir_pop_loop(b, loop);

   b->cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(b, &phi->instr);

   nir_validate_shader(b->shader, "input");

   bool progress;

   int progress_count = 0;
   for (int i = 0; i < 10; i++) {
      progress = nir_opt_if(b->shader, nir_opt_if_optimize_phi_true_false);
      if (progress)
         progress_count++;
      else
         break;
      nir_opt_constant_folding(b->shader);
   }

   EXPECT_LE(progress_count, 2);
   ASSERT_FALSE(progress);
}

static const struct nir_opt_if_merge_test_config {
   template <class ParamType>
   std::string
   operator()(const ::testing::TestParamInfo<ParamType> &info) const
   {
      return info.param.name;
   }

   const char *name;

   /* Test parameter: Location of a return statement within the control flow */
   enum return_location {
      NO_RETURN,
      RETURN_IN_1ST_THEN,
      RETURN_IN_1ST_ELSE,
      RETURN_IN_2ND_THEN,
      RETURN_IN_2ND_ELSE,
   } return_location;
} nir_opt_if_merge_test_configs [] = {
   { "no_return",          nir_opt_if_merge_test_config::NO_RETURN, },
   { "return_in_1st_then", nir_opt_if_merge_test_config::RETURN_IN_1ST_THEN, },
   { "return_in_1st_else", nir_opt_if_merge_test_config::RETURN_IN_1ST_ELSE, },
   { "return_in_2nd_then", nir_opt_if_merge_test_config::RETURN_IN_2ND_THEN, },
   { "return_in_2nd_else", nir_opt_if_merge_test_config::RETURN_IN_2ND_ELSE, },
};

class nir_opt_if_merge_test
   : public nir_opt_if_test
   , public ::testing::WithParamInterface<struct nir_opt_if_merge_test_config>
{
protected:
   virtual void SetUp();

   enum nir_opt_if_merge_test_config::return_location return_location;
};

void
nir_opt_if_merge_test::SetUp()
{
   struct nir_opt_if_merge_test_config config = GetParam();
   return_location = config.return_location;
}

INSTANTIATE_TEST_SUITE_P(
   nir_opt_if, nir_opt_if_merge_test,
   ::testing::ValuesIn(nir_opt_if_merge_test_configs),
   nir_opt_if_merge_test_config()
);

TEST_P(nir_opt_if_merge_test, opt_if_merge)
{
   /* Tests that opt_if_merge correctly merges if statements with the same
    * condition, parameterized on the location of a return statement added
    * within the control-flow.
    *
    * block b0:   // preds:
    * 32     %0 = deref_var &in (shader_in int)
    * 32     %1 = @load_deref (%0) (access=none)
    * 32     %2 = load_const (0x00000001)
    * 32     %3 = load_const (0x00000002)
    * 32     %4 = load_const (0x00000006)
    * 1      %5 = ige %1, %2 (0x1)
    *             // succs: b1 b2
    * if %5 {
    *     block b1:   // preds: b0
    *     32     %6 = iadd %1, %2 (0x1)
    *     32     %7 = deref_var &out (shader_out int)
    *                 @store_deref (%7, %6) (wrmask=x, access=none)
    *                 // succs: b3
    * } else {
    *     block b2:   // preds: b0
    *     32     %8 = iadd %1, %3 (0x2)
    *     32     %9 = deref_var &out (shader_out int)
    *                 @store_deref (%9, %8) (wrmask=x, access=none)
    *                 // succs: b3
    * }
    * block b3:  // preds: b1 b2, succs: b4 b5
    * if %5 {
    *     block b4:   // preds: b3
    *     32    %10 = imul %1, %3 (0x2)
    *                 // succs: b6
    * } else {
    *     block b5:   // preds: b3
    *     32    %11 = imul %1, %4 (0x6)
    *                 // succs: b6
    * }
    * block b6:   // preds: b4 b5
    * 32    %12 = phi b4: %10, b5: %11
    * 32    %13 = deref_var &out (shader_out int)
    *             @store_deref (%13, %12) (wrmask=x, access=none)
    *             // succs: b7
    * block b7:
    */

   int instr_in_1st_then = 3, instr_in_1st_else = 3;
   int instr_in_2nd_then = 1, instr_in_2nd_else = 1;

   nir_def *one = nir_imm_int(b, 1);
   nir_def *two = nir_imm_int(b, 2);
   nir_def *six = nir_imm_int(b, 6);

   nir_def *cmp_result = nir_ige(b, in_def, one);

   nir_if *nif = nir_push_if(b, cmp_result);

   nir_store_var(b, out_var, nir_iadd(b, in_def, one), 1);
   if (return_location == nir_opt_if_merge_test_config::RETURN_IN_1ST_THEN) {
      nir_jump(b, nir_jump_return);
      instr_in_1st_then++;
   }

   nir_push_else(b, NULL);

   nir_store_var(b, out_var, nir_iadd(b, in_def, two), 1);
   if (return_location == nir_opt_if_merge_test_config::RETURN_IN_1ST_ELSE) {
      nir_jump(b, nir_jump_return);
      instr_in_1st_else++;
   }

   nir_pop_if(b, NULL);

   nir_phi_instr *phi = nir_phi_instr_create(b->shader);
   nir_def_init(&phi->instr, &phi->def,
                one->num_components, one->bit_size);

   nir_if *next_if = nir_push_if(b, cmp_result);

   nir_def *y1 = nir_imul(b, in_def, two);
   if (return_location == nir_opt_if_merge_test_config::RETURN_IN_2ND_THEN) {
      nir_jump(b, nir_jump_return);
      instr_in_2nd_then++;
   } else {
      nir_phi_instr_add_src(phi, nir_cursor_current_block(b->cursor), y1);
   }

   nir_push_else(b, NULL);

   nir_def *y2 = nir_imul(b, in_def, six);
   if (return_location == nir_opt_if_merge_test_config::RETURN_IN_2ND_ELSE) {
      nir_jump(b, nir_jump_return);
      instr_in_2nd_else++;
   } else {
      nir_phi_instr_add_src(phi, nir_cursor_current_block(b->cursor), y2);
   }

   nir_pop_if(b, NULL);

   nir_builder_instr_insert(b, &phi->instr);

   // do_work
   nir_store_var(b, out_var, &phi->def, 1);

   if (return_location == nir_opt_if_merge_test_config::RETURN_IN_2ND_THEN ||
       return_location == nir_opt_if_merge_test_config::RETURN_IN_2ND_ELSE) {
      ASSERT_FALSE(nir_opt_if(b->shader, nir_opt_if_optimize_phi_true_false));
   } else if (return_location == nir_opt_if_merge_test_config::NO_RETURN) {
      ASSERT_TRUE(nir_opt_if(b->shader, nir_opt_if_optimize_phi_true_false));
      instr_in_1st_then += instr_in_2nd_then;
      instr_in_1st_else += instr_in_2nd_else;
      instr_in_2nd_then = 0;
      instr_in_2nd_else = 0;
   } else {
      ASSERT_TRUE(nir_opt_if(b->shader, nir_opt_if_optimize_phi_true_false));
   }

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(exec_list_length((&nir_if_first_then_block(nif)->instr_list)), instr_in_1st_then);
   ASSERT_EQ(exec_list_length((&nir_if_first_else_block(nif)->instr_list)), instr_in_1st_else);
   ASSERT_EQ(exec_list_length((&nir_if_first_then_block(next_if)->instr_list)), instr_in_2nd_then);
   ASSERT_EQ(exec_list_length((&nir_if_first_else_block(next_if)->instr_list)), instr_in_2nd_else);
}
