/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2026 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"
#include "panvk_instr.h"

#include "util/bitscan.h"

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdResetEvent2)(VkCommandBuffer commandBuffer, VkEvent _event,
                               VkPipelineStageFlags2 stageMask)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_event, event, _event);

   /* Wrap stageMask with a VkDependencyInfo object so we can re-use
    * add_cs_deps(). */
   const VkMemoryBarrier2 barrier = {
      .srcStageMask = stageMask,
   };
   const VkDependencyInfo info = {
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &barrier,
   };
   struct panvk_cs_deps deps = {0};

   panvk_per_arch(add_cs_deps)(cmdbuf, &info, &deps, false);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);
      uint32_t sb_mask = deps.src[i].wait_sb_mask;
      struct cs_index sync_addr = cs_scratch_reg64(b, 0);
      struct cs_index seqno = cs_scratch_reg32(b, 2);
      struct cs_index cmp_scratch = cs_scratch_reg32(b, 3);

      cs_move64_to(b, sync_addr,
                   panvk_priv_mem_dev_addr(event->syncobjs) +
                      (i * sizeof(struct panvk_cs_sync32)));
      cs_load32_to(b, seqno, sync_addr,
                   offsetof(struct panvk_cs_sync32, seqno));

      cs_match(b, seqno, cmp_scratch) {
         cs_case(b, 0) {
            /* Nothing to do, we just need it defined for the default case. */
         }

         cs_default(b) {
            cs_move32_to(b, seqno, 0);
            cs_sync32_set(b, false, MALI_CS_SYNC_SCOPE_CSG, seqno, sync_addr,
                          cs_defer(sb_mask | SB_MASK(DEFERRED_FLUSH),
                                   SB_ID(DEFERRED_SYNC)));
         }
      }
   }
}

static struct panvk_cache_flush_info
collect_event_cache_flush(const struct panvk_cs_deps *deps)
{
   struct panvk_cache_flush_info flush = {0};

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      flush.l2 |= deps->src[i].cache_flush.l2;
      flush.lsc |= deps->src[i].cache_flush.lsc;
      flush.others |= deps->src[i].cache_flush.others;
   }

   return flush;
}

static bool
panvk_event_transitions_execute_at_wait(const VkDependencyInfo *info)
{
   if (info->dependencyFlags & VK_DEPENDENCY_ASYMMETRIC_EVENT_BIT_KHR) {
      return true;
   }

   VkPipelineStageFlags2 src_stages =
      vk_collect_dependency_info_src_stages(info);

   return !(src_stages & ~VK_PIPELINE_STAGE_2_HOST_BIT);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdSetEvent2)(VkCommandBuffer commandBuffer, VkEvent _event,
                             const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_event, event, _event);
   struct panvk_cs_deps deps = {0};

   panvk_per_arch(add_cs_deps)(cmdbuf, pDependencyInfo, &deps, true);

   /* vkCmdSetEvents() is not allowed to be called mid-render-pass */
   assert(!deps.needs_fb_barrier);

   STACK_ARRAY(uint64_t, crc_addrs, pDependencyInfo->imageMemoryBarrierCount);

   if (!crc_addrs) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   if (!panvk_event_transitions_execute_at_wait(pDependencyInfo)) {
      panvk_per_arch(collect_crc_invalidation_deps)(pDependencyInfo, &deps,
                                                    crc_addrs);

      if (deps.crc.count) {
         uint32_t src_mask = vk_stages_to_subqueue_mask(
            vk_collect_dependency_info_src_stages(pDependencyInfo),
            SYNC_SCOPE_FIRST);

         /* Wait for the complete event source scope, invalidate CRC, then
          * release the source streams so their event signals happen after it.
          */
         struct panvk_cs_deps transition = {
            .crc = deps.crc,
         };

         transition.crc.src_subqueue_mask = src_mask;
         transition.crc.dst_subqueue_mask =
            src_mask ?: BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);

         panvk_per_arch(emit_barrier)(cmdbuf, transition);
      }
   }

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);
      uint32_t sb_mask = deps.src[i].wait_sb_mask;
      struct cs_index sync_addr = cs_scratch_reg64(b, 0);
      struct cs_index seqno = cs_scratch_reg32(b, 2);
      struct cs_index cmp_scratch = cs_scratch_reg32(b, 3);

      cs_move64_to(b, sync_addr,
                   panvk_priv_mem_dev_addr(event->syncobjs) +
                      (i * sizeof(struct panvk_cs_sync32)));
      cs_load32_to(b, seqno, sync_addr,
                   offsetof(struct panvk_cs_sync32, seqno));

      cs_match(b, seqno, cmp_scratch) {
         cs_case(b, 0) {
            struct panvk_cache_flush_info cache_flush = deps.src[i].cache_flush;

            if (!panvk_cache_flush_is_nop(&cache_flush)) {
               /* We rely on r88 being zero since we're in the if (r88 == 0)
                * branch. */
               cs_flush_caches(b, cache_flush.l2, cache_flush.lsc,
                               cache_flush.others, seqno,
                               cs_defer(sb_mask, SB_ID(DEFERRED_FLUSH)));
            }

            cs_move32_to(b, seqno, 1);
            cs_sync32_set(b, false, panvk_cs_signal_scope(cmdbuf), seqno,
                          sync_addr, cs_defer(sb_mask | SB_MASK(DEFERRED_FLUSH),
                                              SB_ID(DEFERRED_SYNC)));
         }
      }
   }
   STACK_ARRAY_FINISH(crc_addrs);
}

static void
cmd_wait_event(struct panvk_cmd_buffer *cmdbuf, struct panvk_event *event,
               const VkDependencyInfo *info)
{
   struct panvk_cs_deps deps = {0};

   panvk_per_arch(add_cs_deps)(cmdbuf, info, &deps, false);

   STACK_ARRAY(uint64_t, crc_addrs, info->imageMemoryBarrierCount);

   if (!crc_addrs) {
      vk_command_buffer_set_error(&cmdbuf->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   if (panvk_event_transitions_execute_at_wait(info)) {
      panvk_per_arch(collect_crc_invalidation_deps)(info, &deps, crc_addrs);
   }

   struct panvk_cs_crc_deps crc = deps.crc;
   struct panvk_cache_flush_info cache_flush = collect_event_cache_flush(&deps);

   uint32_t dst_mask = vk_stages_to_subqueue_mask(
      vk_collect_dependency_info_dst_stages(info), SYNC_SCOPE_SECOND);

   bool has_wait_ops = crc.count || !panvk_cache_flush_is_nop(&cache_flush);

   uint32_t exec_mask = dst_mask;
   enum panvk_subqueue_id exec = panvk_crc_exec_subqueue(exec_mask);

   if (has_wait_ops) {
      uint32_t event_src_mask = 0;

      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
         event_src_mask |= deps.dst[i].wait_subqueue_mask;

      deps.dst[exec].wait_subqueue_mask |= event_src_mask;
   }

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);

      u_foreach_bit(j, deps.dst[i].wait_subqueue_mask) {
         struct cs_index sync_addr = cs_scratch_reg64(b, 0);
         struct cs_index seqno = cs_scratch_reg32(b, 2);

         cs_move64_to(b, sync_addr,
                      panvk_priv_mem_dev_addr(event->syncobjs) +
                         (j * sizeof(struct panvk_cs_sync32)));

         cs_move32_to(b, seqno, 0);
         panvk_instr_sync32_wait(cmdbuf, i, false, MALI_CS_CONDITION_GREATER,
                                 seqno, sync_addr);
      }
   }

   if (has_wait_ops) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, exec);

      if (!panvk_cache_flush_is_nop(&cache_flush)) {
         struct cs_index flush_id = cs_scratch_reg32(b, 0);

         cs_move32_to(b, flush_id, 0);
         cs_flush_caches(b, cache_flush.l2, cache_flush.lsc, cache_flush.others,
                         flush_id, cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
         cs_wait_slot(b, SB_ID(IMM_FLUSH));
      }

      for (uint32_t i = 0; i < crc.count; i++)
         panvk_per_arch(cmd_invalidate_crc)(b, crc.addrs[i]);

      uint32_t other_dst = dst_mask & ~BITFIELD_BIT(exec);

      if (other_dst) {
         struct panvk_cs_deps post = {0};

         post.src[exec].wait_sb_mask = SB_MASK(LS);

         u_foreach_bit(dst, other_dst)
            post.dst[dst].wait_subqueue_mask = BITFIELD_BIT(exec);

         panvk_per_arch(emit_barrier)(cmdbuf, post);
      }
   }

   STACK_ARRAY_FINISH(crc_addrs);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdWaitEvents2)(VkCommandBuffer commandBuffer,
                               uint32_t eventCount, const VkEvent *pEvents,
                               const VkDependencyInfo *pDependencyInfos)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   for (uint32_t i = 0; i < eventCount; i++) {
      VK_FROM_HANDLE(panvk_event, event, pEvents[i]);
      const VkDependencyInfo *info = &pDependencyInfos[i];

      cmd_wait_event(cmdbuf, event, info);
   }
}
