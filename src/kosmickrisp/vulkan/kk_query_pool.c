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
#include "kk_entrypoints.h"
#include "kk_physical_device.h"
#include "kk_query_table.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

struct kk_query_report {
   uint64_t value;
};

uint16_t *
kk_pool_index_ptr(const struct kk_query_pool *pool)
{
   return (uint16_t *)((uint8_t *)pool->bo->cpu + pool->index_start);
}

static uint32_t
kk_reports_per_query(struct kk_query_pool *pool)
{
   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
      return 1;
   default:
      UNREACHABLE("Unsupported query type");
   }
}

static uint32_t *
kk_query_available_map(struct kk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return (uint32_t *)pool->bo->cpu + query;
}

static uint64_t
kk_query_offset(struct kk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return pool->query_start + query * pool->query_stride;
}

static inline bool
kk_pool_is_oq(struct kk_query_pool *pool)
{
   return pool->vk.query_type == VK_QUERY_TYPE_OCCLUSION;
}

static inline bool
kk_pool_is_ts(struct kk_query_pool *pool)
{
   return pool->vk.query_type == VK_QUERY_TYPE_TIMESTAMP;
}

static uint64_t
kk_query_report_addr(struct kk_device *dev, struct kk_query_pool *pool,
                     uint32_t query)
{
   struct kk_bo *bo =
      kk_pool_is_oq(pool) ? dev->occlusion_queries.bo : pool->bo;

   uint16_t *remap_index = kk_pool_index_ptr(pool);
   return bo->gpu + pool->query_start + (remap_index[query] * sizeof(uint64_t));
}

static uint64_t
kk_query_available_addr(struct kk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return pool->bo->gpu + query * sizeof(uint32_t);
}

static struct kk_query_report *
kk_query_report_map(struct kk_device *dev, struct kk_query_pool *pool,
                    uint32_t query)
{
   struct kk_bo *bo =
      kk_pool_is_oq(pool) ? dev->occlusion_queries.bo : pool->bo;

   uint64_t *queries = (uint64_t *)(bo->cpu + pool->query_start);
   uint16_t *remap_index = kk_pool_index_ptr(pool);

   return (struct kk_query_report *)&queries[remap_index[query]];
}

static void
host_zero_queries(struct kk_device *dev, struct kk_query_pool *pool,
                  uint32_t first_index, uint32_t num_queries,
                  bool set_available)
{
   for (uint32_t i = 0; i < num_queries; i++) {
      struct kk_query_report *reports =
         kk_query_report_map(dev, pool, first_index + i);

      uint32_t *available = kk_query_available_map(pool, first_index + i);
      *available = set_available;

      for (unsigned j = 0; j < kk_reports_per_query(pool); ++j) {
         reports[j].value = 0;
      }
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

   /* VUID-VkQueryPoolCreateInfo-queryCount-02763: queryCount must be greater
    * than 0 */
   assert(pool->vk.query_count > 0);

   /* We place the availability, then index, and then data (if in this buffer) */
   pool->index_start = align(pool->vk.query_count * sizeof(uint32_t),
                             sizeof(struct kk_query_report));
   uint32_t bo_size =
      align(pool->index_start + sizeof(uint16_t) * pool->vk.query_count,
            sizeof(struct kk_query_report));

   uint32_t reports_per_query = kk_reports_per_query(pool);
   pool->query_stride = reports_per_query * sizeof(struct kk_query_report);

   /* For occlusion queries, results come from the global visibility buffer */
   if (kk_pool_is_oq(pool)) {
      pool->query_start = 0;
   } else {
      pool->query_start = bo_size;
      bo_size += pool->query_stride * pool->vk.query_count;
   }

   result = kk_alloc_bo(dev, &dev->vk.base, bo_size, 8, &pool->bo);
   if (result != VK_SUCCESS) {
      kk_DestroyQueryPool(device, kk_query_pool_to_handle(pool), pAllocator);
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   uint16_t *remap_index = kk_pool_index_ptr(pool);
   if (kk_pool_is_oq(pool)) {

      for (unsigned i = 0; i < pool->vk.query_count; ++i) {
         uint64_t zero = 0;
         unsigned index;

         VkResult result =
            kk_query_table_add(dev, &dev->occlusion_queries, zero, &index);

         if (result != VK_SUCCESS) {
            kk_DestroyQueryPool(device, kk_query_pool_to_handle(pool),
                                pAllocator);
            return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         }

         /* We increment as we go so we can clean up properly if we run out */
         remap_index[pool->oq.queries++] = index;
      }
   } else if (kk_pool_is_ts(pool)) {
      /* Timestamps are sampled into a Metal counter heap (one entry per query)
       * and resolved into `bo` after the GPU writes them. */
      pool->ts.heap =
         mtl_new_timestamp_counter_heap(dev->mtl_handle, pool->vk.query_count);
      if (pool->ts.heap == NULL) {
         kk_DestroyQueryPool(device, kk_query_pool_to_handle(pool), pAllocator);
         return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }
      pool->ts.stage_map = UTIL_DYNARRAY_INIT;

      /* set up default mapping for unique queries */
      for (unsigned i = 0; i < pool->vk.query_count; ++i) {
         remap_index[i] = i;
      }
   }

   if (pCreateInfo->flags & VK_QUERY_POOL_CREATE_RESET_BIT_KHR)
      host_zero_queries(dev, pool, 0, pool->vk.query_count, false);

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

   if (kk_pool_is_oq(pool)) {
      uint16_t *remap_index = kk_pool_index_ptr(pool);
      for (unsigned i = 0; i < pool->oq.queries; ++i) {
         kk_query_table_remove(dev, &dev->occlusion_queries, remap_index[i]);
      }
   } else if (kk_pool_is_ts(pool)) {
      if (pool->ts.heap)
         mtl_release(pool->ts.heap);
      util_dynarray_fini(&pool->ts.stage_map);
   }

   kk_destroy_bo(dev, pool->bo);

   vk_query_pool_destroy(&dev->vk, pAllocator, &pool->vk);
}

VKAPI_ATTR void VKAPI_CALL
kk_ResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                  uint32_t queryCount)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);

   host_zero_queries(dev, pool, firstQuery, queryCount, false);
}

static void
emit_zero_queries(struct kk_cmd_buffer *cmd, struct kk_query_pool *pool,
                  uint32_t first_index, uint32_t num_queries,
                  bool set_available)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   struct kk_bo *results_bo =
      kk_pool_is_oq(pool) ? dev->occlusion_queries.bo : pool->bo;

   struct libkk_reset_query_args info = {
      .availability = pool->bo->gpu,
      .results = results_bo->gpu + pool->query_start,
      .oq_index = pool->bo->gpu + pool->index_start,

      .first_query = first_index,
      .reports_per_query = kk_reports_per_query(pool),
      .set_available = set_available,
   };
   libkk_reset_query_struct(cmd, kk_grid_1d(num_queries), false, info);
}

static uint32_t
kk_mv_query_count(struct kk_cmd_buffer *cmd)
{
   struct kk_rendering_state *render = &cmd->state.gfx.render;
   return cmd->gfx.encoder && render->view_mask
             ? util_bitcount(render->view_mask)
             : 1;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                     uint32_t firstQuery, uint32_t queryCount)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);

   assert(cmd->gfx.encoder == NULL);

   /* A prior timestamp write may have a resolve still pending. Land it before
    * the reset zeroes the pool BO, otherwise the deferred resolve would clobber
    * the reset and leave the query spuriously available. */
   if (util_dynarray_num_elements(&cmd->pre_gfx->ts_resolves,
                                  struct kk_ts_resolve) > 0)
      cs_end(cmd);

   emit_zero_queries(cmd, pool, firstQuery, queryCount, false);
}

typedef struct {
   VkPipelineStageFlags2 vk_flags;
   enum mtl_render_stages stage;
} StageMapping;

/* This table is sorted in reverse pipeline order so that we pick the latest
 * metal stage if given a mask of more than one vulkan stage */
static const StageMapping stage_lut[] = {
   {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
       VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    MTL_RENDER_STAGE_TILE},

   {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
       VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    MTL_RENDER_STAGE_FRAGMENT},

   {VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, MTL_RENDER_STAGE_MESH},

   {VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT, MTL_RENDER_STAGE_OBJECT},

   {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
       VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
       VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
       VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
       VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT,
    MTL_RENDER_STAGE_VERTEX},
};

static enum mtl_render_stages
kk_pipeline_stages_to_mtl_render_stage(VkPipelineStageFlags2 vk_flags)
{
   if (vk_flags == VK_PIPELINE_STAGE_2_NONE) {
      return 0;
   }

   for (size_t i = 0; i < ARRAY_SIZE(stage_lut); ++i) {
      if (vk_flags & stage_lut[i].vk_flags) {
         return stage_lut[i].stage;
      }
   }
   return 0;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                      VkPipelineStageFlags2 stage, VkQueryPool queryPool,
                      uint32_t query)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   assert(kk_pool_is_ts(pool) && pool->ts.heap);

   uint32_t count = kk_mv_query_count(cmd);
   enum mtl_render_stages mtl_stage =
      kk_pipeline_stages_to_mtl_render_stage(stage);

   /* The sampled value lives in the counter heap; queue a resolve into the pool
    * BO (overwriting the unavailable sentinel) so both the host and GPU result
    * paths can read it. Flushed on the GPU timeline at cs_end. */
   struct kk_ts_resolve resolve = {
      .heap = pool->ts.heap,
      .index = query,
      .dst_addr = kk_query_report_addr(dev, pool, query),
   };

   /* non-gfx or not found*/
   if (cmd->gfx.encoder && mtl_stage) {
      uint64_t addr = kk_query_available_addr(pool, query);
      for (uint32_t i = 0; i < count; i++) {
         libkk_write_u32(cmd, kk_grid_1d(1), false, addr, true);
         addr += sizeof(uint32_t);
      }

      /* If we've already issued a timestamp write for a render stage, reuse it
       * because reissuing might return a 0 timestamp
       */
      util_dynarray_foreach(&pool->ts.stage_map, struct kk_ts_stage_entry,
                            entry) {
         if (entry->stage == mtl_stage && entry->pass == cmd->gfx.encoder) {
            uint16_t *remap_index = kk_pool_index_ptr(pool);
            remap_index[query] = entry->index;
            return;
         }
      }
      struct kk_ts_stage_entry entry = {.stage = mtl_stage,
                                        .pass = cmd->gfx.encoder,
                                        .index = query};
      util_dynarray_append(&pool->ts.stage_map, entry);

      mtl_render_write_timestamp(cmd->gfx.encoder, mtl_stage, pool->ts.heap,
                                 query);
      util_dynarray_append(&cmd->gfx.ts_resolves, resolve);
   } else {
      bool top = true;
      struct kk_encoder_state *es;

      const VkPipelineStageFlagBits2 bottom_mask =
         VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
      if (cmd->gfx.encoder && ((stage & bottom_mask) != 0)) {
         top = false;
      }
      es = top ? cmd->pre_gfx : cmd->post_gfx;

      /* write the availability markers. For compute, this must happen before the
       * timestamp write to ensure that there is compute work to trigger it. */
      uint64_t addr = kk_query_available_addr(pool, query);
      for (uint32_t i = 0; i < count; i++) {
         libkk_write_u32(cmd, kk_grid_1d(1), top, addr, true);
         addr += sizeof(uint32_t);
      }

      mtl_compute_write_timestamp(es->encoder, pool->ts.heap, query);
      util_dynarray_append(&es->ts_resolves, resolve);
   }
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
   uint16_t *remap_index = kk_pool_index_ptr(pool);
   cmd->state.gfx.occlusion.index = remap_index[query];
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
               uint32_t query)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_query_pool, pool, queryPool);
   cmd->state.gfx.occlusion.mode = MTL_VISIBILITY_RESULT_MODE_DISABLED;
   cmd->state.gfx.dirty |= KK_DIRTY_OCCLUSION;

   /* Make the query available. The Vulkan spec states:
    * If queries are used while executing a render pass instance that has
    * multiview enabled, the query uses N consecutive query indices in the
    * query pool (starting at query) where N is the number of bits set in the
    * view mask in the subpass the query is used in. How the numerical
    * results of the query are distributed among the queries is
    * implementation-dependent.
    * ...
    * Queries used with multiview rendering must not span subpasses, i.e.
    * they must begin and end in the same subpass.
    */
   uint64_t addr = kk_query_available_addr(pool, query);
   uint32_t count = kk_mv_query_count(cmd);
   for (uint32_t i = 0; i < count; i++) {
      kk_cmd_write(cmd, (struct libkk_imm_write){addr, true});
      addr += sizeof(uint32_t);
   }
}

static bool
kk_query_is_available(struct kk_device *dev, struct kk_query_pool *pool,
                      uint32_t query)
{
   uint32_t *available = kk_query_available_map(pool, query);
   return p_atomic_read(available) != 0;
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

   /* Timestamp results are resolved into the pool BO on a deferred command
    * buffer; make sure any pending resolve lands before this copy reads it.
    * This also gives VK_QUERY_RESULT_WAIT_BIT its meaning for timestamps. */
   if (util_dynarray_num_elements(&cmd->pre_gfx->ts_resolves,
                                  struct kk_ts_resolve) > 0)
      cs_end(cmd);

   mtl_compute_encoder *encoder = cs_get_compute(cmd, true);
   /* The resolveCounterHeap runs on the blit stage and it needs to be available
    * for the compute job to copy results to the bo. */
   mtl_barrier_after_queue_stages(encoder, MTL_STAGE_BLIT, MTL_STAGE_DISPATCH);

   struct kk_bo *results_bo =
      kk_pool_is_oq(pool) ? dev->occlusion_queries.bo : pool->bo;

   struct libkk_copy_queries_args args = {
      .availability = pool->bo->gpu,
      .results = results_bo->gpu + pool->query_start,
      .oq_index = pool->bo->gpu + pool->index_start,
      .dst_addr = dst_buf->vk.device_address + dstOffset,
      .dst_stride = stride,
      .first_query = firstQuery,
      .flags = flags,
      .reports_per_query = kk_reports_per_query(pool),
   };
   libkk_copy_queries_struct(cmd, kk_grid_1d(queryCount), false, args);
}
