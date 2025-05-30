/**************************************************************************
 *
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include <stdint.h>

#include "ac_uvd_dec.h"
#include "util/os_time.h"
#include "util/detect_os.h"
#include "util/bitpack_helpers.h"

#if DETECT_OS_POSIX
#include <unistd.h>
#endif

void ac_uvd_init_stream_handle(struct ac_uvd_stream_handle *handle)
{
#if DETECT_OS_POSIX
   handle->base = util_bitreverse(getpid() ^ os_time_get());
#else
   handle->base = util_bitreverse(os_time_get());
#endif
   handle->counter = 0;
}

unsigned ac_uvd_alloc_stream_handle(struct ac_uvd_stream_handle *handle)
{
   return handle->base ^ ++handle->counter;
}
