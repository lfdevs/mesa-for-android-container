/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvkmd_nouveau.h"

#include "nouveau_device.h"
#include "vk_log.h"

#include <xf86drm.h>

VkResult
nvkmd_nouveau_create_dev(struct nvkmd_pdev *_pdev,
                         struct vk_object_base *log_obj,
                         struct nvkmd_dev **dev_out)
{
   struct nvkmd_nouveau_pdev *pdev = nvkmd_nouveau_pdev(_pdev);

   struct nvkmd_nouveau_dev *dev = CALLOC_STRUCT(nvkmd_nouveau_dev);
   if (dev == NULL)
      return vk_error(log_obj, VK_ERROR_OUT_OF_HOST_MEMORY);

   dev->base.ops = &nvkmd_nouveau_dev_ops;
   dev->base.pdev = &pdev->base;
   dev->base.va_start = 0;
   dev->base.va_end = NOUVEAU_WS_DEVICE_KERNEL_RESERVATION_START;

   list_inithead(&dev->base.mems);
   simple_mtx_init(&dev->base.mems_mutex, mtx_plain);

   drmDevicePtr drm_device = NULL;
   int ret = drmGetDeviceFromDevId(pdev->base.drm.render_dev, 0, &drm_device);
   if (ret != 0) {
      FREE(dev);
      return vk_errorf(log_obj, VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to get DRM device: %m");
   }

   dev->ws_dev = nouveau_ws_device_new(drm_device);
   drmFreeDevice(&drm_device);
   if (dev->ws_dev == NULL) {
      FREE(dev);
      return vk_errorf(log_obj, VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to get DRM device: %m");
   }

   simple_mtx_init(&dev->heap_mutex, mtx_plain);

   STATIC_ASSERT(NVKMD_NOUVEAU_HEAP_START < NVKMD_NOUVEAU_HEAP_END);
   util_vma_heap_init(&dev->heap, NVKMD_NOUVEAU_HEAP_START,
                      NVKMD_NOUVEAU_HEAP_END - NVKMD_NOUVEAU_HEAP_START);

   STATIC_ASSERT(NVKMD_NOUVEAU_REPLAY_HEAP_START <
                    NVKMD_NOUVEAU_REPLAY_HEAP_END);
   util_vma_heap_init(&dev->replay_heap,
      NVKMD_NOUVEAU_REPLAY_HEAP_START,
      NVKMD_NOUVEAU_REPLAY_HEAP_END - NVKMD_NOUVEAU_REPLAY_HEAP_START);

   *dev_out = &dev->base;

   return VK_SUCCESS;
}

static void
nvkmd_nouveau_dev_destroy(struct nvkmd_dev *_dev)
{
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_dev);

   nouveau_ws_device_destroy(dev->ws_dev);
   FREE(dev);
}

static inline uint64_t
nvkmd_nouveau_dev_get_gpu_timestamp(struct nvkmd_dev *_dev)
{
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_dev);

   return nouveau_ws_device_timestamp(dev->ws_dev);
}

static int
nvkmd_nouveau_dev_get_drm_fd(struct nvkmd_dev *_dev)
{
   struct nvkmd_nouveau_dev *dev = nvkmd_nouveau_dev(_dev);

   return dev->ws_dev->fd;
}

const struct nvkmd_dev_ops nvkmd_nouveau_dev_ops = {
   .destroy = nvkmd_nouveau_dev_destroy,
   .get_gpu_timestamp = nvkmd_nouveau_dev_get_gpu_timestamp,
   .get_drm_fd = nvkmd_nouveau_dev_get_drm_fd,
   .alloc_mem = nvkmd_nouveau_alloc_mem,
   .alloc_tiled_mem = nvkmd_nouveau_alloc_tiled_mem,
   .import_dma_buf = nvkmd_nouveau_import_dma_buf,
   .alloc_va = nvkmd_nouveau_alloc_va,
   .create_ctx = nvkmd_nouveau_create_ctx,
};
