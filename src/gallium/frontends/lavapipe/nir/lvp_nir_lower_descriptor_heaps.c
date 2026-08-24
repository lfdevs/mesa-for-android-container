/*
 * Copyright © 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "lvp_nir.h"
#include "lvp_private.h"

static void
lower_buffer(nir_builder *b, nir_intrinsic_instr *intr, uint32_t src_index)
{
   if (nir_src_bit_size(intr->src[src_index]) == 64)
      return;

   nir_def *addr = nir_pack_64_2x32(b, nir_channels(b, intr->src[src_index].ssa, 0x3));
   nir_src_rewrite(&intr->src[src_index], addr);
}

static bool
pass(nir_builder *b, nir_instr *instr, void *data)
{
   b->cursor = nir_before_instr(instr);

   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_ubo:
         lower_buffer(b, intr, 0);
         return true;

      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_ssbo_atomic:
      case nir_intrinsic_ssbo_atomic_swap:
      case nir_intrinsic_get_ssbo_size:
      case nir_intrinsic_load_ssbo_address:
         lower_buffer(b, intr, 0);
         return true;

      case nir_intrinsic_store_ssbo:
         lower_buffer(b, intr, 1);
         return true;

      case nir_intrinsic_load_heap_descriptor: {
         uint32_t resource_type = nir_intrinsic_resource_type(intr);

         enum lvp_descriptor_heap heap = LVP_DESCRIPTOR_HEAP_RESOURCE;
         if (resource_type == VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT)
            heap = LVP_DESCRIPTOR_HEAP_SAMPLER;

         nir_def *base = nir_load_const_buf_base_addr_lvp(b, nir_imm_int(b, heap + 1));
         nir_def *addr = nir_iadd(b, base, nir_u2u64(b, intr->src[0].ssa));

         if (resource_type == VK_SPIRV_RESOURCE_TYPE_ACCELERATION_STRUCTURE_BIT_EXT) {
            nir_def_replace(&intr->def, nir_build_load_global(b, 1, 64, addr));
            return true;
         }

         nir_def_replace(&intr->def, nir_vec3(b, nir_unpack_64_2x32_split_x(b, addr),
                                              nir_unpack_64_2x32_split_y(b, addr), nir_imm_int(b, 0)));

         return true;
      }

      case nir_intrinsic_load_resource_heap_data: {
         nir_def *base = nir_load_const_buf_base_addr_lvp(b, nir_imm_int(b, LVP_DESCRIPTOR_HEAP_RESOURCE + 1));
         nir_def *addr = nir_iadd(b, base, nir_u2u64(b, intr->src[0].ssa));
         nir_def_replace(&intr->def, nir_build_load_global(b, intr->def.num_components, intr->def.bit_size, addr,
                                                           .align_mul = nir_intrinsic_align_mul(intr),
                                                           .align_offset = nir_intrinsic_align_offset(intr)));

         return true;
      }

      case nir_intrinsic_image_heap_sparse_load:
      case nir_intrinsic_image_heap_load:
      case nir_intrinsic_image_heap_store:
      case nir_intrinsic_image_heap_atomic:
      case nir_intrinsic_image_heap_atomic_swap:
      case nir_intrinsic_image_heap_size:
      case nir_intrinsic_image_heap_samples: {
         nir_def *base = nir_load_const_buf_base_addr_lvp(b, nir_imm_int(b, LVP_DESCRIPTOR_HEAP_RESOURCE + 1));
         nir_rewrite_image_intrinsic(intr, nir_iadd(b, base, nir_u2u64(b, intr->src[0].ssa)), nir_image_intrinsic_type_bindless);
         return true;
      }

      default:
         return false;
      }
   }

   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);

      nir_def *plane_ssa = nir_steal_tex_src(tex, nir_tex_src_plane);
      uint32_t plane = plane_ssa ? nir_src_as_uint(nir_src_for_ssa(plane_ssa)) : 0;
      uint32_t plane_offset = plane * sizeof(struct lp_image_descriptor);

      for (uint32_t i = 0; i < tex->num_srcs; i++) {
         if (tex->src[i].src_type == nir_tex_src_texture_heap_offset) {
            tex->src[i].src_type = nir_tex_src_texture_handle;
            nir_def *base = nir_load_const_buf_base_addr_lvp(b, nir_imm_int(b, LVP_DESCRIPTOR_HEAP_RESOURCE + 1));
            nir_src_rewrite(&tex->src[i].src, nir_iadd(b, base, nir_u2u64(b, nir_iadd_imm(b, tex->src[i].src.ssa, plane_offset))));
         } else if (tex->src[i].src_type == nir_tex_src_sampler_heap_offset) {
            tex->src[i].src_type = nir_tex_src_sampler_handle;
            nir_def *base = nir_load_const_buf_base_addr_lvp(b, nir_imm_int(b, LVP_DESCRIPTOR_HEAP_SAMPLER + 1));
            nir_src_rewrite(&tex->src[i].src, nir_iadd(b, base, nir_u2u64(b, tex->src[i].src.ssa)));
         }
      }

      if (tex->embedded_sampler) {
         nir_def *base = nir_load_const_buf_base_addr_lvp(b, nir_imm_int(b, LVP_DESCRIPTOR_HEAP_EMBEDDED + 1));
         nir_def *sampler = nir_iadd_imm(b, base, tex->sampler_index * sizeof(struct lp_sampler_descriptor));
         nir_tex_instr_add_src(tex, nir_tex_src_sampler_handle, sampler);
      }

      return true;
   }

   return false;
}

bool
lvp_nir_lower_desciptor_heaps(nir_shader *shader, const VkShaderDescriptorSetAndBindingMappingInfoEXT *mapping)
{
   // nir_print_shader(shader, stdout);
   return nir_shader_instructions_pass(shader, pass, nir_metadata_control_flow, NULL);
}
