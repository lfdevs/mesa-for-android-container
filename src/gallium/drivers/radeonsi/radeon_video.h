/**************************************************************************
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#ifndef RADEON_VIDEO_H
#define RADEON_VIDEO_H

#include "winsys/radeon_winsys.h"
#include "vl/vl_video_buffer.h"
#include "si_pipe.h"
#include "util/log.h"

#undef  MESA_LOG_TAG
#define MESA_LOG_TAG "radeonsi"

#define RVID_ERR(fmt, args...)                                                                     \
   mesa_loge("%s:%d %s UVD - " fmt, __FILE__, __LINE__, __func__, ##args)

#define UVD_FW_1_66_16 ((1 << 24) | (66 << 16) | (16 << 8))

/* video buffer offset info representation */
struct rvid_buf_offset_info {
   unsigned num_units;
   unsigned old_offset;
   unsigned new_offset;
};

/* generate an stream handle */
unsigned si_vid_alloc_stream_handle(void);

/* reallocate a buffer, preserving its content */
bool si_vid_resize_buffer(struct pipe_context *context,
                          struct si_resource **buf, unsigned new_size,
                          struct rvid_buf_offset_info *buf_ofst_info);

#endif // RADEON_VIDEO_H
