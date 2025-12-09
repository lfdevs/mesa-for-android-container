/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_rtld.h"
#include "ac_shader_util.h"
#include "nir_builder.h"
#include "nir_serialize.h"
#include "nir_tcs_info.h"
#include "nir_xfb_info.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "pipe/p_shader_tokens.h"

static void si_fix_resource_usage(struct si_screen *sscreen, struct si_shader *shader);

/* Get the number of all interpolated inputs */
unsigned si_get_ps_num_interp(struct si_shader *ps)
{
   unsigned num_interp = ps->info.num_ps_inputs;

   /* Back colors are added by the PS prolog when needed. */
   if (!ps->is_monolithic && ps->key.ps.part.prolog.color_two_side)
      num_interp += !!(ps->info.ps_colors_read & 0x0f) + !!(ps->info.ps_colors_read & 0xf0);

   assert(num_interp <= 32);
   return MIN2(num_interp, 32);
}

/** Whether the shader runs as a combination of multiple API shaders */
bool si_is_multi_part_shader(struct si_shader *shader)
{
   if (shader->selector->screen->info.gfx_level <= GFX8 ||
       shader->selector->stage > MESA_SHADER_GEOMETRY)
      return false;

   return shader->key.ge.as_ls || shader->key.ge.as_es ||
          shader->selector->stage == MESA_SHADER_TESS_CTRL ||
          shader->selector->stage == MESA_SHADER_GEOMETRY;
}

/** Whether the shader runs on a merged HW stage (LSHS or ESGS) */
bool si_is_merged_shader(struct si_shader *shader)
{
   if (shader->selector->stage > MESA_SHADER_GEOMETRY || shader->is_gs_copy_shader)
      return false;

   return shader->key.ge.as_ngg || si_is_multi_part_shader(shader);
}

/**
 * Returns a unique index for a semantic name and index. The index must be
 * less than 64, so that a 64-bit bitmask of used inputs or outputs can be
 * calculated.
 */
unsigned si_shader_io_get_unique_index(unsigned semantic)
{
   switch (semantic) {
   case VARYING_SLOT_POS:
      return SI_UNIQUE_SLOT_POS;
   default:
      if (semantic >= VARYING_SLOT_VAR0 && semantic <= VARYING_SLOT_VAR31)
         return SI_UNIQUE_SLOT_VAR0 + (semantic - VARYING_SLOT_VAR0);

      if (semantic >= VARYING_SLOT_VAR0_16BIT && semantic <= VARYING_SLOT_VAR15_16BIT)
         return SI_UNIQUE_SLOT_VAR0_16BIT + (semantic - VARYING_SLOT_VAR0_16BIT);

      assert(!"invalid generic index");
      return 0;

   /* Legacy desktop GL varyings. */
   case VARYING_SLOT_FOGC:
      return SI_UNIQUE_SLOT_FOGC;
   case VARYING_SLOT_COL0:
      return SI_UNIQUE_SLOT_COL0;
   case VARYING_SLOT_COL1:
      return SI_UNIQUE_SLOT_COL1;
   case VARYING_SLOT_BFC0:
      return SI_UNIQUE_SLOT_BFC0;
   case VARYING_SLOT_BFC1:
      return SI_UNIQUE_SLOT_BFC1;
   case VARYING_SLOT_TEX0:
   case VARYING_SLOT_TEX1:
   case VARYING_SLOT_TEX2:
   case VARYING_SLOT_TEX3:
   case VARYING_SLOT_TEX4:
   case VARYING_SLOT_TEX5:
   case VARYING_SLOT_TEX6:
   case VARYING_SLOT_TEX7:
      return SI_UNIQUE_SLOT_TEX0 + (semantic - VARYING_SLOT_TEX0);
   case VARYING_SLOT_CLIP_VERTEX:
      return SI_UNIQUE_SLOT_CLIP_VERTEX;

   /* Varyings present in both GLES and desktop GL. */
   case VARYING_SLOT_CLIP_DIST0:
      return SI_UNIQUE_SLOT_CLIP_DIST0;
   case VARYING_SLOT_CLIP_DIST1:
      return SI_UNIQUE_SLOT_CLIP_DIST1;
   case VARYING_SLOT_PSIZ:
      return SI_UNIQUE_SLOT_PSIZ;
   case VARYING_SLOT_LAYER:
      return SI_UNIQUE_SLOT_LAYER;
   case VARYING_SLOT_VIEWPORT:
      return SI_UNIQUE_SLOT_VIEWPORT;
   case VARYING_SLOT_PRIMITIVE_ID:
      return SI_UNIQUE_SLOT_PRIMITIVE_ID;
   }
}

unsigned si_get_max_workgroup_size(const struct si_shader *shader)
{
   struct si_screen *sscreen = shader->selector->screen;
   mesa_shader_stage stage = shader->is_gs_copy_shader ?
      MESA_SHADER_VERTEX : shader->selector->stage;

   assert(shader->wave_size);

   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      /* Use the largest workgroup size for streamout */
      if (shader->key.ge.as_ngg)
         return shader->info.num_streamout_vec4s ? 256 : 128;

      /* As part of merged shader. */
      return sscreen->info.gfx_level >= GFX9 &&
         (shader->key.ge.as_ls || shader->key.ge.as_es) ? 128 : shader->wave_size;

   case MESA_SHADER_TESS_CTRL:
      /* Return this so that LLVM doesn't remove s_barrier
       * instructions on chips where we use s_barrier. */
      return sscreen->info.gfx_level >= GFX7 ? 128 : shader->wave_size;

   case MESA_SHADER_GEOMETRY:
      /* GS can always generate up to 256 vertices. */
      return sscreen->info.gfx_level >= GFX9 ? 256 : shader->wave_size;

   case MESA_SHADER_TASK:
   case MESA_SHADER_MESH:
   case MESA_SHADER_COMPUTE:
      break; /* see below */

   default:
      return shader->wave_size;
   }

   /* Compile a variable block size using the maximum variable size. */
   if (shader->selector->info.base.workgroup_size_variable)
      return SI_MAX_VARIABLE_THREADS_PER_BLOCK;

   uint16_t *local_size = shader->selector->info.base.workgroup_size;
   unsigned max_work_group_size = (uint32_t)local_size[0] *
                                  (uint32_t)local_size[1] *
                                  (uint32_t)local_size[2];

   /* Without multi-row export, we need at least number of output vertex/primitive
    * threads in workgroup for export (one vertex/primitive per thread).
    */
   if (stage == MESA_SHADER_MESH && !sscreen->info.mesh_fast_launch_2) {
      max_work_group_size = MAX3(max_work_group_size,
                                 shader->selector->info.base.mesh.max_vertices_out,
                                 shader->selector->info.base.mesh.max_primitives_out);
   }

   assert(max_work_group_size);
   return max_work_group_size;
}


unsigned si_get_shader_prefetch_size(struct si_shader *shader)
{
   struct si_screen *sscreen = shader->selector->screen;
   /* This excludes arrays of constants after instructions. */
   unsigned exec_size =
      ac_align_shader_binary_for_prefetch(&sscreen->info,
                                          shader->complete_shader_binary_size);

   /* INST_PREF_SIZE uses 128B granularity.
    * - GFX11: max 128 * 63 = 8064
    * - GFX12: max 128 * 255 = 32640
    */
   unsigned max_pref_size = shader->selector->screen->info.gfx_level >= GFX12 ? 255 : 63;
   unsigned exec_size_gran128 = DIV_ROUND_UP(exec_size, 128);

   return MIN2(max_pref_size, exec_size_gran128);
}

unsigned si_calculate_needed_lds_size(enum amd_gfx_level gfx_level, struct si_shader *shader)
{
   mesa_shader_stage stage =
      shader->is_gs_copy_shader ? MESA_SHADER_VERTEX : shader->selector->stage;
   unsigned lds_size = 0;

   if (gfx_level >= GFX9 && stage <= MESA_SHADER_GEOMETRY &&
       (stage == MESA_SHADER_GEOMETRY || shader->key.ge.as_ngg)) {
      unsigned size_in_dw = shader->key.ge.as_ngg ? shader->ngg.info.esgs_lds_size
                                                  : shader->gs_info.esgs_lds_size;

      if (stage == MESA_SHADER_GEOMETRY && shader->key.ge.as_ngg)
         size_in_dw += shader->ngg.info.ngg_out_lds_size;

      lds_size = size_in_dw * 4;
   }

   if (stage == MESA_SHADER_COMPUTE ||
       stage == MESA_SHADER_TASK ||
       stage == MESA_SHADER_MESH) {
      lds_size = shader->info.shared_size;
   }

   /* Check that the LDS size is within hw limits. */
   assert(lds_size <= shader->selector->screen->info.lds_size_per_workgroup);
   return lds_size;
}


static void si_calculate_max_simd_waves(struct si_shader *shader)
{
   struct si_screen *sscreen = shader->selector->screen;
   struct ac_shader_config *conf = &shader->config;
   unsigned lds_increment = ac_shader_get_lds_alloc_granularity(sscreen->info.gfx_level);
   unsigned lds_per_wave = 0;
   unsigned max_simd_waves;

   max_simd_waves = sscreen->info.max_waves_per_simd;

   /* Compute LDS usage for PS. */
   switch (shader->selector->stage) {
   case MESA_SHADER_FRAGMENT:
      /* The minimum usage per wave is (num_inputs * 48). The maximum
       * usage is (num_inputs * 48 * 16).
       * We can get anything in between and it varies between waves.
       *
       * The 48 bytes per input for a single primitive is equal to
       * 4 bytes/component * 4 components/input * 3 points.
       *
       * Other stages don't know the size at compile time or don't
       * allocate LDS per wave, but instead they do it per thread group.
       */
      lds_per_wave = align(conf->lds_size, lds_increment) +
                     align(shader->info.num_ps_inputs * 48, lds_increment);
      break;
   case MESA_SHADER_TASK:
   case MESA_SHADER_MESH:
   case MESA_SHADER_COMPUTE: {
         unsigned max_workgroup_size = si_get_max_workgroup_size(shader);
         lds_per_wave = align(conf->lds_size, lds_increment) / DIV_ROUND_UP(max_workgroup_size, shader->wave_size);
      }
      break;
   default:;
   }

   /* Compute the per-SIMD wave counts. */
   if (conf->num_sgprs) {
      max_simd_waves =
         MIN2(max_simd_waves, sscreen->info.num_physical_sgprs_per_simd / conf->num_sgprs);
   }

   if (conf->num_vgprs) {
      /* GFX 10.3 internally:
       * - aligns VGPRS to 16 for Wave32 and 8 for Wave64
       * - aligns LDS to 1024
       *
       * For shader-db stats, set num_vgprs that the hw actually uses.
       */
      unsigned num_vgprs = conf->num_vgprs;
      if (sscreen->info.gfx_level >= GFX10_3) {
         unsigned real_vgpr_gran = sscreen->info.num_physical_wave64_vgprs_per_simd / 64;
         num_vgprs = util_align_npot(num_vgprs, real_vgpr_gran * (shader->wave_size == 32 ? 2 : 1));
      } else {
         num_vgprs = align(num_vgprs, shader->wave_size == 32 ? 8 : 4);
      }

      /* Always print wave limits as Wave64, so that we can compare
       * Wave32 and Wave64 with shader-db fairly. */
      unsigned max_vgprs = sscreen->info.num_physical_wave64_vgprs_per_simd;
      max_simd_waves = MIN2(max_simd_waves, max_vgprs / num_vgprs);
   }

   unsigned max_lds_per_simd = sscreen->info.lds_size_per_workgroup / sscreen->info.num_simd_per_compute_unit;
   if (lds_per_wave)
      max_simd_waves = MIN2(max_simd_waves, max_lds_per_simd / lds_per_wave);

   shader->info.max_simd_waves = max_simd_waves;
}

unsigned si_map_io_driver_location(unsigned semantic)
{
   if ((semantic >= VARYING_SLOT_PATCH0 && semantic < VARYING_SLOT_TESS_MAX) ||
       semantic == VARYING_SLOT_TESS_LEVEL_INNER ||
       semantic == VARYING_SLOT_TESS_LEVEL_OUTER)
      return ac_shader_io_get_unique_index_patch(semantic);

   return si_shader_io_get_unique_index(semantic);
}

static bool si_lower_io_to_mem(struct si_shader *shader, nir_shader *nir)
{
   struct si_shader_selector *sel = shader->selector;
   struct si_shader_selector *next_sel = shader->next_shader ? shader->next_shader->selector : sel;
   const union si_shader_key *key = &shader->key;
   const bool is_gfx9_mono_tcs = shader->is_monolithic &&
                                 next_sel->stage == MESA_SHADER_TESS_CTRL &&
                                 sel->screen->info.gfx_level >= GFX9;

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      if (key->ge.as_ls) {
         NIR_PASS(_, nir, ac_nir_lower_ls_outputs_to_mem,
                  is_gfx9_mono_tcs ? NULL : si_map_io_driver_location,
                  sel->screen->info.gfx_level,
                  key->ge.opt.same_patch_vertices,
                  is_gfx9_mono_tcs ? next_sel->info.tcs_inputs_via_temp : 0,
                  is_gfx9_mono_tcs ? next_sel->info.tcs_inputs_via_lds : ~0ull);
         return true;
      } else if (key->ge.as_es) {
         NIR_PASS(_, nir, ac_nir_lower_es_outputs_to_mem, si_map_io_driver_location,
                  sel->screen->info.gfx_level, sel->info.esgs_vertex_stride, ~0ULL);
         return true;
      }
   } else if (nir->info.stage == MESA_SHADER_TESS_CTRL) {
      NIR_PASS(_, nir, ac_nir_lower_hs_inputs_to_mem,
               is_gfx9_mono_tcs ? NULL : si_map_io_driver_location,
               sel->screen->info.gfx_level, key->ge.opt.same_patch_vertices,
               sel->info.tcs_inputs_via_temp, sel->info.tcs_inputs_via_lds);

      /* Used by hs_emit_write_tess_factors() when monolithic shader. */
      if (nir->info.tess._primitive_mode == TESS_PRIMITIVE_UNSPECIFIED)
         nir->info.tess._primitive_mode = key->ge.opt.tes_prim_mode;

      nir_tcs_info tcs_info;
      nir_gather_tcs_info(nir, &tcs_info, nir->info.tess._primitive_mode,
                          nir->info.tess.spacing);
      ac_nir_tess_io_info tess_io_info;
      ac_nir_get_tess_io_info(nir, &tcs_info, ~0ull, ~0, si_map_io_driver_location, false,
                              &tess_io_info);

      NIR_PASS(_, nir, ac_nir_lower_hs_outputs_to_mem, &tcs_info, &tess_io_info, si_map_io_driver_location,
               sel->screen->info.gfx_level, shader->wave_size);
      return true;
   } else if (nir->info.stage == MESA_SHADER_TESS_EVAL) {
      NIR_PASS(_, nir, ac_nir_lower_tes_inputs_to_mem, si_map_io_driver_location);

      if (key->ge.as_es) {
         NIR_PASS(_, nir, ac_nir_lower_es_outputs_to_mem, si_map_io_driver_location,
                  sel->screen->info.gfx_level, sel->info.esgs_vertex_stride, ~0ULL);
      }

      return true;
   } else if (nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS(_, nir, ac_nir_lower_gs_inputs_to_mem, si_map_io_driver_location,
               sel->screen->info.gfx_level, key->ge.mono.u.gs_tri_strip_adj_fix);
      return true;
   }

   return false;
}

static bool gfx10_ngg_writes_user_edgeflags(struct si_shader *shader)
{
   return gfx10_has_variable_edgeflags(shader) &&
          shader->selector->info.writes_edgeflag;
}

bool gfx10_ngg_export_prim_early(struct si_shader *shader)
{
   struct si_shader_selector *sel = shader->selector;

   assert(shader->key.ge.as_ngg && !shader->key.ge.as_es);

   return sel->stage != MESA_SHADER_GEOMETRY &&
          !gfx10_ngg_writes_user_edgeflags(shader) &&
          /* gfx10.x is sometimes slower with the late primitive export, so use the early prim
           * export by default. */
          sel->screen->info.gfx_level < GFX11;
}

static void si_lower_ngg(struct si_shader *shader, nir_shader *nir,
                         struct si_temp_shader_variant_info *temp_info)
{
   struct si_shader_selector *sel = shader->selector;
   const union si_shader_key *key = &shader->key;
   assert(key->ge.as_ngg);

   unsigned max_workgroup_size = si_get_max_workgroup_size(shader);

   if (nir->info.stage == MESA_SHADER_MESH) {
      bool out_needs_scratch_ring;
      NIR_PASS(_, nir, ac_nir_lower_ngg_mesh,
               &sel->screen->info,
               shader->info.clipdist_mask | shader->info.culldist_mask,
               temp_info->vs_output_param_offset,
               shader->info.nr_param_exports || shader->info.nr_prim_param_exports,
               &out_needs_scratch_ring,
               shader->wave_size,
               align(max_workgroup_size, shader->wave_size),
               false,
               false);
      shader->info.uses_mesh_scratch_ring = out_needs_scratch_ring;
      return;
   }

   ac_nir_lower_ngg_options options = {
      .hw_info = &sel->screen->info,
      .max_workgroup_size = max_workgroup_size,
      .wave_size = shader->wave_size,
      .can_cull = si_shader_culling_enabled(shader),
      .disable_streamout = !shader->info.num_streamout_vec4s,
      .vs_output_param_offset = temp_info->vs_output_param_offset,
      .has_param_exports = shader->info.nr_param_exports,
      .export_clipdist_mask = shader->info.clipdist_mask | shader->info.culldist_mask,
      .cull_clipdist_mask = si_shader_culling_enabled(shader) ?
                                 SI_NGG_CULL_GET_CLIP_PLANE_ENABLE(key->ge.opt.ngg_culling) |
                                 shader->info.culldist_mask : 0,
      .write_pos_to_clipvertex = shader->key.ge.mono.write_pos_to_clipvertex,
      .force_vrs = sel->screen->options.vrs2x2,
      .use_gfx12_xfb_intrinsic = !nir->info.use_aco_amd,
      .skip_viewport_state_culling = sel->info.writes_viewport_index,
      .use_point_tri_intersection = sel->screen->info.num_cu / sel->screen->info.num_se >= 12,
   };

   /* Cull distances are not exported if the shader culls against them. */
   if (options.can_cull)
      shader->info.culldist_mask = 0;

   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_TESS_EVAL) {
      /* Per instance inputs, used to remove instance load after culling. */
      unsigned instance_rate_inputs = 0;

      if (nir->info.stage == MESA_SHADER_VERTEX) {
         instance_rate_inputs = key->ge.mono.instance_divisor_is_one |
                                key->ge.mono.instance_divisor_is_fetched;

         /* Manually mark the instance ID used, so the shader can repack it. */
         if (instance_rate_inputs)
            BITSET_SET(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
      } else {
         /* Manually mark the primitive ID used, so the shader can repack it. */
         if (key->ge.mono.u.vs_export_prim_id)
            BITSET_SET(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);
      }

      unsigned num_vertices = si_get_num_vertices_per_output_prim(shader);

      options.num_vertices_per_primitive = num_vertices ? num_vertices : 3;
      options.early_prim_export = gfx10_ngg_export_prim_early(shader);
      options.passthrough = gfx10_is_ngg_passthrough(shader);
      options.use_edgeflags = gfx10_has_variable_edgeflags(shader);
      options.has_gen_prim_query = options.has_xfb_prim_query =
         sel->screen->info.gfx_level >= GFX11 && !nir->info.vs.blit_sgprs_amd;
      options.export_primitive_id = key->ge.mono.u.vs_export_prim_id;
      options.instance_rate_inputs = instance_rate_inputs;

      NIR_PASS(_, nir, ac_nir_lower_ngg_nogs, &options, &shader->info.ngg_lds_vertex_size,
               &shader->info.ngg_lds_scratch_size);
   } else {
      assert(nir->info.stage == MESA_SHADER_GEOMETRY);

      options.has_gen_prim_query = options.has_xfb_prim_query =
         sel->screen->info.gfx_level >= GFX11;
      options.has_gs_invocations_query = sel->screen->info.gfx_level < GFX11;
      options.has_gs_primitives_query = true;

      /* For monolithic ES/GS to add vscnt wait when GS export pos0. */
      if (key->ge.part.gs.es)
         nir->info.writes_memory |= key->ge.part.gs.es->info.base.writes_memory;

      NIR_PASS(_, nir, ac_nir_lower_ngg_gs, &options, &shader->info.ngg_lds_vertex_size,
               &shader->info.ngg_lds_scratch_size);
   }

   /* may generate some vector output store */
   NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);
}

struct nir_shader *si_deserialize_shader(struct si_shader_selector *sel)
{
   struct pipe_screen *screen = &sel->screen->b;
   const void *options = screen->nir_options[sel->stage];

   struct blob_reader blob_reader;
   blob_reader_init(&blob_reader, sel->nir_binary, sel->nir_size);
   return nir_deserialize(NULL, options, &blob_reader);
}

static void si_nir_assign_param_offsets(nir_shader *nir, struct si_shader *shader,
                                        int8_t slot_remap[NUM_TOTAL_VARYING_SLOTS],
                                        struct si_temp_shader_variant_info *temp_info)
{
   struct si_shader_selector *sel = shader->selector;
   struct si_shader_variant_info *info = &shader->info;

   uint64_t outputs_written = 0;
   uint32_t outputs_written_16bit = 0;
   uint64_t per_primitive_outputs = 0;

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   assert(impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_store_output &&
             intr->intrinsic != nir_intrinsic_store_per_vertex_output &&
             intr->intrinsic != nir_intrinsic_store_per_primitive_output)
            continue;

         /* No indirect indexing allowed. */
         ASSERTED nir_src offset = *nir_get_io_offset_src(intr);
         assert(nir_src_is_const(offset) && nir_src_as_uint(offset) == 0);

         assert(intr->num_components == 1); /* only scalar stores expected */
         nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

         if (sem.location >= VARYING_SLOT_VAR0_16BIT)
            outputs_written_16bit |= BITFIELD_BIT(sem.location - VARYING_SLOT_VAR0_16BIT);
         else
            outputs_written |= BITFIELD64_BIT(sem.location);

         if (intr->intrinsic == nir_intrinsic_store_per_primitive_output)
            per_primitive_outputs |= BITFIELD64_BIT(sem.location);

         /* Assign the param index if it's unassigned. */
         if (nir_slot_is_varying(sem.location, MESA_SHADER_FRAGMENT) && !sem.no_varying &&
             (sem.gs_streams & 0x3) == 0 &&
             temp_info->vs_output_param_offset[sem.location] == AC_EXP_PARAM_UNDEFINED) {
            /* The semantic and the base should be the same as in si_shader_info. */
            assert(sem.location == sel->info.output_semantic[nir_intrinsic_base(intr)]);
            /* It must not be remapped (duplicated). */
            assert(slot_remap[sem.location] == -1);

            temp_info->vs_output_param_offset[sem.location] =
               intr->intrinsic == nir_intrinsic_store_per_primitive_output ?
               info->nr_prim_param_exports++ :
               info->nr_param_exports++;
         }
      }
   }

   /* Duplicated outputs are redirected here. */
   for (unsigned i = 0; i < NUM_TOTAL_VARYING_SLOTS; i++) {
      if (slot_remap[i] >= 0)
         temp_info->vs_output_param_offset[i] = temp_info->vs_output_param_offset[slot_remap[i]];
   }

   if (shader->key.ge.mono.u.vs_export_prim_id) {
      temp_info->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = info->nr_param_exports++;
   }

   /* per primitive outputs come after per vertex outputs */
   unsigned per_primitive_outputs_offset = info->nr_param_exports;
   if (sel->screen->info.gfx_level >= GFX11)
      per_primitive_outputs_offset = MAX2(per_primitive_outputs_offset, 1);
   u_foreach_bit64 (i, per_primitive_outputs) {
      if (temp_info->vs_output_param_offset[i] != AC_EXP_PARAM_DEFAULT_VAL_0000)
         temp_info->vs_output_param_offset[i] += per_primitive_outputs_offset;
   }

   /* Update outputs written info, we may remove some outputs before. */
   nir->info.outputs_written = outputs_written;
   nir->info.outputs_written_16bit = outputs_written_16bit;
   nir->info.per_primitive_outputs = per_primitive_outputs;
}

static void si_assign_param_offsets(nir_shader *nir, struct si_shader *shader,
                                    struct si_temp_shader_variant_info *temp_info)
{
   /* Initialize this first. */
   shader->info.nr_param_exports = 0;
   shader->info.nr_prim_param_exports = 0;

   STATIC_ASSERT(sizeof(temp_info->vs_output_param_offset[0]) == 1);
   memset(temp_info->vs_output_param_offset, AC_EXP_PARAM_UNDEFINED,
          sizeof(temp_info->vs_output_param_offset));

   /* A slot remapping table for duplicated outputs, so that 1 vertex shader output can be
    * mapped to multiple fragment shader inputs.
    */
   int8_t slot_remap[NUM_TOTAL_VARYING_SLOTS];
   memset(slot_remap, -1, NUM_TOTAL_VARYING_SLOTS);

   /* This sets DEFAULT_VAL for constant outputs in vs_output_param_offset. */
   /* TODO: This doesn't affect GS and MS. */
   NIR_PASS(_, nir, ac_nir_optimize_outputs, false, slot_remap,
            temp_info->vs_output_param_offset);

   /* Assign the non-constant outputs. */
   si_nir_assign_param_offsets(nir, shader, slot_remap, temp_info);

   /* Any unwritten output will default to (0,0,0,0). */
   for (unsigned i = 0; i < NUM_TOTAL_VARYING_SLOTS; i++) {
      if (temp_info->vs_output_param_offset[i] == AC_EXP_PARAM_UNDEFINED)
         temp_info->vs_output_param_offset[i] = AC_EXP_PARAM_DEFAULT_VAL_0000;
   }
}

bool si_should_clear_lds(struct si_screen *sscreen, const struct nir_shader *shader)
{
   return mesa_shader_stage_is_compute(shader->info.stage) &&
      shader->info.shared_size > 0 && sscreen->options.clear_lds;
}

/* Run passes that eliminate code and affect shader_info. These should be run before linking
 * and shader_info gathering. Lowering passes can be run here too, but only if they lead to
 * better code or lower undesirable representations (like derefs). Lowering passes that prevent
 * linking optimizations or destroy shader_info shouldn't be run here.
 */
static void run_pre_link_optimization_passes(struct si_nir_shader_ctx *ctx)
{
   struct si_shader *shader = ctx->shader;
   struct si_shader_selector *sel = shader->selector;
   const union si_shader_key *key = &shader->key;
   nir_shader *nir = ctx->nir;
   bool progress = false;

   /* Kill outputs according to the shader key. */
   if (nir->info.stage <= MESA_SHADER_GEOMETRY || nir->info.stage == MESA_SHADER_MESH)
      NIR_PASS(progress, nir, si_nir_kill_outputs, key);

   bool inline_uniforms = false;
   uint32_t *inlined_uniform_values;
   si_get_inline_uniform_state((union si_shader_key*)key, nir->info.stage,
                               &inline_uniforms, &inlined_uniform_values);

   if (inline_uniforms) {
      /* Most places use shader information from the default variant, not
       * the optimized variant. These are the things that the driver looks at
       * in optimized variants and the list of things that we need to do.
       *
       * The driver takes into account these things if they suddenly disappear
       * from the shader code:
       * - Register usage and code size decrease (obvious)
       * - Eliminated PS system values are disabled
       * - VS/TES/GS param exports are eliminated if they are undef.
       *   The param space for eliminated outputs is also not allocated.
       * - VS/TCS/TES/GS/PS input loads are eliminated (VS relies on DCE in LLVM)
       * - TCS output stores are eliminated
       * - Eliminated PS inputs are removed from PS.NUM_INTERP.
       *
       * TODO: These are things the driver ignores in the final shader code
       * and relies on the default shader info.
       * - System values in VS, TCS, TES, GS are not eliminated
       * - uses_discard - if it changed to false
       * - writes_memory - if it changed to false
       * - VS->TCS, VS->GS, TES->GS output stores for the former stage are not
       *   eliminated
       * - Eliminated VS/TCS/TES outputs are still allocated. (except when feeding PS)
       *   GS outputs are eliminated except for the temporary LDS.
       *   Clip distances, gl_PointSize, gl_Layer and PS outputs are eliminated based
       *   on current states, so we don't care about the shader code.
       *
       * TODO: Merged shaders don't inline uniforms for the first stage.
       * VS-GS: only GS inlines uniforms; VS-TCS: only TCS; TES-GS: only GS.
       * (key == NULL for the first stage here)
       *
       * TODO: Compute shaders don't support inlinable uniforms, because they
       * don't have shader variants.
       *
       * TODO: The driver uses a linear search to find a shader variant. This
       * can be really slow if we get too many variants due to uniform inlining.
       */
      NIR_PASS(_, nir, nir_inline_uniforms, nir->info.num_inlinable_uniforms,
               inlined_uniform_values, nir->info.inlinable_uniform_dw_offsets);
      progress = true;
   }

   NIR_PASS(progress, nir, nir_opt_shrink_stores, false);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* This uses the prolog/epilog keys, so only monolithic shaders can call this. */
      if (shader->is_monolithic) {
         /* This lowers load_color intrinsics to COLn/BFCn input loads and two-side color
          * selection.
          */
         if (sel->info.colors_read)
            NIR_PASS(progress, nir, si_nir_lower_ps_color_inputs, &shader->key, &sel->info);

         /* This adds discard and barycentrics. */
         if (key->ps.mono.point_smoothing)
            NIR_PASS(progress, nir, nir_lower_point_smooth, true);

         /* This eliminates system values and unused shader output components. */
         ac_nir_lower_ps_early_options early_options = {
            .msaa_disabled = key->ps.part.prolog.force_persp_center_interp ||
                             key->ps.part.prolog.force_linear_center_interp ||
                             key->ps.part.prolog.force_samplemask_to_helper_invocation ||
                             key->ps.mono.interpolate_at_sample_force_center,
            .load_sample_positions_always_loads_current_ones = true,
            .force_front_face = key->ps.opt.force_front_face_input,
            .optimize_frag_coord = true,
            .frag_coord_is_center = true,
            /* This does a lot of things. See the description in ac_nir_lower_ps_early_options. */
            .ps_iter_samples = key->ps.part.prolog.samplemask_log_ps_iter ?
                                 (1 << key->ps.part.prolog.samplemask_log_ps_iter) :
                                 (key->ps.part.prolog.force_persp_sample_interp ||
                                  key->ps.part.prolog.force_linear_sample_interp ? 2 :
                                  (key->ps.part.prolog.get_frag_coord_from_pixel_coord ? 1 : 0)),

            .fbfetch_is_1D = key->ps.mono.fbfetch_is_1D,
            .fbfetch_layered = key->ps.mono.fbfetch_layered,
            .fbfetch_msaa = key->ps.mono.fbfetch_msaa,
            .fbfetch_apply_fmask = sel->screen->info.gfx_level < GFX11 &&
                                   !(sel->screen->debug_flags & DBG(NO_FMASK)),

            .clamp_color = key->ps.part.epilog.clamp_color,
            .alpha_test_alpha_to_one = key->ps.part.epilog.alpha_to_one,
            .alpha_func = key->ps.part.epilog.alpha_func,
            .keep_alpha_for_mrtz = key->ps.part.epilog.alpha_to_coverage_via_mrtz,
            .spi_shader_col_format_hint = key->ps.part.epilog.spi_shader_col_format,
            .kill_z = key->ps.part.epilog.kill_z,
            .kill_stencil = key->ps.part.epilog.kill_stencil,
            .kill_samplemask = key->ps.part.epilog.kill_samplemask,
         };

         NIR_PASS(progress, nir, ac_nir_lower_ps_early, &early_options);

         /* This adds gl_SampleMaskIn. It must be after ac_nir_lower_ps_early that lowers
          * sample_mask_in to load_helper_invocation because we only want to do that for user
          * shaders while keeping the real sample mask for smoothing, which is produced using
          * MSAA overrasterization over a single-sample color buffer.
          */
         if (key->ps.mono.poly_line_smoothing)
            NIR_PASS(progress, nir, nir_lower_poly_line_smooth, SI_NUM_SMOOTH_AA_SAMPLES);

         /* This adds discard. */
         if (key->ps.part.prolog.poly_stipple)
            NIR_PASS(progress, nir, si_nir_lower_polygon_stipple);
      } else {
         ac_nir_lower_ps_early_options early_options = {
            .optimize_frag_coord = true,
            .frag_coord_is_center = true,
            .alpha_func = COMPARE_FUNC_ALWAYS,
            .spi_shader_col_format_hint = ~0,
         };
         NIR_PASS(progress, nir, ac_nir_lower_ps_early, &early_options);
      }
   }

   if (progress) {
      si_nir_opts(sel->screen, nir, true);
      progress = false;
   }

   /* This reduces code size, but also SIMD occupancy to a smaller degree due to increased
    * register usage, and it improves latency hiding for lds_param_load. It also hides another
    * LLVM WQM bug.
    *
    * VS input loads are moved to top because we always want them at the beginning and issued
    * all at once.
    */
   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(progress, nir, nir_opt_move_to_top, nir_move_to_top_input_loads);

   /* Remove dead temps before we lower indirect indexing. */
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);

   /* Lower indirect indexing last.
    *
    * Shader variant optimizations (such as uniform inlining, replacing barycentrics, and IO
    * elimination) can help eliminate indirect indexing, so this should be done after that.
    *
    * Note that the code can still contain tautologies such as "array1[i] == array2[i]" when
    * array1 and array2 have provably equal values (NIR doesn't have a pass that can do that),
    * which NIR can optimize only after we lower indirecting indexing, so it's important that
    * we lower it before we gather shader_info.
    */

   /* Lower indirect indexing of large constant arrays to the load_constant intrinsic, which
    * will be turned into PC-relative loads from a data section next to the shader.
    */
   NIR_PASS(progress, nir, nir_opt_large_constants, glsl_get_natural_size_align_bytes, 16);

   /* Lower all other indirect indexing to if-else ladders or scratch. */
   progress |= ac_nir_lower_indirect_derefs(nir, sel->screen->info.gfx_level);

   if (progress)
      si_nir_opts(shader->selector->screen, nir, false);
}

/* Late optimization passes and lowering passes. The majority of lowering passes are here.
 * These passes should have no impact on linking optimizations and shouldn't affect shader_info
 * (those should be run before this) because any changes in shader_info won't be reflected
 * in hw registers from now on.
 */
static void run_late_optimization_and_lowering_passes(struct si_nir_shader_ctx *ctx)
{
   struct si_shader *shader = ctx->shader;
   struct si_shader_selector *sel = shader->selector;
   const union si_shader_key *key = &shader->key;
   nir_shader *nir = ctx->nir;
   bool progress = false;

   si_init_shader_args(shader, &ctx->args, &nir->info);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(progress, nir, nir_lower_fragcoord_wtrans);

   NIR_PASS(progress, nir, ac_nir_lower_tex,
            &(ac_nir_lower_tex_options){
               .gfx_level = sel->screen->info.gfx_level,
               .lower_array_layer_round_even = !sel->screen->info.conformant_trunc_coord,
            });

   if (nir->info.uses_resource_info_query)
      NIR_PASS(progress, nir, ac_nir_lower_resinfo, sel->screen->info.gfx_level);

   /* This must be before si_nir_lower_resource. */
   if (!sel->screen->info.has_image_opcodes)
      NIR_PASS(progress, nir, ac_nir_lower_image_opcodes);

   /* LLVM does not work well with this, so is handled in llvm backend waterfall. */
   if (nir->info.use_aco_amd && ctx->temp_info.has_non_uniform_tex_access) {
      nir_lower_non_uniform_access_options options = {
         .types = nir_lower_non_uniform_texture_access,
      };
      NIR_PASS(progress, nir, nir_lower_non_uniform_access, &options);
   }

   /* Legacy GS is not the last VGT stage because there is also the GS copy shader. */
   bool is_last_vgt_stage =
      nir->info.stage == MESA_SHADER_MESH ||
      ((nir->info.stage == MESA_SHADER_VERTEX ||
        nir->info.stage == MESA_SHADER_TESS_EVAL ||
        (nir->info.stage == MESA_SHADER_GEOMETRY && shader->key.ge.as_ngg)) &&
       !shader->key.ge.as_ls && !shader->key.ge.as_es);

   if (nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(progress, nir, si_nir_lower_vs_inputs, shader, &ctx->args);

   progress |= si_lower_io_to_mem(shader, nir);

   if (is_last_vgt_stage) {
      /* Assign param export indices. */
      si_assign_param_offsets(nir, shader, &ctx->temp_info);

      if (key->ge.as_ngg) {
         /* Lower last VGT NGG shader stage. */
         si_lower_ngg(shader, nir, &ctx->temp_info);
      } else if (nir->info.stage == MESA_SHADER_VERTEX ||
                 nir->info.stage == MESA_SHADER_TESS_EVAL) {
         /* Lower last VGT none-NGG VS/TES shader stage. */
         NIR_PASS(_, nir, ac_nir_lower_legacy_vs,
                  sel->screen->info.gfx_level,
                  shader->info.clipdist_mask | shader->info.culldist_mask,
                  shader->key.ge.mono.write_pos_to_clipvertex,
                  ctx->temp_info.vs_output_param_offset,
                  shader->info.nr_param_exports,
                  shader->key.ge.mono.u.vs_export_prim_id,
                  !shader->info.num_streamout_vec4s,
                  sel->screen->options.vrs2x2);
      }
      progress = true;
   } else if (nir->info.stage == MESA_SHADER_GEOMETRY && !key->ge.as_ngg) {
      /* Assign param export indices. */
      si_assign_param_offsets(nir, shader, &ctx->temp_info);

      ac_nir_lower_legacy_gs_options options = {
         .has_gen_prim_query = false,
         .has_pipeline_stats_query = sel->screen->use_ngg,
         .gfx_level = sel->screen->info.gfx_level,
         .export_clipdist_mask = shader->info.clipdist_mask | shader->info.culldist_mask,
         .write_pos_to_clipvertex = shader->key.ge.mono.write_pos_to_clipvertex,
         .param_offsets = ctx->temp_info.vs_output_param_offset,
         .has_param_exports = shader->info.nr_param_exports,
         .disable_streamout = !shader->info.num_streamout_vec4s,
         .force_vrs = sel->screen->options.vrs2x2,
      };

      NIR_PASS(_, nir, ac_nir_lower_legacy_gs, &options, &ctx->gs_copy_shader,
               &shader->info.legacy_gs);
      progress = true;
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT && shader->is_monolithic) {
      ac_nir_lower_ps_late_options late_options = {
         .gfx_level = sel->screen->info.gfx_level,
         .family = sel->screen->info.family,
         .use_aco = nir->info.use_aco_amd,
         .bc_optimize_for_persp = key->ps.part.prolog.bc_optimize_for_persp,
         .bc_optimize_for_linear = key->ps.part.prolog.bc_optimize_for_linear,
         .uses_discard = shader->info.uses_discard,
         .alpha_to_coverage_via_mrtz = key->ps.part.epilog.alpha_to_coverage_via_mrtz,
         .dual_src_blend_swizzle = key->ps.part.epilog.dual_src_blend_swizzle,
         .spi_shader_col_format = key->ps.part.epilog.spi_shader_col_format,
         .color_is_int8 = key->ps.part.epilog.color_is_int8,
         .color_is_int10 = key->ps.part.epilog.color_is_int10,
         .alpha_to_one = key->ps.part.epilog.alpha_to_one,
      };

      NIR_PASS(progress, nir, ac_nir_lower_ps_late, &late_options);
   }

   assert(shader->wave_size == 32 || shader->wave_size == 64);

   NIR_PASS(progress, nir, nir_lower_subgroups,
            &(struct nir_lower_subgroups_options) {
               .subgroup_size = shader->wave_size,
               .ballot_bit_size = shader->wave_size,
               .ballot_components = 1,
               .lower_to_scalar = true,
               .lower_subgroup_masks = true,
               .lower_relative_shuffle = true,
               .lower_rotate_to_shuffle = !nir->info.use_aco_amd,
               .lower_shuffle_to_32bit = true,
               .lower_vote_feq = true,
               .lower_vote_ieq = true,
               .lower_vote_bool_eq = true,
               .lower_quad_broadcast_dynamic = true,
               .lower_quad_broadcast_dynamic_to_const = sel->screen->info.gfx_level <= GFX7,
               .lower_shuffle_to_swizzle_amd = true,
               .lower_ballot_bit_count_to_mbcnt_amd = true,
               .lower_boolean_reduce = nir->info.use_aco_amd,
               .lower_boolean_shuffle = true,
            });

   NIR_PASS(progress, nir, nir_lower_pack);
   NIR_PASS(progress, nir, nir_opt_idiv_const, 8);
   NIR_PASS(progress, nir, nir_lower_idiv,
            &(nir_lower_idiv_options){
               .allow_fp16 = sel->screen->info.gfx_level >= GFX9,
            });

   if (si_should_clear_lds(sel->screen, nir)) {
      const unsigned chunk_size = 16; /* max single store size */
      const unsigned shared_size = align(nir->info.shared_size, chunk_size);
      NIR_PASS(_, nir, nir_clear_shared_memory, shared_size, chunk_size);
   }

   /* This is required by ac_nir_scalarize_overfetching_loads_callback. */
   NIR_PASS(progress, nir, ac_nir_flag_smem_for_loads, sel->screen->info.gfx_level,
            !sel->info.base.use_aco_amd);
   /* Scalarize overfetching loads, so that we don't load more components than necessary.
    * Adjacent loads will be re-vectorized with a conservative overfetching limit.
    */
   NIR_PASS(progress, nir, nir_lower_io_to_scalar,
            nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_shared | nir_var_mem_global,
            ac_nir_scalarize_overfetching_loads_callback, &sel->screen->info.gfx_level);
   /* Scalarize shared memory ops to get ds_load_2addr/ds_store_2addr more often.
    * If we don't do that, we might get pairs of ds_load_2addr + ds_load for vec3 loads, etc.
    */
   NIR_PASS(progress, nir, nir_lower_io_to_scalar, nir_var_mem_shared, NULL, NULL);
   NIR_PASS(progress, nir, si_nir_lower_resource, shader, &ctx->args);

   /* This must be done before load/store vectorization to lower 16-bit SMEM loads to 32 bits,
    * so that they can be vectorized as 32-bit loads. 16-bit loads are never vectorized.
    */
   NIR_PASS(progress, nir, ac_nir_lower_mem_access_bit_sizes,
            sel->screen->info.gfx_level, !nir->info.use_aco_amd);

   /* Load/store vectorization requires that offset computations are optimized. */
   if (progress) {
      si_nir_opts(sel->screen, nir, false);
      progress = false;
   }

   NIR_PASS(progress, nir, nir_opt_load_store_vectorize,
            &(nir_load_store_vectorize_options){
               .modes = nir_var_mem_ssbo | nir_var_mem_ubo | nir_var_mem_shared | nir_var_mem_global |
                        nir_var_shader_temp,
               .callback = ac_nir_mem_vectorize_callback,
               .cb_data = &(struct ac_nir_config){sel->screen->info.gfx_level, sel->info.base.use_aco_amd},
               .has_shared2_amd = true,
            });

   /* This must be done again if 8-bit or 16-bit buffer stores were vectorized. */
   NIR_PASS(progress, nir, ac_nir_lower_mem_access_bit_sizes,
            sel->screen->info.gfx_level, !nir->info.use_aco_amd);

   if (ac_nir_might_lower_bit_size(nir)) {
      if (sel->screen->info.gfx_level >= GFX8)
         nir_divergence_analysis(nir);

      NIR_PASS(progress, nir, nir_lower_bit_size, ac_nir_lower_bit_size_callback,
               &sel->screen->info.gfx_level);
   }

   /* This must be after lowering resources to descriptor loads and before lowering intrinsics
    * to args and lowering int64.
    */
   if (nir->info.use_aco_amd)
      progress |= ac_nir_optimize_uniform_atomics(nir);

   NIR_PASS(progress, nir, si_nir_lower_abi, shader, &ctx->args);
   /* Global access lowering must be called after lowering ABI which emits regular load_global intrinsics. */
   NIR_PASS(progress, nir, ac_nir_lower_global_access);
   NIR_PASS(progress, nir, nir_lower_int64);

   NIR_PASS(progress, nir, ac_nir_lower_intrinsics_to_args, sel->screen->info.gfx_level,
            sel->screen->info.has_ls_vgpr_init_bug,
            si_select_hw_stage(nir->info.stage, key, sel->screen->info.gfx_level),
            shader->wave_size, si_get_max_workgroup_size(shader), &ctx->args.ac);

   /* LLVM keep non-uniform sampler as index, so can't do this in NIR.
    * Must be done after si_nir_lower_resource().
    */
   if (nir->info.use_aco_amd && ctx->temp_info.has_shadow_comparison &&
       sel->screen->info.gfx_level >= GFX8 && sel->screen->info.gfx_level <= GFX9) {
      NIR_PASS(progress, nir, si_nir_clamp_shadow_comparison_value);
   }

   if (progress) {
      si_nir_opts(sel->screen, nir, false);
      progress = false;
   }

   const nir_opt_offsets_options offset_options = {
      .uniform_max = 0,
      .buffer_max = ~0,
      .shared_max = UINT16_MAX,
      .shared_atomic_max = UINT16_MAX,
      .allow_offset_wrap_cb = ac_nir_allow_offset_wrap_cb,
      .cb_data = &sel->screen->info.gfx_level,
   };
   NIR_PASS(_, nir, nir_opt_offsets, &offset_options);

   si_nir_late_opts(nir);

   NIR_PASS(progress, nir, nir_opt_sink,
            nir_move_const_undef | nir_move_copies | nir_move_alu | nir_move_comparisons |
            nir_move_load_ubo | nir_move_load_ssbo);
   NIR_PASS(progress, nir, nir_opt_move,
            nir_move_const_undef | nir_move_copies | nir_move_alu | nir_move_comparisons |
            nir_move_load_ubo);
   /* Run nir_opt_move again to make sure that comparisons are as close as possible to the first
    * use to prevent SCC spilling.
    */
   NIR_PASS(progress, nir, nir_opt_move, nir_move_comparisons);

   /* This must be done after si_nir_late_opts() because it may generate vec const. */
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);

   /* This helps LLVM form VMEM clauses and thus get more GPU cache hits.
    * 200 is tuned for Viewperf. It should be done last.
    */
   NIR_PASS(_, nir, nir_opt_group_loads, nir_group_same_resource_only, 200);
}

static void get_input_nir(struct si_shader *shader, struct si_nir_shader_ctx *ctx)
{
   struct si_shader_selector *sel = shader->selector;

   ctx->shader = shader;
   ctx->free_nir = !sel->nir && sel->nir_binary;
   ctx->nir = sel->nir ? sel->nir : (sel->nir_binary ? si_deserialize_shader(sel) : NULL);
   assert(ctx->nir);

   if (sel->stage <= MESA_SHADER_GEOMETRY)
      ctx->nir->info.use_aco_amd = shader->key.ge.use_aco;

   assert(ctx->nir->info.use_aco_amd == si_shader_uses_aco(shader));

   if (unlikely(should_print_nir(ctx->nir))) {
      /* Modify the shader's name so that each variant gets its own name. */
      ctx->nir->info.name = ralloc_asprintf(ctx->nir, "%s-%08x", ctx->nir->info.name,
                                            _mesa_hash_data(&shader->key, sizeof(shader->key)));

      /* Dummy pass to get the starting point. */
      printf("nir_dummy_pass\n");
      nir_print_shader(ctx->nir, stdout);
   }
}

static void get_prev_stage_input_nir(struct si_shader *shader, struct si_linked_shaders *linked)
{
   const union si_shader_key *key = &shader->key;

   if (shader->selector->stage == MESA_SHADER_TESS_CTRL) {
      linked->producer_shader.selector = key->ge.part.tcs.ls;
      linked->producer_shader.key.ge.as_ls = 1;
   } else {
      linked->producer_shader.selector = key->ge.part.gs.es;
      linked->producer_shader.key.ge.as_es = 1;
      linked->producer_shader.key.ge.as_ngg = key->ge.as_ngg;
   }
   linked->producer_shader.key.ge.use_aco = key->ge.use_aco;

   linked->producer_shader.next_shader = shader;
   linked->producer_shader.key.ge.mono = key->ge.mono;
   linked->producer_shader.key.ge.opt = key->ge.opt;
   linked->producer_shader.key.ge.opt.inline_uniforms = false; /* only TCS/GS can inline uniforms */
   /* kill_outputs was computed based on second shader's outputs so we can't use it to
    * kill first shader's outputs.
    */
   linked->producer_shader.key.ge.opt.kill_outputs = 0;
   linked->producer_shader.is_monolithic = true;
   linked->producer_shader.wave_size = shader->wave_size;

   get_input_nir(&linked->producer_shader, &linked->producer);
}

static void get_nir_shaders(struct si_shader *shader, struct si_linked_shaders *linked)
{
   memset(linked, 0, sizeof(*linked));
   get_input_nir(shader, &linked->consumer);

   if (shader->selector->screen->info.gfx_level >= GFX9 && shader->is_monolithic &&
       (shader->selector->stage == MESA_SHADER_TESS_CTRL ||
        shader->selector->stage == MESA_SHADER_GEOMETRY))
      get_prev_stage_input_nir(shader, linked);

   for (unsigned i = 0; i < SI_NUM_LINKED_SHADERS; i++) {
      if (linked->shader[i].nir)
         run_pre_link_optimization_passes(&linked->shader[i]);
   }

   /* TODO: run linking optimizations here if we have LS+HS or ES+GS */

   /* Remove holes after removed PS inputs by renumbering them. Holes can only occur with
    * monolithic PS.
    */
   if (shader->selector->stage == MESA_SHADER_FRAGMENT && shader->is_monolithic)
      NIR_PASS(_, linked->consumer.nir, nir_recompute_io_bases, nir_var_shader_in);

   for (unsigned i = 0; i < SI_NUM_LINKED_SHADERS; i++) {
      if (linked->shader[i].nir) {
         si_get_shader_variant_info(shader, &linked->shader[i].temp_info, linked->shader[i].nir);
         run_late_optimization_and_lowering_passes(&linked->shader[i]);
         si_get_late_shader_variant_info(shader, &linked->shader[i].args, linked->shader[i].nir);
      }
   }
}

/* Generate code for the hardware VS shader stage to go with a geometry shader */
static struct si_shader *
si_nir_generate_gs_copy_shader(struct si_screen *sscreen,
                               struct ac_llvm_compiler *compiler,
                               struct si_shader *gs_shader,
                               nir_shader *gs_nir, nir_shader *gs_copy_shader,
                               struct util_debug_callback *debug)
{
   struct si_shader *shader;
   struct si_shader_selector *gs_selector = gs_shader->selector;

   shader = CALLOC_STRUCT(si_shader);
   if (!shader)
      return NULL;

   /* We can leave the fence as permanently signaled because the GS copy
    * shader only becomes visible globally after it has been compiled. */
   util_queue_fence_init(&shader->ready);

   shader->selector = gs_selector;
   shader->is_gs_copy_shader = true;
   shader->wave_size = si_determine_wave_size(sscreen, shader);
   shader->info.num_streamout_vec4s = gs_shader->info.num_streamout_vec4s;
   shader->info.nr_param_exports = gs_shader->info.nr_param_exports;
   shader->info.clipdist_mask = gs_shader->info.clipdist_mask;
   shader->info.culldist_mask = gs_shader->info.culldist_mask;

   nir_shader *nir = gs_copy_shader;
   nir->info.use_aco_amd = gs_nir->info.use_aco_amd;

   struct si_linked_shaders linked;
   memset(&linked, 0, sizeof(linked));
   linked.consumer.nir = nir;

   si_init_shader_args(shader, &linked.consumer.args, &gs_nir->info);

   NIR_PASS(_, nir, si_nir_lower_abi, shader, &linked.consumer.args);
   NIR_PASS(_, nir, ac_nir_lower_intrinsics_to_args, sscreen->info.gfx_level,
            sscreen->info.has_ls_vgpr_init_bug, AC_HW_VERTEX_SHADER, 64, 64,
            &linked.consumer.args.ac);

   NIR_PASS(_, nir, ac_nir_lower_global_access);
   NIR_PASS(_, nir, nir_lower_int64);

   si_nir_opts(gs_selector->screen, nir, false);

   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   /* This pass must be last. */
   si_get_late_shader_variant_info(shader, &linked.consumer.args, nir);

   if (si_can_dump_shader(sscreen, MESA_SHADER_GEOMETRY, SI_DUMP_NIR)) {
      fprintf(stderr, "GS Copy Shader:\n");
      nir_print_shader(nir, stderr);
   }

   bool ok =
#if AMD_LLVM_AVAILABLE
      !gs_nir->info.use_aco_amd ? si_llvm_compile_shader(sscreen, compiler, shader, &linked, debug) :
#endif
      si_aco_compile_shader(shader, &linked, debug);

#if !AMD_LLVM_AVAILABLE
   assert(gs_nir->info.use_aco_amd);
#endif

   if (ok) {
      assert(!shader->config.scratch_bytes_per_wave);
      ok = si_shader_binary_upload(sscreen, shader, 0) >= 0;
      si_shader_dump(sscreen, shader, debug, stderr, true);
   }
   ralloc_free(nir);

   if (!ok) {
      FREE(shader);
      shader = NULL;
   } else {
      si_fix_resource_usage(sscreen, shader);
   }
   return shader;
}

static void
debug_message_stderr(void *data, unsigned *id, enum util_debug_type ptype,
                      const char *fmt, va_list args)
{
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
}

bool si_compile_shader(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                       struct si_shader *shader, struct util_debug_callback *debug)
{
   bool ret = true;
   struct si_shader_selector *sel = shader->selector;
   struct si_linked_shaders linked;

   get_nir_shaders(shader, &linked);
   nir_shader *nir = linked.consumer.nir;

   /* Dump NIR before doing NIR->LLVM conversion in case the
    * conversion fails. */
   if (si_can_dump_shader(sscreen, nir->info.stage, SI_DUMP_NIR)) {
      nir_print_shader(nir, stderr);

      if (nir->xfb_info)
         nir_print_xfb_info(nir->xfb_info, stderr);
   }

   /* Initialize vs_output_ps_input_cntl to default. */
   for (unsigned i = 0; i < ARRAY_SIZE(shader->info.vs_output_ps_input_cntl); i++)
      shader->info.vs_output_ps_input_cntl[i] = SI_PS_INPUT_CNTL_UNUSED;
   shader->info.vs_output_ps_input_cntl[VARYING_SLOT_COL0] = SI_PS_INPUT_CNTL_UNUSED_COLOR0;

   shader->info.private_mem_vgprs = DIV_ROUND_UP(nir->scratch_size, 4);

   /* Set the FP ALU behavior. */
   /* By default, we disable denormals for FP32 and enable them for FP16 and FP64
    * for performance and correctness reasons. FP32 denormals can't be enabled because
    * they break output modifiers and v_mad_f32 and are very slow on GFX6-7.
    *
    * float_controls_execution_mode defines the set of valid behaviors. Contradicting flags
    * can be set simultaneously, which means we are allowed to choose, but not really because
    * some options cause GLCTS failures.
    */
   unsigned float_mode = V_00B028_FP_16_64_DENORMS;

   if (!(nir->info.float_controls_execution_mode & FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP32) &&
       nir->info.float_controls_execution_mode & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32)
      float_mode |= V_00B028_FP_32_ROUND_TOWARDS_ZERO;

   if (!(nir->info.float_controls_execution_mode & (FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP16 |
                                                    FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP64)) &&
       nir->info.float_controls_execution_mode & (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 |
                                                  FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64))
      float_mode |= V_00B028_FP_16_64_ROUND_TOWARDS_ZERO;

   if (!(nir->info.float_controls_execution_mode & (FLOAT_CONTROLS_DENORM_PRESERVE_FP16 |
                                                    FLOAT_CONTROLS_DENORM_PRESERVE_FP64)) &&
       nir->info.float_controls_execution_mode & (FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16 |
                                                  FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP64))
      float_mode &= ~V_00B028_FP_16_64_DENORMS;

   assert(nir->info.use_aco_amd == si_shader_uses_aco(shader));
   ret =
#if AMD_LLVM_AVAILABLE
      !nir->info.use_aco_amd ? si_llvm_compile_shader(sscreen, compiler, shader, &linked, debug) :
#endif
      si_aco_compile_shader(shader, &linked, debug);

#if !AMD_LLVM_AVAILABLE
   assert(nir->info.use_aco_amd);
#endif

   if (!ret)
      goto out;

   shader->config.float_mode = float_mode;

   /* The GS copy shader is compiled next. */
   if (nir->info.stage == MESA_SHADER_GEOMETRY && !shader->key.ge.as_ngg) {
      shader->gs_copy_shader =
         si_nir_generate_gs_copy_shader(sscreen, compiler, shader, nir,
                                        linked.consumer.gs_copy_shader, debug);
      if (!shader->gs_copy_shader) {
         mesa_loge("can't create GS copy shader");
         ret = false;
         goto out;
      }
   }

   /* Compute vs_output_ps_input_cntl. */
   if ((nir->info.stage == MESA_SHADER_VERTEX ||
        nir->info.stage == MESA_SHADER_TESS_EVAL ||
        nir->info.stage == MESA_SHADER_GEOMETRY ||
        nir->info.stage == MESA_SHADER_MESH) &&
       !shader->key.ge.as_ls && !shader->key.ge.as_es) {
      uint8_t *vs_output_param_offset = linked.consumer.temp_info.vs_output_param_offset;

      /* We must use the original shader info before the removal of duplicated shader outputs. */
      /* VS and TES should also set primitive ID output if it's used. */
      unsigned num_outputs_with_prim_id = sel->info.num_outputs +
                                          shader->key.ge.mono.u.vs_export_prim_id;

      for (unsigned i = 0; i < num_outputs_with_prim_id; i++) {
         unsigned semantic = sel->info.output_semantic[i];
         unsigned offset = vs_output_param_offset[semantic];
         unsigned ps_input_cntl;

         if (offset <= AC_EXP_PARAM_OFFSET_31) {
            /* The input is loaded from parameter memory. */
            ps_input_cntl = S_028644_OFFSET(offset);
         } else {
            /* The input is a DEFAULT_VAL constant. */
            assert(offset >= AC_EXP_PARAM_DEFAULT_VAL_0000 &&
                   offset <= AC_EXP_PARAM_DEFAULT_VAL_1111);
            offset -= AC_EXP_PARAM_DEFAULT_VAL_0000;

            /* OFFSET=0x20 means that DEFAULT_VAL is used. */
            ps_input_cntl = S_028644_OFFSET(0x20) |
                            S_028644_DEFAULT_VAL(offset);
         }

         if (sscreen->info.gfx_level >= GFX11 &&
             (nir->info.per_primitive_outputs & BITFIELD64_BIT(semantic)))
            ps_input_cntl |= S_028644_PRIM_ATTR(1);

         shader->info.vs_output_ps_input_cntl[semantic] = ps_input_cntl;
      }
   }

   /* Validate SGPR and VGPR usage for compute to detect compiler bugs. */
   if (mesa_shader_stage_is_compute(nir->info.stage)) {
      unsigned max_vgprs =
         sscreen->info.num_physical_wave64_vgprs_per_simd * (shader->wave_size == 32 ? 2 : 1);
      unsigned max_sgprs = sscreen->info.num_physical_sgprs_per_simd;
      unsigned max_sgprs_per_wave = 128;
      unsigned simds_per_tg = 4; /* assuming WGP mode on gfx10 */
      unsigned threads_per_tg = si_get_max_workgroup_size(shader);
      unsigned waves_per_tg = DIV_ROUND_UP(threads_per_tg, shader->wave_size);
      unsigned waves_per_simd = DIV_ROUND_UP(waves_per_tg, simds_per_tg);

      max_vgprs = max_vgprs / waves_per_simd;
      max_sgprs = MIN2(max_sgprs / waves_per_simd, max_sgprs_per_wave);

      if (shader->config.num_sgprs > max_sgprs || shader->config.num_vgprs > max_vgprs) {
         mesa_loge("LLVM failed to compile a shader correctly: "
                   "SGPR:VGPR usage is %u:%u, but the hw limit is %u:%u",
                   shader->config.num_sgprs, shader->config.num_vgprs, max_sgprs, max_vgprs);

         /* Just terminate the process, because dependent
          * shaders can hang due to bad input data, but use
          * the env var to allow shader-db to work.
          */
         if (!debug_get_bool_option("SI_PASS_BAD_SHADERS", false))
            abort();
      }
   }

   /* Add/remove the scratch offset to/from input SGPRs. */
   if (!sel->screen->info.has_scratch_base_registers &&
       !si_is_merged_shader(shader)) {
      if (nir->info.use_aco_amd) {
         /* When aco scratch_offset arg is added explicitly at the beginning.
          * After compile if no scratch used, reduce the input sgpr count.
          */
         if (!shader->config.scratch_bytes_per_wave)
            shader->info.num_input_sgprs--;
      } else {
         /* scratch_offset arg is added by llvm implicitly */
         if (shader->info.num_input_sgprs)
            shader->info.num_input_sgprs++;
      }
   }

   /* Calculate the number of fragment input VGPRs. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      shader->info.num_input_vgprs = ac_get_fs_input_vgpr_cnt(&shader->config);

   si_calculate_max_simd_waves(shader);

   if (si_can_dump_shader(sscreen, nir->info.stage, SI_DUMP_STATS)) {
      struct util_debug_callback out_stderr = {
         .debug_message = debug_message_stderr,
      };

      si_shader_dump_stats_for_shader_db(sscreen, shader, &out_stderr);
   } else {
      si_shader_dump_stats_for_shader_db(sscreen, shader, debug);
   }

out:
   for (unsigned i = 0; i < SI_NUM_LINKED_SHADERS; i++) {
      if (linked.shader[i].free_nir)
         ralloc_free(linked.shader[i].nir);
   }

   return ret;
}

/**
 * Create, compile and return a shader part (prolog or epilog).
 *
 * \param sscreen  screen
 * \param list     list of shader parts of the same category
 * \param type     shader type
 * \param key      shader part key
 * \param prolog   whether the part being requested is a prolog
 * \param tm       LLVM target machine
 * \param debug    debug callback
 * \return         non-NULL on success
 */
static struct si_shader_part *
si_get_shader_part(struct si_screen *sscreen, struct si_shader_part **list,
                   mesa_shader_stage stage, bool prolog, union si_shader_part_key *key,
                   struct ac_llvm_compiler *compiler, struct util_debug_callback *debug,
                   const char *name)
{
   struct si_shader_part *result;

   simple_mtx_lock(&sscreen->shader_parts_mutex);

   /* Find existing. */
   for (result = *list; result; result = result->next) {
      if (memcmp(&result->key, key, sizeof(*key)) == 0) {
         simple_mtx_unlock(&sscreen->shader_parts_mutex);
         return result;
      }
   }

   /* Compile a new one. */
   result = CALLOC_STRUCT(si_shader_part);
   result->key = *key;

   bool ok =
#if AMD_LLVM_AVAILABLE
      !(sscreen->use_aco ||
        (stage == MESA_SHADER_FRAGMENT &&
         ((prolog && key->ps_prolog.use_aco) ||
          (!prolog && key->ps_epilog.use_aco)))) ?
      si_llvm_build_shader_part(sscreen, stage, prolog, compiler, debug, name, result) :
#endif
      si_aco_build_shader_part(sscreen, stage, prolog, debug, name, result);

   if (ok) {
      result->next = *list;
      *list = result;
   } else {
      FREE(result);
      result = NULL;
   }

   simple_mtx_unlock(&sscreen->shader_parts_mutex);
   return result;
}


/**
 * Select and compile (or reuse) TCS parts (epilog).
 */
static bool si_shader_select_tcs_parts(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                                       struct si_shader *shader, struct util_debug_callback *debug)
{
   if (sscreen->info.gfx_level >= GFX9) {
      assert(shader->wave_size == 32 || shader->wave_size == 64);
      unsigned wave_size_index = shader->wave_size == 64;
      shader->previous_stage =
         shader->key.ge.part.tcs.ls->main_parts.named.ls[wave_size_index][shader->key.ge.use_aco];
      assert(shader->previous_stage->key.ge.use_aco == si_shader_uses_aco(shader));
      assert((shader->previous_stage->binary.type == SI_SHADER_BINARY_RAW) == si_shader_uses_aco(shader));
   }

   return true;
}

/**
 * Select and compile (or reuse) GS parts (prolog).
 */
static bool si_shader_select_gs_parts(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                                      struct si_shader *shader, struct util_debug_callback *debug)
{
   if (sscreen->info.gfx_level >= GFX9) {
      if (shader->key.ge.as_ngg) {
         assert(shader->wave_size == 32 || shader->wave_size == 64);
         unsigned wave_size_index = shader->wave_size == 64;
         shader->previous_stage =
            shader->key.ge.part.gs.es->main_parts.named.ngg_es[wave_size_index][shader->key.ge.use_aco];
      } else {
         shader->previous_stage = shader->key.ge.part.gs.es->main_parts.named.es[shader->key.ge.use_aco];
      }
      assert(shader->previous_stage->key.ge.use_aco == si_shader_uses_aco(shader));
      assert((shader->previous_stage->binary.type == SI_SHADER_BINARY_RAW) == si_shader_uses_aco(shader));
   }

   return true;
}

/**
 * Compute the PS prolog key, which contains all the information needed to
 * build the PS prolog function, and set related bits in shader->config.
 */
static void si_get_ps_prolog_key(struct si_shader *shader, union si_shader_part_key *key)
{
   struct si_shader_info *info = &shader->selector->info;

   memset(key, 0, sizeof(*key));
   key->ps_prolog.states = shader->key.ps.part.prolog;
   key->ps_prolog.use_aco = info->base.use_aco_amd;
   key->ps_prolog.wave32 = shader->wave_size == 32;
   key->ps_prolog.colors_read = shader->info.ps_colors_read;
   key->ps_prolog.num_input_sgprs = shader->info.num_input_sgprs;
   key->ps_prolog.wqm =
      info->base.fs.needs_coarse_quad_helper_invocations &&
      (key->ps_prolog.colors_read || key->ps_prolog.states.force_persp_sample_interp ||
       key->ps_prolog.states.force_linear_sample_interp ||
       key->ps_prolog.states.force_persp_center_interp ||
       key->ps_prolog.states.force_linear_center_interp ||
       key->ps_prolog.states.bc_optimize_for_persp || key->ps_prolog.states.bc_optimize_for_linear ||
       key->ps_prolog.states.samplemask_log_ps_iter ||
       key->ps_prolog.states.get_frag_coord_from_pixel_coord ||
       key->ps_prolog.states.force_samplemask_to_helper_invocation);
   key->ps_prolog.fragcoord_usage_mask =
      G_0286CC_POS_X_FLOAT_ENA(shader->config.spi_ps_input_ena) |
      (G_0286CC_POS_Y_FLOAT_ENA(shader->config.spi_ps_input_ena) << 1) |
      (G_0286CC_POS_Z_FLOAT_ENA(shader->config.spi_ps_input_ena) << 2) |
      (G_0286CC_POS_W_FLOAT_ENA(shader->config.spi_ps_input_ena) << 3);
   key->ps_prolog.pixel_center_integer = key->ps_prolog.fragcoord_usage_mask &&
                                         shader->selector->info.base.fs.pixel_center_integer;

   if (shader->key.ps.part.prolog.poly_stipple)
      shader->info.uses_vmem_load_other = true;

   if (shader->info.ps_colors_read) {
      uint8_t *color = shader->selector->info.color_attr_index;

      if (shader->key.ps.part.prolog.color_two_side) {
         /* BCOLORs are stored after the last input. */
         key->ps_prolog.num_interp_inputs = shader->info.num_ps_inputs;
         shader->config.spi_ps_input_ena |= S_0286CC_FRONT_FACE_ENA(1);
      }

      for (unsigned i = 0; i < 2; i++) {
         unsigned interp = info->color_interpolate[i];
         unsigned location = info->color_interpolate_loc[i];

         if (!(shader->info.ps_colors_read & (0xf << i * 4)))
            continue;

         key->ps_prolog.color_attr_index[i] = color[i];

         if (shader->key.ps.part.prolog.flatshade_colors && interp == INTERP_MODE_COLOR)
            interp = INTERP_MODE_FLAT;

         switch (interp) {
         case INTERP_MODE_FLAT:
            key->ps_prolog.color_interp_vgpr_index[i] = -1;
            break;
         case INTERP_MODE_SMOOTH:
         case INTERP_MODE_COLOR:
            /* Force the interpolation location for colors here. */
            if (shader->key.ps.part.prolog.force_persp_sample_interp)
               location = TGSI_INTERPOLATE_LOC_SAMPLE;
            if (shader->key.ps.part.prolog.force_persp_center_interp)
               location = TGSI_INTERPOLATE_LOC_CENTER;

            switch (location) {
            case TGSI_INTERPOLATE_LOC_SAMPLE:
               key->ps_prolog.color_interp_vgpr_index[i] = 0;
               shader->config.spi_ps_input_ena |= S_0286CC_PERSP_SAMPLE_ENA(1);
               break;
            case TGSI_INTERPOLATE_LOC_CENTER:
               key->ps_prolog.color_interp_vgpr_index[i] = 2;
               shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTER_ENA(1);
               break;
            case TGSI_INTERPOLATE_LOC_CENTROID:
               key->ps_prolog.color_interp_vgpr_index[i] = 4;
               shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTROID_ENA(1);
               break;
            default:
               assert(0);
            }
            break;
         case INTERP_MODE_NOPERSPECTIVE:
            /* Force the interpolation location for colors here. */
            if (shader->key.ps.part.prolog.force_linear_sample_interp)
               location = TGSI_INTERPOLATE_LOC_SAMPLE;
            if (shader->key.ps.part.prolog.force_linear_center_interp)
               location = TGSI_INTERPOLATE_LOC_CENTER;

            /* The VGPR assignment for non-monolithic shaders
             * works because InitialPSInputAddr is set on the
             * main shader and PERSP_PULL_MODEL is never used.
             */
            switch (location) {
            case TGSI_INTERPOLATE_LOC_SAMPLE:
               key->ps_prolog.color_interp_vgpr_index[i] = 6;
               shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_SAMPLE_ENA(1);
               break;
            case TGSI_INTERPOLATE_LOC_CENTER:
               key->ps_prolog.color_interp_vgpr_index[i] = 8;
               shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTER_ENA(1);
               break;
            case TGSI_INTERPOLATE_LOC_CENTROID:
               key->ps_prolog.color_interp_vgpr_index[i] = 10;
               shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTROID_ENA(1);
               break;
            default:
               assert(0);
            }
            break;
         default:
            assert(0);
         }
      }
   }
}

/**
 * Check whether a PS prolog is required based on the key.
 */
static bool si_need_ps_prolog(const union si_shader_part_key *key)
{
   return key->ps_prolog.colors_read || key->ps_prolog.states.force_persp_sample_interp ||
          key->ps_prolog.states.force_linear_sample_interp ||
          key->ps_prolog.states.force_persp_center_interp ||
          key->ps_prolog.states.force_linear_center_interp ||
          key->ps_prolog.states.bc_optimize_for_persp ||
          key->ps_prolog.states.bc_optimize_for_linear || key->ps_prolog.states.poly_stipple ||
          key->ps_prolog.states.samplemask_log_ps_iter ||
          key->ps_prolog.states.get_frag_coord_from_pixel_coord ||
          key->ps_prolog.states.force_samplemask_to_helper_invocation;
}

/**
 * Compute the PS epilog key, which contains all the information needed to
 * build the PS epilog function.
 */
static void si_get_ps_epilog_key(struct si_shader *shader, union si_shader_part_key *key)
{
   struct si_shader_info *info = &shader->selector->info;
   memset(key, 0, sizeof(*key));
   key->ps_epilog.use_aco = info->base.use_aco_amd;
   key->ps_epilog.wave32 = shader->wave_size == 32;
   key->ps_epilog.uses_discard = shader->info.uses_discard ||
                                 shader->key.ps.part.prolog.poly_stipple ||
                                 shader->key.ps.part.epilog.alpha_func != PIPE_FUNC_ALWAYS;
   key->ps_epilog.colors_written = info->colors_written;
   key->ps_epilog.color_types = info->output_color_types;
   key->ps_epilog.writes_all_cbufs = info->color0_writes_all_cbufs &&
                                     /* Check whether a non-zero color buffer is bound. */
                                     !!(shader->key.ps.part.epilog.spi_shader_col_format & 0xfffffff0);
   key->ps_epilog.writes_z = info->writes_z;
   key->ps_epilog.writes_stencil = info->writes_stencil;
   key->ps_epilog.writes_samplemask = info->writes_samplemask;
   key->ps_epilog.states = shader->key.ps.part.epilog;
}

/**
 * Select and compile (or reuse) pixel shader parts (prolog & epilog).
 */
static bool si_shader_select_ps_parts(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                                      struct si_shader *shader, struct util_debug_callback *debug)
{
   union si_shader_part_key prolog_key;
   union si_shader_part_key epilog_key;

   /* Get the prolog. */
   si_get_ps_prolog_key(shader, &prolog_key);

   /* The prolog is a no-op if these aren't set. */
   if (si_need_ps_prolog(&prolog_key)) {
      shader->prolog =
         si_get_shader_part(sscreen, &sscreen->ps_prologs, MESA_SHADER_FRAGMENT, true, &prolog_key,
                            compiler, debug, "Fragment Shader Prolog");
      if (!shader->prolog)
         return false;
   }

   /* Get the epilog. */
   si_get_ps_epilog_key(shader, &epilog_key);

   shader->epilog =
      si_get_shader_part(sscreen, &sscreen->ps_epilogs, MESA_SHADER_FRAGMENT, false, &epilog_key,
                         compiler, debug, "Fragment Shader Epilog");
   if (!shader->epilog)
      return false;

   si_set_spi_ps_input_config_for_separate_prolog(shader);
   si_fixup_spi_ps_input_config(shader);

   /* Make sure spi_ps_input_addr bits is superset of spi_ps_input_ena. */
   unsigned spi_ps_input_ena = shader->config.spi_ps_input_ena;
   unsigned spi_ps_input_addr = shader->config.spi_ps_input_addr;
   assert((spi_ps_input_ena & spi_ps_input_addr) == spi_ps_input_ena);

   return true;
}

void si_multiwave_lds_size_workaround(struct si_screen *sscreen, unsigned *lds_size)
{
   /* If tessellation is all offchip and on-chip GS isn't used, this
    * workaround is not needed.
    */
   return;

   /* SPI barrier management bug:
    *   Make sure we have at least 4k of LDS in use to avoid the bug.
    *   It applies to workgroup sizes of more than one wavefront.
    */
   if (sscreen->info.family == CHIP_BONAIRE || sscreen->info.family == CHIP_KABINI)
      *lds_size = MAX2(*lds_size, 8 * ac_shader_get_lds_alloc_granularity(sscreen->info.gfx_level));
}

static void si_fix_resource_usage(struct si_screen *sscreen, struct si_shader *shader)
{
   unsigned min_sgprs = shader->info.num_input_sgprs + 2; /* VCC */

   shader->config.num_sgprs = MAX2(shader->config.num_sgprs, min_sgprs);

   if (shader->selector->stage == MESA_SHADER_COMPUTE &&
       si_get_max_workgroup_size(shader) > shader->wave_size) {
      si_multiwave_lds_size_workaround(sscreen, &shader->config.lds_size);
   }
}

static void si_init_mesh_shader_ngg_info(struct si_shader *shader)
{
   struct si_shader_selector *sel = shader->selector;

   shader->ngg.info.hw_max_esverts = 1;
   shader->ngg.info.max_gsprims = 1;
   shader->ngg.info.max_out_verts = sel->info.base.mesh.max_vertices_out;
   shader->ngg.info.max_vert_out_per_gs_instance = false;
   shader->ngg.info.ngg_out_lds_size = 0;
}

bool si_create_shader_variant(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                              struct si_shader *shader, struct util_debug_callback *debug)
{
   struct si_shader_selector *sel = shader->selector;
   struct si_shader *mainp = *si_get_main_shader_part(sel, &shader->key, shader->wave_size);

   /* LS, ES, VS are compiled on demand if the main part hasn't been
    * compiled for that stage.
    *
    * GS are compiled on demand if the main part hasn't been compiled
    * for the chosen NGG-ness.
    *
    * Vertex shaders are compiled on demand when a vertex fetch
    * workaround must be applied.
    */
   if (shader->is_monolithic) {
      /* Monolithic shader (compiled as a whole, has many variants,
       * may take a long time to compile).
       */
      if (!si_compile_shader(sscreen, compiler, shader, debug))
         return false;
   } else {
      /* The shader consists of several parts:
       *
       * - the middle part is the user shader, it has 1 variant only
       *   and it was compiled during the creation of the shader
       *   selector
       * - the prolog part is inserted at the beginning
       * - the epilog part is inserted at the end
       *
       * The prolog and epilog have many (but simple) variants.
       *
       * Starting with gfx9, geometry and tessellation control
       * shaders also contain the prolog and user shader parts of
       * the previous shader stage.
       */

      if (!mainp)
         return false;

      /* Copy the compiled shader data over. */
      shader->is_binary_shared = true;
      shader->binary = mainp->binary;
      shader->config = mainp->config;
      shader->info = mainp->info;

      /* Select prologs and/or epilogs. */
      switch (sel->stage) {
      case MESA_SHADER_TESS_CTRL:
         if (!si_shader_select_tcs_parts(sscreen, compiler, shader, debug))
            return false;
         break;
      case MESA_SHADER_GEOMETRY:
         if (!si_shader_select_gs_parts(sscreen, compiler, shader, debug))
            return false;

         /* Clone the GS copy shader for the shader variant.
          * We can't just copy the pointer because we change the pm4 state and
          * si_shader_selector::gs_copy_shader must be immutable because it's shared
          * by multiple contexts.
          */
         if (!shader->key.ge.as_ngg) {
            assert(mainp->gs_copy_shader);
            assert(mainp->gs_copy_shader->bo);
            assert(!mainp->gs_copy_shader->previous_stage_sel);
            assert(!mainp->gs_copy_shader->scratch_va);

            shader->gs_copy_shader = CALLOC_STRUCT(si_shader);
            memcpy(shader->gs_copy_shader, mainp->gs_copy_shader,
                   sizeof(*shader->gs_copy_shader));
            /* Increase the reference count. */
            pipe_reference(NULL, &shader->gs_copy_shader->bo->b.b.reference);
            /* Initialize some fields differently. */
            shader->gs_copy_shader->shader_log = NULL;
            shader->gs_copy_shader->is_binary_shared = true;
            util_queue_fence_init(&shader->gs_copy_shader->ready);
         }
         break;
      case MESA_SHADER_FRAGMENT:
         if (!si_shader_select_ps_parts(sscreen, compiler, shader, debug))
            return false;

         /* Make sure we have at least as many VGPRs as there
          * are allocated inputs.
          */
         shader->config.num_vgprs = MAX2(shader->config.num_vgprs, shader->info.num_input_vgprs);

         shader->info.writes_z &= !shader->key.ps.part.epilog.kill_z;
         shader->info.writes_stencil &= !shader->key.ps.part.epilog.kill_stencil;
         shader->info.writes_sample_mask &= !shader->key.ps.part.epilog.kill_samplemask;
         shader->info.uses_discard |= shader->key.ps.part.prolog.poly_stipple ||
                                      shader->key.ps.part.epilog.alpha_func != PIPE_FUNC_ALWAYS;
         break;
      default:;
      }

      assert(shader->wave_size == mainp->wave_size);
      assert(!shader->previous_stage || shader->wave_size == shader->previous_stage->wave_size);

      /* Update SGPR and VGPR counts. */
      if (shader->prolog) {
         shader->config.num_sgprs =
            MAX2(shader->config.num_sgprs, shader->prolog->num_sgprs);
         shader->config.num_vgprs =
            MAX2(shader->config.num_vgprs, shader->prolog->num_vgprs);
      }
      if (shader->previous_stage) {
         shader->config.num_sgprs =
            MAX2(shader->config.num_sgprs, shader->previous_stage->config.num_sgprs);
         shader->config.num_vgprs =
            MAX2(shader->config.num_vgprs, shader->previous_stage->config.num_vgprs);
         shader->config.spilled_sgprs =
            MAX2(shader->config.spilled_sgprs, shader->previous_stage->config.spilled_sgprs);
         shader->config.spilled_vgprs =
            MAX2(shader->config.spilled_vgprs, shader->previous_stage->config.spilled_vgprs);
         shader->info.private_mem_vgprs =
            MAX2(shader->info.private_mem_vgprs, shader->previous_stage->info.private_mem_vgprs);
         shader->config.scratch_bytes_per_wave =
            MAX2(shader->config.scratch_bytes_per_wave,
                 shader->previous_stage->config.scratch_bytes_per_wave);

         shader->info.uses_vmem_load_other |= shader->previous_stage->info.uses_vmem_load_other;
         shader->info.uses_vmem_sampler_or_bvh |= shader->previous_stage->info.uses_vmem_sampler_or_bvh;
         shader->info.uses_instance_id |= shader->previous_stage->info.uses_instance_id;
         shader->info.uses_base_instance |= shader->previous_stage->info.uses_base_instance;
         shader->info.uses_draw_id |= shader->previous_stage->info.uses_draw_id;
         shader->info.uses_vs_state_indexed |= shader->previous_stage->info.uses_vs_state_indexed;
         shader->info.uses_gs_state_provoking_vtx_first |= shader->previous_stage->info.uses_gs_state_provoking_vtx_first;
         shader->info.uses_gs_state_outprim |= shader->previous_stage->info.uses_gs_state_outprim;
      }
      if (shader->epilog) {
         shader->config.num_sgprs =
            MAX2(shader->config.num_sgprs, shader->epilog->num_sgprs);
         shader->config.num_vgprs =
            MAX2(shader->config.num_vgprs, shader->epilog->num_vgprs);
      }
      si_calculate_max_simd_waves(shader);
   }

   if (sel->stage <= MESA_SHADER_GEOMETRY && shader->key.ge.as_ngg) {
      assert(!shader->key.ge.as_es && !shader->key.ge.as_ls);
      const struct si_shader_selector *gs_sel = shader->selector;
      const struct si_shader_selector *es_sel =
         shader->previous_stage_sel ? shader->previous_stage_sel : gs_sel;
      const unsigned input_prim = si_get_input_prim(gs_sel, &shader->key, false);
      unsigned gs_vertices_out = gs_sel->stage == MESA_SHADER_GEOMETRY ? gs_sel->info.base.gs.vertices_out : 0;
      unsigned gs_invocations = gs_sel->stage == MESA_SHADER_GEOMETRY ? gs_sel->info.base.gs.invocations : 0;

      if (!ac_ngg_compute_subgroup_info(gs_sel->screen->info.gfx_level, es_sel->stage,
                                        gs_sel->stage == MESA_SHADER_GEOMETRY,
                                        input_prim, gs_vertices_out, gs_invocations,
                                        si_get_max_workgroup_size(shader), shader->wave_size,
                                        es_sel->info.esgs_vertex_stride, shader->info.ngg_lds_vertex_size,
                                        shader->info.ngg_lds_scratch_size, gs_sel->tess_turns_off_ngg,
                                        gs_sel->stage == MESA_SHADER_GEOMETRY ? 255 : 0, &shader->ngg.info)) {
         mesa_loge("Failed to compute subgroup info");
         return false;
      }

      /* GS outputs in LDS must start at a multiple of 256B because GS_STATE_GS_OUT_LDS_OFFSET_256B
       * doesn't store the low 8 bits.
       */
      if (sel->stage == MESA_SHADER_GEOMETRY)
         shader->ngg.info.esgs_lds_size = align(shader->ngg.info.esgs_lds_size, 64); /* align to 256B in dword units */
   } else if (sscreen->info.gfx_level >= GFX9 && sel->stage == MESA_SHADER_GEOMETRY) {
      ac_legacy_gs_compute_subgroup_info(sel->info.base.gs.input_primitive,
                                         sel->info.base.gs.vertices_out,
                                         sel->info.base.gs.invocations,
                                         shader->previous_stage_sel->info.esgs_vertex_stride,
                                         &shader->gs_info);
   } else if (sel->stage == MESA_SHADER_MESH) {
      si_init_mesh_shader_ngg_info(shader);
   }

   si_fix_resource_usage(sscreen, shader);

   /* Upload. */
   bool ok = si_shader_binary_upload(sscreen, shader, 0) >= 0;

   shader->complete_shader_binary_size = si_get_shader_binary_size(sscreen, shader);

   si_shader_dump(sscreen, shader, debug, stderr, true);

   if (!ok)
      mesa_loge("LLVM failed to upload shader");
   return ok;
}

void si_shader_destroy(struct si_shader *shader)
{
   si_resource_reference(&shader->bo, NULL);

   if (!shader->is_binary_shared)
      si_shader_binary_clean(&shader->binary);

   free(shader->shader_log);
}
