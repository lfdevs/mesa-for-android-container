/*
 * Copyright © 2015 Intel Corporation
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

class nir_cf_test : public nir_test {
protected:
   nir_cf_test()
      : nir_test::nir_test("nir_cf_test")
   {
   }
};

class nir_loop_simplify_test : public nir_test {
protected:
   nir_loop_simplify_test()
      : nir_test::nir_test("nir_loop_simplify_test")
   {
   }
};

TEST_F(nir_cf_test, delete_break_in_loop)
{
   /* Create IR:
    *
    * while (...) { break; }
    */
   nir_loop *loop = nir_loop_create(b->impl);
   nir_cf_node_insert(nir_after_cf_list(&b->impl->body), &loop->cf_node);

   b->cursor = nir_after_cf_list(&loop->body);

   nir_jump_instr *jump = nir_jump_instr_create(b->shader, nir_jump_break);
   nir_builder_instr_insert(b, &jump->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0
    *                 break
    *                 // succs: block_2
    *         }
    *         block block_2:
    *         // preds: block_1
    *         // succs: block_3
    *         block block_3:
    * }
    */
   nir_block *block_0 = nir_start_block(b->impl);
   nir_block *block_1 = nir_loop_first_block(loop);
   nir_block *block_2 = nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node));
   nir_block *block_3 = b->impl->end_block;
   ASSERT_EQ(nir_cf_node_block, block_0->cf_node.type);
   ASSERT_EQ(nir_cf_node_block, block_1->cf_node.type);
   ASSERT_EQ(nir_cf_node_block, block_2->cf_node.type);
   ASSERT_EQ(nir_cf_node_block, block_3->cf_node.type);

   /* Verify the successors and predecessors. */
   EXPECT_EQ(block_1, block_0->successors[0]);
   EXPECT_EQ(NULL,    block_0->successors[1]);
   EXPECT_EQ(block_2, block_1->successors[0]);
   EXPECT_EQ(NULL,    block_1->successors[1]);
   EXPECT_EQ(block_3, block_2->successors[0]);
   EXPECT_EQ(NULL,    block_2->successors[1]);
   EXPECT_EQ(NULL,    block_3->successors[0]);
   EXPECT_EQ(NULL,    block_3->successors[1]);
   EXPECT_EQ(0,       nir_block_num_preds(block_0));
   EXPECT_EQ(1,       nir_block_num_preds(block_1));
   EXPECT_EQ(1,       nir_block_num_preds(block_2));
   EXPECT_EQ(1,       nir_block_num_preds(block_3));
   EXPECT_TRUE(nir_block_has_pred(block_1, block_0));
   EXPECT_TRUE(nir_block_has_pred(block_2, block_1));
   EXPECT_TRUE(nir_block_has_pred(block_3, block_2));

   /* Now remove the break. */
   nir_instr_remove(&jump->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_1
    *                 // succs: block_1
    *         }
    *         block block_2:
    *         // preds:
    *         // succs: block_3
    *         block block_3:
    * }
    *
    * Re-verify the predecessors and successors.
    */
   EXPECT_EQ(block_1, block_0->successors[0]);
   EXPECT_EQ(NULL,    block_0->successors[1]);
   EXPECT_EQ(block_1, block_1->successors[0]); /* back to itself */
   EXPECT_EQ(NULL,    block_1->successors[1]);
   EXPECT_EQ(block_3, block_2->successors[0]);
   EXPECT_EQ(NULL,    block_2->successors[1]);
   EXPECT_EQ(NULL,    block_3->successors[0]);
   EXPECT_EQ(NULL,    block_3->successors[1]);
   EXPECT_EQ(0,       nir_block_num_preds(block_0));
   EXPECT_EQ(2,       nir_block_num_preds(block_1));
   EXPECT_EQ(0,       nir_block_num_preds(block_2));
   EXPECT_EQ(1,       nir_block_num_preds(block_3));
   EXPECT_TRUE(nir_block_has_pred(block_1, block_0));
   EXPECT_TRUE(nir_block_has_pred(block_1, block_1));
   EXPECT_FALSE(nir_block_has_pred(block_2, block_1));
   EXPECT_TRUE(nir_block_has_pred(block_3, block_2));

   nir_metadata_require(b->impl, nir_metadata_dominance);
}

/* This tests a bug in nir_convert_loop_to_lcssa, two consecutive deref
 * instructions can cause the function's loop to exit prematurely if
 * rematerializing the second causes the first to also be rematerialized. */
TEST_F(nir_cf_test, lcssa_iter_safety_during_deref_remat)
{
   nir_variable *ubo_var_array = nir_variable_create(
      b->shader, nir_var_mem_ubo, glsl_array_type(glsl_int_type(), 4, 0), "ubo_array");
   nir_variable *out_var = nir_variable_create(
      b->shader, nir_var_shader_out, glsl_int_type(), "out");

   nir_loop *loop = nir_push_loop(b);

   nir_def *index = nir_imm_int(b, 2);
   nir_deref_instr *deref = nir_build_deref_var(b, ubo_var_array);
   deref = nir_build_deref_array(b, deref, index);
   nir_jump(b, nir_jump_break);

   nir_pop_loop(b, loop);

   nir_def *val = nir_load_deref(b, deref);
   nir_store_deref(b, nir_build_deref_var(b, out_var), val, 0x1);

   nir_convert_loop_to_lcssa(loop);

   nir_block *block_after_loop = nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node));
   nir_block *block_before_loop = nir_cf_node_as_block(nir_cf_node_prev(&loop->cf_node));

   EXPECT_FALSE(nir_def_is_unused(index));
   EXPECT_TRUE(nir_def_block(index) == block_before_loop);
   nir_foreach_use_including_if(src, index)
      EXPECT_TRUE(!nir_src_is_if(src) && nir_src_use_instr(src)->type == nir_instr_type_deref &&
                  nir_src_use_instr(src)->block == block_after_loop);

   nir_validate_shader(b->shader, NULL);
   nir_validate_ssa_dominance(b->shader, NULL);
}

TEST_F(nir_loop_simplify_test, move_toplevel)
{
   nir_loop *loop = nir_push_loop(b);
   nir_break_if(b, nir_unit_test_uniform_input(b, 1, 1));
   nir_unit_test_uniform_input(b, 1, 32, 1);
   nir_pop_loop(b, NULL);

   nir_validate_shader(b->shader, "before nir_simplify_loop");
   nir_simplify_loop(loop, nir_jump_break);
   nir_validate_shader(b->shader, "after nir_simplify_loop");

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_loop_simplify_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
         block b0:  // preds:
         32    %0 = @decl_reg (num_components=1, num_array_elems=0, bit_size=1, divergent=1)
                     // succs: b1
         loop {
            block b1:  // preds: b0 b7
            1     %1 = load_const (false)
                        @store_reg (%1 (false), %0) (base=0, wrmask=x, legacy_fsat=0)
            1     %2 = @unit_test_uniform_input (base=0)
                        // succs: b2 b3
            if %2 {
                  block b2:  // preds: b1
                  1     %3 = load_const (true)
                           @store_reg (%3 (true), %0) (base=0, wrmask=x, legacy_fsat=0)
                           // succs: b4
            } else {
                  block b3:  // preds: b1
                  32    %4 = @unit_test_uniform_input (base=1)
                           // succs: b4
            }
            block b4:  // preds: b2 b3
            1     %5 = @load_reg (%0) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b5 b6
            if %5 {
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

TEST_F(nir_loop_simplify_test, predicate_toplevel)
{
   nir_loop *loop = nir_push_loop(b);
   nir_push_if(b, nir_unit_test_uniform_input(b, 1, 1));
   nir_break_if(b, nir_unit_test_uniform_input(b, 1, 1, 1));
   nir_pop_if(b, NULL);
   nir_unit_test_uniform_input(b, 1, 32, 2);
   nir_pop_loop(b, NULL);

   nir_validate_shader(b->shader, "before nir_simplify_loop");
   nir_simplify_loop(loop, nir_jump_break);
   nir_validate_shader(b->shader, "after nir_simplify_loop");

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_loop_simplify_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
         block b0:  // preds:
         32    %0 = @decl_reg (num_components=1, num_array_elems=0, bit_size=1, divergent=1)
                     // succs: b1
         loop {
            block b1:  // preds: b0 b13
            1     %1 = load_const (false)
                        @store_reg (%1 (false), %0) (base=0, wrmask=x, legacy_fsat=0)
            1     %2 = @unit_test_uniform_input (base=0)
                        // succs: b2 b6
            if %2 {
                  block b2:  // preds: b1
                  1     %3 = @unit_test_uniform_input (base=1)
                           // succs: b3 b4
                  if %3 {
                     block b3:  // preds: b2
                     1     %4 = load_const (true)
                                 @store_reg (%4 (true), %0) (base=0, wrmask=x, legacy_fsat=0)
                                 // succs: b5
                  } else {
                     block b4:  // preds: b2, succs: b5
                  }
                  block b5:  // preds: b3 b4, succs: b7
            } else {
                  block b6:  // preds: b1, succs: b7
            }
            block b7:  // preds: b5 b6
            1     %5 = @load_reg (%0) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b8 b9
            if %5 {
                  block b8:  // preds: b7, succs: b10
            } else {
                  block b9:  // preds: b7
                  32    %6 = @unit_test_uniform_input (base=2)
                           // succs: b10
            }
            block b10: // preds: b8 b9
            1     %7 = @load_reg (%0) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b11 b12
            if %7 {
                  block b11:// preds: b10
                  break
                  // succs: b14
            } else {
                  block b12:  // preds: b10, succs: b13
            }
            block b13:  // preds: b12, succs: b1
         }
         block b14:  // preds: b11, succs: b15
         block b15:
      }
   )"));
}

TEST_F(nir_loop_simplify_test, move_inner)
{
   nir_loop *loop = nir_push_loop(b);
   nir_push_if(b, nir_unit_test_uniform_input(b, 1, 1));
   nir_break_if(b, nir_unit_test_uniform_input(b, 1, 1, 1));
   nir_def *val = nir_unit_test_uniform_input(b, 1, 32, 2);
   nir_pop_if(b, NULL);
   val = nir_if_phi(b, val, nir_undef(b, 1, 32));
   nir_use(b, val);
   nir_pop_loop(b, NULL);

   nir_validate_shader(b->shader, "before nir_simplify_loop");
   nir_simplify_loop(loop, nir_jump_break);
   nir_validate_shader(b->shader, "after nir_simplify_loop");

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_loop_simplify_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
         block b0:   // preds:
         32     %0 = @decl_reg (num_components=1, num_array_elems=0, bit_size=32, divergent=1)
         32     %1 = @decl_reg (num_components=1, num_array_elems=0, bit_size=1, divergent=1)
         32     %2 = undefined
                     // succs: b1
         loop {
            block b1:   // preds: b0 b13
            1      %3 = load_const (false)
                        @store_reg (%3 (false), %1) (base=0, wrmask=x, legacy_fsat=0)
            1      %4 = @unit_test_uniform_input (base=0)
                        // succs: b2 b6
            if %4 {
                  block b2:   // preds: b1
                  1      %5 = @unit_test_uniform_input (base=1)
                              // succs: b3 b4
                  if %5 {
                     block b3:   // preds: b2
                     1      %6 = load_const (true)
                                 @store_reg (%6 (true), %1) (base=0, wrmask=x, legacy_fsat=0)
                                 // succs: b5
                  } else {
                     block b4:   // preds: b2
                     32     %7 = @unit_test_uniform_input (base=2)
                                 @store_reg (%7, %0) (base=0, wrmask=x, legacy_fsat=0)
                                 // succs: b5
                  }
                  block b5:   // preds: b3 b4
                  32     %8 = @load_reg (%0) (base=0, legacy_fabs=0, legacy_fneg=0)
                              // succs: b7
            } else {
                  block b6:  // preds: b1, succs: b7
            }
            block b7:   // preds: b5 b6
            32     %9 = phi b5: %8, b6: %2
            1     %10 = @load_reg (%1) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b8 b9
            if %10 {
                  block b8:  // preds: b7, succs: b10
            } else {
                  block b9:// preds: b7
                  @use (%9)
                  // succs: b10
            }
            block b10:  // preds: b8 b9
            1     %11 = @load_reg (%1) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b11 b12
            if %11 {
                  block b11:// preds: b10
                  break
                  // succs: b14
            } else {
                  block b12:  // preds: b10, succs: b13
            }
            block b13:  // preds: b12, succs: b1
         }
         block b14:  // preds: b11, succs: b15
         block b15:
      }
   )"));
}

TEST_F(nir_loop_simplify_test, predicate_inner)
{
   nir_loop *loop = nir_push_loop(b);
   nir_push_if(b, nir_unit_test_uniform_input(b, 1, 1));
   nir_push_if(b, nir_unit_test_uniform_input(b, 1, 1, 1));
   nir_break_if(b, nir_unit_test_uniform_input(b, 1, 1, 2));
   nir_pop_if(b, NULL);
   nir_def *val = nir_unit_test_uniform_input(b, 1, 32, 3);
   nir_pop_if(b, NULL);
   val = nir_if_phi(b, val, nir_undef(b, 1, 32));
   nir_use(b, val);
   nir_pop_loop(b, NULL);

   nir_validate_shader(b->shader, "before nir_simplify_loop");
   nir_simplify_loop(loop, nir_jump_break);
   nir_validate_shader(b->shader, "after nir_simplify_loop");

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_loop_simplify_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
         block b0:   // preds:
         32     %0 = @decl_reg (num_components=1, num_array_elems=0, bit_size=32, divergent=1)
         32     %1 = @decl_reg (num_components=1, num_array_elems=0, bit_size=1, divergent=1)
         32     %2 = undefined
                     // succs: b1
         loop {
            block b1:   // preds: b0 b19
            1      %3 = load_const (false)
                        @store_reg (%3 (false), %1) (base=0, wrmask=x, legacy_fsat=0)
            1      %4 = @unit_test_uniform_input (base=0)
                        // succs: b2 b12
            if %4 {
                  block b2:   // preds: b1
                  1      %5 = @unit_test_uniform_input (base=1)
                              // succs: b3 b7
                  if %5 {
                     block b3:   // preds: b2
                     1      %6 = @unit_test_uniform_input (base=2)
                                 // succs: b4 b5
                     if %6 {
                        block b4:   // preds: b3
                        1      %7 = load_const (true)
                                    @store_reg (%7 (true), %1) (base=0, wrmask=x, legacy_fsat=0)
                                    // succs: b6
                     } else {
                        block b5:  // preds: b3, succs: b6
                     }
                     block b6:  // preds: b4 b5, succs: b8
                  } else {
                     block b7:  // preds: b2, succs: b8
                  }
                  block b8:   // preds: b6 b7
                  1      %8 = @load_reg (%1) (base=0, legacy_fabs=0, legacy_fneg=0)
                              // succs: b9 b10
                  if %8 {
                     block b9:  // preds: b8, succs: b11
                  } else {
                     block b10:  // preds: b8
                     32     %9 = @unit_test_uniform_input (base=3)
                                 @store_reg (%9, %0) (base=0, wrmask=x, legacy_fsat=0)
                                 // succs: b11
                  }
                  block b11:  // preds: b9 b10
                  32    %10 = @load_reg (%0) (base=0, legacy_fabs=0, legacy_fneg=0)
                              // succs: b13
            } else {
                  block b12:  // preds: b1, succs: b13
            }
            block b13:  // preds: b11 b12
            32    %11 = phi b11: %10, b12: %2
            1     %12 = @load_reg (%1) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b14 b15
            if %12 {
                  block b14:  // preds: b13, succs: b16
            } else {
                  block b15:// preds: b13
                  @use (%11)
                  // succs: b16
            }
            block b16:  // preds: b14 b15
            1     %13 = @load_reg (%1) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b17 b18
            if %13 {
                  block b17:// preds: b16
                  break
                  // succs: b20
            } else {
                  block b18:  // preds: b16, succs: b19
            }
            block b19:  // preds: b18, succs: b1
         }
         block b20:  // preds: b17, succs: b21
         block b21:
      }
   )"));
}

TEST_F(nir_loop_simplify_test, predicate_phis)
{
   /* The pass will try to predicate the instructions at the merge block, but
    * the only instructions there are phis.
    */
   nir_def *undef0 = nir_undef(b, 1, 32);
   nir_def *undef1 = nir_undef(b, 1, 32);
   nir_loop *loop = nir_push_loop(b);
   nir_push_if(b, nir_unit_test_uniform_input(b, 1, 1));
   nir_break_if(b, nir_unit_test_uniform_input(b, 1, 1, 1));
   nir_pop_if(b, NULL);
   nir_if_phi(b, undef1, undef0);
   nir_pop_loop(b, NULL);

   nir_validate_shader(b->shader, "before nir_simplify_loop");
   nir_simplify_loop(loop, nir_jump_break);
   nir_validate_shader(b->shader, "after nir_simplify_loop");

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_loop_simplify_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
         block b0:  // preds:
         32    %0 = @decl_reg (num_components=1, num_array_elems=0, bit_size=1, divergent=1)
         32    %1 = undefined
         32    %2 = undefined
                     // succs: b1
         loop {
            block b1:  // preds: b0 b10
            1     %3 = load_const (false)
                        @store_reg (%3 (false), %0) (base=0, wrmask=x, legacy_fsat=0)
            1     %4 = @unit_test_uniform_input (base=0)
                        // succs: b2 b6
            if %4 {
                  block b2:  // preds: b1
                  1     %5 = @unit_test_uniform_input (base=1)
                           // succs: b3 b4
                  if %5 {
                     block b3:  // preds: b2
                     1     %6 = load_const (true)
                                 @store_reg (%6 (true), %0) (base=0, wrmask=x, legacy_fsat=0)
                                 // succs: b5
                  } else {
                     block b4:  // preds: b2, succs: b5
                  }
                  block b5:  // preds: b3 b4, succs: b7
            } else {
                  block b6:  // preds: b1, succs: b7
            }
            block b7:  // preds: b5 b6
            32    %7 = phi b5: %1, b6: %2
            1     %8 = @load_reg (%0) (base=0, legacy_fabs=0, legacy_fneg=0)
                        // succs: b8 b9
            if %8 {
                  block b8:// preds: b7
                  break
                  // succs: b11
            } else {
                  block b9:  // preds: b7, succs: b10
            }
            block b10:  // preds: b9, succs: b1
         }
         block b11:  // preds: b8, succs: b12
         block b12:
      }
   )"));
}
