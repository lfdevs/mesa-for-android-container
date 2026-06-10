/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_shader.h"

#include "kk_cmd_buffer.h"
#include "kk_debug.h"
#include "kk_descriptor_set_layout.h"
#include "kk_device.h"
#include "kk_format.h"
#include "kk_nir_lower_vbo.h"
#include "kk_physical_device.h"
#include "kk_sampler.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"
#include "kosmickrisp/compiler/nir_to_msl.h"

#include "nir_builder.h"
#include "nir_lower_blend.h"

#include "poly/nir/poly_nir.h"

#include "vk_blend.h"
#include "vk_format.h"
#include "vk_graphics_state.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_pipeline.h"

static const nir_shader_compiler_options *
kk_get_nir_options(struct vk_physical_device *vk_pdev, mesa_shader_stage stage,
                   UNUSED const struct vk_pipeline_robustness_state *rs)
{
   return &kk_nir_options;
}

static const struct vk_pipeline_robustness_state rs_all_supported = {
   .uniform_buffers =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2,
   .storage_buffers =
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2,
   .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2,
};

static struct spirv_to_nir_options
kk_get_spirv_options(struct vk_physical_device *vk_pdev,
                     UNUSED mesa_shader_stage stage,
                     const struct vk_pipeline_robustness_state *rs)
{
   if (KK_DEBUG(FORCE_ROBUSTNESS))
      rs = &rs_all_supported;
   return (struct spirv_to_nir_options){
      .environment = NIR_SPIRV_VULKAN,
      .ssbo_addr_format = kk_buffer_addr_format(rs->storage_buffers),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .ubo_addr_format = kk_buffer_addr_format(rs->uniform_buffers),
      .shared_addr_format = nir_address_format_32bit_offset,
      .min_ssbo_alignment = KK_MIN_SSBO_ALIGNMENT,
      .min_ubo_alignment = KK_MIN_UBO_ALIGNMENT,
   };
}

static void
kk_preprocess_nir(UNUSED struct vk_physical_device *vk_pdev, nir_shader *nir,
                  UNUSED const struct vk_pipeline_robustness_state *rs)
{
   /* Gather info before preprocess_nir but after some general lowering, so
    * inputs_read and system_values_read are accurately set.
    */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* nir_lower_io_to_temporaries is required before nir_lower_blend since the
    * blending pass sinks writes to the end of the block where we may have a
    * jump, which is illegal.
    */
   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir), nir_var_shader_out);

   msl_preprocess_nir(nir);

   /* Cannot be part of msl_preprocess_nir since clc does not expose
    * has_base_workgroup_id */
   nir_lower_compute_system_values_options csv_options = {
      .has_base_global_invocation_id = 0,
      .has_base_workgroup_id = true,
   };
   NIR_PASS(_, nir, nir_lower_compute_system_values, &csv_options);
}

static bool
has_static_depth_stencil_state(const struct vk_graphics_pipeline_state *state)
{
   if (!state->ds)
      return false;

   return !(
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_OP) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK) |
      BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK));
}

struct kk_vs_key {
   bool is_points;
   struct vk_vertex_input_state vi;
};

static void
kk_populate_vs_key(struct kk_vs_key *key,
                   const struct vk_graphics_pipeline_state *state)
{
   memset(key, 0, sizeof(*key));
   key->is_points =
      (state->ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
   key->vi = *state->vi;
}

struct kk_fs_key {
   VkFormat color_formats[MESA_VK_MAX_COLOR_ATTACHMENTS];
   struct vk_color_blend_state color_blend;
   struct vk_depth_stencil_state ds;
   uint32_t rasterization_samples;
   uint16_t static_sample_mask;
   bool sample_shading_enable;
   bool has_depth;
};

static void
kk_populate_fs_key(struct kk_fs_key *key,
                   const struct vk_graphics_pipeline_state *state)
{
   memset(key, 0, sizeof(*key));

   /* Required since we [de]serialize blend, and render target swizzle for
    * non-native formats */
   memcpy(key->color_formats, state->rp->color_attachment_formats,
          sizeof(key->color_formats));

   if (has_static_depth_stencil_state(state))
      key->ds = *(state->ds);

   /* Blend state gets [de]serialized, so we need to hash it */
   if (state->cb)
      key->color_blend = *(state->cb);

   if (state->ms) {
      key->rasterization_samples = state->ms->rasterization_samples;
      key->static_sample_mask = state->ms->sample_mask;
      key->sample_shading_enable = state->ms->sample_shading_enable;
   }

   /* Depth writes are removed unless there's an actual attachment */
   key->has_depth = state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED;
}

static void
kk_hash_graphics_state(struct vk_physical_device *device,
                       const struct vk_graphics_pipeline_state *state,
                       const struct vk_features *enabled_features,
                       VkShaderStageFlags stages, blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

   if (stages & VK_SHADER_STAGE_VERTEX_BIT) {
      struct kk_vs_key key;
      kk_populate_vs_key(&key, state);
      _mesa_blake3_update(&blake3_ctx, &key, sizeof(key));
   }

   if (stages & VK_SHADER_STAGE_FRAGMENT_BIT) {
      struct kk_fs_key key;
      kk_populate_fs_key(&key, state);
      _mesa_blake3_update(&blake3_ctx, &key, sizeof(key));

      _mesa_blake3_update(&blake3_ctx, &state->mv->view_mask,
                          sizeof(state->mv->view_mask));
   }

   _mesa_blake3_update(&blake3_ctx, &enabled_features->nullDescriptor,
                       sizeof(enabled_features->nullDescriptor));

   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size;
}

struct lower_ycbcr_state {
   uint32_t set_layout_count;
   struct vk_descriptor_set_layout *const *set_layouts;
};

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *_state, uint32_t set, uint32_t binding,
                        uint32_t array_index)
{
   const struct lower_ycbcr_state *state = _state;
   assert(set < state->set_layout_count);
   assert(state->set_layouts[set] != NULL);
   const struct kk_descriptor_set_layout *set_layout =
      vk_to_kk_descriptor_set_layout(state->set_layouts[set]);
   assert(binding < set_layout->binding_count);

   const struct kk_descriptor_set_binding_layout *bind_layout =
      &set_layout->binding[binding];

   if (bind_layout->immutable_samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->array_size - 1);

   const struct kk_sampler *sampler =
      bind_layout->immutable_samplers[array_index];

   return sampler && sampler->vk.ycbcr_conversion
             ? &sampler->vk.ycbcr_conversion->state
             : NULL;
}

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static bool
kk_nir_swizzle_fragment_output(nir_builder *b, nir_intrinsic_instr *intrin,
                               void *data)
{
   if (intrin->intrinsic != nir_intrinsic_store_output &&
       intrin->intrinsic != nir_intrinsic_load_output)
      return false;

   unsigned slot = nir_intrinsic_io_semantics(intrin).location;
   if (slot < FRAG_RESULT_DATA0)
      return false;

   const struct vk_graphics_pipeline_state *state =
      (const struct vk_graphics_pipeline_state *)data;
   VkFormat vk_format =
      state->rp->color_attachment_formats[slot - FRAG_RESULT_DATA0];
   if (vk_format == VK_FORMAT_UNDEFINED)
      return false;

   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   const struct kk_va_format *supported_format = kk_get_va_format(format);

   /* Check if we have to apply any swizzle */
   if (!supported_format->is_native) {
      unsigned channel_unswizzle[] = {
         supported_format->unswizzle.red, supported_format->unswizzle.green,
         supported_format->unswizzle.blue, supported_format->unswizzle.alpha};

      if (intrin->intrinsic == nir_intrinsic_store_output) {
         unsigned channel_swizzle[4] = {0u};
         for (uint32_t i = 0u; i < 4; ++i)
            channel_swizzle[channel_unswizzle[i]] = i;

         b->cursor = nir_before_instr(&intrin->instr);
         nir_def *to_replace = intrin->src[0].ssa;
         nir_def *swizzled = nir_swizzle(b, to_replace, channel_swizzle,
                                         to_replace->num_components);
         nir_src_rewrite(&intrin->src[0], swizzled);
      } else {
         b->cursor = nir_after_instr(&intrin->instr);
         nir_def *to_replace = &intrin->def;
         nir_def *swizzled = nir_swizzle(b, to_replace, channel_unswizzle,
                                         to_replace->num_components);
         nir_def_rewrite_uses_after(to_replace, swizzled);
      }
      return true;
   }

   return false;
}

static void
kk_lower_vs_vbo(nir_shader *nir, const struct vk_graphics_pipeline_state *state,
                const struct vk_pipeline_robustness_state *rs)
{
   assert(!(nir->info.inputs_read & BITFIELD64_MASK(VERT_ATTRIB_GENERIC0)) &&
          "Fixed-function attributes not used in Vulkan");
   NIR_PASS(_, nir, nir_recompute_io_bases, nir_var_shader_in);
   /* the shader_out portion of this is load-bearing even for tess eval */
   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   struct kk_attribute attributes[KK_MAX_ATTRIBS] = {};
   uint64_t attribs_read = nir->info.inputs_read >> VERT_ATTRIB_GENERIC0;
   u_foreach_bit(i, state->vi->attributes_valid) {
      const struct vk_vertex_attribute_state *attr = &state->vi->attributes[i];
      assert(state->vi->bindings_valid & BITFIELD_BIT(attr->binding));
      const struct vk_vertex_binding_state *binding =
         &state->vi->bindings[attr->binding];

      /* nir_assign_io_var_locations compacts vertex inputs, eliminating
       * unused inputs. We need to do the same here to match the locations.
       */
      unsigned slot = util_bitcount64(attribs_read & BITFIELD_MASK(i));
      attributes[slot].divisor = binding->divisor;
      attributes[slot].binding = attr->binding;
      attributes[slot].format = vk_format_to_pipe_format(attr->format);
      attributes[slot].buf = attr->binding;
      attributes[slot].instanced =
         binding->input_rate == VK_VERTEX_INPUT_RATE_INSTANCE;
   }
   bool robustness2 =
      rs->vertex_inputs ==
      VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2;
   NIR_PASS(_, nir, kk_nir_lower_vbo, attributes, robustness2);
}

/* Lowering for the stage which ends up as the vertex stage on hardware */
static void
kk_lower_hw_vs(nir_shader *nir, const struct vk_graphics_pipeline_state *state)
{
   bool is_point =
      nir->info.stage == MESA_SHADER_TESS_EVAL
         ? nir->info.tess.point_mode
         : state->ia->primitive_topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
   if (is_point)
      NIR_PASS(_, nir, msl_ensure_vertex_point_size_output);
   else
      nir_shader_intrinsics_pass(nir, msl_nir_vs_remove_point_size_write,
                                 nir_metadata_control_flow, NULL);

   NIR_PASS(_, nir, msl_ensure_vertex_position_output);
   NIR_PASS(_, nir, nir_lower_clip_halfz_dynamic);
   NIR_PASS(_, nir, msl_nir_vs_io_types);
}

static void
kk_lower_fs_blend(nir_shader *nir,
                  const struct vk_graphics_pipeline_state *state)
{
   nir_lower_blend_options opts = {
      .scalar_blend_const = false,
      .logicop_enable = state->cb->logic_op_enable,
      .logicop_func = vk_logic_op_to_pipe(state->cb->logic_op),
   };

   static_assert(ARRAY_SIZE(opts.rt) == 8, "max RTs out of sync");

   for (unsigned i = 0; i < ARRAY_SIZE(opts.rt); ++i) {
      const struct vk_color_blend_attachment_state *att =
         &state->cb->attachments[i];
      nir_lower_blend_rt *rt = &opts.rt[i];

      rt->format =
         vk_format_to_pipe_format(state->rp->color_attachment_formats[i]);
      rt->colormask = att->write_mask;

      if (!att->blend_enable) {
         rt->rgb.src_factor = rt->alpha.src_factor = PIPE_BLENDFACTOR_ONE;
         rt->rgb.dst_factor = rt->alpha.dst_factor = PIPE_BLENDFACTOR_ZERO;
         rt->rgb.func = rt->alpha.func = PIPE_BLEND_ADD;
      } else if (att->color_blend_op >= VK_BLEND_OP_ZERO_EXT) {
         rt->advanced_blend = true;
         rt->blend_mode = vk_advanced_blend_op_to_pipe(att->color_blend_op);
         rt->src_premultiplied = att->src_premultiplied;
         rt->dst_premultiplied = att->dst_premultiplied;
         rt->overlap = vk_blend_overlap_to_pipe(att->blend_overlap);

         /* Unused by runtime and not supported */
         assert(!att->clamp_results);
      } else {
         rt->rgb.src_factor =
            vk_blend_factor_to_pipe(att->src_color_blend_factor);
         rt->rgb.dst_factor =
            vk_blend_factor_to_pipe(att->dst_color_blend_factor);
         rt->rgb.func = vk_blend_op_to_pipe(att->color_blend_op);

         rt->alpha.src_factor =
            vk_blend_factor_to_pipe(att->src_alpha_blend_factor);
         rt->alpha.dst_factor =
            vk_blend_factor_to_pipe(att->dst_alpha_blend_factor);
         rt->alpha.func = vk_blend_op_to_pipe(att->alpha_blend_op);
      }
   }
   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_lower_blend, &opts);
}

static bool
lower_subpass_dim(nir_builder *b, nir_tex_instr *tex, UNUSED void *_data)
{
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS)
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   else if (tex->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS)
      tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
   else
      return false;

   return true;
}

static void
kk_lower_fs(struct kk_device *dev, nir_shader *nir,
            const struct vk_graphics_pipeline_state *state)
{
   nir->info.fs.uses_sample_shading |=
      state->ms && state->ms->sample_shading_enable;

   /* msl_nir_lower_sample_shading needs to go before blending since
    * nir_lower_blend will always set uses_sample_shading to true if there's any
    * output read. I believe we do not need to lower it always, that is why it
    * goes here but need to double check. */
   if (nir->info.fs.uses_sample_shading)
      NIR_PASS(_, nir, msl_nir_lower_sample_shading);

   if (state->cb)
      kk_lower_fs_blend(nir, state);

   enum pipe_format rts[MAX_DRAW_BUFFERS] = {PIPE_FORMAT_NONE};
   const struct vk_render_pass_state *rp = state->rp;
   for (uint32_t i = 0u; i < MAX_DRAW_BUFFERS; ++i)
      rts[i] = vk_format_to_pipe_format(rp->color_attachment_formats[i]);

   NIR_PASS(_, nir, msl_nir_fs_force_output_signedness, rts);

   if (state->rp->depth_attachment_format == VK_FORMAT_UNDEFINED ||
       nir->info.fs.early_fragment_tests)
      NIR_PASS(_, nir, nir_shader_intrinsics_pass,
               msl_nir_fs_remove_depth_write, nir_metadata_control_flow, NULL);

   /* Input attachments are treated as 2D textures. Fixes sampler dimension */
   NIR_PASS(_, nir, nir_shader_tex_pass, lower_subpass_dim, nir_metadata_all,
            NULL);

   /* Swizzle non-native formats' outputs */
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, kk_nir_swizzle_fragment_output,
            nir_metadata_control_flow, (void *)state);

   NIR_PASS(_, nir, msl_nir_fs_io_types);

   if (state->ms && state->ms->rasterization_samples &&
       state->ms->sample_mask != UINT16_MAX) {

      /* KK_WORKAROUND_7 */
      if (!(dev->disabled_workarounds & BITFIELD64_BIT(7))) {
         if (!nir->info.fs.early_fragment_tests) {
            nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
            nir_builder b = nir_builder_at(nir_after_impl(entrypoint));

            nir_def *sample_id = nir_load_sample_id(&b);
            nir_def *sample_bit = nir_ishl(&b, nir_imm_int(&b, 1), sample_id);
            nir_def *sample_mask_bit =
               nir_iand(&b, nir_load_sample_mask_in(&b), sample_bit);
            nir_discard_if(&b, nir_ieq_imm(&b, sample_mask_bit, 0u));
         }
      }
      NIR_PASS(_, nir, msl_lower_static_sample_mask, state->ms->sample_mask);
   }
   /* Check https://github.com/KhronosGroup/Vulkan-Portability/issues/54 for
    * explanation on why we need this. */
   else if (nir->info.fs.needs_full_quad_helper_invocations ||
            nir->info.fs.needs_coarse_quad_helper_invocations)
      NIR_PASS(_, nir, msl_lower_static_sample_mask, 0xFFFFFFFF);

   /* KK_WORKAROUND_5 */
   if (!(dev->disabled_workarounds & BITFIELD64_BIT(5)))
      NIR_PASS(_, nir, msl_nir_fake_guard_for_discards);
   /* KK_WORKAROUND_4 */
   if (!(dev->disabled_workarounds & BITFIELD64_BIT(4))) {
      NIR_PASS(_, nir, nir_lower_helper_writes, true);
      NIR_PASS(_, nir, nir_lower_is_helper_invocation);
   }
}

static void
kk_lower_nir(struct kk_device *dev, nir_shader *nir, bool emulated_stage,
             const struct vk_pipeline_robustness_state *rs,
             uint32_t set_layout_count,
             struct vk_descriptor_set_layout *const *set_layouts,
             const struct vk_graphics_pipeline_state *state)
{
   if (nir->info.io_lowered)
      return;

   if (KK_DEBUG(FORCE_ROBUSTNESS))
      rs = &rs_all_supported;

   const nir_opt_access_options access_options = {
      .is_vulkan = true,
   };
   NIR_PASS(_, nir, nir_opt_access, &access_options);

   /* Massage IO related variables to please Metal */
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_, nir, kk_nir_lower_vs_multiview, state->mv->view_mask);

      /* kk_nir_lower_vs_multiview may create a temporary array to assign the
       * correct view index. Since we don't handle derefs, we need to get rid of
       * them. */
      NIR_PASS(_, nir, nir_lower_vars_to_scratch, 0,
               glsl_get_natural_size_align_bytes,
               glsl_get_natural_size_align_bytes);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, kk_nir_lower_fs_multiview, state->mv->view_mask);

      if (state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED &&
          state->ial && state->ial->depth_att != MESA_VK_ATTACHMENT_NO_INDEX) {
         NIR_PASS(_, nir, msl_ensure_depth_write);
      }
   }

   const struct lower_ycbcr_state ycbcr_state = {
      .set_layout_count = set_layout_count,
      .set_layouts = set_layouts,
   };
   NIR_PASS(_, nir, nir_vk_lower_ycbcr_tex, lookup_ycbcr_conversion,
            &ycbcr_state);

   /* Common msl texture lowering needs to happen after ycbcr lowering and
    * before descriptor lowering. */
   NIR_PASS(_, nir, msl_lower_textures);

   /* Lower push constants before lower_descriptors */
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);

   NIR_PASS(_, nir, nir_lower_memory_model);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_global,
            nir_address_format_64bit_global);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ssbo,
            kk_buffer_addr_format(rs->storage_buffers));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo,
            kk_buffer_addr_format(rs->uniform_buffers));

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            type_size_vec4,
            nir_lower_io_lower_64bit_to_32 |
               nir_lower_io_use_interpolated_input_intrinsics);

   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
            shared_var_info);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared,
            nir_address_format_32bit_offset);

   if (nir->info.zero_initialize_shared_memory && nir->info.shared_size > 0) {
      /* QMD::SHARED_MEMORY_SIZE requires an alignment of 256B so it's safe to
       * align everything up to 16B so we can write whole vec4s.
       */
      nir->info.shared_size = align(nir->info.shared_size, 16);
      NIR_PASS(_, nir, nir_zero_initialize_shared_memory, nir->info.shared_size,
               16);

      /* We need to call lower_compute_system_values again because
       * nir_zero_initialize_shared_memory generates load_invocation_id which
       * has to be lowered to load_invocation_index.
       */
      NIR_PASS(_, nir, nir_lower_compute_system_values, NULL);
   }

   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, nir_lower_variable_initializers, ~nir_var_function_temp);
   NIR_PASS(_, nir, nir_remove_dead_variables,
            nir_var_shader_in | nir_var_shader_out | nir_var_system_value,
            NULL);
   nir->info.io_lowered = true;

   /* Required before kk_nir_lower_vbo so load_input intrinsics' parents are
    * load_const, otherwise the pass will complain */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   /* These passes operate on lowered IO. */
   if ((nir->info.stage == MESA_SHADER_VERTEX ||
        nir->info.stage == MESA_SHADER_TESS_EVAL) &&
       !emulated_stage) {
      kk_lower_hw_vs(nir, state);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      kk_lower_fs(dev, nir, state);
   }

   /* Descriptor lowering needs to happen after lowering blend since we will
    * generate a nir_intrinsic_load_blend_const_color_rgba which gets lowered by
    * the lower descriptor pass
    */
   NIR_PASS(_, nir, kk_nir_lower_descriptors, rs, set_layout_count,
            set_layouts);
   NIR_PASS(_, nir, kk_nir_lower_textures);

   if (dev->vk.enabled_features.nullDescriptor)
      NIR_PASS(_, nir, kk_nir_lower_null_images);

   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
}

static const struct vk_shader_ops kk_shader_ops;

static void
kk_shader_destroy(struct vk_device *vk_dev, struct kk_shader *shader,
                  const VkAllocationCallbacks *pAllocator)
{
   struct kk_device *dev = container_of(vk_dev, struct kk_device, vk);

   struct kk_pipeline_handles *pipe = &shader->pipeline;

   if (shader->info.stage == MESA_SHADER_COMPUTE) {
      mtl_release(pipe->cs);
      pipe->cs = NULL;
   } else if (shader->info.stage == MESA_SHADER_VERTEX) {
      /* Pre-render compute pipelines. */
      for (uint32_t i = 0u; i < pipe->gfx.pre_render_count; ++i) {
         mtl_release(pipe->gfx.pre_render[i]);
         pipe->gfx.pre_render[i] = NULL;
      }
      pipe->gfx.pre_render_count = 0u;

      /* Graphics pipeline. */
      mtl_release(pipe->gfx.render);
      if (pipe->gfx.ds_handle)
         mtl_release(pipe->gfx.ds_handle);
      pipe->gfx.render = NULL;
      pipe->gfx.ds_handle = NULL;

      for (uint32_t i = 0u; i < pipe->gfx.pre_render_count; ++i)
         mtl_release(pipe->gfx.pre_render[i]);

      /* Serialization data. */
      u_foreach_bit(i, shader->info.vs.additional_stages_bits) {
         ralloc_free((void *)shader->entrypoint_names[i]);
         ralloc_free((void *)shader->msl_shaders[i]);
      }
   }

   ralloc_free((void *)shader->entrypoint_names[shader->info.stage]);
   ralloc_free((void *)shader->msl_shaders[shader->info.stage]);

   vk_shader_free(&dev->vk, pAllocator, &shader->vk);
}

static bool
gather_vs_inputs(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_input)
      return false;

   struct nir_io_semantics io = nir_intrinsic_io_semantics(intr);
   BITSET_WORD *attribs_read = data;
   BITSET_SET(attribs_read, (io.location - VERT_ATTRIB_GENERIC0));
   return false;
}

static void
gather_shader_info(struct kk_shader *shader, nir_shader *nir,
                   const struct vk_graphics_pipeline_state *state)
{
   shader->info.stage = nir->info.stage;
   shader->info.uses_per_draw_data = msl_gather_uses_per_draw_data(nir);
   shader->info.num_cull_distances = nir->info.cull_distance_array_size;
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      nir_shader_intrinsics_pass(nir, gather_vs_inputs, nir_metadata_all,
                                 &shader->info.vs.attribs_read);
      shader->info.vs.outputs_written = nir->info.outputs_written;
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* Some meta shaders like vk-meta-resolve will have depth_layout as NONE
       * which is not a valid Metal layout */
      if (nir->info.fs.depth_layout == FRAG_DEPTH_LAYOUT_NONE)
         nir->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_ANY;
   } else if (nir->info.stage == MESA_SHADER_COMPUTE) {
      shader->info.cs.local_size.x = nir->info.workgroup_size[0];
      shader->info.cs.local_size.y = nir->info.workgroup_size[1];
      shader->info.cs.local_size.z = nir->info.workgroup_size[2];
   }
}

static void
modify_nir_info(nir_shader *nir)
{
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      /* Vertex attribute fetch is done in shader through argument buffers. */
      nir->info.inputs_read = 0u;
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* Some meta shaders like vk-meta-resolve will have depth_layout as NONE
       * which is not a valid Metal layout */
      if (nir->info.fs.depth_layout == FRAG_DEPTH_LAYOUT_NONE)
         nir->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_ANY;

      /* These values are part of the declaration and go with IO. We only
       * require the instrunctions to understand interpolation mode. */
      BITSET_CLEAR(nir->info.system_values_read,
                   SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL);
      BITSET_CLEAR(nir->info.system_values_read,
                   SYSTEM_VALUE_BARYCENTRIC_PERSP_SAMPLE);
      BITSET_CLEAR(nir->info.system_values_read,
                   SYSTEM_VALUE_BARYCENTRIC_PERSP_CENTROID);
      BITSET_CLEAR(nir->info.system_values_read,
                   SYSTEM_VALUE_BARYCENTRIC_LINEAR_PIXEL);
      BITSET_CLEAR(nir->info.system_values_read,
                   SYSTEM_VALUE_BARYCENTRIC_LINEAR_CENTROID);
      BITSET_CLEAR(nir->info.system_values_read,
                   SYSTEM_VALUE_BARYCENTRIC_LINEAR_SAMPLE);
   }
}

static bool
lower_vertex_id_zero_base(struct nir_builder *b, nir_intrinsic_instr *intrin,
                          void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_vertex_id_zero_base)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *id = nir_channel(b, nir_load_global_invocation_id(b, 32), 0);
   nir_def_rewrite_uses(&intrin->def, id);

   return true;
}

static bool
kk_nir_lower_vertex_id_zero_base(struct nir_shader *nir)
{
   bool progress = nir_shader_intrinsics_pass(nir, lower_vertex_id_zero_base,
                                              nir_metadata_control_flow, NULL);

   if (progress) {
      BITSET_SET(nir->info.system_values_read,
                 SYSTEM_VALUE_GLOBAL_INVOCATION_ID);
      BITSET_CLEAR(nir->info.system_values_read,
                   SYSTEM_VALUE_VERTEX_ID_ZERO_BASE);
   }
   return progress;
}

static VkResult
kk_compile_shader(struct kk_device *dev, nir_shader *nir,
                  struct kk_shader *prev_stage,
                  const struct vk_pipeline_robustness_state *robustness,
                  const struct vk_graphics_pipeline_state *state,
                  const VkAllocationCallbacks *pAllocator,
                  struct kk_shader **shader_out)
{
   assert(nir->info.io_lowered && "nir must have lowered io");

   struct kk_shader *shader;
   VkResult result = VK_SUCCESS;

   mesa_shader_stage stage = nir->info.stage;
   shader = vk_shader_zalloc(&dev->vk, &kk_shader_ops, stage, pAllocator,
                             sizeof(*shader));
   if (shader == NULL) {
      ralloc_free(nir);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   gather_shader_info(shader, nir, state);

   unsigned num_cull_distances =
      prev_stage ? prev_stage->info.num_cull_distances : 0;
   msl_nir_lower_clip_cull_distance(nir, num_cull_distances);

   /* When using poly to emulate tessellation, vertex and tess control shaders
    * are turned to compute shaders that will be dispatched before the draw
    * call. Tess evalutaion shaders are turned into vertex shaders. */
   if (stage == MESA_SHADER_VERTEX) {
      /* VBO lowering needs to go here otherwise, the linking step removes all
       * inputs since we read vertex attributes from UBOs. */
      kk_lower_vs_vbo(nir, state, robustness);
      if (nir->info.next_stage == MESA_SHADER_TESS_CTRL) {
         NIR_PASS(_, nir, poly_nir_lower_vs_before_gs);
         NIR_PASS(_, nir, kk_nir_lower_vertex_id_zero_base);
         nir->info.stage = MESA_SHADER_COMPUTE;
         memset(&nir->info.cs, 0, sizeof(nir->info.cs));
         nir->xfb_info = NULL;
         NIR_PASS(_, nir, poly_nir_lower_sw_vs);
      } else
         /* Metal's instance_id contains base_instance. When the emulation path
          * is taken, since we launch compute, they correctly get translated.
          * For the non-emulated path we need to subtract base_instance... */
         NIR_PASS(_, nir, msl_nir_lower_instance_id);
   } else if (stage == MESA_SHADER_TESS_CTRL) {
      NIR_PASS(_, nir, poly_nir_lower_tcs);

      shader->info.tess.info.ccw = nir->info.tess.ccw;
      shader->info.tess.info.points = nir->info.tess.point_mode;
      shader->info.tess.info.spacing = nir->info.tess.spacing;
      shader->info.tess.info.mode = nir->info.tess._primitive_mode;

      shader->info.tess.tcs_output_patch_size = nir->info.tess.tcs_vertices_out;
      shader->info.tess.tcs_per_vertex_outputs =
         poly_tcs_per_vertex_outputs(nir);
      shader->info.tess.tcs_nr_patch_outputs =
         util_last_bit(nir->info.patch_outputs_written);
      shader->info.tess.tcs_output_stride = poly_tcs_output_stride(nir);
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      shader->info.tess.info.ccw = nir->info.tess.ccw;
      shader->info.tess.info.points = nir->info.tess.point_mode;
      shader->info.tess.info.spacing = nir->info.tess.spacing;
      shader->info.tess.info.mode = nir->info.tess._primitive_mode;

      /* This destroys info so it needs to happen after the gather */
      NIR_PASS(_, nir, poly_nir_lower_tes, true);
   }

   NIR_PASS(_, nir, kk_nir_lower_poly);

   msl_optimize_nir(nir);
   modify_nir_info(nir);

   struct nir_to_msl_options translate_options = {
      .mem_ctx = NULL,
      .disabled_workarounds = dev->disabled_workarounds,
   };
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      for (uint32_t i = 0u; i < MAX_DRAW_BUFFERS; ++i) {
         enum pipe_format format =
            vk_format_to_pipe_format(state->rp->color_attachment_formats[i]);

         /* If the attachment is unused, default to 4 to avoid issues with alpha
          * to coverage with unused attachments. */
         translate_options.rts_component_count[i] =
            format == PIPE_FORMAT_NONE ? 4u
                                       : util_format_get_nr_components(format);
      }
   }
   shader->msl_shaders[stage] = nir_to_msl(nir, &translate_options);
   const char *entrypoint_name = nir_shader_get_entrypoint(nir)->function->name;

   /* We need to steal so it doesn't get destroyed with the nir. Needs to happen
    * after nir_to_msl since that's where we rename the entrypoint.
    */
   ralloc_steal(NULL, (void *)entrypoint_name);
   shader->entrypoint_names[stage] = (char *)entrypoint_name;

   if (KK_DEBUG(MSL))
      mesa_logi("%s\n", shader->msl_shaders[stage]);

   ralloc_free(nir);

   *shader_out = shader;

   return result;
}

static const struct vk_pipeline_robustness_state rs_none = {
   .uniform_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED,
   .storage_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED,
   .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2,
};

VkResult
kk_compile_nir_shader(struct kk_device *dev, nir_shader *nir,
                      const VkAllocationCallbacks *alloc,
                      struct kk_shader **shader_out)
{
   const struct kk_physical_device *pdev = kk_device_physical(dev);

   assert(nir->info.stage == MESA_SHADER_COMPUTE);
   if (nir->options == NULL)
      nir->options = kk_get_nir_options((struct vk_physical_device *)&pdev->vk,
                                        nir->info.stage, &rs_none);

   struct kk_shader *shader = NULL;
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   VkResult result =
      kk_compile_shader(dev, nir, NULL, &rs_none, NULL, alloc, &shader);
   if (result != VK_SUCCESS)
      return result;

   *shader_out = shader;

   return VK_SUCCESS;
}

static void
nir_opts(nir_shader *nir, void *data)
{
   bool progress;

   do {
      progress = false;

      NIR_PASS(progress, nir, nir_opt_loop);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);

      NIR_PASS(progress, nir, nir_opt_if, 0);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);

      NIR_PASS(progress, nir, nir_opt_peephole_select,
               &(nir_opt_peephole_select_options){
                  .limit = 8,
                  .expensive_alu_ok = true,
                  .discard_ok = true,
               });

      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);
   } while (progress);
}

static nir_shader *
get_empty_nir(struct kk_device *dev, mesa_shader_stage stage,
              const struct vk_graphics_pipeline_state *state)
{
   nir_shader *nir = nir_shader_create(
      NULL, stage,
      kk_get_nir_options(&kk_device_physical(dev)->vk, stage, NULL));

   nir_function *function = nir_function_create(nir, "main_entrypoint");
   function->is_entrypoint = true;
   nir_function_impl_create(function);

   const struct vk_pipeline_robustness_state no_robustness = {
      .storage_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED,
      .uniform_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED,
      .vertex_inputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED,
      .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED,
      .null_uniform_buffer_descriptor = false,
      .null_storage_buffer_descriptor = false,
   };
   kk_lower_nir(dev, nir, false, &no_robustness, 0u, NULL, state);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   return nir;
}

static VkResult
kk_compile_compute_pipeline(struct kk_device *device, const char *msl,
                            const char *entrypoint_name,
                            uint32_t local_size_threads,
                            mtl_compute_pipeline_state **pipe)
{
   mtl_library *library = mtl_new_library(device->mtl_compiler_handle, msl);
   if (library == NULL)
      return VK_ERROR_INVALID_SHADER_NV;

   mtl_function_descriptor *function =
      mtl_new_library_function_descriptor(library, entrypoint_name);
   mtl_compute_pipeline_state *pipeline = mtl_new_compute_pipeline_state(
      device->mtl_compiler_handle, function, local_size_threads);
   mtl_release(function);
   mtl_release(library);

   if (pipeline == NULL)
      return VK_ERROR_INVALID_SHADER_NV;

   *pipe = pipeline;

   return VK_SUCCESS;
}

static mtl_depth_stencil_state *
kk_compile_ds_state(struct kk_device *device, struct kk_shader_info *info)
{
   mtl_stencil_descriptor *front = NULL;
   mtl_stencil_descriptor *back = NULL;
   mtl_depth_stencil_descriptor *descriptor =
      mtl_new_depth_stencil_descriptor();
   mtl_depth_stencil_descriptor_set_depth_write_enabled(
      descriptor, info->vs.has_depth_write);
   mtl_depth_stencil_descriptor_set_depth_compare_function(
      descriptor, info->vs.depth_compare_op);

   if (info->vs.has_stencil_test) {
      back = mtl_new_stencil_descriptor();
      mtl_stencil_descriptor_set_depth_failure_operation(
         back, info->vs.stencil_back.depth_fail);
      mtl_stencil_descriptor_set_stencil_failure_operation(
         back, info->vs.stencil_back.fail);
      mtl_stencil_descriptor_set_depth_stencil_pass_operation(
         back, info->vs.stencil_back.pass);
      mtl_stencil_descriptor_set_stencil_compare_function(
         back, info->vs.stencil_back.compare);
      mtl_stencil_descriptor_set_read_mask(back,
                                           info->vs.stencil_back.compare_mask);
      mtl_stencil_descriptor_set_write_mask(back,
                                            info->vs.stencil_back.write_mask);
      mtl_depth_stencil_descriptor_set_back_face_stencil(descriptor, back);

      front = mtl_new_stencil_descriptor();
      mtl_stencil_descriptor_set_depth_failure_operation(
         front, info->vs.stencil_front.depth_fail);
      mtl_stencil_descriptor_set_stencil_failure_operation(
         front, info->vs.stencil_front.fail);
      mtl_stencil_descriptor_set_depth_stencil_pass_operation(
         front, info->vs.stencil_front.pass);
      mtl_stencil_descriptor_set_stencil_compare_function(
         front, info->vs.stencil_front.compare);
      mtl_stencil_descriptor_set_read_mask(front,
                                           info->vs.stencil_front.compare_mask);
      mtl_stencil_descriptor_set_write_mask(front,
                                            info->vs.stencil_front.write_mask);
      mtl_depth_stencil_descriptor_set_front_face_stencil(descriptor, front);
   }

   mtl_depth_stencil_state *state =
      mtl_new_depth_stencil_state(device->mtl_handle, descriptor);

   if (front)
      mtl_release(front);
   if (back)
      mtl_release(back);
   mtl_release(descriptor);

   return state;
}

static void
kk_gather_ds_info(const struct vk_depth_stencil_state *ds, bool has_depth,
                  bool has_stencil, struct kk_shader_info *info)
{
   /* Depth */
   if (has_depth && ds->depth.test_enable) {
      info->vs.has_depth_write = ds->depth.write_enable;
      info->vs.depth_compare_op = ds->depth.compare_op;
   } else {
      info->vs.has_depth_write = false;
      info->vs.depth_compare_op = VK_COMPARE_OP_ALWAYS;
   }

   /* Stencil */
   info->vs.has_stencil_test = has_stencil && ds->stencil.test_enable;
   if (info->vs.has_stencil_test) {
      info->vs.stencil_back.depth_fail = ds->stencil.back.op.depth_fail;
      info->vs.stencil_back.fail = ds->stencil.back.op.fail;
      info->vs.stencil_back.pass = ds->stencil.back.op.pass;
      info->vs.stencil_back.compare = ds->stencil.back.op.compare;
      info->vs.stencil_back.compare_mask = ds->stencil.back.compare_mask;
      info->vs.stencil_back.write_mask = ds->stencil.back.write_mask;

      info->vs.stencil_front.depth_fail = ds->stencil.front.op.depth_fail;
      info->vs.stencil_front.fail = ds->stencil.front.op.fail;
      info->vs.stencil_front.pass = ds->stencil.front.op.pass;
      info->vs.stencil_front.compare = ds->stencil.front.op.compare;
      info->vs.stencil_front.compare_mask = ds->stencil.front.compare_mask;
      info->vs.stencil_front.write_mask = ds->stencil.front.write_mask;
   }
}

mtl_depth_stencil_state *
kk_compile_depth_stencil_state(struct kk_device *device,
                               const struct vk_depth_stencil_state *ds,
                               bool has_depth, bool has_stencil)
{
   struct kk_shader_info info = {};
   kk_gather_ds_info(ds, has_depth, has_stencil, &info);
   return kk_compile_ds_state(device, &info);
}

/* Copies all msl shader data to the vertex and gathers information for pipeline
 * compilation. */
static void
gather_graphics_pipeline_create_info(
   const struct vk_graphics_pipeline_state *state, struct kk_shader **shaders,
   uint32_t shader_count)
{
   assert(state && shader_count >= 2 &&
          "state, vertex and fragment shaders are a must");
   struct kk_shader *vs = shaders[0];
   struct kk_shader_info *info = &vs->info;

   /* Render pass data */
   const struct vk_render_pass_state *rp = state->rp;
   bool has_depth = rp->depth_attachment_format != VK_FORMAT_UNDEFINED;
   bool has_stencil = rp->stencil_attachment_format != VK_FORMAT_UNDEFINED;
   {
      info->vs.color_attachment_count = rp->color_attachment_count;
      for (uint8_t i = 0u; i < info->vs.color_attachment_count; ++i) {
         VkFormat format = rp->color_attachment_formats[i];
         info->vs.rt_formats[i] = format == VK_FORMAT_UNDEFINED
                                     ? MTL_PIXEL_FORMAT_INVALID
                                     : vk_format_to_mtl_pixel_format(format);
      }
      info->vs.d_format =
         has_depth ? vk_format_to_mtl_pixel_format(rp->depth_attachment_format)
                   : MTL_PIXEL_FORMAT_INVALID;
      info->vs.s_format =
         has_stencil
            ? vk_format_to_mtl_pixel_format(rp->stencil_attachment_format)
            : MTL_PIXEL_FORMAT_INVALID;

      info->vs.view_mask = state->mv->view_mask;
   }

   info->vs.has_ds = has_static_depth_stencil_state(state);
   if (info->vs.has_ds)
      kk_gather_ds_info(state->ds, has_depth, has_stencil, info);

   info->vs.has_ms = state->ms != NULL;
   info->vs.sample_count = 1u;
   if (info->vs.has_ms) {
      const struct vk_multisample_state *ms = state->ms;
      info->vs.sample_count = ms->rasterization_samples;
      info->vs.has_alpha_to_coverage_enabled = ms->alpha_to_coverage_enable;
      info->vs.has_alpha_to_one_enabled = ms->alpha_to_one_enable;
   }

   /* We need to store all other stage sources in the vertex too otherwise we
    * won't be able to create the whole pipeline correctly. */
   for (uint32_t i = 1; i < shader_count; ++i) {
      struct kk_shader *s = shaders[i];
      mesa_shader_stage stage = s->info.stage;
      uint32_t length = strlen(s->msl_shaders[stage]) + 1u;
      vs->msl_shaders[stage] = ralloc_size(NULL, length);
      memcpy(vs->msl_shaders[stage], s->msl_shaders[stage], length);

      length = strlen(s->entrypoint_names[stage]) + 1;
      vs->entrypoint_names[stage] = ralloc_size(NULL, length);
      memcpy(vs->entrypoint_names[stage], s->entrypoint_names[stage], length);

      info->vs.additional_stages_bits |= BITFIELD_BIT(stage);

      if (s->info.stage == MESA_SHADER_TESS_CTRL)
         info->vs.tess_local_thread_size = s->info.tess.tcs_output_patch_size;
   }

   uint8_t topology = state->ia->primitive_topology;
   if (info->vs.additional_stages_bits & BITFIELD_BIT(MESA_SHADER_TESS_CTRL)) {
      /* TODO_KOSMICKRISP Hackity hack */
      struct kk_shader *tesc = shaders[1];
      struct kk_shader *tese = shaders[2];

      struct kk_tess_info info =
         kk_tess_info_merge(tese->info.tess.info, tesc->info.tess.info);

      /* Determine primitive based on the merged state */
      if (info.points) {
         topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      } else if (info.mode == TESS_PRIMITIVE_ISOLINES) {
         topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      } else {
         topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      }
   }

   info->vs.topology =
      vk_primitive_topology_to_mtl_primitive_topology_class(topology);
}

static VkResult
kk_compile_graphics_pipeline(struct kk_device *device, struct kk_shader *vs)
{
   VkResult result = VK_SUCCESS;
   struct kk_pipeline_handles *pipe = &vs->pipeline;
   mesa_shader_stage vs_stage = MESA_SHADER_VERTEX;

   /* When tessellation or geometry shaders are present, the pair that will be
    * used for the Metal render pipeline will be the fragment shader and the
    * previous stage. This means that any shader before that needs to be a
    * compute pipeline to be launched before the render pipeline. */
   uint32_t stages_bits = vs->info.vs.additional_stages_bits;
   pipe->gfx.pre_render_count = util_bitcount(stages_bits) - 1u;
   for (uint32_t i = 0u; i < pipe->gfx.pre_render_count; ++i) {
      const char *msl = vs->msl_shaders[vs_stage];
      const char *entrypoint_name = vs->entrypoint_names[vs_stage];
      uint32_t local_thread_size =
         (i == 0u) ? 64u : vs->info.vs.tess_local_thread_size;
      result = kk_compile_compute_pipeline(device, msl, entrypoint_name,
                                           local_thread_size,
                                           &pipe->gfx.pre_render[i]);

      if (result != VK_SUCCESS)
         return VK_ERROR_INVALID_SHADER_NV;
      vs_stage = u_bit_scan(&stages_bits);
   }

   mtl_library *vertex_library =
      mtl_new_library(device->mtl_compiler_handle, vs->msl_shaders[vs_stage]);
   if (vertex_library == NULL)
      return VK_ERROR_INVALID_SHADER_NV;

   mtl_function_descriptor *vertex_function =
      mtl_new_library_function_descriptor(vertex_library,
                                          vs->entrypoint_names[vs_stage]);

   mtl_library *fragment_library = mtl_new_library(
      device->mtl_compiler_handle, vs->msl_shaders[MESA_SHADER_FRAGMENT]);
   if (fragment_library == NULL) {
      result = VK_ERROR_INVALID_SHADER_NV;
      goto destroy_vertex;
   }
   mtl_function_descriptor *fragment_function =
      mtl_new_library_function_descriptor(
         fragment_library, vs->entrypoint_names[MESA_SHADER_FRAGMENT]);

   mtl_render_pipeline_descriptor *pipeline_descriptor =
      mtl_new_render_pipeline_descriptor();
   mtl_render_pipeline_descriptor_set_vertex_shader(pipeline_descriptor,
                                                    vertex_function);
   mtl_render_pipeline_descriptor_set_fragment_shader(pipeline_descriptor,
                                                      fragment_function);

   /* Layered rendering in Metal requires setting primitive topology class */
   mtl_render_pipeline_descriptor_set_input_primitive_topology(
      pipeline_descriptor, vs->info.vs.topology);

   for (uint8_t i = 0; i < vs->info.vs.color_attachment_count; ++i) {
      if (vs->info.vs.rt_formats[i] != MTL_PIXEL_FORMAT_INVALID)
         mtl_render_pipeline_descriptor_set_color_attachment_format(
            pipeline_descriptor, i, vs->info.vs.rt_formats[i]);
   }

   if (vs->info.vs.has_ds) {
      pipe->gfx.ds_handle = kk_compile_ds_state(device, &vs->info);
   }

   if (vs->info.vs.view_mask) {
      uint32_t max_amplification = util_bitcount(vs->info.vs.view_mask);
      mtl_render_pipeline_descriptor_set_max_vertex_amplification_count(
         pipeline_descriptor, max_amplification);
   }

   if (vs->info.vs.has_ms) {
      mtl_render_pipeline_descriptor_set_raster_sample_count(
         pipeline_descriptor, vs->info.vs.sample_count);
      mtl_render_pipeline_descriptor_set_alpha_to_coverage(
         pipeline_descriptor, vs->info.vs.has_alpha_to_coverage_enabled);
      mtl_render_pipeline_descriptor_set_alpha_to_one(
         pipeline_descriptor, vs->info.vs.has_alpha_to_one_enabled);
   }

   pipe->gfx.render =
      mtl_new_render_pipeline(device->mtl_compiler_handle, pipeline_descriptor);
   if (pipe->gfx.render == NULL)
      result = VK_ERROR_INVALID_SHADER_NV;

   mtl_release(pipeline_descriptor);
   mtl_release(fragment_function);
   mtl_release(fragment_library);
destroy_vertex:
   mtl_release(vertex_function);
   mtl_release(vertex_library);

   return result;
}

static VkResult
kk_compile_shaders(struct vk_device *device, uint32_t shader_count,
                   struct vk_shader_compile_info *infos,
                   const struct vk_graphics_pipeline_state *state,
                   const struct vk_features *enabled_features,
                   const VkAllocationCallbacks *pAllocator,
                   struct vk_shader **shaders_out)
{
   VkResult result = VK_SUCCESS;
   struct kk_device *dev = container_of(device, struct kk_device, vk);

   /* Vulkan doesn't enforce a fragment shader to build pipelines. We may need
    * to create one. */
   nir_shader *nir_shaders[shader_count + 1u];
   struct kk_shader *shaders[shader_count + 1u];

   /* Determine if the pipeline contains tessellation stages */
   bool tess = false;
   for (uint32_t i = 0u; i < shader_count; ++i) {
      const struct vk_shader_compile_info *info = &infos[i];
      if (info->nir->info.stage == MESA_SHADER_TESS_CTRL ||
          info->nir->info.stage == MESA_SHADER_TESS_EVAL) {
         tess = true;
         break;
      }
   }

   /* Lower shaders, notably lowering IO. This is a prerequisite for intershader
    * optimization. */
   const struct vk_pipeline_robustness_state *vertex_robustness = &rs_none;
   for (uint32_t i = 0u; i < shader_count; ++i) {
      const struct vk_shader_compile_info *info = &infos[i];
      nir_shader *nir = info->nir;

      /* For tessellation pipelines, some stages may be emulated in compute */
      bool emulated_stage = tess && (nir->info.stage == MESA_SHADER_VERTEX ||
                                     nir->info.stage == MESA_SHADER_TESS_CTRL);

      msl_preprocess_nir_workarounds(nir, dev->disabled_workarounds);
      kk_lower_nir(dev, nir, emulated_stage, info->robustness,
                   info->set_layout_count, info->set_layouts, state);

      if (nir->info.stage == MESA_SHADER_VERTEX)
         vertex_robustness = info->robustness;

      nir_shaders[i] = nir;
   }

   /* Since we don't support GPL nor shader objects and Metal render pipelines
    * require both vertex and fragment, we may need to provide a pass-through
    * fragment. */
   uint32_t total_shaders = shader_count;
   if (state && infos[shader_count - 1u].stage != MESA_SHADER_FRAGMENT) {
      nir_shaders[shader_count] =
         get_empty_nir(dev, MESA_SHADER_FRAGMENT, state);
      total_shaders += 1u;
   }

   nir_opt_varyings_bulk(nir_shaders, total_shaders, true, UINT32_MAX,
                         UINT32_MAX, nir_opts, NULL);
   /* Second pass is required because some dEQP-VK.glsl.matrix.sub.dynamic.*
    * would fail otherwise due to vertex outputting vec4 while fragments reading
    * vec3 when in reality only vec3 is needed. */
   nir_opt_varyings_bulk(nir_shaders, total_shaders, true, UINT32_MAX,
                         UINT32_MAX, nir_opts, NULL);

   for (uint32_t i = 0; i < total_shaders; i++) {
      struct kk_shader *prev_stage = i > 0 ? shaders[i - 1] : NULL;
      result =
         kk_compile_shader(dev, nir_shaders[i], prev_stage, vertex_robustness,
                           state, pAllocator, &shaders[i]);

      if (result != VK_SUCCESS) {
         /* Clean up all the shaders before this point */
         for (uint32_t j = 0; j < i; j++)
            kk_shader_destroy(&dev->vk, shaders[j], pAllocator);

         /* Clean up all the NIR after this point */
         for (uint32_t j = i + 1; j < total_shaders; j++)
            ralloc_free(nir_shaders[j]);

         /* Memset the output array */
         memset(shaders_out, 0, shader_count * sizeof(*shaders_out));

         return result;
      }
   }

   /* Compile pipeline:
    * 1. Compute pipeline
    * 2. Graphics with all stages (since we don't support GPL nor shader
    * objects for now). This will be addressed later.
    */
   if (shaders[0]->vk.stage == MESA_SHADER_COMPUTE) {
      struct kk_shader *s = shaders[0];
      const char *msl = s->msl_shaders[MESA_SHADER_COMPUTE];
      const char *entrypoint_name = s->entrypoint_names[MESA_SHADER_COMPUTE];
      uint32_t local_size_threads = s->info.cs.local_size.x *
                                    s->info.cs.local_size.y *
                                    s->info.cs.local_size.z;
      result = kk_compile_compute_pipeline(dev, msl, entrypoint_name,
                                           local_size_threads, &s->pipeline.cs);
   } else {
      gather_graphics_pipeline_create_info(state, shaders, total_shaders);
      result = kk_compile_graphics_pipeline(dev, shaders[0]);

      /* Free generated fragment shader */
      if (shader_count != total_shaders)
         kk_shader_destroy(&dev->vk, shaders[shader_count], pAllocator);
   }

   if (result != VK_SUCCESS) {
      for (uint32_t i = 0; i < shader_count; i++) {
         kk_shader_destroy(&dev->vk, shaders[i], pAllocator);
      }

      /* Memset the output array */
      memset(shaders_out, 0, shader_count * sizeof(*shaders_out));
   } else {
      memcpy(shaders_out, shaders, shader_count * sizeof(*shaders_out));
   }

   return result;
}

static void
kk_msl_serialize(struct kk_shader *shader, mesa_shader_stage stage,
                 struct blob *blob)
{
   uint32_t entrypoint_length = strlen(shader->entrypoint_names[stage]) + 1;
   uint32_t code_length = strlen(shader->msl_shaders[stage]) + 1;
   blob_write_uint32(blob, entrypoint_length);
   blob_write_uint32(blob, code_length);
   blob_write_bytes(blob, shader->entrypoint_names[stage], entrypoint_length);
   blob_write_bytes(blob, shader->msl_shaders[stage], code_length);
}

static bool
kk_shader_serialize(struct vk_device *vk_dev, const struct vk_shader *vk_shader,
                    struct blob *blob)
{
   struct kk_shader *shader = container_of(vk_shader, struct kk_shader, vk);

   blob_write_bytes(blob, &shader->info, sizeof(shader->info));
   kk_msl_serialize(shader, shader->info.stage, blob);
   if (shader->info.stage == MESA_SHADER_VERTEX) {
      u_foreach_bit(stage, shader->info.vs.additional_stages_bits) {
         kk_msl_serialize(shader, stage, blob);
      }
   }

   return !blob->out_of_memory;
}

static VkResult
kk_msl_deserialize(struct blob_reader *blob, mesa_shader_stage stage,
                   struct kk_shader *shader)
{
   const uint32_t entrypoint_length = blob_read_uint32(blob);
   const uint32_t code_length = blob_read_uint32(blob);
   shader->entrypoint_names[stage] =
      ralloc_array(NULL, char, entrypoint_length);
   if (shader->entrypoint_names[stage] == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   shader->msl_shaders[stage] = ralloc_array(NULL, char, code_length);
   if (shader->msl_shaders[stage] == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   blob_copy_bytes(blob, (void *)shader->entrypoint_names[stage],
                   entrypoint_length);
   blob_copy_bytes(blob, (void *)shader->msl_shaders[stage], code_length);

   if (blob->overrun)
      return VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT;

   return VK_SUCCESS;
}

static VkResult
kk_deserialize_shader(struct vk_device *vk_dev, struct blob_reader *blob,
                      uint32_t binary_version,
                      const VkAllocationCallbacks *pAllocator,
                      struct vk_shader **shader_out)
{
   struct kk_device *dev = container_of(vk_dev, struct kk_device, vk);

   struct kk_shader_info info;
   blob_copy_bytes(blob, &info, sizeof(info));
   if (blob->overrun)
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   struct kk_shader *shader = vk_shader_zalloc(
      &dev->vk, &kk_shader_ops, info.stage, pAllocator, sizeof(*shader));
   if (shader == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = kk_msl_deserialize(blob, info.stage, shader);
   if (result != VK_SUCCESS)
      goto fail;

   shader->info = info;

   if (shader->info.stage == MESA_SHADER_VERTEX) {
      u_foreach_bit(stage, shader->info.vs.additional_stages_bits) {
         result = kk_msl_deserialize(blob, stage, shader);
         if (result != VK_SUCCESS)
            goto fail;
      }
   }

   if (info.stage == MESA_SHADER_COMPUTE) {
      const char *msl = shader->msl_shaders[MESA_SHADER_COMPUTE];
      const char *entrypoint_name =
         shader->entrypoint_names[MESA_SHADER_COMPUTE];
      uint32_t local_size_threads = shader->info.cs.local_size.x *
                                    shader->info.cs.local_size.y *
                                    shader->info.cs.local_size.z;
      result = kk_compile_compute_pipeline(
         dev, msl, entrypoint_name, local_size_threads, &shader->pipeline.cs);
   } else if (info.stage == MESA_SHADER_VERTEX) {
      result = kk_compile_graphics_pipeline(dev, shader);
   }

   if (result != VK_SUCCESS)
      goto fail;

   *shader_out = &shader->vk;

   return VK_SUCCESS;

fail:
   kk_shader_destroy(&dev->vk, shader, pAllocator);
   return vk_error(dev, result);
}

void
kk_cmd_bind_compute_shader(struct kk_cmd_buffer *cmd, struct kk_shader *shader)
{
   cmd->state.shaders[MESA_SHADER_COMPUTE] = shader;
}

static void
kk_cmd_bind_graphics_shader(struct kk_cmd_buffer *cmd,
                            const mesa_shader_stage stage,
                            struct kk_shader *shader)
{
   cmd->state.shaders[stage] = shader;
   cmd->state.dirty_shaders |= BITFIELD_BIT(stage);

   /* Relevant pipeline data is only stored in vertex shaders */
   if (stage != MESA_SHADER_VERTEX)
      return;

   bool requires_dynamic_depth_stencil = shader->pipeline.gfx.ds_handle == NULL;
   if (cmd->state.gfx.is_depth_stencil_dynamic) {
      /* If we are switching from dynamic to static, we need to clean up
       * temporary state. Otherwise, leave the existing dynamic state
       * untouched.
       */
      if (!requires_dynamic_depth_stencil) {
         mtl_release(cmd->state.gfx.depth_stencil_state);
         cmd->state.gfx.depth_stencil_state = shader->pipeline.gfx.ds_handle;
      }
   } else
      cmd->state.gfx.depth_stencil_state = shader->pipeline.gfx.ds_handle;
   cmd->state.gfx.is_depth_stencil_dynamic = requires_dynamic_depth_stencil;
   cmd->state.gfx.dirty |= KK_DIRTY_VB;

   cmd->state.gfx.pipeline_sample_count = shader->info.vs.sample_count;
}

static void
kk_cmd_bind_shaders(struct vk_command_buffer *cmd_buffer, uint32_t stage_count,
                    const mesa_shader_stage *stages,
                    struct vk_shader **const shaders)
{
   struct kk_cmd_buffer *cmd =
      container_of(cmd_buffer, struct kk_cmd_buffer, vk);

   for (uint32_t i = 0; i < stage_count; i++) {
      struct kk_shader *shader = container_of(shaders[i], struct kk_shader, vk);

      if (stages[i] == MESA_SHADER_COMPUTE || stages[i] == MESA_SHADER_KERNEL)
         kk_cmd_bind_compute_shader(cmd, shader);
      else
         kk_cmd_bind_graphics_shader(cmd, stages[i], shader);
   }
}

static VkResult
kk_shader_get_executable_properties(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t *executable_count, VkPipelineExecutablePropertiesKHR *properties)
{
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out, properties,
                          executable_count);

   return vk_outarray_status(&out);
}

static VkResult
kk_shader_get_executable_statistics(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *statistic_count,
   VkPipelineExecutableStatisticKHR *statistics)
{
   /* TODO_KOSMICKRISP */
   return VK_SUCCESS;
}

static VkResult
kk_shader_get_executable_internal_representations(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *internal_representation_count,
   VkPipelineExecutableInternalRepresentationKHR *internal_representations)
{
   /* TODO_KOSMICKRISP */
   return VK_SUCCESS;
}

static void
kk_vk_shader_destroy(struct vk_device *vk_dev, struct vk_shader *vk_shader,
                     const VkAllocationCallbacks *pAllocator)
{
   kk_shader_destroy(vk_dev, container_of(vk_shader, struct kk_shader, vk),
                     pAllocator);
}

static const struct vk_shader_ops kk_shader_ops = {
   .destroy = kk_vk_shader_destroy,
   .serialize = kk_shader_serialize,
   .get_executable_properties = kk_shader_get_executable_properties,
   .get_executable_statistics = kk_shader_get_executable_statistics,
   .get_executable_internal_representations =
      kk_shader_get_executable_internal_representations,
};

const struct vk_device_shader_ops kk_device_shader_ops = {
   .get_nir_options = kk_get_nir_options,
   .get_spirv_options = kk_get_spirv_options,
   .preprocess_nir = kk_preprocess_nir,
   .hash_state = kk_hash_graphics_state,
   .compile =
      kk_compile_shaders, /* This will only generate the MSL string we need
                             to use for actual library generation */
   .deserialize = kk_deserialize_shader,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_bind_shaders = kk_cmd_bind_shaders,
};
