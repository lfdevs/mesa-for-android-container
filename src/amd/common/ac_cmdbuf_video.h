/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_CMDBUF_VIDEO_H
#define AC_CMDBUF_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

struct radeon_info;
struct ac_cmdbuf;
enum amd_ip_type;

#ifdef __cplusplus
extern "C" {
#endif

void
ac_emit_video_write_memory(struct ac_cmdbuf *cs, const struct radeon_info *info,
                           enum amd_ip_type ip_type, uint64_t va, uint64_t value);

void
ac_emit_video_write_timestamp(struct ac_cmdbuf *cs, enum amd_ip_type ip_type, uint64_t va);

#ifdef __cplusplus
}
#endif

#endif
