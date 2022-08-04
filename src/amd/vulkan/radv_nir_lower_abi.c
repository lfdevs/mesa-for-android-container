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
lower_abi_instr(nir_builder *b, nir_instr *instr, void *state)
{
   lower_abi_state *s = (lower_abi_state *) state;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   gl_shader_stage stage = b->shader->info.stage;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ring_tess_factors_amd:
      return load_ring(b, RING_HS_TESS_FACTOR, s);

   case nir_intrinsic_load_ring_tess_factors_offset_amd:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ac.tcs_factor_offset);

   case nir_intrinsic_load_ring_tess_offchip_amd:
      return load_ring(b, RING_HS_TESS_OFFCHIP, s);

   case nir_intrinsic_load_ring_tess_offchip_offset_amd:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ac.tess_offchip_offset);

   case nir_intrinsic_load_tcs_num_patches_amd:
      return nir_imm_int(b, s->info->num_tess_patches);

   case nir_intrinsic_load_ring_esgs_amd:
      return load_ring(b, stage == MESA_SHADER_GEOMETRY ? RING_ESGS_GS : RING_ESGS_VS, s);

   case nir_intrinsic_load_ring_es2gs_offset_amd:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ac.es2gs_offset);

   case nir_intrinsic_load_tess_rel_patch_id_amd:
      if (stage == MESA_SHADER_TESS_CTRL) {
         return nir_extract_u8(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.tcs_rel_ids), nir_imm_int(b, 0));
      } else if (stage == MESA_SHADER_TESS_EVAL) {
         /* Setting an upper bound like this will actually make it possible
          * to optimize some multiplications (in address calculations) so that
          * constant additions can be added to the const offset in memory load instructions.
          */
         nir_ssa_def *arg = ac_nir_load_arg(b, &s->args->ac, s->args->ac.tes_rel_patch_id);
         nir_intrinsic_instr *load_arg = nir_instr_as_intrinsic(arg->parent_instr);
         nir_intrinsic_set_arg_upper_bound_u32_amd(load_arg, 2048 / MAX2(b->shader->info.tess.tcs_vertices_out, 1));
         return arg;
      } else {
         unreachable("invalid tessellation shader stage");
      }

   case nir_intrinsic_load_patch_vertices_in:
      if (stage == MESA_SHADER_TESS_CTRL)
         return nir_imm_int(b, s->pl_key->tcs.tess_input_vertices);
      else if (stage == MESA_SHADER_TESS_EVAL)
         return nir_imm_int(b, b->shader->info.tess.tcs_vertices_out);
      else
         unreachable("invalid tessellation shader stage");

   case nir_intrinsic_load_gs_vertex_offset_amd:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_vtx_offset[nir_intrinsic_base(intrin)]);

   case nir_intrinsic_load_workgroup_num_input_vertices_amd:
      return nir_ubfe(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_tg_info),
                         nir_imm_int(b, 12), nir_imm_int(b, 9));

   case nir_intrinsic_load_workgroup_num_input_primitives_amd:
      return nir_ubfe(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_tg_info),
                         nir_imm_int(b, 22), nir_imm_int(b, 9));

   case nir_intrinsic_load_packed_passthrough_primitive_amd:
      /* NGG passthrough mode: the HW already packs the primitive export value to a single register. */
      return ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_vtx_offset[0]);

   case nir_intrinsic_load_shader_query_enabled_amd:
      return nir_ieq_imm(b, ac_nir_load_arg(b, &s->args->ac, s->args->ngg_query_state), 1);

   case nir_intrinsic_load_cull_any_enabled_amd:
      return nggc_bool_setting(b, radv_nggc_front_face | radv_nggc_back_face | radv_nggc_small_primitives, s);

   case nir_intrinsic_load_cull_front_face_enabled_amd:
      return nggc_bool_setting(b, radv_nggc_front_face, s);

   case nir_intrinsic_load_cull_back_face_enabled_amd:
      return nggc_bool_setting(b, radv_nggc_back_face, s);

   case nir_intrinsic_load_cull_ccw_amd:
      return nggc_bool_setting(b, radv_nggc_face_is_ccw, s);

   case nir_intrinsic_load_cull_small_primitives_enabled_amd:
      return nggc_bool_setting(b, radv_nggc_small_primitives, s);

   case nir_intrinsic_load_cull_small_prim_precision_amd: {
      /* To save space, only the exponent is stored in the high 8 bits.
       * We calculate the precision from those 8 bits:
       * exponent = nggc_settings >> 24
       * precision = 1.0 * 2 ^ exponent
       */
      nir_ssa_def *settings = ac_nir_load_arg(b, &s->args->ac, s->args->ngg_culling_settings);
      nir_ssa_def *exponent = nir_ishr_imm(b, settings, 24u);
      return nir_ldexp(b, nir_imm_float(b, 1.0f), exponent);
   }

   case nir_intrinsic_load_viewport_x_scale:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_scale[0]);

   case nir_intrinsic_load_viewport_x_offset:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_translate[0]);

   case nir_intrinsic_load_viewport_y_scale:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_scale[1]);

   case nir_intrinsic_load_viewport_y_offset:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ngg_viewport_translate[1]);

   case nir_intrinsic_load_ring_task_draw_amd:
      return load_ring(b, RING_TS_DRAW, s);

   case nir_intrinsic_load_ring_task_payload_amd:
      return load_ring(b, RING_TS_PAYLOAD, s);

   case nir_intrinsic_load_ring_mesh_scratch_amd:
      return load_ring(b, RING_MS_SCRATCH, s);

   case nir_intrinsic_load_ring_mesh_scratch_offset_amd:
      /* gs_tg_info[0:11] is ordered_wave_id. Multiply by the ring entry size. */
      return nir_imul_imm(b, nir_iand_imm(b, ac_nir_load_arg(b, &s->args->ac, s->args->ac.gs_tg_info), 0xfff),
                                          RADV_MESH_SCRATCH_ENTRY_BYTES);

   case nir_intrinsic_load_task_ring_entry_amd:
      return ac_nir_load_arg(b, &s->args->ac, s->args->ac.task_ring_entry);

   case nir_intrinsic_load_task_ib_addr:
      return ac_nir_load_arg(b, &s->args->ac, s->args->task_ib_addr);

   case nir_intrinsic_load_task_ib_stride:
      return ac_nir_load_arg(b, &s->args->ac, s->args->task_ib_stride);

   case nir_intrinsic_load_lshs_vertex_stride_amd: {
      unsigned io_num = stage == MESA_SHADER_VERTEX ?
         s->info->vs.num_linked_outputs :
         s->info->tcs.num_linked_inputs;
      return nir_imm_int(b, io_num * 16);
   }

   case nir_intrinsic_load_hs_out_patch_data_offset_amd: {
      unsigned num_patches = s->info->num_tess_patches;
      unsigned out_vertices_per_patch = b->shader->info.tess.tcs_vertices_out;
      unsigned num_tcs_outputs = stage == MESA_SHADER_TESS_CTRL ?
         s->info->tcs.num_linked_outputs : s->info->tes.num_linked_inputs;
      int per_vertex_output_patch_size = out_vertices_per_patch * num_tcs_outputs * 16u;
      return nir_imm_int(b, num_patches * per_vertex_output_patch_size);
   }

   default:
      unreachable("invalid NIR RADV ABI intrinsic.");
   }
}

static bool
filter_abi_instr(const nir_instr *instr,
                 UNUSED const void *state)
{
   lower_abi_state *s = (lower_abi_state *) state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return (intrin->intrinsic == nir_intrinsic_load_ring_tess_factors_amd && !s->use_llvm) ||
          (intrin->intrinsic == nir_intrinsic_load_ring_tess_offchip_amd && !s->use_llvm) ||
          (intrin->intrinsic == nir_intrinsic_load_ring_esgs_amd && !s->use_llvm) ||
          intrin->intrinsic == nir_intrinsic_load_ring_tess_factors_offset_amd ||
          intrin->intrinsic == nir_intrinsic_load_ring_tess_offchip_offset_amd ||
          intrin->intrinsic == nir_intrinsic_load_patch_vertices_in ||
          intrin->intrinsic == nir_intrinsic_load_tcs_num_patches_amd ||
          intrin->intrinsic == nir_intrinsic_load_ring_es2gs_offset_amd ||
          intrin->intrinsic == nir_intrinsic_load_tess_rel_patch_id_amd ||
          intrin->intrinsic == nir_intrinsic_load_gs_vertex_offset_amd ||
          intrin->intrinsic == nir_intrinsic_load_workgroup_num_input_vertices_amd ||
          intrin->intrinsic == nir_intrinsic_load_workgroup_num_input_primitives_amd ||
          intrin->intrinsic == nir_intrinsic_load_packed_passthrough_primitive_amd ||
          intrin->intrinsic == nir_intrinsic_load_shader_query_enabled_amd ||
          intrin->intrinsic == nir_intrinsic_load_cull_any_enabled_amd ||
          intrin->intrinsic == nir_intrinsic_load_cull_front_face_enabled_amd ||
          intrin->intrinsic == nir_intrinsic_load_cull_back_face_enabled_amd ||
          intrin->intrinsic == nir_intrinsic_load_cull_ccw_amd ||
          intrin->intrinsic == nir_intrinsic_load_cull_small_primitives_enabled_amd ||
          intrin->intrinsic == nir_intrinsic_load_cull_small_prim_precision_amd ||
          intrin->intrinsic == nir_intrinsic_load_viewport_x_scale ||
          intrin->intrinsic == nir_intrinsic_load_viewport_x_offset ||
          intrin->intrinsic == nir_intrinsic_load_viewport_y_scale ||
          intrin->intrinsic == nir_intrinsic_load_viewport_y_offset ||
          intrin->intrinsic == nir_intrinsic_load_ring_task_draw_amd ||
          intrin->intrinsic == nir_intrinsic_load_ring_task_payload_amd ||
          intrin->intrinsic == nir_intrinsic_load_ring_mesh_scratch_amd ||
          intrin->intrinsic == nir_intrinsic_load_ring_mesh_scratch_offset_amd ||
          intrin->intrinsic == nir_intrinsic_load_task_ring_entry_amd ||
          intrin->intrinsic == nir_intrinsic_load_task_ib_addr ||
          intrin->intrinsic == nir_intrinsic_load_task_ib_stride ||
          intrin->intrinsic == nir_intrinsic_load_lshs_vertex_stride_amd ||
          intrin->intrinsic == nir_intrinsic_load_hs_out_patch_data_offset_amd;
}

void
radv_nir_lower_abi(nir_shader *shader, enum amd_gfx_level gfx_level,
                   const struct radv_shader_info *info, const struct radv_shader_args *args,
                   const struct radv_pipeline_key *pl_key, bool use_llvm)
{
   lower_abi_state state = {
      .gfx_level = gfx_level,
      .info = info,
      .args = args,
      .pl_key = pl_key,
      .use_llvm = use_llvm,
   };

   nir_shader_lower_instructions(shader, filter_abi_instr, lower_abi_instr, &state);
}
