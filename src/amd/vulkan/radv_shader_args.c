/*
 * Copyright © 2019 Valve Corporation.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_shader_args.h"
#include "radv_device.h"
#include "radv_physical_device.h"
#include "radv_shader.h"

#include "util/memstream.h"

struct user_sgpr_info {
   uint64_t inline_push_constant_mask;
   bool inlined_all_push_consts;
   bool indirect_all_descriptor_sets;
   uint8_t remaining_sgprs;
};

static void
allocate_inline_push_consts(const struct radv_shader_info *info, struct user_sgpr_info *user_sgpr_info)
{
   uint8_t remaining_sgprs = user_sgpr_info->remaining_sgprs;

   if (!info->inline_push_constant_mask)
      return;

   uint64_t mask = info->inline_push_constant_mask;
   uint8_t num_push_consts = util_bitcount64(mask);

   /* Disable the default push constants path if all constants can be inlined. */
   if (num_push_consts <= MIN2(remaining_sgprs + 1, AC_MAX_INLINE_PUSH_CONSTS) && info->can_inline_all_push_constants) {
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

struct radv_shader_args_state {
   struct radv_shader_args *args;
   bool gather_debug_info;
   void *ctx;
   const char *arg_names[AC_MAX_ARGS];
   BITSET_DECLARE(user_data, AC_MAX_ARGS);
};

static void
add_ud_arg(struct radv_shader_args_state *state, unsigned size, enum ac_arg_type type, struct ac_arg *arg,
           enum radv_ud_index ud)
{
   ac_add_arg(&state->args->ac, AC_ARG_SGPR, size, type, arg);

   struct radv_userdata_info *ud_info = &state->args->user_sgprs_locs.shader_data[ud];

   if (ud_info->sgpr_idx == -1)
      ud_info->sgpr_idx = state->args->num_user_sgprs;

   ud_info->num_sgprs += size;

   state->args->num_user_sgprs += size;

   if (state->gather_debug_info)
      BITSET_SET(state->user_data, arg->arg_index);
}

#define RADV_ADD_UD_ARG(state, size, type, arg, ud_index)                                                              \
   do {                                                                                                                \
      add_ud_arg(state, size, type, &(state)->args->arg, ud_index);                                                    \
      if ((state)->gather_debug_info) {                                                                                \
         (state)->arg_names[(state)->args->arg.arg_index] = #arg;                                                      \
      }                                                                                                                \
   } while (false)

#define RADV_ADD_UD_ARRAY_ARG(state, size, type, arg, array_index, ud_index)                                           \
   do {                                                                                                                \
      add_ud_arg(state, size, type, &(state)->args->arg[array_index], ud_index);                                       \
      if ((state)->gather_debug_info) {                                                                                \
         (state)->arg_names[(state)->args->arg[array_index].arg_index] =                                               \
            ralloc_asprintf((state)->ctx, "%s[%u]", #arg, array_index);                                                \
      }                                                                                                                \
   } while (false)

#define RADV_ADD_ARG(state, regfile, size, type, arg)                                                                  \
   do {                                                                                                                \
      ac_add_arg(&(state)->args->ac, regfile, size, type, &(state)->args->arg);                                        \
      if ((state)->gather_debug_info) {                                                                                \
         (state)->arg_names[(state)->args->arg.arg_index] = #arg;                                                      \
      }                                                                                                                \
   } while (false)

#define RADV_ADD_ARRAY_ARG(state, regfile, size, type, arg, array_index)                                               \
   do {                                                                                                                \
      ac_add_arg(&(state)->args->ac, regfile, size, type, &(state)->args->arg[array_index]);                           \
      if ((state)->gather_debug_info) {                                                                                \
         (state)->arg_names[(state)->args->arg[array_index].arg_index] =                                               \
            ralloc_asprintf((state)->ctx, "%s[%u]", #arg, array_index);                                                \
      }                                                                                                                \
   } while (false)

#define RADV_ADD_NULL_ARG(state, regfile, size, type) ac_add_arg(&(state)->args->ac, regfile, size, type, NULL)

static void
add_descriptor_set(struct radv_shader_args_state *state, uint32_t set)
{
   RADV_ADD_ARRAY_ARG(state, AC_ARG_SGPR, 1, AC_ARG_CONST_ADDR, descriptors, set);

   struct radv_userdata_info *ud_info = &state->args->user_sgprs_locs.descriptor_sets[set];
   ud_info->sgpr_idx = state->args->num_user_sgprs;
   ud_info->num_sgprs = 1;

   state->args->user_sgprs_locs.descriptor_sets_enabled |= 1u << set;
   state->args->num_user_sgprs++;
}

static void
add_descriptor_heap(struct radv_shader_args_state *state, uint32_t heap)
{
   RADV_ADD_ARRAY_ARG(state, AC_ARG_SGPR, 1, AC_ARG_CONST_ADDR, descriptors, heap);

   struct radv_userdata_info *ud_info = &state->args->user_sgprs_locs.descriptor_heaps[heap];
   ud_info->sgpr_idx = state->args->num_user_sgprs;
   ud_info->num_sgprs = 1;

   state->args->user_sgprs_locs.descriptor_heaps_enabled |= 1u << heap;
   state->args->num_user_sgprs++;
}

static void
declare_global_input_sgprs(struct radv_shader_args_state *state, const enum amd_gfx_level gfx_level,
                           const struct radv_shader_info *info, const struct user_sgpr_info *user_sgpr_info)
{
   if (user_sgpr_info) {
      if (info->descriptor_heap) {
         add_descriptor_heap(state, RADV_HEAP_RESOURCE);
         add_descriptor_heap(state, RADV_HEAP_SAMPLER);
      } else {
         /* 1 for each descriptor set */
         if (!user_sgpr_info->indirect_all_descriptor_sets) {
            uint32_t mask = info->desc_set_used_mask;

            while (mask) {
               int i = u_bit_scan(&mask);

               add_descriptor_set(state, i);
            }
         } else {
            RADV_ADD_UD_ARRAY_ARG(state, 1, AC_ARG_CONST_ADDR, descriptors, 0, AC_UD_INDIRECT_DESCRIPTORS);
         }
      }

      if (info->merged_shader_compiled_separately ||
          (info->loads_push_constants && !user_sgpr_info->inlined_all_push_consts)) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, ac.push_constants, AC_UD_PUSH_CONSTANTS);
      }

      if (info->merged_shader_compiled_separately || info->loads_dynamic_offsets) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, ac.dynamic_descriptors, AC_UD_DYNAMIC_DESCRIPTORS);

         if (info->loads_dynamic_descriptors_offset_addr) {
            RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, ac.dynamic_descriptors_offset_addr,
                            AC_UD_DYNAMIC_DESCRIPTORS_OFFSET_ADDR);
         }
      }

      for (unsigned i = 0; i < util_bitcount64(user_sgpr_info->inline_push_constant_mask); i++) {
         RADV_ADD_UD_ARRAY_ARG(state, 1, AC_ARG_VALUE, ac.inline_push_consts, i, AC_UD_INLINE_PUSH_CONSTANTS);
      }
      state->args->ac.inline_push_const_mask = user_sgpr_info->inline_push_constant_mask;
   }

   const bool needs_streamout_buffers =
      info->so.enabled_stream_buffers_mask ||
      (info->merged_shader_compiled_separately &&
       ((info->stage == MESA_SHADER_VERTEX && info->vs.as_es) ||
        (info->stage == MESA_SHADER_TESS_EVAL && info->tes.as_es) || info->stage == MESA_SHADER_GEOMETRY));

   if (needs_streamout_buffers) {
      RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, streamout_buffers, AC_UD_STREAMOUT_BUFFERS);

      if (gfx_level >= GFX12)
         RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, streamout_state, AC_UD_STREAMOUT_STATE);
   }
}

static void
declare_vs_specific_input_sgprs(struct radv_shader_args_state *state, const struct radv_shader_info *info)
{
   if (info->vs.has_prolog)
      RADV_ADD_UD_ARG(state, 2, AC_ARG_VALUE, prolog_inputs, AC_UD_VS_PROLOG_INPUTS);

   if (info->type != RADV_SHADER_TYPE_GS_COPY) {
      if (info->vs.vb_desc_usage_mask) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, ac.vertex_buffers, AC_UD_VS_VERTEX_BUFFERS);
      }

      RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.base_vertex, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
      if (info->vs.needs_draw_id) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.draw_id, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
      }
      if (info->vs.needs_base_instance) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.start_instance, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
      }
   }
}

static void
declare_vs_input_vgprs(struct radv_shader_args_state *state, enum amd_gfx_level gfx_level,
                       const struct radv_shader_info *info, bool merged_vs_tcs)
{
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.vertex_id);
   if (info->type != RADV_SHADER_TYPE_GS_COPY) {
      if (gfx_level >= GFX12) {
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.instance_id);
      } else if (info->vs.as_ls || merged_vs_tcs) {
         if (gfx_level >= GFX11) {
            RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* user VGPR */
            RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* user VGPR */
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.instance_id);
         } else if (gfx_level >= GFX10) {
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.vs_rel_patch_id);
            RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* user vgpr */
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.instance_id);
         } else {
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.vs_rel_patch_id);
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.instance_id);
            RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* unused */
         }
      } else {
         if (gfx_level >= GFX10) {
            if (info->is_ngg) {
               RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* user vgpr */
               RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* user vgpr */
               RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.instance_id);
            } else {
               RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* unused */
               RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.vs_prim_id);
               RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.instance_id);
            }
         } else {
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.instance_id);
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.vs_prim_id);
            RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* unused */
         }
      }
   }

   if (info->vs.dynamic_inputs) {
      assert(info->vs.use_per_attribute_vb_descs);
      unsigned num_attributes = util_last_bit(info->vs.input_slot_usage_mask);
      for (unsigned i = 0; i < num_attributes; i++) {
         RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 4, AC_ARG_VALUE, vs_inputs, i);

         /* The vertex shader isn't required to consume all components that are loaded by the prolog
          * and it's possible that more VGPRs are written. This specific case is handled at the end
          * of the prolog which waits for all pending VMEM loads if needed.
          */
         state->args->ac.args[state->args->vs_inputs[i].arg_index].pending_vmem = true;
      }
   }
}

static void
declare_streamout_sgprs(struct radv_shader_args_state *state, const struct radv_shader_info *info,
                        mesa_shader_stage stage)
{
   int i;

   /* Streamout SGPRs. */
   if (info->so.enabled_stream_buffers_mask) {
      assert(stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL);

      RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.streamout_config);
      RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.streamout_write_index);
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      RADV_ADD_NULL_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE);
   }

   /* A streamout buffer offset is loaded if the stride is non-zero. */
   for (i = 0; i < 4; i++) {
      if (!info->so.strides[i])
         continue;

      RADV_ADD_ARRAY_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.streamout_offset, i);
   }
}

static void
declare_tes_input_vgprs(struct radv_shader_args_state *state)
{
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tes_u);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tes_v);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tes_rel_patch_id);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tes_patch_id);
}

static void
declare_ms_input_sgprs(struct radv_shader_args_state *state, const struct radv_shader_info *info)
{
   if (info->cs.uses_grid_size) {
      RADV_ADD_UD_ARG(state, 3, AC_ARG_VALUE, ac.num_work_groups, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   }
   if (info->vs.needs_draw_id) {
      RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.draw_id, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   }
   if (info->ms.has_task) {
      RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.task_ring_entry, AC_UD_TASK_RING_ENTRY);
   }
}

static void
declare_ms_input_vgprs(struct radv_shader_args_state *state, const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->info.mesh_fast_launch_2) {
      RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.local_invocation_ids_packed);
   } else {
      RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.vertex_id);
      RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* user vgpr */
      RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* user vgpr */
      RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* instance_id */
   }
}

static void
declare_ps_input_vgprs(struct radv_shader_args_state *state, const struct radv_shader_info *info)
{
   RADV_ADD_ARG(state, AC_ARG_VGPR, 2, AC_ARG_VALUE, ac.persp_sample);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 2, AC_ARG_VALUE, ac.persp_center);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 2, AC_ARG_VALUE, ac.persp_centroid);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 3, AC_ARG_VALUE, ac.pull_model);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 2, AC_ARG_VALUE, ac.linear_sample);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 2, AC_ARG_VALUE, ac.linear_center);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 2, AC_ARG_VALUE, ac.linear_centroid);
   RADV_ADD_NULL_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE); /* line stipple tex */
   RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.frag_pos, 0);
   RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.frag_pos, 1);
   RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.frag_pos, 2);
   RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.frag_pos, 3);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.front_face);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.ancillary);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.sample_coverage);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.pos_fixed_pt);

   if (state->args->remap_spi_ps_input)
      ac_compact_ps_vgpr_args(&state->args->ac, info->ps.spi_ps_input_ena);
}

static void
declare_ngg_sgprs(struct radv_shader_args_state *state, const struct radv_shader_info *info, bool ngg_needs_state_sgpr)
{
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ngg_lds_layout, AC_UD_NGG_LDS_LAYOUT);

   if (ngg_needs_state_sgpr)
      RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ngg_state, AC_UD_NGG_STATE);

   if (info->has_ngg_culling) {
      RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, nggc_settings, AC_UD_NGGC_SETTINGS);
      RADV_ADD_UD_ARRAY_ARG(state, 1, AC_ARG_VALUE, nggc_viewport_scale, 0, AC_UD_NGGC_VIEWPORT);
      RADV_ADD_UD_ARRAY_ARG(state, 1, AC_ARG_VALUE, nggc_viewport_scale, 1, AC_UD_NGGC_VIEWPORT);
      RADV_ADD_UD_ARRAY_ARG(state, 1, AC_ARG_VALUE, nggc_viewport_translate, 0, AC_UD_NGGC_VIEWPORT);
      RADV_ADD_UD_ARRAY_ARG(state, 1, AC_ARG_VALUE, nggc_viewport_translate, 1, AC_UD_NGGC_VIEWPORT);
   }
}

static void
radv_init_shader_args(struct radv_shader_args_state *state, const struct radv_device *device, mesa_shader_stage stage)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   memset(state->args, 0, sizeof(*state->args));

   state->args->explicit_scratch_args = !pdev->use_llvm;
   state->args->remap_spi_ps_input = !pdev->use_llvm;

   for (int i = 0; i < MAX_SETS; i++)
      state->args->user_sgprs_locs.descriptor_sets[i].sgpr_idx = -1;
   for (int i = 0; i < RADV_MAX_HEAPS; i++)
      state->args->user_sgprs_locs.descriptor_heaps[i].sgpr_idx = -1;
   for (int i = 0; i < AC_UD_MAX_UD; i++)
      state->args->user_sgprs_locs.shader_data[i].sgpr_idx = -1;
}

static bool
radv_tcs_needs_state_sgpr(const struct radv_shader_info *info, const struct radv_graphics_state_key *gfx_state)
{
   /* Some values are loaded from a SGPR when dynamic states are used or when the shader is unlinked. */
   return !gfx_state->ts.patch_control_points || !info->num_tess_patches || !info->inputs_linked;
}

static bool
radv_tes_needs_state_sgpr(const struct radv_shader_info *info)
{
   /* Some values are loaded from a SGPR when dynamic states are used or when the shader is unlinked. */
   return !info->num_tess_patches || !info->tes.tcs_vertices_out || !info->inputs_linked;
}

static bool
radv_ps_needs_state_sgpr(const struct radv_shader_info *info, const struct radv_graphics_state_key *gfx_state)
{
   if (info->ps.needs_sample_positions && gfx_state->dynamic_rasterization_samples)
      return true;

   if (gfx_state->dynamic_line_rast_mode)
      return true;

   if (info->ps.reads_sample_mask_in && (info->ps.uses_sample_shading || gfx_state->ms.sample_shading_enable))
      return true;

   /* For computing barycentrics when the primitive topology is unknown at compile time (GPL). */
   if (info->ps.load_rasterization_prim && gfx_state->unknown_rast_prim)
      return true;

   return false;
}

static void
declare_unmerged_vs_tcs_args(struct radv_shader_args_state *state, const enum amd_gfx_level gfx_level,
                             const struct radv_shader_info *info, const struct user_sgpr_info *user_sgpr_info)
{
   /* SGPRs */
   RADV_ADD_UD_ARG(state, 2, AC_ARG_VALUE, prolog_inputs, AC_UD_VS_PROLOG_INPUTS);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, ac.vertex_buffers, AC_UD_VS_VERTEX_BUFFERS);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.base_vertex, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.draw_id, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.start_instance, AC_UD_VS_BASE_VERTEX_START_INSTANCE);

   declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, epilog_pc, AC_UD_EPILOG_PC);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, next_stage_pc, AC_UD_NEXT_STAGE_PC);

   /* VGPRs (TCS first, then VS) */
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tcs_patch_id);
   RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tcs_rel_ids);

   declare_vs_input_vgprs(state, gfx_level, info, true);

   /* Preserved SGPRs */
   ac_add_preserved(&state->args->ac, &state->args->ac.ring_offsets);
   ac_add_preserved(&state->args->ac, &state->args->ac.tess_offchip_offset);
   ac_add_preserved(&state->args->ac, &state->args->ac.merged_wave_info);
   ac_add_preserved(&state->args->ac, &state->args->ac.tcs_factor_offset);

   if (gfx_level >= GFX11) {
      ac_add_preserved(&state->args->ac, &state->args->ac.tcs_wave_id);
   } else {
      ac_add_preserved(&state->args->ac, &state->args->ac.scratch_offset);
   }

   ac_add_preserved(&state->args->ac, &state->args->descriptors[0]);
   ac_add_preserved(&state->args->ac, &state->args->ac.push_constants);
   ac_add_preserved(&state->args->ac, &state->args->ac.dynamic_descriptors);
   ac_add_preserved(&state->args->ac, &state->args->ac.view_index);
   ac_add_preserved(&state->args->ac, &state->args->ac.tcs_offchip_layout);
   ac_add_preserved(&state->args->ac, &state->args->epilog_pc);

   /* Preserved VGPRs */
   ac_add_preserved(&state->args->ac, &state->args->ac.tcs_patch_id);
   ac_add_preserved(&state->args->ac, &state->args->ac.tcs_rel_ids);
}

static void
declare_unmerged_vs_tes_gs_args(struct radv_shader_args_state *state, const enum amd_gfx_level gfx_level,
                                const struct radv_shader_info *info, const struct user_sgpr_info *user_sgpr_info)
{
   /* SGPRs */
   RADV_ADD_UD_ARG(state, 2, AC_ARG_VALUE, prolog_inputs, AC_UD_VS_PROLOG_INPUTS);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, ac.vertex_buffers, AC_UD_VS_VERTEX_BUFFERS);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.base_vertex, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.draw_id, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.start_instance, AC_UD_VS_BASE_VERTEX_START_INSTANCE);

   declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);

   if (info->is_ngg) {
      RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ngg_state, AC_UD_NGG_STATE);
      if (gfx_level >= GFX11)
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ngg_query_buf_va, AC_UD_NGG_QUERY_BUF_VA);
   }
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, vgt_esgs_ring_itemsize, AC_UD_VGT_ESGS_RING_ITEMSIZE);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ngg_lds_layout, AC_UD_NGG_LDS_LAYOUT);
   RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, next_stage_pc, AC_UD_NEXT_STAGE_PC);

   /* VGPRs (GS) */
   if (gfx_level >= GFX12) {
      RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 0);
      RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_prim_id);
      RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 1);
   } else {
      RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 0);
      RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 1);
      RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_prim_id);
      RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_invocation_id);
      RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 2);
   }

   /* Preserved SGPRs */
   ac_add_preserved(&state->args->ac, &state->args->ac.ring_offsets);
   if (info->is_ngg) {
      ac_add_preserved(&state->args->ac, &state->args->ac.gs_tg_info);
   } else {
      ac_add_preserved(&state->args->ac, &state->args->ac.gs2vs_offset);
   }
   ac_add_preserved(&state->args->ac, &state->args->ac.merged_wave_info);
   ac_add_preserved(&state->args->ac, &state->args->ac.tess_offchip_offset);

   if (gfx_level >= GFX11) {
      ac_add_preserved(&state->args->ac, &state->args->ac.gs_attr_offset);
   } else {
      ac_add_preserved(&state->args->ac, &state->args->ac.scratch_offset);
   }

   ac_add_preserved(&state->args->ac, &state->args->descriptors[0]);
   ac_add_preserved(&state->args->ac, &state->args->ac.push_constants);
   ac_add_preserved(&state->args->ac, &state->args->ac.dynamic_descriptors);
   ac_add_preserved(&state->args->ac, &state->args->streamout_buffers);
   if (gfx_level >= GFX12)
      ac_add_preserved(&state->args->ac, &state->args->streamout_state);
   ac_add_preserved(&state->args->ac, &state->args->ac.view_index);
   ac_add_preserved(&state->args->ac, &state->args->ac.tcs_offchip_layout);
   if (info->is_ngg) {
      ac_add_preserved(&state->args->ac, &state->args->ngg_state);
      if (gfx_level >= GFX11)
         ac_add_preserved(&state->args->ac, &state->args->ngg_query_buf_va);
   }
   ac_add_preserved(&state->args->ac, &state->args->vgt_esgs_ring_itemsize);
   ac_add_preserved(&state->args->ac, &state->args->ngg_lds_layout);

   /* Preserved VGPRs */
   ac_add_preserved(&state->args->ac, &state->args->ac.gs_vtx_offset[0]);
   ac_add_preserved(&state->args->ac, &state->args->ac.gs_vtx_offset[1]);
   ac_add_preserved(&state->args->ac, &state->args->ac.gs_prim_id);

   if (gfx_level < GFX12) {
      ac_add_preserved(&state->args->ac, &state->args->ac.gs_invocation_id);
      ac_add_preserved(&state->args->ac, &state->args->ac.gs_vtx_offset[2]);
   }
}

static void
declare_shader_args(struct radv_shader_args_state *state, const struct radv_device *device,
                    const struct radv_graphics_state_key *gfx_state, const struct radv_shader_info *info,
                    mesa_shader_stage stage, mesa_shader_stage previous_stage, struct user_sgpr_info *user_sgpr_info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum amd_gfx_level gfx_level = pdev->info.gfx_level;
   bool has_shader_query = info->has_prim_query || info->has_xfb_query ||
                           (stage == MESA_SHADER_GEOMETRY && info->gs.has_pipeline_stat_query) ||
                           (stage == MESA_SHADER_MESH && info->ms.has_query) ||
                           (stage == MESA_SHADER_TASK && info->cs.has_query);
   bool has_ngg_provoking_vtx =
      (stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_GEOMETRY) && gfx_state->dynamic_provoking_vtx_mode;

   if (gfx_level >= GFX10 && info->is_ngg && stage != MESA_SHADER_GEOMETRY) {
      /* Handle all NGG shaders as GS to simplify the code here. */
      previous_stage = stage;
      stage = MESA_SHADER_GEOMETRY;
   }

   if (info->merged_shader_compiled_separately) {
      /* Update the stage for merged shaders compiled separately with ESO on GFX9+. */
      if (stage == MESA_SHADER_VERTEX && info->vs.as_ls) {
         previous_stage = MESA_SHADER_VERTEX;
         stage = MESA_SHADER_TESS_CTRL;
      } else if (stage == MESA_SHADER_VERTEX && info->vs.as_es) {
         previous_stage = MESA_SHADER_VERTEX;
         stage = MESA_SHADER_GEOMETRY;
      } else if (stage == MESA_SHADER_TESS_EVAL && info->tes.as_es) {
         previous_stage = MESA_SHADER_TESS_EVAL;
         stage = MESA_SHADER_GEOMETRY;
      }
   }

   radv_init_shader_args(state, device, stage);

   if (mesa_shader_stage_is_rt(stage)) {
      return;
   }

   RADV_ADD_UD_ARG(state, 2, AC_ARG_CONST_ADDR, ac.ring_offsets, AC_UD_SCRATCH_RING_OFFSETS);
   if (stage == MESA_SHADER_TASK) {
      RADV_ADD_UD_ARG(state, 2, AC_ARG_CONST_ADDR, task_ring_offsets, AC_UD_CS_TASK_RING_OFFSETS);
   }

   /* For merged shaders the user SGPRs start at 8, with 8 system SGPRs in front (including
    * the rw_buffers at s0/s1. With user SGPR0 = s8, lets restart the count from 0.
    */
   if (previous_stage != MESA_SHADER_NONE)
      state->args->num_user_sgprs = 0;

   /* To ensure prologs match the main VS, VS specific input SGPRs have to be placed before other
    * sgprs.
    */

   switch (stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

      if (info->cs.uses_grid_size) {
         if (device->load_grid_size_from_user_sgpr)
            RADV_ADD_UD_ARG(state, 3, AC_ARG_VALUE, ac.num_work_groups, AC_UD_CS_GRID_SIZE);
         else
            RADV_ADD_UD_ARG(state, 2, AC_ARG_CONST_ADDR, ac.num_work_groups, AC_UD_CS_GRID_SIZE);
      }

      if (info->type == RADV_SHADER_TYPE_RT_PROLOG) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_CONST_ADDR, ac.rt.traversal_shader_addr, AC_UD_CS_TRAVERSAL_SHADER_ADDR);
         RADV_ADD_UD_ARG(state, 2, AC_ARG_CONST_ADDR, ac.rt.sbt_descriptors, AC_UD_CS_SBT_DESCRIPTORS);
         RADV_ADD_UD_ARG(state, 2, AC_ARG_CONST_ADDR, ac.rt.launch_size_addr, AC_UD_CS_RAY_LAUNCH_SIZE_ADDR);
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.rt.dynamic_callable_stack_base,
                         AC_UD_CS_RAY_DYNAMIC_CALLABLE_STACK_BASE);
      }

      if (info->vs.needs_draw_id) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.draw_id, AC_UD_CS_TASK_DRAW_ID);
      }

      if (stage == MESA_SHADER_TASK) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.task_ring_entry, AC_UD_TASK_RING_ENTRY);

         if (has_shader_query) {
            RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, task_state, AC_UD_TASK_STATE);
         }
      }

      for (int i = 0; i < 3; i++) {
         if (info->cs.uses_block_id[i]) {
            if (gfx_level >= GFX12)
               state->args->ac.workgroup_ids[i].used = true;
            else
               RADV_ADD_ARRAY_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.workgroup_ids, i);
         }
      }

      if (info->cs.uses_local_invocation_idx) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tg_size);
      }

      if (state->args->explicit_scratch_args && gfx_level < GFX11) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
      }

      if (pdev->info.compiler_info.local_invocation_ids_packed) {
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.local_invocation_ids_packed);
      } else {
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.local_invocation_id_x);
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.local_invocation_id_y);
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.local_invocation_id_z);
      }
      break;
   case MESA_SHADER_VERTEX:
      /* NGG is handled by the GS case */
      assert(!info->is_ngg);

      declare_vs_specific_input_sgprs(state, info);

      declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

      if (info->uses_view_index) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);
      }

      if (info->force_vrs_per_vertex) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.force_vrs_rates, AC_UD_FORCE_VRS_RATES);
      }

      if (info->vs.as_es) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.es2gs_offset);
      } else if (info->vs.as_ls) {
         /* no extra parameters */
      } else {
         declare_streamout_sgprs(state, info, stage);
      }

      if (state->args->explicit_scratch_args) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
      }

      declare_vs_input_vgprs(state, gfx_level, info, false);
      break;
   case MESA_SHADER_TESS_CTRL:
      if (previous_stage != MESA_SHADER_NONE) {
         /* First 6 system regs */
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tess_offchip_offset);
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.merged_wave_info);
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tcs_factor_offset);

         if (gfx_level >= GFX11) {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tcs_wave_id);
         } else {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
         }

         RADV_ADD_NULL_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE);
         RADV_ADD_NULL_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE);

         if (info->merged_shader_compiled_separately) {
            declare_unmerged_vs_tcs_args(state, gfx_level, info, user_sgpr_info);
         } else {
            declare_vs_specific_input_sgprs(state, info);

            declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

            if (info->uses_view_index) {
               RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);
            }

            if (radv_tcs_needs_state_sgpr(info, gfx_state)) {
               RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);
            }

            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tcs_patch_id);
            RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tcs_rel_ids);

            declare_vs_input_vgprs(state, gfx_level, info, true);
         }
      } else {
         declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

         if (info->uses_view_index) {
            RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);
         }

         if (radv_tcs_needs_state_sgpr(info, gfx_state)) {
            RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);
         }

         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tess_offchip_offset);
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tcs_factor_offset);
         if (state->args->explicit_scratch_args) {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
         }
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tcs_patch_id);
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.tcs_rel_ids);
      }
      break;
   case MESA_SHADER_TESS_EVAL:
      /* NGG is handled by the GS case */
      assert(!info->is_ngg);

      declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

      if (info->uses_view_index)
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);

      if (radv_tes_needs_state_sgpr(info))
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);

      if (info->tes.as_es) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tess_offchip_offset);
         RADV_ADD_NULL_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE);
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.es2gs_offset);
      } else {
         declare_streamout_sgprs(state, info, stage);
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tess_offchip_offset);
      }
      if (state->args->explicit_scratch_args) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
      }
      declare_tes_input_vgprs(state);
      break;
   case MESA_SHADER_GEOMETRY:
      if (previous_stage != MESA_SHADER_NONE) {
         /* First 6 system regs */
         if (info->is_ngg) {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.gs_tg_info);
         } else {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.gs2vs_offset);
         }

         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.merged_wave_info);
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.tess_offchip_offset);

         if (gfx_level >= GFX11) {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.gs_attr_offset);
         } else {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
         }

         RADV_ADD_NULL_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE);
         RADV_ADD_NULL_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE);

         if (info->merged_shader_compiled_separately) {
            declare_unmerged_vs_tes_gs_args(state, gfx_level, info, user_sgpr_info);
         } else {
            if (previous_stage == MESA_SHADER_VERTEX) {
               declare_vs_specific_input_sgprs(state, info);
            } else if (previous_stage == MESA_SHADER_MESH) {
               declare_ms_input_sgprs(state, info);
            }

            declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

            if (info->uses_view_index) {
               RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);
            }

            if (previous_stage == MESA_SHADER_TESS_EVAL && radv_tes_needs_state_sgpr(info))
               RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.tcs_offchip_layout, AC_UD_TCS_OFFCHIP_LAYOUT);

            /* Legacy GS force vrs is handled by GS copy shader. */
            if (info->force_vrs_per_vertex && info->is_ngg) {
               RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.force_vrs_rates, AC_UD_FORCE_VRS_RATES);
            }

            if (info->is_ngg) {
               const bool ngg_needs_state_sgpr =
                  has_ngg_provoking_vtx || has_shader_query ||
                  (previous_stage == MESA_SHADER_VERTEX && info->vs.dynamic_num_verts_per_prim);

               declare_ngg_sgprs(state, info, ngg_needs_state_sgpr);

               if (pdev->info.gfx_level >= GFX11 && has_shader_query)
                  RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ngg_query_buf_va, AC_UD_NGG_QUERY_BUF_VA);
            }

            if (previous_stage != MESA_SHADER_MESH || !pdev->info.mesh_fast_launch_2) {
               if (gfx_level >= GFX12) {
                  RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 0);
                  RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_prim_id);
                  RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 1);
               } else {
                  RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 0);
                  RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 1);
                  RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_prim_id);
                  RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_invocation_id);
                  RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 2);
               }
            }
         }

         if (previous_stage == MESA_SHADER_VERTEX) {
            declare_vs_input_vgprs(state, gfx_level, info, false);
         } else if (previous_stage == MESA_SHADER_TESS_EVAL) {
            declare_tes_input_vgprs(state);
         } else if (previous_stage == MESA_SHADER_MESH) {
            declare_ms_input_vgprs(state, device);
         }
      } else {
         declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

         if (info->uses_view_index) {
            RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.view_index, AC_UD_VIEW_INDEX);
         }

         if (info->force_vrs_per_vertex) {
            RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ac.force_vrs_rates, AC_UD_FORCE_VRS_RATES);
         }

         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.gs2vs_offset);
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.gs_wave_id);
         if (state->args->explicit_scratch_args) {
            RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
         }
         RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 0);
         RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 1);
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_prim_id);
         RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 2);
         RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 3);
         RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 4);
         RADV_ADD_ARRAY_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_vtx_offset, 5);
         RADV_ADD_ARG(state, AC_ARG_VGPR, 1, AC_ARG_VALUE, ac.gs_invocation_id);
      }
      break;
   case MESA_SHADER_FRAGMENT:
      declare_global_input_sgprs(state, gfx_level, info, user_sgpr_info);

      if (info->ps.has_epilog) {
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, epilog_pc, AC_UD_EPILOG_PC);
      }

      if (radv_ps_needs_state_sgpr(info, gfx_state))
         RADV_ADD_UD_ARG(state, 1, AC_ARG_VALUE, ps_state, AC_UD_PS_STATE);

      RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.prim_mask);

      if (info->ps.pops && gfx_level < GFX11) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.pops_collision_wave_id);
      }

      if (info->ps.load_provoking_vtx) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.load_provoking_vtx);
      }

      if (state->args->explicit_scratch_args && gfx_level < GFX11) {
         RADV_ADD_ARG(state, AC_ARG_SGPR, 1, AC_ARG_VALUE, ac.scratch_offset);
      }

      declare_ps_input_vgprs(state, info);
      break;
   default:
      UNREACHABLE("Shader stage not implemented");
   }
}

static void
radv_gather_shader_args_debug_info(struct radv_shader_args_state *state, struct radv_shader_debug_info *debug)
{
   char *data = NULL;
   size_t size = 0;
   struct u_memstream mem;
   if (u_memstream_open(&mem, &data, &size)) {
      FILE *const memf = u_memstream_get(&mem);

      for (uint32_t i = 0; i < state->args->ac.arg_count; i++) {
         fprintf(memf, "   %u.", i);
         switch (state->args->ac.args[i].file) {
         case AC_ARG_SGPR:
            fprintf(memf, " sgpr");
            break;
         case AC_ARG_VGPR:
            fprintf(memf, " vgpr");
            break;
         }
         switch (state->args->ac.args[i].type) {
         case AC_ARG_VALUE:
            fprintf(memf, " value");
            break;
         case AC_ARG_CONST_ADDR:
            fprintf(memf, " const_addr");
            break;
         }
         if (state->args->ac.args[i].skip)
            fprintf(memf, " skip");
         if (state->args->ac.args[i].pending_vmem)
            fprintf(memf, " pending_vmem");
         if (state->args->ac.args[i].preserved)
            fprintf(memf, " preserved");
         if (BITSET_TEST(state->user_data, i))
            fprintf(memf, " user_data");
         fprintf(memf, " offset=%u size=%u name=%s\n", state->args->ac.args[i].offset, state->args->ac.args[i].size,
                 state->arg_names[i] ? state->arg_names[i] : "(null)");
      }

      u_memstream_close(&mem);
   }

   debug->args_string = malloc(size + 1);
   if (debug->args_string) {
      memcpy(debug->args_string, data, size);
      debug->args_string[size] = 0;
   }
   free(data);
}

void
radv_declare_shader_args(const struct radv_device *device, const struct radv_graphics_state_key *gfx_state,
                         const struct radv_shader_info *info, mesa_shader_stage stage, mesa_shader_stage previous_stage,
                         struct radv_shader_args *args, struct radv_shader_debug_info *debug)
{
   struct radv_shader_args_state state = {
      .args = args,
   };

   struct user_sgpr_info user_sgpr_info = {0};

   if (!mesa_shader_stage_is_rt(stage)) {
      declare_shader_args(&state, device, gfx_state, info, stage, previous_stage, NULL);

      uint32_t num_user_sgprs = args->num_user_sgprs;
      if (info->loads_push_constants)
         num_user_sgprs++;
      if (info->loads_dynamic_offsets) {
         num_user_sgprs++;
         if (info->loads_dynamic_descriptors_offset_addr)
            num_user_sgprs++;
      }

      const struct radv_physical_device *pdev = radv_device_physical(device);
      const enum amd_gfx_level gfx_level = pdev->info.gfx_level;
      uint32_t available_sgprs =
         gfx_level >= GFX9 && stage != MESA_SHADER_COMPUTE && stage != MESA_SHADER_TASK ? 32 : 16;
      uint32_t remaining_sgprs = available_sgprs - num_user_sgprs;

      user_sgpr_info.remaining_sgprs = remaining_sgprs;

      if (info->descriptor_heap) {
         assert(user_sgpr_info.remaining_sgprs >= RADV_MAX_HEAPS);
         user_sgpr_info.remaining_sgprs -= RADV_MAX_HEAPS;
      } else {
         const uint32_t num_desc_set = util_bitcount(info->desc_set_used_mask);

         if (info->force_indirect_descriptors || remaining_sgprs < num_desc_set) {
            user_sgpr_info.indirect_all_descriptor_sets = true;
            user_sgpr_info.remaining_sgprs--;
         } else {
            user_sgpr_info.remaining_sgprs -= num_desc_set;
         }
      }

      if (!info->merged_shader_compiled_separately)
         allocate_inline_push_consts(info, &user_sgpr_info);
   }

   state.gather_debug_info = debug && device->keep_shader_info;
   if (state.gather_debug_info) {
      state.ctx = ralloc_context(NULL);
      state.gather_debug_info &= !!state.ctx;
   }

   declare_shader_args(&state, device, gfx_state, info, stage, previous_stage, &user_sgpr_info);

   if (state.gather_debug_info)
      radv_gather_shader_args_debug_info(&state, debug);

   ralloc_free(state.ctx);
}

void
radv_declare_ps_epilog_args(const struct radv_device *device, const struct radv_ps_epilog_key *key,
                            struct radv_shader_args *args)
{
   struct radv_shader_args_state state = {
      .args = args,
   };

   radv_init_shader_args(&state, device, MESA_SHADER_FRAGMENT);

   /* Declare VGPR arguments for depth/stencil/sample exports. */
   if (key->export_depth)
      RADV_ADD_ARG(&state, AC_ARG_VGPR, 1, AC_ARG_VALUE, depth);
   if (key->export_stencil)
      RADV_ADD_ARG(&state, AC_ARG_VGPR, 1, AC_ARG_VALUE, stencil);
   if (key->export_sample_mask)
      RADV_ADD_ARG(&state, AC_ARG_VGPR, 1, AC_ARG_VALUE, sample_mask);

   /* Declare VGPR arguments for color exports. */
   for (unsigned i = 0; i < MAX_RTS; i++) {
      const uint8_t color = (key->colors_written >> (i * 4) & 0xf);

      if (!color) {
         RADV_ADD_NULL_ARG(&state, AC_ARG_VGPR, 4, AC_ARG_VALUE);
         continue;
      }

      RADV_ADD_ARRAY_ARG(&state, AC_ARG_VGPR, 4, AC_ARG_VALUE, colors, i);
   }
}
