/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_VIDEO_H
#define SI_VIDEO_H

#include "si_pipe.h"
#include "pipe/p_video_codec.h"

#define RVID_ERR(fmt, args...)                                                                     \
   mesa_loge("%s:%d %s UVD - " fmt, __FILE__, __LINE__, __func__, ##args)

#define UVD_FW_1_66_16 ((1 << 24) | (66 << 16) | (16 << 8))

/* generate a stream handle */
unsigned si_vid_alloc_stream_handle(void);

/* reallocate a buffer, preserving its content */
bool si_vid_resize_buffer(struct pipe_context *context,
                          struct si_resource **buf, unsigned new_size);

struct pipe_video_buffer *si_video_buffer_create(struct pipe_context *pipe,
                                                 const struct pipe_video_buffer *tmpl);

struct pipe_video_buffer *si_video_buffer_create_with_modifiers(struct pipe_context *pipe,
                                                                const struct pipe_video_buffer *tmpl,
                                                                const uint64_t *modifiers,
                                                                unsigned int modifiers_count);

struct pipe_video_codec *si_video_codec_create(struct pipe_context *context,
                                               const struct pipe_video_codec *templ);

#endif
