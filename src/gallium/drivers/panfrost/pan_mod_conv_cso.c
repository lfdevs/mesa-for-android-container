/*
 * Copyright (C) 2023 Amazon.com, Inc. or its affiliates
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pan_mod_conv_cso.h"
#include "nir/pipe_nir.h"
#include "nir_builder.h"
#include "pan_afbc.h"
#include "pan_context.h"
#include "pan_resource.h"
#include "pan_screen.h"
#include "pan_compiler.h"

#define panfrost_afbc_add_info_ubo(name, b)                                    \
   nir_variable *info_ubo = nir_variable_create(                               \
      b.shader, nir_var_mem_ubo,                                               \
      glsl_array_type(glsl_uint_type(),                                        \
                      sizeof(struct panfrost_afbc_##name##_info) / 4, 0),      \
      "info_ubo");                                                             \
   info_ubo->data.driver_location = 0;

#define panfrost_afbc_get_info_field(name, b, field)                           \
   nir_load_ubo(                                                               \
      (b), 1, sizeof(((struct panfrost_afbc_##name##_info *)0)->field) * 8,    \
      nir_imm_int(b, 0),                                                       \
      nir_imm_int(b, offsetof(struct panfrost_afbc_##name##_info, field)),     \
      .align_mul = 4, .range = ~0)

#define panfrost_mtk_add_info_ubo(name, b)                                     \
   nir_variable *info_ubo = nir_variable_create(                               \
      b.shader, nir_var_mem_ubo,                                               \
      glsl_array_type(glsl_uint_type(),                                        \
                      sizeof(struct panfrost_mtk_##name##_info) / 4, 0),       \
      "info_ubo");                                                             \
   info_ubo->data.driver_location = 0;

#define panfrost_mtk_get_info_field(name, b, field)                            \
   nir_load_ubo(                                                               \
      (b), 1, sizeof(((struct panfrost_mtk_##name##_info *)0)->field) * 8,     \
      nir_imm_int(b, 0),                                                       \
      nir_imm_int(b, offsetof(struct panfrost_mtk_##name##_info, field)),      \
      .align_mul = 4, .range = ~0)

static nir_def *
read_afbc_header(nir_builder *b, nir_def *buf, nir_def *idx)
{
   nir_def *offset = nir_imul_imm(b, idx, AFBC_HEADER_BYTES_PER_TILE);
   return nir_load_global(b, AFBC_HEADER_BYTES_PER_TILE / 4, 32,
                          nir_iadd(b, buf, nir_u2u64(b, offset)),
                          .align_mul = 16);
}

static void
write_afbc_header(nir_builder *b, nir_def *buf, nir_def *idx, nir_def *hdr)
{
   nir_def *offset = nir_imul_imm(b, idx, AFBC_HEADER_BYTES_PER_TILE);
   nir_store_global(b, hdr, nir_iadd(b, buf, nir_u2u64(b, offset)),
                    .align_mul = 16);
}

static nir_def *
get_superblock_size(nir_builder *b, unsigned arch, nir_def *hdr,
                    nir_def *uncompressed_size)
{
   nir_def *size = nir_imm_int(b, 0);

   unsigned body_base_ptr_len = 32;
   unsigned nr_subblocks = 16;
   unsigned sz_len = 6; /* bits */
   nir_def *words[4];
   nir_def *mask = nir_imm_int(b, (1 << sz_len) - 1);
   nir_def *is_solid_color = nir_imm_bool(b, false);

   for (int i = 0; i < 4; i++)
      words[i] = nir_channel(b, hdr, i);

   /* Sum up all of the subblock sizes */
   for (int i = 0; i < nr_subblocks; i++) {
      nir_def *subblock_size;
      unsigned bitoffset = body_base_ptr_len + (i * sz_len);
      unsigned start = bitoffset / 32;
      unsigned end = (bitoffset + (sz_len - 1)) / 32;
      unsigned offset = bitoffset % 32;

      /* Handle differently if the size field is split between two words
       * of the header */
      if (start != end) {
         subblock_size = nir_ior(b, nir_ushr_imm(b, words[start], offset),
                                 nir_ishl_imm(b, words[end], 32 - offset));
         subblock_size = nir_iand(b, subblock_size, mask);
      } else {
         subblock_size =
            nir_ubitfield_extract_imm(b, words[start], offset, sz_len);
      }
      subblock_size = nir_bcsel(b, nir_ieq_imm(b, subblock_size, 1),
                                uncompressed_size, subblock_size);
      size = nir_iadd(b, size, subblock_size);

      /* When the first subblock size is set to zero, the whole superblock is
       * filled with a solid color specified in the header */
      if (arch >= 7 && i == 0)
         is_solid_color = nir_ieq_imm(b, size, 0);
   }

   return (arch >= 7)
             ? nir_bcsel(b, is_solid_color, nir_imm_zero(b, 1, 32), size)
             : size;
}

static nir_def *
get_packed_offset(nir_builder *b, nir_def *layout, nir_def *idx,
                  nir_def **out_size)
{
   nir_def *layout_offset =
      nir_u2u64(b, nir_imul_imm(b, idx, sizeof(struct pan_afbc_payload_extent)));
   nir_def *range_ptr = nir_iadd(b, layout, layout_offset);
   nir_def *entry = nir_load_global(
      b, sizeof(struct pan_afbc_payload_extent) / 4, 32, range_ptr);
   nir_def *offset =
      nir_channel(b, entry, offsetof(struct pan_afbc_payload_extent, offset) / 4);

   if (out_size)
      *out_size =
         nir_channel(b, entry, offsetof(struct pan_afbc_payload_extent, size) / 4);

   return nir_u2u64(b, offset);
}

#define MAX_LINE_SIZE 16

static void
copy_superblock(nir_builder *b, nir_def *dst, nir_def *hdr_sz, nir_def *src,
                nir_def *layout, nir_def *idx, unsigned align)
{
   nir_def *hdr = read_afbc_header(b, src, idx);
   nir_def *src_body_base_ptr = nir_u2u64(b, nir_channel(b, hdr, 0));
   nir_def *src_bodyptr = nir_iadd(b, src, src_body_base_ptr);

   nir_def *size;
   nir_def *dst_offset = get_packed_offset(b, layout, idx, &size);
   nir_def *dst_body_base_ptr = nir_iadd(b, dst_offset, hdr_sz);
   nir_def *dst_bodyptr = nir_iadd(b, dst, dst_body_base_ptr);

   /* Replace the `base_body_ptr` field if not zero (solid color) */
   nir_def *hdr2 =
      nir_vector_insert_imm(b, hdr, nir_u2u32(b, dst_body_base_ptr), 0);
   hdr = nir_bcsel(b, nir_ieq_imm(b, src_body_base_ptr, 0), hdr, hdr2);
   write_afbc_header(b, dst, idx, hdr);

   nir_variable *offset_var =
      nir_local_variable_create(b->impl, glsl_uint_type(), "offset");
   nir_store_var(b, offset_var, nir_imm_int(b, 0), 1);
   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *offset = nir_load_var(b, offset_var);
      nir_if *loop_check = nir_push_if(b, nir_uge(b, offset, size));
      nir_jump(b, nir_jump_break);
      nir_push_else(b, loop_check);
      unsigned line_sz = align <= MAX_LINE_SIZE ? align : MAX_LINE_SIZE;
      for (unsigned i = 0; i < align / line_sz; ++i) {
         nir_def *src_line = nir_iadd(b, src_bodyptr, nir_u2u64(b, offset));
         nir_def *dst_line = nir_iadd(b, dst_bodyptr, nir_u2u64(b, offset));
         nir_store_global(
            b,
            nir_load_global(b, line_sz / 4, 32, src_line, .align_mul = line_sz),
            dst_line, .align_mul = line_sz);
         offset = nir_iadd_imm(b, offset, line_sz);
      }
      nir_store_var(b, offset_var, offset, 0x1);
      nir_pop_if(b, loop_check);
   }
   nir_pop_loop(b, loop);
}

#define panfrost_afbc_size_get_info_field(b, field)                            \
   panfrost_afbc_get_info_field(size, b, field)

static nir_shader *
panfrost_create_afbc_size_shader(struct panfrost_screen *screen,
                                 const struct pan_mod_convert_shader_key *key)
{
   unsigned align = key->afbc.align;
   struct panfrost_device *dev = pan_device(&screen->base);

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, pan_get_nir_shader_compiler_options(dev->arch),
      "panfrost_afbc_size(uncompressed_size=%u, align=%u)",
      key->afbc.uncompressed_size, align);

   panfrost_afbc_add_info_ubo(size, b);

   nir_def *coord = nir_load_global_invocation_id(&b, 32);
   nir_def *block_idx = nir_channel(&b, coord, 0);
   nir_def *src = panfrost_afbc_size_get_info_field(&b, src);
   nir_def *layout = panfrost_afbc_size_get_info_field(&b, layout);
   nir_def *uncompressed_size = nir_imm_int(&b, key->afbc.uncompressed_size);

   nir_def *hdr = read_afbc_header(&b, src, block_idx);
   nir_def *size = get_superblock_size(&b, dev->arch, hdr, uncompressed_size);
   size = nir_iand(&b, nir_iadd(&b, size, nir_imm_int(&b, align - 1)),
                   nir_inot(&b, nir_imm_int(&b, align - 1)));

   nir_def *offset = nir_u2u64(
      &b,
      nir_iadd(&b,
               nir_imul_imm(&b, block_idx, sizeof(struct pan_afbc_payload_extent)),
               nir_imm_int(&b, offsetof(struct pan_afbc_payload_extent, size))));
   nir_store_global(&b, size, nir_iadd(&b, layout, offset));

   return b.shader;
}

#define panfrost_afbc_pack_get_info_field(b, field)                            \
   panfrost_afbc_get_info_field(pack, b, field)

static nir_shader *
panfrost_create_afbc_pack_shader(struct panfrost_screen *screen,
                                 const struct pan_mod_convert_shader_key *key)
{
   unsigned align = key->afbc.align;
   struct panfrost_device *dev = pan_device(&screen->base);
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, pan_get_nir_shader_compiler_options(dev->arch),
      "panfrost_afbc_pack");

   panfrost_afbc_add_info_ubo(pack, b);

   nir_def *coord = nir_load_global_invocation_id(&b, 32);
   nir_def *idx = nir_channel(&b, coord, 0);
   nir_def *src = panfrost_afbc_pack_get_info_field(&b, src);
   nir_def *dst = panfrost_afbc_pack_get_info_field(&b, dst);
   nir_def *header_size =
      nir_u2u64(&b, panfrost_afbc_pack_get_info_field(&b, header_size));
   nir_def *layout = panfrost_afbc_pack_get_info_field(&b, layout);

   copy_superblock(&b, dst, header_size, src, layout, idx, align);

   return b.shader;
}

#define panfrost_mtk_detile_get_info_field(b, field)                            \
   panfrost_mtk_get_info_field(detile, b, field)

static void
copy_y_uv_texel(nir_builder *b, unsigned src_img, nir_def *src_coords,
                unsigned dst_img, nir_def *dst_coords)
{
   nir_def *sample = nir_imm_int(b, 0);
   nir_def *lod = nir_imm_int(b, 0);

   nir_def *val = nir_image_load(
      b, 4, 32, nir_imm_int(b, src_img), src_coords, sample, lod,
      .access = ACCESS_NON_WRITEABLE, .image_dim = GLSL_SAMPLER_DIM_2D,
      .image_array = false, .dest_type = nir_type_uint32);
   nir_image_store(b, nir_imm_int(b, dst_img), dst_coords, sample, val, lod,
                   .access = ACCESS_NON_READABLE,
                   .image_dim = GLSL_SAMPLER_DIM_2D, .image_array = false,
                   .src_type = nir_type_uint32);
}

static nir_shader *
panfrost_create_mtk_tiled_detile_shader(
   struct panfrost_screen *screen, const struct pan_mod_convert_shader_key *key)
{
   const struct panfrost_device *device = &screen->dev;
   bool tint_yuv = (device->debug & PAN_DBG_YUV) != 0;
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, pan_get_nir_shader_compiler_options(device->arch),
      "panfrost_mtk_detile");
   b.shader->info.workgroup_size[0] = 4;
   b.shader->info.workgroup_size[1] = 16;
   b.shader->info.workgroup_size[2] = 1;
   nir_def *intra_tile_coords = nir_trim_vector(&b, nir_load_local_invocation_id(&b), 2);
   nir_def *wg_id = nir_trim_vector(&b, nir_load_workgroup_id(&b), 2);
   nir_def *uv_linear_coords = nir_pad_vector_imm_int(
      &b,
      nir_iadd(&b, nir_imul(&b, wg_id, nir_imm_ivec2(&b, 4, 16)),
               intra_tile_coords),
      0, 4);
   nir_def *y_linear_coords =
      nir_ishl(&b, uv_linear_coords, nir_imm_ivec4(&b, 0, 1, 0, 0));
   nir_def *src_y_row_stride_tl =
      panfrost_mtk_detile_get_info_field(&b, src_y_row_stride_tl);
   nir_def *src_uv_row_stride_tl =
      panfrost_mtk_detile_get_info_field(&b, src_uv_row_stride_tl);
   nir_def *dst_extent =
      nir_vec2(&b, panfrost_mtk_detile_get_info_field(&b, width),
               panfrost_mtk_detile_get_info_field(&b, height));
   nir_def *uv_tiled_coords = nir_vec4(
      &b,
      nir_iadd(&b, nir_imul_imm(&b, nir_channel(&b, intra_tile_coords, 1), 4),
               nir_channel(&b, intra_tile_coords, 0)),
      nir_iadd(&b, nir_imul(&b, nir_channel(&b, wg_id, 1), src_uv_row_stride_tl),
               nir_channel(&b, wg_id, 0)),
      nir_imm_int(&b, 0), nir_imm_int(&b, 0));
   nir_def *y_tiled_coords = nir_vec4(
      &b,
      nir_iadd(&b, nir_imul_imm(&b, nir_channel(&b, intra_tile_coords, 1), 8),
               nir_channel(&b, intra_tile_coords, 0)),
      nir_iadd(&b, nir_imul(&b, nir_channel(&b, wg_id, 1), src_y_row_stride_tl),
               nir_channel(&b, wg_id, 0)),
      nir_imm_int(&b, 0), nir_imm_int(&b, 0));

   const struct glsl_type *image_type =
      glsl_image_type(GLSL_SAMPLER_DIM_2D, /*is_array*/ false, GLSL_TYPE_UINT);

   panfrost_mtk_add_info_ubo(detile, b);

   nir_variable *y_tiled =
      nir_variable_create(b.shader, nir_var_image, image_type, "y_tiled");
   y_tiled->data.binding = 0;
   y_tiled->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 0);
   nir_variable *uv_tiled =
      nir_variable_create(b.shader, nir_var_image, image_type, "uv_tiled");
   uv_tiled->data.binding = 1;
   uv_tiled->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 1);

   nir_variable *y_linear =
      nir_variable_create(b.shader, nir_var_image, image_type, "y_linear");
   y_linear->data.binding = 2;
   y_linear->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 2);

   nir_variable *uv_linear =
      nir_variable_create(b.shader, nir_var_image, image_type, "uv_linear");
   uv_linear->data.binding = 3;
   uv_linear->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 3);

   nir_def *in_bounds = nir_ball(&b, nir_ilt(&b, y_linear_coords, dst_extent));
   nir_push_if(&b, in_bounds);
   {
      if (key->mtk_tiled.has_y)
         copy_y_uv_texel(&b, 0, y_tiled_coords, 2, y_linear_coords);

      if (key->mtk_tiled.has_uv) {
         if (!tint_yuv) {
            copy_y_uv_texel(&b, 1, uv_tiled_coords, 3, uv_linear_coords);
         } else {
            /* use just blue for chroma */
            nir_def *val = nir_imm_ivec4(&b, 0xc0, 0x80, 0xc0, 0x80);
            nir_def *sample = nir_imm_int(&b, 0);
            nir_def *lod = nir_imm_int(&b, 0);

            nir_image_store(&b, nir_imm_int(&b, 3), uv_linear_coords, sample,
                            val, lod, .access = ACCESS_NON_READABLE,
                            .image_dim = GLSL_SAMPLER_DIM_2D,
                            .image_array = false, .src_type = nir_type_uint32);
         }
      }

      /* Next line of Y components, UV is vertically subsampled. */
      if (key->mtk_tiled.has_y) {
         y_linear_coords =
            nir_iadd(&b, y_linear_coords, nir_imm_ivec2(&b, 0, 1));
         y_tiled_coords = nir_iadd(&b, y_tiled_coords, nir_imm_ivec2(&b, 4, 0));
         copy_y_uv_texel(&b, 0, y_tiled_coords, 2, y_linear_coords);
      }
   }
   nir_pop_if(&b, NULL);

   return b.shader;
}

static struct pan_mod_convert_shader_data *
get_mod_convert_shaders(struct panfrost_context *ctx,
                        const struct pan_mod_convert_shader_key *key)
{
   struct pipe_context *pctx = &ctx->base;
   struct panfrost_screen *screen = pan_screen(ctx->base.screen);

   pthread_mutex_lock(&ctx->mod_convert_shaders.lock);
   struct hash_entry *he =
      _mesa_hash_table_search(ctx->mod_convert_shaders.shaders, key);
   struct pan_mod_convert_shader_data *shader = he ? he->data : NULL;
   pthread_mutex_unlock(&ctx->mod_convert_shaders.lock);

   if (shader)
      return shader;

   shader = rzalloc(ctx->mod_convert_shaders.shaders, struct pan_mod_convert_shader_data);
   shader->key = *key;

#define COMPILE_SHADER(type, name, key)                                        \
   {                                                                           \
      nir_shader *nir = panfrost_create_##type##_##name##_shader(screen, key); \
      nir->info.num_ubos = 1;                                                  \
      /* "default" UBO is maybe not correct here, but in panfrost we're */     \
      /* using this as an indicator for whether UBO0 is a user UBO */          \
      nir->info.first_ubo_is_default_ubo = true;                               \
      shader->type.name##_cso = pipe_shader_from_nir(pctx, nir);               \
   }

   if (drm_is_afbc(key->mod)) {
      if (pan_screen(ctx->base.screen)->afbcp_gpu_payload_sizes)
         COMPILE_SHADER(afbc, size, key);
      COMPILE_SHADER(afbc, pack, key);
   } else if (drm_is_mtk_tiled(key->mod)) {
      COMPILE_SHADER(mtk_tiled, detile, key);
   } else {
      UNREACHABLE("Unsupported conversion");
   }

#undef COMPILE_SHADER

   pthread_mutex_lock(&ctx->mod_convert_shaders.lock);
   _mesa_hash_table_insert(ctx->mod_convert_shaders.shaders, &shader->key, shader);
   pthread_mutex_unlock(&ctx->mod_convert_shaders.lock);

   return shader;
}

struct pan_mod_convert_shader_data *
panfrost_get_afbc_pack_shaders(struct panfrost_context *ctx,
                               struct panfrost_resource *rsrc, unsigned align)
{
   struct pan_mod_convert_shader_key key = {
      .mod = DRM_FORMAT_MOD_ARM_AFBC(0),
      .afbc = {
         .uncompressed_size = pan_afbc_payload_uncompressed_size(
            rsrc->base.format, rsrc->modifier),
         .align = align,
      },
   };

   return get_mod_convert_shaders(ctx, &key);
}

struct pan_mod_convert_shader_data *
panfrost_get_mtk_detile_shader(struct panfrost_context *ctx, bool has_y,
                               bool has_uv)
{
   struct pan_mod_convert_shader_key key = {
      .mod = DRM_FORMAT_MOD_MTK_16L_32S_TILE,
      .mtk_tiled = {
         .has_y = has_y,
         .has_uv = has_uv,
      },
   };

   return get_mod_convert_shaders(ctx, &key);
}

DERIVE_HASH_TABLE(pan_mod_convert_shader_key);

void
panfrost_afbc_context_init(struct panfrost_context *ctx)
{
   ctx->mod_convert_shaders.shaders = pan_mod_convert_shader_key_table_create(NULL);
   pthread_mutex_init(&ctx->mod_convert_shaders.lock, NULL);
}

void
panfrost_afbc_context_destroy(struct panfrost_context *ctx)
{
   hash_table_foreach(ctx->mod_convert_shaders.shaders, he) {
      assert(he->data);
      struct pan_mod_convert_shader_data *shader = he->data;

      if (drm_is_afbc(shader->key.mod)) {
         if (pan_screen(ctx->base.screen)->afbcp_gpu_payload_sizes)
            ctx->base.delete_compute_state(&ctx->base, shader->afbc.size_cso);
         ctx->base.delete_compute_state(&ctx->base, shader->afbc.pack_cso);
      } else if (drm_is_mtk_tiled(shader->key.mod)) {
         ctx->base.delete_compute_state(&ctx->base, shader->mtk_tiled.detile_cso);
      }
   }

   _mesa_hash_table_destroy(ctx->mod_convert_shaders.shaders, NULL);
   pthread_mutex_destroy(&ctx->mod_convert_shaders.lock);
}
