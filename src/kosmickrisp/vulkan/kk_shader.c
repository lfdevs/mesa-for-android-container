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

#include "vk_blend.h"
#include "vk_format.h"
#include "vk_graphics_state.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_pipeline.h"

static const nir_shader_compiler_options *
kk_get_nir_options(struct vk_physical_device *vk_pdev, mesa_shader_stage stage,
                   UNUSED const struct vk_pipeline_robustness_state *rs)
{
   static nir_shader_compiler_options options = {
      .lower_fdph = true,
      .has_fsub = true,
      .has_isub = true,
      .lower_extract_word = true,
      .lower_extract_byte = true,
      .lower_insert_word = true,
      .lower_insert_byte = true,
      .lower_fmod = true,
      .discard_is_demote = true,
      .instance_id_includes_base_index = true,
      .lower_device_index_to_zero = true,
      .lower_pack_64_2x32_split = true,
      .lower_unpack_64_2x32_split = true,
      .lower_pack_64_2x32 = true,
      .lower_pack_half_2x16 = true,
      .lower_pack_split = true,
      .lower_unpack_half_2x16 = true,
      .has_cs_global_id = true,
      .lower_vector_cmp = true,
      .lower_fquantize2f16 = true,
      .lower_scmp = true,
      .lower_ifind_msb = true,
      .lower_ufind_msb = true,
      .lower_find_lsb = true,
      .has_uclz = true,
      .lower_mul_2x32_64 = true,
      .lower_uadd_carry = true,
      .lower_usub_borrow = true,
      /* Metal does not support double. */
      .lower_doubles_options = (nir_lower_doubles_options)(~0),
      .lower_int64_options =
         nir_lower_ufind_msb64 | nir_lower_subgroup_shuffle64,
   };
   return &options;
}

static struct spirv_to_nir_options
kk_get_spirv_options(struct vk_physical_device *vk_pdev,
                     UNUSED mesa_shader_stage stage,
                     const struct vk_pipeline_robustness_state *rs)
{
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
   uint32_t rasterization_samples;
   uint16_t static_sample_mask;
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

   /* Blend state gets [de]serialized, so we need to hash it */
   if (state->cb)
      key->color_blend = *(state->cb);

   if (state->ms) {
      key->rasterization_samples = state->ms->rasterization_samples;
      key->static_sample_mask = state->ms->sample_mask;
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

      _mesa_blake3_update(&blake3_ctx, &state->rp->view_mask,
                          sizeof(state->rp->view_mask));
   }

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
kk_lower_vs_vbo(nir_shader *nir, const struct vk_graphics_pipeline_state *state)
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
   NIR_PASS(_, nir, kk_nir_lower_vbo, attributes);
}

static void
kk_lower_vs(nir_shader *nir, const struct vk_graphics_pipeline_state *state)
{
   if (state->ia->primitive_topology != VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
      nir_shader_intrinsics_pass(nir, msl_nir_vs_remove_point_size_write,
                                 nir_metadata_control_flow, NULL);

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

   static_assert(ARRAY_SIZE(opts.format) == 8, "max RTs out of sync");

   for (unsigned i = 0; i < ARRAY_SIZE(opts.format); ++i) {
      opts.format[i] =
         vk_format_to_pipe_format(state->rp->color_attachment_formats[i]);
      if (state->cb->attachments[i].blend_enable) {
         opts.rt[i] = (nir_lower_blend_rt){
            .rgb.src_factor = vk_blend_factor_to_pipe(
               state->cb->attachments[i].src_color_blend_factor),
            .rgb.dst_factor = vk_blend_factor_to_pipe(
               state->cb->attachments[i].dst_color_blend_factor),
            .rgb.func =
               vk_blend_op_to_pipe(state->cb->attachments[i].color_blend_op),

            .alpha.src_factor = vk_blend_factor_to_pipe(
               state->cb->attachments[i].src_alpha_blend_factor),
            .alpha.dst_factor = vk_blend_factor_to_pipe(
               state->cb->attachments[i].dst_alpha_blend_factor),
            .alpha.func =
               vk_blend_op_to_pipe(state->cb->attachments[i].alpha_blend_op),

            .colormask = state->cb->attachments[i].write_mask,
         };
      } else {
         opts.rt[i] = (nir_lower_blend_rt){
            .rgb.src_factor = PIPE_BLENDFACTOR_ONE,
            .rgb.dst_factor = PIPE_BLENDFACTOR_ZERO,
            .rgb.func = PIPE_BLEND_ADD,

            .alpha.src_factor = PIPE_BLENDFACTOR_ONE,
            .alpha.dst_factor = PIPE_BLENDFACTOR_ZERO,
            .alpha.func = PIPE_BLEND_ADD,

            .colormask = state->cb->attachments[i].write_mask,
         };
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
   if (state->cb)
      kk_lower_fs_blend(nir, state);

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
       state->ms->sample_mask != UINT16_MAX)
      NIR_PASS(_, nir, msl_lower_static_sample_mask, state->ms->sample_mask);
   /* Check https://github.com/KhronosGroup/Vulkan-Portability/issues/54 for
    * explanation on why we need this. */
   else if (nir->info.fs.needs_full_quad_helper_invocations ||
            nir->info.fs.needs_coarse_quad_helper_invocations)
      NIR_PASS(_, nir, msl_lower_static_sample_mask, 0xFFFFFFFF);

   /* KK_WORKAROUND_4 */
   if (!(dev->disabled_workarounds & BITFIELD64_BIT(4))) {
      NIR_PASS(_, nir, nir_lower_helper_writes, true);
      NIR_PASS(_, nir, nir_lower_is_helper_invocation);
   }
}

static void
kk_lower_nir(struct kk_device *dev, nir_shader *nir,
             const struct vk_pipeline_robustness_state *rs,
             uint32_t set_layout_count,
             struct vk_descriptor_set_layout *const *set_layouts,
             const struct vk_graphics_pipeline_state *state)
{
   /* Massage IO related variables to please Metal */
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_, nir, kk_nir_lower_vs_multiview, state->rp->view_mask);

      /* kk_nir_lower_vs_multiview may create a temporary array to assign the
       * correct view index. Since we don't handle derefs, we need to get rid of
       * them. */
      NIR_PASS(_, nir, nir_lower_vars_to_scratch, nir_var_function_temp, 0,
               glsl_get_natural_size_align_bytes,
               glsl_get_natural_size_align_bytes);

      NIR_PASS(_, nir, msl_ensure_vertex_position_output);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      enum pipe_format rts[MAX_DRAW_BUFFERS] = {PIPE_FORMAT_NONE};
      const struct vk_render_pass_state *rp = state->rp;
      for (uint32_t i = 0u; i < MAX_DRAW_BUFFERS; ++i)
         rts[i] = vk_format_to_pipe_format(rp->color_attachment_formats[i]);

      NIR_PASS(_, nir, msl_nir_fs_force_output_signedness, rts);

      NIR_PASS(_, nir, kk_nir_lower_fs_multiview, state->rp->view_mask);

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
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      kk_lower_vs(nir, state);
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

   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
}

static const struct vk_shader_ops kk_shader_ops;

static void
kk_shader_destroy(struct vk_device *vk_dev, struct vk_shader *vk_shader,
                  const VkAllocationCallbacks *pAllocator)
{
   struct kk_device *dev = container_of(vk_dev, struct kk_device, vk);
   struct kk_shader *shader = container_of(vk_shader, struct kk_shader, vk);

   if (shader->pipeline.cs) {
      mtl_release(shader->pipeline.cs);
   } else if (shader->pipeline.gfx.handle) {
      mtl_release(shader->pipeline.gfx.handle);
      if (shader->pipeline.gfx.mtl_depth_stencil_state_handle)
         mtl_release(shader->pipeline.gfx.mtl_depth_stencil_state_handle);
      shader->pipeline.gfx.handle = NULL;
      shader->pipeline.gfx.mtl_depth_stencil_state_handle = NULL;
   }

   ralloc_free((void *)shader->msl_code);
   ralloc_free((void *)shader->entrypoint_name);

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
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      nir_shader_intrinsics_pass(nir, gather_vs_inputs, nir_metadata_all,
                                 &shader->info.vs.attribs_read);
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

static VkResult
kk_compile_shader(struct kk_device *dev, struct vk_shader_compile_info *info,
                  const struct vk_graphics_pipeline_state *state,
                  const VkAllocationCallbacks *pAllocator,
                  struct vk_shader **shader_out)
{
   struct kk_shader *shader;
   VkResult result = VK_SUCCESS;

   /* We consume the NIR, regardless of success or failure */
   nir_shader *nir = info->nir;

   shader = vk_shader_zalloc(&dev->vk, &kk_shader_ops, info->stage, pAllocator,
                             sizeof(*shader));
   if (shader == NULL) {
      ralloc_free(nir);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   if (nir->info.io_lowered == false)
      kk_lower_nir(dev, nir, info->robustness, info->set_layout_count,
                   info->set_layouts, state);

   gather_shader_info(shader, nir, state);

   /* VBO lowering needs to go here otherwise, the linking step removes all
    * inputs since we read vertex attributes from UBOs. */
   if (info->stage == MESA_SHADER_VERTEX) {
      kk_lower_vs_vbo(nir, state);
   }
   msl_lower_nir_late(nir);
   msl_optimize_nir(nir);
   modify_nir_info(nir);
   shader->msl_code = nir_to_msl(nir, NULL, dev->disabled_workarounds);
   const char *entrypoint_name = nir_shader_get_entrypoint(nir)->function->name;

   /* We need to steal so it doesn't get destroyed with the nir. Needs to happen
    * after nir_to_msl since that's where we rename the entrypoint.
    */
   ralloc_steal(NULL, (void *)entrypoint_name);
   shader->entrypoint_name = entrypoint_name;

   if (KK_DEBUG(MSL))
      mesa_logi("%s\n", shader->msl_code);

   ralloc_free(nir);

   *shader_out = &shader->vk;

   return result;
}

static const struct vk_pipeline_robustness_state rs_none = {
   .uniform_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
   .storage_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
   .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2_EXT,
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

   struct vk_shader_compile_info info = {
      .stage = nir->info.stage,
      .nir = nir,
      .robustness = &rs_none,
   };

   struct vk_shader *shader = NULL;
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   VkResult result = kk_compile_shader(dev, &info, NULL, alloc, &shader);
   if (result != VK_SUCCESS)
      return result;

   *shader_out = container_of(shader, struct kk_shader, vk);

   return VK_SUCCESS;
}

static void
nir_opts(nir_shader *nir)
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
   kk_lower_nir(dev, nir, &no_robustness, 0u, NULL, state);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   return nir;
}

static VkResult
kk_compile_compute_pipeline(struct kk_device *device, struct kk_shader *shader)
{
   uint32_t local_size_threads = shader->info.cs.local_size.x *
                                 shader->info.cs.local_size.y *
                                 shader->info.cs.local_size.z;
   mtl_library *library = mtl_new_library(device->mtl_handle, shader->msl_code);
   if (library == NULL)
      return VK_ERROR_INVALID_SHADER_NV;

   mtl_function *function =
      mtl_new_function_with_name(library, shader->entrypoint_name);
   shader->pipeline.cs = mtl_new_compute_pipeline_state(
      device->mtl_handle, function, local_size_threads);
   mtl_release(function);
   mtl_release(library);

   if (shader->pipeline.cs == NULL)
      return VK_ERROR_INVALID_SHADER_NV;

   return VK_SUCCESS;
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

mtl_depth_stencil_state *
kk_compile_depth_stencil_state(struct kk_device *device,
                               const struct vk_depth_stencil_state *ds,
                               bool has_depth, bool has_stencil)
{
   mtl_stencil_descriptor *front = NULL;
   mtl_stencil_descriptor *back = NULL;
   mtl_depth_stencil_descriptor *descriptor =
      mtl_new_depth_stencil_descriptor();
   if (has_depth && ds->depth.test_enable) {
      mtl_depth_stencil_descriptor_set_depth_write_enabled(
         descriptor, ds->depth.write_enable);
      mtl_depth_stencil_descriptor_set_depth_compare_function(
         descriptor, ds->depth.compare_op);
   } else {
      /* Only way to disable is to always pass */
      mtl_depth_stencil_descriptor_set_depth_write_enabled(descriptor, false);
      mtl_depth_stencil_descriptor_set_depth_compare_function(
         descriptor, VK_COMPARE_OP_ALWAYS);
   }

   if (has_stencil && ds->stencil.test_enable) {
      back = mtl_new_stencil_descriptor();
      mtl_stencil_descriptor_set_depth_failure_operation(
         back, ds->stencil.back.op.depth_fail);
      mtl_stencil_descriptor_set_stencil_failure_operation(
         back, ds->stencil.back.op.fail);
      mtl_stencil_descriptor_set_depth_stencil_pass_operation(
         back, ds->stencil.back.op.pass);
      mtl_stencil_descriptor_set_stencil_compare_function(
         back, ds->stencil.back.op.compare);
      mtl_stencil_descriptor_set_read_mask(back, ds->stencil.back.compare_mask);
      mtl_stencil_descriptor_set_write_mask(back, ds->stencil.back.write_mask);
      mtl_depth_stencil_descriptor_set_back_face_stencil(descriptor, back);

      front = mtl_new_stencil_descriptor();
      mtl_stencil_descriptor_set_depth_failure_operation(
         front, ds->stencil.front.op.depth_fail);
      mtl_stencil_descriptor_set_stencil_failure_operation(
         front, ds->stencil.front.op.fail);
      mtl_stencil_descriptor_set_depth_stencil_pass_operation(
         front, ds->stencil.front.op.pass);
      mtl_stencil_descriptor_set_stencil_compare_function(
         front, ds->stencil.front.op.compare);
      mtl_stencil_descriptor_set_read_mask(front,
                                           ds->stencil.front.compare_mask);
      mtl_stencil_descriptor_set_write_mask(front,
                                            ds->stencil.front.write_mask);
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

/* TODO_KOSMICKRISP For now we just support vertex and fragment */
static VkResult
kk_compile_graphics_pipeline(struct kk_device *device,
                             struct kk_shader *vertex_shader,
                             struct kk_shader *fragment_shader,
                             const struct vk_graphics_pipeline_state *state)
{
   VkResult result = VK_SUCCESS;

   assert(vertex_shader->info.stage == MESA_SHADER_VERTEX &&
          fragment_shader->info.stage == MESA_SHADER_FRAGMENT);

   mtl_library *vertex_library =
      mtl_new_library(device->mtl_handle, vertex_shader->msl_code);
   if (vertex_library == NULL)
      return VK_ERROR_INVALID_SHADER_NV;

   mtl_function *vertex_function = mtl_new_function_with_name(
      vertex_library, vertex_shader->entrypoint_name);

   mtl_library *fragment_library =
      mtl_new_library(device->mtl_handle, fragment_shader->msl_code);
   if (fragment_library == NULL) {
      result = VK_ERROR_INVALID_SHADER_NV;
      goto destroy_vertex;
   }
   mtl_function *fragment_function = mtl_new_function_with_name(
      fragment_library, fragment_shader->entrypoint_name);

   mtl_render_pipeline_descriptor *pipeline_descriptor =
      mtl_new_render_pipeline_descriptor();
   mtl_render_pipeline_descriptor_set_vertex_shader(pipeline_descriptor,
                                                    vertex_function);
   if (fragment_function)
      mtl_render_pipeline_descriptor_set_fragment_shader(pipeline_descriptor,
                                                         fragment_function);
   /* Layered rendering in Metal requires setting primitive topology class */
   mtl_render_pipeline_descriptor_set_input_primitive_topology(
      pipeline_descriptor,
      vk_primitive_topology_to_mtl_primitive_topology_class(
         state->ia->primitive_topology));

   for (uint8_t i = 0; i < state->rp->color_attachment_count; ++i) {
      if (state->rp->color_attachment_formats[i] != VK_FORMAT_UNDEFINED)
         mtl_render_pipeline_descriptor_set_color_attachment_format(
            pipeline_descriptor, i,
            vk_format_to_mtl_pixel_format(
               state->rp->color_attachment_formats[i]));
   }

   if (state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED)
      mtl_render_pipeline_descriptor_set_depth_attachment_format(
         pipeline_descriptor,
         vk_format_to_mtl_pixel_format(state->rp->depth_attachment_format));

   if (state->rp->stencil_attachment_format != VK_FORMAT_UNDEFINED)
      mtl_render_pipeline_descriptor_set_stencil_attachment_format(
         pipeline_descriptor,
         vk_format_to_mtl_pixel_format(state->rp->stencil_attachment_format));

   if (has_static_depth_stencil_state(state)) {
      bool has_depth =
         state->rp->depth_attachment_format != VK_FORMAT_UNDEFINED;
      bool has_stencil =
         state->rp->stencil_attachment_format != VK_FORMAT_UNDEFINED;
      vertex_shader->pipeline.gfx.mtl_depth_stencil_state_handle =
         kk_compile_depth_stencil_state(device, state->ds, has_depth,
                                        has_stencil);
   }

   if (state->rp->view_mask) {
      uint32_t max_amplification = util_bitcount(state->rp->view_mask);
      mtl_render_pipeline_descriptor_set_max_vertex_amplification_count(
         pipeline_descriptor, max_amplification);
   }

   if (state->ms) {
      mtl_render_pipeline_descriptor_set_raster_sample_count(
         pipeline_descriptor, state->ms->rasterization_samples);
      mtl_render_pipeline_descriptor_set_alpha_to_coverage(
         pipeline_descriptor, state->ms->alpha_to_coverage_enable);
      mtl_render_pipeline_descriptor_set_alpha_to_one(
         pipeline_descriptor, state->ms->alpha_to_one_enable);
   }

   vertex_shader->pipeline.gfx.handle =
      mtl_new_render_pipeline(device->mtl_handle, pipeline_descriptor);
   if (vertex_shader->pipeline.gfx.handle == NULL)
      result = VK_ERROR_INVALID_SHADER_NV;
   vertex_shader->pipeline.gfx.primitive_type =
      vk_primitive_topology_to_mtl_primitive_type(
         state->ia->primitive_topology);

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
   nir_shader *null_fs = NULL;
   nir_shader *shaders[shader_count + 1u];

   /* Lower shaders, notably lowering IO. This is a prerequisite for intershader
    * optimization. */
   for (uint32_t i = 0u; i < shader_count; ++i) {
      const struct vk_shader_compile_info *info = &infos[i];
      nir_shader *nir = info->nir;

      kk_lower_nir(dev, nir, info->robustness, info->set_layout_count,
                   info->set_layouts, state);

      shaders[i] = nir;
   }

   /* Since we don't support GPL nor shader objects and Metal render pipelines
    * require both vertex and fragment, we may need to provide a pass-through
    * fragment. */
   if (state &&
       shaders[shader_count - 1u]->info.stage != MESA_SHADER_FRAGMENT) {
      null_fs = get_empty_nir(dev, MESA_SHADER_FRAGMENT, state);
      shaders[shader_count] = null_fs;
   }

   uint32_t total_shaders = null_fs ? shader_count + 1 : shader_count;
   nir_opt_varyings_bulk(shaders, total_shaders, true, UINT32_MAX, UINT32_MAX,
                         nir_opts);
   /* Second pass is required because some dEQP-VK.glsl.matrix.sub.dynamic.*
    * would fail otherwise due to vertex outputting vec4 while fragments reading
    * vec3 when in reality only vec3 is needed. */
   nir_opt_varyings_bulk(shaders, total_shaders, true, UINT32_MAX, UINT32_MAX,
                         nir_opts);

   for (uint32_t i = 0; i < shader_count; i++) {
      result =
         kk_compile_shader(dev, &infos[i], state, pAllocator, &shaders_out[i]);
      if (result != VK_SUCCESS) {
         /* Clean up all the shaders before this point */
         for (uint32_t j = 0; j < i; j++)
            kk_shader_destroy(&dev->vk, shaders_out[j], pAllocator);

         /* Clean up all the NIR after this point */
         for (uint32_t j = i + 1; j < shader_count; j++)
            ralloc_free(shaders[j]);

         if (null_fs)
            ralloc_free(null_fs);

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
   if (shaders_out[0]->stage == MESA_SHADER_COMPUTE) {
      result = kk_compile_compute_pipeline(
         dev, container_of(shaders_out[0], struct kk_shader, vk));
   } else {
      struct kk_shader *vs = container_of(shaders_out[0], struct kk_shader, vk);
      struct kk_shader *fs =
         container_of(shaders_out[shader_count - 1u], struct kk_shader, vk);
      if (null_fs) {
         struct vk_shader_compile_info info = {
            .stage = MESA_SHADER_FRAGMENT,
            .nir = null_fs,
            .robustness = &rs_none,
         };
         struct vk_shader *frag_shader;
         result =
            kk_compile_shader(dev, &info, state, &dev->vk.alloc, &frag_shader);

         if (result != VK_SUCCESS) {
            for (uint32_t i = 0; i < shader_count; i++)
               kk_shader_destroy(&dev->vk, shaders_out[i], pAllocator);

            /* Memset the output array */
            memset(shaders_out, 0, shader_count * sizeof(*shaders_out));

            return result;
         }
         fs = container_of(frag_shader, struct kk_shader, vk);
      }

      result = kk_compile_graphics_pipeline(dev, vs, fs, state);

      if (null_fs)
         kk_shader_destroy(&dev->vk, &fs->vk, pAllocator);
   }

   return result;
}

static bool
kk_shader_serialize(struct vk_device *vk_dev, const struct vk_shader *vk_shader,
                    struct blob *blob)
{
   struct kk_shader *shader = container_of(vk_shader, struct kk_shader, vk);

   blob_write_bytes(blob, &shader->info, sizeof(shader->info));
   uint32_t entrypoint_length = strlen(shader->entrypoint_name) + 1;
   blob_write_bytes(blob, &entrypoint_length, sizeof(entrypoint_length));
   uint32_t code_length = strlen(shader->msl_code) + 1;
   blob_write_bytes(blob, &code_length, sizeof(code_length));
   blob_write_bytes(blob, shader->entrypoint_name, entrypoint_length);
   blob_write_bytes(blob, shader->msl_code, code_length);
   blob_write_bytes(blob, &shader->pipeline, sizeof(shader->pipeline));

   /* We are building a new shader into the cache so we need to retain resources
    */
   if (shader->info.stage == MESA_SHADER_COMPUTE)
      mtl_retain(shader->pipeline.cs);
   else if (shader->info.stage == MESA_SHADER_VERTEX) {
      mtl_retain(shader->pipeline.gfx.handle);
      if (shader->pipeline.gfx.mtl_depth_stencil_state_handle)
         mtl_retain(shader->pipeline.gfx.mtl_depth_stencil_state_handle);
   }

   return !blob->out_of_memory;
}

static VkResult
kk_deserialize_shader(struct vk_device *vk_dev, struct blob_reader *blob,
                      uint32_t binary_version,
                      const VkAllocationCallbacks *pAllocator,
                      struct vk_shader **shader_out)
{
   struct kk_device *dev = container_of(vk_dev, struct kk_device, vk);
   struct kk_shader *shader;

   struct kk_shader_info info;
   blob_copy_bytes(blob, &info, sizeof(info));

   const uint32_t entrypoint_length = blob_read_uint32(blob);
   const uint32_t code_length = blob_read_uint32(blob);
   if (blob->overrun)
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader = vk_shader_zalloc(&dev->vk, &kk_shader_ops, info.stage, pAllocator,
                             sizeof(*shader));
   if (shader == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   shader->info = info;

   shader->entrypoint_name = ralloc_array(NULL, char, entrypoint_length);
   if (shader->entrypoint_name == NULL) {
      kk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   shader->msl_code = ralloc_array(NULL, char, code_length);
   if (shader->msl_code == NULL) {
      kk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   blob_copy_bytes(blob, (void *)shader->entrypoint_name, entrypoint_length);
   blob_copy_bytes(blob, (void *)shader->msl_code, code_length);
   blob_copy_bytes(blob, &shader->pipeline, sizeof(shader->pipeline));
   if (blob->overrun) {
      kk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
   }

   /* We are building a new shader so we need to retain resources */
   if (info.stage == MESA_SHADER_COMPUTE)
      mtl_retain(shader->pipeline.cs);
   else if (info.stage == MESA_SHADER_VERTEX) {
      mtl_retain(shader->pipeline.gfx.handle);
      if (shader->pipeline.gfx.mtl_depth_stencil_state_handle)
         mtl_retain(shader->pipeline.gfx.mtl_depth_stencil_state_handle);
   }

   *shader_out = &shader->vk;

   return VK_SUCCESS;
}

static void
kk_cmd_bind_compute_shader(struct kk_cmd_buffer *cmd, struct kk_shader *shader)
{
   cmd->state.cs.pipeline_state = shader->pipeline.cs;
   cmd->state.cs.dirty |= KK_DIRTY_PIPELINE;
   cmd->state.cs.local_size = shader->info.cs.local_size;
}

static void
kk_cmd_bind_graphics_shader(struct kk_cmd_buffer *cmd,
                            const mesa_shader_stage stage,
                            struct kk_shader *shader)
{
   /* Relevant pipeline data is only stored in vertex shaders */
   if (stage != MESA_SHADER_VERTEX)
      return;

   cmd->state.gfx.primitive_type = shader->pipeline.gfx.primitive_type;
   cmd->state.gfx.pipeline_state = shader->pipeline.gfx.handle;
   cmd->state.gfx.vb.attribs_read = shader->info.vs.attribs_read;

   bool requires_dynamic_depth_stencil =
      shader->pipeline.gfx.mtl_depth_stencil_state_handle == NULL;
   if (cmd->state.gfx.is_depth_stencil_dynamic) {
      /* If we are switching from dynamic to static, we need to clean up
       * temporary state. Otherwise, leave the existing dynamic state
       * untouched.
       */
      if (!requires_dynamic_depth_stencil) {
         mtl_release(cmd->state.gfx.depth_stencil_state);
         cmd->state.gfx.depth_stencil_state =
            shader->pipeline.gfx.mtl_depth_stencil_state_handle;
      }
   } else
      cmd->state.gfx.depth_stencil_state =
         shader->pipeline.gfx.mtl_depth_stencil_state_handle;
   cmd->state.gfx.is_depth_stencil_dynamic = requires_dynamic_depth_stencil;
   cmd->state.gfx.dirty |= KK_DIRTY_PIPELINE;
   cmd->state.gfx.dirty |= KK_DIRTY_VB;
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

static const struct vk_shader_ops kk_shader_ops = {
   .destroy = kk_shader_destroy,
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
      kk_compile_shaders, /* This will only generate the MSL string we need to
                             use for actual library generation */
   .deserialize = kk_deserialize_shader,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_bind_shaders = kk_cmd_bind_shaders,
};
