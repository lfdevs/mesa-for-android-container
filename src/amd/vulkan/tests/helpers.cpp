/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "helpers.h"
#include "util/macros.h"

extern "C" {
PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);
}

radv_test::radv_test()
{
}

radv_test::~radv_test()
{
   assert(envvars.size() == 0);
}

void
radv_test::create_device()
{
   VkResult result;

   /* Create instance. */
   VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "radv_tests",
      .apiVersion = VK_API_VERSION_1_4,
   };

   VkInstanceCreateInfo instance_create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
   };

   result = ((PFN_vkCreateInstance)vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance"))(&instance_create_info, NULL,
                                                                                        &instance);
   assert(result == VK_SUCCESS);

#define ITEM(n) n = (PFN_vk##n)vk_icdGetInstanceProcAddr(instance, "vk" #n);
   FUNCTION_LIST
#undef ITEM

   /* Get physical device. */
   uint32_t device_count = 1;

   result = EnumeratePhysicalDevices(instance, &device_count, &physical_device);
   assert(result == VK_SUCCESS);

   /* Create logical device. */
   static const char *extensions[] = {"VK_KHR_pipeline_executable_properties"};

   VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .enabledExtensionCount = ARRAY_SIZE(extensions),
      .ppEnabledExtensionNames = extensions,
   };

   result = CreateDevice(physical_device, &device_create_info, NULL, &device);
   assert(result == VK_SUCCESS);
}

void
radv_test::destroy_device()
{
   unset_envvars();

   DestroyDevice(device, NULL);
   DestroyInstance(instance, NULL);
}

void
radv_test::get_physical_device_properties2(VkPhysicalDeviceProperties2 *pdev_props)
{
   GetPhysicalDeviceProperties2(physical_device, pdev_props);
}

void
radv_test::get_physical_device_format_properties2(VkFormat format, VkFormatProperties2 *format_props)
{
   GetPhysicalDeviceFormatProperties2(physical_device, format, format_props);
}

bool
radv_test::is_dedicated_sparse_queue_enabled()
{
   bool found_dedicated_sparse_queue = false;
   uint32_t num_queue_family_props = 0;

   GetPhysicalDeviceQueueFamilyProperties2(physical_device, &num_queue_family_props, NULL);
   if (num_queue_family_props > 0) {
      VkQueueFamilyProperties2 *queue_family_props = NULL;

      queue_family_props = (VkQueueFamilyProperties2 *)calloc(num_queue_family_props, sizeof(*queue_family_props));
      assert(queue_family_props);

      GetPhysicalDeviceQueueFamilyProperties2(physical_device, &num_queue_family_props, queue_family_props);

      for (uint32_t i = 0; i < num_queue_family_props; i++) {
         VkQueueFamilyProperties2 *queue_family_prop = &queue_family_props[i];
         if (queue_family_prop->queueFamilyProperties.queueFlags == VK_QUEUE_SPARSE_BINDING_BIT)
            found_dedicated_sparse_queue = true;
      }

      free(queue_family_props);
   }

   return found_dedicated_sparse_queue;
}

void
radv_test::create_compute_pipeline(uint32_t code_size, const uint32_t *code, VkPipelineCreateFlags flags)
{
   VkResult result;

   VkShaderModuleCreateInfo shader_module_create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code_size,
      .pCode = code,
   };
   VkShaderModule shader_module;

   result = CreateShaderModule(device, &shader_module_create_info, NULL, &shader_module);
   assert(result == VK_SUCCESS);

   VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
   };

   result = CreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout);
   assert(result == VK_SUCCESS);

   VkPipelineShaderStageCreateInfo stage_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = shader_module,
      .pName = "main",
   };

   VkComputePipelineCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .flags = flags,
      .stage = stage_create_info,
      .layout = pipeline_layout,
   };

   result = CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &pipeline);
   assert(result == VK_SUCCESS);

   DestroyShaderModule(device, shader_module, NULL);
}

void
radv_test::destroy_pipeline()
{
   DestroyPipelineLayout(device, pipeline_layout, NULL);
   DestroyPipeline(device, pipeline, NULL);
}

uint64_t
radv_test::get_pipeline_hash(VkShaderStageFlags stage)
{
   VkResult result;

   uint32_t executable_count = 16;
   VkPipelineExecutablePropertiesKHR executables[16];
   VkPipelineInfoKHR pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
      .pipeline = pipeline,
   };

   result = GetPipelineExecutablePropertiesKHR(device, &pipeline_info, &executable_count, executables);
   assert(result == VK_SUCCESS);

   uint32_t executable = 0;
   for (; executable < executable_count; executable++) {
      if (executables[executable].stages == stage)
         break;
   }
   assert(executable != executable_count);

   uint32_t stat_count = 32;
   VkPipelineExecutableStatisticKHR stats[32];
   VkPipelineExecutableInfoKHR exec_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
      .pipeline = pipeline,
      .executableIndex = executable,
   };

   result = GetPipelineExecutableStatisticsKHR(device, &exec_info, &stat_count, stats);
   assert(result == VK_SUCCESS);

   for (uint32_t i = 0; i < stat_count; i++) {
      if (!strcmp(stats[i].name, "Driver pipeline hash"))
         return stats[i].value.u64;
   }

   UNREACHABLE("Driver pipeline hash not found");
}

void
radv_test::get_pipeline_key(uint32_t code_size, const uint32_t *code, VkPipelineBinaryKeyKHR *pipeline_key,
                            VkPipelineCreateFlags flags)
{
   VkResult result;

   VkShaderModuleCreateInfo shader_module_create_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code_size,
      .pCode = code,
   };
   VkShaderModule shader_module;

   result = CreateShaderModule(device, &shader_module_create_info, NULL, &shader_module);
   assert(result == VK_SUCCESS);

   VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
   };

   result = CreatePipelineLayout(device, &pipeline_layout_info, NULL, &pipeline_layout);
   assert(result == VK_SUCCESS);

   VkPipelineShaderStageCreateInfo stage_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = shader_module,
      .pName = "main",
   };

   VkComputePipelineCreateInfo compute_pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .flags = flags,
      .stage = stage_create_info,
      .layout = pipeline_layout,
   };

   VkPipelineCreateInfoKHR pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_INFO_KHR,
      .pNext = &compute_pipeline_create_info,
   };

   result = GetPipelineKeyKHR(device, &pipeline_create_info, pipeline_key);
   assert(result == VK_SUCCESS);

   DestroyPipelineLayout(device, pipeline_layout, NULL);
   DestroyShaderModule(device, shader_module, NULL);
}
