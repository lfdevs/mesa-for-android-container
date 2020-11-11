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
#include "vk_util.h"

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
   .stencil_op = {
      .front = {
         .fail_op = 0,
         .pass_op = 0,
         .depth_fail_op = 0,
         .compare_op = 0,
      },
      .back = {
         .fail_op = 0,
         .pass_op = 0,
         .depth_fail_op = 0,
         .compare_op = 0,
      },
   },
   .line_stipple = {
      .factor = 0u,
      .pattern = 0u,
   },
   .cull_mode = 0,
   .front_face = 0,
   .primitive_topology = 0,
   .depth_test_enable = 0,
   .depth_write_enable = 0,
   .depth_compare_op = 0,
   .depth_bounds_test_enable = 0,
   .stencil_test_enable = 0,
   .dyn_vbo_stride = 0,
   .dyn_vbo_size = 0,
};

/**
 * Copy the dynamic state from src to dest based on the copy_mask.
 *
 * Avoid copying states that have not changed, except for VIEWPORT, SCISSOR and
 * BLEND_CONSTANTS (always copy them if they are in the copy_mask).
 *
 * Returns a mask of the states which changed.
 */
anv_cmd_dirty_mask_t
anv_dynamic_state_copy(struct anv_dynamic_state *dest,
                       const struct anv_dynamic_state *src,
                       anv_cmd_dirty_mask_t copy_mask)
{
   anv_cmd_dirty_mask_t changed = 0;

   if (copy_mask & ANV_CMD_DIRTY_DYNAMIC_VIEWPORT) {
      dest->viewport.count = src->viewport.count;
      typed_memcpy(dest->viewport.viewports, src->viewport.viewports,
                   src->viewport.count);
      changed |= ANV_CMD_DIRTY_DYNAMIC_VIEWPORT;
   }

   if (copy_mask & ANV_CMD_DIRTY_DYNAMIC_SCISSOR) {
      dest->scissor.count = src->scissor.count;
      typed_memcpy(dest->scissor.scissors, src->scissor.scissors,
                   src->scissor.count);
      changed |= ANV_CMD_DIRTY_DYNAMIC_SCISSOR;
   }

   if (copy_mask & ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS) {
      typed_memcpy(dest->blend_constants, src->blend_constants, 4);
      changed |= ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS;
   }

#define ANV_CMP_COPY(field, flag)                                 \
   if (copy_mask & flag) {                                        \
      if (dest->field != src->field) {                            \
         dest->field = src->field;                                \
         changed |= flag;                                         \
      }                                                           \
   }

   ANV_CMP_COPY(line_width, ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH);

   ANV_CMP_COPY(depth_bias.bias, ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS);
   ANV_CMP_COPY(depth_bias.clamp, ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS);
   ANV_CMP_COPY(depth_bias.slope, ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS);

   ANV_CMP_COPY(depth_bounds.min, ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS);
   ANV_CMP_COPY(depth_bounds.max, ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS);

   ANV_CMP_COPY(stencil_compare_mask.front, ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK);
   ANV_CMP_COPY(stencil_compare_mask.back, ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK);

   ANV_CMP_COPY(stencil_write_mask.front, ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK);
   ANV_CMP_COPY(stencil_write_mask.back, ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK);

   ANV_CMP_COPY(stencil_reference.front, ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE);
   ANV_CMP_COPY(stencil_reference.back, ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE);

   ANV_CMP_COPY(line_stipple.factor, ANV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE);
   ANV_CMP_COPY(line_stipple.pattern, ANV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE);

   ANV_CMP_COPY(cull_mode, ANV_CMD_DIRTY_DYNAMIC_CULL_MODE);
   ANV_CMP_COPY(front_face, ANV_CMD_DIRTY_DYNAMIC_FRONT_FACE);
   ANV_CMP_COPY(primitive_topology, ANV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY);
   ANV_CMP_COPY(depth_test_enable, ANV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE);
   ANV_CMP_COPY(depth_write_enable, ANV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE);
   ANV_CMP_COPY(depth_compare_op, ANV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP);
   ANV_CMP_COPY(depth_bounds_test_enable, ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE);
   ANV_CMP_COPY(stencil_test_enable, ANV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE);

   if (copy_mask & VK_DYNAMIC_STATE_STENCIL_OP_EXT) {
      ANV_CMP_COPY(stencil_op.front.fail_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
      ANV_CMP_COPY(stencil_op.front.pass_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
      ANV_CMP_COPY(stencil_op.front.depth_fail_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
      ANV_CMP_COPY(stencil_op.front.compare_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
      ANV_CMP_COPY(stencil_op.back.fail_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
      ANV_CMP_COPY(stencil_op.back.pass_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
      ANV_CMP_COPY(stencil_op.back.depth_fail_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
      ANV_CMP_COPY(stencil_op.back.compare_op, ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP);
   }

   ANV_CMP_COPY(dyn_vbo_stride, ANV_CMD_DIRTY_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE);
   ANV_CMP_COPY(dyn_vbo_size, ANV_CMD_DIRTY_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE);

#undef ANV_CMP_COPY

   return changed;
}

static void
anv_cmd_state_init(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_cmd_state *state = &cmd_buffer->state;

   memset(state, 0, sizeof(*state));

   state->current_pipeline = UINT32_MAX;
   state->restart_index = UINT32_MAX;
   state->gfx.dynamic = default_dynamic_state;
}

static void
anv_cmd_pipeline_state_finish(struct anv_cmd_buffer *cmd_buffer,
                              struct anv_cmd_pipeline_state *pipe_state)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(pipe_state->push_descriptors); i++) {
      if (pipe_state->push_descriptors[i]) {
         anv_descriptor_set_layout_unref(cmd_buffer->device,
             pipe_state->push_descriptors[i]->set.layout);
         vk_free(&cmd_buffer->pool->alloc, pipe_state->push_descriptors[i]);
      }
   }
}

static void
anv_cmd_state_finish(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_cmd_state *state = &cmd_buffer->state;

   anv_cmd_pipeline_state_finish(cmd_buffer, &state->gfx.base);
   anv_cmd_pipeline_state_finish(cmd_buffer, &state->compute.base);

   vk_free(&cmd_buffer->pool->alloc, state->attachments);
}

static void
anv_cmd_state_reset(struct anv_cmd_buffer *cmd_buffer)
{
   anv_cmd_state_finish(cmd_buffer);
   anv_cmd_state_init(cmd_buffer);
}

static VkResult anv_create_cmd_buffer(
    struct anv_device *                         device,
    struct anv_cmd_pool *                       pool,
    VkCommandBufferLevel                        level,
    VkCommandBuffer*                            pCommandBuffer)
{
   struct anv_cmd_buffer *cmd_buffer;
   VkResult result;

   cmd_buffer = vk_alloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &cmd_buffer->base,
                       VK_OBJECT_TYPE_COMMAND_BUFFER);

   cmd_buffer->batch.status = VK_SUCCESS;

   cmd_buffer->device = device;
   cmd_buffer->pool = pool;
   cmd_buffer->level = level;

   result = anv_cmd_buffer_init_batch_bo_chain(cmd_buffer);
   if (result != VK_SUCCESS)
      goto fail;

   anv_state_stream_init(&cmd_buffer->surface_state_stream,
                         &device->surface_state_pool, 4096);
   anv_state_stream_init(&cmd_buffer->dynamic_state_stream,
                         &device->dynamic_state_pool, 16384);

   anv_cmd_state_init(cmd_buffer);

   list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);

   *pCommandBuffer = anv_cmd_buffer_to_handle(cmd_buffer);

   return VK_SUCCESS;

 fail:
   vk_free(&cmd_buffer->pool->alloc, cmd_buffer);

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

   if (result != VK_SUCCESS) {
      anv_FreeCommandBuffers(_device, pAllocateInfo->commandPool,
                             i, pCommandBuffers);
      for (i = 0; i < pAllocateInfo->commandBufferCount; i++)
         pCommandBuffers[i] = VK_NULL_HANDLE;
   }

   return result;
}

static void
anv_cmd_buffer_destroy(struct anv_cmd_buffer *cmd_buffer)
{
   list_del(&cmd_buffer->pool_link);

   anv_cmd_buffer_fini_batch_bo_chain(cmd_buffer);

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);

   anv_cmd_state_finish(cmd_buffer);

   vk_object_base_finish(&cmd_buffer->base);
   vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

void anv_FreeCommandBuffers(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    uint32_t                                    commandBufferCount,
    const VkCommandBuffer*                      pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

      if (!cmd_buffer)
         continue;

      anv_cmd_buffer_destroy(cmd_buffer);
   }
}

VkResult
anv_cmd_buffer_reset(struct anv_cmd_buffer *cmd_buffer)
{
   cmd_buffer->usage_flags = 0;
   cmd_buffer->perf_query_pool = NULL;
   anv_cmd_buffer_reset_batch_bo_chain(cmd_buffer);
   anv_cmd_state_reset(cmd_buffer);

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_init(&cmd_buffer->surface_state_stream,
                         &cmd_buffer->device->surface_state_pool, 4096);

   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);
   anv_state_stream_init(&cmd_buffer->dynamic_state_stream,
                         &cmd_buffer->device->dynamic_state_pool, 16384);
   return VK_SUCCESS;
}

VkResult anv_ResetCommandBuffer(
    VkCommandBuffer                             commandBuffer,
    VkCommandBufferResetFlags                   flags)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   return anv_cmd_buffer_reset(cmd_buffer);
}

#define anv_genX_call(devinfo, func, ...)          \
   switch ((devinfo)->gen) {                       \
   case 7:                                         \
      if ((devinfo)->is_haswell) {                 \
         gen75_##func(__VA_ARGS__);                \
      } else {                                     \
         gen7_##func(__VA_ARGS__);                 \
      }                                            \
      break;                                       \
   case 8:                                         \
      gen8_##func(__VA_ARGS__);                    \
      break;                                       \
   case 9:                                         \
      gen9_##func(__VA_ARGS__);                    \
      break;                                       \
   case 11:                                        \
      gen11_##func(__VA_ARGS__);                   \
      break;                                       \
   case 12:                                        \
      gen12_##func(__VA_ARGS__);                   \
      break;                                       \
   default:                                        \
      assert(!"Unknown hardware generation");      \
   }

void
anv_cmd_buffer_emit_state_base_address(struct anv_cmd_buffer *cmd_buffer)
{
   anv_genX_call(&cmd_buffer->device->info,
                 cmd_buffer_emit_state_base_address,
                 cmd_buffer);
}

void
anv_cmd_buffer_mark_image_written(struct anv_cmd_buffer *cmd_buffer,
                                  const struct anv_image *image,
                                  VkImageAspectFlagBits aspect,
                                  enum isl_aux_usage aux_usage,
                                  uint32_t level,
                                  uint32_t base_layer,
                                  uint32_t layer_count)
{
   anv_genX_call(&cmd_buffer->device->info,
                 cmd_buffer_mark_image_written,
                 cmd_buffer, image, aspect, aux_usage,
                 level, base_layer, layer_count);
}

void
anv_cmd_emit_conditional_render_predicate(struct anv_cmd_buffer *cmd_buffer)
{
   anv_genX_call(&cmd_buffer->device->info,
                 cmd_emit_conditional_render_predicate,
                 cmd_buffer);
}

static bool
mem_update(void *dst, const void *src, size_t size)
{
   if (memcmp(dst, src, size) == 0)
      return false;

   memcpy(dst, src, size);
   return true;
}

static void
set_dirty_for_bind_map(struct anv_cmd_buffer *cmd_buffer,
                       gl_shader_stage stage,
                       const struct anv_pipeline_bind_map *map)
{
   if (mem_update(cmd_buffer->state.surface_sha1s[stage],
                  map->surface_sha1, sizeof(map->surface_sha1)))
      cmd_buffer->state.descriptors_dirty |= mesa_to_vk_shader_stage(stage);

   if (mem_update(cmd_buffer->state.sampler_sha1s[stage],
                  map->sampler_sha1, sizeof(map->sampler_sha1)))
      cmd_buffer->state.descriptors_dirty |= mesa_to_vk_shader_stage(stage);

   if (mem_update(cmd_buffer->state.push_sha1s[stage],
                  map->push_sha1, sizeof(map->push_sha1)))
      cmd_buffer->state.push_constants_dirty |= mesa_to_vk_shader_stage(stage);
}

void anv_CmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  _pipeline)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_pipeline, pipeline, _pipeline);

   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_COMPUTE: {
      struct anv_compute_pipeline *compute_pipeline =
         anv_pipeline_to_compute(pipeline);
      if (cmd_buffer->state.compute.pipeline == compute_pipeline)
         return;

      cmd_buffer->state.compute.pipeline = compute_pipeline;
      cmd_buffer->state.compute.pipeline_dirty = true;
      set_dirty_for_bind_map(cmd_buffer, MESA_SHADER_COMPUTE,
                             &compute_pipeline->cs->bind_map);
      break;
   }

   case VK_PIPELINE_BIND_POINT_GRAPHICS: {
      struct anv_graphics_pipeline *gfx_pipeline =
         anv_pipeline_to_graphics(pipeline);
      if (cmd_buffer->state.gfx.pipeline == gfx_pipeline)
         return;

      cmd_buffer->state.gfx.pipeline = gfx_pipeline;
      cmd_buffer->state.gfx.vb_dirty |= gfx_pipeline->vb_used;
      cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_PIPELINE;

      anv_foreach_stage(stage, gfx_pipeline->active_stages) {
         set_dirty_for_bind_map(cmd_buffer, stage,
                                &gfx_pipeline->shaders[stage]->bind_map);
      }

      /* Apply the dynamic state from the pipeline */
      cmd_buffer->state.gfx.dirty |=
         anv_dynamic_state_copy(&cmd_buffer->state.gfx.dynamic,
                                &gfx_pipeline->dynamic_state,
                                gfx_pipeline->dynamic_state_mask);
      break;
   }

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
   if (cmd_buffer->state.gfx.dynamic.viewport.count < total_count)
      cmd_buffer->state.gfx.dynamic.viewport.count = total_count;

   memcpy(cmd_buffer->state.gfx.dynamic.viewport.viewports + firstViewport,
          pViewports, viewportCount * sizeof(*pViewports));

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_VIEWPORT;
}

void anv_CmdSetViewportWithCountEXT(
   VkCommandBuffer                              commandBuffer,
   uint32_t                                     viewportCount,
   const VkViewport*                            pViewports)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.viewport.count = viewportCount;

   memcpy(cmd_buffer->state.gfx.dynamic.viewport.viewports,
          pViewports, viewportCount * sizeof(*pViewports));

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_VIEWPORT;
}

void anv_CmdSetScissor(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   const uint32_t total_count = firstScissor + scissorCount;
   if (cmd_buffer->state.gfx.dynamic.scissor.count < total_count)
      cmd_buffer->state.gfx.dynamic.scissor.count = total_count;

   memcpy(cmd_buffer->state.gfx.dynamic.scissor.scissors + firstScissor,
          pScissors, scissorCount * sizeof(*pScissors));

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_SCISSOR;
}

void anv_CmdSetScissorWithCountEXT(
   VkCommandBuffer                              commandBuffer,
   uint32_t                                     scissorCount,
   const VkRect2D*                              pScissors)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.scissor.count = scissorCount;

   memcpy(cmd_buffer->state.gfx.dynamic.scissor.scissors,
          pScissors, scissorCount * sizeof(*pScissors));

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_SCISSOR;
}

void anv_CmdSetPrimitiveTopologyEXT(
   VkCommandBuffer                              commandBuffer,
   VkPrimitiveTopology                          primitiveTopology)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.primitive_topology = primitiveTopology;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_PRIMITIVE_TOPOLOGY;
}

void anv_CmdSetLineWidth(
    VkCommandBuffer                             commandBuffer,
    float                                       lineWidth)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.line_width = lineWidth;
   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH;
}

void anv_CmdSetDepthBias(
    VkCommandBuffer                             commandBuffer,
    float                                       depthBiasConstantFactor,
    float                                       depthBiasClamp,
    float                                       depthBiasSlopeFactor)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.depth_bias.bias = depthBiasConstantFactor;
   cmd_buffer->state.gfx.dynamic.depth_bias.clamp = depthBiasClamp;
   cmd_buffer->state.gfx.dynamic.depth_bias.slope = depthBiasSlopeFactor;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS;
}

void anv_CmdSetBlendConstants(
    VkCommandBuffer                             commandBuffer,
    const float                                 blendConstants[4])
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   memcpy(cmd_buffer->state.gfx.dynamic.blend_constants,
          blendConstants, sizeof(float) * 4);

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS;
}

void anv_CmdSetDepthBounds(
    VkCommandBuffer                             commandBuffer,
    float                                       minDepthBounds,
    float                                       maxDepthBounds)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.depth_bounds.min = minDepthBounds;
   cmd_buffer->state.gfx.dynamic.depth_bounds.max = maxDepthBounds;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS;
}

void anv_CmdSetStencilCompareMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    compareMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.gfx.dynamic.stencil_compare_mask.front = compareMask;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.gfx.dynamic.stencil_compare_mask.back = compareMask;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK;
}

void anv_CmdSetStencilWriteMask(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    writeMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.gfx.dynamic.stencil_write_mask.front = writeMask;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.gfx.dynamic.stencil_write_mask.back = writeMask;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK;
}

void anv_CmdSetStencilReference(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    uint32_t                                    reference)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.gfx.dynamic.stencil_reference.front = reference;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.gfx.dynamic.stencil_reference.back = reference;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE;
}

void anv_CmdSetLineStippleEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    lineStippleFactor,
    uint16_t                                    lineStipplePattern)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.line_stipple.factor = lineStippleFactor;
   cmd_buffer->state.gfx.dynamic.line_stipple.pattern = lineStipplePattern;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_LINE_STIPPLE;
}

void anv_CmdSetCullModeEXT(
   VkCommandBuffer                              commandBuffer,
   VkCullModeFlags                              cullMode)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.cull_mode = cullMode;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_CULL_MODE;
}

void anv_CmdSetFrontFaceEXT(
   VkCommandBuffer                              commandBuffer,
   VkFrontFace                                  frontFace)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.front_face = frontFace;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_FRONT_FACE;
}

void anv_CmdSetDepthTestEnableEXT(
   VkCommandBuffer                              commandBuffer,
   VkBool32                                     depthTestEnable)

{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.depth_test_enable = depthTestEnable;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_TEST_ENABLE;
}

void anv_CmdSetDepthWriteEnableEXT(
   VkCommandBuffer                              commandBuffer,
   VkBool32                                     depthWriteEnable)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.depth_write_enable = depthWriteEnable;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_WRITE_ENABLE;
}

void anv_CmdSetDepthCompareOpEXT(
   VkCommandBuffer                              commandBuffer,
   VkCompareOp                                  depthCompareOp)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.depth_compare_op = depthCompareOp;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_COMPARE_OP;
}

void anv_CmdSetDepthBoundsTestEnableEXT(
   VkCommandBuffer                              commandBuffer,
   VkBool32                                     depthBoundsTestEnable)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.depth_bounds_test_enable = depthBoundsTestEnable;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE;
}

void anv_CmdSetStencilTestEnableEXT(
   VkCommandBuffer                              commandBuffer,
   VkBool32                                     stencilTestEnable)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.gfx.dynamic.stencil_test_enable = stencilTestEnable;

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_TEST_ENABLE;
}

void anv_CmdSetStencilOpEXT(
   VkCommandBuffer                              commandBuffer,
   VkStencilFaceFlags                           faceMask,
   VkStencilOp                                  failOp,
   VkStencilOp                                  passOp,
   VkStencilOp                                  depthFailOp,
   VkCompareOp                                  compareOp)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
      cmd_buffer->state.gfx.dynamic.stencil_op.front.fail_op = failOp;
      cmd_buffer->state.gfx.dynamic.stencil_op.front.pass_op = passOp;
      cmd_buffer->state.gfx.dynamic.stencil_op.front.depth_fail_op = depthFailOp;
      cmd_buffer->state.gfx.dynamic.stencil_op.front.compare_op = compareOp;
    }

   if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
      cmd_buffer->state.gfx.dynamic.stencil_op.back.fail_op = failOp;
      cmd_buffer->state.gfx.dynamic.stencil_op.back.pass_op = passOp;
      cmd_buffer->state.gfx.dynamic.stencil_op.back.depth_fail_op = depthFailOp;
      cmd_buffer->state.gfx.dynamic.stencil_op.back.compare_op = compareOp;
   }

   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_DYNAMIC_STENCIL_OP;
}

static void
anv_cmd_buffer_bind_descriptor_set(struct anv_cmd_buffer *cmd_buffer,
                                   VkPipelineBindPoint bind_point,
                                   struct anv_pipeline_layout *layout,
                                   uint32_t set_index,
                                   struct anv_descriptor_set *set,
                                   uint32_t *dynamic_offset_count,
                                   const uint32_t **dynamic_offsets)
{
   struct anv_descriptor_set_layout *set_layout =
      layout->set[set_index].layout;

   VkShaderStageFlags stages = set_layout->shader_stages;
   struct anv_cmd_pipeline_state *pipe_state;

   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      stages &= VK_SHADER_STAGE_ALL_GRAPHICS;
      pipe_state = &cmd_buffer->state.gfx.base;
      break;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      stages &= VK_SHADER_STAGE_COMPUTE_BIT;
      pipe_state = &cmd_buffer->state.compute.base;
      break;

   default:
      unreachable("invalid bind point");
   }

   VkShaderStageFlags dirty_stages = 0;
   if (pipe_state->descriptors[set_index] != set) {
      pipe_state->descriptors[set_index] = set;
      dirty_stages |= stages;
   }

   /* If it's a push descriptor set, we have to flag things as dirty
    * regardless of whether or not the CPU-side data structure changed as we
    * may have edited in-place.
    */
   if (set->pool == NULL)
      dirty_stages |= stages;

   if (dynamic_offsets) {
      if (set_layout->dynamic_offset_count > 0) {
         struct anv_push_constants *push = &pipe_state->push_constants;
         uint32_t dynamic_offset_start =
            layout->set[set_index].dynamic_offset_start;
         uint32_t *push_offsets =
            &push->dynamic_offsets[dynamic_offset_start];

         /* Assert that everything is in range */
         assert(set_layout->dynamic_offset_count <= *dynamic_offset_count);
         assert(dynamic_offset_start + set_layout->dynamic_offset_count <=
                ARRAY_SIZE(push->dynamic_offsets));

         for (uint32_t i = 0; i < set_layout->dynamic_offset_count; i++) {
            if (push_offsets[i] != (*dynamic_offsets)[i]) {
               push_offsets[i] = (*dynamic_offsets)[i];
               /* dynamic_offset_stages[] elements could contain blanket
                * values like VK_SHADER_STAGE_ALL, so limit this to the
                * binding point's bits.
                */
               dirty_stages |= set_layout->dynamic_offset_stages[i] & stages;
            }
         }

         *dynamic_offsets += set_layout->dynamic_offset_count;
         *dynamic_offset_count -= set_layout->dynamic_offset_count;
      }
   }

   cmd_buffer->state.descriptors_dirty |= dirty_stages;
   cmd_buffer->state.push_constants_dirty |= dirty_stages;
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

   assert(firstSet + descriptorSetCount <= MAX_SETS);

   for (uint32_t i = 0; i < descriptorSetCount; i++) {
      ANV_FROM_HANDLE(anv_descriptor_set, set, pDescriptorSets[i]);
      anv_cmd_buffer_bind_descriptor_set(cmd_buffer, pipelineBindPoint,
                                         layout, firstSet + i, set,
                                         &dynamicOffsetCount,
                                         &pDynamicOffsets);
   }
}

void anv_CmdBindVertexBuffers2EXT(
   VkCommandBuffer                              commandBuffer,
   uint32_t                                     firstBinding,
   uint32_t                                     bindingCount,
   const VkBuffer*                              pBuffers,
   const VkDeviceSize*                          pOffsets,
   const VkDeviceSize*                          pSizes,
   const VkDeviceSize*                          pStrides)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_vertex_binding *vb = cmd_buffer->state.vertex_bindings;

   /* We have to defer setting up vertex buffer since we need the buffer
    * stride from the pipeline. */

   if (pSizes)
      cmd_buffer->state.gfx.dynamic.dyn_vbo_size = true;
   if (pStrides)
      cmd_buffer->state.gfx.dynamic.dyn_vbo_stride = true;

   assert(firstBinding + bindingCount <= MAX_VBS);
   for (uint32_t i = 0; i < bindingCount; i++) {
      vb[firstBinding + i].buffer = anv_buffer_from_handle(pBuffers[i]);
      vb[firstBinding + i].offset = pOffsets[i];
      vb[firstBinding + i].size = pSizes ? pSizes[i] : 0;
      vb[firstBinding + i].stride = pStrides ? pStrides[i] : 0;
      cmd_buffer->state.gfx.vb_dirty |= 1 << (firstBinding + i);
   }
}

void anv_CmdBindVertexBuffers(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets)
{
   return anv_CmdBindVertexBuffers2EXT(commandBuffer, firstBinding,
                                       bindingCount, pBuffers, pOffsets,
                                       NULL, NULL);
}

void anv_CmdBindTransformFeedbackBuffersEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_xfb_binding *xfb = cmd_buffer->state.xfb_bindings;

   /* We have to defer setting up vertex buffer since we need the buffer
    * stride from the pipeline. */

   assert(firstBinding + bindingCount <= MAX_XFB_BUFFERS);
   for (uint32_t i = 0; i < bindingCount; i++) {
      if (pBuffers[i] == VK_NULL_HANDLE) {
         xfb[firstBinding + i].buffer = NULL;
      } else {
         ANV_FROM_HANDLE(anv_buffer, buffer, pBuffers[i]);
         xfb[firstBinding + i].buffer = buffer;
         xfb[firstBinding + i].offset = pOffsets[i];
         xfb[firstBinding + i].size =
            anv_buffer_get_range(buffer, pOffsets[i],
                                 pSizes ? pSizes[i] : VK_WHOLE_SIZE);
      }
   }
}

enum isl_format
anv_isl_format_for_descriptor_type(const struct anv_device *device,
                                   VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return device->physical->compiler->indirect_ubos_use_sampler ?
             ISL_FORMAT_R32G32B32A32_FLOAT : ISL_FORMAT_RAW;

   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return ISL_FORMAT_RAW;

   default:
      unreachable("Invalid descriptor type");
   }
}

struct anv_state
anv_cmd_buffer_emit_dynamic(struct anv_cmd_buffer *cmd_buffer,
                            const void *data, uint32_t size, uint32_t alignment)
{
   struct anv_state state;

   state = anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, size, alignment);
   memcpy(state.map, data, size);

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

   VG(VALGRIND_CHECK_MEM_IS_DEFINED(p, dwords * 4));

   return state;
}

struct anv_state
anv_cmd_buffer_gfx_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_push_constants *data =
      &cmd_buffer->state.gfx.base.push_constants;

   struct anv_state state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                         sizeof(struct anv_push_constants),
                                         32 /* bottom 5 bits MBZ */);
   memcpy(state.map, data, sizeof(struct anv_push_constants));

   return state;
}

struct anv_state
anv_cmd_buffer_cs_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_push_constants *data =
      &cmd_buffer->state.compute.base.push_constants;
   struct anv_compute_pipeline *pipeline = cmd_buffer->state.compute.pipeline;
   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(pipeline);
   const struct anv_push_range *range = &pipeline->cs->bind_map.push_ranges[0];

   const struct anv_cs_parameters cs_params = anv_cs_parameters(pipeline);
   const unsigned total_push_constants_size =
      brw_cs_push_const_total_size(cs_prog_data, cs_params.threads);
   if (total_push_constants_size == 0)
      return (struct anv_state) { .offset = 0 };

   const unsigned push_constant_alignment =
      cmd_buffer->device->info.gen < 8 ? 32 : 64;
   const unsigned aligned_total_push_constants_size =
      ALIGN(total_push_constants_size, push_constant_alignment);
   struct anv_state state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                         aligned_total_push_constants_size,
                                         push_constant_alignment);

   void *dst = state.map;
   const void *src = (char *)data + (range->start * 32);

   if (cs_prog_data->push.cross_thread.size > 0) {
      memcpy(dst, src, cs_prog_data->push.cross_thread.size);
      dst += cs_prog_data->push.cross_thread.size;
      src += cs_prog_data->push.cross_thread.size;
   }

   if (cs_prog_data->push.per_thread.size > 0) {
      for (unsigned t = 0; t < cs_params.threads; t++) {
         memcpy(dst, src, cs_prog_data->push.per_thread.size);

         uint32_t *subgroup_id = dst +
            offsetof(struct anv_push_constants, cs.subgroup_id) -
            (range->start * 32 + cs_prog_data->push.cross_thread.size);
         *subgroup_id = t;

         dst += cs_prog_data->push.per_thread.size;
      }
   }

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

   if (stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.gfx.base;

      memcpy(pipe_state->push_constants.client_data + offset, pValues, size);
   }
   if (stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.compute.base;

      memcpy(pipe_state->push_constants.client_data + offset, pValues, size);
   }

   cmd_buffer->state.push_constants_dirty |= stageFlags;
}

VkResult anv_CreateCommandPool(
    VkDevice                                    _device,
    const VkCommandPoolCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkCommandPool*                              pCmdPool)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_cmd_pool *pool;

   pool = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pool->base, VK_OBJECT_TYPE_COMMAND_POOL);

   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->vk.alloc;

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

   if (!pool)
      return;

   list_for_each_entry_safe(struct anv_cmd_buffer, cmd_buffer,
                            &pool->cmd_buffers, pool_link) {
      anv_cmd_buffer_destroy(cmd_buffer);
   }

   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
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

void anv_TrimCommandPool(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlags                      flags)
{
   /* Nothing for us to do here.  Our pools stay pretty tidy. */
}

/**
 * Return NULL if the current subpass has no depthstencil attachment.
 */
const struct anv_image_view *
anv_cmd_buffer_get_depth_stencil_view(const struct anv_cmd_buffer *cmd_buffer)
{
   const struct anv_subpass *subpass = cmd_buffer->state.subpass;

   if (subpass->depth_stencil_attachment == NULL)
      return NULL;

   const struct anv_image_view *iview =
      cmd_buffer->state.attachments[subpass->depth_stencil_attachment->attachment].image_view;

   assert(iview->aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                                VK_IMAGE_ASPECT_STENCIL_BIT));

   return iview;
}

static struct anv_descriptor_set *
anv_cmd_buffer_push_descriptor_set(struct anv_cmd_buffer *cmd_buffer,
                                   VkPipelineBindPoint bind_point,
                                   struct anv_descriptor_set_layout *layout,
                                   uint32_t _set)
{
   struct anv_cmd_pipeline_state *pipe_state;
   if (bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
      pipe_state = &cmd_buffer->state.compute.base;
   } else {
      assert(bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS);
      pipe_state = &cmd_buffer->state.gfx.base;
   }

   struct anv_push_descriptor_set **push_set =
      &pipe_state->push_descriptors[_set];

   if (*push_set == NULL) {
      *push_set = vk_zalloc(&cmd_buffer->pool->alloc,
                            sizeof(struct anv_push_descriptor_set), 8,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (*push_set == NULL) {
         anv_batch_set_error(&cmd_buffer->batch, VK_ERROR_OUT_OF_HOST_MEMORY);
         return NULL;
      }
   }

   struct anv_descriptor_set *set = &(*push_set)->set;

   if (set->layout != layout) {
      if (set->layout)
         anv_descriptor_set_layout_unref(cmd_buffer->device, set->layout);
      anv_descriptor_set_layout_ref(layout);
      set->layout = layout;
   }
   set->size = anv_descriptor_set_layout_size(layout, 0);
   set->buffer_view_count = layout->buffer_view_count;
   set->descriptor_count = layout->descriptor_count;
   set->buffer_views = (*push_set)->buffer_views;

   if (layout->descriptor_buffer_size &&
       ((*push_set)->set_used_on_gpu ||
        set->desc_mem.alloc_size < layout->descriptor_buffer_size)) {
      /* The previous buffer is either actively used by some GPU command (so
       * we can't modify it) or is too small.  Allocate a new one.
       */
      struct anv_state desc_mem =
         anv_state_stream_alloc(&cmd_buffer->dynamic_state_stream,
                                layout->descriptor_buffer_size, 32);
      if (set->desc_mem.alloc_size) {
         /* TODO: Do we really need to copy all the time? */
         memcpy(desc_mem.map, set->desc_mem.map,
                MIN2(desc_mem.alloc_size, set->desc_mem.alloc_size));
      }
      set->desc_mem = desc_mem;

      struct anv_address addr = {
         .bo = cmd_buffer->dynamic_state_stream.state_pool->block_pool.bo,
         .offset = set->desc_mem.offset,
      };

      enum isl_format format =
         anv_isl_format_for_descriptor_type(cmd_buffer->device,
                                            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

      const struct isl_device *isl_dev = &cmd_buffer->device->isl_dev;
      set->desc_surface_state =
         anv_state_stream_alloc(&cmd_buffer->surface_state_stream,
                                isl_dev->ss.size, isl_dev->ss.align);
      anv_fill_buffer_surface_state(cmd_buffer->device,
                                    set->desc_surface_state, format,
                                    ISL_SURF_USAGE_CONSTANT_BUFFER_BIT,
                                    addr, layout->descriptor_buffer_size, 1);
   }

   return set;
}

void anv_CmdPushDescriptorSetKHR(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout _layout,
    uint32_t _set,
    uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_pipeline_layout, layout, _layout);

   assert(_set < MAX_SETS);

   struct anv_descriptor_set_layout *set_layout = layout->set[_set].layout;

   struct anv_descriptor_set *set =
      anv_cmd_buffer_push_descriptor_set(cmd_buffer, pipelineBindPoint,
                                         set_layout, _set);
   if (!set)
      return;

   /* Go through the user supplied descriptors. */
   for (uint32_t i = 0; i < descriptorWriteCount; i++) {
      const VkWriteDescriptorSet *write = &pDescriptorWrites[i];

      switch (write->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            anv_descriptor_set_write_image_view(cmd_buffer->device, set,
                                                write->pImageInfo + j,
                                                write->descriptorType,
                                                write->dstBinding,
                                                write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            ANV_FROM_HANDLE(anv_buffer_view, bview,
                            write->pTexelBufferView[j]);

            anv_descriptor_set_write_buffer_view(cmd_buffer->device, set,
                                                 write->descriptorType,
                                                 bview,
                                                 write->dstBinding,
                                                 write->dstArrayElement + j);
         }
         break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            ANV_FROM_HANDLE(anv_buffer, buffer, write->pBufferInfo[j].buffer);

            anv_descriptor_set_write_buffer(cmd_buffer->device, set,
                                            &cmd_buffer->surface_state_stream,
                                            write->descriptorType,
                                            buffer,
                                            write->dstBinding,
                                            write->dstArrayElement + j,
                                            write->pBufferInfo[j].offset,
                                            write->pBufferInfo[j].range);
         }
         break;

      default:
         break;
      }
   }

   anv_cmd_buffer_bind_descriptor_set(cmd_buffer, pipelineBindPoint,
                                      layout, _set, set, NULL, NULL);
}

void anv_CmdPushDescriptorSetWithTemplateKHR(
    VkCommandBuffer                             commandBuffer,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    VkPipelineLayout                            _layout,
    uint32_t                                    _set,
    const void*                                 pData)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_descriptor_update_template, template,
                   descriptorUpdateTemplate);
   ANV_FROM_HANDLE(anv_pipeline_layout, layout, _layout);

   assert(_set < MAX_PUSH_DESCRIPTORS);

   struct anv_descriptor_set_layout *set_layout = layout->set[_set].layout;

   struct anv_descriptor_set *set =
      anv_cmd_buffer_push_descriptor_set(cmd_buffer, template->bind_point,
                                         set_layout, _set);
   if (!set)
      return;

   anv_descriptor_set_write_template(cmd_buffer->device, set,
                                     &cmd_buffer->surface_state_stream,
                                     template,
                                     pData);

   anv_cmd_buffer_bind_descriptor_set(cmd_buffer, template->bind_point,
                                      layout, _set, set, NULL, NULL);
}

void anv_CmdSetDeviceMask(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    deviceMask)
{
   /* No-op */
}
