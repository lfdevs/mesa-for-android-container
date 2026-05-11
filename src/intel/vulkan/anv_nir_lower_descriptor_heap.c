/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_nir.h"
#include "nir/nir_builder.h"

#include "genxml/genX_bits.h"

static nir_def *
build_surface_handle(nir_builder *b, nir_def *heap_offset,
                     uint32_t plane, bool non_uniform,
                     const struct anv_physical_device *pdevice)
{
   if (plane != 0)
      heap_offset = nir_iadd_imm(b, heap_offset, plane * ANV_SURFACE_STATE_SIZE);
   nir_def *surface_handle =
      intel_has_extended_bindless(&pdevice->info) ?
      nir_iadd(b,
               anv_load_driver_uniform(b, 1, desc_surface_offsets[0]),
               heap_offset) :
      nir_ishl_imm(b, heap_offset, 6);
   return nir_resource_intel(
      b,
      nir_imm_int(b, 0xdeaddead),
      surface_handle,
      nir_imm_int(b, 0xdeaddead),
      nir_imm_int(b, 0) /* bindless_base_offset */,
      .desc_set = -1,
      .binding = -1,
      .resource_block_intel = UINT32_MAX,
      .resource_access_intel = (
         nir_resource_intel_bindless |
         (non_uniform ? nir_resource_intel_non_uniform : 0)));
}

static nir_def *
build_deref_surface_handle(nir_builder *b, nir_deref_instr *deref,
                           const struct anv_physical_device *pdevice)
{
   nir_def *surface_handle = nir_explicit_io_address_from_deref(
      b, deref,
      intel_has_extended_bindless(&pdevice->info) ?
      anv_load_driver_uniform(b, 1, desc_surface_offsets[0]) :
      nir_imm_int(b, 0),
      nir_address_format_32bit_offset);
   if (!intel_has_extended_bindless(&pdevice->info))
      surface_handle = nir_ishl_imm(b, surface_handle, 6);

   return nir_resource_intel(
      b,
      nir_imm_int(b, 0xdeaddead),
      surface_handle,
      nir_imm_int(b, 0xdeaddead),
      nir_imm_int(b, 0) /* bindless_base_offset */,
      .desc_set = -1,
      .binding = -1,
      .resource_block_intel = UINT32_MAX,
      .resource_access_intel = nir_resource_intel_bindless);
}

static nir_def *
build_sampler_handle(nir_builder *b, nir_def *heap_offset,
                     uint32_t plane, bool non_uniform, bool embedded,
                     const struct anv_physical_device *pdevice)
{
   if (plane != 0)
      heap_offset = nir_iadd_imm(b, heap_offset, plane * ANV_SAMPLER_STATE_SIZE);
   nir_def *sampler_handle =
      embedded ? heap_offset :
      nir_iadd(b,
               anv_load_driver_uniform(b, 1, desc_surface_offsets[1]),
               heap_offset);
   return nir_resource_intel(
      b,
      nir_imm_int(b, 0xdeaddead),
      sampler_handle,
      nir_imm_int(b, 0xdeaddead),
      nir_imm_int(b, 0) /* bindless_base_offset */,
      .desc_set = -1,
      .binding = -1,
      .resource_block_intel = UINT32_MAX,
      .resource_access_intel = (
         nir_resource_intel_sampler |
         nir_resource_intel_bindless |
         (non_uniform ? nir_resource_intel_non_uniform : 0) |
         (embedded ? nir_resource_intel_sampler_embedded : 0)));
}

static nir_def *
build_descriptor_addr(nir_builder *b, nir_def *heap_offset,
                      const struct anv_physical_device *pdevice)
{
   return nir_pack_64_2x32_split(
      b,
      nir_iadd(
         b,
         anv_load_driver_uniform(b, 1, desc_surface_offsets[0]),
         heap_offset),
      nir_load_reloc_const_intel(
         b, BRW_SHADER_RELOC_DESCRIPTORS_BUFFER_ADDR_HIGH));
}

static nir_def *
nir_deref_find_heap_offset(nir_deref_instr *deref, unsigned *resource_type)
{
   while (1) {
      /* Nothing we will use this on has a variable */
      assert(deref->deref_type != nir_deref_type_var);

      nir_deref_instr *parent = nir_src_as_deref(deref->parent);
      if (!parent)
         break;

      deref = parent;
   }
   assert(deref->deref_type == nir_deref_type_cast);

   nir_intrinsic_instr *intrin = nir_src_as_intrinsic(deref->parent);
   if (!intrin || intrin->intrinsic != nir_intrinsic_load_heap_descriptor)
      return NULL;

   if (resource_type != NULL)
      *resource_type = nir_intrinsic_resource_type(intrin);

   return intrin->src[0].ssa;
}

static nir_intrinsic_op
heap_to_bindless_intrinsic(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_image_heap_load: return nir_intrinsic_bindless_image_load;
   case nir_intrinsic_image_heap_store: return nir_intrinsic_bindless_image_store;
   case nir_intrinsic_image_heap_atomic: return nir_intrinsic_bindless_image_atomic;
   case nir_intrinsic_image_heap_atomic_swap: return nir_intrinsic_bindless_image_atomic_swap;
   case nir_intrinsic_image_heap_size: return nir_intrinsic_bindless_image_size;
   case nir_intrinsic_image_heap_samples: return nir_intrinsic_bindless_image_samples;
   case nir_intrinsic_image_heap_load_raw_intel: return nir_intrinsic_bindless_image_load_raw_intel;
   case nir_intrinsic_image_heap_store_raw_intel: return nir_intrinsic_bindless_image_store_raw_intel;
   case nir_intrinsic_image_heap_sparse_load: return nir_intrinsic_bindless_image_sparse_load;
   default: UNREACHABLE("invalid intrinsic");
   }
}

static nir_intrinsic_op
deref_to_bindless_intrinsic(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_image_deref_load: return nir_intrinsic_bindless_image_load;
   case nir_intrinsic_image_deref_store: return nir_intrinsic_bindless_image_store;
   case nir_intrinsic_image_deref_atomic: return nir_intrinsic_bindless_image_atomic;
   case nir_intrinsic_image_deref_atomic_swap: return nir_intrinsic_bindless_image_atomic_swap;
   case nir_intrinsic_image_deref_size: return nir_intrinsic_bindless_image_size;
   case nir_intrinsic_image_deref_samples: return nir_intrinsic_bindless_image_samples;
   case nir_intrinsic_image_deref_load_raw_intel: return nir_intrinsic_bindless_image_load_raw_intel;
   case nir_intrinsic_image_deref_store_raw_intel: return nir_intrinsic_bindless_image_store_raw_intel;
   case nir_intrinsic_image_deref_sparse_load: return nir_intrinsic_bindless_image_sparse_load;
   default: UNREACHABLE("invalid intrinsic");
   }
}

static nir_def *
build_load_descriptor_mem(nir_builder *b,
                          unsigned num_component, unsigned bit_size,
                          nir_def *heap_offset, uint32_t imm_offset,
                          const struct anv_physical_device *pdevice)
{
   return nir_load_global_constant(
      b, num_component, bit_size,
      build_descriptor_addr(b, nir_iadd_imm(b, heap_offset, imm_offset), pdevice));
}

static bool
lower_accel_struct_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin,
                             const struct anv_physical_device *pdevice)
{
   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *accel_addr = build_load_descriptor_mem(
      b, 1, 64, intrin->src[0].ssa, 0, pdevice);
   nir_def_replace(&intrin->def, accel_addr);

   return true;
}

static bool
lower_resource_heap_data_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin,
                                   const struct anv_physical_device *pdevice)
{
   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *data = build_load_descriptor_mem(
      b, intrin->def.num_components, intrin->def.bit_size,
      intrin->src[0].ssa, 0, pdevice);
   nir_def_replace(&intrin->def, data);

   return true;
}

static bool
lower_buffer_intrinsic(nir_builder *b,
                       nir_intrinsic_instr *intrin,
                       const struct anv_physical_device *pdevice)
{
   const struct intel_device_info *devinfo = &pdevice->info;

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *heap_offset = intrin->src[0].ssa;

   nir_def *addr;
   if (pdevice->isl_dev.buffer_length_in_aux_addr) {
      nir_def *surface_addr =
         build_load_descriptor_mem(b, 4, 32, heap_offset,
                                   RENDER_SURFACE_STATE_SurfaceBaseAddress_start(devinfo) / 8,
                                   pdevice);
      nir_def *addr_ldw = nir_channel(b, surface_addr, 0);
      nir_def *addr_udw = nir_channel(b, surface_addr, 1);
      nir_def *length = nir_channel(b, surface_addr, 3);

      addr = nir_vec4(b, addr_ldw, addr_udw, length, nir_imm_int(b, 0));
   } else {
      /* Wa_14019708328 */
      assert(((RENDER_SURFACE_STATE_SurfaceBaseAddress_start(devinfo) +
               RENDER_SURFACE_STATE_SurfaceBaseAddress_bits(devinfo) - 1) -
              RENDER_SURFACE_STATE_Width_start(devinfo)) / 8 <= 32);

      nir_def *surface_addr =
         build_load_descriptor_mem(b, 2, 32, heap_offset,
                                   RENDER_SURFACE_STATE_SurfaceBaseAddress_start(devinfo) / 8,
                                   pdevice);
      nir_def *addr_ldw = nir_channel(b, surface_addr, 0);
      nir_def *addr_udw = nir_channel(b, surface_addr, 1);

      /* Take all the RENDER_SURFACE_STATE fields from the beginning of the
       * structure up to the Depth field.
       */
      const uint32_t type_sizes_dwords =
         DIV_ROUND_UP(RENDER_SURFACE_STATE_Depth_start(devinfo) +
                      RENDER_SURFACE_STATE_Depth_bits(devinfo), 32);
      nir_def *type_sizes =
         build_load_descriptor_mem(b, type_sizes_dwords, 32, heap_offset, 0, pdevice);

      const unsigned width_start = RENDER_SURFACE_STATE_Width_start(devinfo);
      /* SKL PRMs, Volume 2d: Command Reference: Structures, RENDER_SURFACE_STATE
       *
       *    Width:  "bits [6:0]   of the number of entries in the buffer - 1"
       *    Height: "bits [20:7]  of the number of entries in the buffer - 1"
       *    Depth:  "bits [31:21] of the number of entries in the buffer - 1"
       */
      const unsigned width_bits = 7;
      nir_def *width =
         nir_ubitfield_extract_imm(
            b,
            nir_channel(b, type_sizes, width_start / 32),
            width_start % 32, width_bits);

      const unsigned height_start = RENDER_SURFACE_STATE_Height_start(devinfo);
      const unsigned height_bits = RENDER_SURFACE_STATE_Height_bits(devinfo);
      nir_def *height =
         nir_ubitfield_extract_imm(
            b,
            nir_channel(b, type_sizes, height_start / 32),
            height_start % 32, height_bits);

      const unsigned depth_start = RENDER_SURFACE_STATE_Depth_start(devinfo);
      const unsigned depth_bits = RENDER_SURFACE_STATE_Depth_bits(devinfo);
      nir_def *depth =
         nir_ubitfield_extract_imm(
            b,
            nir_channel(b, type_sizes, depth_start / 32),
            depth_start % 32, depth_bits);

      nir_def *length = width;
      length = nir_ior(b, length, nir_ishl_imm(b, height, width_bits));
      length = nir_ior(b, length, nir_ishl_imm(b, depth, width_bits + height_bits));
      length = nir_iadd_imm(b, length, 1);

      /* Check the surface type, if it's SURFTYPE_NULL, set the length of the
       * buffer to 0.
       */
      const unsigned type_start = RENDER_SURFACE_STATE_SurfaceType_start(devinfo);
      const unsigned type_dw = type_start / 32;
      nir_def *type =
         nir_iand_imm(b,
                      nir_ishr_imm(b,
                                   nir_channel(b, type_sizes, type_dw),
                                   type_start % 32),
                      (1u << RENDER_SURFACE_STATE_SurfaceType_bits(devinfo)) - 1);

      length = nir_bcsel(b,
                         nir_ieq_imm(b, type, 7 /* SURFTYPE_NULL */),
                         nir_imm_int(b, 0), length);

      addr = nir_vec4(b, addr_ldw, addr_udw, length, nir_imm_int(b, 0));
   }

   nir_def_replace(&intrin->def, addr);

   return true;
}

static bool
lower_param_intrinsic(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      nir_def *heap_offset,
                      const struct anv_physical_device *pdevice)
{
   const struct intel_device_info *devinfo = &pdevice->info;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *data;
   switch (nir_intrinsic_base(intrin)) {
   case ISL_SURF_PARAM_BASE_ADDRESSS: {
      data = build_load_descriptor_mem(
         b, intrin->def.num_components, intrin->def.bit_size,
         heap_offset,
         RENDER_SURFACE_STATE_SurfaceBaseAddress_start(devinfo) / 8,
         pdevice);
      break;
   }
   case ISL_SURF_PARAM_TILE_MODE: {
      nir_def *dword = build_load_descriptor_mem(
         b, 1, 32, heap_offset,
         RENDER_SURFACE_STATE_TileMode_start(devinfo) / 8,
         pdevice);
      data = nir_ubitfield_extract_imm(
         b, dword,
         RENDER_SURFACE_STATE_TileMode_start(devinfo) % 32,
         RENDER_SURFACE_STATE_TileMode_bits(devinfo));
      break;
   }
   case ISL_SURF_PARAM_PITCH: {
      assert(RENDER_SURFACE_STATE_SurfacePitch_start(devinfo) % 32 == 0);
      nir_def *pitch_dword = build_load_descriptor_mem(
         b, 1, 32, heap_offset,
         RENDER_SURFACE_STATE_SurfacePitch_start(devinfo) / 8,
         pdevice);
      data = nir_ubitfield_extract_imm(
         b, pitch_dword,
         RENDER_SURFACE_STATE_SurfacePitch_start(devinfo) % 32,
         RENDER_SURFACE_STATE_SurfacePitch_bits(devinfo));
      /* Pitch is written with -1 in ISL (see isl_surface_state.c) */
      data = nir_iadd_imm(b, data, 1);
      break;
   }
   case ISL_SURF_PARAM_QPITCH: {
      assert(RENDER_SURFACE_STATE_SurfaceQPitch_start(devinfo) % 32 == 0);
      nir_def *pitch_dword = build_load_descriptor_mem(
         b, 1, 32, heap_offset,
         RENDER_SURFACE_STATE_SurfaceQPitch_start(devinfo) / 8,
         pdevice);
      data = nir_ubitfield_extract_imm(
         b, pitch_dword,
         RENDER_SURFACE_STATE_SurfaceQPitch_start(devinfo) % 32,
         RENDER_SURFACE_STATE_SurfaceQPitch_bits(devinfo));
      /* QPitch in written with >> 2 in ISL (see isl_surface_state.c) */
      data = nir_ishl_imm(b, data, 2);
      break;
   }
   case ISL_SURF_PARAM_FORMAT: {
      nir_def *format_dword = build_load_descriptor_mem(
         b, 1, 32, heap_offset,
         RENDER_SURFACE_STATE_SurfaceFormat_start(devinfo) / 8,
         pdevice);
      data = nir_ubitfield_extract_imm(
         b, format_dword,
         RENDER_SURFACE_STATE_SurfaceFormat_start(devinfo) % 32,
         RENDER_SURFACE_STATE_SurfaceFormat_bits(devinfo));
      break;
   }
   default:
      UNREACHABLE("Invalid surface parameter");
   }

   nir_def_rewrite_uses(&intrin->def, data);
   return true;
}

static bool
lower_intrinsics(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_heap_load:
   case nir_intrinsic_image_heap_store:
   case nir_intrinsic_image_heap_atomic:
   case nir_intrinsic_image_heap_atomic_swap:
   case nir_intrinsic_image_heap_size:
   case nir_intrinsic_image_heap_samples:
   case nir_intrinsic_image_heap_load_raw_intel:
   case nir_intrinsic_image_heap_store_raw_intel:
   case nir_intrinsic_image_heap_sparse_load: {
      b->cursor = nir_before_instr(&intrin->instr);
      nir_src *index_src = nir_get_io_index_src(intrin);
      nir_src_rewrite(
         index_src,
         build_surface_handle(
            b, index_src->ssa, 0,
            nir_intrinsic_access(intrin) & ACCESS_NON_UNIFORM, data));
      intrin->intrinsic = heap_to_bindless_intrinsic(intrin->intrinsic);
      return true;
   }

   case nir_intrinsic_image_heap_load_param_intel:
      return lower_param_intrinsic(b, intrin, intrin->src[0].ssa, data);

   case nir_intrinsic_load_heap_descriptor:
      if (nir_intrinsic_resource_type(intrin) ==
          nir_resource_type_acceleration_structure)
         return lower_accel_struct_intrinsic(b, intrin, data);
      return lower_buffer_intrinsic(b, intrin, data);

   case nir_intrinsic_load_resource_heap_data:
      return lower_resource_heap_data_intrinsic(b, intrin, data);

   default:
      return false;
   }
}

static uint32_t
tex_instr_get_and_remove_plane_src(nir_tex_instr *tex)
{
   int plane_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_plane);
   if (plane_src_idx < 0)
      return 0;

   unsigned plane = nir_src_as_uint(tex->src[plane_src_idx].src);

   nir_tex_instr_remove_src(tex, plane_src_idx);

   return plane;
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex, void *data)
{
   bool progress = false;

   b->cursor = nir_before_instr(&tex->instr);

   uint32_t plane = tex_instr_get_and_remove_plane_src(tex);

   int surf_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_heap_offset);
   if (surf_src_idx >= 0) {
      nir_src_rewrite(
         &tex->src[surf_src_idx].src,
         build_surface_handle(b, tex->src[surf_src_idx].src.ssa,
                              plane, tex->texture_non_uniform, data));
      tex->src[surf_src_idx].src_type = nir_tex_src_texture_handle;
      progress = true;
   }
   int smpl_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_sampler_heap_offset);
   if (smpl_src_idx >= 0) {
      nir_def *sampler_index =
         build_sampler_handle(b, tex->src[smpl_src_idx].src.ssa, plane,
                              tex->sampler_non_uniform, false, data);
      nir_src_rewrite(&tex->src[smpl_src_idx].src, sampler_index);
      tex->src[smpl_src_idx].src_type = nir_tex_src_sampler_handle;
      progress = true;
   } else if (tex->embedded_sampler) {
      nir_def *sampler_index = build_sampler_handle(
         b,
         nir_load_reloc_const_intel(
            b, BRW_SHADER_RELOC_EMBEDDED_SAMPLER_HANDLE + tex->sampler_index),
         plane, tex->sampler_non_uniform, true, data);
      nir_tex_instr_add_src(tex, nir_tex_src_sampler_handle, sampler_index);
      progress = true;
   }

   return progress;
}

static nir_def *
build_buffer_addr_for_deref(nir_builder *b, nir_deref_instr *deref,
                            const struct anv_physical_device *pdevice)
{
   if (deref->deref_type != nir_deref_type_cast) {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);
      nir_def *addr =
         build_buffer_addr_for_deref(b, parent, pdevice);

      b->cursor = nir_before_instr(&deref->instr);
      return nir_explicit_io_address_from_deref(
         b, deref, addr, nir_address_format_32bit_index_offset);
   }

   nir_intrinsic_instr *intrin = nir_src_as_intrinsic(deref->parent);
   assert(intrin->intrinsic == nir_intrinsic_load_heap_descriptor);

   b->cursor = nir_before_instr(&intrin->instr);
   return nir_vec2(b,
                   build_surface_handle(b, intrin->src[0].ssa, 0, 0, pdevice),
                   nir_imm_int(b, 0));
}

static bool
try_lower_direct_buffer_intrinsic(nir_builder *b,
                                  nir_intrinsic_instr *intrin,
                                  bool is_atomic,
                                  const struct anv_physical_device *pdevice)
{
   /* Although we could lower non uniform binding table accesses with
    * nir_opt_non_uniform_access, we might as well use an A64 message and
    * avoid the loops inserted by that lowering pass.
    */
   if (nir_intrinsic_access(intrin) & ACCESS_NON_UNIFORM)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   if ((deref->modes & (nir_var_mem_ubo | nir_var_mem_ssbo)) == 0)
      return false;

   unsigned resource_type;
   nir_def *heap_offset = nir_deref_find_heap_offset(deref, &resource_type);
   if (heap_offset == NULL) {
      return false;
   }
   assert(resource_type == VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT ||
          resource_type == VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT ||
          resource_type == VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT);

   if (resource_type == VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT) {
      /* 64-bit atomics only support A64 messages so we can't lower them to
       * the index+offset model.
       */
      if (is_atomic && intrin->def.bit_size == 64 && !pdevice->info.has_lsc)
         return false;
   }

   deref->modes =
      resource_type == VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT ? nir_var_mem_ubo :
      nir_var_mem_ssbo;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *addr = build_buffer_addr_for_deref(b, deref, pdevice);
   nir_lower_explicit_io_instr(b, intrin, addr,
                               nir_address_format_32bit_index_offset);

   return true;
}

static bool
lower_image_intrinsic(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      const struct anv_physical_device *pdevice)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_src *index_src = nir_get_io_index_src(intrin);
   nir_deref_instr *deref = nir_def_as_deref(index_src->ssa);
   deref = deref->deref_type == nir_deref_type_cast ?
      nir_deref_instr_parent(deref) : deref;
   nir_src_rewrite(
      index_src, build_deref_surface_handle(b, deref, pdevice));
   intrin->intrinsic = deref_to_bindless_intrinsic(intrin->intrinsic);

   return true;
}

static bool
lower_direct_intrinsic(nir_builder *b,
                       nir_intrinsic_instr *intrin,
                       void *data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
      return try_lower_direct_buffer_intrinsic(b, intrin, false, data);

   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_deref_atomic_swap:
      return try_lower_direct_buffer_intrinsic(b, intrin, true, data);

   case nir_intrinsic_get_ssbo_size: {
      b->cursor = nir_before_instr(&intrin->instr);
      nir_def *heap_offset = nir_deref_find_heap_offset(
         nir_src_as_deref(intrin->src[0]), NULL);
      nir_src_rewrite(&intrin->src[0],
                      build_surface_handle(
                         b, heap_offset, 0,
                         nir_intrinsic_access(intrin) & ACCESS_NON_UNIFORM,
                         data));
      return true;
   }

   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_image_deref_load_raw_intel:
   case nir_intrinsic_image_deref_store_raw_intel:
   case nir_intrinsic_image_deref_sparse_load:
      return lower_image_intrinsic(b, intrin, data);

   case nir_intrinsic_image_deref_load_param_intel: {
      b->cursor = nir_before_instr(&intrin->instr);
      nir_src *index_src = nir_get_io_index_src(intrin);
      nir_deref_instr *deref = nir_def_as_deref(index_src->ssa);
      deref = deref->deref_type == nir_deref_type_cast ?
         nir_deref_instr_parent(deref) : deref;
      return lower_param_intrinsic(
         b, intrin, build_deref_surface_handle(b, deref, data), data);
   }

   default:
      return false;
   }
}

bool
anv_nir_lower_descriptor_heap(nir_shader *shader,
                              const struct anv_device *device,
                              uint32_t embedded_sampler_count,
                              const struct vk_sampler_state* embedded_samplers,
                              struct anv_pipeline_bind_map *map)
{
   for (uint32_t i = 0; i < embedded_sampler_count; i++) {
      struct anv_sampler_state anv_state;
      anv_genX(device->info, emit_sampler_state)(device, &embedded_samplers[i],
                                                 0, &anv_state);

      map->embedded_sampler_to_binding[map->embedded_sampler_count++] =
         (struct anv_pipeline_embedded_sampler_binding) {
         .set = -1,
         .binding = i,
         .key = anv_state.embedded_key,
      };
   }

   /* Required to lower buffers efficiently */
   nir_divergence_analysis(shader);

   bool progress =
      nir_shader_intrinsics_pass(shader,
                                 lower_direct_intrinsic,
                                 nir_metadata_control_flow,
                                 (void *)device->physical);
   progress |=
      nir_shader_intrinsics_pass(shader, lower_intrinsics,
                                 nir_metadata_control_flow,
                                 (void *)device->physical);
   progress |=
      nir_shader_tex_pass(shader, lower_tex,
                          nir_metadata_control_flow,
                          (void *)device->physical);

   return progress;
}
