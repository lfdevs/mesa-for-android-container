/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include "util/os_time.h"

#include "cs_builder.h"

#include "vk_enum_defines.h"
#include "vk_log.h"
#include "vk_synchronization.h"

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_dispatch.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_meta.h"
#include "panvk_cmd_precomp.h"
#include "panvk_cmd_ts.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_query_pool.h"
#include "panvk_queue.h"
#include "panvk_shader.h"

static enum panvk_subqueue_id
panvk_subqueue_for_query_type(VkQueryType type)
{
   /* timestamp queries are not handled here , because they may be written
    * from any subqueue */
   switch (type) {
   case VK_QUERY_TYPE_OCCLUSION:
      return PANVK_SUBQUEUE_FRAGMENT;
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      return PANVK_SUBQUEUE_COMPUTE;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      /* Availability is signalled from the fragment subqueue, after the
       * compute subqueue and the fragment jobs are done. */
      return PANVK_SUBQUEUE_FRAGMENT;
   default:
      UNREACHABLE("Unsupported query type");
   }
}

/* At the API level, a query consists of a status and a result.  Both are
 * uninitialized initially.  There are these query operations:
 *
 *  - Reset op sets the status to unavailable and leaves the result undefined.
 *  - Begin/End pair or Write op sets the status to available and the result
 *    to the final query value.  Because of VK_QUERY_RESULT_PARTIAL_BIT, the
 *    result must hold valid intermediate query values while the query is
 *    active.
 *  - Copy op copies the result and optionally the status to a buffer.
 *
 * All query operations define execution dependencies among themselves when
 * they reference the same queries.  The only exception is the Copy op when
 * VK_QUERY_RESULT_WAIT_BIT is not set.
 *
 * We use a panvk_cs_sync32 to store the status of a query:
 *
 *  - Reset op waits on all prior query operations affecting the query before
 *    setting the seqno to 0 synchronously.
 *  - Begin op does not access the seqno.
 *  - End or Write op sets the seqno to 1 asynchronously.
 *  - Copy op waits on the seqno only when VK_QUERY_RESULT_WAIT_BIT is set.
 *
 * Because Reset op acts as a full barrier, End or Write op knows the seqno is
 * 0 and does not need to wait.
 */

/* Default path for query types that don't need special logic */
static void
reset_queries_batch(struct cs_builder *b, struct cs_index addr,
               struct cs_index zero_regs, uint32_t query_count)
{
   const uint32_t regs_per_query = 2;
   const uint32_t queries_per_batch = zero_regs.size / regs_per_query;
   uint32_t remaining_queries = query_count;

   assert(zero_regs.size > 2 && ALIGN_POT(zero_regs.size, 2) == zero_regs.size);

   if (query_count > queries_per_batch * 4) {
      struct cs_index counter = cs_reg32(b, zero_regs.reg + zero_regs.size - 1);
      struct cs_index new_zero_regs =
         cs_reg_tuple(b, zero_regs.reg, zero_regs.size - 2);
      const uint32_t adjusted_queries_per_batch =
         new_zero_regs.size / regs_per_query;
      uint32_t full_batches = query_count / adjusted_queries_per_batch;

      cs_move32_to(b, counter, full_batches);
      cs_while(b, MALI_CS_CONDITION_GREATER, counter) {
         cs_store(b, new_zero_regs, addr, BITFIELD_MASK(new_zero_regs.size), 0);
         cs_add_imm64(b, addr, addr, new_zero_regs.size * sizeof(uint32_t));
         cs_add_imm32(b, counter, counter, -1);
      }

      remaining_queries =
         query_count - (full_batches * adjusted_queries_per_batch);
   }

   for (uint32_t i = 0; i < remaining_queries; i += queries_per_batch) {
      struct cs_index new_zero_regs = cs_reg_tuple(
         b, zero_regs.reg,
         MIN2(remaining_queries - i, queries_per_batch) * regs_per_query);

      cs_store(b, new_zero_regs, addr, BITFIELD_MASK(new_zero_regs.size),
               i * sizeof(uint32_t));
   }
}

static void
panvk_cmd_reset_queries(struct panvk_cmd_buffer *cmd,
                        struct panvk_query_pool *pool, uint32_t first_query,
                        uint32_t query_count)
{
   enum panvk_subqueue_id subqueue =
      panvk_subqueue_for_query_type(pool->vk.query_type);
   struct cs_builder *b = panvk_get_cs_builder(cmd, subqueue);

   /* Wait on deferred sync to ensure all prior query operations have
    * completed
    */
   cs_wait_slot(b, SB_ID(DEFERRED_SYNC));

   struct cs_index addr = cs_scratch_reg64(b, 16);
   struct cs_index zero_regs = cs_scratch_reg_tuple(b, 0, 16);

   for (uint32_t i = 0; i < zero_regs.size; i += 2)
      cs_move64_to(b, cs_scratch_reg64(b, i), 0);

   /* Zero all query syncobj so it reports non-available. We don't use
    * cs_sync32_set() because no-one is waiting on this syncobj with
    * cs_sync32_wait(). The only reason we use a syncobj is so we can
    * defer the signalling in the issue_fragmnent_jobs() path. */
   cs_move64_to(b, addr, panvk_query_available_dev_addr(pool, first_query));
   reset_queries_batch(b, addr, zero_regs, query_count);

   /* Pipeline statistics reports are written from several subqueues and
    * zeroed by Begin on the subqueue that owns each counter, zeroing them
    * here could race with a following Begin. */
   if (pool->vk.query_type != VK_QUERY_TYPE_PIPELINE_STATISTICS &&
       pool->vk.query_type != VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT) {
      cs_move64_to(b, addr, panvk_query_report_dev_addr(pool, first_query));
      reset_queries_batch(b, addr, zero_regs, query_count);
   }

   /* reset_queries_batch() only does the stores, we need to flush those explicitly
    * here. */
   cs_flush_stores(b);

   /* We flush the caches to make the new value visible to the CPU. */
   struct cs_index flush_id = cs_scratch_reg32(b, 0);

   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
                   MALI_CS_OTHER_FLUSH_MODE_NONE, flush_id,
                   cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
   cs_wait_slot(b, SB_ID(IMM_FLUSH));
}

static void
copy_result_batch(struct cs_builder *b,
                  VkQueryResultFlags flags,
                  struct cs_index dst_addr,
                  VkDeviceSize dst_stride,
                  struct cs_index res_addr,
                  struct cs_index avail_addr,
                  struct cs_index scratch_regs,
                  uint32_t query_count)
{
   uint32_t res_size = (flags & VK_QUERY_RESULT_64_BIT) ? 2 : 1;
   uint32_t regs_per_copy =
      res_size + ((flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 1 : 0);

   assert(query_count <= scratch_regs.size / regs_per_copy);

   for (uint32_t i = 0; i < query_count; i++) {
      struct cs_index res =
         cs_reg_tuple(b, scratch_regs.reg + (i * regs_per_copy), res_size);
      struct cs_index avail = cs_reg32(b, res.reg + res_size);

      cs_load_to(b, res, res_addr, BITFIELD_MASK(res.size),
                 i * sizeof(uint64_t));

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cs_load32_to(b, avail, avail_addr, i * sizeof(struct panvk_cs_sync32));
   }

   for (uint32_t i = 0; i < query_count; i++) {
      struct cs_index store_src =
         cs_reg_tuple(b, scratch_regs.reg + (i * regs_per_copy), regs_per_copy);

      cs_store(b, store_src, dst_addr, BITFIELD_MASK(regs_per_copy),
               i * dst_stride);
   }

   /* Flush the stores. */
   cs_flush_stores(b);
}

static void
panvk_copy_query_results(struct panvk_cmd_buffer *cmd,
                         struct panvk_query_pool *pool,
                         uint32_t first_query, uint32_t query_count,
                         uint64_t dst_buffer_addr,
                         VkDeviceSize stride,
                         VkQueryResultFlags flags)
{
   enum panvk_subqueue_id subqueue =
      panvk_subqueue_for_query_type(pool->vk.query_type);
   struct cs_builder *b = panvk_get_cs_builder(cmd, subqueue);

   /* Wait for query syncobjs to be signalled. */
   if (flags & VK_QUERY_RESULT_WAIT_BIT)
      cs_wait_slot(b, SB_ID(DEFERRED_SYNC));

   uint32_t res_size = (flags & VK_QUERY_RESULT_64_BIT) ? 2 : 1;
   uint32_t regs_per_copy =
      res_size + ((flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 1 : 0);

   struct cs_index dst_addr = cs_scratch_reg64(b, 16);
   struct cs_index res_addr = cs_scratch_reg64(b, 14);
   struct cs_index avail_addr = cs_scratch_reg64(b, 12);
   struct cs_index counter = cs_scratch_reg32(b, 11);
   struct cs_index scratch_regs = cs_scratch_reg_tuple(b, 0, 11);
   uint32_t queries_per_batch = scratch_regs.size / regs_per_copy;

   if (stride > 0) {
      /* Store offset is a 16-bit signed integer, so we might be limited by the
       * stride here. */
      queries_per_batch = MIN2(((1u << 15) / stride) + 1, queries_per_batch);
   }

   /* Stop unrolling the loop when it takes more than 2 steps to copy the
    * queries. */
   if (query_count > 2 * queries_per_batch) {
      uint32_t copied_query_count =
         query_count - (query_count % queries_per_batch);

      cs_move32_to(b, counter, copied_query_count);
      cs_move64_to(b, dst_addr, dst_buffer_addr);
      cs_move64_to(b, res_addr, panvk_query_report_dev_addr(pool, first_query));
      cs_move64_to(b, avail_addr,
                   panvk_query_available_dev_addr(pool, first_query));
      cs_while(b, MALI_CS_CONDITION_GREATER, counter) {
         copy_result_batch(b, flags, dst_addr, stride, res_addr, avail_addr,
                           scratch_regs, queries_per_batch);

         cs_add_imm32(b, counter, counter, -queries_per_batch);
         cs_add_imm64(b, dst_addr, dst_addr, queries_per_batch * stride);
         cs_add_imm64(b, res_addr, res_addr,
                      queries_per_batch * sizeof(uint64_t));
         cs_add_imm64(b, avail_addr, avail_addr,
                      queries_per_batch * sizeof(uint64_t));
      }

      dst_buffer_addr += stride * copied_query_count;
      first_query += copied_query_count;
      query_count -= copied_query_count;
   }

   for (uint32_t i = 0; i < query_count; i += queries_per_batch) {
      cs_move64_to(b, dst_addr, dst_buffer_addr + (i * stride));
      cs_move64_to(b, res_addr,
                   panvk_query_report_dev_addr(pool, i + first_query));
      cs_move64_to(b, avail_addr,
                   panvk_query_available_dev_addr(pool, i + first_query));
      copy_result_batch(b, flags, dst_addr, stride, res_addr, avail_addr,
                        scratch_regs,
                        MIN2(queries_per_batch, query_count - i));
   }
}

static void
panvk_cmd_begin_occlusion_query(struct panvk_cmd_buffer *cmd,
                                struct panvk_query_pool *pool, uint32_t query,
                                VkQueryControlFlags flags)
{
   uint64_t report_addr = panvk_query_report_dev_addr(pool, query);

   cmd->state.gfx.occlusion_query.ptr = report_addr;
   cmd->state.gfx.occlusion_query.syncobj =
      panvk_query_available_dev_addr(pool, query);
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
   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_FRAGMENT);

   struct cs_index report_addr_gpu = cs_scratch_reg64(b, 0);
   struct cs_index clear_value = cs_scratch_reg64(b, 2);
   cs_move64_to(b, report_addr_gpu, report_addr);
   cs_move64_to(b, clear_value, 0);
   cs_store64(b, clear_value, report_addr_gpu, 0);
   cs_flush_stores(b);
}

static void
panvk_cmd_end_occlusion_query(struct panvk_cmd_buffer *cmd,
                              struct panvk_query_pool *pool, uint32_t query)
{
   uint64_t syncobj_addr = panvk_query_available_dev_addr(pool, query);
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);

   cmd->state.gfx.occlusion_query.ptr = 0;
   cmd->state.gfx.occlusion_query.syncobj = 0;
   cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
   gfx_state_set_dirty(cmd, OQ);
   panvk_per_arch(cmd_pstats_restore_oq)(cmd);

   /* If the render pass is active, we let EndRendering take care of the
    * occlusion query end when the fragment job is issued. */
   if (cmd->state.gfx.render.oq.last == syncobj_addr)
      return;

   /* Multiview can only be active inside of a renderpass.
    * A query that begins in a subpass, must end in the same subpass.
    * Therefore, if the occlusion query ends outside of a render pass, multiview
    * should not be active. */
   assert(cmd->state.gfx.render.view_mask == 0);

   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_FRAGMENT);
   struct cs_index oq_syncobj = cs_scratch_reg64(b, 0);
   struct cs_index val = cs_scratch_reg32(b, 2);

   /* OQ accumulates sample counts to the report which is on a cached memory.
    * Wait for the accumulation and flush the caches.
    */
   cs_move32_to(b, val, 0);
   cs_flush_caches(
      b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
      MALI_CS_OTHER_FLUSH_MODE_NONE, val,
      cs_defer(dev->csf.sb.all_iters_mask, SB_ID(DEFERRED_FLUSH)));

   /* Signal the query syncobj after the flush is effective. */
   cs_move32_to(b, val, 1);
   cs_move64_to(b, oq_syncobj, panvk_query_available_dev_addr(pool, query));
   cs_sync32_set(b, true, MALI_CS_SYNC_SCOPE_CSG, val, oq_syncobj,
                 cs_defer(SB_MASK(DEFERRED_FLUSH), SB_ID(DEFERRED_SYNC)));
}

static void
panvk_cmd_reset_timestamp_queries(struct panvk_cmd_buffer *cmd,
                                  struct panvk_query_pool *pool,
                                  uint32_t first_query, uint32_t query_count)
{
   for (int sq = 0; sq < PANVK_SUBQUEUE_COUNT; ++sq) {
      struct cs_builder *b = panvk_get_cs_builder(cmd, sq);

      struct cs_index zeros = cs_scratch_reg_tuple(b, 0, 4);
      struct cs_index zero64 = cs_scratch_reg64(b, 0);
      struct cs_index addr = cs_scratch_reg64(b, 4);
      struct cs_index counter = cs_scratch_reg32(b, 6);

      int offset = sq * sizeof(struct panvk_query_report);

      for (uint32_t i = 0; i < zeros.size; i += 2)
         cs_move64_to(b, cs_scratch_reg64(b, i), 0);

      cs_move32_to(b, counter, query_count);
      cs_move64_to(b, addr, panvk_query_report_dev_addr(pool, first_query));

      /* Wait for timestamp writes. */
      cs_wait_slot(b, SB_ID(LS));

      cs_while(b, MALI_CS_CONDITION_GREATER, counter) {
         /* If the info subqueue is the last one, it can reset the info field in
          * one store because of the memory layout of the query report values. */
         STATIC_ASSERT(PANVK_QUERY_TS_INFO_SUBQUEUE ==
                       PANVK_SUBQUEUE_COUNT - 1);
         if (sq == PANVK_QUERY_TS_INFO_SUBQUEUE)
            cs_store(b, zeros, addr, BITFIELD_MASK(zeros.size), offset);
         else
            cs_store64(b, zero64, addr, offset);

         cs_add_imm64(b, addr, addr, pool->query_stride);
         cs_add_imm32(b, counter, counter, -1);
      }

      cs_flush_stores(b);
   }

   /* Reset availability from the info subqueue because we also use that queue
    * to signal the availability later. */
   struct cs_builder *b =
      panvk_get_cs_builder(cmd, PANVK_QUERY_TS_INFO_SUBQUEUE);
   struct cs_index addr = cs_scratch_reg64(b, 16);
   struct cs_index zero_regs = cs_scratch_reg_tuple(b, 0, 16);
   cs_move64_to(b, addr, panvk_query_available_dev_addr(pool, first_query));
   reset_queries_batch(b, addr, zero_regs, query_count);
   cs_flush_stores(b);
}

static uint32_t
vk_stage_to_timestamp_subqueue_mask(VkPipelineStageFlagBits2 vk_stage)
{
   assert(util_bitcount64(vk_stage) == 1);

   /* Handle special stages. */
   if (vk_stage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT)
      return BITFIELD_BIT(PANVK_SUBQUEUE_VERTEX_TILER) |
             BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);
   if (vk_stage == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT)
      return BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT) |
             BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);
   if (vk_stage == VK_PIPELINE_STAGE_2_HOST_BIT)
      /* We need to map host to something, so map it to compute to not interfer
       * with drawing. */
      return BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);

   uint32_t result = vk_stages_to_subqueue_mask(vk_stage, SYNC_SCOPE_FIRST);

   /* All stages should map to at least one subqueue. */
   assert(util_bitcount(result) > 0);

   return result;
}

static void
panvk_cs_write_ts_info(struct panvk_cmd_buffer *cmd,
                       VkPipelineStageFlags2 stage,
                       struct panvk_query_pool *pool, uint32_t first_query)
{
   const uint32_t n_views =
      MAX2(1, util_bitcount(cmd->state.gfx.render.view_mask));

   /* Store the timestamp info needed during copy. */
   struct cs_builder *b =
      panvk_get_cs_builder(cmd, PANVK_QUERY_TS_INFO_SUBQUEUE);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index info = cs_scratch_reg64(b, 2);
   int offset = PANVK_SUBQUEUE_COUNT * sizeof(struct panvk_query_report);

   uint64_t ts_info = panvk_timestamp_info_encode(
      stage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT ? PANVK_QUERY_TS_OP_MIN
                                                   : PANVK_QUERY_TS_OP_MAX,
      vk_stage_to_timestamp_subqueue_mask(stage));

   cs_move64_to(b, info, ts_info);
   for (uint32_t query = first_query; query < first_query + n_views; ++query) {
      cs_move64_to(b, addr, panvk_query_report_dev_addr(pool, query));
      cs_store64(b, info, addr, offset);
   }
}

static void
panvk_add_finished_query(struct panvk_cmd_buffer *cmd,
                         VkPipelineStageFlags2 stage,
                         struct panvk_query_pool *pool, uint32_t query)
{
   struct cs_builder *b =
      panvk_get_cs_builder(cmd, PANVK_QUERY_TS_INFO_SUBQUEUE);

   struct pan_ptr new_ts_node = panvk_cmd_alloc_dev_mem(
      cmd, desc, sizeof(struct panvk_cs_timestamp_query), 8);

   *((struct panvk_cs_timestamp_query *)new_ts_node.cpu) =
      (struct panvk_cs_timestamp_query){
         .node = {.next = 0},
         .reports = panvk_query_report_dev_addr(pool, query),
         .avail = panvk_query_available_dev_addr(pool, query),
      };

   struct cs_index new_node_ptr = cs_scratch_reg64(b, 0);
   cs_move64_to(b, new_node_ptr, new_ts_node.gpu);

   cs_single_link_list_add_tail(
      b, cs_subqueue_ctx_reg(b),
      offsetof(struct panvk_cs_subqueue_context, render.ts_done_chain),
      new_node_ptr, offsetof(struct panvk_cs_timestamp_query, node),
      cs_scratch_reg_tuple(b, 10, 4));
}

static void
panvk_cs_defer_timestamp(struct panvk_cmd_buffer *cmd,
                         VkPipelineStageFlags2 stage,
                         struct panvk_query_pool *pool, uint32_t query)
{
   /* Deferring top of pipe doesn't make sense. */
   assert(VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT != stage);

   const uint32_t write_sq_mask = vk_stage_to_timestamp_subqueue_mask(stage);
   const uint32_t n_views =
      MAX2(1, util_bitcount(cmd->state.gfx.render.view_mask));

   /* Each subqueue in write_sq_mask must write a timestamp value.
    * Additionally, the info subqueue needs to move the deferred timestamp
    * into the list of timestamps to be signalled later - Regardless of
    * whether a timestamp is needed from that subqueue.
    */
   for (uint32_t sq = 0; sq < PANVK_SUBQUEUE_COUNT; ++sq) {
      if (((write_sq_mask | BITFIELD_BIT(PANVK_QUERY_TS_INFO_SUBQUEUE)) &
           BITFIELD_BIT(sq)) == 0)
         continue;

      bool write_report =
         (sq != PANVK_QUERY_TS_INFO_SUBQUEUE) ||
         (write_sq_mask & BITFIELD_BIT(PANVK_QUERY_TS_INFO_SUBQUEUE)) != 0;

      struct cs_builder *b = panvk_get_cs_builder(cmd, sq);

      for (uint32_t q = query; q < query + n_views; ++q) {
         struct pan_ptr new_ts_node = panvk_cmd_alloc_dev_mem(
            cmd, desc, sizeof(struct panvk_cs_timestamp_query), 8);
         *((struct panvk_cs_timestamp_query *)new_ts_node.cpu) =
            (struct panvk_cs_timestamp_query){
               .node = {.next = 0},
               .reports =
                  write_report ? panvk_query_report_dev_addr(pool, q) : 0,
               .avail = panvk_query_available_dev_addr(pool, q),
            };

         struct cs_index new_node_ptr = cs_scratch_reg64(b, 0);
         cs_move64_to(b, new_node_ptr, new_ts_node.gpu);
         cs_single_link_list_add_tail(
            b, cs_subqueue_ctx_reg(b),
            offsetof(struct panvk_cs_subqueue_context, render.ts_chain),
            new_node_ptr, offsetof(struct panvk_cs_timestamp_query, node),
            cs_scratch_reg_tuple(b, 10, 4));
      }
   }
}

static void
panvk_cs_write_timestamp(struct panvk_cmd_buffer *cmd,
                         VkPipelineStageFlags2 stage,
                         struct panvk_query_pool *pool, uint32_t query)
{
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);

   const uint32_t write_sq_mask = vk_stage_to_timestamp_subqueue_mask(stage);
   const uint32_t n_views =
      MAX2(1, util_bitcount(cmd->state.gfx.render.view_mask));

   for (uint32_t sq = 0; sq < PANVK_SUBQUEUE_COUNT; ++sq) {
      if ((write_sq_mask & BITFIELD_BIT(sq)) == 0)
         continue;

      struct cs_builder *b = panvk_get_cs_builder(cmd, sq);
      struct cs_index addr = cs_scratch_reg64(b, 0);
      int offset = sq * sizeof(struct panvk_query_report);

      for (uint32_t q = query; q < query + n_views; ++q) {
         /* Wait for prev. timestamp so they increase monotonically. */
         cs_wait_slot(b, SB_ID(LS));
         cs_move64_to(b, addr, panvk_query_report_dev_addr(pool, q));
         cs_store_state(b, addr, offset, MALI_CS_STATE_TIMESTAMP,
                        cs_defer(dev->csf.sb.all_iters_mask, SB_ID(LS)));
      }
   }

   /* Store the queries syncobj for signalling at the end of this cmdbuf. */
   for (uint32_t q = query; q < query + n_views; ++q)
      panvk_add_finished_query(cmd, stage, pool, q);
}

static void
panvk_cmd_write_timestamp_query(struct panvk_cmd_buffer *cmd,
                                VkPipelineStageFlags2 stage,
                                struct panvk_query_pool *pool, uint32_t query)
{
   /* Store the actual timestamp values per subqueue. */
   const uint32_t write_sq_mask = vk_stage_to_timestamp_subqueue_mask(stage);

   /* The timestamp has to be written after RUN_FRAGMENT if we are inside
    * a renderpass at the moment and cover the F subqueue.
    */
   const bool in_rp = cmd->state.gfx.render.tiler || inherits_render_ctx(cmd);
   const bool defer =
      in_rp && (write_sq_mask & BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT));

   if (defer)
      panvk_cs_defer_timestamp(cmd, stage, pool, query);
   else
      panvk_cs_write_timestamp(cmd, stage, pool, query);

   panvk_cs_write_ts_info(cmd, stage, pool, query);

   cmd->state.contains_timestamp_queries = true;
}

static void
panvk_copy_timestamp_query_results(struct panvk_cmd_buffer *cmd,
                                   struct panvk_query_pool *pool,
                                   uint32_t first_query, uint32_t query_count,
                                   uint64_t dst_buffer_addr,
                                   VkDeviceSize stride,
                                   VkQueryResultFlags flags)
{
   /*
    * Step 1:
    * The point of this is to have each subqueue "save" its own value
    * into a buffer, such that any following query operations like reset
    * don't have to worry about destroying the result before other
    * subqueues are done with it.
    */

   uint32_t query_stride = pool->query_stride;
   size_t buf_sz = query_count * query_stride;
   struct pan_ptr intermediate_buf =
      panvk_cmd_alloc_dev_mem(cmd, desc, buf_sz, 16);

   for (uint32_t sq = 0; sq < PANVK_SUBQUEUE_COUNT; ++sq) {
      struct cs_builder *b = panvk_get_cs_builder(cmd, sq);
      uint32_t sq_offset = sq * sizeof(uint64_t);

      struct cs_index src = cs_scratch_reg64(b, 0);
      struct cs_index dst = cs_scratch_reg64(b, 2);
      struct cs_index tmp = cs_scratch_reg64(b, 4);
      struct cs_index tmp2 = cs_scratch_reg64(b, 6);

      /* Wait for STORE_STATEs to finish. */
      cs_wait_slot(b, SB_ID(LS));

      cs_move64_to(b, src, panvk_query_report_dev_addr(pool, first_query));
      cs_move64_to(b, dst, intermediate_buf.gpu);

      struct cs_index count = cs_scratch_reg32(b, 8);
      cs_move32_to(b, count, query_count);
      cs_while(b, MALI_CS_CONDITION_GREATER, count) {
         cs_load64_to(b, tmp, src, sq_offset);
         if (sq == PANVK_QUERY_TS_INFO_SUBQUEUE) {
            assert(PANVK_QUERY_TS_INFO_SUBQUEUE == PANVK_SUBQUEUE_COUNT - 1);
            cs_load64_to(b, tmp2, src, sq_offset + 8);
         }
         cs_store64(b, tmp, dst, sq_offset);
         if (sq == PANVK_QUERY_TS_INFO_SUBQUEUE)
            cs_store64(b, tmp2, dst, sq_offset + 8);

         cs_add_imm64(b, src, src, query_stride);
         cs_add_imm64(b, dst, dst, query_stride);
         cs_add_imm32(b, count, count, -1);
      }
   }

   /* Make sure C waits for all copies to be done. */
   struct panvk_cs_deps deps = {0};
   deps.dst[PANVK_SUBQUEUE_COMPUTE].wait_subqueue_mask =
      BITFIELD_MASK(PANVK_SUBQUEUE_COUNT) & ~BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);
   u_foreach_bit(i, deps.dst[PANVK_SUBQUEUE_COMPUTE].wait_subqueue_mask)
      deps.src[i].wait_sb_mask = SB_MASK(LS);
   panvk_per_arch(emit_barrier)(cmd, deps);

   /* Step 2: Copy from the intermediate into the application buffer. */

   const struct panlib_copy_ts_query_result_args push = {
      .pool_addr = intermediate_buf.gpu,
      .available_addr = panvk_query_available_dev_addr(pool, first_query),
      .query_stride = pool->query_stride,
      /* The intermediate buffer starts at first_query. */
      .first_query = 0,
      .query_count = query_count,
      .report_count = pool->reports_per_query,
      .dst_addr = dst_buffer_addr,
      .dst_stride = stride,
      .flags = flags,
   };

   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmd);
   panlib_copy_ts_query_result_struct(&precomp_ctx, panlib_1d(query_count),
                                      PANLIB_BARRIER_CSF_SYNC, push);
}

static void
panvk_cmd_begin_prims_generated_query(
   struct panvk_cmd_buffer *cmd, struct panvk_query_pool *pool, uint32_t query,
   VkQueryControlFlags flags)
{
   uint64_t report_addr = panvk_query_report_dev_addr(pool, query);

   cmd->state.gfx.prims_generated_query.ptr = report_addr;
   cmd->state.gfx.prims_generated_query.syncobj =
      panvk_query_available_dev_addr(pool, query);

   /* From the Vulkan spec:
    *
    *   "When a primitives generated query begins, the count of primitives
    *    generated starts from zero."
    *
    */
   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_COMPUTE);

   struct cs_index report_addr_gpu = cs_scratch_reg64(b, 0);
   struct cs_index clear_value = cs_scratch_reg64(b, 2);
   cs_move64_to(b, report_addr_gpu, report_addr);
   cs_move64_to(b, clear_value, 0);
   cs_store64(b, clear_value, report_addr_gpu, 0);
   cs_flush_stores(b);
}

static void
panvk_cmd_end_prims_generated_query(
   struct panvk_cmd_buffer *cmd, struct panvk_query_pool *pool, uint32_t query)
{
   cmd->state.gfx.prims_generated_query.ptr = 0;
   cmd->state.gfx.prims_generated_query.syncobj = 0;

   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index query_syncobj = cs_scratch_reg64(b, 0);
   struct cs_index val = cs_scratch_reg32(b, 2);

   /* Query accumulates sample counts to the report which is on a cached memory.
    * Wait for the accumulation and flush the caches.
    *
    * No need to wait on iter_sb because all shader invocations to update the
    * counter use PANLIB_BARRIER_CSF_WAIT already, and all direct draws update
    * the counter synchronously. The gpu_prerast GS kernel adds to the counter
    * without such a barrier, so wait for all iterators.
    */
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);
   cs_move32_to(b, val, 0);
   cs_flush_caches(
      b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
      MALI_CS_OTHER_FLUSH_MODE_NONE, val,
      cs_defer(dev->csf.sb.all_iters_mask, SB_ID(DEFERRED_FLUSH)));

   /* Signal the query syncobj after the flush is effective. */
   cs_move32_to(b, val, 1);
   cs_move64_to(b, query_syncobj, panvk_query_available_dev_addr(pool, query));
   cs_sync32_set(b, true, MALI_CS_SYNC_SCOPE_CSG, val, query_syncobj,
                 cs_defer(SB_MASK(DEFERRED_FLUSH), SB_ID(DEFERRED_SYNC)));
}

static void
panvk_cmd_begin_xfb_query(struct panvk_cmd_buffer *cmd,
                          struct panvk_query_pool *pool, uint32_t query,
                          uint32_t index)
{
   uint64_t report_addr = panvk_query_report_dev_addr(pool, query);

   cmd->state.gfx.xfb.queries[index] = report_addr;

   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index addr = cs_scratch_reg64(b, 0);
   struct cs_index zero = cs_scratch_reg64(b, 2);

   cs_move64_to(b, addr, report_addr);
   cs_move64_to(b, zero, 0);
   for (uint32_t i = 0; i < pool->reports_per_query; i++)
      cs_store64(b, zero, addr, i * sizeof(uint64_t));
   cs_flush_stores(b);
}

static void
panvk_cmd_end_xfb_query(struct panvk_cmd_buffer *cmd,
                        struct panvk_query_pool *pool, uint32_t query,
                        uint32_t index)
{
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);

   cmd->state.gfx.xfb.queries[index] = 0;

   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_COMPUTE);
   struct cs_index query_syncobj = cs_scratch_reg64(b, 0);
   struct cs_index val = cs_scratch_reg32(b, 2);

   cs_wait_slots(b, dev->csf.sb.all_iters_mask);
   cs_move32_to(b, val, 0);
   cs_flush_caches(
      b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
      MALI_CS_OTHER_FLUSH_MODE_NONE, val,
      cs_defer(SB_IMM_MASK, SB_ID(DEFERRED_FLUSH)));

   cs_move32_to(b, val, 1);
   cs_move64_to(b, query_syncobj, panvk_query_available_dev_addr(pool, query));
   cs_sync32_set(b, true, MALI_CS_SYNC_SCOPE_CSG, val, query_syncobj,
                 cs_defer(SB_MASK(DEFERRED_FLUSH), SB_ID(DEFERRED_SYNC)));
}

#define PSTAT(name) VK_QUERY_PIPELINE_STATISTIC_##name##_BIT

/* The largest per-draw or per-dispatch increment added with CS immediates,
 * bigger ones go through the GPU kernels. */
#define PSTATS_CS_MAX_ADD (8ull * INT32_MAX)

static void
pstats_cs_add(struct cs_builder *b, uint64_t addr, uint64_t value)
{
   if (!addr || !value)
      return;

   struct cs_index a = cs_scratch_reg64(b, 0);
   struct cs_index v = cs_scratch_reg64(b, 2);

   cs_move64_to(b, a, addr);
   cs_load64_to(b, v, a, 0);
   for (; value > INT32_MAX; value -= INT32_MAX)
      cs_add_imm64(b, v, v, INT32_MAX);
   cs_add_imm64(b, v, v, value);
   cs_store64(b, v, a, 0);
   cs_flush_stores(b);
}

static uint64_t
pstats_kernel_half(const struct panvk_pstats_query_state *q)
{
   return q->report + util_bitcount(q->mask) * sizeof(uint64_t);
}

/* Packs a 4-bit slot per counter for panlib_update_pstats_draw. */
static uint32_t
pstats_draw_slots(const struct panvk_pstats_query_state *q, uint32_t counted)
{
   static const uint32_t order[] = {
      PSTAT(INPUT_ASSEMBLY_VERTICES), PSTAT(INPUT_ASSEMBLY_PRIMITIVES),
      PSTAT(VERTEX_SHADER_INVOCATIONS), PSTAT(CLIPPING_INVOCATIONS),
      PSTAT(CLIPPING_PRIMITIVES),
   };
   uint32_t slots = 0;

   for (uint32_t i = 0; i < ARRAY_SIZE(order); i++) {
      uint32_t slot = (counted & order[i])
                         ? util_bitcount(q->mask & (order[i] - 1))
                         : 0xf;
      slots |= slot << (i * 4);
   }

   return slots;
}

void
panvk_per_arch(cmd_pstats_draw)(struct panvk_cmd_buffer *cmdbuf,
                                const struct panvk_draw_info *draw)
{
   const struct panvk_pstats_query_state *q = &cmdbuf->state.gfx.pstats_query;

   if (!q->report)
      return;

   /* ponytail: VS invocations are counted as IA vertices, without vertex
    * reuse, and clipping primitives as clipping invocations, since nothing
    * reports the clipper output. Both are implementation-defined. With a
    * geometry shader, the GS kernel counts clipping from its output.
    */
   uint32_t counted = q->mask & (PSTAT(INPUT_ASSEMBLY_VERTICES) |
                                 PSTAT(INPUT_ASSEMBLY_PRIMITIVES) |
                                 PSTAT(VERTEX_SHADER_INVOCATIONS));
   const uint32_t patch_size =
      cmdbuf->vk.dynamic_graphics_state.ts.patch_control_points;
   /* The GS and tessellation kernels count clipping from their output. */
   if (!cmdbuf->state.gfx.gs.shader && !cmdbuf->state.gfx.tes.shader) {
      counted |= q->mask & (PSTAT(CLIPPING_INVOCATIONS) |
                            PSTAT(CLIPPING_PRIMITIVES));
   }
   if (!counted)
      return;

   const uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
   const uint32_t view_count = view_mask ? util_bitcount(view_mask) : 1;
   const bool restart = draw->index.index_size && draw->index.restart_enable;
   const uint64_t instances = (uint64_t)draw->instance.count * view_count;
   const uint64_t verts = draw->vertex.count * instances;

   if (!draw->indirect.buffer_dev_addr && !restart &&
       verts <= PSTATS_CS_MAX_ADD) {
      struct cs_builder *b =
         panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
      uint64_t prims =
         (draw->prim == MESA_PRIM_PATCHES
             ? draw->vertex.count / MAX2(patch_size, 1)
             : u_decomposed_prims_for_vertices(draw->prim,
                                               draw->vertex.count)) *
         instances;

      pstats_cs_add(b, panvk_pstats_counter_addr(
                          q, counted & PSTAT(INPUT_ASSEMBLY_VERTICES), false),
                    verts);
      pstats_cs_add(b, panvk_pstats_counter_addr(
                          q, counted & PSTAT(INPUT_ASSEMBLY_PRIMITIVES), false),
                    prims);
      pstats_cs_add(b, panvk_pstats_counter_addr(
                          q, counted & PSTAT(VERTEX_SHADER_INVOCATIONS), false),
                    verts);
      pstats_cs_add(b, panvk_pstats_counter_addr(
                          q, counted & PSTAT(CLIPPING_INVOCATIONS), false),
                    prims);
      pstats_cs_add(b, panvk_pstats_counter_addr(
                          q, counted & PSTAT(CLIPPING_PRIMITIVES), false),
                    prims);
      return;
   }

   /* Indirect, primitive restart or huge draws: count on the GPU, one
    * workgroup per draw, atomically into the shader half of the report. */
   uint64_t cmd = draw->indirect.buffer_dev_addr;
   uint32_t cmd_stride = draw->indirect.stride;
   uint32_t draw_count = draw->indirect.draw_count;

   if (!cmd) {
      struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(VkDrawIndexedIndirectCommand), 16);
      if (!ptr.gpu)
         return;

      *(VkDrawIndexedIndirectCommand *)ptr.cpu = (VkDrawIndexedIndirectCommand){
         .indexCount = draw->vertex.count,
         .instanceCount = draw->instance.count,
         .firstIndex = draw->index.offset,
         .vertexOffset = draw->vertex.base,
         .firstInstance = draw->instance.base,
      };
      cmd = ptr.gpu;
      cmd_stride = 0;
      draw_count = 1;
   }

   const uint32_t index_size = draw->index.index_size;
   struct panlib_update_pstats_draw_args args = {
      .stats = pstats_kernel_half(q),
      .slots = pstats_draw_slots(q, counted),
      .cmd = cmd,
      .cmd_stride = cmd_stride,
      .count_buffer = draw->indirect.count_buffer_dev_addr,
      .draw_index = draw->indirect.draw_index,
      .index_buffer = cmdbuf->state.gfx.ib.dev_addr,
      .index_buffer_size_el =
         index_size ? cmdbuf->state.gfx.ib.size / index_size : 0,
      .index_size_B = index_size,
      .restart = restart,
      .prim = draw->prim,
      .view_count = view_count,
      .patch_size = patch_size,
   };
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   panlib_update_pstats_draw_struct(&precomp_ctx, panlib_1d(draw_count),
                                    PANLIB_BARRIER_CSF_SYNC, args);
}

void
panvk_per_arch(cmd_pstats_dispatch)(struct panvk_cmd_buffer *cmdbuf,
                                    const struct panvk_shader_variant *cs,
                                    const struct panvk_dispatch_info *info)
{
   const struct panvk_pstats_query_state *q = &cmdbuf->state.gfx.pstats_query;

   if (!panvk_pstats_counter_addr(q, PSTAT(COMPUTE_SHADER_INVOCATIONS), false))
      return;

   /* ponytail: vk_meta compute dispatches go through here too and are
    * counted, filter them if an app notices.
    */
   const uint32_t local_size =
      cs->cs.local_size.x * cs->cs.local_size.y * cs->cs.local_size.z;
   const uint64_t invocations = (uint64_t)info->direct.wg_count.x *
                                info->direct.wg_count.y *
                                info->direct.wg_count.z * local_size;
   uint64_t cmd = info->indirect.buffer_dev_addr;

   if (!cmd && invocations <= PSTATS_CS_MAX_ADD) {
      pstats_cs_add(panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE),
                    panvk_pstats_counter_addr(
                       q, PSTAT(COMPUTE_SHADER_INVOCATIONS), false),
                    invocations);
      return;
   }

   if (!cmd) {
      struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(
         cmdbuf, desc, sizeof(VkDispatchIndirectCommand), 16);
      if (!ptr.gpu)
         return;

      *(VkDispatchIndirectCommand *)ptr.cpu = (VkDispatchIndirectCommand){
         .x = info->direct.wg_count.x,
         .y = info->direct.wg_count.y,
         .z = info->direct.wg_count.z,
      };
      cmd = ptr.gpu;
   }

   struct panlib_update_pstats_dispatch_args args = {
      .counter =
         panvk_pstats_counter_addr(q, PSTAT(COMPUTE_SHADER_INVOCATIONS), true),
      .cmd = cmd,
      .local_size = local_size,
   };
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   panlib_update_pstats_dispatch_struct(&precomp_ctx, panlib_1d(1),
                                        PANLIB_BARRIER_CSF_SYNC, args);
}

/* ponytail: the fragment shader invocation count is the sample count of the
 * hardware occlusion counter, pointed at the report while no occlusion query
 * owns it. It misses fragments killed by late depth/stencil tests and counts
 * samples rather than invocations with MSAA, and draws made while an
 * occlusion query is active are not counted. Counting in the fragment shader
 * would need a shader variant.
 */
static void
pstats_take_oq(struct panvk_cmd_buffer *cmd)
{
   struct panvk_pstats_query_state *q = &cmd->state.gfx.pstats_query;
   uint64_t fs = panvk_pstats_counter_addr(
      q, PSTAT(FRAGMENT_SHADER_INVOCATIONS), true);

   if (!fs || !q->oq_sync ||
       cmd->state.gfx.occlusion_query.mode != MALI_OCCLUSION_MODE_DISABLED)
      return;

   cmd->state.gfx.occlusion_query.ptr = fs;
   cmd->state.gfx.occlusion_query.syncobj = q->oq_sync;
   cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_COUNTER;
   gfx_state_set_dirty(cmd, OQ);
}

void
panvk_per_arch(cmd_pstats_restore_oq)(struct panvk_cmd_buffer *cmd)
{
   pstats_take_oq(cmd);
}

static void
pstats_signal(struct panvk_cmd_buffer *cmd, uint64_t avail,
              uint32_t view_count)
{
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);
   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_FRAGMENT);
   struct cs_index syncobj = cs_scratch_reg64(b, 0);
   struct cs_index val = cs_scratch_reg32(b, 2);

   /* Wait for the fragment jobs feeding the occlusion counter and make all
    * counters visible before flagging the query available. */
   cs_move32_to(b, val, 0);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
                   MALI_CS_OTHER_FLUSH_MODE_NONE, val,
                   cs_defer(dev->csf.sb.all_iters_mask, SB_ID(DEFERRED_FLUSH)));

   cs_move32_to(b, val, 1);
   for (uint32_t v = 0; v < view_count; v++) {
      cs_move64_to(b, syncobj,
                   avail + v * sizeof(struct panvk_query_available_obj));
      cs_sync32_set(b, true, MALI_CS_SYNC_SCOPE_CSG, val, syncobj,
                    cs_defer(SB_MASK(DEFERRED_FLUSH), SB_ID(DEFERRED_SYNC)));
   }
}

void
panvk_per_arch(cmd_pstats_flush_pending)(struct panvk_cmd_buffer *cmd)
{
   for (uint32_t i = 0; i < cmd->state.gfx.render.pstats_pending_count; i++) {
      pstats_signal(cmd, cmd->state.gfx.render.pstats_pending[i].avail,
                    cmd->state.gfx.render.pstats_pending[i].view_count);
   }

   cmd->state.gfx.render.pstats_pending_count = 0;
}

static void
panvk_cmd_begin_pstats_query(struct panvk_cmd_buffer *cmd,
                             struct panvk_query_pool *pool, uint32_t query)
{
   struct panvk_pstats_query_state *q = &cmd->state.gfx.pstats_query;
   const uint32_t n = util_bitcount(pool->vk.pipeline_statistics);
   const uint64_t report = panvk_query_report_dev_addr(pool, query);

   *q = (struct panvk_pstats_query_state){
      .report = report,
      .mask = pool->vk.pipeline_statistics,
   };

   /* Counters start at zero. Zero each one from the subqueue that writes it
    * so the stores are ordered with the updates: everything on compute but
    * the FS invocation counter, which the fragment jobs accumulate into. */
   uint64_t fs =
      panvk_pstats_counter_addr(q, PSTAT(FRAGMENT_SHADER_INVOCATIONS), true);
   const uint32_t fs_offset = fs ? fs - report : UINT32_MAX;
   /* With multiview, the other views' queries are all zero. */
   const uint32_t view_mask = cmd->state.gfx.render.view_mask;
   const uint32_t view_count = view_mask ? util_bitcount(view_mask) : 1;

   for (uint32_t sq = 0; sq < 2; sq++) {
      struct cs_builder *b = panvk_get_cs_builder(
         cmd, sq ? PANVK_SUBQUEUE_FRAGMENT : PANVK_SUBQUEUE_COMPUTE);
      struct cs_index addr = cs_scratch_reg64(b, 0);
      struct cs_index zero = cs_scratch_reg64(b, 2);

      if (sq && !fs)
         break;

      cs_move64_to(b, zero, 0);
      for (uint32_t v = 0; v < view_count; v++) {
         cs_move64_to(b, addr, panvk_query_report_dev_addr(pool, query + v));
         for (uint32_t off = 0; off < 2 * n * sizeof(uint64_t);
              off += sizeof(uint64_t)) {
            if ((off == fs_offset) == (sq == 1))
               cs_store64(b, zero, addr, off);
         }
         cs_flush_stores(b);
      }
   }

   if (!fs)
      return;

   /* wrap_prev_oq() signals one syncobj per view when the occlusion counter
    * changes owner, give it harmless ones. */
   const size_t oq_sync_size = 32 * sizeof(struct panvk_query_available_obj);
   struct pan_ptr oq_sync = panvk_cmd_alloc_dev_mem(cmd, desc, oq_sync_size, 64);
   if (!oq_sync.gpu)
      return;

   memset(oq_sync.cpu, 0, oq_sync_size);
   q->oq_sync = oq_sync.gpu;
   pstats_take_oq(cmd);
}

static void
panvk_cmd_end_pstats_query(struct panvk_cmd_buffer *cmd,
                           struct panvk_query_pool *pool, uint32_t query)
{
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);
   struct panvk_pstats_query_state *q = &cmd->state.gfx.pstats_query;

   if (q->oq_sync && cmd->state.gfx.occlusion_query.syncobj == q->oq_sync) {
      cmd->state.gfx.occlusion_query.ptr = 0;
      cmd->state.gfx.occlusion_query.syncobj = 0;
      cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
      gfx_state_set_dirty(cmd, OQ);
   }
   *q = (struct panvk_pstats_query_state){0};

   /* The fragment subqueue signals availability, once the compute subqueue
    * is done with the command stream updates and the shader atomics. This
    * is one wait per query, not per draw. */
   struct panvk_cs_deps deps = {0};
   deps.src[PANVK_SUBQUEUE_COMPUTE].wait_sb_mask =
      dev->csf.sb.all_iters_mask | SB_MASK(LS);
   deps.src[PANVK_SUBQUEUE_COMPUTE].cache_flush = (struct panvk_cache_flush_info){
      .l2 = MALI_CS_FLUSH_MODE_CLEAN,
      .lsc = MALI_CS_FLUSH_MODE_CLEAN,
      .others = MALI_CS_OTHER_FLUSH_MODE_NONE,
   };
   deps.dst[PANVK_SUBQUEUE_FRAGMENT].wait_subqueue_mask =
      BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);
   panvk_per_arch(emit_barrier)(cmd, deps);

   const uint32_t view_mask = cmd->state.gfx.render.view_mask;
   const uint32_t view_count = view_mask ? util_bitcount(view_mask) : 1;
   const uint64_t avail = panvk_query_available_dev_addr(pool, query);

   /* Inside a render pass, the fragment jobs that feed the FS invocation
    * counter are only issued by EndRendering, so defer the signal there. A
    * secondary hands its list to the primary in CmdExecuteCommands.
    * ponytail: a full pending list signals now and can miss that pass's FS
    * invocations.
    */
   if ((cmd->state.gfx.render.tiler || inherits_render_ctx(cmd)) &&
       cmd->state.gfx.render.pstats_pending_count < PANVK_PSTATS_MAX_PENDING) {
      cmd->state.gfx.render.pstats_pending[cmd->state.gfx.render.pstats_pending_count++] =
         (struct panvk_pstats_pending){avail, view_count};
      return;
   }

   pstats_signal(cmd, avail, view_count);
}

static void
panvk_copy_pstats_query_results(struct panvk_cmd_buffer *cmd,
                                struct panvk_query_pool *pool,
                                uint32_t first_query, uint32_t query_count,
                                uint64_t dst_buffer_addr, VkDeviceSize stride,
                                VkQueryResultFlags flags)
{
   /* Order the copy after the availability writes and resets, which happen
    * on the fragment subqueue. */
   struct panvk_cs_deps deps = {0};
   deps.src[PANVK_SUBQUEUE_FRAGMENT].wait_sb_mask =
      SB_MASK(DEFERRED_FLUSH) | SB_MASK(DEFERRED_SYNC) | SB_MASK(LS);
   deps.dst[PANVK_SUBQUEUE_COMPUTE].wait_subqueue_mask =
      BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT);
   panvk_per_arch(emit_barrier)(cmd, deps);

   const bool xfb =
      pool->vk.query_type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;

   if (xfb)
      cs_wait_slot(panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_COMPUTE),
                   SB_ID(DEFERRED_SYNC));

   const struct panlib_copy_pstats_query_result_args push = {
      .pool_addr = panvk_query_report_dev_addr(pool, first_query),
      .available_addr = panvk_query_available_dev_addr(pool, first_query),
      .query_stride = pool->query_stride,
      .query_count = query_count,
      .dst_addr = dst_buffer_addr,
      .dst_stride = stride,
      .flags = flags,
      .stat_count = xfb ? 2 : util_bitcount(pool->vk.pipeline_statistics),
   };
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmd);

   panlib_copy_pstats_query_result_struct(&precomp_ctx, panlib_1d(query_count),
                                          PANLIB_BARRIER_CSF_SYNC, push);

   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);
   struct panvk_cs_deps post = {0};
   post.src[PANVK_SUBQUEUE_COMPUTE].wait_sb_mask =
      dev->csf.sb.all_iters_mask | SB_MASK(LS);
   post.dst[PANVK_SUBQUEUE_FRAGMENT].wait_subqueue_mask =
      BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);
   panvk_per_arch(emit_barrier)(cmd, post);
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

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      panvk_cmd_reset_queries(cmd, pool, firstQuery, queryCount);
      break;
   }
   case VK_QUERY_TYPE_TIMESTAMP: {
      panvk_cmd_reset_timestamp_queries(cmd, pool, firstQuery, queryCount);
      break;
   }
   default:
      UNREACHABLE("Unsupported query type");
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginQueryIndexedEXT)(VkCommandBuffer commandBuffer,
                                        VkQueryPool queryPool, uint32_t query,
                                        VkQueryControlFlags flags,
                                        uint32_t index)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   assert(index == 0 ||
          pool->vk.query_type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT);

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      panvk_cmd_begin_xfb_query(cmd, pool, query, index);
      break;
   }
   case VK_QUERY_TYPE_OCCLUSION: {
      panvk_cmd_begin_occlusion_query(cmd, pool, query, flags);
      break;
   }
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT: {
      panvk_cmd_begin_prims_generated_query(cmd, pool, query, flags);
      break;
   }
   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      panvk_cmd_begin_pstats_query(cmd, pool, query);
      break;
   }
   default:
      UNREACHABLE("Unsupported query type");
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndQueryIndexedEXT)(VkCommandBuffer commandBuffer,
                                      VkQueryPool queryPool, uint32_t query,
                                      uint32_t index)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   assert(index == 0 ||
          pool->vk.query_type == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT);

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      panvk_cmd_end_xfb_query(cmd, pool, query, index);
      break;
   }
   case VK_QUERY_TYPE_OCCLUSION: {
      panvk_cmd_end_occlusion_query(cmd, pool, query);
      break;
   }
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT: {
      panvk_cmd_end_prims_generated_query(cmd, pool, query);
      break;
   }
   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      panvk_cmd_end_pstats_query(cmd, pool, query);
      break;
   }
   default:
      UNREACHABLE("Unsupported query type");
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdWriteTimestamp2)(VkCommandBuffer commandBuffer,
                                   VkPipelineStageFlags2 stage,
                                   VkQueryPool queryPool, uint32_t query)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   panvk_cmd_write_timestamp_query(cmd, stage, pool, query);
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

   uint64_t dst_buffer_addr = panvk_buffer_gpu_ptr(dst_buffer, dstOffset);

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT: {
      panvk_copy_query_results(cmd, pool, firstQuery, queryCount,
                               dst_buffer_addr, stride, flags);
      break;
   }
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      panvk_copy_pstats_query_results(cmd, pool, firstQuery, queryCount,
                                      dst_buffer_addr, stride, flags);
      break;
   }
#if PAN_ARCH >= 10
   case VK_QUERY_TYPE_TIMESTAMP: {
      panvk_copy_timestamp_query_results(cmd, pool, firstQuery, queryCount,
                                         dst_buffer_addr, stride, flags);
      break;
   }
#endif
   default:
      UNREACHABLE("Unsupported query type");
   }
}
