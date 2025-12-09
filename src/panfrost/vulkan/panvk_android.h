/*
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_ANDROID_H
#define PANVK_ANDROID_H

#include <stdbool.h>

#include "util/detect_os.h"
#include "vulkan/vulkan.h"

struct panvk_device;

#ifdef VK_USE_PLATFORM_ANDROID_KHR

bool panvk_android_is_gralloc_image(const VkImageCreateInfo *pCreateInfo);

VkResult panvk_android_create_gralloc_image(
   VkDevice device, const VkImageCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkImage *pImage);

VkResult panvk_android_get_wsi_memory(struct panvk_device *dev,
                                      const VkBindImageMemoryInfo *bind_info,
                                      VkDeviceMemory *out_mem_handle);

bool panvk_android_is_ahb_memory(const VkMemoryAllocateInfo *pAllocateInfo);

VkResult panvk_android_allocate_ahb_memory(
   VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
   const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);

#else /* VK_USE_PLATFORM_ANDROID_KHR */

static inline bool
panvk_android_is_gralloc_image(const VkImageCreateInfo *pCreateInfo)
{
   return false;
}

static inline VkResult
panvk_android_create_gralloc_image(VkDevice device,
                                   const VkImageCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkImage *pImage)
{
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

static inline VkResult
panvk_android_get_wsi_memory(struct panvk_device *dev,
                             const VkBindImageMemoryInfo *bind_info,
                             VkDeviceMemory *out_mem_handle)
{
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

static inline bool
panvk_android_is_ahb_memory(const VkMemoryAllocateInfo *pAllocateInfo)
{
   return false;
}

static inline VkResult
panvk_android_allocate_ahb_memory(VkDevice device,
                                  const VkMemoryAllocateInfo *pAllocateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkDeviceMemory *pMemory)
{
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

#endif /* VK_USE_PLATFORM_ANDROID_KHR */

#endif /* PANVK_ANDROID_H */
