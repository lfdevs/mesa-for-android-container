/*
 * Copyright © 2022 Intel Corporation
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

class nir_loop_analyze_test : public ::testing::Test {
protected:
   nir_loop_analyze_test();
   ~nir_loop_analyze_test();

   nir_builder b;
};

nir_loop_analyze_test::nir_loop_analyze_test()
{
   glsl_type_singleton_init_or_ref();

   static const nir_shader_compiler_options options = { };
   b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, &options,
                                      "loop analyze");
}

nir_loop_analyze_test::~nir_loop_analyze_test()
{
   ralloc_free(b.shader);
   glsl_type_singleton_decref();
}

TEST_F(nir_loop_analyze_test, infinite_loop_feq)
{
   /* Create IR:
    *
    *    float i = 0.0;
    *    while (true) {
    *       if (i == 0.9)
    *          break;
    *
    *       i = i + 0.2;
    *    }
    */
   nir_ssa_def *ssa_0 = nir_imm_int(&b, 0x00000000);
   nir_ssa_def *ssa_1 = nir_imm_int(&b, 0x3e4ccccd);
   nir_ssa_def *ssa_2 = nir_imm_int(&b, 0x3f666666);

   nir_phi_instr *const phi = nir_phi_instr_create(b.shader);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_ssa_dest_init(&phi->instr, &phi->dest,
                        ssa_0->num_components, ssa_0->bit_size,
                        NULL);

      nir_phi_instr_add_src(phi, ssa_0->parent_instr->block,
                            nir_src_for_ssa(ssa_0));

      nir_ssa_def *ssa_5 = &phi->dest.ssa;
      nir_ssa_def *ssa_3 = nir_feq(&b, ssa_5, ssa_1);

      nir_if *nif = nir_push_if(&b, ssa_3);
      {
         nir_jump_instr *jump = nir_jump_instr_create(b.shader, nir_jump_break);
         nir_builder_instr_insert(&b, &jump->instr);
      }
      nir_pop_if(&b, nif);

      nir_ssa_def *ssa_4 = nir_fadd(&b, ssa_5, ssa_2);

      nir_phi_instr_add_src(phi, ssa_4->parent_instr->block,
                            nir_src_for_ssa(ssa_4));
   }
   nir_pop_loop(&b, loop);

   b.cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(&b, &phi->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         vec1 32 ssa_0 = load_const (0x00000000 = 0.000000)
    *         vec1 32 ssa_1 = load_const (0x3e4ccccd = 0.900000)
    *         vec1 32 ssa_2 = load_const (0x3f666666 = 0.200000)
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_4
    *                 vec1 32 ssa_5 = phi block_0: ssa_0, block_4: ssa_4
    *                 vec1  1 ssa_3 = feq ssa_5, ssa_1
    *                 // succs: block_2 block_3
    *                 if ssa_3 {
    *                         block block_2:
    *                         // preds: block_1
    *                         break
    *                         // succs: block_5
    *                 } else {
    *                         block block_3:
    *                         // preds: block_1
    *                         // succs: block_4
    *                 }
    *                 block block_4:
    *                 // preds: block_3
    *                 vec1 32 ssa_4 = fadd ssa_5, ssa_2
    *                 // succs: block_1
    *         }
    *         block block_5:
    *         // preds: block_2
    *         // succs: block_6
    *         block block_6:
    * }
    */
   nir_validate_shader(b.shader, "input");

   nir_loop_analyze_impl(b.impl, nir_var_all, false);

   ASSERT_NE((void *)0, loop->info);
   EXPECT_FALSE(loop->info->guessed_trip_count);
   EXPECT_FALSE(loop->info->exact_trip_count_known);
   EXPECT_EQ((void *)0, loop->info->limiting_terminator);
}

TEST_F(nir_loop_analyze_test, zero_iterations_ine)
{
   /* Create IR:
    *
    *    uint i = 1;
    *    while (true) {
    *       if (i != 0)
    *          break;
    *
    *       i++;
    *    }
    *
    * This loop should have an iteration count of zero.  See also
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/19732#note_1648999
    */
   nir_ssa_def *ssa_0 = nir_imm_int(&b, 0);
   nir_ssa_def *ssa_1 = nir_imm_int(&b, 1);

   nir_phi_instr *const phi = nir_phi_instr_create(b.shader);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_ssa_dest_init(&phi->instr, &phi->dest,
                        ssa_1->num_components, ssa_1->bit_size,
                        NULL);

      nir_phi_instr_add_src(phi, ssa_1->parent_instr->block,
                            nir_src_for_ssa(ssa_1));

      nir_ssa_def *ssa_4 = &phi->dest.ssa;
      nir_ssa_def *ssa_2 = nir_ine(&b, ssa_4, ssa_0);

      nir_if *nif = nir_push_if(&b, ssa_2);
      {
         nir_jump_instr *jump = nir_jump_instr_create(b.shader, nir_jump_break);
         nir_builder_instr_insert(&b, &jump->instr);
      }
      nir_pop_if(&b, nif);

      nir_ssa_def *ssa_3 = nir_iadd(&b, ssa_4, ssa_1);

      nir_phi_instr_add_src(phi, ssa_3->parent_instr->block,
                            nir_src_for_ssa(ssa_3));
   }
   nir_pop_loop(&b, loop);

   b.cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(&b, &phi->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         vec1 32 ssa_0 = load_const (0x00000000 = 0.000000)
    *         vec1 32 ssa_1 = load_const (0x00000001 = 0.000000)
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_4
    *                 vec1 32 ssa_4 = phi block_0: ssa_1, block_4: ssa_3
    *                 vec1  1 ssa_2 = ine ssa_4, ssa_0
    *                 // succs: block_2 block_3
    *                 if ssa_2 {
    *                         block block_2:
    *                         // preds: block_1
    *                         break
    *                         // succs: block_5
    *                 } else {
    *                         block block_3:
    *                         // preds: block_1
    *                         // succs: block_4
    *                 }
    *                 block block_4:
    *                 // preds: block_3
    *                 vec1 32 ssa_3 = iadd ssa_4, ssa_1
    *                 // succs: block_1
    *         }
    *         block block_5:
    *         // preds: block_2
    *         // succs: block_6
    *         block block_6:
    * }
    */
   nir_validate_shader(b.shader, "input");

   nir_loop_analyze_impl(b.impl, nir_var_all, false);

   ASSERT_NE((void *)0, loop->info);
   EXPECT_EQ(0, loop->info->max_trip_count);
   EXPECT_TRUE(loop->info->exact_trip_count_known);
}

TEST_F(nir_loop_analyze_test, one_iteration_uge)
{
   /* Create IR:
    *
    *    uint i = 0;
    *    while (true) {
    *       if (i >= 1)
    *          break;
    *
    *       i++;
    *    }
    */
   nir_ssa_def *ssa_0 = nir_imm_int(&b, 0);
   nir_ssa_def *ssa_1 = nir_imm_int(&b, 1);

   nir_phi_instr *const phi = nir_phi_instr_create(b.shader);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_ssa_dest_init(&phi->instr, &phi->dest,
                        ssa_0->num_components, ssa_0->bit_size,
                        NULL);

      nir_phi_instr_add_src(phi, ssa_0->parent_instr->block,
                            nir_src_for_ssa(ssa_0));

      nir_ssa_def *ssa_4 = &phi->dest.ssa;
      nir_ssa_def *ssa_2 = nir_uge(&b, ssa_4, ssa_1);

      nir_if *nif = nir_push_if(&b, ssa_2);
      {
         nir_jump_instr *jump = nir_jump_instr_create(b.shader, nir_jump_break);
         nir_builder_instr_insert(&b, &jump->instr);
      }
      nir_pop_if(&b, nif);

      nir_ssa_def *ssa_3 = nir_iadd(&b, ssa_4, ssa_1);

      nir_phi_instr_add_src(phi, ssa_3->parent_instr->block,
                            nir_src_for_ssa(ssa_3));
   }
   nir_pop_loop(&b, loop);

   b.cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(&b, &phi->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         vec1 32 ssa_0 = load_const (0x00000000 = 0.000000)
    *         vec1 32 ssa_1 = load_const (0x00000001 = 0.000000)
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_4
    *                 vec1 32 ssa_4 = phi block_0: ssa_0, block_4: ssa_3
    *                 vec1  1 ssa_2 = uge ssa_4, ssa_1
    *                 // succs: block_2 block_3
    *                 if ssa_2 {
    *                         block block_2:
    *                         // preds: block_1
    *                         break
    *                         // succs: block_5
    *                 } else {
    *                         block block_3:
    *                         // preds: block_1
    *                         // succs: block_4
    *                 }
    *                 block block_4:
    *                 // preds: block_3
    *                 vec1 32 ssa_3 = iadd ssa_4, ssa_1
    *                 // succs: block_1
    *         }
    *         block block_5:
    *         // preds: block_2
    *         // succs: block_6
    *         block block_6:
    * }
    */
   nir_validate_shader(b.shader, "input");

   nir_loop_analyze_impl(b.impl, nir_var_all, false);

   ASSERT_NE((void *)0, loop->info);
   EXPECT_EQ(1, loop->info->max_trip_count);
   EXPECT_TRUE(loop->info->exact_trip_count_known);
}

TEST_F(nir_loop_analyze_test, one_iteration_ine)
{
   /* Create IR:
    *
    *    uint i = 0;
    *    while (true) {
    *       if (i != 0)
    *          break;
    *
    *       i++;
    *    }
    */
   nir_ssa_def *ssa_0 = nir_imm_int(&b, 0);
   nir_ssa_def *ssa_1 = nir_imm_int(&b, 1);

   nir_phi_instr *const phi = nir_phi_instr_create(b.shader);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_ssa_dest_init(&phi->instr, &phi->dest,
                        ssa_0->num_components, ssa_0->bit_size,
                        NULL);

      nir_phi_instr_add_src(phi, ssa_0->parent_instr->block,
                            nir_src_for_ssa(ssa_0));

      nir_ssa_def *ssa_4 = &phi->dest.ssa;
      nir_ssa_def *ssa_2 = nir_ine(&b, ssa_4, ssa_0);

      nir_if *nif = nir_push_if(&b, ssa_2);
      {
         nir_jump_instr *jump = nir_jump_instr_create(b.shader, nir_jump_break);
         nir_builder_instr_insert(&b, &jump->instr);
      }
      nir_pop_if(&b, nif);

      nir_ssa_def *ssa_3 = nir_iadd(&b, ssa_4, ssa_1);

      nir_phi_instr_add_src(phi, ssa_3->parent_instr->block,
                            nir_src_for_ssa(ssa_3));
   }
   nir_pop_loop(&b, loop);

   b.cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(&b, &phi->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         vec1 32 ssa_0 = load_const (0x00000000 = 0.000000)
    *         vec1 32 ssa_1 = load_const (0x00000001 = 0.000000)
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_4
    *                 vec1 32 ssa_4 = phi block_0: ssa_0, block_4: ssa_3
    *                 vec1  1 ssa_2 = ine ssa_4, ssa_0
    *                 // succs: block_2 block_3
    *                 if ssa_2 {
    *                         block block_2:
    *                         // preds: block_1
    *                         break
    *                         // succs: block_5
    *                 } else {
    *                         block block_3:
    *                         // preds: block_1
    *                         // succs: block_4
    *                 }
    *                 block block_4:
    *                 // preds: block_3
    *                 vec1 32 ssa_3 = iadd ssa_4, ssa_1
    *                 // succs: block_1
    *         }
    *         block block_5:
    *         // preds: block_2
    *         // succs: block_6
    *         block block_6:
    * }
    */
   nir_validate_shader(b.shader, "input");

   nir_loop_analyze_impl(b.impl, nir_var_all, false);

   ASSERT_NE((void *)0, loop->info);
   EXPECT_EQ(1, loop->info->max_trip_count);
   EXPECT_TRUE(loop->info->exact_trip_count_known);
}

TEST_F(nir_loop_analyze_test, one_iteration_ieq)
{
   /* Create IR:
    *
    *    uint i = 0;
    *    while (true) {
    *       if (i == 1)
    *          break;
    *
    *       i++;
    *    }
    */
   nir_ssa_def *ssa_0 = nir_imm_int(&b, 0);
   nir_ssa_def *ssa_1 = nir_imm_int(&b, 1);

   nir_phi_instr *const phi = nir_phi_instr_create(b.shader);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_ssa_dest_init(&phi->instr, &phi->dest,
                        ssa_0->num_components, ssa_0->bit_size,
                        NULL);

      nir_phi_instr_add_src(phi, ssa_0->parent_instr->block,
                            nir_src_for_ssa(ssa_0));

      nir_ssa_def *ssa_4 = &phi->dest.ssa;
      nir_ssa_def *ssa_2 = nir_ieq(&b, ssa_4, ssa_1);

      nir_if *nif = nir_push_if(&b, ssa_2);
      {
         nir_jump_instr *jump = nir_jump_instr_create(b.shader, nir_jump_break);
         nir_builder_instr_insert(&b, &jump->instr);
      }
      nir_pop_if(&b, nif);

      nir_ssa_def *ssa_3 = nir_iadd(&b, ssa_4, ssa_1);

      nir_phi_instr_add_src(phi, ssa_3->parent_instr->block,
                            nir_src_for_ssa(ssa_3));
   }
   nir_pop_loop(&b, loop);

   b.cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(&b, &phi->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         vec1 32 ssa_0 = load_const (0x00000000 = 0.000000)
    *         vec1 32 ssa_1 = load_const (0x00000001 = 0.000000)
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_4
    *                 vec1 32 ssa_4 = phi block_0: ssa_0, block_4: ssa_3
    *                 vec1  1 ssa_2 = ine ssa_4, ssa_0
    *                 // succs: block_2 block_3
    *                 if ssa_2 {
    *                         block block_2:
    *                         // preds: block_1
    *                         break
    *                         // succs: block_5
    *                 } else {
    *                         block block_3:
    *                         // preds: block_1
    *                         // succs: block_4
    *                 }
    *                 block block_4:
    *                 // preds: block_3
    *                 vec1 32 ssa_3 = iadd ssa_4, ssa_1
    *                 // succs: block_1
    *         }
    *         block block_5:
    *         // preds: block_2
    *         // succs: block_6
    *         block block_6:
    * }
    */
   nir_validate_shader(b.shader, "input");

   nir_loop_analyze_impl(b.impl, nir_var_all, false);

   ASSERT_NE((void *)0, loop->info);
   EXPECT_EQ(1, loop->info->max_trip_count);
   EXPECT_TRUE(loop->info->exact_trip_count_known);
}

TEST_F(nir_loop_analyze_test, one_iteration_easy_fneu)
{
   /* Create IR:
    *
    *    float i = 0.0;
    *    while (true) {
    *       if (i != 0.0)
    *          break;
    *
    *       i = i + 1.0;
    *    }
    */
   nir_ssa_def *ssa_0 = nir_imm_int(&b, 0x00000000);
   nir_ssa_def *ssa_1 = nir_imm_int(&b, 0x3f800000);

   nir_phi_instr *const phi = nir_phi_instr_create(b.shader);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_ssa_dest_init(&phi->instr, &phi->dest,
                        ssa_0->num_components, ssa_0->bit_size,
                        NULL);

      nir_phi_instr_add_src(phi, ssa_0->parent_instr->block,
                            nir_src_for_ssa(ssa_0));

      nir_ssa_def *ssa_4 = &phi->dest.ssa;
      nir_ssa_def *ssa_2 = nir_fneu(&b, ssa_4, ssa_0);

      nir_if *nif = nir_push_if(&b, ssa_2);
      {
         nir_jump_instr *jump = nir_jump_instr_create(b.shader, nir_jump_break);
         nir_builder_instr_insert(&b, &jump->instr);
      }
      nir_pop_if(&b, nif);

      nir_ssa_def *ssa_3 = nir_fadd(&b, ssa_4, ssa_1);

      nir_phi_instr_add_src(phi, ssa_3->parent_instr->block,
                            nir_src_for_ssa(ssa_3));
   }
   nir_pop_loop(&b, loop);

   b.cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(&b, &phi->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         vec1 32 ssa_0 = load_const (0x00000000 = 0.000000)
    *         vec1 32 ssa_1 = load_const (0x3f800000 = 1.000000)
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_4
    *                 vec1 32 ssa_4 = phi block_0: ssa_0, block_4: ssa_3
    *                 vec1  1 ssa_2 = fneu ssa_4, ssa_0
    *                 // succs: block_2 block_3
    *                 if ssa_2 {
    *                         block block_2:
    *                         // preds: block_1
    *                         break
    *                         // succs: block_5
    *                 } else {
    *                         block block_3:
    *                         // preds: block_1
    *                         // succs: block_4
    *                 }
    *                 block block_4:
    *                 // preds: block_3
    *                 vec1 32 ssa_3 = fadd ssa_4, ssa_1
    *                 // succs: block_1
    *         }
    *         block block_5:
    *         // preds: block_2
    *         // succs: block_6
    *         block block_6:
    * }
    */
   nir_validate_shader(b.shader, "input");

   nir_loop_analyze_impl(b.impl, nir_var_all, false);

   ASSERT_NE((void *)0, loop->info);
   EXPECT_EQ(1, loop->info->max_trip_count);
   EXPECT_TRUE(loop->info->exact_trip_count_known);
}

TEST_F(nir_loop_analyze_test, one_iteration_fneu)
{
   /* Create IR:
    *
    *    float i = uintBitsToFloat(0xe7000000);
    *    while (true) {
    *       if (i != uintBitsToFloat(0xe7000000))
    *          break;
    *
    *       i = i + uintBitsToFloat(0x5b000000);
    *    }
    *
    * Going towards smaller magnitude (i.e., adding a small positive value to
    * a large negative value) requires a smaller delta to make a difference
    * than going towards a larger magnitude. For this reason, ssa_0 + ssa_1 !=
    * ssa_0, but ssa_0 - ssa_1 == ssa_0. Math class is tough.
    */
   nir_ssa_def *ssa_0 = nir_imm_int(&b, 0xe7000000);
   nir_ssa_def *ssa_1 = nir_imm_int(&b, 0x5b000000);

   nir_phi_instr *const phi = nir_phi_instr_create(b.shader);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_ssa_dest_init(&phi->instr, &phi->dest,
                        ssa_0->num_components, ssa_0->bit_size,
                        NULL);

      nir_phi_instr_add_src(phi, ssa_0->parent_instr->block,
                            nir_src_for_ssa(ssa_0));

      nir_ssa_def *ssa_4 = &phi->dest.ssa;
      nir_ssa_def *ssa_2 = nir_fneu(&b, ssa_4, ssa_0);

      nir_if *nif = nir_push_if(&b, ssa_2);
      {
         nir_jump_instr *jump = nir_jump_instr_create(b.shader, nir_jump_break);
         nir_builder_instr_insert(&b, &jump->instr);
      }
      nir_pop_if(&b, nif);

      nir_ssa_def *ssa_3 = nir_fadd(&b, ssa_4, ssa_1);

      nir_phi_instr_add_src(phi, ssa_3->parent_instr->block,
                            nir_src_for_ssa(ssa_3));
   }
   nir_pop_loop(&b, loop);

   b.cursor = nir_before_block(nir_loop_first_block(loop));
   nir_builder_instr_insert(&b, &phi->instr);

   /* At this point, we should have:
    *
    * impl main {
    *         block block_0:
    *         // preds:
    *         vec1 32 ssa_0 = load_const (0xe7000000 = -604462909807314587353088.0)
    *         vec1 32 ssa_1 = load_const (0x5b000000 = 36028797018963968.0)
    *         // succs: block_1
    *         loop {
    *                 block block_1:
    *                 // preds: block_0 block_4
    *                 vec1 32 ssa_4 = phi block_0: ssa_0, block_4: ssa_3
    *                 vec1  1 ssa_2 = fneu ssa_4, ssa_0
    *                 // succs: block_2 block_3
    *                 if ssa_2 {
    *                         block block_2:
    *                         // preds: block_1
    *                         break
    *                         // succs: block_5
    *                 } else {
    *                         block block_3:
    *                         // preds: block_1
    *                         // succs: block_4
    *                 }
    *                 block block_4:
    *                 // preds: block_3
    *                 vec1 32 ssa_3 = fadd ssa_4, ssa_1
    *                 // succs: block_1
    *         }
    *         block block_5:
    *         // preds: block_2
    *         // succs: block_6
    *         block block_6:
    * }
    */
   nir_validate_shader(b.shader, "input");

   nir_loop_analyze_impl(b.impl, nir_var_all, false);

   ASSERT_NE((void *)0, loop->info);
   EXPECT_EQ(1, loop->info->max_trip_count);
   EXPECT_TRUE(loop->info->exact_trip_count_known);
}
