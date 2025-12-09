/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_shader.c which is:
 * Copyright © 2019 Google LLC
 *
 * Also derived from anv_pipeline.c which is
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "panvk_device.h"
#include "panvk_shader.h"

#include "vk_graphics_state.h"
#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"

#include "util/bitset.h"
#include "nir.h"
#include "nir_builder.h"

#if PAN_ARCH >= 9
#define VALHALL_RESOURCE_TABLE_IDX 62
#endif

struct panvk_shader_desc_map {
   /* The index of the map serves as the table offset, the value of the
    * entry is a COPY_DESC_HANDLE() encoding the source set, and the
    * index of the descriptor in the set. */
   uint32_t *map;

   /* Number of entries in the map array. */
   uint32_t count;
};

struct panvk_shader_desc_info {
   uint32_t used_set_mask;
#if PAN_ARCH < 9
   struct panvk_shader_desc_map dyn_ubos;
   struct panvk_shader_desc_map dyn_ssbos;
   struct panvk_shader_desc_map others[PANVK_BIFROST_DESC_TABLE_COUNT];
#else
   uint32_t dummy_sampler_handle;
   uint32_t dyn_bufs_start;
   struct panvk_shader_desc_map dyn_bufs;
   uint32_t num_varying_attr_descs;
#endif
};

struct lower_desc_ctx {
   const struct panvk_descriptor_set_layout *set_layouts[MAX_SETS];
   struct panvk_shader_desc_info desc_info;
   struct hash_table_u64 *ht;
   bool add_bounds_checks;
   bool null_descriptor_support;
   nir_address_format ubo_addr_format;
   nir_address_format ssbo_addr_format;
   struct panvk_shader_variant *shader;
};

static nir_address_format
addr_format_for_desc_type(VkDescriptorType desc_type,
                          const struct lower_desc_ctx *ctx)
{
   switch (desc_type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return ctx->ubo_addr_format;

   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return ctx->ssbo_addr_format;

   default:
      UNREACHABLE("Unsupported descriptor type");
   }
}

static const struct panvk_descriptor_set_layout *
get_set_layout(uint32_t set, const struct lower_desc_ctx *ctx)
{
   return ctx->set_layouts[set];
}

static const struct panvk_descriptor_set_binding_layout *
get_binding_layout(uint32_t set, uint32_t binding,
                   const struct lower_desc_ctx *ctx)
{
   return &get_set_layout(set, ctx)->bindings[binding];
}

struct desc_id {
   union {
      struct {
         uint32_t binding;
         uint32_t set : 4;
         uint32_t sampler_subdesc : 1;
         uint32_t pad : 27;
      };
      uint64_t ht_key;
   };
};

#if PAN_ARCH < 9
static enum panvk_bifrost_desc_table_type
desc_type_to_table_type(
   const struct panvk_descriptor_set_binding_layout *binding_layout,
   bool sampler_subdesc)
{
   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return sampler_subdesc ? PANVK_BIFROST_DESC_TABLE_SAMPLER
                             : PANVK_BIFROST_DESC_TABLE_TEXTURE;
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      return PANVK_BIFROST_DESC_TABLE_TEXTURE;
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      return PANVK_BIFROST_DESC_TABLE_SAMPLER;
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      return PANVK_BIFROST_DESC_TABLE_IMG;
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
      return PANVK_BIFROST_DESC_TABLE_UBO;
   default:
      return PANVK_BIFROST_DESC_TABLE_INVALID;
   }
}
#endif

static uint32_t
shader_desc_idx(uint32_t set, uint32_t binding,
                struct panvk_subdesc_info subdesc,
                const struct lower_desc_ctx *ctx)
{
   const struct panvk_descriptor_set_layout *set_layout =
      get_set_layout(set, ctx);
   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &set_layout->bindings[binding];
   uint32_t subdesc_idx = get_subdesc_idx(bind_layout, subdesc);

   /* On Valhall, all non-dynamic descriptors are accessed directly through
    * their set. The vertex attribute table always comes first, so we always
    * offset user sets by one if we're dealing with a vertex shader. */
   if (PAN_ARCH >= 9 && !vk_descriptor_type_is_dynamic(bind_layout->type))
      return pan_res_handle(set + 1, bind_layout->desc_idx + subdesc_idx);

   /* On Bifrost, the SSBO descriptors are read directly from the set. */
   if (PAN_ARCH < 9 && bind_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
      return bind_layout->desc_idx;

   struct desc_id src = {
      .set = set,
      .sampler_subdesc = subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLER,
      .binding = binding,
   };
   uint32_t *entry =
      _mesa_hash_table_u64_search(ctx->ht, src.ht_key);

   assert(entry);

   const struct panvk_shader_desc_map *map;

#if PAN_ARCH < 9
   if (bind_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
      map = &ctx->desc_info.dyn_ubos;
   } else if (bind_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      map = &ctx->desc_info.dyn_ssbos;
   } else {
      uint32_t table = desc_type_to_table_type(bind_layout, src.sampler_subdesc);

      assert(table < PANVK_BIFROST_DESC_TABLE_COUNT);
      map = &ctx->desc_info.others[table];
   }
#else
   map = &ctx->desc_info.dyn_bufs;
#endif

   assert(entry >= map->map && entry < map->map + map->count);

   uint32_t idx = entry - map->map;

#if PAN_ARCH < 9
   /* Adjust the destination index for all dynamic UBOs, which are laid out
    * just after the regular UBOs in the UBO table. */
   if (bind_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
      idx += ctx->desc_info.others[PANVK_BIFROST_DESC_TABLE_UBO].count;
   } else if (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
      /* the Cb/Cr planes share the same sampler, so in a 3 plane arrangement
       * the number of planes can exceed the number of samplers */
      idx += MIN2(subdesc.plane, bind_layout->samplers_per_desc - 1);
   } else if (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
      assert(subdesc.plane < bind_layout->textures_per_desc);
      idx += subdesc.plane;
   }
#else
   /* Dynamic buffers are pushed directly in the resource tables, after all
    * sets. */
   idx = pan_res_handle(0, ctx->desc_info.dyn_bufs_start + idx);
#endif

   return idx;
}

static nir_address_format
addr_format_for_type(VkDescriptorType type, const struct lower_desc_ctx *ctx)
{
   /* Mutable must imply that both formats are the same. */
   assert(type != VK_DESCRIPTOR_TYPE_MUTABLE_EXT || ctx->ubo_addr_format == ctx->ssbo_addr_format);

   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
   case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
      return ctx->ubo_addr_format;

   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return ctx->ssbo_addr_format;

   default:
      UNREACHABLE("Unsupported descriptor type");
      return ~0;
   }
}

#if PAN_ARCH < 9
static uint32_t
shader_ssbo_table(nir_builder *b, unsigned set, unsigned binding,
                  const struct lower_desc_ctx *ctx)
{
   const struct panvk_descriptor_set_layout *set_layout =
      get_set_layout(set, ctx);
   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &set_layout->bindings[binding];

   assert(bind_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
          bind_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
   bool is_dyn = bind_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

   if (!is_dyn)
      return PANVK_DESC_TABLE_USER + set;

   switch (b->shader->info.stage) {
   case MESA_SHADER_COMPUTE:
      return PANVK_DESC_TABLE_CS_DYN_SSBOS;
   case MESA_SHADER_VERTEX:
      return PANVK_DESC_TABLE_VS_DYN_SSBOS;
   case MESA_SHADER_FRAGMENT:
      return PANVK_DESC_TABLE_FS_DYN_SSBOS;
   default:
      assert(!"Invalid stage");
      return ~0;
   }
}
#endif

/** Build a Vulkan resource index
 *
 * A "resource index" is the term used by our SPIR-V parser and the relevant
 * NIR intrinsics for a reference into a descriptor set.  It acts much like a
 * deref in NIR except that it accesses opaque descriptors instead of memory.
 *
 * Coming out of SPIR-V, both the resource indices (in the form of
 * vulkan_resource_[re]index intrinsics) and the memory derefs (in the form
 * of nir_deref_instr) use the same vector component/bit size.  The meaning
 * of those values for memory derefs (nir_deref_instr) is given by the
 * nir_address_format associated with the descriptor type.  For resource
 * indices, it's an entirely internal to panvk encoding which describes, in
 * some sense, the address of the descriptor.  Thanks to the NIR/SPIR-V rules,
 * it must be packed into the same size SSA values as a memory address.  For
 * this reason, the actual encoding may depend both on the address format for
 * memory derefs and the descriptor address format.
 *
 * The load_vulkan_descriptor intrinsic exists to provide a transition point
 * between these two forms of derefs: descriptor and memory.
 */
static nir_def *
build_res_index(nir_builder *b, uint32_t set, uint32_t binding,
                nir_def *array_index, nir_address_format addr_format,
                const struct lower_desc_ctx *ctx)
{
   const struct panvk_descriptor_set_layout *set_layout =
      get_set_layout(set, ctx);
   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &set_layout->bindings[binding];
   uint32_t array_size = bind_layout->desc_count;
   nir_address_format addr_fmt = addr_format_for_type(bind_layout->type, ctx);
   uint32_t desc_idx = shader_desc_idx(set, binding, NO_SUBDESC, ctx);

   switch (addr_fmt) {
#if PAN_ARCH < 9
   case nir_address_format_32bit_index_offset: {
      const uint32_t packed_desc_idx_array_size =
         (array_size - 1) << 16 | desc_idx;

      return nir_vec2(b, nir_imm_int(b, packed_desc_idx_array_size),
                      array_index);
   }

   case nir_address_format_64bit_bounded_global:
   case nir_address_format_64bit_global_32bit_offset: {
      unsigned desc_table = shader_ssbo_table(b, set, binding, ctx);

      return nir_vec4(b, nir_imm_int(b, desc_table),
                      nir_imm_int(b, desc_idx), array_index,
                      nir_imm_int(b, array_size - 1));
   }
#else
   case nir_address_format_vec2_index_32bit_offset:
      return nir_vec3(b, nir_imm_int(b, desc_idx), array_index,
                      nir_imm_int(b, array_size - 1));
#endif

   default:
      UNREACHABLE("Unsupported descriptor type");
   }
}

/** Adjust a Vulkan resource index
 *
 * This is the equivalent of nir_deref_type_ptr_as_array for resource indices.
 * For array descriptors, it allows us to adjust the array index.  Thanks to
 * variable pointers, we cannot always fold this re-index operation into the
 * vulkan_resource_index intrinsic and we have to do it based on nothing but
 * the address format.
 */
static nir_def *
build_res_reindex(nir_builder *b, nir_def *orig, nir_def *delta,
                  nir_address_format addr_format)
{
   switch (addr_format) {
#if PAN_ARCH < 9
   case nir_address_format_32bit_index_offset:
      return nir_vec2(b, nir_channel(b, orig, 0),
                      nir_iadd(b, nir_channel(b, orig, 1), delta));

   case nir_address_format_64bit_bounded_global:
   case nir_address_format_64bit_global_32bit_offset:
      return nir_vec4(b, nir_channel(b, orig, 0), nir_channel(b, orig, 1),
                      nir_iadd(b, nir_channel(b, orig, 2), delta),
                      nir_imm_int(b, 3));
#else
   case nir_address_format_vec2_index_32bit_offset:
      return nir_vec3(b, nir_channel(b, orig, 0),
                      nir_iadd(b, nir_channel(b, orig, 1), delta),
                      nir_channel(b, orig, 2));
#endif

   default:
      UNREACHABLE("Unhandled address format");
   }
}

/** Convert a Vulkan resource index into a buffer address
 *
 * In some cases, this does a  memory load from the descriptor set and, in
 * others, it simply converts from one form to another.
 *
 * See build_res_index for details about each resource index format.
 */
static nir_def *
build_buffer_addr_for_res_index(nir_builder *b, nir_def *res_index,
                                nir_address_format addr_format,
                                const struct lower_desc_ctx *ctx)
{
   switch (addr_format) {
#if PAN_ARCH < 9
   case nir_address_format_32bit_index_offset: {
      nir_def *packed = nir_channel(b, res_index, 0);
      nir_def *array_index = nir_channel(b, res_index, 1);
      nir_def *first_desc_index = nir_extract_u16(b, packed, nir_imm_int(b, 0));
      nir_def *array_max = nir_extract_u16(b, packed, nir_imm_int(b, 1));

      if (ctx->add_bounds_checks)
         array_index = nir_umin(b, array_index, array_max);

      return nir_vec2(b, nir_iadd(b, first_desc_index, array_index),
                      nir_imm_int(b, 0));
   }

   case nir_address_format_64bit_bounded_global:
   case nir_address_format_64bit_global_32bit_offset: {
      nir_def *desc_table_index = nir_channel(b, res_index, 0);
      nir_def *first_desc_index = nir_channel(b, res_index, 1);
      nir_def *array_index = nir_channel(b, res_index, 2);
      nir_def *array_max = nir_channel(b, res_index, 3);

      if (ctx->add_bounds_checks)
         array_index = nir_umin(b, array_index, array_max);

      nir_def *desc_offset = nir_imul_imm(
         b, nir_iadd(b, array_index, first_desc_index), PANVK_DESCRIPTOR_SIZE);

      nir_def *base_addr =
         b->shader->info.stage == MESA_SHADER_COMPUTE
            ? load_sysval_entry(b, compute, 64, desc.sets, desc_table_index)
            : load_sysval_entry(b, graphics, 64, desc.sets, desc_table_index);

      nir_def *desc_addr = nir_iadd(b, base_addr, nir_u2u64(b, desc_offset));
      nir_def *desc = nir_load_global(b, 4, 32, desc_addr,
                                      .align_mul = PANVK_DESCRIPTOR_SIZE);

      /* The offset in the descriptor is guaranteed to be zero when it's
       * written into the descriptor set.  This lets us avoid some unnecessary
       * adds.
       */
      return nir_vec4(b, nir_channel(b, desc, 0), nir_channel(b, desc, 1),
                      nir_channel(b, desc, 2), nir_imm_int(b, 0));
   }
#else
   case nir_address_format_vec2_index_32bit_offset: {
      nir_def *first_desc_index = nir_channel(b, res_index, 0);
      nir_def *array_index = nir_channel(b, res_index, 1);
      nir_def *array_max = nir_channel(b, res_index, 2);

      if (ctx->add_bounds_checks)
         array_index = nir_umin(b, array_index, array_max);

      return nir_vec3(b, first_desc_index, array_index, nir_imm_int(b, 0));
   }
#endif

   default:
      UNREACHABLE("Unhandled address format");
   }
}

static bool
lower_res_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin,
                    const struct lower_desc_ctx *ctx)
{
   b->cursor = nir_before_instr(&intrin->instr);

   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   nir_address_format addr_format = addr_format_for_desc_type(desc_type, ctx);

   nir_def *res;
   switch (intrin->intrinsic) {
   case nir_intrinsic_vulkan_resource_index:
      res = build_res_index(b, nir_intrinsic_desc_set(intrin),
                            nir_intrinsic_binding(intrin), intrin->src[0].ssa,
                            addr_format, ctx);
      break;

   case nir_intrinsic_vulkan_resource_reindex:
      res = build_res_reindex(b, intrin->src[0].ssa, intrin->src[1].ssa,
                              addr_format);
      break;

   case nir_intrinsic_load_vulkan_descriptor:
      res = build_buffer_addr_for_res_index(b, intrin->src[0].ssa, addr_format,
                                            ctx);
      break;

   default:
      UNREACHABLE("Unhandled resource intrinsic");
   }

   assert(intrin->def.bit_size == res->bit_size);
   assert(intrin->def.num_components == res->num_components);
   nir_def_replace(&intrin->def, res);

   return true;
}

static void
get_resource_deref_binding(nir_deref_instr *deref, uint32_t *set,
                           uint32_t *binding, uint32_t *index_imm,
                           nir_def **index_ssa, uint32_t *max_idx)
{
   *index_imm = 0;
   *max_idx = 0;
   *index_ssa = NULL;

   if (deref->deref_type == nir_deref_type_array) {
      if (nir_src_is_const(deref->arr.index)) {
         *index_imm = nir_src_as_uint(deref->arr.index);
         *max_idx = *index_imm;
      } else {
         *index_ssa = deref->arr.index.ssa;

         /* Zero means variable array. The minus one should give us UINT32_MAX,
          * which matches what we want. */
         *max_idx = ((uint32_t)glsl_array_size(nir_deref_instr_parent(deref)->type)) - 1;
      }

      deref = nir_deref_instr_parent(deref);
   }

   assert(deref->deref_type == nir_deref_type_var);
   nir_variable *var = deref->var;

   *set = var->data.descriptor_set;
   *binding = var->data.binding;
}

static nir_def *
load_resource_deref_desc(nir_builder *b, nir_deref_instr *deref,
                         VkDescriptorType subdesc_type, unsigned desc_offset,
                         unsigned num_components, unsigned bit_size,
                         const struct lower_desc_ctx *ctx)
{
   uint32_t set, binding, index_imm, max_idx;
   nir_def *index_ssa;
   get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa,
                              &max_idx);

   const struct panvk_descriptor_set_layout *set_layout =
      get_set_layout(set, ctx);
   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &set_layout->bindings[binding];
   struct panvk_subdesc_info subdesc =
      subdesc_type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
         ? get_tex_subdesc_info(bind_layout->type, 0)
         : subdesc_type == VK_DESCRIPTOR_TYPE_SAMPLER
            ? get_sampler_subdesc_info(bind_layout->type, 0)
            : NO_SUBDESC;

   unsigned subdesc_idx = get_subdesc_idx(bind_layout, subdesc);

   assert(index_ssa == NULL || index_imm == 0);
   if (index_ssa == NULL)
      index_ssa = nir_imm_int(b, index_imm);

   unsigned desc_stride = panvk_get_desc_stride(bind_layout);
   nir_def *set_offset =
      nir_imul_imm(b,
                   nir_iadd_imm(b, nir_imul_imm(b, index_ssa, desc_stride),
                                bind_layout->desc_idx + subdesc_idx),
                   PANVK_DESCRIPTOR_SIZE);

   set_offset = nir_iadd_imm(b, set_offset, desc_offset);

#if PAN_ARCH < 9
   nir_def *set_base_addr =
      b->shader->info.stage == MESA_SHADER_COMPUTE
         ? load_sysval_entry(b, compute, 64, desc.sets, nir_imm_int(b, set))
         : load_sysval_entry(b, graphics, 64, desc.sets, nir_imm_int(b, set));

   unsigned desc_align = 1 << (ffs(PANVK_DESCRIPTOR_SIZE + desc_offset) - 1);

   return nir_load_global(b, num_components, bit_size,
                          nir_iadd(b, set_base_addr, nir_u2u64(b, set_offset)),
                          .align_mul = desc_align);
#else
   /* note that user sets start from index 1 */
   return nir_load_ubo(
      b, num_components, bit_size,
      nir_imm_int(b, pan_res_handle(VALHALL_RESOURCE_TABLE_IDX, set + 1)),
      set_offset, .range = ~0u, .align_mul = PANVK_DESCRIPTOR_SIZE,
      .align_offset = desc_offset);
#endif
}

static nir_def *
is_nulldesc(nir_builder *b, nir_deref_instr *deref,
            enum VkDescriptorType desc_type, const struct lower_desc_ctx *ctx)
{
   nir_def *desc_header =
      load_resource_deref_desc(b, deref, desc_type, 0, 1, 16, ctx);
   /* If the first 16 bits are all zero (specifically the descriptor type),
    * this is a nulldescriptor, in which case we need to avoid the "add 1"
    * when loading the size from the descriptor. */
   return nir_ieq_imm(b, desc_header, 0);
}

static nir_def *
load_tex_size(nir_builder *b, nir_deref_instr *deref, enum glsl_sampler_dim dim,
              bool is_array, const struct lower_desc_ctx *ctx)
{
   nir_def *loaded_size;
   if (dim == GLSL_SAMPLER_DIM_BUF) {
#if PAN_ARCH >= 9
      nir_def *bytes = load_resource_deref_desc(
         b, deref, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 4, 1, 32, ctx);
      nir_def *stride = load_resource_deref_desc(
         b, deref, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 16, 1, 32, ctx);
      loaded_size = nir_idiv(b, nir_u2u32(b, bytes), nir_u2u32(b, stride));
#else
      nir_def *tex_w = load_resource_deref_desc(
         b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, 1, 16, ctx);

      /* S dimension is 16 bits wide. We don't support combining S,T dimensions
       * to allow large buffers yet. */
      loaded_size = nir_iadd_imm(b, nir_u2u32(b, tex_w), 1);
#endif
   } else {
      nir_def *tex_w_h = load_resource_deref_desc(
         b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4, 2, 16, ctx);
      nir_def *tex_depth_or_layer_count = load_resource_deref_desc(
         b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         dim == GLSL_SAMPLER_DIM_3D ? 28 : 24, 1, 16, ctx);

      nir_def *tex_sz =
         is_array && dim == GLSL_SAMPLER_DIM_1D
            ? nir_vec2(b, nir_channel(b, tex_w_h, 0), tex_depth_or_layer_count)
            : nir_vec3(b, nir_channel(b, tex_w_h, 0),
                       nir_channel(b, tex_w_h, 1), tex_depth_or_layer_count);

      tex_sz = nir_pad_vector_imm_int(b, tex_sz, 0, 4);

      /* The sizes are provided as 16-bit values with 1 subtracted so
       * convert to 32-bit and add 1.
       */
      loaded_size = nir_iadd_imm(b, nir_u2u32(b, tex_sz), 1);
   }

   if (PAN_ARCH >= 9 && ctx->null_descriptor_support) {
      nir_def *nulldesc =
         is_nulldesc(b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ctx);
      return nir_bcsel(b, nulldesc, nir_u2u32(b, nir_imm_int(b, 0)),
                       loaded_size);
   }

   return loaded_size;
}

static nir_def *
load_img_size(nir_builder *b, nir_deref_instr *deref, enum glsl_sampler_dim dim,
              bool is_array, const struct lower_desc_ctx *ctx)
{
   if (PAN_ARCH >= 9)
      return load_tex_size(b, deref, dim, is_array, ctx);

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      nir_def *tex_w = load_resource_deref_desc(
         b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 18, 1, 16, ctx);

      /* S dimension is 16 bits wide. We don't support combining S,T dimensions
       * to allow large buffers yet. */
      return nir_iadd_imm(b, nir_u2u32(b, tex_w), 1);
   } else {
      nir_def *tex_sz = load_resource_deref_desc(
         b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 18, 3, 16, ctx);

#if PAN_ARCH < 9
      if (is_array && dim == GLSL_SAMPLER_DIM_CUBE)
         tex_sz =
            nir_vector_insert_imm(b, tex_sz,
                                     nir_udiv_imm(b, nir_channel(b, tex_sz, 2),
                                                     6),
                                     2);
#endif

      if (is_array && dim == GLSL_SAMPLER_DIM_1D)
         tex_sz =
            nir_vec2(b, nir_channel(b, tex_sz, 0), nir_channel(b, tex_sz, 2));

      tex_sz = nir_pad_vector_imm_int(b, tex_sz, 0, 4);

      /* The sizes are provided as 16-bit values with 1 subtracted so
       * convert to 32-bit and add 1.
       */
      return nir_iadd_imm(b, nir_u2u32(b, tex_sz), 1);
   }
}

static nir_def *
load_tex_levels(nir_builder *b, nir_deref_instr *deref,
                enum glsl_sampler_dim dim, const struct lower_desc_ctx *ctx)
{
   assert(dim != GLSL_SAMPLER_DIM_BUF);

   /* LOD count is stored in word2[16:21] and has a minus(1) modifier. */
   nir_def *tex_word2 = load_resource_deref_desc(
      b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8, 1, 32, ctx);
   nir_def *lod_count = nir_iand_imm(b, nir_ushr_imm(b, tex_word2, 16), 0x1f);
   nir_def *loaded_levels = nir_iadd_imm(b, lod_count, 1);

   if (PAN_ARCH >= 9 && ctx->null_descriptor_support) {
      nir_def *nulldesc =
         is_nulldesc(b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ctx);
      return nir_bcsel(b, nulldesc, nir_imm_int(b, 0), loaded_levels);
   }

   return loaded_levels;
}

static nir_def *
load_tex_samples(nir_builder *b, nir_deref_instr *deref,
                 enum glsl_sampler_dim dim, const struct lower_desc_ctx *ctx)
{
   assert(dim != GLSL_SAMPLER_DIM_BUF);

   /* Sample count is stored in word3[13:25], and has a log2 modifier. */
   nir_def *tex_word3 = load_resource_deref_desc(
      b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 12, 1, 32, ctx);
   nir_def *sample_count = nir_iand_imm(b, nir_ushr_imm(b, tex_word3, 13), 0x7);
   nir_def *loaded_samples = nir_ishl(b, nir_imm_int(b, 1), sample_count);

   if (PAN_ARCH >= 9 && ctx->null_descriptor_support) {
      nir_def *nulldesc =
         is_nulldesc(b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ctx);
      return nir_bcsel(b, nulldesc, nir_imm_int(b, 0), loaded_samples);
   }

   return loaded_samples;
}

static nir_def *
load_img_samples(nir_builder *b, nir_deref_instr *deref,
                 enum glsl_sampler_dim dim, const struct lower_desc_ctx *ctx)
{
   if (PAN_ARCH >= 9)
      return load_tex_samples(b, deref, dim, ctx);

   assert(dim != GLSL_SAMPLER_DIM_BUF);

   /* Sample count is stored in the image depth field.
    * FIXME: This won't work for 2DMSArray images, but those are already
    * broken. */
   nir_def *sample_count = load_resource_deref_desc(
      b, deref, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 22, 1, 16, ctx);
   return nir_iadd_imm(b, nir_u2u32(b, sample_count), 1);
}

static uint32_t
get_desc_array_stride(const struct panvk_descriptor_set_binding_layout *layout,
                      VkDescriptorType subtype)
{
   if (PAN_ARCH >= 9)
      return panvk_get_desc_stride(layout);

   if (layout->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      return 1;

   /* On Bifrost, descriptors are copied from the sets to the final
    * descriptor tables which are per-type. For combined image-sampler,
    * the stride is {textures/samplers}_per_desc in this context;
    * otherwise the stride is one. */
   switch(subtype) {
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      return layout->textures_per_desc;
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      return layout->samplers_per_desc;
   default:
      UNREACHABLE("Invalid subtype");
   }
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex, const struct lower_desc_ctx *ctx)
{
   bool progress = false;

   b->cursor = nir_before_instr(&tex->instr);

   if (tex->op == nir_texop_txs || tex->op == nir_texop_query_levels ||
       tex->op == nir_texop_texture_samples) {
      int tex_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
      assert(tex_src_idx >= 0);
      nir_deref_instr *deref = nir_src_as_deref(tex->src[tex_src_idx].src);

      const enum glsl_sampler_dim dim = tex->sampler_dim;

      nir_def *res;
      switch (tex->op) {
      case nir_texop_txs:
         res = nir_channels(b, load_tex_size(b, deref, dim, tex->is_array, ctx),
                            nir_component_mask(tex->def.num_components));
         break;
      case nir_texop_query_levels:
         assert(tex->def.num_components == 1);
         res = load_tex_levels(b, deref, dim, ctx);
         break;
      case nir_texop_texture_samples:
         assert(tex->def.num_components == 1);
         res = load_tex_samples(b, deref, dim, ctx);
         break;
      default:
         UNREACHABLE("Unsupported texture query op");
      }

      nir_def_replace(&tex->def, res);
      return true;
   }

   uint32_t plane = 0;
   int sampler_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (sampler_src_idx >= 0) {
      nir_def *plane_ssa = nir_steal_tex_src(tex, nir_tex_src_plane);
      plane =
         plane_ssa ? nir_src_as_uint(nir_src_for_ssa(plane_ssa)) : 0;

      nir_deref_instr *deref = nir_src_as_deref(tex->src[sampler_src_idx].src);
      nir_tex_instr_remove_src(tex, sampler_src_idx);

      uint32_t set, binding, index_imm, max_idx;
      nir_def *index_ssa;
      get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa, &max_idx);

      const struct panvk_descriptor_set_layout *set_layout =
         get_set_layout(set, ctx);
      const struct panvk_descriptor_set_binding_layout *bind_layout =
         &set_layout->bindings[binding];
      struct panvk_subdesc_info subdesc =
         get_sampler_subdesc_info(bind_layout->type, plane);
      uint32_t desc_stride = get_desc_array_stride(bind_layout, subdesc.type);
      uint32_t sampler_index = shader_desc_idx(set, binding, subdesc, ctx) + index_imm * desc_stride;

      if (index_ssa != NULL) {
         nir_def *offset = nir_imul_imm(b, index_ssa, desc_stride);
         offset = nir_iadd_imm(b, offset, sampler_index);
         nir_tex_instr_add_src(tex, nir_tex_src_sampler_offset, offset);
      } else {
         tex->sampler_index = sampler_index;
      }
      progress = true;
   } else {
#if PAN_ARCH >= 9
      tex->sampler_index = ctx->desc_info.dummy_sampler_handle;
#endif
   }

   int tex_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (tex_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[tex_src_idx].src);
      nir_tex_instr_remove_src(tex, tex_src_idx);

      uint32_t set, binding, index_imm, max_idx;
      nir_def *index_ssa;
      get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa,
                                 &max_idx);

      const struct panvk_descriptor_set_layout *set_layout =
         get_set_layout(set, ctx);
      const struct panvk_descriptor_set_binding_layout *bind_layout =
         &set_layout->bindings[binding];
      struct panvk_subdesc_info subdesc =
         get_tex_subdesc_info(bind_layout->type, plane);
      uint32_t desc_stride = get_desc_array_stride(bind_layout, subdesc.type);
      uint32_t texture_index = shader_desc_idx(set, binding, subdesc, ctx) + index_imm * desc_stride;

      if (index_ssa != NULL) {
         nir_def *offset = nir_imul_imm(b, index_ssa, desc_stride);
         offset = nir_iadd_imm(b, offset, texture_index);
         nir_tex_instr_add_src(tex, nir_tex_src_texture_offset, offset);
      } else {
         tex->texture_index = texture_index;
      }
      progress = true;
   }

   return progress;
}

static nir_def *
get_img_index(nir_builder *b, nir_deref_instr *deref,
              const struct lower_desc_ctx *ctx)
{
   uint32_t set, binding, index_imm, max_idx;
   nir_def *index_ssa;
   get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa,
                              &max_idx);

   const struct panvk_descriptor_set_binding_layout *bind_layout =
      get_binding_layout(set, binding, ctx);
   assert(bind_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
          bind_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
          bind_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
          bind_layout->type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT);

   unsigned img_offset = shader_desc_idx(set, binding, NO_SUBDESC, ctx);

   if (index_ssa == NULL) {
      return nir_imm_int(b, img_offset + index_imm);
   } else {
      assert(index_imm == 0);
      return nir_iadd_imm(b, index_ssa, img_offset);
   }
}

struct panvk_lower_input_attachment_load_ctx {
   uint32_t ro_color_mask;
   struct panvk_shader_variant *shader;
};

static bool
lower_input_attachment_load(nir_builder *b, nir_intrinsic_instr *intr,
                            void *data)
{
   if (intr->intrinsic != nir_intrinsic_image_deref_load &&
       intr->intrinsic != nir_intrinsic_image_deref_sparse_load)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   enum glsl_sampler_dim image_dim = glsl_get_sampler_dim(deref->type);
   if (image_dim != GLSL_SAMPLER_DIM_SUBPASS &&
       image_dim != GLSL_SAMPLER_DIM_SUBPASS_MS)
      return false;

   const struct panvk_lower_input_attachment_load_ctx *ctx = data;
   struct panvk_shader_variant *shader = ctx->shader;
   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(var);

   b->cursor = nir_before_instr(&intr->instr);

   uint32_t set, binding, index_imm, max_idx;
   nir_def *index_ssa;
   get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa, &max_idx);
   const unsigned base_idx =
      var->data.index != NIR_VARIABLE_NO_INDEX ? var->data.index + 1 : 0;
   index_imm += base_idx;
   index_ssa = index_ssa ?
      nir_iadd_imm(b, index_ssa, base_idx) : nir_imm_int(b, index_imm);

   nir_alu_type dest_type = nir_intrinsic_dest_type(intr);

   unsigned range = max_idx == UINT32_MAX ? 9 - index_imm : max_idx + 1;
   shader->fs.input_attachment_read |= BITFIELD_RANGE(index_imm, range);

   nir_def *target = nir_load_input_attachment_target_pan(b, index_ssa);
   nir_def *load_img, *load_output;

   nir_push_if(b, nir_ine_imm(b, target, ~0));
   {
      nir_def *is_color_att = nir_ilt_imm(b, target, 8);
      nir_def *load_color, *load_zs;
      nir_io_semantics iosem = {0};
      iosem.fb_fetch_output = true;
      iosem.fb_fetch_output_coherent = !!(nir_intrinsic_access(intr) & ACCESS_COHERENT);

      nir_push_if(b, is_color_att);
      {
         nir_def *conversion =
            nir_load_input_attachment_conv_pan(b, index_ssa);
         nir_def *is_read_only =
            nir_i2b(b, nir_iand_imm(b, nir_ishl(b, nir_imm_int(b, 1), target),
                                    ctx->ro_color_mask));
         nir_def *load_ro_color, *load_rw_color;

         iosem.location = FRAG_RESULT_DATA0;
         nir_push_if(b, is_read_only);
	 {
            load_ro_color = nir_load_readonly_output_pan(
               b, intr->def.num_components, intr->def.bit_size, target,
               intr->src[2].ssa, conversion, .dest_type = dest_type,
               .access = nir_intrinsic_access(intr), .io_semantics = iosem);
         }
         nir_push_else(b, NULL);
         {
            load_rw_color = nir_load_converted_output_pan(
               b, intr->def.num_components, intr->def.bit_size, target,
               intr->src[2].ssa, conversion, .dest_type = dest_type,
               .access = nir_intrinsic_access(intr), .io_semantics = iosem);
         }
         nir_pop_if(b, NULL);
         load_color = nir_if_phi(b, load_ro_color, load_rw_color);
      }
      nir_push_else(b, NULL);
      {
#if PAN_ARCH < 9
         /* On v7, we need to pass the depth format around. If we use a
          * conversion of zero, like we do on v9+, the GPU reports an
          * INVALID_INSTR_ENC. */
         struct mali_internal_conversion_packed stencil_conv;

         pan_pack(&stencil_conv, INTERNAL_CONVERSION, cfg) {
            cfg.register_format = MALI_REGISTER_FILE_FORMAT_U32;
            cfg.memory_format = GENX(pan_dithered_format_from_pipe_format)(
               PIPE_FORMAT_S8_UINT, false);
         }

         nir_def *conversion =
            dest_type == nir_type_uint32
               ? nir_imm_int(b, stencil_conv.opaque[0])
               : nir_load_input_attachment_conv_pan(b, index_ssa);
#else
         nir_def *conversion = nir_imm_int(b, 0);
#endif

         iosem.location = dest_type == nir_type_float32 ? FRAG_RESULT_DEPTH
                                                        : FRAG_RESULT_STENCIL;
         target = nir_imm_int(b, 0);
         load_zs = nir_load_converted_output_pan(
            b, intr->def.num_components, intr->def.bit_size, target,
            intr->src[2].ssa, conversion, .dest_type = dest_type,
            .access = nir_intrinsic_access(intr), .io_semantics = iosem);

         /* If we loaded the stencil value, the upper 24 bits might contain
	  * garbage, hence the masking done here. */
         if (iosem.location == FRAG_RESULT_STENCIL)
            load_zs = nir_iand_imm(b, load_zs, BITFIELD_MASK(8));
      }
      nir_pop_if(b, NULL);

      load_output = nir_if_phi(b, load_color, load_zs);
   }
   nir_push_else(b, NULL);
   {
      load_img =
         intr->intrinsic == nir_intrinsic_image_deref_sparse_load
            ? nir_image_deref_sparse_load(
                 b, intr->num_components, intr->def.bit_size, intr->src[0].ssa,
                 intr->src[1].ssa, intr->src[2].ssa, intr->src[3].ssa,
                 .image_dim = nir_intrinsic_image_dim(intr),
                 .image_array = nir_intrinsic_image_array(intr),
                 .format = nir_intrinsic_format(intr),
                 .access = nir_intrinsic_access(intr), .dest_type = dest_type)
            : nir_image_deref_load(
                 b, intr->num_components, intr->def.bit_size, intr->src[0].ssa,
                 intr->src[1].ssa, intr->src[2].ssa, intr->src[3].ssa,
                 .image_dim = nir_intrinsic_image_dim(intr),
                 .image_array = nir_intrinsic_image_array(intr),
                 .format = nir_intrinsic_format(intr),
                 .access = nir_intrinsic_access(intr), .dest_type = dest_type);
   }
   nir_pop_if(b, NULL);

   nir_def_replace(&intr->def, nir_if_phi(b, load_output, load_img));

   return true;
}

static bool
collect_frag_writes(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);

   if (deref->modes != nir_var_shader_out)
      return false;

   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(var);

   if (var->data.location < FRAG_RESULT_DATA0 ||
       var->data.location > FRAG_RESULT_DATA7)
      return false;

   uint32_t *written_mask = data;

   *written_mask |= BITFIELD_BIT(var->data.location - FRAG_RESULT_DATA0);
   return true;
}

static uint32_t
readonly_color_mask(nir_shader *nir,
                    const struct vk_graphics_pipeline_state *state)
{
   if (!state || !state->ial || !state->cal)
      return 0;

   uint32_t in_mask = 0, out_mask = 0;

   for (uint32_t i = 0; i < ARRAY_SIZE(state->ial->color_map); i++) {
      if (i >= state->ial->color_attachment_count)
         break;

      if (state->ial->color_map[i] != MESA_VK_ATTACHMENT_UNUSED)
         in_mask |= BITFIELD_BIT(i);
   }

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, collect_frag_writes,
            nir_metadata_all, &out_mask);

   for (uint32_t i = 0; i < ARRAY_SIZE(state->cal->color_map); i++) {
      if (state->ial->color_map[i] == MESA_VK_ATTACHMENT_UNUSED)
         out_mask &= ~BITFIELD_BIT(i);
   }

   return in_mask & ~out_mask;
}

static bool
lower_input_attachment_loads(nir_shader *nir,
                             const struct vk_graphics_pipeline_state *state,
                             struct panvk_shader_variant *shader)
{
   bool progress = false;
   struct panvk_lower_input_attachment_load_ctx ia_load_ctx = {
      .ro_color_mask = readonly_color_mask(nir, state),
      .shader = shader,
   };

   NIR_PASS(progress, nir, nir_shader_intrinsics_pass,
            lower_input_attachment_load, nir_metadata_none,
            &ia_load_ctx);

   /* Lower the remaining input attachment loads. */
   struct nir_input_attachment_options lower_input_attach_opts = { };
   NIR_PASS(progress, nir, nir_lower_input_attachments,
            &lower_input_attach_opts);

   return progress;
}

static bool
lower_img_intrinsic(nir_builder *b, nir_intrinsic_instr *intr,
                    struct lower_desc_ctx *ctx)
{
   b->cursor = nir_before_instr(&intr->instr);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);

   if (intr->intrinsic == nir_intrinsic_image_deref_size ||
       intr->intrinsic == nir_intrinsic_image_deref_samples) {
      const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
      bool is_array = nir_intrinsic_image_array(intr);

      nir_def *res;
      switch (intr->intrinsic) {
      case nir_intrinsic_image_deref_size:
         res = nir_channels(b, load_img_size(b, deref, dim, is_array, ctx),
                            nir_component_mask(intr->def.num_components));
         break;
      case nir_intrinsic_image_deref_samples:
         res = load_img_samples(b, deref, dim, ctx);
         break;
      default:
         UNREACHABLE("Unsupported image query op");
      }

      nir_def_replace(&intr->def, res);
   } else {
      nir_rewrite_image_intrinsic(intr, get_img_index(b, deref, ctx), false);
   }

   return true;
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *intr,
                struct lower_desc_ctx *ctx)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_vulkan_resource_index:
   case nir_intrinsic_vulkan_resource_reindex:
   case nir_intrinsic_load_vulkan_descriptor:
      return lower_res_intrinsic(b, intr, ctx);
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
      return lower_img_intrinsic(b, intr, ctx);
   default:
      return false;
   }
}

static bool
lower_descriptors_instr(nir_builder *b, nir_instr *instr, void *data)
{
   struct lower_desc_ctx *ctx = data;

   switch (instr->type) {
   case nir_instr_type_tex:
      return lower_tex(b, nir_instr_as_tex(instr), ctx);
   case nir_instr_type_intrinsic:
      return lower_intrinsic(b, nir_instr_as_intrinsic(instr), ctx);
   default:
      return false;
   }
}

static void
record_binding(struct lower_desc_ctx *ctx, unsigned set, unsigned binding,
               struct panvk_subdesc_info subdesc, uint32_t max_idx)
{
   const struct panvk_descriptor_set_layout *set_layout = ctx->set_layouts[set];
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set_layout->bindings[binding];
   uint32_t subdesc_idx = get_subdesc_idx(binding_layout, subdesc);
   uint32_t desc_stride = panvk_get_desc_stride(binding_layout);
   uint32_t max_desc_stride = MAX2(
      binding_layout->samplers_per_desc + binding_layout->textures_per_desc, 1);

   assert(desc_stride >= 1 && desc_stride <= max_desc_stride);
   ctx->desc_info.used_set_mask |= BITFIELD_BIT(set);

   /* On valhall, we only record dynamic bindings, others are accessed directly
    * from the set. */
   if (PAN_ARCH >= 9 && !vk_descriptor_type_is_dynamic(binding_layout->type))
      return;

   /* SSBOs are accessed directly from the sets, no need to record accesses
    * to such resources. */
   if (PAN_ARCH < 9 &&
       binding_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
      return;

   assert(subdesc_idx < desc_stride);

   struct desc_id src = {
      .set = set,
      .sampler_subdesc = subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLER,
      .binding = binding,
   };
   uint32_t *entry = _mesa_hash_table_u64_search(ctx->ht, src.ht_key);
   uint32_t old_desc_count = (uintptr_t)entry;
   uint32_t new_desc_count =
      max_idx == UINT32_MAX ? binding_layout->desc_count : max_idx + 1;

   assert(new_desc_count <= binding_layout->desc_count);

   if (old_desc_count >= new_desc_count)
      return;

   _mesa_hash_table_u64_insert(ctx->ht, src.ht_key,
                               (void *)(uintptr_t)new_desc_count);

   uint32_t desc_count_diff = new_desc_count - old_desc_count;

#if PAN_ARCH < 9
   if (binding_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
      ctx->desc_info.dyn_ubos.count += desc_count_diff;
   } else if (binding_layout->type ==
              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      ctx->desc_info.dyn_ssbos.count += desc_count_diff;
   } else {
      uint32_t table =
         desc_type_to_table_type(binding_layout, src.sampler_subdesc);

      assert(table < PANVK_BIFROST_DESC_TABLE_COUNT);
      ctx->desc_info.others[table].count +=
         desc_count_diff * get_desc_array_stride(binding_layout, subdesc.type);
   }
#else
   ctx->desc_info.dyn_bufs.count += desc_count_diff;
#endif
}

static uint32_t *
fill_copy_descs_for_binding(struct lower_desc_ctx *ctx, unsigned set,
                            unsigned binding, bool sampler_subdesc,
                            uint32_t desc_count)
{
   assert(desc_count);

   const struct panvk_descriptor_set_layout *set_layout = ctx->set_layouts[set];
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      &set_layout->bindings[binding];
   uint32_t desc_stride = panvk_get_desc_stride(binding_layout);
   uint32_t subdesc_offset = sampler_subdesc ? binding_layout->textures_per_desc : 0;
   uint32_t *first_entry = NULL;

   assert(desc_count <= binding_layout->desc_count);

   for (uint32_t i = 0; i < desc_count; i++) {
      uint32_t src_idx =
         binding_layout->desc_idx + (i * desc_stride) + subdesc_offset;
      struct panvk_shader_desc_map *map;

#if PAN_ARCH < 9
      if (binding_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
         map = &ctx->desc_info.dyn_ubos;
      } else if (binding_layout->type ==
                 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
         map = &ctx->desc_info.dyn_ssbos;
      } else {
         uint32_t dst_table =
            desc_type_to_table_type(binding_layout, sampler_subdesc);

         assert(dst_table < PANVK_BIFROST_DESC_TABLE_COUNT);
         map = &ctx->desc_info.others[dst_table];
      }
#else
      map = &ctx->desc_info.dyn_bufs;
#endif

      if (!first_entry)
         first_entry = &map->map[map->count];

      uint32_t desc_array_stride = get_desc_array_stride(
         binding_layout, sampler_subdesc ? VK_DESCRIPTOR_TYPE_SAMPLER
                                         : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

      for (uint32_t j = 0; j < desc_array_stride; j++)
         map->map[map->count++] = COPY_DESC_HANDLE(set, src_idx + j);
   }

   return first_entry;
}

static void
create_copy_table(nir_shader *nir, struct lower_desc_ctx *ctx)
{
   struct panvk_shader_desc_info *desc_info = &ctx->desc_info;
   uint32_t copy_count;

#if PAN_ARCH < 9
   copy_count = desc_info->dyn_ubos.count + desc_info->dyn_ssbos.count;
   for (uint32_t i = 0; i < PANVK_BIFROST_DESC_TABLE_COUNT; i++)
      copy_count += desc_info->others[i].count;
#else
   uint32_t dummy_sampler_idx;
   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX:
      /* Dummy sampler comes after the vertex attributes. */
      dummy_sampler_idx = 16;
      break;
   case MESA_SHADER_FRAGMENT:
      /* Dummy sampler comes after the varyings. */
      dummy_sampler_idx = desc_info->num_varying_attr_descs;
      break;
   case MESA_SHADER_COMPUTE:
      dummy_sampler_idx = 0;
      break;
   default:
      UNREACHABLE("unexpected stage");
   }
   desc_info->dummy_sampler_handle = pan_res_handle(0, dummy_sampler_idx);

   copy_count = desc_info->dyn_bufs.count;
#endif

   if (copy_count == 0)
      return;

#if PAN_ARCH < 9
   uint32_t *copy_table = rzalloc_array(ctx->ht, uint32_t, copy_count);

   assert(copy_table);
   desc_info->dyn_ubos.map = copy_table;
   copy_table += desc_info->dyn_ubos.count;
   desc_info->dyn_ubos.count = 0;
   desc_info->dyn_ssbos.map = copy_table;
   copy_table += desc_info->dyn_ssbos.count;
   desc_info->dyn_ssbos.count = 0;

   for (uint32_t i = 0; i < PANVK_BIFROST_DESC_TABLE_COUNT; i++) {
      desc_info->others[i].map = copy_table;
      copy_table += desc_info->others[i].count;
      desc_info->others[i].count = 0;
   }
#else
   /* Dynamic buffers come after the dummy sampler. */
   desc_info->dyn_bufs_start = dummy_sampler_idx + 1;

   desc_info->dyn_bufs.map = rzalloc_array(ctx->ht, uint32_t, copy_count);
   desc_info->dyn_bufs.count = 0;
   assert(desc_info->dyn_bufs.map);
#endif

   hash_table_u64_foreach(ctx->ht, he) {
      /* We use the upper binding bit to encode the subdesc index. */
      uint32_t desc_count = (uintptr_t)he.data;
      struct desc_id src = {
         .ht_key = he.key,
      };

      /* Until now, we were just using the hash table to track descriptors
       * count, but after that point, it's a <set,binding> -> <table_index>
       * map. */
      void *new_data = fill_copy_descs_for_binding(
         ctx, src.set, src.binding, src.sampler_subdesc, desc_count);
      _mesa_hash_table_u64_replace(ctx->ht, &he, new_data);
   }
}

/* TODO: Texture instructions support bindless through DTSEL_IMM(63),
 * which would save us copies of the texture/sampler descriptors. */
static bool
collect_tex_desc_access(nir_builder *b, nir_tex_instr *tex,
                        struct lower_desc_ctx *ctx)
{
   bool recorded = false;
   uint32_t plane = 0;
   int sampler_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (sampler_src_idx >= 0) {
      int plane_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_plane);
      if (plane_src_idx >= 0)
         plane = nir_src_as_uint(tex->src[plane_src_idx].src);

      nir_deref_instr *deref = nir_src_as_deref(tex->src[sampler_src_idx].src);

      uint32_t set, binding, index_imm, max_idx;
      nir_def *index_ssa;
      get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa,
                                 &max_idx);
      const struct panvk_descriptor_set_layout *set_layout =
         ctx->set_layouts[set];
      const struct panvk_descriptor_set_binding_layout *binding_layout =
         &set_layout->bindings[binding];
      struct panvk_subdesc_info subdesc =
         get_sampler_subdesc_info(binding_layout->type, plane);

      record_binding(ctx, set, binding, subdesc, max_idx);
      recorded = true;
   }

   int tex_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (tex_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[tex_src_idx].src);

      uint32_t set, binding, index_imm, max_idx;
      nir_def *index_ssa;
      get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa,
                                 &max_idx);
      const struct panvk_descriptor_set_layout *set_layout =
         ctx->set_layouts[set];
      const struct panvk_descriptor_set_binding_layout *binding_layout =
         &set_layout->bindings[binding];
      struct panvk_subdesc_info subdesc =
         get_tex_subdesc_info(binding_layout->type, plane);

      record_binding(ctx, set, binding, subdesc, max_idx);
      recorded = true;
   }

   return recorded;
}

static bool
collect_intr_desc_access(nir_builder *b, nir_intrinsic_instr *intrin,
                         struct lower_desc_ctx *ctx)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_vulkan_resource_index: {
      unsigned set, binding;

      set = nir_intrinsic_desc_set(intrin);
      binding = nir_intrinsic_binding(intrin);

      /* TODO: walk the reindex chain from load_vulkan_descriptor() to try and
       * guess the max index. */
      record_binding(ctx, set, binding, NO_SUBDESC, UINT32_MAX);
      return true;
   }

   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples: {
      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
      unsigned set, binding, index_imm, max_idx;
      nir_def *index_ssa;

      get_resource_deref_binding(deref, &set, &binding, &index_imm, &index_ssa,
                                 &max_idx);
      record_binding(ctx, set, binding, NO_SUBDESC, max_idx);
      return true;
   }
   default:
      return false;
   }
}

static bool
collect_instr_desc_access(nir_builder *b, nir_instr *instr, void *data)
{
   struct lower_desc_ctx *ctx = data;

   switch (instr->type) {
   case nir_instr_type_tex:
      return collect_tex_desc_access(b, nir_instr_as_tex(instr), ctx);
   case nir_instr_type_intrinsic:
      return collect_intr_desc_access(b, nir_instr_as_intrinsic(instr), ctx);
   default:
      return false;
   }
}

static void
upload_shader_desc_info(struct panvk_device *dev, struct panvk_shader_variant *shader,
                        const struct panvk_shader_desc_info *desc_info)
{
#if PAN_ARCH < 9
   unsigned copy_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      shader->desc_info.others.count[i] = desc_info->others[i].count;
      copy_count += desc_info->others[i].count;
   }

   if (copy_count > 0) {
      shader->desc_info.others.map = panvk_pool_upload_aligned(
         &dev->mempools.rw, desc_info->others[0].map,
         copy_count * sizeof(uint32_t), sizeof(uint32_t));
   }

   assert(desc_info->dyn_ubos.count <=
          ARRAY_SIZE(shader->desc_info.dyn_ubos.map));
   shader->desc_info.dyn_ubos.count = desc_info->dyn_ubos.count;
   memcpy(shader->desc_info.dyn_ubos.map, desc_info->dyn_ubos.map,
          desc_info->dyn_ubos.count * sizeof(*shader->desc_info.dyn_ubos.map));
   assert(desc_info->dyn_ssbos.count <=
          ARRAY_SIZE(shader->desc_info.dyn_ssbos.map));
   shader->desc_info.dyn_ssbos.count = desc_info->dyn_ssbos.count;
   memcpy(
      shader->desc_info.dyn_ssbos.map, desc_info->dyn_ssbos.map,
      desc_info->dyn_ssbos.count * sizeof(*shader->desc_info.dyn_ssbos.map));
#else
   assert(desc_info->dyn_bufs.count <=
          ARRAY_SIZE(shader->desc_info.dyn_bufs.map));
   shader->desc_info.dyn_bufs.count = desc_info->dyn_bufs.count;
   memcpy(shader->desc_info.dyn_bufs.map, desc_info->dyn_bufs.map,
          desc_info->dyn_bufs.count * sizeof(*shader->desc_info.dyn_bufs.map));
#endif

   shader->desc_info.used_set_mask = desc_info->used_set_mask;
}

void
panvk_per_arch(nir_lower_descriptors)(
   nir_shader *nir, struct panvk_device *dev,
   const struct vk_pipeline_robustness_state *rs, uint32_t set_layout_count,
   struct vk_descriptor_set_layout *const *set_layouts,
   const struct vk_graphics_pipeline_state *state, struct panvk_shader_variant *shader)
{
   struct lower_desc_ctx ctx = {
      .shader = shader,
      .add_bounds_checks =
         rs->storage_buffers !=
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||
         rs->uniform_buffers !=
            VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||
         rs->images != VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT,
      .null_descriptor_support = dev->vk.enabled_features.nullDescriptor,
   };
   bool progress = false;

#if PAN_ARCH < 9
   ctx.ubo_addr_format = nir_address_format_32bit_index_offset;
   ctx.ssbo_addr_format =
      rs->storage_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT
         ? nir_address_format_64bit_bounded_global
         : nir_address_format_64bit_global_32bit_offset;
#else
   ctx.ubo_addr_format = nir_address_format_vec2_index_32bit_offset;
   ctx.ssbo_addr_format = nir_address_format_vec2_index_32bit_offset;
#endif

   ctx.ht = _mesa_hash_table_u64_create(NULL);
   assert(ctx.ht);

   for (uint32_t i = 0; i < set_layout_count; i++)
      ctx.set_layouts[i] = to_panvk_descriptor_set_layout(set_layouts[i]);

   NIR_PASS(progress, nir, nir_shader_instructions_pass,
            collect_instr_desc_access, nir_metadata_all, &ctx);
   if (!progress)
      goto out;

#if PAN_ARCH >= 9
   ctx.desc_info.num_varying_attr_descs = 0;
   /* We require Attribute Descriptors if we cannot use LD_VAR_BUF[_IMM] for
    * varyings. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT &&
       !panvk_use_ld_var_buf(shader))
      ctx.desc_info.num_varying_attr_descs =
         shader->desc_info.max_varying_loads;
#endif
   create_copy_table(nir, &ctx);
   upload_shader_desc_info(dev, shader, &ctx.desc_info);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(progress, nir, lower_input_attachment_loads, state, shader);

   NIR_PASS(progress, nir, nir_shader_instructions_pass,
            lower_descriptors_instr, nir_metadata_control_flow, &ctx);

out:
   _mesa_hash_table_u64_destroy(ctx.ht);
}
