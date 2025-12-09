/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/* This file contains the public interface for all RT pipeline stage lowering. */

#ifndef RADV_NIR_RT_STAGE_MONOLITHIC_H
#define RADV_NIR_RT_STAGE_MONOLITHIC_H

#include "radv_pipeline_rt.h"

void radv_nir_lower_rt_abi_monolithic(nir_shader *shader, const struct radv_shader_args *args, uint32_t *stack_size,
                                      struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline);
void radv_nir_lower_rt_io_monolithic(nir_shader *shader);

#endif // RADV_NIR_RT_STAGE_MONOLITHIC_H
