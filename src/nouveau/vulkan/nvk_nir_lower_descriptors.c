/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cmd_buffer.h"
#include "nvk_descriptor_set_layout.h"
#include "nvk_descriptor_types.h"
#include "nvk_shader.h"

#include "vk_pipeline.h"

#include "nir_builder.h"
#include "nir_deref.h"

#include "clb097.h"
#include "clc397.h"
#include "clc597.h"

struct lower_desc_cbuf {
   struct nvk_cbuf key;

   uint32_t use_count;

   uint64_t start;
   uint64_t end;
};

DERIVE_HASH_TABLE(nvk_cbuf);

static int
compar_cbufs(const void *_a, const void *_b)
{
   const struct lower_desc_cbuf *a = _a;
   const struct lower_desc_cbuf *b = _b;

#define COMPAR(field, pos) \
   if (a->field < b->field) return -(pos); \
   if (a->field > b->field) return (pos);

   /* Sort by most used first */
   COMPAR(use_count, -1)

   /* Keep the list stable by then sorting by key fields. */
   COMPAR(key.type, 1)
   COMPAR(key.desc_set, 1)
   COMPAR(key.dynamic_idx, 1)
   COMPAR(key.desc_offset, 1)

#undef COMPAR

   return 0;
}

struct lower_descriptors_ctx {
   const struct nv_device_info *dev_info;
   const struct nvk_descriptor_set_layout *set_layouts[NVK_MAX_SETS];

   bool use_bindless_cbuf;
   bool use_bindless_cbuf_2;
   bool use_edb_buffer_views;
   bool clamp_desc_array_bounds;
   bool indirect_bind;
   nir_address_format ubo_addr_format;
   nir_address_format ssbo_addr_format;

   struct hash_table *cbufs;
   struct nvk_cbuf_map *cbuf_map;
};

static bool
descriptor_type_is_ubo(VkDescriptorType desc_type)
{
   switch (desc_type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
      return true;

   default:
      return false;
   }
}

static bool
descriptor_type_is_ssbo(VkDescriptorType desc_type)
{
   switch (desc_type) {
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return true;

   default:
      return false;
   }
}

static void
record_cbuf_use(const struct nvk_cbuf *key, uint64_t start, uint64_t end,
                struct lower_descriptors_ctx *ctx)
{
   struct hash_entry *entry = _mesa_hash_table_search(ctx->cbufs, key);
   if (entry != NULL) {
      struct lower_desc_cbuf *cbuf = entry->data;
      cbuf->use_count++;
      cbuf->start = MIN2(cbuf->start, start);
      cbuf->end = MAX2(cbuf->end, end);
   } else {
      struct lower_desc_cbuf *cbuf =
         ralloc(ctx->cbufs, struct lower_desc_cbuf);
      *cbuf = (struct lower_desc_cbuf) {
         .key = *key,
         .use_count = 1,
         .start = start,
         .end = end,
      };
      _mesa_hash_table_insert(ctx->cbufs, &cbuf->key, cbuf);
   }
}

static const struct nvk_descriptor_set_binding_layout *
get_binding_layout(uint32_t set, uint32_t binding,
                   const struct lower_descriptors_ctx *ctx)
{
   assert(set < NVK_MAX_SETS);
   assert(ctx->set_layouts[set] != NULL);

   const struct nvk_descriptor_set_layout *set_layout = ctx->set_layouts[set];

   assert(binding < set_layout->binding_count);
   return &set_layout->binding[binding];
}

static void
record_descriptor_cbuf_use(uint32_t set, uint32_t binding, nir_src *index,
                           struct lower_descriptors_ctx *ctx)
{
   const struct nvk_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   const struct nvk_cbuf key = {
      .type = NVK_CBUF_TYPE_DESC_SET,
      .desc_set = set,
   };

   uint64_t start, end;
   if (index == NULL) {
      /* When we don't have an index, assume 0 */
      start = binding_layout->offset;
      end = start + binding_layout->stride;
   } else if (nir_src_is_const(*index)) {
      start = binding_layout->offset +
              nir_src_as_uint(*index) * binding_layout->stride;
      end = start + binding_layout->stride;
   } else {
      start = binding_layout->offset;
      end = start + binding_layout->array_size * binding_layout->stride;
   }

   record_cbuf_use(&key, start, end, ctx);
}

static void
record_vulkan_resource_cbuf_use(nir_intrinsic_instr *intrin,
                                struct lower_descriptors_ctx *ctx)
{
   assert(intrin->intrinsic == nir_intrinsic_vulkan_resource_index);

   /* These we'll handle later */
   if (descriptor_type_is_ubo(nir_intrinsic_desc_type(intrin)))
      return;

   record_descriptor_cbuf_use(nir_intrinsic_desc_set(intrin),
                              nir_intrinsic_binding(intrin),
                              &intrin->src[0], ctx);
}

static void
record_deref_descriptor_cbuf_use(nir_deref_instr *deref,
                                 struct lower_descriptors_ctx *ctx)
{
   nir_src *index_src = NULL;
   if (deref->deref_type == nir_deref_type_array) {
      index_src = &deref->arr.index;
      deref = nir_deref_instr_parent(deref);
   }

   assert(deref->deref_type == nir_deref_type_var);
   nir_variable *var = deref->var;

   record_descriptor_cbuf_use(var->data.descriptor_set,
                              var->data.binding,
                              index_src, ctx);
}

static void
record_tex_descriptor_cbuf_use(nir_tex_instr *tex,
                               struct lower_descriptors_ctx *ctx)
{
   nir_deref_instr *texture = nir_get_tex_deref(tex, nir_tex_src_texture_deref);
   if (texture != NULL)
      record_deref_descriptor_cbuf_use(texture, ctx);

   nir_deref_instr *sampler = nir_get_tex_deref(tex, nir_tex_src_sampler_deref);
   if (sampler != NULL)
      record_deref_descriptor_cbuf_use(sampler, ctx);
}

static struct nvk_cbuf
ubo_deref_to_cbuf(nir_deref_instr *deref,
                  nir_intrinsic_instr **resource_index_out,
                  uint64_t *offset_out,
                  uint64_t *start_out, uint64_t *end_out,
                  const struct lower_descriptors_ctx *ctx)
{
   assert(nir_deref_mode_is(deref, nir_var_mem_ubo));

   /* In case we early return */
   *offset_out = 0;
   *start_out = 0;
   *end_out = UINT64_MAX;
   *resource_index_out = NULL;

   const struct nvk_cbuf invalid = {
      .type = NVK_CBUF_TYPE_INVALID,
   };

   uint64_t offset = 0;
   uint64_t range = glsl_get_explicit_size(deref->type, false);
   bool offset_valid = true;
   while (deref->deref_type != nir_deref_type_cast) {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);

      switch (deref->deref_type) {
      case nir_deref_type_var:
         UNREACHABLE("Buffers don't use variables in Vulkan");

      case nir_deref_type_array:
      case nir_deref_type_array_wildcard: {
         uint32_t stride = nir_deref_instr_array_stride(deref);
         if (deref->deref_type == nir_deref_type_array &&
             nir_src_is_const(deref->arr.index)) {
            offset += nir_src_as_uint(deref->arr.index) * stride;
         } else {
            range = glsl_get_length(parent->type) * stride;
         }
         break;
      }

      case nir_deref_type_ptr_as_array:
         /* All bets are off.  We shouldn't see these most of the time
          * anyway, even with variable pointers.
          */
         offset_valid = false;
         UNREACHABLE("Variable pointers aren't allowed on UBOs");
         break;

      case nir_deref_type_struct: {
         offset += glsl_get_struct_field_offset(parent->type,
                                                deref->strct.index);
         break;
      }

      default:
         UNREACHABLE("Unknown deref type");
      }

      deref = parent;
   }

   nir_intrinsic_instr *load_desc = nir_src_as_intrinsic(deref->parent);
   if (load_desc == NULL ||
       load_desc->intrinsic != nir_intrinsic_load_vulkan_descriptor)
      return invalid;

   nir_intrinsic_instr *res_index = nir_src_as_intrinsic(load_desc->src[0]);
   if (res_index == NULL ||
       res_index->intrinsic != nir_intrinsic_vulkan_resource_index)
      return invalid;

   /* We try to early return as little as possible prior to this point so we
    * can return the resource index intrinsic in as many cases as possible.
    * After this point, though, early returns are fair game.
    */
   *resource_index_out = res_index;

   if (!offset_valid || !nir_src_is_const(res_index->src[0]))
      return invalid;

   uint32_t set = nir_intrinsic_desc_set(res_index);
   uint32_t binding = nir_intrinsic_binding(res_index);
   uint32_t index = nir_src_as_uint(res_index->src[0]);

   const struct nvk_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_MUTABLE_EXT:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
      *offset_out = 0;
      *start_out = offset;
      *end_out = offset + range;
      return (struct nvk_cbuf) {
         .type = NVK_CBUF_TYPE_UBO_DESC,
         .desc_set = set,
         .desc_offset = binding_layout->offset +
                        index * binding_layout->stride,
      };
   }

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: {
      *offset_out = 0;
      *start_out = offset;
      *end_out = offset + range;

      return (struct nvk_cbuf) {
         .type = NVK_CBUF_TYPE_DYNAMIC_UBO,
         .desc_set = set,
         .dynamic_idx = binding_layout->dynamic_buffer_index + index,
      };
   }

   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
      *offset_out = binding_layout->offset;
      *start_out = binding_layout->offset + offset;
      *end_out = *start_out + range;

      return (struct nvk_cbuf) {
         .type = NVK_CBUF_TYPE_DESC_SET,
         .desc_set = set,
      };
   }

   default:
      return invalid;
   }
}

static void
record_load_ubo_cbuf_uses(nir_deref_instr *deref,
                          struct lower_descriptors_ctx *ctx)
{
   assert(nir_deref_mode_is(deref, nir_var_mem_ubo));

   UNUSED uint64_t offset;
   uint64_t start, end;
   nir_intrinsic_instr *res_index;
   struct nvk_cbuf cbuf =
      ubo_deref_to_cbuf(deref, &res_index, &offset, &start, &end, ctx);

   if (cbuf.type != NVK_CBUF_TYPE_INVALID) {
      record_cbuf_use(&cbuf, start, end, ctx);
   } else if (res_index != NULL) {
      record_vulkan_resource_cbuf_use(res_index, ctx);
   }
}

static bool
record_cbuf_uses_instr(UNUSED nir_builder *b, nir_instr *instr, void *_ctx)
{
   struct lower_descriptors_ctx *ctx = _ctx;

   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_vulkan_resource_index:
         record_vulkan_resource_cbuf_use(intrin, ctx);
         return false;

      case nir_intrinsic_load_deref: {
         nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
         if (nir_deref_mode_is(deref, nir_var_mem_ubo))
            record_load_ubo_cbuf_uses(deref, ctx);
         return false;
      }

      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_samples: {
         nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
         record_deref_descriptor_cbuf_use(deref, ctx);
         return false;
      }

      default:
         return false;
      }
      UNREACHABLE("All cases return false");
   }

   case nir_instr_type_tex:
      record_tex_descriptor_cbuf_use(nir_instr_as_tex(instr), ctx);
      return false;

   default:
      return false;
   }
}

static void
build_cbuf_map(nir_shader *nir, struct lower_descriptors_ctx *ctx)
{
   ctx->cbuf_map->cbuf_count = 0;

   /* Root descriptors always go in cbuf 0 */
   ctx->cbuf_map->cbufs[ctx->cbuf_map->cbuf_count++] = (struct nvk_cbuf) {
      .type = NVK_CBUF_TYPE_ROOT_DESC,
   };

   /* If we have constant data, put it at cbuf 1 */
   if (nir->constant_data_size > 0) {
      ctx->cbuf_map->cbufs[ctx->cbuf_map->cbuf_count++] = (struct nvk_cbuf) {
         .type = NVK_CBUF_TYPE_SHADER_DATA,
      };
   }

   if (ctx->indirect_bind)
      return;

   ctx->cbufs = nvk_cbuf_table_create(NULL);
   nir_shader_instructions_pass(nir, record_cbuf_uses_instr,
                                nir_metadata_all, (void *)ctx);

   struct lower_desc_cbuf *cbufs =
      ralloc_array(ctx->cbufs, struct lower_desc_cbuf,
                   _mesa_hash_table_num_entries(ctx->cbufs));

   uint32_t num_cbufs = 0;
   hash_table_foreach(ctx->cbufs, entry) {
      struct lower_desc_cbuf *cbuf = entry->data;

      /* We currently only start cbufs at the beginning so if it starts after
       * the max cbuf size, there's no point in including it in the list.
       */
      if (cbuf->start > NVK_MAX_CBUF_SIZE)
         continue;

      cbufs[num_cbufs++] = *cbuf;
   }

   qsort(cbufs, num_cbufs, sizeof(*cbufs), compar_cbufs);

   uint8_t max_cbuf_bindings;
   if (nir->info.stage == MESA_SHADER_COMPUTE ||
       nir->info.stage == MESA_SHADER_KERNEL) {
      max_cbuf_bindings = 8;
   } else {
      max_cbuf_bindings = 16;
   }

   for (uint32_t i = 0; i < num_cbufs; i++) {
      if (ctx->cbuf_map->cbuf_count >= max_cbuf_bindings)
         break;

      /* We can't support indirect cbufs in compute yet */
      if ((nir->info.stage == MESA_SHADER_COMPUTE ||
           nir->info.stage == MESA_SHADER_KERNEL) &&
          cbufs[i].key.type == NVK_CBUF_TYPE_UBO_DESC)
         continue;

      /* Prior to Turing, indirect cbufs require splitting the pushbuf and
       * pushing bits of the descriptor set.  Doing this every draw call is
       * probably more overhead than it's worth.
       */
      if (ctx->dev_info->cls_eng3d < TURING_A &&
          cbufs[i].key.type == NVK_CBUF_TYPE_UBO_DESC)
         continue;

      ctx->cbuf_map->cbufs[ctx->cbuf_map->cbuf_count++] = cbufs[i].key;
   }

   ralloc_free(ctx->cbufs);
   ctx->cbufs = NULL;
}

static int
get_mapped_cbuf_idx(const struct nvk_cbuf *key,
                    const struct lower_descriptors_ctx *ctx)
{
   if (ctx->cbuf_map == NULL)
      return -1;

   for (uint32_t c = 0; c < ctx->cbuf_map->cbuf_count; c++) {
      if (nvk_cbuf_equal(&ctx->cbuf_map->cbufs[c], key)) {
         return c;
      }
   }

   return -1;
}

static bool
lower_load_ubo_intrin(nir_builder *b, nir_intrinsic_instr *load, void *_ctx)
{
   const struct lower_descriptors_ctx *ctx = _ctx;

   if (load->intrinsic != nir_intrinsic_load_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(load->src[0]);
   if (!nir_deref_mode_is(deref, nir_var_mem_ubo))
      return false;

   uint64_t offset, end;
   UNUSED uint64_t start;
   UNUSED nir_intrinsic_instr *res_index;
   struct nvk_cbuf cbuf_key =
      ubo_deref_to_cbuf(deref, &res_index, &offset, &start, &end, ctx);

   if (cbuf_key.type == NVK_CBUF_TYPE_INVALID)
      return false;

   if (end > NVK_MAX_CBUF_SIZE)
      return false;

   int cbuf_idx = get_mapped_cbuf_idx(&cbuf_key, ctx);
   if (cbuf_idx < 0)
      return false;

   b->cursor = nir_before_instr(&load->instr);

   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   nir_def *addr = nir_imm_ivec2(b, cbuf_idx, offset);
   nir_address_format addr_format = nir_address_format_32bit_index_offset;
   for (nir_deref_instr **p = &path.path[1]; *p != NULL; p++)
      addr = nir_explicit_io_address_from_deref(b, *p, addr, addr_format);

   nir_deref_path_finish(&path);

   nir_lower_explicit_io_instr(b, load, addr, addr_format);

   return true;
}

static bool
lower_load_constant(nir_builder *b, nir_intrinsic_instr *load,
                    const struct lower_descriptors_ctx *ctx)
{
   assert(load->intrinsic == nir_intrinsic_load_constant);

   const struct nvk_cbuf cbuf_key = {
      .type = NVK_CBUF_TYPE_SHADER_DATA,
   };
   int cbuf_idx = get_mapped_cbuf_idx(&cbuf_key, ctx);
   assert(cbuf_idx >= 0);

   uint32_t base = nir_intrinsic_base(load);

   b->cursor = nir_before_instr(&load->instr);

   nir_def *offset = nir_iadd_imm(b, load->src[0].ssa, base);
   nir_def *data = nir_ldc_nv(b, load->def.num_components, load->def.bit_size,
                              nir_imm_int(b, cbuf_idx), offset,
                              .align_mul = nir_intrinsic_align_mul(load),
                              .align_offset = nir_intrinsic_align_offset(load));

   nir_def_rewrite_uses(&load->def, data);

   return true;
}

static nir_def *
load_descriptor_set_addr(nir_builder *b, uint32_t set,
                         UNUSED const struct lower_descriptors_ctx *ctx)
{
   uint32_t set_addr_offset = nvk_root_descriptor_offset(sets) +
      set * sizeof(struct nvk_buffer_address);

   return nir_ldc_nv(b, 1, 64, nir_imm_int(b, 0),
                     nir_imm_int(b, set_addr_offset),
                     .align_mul = 8, .align_offset = 0);
}

static nir_def *
load_dynamic_buffer_start(nir_builder *b, uint32_t set,
                          const struct lower_descriptors_ctx *ctx)
{
   int dynamic_buffer_start_imm = 0;
   for (uint32_t s = 0; s < set; s++) {
      if (ctx->set_layouts[s] == NULL) {
         dynamic_buffer_start_imm = -1;
         break;
      }

      dynamic_buffer_start_imm += ctx->set_layouts[s]->dynamic_buffer_count;
   }

   if (dynamic_buffer_start_imm >= 0) {
      return nir_imm_int(b, dynamic_buffer_start_imm);
   } else {
      uint32_t root_offset =
         nvk_root_descriptor_offset(set_dynamic_buffer_start) + set;

      return nir_u2u32(b, nir_ldc_nv(b, 1, 8, nir_imm_int(b, 0),
                                     nir_imm_int(b, root_offset),
                                     .align_mul = 1, .align_offset = 0));
   }
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
      nir_def *dynamic_buffer_start = load_dynamic_buffer_start(b, set, ctx);

      index = nir_iadd(b, index,
                       nir_iadd_imm(b, dynamic_buffer_start,
                                    binding_layout->dynamic_buffer_index));
      uint32_t desc_size = sizeof(union nvk_buffer_descriptor);
      nir_def *root_desc_offset =
         nir_iadd_imm(b, nir_imul_imm(b, index, desc_size),
                      nvk_root_descriptor_offset(dynamic_buffers));

      assert(num_components * bit_size <= desc_size * 8);
      return nir_ldc_nv(b, num_components, bit_size,
                        nir_imm_int(b, 0), root_desc_offset,
                        .align_mul = 16, .align_offset = 0);
   }

   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
      nir_def *base_addr =
         nir_iadd_imm(b, load_descriptor_set_addr(b, set, ctx),
                          binding_layout->offset);

      assert(binding_layout->stride == 1);
      const uint32_t binding_size = binding_layout->array_size;

      if (ctx->use_bindless_cbuf_2) {
         assert(num_components == 1 && bit_size == 64);
         const uint32_t size = align(binding_size, 16);
         return nir_ior_imm(b, nir_ishr_imm(b, base_addr, 6),
                               ((uint64_t)size >> 4) << 51);
      } else if (ctx->use_bindless_cbuf) {
         assert(num_components == 1 && bit_size == 64);
         const uint32_t size = align(binding_size, 16);
         return nir_ior_imm(b, nir_ishr_imm(b, base_addr, 4),
                               ((uint64_t)size >> 4) << 45);
      } else {
         /* Convert it to nir_address_format_64bit_bounded_global */
         assert(num_components == 4 && bit_size == 32);
         return nir_vec4(b, nir_unpack_64_2x32_split_x(b, base_addr),
                            nir_unpack_64_2x32_split_y(b, base_addr),
                            nir_imm_int(b, binding_size),
                            nir_imm_int(b, 0));
      }
   }

   default: {
      assert(binding_layout->stride > 0);
      nir_def *desc_ubo_offset =
         nir_iadd_imm(b, nir_imul_imm(b, index, binding_layout->stride),
                         binding_layout->offset + offset_B);

      uint64_t max_desc_ubo_offset = binding_layout->offset +
         binding_layout->array_size * binding_layout->stride;

      unsigned desc_align_mul = (1 << (ffs(binding_layout->stride) - 1));
      desc_align_mul = MIN2(desc_align_mul, 16);
      unsigned desc_align_offset = binding_layout->offset + offset_B;
      desc_align_offset %= desc_align_mul;

      const struct nvk_cbuf cbuf_key = {
         .type = NVK_CBUF_TYPE_DESC_SET,
         .desc_set = set,
      };
      int cbuf_idx = get_mapped_cbuf_idx(&cbuf_key, ctx);

      if (cbuf_idx >= 0 && max_desc_ubo_offset <= NVK_MAX_CBUF_SIZE) {
         return nir_ldc_nv(b, num_components, bit_size,
                           nir_imm_int(b, cbuf_idx),
                           desc_ubo_offset,
                           .align_mul = desc_align_mul,
                           .align_offset = desc_align_offset);
      } else {
         nir_def *set_addr = load_descriptor_set_addr(b, set, ctx);
         return nir_load_global_constant_offset(b, num_components, bit_size,
                                                set_addr, desc_ubo_offset,
                                                .align_mul = desc_align_mul,
                                                .align_offset = desc_align_offset);
      }
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

   const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   if (descriptor_type_is_ubo(desc_type) && ctx->use_bindless_cbuf) {
      nir_def *desc = load_descriptor(b, 1, 64, set, binding, index, 0, ctx);

      /* The descriptor is just the handle.  NIR also needs an offset. */
      return nir_vec3(b, nir_unpack_64_2x32_split_x(b, desc),
                         nir_unpack_64_2x32_split_y(b, desc),
                         nir_imm_int(b, 0));
   } else {
      nir_def *desc = load_descriptor(b, 4, 32, set, binding, index, 0, ctx);

      /* We know a priori that the the .w compnent (offset) is zero */
      return nir_vec4(b, nir_channel(b, desc, 0),
                         nir_channel(b, desc, 1),
                         nir_channel(b, desc, 2),
                         nir_imm_int(b, 0));
   }
}

static bool
try_lower_load_vulkan_descriptor(nir_builder *b, nir_intrinsic_instr *intrin,
                                 const struct lower_descriptors_ctx *ctx)
{
   ASSERTED const VkDescriptorType desc_type = nir_intrinsic_desc_type(intrin);
   b->cursor = nir_before_instr(&intrin->instr);

   nir_intrinsic_instr *idx_intrin = nir_src_as_intrinsic(intrin->src[0]);
   if (idx_intrin == NULL || !is_idx_intrin(idx_intrin)) {
      assert(descriptor_type_is_ssbo(desc_type));
      return false;
   }

   nir_def *desc = load_descriptor_for_idx_intrin(b, idx_intrin, ctx);

   nir_def_rewrite_uses(&intrin->def, desc);

   return true;
}

static nir_def *
_load_root_table(nir_builder *b,
                 unsigned num_components, unsigned bit_size,
                 uint32_t root_table_offset,
                 const struct lower_descriptors_ctx *ctx)
{
   unsigned align_mul = bit_size / 8;
   return nir_ldc_nv(b, num_components, bit_size,
                     nir_imm_int(b, 0), /* Root table */
                     nir_imm_int(b, root_table_offset),
                     .align_mul = align_mul,
                     .align_offset = 0);
}

#define load_root_table(b, nc, bs, member, ctx) \
   _load_root_table(b, nc, bs, nvk_root_descriptor_offset(member), ctx)

static bool
_lower_sysval_to_root_table(nir_builder *b, nir_intrinsic_instr *intrin,
                            uint32_t root_table_offset,
                            const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *val = _load_root_table(b, intrin->def.num_components,
                                   intrin->def.bit_size,
                                   root_table_offset, ctx);

   nir_def_rewrite_uses(&intrin->def, val);

   return true;
}

#define lower_sysval_to_root_table(b, intrin, member, ctx)           \
   _lower_sysval_to_root_table(b, intrin,                            \
                               nvk_root_descriptor_offset(member),   \
                               ctx)

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
      nir_ldc_nv(b, load->def.num_components, load->def.bit_size,
                 nir_imm_int(b, 0), offset,
                 .align_mul = load->def.bit_size / 8,
                 .align_offset = 0);

   nir_def_rewrite_uses(&load->def, val);

   return true;
}

static bool
lower_load_input_attachment_coord(nir_builder *b, nir_intrinsic_instr *load,
                                  const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&load->instr);

   nir_def *pos = nir_f2i32(b, nir_load_frag_coord(b));

   nir_def *layer = nir_load_layer_id(b);
   nir_def *view = load_root_table(b, 1, 32, draw.view_index, ctx);

   nir_def *coord = nir_vec3(b, nir_channel(b, pos, 0),
                                nir_channel(b, pos, 1),
                                nir_iadd(b, layer, view));

   nir_def_replace(&load->def, coord);

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
is_edb_buffer_view(nir_deref_instr *deref,
                   const struct lower_descriptors_ctx *ctx)
{
   if (glsl_get_sampler_dim(deref->type) != GLSL_SAMPLER_DIM_BUF)
      return false;

   if (ctx->use_edb_buffer_views)
      return true;

   nir_variable *var = nir_deref_instr_get_variable(deref);
   uint8_t set = var->data.descriptor_set;

   return (ctx->set_layouts[set]->flags &
           VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) &&
          !(ctx->set_layouts[set]->flags &
            VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT);
}

static nir_def *
edb_buffer_view_is_null(nir_builder *b, nir_def *desc)
{
   assert(desc->num_components == 4);
   nir_def *index = nir_channel(b, desc, 0);
   return nir_ieq_imm(b, index, 0);
}

static nir_def *
edb_buffer_view_offset_el(nir_builder *b, nir_def *desc)
{
   assert(desc->num_components == 4);
   return nir_channel(b, desc, 1);
}

static nir_def *
edb_buffer_view_size_el(nir_builder *b, nir_def *desc)
{
   assert(desc->num_components == 4);
   return nir_channel(b, desc, 2);
}

static nir_def *
edb_buffer_view_oob_alpha(nir_builder *b, nir_def *desc)
{
   assert(desc->num_components == 4);
   return nir_channel(b, desc, 3);
}

static nir_def *
edb_buffer_view_coord_is_in_bounds(nir_builder *b, nir_def *desc,
                                   nir_def *coord)
{
   assert(desc->num_components == 4);
   return nir_ult(b, coord, edb_buffer_view_size_el(b, desc));
}

static nir_def *
edb_buffer_view_index(nir_builder *b, nir_def *desc, nir_def *in_bounds)
{
   assert(desc->num_components == 4);
   nir_def *index = nir_channel(b, desc, 0);

   /* Use the NULL descriptor for OOB access */
   return nir_bcsel(b, in_bounds, index, nir_imm_int(b, 0));
}

static nir_def *
adjust_edb_buffer_view_coord(nir_builder *b, nir_def *desc, nir_def *coord)
{
   return nir_iadd(b, coord, edb_buffer_view_offset_el(b, desc));
}

static nir_def *
fixup_edb_buffer_view_result(nir_builder *b, nir_def *desc, nir_def *in_bounds,
                             nir_def *res, nir_alu_type dest_type)
{
   if (res->num_components < 4)
      return res;

   nir_def *is_null = edb_buffer_view_is_null(b, desc);
   nir_def *oob_alpha = edb_buffer_view_oob_alpha(b, desc);

   nir_def *a = nir_channel(b, res, 3);
   a = nir_bcsel(b, nir_ior(b, in_bounds, is_null), a, oob_alpha);
   return nir_vector_insert_imm(b, res, a, 3);
}

static void
lower_edb_buffer_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                              const struct lower_descriptors_ctx *ctx)
{
   assert(nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_BUF);

   b->cursor = nir_before_instr(&intrin->instr);
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_def *desc = load_resource_deref_desc(b, 4, 32, deref, 0, ctx);

   switch (intrin->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap: {
      nir_def *pos = intrin->src[1].ssa;
      nir_def *x = nir_channel(b, pos, 0);

      nir_def *in_bounds = edb_buffer_view_coord_is_in_bounds(b, desc, x);
      nir_def *index = edb_buffer_view_index(b, desc, in_bounds);

      nir_def *new_x = adjust_edb_buffer_view_coord(b, desc, x);
      pos = nir_vector_insert_imm(b, pos, new_x, 0);
      nir_src_rewrite(&intrin->src[1], pos);

      if (intrin->intrinsic == nir_intrinsic_image_deref_load ||
          intrin->intrinsic == nir_intrinsic_image_deref_sparse_load) {
         b->cursor = nir_after_instr(&intrin->instr);
         nir_def *res = &intrin->def;
         res = fixup_edb_buffer_view_result(b, desc, in_bounds, res,
                                            nir_intrinsic_dest_type(intrin));
         nir_def_rewrite_uses_after(&intrin->def, res);
      }

      nir_rewrite_image_intrinsic(intrin, index, true);
      break;
   }

   case nir_intrinsic_image_deref_size: {
      assert(intrin->def.num_components == 1);
      nir_def *size_el = nir_channel(b, desc, 2);
      nir_def_rewrite_uses(&intrin->def, size_el);
      break;
   }

   default:
      UNREACHABLE("Unknown image intrinsic");
   }
}

static bool
lower_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                   const struct lower_descriptors_ctx *ctx)
{
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   if (is_edb_buffer_view(deref, ctx)) {
      lower_edb_buffer_image_intrin(b, intrin, ctx);
      return true;
   }

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *desc = load_resource_deref_desc(b, 1, 32, deref, 0, ctx);
   nir_rewrite_image_intrinsic(intrin, desc, true);

   /* On pre-Volta hardware, we don't have real null descriptors.  Null
    * descriptors work well enough for sampling but they may not return the
    * correct query results.
    */
   if (ctx->dev_info->cls_eng3d < VOLTA_A &&
       (intrin->intrinsic == nir_intrinsic_bindless_image_size ||
        intrin->intrinsic == nir_intrinsic_bindless_image_samples)) {
      b->cursor = nir_after_instr(&intrin->instr);

      nir_def *image_handle =
         nir_iand_imm(b, desc, NVK_IMAGE_DESCRIPTOR_IMAGE_INDEX_MASK);
      nir_def *is_null = nir_ieq_imm(b, image_handle, 0);
      nir_def *zero = nir_imm_zero(b, intrin->def.num_components,
                                      intrin->def.bit_size);
      nir_def *res = nir_bcsel(b, is_null, zero, &intrin->def);
      nir_def_rewrite_uses_after(&intrin->def, res);
   }

   return true;
}

static bool
lower_load_image_info(nir_builder *b, nir_intrinsic_instr *load,
                      const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&load->instr);
   nir_deref_instr *deref = nir_src_as_deref(load->src[0]);
   unsigned offset = nir_intrinsic_base(load);
   assert(load->def.bit_size == 32);
   nir_def *desc = load_resource_deref_desc(b, load->num_components, 32,
                                            deref, offset, ctx);
   nir_def_rewrite_uses(&load->def, desc);

   return true;
}

static bool
lower_interp_at_sample(nir_builder *b, nir_intrinsic_instr *interp,
                       const struct lower_descriptors_ctx *ctx)
{
   nir_def *sample = interp->src[1].ssa;

   b->cursor = nir_before_instr(&interp->instr);

   nir_def *loc = load_root_table(b, 1, 64, draw.sample_locations, ctx);

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
   case nir_intrinsic_load_constant:
      return lower_load_constant(b, intrin, ctx);

   case nir_intrinsic_load_vulkan_descriptor:
      return try_lower_load_vulkan_descriptor(b, intrin, ctx);

   case nir_intrinsic_load_workgroup_size:
      UNREACHABLE("Should have been lowered by nir_lower_cs_intrinsics()");

   case nir_intrinsic_load_num_workgroups:
      return lower_sysval_to_root_table(b, intrin, cs.group_count, ctx);

   case nir_intrinsic_load_base_workgroup_id:
      return lower_sysval_to_root_table(b, intrin, cs.base_group, ctx);

   case nir_intrinsic_load_push_constant:
      return lower_load_push_constant(b, intrin, ctx);

   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
      return lower_sysval_to_root_table(b, intrin, draw.base_vertex, ctx);

   case nir_intrinsic_load_base_instance:
      return lower_sysval_to_root_table(b, intrin, draw.base_instance, ctx);

   case nir_intrinsic_load_draw_id:
      return lower_sysval_to_root_table(b, intrin, draw.draw_index, ctx);

   case nir_intrinsic_load_view_index:
      return lower_sysval_to_root_table(b, intrin, draw.view_index, ctx);

   case nir_intrinsic_load_input_attachment_coord:
      return lower_load_input_attachment_coord(b, intrin, ctx);

   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
      return lower_image_intrin(b, intrin, ctx);

   case nir_intrinsic_image_deref_load_info_nv:
      return lower_load_image_info(b, intrin, ctx);

   case nir_intrinsic_interp_deref_at_sample:
      return lower_interp_at_sample(b, intrin, ctx);

   default:
      return false;
   }
}

static void
lower_edb_buffer_tex_instr(nir_builder *b, nir_tex_instr *tex,
                           nir_deref_instr *texture,
                           const struct lower_descriptors_ctx *ctx)
{
   assert(tex->sampler_dim == GLSL_SAMPLER_DIM_BUF);

   b->cursor = nir_before_instr(&tex->instr);

   nir_def *desc = load_resource_deref_desc(b, 4, 32, texture, 0, ctx);

   switch (tex->op) {
   case nir_texop_txf: {
      const int coord_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
      assert(coord_src_idx >= 0);
      nir_def *coord = tex->src[coord_src_idx].src.ssa;

      nir_def *in_bounds = edb_buffer_view_coord_is_in_bounds(b, desc, coord);

      nir_def *index = edb_buffer_view_index(b, desc, in_bounds);
      nir_def *new_coord = adjust_edb_buffer_view_coord(b, desc, coord);
      nir_def *u = nir_undef(b, 1, 32);

      /* The tricks we play for EDB use very large texel buffer views.  These
       * don't seem to play nicely with the tld instruction which thinks
       * buffers are a 1D texture.  However, suld seems fine with it so we'll
       * rewrite to use that.
       */
      nir_def *res = nir_bindless_image_load(b, tex->def.num_components,
                                             tex->def.bit_size,
                                             index,
                                             nir_vec4(b, new_coord, u, u, u),
                                             u, /* sample_id */
                                             nir_imm_int(b, 0), /* LOD */
                                             .image_dim = GLSL_SAMPLER_DIM_BUF,
                                             .image_array = false,
                                             .format = PIPE_FORMAT_NONE,
                                             .access = ACCESS_NON_WRITEABLE |
                                                       ACCESS_CAN_REORDER,
                                             .dest_type = tex->dest_type);
      if (tex->is_sparse) {
         nir_intrinsic_instr *intr = nir_def_as_intrinsic(res);
         intr->intrinsic = nir_intrinsic_bindless_image_sparse_load;
      }

      res = fixup_edb_buffer_view_result(b, desc, in_bounds,
                                         res, tex->dest_type);

      nir_def_rewrite_uses(&tex->def, res);
      break;
   }

   case nir_texop_txs: {
      assert(tex->def.num_components == 1);
      nir_def *size_el = edb_buffer_view_size_el(b, desc);
      nir_def_rewrite_uses(&tex->def, size_el);
      break;
   }

   default:
      UNREACHABLE("Invalid buffer texture op");
   }
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          const struct lower_descriptors_ctx *ctx)
{
   nir_deref_instr *texture =
      nir_steal_tex_deref(tex, nir_tex_src_texture_deref);
   nir_deref_instr *sampler =
      nir_steal_tex_deref(tex, nir_tex_src_sampler_deref);
   if (texture == NULL) {
      assert(sampler == NULL);
      return false;
   }

   nir_def *plane_ssa = nir_steal_tex_src(tex, nir_tex_src_plane);
   const uint32_t plane =
      plane_ssa ? nir_src_as_uint(nir_src_for_ssa(plane_ssa)) : 0;

   if (is_edb_buffer_view(texture, ctx)) {
      assert(plane == 0);
      lower_edb_buffer_tex_instr(b, tex, texture, ctx);
      return true;
   }

   b->cursor = nir_before_instr(&tex->instr);

   const uint64_t plane_offset_B =
      plane * sizeof(struct nvk_sampled_image_descriptor);

   nir_def *texture_desc =
         load_resource_deref_desc(b, 1, 32, texture, plane_offset_B, ctx);

   nir_def *combined_handle;

   if (!nir_tex_instr_need_sampler(tex)) {
      combined_handle = texture_desc;

      /* On Kepler and earlier, TXF takes a sampler but SPIR-V defines it as
       * not taking one so we can't trust the sampler from the client's image
       * descriptor.  Instead, mask off the top bits so we get a zero sampler
       * index which we've conveniently reserved at device cration time for a
       * special TXF sampler.
       */
      if (ctx->dev_info->cls_eng3d < MAXWELL_A) {
         combined_handle = nir_iand_imm(b, combined_handle,
                                        NVK_IMAGE_DESCRIPTOR_IMAGE_INDEX_MASK);
      }
   } else if (texture == sampler) {
      combined_handle = texture_desc;
   } else {
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

   nir_tex_instr_add_src(tex, nir_tex_src_texture_handle, combined_handle);

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

#define ROOT_DESC_BASE_ADDR_HI 0x0057de3c

static bool
lower_ssbo_resource_index(nir_builder *b, nir_intrinsic_instr *intrin,
                          const struct lower_descriptors_ctx *ctx)
{
   if (!descriptor_type_is_ssbo(nir_intrinsic_desc_type(intrin)))
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
      nir_def *dynamic_buffer_start =
         nir_iadd_imm(b, load_dynamic_buffer_start(b, set, ctx),
                      binding_layout->dynamic_buffer_index);

      nir_def *dynamic_binding_offset =
         nir_iadd_imm(b, nir_imul_imm(b, dynamic_buffer_start,
                                      sizeof(struct nvk_buffer_address)),
                      nvk_root_descriptor_offset(dynamic_buffers));

      binding_addr =
         nir_pack_64_2x32_split(b, dynamic_binding_offset,
                                nir_imm_int(b, ROOT_DESC_BASE_ADDR_HI));
      binding_stride = sizeof(struct nvk_buffer_address);
      break;
   }

   default:
      UNREACHABLE("Not an SSBO descriptor");
   }

   /* Tuck the stride in the top 8 bits of the binding address */
   binding_addr = nir_ior_imm(b, binding_addr, (uint64_t)binding_stride << 56);

   const uint32_t binding_size = binding_layout->array_size * binding_stride;
   nir_def *offset_in_binding = nir_imul_imm(b, index, binding_stride);

   /* We depend on this when we load descrptors */
   assert(binding_layout->array_size >= 1);

   nir_def *addr;
   switch (ctx->ssbo_addr_format) {
   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      addr = nir_vec4(b, nir_unpack_64_2x32_split_x(b, binding_addr),
                         nir_unpack_64_2x32_split_y(b, binding_addr),
                         nir_imm_int(b, binding_size),
                         offset_in_binding);
      break;

   default:
      UNREACHABLE("Unknown address mode");
   }

   nir_def_rewrite_uses(&intrin->def, addr);

   return true;
}

static bool
lower_ssbo_resource_reindex(nir_builder *b, nir_intrinsic_instr *intrin,
                            const struct lower_descriptors_ctx *ctx)
{
   if (!descriptor_type_is_ssbo(nir_intrinsic_desc_type(intrin)))
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *addr = intrin->src[0].ssa;
   nir_def *index = intrin->src[1].ssa;

   nir_def *addr_high32;
   switch (ctx->ssbo_addr_format) {
   case nir_address_format_64bit_global_32bit_offset:
   case nir_address_format_64bit_bounded_global:
      addr_high32 = nir_channel(b, addr, 1);
      break;

   default:
      UNREACHABLE("Unknown address mode");
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
   if (!descriptor_type_is_ssbo(nir_intrinsic_desc_type(intrin)))
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_def *addr = intrin->src[0].ssa;

   nir_def *base, *offset, *size = NULL;
   switch (ctx->ssbo_addr_format) {
   case nir_address_format_64bit_global_32bit_offset: {
      base = nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2));
      offset = nir_channel(b, addr, 3);
      break;
   }

   case nir_address_format_64bit_bounded_global: {
      base = nir_pack_64_2x32(b, nir_trim_vector(b, addr, 2));
      size = nir_channel(b, addr, 2);
      offset = nir_channel(b, addr, 3);
      break;
   }

   default:
      UNREACHABLE("Unknown address mode");
   }

   /* Mask off the binding stride */
   base = nir_iand_imm(b, base, BITFIELD64_MASK(56));

   nir_def *base_lo = nir_unpack_64_2x32_split_x(b, base);
   nir_def *base_hi = nir_unpack_64_2x32_split_y(b, base);

   nir_def *desc_root, *desc_global;
   nir_push_if(b, nir_ieq_imm(b, base_hi, ROOT_DESC_BASE_ADDR_HI));
   {
      desc_root = nir_load_ubo(b, 4, 32, nir_imm_int(b, 0),
                               nir_iadd(b, base_lo, offset),
                               .align_mul = 16, .align_offset = 0,
                               .range = ~0);
      if (size != NULL) {
         /* assert(binding_layout->array_size >= 1); */
         nir_def *is_oob = nir_ult(b, nir_iadd_imm(b, size, -16), offset);
         desc_root = nir_bcsel(b, is_oob, nir_imm_zero(b, 4, 32), desc_root);
      }
   }
   nir_push_else(b, NULL);
   {
      if (size != NULL) {
         desc_global = nir_load_global_constant_bounded(b, 4, 32, base,
                                                        offset, size,
                                                        .align_mul = 16,
                                                        .align_offset = 0);
      } else {
         desc_global = nir_load_global_constant_offset(b, 4, 32, base,
                                                       offset,
                                                       .align_mul = 16,
                                                       .align_offset = 0);
      }
   }
   nir_pop_if(b, NULL);
   nir_def *desc = nir_if_phi(b, desc_root, desc_global);

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
                          const struct nvk_physical_device *pdev,
                          VkShaderCreateFlagsEXT shader_flags,
                          const struct vk_pipeline_robustness_state *rs,
                          uint32_t set_layout_count,
                          struct vk_descriptor_set_layout * const *set_layouts,
                          struct nvk_cbuf_map *cbuf_map_out)
{
   struct lower_descriptors_ctx ctx = {
      .dev_info = &pdev->info,
      .use_bindless_cbuf = nvk_use_bindless_cbuf(&pdev->info),
      .use_bindless_cbuf_2 = nvk_use_bindless_cbuf_2(&pdev->info),
      .use_edb_buffer_views = nvk_use_edb_buffer_views(pdev),
      .clamp_desc_array_bounds =
         rs->storage_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||
         rs->uniform_buffers != VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT ||
         rs->images != VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DISABLED_EXT,
      .indirect_bind =
         shader_flags & VK_SHADER_CREATE_INDIRECT_BINDABLE_BIT_EXT,
      .ssbo_addr_format = nvk_ssbo_addr_format(pdev, rs),
      .ubo_addr_format = nvk_ubo_addr_format(pdev, rs),
   };

   assert(set_layout_count <= NVK_MAX_SETS);
   for (uint32_t s = 0; s < set_layout_count; s++) {
      if (set_layouts[s] != NULL)
         ctx.set_layouts[s] = vk_to_nvk_descriptor_set_layout(set_layouts[s]);
   }

   /* We run in four passes:
    *
    *  1. Find ranges of UBOs that we can promote to bound UBOs.  Nothing is
    *     actually lowered in this pass.  It's just analysis.
    *
    *  2. Try to lower UBO loads to cbufs based on the map we just created.
    *     We need to do this before the main lowering pass because it relies
    *     on the original descriptor load intrinsics.
    *
    *  3. Attempt to lower everything with direct descriptors.  This may fail
    *     to lower some SSBO intrinsics when variable pointers are used.
    *
    *  4. Clean up any SSBO intrinsics which are left and lower them to
    *     slightly less efficient but variable- pointers-correct versions.
    */

   bool pass_lower_ubo = false;
   if (cbuf_map_out != NULL) {
      ctx.cbuf_map = cbuf_map_out;
      build_cbuf_map(nir, &ctx);

      pass_lower_ubo =
         nir_shader_intrinsics_pass(nir, lower_load_ubo_intrin,
                                    nir_metadata_control_flow,
                                    (void *)&ctx);
   }

   bool pass_lower_descriptors =
      nir_shader_instructions_pass(nir, try_lower_descriptors_instr,
                                   nir_metadata_control_flow,
                                   (void *)&ctx);
   bool pass_lower_ssbo =
      nir_shader_instructions_pass(nir, lower_ssbo_descriptor_instr,
                                   nir_metadata_none,
                                   (void *)&ctx);
   return pass_lower_ubo || pass_lower_descriptors || pass_lower_ssbo;
}
