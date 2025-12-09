/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir_test.h"

class nir_minimize_call_live_states_test : public nir_test {
protected:
   nir_minimize_call_live_states_test();

   void run_pass();

   nir_function *indirect_decl;
};

nir_minimize_call_live_states_test::nir_minimize_call_live_states_test()
   : nir_test::nir_test("nir_minimize_call_live_states_test")
{
   indirect_decl = nir_function_create(b->shader, "test_function");
}

void
nir_minimize_call_live_states_test::run_pass()
{
   nir_validate_shader(b->shader, "before nir_minimize_call_live_states");
   nir_minimize_call_live_states(b->shader);
   nir_validate_shader(b->shader, "after nir_minimize_call_live_states");
}

TEST_F(nir_minimize_call_live_states_test, no_live_states)
{
   nir_def *callee = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0));
   nir_build_indirect_call(b, indirect_decl, callee, 0, NULL);

   run_pass();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_minimize_call_live_states_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:  // preds:
          32    %0 = load_const (0x00000000 = 0.000000)
          32    %1 = load_const (0x00000000)
          32    %2 = @load_push_constant (%1 (0x0)) (base=0, range=0, align_mul=4, align_offset=0)
                     call test_function (indirect %2)
                     // succs: b1
          block b1:
      }

      decl_function test_function ()
   )"));
}

TEST_F(nir_minimize_call_live_states_test, life_intrinsics)
{
   nir_def *callee = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0));

   nir_def *v1 = nir_load_push_constant(b, 1, 64, nir_imm_int(b, 8));
   nir_def *v2 = nir_load_global(b, 3, 32, v1);
   nir_def *v3 = nir_load_global_constant(b, 1, 32, v1);

   nir_build_indirect_call(b, indirect_decl, callee, 0, NULL);

   nir_use(b, v1);
   nir_use(b, v2);
   nir_use(b, v3);

   run_pass();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_minimize_call_live_states_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = load_const (0x00000000 = 0.000000)
          32     %1 = load_const (0x00000000)
          32     %2 = @load_push_constant (%1 (0x0)) (base=0, range=0, align_mul=4, align_offset=0)
          32     %3 = load_const (0x00000008 = 0.000000)
          32     %4 = load_const (0x00000008)
          64     %5 = @load_push_constant (%4 (0x8)) (base=0, range=0, align_mul=8, align_offset=0)
          32x3   %6 = @load_global (%5) (access=none, align_mul=4, align_offset=0)
          32     %7 = @load_global_constant (%5) (access=none, align_mul=4, align_offset=0)
                      call test_function (indirect %2)
          32     %8 = load_const (0x00000008 = 0.000000)
          32     %9 = load_const (0x00000008)
          64    %10 = @load_push_constant (%9 (0x8)) (base=0, range=0, align_mul=8, align_offset=0)
          32    %11 = @load_global_constant (%10) (access=none, align_mul=4, align_offset=0)
                      @use (%10)
                      @use (%6)
                      @use (%11)
                      // succs: b1
          block b1:
      }

      decl_function test_function ()
   )"));
}

TEST_F(nir_minimize_call_live_states_test, life_alu)
{
   nir_def *callee = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0));

   nir_def *v1 = nir_iadd_imm(b, callee, 1);
   nir_def *v2 = nir_vec3(b, callee, callee, callee);

   nir_build_indirect_call(b, indirect_decl, callee, 0, NULL);

   nir_use(b, v1);
   nir_use(b, v2);

   run_pass();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_minimize_call_live_states_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = load_const (0x00000000 = 0.000000)
          32     %1 = load_const (0x00000000)
          32     %2 = @load_push_constant (%1 (0x0)) (base=0, range=0, align_mul=4, align_offset=0)
          32     %3 = load_const (0x00000001 = 0.000000)
          32     %4 = load_const (0x00000001)
          32     %5 = iadd %2, %4 (0x1)
          32x3   %6 = vec3 %2, %2, %2
                      call test_function (indirect %2)
          32     %7 = load_const (0x00000000 = 0.000000)
          32     %8 = load_const (0x00000000)
          32     %9 = @load_push_constant (%8 (0x0)) (base=0, range=0, align_mul=4, align_offset=0)
          32    %10 = load_const (0x00000001 = 0.000000)
          32    %11 = load_const (0x00000001)
          32    %12 = iadd %9, %11 (0x1)
          32x3  %13 = vec3 %9, %9, %9
                      @use (%12)
                      @use (%13)
                      // succs: b1
          block b1:
      }

      decl_function test_function ()
   )"));
}

TEST_F(nir_minimize_call_live_states_test, call_inside_if)
{
   nir_def *callee = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0));

   nir_def *value = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 4));

   nir_def *v1 = nir_iadd_imm(b, value, 1);

   nir_push_if(b, nir_ine_imm(b, value, 0));
   nir_build_indirect_call(b, indirect_decl, callee, 0, NULL);
   nir_pop_if(b, NULL);

   nir_use(b, value);
   nir_use(b, v1);

   run_pass();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_minimize_call_live_states_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = load_const (0x00000000 = 0.000000)
          32     %1 = load_const (0x00000000)
          32     %2 = @load_push_constant (%1 (0x0)) (base=0, range=0, align_mul=4, align_offset=0)
          32     %3 = load_const (0x00000004 = 0.000000)
          32     %4 = load_const (0x00000004)
          32     %5 = @load_push_constant (%4 (0x4)) (base=0, range=0, align_mul=4, align_offset=0)
          32     %6 = load_const (0x00000001 = 0.000000)
          32     %7 = load_const (0x00000001)
          32     %8 = iadd %5, %7 (0x1)
          32     %9 = load_const (0x00000000 = 0.000000)
          32    %10 = load_const (0x00000000)
          1     %11 = ine %5, %10 (0x0)
                      // succs: b1 b2
          if %11 {
              block b1:   // preds: b0
                          call test_function (indirect %2)
              32    %12 = load_const (0x00000004 = 0.000000)
              32    %13 = load_const (0x00000004)
              32    %14 = @load_push_constant (%13 (0x4)) (base=0, range=0, align_mul=4, align_offset=0)
              32    %15 = load_const (0x00000001 = 0.000000)
              32    %16 = load_const (0x00000001)
              32    %17 = iadd %14, %16 (0x1)
                          // succs: b3
          } else {
              block b2:  // preds: b0, succs: b3
          }
          block b3:   // preds: b1 b2
          32    %18 = phi b1: %17, b2: %8
          32    %19 = phi b1: %14, b2: %5
                      @use (%19)
                      @use (%18)
                      // succs: b4
          block b4:
      }

      decl_function test_function ()
   )"));
}

TEST_F(nir_minimize_call_live_states_test, call_inside_loop)
{
   nir_def *addr = nir_load_push_constant(b, 1, 64, nir_imm_int(b, 0));
   nir_def *index = nir_channel(b, nir_load_ray_launch_id(b), 0);
   addr = nir_iadd(b, addr, nir_u2u64(b, nir_imul_imm(b, index, 4)));

   nir_def *callee = nir_load_global_constant(b, 1, 32, addr);

   nir_def *value = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 4));
   nir_def *v1 = nir_iadd_imm(b, value, 1);

   nir_push_loop(b);
   {
      nir_def *c = nir_read_first_invocation(b, callee);
      nir_push_if(b, nir_ieq(b, c, callee));
      nir_build_indirect_call(b, indirect_decl, c, 0, NULL);
      nir_jump(b, nir_jump_break);
      nir_pop_if(b, NULL);
   }
   nir_pop_loop(b, NULL);

   nir_use(b, callee);
   nir_use(b, value);
   nir_use(b, v1);

   run_pass();

   check_nir_string(NIR_REFERENCE_SHADER(R"(
      shader: MESA_SHADER_COMPUTE
      name: nir_minimize_call_live_states_test
      workgroup_size: 1, 1, 1
      max_subgroup_size: 128
      min_subgroup_size: 1
      decl_function main () (entrypoint)

      impl main {
          block b0:   // preds:
          32     %0 = load_const (0x00000000 = 0.000000)
          32     %1 = load_const (0x00000000)
          64     %2 = @load_push_constant (%1 (0x0)) (base=0, range=0, align_mul=8, align_offset=0)
          32x3   %3 = @load_ray_launch_id
          32     %4 = mov %3.x
          32     %5 = load_const (0x00000002 = 0.000000)
          32     %6 = load_const (0x00000002)
          32     %7 = ishl %4, %6 (0x2)
          64     %8 = u2u64 %7
          64     %9 = iadd %2, %8
          32    %10 = @load_global_constant (%9) (access=none, align_mul=4, align_offset=0)
          32    %11 = load_const (0x00000004 = 0.000000)
          32    %12 = load_const (0x00000004)
          32    %13 = @load_push_constant (%12 (0x4)) (base=0, range=0, align_mul=4, align_offset=0)
          32    %14 = load_const (0x00000001 = 0.000000)
          32    %15 = load_const (0x00000001)
          32    %16 = iadd %13, %15 (0x1)
                      // succs: b1
          loop {
              block b1:   // preds: b0 b4
              32    %17 = @read_first_invocation (%10)
              1     %18 = ieq %17, %10
                          // succs: b2 b3
              if %18 {
                  block b2:   // preds: b1
                              call test_function (indirect %17)
                  32    %19 = load_const (0x00000000 = 0.000000)
                  32    %20 = load_const (0x00000000)
                  64    %21 = @load_push_constant (%20 (0x0)) (base=0, range=0, align_mul=8, align_offset=0)
                  32x3  %22 = @load_ray_launch_id
                  32    %23 = mov %22.x
                  32    %24 = load_const (0x00000002 = 0.000000)
                  32    %25 = load_const (0x00000002)
                  32    %26 = ishl %23, %25 (0x2)
                  64    %27 = u2u64 %26
                  64    %28 = iadd %21, %27
                  32    %29 = @load_global_constant (%28) (access=none, align_mul=4, align_offset=0)
                  32    %30 = load_const (0x00000004 = 0.000000)
                  32    %31 = load_const (0x00000004)
                  32    %32 = @load_push_constant (%31 (0x4)) (base=0, range=0, align_mul=4, align_offset=0)
                  32    %33 = load_const (0x00000001 = 0.000000)
                  32    %34 = load_const (0x00000001)
                  32    %35 = iadd %32, %34 (0x1)
                              break
                              // succs: b5
              } else {
                  block b3:  // preds: b1, succs: b4
              }
              block b4:  // preds: b3, succs: b1
          }
          block b5:// preds: b2
          @use (%29)
          @use (%32)
          @use (%35)
          // succs: b6
          block b6:
      }

      decl_function test_function ()
   )"));
}
