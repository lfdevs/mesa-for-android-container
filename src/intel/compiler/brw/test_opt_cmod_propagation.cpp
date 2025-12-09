/*
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class cmod_propagation_test : public brw_shader_pass_test {
protected:
   void test_mov_prop(enum brw_conditional_mod cmod,
                      enum brw_reg_type add_type,
                      enum brw_reg_type mov_dst_type,
                      bool expected_cmod_prop_progress);

   void test_saturate_prop(enum brw_conditional_mod before,
                           enum opcode op,
                           enum brw_reg_type add_type,
                           enum brw_reg_type op_type,
                           bool expected_cmod_prop_progress);

   void test_cmp_to_add_sat(enum brw_conditional_mod before,
                            bool add_negative_src0,
                            bool add_negative_constant,
                            bool expected_cmod_prop_progress);

   void test_subtract_merge(enum brw_conditional_mod before,
                            bool expected_cmod_prop_progress);
};

TEST_F(cmod_propagation_test, basic)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero = brw_imm_f(0.0f);

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest, src0, src1)->conditional_mod = BRW_CONDITIONAL_GE;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, basic_other_flag)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero = brw_imm_f(0.0f);

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE)
      ->flag_subreg = 1;

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   brw_inst *add = exp.ADD(dest, src0, src1);
   add->conditional_mod = BRW_CONDITIONAL_GE;
   add->flag_subreg = 1;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, cmp_nonzero)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg src1 = bld.vgrf(BRW_TYPE_F);
   brw_reg nonzero(brw_imm_f(1.0f));

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), dest, nonzero, BRW_CONDITIONAL_GE);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, non_cmod_instruction)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_UD);
   brw_reg src0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg zero(brw_imm_ud(0u));

   bld.FBL(dest, src0);
   bld.CMP(bld.null_reg_ud(), dest, zero, BRW_CONDITIONAL_GE);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, non_cmod_livechannel)
{
   brw_builder bld = make_shader(MESA_SHADER_FRAGMENT, 32);

   brw_reg dest = bld.vgrf(BRW_TYPE_UD);
   brw_reg zero(brw_imm_d(0));

   bld.emit(SHADER_OPCODE_FIND_LIVE_CHANNEL, dest);
   bld.CMP(bld.null_reg_d(), dest, zero, BRW_CONDITIONAL_Z);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, intervening_flag_write)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg src1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src2 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0.0f));

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src2, zero, BRW_CONDITIONAL_GE);
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, intervening_mismatch_flag_write)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0.0f));

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src2, zero, BRW_CONDITIONAL_GE)
      ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), dest, zero, BRW_CONDITIONAL_GE);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest, src0, src1)->conditional_mod = BRW_CONDITIONAL_GE;
   exp.CMP(bld.null_reg_f(), src2, zero, BRW_CONDITIONAL_GE)
      ->flag_subreg = 1;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, intervening_flag_read)
{
   brw_builder bld = make_shader();

   brw_reg dest0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dest1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg src1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src2 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0.0f));

   bld.ADD(dest0, src0, src1);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, intervening_mismatch_flag_read)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero  = brw_imm_f(0.0f);

   bld.ADD(dest0, src0, src1);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero))
      ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest0, src0, src1)->conditional_mod = BRW_CONDITIONAL_GE;
   set_predicate(BRW_PREDICATE_NORMAL, exp.SEL(dest1, src2, zero))
      ->flag_subreg = 1;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, intervening_flag_read_same_value)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero  = brw_imm_f(0.0f);

   set_condmod(BRW_CONDITIONAL_GE,     bld.ADD(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   bld.CMP(bld.null_reg_f(), dest0, zero, BRW_CONDITIONAL_GE);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   set_condmod(BRW_CONDITIONAL_GE,     exp.ADD(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, exp.SEL(dest1, src2, zero));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, negate)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero = brw_imm_f(0.0f);

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), negate(dest), zero, BRW_CONDITIONAL_GE);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest, src0, src1)->conditional_mod = BRW_CONDITIONAL_LE;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, movnz)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);

   bld.CMP(dest, src0, src1, BRW_CONDITIONAL_GE);
   bld.MOV(bld.null_reg_f(), dest)->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.CMP(dest, src0, src1, BRW_CONDITIONAL_GE);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, different_types_cmod_with_zero)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_D);
   brw_reg src0 = bld.vgrf(BRW_TYPE_D);
   brw_reg src1 = bld.vgrf(BRW_TYPE_D);
   brw_reg zero(brw_imm_f(0.0f));

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), retype(dest, BRW_TYPE_F), zero,
           BRW_CONDITIONAL_GE);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, andnz_one)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero = brw_imm_f(0.0f);
   brw_reg one  = brw_imm_d(1);

   bld.CMP(retype(dest, BRW_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   bld.AND(bld.null_reg_d(), dest, one)->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.CMP(retype(dest, BRW_TYPE_F), src0, zero, BRW_CONDITIONAL_L);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, andnz_non_one)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_D);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0.0f));
   brw_reg nonone(brw_imm_d(38));

   bld.CMP(retype(dest, BRW_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.AND(bld.null_reg_d(), dest, nonone));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, cmp_cmpnz)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero = brw_imm_f(0);

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_NZ);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, cmp_cmpg)
{
   brw_builder bld = make_shader();

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_G);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, plnnz_cmpnz)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero = brw_imm_f(0);

   bld.PLN(dst0, src0, zero)->conditional_mod = BRW_CONDITIONAL_NZ;
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_NZ);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.PLN(dst0, src0, zero)->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, plnnz_cmpz)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero = brw_imm_f(0);

   bld.PLN(dst0, src0, zero)->conditional_mod = BRW_CONDITIONAL_NZ;
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_Z);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.PLN(dst0, src0, zero)->conditional_mod = BRW_CONDITIONAL_Z;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, plnnz_sel_cmpz)
{
   brw_builder bld = make_shader();

   brw_reg dst0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dst1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0));

   set_condmod(BRW_CONDITIONAL_NZ, bld.PLN(dst0, src0, zero));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dst1, src0, zero));
   bld.CMP(bld.null_reg_f(), dst0, zero, BRW_CONDITIONAL_Z);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, cmp_cmpg_D)
{
   brw_builder bld = make_shader();

   brw_reg dst0 = bld.vgrf(BRW_TYPE_D);
   brw_reg src0 = bld.vgrf(BRW_TYPE_D);
   brw_reg zero(brw_imm_d(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_d(), dst0, zero, BRW_CONDITIONAL_G);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}



TEST_F(cmod_propagation_test, cmp_cmpg_UD)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg zero = brw_imm_ud(0);

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_ud(), dst0, zero, BRW_CONDITIONAL_G);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, cmp_cmpl_D)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dst0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg zero = brw_imm_d(0);

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_d(), dst0, zero, BRW_CONDITIONAL_L);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, cmp_cmpl_UD)
{
   brw_builder bld = make_shader();

   brw_reg dst0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg src0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg zero(brw_imm_ud(0));

   bld.CMP(dst0, src0, zero, BRW_CONDITIONAL_NZ);
   bld.CMP(bld.null_reg_ud(), dst0, zero, BRW_CONDITIONAL_L);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, andz_one)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_D);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0.0f));
   brw_reg one(brw_imm_d(1));

   bld.CMP(retype(dest, BRW_TYPE_F), src0, zero, BRW_CONDITIONAL_L);
   set_condmod(BRW_CONDITIONAL_Z,
               bld.AND(bld.null_reg_d(), dest, one));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, add_not_merge_with_compare)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg src1 = bld.vgrf(BRW_TYPE_F);

   bld.ADD(dest, src0, src1);
   bld.CMP(bld.null_reg_f(), src0, src1, BRW_CONDITIONAL_L);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

void
cmod_propagation_test::test_subtract_merge(enum brw_conditional_mod before,
                                           bool expected_cmod_prop_progress)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(dest, src0, negate(src1));
   bld.CMP(bld.null_reg_f(), src0, src1, before);

   EXPECT_PROGRESS_RESULT(expected_cmod_prop_progress,
                          brw_opt_cmod_propagation, bld);

   if (expected_cmod_prop_progress) {
      exp.ADD(dest, src0, negate(src1))
         ->conditional_mod = before;

      EXPECT_SHADERS_MATCH(bld, exp);
   }
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_l)
{
   test_subtract_merge(BRW_CONDITIONAL_L, true);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_g)
{
   test_subtract_merge(BRW_CONDITIONAL_G, true);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_le)
{
   test_subtract_merge(BRW_CONDITIONAL_LE, false);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_ge)
{
   test_subtract_merge(BRW_CONDITIONAL_GE, false);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_z)
{
   test_subtract_merge(BRW_CONDITIONAL_Z, false);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_nz)
{
   test_subtract_merge(BRW_CONDITIONAL_NZ, false);
}

TEST_F(cmod_propagation_test, subtract_immediate_merge_with_compare)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest         = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0         = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   bld.ADD(dest, src0, negative_one);
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_NZ);

   /* = Before =
    * 0: add(8)          dest:F  src0:F  -1.0f
    * 1: cmp.nz.f0(8)    null:F  src0:F  1.0f
    *
    * = After =
    * 0: add.nz.f0(8)    dest:F  src0:F  -1.0f
    */
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest, src0, negative_one)->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, subtract_merge_with_compare_intervening_add)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   bld.ADD(dest0, src0, negative_one);
   bld.ADD(dest1, src0, one);
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest0, src0, negative_one)->conditional_mod = BRW_CONDITIONAL_L;
   exp.ADD(dest1, src0, one);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, subtract_not_merge_with_compare_intervening_partial_write)
{
   brw_builder bld = make_shader();

   brw_reg dest0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dest1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0  = bld.vgrf(BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   bld.ADD(dest0, src0, negative_one);
   set_predicate(BRW_PREDICATE_NORMAL, bld.ADD(dest1, src0, one));
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, subtract_not_merge_with_compare_intervening_add)
{
   brw_builder bld = make_shader();

   brw_reg dest0 = bld.vgrf(BRW_TYPE_F);
   brw_reg dest1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   bld.ADD(dest0, src0, negative_one);
   set_condmod(BRW_CONDITIONAL_EQ, bld.ADD(dest1, src0, one));
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, negative_subtract_merge_with_compare)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg one  = brw_imm_f(1.0f);

   bld.ADD(dest, negate(src0), one);
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   /* The result of the subtract is the negation of the result of the
    * implicit subtract in the compare, so the condition must change.
    */
   exp.ADD(dest, negate(src0), one)->conditional_mod = BRW_CONDITIONAL_G;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, subtract_delete_compare)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest, src0, negative_one));
   set_predicate(BRW_PREDICATE_NORMAL, bld.MOV(dest1, src2));
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add.l.f0(8)     dest0:F src0:F  -1.0:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    * 2: cmp.l.f0(8)     null:F  src0:F  1.0:F
    *
    * = After =
    * 0: add.l.f0(8)     dest:F  src0:F  -1.0:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    */
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   set_condmod(BRW_CONDITIONAL_L, exp.ADD(dest, src0, negative_one));
   set_predicate(BRW_PREDICATE_NORMAL, exp.MOV(dest1, src2));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, subtract_delete_compare_other_flag)
{
   /* This test is the same as subtract_delete_compare but it explicitly used
    * flag f0.1 for the subtraction and the comparison.
    */
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest, src0, negative_one))
      ->flag_subreg = 1;
   set_predicate(BRW_PREDICATE_NORMAL, bld.MOV(dest1, src2));
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L)
      ->flag_subreg = 1;

   /* = Before =
    * 0: add.l.f0.1(8)   dest0:F src0:F  -1.0:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    * 2: cmp.l.f0.1(8)   null:F  src0:F  1.0:F
    *
    * = After =
    * 0: add.l.f0.1(8)   dest:F  src0:F  -1.0:F
    * 1: (+f0) mov(0)    dest1:F src2:F
    */
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   set_condmod(BRW_CONDITIONAL_L, exp.ADD(dest, src0, negative_one))
      ->flag_subreg = 1;
   set_predicate(BRW_PREDICATE_NORMAL, exp.MOV(dest1, src2));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, subtract_to_mismatch_flag)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest, src0, negative_one));
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L)
      ->flag_subreg = 1;

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test,
       subtract_merge_with_compare_intervening_mismatch_flag_write)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   bld.ADD(dest0, src0, negative_one);
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L)
            ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)         dest0:F src0:F  -1.0:F
    * 1: cmp.l.f0.1(8)  null:F  src0:F  1.0:F
    * 2: cmp.l.f0(8)    null:F  src0:F  1.0:F
    *
    * = After =
    * 0: add.l.f0(8)    dest0:F src0:F  -1.0:F
    * 1: cmp.l.f0.1(8)  null:F  src0:F  1.0:F
    *
    * NOTE: Another perfectly valid after sequence would be:
    *
    * 0: add.f0.1(8)    dest0:F src0:F  -1.0:F
    * 1: cmp.l.f0(8)    null:F  src0:F  1.0:F
    *
    * However, the optimization pass starts at the end of the basic block.
    * Because of this, the cmp.l.f0 will always be chosen.  If the pass
    * changes its strategy, this test will also need to change.
    */
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest0, src0, negative_one)->conditional_mod = BRW_CONDITIONAL_L;
   exp.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L)
            ->flag_subreg = 1;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test,
       subtract_merge_with_compare_intervening_mismatch_flag_read)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero  = brw_imm_f(0.0f);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   bld.ADD(dest0, src0, negative_one);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero))
      ->flag_subreg = 1;
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add(8)         dest0:F src0:F  -1.0:F
    * 1: (+f0.1) sel(8) dest1   src2    0.0f
    * 2: cmp.l.f0(8)    null:F  src0:F  1.0:F
    *
    * = After =
    * 0: add.l.f0(8)    dest0:F src0:F  -1.0:F
    * 1: (+f0.1) sel(8) dest1   src2    0.0f
    */
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest0, src0, negative_one)->conditional_mod = BRW_CONDITIONAL_L;
   set_predicate(BRW_PREDICATE_NORMAL, exp.SEL(dest1, src2, zero))
      ->flag_subreg = 1;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, subtract_delete_compare_derp)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg one          = brw_imm_f(1.0f);
   brw_reg negative_one = brw_imm_f(-1.0f);

   set_condmod(BRW_CONDITIONAL_L, bld.ADD(dest0, src0, negative_one));
   set_predicate(BRW_PREDICATE_NORMAL, bld.ADD(dest1, negate(src0), one));
   bld.CMP(bld.null_reg_f(), src0, one, BRW_CONDITIONAL_L);

   /* = Before =
    * 0: add.l.f0(8)     dest0:F src0:F  -1.0:F
    * 1: (+f0) add(0)    dest1:F -src0:F 1.0:F
    * 2: cmp.l.f0(8)     null:F  src0:F  1.0:F
    *
    * = After =
    * 0: add.l.f0(8)     dest0:F src0:F  -1.0:F
    * 1: (+f0) add(0)    dest1:F -src0:F 1.0:F
    */
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   set_condmod(BRW_CONDITIONAL_L, exp.ADD(dest0, src0, negative_one));
   set_predicate(BRW_PREDICATE_NORMAL, exp.ADD(dest1, negate(src0), one));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, signed_unsigned_comparison_mismatch)
{
   brw_builder bld = make_shader();

   brw_reg dest0 = bld.vgrf(BRW_TYPE_D);
   brw_reg src0 = bld.vgrf(BRW_TYPE_D);
   src0.type = BRW_TYPE_W;

   bld.ASR(dest0, negate(src0), brw_imm_d(15));
   bld.CMP(bld.null_reg_ud(), retype(dest0, BRW_TYPE_UD),
           brw_imm_ud(0u), BRW_CONDITIONAL_LE);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, ior_f2i_nz)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_D);
   brw_reg src0 = bld.vgrf(BRW_TYPE_D);
   brw_reg src1 = bld.vgrf(BRW_TYPE_D);

   bld.OR(dest, src0, src1);
   bld.MOV(bld.null_reg_d(), retype(dest, BRW_TYPE_F))
      ->conditional_mod = BRW_CONDITIONAL_NZ;

   /* = Before =
    * 0: or(8)           dest:D  src0:D  src1:D
    * 1: mov.nz(8)       null:D  dest:F
    *
    * = After =
    * No changes.
    *
    * If src0 = 0x30000000 and src1 = 0x0f000000, then the value stored in
    * dest, interpreted as floating point, is 0.5.  This bit pattern is not
    * zero, but after the float-to-integer conversion, the value is zero.
    */
   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, uand_b2f_g)
{
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_UD);
   brw_reg src0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg src1 = bld.vgrf(BRW_TYPE_UD);

   bld.AND(dest, src0, src1);
   bld.MOV(bld.null_reg_f(), negate(retype(dest, BRW_TYPE_D)))
   ->conditional_mod = BRW_CONDITIONAL_G;

   /* = Before =
    * 0: and(8)           dest:UD  src0:UD  src1:UD
    * 1: mov.g(8)         null:F  -dest:D
    *
    * = After =
    * No changes.
    *
    * If src0 and src1 are 0xffffffff, then dest:D will be interpreted as -1,
    * and -dest:D will be 1, which is > 0.
    * If the cmod was propagated (and.l(8) dest:UD  src0:UD  src1:UD),
    * dest:UD can never be < 0.
    *
    */
   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

void
cmod_propagation_test::test_mov_prop(enum brw_conditional_mod cmod,
                                     enum brw_reg_type add_type,
                                     enum brw_reg_type mov_dst_type,
                                     bool expected_cmod_prop_progress)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, add_type);
   brw_reg src0 = vgrf(bld, exp, add_type);
   brw_reg src1 = vgrf(bld, exp, add_type);

   bld.ADD(dest, src0, src1);
   bld.MOV(retype(bld.null_reg_ud(), mov_dst_type), dest)
      ->conditional_mod = cmod;

   EXPECT_PROGRESS_RESULT(expected_cmod_prop_progress,
                          brw_opt_cmod_propagation, bld);

   if (expected_cmod_prop_progress) {
      exp.ADD(dest, src0, src1)->conditional_mod = cmod;

      EXPECT_SHADERS_MATCH(bld, exp);
   }
}

TEST_F(cmod_propagation_test, fadd_fmov_nz)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.nz(8)       null:F  dest:F
    *
    * = After =
    * 0: add.nz(8)       dest:F  src0:F  src1:F
    */
   test_mov_prop(BRW_CONDITIONAL_NZ,
                 BRW_TYPE_F,
                 BRW_TYPE_F,
                 true);
}

TEST_F(cmod_propagation_test, fadd_fmov_z)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.z(8)        null:F  dest:F
    *
    * = After =
    * 0: add.z(8)        dest:F  src0:F  src1:F
    */
   test_mov_prop(BRW_CONDITIONAL_Z,
                 BRW_TYPE_F,
                 BRW_TYPE_F,
                 true);
}

TEST_F(cmod_propagation_test, fadd_fmov_l)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.l(8)        null:F  dest:F
    *
    * = After =
    * 0: add.l(8)        dest:F  src0:F  src1:F
    */
   test_mov_prop(BRW_CONDITIONAL_L,
                 BRW_TYPE_F,
                 BRW_TYPE_F,
                 true);
}

TEST_F(cmod_propagation_test, fadd_fmov_g)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.g(8)        null:F  dest:F
    *
    * = After =
    * 0: add.g(8)        dest:F  src0:F  src1:F
    */
   test_mov_prop(BRW_CONDITIONAL_G,
                 BRW_TYPE_F,
                 BRW_TYPE_F,
                 true);
}

TEST_F(cmod_propagation_test, fadd_fmov_le)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.le(8)       null:F  dest:F
    *
    * = After =
    * 0: add.le(8)        dest:F  src0:F  src1:F
    */
   test_mov_prop(BRW_CONDITIONAL_LE,
                 BRW_TYPE_F,
                 BRW_TYPE_F,
                 true);
}

TEST_F(cmod_propagation_test, fadd_fmov_ge)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.ge(8)       null:F  dest:F
    *
    * = After =
    * 0: add.ge(8)       dest:F  src0:F  src1:F
    */
   test_mov_prop(BRW_CONDITIONAL_GE,
                 BRW_TYPE_F,
                 BRW_TYPE_F,
                 true);
}

TEST_F(cmod_propagation_test, iadd_imov_nz)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.nz(8)       null:D  dest:D
    *
    * = After =
    * 0: add.nz(8)       dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_NZ,
                 BRW_TYPE_D,
                 BRW_TYPE_D,
                 true);
}

TEST_F(cmod_propagation_test, iadd_imov_z)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.z(8)        null:D  dest:D
    *
    * = After =
    * 0: add.z(8)        dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_Z,
                 BRW_TYPE_D,
                 BRW_TYPE_D,
                 true);
}

TEST_F(cmod_propagation_test, iadd_imov_l)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.l(8)        null:D  dest:D
    *
    * = After =
    * 0: add.l(8)        dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_L,
                 BRW_TYPE_D,
                 BRW_TYPE_D,
                 true);
}

TEST_F(cmod_propagation_test, iadd_imov_g)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.g(8)        null:D  dest:D
    *
    * = After =
    * 0: add.g(8)        dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_G,
                 BRW_TYPE_D,
                 BRW_TYPE_D,
                 true);
}

TEST_F(cmod_propagation_test, iadd_imov_le)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.le(8)       null:D  dest:D
    *
    * = After =
    * 0: add.le(8)       dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_LE,
                 BRW_TYPE_D,
                 BRW_TYPE_D,
                 true);
}

TEST_F(cmod_propagation_test, iadd_imov_ge)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.ge(8)       null:D  dest:D
    *
    * = After =
    * 0: add.ge(8)       dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_GE,
                 BRW_TYPE_D,
                 BRW_TYPE_D,
                 true);
}

TEST_F(cmod_propagation_test, iadd_umov_nz)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.nz(8)       null:UD dest:D
    *
    * = After =
    * 0: add.nz(8)       dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_NZ,
                 BRW_TYPE_D,
                 BRW_TYPE_UD,
                 true);
}

TEST_F(cmod_propagation_test, iadd_umov_z)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.z(8)        null:UD dest:D
    *
    * = After =
    * 0: add.z(8)        dest:D  src0:D  src1:D
    */
   test_mov_prop(BRW_CONDITIONAL_Z,
                 BRW_TYPE_D,
                 BRW_TYPE_UD,
                 true);
}

TEST_F(cmod_propagation_test, iadd_umov_l)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.l(8)        null:UD dest:D
    *
    * = After =
    * No changes.
    *
    * Due to the signed-to-usigned type conversion, the conditional modifier
    * cannot be propagated to the ADD without changing at least the
    * destination type of the add.
    *
    * This particular tests is a little silly.  Unsigned less than zero is a
    * contradiction, and earlier optimization passes should have eliminated
    * it.
    */
   test_mov_prop(BRW_CONDITIONAL_L,
                 BRW_TYPE_D,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, iadd_umov_g)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.g(8)        null:UD dest:D
    *
    * = After =
    * No changes.
    *
    * In spite of the type conversion, this could be made to work by
    * propagating NZ instead of G to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_G,
                 BRW_TYPE_D,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, iadd_umov_le)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.le(8)       null:UD dest:D
    *
    * = After =
    * No changes.
    *
    * In spite of the type conversion, this could be made to work by
    * propagating Z instead of LE to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_LE,
                 BRW_TYPE_D,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, iadd_umov_ge)
{
   /* = Before =
    * 0: add(8)          dest:D  src0:D  src1:D
    * 1: mov.ge(8)       null:UD dest:D
    *
    * = After =
    * No changes.
    *
    * Due to the signed-to-usigned type conversion, the conditional modifier
    * cannot be propagated to the ADD without changing at least the
    * destination type of the add.
    *
    * This particular tests is a little silly.  Unsigned greater than or equal
    * to zero is a tautology, and earlier optimization passes should have
    * eliminated it.
    */
   test_mov_prop(BRW_CONDITIONAL_GE,
                 BRW_TYPE_D,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2u_nz)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.nz(8)       null:UD dest:F
    *
    * = After =
    * No changes.  The MOV changes the type from float to unsigned integer.
    * If dest is in the range [-Inf, 1), the conversion will clamp it to zero.
    * If dest is NaN, the conversion will also clamp it to zero.  It is not
    * safe to propagate the NZ back to the ADD.
    *
    * It's tempting to try to propagate G to the ADD in place of the NZ.  This
    * fails for values (0, 1).  For example, if dest is 0.5, add.g would set
    * the flag, but mov.nz would not because the 0.5 would get rounded down to
    * zero.
    */
   test_mov_prop(BRW_CONDITIONAL_NZ,
                 BRW_TYPE_F,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2u_z)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.z(8)        null:UD dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to unsigned integer.  If dest is in
    * the range [-Inf, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the Z back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_Z,
                 BRW_TYPE_F,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2u_l)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.l(8)        null:UD dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to unsigned integer.  If dest is in
    * the range [-Inf, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the L back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_L,
                 BRW_TYPE_F,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2u_g)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.g(8)        null:UD dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to unsigned integer.  If dest is in
    * the range [-Inf, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the G back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_G,
                 BRW_TYPE_F,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2u_le)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.le(8)       null:UD dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to unsigned integer.  If dest is in
    * the range [-Inf, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the LE back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_LE,
                 BRW_TYPE_F,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2u_ge)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.ge(8)       null:UD dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to unsigned integer.  If dest is in
    * the range [-Inf, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the GE back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_GE,
                 BRW_TYPE_F,
                 BRW_TYPE_UD,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2i_nz)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.nz(8)       null:D  dest:F
    *
    * = After =
    * No changes.  The MOV changes the type from float to signed integer.  If
    * dest is in the range (-1, 1), the conversion will clamp it to zero.  If
    * dest is NaN, the conversion will also clamp it to zero.  It is not safe
    * to propagate the NZ back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_NZ,
                 BRW_TYPE_F,
                 BRW_TYPE_D,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2i_z)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.z(8)        null:D  dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to signed integer.  If dest is in
    * the range (-1, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the Z back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_Z,
                 BRW_TYPE_F,
                 BRW_TYPE_D,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2i_l)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.l(8)        null:D  dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to signed integer.  If dest is in
    * the range (-1, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the L back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_L,
                 BRW_TYPE_F,
                 BRW_TYPE_D,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2i_g)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.g(8)        null:D  dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to signed integer.  If dest is in
    * the range (-1, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the G back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_G,
                 BRW_TYPE_F,
                 BRW_TYPE_D,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2i_le)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.le(8)       null:D  dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to signed integer.  If dest is in
    * the range (-1, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the LE back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_LE,
                 BRW_TYPE_F,
                 BRW_TYPE_D,
                 false);
}

TEST_F(cmod_propagation_test, fadd_f2i_ge)
{
   /* = Before =
    * 0: add(8)          dest:F  src0:F  src1:F
    * 1: mov.ge(8)       null:D  dest:F
    *
    * = After =
    * No changes.
    *
    * The MOV changes the type from float to signed integer.  If dest is in
    * the range (-1, 1), the conversion will clamp it to zero.  If dest is
    * NaN, the conversion will also clamp it to zero.  It is not safe to
    * propagate the GE back to the ADD.
    */
   test_mov_prop(BRW_CONDITIONAL_GE,
                 BRW_TYPE_F,
                 BRW_TYPE_D,
                 false);
}

void
cmod_propagation_test::test_saturate_prop(enum brw_conditional_mod before,
                                          enum opcode op,
                                          enum brw_reg_type add_type,
                                          enum brw_reg_type op_type,
                                          bool expected_cmod_prop_progress)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, add_type);
   brw_reg src0 = vgrf(bld, exp, add_type);
   brw_reg src1 = vgrf(bld, exp, add_type);
   brw_reg zero = brw_imm_ud(0);

   bld.ADD(dest, src0, src1)->saturate = true;

   assert(op == BRW_OPCODE_CMP || op == BRW_OPCODE_MOV);
   if (op == BRW_OPCODE_CMP) {
      bld.CMP(bld.vgrf(op_type, 0),
              retype(dest, op_type),
              retype(zero, op_type),
              before);
   } else {
      bld.MOV(bld.vgrf(op_type, 0), retype(dest, op_type))
         ->conditional_mod = before;
   }

   EXPECT_PROGRESS_RESULT(expected_cmod_prop_progress,
                          brw_opt_cmod_propagation, bld);

   if (expected_cmod_prop_progress) {
      brw_inst *add = exp.ADD(dest, src0, src1);
      add->saturate = true;
      add->conditional_mod = before;

      EXPECT_SHADERS_MATCH(bld, exp);
   }
}

TEST_F(cmod_propagation_test, float_saturate_nz_cmp)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.nz.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.sat.nz.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_NZ, BRW_OPCODE_CMP,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_nz_mov)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.nz.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.nz.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_NZ, BRW_OPCODE_MOV,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_z_cmp)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.z.f0(8)   null  dest  0.0f
    *
    * = After =
    * 0: add.sat.z.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_CMP,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_z_mov)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.z.f0(8)   null  dest
    *
    * = After =
    * 0: add.sat.z.f0(8) dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_MOV,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_g_cmp)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.g.f0(8)   null  dest  0.0f
    *
    * = After =
    * 0: add.sat.g.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_G, BRW_OPCODE_CMP,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_g_mov)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.g.f0(8)   null  dest
    *
    * = After =
    * 0: add.sat.g.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_G, BRW_OPCODE_MOV,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_le_cmp)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.le.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.sat.le.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_LE, BRW_OPCODE_CMP,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_le_mov)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].  (sat(x) <= 0) == (x <= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.le.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.le.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_LE, BRW_OPCODE_MOV,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_l_cmp)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.l.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.sat.l.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_CMP,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_l_mov)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.l.f0(8)   null  dest
    *
    * = After =
    * 0: add.sat.l.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_MOV,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_ge_cmp)
{
   /* With the saturate modifier, the comparison happens after clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  0.0f
    *
    * = After =
    * 0: add.sat.ge.f0(8)  dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_CMP,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, float_saturate_ge_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.ge.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.ge.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_MOV,
                      BRW_TYPE_F, BRW_TYPE_F,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_nz_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.nz.f0(8)  null  dest  0
    *
    * = After =
    * 0: add.sat.nz.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_NZ, BRW_OPCODE_CMP,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, uint_saturate_nz_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest:UD  src0:UD  src1:UD
    * 1: cmp.nz.f0(8)  null:D   dest:D   0
    *
    * = After =
    * 0: add.sat.nz.f0(8)    dest:UD  src0:UD  src1:UD
    */
   test_saturate_prop(BRW_CONDITIONAL_NZ, BRW_OPCODE_CMP,
                      BRW_TYPE_UD, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_nz_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.nz.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.nz.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_NZ, BRW_OPCODE_MOV,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_z_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.z.f0(8)   null  dest  0
    *
    * = After =
    * 0: add.sat.z.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_CMP,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, uint_saturate_z_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)   dest:UD  src0:UD  src1:UD
    * 1: cmp.z.f0(8)  null:D   dest:D   0
    *
    * = After =
    * 0: add.sat.z.f0(8)    dest:UD  src0:UD  src1:UD
    */
   test_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_CMP,
                      BRW_TYPE_UD, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_z_mov)
{
   /* With the saturate modifier, the comparison happens before clamping to
    * [0, 1].  (sat(x) == 0) == (x <= 0).
    *
    * = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.z.f0(8)   null  dest
    *
    * = After =
    * 0: add.sat.z.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_Z, BRW_OPCODE_MOV,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_g_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.g.f0(8)   null  dest  0
    *
    * = After =
    * 0: add.sat.g.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_G, BRW_OPCODE_CMP,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_g_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.g.f0(8)   null  dest
    *
    * = After =
    * 0: add.sat.g.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_G, BRW_OPCODE_MOV,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_le_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.le.f0(8)  null  dest  0
    *
    * = After =
    * 0: add.sat.le.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_LE, BRW_OPCODE_CMP,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_le_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.le.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.le.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_LE, BRW_OPCODE_MOV,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_l_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.l.f0(8)  null  dest  0
    *
    * = After =
    * 0: add.sat.l.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_CMP,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_l_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.l.f0(8)  null  dest  0
    *
    * = After =
    * 0: add.sat.l.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_L, BRW_OPCODE_MOV,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_ge_cmp)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: cmp.ge.f0(8)  null  dest  0
    *
    * = After =
    * 0: add.sat.ge.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_CMP,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, int_saturate_ge_mov)
{
   /* = Before =
    *
    * 0: add.sat(8)    dest  src0  src1
    * 1: mov.ge.f0(8)  null  dest
    *
    * = After =
    * 0: add.sat.ge.f0(8)    dest  src0  src1
    */
   test_saturate_prop(BRW_CONDITIONAL_GE, BRW_OPCODE_MOV,
                      BRW_TYPE_D, BRW_TYPE_D,
                      true);
}

TEST_F(cmod_propagation_test, not_to_or)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_UD);

   bld.OR(dest, src0, src1);
   bld.NOT(bld.null_reg_ud(), dest)->conditional_mod = BRW_CONDITIONAL_NZ;

   /* = Before =
    *
    * 0: or(8)         dest  src0  src1
    * 1: not.nz.f0(8)  null  dest
    *
    * = After =
    * 0: or.z.f0(8)    dest  src0  src1
    */

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
   EXPECT_PROGRESS(brw_opt_algebraic, bld);
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.OR(dest, src0, src1)->conditional_mod = BRW_CONDITIONAL_Z;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, not_to_and)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_UD);

   bld.AND(dest, src0, src1);
   bld.NOT(bld.null_reg_ud(), dest)->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
   EXPECT_PROGRESS(brw_opt_algebraic, bld);
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.AND(dest, src0, src1)->conditional_mod = BRW_CONDITIONAL_Z;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, not_to_uadd)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    *
    * The optimization pass currently restricts to just OR and AND.  It's
    * possible that this is too restrictive, and the actual, necessary
    * restriction is just the the destination type of the ALU instruction is
    * the same as the source type of the NOT instruction.
    */
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_UD);
   brw_reg src0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg src1 = bld.vgrf(BRW_TYPE_UD);

   bld.ADD(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, not_to_fadd_to_ud)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    *
    * The optimization pass currently restricts to just OR and AND.  It's
    * possible that this is too restrictive, and the actual, necessary
    * restriction is just the the destination type of the ALU instruction is
    * the same as the source type of the NOT instruction.
    */
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_UD);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg src1 = bld.vgrf(BRW_TYPE_F);

   bld.ADD(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, not_to_fadd)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    *
    * The optimization pass currently restricts to just OR and AND.  It's
    * possible that this is too restrictive, and the actual, necessary
    * restriction is just the the destination type of the ALU instruction is
    * the same as the source type of the NOT instruction.
    */
   brw_builder bld = make_shader();

   brw_reg dest = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_F);
   brw_reg src1 = bld.vgrf(BRW_TYPE_F);

   bld.ADD(dest, src0, src1);
   set_condmod(BRW_CONDITIONAL_NZ,
               bld.NOT(bld.null_reg_ud(),
                       retype(dest, BRW_TYPE_UD)));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_flag_read_compatible_value)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1  = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero  = brw_imm_f(0.0f);

   set_condmod(BRW_CONDITIONAL_Z,      bld.OR(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   set_condmod(BRW_CONDITIONAL_NZ,     bld.NOT(bld.null_reg_ud(), dest0));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
   EXPECT_PROGRESS(brw_opt_algebraic, bld);
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   set_condmod(BRW_CONDITIONAL_Z,      exp.OR(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, exp.SEL(dest1, src2, zero));

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test,
       not_to_or_intervening_flag_read_compatible_value_mismatch_flag)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */

   brw_builder bld = make_shader();

   brw_reg dest0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg dest1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg src1 = bld.vgrf(BRW_TYPE_UD);
   brw_reg src2 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0.0f));

   set_condmod(BRW_CONDITIONAL_Z, bld.OR(dest0, src0, src1))
      ->flag_subreg = 1;
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_flag_read_incompatible_value)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */

   brw_builder bld = make_shader();

   brw_reg dest0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg dest1 = bld.vgrf(BRW_TYPE_F);
   brw_reg src0 = bld.vgrf(BRW_TYPE_UD);
   brw_reg src1 = bld.vgrf(BRW_TYPE_UD);
   brw_reg src2 = bld.vgrf(BRW_TYPE_F);
   brw_reg zero(brw_imm_f(0.0f));

   set_condmod(BRW_CONDITIONAL_NZ, bld.OR(dest0, src0, src1));
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero));
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_mismatch_flag_write)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1  = vgrf(bld, exp, BRW_TYPE_UD);

   bld.OR(dest0, src0, src1);
   set_condmod(BRW_CONDITIONAL_Z, bld.OR(dest1, src0, src1))
      ->flag_subreg = 1;
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
   EXPECT_PROGRESS(brw_opt_algebraic, bld);
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   set_condmod(BRW_CONDITIONAL_Z, exp.OR(dest0, src0, src1));
   set_condmod(BRW_CONDITIONAL_Z, exp.OR(dest1, src0, src1))
      ->flag_subreg = 1;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, not_to_or_intervening_mismatch_flag_read)
{
   /* Exercise propagation of conditional modifier from a NOT instruction to
    * another ALU instruction as performed by cmod_propagate_not.
    */

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1  = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero  = brw_imm_f(0.0f);

   bld.OR(dest0, src0, src1);
   set_predicate(BRW_PREDICATE_NORMAL, bld.SEL(dest1, src2, zero))
      ->flag_subreg = 1;
   set_condmod(BRW_CONDITIONAL_NZ, bld.NOT(bld.null_reg_ud(), dest0));

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
   EXPECT_PROGRESS(brw_opt_algebraic, bld);
   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.OR(dest0, src0, src1)->conditional_mod = BRW_CONDITIONAL_Z;
   set_predicate(BRW_PREDICATE_NORMAL, exp.SEL(dest1, src2, zero))
      ->flag_subreg = 1;

   EXPECT_SHADERS_MATCH(bld, exp);
}

void
cmod_propagation_test::test_cmp_to_add_sat(enum brw_conditional_mod before,
                                           bool add_negative_src0,
                                           bool add_negative_constant,
                                           bool expected_cmod_prop_progress)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg neg = brw_imm_f(-0.5f);
   brw_reg pos = brw_imm_f(0.5f);

   bld.ADD(dest,
           add_negative_src0 ? negate(src0) : src0,
           add_negative_constant ? neg : pos)
      ->saturate = true;

   /* The parity of negations between the ADD and the CMP must be
    * different. Otherwise the ADD and the CMP aren't performing the same
    * arithmetic, and the optimization won't trigger.
    */
   const bool cmp_negative_constant =
      add_negative_constant == add_negative_src0;

   bld.CMP(bld.null_reg_f(),
           src0,
           cmp_negative_constant ? neg : pos,
           before);

   EXPECT_PROGRESS_RESULT(expected_cmod_prop_progress,
                          brw_opt_cmod_propagation, bld);

   if (expected_cmod_prop_progress) {
      const enum brw_conditional_mod after =
         add_negative_src0 ? brw_swap_cmod(before) : before;

      brw_inst *add = exp.ADD(dest,
                              add_negative_src0 ? negate(src0) : src0,
                              add_negative_constant ? neg : pos);
      add->saturate = true;
      add->conditional_mod = after;

      EXPECT_SHADERS_MATCH(bld, exp);
   }
}

TEST_F(cmod_propagation_test, cmp_g_to_add_src0_neg_constant)
{
   /* This works even if src0 is NaN. (NaN > 0.5) is false, and (0.0 > 0.5) is
    * false.
    */
   test_cmp_to_add_sat(BRW_CONDITIONAL_G, false, true, true);
}

TEST_F(cmod_propagation_test, cmp_g_to_add_src0_pos_constant)
{
   /* This fails if src0 is NaN. (NaN > -0.5) is false, but (0.0 > -0.5) is
    * true.
    */
   test_cmp_to_add_sat(BRW_CONDITIONAL_G, false, false, false);
}

TEST_F(cmod_propagation_test, cmp_g_to_add_neg_src0_neg_constant)
{
   /* This is effectively the same as cmp_l_to_add_src0_neg_constant. */
   test_cmp_to_add_sat(BRW_CONDITIONAL_G, true, true, false);
}

TEST_F(cmod_propagation_test, cmp_g_to_add_neg_src0_pos_constant)
{
   /* This is effectively the same as cmp_l_to_add_src0_pos_constant. */
   test_cmp_to_add_sat(BRW_CONDITIONAL_G, true, false, false);
}

TEST_F(cmod_propagation_test, cmp_l_to_add_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_L, false, true, false);
}

TEST_F(cmod_propagation_test, cmp_l_to_add_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_L, false, false, false);
}

TEST_F(cmod_propagation_test, cmp_l_to_add_neg_src0_neg_constant)
{
   /* This is effectively the same as cmp_g_to_add_src0_neg_constant. */
   test_cmp_to_add_sat(BRW_CONDITIONAL_L, true, true, true);
}

TEST_F(cmod_propagation_test, cmp_l_to_add_neg_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_L, true, false, false);
}

TEST_F(cmod_propagation_test, cmp_le_to_add_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_LE, false, true, false);
}

TEST_F(cmod_propagation_test, cmp_le_to_add_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_LE, false, false, false);
}

TEST_F(cmod_propagation_test, cmp_le_to_add_neg_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_LE, true, true, false);
}

TEST_F(cmod_propagation_test, cmp_le_to_add_neg_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_LE, true, false, false);
}

TEST_F(cmod_propagation_test, cmp_ge_to_add_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_GE, false, true, false);
}

TEST_F(cmod_propagation_test, cmp_ge_to_add_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_GE, false, false, false);
}

TEST_F(cmod_propagation_test, cmp_ge_to_add_neg_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_GE, true, true, false);
}

TEST_F(cmod_propagation_test, cmp_ge_to_add_neg_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_GE, true, false, false);
}

TEST_F(cmod_propagation_test, cmp_nz_to_add_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_NZ, false, true, false);
}

TEST_F(cmod_propagation_test, cmp_nz_to_add_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_NZ, false, false, false);
}

TEST_F(cmod_propagation_test, cmp_nz_to_add_neg_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_NZ, true, true, false);
}

TEST_F(cmod_propagation_test, cmp_nz_to_add_neg_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_NZ, true, false, false);
}

TEST_F(cmod_propagation_test, cmp_z_to_add_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_Z, false, true, false);
}

TEST_F(cmod_propagation_test, cmp_z_to_add_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_Z, false, false, false);
}

TEST_F(cmod_propagation_test, cmp_z_to_add_neg_src0_neg_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_Z, true, true, false);
}

TEST_F(cmod_propagation_test, cmp_z_to_add_neg_src0_pos_constant)
{
   test_cmp_to_add_sat(BRW_CONDITIONAL_Z, true, false, false);
}

TEST_F(cmod_propagation_test, prop_across_sel)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg dest2 = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src1  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src2  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg src3  = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg zero  = brw_imm_f(0.0f);

   bld.ADD(dest1, src0, src1);
   bld.emit_minmax(dest2, src2, src3, BRW_CONDITIONAL_GE);
   bld.CMP(bld.null_reg_f(), dest1, zero, BRW_CONDITIONAL_GE);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.ADD(dest1, src0, src1)->conditional_mod = BRW_CONDITIONAL_GE;
   exp.SEL(dest2, src2, src3)->conditional_mod = BRW_CONDITIONAL_GE;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, Boolean_size_conversion)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_W);
   brw_reg src0  = vgrf(bld, exp, BRW_TYPE_W);
   brw_reg zero  = brw_imm_w(0);

   bld.CMP(dest1, src0, zero, BRW_CONDITIONAL_NZ);
   bld.MOV(bld.null_reg_d(), dest1)->conditional_mod = BRW_CONDITIONAL_NZ;

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.CMP(dest1, src0, zero, BRW_CONDITIONAL_NZ);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, BFN_CMP_Z)
{
   // BFN doesn't exist on earlier platforms.
   set_gfx_verx10(125);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg zero  = brw_imm_ud(0);

   bld.BFN(dest1, src0, src1, src2, 0xfe);
   bld.CMP(bld.null_reg_d(), dest1, zero, BRW_CONDITIONAL_Z);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_Z;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, BFN_Z_CMP_Z)
{
   // BFN doesn't exist on earlier platforms.
   set_gfx_verx10(125);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg zero  = brw_imm_ud(0);

   bld.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_Z;
   bld.CMP(bld.null_reg_d(), dest1, zero, BRW_CONDITIONAL_Z);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_Z;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, BFN_CMP_NZ)
{
   // BFN doesn't exist on earlier platforms.
   set_gfx_verx10(125);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg zero  = brw_imm_ud(0);

   bld.BFN(dest1, src0, src1, src2, 0xfe);
   bld.CMP(bld.null_reg_d(), dest1, zero, BRW_CONDITIONAL_NZ);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_G;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, BFN_CMP_D_NZ)
{
   // BFN doesn't exist on earlier platforms.
   set_gfx_verx10(125);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg zero  = brw_imm_ud(0);

   bld.BFN(dest1, src0, src1, src2, 0xfe);
   bld.CMP(bld.null_reg_d(), dest1, zero, BRW_CONDITIONAL_NZ);

   EXPECT_NO_PROGRESS(brw_opt_cmod_propagation, bld);
}

TEST_F(cmod_propagation_test, BFN_G_CMP_NZ)
{
   // BFN doesn't exist on earlier platforms.
   set_gfx_verx10(125);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg zero  = brw_imm_ud(0);

   bld.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_G;
   bld.CMP(bld.null_reg_d(), dest1, zero, BRW_CONDITIONAL_NZ);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_G;

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(cmod_propagation_test, BFN_G_CMP_D_G)
{
   // BFN doesn't exist on earlier platforms.
   set_gfx_verx10(125);

   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg dest1 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src0 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg src1 = vgrf(bld, exp, BRW_TYPE_UD);
   brw_reg src2 = vgrf(bld, exp, BRW_TYPE_D);
   brw_reg zero  = brw_imm_ud(0);

   bld.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_G;
   bld.CMP(bld.null_reg_d(), dest1, zero, BRW_CONDITIONAL_G);

   EXPECT_PROGRESS(brw_opt_cmod_propagation, bld);

   exp.BFN(dest1, src0, src1, src2, 0xfe)
      ->conditional_mod = BRW_CONDITIONAL_G;

   EXPECT_SHADERS_MATCH(bld, exp);
}
