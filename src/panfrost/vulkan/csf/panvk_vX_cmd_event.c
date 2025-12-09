/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"
#include "panvk_instr.h"
#include "panvk_meta.h"

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

   panvk_per_arch(add_cs_deps)(cmdbuf, PANVK_BARRIER_STAGE_FIRST, &info, &deps, false);

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

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdSetEvent2)(VkCommandBuffer commandBuffer, VkEvent _event,
                             const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_event, event, _event);
   struct panvk_cs_deps deps = {0};

   panvk_per_arch(add_cs_deps)(cmdbuf, PANVK_BARRIER_STAGE_FIRST, pDependencyInfo, &deps, true);

   if (deps.needs_draw_flush)
      panvk_per_arch(cmd_flush_draws)(cmdbuf);

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
            cs_sync32_set(b, false, MALI_CS_SYNC_SCOPE_CSG, seqno, sync_addr,
                          cs_defer(sb_mask | SB_MASK(DEFERRED_FLUSH),
                                   SB_ID(DEFERRED_SYNC)));
         }
      }
   }
}

static void
cmd_wait_event(struct panvk_cmd_buffer *cmdbuf, struct panvk_event *event,
               const VkDependencyInfo *info, struct panvk_cs_deps *trans_deps,
               bool *needs_trans_barrier)
{
   struct panvk_cs_deps deps = {0};

   panvk_per_arch(add_cs_deps)(cmdbuf, PANVK_BARRIER_STAGE_FIRST, info, &deps, false);

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

   if (deps.needs_layout_transitions) {
      for (uint32_t i = 0; i < info->imageMemoryBarrierCount; i++) {
         const VkImageMemoryBarrier2 *barrier = &info->pImageMemoryBarriers[i];

         panvk_per_arch(cmd_transition_image_layout)(
            panvk_cmd_buffer_to_handle(cmdbuf), barrier);
      }

      panvk_per_arch(add_cs_deps)(
         cmdbuf, PANVK_BARRIER_STAGE_AFTER_LAYOUT_TRANSITION,
         info, trans_deps, false);
      *needs_trans_barrier = true;
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdWaitEvents2)(VkCommandBuffer commandBuffer,
                               uint32_t eventCount, const VkEvent *pEvents,
                               const VkDependencyInfo *pDependencyInfos)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   struct panvk_cs_deps trans_deps = {0};
   bool needs_trans_barrier = false;

   for (uint32_t i = 0; i < eventCount; i++) {
      VK_FROM_HANDLE(panvk_event, event, pEvents[i]);
      const VkDependencyInfo *info = &pDependencyInfos[i];

      cmd_wait_event(cmdbuf, event, info, &trans_deps, &needs_trans_barrier);
   }

   if (needs_trans_barrier) {
      assert(!trans_deps.needs_draw_flush);

      panvk_per_arch(emit_barrier)(cmdbuf, trans_deps);
   }
}
