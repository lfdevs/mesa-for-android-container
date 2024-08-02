/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "dev/intel_device_info.h"

uint32_t intel_compute_slm_calculate_size(unsigned gen, uint32_t bytes);
uint32_t intel_compute_slm_encode_size(unsigned gen, uint32_t bytes);
uint32_t intel_compute_preferred_slm_calc_encode_size(const struct intel_device_info *devinfo,
                                                      const uint32_t slm_size_per_workgroup,
                                                      const uint32_t invocations_per_workgroup,
                                                      const uint8_t cs_simd);
