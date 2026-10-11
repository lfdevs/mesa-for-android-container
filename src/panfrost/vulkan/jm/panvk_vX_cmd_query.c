/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "util/os_time.h"

#include "libpan_dgc.h"
#include "nir_builder.h"

#include "vk_log.h"
#include "vk_meta.h"
#include "vk_pipeline.h"

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_meta.h"
#include "panvk_cmd_precomp.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_query_pool.h"

#include "libpan.h"

static nir_def *
panvk_nir_query_report_dev_addr(nir_builder *b, nir_def *pool_addr,
                                nir_def *query_stride, nir_def *query)
{
   return nir_iadd(b, pool_addr, nir_umul_2x32_64(b, query, query_stride));
}

static nir_def *
panvk_nir_available_dev_addr(nir_builder *b, nir_def *available_addr,
                             nir_def *query)
{
   nir_def *offset = nir_imul_imm(b, query, sizeof(uint32_t));
   return nir_iadd(b, available_addr, nir_u2u64(b, offset));
}

static void
panvk_emit_write_job(struct panvk_cmd_buffer *cmd, struct panvk_batch *batch,
                     enum mali_write_value_type type, uint64_t addr,
                     uint64_t value)
{
   struct pan_ptr job =
      pan_pool_alloc_desc(&cmd->desc_pool.base, WRITE_VALUE_JOB);

   pan_section_pack(job.cpu, WRITE_VALUE_JOB, PAYLOAD, payload) {
      payload.type = type;
      payload.address = addr;
      payload.immediate_value = value;
   };

   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_WRITE_VALUE, true, false, 0, 0,
                  &job, false);
   util_dynarray_append(&batch->jobs, job.cpu);
}

static struct panvk_batch *
open_batch(struct panvk_cmd_buffer *cmd, bool *had_batch)
{
   bool res = cmd->cur_batch != NULL;

   if (!res)
      panvk_per_arch(cmd_open_batch)(cmd);

   *had_batch = res;

   return cmd->cur_batch;
}

static void
close_batch(struct panvk_cmd_buffer *cmd, bool had_batch)
{
   if (!had_batch)
      panvk_per_arch(cmd_close_batch)(cmd);
}

static void
panvk_emit_clear_queries(struct panvk_cmd_buffer *cmd,
                         struct panvk_query_pool *pool, bool availaible,
                         uint32_t first_query, uint32_t query_count)
{
   const struct panlib_clear_query_result_args push = {
      .pool_addr = panvk_priv_mem_dev_addr(pool->mem),
      .available_addr = panvk_priv_mem_dev_addr(pool->available_mem),
      .query_stride = pool->query_stride,
      .first_query = first_query,
      .query_count = query_count,
      .report_count = pool->reports_per_query,
      .availaible_value = availaible,
   };

   bool had_batch;
   open_batch(cmd, &had_batch);
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmd);
   panlib_clear_query_result_struct(&precomp_ctx, panlib_1d(query_count),
                                    PANLIB_BARRIER_NONE, push);
   close_batch(cmd, had_batch);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdResetQueryPool)(VkCommandBuffer commandBuffer,
                                  VkQueryPool queryPool, uint32_t firstQuery,
                                  uint32_t queryCount)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   if (queryCount == 0)
      return;

   panvk_emit_clear_queries(cmd, pool, false, firstQuery, queryCount);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdWriteTimestamp2)(VkCommandBuffer commandBuffer,
                                   VkPipelineStageFlags2 stage,
                                   VkQueryPool queryPool, uint32_t query)
{
   UNUSED VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   UNUSED VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   panvk_stub();
}

#if PAN_ARCH == 9
/* ponytail: as on CSF, the fragment shader invocation count is the sample
 * count of the hardware occlusion counter, pointed at the report while no
 * occlusion query owns it. It misses fragments killed by late depth/stencil
 * tests, counts samples with MSAA, and draws made while an occlusion query is
 * active are not counted.
 */
static void
pstats_take_oq(struct panvk_cmd_buffer *cmd)
{
   uint64_t fs = panvk_pstats_counter_addr(
      &cmd->state.gfx.pstats_query,
      VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT, true);

   if (!fs ||
       cmd->state.gfx.occlusion_query.mode != MALI_OCCLUSION_MODE_DISABLED)
      return;

   cmd->state.gfx.occlusion_query.ptr = fs;
   cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_COUNTER;
   gfx_state_set_dirty(cmd, OQ);
}

/* With multiview, a query takes one slot per view. render.view_mask outlives
 * the render pass, so stay inside the pool. */
static uint32_t
query_view_count(struct panvk_cmd_buffer *cmd, struct panvk_query_pool *pool,
                 uint32_t query)
{
   const uint32_t view_mask = cmd->state.gfx.render.view_mask;

   return MIN2(view_mask ? util_bitcount(view_mask) : 1,
               pool->vk.query_count - query);
}
#endif

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginQueryIndexedEXT)(VkCommandBuffer commandBuffer,
                                        VkQueryPool queryPool, uint32_t query,
                                        VkQueryControlFlags flags,
                                        uint32_t index)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   bool had_batch;
   struct panvk_batch *batch = open_batch(cmd, &had_batch);
   uint64_t report_addr = panvk_query_report_dev_addr(pool, query);
   uint32_t zero_queries = 1;

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      cmd->state.gfx.occlusion_query.ptr = report_addr;
      cmd->state.gfx.occlusion_query.mode = flags & VK_QUERY_CONTROL_PRECISE_BIT
                                               ? MALI_OCCLUSION_MODE_COUNTER
                                               : MALI_OCCLUSION_MODE_PREDICATE;
      gfx_state_set_dirty(cmd, OQ);

      /* From the Vulkan spec:
       *
       *   "When an occlusion query begins, the count of passing samples
       *    always starts at zero."
       *
       */
      break;
   }
#if PAN_ARCH == 9
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
      cmd->state.gfx.prims_generated_query.ptr = report_addr;
      zero_queries = query_view_count(cmd, pool, query);
      break;
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      cmd->state.gfx.xfb.queries[index] = report_addr;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      cmd->state.gfx.pstats_query = (struct panvk_pstats_query_state){
         .report = report_addr,
         .mask = pool->vk.pipeline_statistics,
      };
      /* With multiview, the other views' queries are all zero. */
      zero_queries = query_view_count(cmd, pool, query);
      pstats_take_oq(cmd);
      break;
#endif
   default:
      UNREACHABLE("Unsupported query type");
   }

   /* Counters start at zero. The write jobs come before the jobs and
    * fragment jobs of this batch that count into the report. */
   for (uint32_t q = 0; q < zero_queries; q++) {
      for (unsigned i = 0; i < pool->reports_per_query; i++) {
         panvk_emit_write_job(
            cmd, batch, MALI_WRITE_VALUE_TYPE_IMMEDIATE_64,
            panvk_query_report_dev_addr(pool, query + q) +
               i * sizeof(struct panvk_query_report),
            0);
      }
   }

   close_batch(cmd, had_batch);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndQueryIndexedEXT)(VkCommandBuffer commandBuffer,
                                      VkQueryPool queryPool, uint32_t query,
                                      uint32_t index)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   bool end_sync = cmd->cur_batch != NULL;

   /* Close to ensure we are sync and flush caches */
   if (end_sync)
      panvk_per_arch(cmd_close_batch)(cmd);

   bool had_batch;
   struct panvk_batch *batch = open_batch(cmd, &had_batch);
   had_batch |= end_sync;
   uint32_t view_count = 1;

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      cmd->state.gfx.occlusion_query.ptr = 0;
      cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
      gfx_state_set_dirty(cmd, OQ);
#if PAN_ARCH == 9
      /* Give the counter back to an active pipeline statistics query. */
      pstats_take_oq(cmd);
#endif
      break;
   }
#if PAN_ARCH == 9
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
      cmd->state.gfx.prims_generated_query.ptr = 0;
      view_count = query_view_count(cmd, pool, query);
      break;
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      cmd->state.gfx.xfb.queries[index] = 0;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      uint64_t fs = panvk_pstats_counter_addr(
         &cmd->state.gfx.pstats_query,
         VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT, true);

      if (fs && cmd->state.gfx.occlusion_query.ptr == fs) {
         cmd->state.gfx.occlusion_query.ptr = 0;
         cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
         gfx_state_set_dirty(cmd, OQ);
      }
      cmd->state.gfx.pstats_query = (struct panvk_pstats_query_state){0};
      view_count = query_view_count(cmd, pool, query);
      break;
   }
#endif
   default:
      UNREACHABLE("Unsupported query type");
   }

   /* The batch closed above ran every job and fragment job counting into
    * the report. */
   for (uint32_t v = 0; v < view_count; v++) {
      panvk_emit_write_job(cmd, batch, MALI_WRITE_VALUE_TYPE_IMMEDIATE_32,
                           panvk_query_available_dev_addr(pool, query + v), 1);
   }

   close_batch(cmd, had_batch);
}

static void
panvk_meta_copy_query_pool_results(struct panvk_cmd_buffer *cmd,
                                   struct panvk_query_pool *pool,
                                   uint32_t first_query, uint32_t query_count,
                                   uint64_t dst_addr, uint64_t dst_stride,
                                   VkQueryResultFlags flags)
{
   const struct panlib_copy_query_result_args push = {
      .pool_addr = panvk_priv_mem_dev_addr(pool->mem),
      .available_addr = panvk_priv_mem_dev_addr(pool->available_mem),
      .query_stride = pool->query_stride,
      .first_query = first_query,
      .query_count = query_count,
      .dst_addr = dst_addr,
      .dst_stride = dst_stride,
      .query_type = pool->vk.query_type,
      .flags = flags,
      .report_count = pool->reports_per_query,
   };

   bool had_batch;
   open_batch(cmd, &had_batch);
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmd);
#if PAN_ARCH == 9
   if (pool->vk.query_type == VK_QUERY_TYPE_PIPELINE_STATISTICS ||
       pool->vk.query_type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT) {
      const bool xfb =
         pool->vk.query_type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
      const struct panlib_copy_pstats_query_result_args pstats = {
         .pool_addr = panvk_query_report_dev_addr(pool, first_query),
         .available_addr = panvk_query_available_dev_addr(pool, first_query),
         .query_stride = pool->query_stride,
         .query_count = query_count,
         .dst_addr = dst_addr,
         .dst_stride = dst_stride,
         .flags = flags,
         .stat_count = xfb ? 2 : util_bitcount(pool->vk.pipeline_statistics),
      };

      panlib_copy_pstats_query_result_struct(
         &precomp_ctx, panlib_1d(query_count), PANLIB_BARRIER_NONE, pstats);
      close_batch(cmd, had_batch);
      return;
   }
#endif
   panlib_copy_query_result_struct(&precomp_ctx, panlib_1d(query_count),
                                   PANLIB_BARRIER_NONE, push);
   close_batch(cmd, had_batch);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyQueryPoolResults)(
   VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
   uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
   VkDeviceSize stride, VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);
   VK_FROM_HANDLE(panvk_buffer, dst_buffer, dstBuffer);

   /* XXX: Do we really need that barrier when EndQuery already handle it? */
   if ((flags & VK_QUERY_RESULT_WAIT_BIT) && cmd->cur_batch != NULL) {
      close_batch(cmd, true);
   }

   uint64_t dst_addr = panvk_buffer_gpu_ptr(dst_buffer, dstOffset);
   panvk_meta_copy_query_pool_results(cmd, pool, firstQuery, queryCount,
                                      dst_addr, stride, flags);
}
