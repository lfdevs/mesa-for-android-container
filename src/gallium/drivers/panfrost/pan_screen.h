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
#include "util/set.h"

#include <panfrost-misc.h>
#include "pan_allocate.h"

struct panfrost_batch;
struct panfrost_context;
struct panfrost_resource;
struct panfrost_screen;

/* Driver limits */
#define PAN_MAX_CONST_BUFFERS 16

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

        pthread_mutex_t active_bos_lock;
        struct set *active_bos;

        pthread_mutex_t bo_cache_lock;

        /* The BO cache is a set of buckets with power-of-two sizes ranging
         * from 2^12 (4096, the page size) to 2^(12 + MAX_BO_CACHE_BUCKETS).
         * Each bucket is a linked list of free panfrost_bo objects. */

        struct list_head bo_cache[NR_BO_CACHE_BUCKETS];
};

static inline struct panfrost_screen *
pan_screen(struct pipe_screen *p)
{
        return (struct panfrost_screen *)p;
}

struct panfrost_fence *
panfrost_fence_create(struct panfrost_context *ctx,
                      struct util_dynarray *fences);

#endif /* PAN_SCREEN_H */
