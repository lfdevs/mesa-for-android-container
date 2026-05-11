/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>
#include <stdbool.h>

#include "util/u_dynarray.h"
#include "common/intel_hang_dump.h"
#include "intel/dev/intel_device_info.h"

bool process_xe_dmp_file(int file_fd, int drm_fd, const struct intel_device_info *devinfo,
                         struct util_dynarray *buffers, void *mem_ctx,
                         struct intel_hang_dump_block_exec *init,
                         struct intel_hang_dump_block_exec *exec,
                         uint32_t  vm_uapi_flags, uint32_t bo_dumpable, bool scratch);
