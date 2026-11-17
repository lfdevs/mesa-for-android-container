/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include "util/os_time.h"
#include "vk_log.h"

#include "pan_props.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_query_pool.h"

#include "panvk_cmd_ts.h"

#if PAN_ARCH >= 10
#include "panvk_queue.h"
#endif

#define PANVK_QUERY_TIMEOUT 2000000000ull

static void
reset_query_pool(struct panvk_query_pool *pool, uint32_t firstQuery,
                 uint32_t queryCount)
{
   panvk_priv_mem_write_array(pool->available_mem,
                              panvk_query_available_offset(pool, firstQuery),
                              struct panvk_query_available_obj, queryCount,
                              available)
      memset(available, 0, queryCount * sizeof(*available));
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateQueryPool)(VkDevice _device,
                                const VkQueryPoolCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkQueryPool *pQueryPool)
{
   VK_FROM_HANDLE(panvk_device, device, _device);

   struct panvk_query_pool *pool;

   pool =
      vk_query_pool_create(&device->vk, pCreateInfo, pAllocator, sizeof(*pool));
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t reports_per_query;
   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION: {
      /* The counter is per core on Bifrost */
#if PAN_ARCH < 9
      const struct panvk_physical_device *phys_dev =
         to_panvk_physical_device(device->vk.physical);

      reports_per_query = pan_query_core_id_range(&phys_dev->kmod.dev->props);
#else
      reports_per_query = 1;
#endif
      break;
   }
#if PAN_ARCH >= 10
   case VK_QUERY_TYPE_TIMESTAMP: {
      /* One value per subqueue + 1 value for metadata. */
      reports_per_query = PANVK_SUBQUEUE_COUNT + 1;
      break;
   }
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT: {
      reports_per_query = 1;
      break;
   }
   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      /* See panvk_pstats_query_state. */
      reports_per_query =
         MAX2(2 * util_bitcount(pCreateInfo->pipelineStatistics), 1);
      break;
   }
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      reports_per_query = 4;
      break;
   }
#endif
   default:
      UNREACHABLE("Unsupported query type");
   }

   pool->reports_per_query = reports_per_query;
   pool->query_stride = reports_per_query * sizeof(struct panvk_query_report);

   assert(pool->vk.query_count > 0);

   struct panvk_pool_alloc_info alloc_info = {
      .size = pool->reports_per_query * sizeof(struct panvk_query_report) *
              pool->vk.query_count,
      .alignment = sizeof(struct panvk_query_report),
   };
   pool->mem = panvk_pool_alloc_mem(&device->mempools.rw, alloc_info);
   if (!pool->mem.bo) {
      vk_query_pool_destroy(&device->vk, pAllocator, &pool->vk);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   struct panvk_pool_alloc_info syncobjs_alloc_info = {
      .size = sizeof(struct panvk_query_available_obj) * pool->vk.query_count,
      .alignment = 64,
   };
   pool->available_mem =
      panvk_pool_alloc_mem(&device->mempools.rw_nc, syncobjs_alloc_info);
   if (!pool->available_mem.bo) {
      panvk_pool_free_mem(&pool->mem);
      vk_query_pool_destroy(&device->vk, pAllocator, &pool->vk);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   if (pCreateInfo->flags & VK_QUERY_POOL_CREATE_RESET_BIT_KHR)
      reset_query_pool(pool, 0, pool->vk.query_count);

   *pQueryPool = panvk_query_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyQueryPool)(VkDevice _device, VkQueryPool queryPool,
                                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   if (!pool)
      return;

   panvk_pool_free_mem(&pool->mem);
   panvk_pool_free_mem(&pool->available_mem);
   vk_query_pool_destroy(&device->vk, pAllocator, &pool->vk);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(ResetQueryPool)(VkDevice device, VkQueryPool queryPool,
                               uint32_t firstQuery, uint32_t queryCount)
{
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);
   reset_query_pool(pool, firstQuery, queryCount);
}

static bool
panvk_query_is_available(struct panvk_query_pool *pool, uint32_t query)
{
   bool res = false;

   panvk_priv_mem_readback(pool->available_mem,
                           panvk_query_available_offset(pool, query),
                           struct panvk_query_available_obj, available) {
#if PAN_ARCH >= 10
      res = p_atomic_read(&available->sync_obj.seqno) != 0;
#else
      res = p_atomic_read(&available->value) != 0;
#endif
   }

   return res;
}

static VkResult
panvk_query_wait_for_available(struct panvk_device *dev,
                               struct panvk_query_pool *pool, uint32_t query)
{
   int64_t abs_timeout_ns = os_time_get_absolute_timeout(PANVK_QUERY_TIMEOUT);
   int64_t now;

   while ((now = os_time_get_nano()) < abs_timeout_ns) {
      if (panvk_query_is_available(pool, query))
         return VK_SUCCESS;

      VkResult status = vk_device_check_status(&dev->vk);
      if (status != VK_SUCCESS)
         return status;

      /* The availability object is written by the GPU with a CSG-scoped
       * sync, which never wakes the host, so block on the kernel instead of
       * spinning. On kbase, spinning also starves the CSF event path the
       * submitted work may depend on (tiler heap growth, scheduling).
       */
      int64_t step_ns = MIN2(abs_timeout_ns - now, 1000000);
#if PAN_ARCH >= 10 && defined(HAVE_PAN_KMOD_KBASE)
      const struct panvk_physical_device *phys_dev =
         to_panvk_physical_device(dev->vk.physical);

      if (phys_dev->kbase_node_path[0] &&
          kbase_kmod_csf_wait_event(dev->kmod.dev, step_ns) == 0)
         continue;
#endif
      os_time_sleep(MAX2(step_ns / 1000, 1));
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

static void
cpu_write_occlusion_query_result(void *dst, uint32_t idx,
                                 VkQueryResultFlags flags,
                                 const struct panvk_query_report *src,
                                 unsigned core_count)
{
   uint64_t result = 0;

   for (uint32_t core_idx = 0; core_idx < core_count; core_idx++)
      result += src[core_idx].value;

   cpu_write_query_result(dst, idx, flags, result);
}

#if PAN_ARCH >= 10
static void
cpu_write_timestamp_query_result(void *dst, uint32_t idx,
                                 VkQueryResultFlags flags,
                                 const struct panvk_query_report *src,
                                 unsigned input_value_count)
{
   enum panvk_query_ts_op op =
      panvk_timestamp_info_get_op(src[input_value_count - 1].value);
   uint32_t sq_mask =
      panvk_timestamp_info_get_sq_mask(src[input_value_count - 1].value);

   uint64_t result = op == PANVK_QUERY_TS_OP_MIN ? UINT64_MAX : 0;

   for (uint32_t idx = 0; idx < input_value_count - 1; ++idx) {
      if ((sq_mask & BITFIELD_BIT(idx)) == 0)
         continue;
      if (src[idx].value == 0)
         continue;

      if (op == PANVK_QUERY_TS_OP_MIN)
         result = MIN2(result, src[idx].value);
      else
         result = MAX2(result, src[idx].value);
   }

   if (op == PANVK_QUERY_TS_OP_MIN && result == UINT64_MAX)
      result = 0;

   cpu_write_query_result(dst, idx, flags, result);
}
#endif

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(GetQueryPoolResults)(VkDevice _device, VkQueryPool queryPool,
                                    uint32_t firstQuery, uint32_t queryCount,
                                    size_t dataSize, void *pData,
                                    VkDeviceSize stride,
                                    VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   if (vk_device_is_lost(&device->vk))
      return VK_ERROR_DEVICE_LOST;

   VkResult status = VK_SUCCESS;
   const uint32_t result_count =
      pool->vk.query_type == VK_QUERY_TYPE_PIPELINE_STATISTICS
         ? util_bitcount(pool->vk.pipeline_statistics)
      : pool->vk.query_type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT
         ? 2
         : 1;
   for (uint32_t i = 0; i < queryCount; i++) {
      const uint32_t query = firstQuery + i;

      bool available = panvk_query_is_available(pool, query);

      if (!available && (flags & VK_QUERY_RESULT_WAIT_BIT)) {
         status = panvk_query_wait_for_available(device, pool, query);
         if (status != VK_SUCCESS)
            return status;

         available = true;
      }

      bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

      assert(i * stride < dataSize);
      void *dst = (char *)pData + i * stride;

      panvk_priv_mem_readback_array(pool->mem, panvk_query_offset(pool, query),
                                    struct panvk_query_report,
                                    pool->reports_per_query, src) {
         switch (pool->vk.query_type) {
         case VK_QUERY_TYPE_OCCLUSION: {
            if (write_results)
               cpu_write_occlusion_query_result(dst, 0, flags, src,
                                                pool->reports_per_query);
            break;
         }
#if PAN_ARCH >= 10
         case VK_QUERY_TYPE_TIMESTAMP: {
            if (write_results)
               cpu_write_timestamp_query_result(dst, 0, flags, src,
                                                pool->reports_per_query);
            break;
         }
         case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT: {
            if (write_results)
               cpu_write_query_result(dst, 0, flags, src[0].value);
            break;
         }
         case VK_QUERY_TYPE_PIPELINE_STATISTICS:
         case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
            for (uint32_t s = 0; write_results && s < result_count; s++) {
               cpu_write_query_result(dst, s, flags,
                                      src[s].value +
                                         src[result_count + s].value);
            }
            break;
         }
#endif
         default:
            UNREACHABLE("Unsupported query type");
         }
      }

      if (!write_results)
         status = VK_NOT_READY;

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cpu_write_query_result(dst, result_count, flags, available);
   }

   return status;
}
