/*
 * © Copyright 2018 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "util/hash_table.h"
#include "pan_bo.h"
#include "pan_pool.h"

/* TODO: What does this actually have to be? */
#define ALIGNMENT 128

/* Transient command stream pooling: command stream uploads try to simply copy
 * into whereever we left off. If there isn't space, we allocate a new entry
 * into the pool and copy there */

struct pan_pool
panfrost_create_pool(void *memctx, struct panfrost_device *dev)
{
        struct pan_pool pool = {
                .dev = dev,
                .transient_offset = 0,
                .transient_bo = NULL
        };

        pool.bos = _mesa_hash_table_create(memctx, _mesa_hash_pointer,
                        _mesa_key_pointer_equal);


        return pool;
}

struct panfrost_transfer
panfrost_pool_alloc(struct pan_pool *pool, size_t sz)
{
        /* Pad the size */
        sz = ALIGN_POT(sz, ALIGNMENT);

        /* Find or create a suitable BO */
        struct panfrost_bo *bo = NULL;

        unsigned offset = 0;

        bool fits_in_current = (pool->transient_offset + sz) < TRANSIENT_SLAB_SIZE;

        if (likely(pool->transient_bo && fits_in_current)) {
                /* We can reuse the current BO, so get it */
                bo = pool->transient_bo;

                /* Use the specified offset */
                offset = pool->transient_offset;
                pool->transient_offset = offset + sz;
        } else {
                size_t bo_sz = sz < TRANSIENT_SLAB_SIZE ?
                               TRANSIENT_SLAB_SIZE : ALIGN_POT(sz, 4096);

                /* We can't reuse the current BO, but we can create a new one.
                 * We don't know what the BO will be used for, so let's flag it
                 * RW and attach it to both the fragment and vertex/tiler jobs.
                 * TODO: if we want fine grained BO assignment we should pass
                 * flags to this function and keep the read/write,
                 * fragment/vertex+tiler pools separate.
                 */
                bo = panfrost_bo_create(pool->dev, bo_sz, 0);

                uintptr_t flags = PAN_BO_ACCESS_PRIVATE |
                                  PAN_BO_ACCESS_RW |
                                  PAN_BO_ACCESS_VERTEX_TILER |
                                  PAN_BO_ACCESS_FRAGMENT;

                _mesa_hash_table_insert(pool->bos, bo, (void *) flags);

                if (sz < TRANSIENT_SLAB_SIZE) {
                        pool->transient_bo = bo;
                        pool->transient_offset = offset + sz;
                }
        }

        struct panfrost_transfer ret = {
                .cpu = bo->cpu + offset,
                .gpu = bo->gpu + offset,
        };

        return ret;

}

mali_ptr
panfrost_pool_upload(struct pan_pool *pool, const void *data, size_t sz)
{
        struct panfrost_transfer transfer = panfrost_pool_alloc(pool, sz);
        memcpy(transfer.cpu, data, sz);
        return transfer.gpu;
}
