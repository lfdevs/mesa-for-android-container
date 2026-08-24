/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VN_SYNC_H
#define VN_SYNC_H

#include "vn_common.h"

#include "vn_feedback.h"

enum vn_sync_type {
   /* no payload */
   VN_SYNC_TYPE_INVALID,

   /* device object */
   VN_SYNC_TYPE_DEVICE_ONLY,

   /* renderer sync object */
   VN_SYNC_TYPE_SYNC,

   /* renderer sync objects
    * - cpu sync x 1
    * - gpu sync x vn_device::queue_count
    */
   VN_SYNC_TYPE_TIMELINE_SYNC,

   /* payload is an imported sync file */
   VN_SYNC_TYPE_IMPORTED_SYNC_FD,
};

struct vn_sync_payload {
   enum vn_sync_type type;

   union {
      /* If type is VN_SYNC_TYPE_SYNC, sync is non-NULL. */
      struct vn_renderer_sync *sync;

      /* If type is VN_SYNC_TYPE_TIMELINE_SYNC, syncs are non-NULL. */
      struct vn_renderer_sync **syncs;

      /* If type is VN_SYNC_TYPE_IMPORTED_SYNC_FD, fd is a sync file. */
      int fd;
   };
};

struct vn_fence {
   struct vn_object_base base;

   struct vn_sync_payload *payload;

   struct vn_sync_payload permanent;
   struct vn_sync_payload temporary;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_fence,
                               base.vk,
                               VkFence,
                               VK_OBJECT_TYPE_FENCE)

struct vn_semaphore {
   struct vn_object_base base;

   VkSemaphoreType type;
   bool sync_fd_export;

   struct vn_sync_payload *payload;

   struct vn_sync_payload permanent;
   struct vn_sync_payload temporary;

   struct vn_sync_feedback feedback;
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

static inline bool
vn_semaphore_is_timeline(VkSemaphore sem_handle)
{
   struct vn_semaphore *sem = vn_semaphore_from_handle(sem_handle);
   return sem->type == VK_SEMAPHORE_TYPE_TIMELINE;
}

static inline bool
vn_semaphore_is_sync_fd(VkSemaphore sem_handle)
{
   struct vn_semaphore *sem = vn_semaphore_from_handle(sem_handle);
   return sem->payload->type == VN_SYNC_TYPE_IMPORTED_SYNC_FD ||
          (sem->payload->type == VN_SYNC_TYPE_SYNC && sem->sync_fd_export);
}

bool
vn_semaphore_wait_sync_fd(VkDevice dev_handle, VkSemaphore sem_handle);

#endif /* VN_SYNC_H */
