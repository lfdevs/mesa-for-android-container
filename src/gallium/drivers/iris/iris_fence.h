/*
 * Copyright © 2018 Intel Corporation
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

#ifndef IRIS_FENCE_H
#define IRIS_FENCE_H

#include "util/u_inlines.h"

struct pipe_screen;
struct iris_screen;
struct iris_batch;

struct iris_syncpt {
   struct pipe_reference ref;
   uint32_t handle;
};

void iris_init_context_fence_functions(struct pipe_context *ctx);
void iris_init_screen_fence_functions(struct pipe_screen *screen);

struct iris_syncpt *iris_create_syncpt(struct iris_screen *screen);
void iris_syncpt_destroy(struct iris_screen *, struct iris_syncpt *);
void iris_batch_add_syncpt(struct iris_batch *batch,
                           struct iris_syncpt *syncpt,
                           unsigned flags);
bool iris_wait_syncpt(struct pipe_screen *screen,
                      struct iris_syncpt *syncpt,
                      int64_t timeout_nsec);
static inline void
iris_syncpt_reference(struct iris_screen *screen,
                      struct iris_syncpt **dst,
                      struct iris_syncpt *src)
{
   if (pipe_reference(&(*dst)->ref, &src->ref))
      iris_syncpt_destroy(screen, *dst);

   *dst = src;
}

#endif
