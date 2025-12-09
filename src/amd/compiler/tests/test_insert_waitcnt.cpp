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
      //~gfx12! s_wait_loadcnt imm:0
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
      //~gfx12! s_wait_samplecnt imm:0
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
      //~gfx12! s_wait_bvhcnt imm:0
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
      //~gfx12! s_wait_loadcnt imm:0
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
      //~gfx12! s_wait_loadcnt imm:0
      //~gfx12! s_wait_samplecnt imm:0
      //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.waw.point_sample_accel)
   for (radeon_family family : {CHIP_STRIX1, CHIP_GFX1153}) {
      if (!setup_cs(NULL, GFX11_5, family, family == CHIP_GFX1153 ? "_3" : "_0"))
         continue;

      Definition def_v4(PhysReg(260), v1);
      Operand op_v0(PhysReg(256), v1);
      Operand desc_s4(PhysReg(0), s4);
      Operand desc_s8(PhysReg(8), s8);

      /* image_sample has point sample acceleration, but image_sample_b does not. Both are VMEM
       * sample instructions. */

      //>> p_unit_test 0
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      //~gfx11_5_0! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> p_unit_test 1
      //! v1: %0:v[4] = image_sample_b %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      //~gfx11_5_0! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.mimg(aco_opcode::image_sample_b, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> p_unit_test 2
      //! v1: %0:v[4] = image_load %0:s[8-15], s4: undef, v1: undef, %0:v[0] 1d
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.mimg(aco_opcode::image_load, def_v4, desc_s8, Operand(s4), Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> p_unit_test 3
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      //~gfx11_5_0! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = image_sample_b %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_sample_b, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> p_unit_test 4
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = image_load %0:s[8-15], s4: undef, v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_load, def_v4, desc_s8, Operand(s4), Operand(v1), op_v0);

      //>> p_unit_test 5
      //! v1: %0:v[4] = image_sample_b %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      //! v1: %0:v[4] = image_sample_b %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.mimg(aco_opcode::image_sample_b, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_sample_b, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);

      //>> p_unit_test 5
      //! v1: %0:v[4] = image_load %0:s[8-15], s4: undef, v1: undef, %0:v[0] 1d
      //! v1: %0:v[4] = image_load %0:s[8-15], s4: undef, v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.mimg(aco_opcode::image_load, def_v4, desc_s8, Operand(s4), Operand(v1), op_v0);
      bld.mimg(aco_opcode::image_load, def_v4, desc_s8, Operand(s4), Operand(v1), op_v0);

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
               memory_sync_info(storage_buffer | storage_shared, semantic_acqrel, scope_workgroup),
               scope_workgroup);

   finish_waitcnt_test();
END_TEST

BEGIN_TEST(insert_waitcnt.waw.vmem_ds_valu)
   for (amd_gfx_level gfx : {GFX10_3, GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition def_v4(PhysReg(260), v1);
      Operand op_v0(PhysReg(256), v1);
      Operand desc_s4(PhysReg(0), s4);

      emit_divergent_if_else(
         program.get(), bld, Operand::c64(1),
         [&]()
         {
            //>> p_unit_test 1
            //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
            bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0,
                      false);
         },
         [&]()
         {
            //>> p_unit_test 2
            //~gfx11! s_waitcnt vmcnt(0)
            //~gfx12! s_wait_loadcnt imm:0
            //! v1: %0:v[4] = v_mov_b32 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
            bld.vop1(aco_opcode::v_mov_b32, def_v4, Operand::zero());
         });
      //>> p_unit_test 3
      //~gfx(10_3|11)! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_loadcnt imm:0
      //! p_unit_test %0:v[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.pseudo(aco_opcode::p_unit_test, Operand(PhysReg(260), v1));

      emit_divergent_if_else(
         program.get(), bld, Operand::c64(1),
         [&]()
         {
            //>> p_unit_test 4
            //! v1: %0:v[4] = ds_read_b32 %0:v[0]
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
            bld.ds(aco_opcode::ds_read_b32, def_v4, op_v0);
         },
         [&]()
         {
            //>> p_unit_test 5
            //~gfx11! s_waitcnt lgkmcnt(0)
            //~gfx12! s_wait_dscnt imm:0
            //! v1: %0:v[4] = v_mov_b32 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
            bld.vop1(aco_opcode::v_mov_b32, def_v4, Operand::zero());
         });
      //>> p_unit_test 6
      //~gfx(10_3|11)! s_waitcnt lgkmcnt(0)
      //~gfx12! s_wait_dscnt imm:0
      //! p_unit_test %0:v[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.pseudo(aco_opcode::p_unit_test, Operand(PhysReg(260), v1));

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.waw.vmem_different_halves)
   if (!setup_cs(NULL, GFX12))
      return;

   Definition def_v4_lo(PhysReg(260), v2b);
   Definition def_v4_hi(PhysReg(260).advance(2), v2b);
   Operand op_v0(PhysReg(256), v1);
   Operand desc_s4(PhysReg(0), s4);
   Operand desc_s8(PhysReg(8), s8);

   //>> p_unit_test 0
   //! v2b: %0:v[4][0:16] = buffer_load_short_d16 %0:s[0-3], %0:v[0], 0
   //! v2b: %0:v[4][16:32] = buffer_load_short_d16_hi %0:s[0-3], %0:v[0], 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
   bld.mubuf(aco_opcode::buffer_load_short_d16, def_v4_lo, desc_s4, op_v0, Operand::zero(), 0,
             false);
   bld.mubuf(aco_opcode::buffer_load_short_d16_hi, def_v4_hi, desc_s4, op_v0, Operand::zero(), 0,
             false);

   //>> p_unit_test 1
   //! v2b: %0:v[4][16:32] = buffer_load_short_d16_hi %0:s[0-3], %0:v[0], 0
   //! v2b: %0:v[4][0:16] = buffer_load_short_d16 %0:s[0-3], %0:v[0], 0
   bld.reset(program->create_and_insert_block());
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.mubuf(aco_opcode::buffer_load_short_d16_hi, def_v4_hi, desc_s4, op_v0, Operand::zero(), 0,
             false);
   bld.mubuf(aco_opcode::buffer_load_short_d16, def_v4_lo, desc_s4, op_v0, Operand::zero(), 0,
             false);

   //>> p_unit_test 2
   //! v2b: %0:v[4][0:16] = buffer_load_short_d16 %0:s[0-3], %0:v[0], 0
   //! s_wait_loadcnt imm:0
   //! v2b: %0:v[4][0:16] = buffer_load_short_d16 %0:s[0-3], %0:v[0], 0
   bld.reset(program->create_and_insert_block());
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.mubuf(aco_opcode::buffer_load_short_d16, def_v4_lo, desc_s4, op_v0, Operand::zero(), 0,
             false);
   bld.mubuf(aco_opcode::buffer_load_short_d16, def_v4_lo, desc_s4, op_v0, Operand::zero(), 0,
             false);

   //>> p_unit_test 3
   //! v2b: %0:v[4][16:32] = buffer_load_short_d16_hi %0:s[0-3], %0:v[0], 0
   //! s_wait_loadcnt imm:0
   //! v2b: %0:v[4][16:32] = buffer_load_short_d16_hi %0:s[0-3], %0:v[0], 0
   bld.reset(program->create_and_insert_block());
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.mubuf(aco_opcode::buffer_load_short_d16_hi, def_v4_hi, desc_s4, op_v0, Operand::zero(), 0,
             false);
   bld.mubuf(aco_opcode::buffer_load_short_d16_hi, def_v4_hi, desc_s4, op_v0, Operand::zero(), 0,
             false);

   //>> p_unit_test 4
   //! v2b: %0:v[4][0:16] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d d16
   //! s_wait_samplecnt imm:0
   //! v2b: %0:v[4][16:32] = buffer_load_short_d16_hi %0:s[0-3], %0:v[0], 0
   bld.reset(program->create_and_insert_block());
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   Instruction* instr =
      bld.mimg(aco_opcode::image_sample, def_v4_lo, desc_s8, desc_s4, Operand(v1), op_v0);
   instr->mimg().dmask = 0x1;
   instr->mimg().d16 = true;
   bld.mubuf(aco_opcode::buffer_load_short_d16_hi, def_v4_hi, desc_s4, op_v0, Operand::zero(), 0,
             false);

   //>> p_unit_test 5
   //! v2b: %0:v[4][16:32] = buffer_load_short_d16_hi %0:s[0-3], %0:v[0], 0
   //! s_wait_loadcnt imm:0
   //! v2b: %0:v[4][0:16] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d d16
   bld.reset(program->create_and_insert_block());
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.mubuf(aco_opcode::buffer_load_short_d16_hi, def_v4_hi, desc_s4, op_v0, Operand::zero(), 0,
             false);
   instr = bld.mimg(aco_opcode::image_sample, def_v4_lo, desc_s8, desc_s4, Operand(v1), op_v0);
   instr->mimg().dmask = 0x1;
   instr->mimg().d16 = true;

   finish_waitcnt_test();
END_TEST

BEGIN_TEST(insert_waitcnt.waw.vmem_different_lanes)
   for (amd_gfx_level gfx : {GFX10_3, GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition def_v4(PhysReg(260), v1);
      Operand op_v0(PhysReg(256), v1);
      Operand desc_s4(PhysReg(0), s4);
      Operand desc_s8(PhysReg(8), s8);

      emit_divergent_if_else(
         program.get(), bld, Operand::c64(1),
         [&]()
         {
            //>> p_unit_test 1
            //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
            bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0,
                      false);
         },
         [&]()
         {
            //>> p_unit_test 2
            //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
            bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0,
                      false);
         });
      //>> p_unit_test 3
      //~gfx(10_3|11)! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_loadcnt imm:0
      //! p_unit_test %0:v[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.pseudo(aco_opcode::p_unit_test, Operand(PhysReg(260), v1));

      emit_divergent_if_else(
         program.get(), bld, Operand::c64(1),
         [&]()
         {
            //>> p_unit_test 4
            //! v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
            bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0,
                      false);
         },
         [&]()
         {
            //>> p_unit_test 5
            //~gfx12! s_wait_loadcnt imm:0
            //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3],  v1: undef, %0:v[0] 1d
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
            bld.mimg(aco_opcode::image_sample, def_v4, desc_s8, desc_s4, Operand(v1), op_v0);
         });
      //>> p_unit_test 6
      //~gfx(10_3|11)! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_loadcnt imm:0
      //~gfx12! s_wait_samplecnt imm:0
      //! p_unit_test %0:v[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.pseudo(aco_opcode::p_unit_test, Operand(PhysReg(260), v1));

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.divergent_branch.inc_counter)
   for (amd_gfx_level gfx : {GFX10_3, GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition def_v4(PhysReg(260), v1);
      Definition def_v5(PhysReg(261), v1);
      Operand op_v0(PhysReg(256), v1);
      Operand desc_s4(PhysReg(0), s4);
      Operand desc_s8(PhysReg(8), s8);

      //>> v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      emit_divergent_if_else(
         program.get(), bld, Operand::c64(1),
         [&]()
         {
            //>> p_unit_test 1
            //! v1: %0:v[5] = buffer_load_dword %0:s[0-3], %0:v[0], 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
            bld.mubuf(aco_opcode::buffer_load_dword, def_v5, desc_s4, op_v0, Operand::zero(), 0,
                      false);
         },
         [&]()
         {
            //>> p_unit_test 2
            //! v1: %0:v[5] = buffer_load_dword %0:s[0-3], %0:v[0], 0
            bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
            bld.mubuf(aco_opcode::buffer_load_dword, def_v5, desc_s4, op_v0, Operand::zero(), 0,
                      false);
         });
      //>> p_unit_test 3
      //~gfx(10_3|11)! s_waitcnt vmcnt(1)
      //~gfx12! s_wait_loadcnt imm:1
      //! p_unit_test %0:v[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.pseudo(aco_opcode::p_unit_test, Operand(PhysReg(260), v1));

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.divergent_branch.no_skip)
   for (amd_gfx_level gfx : {GFX10_3, GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition def_v4(PhysReg(260), v1);
      Operand op_v0(PhysReg(256), v1);
      Operand desc_s4(PhysReg(0), s4);
      Operand desc_s8(PhysReg(8), s8);

      //>> v1: %0:v[4] = buffer_load_dword %0:s[0-3], %0:v[0], 0
      bld.mubuf(aco_opcode::buffer_load_dword, def_v4, desc_s4, op_v0, Operand::zero(), 0, false);

      //>> p_unit_test 1
      //~gfx(10_3|11)! s_waitcnt vmcnt(0)
      //~gfx12! s_wait_loadcnt imm:0
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      program->blocks[1].linear_preds.push_back(0);
      program->blocks[1].logical_preds.push_back(0);
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.pseudo(aco_opcode::p_unit_test, Operand(PhysReg(260), v1));

      //>> p_unit_test 2
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      program->blocks[2].linear_preds.push_back(1);
      program->blocks[2].logical_preds.push_back(1);
      program->blocks[2].logical_preds.push_back(0);
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.pseudo(aco_opcode::p_unit_test, Operand(PhysReg(260), v1));

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.flat.wait_zero)
   for (amd_gfx_level gfx : {GFX9, GFX10}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dest0(PhysReg(260), v1);
      Definition dest1(PhysReg(261), v1);
      Operand offset(PhysReg(256), v1);
      Operand addr(PhysReg(256), v2);

      //>> p_unit_test 0
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //~gfx9! s_waitcnt vmcnt(0)
      //~gfx10! s_waitcnt vmcnt(1)
      //! p_unit_test %0:v[4]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      bld.global(aco_opcode::global_load_dword, dest0, addr, Operand(s1));
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 1
      //! v1: %0:v[4] = ds_read_b32 %0:v[0]
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //~gfx9! s_waitcnt lgkmcnt(0)
      //~gfx10! s_waitcnt lgkmcnt(1)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.ds(aco_opcode::ds_read_b32, dest0, offset);
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 2
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef
      //~gfx9! s_waitcnt lgkmcnt(0) vmcnt(0)
      //~gfx10! s_waitcnt lgkmcnt(0) vmcnt(1)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.flat(aco_opcode::flat_load_dword, dest0, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.global(aco_opcode::global_load_dword, dest1, addr, Operand(s1));
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 3
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //! v1: %0:v[5] = ds_read_b32 %0:v[0]
      //~gfx9! s_waitcnt lgkmcnt(0) vmcnt(0)
      //~gfx10! s_waitcnt lgkmcnt(1) vmcnt(0)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.flat(aco_opcode::flat_load_dword, dest0, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.ds(aco_opcode::ds_read_b32, dest1, offset);
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 4
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //~gfx9! s_waitcnt lgkmcnt(0) vmcnt(0)
      //~gfx10! s_waitcnt lgkmcnt(1) vmcnt(1)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.flat(aco_opcode::flat_load_dword, dest0, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 5
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef
      //! s_waitcnt vmcnt(1)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.global(aco_opcode::global_load_dword, dest0, addr, Operand(s1));
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1));
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 6
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef
      //! s_waitcnt vmcnt(1)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.flat(aco_opcode::flat_load_dword, dest0, addr, Operand(s1));
      bld.global(aco_opcode::global_load_dword, dest1, addr, Operand(s1));
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 7
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef
      //! s_waitcnt vmcnt(1)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      bld.flat(aco_opcode::flat_load_dword, dest0, addr, Operand(s1));
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1));
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      //>> p_unit_test 8
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //~gfx9! s_waitcnt vmcnt(0)
      //~gfx10! s_waitcnt vmcnt(1)
      //! p_unit_test %0:v[4]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
      bld.flat(aco_opcode::flat_load_dword, dest0, addr, Operand(s1));
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.pseudo(aco_opcode::p_unit_test, Operand(dest0.physReg(), v1));

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.flat.waw)
   for (amd_gfx_level gfx : {GFX9, GFX10}) {
      if (!setup_cs(NULL, gfx))
         continue;

      /* Flat might use either LDS or VMEM, so WaW always needs a wait. */
      Definition dest(PhysReg(260), v1);
      Operand offset(PhysReg(256), v1);
      Operand addr(PhysReg(256), v2);
      Operand desc_s4(PhysReg(0), s4);
      Operand desc_s8(PhysReg(8), s8);

      //>> p_unit_test 0
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      bld.global(aco_opcode::global_load_dword, dest, addr, Operand(s1));
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1)).instr->flat().may_use_lds = true;

      //>> p_unit_test 1
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //! s_waitcnt lgkmcnt(0) vmcnt(0)
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.global(aco_opcode::global_load_dword, dest, addr, Operand(s1));

      //>> p_unit_test 2
      //! v1: %0:v[4] = ds_read_b32 %0:v[0]
      //! s_waitcnt lgkmcnt(0)
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.ds(aco_opcode::ds_read_b32, dest, offset);
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1)).instr->flat().may_use_lds = true;

      //>> p_unit_test 3
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = ds_read_b32 %0:v[0]
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.ds(aco_opcode::ds_read_b32, dest, offset);

      /* In theory, we don't need a wait here, but we don't optimize this. */
      //>> p_unit_test 4
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //! s_waitcnt lgkmcnt(0) vmcnt(0)
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1)).instr->flat().may_use_lds = true;

      //>> p_unit_test 5
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //! s_waitcnt lgkmcnt(0) vmcnt(0)
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.mimg(aco_opcode::image_sample, dest, desc_s8, desc_s4, Operand(v1), offset);

      //>> p_unit_test 6
      //! v1: %0:v[4] = flat_load_dword %0:v[0-1], s1: undef
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[4] = image_sample %0:s[8-15], %0:s[0-3], v1: undef, %0:v[0] 1d
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.flat(aco_opcode::flat_load_dword, dest, addr, Operand(s1));
      bld.mimg(aco_opcode::image_sample, dest, desc_s8, desc_s4, Operand(v1), offset);

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.flat.barrier)
   for (amd_gfx_level gfx : {GFX9, GFX10}) {
      if (!setup_cs(NULL, gfx))
         continue;

      Definition dest0(PhysReg(260), v1);
      Definition dest1(PhysReg(261), v1);
      Operand addr(PhysReg(256), v2);
      Operand data(PhysReg(256), v1);

      //>> p_unit_test 0
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef may_use_lds
      //~gfx9! s_waitcnt vmcnt(0)
      //~gfx10! s_waitcnt vmcnt(1)
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      bld.global(aco_opcode::global_load_dword, dest0, addr, Operand(s1), 0,
                 memory_sync_info(storage_buffer));
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1)).instr->flat().may_use_lds = true;
      bld.barrier(aco_opcode::p_barrier,
                  memory_sync_info(storage_buffer, semantic_acqrel, scope_device), scope_workgroup);

      //>> p_unit_test 1
      //! v1: %0:v[5] = flat_load_dword %0:v[0-1], s1: undef may_use_lds storage:buffer
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef
      //~gfx9! s_waitcnt lgkmcnt(0) vmcnt(0)
      //~gfx10! s_waitcnt lgkmcnt(0) vmcnt(1)
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.flat(aco_opcode::flat_load_dword, dest1, addr, Operand(s1), 0,
               memory_sync_info(storage_buffer)).instr->flat().may_use_lds = true;
      bld.global(aco_opcode::global_load_dword, dest0, addr, Operand(s1), 0);
      bld.barrier(aco_opcode::p_barrier,
                  memory_sync_info(storage_buffer, semantic_acqrel, scope_device), scope_workgroup);

      //>> p_unit_test 2
      //! flat_store_dword %0:v[0-1], s1: undef, %0:v[0] may_use_lds storage:buffer
      //~gfx9! s_waitcnt lgkmcnt(0) vmcnt(0)
      //~gfx10! s_waitcnt_vscnt %0:null imm:0
      //~gfx10! s_waitcnt lgkmcnt(0)
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.flat(aco_opcode::flat_store_dword, addr, Operand(s1), data, 0,
               memory_sync_info(storage_buffer)).instr->flat().may_use_lds = true;
      bld.barrier(aco_opcode::p_barrier,
                  memory_sync_info(storage_buffer, semantic_acqrel, scope_device), scope_workgroup);

      finish_waitcnt_test();
   }
END_TEST

static void
barrier(unsigned storage, unsigned semantics, sync_scope scope,
        sync_scope exec_scope = scope_invocation)
{
   bld.barrier(aco_opcode::p_barrier, memory_sync_info(storage, semantics, scope), exec_scope);
}

static Definition&
load_global(unsigned semantics = 0, sync_scope scope = scope_invocation)
{
   Definition dest0(PhysReg(260), v1);
   Operand addr(PhysReg(256), v2);
   return bld
      .global(aco_opcode::global_load_dword, dest0, addr, Operand(s1), 0,
              memory_sync_info(storage_buffer, semantics, scope))
      .def(0);
}

static void
store_global(unsigned semantics = 0, sync_scope scope = scope_invocation)
{
   Operand addr(PhysReg(256), v2);
   Operand data(PhysReg(256), v1);
   bld.global(aco_opcode::global_store_dword, addr, Operand(s1), data, 0,
              memory_sync_info(storage_buffer, semantics, scope));
}

static Definition&
load_shared(unsigned semantics = 0, sync_scope scope = scope_invocation)
{
   Definition dest0(PhysReg(260), v1);
   Operand offset(PhysReg(256), v1);
   Builder::Result res = bld.ds(aco_opcode::ds_read_b32, dest0, offset);
   res.instr->ds().sync = memory_sync_info(storage_shared, semantics, scope);
   return res.def(0);
}

static void
store_shared(unsigned semantics = 0, sync_scope scope = scope_invocation)
{
   Operand offset(PhysReg(256), v1);
   Operand data(PhysReg(256), v1);
   bld.ds(aco_opcode::ds_write_b32, offset, data).instr->ds().sync =
      memory_sync_info(storage_shared, semantics, scope);
}

struct barrier_test_variant {
   unsigned workgroup_size;
   bool wgp;
   const char* name;
};

static const barrier_test_variant barrier_test_variants[] = {
   {64, false, "_wg64cu"},
   {128, false, "_wg128cu"},
   {64, true, "_wg64wgp"},
   {128, true, "_wg128wgp"},
};

BEGIN_TEST(insert_waitcnt.barrier.release)
   for (barrier_test_variant var : barrier_test_variants) {
      if (!setup_cs(NULL, GFX10, CHIP_UNKNOWN, var.name))
         continue;

      program->workgroup_size = var.workgroup_size;
      program->wgp_mode = var.wgp;

      Definition dest0(PhysReg(260), v1);
      Definition dest1(PhysReg(261), v1);
      Operand addr(PhysReg(256), v2);
      Operand offset(PhysReg(256), v1);
      Operand data(PhysReg(256), v1);

      /* global->global, device scope */
      //>> p_unit_test 0
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //! s_waitcnt_vscnt %0:null imm:0
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      store_global();
      barrier(storage_buffer, semantic_release, scope_device);
      store_global(semantic_atomic);

      //>> p_unit_test 1
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      //! s_waitcnt vmcnt(0)
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      load_global();
      barrier(storage_buffer, semantic_release, scope_device);
      store_global(semantic_atomic);

      /* global->global, workgroup scope */
      //>> p_unit_test 2
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //~gfx10_wg128wgp! s_waitcnt_vscnt %0:null imm:0
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      store_global();
      barrier(storage_buffer, semantic_release, scope_workgroup);
      store_global(semantic_atomic);

      //>> p_unit_test 3
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      //~gfx10_wg128wgp! s_waitcnt vmcnt(0)
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      load_global();
      barrier(storage_buffer, semantic_release, scope_workgroup);
      store_global(semantic_atomic);

      /* shared->shared */
      //>> p_unit_test 4
      //! ds_write_b32 %0:v[0], %0:v[0] storage:shared
      //! ds_write_b32 %0:v[0], %0:v[0] storage:shared semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      store_shared();
      barrier(storage_shared, semantic_release, scope_workgroup);
      store_shared(semantic_atomic);

      //>> p_unit_test 5
      //! v1: %0:v[4] = ds_read_b32 %0:v[0] storage:shared
      //! ds_write_b32 %0:v[0], %0:v[0] storage:shared semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      load_shared();
      barrier(storage_shared, semantic_release, scope_workgroup);
      store_shared(semantic_atomic);

      /* shared->global */
      //>> p_unit_test 6
      //! ds_write_b32 %0:v[0], %0:v[0] storage:shared
      //~gfx10_wg128(cu|wgp)! s_waitcnt lgkmcnt(0)
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      store_shared();
      barrier(storage_buffer | storage_shared, semantic_release, scope_workgroup);
      store_global(semantic_atomic);

      /* global->shared */
      //>> p_unit_test 7
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //~gfx10_wg128(cu|wgp)! s_waitcnt_vscnt %0:null imm:0
      //! ds_write_b32 %0:v[0], %0:v[0] storage:shared semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      store_global();
      barrier(storage_buffer | storage_shared, semantic_release, scope_workgroup);
      store_shared(semantic_atomic);

      /* global->global, device scope, release in the atomic */
      //>> p_unit_test 8
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //! s_waitcnt_vscnt %0:null imm:0
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:release,atomic scope:device
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
      store_global();
      store_global(semantic_atomic | semantic_release, scope_device);

      /* global->global, workgroup scope, control barrier */
      //>> p_unit_test 9
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //~gfx10_wg128wgp! s_waitcnt_vscnt %0:null imm:0
      //~gfx10_wg128cu! s_waitcnt_depctr vm_vsrc(0)
      //! s_barrier
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
      store_global();
      barrier(storage_buffer, semantic_release, scope_workgroup);
      barrier(0, 0, scope_invocation, scope_workgroup);
      bld.sopp(aco_opcode::s_barrier);

      /* shared->shared, workgroup scope, control barrier */
      //>> p_unit_test 10
      //! ds_write_b32 %0:v[0], %0:v[0] storage:shared
      //~gfx10_wg128cu! s_waitcnt_depctr vm_vsrc(0)
      //~gfx10_wg128wgp! s_waitcnt lgkmcnt(0)
      //! s_barrier
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
      store_shared();
      barrier(storage_shared, semantic_release, scope_workgroup);
      barrier(0, 0, scope_invocation, scope_workgroup);
      bld.sopp(aco_opcode::s_barrier);

      /* global->global, device scope, delayed waitcnt */
      //>> p_unit_test 11
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //! v_nop
      //! s_waitcnt_vscnt %0:null imm:0
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(11));
      store_global();
      barrier(storage_buffer, semantic_release, scope_device);
      bld.vop1(aco_opcode::v_nop);
      store_global(semantic_atomic);

      //>> p_unit_test 12
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //! v_nop
      //! s_waitcnt_vscnt %0:null imm:0
      //! v_nop
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(12));
      store_global();
      barrier(storage_buffer, semantic_release, scope_device);
      bld.vop1(aco_opcode::v_nop);
      wait_imm vs_imm;
      vs_imm.vs = 0;
      vs_imm.build_waitcnt(bld);
      bld.vop1(aco_opcode::v_nop);
      store_global(semantic_atomic);

      //>> p_unit_test 13
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:private
      //! v_nop
      //! s_waitcnt_vscnt %0:null imm:1
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer semantics:atomic
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(13));
      store_global();
      barrier(storage_buffer, semantic_release, scope_device);
      store_global(semantic_private);
      bld.vop1(aco_opcode::v_nop); /* break up clause */
      store_global(semantic_atomic);

      finish_waitcnt_test();
   }
END_TEST

BEGIN_TEST(insert_waitcnt.barrier.acquire)
   for (barrier_test_variant var : barrier_test_variants) {
      if (!setup_cs(NULL, GFX10, CHIP_UNKNOWN, var.name))
         continue;

      program->workgroup_size = var.workgroup_size;
      program->wgp_mode = var.wgp;

      Definition dest0(PhysReg(260), v1);
      Definition dest1(PhysReg(261), v1);
      Definition dest2(PhysReg(262), v1);
      Operand addr(PhysReg(256), v2);
      Operand offset(PhysReg(256), v1);
      Operand data(PhysReg(256), v1);

      /* global->global, device scope */
      //>> p_unit_test 0
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(0));
      load_global(semantic_atomic);
      barrier(storage_buffer, semantic_acquire, scope_device);
      load_global() = dest1;

      //>> p_unit_test 1
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //! s_waitcnt vmcnt(0)
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      load_global(semantic_atomic);
      barrier(storage_buffer, semantic_acquire, scope_device);
      store_global();

      /* global->global, workgroup scope */
      //>> p_unit_test 2
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //~gfx10_wg128wgp! s_waitcnt vmcnt(0)
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      load_global(semantic_atomic);
      barrier(storage_buffer, semantic_acquire, scope_workgroup);
      load_global() = dest1;

      //>> p_unit_test 3
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //~gfx10_wg128wgp! s_waitcnt vmcnt(0)
      //! global_store_dword %0:v[0-1], s1: undef, %0:v[0] storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      load_global(semantic_atomic);
      barrier(storage_buffer, semantic_acquire, scope_workgroup);
      store_global();

      /* shared->shared */
      //>> p_unit_test 4
      //! v1: %0:v[4] = ds_read_b32 %0:v[0] storage:shared semantics:atomic
      //! v1: %0:v[5] = ds_read_b32 %0:v[0] storage:shared
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      load_shared(semantic_atomic);
      barrier(storage_shared, semantic_acquire, scope_workgroup);
      load_shared() = dest1;

      //>> p_unit_test 5
      //! v1: %0:v[4] = ds_read_b32 %0:v[0] storage:shared semantics:atomic
      //! ds_write_b32 %0:v[0], %0:v[0] storage:shared
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      load_shared(semantic_atomic);
      barrier(storage_shared, semantic_acquire, scope_workgroup);
      store_shared();

      /* shared->global */
      //>> p_unit_test 6
      //! v1: %0:v[4] = ds_read_b32 %0:v[0] storage:shared semantics:atomic
      //~gfx10_wg128(cu|wgp)! s_waitcnt lgkmcnt(0)
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      load_shared(semantic_atomic);
      barrier(storage_buffer | storage_shared, semantic_acquire, scope_workgroup);
      load_global() = dest1;

      /* global->shared */
      //>> p_unit_test 7
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //~gfx10_wg128(cu|wgp)! s_waitcnt vmcnt(0)
      //! v1: %0:v[5] = ds_read_b32 %0:v[0] storage:shared
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      load_global(semantic_atomic);
      barrier(storage_buffer | storage_shared, semantic_acquire, scope_workgroup);
      load_shared() = dest1;

      /* global->global, device scope, acquire in the atomic */
      //>> p_unit_test 8
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:acquire,atomic scope:device
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(8));
      load_global(semantic_atomic | semantic_acquire, scope_device);
      load_global() = dest1;

      /* global->global, workgroup scope, control barrier */
      //>> p_unit_test 9
      //! s_barrier
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(9));
      barrier(0, 0, scope_invocation, scope_workgroup);
      bld.sopp(aco_opcode::s_barrier);
      barrier(storage_buffer, semantic_acquire, scope_workgroup);
      load_global();

      /* global->global, device scope, delayed waitcnt */
      //>> p_unit_test 10
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //! v_nop
      //! s_waitcnt vmcnt(0)
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(10));
      load_global(semantic_atomic);
      barrier(storage_buffer, semantic_acquire, scope_device);
      bld.vop1(aco_opcode::v_nop);
      load_global() = dest1;

      //>> p_unit_test 11
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //! v_nop
      //! s_waitcnt vmcnt(0)
      //! v_nop
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(11));
      load_global(semantic_atomic);
      barrier(storage_buffer, semantic_acquire, scope_device);
      bld.vop1(aco_opcode::v_nop);
      wait_imm vm_imm;
      vm_imm.vm = 0;
      vm_imm.build_waitcnt(bld);
      bld.vop1(aco_opcode::v_nop);
      load_global() = dest1;

      //>> p_unit_test 12
      //! v1: %0:v[4] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:atomic
      //! v1: %0:v[5] = global_load_dword %0:v[0-1], s1: undef storage:buffer semantics:private
      //! v_nop
      //! s_waitcnt vmcnt(1)
      //! v1: %0:v[6] = global_load_dword %0:v[0-1], s1: undef storage:buffer
      bld.reset(program->create_and_insert_block());
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(12));
      load_global(semantic_atomic);
      barrier(storage_buffer, semantic_acquire, scope_device);
      load_global(semantic_private) = dest1;
      bld.vop1(aco_opcode::v_nop); /* break up clause */
      load_global() = dest2;

      finish_waitcnt_test();
   }
END_TEST
