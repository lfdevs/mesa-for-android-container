/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include "helpers.h"

class drirc : public radv_test {};

TEST_F(drirc, override_uniform_offset_alignment)
{
   create_device();

   VkPhysicalDeviceProperties2 pdev_props = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };

   get_physical_device_properties2(&pdev_props);
   const uint64_t min_uniform_offset_alignment = pdev_props.properties.limits.minUniformBufferOffsetAlignment;

   destroy_device();

   add_envvar("radv_override_uniform_offset_alignment", "16");

   create_device();

   get_physical_device_properties2(&pdev_props);
   const uint64_t min_uniform_offset_alignment_override = pdev_props.properties.limits.minUniformBufferOffsetAlignment;

   EXPECT_TRUE(min_uniform_offset_alignment != min_uniform_offset_alignment_override);
   EXPECT_TRUE(min_uniform_offset_alignment_override == 16);
   destroy_device();
}

TEST_F(drirc, disable_depth_storage)
{
   create_device();

   const VkFormatFeatureFlags2 storage_features = VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                                                  VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                                                  VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;

   VkFormatProperties2 format_props = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
   };

   get_physical_device_format_properties2(VK_FORMAT_D32_SFLOAT, &format_props);
   const VkFormatFeatureFlags2 tiled_storage_features = format_props.formatProperties.optimalTilingFeatures;

   EXPECT_TRUE(tiled_storage_features & storage_features);

   destroy_device();

   add_envvar("radv_disable_depth_storage", "true");

   create_device();

   get_physical_device_format_properties2(VK_FORMAT_D32_SFLOAT, &format_props);
   const VkFormatFeatureFlags2 tiled_storage_features_override = format_props.formatProperties.optimalTilingFeatures;

   EXPECT_FALSE(tiled_storage_features_override & storage_features);
   destroy_device();
}

TEST_F(drirc, override_compute_shader_version)
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

   VkPipelineBinaryKeyKHR pipeline_keys[3];

   /* Create a simple compute pipeline to get the pipeline key. */
   get_pipeline_key(ARRAY_SIZE(code), (uint32_t *)code, &pipeline_keys[0],
                    VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR);

   /* Verify that re-creating the exact same pipeline returns the same pipeline key. */
   get_pipeline_key(ARRAY_SIZE(code), (uint32_t *)code, &pipeline_keys[1],
                    VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR);
   EXPECT_EQ(pipeline_keys[0].keySize, pipeline_keys[1].keySize);
   EXPECT_FALSE(memcmp(pipeline_keys[0].key, pipeline_keys[1].key, pipeline_keys[0].keySize));

   destroy_device();

   add_envvar("radv_override_compute_shader_version", "1");

   create_device();

   /* Verify that overwriting the compute pipeline version returns a different key. */
   get_pipeline_key(ARRAY_SIZE(code), (uint32_t *)code, &pipeline_keys[2],
                    VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR);
   EXPECT_EQ(pipeline_keys[1].keySize, pipeline_keys[2].keySize);
   EXPECT_TRUE(memcmp(pipeline_keys[1].key, pipeline_keys[2].key, pipeline_keys[1].keySize));

   destroy_device();
}
