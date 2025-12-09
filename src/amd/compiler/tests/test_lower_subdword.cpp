/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "helpers.h"
#include <stdarg.h>

using namespace aco;

BEGIN_TEST(lower_subdword.simple_extract)
   //>> v1: %a:v[0], v2: %b:v[1-2] = p_startpgm
   if (!setup_cs("v1 v2", GFX6))
      return;

   //! v1: %aw0 = p_parallelcopy %a
   bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), inputs[0], Operand::c32(0));

   //! v1: %aw1 = v_lshrrev_b32 16, %a
   bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), inputs[0], Operand::c32(1));

   //! v1: %ab3 = v_lshrrev_b32 24, %a
   bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), inputs[0], Operand::c32(3));

   //! v1: %_,  v1: %bd1 = p_split_vector %b
   //! v1: %6 = v_lshrrev_b32 16, %bd1
   bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), inputs[1], Operand::c32(3));

   //! v1: %bd0,  v1: %_ = p_split_vector %b
   //! v1: %bb2 = v_lshrrev_b32 16, %bd0
   bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), inputs[1], Operand::c32(2));

   //! v1: %bd0_2,  v1: %bd1_2 = p_split_vector %b
   //! v1: %bv3bhi = v_alignbyte_b32 %bd1_2, %bd0_2, 3
   bld.pseudo(aco_opcode::p_extract_vector, bld.def(v3b), inputs[1], Operand::c32(1));

   finish_lower_subdword_test();
END_TEST

BEGIN_TEST(lower_subdword.simple_split)
   //>> v1: %a:v[0], v2: %b:v[1-2] = p_startpgm
   if (!setup_cs("v1 v2", GFX6))
      return;

   //! v1: %o1_0 = p_parallelcopy %a
   //! v1: %o1_1 = v_lshrrev_b32 8, %a
   //! v1: %o1_2 = v_lshrrev_b32 24, %a
   bld.pseudo(aco_opcode::p_split_vector, bld.def(v1b), bld.def(v2b), bld.def(v1b), inputs[0]);

   //! v1: %tmp2_0,  v1: %_ = p_split_vector %b
   //! v1: %o2_0 = p_parallelcopy %tmp2_0
   //! v1: %tmp2_1,  v1: %_ = p_split_vector %b
   //! v1: %o2_1 = v_lshrrev_b32 8, %tmp2_1
   //! v1: %tmp2_2,  v1: %_ = p_split_vector %b
   //! v1: %o2_2 = v_lshrrev_b32 16, %tmp2_2
   //! v1: %tmp2_3,  v1: %_ = p_split_vector %b
   //! v1: %o2_3 = v_lshrrev_b32 24, %tmp2_3
   //! v1: %_,  v1: %tmp2_4 = p_split_vector %b
   //! v1: %o2_4 = p_parallelcopy %tmp2_4
   //! v1: %_,  v1: %tmp2_5 = p_split_vector %b
   //! v1: %o2_5 = v_lshrrev_b32 8, %tmp2_5
   //! v1: %_,  v1: %tmp2_6 = p_split_vector %b
   //! v1: %o2_6 = v_lshrrev_b32 16, %tmp2_6
   //! v1: %_,  v1: %tmp2_7 = p_split_vector %b
   //! v1: %o2_7 = v_lshrrev_b32 24, %tmp2_7
   bld.pseudo(aco_opcode::p_split_vector, bld.def(v1b), bld.def(v1b), bld.def(v1b), bld.def(v1b),
              bld.def(v1b), bld.def(v1b), bld.def(v1b), bld.def(v1b), inputs[1]);

   finish_lower_subdword_test();
END_TEST

BEGIN_TEST(lower_subdword.simple_create)
   //>> v1: %b:v[0],  v1: %w:v[1],  v1: %tb:v[2],  s1: %sd:s[0],  v1: %vd:v[3] = p_startpgm
   if (!setup_cs("v1b v2b v3b s1 v1 ", GFX6))
      return;

   //! v1: %tmp1_0 = v_lshlrev_b32 24, %b
   //! v1: %tmp1_1 = v_alignbyte_b32 %b, %tmp1_0, 1
   //! v1: %tmp1_2 = v_alignbyte_b32 %b, %tmp1_1, 1
   //! v1: %o1 = v_alignbyte_b32 %b, %tmp1_2, 1
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), inputs[0], inputs[0], inputs[0], inputs[0]);

   //! v1: %tmp2_0 = v_lshlrev_b32 24, %b
   //! v1: %tmp2_1 = v_alignbyte_b32 %w, %tmp2_0, 2
   //! v1: %o2 = v_alignbyte_b32 %b, %tmp2_1, 1
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), inputs[0], inputs[1], inputs[0]);

   //! v1: %tmp3_0 = v_lshlrev_b32 24, %b
   //! v1: %tmp3_1 = v_alignbyte_b32 %tb, %tmp3_0, 3
   //! v1: %tmp3_2 = v_lshlrev_b32 16, %w
   //! v1: %tmp3_3 = v_alignbyte_b32 %b, %tmp3_2, 1
   //! v1: %tmp3_4 = v_alignbyte_b32 %b, %tmp3_3, 1
   //! v2: %o3 = p_create_vector %tmp3_1, %tmp3_4
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), inputs[0], inputs[2], inputs[1], inputs[0],
              inputs[0]);

   //! v1: %tmp4_0 = p_parallelcopy %sd
   //! v1: %tmp4_1 = p_parallelcopy %vd
   //! v1: %tmp4_2 = v_lshlrev_b32 16, %w
   //! v1: %tmp4_3 = v_alignbyte_b32 %tb, %tmp4_2, 2
   //! v1: %tmp4_4 = v_lshlrev_b32 8, %tb
   //! v1: %tmp4_5 = v_alignbyte_b32 %tb, %tmp4_4, 3
   //! v4: %o4 = p_create_vector %tmp4_0, %tmp4_1, %tmp4_3, %tmp4_5
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v4), inputs[3], inputs[4], inputs[1], inputs[2],
              inputs[2]);

   //! v1: %tmp5_0 = v_lshlrev_b32 24, %b
   //! v1: %o5 = v_alignbyte_b32 %b, %tmp5_0, 3
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v2b), inputs[0], inputs[0]);

   finish_lower_subdword_test();
END_TEST

BEGIN_TEST(lower_subdword.create_const_undef)
   //>> v1: %b:v[0],  v1: %w:v[1] = p_startpgm
   if (!setup_cs("v1b v2b", GFX6))
      return;

   //! v1: %o1 = p_parallelcopy %b
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), inputs[0], Operand(v1b), Operand(v2b));

   //! v1: %o2 = p_parallelcopy 0x40300
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v3b), Operand::c8(0), Operand::c8(3),
              Operand::c8(4));

   //! s1: %tmp1 = p_parallelcopy 0x64000000
   //! v1: %o3 = v_alignbyte_b32 %w, %tmp1, 3
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v1), Operand::c8(100), inputs[1], Operand(v1b));

   //! s1: %tmp2 = p_parallelcopy 0x64000000
   //! v1: %tmp3 = v_alignbyte_b32 %b, %tmp2, 2
   //! v1: %tmp4 = v_alignbyte_b32 %b, %tmp3, 1
   //! v1: %tmp5 = p_parallelcopy %w
   //! v2: %o4 = p_create_vector %tmp4, %tmp5
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v6b), Operand::c8(100), inputs[0], Operand(v1b),
              inputs[0], inputs[1]);

   //! v1: %o5 = v_alignbyte_b32 %b, 0, 3
   bld.pseudo(aco_opcode::p_create_vector, bld.def(v2b), Operand(v1b), inputs[0]);

   finish_lower_subdword_test();
END_TEST
