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
#include "anv_measure.h"

#include "vk_util.h"

/** \file anv_cmd_buffer.c
 *
 * This file contains all of the stuff for emitting commands into a command
 * buffer.  This includes implementations of most of the vkCmd*
 * entrypoints.  This file is concerned entirely with state emission and
 * not with the command buffer data structure itself.  As far as this file
 * is concerned, most of anv_cmd_buffer is magic.
 */

static void
anv_cmd_state_init(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_cmd_state *state = &cmd_buffer->state;

   memset(state, 0, sizeof(*state));

   state->current_pipeline = UINT32_MAX;
   state->gfx.restart_index = UINT32_MAX;
   state->gfx.object_preemption = true;
   state->gfx.dirty = 0;
}

static void
anv_cmd_pipeline_state_finish(struct anv_cmd_buffer *cmd_buffer,
                              struct anv_cmd_pipeline_state *pipe_state)
{
   if (pipe_state->push_descriptor) {
      anv_descriptor_set_layout_unref(cmd_buffer->device,
                                      pipe_state->push_descriptor->set.layout);
      vk_free(&cmd_buffer->vk.pool->alloc, pipe_state->push_descriptor);
   }
}

static void
anv_cmd_state_finish(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_cmd_state *state = &cmd_buffer->state;

   anv_cmd_pipeline_state_finish(cmd_buffer, &state->gfx.base);
   anv_cmd_pipeline_state_finish(cmd_buffer, &state->compute.base);
}

static void
anv_cmd_state_reset(struct anv_cmd_buffer *cmd_buffer)
{
   anv_cmd_state_finish(cmd_buffer);
   anv_cmd_state_init(cmd_buffer);
}

static VkResult
anv_create_cmd_buffer(struct vk_command_pool *pool,
                      struct vk_command_buffer **cmd_buffer_out)
{
   struct anv_device *device =
      container_of(pool->base.device, struct anv_device, vk);
   struct anv_cmd_buffer *cmd_buffer;
   VkResult result;

   cmd_buffer = vk_alloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(pool, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = vk_command_buffer_init(pool, &cmd_buffer->vk,
                                   &anv_cmd_buffer_ops, 0);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   cmd_buffer->vk.dynamic_graphics_state.ms.sample_locations =
      &cmd_buffer->state.gfx.sample_locations;
   cmd_buffer->vk.dynamic_graphics_state.vi =
      &cmd_buffer->state.gfx.vertex_input;

   cmd_buffer->batch.status = VK_SUCCESS;
   cmd_buffer->generation_batch.status = VK_SUCCESS;

   cmd_buffer->device = device;

   assert(pool->queue_family_index < device->physical->queue.family_count);
   cmd_buffer->queue_family =
      &device->physical->queue.families[pool->queue_family_index];

   result = anv_cmd_buffer_init_batch_bo_chain(cmd_buffer);
   if (result != VK_SUCCESS)
      goto fail_vk;

   anv_state_stream_init(&cmd_buffer->surface_state_stream,
                         &device->internal_surface_state_pool, 4096);
   anv_state_stream_init(&cmd_buffer->dynamic_state_stream,
                         &device->dynamic_state_pool, 16384);
   anv_state_stream_init(&cmd_buffer->general_state_stream,
                         &device->general_state_pool, 16384);

   int success = u_vector_init_pow2(&cmd_buffer->dynamic_bos, 8,
                                    sizeof(struct anv_bo *));
   if (!success)
      goto fail_batch_bo;

   cmd_buffer->self_mod_locations = NULL;

   cmd_buffer->generation_jump_addr = ANV_NULL_ADDRESS;
   cmd_buffer->generation_return_addr = ANV_NULL_ADDRESS;
   cmd_buffer->generation_bt_state = ANV_STATE_NULL;

   anv_cmd_state_init(cmd_buffer);

   anv_measure_init(cmd_buffer);

   u_trace_init(&cmd_buffer->trace, &device->ds.trace_context);

   *cmd_buffer_out = &cmd_buffer->vk;

   return VK_SUCCESS;

 fail_batch_bo:
   anv_cmd_buffer_fini_batch_bo_chain(cmd_buffer);
 fail_vk:
   vk_command_buffer_finish(&cmd_buffer->vk);
 fail_alloc:
   vk_free2(&device->vk.alloc, &pool->alloc, cmd_buffer);

   return result;
}

static void
anv_cmd_buffer_destroy(struct vk_command_buffer *vk_cmd_buffer)
{
   struct anv_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct anv_cmd_buffer, vk);

   u_trace_fini(&cmd_buffer->trace);

   anv_measure_destroy(cmd_buffer);

   anv_cmd_buffer_fini_batch_bo_chain(cmd_buffer);

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);
   anv_state_stream_finish(&cmd_buffer->general_state_stream);

   while (u_vector_length(&cmd_buffer->dynamic_bos) > 0) {
      struct anv_bo **bo = u_vector_remove(&cmd_buffer->dynamic_bos);
      anv_device_release_bo(cmd_buffer->device, *bo);
   }
   u_vector_finish(&cmd_buffer->dynamic_bos);

   anv_cmd_state_finish(cmd_buffer);

   vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer->self_mod_locations);

   vk_command_buffer_finish(&cmd_buffer->vk);
   vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer);
}

void
anv_cmd_buffer_reset(struct vk_command_buffer *vk_cmd_buffer,
                     UNUSED VkCommandBufferResetFlags flags)
{
   struct anv_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct anv_cmd_buffer, vk);

   vk_command_buffer_reset(&cmd_buffer->vk);

   cmd_buffer->usage_flags = 0;
   cmd_buffer->perf_query_pool = NULL;
   anv_cmd_buffer_reset_batch_bo_chain(cmd_buffer);
   anv_cmd_state_reset(cmd_buffer);

   cmd_buffer->generation_jump_addr = ANV_NULL_ADDRESS;
   cmd_buffer->generation_return_addr = ANV_NULL_ADDRESS;
   cmd_buffer->generation_bt_state = ANV_STATE_NULL;

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_init(&cmd_buffer->surface_state_stream,
                         &cmd_buffer->device->internal_surface_state_pool, 4096);

   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);
   anv_state_stream_init(&cmd_buffer->dynamic_state_stream,
                         &cmd_buffer->device->dynamic_state_pool, 16384);

   anv_state_stream_finish(&cmd_buffer->general_state_stream);
   anv_state_stream_init(&cmd_buffer->general_state_stream,
                         &cmd_buffer->device->general_state_pool, 16384);

   while (u_vector_length(&cmd_buffer->dynamic_bos) > 0) {
      struct anv_bo **bo = u_vector_remove(&cmd_buffer->dynamic_bos);
      anv_device_release_bo(cmd_buffer->device, *bo);
   }

   anv_measure_reset(cmd_buffer);

   u_trace_fini(&cmd_buffer->trace);
   u_trace_init(&cmd_buffer->trace, &cmd_buffer->device->ds.trace_context);
}

const struct vk_command_buffer_ops anv_cmd_buffer_ops = {
   .create = anv_create_cmd_buffer,
   .reset = anv_cmd_buffer_reset,
   .destroy = anv_cmd_buffer_destroy,
};

void
anv_cmd_buffer_emit_state_base_address(struct anv_cmd_buffer *cmd_buffer)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   anv_genX(devinfo, cmd_buffer_emit_state_base_address)(cmd_buffer);
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
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   anv_genX(devinfo, cmd_buffer_mark_image_written)(cmd_buffer, image,
                                                    aspect, aux_usage,
                                                    level, base_layer,
                                                    layer_count);
}

void
anv_cmd_emit_conditional_render_predicate(struct anv_cmd_buffer *cmd_buffer)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   anv_genX(devinfo, cmd_emit_conditional_render_predicate)(cmd_buffer);
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
   assert(stage < ARRAY_SIZE(cmd_buffer->state.surface_sha1s));
   if (mem_update(cmd_buffer->state.surface_sha1s[stage],
                  map->surface_sha1, sizeof(map->surface_sha1)))
      cmd_buffer->state.descriptors_dirty |= mesa_to_vk_shader_stage(stage);

   assert(stage < ARRAY_SIZE(cmd_buffer->state.sampler_sha1s));
   if (mem_update(cmd_buffer->state.sampler_sha1s[stage],
                  map->sampler_sha1, sizeof(map->sampler_sha1)))
      cmd_buffer->state.descriptors_dirty |= mesa_to_vk_shader_stage(stage);

   assert(stage < ARRAY_SIZE(cmd_buffer->state.push_sha1s));
   if (mem_update(cmd_buffer->state.push_sha1s[stage],
                  map->push_sha1, sizeof(map->push_sha1)))
      cmd_buffer->state.push_constants_dirty |= mesa_to_vk_shader_stage(stage);
}

static void
anv_cmd_buffer_set_ray_query_buffer(struct anv_cmd_buffer *cmd_buffer,
                                    struct anv_cmd_pipeline_state *pipeline_state,
                                    struct anv_pipeline *pipeline,
                                    VkShaderStageFlags stages)
{
   struct anv_device *device = cmd_buffer->device;

   uint64_t ray_shadow_size =
      align64(brw_rt_ray_queries_shadow_stacks_size(device->info,
                                                    pipeline->ray_queries),
              4096);
   if (ray_shadow_size > 0 &&
       (!cmd_buffer->state.ray_query_shadow_bo ||
        cmd_buffer->state.ray_query_shadow_bo->size < ray_shadow_size)) {
      unsigned shadow_size_log2 = MAX2(util_logbase2_ceil(ray_shadow_size), 16);
      unsigned bucket = shadow_size_log2 - 16;
      assert(bucket < ARRAY_SIZE(device->ray_query_shadow_bos));

      struct anv_bo *bo = p_atomic_read(&device->ray_query_shadow_bos[bucket]);
      if (bo == NULL) {
         struct anv_bo *new_bo;
         VkResult result = anv_device_alloc_bo(device, "RT queries shadow",
                                               ray_shadow_size,
                                               0, /* alloc_flags */
                                               0, /* explicit_address */
                                               &new_bo);
         if (result != VK_SUCCESS) {
            anv_batch_set_error(&cmd_buffer->batch, result);
            return;
         }

         bo = p_atomic_cmpxchg(&device->ray_query_shadow_bos[bucket], NULL, new_bo);
         if (bo != NULL) {
            anv_device_release_bo(device, bo);
         } else {
            bo = new_bo;
         }
      }
      cmd_buffer->state.ray_query_shadow_bo = bo;

      /* Add the ray query buffers to the batch list. */
      anv_reloc_list_add_bo(cmd_buffer->batch.relocs,
                            cmd_buffer->batch.alloc,
                            cmd_buffer->state.ray_query_shadow_bo);
   }

   /* Add the HW buffer to the list of BO used. */
   anv_reloc_list_add_bo(cmd_buffer->batch.relocs,
                         cmd_buffer->batch.alloc,
                         device->ray_query_bo);

   /* Fill the push constants & mark them dirty. */
   struct anv_state ray_query_global_state =
      anv_genX(device->info, cmd_buffer_ray_query_globals)(cmd_buffer);

   struct anv_address ray_query_globals_addr =
      anv_state_pool_state_address(&device->dynamic_state_pool,
                                   ray_query_global_state);
   pipeline_state->push_constants.ray_query_globals =
      anv_address_physical(ray_query_globals_addr);
   cmd_buffer->state.push_constants_dirty |= stages;
}

void anv_CmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  _pipeline)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_pipeline, pipeline, _pipeline);
   struct anv_cmd_pipeline_state *state;
   VkShaderStageFlags stages = 0;

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

      state = &cmd_buffer->state.compute.base;
      stages = VK_SHADER_STAGE_COMPUTE_BIT;
      break;
   }

   case VK_PIPELINE_BIND_POINT_GRAPHICS: {
      struct anv_graphics_pipeline *gfx_pipeline =
         anv_pipeline_to_graphics(pipeline);
      if (cmd_buffer->state.gfx.pipeline == gfx_pipeline)
         return;

      cmd_buffer->state.gfx.pipeline = gfx_pipeline;
      cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_PIPELINE;

      anv_foreach_stage(stage, gfx_pipeline->active_stages) {
         set_dirty_for_bind_map(cmd_buffer, stage,
                                &gfx_pipeline->shaders[stage]->bind_map);
      }

      /* Apply the non dynamic state from the pipeline */
      vk_cmd_set_dynamic_graphics_state(&cmd_buffer->vk,
                                        &gfx_pipeline->dynamic_state);

      state = &cmd_buffer->state.gfx.base;
      stages = gfx_pipeline->active_stages;
      break;
   }

   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR: {
      struct anv_ray_tracing_pipeline *rt_pipeline =
         anv_pipeline_to_ray_tracing(pipeline);
      if (cmd_buffer->state.rt.pipeline == rt_pipeline)
         return;

      cmd_buffer->state.rt.pipeline = rt_pipeline;
      cmd_buffer->state.rt.pipeline_dirty = true;

      if (rt_pipeline->stack_size > 0) {
         anv_CmdSetRayTracingPipelineStackSizeKHR(commandBuffer,
                                                  rt_pipeline->stack_size);
      }

      state = &cmd_buffer->state.rt.base;
      break;
   }

   default:
      unreachable("invalid bind point");
      break;
   }

   if (pipeline->ray_queries > 0)
      anv_cmd_buffer_set_ray_query_buffer(cmd_buffer, state, pipeline, stages);
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
   /* Either we have no pool because it's a push descriptor or the pool is not
    * host only :
    *
    * VUID-vkCmdBindDescriptorSets-pDescriptorSets-04616:
    *
    *    "Each element of pDescriptorSets must not have been allocated from a
    *     VkDescriptorPool with the
    *     VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT flag set"
    */
   assert(!set->pool || !set->pool->host_only);

   struct anv_descriptor_set_layout *set_layout =
      layout->set[set_index].layout;

   VkShaderStageFlags stages = set_layout->shader_stages;
   struct anv_cmd_pipeline_state *pipe_state;

   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      stages &= VK_SHADER_STAGE_ALL_GRAPHICS |
                ((cmd_buffer->device->vk.enabled_extensions.NV_mesh_shader ||
                  cmd_buffer->device->vk.enabled_extensions.EXT_mesh_shader) ?
                      (VK_SHADER_STAGE_TASK_BIT_EXT |
                       VK_SHADER_STAGE_MESH_BIT_EXT) : 0);
      pipe_state = &cmd_buffer->state.gfx.base;
      break;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      stages &= VK_SHADER_STAGE_COMPUTE_BIT;
      pipe_state = &cmd_buffer->state.compute.base;
      break;

   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
      stages &= VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                VK_SHADER_STAGE_MISS_BIT_KHR |
                VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
                VK_SHADER_STAGE_CALLABLE_BIT_KHR;
      pipe_state = &cmd_buffer->state.rt.base;
      break;

   default:
      unreachable("invalid bind point");
   }

   VkShaderStageFlags dirty_stages = 0;
   /* If it's a push descriptor set, we have to flag things as dirty
    * regardless of whether or not the CPU-side data structure changed as we
    * may have edited in-place.
    */
   if (pipe_state->descriptors[set_index] != set ||
         anv_descriptor_set_is_push(set)) {
      pipe_state->descriptors[set_index] = set;

      /* Those stages don't have access to HW binding tables.
       * This means that we have to upload the descriptor set
       * as an 64-bit address in the push constants.
       */
      bool update_desc_sets = stages & (VK_SHADER_STAGE_TASK_BIT_EXT |
                                        VK_SHADER_STAGE_MESH_BIT_EXT |
                                        VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                        VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                                        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                        VK_SHADER_STAGE_MISS_BIT_KHR |
                                        VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
                                        VK_SHADER_STAGE_CALLABLE_BIT_KHR);

      if (update_desc_sets) {
         struct anv_push_constants *push = &pipe_state->push_constants;

         struct anv_address addr = anv_descriptor_set_address(set);
         push->desc_sets[set_index] = anv_address_physical(addr);

         if (addr.bo) {
            anv_reloc_list_add_bo(cmd_buffer->batch.relocs,
                                  cmd_buffer->batch.alloc,
                                  addr.bo);
         }
      }

      dirty_stages |= stages;
   }

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

   if (set->is_push)
      cmd_buffer->state.push_descriptors_dirty |= dirty_stages;
   else
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

void anv_CmdBindVertexBuffers2(
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

   assert(firstBinding + bindingCount <= MAX_VBS);
   for (uint32_t i = 0; i < bindingCount; i++) {
      ANV_FROM_HANDLE(anv_buffer, buffer, pBuffers[i]);

      if (buffer == NULL) {
         vb[firstBinding + i] = (struct anv_vertex_binding) {
            .buffer = NULL,
         };
      } else {
         vb[firstBinding + i] = (struct anv_vertex_binding) {
            .buffer = buffer,
            .offset = pOffsets[i],
            .size = vk_buffer_range(&buffer->vk, pOffsets[i],
                                    pSizes ? pSizes[i] : VK_WHOLE_SIZE),
         };
      }
      cmd_buffer->state.gfx.vb_dirty |= 1 << (firstBinding + i);
   }

   if (pStrides != NULL) {
      vk_cmd_set_vertex_binding_strides(&cmd_buffer->vk, firstBinding,
                                        bindingCount, pStrides);
   }
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
            vk_buffer_range(&buffer->vk, pOffsets[i],
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
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   struct anv_push_constants *data =
      &cmd_buffer->state.compute.base.push_constants;
   struct anv_compute_pipeline *pipeline = cmd_buffer->state.compute.pipeline;
   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(pipeline);
   const struct anv_push_range *range = &pipeline->cs->bind_map.push_ranges[0];

   const struct brw_cs_dispatch_info dispatch =
      brw_cs_get_dispatch_info(devinfo, cs_prog_data, NULL);
   const unsigned total_push_constants_size =
      brw_cs_push_const_total_size(cs_prog_data, dispatch.threads);
   if (total_push_constants_size == 0)
      return (struct anv_state) { .offset = 0 };

   const unsigned push_constant_alignment = 64;
   const unsigned aligned_total_push_constants_size =
      ALIGN(total_push_constants_size, push_constant_alignment);
   struct anv_state state;
   if (devinfo->verx10 >= 125) {
      state = anv_state_stream_alloc(&cmd_buffer->general_state_stream,
                                     aligned_total_push_constants_size,
                                     push_constant_alignment);
   } else {
      state = anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                                 aligned_total_push_constants_size,
                                                 push_constant_alignment);
   }

   void *dst = state.map;
   const void *src = (char *)data + (range->start * 32);

   if (cs_prog_data->push.cross_thread.size > 0) {
      memcpy(dst, src, cs_prog_data->push.cross_thread.size);
      dst += cs_prog_data->push.cross_thread.size;
      src += cs_prog_data->push.cross_thread.size;
   }

   if (cs_prog_data->push.per_thread.size > 0) {
      for (unsigned t = 0; t < dispatch.threads; t++) {
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

   if (stageFlags & (VK_SHADER_STAGE_ALL_GRAPHICS |
                     VK_SHADER_STAGE_TASK_BIT_EXT |
                     VK_SHADER_STAGE_MESH_BIT_EXT)) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.gfx.base;

      memcpy(pipe_state->push_constants.client_data + offset, pValues, size);
   }
   if (stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.compute.base;

      memcpy(pipe_state->push_constants.client_data + offset, pValues, size);
   }
   if (stageFlags & (VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                     VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                     VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                     VK_SHADER_STAGE_MISS_BIT_KHR |
                     VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
                     VK_SHADER_STAGE_CALLABLE_BIT_KHR)) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.rt.base;

      memcpy(pipe_state->push_constants.client_data + offset, pValues, size);
   }

   cmd_buffer->state.push_constants_dirty |= stageFlags;
}

static struct anv_descriptor_set *
anv_cmd_buffer_push_descriptor_set(struct anv_cmd_buffer *cmd_buffer,
                                   VkPipelineBindPoint bind_point,
                                   struct anv_descriptor_set_layout *layout,
                                   uint32_t _set)
{
   struct anv_cmd_pipeline_state *pipe_state;

   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      pipe_state = &cmd_buffer->state.gfx.base;
      break;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      pipe_state = &cmd_buffer->state.compute.base;
      break;

   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
      pipe_state = &cmd_buffer->state.rt.base;
      break;

   default:
      unreachable("invalid bind point");
   }

   struct anv_push_descriptor_set **push_set =
      &pipe_state->push_descriptor;

   if (*push_set == NULL) {
      *push_set = vk_zalloc(&cmd_buffer->vk.pool->alloc,
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
      set->generate_surface_states = 0;
   }
   set->is_push = true;
   set->size = anv_descriptor_set_layout_size(layout, false /* host_only */, 0);
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
                                anv_descriptor_set_layout_descriptor_buffer_size(layout, 0),
                                ANV_UBO_ALIGNMENT);
      if (set->desc_mem.alloc_size) {
         /* TODO: Do we really need to copy all the time? */
         memcpy(desc_mem.map, set->desc_mem.map,
                MIN2(desc_mem.alloc_size, set->desc_mem.alloc_size));
      }
      set->desc_mem = desc_mem;

      set->desc_addr = (struct anv_address) {
         .bo = cmd_buffer->dynamic_state_stream.state_pool->block_pool.bo,
         .offset = set->desc_mem.offset,
      };
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
                                            write->descriptorType,
                                            buffer,
                                            write->dstBinding,
                                            write->dstArrayElement + j,
                                            write->pBufferInfo[j].offset,
                                            write->pBufferInfo[j].range);
         }
         break;

      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
         const VkWriteDescriptorSetAccelerationStructureKHR *accel_write =
            vk_find_struct_const(write, WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
         assert(accel_write->accelerationStructureCount ==
                write->descriptorCount);
         for (uint32_t j = 0; j < write->descriptorCount; j++) {
            ANV_FROM_HANDLE(vk_acceleration_structure, accel,
                            accel_write->pAccelerationStructures[j]);
            anv_descriptor_set_write_acceleration_structure(cmd_buffer->device,
                                                            set, accel,
                                                            write->dstBinding,
                                                            write->dstArrayElement + j);
         }
         break;
      }

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
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
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
                                     template,
                                     pData);

   anv_cmd_buffer_bind_descriptor_set(cmd_buffer, template->bind_point,
                                      layout, _set, set, NULL, NULL);
}

void anv_CmdSetRayTracingPipelineStackSizeKHR(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    pipelineStackSize)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_cmd_ray_tracing_state *rt = &cmd_buffer->state.rt;
   struct anv_device *device = cmd_buffer->device;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   uint32_t stack_ids_per_dss = 2048; /* TODO */

   unsigned stack_size_log2 = util_logbase2_ceil(pipelineStackSize);
   if (stack_size_log2 < 10)
      stack_size_log2 = 10;

   if (rt->scratch.layout.total_size == 1 << stack_size_log2)
      return;

   brw_rt_compute_scratch_layout(&rt->scratch.layout, device->info,
                                 stack_ids_per_dss, 1 << stack_size_log2);

   unsigned bucket = stack_size_log2 - 10;
   assert(bucket < ARRAY_SIZE(device->rt_scratch_bos));

   struct anv_bo *bo = p_atomic_read(&device->rt_scratch_bos[bucket]);
   if (bo == NULL) {
      struct anv_bo *new_bo;
      VkResult result = anv_device_alloc_bo(device, "RT scratch",
                                            rt->scratch.layout.total_size,
                                            0, /* alloc_flags */
                                            0, /* explicit_address */
                                            &new_bo);
      if (result != VK_SUCCESS) {
         rt->scratch.layout.total_size = 0;
         anv_batch_set_error(&cmd_buffer->batch, result);
         return;
      }

      bo = p_atomic_cmpxchg(&device->rt_scratch_bos[bucket], NULL, new_bo);
      if (bo != NULL) {
         anv_device_release_bo(device, bo);
      } else {
         bo = new_bo;
      }
   }

   rt->scratch.bo = bo;
}
