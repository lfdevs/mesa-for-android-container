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

#include "util/u_debug.h"
#include "util/disk_cache.h"
#include "util/macros.h"
#include "util/mesa-sha1.h"
#include "util/u_atomic.h"
#include "vulkan/util/vk_util.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "aco_interface.h"
#include "vk_pipeline.h"

static bool
radv_is_cache_disabled(struct radv_device *device)
{
   /* Pipeline caches can be disabled with RADV_DEBUG=nocache, with MESA_GLSL_CACHE_DISABLE=1 and
    * when ACO_DEBUG is used. MESA_GLSL_CACHE_DISABLE is done elsewhere.
    */
   return (device->instance->debug_flags & RADV_DEBUG_NO_CACHE) ||
          (device->physical_device->use_llvm ? 0 : aco_get_codegen_flags());
}

void
radv_hash_shaders(unsigned char *hash, const struct radv_pipeline_stage *stages,
                  uint32_t stage_count, const struct radv_pipeline_layout *layout,
                  const struct radv_pipeline_key *key, uint32_t flags)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);
   if (key)
      _mesa_sha1_update(&ctx, key, sizeof(*key));
   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   for (unsigned s = 0; s < stage_count; s++) {
      if (!stages[s].entrypoint)
         continue;

      _mesa_sha1_update(&ctx, stages[s].shader_sha1, sizeof(stages[s].shader_sha1));
   }
   _mesa_sha1_update(&ctx, &flags, 4);
   _mesa_sha1_final(&ctx, hash);
}

void
radv_hash_rt_stages(struct mesa_sha1 *ctx, const VkPipelineShaderStageCreateInfo *stages,
                    unsigned stage_count)
{
   for (unsigned i = 0; i < stage_count; ++i) {
      unsigned char hash[20];
      vk_pipeline_hash_shader_stage(&stages[i], NULL, hash);
      _mesa_sha1_update(ctx, hash, sizeof(hash));
   }
}

void
radv_hash_rt_shaders(unsigned char *hash, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                     const struct radv_pipeline_key *key,
                     const struct radv_ray_tracing_module *groups, uint32_t flags)
{
   RADV_FROM_HANDLE(radv_pipeline_layout, layout, pCreateInfo->layout);
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);
   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   _mesa_sha1_update(&ctx, key, sizeof(*key));

   radv_hash_rt_stages(&ctx, pCreateInfo->pStages, pCreateInfo->stageCount);

   for (uint32_t i = 0; i < pCreateInfo->groupCount; i++) {
      _mesa_sha1_update(&ctx, &pCreateInfo->pGroups[i].type,
                        sizeof(pCreateInfo->pGroups[i].type));
      _mesa_sha1_update(&ctx, &pCreateInfo->pGroups[i].generalShader,
                        sizeof(pCreateInfo->pGroups[i].generalShader));
      _mesa_sha1_update(&ctx, &pCreateInfo->pGroups[i].anyHitShader,
                        sizeof(pCreateInfo->pGroups[i].anyHitShader));
      _mesa_sha1_update(&ctx, &pCreateInfo->pGroups[i].closestHitShader,
                        sizeof(pCreateInfo->pGroups[i].closestHitShader));
      _mesa_sha1_update(&ctx, &pCreateInfo->pGroups[i].intersectionShader,
                        sizeof(pCreateInfo->pGroups[i].intersectionShader));
      _mesa_sha1_update(&ctx, &groups[i].handle, sizeof(struct radv_pipeline_group_handle));
   }

   const uint32_t pipeline_flags =
      pCreateInfo->flags & (VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR |
                            VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR |
                            VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR |
                            VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR |
                            VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR |
                            VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_INTERSECTION_SHADERS_BIT_KHR);
   _mesa_sha1_update(&ctx, &pipeline_flags, 4);

   _mesa_sha1_update(&ctx, &flags, 4);
   _mesa_sha1_final(&ctx, hash);
}

static void
radv_shader_destroy(struct vk_device *_device, struct vk_pipeline_cache_object *object)
{
   struct radv_device *device = container_of(_device, struct radv_device, vk);
   struct radv_shader *shader = container_of(object, struct radv_shader, base);

   if (device->shader_use_invisible_vram) {
      /* Wait for any pending upload to complete, or we'll be writing into freed shader memory. */
      radv_shader_wait_for_upload(device, shader->upload_seq);
   }

   radv_free_shader_memory(device, shader->alloc);

   free(shader->code);
   free(shader->spirv);
   free(shader->nir_string);
   free(shader->disasm_string);
   free(shader->ir_string);
   free(shader->statistics);

   vk_pipeline_cache_object_finish(&shader->base);
   free(shader);
}

static struct vk_pipeline_cache_object *
radv_shader_deserialize(struct vk_pipeline_cache *cache, const void *key_data, size_t key_size,
                        struct blob_reader *blob)
{
   struct radv_device *device = container_of(cache->base.device, struct radv_device, vk);
   const struct radv_shader_binary *binary =
      blob_read_bytes(blob, sizeof(struct radv_shader_binary));
   assert(key_size == SHA1_DIGEST_LENGTH);

   struct radv_shader *shader = radv_shader_create(device, binary);
   if (!shader)
      return NULL;

   memcpy(shader->sha1, key_data, key_size);
   blob_skip_bytes(blob, binary->total_size - sizeof(struct radv_shader_binary));

   return &shader->base;
}

static bool
radv_shader_serialize(struct vk_pipeline_cache_object *object, struct blob *blob)
{
   struct radv_shader *shader = container_of(object, struct radv_shader, base);
   size_t stats_size = shader->statistics ? aco_num_statistics * sizeof(uint32_t) : 0;
   size_t code_size = shader->code_size - 5 /* DEBUGGER_NUM_MARKERS */ * 4;
   uint32_t total_size = sizeof(struct radv_shader_binary_legacy) + code_size + stats_size;

   struct radv_shader_binary_legacy binary = {
      .base =
         {
            .type = RADV_BINARY_TYPE_LEGACY,
            .config = shader->config,
            .info = shader->info,
            .total_size = total_size,
         },
      .code_size = code_size,
      .exec_size = shader->exec_size,
      .ir_size = 0,
      .disasm_size = 0,
      .stats_size = stats_size,
   };

   blob_write_bytes(blob, &binary, sizeof(struct radv_shader_binary_legacy));
   blob_write_bytes(blob, shader->statistics, stats_size);
   blob_write_bytes(blob, shader->code, code_size);

   return true;
}

struct radv_shader *
radv_shader_create_cached(struct radv_device *device, struct vk_pipeline_cache *cache,
                          const struct radv_shader_binary *binary)
{
   if (radv_is_cache_disabled(device))
      return radv_shader_create(device, binary);

   if (!cache)
      cache = device->mem_cache;

   uint8_t hash[SHA1_DIGEST_LENGTH];
   _mesa_sha1_compute(binary, binary->total_size, hash);

   /* TODO: Skip disk-cache for meta-shaders because they are stored in a different cache file */

   struct vk_pipeline_cache_object *shader_obj;
   shader_obj = vk_pipeline_cache_create_and_insert_object(cache, hash, SHA1_DIGEST_LENGTH, binary,
                                                           binary->total_size, &radv_shader_ops);

   return shader_obj ? container_of(shader_obj, struct radv_shader, base) : NULL;
}

const struct vk_pipeline_cache_object_ops radv_shader_ops = {
   .serialize = radv_shader_serialize,
   .deserialize = radv_shader_deserialize,
   .destroy = radv_shader_destroy,
};

struct radv_pipeline_cache_object {
   struct vk_pipeline_cache_object base;
   unsigned num_shaders;
   unsigned num_stack_sizes;
   unsigned ps_epilog_binary_size;
   struct radv_shader_part *ps_epilog;
   void *data; /* pointer to stack sizes or ps epilog binary */
   uint8_t sha1[SHA1_DIGEST_LENGTH];
   struct radv_shader *shaders[];
};

const struct vk_pipeline_cache_object_ops radv_pipeline_ops;

static struct radv_pipeline_cache_object *
radv_pipeline_cache_object_create(struct vk_device *device, unsigned num_shaders, const void *hash,
                                  unsigned num_stack_sizes, unsigned ps_epilog_binary_size)
{
   assert(num_stack_sizes == 0 || ps_epilog_binary_size == 0);
   const size_t size = sizeof(struct radv_pipeline_cache_object) +
                       (num_shaders * sizeof(struct radv_shader *)) + ps_epilog_binary_size +
                       (num_stack_sizes * sizeof(struct radv_pipeline_shader_stack_size));

   struct radv_pipeline_cache_object *object =
      vk_alloc(&device->alloc, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_CACHE);
   if (!object)
      return NULL;

   vk_pipeline_cache_object_init(device, &object->base, &radv_pipeline_ops, object->sha1,
                                 SHA1_DIGEST_LENGTH);
   object->num_shaders = num_shaders;
   object->num_stack_sizes = num_stack_sizes;
   object->ps_epilog_binary_size = ps_epilog_binary_size;
   object->ps_epilog = NULL;
   object->data = &object->shaders[num_shaders];
   memcpy(object->sha1, hash, SHA1_DIGEST_LENGTH);
   memset(object->shaders, 0, sizeof(object->shaders[0]) * num_shaders);

   return object;
}

static void
radv_pipeline_cache_object_destroy(struct vk_device *_device,
                                   struct vk_pipeline_cache_object *object)
{
   struct radv_device *device = container_of(_device, struct radv_device, vk);
   struct radv_pipeline_cache_object *pipeline_obj =
      container_of(object, struct radv_pipeline_cache_object, base);

   for (unsigned i = 0; i < pipeline_obj->num_shaders; i++) {
      if (pipeline_obj->shaders[i])
         radv_shader_unref(device, pipeline_obj->shaders[i]);
   }
   if (pipeline_obj->ps_epilog)
      radv_shader_part_unref(device, pipeline_obj->ps_epilog);

   vk_pipeline_cache_object_finish(&pipeline_obj->base);
   vk_free(&_device->alloc, pipeline_obj);
}

static struct vk_pipeline_cache_object *
radv_pipeline_cache_object_deserialize(struct vk_pipeline_cache *cache, const void *key_data,
                                       size_t key_size, struct blob_reader *blob)
{
   struct radv_device *device = container_of(cache->base.device, struct radv_device, vk);
   assert(key_size == SHA1_DIGEST_LENGTH);
   unsigned total_size = blob->end - blob->current;
   unsigned num_shaders = blob_read_uint32(blob);
   unsigned num_stack_sizes = blob_read_uint32(blob);
   unsigned ps_epilog_binary_size = blob_read_uint32(blob);

   struct radv_pipeline_cache_object *object;
   object = radv_pipeline_cache_object_create(&device->vk, num_shaders, key_data, num_stack_sizes,
                                              ps_epilog_binary_size);
   if (!object)
      return NULL;

   object->base.data_size = total_size;

   for (unsigned i = 0; i < num_shaders; i++) {
      const unsigned char *hash = blob_read_bytes(blob, SHA1_DIGEST_LENGTH);
      struct vk_pipeline_cache_object *shader =
         vk_pipeline_cache_lookup_object(cache, hash, SHA1_DIGEST_LENGTH, &radv_shader_ops, NULL);

      if (!shader) {
         /* If some shader could not be created from cache, better return NULL here than having
          * an incomplete cache object which needs to be fixed up later.
          */
         vk_pipeline_cache_object_unref(&device->vk, &object->base);
         return NULL;
      }

      object->shaders[i] = container_of(shader, struct radv_shader, base);
   }

   const size_t data_size =
      ps_epilog_binary_size + (num_stack_sizes * sizeof(struct radv_pipeline_shader_stack_size));
   blob_copy_bytes(blob, object->data, data_size);

   if (ps_epilog_binary_size) {
      assert(num_stack_sizes == 0);
      struct radv_shader_part_binary *binary = object->data;
      object->ps_epilog =
         radv_shader_part_create(device, binary, device->physical_device->ps_wave_size);

      if (!object->ps_epilog) {
         vk_pipeline_cache_object_unref(&device->vk, &object->base);
         return NULL;
      }
   }

   return &object->base;
}

static bool
radv_pipeline_cache_object_serialize(struct vk_pipeline_cache_object *object, struct blob *blob)
{
   struct radv_pipeline_cache_object *pipeline_obj =
      container_of(object, struct radv_pipeline_cache_object, base);

   blob_write_uint32(blob, pipeline_obj->num_shaders);
   blob_write_uint32(blob, pipeline_obj->num_stack_sizes);
   blob_write_uint32(blob, pipeline_obj->ps_epilog_binary_size);

   for (unsigned i = 0; i < pipeline_obj->num_shaders; i++)
      blob_write_bytes(blob, pipeline_obj->shaders[i]->sha1, SHA1_DIGEST_LENGTH);

   const size_t data_size =
      pipeline_obj->ps_epilog_binary_size +
      (pipeline_obj->num_stack_sizes * sizeof(struct radv_pipeline_shader_stack_size));
   blob_write_bytes(blob, pipeline_obj->data, data_size);

   return true;
}

const struct vk_pipeline_cache_object_ops radv_pipeline_ops = {
   .serialize = radv_pipeline_cache_object_serialize,
   .deserialize = radv_pipeline_cache_object_deserialize,
   .destroy = radv_pipeline_cache_object_destroy,
};

bool
radv_pipeline_cache_search(struct radv_device *device, struct vk_pipeline_cache *cache,
                           struct radv_pipeline *pipeline, const unsigned char *sha1,
                           bool *found_in_application_cache)
{
   *found_in_application_cache = false;

   if (radv_is_cache_disabled(device))
      return false;

   bool *found = found_in_application_cache;
   if (!cache) {
      cache = device->mem_cache;
      found = NULL;
   }

   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_lookup_object(cache, sha1, SHA1_DIGEST_LENGTH, &radv_pipeline_ops, found);

   if (!object)
      return false;

   struct radv_pipeline_cache_object *pipeline_obj =
      container_of(object, struct radv_pipeline_cache_object, base);

   for (unsigned i = 0; i < pipeline_obj->num_shaders; i++) {
      gl_shader_stage s = pipeline_obj->shaders[i]->info.stage;
      if (s == MESA_SHADER_VERTEX && i > 0) {
         /* The GS copy-shader is a VS placed after all other stages */
         assert(i == pipeline_obj->num_shaders - 1 && pipeline->shaders[MESA_SHADER_GEOMETRY]);
         pipeline->gs_copy_shader = radv_shader_ref(pipeline_obj->shaders[i]);
      } else {
         pipeline->shaders[s] = radv_shader_ref(pipeline_obj->shaders[i]);
      }
   }

   if (pipeline_obj->ps_epilog) {
      struct radv_shader_part *ps_epilog = radv_shader_part_ref(pipeline_obj->ps_epilog);

      if (pipeline->type == RADV_PIPELINE_GRAPHICS)
         radv_pipeline_to_graphics(pipeline)->ps_epilog = ps_epilog;
      else
         radv_pipeline_to_graphics_lib(pipeline)->base.ps_epilog = ps_epilog;
   }

   if (pipeline->type == RADV_PIPELINE_RAY_TRACING) {
      unsigned num_rt_groups = radv_pipeline_to_ray_tracing(pipeline)->group_count;
      assert(num_rt_groups == pipeline_obj->num_stack_sizes);
      struct radv_pipeline_shader_stack_size *stack_sizes = pipeline_obj->data;
      struct radv_ray_tracing_module *rt_groups = radv_pipeline_to_ray_tracing(pipeline)->groups;
      for (unsigned i = 0; i < num_rt_groups; i++)
         rt_groups[i].stack_size = stack_sizes[i];
   }

   vk_pipeline_cache_object_unref(&device->vk, object);
   return true;
}

void
radv_pipeline_cache_insert(struct radv_device *device, struct vk_pipeline_cache *cache,
                           struct radv_pipeline *pipeline,
                           struct radv_shader_part_binary *ps_epilog_binary,
                           const unsigned char *sha1)
{
   if (radv_is_cache_disabled(device))
      return;

   if (!cache)
      cache = device->mem_cache;

   /* Count shaders */
   unsigned num_shaders = 0;
   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i)
      num_shaders += pipeline->shaders[i] ? 1 : 0;
   num_shaders += pipeline->gs_copy_shader ? 1 : 0;

   unsigned ps_epilog_binary_size = ps_epilog_binary ? ps_epilog_binary->total_size : 0;
   unsigned num_rt_groups = 0;
   if (pipeline->type == RADV_PIPELINE_RAY_TRACING)
      num_rt_groups = radv_pipeline_to_ray_tracing(pipeline)->group_count;

   struct radv_pipeline_cache_object *pipeline_obj;
   pipeline_obj = radv_pipeline_cache_object_create(&device->vk, num_shaders, sha1, num_rt_groups,
                                                    ps_epilog_binary_size);

   if (!pipeline_obj)
      return;

   unsigned idx = 0;
   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (pipeline->shaders[i])
         pipeline_obj->shaders[idx++] = radv_shader_ref(pipeline->shaders[i]);
   }
   /* Place the GS copy-shader after all other stages */
   if (pipeline->gs_copy_shader)
      pipeline_obj->shaders[idx++] = radv_shader_ref(pipeline->gs_copy_shader);

   assert(idx == num_shaders);

   if (ps_epilog_binary) {
      memcpy(pipeline_obj->data, ps_epilog_binary, ps_epilog_binary_size);
      struct radv_shader_part *ps_epilog;
      if (pipeline->type == RADV_PIPELINE_GRAPHICS)
         ps_epilog = radv_pipeline_to_graphics(pipeline)->ps_epilog;
      else
         ps_epilog = radv_pipeline_to_graphics_lib(pipeline)->base.ps_epilog;

      pipeline_obj->ps_epilog = radv_shader_part_ref(ps_epilog);
   }

   if (pipeline->type == RADV_PIPELINE_RAY_TRACING) {
      struct radv_pipeline_shader_stack_size *stack_sizes = pipeline_obj->data;
      struct radv_ray_tracing_module *rt_groups = radv_pipeline_to_ray_tracing(pipeline)->groups;
      for (unsigned i = 0; i < num_rt_groups; i++)
         stack_sizes[i] = rt_groups[i].stack_size;
   }

   /* Add the object to the cache */
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_add_object(cache, &pipeline_obj->base);
   vk_pipeline_cache_object_unref(&device->vk, object);
}
