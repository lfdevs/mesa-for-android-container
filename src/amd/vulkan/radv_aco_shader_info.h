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

#include "ac_hw_stage.h"
#include "aco_shader_info.h"

#define ASSIGN_FIELD(x)    aco_info->x = radv->x
#define ASSIGN_FIELD_CP(x) memcpy(&aco_info->x, &radv->x, sizeof(radv->x))

static inline void radv_aco_convert_ps_epilog_key(struct aco_ps_epilog_info *aco_info,
                                                  const struct radv_ps_epilog_key *radv,
                                                  const struct radv_shader_args *radv_args);

static enum ac_hw_stage
radv_select_hw_stage(const struct radv_shader_info *const info, const enum amd_gfx_level gfx_level)
{
   switch (info->stage) {
   case MESA_SHADER_VERTEX:
      if (info->is_ngg)
         return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
      else if (info->vs.as_es)
         return gfx_level >= GFX9 ? AC_HW_LEGACY_GEOMETRY_SHADER : AC_HW_EXPORT_SHADER;
      else if (info->vs.as_ls)
         return gfx_level >= GFX9 ? AC_HW_HULL_SHADER : AC_HW_LOCAL_SHADER;
      else
         return AC_HW_VERTEX_SHADER;
   case MESA_SHADER_TESS_EVAL:
      if (info->is_ngg)
         return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
      else if (info->tes.as_es)
         return gfx_level >= GFX9 ? AC_HW_LEGACY_GEOMETRY_SHADER : AC_HW_EXPORT_SHADER;
      else
         return AC_HW_VERTEX_SHADER;
   case MESA_SHADER_TESS_CTRL:
      return AC_HW_HULL_SHADER;
   case MESA_SHADER_GEOMETRY:
      if (info->is_ngg)
         return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
      else
         return AC_HW_LEGACY_GEOMETRY_SHADER;
   case MESA_SHADER_MESH:
      return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
   case MESA_SHADER_FRAGMENT:
      return AC_HW_PIXEL_SHADER;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
   case MESA_SHADER_TASK:
   case MESA_SHADER_RAYGEN:
   case MESA_SHADER_ANY_HIT:
   case MESA_SHADER_CLOSEST_HIT:
   case MESA_SHADER_MISS:
   case MESA_SHADER_INTERSECTION:
   case MESA_SHADER_CALLABLE:
      return AC_HW_COMPUTE_SHADER;
   default:
      unreachable("Unsupported HW stage");
   }
}

static inline void
radv_aco_convert_shader_info(struct aco_shader_info *aco_info, const struct radv_shader_info *radv,
                             const struct radv_shader_args *radv_args, const struct radv_pipeline_key *radv_key,
                             const enum amd_gfx_level gfx_level)
{
   ASSIGN_FIELD(wave_size);
   ASSIGN_FIELD(has_ngg_culling);
   ASSIGN_FIELD(has_ngg_early_prim_export);
   ASSIGN_FIELD(workgroup_size);
   ASSIGN_FIELD(vs.tcs_in_out_eq);
   ASSIGN_FIELD(vs.tcs_temp_only_input_mask);
   ASSIGN_FIELD(vs.has_prolog);
   ASSIGN_FIELD(tcs.num_lds_blocks);
   ASSIGN_FIELD(ps.has_epilog);
   ASSIGN_FIELD(ps.num_interp);
   ASSIGN_FIELD(ps.spi_ps_input);
   ASSIGN_FIELD(cs.subgroup_size);
   ASSIGN_FIELD(cs.uses_full_subgroups);
   aco_info->gfx9_gs_ring_lds_size = radv->gs_ring_info.lds_size;
   aco_info->is_trap_handler_shader = radv->type == RADV_SHADER_TYPE_TRAP_HANDLER;
   aco_info->tcs.tess_input_vertices = radv_key->tcs.tess_input_vertices;
   aco_info->image_2d_view_of_3d = radv_key->image_2d_view_of_3d;
   aco_info->ps.epilog_pc = radv_args->ps_epilog_pc;
   aco_info->hw_stage = radv_select_hw_stage(radv, gfx_level);
}

#define ASSIGN_VS_STATE_FIELD(x)    aco_info->state.x = radv->state->x
#define ASSIGN_VS_STATE_FIELD_CP(x) memcpy(&aco_info->state.x, &radv->state->x, sizeof(radv->state->x))
static inline void
radv_aco_convert_vs_prolog_key(struct aco_vs_prolog_info *aco_info, const struct radv_vs_prolog_key *radv,
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
radv_aco_convert_ps_epilog_key(struct aco_ps_epilog_info *aco_info, const struct radv_ps_epilog_key *radv,
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
radv_aco_convert_opts(struct aco_compiler_options *aco_info, const struct radv_nir_compiler_options *radv,
                      const struct radv_shader_args *radv_args)
{
   ASSIGN_FIELD(dump_shader);
   ASSIGN_FIELD(dump_preoptir);
   ASSIGN_FIELD(record_ir);
   ASSIGN_FIELD(record_stats);
   ASSIGN_FIELD(enable_mrt_output_nan_fixup);
   ASSIGN_FIELD(wgp_mode);
   ASSIGN_FIELD(debug.func);
   ASSIGN_FIELD(debug.private_data);
   ASSIGN_FIELD(debug.private_data);
   aco_info->is_opengl = false;
   aco_info->load_grid_size_from_user_sgpr = radv_args->load_grid_size_from_user_sgpr;
   aco_info->optimisations_disabled = radv->key.optimisations_disabled;
   aco_info->gfx_level = radv->info->gfx_level;
   aco_info->family = radv->info->family;
   aco_info->address32_hi = radv->info->address32_hi;
   aco_info->has_ls_vgpr_init_bug = radv->info->has_ls_vgpr_init_bug;
}
#undef ASSIGN_VS_STATE_FIELD
#undef ASSIGN_VS_STATE_FIELD_CP
#undef ASSIGN_FIELD
#undef ASSIGN_FIELD_CP

#endif
