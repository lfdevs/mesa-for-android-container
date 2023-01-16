/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_meta.h"
#include "agx_compile.h"
#include "agx_device.h" /* for AGX_MEMORY_TYPE_SHADER */
#include "agx_tilebuffer.h"
#include "nir_builder.h"

static struct agx_meta_shader *
agx_compile_meta_shader(struct agx_meta_cache *cache, nir_shader *shader,
                        struct agx_shader_key *key,
                        struct agx_tilebuffer_layout *tib)
{
   struct util_dynarray binary;
   util_dynarray_init(&binary, NULL);

   agx_preprocess_nir(shader);
   if (tib)
      agx_nir_lower_tilebuffer(shader, tib);

   struct agx_meta_shader *res = rzalloc(cache->ht, struct agx_meta_shader);
   agx_compile_shader_nir(shader, key, NULL, &binary, &res->info);

   res->ptr = agx_pool_upload_aligned_with_bo(&cache->pool, binary.data,
                                              binary.size, 128, &res->bo);
   util_dynarray_fini(&binary);

   return res;
}

static nir_ssa_def *
build_background_op(nir_builder *b, enum agx_meta_op op, unsigned rt,
                    unsigned nr, bool msaa)
{
   if (op == AGX_META_OP_LOAD) {
      nir_ssa_def *fragcoord = nir_load_frag_coord(b);
      nir_ssa_def *coord = nir_channels(b, fragcoord, 0x3);

      nir_tex_instr *tex = nir_tex_instr_create(b->shader, msaa ? 2 : 1);
      /* The type doesn't matter as long as it matches the variable type */
      tex->dest_type = nir_type_uint32;
      tex->sampler_dim = msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;
      tex->op = nir_texop_tex;
      tex->src[0].src_type = nir_tex_src_coord;
      tex->src[0].src = nir_src_for_ssa(coord);

      if (msaa) {
         tex->src[1].src_type = nir_tex_src_ms_index;
         tex->src[1].src = nir_src_for_ssa(nir_load_sample_id(b));
      }

      tex->coord_components = 2;
      tex->texture_index = rt;
      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);
      nir_builder_instr_insert(b, &tex->instr);

      return nir_trim_vector(b, &tex->dest.ssa, nr);
   } else {
      assert(op == AGX_META_OP_CLEAR);

      nir_ssa_def *comp[] = {
         nir_load_preamble(b, 1, 32, (rt * 8) + 0),
         nir_load_preamble(b, 1, 32, (rt * 8) + 2),
         nir_load_preamble(b, 1, 32, (rt * 8) + 4),
         nir_load_preamble(b, 1, 32, (rt * 8) + 6),
      };

      return nir_vec(b, comp, nr);
   }
}

static struct agx_meta_shader *
agx_build_background_shader(struct agx_meta_cache *cache,
                            struct agx_meta_key *key)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "agx_background");
   b.shader->info.fs.untyped_color_outputs = true;

   struct agx_shader_key compiler_key = {
      .fs.ignore_tib_dependencies = true,
   };

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_META_OP_NONE)
         continue;

      unsigned nr = util_format_get_nr_components(key->tib.logical_format[rt]);
      bool msaa = key->tib.nr_samples > 1;
      assert(nr > 0);

      nir_variable *out =
         nir_variable_create(b.shader, nir_var_shader_out,
                             glsl_vector_type(GLSL_TYPE_UINT, nr), "output");
      out->data.location = FRAG_RESULT_DATA0 + rt;

      nir_store_var(&b, out, build_background_op(&b, key->op[rt], rt, nr, msaa),
                    0xFF);
   }

   return agx_compile_meta_shader(cache, b.shader, &compiler_key, &key->tib);
}

static struct agx_meta_shader *
agx_build_end_of_tile_shader(struct agx_meta_cache *cache,
                             struct agx_meta_key *key)
{
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                                                  &agx_nir_options, "agx_eot");

   enum glsl_sampler_dim dim =
      (key->tib.nr_samples > 1) ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_META_OP_NONE)
         continue;

      assert(key->op[rt] == AGX_META_OP_STORE);
      nir_block_image_store_agx(
         &b, nir_imm_int(&b, rt), nir_imm_intN_t(&b, key->tib.offset_B[rt], 16),
         .format = agx_tilebuffer_physical_format(&key->tib, rt),
         .image_dim = dim);
   }

   struct agx_shader_key compiler_key = {0};
   return agx_compile_meta_shader(cache, b.shader, &compiler_key, NULL);
}

struct agx_meta_shader *
agx_get_meta_shader(struct agx_meta_cache *cache, struct agx_meta_key *key)
{
   struct hash_entry *ent = _mesa_hash_table_search(cache->ht, key);
   if (ent)
      return ent->data;

   struct agx_meta_shader *ret = NULL;

   for (unsigned rt = 0; rt < ARRAY_SIZE(key->op); ++rt) {
      if (key->op[rt] == AGX_META_OP_STORE) {
         ret = agx_build_end_of_tile_shader(cache, key);
         break;
      }
   }

   if (!ret)
      ret = agx_build_background_shader(cache, key);

   ret->key = *key;
   _mesa_hash_table_insert(cache->ht, &ret->key, ret);
   return ret;
}

static uint32_t
key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct agx_meta_key));
}

static bool
key_compare(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct agx_meta_key)) == 0;
}

void
agx_meta_init(struct agx_meta_cache *cache, struct agx_device *dev,
              void *memctx)
{
   agx_pool_init(&cache->pool, dev, AGX_MEMORY_TYPE_SHADER, true);
   cache->ht = _mesa_hash_table_create(memctx, key_hash, key_compare);
}
