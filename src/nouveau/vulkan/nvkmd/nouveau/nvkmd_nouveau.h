/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVKMD_DRM_H
#define NVKMD_DRM_H 1

#include "nvkmd/nvkmd.h"
#include "vk_drm_syncobj.h"
#include "util/vma.h"

#include "drm-uapi/nouveau_drm.h"

#include <sys/types.h>

struct nouveau_ws_bo;
struct nouveau_ws_context;
struct nouveau_ws_device;

struct nvkmd_nouveau_pdev {
   struct nvkmd_pdev base;

   /* Used for get_vram_used() */
   struct nouveau_ws_device *ws_dev;

   int primary_fd;

   struct vk_sync_type syncobj_sync_type;
   const struct vk_sync_type *sync_types[2];
};

NVKMD_DECL_SUBCLASS(pdev, nouveau);

VkResult nvkmd_nouveau_try_create_pdev(struct _drmDevice *drm_device,
                                       struct vk_object_base *log_obj,
                                       enum nvk_debug debug_flags,
                                       struct nvkmd_pdev **pdev_out);

#define NVKMD_NOUVEAU_HEAP_START ((uint64_t)4096)
#define NVKMD_NOUVEAU_HEAP_END ((uint64_t)(1ull << 38))
#define NVKMD_NOUVEAU_REPLAY_HEAP_START NVKMD_NOUVEAU_HEAP_END
#define NVKMD_NOUVEAU_REPLAY_HEAP_END \
   ((uint64_t)NOUVEAU_WS_DEVICE_KERNEL_RESERVATION_START)

struct nvkmd_nouveau_dev {
   struct nvkmd_dev base;

   struct nouveau_ws_device *ws_dev;

   simple_mtx_t heap_mutex;
   struct util_vma_heap heap;
   struct util_vma_heap replay_heap;
};

NVKMD_DECL_SUBCLASS(dev, nouveau);

VkResult nvkmd_nouveau_create_dev(struct nvkmd_pdev *pdev,
                                  struct vk_object_base *log_obj,
                                  struct nvkmd_dev **dev_out);

struct nvkmd_nouveau_mem {
   struct nvkmd_mem base;

   struct nouveau_ws_bo *bo;
};

NVKMD_DECL_SUBCLASS(mem, nouveau);

VkResult nvkmd_nouveau_alloc_mem(struct nvkmd_dev *dev,
                                 struct vk_object_base *log_obj,
                                 uint64_t size_B, uint64_t align_B,
                                 enum nvkmd_mem_flags flags,
                                 struct nvkmd_mem **mem_out);

VkResult nvkmd_nouveau_alloc_tiled_mem(struct nvkmd_dev *dev,
                                       struct vk_object_base *log_obj,
                                       uint64_t size_B, uint64_t align_B,
                                       uint8_t pte_kind, uint16_t tile_mode,
                                       enum nvkmd_mem_flags flags,
                                       struct nvkmd_mem **mem_out);

VkResult nvkmd_nouveau_import_dma_buf(struct nvkmd_dev *dev,
                                      struct vk_object_base *log_obj,
                                      int fd, struct nvkmd_mem **mem_out);

struct nvkmd_nouveau_va {
   struct nvkmd_va base;
};

NVKMD_DECL_SUBCLASS(va, nouveau);

VkResult nvkmd_nouveau_alloc_va(struct nvkmd_dev *dev,
                                struct vk_object_base *log_obj,
                                enum nvkmd_va_flags flags, uint8_t pte_kind,
                                uint64_t size_B, uint64_t align_B,
                                uint64_t fixed_addr, struct nvkmd_va **va_out);

#define NVKMD_NOUVEAU_MAX_SYNCS 256
#define NVKMD_NOUVEAU_MAX_BINDS 4096
#define NVKMD_NOUVEAU_MAX_PUSH 1024

struct nvkmd_nouveau_exec_ctx {
   struct nvkmd_ctx base;

   struct nouveau_ws_device *ws_dev;
   struct nouveau_ws_context *ws_ctx;

   uint32_t syncobj;

   uint32_t max_push;

   struct drm_nouveau_sync req_wait[NVKMD_NOUVEAU_MAX_SYNCS];
   struct drm_nouveau_sync req_sig[NVKMD_NOUVEAU_MAX_SYNCS];
   struct drm_nouveau_exec_push req_push[NVKMD_NOUVEAU_MAX_PUSH];
   struct drm_nouveau_exec req;
};

NVKMD_DECL_SUBCLASS(ctx, nouveau_exec);

struct nvkmd_nouveau_bind_ctx {
   struct nvkmd_ctx base;

   struct nouveau_ws_device *ws_dev;

   struct drm_nouveau_sync req_wait[NVKMD_NOUVEAU_MAX_SYNCS];
   struct drm_nouveau_sync req_sig[NVKMD_NOUVEAU_MAX_SYNCS];
   struct drm_nouveau_vm_bind_op req_ops[NVKMD_NOUVEAU_MAX_BINDS];
   struct drm_nouveau_vm_bind req;
};

NVKMD_DECL_SUBCLASS(ctx, nouveau_bind);

VkResult nvkmd_nouveau_create_ctx(struct nvkmd_dev *dev,
                                  struct vk_object_base *log_obj,
                                  enum nvkmd_engines engines,
                                  struct nvkmd_ctx **ctx_out);

#endif /* NVKMD_DRM_H */
