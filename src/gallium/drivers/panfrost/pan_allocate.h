/*
 * © Copyright 2017-2018 Alyssa Rosenzweig
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

#ifndef __PAN_ALLOCATE_H__
#define __PAN_ALLOCATE_H__

#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>

#include <panfrost-misc.h>

#include "util/list.h"

struct panfrost_context;

/* Represents a fat pointer for GPU-mapped memory, returned from the transient
 * allocator and not used for much else */

struct panfrost_transfer {
        uint8_t *cpu;
        mali_ptr gpu;
};

struct panfrost_bo {
        /* Must be first for casting */
        struct list_head link;

        struct pipe_reference reference;

        /* Mapping for the entire object (all levels) */
        uint8_t *cpu;

        /* GPU address for the object */
        mali_ptr gpu;

        /* Size of all entire trees */
        size_t size;

        int gem_handle;

        uint32_t flags;
};

struct panfrost_memory {
        /* Backing for the slab in memory */
        struct panfrost_bo *bo;
        int stack_bottom;
};

/* Functions for the actual Galliumish driver */
mali_ptr panfrost_upload(struct panfrost_memory *mem, const void *data, size_t sz);

struct panfrost_transfer
panfrost_allocate_transient(struct panfrost_context *ctx, size_t sz);

mali_ptr
panfrost_upload_transient(struct panfrost_context *ctx, const void *data, size_t sz);

static inline mali_ptr
panfrost_reserve(struct panfrost_memory *mem, size_t sz)
{
        mem->stack_bottom += sz;
        return mem->bo->gpu + (mem->stack_bottom - sz);
}

#endif /* __PAN_ALLOCATE_H__ */
