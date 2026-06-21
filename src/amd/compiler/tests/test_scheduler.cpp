/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "helpers.h"

using namespace aco;

BEGIN_TEST(vopd_sched.commutative)
   if (!setup_cs(NULL, GFX11, CHIP_UNKNOWN, "", 32))
      return;

   PhysReg reg_v0{256};
   PhysReg reg_v1{257};
   PhysReg reg_v2{258};
   PhysReg reg_v3{259};

   //>> p_unit_test 0
   //! v1: %0:v[1] = v_dual_add_f32 %0:v[3], %0:v[2] :: v1: %0:v[0] = v_dual_mul_f32 %0:v[2], %0:v[3]
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
   bld.vop2(aco_opcode::v_mul_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));
   bld.vop2(aco_opcode::v_add_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));

   /* Neither of these opcodes are commutative. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 1
   //! v1: %0:v[0] = v_fmamk_f32 %0:v[2], %0:v[3], 0
   //! v1: %0:v[1] = v_lshlrev_b32 %0:v[2], %0:v[3]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vop2(aco_opcode::v_fmamk_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1), Operand::zero());
   bld.vop2(aco_opcode::v_lshlrev_b32, Definition(reg_v1, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));

   /* We have to change the opcode for subtractions. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 2
   //! v1: %0:v[1] = v_dual_subrev_f32 %0:v[3], %0:v[2] :: v1: %0:v[0] = v_dual_fmamk_f32 %0:v[2], %0:v[3], 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vop2(aco_opcode::v_fmamk_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1), Operand::zero());
   bld.vop2(aco_opcode::v_sub_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));

   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 3
   //! v1: %0:v[0] = v_dual_fmamk_f32 %0:v[2], %0:v[3], 0 :: v1: %0:v[1] = v_dual_sub_f32 %0:v[3], %0:v[2]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.vop2(aco_opcode::v_subrev_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));
   bld.vop2(aco_opcode::v_fmamk_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1), Operand::zero());

   /* If we have to move the second instruction into OPY instead of OPX, then swapping must still be
    * correct. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 4
   //! v1: %0:v[0] = v_dual_mul_f32 %0:v[3], %0:v[2] :: v1: %0:v[1] = v_dual_lshlrev_b32 %0:v[2], %0:v[3]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.vop2(aco_opcode::v_mul_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));
   bld.vop2(aco_opcode::v_lshlrev_b32, Definition(reg_v1, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));

   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 5
   //! v1: %0:v[0] = v_dual_mul_f32 %0:v[3], %0:v[2] :: v1: %0:v[1] = v_dual_lshlrev_b32 %0:v[2], %0:v[3]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
   bld.vop2(aco_opcode::v_lshlrev_b32, Definition(reg_v1, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));
   bld.vop2(aco_opcode::v_mul_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
            Operand(reg_v3, v1));

   finish_schedule_vopd_test();
END_TEST

BEGIN_TEST(vopd_sched.mov_to_add_bfrev)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx, CHIP_UNKNOWN, "", 32))
         continue;

      PhysReg reg_v0{256};
      PhysReg reg_v1{257};
      PhysReg reg_v2{258};
      PhysReg reg_v3{259};

      //>> p_unit_test 0
      //~gfx11! v1: %0:v[1] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[0] = v_dual_add_nc_u32 0, %0:v[2]
      //~gfx12! v1: %0:v[1] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[0] = v_dual_mov_b32 %0:v[2]
      bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v0, v1), Operand(reg_v2, v1));
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v1, v1), Operand(reg_v2, v1));

      /* We can't turn the v_mov_b32 into a v_add_u32 because then both instructions would be
       * OPY-only.
       */
      bld.reset(program->create_and_insert_block());
      //>> p_unit_test 1
      //~gfx11! v1: %0:v[0] = v_mov_b32 %0:v[2]
      //~gfx11! v1: %0:v[1] = v_lshlrev_b32 %0:v[2], %0:v[3]
      //~gfx12! v1: %0:v[0] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[1] = v_dual_lshlrev_b32 %0:v[2], %0:v[3]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v0, v1), Operand(reg_v2, v1));
      bld.vop2(aco_opcode::v_lshlrev_b32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1));

      bld.reset(program->create_and_insert_block());
      //>> p_unit_test 2
      //~gfx11! v1: %0:v[1] = v_lshlrev_b32 %0:v[2], %0:v[3]
      //~gfx11! v1: %0:v[0] = v_mov_b32 %0:v[2]
      //~gfx12! v1: %0:v[0] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[1] = v_dual_lshlrev_b32 %0:v[2], %0:v[3]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.vop2(aco_opcode::v_lshlrev_b32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1));
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v0, v1), Operand(reg_v2, v1));

      bld.reset(program->create_and_insert_block());
      //>> p_unit_test 3
      //~gfx11! v1: %0:v[0] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[1] = v_dual_and_b32 %0:v[3], %0:v[2]
      //~gfx12! v1: %0:v[0] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[1] = v_dual_and_b32 %0:v[2], %0:v[3]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v0, v1), Operand(reg_v2, v1));
      bld.vop2(aco_opcode::v_and_b32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1));

      bld.reset(program->create_and_insert_block());
      //>> p_unit_test 4
      //~gfx11! v1: %0:v[0] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[1] = v_dual_and_b32 %0:v[3], %0:v[2]
      //~gfx12! v1: %0:v[0] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[1] = v_dual_and_b32 %0:v[2], %0:v[3]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.vop2(aco_opcode::v_and_b32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1));
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v0, v1), Operand(reg_v2, v1));

      /* The v_add_u32 should be OPY, not OPX. */
      bld.reset(program->create_and_insert_block());
      //>> p_unit_test 5
      //~gfx11! v1: %0:v[1] = v_dual_fmamk_f32 %0:v[2], %0:v[3], 0 :: v1: %0:v[0] = v_dual_add_nc_u32 0, %0:v[2]
      //~gfx12! v1: %0:v[0] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[1] = v_dual_fmamk_f32 %0:v[2], %0:v[3], 0
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.vop2(aco_opcode::v_fmamk_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1), Operand::zero());
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v0, v1), Operand(reg_v2, v1));

      bld.reset(program->create_and_insert_block());
      //>> p_unit_test 6
      //~gfx11! v1: %0:v[1] = v_dual_fmamk_f32 %0:v[2], %0:v[3], 0 :: v1: %0:v[0] = v_dual_add_nc_u32 0, %0:v[2]
      //~gfx12! v1: %0:v[1] = v_dual_fmamk_f32 %0:v[2], %0:v[3], 0 :: v1: %0:v[0] = v_dual_mov_b32 %0:v[2]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v0, v1), Operand(reg_v2, v1));
      bld.vop2(aco_opcode::v_fmamk_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1), Operand::zero());

      //>> p_unit_test 7
      //! v1: %0:v[1] = v_dual_mov_b32 %0:v[2] :: v1: %0:v[0] = v_dual_mov_b32 0x3c000000
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      bld.vop1(aco_opcode::v_bfrev_b32, Definition(reg_v0, v1), Operand::c32(60));
      bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v1, v1), Operand(reg_v2, v1));

      finish_schedule_vopd_test();
   }
END_TEST

BEGIN_TEST(vopd_sched.war)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx, CHIP_UNKNOWN, "", 32))
         continue;

      PhysReg reg_v0{256};
      PhysReg reg_v1{257};
      PhysReg reg_v3{259};

      //>> p_unit_test 0
      //~gfx11! v1: %0:v[1] = v_dual_add_f32 %0:v[3], %0:v[1] :: v1: %0:v[0] = v_dual_mul_f32 %0:v[1], %0:v[3]
      //~gfx12! v1: %0:v[0] = v_dual_mul_f32 %0:v[1], %0:v[3] :: v1: %0:v[1] = v_dual_add_f32 %0:v[3], %0:v[1]
      bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
      bld.vop2(aco_opcode::v_mul_f32, Definition(reg_v0, v1), Operand(reg_v1, v1),
               Operand(reg_v3, v1));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v1, v1), Operand(reg_v3, v1),
               Operand(reg_v1, v1));

      /* We can't use OPX for the v_mul_f32 because of the WaR, but we also can't use OPX for the
       * v_add_u32 because that opcode is OPY-only. */
      //>> p_unit_test 1
      //~gfx11! v1: %0:v[1] = v_dual_mul_f32 %0:v[3], %0:v[1] :: v1: %0:v[0] = v_dual_add_nc_u32 %0:v[1], %0:v[3]
      //~gfx12! v1: %0:v[0] = v_add_u32 %0:v[1], %0:v[3]
      //~gfx12! v1: %0:v[1] = v_mul_f32 %0:v[3], %0:v[1]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.vop2(aco_opcode::v_add_u32, Definition(reg_v0, v1), Operand(reg_v1, v1),
               Operand(reg_v3, v1));
      bld.vop2(aco_opcode::v_mul_f32, Definition(reg_v1, v1), Operand(reg_v3, v1),
               Operand(reg_v1, v1));

      finish_schedule_vopd_test();
   }
END_TEST

BEGIN_TEST(vopd_sched.same_vgpr)
   for (amd_gfx_level gfx : {GFX11, GFX12}) {
      if (!setup_cs(NULL, gfx, CHIP_UNKNOWN, "", 32))
         continue;

      PhysReg reg_v0{256};
      PhysReg reg_v1{257};
      PhysReg reg_v2{258};
      PhysReg reg_v3{259};

      //>> p_unit_test 0
      //~gfx11! v1: %0:v[1] = v_dual_add_f32 %0:v[1], %0:v[2] :: v1: %0:v[0] = v_dual_add_f32 %0:v[2], %0:v[0]
      //~gfx12! v1: %0:v[1] = v_dual_add_f32 %0:v[2], %0:v[1] :: v1: %0:v[0] = v_dual_add_f32 %0:v[2], %0:v[0]
      bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
               Operand(reg_v0, v1));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v1, v1));

      //>> p_unit_test 1
      //~gfx11! v1: %0:v[1] = v_dual_add_f32 %0:v[2], %0:v[1] :: v1: %0:v[0] = v_dual_add_f32 %0:v[0], %0:v[2]
      //~gfx12! v1: %0:v[1] = v_dual_add_f32 %0:v[1], %0:v[2] :: v1: %0:v[0] = v_dual_add_f32 %0:v[0], %0:v[2]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v0, v1), Operand(reg_v0, v1),
               Operand(reg_v2, v1));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v1, v1), Operand(reg_v1, v1),
               Operand(reg_v2, v1));

      //>> p_unit_test 2
      //~gfx11! v1: %0:v[1] = v_dual_add_f32 %0:v[3], %0:v[2] :: v1: %0:v[0] = v_dual_add_f32 %0:v[2], %0:v[3]
      //~gfx12! v1: %0:v[1] = v_dual_add_f32 %0:v[2], %0:v[3] :: v1: %0:v[0] = v_dual_add_f32 %0:v[2], %0:v[3]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v3, v1));

      //>> p_unit_test 3
      //~gfx11! v1: %0:v[0] = v_add_f32 %0:v[2], %0:v[2]
      //~gfx11! v1: %0:v[1] = v_add_f32 %0:v[2], %0:v[2]
      //~gfx12! v1: %0:v[1] = v_dual_add_f32 %0:v[2], %0:v[2] :: v1: %0:v[0] = v_dual_add_f32 %0:v[2], %0:v[2]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
               Operand(reg_v2, v1));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v2, v1));

      /* the sources can't be swapped because src0 is an SGPR */
      //>> p_unit_test 4
      //~gfx11! v1: %0:v[1] = v_mul_f32 %0:s[1], %0:v[2]
      //~gfx11! v1: %0:v[0] = v_mul_f32 %0:s[2], %0:v[2]
      //~gfx12! v1: %0:v[0] = v_dual_mul_f32 %0:s[2], %0:v[2] :: v1: %0:v[1] = v_dual_mul_f32 %0:s[1], %0:v[2]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
      bld.vop2(aco_opcode::v_mul_f32, Definition(reg_v1, v1), Operand(PhysReg(1), s1),
               Operand(reg_v2, v1));
      bld.vop2(aco_opcode::v_mul_f32, Definition(reg_v0, v1), Operand(PhysReg(2), s1),
               Operand(reg_v2, v1));

      /* fmamk uses src2 for the second source, which doesn't allow the same VGPR */
      //>> p_unit_test 5
      //! v1: %0:v[0] = v_fmamk_f32 %0:v[0], %0:v[2], 0x80
      //! v1: %0:v[1] = v_fmamk_f32 %0:v[1], %0:v[2], 0x80
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(5));
      bld.vop2(aco_opcode::v_fmamk_f32, Definition(reg_v0, v1), Operand(reg_v0, v1),
               Operand(reg_v2, v1), Operand::literal32(128));
      bld.vop2(aco_opcode::v_fmamk_f32, Definition(reg_v1, v1), Operand(reg_v1, v1),
               Operand(reg_v2, v1), Operand::literal32(128));

      /* the two sources have to be the same size */
      //>> p_unit_test 6
      //! v1: %0:v[0] = v_add_f32 %0:v[2], %0:v[2]
      //! v1: %0:v[1] = v_dot2c_f32_f16 %0:v[2], %0:v[2]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(6));
      bld.vop2(aco_opcode::v_add_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
               Operand(reg_v2, v1));
      bld.vop2(aco_opcode::v_dot2c_f32_f16, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v2, v1));

      //>> p_unit_test 7
      //~gfx11! v1: %0:v[0] = v_dot2c_f32_f16 %0:v[2], %0:v[2], %0:v[0]
      //~gfx11! v1: %0:v[1] = v_dot2c_f32_f16 %0:v[2], %0:v[2], %0:v[1]
      //~gfx12! v1: %0:v[1] = v_dual_dot2acc_f32_f16 %0:v[2], %0:v[2], %0:v[1] :: v1: %0:v[0] = v_dual_dot2acc_f32_f16 %0:v[2], %0:v[2], %0:v[0]
      bld.pseudo(aco_opcode::p_unit_test, Operand::c32(7));
      bld.vop2(aco_opcode::v_dot2c_f32_f16, Definition(reg_v0, v1), Operand(reg_v2, v1),
               Operand(reg_v2, v1), Operand(reg_v0, v1));
      bld.vop2(aco_opcode::v_dot2c_f32_f16, Definition(reg_v1, v1), Operand(reg_v2, v1),
               Operand(reg_v2, v1), Operand(reg_v1, v1));

      finish_schedule_vopd_test();
   }
END_TEST

BEGIN_TEST(vopd_sched.fma_with_constant)
   if (!setup_cs(NULL, GFX11, CHIP_UNKNOWN, "", 32))
      return;

   PhysReg reg_v0{256};
   PhysReg reg_v1{257};
   PhysReg reg_v2{258};
   PhysReg reg_v3{259};
   PhysReg reg_s0{0};

   //>> p_unit_test 0
   //! v1: %0:v[1] = v_dual_fmamk_f32 %0:v[3], %0:v[2], 0x40000000 :: v1: %0:v[0] = v_dual_fmaak_f32 4.0, %0:v[2], 0x40000000
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
   bld.vop3(aco_opcode::v_fma_f32, Definition(reg_v0, v1), Operand(reg_v2, v1),
            Operand::c32(fui(4.0f)), Operand::c32(fui(2.0f)));
   bld.vop3(aco_opcode::v_fma_f32, Definition(reg_v1, v1), Operand(reg_v3, v1),
            Operand::c32(fui(2.0f)), Operand(reg_v2, v1));

   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 1
   //! v1: %0:v[1] = v_dual_fmamk_f32 %0:v[3], %0:v[2], 0x40800000 :: v1: %0:v[0] = v_dual_fmaak_f32 2.0, %0:v[2], 0x40800000
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vop3(aco_opcode::v_fma_f32, Definition(reg_v0, v1), Operand::c32(fui(2.0f)),
            Operand(reg_v2, v1), Operand::c32(fui(4.0f)));
   bld.vop3(aco_opcode::v_fma_f32, Definition(reg_v1, v1), Operand::c32(fui(4.0f)),
            Operand(reg_v3, v1), Operand(reg_v2, v1));

   /* Allow no sgpr operand. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 2
   //! v1: %0:v[1] = v_mov_b32 0
   //! v1: %0:v[0] = v_fma_f32 %0:s[0], %0:v[2], 2.0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vop3(aco_opcode::v_fma_f32, Definition(reg_v0, v1), Operand(reg_s0, s1), Operand(reg_v2, v1),
            Operand::c32(fui(2.0f)));
   bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v1, v1), Operand::c32(0));

   /* Allow no modifiers. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 3
   //! v1: %0:v[1] = v_mov_b32 0
   //! v1: %0:v[0] = v_fma_f32 |%0:v[2]|, %0:v[2], 2.0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.vop3(aco_opcode::v_fma_f32, Definition(reg_v0, v1), Operand(reg_v2, v1), Operand(reg_v2, v1),
            Operand::c32(fui(2.0f)))
      ->valu()
      .abs[0] = true;
   bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v1, v1), Operand::c32(0));

   finish_schedule_vopd_test();
END_TEST

BEGIN_TEST(vopd_sched.dot2acc_from_vop3p)
   if (!setup_cs(NULL, GFX11, CHIP_UNKNOWN, "", 32))
      return;

   PhysReg reg_v0{256};
   PhysReg reg_v1{257};
   PhysReg reg_v2{258};
   PhysReg reg_v3{259};
   PhysReg reg_s0{0};

   //>> p_unit_test 0
   //! v1: %0:v[1] = v_dual_dot2acc_f32_bf16 %0:s[0], %0:v[3], %0:v[1] :: v1: %0:v[0] = v_dual_dot2acc_f32_f16 2.0, %0:v[2], %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
   bld.vop3p(aco_opcode::v_dot2_f32_f16, Definition(reg_v0, v1), Operand(reg_v2, v1),
             Operand::c16(0x4000), Operand(reg_v0, v1), 0, 0b101);
   bld.vop3p(aco_opcode::v_dot2_f32_bf16, Definition(reg_v1, v1), Operand(reg_v3, v1),
             Operand(reg_s0, s1), Operand(reg_v1, v1), 0, 0b111);

   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 1
   //! v1: %0:v[1] = v_dual_dot2acc_f32_bf16 %0:s[0], %0:v[3], %0:v[1] :: v1: %0:v[0] = v_dual_dot2acc_f32_f16 0x70ad70ad, %0:v[2], %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(1));
   bld.vop3p(aco_opcode::v_dot2_f32_f16, Definition(reg_v0, v1), Operand(reg_v2, v1),
             Operand::literal32(0x70AD), Operand(reg_v0, v1), 0, 0b101);
   bld.vop3p(aco_opcode::v_dot2_f32_bf16, Definition(reg_v1, v1), Operand(reg_s0, s1),
             Operand(reg_v3, v1), Operand(reg_v1, v1), 0, 0b111);

   /* Needs at least one vgpr factor. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 2
   //! v1: %0:v[1] = v_mov_b32 0
   //! v1: %0:v[0] = v_dot2_f32_f16 %0:s[0], 2.0.xx, %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(2));
   bld.vop3p(aco_opcode::v_dot2_f32_f16, Definition(reg_v0, v1), Operand(reg_s0, s1),
             Operand::c16(0x4000), Operand(reg_v0, v1), 0, 0b101);
   bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v1, v1), Operand::c32(0));

   /* Allow no modifiers. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 3
   //! v1: %0:v[0] = v_dot2_f32_f16 %0:v[1]*[-1,1], %0:v[2], %0:v[0]
   //! v1: %0:v[1] = v_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(3));
   bld.vop3p(aco_opcode::v_dot2_f32_f16, Definition(reg_v0, v1), Operand(reg_v1, v1),
             Operand(reg_v2, v1), Operand(reg_v0, v1), 0, 0b111)
      ->valu()
      .neg_lo[0] = true;
   bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v1, v1), Operand::c32(0));

   /* Definition must be the same as the last operand. */
   bld.reset(program->create_and_insert_block());
   //>> p_unit_test 4
   //! v1: %0:v[0] = v_dot2_f32_f16 %0:v[1], %0:v[2], %0:v[3]
   //! v1: %0:v[1] = v_mov_b32 0
   bld.pseudo(aco_opcode::p_unit_test, Operand::c32(4));
   bld.vop3p(aco_opcode::v_dot2_f32_f16, Definition(reg_v0, v1), Operand(reg_v1, v1),
             Operand(reg_v2, v1), Operand(reg_v3, v1), 0, 0b111);
   bld.vop1(aco_opcode::v_mov_b32, Definition(reg_v1, v1), Operand::c32(0));

   finish_schedule_vopd_test();
END_TEST

BEGIN_TEST(vopd_sched.dot2acc_from_vop2_inline)
   if (!setup_cs(NULL, GFX11, CHIP_UNKNOWN, "", 32))
      return;

   PhysReg reg_v0{256};
   PhysReg reg_v1{257};
   PhysReg reg_v2{258};
   PhysReg reg_v3{259};
   PhysReg reg_s0{0};

   //>> p_unit_test 0
   //! v1: %0:v[1] = v_dual_dot2acc_f32_f16 %0:s[0], %0:v[3], %0:v[1] :: v1: %0:v[0] = v_dual_dot2acc_f32_f16 0x4000, %0:v[2], %0:v[0]
   bld.pseudo(aco_opcode::p_unit_test, Operand::zero());
   bld.vop2(aco_opcode::v_dot2c_f32_f16, Definition(reg_v0, v1), Operand::c16(0x4000),
            Operand(reg_v2, v1), Operand(reg_v0, v1));
   bld.vop2(aco_opcode::v_dot2c_f32_f16, Definition(reg_v1, v1), Operand(reg_s0, s1),
            Operand(reg_v3, v1), Operand(reg_v1, v1));

   finish_schedule_vopd_test();
END_TEST
