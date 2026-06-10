/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "gen_private.h"

#include <gtest/gtest.h>

#include <cstring>

static intel_device_info
get_devinfo(const char *name)
{
   intel_device_info devinfo;
   memset(&devinfo, 0, sizeof(devinfo));

   const int devid = intel_device_name_to_pci_device_id(name);
   EXPECT_NE(devid, -1);
   EXPECT_TRUE(intel_get_device_info_from_pci_id(devid, &devinfo));
   return devinfo;
}

TEST(GenEncoding, RawLayoutTracksOffsetsAndCompaction)
{
   const uint64_t raw[] = {
      BITFIELD_BIT(29),
      0,
      0,
   };
   gen_inst_layout layouts[3] = {};

   gen_scan_raw_layout_params params = {
      .raw_bytes = raw,
      .raw_bytes_size = (int)sizeof(raw),
      .layouts = layouts,
      .num_insts = 3,
   };

   ASSERT_TRUE(gen_scan_raw_layout(&params));
   ASSERT_EQ(params.num_insts, 2);
   EXPECT_EQ(params.end_offset, 24);
   EXPECT_EQ(layouts[0].offset, 0);
   EXPECT_EQ(layouts[1].offset, 8);
   EXPECT_TRUE(layouts[0].was_compacted);
   EXPECT_FALSE(layouts[1].was_compacted);
}

TEST(GenEncoding, RawLayoutStopsBeforeIncompleteInstruction)
{
   const uint64_t raw[] = {
      BITFIELD_BIT(29),
      0,
   };
   gen_inst_layout layouts[2] = {};

   gen_scan_raw_layout_params params = {
      .raw_bytes = raw,
      .raw_bytes_size = (int)sizeof(raw),
      .layouts = layouts,
      .num_insts = 2,
   };

   ASSERT_TRUE(gen_scan_raw_layout(&params));
   ASSERT_EQ(params.num_insts, 1);
   EXPECT_EQ(params.end_offset, 8);
   EXPECT_EQ(layouts[0].offset, 0);
   EXPECT_TRUE(layouts[0].was_compacted);
}

TEST(GenEncoding, RawLayoutReturnsCountWhenArraysTooSmall)
{
   const uint64_t raw[] = {
      BITFIELD_BIT(29),
      0,
      0,
   };

   gen_scan_raw_layout_params params = {
      .raw_bytes = raw,
      .raw_bytes_size = (int)sizeof(raw),
      .num_insts = 1,
   };

   ASSERT_FALSE(gen_scan_raw_layout(&params));
}

TEST(GenEncoding, SwsbEncodeDecodeRoundTrip)
{
   const struct {
      const char *gfx;
      gen_opcode op;
      bool is_unordered;
      gen_swsb swsb;
   } tests[] = {
      { "tgl", GEN_OP_ADD,  false, { 1, GEN_PIPE_NONE,   0, GEN_SBID_NULL } },
      { "dg2", GEN_OP_ADD,  false, { 2, GEN_PIPE_FLOAT,  0, GEN_SBID_NULL } },
      { "dg2", GEN_OP_ADD,  false, { 3, GEN_PIPE_INT,    0, GEN_SBID_NULL } },
      { "dg2", GEN_OP_ADD,  false, { 4, GEN_PIPE_ALL,    0, GEN_SBID_NULL } },
      { "tgl", GEN_OP_ADD,  false, { 3, GEN_PIPE_NONE,   5, GEN_SBID_DST } },
      { "tgl", GEN_OP_SEND, true,  { 4, GEN_PIPE_NONE,   7, GEN_SBID_SET } },
      { "tgl", GEN_OP_ADD,  false, { 0, GEN_PIPE_NONE,   2, GEN_SBID_DST } },
      { "tgl", GEN_OP_ADD,  false, { 0, GEN_PIPE_NONE,   3, GEN_SBID_SRC } },
      { "tgl", GEN_OP_SEND, true,  { 0, GEN_PIPE_NONE,   4, GEN_SBID_SET } },
      { "bmg", GEN_OP_ADD,  false, { 5, GEN_PIPE_LONG,   0, GEN_SBID_NULL } },
      { "bmg", GEN_OP_ADD,  false, { 6, GEN_PIPE_MATH,   0, GEN_SBID_NULL } },
      { "bmg", GEN_OP_SEND, true,  { 1, GEN_PIPE_INT,   11, GEN_SBID_SET } },
      { "bmg", GEN_OP_SEND, true,  { 2, GEN_PIPE_FLOAT, 10, GEN_SBID_SET } },
      { "bmg", GEN_OP_SEND, true,  { 3, GEN_PIPE_ALL,    9, GEN_SBID_SET } },
      { "bmg", GEN_OP_DPAS, true,  { 4, GEN_PIPE_NONE,  12, GEN_SBID_SET } },
      { "bmg", GEN_OP_DPAS, true,  { 5, GEN_PIPE_NONE,  13, GEN_SBID_SRC } },
      { "bmg", GEN_OP_DPAS, true,  { 6, GEN_PIPE_NONE,  14, GEN_SBID_DST } },
      { "bmg", GEN_OP_ADD,  false, { 1, GEN_PIPE_ALL,   17, GEN_SBID_DST } },
      { "lnl", GEN_OP_MOV,  false, { 1, GEN_PIPE_NONE,   1, GEN_SBID_DST } },
      { "bmg", GEN_OP_CMP,  false, { 1, GEN_PIPE_NONE,   0, GEN_SBID_DST } },
      { "bmg", GEN_OP_ADD,  false, { 2, GEN_PIPE_NONE,  16, GEN_SBID_DST } },
      { "bmg", GEN_OP_ADD,  false, { 3, GEN_PIPE_NONE,  15, GEN_SBID_SRC } },
      { "bmg", GEN_OP_ADD,  false, { 0, GEN_PIPE_NONE,  18, GEN_SBID_DST } },
      { "bmg", GEN_OP_ADD,  false, { 0, GEN_PIPE_NONE,  19, GEN_SBID_SRC } },
      { "bmg", GEN_OP_SEND, true,  { 0, GEN_PIPE_NONE,  20, GEN_SBID_SET } },
      { "ptl", GEN_OP_ADD,  false, { 5, GEN_PIPE_LONG,   0, GEN_SBID_NULL } },
      { "ptl", GEN_OP_ADD,  false, { 6, GEN_PIPE_MATH,   0, GEN_SBID_NULL } },
      { "ptl", GEN_OP_MOV,  false, { 7, GEN_PIPE_SCALAR, 0, GEN_SBID_NULL } },
      { "ptl", GEN_OP_SEND, true,  { 1, GEN_PIPE_INT,   11, GEN_SBID_SET } },
      { "ptl", GEN_OP_SEND, true,  { 2, GEN_PIPE_FLOAT, 10, GEN_SBID_SET } },
      { "ptl", GEN_OP_SEND, true,  { 3, GEN_PIPE_ALL,    9, GEN_SBID_SET } },
      { "ptl", GEN_OP_DPAS, true,  { 4, GEN_PIPE_NONE,  12, GEN_SBID_SET } },
      { "ptl", GEN_OP_DPAS, true,  { 5, GEN_PIPE_NONE,  13, GEN_SBID_SRC } },
      { "ptl", GEN_OP_DPAS, true,  { 6, GEN_PIPE_NONE,  14, GEN_SBID_DST } },
      { "ptl", GEN_OP_ADD,  false, { 1, GEN_PIPE_ALL,   17, GEN_SBID_DST } },
      { "ptl", GEN_OP_ADD,  false, { 2, GEN_PIPE_NONE,  16, GEN_SBID_DST } },
      { "ptl", GEN_OP_ADD,  false, { 3, GEN_PIPE_NONE,  15, GEN_SBID_SRC } },
      { "ptl", GEN_OP_ADD,  false, { 0, GEN_PIPE_NONE,  18, GEN_SBID_DST } },
      { "ptl", GEN_OP_ADD,  false, { 0, GEN_PIPE_NONE,  19, GEN_SBID_SRC } },
      { "ptl", GEN_OP_SEND, true,  { 0, GEN_PIPE_NONE,  20, GEN_SBID_SET } },
   };

   for (const auto &test : tests) {
      SCOPED_TRACE(::testing::Message()
                   << "gfx=" << test.gfx
                   << " op=" << test.op
                   << " unordered=" << test.is_unordered);

      const intel_device_info devinfo = get_devinfo(test.gfx);

      gen_inst inst = {};
      inst.opcode = test.op;
      inst.swsb = test.swsb;

      const uint32_t encoded = gen_swsb_encode(&devinfo, inst.swsb, inst.opcode);
      const gen_swsb decoded =
         gen_swsb_decode(&devinfo, test.is_unordered, encoded, inst.opcode);

      EXPECT_EQ(inst.swsb.regdist, decoded.regdist);
      EXPECT_EQ(inst.swsb.pipe,    decoded.pipe);
      EXPECT_EQ(inst.swsb.sbid,    decoded.sbid);
      EXPECT_EQ(inst.swsb.mode,    decoded.mode);
   }
}
