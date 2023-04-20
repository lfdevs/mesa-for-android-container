/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef RADV_ACO_SHADER_INFO_H
#define RADV_ACO_SHADER_INFO_H

/* this will convert from radv shader info to the ACO one. */

#include "aco_shader_info.h"

#define ASSIGN_FIELD(x) aco_info->x = radv->x
#define ASSIGN_FIELD_CP(x) memcpy(&aco_info->x, &radv->x, sizeof(radv->x))

static inline void radv_aco_convert_ps_epilog_key(struct aco_ps_epilog_info *aco_info,
                                                  const struct radv_ps_epilog_key *radv,
                                                  const struct radv_shader_args *radv_args);

static inline void
radv_aco_convert_shader_info(struct aco_shader_info *aco_info, const struct radv_shader_info *radv,
                             const struct radv_shader_args *radv_args,
                             const struct radv_pipeline_key *radv_key)
{
   radv_aco_convert_ps_epilog_key(&aco_info->ps.epilog, &radv_key->ps.epilog, radv_args);
   ASSIGN_FIELD(wave_size);
   ASSIGN_FIELD(is_ngg);
   ASSIGN_FIELD(has_ngg_culling);
   ASSIGN_FIELD(has_ngg_early_prim_export);
   ASSIGN_FIELD(workgroup_size);
   ASSIGN_FIELD(vs.as_es);
   ASSIGN_FIELD(vs.as_ls);
   ASSIGN_FIELD(vs.tcs_in_out_eq);
   ASSIGN_FIELD(vs.tcs_temp_only_input_mask);
   ASSIGN_FIELD(vs.use_per_attribute_vb_descs);
   ASSIGN_FIELD(vs.input_slot_usage_mask);
   ASSIGN_FIELD(vs.has_prolog);
   ASSIGN_FIELD(vs.dynamic_inputs);
   ASSIGN_FIELD_CP(gs.output_usage_mask);
   ASSIGN_FIELD_CP(gs.num_stream_output_components);
   ASSIGN_FIELD_CP(gs.output_streams);
   ASSIGN_FIELD(gs.vertices_out);
   ASSIGN_FIELD(tcs.num_lds_blocks);
   ASSIGN_FIELD(tes.as_es);
   ASSIGN_FIELD(ps.writes_z);
   ASSIGN_FIELD(ps.writes_stencil);
   ASSIGN_FIELD(ps.writes_sample_mask);
   ASSIGN_FIELD(ps.has_epilog);
   ASSIGN_FIELD(ps.num_interp);
   ASSIGN_FIELD(ps.spi_ps_input);
   ASSIGN_FIELD(cs.subgroup_size);
   ASSIGN_FIELD(cs.uses_full_subgroups);
   aco_info->gfx9_gs_ring_lds_size = radv->gs_ring_info.lds_size;
   aco_info->is_trap_handler_shader = radv_args->type == RADV_SHADER_TYPE_TRAP_HANDLER;
   aco_info->tcs.tess_input_vertices = radv_key->tcs.tess_input_vertices;
   aco_info->ps.alpha_to_coverage_via_mrtz = radv_key->ps.alpha_to_coverage_via_mrtz;
   aco_info->image_2d_view_of_3d = radv_key->image_2d_view_of_3d;
}

#define ASSIGN_VS_STATE_FIELD(x) aco_info->state.x = radv->state->x
#define ASSIGN_VS_STATE_FIELD_CP(x) memcpy(&aco_info->state.x, &radv->state->x, sizeof(radv->state->x))
static inline void
radv_aco_convert_vs_prolog_key(struct aco_vs_prolog_info *aco_info,
                               const struct radv_vs_prolog_key *radv,
                               const struct radv_shader_args *radv_args)
{
   ASSIGN_VS_STATE_FIELD(instance_rate_inputs);
   ASSIGN_VS_STATE_FIELD(nontrivial_divisors);
   ASSIGN_VS_STATE_FIELD(post_shuffle);
   ASSIGN_VS_STATE_FIELD(alpha_adjust_lo);
   ASSIGN_VS_STATE_FIELD(alpha_adjust_hi);
   ASSIGN_VS_STATE_FIELD_CP(divisors);
   ASSIGN_VS_STATE_FIELD_CP(formats);
   ASSIGN_FIELD(num_attributes);
   ASSIGN_FIELD(misaligned_mask);
   ASSIGN_FIELD(is_ngg);
   ASSIGN_FIELD(next_stage);

   aco_info->inputs = radv_args->prolog_inputs;
}

static inline void
radv_aco_convert_ps_epilog_key(struct aco_ps_epilog_info *aco_info,
                               const struct radv_ps_epilog_key *radv,
                               const struct radv_shader_args *radv_args)
{
   ASSIGN_FIELD(spi_shader_col_format);
   ASSIGN_FIELD(color_is_int8);
   ASSIGN_FIELD(color_is_int10);
   ASSIGN_FIELD(mrt0_is_dual_src);

   memcpy(aco_info->inputs, radv_args->ps_epilog_inputs, sizeof(aco_info->inputs));
   aco_info->pc = radv_args->ps_epilog_pc;
}

static inline void
radv_aco_convert_opts(struct aco_compiler_options *aco_info,
                      const struct radv_nir_compiler_options *radv,
                      const struct radv_shader_args *radv_args)
{
   ASSIGN_FIELD(robust_buffer_access);
   ASSIGN_FIELD(dump_shader);
   ASSIGN_FIELD(dump_preoptir);
   ASSIGN_FIELD(record_ir);
   ASSIGN_FIELD(record_stats);
   ASSIGN_FIELD(has_ls_vgpr_init_bug);
   ASSIGN_FIELD(enable_mrt_output_nan_fixup);
   ASSIGN_FIELD(wgp_mode);
   ASSIGN_FIELD(family);
   ASSIGN_FIELD(gfx_level);
   ASSIGN_FIELD(address32_hi);
   ASSIGN_FIELD(debug.func);
   ASSIGN_FIELD(debug.private_data);
   ASSIGN_FIELD(debug.private_data);
   aco_info->load_grid_size_from_user_sgpr = radv_args->load_grid_size_from_user_sgpr;
   aco_info->optimisations_disabled = radv->key.optimisations_disabled;
}
#undef ASSIGN_VS_STATE_FIELD
#undef ASSIGN_VS_STATE_FIELD_CP
#undef ASSIGN_FIELD
#undef ASSIGN_FIELD_CP

#endif
