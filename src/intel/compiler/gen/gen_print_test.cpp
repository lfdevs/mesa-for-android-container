/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "dev/intel_device_info.h"
#include "gen.h"
#include "util/ralloc.h"

static std::optional<std::string>
gen_print_to_string(gen_print_params *params)
{
   char *str = NULL;
   size_t size = 0;
   FILE *fp = open_memstream(&str, &size);
   if (!fp)
      return std::nullopt;

   params->fp = fp;
   /* Tests want deterministic output, regardless of INTEL_DEBUG. */
   params->flags = (gen_print_flags)(params->flags | GEN_PRINT_IGNORE_ENV);
   const bool ok = gen_print(params);
   fclose(fp);

   if (!ok) {
      free(str);
      return std::nullopt;
   }

   std::string out(str ? str : "");
   free(str);
   return out;
}

static std::string
gen_print_swsb_to_string(const intel_device_info *devinfo, gen_swsb swsb)
{
   char buf[128];
   FILE *fp = fmemopen(buf, sizeof(buf), "w");
   if (!fp)
      return "";

   gen_print_swsb(devinfo, fp, swsb);
   fflush(fp);
   const long n = ftell(fp);
   fclose(fp);

   return std::string(buf, (size_t)std::max(n, 0L));
}

namespace {

static gen_operand
grf(unsigned nr, unsigned subnr, gen_region region = {},
    gen_reg_type type = GEN_TYPE_UD)
{
   gen_operand op = {};
   op.file = GEN_GRF;
   op.nr = nr;
   op.subnr = (uint8_t)subnr;
   op.region = region;
   op.type = type;
   return op;
}

} /* namespace */

struct GenPrintTest : public ::testing::Test {
   static constexpr int CAP = 256;

   intel_device_info devinfo;
   gen_inst insts[CAP] = {};
   int num_insts = 0;

   void
   set_devinfo(const char *name)
   {
      memset(&devinfo, 0, sizeof(devinfo));

      const int devid = intel_device_name_to_pci_device_id(name);
      EXPECT_NE(devid, -1);
      EXPECT_TRUE(intel_get_device_info_from_pci_id(devid, &devinfo));
   }

   gen_inst *append(gen_opcode op) {
      assert(num_insts < CAP);
      gen_inst *inst = &insts[num_insts++];
      inst->opcode = op;
      return inst;
   }

   gen_inst *append(gen_opcode op, gen_operand dst, gen_operand src0,
                    gen_operand src1 = {}) {
      gen_inst *inst = append(op);
      inst->exec_size = devinfo.ver >= 20 ? 16 : 8;
      inst->dst = dst;
      inst->src[0] = src0;
      inst->src[1] = src1;
      return inst;
   }

   std::string
   print_program(gen_print_flags flags = GEN_PRINT_NONE,
                 gen_print_params params = {})
   {
      params.devinfo = &devinfo;
      params.flags = (gen_print_flags)(params.flags | flags);
      params.insts = insts;
      params.num_insts = num_insts;

      auto out = gen_print_to_string(&params);
      EXPECT_TRUE(out.has_value());
      return out.value_or("");
   }

   std::vector<uint8_t>
   encode_program()
   {
      void *mem_ctx = ralloc_context(NULL);
      const int raw_bytes_size = num_insts > 0 ?
         num_insts * (int)sizeof(gen_raw_inst) : 1;

      std::vector<uint8_t> raw(raw_bytes_size);
      gen_encode_params params = {};
      params.devinfo = &devinfo;
      params.insts = insts;
      params.num_insts = num_insts;
      params.mem_ctx = mem_ctx;
      params.raw_bytes = raw.data();
      params.raw_bytes_size = raw.size();
      EXPECT_TRUE(gen_encode(&params));
      raw.resize(params.raw_bytes_size);

      ralloc_free(mem_ctx);
      return raw;
   }
};

TEST_F(GenPrintTest, PrefixAndTypeSyntax)
{
   set_devinfo("tgl");

   gen_inst *add = append(GEN_OP_ADD,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F),
                          gen_imm(GEN_TYPE_F, 0x3f800000));
   add->no_mask = true;
   add->pred_control = GEN_PREDICATE_NORMAL;
   add->pred_inv = true;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "(W&~f0.0) add (8|M0)              r1.0<1>:f     r2.0<8;8,1>:f     0x3f800000:f\n");
}
TEST_F(GenPrintTest, SourceRegionShorthand)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,
          grf(1, 0, { .hstride = 1 }, GEN_TYPE_UW),
          grf(2, 0, { 16, 8, 2 }, GEN_TYPE_UW));

   const std::string compact = print_program(GEN_PRINT_NONE);
   EXPECT_NE(compact.find("r2<2>:uw"), std::string::npos) << compact;

   const std::string verbose = print_program(GEN_PRINT_VERBOSE);
   EXPECT_NE(verbose.find("r2.0<16;8,2>:uw"), std::string::npos) << verbose;
}

TEST_F(GenPrintTest, ConditionalModifierPlacement)
{
   set_devinfo("tgl");

   gen_inst *cmp = append(GEN_OP_CMP,
                          grf(5, 0, { .hstride = 1 }, GEN_TYPE_UD),
                          grf(6, 0, { 8, 8, 1 }, GEN_TYPE_UD),
                          grf(7, 0, { 8, 8, 1 }, GEN_TYPE_UD));
   cmp->exec_size = 16;
   cmp->cmod = GEN_CONDITION_GE;
   cmp->flag_nr = 3;
   cmp->flag_subnr = 1;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        cmp (16|M0)    (ge)f3.1   r5.0<1>:ud    r6.0<8;8,1>:ud    r7.0<8;8,1>:ud\n");
}

TEST_F(GenPrintTest, LtGtAndAccumulatorSubregSyntax)
{
   set_devinfo("ptl");

   gen_inst *cmp = append(GEN_OP_CMP,
                          gen_null(),
                          grf(63, 0, { 1, 1, 0 }, GEN_TYPE_F),
                          gen_accumulator(0));
   cmp->cmod = GEN_CONDITION_LT;
   cmp->flag_nr = 2;
   cmp->flag_subnr = 0;
   cmp->dst.type = GEN_TYPE_F;
   cmp->dst.region.hstride = 1;
   cmp->src[1].type = GEN_TYPE_F;
   cmp->src[1].region = { 1, 1, 0 };

   gen_inst *sel = append(GEN_OP_SEL,
                          grf(4, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(5, 0, { 1, 1, 0 }, GEN_TYPE_F),
                          grf(6, 0, { 1, 1, 0 }, GEN_TYPE_F));
   sel->exec_size = 8;
   sel->cmod = GEN_CONDITION_GT;
   sel->flag_nr = 1;
   sel->flag_subnr = 1;
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        cmp (16|M0)    (lt)f2.0   null<1>:f     r63.0<1;1,0>:f    acc0.0<1;1,0>:f\n"
             "        sel (8|M0)     (gt)f1.1   r4.0<1>:f     r5.0<1;1,0>:f     r6.0<1;1,0>:f\n");
}

TEST_F(GenPrintTest, MathSqtSyntax)
{
   set_devinfo("ptl");

   gen_inst *math = append(GEN_OP_MATH,
                           grf(14, 0, { .hstride = 1 }, GEN_TYPE_F),
                           grf(13, 0, { 1, 1, 0 }, GEN_TYPE_F),
                           gen_null());
   math->math.func = GEN_MATH_SQRT;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        math.sqt (16|M0)          r14.0<1>:f    r13.0<1;1,0>:f    null\n");
}

TEST_F(GenPrintTest, SyntheticLabels)
{
   set_devinfo("tgl");

   gen_inst *go = append(GEN_OP_GOTO,
                         gen_null(),
                         gen_imm_d(32),
                         gen_imm_d(32));
   go->exec_size = 16;

   append(GEN_OP_NOP);
   append(GEN_OP_NOP);

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        goto (16|M0)                          jip:L0              uip:L0\n"
             "        nop\n"
             "\n"
             "L0:\n"
             "        nop\n");
}

TEST_F(GenPrintTest, LabelAnnotationFallsBackToAnnotation)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,

             grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),

             grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));

   const char *label_annotations[] = {
      "42 cycles",
   };

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .label_annotations = label_annotations }),
             "\n// 42 cycles\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST_F(GenPrintTest, LabelAnnotationUsesExistingSyntheticLabel)
{
   set_devinfo("tgl");

   gen_inst *go = append(GEN_OP_GOTO,
                         gen_null(),
                         gen_imm_d(32),
                         gen_imm_d(32));
   go->exec_size = 16;

   append(GEN_OP_NOP);
   append(GEN_OP_NOP);

   const char *label_annotations[] = {
      NULL,
      NULL,
      "42 cycles",
   };

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .label_annotations = label_annotations }),
             "        goto (16|M0)                          jip:L0              uip:L0\n"
             "        nop\n"
             "\n"
             "L0: // 42 cycles\n"
             "        nop\n");
}

TEST_F(GenPrintTest, SingleInstructionDefaultUsesRawOffsets)
{
   set_devinfo("tgl");

   gen_inst *go = append(GEN_OP_GOTO,
                         gen_null(),
                         gen_imm_d(32),
                         gen_imm_d(32));
   go->exec_size = 16;

   EXPECT_EQ(print_program(),
             "        goto (16)                             jip:0x20            uip:0x20\n");
}

TEST_F(GenPrintTest, ExecControlUsesRawChannelOffset)
{
   set_devinfo("tgl");

   gen_inst *mad = append(GEN_OP_MAD,
                          grf(10, 16, { .hstride = 1 }, GEN_TYPE_F),
                          grf(11, 0, { 0, 1, 0 }, GEN_TYPE_F),
                          grf(12, 16, { 1, 1, 0 }, GEN_TYPE_F));
   mad->chan_offset = 24;

   mad->src[2] = grf(13, 32, { 8, 8, 1 }, GEN_TYPE_F);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        mad (8|M24)               r10.4<1>:f    r11.0<0;0>:f      r12.4<1;0>:f      r13.8<1>:f\n");
}

TEST_F(GenPrintTest, ThreeSourceRegionShorthandMad)
{
   set_devinfo("tgl");

   gen_inst *mad = append(GEN_OP_MAD,
                          grf(70, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(2, 32, { 0, 1, 0 }, GEN_TYPE_F),
                          grf(24, 0, { 1, 1, 0 }, GEN_TYPE_F));
   mad->exec_size = 32;

   mad->src[2] = grf(2, 40, { 0, 1, 0 }, GEN_TYPE_F);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        mad (32|M0)               r70.0<1>:f    r2.8<0;0>:f       r24.0<1;0>:f      r2.10<0>:f\n");
}

TEST_F(GenPrintTest, ThreeSourceRegionShorthandAdd3)
{
   set_devinfo("tgl");

   gen_inst *add3 = append(GEN_OP_ADD3,
                           grf(3, 0, { .hstride = 1 }, GEN_TYPE_D),
                           grf(19, 4, { 0, 1, 0 }, GEN_TYPE_D),
                           grf(21, 0, { 1, 1, 0 }, GEN_TYPE_D));
   add3->exec_size = 16;

   add3->src[2] = grf(2, 0, { 0, 1, 1 }, GEN_TYPE_D);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        add3 (16|M0)              r3.0<1>:d     r19.1<0;0>:d      r21.0<1;0>:d      r2.0<1>:d\n");
}

TEST_F(GenPrintTest, DefaultModeOmitsDefaultPieces)
{
   set_devinfo("tgl");

   append(GEN_OP_ADD,
          grf(5, 0, { .hstride = 1 }, GEN_TYPE_UD),
          grf(6, 0, { 1, 1, 0 }, GEN_TYPE_UD),
          gen_imm_ud(0x2a));
   EXPECT_EQ(print_program(),
             "        add (8)                   r5            r6                0x0000002a\n");
}

TEST_F(GenPrintTest, DefaultModeThreeSourceDefaults)
{
   set_devinfo("tgl");

   gen_inst *add3 = append(GEN_OP_ADD3,
                           grf(3, 0, { .hstride = 1 }, GEN_TYPE_D),
                           grf(19, 4, { 0, 1, 0 }, GEN_TYPE_D),
                           grf(21, 0, { 1, 1, 0 }, GEN_TYPE_D));
   add3->exec_size = 16;

   add3->src[2] = grf(2, 0, { 0, 1, 1 }, GEN_TYPE_D);
   EXPECT_EQ(print_program(),
             "        add3 (16)                 r3:d          r19.1<0>:d        r21:d             r2<1>:d\n");
}

TEST_F(GenPrintTest, DefaultModeUsesUniformRegionShorthand)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,
          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
          grf(2, 0, { 0, 1, 0 }, GEN_TYPE_F));
   EXPECT_EQ(print_program(),
             "        mov (8)                   r1:f          r2<0>:f\n");
}

TEST_F(GenPrintTest, DefaultModeKeepsNonZeroChannelOffset)
{
   set_devinfo("tgl");

   gen_inst *mov = append(GEN_OP_MOV,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_UD),
                          grf(2, 0, { 1, 1, 0 }, GEN_TYPE_UD));
   mov->chan_offset = 24;
   EXPECT_EQ(print_program(),
             "        mov (8|M24)               r1            r2\n");
}

TEST_F(GenPrintTest, RawSendFormatting)
{
   set_devinfo("tgl");

   gen_inst *send = append(GEN_OP_SEND,
                           grf(40, 0),
                           grf(35, 0),
                           gen_null());
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x2229e500;
   send->swsb = { 2, GEN_PIPE_INT, 5, GEN_SBID_SET };

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.ugm (1|M0)           r40     r35    null    a0.2        0x2229E500   {@2,$5}\n");
}

TEST_F(GenPrintTest, RawSendSwsbKeepsPipeLetterOnXeHpg)
{
   set_devinfo("dg2");

   gen_inst *send = append(GEN_OP_SEND,
                           grf(40, 0),
                           grf(35, 0),
                           gen_null());
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x2229e500;
   send->swsb = { 2, GEN_PIPE_INT, 5, GEN_SBID_SET };
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.ugm (1|M0)           r40     r35    null    a0.2        0x2229E500   {I@2,$5}  // wr:1+a0.2, rd:2; load.ugm.d32x32t.a32.ca.ca.bss[a0.2]\n");
}

TEST_F(GenPrintTest, SwsbHelperCanonicalOutputTgl)
{
   set_devinfo("tgl");

   struct {
      gen_swsb swsb;
      const char *expected;
   } cases[] = {
      { gen_swsb_null(),                       "" },
      { { 2, GEN_PIPE_INT },                   "@2" },
      { { 1, GEN_PIPE_FLOAT },                 "@1" },
      { { 3, GEN_PIPE_ALL },                   "@3" },
      { gen_swsb_sbid(GEN_SBID_SET, 3),        "$3" },
      { gen_swsb_sbid(GEN_SBID_DST, 3),        "$3.dst" },
      { gen_swsb_sbid(GEN_SBID_SRC, 3),        "$3.src" },
      { { 2, GEN_PIPE_INT, 5, GEN_SBID_SET },  "@2 $5" },
      { { 1, GEN_PIPE_INT, 3, GEN_SBID_DST },  "@1 $3.dst" },
      { { 1, GEN_PIPE_INT, 3, GEN_SBID_SRC },  "@1 $3.src" },
   };

   for (const auto &c : cases)
      EXPECT_EQ(gen_print_swsb_to_string(&devinfo, c.swsb), c.expected);
}

TEST_F(GenPrintTest, SwsbHelperCanonicalOutputDg2)
{
   set_devinfo("dg2");

   struct {
      gen_swsb swsb;
      const char *expected;
   } cases[] = {
      { gen_swsb_null(),                        "" },
      { { 2, GEN_PIPE_INT },                    "I@2" },
      { { 1, GEN_PIPE_FLOAT },                  "F@1" },
      { { 1, GEN_PIPE_LONG },                   "L@1" },
      { { 1, GEN_PIPE_ALL },                    "A@1" },
      { { 1, GEN_PIPE_MATH },                   "M@1" },
      { { 1, GEN_PIPE_SCALAR },                 "S@1" },
      { gen_swsb_sbid(GEN_SBID_SET, 3),         "$3" },
      { gen_swsb_sbid(GEN_SBID_DST, 3),         "$3.dst" },
      { gen_swsb_sbid(GEN_SBID_SRC, 3),         "$3.src" },
      { { 2, GEN_PIPE_INT, 5, GEN_SBID_SET },   "I@2 $5" },
      { { 2, GEN_PIPE_INT, 5, GEN_SBID_DST },   "I@2 $5.dst" },
      { { 2, GEN_PIPE_INT, 5, GEN_SBID_SRC },   "I@2 $5.src" },
      { { 1, GEN_PIPE_FLOAT, 3, GEN_SBID_SET }, "F@1 $3" },
   };

   for (const auto &c : cases)
      EXPECT_EQ(gen_print_swsb_to_string(&devinfo, c.swsb), c.expected);
}

TEST_F(GenPrintTest, RawSendSfidNamingByPlatform)
{

   gen_inst *send = append(GEN_OP_SEND,
                           gen_null(),
                           grf(127, 0),
                           gen_null());
   send->exec_size = 8;
   send->no_mask = true;
   send->send.ex_desc_imm = 0;
   send->send.desc_imm = 0x02000010;
   send->send.eot = true;

   {
      set_devinfo("skl");
      send->send.sfid = GEN_SFID_THREAD_SPAWNER;
      EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
                "(W)     send.ts (8|M0)            null    r127           0x00000000  0x02000010   {EOT}\n");
   }

   {
      set_devinfo("dg2");
      send->send.sfid = GEN_SFID_BINDLESS_THREAD_DISPATCH;
      EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
                "(W)     send.btd (8|M0)           null    r127   null:0  0x00000000  0x02000010   {EOT}  // wr:1+0, rd:0; ?\n");
   }

   {
      set_devinfo("dg2");
      send->send.sfid = GEN_SFID_MESSAGE_GATEWAY;
      EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
                "(W)     send.gtwy (8|M0)          null    r127   null:0  0x00000000  0x02000010   {EOT}  // wr:1+0, rd:0; ?\n");
   }
}

TEST_F(GenPrintTest, DpasFunctionControlFormatting)
{
   set_devinfo("mtl");

   gen_inst *dpas = append(GEN_OP_DPAS,
                           grf(1, 0, { .hstride = 1 }, GEN_TYPE_D),
                           grf(2, 0, { 8, 8, 1 }, GEN_TYPE_D),
                           grf(3, 0, { 8, 8, 1 }, GEN_TYPE_HF));
   dpas->dpas.sdepth = 8;
   dpas->dpas.rcount = 4;
   dpas->src[2] = grf(4, 0, { 8, 8, 1 }, GEN_TYPE_HF);

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        dpas.8x4 (8|M0)           r1.0<1>:d     r2.0<8;8,1>:d     r3.0<8;8,1>:hf    r4.0<8;8,1>:hf\n");
}

TEST_F(GenPrintTest, BfnFunctionControlFormatting)
{
   set_devinfo("tgl");

   gen_inst *bfn = append(GEN_OP_BFN,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_UD),
                          grf(2, 0, { 1, 1, 0 }, GEN_TYPE_UD),
                          grf(3, 0, { 1, 1, 0 }, GEN_TYPE_UD));
   bfn->boolean_func_ctrl = 0x9a;
   bfn->src[2] = grf(4, 0, { 0, 1, 1 }, GEN_TYPE_UD);

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        bfn.(a ^ ~b & c) (8|M0)   r1.0<1>:ud    r2.0<1;0>:ud      r3.0<1;0>:ud      r4.0<1>:ud\n");
}

TEST_F(GenPrintTest, BranchControlSuffixFormatting)
{
   set_devinfo("tgl");

   gen_inst *go = append(GEN_OP_GOTO,
                         gen_null(),
                         gen_imm_d(16),
                         gen_imm_d(16));
   go->exec_size = 16;
   go->branch_control = true;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        goto.b (16|M0)                        jip:L0              uip:L0\n"
             "L0:\n");
}

TEST_F(GenPrintTest, InstructionOptionFormatting)
{
   set_devinfo("tgl");

   gen_inst *mov = append(GEN_OP_MOV,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));
   mov->acc_wr_control = true;
   mov->debug_control = true;
   mov->no_dd_check = true;
   mov->no_dd_clear = true;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {AccWrEn,Breakpoint,NoDDChk,NoDDClr}\n");
}

TEST_F(GenPrintTest, RawBytesOnlyByteOffsetFormatting)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,

             grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),

             grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));
   const std::vector<uint8_t> raw = encode_program();

   gen_print_params params = {};
   params.devinfo = &devinfo;
   params.flags = (gen_print_flags)(GEN_PRINT_BYTE_OFFSETS | GEN_PRINT_VERBOSE);
   params.raw_bytes = raw.data();
   params.raw_bytes_size = raw.size();
   params.address_base = 0x20;

   EXPECT_EQ(gen_print_to_string(&params).value_or(""),
             "0x00000020:         mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST_F(GenPrintTest, InternalValidateMatchesExternalValidate)
{
   set_devinfo("tgl");

   gen_inst *mov = append(GEN_OP_MOV,
                          grf(0, 0, { .hstride = 1 }, GEN_TYPE_D),
                          gen_null());
   mov->src[0].type = GEN_TYPE_D;

   void *mem_ctx = ralloc_context(NULL);
   gen_validate_params validate = {};
   validate.devinfo = &devinfo;
   validate.insts = insts;
   validate.num_insts = num_insts;
   validate.mem_ctx = mem_ctx;
   ASSERT_FALSE(gen_validate(&validate));

   gen_print_params external = {};
   external.errors = validate.errors;
   external.num_errors = validate.num_errors;

   gen_print_params internal = {};
   internal.validate = true;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE, internal),
             print_program(GEN_PRINT_VERBOSE, external));

   ralloc_free(mem_ctx);
}

TEST_F(GenPrintTest, InternalValidateDoesNotWriteBackErrors)
{
   set_devinfo("tgl");

   gen_inst *mov = append(GEN_OP_MOV,
                          grf(0, 0, { .hstride = 1 }, GEN_TYPE_D),
                          gen_null());
   mov->src[0].type = GEN_TYPE_D;

   char *str = NULL;
   size_t size = 0;
   FILE *fp = open_memstream(&str, &size);

   gen_print_params params = {};
   params.devinfo = &devinfo;
   params.fp = fp;
   params.insts = insts;
   params.num_insts = num_insts;
   params.validate = true;
   ASSERT_TRUE(gen_print(&params));

   fclose(fp);
   std::string out(str ? str : "");
   free(str);

   EXPECT_NE(out.find("ERROR:"), std::string::npos);
   EXPECT_EQ(params.num_errors, 0);
   EXPECT_EQ(params.errors, nullptr);
}

TEST_F(GenPrintTest, Align16OptionFormatting)
{
   set_devinfo("tgl");

   gen_inst *mov = append(GEN_OP_MOV,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(2, 0, { 4, 4, 1 }, GEN_TYPE_F));
   mov->align16 = true;
   mov->dst.writemask = 0xf;
   mov->src[0].swizzle = gen_swizzle4(0, 1, 2, 3);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        mov (8|M0)                r1.0<1>:f     r2.0<4;4,1>:f                     {Align16}\n");
}

TEST_F(GenPrintTest, Align16RepCtrlFormatting)
{
   set_devinfo("skl");

   gen_inst *mad = append(GEN_OP_MAD,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(2, 0, { 0, 1, 0 }, GEN_TYPE_F),
                          grf(3, 0, { 0, 1, 0 }, GEN_TYPE_F));
   mad->align16 = true;
   mad->dst.writemask = 0xf;

   mad->src[0].rep_ctrl = true;

   mad->src[1].rep_ctrl = true;

   mad->src[2] = grf(4, 0, { 0, 1, 0 }, GEN_TYPE_F);
   mad->src[2].rep_ctrl = true;
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        mad (8|M0)                r1.0<1>:f     r2.0<0;0>.r:f     r3.0<0;0>.r:f     r4.0<0>.r:f {Align16}\n");
}

TEST_F(GenPrintTest, AtomicOptionFormatting)
{
   set_devinfo("tgl");

   gen_inst *mov = append(GEN_OP_MOV,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));
   mov->atomic_control = true;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Atomic}\n");
}

TEST_F(GenPrintTest, SwitchOptionFormatting)
{
   set_devinfo("tgl");

   gen_inst *mov = append(GEN_OP_MOV,
                          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
                          grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));
   mov->thread_control = GEN_THREAD_SWITCH;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Switch}\n");
}

TEST_F(GenPrintTest, SerializeOptionFormatting)
{
   set_devinfo("tgl");

   gen_inst *send = append(GEN_OP_SEND,
                           grf(40, 0),
                           grf(35, 0),
                           gen_null());
   send->fusion_control = true;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x02100000;
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.ugm (8|M0)           r40     r35    null    a0.2        0x02100000   {Serialize}\n");
}

TEST_F(GenPrintTest, SendSrc1LengthAndExBsoFormatting)
{
   set_devinfo("mtl");

   /* Realistic LSC d8 load via BSS, mlen=2, rlen=1; src1_len=3 explicit
    * to cover the split-send/ExBSO formatting path. */
   gen_inst *send = append(GEN_OP_SEND,
                           grf(40, 0),
                           grf(35, 0),
                           grf(8, 0));
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   send->send.desc_imm = 0x24100100;
   send->send.src1_len = 3;
   send->send.ex_bso = true;
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.ugm (8|M0)           r40     r35    r8:3    a0.2        0x24100100   {ExBSO}  // wr:2+3, rd:1; load.ugm.d8.a32.bss[a0.2]\n");
}

TEST_F(GenPrintTest, SendExDescImmediateOffsetFormatting)
{
   set_devinfo("bmg");

   /* Realistic LSC d8 BSS load with a non-zero immediate base offset packed
    * into ex_desc_imm_extra. */
   gen_inst *send = append(GEN_OP_SEND,
                           grf(40, 0),
                           grf(35, 0),
                           gen_null());
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;
   /* ex_desc_imm_extra encodes a 17-bit base offset with bits split
    * across [31:19] and [15:12]; 0x80000 → packed bits[16:4] = 1 → 0x10. */
   send->send.ex_desc_imm_extra = 0x80000;
   send->send.desc_imm = 0x22100100;
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.ugm (8|M0)           r40     r35    null:0  0x00080000:a0.2 0x22100100  // wr:1+a0.2, rd:1; load.ugm.d8.a32.bss[a0.2][A+0x10]\n");
}

/* Real-world PTL send with a non-zero FLAT immediate base offset packed
 * into the ex-desc. The LSC translator should decode the offset symbolically
 * as '.flat[A+0xN]' instead of bailing out with a '?' fragment.
 */
TEST_F(GenPrintTest, SendUgmFlatBaseOffsetPtl)
{
   set_devinfo("ptl");

   gen_inst *send = append(GEN_OP_SEND,
                           grf(141, 0),
                           grf(8, 0),
                           gen_null());
   send->exec_size = 1;
   send->no_mask = true;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_imm = 0x00020000;
   send->send.desc_imm = 0x0210B580;
   send->swsb = { 4, GEN_PIPE_INT, 0, GEN_SBID_SET };
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "(W)     send.ugm (1|M0)           r141    r8     null:0  0x00020000  0x0210B580   {I@4,$0}  // wr:1+0, rd:1; load.ugm.d32x4t.a64.flat[A+0x20]\n");
}

/* Real-world PTL send with a BSS surface (a0.X) and a non-zero base offset
 * packed into ex_desc_imm_extra. The translator should decode the offset
 * symbolically as 'bss[a0.X][A+0xN]' rather than bailing.
 */
TEST_F(GenPrintTest, SendUgmBssBaseOffsetPtl)
{
   set_devinfo("ptl");

   gen_inst *send = append(GEN_OP_SEND,
                           grf(27, 0),
                           grf(139, 0),
                           gen_null());
   send->exec_size = 32;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 4;  /* a0.2 */
   send->send.ex_desc_imm_extra = 0x00080000;
   send->send.desc_imm = 0x24803500;
   send->send.ex_bso = true;
   send->swsb = { 4, GEN_PIPE_ALL, 5, GEN_SBID_SET };
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.ugm (32|M0)          r27     r139   null:0  0x00080000:a0.2 0x24803500 {ExBSO,A@4,$5}  // wr:2+a0.2, rd:8; load.ugm.d32x4.a32.bss[a0.2][A+0x10]\n");
}

/* Real-world PTL send with a BTI surface and a non-zero base offset packed
 * into the immediate ex-desc (top 8 bits = BTI index, bits 23..12 = signed
 * 12-bit offset). The translator should decode this as 'bti[N][A+0xK]'.
 */
TEST_F(GenPrintTest, SendUgmBtiBaseOffsetPtl)
{
   set_devinfo("ptl");

   gen_inst *send = append(GEN_OP_SEND,
                           grf(3, 0),
                           grf(191, 0),
                           gen_null());
   send->exec_size = 1;
   send->no_mask = true;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_imm = 0xFF040000;
   send->send.desc_imm = 0x6219C500;
   send->swsb = { 0, GEN_PIPE_NONE, 1, GEN_SBID_SET };
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "(W)     send.ugm (1|M0)           r3      r191   null:0  0xFF040000  0x6219C500   {$1}  // wr:1+0, rd:1; load.ugm.d32x8t.a32.ca.cc.bti[255][A+0x40]\n");
}

TEST_F(GenPrintTest, PreLscSamplerCommentSyntaxFormatting)
{
   set_devinfo("skl");

   /* sample_l, SIMD16 (matches exec_size, so elided), bti=3, sampler=0,
    * mlen=2, rlen=4, no header, no hp. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_SAMPLER;
   send->send.desc_imm = 0x04442003;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.smpl (16|M0)         r10     r20            0x00000000  0x04442003  // wr:2+0, rd:4; sample_l (16) bti(3) using sampler index 0\n");
}

TEST_F(GenPrintTest, PreLscSamplerBindlessCommentFormatting)
{
   set_devinfo("skl");

   /* sample_l, SIMD16, bti=252 (GEN_BTI_BINDLESS), sampler=0,
    * ex_desc in a0.3 (subnr = 6 selects a0.3 after divide-by-2),
    * mlen=2, rlen=4.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_SAMPLER;
   send->send.desc_imm = 0x044420FC;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 6;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.smpl (16|M0)         r10     r20            a0.3        0x044420FC  // wr:2+a0.3, rd:4; sample_l (16) bss(a0.3) using sampler index 0\n");
}

TEST_F(GenPrintTest, PreLscSamplerCommentNonDefaultSimdFormatting)
{
   set_devinfo("skl");

   /* Same as above but exec_size=16 with SIMD8 simd_mode: simd_mode no
    * longer matches the implied default for exec_size=16, so the mnemonic
    * emits ".simd8" explicitly.
    *   simd_mode = 1 -> bits 18:17 = 01 -> 0x00020000
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_SAMPLER;
   send->send.desc_imm = 0x04422003;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.smpl (16|M0)         r10     r20            0x00000000  0x04422003  // wr:2+0, rd:4; sample_l (16) bti(3) using sampler index 0\n");
}

TEST_F(GenPrintTest, PreLscUrbSimd8WriteCommentFormatting)
{
   set_devinfo("skl");

   /* simd8_write (opcode 7), offset=12, masked (bit 15), per_slot (bit 17),
    * mlen=5, rlen=0.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_URB;
   send->send.desc_imm = 0x0A0280C7;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.urb (8|M0)           null    r20            0x00000000  0x0A0280C7  // wr:5+0, rd:0; simd8_write off=12 masked per_slot (8)\n");
}

TEST_F(GenPrintTest, PreLscUrbSimd8ReadCommentFormatting)
{
   set_devinfo("skl");

   /* simd8_read (opcode 8), offset=0, not masked, not per_slot,
    * mlen=1, rlen=2.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_URB;
   send->send.desc_imm = 0x02200008;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.urb (8|M0)           r10     r20            0x00000000  0x02200008  // wr:1+0, rd:2; simd8_read (8)\n");
}

TEST_F(GenPrintTest, PreLscUrbFenceCommentFormatting)
{
   set_devinfo("dg2");

   /* URB fence (opcode 9), gfx12.5, mlen=1, rlen=0. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_URB;
   send->send.desc_imm = 0x02000009;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.urb (8|M0)           null    r20    null:0  0x00000000  0x02000009  // wr:1+0, rd:0; fence (8)\n");
}

TEST_F(GenPrintTest, PreLscUrbAtomicInterleaveCommentFormatting)
{
   set_devinfo("skl");

   /* atomic_add (opcode 6), offset=0, interleave (bit 15), mlen=1, rlen=0. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_URB;
   send->send.desc_imm = 0x02008006;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.urb (8|M0)           null    r20            0x00000000  0x02008006  // wr:1+0, rd:0; atomic_add interleave (8)\n");
}

TEST_F(GenPrintTest, PreLscHdc1UntypedReadCommentFormatting)
{
   set_devinfo("skl");

   /* untyped_read (msg_type 1), SIMD16 (matches exec_size=16, elided),
    * cmask xyzw (disable=0), bti=3, mlen=1, rlen=4.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_HDC1;
   send->send.desc_imm = 0x02405003;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc1 (16|M0)         r10     r20            0x00000000  0x02405003  // wr:1+0, rd:4; untyped_read:xyzw simd16 (16) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc1UntypedAtomicAddCommentFormatting)
{
   set_devinfo("skl");

   /* untyped_atomic (msg_type 2), aop=ADD(7), SIMD8 (matches exec_size=8),
    * bti=3, mlen=2, rlen=1.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_HDC1;
   send->send.desc_imm = 0x04109703;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc1 (8|M0)          r10     r20            0x00000000  0x04109703  // wr:2+0, rd:1; untyped_atomic_add simd8 (8) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc1UntypedAtomicFloatAddCommentFormatting)
{
   set_devinfo("skl");

   /* untyped_atomic_float (msg_type 0x1b), aop=FADD(4), SIMD16, bti=3,
    * mlen=2, rlen=2. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_HDC1;
   send->send.desc_imm = 0x0426C403;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc1 (16|M0)         r10     r20            0x00000000  0x0426C403  // wr:2+0, rd:2; untyped_atomic_fadd simd16 (16) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc1A64OwordBlockReadCommentFormatting)
{
   set_devinfo("skl");

   /* a64_oword_block_read (msg_type 0x14), owords=4 (BLOCK_4_OWORDS=3),
    * aligned=0, bti=0xFF, mlen=1, rlen=4. No bti tail (A64).
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_HDC1;
   send->send.desc_imm = 0x024503FF;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc1 (1|M0)          r10     r20            0x00000000  0x024503FF  // wr:1+0, rd:4; a64_oword_block_read:owords4 (1) flat+0x0\n");
}

TEST_F(GenPrintTest, PreLscHdcRoOwordBlockReadCommentFormatting)
{
   set_devinfo("skl");

   /* HDC_READ_ONLY oword_block_read (msg_type 0), owords=4, bti=1,
    * mlen=1, rlen=2.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_HDC_READ_ONLY;
   send->send.desc_imm = 0x02200301;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc_ro (1|M0)        r10     r20            0x00000000  0x02200301  // wr:1+0, rd:2; oword_block_read:owords4 (1) bti(1)\n");
}

TEST_F(GenPrintTest, PreLscHdc0OwordBlockReadCommentFormatting)
{
   set_devinfo("skl");

   /* HDC0 oword_block_read (msg_type 0), owords=4 (BLOCK_4_OWORDS=3),
    * bti=3, mlen=1, rlen=2.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_HDC0;
   send->send.desc_imm = 0x02200303;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc0 (1|M0)          r10     r20            0x00000000  0x02200303  // wr:1+0, rd:2; oword_block_read:owords4 (1) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc0ByteScatteredReadCommentFormatting)
{
   set_devinfo("skl");

   /* byte_scattered_read (msg_type 4), data=d32 (msg_ctrl bits 3:2 = 2),
    * SIMD16 (msg_ctrl[0]=1 matches exec_size=16, elided),
    * bti=3, mlen=1, rlen=2. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_HDC0;
   send->send.desc_imm = 0x02210903;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc0 (16|M0)         r10     r20            0x00000000  0x02210903  // wr:1+0, rd:2; byte_scattered_read:d32 simd16 (16) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc0DwordScatteredWriteCommentFormatting)
{
   set_devinfo("skl");

   /* dword_scattered_write (msg_type 11), SIMD8 (msg_ctrl[0]=0 matches
    * exec_size=8, elided), bti=3, mlen=2, rlen=0. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_HDC0;
   send->send.desc_imm = 0x0402C203;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc0 (8|M0)          null    r20            0x00000000  0x0402C203  // wr:2+0, rd:0; dword_scattered_write simd8 (8) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc0UntypedAtomicAddCommentFormatting)
{
   set_devinfo("skl");

   /* HDC0 untyped_atomic (msg_type 6), aop=ADD(7), SIMD16 (matches
    * exec_size=16), bti=3, mlen=2, rlen=0.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_HDC0;
   send->send.desc_imm = 0x04018703;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc0 (16|M0)         null    r20            0x00000000  0x04018703  // wr:2+0, rd:0; untyped_atomic_add simd16 (16) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc0MemoryFenceCommentFormatting)
{
   set_devinfo("skl");

   /* HDC0 memory_fence (msg_type 7), bti=0, mlen=1, rlen=0. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_HDC0;
   send->send.desc_imm = 0x0201C000;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.hdc0 (1|M0)          null    r20            0x00000000  0x0201C000  // wr:1+0, rd:0; memory_fence (1)\n");
}

TEST_F(GenPrintTest, PreLscRtWriteCommentFormatting)
{
   set_devinfo("skl");

   /* rt_write (msg_type 12), subtype=SIMD16 (matches exec_size=16 default,
    * elided), last_rt, bti=3, mlen=4, rlen=0.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_RENDER_CACHE;
   send->send.desc_imm = 0x08031003;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.render (16|M0)       null    r20            0x00000000  0x08031003  // wr:4+0, rd:0; simd16 rt_write last_rt (16) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscRtWriteDualSrcCommentFormatting)
{
   set_devinfo("skl");

   /* rt_write, subtype=simd8_dualsrc_low (2), last_rt, bti=3, mlen=4. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_RENDER_CACHE;
   send->send.desc_imm = 0x08031203;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.render (16|M0)       null    r20            0x00000000  0x08031203  // wr:4+0, rd:0; simd8_dualsrc_low rt_write last_rt (16) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscRtReadCommentFormatting)
{
   set_devinfo("skl");

   /* rt_read (msg_type 13), SIMD16 (matches exec_size=16, elided),
    * per_sample=1, bti=5, mlen=1, rlen=2.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_RENDER_CACHE;
   send->send.desc_imm = 0x02236005;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.render (16|M0)       r10     r20            0x00000000  0x02236005  // wr:1+0, rd:2; simd16 rt_read per_sample (16) bti(5)\n");
}

TEST_F(GenPrintTest, PreLscPiCommentFormatting)
{
   set_devinfo("skl");

   /* pixel_interpolator centroid, linear, msg_data=0x04, mlen=1, rlen=2. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_PIXEL_INTERPOLATOR;
   send->send.desc_imm = 0x02206004;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.pi (16|M0)           r10     r20            0x00000000  0x02206004  // wr:1+0, rd:2; centroid linear data=0x04 (16)\n");
}

TEST_F(GenPrintTest, PreLscGatewayBarrierCommentFormatting)
{
   set_devinfo("skl");

   /* gateway barrier_msg, subfunc=4, mlen=1, rlen=0. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_MESSAGE_GATEWAY;
   send->send.desc_imm = 0x02000004;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.gtwy (1|M0)          null    r20            0x00000000  0x02000004  // wr:1+0, rd:0; barrier_msg (1)\n");
}

TEST_F(GenPrintTest, PreLscBtdSpawnCommentFormatting)
{
   set_devinfo("dg2");

   /* BTD spawn (msg_type 1), SIMD16 (matches exec_size=16, elided),
    * mlen=2, rlen=0. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_BINDLESS_THREAD_DISPATCH;
   send->send.desc_imm = 0x04004100;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.btd (16|M0)          null    r20    null:0  0x00000000  0x04004100  // wr:2+0, rd:0; spawn simd16 (16)\n");
}

TEST_F(GenPrintTest, PreLscRtAccelTraceRayCommentFormatting)
{
   set_devinfo("dg2");

   /* trace_ray: bit 8 set = SIMD16, matches exec_size=16 => simd tag elided.
    * mlen=1, rlen=2.
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_RAY_TRACE_ACCELERATOR;
   send->send.desc_imm = 0x02200100;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.rtaccel (16|M0)      r10     r20    null:0  0x00000000  0x02200100  // wr:1+0, rd:2; trace_ray simd16 (16)\n");
}

TEST_F(GenPrintTest, PreLscSamplerTranslatedSyntaxFormatting)
{
   set_devinfo("skl");

   /* Same sampler descriptor as the comment-mode test, but rendered in
    * translated mode: the mnemonic replaces 'send.smpl ... ex_desc desc',
    * and dst/src0 both carry ':N' length suffixes (rlen/mlen).
    */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_SAMPLER;
   send->send.desc_imm = 0x04442003;
   send->dst.file = GEN_GRF;
   send->dst.nr = 10;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        send.smpl (16|M0)         r10     r20            0x00000000  0x04442003  // wr:2+0, rd:4; sample_l (16) bti(3) using sampler index 0\n");
}

TEST_F(GenPrintTest, PreLscHdc1UntypedAtomicAddTranslatedSyntaxFormatting)
{
   set_devinfo("skl");

   /* untyped_atomic.add, bti=3, SIMD8, mlen=2, rlen=0; translated mode. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_HDC1;
   send->send.desc_imm = 0x04109703;
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_ARF;
   send->src[1].nr = GEN_ARF_NULL;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        send.hdc1 (8|M0)          null    r20            0x00000000  0x04109703  // wr:2+0, rd:1; untyped_atomic_add simd8 (8) bti(3)\n");
}

TEST_F(GenPrintTest, PreLscHdc1SplitSendTranslatedSyntaxFormatting)
{
   /* Need a split-send-capable gen (ver >= 12); tgl is gfx12. */
   set_devinfo("tgl");

   /* Split-send variant of untyped_atomic.add: src1 comes from a real GRF,
    * ex_desc carries ex_mlen = 1. Expect src1 to show ':1'. */
   gen_inst *send = append(GEN_OP_SEND);
   send->exec_size = 8;
   send->send.sfid = GEN_SFID_HDC1;
   send->send.desc_imm = 0x04109703;
   send->send.ex_desc_imm = 1u << 6;  /* ex_mlen = 1 */
   send->dst.file = GEN_ARF;
   send->dst.nr = GEN_ARF_NULL;
   send->src[0].file = GEN_GRF;
   send->src[0].nr = 20;
   send->src[1].file = GEN_GRF;
   send->src[1].nr = 40;

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        send.hdc1 (8|M0)          null    r20    r40     0x00000040  0x04109703  // wr:2+1, rd:1; untyped_atomic_add simd8 (8) bti(3)\n");
}

TEST_F(GenPrintTest, LscUgmRawAndTranslatedFormatting)
{
   set_devinfo("bmg");

   gen_inst *send = append(GEN_OP_SEND,
                           grf(6, 0),
                           grf(5, 0),
                           gen_null());
   send->exec_size = 1;
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 0;
   send->send.desc_imm = 0x2229e500;

   /* Translated-sends mode: only the LSC-syntax form is shown. */
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        load.ugm.d32x32t.a32.ca.cc.bss[a0.0] (1|M0) r6:2 r5:1\n");

   /* Verbose mode: raw send form with the LSC translation as a comment. */
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.ugm (1|M0)           r6      r5     null:0  a0.0        0x2229E500  // wr:1+a0.0, rd:2; load.ugm.d32x32t.a32.ca.cc.bss[a0.0]\n");
}

TEST_F(GenPrintTest, LscSlmSourceSyntaxFormatting)
{
   set_devinfo("mtl");

   gen_lsc_desc desc = {
      .op = LSC_OP_LOAD,
      .addr_type = LSC_ADDR_SURFTYPE_FLAT,
      .addr_size = LSC_ADDR_SIZE_A32,
      .data_size = LSC_DATA_SIZE_D32,
      .vect_size = LSC_VECT_SIZE_V4,
   };

   gen_inst *send = append(GEN_OP_SEND,
                           grf(10, 0),
                           grf(8, 0),
                           gen_null());
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_SLM;
   send->send.ex_desc_imm = 0;
   gen_message_desc msg = { .msg_length = 2, .response_length = 8 };
   send->send.desc_imm =
      gen_message_desc_encode(&devinfo, &msg) |
      gen_lsc_desc_encode(&devinfo, &desc);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        load.slm.d32x4.a32 (16|M0) r10:8  r8:2\n");
}

TEST_F(GenPrintTest, LscStoreOmitsNullDestination)
{
   set_devinfo("mtl");

   gen_lsc_desc desc = {
      .op = LSC_OP_STORE,
      .addr_type = LSC_ADDR_SURFTYPE_FLAT,
      .addr_size = LSC_ADDR_SIZE_A32,
      .data_size = LSC_DATA_SIZE_D32,
      .vect_size = LSC_VECT_SIZE_V4,
   };

   gen_inst *send = append(GEN_OP_SEND,
                           gen_null(),
                           grf(8, 0),
                           grf(10, 0));
   send->exec_size = 16;
   send->send.sfid = GEN_SFID_SLM;
   send->send.ex_desc_imm = 0;
   send->send.src1_len = 8;
   gen_message_desc msg = { .msg_length = 2, .response_length = 0 };
   send->send.desc_imm =
      gen_message_desc_encode(&devinfo, &msg) |
      gen_lsc_desc_encode(&devinfo, &desc);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        store.slm.d32x4.a32 (16|M0)       r8:2   r10:8\n");
}

TEST_F(GenPrintTest, LscTypedTgmSourceSyntaxFormatting)
{
   set_devinfo("mtl");
   gen_lsc_desc desc = {
      .op = LSC_OP_LOAD_CMASK,
      .addr_type = LSC_ADDR_SURFTYPE_BTI,
      .addr_size = LSC_ADDR_SIZE_A32,
      .data_size = LSC_DATA_SIZE_D32,
      .cache_ctrl = LSC_CACHE_LOAD_L1C_L3C,
      .cmask = LSC_CMASK_XY,
   };
   gen_lsc_ex_desc ex_desc = {
      .addr_type = LSC_ADDR_SURFTYPE_BTI,
      .bti = {
         .index = 3,
         .base_offset = 0,
      },
   };

   gen_inst *send = append(GEN_OP_SEND,
                           grf(20, 0),
                           grf(12, 0),
                           gen_null());
   send->send.sfid = GEN_SFID_TGM;
   send->send.ex_desc_imm =
      gen_lsc_ex_desc_encode(&devinfo, desc.op, &ex_desc, NULL);
   gen_message_desc msg = { .msg_length = 4, .response_length = 2 };
   send->send.desc_imm =
      gen_message_desc_encode(&devinfo, &msg) |
      gen_lsc_desc_encode(&devinfo, &desc);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        load_cmask.tgm.d32.xy.a32.ca.ca.bti[3] (8|M0) r20:2 r12:4\n");
}

TEST_F(GenPrintTest, LscAtomicSourceSyntaxFormatting)
{
   set_devinfo("mtl");
   gen_lsc_desc desc = {
      .op = LSC_OP_ATOMIC_ADD,
      .addr_type = LSC_ADDR_SURFTYPE_BSS,
      .addr_size = LSC_ADDR_SIZE_A32,
      .data_size = LSC_DATA_SIZE_D32,
      .cache_ctrl = LSC_CACHE_STORE_L1WT_L3WB,
      .vect_size = LSC_VECT_SIZE_V1,
   };

   gen_inst *send = append(GEN_OP_SEND,
                           grf(40, 0),
                           grf(35, 0),
                           grf(8, 0));
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_is_reg = true;
   send->send.ex_desc_subnr = 0;
   send->send.src1_len = 1;
   gen_message_desc msg = { .msg_length = 1, .response_length = 1 };
   send->send.desc_imm =
      gen_message_desc_encode(&devinfo, &msg) |
      gen_lsc_desc_encode(&devinfo, &desc);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        atomic_add.ugm.d32.a32.wt.wb.bss[a0.0] (8|M0) r40:1 r35:1 r8:1\n");
}

TEST_F(GenPrintTest, LscFenceSourceSyntaxFormatting)
{
   set_devinfo("mtl");
   gen_lsc_desc desc = {
      .op = LSC_OP_FENCE,
      .addr_type = LSC_ADDR_SURFTYPE_FLAT,
      .addr_size = LSC_ADDR_SIZE_A32,
      .fence = {
         .scope = LSC_FENCE_GPU,
         .flush_type = LSC_FLUSH_TYPE_EVICT,
         .route_to_lsc = true,
      },
   };

   gen_inst *send = append(GEN_OP_SEND,
                           grf(2, 0),
                           grf(0, 0),
                           gen_null());
   send->send.sfid = GEN_SFID_UGM;
   send->send.ex_desc_imm = 0;
   gen_message_desc msg = { .msg_length = 1, .response_length = 1 };
   send->send.desc_imm =
      gen_message_desc_encode(&devinfo, &msg) |
      gen_lsc_desc_encode(&devinfo, &desc);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .flags = GEN_PRINT_TRANSLATED_SENDS }),
             "        fence.ugm.gpu.evict.route_to_lsc (8|M0) r2:1 r0:1\n");
}

TEST_F(GenPrintTest, LscDescDecodeEncode)
{
   set_devinfo("mtl");
   gen_lsc_desc expected = {
      .op = LSC_OP_LOAD_CMASK,
      .addr_type = LSC_ADDR_SURFTYPE_BTI,
      .addr_size = LSC_ADDR_SIZE_A32,
      .data_size = LSC_DATA_SIZE_D32,
      .cache_ctrl = LSC_CACHE_LOAD_L1C_L3C,
      .cmask = LSC_CMASK_XY,
   };

   const uint32_t raw_desc = gen_lsc_desc_encode(&devinfo, &expected);

   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.op, LSC_OP_LOAD_CMASK);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_BTI);
   EXPECT_EQ(desc.addr_size, LSC_ADDR_SIZE_A32);
   EXPECT_EQ(desc.data_size, LSC_DATA_SIZE_D32);
   EXPECT_EQ(desc.cache_ctrl, LSC_CACHE_LOAD_L1C_L3C);
   EXPECT_EQ(desc.cmask, LSC_CMASK_XY);

   EXPECT_EQ(gen_lsc_desc_encode(&devinfo, &desc), raw_desc);
}

TEST_F(GenPrintTest, LscBlock2dDescDecodeEncode)
{
   set_devinfo("bmg");
   gen_lsc_desc expected = {
      .op = LSC_OP_LOAD_2D_BLOCK,
      .addr_type = LSC_ADDR_SURFTYPE_FLAT,
      .data_size = LSC_DATA_SIZE_D16,
      .cache_ctrl = XE2_LSC_CACHE_LOAD_L1UC_L3C,
      .transpose = true,
      .vnni = true,
   };

   const uint32_t raw_desc = gen_lsc_desc_encode(&devinfo, &expected);

   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.op, LSC_OP_LOAD_2D_BLOCK);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_FLAT);
   EXPECT_EQ((unsigned)desc.addr_size, 0u);
   EXPECT_EQ(desc.data_size, LSC_DATA_SIZE_D16);
   EXPECT_EQ(desc.cache_ctrl, XE2_LSC_CACHE_LOAD_L1UC_L3C);
   EXPECT_TRUE(desc.vnni);
   EXPECT_TRUE(desc.transpose);

   EXPECT_EQ(gen_lsc_desc_encode(&devinfo, &desc), raw_desc);
}

TEST_F(GenPrintTest, LscFenceDescDecodeEncode)
{
   set_devinfo("mtl");
   gen_lsc_desc expected = {
      .op = LSC_OP_FENCE,
      .addr_type = LSC_ADDR_SURFTYPE_FLAT,
      .addr_size = LSC_ADDR_SIZE_A32,
      .fence = {
         .scope = LSC_FENCE_GPU,
         .flush_type = LSC_FLUSH_TYPE_EVICT,
         .route_to_lsc = true,
      },
   };

   const uint32_t raw_desc = gen_lsc_desc_encode(&devinfo, &expected);

   const gen_lsc_desc desc = gen_lsc_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.op, LSC_OP_FENCE);
   EXPECT_EQ(desc.addr_type, LSC_ADDR_SURFTYPE_FLAT);
   EXPECT_EQ(desc.addr_size, LSC_ADDR_SIZE_A32);
   EXPECT_EQ(desc.fence.scope, LSC_FENCE_GPU);
   EXPECT_EQ(desc.fence.flush_type, LSC_FLUSH_TYPE_EVICT);
   EXPECT_TRUE(desc.fence.route_to_lsc);

   EXPECT_EQ(gen_lsc_desc_encode(&devinfo, &desc), raw_desc);
}

TEST_F(GenPrintTest, UrbDescDecodeEncode)
{
   set_devinfo("tgl");

   /* Representative: simd8_write at global_offset=12 with masked and
    * per_slot bits set. */
   gen_urb_desc expected = {
      .op = GEN_URB_OPCODE_SIMD8_WRITE,
      .global_offset = 12,
      .swizzle = true,
      .per_slot_offset = true,
   };

   const uint32_t raw_desc = gen_urb_desc_encode(&devinfo, &expected);
   const gen_urb_desc desc = gen_urb_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.op, GEN_URB_OPCODE_SIMD8_WRITE);
   EXPECT_EQ(desc.global_offset, 12u);
   EXPECT_TRUE(desc.swizzle);
   EXPECT_TRUE(desc.per_slot_offset);

   EXPECT_EQ(gen_urb_desc_encode(&devinfo, &desc), raw_desc);
}

TEST_F(GenPrintTest, UrbDescRoundTripAllOpsAndFlags)
{
   set_devinfo("tgl");

   static const enum gen_urb_opcode ops[] = {
      GEN_URB_OPCODE_ATOMIC_MOV,
      GEN_URB_OPCODE_ATOMIC_INC,
      GEN_URB_OPCODE_ATOMIC_ADD,
      GEN_URB_OPCODE_SIMD8_WRITE,
      GEN_URB_OPCODE_SIMD8_READ,
      GEN_GFX125_URB_OPCODE_FENCE,
   };

   static const unsigned offsets[] = { 0, 1, 12, 0x7FF };

   for (enum gen_urb_opcode op : ops) {
      for (unsigned off : offsets) {
         for (unsigned swiz = 0; swiz < 2; swiz++) {
            for (unsigned ps = 0; ps < 2; ps++) {
               gen_urb_desc in = {};
               in.op = op;
               in.global_offset = off;
               in.swizzle = swiz;
               in.per_slot_offset = ps;

               const uint32_t raw = gen_urb_desc_encode(&devinfo, &in);
               const gen_urb_desc out = gen_urb_desc_decode(&devinfo, raw);

               EXPECT_EQ(out.op, in.op);
               EXPECT_EQ(out.global_offset, in.global_offset);
               EXPECT_EQ(out.swizzle, in.swizzle);
               EXPECT_EQ(out.per_slot_offset, in.per_slot_offset);
            }
         }
      }
   }
}

TEST_F(GenPrintTest, SamplerDescDecodeEncode)
{
   set_devinfo("skl");

   gen_sampler_desc expected = {
      .msg_type = GEN_SAMPLER_MESSAGE_SAMPLE_LOD,
      .simd_mode = GEN_SAMPLER_SIMD_MODE_SIMD16,
      .bti = 3,
      .sampler_index = 5,
      .return_hp = true,
   };

   const uint32_t raw_desc = gen_sampler_desc_encode(&devinfo, &expected);
   const gen_sampler_desc desc = gen_sampler_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.msg_type, (unsigned)GEN_SAMPLER_MESSAGE_SAMPLE_LOD);
   EXPECT_EQ(desc.simd_mode, (unsigned)GEN_SAMPLER_SIMD_MODE_SIMD16);
   EXPECT_EQ(desc.bti, 3u);
   EXPECT_EQ(desc.sampler_index, 5u);
   EXPECT_TRUE(desc.return_hp);

   EXPECT_EQ(gen_sampler_desc_encode(&devinfo, &desc), raw_desc);
}

TEST_F(GenPrintTest, SamplerDescXe2MsgTypeBit5)
{
   set_devinfo("bmg");

   /* Xe2 widens msg_type by one bit, sitting at desc[31]. Verify the bit
    * round-trips through the split encoding. */
   gen_sampler_desc expected = {
      .msg_type = GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO,
      .simd_mode = GEN_XE2_SAMPLER_SIMD_MODE_SIMD16,
      .bti = 1,
      .sampler_index = 0,
   };
   EXPECT_NE(expected.msg_type >> 5, 0u);

   const uint32_t raw_desc = gen_sampler_desc_encode(&devinfo, &expected);
   const gen_sampler_desc desc = gen_sampler_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.msg_type, (unsigned)GEN_XE2_SAMPLER_MESSAGE_SAMPLE_PO);
   EXPECT_EQ(desc.simd_mode, (unsigned)GEN_XE2_SAMPLER_SIMD_MODE_SIMD16);
   EXPECT_EQ(desc.bti, 1u);
   EXPECT_EQ(desc.sampler_index, 0u);
   EXPECT_FALSE(desc.return_hp);

   EXPECT_EQ(gen_sampler_desc_encode(&devinfo, &desc), raw_desc);
}

TEST_F(GenPrintTest, HdcDescDecodeEncode)
{
   set_devinfo("skl");

   gen_hdc_desc expected = {
      .bti = 7,
      .msg_ctrl = 0x1a,
      .msg_type = GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP,
   };

   const uint32_t raw_desc = gen_hdc_desc_encode(&devinfo, &expected);
   const gen_hdc_desc desc = gen_hdc_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.bti, 7u);
   EXPECT_EQ(desc.msg_ctrl, 0x1au);
   EXPECT_EQ(desc.msg_type, (unsigned)GEN_DATAPORT_DC_PORT1_UNTYPED_ATOMIC_OP);

   EXPECT_EQ(gen_hdc_desc_encode(&devinfo, &desc), raw_desc);

   /* Verify the encode leaves bits [31:19] alone (those belong to generic
    * SEND mlen/rlen/header_present). */
   EXPECT_EQ(raw_desc & ~((1u << 19) - 1), 0u);
}

TEST_F(GenPrintTest, RenderDescDecodeEncode)
{
   set_devinfo("tgl");

   gen_render_desc expected = {
      .bti = 0x12,
      .msg_ctrl = 0x2a,
      .msg_type = GEN_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE,
      .coarse_write = true,
   };

   const uint32_t raw_desc = gen_render_desc_encode(&devinfo, &expected);
   const gen_render_desc desc = gen_render_desc_decode(&devinfo, raw_desc);

   EXPECT_EQ(desc.bti, 0x12u);
   EXPECT_EQ(desc.msg_ctrl, 0x2au);
   EXPECT_EQ(desc.msg_type,
             (unsigned)GEN_DATAPORT_WRITE_MESSAGE_RENDER_TARGET_WRITE);
   EXPECT_TRUE(desc.coarse_write);

   EXPECT_EQ(gen_render_desc_encode(&devinfo, &desc), raw_desc);

   /* coarse_write at desc[18] must survive round trip independent of
    * msg_ctrl (which only covers desc[13:8]) and msg_type (desc[17:14]). */
   EXPECT_TRUE((raw_desc >> 18) & 1u);
}

TEST_F(GenPrintTest, AnnotationPrintedBeforeInstruction)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,
          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
          grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));

   const char *annotations[] = {
      "annotate me",
   };

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .annotations = annotations }),
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST_F(GenPrintTest, AnnotationPreservesEmbeddedNewlines)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,
          grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
          grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));

   const char *annotations[] = {
      "line 1\nline 2",
   };

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .annotations = annotations }),
             "\n// line 1\n"
             "line 2\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST_F(GenPrintTest, AnnotationDeduplicatesConsecutiveEqualText)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,

             grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),

             grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));
   append(GEN_OP_MOV,
             grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
             grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));

   std::string first = "annotate me";
   std::string second = "annotate me";
   const char *annotations[] = {
      first.c_str(),
      second.c_str(),
   };

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .annotations = annotations }),
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST_F(GenPrintTest, AnnotationPrintedAgainAfterGap)
{
   set_devinfo("tgl");

   append(GEN_OP_MOV,

             grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),

             grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));
   append(GEN_OP_MOV,
             grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
             grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));
   append(GEN_OP_MOV,
             grf(1, 0, { .hstride = 1 }, GEN_TYPE_F),
             grf(2, 0, { 8, 8, 1 }, GEN_TYPE_F));

   const char *annotations[] = {
      "annotate me",
      "",
      "annotate me",
   };

   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE,
                           { .annotations = annotations }),
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n"
             "\n// annotate me\n"
             "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f\n");
}

TEST_F(GenPrintTest, LscXe2UrbLoadCommentFormatting)
{
   set_devinfo("bmg");

   gen_lsc_desc desc = {
      .op = LSC_OP_LOAD,
      .addr_type = LSC_ADDR_SURFTYPE_FLAT,
      .addr_size = LSC_ADDR_SIZE_A32,
      .data_size = LSC_DATA_SIZE_D32,
      .vect_size = LSC_VECT_SIZE_V1,
   };

   gen_inst *send = append(GEN_OP_SEND,
                           grf(10, 0),
                           grf(20, 0),
                           gen_null());
   send->send.sfid = GEN_SFID_URB;
   send->send.ex_desc_imm = 0;
   gen_message_desc msg = { .msg_length = 1, .response_length = 1 };
   send->send.desc_imm = gen_message_desc_encode(&devinfo, &msg) |
                         gen_lsc_desc_encode(&devinfo, &desc);
   EXPECT_EQ(print_program(GEN_PRINT_VERBOSE),
             "        send.urb (16|M0)          r10     r20    null:0  0x00000000  0x02100500  // wr:1+0, rd:1; load.urb.d32.a32\n");
}
