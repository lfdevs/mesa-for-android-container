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

namespace {

class nir_vars_test : public ::testing::Test {
protected:
   nir_vars_test();
   ~nir_vars_test();

   nir_variable *create_var(nir_variable_mode mode, const glsl_type *type,
                            const char *name) {
      if (mode == nir_var_function_temp)
         return nir_local_variable_create(b->impl, type, name);
      else
         return nir_variable_create(b->shader, mode, type, name);
   }

   nir_variable *create_int(nir_variable_mode mode, const char *name) {
      return create_var(mode, glsl_int_type(), name);
   }

   nir_variable *create_ivec2(nir_variable_mode mode, const char *name) {
      return create_var(mode, glsl_vector_type(GLSL_TYPE_INT, 2), name);
   }

   nir_variable *create_ivec4(nir_variable_mode mode, const char *name) {
      return create_var(mode, glsl_vector_type(GLSL_TYPE_INT, 4), name);
   }

   nir_variable **create_many_int(nir_variable_mode mode, const char *prefix, unsigned count) {
      nir_variable **result = (nir_variable **)linear_alloc_child(lin_ctx, sizeof(nir_variable *) * count);
      for (unsigned i = 0; i < count; i++)
         result[i] = create_int(mode, linear_asprintf(lin_ctx, "%s%u", prefix, i));
      return result;
   }

   nir_variable **create_many_ivec2(nir_variable_mode mode, const char *prefix, unsigned count) {
      nir_variable **result = (nir_variable **)linear_alloc_child(lin_ctx, sizeof(nir_variable *) * count);
      for (unsigned i = 0; i < count; i++)
         result[i] = create_ivec2(mode, linear_asprintf(lin_ctx, "%s%u", prefix, i));
      return result;
   }

   nir_variable **create_many_ivec4(nir_variable_mode mode, const char *prefix, unsigned count) {
      nir_variable **result = (nir_variable **)linear_alloc_child(lin_ctx, sizeof(nir_variable *) * count);
      for (unsigned i = 0; i < count; i++)
         result[i] = create_ivec4(mode, linear_asprintf(lin_ctx, "%s%u", prefix, i));
      return result;
   }

   unsigned count_intrinsics(nir_intrinsic_op intrinsic);

   nir_intrinsic_instr *get_intrinsic(nir_intrinsic_op intrinsic,
                                      unsigned index);

   void *mem_ctx;
   void *lin_ctx;

   nir_builder *b;
};

nir_vars_test::nir_vars_test()
{
   mem_ctx = ralloc_context(NULL);
   lin_ctx = linear_alloc_parent(mem_ctx, 0);
   static const nir_shader_compiler_options options = { };
   b = rzalloc(mem_ctx, nir_builder);
   nir_builder_init_simple_shader(b, mem_ctx, MESA_SHADER_FRAGMENT, &options);
}

nir_vars_test::~nir_vars_test()
{
   if (HasFailure()) {
      printf("\nShader from the failed test:\n\n");
      nir_print_shader(b->shader, stdout);
   }

   ralloc_free(mem_ctx);
}

unsigned
nir_vars_test::count_intrinsics(nir_intrinsic_op intrinsic)
{
   unsigned count = 0;
   nir_foreach_block(block, b->impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic == intrinsic)
            count++;
      }
   }
   return count;
}

nir_intrinsic_instr *
nir_vars_test::get_intrinsic(nir_intrinsic_op intrinsic,
                             unsigned index)
{
   nir_foreach_block(block, b->impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic == intrinsic) {
            if (index == 0)
               return intrin;
            index--;
         }
      }
   }
   return NULL;
}

/* Allow grouping the tests while still sharing the helpers. */
class nir_redundant_load_vars_test : public nir_vars_test {};
class nir_copy_prop_vars_test : public nir_vars_test {};
class nir_dead_write_vars_test : public nir_vars_test {};
class nir_combine_stores_test : public nir_vars_test {};

} // namespace

TEST_F(nir_redundant_load_vars_test, duplicated_load)
{
   /* Load a variable twice in the same block.  One should be removed. */

   nir_variable *in = create_int(nir_var_shader_in, "in");
   nir_variable **out = create_many_int(nir_var_shader_out, "out", 2);

   nir_store_var(b, out[0], nir_load_var(b, in), 1);
   nir_store_var(b, out[1], nir_load_var(b, in), 1);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 1);
}

TEST_F(nir_redundant_load_vars_test, duplicated_load_in_two_blocks)
{
   /* Load a variable twice in different blocks.  One should be removed. */

   nir_variable *in = create_int(nir_var_shader_in, "in");
   nir_variable **out = create_many_int(nir_var_shader_out, "out", 2);

   nir_store_var(b, out[0], nir_load_var(b, in), 1);

   /* Forces the stores to be in different blocks. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   nir_store_var(b, out[1], nir_load_var(b, in), 1);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 1);
}

TEST_F(nir_redundant_load_vars_test, invalidate_inside_if_block)
{
   /* Load variables, then write to some of then in different branches of the
    * if statement.  They should be invalidated accordingly.
    */

   nir_variable **g = create_many_int(nir_var_shader_temp, "g", 3);
   nir_variable **out = create_many_int(nir_var_shader_out, "out", 3);

   nir_load_var(b, g[0]);
   nir_load_var(b, g[1]);
   nir_load_var(b, g[2]);

   nir_if *if_stmt = nir_push_if(b, nir_imm_int(b, 0));
   nir_store_var(b, g[0], nir_imm_int(b, 10), 1);

   nir_push_else(b, if_stmt);
   nir_store_var(b, g[1], nir_imm_int(b, 20), 1);

   nir_pop_if(b, if_stmt);

   nir_store_var(b, out[0], nir_load_var(b, g[0]), 1);
   nir_store_var(b, out[1], nir_load_var(b, g[1]), 1);
   nir_store_var(b, out[2], nir_load_var(b, g[2]), 1);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   /* There are 3 initial loads, plus 2 loads for the values invalidated
    * inside the if statement.
    */
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 5);

   /* We only load g[2] once. */
   unsigned g2_load_count = 0;
   for (int i = 0; i < 5; i++) {
         nir_intrinsic_instr *load = get_intrinsic(nir_intrinsic_load_deref, i);
         if (nir_intrinsic_get_var(load, 0) == g[2])
            g2_load_count++;
   }
   EXPECT_EQ(g2_load_count, 1);
}

TEST_F(nir_redundant_load_vars_test, invalidate_live_load_in_the_end_of_loop)
{
   /* Invalidating a load in the end of loop body will apply to the whole loop
    * body.
    */

   nir_variable *v = create_int(nir_var_mem_ssbo, "v");

   nir_load_var(b, v);

   nir_loop *loop = nir_push_loop(b);

   nir_if *if_stmt = nir_push_if(b, nir_imm_int(b, 0));
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, if_stmt);

   nir_load_var(b, v);
   nir_store_var(b, v, nir_imm_int(b, 10), 1);

   nir_pop_loop(b, loop);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   ASSERT_FALSE(progress);
}

TEST_F(nir_copy_prop_vars_test, simple_copies)
{
   nir_variable *in   = create_int(nir_var_shader_in,     "in");
   nir_variable *temp = create_int(nir_var_function_temp, "temp");
   nir_variable *out  = create_int(nir_var_shader_out,    "out");

   nir_copy_var(b, temp, in);
   nir_copy_var(b, out, temp);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_copy_deref), 2);

   nir_intrinsic_instr *first_copy = get_intrinsic(nir_intrinsic_copy_deref, 0);
   ASSERT_TRUE(first_copy->src[1].is_ssa);

   nir_intrinsic_instr *second_copy = get_intrinsic(nir_intrinsic_copy_deref, 1);
   ASSERT_TRUE(second_copy->src[1].is_ssa);

   EXPECT_EQ(first_copy->src[1].ssa, second_copy->src[1].ssa);
}

TEST_F(nir_copy_prop_vars_test, simple_store_load)
{
   nir_variable **v = create_many_ivec2(nir_var_function_temp, "v", 2);
   unsigned mask = 1 | 2;

   nir_ssa_def *stored_value = nir_imm_ivec2(b, 10, 20);
   nir_store_var(b, v[0], stored_value, mask);

   nir_ssa_def *read_value = nir_load_var(b, v[0]);
   nir_store_var(b, v[1], read_value, mask);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   for (int i = 0; i < 2; i++) {
      nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, i);
      ASSERT_TRUE(store->src[1].is_ssa);
      EXPECT_EQ(store->src[1].ssa, stored_value);
   }
}

TEST_F(nir_copy_prop_vars_test, store_store_load)
{
   nir_variable **v = create_many_ivec2(nir_var_function_temp, "v", 2);
   unsigned mask = 1 | 2;

   nir_ssa_def *first_value = nir_imm_ivec2(b, 10, 20);
   nir_store_var(b, v[0], first_value, mask);

   nir_ssa_def *second_value = nir_imm_ivec2(b, 30, 40);
   nir_store_var(b, v[0], second_value, mask);

   nir_ssa_def *read_value = nir_load_var(b, v[0]);
   nir_store_var(b, v[1], read_value, mask);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 3);

   /* Store to v[1] should use second_value directly. */
   nir_intrinsic_instr *store_to_v1 = get_intrinsic(nir_intrinsic_store_deref, 2);
   ASSERT_EQ(nir_intrinsic_get_var(store_to_v1, 0), v[1]);
   ASSERT_TRUE(store_to_v1->src[1].is_ssa);
   EXPECT_EQ(store_to_v1->src[1].ssa, second_value);
}

TEST_F(nir_copy_prop_vars_test, store_store_load_different_components)
{
   nir_variable **v = create_many_ivec2(nir_var_function_temp, "v", 2);

   nir_ssa_def *first_value = nir_imm_ivec2(b, 10, 20);
   nir_store_var(b, v[0], first_value, 1 << 1);

   nir_ssa_def *second_value = nir_imm_ivec2(b, 30, 40);
   nir_store_var(b, v[0], second_value, 1 << 0);

   nir_ssa_def *read_value = nir_load_var(b, v[0]);
   nir_store_var(b, v[1], read_value, 1 << 1);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   nir_opt_constant_folding(b->shader);
   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 3);

   /* Store to v[1] should use first_value directly.  The write of
    * second_value did not overwrite the component it uses.
    */
   nir_intrinsic_instr *store_to_v1 = get_intrinsic(nir_intrinsic_store_deref, 2);
   ASSERT_EQ(nir_intrinsic_get_var(store_to_v1, 0), v[1]);
   ASSERT_EQ(nir_src_comp_as_uint(store_to_v1->src[1], 1), 20);
}

TEST_F(nir_copy_prop_vars_test, store_store_load_different_components_in_many_blocks)
{
   nir_variable **v = create_many_ivec2(nir_var_function_temp, "v", 2);

   nir_ssa_def *first_value = nir_imm_ivec2(b, 10, 20);
   nir_store_var(b, v[0], first_value, 1 << 1);

   /* Adding an if statement will cause blocks to be created. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   nir_ssa_def *second_value = nir_imm_ivec2(b, 30, 40);
   nir_store_var(b, v[0], second_value, 1 << 0);

   /* Adding an if statement will cause blocks to be created. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   nir_ssa_def *read_value = nir_load_var(b, v[0]);
   nir_store_var(b, v[1], read_value, 1 << 1);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   nir_opt_constant_folding(b->shader);
   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 3);

   /* Store to v[1] should use first_value directly.  The write of
    * second_value did not overwrite the component it uses.
    */
   nir_intrinsic_instr *store_to_v1 = get_intrinsic(nir_intrinsic_store_deref, 2);
   ASSERT_EQ(nir_intrinsic_get_var(store_to_v1, 0), v[1]);
   ASSERT_EQ(nir_src_comp_as_uint(store_to_v1->src[1], 1), 20);
}

TEST_F(nir_copy_prop_vars_test, memory_barrier_in_two_blocks)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 4);

   nir_store_var(b, v[0], nir_imm_int(b, 1), 1);
   nir_store_var(b, v[1], nir_imm_int(b, 2), 1);

   /* Split into many blocks. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   nir_store_var(b, v[2], nir_load_var(b, v[0]), 1);

   nir_builder_instr_insert(b, &nir_intrinsic_instr_create(b->shader, nir_intrinsic_memory_barrier)->instr);

   nir_store_var(b, v[3], nir_load_var(b, v[1]), 1);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   ASSERT_TRUE(progress);

   /* Only the second load will remain after the optimization. */
   ASSERT_EQ(1, count_intrinsics(nir_intrinsic_load_deref));
   nir_intrinsic_instr *load = get_intrinsic(nir_intrinsic_load_deref, 0);
   ASSERT_EQ(nir_intrinsic_get_var(load, 0), v[1]);
}

TEST_F(nir_copy_prop_vars_test, simple_store_load_in_two_blocks)
{
   nir_variable **v = create_many_ivec2(nir_var_function_temp, "v", 2);
   unsigned mask = 1 | 2;

   nir_ssa_def *stored_value = nir_imm_ivec2(b, 10, 20);
   nir_store_var(b, v[0], stored_value, mask);

   /* Adding an if statement will cause blocks to be created. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   nir_ssa_def *read_value = nir_load_var(b, v[0]);
   nir_store_var(b, v[1], read_value, mask);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   for (int i = 0; i < 2; i++) {
      nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, i);
      ASSERT_TRUE(store->src[1].is_ssa);
      EXPECT_EQ(store->src[1].ssa, stored_value);
   }
}

TEST_F(nir_copy_prop_vars_test, load_direct_array_deref_on_vector_reuses_previous_load)
{
   nir_variable *in0 = create_ivec2(nir_var_mem_ssbo, "in0");
   nir_variable *in1 = create_ivec2(nir_var_mem_ssbo, "in1");
   nir_variable *vec = create_ivec2(nir_var_mem_ssbo, "vec");
   nir_variable *out = create_int(nir_var_mem_ssbo, "out");

   nir_store_var(b, vec, nir_load_var(b, in0), 1 << 0);
   nir_store_var(b, vec, nir_load_var(b, in1), 1 << 1);

   /* This load will be dropped, as vec.y (or vec[1]) is already known. */
   nir_deref_instr *deref =
      nir_build_deref_array_imm(b, nir_build_deref_var(b, vec), 1);
   nir_ssa_def *loaded_from_deref = nir_load_deref(b, deref);

   /* This store should use the value loaded from in1. */
   nir_store_var(b, out, loaded_from_deref, 1 << 0);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 3);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 3);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 3);

   nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, 2);
   ASSERT_TRUE(store->src[1].is_ssa);

   /* NOTE: The ALU instruction is how we get the vec.y. */
   ASSERT_TRUE(nir_src_as_alu_instr(store->src[1]));
}

TEST_F(nir_copy_prop_vars_test, load_direct_array_deref_on_vector_reuses_previous_copy)
{
   nir_variable *in0 = create_ivec2(nir_var_mem_ssbo, "in0");
   nir_variable *vec = create_ivec2(nir_var_mem_ssbo, "vec");

   nir_copy_var(b, vec, in0);

   /* This load will be replaced with one from in0. */
   nir_deref_instr *deref =
      nir_build_deref_array_imm(b, nir_build_deref_var(b, vec), 1);
   nir_load_deref(b, deref);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 1);

   nir_intrinsic_instr *load = get_intrinsic(nir_intrinsic_load_deref, 0);
   ASSERT_EQ(nir_intrinsic_get_var(load, 0), in0);
}

TEST_F(nir_copy_prop_vars_test, load_direct_array_deref_on_vector_gets_reused)
{
   nir_variable *in0 = create_ivec2(nir_var_mem_ssbo, "in0");
   nir_variable *vec = create_ivec2(nir_var_mem_ssbo, "vec");
   nir_variable *out = create_ivec2(nir_var_mem_ssbo, "out");

   /* Loading "vec[1]" deref will save the information about vec.y. */
   nir_deref_instr *deref =
      nir_build_deref_array_imm(b, nir_build_deref_var(b, vec), 1);
   nir_load_deref(b, deref);

   /* Store to vec.x. */
   nir_store_var(b, vec, nir_load_var(b, in0), 1 << 0);

   /* This load will be dropped, since both vec.x and vec.y are known. */
   nir_ssa_def *loaded_from_vec = nir_load_var(b, vec);
   nir_store_var(b, out, loaded_from_vec, 0x3);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 3);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, 1);
   ASSERT_TRUE(store->src[1].is_ssa);
   ASSERT_TRUE(nir_src_as_alu_instr(store->src[1]));
}

TEST_F(nir_copy_prop_vars_test, store_load_direct_array_deref_on_vector)
{
   nir_variable *vec = create_ivec2(nir_var_mem_ssbo, "vec");
   nir_variable *out0 = create_int(nir_var_mem_ssbo, "out0");
   nir_variable *out1 = create_ivec2(nir_var_mem_ssbo, "out1");

   /* Store to "vec[1]" and "vec[0]". */
   nir_deref_instr *store_deref_y =
      nir_build_deref_array_imm(b, nir_build_deref_var(b, vec), 1);
   nir_store_deref(b, store_deref_y, nir_imm_int(b, 20), 1);

   nir_deref_instr *store_deref_x =
      nir_build_deref_array_imm(b, nir_build_deref_var(b, vec), 0);
   nir_store_deref(b, store_deref_x, nir_imm_int(b, 10), 1);

   /* Both loads below will be dropped, because the values are already known. */
   nir_deref_instr *load_deref_y =
      nir_build_deref_array_imm(b, nir_build_deref_var(b, vec), 1);
   nir_store_var(b, out0, nir_load_deref(b, load_deref_y), 1);

   nir_store_var(b, out1, nir_load_var(b, vec), 1);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 4);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 0);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 4);

   /* Third store will just use the value from first store. */
   nir_intrinsic_instr *first_store = get_intrinsic(nir_intrinsic_store_deref, 0);
   nir_intrinsic_instr *third_store = get_intrinsic(nir_intrinsic_store_deref, 2);
   ASSERT_TRUE(third_store->src[1].is_ssa);
   EXPECT_EQ(third_store->src[1].ssa, first_store->src[1].ssa);

   /* Fourth store will compose first and second store values. */
   nir_intrinsic_instr *fourth_store = get_intrinsic(nir_intrinsic_store_deref, 3);
   ASSERT_TRUE(fourth_store->src[1].is_ssa);
   EXPECT_TRUE(nir_src_as_alu_instr(fourth_store->src[1]));
}

TEST_F(nir_copy_prop_vars_test, store_load_indirect_array_deref_on_vector)
{
   nir_variable *vec = create_ivec2(nir_var_mem_ssbo, "vec");
   nir_variable *idx = create_int(nir_var_mem_ssbo, "idx");
   nir_variable *out = create_int(nir_var_mem_ssbo, "out");

   nir_ssa_def *idx_ssa = nir_load_var(b, idx);

   /* Store to vec[idx]. */
   nir_deref_instr *store_deref =
      nir_build_deref_array(b, nir_build_deref_var(b, vec), idx_ssa);
   nir_store_deref(b, store_deref, nir_imm_int(b, 20), 1);

   /* Load from vec[idx] to store in out. This load should be dropped. */
   nir_deref_instr *load_deref =
      nir_build_deref_array(b, nir_build_deref_var(b, vec), idx_ssa);
   nir_store_var(b, out, nir_load_deref(b, load_deref), 1);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 1);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   /* Store to vec[idx] propagated to out. */
   nir_intrinsic_instr *first = get_intrinsic(nir_intrinsic_store_deref, 0);
   nir_intrinsic_instr *second = get_intrinsic(nir_intrinsic_store_deref, 1);
   ASSERT_TRUE(first->src[1].is_ssa);
   ASSERT_TRUE(second->src[1].is_ssa);
   EXPECT_EQ(first->src[1].ssa, second->src[1].ssa);
}

TEST_F(nir_copy_prop_vars_test, store_load_direct_and_indirect_array_deref_on_vector)
{
   nir_variable *vec = create_ivec2(nir_var_mem_ssbo, "vec");
   nir_variable *idx = create_int(nir_var_mem_ssbo, "idx");
   nir_variable **out = create_many_int(nir_var_mem_ssbo, "out", 2);

   nir_ssa_def *idx_ssa = nir_load_var(b, idx);

   /* Store to vec. */
   nir_store_var(b, vec, nir_imm_ivec2(b, 10, 10), 1 | 2);

   /* Load from vec[idx]. This load is currently not dropped. */
   nir_deref_instr *indirect =
      nir_build_deref_array(b, nir_build_deref_var(b, vec), idx_ssa);
   nir_store_var(b, out[0], nir_load_deref(b, indirect), 1);

   /* Load from vec[idx] again. This load should be dropped. */
   nir_store_var(b, out[1], nir_load_deref(b, indirect), 1);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 3);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 3);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 3);

   /* Store to vec[idx] propagated to out. */
   nir_intrinsic_instr *second = get_intrinsic(nir_intrinsic_store_deref, 1);
   nir_intrinsic_instr *third = get_intrinsic(nir_intrinsic_store_deref, 2);
   ASSERT_TRUE(second->src[1].is_ssa);
   ASSERT_TRUE(third->src[1].is_ssa);
   EXPECT_EQ(second->src[1].ssa, third->src[1].ssa);
}

TEST_F(nir_copy_prop_vars_test, store_load_indirect_array_deref)
{
   nir_variable *arr = create_var(nir_var_mem_ssbo,
                                  glsl_array_type(glsl_int_type(), 10, 0),
                                  "arr");
   nir_variable *idx = create_int(nir_var_mem_ssbo, "idx");
   nir_variable *out = create_int(nir_var_mem_ssbo, "out");

   nir_ssa_def *idx_ssa = nir_load_var(b, idx);

   /* Store to arr[idx]. */
   nir_deref_instr *store_deref =
      nir_build_deref_array(b, nir_build_deref_var(b, arr), idx_ssa);
   nir_store_deref(b, store_deref, nir_imm_int(b, 20), 1);

   /* Load from arr[idx] to store in out. This load should be dropped. */
   nir_deref_instr *load_deref =
      nir_build_deref_array(b, nir_build_deref_var(b, arr), idx_ssa);
   nir_store_var(b, out, nir_load_deref(b, load_deref), 1);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 2);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   bool progress = nir_opt_copy_prop_vars(b->shader);
   EXPECT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_load_deref), 1);
   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 2);

   /* Store to arr[idx] propagated to out. */
   nir_intrinsic_instr *first = get_intrinsic(nir_intrinsic_store_deref, 0);
   nir_intrinsic_instr *second = get_intrinsic(nir_intrinsic_store_deref, 1);
   ASSERT_TRUE(first->src[1].is_ssa);
   ASSERT_TRUE(second->src[1].is_ssa);
   EXPECT_EQ(first->src[1].ssa, second->src[1].ssa);
}

TEST_F(nir_dead_write_vars_test, no_dead_writes_in_block)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 2);

   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_FALSE(progress);
}

TEST_F(nir_dead_write_vars_test, no_dead_writes_different_components_in_block)
{
   nir_variable **v = create_many_ivec2(nir_var_mem_ssbo, "v", 3);

   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1 << 0);
   nir_store_var(b, v[0], nir_load_var(b, v[2]), 1 << 1);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_FALSE(progress);
}

TEST_F(nir_dead_write_vars_test, no_dead_writes_in_if_statement)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 6);

   nir_store_var(b, v[2], nir_load_var(b, v[0]), 1);
   nir_store_var(b, v[3], nir_load_var(b, v[1]), 1);

   /* Each arm of the if statement will overwrite one store. */
   nir_if *if_stmt = nir_push_if(b, nir_imm_int(b, 0));
   nir_store_var(b, v[2], nir_load_var(b, v[4]), 1);

   nir_push_else(b, if_stmt);
   nir_store_var(b, v[3], nir_load_var(b, v[5]), 1);

   nir_pop_if(b, if_stmt);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_FALSE(progress);
}

TEST_F(nir_dead_write_vars_test, no_dead_writes_in_loop_statement)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 3);

   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1);

   /* Loop will write other value.  Since it might not be executed, it doesn't
    * kill the first write.
    */
   nir_loop *loop = nir_push_loop(b);

   nir_if *if_stmt = nir_push_if(b, nir_imm_int(b, 0));
   nir_jump(b, nir_jump_break);
   nir_pop_if(b, if_stmt);

   nir_store_var(b, v[0], nir_load_var(b, v[2]), 1);
   nir_pop_loop(b, loop);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_FALSE(progress);
}

TEST_F(nir_dead_write_vars_test, dead_write_in_block)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 3);

   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1);
   nir_ssa_def *load_v2 = nir_load_var(b, v[2]);
   nir_store_var(b, v[0], load_v2, 1);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_TRUE(progress);

   EXPECT_EQ(1, count_intrinsics(nir_intrinsic_store_deref));

   nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_TRUE(store->src[1].is_ssa);
   EXPECT_EQ(store->src[1].ssa, load_v2);
}

TEST_F(nir_dead_write_vars_test, dead_write_components_in_block)
{
   nir_variable **v = create_many_ivec2(nir_var_mem_ssbo, "v", 3);

   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1 << 0);
   nir_ssa_def *load_v2 = nir_load_var(b, v[2]);
   nir_store_var(b, v[0], load_v2, 1 << 0);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_TRUE(progress);

   EXPECT_EQ(1, count_intrinsics(nir_intrinsic_store_deref));

   nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_TRUE(store->src[1].is_ssa);
   EXPECT_EQ(store->src[1].ssa, load_v2);
}


/* TODO: The DISABLED tests below depend on the dead write removal be able to
 * identify dead writes between multiple blocks.  This is still not
 * implemented.
 */

TEST_F(nir_dead_write_vars_test, DISABLED_dead_write_in_two_blocks)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 3);

   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1);
   nir_ssa_def *load_v2 = nir_load_var(b, v[2]);

   /* Causes the stores to be in different blocks. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   nir_store_var(b, v[0], load_v2, 1);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_TRUE(progress);

   EXPECT_EQ(1, count_intrinsics(nir_intrinsic_store_deref));

   nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_TRUE(store->src[1].is_ssa);
   EXPECT_EQ(store->src[1].ssa, load_v2);
}

TEST_F(nir_dead_write_vars_test, DISABLED_dead_write_components_in_two_blocks)
{
   nir_variable **v = create_many_ivec2(nir_var_mem_ssbo, "v", 3);

   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1 << 0);

   /* Causes the stores to be in different blocks. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   nir_ssa_def *load_v2 = nir_load_var(b, v[2]);
   nir_store_var(b, v[0], load_v2, 1 << 0);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_TRUE(progress);

   EXPECT_EQ(1, count_intrinsics(nir_intrinsic_store_deref));

   nir_intrinsic_instr *store = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_TRUE(store->src[1].is_ssa);
   EXPECT_EQ(store->src[1].ssa, load_v2);
}

TEST_F(nir_dead_write_vars_test, DISABLED_dead_writes_in_if_statement)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 4);

   /* Both branches will overwrite, making the previous store dead. */
   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1);

   nir_if *if_stmt = nir_push_if(b, nir_imm_int(b, 0));
   nir_ssa_def *load_v2 = nir_load_var(b, v[2]);
   nir_store_var(b, v[0], load_v2, 1);

   nir_push_else(b, if_stmt);
   nir_ssa_def *load_v3 = nir_load_var(b, v[3]);
   nir_store_var(b, v[0], load_v3, 1);

   nir_pop_if(b, if_stmt);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_TRUE(progress);
   EXPECT_EQ(2, count_intrinsics(nir_intrinsic_store_deref));

   nir_intrinsic_instr *first_store = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_TRUE(first_store->src[1].is_ssa);
   EXPECT_EQ(first_store->src[1].ssa, load_v2);

   nir_intrinsic_instr *second_store = get_intrinsic(nir_intrinsic_store_deref, 1);
   ASSERT_TRUE(second_store->src[1].is_ssa);
   EXPECT_EQ(second_store->src[1].ssa, load_v3);
}

TEST_F(nir_dead_write_vars_test, DISABLED_memory_barrier_in_two_blocks)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 2);

   nir_store_var(b, v[0], nir_imm_int(b, 1), 1);
   nir_store_var(b, v[1], nir_imm_int(b, 2), 1);

   /* Split into many blocks. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   /* Because it is before the barrier, this will kill the previous store to that target. */
   nir_store_var(b, v[0], nir_imm_int(b, 3), 1);

   nir_builder_instr_insert(b, &nir_intrinsic_instr_create(b->shader, nir_intrinsic_memory_barrier)->instr);

   nir_store_var(b, v[1], nir_imm_int(b, 4), 1);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_TRUE(progress);

   EXPECT_EQ(3, count_intrinsics(nir_intrinsic_store_deref));
}

TEST_F(nir_dead_write_vars_test, DISABLED_unrelated_barrier_in_two_blocks)
{
   nir_variable **v = create_many_int(nir_var_mem_ssbo, "v", 3);
   nir_variable *out = create_int(nir_var_shader_out, "out");

   nir_store_var(b, out, nir_load_var(b, v[1]), 1);
   nir_store_var(b, v[0], nir_load_var(b, v[1]), 1);

   /* Split into many blocks. */
   nir_pop_if(b, nir_push_if(b, nir_imm_int(b, 0)));

   /* Emit vertex will ensure writes to output variables are considered used,
    * but should not affect other types of variables. */

   nir_builder_instr_insert(b, &nir_intrinsic_instr_create(b->shader, nir_intrinsic_emit_vertex)->instr);

   nir_store_var(b, out, nir_load_var(b, v[2]), 1);
   nir_store_var(b, v[0], nir_load_var(b, v[2]), 1);

   bool progress = nir_opt_dead_write_vars(b->shader);
   ASSERT_TRUE(progress);

   /* Verify the first write to v[0] was removed. */
   EXPECT_EQ(3, count_intrinsics(nir_intrinsic_store_deref));

   nir_intrinsic_instr *first_store = get_intrinsic(nir_intrinsic_store_deref, 0);
   EXPECT_EQ(nir_intrinsic_get_var(first_store, 0), out);

   nir_intrinsic_instr *second_store = get_intrinsic(nir_intrinsic_store_deref, 1);
   EXPECT_EQ(nir_intrinsic_get_var(second_store, 0), out);

   nir_intrinsic_instr *third_store = get_intrinsic(nir_intrinsic_store_deref, 2);
   EXPECT_EQ(nir_intrinsic_get_var(third_store, 0), v[0]);
}

TEST_F(nir_combine_stores_test, non_overlapping_stores)
{
   nir_variable **v = create_many_ivec4(nir_var_mem_ssbo, "v", 4);
   nir_variable *out = create_ivec4(nir_var_shader_out, "out");

   for (int i = 0; i < 4; i++)
      nir_store_var(b, out, nir_load_var(b, v[i]), 1 << i);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_combine_stores(b->shader, nir_var_shader_out);
   ASSERT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   /* Clean up to verify from where the values in combined store are coming. */
   nir_copy_prop(b->shader);
   nir_opt_dce(b->shader);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 1);
   nir_intrinsic_instr *combined = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_EQ(nir_intrinsic_write_mask(combined), 0xf);
   ASSERT_EQ(nir_intrinsic_get_var(combined, 0), out);

   nir_alu_instr *vec = nir_src_as_alu_instr(combined->src[1]);
   ASSERT_TRUE(vec);
   for (int i = 0; i < 4; i++) {
      nir_intrinsic_instr *load = nir_src_as_intrinsic(vec->src[i].src);
      ASSERT_EQ(load->intrinsic, nir_intrinsic_load_deref);
      ASSERT_EQ(nir_intrinsic_get_var(load, 0), v[i])
         << "Source value for component " << i << " of store is wrong";
      ASSERT_EQ(vec->src[i].swizzle[0], i)
         << "Source component for component " << i << " of store is wrong";
   }
}

TEST_F(nir_combine_stores_test, overlapping_stores)
{
   nir_variable **v = create_many_ivec4(nir_var_mem_ssbo, "v", 3);
   nir_variable *out = create_ivec4(nir_var_shader_out, "out");

   /* Make stores with xy, yz and zw masks. */
   for (int i = 0; i < 3; i++) {
      nir_component_mask_t mask = (1 << i) | (1 << (i + 1));
      nir_store_var(b, out, nir_load_var(b, v[i]), mask);
   }

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_combine_stores(b->shader, nir_var_shader_out);
   ASSERT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   /* Clean up to verify from where the values in combined store are coming. */
   nir_copy_prop(b->shader);
   nir_opt_dce(b->shader);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 1);
   nir_intrinsic_instr *combined = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_EQ(nir_intrinsic_write_mask(combined), 0xf);
   ASSERT_EQ(nir_intrinsic_get_var(combined, 0), out);

   nir_alu_instr *vec = nir_src_as_alu_instr(combined->src[1]);
   ASSERT_TRUE(vec);

   /* Component x comes from v[0]. */
   nir_intrinsic_instr *load_for_x = nir_src_as_intrinsic(vec->src[0].src);
   ASSERT_EQ(nir_intrinsic_get_var(load_for_x, 0), v[0]);
   ASSERT_EQ(vec->src[0].swizzle[0], 0);

   /* Component y comes from v[1]. */
   nir_intrinsic_instr *load_for_y = nir_src_as_intrinsic(vec->src[1].src);
   ASSERT_EQ(nir_intrinsic_get_var(load_for_y, 0), v[1]);
   ASSERT_EQ(vec->src[1].swizzle[0], 1);

   /* Components z and w come from v[2]. */
   nir_intrinsic_instr *load_for_z = nir_src_as_intrinsic(vec->src[2].src);
   nir_intrinsic_instr *load_for_w = nir_src_as_intrinsic(vec->src[3].src);
   ASSERT_EQ(load_for_z, load_for_w);
   ASSERT_EQ(nir_intrinsic_get_var(load_for_z, 0), v[2]);
   ASSERT_EQ(vec->src[2].swizzle[0], 2);
   ASSERT_EQ(vec->src[3].swizzle[0], 3);
}

TEST_F(nir_combine_stores_test, direct_array_derefs)
{
   nir_variable **v = create_many_ivec4(nir_var_mem_ssbo, "vec", 2);
   nir_variable **s = create_many_int(nir_var_mem_ssbo, "scalar", 2);
   nir_variable *out = create_ivec4(nir_var_mem_ssbo, "out");

   nir_deref_instr *out_deref = nir_build_deref_var(b, out);

   /* Store to vector with mask x. */
   nir_store_deref(b, out_deref, nir_load_var(b, v[0]),
                   1 << 0);

   /* Store to vector with mask yz. */
   nir_store_deref(b, out_deref, nir_load_var(b, v[1]),
                   (1 << 2) | (1 << 1));

   /* Store to vector[2], overlapping with previous store. */
   nir_store_deref(b,
                   nir_build_deref_array_imm(b, out_deref, 2),
                   nir_load_var(b, s[0]),
                   1 << 0);

   /* Store to vector[3], no overlap. */
   nir_store_deref(b,
                   nir_build_deref_array_imm(b, out_deref, 3),
                   nir_load_var(b, s[1]),
                   1 << 0);

   nir_validate_shader(b->shader, NULL);

   bool progress = nir_opt_combine_stores(b->shader, nir_var_mem_ssbo);
   ASSERT_TRUE(progress);

   nir_validate_shader(b->shader, NULL);

   /* Clean up to verify from where the values in combined store are coming. */
   nir_copy_prop(b->shader);
   nir_opt_dce(b->shader);

   ASSERT_EQ(count_intrinsics(nir_intrinsic_store_deref), 1);
   nir_intrinsic_instr *combined = get_intrinsic(nir_intrinsic_store_deref, 0);
   ASSERT_EQ(nir_intrinsic_write_mask(combined), 0xf);
   ASSERT_EQ(nir_intrinsic_get_var(combined, 0), out);

   nir_alu_instr *vec = nir_src_as_alu_instr(combined->src[1]);
   ASSERT_TRUE(vec);

   /* Component x comes from v[0]. */
   nir_intrinsic_instr *load_for_x = nir_src_as_intrinsic(vec->src[0].src);
   ASSERT_EQ(nir_intrinsic_get_var(load_for_x, 0), v[0]);
   ASSERT_EQ(vec->src[0].swizzle[0], 0);

   /* Component y comes from v[1]. */
   nir_intrinsic_instr *load_for_y = nir_src_as_intrinsic(vec->src[1].src);
   ASSERT_EQ(nir_intrinsic_get_var(load_for_y, 0), v[1]);
   ASSERT_EQ(vec->src[1].swizzle[0], 1);

   /* Components z comes from s[0]. */
   nir_intrinsic_instr *load_for_z = nir_src_as_intrinsic(vec->src[2].src);
   ASSERT_EQ(nir_intrinsic_get_var(load_for_z, 0), s[0]);
   ASSERT_EQ(vec->src[2].swizzle[0], 0);

   /* Component w comes from s[1]. */
   nir_intrinsic_instr *load_for_w = nir_src_as_intrinsic(vec->src[3].src);
   ASSERT_EQ(nir_intrinsic_get_var(load_for_w, 0), s[1]);
   ASSERT_EQ(vec->src[3].swizzle[0], 0);
}
