/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_ANDROID_H
#define VN_ANDROID_H

#include "vn_common.h"

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#ifdef VK_USE_PLATFORM_ANDROID_KHR

VkResult
vn_android_image_from_anb(struct vn_device *dev,
                          const VkImageCreateInfo *image_info,
                          const VkNativeBufferANDROID *anb_info,
                          const VkAllocationCallbacks *alloc,
                          struct vn_image **out_img);

struct vn_device_memory *
vn_android_get_wsi_memory_from_bind_info(
   struct vn_device *dev, const VkBindImageMemoryInfo *bind_info);

VkResult
vn_android_device_import_ahb(struct vn_device *dev,
                             struct vn_device_memory *mem,
                             const struct VkMemoryAllocateInfo *alloc_info);

#else /* VK_USE_PLATFORM_ANDROID_KHR */

static inline VkResult
vn_android_image_from_anb(UNUSED struct vn_device *dev,
                          UNUSED const VkImageCreateInfo *image_info,
                          UNUSED const VkNativeBufferANDROID *anb_info,
                          UNUSED const VkAllocationCallbacks *alloc,
                          UNUSED struct vn_image **out_img)
{
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

static inline struct vn_device_memory *
vn_android_get_wsi_memory_from_bind_info(
   UNUSED struct vn_device *dev,
   UNUSED const VkBindImageMemoryInfo *bind_info)
{
   return NULL;
}

static inline VkResult
vn_android_device_import_ahb(
   UNUSED struct vn_device *dev,
   UNUSED struct vn_device_memory *mem,
   UNUSED const struct VkMemoryAllocateInfo *alloc_info)
{
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

#endif /* VK_USE_PLATFORM_ANDROID_KHR */

#endif /* VN_ANDROID_H */
