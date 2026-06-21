/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "v3d_submit_util.h"
#include <assert.h>


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
