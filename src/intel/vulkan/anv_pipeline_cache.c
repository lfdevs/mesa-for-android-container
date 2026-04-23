/*
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "util/blob.h"
#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/disk_cache.h"
#include "util/mesa-blake3.h"
#include "nir/nir_serialize.h"
#include "nir/nir.h"
#include "anv_private.h"
#include "anv_shader.h"
#include "nir/nir_xfb_info.h"
#include "vk_util.h"
#include "compiler/spirv/nir_spirv.h"
#include "shaders/float64_spv.h"
#include "util/u_printf.h"


static bool
anv_shader_internal_serialize(struct vk_pipeline_cache_object *object,
                              struct blob *blob);

struct vk_pipeline_cache_object *
anv_shader_internal_deserialize(struct vk_pipeline_cache *cache,
                                const void *key_data, size_t key_size,
                                struct blob_reader *blob);

static void
anv_shader_internal_destroy(struct vk_device *_device,
                            struct vk_pipeline_cache_object *object)
{
   struct anv_device *device =
      container_of(_device, struct anv_device, vk);

   struct anv_shader_internal *shader =
      container_of(object, struct anv_shader_internal, base);

   anv_shader_heap_free(&device->shader_heap, shader->kernel);
   vk_pipeline_cache_object_finish(&shader->base);
   vk_free(&device->vk.alloc, shader);
}

static const struct vk_pipeline_cache_object_ops anv_shader_internal_ops = {
   .serialize = anv_shader_internal_serialize,
   .deserialize = anv_shader_internal_deserialize,
   .destroy = anv_shader_internal_destroy,
};

const struct vk_pipeline_cache_object_ops *const anv_cache_import_ops[2] = {
   &anv_shader_internal_ops,
   NULL
};

static struct anv_shader_internal *
anv_shader_internal_create(struct anv_device *device,
                           mesa_shader_stage stage,
                           const void *key_data, uint32_t key_size,
                           const void *kernel_data, uint32_t kernel_size,
                           const struct brw_stage_prog_data *prog_data_in,
                           uint32_t prog_data_size,
                           const struct genisa_stats *stats, uint32_t num_stats)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct anv_shader_internal, shader, 1);
   VK_MULTIALLOC_DECL_SIZE(&ma, void, obj_key_data, key_size);
   VK_MULTIALLOC_DECL_SIZE(&ma, struct brw_stage_prog_data, prog_data,
                                prog_data_size);
   VK_MULTIALLOC_DECL(&ma, struct intel_shader_reloc, prog_data_relocs,
                           prog_data_in->num_relocs);
   VK_MULTIALLOC_DECL(&ma, void, code, kernel_size);

   if (!vk_multialloc_zalloc(&ma, &device->vk.alloc,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   memcpy(obj_key_data, key_data, key_size);
   vk_pipeline_cache_object_init(&device->vk, &shader->base,
                                 &anv_shader_internal_ops, obj_key_data, key_size);

   shader->stage = stage;
   shader->kernel_size = kernel_size;

   memcpy(prog_data, prog_data_in, prog_data_size);
   typed_memcpy(prog_data_relocs, prog_data_in->relocs,
                prog_data_in->num_relocs);
   prog_data->relocs = prog_data_relocs;
   shader->prog_data = prog_data;
   shader->prog_data_size = prog_data_size;

   assert(num_stats <= ARRAY_SIZE(shader->stats));
   assert((stats != NULL) || (num_stats == 0));
   typed_memcpy(shader->stats, stats, num_stats);
   shader->num_stats = num_stats;

   shader->code = code;
   memcpy(shader->code, kernel_data, kernel_size);

   if (anv_needs_printf_buffer()) {
      struct intel_shader_reloc_value reloc_values[3];
      uint32_t rv_count = 0;

      struct anv_bo *bo = device->printf.bo;
      assert(bo != NULL);

      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_LOW,
         .value = bo->offset & 0xffffffff,
      };
      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_HIGH,
         .value = bo->offset >> 32,
      };
      reloc_values[rv_count++] = (struct intel_shader_reloc_value) {
         .id = BRW_SHADER_RELOC_PRINTF_BUFFER_SIZE,
         .value = anv_printf_buffer_size(),
      };

      brw_write_shader_relocs(&device->physical->compiler->isa,
                              shader->code, shader->prog_data,
                              reloc_values, rv_count);
   }

   shader->kernel = anv_shader_heap_alloc(&device->shader_heap,
                                          kernel_size, 64, false, 0);
   if (shader->kernel.alloc_size == 0) {
      vk_pipeline_cache_object_finish(&shader->base);
      vk_free(&device->vk.alloc, shader);
      return NULL;
   }

   anv_shader_heap_upload(&device->shader_heap,
                          shader->kernel,
                          shader->code,
                          shader->prog_data,
                          shader->stats->dispatch_width);

   return shader;
}

static bool
anv_shader_internal_serialize(struct vk_pipeline_cache_object *object,
                              struct blob *blob)
{
   struct anv_shader_internal *shader =
      container_of(object, struct anv_shader_internal, base);

   blob_write_uint32(blob, shader->stage);

   blob_write_uint32(blob, shader->kernel_size);
   blob_write_bytes(blob, shader->code, shader->kernel_size);

   blob_write_uint32(blob, shader->prog_data_size);

   union brw_any_prog_data prog_data;
   assert(shader->prog_data_size <= sizeof(prog_data));
   memcpy(&prog_data, shader->prog_data, shader->prog_data_size);
   prog_data.base.relocs = NULL;
   blob_write_bytes(blob, &prog_data, shader->prog_data_size);

   blob_write_bytes(blob, shader->prog_data->relocs,
                    shader->prog_data->num_relocs *
                    sizeof(shader->prog_data->relocs[0]));

   blob_write_uint32(blob, shader->num_stats);
   blob_write_bytes(blob, shader->stats,
                    shader->num_stats * sizeof(shader->stats[0]));

   return !blob->out_of_memory;
}

struct vk_pipeline_cache_object *
anv_shader_internal_deserialize(struct vk_pipeline_cache *cache,
                                const void *key_data, size_t key_size,
                                struct blob_reader *blob)
{
   struct anv_device *device =
      container_of(cache->base.device, struct anv_device, vk);

   mesa_shader_stage stage = blob_read_uint32(blob);

   uint32_t kernel_size = blob_read_uint32(blob);
   const void *kernel_data = blob_read_bytes(blob, kernel_size);

   uint32_t prog_data_size = blob_read_uint32(blob);
   const void *prog_data_bytes = blob_read_bytes(blob, prog_data_size);
   if (blob->overrun)
      return NULL;

   union brw_any_prog_data prog_data;
   memcpy(&prog_data, prog_data_bytes,
          MIN2(sizeof(prog_data), prog_data_size));
   prog_data.base.relocs =
      blob_read_bytes(blob, prog_data.base.num_relocs *
                            sizeof(prog_data.base.relocs[0]));

   void *mem_ctx = ralloc_context(NULL);
   uint32_t num_stats = blob_read_uint32(blob);
   const struct genisa_stats *stats =
      blob_read_bytes(blob, num_stats * sizeof(stats[0]));

   if (blob->overrun) {
      ralloc_free(mem_ctx);
      return NULL;
   }

   struct anv_shader_internal *shader =
      anv_shader_internal_create(device, stage,
                                 key_data, key_size,
                                 kernel_data, kernel_size,
                                 &prog_data.base, prog_data_size,
                                 stats, num_stats);

   ralloc_free(mem_ctx);

   if (shader == NULL)
      return NULL;

   return &shader->base;
}

struct anv_shader_internal *
anv_device_search_for_kernel(struct anv_device *device,
                             struct vk_pipeline_cache *cache,
                             const void *key_data, uint32_t key_size,
                             bool *user_cache_hit)
{
   /* Use the default pipeline cache if none is specified */
   if (cache == NULL)
      cache = device->vk.mem_cache;

   bool cache_hit = false;
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_lookup_object(cache, key_data, key_size,
                                      &anv_shader_internal_ops, &cache_hit);
   if (user_cache_hit != NULL) {
      *user_cache_hit = object != NULL && cache_hit &&
                        cache != device->vk.mem_cache;
   }

   if (object == NULL)
      return NULL;

   return container_of(object, struct anv_shader_internal, base);
}

struct anv_shader_internal *
anv_device_upload_kernel(struct anv_device *device,
                         struct vk_pipeline_cache *cache,
                         const struct anv_shader_upload_params *params)
{
   /* Use the default pipeline cache if none is specified */
   if (cache == NULL)
      cache = device->vk.mem_cache;

   struct anv_shader_internal *shader =
      anv_shader_internal_create(device,
                                 params->stage,
                                 params->key_data,
                                 params->key_size,
                                 params->kernel_data,
                                 params->kernel_size,
                                 params->prog_data,
                                 params->prog_data_size,
                                 params->stats,
                                 params->num_stats);
   if (shader == NULL)
      return NULL;

   struct vk_pipeline_cache_object *cached =
      vk_pipeline_cache_add_object(cache, &shader->base);

   return container_of(cached, struct anv_shader_internal, base);
}

struct nir_shader *
anv_device_search_for_nir(struct anv_device *device,
                          struct vk_pipeline_cache *cache,
                          const nir_shader_compiler_options *nir_options,
                          unsigned char blake3_key[BLAKE3_KEY_LEN],
                          void *mem_ctx)
{
   if (cache == NULL)
      cache = device->vk.mem_cache;

   return vk_pipeline_cache_lookup_nir(cache, blake3_key, BLAKE3_KEY_LEN,
                                       nir_options, NULL, mem_ctx);
}

void
anv_device_upload_nir(struct anv_device *device,
                      struct vk_pipeline_cache *cache,
                      const struct nir_shader *nir,
                      unsigned char blake3_key[BLAKE3_KEY_LEN])
{
   if (cache == NULL)
      cache = device->vk.mem_cache;

   vk_pipeline_cache_add_nir(cache, blake3_key, BLAKE3_KEY_LEN, nir);
}

void
anv_load_fp64_shader(struct anv_device *device)
{
   const nir_shader_compiler_options *nir_options =
      &device->physical->compiler->nir_options[MESA_SHADER_VERTEX];

   const char* shader_name = "float64_spv_lib";
   blake3_hasher blake3_ctx;
   uint8_t blake3[BLAKE3_KEY_LEN];
   _mesa_blake3_init(&blake3_ctx);
   _mesa_blake3_update(&blake3_ctx, shader_name, strlen(shader_name));
   _mesa_blake3_final(&blake3_ctx, blake3);

   device->fp64_nir =
      anv_device_search_for_nir(device, device->internal_cache,
                                   nir_options, blake3, NULL);

   /* The shader found, no need to call spirv_to_nir() again. */
   if (device->fp64_nir)
      return;

   const struct spirv_capabilities spirv_caps = {
      .Addresses = true,
      .Float64 = true,
      .Int8 = true,
      .Int16 = true,
      .Int64 = true,
      .Shader = true,
   };

   struct spirv_to_nir_options spirv_options = {
      .capabilities = &spirv_caps,
      .environment = NIR_SPIRV_VULKAN,
      .create_library = true
   };

   nir_shader* nir =
      spirv_to_nir(float64_spv_source, sizeof(float64_spv_source) / 4,
                   NULL, MESA_SHADER_VERTEX, "main",
                   &spirv_options, nir_options);

   assert(nir != NULL);

   nir_validate_shader(nir, "after spirv_to_nir");

   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);

   anv_device_upload_nir(device, device->internal_cache,
                         nir, blake3);

   device->fp64_nir = nir;
}
