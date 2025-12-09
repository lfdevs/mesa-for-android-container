/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/* This file contains the public interface for all RT pipeline stage lowering. */

#ifndef RADV_NIR_RT_STAGE_CPS_H
#define RADV_NIR_RT_STAGE_CPS_H

#include "radv_pipeline_rt.h"

void radv_gather_unused_args(struct radv_ray_tracing_stage_info *info, nir_shader *nir);

void radv_nir_lower_rt_abi_cps(nir_shader *shader, const struct radv_shader_args *args,
                               const struct radv_shader_info *info, uint32_t *stack_size, bool resume_shader,
                               struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline,
                               bool has_position_fetch, const struct radv_ray_tracing_stage_info *traversal_info);
void radv_nir_lower_rt_io_cps(nir_shader *shader);

#endif // RADV_NIR_RT_STAGE_CPS_H
