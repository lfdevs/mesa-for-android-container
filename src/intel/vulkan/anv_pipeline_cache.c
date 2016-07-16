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
#include "util/debug.h"
#include "anv_private.h"

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

void
anv_pipeline_cache_init(struct anv_pipeline_cache *cache,
                        struct anv_device *device)
{
   cache->device = device;
   anv_state_stream_init(&cache->program_stream,
                         &device->instruction_block_pool);
   pthread_mutex_init(&cache->mutex, NULL);

   cache->kernel_count = 0;
   cache->total_size = 0;
   cache->table_size = 1024;
   const size_t byte_size = cache->table_size * sizeof(cache->hash_table[0]);
   cache->hash_table = malloc(byte_size);

   /* We don't consider allocation failure fatal, we just start with a 0-sized
    * cache. */
   if (cache->hash_table == NULL ||
       !env_var_as_boolean("ANV_ENABLE_PIPELINE_CACHE", true))
      cache->table_size = 0;
   else
      memset(cache->hash_table, 0xff, byte_size);
}

void
anv_pipeline_cache_finish(struct anv_pipeline_cache *cache)
{
   anv_state_stream_finish(&cache->program_stream);
   pthread_mutex_destroy(&cache->mutex);
   free(cache->hash_table);
}

struct cache_entry {
   unsigned char sha1[20];
   uint32_t prog_data_size;
   uint32_t kernel_size;
   uint32_t surface_count;
   uint32_t sampler_count;
   uint32_t image_count;

   char prog_data[0];

   /* kernel follows prog_data at next 64 byte aligned address */
};

static uint32_t
entry_size(struct cache_entry *entry)
{
   /* This returns the number of bytes needed to serialize an entry, which
    * doesn't include the alignment padding bytes.
    */

   const uint32_t map_size =
      entry->surface_count * sizeof(struct anv_pipeline_binding) +
      entry->sampler_count * sizeof(struct anv_pipeline_binding);

   return sizeof(*entry) + entry->prog_data_size + map_size;
}

void
anv_hash_shader(unsigned char *hash, const void *key, size_t key_size,
                struct anv_shader_module *module,
                const char *entrypoint,
                const VkSpecializationInfo *spec_info)
{
   struct mesa_sha1 *ctx;

   ctx = _mesa_sha1_init();
   _mesa_sha1_update(ctx, key, key_size);
   _mesa_sha1_update(ctx, module->sha1, sizeof(module->sha1));
   _mesa_sha1_update(ctx, entrypoint, strlen(entrypoint));
   /* hash in shader stage, pipeline layout? */
   if (spec_info) {
      _mesa_sha1_update(ctx, spec_info->pMapEntries,
                        spec_info->mapEntryCount * sizeof spec_info->pMapEntries[0]);
      _mesa_sha1_update(ctx, spec_info->pData, spec_info->dataSize);
   }
   _mesa_sha1_final(ctx, hash);
}

static uint32_t
anv_pipeline_cache_search_unlocked(struct anv_pipeline_cache *cache,
                                   const unsigned char *sha1,
                                   const struct brw_stage_prog_data **prog_data,
                                   struct anv_pipeline_bind_map *map)
{
   const uint32_t mask = cache->table_size - 1;
   const uint32_t start = (*(uint32_t *) sha1);

   for (uint32_t i = 0; i < cache->table_size; i++) {
      const uint32_t index = (start + i) & mask;
      const uint32_t offset = cache->hash_table[index];

      if (offset == ~0)
         return NO_KERNEL;

      struct cache_entry *entry =
         cache->program_stream.block_pool->map + offset;
      if (memcmp(entry->sha1, sha1, sizeof(entry->sha1)) == 0) {
         if (prog_data) {
            assert(map);
            void *p = entry->prog_data;
            *prog_data = p;
            p += entry->prog_data_size;
            map->surface_count = entry->surface_count;
            map->sampler_count = entry->sampler_count;
            map->image_count = entry->image_count;
            map->surface_to_descriptor = p;
            p += map->surface_count * sizeof(struct anv_pipeline_binding);
            map->sampler_to_descriptor = p;
         }

         return offset + align_u32(entry_size(entry), 64);
      }
   }

   /* This can happen if the pipeline cache is disabled via
    * ANV_ENABLE_PIPELINE_CACHE=false
    */
   return NO_KERNEL;
}

uint32_t
anv_pipeline_cache_search(struct anv_pipeline_cache *cache,
                          const unsigned char *sha1,
                          const struct brw_stage_prog_data **prog_data,
                          struct anv_pipeline_bind_map *map)
{
   uint32_t kernel;

   pthread_mutex_lock(&cache->mutex);

   kernel = anv_pipeline_cache_search_unlocked(cache, sha1, prog_data, map);

   pthread_mutex_unlock(&cache->mutex);

   return kernel;
}

static void
anv_pipeline_cache_set_entry(struct anv_pipeline_cache *cache,
                             struct cache_entry *entry, uint32_t entry_offset)
{
   const uint32_t mask = cache->table_size - 1;
   const uint32_t start = (*(uint32_t *) entry->sha1);

   /* We'll always be able to insert when we get here. */
   assert(cache->kernel_count < cache->table_size / 2);

   for (uint32_t i = 0; i < cache->table_size; i++) {
      const uint32_t index = (start + i) & mask;
      if (cache->hash_table[index] == ~0) {
         cache->hash_table[index] = entry_offset;
         break;
      }
   }

   cache->total_size += entry_size(entry) + entry->kernel_size;
   cache->kernel_count++;
}

static VkResult
anv_pipeline_cache_grow(struct anv_pipeline_cache *cache)
{
   const uint32_t table_size = cache->table_size * 2;
   const uint32_t old_table_size = cache->table_size;
   const size_t byte_size = table_size * sizeof(cache->hash_table[0]);
   uint32_t *table;
   uint32_t *old_table = cache->hash_table;

   table = malloc(byte_size);
   if (table == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   cache->hash_table = table;
   cache->table_size = table_size;
   cache->kernel_count = 0;
   cache->total_size = 0;

   memset(cache->hash_table, 0xff, byte_size);
   for (uint32_t i = 0; i < old_table_size; i++) {
      const uint32_t offset = old_table[i];
      if (offset == ~0)
         continue;

      struct cache_entry *entry =
         cache->program_stream.block_pool->map + offset;
      anv_pipeline_cache_set_entry(cache, entry, offset);
   }

   free(old_table);

   return VK_SUCCESS;
}

static void
anv_pipeline_cache_add_entry(struct anv_pipeline_cache *cache,
                             struct cache_entry *entry, uint32_t entry_offset)
{
   if (cache->kernel_count == cache->table_size / 2)
      anv_pipeline_cache_grow(cache);

   /* Failing to grow that hash table isn't fatal, but may mean we don't
    * have enough space to add this new kernel. Only add it if there's room.
    */
   if (cache->kernel_count < cache->table_size / 2)
      anv_pipeline_cache_set_entry(cache, entry, entry_offset);
}

uint32_t
anv_pipeline_cache_upload_kernel(struct anv_pipeline_cache *cache,
                                 const unsigned char *sha1,
                                 const void *kernel, size_t kernel_size,
                                 const struct brw_stage_prog_data **prog_data,
                                 size_t prog_data_size,
                                 struct anv_pipeline_bind_map *map)
{
   pthread_mutex_lock(&cache->mutex);

   /* Before uploading, check again that another thread didn't upload this
    * shader while we were compiling it.
    */
   if (sha1) {
      uint32_t cached_kernel =
         anv_pipeline_cache_search_unlocked(cache, sha1, prog_data, map);
      if (cached_kernel != NO_KERNEL) {
         pthread_mutex_unlock(&cache->mutex);
         return cached_kernel;
      }
   }

   struct cache_entry *entry;

   const uint32_t map_size =
      map->surface_count * sizeof(struct anv_pipeline_binding) +
      map->sampler_count * sizeof(struct anv_pipeline_binding);

   const uint32_t preamble_size =
      align_u32(sizeof(*entry) + prog_data_size + map_size, 64);

   const uint32_t size = preamble_size + kernel_size;

   assert(size < cache->program_stream.block_pool->block_size);
   const struct anv_state state =
      anv_state_stream_alloc(&cache->program_stream, size, 64);

   entry = state.map;
   entry->prog_data_size = prog_data_size;
   entry->surface_count = map->surface_count;
   entry->sampler_count = map->sampler_count;
   entry->image_count = map->image_count;
   entry->kernel_size = kernel_size;

   void *p = entry->prog_data;
   memcpy(p, *prog_data, prog_data_size);
   p += prog_data_size;

   memcpy(p, map->surface_to_descriptor,
          map->surface_count * sizeof(struct anv_pipeline_binding));
   map->surface_to_descriptor = p;
   p += map->surface_count * sizeof(struct anv_pipeline_binding);

   memcpy(p, map->sampler_to_descriptor,
          map->sampler_count * sizeof(struct anv_pipeline_binding));
   map->sampler_to_descriptor = p;

   if (sha1) {
      assert(anv_pipeline_cache_search_unlocked(cache, sha1,
                                                NULL, NULL) == NO_KERNEL);

      memcpy(entry->sha1, sha1, sizeof(entry->sha1));
      anv_pipeline_cache_add_entry(cache, entry, state.offset);
   }

   pthread_mutex_unlock(&cache->mutex);

   memcpy(state.map + preamble_size, kernel, kernel_size);

   if (!cache->device->info.has_llc)
      anv_state_clflush(state);

   *prog_data = (const struct brw_stage_prog_data *) entry->prog_data;

   return state.offset + preamble_size;
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
   struct cache_header header;
   uint8_t uuid[VK_UUID_SIZE];

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
   anv_device_get_cache_uuid(uuid);
   if (memcmp(header.uuid, uuid, VK_UUID_SIZE) != 0)
      return;

   void *end = (void *) data + size;
   void *p = (void *) data + header.header_size;

   while (p < end) {
      struct cache_entry *entry = p;

      void *data = entry->prog_data;
      const struct brw_stage_prog_data *prog_data = data;
      data += entry->prog_data_size;

      struct anv_pipeline_binding *surface_to_descriptor = data;
      data += entry->surface_count * sizeof(struct anv_pipeline_binding);
      struct anv_pipeline_binding *sampler_to_descriptor = data;
      data += entry->sampler_count * sizeof(struct anv_pipeline_binding);
      void *kernel = data;

      struct anv_pipeline_bind_map map = {
         .surface_count = entry->surface_count,
         .sampler_count = entry->sampler_count,
         .image_count = entry->image_count,
         .surface_to_descriptor = surface_to_descriptor,
         .sampler_to_descriptor = sampler_to_descriptor
      };

      anv_pipeline_cache_upload_kernel(cache, entry->sha1,
                                       kernel, entry->kernel_size,
                                       &prog_data,
                                       entry->prog_data_size, &map);
      p = kernel + entry->kernel_size;
   }
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

   cache = anv_alloc2(&device->alloc, pAllocator,
                       sizeof(*cache), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cache == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   anv_pipeline_cache_init(cache, device);

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

   anv_pipeline_cache_finish(cache);

   anv_free2(&device->alloc, pAllocator, cache);
}

VkResult anv_GetPipelineCacheData(
    VkDevice                                    _device,
    VkPipelineCache                             _cache,
    size_t*                                     pDataSize,
    void*                                       pData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_pipeline_cache, cache, _cache);
   struct cache_header *header;

   const size_t size = sizeof(*header) + cache->total_size;

   if (pData == NULL) {
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
   anv_device_get_cache_uuid(header->uuid);
   p += header->header_size;

   struct cache_entry *entry;
   for (uint32_t i = 0; i < cache->table_size; i++) {
      if (cache->hash_table[i] == ~0)
         continue;

      entry = cache->program_stream.block_pool->map + cache->hash_table[i];
      const uint32_t size = entry_size(entry);
      if (end < p + size + entry->kernel_size)
         break;

      memcpy(p, entry, size);
      p += size;

      void *kernel = (void *) entry + align_u32(size, 64);

      memcpy(p, kernel, entry->kernel_size);
      p += entry->kernel_size;
   }

   *pDataSize = p - pData;

   return VK_SUCCESS;
}

static void
anv_pipeline_cache_merge(struct anv_pipeline_cache *dst,
                         struct anv_pipeline_cache *src)
{
   for (uint32_t i = 0; i < src->table_size; i++) {
      const uint32_t offset = src->hash_table[i];
      if (offset == ~0)
         continue;

      struct cache_entry *entry =
         src->program_stream.block_pool->map + offset;

      if (anv_pipeline_cache_search(dst, entry->sha1, NULL, NULL) != NO_KERNEL)
         continue;

      anv_pipeline_cache_add_entry(dst, entry, offset);
   }
}

VkResult anv_MergePipelineCaches(
    VkDevice                                    _device,
    VkPipelineCache                             destCache,
    uint32_t                                    srcCacheCount,
    const VkPipelineCache*                      pSrcCaches)
{
   ANV_FROM_HANDLE(anv_pipeline_cache, dst, destCache);

   for (uint32_t i = 0; i < srcCacheCount; i++) {
      ANV_FROM_HANDLE(anv_pipeline_cache, src, pSrcCaches[i]);

      anv_pipeline_cache_merge(dst, src);
   }

   return VK_SUCCESS;
}
