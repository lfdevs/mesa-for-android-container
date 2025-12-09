/*
 * Copyright 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdexcept>
#include <iostream>

#include "vulkan_profiles.hpp"

static void checkProfile(const VpProfileProperties& profile_properties) {
   std::cout << "Checking profile " << profile_properties.profileName
             << std::endl;
   VkBool32 instance_profile_supported = false;
   vpGetInstanceProfileSupport(nullptr, &profile_properties,
                               &instance_profile_supported);
   if (!instance_profile_supported) {
      throw std::runtime_error{"UNSUPPORTED instance"};
   }

   VkInstanceCreateInfo create_info{};
   create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

   VpInstanceCreateInfo instance_create_info{};
   instance_create_info.pEnabledFullProfiles = &profile_properties;
   instance_create_info.enabledFullProfileCount = 1;
   instance_create_info.pCreateInfo = &create_info;

   VkInstance instance = VK_NULL_HANDLE;
   VkResult result = vpCreateInstance(&instance_create_info, nullptr,
                                      &instance);
   if (result != VK_SUCCESS) {
      throw std::runtime_error{"Failed to create instance"};
   }

   uint32_t deviceCount = 1;
   VkPhysicalDevice pdev = VK_NULL_HANDLE;
   vkEnumeratePhysicalDevices(instance, &deviceCount, &pdev);
   if (deviceCount == 0) {
      vkDestroyInstance(instance, nullptr);
      throw std::runtime_error("No physical devices found");
   }

   VkPhysicalDeviceProperties2 properties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
   };
   vkGetPhysicalDeviceProperties2(pdev, &properties);
   std::cout << "Checking device " << properties.properties.deviceName
             << std::endl;

   VkBool32 pdev_profile_supported = false;
   vpGetPhysicalDeviceProfileSupport(instance, pdev, &profile_properties,
                                     &pdev_profile_supported);
   if (!pdev_profile_supported) {
      std::cout << "UNSUPPORTED physical device\n\n";
   } else {
      std::cout << "Supported\n\n";
   }

   vkDestroyInstance(instance, nullptr);
}

int main() {
   uint32_t count = 0;
   vpGetProfiles(&count, nullptr);
   std::vector<VpProfileProperties> profiles(count);
   vpGetProfiles(&count, profiles.data());

   for (const VpProfileProperties& profile : profiles) {
      checkProfile(profile);
   }
}
