/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "drm-uapi/panthor_drm.h"

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_pool.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_cmd_ts.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_instr.h"
#include "panvk_meta.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"
#include "panvk_tracepoints.h"
#include "panvk_utrace.h"

#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_props.h"
#include "pan_samples.h"

#include "util/bitscan.h"
#include "vk_descriptor_update_template.h"
#include "vk_format.h"
#include "vk_synchronization.h"

static void
emit_tls(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   unsigned core_id_range;
   pan_query_core_count(&phys_dev->kmod.props, &core_id_range);

   if (cmdbuf->state.tls.info.tls.size) {
      unsigned thread_tls_alloc =
         pan_query_thread_tls_alloc(&phys_dev->kmod.props);
      unsigned size = pan_get_total_stack_size(cmdbuf->state.tls.info.tls.size,
                                               thread_tls_alloc, core_id_range);

      cmdbuf->state.tls.info.tls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, size, 4096).gpu;
   }

   assert(!cmdbuf->state.tls.info.wls.size);

   if (cmdbuf->state.tls.desc.cpu) {
      GENX(pan_emit_tls)(&cmdbuf->state.tls.info, cmdbuf->state.tls.desc.cpu);
   }
}

/**
 * Write all sync point updates to seqno registers and reset the relative sync
 * points to 0.
 */
static void
flush_sync_points(struct panvk_cmd_buffer *cmdbuf)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->state.cs); i++) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);

      if (!cs_is_valid(b)) {
         vk_command_buffer_set_error(&cmdbuf->vk,
                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
         return;
      }

      cs_update_progress_seqno(b) {
         for (uint32_t j = 0; j < PANVK_SUBQUEUE_COUNT; j++) {
            uint32_t rel_sync_point = cmdbuf->state.cs[j].relative_sync_point;

            if (!rel_sync_point)
               continue;

            cs_add64(b, cs_progress_seqno_reg(b, j), cs_progress_seqno_reg(b, j),
                     rel_sync_point);
         }
      }
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->state.cs); i++)
      cmdbuf->state.cs[i].relative_sync_point = 0;
}

static void
finish_cs(struct panvk_cmd_buffer *cmdbuf, uint32_t subqueue)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, subqueue);

   cs_wait_slots(b, dev->csf.sb.all_mask);

   /* save CS error if non-zero */
   if (cmdbuf->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      struct cs_index sync_addr = cs_scratch_reg64(b, 0);
      struct cs_index error = cs_scratch_reg32(b, 2);

      cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context, syncobjs));
      cs_load32_to(b, error, sync_addr,
                   sizeof(struct panvk_cs_sync64) * subqueue +
                      offsetof(struct panvk_cs_sync64, error));
      cs_flush_loads(b);

      cs_if(b, MALI_CS_CONDITION_NEQUAL, error) {
         cs_store32(b, error, cs_subqueue_ctx_reg(b),
                    offsetof(struct panvk_cs_subqueue_context, last_error));
         cs_flush_stores(b);
      }
   }

   /* We need a clean because descriptor/CS memory can be returned to the
    * command pool where they get recycled. If we don't clean dirty cache lines,
    * those cache lines might get evicted asynchronously and their content
    * pushed back to main memory after the CPU has written new stuff there. */
   struct cs_index flush_id = cs_scratch_reg32(b, 0);

   panvk_per_arch(panvk_instr_begin_work)(subqueue, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_FLUSH_CACHE);
   cs_move32_to(b, flush_id, 0);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN,
                   MALI_CS_OTHER_FLUSH_MODE_NONE, flush_id,
                   cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
   cs_wait_slot(b, SB_ID(IMM_FLUSH));
   struct panvk_instr_end_args instr_info_flush = {
      .flush_cache = {
         .l2 = MALI_CS_FLUSH_MODE_CLEAN,
         .lsc = MALI_CS_FLUSH_MODE_CLEAN,
         .other = MALI_CS_OTHER_FLUSH_MODE_NONE,
      }};
   panvk_per_arch(panvk_instr_end_work)(
      subqueue, cmdbuf, PANVK_INSTR_WORK_TYPE_FLUSH_CACHE, &instr_info_flush);

   /* If this is a secondary command buffer, we don't poison the reg file to
    * preserve the render pass context. We also don't poison the reg file if the
    * last render pass was suspended. In practice we could preserve only the
    * registers that matter, but this is a debug feature so let's keep things
    * simple with this all-or-nothing approach. */
   if (PANVK_DEBUG(CS) &&
       cmdbuf->vk.level != VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
       !cmdbuf->state.gfx.render.suspended) {
      cs_update_cmdbuf_regs(b) {
         /* Poison all cmdbuf registers to make sure we don't inherit state from
          * a previously executed cmdbuf. */
         for (uint32_t i = 0; i <= PANVK_CS_REG_SCRATCH_END; i++)
            cs_move32_to(b, cs_reg32(b, i), 0xdead | i << 24);
      }
   }

   struct panvk_instr_end_args instr_info_cmdbuf = {.cmdbuf = {
                                                       .flags = cmdbuf->flags,
                                                    }};
   panvk_per_arch(panvk_instr_end_work)(
      subqueue, cmdbuf, PANVK_INSTR_WORK_TYPE_CMDBUF, &instr_info_cmdbuf);

   cs_finish(&cmdbuf->state.cs[subqueue].builder);
}

static void
finish_queries(struct panvk_cmd_buffer *cmdbuf)
{
   enum panvk_subqueue_id signal_queue = PANVK_QUERY_TS_INFO_SUBQUEUE;

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, signal_queue);
   struct cs_index next = cs_scratch_reg64(b, 6);
   struct cs_index syncobj = cs_scratch_reg64(b, 2);
   struct cs_index signal_val = cs_scratch_reg32(b, 4);

   cs_load64_to(
      b, next, cs_subqueue_ctx_reg(b),
      offsetof(struct panvk_cs_subqueue_context, render.ts_done_chain.head));

   /* If there are queries to signal, wait for other subqueues before
    * signalling the syncobjs. */
   struct panvk_cs_deps deps = {0};
   deps.dst[signal_queue].wait_subqueue_mask =
      BITFIELD_MASK(PANVK_SUBQUEUE_COUNT) & ~BITFIELD_BIT(signal_queue);
   deps.dst[signal_queue].conditional = true;
   deps.dst[signal_queue].cond_value = next;
   deps.dst[signal_queue].cond = MALI_CS_CONDITION_NEQUAL;
   /* Wait for DEFERRED_SYNC in addition to LS so that we don't overtake the
    * deferred SYNC_ADDs added after frag jobs. */
   u_foreach_bit(i, deps.dst[signal_queue].wait_subqueue_mask)
      deps.src[i].wait_sb_mask = SB_MASK(LS) | SB_MASK(DEFERRED_SYNC);
   panvk_per_arch(emit_barrier)(cmdbuf, deps);

   cs_single_link_list_for_each_from(b, next, struct panvk_cs_timestamp_query,
                                     node) {
      cs_load64_to(b, syncobj, next,
                   offsetof(struct panvk_cs_timestamp_query, avail));

      cs_move32_to(b, signal_val, 1);
      cs_sync32_set(b, true, MALI_CS_SYNC_SCOPE_CSG, signal_val, syncobj,
                    cs_defer(SB_IMM_MASK, SB_ID(DEFERRED_SYNC)));
   }

   cs_move64_to(b, next, 0);
   cs_store64(
      b, next, cs_subqueue_ctx_reg(b),
      offsetof(struct panvk_cs_subqueue_context, render.ts_done_chain.head));
   cs_flush_stores(b);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(EndCommandBuffer)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   /* Finishing queries requires a barrier. We don't want to do that more
    * often than necessary. At the end of a primary is usually enough.
    * Additionally, simultaneous use secondaries also need to flush if they
    * contain timestamp query writes to avoid adding the same node more than
    * once into panvk_cs_subqueue_context::render.ts_chain. */
   const bool sim_use_sec_with_ts =
      cmdbuf->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
      (cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) &&
      cmdbuf->state.contains_timestamp_queries;
   if (cmdbuf->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ||
       unlikely(sim_use_sec_with_ts))
      finish_queries(cmdbuf);

   emit_tls(cmdbuf);
   flush_sync_points(cmdbuf);

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->state.cs); i++) {
      struct cs_builder *b = &cmdbuf->state.cs[i].builder;

      if (!cs_is_valid(b)) {
         vk_command_buffer_set_error(&cmdbuf->vk,
                                     VK_ERROR_OUT_OF_DEVICE_MEMORY);
      } else {
         finish_cs(cmdbuf, i);
      }
   }

   return vk_command_buffer_end(&cmdbuf->vk);
}

static void
add_execution_dependency(uint32_t wait_masks[static PANVK_SUBQUEUE_COUNT],
                         VkPipelineStageFlags2 src_stages,
                         VkPipelineStageFlags2 dst_stages)
{
   /* convert stages to subqueues */
   uint32_t src_subqueues = 0;
   uint32_t dst_subqueues = 0;
   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      const VkPipelineStageFlags2 subqueue_stages = panvk_get_subqueue_stages(i);
      if (src_stages & subqueue_stages)
         src_subqueues |= BITFIELD_BIT(i);
      if (dst_stages & subqueue_stages)
         dst_subqueues |= BITFIELD_BIT(i);
   }

   const bool dst_host = dst_stages & VK_PIPELINE_STAGE_2_HOST_BIT;

   /* nothing to wait */
   if (!src_subqueues || (!dst_subqueues && !dst_host))
      return;

   u_foreach_bit(i, dst_subqueues) {
      /* each dst subqueue should wait for all src subqueues */
      uint32_t wait_mask = src_subqueues;

      switch (i) {
      case PANVK_SUBQUEUE_VERTEX_TILER:
         /* Indirect draw buffers are read from the command stream, and
          * load/store operations are synchronized with the LS scoreboard
          * immediately after the read, so no need to wait in that case.
          */
         if ((src_stages & panvk_get_subqueue_stages(i)) ==
             VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT)
            wait_mask &= ~BITFIELD_BIT(i);
         break;
      case PANVK_SUBQUEUE_FRAGMENT:
         /* The fragment subqueue always waits for the tiler subqueue already.
          * Explicit waits can be skipped.
          */
         wait_mask &= ~BITFIELD_BIT(PANVK_SUBQUEUE_VERTEX_TILER);
         break;
      default:
         break;
      }

      wait_masks[i] |= wait_mask;
   }

   /* The host does not wait for src subqueues.  All src subqueues should
    * self-wait instead.
    *
    * Also, our callers currently expect src subqueues to self-wait when there
    * are dst subqueues.  Until that changes, make all src subqueues self-wait.
    */
   if (dst_host || dst_subqueues) {
      u_foreach_bit(i, src_subqueues)
         wait_masks[i] |= BITFIELD_BIT(i);
   }
}

static void
add_memory_dependency(struct panvk_cache_flush_info *cache_flush,
                      VkAccessFlags2 src_access, VkAccessFlags2 dst_access)
{
   /* Note on the cache organization:
    *
    * - L2 cache is unified, so all changes to this cache are automatically
    *   visible to all GPU sub-components (shader cores, tiler, ...). This
    *   means we only need to flush when the host (AKA CPU) is involved.
    * - LS caches (which are basically just read-write L1 caches) are coherent
    *   with each other and with the L2 cache, so again, we only need to flush
    *   when the host is involved.
    * - Other read-only L1 caches (like the ones in front of the texture unit)
    *   are not coherent with the LS or L2 caches, and thus need to be
    *   invalidated any time a write happens.
    *
    * Translating to the Vulkan memory model:
    *
    * - The device domain is the L2 cache.
    * - An availability operation from device writes to the device domain is
    *   nop.
    * - A visibility operation from the device domain to device accesses that
    *   are coherent with L2/LS is nop.
    * - A visibility operation from the device domain to device accesses that
    *   are incoherent with L2/LS invalidates the other RO L1 caches.
    * - A host-to-device domain operation invalidates all caches.
    * - A device-to-host domain operation flushes L2/LS.
    */
   const VkAccessFlags2 ro_l1_access =
      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
      VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
      VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;

   /* visibility op */
   if (dst_access & ro_l1_access)
      cache_flush->others |= MALI_CS_OTHER_FLUSH_MODE_INVALIDATE;

   /* host-to-device domain op */
   if (src_access & VK_ACCESS_2_HOST_WRITE_BIT) {
      cache_flush->l2 |= MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE;
      cache_flush->lsc |= MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE;
      cache_flush->others |= MALI_CS_OTHER_FLUSH_MODE_INVALIDATE;
   }

   /* device-to-host domain op */
   if (dst_access & (VK_ACCESS_2_HOST_READ_BIT | VK_ACCESS_2_HOST_WRITE_BIT)) {
      cache_flush->l2 |= MALI_CS_FLUSH_MODE_CLEAN;
      cache_flush->lsc |= MALI_CS_FLUSH_MODE_CLEAN;
   }
}

static bool
should_split_render_pass(const uint32_t wait_masks[static PANVK_SUBQUEUE_COUNT],
                         VkAccessFlags2 src_access, VkAccessFlags2 dst_access)
{
   /* From the Vulkan 1.3.301 spec:
    *
    *    VUID-vkCmdPipelineBarrier-None-07892
    *
    *    "If vkCmdPipelineBarrier is called within a render pass instance, the
    *    source and destination stage masks of any memory barriers must only
    *    include graphics pipeline stages"
    *
    * We only consider the tiler and the fragment subqueues here.
    */

   /* split if the tiler subqueue waits for the fragment subqueue */
   if (wait_masks[PANVK_SUBQUEUE_VERTEX_TILER] &
       BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT))
      return true;

   /* split if the fragment subqueue self-waits with a feedback loop, because
    * we lower subpassLoad to texelFetch
    */
   if ((wait_masks[PANVK_SUBQUEUE_FRAGMENT] &
        BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT)) &&
       (src_access & (VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) &&
       (dst_access & VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT))
      return true;

   return false;
}

static void
collect_cache_flush_info(enum panvk_subqueue_id subqueue,
                         struct panvk_cache_flush_info *cache_flush,
                         VkAccessFlags2 src_access, VkAccessFlags2 dst_access)
{
   /* limit access to the subqueue and host */
   const VkPipelineStageFlags2 subqueue_stages =
      panvk_get_subqueue_stages(subqueue) | VK_PIPELINE_STAGE_2_HOST_BIT;
   src_access = vk_filter_src_access_flags2(subqueue_stages, src_access);
   dst_access = vk_filter_dst_access_flags2(subqueue_stages, dst_access);

   add_memory_dependency(cache_flush, src_access, dst_access);
}

static void
collect_cs_deps(struct panvk_cmd_buffer *cmdbuf,
                struct panvk_sync_scope src, struct panvk_sync_scope dst,
                struct panvk_cs_deps *deps)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   uint32_t wait_masks[PANVK_SUBQUEUE_COUNT] = {0};
   add_execution_dependency(wait_masks, src.stages, dst.stages);

   /* within a render pass */
   if (cmdbuf->state.gfx.render.tiler || inherits_render_ctx(cmdbuf)) {
      if (should_split_render_pass(wait_masks, src.access, dst.access)) {
         deps->needs_draw_flush = true;
      } else {
         /* skip the tiler subqueue self-wait because we use the same
          * scoreboard slot for the idvs jobs
          */
         wait_masks[PANVK_SUBQUEUE_VERTEX_TILER] &=
            ~BITFIELD_BIT(PANVK_SUBQUEUE_VERTEX_TILER);

         /* skip the fragment subqueue self-wait because we emit the fragment
          * job at the end of the render pass and there is nothing to wait yet
          */
         wait_masks[PANVK_SUBQUEUE_FRAGMENT] &=
            ~BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT);
      }
   }

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (wait_masks[i] & BITFIELD_BIT(i)) {
         /* We need to self-wait for all previously submitted jobs, and given
          * the iterator scoreboard is a moving target, we just wait for the
          * whole dynamic scoreboard range.
          */
         deps->src[i].wait_sb_mask |= dev->csf.sb.all_iters_mask;
      }

      collect_cache_flush_info(i, &deps->src[i].cache_flush, src.access,
                               dst.access);

      deps->dst[i].wait_subqueue_mask |= wait_masks[i];
   }
}

static void
normalize_dependency(struct panvk_sync_scope *src,
                     struct panvk_sync_scope *dst,
                     struct panvk_sync_scope transition,
                     uint32_t src_qfi, uint32_t dst_qfi,
                     enum panvk_barrier_stage barrier_stage)
{
   switch (barrier_stage) {
   case PANVK_BARRIER_STAGE_FIRST:
      if (transition.stages) {
         /* We need to do layout transition, so we want to sync src with layout
          * transition, and then later layout transition with dst.
          */
         *dst = transition;
      }
      break;
   case PANVK_BARRIER_STAGE_AFTER_LAYOUT_TRANSITION:
      /* If transition.stages is empty, there was no layout transition and so we
       * won't be waiting for anything.
       */
      *src = transition;
      break;
   }

   /* Perform queue family ownership transfer if src and dst are unequal. */
   if (src_qfi != dst_qfi) {
      /* Only normalize if we're actually syncing acquire, and not layout
       * transition, with dst.
       */
      if (barrier_stage == PANVK_BARRIER_STAGE_FIRST) {
         /* queue family acquire operation */
         switch (src_qfi) {
         case VK_QUEUE_FAMILY_EXTERNAL:
            /* no execution dependency and no availability operation */
            *src = (struct panvk_sync_scope){VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
            break;
         case VK_QUEUE_FAMILY_FOREIGN_EXT:
            /* treat the foreign queue as the host */
            *src = (struct panvk_sync_scope){VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT};
            break;
         default:
            break;
         }
      }

      /* Only normalize if we're actually syncing the latest of either src or
       * layout transition, with release.
       */
      if ((barrier_stage == PANVK_BARRIER_STAGE_FIRST && !transition.stages) ||
          (barrier_stage == PANVK_BARRIER_STAGE_AFTER_LAYOUT_TRANSITION && transition.stages)) {
         /* queue family release operation */
         switch (dst_qfi) {
         case VK_QUEUE_FAMILY_EXTERNAL:
            /* no execution dependency and no visibility operation */
            *dst = (struct panvk_sync_scope){VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
            break;
         case VK_QUEUE_FAMILY_FOREIGN_EXT:
            /* treat the foreign queue as the host */
            *dst = (struct panvk_sync_scope){VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT};
            break;
         default:
            break;
         }
      }
   }

   src->stages = vk_expand_src_stage_flags2(src->stages);
   src->access = vk_filter_src_access_flags2(src->stages, src->access);
   dst->stages = vk_expand_dst_stage_flags2(dst->stages);
   dst->access = vk_filter_dst_access_flags2(dst->stages, dst->access);
}

void
panvk_per_arch(add_cs_deps)(struct panvk_cmd_buffer *cmdbuf,
                            enum panvk_barrier_stage barrier_stage,
                            const VkDependencyInfo *in,
                            struct panvk_cs_deps *out,
                            bool is_set_event)
{
   bool is_asymmetric_event =
      is_set_event &&
      in->dependencyFlags & VK_DEPENDENCY_ASYMMETRIC_EVENT_BIT_KHR;

   /* As per Vulkan spec, this should look like a vkCmdSetEvent */
   assert(!is_asymmetric_event ||
          (in->memoryBarrierCount == 1 && in->bufferMemoryBarrierCount == 0 &&
           in->imageMemoryBarrierCount == 0));

   for (uint32_t i = 0; i < in->memoryBarrierCount; i++) {
      const VkMemoryBarrier2 *barrier = &in->pMemoryBarriers[i];
      struct panvk_sync_scope src = {barrier->srcStageMask, barrier->srcAccessMask};
      struct panvk_sync_scope dst = {barrier->dstStageMask, barrier->dstAccessMask};

      /* In case of asymmetric event, we need to use the src stages mask just
       * like vkCmdSetEvent */
      if (is_asymmetric_event)
         dst.stages = src.stages;

      normalize_dependency(&src, &dst, (struct panvk_sync_scope){0},
                           VK_QUEUE_FAMILY_IGNORED,
                           VK_QUEUE_FAMILY_IGNORED,
                           barrier_stage);

      collect_cs_deps(cmdbuf, src, dst, out);
   }

   for (uint32_t i = 0; i < in->bufferMemoryBarrierCount; i++) {
      const VkBufferMemoryBarrier2 *barrier = &in->pBufferMemoryBarriers[i];
      struct panvk_sync_scope src = {barrier->srcStageMask, barrier->srcAccessMask};
      struct panvk_sync_scope dst = {barrier->dstStageMask, barrier->dstAccessMask};
      normalize_dependency(&src, &dst, (struct panvk_sync_scope){0},
                           barrier->srcQueueFamilyIndex,
                           barrier->dstQueueFamilyIndex,
                           barrier_stage);

      collect_cs_deps(cmdbuf, src, dst, out);
   }

   for (uint32_t i = 0; i < in->imageMemoryBarrierCount; i++) {
      const VkImageMemoryBarrier2 *barrier = &in->pImageMemoryBarriers[i];
      struct panvk_sync_scope src = {barrier->srcStageMask, barrier->srcAccessMask};
      struct panvk_sync_scope dst = {barrier->dstStageMask, barrier->dstAccessMask};
      struct panvk_sync_scope transition;
      panvk_per_arch(transition_image_layout_sync_scope)(barrier,
         &transition.stages, &transition.access);
      normalize_dependency(&src, &dst, transition,
                           barrier->srcQueueFamilyIndex,
                           barrier->dstQueueFamilyIndex,
                           barrier_stage);

      collect_cs_deps(cmdbuf, src, dst, out);

      if (barrier_stage == PANVK_BARRIER_STAGE_FIRST && transition.stages)
         out->needs_layout_transitions = true;
   }
}

static void
emit_barrier_insert_waits(struct cs_builder *b, struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_cs_deps *deps, enum panvk_subqueue_id i,
                          struct cs_index tmp_regs)
{
   assert(tmp_regs.size == 4);
   u_foreach_bit(j, deps->dst[i].wait_subqueue_mask) {
      struct panvk_cs_state *cs_state = &cmdbuf->state.cs[j];
      struct cs_index sync_addr = cs_reg64(b, tmp_regs.reg);
      struct cs_index wait_val = cs_reg64(b, tmp_regs.reg + 2);

      cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context, syncobjs));
      cs_add64(b, sync_addr, sync_addr, sizeof(struct panvk_cs_sync64) * j);

      cs_add64(b, wait_val, cs_progress_seqno_reg(b, j),
               cs_state->relative_sync_point);

      panvk_instr_sync64_wait(cmdbuf, i, false, MALI_CS_CONDITION_GREATER,
                              wait_val, sync_addr);
   }
}

void
panvk_per_arch(emit_barrier)(struct panvk_cmd_buffer *cmdbuf,
                             struct panvk_cs_deps deps)
{
   uint32_t wait_subqueue_mask = 0;
   uint32_t utrace_subqueue_mask = 0;
   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      /* no need to perform both types of waits on the same subqueue */
      if (deps.src[i].wait_sb_mask)
         deps.dst[i].wait_subqueue_mask &= ~BITFIELD_BIT(i);
      assert(!(deps.dst[i].wait_subqueue_mask & BITFIELD_BIT(i)));

      wait_subqueue_mask |= deps.dst[i].wait_subqueue_mask;

      if (deps.src[i].wait_sb_mask || deps.dst[i].wait_subqueue_mask ||
          !panvk_cache_flush_is_nop(&deps.src[i].cache_flush))
         utrace_subqueue_mask |= BITFIELD_BIT(i);
   }

   u_foreach_bit(i, utrace_subqueue_mask)
      panvk_per_arch(panvk_instr_begin_work)(i, cmdbuf,
                                             PANVK_INSTR_WORK_TYPE_BARRIER);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);
      struct panvk_cs_state *cs_state = &cmdbuf->state.cs[i];

      if (deps.src[i].wait_sb_mask)
         cs_wait_slots(b, deps.src[i].wait_sb_mask);

      struct panvk_cache_flush_info cache_flush = deps.src[i].cache_flush;
      if (!panvk_cache_flush_is_nop(&cache_flush)) {
         struct cs_index flush_id = cs_scratch_reg32(b, 0);

         panvk_per_arch(panvk_instr_begin_work)(
            i, cmdbuf, PANVK_INSTR_WORK_TYPE_FLUSH_CACHE);
         cs_move32_to(b, flush_id, 0);
         cs_flush_caches(b, cache_flush.l2, cache_flush.lsc, cache_flush.others,
                         flush_id, cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
         cs_wait_slot(b, SB_ID(IMM_FLUSH));
         struct panvk_instr_end_args instr_info_flush = {
            .flush_cache = {
               .l2 = cache_flush.l2,
               .lsc = cache_flush.lsc,
               .other = cache_flush.others,
            }};
         panvk_per_arch(panvk_instr_end_work)(
            i, cmdbuf, PANVK_INSTR_WORK_TYPE_FLUSH_CACHE, &instr_info_flush);
      }

      /* If no one waits on us, there's no point signaling the sync object. */
      if (wait_subqueue_mask & BITFIELD_BIT(i)) {
         struct cs_index sync_addr = cs_scratch_reg64(b, 0);
         struct cs_index add_val = cs_scratch_reg64(b, 2);

         assert(deps.src[i].wait_sb_mask);

         cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                      offsetof(struct panvk_cs_subqueue_context, syncobjs));
         cs_add64(b, sync_addr, sync_addr, sizeof(struct panvk_cs_sync64) * i);
         cs_move64_to(b, add_val, 1);
         panvk_instr_sync64_add(cmdbuf, i, true, MALI_CS_SYNC_SCOPE_CSG,
                                add_val, sync_addr, cs_now());
         ++cs_state->relative_sync_point;
      }
   }

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, i);
      struct cs_index tmp_regs = cs_scratch_reg_tuple(b, 0, 4);

      if (deps.dst[i].conditional) {
         assert(deps.dst[i].cond_value.reg >= tmp_regs.reg + tmp_regs.size ||
                deps.dst[i].cond_value.reg + deps.dst[i].cond_value.size <=
                   tmp_regs.reg);
         cs_if(b, deps.dst[i].cond, deps.dst[i].cond_value)
            emit_barrier_insert_waits(b, cmdbuf, &deps, i, tmp_regs);
      } else {
         emit_barrier_insert_waits(b, cmdbuf, &deps, i, tmp_regs);
      }
   }

   u_foreach_bit(i, utrace_subqueue_mask) {
      struct panvk_instr_end_args info = {
         .barrier = {
            .wait_sb_mask = deps.src[i].wait_sb_mask,
            .wait_subqueue_mask = deps.dst[i].wait_subqueue_mask,
         }};
      panvk_per_arch(panvk_instr_end_work)(
         i, cmdbuf, PANVK_INSTR_WORK_TYPE_BARRIER, &info);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPipelineBarrier2)(VkCommandBuffer commandBuffer,
                                    const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   /* Intra render pass barriers can be skipped iff we're inside a render
    * pass. */
   if ((cmdbuf->state.gfx.render.tiler || inherits_render_ctx(cmdbuf)) &&
       (pDependencyInfo->dependencyFlags & VK_DEPENDENCY_BY_REGION_BIT))
      return;

   struct panvk_cs_deps deps = {0};

   panvk_per_arch(add_cs_deps)(cmdbuf, PANVK_BARRIER_STAGE_FIRST, pDependencyInfo, &deps, false);

   if (deps.needs_draw_flush)
      panvk_per_arch(cmd_flush_draws)(cmdbuf);

   panvk_per_arch(emit_barrier)(cmdbuf, deps);

   if (deps.needs_layout_transitions) {
      for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++) {
         const VkImageMemoryBarrier2 *barrier = &pDependencyInfo->pImageMemoryBarriers[i];

         panvk_per_arch(cmd_transition_image_layout)(commandBuffer, barrier);
      }

      struct panvk_cs_deps trans_deps = {0};

      panvk_per_arch(add_cs_deps)(
         cmdbuf, PANVK_BARRIER_STAGE_AFTER_LAYOUT_TRANSITION,
         pDependencyInfo, &trans_deps, false);

      assert(!trans_deps.needs_draw_flush);

      panvk_per_arch(emit_barrier)(cmdbuf, trans_deps);
   }
}

#if PAN_ARCH >= 11
void
panvk_per_arch(cs_next_iter_sb)(struct panvk_cmd_buffer *cmdbuf,
                                enum panvk_subqueue_id subqueue,
                                struct cs_index scratch_regs)
{
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, subqueue);
   struct cs_index iter_sb = cs_extract32(b, scratch_regs, 0);
   struct cs_index sb_wait_mask = cs_extract32(b, scratch_regs, 1);

   /* Wait for scoreboard to be available and select the next scoreboard entry */
   cs_next_sb_entry(b, iter_sb, MALI_CS_SCOREBOARD_TYPE_ENDPOINT,
                    MALI_CS_NEXT_SB_ENTRY_FORMAT_INDEX);

   /* Setup indirect scoreboard wait mask now for indirect defer */
   cs_move32_to(b, sb_wait_mask, 0);
   cs_bit_set32(b, sb_wait_mask, sb_wait_mask, iter_sb);
   cs_set_state(b, MALI_CS_SET_STATE_TYPE_SB_MASK_WAIT, sb_wait_mask);
}
#else
void
panvk_per_arch(cs_next_iter_sb)(struct panvk_cmd_buffer *cmdbuf,
                                enum panvk_subqueue_id subqueue,
                                struct cs_index scratch_regs)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, subqueue);
   struct cs_index iter_sb = cs_extract32(b, scratch_regs, 0);
   struct cs_index cmp_scratch = cs_extract32(b, scratch_regs, 1);

   cs_load32_to(b, iter_sb, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, iter_sb));

   /* Select next scoreboard entry and wrap around if we get past the limit */
   cs_add32(b, iter_sb, iter_sb, 1);
   cs_add32(b, cmp_scratch, iter_sb, -SB_ITER(dev->csf.sb.iter_count));
   cs_if(b, MALI_CS_CONDITION_GEQUAL, cmp_scratch) {
      cs_move32_to(b, iter_sb, SB_ITER(0));
   }

   cs_match_iter_sb(b, x, iter_sb, cmp_scratch) {
      cs_wait_slot(b, SB_ITER(x));
      cs_select_sb_entries_for_async_ops(b, SB_ITER(x));
   }

   cs_store32(b, iter_sb, cs_subqueue_ctx_reg(b),
              offsetof(struct panvk_cs_subqueue_context, iter_sb));
   cs_flush_stores(b);
}
#endif

static struct cs_buffer
alloc_cs_buffer(void *cookie)
{
   struct panvk_cmd_buffer *cmdbuf = cookie;
   const unsigned capacity = 64 * 1024 / sizeof(uint64_t);

   struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(cmdbuf, cs, capacity * 8, 64);

   return (struct cs_buffer){
      .cpu = ptr.cpu,
      .gpu = ptr.gpu,
      .capacity = capacity,
   };
}

static enum cs_reg_perm
cs_reg_perm(struct cs_builder *b, unsigned reg)
{
   struct panvk_cs_state *cs_state =
      container_of(b, struct panvk_cs_state, builder);
   struct panvk_cs_reg_upd_context *upd_ctx;

   for (upd_ctx = cs_state->reg_access.upd_ctx_stack; upd_ctx;
        upd_ctx = upd_ctx->next) {
      if (upd_ctx->reg_perm(b, reg) == CS_REG_RW)
         return CS_REG_RW;
   }

   return cs_state->reg_access.base_perm(b, reg);
}

static void
init_cs_builders(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const reg_perm_cb_t base_reg_perms[PANVK_SUBQUEUE_COUNT] = {
      [PANVK_SUBQUEUE_VERTEX_TILER] = panvk_cs_vt_reg_perm,
      [PANVK_SUBQUEUE_FRAGMENT] = panvk_cs_frag_reg_perm,
      [PANVK_SUBQUEUE_COMPUTE] = panvk_cs_compute_reg_perm,
   };

   const struct drm_panthor_csif_info *csif_info =
      panthor_kmod_get_csif_props(dev->kmod.dev);

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->state.cs); i++) {
      struct cs_builder *b = &cmdbuf->state.cs[i].builder;
      /* Lazy allocation of the root CS. */
      struct cs_buffer root_cs = {0};

      struct cs_builder_conf conf = {
         .nr_registers = csif_info->cs_reg_count,
         .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
         .alloc_buffer = alloc_cs_buffer,
         .cookie = cmdbuf,
         .ls_sb_slot = SB_ID(LS),
      };

      if (PANVK_DEBUG(CS)) {
         cmdbuf->state.cs[i].reg_access.upd_ctx_stack = NULL;
         cmdbuf->state.cs[i].reg_access.base_perm = base_reg_perms[i];
         conf.reg_perm = cs_reg_perm;
      }

      cs_builder_init(b, &conf, root_cs);

      if (PANVK_DEBUG(TRACE)) {
         cmdbuf->state.cs[i].tracing = (struct cs_tracing_ctx){
            .enabled = true,
            .ctx_reg = cs_subqueue_ctx_reg(b),
            .tracebuf_addr_offset =
               offsetof(struct panvk_cs_subqueue_context, debug.tracebuf.cs),
         };
      }
   }
}

static void
panvk_reset_cmdbuf(struct vk_command_buffer *vk_cmdbuf,
                   VkCommandBufferResetFlags flags)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_cmdbuf->pool, struct panvk_cmd_pool, vk);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   vk_command_buffer_reset(&cmdbuf->vk);

   panvk_pool_reset(&cmdbuf->cs_pool);
   panvk_pool_reset(&cmdbuf->desc_pool);
   panvk_pool_reset(&cmdbuf->tls_pool);
   list_splicetail(&cmdbuf->push_sets, &pool->push_sets);
   list_inithead(&cmdbuf->push_sets);

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->utrace.uts); i++) {
      struct u_trace *ut = &cmdbuf->utrace.uts[i];
      u_trace_fini(ut);
      u_trace_init(ut, &dev->utrace.utctx);
   }

   memset(&cmdbuf->state, 0, sizeof(cmdbuf->state));
   init_cs_builders(cmdbuf);
}

static void
panvk_destroy_cmdbuf(struct vk_command_buffer *vk_cmdbuf)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_cmdbuf->pool, struct panvk_cmd_pool, vk);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->utrace.uts); i++)
      u_trace_fini(&cmdbuf->utrace.uts[i]);

   panvk_pool_cleanup(&cmdbuf->cs_pool);
   panvk_pool_cleanup(&cmdbuf->desc_pool);
   panvk_pool_cleanup(&cmdbuf->tls_pool);
   list_splicetail(&cmdbuf->push_sets, &pool->push_sets);
   vk_command_buffer_finish(&cmdbuf->vk);
   vk_free(&dev->vk.alloc, cmdbuf);
}

static VkResult
panvk_create_cmdbuf(struct vk_command_pool *vk_pool, VkCommandBufferLevel level,
                    struct vk_command_buffer **cmdbuf_out)
{
   struct panvk_device *device =
      container_of(vk_pool->base.device, struct panvk_device, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_pool, struct panvk_cmd_pool, vk);
   struct panvk_cmd_buffer *cmdbuf;

   cmdbuf = vk_zalloc(&device->vk.alloc, sizeof(*cmdbuf), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmdbuf)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(
      &pool->vk, &cmdbuf->vk, &panvk_per_arch(cmd_buffer_ops), level);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, cmdbuf);
      return result;
   }

   list_inithead(&cmdbuf->push_sets);
   cmdbuf->vk.dynamic_graphics_state.vi = &cmdbuf->state.gfx.dynamic.vi;
   cmdbuf->vk.dynamic_graphics_state.ms.sample_locations =
      &cmdbuf->state.gfx.dynamic.sl;

   struct panvk_pool_properties cs_pool_props = {
      .create_flags = 0,
      .slab_size = 64 * 1024,
      .label = "Command buffer CS pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->cs_pool, device, &pool->cs_bo_pool, NULL, &cs_pool_props);

   struct panvk_pool_properties desc_pool_props = {
      .create_flags = 0,
      .slab_size = 64 * 1024,
      .label = "Command buffer descriptor pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->desc_pool, device, &pool->desc_bo_pool, NULL,
                   &desc_pool_props);

   struct panvk_pool_properties tls_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_NO_MMAP),
      .slab_size = 64 * 1024,
      .label = "TLS pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->tls_pool, device, &pool->tls_bo_pool, &pool->tls_big_bo_pool,
                   &tls_pool_props);

   for (uint32_t i = 0; i < ARRAY_SIZE(cmdbuf->utrace.uts); i++)
      u_trace_init(&cmdbuf->utrace.uts[i], &device->utrace.utctx);

   init_cs_builders(cmdbuf);
   *cmdbuf_out = &cmdbuf->vk;
   return VK_SUCCESS;
}

const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops) = {
   .create = panvk_create_cmdbuf,
   .reset = panvk_reset_cmdbuf,
   .destroy = panvk_destroy_cmdbuf,
};

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(BeginCommandBuffer)(VkCommandBuffer commandBuffer,
                                   const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   vk_command_buffer_begin(&cmdbuf->vk, pBeginInfo);
   cmdbuf->flags = pBeginInfo->flags;

   if (PANVK_DEBUG(FORCE_SIMULTANEOUS)) {
      cmdbuf->flags |= VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
      cmdbuf->flags &= ~VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   }

   panvk_per_arch(cmd_inherit_render_state)(cmdbuf, pBeginInfo);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
      panvk_per_arch(panvk_instr_begin_work)(i, cmdbuf,
                                             PANVK_INSTR_WORK_TYPE_CMDBUF);

   return VK_SUCCESS;
}

static void
panvk_cmd_invalidate_state(struct panvk_cmd_buffer *cmdbuf)
{
   /* From the Vulkan 1.3.275 spec:
    *
    *    "...There is one exception to this rule - if the primary command
    *    buffer is inside a render pass instance, then the render pass and
    *    subpass state is not disturbed by executing secondary command
    *    buffers."
    *
    * We need to reset everything EXCEPT the render pass state.
    */
   struct panvk_rendering_state render_save = cmdbuf->state.gfx.render;
   memset(&cmdbuf->state.gfx, 0, sizeof(cmdbuf->state.gfx));
   cmdbuf->state.gfx.render = render_save;

   vk_dynamic_graphics_state_dirty_all(&cmdbuf->vk.dynamic_graphics_state);
   gfx_state_set_all_dirty(cmdbuf);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdExecuteCommands)(VkCommandBuffer commandBuffer,
                                   uint32_t commandBufferCount,
                                   const VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, primary, commandBuffer);

   if (commandBufferCount == 0)
      return;

   /* Write out any pending seqno changes to registers before calling
    * secondary command buffers. */
   flush_sync_points(primary);

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(panvk_cmd_buffer, secondary, pCommandBuffers[i]);

      /* make sure the CS context is setup properly
       * to inherit the primary command buffer state
       */
      primary->state.tls.info.tls.size =
         MAX2(primary->state.tls.info.tls.size,
              secondary->state.tls.info.tls.size);
      panvk_per_arch(cmd_prepare_exec_cmd_for_draws)(primary, secondary);

      for (uint32_t j = 0; j < ARRAY_SIZE(primary->state.cs); j++) {
         struct cs_builder *sec_b = panvk_get_cs_builder(secondary, j);
         assert(cs_is_valid(sec_b));
         if (!cs_is_empty(sec_b)) {
            struct cs_builder *prim_b = panvk_get_cs_builder(primary, j);
            struct cs_index addr = cs_scratch_reg64(prim_b, 0);
            struct cs_index size = cs_scratch_reg32(prim_b, 2);
            cs_move64_to(prim_b, addr, cs_root_chunk_gpu_addr(sec_b));
            cs_move32_to(prim_b, size, cs_root_chunk_size(sec_b));
            cs_call(prim_b, addr, size);
            prim_b->req_resource_mask |= sec_b->req_resource_mask;

            struct u_trace *prim_ut = &primary->utrace.uts[j];
            struct u_trace *sec_ut = &secondary->utrace.uts[j];
            u_trace_clone_append(u_trace_begin_iterator(sec_ut),
                                 u_trace_end_iterator(sec_ut), prim_ut, prim_b,
                                 panvk_per_arch(utrace_copy_buffer));
         }
      }

      /* We need to propagate the suspending state of the secondary command
       * buffer if we want to avoid poisoning the reg file when the secondary
       * command buffer suspended the render pass. */
      primary->state.gfx.render.suspended =
         secondary->state.gfx.render.suspended;

      /* Inherit the occlusion query state so that if the secondary contains
       * begin/end, but the renderpass ends in the primary, the primary can
       * correctly detect that an oq has ended in EndRendering. */
      primary->state.gfx.render.oq = secondary->state.gfx.render.oq;

      /* If the render context we passed to the secondary command buffer got
       * invalidated, reset the FB/tiler descs and treat things as if we
       * suspended the render pass, since those descriptors have been
       * re-emitted by the secondary command buffer already. */
      if (secondary->state.gfx.render.invalidate_inherited_ctx) {
         memset(&primary->state.gfx.render.fbds, 0,
                sizeof(primary->state.gfx.render.fbds));
         primary->state.gfx.render.tiler = 0;
         primary->state.gfx.render.flags |= VK_RENDERING_RESUMING_BIT;
      }
   }

   /* From the Vulkan 1.3.275 spec:
    *
    *    "When secondary command buffer(s) are recorded to execute on a
    *    primary command buffer, the secondary command buffer inherits no
    *    state from the primary command buffer, and all state of the primary
    *    command buffer is undefined after an execute secondary command buffer
    *    command is recorded. There is one exception to this rule - if the
    *    primary command buffer is inside a render pass instance, then the
    *    render pass and subpass state is not disturbed by executing secondary
    *    command buffers. For state dependent commands (such as draws and
    *    dispatches), any state consumed by those commands must not be
    *    undefined."
    *
    * Therefore, it's the client's job to reset all the state in the primary
    * after the secondary executes.  However, if we're doing any internal
    * dirty tracking, we may miss the fact that a secondary has messed with
    * GPU state if we don't invalidate all our internal tracking.
    */
   panvk_cmd_invalidate_state(primary);
}
