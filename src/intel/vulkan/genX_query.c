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

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

/* We reserve :
 *    - GPR 14 for perf queries
 *    - GPR 15 for conditional rendering
 */
#define GEN_MI_BUILDER_NUM_ALLOC_GPRS 14
#define GEN_MI_BUILDER_CAN_WRITE_BATCH GEN_GEN >= 8
#define __gen_get_batch_dwords anv_batch_emit_dwords
#define __gen_address_offset anv_address_add
#define __gen_get_batch_address(b, a) anv_address_physical(anv_batch_address(b, a))
#include "common/gen_mi_builder.h"
#include "perf/gen_perf.h"
#include "perf/gen_perf_mdapi.h"

#define OA_REPORT_N_UINT64 (256 / sizeof(uint64_t))

#include "vk_util.h"

static struct anv_address
anv_query_address(struct anv_query_pool *pool, uint32_t query)
{
   return (struct anv_address) {
      .bo = pool->bo,
      .offset = query * pool->stride,
   };
}

VkResult genX(CreateQueryPool)(
    VkDevice                                    _device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   const struct anv_physical_device *pdevice = device->physical;
   const VkQueryPoolPerformanceCreateInfoKHR *perf_query_info = NULL;
   struct anv_query_pool *pool;
   struct gen_perf_counter_pass *counter_pass;
   struct gen_perf_query_info **pass_query;
   ANV_MULTIALLOC(ma);
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);

   /* Query pool slots are made up of some number of 64-bit values packed
    * tightly together. For most query types have the first 64-bit value is
    * the "available" bit which is 0 when the query is unavailable and 1 when
    * it is available. The 64-bit values that follow are determined by the
    * type of query.
    *
    * For performance queries, we have a requirement to align OA reports at
    * 64bytes so we put those first and have the "available" bit behind
    * together with some other counters.
    */
   uint32_t uint64s_per_slot = 0;
   UNUSED uint32_t n_passes = 0;

   anv_multialloc_add(&ma, &pool, 1);

   VkQueryPipelineStatisticFlags pipeline_statistics = 0;
   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION:
      /* Occlusion queries have two values: begin and end. */
      uint64s_per_slot = 1 + 2;
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
      /* Timestamps just have the one timestamp value */
      uint64s_per_slot = 1 + 1;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      pipeline_statistics = pCreateInfo->pipelineStatistics;
      /* We're going to trust this field implicitly so we need to ensure that
       * no unhandled extension bits leak in.
       */
      pipeline_statistics &= ANV_PIPELINE_STATISTICS_MASK;

      /* Statistics queries have a min and max for every statistic */
      uint64s_per_slot = 1 + 2 * util_bitcount(pipeline_statistics);
      break;
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      /* Transform feedback queries are 4 values, begin/end for
       * written/available.
       */
      uint64s_per_slot = 1 + 4;
      break;
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL:
      uint64s_per_slot = 72; /* 576 bytes, see layout below */
      break;
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR:
      perf_query_info = vk_find_struct_const(pCreateInfo->pNext,
                                             QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR);
      n_passes = gen_perf_get_n_passes(pdevice->perf,
                                       perf_query_info->pCounterIndices,
                                       perf_query_info->counterIndexCount,
                                       NULL);
      anv_multialloc_add(&ma, &counter_pass, perf_query_info->counterIndexCount);
      anv_multialloc_add(&ma, &pass_query, n_passes);
      STATIC_ASSERT(ANV_KHR_PERF_QUERY_SIZE % sizeof(uint64_t) == 0);
      uint64s_per_slot = (ANV_KHR_PERF_QUERY_SIZE / sizeof(uint64_t)) * n_passes;
      break;
   default:
      assert(!"Invalid query type");
   }

   if (!anv_multialloc_alloc2(&ma, &device->vk.alloc,
                              pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pool->base, VK_OBJECT_TYPE_QUERY_POOL);
   pool->type = pCreateInfo->queryType;
   pool->pipeline_statistics = pipeline_statistics;
   pool->stride = uint64s_per_slot * sizeof(uint64_t);
   pool->slots = pCreateInfo->queryCount;

   if (pool->type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      pool->n_counters = perf_query_info->counterIndexCount;
      pool->counter_pass = counter_pass;
      gen_perf_get_counters_passes(pdevice->perf,
                                   perf_query_info->pCounterIndices,
                                   perf_query_info->counterIndexCount,
                                   pool->counter_pass);
      pool->n_passes = n_passes;
      pool->pass_query = pass_query;
      gen_perf_get_n_passes(pdevice->perf,
                            perf_query_info->pCounterIndices,
                            perf_query_info->counterIndexCount,
                            pool->pass_query);
   }

   uint32_t bo_flags = 0;
   if (pdevice->supports_48bit_addresses)
      bo_flags |= EXEC_OBJECT_SUPPORTS_48B_ADDRESS;

   if (pdevice->use_softpin)
      bo_flags |= EXEC_OBJECT_PINNED;

   if (pdevice->has_exec_async)
      bo_flags |= EXEC_OBJECT_ASYNC;

   uint64_t size = pool->slots * pool->stride;
   result = anv_device_alloc_bo(device, size,
                                ANV_BO_ALLOC_MAPPED |
                                ANV_BO_ALLOC_SNOOPED,
                                0 /* explicit_address */,
                                &pool->bo);
   if (result != VK_SUCCESS)
      goto fail;

   if (pool->type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      for (uint32_t p = 0; p < pool->n_passes; p++) {
         struct gen_mi_builder b;
         struct anv_batch batch = {
            .start = pool->bo->map + ANV_KHR_PERF_QUERY_SIZE * p + 8,
            .end = pool->bo->map + ANV_KHR_PERF_QUERY_SIZE * p + 64,
         };
         batch.next = batch.start;

         gen_mi_builder_init(&b, &batch);
         gen_mi_store(&b, gen_mi_reg64(ANV_PERF_QUERY_OFFSET_REG),
                      gen_mi_imm(p * ANV_KHR_PERF_QUERY_SIZE));
         anv_batch_emit(&batch, GENX(MI_BATCH_BUFFER_END), bbe);
         assert(batch.next <= (pool->bo->map + ANV_KHR_PERF_QUERY_SIZE * p + 64));
      }
   }

   *pQueryPool = anv_query_pool_to_handle(pool);

   return VK_SUCCESS;

 fail:
   vk_free2(&device->vk.alloc, pAllocator, pool);

   return result;
}

void genX(DestroyQueryPool)(
    VkDevice                                    _device,
    VkQueryPool                                 _pool,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_query_pool, pool, _pool);

   if (!pool)
      return;

   anv_device_release_bo(device, pool->bo);
   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

/**
 * VK_KHR_performance_query layout (576 bytes * number of passes) :
 *
 * -----------------------------------------
 * |       availability (8b)    | |        |
 * |----------------------------| |        |
 * |     Small batch loading    | |        |
 * |  ANV_PERF_QUERY_OFFSET_REG | |        |
 * |          (56b)             | | Pass 0 |
 * |----------------------------| |        |
 * |     begin MI_RPC (256b)    | |        |
 * |----------------------------| |        |
 * |       end MI_RPC (256b)    | |        |
 * |----------------------------|--        | Query 0
 * |       availability (8b)    | |        |
 * |----------------------------| |        |
 * |     Small batch loading    | |        |
 * |  ANV_PERF_QUERY_OFFSET_REG | |        |
 * |          (56b)             | | Pass 1 |
 * |----------------------------| |        |
 * |     begin MI_RPC (256b)    | |        |
 * |----------------------------| |        |
 * |       end MI_RPC (256b)    | |        |
 * |----------------------------|-----------
 * |       availability (8b)    | |        |
 * |----------------------------| |        |
 * |        Unused (48b)        | |        |
 * |----------------------------| | Pass 0 |
 * |     begin MI_RPC (256b)    | |        |
 * |----------------------------| |        | Query 1
 * |       end MI_RPC (256b)    | |        |
 * |----------------------------|--        |
 * |             ...            | |        |
 * -----------------------------------------
 */
UNUSED static uint64_t
khr_perf_query_availability_offset(struct anv_query_pool *pool, uint32_t query, uint32_t pass)
{
   return query * (pool->n_passes * ANV_KHR_PERF_QUERY_SIZE) +
      pass * ANV_KHR_PERF_QUERY_SIZE;
}

UNUSED static uint64_t
khr_perf_query_oa_offset(struct anv_query_pool *pool, uint32_t query, uint32_t pass, bool end)
{
   return query * (pool->n_passes * ANV_KHR_PERF_QUERY_SIZE) +
      pass * ANV_KHR_PERF_QUERY_SIZE +
      64 + (end ? OA_SNAPSHOT_SIZE : 0);
}

UNUSED static struct anv_address
khr_perf_query_availability_address(struct anv_query_pool *pool, uint32_t query, uint32_t pass)
{
   return anv_address_add(
      (struct anv_address) { .bo = pool->bo, },
      khr_perf_query_availability_offset(pool, query, pass));
}

UNUSED static struct anv_address
khr_perf_query_oa_address(struct anv_query_pool *pool, uint32_t query, uint32_t pass, bool end)
{
   return anv_address_add(
      (struct anv_address) { .bo = pool->bo, },
      khr_perf_query_oa_offset(pool, query, pass, end));
}


/**
 * VK_INTEL_performance_query layout (576 bytes) :
 *
 * ------------------------------
 * |       availability (8b)    |
 * |----------------------------|
 * |         marker (8b)        |
 * |----------------------------|
 * | begin RPSTAT register (4b) |
 * |----------------------------|
 * |  end RPSTAT register (4b)  |
 * |----------------------------|
 * | begin perfcntr 1 & 2 (16b) |
 * |----------------------------|
 * |  end perfcntr 1 & 2 (16b)  |
 * |----------------------------|
 * |          Unused (8b)       |
 * |----------------------------|
 * |     begin MI_RPC (256b)    |
 * |----------------------------|
 * |       end MI_RPC (256b)    |
 * ------------------------------
 */

static uint32_t
intel_perf_marker_offset(void)
{
   return 8;
}

static uint32_t
intel_perf_rpstart_offset(bool end)
{
   return 16 + (end ? sizeof(uint32_t) : 0);
}

#if GEN_GEN >= 8 && GEN_GEN <= 11
static uint32_t
intel_perf_counter(bool end)
{
   return 24 + (end ? (2 * sizeof(uint64_t)) : 0);
}
#endif

static uint32_t
intel_perf_mi_rpc_offset(bool end)
{
   return 64 + (end ? 256 : 0);
}

static void
cpu_write_query_result(void *dst_slot, VkQueryResultFlags flags,
                       uint32_t value_index, uint64_t result)
{
   if (flags & VK_QUERY_RESULT_64_BIT) {
      uint64_t *dst64 = dst_slot;
      dst64[value_index] = result;
   } else {
      uint32_t *dst32 = dst_slot;
      dst32[value_index] = result;
   }
}

static void *
query_slot(struct anv_query_pool *pool, uint32_t query)
{
   return pool->bo->map + query * pool->stride;
}

static bool
query_is_available(struct anv_query_pool *pool, uint32_t query)
{
   if (pool->type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      for (uint32_t p = 0; p < pool->n_passes; p++) {
         volatile uint64_t *slot =
            pool->bo->map + khr_perf_query_availability_offset(pool, query, p);
         if (!slot[0])
            return false;
      }
      return true;
   } else {
      return *(volatile uint64_t *)query_slot(pool, query);
   }
}

static VkResult
wait_for_available(struct anv_device *device,
                   struct anv_query_pool *pool, uint32_t query)
{
   uint64_t abs_timeout = anv_get_absolute_timeout(5 * NSEC_PER_SEC);

   while (anv_gettime_ns() < abs_timeout) {
      if (query_is_available(pool, query))
         return VK_SUCCESS;
      VkResult status = anv_device_query_status(device);
      if (status != VK_SUCCESS)
         return status;
   }

   return anv_device_set_lost(device, "query timeout");
}

VkResult genX(GetQueryPoolResults)(
    VkDevice                                    _device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   assert(pool->type == VK_QUERY_TYPE_OCCLUSION ||
          pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS ||
          pool->type == VK_QUERY_TYPE_TIMESTAMP ||
          pool->type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT ||
          pool->type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR ||
          pool->type == VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL);

   if (anv_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   if (pData == NULL)
      return VK_SUCCESS;

   void *data_end = pData + dataSize;

   VkResult status = VK_SUCCESS;
   for (uint32_t i = 0; i < queryCount; i++) {
      bool available = query_is_available(pool, firstQuery + i);

      if (!available && (flags & VK_QUERY_RESULT_WAIT_BIT)) {
         status = wait_for_available(device, pool, firstQuery + i);
         if (status != VK_SUCCESS)
            return status;

         available = true;
      }

      /* From the Vulkan 1.0.42 spec:
       *
       *    "If VK_QUERY_RESULT_WAIT_BIT and VK_QUERY_RESULT_PARTIAL_BIT are
       *    both not set then no result values are written to pData for
       *    queries that are in the unavailable state at the time of the call,
       *    and vkGetQueryPoolResults returns VK_NOT_READY. However,
       *    availability state is still written to pData for those queries if
       *    VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set."
       *
       * From VK_KHR_performance_query :
       *
       *    "VK_QUERY_RESULT_PERFORMANCE_QUERY_RECORDED_COUNTERS_BIT_KHR specifies
       *     that the result should contain the number of counters that were recorded
       *     into a query pool of type ename:VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR"
       */
      bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

      uint32_t idx = 0;
      switch (pool->type) {
      case VK_QUERY_TYPE_OCCLUSION: {
         uint64_t *slot = query_slot(pool, firstQuery + i);
         if (write_results) {
            /* From the Vulkan 1.2.132 spec:
             *
             *    "If VK_QUERY_RESULT_PARTIAL_BIT is set,
             *    VK_QUERY_RESULT_WAIT_BIT is not set, and the query’s status
             *    is unavailable, an intermediate result value between zero and
             *    the final result value is written to pData for that query."
             */
            uint64_t result = available ? slot[2] - slot[1] : 0;
            cpu_write_query_result(pData, flags, idx, result);
         }
         idx++;
         break;
      }

      case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
         uint64_t *slot = query_slot(pool, firstQuery + i);
         uint32_t statistics = pool->pipeline_statistics;
         while (statistics) {
            uint32_t stat = u_bit_scan(&statistics);
            if (write_results) {
               uint64_t result = slot[idx * 2 + 2] - slot[idx * 2 + 1];

               /* WaDividePSInvocationCountBy4:HSW,BDW */
               if ((device->info.gen == 8 || device->info.is_haswell) &&
                   (1 << stat) == VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT)
                  result >>= 2;

               cpu_write_query_result(pData, flags, idx, result);
            }
            idx++;
         }
         assert(idx == util_bitcount(pool->pipeline_statistics));
         break;
      }

      case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
         uint64_t *slot = query_slot(pool, firstQuery + i);
         if (write_results)
            cpu_write_query_result(pData, flags, idx, slot[2] - slot[1]);
         idx++;
         if (write_results)
            cpu_write_query_result(pData, flags, idx, slot[4] - slot[3]);
         idx++;
         break;
      }

      case VK_QUERY_TYPE_TIMESTAMP: {
         uint64_t *slot = query_slot(pool, firstQuery + i);
         if (write_results)
            cpu_write_query_result(pData, flags, idx, slot[1]);
         idx++;
         break;
      }

#if GEN_GEN >= 8
      case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR: {
         const struct anv_physical_device *pdevice = device->physical;
         assert((flags & (VK_QUERY_RESULT_WITH_AVAILABILITY_BIT |
                          VK_QUERY_RESULT_PARTIAL_BIT)) == 0);
         for (uint32_t p = 0; p < pool->n_passes; p++) {
            const uint32_t *begin = pool->bo->map + khr_perf_query_oa_offset(pool, firstQuery + i, p, false);
            const uint32_t *end = pool->bo->map + khr_perf_query_oa_offset(pool, firstQuery + i, p, true);
            struct gen_perf_query_result result;
            gen_perf_query_result_clear(&result);
            gen_perf_query_result_accumulate(&result, pool->pass_query[p], begin, end);
            anv_perf_write_pass_results(pdevice->perf, pool, p, &result, pData);
         }
         break;
      }
#endif

      case VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL: {
         if (!write_results)
            break;
         const void *query_data = query_slot(pool, firstQuery + i);
         const uint32_t *oa_begin = query_data + intel_perf_mi_rpc_offset(false);
         const uint32_t *oa_end = query_data + intel_perf_mi_rpc_offset(true);
         const uint32_t *rpstat_begin = query_data + intel_perf_rpstart_offset(false);
         const uint32_t *rpstat_end = query_data + intel_perf_mi_rpc_offset(true);
         struct gen_perf_query_result result;
         uint32_t core_freq[2];
#if GEN_GEN < 9
         core_freq[0] = ((*rpstat_begin >> 7) & 0x7f) * 1000000ULL;
         core_freq[1] = ((*rpstat_end >> 7) & 0x7f) * 1000000ULL;
#else
         core_freq[0] = ((*rpstat_begin >> 23) & 0x1ff) * 1000000ULL;
         core_freq[1] = ((*rpstat_end >> 23) & 0x1ff) * 1000000ULL;
#endif
         gen_perf_query_result_clear(&result);
         gen_perf_query_result_accumulate(&result, &device->physical->perf->queries[0],
                                          oa_begin, oa_end);
         gen_perf_query_result_read_frequencies(&result, &device->info,
                                                oa_begin, oa_end);
         gen_perf_query_result_write_mdapi(pData, stride,
                                           &device->info,
                                           &result,
                                           core_freq[0], core_freq[1]);
#if GEN_GEN >= 8 && GEN_GEN <= 11
         gen_perf_query_mdapi_write_perfcntr(pData, stride, &device->info,
                                             query_data + intel_perf_counter(false),
                                             query_data + intel_perf_counter(true));
#endif
         const uint64_t *marker = query_data + intel_perf_marker_offset();
         gen_perf_query_mdapi_write_marker(pData, stride, &device->info, *marker);
         break;
      }

      default:
         unreachable("invalid pool type");
      }

      if (!write_results)
         status = VK_NOT_READY;

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cpu_write_query_result(pData, flags, idx, available);

      pData += stride;
      if (pData >= data_end)
         break;
   }

   return status;
}

static void
emit_ps_depth_count(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_address addr)
{
   cmd_buffer->state.pending_pipe_bits |= ANV_PIPE_POST_SYNC_BIT;
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WritePSDepthCount;
      pc.DepthStallEnable        = true;
      pc.Address                 = addr;

      if (GEN_GEN == 9 && cmd_buffer->device->info.gt == 4)
         pc.CommandStreamerStallEnable = true;
   }
}

static void
emit_query_mi_availability(struct gen_mi_builder *b,
                           struct anv_address addr,
                           bool available)
{
   gen_mi_store(b, gen_mi_mem64(addr), gen_mi_imm(available));
}

static void
emit_query_pc_availability(struct anv_cmd_buffer *cmd_buffer,
                           struct anv_address addr,
                           bool available)
{
   cmd_buffer->state.pending_pipe_bits |= ANV_PIPE_POST_SYNC_BIT;
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WriteImmediateData;
      pc.Address                 = addr;
      pc.ImmediateData           = available;
   }
}

/**
 * Goes through a series of consecutive query indices in the given pool
 * setting all element values to 0 and emitting them as available.
 */
static void
emit_zero_queries(struct anv_cmd_buffer *cmd_buffer,
                  struct gen_mi_builder *b, struct anv_query_pool *pool,
                  uint32_t first_index, uint32_t num_queries)
{
   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
      /* These queries are written with a PIPE_CONTROL so clear them using the
       * PIPE_CONTROL as well so we don't have to synchronize between 2 types
       * of operations.
       */
      assert((pool->stride % 8) == 0);
      for (uint32_t i = 0; i < num_queries; i++) {
         struct anv_address slot_addr =
            anv_query_address(pool, first_index + i);

         for (uint32_t qword = 1; qword < (pool->stride / 8); qword++) {
            emit_query_pc_availability(cmd_buffer,
                                       anv_address_add(slot_addr, qword * 8),
                                       false);
         }
         emit_query_pc_availability(cmd_buffer, slot_addr, true);
      }
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      for (uint32_t i = 0; i < num_queries; i++) {
         struct anv_address slot_addr =
            anv_query_address(pool, first_index + i);
         gen_mi_memset(b, anv_address_add(slot_addr, 8), 0, pool->stride - 8);
         emit_query_mi_availability(b, slot_addr, true);
      }
      break;

#if GEN_GEN >= 8
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR: {
      for (uint32_t i = 0; i < num_queries; i++) {
         for (uint32_t p = 0; p < pool->n_passes; p++) {
            gen_mi_memset(b,
                          khr_perf_query_oa_address(pool,
                                                    first_index + i, p, false),
                          0, 2 * OA_SNAPSHOT_SIZE);
            emit_query_mi_availability(b,
                                       khr_perf_query_availability_address(pool, first_index + i, p),
                                       true);
         }
      }
      break;
   }
#endif

   case VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL:
      for (uint32_t i = 0; i < num_queries; i++) {
         struct anv_address slot_addr =
            anv_query_address(pool, first_index + i);
         gen_mi_memset(b, anv_address_add(slot_addr, 8), 0, pool->stride - 8);
         emit_query_mi_availability(b, slot_addr, true);
      }
      break;

   default:
      unreachable("Unsupported query type");
   }
}

void genX(CmdResetQueryPool)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
      for (uint32_t i = 0; i < queryCount; i++) {
         emit_query_pc_availability(cmd_buffer,
                                    anv_query_address(pool, firstQuery + i),
                                    false);
      }
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      struct gen_mi_builder b;
      gen_mi_builder_init(&b, &cmd_buffer->batch);

      for (uint32_t i = 0; i < queryCount; i++)
         emit_query_mi_availability(&b, anv_query_address(pool, firstQuery + i), false);
      break;
   }

#if GEN_GEN >= 8
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR: {
      struct gen_mi_builder b;
      gen_mi_builder_init(&b, &cmd_buffer->batch);

      for (uint32_t i = 0; i < queryCount; i++) {
         for (uint32_t p = 0; p < pool->n_passes; p++) {
            emit_query_mi_availability(
               &b,
               khr_perf_query_availability_address(pool, firstQuery + i, p),
               false);
         }
      }
      break;
   }
#endif

   case VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL: {
      struct gen_mi_builder b;
      gen_mi_builder_init(&b, &cmd_buffer->batch);

      for (uint32_t i = 0; i < queryCount; i++)
         emit_query_mi_availability(&b, anv_query_address(pool, firstQuery + i), false);
      break;
   }

   default:
      unreachable("Unsupported query type");
   }
}

void genX(ResetQueryPool)(
    VkDevice                                    _device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   for (uint32_t i = 0; i < queryCount; i++) {
      if (pool->type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
         for (uint32_t p = 0; p < pool->n_passes; p++) {
            uint64_t *pass_slot = pool->bo->map +
               khr_perf_query_availability_offset(pool, firstQuery + i, p);
            *pass_slot = 0;
         }
      } else {
         uint64_t *slot = query_slot(pool, firstQuery + i);
         *slot = 0;
      }
   }
}

static const uint32_t vk_pipeline_stat_to_reg[] = {
   GENX(IA_VERTICES_COUNT_num),
   GENX(IA_PRIMITIVES_COUNT_num),
   GENX(VS_INVOCATION_COUNT_num),
   GENX(GS_INVOCATION_COUNT_num),
   GENX(GS_PRIMITIVES_COUNT_num),
   GENX(CL_INVOCATION_COUNT_num),
   GENX(CL_PRIMITIVES_COUNT_num),
   GENX(PS_INVOCATION_COUNT_num),
   GENX(HS_INVOCATION_COUNT_num),
   GENX(DS_INVOCATION_COUNT_num),
   GENX(CS_INVOCATION_COUNT_num),
};

static void
emit_pipeline_stat(struct gen_mi_builder *b, uint32_t stat,
                   struct anv_address addr)
{
   STATIC_ASSERT(ANV_PIPELINE_STATISTICS_MASK ==
                 (1 << ARRAY_SIZE(vk_pipeline_stat_to_reg)) - 1);

   assert(stat < ARRAY_SIZE(vk_pipeline_stat_to_reg));
   gen_mi_store(b, gen_mi_mem64(addr),
                gen_mi_reg64(vk_pipeline_stat_to_reg[stat]));
}

static void
emit_xfb_query(struct gen_mi_builder *b, uint32_t stream,
               struct anv_address addr)
{
   assert(stream < MAX_XFB_STREAMS);

   gen_mi_store(b, gen_mi_mem64(anv_address_add(addr, 0)),
                gen_mi_reg64(GENX(SO_NUM_PRIMS_WRITTEN0_num) + stream * 8));
   gen_mi_store(b, gen_mi_mem64(anv_address_add(addr, 16)),
                gen_mi_reg64(GENX(SO_PRIM_STORAGE_NEEDED0_num) + stream * 8));
}

void genX(CmdBeginQuery)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
   genX(CmdBeginQueryIndexedEXT)(commandBuffer, queryPool, query, flags, 0);
}

void genX(CmdBeginQueryIndexedEXT)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags,
    uint32_t                                    index)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   struct anv_address query_addr = anv_query_address(pool, query);

   struct gen_mi_builder b;
   gen_mi_builder_init(&b, &cmd_buffer->batch);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_ps_depth_count(cmd_buffer, anv_address_add(query_addr, 8));
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      /* TODO: This might only be necessary for certain stats */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }

      uint32_t statistics = pool->pipeline_statistics;
      uint32_t offset = 8;
      while (statistics) {
         uint32_t stat = u_bit_scan(&statistics);
         emit_pipeline_stat(&b, stat, anv_address_add(query_addr, offset));
         offset += 16;
      }
      break;
   }

   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }
      emit_xfb_query(&b, index, anv_address_add(query_addr, 8));
      break;

#if GEN_GEN >= 8
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR: {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }
      cmd_buffer->perf_query_pool = pool;

      /* We know the bottom bits of the address are 0s which match what we
       * want in the MI_RPC packet.
       */
      struct gen_mi_value mi_rpc_write_offset =
         gen_mi_iadd(
            &b,
            gen_mi_imm(
               gen_canonical_address(
                  pool->bo->offset +
                  khr_perf_query_oa_offset(pool, query, 0 /* pass */, false))),
            gen_mi_reg64(ANV_PERF_QUERY_OFFSET_REG));
      struct gen_mi_address_token mi_rpc_addr_dest =
         gen_mi_store_address(&b, mi_rpc_write_offset);
      gen_mi_self_mod_barrier(&b);

      void *mi_rpc_dws =
         anv_batch_emitn(&cmd_buffer->batch,
                         GENX(MI_REPORT_PERF_COUNT_length),
                         GENX(MI_REPORT_PERF_COUNT),
                         .MemoryAddress = query_addr /* Will be overwritten */ );
      _gen_mi_resolve_address_token(&b, mi_rpc_addr_dest,
                                    mi_rpc_dws +
                                    GENX(MI_REPORT_PERF_COUNT_MemoryAddress_start) / 8);
      break;
   }
#endif

   case VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL: {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_REPORT_PERF_COUNT), rpc) {
         rpc.MemoryAddress =
            anv_address_add(query_addr, intel_perf_mi_rpc_offset(false));
      }
#if GEN_GEN < 9
      gen_mi_store(&b,
                   gen_mi_mem32(anv_address_add(query_addr,
                                                intel_perf_rpstart_offset(false))),
                   gen_mi_reg32(GENX(RPSTAT1_num)));
#else
      gen_mi_store(&b,
                   gen_mi_mem32(anv_address_add(query_addr,
                                                intel_perf_rpstart_offset(false))),
                   gen_mi_reg32(GENX(RPSTAT0_num)));
#endif
#if GEN_GEN >= 8 && GEN_GEN <= 11
      gen_mi_store(&b, gen_mi_mem64(anv_address_add(query_addr,
                                                    intel_perf_counter(false))),
                   gen_mi_reg64(GENX(PERFCNT1_num)));
      gen_mi_store(&b, gen_mi_mem64(anv_address_add(query_addr,
                                                    intel_perf_counter(false) + 8)),
                   gen_mi_reg64(GENX(PERFCNT2_num)));
#endif
      break;
   }

   default:
      unreachable("");
   }
}

void genX(CmdEndQuery)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
   genX(CmdEndQueryIndexedEXT)(commandBuffer, queryPool, query, 0);
}

void genX(CmdEndQueryIndexedEXT)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    uint32_t                                    index)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   struct anv_address query_addr = anv_query_address(pool, query);

   struct gen_mi_builder b;
   gen_mi_builder_init(&b, &cmd_buffer->batch);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_ps_depth_count(cmd_buffer, anv_address_add(query_addr, 16));
      emit_query_pc_availability(cmd_buffer, query_addr, true);
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      /* TODO: This might only be necessary for certain stats */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }

      uint32_t statistics = pool->pipeline_statistics;
      uint32_t offset = 16;
      while (statistics) {
         uint32_t stat = u_bit_scan(&statistics);
         emit_pipeline_stat(&b, stat, anv_address_add(query_addr, offset));
         offset += 16;
      }

      emit_query_mi_availability(&b, query_addr, true);
      break;
   }

   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }

      emit_xfb_query(&b, index, anv_address_add(query_addr, 16));
      emit_query_mi_availability(&b, query_addr, true);
      break;

#if GEN_GEN >= 8
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR: {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }

      /* We know the bottom bits of the address are 0s which match what we
       * want in the MI_RPC/MI_SDI packets.
       */
      struct gen_mi_value mi_rpc_write_offset =
         gen_mi_iadd(
            &b,
            gen_mi_imm(
               gen_canonical_address(
                  pool->bo->offset +
                  khr_perf_query_oa_offset(pool, query, 0 /* pass*/, true))),
            gen_mi_reg64(ANV_PERF_QUERY_OFFSET_REG));
      struct gen_mi_value availability_write_offset =
         gen_mi_iadd(
            &b,
            gen_mi_imm(
               gen_canonical_address(
                  pool->bo->offset +
                  khr_perf_query_availability_offset(pool, query, 0 /* pass */))),
            gen_mi_reg64(ANV_PERF_QUERY_OFFSET_REG));

      struct gen_mi_address_token mi_rpc_addr_dest =
         gen_mi_store_address(&b, mi_rpc_write_offset);
      struct gen_mi_address_token availability_addr_dest =
         gen_mi_store_address(&b, availability_write_offset);
      gen_mi_self_mod_barrier(&b);

      void *mi_rpc_dws =
         anv_batch_emitn(&cmd_buffer->batch,
                         GENX(MI_REPORT_PERF_COUNT_length),
                         GENX(MI_REPORT_PERF_COUNT),
                         .MemoryAddress = query_addr /* Will be overwritten */ );
      _gen_mi_resolve_address_token(&b, mi_rpc_addr_dest,
                                    mi_rpc_dws +
                                    GENX(MI_REPORT_PERF_COUNT_MemoryAddress_start) / 8);

      void *availability_dws =
         anv_batch_emitn(&cmd_buffer->batch,
                         GENX(MI_STORE_DATA_IMM_length),
                         GENX(MI_STORE_DATA_IMM),
                         .ImmediateData = true);
      _gen_mi_resolve_address_token(&b, availability_addr_dest,
                                    availability_dws +
                                    GENX(MI_STORE_DATA_IMM_Address_start) / 8);
      break;
   }
#endif

   case VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL: {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }
      uint32_t marker_offset = intel_perf_marker_offset();
      gen_mi_store(&b, gen_mi_mem64(anv_address_add(query_addr, marker_offset)),
                   gen_mi_imm(cmd_buffer->intel_perf_marker));
#if GEN_GEN >= 8 && GEN_GEN <= 11
      gen_mi_store(&b, gen_mi_mem64(anv_address_add(query_addr, intel_perf_counter(true))),
                   gen_mi_reg64(GENX(PERFCNT1_num)));
      gen_mi_store(&b, gen_mi_mem64(anv_address_add(query_addr, intel_perf_counter(true) + 8)),
                   gen_mi_reg64(GENX(PERFCNT2_num)));
#endif
#if GEN_GEN < 9
      gen_mi_store(&b,
                   gen_mi_mem32(anv_address_add(query_addr,
                                                intel_perf_rpstart_offset(true))),
                   gen_mi_reg32(GENX(RPSTAT1_num)));
#else
      gen_mi_store(&b,
                   gen_mi_mem32(anv_address_add(query_addr,
                                                intel_perf_rpstart_offset(true))),
                   gen_mi_reg32(GENX(RPSTAT0_num)));
#endif
      /* Position the last OA snapshot at the beginning of the query so that
       * we can tell whether it's ready.
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_REPORT_PERF_COUNT), rpc) {
         rpc.MemoryAddress = anv_address_add(query_addr,
                                             intel_perf_mi_rpc_offset(true));
         rpc.ReportID = 0xdeadbeef; /* This goes in the first dword */
      }
      emit_query_mi_availability(&b, query_addr, true);
      break;
   }

   default:
      unreachable("");
   }

   /* When multiview is active the spec requires that N consecutive query
    * indices are used, where N is the number of active views in the subpass.
    * The spec allows that we only write the results to one of the queries
    * but we still need to manage result availability for all the query indices.
    * Since we only emit a single query for all active views in the
    * first index, mark the other query indices as being already available
    * with result 0.
    */
   if (cmd_buffer->state.subpass && cmd_buffer->state.subpass->view_mask) {
      const uint32_t num_queries =
         util_bitcount(cmd_buffer->state.subpass->view_mask);
      if (num_queries > 1)
         emit_zero_queries(cmd_buffer, &b, pool, query + 1, num_queries - 1);
   }
}

#define TIMESTAMP 0x2358

void genX(CmdWriteTimestamp)(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   struct anv_address query_addr = anv_query_address(pool, query);

   assert(pool->type == VK_QUERY_TYPE_TIMESTAMP);

   struct gen_mi_builder b;
   gen_mi_builder_init(&b, &cmd_buffer->batch);

   switch (pipelineStage) {
   case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
      gen_mi_store(&b, gen_mi_mem64(anv_address_add(query_addr, 8)),
                       gen_mi_reg64(TIMESTAMP));
      break;

   default:
      /* Everything else is bottom-of-pipe */
      cmd_buffer->state.pending_pipe_bits |= ANV_PIPE_POST_SYNC_BIT;
      genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.DestinationAddressType  = DAT_PPGTT;
         pc.PostSyncOperation       = WriteTimestamp;
         pc.Address                 = anv_address_add(query_addr, 8);

         if (GEN_GEN == 9 && cmd_buffer->device->info.gt == 4)
            pc.CommandStreamerStallEnable = true;
      }
      break;
   }

   emit_query_pc_availability(cmd_buffer, query_addr, true);

   /* When multiview is active the spec requires that N consecutive query
    * indices are used, where N is the number of active views in the subpass.
    * The spec allows that we only write the results to one of the queries
    * but we still need to manage result availability for all the query indices.
    * Since we only emit a single query for all active views in the
    * first index, mark the other query indices as being already available
    * with result 0.
    */
   if (cmd_buffer->state.subpass && cmd_buffer->state.subpass->view_mask) {
      const uint32_t num_queries =
         util_bitcount(cmd_buffer->state.subpass->view_mask);
      if (num_queries > 1)
         emit_zero_queries(cmd_buffer, &b, pool, query + 1, num_queries - 1);
   }
}

#if GEN_GEN > 7 || GEN_IS_HASWELL

#if GEN_GEN >= 8 || GEN_IS_HASWELL

#define MI_PREDICATE_SRC0    0x2400
#define MI_PREDICATE_SRC1    0x2408
#define MI_PREDICATE_RESULT  0x2418

/**
 * Writes the results of a query to dst_addr is the value at poll_addr is equal
 * to the reference value.
 */
static void
gpu_write_query_result_cond(struct anv_cmd_buffer *cmd_buffer,
                            struct gen_mi_builder *b,
                            struct anv_address poll_addr,
                            struct anv_address dst_addr,
                            uint64_t ref_value,
                            VkQueryResultFlags flags,
                            uint32_t value_index,
                            struct gen_mi_value query_result)
{
   gen_mi_store(b, gen_mi_reg64(MI_PREDICATE_SRC0), gen_mi_mem64(poll_addr));
   gen_mi_store(b, gen_mi_reg64(MI_PREDICATE_SRC1), gen_mi_imm(ref_value));
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_PREDICATE), mip) {
      mip.LoadOperation    = LOAD_LOAD;
      mip.CombineOperation = COMBINE_SET;
      mip.CompareOperation = COMPARE_SRCS_EQUAL;
   }

   if (flags & VK_QUERY_RESULT_64_BIT) {
      struct anv_address res_addr = anv_address_add(dst_addr, value_index * 8);
      gen_mi_store_if(b, gen_mi_mem64(res_addr), query_result);
   } else {
      struct anv_address res_addr = anv_address_add(dst_addr, value_index * 4);
      gen_mi_store_if(b, gen_mi_mem32(res_addr), query_result);
   }
}

#endif /* GEN_GEN >= 8 || GEN_IS_HASWELL */

static void
gpu_write_query_result(struct gen_mi_builder *b,
                       struct anv_address dst_addr,
                       VkQueryResultFlags flags,
                       uint32_t value_index,
                       struct gen_mi_value query_result)
{
   if (flags & VK_QUERY_RESULT_64_BIT) {
      struct anv_address res_addr = anv_address_add(dst_addr, value_index * 8);
      gen_mi_store(b, gen_mi_mem64(res_addr), query_result);
   } else {
      struct anv_address res_addr = anv_address_add(dst_addr, value_index * 4);
      gen_mi_store(b, gen_mi_mem32(res_addr), query_result);
   }
}

static struct gen_mi_value
compute_query_result(struct gen_mi_builder *b, struct anv_address addr)
{
   return gen_mi_isub(b, gen_mi_mem64(anv_address_add(addr, 8)),
                         gen_mi_mem64(anv_address_add(addr, 0)));
}

void genX(CmdCopyQueryPoolResults)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    destBuffer,
    VkDeviceSize                                destOffset,
    VkDeviceSize                                destStride,
    VkQueryResultFlags                          flags)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   ANV_FROM_HANDLE(anv_buffer, buffer, destBuffer);

   struct gen_mi_builder b;
   gen_mi_builder_init(&b, &cmd_buffer->batch);
   struct gen_mi_value result;

   /* If render target writes are ongoing, request a render target cache flush
    * to ensure proper ordering of the commands from the 3d pipe and the
    * command streamer.
    */
   if (cmd_buffer->state.pending_pipe_bits & ANV_PIPE_RENDER_TARGET_BUFFER_WRITES) {
      cmd_buffer->state.pending_pipe_bits |=
         ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT;
   }

   if ((flags & VK_QUERY_RESULT_WAIT_BIT) ||
       (cmd_buffer->state.pending_pipe_bits & ANV_PIPE_FLUSH_BITS) ||
       /* Occlusion & timestamp queries are written using a PIPE_CONTROL and
        * because we're about to copy values from MI commands, we need to
        * stall the command streamer to make sure the PIPE_CONTROL values have
        * landed, otherwise we could see inconsistent values & availability.
        *
        *  From the vulkan spec:
        *
        *     "vkCmdCopyQueryPoolResults is guaranteed to see the effect of
        *     previous uses of vkCmdResetQueryPool in the same queue, without
        *     any additional synchronization."
        */
       pool->type == VK_QUERY_TYPE_OCCLUSION ||
       pool->type == VK_QUERY_TYPE_TIMESTAMP) {
      cmd_buffer->state.pending_pipe_bits |= ANV_PIPE_CS_STALL_BIT;
      genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
   }

   struct anv_address dest_addr = anv_address_add(buffer->address, destOffset);
   for (uint32_t i = 0; i < queryCount; i++) {
      struct anv_address query_addr = anv_query_address(pool, firstQuery + i);
      uint32_t idx = 0;
      switch (pool->type) {
      case VK_QUERY_TYPE_OCCLUSION:
         result = compute_query_result(&b, anv_address_add(query_addr, 8));
#if GEN_GEN >= 8 || GEN_IS_HASWELL
         /* Like in the case of vkGetQueryPoolResults, if the query is
          * unavailable and the VK_QUERY_RESULT_PARTIAL_BIT flag is set,
          * conservatively write 0 as the query result. If the
          * VK_QUERY_RESULT_PARTIAL_BIT isn't set, don't write any value.
          */
         gpu_write_query_result_cond(cmd_buffer, &b, query_addr, dest_addr,
               1 /* available */, flags, idx, result);
         if (flags & VK_QUERY_RESULT_PARTIAL_BIT) {
            gpu_write_query_result_cond(cmd_buffer, &b, query_addr, dest_addr,
                  0 /* unavailable */, flags, idx, gen_mi_imm(0));
         }
         idx++;
#else /* GEN_GEN < 8 && !GEN_IS_HASWELL */
         gpu_write_query_result(&b, dest_addr, flags, idx++, result);
#endif
         break;

      case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
         uint32_t statistics = pool->pipeline_statistics;
         while (statistics) {
            uint32_t stat = u_bit_scan(&statistics);

            result = compute_query_result(&b, anv_address_add(query_addr,
                                                              idx * 16 + 8));

            /* WaDividePSInvocationCountBy4:HSW,BDW */
            if ((cmd_buffer->device->info.gen == 8 ||
                 cmd_buffer->device->info.is_haswell) &&
                (1 << stat) == VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT) {
               result = gen_mi_ushr32_imm(&b, result, 2);
            }

            gpu_write_query_result(&b, dest_addr, flags, idx++, result);
         }
         assert(idx == util_bitcount(pool->pipeline_statistics));
         break;
      }

      case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
         result = compute_query_result(&b, anv_address_add(query_addr, 8));
         gpu_write_query_result(&b, dest_addr, flags, idx++, result);
         result = compute_query_result(&b, anv_address_add(query_addr, 24));
         gpu_write_query_result(&b, dest_addr, flags, idx++, result);
         break;

      case VK_QUERY_TYPE_TIMESTAMP:
         result = gen_mi_mem64(anv_address_add(query_addr, 8));
         gpu_write_query_result(&b, dest_addr, flags, 0, result);
         break;

#if GEN_GEN >= 8
      case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR:
         unreachable("Copy KHR performance query results not implemented");
         break;
#endif

      default:
         unreachable("unhandled query type");
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         gpu_write_query_result(&b, dest_addr, flags, idx,
                                gen_mi_mem64(query_addr));
      }

      dest_addr = anv_address_add(dest_addr, destStride);
   }
}

#else
void genX(CmdCopyQueryPoolResults)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    destBuffer,
    VkDeviceSize                                destOffset,
    VkDeviceSize                                destStride,
    VkQueryResultFlags                          flags)
{
   anv_finishme("Queries not yet supported on Ivy Bridge");
}
#endif
