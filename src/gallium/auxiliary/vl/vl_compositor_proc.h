/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef VL_COMPOSITOR_PROC_H
#define VL_COMPOSITOR_PROC_H

#include "pipe/p_video_codec.h"

struct pipe_video_codec *
vl_compositor_create_proc(struct pipe_context *context, bool compute_only);

#endif
