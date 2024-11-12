/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "xe/intel_queue.h"

#include "common/intel_gem.h"

#include "drm-uapi/xe_drm.h"

/* Creates a syncobj that will be signaled when all the workloads in given
 * exec_queue_id are completed.
 * Syncobj set must be destroyed by caller.
 */
int
xe_queue_get_syncobj_for_idle(int fd, uint32_t exec_queue_id, uint32_t *syncobj)
{
   struct drm_syncobj_create syncobj_create = {};
   struct drm_xe_sync xe_sync = {
      .type = DRM_XE_SYNC_TYPE_SYNCOBJ,
      .flags = DRM_XE_SYNC_FLAG_SIGNAL,
   };
   struct drm_xe_exec exec = {
      .exec_queue_id = exec_queue_id,
      .num_syncs = 1,
      .syncs = (uintptr_t)&xe_sync,
      .num_batch_buffer = 0,
   };
   struct drm_syncobj_destroy syncobj_destroy = {};
   int ret = intel_ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &syncobj_create);

   if (ret)
      return -errno;

   xe_sync.handle = syncobj_create.handle;
   /* Using the special exec.num_batch_buffer == 0 handling to get syncobj
    * signaled when the last DRM_IOCTL_XE_EXEC is completed.
    */
   ret = intel_ioctl(fd, DRM_IOCTL_XE_EXEC, &exec);
   if (ret) {
      /* exec_queue could have been banned, that is why it is being destroyed
       * so no assert() here
       */
      ret = -errno;
      goto error_exec;
   }

   *syncobj = syncobj_create.handle;
   return 0;

error_exec:
   syncobj_destroy.handle = syncobj_create.handle,
   intel_ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &syncobj_destroy);

   return ret;
}
