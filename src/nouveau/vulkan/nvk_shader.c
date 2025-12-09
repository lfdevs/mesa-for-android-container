/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_shader.h"

#include "nvk_cmd_buffer.h"
#include "nvk_descriptor_set_layout.h"
#include "nvk_device.h"
#include "nvk_mme.h"
#include "nvk_physical_device.h"
#include "nvk_sampler.h"
#include "nvk_shader.h"

#include "vk_nir_convert_ycbcr.h"
#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"
#include "vk_shader_module.h"
#include "vk_ycbcr_conversion.h"

#include "nak.h"
#include "nir.h"
#include "nir_builder.h"
#include "compiler/spirv/nir_spirv.h"

#include "util/mesa-sha1.h"
#include "util/u_debug.h"

#include "cla097.h"
#include "clb097.h"
#include "clc597.h"
#include "nv_push_cl9097.h"
#include "nv_push_clb197.h"
#include "nv_push_clc397.h"
#include "nv_push_clc797.h"

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size;
}

uint64_t
nvk_physical_device_compiler_flags(const struct nvk_physical_device *pdev)
{
   bool no_cbufs = pdev->debug_flags & NVK_DEBUG_NO_CBUF;
   bool use_edb_buffer_views = nvk_use_edb_buffer_views(pdev);
   uint64_t nak_flags = nak_debug_flags(pdev->nak);

   assert(nak_flags <= UINT16_MAX);

   return ((uint64_t)no_cbufs << 12)
      | ((uint64_t)use_edb_buffer_views << 13)
      | (nak_flags << 48);
}

static const nir_shader_compiler_options *
nvk_get_nir_options(struct vk_physical_device *vk_pdev,
                    mesa_shader_stage stage,
                    UNUSED const struct vk_pipeline_robustness_state *rs)
{
   const struct nvk_physical_device *pdev =
      container_of(vk_pdev, struct nvk_physical_device, vk);
   return nak_nir_options(pdev->nak);
}

nir_address_format
nvk_ubo_addr_format(const struct nvk_physical_device *pdev,
                    const struct vk_pipeline_robustness_state *rs)
{
   if (nvk_use_bindless_cbuf(&pdev->info)) {
      return nir_address_format_vec2_index_32bit_offset;
   } else if (rs->null_uniform_buffer_descriptor) {
      /* We need bounds checking for null descriptors */
      return nir_address_format_64bit_bounded_global;
   } else {
      switch (rs->uniform_buffers) {
      case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
         return nir_address_format_64bit_global_32bit_offset;
      case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
      case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
         return nir_address_format_64bit_bounded_global;
      default:
         UNREACHABLE("Invalid robust buffer access behavior");
      }
   }
}

nir_address_format
nvk_ssbo_addr_format(const struct nvk_physical_device *pdev,
                    const struct vk_pipeline_robustness_state *rs)
{
   if (rs->null_storage_buffer_descriptor) {
      /* We need bounds checking for null descriptors */
      return nir_address_format_64bit_bounded_global;
   } else {
      switch (rs->storage_buffers) {
      case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
         return nir_address_format_64bit_global_32bit_offset;
      case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
      case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
         return nir_address_format_64bit_bounded_global;
      default:
         UNREACHABLE("Invalid robust buffer access behavior");
      }
   }
}

static struct spirv_to_nir_options
nvk_get_spirv_options(struct vk_physical_device *vk_pdev,
                      UNUSED mesa_shader_stage stage,
                      const struct vk_pipeline_robustness_state *rs)
{
   const struct nvk_physical_device *pdev =
      container_of(vk_pdev, struct nvk_physical_device, vk);

   return (struct spirv_to_nir_options) {
      .ssbo_addr_format = nvk_ssbo_addr_format(pdev, rs),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .ubo_addr_format = nvk_ubo_addr_format(pdev, rs),
      .shared_addr_format = nir_address_format_32bit_offset,
      .min_ssbo_alignment = NVK_MIN_SSBO_ALIGNMENT,
      .min_ubo_alignment = nvk_min_cbuf_alignment(&pdev->info),
   };
}

static void
nvk_preprocess_nir(struct vk_physical_device *vk_pdev,
                   nir_shader *nir,
                   UNUSED const struct vk_pipeline_robustness_state *rs)
{
   const struct nvk_physical_device *pdev =
      container_of(vk_pdev, struct nvk_physical_device, vk);

   nak_preprocess_nir(nir, pdev->nak);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      nir_input_attachment_options ia_opts = {
         .use_ia_coord_intrin = true,
      };
      NIR_PASS(_, nir, nir_lower_input_attachments, &ia_opts);
   }
}

static void
nvk_populate_fs_key(struct nak_fs_key *key,
                    const struct vk_graphics_pipeline_state *state)
{
   memset(key, 0, sizeof(*key));

   key->sample_info_cb = 0;
   key->sample_locations_offset = nvk_root_descriptor_offset(draw.sample_locations);
   key->sample_masks_offset = nvk_root_descriptor_offset(draw.sample_masks);

   /* Turn underestimate on when no state is availaible or if explicitly set */
   if (state == NULL || state->rs == NULL ||
       state->rs->conservative_mode == VK_CONSERVATIVE_RASTERIZATION_MODE_UNDERESTIMATE_EXT)
      key->uses_underestimate = true;

   if (state == NULL)
      return;

   if (state->pipeline_flags &
       VK_PIPELINE_CREATE_2_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)
      key->zs_self_dep = true;

   /* We force per-sample interpolation whenever sampleShadingEnable is set
    * regardless of minSampleShading or rasterizationSamples.
    *
    * When sampleShadingEnable is set, few guarantees are made about the
    * location of interpolation of the inputs.  The only real guarantees are
    * that the inputs are interpolated within the pixel and that you get at
    * least `rasterizationSamples * minSampleShading` unique positions.
    * Importantly, it does not require that when `rasterizationSamples *
    * minSampleShading <= 1.0` that those positions are at the fragment
    * center.  Therefore, it's valid to just always do per-sample (which maps
    * to CENTROID on NVIDIA hardware) all the time and let the hardware sort
    * it out based on what we set in HYBRID_ANTI_ALIAS_CONTROL::passes.
    *
    * Also, we set HYBRID_ANTI_ALIAS_CONTROL::centroid at draw time based on
    * `rasterizationSamples * minSampleShading` so it should be per-pixel
    * whenever we're running only a single pass.  However, this would still be
    * correct even if it got interpolated at some other sample.
    *
    * The one caveat here is that we have to be careful about gl_SampleMaskIn.
    * When `nak_fs_key::force_sample_shading = true` we also turn any reads of
    * gl_SampleMaskIn into `1 << gl_SampleID` because the hardware sample mask
    * is actually per-fragment, not per-pass.  We handle this by smashing
    * minSampleShading to 1.0 whenever gl_SampleMaskIn is read.
    */
   const struct vk_multisample_state *ms = state->ms;
   if (ms != NULL && ms->sample_shading_enable)
      key->force_sample_shading = true;
}

static void
nvk_hash_state(struct vk_physical_device *device,
               const struct vk_graphics_pipeline_state *state,
               const struct vk_features *enabled_features,
               VkShaderStageFlags stages,
               blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);
   if (state && (stages & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      struct nak_fs_key key;
      nvk_populate_fs_key(&key, state);
      _mesa_blake3_update(&blake3_ctx, &key, sizeof(key));

      /* This doesn't impact the shader compile but it does go in the
       * nvk_shader and gets [de]serialized along with the binary so we
       * need to hash it.
       */
      if (state->ms && state->ms->sample_shading_enable) {
         _mesa_blake3_update(&blake3_ctx, &state->ms->min_sample_shading,
                             sizeof(state->ms->min_sample_shading));
      }
   }
   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

static bool
lower_load_intrinsic(nir_builder *b, nir_intrinsic_instr *load,
                     UNUSED void *_data)
{
   switch (load->intrinsic) {
   case nir_intrinsic_load_ubo: {
      b->cursor = nir_before_instr(&load->instr);

      nir_def *index = load->src[0].ssa;
      nir_def *offset = load->src[1].ssa;
      const enum gl_access_qualifier access = nir_intrinsic_access(load);
      const uint32_t align_mul = nir_intrinsic_align_mul(load);
      const uint32_t align_offset = nir_intrinsic_align_offset(load);

      nir_def *val;
      if (load->src[0].ssa->num_components == 1) {
         val = nir_ldc_nv(b, load->num_components, load->def.bit_size,
                           index, offset, .access = access,
                           .align_mul = align_mul,
                           .align_offset = align_offset);
      } else if (load->src[0].ssa->num_components == 2) {
         nir_def *handle = nir_pack_64_2x32(b, load->src[0].ssa);
         val = nir_ldcx_nv(b, load->num_components, load->def.bit_size,
                           handle, offset, .access = access,
                           .align_mul = align_mul,
                           .align_offset = align_offset);
      } else {
         UNREACHABLE("Invalid UBO index");
      }
      nir_def_rewrite_uses(&load->def, val);
      return true;
   }

   case nir_intrinsic_load_global_constant_offset:
   case nir_intrinsic_load_global_constant_bounded: {
      b->cursor = nir_before_instr(&load->instr);

      nir_def *base_addr = load->src[0].ssa;
      nir_def *offset = load->src[1].ssa;

      nir_def *zero = NULL;
      if (load->intrinsic == nir_intrinsic_load_global_constant_bounded) {
         nir_def *bound = load->src[2].ssa;

         unsigned bit_size = load->def.bit_size;
         assert(bit_size >= 8 && bit_size % 8 == 0);
         unsigned byte_size = bit_size / 8;

         zero = nir_imm_zero(b, load->num_components, bit_size);

         unsigned load_size = byte_size * load->num_components;

         nir_def *sat_offset =
            nir_umin(b, offset, nir_imm_int(b, UINT32_MAX - (load_size - 1)));
         nir_def *in_bounds =
            nir_ilt(b, nir_iadd_imm(b, sat_offset, load_size - 1), bound);

         nir_push_if(b, in_bounds);
      }

      nir_def *val =
         nir_load_global_constant(b, load->def.num_components,
                                        load->def.bit_size,
                                        nir_iadd(b, base_addr, nir_u2u64(b, offset)),
                                        .align_mul = nir_intrinsic_align_mul(load),
                                        .align_offset = nir_intrinsic_align_offset(load));

      if (load->intrinsic == nir_intrinsic_load_global_constant_bounded) {
         nir_pop_if(b, NULL);
         val = nir_if_phi(b, val, zero);
      }

      nir_def_rewrite_uses(&load->def, val);
      return true;
   }

   default:
      return false;
   }
}

struct lower_ycbcr_state {
   uint32_t set_layout_count;
   struct vk_descriptor_set_layout * const *set_layouts;
};

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *_state, uint32_t set,
                        uint32_t binding, uint32_t array_index)
{
   const struct lower_ycbcr_state *state = _state;
   assert(set < state->set_layout_count);
   assert(state->set_layouts[set] != NULL);
   const struct nvk_descriptor_set_layout *set_layout =
      vk_to_nvk_descriptor_set_layout(state->set_layouts[set]);
   assert(binding < set_layout->binding_count);

   const struct nvk_descriptor_set_binding_layout *bind_layout =
      &set_layout->binding[binding];

   if (bind_layout->immutable_samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->array_size - 1);

   const struct nvk_sampler *sampler =
      bind_layout->immutable_samplers[array_index];

   return sampler && sampler->vk.ycbcr_conversion ?
          &sampler->vk.ycbcr_conversion->state : NULL;
}

static void
nvk_lower_nir(struct nvk_device *dev, nir_shader *nir,
              VkShaderCreateFlagsEXT shader_flags,
              const struct vk_pipeline_robustness_state *rs,
              uint32_t set_layout_count,
              struct vk_descriptor_set_layout * const *set_layouts,
              struct nvk_cbuf_map *cbuf_map_out)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   if (nir->info.stage == MESA_SHADER_TESS_EVAL) {
      NIR_PASS(_, nir, nir_lower_patch_vertices,
               nir->info.tess.tcs_vertices_out, NULL);
   }

   const struct lower_ycbcr_state ycbcr_state = {
      .set_layout_count = set_layout_count,
      .set_layouts = set_layouts,
   };
   NIR_PASS(_, nir, nir_vk_lower_ycbcr_tex,
            lookup_ycbcr_conversion, &ycbcr_state);

   nir_lower_compute_system_values_options csv_options = {
      .has_base_workgroup_id = true,
   };
   NIR_PASS(_, nir, nir_lower_compute_system_values, &csv_options);

   /* Lower push constants before lower_descriptors */
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);

   struct nvk_cbuf_map *cbuf_map = NULL;
   if (!(pdev->debug_flags & NVK_DEBUG_NO_CBUF)) {
      cbuf_map = cbuf_map_out;

      /* Large constant support assumes cbufs */
      NIR_PASS(_, nir, nir_opt_large_constants, NULL, 32);
   } else {
      *cbuf_map_out = (struct nvk_cbuf_map) {
         .cbuf_count = 1,
         .cbufs = {
            { .type = NVK_CBUF_TYPE_ROOT_DESC },
         }
      };
   }

   nir_opt_access_options opt_access_options = {
      .is_vulkan = true,
   };
   NIR_PASS(_, nir, nir_opt_access, &opt_access_options);

   /* On Kepler, we have to lower images to addresses */
   if (pdev->info.cls_eng3d < MAXWELL_A)
      NIR_PASS(_, nir, nak_nir_lower_image_addrs, pdev->nak);

   NIR_PASS(_, nir, nvk_nir_lower_descriptors, pdev, shader_flags, rs,
            set_layout_count, set_layouts, cbuf_map);

   if (nvk_use_bindless_cbuf(&pdev->info)) {
      /* On Turing+ where we have bindless cbufs, we use ACCESS_NON_UNIFORM to
       * determine whether or not it's safe to assume a uniform handle so we
       * want to optimize it away whenever possible.
       */
      if (nir_has_non_uniform_access(nir, nir_lower_non_uniform_ubo_access))
         NIR_PASS(_, nir, nir_opt_non_uniform_access);
   }

   if (pdev->info.cls_eng3d < TURING_A) {
      /* NOTE: This does nothing for images on Kepler since those are lowered
       * to suldga/sustga before we get here.  That's fine, though, because
       * our nil_su_info fetches and calculations work fine with non-uniform
       * descriptors.
       */
      struct nir_lower_non_uniform_access_options opts = {
         .types = nir_lower_non_uniform_texture_access |
                  nir_lower_non_uniform_image_access,
         .callback = NULL,
      };
      /* In practice, most shaders do not have non-uniform-qualified accesses
       * thus a cheaper and likely to fail check is run first.
       */
      if (nir_has_non_uniform_access(nir, opts.types)) {
         NIR_PASS(_, nir, nir_opt_non_uniform_access);
         NIR_PASS(_, nir, nir_lower_non_uniform_access, &opts);
      }
   }

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_global,
            nir_address_format_64bit_global);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ssbo,
            nvk_ssbo_addr_format(pdev, rs));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo,
            nvk_ubo_addr_format(pdev, rs));
   NIR_PASS(_, nir, nir_shader_intrinsics_pass,
            lower_load_intrinsic, nir_metadata_none, NULL);

   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_mem_shared, shared_var_info);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared,
            nir_address_format_32bit_offset);

   if (nir->info.zero_initialize_shared_memory && nir->info.shared_size > 0) {
      /* QMD::SHARED_MEMORY_SIZE requires an alignment of 256B so it's safe to
       * align everything up to 16B so we can write whole vec4s.
       */
      nir->info.shared_size = align(nir->info.shared_size, 16);
      NIR_PASS(_, nir, nir_zero_initialize_shared_memory,
               nir->info.shared_size, 16);

      /* We need to call lower_compute_system_values again because
       * nir_zero_initialize_shared_memory generates load_invocation_id which
       * has to be lowered to load_invocation_index.
       */
      NIR_PASS(_, nir, nir_lower_compute_system_values, NULL);
   }
}

#ifndef NDEBUG
static void
nvk_shader_dump(struct nvk_shader *shader)
{
   unsigned pos;

   if (shader->info.stage != MESA_SHADER_COMPUTE) {
      _debug_printf("dumping HDR for %s shader\n",
                    _mesa_shader_stage_to_string(shader->info.stage));
      for (pos = 0; pos < ARRAY_SIZE(shader->info.hdr); ++pos)
         _debug_printf("HDR[%02"PRIxPTR"] = 0x%08x\n",
                      pos * sizeof(shader->info.hdr[0]), shader->info.hdr[pos]);
   }
   _debug_printf("shader binary code (0x%x bytes):", shader->code_size);
   for (pos = 0; pos < shader->code_size / 4; ++pos) {
      if ((pos % 8) == 0)
         _debug_printf("\n");
      _debug_printf("%08x ", ((const uint32_t *)shader->code_ptr)[pos]);
   }
   _debug_printf("\n");
}
#endif

static VkResult
nvk_compile_nir(struct nvk_device *dev, nir_shader *nir,
                VkShaderCreateFlagsEXT shader_flags,
                const struct vk_pipeline_robustness_state *rs,
                const struct nak_fs_key *fs_key,
                struct nvk_shader *shader)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   const bool dump_asm =
      shader_flags & VK_SHADER_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_MESA;

   nir_variable_mode robust2_modes = 0;
   if (rs->uniform_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT)
      robust2_modes |= nir_var_mem_ubo;
   if (rs->storage_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT)
      robust2_modes |= nir_var_mem_ssbo;

   shader->nak = nak_compile_shader(nir, dump_asm, pdev->nak,
                                    robust2_modes, fs_key);
   if (!shader->nak)
      return vk_errorf(pdev, VK_ERROR_UNKNOWN, "Internal compiler error in NAK");

   shader->info = shader->nak->info;
   shader->code_ptr = shader->nak->code;
   shader->code_size = shader->nak->code_size;

   if (nir->constant_data_size > 0) {
      uint32_t data_align = nvk_min_cbuf_alignment(&pdev->info);
      uint32_t data_size = align(nir->constant_data_size, data_align);

      void *data = malloc(data_size);
      if (data == NULL)
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

      memcpy(data, nir->constant_data, nir->constant_data_size);

      assert(nir->constant_data_size <= data_size);
      memset(data + nir->constant_data_size, 0,
             data_size - nir->constant_data_size);

      shader->data_ptr = data;
      shader->data_size = data_size;
   }

   return VK_SUCCESS;
}

static VkResult
nvk_shader_upload(struct nvk_device *dev, struct nvk_shader *shader)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   uint32_t hdr_size = 0;
   if (shader->info.stage != MESA_SHADER_COMPUTE) {
      if (pdev->info.cls_eng3d >= TURING_A)
         hdr_size = TU102_SHADER_HEADER_SIZE;
      else
         hdr_size = GF100_SHADER_HEADER_SIZE;
   }

   /* Fermi   needs 0x40 alignment
    * Kepler+ needs the first instruction to be 0x80 aligned, so we waste 0x30 bytes
    */
   int alignment = pdev->info.cls_eng3d >= KEPLER_A ? 0x80 : 0x40;

   uint32_t total_size = 0;
   if (pdev->info.cls_eng3d >= KEPLER_A &&
       pdev->info.cls_eng3d < TURING_A &&
       hdr_size > 0) {
      /* The instructions are what has to be aligned so we need to start at a
       * small offset (0x30 B) into the upload area.
       */
      total_size = alignment - hdr_size;
   }

   const uint32_t hdr_offset = total_size;
   total_size += hdr_size;

   const uint32_t code_offset = total_size;
   assert(code_offset % alignment == 0);
   total_size += shader->code_size;

   uint32_t data_offset = 0;
   if (shader->data_size > 0) {
      uint32_t cbuf_alignment = nvk_min_cbuf_alignment(&pdev->info);
      alignment = MAX2(alignment, cbuf_alignment);
      total_size = align(total_size, cbuf_alignment);
      data_offset = total_size;
      total_size += shader->data_size;
   }

   char *data = malloc(total_size);
   if (data == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(hdr_size <= sizeof(shader->info.hdr));
   memcpy(data + hdr_offset, shader->info.hdr, hdr_size);
   memcpy(data + code_offset, shader->code_ptr, shader->code_size);
   if (shader->data_size > 0)
      memcpy(data + data_offset, shader->data_ptr, shader->data_size);

#ifndef NDEBUG
   if (debug_get_bool_option("NV50_PROG_DEBUG", false))
      nvk_shader_dump(shader);
#endif

   VkResult result = nvk_heap_upload(dev, &dev->shader_heap, data,
                                     total_size, alignment,
                                     &shader->upload_addr);
   if (result == VK_SUCCESS) {
      shader->upload_size = total_size;

      shader->hdr_addr = shader->upload_addr + hdr_offset;
      if (pdev->info.cls_eng3d < VOLTA_A) {
         const uint64_t heap_base_addr =
            nvk_heap_contiguous_base_address(&dev->shader_heap);
         assert(shader->upload_addr - heap_base_addr < UINT32_MAX);
         shader->hdr_addr -= heap_base_addr;
      }
      shader->data_addr = shader->upload_addr + data_offset;
   }
   free(data);

   return result;
}

uint32_t
mesa_to_nv9097_shader_type(mesa_shader_stage stage)
{
   static const uint32_t mesa_to_nv9097[] = {
      [MESA_SHADER_VERTEX]    = NV9097_SET_PIPELINE_SHADER_TYPE_VERTEX,
      [MESA_SHADER_TESS_CTRL] = NV9097_SET_PIPELINE_SHADER_TYPE_TESSELLATION_INIT,
      [MESA_SHADER_TESS_EVAL] = NV9097_SET_PIPELINE_SHADER_TYPE_TESSELLATION,
      [MESA_SHADER_GEOMETRY]  = NV9097_SET_PIPELINE_SHADER_TYPE_GEOMETRY,
      [MESA_SHADER_FRAGMENT]  = NV9097_SET_PIPELINE_SHADER_TYPE_PIXEL,
   };
   assert(stage < ARRAY_SIZE(mesa_to_nv9097));
   return mesa_to_nv9097[stage];
}

uint32_t
nvk_pipeline_bind_group(mesa_shader_stage stage)
{
   return stage;
}

uint16_t
nvk_max_shader_push_dw(const struct nvk_physical_device *pdev,
                       mesa_shader_stage stage, bool last_vtgm)
{
   if (stage == MESA_SHADER_COMPUTE)
      return 0;

   uint16_t max_dw_count = 8;

   if (stage == MESA_SHADER_TESS_EVAL)
      max_dw_count += 2;

   if (stage == MESA_SHADER_FRAGMENT)
      max_dw_count += 13;

   if (last_vtgm) {
      max_dw_count += 8;
      max_dw_count += 4 * (5 + (128 / 4));
   }

   return max_dw_count;
}

static VkResult
nvk_shader_fill_push(struct nvk_device *dev,
                     struct nvk_shader *shader,
                     const VkAllocationCallbacks* pAllocator)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   ASSERTED uint16_t max_dw_count = 0;
   uint32_t push_dw[200];
   struct nv_push push, *p = &push;
   nv_push_init(&push, push_dw, ARRAY_SIZE(push_dw),
                nvk_queue_subchannels_from_engines(NVKMD_ENGINE_3D));

   const uint32_t type = mesa_to_nv9097_shader_type(shader->info.stage);

   /* We always map index == type */
   const uint32_t idx = type;

   max_dw_count += 2;
   P_IMMD(p, NV9097, SET_PIPELINE_SHADER(idx), {
      .enable  = ENABLE_TRUE,
      .type    = type,
   });

   max_dw_count += 3;
   uint64_t addr = shader->hdr_addr;
   if (pdev->info.cls_eng3d >= VOLTA_A) {
      P_MTHD(p, NVC397, SET_PIPELINE_PROGRAM_ADDRESS_A(idx));
      P_NVC397_SET_PIPELINE_PROGRAM_ADDRESS_A(p, idx, addr >> 32);
      P_NVC397_SET_PIPELINE_PROGRAM_ADDRESS_B(p, idx, addr);
   } else {
      assert(addr < 0xffffffff);
      P_IMMD(p, NV9097, SET_PIPELINE_PROGRAM(idx), addr);
   }

   max_dw_count += 3;
   P_MTHD(p, NVC397, SET_PIPELINE_REGISTER_COUNT(idx));
   P_NVC397_SET_PIPELINE_REGISTER_COUNT(p, idx, shader->info.num_gprs);
   P_NVC397_SET_PIPELINE_BINDING(p, idx,
      nvk_pipeline_bind_group(shader->info.stage));

   if (shader->info.stage == MESA_SHADER_TESS_EVAL) {
      max_dw_count += 2;
      P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_TESS_PARAMS));
      P_INLINE_DATA(p, nvk_mme_tess_params(shader->info.ts.domain,
                                           shader->info.ts.spacing,
                                           shader->info.ts.prims));
   }

   if (shader->info.stage == MESA_SHADER_FRAGMENT) {
      max_dw_count += 13;

      P_MTHD(p, NVC397, SET_SUBTILING_PERF_KNOB_A);
      P_NV9097_SET_SUBTILING_PERF_KNOB_A(p, {
         .fraction_of_spm_register_file_per_subtile         = 0x10,
         .fraction_of_spm_pixel_output_buffer_per_subtile   = 0x40,
         .fraction_of_spm_triangle_ram_per_subtile          = 0x16,
         .fraction_of_max_quads_per_subtile                 = 0x20,
      });
      P_NV9097_SET_SUBTILING_PERF_KNOB_B(p, 0x20);

      P_IMMD(p, NV9097, SET_API_MANDATED_EARLY_Z,
             shader->info.fs.early_fragment_tests);

      if (pdev->info.cls_eng3d >= MAXWELL_B) {
         P_IMMD(p, NVB197, SET_POST_Z_PS_IMASK,
                shader->info.fs.post_depth_coverage);
      } else {
         assert(!shader->info.fs.post_depth_coverage);
      }

      P_IMMD(p, NV9097, SET_ZCULL_BOUNDS, {
         .z_min_unbounded_enable = shader->info.fs.writes_depth,
         .z_max_unbounded_enable = shader->info.fs.writes_depth,
      });

      if (pdev->info.cls_eng3d >= TURING_A) {
         /* From the Vulkan 1.3.297 spec:
          *
          *    "If sample shading is enabled, an implementation must invoke
          *    the fragment shader at least
          *
          *    max( ⌈ minSampleShading × rasterizationSamples ⌉, 1)
          *
          *    times per fragment."
          *
          * The max() here means that, regardless of the actual value of
          * minSampleShading, we need to invoke at least once per pixel,
          * meaning that we need to disable fragment shading rate.  We also
          * need to disable FSR if sample shading is used by the shader.
          */
         P_1INC(p, NV9097, CALL_MME_MACRO(NVK_MME_SET_SHADING_RATE_CONTROL));
         P_INLINE_DATA(p, nvk_mme_shading_rate_control_sample_shading(
            shader->sample_shading_enable ||
            shader->info.fs.uses_sample_shading));
      }

      float mss = 0;
      if (shader->info.fs.uses_sample_shading) {
         mss = 1;
      } else if (shader->sample_shading_enable) {
         mss = CLAMP(shader->min_sample_shading, 0, 1);
      } else {
         mss = 0;
      }
      P_1INC(p, NVB197, CALL_MME_MACRO(NVK_MME_SET_ANTI_ALIAS));
      P_INLINE_DATA(p, nvk_mme_anti_alias_min_sample_shading(mss));
   }

   /* Stash this before we do XFB and clip/cull */
   shader->push_dw_count = nv_push_dw_count(&push);
   assert(max_dw_count ==
          nvk_max_shader_push_dw(pdev, shader->info.stage, false));

   if (shader->info.stage != MESA_SHADER_FRAGMENT &&
       shader->info.stage != MESA_SHADER_TESS_CTRL) {
      max_dw_count += 8;

      P_IMMD(p, NV9097, SET_RT_LAYER, {
         .v       = 0,
         .control = shader->info.vtg.writes_layer ?
                    CONTROL_GEOMETRY_SHADER_SELECTS_LAYER :
                    CONTROL_V_SELECTS_LAYER,
      });

      if (pdev->info.cls_eng3d >= AMPERE_B) {
         P_IMMD(p, NVC797, SET_VARIABLE_PIXEL_RATE_SHADING_TABLE_SELECT, {
            .source = shader->info.vtg.writes_vprs_table_index ?
                      SOURCE_FROM_VPRS_TABLE_INDEX :
                      SOURCE_FROM_CONSTANT,
            .source_constant_value = 0,
         });
      }

      const uint8_t clip_enable = shader->info.vtg.clip_enable;
      const uint8_t cull_enable = shader->info.vtg.cull_enable;
      P_IMMD(p, NV9097, SET_USER_CLIP_ENABLE, {
         .plane0 = ((clip_enable | cull_enable) >> 0) & 1,
         .plane1 = ((clip_enable | cull_enable) >> 1) & 1,
         .plane2 = ((clip_enable | cull_enable) >> 2) & 1,
         .plane3 = ((clip_enable | cull_enable) >> 3) & 1,
         .plane4 = ((clip_enable | cull_enable) >> 4) & 1,
         .plane5 = ((clip_enable | cull_enable) >> 5) & 1,
         .plane6 = ((clip_enable | cull_enable) >> 6) & 1,
         .plane7 = ((clip_enable | cull_enable) >> 7) & 1,
      });
      P_IMMD(p, NV9097, SET_USER_CLIP_OP, {
         .plane0 = (cull_enable >> 0) & 1,
         .plane1 = (cull_enable >> 1) & 1,
         .plane2 = (cull_enable >> 2) & 1,
         .plane3 = (cull_enable >> 3) & 1,
         .plane4 = (cull_enable >> 4) & 1,
         .plane5 = (cull_enable >> 5) & 1,
         .plane6 = (cull_enable >> 6) & 1,
         .plane7 = (cull_enable >> 7) & 1,
      });

      struct nak_xfb_info *xfb = &shader->info.vtg.xfb;
      for (uint8_t b = 0; b < ARRAY_SIZE(xfb->attr_count); b++) {
         const uint8_t attr_count = xfb->attr_count[b];

         max_dw_count += 5 + (128 / 4);

         P_MTHD(p, NV9097, SET_STREAM_OUT_CONTROL_STREAM(b));
         P_NV9097_SET_STREAM_OUT_CONTROL_STREAM(p, b, xfb->stream[b]);
         P_NV9097_SET_STREAM_OUT_CONTROL_COMPONENT_COUNT(p, b, attr_count);
         P_NV9097_SET_STREAM_OUT_CONTROL_STRIDE(p, b, xfb->stride[b]);

         if (attr_count > 0) {
            /* upload packed varying indices in multiples of 4 bytes */
            const uint32_t n = DIV_ROUND_UP(attr_count, 4);
            P_MTHD(p, NV9097, SET_STREAM_OUT_LAYOUT_SELECT(b, 0));
            P_INLINE_ARRAY(p, (const uint32_t*)xfb->attr_index[b], n);
         }
      }

      shader->vtgm_push_dw_count = nv_push_dw_count(&push);
      assert(max_dw_count ==
             nvk_max_shader_push_dw(pdev, shader->info.stage, true));
   }

   assert(nv_push_dw_count(&push) <= max_dw_count);
   assert(max_dw_count <= ARRAY_SIZE(push_dw));

   uint16_t dw_count = nv_push_dw_count(&push);
   shader->push_dw =
      vk_zalloc2(&dev->vk.alloc, pAllocator, dw_count * sizeof(*push_dw),
                 sizeof(*push_dw), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (shader->push_dw == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   memcpy(shader->push_dw, push_dw, dw_count * sizeof(*push_dw));

   return VK_SUCCESS;
}

static const struct vk_shader_ops nvk_shader_ops;

static void
nvk_shader_destroy(struct vk_device *vk_dev,
                   struct vk_shader *vk_shader,
                   const VkAllocationCallbacks* pAllocator)
{
   struct nvk_device *dev = container_of(vk_dev, struct nvk_device, vk);
   struct nvk_shader *shader = container_of(vk_shader, struct nvk_shader, vk);

   vk_free2(&dev->vk.alloc, pAllocator, shader->push_dw);

   if (shader->upload_size > 0) {
      nvk_heap_free(dev, &dev->shader_heap,
                    shader->upload_addr,
                    shader->upload_size);
   }

   if (shader->nak) {
      nak_shader_bin_destroy(shader->nak);
   } else {
      /* This came from deserialize, just free it */
      free((void *)shader->code_ptr);
   }

   free((void *)shader->data_ptr);

   vk_shader_free(&dev->vk, pAllocator, &shader->vk);
}

static VkResult
nvk_compile_shader(struct nvk_device *dev,
                   struct vk_shader_compile_info *info,
                   const struct vk_graphics_pipeline_state *state,
                   const VkAllocationCallbacks* pAllocator,
                   struct vk_shader **shader_out)
{
   struct nvk_shader *shader;
   VkResult result;

   /* We consume the NIR, regardless of success or failure */
   nir_shader *nir = info->nir;

   shader = vk_shader_zalloc(&dev->vk, &nvk_shader_ops, info->stage,
                             pAllocator, sizeof(*shader));
   if (shader == NULL) {
      ralloc_free(nir);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   nvk_lower_nir(dev, nir, info->flags, info->robustness,
                 info->set_layout_count, info->set_layouts,
                 &shader->cbuf_map);

   struct nak_fs_key fs_key_tmp, *fs_key = NULL;
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      nvk_populate_fs_key(&fs_key_tmp, state);
      fs_key = &fs_key_tmp;
   }

   result = nvk_compile_nir(dev, nir, info->flags, info->robustness,
                            fs_key, shader);
   ralloc_free(nir);
   if (result != VK_SUCCESS) {
      nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return result;
   }

   if (dev->nvkmd) {
      result = nvk_shader_upload(dev, shader);
      if (result != VK_SUCCESS) {
         nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
   }

   if (info->stage == MESA_SHADER_FRAGMENT) {
      if (state != NULL && state->ms != NULL) {
         shader->sample_shading_enable = state->ms->sample_shading_enable;
         if (state->ms->sample_shading_enable)
            shader->min_sample_shading = state->ms->min_sample_shading;
      }
   }

   if (info->stage != MESA_SHADER_COMPUTE && dev->nvkmd) {
      result = nvk_shader_fill_push(dev, shader, pAllocator);
      if (result != VK_SUCCESS) {
         nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
   }

   *shader_out = &shader->vk;

   return VK_SUCCESS;
}

VkResult
nvk_compile_nir_shader(struct nvk_device *dev, nir_shader *nir,
                       const VkAllocationCallbacks *alloc,
                       struct nvk_shader **shader_out)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   const struct vk_pipeline_robustness_state rs_none = {
      .uniform_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .storage_buffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT,
      .images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_2_EXT,
   };

   assert(nir->info.stage == MESA_SHADER_COMPUTE);
   if (nir->options == NULL)
      nir->options = nvk_get_nir_options((struct vk_physical_device *)&pdev->vk,
                                         nir->info.stage, &rs_none);

   struct vk_shader_compile_info info = {
      .stage = nir->info.stage,
      .nir = nir,
      .robustness = &rs_none,
   };

   struct vk_shader *shader = NULL;
   VkResult result = nvk_compile_shader(dev, &info, NULL, alloc, &shader);
   if (result != VK_SUCCESS)
      return result;

   *shader_out = container_of(shader, struct nvk_shader, vk);

   return VK_SUCCESS;
}

static VkResult
nvk_compile_shaders(struct vk_device *vk_dev,
                    uint32_t shader_count,
                    struct vk_shader_compile_info *infos,
                    const struct vk_graphics_pipeline_state *state,
                    const struct vk_features *enabled_features,
                    const VkAllocationCallbacks* pAllocator,
                    struct vk_shader **shaders_out)
{
   struct nvk_device *dev = container_of(vk_dev, struct nvk_device, vk);

   for (uint32_t i = 0; i < shader_count; i++) {
      VkResult result = nvk_compile_shader(dev, &infos[i], state,
                                           pAllocator, &shaders_out[i]);
      if (result != VK_SUCCESS) {
         /* Clean up all the shaders before this point */
         for (uint32_t j = 0; j < i; j++)
            nvk_shader_destroy(&dev->vk, shaders_out[j], pAllocator);

         /* Clean up all the NIR after this point */
         for (uint32_t j = i + 1; j < shader_count; j++)
            ralloc_free(infos[j].nir);

         /* Memset the output array */
         memset(shaders_out, 0, shader_count * sizeof(*shaders_out));

         return result;
      }
   }

   return VK_SUCCESS;
}

static VkResult
nvk_deserialize_shader(struct vk_device *vk_dev,
                       struct blob_reader *blob,
                       uint32_t binary_version,
                       const VkAllocationCallbacks* pAllocator,
                       struct vk_shader **shader_out)
{
   struct nvk_device *dev = container_of(vk_dev, struct nvk_device, vk);
   struct nvk_shader *shader;
   VkResult result;

   struct nak_shader_info info;
   blob_copy_bytes(blob, &info, sizeof(info));

   struct nvk_cbuf_map cbuf_map;
   blob_copy_bytes(blob, &cbuf_map, sizeof(cbuf_map));

   bool sample_shading_enable;
   blob_copy_bytes(blob, &sample_shading_enable, sizeof(sample_shading_enable));

   float min_sample_shading;
   blob_copy_bytes(blob, &min_sample_shading, sizeof(min_sample_shading));

   const uint32_t code_size = blob_read_uint32(blob);
   const uint32_t data_size = blob_read_uint32(blob);
   if (blob->overrun)
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader = vk_shader_zalloc(&dev->vk, &nvk_shader_ops, info.stage,
                             pAllocator, sizeof(*shader));
   if (shader == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   shader->info = info;
   shader->cbuf_map = cbuf_map;
   shader->sample_shading_enable = sample_shading_enable;
   shader->min_sample_shading = min_sample_shading;
   shader->code_size = code_size;
   shader->data_size = data_size;

   shader->code_ptr = malloc(code_size);
   if (shader->code_ptr == NULL) {
      nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   shader->data_ptr = malloc(data_size);
   if (shader->data_ptr == NULL) {
      nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   blob_copy_bytes(blob, (void *)shader->code_ptr, shader->code_size);
   blob_copy_bytes(blob, (void *)shader->data_ptr, shader->data_size);
   if (blob->overrun) {
      nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return vk_error(dev, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
   }

   if (dev->nvkmd) {
      result = nvk_shader_upload(dev, shader);
      if (result != VK_SUCCESS) {
         nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
   }

   if (info.stage != MESA_SHADER_COMPUTE && dev->nvkmd) {
      result = nvk_shader_fill_push(dev, shader, pAllocator);
      if (result != VK_SUCCESS) {
         nvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
   }

   *shader_out = &shader->vk;

   return VK_SUCCESS;
}

static bool
nvk_shader_serialize(struct vk_device *vk_dev,
                     const struct vk_shader *vk_shader,
                     struct blob *blob)
{
   struct nvk_shader *shader = container_of(vk_shader, struct nvk_shader, vk);

   /* We can't currently cache assmbly */
   if (shader->nak != NULL && shader->nak->asm_str != NULL)
      return false;

   blob_write_bytes(blob, &shader->info, sizeof(shader->info));
   blob_write_bytes(blob, &shader->cbuf_map, sizeof(shader->cbuf_map));
   blob_write_bytes(blob, &shader->sample_shading_enable,
                    sizeof(shader->sample_shading_enable));
   blob_write_bytes(blob, &shader->min_sample_shading,
                    sizeof(shader->min_sample_shading));

   blob_write_uint32(blob, shader->code_size);
   blob_write_uint32(blob, shader->data_size);
   blob_write_bytes(blob, shader->code_ptr, shader->code_size);
   blob_write_bytes(blob, shader->data_ptr, shader->data_size);

   return !blob->out_of_memory;
}

#define WRITE_STR(field, ...) ({                               \
   memset(field, 0, sizeof(field));                            \
   UNUSED int i = snprintf(field, sizeof(field), __VA_ARGS__); \
   assert(i > 0 && i < sizeof(field));                         \
})

static VkResult
nvk_shader_get_executable_properties(
   UNUSED struct vk_device *device,
   const struct vk_shader *vk_shader,
   uint32_t *executable_count,
   VkPipelineExecutablePropertiesKHR *properties)
{
   struct nvk_shader *shader = container_of(vk_shader, struct nvk_shader, vk);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out,
                          properties, executable_count);

   vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props) {
      props->stages = mesa_to_vk_shader_stage(shader->info.stage);
      props->subgroupSize = 32;
      WRITE_STR(props->name, "%s",
                _mesa_shader_stage_to_string(shader->info.stage));
      WRITE_STR(props->description, "%s shader",
                _mesa_shader_stage_to_string(shader->info.stage));
   }

   return vk_outarray_status(&out);
}

static VkResult
nvk_shader_get_executable_statistics(
   UNUSED struct vk_device *device,
   const struct vk_shader *vk_shader,
   uint32_t executable_index,
   uint32_t *statistic_count,
   VkPipelineExecutableStatisticKHR *statistics)
{
   struct nvk_shader *shader = container_of(vk_shader, struct nvk_shader, vk);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out,
                          statistics, statistic_count);

   assert(executable_index == 0);

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instruction count");
      WRITE_STR(stat->description, "Number of instructions used by this shader");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.num_instrs;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Static cycle count");
      WRITE_STR(stat->description,
                "Total cycles used by fixed-latency instructions in this shader");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.num_static_cycles;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Max warps/SM");
      WRITE_STR(stat->description,
                "Maximum number of warps per SM based on static information");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.max_warps_per_sm;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Spills to memory");
      WRITE_STR(stat->description, "Number of spills from GPRs to memory");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.num_spills_to_mem;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Fills from memory");
      WRITE_STR(stat->description, "Number of fills from memory to GPRs");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.num_spills_to_mem;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Spills to reg");
      WRITE_STR(stat->description,
                "Number of spills between different register files");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.num_spills_to_reg;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Fills from reg");
      WRITE_STR(stat->description,
                "Number of fills between different register files");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.num_fills_from_reg;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Code size");
      WRITE_STR(stat->description,
                "Size of the compiled shader binary, in bytes");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->code_size;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Number of GPRs");
      WRITE_STR(stat->description, "Number of GPRs used by this pipeline");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.num_gprs;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "SLM size");
      WRITE_STR(stat->description,
                "Size of shader local (scratch) memory, in bytes");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->info.slm_size;
   }

   return vk_outarray_status(&out);
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR* ir,
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
nvk_shader_get_executable_internal_representations(
   UNUSED struct vk_device *device,
   const struct vk_shader *vk_shader,
   uint32_t executable_index,
   uint32_t *internal_representation_count,
   VkPipelineExecutableInternalRepresentationKHR *internal_representations)
{
   struct nvk_shader *shader = container_of(vk_shader, struct nvk_shader, vk);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          internal_representations,
                          internal_representation_count);
   bool incomplete_text = false;

   assert(executable_index == 0);

   if (shader->nak != NULL && shader->nak->asm_str != NULL) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "NAK assembly");
         WRITE_STR(ir->description, "NAK assembly");
         if (!write_ir_text(ir, shader->nak->asm_str))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}

static const struct vk_shader_ops nvk_shader_ops = {
   .destroy = nvk_shader_destroy,
   .serialize = nvk_shader_serialize,
   .get_executable_properties = nvk_shader_get_executable_properties,
   .get_executable_statistics = nvk_shader_get_executable_statistics,
   .get_executable_internal_representations =
      nvk_shader_get_executable_internal_representations,
};

const struct vk_device_shader_ops nvk_device_shader_ops = {
   .get_nir_options = nvk_get_nir_options,
   .get_spirv_options = nvk_get_spirv_options,
   .preprocess_nir = nvk_preprocess_nir,
   .hash_state = nvk_hash_state,
   .compile = nvk_compile_shaders,
   .deserialize = nvk_deserialize_shader,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_bind_shaders = nvk_cmd_bind_shaders,
};
