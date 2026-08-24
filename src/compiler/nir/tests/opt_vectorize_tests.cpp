/*
 * Copyright © 2026 Pavel Ondračka
 * SPDX-License-Identifier: MIT
 */

#include "nir_test.h"

namespace {

class nir_opt_vectorize_test : public nir_test {
protected:
   nir_opt_vectorize_test()
      : nir_test::nir_test("nir_opt_vectorize_test")
   {
   }

   nir_alu_instr *create_scalar_alu(nir_op op, nir_def *src0,
                                    unsigned src0_swizzle, nir_def *src1,
                                    unsigned src1_swizzle,
                                    nir_def *src2 = nullptr,
                                    unsigned src2_swizzle = 0)
   {
      nir_def *def;
      if (src2)
         def = nir_build_alu3(b, op, src0, src1, src2);
      else
         def = nir_build_alu2(b, op, src0, src1);

      nir_alu_instr *alu = nir_def_as_alu(def);

      def->num_components = 1;
      alu->src[0].swizzle[0] = src0_swizzle;
      alu->src[1].swizzle[0] = src1_swizzle;
      if (src2)
         alu->src[2].swizzle[0] = src2_swizzle;

      return alu;
   }
};

TEST_F(nir_opt_vectorize_test, commutative_swapped_sources)
{
   nir_def *src0 = nir_undef(b, 4, 32);
   nir_def *src1 = nir_undef(b, 4, 32);

   nir_alu_instr *mul0 = create_scalar_alu(nir_op_fmul, src0, 0, src1, 1);
   nir_alu_instr *mul1 = create_scalar_alu(nir_op_fmul, src1, 2, src0, 3);
   nir_vec2(b, &mul0->def, &mul1->def);

   ASSERT_TRUE(nir_opt_vectorize(b->shader, NULL, NULL));
   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_opt_vectorize_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32x4  %0 = undefined
          32x4  %1 = undefined
          32x2  %2 = fmul %1.xw, %0.yz
          32x2  %3 = vec2 %2.x, %2.y
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_opt_vectorize_test, commutative_swapped_ffma_sources)
{
   nir_def *src0 = nir_undef(b, 4, 32);
   nir_def *src1 = nir_undef(b, 4, 32);
   nir_def *src2 = nir_undef(b, 4, 32);

   nir_alu_instr *ffma0 =
      create_scalar_alu(nir_op_ffma, src0, 0, src1, 1, src2, 2);
   nir_alu_instr *ffma1 =
      create_scalar_alu(nir_op_ffma, src1, 3, src0, 2, src2, 0);
   nir_vec2(b, &ffma0->def, &ffma1->def);

   ASSERT_TRUE(nir_opt_vectorize(b->shader, NULL, NULL));
   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_opt_vectorize_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32x4  %0 = undefined
          32x4  %1 = undefined
          32x4  %2 = undefined
          32x2  %3 = ffma %2.xz, %1.yw, %0.zx
          32x2  %4 = vec2 %3.x, %3.y
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_opt_vectorize_test, non_commutative_swapped_sources)
{
   nir_def *src0 = nir_undef(b, 4, 32);
   nir_def *src1 = nir_undef(b, 4, 32);

   create_scalar_alu(nir_op_fsub, src0, 0, src1, 1);
   create_scalar_alu(nir_op_fsub, src1, 2, src0, 3);

   ASSERT_FALSE(nir_opt_vectorize(b->shader, NULL, NULL));
   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_opt_vectorize_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32x4  %0 = undefined
          32x4  %1 = undefined
          32    %2 = fsub %1.x, %0.y
          32    %3 = fsub %0.z, %1.w
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_opt_vectorize_test, iadd3_swapped_last_two_sources_do_not_vectorize)
{
   nir_def *src0 = nir_undef(b, 4, 32);
   nir_def *src1 = nir_undef(b, 4, 32);
   nir_def *src2 = nir_undef(b, 4, 32);

   /* NIR only describes commutativity between the first two sources. */
   nir_alu_instr *add0 =
      create_scalar_alu(nir_op_iadd3, src0, 0, src1, 1, src2, 2);
   nir_alu_instr *add1 =
      create_scalar_alu(nir_op_iadd3, src0, 3, src2, 0, src1, 2);
   nir_vec2(b, &add0->def, &add1->def);

   ASSERT_FALSE(nir_opt_vectorize(b->shader, NULL, NULL));
   nir_validate_shader(b->shader, NULL);

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_opt_vectorize_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32x4  %0 = undefined
          32x4  %1 = undefined
          32x4  %2 = undefined
          32    %3 = iadd3 %2.x, %1.y, %0.z
          32    %4 = iadd3 %2.w, %0.x, %1.z
          32x2  %5 = vec2 %3, %4
                     // succs: b1
          block b1:
      }
   )"));
}

TEST_F(nir_opt_vectorize_test, combining_loop_phis_converges)
{
   nir_variable *out = nir_variable_create(b->shader, nir_var_shader_out,
                                           glsl_uint_type(), "out");
   nir_variable *sum = nir_local_variable_create(b->impl, glsl_uint_type(), "sum");
   nir_variable *idx = nir_local_variable_create(b->impl, glsl_uint_type(), "idx");

   nir_store_var(b, sum, nir_imm_int(b, 0), 0x1);
   nir_store_var(b, idx, nir_imm_int(b, 0), 0x1);

   /* Two scalar values carried around a loop become two scalar phis in the
    * loop header, which the pass combines into one 2-component phi.
    */
   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *cur_idx = nir_load_var(b, idx);
      nir_def *cur_sum = nir_load_var(b, sum);

      nir_push_if(b, nir_uge_imm(b, cur_idx, 4));
      nir_jump(b, nir_jump_break);
      nir_pop_if(b, NULL);

      nir_store_var(b, sum, nir_iadd(b, cur_sum, cur_idx), 0x1);
      nir_store_var(b, idx, nir_iadd_imm(b, cur_idx, 1), 0x1);
   }
   nir_pop_loop(b, loop);

   nir_store_var(b, out, nir_load_var(b, sum), 0x1);

   NIR_PASS(_, b->shader, nir_lower_vars_to_ssa);

   ASSERT_TRUE(nir_opt_vectorize(b->shader, NULL, NULL));
   nir_validate_shader(b->shader, NULL);

   ASSERT_FALSE(nir_opt_vectorize(b->shader, NULL, NULL));
   nir_validate_shader(b->shader, NULL);
}

} // namespace
