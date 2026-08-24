/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <assert.h>
#include <errno.h>

#include "util/log.h"
#include "v3d_util.h"
#include "v3d_submit_util.h"


void
v3d_multisync_free(struct v3d_multisync *ms)
{
   if (ms->ext.out_syncs)
      ms->ops.free(ms->ops.mem_ctx, (void *)(uintptr_t)ms->ext.out_syncs);
   if (ms->ext.in_syncs)
      ms->ops.free(ms->ops.mem_ctx, (void *)(uintptr_t)ms->ext.in_syncs);
}

void
v3d_submit_ext_set(struct drm_v3d_extension *ext,
            struct drm_v3d_extension *next,
            uint32_t id,
            uintptr_t flags)
{
   ext->next = (uintptr_t)(void *)next;
   ext->id = id;
   ext->flags = flags;
}

bool
v3d_multisync_init(struct v3d_multisync *ms,
                   enum v3d_queue wait_stage,
                   const uint32_t *in_handles, uint32_t in_count,
                   const uint32_t *out_handles, uint32_t out_count,
                   struct drm_v3d_extension *next)
{
   const struct v3d_multisync_ops *ops = &ms->ops;

   struct drm_v3d_sem *in_syncs = NULL;
   struct drm_v3d_sem *out_syncs = NULL;

   /* allocate and populate wait syncs */
   if (in_count > 0) {
      in_syncs = ops->zalloc(ops->mem_ctx, in_count * sizeof(struct drm_v3d_sem));
      if (!in_syncs)
         return false;

      for (uint32_t i = 0; i < in_count; i++)
         in_syncs[i].handle = in_handles[i];
   }

   /* allocate and populate signal syncs */
   if (out_count > 0) {
      out_syncs = ops->zalloc(ops->mem_ctx, out_count * sizeof(struct drm_v3d_sem));
      if (!out_syncs) {
         /* Clean up previously allocated in_syncs on failure */
         if (in_syncs)
            ops->free(ops->mem_ctx, in_syncs);
         return false;
      }

      for (uint32_t i = 0; i < out_count; i++)
         out_syncs[i].handle = out_handles[i];
   }

   v3d_submit_ext_set(&ms->ext.base, next, DRM_V3D_EXT_ID_MULTI_SYNC, 0);
   ms->ext.wait_stage = wait_stage;

   ms->ext.in_sync_count = in_count;
   ms->ext.in_syncs = (uintptr_t)(void *)in_syncs;

   ms->ext.out_sync_count = out_count;
   ms->ext.out_syncs = (uintptr_t)(void *)out_syncs;

   return true;
}

int
v3d_submit_timestamp_query_ioctl(int fd, uint32_t bo_handle,
                                 const uint32_t *offsets,
                                 const uint32_t *syncs,
                                 uint32_t count,
                                 struct drm_v3d_extension *ext)
{
   struct drm_v3d_timestamp_query timestamp = {0};
   v3d_submit_ext_set(&timestamp.base, ext,
                      DRM_V3D_EXT_ID_CPU_TIMESTAMP_QUERY, 0);
   timestamp.count = count;
   timestamp.offsets = (uintptr_t)(void *)offsets;
   timestamp.syncs = (uintptr_t)(void *)syncs;

   struct drm_v3d_submit_cpu submit = {0};
   submit.bo_handle_count = 1;
   submit.bo_handles = (uintptr_t)(void *)&bo_handle;
   submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
   submit.extensions = (uintptr_t)(void *)&timestamp;

   int ret = v3d_ioctl(fd, DRM_IOCTL_V3D_SUBMIT_CPU, &submit);
   if (ret)
      mesa_loge("Failed to submit timestamp query: %s", strerror(errno));
   return ret;
}
