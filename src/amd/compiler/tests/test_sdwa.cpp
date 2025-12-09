/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "helpers.h"
#include <stdarg.h>

using namespace aco;

BEGIN_TEST(validate.sdwa.allow)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %a:v[0], v1: %b:v[1], s1: %c:s[0], s1: %d:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;
      //>> Validation results:
      //! Validation passed

      SDWA_instruction* sdwa =
         &bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1])->sdwa();
      sdwa->neg[0] = sdwa->neg[1] = sdwa->abs[0] = sdwa->abs[1] = true;

      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1b), inputs[0], inputs[1]);

      sdwa = &bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1])->sdwa();
      sdwa->sel[0] = SubdwordSel::sbyte2;
      sdwa->sel[1] = SubdwordSel::uword1;

      finish_validator_test();
   }
END_TEST

BEGIN_TEST(validate.sdwa.support)
   for (unsigned i = GFX7; i <= GFX11; i++) {
      //>> v1: %a:v[0], v1: %b:v[1], s1: %c:s[0], s1: %d:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;
      //>> Validation results:

      //~gfx(7|11)! SDWA is GFX8 to GFX10.3 only: v1: %t0 = v_mul_f32 %a, %b dst_sel:dword src0_sel:dword src1_sel:dword
      //~gfx(7|11)! Validation failed
      //~gfx([89]|10)! Validation passed
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);

      finish_validator_test();
   }
END_TEST

BEGIN_TEST(validate.sdwa.operands)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %vgpr0:v[0], v1: %vgpr1:v[1], s1: %sgpr0:s[0], s1: %sgpr1:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;
      //>> Validation results:

      //~gfx8! Wrong source position for SGPR argument: v1: %_ = v_mul_f32 %sgpr0, %vgpr1 dst_sel:dword src0_sel:dword src1_sel:dword
      //~gfx8! Wrong source position for SGPR argument: v1: %_ = v_mul_f32 %vgpr0, %sgpr1 dst_sel:dword src0_sel:dword src1_sel:dword
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[2], inputs[1]);
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[3]);

      //~gfx8! Wrong source position for constant argument: v1: %_ = v_mul_f32 4, %vgpr1 dst_sel:dword src0_sel:dword src1_sel:dword
      //~gfx8! Wrong source position for constant argument: v1: %_ = v_mul_f32 %vgpr0, 4 dst_sel:dword src0_sel:dword src1_sel:dword
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), Operand::c32(4u), inputs[1]);
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], Operand::c32(4u));

      //! Literal applied on wrong instruction format: v1: %_ = v_mul_f32 0x1234, %vgpr1 dst_sel:dword src0_sel:dword src1_sel:dword
      //! Literal applied on wrong instruction format: v1: %_ = v_mul_f32 %vgpr0, 0x1234 dst_sel:dword src0_sel:dword src1_sel:dword
      //! Wrong source position for Literal argument: v1: %_ = v_mul_f32 %vgpr0, 0x1234 dst_sel:dword src0_sel:dword src1_sel:dword
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), Operand::c32(0x1234u), inputs[1]);
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], Operand::c32(0x1234u));

      //! Validation failed

      finish_validator_test();
   }
END_TEST

BEGIN_TEST(validate.sdwa.vopc)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %vgpr0:v[0], v1: %vgpr1:v[1], s1: %sgpr0:s[0], s1: %sgpr1:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;
      //>> Validation results:

      bld.vopc_sdwa(aco_opcode::v_cmp_gt_f32, bld.def(bld.lm, vcc), inputs[0], inputs[1]);

      //~gfx8! SDWA+VOPC definition must be fixed to vcc on GFX8: s2: %_ = v_cmp_lt_f32 %vgpr0, %vgpr1 src0_sel:dword src1_sel:dword
      bld.vopc_sdwa(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), inputs[0], inputs[1]);

      //~gfx(9|10)! SDWA VOPC clamp only supported on GFX8: s2: %_:vcc = v_cmp_eq_f32 %vgpr0, %vgpr1 clamp src0_sel:dword src1_sel:dword
      bld.vopc_sdwa(aco_opcode::v_cmp_eq_f32, bld.def(bld.lm, vcc), inputs[0], inputs[1])
         ->sdwa()
         .clamp = true;

      //! Validation failed

      finish_validator_test();
   }
END_TEST

BEGIN_TEST(validate.sdwa.omod)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %vgpr0:v[0], v1: %vgpr1:v[1], s1: %sgpr0:s[0], s1: %sgpr1:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;
      //>> Validation results:

      //~gfx8! SDWA omod only supported on GFX9+: v1: %_ = v_mul_f32 %vgpr0, %vgpr1 *2 dst_sel:dword src0_sel:dword src1_sel:dword
      //~gfx8! Validation failed
      //~gfx(9|10)! Validation passed
      bld.vop2_sdwa(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1])->sdwa().omod = 1;

      finish_validator_test();
   }
END_TEST

BEGIN_TEST(validate.sdwa.vcc)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %vgpr0:v[0], v1: %vgpr1:v[1], s2: %sgpr0:s[0-1] = p_startpgm
      if (!setup_cs("v1 v1 s2", (amd_gfx_level)i))
         continue;
      //>> Validation results:

      //! 3rd operand must be fixed to vcc with SDWA: v1: %_ = v_cndmask_b32 %vgpr0, %vgpr1, %_ dst_sel:dword src0_sel:dword src1_sel:dword
      bld.vop2_sdwa(aco_opcode::v_cndmask_b32, bld.def(v1), inputs[0], inputs[1], inputs[2]);
      bld.vop2_sdwa(aco_opcode::v_cndmask_b32, bld.def(v1), inputs[0], inputs[1],
                    bld.vcc(inputs[2]));

      //! 2nd definition must be fixed to vcc with SDWA: v1: %_, s2: %_ = v_add_co_u32 %vgpr0, %vgpr1 dst_sel:dword src0_sel:dword src1_sel:dword
      bld.vop2_sdwa(aco_opcode::v_add_co_u32, bld.def(v1), bld.def(bld.lm), inputs[0], inputs[1]);
      bld.vop2_sdwa(aco_opcode::v_add_co_u32, bld.def(v1), bld.def(bld.lm, vcc), inputs[0],
                    inputs[1]);

      //! Validation failed

      finish_validator_test();
   }
END_TEST

BEGIN_TEST(optimize.sdwa.extract)
   for (unsigned i = GFX7; i <= GFX10; i++) {
      for (unsigned is_signed = 0; is_signed <= 1; is_signed++) {
         //>> v1: %a:v[0], v1: %b:v[1], s1: %c:s[0], s1: %d:s[1] = p_startpgm
         if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i, CHIP_UNKNOWN,
                       is_signed ? "_signed" : "_unsigned"))
            continue;

         //; def standard_test(index, sel):
         //;    res = 'v1: %%res%s = v_mul_f32 %%a, %%b dst_sel:dword src0_sel:dword src1_sel:%c%s\n' % (index, 's' if variant.endswith('_signed') else 'u', sel)
         //;    res += 'p_unit_test %s, %%res%s' % (index, index)
         //;    return res
         //; funcs['standard_test'] = lambda a: standard_test(*(v for v in a.split(',')))

         aco_opcode ext = aco_opcode::p_extract;
         aco_opcode ins = aco_opcode::p_insert;

         {
            //~gfx[^7].*! @standard_test(0,byte0)
            Temp bfe_byte0_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::zero(),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(0, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfe_byte0_b));

            //~gfx[^7].*! @standard_test(1,byte1)
            Temp bfe_byte1_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(1u),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(1, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfe_byte1_b));

            //~gfx[^7].*! @standard_test(2,byte2)
            Temp bfe_byte2_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(2u),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(2, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfe_byte2_b));

            //~gfx[^7].*! @standard_test(3,byte3)
            Temp bfe_byte3_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(3u),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(3, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfe_byte3_b));

            //~gfx[^7].*! @standard_test(4,word0)
            Temp bfe_word0_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::zero(),
                                          Operand::c32(16u), Operand::c32(is_signed));
            writeout(4, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfe_word0_b));

            //~gfx[^7].*! @standard_test(5,word1)
            Temp bfe_word1_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(1u),
                                          Operand::c32(16u), Operand::c32(is_signed));
            writeout(5, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfe_word1_b));

            //~gfx[^7]_unsigned! @standard_test(6,byte0)
            Temp bfi_byte0_b =
               bld.pseudo(ins, bld.def(v1), inputs[1], Operand::zero(), Operand::c32(8u));
            writeout(6, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfi_byte0_b));

            //~gfx[^7]_unsigned! @standard_test(7,word0)
            Temp bfi_word0_b =
               bld.pseudo(ins, bld.def(v1), inputs[1], Operand::zero(), Operand::c32(16u));
            writeout(7, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfi_word0_b));
         }

         //>> p_unit_test 63
         writeout(63);

         {
            //! v1: %tmp8 = p_insert %b, 1, 8
            //! v1: %res8 = v_mul_f32 %a, %tmp8
            //! p_unit_test 8, %res8
            Temp bfi_byte1_b =
               bld.pseudo(ins, bld.def(v1), inputs[1], Operand::c32(1u), Operand::c32(8u));
            writeout(8, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], bfi_byte1_b));

            /* v_cvt_f32_ubyte[0-3] can be used instead of v_cvt_f32_u32+sdwa */
            //~gfx7_signed! v1: %bfe_byte0_b = p_extract %b, 0, 8, 1
            //~gfx7_signed! v1: %res9 = v_cvt_f32_u32 %bfe_byte0_b
            //~gfx[^7]+_signed! v1: %res9 = v_cvt_f32_u32 %b dst_sel:dword src0_sel:sbyte0
            //~gfx\d+_unsigned! v1: %res9 = v_cvt_f32_ubyte0 %b
            //! p_unit_test 9, %res9
            Temp bfe_byte0_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::zero(),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(9, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), bfe_byte0_b));

            //~gfx7_signed! v1: %bfe_byte1_b = p_extract %b, 1, 8, 1
            //~gfx7_signed! v1: %res10 = v_cvt_f32_u32 %bfe_byte1_b
            //~gfx[^7]+_signed! v1: %res10 = v_cvt_f32_u32 %b dst_sel:dword src0_sel:sbyte1
            //~gfx\d+_unsigned! v1: %res10 = v_cvt_f32_ubyte1 %b
            //! p_unit_test 10, %res10
            Temp bfe_byte1_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(1u),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(10, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), bfe_byte1_b));

            //~gfx7_signed! v1: %bfe_byte2_b = p_extract %b, 2, 8, 1
            //~gfx7_signed! v1: %res11 = v_cvt_f32_u32 %bfe_byte2_b
            //~gfx[^7]+_signed! v1: %res11 = v_cvt_f32_u32 %b dst_sel:dword src0_sel:sbyte2
            //~gfx\d+_unsigned! v1: %res11 = v_cvt_f32_ubyte2 %b
            //! p_unit_test 11, %res11
            Temp bfe_byte2_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(2u),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(11, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), bfe_byte2_b));

            //~gfx7_signed! v1: %bfe_byte3_b = p_extract %b, 3, 8, 1
            //~gfx7_signed! v1: %res12 = v_cvt_f32_u32 %bfe_byte3_b
            //~gfx[^7]+_signed! v1: %res12 = v_cvt_f32_u32 %b dst_sel:dword src0_sel:sbyte3
            //~gfx\d+_unsigned! v1: %res12 = v_cvt_f32_ubyte3 %b
            //! p_unit_test 12, %res12
            Temp bfe_byte3_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(3u),
                                          Operand::c32(8u), Operand::c32(is_signed));
            writeout(12, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), bfe_byte3_b));

            /* VOP3-only instructions can't use SDWA but they can use opsel on GFX9+ instead */
            //~gfx(9|10).*! v1: %res13 = v_add_i16 %a, %b
            //~gfx(9|10).*! p_unit_test 13, %res13
            Temp bfe_word0_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::zero(),
                                          Operand::c32(16u), Operand::c32(is_signed));
            writeout(13, bld.vop3(aco_opcode::v_add_i16, bld.def(v1), inputs[0], bfe_word0_b));

            //~gfx(9|10).*! v1: %res14 = v_add_i16 %a, hi(%b)
            //~gfx(9|10).*! p_unit_test 14, %res14
            Temp bfe_word1_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::c32(1u),
                                          Operand::c32(16u), Operand::c32(is_signed));
            writeout(14, bld.vop3(aco_opcode::v_add_i16, bld.def(v1), inputs[0], bfe_word1_b));
         }

         finish_opt_test();
      }
   }
END_TEST

BEGIN_TEST(optimize.sdwa.extract_modifiers)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %a:v[0], v1: %b:v[1], s1: %c:s[0], s1: %d:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;

      aco_opcode ext = aco_opcode::p_extract;

      //! v1: %res0 = v_mul_f32 %a, -%b dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 0, %res0
      Temp byte0 = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::zero(), Operand::c32(8u),
                              Operand::zero());
      Temp neg_byte0 = fneg(byte0);
      writeout(0, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], neg_byte0));

      //! v1: %neg = v_mul_f32 -1.0, %b
      //! v1: %res1 = v_mul_f32 %a, %neg dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 1, %res1
      Temp neg = fneg(inputs[1]);
      Temp byte0_neg =
         bld.pseudo(ext, bld.def(v1), neg, Operand::zero(), Operand::c32(8u), Operand::zero());
      writeout(1, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], byte0_neg));

      //! v1: %res2 = v_mul_f32 %a, |%b| dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 2, %res2
      Temp abs_byte0 = fabs(byte0);
      writeout(2, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], abs_byte0));

      //! v1: %abs = v_mul_f32 1.0, |%b|
      //! v1: %res3 = v_mul_f32 %a, %abs dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 3, %res3
      Temp abs = fabs(inputs[1]);
      Temp byte0_abs =
         bld.pseudo(ext, bld.def(v1), abs, Operand::zero(), Operand::c32(8u), Operand::zero());
      writeout(3, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], byte0_abs));

      //! v1: %res4 = v_mul_f32 %1, -|%2| dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 4, %res4
      Temp neg_abs_byte0 = fneg(abs_byte0);
      writeout(4, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], neg_abs_byte0));

      //! v1: %neg_abs = v_mul_f32 -1.0, |%b|
      //! v1: %res5 = v_mul_f32 %a, %neg_abs dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 5, %res5
      Temp neg_abs = fneg(abs);
      Temp byte0_neg_abs =
         bld.pseudo(ext, bld.def(v1), neg_abs, Operand::zero(), Operand::c32(8u), Operand::zero());
      writeout(5, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], byte0_neg_abs));

      finish_opt_test();
   }
END_TEST

BEGIN_TEST(optimize.sdwa.extract_modifiers_fp16)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v2b: %a:v[0][0:16], v1: %b:v[1] = p_startpgm
      if (!setup_cs("v2b v1", (amd_gfx_level)i))
         continue;

      Temp hi = bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), inputs[1], Operand::c32(1));

      //! v2b: %res0 = v_mul_f16 -%b, %a dst_sel:uword0 dst_preserve src0_sel:uword1 src1_sel:uword0
      //! p_unit_test 0, %res0
      Temp fneg_hi = fneg(hi);
      writeout(0, fmul(fneg_hi, inputs[0]));

      //! v2b: %res1 = v_mul_f16 |%b|, %a dst_sel:uword0 dst_preserve src0_sel:uword1 src1_sel:uword0
      //! p_unit_test 1, %res1
      Temp fabs_hi = fabs(hi);
      writeout(1, fmul(fabs_hi, inputs[0]));

      finish_opt_test();
   }
END_TEST

BEGIN_TEST(optimize.sdwa.extract.sgpr)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %a:v[0], v1: %b:v[1], s1: %c:s[0], s1: %d:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;

      aco_opcode ext = aco_opcode::p_extract;

      //~gfx8! v1: %byte0_b = p_extract %b, 0, 8, 0
      //~gfx8! v1: %res1 = v_mul_f32 %c, %byte0_b
      //~gfx(9|10)! v1: %res1 = v_mul_f32 %c, %b dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 1, %res1
      Temp byte0_b = bld.pseudo(ext, bld.def(v1), inputs[1], Operand::zero(), Operand::c32(8u),
                                Operand::zero());
      writeout(1, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[2], byte0_b));

      //~gfx8! v1: %byte0_c = p_extract %c, 0, 8, 0
      //~gfx8! v1: %res2 = v_mul_f32 %a, %byte0_c
      //~gfx(9|10)! v1: %res2 = v_mul_f32 %a, %c dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 2, %res2
      Temp byte0_c = bld.pseudo(ext, bld.def(v1), inputs[2], Operand::zero(), Operand::c32(8u),
                                Operand::zero());
      writeout(2, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], byte0_c));

      //~gfx8! v1: %byte0_c_2 = p_extract %c, 0, 8, 0
      //~gfx8! v1: %res3 = v_mul_f32 %c, %byte0_c_2
      //~gfx(9|10)! v1: %res3 = v_mul_f32 %c, %c dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 3, %res3
      byte0_c = bld.pseudo(ext, bld.def(v1), inputs[2], Operand::zero(), Operand::c32(8u),
                           Operand::zero());
      writeout(3, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[2], byte0_c));

      //~gfx(8|9)! v1: %byte0_c_3 = p_extract %c, 0, 8, 0
      //~gfx(8|9)! v1: %res4 = v_mul_f32 %d, %byte0_c_3
      //~gfx10! v1: %res4 = v_mul_f32 %d, %c dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 4, %res4
      byte0_c = bld.pseudo(ext, bld.def(v1), inputs[2], Operand::zero(), Operand::c32(8u),
                           Operand::zero());
      writeout(4, bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[3], byte0_c));

      finish_opt_test();
   }
END_TEST

BEGIN_TEST(optimize.sdwa.from_vop3)
   for (unsigned i = GFX8; i <= GFX10; i++) {
      //>> v1: %a:v[0], v1: %b:v[1], s1: %c:s[0], s1: %d:s[1] = p_startpgm
      if (!setup_cs("v1 v1 s1 s1", (amd_gfx_level)i))
         continue;

      //! v1: %res0 = v_mul_f32 -|%a|, %b dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 0, %res0
      Temp byte0_b = bld.pseudo(aco_opcode::p_extract, bld.def(v1), inputs[1], Operand::zero(),
                                Operand::c32(8u), Operand::zero());
      VALU_instruction* mul =
         &bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], byte0_b)->valu();
      mul->neg[0] = true;
      mul->abs[0] = true;
      writeout(0, mul->definitions[0].getTemp());

      //~gfx8! v1: %byte0_b_0 = p_extract %b, 0, 8, 0
      //~gfx8! v1: %res1 = v_mul_f32 %a, %byte0_b_0 *4
      //~gfx(9|10)! v1: %res1 = v_mul_f32 %a, %b *4 dst_sel:dword src0_sel:dword src1_sel:ubyte0
      //! p_unit_test 1, %res1
      byte0_b = bld.pseudo(aco_opcode::p_extract, bld.def(v1), inputs[1], Operand::zero(),
                           Operand::c32(8u), Operand::zero());
      mul = &bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], byte0_b)->valu();
      mul->omod = 2;
      writeout(1, mul->definitions[0].getTemp());

      //~gfx8! v1: %byte0_b_1 = p_extract %b, 0, 8, 0
      //~gfx8! v1: %res2 = v_mul_f32 %byte0_b_1, %c
      //~gfx(9|10)! v1: %res2 = v_mul_f32 %b, %c dst_sel:dword src0_sel:ubyte0 src1_sel:dword
      //! p_unit_test 2, %res2
      byte0_b = bld.pseudo(aco_opcode::p_extract, bld.def(v1), inputs[1], Operand::zero(),
                           Operand::c32(8u), Operand::zero());
      writeout(2, bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), byte0_b, inputs[2]));

      if (i >= GFX10) {
         //~gfx10! v1: %byte0_b_2 = p_extract %b, 0, 8, 0
         //~gfx10! v1: %res3 = v_mul_f32 %byte0_b_2, 0x1234
         //~gfx10! p_unit_test 3, %res3
         byte0_b = bld.pseudo(aco_opcode::p_extract, bld.def(v1), inputs[1], Operand::zero(),
                              Operand::c32(8u), Operand::zero());
         writeout(3,
                  bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), byte0_b, Operand::c32(0x1234u)));
      }

      finish_opt_test();
   }
END_TEST

BEGIN_TEST(optimize.sdwa.insert)
   for (unsigned i = GFX7; i <= GFX10; i++) {
      //>> v1: %a:v[0], v1: %b:v[1] = p_startpgm
      if (!setup_cs("v1 v1", (amd_gfx_level)i))
         continue;

      aco_opcode ext = aco_opcode::p_extract;
      aco_opcode ins = aco_opcode::p_insert;

      //~gfx[^7]! v1: %res0 = v_mul_f32 %a, %b dst_sel:ubyte0 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 0, %res0
      Temp val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(0, bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(8u)));

      //~gfx[^7]! v1: %res1 = v_mul_f32 %a, %b dst_sel:ubyte1 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 1, %res1
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(1, bld.pseudo(ins, bld.def(v1), val, Operand::c32(1u), Operand::c32(8u)));

      //~gfx[^7]! v1: %res2 = v_mul_f32 %a, %b dst_sel:ubyte2 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 2, %res2
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(2, bld.pseudo(ins, bld.def(v1), val, Operand::c32(2u), Operand::c32(8u)));

      //~gfx[^7]! v1: %res3 = v_mul_f32 %a, %b dst_sel:ubyte3 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 3, %res3
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(3, bld.pseudo(ins, bld.def(v1), val, Operand::c32(3u), Operand::c32(8u)));

      //~gfx[^7]! v1: %res4 = v_mul_f32 %a, %b dst_sel:uword0 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 4, %res4
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(4, bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(16u)));

      //~gfx[^7]! v1: %res5 = v_mul_f32 %a, %b dst_sel:uword1 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 5, %res5
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(5, bld.pseudo(ins, bld.def(v1), val, Operand::c32(1u), Operand::c32(16u)));

      //~gfx[^7]! v1: %res6 = v_mul_f32 %a, %b dst_sel:ubyte0 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 6, %res6
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(
         6, bld.pseudo(ext, bld.def(v1), val, Operand::zero(), Operand::c32(8u), Operand::zero()));

      //~gfx[^7]! v1: %res7 = v_mul_f32 %a, %b dst_sel:uword0 src0_sel:dword src1_sel:dword
      //~gfx[^7]! p_unit_test 7, %res7
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(
         7, bld.pseudo(ext, bld.def(v1), val, Operand::zero(), Operand::c32(16u), Operand::zero()));

      //~gfx[^7]! v1: %tmp8 = v_mul_f32 %a, %b
      //~gfx[^7]! v1: %res8 = p_extract %tmp8, 2, 8, 0
      //~gfx[^7]! p_unit_test 8, %res8
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(
         8, bld.pseudo(ext, bld.def(v1), val, Operand::c32(2u), Operand::c32(8u), Operand::zero()));

      //~gfx[^7]! v1: %tmp9 = v_mul_f32 %a, %b
      //~gfx[^7]! v1: %res9 = p_extract %tmp9, 0, 8, 1
      //~gfx[^7]! p_unit_test 9, %res9
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      writeout(
         9, bld.pseudo(ext, bld.def(v1), val, Operand::zero(), Operand::c32(8u), Operand::c32(1u)));

      //>> p_unit_test 63
      writeout(63);

      //! v1: %res10 = v_mul_f32 %a, %b
      //! p_unit_test 10, %res10
      val = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), inputs[0], inputs[1]);
      bld.pseudo(ins, bld.def(v1), val, Operand::c32(1u), Operand::c32(16u));
      writeout(10, val);

      //~gfx[^7]! v1: %tmp11 = v_sub_i16 %a, %b
      //~gfx[^7]! v1: %res11 = p_insert %tmp11, 0, 16
      //~gfx[^7]! p_unit_test 11, %res11
      val = bld.vop3(aco_opcode::v_sub_i16, bld.def(v1), inputs[0], inputs[1]);
      writeout(11, bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(16u)));

      //~gfx[^7]! v1: %tmp12 = v_sub_i16 %a, %b
      //~gfx[^7]! v1: %res12 = p_insert %tmp12, 1, 16
      //~gfx[^7]! p_unit_test 12, %res12
      val = bld.vop3(aco_opcode::v_sub_i16, bld.def(v1), inputs[0], inputs[1]);
      writeout(12, bld.pseudo(ins, bld.def(v1), val, Operand::c32(1u), Operand::c32(16u)));

      //~gfx[^7]! v1: %tmp13 = v_sub_i16 %a, %b
      //~gfx[^7]! v1: %res13 = p_insert %tmp13, 0, 8
      //~gfx[^7]! p_unit_test 13, %res13
      val = bld.vop3(aco_opcode::v_sub_i16, bld.def(v1), inputs[0], inputs[1]);
      writeout(13, bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(8u)));

      finish_opt_test();
   }
END_TEST

BEGIN_TEST(optimize.sdwa.insert_modifiers)
   for (unsigned i = GFX8; i <= GFX9; i++) {
      //>> v1: %a:v[0] = p_startpgm
      if (!setup_cs("v1", (amd_gfx_level)i))
         continue;

      aco_opcode ins = aco_opcode::p_insert;

      //~gfx8! v1: %tmp0 = v_rcp_f32 %a *2
      //~gfx8! v1: %res0 = p_insert %tmp0, 0, 8
      //~gfx9! v1: %res0 = v_rcp_f32 %a *2 dst_sel:ubyte0 src0_sel:dword
      //! p_unit_test 0, %res0
      Temp val = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), inputs[0]);
      val = bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), val, Operand::c32(0x40000000u));
      writeout(0, bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(8u)));

      //! v1: %res1 = v_rcp_f32 %a clamp dst_sel:ubyte0 src0_sel:dword
      //! p_unit_test 1, %res1
      val = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), inputs[0]);
      val = bld.vop3(aco_opcode::v_med3_f32, bld.def(v1), val, Operand::zero(),
                     Operand::c32(0x3f800000u));
      writeout(1, bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(8u)));

      //! v1: %tmp2 = v_rcp_f32 %a dst_sel:ubyte0 src0_sel:dword
      //! v1: %res2 = v_mul_f32 %tmp2, 2.0
      //! p_unit_test 2, %res2
      val = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), inputs[0]);
      val = bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(8u));
      val = bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), val, Operand::c32(0x40000000u));
      writeout(2, val);

      //! v1: %tmp3 = v_rcp_f32 %a dst_sel:ubyte0 src0_sel:dword
      //! v1: %res3 = v_add_f32 %tmp3, 0 clamp
      //! p_unit_test 3, %res3
      val = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), inputs[0]);
      val = bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(8u));
      val = bld.vop3(aco_opcode::v_med3_f32, bld.def(v1), val, Operand::zero(),
                     Operand::c32(0x3f800000u));
      writeout(3, val);

      //~gfx8! v1: %tmp4 = v_rcp_f32 %a *2 clamp
      //~gfx8! v1: %res4 = p_insert %tmp4, 0, 8
      //~gfx9! v1: %res4 = v_rcp_f32 %a *2 clamp dst_sel:ubyte0 src0_sel:dword
      //! p_unit_test 4, %res4
      val = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), inputs[0]);
      val = bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), val, Operand::c32(0x40000000u));
      val = bld.vop3(aco_opcode::v_med3_f32, bld.def(v1), val, Operand::zero(),
                     Operand::c32(0x3f800000u));
      writeout(4, bld.pseudo(ins, bld.def(v1), val, Operand::zero(), Operand::c32(8u)));

      finish_opt_test();
   }
END_TEST

BEGIN_TEST(optimize.sdwa.special_case_valu)
   //>> v1: %a:v[0], s1: %b:s[0] = p_startpgm
   if (!setup_cs("v1 s1", GFX10_3))
      return;

   Temp a = inputs[0];
   Temp b = inputs[1];
   Temp b_vgpr = bld.copy(bld.def(v1), b);

   //! v1: %res0 = v_cvt_f32_ubyte0 %a
   //! p_unit_test 0, %res0
   writeout(0, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), ext_ubyte(a, 0)));

   //! v1: %res1 = v_cvt_f32_ubyte1 %a
   //! p_unit_test 1, %res1
   writeout(1, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), ext_ubyte(a, 1)));

   //! v1: %res2 = v_cvt_f32_ubyte2 %a
   //! p_unit_test 2, %res2
   writeout(2, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), ext_ubyte(a, 2)));

   //! v1: %res3 = v_cvt_f32_ubyte3 %a
   //! p_unit_test 3, %res3
   writeout(3, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), ext_ubyte(a, 3)));

   //! v1: %res4 = v_cvt_f32_u32 %a dst_sel:dword src0_sel:sbyte3
   //! p_unit_test 4, %res4
   writeout(4, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), ext_sbyte(a, 3)));

   //! v1: %res5 = v_cvt_f32_u32 %a dst_sel:dword src0_sel:uword1
   //! p_unit_test 5, %res5
   writeout(5, bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), ext_ushort(a, 1)));

   //! v1: %res6_tmp = p_extract %b, 2, 8, 0
   //! v1: %res6 = v_cvt_f32_u32 %res6_tmp dst_sel:dword src0_sel:sword1
   //! p_unit_test 6, %res6
   writeout(6,
            bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), ext_sshort(ext_ubyte(b_vgpr, 2), 1)));

   //! v1: %res7 = v_lshlrev_b32 16, %a
   //! p_unit_test 7, %res7
   writeout(7,
            bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16), ext_ushort(a, 0)));

   //! v1: %res8 = v_lshlrev_b32 24, %a
   //! p_unit_test 8, %res8
   writeout(8, bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(24), ext_ubyte(a, 0)));

   //! v1: %res9 = v_lshlrev_b32 16, %a
   //! p_unit_test 9, %res9
   writeout(9,
            bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16), ext_sshort(a, 0)));

   //! v1: %res10 = v_lshlrev_b32 24, %a
   //! p_unit_test 10, %res10
   writeout(10,
            bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(24), ext_sbyte(a, 0)));

   //! v1: %res11 = v_lshlrev_b32 16, %a dst_sel:dword src0_sel:dword src1_sel:uword1
   //! p_unit_test 11, %res11
   writeout(11,
            bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16), ext_ushort(a, 1)));

   //! v1: %res12 = v_lshlrev_b32 24, %a dst_sel:dword src0_sel:dword src1_sel:ubyte1
   //! p_unit_test 12, %res12
   writeout(12,
            bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(24), ext_ubyte(a, 1)));

   //! v1: %res13_tmp = p_extract %b, 0, 16, 1
   //! v1: %res13 = v_lshlrev_b32 16, %res13_tmp dst_sel:dword src0_sel:dword src1_sel:ubyte2
   //! p_unit_test 13, %res13
   writeout(13, bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16),
                         ext_ubyte(ext_sshort(b_vgpr, 0), 2)));

   finish_opt_test();
END_TEST

BEGIN_TEST(optimize.sdwa.extract_sgpr_limits)
   //>> s1: %a:s[0] = p_startpgm
   if (!setup_cs("s1", GFX8))
      return;

   Temp a = inputs[0];
   Temp a_vgpr = bld.copy(bld.def(v1), a);

   /* The optimizer should make this VOP3 */
   //! v1: %res0 = v_lshlrev_b32 16, %a
   //! p_unit_test 0, %res0
   writeout(
      0, bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16), ext_ushort(a_vgpr, 0)));

   /* Unsupported on GFX8 */
   //! v1: %res1_tmp = p_extract %a, 0, 16, 0
   //! v1b: %res1 = p_extract %res1_tmp, 0, 8, 0
   //! p_unit_test 1, %res1
   writeout(1, bld.pseudo(aco_opcode::p_extract, bld.def(v1b), ext_ushort(a_vgpr, 0),
                          Operand::c32(0), Operand::c32(8), Operand::c32(false)));

   finish_opt_test();
END_TEST

BEGIN_TEST(optimize.sdwa.subdword_extract)
   //>> v1: %a:v[0], v1: %b:v[1], s2: %c:s[0-1] = p_startpgm
   if (!setup_cs("v1 v1 s2", GFX10_3))
      return;

   Temp a = inputs[0];
   Temp b = inputs[1];

   //! v2b: %res0 = v_lshlrev_b16_e64 4, hi(%a)
   //! p_unit_test 0, %res0
   writeout(0, bld.vop3(aco_opcode::v_lshlrev_b16_e64, bld.def(v2b), Operand::c32(4),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v2b), a, Operand::c32(1),
                                   Operand::c32(16), Operand::c32(false))));

   //! v2b: %res1 = v_add_f16 %a, %b dst_sel:uword0 dst_preserve src0_sel:uword1 src1_sel:uword1
   //! p_unit_test 1, %res1
   writeout(1,
            bld.vop2(aco_opcode::v_add_f16, bld.def(v2b),
                     bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), a, Operand::c32(1)),
                     bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), b, Operand::c32(1))));

   //! v2b: %res2 = v_cndmask_b32 %a, %b, %c:vcc dst_sel:uword0 dst_preserve src0_sel:ubyte0 src1_sel:ubyte1
   //! p_unit_test 2, %res2
   writeout(2, bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v2b),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v2b), a, Operand::c32(0),
                                   Operand::c32(8), Operand::c32(0)),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v2b), b, Operand::c32(1),
                                   Operand::c32(8), Operand::c32(0)),
                        inputs[2]));

   //! v1b: %res3 = v_or_b32 %a, %b dst_sel:ubyte0 dst_preserve src0_sel:uword0 src1_sel:uword1
   //! p_unit_test 3, %res3
   writeout(3, bld.vop2(aco_opcode::v_or_b32, bld.def(v1b),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v1b), a, Operand::c32(0),
                                   Operand::c32(16), Operand::c32(0)),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v1b), b, Operand::c32(1),
                                   Operand::c32(16), Operand::c32(0))));

   //! v2b: %res4 = v_cvt_f16_i16 %a dst_sel:uword0 dst_preserve src0_sel:sbyte0
   //! p_unit_test 4, %res4
   writeout(4, bld.vop1(aco_opcode::v_cvt_f16_i16, bld.def(v2b),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v2b), a, Operand::c32(0),
                                   Operand::c32(8), Operand::c32(1))));

   //! v2b: %res5 = v_or_b32 %a, %b dst_sel:uword0 dst_preserve src0_sel:sbyte0 src1_sel:sbyte1
   //! p_unit_test 5, %res5
   writeout(5, bld.vop2(aco_opcode::v_or_b32, bld.def(v2b),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v2b), a, Operand::c32(0),
                                   Operand::c32(8), Operand::c32(1)),
                        bld.pseudo(aco_opcode::p_extract, bld.def(v2b), b, Operand::c32(1),
                                   Operand::c32(8), Operand::c32(1))));

   finish_opt_test();
END_TEST

BEGIN_TEST(optimize.sdwa.extract_vector)
   //>> v1: %a:v[0] = p_startpgm
   if (!setup_cs("v1", GFX10_3))
      return;

   Temp a = inputs[0];

   //! v1b: %res0 = p_extract_vector %a, 0
   //! p_unit_test 0, %res0
   writeout(
      0, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ubyte(a, 0), Operand::c32(0)));

   //! v1b: %res1 = p_extract_vector %a, 1
   //! p_unit_test 1, %res1
   writeout(
      1, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ubyte(a, 1), Operand::c32(0)));

   //! v1b: %res2 = p_extract_vector %a, 2
   //! p_unit_test 2, %res2
   writeout(
      2, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ubyte(a, 2), Operand::c32(0)));

   //! v1b: %res3 = p_extract_vector %a, 3
   //! p_unit_test 3, %res3
   writeout(
      3, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ubyte(a, 3), Operand::c32(0)));

   //! v1b: %res4 = p_extract_vector %a, 0
   //! p_unit_test 4, %res4
   writeout(
      4, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ushort(a, 0), Operand::c32(0)));

   //! v1b: %res5 = p_extract_vector %a, 2
   //! p_unit_test 5, %res5
   writeout(
      5, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ushort(a, 1), Operand::c32(0)));

   //! v1b: %res6 = p_extract_vector %a, 1
   //! p_unit_test 6, %res6
   writeout(
      6, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ushort(a, 0), Operand::c32(1)));

   //! v1b: %res7 = p_extract_vector %a, 3
   //! p_unit_test 7, %res7
   writeout(
      7, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ushort(a, 1), Operand::c32(1)));

   //! v1: %res8_tmp = p_extract %a, 0, 8, 0
   //! v1b: %res8 = p_extract_vector %res8_tmp, 1
   //! p_unit_test 8, %res8
   writeout(
      8, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ubyte(a, 0), Operand::c32(1)));

   //! v1: %res9_tmp = p_extract %a, 0, 16, 0
   //! v1b: %res9 = p_extract_vector %res9_tmp, 2
   //! p_unit_test 9, %res9
   writeout(
      9, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ushort(a, 0), Operand::c32(2)));

   //! v1: %res10_tmp = p_extract %a, 1, 16, 0
   //! v1b: %res10 = p_extract_vector %res10_tmp, 2
   //! p_unit_test 10, %res10
   writeout(10, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ushort(a, 1),
                           Operand::c32(2)));

   //! v1: %res11_tmp = p_extract %a, 1, 8, 0
   //! v1b: %res11 = p_extract_vector %res11_tmp, 2
   //! p_unit_test 11, %res11
   writeout(
      11, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), ext_ubyte(a, 1), Operand::c32(2)));

   //! v2b: %res12 = p_extract %a, 0, 8, 0
   //! p_unit_test 12, %res12
   writeout(
      12, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ubyte(a, 0), Operand::c32(0)));

   //! v2b: %res13 = p_extract %a, 1, 8, 0
   //! p_unit_test 13, %res13
   writeout(
      13, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ubyte(a, 1), Operand::c32(0)));

   //! v2b: %res14 = p_extract %a, 2, 8, 0
   //! p_unit_test 14, %res14
   writeout(
      14, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ubyte(a, 2), Operand::c32(0)));

   //! v2b: %res15 = p_extract %a, 3, 8, 0
   //! p_unit_test 15, %res15
   writeout(
      15, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ubyte(a, 3), Operand::c32(0)));

   //! v2b: %res16 = p_extract_vector %a, 0
   //! p_unit_test 16, %res16
   writeout(16, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ushort(a, 0),
                           Operand::c32(0)));

   //! v2b: %res17 = p_extract_vector %a, 1
   //! p_unit_test 17, %res17
   writeout(17, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ushort(a, 1),
                           Operand::c32(0)));

   //! v1: %res18_tmp = p_extract %a, 0, 8, 0
   //! v2b: %res18 = p_extract_vector %res18_tmp, 1
   //! p_unit_test 18, %res18
   writeout(
      18, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ubyte(a, 0), Operand::c32(1)));

   //! v1: %res19_tmp = p_extract %a, 0, 16, 0
   //! v2b: %res19 = p_extract_vector %res19_tmp, 1
   //! p_unit_test 19, %res19
   writeout(19, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v2b), ext_ushort(a, 0),
                           Operand::c32(1)));

   //! v1b: %res20 = p_extract_vector %a, 2
   //! p_unit_test 20, %res20
   writeout(20, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b),
                           bld.pseudo(aco_opcode::p_extract, bld.def(v2b), a, Operand::c32(1),
                                      Operand::c32(16), Operand::c32(false)),
                           Operand::c32(0)));

   //! v1b: %res21 = p_extract_vector %a, 3
   //! p_unit_test 21, %res21
   writeout(21, bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b),
                           bld.pseudo(aco_opcode::p_extract, bld.def(v2b), a, Operand::c32(1),
                                      Operand::c32(16), Operand::c32(false)),
                           Operand::c32(1)));

   finish_opt_test();
END_TEST

BEGIN_TEST(optimizer.sdwa.lanemask_extract)
   for (unsigned i = GFX10; i <= GFX11; i++) {
      if (i == GFX10_3)
         continue;

      //>> v1: %a:v[0],  v1: %b:v[1], s1: %c:s[0] = p_startpgm
      if (!setup_cs("v1 v1 s1", (amd_gfx_level)i, CHIP_UNKNOWN, "", 32))
         continue;

      Temp a = inputs[0];
      Temp b = inputs[1];
      Temp c = inputs[2];

      //! s1: %mask0,  s1: %_:scc = p_extract %c, 0, 16, 0
      //! v1: %res0 = v_cndmask_b32 %a, %b, %mask0
      //! p_unit_test 0, %res0
      Temp mask = ext_ushort(c, 0);
      Temp bcsel = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), a, b, mask);
      writeout(0, bcsel);

      //! s1: %mask1,  s1: %_:scc = p_extract %c, 2, 8, 1
      //! v1: %res1 = v_cndmask_b32 %a, %b, %mask1
      //! p_unit_test 1, %res1
      mask = ext_sbyte(c, 2);
      bcsel = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), a, b, mask);
      writeout(1, bcsel);

      //! s1: %mask2,  s1: %_:scc = p_extract %c, 3, 8, 0
      //! v1: %res2 = v_cndmask_b32 %a, %b, %mask2
      //! p_unit_test 2, %res2
      mask = ext_ubyte(c, 3);
      bcsel = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), a, b, mask);
      writeout(2, bcsel);

      finish_opt_test();
   }
END_TEST
