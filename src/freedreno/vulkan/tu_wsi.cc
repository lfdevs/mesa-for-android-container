/*
 * Copyright © 2016 Red Hat
 * SPDX-License-Identifier: MIT
 *
 * based on intel anv code:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_wsi.h"

#include "drm-uapi/drm_fourcc.h"

#include "util/u_debug.h"
#include "vk_util.h"
#include "wsi_common_drm.h"

#include "tu_device.h"

#include <stdio.h>
#include <string.h>

static bool
tu_wsi_debug_enabled(void)
{
   return debug_get_bool_option("TU_WSI_DEBUG", false);
}

static bool
tu_wsi_is_kgsl(const struct tu_physical_device *pdevice)
{
   return pdevice->instance->knl &&
          strcmp(pdevice->instance->knl->name, "kgsl") == 0;
}

static bool
tu_wsi_needs_fd725_x11_workarounds(const struct tu_physical_device *pdevice)
{
   /* Adreno730v3/Adreno725v1 reports this chip-id on KGSL.  On Termux:X11,
    * keep the native DRI3 path but avoid the modifier import path and X
    * Present idle fence import, both of which have failed on this device.
    */
   return tu_wsi_is_kgsl(pdevice) &&
          (pdevice->dev_id.chip_id == 0x07030002 ||
           pdevice->dev_id.chip_id == 0xffff07030002);
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
tu_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(tu_physical_device, pdevice, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(&pdevice->instance->vk, pName);
}

static bool
tu_wsi_can_present_on_device(VkPhysicalDevice physicalDevice, int fd)
{
#ifdef HAVE_LIBDRM
   VK_FROM_HANDLE(tu_physical_device, pdevice, physicalDevice);
   const bool is_kgsl = tu_wsi_is_kgsl(pdevice);

   if (is_kgsl) {
      if (tu_wsi_debug_enabled()) {
         fprintf(stderr,
                 "TU_WSI_DEBUG: can_present kgsl=1 dri3_fd=%d local_fd=%d result=1\n",
                 fd, pdevice->local_fd);
      }
      return true;
   }

   const bool result = wsi_common_drm_devices_equal(fd, pdevice->local_fd);
   if (tu_wsi_debug_enabled()) {
      fprintf(stderr,
              "TU_WSI_DEBUG: can_present kgsl=0 dri3_fd=%d local_fd=%d result=%d\n",
              fd, pdevice->local_fd, result);
   }
   return result;
#else
   if (tu_wsi_debug_enabled())
      fprintf(stderr, "TU_WSI_DEBUG: can_present libdrm=0 result=1\n");
   return true;
#endif
}

VkResult
tu_wsi_init(struct tu_physical_device *physical_device)
{
   VkResult result;

   const struct wsi_device_options options = { .sw_device = false };
   result = wsi_device_init(&physical_device->wsi_device,
                            tu_physical_device_to_handle(physical_device),
                            tu_wsi_proc_addr,
                            &physical_device->instance->vk.alloc,
                            physical_device->master_fd,
                            &physical_device->instance->dri_options,
                            &options);
   if (result != VK_SUCCESS)
      return result;

   const bool fd725_x11_workarounds =
      tu_wsi_needs_fd725_x11_workarounds(physical_device);
   physical_device->wsi_device.supports_modifiers = !fd725_x11_workarounds;
   physical_device->wsi_device.x11.disable_shm_fences =
      fd725_x11_workarounds;
   physical_device->wsi_device.can_present_on_device =
      tu_wsi_can_present_on_device;

   if (tu_wsi_debug_enabled()) {
      fprintf(stderr,
              "TU_WSI_DEBUG: init kgsl=%d chip_id=0x%llx supports_modifiers=%d disable_shm_fences=%d\n",
              tu_wsi_is_kgsl(physical_device),
              (unsigned long long) physical_device->dev_id.chip_id,
              physical_device->wsi_device.supports_modifiers,
              physical_device->wsi_device.x11.disable_shm_fences);
   }

   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}

void
tu_wsi_finish(struct tu_physical_device *physical_device)
{
   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->instance->vk.alloc);
}
