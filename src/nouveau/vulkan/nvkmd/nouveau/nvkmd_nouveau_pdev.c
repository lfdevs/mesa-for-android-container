/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvkmd_nouveau.h"

#include "nouveau_device.h"
#include "util/cache_ops.h"
#include "util/os_misc.h"
#include "util/drm_is_nouveau.h"
#include "vk_log.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <xf86drm.h>

static bool
drm_device_is_nouveau(const char *path)
{
   int fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0)
      return false;

   const bool is_nouveau = drm_fd_is_nouveau(fd);
   close(fd);

   return is_nouveau;
}

VkResult
nvkmd_nouveau_try_create_pdev(struct _drmDevice *drm_device,
                              struct vk_object_base *log_obj,
                              enum nvk_debug debug_flags,
                              struct nvkmd_pdev **pdev_out)
{
   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER)))
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   switch (drm_device->bustype) {
   case DRM_BUS_PCI:
      if (drm_device->deviceinfo.pci->vendor_id != NVIDIA_VENDOR_ID)
         return VK_ERROR_INCOMPATIBLE_DRIVER;
      break;

   case DRM_BUS_PLATFORM: {
      const char *compat_prefix = "nvidia,";
      bool found = false;
      for (int i = 0; drm_device->deviceinfo.platform->compatible[i] != NULL; i++) {
         if (strncmp(drm_device->deviceinfo.platform->compatible[0], compat_prefix, strlen(compat_prefix)) == 0) {
            found = true;
            break;
         }
      }
      if (!found)
         return VK_ERROR_INCOMPATIBLE_DRIVER;
      break;
   }

   default:
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }

   if (!drm_device_is_nouveau(drm_device->nodes[DRM_NODE_RENDER]))
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   struct nouveau_ws_device *ws_dev = nouveau_ws_device_new(drm_device);
   if (!ws_dev)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   if (!ws_dev->has_vm_bind) {
      nouveau_ws_device_destroy(ws_dev);
      /* NVK Requires a Linux kernel version 6.6 or later */
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }

   struct stat st;
   if (stat(drm_device->nodes[DRM_NODE_RENDER], &st)) {
      nouveau_ws_device_destroy(ws_dev);
      return vk_errorf(log_obj, VK_ERROR_INITIALIZATION_FAILED,
                       "fstat() failed on %s: %m",
                       drm_device->nodes[DRM_NODE_RENDER]);
   }
   const dev_t render_dev = st.st_rdev;

   struct nvkmd_nouveau_pdev *pdev = CALLOC_STRUCT(nvkmd_nouveau_pdev);
   if (pdev == NULL) {
      nouveau_ws_device_destroy(ws_dev);
      return vk_error(log_obj, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   pdev->base.ops = &nvkmd_nouveau_pdev_ops;
   pdev->base.debug_flags = debug_flags;
   pdev->base.dev_info = ws_dev->info;
   pdev->base.kmd_info = (struct nvkmd_info) {
      .has_dma_buf = true,
      .has_get_vram_used = nouveau_ws_device_vram_used(ws_dev) != 0,
      .has_alloc_tiled = nouveau_ws_device_has_tiled_bo(ws_dev),
      .has_map_fixed = true,
      .has_overmap = true,
   };

   /* We get this ourselves */
   pdev->base.dev_info.nc_atom_size_B = util_cache_granularity();

   /* Nouveau uses the OS page size for all pages, regardless of whether they
    * come from VRAM or system RAM.
    */
   uint64_t os_page_size;
   os_get_page_size(&os_page_size);
   assert(os_page_size <= UINT32_MAX);
   pdev->base.bind_align_B = os_page_size;

   pdev->base.drm.render_dev = render_dev;

   /* DRM primary is optional */
   if ((drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) &&
       !stat(drm_device->nodes[DRM_NODE_PRIMARY], &st))
      pdev->base.drm.primary_dev = st.st_rdev;

   pdev->primary_fd = -1;
   pdev->ws_dev = ws_dev;

   pdev->syncobj_sync_type = vk_drm_syncobj_get_type(ws_dev->fd);
   pdev->sync_types[0] = &pdev->syncobj_sync_type;
   pdev->sync_types[1] = NULL;
   pdev->base.sync_types = pdev->sync_types;

   *pdev_out = &pdev->base;

   return VK_SUCCESS;
}

static void
nvkmd_nouveau_pdev_destroy(struct nvkmd_pdev *_pdev)
{
   struct nvkmd_nouveau_pdev *pdev = nvkmd_nouveau_pdev(_pdev);

   if (pdev->primary_fd >= 0)
      close(pdev->primary_fd);

   nouveau_ws_device_destroy(pdev->ws_dev);
   FREE(pdev);
}

static uint64_t
nvkmd_nouveau_pdev_get_vram_used(struct nvkmd_pdev *_pdev)
{
   struct nvkmd_nouveau_pdev *pdev = nvkmd_nouveau_pdev(_pdev);

   return nouveau_ws_device_vram_used(pdev->ws_dev);
}

static int
nvkmd_nouveau_pdev_get_drm_primary_fd(struct nvkmd_pdev *_pdev)
{
   struct nvkmd_nouveau_pdev *pdev = nvkmd_nouveau_pdev(_pdev);

   if (pdev->primary_fd >= 0)
      return pdev->primary_fd;

   if (pdev->base.drm.primary_dev == 0)
      return -1;

   drmDevicePtr drm_device = NULL;
   int ret = drmGetDeviceFromDevId(pdev->base.drm.primary_dev, 0, &drm_device);
   if (ret != 0)
      return -1;

   int fd = open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
   drmFreeDevice(&drm_device);

   /* TODO: Test if the FD is usable? */

   pdev->primary_fd = fd;

   return pdev->primary_fd;
}

const struct nvkmd_pdev_ops nvkmd_nouveau_pdev_ops = {
   .destroy = nvkmd_nouveau_pdev_destroy,
   .get_vram_used = nvkmd_nouveau_pdev_get_vram_used,
   .get_drm_primary_fd = nvkmd_nouveau_pdev_get_drm_primary_fd,
   .create_dev = nvkmd_nouveau_create_dev,
};
