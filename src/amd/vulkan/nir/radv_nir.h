/*
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_NIR_H
#define RADV_NIR_H

#include <stdbool.h>
#include <stdint.h>
#include "amd_family.h"
#include "nir_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nir_shader nir_shader;
struct radv_shader_stage;
struct radv_shader_info;
struct radv_shader_args;
struct radv_shader_layout;
struct radv_graphics_state_key;
struct radv_ps_epilog_key;
struct radv_debug_nir;
struct radv_compiler_info;

bool radv_nir_lower_descriptors(nir_shader *shader, const struct radv_compiler_info *compiler_info,
                                const struct radv_shader_stage *stage);

bool radv_nir_lower_abi(nir_shader *shader, enum amd_gfx_level gfx_level, const struct radv_shader_stage *stage,
                        const struct radv_graphics_state_key *gfx_state, uint32_t address32_hi);

bool radv_nir_lower_hit_attrib_derefs(nir_shader *shader);

bool radv_nir_lower_ray_payload_derefs(nir_shader *shader, uint32_t offset);

bool radv_nir_lower_ray_queries(nir_shader *shader, const struct radv_compiler_info *compiler_info);

bool radv_nir_lower_vs_inputs(nir_shader *shader, const struct radv_compiler_info *compiler_info,
                              const struct radv_shader_stage *vs_stage,
                              const struct radv_graphics_state_key *gfx_state);

bool radv_nir_optimize_vs_inputs_to_const(nir_shader *shader, const struct radv_graphics_state_key *gfx_state);

bool radv_nir_lower_primitive_shading_rate(nir_shader *nir, enum amd_gfx_level gfx_level);

bool radv_nir_lower_fs_intrinsics(nir_shader *nir, const struct radv_shader_stage *fs_stage,
                                  const struct radv_graphics_state_key *gfx_state);

bool radv_nir_lower_fs_input_attachment(nir_shader *nir);

bool radv_nir_lower_fs_barycentric(nir_shader *shader, const struct radv_graphics_state_key *gfx_state,
                                   unsigned rast_prim);

bool radv_nir_lower_intrinsics_early(nir_shader *nir, bool lower_view_index_to_zero);

bool radv_nir_lower_view_index(nir_shader *nir);

bool radv_nir_export_multiview(nir_shader *nir);

unsigned radv_map_io_driver_location(unsigned semantic);

void radv_nir_lower_io(nir_shader *nir);

bool radv_nir_lower_io_to_mem(const struct radv_compiler_info *compiler_info, struct radv_shader_stage *stage);

bool radv_nir_lower_cooperative_matrix(nir_shader *shader, enum amd_gfx_level gfx_level, unsigned wave_size);

bool radv_nir_opt_cooperative_matrix(nir_shader *shader, enum amd_gfx_level gfx_level);

bool radv_nir_lower_draw_id_to_zero(nir_shader *shader);

bool radv_nir_remap_color_attachment(nir_shader *shader, const struct radv_graphics_state_key *gfx_state);

bool radv_nir_trim_fs_color_exports(nir_shader *shader, const struct radv_ps_epilog_key *epilog_key);

bool radv_nir_lower_printf(nir_shader *shader, struct radv_debug_nir *debug_nir);

typedef struct radv_nir_opt_tid_function_options {
   bool use_masked_swizzle_amd : 1;
   bool use_dpp16_shift_amd : 1;
   bool use_shuffle_xor : 1;
   bool use_clustered_rotate : 1;
   /* The can be smaller than the api subgroup/ballot size
    * if some invocations are always inactive.
    */
   uint8_t hw_subgroup_size;
   uint8_t hw_ballot_bit_size;
   uint8_t hw_ballot_num_comp;
} radv_nir_opt_tid_function_options;

bool radv_nir_opt_tid_function(nir_shader *shader, const radv_nir_opt_tid_function_options *options);

bool radv_nir_opt_fs_builtins(nir_shader *shader, const struct radv_graphics_state_key *gfx_state,
                              unsigned vgt_outprim_type);

bool radv_nir_lower_immediate_samplers(nir_shader *shader, const struct radv_compiler_info *compiler_info,
                                       const struct radv_shader_stage *stage);

void radv_nir_lower_callee_signature(nir_function *function);

bool radv_nir_lower_call_abi(nir_shader *shader, unsigned wave_size);

#ifdef __cplusplus
}
#endif

#endif /* RADV_NIR_H */
