/*
 * Copyright © 2014-2017 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef VC5_SCREEN_H
#define VC5_SCREEN_H

#include "pipe/p_screen.h"
#include "os/os_thread.h"
#include "state_tracker/drm_driver.h"
#include "util/list.h"
#include "util/slab.h"
#include "broadcom/common/v3d_debug.h"
#include "broadcom/common/v3d_device_info.h"

struct vc5_bo;

#define VC5_MAX_MIP_LEVELS 12
#define VC5_MAX_TEXTURE_SAMPLERS 32
#define VC5_MAX_SAMPLES 4
#define VC5_MAX_DRAW_BUFFERS 4
#define VC5_MAX_ATTRIBUTES 16

struct vc5_simulator_file;

struct vc5_screen {
        struct pipe_screen base;
        int fd;

        struct v3d_device_info devinfo;

        const char *name;

        /** The last seqno we've completed a wait for.
         *
         * This lets us slightly optimize our waits by skipping wait syscalls
         * if we know the job's already done.
         */
        uint64_t finished_seqno;

        struct slab_parent_pool transfer_pool;

        struct vc5_bo_cache {
                /** List of struct vc5_bo freed, by age. */
                struct list_head time_list;
                /** List of struct vc5_bo freed, per size, by age. */
                struct list_head *size_list;
                uint32_t size_list_size;

                mtx_t lock;

                uint32_t bo_size;
                uint32_t bo_count;
        } bo_cache;

        const struct v3d_compiler *compiler;

        struct util_hash_table *bo_handles;
        mtx_t bo_handles_mutex;

        uint32_t bo_size;
        uint32_t bo_count;

        struct vc5_simulator_file *sim_file;
};

static inline struct vc5_screen *
vc5_screen(struct pipe_screen *screen)
{
        return (struct vc5_screen *)screen;
}

struct pipe_screen *vc5_screen_create(int fd);

void
vc5_fence_init(struct vc5_screen *screen);

struct vc5_fence *
vc5_fence_create(struct vc5_screen *screen, uint64_t seqno);

#endif /* VC5_SCREEN_H */
