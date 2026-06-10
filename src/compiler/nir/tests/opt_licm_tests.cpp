/* Copyright 2025 Advanced Micro Devices, Inc.
 * Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir_test.h"

class nir_opt_licm_test : public nir_test {
protected:
   nir_opt_licm_test()
      : nir_test::nir_test("nir_opt_licm_test", MESA_SHADER_COMPUTE)
   {
   }

   nir_loop *loop;
   nir_block *original_block;
   nir_def *x, *y, *z, *r;
   bool expect_progress;
   bool insert_after_break;

   void test_init();
   void test_finish(nir_opt_licm_filter_cb filter_instr);
};

void
nir_opt_licm_test::test_init()
{
   x = nir_load_global(b, 1, 32, nir_undef(b, 1, 64));
   y = nir_load_global(b, 1, 32, nir_undef(b, 1, 64));
   z = nir_load_global(b, 1, 32, nir_undef(b, 1, 64));

   loop = nir_push_loop(b);
   if (insert_after_break)
      nir_break_if(b, nir_undef(b, 1, 1));
   original_block = nir_loop_last_block(loop);
}

static bool
accum_src_bits(nir_src *src, void *state)
{
   *((unsigned*)state) += src->ssa->num_components * src->ssa->bit_size;
   return true;
}

static bool
licm_filter(nir_instr *instr, nir_loop *loop,
            bool instr_block_dominates_exit)
{
   if (!instr_block_dominates_exit && !nir_instr_can_speculate(instr))
      return false;

   if (instr->type == nir_instr_type_alu) {
      nir_def *def = nir_instr_def(instr);
      unsigned sum_src_bits = 0;

      nir_foreach_src(instr, accum_src_bits, &sum_src_bits);

      /* Simple ALU filter. */
      return def->num_components * def->bit_size <= sum_src_bits;
   }

   return true;
}

void
nir_opt_licm_test::test_finish(nir_opt_licm_filter_cb filter_instr)
{
   if (!insert_after_break)
      nir_break_if(b, nir_undef(b, 1, 1));
   nir_pop_loop(b, loop);
   nir_validate_shader(b->shader, NULL);

   bool progress = false;
   NIR_PASS(progress, b->shader, nir_opt_licm, filter_instr);

   if (expect_progress) {
      ASSERT_TRUE(progress);
      ASSERT_EQ(nir_def_instr(r)->block, nir_loop_predecessor_block(loop));
   } else {
      ASSERT_FALSE(progress);
      ASSERT_EQ(nir_def_instr(r)->block, original_block);
   }
}

TEST_F(nir_opt_licm_test, hoist_alu_unary)
{
   this->insert_after_break = true;
   this->expect_progress = true;
   this->test_init();
   r = nir_ineg(b, x);
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, hoist_alu_binary)
{
   this->insert_after_break = true;
   this->expect_progress = true;
   this->test_init();
   r = nir_iadd(b, x, y);
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, skip_alu_u2u64)
{
   this->insert_after_break = true;
   this->expect_progress = false;
   this->test_init();
   r = nir_u2u64(b, x);

   /* If sizeof(dst) > sizeof(all srcs), the default behavior is not to hoist
    * because that would increase register usage of the whole loop.
    */
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, skip_load_ssbo_no_flags_before_break)
{
   this->insert_after_break = false;
   this->expect_progress = false;
   this->test_init();
   r = nir_load_ssbo(b, 1, 32, x, y);
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, hoist_load_ssbo_reorderable_before_break)
{
   this->insert_after_break = false;
   this->expect_progress = true;
   this->test_init();
   r = nir_load_ssbo(b, 1, 32, x, y);
   nir_intrinsic_set_access(nir_def_as_intrinsic(r),
                            (gl_access_qualifier)(ACCESS_CAN_REORDER));
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, skip_load_ssbo_reorderable)
{
   this->insert_after_break = true;
   this->expect_progress = false;
   this->test_init();
   r = nir_load_ssbo(b, 1, 32, x, y);
   nir_intrinsic_set_access(nir_def_as_intrinsic(r),
                            (gl_access_qualifier)(ACCESS_CAN_REORDER));
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, skip_load_ssbo_speculatable)
{
   this->insert_after_break = true;
   this->expect_progress = false;
   this->test_init();
   r = nir_load_ssbo(b, 1, 32, x, y);
   nir_intrinsic_set_access(nir_def_as_intrinsic(r),
                            (gl_access_qualifier)(ACCESS_CAN_SPECULATE));
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, hoist_load_ssbo_reorderable_speculatable)
{
   this->insert_after_break = true;
   this->expect_progress = true;
   this->test_init();
   r = nir_load_ssbo(b, 1, 32, x, y);
   nir_intrinsic_set_access(nir_def_as_intrinsic(r),
                            (gl_access_qualifier)(ACCESS_CAN_REORDER |
                                                  ACCESS_CAN_SPECULATE));
   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, hoist_alu_2_nested_loops)
{
   this->insert_after_break = true;
   this->expect_progress = true;
   this->test_init();

   nir_loop *nested_loop = nir_push_loop(b);
   {
      nir_break_if(b, nir_undef(b, 1, 1));
      r = nir_ineg(b, x);
   }
   nir_pop_loop(b, nested_loop);

   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, hoist_alu_6_nested_loops)
{
   this->insert_after_break = true;
   this->expect_progress = true;
   this->test_init();

   nir_loop *nested_loops[5];

   for (unsigned i = 0; i < ARRAY_SIZE(nested_loops); i++) {
      nested_loops[i] = nir_push_loop(b);
      nir_break_if(b, nir_undef(b, 1, 1));
   }

   r = nir_ineg(b, x);

   for (int i = ARRAY_SIZE(nested_loops) - 1; i >= 0; i--)
      nir_pop_loop(b, nested_loops[i]);

   this->test_finish(licm_filter);
}

TEST_F(nir_opt_licm_test, skip_tex)
{
   this->insert_after_break = true;
   this->expect_progress = false;
   this->test_init();

   nir_tex_builder fields = {0};
   fields.coord = x;
   fields.texture_handle = y;
   fields.dest_type = nir_type_uint32;

   r = nir_build_tex_struct(b, nir_texop_tex, fields);
   this->test_finish(NULL);
}

TEST_F(nir_opt_licm_test, hoist_tex_before_break)
{
   this->insert_after_break = false;
   this->expect_progress = true;
   this->test_init();

   nir_tex_builder fields = {0};
   fields.coord = x;
   fields.texture_handle = y;
   fields.dest_type = nir_type_uint32;

   r = nir_build_tex_struct(b, nir_texop_tex, fields);
   this->test_finish(NULL);
}

TEST_F(nir_opt_licm_test, hoist_tex_speculatable)
{
   this->insert_after_break = true;
   this->expect_progress = true;
   this->test_init();

   nir_tex_builder fields = {0};
   fields.coord = x;
   fields.texture_handle = y;
   fields.can_speculate = true;
   fields.dest_type = nir_type_uint32;

   r = nir_build_tex_struct(b, nir_texop_tex, fields);
   this->test_finish(licm_filter);
}
