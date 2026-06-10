/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "dev/intel_device_info.h"
#include "gen.h"
#include "util/ralloc.h"

struct GenParsePrintTest : public ::testing::Test {
   intel_device_info devinfo = {};
   void *mem_ctx = ralloc_context(NULL);

   /* Latest parse() result. */
   gen_inst *parsed_insts = nullptr;
   int parsed_num_insts = 0;
   gen_error *parsed_errors = nullptr;
   int parsed_num_errors = 0;

   ~GenParsePrintTest() {
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
   parse(const std::string &text)
   {
      gen_parse_params params = {};
      params.devinfo = &devinfo;
      params.text = text.c_str();
      params.text_size = text.size();
      params.mem_ctx = mem_ctx;

      const bool ok = gen_parse(&params);
      parsed_insts = params.insts;
      parsed_num_insts = params.num_insts;
      parsed_errors = params.errors;
      parsed_num_errors = params.num_errors;
      return ok;
   }

   std::string
   print_parsed(gen_print_flags flags)
   {
      gen_print_params params = {};
      params.devinfo = &devinfo;
      params.flags = (gen_print_flags)(flags | GEN_PRINT_IGNORE_ENV);
      params.insts = parsed_insts;
      params.num_insts = parsed_num_insts;

      char *str = NULL;
      size_t size = 0;
      FILE *fp = open_memstream(&str, &size);
      params.fp = fp;
      gen_print(&params);
      fclose(fp);

      std::string out(str ? str : "");
      free(str);
      return out;
   }

   std::string
   first_error() const
   {
      if (!parsed_num_errors)
         return "";
      return std::to_string(parsed_errors[0].index) + ": " +
             parsed_errors[0].msg;
   }
};

TEST_F(GenParsePrintTest, RoundTripsDefaultFlagStrings)
{
   struct test_case {
      const char *platform;
      const char *text;
   };

   const test_case cases[] = {
      {
         "tgl",
         "        add (8)                   r5            r6                0x0000002a\n",
      },
      {
         "tgl",
         "        add3 (16)                 r3:d          r19.1<0>:d        r21:d             r2<1>:d\n",
      },
      {
         "tgl",
         "        mov (8)                   r1:f          r2<0>:f\n",
      },
      {
         "tgl",
         "        mov (8|M24)               r1            r2\n",
      },
      {
         "tgl",
         "        goto (16)                             jip:0x20            uip:0x20\n",
      },
   };

   for (const test_case &tc : cases) {
      SCOPED_TRACE(::testing::Message()
                   << "devinfo=" << tc.platform
                   << "\ntext:\n" << tc.text);

      set_devinfo(tc.platform);

      if (!parse(tc.text)) {
         ADD_FAILURE() << "parse error: " << first_error();
         continue;
      }

      const std::string reprinted = print_parsed(GEN_PRINT_NONE);
      EXPECT_EQ(reprinted, tc.text) << "reprinted:\n" << reprinted;
   }
}

TEST_F(GenParsePrintTest, RoundTripsVerboseFlagStrings)
{
   struct test_case {
      const char *platform;
      const char *text;
   };

   const test_case cases[] = {
      {
         "tgl",
         "(W&~f0.0) add (8|M0)              r1.0<1>:f     r2.0<8;8,1>:f     0x3f800000:f\n",
      },
      {
         "tgl",
         "        cmp (16|M0)    (ge)f3.1   r5.0<1>:ud    r6.0<8;8,1>:ud    r7.0<8;8,1>:ud\n",
      },
      {
         "tgl",
         "        mad (8|M24)               r10.4<1>:f    r11.0<0;0>:f      r12.4<1;0>:f      r13.8<1>:f\n",
      },
      {
         "tgl",
         "        mad (32|M0)               r70.0<1>:f    r2.8<0;0>:f       r24.0<1;0>:f      r2.10<0>:f\n",
      },
      {
         "tgl",
         "        goto.b (16|M0)                        jip:L0              uip:L0\n"
         "L0:\n",
      },
      {
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {AccWrEn,Breakpoint,NoDDChk,NoDDClr}\n",
      },
      {
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<4;4,1>:f                     {Align16}\n",
      },
      {
         "skl",
         "        mad (8|M0)                r1.0<1>:f     r2.0<0;0>.r:f     r3.0<0;0>.r:f     r4.0<0>.r:f {Align16}\n",
      },
      {
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Atomic}\n",
      },
      {
         "tgl",
         "        mov (8|M0)                r1.0<1>:f     r2.0<8;8,1>:f                     {Switch}\n",
      },
      {
         "tgl",
         "        send.ugm (1|M0)           r40     r35    null    a0.2        0x2229E500   {@2,$5}\n",
      },
      {
         "mtl",
         "        send.ugm (8|M0)           r40     r35    r8:3    a0.2        0x24100100   {ExBSO}  // wr:2+3, rd:1; load.ugm.d8.a32.bss[a0.2]\n",
      },
      {
         "bmg",
         "        send.ugm (8|M0)           r40     r35    null:0  0x00080000:a0.2 0x22100100  // wr:1+a0.2, rd:1; load.ugm.d8.a32.bss[a0.2][A+0x10]\n",
      },
      {
         "tgl",
         "        send.ugm (8|M0)           r40     r35    null    a0.2        0x02100000   {Serialize}\n",
      },
      {
         "skl",
         "(W)     send.ts (8|M0)            null    r127           0x00000000  0x02000010   {EOT}\n",
      },
      {
         "bmg",
         "        send.ugm (1|M0)           r6      r5     null:0  a0.0        0x2229E500  // wr:1+a0.0, rd:2; load.ugm.d32x32t.a32.ca.cc.bss[a0.0]\n",
      },
      {
         "mtl",
         "        dpas.8x4 (8|M0)           r1.0<1>:d     r2.0<8;8,1>:d     r3.0<8;8,1>:hf    r4.0<8;8,1>:hf\n",
      },
      {
         "tgl",
         "        bfn.(a ^ ~b & c) (8|M0)   r1.0<1>:ud    r2.0<1;0>:ud      r3.0<1;0>:ud      r4.0<1>:ud\n",
      },
      {
         "tgl",
         "        goto (16|M0)                          jip:L0              uip:L0\n"
         "        nop\n"
         "\n"
         "L0:\n"
         "        nop\n",
      },
   };

   for (const test_case &tc : cases) {
      SCOPED_TRACE(::testing::Message()
                   << "devinfo=" << tc.platform
                   << "\ntext:\n" << tc.text);

      set_devinfo(tc.platform);

      if (!parse(tc.text)) {
         ADD_FAILURE() << "parse error: " << first_error();
         continue;
      }

      const std::string reprinted = print_parsed(GEN_PRINT_VERBOSE);
      EXPECT_EQ(reprinted, tc.text) << "reprinted:\n" << reprinted;
   }
}

TEST_F(GenParsePrintTest, RoundTripsTranslatedSendsFlagStrings)
{
   struct test_case {
      const char *platform;
      const char *text;
   };

   const test_case cases[] = {
      {
         "bmg",
         "        load.ugm.d32x32t.a32.ca.cc.bss[a0.0] (1) r6:2 r5:1\n",
      },
      {
         "mtl",
         "        load.slm.d32x4.a32 (16)   r10:8   r8:2\n",
      },
      {
         "bmg",
         "        load.urb.d32.a32 (16)     r10:1   r20:1\n",
      },
      {
         "mtl",
         "        load_cmask.tgm.d32.xy.a32.ca.ca.bti[3] (8) r20:2 r12:4\n",
      },
      {
         "mtl",
         "        atomic_add.ugm.d32.a32.wt.wb.bss[a0.0] (8) r40:1 r35:1 r8:1\n",
      },
      {
         "mtl",
         "        fence.ugm.gpu.evict.route_to_lsc (8) r2:1 r0:1\n",
      },
      {
         "bmg",
         "        fence.urb.gpu.evict (8)   r2:1    r0:1\n",
      },
   };

   for (const test_case &tc : cases) {
      SCOPED_TRACE(::testing::Message()
                   << "devinfo=" << tc.platform
                   << "\ntext:\n" << tc.text);

      set_devinfo(tc.platform);

      if (!parse(tc.text)) {
         ADD_FAILURE() << "parse error: " << first_error();
         continue;
      }

      const std::string reprinted =
         print_parsed(GEN_PRINT_TRANSLATED_SENDS);
      EXPECT_EQ(reprinted, tc.text) << "reprinted:\n" << reprinted;
   }
}
