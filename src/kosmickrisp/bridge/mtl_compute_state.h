/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_types.h"

#include <stdint.h>

mtl_compute_pipeline_state *
mtl_new_compute_pipeline_state(mtl_device *device, mtl_function *function,
                               uint64_t max_total_threads_per_threadgroup);