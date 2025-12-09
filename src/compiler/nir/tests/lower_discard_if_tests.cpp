/*
 * Copyright 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_test.h"

class nir_lower_discard_if_test : public nir_test {
protected:
   nir_lower_discard_if_test();

   nir_def *in_def;


};

nir_lower_discard_if_test::nir_lower_discard_if_test()
   : nir_test::nir_test("nir_lower_discard_if_test", MESA_SHADER_FRAGMENT)
{
   nir_variable *var = nir_variable_create(b->shader, nir_var_shader_in, glsl_int_type(), "in");
   in_def = nir_load_var(b, var);
}

TEST_F(nir_lower_discard_if_test, move_single_terminate_out_of_loop)
{
   nir_loop *loop = nir_push_loop(b);
   nir_def *cmp_result = nir_ieq(b, in_def, nir_imm_zero(b, 1, 32));
   nir_break_if(b, cmp_result);
   nir_terminate(b);
   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_lower_discard_if(b->shader, nir_move_terminate_out_of_loops));

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_lower_discard_if_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = deref_var &in (shader_in int)
          32    %1 = @load_deref (%0) (access=none)
          1     %2 = load_const (false)
                     // succs: b1
          loop {
              block b1:  // preds: b0 b7
              32    %3 = load_const (0x00000000)
              1     %4 = ieq %1, %3 (0x0)
                         // succs: b2 b3
              if %4 {
                  block b2:// preds: b1
                  break
                  // succs: b8
              } else {
                  block b3:  // preds: b1, succs: b4
              }
              block b4:  // preds: b3
              1     %5 = load_const (true)
                         // succs: b5 b6
              if %5 (true) {
                  block b5:// preds: b4
                  break
                  // succs: b8
              } else {
                  block b6:  // preds: b4, succs: b7
              }
              block b7:  // preds: b6, succs: b1
          }
          block b8:  // preds: b2 b5
          1     %6 = phi b2: %2 (false), b5: %5 (true)
                     @terminate_if (%6)
                     // succs: b9
          block b9:
      }
   )"));
}

TEST_F(nir_lower_discard_if_test, move_multiple_terminate_out_of_loop)
{
   nir_loop *loop = nir_push_loop(b);
   nir_def *cmp_result = nir_ieq(b, in_def, nir_imm_zero(b, 1, 32));
   nir_terminate_if(b, cmp_result);
   nir_def *cmp_result2 = nir_ieq(b, in_def, nir_imm_int(b, 1));
   nir_terminate_if(b, cmp_result2);
   nir_jump(b, nir_jump_break);
   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_lower_discard_if(b->shader, nir_move_terminate_out_of_loops));

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_lower_discard_if_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          1      %0 = undefined
          32     %1 = deref_var &in (shader_in int)
          32     %2 = @load_deref (%1) (access=none)
          1      %3 = load_const (false)
          1      %4 = load_const (false)
                      // succs: b1
          loop {
              block b1:   // preds: b0
              32     %5 = load_const (0x00000000)
              1      %6 = ieq %2, %5 (0x0)
                          // succs: b2 b3
              if %6 {
                  block b2:// preds: b1
                  break
                  // succs: b8
              } else {
                  block b3:  // preds: b1, succs: b4
              }
              block b4:   // preds: b3
              32     %7 = load_const (0x00000001)
              1      %8 = ieq %2, %7 (0x1)
                          // succs: b5 b6
              if %8 {
                  block b5:// preds: b4
                  break
                  // succs: b8
              } else {
                  block b6:  // preds: b4, succs: b7
              }
              block b7:// preds: b6
              break
              // succs: b8
          }
          block b8:   // preds: b2 b5 b7
          1     %9  = phi b2: %6, b5: %0, b7: %3 (false)
          1     %10 = phi b2: %4 (false), b5: %8, b7: %4 (false)
                      @terminate_if (%10)
                      @terminate_if (%9)
                      // succs: b9
          block b9:
      }
   )"));
}

TEST_F(nir_lower_discard_if_test, move_terminate_out_of_nested_loop)
{
   nir_loop *loop = nir_push_loop(b);
   {
    nir_def *cmp_result = nir_ieq(b, in_def, nir_imm_zero(b, 1, 32));
    nir_break_if(b, cmp_result);
    nir_loop *inner = nir_push_loop(b);
    {
        nir_def *cmp_result2 = nir_ieq(b, in_def, nir_imm_int(b, 1));
        nir_terminate_if(b, cmp_result2);
        nir_jump(b, nir_jump_break);
    }
    nir_pop_loop(b, inner);
   }
   nir_pop_loop(b, loop);

   ASSERT_TRUE(nir_lower_discard_if(b->shader, nir_move_terminate_out_of_loops));

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_FRAGMENT
      name: nir_lower_discard_if_test
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = deref_var &in (shader_in int)
          32     %1 = @load_deref (%0) (access=none)
          1      %2 = load_const (false)
                      // succs: b1
          loop {
              block b1:   // preds: b0 b12
              32     %3 = load_const (0x00000000)
              1      %4 = ieq %1, %3 (0x0)
                          // succs: b2 b3
              if %4 {
                  block b2:// preds: b1
                  break
                  // succs: b13
              } else {
                  block b3:  // preds: b1, succs: b4
              }
              block b4:   // preds: b3
              1      %5 = load_const (false)
                          // succs: b5
              loop {
                  block b5:   // preds: b4
                  32     %6 = load_const (0x00000001)
                  1      %7 = ieq %1, %6 (0x1)
                              // succs: b6 b7
                  if %7 {
                      block b6:// preds: b5
                      break
                      // succs: b9
                  } else {
                      block b7:  // preds: b5, succs: b8
                  }
                  block b8:// preds: b7
                  break
                  // succs: b9
              }
              block b9:   // preds: b6 b8
              1      %8 = phi b6: %7, b8: %5 (false)
                          // succs: b10 b11
              if %8 {
                  block b10:// preds: b9
                  break
                  // succs: b13
              } else {
                  block b11:  // preds: b9, succs: b12
              }
              block b12:  // preds: b11, succs: b1
          }
          block b13:  // preds: b2 b10
          1     %9 = phi b2: %2 (false), b10: %8
                     @terminate_if (%9)
                     // succs: b14
          block b14:
      }
   )"));
}
