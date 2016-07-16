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

#include "vk_format_info.h"

/** \file anv_cmd_buffer.c
 *
 * This file contains all of the stuff for emitting commands into a command
 * buffer.  This includes implementations of most of the vkCmd*
 * entrypoints.  This file is concerned entirely with state emission and
 * not with the command buffer data structure itself.  As far as this file
 * is concerned, most of anv_cmd_buffer is magic.
 */

/* TODO: These are taken from GLES.  We should check the Vulkan spec */
const struct anv_dynamic_state default_dynamic_state = {
   .viewport = {
      .count = 0,
   },
   .scissor = {
      .count = 0,
   },
   .line_width = 1.0f,
   .depth_bias = {
      .bias = 0.0f,
      .clamp = 0.0f,
      .slope = 0.0f,
   },
   .blend_constants = { 0.0f, 0.0f, 0.0f, 0.0f },
   .depth_bounds = {
      .min = 0.0f,
      .max = 1.0f,
   },
   .stencil_compare_mask = {
      .front = ~0u,
      .back = ~0u,
   },
   .stencil_write_mask = {
      .front = ~0u,
      .back = ~0u,
   },
   .stencil_reference = {
      .front = 0u,
      .back = 0u,
   },
};

void
anv_dynamic_state_copy(struct anv_dynamic_state *dest,
                       const struct anv_dynamic_state *src,
                       uint32_t copy_mask)
{
   if (copy_mask & (1 << VK_DYNAMIC_STATE_VIEWPORT)) {
      dest->viewport.count = src->viewport.count;
      typed_memcpy(dest->viewport.viewports, src->viewport.viewports,
                   src->viewport.count);
   }

   if (copy_mask & (1 << VK_DYNAMIC_STATE_SCISSOR)) {
      dest->scissor.count = src->scissor.count;
      typed_memcpy(dest->scissor.scissors, src->scissor.scissors,
                   src->scissor.count);
   }

   if (copy_mask & (1 << VK_DYNAMIC_STATE_LINE_WIDTH))
      dest->line_width = src->line_width;

   if (copy_mask & (1 << VK_DYNAMIC_STATE_DEPTH_BIAS))
      dest->depth_bias = src->depth_bias;

   if (copy_mask & (1 << VK_DYNAMIC_STATE_BLEND_CONSTANTS))
      typed_memcpy(dest->blend_constants, src->blend_constants, 4);

   if (copy_mask & (1 << VK_DYNAMIC_STATE_DEPTH_BOUNDS))
      dest->depth_bounds = src->depth_bounds;

   if (copy_mask & (1 << VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK))
      dest->stencil_compare_mask = src->stencil_compare_mask;

   if (copy_mask & (1 << VK_DYNAMIC_STATE_STENCIL_WRITE_MASK))
      dest->stencil_write_mask = src->stencil_write_mask;

   if (copy_mask & (1 << VK_DYNAMIC_STATE_STENCIL_REFERENCE))
      dest->stencil_reference = src->stencil_reference;
}

static void
anv_cmd_state_reset(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_cmd_state *state = &cmd_buffer->state;

   memset(&state->descriptors, 0, sizeof(state->descriptors));
   memset(&state->push_constants, 0, sizeof(state->push_constants));
   memset(state->binding_tables, 0, sizeof(state->binding_tables));
   memset(state->samplers, 0, sizeof(state->samplers));

   /* 0 isn't a valid config.  This ensures that we always configure L3$. */
   cmd_buffer->state.current_l3_config = 0;

   state->dirty = 0;
   state->vb_dirty = 0;
   state->pending_pipe_bits = 0;
   state->descriptors_dirty = 0;
   state->push_constants_dirty = 0;
   state->pipeline = NULL;
   state->push_constant_stages = 0;
   state->restart_index = UINT32_MAX;
   state->dynamic = default_dynamic_state;
   state->need_query_wa = true;

   if (state->attachments != NULL) {
      anv_free(&cmd_buffer->pool->alloc, state->attachments);
      state->attachments = NULL;
   }

   state->gen7.index_buffer = NULL;
}

/**
 * Setup anv_cmd_state::attachments for vkCmdBeginRenderPass.
 */
void
anv_cmd_state_setup_attachments(struct anv_cmd_buffer *cmd_buffer,
                                const VkRenderPassBeginInfo *info)
{
   struct anv_cmd_state *state = &cmd_buffer->state;
   ANV_FROM_HANDLE(anv_render_pass, pass, info->renderPass);

   anv_free(&cmd_buffer->pool->alloc, state->attachments);

   if (pass->attachment_count == 0) {
      state->attachments = NULL;
      return;
   }

   state->attachments = anv_alloc(&cmd_buffer->pool->alloc,
                                  pass->attachment_count *
                                       sizeof(state->attachments[0]),
                                  8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (state->attachments == NULL) {
      /* FIXME: Propagate VK_ERROR_OUT_OF_HOST_MEMORY to vkEndCommandBuffer */
      abort();
   }

   for (uint32_t i = 0; i < pass->attachment_count; ++i) {
      struct anv_render_pass_attachment *att = &pass->attachments[i];
      VkImageAspectFlags att_aspects = vk_format_aspects(att->format);
      VkImageAspectFlags clear_aspects = 0;

      if (att_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
         /* color attachment */
         if (att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
         }
      } else {
         /* depthstencil attachment */
         if ((att_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
             att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
         }
         if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
             att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
         }
      }

      state->attachments[i].pending_clear_aspects = clear_aspects;
      if (clear_aspects) {
         assert(info->clearValueCount > i);
         state->attachments[i].clear_value = info->pClearValues[i];
      }
   }
}

static VkResult
anv_cmd_buffer_ensure_push_constants_size(struct anv_cmd_buffer *cmd_buffer,
                                          gl_shader_stage stage, uint32_t size)
{
   struct anv_push_constants **ptr = &cmd_buffer->state.push_constants[stage];

   if (*ptr == NULL) {
      *ptr = anv_alloc(&cmd_buffer->pool->alloc, size, 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (*ptr == NULL)
         return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
   } else if ((*ptr)->size < size) {
      *ptr = anv_realloc(&cmd_buffer->pool->alloc, *ptr, size, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (*ptr == NULL)
         return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   (*ptr)->size = size;

   return VK_SUCCESS;
}

#define anv_cmd_buffer_ensure_push_constant_field(cmd_buffer, stage, field) \
   anv_cmd_buffer_ensure_push_constants_size(cmd_buffer, stage, \
      (offsetof(struct anv_push_constants, field) + \
       sizeof(cmd_buffer->state.push_constants[0]->field)))

static VkResult anv_create_cmd_buffer(
    struct anv_device *                         device,
    struct anv_cmd_pool *                       pool,
    VkCommandBufferLevel                        level,
    VkCommandBuffer*                            pCommandBuffer)
{
   struct anv_cmd_buffer *cmd_buffer;
   VkResult result;

   cmd_buffer = anv_alloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   cmd_buffer->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   cmd_buffer->device = device;
   cmd_buffer->pool = pool;
   cmd_buffer->level = level;
   cmd_buffer->state.attachments = NULL;

   result = anv_cmd_buffer_init_batch_bo_chain(cmd_buffer);
   if (result != VK_SUCCESS)
      goto fail;

   anv_state_stream_init(&cmd_buffer->surface_state_stream,
                         &device->surface_state_block_pool);
   anv_state_stream_init(&cmd_buffer->dynamic_state_stream,
                         &device->dynamic_state_block_pool);

   if (pool) {
      list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);
   } else {
      /* Init the pool_link so we can safefly call list_del when we destroy
       * the command buffer
       */
      list_inithead(&cmd_buffer->pool_link);
   }

   *pCommandBuffer = anv_cmd_buffer_to_handle(cmd_buffer);

   return VK_SUCCESS;

 fail:
   anv_free(&cmd_buffer->pool->alloc, cmd_buffer);

   return result;
}

VkResult anv_AllocateCommandBuffers(
    VkDevice                                    _device,
    const VkCommandBufferAllocateInfo*          pAllocateInfo,
    VkCommandBuffer*                            pCommandBuffers)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_cmd_pool, pool, pAllocateInfo->commandPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
      result = anv_create_cmd_buffer(device, pool, pAllocateInfo->level,
                                     &pCommandBuffers[i]);
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS)
      anv_FreeCommandBuffers(_device, pAllocateInfo->commandPool,
                             i, pCommandBuffers);

   return result;
}

static void
anv_cmd_buffer_destroy(struct anv_cmd_buffer *cmd_buffer)
{
   list_del(&cmd_buffer->pool_link);

   anv_cmd_buffer_fini_batch_bo_chain(cmd_buffer);

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);

   anv_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);
   anv_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

void anv_FreeCommandBuffers(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

      anv_cmd_buffer_destroy(cmd_buffer);
   }
}

static VkResult
anv_cmd_buffer_reset(struct anv_cmd_buffer *cmd_buffer)
{
   cmd_buffer->usage_flags = 0;
   cmd_buffer->state.current_pipeline = UINT32_MAX;
   anv_cmd_buffer_reset_batch_bo_chain(cmd_buffer);
   anv_cmd_state_reset(cmd_buffer);

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_init(&cmd_buffer->surface_state_stream,
                         &cmd_buffer->device->surface_state_block_pool);

   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);
   anv_state_stream_init(&cmd_buffer->dynamic_state_stream,
                         &cmd_buffer->device->dynamic_state_block_pool);
   return VK_SUCCESS;
}

VkResult anv_ResetCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   return anv_cmd_buffer_reset(cmd_buffer);
}

void
anv_cmd_buffer_emit_state_base_address(struct anv_cmd_buffer *cmd_buffer)
{
   switch (cmd_buffer->device->info.gen) {
   case 7:
      if (cmd_buffer->device->info.is_haswell)
         return gen7_cmd_buffer_emit_state_base_address(cmd_buffer);
      else
         return gen7_cmd_buffer_emit_state_base_address(cmd_buffer);
   case 8:
      return gen8_cmd_buffer_emit_state_base_address(cmd_buffer);
   case 9:
      return gen9_cmd_buffer_emit_state_base_address(cmd_buffer);
   default:
      unreachable("unsupported gen\n");
   }
}

VkResult anv_BeginCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    const VkCommandBufferBeginInfo*             pBeginInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   /* If this is the first vkBeginCommandBuffer, we must *initialize* the
    * command buffer's state. Otherwise, we must *reset* its state. In both
    * cases we reset it.
    *
    * From the Vulkan 1.0 spec:
    *
    *    If a command buffer is in the executable state and the command buffer
    *    was allocated from a command pool with the
    *    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT flag set, then
    *    vkBeginCommandBuffer implicitly resets the command buffer, behaving
    *    as if vkResetCommandBuffer had been called with
    *    VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT not set. It then puts
    *    the command buffer in the recording state.
    */
   anv_cmd_buffer_reset(cmd_buffer);

   cmd_buffer->usage_flags = pBeginInfo->flags;

   assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY ||
          !(cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT));

   anv_cmd_buffer_emit_state_base_address(cmd_buffer);

   if (cmd_buffer->usage_flags &
       VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
      cmd_buffer->state.framebuffer =
         anv_framebuffer_from_handle(pBeginInfo->pInheritanceInfo->framebuffer);
      cmd_buffer->state.pass =
         anv_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);

      struct anv_subpass *subpass =
         &cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];

      anv_cmd_buffer_set_subpass(cmd_buffer, subpass);
   }

   return VK_SUCCESS;
}

VkResult anv_EndCommandBuffer(
    VkCommandBuffer                             commandBuffer)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_device *device = cmd_buffer->device;

   anv_cmd_buffer_end_batch_buffer(cmd_buffer);

   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      /* The algorithm used to compute the validate list is not threadsafe as
       * it uses the bo->index field.  We have to lock the device around it.
       * Fortunately, the chances for contention here are probably very low.
       */
      pthread_mutex_lock(&device->mutex);
      anv_cmd_buffer_prepare_execbuf(cmd_buffer);
      pthread_mutex_unlock(&device->mutex);
   }

   return VK_SUCCESS;
}

void anv_CmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  _pipeline)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_pipeline, pipeline, _pipeline);

   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      cmd_buffer->state.compute_pipeline = pipeline;
      cmd_buffer->state.compute_dirty |= ANV_CMD_DIRTY_PIPELINE;
      cmd_buffer->state.push_constants_dirty |= VK_SHADER_STAGE_COMPUTE_BIT;
      cmd_buffer->state.descriptors_dirty |= VK_SHADER_STAGE_COMPUTE_BIT;
      break;

   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      cmd_buffer->state.pipeline = pipeline;
      cmd_buffer->state.vb_dirty |= pipeline->vb_used;
      cmd_buffer->state.dirty |= ANV_CMD_DIRTY_PIPELINE;
      cmd_buffer->state.push_constants_dirty |= pipeline->active_stages;
      cmd_buffer->state.descriptors_dirty |= pipeline->active_stages;

      /* Apply the dynamic state from the pipeline */
      cmd_buffer->state.dirty |= pipeline->dynamic_state_mask;
      anv_dynamic_state_copy(&cmd_buffer->state.dynamic,
                             &pipeline->dynamic_state,
                             pipeline->dynamic_state_mask);
      break;

   default:
      assert(!"invalid bind point");
      break;
   }
}

void anv_CmdSetViewport(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   const uint32_t total_count = firstViewport + viewportCount;
   if (cmd_buffer->state.dynamic.viewport.count < total_count)
      cmd_buffer->state.dynamic.viewport.count = total_count;

   memcpy(cmd_buffer->state.dynamic.viewport.viewports + firstViewport,
          pViewports, viewportCount * sizeof(*pViewports));

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_VIEWPORT;
}

void anv_CmdSetScissor(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   const uint32_t total_count = firstScissor + scissorCount;
   if (cmd_buffer->state.dynamic.scissor.count < total_count)
      cmd_buffer->state.dynamic.scissor.count = total_count;

   memcpy(cmd_buffer->state.dynamic.scissor.scissors + firstScissor,
          pScissors, scissorCount * sizeof(*pScissors));

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_SCISSOR;
}

void anv_CmdSetLineWidth(
    VkCommandBuffer                             commandBuffer,
    float                                       lineWidth)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.dynamic.line_width = lineWidth;
   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH;
}

void anv_CmdSetDepthBias(
    VkCommandBuffer                             commandBuffer,
    float                                       depthBiasConstantFactor,
    float                                       depthBiasClamp,
    float                                       depthBiasSlopeFactor)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.dynamic.depth_bias.bias = depthBiasConstantFactor;
   cmd_buffer->state.dynamic.depth_bias.clamp = depthBiasClamp;
   cmd_buffer->state.dynamic.depth_bias.slope = depthBiasSlopeFactor;

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
}

void anv_CmdSetBlendConstants(
    VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4])
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   memcpy(cmd_buffer->state.dynamic.blend_constants,
          blendConstants, sizeof(float) * 4);

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS;
}

void anv_CmdSetDepthBounds(
    VkCommandBuffer                             commandBuffer,
    float                                       minDepthBounds,
    float                                       maxDepthBounds)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.dynamic.depth_bounds.min = minDepthBounds;
   cmd_buffer->state.dynamic.depth_bounds.max = maxDepthBounds;

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS;
}

void anv_CmdSetStencilCompareMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.dynamic.stencil_compare_mask.front = compareMask;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.dynamic.stencil_compare_mask.back = compareMask;

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK;
}

void anv_CmdSetStencilWriteMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.dynamic.stencil_write_mask.front = writeMask;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.dynamic.stencil_write_mask.back = writeMask;

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK;
}

void anv_CmdSetStencilReference(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.dynamic.stencil_reference.front = reference;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.dynamic.stencil_reference.back = reference;

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE;
}

void anv_CmdBindDescriptorSets(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            _layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_pipeline_layout, layout, _layout);
   struct anv_descriptor_set_layout *set_layout;

   assert(firstSet + descriptorSetCount < MAX_SETS);

   uint32_t dynamic_slot = 0;
   for (uint32_t i = 0; i < descriptorSetCount; i++) {
      ANV_FROM_HANDLE(anv_descriptor_set, set, pDescriptorSets[i]);
      set_layout = layout->set[firstSet + i].layout;

      if (cmd_buffer->state.descriptors[firstSet + i] != set) {
         cmd_buffer->state.descriptors[firstSet + i] = set;
         cmd_buffer->state.descriptors_dirty |= set_layout->shader_stages;
      }

      if (set_layout->dynamic_offset_count > 0) {
         anv_foreach_stage(s, set_layout->shader_stages) {
            anv_cmd_buffer_ensure_push_constant_field(cmd_buffer, s, dynamic);

            struct anv_push_constants *push =
               cmd_buffer->state.push_constants[s];

            unsigned d = layout->set[firstSet + i].dynamic_offset_start;
            const uint32_t *offsets = pDynamicOffsets + dynamic_slot;
            struct anv_descriptor *desc = set->descriptors;

            for (unsigned b = 0; b < set_layout->binding_count; b++) {
               if (set_layout->binding[b].dynamic_offset_index < 0)
                  continue;

               unsigned array_size = set_layout->binding[b].array_size;
               for (unsigned j = 0; j < array_size; j++) {
                  uint32_t range = 0;
                  if (desc->buffer_view)
                     range = desc->buffer_view->range;
                  push->dynamic[d].offset = *(offsets++);
                  push->dynamic[d].range = range;
                  desc++;
                  d++;
               }
            }
         }
         cmd_buffer->state.push_constants_dirty |= set_layout->shader_stages;
      }
   }
}

void anv_CmdBindVertexBuffers(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_vertex_binding *vb = cmd_buffer->state.vertex_bindings;

   /* We have to defer setting up vertex buffer since we need the buffer
    * stride from the pipeline. */

   assert(firstBinding + bindingCount < MAX_VBS);
   for (uint32_t i = 0; i < bindingCount; i++) {
      vb[firstBinding + i].buffer = anv_buffer_from_handle(pBuffers[i]);
      vb[firstBinding + i].offset = pOffsets[i];
      cmd_buffer->state.vb_dirty |= 1 << (firstBinding + i);
   }
}

static void
add_surface_state_reloc(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_state state, struct anv_bo *bo, uint32_t offset)
{
   /* The address goes in SURFACE_STATE dword 1 for gens < 8 and dwords 8 and
    * 9 for gen8+.  We only write the first dword for gen8+ here and rely on
    * the initial state to set the high bits to 0. */

   const uint32_t dword = cmd_buffer->device->info.gen < 8 ? 1 : 8;

   anv_reloc_list_add(&cmd_buffer->surface_relocs, &cmd_buffer->pool->alloc,
                      state.offset + dword * 4, bo, offset);
}

enum isl_format
anv_isl_format_for_descriptor_type(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return ISL_FORMAT_R32G32B32A32_FLOAT;

   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return ISL_FORMAT_RAW;

   default:
      unreachable("Invalid descriptor type");
   }
}

static struct anv_state
anv_cmd_buffer_alloc_null_surface_state(struct anv_cmd_buffer *cmd_buffer,
                                        struct anv_framebuffer *fb)
{
   switch (cmd_buffer->device->info.gen) {
   case 7:
      if (cmd_buffer->device->info.is_haswell) {
         return gen75_cmd_buffer_alloc_null_surface_state(cmd_buffer, fb);
      } else {
         return gen7_cmd_buffer_alloc_null_surface_state(cmd_buffer, fb);
      }
   case 8:
      return gen8_cmd_buffer_alloc_null_surface_state(cmd_buffer, fb);
   case 9:
      return gen9_cmd_buffer_alloc_null_surface_state(cmd_buffer, fb);
   default:
      unreachable("Invalid hardware generation");
   }
}

VkResult
anv_cmd_buffer_emit_binding_table(struct anv_cmd_buffer *cmd_buffer,
                                  gl_shader_stage stage,
                                  struct anv_state *bt_state)
{
   struct anv_framebuffer *fb = cmd_buffer->state.framebuffer;
   struct anv_subpass *subpass = cmd_buffer->state.subpass;
   struct anv_pipeline_bind_map *map;
   uint32_t bias, state_offset;

   switch (stage) {
   case  MESA_SHADER_COMPUTE:
      map = &cmd_buffer->state.compute_pipeline->bindings[stage];
      bias = 1;
      break;
   default:
      map = &cmd_buffer->state.pipeline->bindings[stage];
      bias = 0;
      break;
   }

   if (bias + map->surface_count == 0) {
      *bt_state = (struct anv_state) { 0, };
      return VK_SUCCESS;
   }

   *bt_state = anv_cmd_buffer_alloc_binding_table(cmd_buffer,
                                                  bias + map->surface_count,
                                                  &state_offset);
   uint32_t *bt_map = bt_state->map;

   if (bt_state->map == NULL)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   if (stage == MESA_SHADER_COMPUTE &&
       get_cs_prog_data(cmd_buffer->state.compute_pipeline)->uses_num_work_groups) {
      struct anv_bo *bo = cmd_buffer->state.num_workgroups_bo;
      uint32_t bo_offset = cmd_buffer->state.num_workgroups_offset;

      struct anv_state surface_state;
      surface_state =
         anv_cmd_buffer_alloc_surface_state(cmd_buffer);

      const enum isl_format format =
         anv_isl_format_for_descriptor_type(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
      anv_fill_buffer_surface_state(cmd_buffer->device, surface_state,
                                    format, bo_offset, 12, 1);

      bt_map[0] = surface_state.offset + state_offset;
      add_surface_state_reloc(cmd_buffer, surface_state, bo, bo_offset);
   }

   if (map->surface_count == 0)
      goto out;

   if (map->image_count > 0) {
      VkResult result =
         anv_cmd_buffer_ensure_push_constant_field(cmd_buffer, stage, images);
      if (result != VK_SUCCESS)
         return result;

      cmd_buffer->state.push_constants_dirty |= 1 << stage;
   }

   uint32_t image = 0;
   for (uint32_t s = 0; s < map->surface_count; s++) {
      struct anv_pipeline_binding *binding = &map->surface_to_descriptor[s];

      struct anv_state surface_state;
      struct anv_bo *bo;
      uint32_t bo_offset;

      if (binding->set == ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS) {
         /* Color attachment binding */
         assert(stage == MESA_SHADER_FRAGMENT);
         assert(binding->binding == 0);
         if (binding->index < subpass->color_count) {
            const struct anv_image_view *iview =
               fb->attachments[subpass->color_attachments[binding->index]];

            assert(iview->color_rt_surface_state.alloc_size);
            surface_state = iview->color_rt_surface_state;
            add_surface_state_reloc(cmd_buffer, iview->color_rt_surface_state,
                                    iview->bo, iview->offset);
         } else {
            /* Null render target */
            struct anv_framebuffer *fb = cmd_buffer->state.framebuffer;
            surface_state =
               anv_cmd_buffer_alloc_null_surface_state(cmd_buffer, fb);
         }

         bt_map[bias + s] = surface_state.offset + state_offset;
         continue;
      }

      struct anv_descriptor_set *set =
         cmd_buffer->state.descriptors[binding->set];
      uint32_t offset = set->layout->binding[binding->binding].descriptor_index;
      struct anv_descriptor *desc = &set->descriptors[offset + binding->index];

      switch (desc->type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
         /* Nothing for us to do here */
         continue;

      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         surface_state = desc->image_view->sampler_surface_state;
         assert(surface_state.alloc_size);
         bo = desc->image_view->bo;
         bo_offset = desc->image_view->offset;
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
         surface_state = desc->image_view->storage_surface_state;
         assert(surface_state.alloc_size);
         bo = desc->image_view->bo;
         bo_offset = desc->image_view->offset;

         struct brw_image_param *image_param =
            &cmd_buffer->state.push_constants[stage]->images[image++];

         *image_param = desc->image_view->storage_image_param;
         image_param->surface_idx = bias + s;
         break;
      }

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         surface_state = desc->buffer_view->surface_state;
         assert(surface_state.alloc_size);
         bo = desc->buffer_view->bo;
         bo_offset = desc->buffer_view->offset;
         break;

      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         surface_state = desc->buffer_view->storage_surface_state;
         assert(surface_state.alloc_size);
         bo = desc->buffer_view->bo;
         bo_offset = desc->buffer_view->offset;

         struct brw_image_param *image_param =
            &cmd_buffer->state.push_constants[stage]->images[image++];

         *image_param = desc->buffer_view->storage_image_param;
         image_param->surface_idx = bias + s;
         break;

      default:
         assert(!"Invalid descriptor type");
         continue;
      }

      bt_map[bias + s] = surface_state.offset + state_offset;
      add_surface_state_reloc(cmd_buffer, surface_state, bo, bo_offset);
   }
   assert(image == map->image_count);

 out:
   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(*bt_state);

   return VK_SUCCESS;
}

VkResult
anv_cmd_buffer_emit_samplers(struct anv_cmd_buffer *cmd_buffer,
                             gl_shader_stage stage, struct anv_state *state)
{
   struct anv_pipeline_bind_map *map;

   if (stage == MESA_SHADER_COMPUTE)
      map = &cmd_buffer->state.compute_pipeline->bindings[stage];
   else
      map = &cmd_buffer->state.pipeline->bindings[stage];

   if (map->sampler_count == 0) {
      *state = (struct anv_state) { 0, };
      return VK_SUCCESS;
   }

   uint32_t size = map->sampler_count * 16;
   *state = anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, size, 32);

   if (state->map == NULL)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   for (uint32_t s = 0; s < map->sampler_count; s++) {
      struct anv_pipeline_binding *binding = &map->sampler_to_descriptor[s];
      struct anv_descriptor_set *set =
         cmd_buffer->state.descriptors[binding->set];
      uint32_t offset = set->layout->binding[binding->binding].descriptor_index;
      struct anv_descriptor *desc = &set->descriptors[offset + binding->index];

      if (desc->type != VK_DESCRIPTOR_TYPE_SAMPLER &&
          desc->type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
         continue;

      struct anv_sampler *sampler = desc->sampler;

      /* This can happen if we have an unfilled slot since TYPE_SAMPLER
       * happens to be zero.
       */
      if (sampler == NULL)
         continue;

      memcpy(state->map + (s * 16),
             sampler->state, sizeof(sampler->state));
   }

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(*state);

   return VK_SUCCESS;
}

uint32_t
anv_cmd_buffer_flush_descriptor_sets(struct anv_cmd_buffer *cmd_buffer)
{
   VkShaderStageFlags dirty = cmd_buffer->state.descriptors_dirty &
                              cmd_buffer->state.pipeline->active_stages;

   VkResult result = VK_SUCCESS;
   anv_foreach_stage(s, dirty) {
      result = anv_cmd_buffer_emit_samplers(cmd_buffer, s,
                                            &cmd_buffer->state.samplers[s]);
      if (result != VK_SUCCESS)
         break;
      result = anv_cmd_buffer_emit_binding_table(cmd_buffer, s,
                                                 &cmd_buffer->state.binding_tables[s]);
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS) {
      assert(result == VK_ERROR_OUT_OF_DEVICE_MEMORY);

      result = anv_cmd_buffer_new_binding_table_block(cmd_buffer);
      assert(result == VK_SUCCESS);

      /* Re-emit state base addresses so we get the new surface state base
       * address before we start emitting binding tables etc.
       */
      anv_cmd_buffer_emit_state_base_address(cmd_buffer);

      /* Re-emit all active binding tables */
      dirty |= cmd_buffer->state.pipeline->active_stages;
      anv_foreach_stage(s, dirty) {
         result = anv_cmd_buffer_emit_samplers(cmd_buffer, s,
                                               &cmd_buffer->state.samplers[s]);
         if (result != VK_SUCCESS)
            return result;
         result = anv_cmd_buffer_emit_binding_table(cmd_buffer, s,
                                                    &cmd_buffer->state.binding_tables[s]);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   cmd_buffer->state.descriptors_dirty &= ~dirty;

   return dirty;
}

struct anv_state
anv_cmd_buffer_emit_dynamic(struct anv_cmd_buffer *cmd_buffer,
                            const void *data, uint32_t size, uint32_t alignment)
{
   struct anv_state state;

   state = anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, size, alignment);
   memcpy(state.map, data, size);

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(state);

   VG(VALGRIND_CHECK_MEM_IS_DEFINED(state.map, size));

   return state;
}

struct anv_state
anv_cmd_buffer_merge_dynamic(struct anv_cmd_buffer *cmd_buffer,
                             uint32_t *a, uint32_t *b,
                             uint32_t dwords, uint32_t alignment)
{
   struct anv_state state;
   uint32_t *p;

   state = anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                              dwords * 4, alignment);
   p = state.map;
   for (uint32_t i = 0; i < dwords; i++)
      p[i] = a[i] | b[i];

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(state);

   VG(VALGRIND_CHECK_MEM_IS_DEFINED(p, dwords * 4));

   return state;
}

/**
 * @brief Setup the command buffer for recording commands inside the given
 * subpass.
 *
 * This does not record all commands needed for starting the subpass.
 * Starting the subpass may require additional commands.
 *
 * Note that vkCmdBeginRenderPass, vkCmdNextSubpass, and vkBeginCommandBuffer
 * with VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, all setup the
 * command buffer for recording commands for some subpass.  But only the first
 * two, vkCmdBeginRenderPass and vkCmdNextSubpass, can start a subpass.
 */
void
anv_cmd_buffer_set_subpass(struct anv_cmd_buffer *cmd_buffer,
                           struct anv_subpass *subpass)
{
   switch (cmd_buffer->device->info.gen) {
   case 7:
      if (cmd_buffer->device->info.is_haswell) {
         gen75_cmd_buffer_set_subpass(cmd_buffer, subpass);
      } else {
         gen7_cmd_buffer_set_subpass(cmd_buffer, subpass);
      }
      break;
   case 8:
      gen8_cmd_buffer_set_subpass(cmd_buffer, subpass);
      break;
   case 9:
      gen9_cmd_buffer_set_subpass(cmd_buffer, subpass);
      break;
   default:
      unreachable("unsupported gen\n");
   }
}

struct anv_state
anv_cmd_buffer_push_constants(struct anv_cmd_buffer *cmd_buffer,
                              gl_shader_stage stage)
{
   struct anv_push_constants *data =
      cmd_buffer->state.push_constants[stage];
   const struct brw_stage_prog_data *prog_data =
      cmd_buffer->state.pipeline->prog_data[stage];

   /* If we don't actually have any push constants, bail. */
   if (data == NULL || prog_data == NULL || prog_data->nr_params == 0)
      return (struct anv_state) { .offset = 0 };

   struct anv_state state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                         prog_data->nr_params * sizeof(float),
                                         32 /* bottom 5 bits MBZ */);

   /* Walk through the param array and fill the buffer with data */
   uint32_t *u32_map = state.map;
   for (unsigned i = 0; i < prog_data->nr_params; i++) {
      uint32_t offset = (uintptr_t)prog_data->param[i];
      u32_map[i] = *(uint32_t *)((uint8_t *)data + offset);
   }

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(state);

   return state;
}

struct anv_state
anv_cmd_buffer_cs_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_push_constants *data =
      cmd_buffer->state.push_constants[MESA_SHADER_COMPUTE];
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(pipeline);
   const struct brw_stage_prog_data *prog_data = &cs_prog_data->base;

   /* If we don't actually have any push constants, bail. */
   if (cs_prog_data->push.total.size == 0)
      return (struct anv_state) { .offset = 0 };

   const unsigned push_constant_alignment =
      cmd_buffer->device->info.gen < 8 ? 32 : 64;
   const unsigned aligned_total_push_constants_size =
      ALIGN(cs_prog_data->push.total.size, push_constant_alignment);
   struct anv_state state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                         aligned_total_push_constants_size,
                                         push_constant_alignment);

   /* Walk through the param array and fill the buffer with data */
   uint32_t *u32_map = state.map;

   if (cs_prog_data->push.cross_thread.size > 0) {
      assert(cs_prog_data->thread_local_id_index < 0 ||
             cs_prog_data->thread_local_id_index >=
                cs_prog_data->push.cross_thread.dwords);
      for (unsigned i = 0;
           i < cs_prog_data->push.cross_thread.dwords;
           i++) {
         uint32_t offset = (uintptr_t)prog_data->param[i];
         u32_map[i] = *(uint32_t *)((uint8_t *)data + offset);
      }
   }

   if (cs_prog_data->push.per_thread.size > 0) {
      for (unsigned t = 0; t < cs_prog_data->threads; t++) {
         unsigned dst =
            8 * (cs_prog_data->push.per_thread.regs * t +
                 cs_prog_data->push.cross_thread.regs);
         unsigned src = cs_prog_data->push.cross_thread.dwords;
         for ( ; src < prog_data->nr_params; src++, dst++) {
            if (src != cs_prog_data->thread_local_id_index) {
               uint32_t offset = (uintptr_t)prog_data->param[src];
               u32_map[dst] = *(uint32_t *)((uint8_t *)data + offset);
            } else {
               u32_map[dst] = t * cs_prog_data->simd_size;
            }
         }
      }
   }

   if (!cmd_buffer->device->info.has_llc)
      anv_state_clflush(state);

   return state;
}

void anv_CmdPushConstants(
    VkCommandBuffer                             commandBuffer,
    VkPipelineLayout                            layout,
    VkShaderStageFlags                          stageFlags,
    uint32_t                                    offset,
    uint32_t                                    size,
    const void*                                 pValues)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   anv_foreach_stage(stage, stageFlags) {
      anv_cmd_buffer_ensure_push_constant_field(cmd_buffer, stage, client_data);

      memcpy(cmd_buffer->state.push_constants[stage]->client_data + offset,
             pValues, size);
   }

   cmd_buffer->state.push_constants_dirty |= stageFlags;
}

void anv_CmdExecuteCommands(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCmdBuffers)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, primary, commandBuffer);

   assert(primary->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      ANV_FROM_HANDLE(anv_cmd_buffer, secondary, pCmdBuffers[i]);

      assert(secondary->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);

      anv_cmd_buffer_add_secondary(primary, secondary);
   }
}

VkResult anv_CreateCommandPool(
    VkDevice                                    _device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCmdPool)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_cmd_pool *pool;

   pool = anv_alloc2(&device->alloc, pAllocator, sizeof(*pool), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->alloc;

   list_inithead(&pool->cmd_buffers);

   *pCmdPool = anv_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

void anv_DestroyCommandPool(
    VkDevice                                    _device,
    VkCommandPool                               commandPool,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_cmd_pool, pool, commandPool);

   list_for_each_entry_safe(struct anv_cmd_buffer, cmd_buffer,
                            &pool->cmd_buffers, pool_link) {
      anv_cmd_buffer_destroy(cmd_buffer);
   }

   anv_free2(&device->alloc, pAllocator, pool);
}

VkResult anv_ResetCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolResetFlags                     flags)
{
   ANV_FROM_HANDLE(anv_cmd_pool, pool, commandPool);

   list_for_each_entry(struct anv_cmd_buffer, cmd_buffer,
                       &pool->cmd_buffers, pool_link) {
      anv_cmd_buffer_reset(cmd_buffer);
   }

   return VK_SUCCESS;
}

/**
 * Return NULL if the current subpass has no depthstencil attachment.
 */
const struct anv_image_view *
anv_cmd_buffer_get_depth_stencil_view(const struct anv_cmd_buffer *cmd_buffer)
{
   const struct anv_subpass *subpass = cmd_buffer->state.subpass;
   const struct anv_framebuffer *fb = cmd_buffer->state.framebuffer;

   if (subpass->depth_stencil_attachment == VK_ATTACHMENT_UNUSED)
      return NULL;

   const struct anv_image_view *iview =
      fb->attachments[subpass->depth_stencil_attachment];

   assert(iview->aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                                VK_IMAGE_ASPECT_STENCIL_BIT));

   return iview;
}
