/* Copyright 2026 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/* Tests for Backward Inter-Shader Code Motion with system values. */

#include "nir_opt_varyings_test.h"

namespace {

typedef enum {
   intrinsic,
   deref,
} use_deref_option;

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
{
public:
   void test(mesa_shader_stage producer_stage,
             mesa_shader_stage consumer_stage, unsigned interp0,
             unsigned interp1, nir_intrinsic_op sysval_op, use_deref_option use_deref);
};

void
nir_opt_varyings_test_bicm_sysval::test(mesa_shader_stage producer_stage,
                                        mesa_shader_stage consumer_stage,
                                        unsigned interp0, unsigned interp1,
                                        nir_intrinsic_op sysval_op, use_deref_option use_deref)
{
   options.varying_expression_max_cost = NULL; /* don't propagate uniforms */
   create_shaders(producer_stage, consumer_stage);
   gl_varying_slot slot = producer_stage == MESA_SHADER_TESS_CTRL ?
                             VARYING_SLOT_PATCH0 : VARYING_SLOT_VAR0;

   /* Producer code. */
   nir_def *producer_input0 = load_input(b1, (gl_varying_slot)0, 0, nir_type_float32, -1, 0);
   nir_def *producer_input1 = load_input(b1, (gl_varying_slot)1, 0, nir_type_float32, -1, 0);
   nir_intrinsic_instr *store1 =
      store_output(b1, slot, 0, nir_type_int32, producer_input0, -1, interp0 != INTERP_FLAT);
   nir_intrinsic_instr *store2 =
      store_output(b1, slot, 1, nir_type_int32, producer_input1, -1, interp1 != INTERP_FLAT);

   /* Consumer code. */
   nir_def *sysval;
   if (use_deref == deref) {
      nir_variable *var = nir_variable_create(b2->shader, nir_var_system_value,
                                              glsl_int_type(), NULL);
      var->data.location = nir_system_value_from_intrinsic(sysval_op);
      sysval = nir_load_var(b2, var);
   } else {
      sysval = nir_load_system_value(b2, sysval_op, 0, 1, 32);
   }

   nir_def *cond = nir_ieq_imm(b2, sysval, 0);
   nir_def *load1 = load_input(b2, slot, 0, nir_type_float32, -1, interp0);
   nir_def *load2 = load_input(b2, slot, 1, nir_type_float32, -1, interp1);
   nir_def *value = nir_bcsel(b2, cond, load1, load2);
   store_output(b2, VARYING_SLOT_VAR0, 0, nir_type_int32, value, 0, false);

   if (interp0 == interp1 &&
       sysval_movable_between_shaders(b2, sysval_op)) {
      ASSERT_EQ(opt_varyings(), (nir_progress_producer | nir_progress_consumer));
      ASSERT_FALSE(shader_contains_def(b2, load1));
      ASSERT_FALSE(shader_contains_def(b2, load2));
      ASSERT_FALSE(shader_contains_sysval(b2, sysval_op));
      ASSERT_TRUE(shader_contains_sysval(b1, sysval_op));
      ASSERT_TRUE(shader_contains_instr(b1, &store1->instr));
      ASSERT_FALSE(shader_contains_instr(b1, &store2->instr));
      ASSERT_TRUE(shader_contains_alu_op(b1, nir_op_bcsel, 32));
      ASSERT_EQ(nir_def_instr(store1->src[0].ssa)->type, nir_instr_type_alu);
      ASSERT_EQ(nir_def_as_alu(store1->src[0].ssa)->op, nir_op_bcsel);
   } else {
      ASSERT_EQ(opt_varyings(), 0);
      ASSERT_TRUE(shader_contains_def(b2, load1));
      ASSERT_TRUE(shader_contains_def(b2, load2));
      ASSERT_TRUE(shader_contains_sysval(b2, sysval_op));
      ASSERT_FALSE(shader_contains_sysval(b1, sysval_op));
      ASSERT_FALSE(shader_contains_alu_op(b1, nir_op_bcsel, 32));
   }
}

#define TEST_SYSVAL3(producer_stage, consumer_stage, interp0, interp1, sysval, use_deref) \
TEST_F(nir_opt_varyings_test_bicm_sysval, \
       sysval##_##producer_stage##_##consumer_stage##_##interp0##_##interp1##_##use_deref) \
{ \
   test(MESA_SHADER_##producer_stage, MESA_SHADER_##consumer_stage, \
        INTERP_##interp0, INTERP_##interp1, \
        nir_intrinsic_load_##sysval, use_deref); \
}

#define TEST_SYSVAL2(producer_stage, consumer_stage, interp0, interp1, sysval) \
   TEST_SYSVAL3(producer_stage, consumer_stage, interp0, interp1, sysval, intrinsic) \
   TEST_SYSVAL3(producer_stage, consumer_stage, interp0, interp1, sysval, deref)

#define TEST_SYSVAL1(producer_stage, consumer_stage, interp0, interp1) \
   TEST_SYSVAL2(producer_stage, consumer_stage, interp0, interp1, view_index) \
   TEST_SYSVAL2(producer_stage, consumer_stage, interp0, interp1, sample_mask_in)

#define TEST_SYSVAL(producer_stage, consumer_stage) \
   TEST_SYSVAL1(producer_stage, consumer_stage, FLAT, FLAT) \
   TEST_SYSVAL1(producer_stage, consumer_stage, FLAT, PERSP_PIXEL) \
   TEST_SYSVAL1(producer_stage, consumer_stage, PERSP_PIXEL, PERSP_PIXEL) \
   TEST_SYSVAL1(producer_stage, consumer_stage, PERSP_PIXEL, PERSP_CENTROID)

TEST_SYSVAL1(VERTEX, TESS_CTRL, FLAT, FLAT)
TEST_SYSVAL1(TESS_CTRL, TESS_EVAL, FLAT, FLAT)
TEST_SYSVAL(VERTEX, FRAGMENT)

}
