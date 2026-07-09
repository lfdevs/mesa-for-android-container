/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class predicate_logic_test : public brw_shader_pass_test {};

TEST_F(predicate_logic_test, and_of_two_cmp)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg b = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg c = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg d = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg t0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, exp, BRW_TYPE_D);

   bld.CMP(t0, a, b, BRW_CONDITIONAL_L);
   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.AND(bld.null_reg_d(), t1, t0)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_PROGRESS(brw_opt_predicate_logic, bld);

   exp.CMP(t0, a, b, BRW_CONDITIONAL_L);
   set_predicate(BRW_PREDICATE_NORMAL,
                 exp.CMP(exp.null_reg_d(), c, d, BRW_CONDITIONAL_L));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(predicate_logic_test, or_of_two_cmp)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg b = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg c = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg d = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg t0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, exp, BRW_TYPE_D);

   bld.CMP(t0, a, b, BRW_CONDITIONAL_L);
   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.OR(bld.null_reg_d(), t1, t0)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_PROGRESS(brw_opt_predicate_logic, bld);

   exp.CMP(t0, a, b, BRW_CONDITIONAL_L);
   set_predicate_inv(BRW_PREDICATE_NORMAL, true,
                     exp.CMP(exp.null_reg_d(), c, d, BRW_CONDITIONAL_L));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(predicate_logic_test, and_to_predicated_mov)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg c = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg d = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg x = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, exp, BRW_TYPE_D);

   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.AND(bld.null_reg_d(), t1, x)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_PROGRESS(brw_opt_predicate_logic, bld);

   exp.CMP(t1, c, d, BRW_CONDITIONAL_L);
   brw_inst *mov = exp.MOV(exp.null_reg_d(), x);
   mov->conditional_mod = BRW_CONDITIONAL_NZ;
   set_predicate(BRW_PREDICATE_NORMAL, mov);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(predicate_logic_test, and_negated_source_to_predicated_not)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg c = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg d = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg x = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, exp, BRW_TYPE_D);

   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.AND(bld.null_reg_d(), negate(x), t1)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_PROGRESS(brw_opt_predicate_logic, bld);

   exp.CMP(t1, c, d, BRW_CONDITIONAL_L);
   brw_inst *no = exp.NOT(exp.null_reg_d(), x);
   no->conditional_mod = BRW_CONDITIONAL_NZ;
   set_predicate(BRW_PREDICATE_NORMAL, no);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(predicate_logic_test, negated_predicate_source_is_not_optimized)
{
   brw_builder bld = make_shader();

   brw_reg c = vgrf(bld, BRW_TYPE_D);
   brw_reg d = vgrf(bld, BRW_TYPE_D);
   brw_reg x = vgrf(bld, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, BRW_TYPE_D);

   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.AND(bld.null_reg_d(), x, negate(t1))
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_NO_PROGRESS(brw_opt_predicate_logic, bld);
}

TEST_F(predicate_logic_test, sel_source_is_not_optimized)
{
   brw_builder bld = make_shader();

   brw_reg a = vgrf(bld, BRW_TYPE_D);
   brw_reg b = vgrf(bld, BRW_TYPE_D);
   brw_reg c = vgrf(bld, BRW_TYPE_D);
   brw_reg d = vgrf(bld, BRW_TYPE_D);
   brw_reg t0 = vgrf(bld, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, BRW_TYPE_D);

   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(t0, a, b));
   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.AND(bld.null_reg_d(), t0, t1)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_NO_PROGRESS(brw_opt_predicate_logic, bld);
}

TEST_F(predicate_logic_test, and_without_boolean_source)
{
   brw_builder bld = make_shader();

   brw_reg a = vgrf(bld, BRW_TYPE_D);
   brw_reg b = vgrf(bld, BRW_TYPE_D);
   brw_reg c = vgrf(bld, BRW_TYPE_D);
   brw_reg d = vgrf(bld, BRW_TYPE_D);
   brw_reg t0 = vgrf(bld, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, BRW_TYPE_D);

   bld.ADD(t0, a, b);
   bld.ADD(t1, c, d);
   bld.AND(bld.null_reg_d(), t1, t0)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_NO_PROGRESS(brw_opt_predicate_logic, bld);
}

TEST_F(predicate_logic_test, non_null_destination_is_not_optimized)
{
   brw_builder bld = make_shader();

   brw_reg a = vgrf(bld, BRW_TYPE_D);
   brw_reg b = vgrf(bld, BRW_TYPE_D);
   brw_reg c = vgrf(bld, BRW_TYPE_D);
   brw_reg d = vgrf(bld, BRW_TYPE_D);
   brw_reg t0 = vgrf(bld, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, BRW_TYPE_D);
   brw_reg t2 = vgrf(bld, BRW_TYPE_D);

   bld.CMP(t0, a, b, BRW_CONDITIONAL_L);
   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.AND(t2, t1, t0)
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_NO_PROGRESS(brw_opt_predicate_logic, bld);
}

TEST_F(predicate_logic_test, no_conditional_mod_is_not_optimized)
{
   brw_builder bld = make_shader();

   brw_reg a = vgrf(bld, BRW_TYPE_D);
   brw_reg b = vgrf(bld, BRW_TYPE_D);
   brw_reg c = vgrf(bld, BRW_TYPE_D);
   brw_reg d = vgrf(bld, BRW_TYPE_D);
   brw_reg t0 = vgrf(bld, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, BRW_TYPE_D);

   bld.CMP(t0, a, b, BRW_CONDITIONAL_L);
   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);
   bld.AND(bld.null_reg_d(), t1, t0);

   EXPECT_NO_PROGRESS(brw_opt_predicate_logic, bld);
}

TEST_F(predicate_logic_test, cmp_flag_subreg_mismatch)
{
   brw_builder bld = make_shader();

   brw_reg a = vgrf(bld, BRW_TYPE_D);
   brw_reg b = vgrf(bld, BRW_TYPE_D);
   brw_reg c = vgrf(bld, BRW_TYPE_D);
   brw_reg d = vgrf(bld, BRW_TYPE_D);
   brw_reg e = vgrf(bld, BRW_TYPE_D);
   brw_reg f = vgrf(bld, BRW_TYPE_D);
   brw_reg t0 = vgrf(bld, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, BRW_TYPE_D);
   brw_reg dst = vgrf(bld, BRW_TYPE_D);

   bld.CMP(t0, a, b, BRW_CONDITIONAL_L);
   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);

   brw_inst *logic = bld.AND(bld.null_reg_d(), t1, t0);
   logic->conditional_mod = BRW_CONDITIONAL_NZ;
   logic->flag_subreg = 1;

   /* Consumer reads the AND's flag on f0.1. */
   brw_inst *sel = bld.SEL(dst, e, f);
   set_predicate(BRW_PREDICATE_NORMAL, sel);
   sel->flag_subreg = 1;

   EXPECT_NO_PROGRESS(brw_opt_predicate_logic, bld);
}

TEST_F(predicate_logic_test, mov_flag_subreg_mismatch)
{
   brw_builder bld = make_shader();

   brw_reg c = vgrf(bld, BRW_TYPE_D);
   brw_reg d = vgrf(bld, BRW_TYPE_D);
   brw_reg e = vgrf(bld, BRW_TYPE_D);
   brw_reg f = vgrf(bld, BRW_TYPE_D);
   brw_reg x = vgrf(bld, BRW_TYPE_D);
   brw_reg t1 = vgrf(bld, BRW_TYPE_D);
   brw_reg dst = vgrf(bld, BRW_TYPE_D);

   bld.CMP(t1, c, d, BRW_CONDITIONAL_L);

   brw_inst *logic = bld.AND(bld.null_reg_d(), t1, x);
   logic->conditional_mod = BRW_CONDITIONAL_NZ;
   logic->flag_subreg = 1;

   /* Consumer reads the AND's flag on f0.1. */
   brw_inst *sel = bld.SEL(dst, e, f);
   set_predicate(BRW_PREDICATE_NORMAL, sel);
   sel->flag_subreg = 1;

   EXPECT_NO_PROGRESS(brw_opt_predicate_logic, bld);
}
