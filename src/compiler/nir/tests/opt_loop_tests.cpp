/*
 * Copyright © 2024 Valve Corporation
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

class nir_opt_loop_test : public nir_test {
protected:
   nir_opt_loop_test();

   nir_deref_instr *add_loop_terminators(nir_if **term1, nir_if **term2,
                                         bool deref_array);
   void create_loop_phis(nir_loop *loop, nir_if *term1, nir_if *term2,
                         nir_def *def1, nir_def *def2);

   nir_def *in_def;
   nir_variable *out_var;
   nir_variable *ubo_var;
   nir_variable *ubo_var_array;
};

nir_opt_loop_test::nir_opt_loop_test()
   : nir_test::nir_test("nir_opt_loop_test")
{
   nir_variable *var = nir_variable_create(b->shader, nir_var_shader_in, glsl_int_type(), "in");
   in_def = nir_load_var(b, var);

   ubo_var = nir_variable_create(b->shader, nir_var_mem_ubo, glsl_int_type(), "ubo1");
   ubo_var_array = nir_variable_create(b->shader, nir_var_mem_ubo, glsl_array_type(glsl_int_type(), 4, 0), "ubo_array");

   out_var = nir_variable_create(b->shader, nir_var_shader_out, glsl_int_type(), "out");
}

nir_deref_instr *
nir_opt_loop_test::add_loop_terminators(nir_if **term1, nir_if **term2,
                                        bool deref_array)
{
   /* Add first terminator */
   nir_def *one = nir_imm_int(b, 1);
   nir_def *cmp_result = nir_ieq(b, in_def, one);
   nir_if *nif = nir_push_if(b, cmp_result);
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, nif);

   if (term1)
      *term1 = nif;

   nir_deref_instr *deref;
   if (deref_array) {
      nir_def *index = nir_imm_int(b, 3);
      deref = nir_build_deref_array(b, nir_build_deref_var(b, ubo_var_array), index);
   } else {
      deref = nir_build_deref_var(b, ubo_var);
   }
   nir_def *ubo_def = nir_load_deref(b, deref);

   /* Add second terminator */
   nir_def *two = nir_imm_int(b, 2);
   nir_def *cmp_result2 = nir_ieq(b, ubo_def, two);
   nir_if *nif2 = nir_push_if(b, cmp_result2);
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, nif2);

   if (term2)
      *term2 = nif2;

   return deref;
}

void
nir_opt_loop_test::create_loop_phis(nir_loop *loop,
                                    nir_if *term1, nir_if *term2,
                                    nir_def *def1, nir_def *def2)
{
   nir_phi_instr *phi_instr = nir_phi_instr_create(b->shader);
   nir_def_init(&phi_instr->instr, &phi_instr->def, 1, 32);
   nir_phi_instr_add_src(phi_instr, nir_if_first_then_block(term1), def1);
   nir_phi_instr_add_src(phi_instr, nir_if_first_then_block(term2), def2);

   nir_instr_insert(nir_after_cf_node(&loop->cf_node),
                    &phi_instr->instr);
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_deref_after_first_if)
{
   /* Tests that opt_loop_merge_terminators creates valid nir after it merges
    * terminators that have a deref statement between them:
    */
   nir_loop *loop = nir_push_loop(b);

   nir_deref_instr *deref = add_loop_terminators(NULL, NULL, false);

   /* Load from deref that will be moved inside the continue branch of the
    * first if-statements continue block. If not handled correctly during
    * the merge this will fail nir validation.
    */
   nir_def *ubo_def = nir_load_deref(b, deref);
   nir_store_var(b, out_var, ubo_def, 1);

   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_deref_phi_index)
{
   /* Tests that opt_loop_merge_terminators creates valid nir after it merges
    * terminators that have a deref statement and index value between them and
    * where that deref and index are both later used again later in the code:
    */
   nir_loop *loop = nir_push_loop(b);

   nir_deref_instr *deref = add_loop_terminators(NULL, NULL, true);

   /* Load from deref that will be moved inside the continue branch of the
    * first if-statements continue block. If not handled correctly during
    * the merge this will fail nir validation.
    */
   nir_def *ubo_def = nir_load_deref(b, deref);
   nir_store_var(b, out_var, ubo_def, 1);

   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_skip_merge_if_phis)
{
   /* Tests that opt_loop_merge_terminators skips merging the terminators if
    * the loop has phis. We can update or remove this test if support for
    * phis is added to this pass:
    */
   nir_deref_instr *deref = nir_build_deref_var(b, ubo_var);
   nir_def *ubo_def = nir_load_deref(b, deref);

   nir_loop *loop = nir_push_loop(b);

   nir_if *term1;
   nir_if *term2;
   add_loop_terminators(&term1, &term2, false);

   nir_pop_loop(b, loop);

   create_loop_phis(loop, term1, term2, in_def, ubo_def);

   ASSERT_FALSE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_skip_merge_if_phis_nested_loop)
{
   /* Tests that opt_loop_merge_terminators skips merging the terminators if
    * the loop has phis. We can update or remove this test if support for
    * phis is added to this pass:
    */
   nir_deref_instr *deref = nir_build_deref_var(b, ubo_var);
   nir_def *ubo_def = nir_load_deref(b, deref);

   nir_loop *loop = nir_push_loop(b);

   /* Add a nested loop to make sure we test the correct loop for trailing phis */
   nir_loop *nested_loop = nir_push_loop(b);
   nir_pop_loop(b, nested_loop);

   nir_if *term1;
   nir_if *term2;
   add_loop_terminators(&term1, &term2, false);

   nir_pop_loop(b, loop);

   create_loop_phis(loop, term1, term2, in_def, ubo_def);

   ASSERT_FALSE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);
}
