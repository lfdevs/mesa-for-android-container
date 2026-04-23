/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "vk_nir_lower_descriptor_heaps.h"

#include "vk_sampler.h"

#include "nir_builder.h"
#include "util/u_dynarray.h"
#include "util/hash_table.h"

static void
hash_embedded_sampler(struct mesa_blake3 *ctx,
                      const struct VkSamplerCreateInfo *info)
{
   if (info != NULL) {
      struct vk_sampler_state state;
      vk_sampler_state_init(&state, info);
      _mesa_blake3_update(ctx, &state, sizeof(state));
   }
}

void
vk_hash_descriptor_heap_mappings(
   const VkShaderDescriptorSetAndBindingMappingInfoEXT *info,
   blake3_hash blake3_out)
{
   struct mesa_blake3 ctx;
   _mesa_blake3_init(&ctx);

#define HASH(ctx, x) _mesa_blake3_update(ctx, &(x), sizeof(x))

   for (uint32_t i = 0; i < info->mappingCount; i++) {
      const VkDescriptorSetAndBindingMappingEXT *mapping = &info->pMappings[i];
      HASH(&ctx, mapping->descriptorSet);
      HASH(&ctx, mapping->firstBinding);
      HASH(&ctx, mapping->bindingCount);
      HASH(&ctx, mapping->resourceMask);
      HASH(&ctx, mapping->source);
      switch (mapping->source) {
      case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT: {
         const VkDescriptorMappingSourceConstantOffsetEXT *data =
            &mapping->sourceData.constantOffset;
         HASH(&ctx, data->heapOffset);
         HASH(&ctx, data->heapArrayStride);
         hash_embedded_sampler(&ctx, data->pEmbeddedSampler);
         HASH(&ctx, data->samplerHeapOffset);
         HASH(&ctx, data->samplerHeapArrayStride);
         break;
      }

      case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT: {
         const VkDescriptorMappingSourcePushIndexEXT *data =
            &mapping->sourceData.pushIndex;
         HASH(&ctx, data->heapOffset);
         HASH(&ctx, data->pushOffset);
         HASH(&ctx, data->heapIndexStride);
         HASH(&ctx, data->heapArrayStride);
         hash_embedded_sampler(&ctx, data->pEmbeddedSampler);
         HASH(&ctx, data->useCombinedImageSamplerIndex);
         HASH(&ctx, data->samplerHeapOffset);
         HASH(&ctx, data->samplerPushOffset);
         HASH(&ctx, data->samplerHeapIndexStride);
         HASH(&ctx, data->samplerHeapArrayStride);
         break;
      }

      case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_EXT: {
         const VkDescriptorMappingSourceIndirectIndexEXT *data =
            &mapping->sourceData.indirectIndex;
         HASH(&ctx, data->heapOffset);
         HASH(&ctx, data->pushOffset);
         HASH(&ctx, data->addressOffset);
         HASH(&ctx, data->heapIndexStride);
         HASH(&ctx, data->heapArrayStride);
         hash_embedded_sampler(&ctx, data->pEmbeddedSampler);
         HASH(&ctx, data->useCombinedImageSamplerIndex);
         HASH(&ctx, data->samplerHeapOffset);
         HASH(&ctx, data->samplerPushOffset);
         HASH(&ctx, data->samplerAddressOffset);
         HASH(&ctx, data->samplerHeapIndexStride);
         HASH(&ctx, data->samplerHeapArrayStride);
         break;
      }

      case VK_DESCRIPTOR_MAPPING_SOURCE_RESOURCE_HEAP_DATA_EXT: {
         const VkDescriptorMappingSourceHeapDataEXT *data =
            &mapping->sourceData.heapData;
         HASH(&ctx, data->heapOffset);
         HASH(&ctx, data->pushOffset);
         break;
      }

      case VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_DATA_EXT:
         HASH(&ctx, mapping->sourceData.pushDataOffset);
         break;

      case VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT:
         HASH(&ctx, mapping->sourceData.pushAddressOffset);
         break;

      case VK_DESCRIPTOR_MAPPING_SOURCE_INDIRECT_ADDRESS_EXT: {
         const VkDescriptorMappingSourceIndirectAddressEXT *data =
            &mapping->sourceData.indirectAddress;
         HASH(&ctx, data->pushOffset);
         HASH(&ctx, data->addressOffset);
         break;
      }

      case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_ARRAY_EXT: {
         const VkDescriptorMappingSourceIndirectIndexArrayEXT *data =
            &mapping->sourceData.indirectIndexArray;
         HASH(&ctx, data->heapOffset);
         HASH(&ctx, data->pushOffset);
         HASH(&ctx, data->addressOffset);
         HASH(&ctx, data->heapIndexStride);
         hash_embedded_sampler(&ctx, data->pEmbeddedSampler);
         HASH(&ctx, data->useCombinedImageSamplerIndex);
         HASH(&ctx, data->samplerHeapOffset);
         HASH(&ctx, data->samplerPushOffset);
         HASH(&ctx, data->samplerAddressOffset);
         HASH(&ctx, data->samplerHeapIndexStride);
         break;
      }

      case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_SHADER_RECORD_INDEX_EXT: {
         const VkDescriptorMappingSourceShaderRecordIndexEXT *data =
            &mapping->sourceData.shaderRecordIndex;
         HASH(&ctx, data->heapOffset);
         HASH(&ctx, data->shaderRecordOffset);
         HASH(&ctx, data->heapIndexStride);
         HASH(&ctx, data->heapArrayStride);
         hash_embedded_sampler(&ctx, data->pEmbeddedSampler);
         HASH(&ctx, data->useCombinedImageSamplerIndex);
         HASH(&ctx, data->samplerHeapOffset);
         HASH(&ctx, data->samplerShaderRecordOffset);
         HASH(&ctx, data->samplerHeapIndexStride);
         HASH(&ctx, data->samplerHeapArrayStride);
         break;
      }

      case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_DATA_EXT:
         HASH(&ctx, mapping->sourceData.shaderRecordDataOffset);
         break;

      case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_ADDRESS_EXT:
         HASH(&ctx, mapping->sourceData.shaderRecordAddressOffset);
         break;

      default:
         UNREACHABLE("Unsupported descriptor mapping source");
      }
   }

   _mesa_blake3_final(&ctx, blake3_out);
}

#undef HASH

struct heap_mapping_ctx {
   const VkShaderDescriptorSetAndBindingMappingInfoEXT *info;

   const vk_nir_lower_descriptor_heaps_options *options;

   /* Map from vk_sampler_state to indices */
   struct hash_table *sampler_idx_map;
};

static uint32_t
hash_sampler(const void *_s)
{
   const struct vk_sampler_state *s = _s;
   return _mesa_hash_data(s, sizeof(*s));
}

static bool
samplers_equal(const void *_a, const void *_b)
{
   const struct vk_sampler_state *a = _a, *b = _b;
   return !memcmp(a, b, sizeof(*a));
}

static uint32_t
add_embedded_sampler(struct heap_mapping_ctx *ctx,
                     const VkSamplerCreateInfo *info)
{
   struct vk_sampler_state key;
   vk_sampler_state_init(&key, info);

   struct hash_entry *entry =
      _mesa_hash_table_search(ctx->sampler_idx_map, &key);
   if (entry != NULL)
      return (uintptr_t)entry->data;

   uint32_t index = ctx->sampler_idx_map->entries;

   struct vk_sampler_state *state =
      ralloc(ctx->sampler_idx_map, struct vk_sampler_state);
   *state = key;

   _mesa_hash_table_insert(ctx->sampler_idx_map, state,
                           (void *)(uintptr_t)index);

   return index;
}

static nir_def *
load_push(nir_builder *b, unsigned bit_size, unsigned offset)
{
   assert(bit_size % 8 == 0);
   assert(offset % (bit_size / 8) == 0);
   return nir_load_push_constant(b, 1, bit_size, nir_imm_int(b, offset),
                                 .range = offset + (bit_size / 8));
}

static nir_def *
load_indirect(nir_builder *b, unsigned bit_size, nir_def *addr, unsigned offset)
{
   assert(bit_size % 8 == 0);
   assert(offset % (bit_size / 8) == 0);
   addr = nir_iadd_imm(b, addr, offset);
   return nir_load_global_constant(b, 1, bit_size, addr);
}

static nir_def *
load_shader_record(nir_builder *b, unsigned bit_size, unsigned offset)
{
   assert(bit_size % 8 == 0);
   assert(offset % (bit_size / 8) == 0);
   nir_def *addr = nir_iadd_imm(b, nir_load_shader_record_ptr(b), offset);
   return nir_load_global_constant(b, 1, bit_size, addr);
}

static nir_def *
unpack_combined_image_sampler(nir_builder *b, nir_def *combined,
                              bool is_sampler)
{
   assert(combined->bit_size == 32);
   if (is_sampler)
      return nir_ubitfield_extract_imm(b, combined, 20, 12);
   else
      return nir_ubitfield_extract_imm(b, combined, 0, 20);
}

static bool
is_mapping_implicitly_non_uniform(struct heap_mapping_ctx *ctx,
                                  const VkDescriptorSetAndBindingMappingEXT *mapping,
                                  nir_def *index)
{
   /* Non-arrayed resources backed by HEAP_WITH_SHADER_RECORD_INDEX can be
    * implicitly non-uniform: different lanes in a subgroup may have different
    * shader record indices (and thus different heap entries) with no
    * descriptor indexing in the shader to annotate it.
    */
   return mapping->source == VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_SHADER_RECORD_INDEX_EXT &&
          index == NULL && ctx->options && ctx->options->lower_shader_record_index_to_non_uniform;
}

static nir_def *
vk_build_descriptor_heap_offset(nir_builder *b,
                                struct heap_mapping_ctx *ctx,
                                const VkDescriptorSetAndBindingMappingEXT *mapping,
                                nir_resource_type resource_type,
                                uint32_t binding, nir_def *index,
                                bool is_sampler, bool *non_uniform_out)
{
   assert(util_is_power_of_two_nonzero(resource_type));

   if (non_uniform_out != NULL)
      *non_uniform_out = is_mapping_implicitly_non_uniform(ctx, mapping, index);

   if (index == NULL)
      index = nir_imm_int(b, 0);

   assert(binding >= mapping->firstBinding);
   const uint32_t rel_binding = binding - mapping->firstBinding;
   assert(rel_binding < mapping->bindingCount);
   nir_def *shader_index = nir_iadd_imm(b, index, rel_binding);

   const bool is_sampled_image = resource_type == nir_resource_type_combined_sampled_image;

   switch (mapping->source) {
   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT: {
      const VkDescriptorMappingSourceConstantOffsetEXT *data =
         &mapping->sourceData.constantOffset;

      uint32_t heap_offset;
      uint32_t array_stride;
      if (is_sampled_image && is_sampler) {
         array_stride = data->samplerHeapArrayStride;
         heap_offset = data->samplerHeapOffset;
      } else {
         array_stride = data->heapArrayStride;
         heap_offset = data->heapOffset;
      }

      return nir_iadd_imm(b, nir_imul_imm(b, shader_index, array_stride),
                             heap_offset);
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT: {
      const VkDescriptorMappingSourcePushIndexEXT *data =
         &mapping->sourceData.pushIndex;

      nir_def *push_index;
      if (is_sampled_image && is_sampler &&
          !data->useCombinedImageSamplerIndex) {
         push_index = load_push(b, 32, data->samplerPushOffset);
      } else {
         push_index = load_push(b, 32, data->pushOffset);
      }

      if (data->useCombinedImageSamplerIndex && is_sampled_image)
         push_index = unpack_combined_image_sampler(b, push_index, is_sampler);

      nir_def *offset;
      uint32_t array_stride;
      if (is_sampled_image && is_sampler) {
         array_stride = data->samplerHeapArrayStride;
         nir_def *push_offset =
            nir_imul_imm(b, push_index, data->samplerHeapIndexStride);
         offset = nir_iadd_imm(b, push_offset, data->samplerHeapOffset);
      } else {
         array_stride = data->heapArrayStride;
         nir_def *push_offset =
            nir_imul_imm(b, push_index, data->heapIndexStride);
         offset = nir_iadd_imm(b, push_offset, data->heapOffset);
      }

      return nir_iadd(b, offset, nir_imul_imm(b, shader_index, array_stride));
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_EXT: {
      const VkDescriptorMappingSourceIndirectIndexEXT *data =
         &mapping->sourceData.indirectIndex;

      nir_def *indirect_index;
      if (is_sampled_image && is_sampler &&
          !data->useCombinedImageSamplerIndex) {
         nir_def *indirect_addr = load_push(b, 64, data->samplerPushOffset);
         indirect_index = load_indirect(b, 32, indirect_addr,
                                        data->samplerAddressOffset);
      } else {
         nir_def *indirect_addr = load_push(b, 64, data->pushOffset);
         indirect_index = load_indirect(b, 32, indirect_addr,
                                        data->addressOffset);
      }

      if (data->useCombinedImageSamplerIndex && is_sampled_image)
         indirect_index = unpack_combined_image_sampler(b, indirect_index,
                                                        is_sampler);

      nir_def *offset;
      uint32_t array_stride;
      if (is_sampled_image && is_sampler) {
         array_stride = data->samplerHeapArrayStride;
         nir_def *indirect_offset =
            nir_imul_imm(b, indirect_index, data->samplerHeapIndexStride);
         offset = nir_iadd_imm(b, indirect_offset, data->samplerHeapOffset);
      } else {
         array_stride = data->heapArrayStride;
         nir_def *indirect_offset =
            nir_imul_imm(b, indirect_index, data->heapIndexStride);
         offset = nir_iadd_imm(b, indirect_offset, data->heapOffset);
      }

      return nir_iadd(b, offset, nir_imul_imm(b, shader_index, array_stride));
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_RESOURCE_HEAP_DATA_EXT: {
      const VkDescriptorMappingSourceHeapDataEXT *data =
         &mapping->sourceData.heapData;
      return nir_iadd_imm(b, load_push(b, 32, data->pushOffset),
                          data->heapOffset);
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_INDIRECT_INDEX_ARRAY_EXT: {
      const VkDescriptorMappingSourceIndirectIndexArrayEXT *data =
         &mapping->sourceData.indirectIndexArray;

      nir_def *indirect_addr;
      uint32_t addr_offset;
      if (is_sampled_image && is_sampler &&
          !data->useCombinedImageSamplerIndex) {
         indirect_addr = load_push(b, 64, data->samplerPushOffset);
         addr_offset = data->samplerAddressOffset;
      } else {
         indirect_addr = load_push(b, 64, data->pushOffset);
         addr_offset = data->addressOffset;
      }

      /* The shader index goes into the indirect. */
      indirect_addr = nir_iadd(b, indirect_addr,
                               nir_u2u64(b, nir_imul_imm(b, shader_index, 4)));
      nir_def *indirect_index = load_indirect(b, 32, indirect_addr,
                                             addr_offset);

      if (data->useCombinedImageSamplerIndex && is_sampled_image)
         indirect_index = unpack_combined_image_sampler(b, indirect_index,
                                                        is_sampler);

      if (is_sampled_image && is_sampler) {
         nir_def *indirect_offset =
            nir_imul_imm(b, indirect_index, data->samplerHeapIndexStride);
         return nir_iadd_imm(b, indirect_offset, data->samplerHeapOffset);
      } else {
         nir_def *indirect_offset =
            nir_imul_imm(b, indirect_index, data->heapIndexStride);
         return nir_iadd_imm(b, indirect_offset, data->heapOffset);
      }
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_SHADER_RECORD_INDEX_EXT: {
      const VkDescriptorMappingSourceShaderRecordIndexEXT *data =
         &mapping->sourceData.shaderRecordIndex;

      nir_def *record_index;
      if (is_sampled_image && is_sampler &&
          !data->useCombinedImageSamplerIndex) {
         record_index = load_shader_record(b, 32, data->samplerShaderRecordOffset);
      } else {
         record_index = load_shader_record(b, 32, data->shaderRecordOffset);
      }

      if (data->useCombinedImageSamplerIndex && is_sampled_image)
         record_index = unpack_combined_image_sampler(b, record_index,
                                                      is_sampler);

      nir_def *offset;
      uint32_t array_stride;
      if (is_sampled_image && is_sampler) {
         array_stride = data->samplerHeapArrayStride;
         nir_def *record_offset =
            nir_imul_imm(b, record_index, data->samplerHeapIndexStride);
         offset = nir_iadd_imm(b, record_offset, data->samplerHeapOffset);
      } else {
         array_stride = data->heapArrayStride;
         nir_def *record_offset =
            nir_imul_imm(b, record_index, data->heapIndexStride);
         offset = nir_iadd_imm(b, record_offset, data->heapOffset);
      }

      return nir_iadd(b, offset, nir_imul_imm(b, shader_index, array_stride));
   }

   default:
      return NULL;
   }
}

static nir_def *
vk_build_descriptor_heap_address(nir_builder *b,
                                 const VkDescriptorSetAndBindingMappingEXT *mapping)
{
   switch (mapping->source) {
   case VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT:
      return load_push(b, 64, mapping->sourceData.pushAddressOffset);

   case VK_DESCRIPTOR_MAPPING_SOURCE_INDIRECT_ADDRESS_EXT: {
      const VkDescriptorMappingSourceIndirectAddressEXT *data =
         &mapping->sourceData.indirectAddress;

      nir_def *addr = load_push(b, 64, data->pushOffset);
      return load_indirect(b, 64, addr, data->addressOffset);
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_DATA_EXT:
      return nir_iadd_imm(b, nir_load_shader_record_ptr(b),
                          mapping->sourceData.shaderRecordDataOffset);

   case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_ADDRESS_EXT:
      return load_shader_record(b, 64,
         mapping->sourceData.shaderRecordAddressOffset);

   default:
      return NULL;
   }
}

static bool
var_is_heap_ptr(nir_variable *var)
{
   return var->data.mode == nir_var_resource_heap ||
          var->data.mode == nir_var_sampler_heap;
}

static nir_deref_instr *
deref_get_root_cast(nir_deref_instr *deref)
{
   while (true) {
      if (deref->deref_type == nir_deref_type_var)
         return NULL;

      nir_deref_instr *parent = nir_src_as_deref(deref->parent);
      if (!parent)
         break;

      if (parent->deref_type == nir_deref_type_var && var_is_heap_ptr(parent->var))
         break;

      deref = parent;
   }
   assert(deref->deref_type == nir_deref_type_cast);

   return deref;
}

static bool
get_deref_resource_binding(nir_deref_instr *deref,
                           uint32_t *set, uint32_t *binding,
                           nir_resource_type *resource_type,
                           nir_def **index_out)
{
   nir_def *index = NULL;
   if (deref->deref_type == nir_deref_type_array) {
      index = deref->arr.index.ssa;
      deref = nir_deref_instr_parent(deref);
   }

   if (deref->deref_type != nir_deref_type_var)
      return false;

   nir_variable *var = deref->var;

   if (var->data.mode != nir_var_uniform && var->data.mode != nir_var_image)
      return false;

   /* This should only happen for internal meta shaders */
   if (var->data.resource_type == 0)
      return false;

   *set = var->data.descriptor_set;
   *binding = var->data.binding;
   *resource_type = var->data.resource_type;
   if (index_out != NULL)
      *index_out = index;

   return true;
}

static bool
get_buffer_resource_binding(nir_intrinsic_instr *desc_load,
                            uint32_t *set, uint32_t *binding,
                            nir_resource_type *resource_type)
{
   assert(desc_load->intrinsic == nir_intrinsic_load_vulkan_descriptor);
   nir_intrinsic_instr *idx_intrin = nir_src_as_intrinsic(desc_load->src[0]);

   while (idx_intrin->intrinsic == nir_intrinsic_vulkan_resource_reindex)
      idx_intrin = nir_src_as_intrinsic(idx_intrin->src[0]);

   if (idx_intrin->intrinsic != nir_intrinsic_vulkan_resource_index)
      return false;

   *set = nir_intrinsic_desc_set(idx_intrin);
   *binding = nir_intrinsic_binding(idx_intrin);
   *resource_type = nir_intrinsic_resource_type(idx_intrin);

   return true;
}

static inline bool
buffer_resource_has_zero_index(nir_intrinsic_instr *desc_load)
{
   assert(desc_load->intrinsic == nir_intrinsic_load_vulkan_descriptor);
   nir_intrinsic_instr *idx_intrin = nir_src_as_intrinsic(desc_load->src[0]);

   if (idx_intrin->intrinsic == nir_intrinsic_vulkan_resource_reindex)
      return false;

   assert(idx_intrin->intrinsic == nir_intrinsic_vulkan_resource_index);
   if (!nir_src_is_const(idx_intrin->src[0]))
      return false;

   return nir_src_as_uint(idx_intrin->src[0]) == 0;
}

/* This assumes get_buffer_resource_binding() already succeeded */
static nir_def *
build_buffer_resource_index(nir_builder *b, nir_intrinsic_instr *desc_load)
{
   assert(desc_load->intrinsic == nir_intrinsic_load_vulkan_descriptor);
   nir_intrinsic_instr *idx_intrin = nir_src_as_intrinsic(desc_load->src[0]);

   nir_def *index = nir_imm_int(b, 0);
   while (idx_intrin->intrinsic == nir_intrinsic_vulkan_resource_reindex) {
      index = nir_iadd(b, index, idx_intrin->src[1].ssa);
      idx_intrin = nir_src_as_intrinsic(idx_intrin->src[0]);
   }

   assert(idx_intrin->intrinsic == nir_intrinsic_vulkan_resource_index);
   return nir_iadd(b, index, idx_intrin->src[0].ssa);
}

/** Builds a buffer address for deref chain
 *
 * This assumes that you can chase the chain all the way back to the original
 * vulkan_resource_index intrinsic.
 *
 * The cursor is not where you left it when this function returns.
 */
static nir_def *
build_buffer_addr_for_deref(nir_builder *b, nir_def *root_addr,
                            nir_deref_instr *deref,
                            nir_address_format addr_format)
{
   nir_deref_instr *parent = nir_deref_instr_parent(deref);
   if (parent) {
      nir_def *addr =
         build_buffer_addr_for_deref(b, root_addr, parent, addr_format);

      b->cursor = nir_before_instr(&deref->instr);
      return nir_explicit_io_address_from_deref(b, deref, addr, addr_format);
   }

   return root_addr;
}

/* The cursor is not where you left it when this function returns. */
static nir_def *
build_deref_heap_offset(nir_builder *b, nir_deref_instr *deref,
                        bool is_sampler, struct heap_mapping_ctx *ctx,
                        bool *non_uniform_out)
{
   uint32_t set, binding;
   nir_resource_type resource_type;
   nir_def *index;
   if (get_deref_resource_binding(deref, &set, &binding,
                                  &resource_type, &index)) {
      if (ctx->info == NULL)
         return NULL;

      const VkDescriptorSetAndBindingMappingEXT *mapping =
         vk_descriptor_heap_mapping(ctx->info, set, binding, resource_type);
      assert(mapping != NULL);
      if (mapping == NULL)
         return NULL;

      b->cursor = nir_before_instr(&deref->instr);

      return vk_build_descriptor_heap_offset(b, ctx, mapping, resource_type,
                                             binding, index, is_sampler,
                                             non_uniform_out);
   } else {
      nir_deref_instr *root_cast = deref_get_root_cast(deref);
      if (root_cast == NULL)
         return NULL;

      /* We're building an offset.  It starts at zero */
      b->cursor = nir_before_instr(&root_cast->instr);
      nir_def *base_addr = nir_imm_int(b, 0);

      return build_buffer_addr_for_deref(b, base_addr, deref,
                                         nir_address_format_32bit_offset);
   }
}

static const VkSamplerCreateInfo *
get_deref_embedded_sampler(nir_deref_instr *sampler,
                           struct heap_mapping_ctx *ctx)
{
   if (ctx->info == NULL)
      return NULL;

   uint32_t set, binding;
   nir_resource_type resource_type;
   if (!get_deref_resource_binding(sampler, &set, &binding,
                                   &resource_type, NULL))
      return NULL;

   const VkDescriptorSetAndBindingMappingEXT *mapping =
      vk_descriptor_heap_mapping(ctx->info, set, binding, resource_type);

   return vk_descriptor_heap_embedded_sampler(mapping);
}

static bool
lower_heaps_tex(nir_builder *b, nir_tex_instr *tex,
                struct heap_mapping_ctx *ctx)
{
   const int texture_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   const int sampler_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   bool progress = false;

   nir_deref_instr *texture = nir_src_as_deref(tex->src[texture_src_idx].src);
   assert(texture != NULL);

   {
      bool texture_non_uniform = false;
      nir_def *heap_offset = build_deref_heap_offset(b, texture, false, ctx,
                                                     &texture_non_uniform);
      if (heap_offset != NULL) {
         nir_src_rewrite(&tex->src[texture_src_idx].src, heap_offset);
         tex->src[texture_src_idx].src_type = nir_tex_src_texture_heap_offset;
         tex->texture_non_uniform |= texture_non_uniform;
         progress = true;
      }
   }

   if (nir_tex_instr_need_sampler(tex)) {
      /* If this is a combined image/sampler, we may only have an image deref
       * source and it's also the sampler deref.
       */
      nir_deref_instr *sampler = sampler_src_idx < 0 ? texture :
                                 nir_src_as_deref(tex->src[sampler_src_idx].src);

      const VkSamplerCreateInfo *embedded_sampler =
         get_deref_embedded_sampler(sampler, ctx);
      if (embedded_sampler == NULL) {
         bool sampler_non_uniform = false;
         nir_def *heap_offset = build_deref_heap_offset(b, sampler, true, ctx,
                                                        &sampler_non_uniform);
         if (heap_offset != NULL) {
            nir_src_rewrite(&tex->src[sampler_src_idx].src, heap_offset);
            tex->src[sampler_src_idx].src_type = nir_tex_src_sampler_heap_offset;
            tex->sampler_non_uniform |= sampler_non_uniform;
            progress = true;
         }
      } else {
         nir_tex_instr_remove_src(tex, sampler_src_idx);
         tex->embedded_sampler = true;
         tex->sampler_index = add_embedded_sampler(ctx, embedded_sampler);
         b->shader->info.uses_embedded_samplers = true;
         progress = true;
      }
   }

   /* Remove unused sampler sources so we don't accidentally reference things
    * that don't actually exist.  The driver can add it back in if it really
    * needs it.
    */
   if (progress && sampler_src_idx >= 0 && !nir_tex_instr_need_sampler(tex))
      nir_tex_instr_remove_src(tex, sampler_src_idx);

   return progress;
}

static bool
lower_heaps_image(nir_builder *b, nir_intrinsic_instr *intrin,
                  struct heap_mapping_ctx *ctx, bool deref)
{
   nir_deref_instr *image = nir_src_as_deref(intrin->src[0]);
   bool is_non_uniform = false;
   nir_def *heap_offset = build_deref_heap_offset(b, image, false, ctx,
                                                  &is_non_uniform);
   if (heap_offset == NULL)
      return false;

   if (deref) {
      nir_rewrite_image_intrinsic(intrin, heap_offset, nir_image_intrinsic_type_heap);
   } else {
      nir_src_rewrite(&intrin->src[0], heap_offset);
   }

   if (is_non_uniform) {
      nir_intrinsic_set_access(intrin,
                               nir_intrinsic_access(intrin) | ACCESS_NON_UNIFORM);
   }

   return true;
}

static bool
try_lower_heaps_deref_access(nir_builder *b, nir_intrinsic_instr *intrin,
                             struct heap_mapping_ctx *ctx)
{
   if (ctx->info == NULL)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_deref_instr *root_cast = deref_get_root_cast(deref);
   if (root_cast == NULL)
      return false;

   nir_intrinsic_instr *desc_load = nir_src_as_intrinsic(root_cast->parent);
   if (desc_load == NULL ||
       desc_load->intrinsic != nir_intrinsic_load_vulkan_descriptor)
      return false;

   uint32_t set, binding;
   nir_resource_type resource_type;
   if (!get_buffer_resource_binding(desc_load, &set, &binding, &resource_type))
      return false;

   const VkDescriptorSetAndBindingMappingEXT *mapping =
      vk_descriptor_heap_mapping(ctx->info, set, binding, resource_type);
   if (mapping == NULL)
      return false;

   switch (mapping->source) {
   case VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_DATA_EXT: {
      assert(nir_deref_mode_is(deref, nir_var_mem_ubo));
      assert(intrin->intrinsic == nir_intrinsic_load_deref);
      assert(buffer_resource_has_zero_index(desc_load));

      b->cursor = nir_before_instr(&desc_load->instr);
      nir_def *offset = nir_imm_int(b, mapping->sourceData.pushDataOffset);

      /* This moves the cursor */
      offset = build_buffer_addr_for_deref(b, offset, deref,
                                           nir_address_format_32bit_offset);

      const uint32_t range = mapping->sourceData.pushDataOffset +
                             glsl_get_explicit_size(root_cast->type, false);

      b->cursor = nir_before_instr(&intrin->instr);
      nir_def *val = nir_load_push_constant(b, intrin->def.num_components,
                                            intrin->def.bit_size,
                                            offset, .range = range);
      nir_def_replace(&intrin->def, val);
      return true;
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_RESOURCE_HEAP_DATA_EXT: {
      assert(nir_deref_mode_is(deref, nir_var_mem_ubo));
      assert(intrin->intrinsic == nir_intrinsic_load_deref);
      assert(buffer_resource_has_zero_index(desc_load));

      b->cursor = nir_before_instr(&desc_load->instr);
      nir_def *heap_offset =
         vk_build_descriptor_heap_offset(b, ctx, mapping, resource_type, binding,
                                         NULL /* index */,
                                         false /* is_sampler */,
                                         NULL /* non_uniform_out */);

      /* This moves the cursor */
      heap_offset = build_buffer_addr_for_deref(b, heap_offset, deref,
                                                nir_address_format_32bit_offset);

      uint32_t align_mul, align_offset;
      if (!nir_get_explicit_deref_align(deref, true, &align_mul,
                                        &align_offset)) {
         /* If we don't have an alignment from the deref, assume scalar */
         assert(glsl_type_is_vector_or_scalar(deref->type) ||
                glsl_type_is_matrix(deref->type));
         align_mul = glsl_type_is_boolean(deref->type) ?
                     4 : glsl_get_bit_size(deref->type) / 8;
         align_offset = 0;
      }

      b->cursor = nir_before_instr(&intrin->instr);
      nir_def *val = nir_load_resource_heap_data(b, intrin->def.num_components,
                                                 intrin->def.bit_size,
                                                 heap_offset,
                                                 .align_mul = align_mul,
                                                 .align_offset = align_offset);
      nir_def_replace(&intrin->def, val);
      return true;
   }

   case VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT:
   case VK_DESCRIPTOR_MAPPING_SOURCE_INDIRECT_ADDRESS_EXT:
   case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_DATA_EXT:
   case VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_ADDRESS_EXT: {
      b->cursor = nir_before_instr(&desc_load->instr);

      nir_def *addr =
         vk_build_descriptor_heap_address(b, mapping);

      /* This moves the cursor */
      addr = build_buffer_addr_for_deref(b, addr, deref,
                                         nir_address_format_64bit_global);

      b->cursor = nir_before_instr(&intrin->instr);
      nir_lower_explicit_io_instr(b, intrin, addr,
                                  nir_address_format_64bit_global);
      return true;
   }

   default:
      /* We could also handle descriptor offset mapping sources here but
       * there's no point.  They access a real descriptor so we don't need to
       * rewrite them to a different address format like we did for UBOs
       * above.  We can handle them in lower_load_descriptors.
       */
      return false;
   }
}

static bool
lower_heaps_load_buffer_ptr(nir_builder *b, nir_intrinsic_instr *ptr_load,
                            struct heap_mapping_ctx *ctx)
{
   assert(ptr_load->intrinsic == nir_intrinsic_load_buffer_ptr_deref);
   nir_deref_instr *deref = nir_src_as_deref(ptr_load->src[0]);

   nir_deref_instr *root_cast = deref_get_root_cast(deref);
   if (root_cast == NULL)
      return false;

   /* We're building an offset.  It starts at zero */
   b->cursor = nir_before_instr(&root_cast->instr);
   nir_def *heap_base_offset = nir_imm_int(b, 0);

   /* This moves the cursor */
   nir_def *heap_offset =
      build_buffer_addr_for_deref(b, heap_base_offset, deref,
                                  nir_address_format_32bit_offset);

   const nir_resource_type resource_type =
      nir_intrinsic_resource_type(ptr_load);

   b->cursor = nir_before_instr(&ptr_load->instr);
   nir_def *desc = nir_load_heap_descriptor(b, ptr_load->def.num_components,
                                            ptr_load->def.bit_size,
                                            heap_offset,
                                            .resource_type = resource_type);

   nir_def_replace(&ptr_load->def, desc);

   return true;
}

static bool
lower_heaps_load_descriptor(nir_builder *b, nir_intrinsic_instr *desc_load,
                            struct heap_mapping_ctx *ctx)
{
   if (ctx->info == NULL)
      return false;

   uint32_t set, binding;
   nir_resource_type resource_type;
   if (!get_buffer_resource_binding(desc_load, &set, &binding, &resource_type))
      return false; /* This must be old school variable pointers */

   const VkDescriptorSetAndBindingMappingEXT *mapping =
      vk_descriptor_heap_mapping(ctx->info, set, binding, resource_type);
   if (mapping == NULL)
      return false; /* Descriptor sets */

   b->cursor = nir_before_instr(&desc_load->instr);

   if (mapping->source == VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_DATA_EXT ||
       mapping->source == VK_DESCRIPTOR_MAPPING_SOURCE_RESOURCE_HEAP_DATA_EXT) {
      /* These have to be handled by try_lower_deref_access() */
      assert(resource_type == nir_resource_type_uniform_buffer);
      return false;
   } else if (mapping->source == VK_DESCRIPTOR_MAPPING_SOURCE_PUSH_ADDRESS_EXT ||
              mapping->source == VK_DESCRIPTOR_MAPPING_SOURCE_INDIRECT_ADDRESS_EXT ||
              mapping->source == VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_DATA_EXT ||
              mapping->source == VK_DESCRIPTOR_MAPPING_SOURCE_SHADER_RECORD_ADDRESS_EXT) {
      /* Other resource types have to be handled by try_lower_deref_access() */
      if (resource_type != nir_resource_type_acceleration_structure)
         return false;

      nir_def *addr = vk_build_descriptor_heap_address(b, mapping);
      assert(addr);
      nir_def_replace(&desc_load->def, addr);
   } else {
      nir_def *index = build_buffer_resource_index(b, desc_load);

      /* Everything else is an offset */
      nir_def *heap_offset =
         vk_build_descriptor_heap_offset(b, ctx, mapping, resource_type, binding,
                                         index, false /* is_sampler */,
                                         NULL /* non_uniform_out */);
      nir_def *desc = nir_load_heap_descriptor(b, desc_load->def.num_components,
                                               desc_load->def.bit_size,
                                               heap_offset,
                                               .resource_type = resource_type);

      nir_def_replace(&desc_load->def, desc);
   }

   return true;
}

static bool
lower_heaps_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                   struct heap_mapping_ctx *ctx)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_image_deref_load_raw_intel:
   case nir_intrinsic_image_deref_store_raw_intel:
   case nir_intrinsic_image_deref_fragment_mask_load_amd:
   case nir_intrinsic_image_deref_store_block_agx:
      return lower_heaps_image(b, intrin, ctx, true);
   case nir_intrinsic_image_heap_load:
   case nir_intrinsic_image_heap_sparse_load:
   case nir_intrinsic_image_heap_store:
   case nir_intrinsic_image_heap_atomic:
   case nir_intrinsic_image_heap_atomic_swap:
   case nir_intrinsic_image_heap_size:
   case nir_intrinsic_image_heap_samples:
   case nir_intrinsic_image_heap_load_raw_intel:
   case nir_intrinsic_image_heap_store_raw_intel:
   case nir_intrinsic_image_heap_fragment_mask_load_amd:
   case nir_intrinsic_image_heap_store_block_agx:
      return lower_heaps_image(b, intrin, ctx, false);

   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
   case nir_intrinsic_load_deref_block_intel:
   case nir_intrinsic_store_deref_block_intel:
   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_deref_atomic_swap:
      return try_lower_heaps_deref_access(b, intrin, ctx);

   case nir_intrinsic_load_buffer_ptr_deref:
      return lower_heaps_load_buffer_ptr(b, intrin, ctx);

   case nir_intrinsic_load_vulkan_descriptor:
      return lower_heaps_load_descriptor(b, intrin, ctx);

   default:
      return false;
   }
}

static bool
lower_heaps_instr(nir_builder *b, nir_instr *instr, void *data)
{
   switch (instr->type) {
   case nir_instr_type_tex:
      return lower_heaps_tex(b, nir_instr_as_tex(instr), data);
   case nir_instr_type_intrinsic:
      return lower_heaps_intrin(b, nir_instr_as_intrinsic(instr), data);
   default:
      return false;
   }
}

bool
vk_nir_lower_descriptor_heaps(
   nir_shader *nir,
   const VkShaderDescriptorSetAndBindingMappingInfoEXT *mapping,
   const vk_nir_lower_descriptor_heaps_options *options,
   struct vk_sampler_state_array *embedded_samplers_out)
{
   struct heap_mapping_ctx ctx = {
      .info = mapping,
      .options = options,
      .sampler_idx_map = _mesa_hash_table_create(NULL, hash_sampler,
                                                 samplers_equal),
   };

   bool progress =
      nir_shader_instructions_pass(nir, lower_heaps_instr,
                                   nir_metadata_control_flow, &ctx);

   memset(embedded_samplers_out, 0, sizeof(*embedded_samplers_out));

   const uint32_t embedded_sampler_count = ctx.sampler_idx_map->entries;
   if (embedded_sampler_count > 0) {
      embedded_samplers_out->sampler_count = embedded_sampler_count;
      embedded_samplers_out->samplers =
         malloc(embedded_sampler_count * sizeof(struct vk_sampler_state));
      hash_table_foreach(ctx.sampler_idx_map, entries) {
         const struct vk_sampler_state *state = entries->key;
         const uint32_t index = (uintptr_t)entries->data;
         embedded_samplers_out->samplers[index] = *state;
      }
   }

   ralloc_free(ctx.sampler_idx_map);

   return progress;
}
