/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dzn_private.h"

#include "vk_alloc.h"

static void
dzn_pipeline_cache_destroy(dzn_pipeline_cache *pcache,
                           const VkAllocationCallbacks *pAllocator)
{
   if (!pcache)
      return;

   dzn_device *device = container_of(pcache->base.device, dzn_device, vk);

   vk_object_base_finish(&pcache->base);
   vk_free2(&device->vk.alloc, pAllocator, pcache);
}

static VkResult
dzn_pipeline_cache_create(dzn_device *device,
                          const VkPipelineCacheCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipelineCache *out)
{
   dzn_pipeline_cache *pcache = (dzn_pipeline_cache *)
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pcache), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pcache)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pcache->base, VK_OBJECT_TYPE_PIPELINE_CACHE);

   /* TODO: cache-ism! */

   *out = dzn_pipeline_cache_to_handle(pcache);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreatePipelineCache(VkDevice device,
                        const VkPipelineCacheCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkPipelineCache *pPipelineCache)
{
   return dzn_pipeline_cache_create(dzn_device_from_handle(device),
                                    pCreateInfo, pAllocator, pPipelineCache);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyPipelineCache(VkDevice device,
                         VkPipelineCache pipelineCache,
                         const VkAllocationCallbacks *pAllocator)
{
   dzn_pipeline_cache_destroy(dzn_pipeline_cache_from_handle(pipelineCache),
                              pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_GetPipelineCacheData(VkDevice device,
                         VkPipelineCache pipelineCache,
                         size_t *pDataSize,
                         void *pData)
{
   // FIXME
   *pDataSize = 0;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_MergePipelineCaches(VkDevice device,
                        VkPipelineCache dstCache,
                        uint32_t srcCacheCount,
                        const VkPipelineCache *pSrcCaches)
{
   // FIXME
   return VK_SUCCESS;
}
