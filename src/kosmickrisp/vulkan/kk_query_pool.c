/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_query_pool.h"

#include "kk_bo.h"
#include "kk_buffer.h"
#include "kk_cmd_buffer.h"
#include "kk_device.h"
#include "kk_encoder.h"
#include "kk_entrypoints.h"
#include "kk_physical_device.h"
#include "kk_query_table.h"
#include "kkcl.h"

struct kk_query_report {
   uint64_t value;
};

static inline bool
kk_has_available(const struct kk_query_pool *pool)
{
   return pool->vk.query_type != VK_QUERY_TYPE_TIMESTAMP;
}

uint16_t *
kk_pool_oq_index_ptr(const struct kk_query_pool *pool)
{
   return (uint16_t *)((uint8_t *)pool->bo->cpu + pool->query_start);
}

static uint32_t
kk_reports_per_query(struct kk_query_pool *pool)
{
   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
      return 1;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      return util_bitcount(pool->vk.pipeline_statistics);
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      // Primitives succeeded and primitives needed
      return 2;
   default:
      UNREACHABLE("Unsupported query type");
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkQueryPool *pQueryPool)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   struct kk_query_pool *pool;
   VkResult result = VK_SUCCESS;

   pool =
      vk_query_pool_create(&dev->vk, pCreateInfo, pAllocator, sizeof(*pool));
   if (!pool)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   bool occlusion = pCreateInfo->queryType == VK_QUERY_TYPE_OCCLUSION;
   unsigned occlusion_queries = occlusion ? pCreateInfo->queryCount : 0;

   /* We place the availability first and then data */
   pool->query_start = 0;
   if (kk_has_available(pool)) {
      pool->query_start = align(pool->vk.query_count * sizeof(uint64_t),
                                sizeof(struct kk_query_report));
   }

   uint32_t reports_per_query = kk_reports_per_query(pool);
   pool->query_stride = reports_per_query * sizeof(struct kk_query_report);

   if (pool->vk.query_count > 0) {
      uint32_t bo_size = pool->query_start;

      /* For occlusion queries, we stick the query index remapping here */
      if (occlusion_queries)
         bo_size += sizeof(uint16_t) * pool->vk.query_count;
      else
         bo_size += pool->query_stride * pool->vk.query_count;

      result = kk_alloc_bo(dev, &dev->vk.base, bo_size, 8, &pool->bo);
      if (result != VK_SUCCESS) {
         kk_DestroyQueryPool(device, kk_query_pool_to_handle(pool), pAllocator);
         return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }

      /* TODO_KOSMICKRISP Timestamps */
   }

   uint16_t *oq_index = kk_pool_oq_index_ptr(pool);

   for (unsigned i = 0; i < occlusion_queries; ++i) {
      uint64_t zero = 0;
      unsigned index;

      VkResult result =
         kk_query_table_add(dev, &dev->occlusion_queries, zero, &index);

      if (result != VK_SUCCESS) {
         kk_DestroyQueryPool(device, kk_query_pool_to_handle(pool), pAllocator);
         return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }

      /* We increment as we go so we can clean up properly if we run out */
      assert(pool->oq_queries < occlusion_queries);
      oq_index[pool->oq_queries++] = index;
   }

   *pQueryPool = kk_query_pool_to_handle(pool);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);

   if (!pool)
      return;

   uint16_t *oq_index = kk_pool_oq_index_ptr(pool);

   for (unsigned i = 0; i < pool->oq_queries; ++i) {
      kk_query_table_remove(dev, &dev->occlusion_queries, oq_index[i]);
   }

   kk_destroy_bo(dev, pool->bo);

   vk_query_pool_destroy(&dev->vk, pAllocator, &pool->vk);
}

static uint64_t *
kk_query_available_map(struct kk_query_pool *pool, uint32_t query)
{
   assert(kk_has_available(pool));
   assert(query < pool->vk.query_count);
   return (uint64_t *)pool->bo->cpu + query;
}

static uint64_t
kk_query_offset(struct kk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return pool->query_start + query * pool->query_stride;
}

static uint64_t
kk_query_report_addr(struct kk_device *dev, struct kk_query_pool *pool,
                     uint32_t query)
{
   if (pool->oq_queries) {
      uint16_t *oq_index = kk_pool_oq_index_ptr(pool);
      return dev->occlusion_queries.bo->gpu +
             (oq_index[query] * sizeof(uint64_t));
   } else {
      return pool->bo->gpu + kk_query_offset(pool, query);
   }
}

static uint64_t
kk_query_available_addr(struct kk_query_pool *pool, uint32_t query)
{
   assert(kk_has_available(pool));
   assert(query < pool->vk.query_count);
   return pool->bo->gpu + query * sizeof(uint64_t);
}

static struct kk_query_report *
kk_query_report_map(struct kk_device *dev, struct kk_query_pool *pool,
                    uint32_t query)
{
   if (pool->oq_queries) {
      uint64_t *queries = (uint64_t *)(dev->occlusion_queries.bo->cpu);
      uint16_t *oq_index = kk_pool_oq_index_ptr(pool);

      return (struct kk_query_report *)&queries[oq_index[query]];
   } else {
      return (void *)((char *)pool->bo->cpu + kk_query_offset(pool, query));
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_ResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                  uint32_t queryCount)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);
   for (uint32_t i = 0; i < queryCount; i++) {
      struct kk_query_report *reports =
         kk_query_report_map(dev, pool, firstQuery + i);

      uint64_t value = 0;
      if (kk_has_available(pool)) {
         uint64_t *available = kk_query_available_map(pool, firstQuery + i);
         *available = 0u;
      } else {
         value = UINT64_MAX;
      }

      for (unsigned j = 0; j < kk_reports_per_query(pool); ++j) {
         reports[j].value = value;
      }
   }
}

/**
 * Goes through a series of consecutive query indices in the given pool,
 * setting all element values to 0 and emitting them as available.
 */
static void
emit_zero_queries(struct kk_cmd_buffer *cmd, struct kk_query_pool *pool,
                  uint32_t first_index, uint32_t num_queries,
                  bool set_available)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   mtl_buffer *buffer = pool->bo->map;

   for (uint32_t i = 0; i < num_queries; i++) {
      uint64_t report = kk_query_report_addr(dev, pool, first_index + i);

      uint64_t value = 0;
      if (kk_has_available(pool)) {
         uint64_t available = kk_query_available_addr(pool, first_index + i);
         kk_cmd_write(cmd, buffer, available, set_available);
      } else {
         value = set_available ? 0u : UINT64_MAX;
      }

      /* XXX: is this supposed to happen on the begin? */
      for (unsigned j = 0; j < kk_reports_per_query(pool); ++j) {
         kk_cmd_write(cmd, buffer,
                      report + (j * sizeof(struct kk_query_report)), value);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                     uint32_t firstQuery, uint32_t queryCount)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);
   emit_zero_queries(cmd, pool, firstQuery, queryCount, false);
   /* If we are not mid encoder, just upload the writes */
   if (cmd->encoder->main.last_used == KK_ENC_NONE)
      upload_queue_writes(cmd);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                      VkPipelineStageFlags2 stage, VkQueryPool queryPool,
                      uint32_t query)
{
   /* TODO_KOSMICKRISP */
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                 uint32_t query, VkQueryControlFlags flags)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);
   cmd->state.gfx.occlusion.mode = flags & VK_QUERY_CONTROL_PRECISE_BIT
                                      ? MTL_VISIBILITY_RESULT_MODE_COUNTING
                                      : MTL_VISIBILITY_RESULT_MODE_BOOLEAN;
   cmd->state.gfx.dirty |= KK_DIRTY_OCCLUSION;
   uint16_t *oq_index = kk_pool_oq_index_ptr(pool);
   cmd->state.gfx.occlusion.index = oq_index[query];
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
               uint32_t query)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);
   cmd->state.gfx.occlusion.mode = MTL_VISIBILITY_RESULT_MODE_DISABLED;
   cmd->state.gfx.dirty |= KK_DIRTY_OCCLUSION;

   /* Make the query available */
   uint64_t addr = kk_query_available_addr(pool, query);
   kk_cmd_write(cmd, pool->bo->map, addr, true);
}

static bool
kk_query_is_available(struct kk_device *dev, struct kk_query_pool *pool,
                      uint32_t query)
{
   if (kk_has_available(pool)) {
      uint64_t *available = kk_query_available_map(pool, query);
      return p_atomic_read(available) != 0;
   } else {
      const struct kk_query_report *report =
         kk_query_report_map(dev, pool, query);

      return report->value != UINT64_MAX;
   }
}

#define KK_QUERY_TIMEOUT 2000000000ull

static VkResult
kk_query_wait_for_available(struct kk_device *dev, struct kk_query_pool *pool,
                            uint32_t query)
{
   uint64_t abs_timeout_ns = os_time_get_absolute_timeout(KK_QUERY_TIMEOUT);

   while (os_time_get_nano() < abs_timeout_ns) {
      if (kk_query_is_available(dev, pool, query))
         return VK_SUCCESS;

      VkResult status = vk_device_check_status(&dev->vk);
      if (status != VK_SUCCESS)
         return status;
   }

   return vk_device_set_lost(&dev->vk, "query timeout");
}

static void
cpu_write_query_result(void *dst, uint32_t idx, VkQueryResultFlags flags,
                       uint64_t result)
{
   if (flags & VK_QUERY_RESULT_64_BIT) {
      uint64_t *dst64 = dst;
      dst64[idx] = result;
   } else {
      uint32_t *dst32 = dst;
      dst32[idx] = result;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool,
                       uint32_t firstQuery, uint32_t queryCount,
                       size_t dataSize, void *pData, VkDeviceSize stride,
                       VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);

   if (vk_device_is_lost(&dev->vk))
      return VK_ERROR_DEVICE_LOST;

   VkResult status = VK_SUCCESS;
   for (uint32_t i = 0; i < queryCount; i++) {
      const uint32_t query = firstQuery + i;

      bool available = kk_query_is_available(dev, pool, query);

      if (!available && (flags & VK_QUERY_RESULT_WAIT_BIT)) {
         status = kk_query_wait_for_available(dev, pool, query);
         if (status != VK_SUCCESS)
            return status;

         available = true;
      }

      bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

      const struct kk_query_report *src = kk_query_report_map(dev, pool, query);
      assert(i * stride < dataSize);
      void *dst = (char *)pData + i * stride;

      uint32_t reports = kk_reports_per_query(pool);
      if (write_results) {
         for (uint32_t j = 0; j < reports; j++) {
            cpu_write_query_result(dst, j, flags, src[j].value);
         }
      }

      if (!write_results)
         status = VK_NOT_READY;

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cpu_write_query_result(dst, reports, flags, available);
   }

   return status;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                           uint32_t firstQuery, uint32_t queryCount,
                           VkBuffer dstBuffer, VkDeviceSize dstOffset,
                           VkDeviceSize stride, VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);
   VK_FROM_HANDLE(kk_buffer, dst_buf, dstBuffer);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct kk_copy_query_pool_results_info info = {
      .availability = kk_has_available(pool) ? pool->bo->gpu : 0,
      .results = pool->oq_queries ? dev->occlusion_queries.bo->gpu
                                  : pool->bo->gpu + pool->query_start,
      .indices = pool->oq_queries ? pool->bo->gpu + pool->query_start : 0,
      .dst_addr = dst_buf->vk.device_address + dstOffset,
      .dst_stride = stride,
      .first_query = firstQuery,
      .flags = flags,
      .reports_per_query = kk_reports_per_query(pool),
      .query_count = queryCount,
   };

   util_dynarray_append(&cmd->encoder->copy_query_pool_result_infos, info);
   /* If we are not mid encoder, just upload the writes */
   if (cmd->encoder->main.last_used == KK_ENC_NONE)
      upload_queue_writes(cmd);
}
