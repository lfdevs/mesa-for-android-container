/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "common/amdgfxregs.h"

#include "helpers.h"

using namespace aco;

void
create_mubuf(unsigned offset, PhysReg dst = PhysReg(256), PhysReg vaddr = PhysReg(256))
{
   bld.mubuf(aco_opcode::buffer_load_dword, Definition(dst, v1), Operand(PhysReg(0), s4),
             Operand(vaddr, v1), Operand::zero(), offset, true);
}

void
create_mubuf_store(PhysReg src = PhysReg(256))
{
   bld.mubuf(aco_opcode::buffer_store_dword, Operand(PhysReg(0), s4), Operand(src, v1),
             Operand::zero(), Operand(src, v1), 0, true);
}

void
create_mimg(bool nsa, unsigned addrs, unsigned instr_dwords)
{
   aco_ptr<Instruction> mimg{
      create_instruction(aco_opcode::image_sample, Format::MIMG, 3 + addrs, 1)};
   mimg->definitions[0] = Definition(PhysReg(256), v1);
   mimg->operands[0] = Operand(PhysReg(0), s8);
   mimg->operands[1] = Operand(PhysReg(0), s4);
   mimg->operands[2] = Operand(v1);
   for (unsigned i = 0; i < addrs; i++)
      mimg->operands[3 + i] = Operand(PhysReg(256 + (nsa ? i * 2 : i)), v1);
   mimg->mimg().dmask = 0x1;
   mimg->mimg().dim = ac_image_2d;

   assert(get_mimg_nsa_dwords(mimg.get()) + 2 == instr_dwords);

   bld.insert(std::move(mimg));
}

void
create_bvh()
{
   aco_ptr<Instruction> instr{
      create_instruction(aco_opcode::image_bvh64_intersect_ray, Format::MIMG, 8, 1)};
   instr->definitions[0] = Definition(PhysReg(256), v4);
   instr->operands[0] = Operand(PhysReg(0), s4);
   instr->operands[1] = Operand(s4);
   instr->operands[2] = Operand(v1);
   instr->operands[3] = Operand(PhysReg(256 + 0), v2); /* node */
   instr->operands[4] = Operand(PhysReg(256 + 2), v1); /* tmax */
   instr->operands[5] = Operand(PhysReg(256 + 3), v3); /* origin */
   instr->operands[6] = Operand(PhysReg(256 + 6), v3); /* dir */
   instr->operands[7] = Operand(PhysReg(256 + 9), v3); /* inv dir */
   instr->mimg().dmask = 0xf;
   instr->mimg().unrm = true;
   instr->mimg().r128 = true;
   bld.insert(std::move(instr));
}

BEGIN_TEST(insert_nops.nsa_to_vmem_bug)
   if (!setup_cs(NULL, GFX10))
      return;

   /* no nop needed because offset&6==0 */
   //>> p_unit_test 0
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2], %0:v[4], %0:v[6], %0:v[8], %0:v[10] 2d
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offset:8 offen
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
   create_mimg(true, 6, 4);
   create_mubuf(8);

   /* nop needed */
   //! p_unit_test 1
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2], %0:v[4], %0:v[6], %0:v[8], %0:v[10] 2d
   //! s_nop
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offset:4 offen
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1u));
   create_mimg(true, 6, 4);
   create_mubuf(4);

   /* no nop needed because the MIMG is not NSA */
   //! p_unit_test 2
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[1], %0:v[2], %0:v[3], %0:v[4], %0:v[5] 2d
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offset:4 offen
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2u));
   create_mimg(false, 6, 2);
   create_mubuf(4);

   /* no nop needed because there's already an instruction in-between */
   //! p_unit_test 3
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2], %0:v[4], %0:v[6], %0:v[8], %0:v[10] 2d
   //! v_nop
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offset:4 offen
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3u));
   create_mimg(true, 6, 4);
   bld.vop1(aco_opcode::v_nop);
   create_mubuf(4);

   /* no nop needed because the NSA instruction is under 4 dwords */
   //! p_unit_test 4
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2] 2d
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offset:4 offen
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4u));
   create_mimg(true, 2, 3);
   create_mubuf(4);

   /* NSA instruction and MUBUF/MTBUF in a different block */
   //! p_unit_test 5
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2], %0:v[4], %0:v[6], %0:v[8], %0:v[10] 2d
   //! BB1
   //! /* logical preds: / linear preds: BB0, / kind: uniform, */
   //! s_nop
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offset:4 offen
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5u));
   create_mimg(true, 6, 4);
   bld.reset(program->create_and_insert_block());
   create_mubuf(4);
   program->blocks[0].linear_succs.push_back(1);
   program->blocks[1].linear_preds.push_back(0);

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.writelane_to_nsa_bug)
   if (!setup_cs(NULL, GFX10))
      return;

   /* nop needed */
   //>> p_unit_test 0
   //! v1: %0:v[255] = v_writelane_b32_e64 0, 0, %0:v[255]
   //! s_nop
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2] 2d
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
   bld.writelane(Definition(PhysReg(511), v1), Operand::zero(), Operand::zero(),
                 Operand(PhysReg(511), v1));
   create_mimg(true, 2, 3);

   /* no nop needed because the MIMG is not NSA */
   //! p_unit_test 1
   //! v1: %0:v[255] = v_writelane_b32_e64 0, 0, %0:v[255]
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[1] 2d
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1u));
   bld.writelane(Definition(PhysReg(511), v1), Operand::zero(), Operand::zero(),
                 Operand(PhysReg(511), v1));
   create_mimg(false, 2, 2);

   /* no nop needed because there's already an instruction in-between */
   //! p_unit_test 2
   //! v1: %0:v[255] = v_writelane_b32_e64 0, 0, %0:v[255]
   //! v_nop
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2] 2d
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2u));
   bld.writelane(Definition(PhysReg(511), v1), Operand::zero(), Operand::zero(),
                 Operand(PhysReg(511), v1));
   bld.vop1(aco_opcode::v_nop);
   create_mimg(true, 2, 3);

   /* writelane and NSA instruction in different blocks */
   //! p_unit_test 3
   //! v1: %0:v[255] = v_writelane_b32_e64 0, 0, %0:v[255]
   //! BB1
   //! /* logical preds: / linear preds: BB0, / kind: uniform, */
   //! s_nop
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2] 2d
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3u));
   bld.writelane(Definition(PhysReg(511), v1), Operand::zero(), Operand::zero(),
                 Operand(PhysReg(511), v1));
   bld.reset(program->create_and_insert_block());
   create_mimg(true, 2, 3);
   program->blocks[0].linear_succs.push_back(1);
   program->blocks[1].linear_preds.push_back(0);

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.vmem_to_scalar_write)
   if (!setup_cs(NULL, GFX10))
      return;

   /* WaR: VMEM load */
   //>> p_unit_test 0
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s1: %0:s[0] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   create_mubuf(0);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());

   //! p_unit_test 1
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s2: %0:exec = s_mov_b64 -1
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   create_mubuf(0);
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));

   /* no hazard: VMEM load */
   //! p_unit_test 2
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
   //! s1: %0:s[4] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   create_mubuf(0);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero());

   /* no hazard: VMEM load with VALU in-between */
   //! p_unit_test 3
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
   //! v_nop
   //! s1: %0:s[0] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   create_mubuf(0);
   bld.vop1(aco_opcode::v_nop);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());

   /* WaR: LDS */
   //! p_unit_test 4
   //! v1: %0:v[0] = ds_read_b32 %0:v[0], %0:m0
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s1: %0:m0 = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1),
          Operand(m0, s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(m0, s1), Operand::zero());

   //! p_unit_test 5
   //! v1: %0:v[0] = ds_read_b32 %0:v[0], %0:m0
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s2: %0:exec = s_mov_b64 -1
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1),
          Operand(m0, s1));
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));

   /* no hazard: LDS */
   //! p_unit_test 6
   //! v1: %0:v[0] = ds_read_b32 %0:v[0], %0:m0
   //! s1: %0:s[0] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1),
          Operand(m0, s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());

   /* no hazard: LDS with VALU in-between */
   //! p_unit_test 7
   //! v1: %0:v[0] = ds_read_b32 %0:v[0], %0:m0
   //! v_nop
   //! s1: %0:m0 = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1),
          Operand(m0, s1));
   bld.vop1(aco_opcode::v_nop);
   bld.sop1(aco_opcode::s_mov_b32, Definition(m0, s1), Operand::zero());

   /* no hazard: VMEM/LDS with the correct waitcnt in-between */
   //! p_unit_test 8
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
   //! s_waitcnt vmcnt(0)
   //! s1: %0:s[0] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
   create_mubuf(0);
   bld.sopp(aco_opcode::s_waitcnt, 0x3f70);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());

   //! p_unit_test 9
   //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[0] offen
   //! s_waitcnt_vscnt %0:null imm:0
   //! s1: %0:s[0] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
   create_mubuf_store();
   bld.sopk(aco_opcode::s_waitcnt_vscnt, Operand(sgpr_null, s1), 0);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());

   //! p_unit_test 10
   //! v1: %0:v[0] = ds_read_b32 %0:v[0], %0:m0
   //! s_waitcnt lgkmcnt(0)
   //! s1: %0:m0 = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1),
          Operand(m0, s1));
   bld.sopp(aco_opcode::s_waitcnt, 0xc07f);
   bld.sop1(aco_opcode::s_mov_b32, Definition(m0, s1), Operand::zero());

   /* VMEM/LDS with the wrong waitcnt in-between */
   //! p_unit_test 11
   //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
   //! s_waitcnt_vscnt %0:null imm:0
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s1: %0:s[0] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(11));
   create_mubuf(0);
   bld.sopk(aco_opcode::s_waitcnt_vscnt, Operand(sgpr_null, s1), 0);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());

   //! p_unit_test 12
   //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[0] offen
   //! s_waitcnt lgkmcnt(0)
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s1: %0:s[0] = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(12));
   create_mubuf_store();
   bld.sopp(aco_opcode::s_waitcnt, 0xc07f);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());

   //! p_unit_test 13
   //! v1: %0:v[0] = ds_read_b32 %0:v[0], %0:m0
   //! s_waitcnt vmcnt(0)
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s1: %0:m0 = s_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(13));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1),
          Operand(m0, s1));
   bld.sopp(aco_opcode::s_waitcnt, 0x3f70);
   bld.sop1(aco_opcode::s_mov_b32, Definition(m0, s1), Operand::zero());

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.lds_direct_valu)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      /* WaW */
      //>> p_unit_test 0
      //! v1: %0:v[0] = v_mov_b32 0
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* WaR */
      //! p_unit_test 1
      //! v1: %0:v[1] = v_mov_b32 %0:v[0]
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* No hazard. */
      //! p_unit_test 2
      //! v1: %0:v[1] = v_mov_b32 0
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::zero());
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* multiples hazards, nearest should be considered */
      //! p_unit_test 3
      //! v1: %0:v[1] = v_mov_b32 %0:v[0]
      //! v1: %0:v[0] = v_mov_b32 0
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* independent VALU increase wait_vdst */
      //! p_unit_test 4
      //! v1: %0:v[0] = v_mov_b32 0
      //! v_nop
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:1
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.vop1(aco_opcode::v_nop);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 5
      //! v1: %0:v[0] = v_mov_b32 0
      //; for i in range(10): insert_pattern('v_nop')
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:10
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      for (unsigned i = 0; i < 10; i++)
         bld.vop1(aco_opcode::v_nop);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 6
      //! v1: %0:v[0] = v_mov_b32 0
      //; for i in range(20): insert_pattern('v_nop')
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      for (unsigned i = 0; i < 20; i++)
         bld.vop1(aco_opcode::v_nop);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* transcendental requires wait_vdst=0 */
      //! p_unit_test 7
      //! v1: %0:v[0] = v_mov_b32 0
      //! v_nop
      //! v1: %0:v[1] = v_sqrt_f32 %0:v[1]
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.vop1(aco_opcode::v_nop);
      bld.vop1(aco_opcode::v_sqrt_f32, Definition(PhysReg(257), v1), Operand(PhysReg(257), v1));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 8
      //! v1: %0:v[0] = v_sqrt_f32 %0:v[0]
      //! v_nop
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
      bld.vop1(aco_opcode::v_sqrt_f32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1));
      bld.vop1(aco_opcode::v_nop);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* transcendental is fine if it's before the instruction */
      //! p_unit_test 9
      //! v1: %0:v[1] = v_sqrt_f32 %0:v[1]
      //! v1: %0:v[0] = v_mov_b32 0
      //! v_nop
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:1
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
      bld.vop1(aco_opcode::v_sqrt_f32, Definition(PhysReg(257), v1), Operand(PhysReg(257), v1));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.vop1(aco_opcode::v_nop);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* non-VALU does not increase wait_vdst */
      //! p_unit_test 10
      //! v1: %0:v[0] = v_mov_b32 0
      //! s1: %0:m0 = s_mov_b32 0
      //! v1: %0:v[0] = lds_direct_load %0:m0 wait_vdst:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.sop1(aco_opcode::s_mov_b32, Definition(m0, s1), Operand::zero());
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* consider instructions which wait on vdst */
      //! p_unit_test 11
      //! v1: %0:v[0] = v_mov_b32 0
      //! v_nop
      //! s_waitcnt_depctr va_vdst(0)
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(11));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.vop1(aco_opcode::v_nop);
      bld.sopp(aco_opcode::s_waitcnt_depctr, 0x0fff);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      finish_insert_nops_test();
   }
END_TEST

BEGIN_TEST(insert_nops.lds_direct_vmem)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      /* WaR: VMEM */
      //>> p_unit_test 0
      //! v1: %0:v[1] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      create_mubuf(0, PhysReg(257));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* WaW: VMEM */
      //! p_unit_test 1
      //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[1], 0 offen
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      create_mubuf(0, PhysReg(256), PhysReg(257));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* no hazard: VMEM */
      //! p_unit_test 2
      //! v1: %0:v[1] = buffer_load_dword %0:s[0-3], %0:v[1], 0 offen
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      create_mubuf(0, PhysReg(257), PhysReg(257));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* no hazard: VMEM with VALU in-between */
      //! p_unit_test 3
      //! v1: %0:v[1] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
      //! v_nop
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      create_mubuf(0, PhysReg(257));
      bld.vop1(aco_opcode::v_nop);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* WaR: LDS */
      //! p_unit_test 4
      //! v1: %0:v[1] = ds_read_b32 %0:v[0]
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* WaW: LDS */
      //! p_unit_test 5
      //! v1: %0:v[0] = ds_read_b32 %0:v[1]
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* no hazard: LDS */
      //! p_unit_test 6
      //! v1: %0:v[1] = ds_read_b32 %0:v[1]
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(257), v1), Operand(PhysReg(257), v1));
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* no hazard: LDS with VALU in-between */
      //! p_unit_test 7
      //! v1: %0:v[1] = ds_read_b32 %0:v[0]
      //! v_nop
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));
      bld.vop1(aco_opcode::v_nop);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      /* no hazard: VMEM/LDS with the correct waitcnt in-between */
      //! p_unit_test 8
      //! v1: %0:v[1] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_loadcnt imm:0
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
      create_mubuf(0, PhysReg(257));
      if (gfx >= GFX12)
         bld.sopp(aco_opcode::s_wait_loadcnt, 0);
      else
         bld.sopp(aco_opcode::s_waitcnt, 0x3ff);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 9
      //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[0] offen
      //~gfx11! s_waitcnt_vscnt %0:null imm:0
      //~gfx12! s_wait_storecnt imm:0
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
      create_mubuf_store();
      if (gfx >= GFX12)
         bld.sopp(aco_opcode::s_wait_storecnt, 0);
      else
         bld.sopk(aco_opcode::s_waitcnt_vscnt, Operand(sgpr_null, s1), 0);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 10
      //! v1: %0:v[1] = ds_read_b32 %0:v[0]
      //~gfx11! s_waitcnt lgkmcnt(0)
      //~gfx12! s_wait_dscnt imm:0
      //! v1: %0:v[0] = lds_direct_load %0:m0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
      bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));
      if (gfx >= GFX12)
         bld.sopp(aco_opcode::s_wait_dscnt, 0);
      else
         bld.sopp(aco_opcode::s_waitcnt, 0xfc0f);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      if (gfx >= GFX12) {
         //~gfx12! p_unit_test 11
         //~gfx12! v1: %0:v[1] = image_load %0:s[0-7], s4: undef, v1: undef, %0:v[0-1] 2d
         //~gfx12! s_wait_loadcnt imm:0
         //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0
         bld.pseudo(aco_opcode::p_unit_test, Operand::c32(11));
         Instruction* instr =
            bld.mimg(aco_opcode::image_load, Definition(PhysReg(257), v1), Operand(PhysReg(0), s8),
                     Operand(s4), Operand(v1), Operand(PhysReg(256), v2))
               .instr;
         instr->mimg().dmask = 0x1;
         instr->mimg().dim = ac_image_2d;
         bld.sopp(aco_opcode::s_wait_loadcnt, 0);
         bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

         //~gfx12! p_unit_test 12
         //~gfx12! v1: %0:v[1] = image_sample %0:s[0-7], %0:s[0-3], v1: undef, %0:v[0-1] 2d
         //~gfx12! s_wait_samplecnt imm:0
         //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0
         bld.pseudo(aco_opcode::p_unit_test, Operand::c32(12));
         instr = bld.mimg(aco_opcode::image_sample, Definition(PhysReg(257), v1),
                          Operand(PhysReg(0), s8), Operand(PhysReg(0), s4), Operand(v1),
                          Operand(PhysReg(256), v2))
                    .instr;
         instr->mimg().dmask = 0x1;
         instr->mimg().dim = ac_image_2d;
         bld.sopp(aco_opcode::s_wait_samplecnt, 0);
         bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

         //~gfx12! p_unit_test 13
         //~gfx12! v4: %0:v[0-3] = image_bvh64_intersect_ray %0:s[0-3], s4: undef, v1: undef, %0:v[0-1], %0:v[2], %0:v[3-5], %0:v[6-8], %0:v[9-11] 1d unrm r128
         //~gfx12! s_wait_bvhcnt imm:0
         //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0
         bld.pseudo(aco_opcode::p_unit_test, Operand::c32(13));
         create_bvh();
         bld.sopp(aco_opcode::s_wait_bvhcnt, 0);
         bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));
      }

      /* VMEM/LDS with the wrong waitcnt in-between */
      //! p_unit_test 14
      //! v1: %0:v[1] = buffer_load_dword %0:s[0-3], %0:v[0], 0 offen
      //~gfx11! s_waitcnt_vscnt %0:null imm:0
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! s_wait_storecnt imm:0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(14));
      create_mubuf(0, PhysReg(257));
      if (gfx >= GFX12)
         bld.sopp(aco_opcode::s_wait_storecnt, 0);
      else
         bld.sopk(aco_opcode::s_waitcnt_vscnt, Operand(sgpr_null, s1), 0);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 15
      //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[0] offen
      //~gfx11! s_waitcnt lgkmcnt(0)
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! s_wait_dscnt imm:0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(15));
      create_mubuf_store();
      if (gfx >= GFX12)
         bld.sopp(aco_opcode::s_wait_dscnt, 0);
      else
         bld.sopp(aco_opcode::s_waitcnt, 0xfc0f);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 16
      //! v1: %0:v[1] = ds_read_b32 %0:v[0]
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! s_wait_loadcnt imm:0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(16));
      bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));
      if (gfx >= GFX12)
         bld.sopp(aco_opcode::s_wait_loadcnt, 0);
      else
         bld.sopp(aco_opcode::s_waitcnt, 0x3ff);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      //! p_unit_test 17
      //! v1: %0:v[0] = buffer_load_dword %0:s[0-3], %0:v[1], 0 offen
      //~gfx11! s_waitcnt_vscnt %0:null imm:0
      //~gfx11! s_waitcnt_depctr vm_vsrc(0)
      //~gfx11! v1: %0:v[0] = lds_direct_load %0:m0
      //~gfx12! s_wait_storecnt imm:0
      //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(17));
      create_mubuf(0, PhysReg(256), PhysReg(257));
      if (gfx >= GFX12)
         bld.sopp(aco_opcode::s_wait_storecnt, 0);
      else
         bld.sopk(aco_opcode::s_waitcnt_vscnt, Operand(sgpr_null, s1), 0);
      bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

      if (gfx >= GFX12) {
         //~gfx12! p_unit_test 18
         //~gfx12! v1: %0:v[1] = image_load %0:s[0-7], s4: undef, v1: undef, %0:v[0-1] 2d
         //~gfx12! s_wait_samplecnt imm:0
         //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
         bld.pseudo(aco_opcode::p_unit_test, Operand::c32(18));
         Instruction* instr =
            bld.mimg(aco_opcode::image_load, Definition(PhysReg(257), v1), Operand(PhysReg(0), s8),
                     Operand(s4), Operand(v1), Operand(PhysReg(256), v2))
               .instr;
         instr->mimg().dmask = 0x1;
         instr->mimg().dim = ac_image_2d;
         bld.sopp(aco_opcode::s_wait_samplecnt, 0);
         bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

         //~gfx12! p_unit_test 19
         //~gfx12! v1: %0:v[1] = image_sample %0:s[0-7], %0:s[0-3], v1: undef, %0:v[0-1] 2d
         //~gfx12! s_wait_loadcnt imm:0
         //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
         bld.pseudo(aco_opcode::p_unit_test, Operand::c32(19));
         instr = bld.mimg(aco_opcode::image_sample, Definition(PhysReg(257), v1),
                          Operand(PhysReg(0), s8), Operand(PhysReg(0), s4), Operand(v1),
                          Operand(PhysReg(256), v2))
                    .instr;
         instr->mimg().dmask = 0x1;
         instr->mimg().dim = ac_image_2d;
         bld.sopp(aco_opcode::s_wait_loadcnt, 0);
         bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));

         //~gfx12! p_unit_test 20
         //~gfx12! v4: %0:v[0-3] = image_bvh64_intersect_ray %0:s[0-3], s4: undef, v1: undef, %0:v[0-1], %0:v[2], %0:v[3-5], %0:v[6-8], %0:v[9-11] 1d unrm r128
         //~gfx12! s_wait_loadcnt imm:0
         //~gfx12! v1: %0:v[0] = lds_direct_load %0:m0 wait_vsrc:0
         bld.pseudo(aco_opcode::p_unit_test, Operand::c32(20));
         create_bvh();
         bld.sopp(aco_opcode::s_wait_loadcnt, 0);
         bld.ldsdir(aco_opcode::lds_direct_load, Definition(PhysReg(256), v1), Operand(m0, s1));
      }

      finish_insert_nops_test();
   }
END_TEST

BEGIN_TEST(insert_nops.valu_trans_use)
   if (!setup_cs(NULL, GFX11))
      return;

   //>> p_unit_test 0
   //! v1: %0:v[0] = v_rcp_f32 %0:v[1]
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[1] = v_mov_b32 %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));

   /* Sufficient VALU mitigates the hazard. */
   //! p_unit_test 1
   //! v1: %0:v[0] = v_rcp_f32 %0:v[1]
   //; for i in range(4): insert_pattern('v_nop')
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[1] = v_mov_b32 %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
   for (unsigned i = 0; i < 4; i++)
      bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));

   //! p_unit_test 2
   //! v1: %0:v[0] = v_rcp_f32 %0:v[1]
   //; for i in range(8): insert_pattern('v_nop')
   //! v1: %0:v[1] = v_mov_b32 %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
   for (unsigned i = 0; i < 8; i++)
      bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));

   /* Sufficient transcendental VALU mitigates the hazard. */
   //! p_unit_test 3
   //! v1: %0:v[0] = v_rcp_f32 %0:v[1]
   //! v1: %0:v[2] = v_sqrt_f32 %0:v[3]
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[1] = v_mov_b32 %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
   bld.vop1(aco_opcode::v_sqrt_f32, Definition(PhysReg(258), v1), Operand(PhysReg(259), v1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));

   //! p_unit_test 4
   //! v1: %0:v[0] = v_rcp_f32 %0:v[1]
   //! v1: %0:v[2] = v_sqrt_f32 %0:v[3]
   //! v1: %0:v[2] = v_sqrt_f32 %0:v[3]
   //! v1: %0:v[1] = v_mov_b32 %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
   for (unsigned i = 0; i < 2; i++)
      bld.vop1(aco_opcode::v_sqrt_f32, Definition(PhysReg(258), v1), Operand(PhysReg(259), v1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));

   /* Transcendental VALU should be counted towards VALU */
   //! p_unit_test 5
   //! v1: %0:v[0] = v_rcp_f32 %0:v[1]
   //; for i in range(5): insert_pattern('v_nop')
   //! v1: %0:v[2] = v_sqrt_f32 %0:v[3]
   //! v1: %0:v[1] = v_mov_b32 %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
   for (unsigned i = 0; i < 5; i++)
      bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_sqrt_f32, Definition(PhysReg(258), v1), Operand(PhysReg(259), v1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));

   /* non-VALU does not mitigate the hazard. */
   //! p_unit_test 6
   //! v1: %0:v[0] = v_rcp_f32 %0:v[1]
   //; for i in range(8): insert_pattern('s_nop')
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[1] = v_mov_b32 %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand(PhysReg(257), v1));
   for (unsigned i = 0; i < 8; i++)
      bld.sopp(aco_opcode::s_nop, 0);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(256), v1));

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.valu_partial_forwarding.basic)
   if (!setup_cs(NULL, GFX11))
      return;

   /* Basic case. */
   //>> p_unit_test 0
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 -1
   //! v1: %0:v[1] = v_mov_b32 1
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max_f32 %0:v[0], %0:v[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.vop2(aco_opcode::v_max_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1));

   /* We should consider both the closest and further VALU after the exec write. */
   //! p_unit_test 1
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 -1
   //! v1: %0:v[1] = v_mov_b32 1
   //; for i in range(2): insert_pattern('v_nop')
   //! v1: %0:v[2] = v_mov_b32 2
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max3_f32 %0:v[0], %0:v[1], %0:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(258), v1), Operand::c32(2));
   bld.vop3(aco_opcode::v_max3_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1), Operand(PhysReg(258), v1));

   //! p_unit_test 2
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 -1
   //! v1: %0:v[1] = v_mov_b32 1
   //! v1: %0:v[2] = v_mov_b32 2
   //; for i in range(4): insert_pattern('v_nop')
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max3_f32 %0:v[0], %0:v[1], %0:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(258), v1), Operand::c32(2));
   for (unsigned i = 0; i < 4; i++)
      bld.vop1(aco_opcode::v_nop);
   bld.vop3(aco_opcode::v_max3_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1), Operand(PhysReg(258), v1));

   /* If a VALU writes a read VGPR in-between the first and second writes, it should still be
    * counted towards the distance between the first and second writes.
    */
   //! p_unit_test 3
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 -1
   //! v1: %0:v[1] = v_mov_b32 1
   //; for i in range(2): insert_pattern('v_nop')
   //! v1: %0:v[2] = v_mov_b32 2
   //; for i in range(3): insert_pattern('v_nop')
   //! v1: %0:v[2] = v_max3_f32 %0:v[0], %0:v[1], %0:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(258), v1), Operand::c32(2));
   for (unsigned i = 0; i < 3; i++)
      bld.vop1(aco_opcode::v_nop);
   bld.vop3(aco_opcode::v_max3_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1), Operand(PhysReg(258), v1));

   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.valu_partial_forwarding.multiple_exec_writes)
   if (!setup_cs(NULL, GFX11))
      return;

   //>> p_unit_test 0
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 0
   //! s2: %0:exec = s_mov_b64 -1
   //! v1: %0:v[1] = v_mov_b32 1
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max_f32 %0:v[0], %0:v[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(0));
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.vop2(aco_opcode::v_max_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1));

   //! p_unit_test 1
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 0
   //! v1: %0:v[1] = v_mov_b32 1
   //! s2: %0:exec = s_mov_b64 -1
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max_f32 %0:v[0], %0:v[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(0));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop2(aco_opcode::v_max_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1));

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.valu_partial_forwarding.control_flow)
   if (!setup_cs(NULL, GFX11))
      return;

   /* Control flow merges: one branch shouldn't interfere with the other (clobbering VALU closer
    * than interesting one).
    */
   //>> p_unit_test 0
   //! s_cbranch_scc1 block:BB2
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0u));
   bld.sopp(aco_opcode::s_cbranch_scc1, 2);

   //! BB1
   //! /* logical preds: / linear preds: BB0, / kind: */
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 -1
   //! v_nop
   //! s_branch block:BB3
   bld.reset(program->create_and_insert_block());
   program->blocks[0].linear_succs.push_back(1);
   program->blocks[1].linear_preds.push_back(0);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_nop);
   bld.sopp(aco_opcode::s_branch, 3);

   //! BB2
   //! /* logical preds: / linear preds: BB0, / kind: */
   //! v1: %0:v[0] = v_mov_b32 0
   bld.reset(program->create_and_insert_block());
   program->blocks[0].linear_succs.push_back(2);
   program->blocks[2].linear_preds.push_back(0);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());

   //! BB3
   //! /* logical preds: / linear preds: BB1, BB2, / kind: */
   //! v1: %0:v[1] = v_mov_b32 1
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max_f32 %0:v[0], %0:v[1]
   bld.reset(program->create_and_insert_block());
   program->blocks[1].linear_succs.push_back(3);
   program->blocks[2].linear_succs.push_back(3);
   program->blocks[3].linear_preds.push_back(1);
   program->blocks[3].linear_preds.push_back(2);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.vop2(aco_opcode::v_max_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1));

   /* Control flow merges: one branch shouldn't interfere with the other (should consider furthest
    * VALU writes after exec).
    */
   //! p_unit_test 1
   //! s_cbranch_scc1 block:BB5
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1u));
   bld.sopp(aco_opcode::s_cbranch_scc1, 5);

   //! BB4
   //! /* logical preds: / linear preds: BB3, / kind: */
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 -1
   //; for i in range(2): insert_pattern('v_nop')
   //! v1: %0:v[1] = v_mov_b32 1
   //! v_nop
   //! s_branch block:BB6
   bld.reset(program->create_and_insert_block());
   program->blocks[3].linear_succs.push_back(4);
   program->blocks[4].linear_preds.push_back(3);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_nop);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   bld.vop1(aco_opcode::v_nop);
   bld.sopp(aco_opcode::s_branch, 6);

   //! BB5
   //! /* logical preds: / linear preds: BB3, / kind: */
   //! v1: %0:v[1] = v_mov_b32 1
   bld.reset(program->create_and_insert_block());
   program->blocks[3].linear_succs.push_back(5);
   program->blocks[5].linear_preds.push_back(3);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));

   //! BB6
   //! /* logical preds: / linear preds: BB4, BB5, / kind: */
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max_f32 %0:v[0], %0:v[1]
   bld.reset(program->create_and_insert_block());
   program->blocks[4].linear_succs.push_back(6);
   program->blocks[5].linear_succs.push_back(6);
   program->blocks[6].linear_preds.push_back(4);
   program->blocks[6].linear_preds.push_back(5);
   bld.vop2(aco_opcode::v_max_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1));

   /* Control flow merges: one branch shouldn't interfere with the other (should consider closest
    * VALU writes after exec).
    */
   //! p_unit_test 2
   //! s_cbranch_scc1 block:BB8
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2u));
   bld.sopp(aco_opcode::s_cbranch_scc1, 8);

   //! BB7
   //! /* logical preds: / linear preds: BB6, / kind: */
   //! v1: %0:v[0] = v_mov_b32 0
   //! s2: %0:exec = s_mov_b64 -1
   //! v1: %0:v[1] = v_mov_b32 1
   //; for i in range(4): insert_pattern('v_nop')
   //! s_branch block:BB9
   bld.reset(program->create_and_insert_block());
   program->blocks[6].linear_succs.push_back(7);
   program->blocks[7].linear_preds.push_back(6);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand::c64(-1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   for (unsigned i = 0; i < 4; i++)
      bld.vop1(aco_opcode::v_nop);
   bld.sopp(aco_opcode::s_branch, 9);

   //! BB8
   //! /* logical preds: / linear preds: BB6, / kind: */
   //! v1: %0:v[1] = v_mov_b32 1
   //; for i in range(5): insert_pattern('v_nop')
   bld.reset(program->create_and_insert_block());
   program->blocks[6].linear_succs.push_back(8);
   program->blocks[8].linear_preds.push_back(6);
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::c32(1));
   for (unsigned i = 0; i < 5; i++)
      bld.vop1(aco_opcode::v_nop);

   //! BB9
   //! /* logical preds: / linear preds: BB7, BB8, / kind: uniform, */
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[2] = v_max_f32 %0:v[0], %0:v[1]
   bld.reset(program->create_and_insert_block());
   program->blocks[7].linear_succs.push_back(9);
   program->blocks[8].linear_succs.push_back(9);
   program->blocks[9].linear_preds.push_back(7);
   program->blocks[9].linear_preds.push_back(8);
   bld.vop2(aco_opcode::v_max_f32, Definition(PhysReg(258), v1), Operand(PhysReg(256), v1),
            Operand(PhysReg(257), v1));

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.valu_mask_write)
   if (!setup_cs(NULL, GFX11))
      return;

   /* Basic case. */
   //>> p_unit_test 0
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));

   /* Mitigation. */
   //! p_unit_test 1
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! v1: %0:v[1] = v_mov_b32 %0:s[1]
   //! s1: %0:s[1] = s_mov_b32 0
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(1), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));

   //! p_unit_test 2
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));

   //! p_unit_test 3
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sopp(aco_opcode::s_waitcnt_depctr, 0xfffe);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));

   /* v_cndmask_b32 is both involved in the hazard and is a mitigation. */
   //! p_unit_test 4
   //! v1: %0:v[0] = v_cndmask_b32 %0:s[2], 0, %0:s[0-1]
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand(PhysReg(2), s1),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));

   /* VALU reading exec does not mitigate the hazard. We also don't consider literals. */
   //! p_unit_test 5
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! v1: %0:v[1] = v_mov_b32 %0:exec_lo
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(exec_lo, s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));

   //! p_unit_test 6
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! v1: %0:v[1] = v_mov_b32 0x200
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand::literal32(0x200));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));

   /* Basic case: VALU. */
   //! p_unit_test 7
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! v1: %0:v[1] = v_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(257), v1), Operand(PhysReg(1), s1));

   /* SALU which both reads and writes a lane mask SGPR. */
   //! p_unit_test 8
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[1] = s_mov_b32 0
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[2-3]
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[2] = s_mov_b32 %0:s[1]
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[4] = s_mov_b32 %0:s[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(2), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand(PhysReg(1), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand(PhysReg(2), s1));

   /* When a SALU writes a lane mask, we shouldn't forget the current SGPRs used as lane masks then
    * written. */
   //! p_unit_test 9
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[0] = s_mov_b32 0
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[2-3]
   //! s1: %0:s[2] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[4] = s_mov_b32 %0:s[0]
   //! s1: %0:s[5] = s_mov_b32 %0:s[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(2), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand(PhysReg(0), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(5), s1), Operand(PhysReg(2), s1));

   /* When a SALU writes a lane mask, we shouldn't forget all SGPRs used as lane masks, there might
    * be later problematic writes. */
   //! p_unit_test 10
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[0] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[4] = s_mov_b32 %0:s[0]
   //! s1: %0:s[1] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[5] = s_mov_b32 %0:s[1]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand(PhysReg(0), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(1), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(5), s1), Operand(PhysReg(1), s1));

   //! p_unit_test 11
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   //! s1: %0:s[0] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[4] = s_mov_b32 %0:s[0]
   //! s1: %0:s[0] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[5] = s_mov_b32 %0:s[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(11));
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand(PhysReg(0), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(5), s1), Operand(PhysReg(0), s1));

   //! p_unit_test 12
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(12));

   //! BB1
   //! /* logical preds: / linear preds: BB0, / kind: */
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[0-1]
   bld.reset(program->create_and_insert_block());
   program->blocks[0].linear_succs.push_back(1);
   program->blocks[1].linear_preds.push_back(0);
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(0), s2));

   //! BB2
   //! /* logical preds: / linear preds: BB0, / kind: */
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:s[2-3]
   bld.reset(program->create_and_insert_block());
   program->blocks[0].linear_succs.push_back(2);
   program->blocks[2].linear_preds.push_back(0);
   bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
                Operand::zero(), Operand(PhysReg(2), s2));

   //! BB3
   //! /* logical preds: / linear preds: BB1, BB2, / kind: uniform, */
   //! s1: %0:s[0] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[4] = s_mov_b32 %0:s[0]
   //! s1: %0:s[2] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[5] = s_mov_b32 %0:s[2]
   bld.reset(program->create_and_insert_block());
   program->blocks[1].linear_succs.push_back(3);
   program->blocks[2].linear_succs.push_back(3);
   program->blocks[3].linear_preds.push_back(1);
   program->blocks[3].linear_preds.push_back(2);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand(PhysReg(0), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(2), s1), Operand::zero());
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(5), s1), Operand(PhysReg(2), s1));

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.wmma_raw)
   if (!setup_cs(NULL, GFX11))
      return;

   /* Basic case. */
   //>> p_unit_test 0
   //! v4: %_:v[20-23] = v_wmma_f16_16x16x16_f16 %_:v[0-7].xx, %_:v[8-15].xx, %_:v[20-23].xx
   //! v_nop
   //! v4: %_:v[48-51] = v_wmma_f16_16x16x16_f16 %_:v[24-31].xx, %_:v[16-23].xx, %_:v[48-51].xx
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   Operand A(PhysReg(256 + 0), v8);
   Operand B(PhysReg(256 + 8), v8);
   Operand C(PhysReg(256 + 20), v4);
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);
   A.setFixed(PhysReg(256 + 24));
   B.setFixed(PhysReg(256 + 16));
   C.setFixed(PhysReg(256 + 48));
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);

   /* Mitigation. */
   //! p_unit_test 1
   //! v4: %_:v[20-23] = v_wmma_f16_16x16x16_f16 %_:v[0-7].xx, %_:v[8-15].xx, %_:v[20-23].xx
   //! v1: %_:v[56] = v_rcp_f32 0
   //! v4: %_:v[48-51] = v_wmma_f16_16x16x16_f16 %_:v[24-31].xx, %_:v[16-23].xx, %_:v[48-51].xx
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   A.setFixed(PhysReg(256 + 0));
   B.setFixed(PhysReg(256 + 8));
   C.setFixed(PhysReg(256 + 20));
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256 + 56), v1), Operand::zero());
   A.setFixed(PhysReg(256 + 24));
   B.setFixed(PhysReg(256 + 16));
   C.setFixed(PhysReg(256 + 48));
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);

   /* No hazard. */
   //>> p_unit_test 2
   //! v4: %_:v[20-23] = v_wmma_f16_16x16x16_f16 %_:v[0-7].xx, %_:v[8-15].xx, %_:v[20-23].xx
   //! v4: %_:v[48-51] = v_wmma_f16_16x16x16_f16 %_:v[24-31].xx, %_:v[32-39].xx, %_:v[48-51].xx
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   A.setFixed(PhysReg(256 + 0));
   B.setFixed(PhysReg(256 + 8));
   C.setFixed(PhysReg(256 + 20));
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);
   A.setFixed(PhysReg(256 + 24));
   B.setFixed(PhysReg(256 + 32));
   C.setFixed(PhysReg(256 + 48));
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);

   //>> p_unit_test 3
   //! v4: %_:v[20-23] = v_wmma_f16_16x16x16_f16 %_:v[0-7].xx, %_:v[8-15].xx, %_:v[20-23].xx
   //! v4: %_:v[20-23] = v_wmma_f16_16x16x16_f16 %_:v[24-31].xx, %_:v[32-39].xx, %_:v[20-23].xx
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   A.setFixed(PhysReg(256 + 0));
   B.setFixed(PhysReg(256 + 8));
   C.setFixed(PhysReg(256 + 20));
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);
   A.setFixed(PhysReg(256 + 24));
   B.setFixed(PhysReg(256 + 32));
   C.setFixed(PhysReg(256 + 20));
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);

   finish_insert_nops_test();
END_TEST

enum StageInfoFlags {
   stage_separate = 1 << 0,
   stage_has_prolog = 1 << 1,
   stage_has_export = 1 << 2,
   stage_is_prolog = 1 << 3,
   stage_is_epilog = 1 << 4,
};

struct StageInfo {
   const char* name;
   Stage stage;
   unsigned flags;
};

BEGIN_TEST(insert_nops.export_priority.stages)
   Stage geometry_ngg(AC_HW_NEXT_GEN_GEOMETRY_SHADER, SWStage::GS);
   for (StageInfo stage : (StageInfo[]){
           {"_fs_first_last", fragment_fs, stage_has_export},
           {"_fs_with_epilog_first", fragment_fs, 0},
           {"_fs_prolog_first", fragment_fs, stage_is_prolog},
           {"_fs_epilog_last", fragment_fs, stage_is_epilog | stage_has_export},
           {"_vs_first_last", vertex_ngg, stage_has_export},
           {"_vs_with_prolog_last", vertex_ngg, stage_has_export | stage_has_prolog},
           {"_tes_first_last", tess_eval_ngg, stage_has_export},
           {"_ms_first_last", mesh_ngg, stage_has_export},
           {"_tesgs_first_last", tess_eval_geometry_ngg, stage_has_export},
           {"_vsgs_first_last", vertex_geometry_ngg, stage_has_export},
           {"_vsgs_with_prolog_last", vertex_geometry_ngg, stage_has_export | stage_has_prolog},
           {"_separate_vs_first", vertex_ngg, stage_separate},
           {"_separate_vs_with_prolog", vertex_ngg, stage_separate | stage_has_prolog},
           {"_separate_tes_first", tess_eval_ngg, stage_separate},
           {"_separate_gs_last", geometry_ngg, stage_separate | stage_has_export}}) {
      if (!setup_cs(NULL, GFX11_5, CHIP_UNKNOWN, stage.name))
         continue;

      program->stage = stage.stage;
      program->info.merged_shader_compiled_separately = stage.flags & stage_separate;
      program->info.vs.has_prolog = stage.flags & stage_has_prolog;
      program->is_prolog = stage.flags & stage_is_prolog;
      program->is_epilog = stage.flags & stage_is_epilog;
      //>> /* logical preds: / linear preds: / kind: uniform, top-level, */
      //~.*first.*! s_setprio imm:2
      if (stage.flags & stage_has_export) {
         //~.*last.*! exp v1: undef, v1: undef, v1: undef, v1: undef en:**** pos0
         //~.*last.*! s_setprio imm:0
         //~.*last.*! s_nop
         //~.*last.*! s_nop
         //~.*last.*! s_endpgm
         bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1), 0x0,
                 V_008DFC_SQ_EXP_POS, false);
      } else {
         //(?!.*last.*)! v_nop
         bld.vop1(aco_opcode::v_nop);
      }

      finish_insert_nops_test(stage.flags & stage_has_export);
   }
END_TEST

BEGIN_TEST(insert_nops.export_priority.instrs_after_export)
   if (!setup_cs(NULL, GFX11_5))
      return;

   program->stage = vertex_ngg;
   //>> /* logical preds: / linear preds: / kind: uniform, top-level, */
   //! s_setprio imm:2
   //! exp v1: undef, v1: undef, v1: undef, v1: undef en:**** pos0
   //! s_setprio imm:0
   //! s_waitcnt_expcnt %0:null imm:0
   //! s_nop
   //! s_nop
   //! s_setprio imm:2
   //! v_nop
   //! s_endpgm
   bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1), 0x0,
           V_008DFC_SQ_EXP_POS, false);
   bld.vop1(aco_opcode::v_nop);

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.export_priority.fallthrough_to_endpgm)
   if (!setup_cs(NULL, GFX11_5))
      return;

   program->stage = vertex_ngg;
   //>> /* logical preds: / linear preds: / kind: top-level, */
   //! s_setprio imm:2
   //! exp v1: undef, v1: undef, v1: undef, v1: undef en:**** pos0
   //! s_setprio imm:0
   //! s_nop
   //! s_nop
   //>> BB1
   //>> /* logical preds: BB0, / linear preds: BB0, / kind: uniform, */
   //! s_endpgm
   bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1), 0x0,
           V_008DFC_SQ_EXP_POS, false);

   bld.reset(program->create_and_insert_block());
   program->blocks[0].linear_succs.push_back(1);
   program->blocks[0].logical_succs.push_back(1);
   program->blocks[1].linear_preds.push_back(0);
   program->blocks[1].logical_preds.push_back(0);

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.export_priority.multiple_exports)
   if (!setup_cs(NULL, GFX11_5))
      return;

   program->stage = vertex_ngg;
   //>> /* logical preds: / linear preds: / kind: uniform, top-level, */
   //! s_setprio imm:2
   //! exp v1: undef, v1: undef, v1: undef, v1: undef en:**** pos0
   //! exp v1: undef, v1: undef, v1: undef, v1: undef en:**** pos1
   //! s_setprio imm:0
   //! s_nop
   //! s_nop
   //! s_endpgm
   bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1), 0x0,
           V_008DFC_SQ_EXP_POS, false);
   bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1), 0x0,
           V_008DFC_SQ_EXP_POS + 1, false);

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.export_priority.set_prio)
   if (!setup_cs(NULL, GFX11_5))
      return;

   program->stage = vertex_ngg;
   //>> /* logical preds: / linear preds: / kind: uniform, top-level, */
   //! s_setprio imm:3
   //! v_nop
   //! s_setprio imm:2
   //! exp v1: undef, v1: undef, v1: undef, v1: undef en:**** pos0
   //! s_setprio imm:0
   //! s_nop
   //! s_nop
   //! s_endpgm
   bld.sopp(aco_opcode::s_setprio, 3);
   bld.vop1(aco_opcode::v_nop);
   bld.sopp(aco_opcode::s_setprio, 1);
   bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1), 0x0,
           V_008DFC_SQ_EXP_POS, false);

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.valu_read_sgpr.basic)
   if (!setup_cs(NULL, GFX12))
      return;

   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(4), s1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(7), s1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(sgpr_null, s1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(exec_lo, s1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(m0, s1));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(scc, s1));

   /* no hazard: SALU write missing */
   //>> p_unit_test 0
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   /* no hazard: SGPR never read by VALU */
   //! p_unit_test 1
   //! s1: %0:s[16] = s_mov_b32 0
   //! s1: %0:s[64] = s_mov_b32 %0:s[16]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(16), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(16), s1));

   /* basic case: SALU read */
   //! p_unit_test 2
   //! s1: %0:s[4] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   /* basic case again: VALU reads never expire */
   //! p_unit_test 3
   //! s1: %0:s[4] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   /* sa_sdst(0) resolves the hazard */
   //! p_unit_test 4
   //! s1: %0:s[4] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   //! p_unit_test 5
   //! s1: %0:s[4] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   bld.sopp(aco_opcode::s_waitcnt_depctr, 0xfffe);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   /* basic case: VALU read */
   //! p_unit_test 6
   //! s1: %0:s[4] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! v1: %0:v[0] = v_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(4), s1));

   /* the SALU write is in the same SGPR pair as the VALU read */
   //! p_unit_test 7
   //! s1: %0:s[6] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[6]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(6), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(6), s1));

   /* no hazard: these registers are not problematic */
   //! p_unit_test 8
   //! s1: %0:null = s_mov_b32 0
   //! s1: %0:s[64] = s_mov_b32 %0:null
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
   bld.sop1(aco_opcode::s_mov_b32, Definition(sgpr_null, s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(sgpr_null, s1));

   //! p_unit_test 9
   //! s1: %0:exec_lo = s_mov_b32 0
   //! s1: %0:s[64] = s_mov_b32 %0:exec_lo
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
   bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(exec_lo, s1));

   //! p_unit_test 10
   //! s1: %0:m0 = s_mov_b32 0
   //! s1: %0:s[64] = s_mov_b32 %0:m0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
   bld.sop1(aco_opcode::s_mov_b32, Definition(m0, s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(m0, s1));

   //! p_unit_test 11
   //! s1: %0:scc = s_cmp_lg_i32 0, 0
   //! s1: %0:s[64] = s_mov_b32 %0:scc
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(11));
   bld.sopc(aco_opcode::s_cmp_lg_i32, Definition(scc, s1), Operand::zero(4), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(scc, s1));

   /* 11 SALU between the write and a VALU read expire the hazard */
   //! p_unit_test 12
   //! s1: %0:s[4] = s_mov_b32 0
   //; for i in range(11): insert_pattern('s1: %0:s[64] = s_mov_b32 0')
   //! v1: %0:v[0] = v_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(12));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   for (unsigned i = 0; i < 11; i++)
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand::zero(4));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(4), s1));

   //! p_unit_test 13
   //! s1: %0:s[4] = s_mov_b32 0
   //; for i in range(10): insert_pattern('s1: %0:s[64] = s_mov_b32 0')
   //! s_waitcnt_depctr sa_sdst(0)
   //! v1: %0:v[0] = v_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(13));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   for (unsigned i = 0; i < 10; i++)
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand::zero(4));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(4), s1));

   /* 10 SALU between the write and a SALU read expire the hazard */
   //! p_unit_test 14
   //! s1: %0:s[4] = s_mov_b32 0
   //; for i in range(10): insert_pattern('s1: %0:s[64] = s_mov_b32 0')
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(14));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   for (unsigned i = 0; i < 10; i++)
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   //! p_unit_test 15
   //! s1: %0:s[4] = s_mov_b32 0
   //; for i in range(9): insert_pattern('s1: %0:s[64] = s_mov_b32 0')
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(15));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   for (unsigned i = 0; i < 9; i++)
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   /* SOPP in-between the write and the read do not count */
   //! p_unit_test 16
   //! s1: %0:s[4] = s_mov_b32 0
   //; for i in range(9): insert_pattern('s1: %0:s[64] = s_mov_b32 0')
   //! s_nop
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(16));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   for (unsigned i = 0; i < 9; i++)
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand::zero(4));
   bld.sopp(aco_opcode::s_nop, 0);
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.valu_read_sgpr.previous_part)
   if (!setup_cs(NULL, GFX12))
      return;

   /* Raytracing shaders have a prolog and may also be split into several parts. */
   program->stage = raytracing_cs;

   /* Despite the SGPR never being read by a VALU in this shader, a sa_sdst(0) is needed. */
   //>> p_unit_test 0
   //! s1: %0:s[4] = s_mov_b32 0
   //! s_waitcnt_depctr sa_sdst(0)
   //! s1: %0:s[64] = s_mov_b32 %0:s[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand(PhysReg(4), s1));

   finish_insert_nops_test();
END_TEST

BEGIN_TEST(insert_nops.setpc_gfx6)
   if (!setup_cs(NULL, GFX6))
      return;

   /* SGPR->SMEM hazards */
   //>> p_unit_test 0
   //! s1: %0:s[0] = s_mov_b32 0
   //! s_nop imm:2
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   //! p_unit_test 1
   //! s1: %0:s[0] = s_mov_b32 0
   //! s_nop imm:2
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand::zero());
   bld.sopp(aco_opcode::s_nop, 2);
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   finish_insert_nops_test();

   /* This hazard can't be tested using s_setpc_b64, because the s_setpc_b64 itself resolves it. */

   /* VINTRP->v_readlane_b32/etc */
   //>> p_unit_test 2
   //! v1: %0:v[0] = v_interp_mov_f32 2, %0:m0 attr0.x
   //! s_nop
   create_program(GFX6, compute_cs, 64, CHIP_UNKNOWN);
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vintrp(aco_opcode::v_interp_mov_f32, Definition(PhysReg(256), v1), Operand::c32(2u),
              Operand(m0, s1), 0, 0);
   finish_insert_nops_test(false);
END_TEST

BEGIN_TEST(insert_nops.setpc_gfx7)
   for (amd_gfx_level gfx : {GFX7, GFX9}) {
      if (!setup_cs(NULL, gfx))
         continue;

      //>> p_unit_test 0
      //! s_setpc_b64 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

      /* Break up SMEM clauses: resolved by the s_setpc_b64 itself */
      //! p_unit_test 1
      //! s1: %0:s[0] = s_load_dword %0:s[0-1]
      //! s_setpc_b64 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.smem(aco_opcode::s_load_dword, Definition(PhysReg(0), s1), Operand(PhysReg(0), s2));
      bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

      /* SALU and GDS hazards */
      //! p_unit_test 2
      //! s_setreg_imm32_b32 0x0 imm:14337
      //! s_nop
      //! s_setpc_b64 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.sopk(aco_opcode::s_setreg_imm32_b32, Operand::literal32(0), (7 << 11) | 1);
      bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

      /* VALU writes vcc -> vccz/v_div_fmas */
      //! p_unit_test 3
      //! s2: %0:vcc = v_cmp_eq_u32 0, 0
      //! s_nop imm:3
      //! s_setpc_b64 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.vopc_e64(aco_opcode::v_cmp_eq_u32, Definition(vcc, s2), Operand::zero(), Operand::zero());
      bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

      /* VALU writes exec -> execz/DPP */
      //! p_unit_test 4
      //! s2: %0:exec = v_cmpx_eq_u32 0, 0
      //! s_nop imm:3
      //! s_setpc_b64 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.vopc_e64(aco_opcode::v_cmpx_eq_u32, Definition(exec, s2), Operand::zero(),
                   Operand::zero());
      bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

      /* VALU->DPP */
      //! p_unit_test 5
      //! v1: %0:v[0] = v_mov_b32 0
      //~gfx9! s_nop
      //! s_setpc_b64 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
      bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

      /* VALU->v_readlane_b32/VMEM/etc */
      //! p_unit_test 6
      //! s1: %0:s[0] = v_readfirstlane_b32 %0:v[0]
      //! s_nop imm:3
      //! s_setpc_b64 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.vop1(aco_opcode::v_readfirstlane_b32, Definition(PhysReg(0), s1),
               Operand(PhysReg(256), v1));
      bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

      finish_insert_nops_test();

      /* These hazards can't be tested using s_setpc_b64, because the s_setpc_b64 itself resolves
       * them. */

      //>> p_unit_test 7
      //! buffer_store_dwordx3 %0:s[0-3], %0:v[0], 0, %0:v[0-2] offen
      //! s_nop
      create_program(gfx, compute_cs, 64, CHIP_UNKNOWN);
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      bld.mubuf(aco_opcode::buffer_store_dwordx3, Operand(PhysReg(0), s4),
                Operand(PhysReg(256), v1), Operand::zero(), Operand(PhysReg(256), v3), 0, true);
      finish_insert_nops_test(false);

      //>> p_unit_test 8
      //! s1: %0:m0 = s_mov_b32 0
      //! s_nop
      create_program(gfx, compute_cs, 64, CHIP_UNKNOWN);
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(m0), s1), Operand::zero());
      finish_insert_nops_test(false);

      /* Break up SMEM clauses */
      //>> p_unit_test 9
      //! s1: %0:s[0] = s_load_dword %0:s[0-1]
      //! s_nop
      create_program(gfx, compute_cs, 64, CHIP_UNKNOWN);
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
      bld.smem(aco_opcode::s_load_dword, Definition(PhysReg(0), s1), Operand(PhysReg(0), s2));
      finish_insert_nops_test(false);
   }
END_TEST

BEGIN_TEST(insert_nops.setpc_gfx10)
   if (!setup_cs(NULL, GFX10))
      return;

   //>> p_unit_test 0
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VcmpxPermlaneHazard */
   //! p_unit_test 1
   //! s2: %0:exec = v_cmpx_eq_u32 0, 0
   //! v1: %0:v[0] = v_mov_b32 %0:v[0]
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vopc_e64(aco_opcode::v_cmpx_eq_u32, Definition(exec, s2), Operand::zero(), Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VMEMtoScalarWriteHazard */
   //! p_unit_test 2
   //! v1: %0:v[0] = ds_read_b32 %0:v[0]
   //! s_waitcnt_vscnt %0:null imm:0
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1));
   bld.sopk(aco_opcode::s_waitcnt_vscnt, Operand(sgpr_null, s1),
            0); /* reset LdsBranchVmemWARHazard */
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VcmpxExecWARHazard */
   //! p_unit_test 3
   //! s1: %0:s[0] = s_mov_b32 %0:exec_hi
   //! s_waitcnt_depctr sa_sdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(0), s1), Operand(exec_hi, s1));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* LdsBranchVmemWARHazard */
   //! p_unit_test 4
   //! v1: %0:v[0] = ds_read_b32 %0:v[0]
   //! v_nop
   //! s_branch block:BB0
   //! s_waitcnt_vscnt %0:null imm:0
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1));
   bld.vop1(aco_opcode::v_nop); /* reset VMEMtoScalarWriteHazard */
   bld.sopp(aco_opcode::s_branch, 0);
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   //! p_unit_test 5
   //! v1: %0:v[0] = ds_read_b32 %0:v[0]
   //! v_nop
   //! s_waitcnt_vscnt %0:null imm:0
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1));
   bld.vop1(aco_opcode::v_nop); /* reset VMEMtoScalarWriteHazard */
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* waNsaCannotFollowWritelane: resolved by the s_setpc_b64 */
   //! p_unit_test 6
   //! v1: %0:v[0] = v_writelane_b32_e64 %0:v[1], 0, %0:v[0]
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
   bld.vop3(aco_opcode::v_writelane_b32_e64, Definition(PhysReg(256), v1),
            Operand(PhysReg(257), v1), Operand::zero(4), Operand(PhysReg(256), v1));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   finish_insert_nops_test();

   /* These hazards can't be tested using s_setpc_b64, because the s_setpc_b64 itself resolves them.
    */

   /* SMEMtoVectorWriteHazard */
   //>> p_unit_test 7
   //! s1: %0:s[0] = s_load_dword %0:s[0-1]
   //! s1: %0:null = s_mov_b32 0
   create_program(GFX10, compute_cs, 64, CHIP_UNKNOWN);
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
   bld.smem(aco_opcode::s_load_dword, Definition(PhysReg(0), s1), Operand(PhysReg(0), s2));
   finish_insert_nops_test(false);

   /* NSAToVMEMBug is already resolved indirectly through VMEMtoScalarWriteHazard and
    * LdsBranchVmemWARHazard. */
   //>> p_unit_test 8
   //! v1: %0:v[0] = image_sample %0:s[0-7], %0:s[0-3],  v1: undef, %0:v[0], %0:v[2], %0:v[4], %0:v[6], %0:v[8], %0:v[10] 2d
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s_waitcnt_vscnt %0:null imm:0
   create_program(GFX10, compute_cs, 64, CHIP_UNKNOWN);
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
   create_mimg(true, 6, 4);
   finish_insert_nops_test(false);

   /* waNsaCannotFollowWritelane */
   //>> p_unit_test 9
   //! v1: %0:v[0] = v_writelane_b32_e64 %0:v[1], 0, %0:v[0]
   //! s_nop
   create_program(GFX10, compute_cs, 64, CHIP_UNKNOWN);
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
   bld.vop3(aco_opcode::v_writelane_b32_e64, Definition(PhysReg(256), v1),
            Operand(PhysReg(257), v1), Operand::zero(4), Operand(PhysReg(256), v1));
   finish_insert_nops_test(false);
END_TEST

BEGIN_TEST(insert_nops.setpc_gfx11)
   if (!setup_cs(NULL, GFX11))
      return;

   //>> p_unit_test 0
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* LdsDirectVALUHazard */
   //! p_unit_test 1
   //! s2: %0:vcc = v_cmp_eq_u32 %0:v[0], 0
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vopc_e64(aco_opcode::v_cmp_eq_u32, Definition(vcc, s2), Operand(PhysReg(256), v1),
                Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VALUPartialForwardingHazard */
   //! p_unit_test 2
   //! v1: %0:v[0] = v_mov_b32 0
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VcmpxPermlaneHazard */
   //! p_unit_test 2
   //! s2: %0:exec = v_cmpx_eq_u32 0, 0
   //! v_nop
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vopc_e64(aco_opcode::v_cmpx_eq_u32, Definition(exec, s2), Operand::zero(), Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VALUTransUseHazard */
   //! p_unit_test 3
   //! v1: %0:v[0] = v_rcp_f32 0
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.vop1(aco_opcode::v_rcp_f32, Definition(PhysReg(256), v1), Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VALUMaskWriteHazard */
   //! p_unit_test 4
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:vcc
   //! s1: %0:vcc_hi = s_mov_b32 0
   //! s_waitcnt_depctr va_vdst(0) sa_sdst(0)
   //! v1: %0:v[0] = v_xor3_b32 %0:v[0], %0:s[0], %0:s[0]
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.vop2(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
            Operand::zero(), Operand(vcc, s2));
   bld.sop1(aco_opcode::s_mov_b32, Definition(vcc_hi, s1), Operand::c32(0));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   //! p_unit_test 8
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:vcc
   //! s_waitcnt_depctr va_vdst(0)
   //! v1: %0:v[0] = v_xor3_b32 %0:v[0], %0:s[0], %0:s[0]
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
   bld.vop2(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
            Operand::zero(), Operand(vcc, s2));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   //! p_unit_test 5
   //! v1: %0:v[0] = v_cndmask_b32 0, 0, %0:vcc
   //! s2: %0:vcc = s_mov_b64 0
   //! s_waitcnt_depctr va_vdst(0) sa_sdst(0)
   //! v1: %0:v[0] = v_xor3_b32 %0:v[0], %0:s[0], %0:s[0]
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.vop2(aco_opcode::v_cndmask_b32, Definition(PhysReg(256), v1), Operand::zero(),
            Operand::zero(), Operand(vcc, s2));
   bld.sop1(aco_opcode::s_mov_b64, Definition(vcc, s2), Operand::zero(8));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* LdsDirectVMEMHazard */
   //! p_unit_test 6
   //! v1: %0:v[0] = ds_read_b32 %0:v[0]
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* WMMA Hazards */
   //! p_unit_test 7
   //! v4: %0:v[20-23] = v_wmma_f16_16x16x16_f16 %0:v[0-7].xx, %0:v[8-15].xx, %0:v[20-23].xx
   //! v_nop
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
   Operand A(PhysReg(256 + 0), v8);
   Operand B(PhysReg(256 + 8), v8);
   Operand C(PhysReg(256 + 20), v4);
   bld.vop3p(aco_opcode::v_wmma_f16_16x16x16_f16, Definition(C.physReg(), C.regClass()), A, B, C, 0,
             0);
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   finish_insert_nops_test(true);
END_TEST

BEGIN_TEST(insert_nops.setpc_gfx12)
   if (!setup_cs(NULL, GFX12))
      return;

   //>> p_unit_test 0
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* LdsDirectVALUHazard */
   //! p_unit_test 1
   //! s2: %0:vcc = v_cmp_eq_u32 %0:v[0], 0
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vopc_e64(aco_opcode::v_cmp_eq_u32, Definition(vcc, s2), Operand(PhysReg(256), v1),
                Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VcmpxPermlaneHazard */
   //! p_unit_test 2
   //! s2: %0:exec = v_cmpx_eq_u32 0, 0
   //! v_nop
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vopc_e64(aco_opcode::v_cmpx_eq_u32, Definition(exec, s2), Operand::zero(), Operand::zero());
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* LdsDirectVMEMHazard */
   //! p_unit_test 3
   //! v1: %0:v[0] = ds_read_b32 %0:v[0]
   //! s_waitcnt_depctr vm_vsrc(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.ds(aco_opcode::ds_read_b32, Definition(PhysReg(256), v1), Operand(PhysReg(256), v1));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   /* VALUReadSGPRHazard */
   //! p_unit_test 4
   //! v1: %0:v[0] = v_mov_b32 %0:s[4]
   //! s1: %0:s[4] = s_mov_b32 0
   //! s_waitcnt_depctr va_vdst(0) sa_sdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(4), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   //! p_unit_test 5
   //! v1: %0:v[0] = v_mov_b32 %0:s[4]
   //! s1: %0:s[4] = s_mov_b32 0
   //; for i in range(10): insert_pattern('s1: %0:s[64] = s_mov_b32 0')
   //! s_waitcnt_depctr va_vdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(4), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   for (unsigned i = 0; i < 10; i++) /* the s_setpc_b64 counts */
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   //! p_unit_test 6
   //! v1: %0:v[0] = v_mov_b32 %0:s[4]
   //! s1: %0:s[4] = s_mov_b32 0
   //; for i in range(9): insert_pattern('s1: %0:s[64] = s_mov_b32 0')
   //! s_waitcnt_depctr va_vdst(0) sa_sdst(0)
   //! s_setpc_b64 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
   bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg(256), v1), Operand(PhysReg(4), s1));
   bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(4), s1), Operand::zero(4));
   for (unsigned i = 0; i < 9; i++)
      bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg(64), s1), Operand::zero(4));
   bld.sop1(aco_opcode::s_setpc_b64, Operand::zero(8));

   finish_insert_nops_test(true);
END_TEST
