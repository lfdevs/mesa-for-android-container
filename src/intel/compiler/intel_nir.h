/*
 * Copyright (c) 2015-2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nir.h"

#ifdef __cplusplus
extern "C" {
#endif

struct intel_device_info;

#define intel_nir_tess_field(b, field)                                  \
   nir_ubitfield_extract_imm(b, nir_load_tess_config_intel(b),          \
                             INTEL_TESS_CONFIG_##field##_OFFSET,        \
                             INTEL_TESS_CONFIG_##field##_SIZE)

void intel_nir_apply_tcs_quads_workaround(nir_shader *nir);
bool brw_nir_rebase_const_offset_ubo_loads(nir_shader *shader);
bool intel_nir_blockify_uniform_loads(nir_shader *shader,
                                      const struct intel_device_info *devinfo);
bool intel_nir_clamp_image_1d_2d_array_sizes(nir_shader *shader);
bool intel_nir_clamp_per_vertex_loads(nir_shader *shader);
bool intel_nir_cleanup_resource_intel(nir_shader *shader);

bool intel_nir_lower_non_uniform_barycentric_at_sample(nir_shader *nir);
bool intel_nir_lower_non_uniform_resource_intel(nir_shader *shader);
bool intel_nir_lower_patch_vertices_in(nir_shader *shader,
                                       unsigned input_vertices);
bool intel_nir_lower_patch_vertices_tes(nir_shader *shader);

bool intel_nir_lower_shading_rate_output(nir_shader *nir);
bool intel_nir_lower_sparse_intrinsics(nir_shader *nir);

bool intel_nir_opt_peephole_ffma(nir_shader *shader);
bool intel_nir_opt_peephole_imul32x16(nir_shader *shader);

bool intel_nir_pulls_at_sample(nir_shader *shader);

unsigned intel_nir_split_conversions_cb(const nir_instr *instr, void *data);

bool intel_nir_lower_printf(nir_shader *nir);

#ifdef __cplusplus
}
#endif
