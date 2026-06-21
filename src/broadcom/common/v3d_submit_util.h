/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef V3D_MULTISYNC_H
#define V3D_MULTISYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "drm-uapi/v3d_drm.h"

/* Generic allocation interface */
struct v3d_multisync_ops {
   void *(*zalloc)(void *mem_ctx, size_t size);
   void (*free)(void *mem_ctx, void *ptr);
   /* Pointer to object (if any) providing a memory allocation context */
   void *mem_ctx;
};

struct v3d_multisync {
   struct drm_v3d_multi_sync ext;
   struct v3d_multisync_ops ops;
};

void
v3d_multisync_free(struct v3d_multisync *ms);

void
v3d_submit_ext_set(struct drm_v3d_extension *ext,
                   struct drm_v3d_extension *next,
                   uint32_t id,
                   uintptr_t flags);

bool
v3d_multisync_init(struct v3d_multisync *ms,
                   enum v3d_queue wait_stage,
                   const uint32_t *in_handles, uint32_t in_count,
                   const uint32_t *out_handles, uint32_t out_count,
                   struct drm_v3d_extension *next);

#endif /* V3D_MULTISYNC_H */
