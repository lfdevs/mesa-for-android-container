/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/libcl/libcl.h"

#define PANLIB_XFB_MAX_BUFFERS 4
#define PANLIB_XFB_MAX_STREAMS 4
#define PANLIB_XFB_MAX_OUTPUTS 128

#define PANLIB_XFB_OUT_DST_SHIFT 0
#define PANLIB_XFB_OUT_SRC_SHIFT 12
#define PANLIB_XFB_OUT_BUF_SHIFT 20

static inline uint32_t
panlib_xfb_output(uint32_t dst_dw, uint32_t src_dw, uint32_t buffer)
{
   return (dst_dw << PANLIB_XFB_OUT_DST_SHIFT) |
          (src_dw << PANLIB_XFB_OUT_SRC_SHIFT) |
          (buffer << PANLIB_XFB_OUT_BUF_SHIFT);
}

struct panlib_xfb_params {
   uint64_t buffers[PANLIB_XFB_MAX_BUFFERS];
   uint64_t queries[PANLIB_XFB_MAX_STREAMS];
   uint64_t offsets;
   uint64_t records;
   uint64_t indices;
   uint64_t counts;
   uint32_t sizes[PANLIB_XFB_MAX_BUFFERS];
   uint32_t strides[PANLIB_XFB_MAX_BUFFERS];
   uint32_t record_stride;
   uint32_t record_capacity;
   uint32_t prim;
   uint32_t restart;
   uint32_t gs_max_vertices;
   uint32_t gs_invocations;
   uint32_t buffer_to_stream;
   uint32_t output_count;
   uint32_t active;
   uint32_t outputs[PANLIB_XFB_MAX_OUTPUTS];
};
