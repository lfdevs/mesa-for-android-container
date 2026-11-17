/*
 * Copyright © 2025 Arm Ltd.
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2019-2026 Google LLC
 *
 * Also derived from anv_pipeline.c which is
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_cmd_buffer.h"
#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_instance.h"
#include "panvk_mempool.h"
#include "panvk_nir.h"
#include "panvk_physical_device.h"
#include "panvk_sampler.h"
#include "panvk_shader.h"
#if PAN_ARCH >= 10
#include "panvk_gpu_prerast.h"
#endif

#include "spirv/nir_spirv.h"
#include "util/memstream.h"
#include "util/mesa-blake3.h"
#include "util/shader_stats.h"
#include "util/u_dynarray.h"
#include "util/u_hexdump.h"
#include "util/u_memory.h"
#include "nir_builder.h"
#include "nir_conversion_builder.h"
#include "nir_deref.h"

#include "shader_enums.h"
#include "vk_graphics_state.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_shader_module.h"
#include "vk_ycbcr_conversion.h"

#include "compiler/bifrost/bifrost_nir.h"
#include "compiler/bifrost/bifrost_compile.h"
#include "compiler/pan_compiler.h"
#include "compiler/pan_nir.h"
#include "pan_shader.h"

#include "vk_log.h"
#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"
#include "vk_shader.h"
#include "vk_util.h"

#define FAU_WORD_COUNT 64

struct panvk_lower_sysvals_context {
   struct panvk_shader_variant *shader;
   const struct vk_graphics_pipeline_state *state;
};

static bool
panvk_lower_sysvals(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   const struct panvk_lower_sysvals_context *ctx = data;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   unsigned bit_size = intr->def.bit_size;
   nir_def *val = NULL;
   b->cursor = nir_before_instr(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_base_workgroup_id:
      val = load_sysval(b, compute, bit_size, base);
      break;
   case nir_intrinsic_load_num_workgroups:
      val = load_sysval(b, compute, bit_size, num_work_groups);
      break;
   case nir_intrinsic_load_workgroup_size:
      val = load_sysval(b, compute, bit_size, local_group_size);
      break;
   case nir_intrinsic_load_viewport_scale:
      val = load_sysval(b, graphics, bit_size, viewport.scale);
      break;
   case nir_intrinsic_load_viewport_offset:
      val = load_sysval(b, graphics, bit_size, viewport.offset);
      break;
   case nir_intrinsic_load_first_vertex:
      val = load_sysval(b, graphics, bit_size, vs.first_vertex);
      break;
   case nir_intrinsic_load_base_instance:
      val = load_sysval(b, graphics, bit_size, vs.base_instance);
      break;
   case nir_intrinsic_load_noperspective_varyings_pan:
      /* TODO: use a VS epilog specialized on constant noperspective_varyings
       * with VK_EXT_graphics_pipeline_libraries and VK_EXT_shader_object */
      assert(b->shader->info.stage == MESA_SHADER_VERTEX);
      val = load_sysval(b, graphics, bit_size, vs.noperspective_varyings);
      break;

#if PAN_ARCH < 9
   case nir_intrinsic_load_raw_vertex_offset:
      val = load_sysval(b, graphics, bit_size, vs.raw_vertex_offset);
      break;
   case nir_intrinsic_load_layer_id:
      assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      val = load_sysval(b, graphics, bit_size, layer_id);
      break;
   case nir_intrinsic_load_view_index:
      assert(b->shader->info.stage != MESA_SHADER_COMPUTE);
      if (ctx->state->mv->view_mask == 0)
         val = nir_imm_zero(b, 1, 32);
      else
         val = load_sysval(b, graphics, bit_size, layer_id);
      break;
#else
   case nir_intrinsic_load_view_index:
      if (ctx->state->mv->view_mask == 0) {
         val = nir_imm_zero(b, 1, 32);
         break;
      } else if (b->shader->info.stage == MESA_SHADER_VERTEX) {
         /* On v14+ we have real multiview and view_index comes from a preload
          * in the vertex stage.  On earlier generations, view_index in vertex
          * shaders gets lowered away by nir_lower_multiview() so we should
          * never see it here.
          */
         assert(PAN_ARCH >= 14);
         return false;
      }

      /* For fragment shaders, it's the same as layer_id */
      assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      FALLTHROUGH;

   case nir_intrinsic_load_layer_id:
      val = nir_load_frame_arg_pan(b);
      val = nir_extract_u8_imm(b, nir_u2u32(b, val), 0);
      break;
#endif

   case nir_intrinsic_load_draw_id:
      /* Multidraw is supported on v10. */
      if (PAN_ARCH >= 10)
         return false;

      /* TODO: We only implement single-draw direct and indirect draws, so this
       * is sufficient. We'll revisit this when we get around to implementing
       * multidraw. */
      assert(b->shader->info.stage == MESA_SHADER_VERTEX);
      val = nir_imm_int(b, 0);
      break;

   case nir_intrinsic_load_printf_buffer_address:
      val = load_sysval(b, common, bit_size, printf_buffer_address);
      break;

   case nir_intrinsic_load_blend_descriptor_pan: {
      uint32_t loc = nir_intrinsic_base(intr);
      val = load_sysval(b, graphics, bit_size, fs.blend_descs[loc]);
      break;
   }

   case nir_intrinsic_load_input_attachment_target_pan: {
      const struct vk_input_attachment_location_state *ial =
         ctx->state ? ctx->state->ial : NULL;

      if (ial && nir_src_is_const(intr->src[0])) {
         uint32_t index = nir_src_as_uint(intr->src[0]);
         uint32_t depth_idx = ial->depth_att == MESA_VK_ATTACHMENT_NO_INDEX
                                 ? 0
                                 : ial->depth_att + 1;
         uint32_t stencil_idx = ial->stencil_att == MESA_VK_ATTACHMENT_NO_INDEX
                                   ? 0
                                   : ial->stencil_att + 1;
         uint32_t target = ~0;

         if (depth_idx == index || stencil_idx == index) {
            target = PANVK_ZS_ATTACHMENT;
         } else {
            for (unsigned i = 0; i < ial->color_attachment_count; i++) {
               if (ial->color_map[i] == MESA_VK_ATTACHMENT_UNUSED)
                  continue;

               if (ial->color_map[i] + 1 == index) {
                  target = PANVK_COLOR_ATTACHMENT(i);
                  break;
               }
            }
         }

         val = nir_imm_int(b, target);
      } else {
         nir_def *ia_info =
            load_sysval_entry(b, graphics, bit_size, iam, intr->src[0].ssa);

         val = nir_channel(b, ia_info, 0);
      }
      break;
   }

   case nir_intrinsic_load_input_attachment_conv_pan: {
      nir_def *ia_info =
         load_sysval_entry(b, graphics, bit_size, iam, intr->src[0].ssa);

      val = nir_channel(b, ia_info, 1);
      break;
   }

   case nir_intrinsic_load_ro_sink_address_poly:
      val = nir_imm_int64(b, PAN_SHADER_OOB_ADDRESS);
      break;

   default:
      return false;
   }

   assert(val->num_components == intr->def.num_components);

   b->cursor = nir_after_instr(instr);
   nir_def_rewrite_uses(&intr->def, val);
   return true;
}

#if PAN_ARCH < 9
static bool
lower_gl_pos_layer_writes(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic != nir_intrinsic_copy_deref)
      return false;

   nir_variable *dst_var = nir_intrinsic_get_var(intr, 0);
   nir_variable *src_var = nir_intrinsic_get_var(intr, 1);

   if (!dst_var || dst_var->data.mode != nir_var_shader_out || !src_var ||
       src_var->data.mode != nir_var_shader_temp)
      return false;

   if (dst_var->data.location == VARYING_SLOT_LAYER) {
      /* We don't really write the layer, we just make sure primitives are
       * discarded if gl_Layer doesn't match the layer passed to the draw.
       */
      b->cursor = nir_instr_remove(instr);
      return true;
   }

   if (dst_var->data.location == VARYING_SLOT_POS) {
      nir_variable *temp_layer_var = data;
      nir_variable *temp_pos_var = src_var;

      b->cursor = nir_before_instr(instr);
      nir_def *layer = nir_load_var(b, temp_layer_var);
      nir_def *pos = nir_load_var(b, temp_pos_var);
      nir_def *inf_pos = nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, 1.0f);
      nir_def *ref_layer = load_sysval(b, graphics, 32, layer_id);

      nir_store_var(b, temp_pos_var,
                    nir_bcsel(b, nir_ieq(b, layer, ref_layer), pos, inf_pos),
                    0xf);
      return true;
   }

   return false;
}

static bool
lower_layer_writes(nir_shader *nir)
{
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      return false;

   nir_variable *temp_layer_var = NULL;
   bool has_layer_var = false;

   nir_foreach_variable_with_modes(var, nir,
                                   nir_var_shader_out | nir_var_shader_temp) {
      if (var->data.mode == nir_var_shader_out &&
          var->data.location == VARYING_SLOT_LAYER)
         has_layer_var = true;

      if (var->data.mode == nir_var_shader_temp &&
          var->data.location == VARYING_SLOT_LAYER)
         temp_layer_var = var;
   }

   if (!has_layer_var)
      return false;

   assert(temp_layer_var);

   return nir_shader_instructions_pass(nir, lower_gl_pos_layer_writes,
                                       nir_metadata_control_flow,
                                       temp_layer_var);
}
#endif

#if PAN_ARCH >= 10
static bool
mark_all_access_non_uniform(nir_builder *b, nir_instr *instr, void *data)
{
   switch (instr->type) {
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);

      for (unsigned i = 0; i < tex->num_srcs; i++) {
         switch (tex->src[i].src_type) {
         case nir_tex_src_texture_offset:
         case nir_tex_src_texture_handle:
            tex->texture_non_uniform = true;
            break;

         case nir_tex_src_sampler_offset:
         case nir_tex_src_sampler_handle:
            tex->sampler_non_uniform = true;
            break;

         default:
            break;
         }
      }

      return true;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_store_ssbo:
      case nir_intrinsic_ssbo_atomic:
      case nir_intrinsic_ssbo_atomic_swap:
      case nir_intrinsic_image_load:
      case nir_intrinsic_image_sparse_load:
      case nir_intrinsic_image_store:
      case nir_intrinsic_image_atomic:
      case nir_intrinsic_image_atomic_swap:
      case nir_intrinsic_image_size:
      case nir_intrinsic_image_samples:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_sparse_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
      case nir_intrinsic_image_deref_levels:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_samples:
      case nir_intrinsic_image_deref_samples_identical:
         nir_intrinsic_set_access(
            intrin, nir_intrinsic_access(intrin) | ACCESS_NON_UNIFORM);
         return true;
      default:
         return false;
      }
   }
   default:
      return false;
   }
}
#endif

static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size * (length == 3 ? 4 : length);
}

static inline nir_address_format
panvk_buffer_ubo_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return PAN_ARCH < 9 ? nir_address_format_32bit_index_offset
                           : nir_address_format_vec2_index_32bit_offset;
   default:
      UNREACHABLE("Invalid robust buffer access behavior");
   }
}

static inline nir_address_format
panvk_buffer_ssbo_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
      return PAN_ARCH < 9 ? nir_address_format_64bit_global_32bit_offset
                           : nir_address_format_vec2_index_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return PAN_ARCH < 9 ? nir_address_format_64bit_bounded_global
                           : nir_address_format_vec2_index_32bit_offset;
   default:
      UNREACHABLE("Invalid robust buffer access behavior");
   }
}

static const nir_shader_compiler_options *
panvk_get_nir_options(UNUSED struct vk_physical_device *vk_pdev,
                      mesa_shader_stage stage,
                      UNUSED const struct vk_pipeline_robustness_state *rs)
{
   return pan_get_nir_shader_compiler_options(PAN_ARCH, stage, false);
}

static struct spirv_to_nir_options
panvk_get_spirv_options(UNUSED struct vk_physical_device *vk_pdev,
                        UNUSED mesa_shader_stage stage,
                        const struct vk_pipeline_robustness_state *rs)
{
   return (struct spirv_to_nir_options){
      .ubo_addr_format = panvk_buffer_ubo_addr_format(rs->uniform_buffers),
      .ssbo_addr_format = panvk_buffer_ssbo_addr_format(rs->storage_buffers),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .shared_addr_format = nir_address_format_32bit_offset,
      .min_ubo_alignment = 16,
      .min_ssbo_alignment = PAN_ARCH >= 9 ? 4 : 16,
      .debug_info = pan_want_debug_info(PAN_ARCH),
   };
}

#define PANVK_CULL_ALL_NEGATIVE_MIN (1.0f - 1.0f / 65536.0f)

struct panvk_dist_elem {
   nir_variable *var;
   unsigned idx;
};

static unsigned
panvk_collect_dist_vars(nir_shader *nir, nir_variable_mode mode,
                        struct panvk_dist_elem clip[8],
                        struct panvk_dist_elem cull[8])
{
   const unsigned nclip = nir->info.clip_distance_array_size;
   unsigned found = 0;

   memset(clip, 0, 8 * sizeof(*clip));
   memset(cull, 0, 8 * sizeof(*cull));
   nir_foreach_variable_with_modes(var, nir, mode) {
      const gl_varying_slot loc = var->data.location;
      if (!var->data.compact)
         continue;

      bool is_clip_slot =
         loc == VARYING_SLOT_CLIP_DIST0 || loc == VARYING_SLOT_CLIP_DIST1;
      bool is_cull_slot =
         loc == VARYING_SLOT_CULL_DIST0 || loc == VARYING_SLOT_CULL_DIST1;
      if (!is_clip_slot && !is_cull_slot)
         continue;

      unsigned base = (loc - (is_clip_slot ? VARYING_SLOT_CLIP_DIST0
                                           : VARYING_SLOT_CULL_DIST0)) * 4 +
                      var->data.location_frac;
      for (unsigned i = 0; i < glsl_get_length(var->type); i++) {
         unsigned d = base + i;
         struct panvk_dist_elem e = {var, i};
         if (is_cull_slot) {
            if (d < 8)
               cull[d] = e;
         } else if (d < nclip) {
            clip[d] = e;
         } else if (d - nclip < 8) {
            cull[d - nclip] = e;
         }
      }
      found++;
   }

   return found;
}

static void
panvk_dist_vars_to_temp(nir_shader *nir, struct panvk_dist_elem clip[8],
                        struct panvk_dist_elem cull[8])
{
   for (unsigned i = 0; i < 16; i++) {
      nir_variable *var = i < 8 ? clip[i].var : cull[i - 8].var;
      if (var && var->data.mode != nir_var_shader_temp) {
         var->data.mode = nir_var_shader_temp;
         var->data.compact = false;
         var->data.location = 0;
         var->data.location_frac = 0;
      }
   }
   nir_fixup_deref_modes(nir);
}

static nir_variable *
panvk_create_dist_var(nir_shader *nir, nir_variable_mode mode,
                      gl_varying_slot loc, const char *name)
{
   nir_variable *var =
      nir_variable_create(nir, mode, glsl_vec4_type(), name);
   var->data.location = loc;
   var->data.interpolation = INTERP_MODE_SMOOTH;
   return var;
}

static bool
panvk_lower_clip_cull_vs(nir_shader *nir)
{
   struct panvk_dist_elem clip[8], cull[8];
   if (!panvk_collect_dist_vars(nir, nir_var_shader_out, clip, cull))
      return false;

   const unsigned nclip = nir->info.clip_distance_array_size;
   const unsigned ncull = nir->info.cull_distance_array_size;
   assert(nclip <= 8 && ncull <= 8);

   nir_builder b =
      nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(nir)));
   nir_def *clip_v[8], *cull_v[8];
   for (unsigned i = 0; i < 8; i++) {
      clip_v[i] = i < nclip && clip[i].var
                     ? nir_load_array_var_imm(&b, clip[i].var, clip[i].idx)
                     : nir_imm_float(&b, 1.0f);
      cull_v[i] =
         i < ncull && cull[i].var
            ? nir_b2f32(&b, nir_flt_imm(&b,
                                        nir_load_array_var_imm(
                                           &b, cull[i].var, cull[i].idx),
                                        0.0))
            : nir_imm_float(&b, 0.0f);
   }

   panvk_dist_vars_to_temp(nir, clip, cull);

   if (nclip) {
      nir_store_var(&b,
                    panvk_create_dist_var(nir, nir_var_shader_out,
                                          VARYING_SLOT_CLIP_DIST0, "clip0"),
                    nir_vec(&b, &clip_v[0], 4), 0xf);
   }
   if (nclip > 4) {
      nir_store_var(&b,
                    panvk_create_dist_var(nir, nir_var_shader_out,
                                          VARYING_SLOT_CLIP_DIST1, "clip1"),
                    nir_vec(&b, &clip_v[4], 4), 0xf);
   }
   if (ncull) {
      nir_store_var(&b,
                    panvk_create_dist_var(nir, nir_var_shader_out,
                                          VARYING_SLOT_CULL_DIST0, "cull0"),
                    nir_vec(&b, &cull_v[0], 4), 0xf);
   }
   if (ncull > 4) {
      nir_store_var(&b,
                    panvk_create_dist_var(nir, nir_var_shader_out,
                                          VARYING_SLOT_CULL_DIST1, "cull1"),
                    nir_vec(&b, &cull_v[4], 4), 0xf);
   }

   return nir_progress(true, nir_shader_get_entrypoint(nir),
                       nir_metadata_none);
}

static bool
panvk_lower_clip_cull_fs(nir_shader *nir, uint64_t prev_outputs)
{
   static const gl_varying_slot locs[4] = {
      VARYING_SLOT_CLIP_DIST0, VARYING_SLOT_CLIP_DIST1,
      VARYING_SLOT_CULL_DIST0, VARYING_SLOT_CULL_DIST1,
   };
   static const char *names[4] = {"clip0", "clip1", "cull0", "cull1"};

   if (!(prev_outputs & (VARYING_BIT_CLIP_DIST0 | VARYING_BIT_CLIP_DIST1 |
                         VARYING_BIT_CULL_DIST0 | VARYING_BIT_CULL_DIST1)))
      return false;

   struct panvk_dist_elem old_clip[8], old_cull[8];
   if (panvk_collect_dist_vars(nir, nir_var_shader_in, old_clip, old_cull))
      panvk_dist_vars_to_temp(nir, old_clip, old_cull);

   nir_builder b =
      nir_builder_at(nir_before_impl(nir_shader_get_entrypoint(nir)));
   nir_def *vals[4] = {NULL};
   nir_def *kill = nir_imm_false(&b);
   for (unsigned i = 0; i < 4; i++) {
      if (!(prev_outputs & BITFIELD64_BIT(locs[i])))
         continue;

      nir_variable *var =
         panvk_create_dist_var(nir, nir_var_shader_in, locs[i], names[i]);
      vals[i] = nir_load_var(&b, var);
      nir_def *c = i < 2
                      ? nir_flt_imm(&b, vals[i], 0.0)
                      : nir_fge_imm(&b, vals[i], PANVK_CULL_ALL_NEGATIVE_MIN);
      kill = nir_ior(&b, kill, nir_bany(&b, c));
   }

   for (unsigned i = 0; i < 8; i++) {
      if (old_clip[i].var) {
         nir_def *v = vals[i / 4] ? nir_channel(&b, vals[i / 4], i % 4)
                                  : nir_imm_float(&b, 0.0f);
         nir_store_array_var_imm(&b, old_clip[i].var, old_clip[i].idx, v, 1);
      }
      if (old_cull[i].var) {
         nir_store_array_var_imm(&b, old_cull[i].var, old_cull[i].idx,
                                 nir_imm_float(&b, 0.0f), 1);
      }
   }

   nir_discard_if(&b, kill);
   nir->info.fs.uses_discard = true;

   return nir_progress(true, nir_shader_get_entrypoint(nir),
                       nir_metadata_none);
}

static void
panvk_preprocess_nir(struct vk_physical_device *vk_pdev,
                     nir_shader *nir,
                     UNUSED const struct vk_pipeline_robustness_state *rs)
{
   struct panvk_physical_device *pdev = to_panvk_physical_device(vk_pdev);

   /* Lower mediump temporary vars before NIR has the chance to screw it up
    * and delete the mediump information.  On the old back-end, this might be
    * a performance regression because of all the extra f2f it may insert but
    * Kraid is good enough at fusing float conversions into other ALU ops that
    * it doesn't really matter.
    */
   if (pan_use_kraid(PAN_ARCH, nir->info.stage, nir->info.internal)) {
      NIR_PASS(_, nir, nir_lower_mediump_vars,
               nir_var_shader_temp | nir_var_function_temp);
   }

   /* Ensure to regroup output variables at the same location */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_opt_vectorize_io_vars, nir_var_shader_out);

   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir), nir_var_shader_out);

   if (nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, nir, panvk_lower_clip_cull_vs);

#if PAN_ARCH < 9
   /* This needs to be done just after the io_to_temporaries pass, because we
    * rely on out temporaries to collect the final layer_id value.
    */
   NIR_PASS(_, nir, lower_layer_writes);
#endif

   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_split_var_copies);

   NIR_PASS(_, nir, nir_opt_copy_prop_vars);
   NIR_PASS(_, nir, nir_opt_combine_stores, nir_var_all);
   NIR_PASS(_, nir, nir_opt_loop);

   NIR_PASS(_, nir, nir_opt_barrier_modes);
   NIR_PASS(_, nir, nir_opt_acquire_release_barriers, SCOPE_DEVICE);

   /* Do texture lowering here. We need to lower texture stuff
    * now, before we call panvk_per_arch(nir_lower_descriptors)() because some
    * of the texture lowering generates nir_texop_txs which we handle as part
    * of descriptor lowering.
    *
    * TODO: We really should be doing this in common code, not duplicated in
    * panvk. In order to do that, we need to rework the panfrost compile
    * flow to look more like the Intel flow:
    *
    *  1. Compile SPIR-V to NIR and maybe do a tiny bit of lowering that needs
    *     to be done really early.
    *
    *  2. pan_preprocess_nir: Does common lowering and runs the optimization
    *     loop.  Nothing here should be API-specific.
    *
    *  3. Do additional lowering in panvk
    *
    *  4. pan_postprocess_nir: Does final lowering and runs the optimization
    *     loop again.  This can happen as part of the final compile.
    *
    * This would give us a better place to do panvk-specific lowering.
    */
   NIR_PASS(_, nir, nir_lower_system_values);

   nir_lower_compute_system_values_options options = {
      .has_base_workgroup_id = true,
      .shuffle_local_ids_for_quad_derivatives = true,
   };

   NIR_PASS(_, nir, nir_lower_compute_system_values, &options);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_lower_wpos_center);

   assert(pdev->kmod.dev->props.shader_present != 0);
   uint64_t core_max_id =
      util_last_bit(pdev->kmod.dev->props.shader_present) - 1;
   NIR_PASS(_, nir, nir_inline_sysval, nir_intrinsic_load_core_max_id_arm,
            core_max_id);

   pan_preprocess_nir(nir, PAN_ARCH);
}

#if PAN_ARCH >= 10
static bool
panvk_lower_gpu_prerast_sysval(nir_builder *b, nir_intrinsic_instr *intr,
                               UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_vertex_param_buffer_poly)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *addr = load_sysval(b, graphics, 64, gpu_prerast_params);
   nir_def_replace(&intr->def, addr);
   return true;
}

static nir_def *
poly_nir_load_raw_vertex_id(nir_builder *b, nir_def *id)
{
   nir_def *p = nir_load_vertex_param_buffer_poly(b);
   nir_def *index_size = nir_load_global(
      b, 1, 32,
      nir_iadd_imm(b, p, offsetof(struct poly_vertex_params, index_size_B)),
      .align_mul = 4);
   nir_def *pulled;
   nir_if *have_index = nir_push_if(b, nir_ine_imm(b, index_size, 0));
   {
      nir_def *index_buffer = nir_load_global(b, 1, 64, p, .align_mul = 8);
      nir_def *range = nir_load_global(
         b, 1, 32,
         nir_iadd_imm(b, p,
                      offsetof(struct poly_vertex_params,
                               index_buffer_range_el)),
         .align_mul = 4);
      nir_def *oob = nir_uge(b, id, range);
      nir_def *el_id = nir_bcsel(b, oob, nir_imm_int(b, 0), id);
      nir_def *addr = nir_iadd(
         b, index_buffer, nir_u2u64(b, nir_imul(b, el_id, index_size)));
      nir_def *el8;
      nir_def *el_rest;
      nir_if *is8 = nir_push_if(b, nir_ieq_imm(b, index_size, 1));
      {
         el8 = nir_u2u32(b, nir_load_global(b, 1, 8, addr, .align_mul = 1));
      }
      nir_push_else(b, is8);
      {
         nir_def *el16;
         nir_def *el32;
         nir_if *is16 = nir_push_if(b, nir_ieq_imm(b, index_size, 2));
         {
            el16 =
               nir_u2u32(b, nir_load_global(b, 1, 16, addr, .align_mul = 2));
         }
         nir_push_else(b, is16);
         {
            el32 = nir_load_global(b, 1, 32, addr, .align_mul = 4);
         }
         nir_pop_if(b, is16);
         el_rest = nir_if_phi(b, el16, el32);
      }
      nir_pop_if(b, is8);
      nir_def *el = nir_if_phi(b, el8, el_rest);
      pulled = nir_bcsel(b, oob, nir_imm_int(b, 0), el);
   }
   nir_pop_if(b, have_index);
   return nir_if_phi(b, pulled, id);
}

static nir_def *
poly_nir_load_vertex_id(nir_builder *b, nir_def *id)
{
   return nir_iadd(b, poly_nir_load_raw_vertex_id(b, id),
                   nir_load_first_vertex(b));
}

static bool
panvk_gpu_prerast_lower_sysval_intr(nir_builder *b, nir_intrinsic_instr *intr,
                                    UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_index_size_poly &&
       intr->intrinsic != nir_intrinsic_load_vs_outputs_poly)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *p = nir_load_vertex_param_buffer_poly(b);
   unsigned off = intr->intrinsic == nir_intrinsic_load_index_size_poly
                     ? offsetof(struct poly_vertex_params, index_size_B)
                     : offsetof(struct poly_vertex_params, outputs);
   unsigned bits = intr->intrinsic == nir_intrinsic_load_index_size_poly ? 32
                                                                         : 64;
   nir_def *v = nir_load_global(b, 1, bits, nir_iadd_imm(b, p, off),
                                .align_mul = bits / 8);
   nir_def_replace(&intr->def, v);
   return true;
}

static bool
poly_nir_lower_sysvals(nir_shader *s)
{
   return nir_shader_intrinsics_pass(s, panvk_gpu_prerast_lower_sysval_intr,
                                     nir_metadata_control_flow, NULL);
}

static bool
panvk_gpu_prerast_lower_sw_vs_intr(nir_builder *b, nir_intrinsic_instr *intr,
                                   UNUSED void *data)
{
   b->cursor = nir_before_instr(&intr->instr);
   if (intr->intrinsic == nir_intrinsic_load_vertex_id) {
      nir_def *id = nir_channel(b, nir_load_global_invocation_id(b, 32), 0);
      nir_def *first_index = nir_load_global(
         b, 1, 32,
         nir_iadd_imm(b, nir_load_vertex_param_buffer_poly(b),
                      offsetof(struct panvk_gpu_prerast_params, index_offset)),
         .align_mul = 4);
      nir_def_replace(&intr->def,
                      poly_nir_load_vertex_id(b, nir_iadd(b, id, first_index)));
      return true;
   }
   if (intr->intrinsic == nir_intrinsic_load_vertex_id_zero_base) {
      nir_def_replace(
         &intr->def, nir_channel(b, nir_load_global_invocation_id(b, 32), 0));
      return true;
   }
   if (intr->intrinsic == nir_intrinsic_load_instance_id) {
      nir_def_replace(
         &intr->def, nir_channel(b, nir_load_global_invocation_id(b, 32), 1));
      return true;
   }
   return false;
}

static bool
poly_nir_lower_sw_vs(nir_shader *s)
{
   return nir_shader_intrinsics_pass(s, panvk_gpu_prerast_lower_sw_vs_intr,
                                     nir_metadata_control_flow, NULL);
}

static bool
panvk_gpu_prerast_lower_vs_before_gs_intr(nir_builder *b,
                                          nir_intrinsic_instr *intr,
                                          UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   b->cursor = nir_instr_remove(&intr->instr);
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   uint64_t mask = b->shader->info.outputs_written;
   unsigned location = sem.location;
   if (nir_src_is_const(intr->src[1]))
      location += nir_src_as_uint(intr->src[1]);
   unsigned slot =
      util_bitcount64(mask & (((uint64_t)1 << location) - 1));
   unsigned nout = util_bitcount64(mask);
   nir_def *p = nir_load_vertex_param_buffer_poly(b);
   nir_def *nr_verts = nir_load_global(
      b, 1, 32,
      nir_iadd_imm(b, p, offsetof(struct poly_vertex_params, verts_per_instance)),
      .align_mul = 4);
   nir_def *out_buf = nir_load_global(
      b, 1, 64,
      nir_iadd_imm(b, p, offsetof(struct poly_vertex_params, output_buffer)),
      .align_mul = 8);
   nir_def *linear =
      nir_iadd(b, nir_imul(b, nir_load_instance_id(b), nr_verts),
               nir_load_vertex_id_zero_base(b));
   nir_def *addr = nir_iadd(
      b, out_buf,
      nir_u2u64(b, nir_iadd_imm(b, nir_imul_imm(b, linear, nout * 16),
                                slot * 16 + nir_intrinsic_component(intr) * 4)));
   /* Records are 32-bit per component; widen mediump values. */
   nir_def *value = intr->src[0].ssa;
   if (value->bit_size != 32) {
      nir_alu_type base =
         nir_alu_type_get_base_type(nir_intrinsic_src_type(intr));
      value = base == nir_type_float ? nir_f2f32(b, value)
              : base == nir_type_int ? nir_i2i32(b, value)
                                     : nir_u2u32(b, value);
   }
   nir_def *cap = nir_load_global(
      b, 1, 32,
      nir_iadd_imm(b, p, offsetof(struct panvk_gpu_prerast_params,
                                  record_capacity)),
      .align_mul = 4);
   nir_push_if(b, nir_ult(b, linear, cap));
   nir_store_global(b, value, addr,
                    .write_mask = nir_intrinsic_write_mask(intr),
                    .align_mul = 4);
   nir_pop_if(b, NULL);
   return true;
}

static bool
panvk_gpu_prerast_lower_vs_outputs(nir_shader *vs)
{
   return nir_shader_intrinsics_pass(vs, panvk_gpu_prerast_lower_vs_before_gs_intr,
                                     nir_metadata_control_flow, NULL);
}

/* Output slot types seen by the lowered VS, so the passthrough stores each
 * record slot with the same NIR type the original VS used.
 */
struct panvk_gpu_prerast_out_types {
   nir_alu_type type[VARYING_SLOT_MAX];
};

static bool
panvk_gpu_prerast_collect_out_type(nir_builder *b, nir_intrinsic_instr *intr,
                                   void *data)
{
   struct panvk_gpu_prerast_out_types *t = data;
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   unsigned location = sem.location;
   if (nir_src_is_const(intr->src[1]))
      location += nir_src_as_uint(intr->src[1]);
   if (location < VARYING_SLOT_MAX)
      t->type[location] =
         nir_alu_type_get_base_type(nir_intrinsic_src_type(intr)) | 32;
   return false;
}

static nir_shader *
panvk_gpu_prerast_passthrough_nir(const nir_shader *source, uint64_t outputs,
                                  const struct panvk_gpu_prerast_out_types *t,
                                  const struct pan_varying_layout *layout)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, source->options, "panvk_gpu_prerast_passthrough");
    nir_def *base = load_sysval(&b, graphics, 64, gpu_prerast_records);
    nir_def *params = load_sysval(&b, graphics, 64, gpu_prerast_params);
    nir_def *stride = nir_u2u64(
       &b, nir_imm_int(&b, ALIGN_POT(util_bitcount64(outputs) * 16, 16)));
    nir_def *verts = nir_load_global(
       &b, 1, 32,
       nir_iadd_imm(&b, params,
                    offsetof(struct panvk_gpu_prerast_params, vertex_count)),
       .align_mul = 4);
    /* The generated draw uses indices 0..n-1 with vertexOffset 0, so the
     * Valhall IDVS vertex ID is the record index. load_vertex_id_zero_base
     * has no backend lowering here (preprocess already ran).
     */
    nir_def *vertex = nir_load_vertex_id(&b);
    nir_def *linear = nir_iadd(&b, vertex,
                               nir_imul(&b, nir_load_instance_id(&b), verts));
    nir_def *record = nir_iadd(&b, base, nir_imul(&b, nir_u2u64(&b, linear), stride));

    u_foreach_bit64(location, outputs) {
       /* store_output base is the HW varying-layout slot index, not the
        * varying location. */
       unsigned layout_idx = UINT_MAX;
       for (unsigned i = 0; i < layout->count; i++) {
          const struct pan_varying_slot *ls =
             pan_varying_layout_slot_at(layout, i);
          if (ls && ls->location == location) {
             layout_idx = i;
             break;
          }
       }
       if (layout_idx == UINT_MAX)
          continue;

       unsigned slot = util_bitcount64(outputs & BITFIELD64_MASK(location));
       nir_def *addr = nir_iadd_imm(&b, record, slot * 16);
       nir_def *value = nir_load_global(&b, 4, 32, addr, .align_mul = 16);
       nir_alu_type type = t->type[location] ? t->type[location]
                                             : nir_type_float32;
       nir_store_output(&b, value, nir_imm_int(&b, 0), .base = layout_idx,
                        .write_mask = 0xf,
                        .src_type = type,
                        .io_semantics = { .location = location, .num_slots = 1 });
    }

   b.shader->info.outputs_written = outputs;
   return b.shader;
}

static void
panvk_gpu_prerast_emit_generated_records(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_after_impl(impl));
   nir_def *gid = nir_load_global_invocation_id(&b, 32);
   nir_def *vertex = nir_channel(&b, gid, 0);
   nir_def *instance = nir_channel(&b, gid, 1);
   nir_def *params = load_sysval(&b, graphics, 64, gpu_prerast_params);

    nir_def *index = vertex;
    nir_def *index_offset = nir_load_global(
       &b, 1, 32,
       nir_iadd_imm(&b, params,
                    offsetof(struct panvk_gpu_prerast_params, index_offset)),
       .align_mul = 4);
    index = nir_iadd(&b, vertex, index_offset);
    nir_def *raw = poly_nir_load_raw_vertex_id(&b, index);
   nir_def *restart_enable = nir_load_global(
      &b, 1, 32,
      nir_iadd_imm(&b, params,
                   offsetof(struct panvk_gpu_prerast_params, primitive_restart)),
      .align_mul = 4);
   nir_def *restart_index = nir_load_global(
      &b, 1, 32,
      nir_iadd_imm(&b, params,
                   offsetof(struct panvk_gpu_prerast_params, restart_index)),
      .align_mul = 4);
   nir_def *is_restart = nir_iand(
      &b, nir_ine_imm(&b, restart_enable, 0), nir_ieq(&b, raw, restart_index));
   nir_def *verts = nir_load_global(
      &b, 1, 32,
      nir_iadd_imm(&b, params,
                   offsetof(struct panvk_gpu_prerast_params, vertex_count)),
      .align_mul = 4);
   nir_def *linear = nir_iadd(&b, vertex, nir_imul(&b, instance, verts));
   nir_def *generated_indices = nir_load_global(
      &b, 1, 64,
      nir_iadd_imm(&b, params,
                   offsetof(struct panvk_gpu_prerast_params, generated_indices)),
      .align_mul = 8);
   nir_push_if(&b, nir_iand(&b, nir_ieq_imm(&b, instance, 0),
                            nir_ult_imm(&b, vertex,
                                        PANVK_GPU_PRERAST_MAX_INVOCATIONS)));
   nir_store_global(&b, nir_bcsel(&b, is_restart, nir_imm_int(&b, -1), vertex),
                    nir_iadd(&b, generated_indices,
                             nir_u2u64(&b, nir_imul_imm(&b, vertex, 4))),
                    .write_mask = 1, .align_mul = 4);
   nir_pop_if(&b, NULL);
   nir_progress(true, impl, nir_metadata_none);
}

#define PRERAST_PARAM(b, p, bits, field)                                       \
   nir_load_global((b), 1, (bits),                                             \
                   nir_iadd_imm((b), (p),                                      \
                                offsetof(struct panvk_gpu_prerast_params,      \
                                         field)),                              \
                   .align_mul = (bits) / 8)

static nir_def *
panvk_gpu_prerast_is_restart(nir_builder *b, nir_def *restart_en,
                             nir_def *gen_indices, nir_def *pos)
{
   nir_def *idx = nir_load_global(
      b, 1, 32, nir_iadd(b, gen_indices, nir_u2u64(b, nir_imul_imm(b, pos, 4))),
      .align_mul = 4);
   return nir_iand(b, restart_en, nir_ieq_imm(b, idx, 0xffffffff));
}

static nir_shader *
panvk_gpu_prerast_polygon_nir(const nir_shader_compiler_options *options,
                              uint32_t record_stride, uint32_t pos_offset)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, options, "panvk_gpu_prerast_polygon");
   nir_function_impl *impl = b.impl;
   b.shader->info.workgroup_size[0] = 1;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   nir_def *gid = nir_load_global_invocation_id(&b, 32);
   nir_def *j = nir_channel(&b, gid, 0);
   nir_def *inst = nir_channel(&b, gid, 1);
   nir_def *p = load_sysval(&b, graphics, 64, gpu_prerast_params);

   nir_def *n = PRERAST_PARAM(&b, p, 32, vertex_count);
   nir_def *ninst = PRERAST_PARAM(&b, p, 32, instance_count);
   nir_def *restart_en =
      nir_ine_imm(&b, PRERAST_PARAM(&b, p, 32, primitive_restart), 0);
   nir_def *gen = PRERAST_PARAM(&b, p, 64, generated_indices);
   nir_def *recs = PRERAST_PARAM(&b, p, 64, vertex.output_buffer);
   nir_def *cap = PRERAST_PARAM(&b, p, 32, record_capacity);
   nir_def *out = PRERAST_PARAM(&b, p, 64, poly.indices);
   nir_def *mode = PRERAST_PARAM(&b, p, 32, poly.mode);
   nir_def *prim = PRERAST_PARAM(&b, p, 32, poly.prim);
   nir_def *cull = PRERAST_PARAM(&b, p, 32, poly.cull);
   nir_def *ccw = nir_ine_imm(&b, PRERAST_PARAM(&b, p, 32, poly.front_ccw), 0);
   nir_def *flip = nir_ine_imm(&b, PRERAST_PARAM(&b, p, 32, poly.flip_y), 0);

   const unsigned per = PANVK_GPU_PRERAST_POLY_INDICES_PER_PRIM;
   nir_def *base = nir_imul(&b, inst, n);
   nir_def *lin = nir_iadd(&b, base, j);
   nir_def *in_cap = nir_ult(&b, lin, cap);

   nir_push_if(&b, nir_ieq_imm(&b, lin, 0));
   {
      nir_def *total = nir_umin(&b, nir_imul(&b, n, ninst), cap);
      nir_def *draw = PRERAST_PARAM(&b, p, 64, poly.draw);
      nir_store_global(&b,
                       nir_vec4(&b, nir_imul_imm(&b, total, per),
                                nir_imm_int(&b, 1), nir_imm_int(&b, 0),
                                nir_imm_int(&b, 0)),
                       draw, .write_mask = 0xf, .align_mul = 4);
      nir_store_global(&b, nir_imm_int(&b, 0), nir_iadd_imm(&b, draw, 16),
                       .write_mask = 1, .align_mul = 4);
   }
   nir_pop_if(&b, NULL);

   nir_push_if(&b, in_cap);
   {
      nir_def *slot =
         nir_iadd(&b, out, nir_u2u64(&b, nir_imul_imm(&b, lin, per * 4)));
      nir_def *rst = nir_imm_int(&b, 0xffffffff);
      nir_store_global(&b, nir_vec4(&b, rst, rst, rst, rst), slot,
                       .write_mask = 0xf, .align_mul = 4);
      nir_store_global(&b, nir_vec2(&b, rst, rst), nir_iadd_imm(&b, slot, 16),
                       .write_mask = 0x3, .align_mul = 4);

      nir_push_if(&b, nir_inot(&b, panvk_gpu_prerast_is_restart(
                                       &b, restart_en, gen, j)));
      {
         nir_variable *seg_var =
            nir_local_variable_create(impl, glsl_uint_type(), "seg");
         nir_store_var(&b, seg_var, nir_bcsel(&b, restart_en, j, nir_imm_int(&b, 0)),
                       1);
         nir_push_if(&b, restart_en);
         nir_push_loop(&b);
         {
            nir_def *s = nir_load_var(&b, seg_var);
            nir_break_if(&b, nir_ieq_imm(&b, s, 0));
            nir_def *sm1 = nir_iadd_imm(&b, s, -1);
            nir_break_if(&b,
                         panvk_gpu_prerast_is_restart(&b, restart_en, gen, sm1));
            nir_store_var(&b, seg_var, sm1, 1);
         }
         nir_pop_loop(&b, NULL);
         nir_pop_if(&b, NULL);
         nir_def *seg = nir_load_var(&b, seg_var);
         nir_def *k = nir_isub(&b, j, seg);

         nir_def *is_list = nir_ieq_imm(&b, prim, MESA_PRIM_TRIANGLES);
         nir_def *is_strip = nir_ieq_imm(&b, prim, MESA_PRIM_TRIANGLE_STRIP);
         nir_def *valid =
            nir_bcsel(&b, is_list, nir_ieq_imm(&b, nir_umod_imm(&b, k, 3), 2),
                      nir_uge_imm(&b, k, 2));
         nir_def *odd =
            nir_ine_imm(&b, nir_iand_imm(&b, nir_iadd_imm(&b, k, -2), 1), 0);
         nir_def *jm1 = nir_iadd_imm(&b, j, -1);
         nir_def *jm2 = nir_iadd_imm(&b, j, -2);
         nir_def *va =
            nir_bcsel(&b, is_list, jm2,
                      nir_bcsel(&b, is_strip, nir_bcsel(&b, odd, jm1, jm2), jm1));
         nir_def *vb =
            nir_bcsel(&b, is_list, jm1,
                      nir_bcsel(&b, is_strip, nir_bcsel(&b, odd, jm2, jm1), j));
         nir_def *vc = nir_bcsel(&b, nir_ior(&b, is_list, is_strip), j, seg);

         nir_push_if(&b, valid);
         {
            nir_def *ids[3] = {nir_iadd(&b, base, va), nir_iadd(&b, base, vb),
                               nir_iadd(&b, base, vc)};
            nir_def *pos[3];
            for (unsigned i = 0; i < 3; i++) {
               nir_def *addr = nir_iadd(
                  &b, recs,
                  nir_u2u64(&b, nir_iadd_imm(&b,
                                             nir_imul_imm(&b, ids[i],
                                                          record_stride),
                                             pos_offset)));
               pos[i] = nir_load_global(&b, 4, 32, addr, .align_mul = 16);
            }
            nir_def *x0 = nir_channel(&b, pos[0], 0);
            nir_def *y0 = nir_channel(&b, pos[0], 1);
            nir_def *w0 = nir_channel(&b, pos[0], 3);
            nir_def *x1 = nir_channel(&b, pos[1], 0);
            nir_def *y1 = nir_channel(&b, pos[1], 1);
            nir_def *w1 = nir_channel(&b, pos[1], 3);
            nir_def *x2 = nir_channel(&b, pos[2], 0);
            nir_def *y2 = nir_channel(&b, pos[2], 1);
            nir_def *w2 = nir_channel(&b, pos[2], 3);
            nir_def *det = nir_fadd(
               &b,
               nir_fsub(&b,
                        nir_fmul(&b, x0,
                                 nir_fsub(&b, nir_fmul(&b, y1, w2),
                                          nir_fmul(&b, y2, w1))),
                        nir_fmul(&b, y0,
                                 nir_fsub(&b, nir_fmul(&b, x1, w2),
                                          nir_fmul(&b, x2, w1)))),
               nir_fmul(&b, w0,
                        nir_fsub(&b, nir_fmul(&b, x1, y2),
                                 nir_fmul(&b, x2, y1))));
            nir_def *s = nir_bcsel(&b, flip, nir_fneg(&b, det), det);
            nir_def *neg = nir_flt_imm(&b, s, 0.0);
            nir_def *pos_area = nir_fgt_imm(&b, s, 0.0);
            nir_def *front = nir_bcsel(&b, ccw, neg, pos_area);
            nir_def *back = nir_bcsel(&b, ccw, pos_area, neg);
            nir_def *culled = nir_ior(
               &b, nir_ieq_imm(&b, cull, 3),
               nir_ior(&b,
                       nir_iand(&b, nir_ine_imm(&b, nir_iand_imm(&b, cull, 1), 0),
                                front),
                       nir_iand(&b, nir_ine_imm(&b, nir_iand_imm(&b, cull, 2), 0),
                                back)));

            nir_push_if(&b, nir_inot(&b, culled));
            {
               nir_push_if(&b, nir_ieq_imm(&b, mode,
                                           PANVK_GPU_PRERAST_POLY_POINT));
               {
                  nir_store_global(&b, nir_vec3(&b, ids[0], ids[1], ids[2]),
                                   slot, .write_mask = 0x7, .align_mul = 4);
               }
               nir_push_else(&b, NULL);
               {
                  nir_store_global(&b,
                                   nir_vec4(&b, ids[0], ids[1], ids[1], ids[2]),
                                   slot, .write_mask = 0xf, .align_mul = 4);
                  nir_store_global(&b, nir_vec2(&b, ids[2], ids[0]),
                                   nir_iadd_imm(&b, slot, 16),
                                   .write_mask = 0x3, .align_mul = 4);
               }
               nir_pop_if(&b, NULL);
            }
            nir_pop_if(&b, NULL);
         }
         nir_pop_if(&b, NULL);
      }
      nir_pop_if(&b, NULL);
   }
   nir_pop_if(&b, NULL);

   return b.shader;
}
#undef PRERAST_PARAM

#define GS_PARAM(b, p, bits, field)                                            \
   nir_load_global((b), 1, (bits),                                             \
                   nir_iadd_imm((b), (p),                                      \
                                offsetof(struct panvk_gpu_prerast_gs_params,   \
                                         field)),                              \
                   .align_mul = (bits) / 8)

struct panvk_gs_lower_state {
   const struct panvk_gpu_prerast_gs_info *gs;
   nir_variable *vcount;
   nir_variable *ipos;
   /* Vertices in the current strip and primitives emitted, for the
    * pipeline statistics. */
   nir_variable *strip_len;
   nir_variable *prims;
   nir_def *vin[6];
   unsigned nvin;
   nir_def *inst;
   nir_def *n;
   nir_def *vs_records;
   nir_def *vs_outputs;
   nir_def *vs_stride;
   nir_def *rec_base;
   nir_def *rec_idx_base;
   nir_def *idx_base;
   nir_def *prim_id;
   nir_def *invocation;
};

static nir_def *
panvk_gs_is_restart(nir_builder *b, nir_def *restart_en, nir_def *gen,
                    nir_def *pos)
{
   nir_def *idx = nir_load_global(
      b, 1, 32, nir_iadd(b, gen, nir_u2u64(b, nir_imul_imm(b, pos, 4))),
      .align_mul = 4);
   return nir_iand(b, restart_en, nir_ieq_imm(b, idx, 0xffffffff));
}

static nir_def *
panvk_gs_prims_in_run(nir_builder *b, nir_def *prim, nir_def *len)
{
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *sub_clamp1 = nir_bcsel(b, nir_uge_imm(b, len, 1),
                                   nir_iadd_imm(b, len, -1), zero);
   nir_def *sub_clamp2 = nir_bcsel(b, nir_uge_imm(b, len, 2),
                                   nir_iadd_imm(b, len, -2), zero);
   nir_def *sub_clamp3 = nir_bcsel(b, nir_uge_imm(b, len, 3),
                                   nir_iadd_imm(b, len, -3), zero);
   nir_def *strip_adj = nir_bcsel(b, nir_uge_imm(b, len, 6),
                                  nir_udiv_imm(b, nir_iadd_imm(b, len, -4), 2),
                                  zero);
   nir_def *r = len;
   r = nir_bcsel(b, nir_ieq_imm(b, prim, MESA_PRIM_LINES),
                 nir_udiv_imm(b, len, 2), r);
   r = nir_bcsel(b, nir_ieq_imm(b, prim, MESA_PRIM_LINE_STRIP), sub_clamp1, r);
   r = nir_bcsel(b, nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLES),
                 nir_udiv_imm(b, len, 3), r);
   r = nir_bcsel(b, nir_ior(b, nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLE_STRIP),
                            nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLE_FAN)),
                 sub_clamp2, r);
   r = nir_bcsel(b, nir_ieq_imm(b, prim, MESA_PRIM_LINES_ADJACENCY),
                 nir_udiv_imm(b, len, 4), r);
   r = nir_bcsel(b, nir_ieq_imm(b, prim, MESA_PRIM_LINE_STRIP_ADJACENCY),
                 sub_clamp3, r);
   r = nir_bcsel(b, nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLES_ADJACENCY),
                 nir_udiv_imm(b, len, 6), r);
   r = nir_bcsel(b, nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLE_STRIP_ADJACENCY),
                 strip_adj, r);
   return r;
}

static nir_def *
panvk_gs_tri_strip_adj_offset(nir_builder *b, nir_def *lp, unsigned v,
                              nir_def *np, nir_def *pv_last)
{
   nir_def *last = nir_ieq(b, lp, nir_iadd_imm(b, np, -1));
   nir_def *first = nir_ieq_imm(b, lp, 0);
   nir_def *even = nir_ieq_imm(b, nir_iand_imm(b, lp, 1), 0);
   nir_def *even_or_first = nir_ior(b, even, first);

   nir_def *vr = nir_imm_int(b, v);
   vr = nir_bcsel(b, nir_iand(b, pv_last, nir_inot(b, even_or_first)),
                  nir_imm_int(b, (v + 4) % 6), vr);

   nir_def *offs[6] = {
      nir_imm_int(b, 0),
      nir_bcsel(b, first, nir_imm_int(b, 1),
                nir_bcsel(b, even, nir_imm_int(b, -2), nir_imm_int(b, 3))),
      nir_bcsel(b, even_or_first, nir_imm_int(b, 2), nir_imm_int(b, 4)),
      nir_bcsel(b, last, nir_imm_int(b, 5), nir_imm_int(b, 6)),
      nir_bcsel(b, even_or_first, nir_imm_int(b, 4), nir_imm_int(b, 2)),
      nir_bcsel(b, even_or_first, nir_imm_int(b, 3), nir_imm_int(b, -2)),
   };
   nir_def *off = offs[0];
   for (unsigned i = 1; i < 6; i++)
      off = nir_bcsel(b, nir_ieq_imm(b, vr, i), offs[i], off);

   return nir_iadd(b, nir_imul_imm(b, lp, 2), off);
}

static nir_def *
panvk_gs_widen32(nir_builder *b, nir_def *value, nir_alu_type type)
{
   if (value->bit_size == 32)
      return value;

   nir_alu_type base = nir_alu_type_get_base_type(type);
   return base == nir_type_float ? nir_f2f32(b, value)
          : base == nir_type_int ? nir_i2i32(b, value)
                                 : nir_u2u32(b, value);
}

static bool
panvk_gs_lower_intr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct panvk_gs_lower_state *s = data;
   const struct panvk_gpu_prerast_gs_info *gs = s->gs;

   switch (intr->intrinsic) {
   case nir_intrinsic_store_output: {
      b->cursor = nir_instr_remove(&intr->instr);
      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      if (sem.gs_streams & 0x3)
         return true;
      unsigned location = sem.location;
      if (nir_src_is_const(intr->src[1]))
         location += nir_src_as_uint(intr->src[1]);
      if (!(gs->outputs_written & BITFIELD64_BIT(location)))
         return true;
      unsigned slot =
         util_bitcount64(gs->outputs_written & BITFIELD64_MASK(location));
      nir_def *vc = nir_load_var(b, s->vcount);
      nir_def *addr = nir_iadd(
         b, s->rec_base,
         nir_u2u64(b, nir_iadd_imm(b, nir_imul_imm(b, vc, gs->record_stride),
                                   slot * 16 +
                                      nir_intrinsic_component(intr) * 4)));
      nir_def *value = panvk_gs_widen32(b, intr->src[0].ssa,
                                        nir_intrinsic_src_type(intr));
      nir_push_if(b, nir_ult_imm(b, vc, gs->max_vertices));
      nir_store_global(b, value, addr,
                       .write_mask = nir_intrinsic_write_mask(intr),
                       .align_mul = 4);
      nir_pop_if(b, NULL);
      return true;
   }

   case nir_intrinsic_emit_vertex: {
      b->cursor = nir_instr_remove(&intr->instr);
      if (nir_intrinsic_stream_id(intr) != 0)
         return true;
      nir_def *vc = nir_load_var(b, s->vcount);
      nir_push_if(b, nir_ult_imm(b, vc, gs->max_vertices));
      {
         nir_def *ip = nir_load_var(b, s->ipos);
         nir_push_if(b, nir_ult_imm(b, ip, gs->index_stride));
         nir_store_global(b, nir_iadd(b, s->rec_idx_base, vc),
                          nir_iadd(b, s->idx_base,
                                   nir_u2u64(b, nir_imul_imm(b, ip, 4))),
                          .write_mask = 1, .align_mul = 4);
         nir_pop_if(b, NULL);
         nir_store_var(b, s->ipos, nir_iadd_imm(b, ip, 1), 1);
         nir_store_var(b, s->vcount, nir_iadd_imm(b, vc, 1), 1);

         const unsigned prim_verts =
            gs->output_prim == MESA_PRIM_POINTS       ? 1
            : gs->output_prim == MESA_PRIM_LINE_STRIP ? 2
                                                      : 3;
         nir_def *len = nir_iadd_imm(b, nir_load_var(b, s->strip_len), 1);
         nir_store_var(b, s->strip_len, len, 1);
         nir_store_var(b, s->prims,
                       nir_iadd(b, nir_load_var(b, s->prims),
                                nir_b2i32(b, nir_uge_imm(b, len, prim_verts))),
                       1);
      }
      nir_pop_if(b, NULL);
      return true;
   }

   case nir_intrinsic_end_primitive: {
      b->cursor = nir_instr_remove(&intr->instr);
      if (nir_intrinsic_stream_id(intr) != 0)
         return true;
      nir_store_var(b, s->ipos, nir_iadd_imm(b, nir_load_var(b, s->ipos), 1),
                    1);
      nir_store_var(b, s->strip_len, nir_imm_int(b, 0), 1);
      return true;
   }

   case nir_intrinsic_load_per_vertex_input: {
      b->cursor = nir_before_instr(&intr->instr);
      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      nir_def *vtx = intr->src[0].ssa;
      nir_def *pos = s->vin[0];
      for (unsigned i = 1; i < s->nvin; i++)
         pos = nir_bcsel(b, nir_ieq_imm(b, vtx, i), s->vin[i], pos);
      nir_def *loc = nir_iadd_imm(b, intr->src[1].ssa, sem.location);
      nir_def *below = nir_iadd_imm(
         b, nir_ishl(b, nir_imm_int64(b, 1), loc), -1);
      nir_def *slot = nir_bit_count(b, nir_iand(b, s->vs_outputs, below));
      nir_def *rec = nir_iadd(b, nir_imul(b, s->inst, s->n), pos);
      nir_def *off = nir_iadd(
         b, nir_imul(b, rec, s->vs_stride),
         nir_iadd_imm(b, nir_imul_imm(b, slot, 16),
                      nir_intrinsic_component(intr) * 4));
      nir_def *v = nir_load_global(b, intr->def.num_components, 32,
                                   nir_iadd(b, s->vs_records, nir_u2u64(b, off)),
                                   .align_mul = 4);
      if (intr->def.bit_size != 32) {
         nir_alu_type t = nir_intrinsic_has_dest_type(intr)
                             ? nir_alu_type_get_base_type(
                                  nir_intrinsic_dest_type(intr))
                             : nir_type_float;
         v = t == nir_type_float ? nir_f2fN(b, v, intr->def.bit_size)
                                 : nir_u2uN(b, v, intr->def.bit_size);
      }
      nir_def_replace(&intr->def, v);
      return true;
   }

   case nir_intrinsic_load_primitive_id:
      b->cursor = nir_before_instr(&intr->instr);
      nir_def_replace(&intr->def, s->prim_id);
      return true;

   case nir_intrinsic_load_invocation_id:
      b->cursor = nir_before_instr(&intr->instr);
      nir_def_replace(&intr->def, s->invocation);
      return true;

   default:
      return false;
   }
}

static void
panvk_gpu_prerast_lower_gs(nir_shader *nir,
                           const struct panvk_gpu_prerast_gs_info *gs)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   const bool reads_prim_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);
   const unsigned nvin = nir->info.gs.vertices_in;
   const enum mesa_prim in_prim = nir->info.gs.input_primitive;

   nir_cf_list body;
   nir_cf_extract(&body, nir_before_impl(impl), nir_after_impl(impl));

   nir_builder bld = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &bld;

   struct panvk_gs_lower_state s = {
      .gs = gs,
      .nvin = nvin,
   };
   s.vcount = nir_local_variable_create(impl, glsl_uint_type(), "gs_vcount");
   s.ipos = nir_local_variable_create(impl, glsl_uint_type(), "gs_ipos");
   s.strip_len =
      nir_local_variable_create(impl, glsl_uint_type(), "gs_strip_len");
   s.prims = nir_local_variable_create(impl, glsl_uint_type(), "gs_prims");

   nir_def *gid = nir_load_global_invocation_id(b, 32);
   nir_def *j = nir_channel(b, gid, 0);
   s.inst = nir_channel(b, gid, 1);
   s.invocation = nir_channel(b, gid, 2);
   nir_def *p = load_sysval(b, graphics, 64, gpu_prerast_params);

   s.n = GS_PARAM(b, p, 32, vertex_count);
   nir_def *ninst = GS_PARAM(b, p, 32, instance_count);
   nir_def *restart_en =
      nir_ine_imm(b, GS_PARAM(b, p, 32, primitive_restart), 0);
   nir_def *prim = GS_PARAM(b, p, 32, prim);
   nir_def *cap_slots = GS_PARAM(b, p, 32, cap_slots);
   nir_def *vs_cap = GS_PARAM(b, p, 32, vs_record_capacity);
   nir_def *pv_last = nir_ine_imm(b, GS_PARAM(b, p, 32, provoking_last), 0);
   nir_def *gen = GS_PARAM(b, p, 64, gen_indices);
   s.vs_records = GS_PARAM(b, p, 64, vs_records);
   s.vs_outputs = GS_PARAM(b, p, 64, vs_outputs);
   s.vs_stride = nir_imul_imm(b, nir_bit_count(b, s.vs_outputs), 16);
   nir_def *records = GS_PARAM(b, p, 64, records);
   nir_def *indices = GS_PARAM(b, p, 64, indices);

   nir_def *slot_idx = nir_iadd(
      b, nir_imul_imm(b, nir_iadd(b, nir_imul(b, s.inst, s.n), j),
                      gs->invocations),
      s.invocation);
   nir_def *in_cap = nir_ult(b, slot_idx, cap_slots);

   nir_push_if(b, nir_ieq_imm(b, nir_ior(b, nir_ior(b, j, s.inst),
                                          s.invocation), 0));
   {
      nir_def *total = nir_umin(
         b, nir_imul_imm(b, nir_imul(b, s.n, ninst), gs->invocations),
         cap_slots);
      nir_def *draw = GS_PARAM(b, p, 64, draw);
      nir_store_global(b,
                       nir_vec4(b, nir_imul_imm(b, total, gs->index_stride),
                                nir_imm_int(b, 1), nir_imm_int(b, 0),
                                nir_imm_int(b, 0)),
                       draw, .write_mask = 0xf, .align_mul = 4);
      nir_store_global(b, nir_imm_int(b, 0), nir_iadd_imm(b, draw, 16),
                       .write_mask = 1, .align_mul = 4);
   }
   nir_pop_if(b, NULL);

   s.idx_base = nir_iadd(
      b, indices,
      nir_u2u64(b, nir_imul_imm(b, slot_idx, gs->index_stride * 4)));
   s.rec_idx_base = nir_imul_imm(b, slot_idx, gs->max_vertices);
   s.rec_base = nir_iadd(
      b, records,
      nir_u2u64(b, nir_imul_imm(b, s.rec_idx_base, gs->record_stride)));

   nir_push_if(b, in_cap);
   {
      nir_variable *fill =
         nir_local_variable_create(impl, glsl_uint_type(), "gs_fill");
      nir_store_var(b, fill, nir_imm_int(b, 0), 1);
      nir_def *rst = nir_imm_int(b, 0xffffffff);
      nir_push_loop(b);
      {
         nir_def *f = nir_load_var(b, fill);
         nir_break_if(b, nir_uge_imm(b, f, gs->index_stride));
         nir_store_global(b, nir_vec4(b, rst, rst, rst, rst),
                          nir_iadd(b, s.idx_base,
                                   nir_u2u64(b, nir_imul_imm(b, f, 4))),
                          .write_mask = 0xf, .align_mul = 16);
         nir_store_var(b, fill, nir_iadd_imm(b, f, 4), 1);
      }
      nir_pop_loop(b, NULL);
   }
   nir_pop_if(b, NULL);

   nir_variable *seg_var =
      nir_local_variable_create(impl, glsl_uint_type(), "gs_seg");
   nir_variable *end_var =
      nir_local_variable_create(impl, glsl_uint_type(), "gs_end");
   nir_store_var(b, seg_var, nir_imm_int(b, 0), 1);
   nir_store_var(b, end_var, s.n, 1);
   nir_push_if(b, restart_en);
   {
      nir_store_var(b, seg_var, j, 1);
      nir_push_loop(b);
      {
         nir_def *sg = nir_load_var(b, seg_var);
         nir_break_if(b, nir_ieq_imm(b, sg, 0));
         nir_def *sm1 = nir_iadd_imm(b, sg, -1);
         nir_break_if(b, panvk_gs_is_restart(b, restart_en, gen, sm1));
         nir_store_var(b, seg_var, sm1, 1);
      }
      nir_pop_loop(b, NULL);

      if (in_prim == MESA_PRIM_TRIANGLES_ADJACENCY) {
         nir_store_var(b, end_var, j, 1);
         nir_push_loop(b);
         {
            nir_def *e = nir_load_var(b, end_var);
            nir_break_if(b, nir_uge(b, e, s.n));
            nir_break_if(b, panvk_gs_is_restart(b, restart_en, gen, e));
            nir_store_var(b, end_var, nir_iadd_imm(b, e, 1), 1);
         }
         nir_pop_loop(b, NULL);
      }
   }
   nir_pop_if(b, NULL);
   nir_def *seg = nir_load_var(b, seg_var);
   nir_def *seg_len = nir_isub(b, nir_load_var(b, end_var), seg);
   nir_def *k = nir_isub(b, j, seg);
   nir_def *self_restart = panvk_gs_is_restart(b, restart_en, gen, j);

   nir_def *valid = NULL, *lp = NULL;
   nir_def *zero = nir_imm_int(b, 0);
   switch (in_prim) {
   case MESA_PRIM_POINTS:
      valid = nir_imm_true(b);
      lp = k;
      for (unsigned v = 0; v < nvin; v++)
         s.vin[v] = j;
      break;

   case MESA_PRIM_LINES:
   case MESA_PRIM_LINES_ADJACENCY: {
      const unsigned nv = nvin;
      const enum mesa_prim strip =in_prim == MESA_PRIM_LINES
                                 ? MESA_PRIM_LINE_STRIP
                                 : MESA_PRIM_LINE_STRIP_ADJACENCY;
      nir_def *is_strip = nir_ieq_imm(b, prim, strip);
      valid = nir_bcsel(b, is_strip, nir_uge_imm(b, k, nv - 1),
                        nir_ieq_imm(b, nir_umod_imm(b, k, nv), nv - 1));
      lp = nir_bcsel(b, is_strip,
                     nir_bcsel(b, nir_uge_imm(b, k, nv - 1),
                               nir_iadd_imm(b, k, -(int)(nv - 1)), zero),
                     nir_udiv_imm(b, k, nv));
      for (unsigned v = 0; v < nv; v++)
         s.vin[v] = nir_iadd_imm(b, j, (int)v - (int)(nv - 1));
      break;
   }

   case MESA_PRIM_TRIANGLES: {
      nir_def *is_list = nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLES);
      nir_def *is_strip = nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLE_STRIP);
      nir_def *ge2 = nir_uge_imm(b, k, 2);
      valid = nir_bcsel(b, is_list,
                        nir_ieq_imm(b, nir_umod_imm(b, k, 3), 2), ge2);
      nir_def *slp = nir_bcsel(b, ge2, nir_iadd_imm(b, k, -2), zero);
      lp = nir_bcsel(b, is_list, nir_udiv_imm(b, k, 3), slp);
      nir_def *odd = nir_ine_imm(b, nir_iand_imm(b, slp, 1), 0);
      for (unsigned v = 0; v < 3; v++) {
         nir_def *first_map =
            nir_bcsel(b, odd, nir_imm_int(b, v == 0 ? 0 : 3 - v),
                      nir_imm_int(b, v));
         nir_def *last_map =
            nir_bcsel(b, odd, nir_imm_int(b, v == 2 ? 2 : 1 - v),
                      nir_imm_int(b, v));
         nir_def *smap = nir_bcsel(b, pv_last, last_map, first_map);
         nir_def *strip_pos = nir_iadd(b, nir_iadd(b, seg, slp), smap);
         nir_def *fv_first = nir_imm_int(b, v == 2 ? 0 : v + 1);
         nir_def *fv = nir_bcsel(b, pv_last, nir_imm_int(b, v), fv_first);
         nir_def *fan_pos =
            nir_bcsel(b, nir_ieq_imm(b, fv, 0), seg,
                      nir_iadd(b, nir_iadd(b, seg, slp), fv));
         nir_def *list_pos = nir_iadd_imm(b, j, (int)v - 2);
         s.vin[v] = nir_bcsel(b, is_list, list_pos,
                              nir_bcsel(b, is_strip, strip_pos, fan_pos));
      }
      break;
   }

   case MESA_PRIM_TRIANGLES_ADJACENCY: {
      nir_def *is_strip =
         nir_ieq_imm(b, prim, MESA_PRIM_TRIANGLE_STRIP_ADJACENCY);
      nir_def *np = panvk_gs_prims_in_run(b, prim, seg_len);
      nir_def *ge5 = nir_uge_imm(b, k, 5);
      nir_def *slp = nir_bcsel(b, ge5,
                               nir_ushr_imm(b, nir_iadd_imm(b, k, -5), 1),
                               zero);
      nir_def *strip_valid =
         nir_iand(b, nir_iand(b, ge5, nir_ieq_imm(b, nir_iand_imm(b, k, 1), 1)),
                  nir_ult(b, slp, np));
      valid = nir_bcsel(b, is_strip, strip_valid,
                        nir_ieq_imm(b, nir_umod_imm(b, k, 6), 5));
      lp = nir_bcsel(b, is_strip, slp, nir_udiv_imm(b, k, 6));
      for (unsigned v = 0; v < 6; v++) {
         nir_def *strip_pos = nir_iadd(
            b, seg, panvk_gs_tri_strip_adj_offset(b, slp, v, np, pv_last));
         s.vin[v] = nir_bcsel(b, is_strip, strip_pos,
                              nir_iadd_imm(b, j, (int)v - 5));
      }
      break;
   }

   default:
      UNREACHABLE("invalid GS input primitive");
   }

   valid = nir_iand(b, valid, nir_inot(b, self_restart));
   valid = nir_iand(b, valid, in_cap);
   valid = nir_iand(b, valid,
                    nir_uge(b, vs_cap,
                            nir_imul(b, nir_iadd_imm(b, s.inst, 1), s.n)));

   s.prim_id = lp;
   if (reads_prim_id) {
      nir_variable *before =
         nir_local_variable_create(impl, glsl_uint_type(), "gs_prims_before");
      nir_store_var(b, before, zero, 1);
      nir_push_if(b, restart_en);
      {
         nir_variable *q =
            nir_local_variable_create(impl, glsl_uint_type(), "gs_q");
         nir_variable *len =
            nir_local_variable_create(impl, glsl_uint_type(), "gs_len");
         nir_store_var(b, q, zero, 1);
         nir_store_var(b, len, zero, 1);
         nir_push_loop(b);
         {
            nir_def *qq = nir_load_var(b, q);
            nir_break_if(b, nir_uge(b, qq, seg));
            nir_def *l = nir_load_var(b, len);
            nir_push_if(b, panvk_gs_is_restart(b, restart_en, gen, qq));
            {
               nir_store_var(b, before,
                             nir_iadd(b, nir_load_var(b, before),
                                      panvk_gs_prims_in_run(b, prim, l)),
                             1);
               nir_store_var(b, len, zero, 1);
            }
            nir_push_else(b, NULL);
            nir_store_var(b, len, nir_iadd_imm(b, l, 1), 1);
            nir_pop_if(b, NULL);
            nir_store_var(b, q, nir_iadd_imm(b, qq, 1), 1);
         }
         nir_pop_loop(b, NULL);
      }
      nir_pop_if(b, NULL);
      s.prim_id = nir_iadd(b, nir_load_var(b, before), lp);
   }
   s.prim_id = nir_iadd(b, s.prim_id, GS_PARAM(b, p, 32, prim_id_base));

   nir_store_var(b, s.vcount, zero, 1);
   nir_store_var(b, s.ipos, zero, 1);
   nir_store_var(b, s.strip_len, zero, 1);
   nir_store_var(b, s.prims, zero, 1);

   nir_if *vif = nir_push_if(b, valid);
   b->cursor = nir_cf_reinsert(&body, b->cursor);
   nir_pop_if(b, vif);

   /* Pipeline statistics: one GS invocation per valid invocation, and the
    * emitted primitives for the GS primitive and clipping counters. */
   nir_push_if(b, valid);
   {
      nir_def *emitted = nir_u2u64(b, nir_load_var(b, s.prims));
      for (unsigned i = 0; i < 4; i++) {
         nir_def *counter = nir_load_global(
            b, 1, 64,
            nir_iadd_imm(b, p,
                         offsetof(struct panvk_gpu_prerast_gs_params, stats) +
                            i * sizeof(uint64_t)),
            .align_mul = 8);
         nir_push_if(b, nir_ine_imm(b, counter, 0));
         nir_global_atomic(b, 64, counter,
                           i == 0 ? nir_imm_int64(b, 1) : emitted,
                           .atomic_op = nir_atomic_op_iadd);
         nir_pop_if(b, NULL);
      }
   }
   nir_pop_if(b, NULL);

   nir_progress(true, impl, nir_metadata_none);

   nir_shader_intrinsics_pass(nir, panvk_gs_lower_intr, nir_metadata_none,
                              &s);

   nir->info.stage = MESA_SHADER_COMPUTE;
   memset(&nir->info.cs, 0, sizeof(nir->info.cs));
   nir->info.workgroup_size[0] = 1;
   nir->info.workgroup_size[1] = 1;
   nir->info.workgroup_size[2] = 1;
   nir->info.outputs_written = 0;
   nir->info.inputs_read = 0;
   nir->num_outputs = 0;
   nir->num_inputs = 0;
}

static nir_shader *
panvk_gpu_prerast_gs_passthrough_nir(const nir_shader_compiler_options *options,
                                     const struct panvk_gpu_prerast_gs_info *gs,
                                     const struct panvk_gpu_prerast_out_types *t)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, options, "panvk_gpu_prerast_gs_passthrough");
   nir_def *base = load_sysval(&b, graphics, 64, gpu_prerast_records);
   nir_def *record = nir_iadd(
      &b, base,
      nir_u2u64(&b, nir_imul_imm(&b, nir_load_vertex_id(&b),
                                 gs->record_stride)));

   unsigned idx = 0;
   u_foreach_bit64(location, gs->outputs_written) {
      if (location == VARYING_SLOT_VIEWPORT)
         continue;
      unsigned slot =
         util_bitcount64(gs->outputs_written & BITFIELD64_MASK(location));
      unsigned ncomp = 4;
      nir_alu_type type = t->type[location] ? t->type[location]
                                            : nir_type_float32;
      switch (location) {
      case VARYING_SLOT_PSIZ:
         ncomp = 1;
         type = nir_type_float32;
         break;
      case VARYING_SLOT_LAYER:
      case VARYING_SLOT_VIEWPORT:
      case VARYING_SLOT_PRIMITIVE_ID:
         ncomp = 1;
         type = nir_type_int32;
         break;
      default:
         break;
      }
      nir_def *value = nir_load_global(&b, ncomp, 32,
                                       nir_iadd_imm(&b, record, slot * 16),
                                       .align_mul = 4);
      nir_store_output(&b, value, nir_imm_int(&b, 0), .base = idx++,
                       .write_mask = BITFIELD_MASK(ncomp), .src_type = type,
                       .io_semantics = {.location = location, .num_slots = 1});
   }

   b.shader->info.outputs_written =
      gs->outputs_written & ~VARYING_BIT_VIEWPORT;
   b.shader->num_outputs = idx;
   return b.shader;
}
#undef GS_PARAM
#endif

static void
panvk_hash_state(struct vk_physical_device *device,
                 const struct vk_graphics_pipeline_state *state,
                 const struct vk_features *enabled_features,
                 VkShaderStageFlags stages, blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

#if PAN_ARCH >= 10
   const uint32_t gpu_prerast_abi = PANVK_GPU_PRERAST_ABI_VERSION;
   _mesa_blake3_update(&blake3_ctx, &gpu_prerast_abi, sizeof(gpu_prerast_abi));
#endif
#if PAN_ARCH >= 9
   const bool robust_ssbo_vec = PANVK_DEBUG(ROBUST_SSBO_VEC);
   _mesa_blake3_update(&blake3_ctx, &robust_ssbo_vec, sizeof(robust_ssbo_vec));
#endif

   if (state != NULL) {
      /* This doesn't impact the shader compile but it does go in the
       * panvk_shader and gets [de]serialized along with the binary so
       * we need to hash it.
       */
      bool sample_shading_enable =
         state->ms && state->ms->sample_shading_enable;
      _mesa_blake3_update(&blake3_ctx, &sample_shading_enable,
                          sizeof(sample_shading_enable));

      _mesa_blake3_update(&blake3_ctx, &state->mv->view_mask,
                          sizeof(state->mv->view_mask));

      if (state->ial)
         _mesa_blake3_update(&blake3_ctx, state->ial, sizeof(*state->ial));

#if PAN_ARCH >= 10
      bool non_solid = (stages & VK_SHADER_STAGE_VERTEX_BIT) && state->rs &&
                       state->rs->polygon_mode != VK_POLYGON_MODE_FILL;
      _mesa_blake3_update(&blake3_ctx, &non_solid, sizeof(non_solid));
#endif
   }

   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

#if PAN_ARCH >= 9
static bool
valhall_pack_buf_idx(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   unsigned index_src;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      index_src = 0;
      break;

   case nir_intrinsic_store_ssbo:
      index_src = 1;
      break;

   default:
      return false;
   }

   nir_def *index = intrin->src[index_src].ssa;

   /* The descriptor lowering pass can add UBO loads, and those already have the
    * right index format. */
   if (index->num_components == 1)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   /* The valhall backend expects nir_address_format_32bit_index_offset,
    * but address mode is nir_address_format_vec2_index_32bit_offset to allow
    * us to store the array size, set and index without losing information
    * while walking the descriptor deref chain (needed to do a bound check on
    * the array index when we reach the end of the chain).
    * Turn it back to nir_address_format_32bit_index_offset after IOs
    * have been lowered. */
   nir_def *packed_index =
      nir_iadd(b, nir_channel(b, index, 0), nir_channel(b, index, 1));
   nir_src_rewrite(&intrin->src[index_src], packed_index);
   return true;
}
#endif

static bool
is_robust_ssbo_intr(const nir_intrinsic_instr *intr, UNUSED const void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      return true;
   default:
      return false;
   }
}

static bool
valhall_lower_get_ssbo_size(struct nir_builder *b,
                            nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_get_ssbo_size)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *res_handle = nir_channel(b, intr->src[0].ssa, 0);
   nir_def *table_idx = nir_ushr_imm(b, res_handle, 24);
   nir_def *res_idx = nir_iand_imm(b, res_handle, BITFIELD_MASK(24));
   nir_def *res_table = nir_ior_imm(b, table_idx, pan_res_handle(62, 0));
   nir_def *buf_idx = nir_iadd(b, res_idx, nir_channel(b, intr->src[0].ssa, 1));
   nir_def *desc_offset = nir_imul_imm(b, buf_idx, PANVK_DESCRIPTOR_SIZE);
   nir_def *size = nir_load_ubo(
      b, 1, 32, res_table, nir_iadd_imm(b, desc_offset, 4), .range = ~0u,
      .align_mul = PANVK_DESCRIPTOR_SIZE, .align_offset = 4);

   nir_def_replace(&intr->def, size);
   return true;
}

static bool
collect_push_constant(struct nir_builder *b, nir_intrinsic_instr *intr,
                      void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   struct panvk_shader_variant *shader = data;
   uint32_t base = nir_intrinsic_base(intr);
   bool is_sysval = base >= SYSVALS_PUSH_CONST_BASE;
   uint32_t offset, size;

   if (is_sysval)
      base -= SYSVALS_PUSH_CONST_BASE;

   /* If the offset is dynamic, we need to flag [base:base+range] as used, to
    * allow global mem access. */
   if (!nir_src_is_const(intr->src[0])) {
      offset = base;
      size = nir_intrinsic_range(intr);

      /* Flag the push_uniforms sysval as needed if we have an indirect offset.
       */
      shader_use_sysval(shader, common, push_uniforms);
   } else {
      offset = base + nir_src_as_uint(intr->src[0]);
      size = (intr->def.bit_size / 8) * intr->def.num_components;
   }

   if (is_sysval)
      shader_use_sysval_range(shader, offset, size);
   else
      shader_use_push_const_range(shader, offset, size);

   return true;
}

static bool
move_push_constant(struct nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   struct panvk_shader_variant *shader = data;
   unsigned base = nir_intrinsic_base(intr);
   bool is_sysval = base >= SYSVALS_PUSH_CONST_BASE;

   if (is_sysval)
      base -= SYSVALS_PUSH_CONST_BASE;

   b->cursor = nir_before_instr(&intr->instr);

   if (nir_src_is_const(intr->src[0])) {
      unsigned offset = base + nir_src_as_uint(intr->src[0]);

      /* We place the sysvals first, and then comes the user push constants.
       * We do that so we always have the blend constants at offset 0 for
       * blend shaders. */
      if (is_sysval)
         offset = shader_remapped_sysval_offset(shader, offset);
      else
         offset = shader_remapped_push_const_offset(shader, offset);

      nir_src_rewrite(&intr->src[0], nir_imm_int(b, offset));

      /* We always set the range/base to zero, to make sure no pass is using it
       * after that point. */
      nir_intrinsic_set_base(intr, 0);
      nir_intrinsic_set_range(intr, 0);
   } else {
      /* We don't use load_sysval() on purpose, because it would set
       * .base=SYSVALS_PUSH_CONST_BASE, and we're supposed to force a base of
       * zero in this pass. */
      unsigned push_const_buf_offset = shader_remapped_sysval_offset(
         shader, sysval_offset(common, push_uniforms));
      nir_def *push_const_buf = nir_load_push_constant(
         b, 1, 64, nir_imm_int(b, push_const_buf_offset));
      unsigned push_const_offset = is_sysval ?
         shader_remapped_sysval_offset(shader, base) :
         shader_remapped_push_const_offset(shader, base);
      nir_def *offset = nir_iadd_imm(b, intr->src[0].ssa, push_const_offset);
      unsigned align = nir_combined_align(nir_intrinsic_align_mul(intr),
                                          nir_intrinsic_align_offset(intr));

      /* We assume an alignment of 64-bit max for packed push-constants. */
      align = MIN2(align, FAU_WORD_SIZE);
      nir_def *value = nir_load_global(
         b, intr->def.num_components, intr->def.bit_size,
         nir_iadd(b, push_const_buf, nir_u2u64(b, offset)), .align_mul = align);

      nir_def_replace(&intr->def, value);
   }

   return true;
}

static void
lower_load_push_consts(nir_shader *nir, struct panvk_shader_variant *shader)
{
   /* Before we lower load_push_constant()s with a dynamic offset to global
    * loads, we want to run a few optimization passes to get rid of offset
    * calculation involving only constant values. */
   bool progress = false;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);

      nir_opt_peephole_select_options peephole_select_options = {
         .limit = 64,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peephole_select_options);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
   } while (progress);

   /* We always reserve the 4 blend constant words for fragment shaders,
    * because we don't know the blend configuration at this point, and
    * we might end up with a blend shader reading those blend constants. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* We rely on blend constants being placed first and covering 4 words. */
      STATIC_ASSERT(
         offsetof(struct panvk_graphics_sysvals, blend.constants) == 0 &&
         sizeof(((struct panvk_graphics_sysvals *)NULL)->blend.constants) ==
            16);

      shader_use_sysval(shader, graphics, blend.constants);
   }

   progress = false;
   NIR_PASS(progress, nir, nir_shader_intrinsics_pass, collect_push_constant,
            nir_metadata_all, shader);

   /* Some load_push_constant instructions might be eliminated after
    * scalarization+dead-code-elimination. Since these pass happen in
    * bifrost_compile(), we can't run the push_constant packing after the
    * optimization took place, so let's just have our own FAU count instead
    * of using info.fau.end to make it consistent with the
    * used_{sysvals,push_consts} bitmaps, even if it sometimes implies loading
    * more than we really need. Doing that also takes into account the fact
    * blend constants are never loaded from the fragment shader, but might be
    * needed in the blend shader. */
   shader->fau.sysval_count = BITSET_COUNT(shader->fau.used_sysvals);
   /* 32 FAUs (256 bytes) are reserved for API push constants */
   assert(shader->fau.sysval_count <= FAU_WORD_COUNT - 32 &&
          "too many sysval FAUs");
   shader->fau.total_count =
      shader->fau.sysval_count + BITSET_COUNT(shader->fau.used_push_consts);
   assert(shader->fau.total_count <= FAU_WORD_COUNT &&
          "asking for more FAUs than the hardware has to offer");

   if (!progress)
      return;

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, move_push_constant,
            nir_metadata_control_flow, shader);
}

struct lower_ycbcr_state {
   uint32_t set_layout_count;
   struct vk_descriptor_set_layout *const *set_layouts;
};

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *_state, uint32_t set,
                        uint32_t binding, uint32_t array_index)
{
   const struct lower_ycbcr_state *state = _state;
   assert(set < state->set_layout_count);
   assert(state->set_layouts[set] != NULL);
   const struct panvk_descriptor_set_layout *set_layout =
      to_panvk_descriptor_set_layout(state->set_layouts[set]);
   assert(binding < set_layout->binding_count);

   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &set_layout->bindings[binding];

   if (bind_layout->immutable_samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->desc_count - 1);

   const struct panvk_sampler *sampler =
      bind_layout->immutable_samplers[array_index];

   if (!sampler || !sampler->vk.ycbcr_conversion)
      return NULL;

   const struct vk_ycbcr_conversion_state *conversion =
      &sampler->vk.ycbcr_conversion->state;
   if (panvk_image_use_yuv_tex(PAN_ARCH, conversion->format))
      return NULL;

   return conversion;
}

static unsigned
glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void
panvk_lower_nir(struct panvk_device *dev, nir_shader *nir,
                uint32_t set_layout_count,
                struct vk_descriptor_set_layout *const *set_layouts,
                const struct vk_pipeline_robustness_state *rs,
                const struct vk_graphics_pipeline_state *state,
                struct panvk_shader_desc_info *desc_info,
                bool allow_merging_workgroups)
{
   mesa_shader_stage stage = nir->info.stage;

   NIR_PASS(_, nir, nir_opt_large_constants, NULL, 32);

   /* Run before descriptor and explicit-IO lowering so the memory derefs this
    * pass emits get lowered by them.
    */
   NIR_PASS(_, nir, panvk_nir_lower_cooperative_matrix,
            pan_subgroup_size(PAN_ARCH));

   const nir_opt_access_options access_options = {
      .is_vulkan = true,
   };
   NIR_PASS(_, nir, nir_opt_access, &access_options);

   const struct lower_ycbcr_state ycbcr_state = {
      .set_layout_count = set_layout_count,
      .set_layouts = set_layouts,
   };
   NIR_PASS(_, nir, nir_vk_lower_ycbcr_tex, lookup_ycbcr_conversion,
            &ycbcr_state);

   /* We need to do this before nir_lower_descriptors so any image_deref_size
    * intrinsics generated can be lowered there.
    */
   if (PAN_ARCH < 9)
      NIR_PASS(_, nir, pan_nir_lower_image_ms);

   panvk_per_arch(nir_lower_descriptors)(nir, dev, rs, set_layout_count,
                                         set_layouts, state, desc_info);

   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_memcpy);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo,
            panvk_buffer_ubo_addr_format(rs->uniform_buffers));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ssbo,
            panvk_buffer_ssbo_addr_format(rs->storage_buffers));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_global,
            nir_address_format_64bit_global);

   /* nir_lower_ssbo lowers SSBO writes to unbounded store_global, so the
    * descriptor-level bounds check Mali HW does for native buffer
    * loads/stores is bypassed. Insert software bounds checks here for SSBO
    * accesses when robust storage buffer access is requested. */
   if (rs->storage_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT) {
      NIR_PASS(_, nir, nir_lower_robust_access, is_robust_ssbo_intr, NULL);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_opt_dce);
   }

#if PAN_ARCH >= 9
   if (PANVK_DEBUG(ROBUST_SSBO_VEC) && stage == MESA_SHADER_FRAGMENT &&
       rs->storage_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT) {
      NIR_PASS(_, nir, nir_opt_copy_prop);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_opt_cse);
      NIR_PASS(_, nir, nir_opt_vectorize_robust_ssbo_loads);
   }
#endif

#if PAN_ARCH >= 10
   if (allow_merging_workgroups) {
      /* Accesses that were uniform in the source shader may now be
       * nonuniform. To handle this, we just flag everything as nonuniform and
       * then let nir_opt_non_uniform_access figure out which ones can really
       * be nonuniform based on divergence analysis */
      NIR_PASS(_, nir, nir_shader_instructions_pass,
               mark_all_access_non_uniform, nir_metadata_all, NULL);
   }
#endif

   /* nir_lower_non_uniform_access needs to run after lowering UBO and SSBO
    * IO. This means we run it after nir_lower_descriptors, which reads the
    * array indices, but it's okay because lower_descriptors treats all
    * dynamic indices the same. */
   enum nir_lower_non_uniform_access_type lower_non_uniform_access_types =
      nir_lower_non_uniform_ubo_access |
      nir_lower_non_uniform_ssbo_access |
      nir_lower_non_uniform_texture_access |
      nir_lower_non_uniform_texture_query |
      nir_lower_non_uniform_image_access |
      nir_lower_non_uniform_image_query |
      nir_lower_non_uniform_get_ssbo_size;
#if PAN_ARCH < 9
   lower_non_uniform_access_types |=
      nir_lower_non_uniform_texture_offset_access;
#endif

   /* In practice, most shaders do not have non-uniform-qualified accesses
    * thus a cheaper and likely to fail check is run first. */
   if (allow_merging_workgroups ||
       nir_has_non_uniform_access(nir, lower_non_uniform_access_types)) {
      NIR_PASS(_, nir, nir_opt_non_uniform_access);
      struct nir_lower_non_uniform_access_options opts = {
         .types = lower_non_uniform_access_types,
      };
      NIR_PASS(_, nir, nir_lower_non_uniform_access, &opts);
   }

#if PAN_ARCH >= 9
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, valhall_lower_get_ssbo_size,
            nir_metadata_control_flow, NULL);
   NIR_PASS(_, nir, nir_shader_instructions_pass, valhall_pack_buf_idx,
            nir_metadata_control_flow, NULL);
#endif

   if (mesa_shader_stage_uses_workgroup(stage)) {
      NIR_PASS(_, nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
               shared_type_info);

      NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared,
               nir_address_format_32bit_offset);
   }

   if (nir->info.zero_initialize_shared_memory && nir->info.shared_size > 0) {
      /* Align everything up to 16 bytes to take advantage of load store
       * vectorization. */
      nir->info.shared_size = align(nir->info.shared_size, 16);
      NIR_PASS(_, nir, nir_zero_initialize_shared_memory, nir->info.shared_size,
               16);

      /* We need to call lower_compute_system_values again because
       * nir_zero_initialize_shared_memory generates load_invocation_id which
       * has to be lowered to load_invocation_index.
       */
      NIR_PASS(_, nir, nir_lower_compute_system_values, NULL);
   }

   /* Needed to turn shader_temp into function_temp since the backend only
    * handles the latter for now.
    */
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (PANVK_DEBUG(NIR)) {
      mesa_logi("translated nir:");
      nir_log_shaderi(nir);
   }
}

static void
panvk_lower_nir_io(nir_shader *nir)
{
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_shader_in | nir_var_shader_out, UINT32_MAX);
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            glsl_type_size, nir_lower_io_use_interpolated_input_intrinsics);

   /* nir_lower_io just computes offsets based on the original deref and
    * lower_indirect_derefs ensures that the array derefs have a constant
    * index.  Constant-fold to get us actual constants in in load/store
    * instructions.
    */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   pan_nir_lower_mediump_io(nir);
}

static VkResult
panvk_compile_nir(struct panvk_device *dev, nir_shader *nir,
                  VkShaderCreateFlagsEXT shader_flags,
                  const struct pan_compile_inputs *compile_input,
                  const struct vk_graphics_pipeline_state *state,
                  const uint32_t *noperspective_varyings,
                  struct panvk_shader_desc_info *desc_info,
                  struct panvk_shader_variant *shader)
{
   const bool dump_asm =
      shader_flags & VK_SHADER_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_MESA;

   /* We're going to modify this so make our own copy to be nicer to callers */
   struct pan_compile_inputs input = *compile_input;

   pan_postprocess_nir(nir, &input, &shader->info);

   if (noperspective_varyings && nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_, nir, nir_inline_sysval,
               nir_intrinsic_load_noperspective_varyings_pan,
               *noperspective_varyings);
   }

   struct panvk_lower_sysvals_context lower_sysvals_ctx = {
      .shader = shader,
      .state = state,
   };

   NIR_PASS(_, nir, nir_shader_instructions_pass, panvk_lower_sysvals,
            nir_metadata_control_flow, &lower_sysvals_ctx);

   lower_load_push_consts(nir, shader);

   /* Reserve sysvals/push-const, the compiler may fill the remaining space with
    * promoted constants. */
   input.fau.reserved = shader->fau.total_count * 2;
   input.fau.promote_immediates = true;

   struct util_dynarray binary = UTIL_DYNARRAY_INIT;
   pan_shader_compile(nir, &input, &binary, &shader->info);

   /* Propagate potential additional FAU values into the panvk info struct. */
   /* FAU consts are pushed as 32bit values, but total_count is for 64bit
    * ones. */
   shader->fau.total_count = DIV_ROUND_UP(shader->info.fau.count, 2);

   void *bin_ptr = util_dynarray_element(&binary, uint8_t, 0);
   unsigned bin_size = util_dynarray_num_elements(&binary, uint8_t);

   shader->bin_size = 0;
   shader->bin_ptr = NULL;

   if (bin_size) {
      void *data = malloc(bin_size);

      if (data == NULL)
         return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

      memcpy(data, bin_ptr, bin_size);
      shader->bin_size = bin_size;
      shader->bin_ptr = data;
   }
   util_dynarray_fini(&binary);

#if PAN_ARCH < 9
   if (nir->constant_data_size) {
      shader->data_ptr = mem_dup(nir->constant_data, nir->constant_data_size);

      if (shader->data_ptr == NULL)
         return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

      shader->data_size = nir->constant_data_size;
   }
#endif

   if (dump_asm) {
      shader->nir_str = nir_shader_as_str(nir, NULL);

      char *data = NULL;
      size_t disasm_size = 0;

      if (shader->bin_size) {
         struct u_memstream mem;
         if (u_memstream_open(&mem, &data, &disasm_size)) {
            FILE *const stream = u_memstream_get(&mem);
            struct panvk_physical_device *phys_dev =
               to_panvk_physical_device(dev->vk.physical);
            pan_disassemble(stream, shader->bin_ptr, shader->bin_size,
                            phys_dev->kmod.dev->props.gpu_id, false);
            if (nir->constant_data_size) {
               fprintf(stream, "constant data (%u bytes):\n",
                       nir->constant_data_size);
               u_hexdump_words(stream, nir->constant_data, nir->constant_data_size);
            }
            u_memstream_close(&mem);
         }
      }

      char *asm_str = malloc(disasm_size + 1);
      memcpy(asm_str, data, disasm_size);
      asm_str[disasm_size] = '\0';
      free(data);

      shader->asm_str = asm_str;
   }

   /* Pad the total to the 64-bit-aligned FAU count; it's used to initialize the
    * RSD in pan_shader_prepare_rsd().
    */
   shader->info.fau.count = shader->fau.total_count * 2;

#if PAN_ARCH < 9
   /* Patch the descriptor count */
   shader->info.ubo_count =
      desc_info->others.count[PANVK_BIFROST_DESC_TABLE_UBO] +
      desc_info->dyn_ubos.count;
   shader->info.texture_count =
      desc_info->others.count[PANVK_BIFROST_DESC_TABLE_TEXTURE];
   shader->info.sampler_count =
      desc_info->others.count[PANVK_BIFROST_DESC_TABLE_SAMPLER];

   /* Dummy sampler. */
   if (!shader->info.sampler_count && shader->info.texture_count)
      shader->info.sampler_count++;

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      /* We leave holes in the attribute locations, but pan_shader.c assumes the
       * opposite. Patch attribute_count accordingly, so
       * pan_shader_prepare_rsd() does what we expect.
       */
      uint32_t gen_attribs =
         (shader->info.attributes_read & VERT_BIT_GENERIC_ALL) >>
         VERT_ATTRIB_GENERIC0;

      shader->info.attribute_count = util_last_bit(gen_attribs);

      /* NULL IDVS shaders are not allowed. */
      if (!bin_size)
         shader->info.vs.idvs = false;
   }

   /* Image attributes start at MAX_VS_ATTRIBS in the VS attribute table,
    * and zero in other stages.
    */
   if (desc_info->others.count[PANVK_BIFROST_DESC_TABLE_IMG] > 0)
      shader->info.attribute_count =
         desc_info->others.count[PANVK_BIFROST_DESC_TABLE_IMG] +
         (nir->info.stage == MESA_SHADER_VERTEX ? MAX_VS_ATTRIBS : 0);
#endif

   switch (nir->info.stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      shader->cs.local_size.x = nir->info.workgroup_size[0];
      shader->cs.local_size.y = nir->info.workgroup_size[1];
      shader->cs.local_size.z = nir->info.workgroup_size[2];
      break;

   case MESA_SHADER_FRAGMENT:
      shader->fs.earlyzs_lut = pan_earlyzs_analyze(&shader->info, PAN_ARCH);
      break;

   default:
      break;
   }

   return VK_SUCCESS;
}

#if PAN_ARCH >= 9
static enum mali_flush_to_zero_mode
shader_ftz_mode(struct panvk_shader_variant *shader)
{
   if (shader->info.ftz_fp32) {
      if (shader->info.ftz_fp16)
         return MALI_FLUSH_TO_ZERO_MODE_ALWAYS;
      else
         return MALI_FLUSH_TO_ZERO_MODE_DX11;
   } else {
      /* We don't have a "flush FP16, preserve FP32" mode, but APIs
       * should not be able to generate that.
       */
      assert(!shader->info.ftz_fp16 && !shader->info.ftz_fp32);
      return MALI_FLUSH_TO_ZERO_MODE_PRESERVE_SUBNORMALS;
   }
}
#endif

static VkResult
panvk_shader_upload(struct panvk_device *dev,
                    struct panvk_shader_variant *shader,
                    const VkAllocationCallbacks *pAllocator)
{
   shader->code_mem = (struct panvk_priv_mem){0};

#if PAN_ARCH < 9
   shader->data_mem = (struct panvk_priv_mem){0};
   shader->rsd = (struct panvk_priv_mem){0};
#else
   shader->spd = (struct panvk_priv_mem){0};
#endif

   if (!shader->bin_size)
      return VK_SUCCESS;

   shader->code_mem = panvk_pool_upload_aligned(
      &dev->mempools.exec, shader->bin_ptr, shader->bin_size, 128);
   if (!panvk_priv_mem_check_alloc(shader->code_mem))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

#if PAN_ARCH >= 9
   /* The inline constant pool is addressed as PC plus a 32-bit offset */
   ASSERTED uint64_t code_dev_addr = panvk_priv_mem_dev_addr(shader->code_mem);
   assert(code_dev_addr >> 32 == (code_dev_addr + shader->bin_size - 1) >> 32);
#endif

#if PAN_ARCH < 9
   if (shader->data_size) {
      shader->data_mem = panvk_pool_upload_aligned(
         &dev->mempools.rw, shader->data_ptr, shader->data_size, 64);
      if (!panvk_priv_mem_check_alloc(shader->data_mem))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   if (shader->info.stage == MESA_SHADER_FRAGMENT)
      return VK_SUCCESS;

   shader->rsd = panvk_pool_alloc_desc(&dev->mempools.rw, RENDERER_STATE);
   if (!panvk_priv_mem_check_alloc(shader->rsd))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   panvk_priv_mem_write_desc(shader->rsd, 0, RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(&shader->info,
                             panvk_shader_variant_get_dev_addr(shader), &cfg);
   }
#else
   if (shader->info.stage != MESA_SHADER_VERTEX) {
      shader->spd = panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_check_alloc(shader->spd))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      panvk_priv_mem_write_desc(shader->spd, 0, SHADER_PROGRAM, cfg) {
         cfg.stage = pan_shader_stage(&shader->info);

         if (cfg.stage == MALI_SHADER_STAGE_FRAGMENT)
            cfg.fragment_coverage_bitmask_type = MALI_COVERAGE_BITMASK_TYPE_GL;
#if PAN_ARCH < 12
         else if (cfg.stage == MALI_SHADER_STAGE_VERTEX)
            cfg.vertex_warp_limit = MALI_WARP_LIMIT_HALF;
#endif

         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader);
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);

         if (cfg.stage == MALI_SHADER_STAGE_FRAGMENT)
            cfg.requires_helper_threads = shader->info.contains_barrier;
      }
   } else {
#if PAN_ARCH >= 12
      shader->spds.all_points =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_check_alloc(shader->spds.all_points))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      panvk_priv_mem_write_desc(shader->spds.all_points, 0, SHADER_PROGRAM,
                                cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader);
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }

      shader->spds.all_triangles =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_check_alloc(shader->spds.all_triangles))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      panvk_priv_mem_write_desc(shader->spds.all_triangles, 0, SHADER_PROGRAM,
                                cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader) +
                      shader->info.vs.no_psiz_offset;
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }
#else
      shader->spds.pos_points =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_check_alloc(shader->spds.pos_points))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      panvk_priv_mem_write_desc(shader->spds.pos_points, 0, SHADER_PROGRAM,
                                cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.vertex_warp_limit = MALI_WARP_LIMIT_HALF;
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader);
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }

      shader->spds.pos_triangles =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_check_alloc(shader->spds.pos_triangles))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      panvk_priv_mem_write_desc(shader->spds.pos_triangles, 0, SHADER_PROGRAM,
                                cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.vertex_warp_limit = MALI_WARP_LIMIT_HALF;
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader) +
                      shader->info.vs.no_psiz_offset;
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }

      if (shader->info.vs.secondary_enable) {
         shader->spds.var =
            panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
         if (!panvk_priv_mem_check_alloc(shader->spds.var))
            return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

         panvk_priv_mem_write_desc(shader->spds.var, 0, SHADER_PROGRAM, cfg) {
            unsigned work_count = shader->info.vs.secondary_work_reg_count;

            cfg.stage = pan_shader_stage(&shader->info);
            cfg.vertex_warp_limit = MALI_WARP_LIMIT_FULL;
            cfg.register_allocation = pan_register_allocation(work_count);
            cfg.binary = panvk_shader_variant_get_dev_addr(shader) +
                         shader->info.vs.secondary_offset;
            cfg.preload.r48_r63 = (shader->info.vs.secondary_preload >> 48);
            cfg.flush_to_zero_mode = shader_ftz_mode(shader);
         }
      }
#endif
   }
#endif

   return VK_SUCCESS;
}

static void
panvk_shader_variant_destroy(struct panvk_shader_variant *shader)
{
   free((void *)shader->asm_str);
   ralloc_free((void *)shader->nir_str);

   panvk_pool_free_mem(&shader->code_mem);

#if PAN_ARCH < 9
   panvk_pool_free_mem(&shader->data_mem);
   panvk_pool_free_mem(&shader->rsd);
#else
   if (shader->info.stage != MESA_SHADER_VERTEX) {
      panvk_pool_free_mem(&shader->spd);
   } else {
#if PAN_ARCH >= 12
      panvk_pool_free_mem(&shader->spds.all_points);
      panvk_pool_free_mem(&shader->spds.all_triangles);
#else
      panvk_pool_free_mem(&shader->spds.var);
      panvk_pool_free_mem(&shader->spds.pos_points);
      panvk_pool_free_mem(&shader->spds.pos_triangles);
#endif
   }
#endif

#if PAN_ARCH < 9
   free((void *)shader->data_ptr);
#endif

   if (shader->own_bin)
      free((void *)shader->bin_ptr);
}

static void
panvk_shader_destroy(struct vk_device *vk_dev, struct vk_shader *vk_shader,
                     const VkAllocationCallbacks *pAllocator)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   panvk_shader_foreach_variant(shader, variant) {
      panvk_shader_variant_destroy(variant);
   }

#if PAN_ARCH < 9
   panvk_pool_free_mem(&shader->desc_info.others.map);
#endif

   vk_shader_free(vk_dev, pAllocator, &shader->vk);
}

static const struct vk_shader_ops panvk_shader_ops;

static VkResult
panvk_compile_shader(struct panvk_device *dev,
                     struct vk_shader_compile_info *info,
                     const struct vk_graphics_pipeline_state *state,
                     const struct pan_varying_layout *vs_varying_layout,
                     const uint32_t *noperspective_varyings,
                     const VkAllocationCallbacks *pAllocator,
                     struct vk_shader **shader_out)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   struct panvk_shader *shader;
   VkResult result;

   size_t size =
      sizeof(struct panvk_shader) + sizeof(struct panvk_shader_variant) *
                                       panvk_shader_num_variants(info->stage);
   shader = vk_shader_zalloc(&dev->vk, &panvk_shader_ops, info->stage,
                             pAllocator, size);
   if (shader == NULL)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

#if PAN_ARCH >= 10
   if (info->stage == MESA_SHADER_VERTEX) {
      shader->gpu_prerast.io.abi_version = PANVK_GPU_PRERAST_ABI_VERSION;
      shader->gpu_prerast.io.clip_distance_array_size =
         info->nir->info.clip_distance_array_size;
      shader->gpu_prerast.io.cull_distance_array_size =
         info->nir->info.cull_distance_array_size;
      shader->gpu_prerast.has_side_effects = info->nir->info.writes_memory;
   }
#endif

   nir_variable_mode robust_modes = 0;
   if (info->robustness->uniform_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT)
      robust_modes |= nir_var_mem_ubo;
   if (info->robustness->storage_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT)
      robust_modes |= nir_var_mem_ssbo;

   struct pan_compile_inputs inputs = {
      .arch = PAN_ARCH,
      .model = phys_dev->model,
      .view_mask = (state && state->rp) ? state->mv->view_mask : 0,
      .robust_modes = robust_modes,
      .robust_descriptors = dev->vk.enabled_features.nullDescriptor,
   };

   switch (info->stage) {
   case MESA_SHADER_VERTEX: {
#if PAN_ARCH >= 10
      /* Lowered/passthrough variants only exist on the gpu_prerast path. */
      const bool dyn_polygon_mode =
         !state || !state->rs ||
         BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_RS_POLYGON_MODE);
      const bool want_prerast =
         PANVK_DEBUG(GPU_PRERAST) ||
         (info->next_stage_mask & VK_SHADER_STAGE_GEOMETRY_BIT) ||
         (dev->vk.enabled_features.fillModeNonSolid &&
          (dyn_polygon_mode ||
           state->rs->polygon_mode != VK_POLYGON_MODE_FILL));
      const enum panvk_vs_variant last_variant =
         want_prerast ? PANVK_VS_VARIANT_GPU_PASSTHROUGH : PANVK_VS_VARIANT_HW;
      struct panvk_gpu_prerast_out_types out_types = {0};
#else
      const enum panvk_vs_variant last_variant = PANVK_VS_VARIANT_HW;
#endif
      for (enum panvk_vs_variant v = 0; v <= last_variant; v++) {
         struct panvk_shader_variant *variant = &shader->variants[v];

         /* Each variant gets its own NIR. To save an extra clone, we use the
          * original NIR for the last stage.
          */
#if PAN_ARCH >= 10
         const bool clone_nir = true;
         nir_shader *nir = v == PANVK_VS_VARIANT_GPU_PASSTHROUGH
                              ? panvk_gpu_prerast_passthrough_nir(
                                   info->nir,
                                   shader->gpu_prerast.io.outputs_written,
                                   &out_types,
                                   &shader->gpu_prerast.io.varyings)
                              : nir_shader_clone(NULL, info->nir);
#else
         const bool clone_nir = (v != last_variant);
         nir_shader *nir =
            clone_nir ? nir_shader_clone(NULL, info->nir) : info->nir;
#endif

         bool gpu_lower = false;
#if PAN_ARCH >= 10
         gpu_lower = v == PANVK_VS_VARIANT_GPU_LOWERED;
         if (gpu_lower) {
            nir_assign_io_var_locations(nir, nir_var_shader_out);
            nir->xfb_info = NULL;
         }
#endif

         panvk_lower_nir(dev, nir, info->set_layout_count,
                         info->set_layouts, info->robustness,
                          state, &shader->desc_info, false);

#if PAN_ARCH >= 10 && PAN_ARCH < 14
         if (inputs.view_mask) {
            nir_lower_multiview_options options = {
               .view_mask = inputs.view_mask,
               .allowed_per_view_outputs = ~0
            };

            /* The only case where this should fail is with memory/image
             * writes, which we don't support in vertex shaders
             */
            assert(nir_can_lower_multiview(nir, options));
            NIR_PASS(_, nir, nir_lower_multiview, options);

            /* Pull output writes out of the loop and give them constant
             * offsets for pan_lower_store_components
             */
            NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
                     nir_shader_get_entrypoint(nir), nir_var_shader_out);
            NIR_PASS(_, nir, nir_lower_global_vars_to_local);
            NIR_PASS(_, nir, nir_split_var_copies);
         }
#endif

         /* We need the driver_location to match the vertex attribute
          * location, so we can use the attribute layout described by
          * vk_vertex_input_state where there are holes in the attribute
          * locations.
          */
#if PAN_ARCH >= 10
         if (v != PANVK_VS_VARIANT_GPU_PASSTHROUGH) {
#endif
            nir_foreach_shader_in_variable(var, nir) {
               assert(var->data.location >= VERT_ATTRIB_GENERIC0 &&
                      var->data.location <= VERT_ATTRIB_GENERIC15);
               var->data.driver_location =
                  var->data.location - VERT_ATTRIB_GENERIC0;
            }
#if PAN_ARCH >= 10
         }
         if (!gpu_lower && v != PANVK_VS_VARIANT_GPU_PASSTHROUGH) {
#endif
            nir_assign_io_var_locations(nir, nir_var_shader_out);
#if PAN_ARCH >= 10
         }
         if (v != PANVK_VS_VARIANT_GPU_PASSTHROUGH) {
#endif
            panvk_lower_nir_io(nir);
#if PAN_ARCH >= 10
            if (gpu_lower) {
               /* outputs_written is only valid after panvk_lower_nir()
                * gathered info; info->nir may still report 0. The record
                * layout and the passthrough variant both key off this mask.
                */
               shader->gpu_prerast.io.outputs_written =
                  nir->info.outputs_written;
               shader->gpu_prerast.io.record_stride = MAX2(
                  ALIGN_POT(util_bitcount64(nir->info.outputs_written) * 16,
                            16),
                  16);
               nir_shader_intrinsics_pass(nir,
                                          panvk_gpu_prerast_collect_out_type,
                                          nir_metadata_all, &out_types);
               NIR_PASS(_, nir, pan_nir_lower_vs_inputs_poly, inputs.gpu_id);
               NIR_PASS(_, nir, panvk_gpu_prerast_lower_vs_outputs);
               panvk_gpu_prerast_emit_generated_records(nir);
               NIR_PASS(_, nir, poly_nir_lower_sw_vs);
               NIR_PASS(_, nir, poly_nir_lower_sysvals);
               NIR_PASS(_, nir, nir_shader_intrinsics_pass,
                        panvk_lower_gpu_prerast_sysval,
                        nir_metadata_control_flow, NULL);
               nir->info.stage = MESA_SHADER_COMPUTE;
               memset(&nir->info.cs, 0, sizeof(nir->info.cs));
               nir->info.workgroup_size[0] = 1;
               nir->info.workgroup_size[1] = 1;
               nir->info.workgroup_size[2] = 1;
            }
         }
#endif
         /* This somehow folds the location for multi-slot nir_load/nir_store */
         NIR_PASS(_, nir, nir_opt_constant_folding);

         struct pan_varying_layout varying_layout;
         if (v == PANVK_VS_VARIANT_HW) {
            pan_varying_collect_formats(&varying_layout, nir, PAN_ARCH);
            pan_build_varying_layout_compact(&varying_layout, nir, PAN_ARCH);
            inputs.varying_layout = &varying_layout;
#if PAN_ARCH >= 10
            shader->gpu_prerast.io.varyings = varying_layout;
            shader->gpu_prerast.io.varying_count = nir->num_outputs;
#endif
         }

#if PAN_ARCH >= 10
         if (v == PANVK_VS_VARIANT_GPU_PASSTHROUGH)
            inputs.varying_layout = &shader->gpu_prerast.io.varyings;
#endif

         variant->own_bin = true;

         result = panvk_compile_nir(dev, nir, info->flags, &inputs, state,
                                    noperspective_varyings,
                                    &shader->desc_info, variant);

#if PAN_ARCH >= 10
         if (result == VK_SUCCESS && gpu_lower)
            shader->gpu_prerast.lowered_vs_compiled = true;
         if (result == VK_SUCCESS && v == PANVK_VS_VARIANT_GPU_PASSTHROUGH)
            shader->gpu_prerast.passthrough_vs_compiled = true;
#endif

         /* If we cloned, it's our job to clean up */
         if (clone_nir)
            ralloc_free(nir);

         if (result != VK_SUCCESS) {
            panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
            return result;
         }
      }
#if PAN_ARCH >= 10
      if (want_prerast && shader->gpu_prerast.lowered_vs_compiled &&
          shader->gpu_prerast.passthrough_vs_compiled &&
          (shader->gpu_prerast.io.outputs_written & VARYING_BIT_POS)) {
         struct panvk_shader_variant *variant =
            &shader->variants[PANVK_VS_VARIANT_GPU_POLYGON];
         nir_shader *nir = panvk_gpu_prerast_polygon_nir(
            info->nir->options, shader->gpu_prerast.io.record_stride,
            util_bitcount64(shader->gpu_prerast.io.outputs_written &
                            BITFIELD64_MASK(VARYING_SLOT_POS)) * 16);
         struct pan_compile_inputs poly_inputs = inputs;
         poly_inputs.varying_layout = NULL;
         variant->own_bin = true;
         result = panvk_compile_nir(dev, nir, info->flags, &poly_inputs, state,
                                    NULL, &shader->desc_info, variant);
         ralloc_free(nir);
         if (result != VK_SUCCESS) {
            panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
            return result;
         }
         shader->gpu_prerast.polygon_compiled = true;
      }
#endif
      break;
   }

#if PAN_ARCH >= 10
   case MESA_SHADER_GEOMETRY: {
      struct panvk_gpu_prerast_gs_info *gs = &shader->gpu_prerast.gs;
      struct panvk_gpu_prerast_out_types out_types = {0};
      nir_shader *nir = info->nir;

      shader->gpu_prerast.io.abi_version = PANVK_GPU_PRERAST_ABI_VERSION;
      shader->gpu_prerast.has_side_effects = nir->info.writes_memory;

      panvk_lower_nir(dev, nir, info->set_layout_count, info->set_layouts,
                      info->robustness, state, &shader->desc_info, false);
      nir_assign_io_var_locations(nir, nir_var_shader_out);
      panvk_lower_nir_io(nir);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

      gs->outputs_written = nir->info.outputs_written;
      gs->record_stride =
         MAX2(ALIGN_POT(util_bitcount64(gs->outputs_written) * 16, 16), 16);
      gs->max_vertices = MAX2(nir->info.gs.vertices_out, 1);
      gs->index_stride = ALIGN_POT(2 * gs->max_vertices, 4);
      gs->invocations = MAX2(nir->info.gs.invocations, 1);
      gs->input_prim = nir->info.gs.input_primitive;
      gs->output_prim = nir->info.gs.output_primitive;
      nir_shader_intrinsics_pass(nir, panvk_gpu_prerast_collect_out_type,
                                 nir_metadata_all, &out_types);

      struct panvk_shader_variant *pt =
         &shader->variants[PANVK_GS_VARIANT_PASSTHROUGH];
      nir_shader *pt_nir = panvk_gpu_prerast_gs_passthrough_nir(
         info->nir->options, gs, &out_types);
      struct panvk_shader_desc_info pt_desc_info = {0};
      panvk_lower_nir(dev, pt_nir, info->set_layout_count, info->set_layouts,
                      info->robustness, state, &pt_desc_info, false);
      NIR_PASS(_, pt_nir, nir_opt_constant_folding);
      struct pan_varying_layout pt_layout;
      pan_varying_collect_formats(&pt_layout, pt_nir, inputs.gpu_id);
      pan_build_varying_layout_compact(&pt_layout, pt_nir, inputs.gpu_id);
      struct pan_compile_inputs pt_inputs = inputs;
      pt_inputs.varying_layout = &pt_layout;
      pt->own_bin = true;
      result = panvk_compile_nir(dev, pt_nir, info->flags, &pt_inputs, state,
                                 noperspective_varyings, &pt_desc_info, pt);
      ralloc_free(pt_nir);
      if (result != VK_SUCCESS) {
         panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
      gs->passthrough_compiled = true;

      panvk_gpu_prerast_lower_gs(nir, gs);

      struct panvk_shader_variant *kernel =
         &shader->variants[PANVK_GS_VARIANT_KERNEL];
      struct pan_compile_inputs k_inputs = inputs;
      k_inputs.varying_layout = NULL;
      k_inputs.view_mask = 0;
      kernel->own_bin = true;
      result = panvk_compile_nir(dev, nir, info->flags, &k_inputs, state, NULL,
                                 &shader->desc_info, kernel);
      if (result != VK_SUCCESS) {
         panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
      gs->kernel_compiled = true;
      break;
   }
#endif

   case MESA_SHADER_FRAGMENT: {
      struct panvk_shader_variant *variant =
         (struct panvk_shader_variant *)panvk_shader_only_variant(shader);

      nir_shader *nir = info->nir;

      if (state && state->ms && state->ms->sample_shading_enable)
         nir->info.fs.uses_sample_shading = true;

      uint64_t prev_dist_outputs =
         VARYING_BIT_CLIP_DIST0 | VARYING_BIT_CLIP_DIST1 |
         VARYING_BIT_CULL_DIST0 | VARYING_BIT_CULL_DIST1;
      if (vs_varying_layout) {
         prev_dist_outputs = 0;
         for (unsigned i = 0; i < vs_varying_layout->count; i++) {
            const struct pan_varying_slot *slot =
               pan_varying_layout_slot_at(vs_varying_layout, i);
            if (slot)
               prev_dist_outputs |= BITFIELD64_BIT(slot->location);
         }
      }
      NIR_PASS(_, nir, panvk_lower_clip_cull_fs, prev_dist_outputs);

      /* We need to lower input attachments before we lower descriptors */
      NIR_PASS(_, nir, panvk_per_arch(nir_lower_input_attachment_loads),
               state, &variant->fs.input_attachment_read);

      NIR_PASS(_, nir, panvk_nir_lower_tile_image,
               &variant->fs.tile_image_color_read,
               &variant->fs.tile_image_z_read,
               &variant->fs.tile_image_s_read);

      /* Lower input intrinsics for fragment shaders early to get the max
       * number of varying loads, as this number is required during descriptor
       * lowering for v9+.
       */
      nir_assign_io_var_locations(nir, nir_var_shader_in);

      /* VS (if known) decides the memory layout */
      inputs.varying_layout = vs_varying_layout;

      panvk_lower_nir(dev, nir, info->set_layout_count, info->set_layouts,
                      info->robustness, state, &shader->desc_info, false);

      nir_assign_io_var_locations(nir, nir_var_shader_out);
      panvk_lower_nir_io(nir);

      /* Lower FS outputs now so that we can lower load_blend_descriptor_pan
       * to a driver-provided FAU instead of using the blend descriptors
       * uploaded by the hardware.  See panvk_vX_blend.c for details.
       */
      NIR_PASS(_, nir, pan_nir_lower_fs_outputs, false,
               0 /* fragcolor_nr_cbufs */);

      variant->own_bin = true;

      result = panvk_compile_nir(dev, nir, info->flags, &inputs, state,
                                 noperspective_varyings,
                                 &shader->desc_info, variant);
      if (result != VK_SUCCESS) {
         panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }

      #if PAN_ARCH >= 9
      /* LD_VAR_BUF[_IMM] has a fixed-size offset, when shaders overflow
       * that they fall back to LD_VAR[_IMM] and require descriptors.
       * TODO: We could only emit descriptors that overflow the offset,
       *       saving a bit of space.
       */
      shader->desc_info.fs_varying_attr_desc_count =
         variant->info.bifrost.uses_ld_var ? nir->num_inputs : 0;
      #endif

      break;
   }

   case MESA_SHADER_COMPUTE: {
      struct panvk_shader_variant *variant =
         (struct panvk_shader_variant *)panvk_shader_only_variant(shader);

      nir_shader *nir = info->nir;

      bool allow_merging_workgroups = false;
#if PAN_ARCH >= 9
      nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
      allow_merging_workgroups =
         valhall_can_merge_workgroups(nir);

      /* With merged workgroups, we need to use different divergence analysis
       * options to take into account that threads from different workgroups
       * may be in the same subgroup */
      if (allow_merging_workgroups) {
         nir->options = pan_get_nir_shader_compiler_options(
            PAN_ARCH, MESA_SHADER_COMPUTE, true);
         /* Invalidate the old divergence analysis */
         nir_foreach_function_impl(impl, nir)
            nir_progress(true, impl, ~nir_metadata_divergence);
      }
#endif

      panvk_lower_nir(dev, nir, info->set_layout_count, info->set_layouts,
                      info->robustness, state, &shader->desc_info,
                      allow_merging_workgroups);

      variant->own_bin = true;

      result = panvk_compile_nir(dev, nir, info->flags, &inputs, state,
                                 noperspective_varyings,
                                 &shader->desc_info, variant);
      if (result != VK_SUCCESS) {
         panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
      variant->info.cs.allow_merging_workgroups = allow_merging_workgroups;
      break;
   }

   default:
      UNREACHABLE("Unknown shader stage");
   }

   panvk_shader_foreach_variant(shader, variant) {
      result = panvk_shader_upload(dev, variant, pAllocator);
      if (result != VK_SUCCESS) {
         panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
   }

   *shader_out = &shader->vk;

   return result;
}

VkResult
panvk_per_arch(create_shader_from_binary)(struct panvk_device *dev,
                                          const struct pan_shader_info *info,
                                          struct pan_compute_dim local_size,
                                          const void *bin_ptr, size_t bin_size,
                                          struct panvk_shader **shader_out)
{
   struct panvk_shader *shader;
   VkResult result;

   size_t size =
      sizeof(struct panvk_shader) + sizeof(struct panvk_shader_variant) *
                                       panvk_shader_num_variants(info->stage);
   shader = vk_shader_zalloc(&dev->vk, &panvk_shader_ops, info->stage,
                             &dev->vk.alloc, size);
   if (shader == NULL)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(panvk_shader_num_variants(info->stage) == 1);

   struct panvk_shader_variant *variant = &shader->variants[0];
   variant->info = *info;
   variant->cs.local_size = local_size;
   variant->bin_ptr = bin_ptr;
   variant->bin_size = bin_size;
   variant->own_bin = false;
   variant->nir_str = NULL;
   variant->asm_str = NULL;

   result = panvk_shader_upload(dev, variant, &dev->vk.alloc);

   if (result != VK_SUCCESS) {
      panvk_shader_destroy(&dev->vk, &shader->vk, &dev->vk.alloc);
      return result;
   }

   *shader_out = shader;

   return result;
}

static VkResult
compile_shaders(struct vk_device *vk_dev, uint32_t shader_count,
                struct vk_shader_compile_info *infos,
                const struct vk_graphics_pipeline_state *state,
                const struct vk_features *enabled_features,
                const VkAllocationCallbacks *pAllocator,
                struct vk_shader **shaders_out)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   bool use_static_noperspective = false;
   uint32_t noperspective_varyings = 0;
   VkResult result;
   int32_t i;

   /* If we are linking VS and FS, we can use the static interpolation
    * qualifiers from the FS in the VS.  Vulkan runtime passes us shaders in
    * stage order, so the FS will always be last if it exists.
    */
   if (infos[shader_count - 1].nir->info.stage == MESA_SHADER_FRAGMENT) {
      nir_shader *nir = infos[shader_count - 1].nir;
      noperspective_varyings = pan_nir_collect_noperspective_varyings_fs(nir);
      use_static_noperspective = true;
   }

   /* Vulkan runtime passes us shaders in stage order */
   for (i = 0; i < shader_count; i++) {
      const uint32_t *noperspective_varyings_ptr =
         use_static_noperspective ? &noperspective_varyings : NULL;

      /* For fragment shaders, Look at the previous stage to see if we can
       * find a varying layout we can use instead of making up our own.
       */
      const struct pan_varying_layout *vs_varying_layout = NULL;
      if (infos[i].stage == MESA_SHADER_FRAGMENT && i > 0 &&
          infos[i - 1].next_stage_mask == VK_SHADER_STAGE_FRAGMENT_BIT) {
         struct panvk_shader *prev_shader =
            container_of(shaders_out[i - 1], struct panvk_shader, vk);

         /* The last geometry stage before the FS will always have a HW vertex
          * shader variant.
          */
         panvk_shader_foreach_variant(prev_shader, variant) {
            if (variant->info.stage == MESA_SHADER_VERTEX) {
               vs_varying_layout = &variant->info.varyings.formats;
               pan_varying_layout_require_layout(vs_varying_layout);
               break;
            }
         }
      }

      result = panvk_compile_shader(dev, &infos[i], state, vs_varying_layout,
                                    noperspective_varyings_ptr, pAllocator,
                                    &shaders_out[i]);

      if (result != VK_SUCCESS)
         goto err_cleanup;

      /* Clean up NIR for the current shader */
      ralloc_free(infos[i].nir);
   }

   /* TODO: If we get multiple shaders here, we can perform part of the link
    * logic at compile time. */

   return VK_SUCCESS;

err_cleanup:
   /* Clean up all the shaders before this point */
   for (int32_t j = 0; j < i; j++)
      panvk_shader_destroy(&dev->vk, shaders_out[j], pAllocator);

   /* Clean up all the NIR from this point */
   for (int32_t j = i; j < shader_count; j++)
      ralloc_free(infos[j].nir);

   /* Memset the output array */
   memset(shaders_out, 0, shader_count * sizeof(*shaders_out));

   return result;
}

static simple_mtx_t compiler_mutex = SIMPLE_MTX_INITIALIZER;

void
panvk_per_arch(compiler_lock)(void)
{
   if (pan_will_dump_shaders(PAN_ARCH) || PANVK_DEBUG(NIR))
      simple_mtx_lock(&compiler_mutex);
}

void
panvk_per_arch(compiler_unlock)(void)
{
   if (pan_will_dump_shaders(PAN_ARCH) || PANVK_DEBUG(NIR))
      simple_mtx_unlock(&compiler_mutex);
}

static VkResult
panvk_compile_shaders(struct vk_device *vk_dev, uint32_t shader_count,
                      struct vk_shader_compile_info *infos,
                      const struct vk_graphics_pipeline_state *state,
                      const struct vk_features *enabled_features,
                      const VkAllocationCallbacks *pAllocator,
                      struct vk_shader **shaders_out)
{
   panvk_per_arch(compiler_lock)();

   VkResult result = compile_shaders(vk_dev, shader_count, infos, state,
                                     enabled_features, pAllocator,
                                     shaders_out);

   panvk_per_arch(compiler_unlock)();

   return result;
}

static VkResult
shader_desc_info_deserialize(struct panvk_device *dev,
                             struct blob_reader *blob,
                             struct panvk_shader *shader)
{
   shader->desc_info.used_set_mask = blob_read_uint32(blob);

#if PAN_ARCH < 9
   shader->desc_info.dyn_ubos.count = blob_read_uint32(blob);
   if (shader->desc_info.dyn_ubos.count >
       ARRAY_SIZE(shader->desc_info.dyn_ubos.map))
      return panvk_error(shader, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
   blob_copy_bytes(blob, shader->desc_info.dyn_ubos.map,
                   sizeof(*shader->desc_info.dyn_ubos.map) *
                      shader->desc_info.dyn_ubos.count);
   shader->desc_info.dyn_ssbos.count = blob_read_uint32(blob);
   if (shader->desc_info.dyn_ssbos.count >
       ARRAY_SIZE(shader->desc_info.dyn_ssbos.map))
      return panvk_error(shader, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
   blob_copy_bytes(blob, shader->desc_info.dyn_ssbos.map,
                   sizeof(*shader->desc_info.dyn_ssbos.map) *
                      shader->desc_info.dyn_ssbos.count);

   uint32_t others_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      shader->desc_info.others.count[i] = blob_read_uint32(blob);
      if (shader->desc_info.others.count[i] > UINT32_MAX - others_count)
         return panvk_error(shader, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
      others_count += shader->desc_info.others.count[i];
   }

   if (others_count) {
      struct panvk_pool_alloc_info alloc_info = {
         .size = others_count * sizeof(uint32_t),
         .alignment = sizeof(uint32_t),
      };
      shader->desc_info.others.map =
         panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
      if (!panvk_priv_mem_check_alloc(shader->desc_info.others.map))
         return panvk_error(shader, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      panvk_priv_mem_write_array(shader->desc_info.others.map, 0, uint32_t,
                                 others_count, copy_table) {
         blob_copy_bytes(blob, copy_table, others_count * sizeof(*copy_table));
      }
   }
#else
   shader->desc_info.dyn_bufs.count = blob_read_uint32(blob);
   if (shader->desc_info.dyn_bufs.count >
       ARRAY_SIZE(shader->desc_info.dyn_bufs.map))
      return panvk_error(shader, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
   blob_copy_bytes(blob, shader->desc_info.dyn_bufs.map,
                   sizeof(*shader->desc_info.dyn_bufs.map) *
                      shader->desc_info.dyn_bufs.count);
   shader->desc_info.fs_varying_attr_desc_count = blob_read_uint32(blob);
#endif

   return VK_SUCCESS;
}

static VkResult
panvk_deserialize_shader_variant(struct vk_device *vk_dev,
                                 struct blob_reader *blob,
                                 const VkAllocationCallbacks *pAllocator,
                                 struct panvk_shader_variant *shader)
{
   struct panvk_device *device = to_panvk_device(vk_dev);
   struct pan_shader_info info;
   VkResult result;

   blob_copy_bytes(blob, &info, sizeof(info));
   if (blob->overrun)
      return panvk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader->info = info;
   blob_copy_bytes(blob, &shader->fau, sizeof(shader->fau));

   switch (shader->info.stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      blob_copy_bytes(blob, &shader->cs.local_size,
                      sizeof(shader->cs.local_size));
      break;

   case MESA_SHADER_FRAGMENT:
      shader->fs.earlyzs_lut = pan_earlyzs_analyze(&shader->info, PAN_ARCH);
      blob_copy_bytes(blob, &shader->fs.input_attachment_read,
                      sizeof(shader->fs.input_attachment_read));
      blob_copy_bytes(blob, &shader->fs.tile_image_color_read,
                      sizeof(shader->fs.tile_image_color_read));
      blob_copy_bytes(blob, &shader->fs.tile_image_z_read,
                      sizeof(shader->fs.tile_image_z_read));
      blob_copy_bytes(blob, &shader->fs.tile_image_s_read,
                      sizeof(shader->fs.tile_image_s_read));
      break;

   default:
      break;
   }

   shader->bin_size = blob_read_uint32(blob);

   if (blob->overrun)
      return panvk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader->bin_ptr = malloc(shader->bin_size);
   if (shader->bin_ptr == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   shader->own_bin = true;
   blob_copy_bytes(blob, (void *)shader->bin_ptr, shader->bin_size);

#if PAN_ARCH < 9
   shader->data_size = blob_read_uint32(blob);

   if (shader->data_size) {
      shader->data_ptr = malloc(shader->data_size);

      if (shader->data_ptr == NULL)
         return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      blob_copy_bytes(blob, (void *)shader->data_ptr, shader->data_size);
   }
#endif

   uint32_t nir_str_size = blob_read_uint32(blob);
   uint32_t asm_str_size = blob_read_uint32(blob);
   const char *nir_str = blob_read_bytes(blob, nir_str_size);
   const char *asm_str = blob_read_bytes(blob, asm_str_size);

   if (blob->overrun)
      return panvk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   if (nir_str_size > 0) {
      shader->nir_str = ralloc_strndup(NULL, nir_str, nir_str_size);
      if (shader->nir_str == NULL)
         return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   if (asm_str_size > 0) {
      shader->asm_str = strndup(asm_str, asm_str_size);
      if (shader->asm_str == NULL)
         return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   result = panvk_shader_upload(device, shader, pAllocator);

   if (result != VK_SUCCESS)
      return result;

   return result;
}

static VkResult
panvk_deserialize_shader(struct vk_device *vk_dev, struct blob_reader *blob,
                         uint32_t binary_version,
                         const VkAllocationCallbacks *pAllocator,
                         struct vk_shader **shader_out)
{
   struct panvk_device *device = to_panvk_device(vk_dev);
   struct panvk_shader *shader;
   VkResult result;

   mesa_shader_stage stage = blob_read_uint8(blob);
   if (blob->overrun)
      return vk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   size_t size =
      sizeof(struct panvk_shader) +
      sizeof(struct panvk_shader_variant) * panvk_shader_num_variants(stage);
   shader =
      vk_shader_zalloc(vk_dev, &panvk_shader_ops, stage, pAllocator, size);
   if (shader == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

#if PAN_ARCH >= 10
   blob_copy_bytes(blob, &shader->gpu_prerast, sizeof(shader->gpu_prerast));
   if (shader->gpu_prerast.io.abi_version != PANVK_GPU_PRERAST_ABI_VERSION)
      blob->overrun = true;
#endif

   result = shader_desc_info_deserialize(device, blob, shader);
   if (result != VK_SUCCESS) {
      panvk_shader_destroy(vk_dev, &shader->vk, pAllocator);
      return result;
   }

   panvk_shader_foreach_variant(shader, variant) {
      result = panvk_deserialize_shader_variant(vk_dev, blob, pAllocator,
                                                variant);
      if (result != VK_SUCCESS) {
         panvk_shader_destroy(vk_dev, &shader->vk, pAllocator);
         return result;
      }
   }

   *shader_out = &shader->vk;

   return VK_SUCCESS;
}

static void
shader_desc_info_serialize(struct blob *blob,
                           const struct panvk_shader *shader)
{
   blob_write_uint32(blob, shader->desc_info.used_set_mask);

#if PAN_ARCH < 9
   blob_write_uint32(blob, shader->desc_info.dyn_ubos.count);
   blob_write_bytes(blob, shader->desc_info.dyn_ubos.map,
                    sizeof(*shader->desc_info.dyn_ubos.map) *
                       shader->desc_info.dyn_ubos.count);
   blob_write_uint32(blob, shader->desc_info.dyn_ssbos.count);
   blob_write_bytes(blob, shader->desc_info.dyn_ssbos.map,
                    sizeof(*shader->desc_info.dyn_ssbos.map) *
                       shader->desc_info.dyn_ssbos.count);

   unsigned others_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      blob_write_uint32(blob, shader->desc_info.others.count[i]);
      others_count += shader->desc_info.others.count[i];
   }

   /* No need to wrap this one in panvk_priv_mem_readback(), because the
    * GPU is not supposed to touch it. */
   blob_write_bytes(blob,
                    panvk_priv_mem_host_addr(shader->desc_info.others.map),
                    sizeof(uint32_t) * others_count);
#else
   blob_write_uint32(blob, shader->desc_info.dyn_bufs.count);
   blob_write_bytes(blob, shader->desc_info.dyn_bufs.map,
                    sizeof(*shader->desc_info.dyn_bufs.map) *
                       shader->desc_info.dyn_bufs.count);
   blob_write_uint32(blob, shader->desc_info.fs_varying_attr_desc_count);
#endif
}

static bool
panvk_shader_serialize_variant(struct vk_device *vk_dev,
                               const struct panvk_shader_variant *shader,
                               struct blob *blob)
{
   blob_write_bytes(blob, &shader->info, sizeof(shader->info));
   blob_write_bytes(blob, &shader->fau, sizeof(shader->fau));

   switch (shader->info.stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      blob_write_bytes(blob, &shader->cs.local_size,
                       sizeof(shader->cs.local_size));
      break;

   case MESA_SHADER_FRAGMENT:
      blob_write_bytes(blob, &shader->fs.input_attachment_read,
                       sizeof(shader->fs.input_attachment_read));
      blob_write_bytes(blob, &shader->fs.tile_image_color_read,
                       sizeof(shader->fs.tile_image_color_read));
      blob_write_bytes(blob, &shader->fs.tile_image_z_read,
                       sizeof(shader->fs.tile_image_z_read));
      blob_write_bytes(blob, &shader->fs.tile_image_s_read,
                       sizeof(shader->fs.tile_image_s_read));
      break;

   default:
      break;
   }

   blob_write_uint32(blob, shader->bin_size);
   blob_write_bytes(blob, shader->bin_ptr, shader->bin_size);

#if PAN_ARCH < 9
   blob_write_uint32(blob, shader->data_size);
   blob_write_bytes(blob, shader->data_ptr, shader->data_size);
#endif

   /* Include the terminating NULL in the serialization */
   uint32_t nir_str_size = shader->nir_str ? strlen(shader->nir_str) + 1 : 0;
   uint32_t asm_str_size = shader->asm_str ? strlen(shader->asm_str) + 1 : 0;
   blob_write_uint32(blob, nir_str_size);
   blob_write_uint32(blob, asm_str_size);
   blob_write_bytes(blob, shader->nir_str, nir_str_size);
   blob_write_bytes(blob, shader->asm_str, asm_str_size);

   return !blob->out_of_memory;
}

static bool
panvk_shader_serialize(struct vk_device *vk_dev,
                       const struct vk_shader *vk_shader, struct blob *blob)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   blob_write_uint8(blob, vk_shader->stage);

#if PAN_ARCH >= 10
   blob_write_bytes(blob, &shader->gpu_prerast, sizeof(shader->gpu_prerast));
#endif

   shader_desc_info_serialize(blob, shader);

   panvk_shader_foreach_variant(shader, variant) {
      panvk_shader_serialize_variant(vk_dev, variant, blob);
   }

   return !blob->out_of_memory;
}

static VkResult
panvk_shader_get_executable_properties(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t *executable_count, VkPipelineExecutablePropertiesKHR *properties)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out, properties,
                          executable_count);

   panvk_shader_foreach_variant(shader, variant) {
      /* Ignore absent variants */
      if (variant->bin_size == 0)
         continue;

      const char *variant_name = panvk_shader_variant_name(shader, variant);
      const char *stage_name = _mesa_shader_stage_to_string(shader->vk.stage);

      vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props)
      {
         props->stages = mesa_to_vk_shader_stage(shader->vk.stage);
         props->subgroupSize = pan_subgroup_size(PAN_ARCH);

         if (variant_name != NULL) {
            VK_PRINT_STR(props->name, "%s %s", variant_name, stage_name);
            VK_PRINT_STR(props->description, "%s %s shader", variant_name,
                         stage_name);
         } else {
            VK_COPY_STR(props->name, stage_name);
            VK_PRINT_STR(props->description, "%s shader", stage_name);
         }
      }

      if (variant->info.stage == MESA_SHADER_VERTEX &&
          variant->info.vs.secondary_offset) {
         vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out,
                                  props)
         {
            props->stages = mesa_to_vk_shader_stage(shader->vk.stage);
            props->subgroupSize = pan_subgroup_size(PAN_ARCH);

            if (variant_name != NULL) {
               VK_PRINT_STR(props->name, "%s %s varying", variant_name,
                            stage_name);
               VK_PRINT_STR(props->description, "%s %s varying shader",
                            variant_name, stage_name);
            } else {
               VK_PRINT_STR(props->name, "%s varying", stage_name);
               VK_PRINT_STR(props->description, "%s varying shader",
                            stage_name);
            }
         }
      }
   }

   return vk_outarray_status(&out);
}

static const struct panvk_shader_variant *
get_variant_from_executable_index(struct panvk_shader *shader,
                                  uint32_t executable_index,
                                  bool *idvs_varying)
{
   *idvs_varying = false;
   uint32_t i = 0;

   panvk_shader_foreach_variant(shader, variant) {
      /* Ignore absent variants */
      if (variant->bin_size == 0)
         continue;

      if (i == executable_index)
         return variant;

      i++;

      /* VS variants that have a separate IDVS varying shader get two indices,
       * the first for the position shader and the second for the varying
       * shader
       */
      if (variant->info.stage == MESA_SHADER_VERTEX &&
          variant->info.vs.secondary_offset) {
         if (i == executable_index) {
            *idvs_varying = true;
            return variant;
         }

         i++;
      }
   }

   return NULL;
}

static VkResult
panvk_shader_get_executable_statistics(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *statistic_count,
   VkPipelineExecutableStatisticKHR *statistics)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   bool needs_vary = false;

   const struct panvk_shader_variant *variant =
      get_variant_from_executable_index(shader, executable_index, &needs_vary);
   assert(variant != NULL);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out, statistics,
                          statistic_count);

   const struct pan_stats *stats =
      needs_vary ? &variant->info.stats_idvs_varying : &variant->info.stats;

   vk_add_pan_stats(out, stats);
   return vk_outarray_status(&out);
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR *ir,
              const char *data)
{
   ir->isText = VK_TRUE;

   size_t data_len = strlen(data) + 1;

   if (ir->pData == NULL) {
      ir->dataSize = data_len;
      return true;
   }

   strncpy(ir->pData, data, ir->dataSize);
   if (ir->dataSize < data_len)
      return false;

   ir->dataSize = data_len;
   return true;
}

static VkResult
panvk_shader_get_executable_internal_representations(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *internal_representation_count,
   VkPipelineExecutableInternalRepresentationKHR *internal_representations)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          internal_representations,
                          internal_representation_count);

   bool needs_vary = false;

   const struct panvk_shader_variant *variant =
      get_variant_from_executable_index(shader, executable_index, &needs_vary);
   assert(variant != NULL);

   /* XXX: Varying shader assembly */
   if (needs_vary)
      return vk_outarray_status(&out);

   bool incomplete_text = false;

   if (variant->nir_str != NULL) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR,
                               &out, ir)
      {
         VK_COPY_STR(ir->name, "NIR shader");
         VK_COPY_STR(ir->description,
                     "NIR shader before sending to the back-end compiler");
         if (!write_ir_text(ir, variant->nir_str))
            incomplete_text = true;
      }
   }

   if (variant->asm_str != NULL) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR,
                               &out, ir)
      {
         VK_COPY_STR(ir->name, "Assembly");
         VK_COPY_STR(ir->description, "Final Assembly");
         if (!write_ir_text(ir, variant->asm_str))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}

#if PAN_ARCH < 9
static mali_pixel_format
get_varying_format(mesa_shader_stage stage, gl_varying_slot loc,
                   enum pipe_format pfmt)
{
   switch (loc) {
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
#if PAN_ARCH <= 6
      return (MALI_R16F << 12) | pan_get_default_swizzle(1);
#else
      return (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_R000;
#endif
   case VARYING_SLOT_POS:
#if PAN_ARCH <= 6
      return (MALI_SNAP_4 << 12) | pan_get_default_swizzle(4);
#else
      return (MALI_SNAP_4 << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
#endif
   default:
      assert(pfmt != PIPE_FORMAT_NONE);
      return GENX(pan_format_from_pipe_format)(pfmt)->hw;
   }
}

static inline enum panvk_varying_buf_id
varying_buf_id(gl_varying_slot loc)
{
   switch (loc) {
   case VARYING_SLOT_POS:
      return PANVK_VARY_BUF_POSITION;
   case VARYING_SLOT_PSIZ:
      return PANVK_VARY_BUF_PSIZ;
   default:
      return PANVK_VARY_BUF_GENERAL;
   }
}

static mali_pixel_format
varying_format(gl_varying_slot loc, enum pipe_format pfmt)
{
   switch (loc) {
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
#if PAN_ARCH <= 6
      return (MALI_R16F << 12) | pan_get_default_swizzle(1);
#else
      return (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_R000;
#endif
   case VARYING_SLOT_POS:
#if PAN_ARCH <= 6
      return (MALI_SNAP_4 << 12) | pan_get_default_swizzle(4);
#else
      return (MALI_SNAP_4 << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
#endif
   default:
      return GENX(pan_format_from_pipe_format)(pfmt)->hw;
   }
}

static VkResult
emit_varying_attrs(struct panvk_pool *desc_pool,
                   const struct pan_varying_layout *varyings,
                   const unsigned *bit_sizes, const unsigned *buf_offsets,
                   struct panvk_priv_mem *mem)
{
   const unsigned varying_count = varyings->count;
   if (!varying_count) {
      *mem = (struct panvk_priv_mem){0};
      return VK_SUCCESS;
   }

   *mem = panvk_pool_alloc_desc_array(desc_pool, varying_count, ATTRIBUTE);
   if (!panvk_priv_mem_check_alloc(*mem))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   panvk_priv_mem_write_array(*mem, 0, struct mali_attribute_packed,
                              varying_count, attrs) {
      unsigned attr_idx = 0;

      for (unsigned i = 0; i < varying_count; i++) {
         pan_pack(&attrs[attr_idx++], ATTRIBUTE, cfg) {
            const struct pan_varying_slot *slot =
               pan_varying_layout_slot_at(varyings, i);
            if (!slot)
               continue;

            gl_varying_slot loc = slot->location;

            enum pipe_format pfmt = PIPE_FORMAT_NONE;
            bool is_hw = slot->section != PAN_VARYING_SECTION_GENERIC;
            bool is_present = is_hw || bit_sizes[loc] != 0;

            if (is_present && !is_hw) {
               nir_alu_type base_type =
                  nir_alu_type_get_base_type(slot->alu_type);
               pfmt =
                  pan_varying_format(base_type | bit_sizes[loc], slot->ncomps);
            }

            if (!is_present) {
#if PAN_ARCH >= 7
               cfg.format =
                  (MALI_CONSTANT << 12) | MALI_RGB_COMPONENT_ORDER_0000;
#else
               cfg.format = (MALI_CONSTANT << 12) | PAN_V6_SWIZZLE(0, 0, 0, 0);
#endif
            } else {
               cfg.buffer_index = varying_buf_id(loc);
               cfg.offset = buf_offsets[loc];
               cfg.format = varying_format(loc, pfmt);
            }
            cfg.offset_enable = false;
         }
      }
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(link_shaders)(struct panvk_pool *desc_pool,
                             const struct panvk_shader_variant *vs,
                             const struct panvk_shader_variant *fs,
                             struct panvk_shader_link *link)
{
   unsigned buf_strides[PANVK_VARY_BUF_MAX] = {0};
   unsigned buf_offsets[VARYING_SLOT_MAX] = {0};
   unsigned buf_sizes[VARYING_SLOT_MAX] = {0};

   assert(vs && vs->info.stage == MESA_SHADER_VERTEX);
   assert(!fs || fs->info.stage == MESA_SHADER_FRAGMENT);
   const struct pan_varying_layout *vs_layout = &vs->info.varyings.formats;
   pan_varying_layout_require_layout(vs_layout);

   /* Handle the position and point size buffers explicitly, as they are
    * passed through separate buffer pointers to the tiler job.
    */
   const struct pan_varying_slot *vs_pos_slot =
      pan_varying_layout_find_slot(vs_layout, VARYING_SLOT_POS);
   const struct pan_varying_slot *vs_psiz_slot =
      pan_varying_layout_find_slot(vs_layout, VARYING_SLOT_PSIZ);

   if (vs_pos_slot) {
      buf_strides[PANVK_VARY_BUF_POSITION] = sizeof(uint32_t) * 4;
      buf_sizes[VARYING_SLOT_POS] =
         nir_alu_type_get_type_size(vs_pos_slot->alu_type);
   }
   if (vs_psiz_slot) {
      buf_strides[PANVK_VARY_BUF_PSIZ] = sizeof(uint16_t);
      buf_sizes[VARYING_SLOT_PSIZ] =
         nir_alu_type_get_type_size(vs_psiz_slot->alu_type);
   }
   buf_strides[PANVK_VARY_BUF_GENERAL] = vs_layout->generic_size_B;

   /* When no fragment shader is present we can just ignore all
    * generic varyings writes.
    */
   const uint32_t fs_var_count = fs ? fs->info.varyings.formats.count : 0;
   for (uint32_t i = 0; i < fs_var_count; i++) {
      const struct pan_varying_slot *fs_var =
         pan_varying_layout_slot_at(&fs->info.varyings.formats, i);
      if (!fs_var)
         continue;

      const gl_varying_slot pos = fs_var->location;
      /* Skip special varyings. */
      if (fs_var->section != PAN_VARYING_SECTION_GENERIC)
         continue;

      /* Special buffers are handled explicitly before this loop, everything
       * else should be laid out in the general varying buffer.
       */
      assert(varying_buf_id(pos) == PANVK_VARY_BUF_GENERAL);

      const struct pan_varying_slot *vs_slot =
         pan_varying_layout_find_slot(vs_layout, fs_var->location);
      if (vs_slot) {
         buf_offsets[pos] = vs_slot->offset;
         buf_sizes[pos] = nir_alu_type_get_type_size(vs_slot->alu_type);
      }
   }

   panvk_shader_link_cleanup(link);
   VkResult result = emit_varying_attrs(desc_pool, vs_layout, buf_sizes,
                                        buf_offsets, &link->vs.attribs);
   if (result != VK_SUCCESS)
      return result;

   if (fs) {
      pan_varying_layout_require_format(&fs->info.varyings.formats);
      result = emit_varying_attrs(desc_pool, &fs->info.varyings.formats,
                                  buf_sizes, buf_offsets, &link->fs.attribs);
      if (result != VK_SUCCESS)
         return result;
   }

   memcpy(link->buf_strides, buf_strides, sizeof(link->buf_strides));
   return VK_SUCCESS;
}
#endif

static const struct vk_shader_ops panvk_shader_ops = {
   .destroy = panvk_shader_destroy,
   .serialize = panvk_shader_serialize,
   .get_executable_properties = panvk_shader_get_executable_properties,
   .get_executable_statistics = panvk_shader_get_executable_statistics,
   .get_executable_internal_representations =
      panvk_shader_get_executable_internal_representations,
};

static void
panvk_cmd_bind_shader(struct panvk_cmd_buffer *cmd, const mesa_shader_stage stage,
                      struct panvk_shader *shader)
{
   switch (stage) {
   case MESA_SHADER_COMPUTE:
      if (cmd->state.compute.shader != shader) {
         cmd->state.compute.shader = shader;
         compute_state_set_dirty(cmd, CS);
         compute_state_set_dirty(cmd, PUSH_UNIFORMS);
      }
      break;
   case MESA_SHADER_VERTEX:
      if (cmd->state.gfx.vs.shader != shader) {
         cmd->state.gfx.vs.shader = shader;
         gfx_state_set_dirty(cmd, VS);
         gfx_state_set_dirty(cmd, VS_PUSH_UNIFORMS);
      }
      break;
   case MESA_SHADER_FRAGMENT:
      if (cmd->state.gfx.fs.shader != shader) {
         cmd->state.gfx.fs.shader = shader;
         gfx_state_set_dirty(cmd, FS);
         gfx_state_set_dirty(cmd, FS_PUSH_UNIFORMS);
      }
      break;
#if PAN_ARCH >= 10
   case MESA_SHADER_GEOMETRY:
      if (cmd->state.gfx.gs.shader != shader) {
         cmd->state.gfx.gs.shader = shader;
         gfx_state_set_dirty(cmd, VS);
         gfx_state_set_dirty(cmd, VS_PUSH_UNIFORMS);
         gfx_state_set_dirty(cmd, FS);
      }
      break;
#endif
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
      assert(shader == NULL);
      break;
   default:
      assert(!"Unsupported stage");
      break;
   }
}

static void
panvk_cmd_bind_shaders(struct vk_command_buffer *vk_cmd, uint32_t stage_count,
                       const mesa_shader_stage *stages,
                       struct vk_shader **const shaders)
{
   struct panvk_cmd_buffer *cmd =
      container_of(vk_cmd, struct panvk_cmd_buffer, vk);

   for (uint32_t i = 0; i < stage_count; i++) {
      struct panvk_shader *shader =
         container_of(shaders[i], struct panvk_shader, vk);

      panvk_cmd_bind_shader(cmd, stages[i], shader);
   }
}

const struct vk_device_shader_ops panvk_per_arch(device_shader_ops) = {
   .get_nir_options = panvk_get_nir_options,
   .get_spirv_options = panvk_get_spirv_options,
   .preprocess_nir = panvk_preprocess_nir,
   .hash_state = panvk_hash_state,
   .compile = panvk_compile_shaders,
   .deserialize = panvk_deserialize_shader,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_bind_shaders = panvk_cmd_bind_shaders,
};

static void
panvk_internal_shader_destroy(struct vk_device *vk_dev,
                              struct vk_shader *vk_shader,
                              const VkAllocationCallbacks *pAllocator)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   struct panvk_internal_shader *shader =
      container_of(vk_shader, struct panvk_internal_shader, vk);

   panvk_pool_free_mem(&shader->code_mem);

#if PAN_ARCH < 9
   panvk_pool_free_mem(&shader->rsd);
#else
   panvk_pool_free_mem(&shader->spd);
#endif

   vk_shader_free(&dev->vk, pAllocator, &shader->vk);
}

static const struct vk_shader_ops panvk_internal_shader_ops = {
   .destroy = panvk_internal_shader_destroy,
};

VkResult
panvk_per_arch(create_internal_shader)(
   struct panvk_device *dev, nir_shader *nir,
   struct pan_compile_inputs *compiler_inputs,
   struct panvk_internal_shader **shader_out)
{
   struct panvk_internal_shader *shader =
      vk_shader_zalloc(&dev->vk, &panvk_internal_shader_ops, nir->info.stage,
                       NULL, sizeof(*shader));
   if (shader == NULL)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result;
   struct util_dynarray binary;

   panvk_per_arch(compiler_lock)();

   pan_postprocess_nir(nir, compiler_inputs, &shader->info);

   util_dynarray_init(&binary, nir);
   pan_shader_compile(nir, compiler_inputs, &binary, &shader->info);

   panvk_per_arch(compiler_unlock)();

   unsigned bin_size = util_dynarray_num_elements(&binary, uint8_t);
   if (bin_size) {
      shader->code_mem = panvk_pool_upload_aligned(&dev->mempools.exec,
                                                   binary.data, bin_size, 128);
      if (!panvk_priv_mem_check_alloc(shader->code_mem)) {
         result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_free_shader;
      }
   }

   *shader_out = shader;
   return VK_SUCCESS;

err_free_shader:
   vk_shader_free(&dev->vk, NULL, &shader->vk);
   return result;
}

VkResult panvk_per_arch(create_shader)(
   struct panvk_device *dev, nir_shader *nir, struct panvk_shader **shader_out)
{
   const struct vk_pipeline_robustness_state rs = {
      .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT,
      .storage_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .uniform_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .vertex_inputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
   };

   struct vk_shader_compile_info info = {
      .stage = nir->info.stage,
      .nir = nir,
      .robustness = &rs,
   };

   panvk_preprocess_nir(dev->vk.physical, nir, &rs);

   struct vk_shader *vk_shader;
   VkResult result = panvk_compile_shader(dev, &info, NULL, NULL, NULL, NULL,
                                          &vk_shader);
   if (result != VK_SUCCESS)
      return result;

   *shader_out = container_of(vk_shader, struct panvk_shader, vk);

   return VK_SUCCESS;
}
