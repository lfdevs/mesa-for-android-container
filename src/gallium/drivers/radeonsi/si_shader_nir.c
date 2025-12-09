/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "nir_xfb_info.h"
#include "si_pipe.h"
#include "ac_nir.h"
#include "aco_interface.h"
#include "si_shader_internal.h"

bool si_alu_to_scalar_packed_math_filter(const nir_instr *instr, const void *data)
{
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      if (alu->def.bit_size == 16 && alu->def.num_components == 2 &&
          ac_nir_op_supports_packed_math_16bit(alu)) {
         /* ACO requires that all but the first bit of swizzle must be equal. */
         for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
            if ((alu->src[i].swizzle[0] >> 1) != (alu->src[i].swizzle[1] >> 1))
               return true;
         }
         return false;
      }
   }

   return true;
}

static uint8_t si_vectorize_callback(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_alu)
      return 0;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   if (alu->def.bit_size != 16)
      return 1;

   return ac_nir_op_supports_packed_math_16bit(alu) ? 2 : 1;
}

void si_nir_opts(struct si_screen *sscreen, struct nir_shader *nir, bool has_array_temps)
{
   bool progress;

   do {
      progress = false;
      bool lower_alu_to_scalar = false;
      bool lower_phis_to_scalar = false;

      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, nir->options->lower_to_scalar_filter, NULL);
      NIR_PASS(progress, nir, nir_lower_phis_to_scalar, NULL, NULL);

      if (has_array_temps) {
         NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
         NIR_PASS(lower_alu_to_scalar, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
         NIR_PASS(progress, nir, nir_opt_find_array_copies);
      }
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      NIR_PASS(lower_alu_to_scalar, nir, nir_opt_loop);
      /* (Constant) copy propagation is needed for txf with offsets. */
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      /* nir_opt_if_optimize_phi_true_false is disabled on LLVM14 (#6976) */
      NIR_PASS(lower_phis_to_scalar, nir, nir_opt_if,
               nir_opt_if_optimize_phi_true_false);
      NIR_PASS(progress, nir, nir_opt_dead_cf);

      if (lower_alu_to_scalar) {
         NIR_PASS(_, nir, nir_lower_alu_to_scalar, nir->options->lower_to_scalar_filter, NULL);
      }
      if (lower_phis_to_scalar)
         NIR_PASS(_, nir, nir_lower_phis_to_scalar, NULL, NULL);
      progress |= lower_alu_to_scalar | lower_phis_to_scalar;

      NIR_PASS(progress, nir, nir_opt_cse);

      nir_opt_peephole_select_options peephole_select_options = {
         .limit = 8,
         .indirect_load_ok = true,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peephole_select_options);

      /* Needed for algebraic lowering */
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_generate_bfi);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      if (!nir->info.flrp_lowered) {
         unsigned lower_flrp = (nir->options->lower_flrp16 ? 16 : 0) |
                               (nir->options->lower_flrp32 ? 32 : 0) |
                               (nir->options->lower_flrp64 ? 64 : 0);
         assert(lower_flrp);
         NIR_PASS(progress, nir, nir_lower_flrp, lower_flrp, false /* always_precise */);

         /* Nothing should rematerialize any flrps, so we only
          * need to do this lowering once.
          */
         nir->info.flrp_lowered = true;
      }

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_shrink_vectors, true);

      nir_opt_peephole_select_options peephole_discard_options = {
         .limit = 0,
         .discard_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peephole_discard_options);
      if (nir->options->max_unroll_iterations) {
         NIR_PASS(progress, nir, nir_opt_loop_unroll);
      }

      if (nir->info.stage == MESA_SHADER_FRAGMENT)
         NIR_PASS(_, nir, nir_opt_move_discards_to_top);

      if (sscreen->info.has_packed_math_16bit)
         NIR_PASS(progress, nir, nir_opt_vectorize, si_vectorize_callback, NULL);
   } while (progress);

   NIR_PASS(_, nir, nir_lower_var_copies);
}

void si_nir_late_opts(nir_shader *nir)
{
   bool more_late_algebraic = true;
   while (more_late_algebraic) {
      more_late_algebraic = false;
      NIR_PASS(more_late_algebraic, nir, nir_opt_algebraic_late);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_opt_copy_prop);
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_opt_cse);
   }
}

static void si_late_optimize_16bit_samplers(struct si_screen *sscreen, nir_shader *nir)
{
   /* Optimize types of image_sample sources and destinations.
    *
    * The image_sample sources bit sizes are:
    *   nir_tex_src_coord:       a16 ? 16 : 32
    *   nir_tex_src_comparator:  32
    *   nir_tex_src_offset:      32
    *   nir_tex_src_bias:        a16 ? 16 : 32
    *   nir_tex_src_lod:         a16 ? 16 : 32
    *   nir_tex_src_min_lod:     a16 ? 16 : 32
    *   nir_tex_src_ms_index:    a16 ? 16 : 32
    *   nir_tex_src_ddx:         has_g16 ? (g16 ? 16 : 32) : (a16 ? 16 : 32)
    *   nir_tex_src_ddy:         has_g16 ? (g16 ? 16 : 32) : (a16 ? 16 : 32)
    *
    * We only use a16/g16 if all of the affected sources are 16bit.
    */
   bool has_g16 = sscreen->info.gfx_level >= GFX10;
   struct nir_opt_tex_srcs_options opt_srcs_options[] = {
      {
         .sampler_dims =
            ~(BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE) | BITFIELD_BIT(GLSL_SAMPLER_DIM_BUF)),
         .src_types = (1 << nir_tex_src_coord) | (1 << nir_tex_src_lod) |
                      (1 << nir_tex_src_bias) | (1 << nir_tex_src_min_lod) |
                      (1 << nir_tex_src_ms_index) |
                      (has_g16 ? 0 : (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy)),
      },
      {
         .sampler_dims = ~BITFIELD_BIT(GLSL_SAMPLER_DIM_CUBE),
         .src_types = (1 << nir_tex_src_ddx) | (1 << nir_tex_src_ddy),
      },
   };
   struct nir_opt_16bit_tex_image_options opt_16bit_options = {
      .rounding_mode = nir_rounding_mode_undef,
      .opt_tex_dest_types = nir_type_float | nir_type_int | nir_type_uint,
      .opt_image_dest_types = nir_type_float | nir_type_int | nir_type_uint,
      .integer_dest_saturates = true,
      .opt_image_store_data = true,
      .opt_image_srcs = true,
      .opt_srcs_options_count = has_g16 ? 2 : 1,
      .opt_srcs_options = opt_srcs_options,
   };
   bool changed = false;
   NIR_PASS(changed, nir, nir_opt_16bit_tex_image, &opt_16bit_options);

   if (changed) {
      si_nir_opts(sscreen, nir, false);
      si_nir_late_opts(nir);
   }
}

static bool
lower_intrinsic_filter(const nir_instr *instr, const void *dummy)
{
   return instr->type == nir_instr_type_intrinsic;
}

static nir_def *
lower_intrinsic_instr(nir_builder *b, nir_instr *instr, void *dummy)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_is_sparse_texels_resident:
      /* code==0 means sparse texels are resident */
      return nir_ieq_imm(b, intrin->src[0].ssa, 0);
   case nir_intrinsic_sparse_residency_code_and:
      return nir_ior(b, intrin->src[0].ssa, intrin->src[1].ssa);
   default:
      return NULL;
   }
}

static bool si_lower_intrinsics(nir_shader *nir)
{
   return nir_shader_lower_instructions(nir,
                                        lower_intrinsic_filter,
                                        lower_intrinsic_instr,
                                        NULL);
}

static bool can_lower_mediump_io(mesa_shader_stage prev_stage, bool prev_stage_has_xfb,
                                 mesa_shader_stage next_stage, bool config_option)
{
   /* This is the filter that determines when mediump IO is lowered.
    *
    * NOTE: LLVM fails to compile this test if VS inputs are 16-bit:
    * dEQP-GLES31.functional.shaders.builtin_functions.integer.bitfieldinsert.uvec3_lowp_geometry
    */
   return (prev_stage == MESA_SHADER_VERTEX && next_stage == MESA_SHADER_FRAGMENT &&
           !prev_stage_has_xfb && config_option) ||
          prev_stage == MESA_SHADER_FRAGMENT;
}

static void lower_mediump_io(nir_shader *nir, bool config_option)
{
   nir_variable_mode modes = 0;

   if (can_lower_mediump_io(nir->info.stage, nir->xfb_info != NULL, nir->info.next_stage,
                            config_option))
      modes |= nir_var_shader_out;

   if (can_lower_mediump_io(nir->info.prev_stage, nir->info.prev_stage_has_xfb, nir->info.stage,
                            config_option))
      modes |= nir_var_shader_in;

   if (modes) {
      NIR_PASS(_, nir, nir_lower_mediump_io, modes,
               VARYING_BIT_PNTC | BITFIELD64_RANGE(VARYING_SLOT_VAR0, 32), true);
   }
   NIR_PASS(_, nir, nir_clear_mediump_io_flag);
}

void si_lower_mediump_io_default(nir_shader *nir)
{
   lower_mediump_io(nir, false);
}

void si_lower_mediump_io_option(nir_shader *nir)
{
   lower_mediump_io(nir, true);
}

/**
 * Perform "lowering" operations on the NIR that are run once when the shader
 * selector is created.
 */
static void si_lower_nir(struct si_screen *sscreen, struct nir_shader *nir)
{
   /* Perform lowerings (and optimizations) of code.
    *
    * Performance considerations aside, we must:
    * - lower certain ALU operations
    * - ensure constant offsets for texture instructions are folded
    *   and copy-propagated
    */
   const struct nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0u,
      .lower_txf_offset = true,
      .lower_txs_cube_array = true,
      .lower_invalid_implicit_lod = true,
      .lower_tg4_offsets = true,
      .lower_to_fragment_fetch_amd = sscreen->info.gfx_level < GFX11,
      .lower_1d = sscreen->info.gfx_level == GFX9,
      .optimize_txd = true,
   };
   NIR_PASS(_, nir, nir_lower_tex, &lower_tex_options);

   const struct nir_lower_image_options lower_image_options = {
      .lower_cube_size = true,
      .lower_to_fragment_mask_load_amd = sscreen->info.gfx_level < GFX11 &&
                                         !(sscreen->debug_flags & DBG(NO_FMASK)),
   };
   NIR_PASS(_, nir, nir_lower_image, &lower_image_options);

   NIR_PASS(_, nir, si_lower_intrinsics);

   NIR_PASS(_, nir, ac_nir_lower_sin_cos);

   /* Lower load constants to scalar and then clean up the mess */
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_opt_intrinsics);
   NIR_PASS(_, nir, nir_lower_system_values);

   /* si_nir_kill_outputs and ac_nir_optimize_outputs require outputs to be scalar. */
   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_TESS_EVAL ||
       nir->info.stage == MESA_SHADER_GEOMETRY ||
       nir->info.stage == MESA_SHADER_MESH)
      NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);

   if (nir->info.stage == MESA_SHADER_GEOMETRY) {
      unsigned flags = nir_lower_gs_intrinsics_per_stream;
      if (sscreen->use_ngg) {
         flags |= nir_lower_gs_intrinsics_count_primitives |
            nir_lower_gs_intrinsics_count_vertices_per_primitive |
            nir_lower_gs_intrinsics_overwrite_incomplete;
      }

      NIR_PASS(_, nir, nir_lower_gs_intrinsics, flags);
   } else if (nir->info.stage == MESA_SHADER_TASK) {
      NIR_PASS(_, nir, ac_nir_lower_task_outputs_to_mem,
               sscreen->task_info.payload_entry_size,
               sscreen->task_info.num_entries, false);
   } else if (nir->info.stage == MESA_SHADER_MESH) {
      NIR_PASS(_, nir, ac_nir_lower_mesh_inputs_to_mem,
               sscreen->task_info.payload_entry_size,
               sscreen->task_info.num_entries);
   }

   if (mesa_shader_stage_is_compute(nir->info.stage)) {
      nir_lower_compute_system_values_options options = {0};

      /* gl_LocalInvocationIndex must be derived from gl_LocalInvocationID.xyz to make it correct
       * with quad derivatives. Using gl_SubgroupID for that (which is what we do by default) is
       * incorrect with a non-linear thread order.
       *
       * On Gfx12, we always use a non-linear thread order if the workgroup X and Y size is
       * divisible by 2.
       */
      options.lower_local_invocation_index =
         nir->info.derivative_group == DERIVATIVE_GROUP_QUADS ||
         (sscreen->info.gfx_level >= GFX12 &&
          nir->info.derivative_group == DERIVATIVE_GROUP_NONE &&
          (nir->info.workgroup_size_variable ||
           (nir->info.workgroup_size[0] % 2 == 0 && nir->info.workgroup_size[1] % 2 == 0)));
      NIR_PASS(_, nir, nir_lower_compute_system_values, &options);

      /* Gfx12 supports this in hw. */
      if (sscreen->info.gfx_level < GFX12 &&
          nir->info.derivative_group == DERIVATIVE_GROUP_QUADS) {
         nir_opt_cse(nir); /* CSE load_local_invocation_id */
         memset(&options, 0, sizeof(options));
         options.shuffle_local_ids_for_quad_derivatives = true;
         NIR_PASS(_, nir, nir_lower_compute_system_values, &options);
      }
   }

   if (nir->info.stage == MESA_SHADER_MESH && !sscreen->info.mesh_fast_launch_2) {
      nir_lower_compute_system_values_options options = {
         /* Mesh shaders run as NGG which can implement local_invocation_index from
          * the wave ID in merged_wave_info, but they don't have local_invocation_ids
          * in FAST_LAUNCH=1 mode (the default on GFX10.3, deprecated on GFX11).
          */
         .lower_cs_local_id_to_index = true,
         /* Mesh shaders only have a 1D "vertex index" which we use
          * as "workgroup index" to emulate the 3D workgroup ID.
          */
         .lower_workgroup_id_to_index = true,
         .shortcut_1d_workgroup_id = true,
      };
      NIR_PASS(_, nir, nir_lower_compute_system_values, &options);
   }

   si_nir_opts(sscreen, nir, true);
   /* Run late optimizations to fuse ffma and eliminate 16-bit conversions. */
   si_nir_late_opts(nir);

   if (sscreen->info.gfx_level >= GFX9)
      si_late_optimize_16bit_samplers(sscreen, nir);

   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);

   NIR_PASS(_, nir, nir_lower_fp16_casts, nir_lower_fp16_split_fp64);
}

void si_finalize_nir(struct pipe_screen *screen, struct nir_shader *nir,
                     bool optimize)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   if (nir->info.io_lowered) {
      nir_foreach_variable_with_modes(var, nir, nir_var_shader_in | nir_var_shader_out) {
         UNREACHABLE("no IO variables should be present with lowered IO");
      }
   } else {
      nir_lower_io_passes(nir, false);
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_in | nir_var_shader_out, NULL);
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, si_nir_lower_color_inputs_to_sysvals);
      NIR_PASS(_, nir, nir_recompute_io_bases, nir_var_shader_out);
   }

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared, nir_address_format_32bit_offset);

   /* Remove dead derefs, so that we can remove uniforms. */
   NIR_PASS(_, nir, nir_opt_dce);

   /* Remove uniforms because those should have been lowered to UBOs already. */
   nir_foreach_variable_with_modes_safe(var, nir, nir_var_uniform) {
      if (!glsl_type_get_image_count(var->type) &&
          !glsl_type_get_texture_count(var->type) &&
          !glsl_type_get_sampler_count(var->type))
         exec_node_remove(&var->node);
   }

   si_lower_nir(sscreen, nir);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* Update xfb info after we did medium io lowering. */
   if (nir->xfb_info && nir->info.outputs_written_16bit)
      nir_gather_xfb_info_from_intrinsics(nir);

   if (sscreen->options.inline_uniforms)
      nir_find_inlinable_uniforms(nir);

   /* Lower large variables that are always constant with load_constant intrinsics, which
    * get turned into PC-relative loads from a data section next to the shader.
    *
    * Run this once before lcssa because the added phis may prevent this
    * pass from operating correctly.
    *
    * nir_opt_large_constants may use op_amul (see nir_build_deref_offset),
    * or may create unneeded code, so run si_nir_opts if needed.
    */
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   bool progress = false;
   NIR_PASS(progress, nir, nir_opt_large_constants, glsl_get_natural_size_align_bytes, 16);
   if (progress)
      si_nir_opts(sscreen, nir, false);

   NIR_PASS(_, nir, si_nir_mark_divergent_texture_non_uniform);

   /* IO must be scalar when this is called. */
   if (nir->info.stage <= MESA_SHADER_GEOMETRY && nir->info.stage != MESA_SHADER_TESS_CTRL)
      NIR_PASS(_, nir, nir_opt_clip_cull_const);

   /* Require divergence analysis to identify divergent loops. */
   nir_metadata_require(nir_shader_get_entrypoint(nir), nir_metadata_divergence);
}
