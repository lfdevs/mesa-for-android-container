/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include <llvm/Config/llvm-config.h>

#include "helpers.h"
#include "sid.h"

using namespace aco;

static std::vector<amd_gfx_level>
filter_gfx_levels(std::vector<amd_gfx_level> src)
{
   std::vector<amd_gfx_level> res;
   for (amd_gfx_level gfx : src) {
      if (gfx < GFX12 || LLVM_VERSION_MAJOR >= 19)
         res.push_back(gfx);
   }
   return res;
}

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
   //! s_waitcnt_depctr 0xffe3                                     ; bfa3ffe3
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
      bld.sopp(aco_opcode::s_nop, 0);

   //! BB2:
   //! s_endpgm                                                    ; bf810000
   bld.reset(program->create_and_insert_block());

   program->blocks[2].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(1u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.conditional_forwards)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX10, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      //! BB0:
      //! s_cbranch_scc1 BB1                                          ; $_
      //~gfx10! s_waitcnt_depctr 0xffe3                               ; $_
      //! s_getpc_b64 s[0:1]                                          ; $_
      //~gfx12! s_wait_alu 0xfffe                                     ; $_
      //~gfx12! s_sext_i32_i16 s1, s1                                 ; $_
      //~gfx12! s_wait_alu 0xfffe                                     ; $_
      //~gfx10! s_addc_u32 s0, s0, 0x20014                            ; $_ $_
      //~gfx12! s_add_co_ci_u32 s0, s0, 0x20028                       ; $_ $_
      //~gfx12! s_wait_alu 0xfffe                                     ; $_
      //! s_bitcmp1_b32 s0, 0                                         ; $_
      //! s_bitset0_b32 s0, 0                                         ; $_
      //~gfx12! s_wait_alu 0xfffe                                     ; $_
      //! s_setpc_b64 s[0:1]                                          ; $_
      bld.sopp(aco_opcode::s_cbranch_scc0, Definition(PhysReg(0), s2), 2);

      bld.reset(program->create_and_insert_block());

      //! BB1:
      //! s_nop 0 ; bf800000
      //!(then repeated 32767 times)
      for (unsigned i = 0; i < INT16_MAX + 1; i++)
         bld.sopp(aco_opcode::s_nop, 0);

      //! BB2:
      //! s_endpgm                                                    ; $_
      bld.reset(program->create_and_insert_block());

      program->blocks[1].linear_preds.push_back(0u);
      program->blocks[2].linear_preds.push_back(0u);
      program->blocks[2].linear_preds.push_back(1u);

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.long_jump.unconditional_backwards)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //!BB0:
   //! s_nop 0                                                     ; bf800000
   //!(then repeated 32767 times)
   for (unsigned i = 0; i < INT16_MAX + 1; i++)
      bld.sopp(aco_opcode::s_nop, 0);

   //! s_waitcnt_depctr 0xffe3                                     ; bfa3ffe3
   //! s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_addc_u32 s0, s0, 0xfffdfff8                               ; 8200ff00 fffdfff8
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
      bld.sopp(aco_opcode::s_nop, 0);

   //! s_cbranch_execz BB1                                         ; bf880007
   //! s_waitcnt_depctr 0xffe3                                     ; bfa3ffe3
   //! s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_addc_u32 s0, s0, 0xfffdfff4                               ; 8200ff00 fffdfff4
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

BEGIN_TEST(assembler.long_jump._3f)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //! BB0:
   //! s_branch BB1                                                ; bf820040
   //! s_nop 0                                                     ; bf800000
   bld.sopp(aco_opcode::s_branch, Definition(PhysReg(0), s2), 1);

   for (unsigned i = 0; i < 0x3f - 7; i++) // an unconditional long jump is 7 dwords
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
      bld.sopp(aco_opcode::s_nop, 0);

   bld.reset(program->create_and_insert_block());

   //>> s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_add_u32 s0, s0, 32                                         ; 8000ff00 00000020
   bld.sop1(aco_opcode::p_constaddr_getpc, Definition(PhysReg(0), s2), Operand::zero());
   bld.sop2(aco_opcode::p_constaddr_addlo, Definition(PhysReg(0), s1), bld.def(s1, scc),
            Operand(PhysReg(0), s1), Operand::zero(), Operand::zero());

   program->blocks[2].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(1u);

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.long_jump.discard_early_exit)
   if (!setup_cs(NULL, (amd_gfx_level)GFX10))
      return;

   //! BB0:
   //! s_cbranch_scc1 BB1                                          ; bf850007
   //! s_waitcnt_depctr 0xffe3                                     ; bfa3ffe3
   //! s_getpc_b64 s[0:1]                                          ; be801f00
   //! s_addc_u32 s0, s0, 0x20014                                  ; 8200ff00 00020014
   //! s_bitcmp1_b32 s0, 0                                         ; bf0d8000
   //! s_bitset0_b32 s0, 0                                         ; be801b80
   //! s_setpc_b64 s[0:1]                                          ; be802000
   bld.sopp(aco_opcode::s_cbranch_scc0, 2);

   bld.reset(program->create_and_insert_block());

   //! BB1:
   //! s_nop 1                                                     ; bf800001
   //!(then repeated 32766 times)
   //! s_endpgm                                                    ; bf810000
   for (unsigned i = 0; i < INT16_MAX; i++)
      bld.sopp(aco_opcode::s_nop, 1);

   //! BB2:
   //! s_endpgm                                                    ; bf810000
   bld.reset(program->create_and_insert_block());

   program->blocks[1].linear_preds.push_back(0u);
   program->blocks[2].linear_preds.push_back(0u);
   program->blocks[2].kind = block_kind_discard_early_exit;

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.v_add3)
   for (unsigned i = GFX9; i <= GFX10; i++) {
      if (!setup_cs(NULL, (amd_gfx_level)i))
         continue;

      //~gfx9>> v_add3_u32 v0, 0, 0, 0 ; d1ff0000 02010080
      //~gfx10>> v_add3_u32 v0, 0, 0, 0 ; d76d0000 02010080
      aco_ptr<Instruction> add3{create_instruction(aco_opcode::v_add3_u32, Format::VOP3, 3, 1)};
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
      aco_ptr<Instruction> add3{create_instruction(aco_opcode::v_add3_u32, Format::VOP3, 3, 1)};
      add3->operands[0] = Operand::zero();
      add3->operands[1] = Operand::zero();
      add3->operands[2] = Operand::zero();
      add3->definitions[0] = Definition(PhysReg(0), v1);
      add3->valu().clamp = 1;
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
   //! s_add_u32 s0, s0, 44 ; 8000ff00 0000002c
   bld.pseudo(aco_opcode::p_constaddr, dst0, bld.def(s1, scc), Operand::zero());

   //! s_getpc_b64 s[2:3] ; be821c00
   //! s_add_u32 s2, s2, 64 ; 8002ff02 00000040
   bld.pseudo(aco_opcode::p_constaddr, dst1, bld.def(s1, scc), Operand::c32(32));

   aco::lower_to_hw_instr(program.get());
   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.vopc_sdwa)
   for (unsigned i = GFX9; i <= GFX10; i++) {
      if (!setup_cs(NULL, (amd_gfx_level)i))
         continue;

      //~gfx9>> v_cmp_lt_u32_sdwa vcc, 0, 0 src0_sel:DWORD src1_sel:DWORD ; 7d9300f9 86860080
      //~gfx10>> v_cmp_lt_u32_sdwa vcc, 0, 0 src0_sel:DWORD src1_sel:DWORD   ; 7d8300f9 86860080
      bld.vopc_sdwa(aco_opcode::v_cmp_lt_u32, Definition(vcc, s2), Operand::zero(),
                    Operand::zero());

      //~gfx9! v_cmp_lt_u32_sdwa s[44:45], 0, 0 src0_sel:DWORD src1_sel:DWORD ; 7d9300f9 8686ac80
      //~gfx10! v_cmp_lt_u32_sdwa s[44:45], 0, 0 src0_sel:DWORD src1_sel:DWORD ; 7d8300f9 8686ac80
      bld.vopc_sdwa(aco_opcode::v_cmp_lt_u32, Definition(PhysReg(0x2c), s2), Operand::zero(),
                    Operand::zero());

      //~gfx9! v_cmp_lt_u32_sdwa exec, 0, 0 src0_sel:DWORD src1_sel:DWORD ; 7d9300f9 8686fe80
      //~gfx10! v_cmp_lt_u32_sdwa exec, 0, 0 src0_sel:DWORD src1_sel:DWORD  ; 7d8300f9 8686fe80
      bld.vopc_sdwa(aco_opcode::v_cmp_lt_u32, Definition(exec, s2), Operand::zero(),
                    Operand::zero());

      if (i == GFX10) {
         //~gfx10! v_cmpx_lt_u32_sdwa 0, 0 src0_sel:DWORD src1_sel:DWORD ; 7da300f9 86860080
         bld.vopc_sdwa(aco_opcode::v_cmpx_lt_u32, Definition(exec, s2), Operand::zero(),
                       Operand::zero());
      } else {
         //~gfx9! v_cmpx_lt_u32_sdwa vcc, 0, 0 src0_sel:DWORD src1_sel:DWORD ; 7db300f9 86860080
         bld.vopc_sdwa(aco_opcode::v_cmpx_lt_u32, Definition(vcc, s2), Definition(exec, s2),
                       Operand::zero(), Operand::zero());

         //~gfx9! v_cmpx_lt_u32_sdwa s[44:45], 0, 0 src0_sel:DWORD src1_sel:DWORD ; 7db300f9 8686ac80
         bld.vopc_sdwa(aco_opcode::v_cmpx_lt_u32, Definition(PhysReg(0x2c), s2),
                       Definition(exec, s2), Operand::zero(), Operand::zero());
      }

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.smem)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst = bld.def(s1);
      dst.setFixed(PhysReg(4));

      Operand op_s1(bld.tmp(s1));
      op_s1.setFixed(PhysReg(8));

      Operand op_s2(bld.tmp(s2));
      op_s2.setFixed(PhysReg(16));

      Operand op_s4(bld.tmp(s4));
      op_s4.setFixed(PhysReg(32));

      //~gfx11>> s_dcache_inv                                                ; f4840000 f8000000
      //~gfx12>> s_dcache_inv                                                ; f4042000 f8000000
      bld.smem(aco_opcode::s_dcache_inv);

      //! s_load_b32 s4, s[16:17], 0x2a                               ; f4000108 f800002a
      bld.smem(aco_opcode::s_load_dword, dst, op_s2, Operand::c32(42));

      //~gfx11! s_load_b32 s4, s[16:17], s8                                 ; f4000108 10000000
      //~gfx12! s_load_b32 s4, s[16:17], s8 offset:0x0                      ; f4000108 10000000
      bld.smem(aco_opcode::s_load_dword, dst, op_s2, op_s1);

      //! s_load_b32 s4, s[16:17], s8 offset:0x2a                     ; f4000108 1000002a
      bld.smem(aco_opcode::s_load_dword, dst, op_s2, Operand::c32(42), op_s1);

      ac_hw_cache_flags cache_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_non_temporal = {{0, 0, 0, 0, 0}};
      if (gfx >= GFX12) {
         cache_coherent.gfx12.scope = gfx12_scope_device;
         cache_non_temporal.gfx12.temporal_hint = gfx12_load_non_temporal;
      } else {
         cache_coherent.value = ac_glc;
         cache_non_temporal.value = ac_dlc;
      }

      //~gfx11! s_buffer_load_b32 s4, s[32:35], s8 glc                      ; f4204110 10000000
      //~gfx12! s_buffer_load_b32 s4, s[32:35], s8 offset:0x0 scope:SCOPE_DEV ; f4420110 10000000
      bld.smem(aco_opcode::s_buffer_load_dword, dst, op_s4, op_s1)->smem().cache = cache_coherent;

      //~gfx11! s_buffer_load_b32 s4, s[32:35], s8 dlc                      ; f4202110 10000000
      //~gfx12! s_buffer_load_b32 s4, s[32:35], s8 offset:0x0 th:TH_LOAD_NT ; f4820110 10000000
      bld.smem(aco_opcode::s_buffer_load_dword, dst, op_s4, op_s1)->smem().cache =
         cache_non_temporal;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.mubuf)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst = bld.def(v1);
      dst.setFixed(PhysReg(256 + 42));

      Operand op_s4(bld.tmp(s4));
      op_s4.setFixed(PhysReg(32));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 10));

      Operand op_v2(bld.tmp(v2));
      op_v2.setFixed(PhysReg(256 + 20));

      Operand op_s1(bld.tmp(s1));
      op_s1.setFixed(PhysReg(30));

      Operand op_m0(bld.tmp(s1));
      op_m0.setFixed(m0);

      //! llvm_version: #llvm_ver
      fprintf(output, "llvm_version: %u\n", LLVM_VERSION_MAJOR);

      /* Addressing */
      //~gfx11>> buffer_load_b32 v42, off, s[32:35], s30                     ; e0500000 1e082a80
      //~gfx12>> buffer_load_b32 v42, off, s[32:35], s30                     ; c405001e 0080402a 00000000
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), op_s1, 0, false);

      //~gfx11! buffer_load_b32 v42, off, s[32:35], 0                       ; e0500000 80082a80
      //~gfx12! buffer_load_b32 v42, off, s[32:35], null                    ; c405007c 0080402a 00000000
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), Operand::zero(), 0, false);

      //~gfx11! buffer_load_b32 v42, off, s[32:35], 42                      ; e0500000 aa082a80
      if (gfx == GFX11)
         bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), Operand::c32(42), 0,
                   false);

      //~gfx11! buffer_load_b32 v42, v10, s[32:35], s30 offen               ; e0500000 1e482a0a
      //~gfx12! buffer_load_b32 v42, v10, s[32:35], s30 offen               ; c405001e 4080402a 0000000a
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, op_v1, op_s1, 0, true);

      //~gfx11! buffer_load_b32 v42, v10, s[32:35], s30 idxen               ; e0500000 1e882a0a
      //~gfx12! buffer_load_b32 v42, v10, s[32:35], s30 idxen               ; c405001e 8080402a 0000000a
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, op_v1, op_s1, 0, false)->mubuf().idxen =
         true;

      //~gfx11! buffer_load_b32 v42, v[20:21], s[32:35], s30 idxen offen    ; e0500000 1ec82a14
      //~gfx12! buffer_load_b32 v42, v[20:21], s[32:35], s30 idxen offen    ; c405001e c080402a 00000014
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, op_v2, op_s1, 0, true)->mubuf().idxen =
         true;

      //~gfx11! buffer_load_b32 v42, off, s[32:35], s30 offset:84           ; e0500054 1e082a80
      //~gfx12! buffer_load_b32 v42, off, s[32:35], s30 offset:84           ; c405001e 0080402a 00005400
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), op_s1, 84, false);

      /* Various flags */
      ac_hw_cache_flags cache_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_sys_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_non_temporal = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_atomic_rtn = {{0, 0, 0, 0, 0}};
      if (gfx >= GFX12) {
         cache_coherent.gfx12.scope = gfx12_scope_device;
         cache_sys_coherent.gfx12.scope = gfx12_scope_memory;
         cache_non_temporal.gfx12.temporal_hint = gfx12_load_non_temporal;
         cache_atomic_rtn.gfx12.temporal_hint = gfx12_atomic_return;
      } else {
         cache_coherent.value = ac_glc;
         cache_sys_coherent.value = ac_slc;
         cache_non_temporal.value = ac_dlc;
         cache_atomic_rtn.value = ac_glc;
      }

      //~gfx11! buffer_load_b32 v42, off, s[32:35], 0 glc                   ; e0504000 80082a80
      //~gfx12! buffer_load_b32 v42, off, s[32:35], null scope:SCOPE_DEV    ; c405007c 0088402a 00000000
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), Operand::zero(), 0, false)
         ->mubuf()
         .cache = cache_coherent;

      //~gfx11! buffer_load_b32 v42, off, s[32:35], 0 dlc                   ; e0502000 80082a80
      //~gfx12! buffer_load_b32 v42, off, s[32:35], null th:TH_LOAD_NT      ; c405007c 0090402a 00000000
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), Operand::zero(), 0, false)
         ->mubuf()
         .cache = cache_non_temporal;

      //~gfx11! buffer_load_b32 v42, off, s[32:35], 0 slc                   ; e0501000 80082a80
      //~gfx12! buffer_load_b32 v42, off, s[32:35], null scope:SCOPE_SYS    ; c405007c 008c402a 00000000
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), Operand::zero(), 0, false)
         ->mubuf()
         .cache = cache_sys_coherent;

      //; if llvm_ver >= 16 and variant == 'gfx11':
      //;    insert_pattern('buffer_load_b32 v[42:43], off, s[32:35], 0 tfe              ; e0500000 80282a80')
      //; elif variant == 'gfx11':
      //;    insert_pattern('buffer_load_b32 v42, off, s[32:35], 0 tfe                   ; e0500000 80282a80')
      //~gfx12! buffer_load_b32 v[42:43], off, s[32:35], null tfe           ; c445007c 0080402a 00000000
      bld.mubuf(aco_opcode::buffer_load_dword, dst, op_s4, Operand(v1), Operand::zero(), 0, false)
         ->mubuf()
         .tfe = true;

      /* LDS */
      if (gfx == GFX11) {
         //~gfx11! buffer_load_lds_b32 off, s[32:35], 0                        ; e0c40000 80080080
         bld.mubuf(aco_opcode::buffer_load_dword, op_s4, Operand(v1), Operand::zero(), op_m0, 0,
                   false)
            ->mubuf()
            .lds = true;

         //~gfx11! buffer_load_lds_i8 off, s[32:35], 0                         ; e0b80000 80080080
         bld.mubuf(aco_opcode::buffer_load_sbyte, op_s4, Operand(v1), Operand::zero(), op_m0, 0,
                   false)
            ->mubuf()
            .lds = true;

         //~gfx11! buffer_load_lds_i16 off, s[32:35], 0                        ; e0c00000 80080080
         bld.mubuf(aco_opcode::buffer_load_sshort, op_s4, Operand(v1), Operand::zero(), op_m0, 0,
                   false)
            ->mubuf()
            .lds = true;

         //~gfx11! buffer_load_lds_u8 off, s[32:35], 0                         ; e0b40000 80080080
         bld.mubuf(aco_opcode::buffer_load_ubyte, op_s4, Operand(v1), Operand::zero(), op_m0, 0,
                   false)
            ->mubuf()
            .lds = true;

         //~gfx11! buffer_load_lds_u16 off, s[32:35], 0                        ; e0bc0000 80080080
         bld.mubuf(aco_opcode::buffer_load_ushort, op_s4, Operand(v1), Operand::zero(), op_m0, 0,
                   false)
            ->mubuf()
            .lds = true;

         //~gfx11! buffer_load_lds_format_x off, s[32:35], 0                   ; e0c80000 80080080
         bld.mubuf(aco_opcode::buffer_load_format_x, op_s4, Operand(v1), Operand::zero(), op_m0, 0,
                   false)
            ->mubuf()
            .lds = true;
      }

      /* Stores */
      //~gfx11! buffer_store_b32 v10, off, s[32:35], s30                    ; e0680000 1e080a80
      //~gfx12! buffer_store_b32 v10, off, s[32:35], s30                    ; c406801e 0080400a 00000000
      bld.mubuf(aco_opcode::buffer_store_dword, op_s4, Operand(v1), op_s1, op_v1, 0, false);

      //~gfx11! buffer_store_b64 v[20:21], v10, s[32:35], s30 offen         ; e06c0000 1e48140a
      //~gfx12! buffer_store_b64 v[20:21], v10, s[32:35], s30 offen         ; c406c01e 40804014 0000000a
      bld.mubuf(aco_opcode::buffer_store_dwordx2, op_s4, op_v1, op_s1, op_v2, 0, true);

      /* Atomic with return */
      //~gfx11! buffer_atomic_add_u32 v10, off, s[32:35], 0 glc             ; e0d44000 80080a80
      //~gfx12! buffer_atomic_add_u32 v10, off, s[32:35], null th:TH_ATOMIC_RETURN ; c40d407c 0090400a 00000000
      bld.mubuf(aco_opcode::buffer_atomic_add, Definition(op_v1.physReg(), v1), op_s4, Operand(v1),
                Operand::zero(), op_v1, 0, false)
         ->mubuf()
         .cache = cache_atomic_rtn;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.mtbuf)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst = bld.def(v1);
      dst.setFixed(PhysReg(256 + 42));

      Operand op_s4(bld.tmp(s4));
      op_s4.setFixed(PhysReg(32));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 10));

      Operand op_v2(bld.tmp(v2));
      op_v2.setFixed(PhysReg(256 + 20));

      Operand op_s1(bld.tmp(s1));
      op_s1.setFixed(PhysReg(30));

      unsigned dfmt = V_008F0C_BUF_DATA_FORMAT_32_32;
      unsigned nfmt = V_008F0C_BUF_NUM_FORMAT_FLOAT;

      //! llvm_version: #llvm_ver
      fprintf(output, "llvm_version: %u\n", LLVM_VERSION_MAJOR);

      /* Addressing */
      //~gfx11>> tbuffer_load_format_x v42, off, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] ; e9900000 1e082a80
      //~gfx12>> tbuffer_load_format_x v42, off, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] ; c420001e 1900402a 00000080
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), op_s1, dfmt, nfmt, 0,
                false);

      //~gfx11! tbuffer_load_format_x v42, off, s[32:35], 0 format:[BUF_FMT_32_32_FLOAT] ; e9900000 80082a80
      //~gfx12! tbuffer_load_format_x v42, off, s[32:35], null format:[BUF_FMT_32_32_FLOAT] ; c420007c 1900402a 00000080
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), Operand::zero(), dfmt,
                nfmt, 0, false);

      //~gfx11! tbuffer_load_format_x v42, off, s[32:35], 42 format:[BUF_FMT_32_32_FLOAT] ; e9900000 aa082a80
      if (gfx == GFX11)
         bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), Operand::c32(42),
                   dfmt, nfmt, 0, false);

      //~gfx11! tbuffer_load_format_x v42, v10, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] offen ; e9900000 1e482a0a
      //~gfx12! tbuffer_load_format_x v42, v10, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] offen ; c420001e 5900402a 0000000a
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, op_v1, op_s1, dfmt, nfmt, 0, true);

      //~gfx11! tbuffer_load_format_x v42, v10, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] idxen ; e9900000 1e882a0a
      //~gfx12! tbuffer_load_format_x v42, v10, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] idxen ; c420001e 9900402a 0000000a
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, op_v1, op_s1, dfmt, nfmt, 0, false)
         ->mtbuf()
         .idxen = true;

      //~gfx11! tbuffer_load_format_x v42, v[20:21], s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] idxen offen ; e9900000 1ec82a14
      //~gfx12! tbuffer_load_format_x v42, v[20:21], s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] idxen offen ; c420001e d900402a 00000014
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, op_v2, op_s1, dfmt, nfmt, 0, true)
         ->mtbuf()
         .idxen = true;

      //~gfx11! tbuffer_load_format_x v42, off, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] offset:84 ; e9900054 1e082a80
      //~gfx12! tbuffer_load_format_x v42, off, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] offset:84 ; c420001e 1900402a 00005480
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), op_s1, dfmt, nfmt, 84,
                false);

      /* Various flags */
      ac_hw_cache_flags cache_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_sys_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_non_temporal = {{0, 0, 0, 0, 0}};
      if (gfx >= GFX12) {
         cache_coherent.gfx12.scope = gfx12_scope_device;
         cache_sys_coherent.gfx12.scope = gfx12_scope_memory;
         cache_non_temporal.gfx12.temporal_hint = gfx12_load_non_temporal;
      } else {
         cache_coherent.value = ac_glc;
         cache_sys_coherent.value = ac_slc;
         cache_non_temporal.value = ac_dlc;
      }

      //~gfx11! tbuffer_load_format_x v42, off, s[32:35], 0 format:[BUF_FMT_32_32_FLOAT] glc ; e9904000 80082a80
      //~gfx12! tbuffer_load_format_x v42, off, s[32:35], null format:[BUF_FMT_32_32_FLOAT] scope:SCOPE_DEV ; c420007c 1908402a 00000080
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), Operand::zero(), dfmt,
                nfmt, 0, false)
         ->mtbuf()
         .cache = cache_coherent;

      //~gfx11! tbuffer_load_format_x v42, off, s[32:35], 0 format:[BUF_FMT_32_32_FLOAT] dlc ; e9902000 80082a80
      //~gfx12! tbuffer_load_format_x v42, off, s[32:35], null format:[BUF_FMT_32_32_FLOAT] th:TH_LOAD_NT ; c420007c 1910402a 00000080
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), Operand::zero(), dfmt,
                nfmt, 0, false)
         ->mtbuf()
         .cache = cache_non_temporal;

      //~gfx11! tbuffer_load_format_x v42, off, s[32:35], 0 format:[BUF_FMT_32_32_FLOAT] slc ; e9901000 80082a80
      //~gfx12! tbuffer_load_format_x v42, off, s[32:35], null format:[BUF_FMT_32_32_FLOAT] scope:SCOPE_SYS ; c420007c 190c402a 00000080
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), Operand::zero(), dfmt,
                nfmt, 0, false)
         ->mtbuf()
         .cache = cache_sys_coherent;

      //; if llvm_ver >= 19 and variant == 'gfx11':
      //;    insert_pattern('(invalid instruction) ; e9900000')
      //;    insert_pattern('s_add_u32 s40, 0, s42 ; 80282a80')
      //; elif llvm_ver >= 19 and variant == 'gfx12':
      //;    insert_pattern('(invalid instruction) ; c460007c')
      //;    insert_pattern('v_mul_hi_u32_u24_e32 v128, s42, v32 ; 1900402a')
      //;    insert_pattern('(invalid instruction) ; 00000080')
      //; elif llvm_ver >= 16 and variant == 'gfx11':
      //;    insert_pattern('tbuffer_load_format_x v42, off, s[32:35], 0 format:[BUF_FMT_32_32_FLOAT] ; e9900000 80282a80')
      //; elif variant == 'gfx11':
      //;    insert_pattern('tbuffer_load_format_x v42, off, s[32:35], 0 format:[BUF_FMT_32_32_FLOAT] tfe ; e9900000 80282a80')
      //; elif variant == 'gfx12':
      //;    insert_pattern('tbuffer_load_format_x v42, off, s[32:35], null format:[BUF_FMT_32_32_FLOAT] ; c460007c 1900402a 00000080')
      bld.mtbuf(aco_opcode::tbuffer_load_format_x, dst, op_s4, Operand(v1), Operand::zero(), dfmt,
                nfmt, 0, false)
         ->mtbuf()
         .tfe = true;

      /* Stores */
      //~gfx11! tbuffer_store_format_x v10, off, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] ; e9920000 1e080a80
      //~gfx12! tbuffer_store_format_x v10, off, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] ; c421001e 1900400a 00000080
      bld.mtbuf(aco_opcode::tbuffer_store_format_x, op_s4, Operand(v1), op_s1, op_v1, dfmt, nfmt, 0,
                false);

      //~gfx11! tbuffer_store_format_xy v[20:21], v10, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] offen ; e9928000 1e48140a
      //~gfx12! tbuffer_store_format_xy v[20:21], v10, s[32:35], s30 format:[BUF_FMT_32_32_FLOAT] offen ; c421401e 59004014 0000000a
      bld.mtbuf(aco_opcode::tbuffer_store_format_xy, op_s4, op_v1, op_s1, op_v2, dfmt, nfmt, 0,
                true);

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.mimg)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst_v1 = bld.def(v1);
      dst_v1.setFixed(PhysReg(256 + 42));

      Definition dst_v4 = bld.def(v4);
      dst_v4.setFixed(PhysReg(256 + 84));

      Operand op_s4(bld.tmp(s4));
      op_s4.setFixed(PhysReg(32));

      Operand op_s8(bld.tmp(s8));
      op_s8.setFixed(PhysReg(64));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 10));

      Operand op_v2(bld.tmp(v2));
      op_v2.setFixed(PhysReg(256 + 20));

      Operand op_v4(bld.tmp(v4));
      op_v4.setFixed(PhysReg(256 + 30));

      //~gfx11>> image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D ; f06c0f00 2010540a
      //~gfx12>> image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D ; e7c6c000 10008054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1);

      //~gfx11! image_sample v[84:87], v[20:21], s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_2D ; f06c0f04 20105414
      //~gfx12! image_sample v[84:87], [v20, v21], s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_2D ; e7c6c001 10008054 00001514
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v2)->mimg().dim =
         ac_image_2d;

      //~gfx11! image_sample v42, v10, s[64:71], s[32:35] dmask:0x1 dim:SQ_RSRC_IMG_1D ; f06c0100 20102a0a
      //~gfx12! image_sample v42, v10, s[64:71], s[32:35] dmask:0x1 dim:SQ_RSRC_IMG_1D ; e446c000 1000802a 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v1, op_s8, op_s4, Operand(v1), op_v1)->mimg().dmask =
         0x1;

      /* Various flags */
      ac_hw_cache_flags cache_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_sys_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_non_temporal = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_atomic_rtn = {{0, 0, 0, 0, 0}};
      if (gfx >= GFX12) {
         cache_coherent.gfx12.scope = gfx12_scope_device;
         cache_sys_coherent.gfx12.scope = gfx12_scope_memory;
         cache_non_temporal.gfx12.temporal_hint = gfx12_load_non_temporal;
         cache_atomic_rtn.gfx12.temporal_hint = gfx12_atomic_return;
      } else {
         cache_coherent.value = ac_glc;
         cache_sys_coherent.value = ac_slc;
         cache_non_temporal.value = ac_dlc;
         cache_atomic_rtn.value = ac_glc;
      }

      //~gfx11! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D dlc ; f06c2f00 2010540a
      //~gfx12! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D th:TH_LOAD_NT ; e7c6c000 10108054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().cache =
         cache_non_temporal;

      //~gfx11! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D glc ; f06c4f00 2010540a
      //~gfx12! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D scope:SCOPE_DEV ; e7c6c000 10088054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().cache =
         cache_coherent;

      //~gfx11! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D slc ; f06c1f00 2010540a
      //~gfx12! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D scope:SCOPE_SYS ; e7c6c000 100c8054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().cache =
         cache_sys_coherent;

      //~gfx11! image_sample v[84:88], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D tfe ; f06c0f00 2030540a
      //~gfx12! image_sample v[84:88], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D tfe ; e7c6c008 10008054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().tfe =
         true;

      //~gfx11! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D lwe ; f06c0f00 2050540a
      //~gfx12! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D lwe ; e7c6c000 10008154 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().lwe =
         true;

      //~gfx11! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D r128 ; f06c8f00 2010540a
      //~gfx12! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D r128 ; e7c6c010 10008054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().r128 =
         true;

      //~gfx11! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D a16 ; f06d0f00 2010540a
      //~gfx12! image_sample v[84:87], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D a16 ; e7c6c040 10008054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().a16 =
         true;

      //~gfx11! image_sample v[84:85], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D d16 ; f06e0f00 2010540a
      //~gfx12! image_sample v[84:85], v10, s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_1D d16 ; e7c6c020 10008054 0000000a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1)->mimg().d16 =
         true;

      /* NSA */
      //~gfx11! image_sample v[84:87], [v10, v40], s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_2D ; f06c0f05 2010540a 00000028
      //~gfx12! image_sample v[84:87], [v10, v40], s[64:71], s[32:35] dmask:0xf dim:SQ_RSRC_IMG_2D ; e7c6c001 10008054 0000280a
      bld.mimg(aco_opcode::image_sample, dst_v4, op_s8, op_s4, Operand(v1), op_v1,
               Operand(bld.tmp(v1), PhysReg(256 + 40)))
         ->mimg()
         .dim = ac_image_2d;

      //~gfx11! image_bvh_intersect_ray v[84:87], [v40, v42, v[44:46], v[48:50], v[52:54]], s[32:35] ; f0648f81 00085428 34302c2a
      //~gfx12! image_bvh_intersect_ray v[84:87], [v40, v42, v[44:46], v[48:50], v[52:54]], s[32:35] ; d3c64010 34004054 302c2a28
      aco_ptr<Instruction> instr{
         create_instruction(aco_opcode::image_bvh_intersect_ray, Format::MIMG, 8, 1)};
      instr->definitions[0] = dst_v4;
      instr->operands[0] = op_s4;
      instr->operands[1] = Operand(s4);
      instr->operands[2] = Operand(v1);
      instr->operands[3] = Operand(PhysReg(256 + 40), v1); /* node */
      instr->operands[4] = Operand(PhysReg(256 + 42), v1); /* tmax */
      instr->operands[5] = Operand(PhysReg(256 + 44), v3); /* origin */
      instr->operands[6] = Operand(PhysReg(256 + 48), v3); /* dir */
      instr->operands[7] = Operand(PhysReg(256 + 52), v3); /* inv dir */
      instr->mimg().dmask = 0xf;
      instr->mimg().unrm = true;
      instr->mimg().r128 = true;
      bld.insert(std::move(instr));

      /* Stores */
      //~gfx11! image_store v[30:33], v10, s[64:71] dmask:0xf dim:SQ_RSRC_IMG_1D ; f0180f00 00101e0a
      //~gfx12! image_store v[30:33], v10, s[64:71] dmask:0xf dim:SQ_RSRC_IMG_1D ; d3c18000 0000801e 0000000a
      bld.mimg(aco_opcode::image_store, op_s8, Operand(s4), op_v4, op_v1);

      //~gfx11! image_atomic_add v10, v[20:21], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D ; f0300104 00100a14
      //~gfx12! image_atomic_add_uint v10, [v20, v21], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D ; d0430001 0000800a 00001514
      bld.mimg(aco_opcode::image_atomic_add, Definition(op_v1.physReg(), v1), op_s8, Operand(s4),
               op_v1, op_v2, 0x1)
         ->mimg()
         .dim = ac_image_2d;

      /* Atomic with return */
      //~gfx11! image_atomic_add v10, v[20:21], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D glc ; f0304104 00100a14
      //~gfx12! image_atomic_add_uint v10, [v20, v21], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D th:TH_ATOMIC_RETURN ; d0430001 0010800a 00001514
      bld.mimg(aco_opcode::image_atomic_add, Definition(op_v1.physReg(), v1), op_s8, Operand(s4),
               op_v1, op_v2, 0x1, false, false, false, cache_atomic_rtn)
         ->mimg()
         .dim = ac_image_2d;

      //~gfx11! image_load v[84:87], v[20:21], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D ; f0000f04 00105414
      //~gfx12! image_load v[84:87], [v20, v21], s[64:71] dmask:0xf dim:SQ_RSRC_IMG_2D ; d3c00001 00008054 00001514
      bld.mimg(aco_opcode::image_load, dst_v4, op_s8, Operand(s4), Operand(v1), op_v2)->mimg().dim =
         ac_image_2d;

      //~gfx11! image_msaa_load v[84:87], v[30:33], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D_MSAA_ARRAY ; f060011c 0010541e
      //~gfx12! image_msaa_load v[84:87], [v30, v31, v32, v33], s[64:71] dmask:0x1 dim:SQ_RSRC_IMG_2D_MSAA_ARRAY ; e4460007 00008054 21201f1e
      bld.mimg(aco_opcode::image_msaa_load, dst_v4, op_s8, Operand(s4), Operand(v1), op_v4, 0x1)
         ->mimg()
         .dim = ac_image_2darraymsaa;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.flat)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst_v1 = bld.def(v1);
      dst_v1.setFixed(PhysReg(256 + 42));

      Operand op_s1(bld.tmp(s1));
      op_s1.setFixed(PhysReg(32));

      Operand op_s2(bld.tmp(s2));
      op_s2.setFixed(PhysReg(64));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 10));

      Operand op_v2(bld.tmp(v2));
      op_v2.setFixed(PhysReg(256 + 20));

      /* Addressing */
      //~gfx11>> flat_load_b32 v42, v[20:21]                                 ; dc500000 2a7c0014
      //~gfx12>> flat_load_b32 v42, v[20:21]                                 ; ec05007c 0000002a 00000014
      bld.flat(aco_opcode::flat_load_dword, dst_v1, op_v2, Operand(s1));

      //~gfx11! global_load_b32 v42, v[20:21], off                          ; dc520000 2a7c0014
      //~gfx12! global_load_b32 v42, v[20:21], off                          ; ee05007c 0000002a 00000014
      bld.global(aco_opcode::global_load_dword, dst_v1, op_v2, Operand(s1));

      //~gfx11! global_load_b32 v42, v10, s[64:65]                          ; dc520000 2a40000a
      //~gfx12! global_load_b32 v42, v10, s[64:65]                          ; ee050040 0000002a 0000000a
      bld.global(aco_opcode::global_load_dword, dst_v1, op_v1, op_s2);

      //~gfx11! scratch_load_b32 v42, v10, off                              ; dc510000 2afc000a
      //~gfx12! scratch_load_b32 v42, v10, off                              ; ed05007c 0002002a 0000000a
      bld.scratch(aco_opcode::scratch_load_dword, dst_v1, op_v1, Operand(s1));

      //~gfx11! scratch_load_b32 v42, off, s32                              ; dc510000 2a200080
      //~gfx12! scratch_load_b32 v42, off, s32                              ; ed050020 0000002a 00000000
      bld.scratch(aco_opcode::scratch_load_dword, dst_v1, Operand(v1), op_s1);

      //~gfx11! scratch_load_b32 v42, v10, s32                              ; dc510000 2aa0000a
      //~gfx12! scratch_load_b32 v42, v10, s32                              ; ed050020 0002002a 0000000a
      bld.scratch(aco_opcode::scratch_load_dword, dst_v1, op_v1, op_s1);

      //~gfx11! scratch_load_b32 v42, off, off                              ; dc510000 2a7c0080
      //~gfx12! scratch_load_b32 v42, off, off                              ; ed05007c 0000002a 00000000
      bld.scratch(aco_opcode::scratch_load_dword, dst_v1, Operand(v1), Operand(s1));

      //~gfx11! global_load_b32 v42, v[20:21], off offset:-42               ; dc521fd6 2a7c0014
      //~gfx12! global_load_b32 v42, v[20:21], off offset:-42               ; ee05007c 0000002a ffffd614
      bld.global(aco_opcode::global_load_dword, dst_v1, op_v2, Operand(s1), -42);

      //~gfx11! global_load_b32 v42, v[20:21], off offset:84                ; dc520054 2a7c0014
      //~gfx12! global_load_b32 v42, v[20:21], off offset:84                ; ee05007c 0000002a 00005414
      bld.global(aco_opcode::global_load_dword, dst_v1, op_v2, Operand(s1), 84);

      /* Various flags */
      ac_hw_cache_flags cache_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_sys_coherent = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_non_temporal = {{0, 0, 0, 0, 0}};
      ac_hw_cache_flags cache_atomic_rtn = {{0, 0, 0, 0, 0}};
      if (gfx >= GFX12) {
         cache_coherent.gfx12.scope = gfx12_scope_device;
         cache_sys_coherent.gfx12.scope = gfx12_scope_memory;
         cache_non_temporal.gfx12.temporal_hint = gfx12_load_non_temporal;
         cache_atomic_rtn.gfx12.temporal_hint = gfx12_atomic_return;
      } else {
         cache_coherent.value = ac_glc;
         cache_sys_coherent.value = ac_slc;
         cache_non_temporal.value = ac_dlc;
         cache_atomic_rtn.value = ac_glc;
      }

      //~gfx11! flat_load_b32 v42, v[20:21] slc                             ; dc508000 2a7c0014
      //~gfx12! flat_load_b32 v42, v[20:21] scope:SCOPE_SYS                 ; ec05007c 000c002a 00000014
      bld.flat(aco_opcode::flat_load_dword, dst_v1, op_v2, Operand(s1))->flat().cache =
         cache_sys_coherent;

      //~gfx11! flat_load_b32 v42, v[20:21] glc                             ; dc504000 2a7c0014
      //~gfx12! flat_load_b32 v42, v[20:21] scope:SCOPE_DEV                 ; ec05007c 0008002a 00000014
      bld.flat(aco_opcode::flat_load_dword, dst_v1, op_v2, Operand(s1))->flat().cache =
         cache_coherent;

      //~gfx11! flat_load_b32 v42, v[20:21] dlc                             ; dc502000 2a7c0014
      //~gfx12! flat_load_b32 v42, v[20:21] th:TH_LOAD_NT                   ; ec05007c 0010002a 00000014
      bld.flat(aco_opcode::flat_load_dword, dst_v1, op_v2, Operand(s1))->flat().cache =
         cache_non_temporal;

      /* Stores */
      //~gfx11! flat_store_b32 v[20:21], v10                                ; dc680000 007c0a14
      //~gfx12! flat_store_b32 v[20:21], v10                                ; ec06807c 05000000 00000014
      bld.flat(aco_opcode::flat_store_dword, op_v2, Operand(s1), op_v1);

      /* Atomic with return */
      //~gfx11! global_atomic_add_u32 v42, v[20:21], v10, off glc           ; dcd64000 2a7c0a14
      //~gfx12! global_atomic_add_u32 v42, v[20:21], v10, off th:TH_ATOMIC_RETURN ; ee0d407c 0510002a 00000014
      bld.global(aco_opcode::global_atomic_add, dst_v1, op_v2, Operand(s1), op_v1)->global().cache =
         cache_atomic_rtn;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.exp)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Operand op[4];
      for (unsigned i = 0; i < 4; i++)
         op[i] = Operand(PhysReg(256 + i), v1);

      Operand op_m0(bld.tmp(s1));
      op_m0.setFixed(m0);

      //~gfx11>> exp mrt3 v1, v0, v3, v2                                     ; f800003f 02030001
      //~gfx12>> export mrt3 v1, v0, v3, v2                                  ; f800003f 02030001
      bld.exp(aco_opcode::exp, op[1], op[0], op[3], op[2], 0xf, 3);

      //~gfx11! exp mrt3 v1, off, v0, off                                   ; f8000035 80008001
      //~gfx12! export mrt3 v1, off, v0, off                                ; f8000035 80008001
      bld.exp(aco_opcode::exp, op[1], Operand(v1), op[0], Operand(v1), 0x5, 3);

      //~gfx11! exp mrt3 v1, v0, v3, v2 done                                ; f800083f 02030001
      //~gfx12! export mrt3 v1, v0, v3, v2 done                             ; f800083f 02030001
      bld.exp(aco_opcode::exp, op[1], op[0], op[3], op[2], 0xf, 3, false, true);

      //~gfx11! exp mrt3 v1, v0, v3, v2 row_en                              ; f800203f 02030001
      //~gfx12! export mrt3 v1, v0, v3, v2 row_en                           ; f800203f 02030001
      bld.exp(aco_opcode::exp, op[1], op[0], op[3], op[2], op_m0, 0xf, 3)->exp().row_en = true;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.vinterp)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst = bld.def(v1);
      dst.setFixed(PhysReg(256 + 42));

      Operand op0(bld.tmp(v1));
      op0.setFixed(PhysReg(256 + 10));

      Operand op1(bld.tmp(v1));
      op1.setFixed(PhysReg(256 + 20));

      Operand op2(bld.tmp(v1));
      op2.setFixed(PhysReg(256 + 30));

      //! llvm_version: #llvm_ver
      fprintf(output, "llvm_version: %u\n", LLVM_VERSION_MAJOR);

      //>> v_interp_p10_f32 v42, v10, v20, v30 wait_exp:7              ; cd00072a 047a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f32_inreg, dst, op0, op1, op2);

      //! v_interp_p10_f32 v42, v10, v20, v30 wait_exp:6              ; cd00062a 047a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f32_inreg, dst, op0, op1, op2, 0, 6);

      //; if llvm_ver >= 18:
      //;    insert_pattern('v_interp_p2_f32 v42, v10, v20, v30 wait_exp:0               ; cd01002a 047a290a')
      //; else:
      //;    insert_pattern('v_interp_p2_f32 v42, v10, v20, v30                          ; cd01002a 047a290a')
      bld.vinterp_inreg(aco_opcode::v_interp_p2_f32_inreg, dst, op0, op1, op2, 0, 0);

      //! v_interp_p10_f32 v42, -v10, v20, v30 wait_exp:6             ; cd00062a 247a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f32_inreg, dst, op0, op1, op2, 0, 6)
         ->vinterp_inreg()
         .neg[0] = true;

      //! v_interp_p10_f32 v42, v10, -v20, v30 wait_exp:6             ; cd00062a 447a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f32_inreg, dst, op0, op1, op2, 0, 6)
         ->vinterp_inreg()
         .neg[1] = true;

      //! v_interp_p10_f32 v42, v10, v20, -v30 wait_exp:6             ; cd00062a 847a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f32_inreg, dst, op0, op1, op2, 0, 6)
         ->vinterp_inreg()
         .neg[2] = true;

      //! v_interp_p10_f16_f32 v42, v10, v20, v30 op_sel:[1,0,0,0] wait_exp:6 ; cd020e2a 047a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f16_f32_inreg, dst, op0, op1, op2, 0x1, 6);

      //! v_interp_p2_f16_f32 v42, v10, v20, v30 op_sel:[0,1,0,0] wait_exp:6 ; cd03162a 047a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p2_f16_f32_inreg, dst, op0, op1, op2, 0x2, 6);

      //! v_interp_p10_rtz_f16_f32 v42, v10, v20, v30 op_sel:[0,0,1,0] wait_exp:6 ; cd04262a 047a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_rtz_f16_f32_inreg, dst, op0, op1, op2, 0x4, 6);

      //! v_interp_p2_rtz_f16_f32 v42, v10, v20, v30 op_sel:[0,0,0,1] wait_exp:6 ; cd05462a 047a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p2_rtz_f16_f32_inreg, dst, op0, op1, op2, 0x8, 6);

      //! v_interp_p10_f32 v42, v10, v20, v30 clamp wait_exp:6        ; cd00862a 047a290a
      bld.vinterp_inreg(aco_opcode::v_interp_p10_f32_inreg, dst, op0, op1, op2, 0, 6)
         ->vinterp_inreg()
         .clamp = true;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.ldsdir)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst = bld.def(v1);
      dst.setFixed(PhysReg(256 + 42));

      Operand op(bld.tmp(s1));
      op.setFixed(m0);

      //! llvm_version: #llvm_ver
      fprintf(output, "llvm_version: %u\n", LLVM_VERSION_MAJOR);

      //~gfx11>> lds_direct_load v42 wait_vdst:15                           ; ce1f002a
      //~gfx12>> ds_direct_load v42 wait_va_vdst:15 wait_vm_vsrc:1          ; ce9f002a
      bld.ldsdir(aco_opcode::lds_direct_load, dst, op)->ldsdir().wait_vdst = 15;

      //~gfx11! lds_direct_load v42 wait_vdst:6                             ; ce16002a
      //~gfx12! ds_direct_load v42 wait_va_vdst:6 wait_vm_vsrc:1            ; ce96002a
      bld.ldsdir(aco_opcode::lds_direct_load, dst, op)->ldsdir().wait_vdst = 6;

      //; if llvm_ver >= 18 and variant == 'gfx11':
      //;    insert_pattern('lds_direct_load v42 wait_vdst:0                             ; ce10002a')
      //; elif variant == 'gfx11':
      //;    insert_pattern('lds_direct_load v42                                         ; ce10002a')
      //~gfx12! ds_direct_load v42 wait_va_vdst:0 wait_vm_vsrc:1            ; ce90002a
      bld.ldsdir(aco_opcode::lds_direct_load, dst, op)->ldsdir().wait_vdst = 0;

      //~gfx11! lds_param_load v42, attr56.x wait_vdst:8                    ; ce08e02a
      //~gfx12! ds_param_load v42, attr56.x wait_va_vdst:8 wait_vm_vsrc:1   ; ce88e02a
      bld.ldsdir(aco_opcode::lds_param_load, dst, op, 56, 0)->ldsdir().wait_vdst = 8;

      //; if llvm_ver >= 18 and variant == 'gfx11':
      //;    insert_pattern('lds_param_load v42, attr56.x wait_vdst:0                    ; ce00e02a')
      //; elif variant == 'gfx11':
      //;    insert_pattern('lds_param_load v42, attr56.x                                ; ce00e02a')
      //~gfx12! ds_param_load v42, attr56.x wait_va_vdst:0 wait_vm_vsrc:1   ; ce80e02a
      bld.ldsdir(aco_opcode::lds_param_load, dst, op, 56, 0)->ldsdir().wait_vdst = 0;

      //~gfx11! lds_param_load v42, attr34.y wait_vdst:8                    ; ce08892a
      //~gfx12! ds_param_load v42, attr34.y wait_va_vdst:8 wait_vm_vsrc:1   ; ce88892a
      bld.ldsdir(aco_opcode::lds_param_load, dst, op, 34, 1)->ldsdir().wait_vdst = 8;

      //~gfx11! lds_param_load v42, attr12.z wait_vdst:8                    ; ce08322a
      //~gfx12! ds_param_load v42, attr12.z wait_va_vdst:8 wait_vm_vsrc:1   ; ce88322a
      bld.ldsdir(aco_opcode::lds_param_load, dst, op, 12, 2)->ldsdir().wait_vdst = 8;

      //~gfx11>> lds_direct_load v42 wait_vdst:15                           ; ce1f002a
      //~gfx12>> ds_direct_load v42 wait_va_vdst:15 wait_vm_vsrc:0          ; ce1f002a
      bld.ldsdir(aco_opcode::lds_direct_load, dst, op)->ldsdir().wait_vsrc = 0;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.vop12c_v128)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst_v0 = bld.def(v1);
      dst_v0.setFixed(PhysReg(256));

      Definition dst_v128 = bld.def(v1);
      dst_v128.setFixed(PhysReg(256 + 128));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 1));

      Operand op_v2(bld.tmp(v1));
      op_v2.setFixed(PhysReg(256 + 2));

      Operand op_v129(bld.tmp(v1));
      op_v129.setFixed(PhysReg(256 + 129));

      Operand op_v130(bld.tmp(v1));
      op_v130.setFixed(PhysReg(256 + 130));

      //! llvm_version: #llvm_ver
      fprintf(output, "llvm_version: %u\n", LLVM_VERSION_MAJOR);

      //>> BB0:
      //; if llvm_ver == 16:
      //;    insert_pattern('v_mul_f16_e32 v0, v1, v2 ; Error: VGPR_32_Lo128: unknown register 128 ; 6a000501')
      //; else:
      //;    insert_pattern('v_mul_f16_e32 v0, v1, v2                                    ; 6a000501')
      bld.vop2(aco_opcode::v_mul_f16, dst_v0, op_v1, op_v2);

      //! v_mul_f16_e64 v128, v1, v2                                  ; d5350080 00020501
      bld.vop2(aco_opcode::v_mul_f16, dst_v128, op_v1, op_v2);

      //! v_mul_f16_e64 v0, v129, v2                                  ; d5350000 00020581
      bld.vop2(aco_opcode::v_mul_f16, dst_v0, op_v129, op_v2);

      //! v_mul_f16_e64 v0, v1, v130                                  ; d5350000 00030501
      bld.vop2(aco_opcode::v_mul_f16, dst_v0, op_v1, op_v130);

      //! v_rcp_f16_e64 v128, v1                                      ; d5d40080 00000101
      bld.vop1(aco_opcode::v_rcp_f16, dst_v128, op_v1);

      //! v_cmp_eq_f16_e64 vcc, v129, v2                              ; d402006a 00020581
      bld.vopc(aco_opcode::v_cmp_eq_f16, bld.def(s2, vcc), op_v129, op_v2);

      //! v_mul_f16_e64_dpp v128, v1, v2 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5350080 000204fa ff0d2101
      bld.vop2_dpp(aco_opcode::v_mul_f16, dst_v128, op_v1, op_v2, dpp_row_rr(1));

      //! v_mul_f16_e64_dpp v0, v129, v2 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5350000 000204fa ff0d2181
      bld.vop2_dpp(aco_opcode::v_mul_f16, dst_v0, op_v129, op_v2, dpp_row_rr(1));

      //! v_mul_f16_e64_dpp v0, v1, v130 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5350000 000304fa ff0d2101
      bld.vop2_dpp(aco_opcode::v_mul_f16, dst_v0, op_v1, op_v130, dpp_row_rr(1));

      //! v_mul_f16_e64_dpp v128, v1, v2 dpp8:[0,0,0,0,0,0,0,0] fi:1  ; d5350080 000204ea 00000001
      bld.vop2_dpp8(aco_opcode::v_mul_f16, dst_v128, op_v1, op_v2);

      //! v_mul_f16_e64_dpp v0, v129, v2 dpp8:[0,0,0,0,0,0,0,0] fi:1  ; d5350000 000204ea 00000081
      bld.vop2_dpp8(aco_opcode::v_mul_f16, dst_v0, op_v129, op_v2);

      //! v_mul_f16_e64_dpp v0, v1, v130 dpp8:[0,0,0,0,0,0,0,0] fi:1  ; d5350000 000304ea 00000001
      bld.vop2_dpp8(aco_opcode::v_mul_f16, dst_v0, op_v1, op_v130);

      //! v_fma_f16 v128, v1, v2, 0x60                                ; d6480080 03fe0501 00000060
      bld.vop2(aco_opcode::v_fmaak_f16, dst_v128, op_v1, op_v2, Operand::literal32(96));

      //! v_fma_f16 v128, v1, 0x60, v2                                ; d6480080 0409ff01 00000060
      bld.vop2(aco_opcode::v_fmamk_f16, dst_v128, op_v1, op_v2, Operand::literal32(96));

      //! v_rcp_f16_e64_dpp v128, -v1 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5d40080 200000fa ff1d2101
      bld.vop1_dpp(aco_opcode::v_rcp_f16, dst_v128, op_v1, dpp_row_rr(1))->dpp16().neg[0] = true;

      //! v_rcp_f16_e64_dpp v128, |v1| row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5d40180 000000fa ff2d2101
      bld.vop1_dpp(aco_opcode::v_rcp_f16, dst_v128, op_v1, dpp_row_rr(1))->dpp16().abs[0] = true;

      //! v_mul_f16_e64_dpp v128, -v1, v2 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5350080 200204fa ff1d2101
      bld.vop2_dpp(aco_opcode::v_mul_f16, dst_v128, op_v1, op_v2, dpp_row_rr(1))->dpp16().neg[0] =
         true;

      //! v_mul_f16_e64_dpp v128, |v1|, v2 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5350180 000204fa ff2d2101
      bld.vop2_dpp(aco_opcode::v_mul_f16, dst_v128, op_v1, op_v2, dpp_row_rr(1))->dpp16().abs[0] =
         true;

      //! v_cmp_eq_f16_e64_dpp vcc, -v129, v2 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d402006a 200204fa ff1d2181
      bld.vopc_dpp(aco_opcode::v_cmp_eq_f16, bld.def(s2, vcc), op_v129, op_v2, dpp_row_rr(1))
         ->dpp16()
         .neg[0] = true;

      //! v_cmp_eq_f16_e64_dpp vcc, |v129|, v2 row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d402016a 000204fa ff2d2181
      bld.vopc_dpp(aco_opcode::v_cmp_eq_f16, bld.def(s2, vcc), op_v129, op_v2, dpp_row_rr(1))
         ->dpp16()
         .abs[0] = true;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.vop3_dpp)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dst_v0 = bld.def(v1);
      dst_v0.setFixed(PhysReg(256));

      Definition dst_non_vcc = bld.def(s2);
      dst_non_vcc.setFixed(PhysReg(4));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 1));

      Operand op_v2(bld.tmp(v1));
      op_v2.setFixed(PhysReg(256 + 2));

      Operand op_s1(bld.tmp(s1));
      op_s1.setFixed(PhysReg(1));

      //>> BB0:
      //! v_fma_f32_e64_dpp v0, v1, v2, s1 clamp row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d6138000 000604fa ff0d2101
      bld.vop3_dpp(aco_opcode::v_fma_f32, dst_v0, op_v1, op_v2, op_s1, dpp_row_rr(1))->valu().clamp =
         true;

      //! v_fma_mix_f32_e64_dpp v0, |v1|, |v2|, |s1| op_sel:[1,0,0] op_sel_hi:[1,0,1] row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; cc204f00 080604fa ffad2101
      bld.vop3p_dpp(aco_opcode::v_fma_mix_f32, dst_v0, op_v1, op_v2, op_s1, 0x1, 0x5, dpp_row_rr(1))
         ->valu()
         .abs = 0x7;

      //! v_fma_f32_e64_dpp v0, -v1, -v2, -s1 dpp8:[0,0,0,0,0,0,0,0] fi:1 ; d6130000 e00604ea 00000001
      bld.vop3_dpp8(aco_opcode::v_fma_f32, dst_v0, op_v1, op_v2, op_s1)->valu().neg = 0x7;

      //! v_fma_mix_f32_e64_dpp v0, -v1, -v2, s1 op_sel_hi:[1,1,1] dpp8:[0,0,0,0,0,0,0,0] fi:1 ; cc204000 780604ea 00000001
      bld.vop3p_dpp8(aco_opcode::v_fma_mix_f32, dst_v0, op_v1, op_v2, op_s1, 0x0, 0x7)->valu().neg =
         0x3;

      //! v_add_f32_e64_dpp v0, v1, v2 clamp row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5038000 000204fa ff0d2101
      bld.vop2_e64_dpp(aco_opcode::v_add_f32, dst_v0, op_v1, op_v2, dpp_row_rr(1))->valu().clamp =
         true;

      //! v_sqrt_f32_e64_dpp v0, v1 clamp row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d5b38000 000000fa ff0d2101
      bld.vop1_e64_dpp(aco_opcode::v_sqrt_f32, dst_v0, op_v1, dpp_row_rr(1))->valu().clamp = true;

      //! v_cmp_lt_f32_e64_dpp s[4:5], |v1|, |v2| row_ror:1 row_mask:0xf bank_mask:0xf bound_ctrl:1 fi:1 ; d4110304 000204fa ffad2101
      bld.vopc_e64_dpp(aco_opcode::v_cmp_lt_f32, dst_non_vcc, op_v1, op_v2, dpp_row_rr(1))
         ->valu()
         .abs = 0x3;

      //! v_add_f32_e64_dpp v0, v1, v2 mul:4 dpp8:[0,0,0,0,0,0,0,0] fi:1 ; d5030000 100204ea 00000001
      bld.vop2_e64_dpp8(aco_opcode::v_add_f32, dst_v0, op_v1, op_v2)->valu().omod = 2;

      //! v_sqrt_f32_e64_dpp v0, v1 clamp dpp8:[0,0,0,0,0,0,0,0] fi:1 ; d5b38000 000000ea 00000001
      bld.vop1_e64_dpp8(aco_opcode::v_sqrt_f32, dst_v0, op_v1)->valu().clamp = true;

      //! v_cmp_lt_f32_e64_dpp s[4:5], |v1|, v2 dpp8:[0,0,0,0,0,0,0,0] fi:1 ; d4110104 000204ea 00000001
      bld.vopc_e64_dpp8(aco_opcode::v_cmp_lt_f32, dst_non_vcc, op_v1, op_v2)->valu().abs = 0x1;

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.vopd)
   for (amd_gfx_level gfx : filter_gfx_levels({GFX11, GFX12})) {
      if (!setup_cs(NULL, gfx))
         continue;

      program->wave_size = 32;

      Definition dst_v0 = bld.def(v1);
      dst_v0.setFixed(PhysReg(256));

      Definition dst_v1 = bld.def(v1);
      dst_v1.setFixed(PhysReg(256 + 1));

      Operand op_v0(bld.tmp(v1));
      op_v0.setFixed(PhysReg(256 + 0));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 1));

      Operand op_v2(bld.tmp(v1));
      op_v2.setFixed(PhysReg(256 + 2));

      Operand op_v3(bld.tmp(v1));
      op_v3.setFixed(PhysReg(256 + 3));

      Operand op_s0(bld.tmp(s1));
      op_s0.setFixed(PhysReg(0));

      Operand op_vcc(bld.tmp(s1));
      op_vcc.setFixed(vcc);

      //>> BB0:
      //! v_dual_mov_b32 v0, v0 :: v_dual_mov_b32 v1, v1 ; ca100100 00000101
      bld.vopd(aco_opcode::v_dual_mov_b32, dst_v0, dst_v1, op_v0, op_v1,
               aco_opcode::v_dual_mov_b32);

      //! v_dual_mov_b32 v0, 0x60 :: v_dual_mov_b32 v1, s0 ; ca1000ff 00000000 00000060
      bld.vopd(aco_opcode::v_dual_mov_b32, dst_v0, dst_v1, Operand::c32(96), op_s0,
               aco_opcode::v_dual_mov_b32);

      //! v_dual_mov_b32 v0, s0 :: v_dual_mov_b32 v1, 0x60 ; ca100000 000000ff 00000060
      bld.vopd(aco_opcode::v_dual_mov_b32, dst_v0, dst_v1, op_s0, Operand::c32(96),
               aco_opcode::v_dual_mov_b32);

      //! v_dual_mul_f32 v0, v0, v1 :: v_dual_mov_b32 v1, v2 ; c8d00300 00000102
      bld.vopd(aco_opcode::v_dual_mul_f32, dst_v0, dst_v1, op_v0, op_v1, op_v2,
               aco_opcode::v_dual_mov_b32);

      //! v_dual_fmac_f32 v0, v1, v2 :: v_dual_mov_b32 v1, v3 ; c8100501 00000103
      bld.vopd(aco_opcode::v_dual_fmac_f32, dst_v0, dst_v1, op_v1, op_v2, op_v0, op_v3,
               aco_opcode::v_dual_mov_b32);

      //! v_dual_mov_b32 v0, v0 :: v_dual_and_b32 v1, v1, v2 ; ca240100 00000501
      bld.vopd(aco_opcode::v_dual_mov_b32, dst_v0, dst_v1, op_v0, op_v1, op_v2,
               aco_opcode::v_dual_and_b32);

      //! v_dual_cndmask_b32 v0, v0, v1 :: v_dual_cndmask_b32 v1, v2, v3 ; ca520300 00000702
      bld.vopd(aco_opcode::v_dual_cndmask_b32, dst_v0, dst_v1, op_v0, op_v1, op_vcc, op_v2, op_v3,
               op_vcc, aco_opcode::v_dual_cndmask_b32);

      finish_assembler_test();
   }
END_TEST

BEGIN_TEST(assembler.pseudo_scalar_trans)
   if (LLVM_VERSION_MAJOR < 19 || !setup_cs(NULL, GFX12))
      return;

   //>> v_s_sqrt_f32 s5, s1                                         ; d6880005 00000001
   bld.vop3(aco_opcode::v_s_sqrt_f32, Definition(PhysReg(5), s1), Operand(PhysReg(1), s1));

   finish_assembler_test();
END_TEST

BEGIN_TEST(assembler.vintrp_high_16bits)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      if (!setup_cs(NULL, (amd_gfx_level)i))
         continue;

      Definition dst_v0 = bld.def(v1);
      dst_v0.setFixed(PhysReg(256));

      Definition dst_v1 = bld.def(v1);
      dst_v1.setFixed(PhysReg(256 + 1));

      Operand op_v0(bld.tmp(v1));
      op_v0.setFixed(PhysReg(256 + 0));

      Operand op_v1(bld.tmp(v1));
      op_v1.setFixed(PhysReg(256 + 1));

      Operand op_v2(bld.tmp(v1));
      op_v2.setFixed(PhysReg(256 + 2));

      Operand op_m0(bld.tmp(s1));
      op_m0.setFixed(m0);

      aco_opcode interp_p2_op = aco_opcode::v_interp_p2_f16;

      if (bld.program->gfx_level == GFX8)
         interp_p2_op = aco_opcode::v_interp_p2_legacy_f16;

      //! BB0:
      //~gfx8! v_interp_p1ll_f16 v0, v1, attr4.y high                      ; d2740000 00020344
      //~gfx9! v_interp_p1ll_f16 v0, v1, attr4.y high                      ; d2740000 00020344
      //~gfx10! v_interp_p1ll_f16 v0, v1, attr4.y high                      ; d7420000 00020344
      bld.vintrp(aco_opcode::v_interp_p1ll_f16, dst_v0, op_v1, op_m0, 4, 1, true);

      //~gfx8! v_interp_p2_f16 v1, v2, attr4.y, v0 high                    ; d2760001 04020544
      //~gfx9! v_interp_p2_f16 v1, v2, attr4.y, v0 high                    ; d2770001 04020544
      //~gfx10! v_interp_p2_f16 v1, v2, attr4.y, v0 high                    ; d75a0001 04020544
      bld.vintrp(interp_p2_op, dst_v1, op_v2, op_m0, op_v0, 4, 1, true);

      finish_assembler_test();
   }
END_TEST
