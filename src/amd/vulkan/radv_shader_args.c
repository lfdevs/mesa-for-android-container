/*
 * Copyright © 2019 Valve Corporation.
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

#include "radv_shader_args.h"
#include "radv_private.h"
#include "radv_shader.h"

struct user_sgpr_info {
   uint64_t inline_push_constant_mask;
   bool inlined_all_push_consts;
   bool indirect_all_descriptor_sets;
   uint8_t remaining_sgprs;
};

static void
allocate_inline_push_consts(const struct radv_shader_info *info,
                            struct user_sgpr_info *user_sgpr_info)
{
   uint8_t remaining_sgprs = user_sgpr_info->remaining_sgprs;

   if (!info->inline_push_constant_mask)
      return;

   uint64_t mask = info->inline_push_constant_mask;
   uint8_t num_push_consts = util_bitcount64(mask);

   /* Disable the default push constants path if all constants can be inlined and if shaders don't
    * use dynamic descriptors.
    */
   if (num_push_consts <= MIN2(remaining_sgprs + 1, AC_MAX_INLINE_PUSH_CONSTS) &&
       info->can_inline_all_push_constants && !info->loads_dynamic_offsets) {
      user_sgpr_info->inlined_all_push_consts = true;
      remaining_sgprs++;
   } else {
      /* Clamp to the maximum number of allowed inlined push constants. */
      while (num_push_consts > MIN2(remaining_sgprs, AC_MAX_INLINE_PUSH_CONSTS_WITH_INDIRECT)) {
         num_push_consts--;
         mask &= ~BITFIELD64_BIT(util_last_bit64(mask) - 1);
      }
   }

   user_sgpr_info->remaining_sgprs = remaining_sgprs - util_bitcount64(mask);
   user_sgpr_info->inline_push_constant_mask = mask;
}

static void
add_ud_arg(struct radv_shader_args *args, unsigned size, enum ac_arg_type type, struct ac_arg *arg,
           enum radv_ud_index ud)
{
   ac_add_arg(&args->ac, AC_ARG_SGPR, size, type, arg);

   struct radv_userdata_info *ud_info = &args->user_sgprs_locs.shader_data[ud];

   if (ud_info->sgpr_idx == -1)
      ud_info->sgpr_idx = args->num_user_sgprs;

   ud_info->num_sgprs += size;

   args->num_user_sgprs += size;
}

static void
add_descriptor_set(struct radv_shader_args *args, enum ac_arg_type type, struct ac_arg *arg,
                   uint32_t set)
{
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, type, arg);

   struct radv_userdata_info *ud_info = &args->user_sgprs_locs.descriptor_sets[set];
   ud_info->sgpr_idx = args->num_user_sgprs;
   ud_info->num_sgprs = 1;

   args->user_sgprs_locs.descriptor_sets_enabled |= 1u << set;
   args->num_user_sgprs++;
}

static void
declare_global_input_sgprs(const struct radv_shader_info *info,
                           const struct user_sgpr_info *user_sgpr_info,
                           struct radv_shader_args *args)
{
   if (user_sgpr_info) {
      /* 1 for each descriptor set */
      if (!user_sgpr_info->indirect_all_descriptor_sets) {
         uint32_t mask = info->desc_set_used_mask;

         while (mask) {
            int i = u_bit_scan(&mask);

            add_descriptor_set(args, AC_ARG_CONST_PTR, &args->descriptor_sets[i], i);
         }
      } else {
         add_ud_arg(args, 1, AC_ARG_CONST_PTR_PTR, &args->descriptor_sets[0],
                    AC_UD_INDIRECT_DESCRIPTOR_SETS);
      }

      if (info->loads_push_constants && !user_sgpr_info->inlined_all_push_consts) {
         /* 1 for push constants and dynamic descriptors */
         add_ud_arg(args, 1, AC_ARG_CONST_PTR, &args->ac.push_constants, AC_UD_PUSH_CONSTANTS);
      }

      for (unsigned i = 0; i < util_bitcount64(user_sgpr_info->inline_push_constant_mask); i++) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.inline_push_consts[i],
                    AC_UD_INLINE_PUSH_CONSTANTS);
      }
      args->ac.inline_push_const_mask = user_sgpr_info->inline_push_constant_mask;
   }

   if (info->so.num_outputs) {
      add_ud_arg(args, 1, AC_ARG_CONST_DESC_PTR, &args->streamout_buffers, AC_UD_STREAMOUT_BUFFERS);
   }
}

static void
declare_vs_specific_input_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args,
                                gl_shader_stage stage, gl_shader_stage previous_stage)
{
   if (info->vs.has_prolog)
      add_ud_arg(args, 2, AC_ARG_INT, &args->prolog_inputs, AC_UD_VS_PROLOG_INPUTS);

   if (args->type != RADV_SHADER_TYPE_GS_COPY &&
       (stage == MESA_SHADER_VERTEX || previous_stage == MESA_SHADER_VERTEX)) {
      if (info->vs.vb_desc_usage_mask) {
         add_ud_arg(args, 1, AC_ARG_CONST_DESC_PTR, &args->ac.vertex_buffers,
                    AC_UD_VS_VERTEX_BUFFERS);
      }

      add_ud_arg(args, 1, AC_ARG_INT, &args->ac.base_vertex, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
      if (info->vs.needs_draw_id) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.draw_id, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
      }
      if (info->vs.needs_base_instance) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.start_instance,
                    AC_UD_VS_BASE_VERTEX_START_INSTANCE);
      }
   }
}

static void
declare_vs_input_vgprs(enum amd_gfx_level gfx_level, const struct radv_shader_info *info,
                       struct radv_shader_args *args, bool merged_vs_tcs)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vertex_id);
   if (args->type != RADV_SHADER_TYPE_GS_COPY) {
      if (info->vs.as_ls || merged_vs_tcs) {

         if (gfx_level >= GFX11) {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user VGPR */
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user VGPR */
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
         } else if (gfx_level >= GFX10) {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_rel_patch_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
         } else {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_rel_patch_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
         }
      } else {
         if (gfx_level >= GFX10) {
            if (info->is_ngg) {
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            } else {
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_prim_id);
               ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            }
         } else {
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vs_prim_id);
            ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
         }
      }
   }

   if (info->vs.dynamic_inputs) {
      assert(info->vs.use_per_attribute_vb_descs);
      unsigned num_attributes = util_last_bit(info->vs.input_slot_usage_mask);
      for (unsigned i = 0; i < num_attributes; i++) {
         ac_add_arg(&args->ac, AC_ARG_VGPR, 4, AC_ARG_INT, &args->vs_inputs[i]);
         args->ac.args[args->vs_inputs[i].arg_index].pending_vmem = true;
      }
      /* Ensure the main shader doesn't use less vgprs than the prolog. The prolog requires one
       * VGPR more than the number of shader arguments in the case of non-trivial divisors on GFX8.
       */
      ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL);
   }
}

static void
declare_streamout_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args,
                        gl_shader_stage stage)
{
   int i;

   /* Streamout SGPRs. */
   if (info->so.num_outputs) {
      assert(stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL);

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.streamout_config);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.streamout_write_index);
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
   }

   /* A streamout buffer offset is loaded if the stride is non-zero. */
   for (i = 0; i < 4; i++) {
      if (!info->so.strides[i])
         continue;

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.streamout_offset[i]);
   }
}

static void
declare_tes_input_vgprs(struct radv_shader_args *args)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.tes_u);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.tes_v);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tes_rel_patch_id);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tes_patch_id);
}

static void
declare_ms_input_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args)
{
   add_ud_arg(args, 3, AC_ARG_INT, &args->ac.num_work_groups, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   if (info->vs.needs_draw_id) {
      add_ud_arg(args, 1, AC_ARG_INT, &args->ac.draw_id, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   }
   if (info->ms.has_task) {
      add_ud_arg(args, 1, AC_ARG_INT, &args->ac.task_ring_entry, AC_UD_TASK_RING_ENTRY);
   }
}

static void
declare_ms_input_vgprs(struct radv_shader_args *args)
{
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vertex_id);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* instance_id */
}

static void
declare_ps_input_vgprs(const struct radv_shader_info *info, struct radv_shader_args *args)
{
   unsigned spi_ps_input = info->ps.spi_ps_input;

   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_sample);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_center);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_centroid);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 3, AC_ARG_INT, &args->ac.pull_model);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_sample);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_center);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_centroid);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, NULL); /* line stipple tex */
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[0]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[1]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[2]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[3]);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.front_face);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.ancillary);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.sample_coverage);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* fixed pt */

   if (args->remap_spi_ps_input) {
      /* LLVM optimizes away unused FS inputs and computes spi_ps_input_addr itself and then
       * communicates the results back via the ELF binary. Mirror what LLVM does by re-mapping the
       * VGPR arguments here.
       */
      for (unsigned i = 0, vgpr_arg = 0, vgpr_reg = 0; i < args->ac.arg_count; i++) {
         if (args->ac.args[i].file != AC_ARG_VGPR) {
            continue;
         }

         if (!(spi_ps_input & (1 << vgpr_arg))) {
            args->ac.args[i].skip = true;
         } else {
            args->ac.args[i].offset = vgpr_reg;
            vgpr_reg += args->ac.args[i].size;
         }
         vgpr_arg++;
      }
   }
}

static void
declare_ngg_sgprs(const struct radv_shader_info *info, struct radv_shader_args *args,
                  bool has_ngg_query, bool has_ngg_provoking_vtx)
{
   if (has_ngg_query)
      add_ud_arg(args, 1, AC_ARG_INT, &args->ngg_query_state, AC_UD_NGG_QUERY_STATE);

   if (has_ngg_provoking_vtx)
      add_ud_arg(args, 1, AC_ARG_INT, &args->ngg_provoking_vtx, AC_UD_NGG_PROVOKING_VTX);

   if (info->has_ngg_culling) {
      add_ud_arg(args, 1, AC_ARG_INT, &args->ngg_culling_settings, AC_UD_NGG_CULLING_SETTINGS);
      add_ud_arg(args, 1, AC_ARG_INT, &args->ngg_viewport_scale[0], AC_UD_NGG_VIEWPORT);
      add_ud_arg(args, 1, AC_ARG_INT, &args->ngg_viewport_scale[1], AC_UD_NGG_VIEWPORT);
      add_ud_arg(args, 1, AC_ARG_INT, &args->ngg_viewport_translate[0], AC_UD_NGG_VIEWPORT);
      add_ud_arg(args, 1, AC_ARG_INT, &args->ngg_viewport_translate[1], AC_UD_NGG_VIEWPORT);
   }
}

static void
radv_init_shader_args(const struct radv_device *device, gl_shader_stage stage,
                      enum radv_shader_type type, struct radv_shader_args *args)
{
   memset(args, 0, sizeof(*args));

   args->explicit_scratch_args = !radv_use_llvm_for_stage(device, stage);
   args->remap_spi_ps_input = !radv_use_llvm_for_stage(device, stage);
   args->load_grid_size_from_user_sgpr = device->load_grid_size_from_user_sgpr;
   args->type = type;

   for (int i = 0; i < MAX_SETS; i++)
      args->user_sgprs_locs.descriptor_sets[i].sgpr_idx = -1;
   for (int i = 0; i < AC_UD_MAX_UD; i++)
      args->user_sgprs_locs.shader_data[i].sgpr_idx = -1;
}

void
radv_declare_rt_shader_args(enum amd_gfx_level gfx_level, struct radv_shader_args *args)
{
   add_ud_arg(args, 2, AC_ARG_CONST_PTR, &args->ac.rt_shader_pc, AC_UD_SCRATCH_RING_OFFSETS);
   add_ud_arg(args, 1, AC_ARG_CONST_PTR_PTR, &args->descriptor_sets[0],
              AC_UD_INDIRECT_DESCRIPTOR_SETS);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_PTR, &args->ac.push_constants);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_DESC_PTR, &args->ac.sbt_descriptors);
   ac_add_arg(&args->ac, AC_ARG_SGPR, 3, AC_ARG_INT, &args->ac.ray_launch_size);
   if (gfx_level < GFX9) {
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_DESC_PTR, &args->ac.ring_offsets);
   }

   ac_add_arg(&args->ac, AC_ARG_VGPR, 3, AC_ARG_INT, &args->ac.ray_launch_id);
   ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.rt_dynamic_callable_stack_base);
}

static void
declare_shader_args(const struct radv_device *device, const struct radv_pipeline_key *key,
                    const struct radv_shader_info *info, gl_shader_stage stage,
                    gl_shader_stage previous_stage, enum radv_shader_type type,
                    struct radv_shader_args *args, struct user_sgpr_info *user_sgpr_info)
{
   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   bool needs_view_index = info->uses_view_index;
   bool has_ngg_query = info->has_ngg_prim_query || info->has_ngg_xfb_query ||
                        (stage == MESA_SHADER_GEOMETRY && info->gs.has_ngg_pipeline_stat_query);
   bool has_ngg_provoking_vtx = (stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_GEOMETRY) &&
                                key->dynamic_provoking_vtx_mode;

   if (gfx_level >= GFX10 && info->is_ngg && stage != MESA_SHADER_GEOMETRY) {
      /* Handle all NGG shaders as GS to simplify the code here. */
      previous_stage = stage;
      stage = MESA_SHADER_GEOMETRY;
   }

   radv_init_shader_args(device, stage, type, args);

   if (gl_shader_stage_is_rt(stage)) {
      radv_declare_rt_shader_args(gfx_level, args);
      return;
   }

   add_ud_arg(args, 2, AC_ARG_CONST_DESC_PTR, &args->ac.ring_offsets, AC_UD_SCRATCH_RING_OFFSETS);
   if (stage == MESA_SHADER_TASK) {
      add_ud_arg(args, 2, AC_ARG_CONST_DESC_PTR, &args->task_ring_offsets,
                 AC_UD_CS_TASK_RING_OFFSETS);
   }

   /* For merged shaders the user SGPRs start at 8, with 8 system SGPRs in front (including
    * the rw_buffers at s0/s1. With user SGPR0 = s8, lets restart the count from 0.
    */
   if (previous_stage != MESA_SHADER_NONE)
      args->num_user_sgprs = 0;

   /* To ensure prologs match the main VS, VS specific input SGPRs have to be placed before other
    * sgprs.
    */

   switch (stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      declare_global_input_sgprs(info, user_sgpr_info, args);

      if (info->cs.uses_grid_size) {
         if (args->load_grid_size_from_user_sgpr)
            add_ud_arg(args, 3, AC_ARG_INT, &args->ac.num_work_groups, AC_UD_CS_GRID_SIZE);
         else
            add_ud_arg(args, 2, AC_ARG_CONST_PTR, &args->ac.num_work_groups, AC_UD_CS_GRID_SIZE);
      }

      if (info->cs.is_rt_shader) {
         add_ud_arg(args, 2, AC_ARG_CONST_DESC_PTR, &args->ac.sbt_descriptors,
                    AC_UD_CS_SBT_DESCRIPTORS);
         add_ud_arg(args, 2, AC_ARG_CONST_PTR, &args->ac.ray_launch_size_addr,
                    AC_UD_CS_RAY_LAUNCH_SIZE_ADDR);
         add_ud_arg(args, 2, AC_ARG_CONST_PTR, &args->ac.rt_traversal_shader_addr,
                    AC_UD_CS_TRAVERSAL_SHADER_ADDR);
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.rt_dynamic_callable_stack_base,
                    AC_UD_CS_RAY_DYNAMIC_CALLABLE_STACK_BASE);
      }

      if (info->vs.needs_draw_id) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.draw_id, AC_UD_CS_TASK_DRAW_ID);
      }

      if (stage == MESA_SHADER_TASK) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.task_ring_entry, AC_UD_TASK_RING_ENTRY);
      }

      for (int i = 0; i < 3; i++) {
         if (info->cs.uses_block_id[i]) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.workgroup_ids[i]);
         }
      }

      if (info->cs.uses_local_invocation_idx) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tg_size);
      }

      if (args->explicit_scratch_args && gfx_level < GFX11) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }

      if (gfx_level >= GFX11)
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.local_invocation_ids);
      else
         ac_add_arg(&args->ac, AC_ARG_VGPR, 3, AC_ARG_INT, &args->ac.local_invocation_ids);
      break;
   case MESA_SHADER_VERTEX:
      /* NGG is handled by the GS case */
      assert(!info->is_ngg);

      declare_vs_specific_input_sgprs(info, args, stage, previous_stage);

      declare_global_input_sgprs(info, user_sgpr_info, args);

      if (needs_view_index) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.view_index, AC_UD_VIEW_INDEX);
      }

      if (info->force_vrs_per_vertex) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.force_vrs_rates, AC_UD_FORCE_VRS_RATES);
      }

      if (info->vs.as_es) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.es2gs_offset);
      } else if (info->vs.as_ls) {
         /* no extra parameters */
      } else {
         declare_streamout_sgprs(info, args, stage);
      }

      if (args->explicit_scratch_args) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }

      declare_vs_input_vgprs(gfx_level, info, args, false);
      break;
   case MESA_SHADER_TESS_CTRL:
      if (previous_stage != MESA_SHADER_NONE) {
         // First 6 system regs
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.merged_wave_info);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tcs_factor_offset);

         if (gfx_level >= GFX11) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tcs_wave_id);
         } else {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown

         declare_vs_specific_input_sgprs(info, args, stage, previous_stage);

         declare_global_input_sgprs(info, user_sgpr_info, args);

         if (needs_view_index) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->ac.view_index, AC_UD_VIEW_INDEX);
         }

         if (key->dynamic_patch_control_points) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);
         }

         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_patch_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_rel_ids);

         declare_vs_input_vgprs(gfx_level, info, args, true);
      } else {
         declare_global_input_sgprs(info, user_sgpr_info, args);

         if (needs_view_index) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->ac.view_index, AC_UD_VIEW_INDEX);
         }

         if (key->dynamic_patch_control_points) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tcs_factor_offset);
         if (args->explicit_scratch_args) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_patch_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tcs_rel_ids);
      }
      break;
   case MESA_SHADER_TESS_EVAL:
      /* NGG is handled by the GS case */
      assert(!info->is_ngg);

      declare_global_input_sgprs(info, user_sgpr_info, args);

      if (needs_view_index)
         add_ud_arg(args, 1, AC_ARG_INT, &args->ac.view_index, AC_UD_VIEW_INDEX);

      if (key->dynamic_patch_control_points)
         add_ud_arg(args, 1, AC_ARG_INT, &args->tes_num_patches, AC_UD_TES_NUM_PATCHES);

      if (info->tes.as_es) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.es2gs_offset);
      } else {
         declare_streamout_sgprs(info, args, stage);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);
      }
      if (args->explicit_scratch_args) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }
      declare_tes_input_vgprs(args);
      break;
   case MESA_SHADER_GEOMETRY:
      if (previous_stage != MESA_SHADER_NONE) {
         // First 6 system regs
         if (info->is_ngg) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs_tg_info);
         } else {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs2vs_offset);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.merged_wave_info);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.tess_offchip_offset);

         if (gfx_level >= GFX11) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs_attr_offset);
         } else {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown

         if (previous_stage == MESA_SHADER_VERTEX) {
            declare_vs_specific_input_sgprs(info, args, stage, previous_stage);
         } else if (previous_stage == MESA_SHADER_MESH) {
            declare_ms_input_sgprs(info, args);
         }

         declare_global_input_sgprs(info, user_sgpr_info, args);

         if (needs_view_index) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->ac.view_index, AC_UD_VIEW_INDEX);
         }

         if (previous_stage == MESA_SHADER_TESS_EVAL && key->dynamic_patch_control_points)
            add_ud_arg(args, 1, AC_ARG_INT, &args->tes_num_patches, AC_UD_TES_NUM_PATCHES);

         if (previous_stage == MESA_SHADER_VERTEX && info->vs.dynamic_num_verts_per_prim)
            add_ud_arg(args, 1, AC_ARG_INT, &args->num_verts_per_prim, AC_UD_NUM_VERTS_PER_PRIM);

         /* Legacy GS force vrs is handled by GS copy shader. */
         if (info->force_vrs_per_vertex && info->is_ngg) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->ac.force_vrs_rates, AC_UD_FORCE_VRS_RATES);
         }

         if (info->is_ngg) {
            declare_ngg_sgprs(info, args, has_ngg_query, has_ngg_provoking_vtx);
         }

         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[0]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[1]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_prim_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_invocation_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[2]);

         if (previous_stage == MESA_SHADER_VERTEX) {
            declare_vs_input_vgprs(gfx_level, info, args, false);
         } else if (previous_stage == MESA_SHADER_TESS_EVAL) {
            declare_tes_input_vgprs(args);
         } else if (previous_stage == MESA_SHADER_MESH) {
            declare_ms_input_vgprs(args);
         }
      } else {
         declare_global_input_sgprs(info, user_sgpr_info, args);

         if (needs_view_index) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->ac.view_index, AC_UD_VIEW_INDEX);
         }

         if (info->force_vrs_per_vertex) {
            add_ud_arg(args, 1, AC_ARG_INT, &args->ac.force_vrs_rates, AC_UD_FORCE_VRS_RATES);
         }

         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs2vs_offset);
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.gs_wave_id);
         if (args->explicit_scratch_args) {
            ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
         }
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[0]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[1]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_prim_id);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[2]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[3]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[4]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_vtx_offset[5]);
         ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.gs_invocation_id);
      }
      break;
   case MESA_SHADER_FRAGMENT:
      declare_global_input_sgprs(info, user_sgpr_info, args);

      if (info->ps.has_epilog) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ps_epilog_pc, AC_UD_PS_EPILOG_PC);
      }

      if (info->ps.needs_sample_positions && key->dynamic_rasterization_samples) {
         add_ud_arg(args, 1, AC_ARG_INT, &args->ps_num_samples, AC_UD_PS_NUM_SAMPLES);
      }

      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.prim_mask);
      if (args->explicit_scratch_args && gfx_level < GFX11) {
         ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);
      }

      declare_ps_input_vgprs(info, args);
      break;
   default:
      unreachable("Shader stage not implemented");
   }
}

void
radv_declare_shader_args(const struct radv_device *device, const struct radv_pipeline_key *key,
                         const struct radv_shader_info *info, gl_shader_stage stage,
                         gl_shader_stage previous_stage, enum radv_shader_type type,
                         struct radv_shader_args *args)
{
   declare_shader_args(device, key, info, stage, previous_stage, type, args, NULL);

   if (gl_shader_stage_is_rt(stage))
      return;

   uint32_t num_user_sgprs = args->num_user_sgprs;
   if (info->loads_push_constants)
      num_user_sgprs++;

   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;
   uint32_t available_sgprs =
      gfx_level >= GFX9 && stage != MESA_SHADER_COMPUTE && stage != MESA_SHADER_TASK ? 32 : 16;
   uint32_t remaining_sgprs = available_sgprs - num_user_sgprs;

   struct user_sgpr_info user_sgpr_info = {
      .remaining_sgprs = remaining_sgprs,
   };

   uint32_t num_desc_set = util_bitcount(info->desc_set_used_mask);

   if (remaining_sgprs < num_desc_set) {
      user_sgpr_info.indirect_all_descriptor_sets = true;
      user_sgpr_info.remaining_sgprs--;
   } else {
      user_sgpr_info.remaining_sgprs -= num_desc_set;
   }

   allocate_inline_push_consts(info, &user_sgpr_info);

   declare_shader_args(device, key, info, stage, previous_stage, type, args, &user_sgpr_info);
}

void
radv_declare_ps_epilog_args(const struct radv_device *device, const struct radv_ps_epilog_key *key,
                            struct radv_shader_args *args)
{
   const enum amd_gfx_level gfx_level = device->physical_device->rad_info.gfx_level;

   radv_init_shader_args(device, MESA_SHADER_FRAGMENT, RADV_SHADER_TYPE_DEFAULT, args);

   ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_DESC_PTR, &args->ac.ring_offsets);
   if (gfx_level < GFX11)
      ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.scratch_offset);

   /* Declare VGPR arguments for color exports. */
   for (unsigned i = 0; i < MAX_RTS; i++) {
      unsigned col_format = (key->spi_shader_col_format >> (i * 4)) & 0xf;

      if (col_format == V_028714_SPI_SHADER_ZERO)
         continue;

      ac_add_arg(&args->ac, AC_ARG_VGPR, 4, AC_ARG_FLOAT, &args->ps_epilog_inputs[i]);
   }
}
