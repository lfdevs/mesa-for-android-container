/*
 * Copyright © 2022 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "helpers.h"

using namespace aco;

BEGIN_TEST(insert_waitcnt.ds_ordered_count)
   if (!setup_cs(NULL, GFX10_3))
      return;

   Operand def0(PhysReg(256), v1);
   Operand def1(PhysReg(257), v1);
   Operand def2(PhysReg(258), v1);
   Operand gds_base(PhysReg(259), v1);
   Operand chan_counter(PhysReg(260), v1);
   Operand m(m0, s1);

   Instruction* ds_instr;
   //>> ds_ordered_count %0:v[0], %0:v[3], %0:m0 offset0:3072 gds storage:gds semantics:volatile
   //! s_waitcnt lgkmcnt(0)
   ds_instr = bld.ds(aco_opcode::ds_ordered_count, def0, gds_base, m, 3072u, 0u, true);
   ds_instr->ds().sync = memory_sync_info(storage_gds, semantic_volatile);

   //! ds_add_rtn_u32 %0:v[1], %0:v[3], %0:v[4], %0:m0 gds storage:gds semantics:volatile,atomic,rmw
   ds_instr = bld.ds(aco_opcode::ds_add_rtn_u32, def1, gds_base, chan_counter, m, 0u, 0u, true);
   ds_instr->ds().sync = memory_sync_info(storage_gds, semantic_atomicrmw);

   //! s_waitcnt lgkmcnt(0)
   //! ds_ordered_count %0:v[2], %0:v[3], %0:m0 offset0:3840 gds storage:gds semantics:volatile
   ds_instr = bld.ds(aco_opcode::ds_ordered_count, def2, gds_base, m, 3840u, 0u, true);
   ds_instr->ds().sync = memory_sync_info(storage_gds, semantic_volatile);

   finish_waitcnt_test();
END_TEST

BEGIN_TEST(insert_waitcnt.clause)
   if (!setup_cs(NULL, GFX11))
      return;

   Definition def_v4(PhysReg(260), v1);
   Definition def_v5(PhysReg(261), v1);
   Definition def_v6(PhysReg(262), v1);
   Definition def_v7(PhysReg(263), v1);
   Operand op_v0(PhysReg(256), v1);
   Operand op_v4(PhysReg(260), v1);
   Operand op_v5(PhysReg(261), v1);
   Operand op_v6(PhysReg(262), v1);
   Operand op_v7(PhysReg(263), v1);
   Operand desc0(PhysReg(0), s4);

   //>> p_unit_test 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());

   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   //! v1: %0:v[5] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   //! v1: %0:v[6] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   //! v1: %0:v[7] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v0, Operand::zero(), 0, false);
   bld.mubuf(aco_opcode::buffer_load_dword, def_v5, desc0, op_v0, Operand::zero(), 0, false);
   bld.mubuf(aco_opcode::buffer_load_dword, def_v6, desc0, op_v0, Operand::zero(), 0, false);
   bld.mubuf(aco_opcode::buffer_load_dword, def_v7, desc0, op_v0, Operand::zero(), 0, false);
   //! s_waitcnt vmcnt(0)
   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[4], 0
   //! v1: %0:v[5] = buffer_load_dword %0:s[0-3], %0:v[5], 0
   //! v1: %0:v[6] = buffer_load_dword %0:s[0-3], %0:v[6], 0
   //! v1: %0:v[7] = buffer_load_dword %0:s[0-3], %0:v[7], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v4, Operand::zero(), 0, false);
   bld.mubuf(aco_opcode::buffer_load_dword, def_v5, desc0, op_v5, Operand::zero(), 0, false);
   bld.mubuf(aco_opcode::buffer_load_dword, def_v6, desc0, op_v6, Operand::zero(), 0, false);
   bld.mubuf(aco_opcode::buffer_load_dword, def_v7, desc0, op_v7, Operand::zero(), 0, false);
   //! s_waitcnt vmcnt(0)
   //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[4]
   //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[5]
   //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[6]
   //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[7]
   bld.mubuf(aco_opcode::buffer_store_dword, desc0, op_v0, Operand::zero(), op_v4, 0, false);
   bld.mubuf(aco_opcode::buffer_store_dword, desc0, op_v0, Operand::zero(), op_v5, 0, false);
   bld.mubuf(aco_opcode::buffer_store_dword, desc0, op_v0, Operand::zero(), op_v6, 0, false);
   bld.mubuf(aco_opcode::buffer_store_dword, desc0, op_v0, Operand::zero(), op_v7, 0, false);

   //>> p_unit_test 1
   bld.reset(program->create_and_insert_block());
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));

   //! s4: %0:s[4-7] = s_load_dwordx4 %0:s[0-1], 0
   bld.smem(aco_opcode::s_load_dwordx4, Definition(PhysReg(4), s4), Operand(PhysReg(0), s2),
            Operand::zero());
   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v0, Operand::zero(), 0, false);
   //! s_waitcnt lgkmcnt(0) vmcnt(0)
   //! v1: %0:v[5] = buffer_load_dword %0:s[4-7], %0:v[4], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v5, Operand(PhysReg(4), s4), op_v4, Operand::zero(),
             0, false);

   //>> p_unit_test 2
   bld.reset(program->create_and_insert_block());
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));

   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v0, Operand::zero(), 0, false);
   //! v_nop
   bld.vop1(aco_opcode::v_nop);
   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v0, Operand::zero(), 0, false);
   //! s_waitcnt vmcnt(0)
   //! v1: %0:v[5] = buffer_load_dword %0:s[0-3], %0:v[4], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v5, desc0, op_v4, Operand::zero(), 0, false);

   finish_waitcnt_test();
END_TEST

BEGIN_TEST(insert_waitcnt.waw.mixed_vmem_lds.vmem)
   if (!setup_cs(NULL, GFX10))
      return;

   Definition def_v4(PhysReg(260), v1);
   Operand op_v0(PhysReg(256), v1);
   Operand desc0(PhysReg(0), s4);

   //>> BB0
   //! /* logical preds: / linear preds: / kind: top-level, */
   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v0, Operand::zero(), 0, false);

   //>> BB1
   //! /* logical preds: / linear preds: / kind: */
   //! v1: %0:v[4] = ds_read_b32 %0:v[0]
   bld.reset(program->create_and_insert_block());
   bld.ds(aco_opcode::ds_read_b32, def_v4, op_v0);

   bld.reset(program->create_and_insert_block());
   program->blocks[2].linear_preds.push_back(0);
   program->blocks[2].linear_preds.push_back(1);
   program->blocks[2].logical_preds.push_back(0);
   program->blocks[2].logical_preds.push_back(1);

   //>> BB2
   //! /* logical preds: BB0, BB1, / linear preds: BB0, BB1, / kind: uniform, */
   //! s_waitcnt lgkmcnt(0)
   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v0, Operand::zero(), 0, false);

   finish_waitcnt_test();
END_TEST

BEGIN_TEST(insert_waitcnt.waw.mixed_vmem_lds.lds)
   if (!setup_cs(NULL, GFX10))
      return;

   Definition def_v4(PhysReg(260), v1);
   Operand op_v0(PhysReg(256), v1);
   Operand desc0(PhysReg(0), s4);

   //>> BB0
   //! /* logical preds: / linear preds: / kind: top-level, */
   //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc0, op_v0, Operand::zero(), 0, false);

   //>> BB1
   //! /* logical preds: / linear preds: / kind: */
   //! v1: %0:v[4] = ds_read_b32 %0:v[0]
   bld.reset(program->create_and_insert_block());
   bld.ds(aco_opcode::ds_read_b32, def_v4, op_v0);

   bld.reset(program->create_and_insert_block());
   program->blocks[2].linear_preds.push_back(0);
   program->blocks[2].linear_preds.push_back(1);
   program->blocks[2].logical_preds.push_back(0);
   program->blocks[2].logical_preds.push_back(1);

   //>> BB2
   //! /* logical preds: BB0, BB1, / linear preds: BB0, BB1, / kind: uniform, */
   //! s_waitcnt vmcnt(0)
   //! v1: %0:v[4] = ds_read_b32 %0:v[0]
   bld.ds(aco_opcode::ds_read_b32, def_v4, op_v0);

   finish_waitcnt_test();
END_TEST

BEGIN_TEST(insert_waitcnt.waw.vmem_types)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition def_v4(PhysReg(260), v1);
      Operand op_v0(PhysReg(256), v1);
      Operand desc_s4(PhysReg(0), s4);
      Operand desc_s8(PhysReg(8), s8);

      //>> p_unit_test 0
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      //>> p_unit_test 1
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_loadcnt imm:0
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> p_unit_test 2
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_loadcnt imm:0
      //! v1: %0:v[4] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);
      bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v4, desc_s4, Operand(s4), Operand(v1),
               Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)));

      //>> p_unit_test 3
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> p_unit_test 4
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_samplecnt imm:0
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      //>> p_unit_test 5
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_samplecnt imm:0
      //! v1: %0:v[4] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v4, desc_s4, Operand(s4), Operand(v1),
               Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)));

      //>> p_unit_test 6
      //! v1: %0:v[4] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d
      //! v1: %0:v[4] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v4, desc_s4, Operand(s4), Operand(v1),
               Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)));
      bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v4, desc_s4, Operand(s4), Operand(v1),
               Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)));

      //>> p_unit_test 7
      //! v1: %0:v[4] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_bvhcnt imm:0
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v4, desc_s4, Operand(s4), Operand(v1),
               Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)));
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      //>> p_unit_test 8
      //! v1: %0:v[4] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_bvhcnt imm:0
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
      bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v4, desc_s4, Operand(s4), Operand(v1),
               Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)));
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> BB9
      //! /* logical preds: / linear preds: / kind: */
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.reset(program->create_and_insert_block());
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      //>> BB10
      //! /* logical preds: / linear preds: / kind: */
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.reset(program->create_and_insert_block());
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      bld.reset(program->create_and_insert_block());
      program->blocks[11].linear_preds.push_back(9);
      program->blocks[11].linear_preds.push_back(10);
      program->blocks[11].logical_preds.push_back(9);
      program->blocks[11].logical_preds.push_back(10);

      //>> BB11
      //! /* logical preds: BB9, BB10, / linear preds: BB9, BB10, / kind: uniform, */
      //! p_unit_test 9
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      //>> BB12
      //! /* logical preds: / linear preds: / kind: */
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> BB13
      //! /* logical preds: / linear preds: / kind: */
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.reset(program->create_and_insert_block());
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      bld.reset(program->create_and_insert_block());
      program->blocks[14].linear_preds.push_back(12);
      program->blocks[14].linear_preds.push_back(13);
      program->blocks[14].logical_preds.push_back(12);
      program->blocks[14].logical_preds.push_back(13);

      //>> BB14
      //! /* logical preds: BB12, BB13, / linear preds: BB12, BB13, / kind: uniform, */
      //! p_unit_test 10
      //~gfx11! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_samplecnt imm:0
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.vmem)
   if (!setup_cs(NULL, GFX12))
      return;

   Definition def_v4(PhysReg(260), v1);
   Definition def_v5(PhysReg(261), v1);
   Definition def_v6(PhysReg(262), v1);
   Definition def_v7(PhysReg(263), v1);
   Definition def_v8(PhysReg(264), v1);
   Definition def_v9(PhysReg(265), v1);
   Operand op_v0(PhysReg(256), v1);
   Operand op_v4(PhysReg(260), v1);
   Operand op_v5(PhysReg(261), v1);
   Operand op_v6(PhysReg(262), v1);
   Operand op_v7(PhysReg(263), v1);
   Operand op_v8(PhysReg(264), v1);
   Operand op_v9(PhysReg(265), v1);
   Operand desc_s4(PhysReg(0), s4);
   Operand desc_s8(PhysReg(8), s8);

   //>> v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   //! v1: %0:v[5] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
   //! v1: %0:v[6] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d unrm r128
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);
   bld.mimg(aco_opcode::image_sample, def_v5, desc_s8, desc_s4, Operand(v1), op_v0);
   Instruction* instr =
      bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v6, desc_s4, Operand(s4), Operand(v1),
               Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)))
         .instr;
   instr->mimg().unrm = true;
   instr->mimg().r128 = true;

   //! v1: %0:v[7] = image_load %0:s[8-15],  s4: undef,  v1: undef, %0:v[0] 1d
   //! v1: %0:v[8] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
   //! v1: %0:v[9] = image_bvh64_intersect_ray %0:s[0-3],  s4: undef,  v1: undef, %0:v[16-26] 1d unrm r128
   bld.mimg(aco_opcode::image_load, def_v7, desc_s8, Operand(s4), Operand(v1), op_v0, 0x1);
   bld.mimg(aco_opcode::image_sample, def_v8, desc_s8, desc_s4, Operand(v1), op_v0);
   instr = bld.mimg(aco_opcode::image_bvh64_intersect_ray, def_v9, desc_s4, Operand(s4),
                    Operand(v1), Operand(PhysReg(272), RegClass::get(RegType::vgpr, 11 * 4)))
              .instr;
   instr->mimg().unrm = true;
   instr->mimg().r128 = true;

   //! s_wait_loadcnt imm:1
   //! p_unit_test 0, %0:v[4]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0), op_v4);
   //! s_wait_samplecnt imm:1
   //! p_unit_test 1, %0:v[5]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1), op_v5);
   //! s_wait_bvhcnt imm:1
   //! p_unit_test 2, %0:v[6]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2), op_v6);
   //! s_wait_loadcnt imm:0
   //! p_unit_test 3, %0:v[7]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3), op_v7);
   //! s_wait_samplecnt imm:0
   //! p_unit_test 4, %0:v[8]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4), op_v8);
   //! s_wait_bvhcnt imm:0
   //! p_unit_test 5, %0:v[9]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5), op_v9);

   /* Despite not using a sampler, this uses samplecnt. */
   //! v1: %0:v[5] = image_msaa_load %0:s[8-15], s4: undef, v1: undef, %0:v[0] 1d
   //! s_wait_samplecnt imm:0
   //! p_unit_test 6, %0:v[5]
   bld.mimg(aco_opcode::image_msaa_load, def_v5, desc_s8, Operand(s4), Operand(v1), op_v0);
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6), op_v5);

   finish_waitcnt_test();
END_TEST

BEGIN_TEST(insert_waitcnt.lds_smem)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition def_v4(PhysReg(260), v1);
      Definition def_v5(PhysReg(261), v1);
      Definition def_s4(PhysReg(4), s1);
      Definition def_s5(PhysReg(5), s1);
      Operand op_s0(PhysReg(0), s1);
      Operand op_s4(PhysReg(4), s1);
      Operand op_s5(PhysReg(5), s1);
      Operand op_v0(PhysReg(256), v1);
      Operand op_v4(PhysReg(260), v1);
      Operand op_v5(PhysReg(261), v1);
      Operand desc_s4(PhysReg(0), s4);

      //>> v1: %0:v[4] = ds_read_b32 %0:v[0]
      //! s1: %0:s[4] = s_buffer_load_dword %0:s[0-3], %0:s[0]
      //! v1: %0:v[5] = ds_read_b32 %0:v[0]
      //! s1: %0:s[5] = s_buffer_load_dword %0:s[0-3], %0:s[0]
      bld.ds(aco_opcode::ds_read_b32, def_v4, op_v0);
      bld.smem(aco_opcode::s_buffer_load_dword, def_s4, desc_s4, op_s0);
      bld.ds(aco_opcode::ds_read_b32, def_v5, op_v0);
      bld.smem(aco_opcode::s_buffer_load_dword, def_s5, desc_s4, op_s0);

      //~gfx11! s_waitcnt lgkmcnt(1)
      //~gfx12! s_wait_dscnt imm:1
      //! p_unit_test 0, %0:v[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0), op_v4);
      //~gfx11! s_waitcnt lgkmcnt(0)
      //~gfx12! s_wait_kmcnt imm:0
      //! p_unit_test 1, %0:s[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1), op_s4);
      //~gfx12! s_wait_dscnt imm:0
      //! p_unit_test 2, %0:v[5]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2), op_v5);
      //! p_unit_test 3, %0:s[5]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3), op_s5);

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.sendmsg_smem)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition def_s4(PhysReg(4), s1);
      Definition def_s5(PhysReg(5), s1);
      Definition def_s6(PhysReg(6), s1);
      Definition def_s7(PhysReg(7), s1);
      Operand op_s0(PhysReg(0), s1);
      Operand op_s4(PhysReg(4), s1);
      Operand op_s5(PhysReg(5), s1);
      Operand op_s6(PhysReg(6), s1);
      Operand op_s7(PhysReg(7), s1);
      Operand desc_s4(PhysReg(0), s4);

      //>> s1: %0:s[4] = s_sendmsg_rtn_b32 3 sendmsg(rtn_get_realtime)
      //! s1: %0:s[5] = s_buffer_load_dword %0:s[0-3], %0:s[0]
      //! s1: %0:s[6] = s_sendmsg_rtn_b32 3 sendmsg(rtn_get_realtime)
      //! s1: %0:s[7] = s_buffer_load_dword %0:s[0-3], %0:s[0]
      bld.sop1(aco_opcode::s_sendmsg_rtn_b32, def_s4, Operand::c32(sendmsg_rtn_get_realtime));
      bld.smem(aco_opcode::s_buffer_load_dword, def_s5, desc_s4, op_s0);
      bld.sop1(aco_opcode::s_sendmsg_rtn_b32, def_s6, Operand::c32(sendmsg_rtn_get_realtime));
      bld.smem(aco_opcode::s_buffer_load_dword, def_s7, desc_s4, op_s0);

      //~gfx12! s_wait_kmcnt imm:1
      //~gfx11! s_waitcnt lgkmcnt(1)
      //! p_unit_test 0, %0:s[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0), op_s4);
      //~gfx12! s_wait_kmcnt imm:0
      //~gfx11! s_waitcnt lgkmcnt(0)
      //! p_unit_test 1, %0:s[5]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1), op_s5);
      //! p_unit_test 2, %0:s[6]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2), op_s6);
      //! p_unit_test 3, %0:s[7]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3), op_s7);

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.vmem_ds)
   if (!setup_cs(NULL, GFX12))
      return;

   Definition def_v4(PhysReg(260), v1);
   Definition def_v5(PhysReg(261), v1);
   Operand op_v0(PhysReg(256), v1);
   Operand op_v1(PhysReg(257), v1);
   Operand op_v4(PhysReg(260), v1);
   Operand op_v5(PhysReg(261), v1);
   Operand desc_s4(PhysReg(0), s4);

   program->workgroup_size = 128;
   program->wgp_mode = true;

   //>> v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
   //! v1: %0:v[5] = ds_read_b32 %0:v[0]
   bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);
   bld.ds(aco_opcode::ds_read_b32, def_v5, op_v0);

   //! s_wait_loadcnt_dscnt dscnt(0) loadcnt(0)
   //! p_unit_test 0, %0:v[4], %0:v[5]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0), op_v4, op_v5);

   //! buffer_store_dword %0:s[0-3], %0:v[0], 0, %0:v[1] storage:buffer
   //! v1: %0:v[5] = ds_write_b32 %0:v[0], %0:v[1] storage:shared
   Instruction* instr =
      bld.mubuf(aco_opcode::buffer_store_dword, desc_s4, op_v0, Operand::zero(), op_v1, 0, false)
         .instr;
   instr->mubuf().sync = memory_sync_info(storage_buffer);
   instr = bld.ds(aco_opcode::ds_write_b32, def_v5, op_v0, op_v1).instr;
   instr->ds().sync = memory_sync_info(storage_shared);

   //! s_wait_storecnt_dscnt dscnt(0) storecnt(0)
   bld.barrier(aco_opcode::p_barrier,
               memory_sync_info(storage_buffer | storage_shared, semantic_acqrel, scope_workgroup));

   finish_waitcnt_test();
END_TEST
