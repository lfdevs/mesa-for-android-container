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

   /* emulated queue shares base queue id and ring_idx with another queue */
   bool emulated;

   /* whether this queue supports venus feedback */
   bool can_feedback;

   /* only used if renderer supports multiple timelines */
   uint32_t ring_idx;

   /* wait fence used for vn_QueueWaitIdle */
   VkFence wait_fence;

   /* semaphore for gluing vkQueueSubmit feedback commands to
    * vkQueueBindSparse
    */
   VkSemaphore sparse_semaphore;
   uint64_t sparse_semaphore_counter;

   /* for vn_queue_submission storage */
   struct vn_cached_storage storage;
};
VK_DEFINE_HANDLE_CASTS(vn_queue, base.vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

enum vn_sync_type {
   /* no payload */
   VN_SYNC_TYPE_INVALID,

   /* device object */
   VN_SYNC_TYPE_DEVICE_ONLY,

   /* payload is an imported sync file */
   VN_SYNC_TYPE_IMPORTED_SYNC_FD,
};

struct vn_sync_payload {
   enum vn_sync_type type;

   /* If type is VN_SYNC_TYPE_IMPORTED_SYNC_FD, fd is a sync file. */
   int fd;
};

/* For external fences and external semaphores submitted to be signaled. The
 * Vulkan spec guarantees those external syncs are on permanent payload.
 */
struct vn_sync_payload_external {
   /* ring_idx of the last queue submission */
   uint32_t ring_idx;
   /* valid when NO_ASYNC_QUEUE_SUBMIT perf option is not used */
   bool ring_seqno_valid;
   /* ring seqno of the last queue submission */
   uint32_t ring_seqno;
};

struct vn_feedback_slot;

struct vn_fence {
   struct vn_object_base base;

   struct vn_sync_payload *payload;

   struct vn_sync_payload permanent;
   struct vn_sync_payload temporary;

   struct {
      /* non-NULL if VN_PERF_NO_FENCE_FEEDBACK is disabled */
      struct vn_feedback_slot *slot;
      VkCommandBuffer *commands;

      /* Indicate whether the fence status in the feedback slot is pollable.
       * When pollable is false, the fence feedback has been suspended and the
       * slot won't be signaled to VK_SUCCESS.
       * - suspend: submit on queues not supporting feedback
       * - resume: vn_ResetFences will reset pollable to true
       */
      bool pollable;
   } feedback;

   bool is_external;
   struct vn_sync_payload_external external_payload;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_fence,
                               base.vk,
                               VkFence,
                               VK_OBJECT_TYPE_FENCE)

struct vn_semaphore {
   struct vn_object_base base;

   VkSemaphoreType type;

   struct vn_sync_payload *payload;

   struct vn_sync_payload permanent;
   struct vn_sync_payload temporary;

   struct {
      /* non-NULL if VN_PERF_NO_SEMAPHORE_FEEDBACK is disabled */
      struct vn_feedback_slot *slot;

      /* Lists of allocated vn_semaphore_feedback_cmd
       *
       * On submission prepare, sfb cmd is cache allocated from the free list
       * and is moved to the pending list after initialization.
       *
       * On submission cleanup, sfb cmds of the owner semaphores are checked
       * and cached to the free list if they have been "signaled", which is
       * proxyed via the src slot value having been reached.
       */
      struct list_head pending_cmds;
      struct list_head free_cmds;
      uint32_t free_cmd_count;

      /* Lock for accessing free/pending sfb cmds */
      simple_mtx_t cmd_mtx;

      /* Indicate whether the timeline semaphore counter value in the feedback
       * slot is pollable. When pollable is false, the semaphore feedback has
       * been suspended and the slot won't be signaled to the pending counter.
       * - suspend: submit on queues not supporting feedback
       * - resume if any of below occurs:
       *   - vn_SignalSemaphore
       *   - when the queried counter value is no smaller than the suspended
       *     counter value
       */
      bool pollable;

      /* When feedback is active, signaled_counter is the cached counter value
       * to track if an async sem wait call is needed.
       *
       * When feedback is suspended, suspended_counter tracks the greatest
       * signal counter value submitted on queues not supporting feedback.
       *
       * They share the same storage and the value is monotonic.
       */
      union {
         uint64_t signaled_counter;
         uint64_t suspended_counter;
      };

      /* Lock for checking if an async sem wait call is needed based on
       * the current counter value and signaled_counter to ensure async
       * wait order across threads.
       *
       * Also lock to protect suspended_counter and pollable updates.
       */
      simple_mtx_t counter_mtx;
   } feedback;

   bool is_external;
   struct vn_sync_payload_external external_payload;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_semaphore,
                               base.vk,
                               VkSemaphore,
                               VK_OBJECT_TYPE_SEMAPHORE)

struct vn_event {
   struct vn_object_base base;

   /* non-NULL if below are satisfied:
    * - event is created without VK_EVENT_CREATE_DEVICE_ONLY_BIT
    * - VN_PERF_NO_EVENT_FEEDBACK is disabled
    */
   struct vn_feedback_slot *feedback_slot;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_event,
                               base.vk,
                               VkEvent,
                               VK_OBJECT_TYPE_EVENT)

#endif /* VN_QUEUE_H */
