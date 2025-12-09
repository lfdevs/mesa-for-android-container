/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_QUEUE_H
#define PANVK_QUEUE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "genxml/gen_macros.h"

#include <stdint.h>

#include "panvk_device.h"

#include "vk_queue.h"

enum panvk_subqueue_id {
   PANVK_SUBQUEUE_VERTEX_TILER = 0,
   PANVK_SUBQUEUE_FRAGMENT,
   PANVK_SUBQUEUE_COMPUTE,
   PANVK_SUBQUEUE_COUNT,
};

struct panvk_tiler_heap {
   uint32_t chunk_size;
   struct panvk_priv_mem desc;
   struct {
      uint32_t handle;
      uint64_t dev_addr;
   } context;
};

struct panvk_subqueue {
   struct panvk_priv_mem context;
   uint32_t *reg_file;

   /* Memory to save/restore CS registers in functions/exception handlers.
    * Because registers are dumped to a fixed address rather than a moving
    * stack pointer, nested function/exception handler calls are not supported.
    */
   struct panvk_priv_mem regs_save;


   struct {
      /* Mask of resources requested by this subqueue. */
      uint32_t mask;
      /* Address and size of the linear buffer containing REQ_RESOURCE. */
      uint32_t cs_buffer_size;
      uint64_t cs_buffer_addr;
      /* Allocation */
      struct panvk_priv_mem buf;
   } req_resource;

   struct {
      struct pan_kmod_bo *bo;
      uint64_t size;
      struct {
         uint64_t dev;
         void *host;
      } addr;
   } tracebuf;
};

struct panvk_desc_ringbuf {
   struct panvk_priv_mem syncobj;
   struct pan_kmod_bo *bo;
   uint64_t size;
   struct {
      uint64_t dev;
      void *host;
   } addr;
};

struct panvk_gpu_queue {
   struct vk_queue vk;

   uint32_t group_handle;
   uint32_t syncobj_handle;

   struct panvk_tiler_heap tiler_heap;
   struct panvk_desc_ringbuf render_desc_ringbuf;
   struct panvk_priv_mem syncobjs;

   struct {
      struct vk_sync *sync;
      uint64_t next_value;
   } utrace;

   struct panvk_subqueue subqueues[PANVK_SUBQUEUE_COUNT];
};

VK_DEFINE_HANDLE_CASTS(panvk_gpu_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VkResult panvk_per_arch(create_gpu_queue)(
   struct panvk_device *dev, const VkDeviceQueueCreateInfo *create_info,
   uint32_t queue_idx, struct vk_queue **out_queue);
void panvk_per_arch(destroy_gpu_queue)(struct vk_queue *vk_queue);
VkResult panvk_per_arch(gpu_queue_submit)(struct vk_queue *vk_queue,
                                          struct vk_queue_submit *vk_submit);
VkResult panvk_per_arch(gpu_queue_check_status)(struct vk_queue *vk_queue);

struct panvk_bind_queue {
   struct vk_queue vk;

   uint32_t syncobj_handle;
};

VK_DEFINE_HANDLE_CASTS(panvk_bind_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VkResult panvk_per_arch(create_bind_queue)(
   struct panvk_device *dev, const VkDeviceQueueCreateInfo *create_info,
   uint32_t queue_idx,struct vk_queue **out_queue);
void panvk_per_arch(destroy_bind_queue)(struct vk_queue *vk_queue);
VkResult panvk_per_arch(bind_queue_submit)(struct vk_queue *vk_queue,
                                           struct vk_queue_submit *vk_submit);
VkResult panvk_per_arch(bind_queue_check_status)(struct vk_queue *vk_queue);

#endif
