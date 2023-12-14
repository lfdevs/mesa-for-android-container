/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cmd_buffer.h"
#include "nvk_descriptor_set.h"
#include "nvk_descriptor_set_layout.h"
#include "nvk_shader.h"

#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"

#include "nir_builder.h"
#include "nir_deref.h"

struct lower_descriptors_ctx {
   const struct vk_pipeline_layout *layout;
   bool clamp_desc_array_bounds;
   nir_address_format ubo_addr_format;
   nir_address_format ssbo_addr_format;
};

static nir_def *
load_descriptor_set_addr(nir_builder *b, uint32_t set,
                         UNUSED const struct lower_descriptors_ctx *ctx)
{
   uint32_t set_addr_offset =
      nvk_root_descriptor_offset(sets) + set * sizeof(uint64_t);

   return nir_load_ubo(b, 1, 64, nir_imm_int(b, 0),
                       nir_imm_int(b, set_addr_offset),
                       .align_mul = 8, .align_offset = 0, .range = ~0);
}

static const struct nvk_descriptor_set_binding_layout *
get_binding_layout(uint32_t set, uint32_t binding,
                   const struct lower_descriptors_ctx *ctx)
{
   const struct vk_pipeline_layout *layout = ctx->layout;

   assert(set < layout->set_count);
   const struct nvk_descriptor_set_layout *set_layout =
      vk_to_nvk_descriptor_set_layout(layout->set_layouts[set]);

   assert(binding < set_layout->binding_count);
   return &set_layout->binding[binding];
}

static nir_def *
load_descriptor(nir_builder *b, unsigned num_components, unsigned bit_size,
                uint32_t set, uint32_t binding, nir_def *index,
                unsigned offset_B, const struct lower_descriptors_ctx *ctx)
{
   const struct nvk_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   if (ctx->clamp_desc_array_bounds)
      index = nir_umin(b, index, nir_imm_int(b, binding_layout->array_size - 1));

   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      /* Get the index in the root descriptor table dynamic_buffers array. */
      uint8_t dynamic_buffer_start =
         nvk_descriptor_set_layout_dynbuf_start(ctx->layout, set);

      index = nir_iadd_imm(b, index,
                           dynamic_buffer_start +
                           binding_layout->dynamic_buffer_index);

      nir_def *root_desc_offset =
         nir_iadd_imm(b, nir_imul_imm(b, index, sizeof(struct nvk_buffer_address)),
                      nvk_root_descriptor_offset(dynamic_buffers));

      return nir_load_ubo(b, num_components, bit_size,
                          nir_imm_int(b, 0), root_desc_offset,
                          .align_mul = 16, .align_offset = 0, .range = ~0);
   }

   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
      nir_def *base_addr =
         nir_iadd_imm(b, load_descriptor_set_addr(b, set, ctx),
                          binding_layout->offset);

      assert(binding_layout->stride == 1);
      const uint32_t binding_size = binding_layout->array_size;

      /* Convert it to nir_address_format_64bit_bounded_global */
      assert(num_components == 4 && bit_size == 32);
      return nir_vec4(b, nir_unpack_64_2x32_split_x(b, base_addr),
                         nir_unpack_64_2x32_split_y(b, base_addr),
                         nir_imm_int(b, binding_size),
                         nir_imm_int(b, 0));
   }

   default: {
      assert(binding_layout->stride > 0);
      nir_def *desc_ubo_offset =
         nir_iadd_imm(b, nir_imul_imm(b, index, binding_layout->stride),
                         binding_layout->offset + offset_B);

      unsigned desc_align = (1 << (ffs(binding_layout->stride) - 1));
      desc_align = MIN2(desc_align, 16);

      nir_def *set_addr = load_descriptor_set_addr(b, set, ctx);
      return nir_load_global_constant_offset(b, num_components, bit_size,
                                             set_addr, desc_ubo_offset,
                                             .align_mul = desc_align,
                                             .align_offset = 0);
   }
   }
}

static bool
is_idx_intrin(nir_intrinsic_instr *intrin)
{
   while (intrin->intrinsic == nir_intrinsic_vulkan_resource_reindex) {
      intrin = nir_src_as_intrinsic(intrin->src[0]);
      if (intrin == NULL)
         return false;
   }

   return intrin->intrinsic == nir_intrinsic_vulkan_resource_index;
}

static nir_def *
load_descriptor_for_idx_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                               const struct lower_descriptors_ctx *ctx)
{
   nir_def *index = nir_imm_int(b, 0);

   while (intrin->intrinsic == nir_intrinsic_vulkan_resource_reindex) {
      index = nir_iadd(b, index, intrin->src[1].ssa);
      intrin = nir_src_as_intrinsic(intrin->src[0]);
   }

   assert(intrin->intrinsic == nir_intrinsic_vulkan_resource_index);
   uint32_t set = nir_intrinsic_desc_set(intrin);
   uint32_t binding = nir_intrinsic_binding(intrin);
   index = nir_iadd(b, index, intrin->src[0].ssa);

   return load_descriptor(b, 4, 32, set, binding, index, 0, ctx);
}

static bool
try_lower_load_vulkan_descriptor(nir_builder *b, nir_intrinsic_instr *intrin,
                                 const struct lower_descriptors_ctx *ctx)
{
   ASSERTED const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   b->cursor = nir_before_instr(&intrin->instr);

   nir_intrinsic_instr *idx_intrin = nir_src_as_intrinsic(intrin->src[0]);
   if (idx_intrin == NULL || !is_idx_intrin(idx_intrin)) {
      assert(desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
             desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
      return false;
   }

   nir_def *desc = load_descriptor_for_idx_intrin(b, idx_intrin, ctx);

   nir_def_rewrite_uses(&intrin->def, desc);

   return true;
}

static bool
lower_num_workgroups(nir_builder *b, nir_intrinsic_instr *load,
                     const struct lower_descriptors_ctx *ctx)
{
   const uint32_t root_table_offset =
      nvk_root_descriptor_offset(cs.group_count);

   b->cursor = nir_instr_remove(&load->instr);

   nir_def *val = nir_load_ubo(b, 3, 32,
                                   nir_imm_int(b, 0), /* Root table */
                                   nir_imm_int(b, root_table_offset),
                                   .align_mul = 4,
                                   .align_offset = 0,
                                   .range = root_table_offset + 3 * 4);

   nir_def_rewrite_uses(&load->def, val);

   return true;
}

static bool
lower_load_base_workgroup_id(nir_builder *b, nir_intrinsic_instr *load,
                             const struct lower_descriptors_ctx *ctx)
{
   const uint32_t root_table_offset =
      nvk_root_descriptor_offset(cs.base_group);

   b->cursor = nir_instr_remove(&load->instr);

   nir_def *val = nir_load_ubo(b, 3, 32,
                                   nir_imm_int(b, 0),
                                   nir_imm_int(b, root_table_offset),
                                   .align_mul = 4,
                                   .align_offset = 0,
                                   .range = root_table_offset + 3 * 4);

   nir_def_rewrite_uses(&load->def, val);

   return true;
}

static bool
lower_load_push_constant(nir_builder *b, nir_intrinsic_instr *load,
                         const struct lower_descriptors_ctx *ctx)
{
   const uint32_t push_region_offset =
      nvk_root_descriptor_offset(push);
   const uint32_t base = nir_intrinsic_base(load);

   b->cursor = nir_before_instr(&load->instr);

   nir_def *offset = nir_iadd_imm(b, load->src[0].ssa,
                                         push_region_offset + base);

   nir_def *val =
      nir_load_ubo(b, load->def.num_components, load->def.bit_size,
                   nir_imm_int(b, 0), offset,
                   .align_mul = load->def.bit_size / 8,
                   .align_offset = 0,
                   .range = push_region_offset + base +
                            nir_intrinsic_range(load));

   nir_def_rewrite_uses(&load->def, val);

   return true;
}

static bool
lower_load_view_index(nir_builder *b, nir_intrinsic_instr *load,
                      const struct lower_descriptors_ctx *ctx)
{
   const uint32_t root_table_offset =
      nvk_root_descriptor_offset(draw.view_index);

   b->cursor = nir_instr_remove(&load->instr);

   nir_def *val = nir_load_ubo(b, 1, 32,
                                   nir_imm_int(b, 0),
                                   nir_imm_int(b, root_table_offset),
                                   .align_mul = 4,
                                   .align_offset = 0,
                                   .range = root_table_offset + 4);

   nir_def_rewrite_uses(&load->def, val);

   return true;
}

static void
get_resource_deref_binding(nir_builder *b, nir_deref_instr *deref,
                           uint32_t *set, uint32_t *binding,
                           nir_def **index)
{
   if (deref->deref_type == nir_deref_type_array) {
      *index = deref->arr.index.ssa;
      deref = nir_deref_instr_parent(deref);
   } else {
      *index = nir_imm_int(b, 0);
   }

   assert(deref->deref_type == nir_deref_type_var);
   nir_variable *var = deref->var;

   *set = var->data.descriptor_set;
   *binding = var->data.binding;
}

static nir_def *
load_resource_deref_desc(nir_builder *b, 
                         unsigned num_components, unsigned bit_size,
                         nir_deref_instr *deref, unsigned offset_B,
                         const struct lower_descriptors_ctx *ctx)
{
   uint32_t set, binding;
   nir_def *index;
   get_resource_deref_binding(b, deref, &set, &binding, &index);
   return load_descriptor(b, num_components, bit_size,
                          set, binding, index, offset_B, ctx);
}

static bool
lower_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                   const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&intrin->instr);
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_def *desc = load_resource_deref_desc(b, 1, 32, deref, 0, ctx);
   nir_rewrite_image_intrinsic(intrin, desc, true);

   /* We treat 3D images as 2D arrays */
   if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_3D) {
      assert(!nir_intrinsic_image_array(intrin));
      nir_intrinsic_set_image_dim(intrin, GLSL_SAMPLER_DIM_2D);
      nir_intrinsic_set_image_array(intrin, true);
   }

   /* We don't support ReadWithoutFormat yet */
   if (intrin->intrinsic == nir_intrinsic_image_deref_load)
      assert(nir_intrinsic_format(intrin) != PIPE_FORMAT_NONE);

   return true;
}

static bool
lower_interp_at_sample(nir_builder *b, nir_intrinsic_instr *interp,
                       const struct lower_descriptors_ctx *ctx)
{
   const uint32_t root_table_offset =
      nvk_root_descriptor_offset(draw.sample_locations);

   nir_def *sample = interp->src[1].ssa;

   b->cursor = nir_before_instr(&interp->instr);

   nir_def *loc = nir_load_ubo(b, 1, 64,
                               nir_imm_int(b, 0), /* Root table */
                               nir_imm_int(b, root_table_offset),
                               .align_mul = 8,
                               .align_offset = 0,
                               .range = root_table_offset + 8);

   /* Yay little endian */
   loc = nir_ushr(b, loc, nir_imul_imm(b, sample, 8));
   nir_def *loc_x_u4 = nir_iand_imm(b, loc, 0xf);
   nir_def *loc_y_u4 = nir_iand_imm(b, nir_ushr_imm(b, loc, 4), 0xf);
   nir_def *loc_u4 = nir_vec2(b, loc_x_u4, loc_y_u4);
   nir_def *loc_f = nir_fmul_imm(b, nir_i2f32(b, loc_u4), 1.0 / 16.0);
   nir_def *offset = nir_fadd_imm(b, loc_f, -0.5);

   assert(interp->intrinsic == nir_intrinsic_interp_deref_at_sample);
   interp->intrinsic = nir_intrinsic_interp_deref_at_offset;
   nir_src_rewrite(&interp->src[1], offset);

   return true;
}

static bool
try_lower_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                 const struct lower_descriptors_ctx *ctx)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_vulkan_descriptor:
      return try_lower_load_vulkan_descriptor(b, intrin, ctx);

   case nir_intrinsic_load_workgroup_size:
      unreachable("Should have been lowered by nir_lower_cs_intrinsics()");

   case nir_intrinsic_load_num_workgroups:
      return lower_num_workgroups(b, intrin, ctx);

   case nir_intrinsic_load_base_workgroup_id:
      return lower_load_base_workgroup_id(b, intrin, ctx);

   case nir_intrinsic_load_push_constant:
      return lower_load_push_constant(b, intrin, ctx);

   case nir_intrinsic_load_view_index:
      return lower_load_view_index(b, intrin, ctx);

   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_image_deref_load_param_intel:
   case nir_intrinsic_image_deref_load_raw_intel:
   case nir_intrinsic_image_deref_store_raw_intel:
      return lower_image_intrin(b, intrin, ctx);

   case nir_intrinsic_interp_deref_at_sample:
      return lower_interp_at_sample(b, intrin, ctx);

   default:
      return false;
   }
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&tex->instr);

   const int texture_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   const int sampler_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (texture_src_idx < 0) {
      assert(sampler_src_idx < 0);
      return false;
   }

   nir_deref_instr *texture = nir_src_as_deref(tex->src[texture_src_idx].src);
   nir_deref_instr *sampler = sampler_src_idx < 0 ? NULL :
                              nir_src_as_deref(tex->src[sampler_src_idx].src);
   assert(texture);

   nir_def *plane_ssa = nir_steal_tex_src(tex, nir_tex_src_plane);
   const uint32_t plane =
      plane_ssa ? nir_src_as_uint(nir_src_for_ssa(plane_ssa)) : 0;
   const uint64_t plane_offset_B = plane * sizeof(struct nvk_image_descriptor);

   nir_def *combined_handle;
   if (texture == sampler) {
      combined_handle = load_resource_deref_desc(b, 1, 32, texture, plane_offset_B, ctx);
   } else {
      nir_def *texture_desc =
         load_resource_deref_desc(b, 1, 32, texture, plane_offset_B, ctx);
      combined_handle = nir_iand_imm(b, texture_desc,
                                     NVK_IMAGE_DESCRIPTOR_IMAGE_INDEX_MASK);

      if (sampler != NULL) {
         nir_def *sampler_desc =
            load_resource_deref_desc(b, 1, 32, sampler, plane_offset_B, ctx);
         nir_def *sampler_index =
            nir_iand_imm(b, sampler_desc,
                         NVK_IMAGE_DESCRIPTOR_SAMPLER_INDEX_MASK);
         combined_handle = nir_ior(b, combined_handle, sampler_index);
      }
   }

   /* TODO: The nv50 back-end assumes it's 64-bit because of GL */
   combined_handle = nir_u2u64(b, combined_handle);

   /* TODO: The nv50 back-end assumes it gets handles both places, even for
    * texelFetch.
    */
   nir_src_rewrite(&tex->src[texture_src_idx].src, combined_handle);
   tex->src[texture_src_idx].src_type = nir_tex_src_texture_handle;

   if (sampler_src_idx < 0) {
      nir_tex_instr_add_src(tex, nir_tex_src_sampler_handle, combined_handle);
   } else {
      nir_src_rewrite(&tex->src[sampler_src_idx].src, combined_handle);
      tex->src[sampler_src_idx].src_type = nir_tex_src_sampler_handle;
   }

   return true;
}

static bool
try_lower_descriptors_instr(nir_builder *b, nir_instr *instr,
                            void *_data)
{
   const struct lower_descriptors_ctx *ctx = _data;

   switch (instr->type) {
   case nir_instr_type_tex:
      return lower_tex(b, nir_instr_as_tex(instr), ctx);
   case nir_instr_type_intrinsic:
      return try_lower_intrin(b, nir_instr_as_intrinsic(instr), ctx);
   default:
      return false;
   }
}

static bool
lower_ssbo_resource_index(nir_builder *b, nir_intrinsic_instr *intrin,
                          const struct lower_descriptors_ctx *ctx)
{
   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   if (desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
       desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   uint32_t set = nir_intrinsic_desc_set(intrin);
   uint32_t binding = nir_intrinsic_binding(intrin);
   nir_def *index = intrin->src[0].ssa;

   const struct nvk_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   nir_def *binding_addr;
   uint8_t binding_stride;
   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
      nir_def *set_addr = load_descriptor_set_addr(b, set, ctx);
      binding_addr = nir_iadd_imm(b, set_addr, binding_layout->offset);
      binding_stride = binding_layout->stride;
      break;
   }

   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      const uint32_t root_desc_addr_offset =
         nvk_root_descriptor_offset(root_desc_addr);

      nir_def *root_desc_addr =
         nir_load_ubo(b, 1, 64, nir_imm_int(b, 0),
                      nir_imm_int(b, root_desc_addr_offset),
                      .align_mul = 8, .align_offset = 0, .range = ~0);

      const uint8_t dynamic_buffer_start =
         nvk_descriptor_set_layout_dynbuf_start(ctx->layout, set) +
         binding_layout->dynamic_buffer_index;

      const uint32_t dynamic_binding_offset =
         nvk_root_descriptor_offset(dynamic_buffers) +
         dynamic_buffer_start * sizeof(struct nvk_buffer_address);

      binding_addr = nir_iadd_imm(b, root_desc_addr, dynamic_binding_offset);
      binding_stride = sizeof(struct nvk_buffer_address);
      break;
   }

   default:
      unreachable("Not an SSBO descriptor");
   }

   /* Tuck the stride in the top 8 bits of the binding address */
   binding_addr = nir_ior_imm(b, binding_addr, (uint64_t)binding_stride << 56);

   const uint32_t binding_size = binding_layout->array_size * binding_stride;
   nir_def *offset_in_binding = nir_imul_imm(b, index, binding_stride);

   nir_def *addr;
   switch (ctx->ssbo_addr_format) {
   case nir_address_format_64bit_global:
      addr = nir_iadd(b, binding_addr, nir_u2u64(b, offset_in_binding));
      break;

   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      addr = nir_vec4(b, nir_unpack_64_2x32_split_x(b, binding_addr),
                         nir_unpack_64_2x32_split_y(b, binding_addr),
                         nir_imm_int(b, binding_size),
                         offset_in_binding);
      break;

   default:
      unreachable("Unknown address mode");
   }

   nir_def_rewrite_uses(&intrin->def, addr);

   return true;
}

static bool
lower_ssbo_resource_reindex(nir_builder *b, nir_intrinsic_instr *intrin,
                            const struct lower_descriptors_ctx *ctx)
{
   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   if (desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
       desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *addr = intrin->src[0].ssa;
   nir_def *index = intrin->src[1].ssa;

   nir_def *addr_high32;
   switch (ctx->ssbo_addr_format) {
   case nir_address_format_64bit_global:
      addr_high32 = nir_unpack_64_2x32_split_y(b, addr);
      break;

   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      addr_high32 = nir_channel(b, addr, 1);
      break;

   default:
      unreachable("Unknown address mode");
   }

   nir_def *stride = nir_ushr_imm(b, addr_high32, 24);
   nir_def *offset = nir_imul(b, index, stride);

   addr = nir_build_addr_iadd(b, addr, ctx->ssbo_addr_format,
                              nir_var_mem_ssbo, offset);
   nir_def_rewrite_uses(&intrin->def, addr);

   return true;
}

static bool
lower_load_ssbo_descriptor(nir_builder *b, nir_intrinsic_instr *intrin,
                           const struct lower_descriptors_ctx *ctx)
{
   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   if (desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
       desc_type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *addr = intrin->src[0].ssa;

   nir_def *desc;
   switch (ctx->ssbo_addr_format) {
   case nir_address_format_64bit_global:
      /* Mask off the binding stride */
      addr = nir_iand_imm(b, addr, BITFIELD64_MASK(56));
      desc = nir_build_load_global(b, 1, 64, addr,
                                   .access = ACCESS_NON_WRITEABLE,
                                   .align_mul = 16, .align_offset = 0);
      break;

   case nir_address_format_64bit_global_32bit_offset: {
      nir_def *base = nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2));
      nir_def *offset = nir_channel(b, addr, 3);
      /* Mask off the binding stride */
      base = nir_iand_imm(b, base, BITFIELD64_MASK(56));
      desc = nir_load_global_constant_offset(b, 4, 32, base, offset,
                                             .align_mul = 16,
                                             .align_offset = 0);
      break;
   }

   case nir_address_format_64bit_bounded_global: {
      nir_def *base = nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2));
      nir_def *size = nir_channel(b, addr, 2);
      nir_def *offset = nir_channel(b, addr, 3);
      /* Mask off the binding stride */
      base = nir_iand_imm(b, base, BITFIELD64_MASK(56));
      desc = nir_load_global_constant_bounded(b, 4, 32, base, offset, size,
                                              .align_mul = 16,
                                              .align_offset = 0);
      break;
   }

   default:
      unreachable("Unknown address mode");
   }

   nir_def_rewrite_uses(&intrin->def, desc);

   return true;
}

static bool
lower_ssbo_descriptor_instr(nir_builder *b, nir_instr *instr,
                            void *_data)
{
   const struct lower_descriptors_ctx *ctx = _data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_vulkan_resource_index:
      return lower_ssbo_resource_index(b, intrin, ctx);
   case nir_intrinsic_vulkan_resource_reindex:
      return lower_ssbo_resource_reindex(b, intrin, ctx);
   case nir_intrinsic_load_vulkan_descriptor:
      return lower_load_ssbo_descriptor(b, intrin, ctx);
   default:
      return false;
   }
}

bool
nvk_nir_lower_descriptors(nir_shader *nir,
                          const struct vk_pipeline_robustness_state *rs,
                          const struct vk_pipeline_layout *layout)
{
   struct lower_descriptors_ctx ctx = {
      .layout = layout,
      .clamp_desc_array_bounds =
         rs->storage_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||
         rs->uniform_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||
         rs->images != VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT,
      .ssbo_addr_format = nvk_buffer_addr_format(rs->storage_buffers),
      .ubo_addr_format = nvk_buffer_addr_format(rs->uniform_buffers),
   };

   /* We run in two passes.  The first attempts to lower everything it can.
    * In the variable pointers case, some SSBO intrinsics may fail to lower
    * but that's okay.  The second pass cleans up any SSBO intrinsics which
    * are left and lowers them to slightly less efficient but variable-
    * pointers-correct versions.
    */
   bool pass_lower_descriptors =
      nir_shader_instructions_pass(nir, try_lower_descriptors_instr,
                                   nir_metadata_block_index |
                                   nir_metadata_dominance,
                                   (void *)&ctx);
   bool pass_lower_ssbo =
      nir_shader_instructions_pass(nir, lower_ssbo_descriptor_instr,
                                   nir_metadata_block_index |
                                   nir_metadata_dominance,
                                   (void *)&ctx);
   return pass_lower_descriptors || pass_lower_ssbo;
}
