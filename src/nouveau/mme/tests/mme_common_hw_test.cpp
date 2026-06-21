/*
 * Copyright 2026 Valve Corporation.
 * SPDX-License-Identifier: MIT
 */
#include "mme_runner.h"
#include "nv_push_clc597.h"

class mme_common_hw_test : public ::testing::Test, public mme_hw_runner {
public:
   mme_common_hw_test();
   ~mme_common_hw_test();

   void SetUp();
};

mme_common_hw_test::mme_common_hw_test() :
   ::testing::Test(),
   mme_hw_runner()
{ }

mme_common_hw_test::~mme_common_hw_test()
{ }

void
mme_common_hw_test::SetUp()
{
   ASSERT_TRUE(set_up_hw(FERMI_A, UINT16_MAX));
}

/* This test exist to prove the following assumption on MME Shadow RAM control modes:
 * - "TRACK" will compare the value in shadow RAM with the value provided,
 *   perform the method write only if it mismatch and update the shadow RAM
 *   value.
 * - "PASSTHROUGH" will directly update the method and bypass the MME shadow RAM
 *   entirely (nothing is tracked or checked against)
 * - "REPLAY" pull the value from the shadow RAM and ignore the value provided.
 */
TEST_F(mme_common_hw_test, mme_shadow_ram_control_behaviors)
{
   /* We build a very simple macro to read the value of NV9097_SET_POINT_SIZE */
   mme_builder b;
   mme_builder_init(&b, devinfo);
   mme_value offset = mme_load(&b);
   struct mme_value64 addr = mme_mov64(&b, mme_imm64(data_addr));
   mme_add64_to(&b, addr, addr, mme_value64(offset, mme_zero()));
   struct mme_value scratch = mme_state(&b, NV9097_SET_POINT_SIZE);
   mme_store(&b, addr, scratch, false);
   auto macro = mme_builder_finish_vec(&b);

   reset_push();
   push_macro(0, macro);

   /* First verify that "TRACK" and "TRACK_WITH_FILTER" store a copy in shadow RAM */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK);
   P_IMMD(p, NV9097, SET_POINT_SIZE, 0);
   P_1INC(p, NV9097, CALL_MME_MACRO(0));
   P_INLINE_DATA(p, 0x00);

   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK_WITH_FILTER);
   P_IMMD(p, NV9097, SET_POINT_SIZE, 1);
   P_1INC(p, NV9097, CALL_MME_MACRO(0));
   P_INLINE_DATA(p, 0x04);

   /* Verify that "PASSTHROUGH" bypass the shadow RAM */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_PASSTHROUGH);
   P_IMMD(p, NV9097, SET_POINT_SIZE, 2);
   P_1INC(p, NV9097, CALL_MME_MACRO(0));
   P_INLINE_DATA(p, 0x08);

   /* Verify that "REPLAY" does not touch the value in shadow RAM and read it */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_REPLAY);
   P_IMMD(p, NV9097, SET_POINT_SIZE, 3);
   /* Ensure to restore the control state here to avoid any issues */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK_WITH_FILTER);
   P_1INC(p, NV9097, CALL_MME_MACRO(0));
   P_INLINE_DATA(p, 0x0C);

   /* Now we still need to prove a few things and will use a report semaphore for it */

   uint64_t report_addr = data_addr + 4 * sizeof(uint32_t);

   /* First do a sanity semaphore write */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK_WITH_FILTER);
   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
   P_NV9097_SET_REPORT_SEMAPHORE_A(p, high32(report_addr));
   P_NV9097_SET_REPORT_SEMAPHORE_B(p, low32(report_addr));
   P_NV9097_SET_REPORT_SEMAPHORE_C(p, 4);
   P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
      .operation = OPERATION_RELEASE,
      .release = RELEASE_AFTER_ALL_PRECEEDING_WRITES_COMPLETE,
      .pipeline_location = PIPELINE_LOCATION_ALL,
      .structure_size = STRUCTURE_SIZE_ONE_WORD,
   });
   report_addr += sizeof(uint32_t);

   /* Now we test "PASSTHROUGH" actually perform the operation */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_PASSTHROUGH);
   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
   P_NV9097_SET_REPORT_SEMAPHORE_A(p, high32(report_addr));
   P_NV9097_SET_REPORT_SEMAPHORE_B(p, low32(report_addr));
   P_NV9097_SET_REPORT_SEMAPHORE_C(p, 5);
   P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
      .operation = OPERATION_RELEASE,
      .release = RELEASE_AFTER_ALL_PRECEEDING_WRITES_COMPLETE,
      .pipeline_location = PIPELINE_LOCATION_ALL,
      .structure_size = STRUCTURE_SIZE_ONE_WORD,
   });
   report_addr += sizeof(uint32_t);

   /* At this point we have NV9097_SET_REPORT_SEMAPHORE_C=4 in MME Shadow RAM.
    * We are now going to prove that "REPLAY" will use values in Shadow RAM.
    *
    * Instead of proving it with NV9097_SET_REPORT_SEMAPHORE_C=4, we are going
    * to use the semaphore address as on pre-Pascal "REPLAY" on
    * NV9097_SET_REPORT_SEMAPHORE is quite buggy and appear to also not properly
    * respect "PASSTHROUGH" mode.
    */

   /* First we write the address we want */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK);
   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
   P_NV9097_SET_REPORT_SEMAPHORE_A(p, high32(report_addr));
   P_NV9097_SET_REPORT_SEMAPHORE_B(p, low32(report_addr));
   report_addr += sizeof(uint32_t);

   /* Now we replay the address with something different than before */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_REPLAY);
   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
   P_NV9097_SET_REPORT_SEMAPHORE_A(p, high32(report_addr));
   P_NV9097_SET_REPORT_SEMAPHORE_B(p, low32(report_addr));

   /* Now trigger the actual semaphore write with the value we want  */
   P_IMMD(p, NV9097, SET_MME_SHADOW_RAM_CONTROL, MODE_METHOD_TRACK);
   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_C);
   P_NV9097_SET_REPORT_SEMAPHORE_C(p, 6);
   P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
      .operation = OPERATION_RELEASE,
      .release = RELEASE_AFTER_ALL_PRECEEDING_WRITES_COMPLETE,
      .pipeline_location = PIPELINE_LOCATION_ALL,
      .structure_size = STRUCTURE_SIZE_ONE_WORD,
   });
   report_addr += sizeof(uint32_t);

   /* Submit and verify the values */
   submit_push();

   ASSERT_EQ(data[0], 0);
   ASSERT_EQ(data[1], 1);
   ASSERT_EQ(data[2], 1);
   ASSERT_EQ(data[3], 1);
   ASSERT_EQ(data[4], 4);
   ASSERT_EQ(data[5], 5);
   ASSERT_EQ(data[6], 6);
}
