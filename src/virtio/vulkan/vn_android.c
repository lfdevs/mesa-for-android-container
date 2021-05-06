/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_common.h"

#include <hardware/hwvulkan.h>
#include <vulkan/vk_icd.h>

static int
vn_hal_open(const struct hw_module_t *mod,
            const char *id,
            struct hw_device_t **dev);

static void UNUSED
static_asserts(void)
{
   STATIC_ASSERT(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC);
}

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common = {
      .tag = HARDWARE_MODULE_TAG,
      .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
      .hal_api_version = HARDWARE_HAL_API_VERSION,
      .id = HWVULKAN_HARDWARE_MODULE_ID,
      .name = "Venus Vulkan HAL",
      .author = "Google LLC",
      .methods = &(hw_module_methods_t) {
         .open = vn_hal_open,
      },
   },
};

static int
vn_hal_close(UNUSED struct hw_device_t *dev)
{
   return 0;
}

static hwvulkan_device_t vn_hal_dev = {
  .common = {
     .tag = HARDWARE_DEVICE_TAG,
     .version = HWVULKAN_DEVICE_API_VERSION_0_1,
     .module = &HAL_MODULE_INFO_SYM.common,
     .close = vn_hal_close,
  },
 .EnumerateInstanceExtensionProperties = vn_EnumerateInstanceExtensionProperties,
 .CreateInstance = vn_CreateInstance,
 .GetInstanceProcAddr = vn_GetInstanceProcAddr,
};

static int
vn_hal_open(const struct hw_module_t *mod,
            const char *id,
            struct hw_device_t **dev)
{
   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   *dev = &vn_hal_dev.common;
   return 0;
}
