/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_cmd_buffer.h"
#include "pvr_entrypoints.h"
#include "pvr_hw_pass.h"
#include "pvr_macros.h"
#include "pvr_pass.h"
#include "pvr_query.h"

void PVR_PER_ARCH(CmdResetQueryPool)(VkCommandBuffer commandBuffer,
                                     VkQueryPool queryPool,
                                     uint32_t firstQuery,
                                     uint32_t queryCount)
{
   VK_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_query_info query_info;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   if (queryCount == 0)
      return;

   query_info.type = PVR_QUERY_TYPE_RESET_QUERY_POOL;

   query_info.reset_query_pool.query_pool = queryPool;
   query_info.reset_query_pool.first_query = firstQuery;
   query_info.reset_query_pool.query_count = queryCount;

   /* make the query-reset program wait for previous geom/frag,
    * to not overwrite them
    */
   result =
      pvr_arch_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_EVENT);
   if (result != VK_SUCCESS)
      return;

   cmd_buffer->state.current_sub_cmd->event = (struct pvr_sub_cmd_event){
      .type = PVR_EVENT_TYPE_BARRIER,
      .barrier = {
         .wait_for_stage_mask = PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS,
         .wait_at_stage_mask = PVR_PIPELINE_STAGE_QUERY_BIT,
      },
   };

   /* add the query-program itself */
   result = pvr_arch_add_query_program(cmd_buffer, &query_info);
   if (result != VK_SUCCESS)
      return;

   /* make future geom/frag wait for the query-reset program to
    * reset the counters to 0
    */
   result =
      pvr_arch_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_EVENT);
   if (result != VK_SUCCESS)
      return;

   cmd_buffer->state.current_sub_cmd->event = (struct pvr_sub_cmd_event){
      .type = PVR_EVENT_TYPE_BARRIER,
      .barrier = {
         .wait_for_stage_mask = PVR_PIPELINE_STAGE_QUERY_BIT,
         .wait_at_stage_mask = PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS,
      },
   };
}

void PVR_PER_ARCH(CmdCopyQueryPoolResults)(VkCommandBuffer commandBuffer,
                                           VkQueryPool queryPool,
                                           uint32_t firstQuery,
                                           uint32_t queryCount,
                                           VkBuffer dstBuffer,
                                           VkDeviceSize dstOffset,
                                           VkDeviceSize stride,
                                           VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_query_info query_info;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   if (queryCount == 0)
      return;

   query_info.type = PVR_QUERY_TYPE_COPY_QUERY_RESULTS;

   query_info.copy_query_results.query_pool = queryPool;
   query_info.copy_query_results.first_query = firstQuery;
   query_info.copy_query_results.query_count = queryCount;
   query_info.copy_query_results.dst_buffer = dstBuffer;
   query_info.copy_query_results.dst_offset = dstOffset;
   query_info.copy_query_results.stride = stride;
   query_info.copy_query_results.flags = flags;

   result =
      pvr_arch_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_EVENT);
   if (result != VK_SUCCESS)
      return;

   /* The Vulkan 1.3.231 spec says:
    *
    *    "vkCmdCopyQueryPoolResults is considered to be a transfer operation,
    *    and its writes to buffer memory must be synchronized using
    *    VK_PIPELINE_STAGE_TRANSFER_BIT and VK_ACCESS_TRANSFER_WRITE_BIT before
    *    using the results."
    *
    */
   /* We record barrier event sub commands to sync the compute job used for the
    * copy query results program with transfer jobs to prevent an overlapping
    * transfer job with the compute job.
    */

   cmd_buffer->state.current_sub_cmd->event = (struct pvr_sub_cmd_event){
      .type = PVR_EVENT_TYPE_BARRIER,
      .barrier = {
         .wait_for_stage_mask = PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS | PVR_PIPELINE_STAGE_TRANSFER_BIT,
         .wait_at_stage_mask = PVR_PIPELINE_STAGE_QUERY_BIT,
      },
   };

   result = pvr_arch_cmd_buffer_end_sub_cmd(cmd_buffer);
   if (result != VK_SUCCESS)
      return;

   pvr_arch_add_query_program(cmd_buffer, &query_info);

   result =
      pvr_arch_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_EVENT);
   if (result != VK_SUCCESS)
      return;

   cmd_buffer->state.current_sub_cmd->event = (struct pvr_sub_cmd_event){
      .type = PVR_EVENT_TYPE_BARRIER,
      .barrier = {
         .wait_for_stage_mask = PVR_PIPELINE_STAGE_QUERY_BIT,
         .wait_at_stage_mask = PVR_PIPELINE_STAGE_ALL_GRAPHICS_BITS | PVR_PIPELINE_STAGE_TRANSFER_BIT,
      },
   };
}

static inline const uint32_t
pvr_cmd_buffer_state_get_view_count(const struct pvr_cmd_buffer_state *state)
{
   const struct pvr_render_pass_info *render_pass_info =
      &state->render_pass_info;
   const struct pvr_sub_cmd_gfx *gfx_sub_cmd = &state->current_sub_cmd->gfx;
   const uint32_t hw_render_idx = gfx_sub_cmd->hw_render_idx;
   const struct pvr_renderpass_hwsetup_render *hw_render =
      pvr_arch_pass_info_get_hw_render(render_pass_info, hw_render_idx);
   const uint32_t view_count = util_bitcount(hw_render->view_mask);

   assert(state->current_sub_cmd->type == PVR_SUB_CMD_TYPE_GRAPHICS);
   /* hw_render view masks have 1 bit set at least. */
   assert(view_count);

   return view_count;
}

void PVR_PER_ARCH(CmdBeginQuery)(VkCommandBuffer commandBuffer,
                                 VkQueryPool queryPool,
                                 uint32_t query,
                                 VkQueryControlFlags flags)
{
   VK_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   uint32_t view_count = 1;
   VK_FROM_HANDLE(pvr_query_pool, pool, queryPool);

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   /* Occlusion queries can't be nested. */
   assert(!state->vis_test_enabled);

   if (state->current_sub_cmd &&
       state->current_sub_cmd->type == PVR_SUB_CMD_TYPE_GRAPHICS) {
      if (!state->current_sub_cmd->gfx.query_pool) {
         state->current_sub_cmd->gfx.query_pool = pool;
      } else if (state->current_sub_cmd->gfx.query_pool != pool) {
         VkResult result;

         /* Kick render. */
         state->current_sub_cmd->gfx.barrier_store = true;

         result = pvr_arch_cmd_buffer_end_sub_cmd(cmd_buffer);
         if (result != VK_SUCCESS)
            return;

         result = pvr_arch_cmd_buffer_start_sub_cmd(cmd_buffer,
                                                    PVR_SUB_CMD_TYPE_GRAPHICS);
         if (result != VK_SUCCESS)
            return;

         /* Use existing render setup, but load color attachments from HW
          * BGOBJ.
          */
         state->current_sub_cmd->gfx.barrier_load = true;
         state->current_sub_cmd->gfx.barrier_store = false;
         state->current_sub_cmd->gfx.query_pool = pool;
      }

      view_count = pvr_cmd_buffer_state_get_view_count(state);
   }

   state->query_pool = pool;
   state->vis_test_enabled = true;
   state->vis_reg = query;
   state->dirty.vis_test = true;

   /* Add the index to the list for this render. */
   for (uint32_t i = 0; i < view_count; i++) {
      util_dynarray_append(&state->query_indices, query);
   }
}

void PVR_PER_ARCH(CmdEndQuery)(VkCommandBuffer commandBuffer,
                               VkQueryPool queryPool,
                               uint32_t query)
{
   VK_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   state->vis_test_enabled = false;
   state->dirty.vis_test = true;
}
