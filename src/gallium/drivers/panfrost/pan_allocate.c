/*
 * © Copyright 2018 Alyssa Rosenzweig
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

/* Allocate a mapped chunk directly from a heap */

struct panfrost_transfer
panfrost_allocate_chunk(struct panfrost_context *ctx, size_t size, unsigned heap_id)
{
        size = ALIGN(size, ALIGNMENT);

        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_screen *screen = pan_screen(gallium->screen);

        struct pb_slab_entry *entry = pb_slab_alloc(&screen->slabs, size, heap_id);
        struct panfrost_memory_entry *p_entry = (struct panfrost_memory_entry *) entry;
        struct panfrost_memory *backing = (struct panfrost_memory *) entry->slab;

        struct panfrost_transfer transfer = {
                .cpu = backing->cpu + p_entry->offset,
                .gpu = backing->gpu + p_entry->offset
        };

        return transfer;
}

/* Transient command stream pooling: command stream uploads try to simply copy
 * into whereever we left off. If there isn't space, we allocate a new entry
 * into the pool and copy there */

struct panfrost_transfer
panfrost_allocate_transient(struct panfrost_context *ctx, size_t sz)
{
        /* Pad the size */
        sz = ALIGN(sz, ALIGNMENT);

        /* Check if there is room in the current entry */
        struct panfrost_transient_pool *pool = &ctx->transient_pools[ctx->cmdstream_i];

        if ((pool->entry_offset + sz) > pool->entry_size) {
                /* Don't overflow this entry -- advance to the next */

                pool->entry_offset = 0;

                pool->entry_index++;
                assert(pool->entry_index < PANFROST_MAX_TRANSIENT_ENTRIES);

                /* Check if this entry exists */
                
                if (pool->entry_index >= pool->entry_count) {
                        /* Don't overflow the pool -- allocate a new one */
                        struct pipe_context *gallium = (struct pipe_context *) ctx;
                        struct panfrost_screen *screen = pan_screen(gallium->screen);
                        struct pb_slab_entry *entry = pb_slab_alloc(&screen->slabs, pool->entry_size, HEAP_TRANSIENT);

                        pool->entry_count++;
                        pool->entries[pool->entry_index] = (struct panfrost_memory_entry *) entry;
                }

                /* Make sure we -still- won't overflow */
                assert(sz < pool->entry_size);
        }

        /* We have an entry we can write to, so do the upload! */
        struct panfrost_memory_entry *p_entry = pool->entries[pool->entry_index];
        struct panfrost_memory *backing = (struct panfrost_memory *) p_entry->base.slab;

        struct panfrost_transfer ret = {
                .cpu = backing->cpu + p_entry->offset + pool->entry_offset,
                .gpu = backing->gpu + p_entry->offset + pool->entry_offset
        };

        /* Advance the pointer */
        pool->entry_offset += sz;

        return ret;

}

mali_ptr
panfrost_upload_transient(struct panfrost_context *ctx, const void *data, size_t sz)
{
        struct panfrost_transfer transfer = panfrost_allocate_transient(ctx, sz);
        memcpy(transfer.cpu, data, sz);
        return transfer.gpu;
}

// TODO: An actual allocator, perhaps
// TODO: Multiple stacks for multiple bases?

int hack_stack_bottom = 4096; /* Don't interfere with constant offsets */
int last_offset = 0;

static inline int
pandev_allocate_offset(int *stack, size_t sz)
{
        /* First, align the stack bottom to something nice; it's not critical
         * at this point if we waste a little space to do so. */

        int excess = *stack & (ALIGNMENT - 1);

        /* Add the secret of my */
        if (excess)
                *stack += ALIGNMENT - excess;

        /* Finally, use the new bottom for the allocation and move down the
         * stack */

        int ret = *stack;
        *stack += sz;
        return ret;
}

inline mali_ptr
pandev_upload(int cheating_offset, int *stack_bottom, mali_ptr base, void *base_map, const void *data, size_t sz, bool no_pad)
{
        int offset;

        /* We're not positive about the sizes of all objects, but we don't want
         * them to crash against each other either. Let the caller disable
         * padding if they so choose, though. */

        size_t padded_size = no_pad ? sz : sz * 2;

        /* If no specific bottom is specified, use a global one... don't do
         * this in production, kids */

        if (!stack_bottom)
                stack_bottom = &hack_stack_bottom;

        /* Allocate space for the new GPU object, if required */

        if (cheating_offset == -1) {
                offset = pandev_allocate_offset(stack_bottom, padded_size);
        } else {
                offset = cheating_offset;
                *stack_bottom = offset + sz;
        }

        /* Save last offset for sequential uploads (job descriptors) */
        last_offset = offset + padded_size;

        /* Upload it */
        memcpy((uint8_t *) base_map + offset, data, sz);

        /* Return the GPU address */
        return base + offset;
}

/* Upload immediately after the last allocation */

mali_ptr
pandev_upload_sequential(mali_ptr base, void *base_map, const void *data, size_t sz)
{
        return pandev_upload(last_offset, NULL, base, base_map, data, sz, /* false */ true);
}

/* Simplified APIs for the real driver, rather than replays */

mali_ptr
panfrost_upload(struct panfrost_memory *mem, const void *data, size_t sz, bool no_pad)
{
        /* Bounds check */
        if ((mem->stack_bottom + sz) >= mem->size) {
                printf("Out of memory, tried to upload %zd but only %zd available\n", sz, mem->size - mem->stack_bottom);
                assert(0);
        }

        return pandev_upload(-1, &mem->stack_bottom, mem->gpu, mem->cpu, data, sz, no_pad);
}

mali_ptr
panfrost_upload_sequential(struct panfrost_memory *mem, const void *data, size_t sz)
{
        return pandev_upload(last_offset, &mem->stack_bottom, mem->gpu, mem->cpu, data, sz, true);
}

/* Simplified interface to allocate a chunk without any upload, to allow
 * zero-copy uploads. This is particularly useful when the copy would happen
 * anyway, for instance with texture swizzling. */

void *
panfrost_allocate_transfer(struct panfrost_memory *mem, size_t sz, mali_ptr *gpu)
{
        int offset = pandev_allocate_offset(&mem->stack_bottom, sz);

        *gpu = mem->gpu + offset;
        return mem->cpu + offset;
}
