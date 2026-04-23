/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/* This file contains the public interface for all RT pipeline stage lowering. */

#ifndef RADV_NIR_RT_STAGE_FUNCTIONS_H
#define RADV_NIR_RT_STAGE_FUNCTIONS_H

#include "radv_pipeline_rt.h"

nir_function_impl *radv_get_rt_shader_entrypoint(nir_shader *shader);

void radv_nir_init_rt_function_params(nir_function *function, mesa_shader_stage stage, unsigned payload_size,
                                      unsigned hit_attrib_size, bool uses_descriptor_heap);

void radv_nir_lower_rt_abi_functions(nir_shader *shader, const struct radv_shader_info *info, uint32_t payload_size,
                                     uint32_t hit_attrib_size, struct radv_device *device,
                                     struct radv_ray_tracing_pipeline *pipeline);
void radv_nir_lower_rt_io_functions(nir_shader *shader);

#endif // RADV_NIR_RT_STAGE_FUNCTIONS_H
