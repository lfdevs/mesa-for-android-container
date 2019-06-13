/*
 * Copyright (C) 2019 Alyssa Rosenzweig
 * Copyright (C) 2014-2017 Broadcom
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

#ifndef __PAN_JOB_H__
#define __PAN_JOB_H__

/* Used as a hash table key */

struct panfrost_job_key {
        struct pipe_surface *cbufs[4];
        struct pipe_surface *zsbuf;
};

#define PAN_REQ_MSAA            (1 << 0)
#define PAN_REQ_DEPTH_WRITE     (1 << 1)

/* A panfrost_job corresponds to a bound FBO we're rendering to,
 * collecting over multiple draws. */

struct panfrost_job {
        struct panfrost_context *ctx;
        struct panfrost_job_key key;

        /* Buffers cleared (PIPE_CLEAR_* bitmask) */
        unsigned clear;

        /* Packed clear values */
        uint32_t clear_color;
        float clear_depth;
        unsigned clear_stencil;

        /* Whether this job uses the corresponding requirement (PAN_REQ_*
         * bitmask) */
        unsigned requirements;

        /* BOs referenced -- will be used for flushing logic */
        struct set *bos;
};

/* Functions for managing the above */

struct panfrost_job *
panfrost_create_job(struct panfrost_context *ctx);

void
panfrost_free_job(struct panfrost_context *ctx, struct panfrost_job *job);

struct panfrost_job *
panfrost_get_job(struct panfrost_context *ctx,
                struct pipe_surface **cbufs, struct pipe_surface *zsbuf);

struct panfrost_job *
panfrost_get_job_for_fbo(struct panfrost_context *ctx);

void
panfrost_job_init(struct panfrost_context *ctx);

void
panfrost_job_add_bo(struct panfrost_job *job, struct panfrost_bo *bo);

void
panfrost_flush_jobs_writing_resource(struct panfrost_context *panfrost,
                                struct pipe_resource *prsc);

void
panfrost_flush_jobs_reading_resource(struct panfrost_context *panfrost,
                                struct pipe_resource *prsc);

#endif
