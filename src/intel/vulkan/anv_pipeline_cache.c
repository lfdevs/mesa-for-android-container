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

#include "util/mesa-sha1.h"
#include "util/hash_table.h"
#include "util/debug.h"
#include "anv_private.h"

static size_t
anv_shader_bin_size(uint32_t prog_data_size, uint32_t nr_params,
                    uint32_t key_size,
                    uint32_t surface_count, uint32_t sampler_count)
{
   const uint32_t binding_data_size =
      (surface_count + sampler_count) * sizeof(struct anv_pipeline_binding);

   return align_u32(sizeof(struct anv_shader_bin), 8) +
          align_u32(prog_data_size, 8) +
          align_u32(nr_params * sizeof(void *), 8) +
          align_u32(sizeof(uint32_t) + key_size, 8) +
          align_u32(binding_data_size, 8);
}

struct anv_shader_bin *
anv_shader_bin_create(struct anv_device *device,
                      const void *key_data, uint32_t key_size,
                      const void *kernel_data, uint32_t kernel_size,
                      const struct brw_stage_prog_data *prog_data,
                      uint32_t prog_data_size, const void *prog_data_param,
                      const struct anv_pipeline_bind_map *bind_map)
{
   const size_t size =
      anv_shader_bin_size(prog_data_size, prog_data->nr_params, key_size,
                          bind_map->surface_count, bind_map->sampler_count);

   struct anv_shader_bin *shader =
      vk_alloc(&device->alloc, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!shader)
      return NULL;

   shader->ref_cnt = 1;

   shader->kernel =
      anv_state_pool_alloc(&device->instruction_state_pool, kernel_size, 64);
   memcpy(shader->kernel.map, kernel_data, kernel_size);
   shader->kernel_size = kernel_size;
   shader->bind_map = *bind_map;
   shader->prog_data_size = prog_data_size;

   /* Now we fill out the floating data at the end */
   void *data = shader;
   data += align_u32(sizeof(struct anv_shader_bin), 8);

   shader->prog_data = data;
   struct brw_stage_prog_data *new_prog_data = data;
   memcpy(data, prog_data, prog_data_size);
   data += align_u32(prog_data_size, 8);

   assert(prog_data->nr_pull_params == 0);
   assert(prog_data->nr_image_params == 0);
   new_prog_data->param = data;
   uint32_t param_size = prog_data->nr_params * sizeof(void *);
   memcpy(data, prog_data_param, param_size);
   data += align_u32(param_size, 8);

   shader->key = data;
   struct anv_shader_bin_key *key = data;
   key->size = key_size;
   memcpy(key->data, key_data, key_size);
   data += align_u32(sizeof(*key) + key_size, 8);

   shader->bind_map.surface_to_descriptor = data;
   memcpy(data, bind_map->surface_to_descriptor,
          bind_map->surface_count * sizeof(struct anv_pipeline_binding));
   data += bind_map->surface_count * sizeof(struct anv_pipeline_binding);

   shader->bind_map.sampler_to_descriptor = data;
   memcpy(data, bind_map->sampler_to_descriptor,
          bind_map->sampler_count * sizeof(struct anv_pipeline_binding));

   return shader;
}

void
anv_shader_bin_destroy(struct anv_device *device,
                       struct anv_shader_bin *shader)
{
   assert(shader->ref_cnt == 0);
   anv_state_pool_free(&device->instruction_state_pool, shader->kernel);
   vk_free(&device->alloc, shader);
}

static size_t
anv_shader_bin_data_size(const struct anv_shader_bin *shader)
{
   return anv_shader_bin_size(shader->prog_data_size,
                              shader->prog_data->nr_params, shader->key->size,
                              shader->bind_map.surface_count,
                              shader->bind_map.sampler_count) +
          align_u32(shader->kernel_size, 8);
}

static void
anv_shader_bin_write_data(const struct anv_shader_bin *shader, void *data)
{
   size_t struct_size =
      anv_shader_bin_size(shader->prog_data_size,
                          shader->prog_data->nr_params, shader->key->size,
                          shader->bind_map.surface_count,
                          shader->bind_map.sampler_count);

   memcpy(data, shader, struct_size);
   data += struct_size;

   memcpy(data, shader->kernel.map, shader->kernel_size);
}

/* Remaining work:
 *
 * - Compact binding table layout so it's tight and not dependent on
 *   descriptor set layout.
 *
 * - Review prog_data struct for size and cacheability: struct
 *   brw_stage_prog_data has binding_table which uses a lot of uint32_t for 8
 *   bit quantities etc; param, pull_param, and image_params are pointers, we
 *   just need the compation map. use bit fields for all bools, eg
 *   dual_src_blend.
 */

static uint32_t
shader_bin_key_hash_func(const void *void_key)
{
   const struct anv_shader_bin_key *key = void_key;
   return _mesa_hash_data(key->data, key->size);
}

static bool
shader_bin_key_compare_func(const void *void_a, const void *void_b)
{
   const struct anv_shader_bin_key *a = void_a, *b = void_b;
   if (a->size != b->size)
      return false;

   return memcmp(a->data, b->data, a->size) == 0;
}

void
anv_pipeline_cache_init(struct anv_pipeline_cache *cache,
                        struct anv_device *device,
                        bool cache_enabled)
{
   cache->device = device;
   pthread_mutex_init(&cache->mutex, NULL);

   if (cache_enabled) {
      cache->cache = _mesa_hash_table_create(NULL, shader_bin_key_hash_func,
                                             shader_bin_key_compare_func);
   } else {
      cache->cache = NULL;
   }
}

void
anv_pipeline_cache_finish(struct anv_pipeline_cache *cache)
{
   pthread_mutex_destroy(&cache->mutex);

   if (cache->cache) {
      /* This is a bit unfortunate.  In order to keep things from randomly
       * going away, the shader cache has to hold a reference to all shader
       * binaries it contains.  We unref them when we destroy the cache.
       */
      struct hash_entry *entry;
      hash_table_foreach(cache->cache, entry)
         anv_shader_bin_unref(cache->device, entry->data);

      _mesa_hash_table_destroy(cache->cache, NULL);
   }
}

void
anv_hash_shader(unsigned char *hash, const void *key, size_t key_size,
                struct anv_shader_module *module,
                const char *entrypoint,
                const struct anv_pipeline_layout *pipeline_layout,
                const VkSpecializationInfo *spec_info)
{
   struct mesa_sha1 *ctx;

   ctx = _mesa_sha1_init();
   _mesa_sha1_update(ctx, key, key_size);
   _mesa_sha1_update(ctx, module->sha1, sizeof(module->sha1));
   _mesa_sha1_update(ctx, entrypoint, strlen(entrypoint));
   if (pipeline_layout) {
      _mesa_sha1_update(ctx, pipeline_layout->sha1,
                        sizeof(pipeline_layout->sha1));
   }
   /* hash in shader stage, pipeline layout? */
   if (spec_info) {
      _mesa_sha1_update(ctx, spec_info->pMapEntries,
                        spec_info->mapEntryCount * sizeof spec_info->pMapEntries[0]);
      _mesa_sha1_update(ctx, spec_info->pData, spec_info->dataSize);
   }
   _mesa_sha1_final(ctx, hash);
}

static struct anv_shader_bin *
anv_pipeline_cache_search_locked(struct anv_pipeline_cache *cache,
                                 const void *key_data, uint32_t key_size)
{
   uint32_t vla[1 + DIV_ROUND_UP(key_size, sizeof(uint32_t))];
   struct anv_shader_bin_key *key = (void *)vla;
   key->size = key_size;
   memcpy(key->data, key_data, key_size);

   struct hash_entry *entry = _mesa_hash_table_search(cache->cache, key);
   if (entry)
      return entry->data;
   else
      return NULL;
}

struct anv_shader_bin *
anv_pipeline_cache_search(struct anv_pipeline_cache *cache,
                          const void *key_data, uint32_t key_size)
{
   if (!cache->cache)
      return NULL;

   pthread_mutex_lock(&cache->mutex);

   struct anv_shader_bin *shader =
      anv_pipeline_cache_search_locked(cache, key_data, key_size);

   pthread_mutex_unlock(&cache->mutex);

   /* We increment refcount before handing it to the caller */
   if (shader)
      anv_shader_bin_ref(shader);

   return shader;
}

static struct anv_shader_bin *
anv_pipeline_cache_add_shader(struct anv_pipeline_cache *cache,
                              const void *key_data, uint32_t key_size,
                              const void *kernel_data, uint32_t kernel_size,
                              const struct brw_stage_prog_data *prog_data,
                              uint32_t prog_data_size,
                              const void *prog_data_param,
                              const struct anv_pipeline_bind_map *bind_map)
{
   struct anv_shader_bin *shader =
      anv_pipeline_cache_search_locked(cache, key_data, key_size);
   if (shader)
      return shader;

   struct anv_shader_bin *bin =
      anv_shader_bin_create(cache->device, key_data, key_size,
                            kernel_data, kernel_size,
                            prog_data, prog_data_size, prog_data_param,
                            bind_map);
   if (!bin)
      return NULL;

   _mesa_hash_table_insert(cache->cache, bin->key, bin);

   return bin;
}

struct anv_shader_bin *
anv_pipeline_cache_upload_kernel(struct anv_pipeline_cache *cache,
                                 const void *key_data, uint32_t key_size,
                                 const void *kernel_data, uint32_t kernel_size,
                                 const struct brw_stage_prog_data *prog_data,
                                 uint32_t prog_data_size,
                                 const struct anv_pipeline_bind_map *bind_map)
{
   if (cache->cache) {
      pthread_mutex_lock(&cache->mutex);

      struct anv_shader_bin *bin =
         anv_pipeline_cache_add_shader(cache, key_data, key_size,
                                       kernel_data, kernel_size,
                                       prog_data, prog_data_size,
                                       prog_data->param, bind_map);

      pthread_mutex_unlock(&cache->mutex);

      /* We increment refcount before handing it to the caller */
      anv_shader_bin_ref(bin);

      return bin;
   } else {
      /* In this case, we're not caching it so the caller owns it entirely */
      return anv_shader_bin_create(cache->device, key_data, key_size,
                                   kernel_data, kernel_size,
                                   prog_data, prog_data_size,
                                   prog_data->param, bind_map);
   }
}

struct cache_header {
   uint32_t header_size;
   uint32_t header_version;
   uint32_t vendor_id;
   uint32_t device_id;
   uint8_t  uuid[VK_UUID_SIZE];
};

static void
anv_pipeline_cache_load(struct anv_pipeline_cache *cache,
                        const void *data, size_t size)
{
   struct anv_device *device = cache->device;
   struct anv_physical_device *pdevice = &device->instance->physicalDevice;
   struct cache_header header;

   if (cache->cache == NULL)
      return;

   if (size < sizeof(header))
      return;
   memcpy(&header, data, sizeof(header));
   if (header.header_size < sizeof(header))
      return;
   if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
      return;
   if (header.vendor_id != 0x8086)
      return;
   if (header.device_id != device->chipset_id)
      return;
   if (memcmp(header.uuid, pdevice->uuid, VK_UUID_SIZE) != 0)
      return;

   const void *end = data + size;
   const void *p = data + header.header_size;

   /* Count is the total number of valid entries */
   uint32_t count;
   if (p + sizeof(count) >= end)
      return;
   memcpy(&count, p, sizeof(count));
   p += align_u32(sizeof(count), 8);

   for (uint32_t i = 0; i < count; i++) {
      struct anv_shader_bin bin;
      if (p + sizeof(bin) > end)
         break;
      memcpy(&bin, p, sizeof(bin));
      p += align_u32(sizeof(struct anv_shader_bin), 8);

      const struct brw_stage_prog_data *prog_data = p;
      p += align_u32(bin.prog_data_size, 8);
      if (p > end)
         break;

      uint32_t param_size = prog_data->nr_params * sizeof(void *);
      const void *prog_data_param = p;
      p += align_u32(param_size, 8);

      struct anv_shader_bin_key key;
      if (p + sizeof(key) > end)
         break;
      memcpy(&key, p, sizeof(key));
      const void *key_data = p + sizeof(key);
      p += align_u32(sizeof(key) + key.size, 8);

      /* We're going to memcpy this so getting rid of const is fine */
      struct anv_pipeline_binding *bindings = (void *)p;
      p += align_u32((bin.bind_map.surface_count + bin.bind_map.sampler_count) *
                     sizeof(struct anv_pipeline_binding), 8);
      bin.bind_map.surface_to_descriptor = bindings;
      bin.bind_map.sampler_to_descriptor = bindings + bin.bind_map.surface_count;

      const void *kernel_data = p;
      p += align_u32(bin.kernel_size, 8);

      if (p > end)
         break;

      anv_pipeline_cache_add_shader(cache, key_data, key.size,
                                    kernel_data, bin.kernel_size,
                                    prog_data, bin.prog_data_size,
                                    prog_data_param, &bin.bind_map);
   }
}

static bool
pipeline_cache_enabled()
{
   static int enabled = -1;
   if (enabled < 0)
      enabled = env_var_as_boolean("ANV_ENABLE_PIPELINE_CACHE", true);
   return enabled;
}

VkResult anv_CreatePipelineCache(
    VkDevice                                    _device,
    const VkPipelineCacheCreateInfo*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineCache*                            pPipelineCache)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_pipeline_cache *cache;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);

   cache = vk_alloc2(&device->alloc, pAllocator,
                       sizeof(*cache), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cache == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   anv_pipeline_cache_init(cache, device, pipeline_cache_enabled());

   if (pCreateInfo->initialDataSize > 0)
      anv_pipeline_cache_load(cache,
                              pCreateInfo->pInitialData,
                              pCreateInfo->initialDataSize);

   *pPipelineCache = anv_pipeline_cache_to_handle(cache);

   return VK_SUCCESS;
}

void anv_DestroyPipelineCache(
    VkDevice                                    _device,
    VkPipelineCache                             _cache,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_pipeline_cache, cache, _cache);

   if (!cache)
      return;

   anv_pipeline_cache_finish(cache);

   vk_free2(&device->alloc, pAllocator, cache);
}

VkResult anv_GetPipelineCacheData(
    VkDevice                                    _device,
    VkPipelineCache                             _cache,
    size_t*                                     pDataSize,
    void*                                       pData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_pipeline_cache, cache, _cache);
   struct anv_physical_device *pdevice = &device->instance->physicalDevice;
   struct cache_header *header;

   if (pData == NULL) {
      size_t size = align_u32(sizeof(*header), 8) +
                    align_u32(sizeof(uint32_t), 8);

      if (cache->cache) {
         struct hash_entry *entry;
         hash_table_foreach(cache->cache, entry)
            size += anv_shader_bin_data_size(entry->data);
      }

      *pDataSize = size;
      return VK_SUCCESS;
   }

   if (*pDataSize < sizeof(*header)) {
      *pDataSize = 0;
      return VK_INCOMPLETE;
   }

   void *p = pData, *end = pData + *pDataSize;
   header = p;
   header->header_size = sizeof(*header);
   header->header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
   header->vendor_id = 0x8086;
   header->device_id = device->chipset_id;
   memcpy(header->uuid, pdevice->uuid, VK_UUID_SIZE);
   p += align_u32(header->header_size, 8);

   uint32_t *count = p;
   p += align_u32(sizeof(*count), 8);
   *count = 0;

   VkResult result = VK_SUCCESS;
   if (cache->cache) {
      struct hash_entry *entry;
      hash_table_foreach(cache->cache, entry) {
         struct anv_shader_bin *shader = entry->data;
         size_t data_size = anv_shader_bin_data_size(entry->data);
         if (p + data_size > end) {
            result = VK_INCOMPLETE;
            break;
         }

         anv_shader_bin_write_data(shader, p);
         p += data_size;

         (*count)++;
      }
   }

   *pDataSize = p - pData;

   return result;
}

VkResult anv_MergePipelineCaches(
    VkDevice                                    _device,
    VkPipelineCache                             destCache,
    uint32_t                                    srcCacheCount,
    const VkPipelineCache*                      pSrcCaches)
{
   ANV_FROM_HANDLE(anv_pipeline_cache, dst, destCache);

   if (!dst->cache)
      return VK_SUCCESS;

   for (uint32_t i = 0; i < srcCacheCount; i++) {
      ANV_FROM_HANDLE(anv_pipeline_cache, src, pSrcCaches[i]);
      if (!src->cache)
         continue;

      struct hash_entry *entry;
      hash_table_foreach(src->cache, entry) {
         struct anv_shader_bin *bin = entry->data;
         if (_mesa_hash_table_search(dst->cache, bin->key))
            continue;

         anv_shader_bin_ref(bin);
         _mesa_hash_table_insert(dst->cache, bin->key, bin);
      }
   }

   return VK_SUCCESS;
}
