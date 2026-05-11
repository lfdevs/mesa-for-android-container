/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include "helpers.h"

class misc : public radv_test {};

/**
 * This test verifies that the pipeline cache UUID is invariant when random debug options or
 * workarounds are applied. This is very important for SteamOS precompilation.
 */
TEST_F(misc, invariant_pipeline_cache_uuid)
{
   create_device();

   VkPhysicalDeviceProperties2 pdev_props_default = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };

   get_physical_device_properties2(&pdev_props_default);

   const uint8_t *uuid_default = pdev_props_default.properties.pipelineCacheUUID;

   destroy_device();

   add_envvar("vk_lower_terminate_to_discard", "true");
   add_envvar("radv_disable_shrink_image_store", "true");
   add_envvar("RADV_DEBUG", "cswave32");

   create_device();

   VkPhysicalDeviceProperties2 pdev_props_override = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };

   get_physical_device_properties2(&pdev_props_override);

   const uint8_t *uuid_override = pdev_props_override.properties.pipelineCacheUUID;

   EXPECT_TRUE(!memcmp(uuid_default, uuid_override, VK_UUID_SIZE));

   destroy_device();
}

/**
 * This test verifies that the pipeline key returned when shader stats are captured (eg. Fossilize)
 * matches the pipeline key returned when RGP is enabled.
 */
TEST_F(misc, pipeline_key_rgp_fossilize)
{

   create_device();

   /*
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint GLCompute %main "main"
               OpExecutionMode %main LocalSize 1 1 1
               OpSource GLSL 460
               OpName %main "main"
               OpDecorate %gl_WorkGroupSize BuiltIn WorkgroupSize
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
       %uint = OpTypeInt 32 0
     %v3uint = OpTypeVector %uint 3
     %uint_1 = OpConstant %uint 1
%gl_WorkGroupSize = OpConstantComposite %v3uint %uint_1 %uint_1 %uint_1
       %main = OpFunction %void None %3
          %5 = OpLabel
               OpReturn
               OpFunctionEnd
   */
   unsigned char code[] = {
      0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x08, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x11, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x47, 0x4c,
      0x53, 0x4c, 0x2e, 0x73, 0x74, 0x64, 0x2e, 0x34, 0x35, 0x30, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
      0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x06, 0x00, 0x04, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
      0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00,
      0x00, 0x00, 0xcc, 0x01, 0x00, 0x00, 0x05, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x6d, 0x61, 0x69, 0x6e, 0x00,
      0x00, 0x00, 0x00, 0x47, 0x00, 0x04, 0x00, 0x09, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x19, 0x00, 0x00, 0x00,
      0x13, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00, 0x21, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
      0x00, 0x15, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00,
      0x04, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x2b, 0x00, 0x04, 0x00, 0x06,
      0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x06, 0x00, 0x07, 0x00, 0x00, 0x00,
      0x09, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x36, 0x00, 0x05,
      0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xf8, 0x00,
      0x02, 0x00, 0x05, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x01, 0x00, 0x38, 0x00, 0x01, 0x00};

   VkPipelineBinaryKeyKHR pipeline_keys[2];

   /* Get the pipeline key for a simple compute pipeline that captures shader statistics (like Fossilize). */
   get_pipeline_key(ARRAY_SIZE(code), (uint32_t *)code, &pipeline_keys[0],
                    VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR);

   destroy_device();

   /* RADV_THREAD_TRACE_CACHE_COUNTERS needs to be disabled with noop device. */
   add_envvar("RADV_THREAD_TRACE_CACHE_COUNTERS", "false");
   add_envvar("MESA_VK_TRACE", "rgp");

   create_device();

   /* Verify the pipeline keys match. */
   get_pipeline_key(ARRAY_SIZE(code), (uint32_t *)code, &pipeline_keys[1]);
   EXPECT_EQ(pipeline_keys[0].keySize, pipeline_keys[1].keySize);
   EXPECT_FALSE(memcmp(pipeline_keys[0].key, pipeline_keys[1].key, pipeline_keys[0].keySize));

   destroy_device();
}

/**
 * This test verifies the compatibility between global pipeline keys. These keys are computed from
 * the device cache hash which is used to share shader binaries between different compatible GPUs.
 */
TEST_F(misc, global_pipeline_key_compat)
{
   /* RDNA2 keys */
   VkPipelineBinaryKeyKHR vangogh, rembrandt, navi21;
   get_global_pipeline_key(CHIP_VANGOGH, &vangogh);
   get_global_pipeline_key(CHIP_REMBRANDT, &rembrandt);
   get_global_pipeline_key(CHIP_NAVI21, &navi21);

   /* Verify that global keys between VANGOGH and REMBRANDT are compatible. */
   EXPECT_EQ(vangogh.keySize, rembrandt.keySize);
   EXPECT_FALSE(memcmp(vangogh.key, rembrandt.key, vangogh.keySize));

   /* Verify that global keys between VANGOGH and NAVI21 aren't compatible. */
   EXPECT_EQ(vangogh.keySize, navi21.keySize);
   EXPECT_TRUE(memcmp(vangogh.key, navi21.key, vangogh.keySize));

   /* RDNA3 keys */
   VkPipelineBinaryKeyKHR phoenix, phoenix2, navi33, navi31;
   get_global_pipeline_key(CHIP_PHOENIX, &phoenix);
   get_global_pipeline_key(CHIP_PHOENIX2, &phoenix2);
   get_global_pipeline_key(CHIP_NAVI33, &navi33);
   get_global_pipeline_key(CHIP_NAVI31, &navi31);

   /* Verify that global keys between PHOENIX, PHOENIX2 and NAVI33 are compatible. */
   EXPECT_EQ(phoenix.keySize, phoenix2.keySize);
   EXPECT_EQ(phoenix2.keySize, navi33.keySize);
   EXPECT_FALSE(memcmp(phoenix.key, phoenix2.key, phoenix.keySize));
   EXPECT_FALSE(memcmp(phoenix2.key, navi33.key, phoenix2.keySize));

   /* Verify that global keys between NAVI33 and NAVI31 aren't compatible. */
   EXPECT_EQ(navi33.keySize, navi31.keySize);
   EXPECT_TRUE(memcmp(navi33.key, navi31.key, navi33.keySize));
}
