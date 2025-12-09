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
                                         bool break_in_else, bool deref_array);
   void create_loop_phis(nir_loop *loop, nir_if *term1, nir_if *term2,
                         nir_def *def1, nir_def *def2);
   void test_merged_if(bool break_in_else);

   nir_def *in_def;
   nir_variable *out_var;
   nir_variable *ubo_var;
   nir_variable *ubo_var_array;
};

nir_opt_loop_test::nir_opt_loop_test()
   : nir_test::nir_test("nir_opt_loop_test", MESA_SHADER_FRAGMENT)
{
   nir_variable *var = nir_variable_create(b->shader, nir_var_shader_in, glsl_int_type(), "in");
   in_def = nir_load_var(b, var);

   ubo_var = nir_variable_create(b->shader, nir_var_mem_ubo, glsl_int_type(), "ubo1");
   ubo_var_array = nir_variable_create(b->shader, nir_var_mem_ubo, glsl_array_type(glsl_int_type(), 4, 0), "ubo_array");

   out_var = nir_variable_create(b->shader, nir_var_shader_out, glsl_int_type(), "out");
}

nir_deref_instr *
nir_opt_loop_test::add_loop_terminators(nir_if **term1, nir_if **term2,
                                        bool break_in_else, bool deref_array)
{
   /* Add first terminator */
   nir_def *one = nir_imm_int(b, 1);
   nir_def *cmp_result = nir_ieq(b, in_def, one);
   nir_if *nif = nir_push_if(b, cmp_result);

   if (break_in_else)
      nir_push_else(b, nif);

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

   if (break_in_else)
      nir_push_else(b, nif2);

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

void
nir_opt_loop_test::test_merged_if(bool break_in_else)
{
   /* Tests that opt_loop_merge_terminators results in valid nir and that
    * the test condition is correct based on the location of the break in
    * the terminators.
    */
   nir_loop *loop = nir_push_loop(b);

   nir_if *term1;
   nir_if *term2;
   add_loop_terminators(&term1, &term2, break_in_else, false);

   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_break_in_then)
{
   test_merged_if(false);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = deref_var &in (shader_in int)
          32     %1 = @load_deref (%0) (access=none)
                      // succs: b1
          loop {
              block b1:   // preds: b0 b7
              32     %2 = load_const (0x00000001)
              1      %3 = ieq %1, %2 (0x1)
                          // succs: b2 b3
              if %3 {
                  block b2:   // preds: b1
                  1      %4 = undefined
                              // succs: b4
              } else {
                  block b3:   // preds: b1
                  32     %5 = deref_var &ubo1 (ubo int)
                  32     %6 = @load_deref (%5) (access=none)
                  32     %7 = load_const (0x00000002)
                  1      %8 = ieq %6, %7 (0x2)
                              // succs: b4
              }
              block b4:   // preds: b2 b3
              1      %9 = phi b2: %4, b3: %8
              1     %10 = ior %9, %3
                          // succs: b5 b6
              if %10 {
                  block b5:// preds: b4
                  break
                  // succs: b8
              } else {
                  block b6:  // preds: b4, succs: b7
              }
              block b7:  // preds: b6, succs: b1
          }
          block b8:  // preds: b5, succs: b9
          block b9:
      }
   )"));
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_break_in_else)
{
   test_merged_if(true);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = deref_var &in (shader_in int)
          32     %1 = @load_deref (%0) (access=none)
                      // succs: b1
          loop {
              block b1:   // preds: b0 b7
              32     %2 = load_const (0x00000001)
              1      %3 = ieq %1, %2 (0x1)
                          // succs: b2 b3
              if %3 {
                  block b2:   // preds: b1
                  32     %4 = deref_var &ubo1 (ubo int)
                  32     %5 = @load_deref (%4) (access=none)
                  32     %6 = load_const (0x00000002)
                  1      %7 = ieq %5, %6 (0x2)
                              // succs: b4
              } else {
                  block b3:   // preds: b1
                  1      %8 = undefined
                              // succs: b4
              }
              block b4:   // preds: b2 b3
              1      %9 = phi b2: %7, b3: %8
              1     %10 = iand %9, %3
                          // succs: b5 b6
              if %10 {
                  block b5:  // preds: b4, succs: b7
              } else {
                  block b6:// preds: b4
                  break
                  // succs: b8
              }
              block b7:  // preds: b5, succs: b1
          }
          block b8:  // preds: b6, succs: b9
          block b9:
      }
   )"));
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_deref_after_first_if)
{
   /* Tests that opt_loop_merge_terminators creates valid nir after it merges
    * terminators that have a deref statement between them:
    */
   nir_loop *loop = nir_push_loop(b);

   nir_deref_instr *deref = add_loop_terminators(NULL, NULL, false, false);

   /* Load from deref that will be moved inside the continue branch of the
    * first if-statements continue block. If not handled correctly during
    * the merge this will fail nir validation.
    */
   nir_def *ubo_def = nir_load_deref(b, deref);
   nir_store_var(b, out_var, ubo_def, 1);

   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = deref_var &in (shader_in int)
          32     %1 = @load_deref (%0) (access=none)
                      // succs: b1
          loop {
              block b1:   // preds: b0 b7
              32     %2 = load_const (0x00000001)
              1      %3 = ieq %1, %2 (0x1)
                          // succs: b2 b3
              if %3 {
                  block b2:   // preds: b1
                  1      %4 = undefined
                              // succs: b4
              } else {
                  block b3:   // preds: b1
                  32     %5 = deref_var &ubo1 (ubo int)
                  32     %6 = @load_deref (%5) (access=none)
                  32     %7 = load_const (0x00000002)
                  1      %8 = ieq %6, %7 (0x2)
                              // succs: b4
              }
              block b4:   // preds: b2 b3
              1      %9 = phi b2: %4, b3: %8
              1     %10 = ior %9, %3
                          // succs: b5 b6
              if %10 {
                  block b5:// preds: b4
                  break
                  // succs: b8
              } else {
                  block b6:  // preds: b4, succs: b7
              }
              block b7:   // preds: b6
              32    %11 = deref_var &ubo1 (ubo int)
              32    %12 = @load_deref (%11) (access=none)
              32    %13 = deref_var &out (shader_out int)
                          @store_deref (%13, %12) (wrmask=x, access=none)
                          // succs: b1
          }
          block b8:  // preds: b5, succs: b9
          block b9:
      }
   )"));
}

TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_deref_phi_index)
{
   /* Tests that opt_loop_merge_terminators creates valid nir after it merges
    * terminators that have a deref statement and index value between them and
    * where that deref and index are both later used again later in the code:
    */
   nir_loop *loop = nir_push_loop(b);

   nir_deref_instr *deref = add_loop_terminators(NULL, NULL, false, true);

   /* Load from deref that will be moved inside the continue branch of the
    * first if-statements continue block. If not handled correctly during
    * the merge this will fail nir validation.
    */
   nir_def *ubo_def = nir_load_deref(b, deref);
   nir_store_var(b, out_var, ubo_def, 1);

   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = deref_var &in (shader_in int)
          32     %1 = @load_deref (%0) (access=none)
                      // succs: b1
          loop {
              block b1:   // preds: b0 b7
              32     %2 = load_const (0x00000001)
              1      %3 = ieq %1, %2 (0x1)
                          // succs: b2 b3
              if %3 {
                  block b2:   // preds: b1
                  1      %4 = undefined
                  32     %5 = undefined
                              // succs: b4
              } else {
                  block b3:   // preds: b1
                  32     %6 = load_const (0x00000003 = 0.000000)
                  32     %7 = deref_var &ubo_array (ubo int[4])
                  32     %8 = deref_array &(*%7)[3] (ubo int)  // &ubo_array[3]
                  32     %9 = @load_deref (%8) (access=none)
                  32    %10 = load_const (0x00000002)
                  1     %11 = ieq %9, %10 (0x2)
                              // succs: b4
              }
              block b4:   // preds: b2 b3
              1     %12 = phi b2: %4, b3: %11
              32    %13 = phi b2: %5, b3: %6 (0x3)
              1     %14 = ior %12, %3
                          // succs: b5 b6
              if %14 {
                  block b5:// preds: b4
                  break
                  // succs: b8
              } else {
                  block b6:  // preds: b4, succs: b7
              }
              block b7:   // preds: b6
              32    %15 = deref_var &ubo_array (ubo int[4])
              32    %16 = deref_array &(*%15)[%13] (ubo int)  // &ubo_array[%13]
              32    %17 = @load_deref (%16) (access=none)
              32    %18 = deref_var &out (shader_out int)
                          @store_deref (%18, %17) (wrmask=x, access=none)
                          // succs: b1
          }
          block b8:  // preds: b5, succs: b9
          block b9:
      }
   )"));
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
   add_loop_terminators(&term1, &term2, false, false);

   nir_pop_loop(b, loop);

   create_loop_phis(loop, term1, term2, in_def, ubo_def);

   ASSERT_FALSE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = deref_var &in (shader_in int)
          32     %1 = @load_deref (%0) (access=none)
          32     %2 = deref_var &ubo1 (ubo int)
          32     %3 = @load_deref (%2) (access=none)
                      // succs: b1
          loop {
              block b1:   // preds: b0 b7
              32     %4 = load_const (0x00000001)
              1      %5 = ieq %1, %4 (0x1)
                          // succs: b2 b3
              if %5 {
                  block b2:// preds: b1
                  break
                  // succs: b8
              } else {
                  block b3:  // preds: b1, succs: b4
              }
              block b4:   // preds: b3
              32     %6 = deref_var &ubo1 (ubo int)
              32     %7 = @load_deref (%6) (access=none)
              32     %8 = load_const (0x00000002)
              1      %9 = ieq %7, %8 (0x2)
                          // succs: b5 b6
              if %9 {
                  block b5:// preds: b4
                  break
                  // succs: b8
              } else {
                  block b6:  // preds: b4, succs: b7
              }
              block b7:  // preds: b6, succs: b1
          }
          block b8:   // preds: b2 b5
          32    %10 = phi b2: %1, b5: %3
                      // succs: b9
          block b9:
      }
   )"));
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
   add_loop_terminators(&term1, &term2, false, false);

   nir_pop_loop(b, loop);

   create_loop_phis(loop, term1, term2, in_def, ubo_def);

   ASSERT_FALSE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = deref_var &in (shader_in int)
          32     %1 = @load_deref (%0) (access=none)
          32     %2 = deref_var &ubo1 (ubo int)
          32     %3 = @load_deref (%2) (access=none)
                      // succs: b1
          loop {
              block b1:  // preds: b0 b9, succs: b2
              loop {
                  block b2:  // preds: b1 b2, succs: b2
              }
              block b3:   // preds:
              32     %4 = load_const (0x00000001)
              1      %5 = ieq %1, %4 (0x1)
                          // succs: b4 b5
              if %5 {
                  block b4:// preds: b3
                  break
                  // succs: b10
              } else {
                  block b5:  // preds: b3, succs: b6
              }
              block b6:   // preds: b5
              32     %6 = deref_var &ubo1 (ubo int)
              32     %7 = @load_deref (%6) (access=none)
              32     %8 = load_const (0x00000002)
              1      %9 = ieq %7, %8 (0x2)
                          // succs: b7 b8
              if %9 {
                  block b7:// preds: b6
                  break
                  // succs: b10
              } else {
                  block b8:  // preds: b6, succs: b9
              }
              block b9:  // preds: b8, succs: b1
          }
          block b10:  // preds: b4 b7
          32    %10 = phi b4: %1, b7: %3
                      // succs: b11
          block b11:
      }
   )"));
}

TEST_F(nir_opt_loop_test, opt_loop_peel_initial_break_ends_with_jump)
{
   nir_loop *loop = nir_push_loop(b);

   /* the break we want to move down: */
   nir_break_if(b, nir_imm_true(b));

   /* do_work_2: */
   nir_push_if(b, nir_imm_true(b));
   nir_jump(b, nir_jump_continue);
   nir_pop_if(b, NULL);
   nir_jump(b, nir_jump_return);

   nir_pop_loop(b, loop);

   ASSERT_FALSE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = deref_var &in (shader_in int)
          32    %1 = @load_deref (%0) (access=none)
                     // succs: b1
          loop {
              block b1:  // preds: b0 b5
              1     %2 = load_const (true)
                         // succs: b2 b3
              if %2 (true) {
                  block b2:// preds: b1
                  break
                  // succs: b8
              } else {
                  block b3:  // preds: b1, succs: b4
              }
              block b4:  // preds: b3
              1     %3 = load_const (true)
                         // succs: b5 b6
              if %3 (true) {
                  block b5:// preds: b4
                  continue
                  // succs: b1
              } else {
                  block b6:  // preds: b4, succs: b7
              }
              block b7:// preds: b6
              return
              // succs: b9
          }
          block b8:  // preds: b2, succs: b9
          block b9:
      }
   )"));
}

TEST_F(nir_opt_loop_test, opt_loop_peel_initial_break_nontrivial_break)
{
   nir_loop *loop = nir_push_loop(b);

   nir_push_if(b, nir_imm_true(b));

   nir_push_if(b, nir_imm_true(b));
   nir_push_if(b, nir_imm_true(b));
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, NULL);
   nir_pop_if(b, NULL);
   nir_nop(b);

   nir_jump(b, nir_jump_break);
   nir_pop_if(b, NULL);

   /* do_work_2: */
   nir_nop(b);

   nir_pop_loop(b, loop);

   ASSERT_FALSE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = deref_var &in (shader_in int)
          32    %1 = @load_deref (%0) (access=none)
                     // succs: b1
          loop {
              block b1:  // preds: b0 b10
              1     %2 = load_const (true)
                         // succs: b2 b9
              if %2 (true) {
                  block b2:  // preds: b1
                  1     %3 = load_const (true)
                             // succs: b3 b7
                  if %3 (true) {
                      block b3:  // preds: b2
                      1     %4 = load_const (true)
                                 // succs: b4 b5
                      if %4 (true) {
                          block b4:// preds: b3
                          break
                          // succs: b11
                      } else {
                          block b5:  // preds: b3, succs: b6
                      }
                      block b6:  // preds: b5, succs: b8
                  } else {
                      block b7:  // preds: b2, succs: b8
                  }
                  block b8:// preds: b6 b7
                  @nop
                  break
                  // succs: b11
              } else {
                  block b9:  // preds: b1, succs: b10
              }
              block b10:// preds: b9
              @nop
              // succs: b1
          }
          block b11:  // preds: b4 b8, succs: b12
          block b12:
      }
   )"));
}

TEST_F(nir_opt_loop_test, opt_loop_peel_initial_break_deref)
{
   nir_loop *loop = nir_push_loop(b);

   nir_deref_instr *var_deref = nir_build_deref_var(b, out_var);

   nir_push_if(b, nir_imm_true(b));
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, NULL);

   nir_store_deref(b, var_deref, nir_imm_int(b, 42), 0x1);

   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_opt_loop(b->shader));

   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_opt_loop_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)
      decl_var ubo INTERP_MODE_NONE none int[4] ubo_array (0, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = deref_var &in (shader_in int)
          32    %1 = @load_deref (%0) (access=none)
          1     %2 = load_const (true)
                     // succs: b1 b2
          if %2 (true) {
              block b1:  // preds: b0, succs: b8
          } else {
              block b2:  // preds: b0, succs: b3
              loop {
                  block b3:  // preds: b2 b6
                  32    %3 = load_const (0x0000002a = 42)
                  32    %4 = deref_var &out (shader_out int)
                             @store_deref (%4, %3 (0x2a)) (wrmask=x, access=none)
                  1     %5 = load_const (true)
                             // succs: b4 b5
                  if %5 (true) {
                      block b4:// preds: b3
                      break
                      // succs: b7
                  } else {
                      block b5:  // preds: b3, succs: b6
                  }
                  block b6:  // preds: b5, succs: b3
              }
              block b7:  // preds: b4, succs: b8
          }
          block b8:  // preds: b1 b7, succs: b9
          block b9:
      }
   )"));
}
