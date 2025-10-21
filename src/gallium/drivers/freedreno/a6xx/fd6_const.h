/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef FD6_CONST_H
#define FD6_CONST_H

#include "fd6_emit.h"

template <chip CHIP>
struct fd_ringbuffer *fd6_build_tess_consts(struct fd6_emit *emit) assert_dt;
template <chip CHIP>
unsigned fd6_user_consts_cmdstream_size(const struct ir3_shader_variant *v);

template <fd6_pipeline_type PIPELINE, chip CHIP>
struct fd_ringbuffer *fd6_build_user_consts(struct fd6_emit *emit) assert_dt;

template <fd6_pipeline_type PIPELINE, chip CHIP>
struct fd_ringbuffer *
fd6_build_driver_params(struct fd6_emit *emit) assert_dt;

template <chip CHIP>
void fd6_emit_cs_driver_params(struct fd_context *ctx, fd_cs &cs,
                               const struct ir3_shader_variant *v,
                               const struct pipe_grid_info *info) assert_dt;
template <chip CHIP>
void fd6_emit_cs_user_consts(struct fd_context *ctx, fd_cs &cs,
                             const struct ir3_shader_variant *v) assert_dt;
template <chip CHIP>
void fd6_emit_immediates(const struct ir3_shader_variant *v, fd_cs &cs) assert_dt;
template <chip CHIP>
void fd6_emit_link_map(struct fd_context *ctx, fd_cs &cs,
                       const struct ir3_shader_variant *producer,
                       const struct ir3_shader_variant *consumer) assert_dt;

#endif /* FD6_CONST_H */
