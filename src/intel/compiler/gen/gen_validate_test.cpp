/*
 * Copyright © 2016-2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen_private.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "util/ralloc.h"

static constexpr int MIN_GFX_VERX10 = 90;
static constexpr int MAX_GFX_VERX10 = 350;

/* Many of the tests can be represented as an assembly instruction that can be
 * parsed and then validate.  These tests are handled in a single table.
 *
 * For other cases where the parser already rejects or can't represent them
 * (e.g. some subnr values, or .sat in BFN), see the later TEST() entries that
 * produce them in code directly.
 */

static const char *const VALID = NULL;
static const char *const SAME_ERROR = "same error as before";

static const struct {
   const char *input;
   const char *expected_pattern;
   struct {
      unsigned eq;
      unsigned ge;
      unsigned gt;
      unsigned le;
      unsigned lt;

      bool is_9lp;
      bool is_not_9lp;

      bool has_bfloat16;
      bool has_64bit_float;
      bool has_64bit_int;

      bool has_lsc;
      bool no_lsc;
   };
} validation_tests[] = {
   { "nop", VALID },

   /* Opcode restrictions by version. */

   { "dp2 (8) r0:f r1:f r2:f",                 VALID, { .eq = 90 } },
   { "dp3 (8) r0:f r1:f r2:f",                 VALID, { .eq = 90 } },
   { "dp4 (8) r0:f r1:f r2:f",                 VALID, { .eq = 90 } },
   { "dph (8) r0:f r1:f r2:f",                 VALID, { .eq = 90 } },
   { "line (8) r0:f r1<0>:f r2:f",             VALID, { .eq = 90 } },
   { "lrp (8) r0.:f r1:f r2:f r3:f {Align16}", VALID, { .eq = 90 } },
   { "pln (8) r0:f r1<0>:f r2:f",              VALID, { .eq = 90 } },

   { "dp2 (8) r0:f r1:f r2:f",      "DP2 is Gfx9 only.",  { .gt = 90 } },
   { "dp3 (8) r0:f r1:f r2:f",      "DP3 is Gfx9 only.",  { .gt = 90 } },
   { "dp4 (8) r0:f r1:f r2:f",      "DP4 is Gfx9 only.",  { .gt = 90 } },
   { "dph (8) r0:f r1:f r2:f",      "DPH is Gfx9 only.",  { .gt = 90 } },
   { "line (8) r0:f r1<0>:f r2:f",  "LINE is Gfx9 only.", { .gt = 90 } },
   { "lrp (8) r0:f r1:f r2:f r3:f", "LRP is Gfx9 only.",  { .gt = 90 } },
   { "pln (8) r0:f r1<0>:f r2:f",   "PLN is Gfx9 only.",  { .gt = 90 } },

   { "sends.null (16) null r1 null 0x0 0x0", VALID, { .le = 110 } },
   { "sends.null (16) null r1 null 0x0 0x0",
      "SENDS is Gfx9 and Gfx11 only.", { .gt = 110 }
   },

   { "sendsc.null (16) null r1 null 0x0 0x0", VALID, { .eq = 90 } },
   { "sendsc.null (16) null r1 null 0x0 0x0", VALID, { .eq = 110 } },
   { "sendsc.null (16) null r1 null 0x0 0x0",
     "SENDSC is Gfx9 and Gfx11 only.", { .gt = 110 }
   },

   { "wait (8) r0", VALID,                          { .le = 110 } },
   { "wait (8) r0", "WAIT is Gfx9 and Gfx11 only.", { .gt = 110 } },

   { "rol (8) r0 r1 r2", "ROL is Gfx11+ only.", { .lt = 110 } },
   { "rol (8) r0 r1 r2", VALID,                 { .ge = 110 } },

   { "ror (8) r0 r1 r2", "ROR is Gfx11+ only.", { .lt = 110 } },
   { "ror (8) r0 r1 r2", VALID,                 { .ge = 110 } },

   { "add3 (8) r0:d r1<0;0>:d r2<0;0>:d r3<0>:d", "ADD3 is Gfx12+ only.", { .lt = 120 } },
   { "add3 (8) r0:d r1<0;0>:d r2<0;0>:d r3<0>:d", VALID,                  { .ge = 120 } },

   { "bfn.0x00 (8) r0 r1 r2 r3", "BFN is Gfx12+ only.", { .lt = 120 } },
   { "bfn.0x00 (8) r0 r1 r2 r3", VALID,                 { .ge = 120 } },

   { "dp4a (8) r0:d r1<0;0>:d r2<0;0>:d r3<0>:d", "DP4A is Gfx12+ only.", { .lt = 120 } },
   { "dp4a (8) r0:d r1<0;0>:d r2<0;0>:d r3<0>:d", VALID,                  { .ge = 120 } },

   { "sync.nop (1) r1<0>", "SYNC is Gfx12+ only.", { .lt = 120 } },
   { "sync.nop (1) r1<0>", VALID,                  { .ge = 120 } },

   { "dpas.8x8 (8) r0:f r1:f r2:hf r3:hf",
     "DPAS is Gfx12.5+ only.", { .lt = 125 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2:hf r3:hf",  VALID, { .eq = 125 } },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3:hf", VALID, { .ge = 200 } },

   { "srnd (8) r0<2>:hf r1:f r2:f", "SRND is Gfx20+ only.", { .lt = 200 } },
   { "srnd (8) r0<2>:hf r1:f r2:f", VALID,                  { .ge = 200 } },


   /* Sources vs. null register. */
   { "mov (8) r0:d null",           "src0 is null" },
   { "add (8) r0:d r1:d null",      "src1 is null" },
   { "math.pow (8) r0:f null r2:f", "src0 is null" },
   { "math.pow (8) r0:f r1:f null", "src1 is null" },
   { "math.inv (8) r0:f r1:f null", VALID },


   { "math.sqt (8) r0:hf r1<0>:hf null", VALID, { .le = 125 } },
   { "math.sqt (8) r0:hf r1<0>:hf null",
     "Scalar broadcast on HF MATH inputs is not supported on this platform.", { .gt = 125, .lt = 350 }
   },
   { "math.sqt (8) r0:hf r1<0>:hf null", VALID, { .ge = 350 } },
   { "math.invm (8) r0:hf r1:hf r2<0>:hf", VALID, { .le = 125 } },
   { "math.invm (8) r0:hf r1:hf r2<0>:hf",
     "Scalar broadcast on HF MATH inputs is not supported on this platform.", { .gt = 125, .lt = 350 }
   },
   { "math.invm (8) r0:hf r1:hf r2<0>:hf", VALID, { .ge = 350 } },


   /* Branch restrictions. */
   { "jmpi (8) 0x0",       VALID },
   { "jmpi (8) bad<0>:d",  "JMPI and BRD source must be a register or immediate." },

   { "brd (8) 0x0",       VALID },
   { "brd (8) bad<0>:d",  "JMPI and BRD source must be a register or immediate." },

   { "brc (8) jip:0x0 uip:0x0", VALID },
   { "brc (8) r1<2;2,1>:d",     VALID },
   { "brc (8) r1<2;2,1>:w",     "BRC register form requires a paired DWord integer source." },
   { "brc (8) r1<2>:d",         "BRC register form requires src0 region <2;2,1>." },
   { "brc (8) 0x0",             "BRC immediate form requires src0 and src1 to be immediates." },


   { "add (8) r0:d r1:d r2:d", VALID },
   { "add r0:d r1:d r2:d",     "Execution size must be 1, 2, 4, 8, 16, or 32." },
   { "add (3) r0:d r1:d r2:d", SAME_ERROR },


   /* GFX register numbers. */
   { "mov (4) r127 r127",        VALID,                                                      { .lt = 200 } },
   { "mov (4) r128 r1",          "Destination GRF register number 128 exceeds maximum 127.", { .lt = 200 } },
   { "mov (4) r0 r128",          "Source 0 GRF register number 128 exceeds maximum 127.",    { .lt = 200 } },
   { "mov (4) r[a0.0 + 128] r1", VALID,                                                      { .lt = 200 } },
   { "mov (4) r0 r[a0.0 + 128]", VALID,                                                      { .lt = 200 } },

   { "mov (4) r255 r255",        VALID,                                                      { .ge = 200 } },
   { "mov (4) r256 r1",          "Destination GRF register number 256 exceeds maximum 255.", { .ge = 200 } },
   { "mov (4) r0 r256",          "Source 0 GRF register number 256 exceeds maximum 255.",    { .ge = 200 } },
   { "mov (4) r[a0.0 + 256] r1", VALID,                                                      { .ge = 200 } },
   { "mov (4) r0 r[a0.0 + 256]", VALID,                                                      { .ge = 200 } },


   /* Conditional modifier not encodable. */
   { "mov (1) r0:uq 0x12111015140a0001:uq",          VALID, { .eq = 90 } },
   { "mov (1) (eq)f0.0 r0:uq 0x12111015140a0001:uq", VALID, { .eq = 90 } },
   { "mov (1) r0:df 0x3ff0000000000000:df",          VALID, { .ge = 120, .has_64bit_float = true } },
   { "mov (1) r0:uq 0x12111015140a0001:uq",          VALID, { .ge = 200 } },

   { "mov (1) (eq)f0.0 r0:df 0x3ff0000000000000:df",
     "Conditional modifier is not encodable for Xe basic 1-source instructions with a 64-bit immediate src0.",
     { .ge = 120, .has_64bit_float = true }
   },
   { "mov (1) (eq)f0.0 r0:uq 0x12111015140a0001:uq", SAME_ERROR, { .ge = 200 } },


   /* Channel offset restrictions. */
   { "mov (4|M4) r0:d r1:d", VALID,                                               { .lt = 200 } },
   { "mov (4|M4) r0:d r1:d", "Channel offset must be a multiple of 8 for Gfx20+", { .ge = 200 } },
   { "mov (4|M8) r0:d r1:d", VALID, },

   { "mov (8|M4) r0:d r1:d", VALID, { .lt = 120 } },
   { "mov (8|M4) r0:d r1:d",
     "The execution size must be a factor of the chosen offset", { .ge = 120, .lt = 200 }
   },
   { "mov (8|M4) r0:d r1:d",
     "Channel offset must be a multiple of 8 for Gfx20+", { .ge = 200 }
   },


   { "mad (8) r0:d r1:d r2:d r3:d",
     "Gfx9 3-source instructions must use Align16 mode.", { .eq = 90 }
   },
   { "mad (8) r0:d r1:d r2:d r3:d", VALID, { .gt = 90 } },

   { "mad (8) r0.:d r1:d r2:d r3:d {Align16}", VALID,                                   { .eq = 90 } },
   { "mad (8) r0.:d r1:d r2:d r3:d {Align16}", "Align16 mode doesn't exist on Gfx11+.", { .ge = 110 } },


   { "mad (8) r0.:f r1:f 0x0:f r3:f {Align16}",
     "3-source instructions cannot use an immediate src1.", { .eq = 90 }
   },
   { "mad (8) r0:f r1:f 0x0:f r3:f",
     SAME_ERROR, { .ge = 110 }
   },


   { "mad (8) r0.:f 0x0:f r2:f r3:f {Align16}",
     "Gfx9 3-source instructions cannot use immediate sources.", { .eq = 90 }
   },
   { "mad (8) r0.:f r1:f r2:f 0x0:f {Align16}", SAME_ERROR, { .eq = 90 } },


   { "bfe (8) r0:d 0:d r2:d r3:d",
     "BFE and CSEL cannot use immediate sources on Gfx11.", { .eq = 110 }
   },
   { "bfe (8) r0:d r1:d r2:d 0:d",                    SAME_ERROR, { .eq = 110 } },
   { "csel (8) (eq)f0.0 r0:f 0x0:f r2:f r3:f", SAME_ERROR, { .eq = 110 } },
   { "csel (8) (eq)f0.0 r0:f r1:f r2:f 0x0:f", SAME_ERROR, { .eq = 110 } },


   { "mad (8) r0:f 0x0:f r2:f r3:f", VALID, { .eq = 110 } },
   { "mad (8) r0:f 0x0:f r2:f 0x0:f",
     "MAD and DP4A can have at most one immediate source on Gfx11.", { .eq = 110 }
   },


   { "mad (8) r0.:df r1:df r2:df r3:df {Align16}",
     VALID, { .eq = 90, .has_64bit_float = true }
   },
   { "mad (8) r0.:df r1.r:df r2:df r3:df {Align16}",
     "RepCtrl must be 0 for 64-bit src0.", { .eq = 90, .has_64bit_float = true }
   },
   { "mad (8) r0.:df r1:df r2.r:df r3:df {Align16}",
     "RepCtrl must be 0 for 64-bit src1.", { .eq = 90, .has_64bit_float = true }
   },
   { "mad (8) r0.:df r1:df r2:df r3.r:df {Align16}",
     "RepCtrl must be 0 for 64-bit src2.", { .eq = 90, .has_64bit_float = true }
   },


   { "mad (8) r[a0.0].:f r1:f r2:f r3:f {Align16}",
     "3-source destination must use direct addressing.", { .eq = 90 }
   },
   { "mad (8) r0.:f r[a0.0 + 1]:f r2:f r3:f {Align16}",
     "3-source sources must use direct addressing.", { .eq = 90 }
   },
   { "mad (8) r0.:f r1:f r[a0.0 + 2]:f r3:f {Align16}", SAME_ERROR, { .eq = 90 } },
   { "mad (8) r0.:f r1:f r2:f r[a0.0 + 3]:f {Align16}", SAME_ERROR, { .eq = 90 } },

   { "mad (8) r[a0.0]:f r1:f r2:f r3:f",
     "3-source destination must use direct addressing.", { .ge = 110 }
   },
   { "mad (8) r0:f r[a0.0 + 1]:f r2:f r3:f",
     "3-source sources must use direct addressing.", { .ge = 110 }
   },
   { "mad (8) r0:f r1:f r[a0.0 + 2]:f r3:f", SAME_ERROR, { .ge = 110 } },
   { "mad (8) r0:f r1:f r2:f r[a0.0 + 3]:f", SAME_ERROR, { .ge = 110 } },


   { "mad (8) acc0:f r1:f r2:f r3:f {Align16}",
     "Align16 3-source instructions require a GRF destination.", { .eq = 90 }
   },
   { "mad (8) r0.:f acc0:f r2:f r3:f {Align16}",
     "Align16 3-source instructions require GRF sources.", { .eq = 90 }
   },
   { "mad (8) r0.:f r1:f acc0:f r3:f {Align16}", SAME_ERROR, { .eq = 90 } },
   { "mad (8) r0.:f r1:f r2:f acc0:f {Align16}", SAME_ERROR, { .eq = 90 } },


   { "mad (8) r0.:f r1:f r2:f r3:f {Align16}", VALID, { .eq = 90 } },
   { "mad (8) r0.:w r1:w r2:w r3:w {Align16}",
     "Align16 3-source destination type must be F, DF, D, UD, or HF.", { .eq = 90 }
   },


   { "mad (8) a0:f r1:f r2:f r3:f",
     "Align1 3-source destination must be a GRF, accumulator, or null register.", { .ge = 110 }
   },
   { "mad (8) r0<2>:f r1:f r2:f r3:f", VALID, { .ge = 110, .lt = 350 } },
   { "mad (8) r0<4>:f r1:f r2:f r3:f",
     "Align1 3-source destination horizontal stride must be 1 or 2.", { .ge = 110 }
   },


   { "mad (8) r0:f r1<8;3>:f r2:f r3:f",
     "Align1 3-source source horizontal stride must be 0, 1, 2, or 4.", { .ge = 110 }
   },
   { "mad (8) r0:f r1:f r2:f r3<3>:f", SAME_ERROR, { .ge = 110 } },
   { "mad (8) r0:f r1<3;1>:f r2:f r3:f",
     "Align1 3-source source vertical stride is not encodable.", { .ge = 110 }
   },


   { "mad (8) r0:f r1:f r2:f r3:f", VALID, { .ge = 110 } },
   { "mad (8) r0:f r1:d r2:f r3:f",
     "Align1 3-source source type must match the destination execution type.", { .ge = 110 }
   },
   { "mad (8) r0:d r1:d r2:d r3:f", SAME_ERROR, { .ge = 110 } },


   { "mad (8) r0.:f bad:f r2:f r3:f {Align16}",
     "Align16 3-source instructions require GRF sources.", { .eq = 90 }
   },
   { "mad (8) r0:f bad:f r2:f r3:f",
     "3-source register sources must use GRF or ARF files.", { .ge = 110 }
   },


   { "mov (4) r0.:d r1<4;4,1>.x:d {Align16}", VALID, { .eq = 90 } },
   { "mov (4) r0.:d r1<4;4,1>.x:d {Align16}",
     "Align16 mode doesn't exist on Gfx11+.", { .ge = 110 }
   },


   { "add (8) r0:w r1:d r2:d",
     "Destination stride must match the ratio of execution type size to destination type size.",
   },
   { "add (8) r0<2>:w r1:d r2:d", VALID },


   { "add (8) r0.1<2>:w r1:d r2:d",
     "Destination subregister must align to the execution type size.",
   },
   { "add (4) r0.4<2>:w r1:d r2:d", VALID },


   { "add (8) r0:d r1<8;16,1>:d r2:d", "ExecSize must be greater than or equal to Width." },
   { "add (8) r0:d r1:d r2<8;16,1>:d", SAME_ERROR },


   { "add (8) r0:d r1<4;8,1>:d r2:d",
     "If ExecSize equals Width and HorzStride is not 0, VertStride must equal Width * HorzStride.",
   },
   { "add (8) r0:d r1:d r2<4;8,1>:d", SAME_ERROR },


   { "add (8) r0:d r1<0;1,1>:d r2:d", "If Width is 1, HorzStride must be 0." },
   { "add (8) r0:d r1:d r2<0;1,1>:d", SAME_ERROR },


   { "add (1) r0:d r1<1>:d r2<0;1,0>:d",
     "If ExecSize is 1 and Width is 1, both VertStride and HorzStride must be 0.",
   },
   { "add (1) r0:d r1<0;1,0>:d r2<1>:d", SAME_ERROR },


   { "add (8) r0:d r1<0;2,0>:d r2:d",
     "If VertStride and HorzStride are 0, Width must be 1.",
   },
   { "add (8) r0:d r1:d r2<0;2,0>:d", SAME_ERROR },


   { "add (8) r0<0>:d r1:d r2:d",
     "Destination horizontal stride must not be 0.",
   },
   { "add (8) r0<0>.:d r1.x:d r2.x:d {Align16}",
     "In Align16 mode, destination horizontal stride must be 1.", { .eq = 90 }
   },


   { "add (8) r0:d r1.1<8;8,1>:d r2:d",
     "VertStride must be used to cross GRF register boundaries.", { .lt = 200 }
   },
   { "add (8) r0:d r1:d r2.1<8;8,1>:d", SAME_ERROR, { .lt = 200 } },
   { "add (8) r0:d r1.2<4;4,2>:d r2:d", SAME_ERROR, { .lt = 200 } },
   { "add (8) r0:d r1:d r2.2<4;4,2>:d", SAME_ERROR, { .lt = 200 } },

   { "add (8) r0:d r1.9<8;8,1>:d r2:d",
      SAME_ERROR, { .ge = 200 }
   },
   { "add (8) r0:d r1:d r2.9<8;8,1>:d", SAME_ERROR, { .ge = 200 } },
   { "add (8) r0:d r1.10<4;4,2>:d r2:d", SAME_ERROR, { .ge = 200 } },
   { "add (8) r0:d r1:d r2.10<4;4,2>:d", SAME_ERROR, { .ge = 200 } },


   { "add (8) r0<2>.:d r1<4;4,1>.x:d r2<4;4,1>.x:d {Align16}",
     "In Align16 mode, destination horizontal stride must be 1.", { .eq = 90 }
   },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<4;4,1>.x:d {Align16}", VALID, { .eq = 90 } },


   { "add (8) r0.:d r1<0;4,1>.x:d r2<4;4,1>.x:d {Align16}", VALID, { .eq = 90 } },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<0;4,1>.x:d {Align16}", VALID, { .eq = 90 } },
   { "add (8) r0.:d r1<1;4,1>.x:d r2<4;4,1>.x:d {Align16}",
     "In Align16 mode, source vertical stride must be 0, 2, or 4.", { .eq = 90 }
   },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<1;4,1>.x:d {Align16}", SAME_ERROR, { .eq = 90 } },
   { "add (8) r0.:d r1<2;4,1>.x:d r2<4;4,1>.x:d {Align16}", VALID, { .eq = 90 } },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<2;4,1>.x:d {Align16}", VALID, { .eq = 90 } },
   { "add (8) r0.:d r1<8;4,1>.x:d r2<4;4,1>.x:d {Align16}",
     "In Align16 mode, source vertical stride must be 0, 2, or 4.", { .eq = 90 }
   },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<8;4,1>.x:d {Align16}",   SAME_ERROR, { .eq = 90 } },
   { "add (8) r0.:d r1<16;4,1>.x:d r2<4;4,1>.x:d {Align16}",  SAME_ERROR, { .eq = 90 } },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<16;4,1>.x:d {Align16}",  SAME_ERROR, { .eq = 90 } },
   { "add (8) r0.:d r1<32;4,1>.x:d r2<4;4,1>.x:d {Align16}",  SAME_ERROR, { .eq = 90 } },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<32;4,1>.x:d {Align16}",  SAME_ERROR, { .eq = 90 } },
   { "add (8) r0.:d r1<VxH;4,1>.x:d r2<4;4,1>.x:d {Align16}", SAME_ERROR, { .eq = 90 } },
   { "add (8) r0.:d r1<4;4,1>.x:d r2<VxH;4,1>.x:d {Align16}", SAME_ERROR, { .eq = 90 } },


   { "add (32) r0:w r1:w r2<2>:w",
     "A source cannot span more than 2 adjacent GRF registers.", { .lt = 200 }
   },
   { "add (16) r0:w r1:w r2.1<2>:w", VALID, { .lt = 200 } },
   { "add (16) r0:w r1:w r2:w",      VALID, { .lt = 200 } },

   { "add (32) r0:d r1:d r2<2>:d",
     "A source cannot span more than 2 adjacent GRF registers.", { .ge = 200 }
   },
   { "add (16) r0:d r1:d r2.0<2>:d", VALID, { .ge = 200, .lt = 350 } },
   { "add (16) r0:d r1:d r2:d", VALID, { .ge = 200 } },


   { "add (32) r0<2>:w r1:w r2:w",
     "A destination cannot span more than 2 adjacent GRF registers.", { .lt = 200 }
   },
   { "add (32) r0<4>:w r1:w r2:w",  SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.3<4>:w r1:w r2:w", VALID, { .lt = 350 } },


   { "add (8) r0:w r1:w r2<16;4,2>:w",   VALID, { .lt = 200 } },
   { "add (8) r0.8:w r1:w r2<16;4,2>:w", VALID, { .lt = 200 } },
   { "add (16) r0:w r1:w r2<2>:w",       VALID, { .lt = 200 } },
   { "add (4) r0.5:w r1:w r2<16;2,1>:w", VALID, { .lt = 200 } },
   { "add (8) r0:d r1:d r2<16;4,2>:d",   VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0.4:d r1:d r2<16;4,2>:d", VALID, { .ge = 200, .lt = 350 } },
   { "add (16) r0:d r1:d r2<2>:d",       VALID, { .ge = 200, .lt = 350 } },
   { "add (4) r0.2:d r1:d r2<16;2,1>:d", VALID, { .ge = 200, .lt = 350 } },


   /* dst_elements_must_be_evenly_split_between_registers. */
   { "math.sin (8) r0:f r1:f null",
     VALID, { .lt = 200 }
   },
   { "math.sin (8) r0.1:f r1:f null",
     "MATH writes must be evenly split between the two destination registers.", { .lt = 200 }
   },
   { "math.sin (16) r0:f r1:f null",
     VALID, { .ge = 200 }
   },
   { "math.sin (16) r0.1:f r1:f null",
     "MATH writes must be evenly split between the two destination registers.", { .ge = 200 }
   },


   { "add (4) r0<4>:f r1.4<2>:f r2:f", VALID, { .lt = 125 } },
   { "add (4) r0<4>:f r1.4<2>:f r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (4) r0<4>:f r1<4>:f r2<8;2,1>:f", VALID, { .lt = 125 } },
   { "add (4) r0<4>:f r1<4>:f r2<8;2,1>:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "mov (16) r0<2>:w r1.4:w", VALID },
   { "mov (8) r0.4:f r1.2:f", VALID, { .lt = 125 } },
   { "mov (8) r0.4:f r1.2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "add (16) r0:d r0<0>:d r0<0>:d", VALID },
   { "add (16) r0:d r1:w r2:d",       VALID },
   { "add (16) r0:d r1:d r2:w",       VALID, { .lt = 350 } },
   { "add (16) r0:d r1:w r2:w",       VALID, { .lt = 350 } },
   { "add (16) r0<2>:w r1:w r2<0>:w", VALID },
   { "add (16) r0<2>:w r1<0>:w r2:w", VALID, { .lt = 350 } },


   { "mov (8) r0:ub r1:ub",      VALID, },
   { "mov (8) r0:b r1:b",        VALID, },
   { "mov (8) r0:ub r1:b",       VALID, },
   { "mov (8) r0:b r1:ub",       VALID, },
   { "mov (8) r0:ub -r1:ub",
     "Packed byte destinations are only allowed for raw MOV.",
   },
   { "mov (8) r0:ub (abs)r1:ub", SAME_ERROR },
   { "mov.sat (8) r0:ub r1:ub",  SAME_ERROR },
   { "mov (8) r0:ub r1:uw",      SAME_ERROR },
   { "mov (8) r0:b r1:w",        SAME_ERROR },
   { "add (8) r0:b r1:w r2:w",   SAME_ERROR },


   { "(f0.0) sel (8) r0<2>:b r1:w r2:w", VALID },
   { "(f0.0) sel (8) r0.1<2>:b r1:w r2:w", VALID },


   { "mov (8) r0<2>:b 0x00:q",
     "There are no direct conversions between 64-bit types and B/UB.",
   },
   { "mov (8) r0:q r1:b",                     SAME_ERROR },
   { "mov (8) r0<2>:b 0x00:uq", SAME_ERROR },
   { "mov (8) r0:uq r1:b",                    SAME_ERROR },
   { "mov (8) r0<2>:b 0x00:df", SAME_ERROR },
   { "mov (8) r0:df r1:b",                    SAME_ERROR },


   { "mov (4) r0:hf 0:w",
     "Conversions between integer and HF must be strided by a DWord on the destination.",
   },
   { "mov (4) r0<2>:hf 0:w", VALID },
   { "mov (4) r0.1<2>:hf 0:w",
     "Conversions between integer and HF must be DWord-aligned on the destination.",
   },
   { "mov (4) r0:w r1:hf",
     "Conversions between integer and HF must be strided by a DWord on the destination.",
   },
   { "mov (4) r0<2>:w r1:hf", VALID },
   { "mov (4) r0.1<2>:w r1:hf",
     "Conversions between integer and HF must be DWord-aligned on the destination.",
   },
   { "mov (4) r0<4>:b r1:hf", VALID },
   { "mov (4) r0.1<4>:b r1:hf",
     "Conversions between integer and HF must be DWord-aligned on the destination.",
   },
   { "mov (4) r0<2>:hf 0x00:q",
     "There are no direct conversions between 64-bit types and HF.",
   },
   { "mov (4) r0:q r1:hf",       SAME_ERROR },
   { "mov (4) r0<2>:hf 0x00:df", SAME_ERROR, { .has_64bit_float = true } },
   { "mov (4) r0:df r1:hf",      SAME_ERROR, { .has_64bit_float = true } },


   { "add (8) r0<2>:hf r1:f r2:f",      VALID },
   { "add (8) r[a0.0]<2>:hf r1:f r2:f", VALID },
   { "add (8) r0<2>:hf r[a0.0 + 1]:f r2:f",
     "Indirect addressing on source is not supported in mixed float mode.",
   },
   { "add (8) r0:f r1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0:f r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r[a0.0]:f r1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r[a0.0]:f r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:f r[a0.0 + 1]:hf r2:f",
     "Indirect addressing on source is not supported in mixed float mode.",
   },
   { "add (8) r0<2>:hf r1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:f r[a0.0 + 1]:f r2:hf",
     "Indirect addressing on source is not supported in mixed float mode.",
   },


   { "mov (8) r0:f r1<2>:hf", VALID, { .ge = 125 } },
   { "mov (8) r0:f r1:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "mov (8) r0<2>:hf r1:f", VALID, { .ge = 125 } },
   { "mov (8) r0:hf r1:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "mov (8) r0:f r1:f",    VALID, { .ge = 125 } },
   { "mov (8) r0:f acc0:f",  VALID, { .ge = 125 } },
   { "mov (8) r0:f f0:f",
     "Explicit ARF operands must be null, accumulator, or scalar for float and 64-bit regioning restrictions.", { .ge = 125 }
   },
   { "mov (8) acc0:f r1:f",   VALID, { .ge = 125 } },
   { "mov (8) f0:f r1:f",
     "Explicit ARF operands must be null, accumulator, or scalar for float and 64-bit regioning restrictions.", { .ge = 125 }
   },
   { "mov (4) r0:df r1:df",   VALID, { .ge = 125, .has_64bit_float = true } },
   { "mov (4) r0:df acc0:df", VALID, { .ge = 125, .has_64bit_float = true } },
   { "mov (4) r0:df f0:df",
     "Explicit ARF operands must be null, accumulator, or scalar for float and 64-bit regioning restrictions.", { .ge = 125, .has_64bit_float = true }
   },


   { "mov (8) r0:f r[a0.0]:f", VALID, { .ge = 125 } },
   { "mov (8) r0:f r[a0.0]<VxH;8,1>:f",
     "Vx1 and VxH indirect addressing are not allowed for float, bfloat, and 64-bit source types.", { .ge = 125 }
   },
   { "mov (8) r0:f r[a0.0]<VxH;1,0>:f", SAME_ERROR, { .ge = 125 } },
   { "mov (8) r0:f r[a0.0]:bf",         VALID,      { .ge = 125, .has_bfloat16 = true } },
   { "mov (8) r0:f r[a0.0]<VxH;8,1>:bf",
     "Vx1 and VxH indirect addressing are not allowed for float, bfloat, and 64-bit source types.", { .ge = 125, .has_bfloat16 = true }
   },
   { "mov (8) r0:f r[a0.0]<VxH;1,0>:bf", SAME_ERROR, { .ge = 125, .has_bfloat16 = true } },
   { "mov (4) r0:df r[a0.0]:df",         VALID,      { .ge = 125, .has_64bit_float = true } },
   { "mov (4) r0:df r[a0.0]<VxH;4,1>:df",
     "Vx1 and VxH indirect addressing are not allowed for float, bfloat, and 64-bit source types.", { .ge = 125, .has_64bit_float = true }
   },
   { "mov (4) r0:df r[a0.0]<VxH;1,0>:df", SAME_ERROR, { .ge = 125, .has_64bit_float = true } },


   { "add (8) r0<2>:hf r1:f r2:hf", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf r1:f r2:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (16) r0<2>:hf r1:hf r2:f", VALID, { .lt = 125 } },
   { "add (16) r0<2>:hf r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (16) r0:hf r1:hf r2:f",
     "Align1 mixed float mode is limited to SIMD8 when destination is packed half-float.",
   },
   { "add (16) r0:hf r1:f r2:hf", SAME_ERROR, },
   { "add (8) r0:f r1:f r2:hf", VALID, { .lt = 125 } },
   { "add (8) r0:f r1:f r2:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (16) r0:f r1:hf r2:f",
     "Mixed float mode with 32-bit float destination is limited to SIMD8.", { .lt = 125 }
   },
   { "add (16) r0:f r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (16) r0:f r1:f r2:hf",
     "Mixed float mode with 32-bit float destination is limited to SIMD8.", { .lt = 125 }
   },
   { "add (16) r0:f r1:f r2:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "add (8) r0<2>:hf acc0:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf acc0:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0<2>:hf acc0.1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf acc0.1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0<2>:hf acc0.2:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf acc0.2:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0<2>:hf acc0.4:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf acc0.4:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0<2>:hf acc0.8:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf acc0.8:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf r1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0:hf r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf r1.1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0:hf r1.1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf r1.2:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0:hf r1.2:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf r1.4:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0:hf r1.4:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf r1.8:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0:hf r1.8:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf acc0:hf r2:f",
     "Mixed-float operations with accumulator sources and half-float destinations require destination stride 2.",
   },
   { "add (8) r0:hf acc0.1:hf r2:f",
     "Mixed-float accumulator reads into packed half-float destinations require src0 subnr 0.",
   },
   { "add (8) r0:hf acc0.2:hf r2:f", SAME_ERROR, },
   { "add (8) r0:hf acc0.4:hf r2:f", SAME_ERROR, },
   { "add (8) r0:hf acc0.8:hf r2:f", SAME_ERROR, },


   { "mac (8) r0:hf r1:hf r2:f",
     "Mixed-float operations with accumulator sources and half-float destinations require destination stride 2.",
   },
   { "mac (8) r0<2>:hf r1:hf r2:f", VALID, { .lt = 125 } },
   { "mac (8) r0<2>:hf r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "mac (8) r0:hf r1:f r2:hf",
     "Mixed-float operations with accumulator sources and half-float destinations require destination stride 2.",
   },
   { "mac (8) r0<2>:hf r1:f r2:hf", VALID, { .lt = 125 } },
   { "mac (8) r0<2>:hf r1:f r2:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf acc0:f r2:hf",
     "Mixed-float operations with accumulator sources and half-float destinations require destination stride 2.",
   },
   { "add (8) r0<2>:hf acc0:f r2:hf", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf acc0:f r2:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "mac (8) r0:f r1:hf r2:f", VALID, { .lt = 125 } },
   { "mac (8) r0:f r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "mac (8) r0<2>:f r1:hf r2:f", VALID, { .lt = 125 } },
   { "mac (8) r0<2>:f r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0<2>:f r1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:f r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0:hf r1:f r2:hf", VALID, { .lt = 125 } },
   { "add (8) r0:hf r1:f r2:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "add (8) r0<2>:hf r1<2>:hf acc0:f", VALID, },
   { "add (16) r0<2>:hf r1<2>:hf acc0:f", VALID, },
   { "add (8) r0:hf r1<2>:hf acc0:f",
     "Mixed-float operations with accumulator sources and half-float destinations require destination stride 2.",
   },
   { "add (16) r0:hf r1<2>:hf acc0:f",
     "Align1 mixed float mode is limited to SIMD8 when destination is packed half-float.",
   },


   { "add (8) r0:hf r1<2>:hf acc0.1:f",
     "Mixed-float accumulator reads into packed half-float destinations require src1 subnr 0.",
   },
   { "add (8) r0:hf r1<2>:hf acc0.2:f", SAME_ERROR, },
   { "add (8) r0:hf r1<2>:hf acc0.4:f", SAME_ERROR, },
   { "add (8) r0:hf r1<2>:hf acc0.8:f", SAME_ERROR, },
   { "add (8) r0<2>:hf r1<2>:hf acc0.1:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf r1<2>:hf acc0.1:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (8) r0<2>:hf r1<2>:hf acc0.8:f", VALID, { .lt = 125 } },
   { "add (8) r0<2>:hf r1<2>:hf acc0.8:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "math.pow (8) r0:hf r1<4;4,2>:hf r2:f", VALID, { .lt = 125 } },
   { "math.pow (8) r0:hf r1<4;4,2>:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "math.pow (8) r0:hf r1:hf r2:f",
     "Align1 mixed-float MATH requires strided half-float src0 inputs.", { .lt = 125 }
   },
   { "math.pow (8) r0:hf r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "math.pow (8) r0:hf r1:f r2<4;4,2>:hf", VALID, { .lt = 125 } },
   { "math.pow (8) r0:hf r1:f r2<4;4,2>:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "math.pow (8) r0:hf r1:f r2:hf",
     "Align1 mixed-float MATH requires strided half-float src1 inputs.", { .lt = 125 }
   },
   { "math.pow (8) r0:hf r1:f r2:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "math.pow (8) r0<2>:hf r1<4;4,2>:hf r2:f", VALID, { .lt = 125 } },
   { "math.pow (8) r0<2>:hf r1<4;4,2>:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "math.pow (8) r0<2>:hf r1:f r2<4;4,2>:hf", VALID, { .lt = 125 } },
   { "math.pow (8) r0<2>:hf r1:f r2<4;4,2>:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "math.pow (8) r0<2>:hf r1:f r2:hf",
     "Align1 mixed-float MATH requires strided half-float src1 inputs.",
   },
   { "math.pow (8) r0:f r1:f r2<4;4,2>:hf", VALID, { .lt = 125 } },
   { "math.pow (8) r0:f r1:f r2<4;4,2>:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "math.pow (8) r0:f r1<4;4,2>:hf r2:hf",
     "Align1 mixed-float MATH requires strided half-float src1 inputs.",
   },
   { "math.pow (8) r0:f r1<4;4,2>:hf r2<4;4,2>:hf", VALID, { .lt = 125 } },
   { "math.pow (8) r0:f r1<4;4,2>:hf r2<4;4,2>:hf",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },


   { "math.pow (8) r0:f r1<8;4,2>:hf r2:f", VALID, { .lt = 125 } },
   { "math.pow (8) r0:f r1<8;4,2>:hf r2:f",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.pow (8) r0:f r1:hf r2:f",
     "Align1 mixed-float MATH requires strided half-float src0 inputs.",
   },
   { "math.pow (8) r0:f r1:f r2<8;4,2>:hf",
     VALID, { .lt = 125 }
   },
   { "math.pow (8) r0:f r1:f r2<8;4,2>:hf",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.pow (8) r0:f r1:f r2:hf",
     "Align1 mixed-float MATH requires strided half-float src1 inputs.",
   },
   { "math.pow (8) r0<2>:hf r1<8;4,2>:hf r2:f", VALID, { .lt = 125 } },
   { "math.pow (8) r0<2>:hf r1<8;4,2>:hf r2:f",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.pow (8) r0<2>:hf r1:hf r2:f",
     "Align1 mixed-float MATH requires strided half-float src0 inputs.",
   },
   { "math.pow (8) r0<2>:hf r1:f r2<8;4,2>:hf", VALID, { .lt = 125 } },
   { "math.pow (8) r0<2>:hf r1:f r2<8;4,2>:hf",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },


   { "add (8) r0.1:hf r1:hf r2:f",
     "Align1 mixed-float packed half-float destinations must be OWord-aligned.",
   },
   { "add (8) r0.2:hf r1:hf r2:f", SAME_ERROR },
   { "add (8) r0.4:hf r1:hf r2:f", SAME_ERROR },
   { "add (8) r0.8:hf r1:hf r2:f", VALID, { .lt = 125 } },
   { "add (8) r0.8:hf r1:hf r2:f",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "add (16) r0.1:hf r1:hf r2:f",
     "Align1 mixed-float packed half-float destinations must be OWord-aligned.",
   },
   { "add (16) r0.2:hf r1:hf r2:f",
     SAME_ERROR,
   },
   { "add (16) r0.4:hf r1:hf r2:f",
     SAME_ERROR,
   },
   { "add (16) r0.8:hf r1:hf r2:f",
     "Align1 mixed float mode is limited to SIMD8 when destination is packed half-float.",
   },


   { "add (8) r0.:f r1<4;4,1>.x:f r2<4;4,1>.x:hf {Align16}",
     VALID, { .eq = 90 }
   },
   { "add (8) r0.:f r1<2;4,1>.x:f r2<4;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src0 (vstride must be 4).", { .eq = 90 }
   },
   { "add (8) r0.:f r1<4;4,1>.x:f r2<2;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).", { .eq = 90 }
   },
   { "add (8) r0.:f r1<0;4,1>.x:f r2<4;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src0 (vstride must be 4).", { .eq = 90 }
   },
   { "add (8) r0.:f r1<4;4,1>.x:f r2<0;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).", { .eq = 90 }
   },
   { "add (8) r0.:f r1<4;4,1>.x:hf r2<4;4,1>.x:f {Align16}",
     VALID, { .eq = 90 }
   },
   { "add (8) r0.:f r1<4;4,1>.x:hf r2<2;4,1>.x:f {Align16}",
     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).", { .eq = 90 }
   },
   { "add (8) r0.:f r1<2;4,1>.x:hf r2<4;4,1>.x:f {Align16}",
     "Align16 mixed float mode assumes packed data for src0 (vstride must be 4).", { .eq = 90 }
   },
   { "add (8) r0.:f r1<0;4,1>.x:hf r2<4;4,1>.x:f {Align16}",
     SAME_ERROR, { .eq = 90 }
   },
   { "add (8) r0.:f r1<4;4,1>.x:hf r2<0;4,1>.x:f {Align16}",
     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).", { .eq = 90 }
   },


   { "add (16) r0.:f r1<4;4,1>.x:f r2<4;4,1>.x:hf {Align16}",
     "Align16 mixed float mode is limited to SIMD8.", { .eq = 90 }
   },
   { "add (16) r0.:f r1<4;4,1>.x:hf r2<4;4,1>.x:f {Align16}", SAME_ERROR, { .eq = 90 } },


   { "add (8) r0.:f acc0<4;4,1>.x:f r2<4;4,1>.x:hf {Align16}",
     "Align16 mixed float mode does not allow accumulator reads.", { .eq = 90 }
   },
   { "add (8) r0.:f acc0<4;4,1>.x:hf r2<4;4,1>.x:f {Align16}",
     "Align16 mixed float mode does not allow accumulator reads.", { .eq = 90 }
   },


   { "add (8) r0.:f r1<4;4,1>.x:f acc0<4;4,1>.x:hf {Align16}",
     "Align16 mixed float mode does not allow accumulator reads.", { .eq = 90 }
   },
   { "add (8) r0.:f r1<4;4,1>.x:hf acc0<4;4,1>.x:f {Align16}",
     "Align16 mixed float mode does not allow accumulator reads.", { .eq = 90 }
   },


   { "math.pow (8) r0.:f r1<4;4,1>.x:hf r2<0;4,1>.x:f {Align16}",
     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).", { .eq = 90 }
   },
   { "math.pow (8) r0.:f r1<4;4,1>.x:hf r2<4;4,1>.x:hf {Align16}", VALID, { .eq = 90 } },
   { "math.pow (8) r0.:f r1<4;4,1>.x:f r2<0;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).", { .eq = 90 }
   },
   { "math.pow (8) r0.:f r1<2;4,1>.x:f r2<4;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src0 (vstride must be 4).", { .eq = 90 }
   },
   { "math.pow (8) r0.:f r1<4;4,1>.x:f r2<2;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src1 (vstride must be 4).", { .eq = 90 }
   },
   { "math.pow (8) r0.:f r1<0;4,1>.x:hf r2<4;4,1>.x:hf {Align16}",
     "Align16 mixed float mode assumes packed data for src0 (vstride must be 4).", { .eq = 90 }
   },


   { "mov (8) r0<2>:bf r1:f", VALID, { .has_bfloat16 = true } },
   { "mov (8) r0<2>:bf r1:bf",
     "Instructions with pure bfloat operands are not supported.", { .has_bfloat16 = true }
   },
   { "mov (16) r0<2>:bf r1:f",
     "Mixed bfloat mode is limited to SIMD8 before Gfx20.", { .lt = 200, .has_bfloat16 = true }
   },
   { "mov (32) r0<2>:bf r1:f",
     "Mixed bfloat mode is limited to SIMD16 on Gfx20+.", { .ge = 200, .has_bfloat16 = true }
   },
   { "add (8) r0<2>:bf r1:f r2:f", VALID, { .has_bfloat16 = true } },
   { "add (8) r0<2>:bf r1:bf r2:bf",
     "Instructions with pure bfloat operands are not supported.", { .has_bfloat16 = true }
   },
   { "add (8) r0<2>:bf r1:f r2<0>:bf",
     "Broadcast of bfloat scalar is not supported.", { .has_bfloat16 = true }
   },
   { "mul (8) r0:bf r1:bf r2:f", VALID, { .lt = 350, .has_bfloat16 = true } },
   { "mul (8) r0:bf r1:f r2:bf",
     "Bfloat is not allowed in src1 of 2-source multiplier instructions.", { .has_bfloat16 = true }
   },
   { "mad (8) r0:bf r1:bf r2:bf r3:f", VALID, { .has_bfloat16 = true } },
   { "mad (8) r0:bf r1:bf r2:f r3:bf",
     "Bfloat is not allowed in src2 of 3-source multiplier instructions.", { .has_bfloat16 = true }
   },
   { "add (8) r0.8:bf r1:bf r2:f", VALID, { .lt = 200, .has_bfloat16 = true } },
   { "add (8) r0.16:bf r1:bf r2:f", VALID, { .ge = 200, .lt = 350, .has_bfloat16 = true } },
   { "add (8) r0.1:bf r1:bf r2:f",
     "Packed bfloat destination must have register offset 0 or half a GRF.",
     { .lt = 350, .has_bfloat16 = true }
   },
   { "add (8) r0.2<2>:bf r1:f r2:f",
     "Unpacked bfloat destination must have stride 2 and register offset 0 or 1 element.", { .has_bfloat16 = true }
   },
   { "add (8) r0:bf r1<2>:bf r2:f",
     "Bfloat sources must be packed.", { .has_bfloat16 = true }
   },
   { "add (8) r0:bf r1.8:bf r2:f", VALID, { .lt = 200, .has_bfloat16 = true } },
   { "add (8) r0:bf r1.16:bf r2:f", VALID, { .ge = 200, .lt = 350, .has_bfloat16 = true } },
   { "add (8) r0:bf r1.2:bf r2:f",
     "Bfloat sources must have register offset 0 or half a GRF.", { .has_bfloat16 = true }
   },


   { "mov (8) r0.1<2>:bf r1:f", VALID, { .has_bfloat16 = true } },
   { "mov (8) r0.8<2>:bf r1:f",
     "Unpacked bfloat destination must have stride 2 and register offset 0 or 1 element.", { .lt = 200, .has_bfloat16 = true }
   },
   { "mov (8) r0.16<2>:bf r1:f",
     SAME_ERROR, { .ge = 200, .has_bfloat16 = true }
   },


   { "add (16) r0<2>:bf r1:f r2:f",
     "Mixed bfloat mode is limited to SIMD8 before Gfx20.", { .lt = 200, .has_bfloat16 = true }
   },
   { "add (16) r0<2>:bf r1:f r2:f", VALID, { .ge = 200, .has_bfloat16 = true } },
   { "add (32) r0<2>:bf r1:f r2:f",
     "Mixed bfloat mode is limited to SIMD8 before Gfx20.", { .lt = 200, .has_bfloat16 = true }
   },
   { "add (32) r0<2>:bf r1:f r2:f",
     "Mixed bfloat mode is limited to SIMD16 on Gfx20+.", { .ge = 200, .has_bfloat16 = true }
   },


   { "mul (8) r0:f r1:bf r2:f", VALID, { .has_bfloat16 = true } },
   { "mul (8) r0:f r1:f r2:bf",
     "Bfloat is not allowed in src1 of 2-source multiplier instructions.", { .has_bfloat16 = true }
   },
   { "mad (8) r0:f r1:bf r2:f r3:f", VALID, { .has_bfloat16 = true } },
   { "mad (8) r0:f r1:f r2:bf r3:f", VALID, { .lt = 350, .has_bfloat16 = true } },
   { "mad (8) r0:f r1:f r2:f r3:bf",
     "Bfloat is not allowed in src2 of 3-source multiplier instructions.", { .has_bfloat16 = true }
   },


   { "add (8) r0<2>:bf r1:f r2.8:bf", VALID, { .lt = 200, .has_bfloat16 = true } },
   { "add (8) r0<2>:bf r1:f r2.16:bf", VALID, { .ge = 200, .lt = 350, .has_bfloat16 = true } },
   { "add (8) r0<2>:bf r1:f r2.2:bf",
     "Bfloat sources must have register offset 0 or half a GRF.", { .has_bfloat16 = true }
   },
   { "add (8) r0<2>:bf r1:f r2<2>:bf", "Bfloat sources must be packed.", { .has_bfloat16 = true } },


   { "add (8) r0:d r1:d r2:b", VALID, { .le = 100 } },
   { "add (8) r0:d r1:d r2:b",
     "Byte data type is not supported for src1 regioning.", { .ge = 110 }
   },
   { "mad (8) r0.:d r1:d r2:d r3:b {Align16}", VALID, { .eq = 90 } },
   { "mad (8) r0:d r1:d r2:d r3:b",
     "Byte data type is not supported for src1/2 regioning.", { .ge = 110 }
   },
   { "dpas.8x8 (8) r0:d r1:d r2:b r3:b",
     "DPAS is Gfx12.5+ only.", { .lt = 125 }
   },
   { "dpas.8x8 (8) r0:d r1:d r2:b r3:b", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (16) r0:d r1:d r2:b r3:b", VALID, { .ge = 200 } },


   { "srnd (8) r0<2>:hf r1:f 0x0:f", VALID, { .ge = 200 } },
   { "srnd (8) r0<2>:hf r1:f r2:uw",
     "SRND requires dst=HF and src0/src1=F.", { .ge = 200 }
   },


   { "add3 (8) r0:f r1<0;0>:f r2<0;0>:f r3<0>:f",
     "ADD3 destination must be integer D, UD, W, or UW type.", { .ge = 125 }
   },
   { "add3 (8) r0:hf r1<0;0>:hf r2<0;0>:hf r3<0>:hf", SAME_ERROR, { .ge = 125 } },

   { "add3 (8) r0:b r1<0;0>:b r2<0;0>:b r3<0>:b",
     "Byte data type is not supported for src1/2 regioning.", { .ge = 125 }
   },
   { "add3 (8) r0:ub r1<0;0>:ub r2<0;0>:ub r3<0>:ub", SAME_ERROR, { .ge = 125 } },

   { "add3 (8) r0:w r1<0;0>:w r2<0;0>:w r3<0>:w",     VALID, { .ge = 125 } },
   { "add3 (8) r0:uw r1<0;0>:uw r2<0;0>:uw r3<0>:uw", VALID, { .ge = 125 } },
   { "add3 (8) r0 r1<0;0> r2<0;0> r3<0>",             VALID, { .ge = 125 } },
   { "add3 (8) r0:w r1<0;0>:d r2<0;0>:w r3<0>:w",     VALID, { .ge = 125 } },
   { "add3 (8) r0:uw r1<0;0>:uw r2<0;0> r3<0>:uw",    VALID, { .ge = 125 } },
   { "add3 (8) r0:d r1<0;0>:d r2<0;0>:w r3<0>:d",     VALID, { .ge = 125 } },
   { "add3 (8) r0 r1<0;0> r2<0;0> r3<0>:uw",          VALID, { .ge = 125 } },


   { "add3 (8) r0:w 4660:w r2<0;0>:w r3<0>:w",       VALID, { .ge = 125 } },
   { "add3 (8) r0:w r1<0;0>:w r2<0;0>:w 8515:w",     VALID, { .ge = 125 } },
   { "add3 (8) r0:uw 4660:uw r2<0;0>:uw r3<0>:uw",   VALID, { .ge = 125 } },
   { "add3 (8) r0:uw r1<0;0>:uw r2<0;0>:uw 8515:uw", VALID, { .ge = 125 } },
   { "add3 (8) r0:d 4660:w r2<0;0>:d r3<0>:d",       VALID, { .ge = 125 } },
   { "add3 (8) r0 r1<0;0> r2<0;0> 8515:w",           VALID, { .ge = 125 } },
   { "add3 (8) r0:d 4660:uw r2<0;0>:d r3<0>:d",      VALID, { .ge = 125 } },

   { "add3 (8) r0:w 4660:d r2<0;0>:w r3<0>:w",
     "ADD3 immediate sources must be integer W or UW type.", { .ge = 125 }
   },
   { "add3 (8) r0:w r1<0;0>:w r2<0;0>:w 8515:d",        SAME_ERROR, { .ge = 125 } },
   { "add3 (8) r0:uw 0x00001234 r2<0;0>:uw r3<0>:uw",   SAME_ERROR, { .ge = 125 } },
   { "add3 (8) r0:uw r1<0;0>:uw r2<0;0>:uw 0x00002143", SAME_ERROR, { .ge = 125 } },
   { "add3 (8) r0:d 4660:d r2<0;0>:d r3<0>:d",          SAME_ERROR, { .ge = 125 } },
   { "add3 (8) r0 r1<0;0> r2<0;0> 8515:d",              SAME_ERROR, { .ge = 125 } },
   { "add3 (8) r0:d 0x00001234 r2<0;0>:d r3<0>:d",      SAME_ERROR, { .ge = 125 } },


   { "dp4a (8) r0:d acc0<0;0>:d r2<0;0>:d r3<0>:d", VALID, { .ge = 120 } },
   { "dp4a (8) r0:d r1<0;0>:d acc0<0;0>:d r3<0>:d", VALID, { .ge = 120 } },
   { "dp4a (8) r0:d acc0<0;0>:d acc0<0;0>:d r3<0>:d",
     "At most one of DP4A src0 and src1 may be an accumulator.", { .ge = 120 }
   },


   { "dpas.16x8 (8) r0:f r1:f r2:hf r3:hf",
     "DPAS systolic depth must be 8.", { .ge = 125, .lt = 200 }
   },
   { "dpas.2x8 (8) r0:f r1:f r2:hf r3:hf", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.4x8 (8) r0:f r1:f r2:hf r3:hf", SAME_ERROR, { .ge = 125, .lt = 200 } },

   { "dpas.16x8 (16) r0:f r1:f r2:hf r3:hf",
     "DPAS systolic depth must be 8.", { .ge = 200 }
   },
   { "dpas.2x8 (16) r0:f r1:f r2:hf r3:hf", SAME_ERROR, { .ge = 200 } },
   { "dpas.4x8 (16) r0:f r1:f r2:hf r3:hf", SAME_ERROR, { .ge = 200 } },


   { "dpas.8x8 (1) r0:f r1:f r2:hf r3:hf",
     "DPAS execution size must be 8 before Gfx20.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (2) r0:f r1:f r2:hf r3:hf",  SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (4) r0:f r1:f r2:hf r3:hf",  SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3:hf", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (32) r0:f r1:f r2:hf r3:hf", SAME_ERROR, { .ge = 125, .lt = 200 } },

   { "dpas.8x8 (1) r0:f r1:f r2:hf r3:hf",
     "DPAS execution size must be 16 on Gfx20+.", { .ge = 200 }
   },
   { "dpas.8x8 (2) r0:f r1:f r2:hf r3:hf",  SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (4) r0:f r1:f r2:hf r3:hf",  SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (8) r0:f r1:f r2:hf r3:hf",  SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (32) r0:f r1:f r2:hf r3:hf", SAME_ERROR, { .ge = 200 } },


   { "dpas.8x8 (8) r0:f acc0:f r2:hf r3:hf",  VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (16) r0:f acc0:f r2:hf r3:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (8) r0:f 0x0:f r2:hf r3:hf",
     "DPAS currently only supports GRF or GEN_ARF for Source 0.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (16) r0:f 0x0:f r2:hf r3:hf", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (8) r0:f r1:f acc0:hf r3:hf",
     "DPAS currently only supports GRF for Source 1", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f acc0:hf r3:hf", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (8) r0:f r1:f r2:hf acc0:hf",
     "DPAS currently only supports GRF for Source 2", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2:hf acc0:hf", SAME_ERROR, { .ge = 200 } },


   { "dpas.8x8 (8) r0:f r1:f r2:hf r3:hf {Atomic}",
     "Atomic DPAS is not supported by the validator.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3:hf {Atomic}", SAME_ERROR, { .ge = 200 } },


   { "dpas.8x8 (8) r0:hf r1:hf r2:hf r3:hf",
     "DPAS destination type must be F before Gfx20 in floating-point mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:hf r2:hf r3:hf",
     "DPAS src0 type must be F before Gfx20 in floating-point mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:hf r1:f r2:hf r3:hf",
     "DPAS destination type must be F before Gfx20 in floating-point mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2:f r3:hf",
     "DPAS src1 type must be HF or BF in floating-point mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2:hf r3:f",
     "DPAS src2 type must be HF or BF in floating-point mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2:bf r3:bf", VALID, { .ge = 125, .lt = 200, .has_bfloat16 = true } },
   { "dpas.8x8 (8) r0:bf r1:bf r2:bf r3:bf",
     "DPAS destination type must be F before Gfx20 in floating-point mode.", { .ge = 125, .lt = 200, .has_bfloat16 = true }
   },
   { "dpas.8x8 (8) r0:bf r1:f r2:bf r3:bf", SAME_ERROR, { .ge = 125, .lt = 200, .has_bfloat16 = true } },
   { "dpas.8x8 (8) r0:f r1:bf r2:bf r3:bf",
     "DPAS src0 type must be F before Gfx20 in floating-point mode.", { .ge = 125, .lt = 200, .has_bfloat16 = true }
   },
   { "dpas.8x8 (8) r0:df r1:df r2:df r3:df",
     "DPAS destination type must be F before Gfx20 in floating-point mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:df r1:df r2:df r3:f", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:df r1:df r2:f r3:df", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:df r1:f r2:df r3:df", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:df r1:df r2:df r3:hf", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:df r1:df r2:hf r3:df", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:df r1:hf r2:df r3:df", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0 r1 r2:ub r3:ub", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0 r1 r2:ub r3", "DPAS src2 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0 r1 r2 r3:ub",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0 r1 r2:ub r3:uw",
     "DPAS src2 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0 r1 r2:uw r3:ub",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0 r1:ub r2:ub r3:ub",
     "DPAS src0 type must be D or UD in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0 r1:uw r2:ub r3:ub", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:uq r1:uq r2:ub r3:ub",
     "64-bit integer destination requires 64-bit integer support.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:uq r1:uq r2:ub r3:uq", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:uq r1:uq r2:uq r3:ub", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:uq r1:uq r2:ub r3:uw", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:uq r1:uq r2:uw r3:ub", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:d r1:d r2:b r3:ub", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:d r1:d r2:ub r3:b", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:d r1 r2:b r3:b", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:d r1:d r2:b r3:d",
     "DPAS src2 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:d r1:d r2:d r3:b",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:d r1:d r2:b r3:w",
     "DPAS src2 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:d r1:d r2:w r3:b",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:d r1:b r2:b r3:b",
     "DPAS src0 type must be D or UD in integer mode.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:d r1:w r2:b r3:b", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:q r1:q r2:b r3:b",
     "64-bit integer destination requires 64-bit integer support.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:q r1:q r2:b r3:q", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:q r1:q r2:q r3:b", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:q r1:q r2:b r3:w", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:q r1:q r2:w r3:b", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0 r1 r2:ub r3:b",
     "DPAS with an unsigned destination requires all source types to be unsigned.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0 r1 r2:b r3:ub", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0 r1:d r2:ub r3:ub", SAME_ERROR, { .ge = 125, .lt = 200 } },


   { "dpas.8x8 (16) r0:hf r1:hf r2:hf r3:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:f r1:hf r2:hf r3:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:hf r1:f r2:hf r3:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:f r1:f r2:f r3:hf",
     "DPAS src1 type must be HF or BF in floating-point mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3:f",
     "DPAS src2 type must be HF or BF in floating-point mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2:bf r3:bf", VALID, { .ge = 200, .has_bfloat16 = true } },
   { "dpas.8x8 (16) r0:bf r1:bf r2:bf r3:bf", VALID, { .ge = 200, .has_bfloat16 = true } },
   { "dpas.8x8 (16) r0:bf r1:f r2:bf r3:bf", VALID, { .ge = 200, .has_bfloat16 = true } },
   { "dpas.8x8 (16) r0:f r1:bf r2:bf r3:bf", VALID, { .ge = 200, .has_bfloat16 = true } },
   { "dpas.8x8 (16) r0:df r1:df r2:df r3:df",
     "DPAS src1 type must be HF or BF in floating-point mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:df r1:df r2:df r3:f", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:df r1:df r2:f r3:df", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:df r1:f r2:df r3:df", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:df r1:df r2:df r3:hf", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:df r1:df r2:hf r3:df",
     "DPAS src2 type must be HF or BF in floating-point mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:df r1:hf r2:df r3:df",
     "DPAS src1 type must be HF or BF in floating-point mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0 r1 r2:ub r3:ub", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0 r1 r2:ub r3",
     "DPAS src2 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0 r1 r2 r3:ub",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0 r1 r2:ub r3:uw",
     "DPAS src2 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0 r1 r2:uw r3:ub",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0 r1:ub r2:ub r3:ub",
     "DPAS src0 type must be D or UD in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0 r1:uw r2:ub r3:ub", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:uq r1:uq r2:ub r3:ub",
     "DPAS destination type must be D or UD in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:uq r1:uq r2:ub r3:uq", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:uq r1:uq r2:uq r3:ub", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:uq r1:uq r2:ub r3:uw", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:uq r1:uq r2:uw r3:ub", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:d r1:d r2:b r3:ub", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:d r1:d r2:ub r3:b", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:d r1 r2:b r3:b", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:d r1:d r2:b r3:d",
     "DPAS src2 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:d r1:d r2:d r3:b",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:d r1:d r2:b r3:w",
     "DPAS src2 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:d r1:d r2:w r3:b",
     "DPAS src1 type must be B or UB in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:d r1:b r2:b r3:b",
     "DPAS src0 type must be D or UD in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:d r1:w r2:b r3:b", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:q r1:q r2:b r3:b",
     "DPAS destination type must be D or UD in integer mode.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:q r1:q r2:b r3:q", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:q r1:q r2:q r3:b", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:q r1:q r2:b r3:w", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0:q r1:q r2:w r3:b", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0 r1 r2:ub r3:b",
     "DPAS with an unsigned destination requires all source types to be unsigned.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0 r1 r2:b r3:ub", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0 r1:d r2:ub r3:ub", SAME_ERROR, { .ge = 200 } },


   { "dpas.8x8 (8) r0:d r1:d r2:ub r3:ub",   VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0.0:f r1:f r2:hf r3:hf", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:f r1.0:f r2:hf r3:hf", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:f r1:f r2.0:hf r3:hf", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0:f r1:f r2:hf r3.0:hf", VALID, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0.1:f r1:f r2:hf r3:hf",
     "DPAS destination subregister offset must be aligned to the destination execution footprint.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1.1:f r2:hf r3:hf",
     "DPAS src0 subregister offset must be aligned to the src0 execution footprint.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2.1:hf r3:hf",
     "DPAS src1 subregister offset must be 0.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2:hf r3.1:hf",
     "DPAS src2 subregister offset must be aligned to systolic depth times ops-per-channel.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2:hf r3.8:hf", SAME_ERROR, { .ge = 125, .lt = 200 } },
   { "dpas.8x8 (8) r0.8:f r1:f r2:hf r3:hf",
     "DPAS destination subregister offset must not specify the next GRF.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1.8:f r2:hf r3:hf",
     "DPAS src0 subregister offset must not specify the next GRF.", { .ge = 125, .lt = 200 }
   },
   { "dpas.8x8 (8) r0:f r1:f r2:hf r3.16:hf",
     "DPAS src2 subregister offset must not specify the next GRF.", { .ge = 125, .lt = 200 }
   },


   { "dpas.8x8 (16) r0:d r1:d r2:ub r3:ub",   VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0.0:f r1:f r2:hf r3:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:f r1.0:f r2:hf r3:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:f r1:f r2.0:hf r3:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3.0:hf", VALID, { .ge = 200 } },
   { "dpas.8x8 (16) r0.1:f r1:f r2:hf r3:hf",
     "DPAS destination subregister offset must be aligned to the destination execution footprint.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1.1:f r2:hf r3:hf",
     "DPAS src0 subregister offset must be aligned to the src0 execution footprint.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2.1:hf r3:hf",
     "DPAS src1 subregister offset must be 0.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3.1:hf",
     "DPAS src2 subregister offset must be aligned to systolic depth times ops-per-channel.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3.8:hf", SAME_ERROR, { .ge = 200 } },
   { "dpas.8x8 (16) r0.16:f r1:f r2:hf r3:hf",
     "DPAS destination subregister offset must not specify the next GRF.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1.16:f r2:hf r3:hf",
     "DPAS src0 subregister offset must not specify the next GRF.", { .ge = 200 }
   },
   { "dpas.8x8 (16) r0:f r1:f r2:hf r3.32:hf",
     "DPAS src2 subregister offset must not specify the next GRF.", { .ge = 200 }
   },


   { "cmp (8) r0:d r1:d r2:d",
     "CMP and CMPN must have a condition modifier."
   },
   { "cmp (8) (eq)f0.0 r0:d r1:d r2:d", VALID },
   { "cmpn (8) r0:d r1:d r2:d",
     "CMP and CMPN must have a condition modifier."
   },
   { "cmpn (8) (eq)f0.0 r0:d r1:d r2:d", VALID },


   { "sel (8) r0:d r1:d r2:d",
     "SEL must be predicated or use a condition modifier, but not both."
   },
   { "(f0.0) sel (8) (eq)f0.0 r0:d r1:d r2:d", SAME_ERROR },
   { "sel (8) (eq)f0.0 r0:d r1:d r2:d",        VALID },
   { "(f0.0) sel (8) r0:d r1:d r2:d",          VALID },


   { "csel (8) (eq)f0.0 r0.:f r1:f r2:f r3:f {Align16}", VALID, { .eq = 90 } },
   { "csel (8) r0.:f r1:f r2:f r3:f {Align16}",
     "CSEL must have a condition modifier.", { .eq = 90 }
   },
   { "(f0.0) csel (8) (eq)f0.0 r0.:f r1:f r2:f r3:f {Align16}",
     "CSEL cannot be predicated.", { .eq = 90 }
   },
   { "csel (8) (eq)f0.0 r0.:d r1:d r2:d r3:d {Align16}",
     "CSEL destination must be F on Gfx9.", { .eq = 90 }
   },
   { "csel (8) (eq)f0.0 r0:f r1:f r2:f r3:f", VALID, { .ge = 110 } },
   { "csel (8) r0:f r1:f r2:f r3:f",
     "CSEL must have a condition modifier.", { .ge = 110 }
   },
   { "(f0.0) csel (8) (eq)f0.0 r0:f r1:f r2:f r3:f",
     "CSEL cannot be predicated.", { .ge = 110 }
   },
   { "csel (8) (eq)f0.0 r0:d r1:d r2:d r3:d", VALID, { .ge = 110 } },
   { "csel (8) (eq)f0.0 r0:d r1:d r2:d r3:f",
     "CSEL cannot mix floating-point and integer types.", { .ge = 110 }
   },
   { "csel (8) (eq)f0.0 r0:d r1:d r2:d r3:w",
     "CSEL cannot mix operand sizes.", { .ge = 110 }
   },


   { "csel (8) (eq)f0.0 r0:d r1:w r2:d r3:d",
     "CSEL cannot mix operand sizes.", { .ge = 110 }
   },
   { "csel (8) (eq)f0.0 r0:d r1:d r2:f r3:d",
     "CSEL cannot mix floating-point and integer types.", { .ge = 110 }
   },


   { "csel (8) (eq)f0.0 r0.:b r1:b r2:b r3:b {Align16}",
     "CSEL destination must be F on Gfx9.", { .eq = 90 }
   },
   { "csel (8) (eq)f0.0 r0.:hf r1:hf r2:hf r3:hf {Align16}", SAME_ERROR, { .eq = 90 } },
   { "csel (8) (eq)f0.0 r0:b r1:b r2:b r3:b",
     "CSEL destination must be F, HF, D, UD, W, or UW.", { .ge = 110 }
   },
   { "csel (8) (eq)f0.0 r0:hf r1:hf r2:hf r3:hf", VALID, { .ge = 110 } },


   { "bfn.0x00 (8) (eq)f0.0 r0 r1 r2 r3", VALID, { .ge = 120 } },
   { "bfn.0x00 (8) (gt)f0.0 r0 r1 r2 r3", VALID, { .ge = 120 } },
   { "bfn.0x00 (8) (lt)f0.0 r0 r1 r2 r3", VALID, { .ge = 120 } },
   { "bfn.0x00 (8) (ne)f0.0 r0 r1 r2 r3",
     "BFN supports only ZE, GT, LT or none conditional modifiers.", { .ge = 120 }
   },
   { "bfn.0x00 (8) r0 r1 r2:d r3",
     "BFN sources must be UD or UW type.", { .ge = 120 }
   },
   { "bfn.0x00 (8) r0 r1 r2 -r3",
     "BFN does not support source modifiers.", { .ge = 120 }
   },


   { "avg (8) r0 r1 r2", VALID },
   { "avg (8) r0:f r1:f r2:f", "AVG supports only integer operand types." },
   { "avg (8) r0:uq r1:uq r2:uq", "AVG does not support 64-bit operand types." },


   { "line (8) r0:f r1:f r2:f",
     "LINE and PLN require src0 to use the scalar region <0;1,0>.", { .eq = 90 }
   },
   { "pln (8) r0:f r1:f r2:f", SAME_ERROR, { .eq = 90 } },


   { "ror (8) r0:f r1 r2",
     "ROR and ROL require dst type UD or UW.", { .ge = 110 }
   },
   { "ror (8) r0 r1:uw r2",
     "ROR and ROL require src0 and dst to have the same type.", { .ge = 110 }
   },
   { "rol (8) r0:f r1 r2",
     "ROR and ROL require dst type UD or UW.", { .ge = 110 }
   },
   { "rol (8) r0 r1:uw r2",
     "ROR and ROL require src0 and dst to have the same type.", { .ge = 110 }
   },


   { "lrp (8) r0.:f r1:f r2:f r3:d {Align16}",
     "LRP requires dst and all sources to be F type.", { .eq = 90 }
   },


   { "math.intdiv_q (8) r0:d r1:d r2:d", VALID, { .lt = 125 } },
   { "math.intdiv_q (8) r0:d r1:d r2:d",
     "MATH integer divide functions are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.intdiv_q (8) r0:d r1:d -r2:d",
     "MATH integer divide functions do not support source modifiers."
   },
   { "math.sqt (8) null:f r1:f r2:f",
     "MATH must use a GRF destination."
   },
   { "math.pow (8) r0:f r1:f r2:f", VALID, { .lt = 125 } },
   { "math.pow (8) r0:f r1:f r2:f",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.sqt (8) r0:f r1:f r2:f", VALID },
   { "math.sqt (8) r0:df r1:df r2:df",
     "MATH src0 type must be F or HF.", { .ge = 125, .has_64bit_float = true }
   },
   { "math.invm (8) r0:f acc0:f r2:f", VALID, { .lt = 200 } },
   { "math.invm (8) r0:f acc0:f r2:f",
     "Accumulator reads are only allowed for IEEE macro MATH before Gfx20.", { .ge = 200 }
   },
   { "math.sqt (8) r0:f acc0:f r2:f", SAME_ERROR },


   { "math.invm (8) r0:f r1:f r2<16;8,2>:hf", VALID, { .lt = 125 } },
   { "math.invm (8) r0:f r1:f r2<16;8,2>:hf",
     "On Gfx12.5+, 2-source MATH requires both source types to match.", { .ge = 125 }
   },
   { "math.invm (8) r0:f r1:f r2",
     "Before Gfx12.5, 2-source MATH src1 type must be F or HF.", { .lt = 125 }
   },
   { "math.invm (8) r0:f r1:f r2",
     "On Gfx12.5+, 2-source MATH requires both source types to match.", { .ge = 125 }
   },


   { "math.pow (8) r0:f r1:f r2<16;8,2>:hf", VALID, { .lt = 125 } },
   { "math.pow (8) r0:f r1:f r2<16;8,2>:hf",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.pow (8) r0:f r1:f r2",
     "Before Gfx12.5, 2-source MATH src1 type must be F or HF.", { .lt = 125 }
   },
   { "math.pow (8) r0:f r1:f r2",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.fdiv (8) r0:f r1:f r2<16;8,2>:hf", VALID, { .lt = 125 } },
   { "math.fdiv (8) r0:f r1:f r2<16;8,2>:hf",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.fdiv (8) r0:f r1:f r2",
     "Before Gfx12.5, 2-source MATH src1 type must be F or HF.", { .lt = 125 }
   },
   { "math.fdiv (8) r0:f r1:f r2",
     "MATH POW and FDIV are not supported on Gfx12.5+.", { .ge = 125 }
   },


   { "math.sqt (8) r0:f r1:f null",         VALID },
   { "math.sqt (8) r0<2>:hf r1<2>:hf null", VALID },
   { "math.sqt (8) r0<2>:hf r1:f null",     VALID, { .lt = 125 } },
   { "math.sqt (8) r0<2>:hf r1:f null",
     "On Gfx12.5+, MATH src0 and dst types must match.", { .ge = 125 }
   },
   { "math.sqt (8) r0:f r1<8;4,2>:hf null", VALID, { .lt = 125 } },
   { "math.sqt (8) r0:f r1<8;4,2>:hf null",
     "On Gfx12.5+, MATH src0 and dst types must match.", { .ge = 125 }
   },


   { "math.rsqrtm (4) r0:df r1:df null",
     "MATH src0 type must be F or HF.", { .lt = 125, .has_64bit_float = true }
   },
   { "math.rsqrtm (4) r0:df r1:df null", VALID, { .ge = 125, .has_64bit_float = true } },
   { "math.invm (4) r0:df r1:df r2:df",
     "MATH src0 type must be F or HF.", { .lt = 125, .has_64bit_float = true }
   },
   { "math.invm (4) r0:df r1:df r2:df", VALID, { .ge = 125, .has_64bit_float = true } },
   { "math.invm (4) r0:df r1:df r2:f",
     "MATH src0 type must be F or HF.", { .lt = 125, .has_64bit_float = true }
   },
   { "math.invm (4) r0:df r1:df r2:f",
     "On Gfx12.5+, 2-source MATH requires both source types to match.", { .ge = 125, .has_64bit_float = true }
   },
   { "math.rsqrtm (4) r0:f r1:df null",
     "MATH src0 type must be F or HF.", { .lt = 125, .has_64bit_float = true }
   },
   { "math.rsqrtm (4) r0:f r1:df null",
     "On Gfx12.5+, MATH src0 and dst types must match.", { .ge = 125, .has_64bit_float = true }
   },


   { "math.rsqrtm (8) r0:f acc0:f r2:f", VALID, { .lt = 200 } },
   { "math.rsqrtm (8) r0:f acc0:f r2:f",
     "Accumulator reads are only allowed for IEEE macro MATH before Gfx20.", { .ge = 200 }
   },
   { "math.invm (8) r0:f r1:f acc0:f", VALID, { .lt = 200 } },
   { "math.invm (8) r0:f r1:f acc0:f",
     "Accumulator reads are only allowed for IEEE macro MATH before Gfx20.", { .ge = 200 }
   },


   { "math.intdiv_r (8) r0 r1 r2", VALID, { .lt = 125 } },
   { "math.intdiv_r (8) r0 r1 r2",
     "MATH integer divide functions are not supported on Gfx12.5+.", { .ge = 125 }
   },
   { "math.intdiv_r (8) r0 r1 r2:d",
     "MATH integer divide functions require matching operand types."
   },
   { "math.intdiv_r (8) r0:d r1 r2", SAME_ERROR },


   { "xor (8) r0 r1 -acc0",
     "Logic instruction source modifiers are not allowed on accumulator src1."
   },


   { "and (8) r0 r1 (abs)0x00000001", VALID },
   { "and (8) (eq)f0.0 r0 r1 r2",     VALID },
   { "and (8) r0 (abs)r1 r2",
     "Logic instructions do not support abs on src0."
   },
   { "and (8) r0 r1 (abs)r2",
     "Logic instructions do not support abs on src1."
   },
   { "xor (8) r0 -acc0 r2",
     "Logic instruction source modifiers are not allowed on accumulator src0."
   },


   { "and (8) (un)f0.0 r0 r1 r2",
     "Logic instructions must not use OV or UN conditional modifiers."
   },
   { "or (8) (un)f0.0 r0 r1 r2",  SAME_ERROR },
   { "xor (8) (un)f0.0 r0 r1 r2", SAME_ERROR },
   { "not (4) (un)f0.0 r0 r1",    SAME_ERROR },
   { "not (4) (ov)f0.0 r0 r1",    SAME_ERROR },
   { "or (8) (eq)f0.0 r0 r1 r2",  VALID },
   { "xor (8) (eq)f0.0 r0 r1 r2", VALID },
   { "not (4) (eq)f0.0 r0 r1",    VALID },


   { "bfi2 (8) r0.:d r1:d r2:d r3:d {Align16}", VALID, { .eq = 90 } },
   { "bfi2 (8) (eq)f0.0 r0.:d r1:d r2:d r3:d {Align16}",
     "BFI2 cannot use a condition modifier.", { .eq = 90 }
   },
   { "bfi2.sat (8) r0.:d r1:d r2:d r3:d {Align16}",
     "BFI2 cannot use saturate.", { .eq = 90 }
   },
   { "bfi2 (8) r0.:f r1:f r2:f r3:f {Align16}",
     "BFI2 destination type must be D or UD.", { .eq = 90 }
   },
   { "bfi2 (8) r0.:d r1:d r2:d r3 {Align16}",
     "BFI2 source types must match the destination type.", { .eq = 90 }
   },
   { "bfi2 (8) r0.:d 0:d r2:d r3:d {Align16}",
     "BFI2 cannot use immediate sources.", { .eq = 90 }
   },
   { "bfi2 (8) r0.:d r1:d 0:d r3:d {Align16}", SAME_ERROR, { .eq = 90 } },
   { "bfi2 (8) r0.:d r1:d r2:d 0:d {Align16}", SAME_ERROR, { .eq = 90 } },
   { "bfi2 (8) r0:d r1:d r2:d r3:d",           VALID, { .ge = 110 } },
   { "bfi2 (8) (eq)f0.0 r0:d r1:d r2:d r3:d",
     "BFI2 cannot use a condition modifier.", { .ge = 110 }
   },
   { "bfi2.sat (8) r0:d r1:d r2:d r3:d",
     "BFI2 cannot use saturate.", { .ge = 110 }
   },
   { "bfi2 (8) r0:f r1:f r2:f r3:f",
     "BFI2 destination type must be D or UD.", { .ge = 110 }
   },
   { "bfi2 (8) r0:d r1:d r2:d r3",
     "BFI2 source types must match the destination type.", { .ge = 110 }
   },
   { "bfi2 (8) r0:d 0:d r2:d r3:d",
     "BFI2 cannot use immediate sources.", { .ge = 110 }
   },
   { "bfi2 (8) r0:d r1:d 0:d r3:d", SAME_ERROR, { .ge = 110 } },
   { "bfi2 (8) r0:d r1:d r2:d 0:d", SAME_ERROR, { .ge = 110 } },


   { "mul (8) r0 r1:uw r2",
     "When multiplying a DWord and a lower-precision integer, the DWord operand must be src0."
   },
   { "mul.sat (8) r0 r1 r2:uw",
     "DWord integer MUL with W/UW/D/UD destinations cannot use saturate or conditional modifiers."
   },
   { "mul (8) (eq)f0.0 r0 r1 r2:uw", SAME_ERROR },
   { "mul (8) r0 r1 -r2:uw",         VALID, { .lt = 120 } },
   { "mul (8) r0 r1 -r2:uw",
     "When multiplying a DWord and a lower-precision integer, source modifiers are not supported.", { .ge = 120 }
   },


   { "mul (8) r0 -r1 r2:uw",     VALID, { .lt = 350 } },
   { "mul (8) r0 r1 (abs)r2:uw", VALID, { .lt = 120 } },
   { "mul (8) r0 r1 (abs)r2:uw",
     "When multiplying a DWord and a lower-precision integer, source modifiers are not supported.", { .ge = 120 }
   },
   { "mul (8) r0 -r1:uw r2",
     "When multiplying a DWord and a lower-precision integer, the DWord operand must be src0."
   },


   { "mul (8) r0 r1:uw 0x0",
     "When multiplying a DWord and a lower-precision integer, the DWord operand must be src0."
   },
   { "mul (8) r0 r1 0:uw", VALID },


   { "mul (8) r0:f r1 r2",
     "MUL cannot mix floating-point destination types with integer sources."
   },
   { "mul (8) r0:f r1:f r2",              SAME_ERROR },
   { "mul (8) r0:f r1 0x0:vf",            SAME_ERROR },
   { "mul (8) r0:f r1:f r2:f",            VALID },
   { "mul (8) r0<2> r1<8;4,2> r2<8;4,2>", VALID },


   { "mul (8) r0 acc0 r2",
     "MUL integer sources cannot use accumulator registers."
   },
   { "mul (8) r0 r1 acc0",       SAME_ERROR },
   { "mul (8) r0:f acc0:f r2:f", VALID },


   { "add (8) r0:f r1:f r2",
     "ADD cannot mix floating-point and integer source types."
   },
   { "add (8) r0:f r1 r2:f", SAME_ERROR },
   { "add (8) r0:f r1:f r2:f", VALID },
   { "add (8) r0 r1 r2", VALID },


   { "mov (4) r0:f 0x0:vf", VALID },
   { "mov (4) r0.4:f 0x0:vf", VALID },
   { "mov (4) r0.1:f 0x0:vf",
     "Destination must be 128-bit aligned for vector immediate types."
   },
   { "mov (8) r0:w 0x0:v", VALID },
   { "mov (8) r0.8:w 0x0:v", VALID },
   { "mov (8) r0.1:w 0x0:v",
     "Destination must be 128-bit aligned for vector immediate types."
   },
   { "mov (8) r0:w 0x0:uv", VALID },
   { "mov (8) r0.8:w 0x0:uv", VALID },
   { "mov (8) r0.1:w 0x0:uv",
     "Destination must be 128-bit aligned for vector immediate types."
   },


   { "mov (8) r0:f 0x0:vf", VALID },
   { "mov (8) r0<2>:f 0x0:vf",
     "Destination stride must be dword-equivalent for VF immediate types."
   },
   { "mov (8) r0:d 0x0:vf", VALID },
   { "mov (8) r0<2>:d 0x0:vf",
     "Destination stride must be dword-equivalent for VF immediate types."
   },
   { "mov (8) r0<2>:w 0x0:vf", VALID },
   { "mov (8) r0<4>:b 0x0:vf", VALID },
   { "mov (8) r0<2>:w 0x0:v",
     "Destination stride must be word-equivalent for V/UV immediate types."
   },
   { "mov (8) r0<4>:w 0x0:v", SAME_ERROR },
   { "mov (8) r0<2>:b 0x0:v", VALID },
   { "mov (8) r0<2>:w 0x0:uv",
     "Destination stride must be word-equivalent for V/UV immediate types."
   },
   { "mov (8) r0<4>:w 0x0:uv", SAME_ERROR },
   { "mov (8) r0<2>:b 0x0:uv", VALID },


   { "mul (8) r0<2>:d r1<8;4,2>:d r2<8;4,2>:d", VALID },
   { "mul (8) r0<2>:d r1<4;4,1>:d r2<8;4,2>:d", VALID, { .le = 120, .is_not_9lp = true } },
   { "mul (8) r0<2>:d r1<4;4,1>:d r2<8;4,2>:d",
     "64-bit and integer DW-multiply Align1 regions require matching qword-aligned source and destination strides.", { .is_9lp = true }
   },
   { "mul (8) r0<2>:d r1<4;4,1>:d r2<8;4,2>:d",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "mul (8) r0<2>:d r1<0;4,2>:d r2<8;4,2>:d", VALID, { .le = 120, .is_not_9lp = true } },
   { "mul (8) r0<2>:d r1<0;4,2>:d r2<8;4,2>:d",
     "64-bit and integer DW-multiply Align1 regions require vstride = width * hstride.", { .is_9lp = true }
   },
   { "mul (8) r0<2>:d r1<0;4,2>:d r2<8;4,2>:d",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "mul (8) r0.1<2>:d r1<8;4,2>:d r2<8;4,2>:d", VALID, { .le = 120, .is_not_9lp = true } },
   { "mul (8) r0.1<2>:d r1<8;4,2>:d r2<8;4,2>:d",
     "64-bit and integer DW-multiply Align1 regions require matching source and destination subregister offsets.", { .is_9lp = true }
   },
   { "mul (8) r0.1<2>:d r1<8;4,2>:d r2<8;4,2>:d",
     "Float and 64-bit source regions must preserve the LSB channel bit location unless using scalar broadcast.", { .ge = 125 }
   },
   { "mov (2) r0.1:df r1<0>:df", VALID, { .has_64bit_float = true } },


   { "mov (4) r[a0.0]:f r1:f", VALID },
   { "mul (8) r[a0.0]<2>:d r1<8;4,2>:d r2<8;4,2>:d", VALID, { .is_not_9lp = true } },
   { "mul (8) r[a0.0]<2>:d r1<8;4,2>:d r2<8;4,2>:d",
     "Indirect addressing is not allowed for 64-bit and integer DW-multiply operations on Gfx9 LP.", { .is_9lp = true }
   },
   { "mul (8) r0<2>:d r[a0.0 + 1]<8;4,2>:d r2<8;4,2>:d", VALID, { .is_not_9lp = true } },
   { "mul (8) r0<2>:d r[a0.0 + 1]<8;4,2>:d r2<8;4,2>:d",
     "Indirect addressing is not allowed for 64-bit and integer DW-multiply operations on Gfx9 LP.", { .is_9lp = true }
   },


   { "mul (8) acc0<2>:d r1<8;4,2>:d r2<8;4,2>:d", VALID, { .is_not_9lp = true } },
   { "mul (8) acc0<2>:d r1<8;4,2>:d r2<8;4,2>:d",
     "Architecture registers cannot be used for 64-bit and integer DW-multiply operations on Gfx9 LP.", { .is_9lp = true }
   },
   { "mul (8) null<2>:d r1<8;4,2>:d r2<8;4,2>:d", VALID, { .lt = 350 } },
   { "mul (8) null<2>:d r1<8;4,2>:d r2<8;4,2>:d {AccWrEn}", VALID, { .lt = 200, .is_not_9lp = true } },
   { "mul (8) null<2>:d r1<8;4,2>:d r2<8;4,2>:d {AccWrEn}",
     "Architecture registers cannot be used for 64-bit and integer DW-multiply operations on Gfx9 LP.", { .is_9lp = true }
   },
   { "mul (8) null<2>:d r1<8;4,2>:d r2<8;4,2>:d {AccWrEn}",
     "AccWrControl is not present on Gfx20+.", { .ge = 200 }
   },
   { "mac (4) r0:df r1<4;4,1>:df r2<4;4,1>:df", VALID, { .is_not_9lp = true, .has_64bit_float = true } },
   { "mac (4) r0:df r1<4;4,1>:df r2<4;4,1>:df",
     "Architecture registers cannot be used for 64-bit and integer DW-multiply operations on Gfx9 LP.", { .is_9lp = true, .has_64bit_float = true }
   },


   { "mov (2) r0.:q r1<2;2,1>.x:d {Align16}", VALID, { .le = 100, .has_64bit_int = true } },
   { "mov (4) r0.:q r1<4;4,1>.x:d {Align16}",
     "Align16 with a 64-bit destination and a non-64-bit source requires exec_size <= 2.", { .le = 100, .has_64bit_int = true }
   },
   { "mov (2) r0.:df r1<2;2,1>.x:f {Align16}", VALID, { .le = 100, .has_64bit_float = true } },
   { "mov (4) r0.:df r1<4;4,1>.x:f {Align16}",
     "Align16 with a 64-bit destination and a non-64-bit source requires exec_size <= 2.", { .le = 100, .has_64bit_float = true }
   },


   { "mul (8) r0<2>:d r1<8;4,2>:d r2<8;4,2>:d {NoDDChk}", VALID, { .le = 110, .is_not_9lp = true } },
   { "mul (8) r0<2>:d r1<8;4,2>:d r2<8;4,2>:d {NoDDChk}",
     "DepCtrl is not allowed for 64-bit and integer DW-multiply operations on Gfx9 LP.", { .is_9lp = true }
   },
   { "mul (8) r0<2>:d r1<8;4,2>:d r2<8;4,2>:d {NoDDClr}", VALID, { .le = 110, .is_not_9lp = true } },
   { "mul (8) r0<2>:d r1<8;4,2>:d r2<8;4,2>:d {NoDDClr}",
     "DepCtrl is not allowed for 64-bit and integer DW-multiply operations on Gfx9 LP.", { .is_9lp = true }
   },


   { "add (8) r0:d r1:d r2:d {Serialize}",
     "Fusion control bit only used for Gfx12.", { .lt = 120 }
   },
   { "add (8) r0:d r1:d r2:d {Serialize}", SAME_ERROR, { .ge = 200 } },
   { "send.null (16) null r1 null:0 0x0 0x0 {Serialize}", SAME_ERROR, { .lt = 120 } },
   { "send.null (16) null r1 null:0 0x0 0x0 {Serialize}", SAME_ERROR, { .ge = 200 } },
   { "add (8) r0:d r1:d r2:d {Serialize}",
     "Fusion control is only allowed on send instructions.", { .ge = 120, .le = 125 }
   },
   { "send.null (16) null r1 null:0 0x0 0x0 {Serialize}",
     VALID, { .ge = 120, .le = 125 }
   },


   { "mov (4) r0:d r1:d {AccWrEn}", VALID, { .lt = 200 } },
   { "mov (4) r0:d r1:d {AccWrEn}",
     "AccWrControl is not present on Gfx20+.", { .ge = 200 }
   },


   { "send.null (16) null f0 0x0 0x0", "send from non-GRF", { .lt = 120 } },
   { "send.null (16) null r1 f0:0 0x0 0x0",
     "src1 of split send must be a GRF or NULL", { .ge = 120 }
   },


   { "send.null (16) null r111 0x0 0x0 {EOT}",
     "send with EOT must use g112-g127", { .lt = 120 }
   },
   { "send.null (16) null r112 0x0 0x0 {EOT}", VALID, { .lt = 120 } },
   { "send.null (16) null r111 r112:0 0x0 0x0 {EOT}",
     "send with EOT must use g112-g127", { .ge = 120, .lt = 200 }
   },
   { "send.null (16) null r111 r112:0 0x0 0x0 {EOT}", VALID, { .ge = 200 } },
   { "send.null (16) null r112 r111:0 0x0 0x0 {EOT}",
     "send with EOT must use g112-g127", { .ge = 120, .lt = 200 }
   },
   { "send.null (16) null r112 r111:0 0x0 0x0 {EOT}", VALID, { .ge = 200 } },
   { "send.null (16) null r112 r112:0 0x0 0x0 {EOT}", VALID, { .ge = 120 } },


   { "sends.null (16) null r[a0.0 + 1] null 0x0 0x0",
     "send must use direct addressing", { .lt = 120 }
   },
   { "sends.null (16) null r1 r[a0.0 + 2] 0x0 0x0", SAME_ERROR, { .lt = 120 } },
   { "send.null (16) null r[a0.0 + 1] null:0 0x0 0x0",
     SAME_ERROR, { .ge = 120 }
   },
   { "send.null (16) null r1 r[a0.0 + 2]:0 0x0 0x0", SAME_ERROR, { .ge = 120 } },


   { "send.null (16) r127 r126 0x0 0x04100000",
     "r127 must not be used for return address when there is a src and dest overlap", { .eq = 90 }
   },
   { "send.null (16) r127 r125 0x0 0x04100000", VALID, { .eq = 90 } },
   { "send.null (16) null r126 0x0 0x04100000", VALID, { .eq = 90 } },


   { "sends.null (16) null r4 r6 0x0 0x04100000", VALID, { .lt = 120 } },
   { "sends.null (16) null r4 r5 0x0 0x04100000",
     "Split-send payloads must not overlap.", { .lt = 120 }
   },
   { "sends.null (16) null r4 r4 a0.0 a0.0", SAME_ERROR, { .lt = 120 } },
   { "send.null (16) null r4 r6:1 0x0 0x04100000", VALID, { .ge = 120 } },
   { "send.null (16) null r4 r5:1 0x0 0x04100000",
     "Split-send payloads must not overlap.", { .ge = 120 }
   },
   { "send.null (16) null r4 r4 a0.0 a0.0", SAME_ERROR, { .ge = 120 } },


   { "sends.null (16) null r4 r7 a0.0 0x06100000", VALID, { .lt = 120 } },
   { "sends.null (16) null r4 r6 a0.0 0x06100000",
     "Split-send payloads must not overlap.", { .lt = 120 }
   },
   { "send.null (16) null r4 r7 a0.0 0x06100000", VALID, { .ge = 120 } },
   { "send.null (16) null r4 r6 a0.0 0x06100000",
     "Split-send payloads must not overlap.", { .ge = 120 }
   },
   { "send.null (16) null r5 r4:2 0x0 a0.0", SAME_ERROR, { .ge = 120 } },
   { "send.null (16) null r6 r4:2 0x0 a0.0", VALID, { .ge = 120 } },


   { "send.ugm (16) null r1 null:0 0x0 0x02100500", VALID, { .has_lsc = true } },
   { "send.slm (16) null r1 null:0 0x0 0x02100500", VALID, { .has_lsc = true } },
   { "send.tgm (16) null r1 null:0 0x0 0x02100500", VALID, { .has_lsc = true } },
   { "send.ugm (16) null r1 null:0 0x0 0x02100500",
     "Platform does not support LSC.", { .no_lsc = true }
   },
   { "send.slm (16) null r1 null:0 0x0 0x02100500", SAME_ERROR, { .no_lsc = true } },
   { "send.tgm (16) null r1 null:0 0x0 0x02100500", SAME_ERROR, { .no_lsc = true } },


   { "send.ugm (8) null r1 null:0 0x0 0x02109500",
     "Transposed LSC vectors are restricted to exec_size=1.", { .has_lsc = true }
   },
   { "send.ugm (1) null r1 null:0 0x0 0x02109500", VALID, { .has_lsc = true } },
   { "send.ugm (8) null r1 null:0 0x0 0x02109504",
     "Transposed LSC vectors are restricted to exec_size=1.", { .has_lsc = true }
   },
   { "send.ugm (1) null r1 null:0 0x0 0x02109504", VALID, { .has_lsc = true } },
   { "send.slm (8) null r1 null:0 0x0 0x02109500",
     "Transposed LSC vectors are restricted to exec_size=1.", { .has_lsc = true }
   },
   { "send.slm (1) null r1 null:0 0x0 0x02109500", VALID, { .has_lsc = true } },
   { "send.slm (8) null r1 null:0 0x0 0x02109504",
     "Transposed LSC vectors are restricted to exec_size=1.", { .has_lsc = true }
   },
   { "send.slm (1) null r1 null:0 0x0 0x02109504", VALID, { .has_lsc = true } },
   { "send.tgm (8) null r1 null:0 0x0 0x02109500",
     "Transposed LSC vectors are restricted to exec_size=1.", { .has_lsc = true }
   },
   { "send.tgm (1) null r1 null:0 0x0 0x02109500", VALID, { .has_lsc = true } },
   { "send.tgm (8) null r1 null:0 0x0 0x02109504",
     "Transposed LSC vectors are restricted to exec_size=1.", { .has_lsc = true }
   },
   { "send.tgm (1) null r1 null:0 0x0 0x02109504", VALID, { .has_lsc = true } },


   { "send.urb (16) null r1 null:0 0x0 0x02100004",
     "Header must be present for all pre-Gfx20 URB messages.", { .lt = 200 }
   },
   { "send.urb (16) null r1 null:0 0x0 0x02180004", VALID, { .lt = 200 } },
   { "send.urb (16) null r1 null:0 0x0 0x02080008",
     "URB SIMD8 read messages must have a non-zero response length.", { .lt = 200 }
   },
   { "send.urb (16) null r1 null:0 0x0 0x02180008", VALID, { .lt = 200 } },
   { "send.urb (16) null r1 null:0 0x0 0x02180000",
     "Invalid URB message.", { .lt = 200 }
   },
   { "send.urb (16) null r1 null:0 0x0 0x02180009",
     "URB fence messages require Gfx12.5+.", { .lt = 125 }
   },
   { "send.urb (16) null r1 null:0 0x0 0x02180009", VALID, { .ge = 125, .lt = 200 } },
   { "send.urb (16) null r1 null:0 0x0 0x02000008",
     "URB SIMD8 read messages must have a non-zero response length.", { .lt = 200 }
   },
   { "send.urb (16) null r1 null:0 0x0 a0.0", VALID, { .lt = 200 } },
   { "send.ugm (8) null r1 null:0 0x0 a0.0", VALID, { .ge = 200, .has_lsc = true } },
   { "send.urb (16) null r1 null a0.0 0x02000008",
     "URB SIMD8 read messages must have a non-zero response length.", { .ge = 120, .lt = 200 }
   },
   { "send.urb (16) null r1 null a0.0 a0.0", VALID, { .ge = 120, .lt = 200 } },

   { "send.ugm (8) null r1 null:0 a0.0 0x02109500",
     "Transposed LSC vectors are restricted to exec_size=1.", { .ge = 200, .has_lsc = true }
   },
   { "send.ugm (8) null r1 null:0 a0.0 a0.0", VALID, { .ge = 200, .has_lsc = true } },


   /* ARF scalar register restrictions. */

   { "mov (1) s0 r1<0>",
     "Scalar registers are not available before Gfx30.", { .lt = 300 }
   },
   { "add (8) s0 r1 r2",
     "Scalar-register destinations are only allowed for MOV.", { .ge = 300 }
   },
   { "mov (1) s0 r1<0>", VALID, { .ge = 300 } },
   { "mov (1) s0:uq 0x00:uq", VALID, { .ge = 300 } },
   { "mov (1) s0:uq 0x0",
     "Scalar-register MOV requires matching source and destination types.", { .ge = 300 }
   },
   { "mov (8) s0:uw 0:uw",
     "Scalar-register MOV with an immediate source requires exec_size=1.", { .ge = 300 }
   },
   { "mov (1) (eq)f0.0 s0:uw 0:uw",
     "Scalar-register MOV with an immediate source cannot use a condition modifier.", { .ge = 300 }
   },
   { "mov (1) s0.28 r1<0>",
     "Scalar-register destinations must not cross the lower/upper 8-dword boundary.", { .ge = 300 }
   },
   { "mov (8) r0:uw s0<0>:uw", VALID, { .ge = 300 } },
   { "mov (8) r0:uw s0:uw",
     "Scalar-register MOV sources must use scalar broadcast region <0;1,0>.", { .ge = 300 }
   },
   { "mov (1) s0:uw s0<0>:uw",
     "A scalar-register source cannot be written to a scalar-register destination.", { .ge = 300 }
   },
   { "send.null (16) null r[s0.0] null:0 0x0 0x0", VALID, { .ge = 300 } },
   { "send.null (16) null r[s0.1] null:0 0x0 0x0",
     "scalar gather send requires an even scalar subregister", { .ge = 300 }
   },
   { "send.null (16) null r[s0.0] r2:0 0x0 0x0",
     "SEND and SENDC with a scalar-register src0 require src1 to be null.", { .ge = 300 }
   },
   { "sendc.null (16) null r[s0.0] null:0 0x0 0x0", VALID, { .ge = 300 } },
   { "sendc.null (16) null r[s0.1] null:0 0x0 0x0",
     "scalar gather send requires an even scalar subregister", { .ge = 300 }
   },
   { "sendc.null (16) null r[s0.0] r2:0 0x0 0x0",
     "SEND and SENDC with a scalar-register src0 require src1 to be null.", { .ge = 300 }
   },
   { "send.null (16) null r1 r[s0.0]:0 0x0 0x0",
     "Scalar registers are only allowed in src0.", { .ge = 300 }
   },


   /* Xe2 register region special restrictions for Src0 and Src1. */

   /* Source 0. One element per dword channel. */
   { "add (8) r0:d r2:d r4:d",                   VALID, { .ge = 200 } },
   { "add (8) r0:d r2:w r4:d",                   VALID, { .ge = 200 } },
   { "add (8) r0:d r2:b r4:d",                   VALID, { .ge = 200 } },
   { "add (8) r0<2>:w r2:d r4:d",                VALID, { .ge = 200 } },
   { "add (8) r0<2>:w r2:w r4:d",                VALID, { .ge = 200 } },
   { "add (8) r0<2>:w r2:b r4:d",                VALID, { .ge = 200 } },
   { "add (8) r0<4>:b r2:d r4:d",                VALID, { .ge = 200 } },
   { "add (8) r0<4>:b r2:w r4:d",                VALID, { .ge = 200 } },
   { "add (8) r0<4>:b r2:b r4:d",                VALID, { .ge = 200 } },
   { "add (8) r0:d r[a0.0 + 2]<VxH;8,1>:d r4:d", VALID, { .ge = 200 } },
   { "add (8) r0:d r[a0.0 + 2]<VxH;1,0>:d r4:d", VALID, { .ge = 200 } },

   /* Source 0. Uniform stride W->W cases. */
   { "add (8) r0.1:w r2:w r4:w", VALID, { .ge = 200 } },
   { "add (8) r0.1:w r2.2:w r4:w", VALID, { .ge = 200 } },
   { "add (8) r0.1:w r2<2>:w r4:w",
     "Invalid register region for source 0. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.1:w r2.2<2>:w r4:w", VALID, { .ge = 200 } },
   { "add (8) r0.1:w r2<4>:w r4:w",
     "Invalid register region for source 0. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.1:w r2.2<4>:w r4:w", SAME_ERROR, { .ge = 200 } },

   /* Source 0. Dword aligned W->W cases. */
   { "add (8) r0.2:w r2<8;4,1>:w r4:w",   VALID, { .ge = 200 } },
   { "add (8) r0.2:w r2.4<8;4,1>:w r4:w", VALID, { .ge = 200 } },
   { "add (8) r0.2:w r2<2>:w r4:w",
     "Invalid register region for source 0. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.2:w r2.4<2>:w r4:w", VALID, { .ge = 200 } },
   { "add (8) r0.2:w r2<16;2,4>:w r4:w",
     "Invalid register region for source 0. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.2:w r2.4<16;2,4>:w r4:w", SAME_ERROR, { .ge = 200 } },

   /* Source 0. Uniform stride W->B cases. */
   { "add (8) r0.2<2>:b r2:w r4:w",      VALID, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2.1:w r4:w",    VALID, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2<2>:w r4:w",
     "Invalid register region for source 0. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.2<2>:b r2.1<2>:w r4:w", SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2<4>:w r4:w",   SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2.1<4>:w r4:w", SAME_ERROR, { .ge = 200 } },

   /* Source 0. Dword aligned W->B cases. */
   { "add (8) r0.4<2>:b r2<8;4,1>:w r4:w",   VALID, { .ge = 200 } },
   { "add (8) r0.4<2>:b r2.2<8;4,1>:w r4:w", VALID, { .ge = 200 } },
   { "add (8) r0.4<2>:b r2<2>:w r4:w",
     "Invalid register region for source 0. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.4<2>:b r2.2<2>:w r4:w",      SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.4<2>:b r2<16;2,4>:w r4:w",   SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.4<2>:b r2.2<16;2,4>:w r4:w", SAME_ERROR, { .ge = 200 } },

   /* Source 1. One element per dword channel. */
   { "add (8) r0:d r2:d r4:w",    VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0<2>:w r2:d r4:w", VALID, { .ge = 200, .lt = 350 } },

   /* Source 1. Uniform stride W->W cases. */
   { "add (8) r0.1:w r2:w r4.2:w",    VALID, { .ge = 200 } },
   { "add (8) r0.1:w r2:w r4<2>:w",
     "Invalid register region for source 1. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.1:w r2:w r4.2<2>:w", VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0.1:w r2:w r4<4>:w",
     "Invalid register region for source 1. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.1:w r2:w r4.2<4>:w", SAME_ERROR, { .ge = 200 } },

   /* Source 1. Dword aligned W->W cases. */
   { "add (8) r0.2:w r2:w r4<8;4,1>:w",   VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0.2:w r2:w r4.4<8;4,1>:w", VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0.2:w r2:w r4<2>:w",
     "Invalid register region for source 1. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.2:w r2:w r4.4<2>:w",     VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0.2:w r2:w r4<16;2,4>:w",
     "Invalid register region for source 1. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.2:w r2:w r4.4<16;2,4>:w", SAME_ERROR, { .ge = 200 } },

   /* Source 1. Uniform stride W->B cases. */
   { "add (8) r0.2<2>:b r2:b r4:w",      VALID, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2:b r4.1:w",    VALID, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2:b r4<2>:w",
     "Invalid register region for source 1. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.2<2>:b r2:b r4.1<2>:w", SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2:b r4<4>:w",   SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.2<2>:b r2:b r4.1<4>:w", SAME_ERROR, { .ge = 200 } },

   /* Source 1. Dword aligned W->B cases. */
   { "add (8) r0.4<2>:b r2:w r4<8;4,1>:w",    VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0.4<2>:b r2:w r4.2<8;4,1>:w",  VALID, { .ge = 200, .lt = 350 } },
   { "add (8) r0.4<2>:b r2:w r4<2>:w",
     "Invalid register region for source 1. See special restrictions section.", { .ge = 200 }
   },
   { "add (8) r0.4<2>:b r2:w r4.2<2>:w",      SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.4<2>:b r2:w r4<16;2,4>:w",   SAME_ERROR, { .ge = 200 } },
   { "add (8) r0.4<2>:b r2:w r4.2<16;2,4>:w", SAME_ERROR, { .ge = 200 } },

   /* Xe3P register region encoding restrictions for Src1. */
   { "add (8) r3<4>:ud r3<4>:ud r4<1>:ud",
     "On Xe3P+, src1 and dst byte stride must match.", { .ge = 350 } },
   { "mad (8) r0<2>:f r1:f r2:f r3:f",    SAME_ERROR, { .ge = 350 } },
   { "add (16) r0:d r1:d r2.0<2>:d",      SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.3<4>:w r1:w r2:w",       SAME_ERROR, { .ge = 350 } },
   { "add (8) r0:d r1:d r2<16;4,2>:d",    SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.4:d r1:d r2<16;4,2>:d",  SAME_ERROR, { .ge = 350 } },
   { "add (16) r0:d r1:d r2<2>:d",        SAME_ERROR, { .ge = 350 } },
   { "add (4) r0.2:d r1:d r2<16;2,1>:d",  SAME_ERROR, { .ge = 350 } },
   { "add (16) r0:d r1:d r2:w",           SAME_ERROR, { .ge = 350 } },
   { "add (16) r0:d r1:w r2:w",           SAME_ERROR, { .ge = 350 } },
   { "add (16) r0<2>:w r1<0>:w r2:w",     SAME_ERROR, { .ge = 350 } },
   { "mul (8) r0:bf r1:bf r2:f",          SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.16:bf r1:bf r2:f",       SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.1:bf r1:bf r2:f",        SAME_ERROR, { .ge = 350 } },
   { "add (8) r0:bf r1.16:bf r2:f",       SAME_ERROR, { .ge = 350 } },
   { "mad (8) r0:f r1:f r2:bf r3:f",      SAME_ERROR, { .ge = 350 } },
   { "add (8) r0<2>:bf r1:f r2.16:bf",    SAME_ERROR, { .ge = 350 } },
   { "mul (8) r0 -r1 r2:uw",              SAME_ERROR, { .ge = 350 } },
   { "mul (8) null<2>:d r1<8;4,2>:d r2<8;4,2>:d", SAME_ERROR, { .ge = 350 } },
   { "add (8) r0:d r2:d r4:w",            SAME_ERROR, { .ge = 350 } },
   { "add (8) r0<2>:w r2:d r4:w",         SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.1:w r2:w r4.2<2>:w",     SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.2:w r2:w r4<8;4,1>:w",   SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.2:w r2:w r4.4<8;4,1>:w", SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.2:w r2:w r4.4<2>:w",     SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.4<2>:b r2:w r4<8;4,1>:w", SAME_ERROR, { .ge = 350 } },
   { "add (8) r0.4<2>:b r2:w r4.2<8;4,1>:w", SAME_ERROR, { .ge = 350 } },

};

static struct intel_device_info
get_platform(const char *name)
{
   struct intel_device_info devinfo = {};

   const int devid = intel_device_name_to_pci_device_id(name);
   EXPECT_NE(devid, -1) << "platform: " << name;
   if (devid == -1)
      return devinfo;

   EXPECT_TRUE(intel_get_device_info_for_build(devid, &devinfo))
      << "platform: " << name;

   return devinfo;
}

struct validation_platform {
   const char *name;
   struct intel_device_info devinfo;
};

static std::vector<struct validation_platform>
get_validation_platforms()
{
   unsigned num_platforms = 0;
   while (intel_platform_name_by_index(num_platforms) != NULL)
      num_platforms++;

   std::vector<struct validation_platform> platforms;
   platforms.reserve(num_platforms);

   for (unsigned i = 0; i < num_platforms; i++) {
      const char *name = intel_platform_name_by_index(i);

      struct intel_device_info devinfo = get_platform(name);

      if (devinfo.verx10 < MIN_GFX_VERX10 ||
          devinfo.verx10 > MAX_GFX_VERX10)
         continue;

      platforms.push_back({ name, devinfo });
   }

   return platforms;
}

TEST(gen_validate_test, validation_table)
{
   const std::vector<struct validation_platform> platforms =
      get_validation_platforms();

   ASSERT_FALSE(platforms.empty());

   void *mem_ctx = ralloc_context(NULL);
   const char *last_expected_pattern = VALID;

   std::map<std::string, std::set<std::string>> executed_tests;
   std::map<std::string, std::set<std::string>> redundant_tests;

   for (const auto &t : validation_tests) {
      const char *expected_pattern = t.expected_pattern;
      if (expected_pattern == SAME_ERROR) {
         ASSERT_NE(last_expected_pattern, VALID)
            << "SAME used without a previous validation error";
         expected_pattern = last_expected_pattern;
      } else {
         last_expected_pattern = expected_pattern;
      }

      unsigned platforms_targeted = 0;

      for (const auto &platform : platforms) {
         const struct intel_device_info *devinfo = &platform.devinfo;
         const unsigned verx10 = devinfo->verx10;

         if ((t.eq && verx10 != t.eq) ||
             (t.ge && verx10 < t.ge) ||
             (t.gt && verx10 <= t.gt) ||
             (t.le && verx10 > t.le) ||
             (t.lt && verx10 >= t.lt) ||
             (t.is_9lp && !intel_device_info_is_9lp(devinfo)) ||
             (t.is_not_9lp && intel_device_info_is_9lp(devinfo)) ||
             (t.has_bfloat16 && !devinfo->has_bfloat16) ||
             (t.has_64bit_float && !devinfo->has_64bit_float) ||
             (t.has_64bit_int && !devinfo->has_64bit_int) ||
             (t.has_lsc && !devinfo->has_lsc) ||
             (t.no_lsc && devinfo->has_lsc))
            continue;

         platforms_targeted++;

         const bool inserted = executed_tests[t.input].insert(platform.name).second;
         if (!inserted)
            redundant_tests[t.input].insert(platform.name);

         SCOPED_TRACE(::testing::Message() << "input: " << t.input);
         SCOPED_TRACE(::testing::Message() << "platform: " << platform.name);

         gen_parse_params pp = {
            .devinfo   = devinfo,
            .text      = t.input,
            .text_size = (int)strlen(t.input),
            .mem_ctx   = mem_ctx,
         };

         if (!gen_parse(&pp)) {
            ADD_FAILURE() << "Failed to parse input: " << t.input;
            continue;
         }

         gen_validate_params vp = {
            .devinfo   = devinfo,
            .insts     = pp.insts,
            .num_insts = pp.num_insts,
            .mem_ctx   = mem_ctx,
         };

         const bool valid = gen_validate(&vp);

         ::testing::Message validation_errors;
         bool found_msg = false;
         for (int j = 0; j < vp.num_errors; j++) {
            validation_errors << "\n  [" << vp.errors[j].index << "] "
                              << vp.errors[j].msg;

            if (expected_pattern &&
                strstr(vp.errors[j].msg, expected_pattern) != NULL)
               found_msg = true;
         }
         if (vp.num_errors == 0)
            validation_errors << "\n  <none>";

         if (expected_pattern == VALID) {
            EXPECT_TRUE(valid)
               << "Validation errors:" << validation_errors.GetString();
         } else {
            EXPECT_FALSE(valid);
            EXPECT_TRUE(found_msg)
               << "Expected validation error containing: " << expected_pattern
               << "\nValidation errors:" << validation_errors.GetString();
         }
      }

      EXPECT_GT(platforms_targeted, 0u)
         << "No platform was targeted by input: " << t.input;
   }

   if (!redundant_tests.empty()) {
      size_t redundant_test_count = 0;
      for (const auto &redundant : redundant_tests)
         redundant_test_count += redundant.second.size();

      fprintf(stderr,
              "WARNING: validation_table found %zu redundant platform/input pairs:\n",
              redundant_test_count);

      for (const auto &redundant : redundant_tests) {
         fprintf(stderr, "  input=\"%s\" platforms=", redundant.first.c_str());

         bool first = true;
         for (const auto &platform : redundant.second) {
            fprintf(stderr, "%s%s", first ? "" : ",", platform.c_str());
            first = false;
         }
         fprintf(stderr, "\n");
      }
   }

   ralloc_free(mem_ctx);
}

static bool
validate(const struct intel_device_info *devinfo, const gen_inst &inst)
{
   void *mem_ctx = ralloc_context(NULL);

   gen_validate_params params = {};
   params.devinfo = devinfo;
   params.insts = &inst;
   params.num_insts = 1;
   params.mem_ctx = mem_ctx;

   const bool valid = gen_validate(&params);
   ralloc_free(mem_ctx);

   return valid;
}

TEST(gen_validate_test, nop_does_not_require_exec_size)
{
   const struct intel_device_info devinfo = get_platform("tgl");

   gen_inst inst = {};
   inst.opcode = GEN_OP_NOP;
   inst.exec_size = 0;

   EXPECT_TRUE(validate(&devinfo, inst));
}

static gen_inst
make_mad(gen_reg_type type)
{
   gen_inst inst = {};
   inst.opcode = GEN_OP_MAD;
   inst.exec_size = 8;

   inst.dst.file = GEN_GRF;
   inst.dst.type = type;
   inst.dst.nr = 0;
   inst.dst.region.hstride = 1;

   for (unsigned i = 0; i < 3; i++) {
      inst.src[i].file = GEN_GRF;
      inst.src[i].type = type;
      inst.src[i].nr = 1 + i;
      inst.src[i].region.vstride = 8;
      inst.src[i].region.width = 8;
      inst.src[i].region.hstride = 1;
   }

   return inst;
}

TEST(gen_validate_test, align16_3src_subregister_encoding)
{
   const struct intel_device_info devinfo = get_platform("skl");
   ASSERT_EQ(devinfo.ver, 9);

   gen_inst inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   inst.dst.subnr = 4;
   EXPECT_TRUE(validate(&devinfo, inst));

   inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   inst.dst.subnr = 2;
   EXPECT_FALSE(validate(&devinfo, inst));

   inst = make_mad(GEN_TYPE_F);
   inst.align16 = true;
   inst.src[0].subnr = 31;
   EXPECT_FALSE(validate(&devinfo, inst));
}

TEST(gen_validate_test, align1_3src_subregister_encoding)
{
   const char *platforms[] = { "tgl", "lnl", "ptl" };

   for (const char *platform : platforms) {
      SCOPED_TRACE(::testing::Message() << "platform: " << platform);

      const struct intel_device_info devinfo = get_platform(platform);

      gen_inst inst = make_mad(GEN_TYPE_F);
      inst.dst.subnr = devinfo.ver >= 20 ? 62 : 31;
      EXPECT_TRUE(validate(&devinfo, inst));

      inst = make_mad(GEN_TYPE_F);
      inst.dst.subnr = devinfo.ver >= 20 ? 31 : 32;
      EXPECT_FALSE(validate(&devinfo, inst));

      inst = make_mad(GEN_TYPE_F);
      inst.src[0].subnr = devinfo.ver >= 20 ? 64 : 32;
      EXPECT_FALSE(validate(&devinfo, inst));
   }
}

TEST(gen_validate_test, dpas_sub_byte_precision)
{
   const char *platforms[] = { "dg2", "bmg", "ptl" };

   const struct {
      gen_reg_type dst_type;
      gen_reg_type src0_type;
      gen_reg_type src1_type;
      uint8_t src1_subbyte;
      gen_reg_type src2_type;
      uint8_t src2_subbyte;
      bool expected_result;
   } tests[] = {
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 0, GEN_TYPE_HF, 0, true  },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 0, GEN_TYPE_HF, 1, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 0, GEN_TYPE_HF, 2, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 1, GEN_TYPE_HF, 0, false },
      { GEN_TYPE_F,  GEN_TYPE_F,  GEN_TYPE_HF, 2, GEN_TYPE_HF, 0, false },

      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 0, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 1, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 2, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 0, GEN_TYPE_UB, 3, false },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 1, GEN_TYPE_UB, 0, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 2, GEN_TYPE_UB, 0, true  },
      { GEN_TYPE_UD, GEN_TYPE_UD, GEN_TYPE_UB, 3, GEN_TYPE_UB, 0, false },
   };

   for (const char *platform : platforms) {
      SCOPED_TRACE(::testing::Message() << "platform: " << platform);

      const struct intel_device_info devinfo = get_platform(platform);
      ASSERT_TRUE(devinfo.has_systolic);

      const uint8_t valid_exec_size = devinfo.ver >= 20 ? 16 : 8;

      for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
         const auto &t = tests[i];
         SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

         gen_inst inst = {
            .opcode = GEN_OP_DPAS,
            .exec_size = valid_exec_size,

            .dpas = {
               .sdepth = 8,
               .rcount = 8,
               .src1_subbyte = t.src1_subbyte,
               .src2_subbyte = t.src2_subbyte,
            },

            .dst = { .file = GEN_GRF, .type = t.dst_type, .region = { .hstride = 1 } },
            .src = {
               { .file = GEN_GRF, .type = t.src0_type,  .nr = 10 },
               { .file = GEN_GRF, .type = t.src1_type, .nr = 20 },
               { .file = GEN_GRF, .type = t.src2_type, .nr = 30 },
            },
         };

         EXPECT_EQ(t.expected_result, validate(&devinfo, inst));
      }
   }
}

TEST(gen_validate_test, dpas_src_subreg_nr_subbyte_encoding)
{
   const char *platforms[] = { "dg2", "bmg", "ptl" };

   for (const char *platform : platforms) {
      SCOPED_TRACE(::testing::Message() << "platform: " << platform);

      const struct intel_device_info devinfo = get_platform(platform);
      ASSERT_TRUE(devinfo.has_systolic);

      const uint8_t valid_exec_size = devinfo.ver >= 20 ? 16 : 8;

      const struct {
         uint8_t src2_subnr;
         uint8_t src2_subbyte;
         bool expected_result;
      } tests[] = {
         { 16, 1, true  },
         {  8, 1, false },
         { devinfo.grf_size, 1, false },
      };

      for (unsigned i = 0; i < ARRAY_SIZE(tests); i++) {
         const auto &t = tests[i];
         SCOPED_TRACE(::testing::Message() << "test vector index = " << i);

         gen_inst inst = {
            .opcode = GEN_OP_DPAS,
            .exec_size = valid_exec_size,

            .dpas = {
               .sdepth = 8,
               .rcount = 8,
               .src2_subbyte = t.src2_subbyte,
            },

            .dst = { .file = GEN_GRF, .type = GEN_TYPE_UD, .region = { .hstride = 1 } },
            .src = {
               { .file = GEN_GRF, .type = GEN_TYPE_UD, .nr = 10 },
               { .file = GEN_GRF, .type = GEN_TYPE_UB, .nr = 20 },
               { .file = GEN_GRF, .type = GEN_TYPE_UB, .subnr = t.src2_subnr, .nr = 30 },
            },
         };

         EXPECT_EQ(t.expected_result, validate(&devinfo, inst));
      }
   }
}

TEST(gen_validate_test, bfn_saturate_invalid)
{
   const char *platforms[] = { "tgl", "bmg", "ptl" };

   for (const char *platform : platforms) {
      SCOPED_TRACE(::testing::Message() << "platform: " << platform);

      const struct intel_device_info devinfo = get_platform(platform);

      gen_inst inst = make_mad(GEN_TYPE_UD);
      inst.opcode = GEN_OP_BFN;
      inst.saturate = true;
      EXPECT_FALSE(validate(&devinfo, inst));
   }
}

TEST(gen_validate_test, split_send_payload_overlap_one_sided_descriptor_fallback_register_descriptor_pre_gfx12)
{
   const char *platforms[] = { "skl", "icl" };

   for (const char *platform : platforms) {
      SCOPED_TRACE(::testing::Message() << "platform: " << platform);

      const struct intel_device_info devinfo = get_platform(platform);
      ASSERT_LT(devinfo.ver, 12);

      gen_inst inst = {
         .opcode = GEN_OP_SENDS,
         .exec_size = 16,
         .send = {
            .desc_is_reg = true,
            .src1_len = 2,
         },
         .dst = gen_retype(gen_null(), GEN_TYPE_UD),
         .src = {
            { .file = GEN_GRF, .type = GEN_TYPE_UD, .nr = 5 },
            { .file = GEN_GRF, .type = GEN_TYPE_UD, .nr = 4 },
         },

      };
      EXPECT_FALSE(validate(&devinfo, inst));

      inst.src[0].nr = 6;
      EXPECT_TRUE(validate(&devinfo, inst));
   }
}

TEST(gen_validate_test, xe2_register_region_special_restrictions_for_3src_src0_and_src1)
{
   const char *platforms[] = { "lnl", "bmg", "ptl" };

   for (const char *platform : platforms) {
      SCOPED_TRACE(::testing::Message() << "platform: " << platform);

      const struct intel_device_info devinfo = get_platform(platform);
      ASSERT_GE(devinfo.verx10, 200);

      gen_inst inst = make_mad(GEN_TYPE_W);
      inst.dst.type = GEN_TYPE_W;
      inst.dst.subnr = 0;
      inst.dst.region.hstride = 1;

      for (unsigned i = 0; i < 3; i++) {
         inst.src[i].type = GEN_TYPE_W;
         inst.src[i].region.vstride = 2;
         inst.src[i].region.width = 1;
         inst.src[i].region.hstride = 0;
         inst.src[i].subnr = 0;
      }

      EXPECT_TRUE(validate(&devinfo, inst));

      inst = make_mad(GEN_TYPE_W);
      inst.dst.type = GEN_TYPE_W;
      inst.dst.subnr = 0;
      inst.dst.region.hstride = 1;
      for (unsigned i = 0; i < 3; i++) {
         inst.src[i].type = GEN_TYPE_W;
         inst.src[i].region.vstride = 2;
         inst.src[i].region.width = 1;
         inst.src[i].region.hstride = 0;
         inst.src[i].subnr = 0;
      }
      inst.src[0].subnr = 4;
      EXPECT_FALSE(validate(&devinfo, inst));

      inst = make_mad(GEN_TYPE_W);
      inst.dst.type = GEN_TYPE_W;
      inst.dst.subnr = 0;
      inst.dst.region.hstride = 1;
      for (unsigned i = 0; i < 3; i++) {
         inst.src[i].type = GEN_TYPE_W;
         inst.src[i].region.vstride = 2;
         inst.src[i].region.width = 1;
         inst.src[i].region.hstride = 0;
         inst.src[i].subnr = 0;
      }
      inst.src[1].subnr = 4;
      EXPECT_FALSE(validate(&devinfo, inst));
   }
}

