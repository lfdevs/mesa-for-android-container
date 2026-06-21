/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 *
 * Derived from tu_wsi.c:
 * Copyright © 2016 Red Hat
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_wsi.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"

#include "vk_util.h"
#include "wsi_common.h"

static VKAPI_PTR PFN_vkVoidFunction
panvk_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(panvk_physical_device, pdevice, physicalDevice);
   struct panvk_instance *instance = to_panvk_instance(pdevice->vk.instance);

   return vk_instance_get_proc_addr_unchecked(&instance->vk, pName);
}

static bool
panvk_can_present_on_device(VkPhysicalDevice pdevice, int fd)
{
   drmDevicePtr device;
   if (drmGetDevice2(fd, 0, &device) != 0)
      return false;
   /* Allow on-device presentation for all devices with bus type PLATFORM.
    * Other device types such as PCI or USB should use the PRIME blit path. */
   bool match = device->bustype == DRM_BUS_PLATFORM;

   drmFreeDevice(&device);

   return match;
}

VkResult
panvk_wsi_init(struct panvk_physical_device *physical_device)
{
   struct panvk_instance *instance =
      to_panvk_instance(physical_device->vk.instance);
   VkResult result;

   result = wsi_device_init(&physical_device->wsi_device,
                            panvk_physical_device_to_handle(physical_device),
                            panvk_wsi_proc_addr, &instance->vk.alloc, -1,
                            &instance->drirc.options,
                            &(struct wsi_device_options){.sw_device = false});
   if (result != VK_SUCCESS)
      return result;

   physical_device->wsi_device.supports_modifiers = true;
   physical_device->wsi_device.can_present_on_device =
      panvk_can_present_on_device;

   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}

void
panvk_wsi_finish(struct panvk_physical_device *physical_device)
{
   struct panvk_instance *instance =
      to_panvk_instance(physical_device->vk.instance);

   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device, &instance->vk.alloc);
}
