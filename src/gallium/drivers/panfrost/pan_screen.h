/**************************************************************************
 *
 * Copyright 2018-2019 Alyssa Rosenzweig
 * Copyright 2018-2019 Collabora, Ltd.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef PAN_SCREEN_H
#define PAN_SCREEN_H

#include <xf86drm.h>
#include "pipe/p_screen.h"
#include "pipe/p_defines.h"
#include "renderonly/renderonly.h"
#include "util/u_dynarray.h"
#include "util/bitset.h"

#include <panfrost-misc.h>
#include "pan_allocate.h"

struct panfrost_context;
struct panfrost_resource;
struct panfrost_screen;

/* Driver limits */
#define PAN_MAX_CONST_BUFFERS 16

/* Flags for allocated memory */

/* This memory region is executable */
#define PAN_ALLOCATE_EXECUTE            (1 << 0)

/* This memory region should be lazily allocated and grow-on-page-fault. Must
 * be used in conjunction with INVISIBLE */
#define PAN_ALLOCATE_GROWABLE           (1 << 1)

/* This memory region should not be mapped to the CPU */
#define PAN_ALLOCATE_INVISIBLE          (1 << 2)

/* This memory region will be used for varyings and needs to have the cache
 * bits twiddled accordingly */
#define PAN_ALLOCATE_COHERENT_LOCAL     (1 << 3)

/* This region may not be used immediately and will not mmap on allocate
 * (semantically distinct from INVISIBLE, which cannot never be mmaped) */
#define PAN_ALLOCATE_DELAY_MMAP         (1 << 4)

/* Transient slab size. This is a balance between fragmentation against cache
 * locality and ease of bookkeeping */

#define TRANSIENT_SLAB_PAGES (32) /* 128kb */
#define TRANSIENT_SLAB_SIZE (4096 * TRANSIENT_SLAB_PAGES)

/* Maximum number of transient slabs so we don't need dynamic arrays. Most
 * interesting Mali boards are 4GB RAM max, so if the entire RAM was filled
 * with transient slabs, you could never exceed (4GB / TRANSIENT_SLAB_SIZE)
 * allocations anyway. By capping, we can use a fixed-size bitset for tracking
 * free slabs, eliminating quite a bit of complexity. We can pack the free
 * state of 8 slabs into a single byte, so for 128kb transient slabs the bitset
 * occupies a cheap 4kb of memory */

#define MAX_TRANSIENT_SLABS (1024*1024 / TRANSIENT_SLAB_PAGES)

/* How many power-of-two levels in the BO cache do we want? 2^12
 * minimum chosen as it is the page size that all allocations are
 * rounded to */

#define MIN_BO_CACHE_BUCKET (12) /* 2^12 = 4KB */
#define MAX_BO_CACHE_BUCKET (22) /* 2^22 = 4MB */

/* Fencepost problem, hence the off-by-one */
#define NR_BO_CACHE_BUCKETS (MAX_BO_CACHE_BUCKET - MIN_BO_CACHE_BUCKET + 1)

struct panfrost_screen {
        struct pipe_screen base;
        int fd;

        /* Properties of the GPU in use */
        unsigned gpu_id;
        bool require_sfbd;

        drmVersionPtr kernel_version;

        struct renderonly *ro;

        /* Transient memory management is based on borrowing fixed-size slabs
         * off the screen (loaning them out to the batch). Dynamic array
         * container of panfrost_bo */

        struct util_dynarray transient_bo;

        /* Set of free transient BOs */
        BITSET_DECLARE(free_transient, MAX_TRANSIENT_SLABS);

        /* The BO cache is a set of buckets with power-of-two sizes ranging
         * from 2^12 (4096, the page size) to 2^(12 + MAX_BO_CACHE_BUCKETS).
         * Each bucket is a linked list of free panfrost_bo objects. */

        struct list_head bo_cache[NR_BO_CACHE_BUCKETS];

        /* While we're busy building up the job for frame N, the GPU is
         * still busy executing frame N-1. So hold a reference to
         * yesterjob */
        int last_fragment_flushed;
        struct panfrost_job *last_job;
};

static inline struct panfrost_screen *
pan_screen(struct pipe_screen *p)
{
        return (struct panfrost_screen *)p;
}

/* Get a transient BO off the screen given a
 * particular index */

static inline struct panfrost_bo *
pan_bo_for_index(struct panfrost_screen *screen, unsigned index)
{
        return *(util_dynarray_element(&screen->transient_bo,
                                struct panfrost_bo *, index));
}

void
panfrost_drm_allocate_slab(struct panfrost_screen *screen,
                           struct panfrost_memory *mem,
                           size_t pages,
                           bool same_va,
                           int extra_flags,
                           int commit_count,
                           int extent);
void
panfrost_drm_free_slab(struct panfrost_screen *screen,
                       struct panfrost_memory *mem);
struct panfrost_bo *
panfrost_drm_create_bo(struct panfrost_screen *screen, size_t size,
                       uint32_t flags);
void
panfrost_drm_mmap_bo(struct panfrost_screen *screen, struct panfrost_bo *bo);
void
panfrost_drm_release_bo(struct panfrost_screen *screen, struct panfrost_bo *bo, bool cacheable);
struct panfrost_bo *
panfrost_drm_import_bo(struct panfrost_screen *screen, int fd);
int
panfrost_drm_export_bo(struct panfrost_screen *screen, const struct panfrost_bo *bo);
int
panfrost_drm_submit_vs_fs_job(struct panfrost_context *ctx, bool has_draws,
                              bool is_scanout);
void
panfrost_drm_force_flush_fragment(struct panfrost_context *ctx,
                                  struct pipe_fence_handle **fence);
unsigned
panfrost_drm_query_gpu_version(struct panfrost_screen *screen);
int
panfrost_drm_init_context(struct panfrost_context *ctx);
void
panfrost_drm_fence_reference(struct pipe_screen *screen,
                             struct pipe_fence_handle **ptr,
                             struct pipe_fence_handle *fence);
boolean
panfrost_drm_fence_finish(struct pipe_screen *pscreen,
                          struct pipe_context *ctx,
                          struct pipe_fence_handle *fence,
                          uint64_t timeout);
struct panfrost_bo *
panfrost_bo_cache_fetch(
                struct panfrost_screen *screen,
                size_t size, uint32_t flags);

bool
panfrost_bo_cache_put(
                struct panfrost_screen *screen,
                struct panfrost_bo *bo);

void
panfrost_bo_cache_evict_all(
                struct panfrost_screen *screen);

#endif /* PAN_SCREEN_H */
