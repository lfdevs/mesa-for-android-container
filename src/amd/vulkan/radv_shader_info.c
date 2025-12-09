/*
 * Copyright © 2017 Red Hat
 *
 * SPDX-License-Identifier: MIT
 */
#include "radv_shader_info.h"
#include "nir/nir.h"
#include "nir/nir_xfb_info.h"
#include "nir/radv_nir.h"
#include "ac_shader_util.h"
#include "nir_tcs_info.h"
#include "radv_device.h"
#include "radv_physical_device.h"
#include "radv_pipeline_graphics.h"
#include "radv_shader.h"

#include "ac_nir.h"

static void
mark_sampler_desc(const nir_variable *var, struct radv_shader_info *info)
{
   info->desc_set_used_mask |= (1u << var->data.descriptor_set);
}

static bool
radv_use_vs_prolog(const nir_shader *nir, const struct radv_graphics_state_key *gfx_state)
{
   return gfx_state->vs.has_prolog && nir->info.inputs_read;
}

static bool
radv_use_per_attribute_vb_descs(const nir_shader *nir, const struct radv_graphics_state_key *gfx_state,
                                const struct radv_shader_stage_key *stage_key)
{
   return stage_key->vertex_robustness1 || radv_use_vs_prolog(nir, gfx_state);
}

static void
gather_load_vs_input_info(const nir_shader *nir, const nir_intrinsic_instr *intrin, struct radv_shader_info *info,
                          const struct radv_graphics_state_key *gfx_state,
                          const struct radv_shader_stage_key *stage_key)
{
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   const unsigned location = io_sem.location;
   const unsigned component = nir_intrinsic_component(intrin);
   unsigned mask = nir_def_components_read(&intrin->def);
   mask = (intrin->def.bit_size == 64 ? util_widen_mask(mask, 2) : mask) << component;

   if (location >= VERT_ATTRIB_GENERIC0) {
      const unsigned generic_loc = location - VERT_ATTRIB_GENERIC0;

      if (gfx_state->vi.instance_rate_inputs & BITFIELD_BIT(generic_loc)) {
         info->vs.needs_instance_id = true;
         info->vs.needs_base_instance = true;
      }

      if (radv_use_per_attribute_vb_descs(nir, gfx_state, stage_key))
         info->vs.vb_desc_usage_mask |= BITFIELD_BIT(generic_loc);
      else
         info->vs.vb_desc_usage_mask |= BITFIELD_BIT(gfx_state->vi.vertex_attribute_bindings[generic_loc]);

      info->vs.input_slot_usage_mask |= BITFIELD_RANGE(generic_loc, io_sem.num_slots);
   }
}

static void
gather_load_fs_input_info(const nir_shader *nir, const nir_intrinsic_instr *intrin, struct radv_shader_info *info,
                          const struct radv_graphics_state_key *gfx_state)
{
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   const unsigned location = io_sem.location;
   const unsigned mapped_location = nir_intrinsic_base(intrin);
   const unsigned attrib_count = io_sem.num_slots;
   const unsigned component = nir_intrinsic_component(intrin);

   switch (location) {
   case VARYING_SLOT_CLIP_DIST0:
      info->ps.input_clips_culls_mask |= BITFIELD_RANGE(component, intrin->num_components);
      break;
   case VARYING_SLOT_CLIP_DIST1:
      info->ps.input_clips_culls_mask |= BITFIELD_RANGE(component, intrin->num_components) << 4;
      break;
   default:
      break;
   }

   const uint32_t mapped_mask = BITFIELD_RANGE(mapped_location, attrib_count);
   const bool per_primitive = nir->info.per_primitive_inputs & BITFIELD64_BIT(location);

   if (!per_primitive) {
      if (intrin->intrinsic == nir_intrinsic_load_input_vertex) {
         if (io_sem.interp_explicit_strict)
            info->ps.explicit_strict_shaded_mask |= mapped_mask;
         else
            info->ps.explicit_shaded_mask |= mapped_mask;
      } else if (intrin->intrinsic == nir_intrinsic_load_interpolated_input && intrin->def.bit_size == 16) {
         if (io_sem.high_16bits)
            info->ps.float16_hi_shaded_mask |= mapped_mask;
         else
            info->ps.float16_shaded_mask |= mapped_mask;
      } else if (intrin->intrinsic == nir_intrinsic_load_interpolated_input) {
         info->ps.float32_shaded_mask |= mapped_mask;
      }
   }

   if (location >= VARYING_SLOT_VAR0) {
      const uint32_t var_mask = BITFIELD_RANGE(location - VARYING_SLOT_VAR0, attrib_count);

      if (per_primitive)
         info->ps.input_per_primitive_mask |= var_mask;
      else
         info->ps.input_mask |= var_mask;
   }
}

static void
gather_intrinsic_load_input_info(const nir_shader *nir, const nir_intrinsic_instr *instr, struct radv_shader_info *info,
                                 const struct radv_graphics_state_key *gfx_state,
                                 const struct radv_shader_stage_key *stage_key)
{
   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX:
      gather_load_vs_input_info(nir, instr, info, gfx_state, stage_key);
      break;
   case MESA_SHADER_FRAGMENT:
      gather_load_fs_input_info(nir, instr, info, gfx_state);
      break;
   default:
      break;
   }
}

static void
gather_intrinsic_store_output_info(const nir_shader *nir, const nir_intrinsic_instr *instr,
                                   struct radv_shader_info *info, bool consider_force_vrs)
{
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(instr);
   const unsigned location = io_sem.location;
   const unsigned component = nir_intrinsic_component(instr);
   const unsigned write_mask = nir_intrinsic_write_mask(instr);

   switch (nir->info.stage) {
   case MESA_SHADER_FRAGMENT:
      if (location >= FRAG_RESULT_DATA0) {
         const unsigned fs_semantic = location + io_sem.dual_source_blend_index;
         info->ps.colors_written |= 0xfu << (4 * (fs_semantic - FRAG_RESULT_DATA0));

         if (fs_semantic == FRAG_RESULT_DATA0)
            info->ps.color0_written = write_mask;
      }
      break;
   default:
      break;
   }

   if (consider_force_vrs && location == VARYING_SLOT_POS) {
      unsigned pos_w_chan = 3 - component;

      if (write_mask & BITFIELD_BIT(pos_w_chan)) {
         nir_scalar pos_w = nir_scalar_resolved(instr->src[0].ssa, pos_w_chan);
         /* Use coarse shading if the value of Pos.W can't be determined or if its value is != 1
          * (typical for non-GUI elements).
          */
         if (!nir_scalar_is_const(pos_w) || nir_scalar_as_uint(pos_w) != 0x3f800000u)
            info->force_vrs_per_vertex = true;
      }
   }

   if ((location == VARYING_SLOT_CLIP_DIST0 || location == VARYING_SLOT_CLIP_DIST1) && !io_sem.no_sysval_output) {
      unsigned base = (location == VARYING_SLOT_CLIP_DIST1 ? 4 : 0) + component;
      unsigned clip_array_mask = BITFIELD_MASK(nir->info.clip_distance_array_size);
      info->outinfo.clip_dist_mask |= (write_mask << base) & clip_array_mask;
      info->outinfo.cull_dist_mask |= (write_mask << base) & ~clip_array_mask;
   }
}

static void
gather_push_constant_info(const nir_shader *nir, const nir_intrinsic_instr *instr, struct radv_shader_info *info)
{
   uint32_t offset, size;

   if (nir_src_is_const(instr->src[0])) {
      offset = nir_intrinsic_base(instr) + nir_src_as_uint(instr->src[0]);
      size = instr->num_components * (instr->def.bit_size / 8u);
   } else {
      offset = nir_intrinsic_base(instr);
      size = nir_intrinsic_range(instr);
   }

   info->loads_push_constants = true;
   info->push_constant_size = MAX2(info->push_constant_size, offset + size);

   if (nir_src_is_const(instr->src[0]) && instr->def.bit_size >= 32) {
      const uint32_t start_dw = offset / 4;
      const uint32_t size_dw = size / 4;

      if (start_dw + size_dw <= (MAX_PUSH_CONSTANTS_SIZE / 4u)) {
         info->inline_push_constant_mask |= BITFIELD64_RANGE(start_dw, size_dw);
         return;
      }
   }

   info->can_inline_all_push_constants = false;
}

static void
gather_intrinsic_info(const nir_shader *nir, const nir_intrinsic_instr *instr, struct radv_shader_info *info,
                      const struct radv_graphics_state_key *gfx_state, const struct radv_shader_stage_key *stage_key,
                      bool consider_force_vrs)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_at_offset: {
      enum glsl_interp_mode mode = nir_intrinsic_interp_mode(instr);
      switch (mode) {
      case INTERP_MODE_SMOOTH:
      case INTERP_MODE_NONE:
         if (instr->intrinsic == nir_intrinsic_load_barycentric_pixel ||
             instr->intrinsic == nir_intrinsic_load_barycentric_at_sample ||
             instr->intrinsic == nir_intrinsic_load_barycentric_at_offset)
            info->ps.reads_persp_center = true;
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_centroid)
            info->ps.reads_persp_centroid = true;
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_sample)
            info->ps.reads_persp_sample = true;
         break;
      case INTERP_MODE_NOPERSPECTIVE:
         if (instr->intrinsic == nir_intrinsic_load_barycentric_pixel ||
             instr->intrinsic == nir_intrinsic_load_barycentric_at_sample ||
             instr->intrinsic == nir_intrinsic_load_barycentric_at_offset)
            info->ps.reads_linear_center = true;
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_centroid)
            info->ps.reads_linear_centroid = true;
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_sample)
            info->ps.reads_linear_sample = true;
         break;
      default:
         break;
      }
      if (instr->intrinsic == nir_intrinsic_load_barycentric_at_sample)
         info->ps.needs_sample_positions = true;
      break;
   }
   case nir_intrinsic_load_provoking_vtx_amd:
      info->ps.load_provoking_vtx = true;
      break;
   case nir_intrinsic_load_sample_positions_amd:
      info->ps.needs_sample_positions = true;
      break;
   case nir_intrinsic_load_rasterization_primitive_amd:
      info->ps.load_rasterization_prim = true;
      break;
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_workgroup_id: {
      unsigned mask = nir_def_components_read(&instr->def);
      while (mask) {
         unsigned i = u_bit_scan(&mask);

         if (instr->intrinsic == nir_intrinsic_load_workgroup_id)
            info->cs.uses_block_id[i] = true;
         else
            info->cs.uses_thread_id[i] = true;
      }
      break;
   }
   case nir_intrinsic_load_pixel_coord:
      info->ps.reads_pixel_coord = true;
      break;
   case nir_intrinsic_load_frag_coord:
      info->ps.reads_frag_coord_mask |= nir_def_components_read(&instr->def);
      break;
   case nir_intrinsic_load_sample_pos:
      info->ps.reads_sample_pos_mask |= nir_def_components_read(&instr->def);
      break;
   case nir_intrinsic_load_push_constant:
      gather_push_constant_info(nir, instr, info);
      break;
   case nir_intrinsic_vulkan_resource_index:
      info->desc_set_used_mask |= (1u << nir_intrinsic_desc_set(instr));
      break;
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples: {
      nir_variable *var = nir_deref_instr_get_variable(nir_def_as_deref(instr->src[0].ssa));
      mark_sampler_desc(var, info);
      break;
   }
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_primitive_input:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_input_vertex:
      gather_intrinsic_load_input_info(nir, instr, info, gfx_state, stage_key);
      break;
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
      gather_intrinsic_store_output_info(nir, instr, info, consider_force_vrs);
      break;
   case nir_intrinsic_load_poly_line_smooth_enabled:
      info->ps.needs_poly_line_smooth = true;
      break;
   case nir_intrinsic_begin_invocation_interlock:
      info->ps.pops = true;
      break;
   default:
      break;
   }
}

static void
gather_tex_info(const nir_shader *nir, const nir_tex_instr *instr, struct radv_shader_info *info)
{
   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_texture_deref:
         mark_sampler_desc(nir_deref_instr_get_variable(nir_src_as_deref(instr->src[i].src)), info);
         break;
      case nir_tex_src_sampler_deref:
         mark_sampler_desc(nir_deref_instr_get_variable(nir_src_as_deref(instr->src[i].src)), info);
         break;
      default:
         break;
      }
   }
}

static void
gather_info_block(const nir_shader *nir, const nir_block *block, struct radv_shader_info *info,
                  const struct radv_graphics_state_key *gfx_state, const struct radv_shader_stage_key *stage_key,
                  bool consider_force_vrs)
{
   nir_foreach_instr (instr, block) {
      switch (instr->type) {
      case nir_instr_type_intrinsic:
         gather_intrinsic_info(nir, nir_instr_as_intrinsic(instr), info, gfx_state, stage_key, consider_force_vrs);
         break;
      case nir_instr_type_tex:
         gather_tex_info(nir, nir_instr_as_tex(instr), info);
         break;
      default:
         break;
      }
   }
}

static void
gather_xfb_info(const nir_shader *nir, struct radv_shader_info *info)
{
   struct radv_streamout_info *so = &info->so;

   if (!nir->xfb_info)
      return;

   const nir_xfb_info *xfb = nir->xfb_info;

   u_foreach_bit (output_buffer, xfb->buffers_written) {
      unsigned stream = xfb->buffer_to_stream[output_buffer];
      so->enabled_stream_buffers_mask |= (1 << output_buffer) << (stream * 4);
      so->strides[output_buffer] = xfb->buffers[output_buffer].stride / 4;
   }
}

static void
assign_outinfo_param(struct radv_vs_output_info *outinfo, gl_varying_slot idx, unsigned *total_param_exports,
                     unsigned extra_offset)
{
   if (outinfo->vs_output_param_offset[idx] == AC_EXP_PARAM_UNDEFINED)
      outinfo->vs_output_param_offset[idx] = extra_offset + (*total_param_exports)++;
}

static void
assign_outinfo_params(struct radv_vs_output_info *outinfo, uint64_t mask, unsigned *total_param_exports,
                      unsigned extra_offset)
{
   u_foreach_bit64 (idx, mask) {
      if (idx >= VARYING_SLOT_VAR0 || idx == VARYING_SLOT_LAYER || idx == VARYING_SLOT_PRIMITIVE_ID ||
          idx == VARYING_SLOT_VIEWPORT)
         assign_outinfo_param(outinfo, idx, total_param_exports, extra_offset);
   }
}

static void
radv_get_output_masks(const struct nir_shader *nir, const struct radv_graphics_state_key *gfx_state,
                      uint64_t *per_vtx_mask, uint64_t *per_prim_mask)
{
   /* These are not compiled into neither output param nor position exports. */
   const uint64_t special_mask =
      VARYING_BIT_PRIMITIVE_COUNT | VARYING_BIT_PRIMITIVE_INDICES | VARYING_BIT_CULL_PRIMITIVE;

   *per_prim_mask = nir->info.outputs_written & nir->info.per_primitive_outputs & ~special_mask;
   *per_vtx_mask = nir->info.outputs_written & ~nir->info.per_primitive_outputs & ~special_mask;

   /* Mesh multiview is only lowered in ac_nir_lower_ngg, so we have to fake it here. */
   if (nir->info.stage == MESA_SHADER_MESH && gfx_state->has_multiview_view_index)
      *per_prim_mask |= VARYING_BIT_LAYER;
}

static void
radv_set_vs_output_param(struct radv_device *device, const struct nir_shader *nir,
                         const struct radv_graphics_state_key *gfx_state, struct radv_shader_info *info,
                         bool export_prim_id, bool export_clip_cull_dists)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_vs_output_info *outinfo = &info->outinfo;
   uint64_t per_vtx_mask, per_prim_mask;

   radv_get_output_masks(nir, gfx_state, &per_vtx_mask, &per_prim_mask);

   memset(outinfo->vs_output_param_offset, AC_EXP_PARAM_UNDEFINED, sizeof(outinfo->vs_output_param_offset));

   /* Implicit primitive ID for VS and TES is added by ac_nir_lower_legacy_vs / ac_nir_lower_ngg,
    * it can be configured as either a per-vertex or per-primitive output depending on the GPU.
    */
   const bool implicit_prim_id_per_prim =
      export_prim_id && info->is_ngg && pdev->info.gfx_level >= GFX10_3 && nir->info.stage == MESA_SHADER_VERTEX;
   const bool implicit_prim_id_per_vertex =
      export_prim_id && !implicit_prim_id_per_prim &&
      (nir->info.stage == MESA_SHADER_VERTEX || nir->info.stage == MESA_SHADER_TESS_EVAL);

   unsigned total_param_exports = 0;

   /* Per-vertex outputs */
   assign_outinfo_params(outinfo, per_vtx_mask, &total_param_exports, 0);

   if (implicit_prim_id_per_vertex) {
      /* Mark the primitive ID as output when it's implicitly exported by VS or TES. */
      if (outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] == AC_EXP_PARAM_UNDEFINED)
         outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = total_param_exports++;

      outinfo->export_prim_id = true;
   }

   if (export_clip_cull_dists) {
      if (nir->info.outputs_written & VARYING_BIT_CLIP_DIST0)
         outinfo->vs_output_param_offset[VARYING_SLOT_CLIP_DIST0] = total_param_exports++;
      if (nir->info.outputs_written & VARYING_BIT_CLIP_DIST1)
         outinfo->vs_output_param_offset[VARYING_SLOT_CLIP_DIST1] = total_param_exports++;
   }

   outinfo->param_exports = total_param_exports;

   /* The HW always assumes that there is at least 1 per-vertex param.
    * so if there aren't any, we have to offset per-primitive params by 1.
    */
   const unsigned extra_offset = !!(total_param_exports == 0 && pdev->info.gfx_level >= GFX11);

   if (implicit_prim_id_per_prim) {
      /* Mark the primitive ID as output when it's implicitly exported by VS. */
      if (outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] == AC_EXP_PARAM_UNDEFINED)
         outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = extra_offset + total_param_exports++;

      outinfo->export_prim_id_per_primitive = true;
   }

   /* Per-primitive outputs: the HW needs these to be last. */
   assign_outinfo_params(outinfo, per_prim_mask, &total_param_exports, extra_offset);

   outinfo->prim_param_exports = total_param_exports - outinfo->param_exports;
}

static uint32_t
radv_compute_esgs_itemsize(const struct radv_device *device, uint32_t num_varyings)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint32_t esgs_itemsize;

   esgs_itemsize = num_varyings * 16;

   /* For the ESGS ring in LDS, add 1 dword to reduce LDS bank
    * conflicts, i.e. each vertex will start on a different bank.
    */
   if (pdev->info.gfx_level >= GFX9 && esgs_itemsize)
      esgs_itemsize += 4;

   return esgs_itemsize;
}

static void
gather_shader_info_ngg_query(struct radv_device *device, struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   info->gs.has_pipeline_stat_query = pdev->emulate_ngg_gs_query_pipeline_stat && info->stage == MESA_SHADER_GEOMETRY;
   info->has_xfb_query = !!info->so.enabled_stream_buffers_mask;
   info->has_prim_query = device->cache_key.primitives_generated_query || info->has_xfb_query;
}

uint64_t
radv_gather_unlinked_io_mask(const uint64_t nir_io_mask)
{
   /* Create a mask of driver locations mapped from NIR semantics. */
   uint64_t radv_io_mask = 0;
   u_foreach_bit64 (semantic, nir_io_mask) {
      /* These outputs are not used when fixed output slots are needed. */
      if (semantic == VARYING_SLOT_LAYER || semantic == VARYING_SLOT_VIEWPORT ||
          semantic == VARYING_SLOT_PRIMITIVE_ID || semantic == VARYING_SLOT_PRIMITIVE_SHADING_RATE)
         continue;

      radv_io_mask |= BITFIELD64_BIT(radv_map_io_driver_location(semantic));
   }

   return radv_io_mask;
}

uint64_t
radv_gather_unlinked_patch_io_mask(const uint64_t nir_io_mask, const uint32_t nir_patch_io_mask)
{
   uint64_t radv_io_mask = 0;
   u_foreach_bit64 (semantic, nir_patch_io_mask) {
      radv_io_mask |= BITFIELD64_BIT(radv_map_io_driver_location(semantic + VARYING_SLOT_PATCH0));
   }

   /* Tess levels need to be handled separately because they are not part of patch_outputs_written. */
   if (nir_io_mask & VARYING_BIT_TESS_LEVEL_OUTER)
      radv_io_mask |= BITFIELD64_BIT(radv_map_io_driver_location(VARYING_SLOT_TESS_LEVEL_OUTER));
   if (nir_io_mask & VARYING_BIT_TESS_LEVEL_INNER)
      radv_io_mask |= BITFIELD64_BIT(radv_map_io_driver_location(VARYING_SLOT_TESS_LEVEL_INNER));

   return radv_io_mask;
}

static void
gather_shader_info_vs(struct radv_device *device, const nir_shader *nir,
                      const struct radv_graphics_state_key *gfx_state, const struct radv_shader_stage_key *stage_key,
                      struct radv_shader_info *info)
{
   if (radv_use_vs_prolog(nir, gfx_state)) {
      info->vs.has_prolog = true;
      info->vs.dynamic_inputs = true;
   }

   info->gs_inputs_read = ~0ULL;
   info->vs.tcs_inputs_via_lds = ~0ULL;

   /* Use per-attribute vertex descriptors to prevent faults and for correct bounds checking. */
   info->vs.use_per_attribute_vb_descs = radv_use_per_attribute_vb_descs(nir, gfx_state, stage_key);

   /* We have to ensure consistent input register assignments between the main shader and the
    * prolog.
    */
   info->vs.needs_instance_id |= info->vs.has_prolog;
   info->vs.needs_base_instance |= info->vs.has_prolog;
   info->vs.needs_draw_id |= info->vs.has_prolog;

   if (info->vs.dynamic_inputs) {
      info->vs.num_attributes = util_last_bit(info->vs.vb_desc_usage_mask);
      info->vs.vb_desc_usage_mask = BITFIELD_MASK(info->vs.num_attributes);
   }

   /* When the topology is unknown (with GPL), the number of vertices per primitive needs be passed
    * through a user SGPR for NGG streamout with VS. Otherwise, the XFB offset is incorrectly
    * computed because using the maximum number of vertices can't work.
    */
   info->vs.dynamic_num_verts_per_prim = gfx_state->ia.topology == V_008958_DI_PT_NONE && info->is_ngg && nir->xfb_info;

   if (!info->outputs_linked)
      info->vs.num_linked_outputs = util_last_bit64(radv_gather_unlinked_io_mask(nir->info.outputs_written));

   if (info->next_stage == MESA_SHADER_TESS_CTRL) {
      info->vs.as_ls = true;
   } else if (info->next_stage == MESA_SHADER_GEOMETRY) {
      info->vs.as_es = true;
      info->esgs_itemsize = radv_compute_esgs_itemsize(device, info->vs.num_linked_outputs);
   }

   if (info->is_ngg) {
      info->vs.num_outputs = nir->num_outputs;

      if (info->next_stage == MESA_SHADER_FRAGMENT || info->next_stage == MESA_SHADER_NONE) {
         gather_shader_info_ngg_query(device, info);
      }
   }
}

static void
gather_shader_info_tcs(struct radv_device *device, const nir_shader *nir,
                       const struct radv_graphics_state_key *gfx_state, struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   ac_nir_map_io_driver_location map_output = info->outputs_linked ? NULL : radv_map_io_driver_location;

   nir_tcs_info tcs_info;
   nir_gather_tcs_info(nir, &tcs_info, nir->info.tess._primitive_mode, nir->info.tess.spacing);
   ac_nir_get_tess_io_info(nir, &tcs_info, ~0ull, ~0, map_output, true, &info->tcs.io_info);

   info->tcs.spacing = nir->info.tess.spacing;
   info->tcs.ccw = nir->info.tess.ccw;
   info->tcs.point_mode = nir->info.tess.point_mode;
   info->tcs.tcs_vertices_out = nir->info.tess.tcs_vertices_out;
   info->tcs.tes_inputs_read = ~0ULL;
   info->tcs.tes_patch_inputs_read = ~0ULL;

   if (!info->inputs_linked)
      info->tcs.num_linked_inputs = util_last_bit64(radv_gather_unlinked_io_mask(nir->info.inputs_read));

   if (gfx_state->ts.patch_control_points) {
      radv_get_tess_wg_info(pdev, &info->tcs.io_info, nir->info.tess.tcs_vertices_out,
                            gfx_state->ts.patch_control_points,
                            /* TODO: This should be only inputs in LDS (not VGPR inputs) to reduce LDS usage */
                            info->tcs.num_linked_inputs, &info->num_tess_patches, &info->tcs.lds_size);
   }
}

static void
gather_shader_info_tes(struct radv_device *device, const nir_shader *nir, struct radv_shader_info *info)
{
   info->gs_inputs_read = ~0ULL;
   info->tes._primitive_mode = nir->info.tess._primitive_mode;
   info->tes.spacing = nir->info.tess.spacing;
   info->tes.ccw = nir->info.tess.ccw;
   info->tes.point_mode = nir->info.tess.point_mode;
   info->tes.tcs_vertices_out = nir->info.tess.tcs_vertices_out;
   info->tes.reads_tess_factors =
      !!(nir->info.inputs_read & (VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER));

   if (!info->inputs_linked) {
      info->tes.num_linked_inputs = util_last_bit64(radv_gather_unlinked_io_mask(
         nir->info.inputs_read & ~(VARYING_BIT_TESS_LEVEL_OUTER | VARYING_BIT_TESS_LEVEL_INNER)));
      info->tes.num_linked_patch_inputs =
         util_last_bit64(radv_gather_unlinked_patch_io_mask(nir->info.inputs_read, nir->info.patch_inputs_read));
   }
   if (!info->outputs_linked)
      info->tes.num_linked_outputs = util_last_bit64(radv_gather_unlinked_io_mask(nir->info.outputs_written));

   if (info->next_stage == MESA_SHADER_GEOMETRY) {
      info->tes.as_es = true;
      info->esgs_itemsize = radv_compute_esgs_itemsize(device, info->tes.num_linked_outputs);
   }

   if (info->is_ngg) {
      info->tes.num_outputs = nir->num_outputs;

      if (info->next_stage == MESA_SHADER_FRAGMENT || info->next_stage == MESA_SHADER_NONE) {
         gather_shader_info_ngg_query(device, info);
      }
   }
}

void
radv_get_legacy_gs_info(const struct radv_device *device, struct radv_shader_info *es_info, struct radv_shader_info *gs_info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_legacy_gs_info *out = &gs_info->legacy_gs_info;
   const unsigned esgs_vertex_stride = es_info ? es_info->esgs_itemsize : out->esgs_itemsize;
   ac_legacy_gs_subgroup_info info;

   ac_legacy_gs_compute_subgroup_info(gs_info->gs.input_prim, gs_info->gs.vertices_out, gs_info->gs.invocations,
                                      esgs_vertex_stride, &info);

   const uint32_t lds_granularity = ac_shader_get_lds_alloc_granularity(pdev->info.gfx_level);
   const uint32_t total_lds_bytes = align(info.esgs_lds_size * 4, lds_granularity);

   out->gs_inst_prims_in_subgroup = info.gs_inst_prims_in_subgroup;
   out->es_verts_per_subgroup = info.es_verts_per_subgroup;
   out->gs_prims_per_subgroup = info.gs_prims_per_subgroup;
   out->lds_size = total_lds_bytes;

   unsigned num_se = pdev->info.max_se;
   unsigned wave_size = 64;
   unsigned max_gs_waves = 32 * num_se; /* max 32 per SE on GCN */
   /* On GFX6-GFX7, the value comes from VGT_GS_VERTEX_REUSE = 16.
    * On GFX8+, the value comes from VGT_VERTEX_REUSE_BLOCK_CNTL = 30 (+2).
    */
   unsigned gs_vertex_reuse = (pdev->info.gfx_level >= GFX8 ? 32 : 16) * num_se;
   unsigned alignment = 256 * num_se;
   /* The maximum size is 63.999 MB per SE. */
   unsigned max_size = ((unsigned)(63.999 * 1024 * 1024) & ~255) * num_se;

   /* Calculate the minimum size. */
   unsigned min_esgs_ring_size = align(esgs_vertex_stride * gs_vertex_reuse * wave_size, alignment);
   /* These are recommended sizes, not minimum sizes. */
   unsigned esgs_ring_size = max_gs_waves * 2 * wave_size * esgs_vertex_stride * gs_info->gs.vertices_in;

   unsigned gsvs_emit_size = 0;
   for (unsigned stream = 0; stream < 4; stream++) {
      gsvs_emit_size += (uint32_t)gs_info->gs.num_components_per_stream[stream] * 4 * gs_info->gs.vertices_out;
   }

   unsigned gsvs_ring_size = max_gs_waves * 2 * wave_size * gsvs_emit_size;

   min_esgs_ring_size = align(min_esgs_ring_size, alignment);
   esgs_ring_size = align(esgs_ring_size, alignment);
   gsvs_ring_size = align(gsvs_ring_size, alignment);

   if (pdev->info.gfx_level <= GFX8)
      out->esgs_ring_size = CLAMP(esgs_ring_size, min_esgs_ring_size, max_size);

   out->gsvs_ring_size = MIN2(gsvs_ring_size, max_size);
}

static void
gather_shader_info_gs(struct radv_device *device, const nir_shader *nir, struct radv_shader_info *info)
{
   info->gs.vertices_in = nir->info.gs.vertices_in;
   info->gs.vertices_out = nir->info.gs.vertices_out;
   info->gs.input_prim = nir->info.gs.input_primitive;
   info->gs.output_prim = nir->info.gs.output_primitive;
   info->gs.invocations = nir->info.gs.invocations;

   if (!info->inputs_linked)
      info->gs.num_linked_inputs = util_last_bit64(radv_gather_unlinked_io_mask(nir->info.inputs_read));

   if (info->is_ngg)
      gather_shader_info_ngg_query(device, info);
   else
      info->legacy_gs_info.esgs_itemsize = radv_compute_esgs_itemsize(device, info->gs.num_linked_inputs);
}

static void
gather_shader_info_mesh(struct radv_device *device, const nir_shader *nir,
                        const struct radv_shader_stage_key *stage_key, struct radv_shader_info *info)
{
   struct gfx10_ngg_info *ngg_info = &info->ngg_info;

   info->ms.output_prim = nir->info.mesh.primitive_type;

   /* Special case for mesh shader workgroups.
    *
    * Mesh shaders don't have any real vertex input, but they can produce
    * an arbitrary number of vertices and primitives (up to 256).
    * We need to precisely control the number of mesh shader workgroups
    * that are launched from draw calls.
    *
    * To achieve that, we set:
    * - input primitive topology to point list
    * - input vertex and primitive count to 1
    * - max output vertex count and primitive amplification factor
    *   to the boundaries of the shader
    *
    * With that, in the draw call:
    * - drawing 1 input vertex ~ launching 1 mesh shader workgroup
    *
    * In the shader:
    * - input vertex id ~ workgroup id (in 1D - shader needs to calculate in 3D)
    *
    * Notes:
    * - without GS_EN=1 PRIM_AMP_FACTOR and MAX_VERTS_PER_SUBGROUP don't seem to work
    * - with GS_EN=1 we must also set VGT_GS_MAX_VERT_OUT (otherwise the GPU hangs)
    * - with GS_FAST_LAUNCH=1 every lane's VGPRs are initialized to the same input vertex index
    *
    */
   ngg_info->esgs_ring_size = 1;
   ngg_info->hw_max_esverts = 1;
   ngg_info->max_gsprims = 1;
   ngg_info->max_out_verts = nir->info.mesh.max_vertices_out;
   ngg_info->max_vert_out_per_gs_instance = false;
   ngg_info->ngg_emit_size = 0;
   ngg_info->prim_amp_factor = nir->info.mesh.max_primitives_out;
   ngg_info->vgt_esgs_ring_itemsize = 1;

   info->ms.has_query = device->cache_key.mesh_shader_queries;
   info->ms.has_task = stage_key->has_task_shader;
}

static void
calc_mesh_workgroup_size(const struct radv_device *device, const nir_shader *nir, struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   unsigned api_workgroup_size = ac_compute_cs_workgroup_size(nir->info.workgroup_size, false, UINT32_MAX);

   if (pdev->info.mesh_fast_launch_2) {
      /* Use multi-row export. It is also necessary to use the API workgroup size for non-emulated queries. */
      info->workgroup_size = api_workgroup_size;
   } else {
      struct gfx10_ngg_info *ngg_info = &info->ngg_info;
      unsigned min_ngg_workgroup_size = ac_compute_ngg_workgroup_size(
         ngg_info->hw_max_esverts, ngg_info->max_gsprims, ngg_info->max_out_verts, ngg_info->prim_amp_factor);

      info->workgroup_size = MAX2(min_ngg_workgroup_size, api_workgroup_size);
   }
}

static void
gather_shader_info_fs(const struct radv_device *device, const nir_shader *nir,
                      const struct radv_graphics_state_key *gfx_state, struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   info->ps.num_inputs = util_bitcount64(nir->info.inputs_read);
   info->ps.can_discard = nir->info.fs.uses_discard;
   info->ps.early_fragment_test =
      nir->info.fs.early_fragment_tests ||
      (nir->info.fs.early_and_late_fragment_tests && nir->info.fs.depth_layout == FRAG_DEPTH_LAYOUT_NONE &&
       nir->info.fs.stencil_front_layout == FRAG_STENCIL_LAYOUT_NONE &&
       nir->info.fs.stencil_back_layout == FRAG_STENCIL_LAYOUT_NONE);
   info->ps.post_depth_coverage = nir->info.fs.post_depth_coverage;
   info->ps.depth_layout = nir->info.fs.depth_layout;
   info->ps.uses_sample_shading = nir->info.fs.uses_sample_shading;
   info->ps.uses_fbfetch_output = nir->info.fs.uses_fbfetch_output;
   info->ps.writes_memory = nir->info.writes_memory;
   info->ps.has_pcoord = nir->info.inputs_read & VARYING_BIT_PNTC;
   info->ps.prim_id_input = nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID;
   info->ps.reads_layer = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_LAYER_ID);
   info->ps.viewport_index_input = nir->info.inputs_read & VARYING_BIT_VIEWPORT;
   info->ps.writes_z = nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH);
   info->ps.writes_stencil = nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL);
   info->ps.writes_sample_mask = nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK);
   info->ps.reads_sample_mask_in = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN);
   info->ps.reads_sample_id = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_ID);
   info->ps.reads_frag_shading_rate = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRAG_SHADING_RATE);
   info->ps.reads_front_face = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRONT_FACE) |
                               BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRONT_FACE_FSIGN);
   info->ps.reads_barycentric_model = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_PULL_MODEL);
   info->ps.reads_fully_covered = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FULLY_COVERED);

   bool uses_persp_or_linear_interp = info->ps.reads_persp_center || info->ps.reads_persp_centroid ||
                                      info->ps.reads_persp_sample || info->ps.reads_linear_center ||
                                      info->ps.reads_linear_centroid || info->ps.reads_linear_sample;

   info->ps.allow_flat_shading =
      !(uses_persp_or_linear_interp || info->ps.needs_sample_positions || info->ps.reads_frag_shading_rate ||
        info->ps.writes_memory || nir->info.fs.needs_coarse_quad_helper_invocations ||
        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FRAG_COORD) ||
        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PIXEL_COORD) ||
        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_POINT_COORD) ||
        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_ID) ||
        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_POS) ||
        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN) ||
        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_HELPER_INVOCATION));

   info->ps.pops_is_per_sample =
      info->ps.pops && (nir->info.fs.sample_interlock_ordered || nir->info.fs.sample_interlock_unordered);

   info->ps.spi_ps_input_ena = radv_compute_spi_ps_input(pdev, gfx_state, info);
   info->ps.spi_ps_input_addr = info->ps.spi_ps_input_ena;
   if (pdev->info.gfx_level >= GFX12) {
      /* Only SPI_PS_INPUT_ENA has this bit on GFX12. */
      info->ps.spi_ps_input_addr &= C_02865C_COVERAGE_TO_SHADER_SELECT;
   }

   info->ps.has_epilog = gfx_state->ps.has_epilog && info->ps.colors_written;

   const bool export_alpha = !!(info->ps.color0_written & 0x8);

   if (info->ps.has_epilog) {
      info->ps.exports_mrtz_via_epilog = gfx_state->ps.exports_mrtz_via_epilog && export_alpha;
   } else {
      info->ps.mrt0_is_dual_src = gfx_state->ps.epilog.mrt0_is_dual_src;
      info->ps.spi_shader_col_format = gfx_state->ps.epilog.spi_shader_col_format;

      /* Clear color attachments that aren't exported by the FS to match IO shader arguments. */
      if (!info->ps.mrt0_is_dual_src)
         info->ps.spi_shader_col_format &= info->ps.colors_written;

      info->ps.cb_shader_mask = ac_get_cb_shader_mask(info->ps.spi_shader_col_format);
   }

   if (!info->ps.exports_mrtz_via_epilog) {
      info->ps.writes_mrt0_alpha = gfx_state->ms.alpha_to_coverage_via_mrtz && export_alpha;
   }

   /* Disable VRS and use the rates from PS_ITER_SAMPLES if:
    *
    * - The fragment shader reads gl_SampleMaskIn because the 16-bit sample coverage mask isn't enough for MSAA8x and
    *   2x2 coarse shading.
    * - On GFX10.3, if the fragment shader requests a fragment interlock execution mode even if the ordered section was
    *   optimized out, to consistently implement fragmentShadingRateWithFragmentShaderInterlock = VK_FALSE.
    */
   info->ps.force_sample_iter_shading_rate =
      (info->ps.reads_sample_mask_in && !info->ps.needs_poly_line_smooth) ||
      (pdev->info.gfx_level == GFX10_3 &&
       (nir->info.fs.sample_interlock_ordered || nir->info.fs.sample_interlock_unordered ||
        nir->info.fs.pixel_interlock_ordered || nir->info.fs.pixel_interlock_unordered));
}

static void
gather_shader_info_rt(const nir_shader *nir, struct radv_shader_info *info)
{
   // TODO: inline push_constants again
   info->loads_dynamic_offsets = true;
   info->loads_push_constants = true;
   info->can_inline_all_push_constants = false;
   info->inline_push_constant_mask = 0;
   info->desc_set_used_mask = -1u;
}

static void
gather_shader_info_cs(struct radv_device *device, const nir_shader *nir, const struct radv_shader_stage_key *stage_key,
                      struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->info.has_cs_regalloc_hang_bug) {
      info->cs.regalloc_hang_bug = info->cs.block_size[0] * info->cs.block_size[1] * info->cs.block_size[2] > 256;
   }
}

static void
gather_shader_info_task(struct radv_device *device, const nir_shader *nir,
                        const struct radv_shader_stage_key *stage_key, struct radv_shader_info *info)
{
   gather_shader_info_cs(device, nir, stage_key, info);

   /* Task shaders always need these for the I/O lowering even if the API shader doesn't actually
    * use them.
    */

   /* Needed to address the task draw/payload rings. */
   info->cs.uses_block_id[0] = true;
   info->cs.uses_block_id[1] = true;
   info->cs.uses_block_id[2] = true;
   info->cs.uses_grid_size = true;

   /* Needed for storing draw ready only on the 1st thread. */
   info->cs.uses_local_invocation_idx = true;

   /* Task->Mesh dispatch is linear when Y = Z = 1.
    * GFX11 CP can optimize this case with a field in its draw packets.
    */
   info->cs.linear_taskmesh_dispatch =
      nir->info.mesh.ts_mesh_dispatch_dimensions[1] == 1 && nir->info.mesh.ts_mesh_dispatch_dimensions[2] == 1;

   info->cs.has_query = device->cache_key.mesh_shader_queries;
}

static uint32_t
radv_get_user_data_0(const struct radv_device *device, struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum amd_gfx_level gfx_level = pdev->info.gfx_level;

   switch (info->stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_MESH:
      if (info->next_stage == MESA_SHADER_TESS_CTRL) {
         assert(info->stage == MESA_SHADER_VERTEX);

         if (gfx_level >= GFX10) {
            return R_00B430_SPI_SHADER_USER_DATA_HS_0;
         } else if (gfx_level == GFX9) {
            return R_00B430_SPI_SHADER_USER_DATA_LS_0;
         } else {
            return R_00B530_SPI_SHADER_USER_DATA_LS_0;
         }
      }

      if (info->next_stage == MESA_SHADER_GEOMETRY) {
         assert(info->stage == MESA_SHADER_VERTEX || info->stage == MESA_SHADER_TESS_EVAL);

         if (gfx_level >= GFX10) {
            return R_00B230_SPI_SHADER_USER_DATA_GS_0;
         } else {
            return R_00B330_SPI_SHADER_USER_DATA_ES_0;
         }
      }

      if (info->is_ngg)
         return R_00B230_SPI_SHADER_USER_DATA_GS_0;

      assert(info->stage != MESA_SHADER_MESH);
      return R_00B130_SPI_SHADER_USER_DATA_VS_0;
   case MESA_SHADER_TESS_CTRL:
      return gfx_level == GFX9 ? R_00B430_SPI_SHADER_USER_DATA_LS_0 : R_00B430_SPI_SHADER_USER_DATA_HS_0;
   case MESA_SHADER_GEOMETRY:
      return gfx_level == GFX9 ? R_00B330_SPI_SHADER_USER_DATA_ES_0 : R_00B230_SPI_SHADER_USER_DATA_GS_0;
   case MESA_SHADER_FRAGMENT:
      return R_00B030_SPI_SHADER_USER_DATA_PS_0;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
   case MESA_SHADER_RAYGEN:
   case MESA_SHADER_CALLABLE:
   case MESA_SHADER_CLOSEST_HIT:
   case MESA_SHADER_MISS:
   case MESA_SHADER_INTERSECTION:
   case MESA_SHADER_ANY_HIT:
      return R_00B900_COMPUTE_USER_DATA_0;
   default:
      UNREACHABLE("invalid shader stage");
   }
}

static bool
radv_is_merged_shader_compiled_separately(const struct radv_device *device, const struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum amd_gfx_level gfx_level = pdev->info.gfx_level;

   if (gfx_level >= GFX9) {
      switch (info->stage) {
      case MESA_SHADER_VERTEX:
         if (info->next_stage == MESA_SHADER_TESS_CTRL || info->next_stage == MESA_SHADER_GEOMETRY)
            return !info->outputs_linked;
         break;
      case MESA_SHADER_TESS_EVAL:
         if (info->next_stage == MESA_SHADER_GEOMETRY)
            return !info->outputs_linked;
         break;
      case MESA_SHADER_TESS_CTRL:
      case MESA_SHADER_GEOMETRY:
         return !info->inputs_linked;
      default:
         break;
      }
   }

   return false;
}

void
radv_nir_shader_info_init(mesa_shader_stage stage, mesa_shader_stage next_stage, struct radv_shader_info *info)
{
   memset(info, 0, sizeof(*info));

   /* Assume that shaders can inline all push constants by default. */
   info->can_inline_all_push_constants = true;

   info->stage = stage;
   info->next_stage = next_stage;
}

void
radv_nir_shader_info_pass(struct radv_device *device, const struct nir_shader *nir,
                          const struct radv_shader_layout *layout, const struct radv_shader_stage_key *stage_key,
                          const struct radv_graphics_state_key *gfx_state, const enum radv_pipeline_type pipeline_type,
                          bool consider_force_vrs, struct radv_shader_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct nir_function *func = (struct nir_function *)exec_list_get_head_const(&nir->functions);

   if (layout->use_dynamic_descriptors)
      info->loads_dynamic_offsets = true;

   nir_foreach_block (block, func->impl) {
      gather_info_block(nir, block, info, gfx_state, stage_key, consider_force_vrs);
   }

   if (nir->info.stage == MESA_SHADER_VERTEX || nir->info.stage == MESA_SHADER_TESS_EVAL ||
       nir->info.stage == MESA_SHADER_GEOMETRY)
      gather_xfb_info(nir, info);

   if (nir->info.stage == MESA_SHADER_VERTEX || nir->info.stage == MESA_SHADER_TESS_EVAL ||
       nir->info.stage == MESA_SHADER_GEOMETRY || nir->info.stage == MESA_SHADER_MESH) {
      struct radv_vs_output_info *outinfo = &info->outinfo;
      uint64_t per_vtx_mask, per_prim_mask;

      radv_get_output_masks(nir, gfx_state, &per_vtx_mask, &per_prim_mask);

      /* Mesh multiview is only lowered in ac_nir_lower_ngg, so we have to fake it here. */
      if (nir->info.stage == MESA_SHADER_MESH && gfx_state->has_multiview_view_index)
         info->uses_view_index = true;

      /* Per vertex outputs. */
      outinfo->writes_pointsize = per_vtx_mask & VARYING_BIT_PSIZ;
      outinfo->writes_viewport_index = per_vtx_mask & VARYING_BIT_VIEWPORT;
      outinfo->writes_layer = per_vtx_mask & VARYING_BIT_LAYER;
      outinfo->writes_primitive_shading_rate =
         (per_vtx_mask & VARYING_BIT_PRIMITIVE_SHADING_RATE) || info->force_vrs_per_vertex;

      /* Per primitive outputs. */
      outinfo->writes_viewport_index_per_primitive = per_prim_mask & VARYING_BIT_VIEWPORT;
      outinfo->writes_layer_per_primitive = per_prim_mask & VARYING_BIT_LAYER;
      outinfo->writes_primitive_shading_rate_per_primitive = per_prim_mask & VARYING_BIT_PRIMITIVE_SHADING_RATE;
      outinfo->export_prim_id_per_primitive = per_prim_mask & VARYING_BIT_PRIMITIVE_ID;
   }

   info->vs.needs_draw_id |= BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);
   info->vs.needs_base_instance |= BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE);
   info->vs.needs_instance_id |= BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
   info->uses_view_index |= BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_VIEW_INDEX);
   info->uses_invocation_id |= BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INVOCATION_ID);
   info->uses_prim_id |= BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

   /* Used by compute and mesh shaders. Mesh shaders must always declare this before GFX11. */
   info->cs.uses_grid_size = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_NUM_WORKGROUPS) ||
                             (nir->info.stage == MESA_SHADER_MESH && pdev->info.gfx_level < GFX11);
   info->cs.uses_local_invocation_idx = BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_LOCAL_INVOCATION_INDEX) |
                                        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_SUBGROUP_ID) |
                                        BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_NUM_SUBGROUPS) |
                                        radv_shader_should_clear_lds(device, nir);
   info->cs.derivative_group = nir->info.derivative_group;

   if (nir->info.stage == MESA_SHADER_COMPUTE || nir->info.stage == MESA_SHADER_TASK ||
       nir->info.stage == MESA_SHADER_MESH) {
      for (int i = 0; i < 3; ++i)
         info->cs.block_size[i] = nir->info.workgroup_size[i];
   }

   info->user_data_0 = radv_get_user_data_0(device, info);
   info->merged_shader_compiled_separately = radv_is_merged_shader_compiled_separately(device, info);
   info->force_indirect_descriptors = info->merged_shader_compiled_separately || stage_key->indirect_bindable;

   switch (nir->info.stage) {
   case MESA_SHADER_COMPUTE:
      gather_shader_info_cs(device, nir, stage_key, info);
      break;
   case MESA_SHADER_TASK:
      gather_shader_info_task(device, nir, stage_key, info);
      break;
   case MESA_SHADER_FRAGMENT:
      gather_shader_info_fs(device, nir, gfx_state, info);
      break;
   case MESA_SHADER_GEOMETRY:
      gather_shader_info_gs(device, nir, info);
      break;
   case MESA_SHADER_TESS_EVAL:
      gather_shader_info_tes(device, nir, info);
      break;
   case MESA_SHADER_TESS_CTRL:
      gather_shader_info_tcs(device, nir, gfx_state, info);
      break;
   case MESA_SHADER_VERTEX:
      gather_shader_info_vs(device, nir, gfx_state, stage_key, info);
      break;
   case MESA_SHADER_MESH:
      gather_shader_info_mesh(device, nir, stage_key, info);
      break;
   default:
      if (mesa_shader_stage_is_rt(nir->info.stage))
         gather_shader_info_rt(nir, info);
      break;
   }

   info->wave_size = nir->info.min_subgroup_size;
   assert(info->wave_size == nir->info.max_subgroup_size);
   assert(info->wave_size == 32 || info->wave_size == 64);
   assert(pdev->info.gfx_level >= GFX10 || info->wave_size == 64);
   assert(nir->info.stage != MESA_SHADER_GEOMETRY || info->is_ngg || info->wave_size == 64);

   switch (nir->info.stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
      info->workgroup_size = ac_compute_cs_workgroup_size(nir->info.workgroup_size, false, UINT32_MAX);

      /* Allow the compiler to assume that the shader always has full subgroups,
       * meaning that the initial EXEC mask is -1 in all waves (all lanes enabled).
       * This assumption is incorrect for ray tracing and internal (meta) shaders
       * because they can use unaligned dispatch.
       */
      info->cs.uses_full_subgroups = pipeline_type != RADV_PIPELINE_RAY_TRACING && !nir->info.internal &&
                                     (info->workgroup_size % info->wave_size) == 0;
      break;
   case MESA_SHADER_VERTEX:
      if (info->vs.as_ls || info->vs.as_es || info->is_ngg) {
         /* Set the maximum possible value by default, this will be optimized during linking if
          * possible.
          */
         info->workgroup_size = 256;
      } else {
         info->workgroup_size = info->wave_size;
      }
      break;
   case MESA_SHADER_TESS_CTRL:
      if (gfx_state->ts.patch_control_points) {
         info->workgroup_size =
            ac_compute_lshs_workgroup_size(pdev->info.gfx_level, MESA_SHADER_TESS_CTRL, info->num_tess_patches,
                                           gfx_state->ts.patch_control_points, info->tcs.tcs_vertices_out);
      } else {
         /* Set the maximum possible value when the workgroup size can't be determined. */
         info->workgroup_size = 256;
      }
      break;
   case MESA_SHADER_TESS_EVAL:
      if (info->tes.as_es || info->is_ngg) {
         /* Set the maximum possible value by default, this will be optimized during linking if
          * possible.
          */
         info->workgroup_size = 256;
      } else {
         info->workgroup_size = info->wave_size;
      }
      break;
   case MESA_SHADER_GEOMETRY:
      if (!info->is_ngg) {
         /* ESGS may operate in workgroups if on-chip GS (LDS rings) are enabled.
          *
          * GFX6: Not possible in the HW.
          * GFX7-8 (unmerged): possible in the HW, but not implemented in Mesa.
          * GFX9+ (merged): implemented in Mesa.
          *
          * Set the maximum possible value by default, this will be optimized during linking if
          * possible.
          */
         if (pdev->info.gfx_level <= GFX8)
            info->workgroup_size = info->wave_size;
         else
            info->workgroup_size = 256;

      } else {
         /* Set the maximum possible value by default, this will be optimized during linking if
          * possible.
          */
         info->workgroup_size = 256;
      }
      break;
   case MESA_SHADER_MESH:
      calc_mesh_workgroup_size(device, nir, info);
      break;
   default:
      /* FS always operates without workgroups. Other stages are computed during linking but assume
       * no workgroups by default.
       */
      info->workgroup_size = info->wave_size;
      break;
   }
}

static void
clamp_gsprims_to_esverts(unsigned *max_gsprims, unsigned max_esverts, unsigned min_verts_per_prim, bool use_adjacency)
{
   unsigned max_reuse = max_esverts - min_verts_per_prim;
   if (use_adjacency)
      max_reuse /= 2;
   *max_gsprims = MIN2(*max_gsprims, 1 + max_reuse);
}

static unsigned
radv_get_num_input_vertices(const struct radv_shader_info *es_info, const struct radv_shader_info *gs_info)
{
   if (gs_info) {
      return gs_info->gs.vertices_in;
   }

   if (es_info->stage == MESA_SHADER_TESS_EVAL) {
      if (es_info->tes.point_mode)
         return 1;
      if (es_info->tes._primitive_mode == TESS_PRIMITIVE_ISOLINES)
         return 2;
      return 3;
   }

   return 3;
}

static unsigned
radv_get_pre_rast_input_topology(const struct radv_shader_info *es_info, const struct radv_shader_info *gs_info)
{
   if (gs_info) {
      return gs_info->gs.input_prim;
   }

   if (es_info->stage == MESA_SHADER_TESS_EVAL) {
      if (es_info->tes.point_mode)
         return MESA_PRIM_POINTS;
      if (es_info->tes._primitive_mode == TESS_PRIMITIVE_ISOLINES)
         return MESA_PRIM_LINES;
      return MESA_PRIM_TRIANGLES;
   }

   return MESA_PRIM_TRIANGLES;
}

static unsigned
gfx10_get_ngg_vert_prim_lds_size(const struct radv_device *device, const struct radv_shader_info *es_info,
                                 const struct radv_shader_info *gs_info, const struct gfx10_ngg_info *ngg_info)
{
   if (gs_info) {
      const unsigned esgs_ring_lds_bytes = ngg_info->esgs_ring_size;
      const unsigned gs_total_out_vtx_bytes = ngg_info->ngg_emit_size * 4u;

      return esgs_ring_lds_bytes + gs_total_out_vtx_bytes;
   } else {
      assert(ngg_info->hw_max_esverts <= 256);
      unsigned total_es_lds_bytes = es_info->ngg_lds_vertex_size * ngg_info->hw_max_esverts;

      return total_es_lds_bytes;
   }
}

void
gfx10_get_ngg_info(const struct radv_device *device, struct radv_shader_info *es_info, struct radv_shader_info *gs_info,
                   struct gfx10_ngg_info *out)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum amd_gfx_level gfx_level = pdev->info.gfx_level;
   const struct radv_shader_info *stage_info = gs_info ? gs_info : es_info;
   const unsigned gs_num_invocations = gs_info ? MAX2(gs_info->gs.invocations, 1) : 1;
   const unsigned input_prim = radv_get_pre_rast_input_topology(es_info, gs_info);
   const unsigned gs_vertices_out = gs_info ? gs_info->gs.vertices_out : 0;
   ac_ngg_subgroup_info info;

   ac_ngg_compute_subgroup_info(gfx_level, es_info->stage, !!gs_info, input_prim, gs_vertices_out, gs_num_invocations,
                                128, stage_info->wave_size, es_info->esgs_itemsize, stage_info->ngg_lds_vertex_size,
                                stage_info->ngg_lds_scratch_size, false, 0, &info);

   out->hw_max_esverts = info.hw_max_esverts;
   out->max_gsprims = info.max_gsprims;
   out->max_out_verts = info.max_out_verts;
   out->max_vert_out_per_gs_instance = info.max_vert_out_per_gs_instance;
   out->ngg_emit_size = info.ngg_out_lds_size;
   out->esgs_ring_size = info.esgs_lds_size * 4;
   out->prim_amp_factor = gs_info ? gs_info->gs.vertices_out : 1;

   const struct radv_shader_info *rinfo = gs_info ? gs_info : es_info;
   out->lds_size = rinfo->ngg_lds_scratch_size + gfx10_get_ngg_vert_prim_lds_size(device, es_info, gs_info, out);

   unsigned workgroup_size = ac_compute_ngg_workgroup_size(info.hw_max_esverts, info.max_gsprims * gs_num_invocations,
                                                           info.max_out_verts, out->prim_amp_factor);
   if (gs_info) {
      gs_info->workgroup_size = workgroup_size;
   }
   es_info->workgroup_size = workgroup_size;
}

void
gfx10_ngg_set_esgs_ring_itemsize(const struct radv_device *device, struct radv_shader_info *es_info,
                                 struct radv_shader_info *gs_info, struct gfx10_ngg_info *out)
{
   if (gs_info) {
      out->vgt_esgs_ring_itemsize = es_info->esgs_itemsize / 4;
   } else {
      out->vgt_esgs_ring_itemsize = 1;
   }
}

static void
radv_determine_ngg_settings(struct radv_device *device, struct radv_shader_stage *ngg_stage,
                            struct radv_shader_stage *fs_stage, const struct radv_graphics_state_key *gfx_state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint64_t ps_inputs_read;

   assert(ngg_stage->info.is_ngg);
   assert(ngg_stage->stage != MESA_SHADER_TESS_EVAL || !ngg_stage->info.tes.as_es);
   assert(ngg_stage->stage != MESA_SHADER_VERTEX || (!ngg_stage->info.tes.as_es && !ngg_stage->info.vs.as_ls));
   assert(!fs_stage || fs_stage->stage == MESA_SHADER_FRAGMENT);

   if (fs_stage) {
      ps_inputs_read = fs_stage->nir->info.inputs_read;
   } else {
      /* Rely on the number of VS/TES/GS outputs when the FS is unknown (for fast-link or unlinked ESO)
       * because this should be a good approximation of the number of FS inputs.
       */
      ps_inputs_read = ngg_stage->nir->info.outputs_written;

      /* Clear varyings that can't be PS inputs. */
      ps_inputs_read &= ~(VARYING_BIT_POS | VARYING_BIT_PSIZ);
   }

   unsigned num_vertices_per_prim = 0;
   if (ngg_stage->stage == MESA_SHADER_VERTEX) {
      num_vertices_per_prim = radv_get_num_vertices_per_prim(gfx_state);
   } else if (ngg_stage->stage == MESA_SHADER_TESS_EVAL) {
      num_vertices_per_prim = ngg_stage->nir->info.tess.point_mode                                   ? 1
                              : ngg_stage->nir->info.tess._primitive_mode == TESS_PRIMITIVE_ISOLINES ? 2
                                                                                                     : 3;
   } else {
      assert(ngg_stage->stage == MESA_SHADER_GEOMETRY);
      num_vertices_per_prim = mesa_vertices_per_prim(ngg_stage->nir->info.gs.output_primitive);
   }

   ngg_stage->info.has_ngg_culling =
      radv_consider_culling(pdev, ngg_stage->nir, ps_inputs_read, num_vertices_per_prim, &ngg_stage->info);

   if (ngg_stage->stage != MESA_SHADER_GEOMETRY) {
      nir_function_impl *impl = nir_shader_get_entrypoint(ngg_stage->nir);
      ngg_stage->info.has_ngg_early_prim_export = pdev->info.gfx_level < GFX11 && exec_list_is_singular(&impl->body);

      /* NGG passthrough mode should be disabled when culling and when the vertex shader
       * exports the primitive ID.
       */
      ngg_stage->info.is_ngg_passthrough =
         !ngg_stage->info.has_ngg_culling &&
         !(ngg_stage->stage == MESA_SHADER_VERTEX && ngg_stage->info.outinfo.export_prim_id);
   }
}

static void
radv_link_shaders_info(struct radv_device *device, struct radv_shader_stage *stages,
                       const struct radv_graphics_state_key *gfx_state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_shader_stage *vs_stage = stages[MESA_SHADER_VERTEX].nir ? &stages[MESA_SHADER_VERTEX] : NULL;
   struct radv_shader_stage *tcs_stage = stages[MESA_SHADER_TESS_CTRL].nir ? &stages[MESA_SHADER_TESS_CTRL] : NULL;
   struct radv_shader_stage *tes_stage = stages[MESA_SHADER_TESS_EVAL].nir ? &stages[MESA_SHADER_TESS_EVAL] : NULL;
   struct radv_shader_stage *gs_stage = stages[MESA_SHADER_GEOMETRY].nir ? &stages[MESA_SHADER_GEOMETRY] : NULL;
   struct radv_shader_stage *ms_stage = stages[MESA_SHADER_MESH].nir ? &stages[MESA_SHADER_MESH] : NULL;
   struct radv_shader_stage *fs_stage = stages[MESA_SHADER_FRAGMENT].nir ? &stages[MESA_SHADER_FRAGMENT] : NULL;
   struct radv_shader_stage *es_stage = tes_stage && tes_stage->info.tes.as_es ? tes_stage
                                        : vs_stage && vs_stage->info.vs.as_es  ? vs_stage
                                                                               : NULL;
   struct radv_shader_stage *prerast_stage = ms_stage                                  ? ms_stage
                                             : gs_stage                                ? gs_stage
                                             : tes_stage && !tes_stage->info.tes.as_es ? tes_stage
                                             : vs_stage && !vs_stage->info.vs.as_es && !vs_stage->info.vs.as_ls
                                                ? vs_stage
                                                : NULL;
   struct radv_shader_stage *ngg_stage = prerast_stage && prerast_stage->info.is_ngg ? prerast_stage : NULL;

   /* Export primitive ID and clip/cull distances if read by the FS, or export unconditionally when
    * the next stage is unknown (with graphics pipeline library).
    */
   if (prerast_stage && (prerast_stage->info.next_stage == MESA_SHADER_FRAGMENT ||
                         !(gfx_state->lib_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT))) {
      const bool ps_prim_id_in = !fs_stage || fs_stage->info.ps.prim_id_input;
      const bool ps_clip_dists_in = !fs_stage || !!fs_stage->info.ps.input_clips_culls_mask;

      radv_set_vs_output_param(device, prerast_stage->nir, gfx_state, &prerast_stage->info, ps_prim_id_in,
                               ps_clip_dists_in);
   }

   if (prerast_stage && !ms_stage) {
      /* Compute NGG info (GFX10+) or GS info. */
      if (ngg_stage) {
         /* Determine other NGG settings like culling. */
         radv_determine_ngg_settings(device, ngg_stage, fs_stage, gfx_state);

         if (es_stage) {
            gfx10_ngg_set_esgs_ring_itemsize(device, &es_stage->info, gs_stage ? &gs_stage->info : NULL,
                                             &prerast_stage->info.ngg_info);
            assert(es_stage->info.workgroup_size == 256);
         }

         assert(ngg_stage->info.workgroup_size == 256);
      } else if (es_stage && gs_stage) {
         es_stage->info.workgroup_size = gs_stage->info.workgroup_size;
      }

      if (es_stage && gs_stage) {
         es_stage->info.gs_inputs_read = gs_stage->nir->info.inputs_read;
      }
   }

   if (vs_stage && tcs_stage) {
      vs_stage->info.vs.tcs_inputs_via_lds = tcs_stage->nir->info.inputs_read;

      if (gfx_state->ts.patch_control_points) {
         vs_stage->info.workgroup_size =
            ac_compute_lshs_workgroup_size(pdev->info.gfx_level, MESA_SHADER_VERTEX, tcs_stage->info.num_tess_patches,
                                           gfx_state->ts.patch_control_points, tcs_stage->info.tcs.tcs_vertices_out);

         if (!radv_use_llvm_for_stage(pdev, MESA_SHADER_VERTEX)) {
            /* When the number of TCS input and output vertices are the same (typically 3):
             * - There is an equal amount of LS and HS invocations
             * - In case of merged LSHS shaders, the LS and HS halves of the shader always process
             *   the exact same vertex. We can use this knowledge to optimize them.
             *
             * We don't set tcs_in_out_eq if the float controls differ because that might involve
             * different float modes for the same block and our optimizer doesn't handle a
             * instruction dominating another with a different mode.
             */
            vs_stage->info.vs.tcs_in_out_eq =
               pdev->info.gfx_level >= GFX9 &&
               gfx_state->ts.patch_control_points == tcs_stage->info.tcs.tcs_vertices_out &&
               vs_stage->nir->info.float_controls_execution_mode == tcs_stage->nir->info.float_controls_execution_mode;

            if (vs_stage->info.vs.tcs_in_out_eq) {
               vs_stage->info.vs.tcs_inputs_via_temp =
                  vs_stage->nir->info.outputs_written &
                  ~(vs_stage->nir->info.outputs_read_indirectly | vs_stage->nir->info.outputs_written_indirectly) &
                  tcs_stage->nir->info.tess.tcs_same_invocation_inputs_read;
               vs_stage->info.vs.tcs_inputs_via_lds =
                  tcs_stage->nir->info.tess.tcs_cross_invocation_inputs_read |
                  (tcs_stage->nir->info.tess.tcs_same_invocation_inputs_read &
                   tcs_stage->nir->info.inputs_read_indirectly) |
                  (tcs_stage->nir->info.tess.tcs_same_invocation_inputs_read &
                   (vs_stage->nir->info.outputs_read_indirectly | vs_stage->nir->info.outputs_written_indirectly));
            }
         }
      }
   }

   /* Copy shader info between TCS<->TES. */
   if (tcs_stage && tes_stage) {
      tcs_stage->info.tcs.tes_reads_tess_factors = tes_stage->info.tes.reads_tess_factors;
      tcs_stage->info.tcs.tes_inputs_read = tes_stage->nir->info.inputs_read;
      tcs_stage->info.tcs.tes_patch_inputs_read = tes_stage->nir->info.patch_inputs_read;
      tcs_stage->info.tes._primitive_mode = tes_stage->nir->info.tess._primitive_mode;

      if (gfx_state->ts.patch_control_points)
         tes_stage->info.num_tess_patches = tcs_stage->info.num_tess_patches;
   }
}

static void
radv_nir_shader_info_merge(const struct radv_shader_stage *src, struct radv_shader_stage *dst)
{
   const struct radv_shader_info *src_info = &src->info;
   struct radv_shader_info *dst_info = &dst->info;

   assert((src->stage == MESA_SHADER_VERTEX && dst->stage == MESA_SHADER_TESS_CTRL) ||
          (src->stage == MESA_SHADER_VERTEX && dst->stage == MESA_SHADER_GEOMETRY) ||
          (src->stage == MESA_SHADER_TESS_EVAL && dst->stage == MESA_SHADER_GEOMETRY));

   dst_info->loads_push_constants |= src_info->loads_push_constants;
   dst_info->loads_dynamic_offsets |= src_info->loads_dynamic_offsets;
   dst_info->desc_set_used_mask |= src_info->desc_set_used_mask;
   dst_info->uses_view_index |= src_info->uses_view_index;
   dst_info->uses_prim_id |= src_info->uses_prim_id;
   dst_info->inline_push_constant_mask |= src_info->inline_push_constant_mask;

   /* Only inline all push constants if both allows it. */
   dst_info->can_inline_all_push_constants &= src_info->can_inline_all_push_constants;

   dst_info->push_constant_size = MAX2(dst_info->push_constant_size, src_info->push_constant_size);

   if (src->stage == MESA_SHADER_VERTEX) {
      dst_info->vs = src_info->vs;
   } else {
      dst_info->tes = src_info->tes;
   }

   if (dst->stage == MESA_SHADER_GEOMETRY)
      dst_info->gs.es_type = src->stage;
}

void
radv_nir_shader_info_link(struct radv_device *device, const struct radv_graphics_state_key *gfx_state,
                          struct radv_shader_stage *stages)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   radv_link_shaders_info(device, stages, gfx_state);

   if (pdev->info.gfx_level >= GFX9) {
      /* Merge shader info for VS+TCS. */
      if (stages[MESA_SHADER_VERTEX].nir && stages[MESA_SHADER_TESS_CTRL].nir) {
         radv_nir_shader_info_merge(&stages[MESA_SHADER_VERTEX], &stages[MESA_SHADER_TESS_CTRL]);
      }

      /* Merge shader info for VS+GS or TES+GS. */
      if ((stages[MESA_SHADER_VERTEX].nir || stages[MESA_SHADER_TESS_EVAL].nir) && stages[MESA_SHADER_GEOMETRY].nir) {
         mesa_shader_stage pre_stage = stages[MESA_SHADER_TESS_EVAL].nir ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;

         radv_nir_shader_info_merge(&stages[pre_stage], &stages[MESA_SHADER_GEOMETRY]);
      }
   }
}

enum ac_hw_stage
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
      UNREACHABLE("Unsupported HW stage");
   }
}
