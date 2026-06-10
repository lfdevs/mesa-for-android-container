/* Copyright 2026 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/* Tests for Backward Inter-Shader Code Motion with system values. */

#include "nir_opt_varyings_test.h"

namespace {

static bool
shader_contains_sysval(nir_builder *b, nir_intrinsic_op op)
{
   gl_system_value sysval = nir_system_value_from_intrinsic(op);

   nir_foreach_block(block, b->impl) {
      nir_foreach_instr(instr, block) {
         if (nir_system_value_from_instr(instr) == sysval)
            return true;
      }
   }
   return false;
}

static bool
sysval_movable_between_shaders(nir_builder *b, nir_intrinsic_op op)
{
   /* TODO: nir_opt_varyings can't move code from TCS to VS because it doesn't
    * handle TCS input loads with invocation_id.
    */
   if (b->shader->info.stage == MESA_SHADER_TESS_CTRL)
      return false;

   switch (op) {
   case nir_intrinsic_load_view_index:
      return true;
   default:
      return false;
   }
}

class nir_opt_varyings_test_bicm_sysval : public nir_opt_varyings_test
{};

#define TEST_SYSVAL3(producer_stage, consumer_stage, sysval, use_deref) \
TEST_F(nir_opt_varyings_test_bicm_sysval, \
       sysval##_##producer_stage##_##consumer_stage##_##use_deref) \
{ \
   options.varying_expression_max_cost = NULL; /* don't propagate uniforms */ \
   create_shaders(MESA_SHADER_##producer_stage, MESA_SHADER_##consumer_stage); \
   gl_varying_slot slot = MESA_SHADER_##producer_stage == MESA_SHADER_TESS_CTRL ? \
                             VARYING_SLOT_PATCH0 : VARYING_SLOT_VAR0; \
   \
   /* Producer code. */ \
   nir_def *producer_input = load_uniform(b1, 32, 0); \
   nir_intrinsic_instr *store = \
      store_output(b1, slot, 0, nir_type_int32, producer_input, -1); \
   \
   /* Consumer code. */ \
   nir_def *sysval; \
   if (use_deref) { \
      nir_variable *var = nir_variable_create(b2->shader, nir_var_system_value, \
                                              glsl_int_type(), NULL); \
      var->data.location = nir_system_value_from_intrinsic(nir_intrinsic_load_##sysval); \
      sysval = nir_load_var(b2, var); \
   } else { \
      sysval = nir_load_##sysval(b2); \
   } \
   \
   nir_def *cond = nir_ieq_imm(b2, sysval, 0); \
   nir_def *load = load_input(b2, slot, 0, nir_type_int32, -1, INTERP_FLAT); \
   nir_def *value = nir_bcsel(b2, cond, load, nir_imm_int(b2, 0)); \
   store_output(b2, VARYING_SLOT_VAR0, 0, nir_type_int32, value, 0); \
   \
   if (sysval_movable_between_shaders(b2, nir_intrinsic_load_##sysval)) { \
      ASSERT_EQ(opt_varyings(), (nir_progress_producer | nir_progress_consumer)); \
      ASSERT_FALSE(shader_contains_def(b2, load)); \
      ASSERT_FALSE(shader_contains_sysval(b2, nir_intrinsic_load_##sysval)); \
      ASSERT_TRUE(shader_contains_sysval(b1, nir_intrinsic_load_##sysval)); \
      ASSERT_TRUE(shader_contains_instr(b1, &store->instr)); \
      ASSERT_TRUE(shader_contains_alu_op(b1, nir_op_bcsel, 32)); \
      ASSERT_EQ(nir_def_instr(store->src[0].ssa)->type, nir_instr_type_alu); \
      ASSERT_EQ(nir_def_as_alu(store->src[0].ssa)->op, nir_op_bcsel); \
   } else { \
      ASSERT_EQ(opt_varyings(), 0); \
      ASSERT_TRUE(shader_contains_def(b2, load)); \
      ASSERT_TRUE(shader_contains_sysval(b2, nir_intrinsic_load_##sysval)); \
      ASSERT_FALSE(shader_contains_sysval(b1, nir_intrinsic_load_##sysval)); \
      ASSERT_FALSE(shader_contains_alu_op(b1, nir_op_bcsel, 32)); \
   } \
}

#define TEST_SYSVAL2(producer_stage, consumer_stage, sysval) \
   TEST_SYSVAL3(producer_stage, consumer_stage, sysval, false) \
   TEST_SYSVAL3(producer_stage, consumer_stage, sysval, true)

#define TEST_SYSVAL(producer_stage, consumer_stage) \
   TEST_SYSVAL2(producer_stage, consumer_stage, view_index) \
   TEST_SYSVAL2(producer_stage, consumer_stage, sample_mask_in)

TEST_SYSVAL(TESS_CTRL, TESS_EVAL)
TEST_SYSVAL(VERTEX, FRAGMENT)
TEST_SYSVAL(VERTEX, TESS_CTRL)

}
