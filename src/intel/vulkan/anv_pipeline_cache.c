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
#include "util/mesa-sha1.h"
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

   for (uint32_t i = 0; i < shader->bind_map.embedded_sampler_count; i++)
      anv_embedded_sampler_unref(device, shader->embedded_samplers[i]);

   ANV_DMR_SP_FREE(&device->vk.base, &device->instruction_state_pool, shader->kernel);
   anv_state_pool_free(&device->instruction_state_pool, shader->kernel);
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
                           const struct genisa_stats *stats, uint32_t num_stats,
                           const nir_xfb_info *xfb_info_in,
                           const struct anv_pipeline_bind_map *bind_map,
                           const struct anv_push_descriptor_info *push_desc_info)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct anv_shader_internal, shader, 1);
   VK_MULTIALLOC_DECL_SIZE(&ma, void, obj_key_data, key_size);
   VK_MULTIALLOC_DECL_SIZE(&ma, struct brw_stage_prog_data, prog_data,
                                prog_data_size);
   VK_MULTIALLOC_DECL(&ma, struct intel_shader_reloc, prog_data_relocs,
                           prog_data_in->num_relocs);
   VK_MULTIALLOC_DECL(&ma, uint32_t, prog_data_param, prog_data_in->nr_params);

   VK_MULTIALLOC_DECL_SIZE(&ma, nir_xfb_info, xfb_info,
                                xfb_info_in == NULL ? 0 :
                                nir_xfb_info_size(xfb_info_in->output_count));

   VK_MULTIALLOC_DECL(&ma, struct anv_pipeline_binding, surface_to_descriptor,
                           bind_map->surface_count);
   VK_MULTIALLOC_DECL(&ma, struct anv_pipeline_binding, sampler_to_descriptor,
                      bind_map->sampler_count);
   VK_MULTIALLOC_DECL(&ma, struct anv_pipeline_embedded_sampler_binding,
                      embedded_sampler_to_binding,
                      bind_map->embedded_sampler_count);
   VK_MULTIALLOC_DECL(&ma, struct anv_embedded_sampler *, embedded_samplers,
                      bind_map->embedded_sampler_count);

   if (!vk_multialloc_zalloc(&ma, &device->vk.alloc,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   memcpy(obj_key_data, key_data, key_size);
   vk_pipeline_cache_object_init(&device->vk, &shader->base,
                                 &anv_shader_internal_ops, obj_key_data, key_size);

   shader->stage = stage;
   if(INTEL_DEBUG(DEBUG_SHOW_SHADER_STAGE))
      fprintf(stderr, "Stage: %s\n", mesa_shader_stage_name(shader->stage));

   shader->kernel =
      anv_state_pool_alloc(&device->instruction_state_pool, kernel_size, 64);
   ANV_DMR_SP_ALLOC(&device->vk.base, &device->instruction_state_pool, shader->kernel);
   memcpy(shader->kernel.map, kernel_data, kernel_size);
   shader->kernel_size = kernel_size;

   if (bind_map->embedded_sampler_count > 0) {
      shader->embedded_samplers = embedded_samplers;
      if (anv_device_get_embedded_samplers(device, embedded_samplers, bind_map) != VK_SUCCESS) {
         ANV_DMR_SP_FREE(&device->vk.base, &device->instruction_state_pool, shader->kernel);
         anv_state_pool_free(&device->instruction_state_pool, shader->kernel);
         vk_free(&device->vk.alloc, shader);
         return NULL;
      }
   }

   memcpy(prog_data, prog_data_in, prog_data_size);
   typed_memcpy(prog_data_relocs, prog_data_in->relocs,
                prog_data_in->num_relocs);
   prog_data->relocs = prog_data_relocs;
   prog_data->param = prog_data_param;
   shader->prog_data = prog_data;
   shader->prog_data_size = prog_data_size;

   assert(num_stats <= ARRAY_SIZE(shader->stats));
   assert((stats != NULL) || (num_stats == 0));
   typed_memcpy(shader->stats, stats, num_stats);
   shader->num_stats = num_stats;

   if (xfb_info_in) {
      *xfb_info = *xfb_info_in;
      typed_memcpy(xfb_info->outputs, xfb_info_in->outputs,
                   xfb_info_in->output_count);
      shader->xfb_info = xfb_info;
   } else {
      shader->xfb_info = NULL;
   }

   typed_memcpy(&shader->push_desc_info, push_desc_info, 1);

   shader->bind_map = *bind_map;

   typed_memcpy(surface_to_descriptor, bind_map->surface_to_descriptor,
                bind_map->surface_count);
   shader->bind_map.surface_to_descriptor = surface_to_descriptor;

   typed_memcpy(sampler_to_descriptor, bind_map->sampler_to_descriptor,
                bind_map->sampler_count);
   shader->bind_map.sampler_to_descriptor = sampler_to_descriptor;

   typed_memcpy(embedded_sampler_to_binding, bind_map->embedded_sampler_to_binding,
                bind_map->embedded_sampler_count);
   shader->bind_map.embedded_sampler_to_binding = embedded_sampler_to_binding;

   typed_memcpy(shader->bind_map.input_attachments,
                bind_map->input_attachments,
                ARRAY_SIZE(bind_map->input_attachments));

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
   blob_write_bytes(blob, shader->kernel.map, shader->kernel_size);

   blob_write_uint32(blob, shader->prog_data_size);

   union brw_any_prog_data prog_data;
   assert(shader->prog_data_size <= sizeof(prog_data));
   memcpy(&prog_data, shader->prog_data, shader->prog_data_size);
   prog_data.base.relocs = NULL;
   prog_data.base.param = NULL;
   blob_write_bytes(blob, &prog_data, shader->prog_data_size);

   blob_write_bytes(blob, shader->prog_data->relocs,
                    shader->prog_data->num_relocs *
                    sizeof(shader->prog_data->relocs[0]));

   blob_write_uint32(blob, shader->num_stats);
   blob_write_bytes(blob, shader->stats,
                    shader->num_stats * sizeof(shader->stats[0]));

   if (shader->xfb_info) {
      uint32_t xfb_info_size =
         nir_xfb_info_size(shader->xfb_info->output_count);
      blob_write_uint32(blob, xfb_info_size);
      blob_write_bytes(blob, shader->xfb_info, xfb_info_size);
   } else {
      blob_write_uint32(blob, 0);
   }

   blob_write_uint32(blob, shader->push_desc_info.used_descriptors);
   blob_write_uint32(blob, shader->push_desc_info.fully_promoted_ubo_descriptors);
   blob_write_uint8(blob, shader->push_desc_info.push_set_buffer);

   blob_write_bytes(blob, shader->bind_map.surface_sha1,
                    sizeof(shader->bind_map.surface_sha1));
   blob_write_bytes(blob, shader->bind_map.sampler_sha1,
                    sizeof(shader->bind_map.sampler_sha1));
   blob_write_bytes(blob, shader->bind_map.push_sha1,
                    sizeof(shader->bind_map.push_sha1));
   blob_write_uint32(blob, shader->bind_map.layout_type);
   blob_write_uint32(blob, shader->bind_map.surface_count);
   blob_write_uint32(blob, shader->bind_map.sampler_count);
   blob_write_uint32(blob, shader->bind_map.embedded_sampler_count);
   blob_write_bytes(blob, shader->bind_map.surface_to_descriptor,
                    shader->bind_map.surface_count *
                    sizeof(*shader->bind_map.surface_to_descriptor));
   blob_write_bytes(blob, shader->bind_map.sampler_to_descriptor,
                    shader->bind_map.sampler_count *
                    sizeof(*shader->bind_map.sampler_to_descriptor));
   blob_write_bytes(blob, shader->bind_map.embedded_sampler_to_binding,
                    shader->bind_map.embedded_sampler_count *
                    sizeof(*shader->bind_map.embedded_sampler_to_binding));
   blob_write_bytes(blob, shader->bind_map.input_attachments,
                    sizeof(shader->bind_map.input_attachments));
   blob_write_bytes(blob, shader->bind_map.push_ranges,
                    sizeof(shader->bind_map.push_ranges));

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

   const nir_xfb_info *xfb_info = NULL;
   uint32_t xfb_size = blob_read_uint32(blob);
   if (xfb_size)
      xfb_info = blob_read_bytes(blob, xfb_size);

   struct anv_push_descriptor_info push_desc_info = {};
   push_desc_info.used_descriptors = blob_read_uint32(blob);
   push_desc_info.fully_promoted_ubo_descriptors = blob_read_uint32(blob);
   push_desc_info.push_set_buffer = blob_read_uint8(blob);

   struct anv_pipeline_bind_map bind_map = {};
   blob_copy_bytes(blob, bind_map.surface_sha1, sizeof(bind_map.surface_sha1));
   blob_copy_bytes(blob, bind_map.sampler_sha1, sizeof(bind_map.sampler_sha1));
   blob_copy_bytes(blob, bind_map.push_sha1, sizeof(bind_map.push_sha1));
   bind_map.layout_type = blob_read_uint32(blob);
   bind_map.surface_count = blob_read_uint32(blob);
   bind_map.sampler_count = blob_read_uint32(blob);
   bind_map.embedded_sampler_count = blob_read_uint32(blob);
   bind_map.surface_to_descriptor = (void *)
      blob_read_bytes(blob, bind_map.surface_count *
                            sizeof(*bind_map.surface_to_descriptor));
   bind_map.sampler_to_descriptor = (void *)
      blob_read_bytes(blob, bind_map.sampler_count *
                            sizeof(*bind_map.sampler_to_descriptor));
   bind_map.embedded_sampler_to_binding = (void *)
      blob_read_bytes(blob, bind_map.embedded_sampler_count *
                            sizeof(*bind_map.embedded_sampler_to_binding));
   blob_copy_bytes(blob, bind_map.input_attachments,
                   sizeof(bind_map.input_attachments));
   blob_copy_bytes(blob, bind_map.push_ranges, sizeof(bind_map.push_ranges));

   if (blob->overrun) {
      ralloc_free(mem_ctx);
      return NULL;
   }

   struct anv_shader_internal *shader =
      anv_shader_internal_create(device, stage,
                                 key_data, key_size,
                                 kernel_data, kernel_size,
                                 &prog_data.base, prog_data_size,
                                 stats, num_stats, xfb_info, &bind_map,
                                 &push_desc_info);

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
                                 params->num_stats,
                                 params->xfb_info,
                                 params->bind_map,
                                 params->push_desc_info);
   if (shader == NULL)
      return NULL;

   struct vk_pipeline_cache_object *cached =
      vk_pipeline_cache_add_object(cache, &shader->base);

   return container_of(cached, struct anv_shader_internal, base);
}

#define SHA1_KEY_SIZE 20

struct nir_shader *
anv_device_search_for_nir(struct anv_device *device,
                          struct vk_pipeline_cache *cache,
                          const nir_shader_compiler_options *nir_options,
                          unsigned char sha1_key[SHA1_KEY_SIZE],
                          void *mem_ctx)
{
   if (cache == NULL)
      cache = device->vk.mem_cache;

   return vk_pipeline_cache_lookup_nir(cache, sha1_key, SHA1_KEY_SIZE,
                                       nir_options, NULL, mem_ctx);
}

void
anv_device_upload_nir(struct anv_device *device,
                      struct vk_pipeline_cache *cache,
                      const struct nir_shader *nir,
                      unsigned char sha1_key[SHA1_KEY_SIZE])
{
   if (cache == NULL)
      cache = device->vk.mem_cache;

   vk_pipeline_cache_add_nir(cache, sha1_key, SHA1_KEY_SIZE, nir);
}

void
anv_load_fp64_shader(struct anv_device *device)
{
   const nir_shader_compiler_options *nir_options =
      &device->physical->compiler->nir_options[MESA_SHADER_VERTEX];

   const char* shader_name = "float64_spv_lib";
   struct mesa_sha1 sha1_ctx;
   uint8_t sha1[20];
   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, shader_name, strlen(shader_name));
   _mesa_sha1_final(&sha1_ctx, sha1);

   device->fp64_nir =
      anv_device_search_for_nir(device, device->internal_cache,
                                   nir_options, sha1, NULL);

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
                   NULL, 0, MESA_SHADER_VERTEX, "main",
                   &spirv_options, nir_options);

   assert(nir != NULL);

   nir_validate_shader(nir, "after spirv_to_nir");

   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);

   anv_device_upload_nir(device, device->internal_cache,
                         nir, sha1);

   device->fp64_nir = nir;
}
