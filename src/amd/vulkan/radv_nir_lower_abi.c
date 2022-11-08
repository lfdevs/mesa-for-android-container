/*
 * Copyright © 2022 Valve Corporation
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

#include "nir.h"
#include "nir_builder.h"
#include "ac_nir.h"
#include "radv_constants.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"

typedef struct {
   enum amd_gfx_level gfx_level;
   const struct radv_shader_args *args;
   const struct radv_shader_info *info;
   const struct radv_pipeline_key *pl_key;
   bool use_llvm;
   uint32_t address32_hi;
} lower_abi_state;

static nir_ssa_def *
load_ring(nir_builder *b, unsigned ring, lower_abi_state *s)
{
   struct ac_arg arg =
      b->shader->info.stage == MESA_SHADER_TASK ?
      s->args->task_ring_offsets :
      s->args->ring_offsets;

   nir_ssa_def *ring_offsets = ac_nir_load_arg(b, &s->args->ac, arg);
   ring_offsets = nir_pack_64_2x32_split(b, nir_channel(b, ring_offsets, 0), nir_channel(b, ring_offsets, 1));
   return nir_load_smem_amd(b, 4, ring_offsets, nir_imm_int(b, ring * 16u), .align_mul = 4u);
}

static nir_ssa_def *
nggc_bool_setting(nir_builder *b, unsigned mask, lower_abi_state *s)
{
   nir_ssa_def *settings = ac_nir_load_arg(b, &s->args->ac, s->args->ngg_culling_settings);
   return nir_test_mask(b, settings, mask);
}

static nir_ssa_def *
ngg_query_bool_setting(nir_builder *b, unsigned mask, lower_abi_state *s)
{
   nir_ssa_def *settings = ac_nir_load_arg(b, &s->args->ac, s->args->ngg_query_state);
   return nir_test_mask(b, settings, mask);
}

static bool
lower_abi_instr(nir_builder *b, nir_instr *instr, void *state)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   lower_abi_state *s = (lower_abi_state *)state;
   gl_shader_stage stage = b->shader->info.stage;

   b->cursor = nir_before_instr(instr);

   nir_ssa_def *replacement = NULL;
   bool progress = true;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ring_tess_factors_amd:
      if (s->use_llvm) {
         progress = false;
         break;
      }

      replacement = load_ring(b, RING_HS_TESS_FACTOR, s);
      break;
   case nir_intrinsic_load_ring_tess_factors_offset_amd:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.tcs_factor_offset);
      break;
   case nir_intrinsic_load_ring_tess_offchip_amd:
      if (s->use_llvm) {
         progress = false;
         break;
      }

      replacement = load_ring(b, RING_HS_TESS_OFFCHIP, s);
      break;
   case nir_intrinsic_load_ring_tess_offchip_offset_amd:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.tess_offchip_offset);
      break;
   case nir_intrinsic_load_tcs_num_patches_amd:
      if (s->pl_key->dynamic_patch_control_points) {
         if (stage == MESA_SHADER_TESS_CTRL) {
            nir_ssa_def *arg = ac_nir_load_arg(b, &s->args->ac, s->args->tcs_offchip_layout);
            replacement = nir_ubfe_imm(b, arg, 6, 8);
         } else {
            replacement = ac_nir_load_arg(b, &s->args->ac, s->args->tes_num_patches);
         }
      } else {
         replacement = nir_imm_int(b, s->info->num_tess_patches);
      }
      break;
   case nir_intrinsic_load_ring_esgs_amd:
      if (s->use_llvm) {
         progress = false;
         break;
      }

      replacement = load_ring(b, stage == MESA_SHADER_GEOMETRY ? RING_ESGS_GS : RING_ESGS_VS, s);
      break;
   case nir_intrinsic_load_ring_gsvs_amd:
      if (s->use_llvm) {
         progress = false;
         break;
      }

      replacement = load_ring(b, RING_GSVS_VS, s);
      break;
   case nir_intrinsic_load_ring_es2gs_offset_amd:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.es2gs_offset);
      break;

   case nir_intrinsic_load_ring_attr_amd:
      if (s->use_llvm) {
         progress = false;
         break;
      }

      replacement = load_ring(b, RING_PS_ATTR, s);

      nir_ssa_def *dword1 = nir_channel(b, replacement, 1);
      dword1 = nir_ior_imm(b, dword1, S_008F04_STRIDE(16 * s->info->outinfo.param_exports));
      replacement = nir_vector_insert_imm(b, replacement, dword1, 1);
      break;

   case nir_intrinsic_load_ring_attr_offset_amd: {
      nir_ssa_def *ring_attr_offset = ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_attr_offset);
      replacement = nir_ishl(b, nir_ubfe(b, ring_attr_offset, nir_imm_int(b, 0), nir_imm_int(b, 15)),
                             nir_imm_int(b, 9)); /* 512b increments. */
      break;
   }

   case nir_intrinsic_load_tess_rel_patch_id_amd:
      if (stage == MESA_SHADER_TESS_CTRL) {
         replacement = nir_extract_u8(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.tcs_rel_ids),
                                      nir_imm_int(b, 0));
      } else if (stage == MESA_SHADER_TESS_EVAL) {
         /* Setting an upper bound like this will actually make it possible
          * to optimize some multiplications (in address calculations) so that
          * constant additions can be added to the const offset in memory load instructions.
          */
         nir_ssa_def *arg = ac_nir_load_arg(b, &s->args->ac, s->args->ac.tes_rel_patch_id);
         nir_intrinsic_instr *load_arg = nir_instr_as_intrinsic(arg->parent_instr);
         nir_intrinsic_set_arg_upper_bound_u32_amd(load_arg, 2048 / MAX2(b->shader->info.tess.tcs_vertices_out, 1));
         replacement = arg;
      } else {
         unreachable("invalid tessellation shader stage");
      }
      break;
   case nir_intrinsic_load_patch_vertices_in:
      if (stage == MESA_SHADER_TESS_CTRL) {
         if (s->pl_key->dynamic_patch_control_points) {
            nir_ssa_def *arg = ac_nir_load_arg(b, &s->args->ac, s->args->tcs_offchip_layout);
            replacement = nir_ubfe_imm(b, arg, 0, 6);
         } else {
            replacement = nir_imm_int(b, s->pl_key->tcs.tess_input_vertices);
         }
      } else if (stage == MESA_SHADER_TESS_EVAL) {
         replacement = nir_imm_int(b, b->shader->info.tess.tcs_vertices_out);
      }
      else
         unreachable("invalid tessellation shader stage");
      break;
   case nir_intrinsic_load_gs_vertex_offset_amd:
      replacement =
         ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_vtx_offset[nir_intrinsic_base(intrin)]);
      break;
   case nir_intrinsic_load_workgroup_num_input_vertices_amd:
      replacement = nir_ubfe(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_tg_info),
                             nir_imm_int(b, 12), nir_imm_int(b, 9));
      break;
   case nir_intrinsic_load_workgroup_num_input_primitives_amd:
      replacement = nir_ubfe(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_tg_info),
                             nir_imm_int(b, 22), nir_imm_int(b, 9));
      break;
   case nir_intrinsic_load_packed_passthrough_primitive_amd:
      /* NGG passthrough mode: the HW already packs the primitive export value to a single register. */
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_vtx_offset[0]);
      break;
   case nir_intrinsic_load_pipeline_stat_query_enabled_amd:
      replacement = ngg_query_bool_setting(b, radv_ngg_query_pipeline_stat, s);
      break;
   case nir_intrinsic_load_prim_gen_query_enabled_amd:
      replacement = ngg_query_bool_setting(b, radv_ngg_query_prim_gen, s);
      break;
   case nir_intrinsic_load_prim_xfb_query_enabled_amd:
      replacement = ngg_query_bool_setting(b, radv_ngg_query_prim_xfb, s);
      break;
   case nir_intrinsic_load_merged_wave_info_amd:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.merged_wave_info);
      break;
   case nir_intrinsic_load_cull_any_enabled_amd:
      replacement = nggc_bool_setting(
         b, radv_nggc_front_face | radv_nggc_back_face | radv_nggc_small_primitives, s);
      break;
   case nir_intrinsic_load_cull_front_face_enabled_amd:
      replacement = nggc_bool_setting(b, radv_nggc_front_face, s);
      break;
   case nir_intrinsic_load_cull_back_face_enabled_amd:
      replacement = nggc_bool_setting(b, radv_nggc_back_face, s);
      break;
   case nir_intrinsic_load_cull_ccw_amd:
      replacement = nggc_bool_setting(b, radv_nggc_face_is_ccw, s);
      break;
   case nir_intrinsic_load_cull_small_primitives_enabled_amd:
      replacement = nggc_bool_setting(b, radv_nggc_small_primitives, s);
      break;
   case nir_intrinsic_load_cull_small_prim_precision_amd: {
      /* To save space, only the exponent is stored in the high 8 bits.
       * We calculate the precision from those 8 bits:
       * exponent = nggc_settings >> 24
       * precision = 1.0 * 2 ^ exponent
       */
      nir_ssa_def *settings = ac_nir_load_arg(b, &s->args->ac, s->args->ngg_culling_settings);
      nir_ssa_def *exponent = nir_ishr_imm(b, settings, 24u);
      replacement = nir_ldexp(b, nir_imm_float(b, 1.0f), exponent);
      break;
   }

   case nir_intrinsic_load_viewport_xy_scale_and_offset: {
      nir_ssa_def *comps[] = {
         ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_scale[0]),
         ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_scale[1]),
         ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_translate[0]),
         ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_translate[1]),
      };
      replacement = nir_vec(b, comps, 4);
      break;
   }

   case nir_intrinsic_load_ring_task_draw_amd:
      replacement = load_ring(b, RING_TS_DRAW, s);
      break;
   case nir_intrinsic_load_ring_task_payload_amd:
      replacement = load_ring(b, RING_TS_PAYLOAD, s);
      break;
   case nir_intrinsic_load_ring_mesh_scratch_amd:
      replacement = load_ring(b, RING_MS_SCRATCH, s);
      break;
   case nir_intrinsic_load_ring_mesh_scratch_offset_amd:
      /* gs_tg_info[0:11] is ordered_wave_id. Multiply by the ring entry size. */
      replacement = nir_imul_imm(
         b, nir_iand_imm(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_tg_info), 0xfff),
         RADV_MESH_SCRATCH_ENTRY_BYTES);
      break;
   case nir_intrinsic_load_task_ring_entry_amd:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.task_ring_entry);
      break;
   case nir_intrinsic_load_task_ib_addr:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->task_ib_addr);
      break;
   case nir_intrinsic_load_task_ib_stride:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->task_ib_stride);
      break;
   case nir_intrinsic_load_lshs_vertex_stride_amd: {
      unsigned io_num = stage == MESA_SHADER_VERTEX ?
         s->info->vs.num_linked_outputs :
         s->info->tcs.num_linked_inputs;
      replacement = nir_imm_int(b, io_num * 16);
      break;
   }
   case nir_intrinsic_load_hs_out_patch_data_offset_amd: {
      unsigned out_vertices_per_patch = b->shader->info.tess.tcs_vertices_out;
      unsigned num_tcs_outputs = stage == MESA_SHADER_TESS_CTRL ?
         s->info->tcs.num_linked_outputs : s->info->tes.num_linked_inputs;
      int per_vertex_output_patch_size = out_vertices_per_patch * num_tcs_outputs * 16u;

      if (s->pl_key->dynamic_patch_control_points) {
         nir_ssa_def *num_patches;

         if (stage == MESA_SHADER_TESS_CTRL) {
            nir_ssa_def *arg = ac_nir_load_arg(b, &s->args->ac, s->args->tcs_offchip_layout);
            num_patches = nir_ubfe_imm(b, arg, 6, 8);
         } else {
            num_patches = ac_nir_load_arg(b, &s->args->ac, s->args->tes_num_patches);
         }
         replacement = nir_imul_imm(b, num_patches, per_vertex_output_patch_size);
      } else {
         unsigned num_patches = s->info->num_tess_patches;
         replacement = nir_imm_int(b, num_patches * per_vertex_output_patch_size);
      }
      break;
   }
   case nir_intrinsic_load_sample_positions_amd: {
      uint32_t sample_pos_offset = (RING_PS_SAMPLE_POSITIONS * 16) - 8;

      nir_ssa_def *ring_offsets = ac_nir_load_arg(b, &s->args->ac, s->args->ring_offsets);
      nir_ssa_def *addr = nir_pack_64_2x32(b, ring_offsets);
      nir_ssa_def *sample_id = nir_umin(b, intrin->src[0].ssa, nir_imm_int(b, 7));
      nir_ssa_def *offset = nir_ishl_imm(b, sample_id, 3); /* 2 floats containing samplepos.xy */

      nir_const_value *const_num_samples = nir_src_as_const_value(intrin->src[1]);
      if (const_num_samples) {
         sample_pos_offset += (const_num_samples->u32 << 3);
      } else {
         offset = nir_iadd(b, offset, nir_ishl_imm(b, intrin->src[1].ssa, 3));
      }

      replacement = nir_load_global_amd(b, 2, 32, addr, offset,
                                        .base = sample_pos_offset, .access = ACCESS_NON_WRITEABLE);
      break;
   }
   case nir_intrinsic_load_rasterization_samples_amd:
      if (s->pl_key->dynamic_rasterization_samples) {
         replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ps_num_samples);
      } else {
         replacement = nir_imm_int(b, s->pl_key->ps.num_samples);
      }
      break;
   case nir_intrinsic_load_provoking_vtx_in_prim_amd: {
      unsigned provoking_vertex = 0;
      if (s->pl_key->vs.provoking_vtx_last) {
         if (stage == MESA_SHADER_VERTEX) {
            provoking_vertex = radv_get_num_vertices_per_prim(s->pl_key) - 1;
         } else if (stage == MESA_SHADER_GEOMETRY) {
            provoking_vertex = b->shader->info.gs.vertices_in - 1;
         } else {
            /* TES won't use this intrinsic, because it can get primitive id directly
             * instead of using this intrinsic to pass primitive id by LDS.
             */
            unreachable("load_provoking_vtx_in_prim_amd is only supported in VS and GS");
         }
      }
      replacement = nir_imm_int(b, provoking_vertex);
      break;
   }
   case nir_intrinsic_atomic_add_gs_emit_prim_count_amd:
      nir_gds_atomic_add_amd(b, 32, intrin->src[0].ssa,
                             nir_imm_int(b, RADV_NGG_QUERY_PIPELINE_STAT_OFFSET),
                             nir_imm_int(b, 0x100));
      break;
   case nir_intrinsic_atomic_add_gen_prim_count_amd:
      nir_gds_atomic_add_amd(b, 32, intrin->src[0].ssa,
                             nir_imm_int(b, RADV_NGG_QUERY_PRIM_GEN_OFFSET(nir_intrinsic_stream_id(intrin))),
                             nir_imm_int(b, 0x100));
      break;
   case nir_intrinsic_atomic_add_xfb_prim_count_amd:
      nir_gds_atomic_add_amd(b, 32, intrin->src[0].ssa,
                             nir_imm_int(b, RADV_NGG_QUERY_PRIM_XFB_OFFSET(nir_intrinsic_stream_id(intrin))),
                             nir_imm_int(b, 0x100));
      break;

   case nir_intrinsic_load_streamout_config_amd:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.streamout_config);
      break;
   case nir_intrinsic_load_streamout_write_index_amd:
      replacement = ac_nir_load_arg(b, &s->args->ac, s->args->ac.streamout_write_index);
      break;
   case nir_intrinsic_load_streamout_buffer_amd: {
      nir_ssa_def *ptr =
         nir_pack_64_2x32_split(b, ac_nir_load_arg(b, &s->args->ac, s->args->streamout_buffers),
                                nir_imm_int(b, s->address32_hi));
      replacement = nir_load_smem_amd(b, 4, ptr, nir_imm_int(b, nir_intrinsic_base(intrin) * 16));
      break;
   }
   case nir_intrinsic_load_streamout_offset_amd:
      replacement =
         ac_nir_load_arg(b, &s->args->ac, s->args->ac.streamout_offset[nir_intrinsic_base(intrin)]);
      break;

   case nir_intrinsic_load_lds_ngg_gs_out_vertex_base_amd:
      replacement = nir_imm_int(b, s->info->ngg_info.esgs_ring_size);
      break;
   case nir_intrinsic_load_lds_ngg_scratch_base_amd:
      replacement = nir_imm_int(b, s->info->ngg_info.scratch_lds_base);
      break;
   case nir_intrinsic_load_num_vertices_per_primitive_amd: {
      unsigned num_vertices;

      if (stage == MESA_SHADER_VERTEX) {
         num_vertices = radv_get_num_vertices_per_prim(s->pl_key);
      } else if (stage == MESA_SHADER_TESS_EVAL) {
         if (s->info->tes.point_mode) {
            num_vertices = 1;
         } else if (s->info->tes._primitive_mode == TESS_PRIMITIVE_ISOLINES) {
            num_vertices = 2;
         } else {
            num_vertices = 3;
         }
      } else {
         assert(stage == MESA_SHADER_GEOMETRY);
         switch (s->info->gs.output_prim) {
         case SHADER_PRIM_POINTS:
            num_vertices = 1;
            break;
         case SHADER_PRIM_LINE_STRIP:
            num_vertices = 2;
            break;
         case SHADER_PRIM_TRIANGLE_STRIP:
            num_vertices = 3;
            break;
         default:
            unreachable("invalid GS output primitive");
            break;
         }
      }
      replacement = nir_imm_int(b, num_vertices);
      break;
   }
   case nir_intrinsic_load_ordered_id_amd:
      replacement = nir_ubfe_imm(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_tg_info), 0, 12);
      break;
   default:
      progress = false;
      break;
   }

   if (!progress)
      return false;

   if (replacement)
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, replacement);

   nir_instr_remove(instr);
   nir_instr_free(instr);

   return true;
}

void
radv_nir_lower_abi(nir_shader *shader, enum amd_gfx_level gfx_level,
                   const struct radv_shader_info *info, const struct radv_shader_args *args,
                   const struct radv_pipeline_key *pl_key, bool use_llvm, uint32_t address32_hi)
{
   lower_abi_state state = {
      .gfx_level = gfx_level,
      .info = info,
      .args = args,
      .pl_key = pl_key,
      .use_llvm = use_llvm,
      .address32_hi = address32_hi,
   };

   nir_shader_instructions_pass(shader, lower_abi_instr,
                                nir_metadata_dominance | nir_metadata_block_index, &state);
}
