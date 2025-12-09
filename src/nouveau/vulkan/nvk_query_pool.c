/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_query_pool.h"

#include "nvk_buffer.h"
#include "nvk_cmd_buffer.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_event.h"
#include "nvk_mme.h"
#include "nvk_physical_device.h"
#include "nvkmd/nvkmd.h"

#include "vk_common_entrypoints.h"
#include "vk_meta.h"
#include "vk_pipeline.h"

#include "cl/nvk_query.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "nvkcl.h"

#include "util/os_time.h"

#include "nv_push_cl906f.h"
#include "nv_push_cl9097.h"
#include "nv_push_cla0c0.h"
#include "nv_push_clc597.h"
#include "nv_push_clc7c0.h"

static uint32_t
vk_query_pool_report_count(const struct vk_query_pool *vk_pool)
{
   switch (vk_pool->query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
   case VK_QUERY_TYPE_TIMESTAMP:
      return 1;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      return util_bitcount(vk_pool->pipeline_statistics);

   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      return 2;

   default:
      UNREACHABLE("Unsupported query type");
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateQueryPool(VkDevice device,
                    const VkQueryPoolCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkQueryPool *pQueryPool)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_query_pool *pool;
   VkResult result;

   pool = vk_query_pool_create(&dev->vk, pCreateInfo,
                               pAllocator, sizeof(*pool));
   if (!pool)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Use interleaved layouts on Tegra so we can safely  handle non-coherent
    * maps
    */
   if (pdev->info.type == NV_DEVICE_TYPE_SOC)
      pool->layout = NVK_QUERY_POOL_LAYOUT_ALIGNED_INTERLEAVED;
   else
      pool->layout = NVK_QUERY_POOL_LAYOUT_SEPARATE;

   uint32_t reports_per_query;
   if (pool->vk.query_type == VK_QUERY_TYPE_TIMESTAMP) {
      /* Timestamps are just a single timestamp */
      reports_per_query = 1;
   } else {
      /* Everything else is two queries because we have to compute a delta */
      reports_per_query = 2 * vk_query_pool_report_count(&pool->vk);
   }

   uint64_t mem_size = 0;
   switch (pool->layout) {
   case NVK_QUERY_POOL_LAYOUT_SEPARATE:
      pool->reports_start = align(pool->vk.query_count * sizeof(uint32_t),
                                  sizeof(struct nvk_query_report));
      pool->query_stride = reports_per_query * sizeof(struct nvk_query_report);
      mem_size = pool->reports_start +
         pool->vk.query_count * (uint64_t)pool->query_stride;
      break;

   case NVK_QUERY_POOL_LAYOUT_ALIGNED_INTERLEAVED:
      pool->reports_start = sizeof(struct nvk_query_report);
      pool->query_stride =
         align((reports_per_query + 1) * sizeof(struct nvk_query_report),
               pdev->info.nc_atom_size_B);
      mem_size = pool->vk.query_count * (uint64_t)pool->query_stride;
      break;

   default:
      UNREACHABLE("Unsupported query layout");
   }

   if (mem_size > 0) {
      result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &dev->vk.base,
                                          mem_size, 0 /* align_B */,
                                          NVKMD_MEM_GART,
                                          NVKMD_MEM_MAP_RDWR,
                                          &pool->mem);
      if (result != VK_SUCCESS) {
         vk_query_pool_destroy(&dev->vk, pAllocator, &pool->vk);
         return result;
      }

      if ((pdev->debug_flags & NVK_DEBUG_ZERO_MEMORY) ||
          (pCreateInfo->flags & VK_QUERY_POOL_CREATE_RESET_BIT_KHR)) {
         memset(pool->mem->map, 0, mem_size);
         nvkmd_mem_sync_map_to_gpu(pool->mem, 0, mem_size);
      }
   }

   *pQueryPool = nvk_query_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyQueryPool(VkDevice device,
                     VkQueryPool queryPool,
                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);

   if (!pool)
      return;

   if (pool->mem)
      nvkmd_mem_unref(pool->mem);
   vk_query_pool_destroy(&dev->vk, pAllocator, &pool->vk);
}

static uint32_t
nvk_query_available_stride_B(struct nvk_query_pool *pool)
{
   return pool->layout == NVK_QUERY_POOL_LAYOUT_SEPARATE ?
          sizeof(uint32_t) : pool->query_stride;
}

static uint64_t
nvk_query_available_offset_B(struct nvk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return query * nvk_query_available_stride_B(pool);
}

static uint64_t
nvk_query_available_addr(struct nvk_query_pool *pool, uint32_t query)
{
   return pool->mem->va->addr + nvk_query_available_offset_B(pool, query);
}

static uint32_t *
nvk_query_available_map(struct nvk_query_pool *pool, uint32_t query)
{
   return pool->mem->map + nvk_query_available_offset_B(pool, query);
}

static uint64_t
nvk_query_report_offset_B(struct nvk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return pool->reports_start + query * pool->query_stride;
}

static uint64_t
nvk_query_report_addr(struct nvk_query_pool *pool, uint32_t query)
{
   return pool->mem->va->addr + nvk_query_report_offset_B(pool, query);
}

static struct nvk_query_report *
nvk_query_report_map(struct nvk_query_pool *pool, uint32_t query)
{
   return pool->mem->map + nvk_query_report_offset_B(pool, query);
}

static void
nvk_sync_queries_to_gpu(struct nvk_query_pool *pool,
                        uint32_t first_query, uint32_t count)
{
   if (pool->mem->flags & NVKMD_MEM_COHERENT)
      return;

   assert(pool->layout == NVK_QUERY_POOL_LAYOUT_ALIGNED_INTERLEAVED);
   nvkmd_mem_sync_map_to_gpu(pool->mem, first_query * pool->query_stride,
                             count * pool->query_stride);
}

static void
nvk_sync_queries_from_gpu(struct nvk_query_pool *pool,
                          uint32_t first_query, uint32_t count)
{
   if (pool->mem->flags & NVKMD_MEM_COHERENT)
      return;

   assert(pool->layout == NVK_QUERY_POOL_LAYOUT_ALIGNED_INTERLEAVED);
   nvkmd_mem_sync_map_from_gpu(pool->mem, first_query * pool->query_stride,
                               count * pool->query_stride);
}

/**
 * Goes through a series of consecutive query indices in the given pool,
 * setting all element values to 0 and emitting them as available.
 */
static void
emit_zero_queries(struct nvk_cmd_buffer *cmd, struct nvk_query_pool *pool,
                  uint32_t first_index, uint32_t num_queries)
{
   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT: {
      for (uint32_t i = 0; i < num_queries; i++) {
         uint64_t addr = nvk_query_available_addr(pool, first_index + i);

         struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
         P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
         P_NV9097_SET_REPORT_SEMAPHORE_A(p, addr >> 32);
         P_NV9097_SET_REPORT_SEMAPHORE_B(p, addr);
         P_NV9097_SET_REPORT_SEMAPHORE_C(p, 1);
         P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
            .operation = OPERATION_RELEASE,
            .release = RELEASE_AFTER_ALL_PRECEEDING_WRITES_COMPLETE,
            .pipeline_location = PIPELINE_LOCATION_ALL,
            .structure_size = STRUCTURE_SIZE_ONE_WORD,
         });
      }
      break;
   }
   default:
      UNREACHABLE("Unsupported query type");
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_ResetQueryPool(VkDevice device,
                   VkQueryPool queryPool,
                   uint32_t firstQuery,
                   uint32_t queryCount)
{
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);

   if (pool->layout == NVK_QUERY_POOL_LAYOUT_SEPARATE) {
      assert(pool->mem->flags & NVKMD_MEM_COHERENT);
      uint32_t *available = nvk_query_available_map(pool, firstQuery);
      memset(available, 0, queryCount * sizeof(*available));
   } else {
      for (uint32_t i = 0; i < queryCount; i++) {
         uint32_t *available = nvk_query_available_map(pool, firstQuery + i);
         *available = 0;
      }
      nvk_sync_queries_to_gpu(pool, firstQuery, queryCount);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdResetQueryPool(VkCommandBuffer commandBuffer,
                      VkQueryPool queryPool,
                      uint32_t firstQuery,
                      uint32_t queryCount)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);

   for (uint32_t i = 0; i < queryCount; i++) {
      uint64_t addr = nvk_query_available_addr(pool, firstQuery + i);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
      P_NV9097_SET_REPORT_SEMAPHORE_A(p, addr >> 32);
      P_NV9097_SET_REPORT_SEMAPHORE_B(p, addr);
      P_NV9097_SET_REPORT_SEMAPHORE_C(p, 0);
      P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
         .operation = OPERATION_RELEASE,
         .release = RELEASE_AFTER_ALL_PRECEEDING_WRITES_COMPLETE,
         .pipeline_location = PIPELINE_LOCATION_ALL,
         .structure_size = STRUCTURE_SIZE_ONE_WORD,
      });
   }

   /* Wait for the above writes to complete.  This prevents WaW hazards on any
    * later query availability updates and ensures vkCmdCopyQueryPoolResults
    * will see the query as unavailable if it happens before the query is
    * completed again.
    */
   for (uint32_t i = 0; i < queryCount; i++) {
      uint64_t addr = nvk_query_available_addr(pool, firstQuery + i);

      struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
      __push_mthd(p, SUBC_NV9097, NV906F_SEMAPHOREA);
      P_NV906F_SEMAPHOREA(p, addr >> 32);
      P_NV906F_SEMAPHOREB(p, (addr & UINT32_MAX) >> 2);
      P_NV906F_SEMAPHOREC(p, 0);
      P_NV906F_SEMAPHORED(p, {
         .operation = OPERATION_ACQUIRE,
         .acquire_switch = ACQUIRE_SWITCH_ENABLED,
         .release_size = RELEASE_SIZE_4BYTE,
      });
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                       VkPipelineStageFlags2 stage,
                       VkQueryPool queryPool,
                       uint32_t query)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);

   struct nv_push *p = nvk_cmd_buffer_push(cmd, 10);

   uint64_t report_addr = nvk_query_report_addr(pool, query);
   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
   P_NV9097_SET_REPORT_SEMAPHORE_A(p, report_addr >> 32);
   P_NV9097_SET_REPORT_SEMAPHORE_B(p, report_addr);
   P_NV9097_SET_REPORT_SEMAPHORE_C(p, 0);
   P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
      .operation = OPERATION_REPORT_ONLY,
      .pipeline_location = vk_stage_flags_to_nv9097_pipeline_location(stage),
      .structure_size = STRUCTURE_SIZE_FOUR_WORDS,
   });

   uint64_t available_addr = nvk_query_available_addr(pool, query);
   P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
   P_NV9097_SET_REPORT_SEMAPHORE_A(p, available_addr >> 32);
   P_NV9097_SET_REPORT_SEMAPHORE_B(p, available_addr);
   P_NV9097_SET_REPORT_SEMAPHORE_C(p, 1);
   P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
      .operation = OPERATION_RELEASE,
      .release = RELEASE_AFTER_ALL_PRECEEDING_WRITES_COMPLETE,
      .pipeline_location = PIPELINE_LOCATION_ALL,
      .structure_size = STRUCTURE_SIZE_ONE_WORD,
   });

   /* From the Vulkan spec:
    *
    *   "If vkCmdWriteTimestamp2 is called while executing a render pass
    *    instance that has multiview enabled, the timestamp uses N consecutive
    *    query indices in the query pool (starting at query) where N is the
    *    number of bits set in the view mask of the subpass the command is
    *    executed in. The resulting query values are determined by an
    *    implementation-dependent choice of one of the following behaviors:"
    *
    * In our case, only the first query is used, so we emit zeros for the
    * remaining queries, as described in the first behavior listed in the
    * Vulkan spec:
    *
    *   "The first query is a timestamp value and (if more than one bit is set
    *   in the view mask) zero is written to the remaining queries."
    */
   if (cmd->state.gfx.render.view_mask != 0) {
      const uint32_t num_queries =
         util_bitcount(cmd->state.gfx.render.view_mask);
      if (num_queries > 1)
         emit_zero_queries(cmd, pool, query + 1, num_queries - 1);
   }
}

struct nvk_3d_stat_query {
   VkQueryPipelineStatisticFlagBits flag;
   uint8_t loc;
   uint8_t report;
};

/* This must remain sorted in flag order */
static const struct nvk_3d_stat_query nvk_3d_stat_queries[] = {{
   .flag    = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_DATA_ASSEMBLER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_DA_VERTICES_GENERATED,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_DATA_ASSEMBLER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_DA_PRIMITIVES_GENERATED,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_VERTEX_SHADER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_VS_INVOCATIONS,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_GEOMETRY_SHADER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_GS_INVOCATIONS,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_GEOMETRY_SHADER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_GS_PRIMITIVES_GENERATED,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_VPC, /* TODO */
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_CLIPPER_INVOCATIONS,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_VPC, /* TODO */
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_CLIPPER_PRIMITIVES_GENERATED,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_PIXEL_SHADER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_PS_INVOCATIONS,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_TESSELATION_INIT_SHADER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_TI_INVOCATIONS,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT,
   .loc     = NV9097_SET_REPORT_SEMAPHORE_D_PIPELINE_LOCATION_TESSELATION_SHADER,
   .report  = NV9097_SET_REPORT_SEMAPHORE_D_REPORT_TS_INVOCATIONS,
}, {
   .flag    = VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT,
   .loc     = UINT8_MAX,
   .report  = UINT8_MAX,
}};

static void
mme_store_global(struct mme_builder *b,
                 struct mme_value64 addr,
                 struct mme_value v)
{
   mme_mthd(b, NV9097_SET_REPORT_SEMAPHORE_A);
   mme_emit_addr64(b, addr);
   mme_emit(b, v);
   mme_emit(b, mme_imm(0x10000000));
}

void
nvk_mme_write_cs_invocations(struct mme_builder *b)
{
   struct mme_value64 dst_addr = mme_load_addr64(b);

   struct mme_value accum_hi = mme_state(b,
      NVC597_SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_CS_INVOCATIONS_HI));
   struct mme_value accum_lo = mme_state(b,
      NVC597_SET_MME_SHADOW_SCRATCH(NVK_MME_SCRATCH_CS_INVOCATIONS_LO));
   struct mme_value64 accum = mme_value64(accum_lo, accum_hi);

   mme_store_global(b, dst_addr, accum.lo);
   mme_store_global(b, mme_add64(b, dst_addr, mme_imm64(4)), accum.hi);
}

static void
nvk_cmd_begin_end_query(struct nvk_cmd_buffer *cmd,
                        struct nvk_query_pool *pool,
                        uint32_t query, uint32_t index,
                        bool end)
{
   const struct nvk_device *dev = nvk_cmd_buffer_device(cmd);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);

   uint64_t report_addr = nvk_query_report_addr(pool, query) +
                          end * sizeof(struct nvk_query_report);

   uint32_t end_size = 7 * end;

   struct nv_push *p;
   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
      p = nvk_cmd_buffer_push(cmd, 7 + end_size);

      P_IMMD(p, NV9097, SET_ZPASS_PIXEL_COUNT, !end);

      P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
      P_NV9097_SET_REPORT_SEMAPHORE_A(p, report_addr >> 32);
      P_NV9097_SET_REPORT_SEMAPHORE_B(p, report_addr);
      P_NV9097_SET_REPORT_SEMAPHORE_C(p, 0);
      P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
         .operation = OPERATION_REPORT_ONLY,
         .pipeline_location = PIPELINE_LOCATION_ALL,
         .report = REPORT_ZPASS_PIXEL_CNT64,
         .structure_size = STRUCTURE_SIZE_FOUR_WORDS,
         .flush_disable = true,
      });
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      uint32_t stat_count = util_bitcount(pool->vk.pipeline_statistics);
      p = nvk_cmd_buffer_push(cmd, stat_count * 5 + end_size);

      ASSERTED uint32_t stats_left = pool->vk.pipeline_statistics;
      for (uint32_t i = 0; i < ARRAY_SIZE(nvk_3d_stat_queries); i++) {
         const struct nvk_3d_stat_query *sq = &nvk_3d_stat_queries[i];
         if (!(stats_left & sq->flag))
            continue;

         /* The 3D stat queries array MUST be sorted */
         assert(!(stats_left & (sq->flag - 1)));

         if (sq->flag == VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT) {
            if (pdev->info.cls_compute >= AMPERE_COMPUTE_B)
               P_1INC(p, NVC7C0, CALL_MME_MACRO(NVK_MME_WRITE_CS_INVOCATIONS));
            else
               P_1INC(p, NVC597, CALL_MME_MACRO(NVK_MME_WRITE_CS_INVOCATIONS));
            P_INLINE_DATA(p, report_addr >> 32);
            P_INLINE_DATA(p, report_addr);
         } else {
            P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
            P_NV9097_SET_REPORT_SEMAPHORE_A(p, report_addr >> 32);
            P_NV9097_SET_REPORT_SEMAPHORE_B(p, report_addr);
            P_NV9097_SET_REPORT_SEMAPHORE_C(p, 0);
            P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
               .operation = OPERATION_REPORT_ONLY,
               .pipeline_location = sq->loc,
               .report = sq->report,
               .structure_size = STRUCTURE_SIZE_FOUR_WORDS,
               .flush_disable = true,
            });
         }

         report_addr += 2 * sizeof(struct nvk_query_report);
         stats_left &= ~sq->flag;
      }
      break;
   }

   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      const uint32_t xfb_reports[] = {
         NV9097_SET_REPORT_SEMAPHORE_D_REPORT_STREAMING_PRIMITIVES_SUCCEEDED,
         NV9097_SET_REPORT_SEMAPHORE_D_REPORT_STREAMING_PRIMITIVES_NEEDED,
      };
      p = nvk_cmd_buffer_push(cmd, 5 * ARRAY_SIZE(xfb_reports) + end_size);
      for (uint32_t i = 0; i < ARRAY_SIZE(xfb_reports); ++i) {
         P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
         P_NV9097_SET_REPORT_SEMAPHORE_A(p, report_addr >> 32);
         P_NV9097_SET_REPORT_SEMAPHORE_B(p, report_addr);
         P_NV9097_SET_REPORT_SEMAPHORE_C(p, 0);
         P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
            .operation = OPERATION_REPORT_ONLY,
            .pipeline_location = PIPELINE_LOCATION_STREAMING_OUTPUT,
            .report = xfb_reports[i],
            .structure_size = STRUCTURE_SIZE_FOUR_WORDS,
            .sub_report = index,
            .flush_disable = true,
         });
         report_addr += 2 * sizeof(struct nvk_query_report);
      }
      break;
   }

   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
      p = nvk_cmd_buffer_push(cmd, 5 + end_size);

      P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
      P_NV9097_SET_REPORT_SEMAPHORE_A(p, report_addr >> 32);
      P_NV9097_SET_REPORT_SEMAPHORE_B(p, report_addr);
      P_NV9097_SET_REPORT_SEMAPHORE_C(p, 1);
      P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
         .operation = OPERATION_REPORT_ONLY,
         .pipeline_location = PIPELINE_LOCATION_STREAMING_OUTPUT,
         .report = REPORT_VTG_PRIMITIVES_OUT,
         .sub_report = index,
         .structure_size = STRUCTURE_SIZE_FOUR_WORDS,
         .flush_disable = true,
      });
      break;

   default:
      UNREACHABLE("Unsupported query type");
   }

   if (end) {
      P_IMMD(p, NV9097, FLUSH_PENDING_WRITES, 0);

      uint64_t available_addr = nvk_query_available_addr(pool, query);
      P_MTHD(p, NV9097, SET_REPORT_SEMAPHORE_A);
      P_NV9097_SET_REPORT_SEMAPHORE_A(p, available_addr >> 32);
      P_NV9097_SET_REPORT_SEMAPHORE_B(p, available_addr);
      P_NV9097_SET_REPORT_SEMAPHORE_C(p, 1);
      P_NV9097_SET_REPORT_SEMAPHORE_D(p, {
         .operation = OPERATION_RELEASE,
         .release = RELEASE_AFTER_ALL_PRECEEDING_WRITES_COMPLETE,
         .pipeline_location = PIPELINE_LOCATION_ALL,
         .structure_size = STRUCTURE_SIZE_ONE_WORD,
      });
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer,
                            VkQueryPool queryPool,
                            uint32_t query,
                            VkQueryControlFlags flags,
                            uint32_t index)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);

   nvk_cmd_begin_end_query(cmd, pool, query, index, false);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer,
                          VkQueryPool queryPool,
                          uint32_t query,
                          uint32_t index)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);

   nvk_cmd_begin_end_query(cmd, pool, query, index, true);

   /* From the Vulkan spec:
    *
    *   "If queries are used while executing a render pass instance that has
    *    multiview enabled, the query uses N consecutive query indices in
    *    the query pool (starting at query) where N is the number of bits set
    *    in the view mask in the subpass the query is used in. How the
    *    numerical results of the query are distributed among the queries is
    *    implementation-dependent."
    *
    * In our case, only the first query is used, so we emit zeros for the
    * remaining queries.
    */
   if (cmd->state.gfx.render.view_mask != 0) {
      const uint32_t num_queries =
         util_bitcount(cmd->state.gfx.render.view_mask);
      if (num_queries > 1)
         emit_zero_queries(cmd, pool, query + 1, num_queries - 1);
   }
}

static bool
nvk_query_is_available(struct nvk_query_pool *pool, uint32_t query)
{
   uint32_t *available = nvk_query_available_map(pool, query);
   return p_atomic_read(available) != 0;
}

#define NVK_QUERY_TIMEOUT 2000000000ull

static VkResult
nvk_query_wait_for_available(struct nvk_device *dev,
                             struct nvk_query_pool *pool,
                             uint32_t query,
                             uint64_t abs_timeout_ns)
{
   if (nvk_query_is_available(pool, query))
      return VK_SUCCESS;

   do {
      VkResult status = vk_device_check_status(&dev->vk);
      if (status != VK_SUCCESS)
         return status;

      nvk_sync_queries_from_gpu(pool, query, 1);

      if (nvk_query_is_available(pool, query))
         return VK_SUCCESS;
   } while (os_time_get_nano() < abs_timeout_ns);

   return vk_device_set_lost(&dev->vk, "query timeout");
}

static void
cpu_write_query_result(void *dst, uint32_t idx,
                       VkQueryResultFlags flags,
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
cpu_get_query_delta(void *dst, const struct nvk_query_report *src,
                    uint32_t idx, VkQueryResultFlags flags)
{
   uint64_t delta = src[idx * 2 + 1].value - src[idx * 2].value;
   cpu_write_query_result(dst, idx, flags, delta);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetQueryPoolResults(VkDevice device,
                        VkQueryPool queryPool,
                        uint32_t firstQuery,
                        uint32_t queryCount,
                        size_t dataSize,
                        void *pData,
                        VkDeviceSize stride,
                        VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);
   VkResult status = VK_SUCCESS;

   if (vk_device_is_lost(&dev->vk))
      return VK_ERROR_DEVICE_LOST;

   nvk_sync_queries_from_gpu(pool, firstQuery, queryCount);

   if (flags & VK_QUERY_RESULT_WAIT_BIT) {
      uint64_t abs_timeout_ns = os_time_get_absolute_timeout(NVK_QUERY_TIMEOUT);
      for (uint32_t i = 0; i < queryCount; i++) {
         status = nvk_query_wait_for_available(dev, pool, firstQuery + i,
                                               abs_timeout_ns);
         if (status != VK_SUCCESS)
            return status;
      }
   }

   for (uint32_t i = 0; i < queryCount; i++) {
      const uint32_t query = firstQuery + i;

      /* If we waited, then we know it's available */
      bool available = (flags & VK_QUERY_RESULT_WAIT_BIT) != 0 ||
                       nvk_query_is_available(pool, query);
      bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

      const struct nvk_query_report *src = nvk_query_report_map(pool, query);
      assert(i * stride < dataSize);
      void *dst = (char *)pData + i * stride;

      const uint32_t report_count = vk_query_pool_report_count(&pool->vk);
      if (pool->vk.query_type == VK_QUERY_TYPE_TIMESTAMP) {
         /* Timestamps are just a single query */
         assert(report_count == 1);
         if (write_results)
            cpu_write_query_result(dst, 0, flags, src->timestamp);
      } else {
         /* For everything else, we have to compute deltas */
         if (write_results) {
            for (uint32_t j = 0; j < report_count; j++)
               cpu_get_query_delta(dst, src, j, flags);
         }
      }

      if (!write_results)
         status = VK_NOT_READY;

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cpu_write_query_result(dst, report_count, flags, available);
   }

   return status;
}

struct nvk_copy_query_push {
   uint64_t pool_addr;
   uint32_t available_stride;
   uint32_t reports_start;
   uint32_t report_count;
   uint32_t query_stride;
   uint32_t first_query;
   uint32_t query_count;
   uint64_t dst_addr;
   uint64_t dst_stride;
   uint32_t flags;
};

static nir_def *
load_struct_var(nir_builder *b, nir_variable *var, uint32_t field)
{
   nir_deref_instr *deref =
      nir_build_deref_struct(b, nir_build_deref_var(b, var), field);
   return nir_load_deref(b, deref);
}

static nir_shader *
build_copy_queries_shader(void)
{
   nir_builder build =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL,
                                     "nvk-meta-copy-queries");
   nir_builder *b = &build;

   struct glsl_struct_field push_fields[] = {
      { .type = glsl_uint64_t_type(), .name = "pool_addr", .offset = 0 },
      { .type = glsl_uint_type(), .name = "available_stride", .offset = 8 },
      { .type = glsl_uint_type(), .name = "reports_start", .offset = 12 },
      { .type = glsl_uint_type(), .name = "report_count", .offset = 16 },
      { .type = glsl_uint_type(), .name = "query_stride", .offset = 20 },
      { .type = glsl_uint_type(), .name = "first_query", .offset = 24 },
      { .type = glsl_uint_type(), .name = "query_count", .offset = 28 },
      { .type = glsl_uint64_t_type(), .name = "dst_addr", .offset = 32 },
      { .type = glsl_uint64_t_type(), .name = "dst_stride", .offset = 40 },
      { .type = glsl_uint_type(), .name = "flags", .offset = 48 },
   };
   const struct glsl_type *push_iface_type =
      glsl_interface_type(push_fields, ARRAY_SIZE(push_fields),
                          GLSL_INTERFACE_PACKING_STD140,
                          false /* row_major */, "push");
   nir_variable *push = nir_variable_create(b->shader, nir_var_mem_push_const,
                                            push_iface_type, "push");

   b->shader->info.workgroup_size[0] = 32;

   nvk_copy_queries(b, load_struct_var(b, push, 0), load_struct_var(b, push, 1),
                    load_struct_var(b, push, 2), load_struct_var(b, push, 3),
                    load_struct_var(b, push, 4), load_struct_var(b, push, 5),
                    load_struct_var(b, push, 6), load_struct_var(b, push, 7),
                    load_struct_var(b, push, 8), load_struct_var(b, push, 9));

   return build.shader;
}

static struct nvk_shader *
atomic_set_or_destroy_shader(struct nvk_device *dev,
                             struct nvk_shader **shader_ptr,
                             struct nvk_shader *shader,
                             const VkAllocationCallbacks *alloc)
{
   struct nvk_shader *old_shader = p_atomic_cmpxchg(shader_ptr, NULL, shader);
   if (old_shader == NULL) {
      return shader;
   } else {
      vk_shader_destroy(&dev->vk, &shader->vk, alloc);
      return old_shader;
   }
}

static VkResult
get_copy_queries_shader(struct nvk_device *dev,
                        struct nvk_shader **shader_out)
{
   struct nvk_shader *shader = p_atomic_read(&dev->copy_queries);
   if (shader != NULL) {
      *shader_out = shader;
      return VK_SUCCESS;
   }

   nir_shader *nir = build_copy_queries_shader();
   VkResult result = nvk_compile_nir_shader(dev, nir, &dev->vk.alloc, &shader);
   if (result != VK_SUCCESS)
      return result;

   *shader_out = atomic_set_or_destroy_shader(dev, &dev->copy_queries,
                                              shader, &dev->vk.alloc);

   return VK_SUCCESS;
}

static void
nvk_meta_copy_query_pool_results(struct nvk_cmd_buffer *cmd,
                                 struct nvk_query_pool *pool,
                                 uint32_t first_query,
                                 uint32_t query_count,
                                 uint64_t dst_addr,
                                 uint64_t dst_stride,
                                 VkQueryResultFlags flags)
{
   struct nvk_device *dev = nvk_cmd_buffer_device(cmd);

   struct nvk_shader *shader;
   VkResult result = get_copy_queries_shader(dev, &shader);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   if (pool->vk.query_type == VK_QUERY_TYPE_TIMESTAMP)
      flags |= NVK_QUERY_IS_TIMESTAMP;

   const struct nvk_copy_query_push push = {
      .pool_addr = pool->mem->va->addr,
      .available_stride = nvk_query_available_stride_B(pool),
      .reports_start = pool->reports_start,
      .report_count = vk_query_pool_report_count(&pool->vk),
      .query_stride = pool->query_stride,
      .first_query = first_query,
      .query_count = query_count,
      .dst_addr = dst_addr,
      .dst_stride = dst_stride,
      .flags = flags,
   };
   nvk_cmd_dispatch_shader(cmd, shader, &push, sizeof(push),
                           DIV_ROUND_UP(query_count, 32), 1, 1);
}

VKAPI_ATTR void VKAPI_CALL
nvk_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer,
                            VkQueryPool queryPool,
                            uint32_t firstQuery,
                            uint32_t queryCount,
                            VkBuffer dstBuffer,
                            VkDeviceSize dstOffset,
                            VkDeviceSize stride,
                            VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(nvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(nvk_query_pool, pool, queryPool);
   VK_FROM_HANDLE(nvk_buffer, dst_buffer, dstBuffer);

   if (flags & VK_QUERY_RESULT_WAIT_BIT) {
      for (uint32_t i = 0; i < queryCount; i++) {
         uint64_t avail_addr = nvk_query_available_addr(pool, firstQuery + i);

         struct nv_push *p = nvk_cmd_buffer_push(cmd, 5);
         __push_mthd(p, SUBC_NV9097, NV906F_SEMAPHOREA);
         P_NV906F_SEMAPHOREA(p, avail_addr >> 32);
         P_NV906F_SEMAPHOREB(p, (avail_addr & UINT32_MAX) >> 2);
         P_NV906F_SEMAPHOREC(p, 1);
         P_NV906F_SEMAPHORED(p, {
            .operation = OPERATION_ACQ_GEQ,
            .acquire_switch = ACQUIRE_SWITCH_ENABLED,
            .release_size = RELEASE_SIZE_4BYTE,
         });
      }
   }

   uint64_t dst_addr = vk_buffer_address(&dst_buffer->vk, dstOffset);
   nvk_meta_copy_query_pool_results(cmd, pool, firstQuery, queryCount,
                                    dst_addr, stride, flags);
}

