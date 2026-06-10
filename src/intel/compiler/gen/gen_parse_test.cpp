/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "dev/intel_device_info.h"
#include "gen.h"
#include "util/lut.h"
#include "util/ralloc.h"

struct GenParseTest : public ::testing::Test {
   intel_device_info devinfo = {};
   void *mem_ctx = ralloc_context(NULL);
   gen_inst *insts = nullptr;
   int num_insts = 0;
   gen_error *errors = nullptr;
   int num_errors = 0;

   ~GenParseTest() {
      ralloc_free(mem_ctx);
   }

   void
   set_devinfo(const char *name)
   {
      memset(&devinfo, 0, sizeof(devinfo));

      const int devid = intel_device_name_to_pci_device_id(name);
      EXPECT_NE(devid, -1);
      EXPECT_TRUE(intel_get_device_info_from_pci_id(devid, &devinfo));
   }

   bool
   parse(const char *text, int text_size = -1)
   {
      gen_parse_params params = {};
      params.devinfo = &devinfo;
      params.text = text;
      params.text_size = text_size < 0 ? (int)strlen(text) : text_size;
      params.mem_ctx = mem_ctx;

      const bool ok = gen_parse(&params);
      insts = params.insts;
      num_insts = params.num_insts;
      errors = params.errors;
      num_errors = params.num_errors;
      return ok;
   }

   std::string
   first_error() const
   {
      if (!num_errors)
         return "";
      return std::to_string(errors[0].index) + ": " + errors[0].msg;
   }
};

TEST_F(GenParseTest, ParsesDpasFunctionControl)
{
   set_devinfo("mtl");

   ASSERT_TRUE(parse(
      "dpas.8x4 (8|M0) r1.0<1>:d r2.0<8;8,1>:d r3.0<8;8,1>:hf r4.0<8;8,1>:hf\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_DPAS);
   EXPECT_EQ(insts[0].dpas.sdepth, 8);
   EXPECT_EQ(insts[0].dpas.rcount, 4);
}

TEST_F(GenParseTest, ParsesBfnFunctionControl)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse(
      "bfn.(a & b | ~a & c) (8|M0) r1.0<1>:ud r2.0<0;0>:ud r3.0<1;0>:ud r4.0<1>:ud\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_BFN);
   EXPECT_EQ(insts[0].boolean_func_ctrl, UTIL_LUT3((a & b) | (~a & c)));
}

TEST_F(GenParseTest, ParsesNumericBfnFunctionControl)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse(
      "bfn.0x9a (8|M0) r1.0<1>:ud r2.0<0;0>:ud r3.0<1;0>:ud r4.0<1>:ud\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_BFN);
   EXPECT_EQ(insts[0].boolean_func_ctrl, 0x9a);
}

TEST_F(GenParseTest, ParsesLtConditionAndAccumulator)
{
   set_devinfo("ptl");

   ASSERT_TRUE(parse(
      "(~f2.0) cmp (16|M0) (lt)f2.0 null<1>:f r63.0<1;1,0>:f acc0.0<1;1,0>:f\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].cmod, GEN_CONDITION_LT);
   EXPECT_EQ(insts[0].src[1].nr, GEN_ARF_ACCUMULATOR);
   EXPECT_EQ(insts[0].src[1].subnr, 0u);
}

TEST_F(GenParseTest, ParsesGtCondition)
{
   set_devinfo("ptl");

   ASSERT_TRUE(parse(
      "sel (8|M0) (gt)f1.1 r4.0<1>:f r5.0<1;1,0>:f r6.0<1;1,0>:f\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].cmod, GEN_CONDITION_GT);
}

TEST_F(GenParseTest, ParsesMathSqt)
{
   set_devinfo("ptl");

   ASSERT_TRUE(parse(
      "math.sqt (16|M0) r14.0<1>:f r13.0<1;1,0>:f null\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_MATH);
   EXPECT_EQ(insts[0].math.func, GEN_MATH_SQRT);
}

TEST_F(GenParseTest, ParsesBranchControlSuffix)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse("goto.b (16|M0) jip:L0 uip:L0\nL0:\n")) << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].branch_control);
   EXPECT_EQ(insts[0].src[0].imm, 16);
   EXPECT_EQ(insts[0].src[1].imm, 16);
}

TEST_F(GenParseTest, ParsesExecControlWithoutSpaceAfterOpcode)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse("add(8) r5 r6 0x0000002a\n")) << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_ADD);
   EXPECT_EQ(insts[0].exec_size, 8u);
   EXPECT_EQ(insts[0].dst.nr, 5u);
   EXPECT_EQ(insts[0].src[0].nr, 6u);
   EXPECT_EQ(insts[0].src[1].imm, 0x2au);
}

TEST_F(GenParseTest, ParsesExecControlWithoutSpaceAfterSendOpcode)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse("send.ugm(8|M0) r40 r35:1 null:0 a0.2 0x02100000\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_SEND);
   EXPECT_EQ(insts[0].exec_size, 8u);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_UGM);
   EXPECT_EQ(insts[0].src[0].nr, 35u);
}

TEST_F(GenParseTest, ParsesExecControlWithoutSpaceAfterLscOpcode)
{
   set_devinfo("bmg");

   ASSERT_TRUE(parse("load.ugm.d32x32t.a32.ca.cc.bss[a0.0](1|M0) r6 r5\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_SEND);
   EXPECT_EQ(insts[0].exec_size, 1u);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_UGM);
   EXPECT_EQ(insts[0].send.desc_imm, 0x2229e500u);
}

TEST_F(GenParseTest, ParsesInstructionOptions)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse(
      "mov (8|M0) r1.0<1>:f r2.0<8;8,1>:f {AccWrEn,Breakpoint,NoDDChk,NoDDClr}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].acc_wr_control);
   EXPECT_TRUE(insts[0].debug_control);
   EXPECT_TRUE(insts[0].no_dd_check);
   EXPECT_TRUE(insts[0].no_dd_clear);
}

TEST_F(GenParseTest, ParsesAlign16Option)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse("mov (8|M0) r1.0<1>:f r2.0<4;4,1>:f {Align16}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].align16);
   EXPECT_EQ(insts[0].dst.writemask, 0xfu);
   EXPECT_EQ(insts[0].src[0].swizzle, gen_swizzle4(0, 1, 2, 3));
}

TEST_F(GenParseTest, ParsesAlign16RepCtrl)
{
   set_devinfo("skl");

   ASSERT_TRUE(parse(
      "mad (8|M0) r1.0<1>:f r2.0<0;0>.r:f r3.0<0;0>.r:f r4.0<0>.r:f {Align16}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].align16);
   EXPECT_TRUE(insts[0].src[0].rep_ctrl);
   EXPECT_TRUE(insts[0].src[1].rep_ctrl);
   EXPECT_TRUE(insts[0].src[2].rep_ctrl);
   EXPECT_EQ(insts[0].src[0].region.vstride, 0u);
   EXPECT_EQ(insts[0].src[0].region.width, 1u);
   EXPECT_EQ(insts[0].src[0].region.hstride, 0u);
}

TEST_F(GenParseTest, ParsesSourceRegionShorthand)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse("mov (8|M0) r1.0:uw r2.0<2>:uw\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].src[0].region.vstride, 2u);
   EXPECT_EQ(insts[0].src[0].region.width, 1u);
   EXPECT_EQ(insts[0].src[0].region.hstride, 0u);
}

TEST_F(GenParseTest, ParsesAtomicOption)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse("mov (8|M0) r1.0<1>:f r2.0<8;8,1>:f {Atomic}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].atomic_control);
}

TEST_F(GenParseTest, ParsesSwitchOption)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse("mov (8|M0) r1.0<1>:f r2.0<8;8,1>:f {Switch}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].thread_control, GEN_THREAD_SWITCH);
}

TEST_F(GenParseTest, ParsesSerializeOption)
{
   set_devinfo("tgl");

   ASSERT_TRUE(parse(
      "send.ugm (8|M0) r40 r35:1 null:0 a0.2 0x02100000 {Serialize}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].fusion_control);
}

TEST_F(GenParseTest, ParsesSendLengthsAndExBso)
{
   set_devinfo("mtl");

   ASSERT_TRUE(parse(
      "send.ugm (8|M0) r40 r35:2 r8:3 a0.2 0x04100000 {ExBSO}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].src[0].nr, 35u);
   EXPECT_EQ(insts[0].src[1].nr, 8u);
   EXPECT_EQ(insts[0].send.src1_len, 3u);
   EXPECT_TRUE(insts[0].send.ex_bso);
}

TEST_F(GenParseTest, ParsesSendExtendedDescriptorImmediateOffset)
{
   set_devinfo("bmg");

   ASSERT_TRUE(parse(
      "send.ugm (8|M0) r40 r35:1 null:0 0x00000100:a0.2 0x02100000\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].send.ex_desc_is_reg);
   EXPECT_EQ(insts[0].send.ex_desc_subnr, 4u);
   EXPECT_EQ(insts[0].send.ex_desc_imm_extra, 0x100u);
}

TEST_F(GenParseTest, NormalizesLegacySendEotDescriptor)
{
   set_devinfo("skl");

   ASSERT_TRUE(parse(
      "(W) send.ts (8) null r127:1 null:0 0x00000000 0x82000010 {EOT}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_TRUE(insts[0].send.eot);
   EXPECT_EQ(insts[0].send.desc_imm, 0x02000010u);
}

TEST_F(GenParseTest, ParsesSfidTsAliasOnSkl)
{
   set_devinfo("skl");

   ASSERT_TRUE(parse("send.ts (8) null r127:1 0x00000000 0x02000010 {EOT}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_THREAD_SPAWNER);
}

TEST_F(GenParseTest, ParsesSfidBtdAliasOnDg2)
{
   set_devinfo("dg2");

   ASSERT_TRUE(parse(
      "send.btd (8) null r127:1 null:0 0x00000000 0x02000010 {EOT}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_BINDLESS_THREAD_DISPATCH);
}

TEST_F(GenParseTest, ParsesSfidGtwyAliasOnDg2)
{
   set_devinfo("dg2");

   ASSERT_TRUE(parse(
      "send.gtwy (8) null r127:1 null:0 0x00000000 0x02000010 {EOT}\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_MESSAGE_GATEWAY);
}

TEST_F(GenParseTest, ParsesLscUgmSourceSyntax)
{
   set_devinfo("bmg");

   ASSERT_TRUE(parse(
      "load.ugm.d32x32t.a32.ca.cc.bss[a0.0] (1|M0) r6 r5\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_SEND);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_UGM);
   EXPECT_TRUE(insts[0].send.ex_desc_is_reg);
   EXPECT_EQ(insts[0].send.ex_desc_subnr, 0u);
   EXPECT_EQ(insts[0].send.desc_imm, 0x2229e500u);
}

TEST_F(GenParseTest, ParsesLscSlmSourceSyntax)
{
   set_devinfo("mtl");

   ASSERT_TRUE(parse("load.slm.d32x4.a32 (16|M0) r10 r8\n")) << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_SLM);
}

TEST_F(GenParseTest, ParsesLscTypedTgmSourceSyntax)
{
   set_devinfo("mtl");

   ASSERT_TRUE(parse(
      "load_cmask.tgm.d32.xy.a32.ca.ca.bti[3] (8|M0) r20 r12\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_TGM);
   EXPECT_FALSE(insts[0].send.ex_desc_is_reg);

   const gen_lsc_ex_desc ex_desc = gen_lsc_ex_desc_decode(
      &devinfo, LSC_ADDR_SURFTYPE_BTI, insts[0].send.ex_desc_imm, 0);
   EXPECT_EQ(ex_desc.bti.index, 3u);
   EXPECT_EQ(ex_desc.bti.base_offset, 0);

   const uint32_t desc_imm = insts[0].send.desc_imm;
   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(desc.op, LSC_OP_LOAD_CMASK);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_BTI);
   EXPECT_EQ(desc.addr_size, LSC_ADDR_SIZE_A32);
   EXPECT_EQ(desc.data_size, LSC_DATA_SIZE_D32);
   EXPECT_EQ(desc.cache_ctrl, LSC_CACHE_LOAD_L1C_L3C);
   EXPECT_EQ(desc.cmask, LSC_CMASK_XY);
   const gen_message_desc msg = gen_message_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(msg.msg_length, 4u);
   EXPECT_EQ(msg.response_length, 2u);
}

TEST_F(GenParseTest, ParsesLscUrbSourceSyntax)
{
   set_devinfo("bmg");

   ASSERT_TRUE(parse("load.urb.d32.a32 (16|M0) r10 r20\n")) << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_URB);
   EXPECT_FALSE(insts[0].send.ex_desc_is_reg);
   EXPECT_EQ(insts[0].send.ex_desc_imm, 0u);

   const uint32_t desc_imm = insts[0].send.desc_imm;
   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(desc.op, LSC_OP_LOAD);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_FLAT);
   EXPECT_EQ(desc.addr_size, LSC_ADDR_SIZE_A32);
   EXPECT_EQ(desc.data_size, LSC_DATA_SIZE_D32);
   EXPECT_EQ(desc.vect_size, LSC_VECT_SIZE_V1);
   const gen_message_desc msg = gen_message_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(msg.msg_length, 1u);
   EXPECT_EQ(msg.response_length, 1u);
}

TEST_F(GenParseTest, ParsesLscAtomicSourceSyntax)
{
   set_devinfo("mtl");

   ASSERT_TRUE(parse(
      "atomic_add.ugm.d32.a32.wt.wb.bss[a0.0] (8|M0) r40 r35 r8\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_UGM);
   EXPECT_TRUE(insts[0].send.ex_desc_is_reg);
   EXPECT_EQ(insts[0].send.ex_desc_subnr, 0u);
   EXPECT_EQ(insts[0].send.src1_len, 1u);

   const uint32_t desc_imm = insts[0].send.desc_imm;
   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(desc.op, LSC_OP_ATOMIC_ADD);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_BSS);
   EXPECT_EQ(desc.addr_size, LSC_ADDR_SIZE_A32);
   EXPECT_EQ(desc.data_size, LSC_DATA_SIZE_D32);
   EXPECT_EQ(desc.cache_ctrl, LSC_CACHE_STORE_L1WT_L3WB);
   const gen_message_desc msg = gen_message_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(msg.msg_length, 1u);
   EXPECT_EQ(msg.response_length, 1u);
}

TEST_F(GenParseTest, ParsesLscFenceSourceSyntax)
{
   set_devinfo("mtl");

   ASSERT_TRUE(parse("fence.ugm.gpu.evict.route_to_lsc (8|M0) r2 r0\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_UGM);

   const uint32_t desc_imm = insts[0].send.desc_imm;
   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(desc.op, LSC_OP_FENCE);
   EXPECT_EQ(desc.fence.scope, LSC_FENCE_GPU);
   EXPECT_EQ(desc.fence.flush_type, LSC_FLUSH_TYPE_EVICT);
   EXPECT_TRUE(desc.fence.route_to_lsc);
   const gen_message_desc msg = gen_message_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(msg.msg_length, 1u);
   EXPECT_EQ(msg.response_length, 1u);
}

TEST_F(GenParseTest, ParsesLscUrbFenceSourceSyntax)
{
   set_devinfo("bmg");

   ASSERT_TRUE(parse("fence.urb.gpu.evict (8|M0) r2 r0\n")) << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.sfid, GEN_SFID_URB);

   const uint32_t desc_imm = insts[0].send.desc_imm;
   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(desc.op, LSC_OP_FENCE);
   EXPECT_EQ(desc.fence.scope, LSC_FENCE_GPU);
   EXPECT_EQ(desc.fence.flush_type, LSC_FLUSH_TYPE_EVICT);
   EXPECT_FALSE(desc.fence.route_to_lsc);
   const gen_message_desc msg = gen_message_desc_decode(&devinfo, desc_imm);
   EXPECT_EQ(msg.msg_length, 1u);
   EXPECT_EQ(msg.response_length, 1u);
}

TEST_F(GenParseTest, IgnoresTrailingLscAnnotationComment)
{
   set_devinfo("mtl");

   ASSERT_TRUE(parse(
      "send.ugm (1|M0) r6 r5:1 null:0 a0.0 0x2229e500"
      " // load.ugm.d32x32t.a32.ca.cc.bss[a0.0]\n"))
      << first_error();

   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].send.desc_imm, 0x2229e500u);
}

TEST_F(GenParseTest, ParsesNonNullTerminatedInput)
{
   set_devinfo("tgl");

   const char *text = "mov (8) r1 r2|garbage that would fail to parse";
   const int valid_size = strchr(text, '|') - text;

   ASSERT_TRUE(parse(text, valid_size)) << first_error();
   ASSERT_EQ(num_insts, 1);
   EXPECT_EQ(insts[0].opcode, GEN_OP_MOV);
   EXPECT_EQ(insts[0].dst.nr, 1u);
   EXPECT_EQ(insts[0].src[0].nr, 2u);
}
