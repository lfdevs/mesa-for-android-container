/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_NIR_RT_TRAVERSAL_SHADER_H
#define RADV_NIR_RT_TRAVERSAL_SHADER_H

#include "radv_pipeline_rt.h"

nir_shader *radv_build_traversal_shader(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline,
                                        struct radv_ray_tracing_stage_info *info);

#endif // RADV_NIR_RT_TRAVERSAL_SHADER_H
