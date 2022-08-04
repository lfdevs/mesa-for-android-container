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

BEGIN_TEST(assembler.s_memtime)
   for (unsigned i = GFX6; i <= GFX10; i++) {
      if (!setup_cs(NULL, (amd_gfx_level)i))
         continue;

      //~gfx[6-7]>> c7800000
      //~gfx[6-7]!  bf810000
      //~gfx[8-9]>> s_memtime s[0:1] ; c0900000 00000000
      //~gfx10>> s_memtime s[0:1] ; f4900000 fa000000
      bld.smem(aco_opcode::s_memtime, bld.def(s2)).def(0).setFixed(PhysReg{0});

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.branch_3f)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //! BB0:
   //! s_branch BB1                                                ; bf820040
   //! s_nop 0                                                     ; bf800000
   bld.sopp(aco_opcode::s_branch, Definition(PhysReg(0), s2), 1);

   for (unsigned i = 0; i < 0x3f; i++)
      bld.vop1(aco_opcode::v_nop);

   bld.reset(program->create_and_insert_block());

   program->blocks[1].linear_preds.push_back(0u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.unconditional_forwards)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //!BB0:
   //! s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_addc_u32 s0, s0, 0x20014                                  ; 8200ff00 00020014
   //! s_bitcmp1_b32 s0, 0                                         ; bf0d8000
   //! s_bitset0_b32 s0, 0                                         ; be801b80
   //! s_setpc_b64 s[0:1]                                          ; be802000
   bld.sopp(aco_opcode::s_branch, Definition(PhysReg(0), s2), 2);

   bld.reset(program->create_and_insert_block());

   //! s_nop 0                                                     ; bf800000
   //!(then repeated 32767 times)
   for (unsigned i = 0; i < INT16_MAX + 1; i++)
      bld.sopp(aco_opcode::s_nop, -1, 0);

   //! BB2:
   //! s_endpgm                                                    ; bf810000
   bld.reset(program->create_and_insert_block());

   program->blocks[2].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(1u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.conditional_forwards)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //! BB0:
   //! s_cbranch_scc1 BB1                                          ; bf850006
   //! s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_addc_u32 s0, s0, 0x20014                                  ; 8200ff00 00020014
   //! s_bitcmp1_b32 s0, 0                                         ; bf0d8000
   //! s_bitset0_b32 s0, 0                                         ; be801b80
   //! s_setpc_b64 s[0:1]                                          ; be802000
   bld.sopp(aco_opcode::s_cbranch_scc0, Definition(PhysReg(0), s2), 2);

   bld.reset(program->create_and_insert_block());

   //! BB1:
   //! s_nop 0 ; bf800000
   //!(then repeated 32767 times)
   for (unsigned i = 0; i < INT16_MAX + 1; i++)
      bld.sopp(aco_opcode::s_nop, -1, 0);

   //! BB2:
   //! s_endpgm                                                    ; bf810000
   bld.reset(program->create_and_insert_block());

   program->blocks[1].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(1u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.unconditional_backwards)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //!BB0:
   //! s_nop 0                                                     ; bf800000
   //!(then repeated 32767 times)
   for (unsigned i = 0; i < INT16_MAX + 1; i++)
      bld.sopp(aco_opcode::s_nop, -1, 0);

   //! s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_addc_u32 s0, s0, 0xfffdfffc                               ; 8200ff00 fffdfffc
   //! s_bitcmp1_b32 s0, 0                                         ; bf0d8000
   //! s_bitset0_b32 s0, 0                                         ; be801b80
   //! s_setpc_b64 s[0:1]                                          ; be802000
   bld.sopp(aco_opcode::s_branch, Definition(PhysReg(0), s2), 0);

   //! BB1:
   //! s_endpgm                                                    ; bf810000
   bld.reset(program->create_and_insert_block());

   program->blocks[0].linear_preds.push_back(0u);
   program->blocks[1].linear_preds.push_back(0u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.conditional_backwards)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //!BB0:
   //! s_nop 0                                                     ; bf800000
   //!(then repeated 32767 times)
   for (unsigned i = 0; i < INT16_MAX + 1; i++)
      bld.sopp(aco_opcode::s_nop, -1, 0);

   //! s_cbranch_execz BB1                                         ; bf880006
   //! s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_addc_u32 s0, s0, 0xfffdfff8                               ; 8200ff00 fffdfff8
   //! s_bitcmp1_b32 s0, 0                                         ; bf0d8000
   //! s_bitset0_b32 s0, 0                                         ; be801b80
   //! s_setpc_b64 s[0:1]                                          ; be802000
   bld.sopp(aco_opcode::s_cbranch_execnz, Definition(PhysReg(0), s2), 0);

   //! BB1:
   //! s_endpgm                                                    ; bf810000
   bld.reset(program->create_and_insert_block());

   program->blocks[0].linear_preds.push_back(0u);
   program->blocks[1].linear_preds.push_back(0u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.3f)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //! BB0:
   //! s_branch BB1                                                ; bf820040
   //! s_nop 0                                                     ; bf800000
   bld.sopp(aco_opcode::s_branch, Definition(PhysReg(0), s2), 1);

   for (unsigned i = 0; i < 0x3f - 6; i++) // a unconditional long jump is 6 dwords
      bld.vop1(aco_opcode::v_nop);
   bld.sopp(aco_opcode::s_branch, Definition(PhysReg(0), s2), 2);

   bld.reset(program->create_and_insert_block());
   for (unsigned i = 0; i < INT16_MAX + 1; i++)
      bld.vop1(aco_opcode::v_nop);
   bld.reset(program->create_and_insert_block());

   program->blocks[1].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(1u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.constaddr)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //>> s_getpc_b64 s[0:1]                                          ; be801f00
   bld.sopp(aco_opcode::s_branch, Definition(PhysReg(0), s2), 2);

   bld.reset(program->create_and_insert_block());

   for (unsigned i = 0; i < INT16_MAX + 1; i++)
      bld.sopp(aco_opcode::s_nop, -1, 0);

   bld.reset(program->create_and_insert_block());

   //>> s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_add_u32 s0, s0, 0xe4                                      ; 8000ff00 000000e4
   bld.sop1(aco_opcode::p_constaddr_getpc, Definition(PhysReg(0), s2), Operand::zero());
   bld.sop2(aco_opcode::p_constaddr_addlo, Definition(PhysReg(0), s1), bld.def(s1, scc),
            Operand(PhysReg(0), s1), Operand::zero(), Operand::zero());

   program->blocks[2].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(1u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.v_add3)
   for (unsigned i = GFX9; i <= GFX10; i++) {
      if (!setup_cs(NULL, (amd_gfx_level)i))
         continue;

      //~gfx9>> v_add3_u32 v0, 0, 0, 0 ; d1ff0000 02010080
      //~gfx10>> v_add3_u32 v0, 0, 0, 0 ; d76d0000 02010080
      aco_ptr<VOP3_instruction> add3{create_instruction<VOP3_instruction>(aco_opcode::v_add3_u32, Format::VOP3, 3, 1)};
      add3->operands[0] = Operand::zero();
      add3->operands[1] = Operand::zero();
      add3->operands[2] = Operand::zero();
      add3->definitions[0] = Definition(PhysReg(0), v1);
      bld.insert(std::move(add3));

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.v_add3_clamp)
   for (unsigned i = GFX9; i <= GFX10; i++) {
      if (!setup_cs(NULL, (amd_gfx_level)i))
         continue;

      //~gfx9>> integer addition + clamp ; d1ff8000 02010080
      //~gfx10>> integer addition + clamp ; d76d8000 02010080
      aco_ptr<VOP3_instruction> add3{create_instruction<VOP3_instruction>(aco_opcode::v_add3_u32, Format::VOP3, 3, 1)};
      add3->operands[0] = Operand::zero();
      add3->operands[1] = Operand::zero();
      add3->operands[2] = Operand::zero();
      add3->definitions[0] = Definition(PhysReg(0), v1);
      add3->clamp = 1;
      bld.insert(std::move(add3));

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.smem_offset)
   for (unsigned i = GFX9; i <= GFX10; i++) {
      if (!setup_cs(NULL, (amd_gfx_level)i))
         continue;

      Definition dst(PhysReg(7), s1);
      Operand sbase(PhysReg(6), s2);
      Operand offset(PhysReg(5), s1);

      //~gfx9>> s_load_dword s7, s[6:7], s5 ; c00001c3 00000005
      //~gfx10>> s_load_dword s7, s[6:7], s5 ; f40001c3 0a000000
      bld.smem(aco_opcode::s_load_dword, dst, sbase, offset);
      //~gfx9! s_load_dword s7, s[6:7], 0x42 ; c00201c3 00000042
      //~gfx10! s_load_dword s7, s[6:7], 0x42 ; f40001c3 fa000042
      bld.smem(aco_opcode::s_load_dword, dst, sbase, Operand::c32(0x42));
      if (i >= GFX9) {
         //~gfx9! s_load_dword s7, s[6:7], s5 offset:0x42 ; c00241c3 0a000042
         //~gfx10! s_load_dword s7, s[6:7], s5 offset:0x42 ; f40001c3 0a000042
         bld.smem(aco_opcode::s_load_dword, dst, sbase, Operand::c32(0x42), offset);
      }

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.p_constaddr)
   if (!setup_cs(NULL, GFX9))
      return;

   Definition dst0 = bld.def(s2);
   Definition dst1 = bld.def(s2);
   dst0.setFixed(PhysReg(0));
   dst1.setFixed(PhysReg(2));

   //>> s_getpc_b64 s[0:1] ; be801c00
   //! s_add_u32 s0, s0, 24 ; 8000ff00 00000018
   bld.pseudo(aco_opcode::p_constaddr, dst0, Operand::zero());

   //! s_getpc_b64 s[2:3] ; be821c00
   //! s_add_u32 s2, s2, 44 ; 8002ff02 0000002c
   bld.pseudo(aco_opcode::p_constaddr, dst1, Operand::c32(32));

   aco::lower_to_hw_instr(program.get());
   finish_assembler_test();
END_TEST
