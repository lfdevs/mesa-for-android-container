/*
 * Copyright © 2020 Valve Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */
#include "helpers.h"

using namespace aco;

BEGIN_TEST(to_hw_instr.swap_subdword)
   PhysReg v0_lo{256};
   PhysReg v0_hi{256};
   PhysReg v0_b1{256};
   PhysReg v0_b3{256};
   PhysReg v1_lo{257};
   PhysReg v1_hi{257};
   PhysReg v1_b1{257};
   PhysReg v1_b3{257};
   PhysReg v2_lo{258};
   PhysReg v3_lo{259};
   v0_hi.reg_b += 2;
   v1_hi.reg_b += 2;
   v0_b1.reg_b += 1;
   v1_b1.reg_b += 1;
   v0_b3.reg_b += 3;
   v1_b3.reg_b += 3;

   for (unsigned i = GFX6; i <= GFX7; i++) {
      if (!setup_cs(NULL, (chip_class)i))
         continue;

      //~gfx[67]>>  p_unit_test 0
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      bld.pseudo(aco_opcode::p_unit_test, Operand(0u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v1_lo, v2b),
                 Operand(v1_lo, v2b), Operand(v0_lo, v2b));

      //~gfx[67]! p_unit_test 1
      //~gfx[67]! v2b: %0:v[0][16:32] = v_lshlrev_b32 16, %0:v[0][0:16]
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[1][0:16], %0:v[0][16:32], 2
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[0][0:16], %0:v[0][16:32], 2
      bld.pseudo(aco_opcode::p_unit_test, Operand(1u));
      bld.pseudo(aco_opcode::p_create_vector,
                 Definition(v0_lo, v1),
                 Operand(v1_lo, v2b), Operand(v0_lo, v2b));

      //~gfx[67]! p_unit_test 2
      //~gfx[67]! v2b: %0:v[0][16:32] = v_lshlrev_b32 16, %0:v[0][0:16]
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[1][0:16], %0:v[0][16:32], 2
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[0][0:16], %0:v[0][16:32], 2
      //~gfx[67]! v2b: %0:v[1][0:16] = v_mov_b32 %0:v[2][0:16]
      bld.pseudo(aco_opcode::p_unit_test, Operand(2u));
      bld.pseudo(aco_opcode::p_create_vector,
                 Definition(v0_lo, v6b), Operand(v1_lo, v2b),
                 Operand(v0_lo, v2b), Operand(v2_lo, v2b));

      //~gfx[67]! p_unit_test 3
      //~gfx[67]! v2b: %0:v[0][16:32] = v_lshlrev_b32 16, %0:v[0][0:16]
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[1][0:16], %0:v[0][16:32], 2
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[0][0:16], %0:v[0][16:32], 2
      //~gfx[67]! v2b: %0:v[1][16:32] = v_lshlrev_b32 16, %0:v[2][0:16]
      //~gfx[67]! v1: %0:v[1] = v_alignbyte_b32 %0:v[3][0:16], %0:v[1][16:32], 2
      bld.pseudo(aco_opcode::p_unit_test, Operand(3u));
      bld.pseudo(aco_opcode::p_create_vector,
                 Definition(v0_lo, v2),
                 Operand(v1_lo, v2b), Operand(v0_lo, v2b),
                 Operand(v2_lo, v2b), Operand(v3_lo, v2b));

      //~gfx[67]! p_unit_test 4
      //~gfx[67]! v2b: %0:v[1][16:32] = v_lshlrev_b32 16, %0:v[1][0:16]
      //~gfx[67]! v1: %0:v[1] = v_alignbyte_b32 %0:v[2][0:16], %0:v[1][16:32], 2
      //~gfx[67]! v2b: %0:v[0][16:32] = v_lshlrev_b32 16, %0:v[0][0:16]
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[3][0:16], %0:v[0][16:32], 2
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      bld.pseudo(aco_opcode::p_unit_test, Operand(4u));
      bld.pseudo(aco_opcode::p_create_vector,
                 Definition(v0_lo, v2),
                 Operand(v1_lo, v2b), Operand(v2_lo, v2b),
                 Operand(v0_lo, v2b), Operand(v3_lo, v2b));

      //~gfx[67]! p_unit_test 5
      //~gfx[67]! v2b: %0:v[1][0:16] = v_mov_b32 %0:v[0][0:16]
      //~gfx[67]! v2b: %0:v[0][0:16] = v_lshrrev_b32 16, %0:v[1][16:32]
      bld.pseudo(aco_opcode::p_unit_test, Operand(5u));
      bld.pseudo(aco_opcode::p_split_vector,
                 Definition(v1_lo, v2b), Definition(v0_lo, v2b),
                 Operand(v0_lo, v1));

      //~gfx[67]! p_unit_test 6
      //~gfx[67]! v2b: %0:v[2][0:16] = v_mov_b32 %0:v[1][0:16]
      //~gfx[67]! v2b: %0:v[1][0:16] = v_mov_b32 %0:v[0][0:16]
      //~gfx[67]! v2b: %0:v[0][0:16] = v_lshrrev_b32 16, %0:v[1][16:32]
      bld.pseudo(aco_opcode::p_unit_test, Operand(6u));
      bld.pseudo(aco_opcode::p_split_vector,
                 Definition(v1_lo, v2b), Definition(v0_lo, v2b),
                 Definition(v2_lo, v2b), Operand(v0_lo, v6b));

      //~gfx[67]! p_unit_test 7
      //~gfx[67]! v2b: %0:v[2][0:16] = v_mov_b32 %0:v[1][0:16]
      //~gfx[67]! v2b: %0:v[1][0:16] = v_mov_b32 %0:v[0][0:16]
      //~gfx[67]! v2b: %0:v[0][0:16] = v_lshrrev_b32 16, %0:v[1][16:32]
      //~gfx[67]! v2b: %0:v[3][0:16] = v_lshrrev_b32 16, %0:v[2][16:32]
      bld.pseudo(aco_opcode::p_unit_test, Operand(7u));
      bld.pseudo(aco_opcode::p_split_vector,
                 Definition(v1_lo, v2b), Definition(v0_lo, v2b),
                 Definition(v2_lo, v2b), Definition(v3_lo, v2b),
                 Operand(v0_lo, v2));

      //~gfx[67]! p_unit_test 8
      //~gfx[67]! v2b: %0:v[2][0:16] = v_lshrrev_b32 16, %0:v[0][16:32]
      //~gfx[67]! v2b: %0:v[3][0:16] = v_lshrrev_b32 16, %0:v[1][16:32]
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      bld.pseudo(aco_opcode::p_unit_test, Operand(8u));
      bld.pseudo(aco_opcode::p_split_vector,
                 Definition(v1_lo, v2b), Definition(v2_lo, v2b),
                 Definition(v0_lo, v2b), Definition(v3_lo, v2b),
                 Operand(v0_lo, v2));

      //~gfx[67]! p_unit_test 9
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx[67]! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      bld.pseudo(aco_opcode::p_unit_test, Operand(9u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1b), Definition(v1_lo, v1b),
                 Operand(v1_lo, v1b), Operand(v0_lo, v1b));

      //~gfx[67]! p_unit_test 10
      //~gfx[67]! v1b: %0:v[1][24:32] = v_lshlrev_b32 24, %0:v[1][0:8]
      //~gfx[67]! v2b: %0:v[1][0:16] = v_alignbyte_b32 %0:v[0][0:8], %0:v[1][24:32], 3
      //~gfx[67]! v2b: %0:v[0][0:16] = v_mov_b32 %0:v[1][0:16]
      bld.pseudo(aco_opcode::p_unit_test, Operand(10u));
      bld.pseudo(aco_opcode::p_create_vector,
                 Definition(v0_lo, v2b),
                 Operand(v1_lo, v1b), Operand(v0_lo, v1b));

      //~gfx[67]! p_unit_test 11
      //~gfx[67]! v1b: %0:v[1][24:32] = v_lshlrev_b32 24, %0:v[1][0:8]
      //~gfx[67]! v2b: %0:v[1][0:16] = v_alignbyte_b32 %0:v[0][0:8], %0:v[1][24:32], 3
      //~gfx[67]! v2b: %0:v[0][0:16] = v_mov_b32 %0:v[1][0:16]
      //~gfx[67]! v2b: %0:v[0][16:32] = v_lshlrev_b32 16, %0:v[0][0:16]
      //~gfx[67]! v3b: %0:v[0][0:24] = v_alignbyte_b32 %0:v[2][0:8], %0:v[0][16:32], 2
      bld.pseudo(aco_opcode::p_unit_test, Operand(11u));
      bld.pseudo(aco_opcode::p_create_vector,
                 Definition(v0_lo, v3b), Operand(v1_lo, v1b),
                 Operand(v0_lo, v1b), Operand(v2_lo, v1b));

      //~gfx[67]! p_unit_test 12
      //~gfx[67]! v1b: %0:v[1][24:32] = v_lshlrev_b32 24, %0:v[1][0:8]
      //~gfx[67]! v2b: %0:v[1][0:16] = v_alignbyte_b32 %0:v[0][0:8], %0:v[1][24:32], 3
      //~gfx[67]! v2b: %0:v[0][0:16] = v_mov_b32 %0:v[1][0:16]
      //~gfx[67]! v2b: %0:v[0][16:32] = v_lshlrev_b32 16, %0:v[0][0:16]
      //~gfx[67]! v3b: %0:v[0][0:24] = v_alignbyte_b32 %0:v[2][0:8], %0:v[0][16:32], 2
      //~gfx[67]! v3b: %0:v[0][8:32] = v_lshlrev_b32 8, %0:v[0][0:24]
      //~gfx[67]! v1: %0:v[0] = v_alignbyte_b32 %0:v[3][0:8], %0:v[0][8:32], 1
      bld.pseudo(aco_opcode::p_unit_test, Operand(12u));
      bld.pseudo(aco_opcode::p_create_vector,
                 Definition(v0_lo, v1),
                 Operand(v1_lo, v1b), Operand(v0_lo, v1b),
                 Operand(v2_lo, v1b), Operand(v3_lo, v1b));

      //~gfx[67]! p_unit_test 13
      //~gfx[67]! v1b: %0:v[0][0:8] = v_and_b32 0xff, %0:v[0][0:8]
      //~gfx[67]! v2b: %0:v[0][0:16] = v_mul_u32_u24 0x101, %0:v[0][0:8]
      //~gfx[67]! v2b: %0:v[0][0:16] = v_and_b32 0xffff, %0:v[0][0:16]
      //~gfx[67]! v3b: %0:v[0][0:24] = v_cvt_pk_u16_u32 %0:v[0][0:16], %0:v[0][0:8]
      //~gfx[67]! v3b: %0:v[0][0:24] = v_and_b32 0xffffff, %0:v[0][0:24]
      //~gfx[67]! s1: %0:m0 = s_mov_b32 0x1000001
      //~gfx[67]! v1: %0:v[0] = v_mul_lo_u32 %0:m0, %0:v[0][0:8]
      bld.pseudo(aco_opcode::p_unit_test, Operand(13u));
      Instruction* pseudo = bld.pseudo(aco_opcode::p_create_vector,
                                       Definition(v0_lo, v1),
                                       Operand(v0_lo, v1b), Operand(v0_lo, v1b),
                                       Operand(v0_lo, v1b), Operand(v0_lo, v1b));
      static_cast<Pseudo_instruction*>(pseudo)->scratch_sgpr = m0;

      //~gfx[67]! p_unit_test 14
      //~gfx[67]! v1b: %0:v[1][0:8] = v_mov_b32 %0:v[0][0:8]
      //~gfx[67]! v1b: %0:v[0][0:8] = v_lshrrev_b32 8, %0:v[1][8:16]
      bld.pseudo(aco_opcode::p_unit_test, Operand(14u));
      bld.pseudo(aco_opcode::p_split_vector,
                 Definition(v1_lo, v1b), Definition(v0_lo, v1b),
                 Operand(v0_lo, v2b));

      //~gfx[67]! p_unit_test 15
      //~gfx[67]! v1b: %0:v[1][0:8] = v_mov_b32 %0:v[0][0:8]
      //~gfx[67]! v1b: %0:v[0][0:8] = v_lshrrev_b32 8, %0:v[1][8:16]
      //~gfx[67]! v1b: %0:v[2][0:8] = v_lshrrev_b32 16, %0:v[1][16:24]
      //~gfx[67]! v1b: %0:v[3][0:8] = v_lshrrev_b32 24, %0:v[1][24:32]
      bld.pseudo(aco_opcode::p_unit_test, Operand(15u));
      bld.pseudo(aco_opcode::p_split_vector,
                 Definition(v1_lo, v1b), Definition(v0_lo, v1b),
                 Definition(v2_lo, v1b), Definition(v3_lo, v1b),
                 Operand(v0_lo, v1));

      //~gfx[67]! s_endpgm

      finish_to_hw_instr_test();
   }

   for (unsigned i = GFX8; i <= GFX9; i++) {
      if (!setup_cs(NULL, (chip_class)i))
         continue;

      //~gfx[89]>> p_unit_test 0
      //~gfx8! v1: %0:v[0] = v_alignbyte_b32 %0:v[0][0:16], %0:v[0][16:32], 2
      //~gfx9! v1: %0:v[0] = v_pack_b32_f16 hi(%0:v[0][16:32]), %0:v[0][0:16]
      bld.pseudo(aco_opcode::p_unit_test, Operand(0u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b),
                 Operand(v0_hi, v2b), Operand(v0_lo, v2b));

      //~gfx[89]! p_unit_test 1
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx9! v1: %0:v[0],  v1: %0:v[1] = v_swap_b32 %0:v[1], %0:v[0]
      //~gfx[89]! v2b: %0:v[1][16:32] = v_mov_b32 %0:v[0][16:32] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(1u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1), Definition(v1_lo, v2b),
                 Operand(v1_lo, v1), Operand(v0_lo, v2b));

      //~gfx[89]! p_unit_test 2
      //~gfx[89]! v2b: %0:v[0][16:32] = v_mov_b32 %0:v[1][16:32] dst_preserve
      //~gfx[89]! v2b: %0:v[1][16:32] = v_mov_b32 %0:v[0][0:16] dst_preserve
      //~gfx[89]! v2b: %0:v[1][0:16] = v_xor_b32 %0:v[1][0:16], %0:v[0][0:16] dst_preserve
      //~gfx[89]! v2b: %0:v[0][0:16] = v_xor_b32 %0:v[1][0:16], %0:v[0][0:16] dst_preserve
      //~gfx[89]! v2b: %0:v[1][0:16] = v_xor_b32 %0:v[1][0:16], %0:v[0][0:16] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(2u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1), Definition(v1_lo, v2b), Definition(v1_hi, v2b),
                 Operand(v1_lo, v1), Operand(v0_lo, v2b), Operand(v0_lo, v2b));

      //~gfx[89]! p_unit_test 3
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx9! v1: %0:v[0],  v1: %0:v[1] = v_swap_b32 %0:v[1], %0:v[0]
      //~gfx[89]! v2b: %0:v[1][0:16] = v_mov_b32 %0:v[0][0:16] dst_preserve
      //~gfx[89]! v1b: %0:v[1][16:24] = v_mov_b32 %0:v[0][16:24] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(3u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1), Definition(v1_b3, v1b),
                 Operand(v1_lo, v1), Operand(v0_b3, v1b));

      //~gfx[89]! p_unit_test 4
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx9! v1: %0:v[0],  v1: %0:v[1] = v_swap_b32 %0:v[1], %0:v[0]
      //~gfx[89]! v1b: %0:v[1][8:16] = v_mov_b32 %0:v[0][8:16] dst_preserve
      //~gfx[89]! v2b: %0:v[1][16:32] = v_mov_b32 %0:v[0][16:32] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(4u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1), Definition(v1_lo, v1b),
                 Operand(v1_lo, v1), Operand(v0_lo, v1b));

      //~gfx[89]! p_unit_test 5
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[0], %0:v[1]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[0], %0:v[1]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[0], %0:v[1]
      //~gfx9! v1: %0:v[1],  v1: %0:v[0] = v_swap_b32 %0:v[0], %0:v[1]
      //~gfx[89]! v1b: %0:v[0][8:16] = v_mov_b32 %0:v[1][8:16] dst_preserve
      //~gfx[89]! v1b: %0:v[0][24:32] = v_mov_b32 %0:v[1][24:32] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(5u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1b), Definition(v0_hi, v1b), Definition(v1_lo, v1),
                 Operand(v1_lo, v1b), Operand(v1_hi, v1b), Operand(v0_lo, v1));

      //~gfx[89]! p_unit_test 6
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx9! v1: %0:v[0],  v1: %0:v[1] = v_swap_b32 %0:v[1], %0:v[0]
      bld.pseudo(aco_opcode::p_unit_test, Operand(6u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b), Definition(v1_lo, v1),
                 Operand(v1_lo, v2b), Operand(v1_hi, v2b), Operand(v0_lo, v1));

      //~gfx[89]! p_unit_test 7
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[0], %0:v[1]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[0], %0:v[1]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[0], %0:v[1]
      //~gfx9! v1: %0:v[1],  v1: %0:v[0] = v_swap_b32 %0:v[0], %0:v[1]
      //~gfx[89]! v1: %0:v[0] = v_alignbyte_b32 %0:v[0][0:16], %0:v[0][16:32], 2
      bld.pseudo(aco_opcode::p_unit_test, Operand(7u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b), Definition(v1_lo, v1),
                 Operand(v1_hi, v2b), Operand(v1_lo, v2b), Operand(v0_lo, v1));

      //~gfx[89]! p_unit_test 8
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx9! v1: %0:v[0],  v1: %0:v[1] = v_swap_b32 %0:v[1], %0:v[0]
      //~gfx[89]! v1b: %0:v[1][24:32] = v_xor_b32 %0:v[1][24:32], %0:v[0][24:32] dst_preserve
      //~gfx[89]! v1b: %0:v[0][24:32] = v_xor_b32 %0:v[1][24:32], %0:v[0][24:32] dst_preserve
      //~gfx[89]! v1b: %0:v[1][24:32] = v_xor_b32 %0:v[1][24:32], %0:v[0][24:32] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(8u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v3b), Definition(v1_lo, v3b),
                 Operand(v1_lo, v3b), Operand(v0_lo, v3b));

      //~gfx[89]! p_unit_test 9
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[0] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx8! v1: %0:v[1] = v_xor_b32 %0:v[1], %0:v[0]
      //~gfx9! v1: %0:v[0],  v1: %0:v[1] = v_swap_b32 %0:v[1], %0:v[0]
      //~gfx[89]! v1b: %0:v[1][24:32] = v_mov_b32 %0:v[0][24:32] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(9u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v3b), Definition(v1_lo, v3b), Definition(v0_b3, v1b),
                 Operand(v1_lo, v3b), Operand(v0_lo, v3b), Operand(v1_b3, v1b));

      //~gfx[89]! p_unit_test 10
      //~gfx[89]! v1b: %0:v[1][8:16] = v_xor_b32 %0:v[1][8:16], %0:v[0][8:16] dst_preserve
      //~gfx[89]! v1b: %0:v[0][8:16] = v_xor_b32 %0:v[1][8:16], %0:v[0][8:16] dst_preserve
      //~gfx[89]! v1b: %0:v[1][8:16] = v_xor_b32 %0:v[1][8:16], %0:v[0][8:16] dst_preserve
      //~gfx[89]! v1b: %0:v[1][16:24] = v_xor_b32 %0:v[1][16:24], %0:v[0][16:24] dst_preserve
      //~gfx[89]! v1b: %0:v[0][16:24] = v_xor_b32 %0:v[1][16:24], %0:v[0][16:24] dst_preserve
      //~gfx[89]! v1b: %0:v[1][16:24] = v_xor_b32 %0:v[1][16:24], %0:v[0][16:24] dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(10u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_b1, v2b), Definition(v1_b1, v2b),
                 Operand(v1_b1, v2b), Operand(v0_b1, v2b));

      //~gfx[89]! p_unit_test 11
      //~gfx[89]! v2b: %0:v[1][0:16] = v_mov_b32 %0:v[0][16:32] dst_preserve
      //~gfx[89]! v1: %0:v[0] = v_mov_b32 42
      bld.pseudo(aco_opcode::p_unit_test, Operand(11u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1), Definition(v1_lo, v2b),
                 Operand(42u), Operand(v0_hi, v2b));

      //~gfx[89]! s_endpgm

      finish_to_hw_instr_test();
   }
END_TEST

BEGIN_TEST(to_hw_instr.subdword_constant)
   PhysReg v0_lo{256};
   PhysReg v0_hi{256};
   PhysReg v0_b1{256};
   PhysReg v1_hi{257};
   v0_hi.reg_b += 2;
   v0_b1.reg_b += 1;
   v1_hi.reg_b += 2;

   for (unsigned i = GFX9; i <= GFX10; i++) {
      if (!setup_cs(NULL, (chip_class)i))
         continue;

      /* 16-bit pack */
      //>> p_unit_test 0
      //! v1: %_:v[0] = v_pack_b32_f16 0.5, hi(%_:v[1][16:32])
      bld.pseudo(aco_opcode::p_unit_test, Operand(0u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b),
                 Operand((uint16_t)0x3800), Operand(v1_hi, v2b));

      //! p_unit_test 1
      //~gfx9! v2b: %0:v[0][16:32] = v_and_b32 0xffff0000, %0:v[1][16:32]
      //~gfx9! v1: %0:v[0] = v_or_b32 0x4205, %0:v[0]
      //~gfx10! v1: %_:v[0] = v_pack_b32_f16 0x4205, hi(%_:v[1][16:32])
      bld.pseudo(aco_opcode::p_unit_test, Operand(1u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b),
                 Operand((uint16_t)0x4205), Operand(v1_hi, v2b));

      //! p_unit_test 2
      //~gfx9! v2b: %0:v[0][16:32] = v_lshlrev_b32 16, %0:v[0][0:16]
      //~gfx9! v1: %_:v[0] = v_or_b32 0x4205, %_:v[0]
      //~gfx10! v1: %0:v[0] = v_pack_b32_f16 0x4205, %0:v[0][0:16]
      bld.pseudo(aco_opcode::p_unit_test, Operand(2u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b),
                 Operand((uint16_t)0x4205), Operand(v0_lo, v2b));

      //! p_unit_test 3
      //! v1: %_:v[0] = v_mov_b32 0x3c003800
      bld.pseudo(aco_opcode::p_unit_test, Operand(3u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b),
                 Operand((uint16_t)0x3800), Operand((uint16_t)0x3c00));

      //! p_unit_test 4
      //! v1: %_:v[0] = v_mov_b32 0x43064205
      bld.pseudo(aco_opcode::p_unit_test, Operand(4u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b),
                 Operand((uint16_t)0x4205), Operand((uint16_t)0x4306));

      //! p_unit_test 5
      //! v1: %_:v[0] = v_mov_b32 0x38004205
      bld.pseudo(aco_opcode::p_unit_test, Operand(5u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Definition(v0_hi, v2b),
                 Operand((uint16_t)0x4205), Operand((uint16_t)0x3800));

      /* 16-bit copy */
      //! p_unit_test 6
      //! v2b: %_:v[0][0:16] = v_add_f16 0.5, 0 dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(6u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Operand((uint16_t)0x3800));

      //! p_unit_test 7
      //~gfx9! v1: %_:v[0] = v_and_b32 0xffff0000, %_:v[0]
      //~gfx9! v1: %_:v[0] = v_or_b32 0x4205, %_:v[0]
      //~gfx10! v2b: %_:v[0][0:16] = v_pack_b32_f16 0x4205, hi(%_:v[0][16:32])
      bld.pseudo(aco_opcode::p_unit_test, Operand(7u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v2b), Operand((uint16_t)0x4205));

      //! p_unit_test 8
      //~gfx9! v1: %_:v[0] = v_and_b32 0xffff, %_:v[0]
      //~gfx9! v1: %_:v[0] = v_or_b32 0x42050000, %_:v[0]
      //~gfx10! v2b: %_:v[0][16:32] = v_pack_b32_f16 %_:v[0][0:16], 0x4205
      bld.pseudo(aco_opcode::p_unit_test, Operand(8u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_hi, v2b), Operand((uint16_t)0x4205));

      //! p_unit_test 9
      //! v1b: %_:v[0][8:16] = v_mov_b32 0 dst_preserve
      //! v1b: %_:v[0][16:24] = v_mov_b32 56 dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(9u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_b1, v2b), Operand((uint16_t)0x3800));

      //! p_unit_test 10
      //! v1b: %_:v[0][8:16] = v_mov_b32 5 dst_preserve
      //! v1b: %_:v[0][16:24] = v_mul_u32_u24 2, 33 dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(10u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_b1, v2b), Operand((uint16_t)0x4205));

      /* 8-bit copy */
      //! p_unit_test 11
      //! v1b: %_:v[0][0:8] = v_mul_u32_u24 2, 33 dst_preserve
      bld.pseudo(aco_opcode::p_unit_test, Operand(11u));
      bld.pseudo(aco_opcode::p_parallelcopy,
                 Definition(v0_lo, v1b), Operand((uint8_t)0x42));

      //! s_endpgm

      finish_to_hw_instr_test();
   }
END_TEST
