/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_PHYSICAL_DEVICE_H
#define PVR_PHYSICAL_DEVICE_H

#include "vk_physical_device.h"

#include <stdint.h>
#include <sys/types.h>
#include <xf86drm.h>

#include "util/mesa-sha1.h"

#include "wsi_common.h"

#include "pvr_device_info.h"
#include "pvr_instance.h"

#if defined(VK_USE_PLATFORM_DISPLAY_KHR) || \
    defined(VK_USE_PLATFORM_WAYLAND_KHR)
#   define PVR_USE_WSI_PLATFORM true
#else
#   define PVR_USE_WSI_PLATFORM false
#endif

struct pvr_instance;
typedef struct _pco_ctx pco_ctx;

struct pvr_physical_device {
   struct vk_physical_device vk;

   /* Back-pointer to instance */
   struct pvr_instance *instance;

   char *render_path;
   char *display_path;

   /* primary node (cardN) of the render device */
   dev_t primary_devid;
   /* render node (renderN) of the render device */
   dev_t render_devid;

   struct pvr_winsys *ws;
   struct pvr_device_info dev_info;
   struct pvr_device_runtime_info dev_runtime_info;

   VkPhysicalDeviceMemoryProperties memory;

   struct wsi_device wsi_device;

   pco_ctx *pco_ctx;

   uint8_t device_uuid[SHA1_DIGEST_LENGTH];
   uint8_t cache_uuid[SHA1_DIGEST_LENGTH];
};

VK_DEFINE_HANDLE_CASTS(pvr_physical_device,
                       vk.base,
                       VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

VkResult
pvr_physical_device_init(struct pvr_physical_device *pdevice,
                         struct pvr_instance *instance,
                         drmDevicePtr drm_render_device,
                         drmDevicePtr drm_display_device);

void
pvr_physical_device_dump_info(const struct pvr_physical_device *pdevice,
                              char *const *comp_display,
                              char *const *comp_render);

void
pvr_physical_device_destroy(struct vk_physical_device *vk_pdevice);

void
pvr_physical_device_free_pipeline_cache(struct pvr_physical_device *const pdevice);

#endif /* PVR_PHYSICAL_DEVICE_H */
