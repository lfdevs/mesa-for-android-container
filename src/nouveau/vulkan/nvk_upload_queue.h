/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DMA_QUEUE_H
#define NVK_DMA_QUEUE_H 1

#include "nvk_private.h"

#include "util/list.h"
#include "util/simple_mtx.h"

struct nvk_device;
struct nvk_upload_mem;
struct nvkmd_mem;
struct nvkmd_ctx;
struct vk_sync;

struct nvk_upload_queue {
   simple_mtx_t mutex;

   struct nvkmd_ctx *ctx;

   struct vk_sync *sync;
   uint64_t last_time_point;

   struct nvk_upload_mem *mem;

   /* We grow the buffer from both ends.  Pushbuf data goes at the start of
    * the buffer and upload data at the tail.
    */
   uint32_t mem_push_start;
   uint32_t mem_push_end;
   uint32_t mem_data_start;

   /* list of nvk_upload_mem */
   struct list_head recycle;
};

VkResult nvk_upload_queue_init(struct nvk_device *dev,
                               struct nvk_upload_queue *queue);
void nvk_upload_queue_finish(struct nvk_device *dev,
                             struct nvk_upload_queue *queue);

VkResult nvk_upload_queue_flush(struct nvk_device *dev,
                                struct nvk_upload_queue *queue,
                                uint64_t *time_point_out);

VkResult nvk_upload_queue_sync(struct nvk_device *dev,
                               struct nvk_upload_queue *queue);

VkResult nvk_upload_queue_upload(struct nvk_device *dev,
                                 struct nvk_upload_queue *queue,
                                 uint64_t dst_addr,
                                 const void *src, size_t size);

VkResult nvk_upload_queue_fill(struct nvk_device *dev,
                               struct nvk_upload_queue *queue,
                               uint64_t dst_addr, uint32_t data, size_t size);

#endif /* NVK_DMA_QUEUE_H */
