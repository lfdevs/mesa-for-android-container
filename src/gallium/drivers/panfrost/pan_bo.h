/*
 * © Copyright 2019 Alyssa Rosenzweig
 * © Copyright 2019 Collabora, Ltd.
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

#ifndef __PAN_BO_H__
#define __PAN_BO_H__

#include <panfrost-misc.h>
#include "pipe/p_state.h"
#include "util/list.h"

struct panfrost_screen;

/* Flags for allocated memory */

/* This memory region is executable */
#define PAN_BO_EXECUTE            (1 << 0)

/* This memory region should be lazily allocated and grow-on-page-fault. Must
 * be used in conjunction with INVISIBLE */
#define PAN_BO_GROWABLE           (1 << 1)

/* This memory region should not be mapped to the CPU */
#define PAN_BO_INVISIBLE          (1 << 2)

/* This memory region will be used for varyings and needs to have the cache
 * bits twiddled accordingly */
#define PAN_BO_COHERENT_LOCAL     (1 << 3)

/* This region may not be used immediately and will not mmap on allocate
 * (semantically distinct from INVISIBLE, which cannot never be mmaped) */
#define PAN_BO_DELAY_MMAP         (1 << 4)

/* Some BOs shouldn't be returned back to the reuse BO cache, use this flag to
 * let the BO logic know about this contraint. */
#define PAN_BO_DONT_REUSE         (1 << 5)

/* BO has been imported */
#define PAN_BO_IMPORTED           (1 << 6)

/* BO has been exported */
#define PAN_BO_EXPORTED           (1 << 7)

/* GPU access flags */

/* BO is either shared (can be accessed by more than one GPU batch) or private
 * (reserved by a specific GPU job). */
#define PAN_BO_ACCESS_PRIVATE         (0 << 0)
#define PAN_BO_ACCESS_SHARED          (1 << 0)

/* BO is being read/written by the GPU */
#define PAN_BO_ACCESS_READ            (1 << 1)
#define PAN_BO_ACCESS_WRITE           (1 << 2)
#define PAN_BO_ACCESS_RW              (PAN_BO_ACCESS_READ | PAN_BO_ACCESS_WRITE)

/* BO is accessed by the vertex/tiler job. */
#define PAN_BO_ACCESS_VERTEX_TILER    (1 << 3)

/* BO is accessed by the fragment job. */
#define PAN_BO_ACCESS_FRAGMENT        (1 << 4)

struct panfrost_bo {
        /* Must be first for casting */
        struct list_head link;

        struct pipe_reference reference;

        struct panfrost_screen *screen;

        /* Mapping for the entire object (all levels) */
        uint8_t *cpu;

        /* GPU address for the object */
        mali_ptr gpu;

        /* Size of all entire trees */
        size_t size;

        int gem_handle;

        uint32_t flags;

        /* Combination of PAN_BO_ACCESS_{READ,WRITE} flags encoding pending
         * GPU accesses to this BO. Useful to avoid calling the WAIT_BO ioctl
         * when the BO is idle.
         */
        uint32_t gpu_access;
};

/* If a BO is accessed for a particular shader stage, will it be in the primary
 * batch (vertex/tiler) or the secondary batch (fragment)? Anything but
 * fragment will be primary, e.g. compute jobs will be considered
 * "vertex/tiler" by analogy */

static inline uint32_t
panfrost_bo_access_for_stage(enum pipe_shader_type stage)
{
        assert(stage == PIPE_SHADER_FRAGMENT ||
               stage == PIPE_SHADER_VERTEX ||
               stage == PIPE_SHADER_COMPUTE);

        return stage == PIPE_SHADER_FRAGMENT ?
               PAN_BO_ACCESS_FRAGMENT :
               PAN_BO_ACCESS_VERTEX_TILER;
}

bool
panfrost_bo_wait(struct panfrost_bo *bo, int64_t timeout_ns,
                 uint32_t access_type);
void
panfrost_bo_reference(struct panfrost_bo *bo);
void
panfrost_bo_unreference(struct panfrost_bo *bo);
struct panfrost_bo *
panfrost_bo_create(struct panfrost_screen *screen, size_t size,
                   uint32_t flags);
void
panfrost_bo_mmap(struct panfrost_bo *bo);
struct panfrost_bo *
panfrost_bo_import(struct panfrost_screen *screen, int fd);
int
panfrost_bo_export(struct panfrost_bo *bo);
void
panfrost_bo_cache_evict_all(struct panfrost_screen *screen);

#endif /* __PAN_BO_H__ */
