/*
 * Copyright © 2020 Intel Corporation
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

#include "gtest/gtest.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_phi_builder.h"

#define UNROLL_TEST_INSERT(_label, _type, _init, _limit, _step,         \
                           _cond, _incr, _rev, _exp_res,                \
                           _exp_instr_count, _exp_loop_count)           \
   TEST_F(nir_loop_unroll_test, _label)                                 \
   {                                                                    \
      nir_def *init = nir_imm_##_type(&bld, _init);                 \
      nir_def *limit = nir_imm_##_type(&bld, _limit);               \
      nir_def *step = nir_imm_##_type(&bld, _step);                 \
      loop_unroll_test_helper(&bld, init, limit, step,                  \
                              &nir_##_cond, &nir_##_incr, _rev);        \
      EXPECT_##_exp_res(nir_opt_loop_unroll(bld.shader));               \
      EXPECT_EQ(_exp_instr_count, count_instr(nir_op_##_incr));         \
      EXPECT_EQ(_exp_loop_count, count_loops());                        \
   }

#define UNROLL_TEST_UNKNOWN_INIT_INSERT(_label, __type, _limit, _step,    \
                                        _cond, _incr, _rev, _exp_res,     \
                                        _exp_instr_count, _exp_loop_count)\
   TEST_F(nir_loop_unroll_test, _label)                                   \
   {                                                                      \
      nir_def *one = nir_imm_int(&bld, 1);                                \
      nir_def *twelve = nir_imm_int(&bld, 12);                            \
      nir_def *init = nir_load_ubo(&bld, 1, 32, one, twelve, (gl_access_qualifier)0, 0, 0, 0, 16); \
      nir_def *limit = nir_imm_##__type(&bld, _limit);                    \
      nir_def *step = nir_imm_##__type(&bld, _step);                      \
      loop_unroll_test_helper(&bld, init, limit, step,                    \
                              &nir_##_cond, &nir_##_incr, _rev);          \
      EXPECT_##_exp_res(nir_opt_loop_unroll(bld.shader));                 \
      EXPECT_EQ(_exp_instr_count, count_instr(nir_op_##_incr));           \
      EXPECT_EQ(_exp_loop_count, count_loops());                          \
   }


namespace {

class nir_loop_unroll_test : public ::testing::Test {
protected:
   nir_loop_unroll_test()
   {
      glsl_type_singleton_init_or_ref();
      static nir_shader_compiler_options options = { };
      options.max_unroll_iterations = 32;
      options.force_indirect_unrolling_sampler = false;
      options.force_indirect_unrolling = nir_var_all;
      bld = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, &options,
                                           "loop unrolling tests");
   }
   ~nir_loop_unroll_test()
   {
      ralloc_free(bld.shader);
      glsl_type_singleton_decref();
   }

   enum instr_count_filter {
      instr_count_all,
      instr_count_used,
      instr_count_unused,
   };

   int count_instr_common(nir_op op, instr_count_filter filter);
   int count_instr(nir_op op);
   int count_used_instr(nir_op op);
   int count_unused_instr(nir_op op);
   int count_loops(void);

   nir_builder bld;
};

} /* namespace */

int
nir_loop_unroll_test::count_instr_common(nir_op op, instr_count_filter filter)
{
   int count = 0;
   nir_foreach_block(block, bld.impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_alu)
            continue;
         nir_alu_instr *alu_instr = nir_instr_as_alu(instr);
         if (alu_instr->op != op)
            continue;

         bool unused = nir_def_is_unused(&alu_instr->def);
         if (filter == instr_count_all ||
             (filter == instr_count_used && !unused) ||
             (filter == instr_count_unused && unused))
            count++;
      }
   }

   return count;
}

int
nir_loop_unroll_test::count_instr(nir_op op)
{
   return count_instr_common(op, instr_count_all);
}

int
nir_loop_unroll_test::count_used_instr(nir_op op)
{
   return count_instr_common(op, instr_count_used);
}

int
nir_loop_unroll_test::count_unused_instr(nir_op op)
{
   return count_instr_common(op, instr_count_unused);
}

int
nir_loop_unroll_test::count_loops(void)
{
   int count = 0;
   foreach_list_typed(nir_cf_node, cf_node, node, &bld.impl->body) {
      if (cf_node->type == nir_cf_node_loop)
         count++;
   }

   return count;
}

static void
partial_unroll_oob_load_test_helper(nir_builder *bld, unsigned init_value,
                                    unsigned array_len, bool limit_minus_one)
{
   /* Build:
    *
    *    for (int i = init_value; i < n_stop [- 1]; i++)
    *       if (0.0 < stops[i])
    *          break;
    */
   nir_variable *n_stop =
      nir_variable_create(bld->shader, nir_var_uniform, glsl_int_type(),
                          "n_stop");
   nir_variable *stops =
      nir_variable_create(bld->shader, nir_var_uniform,
                          glsl_array_type(glsl_float_type(), array_len, 0),
                          "stops");

   nir_def *stop_len = nir_imm_float(bld, 0.0f);
   nir_def *init = nir_imm_int(bld, init_value);

   nir_loop *loop = nir_push_loop(bld);

   nir_block *top_block =
      nir_cf_node_as_block(nir_cf_node_prev(&loop->cf_node));
   nir_block *head_block = nir_loop_first_block(loop);

   nir_phi_instr *phi = nir_phi_instr_create(bld->shader);
   nir_def_init(&phi->instr, &phi->def, 1, 32);
   nir_phi_instr_add_src(phi, top_block, init);

   nir_def *i = &phi->def;
   nir_def *limit = nir_load_deref(bld, nir_build_deref_var(bld, n_stop));
   if (limit_minus_one)
      limit = nir_iadd_imm(bld, limit, -1);

   nir_def *limit_cond = nir_ige(bld, i, limit);
   nir_deref_instr *deref =
      nir_build_deref_array(bld, nir_build_deref_var(bld, stops), i);
   nir_def *stop = nir_load_deref(bld, deref);
   nir_def *stop_cond = nir_flt(bld, stop_len, stop);

   nir_break_if(bld, nir_ior(bld, limit_cond, stop_cond));

   nir_def *next = nir_iadd_imm(bld, i, 1);
   nir_phi_instr_add_src(phi, nir_cursor_current_block(bld->cursor), next);

   nir_pop_loop(bld, loop);

   bld->cursor = nir_after_phis(head_block);
   nir_builder_instr_insert(bld, &phi->instr);

   nir_validate_shader(bld->shader, NULL);
}

void
loop_unroll_test_helper(nir_builder *bld, nir_def *init,
                        nir_def *limit, nir_def *step,
                        nir_def* (*cond_instr)(nir_builder*,
                                                   nir_def*,
                                                   nir_def*),
                        nir_def* (*incr_instr)(nir_builder*,
                                                   nir_def*,
                                                   nir_def*),
                        bool reverse)
{
   nir_loop *loop = nir_push_loop(bld);

   nir_block *top_block =
      nir_cf_node_as_block(nir_cf_node_prev(&loop->cf_node));
   nir_block *head_block = nir_loop_first_block(loop);

   nir_phi_instr *phi = nir_phi_instr_create(bld->shader);
   nir_def_init(&phi->instr, &phi->def, 1, 32);

   nir_phi_instr_add_src(phi, top_block, init);

   nir_def *cond = cond_instr(bld,
                                  (reverse ? limit : &phi->def),
                                  (reverse ? &phi->def : limit));

   nir_break_if(bld, cond);

   nir_def *var = incr_instr(bld, &phi->def, step);

   nir_phi_instr_add_src(phi, nir_cursor_current_block(bld->cursor), var);

   nir_pop_loop(bld, loop);

   bld->cursor = nir_after_phis(head_block);
   nir_builder_instr_insert(bld, &phi->instr);

   nir_validate_shader(bld->shader, NULL);
}

UNROLL_TEST_INSERT(iadd,     int,   0,     24,   4,
                   ige,      iadd,  false, TRUE, 6, 0)
UNROLL_TEST_INSERT(iadd_rev, int,   0,     24,   4,
                   ilt,      iadd,  true,  TRUE, 7, 0)
UNROLL_TEST_INSERT(fadd,     float, 0.0,   24.0, 4.0,
                   fge,      fadd,  false, TRUE, 6, 0)
UNROLL_TEST_INSERT(fadd_rev, float, 0.0,   24.0, 4.0,
                   flt,      fadd,  true,  TRUE, 7, 0)
UNROLL_TEST_INSERT(imul,     int,   1,     81,   3,
                   ige,      imul,  false, TRUE, 4, 0)
UNROLL_TEST_INSERT(imul_rev, int,   1,     81,   3,
                   ilt,      imul,  true,  TRUE, 5, 0)
#if 0 /* Disable tests until support is re-enabled in loop_analyze. */
UNROLL_TEST_INSERT(fmul,     float, 1.5,   81.0, 3.0,
                   fge,      fmul,  false, TRUE, 4, 0)
UNROLL_TEST_INSERT(fmul_rev, float, 1.0,   81.0, 3.0,
                   flt,      fmul,  true,  TRUE, 5, 0)
#endif
UNROLL_TEST_INSERT(ishl,     int,   1,     128,  1,
                   ige,      ishl,  false, TRUE, 7, 0)
UNROLL_TEST_INSERT(ishl_rev, int,   1,     128,  1,
                   ilt,      ishl,  true,  TRUE, 8, 0)
UNROLL_TEST_INSERT(ishr,     int,   64,    4,    1,
                   ilt,      ishr,  false, TRUE, 5, 0)
UNROLL_TEST_INSERT(ishr_rev, int,   64,    4,    1,
                   ige,      ishr,  true,  TRUE, 4, 0)
UNROLL_TEST_INSERT(ushr,     int,   64,    4,    1,
                   ilt,      ushr,  false, TRUE, 5, 0)
UNROLL_TEST_INSERT(ushr_rev, int,   64,    4,    1,
                   ige,      ushr,  true,  TRUE, 4, 0)

UNROLL_TEST_INSERT(lshl_neg,     int,  0xf0f0f0f0, 0,    1,
                   ige,          ishl, false,      TRUE, 4, 0)
UNROLL_TEST_INSERT(lshl_neg_rev, int,  0xf0f0f0f0, 0,    1,
                   ilt,          ishl, true,       TRUE, 4, 0)

UNROLL_TEST_UNKNOWN_INIT_INSERT(iadd_uge_unknown_init_gt, int, 4, 6,
                                uge, iadd, false, TRUE, 2, 0)
UNROLL_TEST_UNKNOWN_INIT_INSERT(iadd_uge_unknown_init_eq, int, 16, 4,
                                uge, iadd, false, TRUE, 5, 0)
UNROLL_TEST_UNKNOWN_INIT_INSERT(iadd_ugt_unknown_init_eq, int, 16, 4,
                                ult, iadd, true, TRUE, 6, 0)
UNROLL_TEST_UNKNOWN_INIT_INSERT(iadd_ige_unknown_init, int, 4, 6,
                                ige, iadd, false, FALSE, 1, 1)

/* Partial unrolling leaves a residual loop.  The OOB array access check should
 * make that residual loop exit unconditionally, leaving the stops[i] comparison
 * computed but unused until later DCE removes it.
 */
TEST_F(nir_loop_unroll_test, partial_unroll_oob_load_nonzero_init)
{
   partial_unroll_oob_load_test_helper(&bld, 1, 4, false);

   ASSERT_TRUE(nir_opt_loop_unroll(bld.shader));
   EXPECT_EQ(3, count_used_instr(nir_op_flt));
   EXPECT_EQ(1, count_unused_instr(nir_op_flt));
}

TEST_F(nir_loop_unroll_test, partial_unroll_oob_load_zero_init)
{
   partial_unroll_oob_load_test_helper(&bld, 0, 4, true);

   ASSERT_TRUE(nir_opt_loop_unroll(bld.shader));
   EXPECT_EQ(4, count_used_instr(nir_op_flt));
   EXPECT_EQ(1, count_unused_instr(nir_op_flt));
}
