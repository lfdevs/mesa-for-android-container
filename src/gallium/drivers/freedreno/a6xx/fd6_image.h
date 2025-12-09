/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_IMAGE_H_
#define FD6_IMAGE_H_

#include "freedreno_context.h"

template <chip CHIP>
struct fd_ringbuffer *
fd6_build_bindless_state(struct fd_context *ctx, mesa_shader_stage shader,
                         bool append_fb_read) assert_dt;

template <chip CHIP>
void fd6_image_init(struct pipe_context *pctx);

#endif /* FD6_IMAGE_H_ */
