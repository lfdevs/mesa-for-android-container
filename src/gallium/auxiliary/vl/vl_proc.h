/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef VL_PROC_H
#define VL_PROC_H

#include "pipe/p_video_codec.h"

struct pipe_video_codec *
vl_create_proc(struct pipe_context *context, struct pipe_video_codec *templat);

#endif
