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

#include "util/mesa-blake3.h"

#include "wsi_common.h"

#include "pvr_device_info.h"
#include "pvr_formats.h"

#if defined(VK_USE_PLATFORM_DISPLAY_KHR) || \
    defined(VK_USE_PLATFORM_WAYLAND_KHR) || \
    defined(VK_USE_PLATFORM_XCB_KHR) || \
    defined(VK_USE_PLATFORM_XLIB_KHR)
#   define PVR_USE_WSI_PLATFORM true
#else
#   define PVR_USE_WSI_PLATFORM false
#endif

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
#   define PVR_USE_WSI_PLATFORM_DISPLAY true
#else
#   define PVR_USE_WSI_PLATFORM_DISPLAY false
#endif

struct pvr_instance;
typedef struct _pco_ctx pco_ctx;

struct pvr_physical_device {
   struct vk_physical_device vk;

   /* Back-pointer to instance */
   struct pvr_instance *instance;

   char *render_path;
   char *display_path;
   bool has_primary;

   /* primary node (cardN) of the render device
    * Only valid when has_primary is true.
    */
   dev_t primary_devid;
   /* render node (renderN) of the render device */
   dev_t render_devid;

   struct pvr_winsys *ws;
   struct pvr_device_info dev_info;
   struct pvr_device_runtime_info dev_runtime_info;

   VkPhysicalDeviceMemoryProperties memory;

   struct wsi_device wsi_device;

   pco_ctx *pco_ctx;

   struct pvr_format_table formats;

   uint8_t device_uuid[BLAKE3_KEY_LEN];
   uint8_t cache_uuid[BLAKE3_KEY_LEN];
};

VK_DEFINE_HANDLE_CASTS(pvr_physical_device,
                       vk.base,
                       VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

static void
pvr_get_render_area_granularity(struct pvr_physical_device *pdevice,
                                VkExtent2D *granularity)
{
   const struct pvr_device_info *dev_info = &pdevice->dev_info;

   /* Granularity does not depend on any settings in the render pass, so return
    * the tile granularity.
    *
    * The default value is based on the minimum value found in all existing
    * cores.
    */
   granularity->width = PVR_GET_FEATURE_VALUE(dev_info, tile_size_x, 16);
   granularity->height = PVR_GET_FEATURE_VALUE(dev_info, tile_size_y, 16);
}

VkResult pvr_physical_device_init(struct pvr_physical_device *pdevice,
                                  struct pvr_instance *instance,
                                  drmDevicePtr drm_render_device,
                                  drmDevicePtr drm_display_device);

void pvr_physical_device_dump_info(const struct pvr_physical_device *pdevice,
                                   char *const *comp_display,
                                   char *const *comp_render);

void pvr_physical_device_destroy(struct vk_physical_device *vk_pdevice);

void pvr_physical_device_free_pipeline_cache(
   struct pvr_physical_device *const pdevice);

#endif /* PVR_PHYSICAL_DEVICE_H */
