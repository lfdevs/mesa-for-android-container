/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_NIR_RT_TRAVERSAL_SHADER_H
#define RADV_NIR_RT_TRAVERSAL_SHADER_H

#include "radv_pipeline_rt.h"

typedef void (*radv_nir_traversal_preprocess_cb)(nir_shader *nir);

void radv_nir_lower_intersection_shader(nir_shader *intersection, nir_shader *any_hit);

nir_shader *radv_build_traversal_shader(const struct radv_compiler_info *compiler_info,
                                        struct radv_ray_tracing_pipeline *pipeline,
                                        struct radv_ray_tracing_stage_info *info,
                                        radv_nir_traversal_preprocess_cb preprocess, uint32_t payload_size,
                                        uint32_t hit_attrib_size);

#endif // RADV_NIR_RT_TRAVERSAL_SHADER_H
