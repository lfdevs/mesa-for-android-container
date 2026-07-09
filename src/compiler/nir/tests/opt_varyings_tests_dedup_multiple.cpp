/* Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

/* Verify that deduplication works when each slot has multiple stores. */

#include "nir_opt_varyings_test.h"

namespace {

class nir_opt_varyings_test_dedup_multiple : public nir_opt_varyings_test
{};

TEST_F(nir_opt_varyings_test_dedup_multiple, if_store2_else_store2)
{
   create_shaders(MESA_SHADER_GEOMETRY, MESA_SHADER_FRAGMENT);

   /* if (..) {
    *    output[0] = a;
    *    output[1] = a; // eliminated
    * } else {
    *    output[0] = b;
    *    output[1] = b; // eliminated
    * }
    */

   nir_intrinsic_instr *store[4] = {0};

   nir_if *if_block =
      nir_push_if(b1, nir_ieq_imm(b1, nir_load_primitive_id(b1), 0));

   for (unsigned block = 0; block < 2; block++) {
      nir_def *input = load_input(b1, VARYING_SLOT_VAR0, block,
                                  nir_type_float32, 0, 0);

      for (unsigned s = 0; s < 2; s++) {
         store[block * 2 + s] = store_output(b1, VARYING_SLOT_VAR0, s,
                                             nir_type_float32, input, 0, false);
      }
      nir_emit_vertex(b1, 0);

      if (block == 0)
         nir_push_else(b1, if_block);
   }

   nir_end_primitive(b1);
   nir_pop_if(b1, if_block);

   nir_def *load[2] = {0};
   for (unsigned s = 0; s < 2; s++) {
      load[s] = load_input(b2, VARYING_SLOT_VAR0, s,
                           nir_type_float32, 0, INTERP_PERSP_PIXEL);
      store_output(b2, (gl_varying_slot)FRAG_RESULT_DATA0, s,
                   nir_type_float32, load[s], 0, false);
   }

   ASSERT_EQ(opt_varyings(), (nir_progress_producer | nir_progress_consumer));
   ASSERT_TRUE(shader_contains_instr(b1, &store[0]->instr));
   ASSERT_TRUE(!shader_contains_instr(b1, &store[1]->instr));
   ASSERT_TRUE(shader_contains_instr(b1, &store[2]->instr));
   ASSERT_TRUE(!shader_contains_instr(b1, &store[3]->instr));
   ASSERT_TRUE(shader_contains_def(b2, load[0]));
   ASSERT_TRUE(!shader_contains_def(b2, load[1]));
}

TEST_F(nir_opt_varyings_test_dedup_multiple, store2_emit_store2_emit)
{
   create_shaders(MESA_SHADER_GEOMETRY, MESA_SHADER_FRAGMENT);

   /* output[0] = a;
    * output[1] = a; // eliminated
    * emit_vertex;
    * output[0] = b;
    * output[1] = b; // eliminated
    * emit_vertex;
    */

   nir_intrinsic_instr *store[4] = {0};

   for (unsigned emit = 0; emit < 2; emit++) {
      nir_def *input = load_input(b1, VARYING_SLOT_VAR0, emit,
                                  nir_type_float32, 0, 0);

      for (unsigned s = 0; s < 2; s++) {
         store[emit * 2 + s] = store_output(b1, VARYING_SLOT_VAR0, s,
                                             nir_type_float32, input, 0, false);
      }
      nir_emit_vertex(b1, 0);

   }

   nir_end_primitive(b1);

   nir_def *load[2] = {0};
   for (unsigned s = 0; s < 2; s++) {
      load[s] = load_input(b2, VARYING_SLOT_VAR0, s,
                           nir_type_float32, 0, INTERP_PERSP_PIXEL);
      store_output(b2, (gl_varying_slot)FRAG_RESULT_DATA0, s,
                   nir_type_float32, load[s], 0, false);
   }

   ASSERT_EQ(opt_varyings(), (nir_progress_producer | nir_progress_consumer));
   ASSERT_TRUE(shader_contains_instr(b1, &store[0]->instr));
   ASSERT_TRUE(!shader_contains_instr(b1, &store[1]->instr));
   ASSERT_TRUE(shader_contains_instr(b1, &store[2]->instr));
   ASSERT_TRUE(!shader_contains_instr(b1, &store[3]->instr));
   ASSERT_TRUE(shader_contains_def(b2, load[0]));
   ASSERT_TRUE(!shader_contains_def(b2, load[1]));
}

TEST_F(nir_opt_varyings_test_dedup_multiple, vs_invalid_case)
{
   create_shaders(MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT);

   /* if (vertex_id == 0)
    *    output[0] = vertex_id;
    * else
    *    output[1] = vertex_id; // can't remove because output[0] is not set
    *                           // for vertex_id > 0
    */

   nir_intrinsic_instr *store[2] = {0};
   nir_def *invoc_id = nir_load_invocation_id(b1);

   nir_if *if_block = nir_push_if(b1, nir_ieq_imm(b1, invoc_id, 0));
   store[0] = store_output(b1, VARYING_SLOT_VAR0, 0, nir_type_float32,
                           invoc_id, 0, false);
   nir_push_else(b1, if_block);
   store[1] = store_output(b1, VARYING_SLOT_VAR0, 1, nir_type_float32,
                           invoc_id, 0, false);
   nir_pop_if(b1, if_block);

   nir_def *load[2] = {0};
   for (unsigned s = 0; s < 2; s++) {
      load[s] = load_input(b2, VARYING_SLOT_VAR0, s,
                           nir_type_float32, 0, INTERP_FLAT);
      store_output(b2, VARYING_SLOT_VAR0, s,
                   nir_type_float32, load[s], 0, false);
   }

   ASSERT_EQ(opt_varyings(), 0);
   ASSERT_TRUE(shader_contains_instr(b1, &store[0]->instr));
   ASSERT_TRUE(shader_contains_instr(b1, &store[1]->instr));
   ASSERT_TRUE(shader_contains_def(b2, load[0]));
   ASSERT_TRUE(shader_contains_def(b2, load[1]));
}

TEST_F(nir_opt_varyings_test_dedup_multiple, tcs_invalid_case)
{
   create_shaders(MESA_SHADER_TESS_CTRL, MESA_SHADER_TESS_EVAL);

   /* if (invocation_id == 0)
    *    output[0] = invocation_id;
    * else
    *    output[1] = invocation_id;
    *
    * These outputs aren't duplicated even if they store the same SSA def.
    * nir_opt_varyings should do nothing.
    */

   nir_intrinsic_instr *store[2] = {0};
   nir_def *invoc_id = nir_load_invocation_id(b1);

   nir_if *if_block = nir_push_if(b1, nir_ieq_imm(b1, invoc_id, 0));
   store[0] = store_output(b1, VARYING_SLOT_PATCH0, 0, nir_type_float32,
                           invoc_id, 0, false);
   nir_push_else(b1, if_block);
   store[1] = store_output(b1, VARYING_SLOT_PATCH0, 1, nir_type_float32,
                           invoc_id, 0, false);
   nir_pop_if(b1, if_block);

   nir_def *load[2] = {0};
   for (unsigned s = 0; s < 2; s++) {
      load[s] = load_input(b2, VARYING_SLOT_PATCH0, s, nir_type_float32, 0,
                           INTERP_FLAT);
      store_output(b2, VARYING_SLOT_VAR0, s,
                   nir_type_float32, load[s], 0, false);
   }

   ASSERT_EQ(opt_varyings(), 0);
   ASSERT_TRUE(shader_contains_instr(b1, &store[0]->instr));
   ASSERT_TRUE(shader_contains_instr(b1, &store[1]->instr));
   ASSERT_TRUE(shader_contains_def(b2, load[0]));
   ASSERT_TRUE(shader_contains_def(b2, load[1]));
}

}
