/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_QUEUE_H
#define VN_QUEUE_H

#include "vn_common.h"

struct vn_queue {
   struct vn_queue_base base;

   /* internal queue index within vn_device::queues */
   uint32_t index;

   /* emulated queue shares base queue id and ring_idx with another queue */
   bool emulated;

   /* whether this queue supports venus feedback */
   bool can_feedback;

   /* only used if renderer supports multiple timelines */
   uint32_t ring_idx;

   /* Ensure fence submission via the virtqueue is executed AFTER the queue
    * submission via the ring.
    *
    * 1. driver submit queue batch via the ring
    * 2. driver submit via vq to wait for ring and then submit queue fences
    */
   bool ring_seqno_valid;
   uint32_t ring_seqno;

   /* Ensure fence submission via the virtqueue is executed BEFORE the next
    * queue submission via the ring.
    *
    * 1. driver submit via vq to update vq seqno
    * 2. driver submit via ring to wait for vq and then submit queue batch
    */
   bool roundtrip_seqno_valid;
   uint64_t roundtrip_seqno;

   /* wait fence used for vn_QueueWaitIdle */
   VkFence wait_fence;

   /* for vn_queue_submission storage */
   struct vn_cached_storage storage;

   /* for async queue present */
   struct {
      /* Protects VkQueue host access except async present states. */
      simple_mtx_t queue_mutex;
      /* Protects state transitions: initialized, pending and join. */
      mtx_t mutex;
      /* Wake up async present thread upon presentation. */
      cnd_t cond;
      /* This is the async present thread. */
      thrd_t thread;
      /* Avoid extra locking on async present thread. */
      pid_t tid;
      /* Track whether the async present thread has been initialized. */
      bool initialized;
      /* Track whether the present is still pending acquired. */
      bool pending;
      /* Track whether to join the async present thread. */
      bool join;
      /* This is a deep copy of the requested presentation. */
      VkPresentInfoKHR *info;
      /* Track the result of the presentation. */
      VkResult result;
      /* This is used by vtest to properly wait before present. */
      VkFence fence;
   } async_present;
};
VK_DEFINE_HANDLE_CASTS(vn_queue, base.vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

#endif /* VN_QUEUE_H */
