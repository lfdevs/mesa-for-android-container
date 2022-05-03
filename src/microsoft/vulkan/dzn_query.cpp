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
#include "vk_debug_report.h"
#include "vk_util.h"

static D3D12_QUERY_HEAP_TYPE
dzn_query_pool_get_heap_type(VkQueryType in)
{
   switch (in) {
   case VK_QUERY_TYPE_OCCLUSION: return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS: return D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
   case VK_QUERY_TYPE_TIMESTAMP: return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
   default: unreachable("Unsupported query type");
   }
}

D3D12_QUERY_TYPE
dzn_query_pool_get_query_type(const dzn_query_pool *qpool,
                              VkQueryControlFlags flags)
{
   switch (qpool->heap_type) {
   case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
      return flags & VK_QUERY_CONTROL_PRECISE_BIT ?
             D3D12_QUERY_TYPE_OCCLUSION : D3D12_QUERY_TYPE_BINARY_OCCLUSION;
   case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS: return D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
   case D3D12_QUERY_HEAP_TYPE_TIMESTAMP: return D3D12_QUERY_TYPE_TIMESTAMP;
   default: unreachable("Unsupported query type");
   }
}

static void
dzn_query_pool_destroy(dzn_query_pool *qpool,
                       const VkAllocationCallbacks *alloc)
{
   if (!qpool)
      return;

   dzn_device *device = container_of(qpool->base.device, dzn_device, vk);

   if (qpool->collect_map)
      qpool->collect_buffer->Unmap(0, NULL);

   if (qpool->collect_buffer)
      qpool->collect_buffer->Release();

   if (qpool->resolve_buffer)
      qpool->resolve_buffer->Release();

   if (qpool->heap)
      qpool->heap->Release();

   for (uint32_t q = 0; q < qpool->query_count; q++) {
      if (qpool->queries[q].fence)
         qpool->queries[q].fence->Release();
   }

   mtx_destroy(&qpool->queries_lock);
   vk_object_base_finish(&qpool->base);
   vk_free2(&device->vk.alloc, alloc, qpool);
}

static VkResult
dzn_query_pool_create(dzn_device *device,
                      const VkQueryPoolCreateInfo *info,
                      const VkAllocationCallbacks *alloc,
                      VkQueryPool *out)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, dzn_query_pool, qpool, 1);
   VK_MULTIALLOC_DECL(&ma, dzn_query, queries, info->queryCount);

   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, alloc,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &qpool->base, VK_OBJECT_TYPE_QUERY_POOL);

   mtx_init(&qpool->queries_lock, mtx_plain);
   qpool->query_count = info->queryCount;
   qpool->queries = queries;

   D3D12_QUERY_HEAP_DESC desc = { 0 };
   qpool->heap_type = desc.Type = dzn_query_pool_get_heap_type(info->queryType);
   desc.Count = info->queryCount;
   desc.NodeMask = 0;

   HRESULT hres =
      device->dev->CreateQueryHeap(&desc, IID_PPV_ARGS(&qpool->heap));
   if (FAILED(hres)) {
      dzn_query_pool_destroy(qpool, alloc);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   switch (info->queryType) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
      qpool->query_size = sizeof(uint64_t);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      qpool->pipeline_statistics = info->pipelineStatistics;
      qpool->query_size = sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
      break;
   default: unreachable("Unsupported query type");
   }

   D3D12_HEAP_PROPERTIES hprops =
      device->dev->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_DEFAULT);
   D3D12_RESOURCE_DESC rdesc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      .Width = info->queryCount * qpool->query_size,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_UNKNOWN,
      .SampleDesc = { .Count = 1, .Quality = 0 },
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
      .Flags = D3D12_RESOURCE_FLAG_NONE,
   };

   hres = device->dev->CreateCommittedResource(&hprops,
                                               D3D12_HEAP_FLAG_NONE,
                                               &rdesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               NULL, IID_PPV_ARGS(&qpool->resolve_buffer));
   if (FAILED(hres)) {
      dzn_query_pool_destroy(qpool, alloc);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   hprops = device->dev->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_READBACK);
   rdesc.Width = info->queryCount * (qpool->query_size + sizeof(uint64_t));
   hres = device->dev->CreateCommittedResource(&hprops,
                                               D3D12_HEAP_FLAG_NONE,
                                               &rdesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               NULL, IID_PPV_ARGS(&qpool->collect_buffer));
   if (FAILED(hres)) {
      dzn_query_pool_destroy(qpool, alloc);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   hres = qpool->collect_buffer->Map(0, NULL, (void **)&qpool->collect_map);
   if (FAILED(hres)) {
      dzn_query_pool_destroy(qpool, alloc);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   memset(qpool->collect_map, 0, rdesc.Width);

   *out = dzn_query_pool_to_handle(qpool);
   return VK_SUCCESS;
}

uint32_t
dzn_query_pool_get_result_offset(const dzn_query_pool *qpool, uint32_t query)
{
   return query * qpool->query_size;
}

uint32_t
dzn_query_pool_get_result_size(const dzn_query_pool *qpool, uint32_t query_count)
{
   return query_count * qpool->query_size;
}

uint32_t
dzn_query_pool_get_availability_offset(const dzn_query_pool *qpool, uint32_t query)
{
   return (qpool->query_count * qpool->query_size) + (sizeof(uint64_t) * query);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateQueryPool(VkDevice device,
                    const VkQueryPoolCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkQueryPool *pQueryPool)
{
   return dzn_query_pool_create(dzn_device_from_handle(device),
                                pCreateInfo, pAllocator, pQueryPool);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyQueryPool(VkDevice device,
                     VkQueryPool queryPool,
                     const VkAllocationCallbacks *pAllocator)
{
   dzn_query_pool_destroy(dzn_query_pool_from_handle(queryPool), pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
dzn_ResetQueryPool(VkDevice device,
                   VkQueryPool queryPool,
                   uint32_t firstQuery,
                   uint32_t queryCount)
{
   VK_FROM_HANDLE(dzn_query_pool, qpool, queryPool);

   mtx_lock(&qpool->queries_lock);
   for (uint32_t q = 0; q < queryCount; q++) {
      dzn_query *query = &qpool->queries[firstQuery + q];

      query->fence_value = 0;
      if (query->fence) {
         query->fence->Release();
         query->fence = NULL;
      }
   }
   mtx_lock(&qpool->queries_lock);

   memset((uint8_t *)qpool->collect_map + dzn_query_pool_get_result_offset(qpool, firstQuery),
          0, queryCount * qpool->query_size);
   memset((uint8_t *)qpool->collect_map + dzn_query_pool_get_availability_offset(qpool, firstQuery),
          0, queryCount * sizeof(uint64_t));
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_GetQueryPoolResults(VkDevice device,
                        VkQueryPool queryPool,
                        uint32_t firstQuery,
                        uint32_t queryCount,
                        size_t dataSize,
                        void *pData,
                        VkDeviceSize stride,
                        VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(dzn_query_pool, qpool, queryPool);

   uint32_t step = (flags & VK_QUERY_RESULT_64_BIT) ?
                   sizeof(uint64_t) : sizeof(uint32_t);
   VkResult result = VK_SUCCESS;

   for (uint32_t q = 0; q < queryCount; q++) {
      dzn_query *query = &qpool->queries[q + firstQuery];

      uint8_t *dst_ptr = (uint8_t *)pData + (stride * q);
      uint8_t *src_ptr =
         (uint8_t *)qpool->collect_map +
         dzn_query_pool_get_result_offset(qpool, firstQuery + q);
      uint64_t available = 0;

      if (flags & VK_QUERY_RESULT_WAIT_BIT) {
         ComPtr<ID3D12Fence> query_fence(NULL);
         uint64_t query_fence_val = 0;

         while (true) {
            mtx_lock(&qpool->queries_lock);
            query_fence = ComPtr<ID3D12Fence>(query->fence);
            query_fence_val = query->fence_value;
            mtx_unlock(&qpool->queries_lock);

            if (query_fence.Get())
               break;

            /* Check again in 10ms.
             * FIXME: decrease the polling period if it happens to hurt latency.
             */
            Sleep(10);
         }

         query_fence->SetEventOnCompletion(query_fence_val, NULL);
         available = UINT64_MAX;
      } else {
         mtx_lock(&qpool->queries_lock);
         ComPtr<ID3D12Fence> query_fence(query->fence);
         uint64_t query_fence_val = query->fence_value;
         mtx_unlock(&qpool->queries_lock);

         if (query_fence.Get() &&
             query_fence->GetCompletedValue() >= query_fence_val)
            available = UINT64_MAX;
      }

      if (qpool->heap_type != D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS) {
         if (available)
            memcpy(dst_ptr, src_ptr, step);
         else if (flags & VK_QUERY_RESULT_PARTIAL_BIT)
            memset(dst_ptr, 0, step);

         dst_ptr += step;
      } else {
         for (uint32_t c = 0; c < sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS) / sizeof(uint64_t); c++) {
            if (!(BITFIELD_BIT(c) & qpool->pipeline_statistics))
               continue;

            if (available)
               memcpy(dst_ptr, src_ptr + (c * sizeof(uint64_t)), step);
            else if (flags & VK_QUERY_RESULT_PARTIAL_BIT)
               memset(dst_ptr, 0, step);

            dst_ptr += step;
         }
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         memcpy(dst_ptr, &available, step);

      if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
         result = VK_NOT_READY;
   }

   return result;
}
