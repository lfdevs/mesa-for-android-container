/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "dev/intel_device_info.h"

#ifdef __cplusplus
extern "C" {
#endif

void intel_common_update_device_info(int fd, struct intel_device_info *devinfo);

void
intel_compute_engine_async_threads_limit(const struct intel_device_info *devinfo,
                                         uint32_t hw_threads_in_wg, bool slm_or_barrier_enabled,
                                         bool uses_fence,
                                         uint8_t *ret_pixel_async_compute_thread_limit,
                                         uint8_t *ret_z_pass_async_compute_thread_limit,
                                         uint8_t *ret_np_z_async_throttle_settings);

int
intel_compute_threads_group_dispatch_size(uint32_t hw_threads_in_wg);

/**
 * Convert a number of GRF registers used (grf_used in prog_data) into a
 * number of GRF register blocks supported by the hardware.
 */
unsigned
intel_register_blocks(const struct intel_device_info *devinfo,
                      unsigned grf_used);

/**
 * Returns true if this number of registers can be exactly selected for in
 * hardware.
 */
bool
intel_register_blocks_supported(const struct intel_device_info *devinfo,
                                int num_regs);

#ifdef __cplusplus
}
#endif
