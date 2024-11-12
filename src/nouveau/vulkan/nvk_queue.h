/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_QUEUE_H
#define NVK_QUEUE_H 1

#include "nvk_private.h"

#include "vk_queue.h"
#include "nvkmd/nvkmd.h"

struct nouveau_ws_bo;
struct nouveau_ws_context;
struct novueau_ws_push;
struct nv_push;
struct nvk_device;
struct nvkmd_mem;
struct nvkmd_ctx;

struct nvk_queue_state {
   struct {
      struct nvkmd_mem *mem;
      uint32_t alloc_count;
   } images;

   struct {
      struct nvkmd_mem *mem;
      uint32_t alloc_count;
   } samplers;

   struct {
      struct nvkmd_mem *mem;
      uint32_t bytes_per_warp;
      uint32_t bytes_per_tpc;
   } slm;
};

struct nvk_queue {
   struct vk_queue vk;

   enum nvkmd_engines engines;

   struct nvkmd_ctx *bind_ctx;
   struct nvkmd_ctx *exec_ctx;

   struct nvk_queue_state state;

   /* CB0 for all draw commands on this queue */
   struct nvkmd_mem *draw_cb0;
};

static inline struct nvk_device *
nvk_queue_device(struct nvk_queue *queue)
{
   return (struct nvk_device *)queue->vk.base.device;
}

VkResult nvk_queue_init(struct nvk_device *dev, struct nvk_queue *queue,
                        const VkDeviceQueueCreateInfo *pCreateInfo,
                        uint32_t index_in_family);

void nvk_queue_finish(struct nvk_device *dev, struct nvk_queue *queue);

VkResult nvk_push_draw_state_init(struct nvk_queue *queue,
                                  struct nv_push *p);

VkResult nvk_push_dispatch_state_init(struct nvk_queue *queue,
                                      struct nv_push *p);

#endif
