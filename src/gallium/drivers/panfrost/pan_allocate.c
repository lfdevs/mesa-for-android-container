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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <panfrost-misc.h>
#include <panfrost-job.h>
#include "pan_context.h"

/* TODO: What does this actually have to be? */
#define ALIGNMENT 128

/* Allocate a new transient slab */

static struct panfrost_bo *
panfrost_create_slab(struct panfrost_screen *screen, unsigned *index)
{
        /* Allocate a new slab on the screen */

        struct panfrost_bo **new =
                util_dynarray_grow(&screen->transient_bo,
                                struct panfrost_bo *, 1);

        struct panfrost_bo *alloc = panfrost_drm_create_bo(screen, TRANSIENT_SLAB_SIZE, 0);

        *new = alloc;

        /* Return the BO as well as the index we just added */

        *index = util_dynarray_num_elements(&screen->transient_bo, void *) - 1;
        return alloc;
}

/* Transient command stream pooling: command stream uploads try to simply copy
 * into whereever we left off. If there isn't space, we allocate a new entry
 * into the pool and copy there */

struct panfrost_transfer
panfrost_allocate_transient(struct panfrost_context *ctx, size_t sz)
{
        struct panfrost_screen *screen = pan_screen(ctx->base.screen);
        struct panfrost_job *batch = panfrost_get_job_for_fbo(ctx);

        /* Pad the size */
        sz = ALIGN_POT(sz, ALIGNMENT);

        /* Find or create a suitable BO */
        struct panfrost_bo *bo = NULL;

        unsigned offset = 0;
        bool update_offset = false;

        bool has_current = batch->transient_indices.size;
        bool fits_in_current = (batch->transient_offset + sz) < TRANSIENT_SLAB_SIZE;

        if (likely(has_current && fits_in_current)) {
                /* We can reuse the topmost BO, so get it */
                unsigned idx = util_dynarray_top(&batch->transient_indices, unsigned);
                bo = pan_bo_for_index(screen, idx);

                /* Use the specified offset */
                offset = batch->transient_offset;
                update_offset = true;
        } else if (sz < TRANSIENT_SLAB_SIZE) {
                /* We can't reuse the topmost BO, but we can get a new one.
                 * First, look for a free slot */

                unsigned count = util_dynarray_num_elements(&screen->transient_bo, void *);
                unsigned index = 0;

                unsigned free = __bitset_ffs(
                                screen->free_transient,
                                count / BITSET_WORDBITS);

                if (likely(free)) {
                        /* Use this one */
                        index = free - 1;

                        /* It's ours, so no longer free */
                        BITSET_CLEAR(screen->free_transient, index);

                        /* Grab the BO */
                        bo = pan_bo_for_index(screen, index);
                } else {
                        /* Otherwise, create a new BO */
                        bo = panfrost_create_slab(screen, &index);
                }

                /* Remember we created this */
                util_dynarray_append(&batch->transient_indices, unsigned, index);

                update_offset = true;
        } else {
                /* Create a new BO and reference it */
                bo = panfrost_drm_create_bo(screen, ALIGN_POT(sz, 4096), 0);
                panfrost_job_add_bo(batch, bo);

                /* Creating a BO adds a reference, and then the job adds a
                 * second one. So we need to pop back one reference */
                panfrost_bo_unreference(&screen->base, bo);
        }

        struct panfrost_transfer ret = {
                .cpu = bo->cpu + offset,
                .gpu = bo->gpu + offset,
        };

        if (update_offset)
                batch->transient_offset = offset + sz;

        return ret;

}

mali_ptr
panfrost_upload_transient(struct panfrost_context *ctx, const void *data, size_t sz)
{
        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, sz);
        memcpy(transfer.cpu, data, sz);
        return transfer.gpu;
}

/* The code below is exclusively for the use of shader memory and is subject to
 * be rewritten soon enough since it never frees the memory it allocates. Here
 * be dragons, etc. */

mali_ptr
panfrost_upload(struct panfrost_memory *mem, const void *data, size_t sz)
{
        size_t aligned_sz = ALIGN_POT(sz, ALIGNMENT);

        /* Bounds check */
        if ((mem->stack_bottom + aligned_sz) >= mem->bo->size) {
                printf("Out of memory, tried to upload %zd but only %zd available\n",
                       sz, mem->bo->size - mem->stack_bottom);
                assert(0);
        }

        memcpy((uint8_t *) mem->bo->cpu + mem->stack_bottom, data, sz);
        mali_ptr gpu = mem->bo->gpu + mem->stack_bottom;

        mem->stack_bottom += aligned_sz;
        return gpu;
}
