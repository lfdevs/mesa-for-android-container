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

#include "vk_common_entrypoints.h"
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
   state->gfx.streamout_stage = MESA_SHADER_NONE;

   state->compute.pixel_async_compute_thread_limit = UINT8_MAX;
   state->compute.z_pass_async_compute_thread_limit = UINT8_MAX;
   state->compute.np_z_async_throttle_settings = UINT8_MAX;

   BITSET_COPY(state->gfx.dyn_state.pack_dirty,
               cmd_buffer->device->gfx_dirty_state);
}

static void
anv_cmd_pipeline_state_finish(struct anv_cmd_buffer *cmd_buffer,
                              struct anv_cmd_pipeline_state *pipe_state)
{
   anv_push_descriptor_set_finish(&pipe_state->push_descriptor);
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

VkResult
anv_cmd_buffer_ensure_rcs_companion(struct anv_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->companion_rcs_cmd_buffer)
      return VK_SUCCESS;

   VkResult result = VK_SUCCESS;
   pthread_mutex_lock(&cmd_buffer->device->mutex);
   VK_FROM_HANDLE(vk_command_pool, pool,
                  cmd_buffer->device->companion_rcs_cmd_pool);
   assert(pool != NULL);

   struct vk_command_buffer *tmp_cmd_buffer = NULL;
   result = pool->command_buffer_ops->create(pool, cmd_buffer->vk.level, &tmp_cmd_buffer);

   if (result != VK_SUCCESS)
      goto unlock_and_return;

   cmd_buffer->companion_rcs_cmd_buffer =
      container_of(tmp_cmd_buffer, struct anv_cmd_buffer, vk);
   anv_genX(cmd_buffer->device->info, cmd_buffer_begin_companion)(
      cmd_buffer->companion_rcs_cmd_buffer, cmd_buffer->vk.level);

unlock_and_return:
   pthread_mutex_unlock(&cmd_buffer->device->mutex);
   return result;
}

static VkResult
anv_create_cmd_buffer(struct vk_command_pool *pool,
                      VkCommandBufferLevel level,
                      struct vk_command_buffer **cmd_buffer_out)
{
   struct anv_device *device =
      container_of(pool->base.device, struct anv_device, vk);
   struct anv_cmd_buffer *cmd_buffer;
   VkResult result;

   cmd_buffer = vk_zalloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(pool, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = vk_command_buffer_init(pool, &cmd_buffer->vk,
                                   &anv_cmd_buffer_ops, level);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   cmd_buffer->vk.dynamic_graphics_state.ms.sample_locations =
      &cmd_buffer->state.gfx.sample_locations;
   cmd_buffer->vk.dynamic_graphics_state.vi =
      &cmd_buffer->state.gfx.vertex_input;

   cmd_buffer->batch.status = VK_SUCCESS;
   cmd_buffer->generation.batch.status = VK_SUCCESS;

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
   anv_state_stream_init(&cmd_buffer->indirect_push_descriptor_stream,
                         &device->indirect_push_descriptor_pool, 4096);
   anv_state_stream_init(&cmd_buffer->push_descriptor_buffer_stream,
                         &device->push_descriptor_buffer_pool, 4096);

   int success = u_vector_init_pow2(&cmd_buffer->dynamic_bos, 8,
                                    sizeof(struct anv_bo *));
   if (!success)
      goto fail_batch_bo;

   cmd_buffer->self_mod_locations = NULL;
   cmd_buffer->companion_rcs_cmd_buffer = NULL;
   cmd_buffer->is_companion_rcs_cmd_buffer = false;

   cmd_buffer->generation.jump_addr = ANV_NULL_ADDRESS;
   cmd_buffer->generation.return_addr = ANV_NULL_ADDRESS;

   memset(&cmd_buffer->generation.shader_state, 0,
          sizeof(cmd_buffer->generation.shader_state));

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
destroy_cmd_buffer(struct anv_cmd_buffer *cmd_buffer)
{
   u_trace_fini(&cmd_buffer->trace);

   anv_measure_destroy(cmd_buffer);

   anv_cmd_buffer_fini_batch_bo_chain(cmd_buffer);

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);
   anv_state_stream_finish(&cmd_buffer->general_state_stream);
   anv_state_stream_finish(&cmd_buffer->indirect_push_descriptor_stream);
   anv_state_stream_finish(&cmd_buffer->push_descriptor_buffer_stream);

   while (u_vector_length(&cmd_buffer->dynamic_bos) > 0) {
      struct anv_bo **bo = u_vector_remove(&cmd_buffer->dynamic_bos);
      ANV_DMR_BO_FREE(&cmd_buffer->vk.base, *bo);
      anv_bo_pool_free((*bo)->map != NULL ?
                       &cmd_buffer->device->batch_bo_pool :
                       &cmd_buffer->device->bvh_bo_pool, *bo);
   }
   u_vector_finish(&cmd_buffer->dynamic_bos);

   anv_cmd_state_finish(cmd_buffer);

   vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer->self_mod_locations);

   vk_command_buffer_finish(&cmd_buffer->vk);
   vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer);
}

static void
anv_cmd_buffer_destroy(struct vk_command_buffer *vk_cmd_buffer)
{
   struct anv_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct anv_cmd_buffer, vk);
   struct anv_device *device = cmd_buffer->device;

   pthread_mutex_lock(&device->mutex);
   if (cmd_buffer->companion_rcs_cmd_buffer) {
      destroy_cmd_buffer(cmd_buffer->companion_rcs_cmd_buffer);
      cmd_buffer->companion_rcs_cmd_buffer = NULL;
   }

   ANV_RMV(cmd_buffer_destroy, cmd_buffer->device, cmd_buffer);

   destroy_cmd_buffer(cmd_buffer);
   pthread_mutex_unlock(&device->mutex);
}

static void
reset_cmd_buffer(struct anv_cmd_buffer *cmd_buffer,
                 UNUSED VkCommandBufferResetFlags flags)
{
   vk_command_buffer_reset(&cmd_buffer->vk);

   cmd_buffer->usage_flags = 0;
   cmd_buffer->perf_query_pool = NULL;
   cmd_buffer->is_companion_rcs_cmd_buffer = false;
   anv_cmd_buffer_reset_batch_bo_chain(cmd_buffer);
   anv_cmd_state_reset(cmd_buffer);

   memset(&cmd_buffer->generation.shader_state, 0,
          sizeof(cmd_buffer->generation.shader_state));

   cmd_buffer->generation.jump_addr = ANV_NULL_ADDRESS;
   cmd_buffer->generation.return_addr = ANV_NULL_ADDRESS;

   anv_state_stream_finish(&cmd_buffer->surface_state_stream);
   anv_state_stream_init(&cmd_buffer->surface_state_stream,
                         &cmd_buffer->device->internal_surface_state_pool, 4096);

   anv_state_stream_finish(&cmd_buffer->dynamic_state_stream);
   anv_state_stream_init(&cmd_buffer->dynamic_state_stream,
                         &cmd_buffer->device->dynamic_state_pool, 16384);

   anv_state_stream_finish(&cmd_buffer->general_state_stream);
   anv_state_stream_init(&cmd_buffer->general_state_stream,
                         &cmd_buffer->device->general_state_pool, 16384);

   anv_state_stream_finish(&cmd_buffer->indirect_push_descriptor_stream);
   anv_state_stream_init(&cmd_buffer->indirect_push_descriptor_stream,
                         &cmd_buffer->device->indirect_push_descriptor_pool,
                         4096);

   anv_state_stream_finish(&cmd_buffer->push_descriptor_buffer_stream);
   anv_state_stream_init(&cmd_buffer->push_descriptor_buffer_stream,
                         &cmd_buffer->device->push_descriptor_buffer_pool, 4096);

   while (u_vector_length(&cmd_buffer->dynamic_bos) > 0) {
      struct anv_bo **bo = u_vector_remove(&cmd_buffer->dynamic_bos);
      anv_device_release_bo(cmd_buffer->device, *bo);
   }

   anv_measure_reset(cmd_buffer);

   u_trace_fini(&cmd_buffer->trace);
   u_trace_init(&cmd_buffer->trace, &cmd_buffer->device->ds.trace_context);
}

void
anv_cmd_buffer_reset(struct vk_command_buffer *vk_cmd_buffer,
                     UNUSED VkCommandBufferResetFlags flags)
{
   struct anv_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct anv_cmd_buffer, vk);

   if (cmd_buffer->companion_rcs_cmd_buffer) {
      reset_cmd_buffer(cmd_buffer->companion_rcs_cmd_buffer, flags);
      destroy_cmd_buffer(cmd_buffer->companion_rcs_cmd_buffer);
      cmd_buffer->companion_rcs_cmd_buffer = NULL;
   }

   ANV_RMV(cmd_buffer_destroy, cmd_buffer->device, cmd_buffer);

   reset_cmd_buffer(cmd_buffer, flags);
}

const struct vk_command_buffer_ops anv_cmd_buffer_ops = {
   .create = anv_create_cmd_buffer,
   .reset = anv_cmd_buffer_reset,
   .destroy = anv_cmd_buffer_destroy,
};

void
anv_cmd_buffer_emit_bt_pool_base_address(struct anv_cmd_buffer *cmd_buffer)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   anv_genX(devinfo, cmd_buffer_emit_bt_pool_base_address)(cmd_buffer);
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
anv_cmd_buffer_mark_image_fast_cleared(struct anv_cmd_buffer *cmd_buffer,
                                       const struct anv_image *image,
                                       const enum isl_format format,
                                       const struct isl_swizzle swizzle,
                                       union isl_color_value clear_color)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   anv_genX(devinfo, set_fast_clear_state)(cmd_buffer, image, format, swizzle,
                                           clear_color);
}

void
anv_cmd_buffer_load_clear_color(struct anv_cmd_buffer *cmd_buffer,
                                struct anv_state state,
                                const struct anv_image_view *iview)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   anv_genX(devinfo, cmd_buffer_load_clear_color)(cmd_buffer, state, iview);
}

void
anv_cmd_emit_conditional_render_predicate(struct anv_cmd_buffer *cmd_buffer)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   anv_genX(devinfo, cmd_emit_conditional_render_predicate)(cmd_buffer);
}

static void
clear_pending_query_bits(enum anv_query_bits *query_bits,
                         enum anv_pipe_bits flushed_bits)
{
   if (flushed_bits & ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT)
      *query_bits &= ~ANV_QUERY_WRITES_RT_FLUSH;

   if (flushed_bits & ANV_PIPE_TILE_CACHE_FLUSH_BIT)
      *query_bits &= ~ANV_QUERY_WRITES_TILE_FLUSH;

   if ((flushed_bits & ANV_PIPE_DATA_CACHE_FLUSH_BIT) &&
       (flushed_bits & ANV_PIPE_HDC_PIPELINE_FLUSH_BIT) &&
       (flushed_bits & ANV_PIPE_UNTYPED_DATAPORT_CACHE_FLUSH_BIT))
      *query_bits &= ~ANV_QUERY_WRITES_TILE_FLUSH;

   /* Once RT/TILE have been flushed, we can consider the CS_STALL flush */
   if ((*query_bits & (ANV_QUERY_WRITES_TILE_FLUSH |
                       ANV_QUERY_WRITES_RT_FLUSH |
                       ANV_QUERY_WRITES_DATA_FLUSH)) == 0 &&
       (flushed_bits & (ANV_PIPE_END_OF_PIPE_SYNC_BIT | ANV_PIPE_CS_STALL_BIT)))
      *query_bits &= ~ANV_QUERY_WRITES_CS_STALL;
}

void
anv_cmd_buffer_update_pending_query_bits(struct anv_cmd_buffer *cmd_buffer,
                                         enum anv_pipe_bits flushed_bits)
{
   clear_pending_query_bits(&cmd_buffer->state.queries.clear_bits, flushed_bits);
   clear_pending_query_bits(&cmd_buffer->state.queries.buffer_write_bits, flushed_bits);
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
                       mesa_shader_stage stage,
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
anv_cmd_buffer_set_rt_query_buffer(struct anv_cmd_buffer *cmd_buffer,
                                   struct anv_cmd_pipeline_state *pipeline_state,
                                   uint32_t ray_queries,
                                   VkShaderStageFlags stages)
{
   struct anv_device *device = cmd_buffer->device;
   uint8_t idx = anv_get_ray_query_bo_index(cmd_buffer);

   uint64_t ray_shadow_size =
      align64(brw_rt_ray_queries_shadow_stacks_size(device->info, ray_queries),
              4096);
   if (ray_shadow_size > 0 &&
       (!cmd_buffer->state.ray_query_shadow_bo ||
        cmd_buffer->state.ray_query_shadow_bo->size < ray_shadow_size)) {
      unsigned shadow_size_log2 = MAX2(util_logbase2_ceil(ray_shadow_size), 16);
      unsigned bucket = shadow_size_log2 - 16;
      assert(bucket < ARRAY_SIZE(device->ray_query_shadow_bos[0]));

      struct anv_bo *bo = p_atomic_read(&device->ray_query_shadow_bos[idx][bucket]);
      if (bo == NULL) {
         struct anv_bo *new_bo;
         VkResult result = anv_device_alloc_bo(device, "RT queries shadow",
                                               1 << shadow_size_log2,
                                               ANV_BO_ALLOC_INTERNAL, /* alloc_flags */
                                               0, /* explicit_address */
                                               &new_bo);
         ANV_DMR_BO_ALLOC(&cmd_buffer->vk.base, new_bo, result);
         if (result != VK_SUCCESS) {
            anv_batch_set_error(&cmd_buffer->batch, result);
            return;
         }

         bo = p_atomic_cmpxchg(&device->ray_query_shadow_bos[idx][bucket], NULL, new_bo);
         if (bo != NULL) {
            ANV_DMR_BO_FREE(&device->vk.base, new_bo);
            anv_device_release_bo(device, new_bo);
         } else {
            bo = new_bo;
         }
      }
      cmd_buffer->state.ray_query_shadow_bo = bo;

      /* Add the ray query buffers to the batch list. */
      anv_reloc_list_add_bo(cmd_buffer->batch.relocs,
                            cmd_buffer->state.ray_query_shadow_bo);
   }

   /* Add the HW buffer to the list of BO used. */
   assert(device->ray_query_bo[idx]);
   anv_reloc_list_add_bo(cmd_buffer->batch.relocs,
                         device->ray_query_bo[idx]);

   /* Fill the push constants & mark them dirty. */
   struct anv_address ray_query_globals_addr =
      anv_genX(device->info, cmd_buffer_ray_query_globals)(cmd_buffer);
   pipeline_state->push_constants.ray_query_globals =
      anv_address_physical(ray_query_globals_addr);
   cmd_buffer->state.push_constants_dirty |= stages;
   pipeline_state->push_constants_data_dirty = true;
}

static void
update_push_descriptor_flags(struct anv_cmd_pipeline_state *state,
                             struct anv_shader ** const shaders,
                             uint32_t shader_count)
{
   state->push_buffer_stages = 0;
   state->push_descriptor_stages = 0;

   for (uint32_t i = 0; i < shader_count; i++) {
      if (shaders[i] == NULL)
         continue;

      VkShaderStageFlags stage = mesa_to_vk_shader_stage(shaders[i]->vk.stage);

      if (shaders[i]->push_desc_info.used_descriptors)
         state->push_descriptor_stages |= stage;

      if (shaders[i]->push_desc_info.push_set_buffer)
         state->push_buffer_stages |= stage;
   }
}

static bool
maybe_update_dynamic_buffers_indices(struct anv_cmd_pipeline_state *state,
                                     const uint8_t *offsets)
{
   struct anv_push_constants *push = &state->push_constants;

   bool modified = false;
   for (uint32_t i = 0; i < MAX_SETS; i++) {
      if ((push->desc_surface_offsets[i] &
           ANV_DESCRIPTOR_SET_DYNAMIC_INDEX_MASK) !=
          offsets[i]) {
         push->desc_surface_offsets[i] &= ~ANV_DESCRIPTOR_SET_DYNAMIC_INDEX_MASK;
         push->desc_surface_offsets[i] |= offsets[i];
         modified = true;
      }
   }

   return modified;
}

static struct anv_cmd_pipeline_state *
anv_cmd_buffer_get_pipeline_layout_state(struct anv_cmd_buffer *cmd_buffer,
                                         VkPipelineBindPoint bind_point,
                                         const struct anv_descriptor_set_layout *set_layout,
                                         VkShaderStageFlags *out_stages)
{
   *out_stages = set_layout->shader_stages;

   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      *out_stages &= VK_SHADER_STAGE_ALL_GRAPHICS |
         (cmd_buffer->device->vk.enabled_extensions.EXT_mesh_shader ?
          (VK_SHADER_STAGE_TASK_BIT_EXT |
           VK_SHADER_STAGE_MESH_BIT_EXT) : 0);
      return &cmd_buffer->state.gfx.base;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      *out_stages &= VK_SHADER_STAGE_COMPUTE_BIT;
      return &cmd_buffer->state.compute.base;

   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
      *out_stages &= ANV_RT_STAGE_BITS;
      return &cmd_buffer->state.rt.base;

   default:
      UNREACHABLE("invalid bind point");
   }
}

static void
anv_cmd_buffer_maybe_dirty_descriptor_mode(struct anv_cmd_buffer *cmd_buffer,
                                           enum anv_cmd_descriptor_buffer_mode new_mode)
{
   if (cmd_buffer->state.pending_db_mode == new_mode)
      return;

   /* Ensure we program the STATE_BASE_ADDRESS properly at least once */
   cmd_buffer->state.descriptor_buffers.dirty = true;
   cmd_buffer->state.pending_db_mode = new_mode;
}

static void
anv_cmd_buffer_bind_descriptor_set(struct anv_cmd_buffer *cmd_buffer,
                                   VkPipelineBindPoint bind_point,
                                   struct vk_pipeline_layout *layout,
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

   struct anv_descriptor_set_layout *set_layout = set->layout;

   anv_cmd_buffer_maybe_dirty_descriptor_mode(
      cmd_buffer,
      (set->layout->vk.flags &
       VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) != 0 ?
      ANV_CMD_DESCRIPTOR_BUFFER_MODE_BUFFER :
      ANV_CMD_DESCRIPTOR_BUFFER_MODE_LEGACY);

   VkShaderStageFlags stages;
   struct anv_cmd_pipeline_state *pipe_state =
      anv_cmd_buffer_get_pipeline_layout_state(cmd_buffer, bind_point,
                                               set_layout, &stages);

   VkShaderStageFlags dirty_stages = 0;
   /* If it's a push descriptor set, we have to flag things as dirty
    * regardless of whether or not the CPU-side data structure changed as we
    * may have edited in-place.
    */
   if (pipe_state->descriptors[set_index] != set ||
       anv_descriptor_set_is_push(set)) {
      pipe_state->descriptors[set_index] = set;

      if (set->layout->vk.flags &
          VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) {
         assert(set->is_push);

         pipe_state->descriptor_buffers[set_index].buffer_index = -1;
         pipe_state->descriptor_buffers[set_index].buffer_offset = set->desc_offset;
         pipe_state->descriptor_buffers[set_index].bound = true;
         cmd_buffer->state.descriptors_dirty |= stages;
         cmd_buffer->state.descriptor_buffers.offsets_dirty |= stages;
      } else {
         /* When using indirect descriptors, stages that have access to the HW
          * binding tables, never need to access the
          * anv_push_constants::desc_offsets fields, because any data they
          * need from the descriptor buffer is accessible through a binding
          * table entry. For stages that are "bindless" (Mesh/Task/RT), we
          * need to provide anv_push_constants::desc_offsets matching the
          * bound descriptor so that shaders can access the descriptor buffer
          * through A64 messages.
          *
          * With direct descriptors, the shaders can use the
          * anv_push_constants::desc_offsets to build bindless offsets. So
          * it's we always need to update the push constant data.
          */
         bool update_desc_sets =
            !cmd_buffer->device->physical->indirect_descriptors ||
            (stages & (VK_SHADER_STAGE_TASK_BIT_EXT |
                       VK_SHADER_STAGE_MESH_BIT_EXT |
                       ANV_RT_STAGE_BITS));

         if (update_desc_sets) {
            struct anv_push_constants *push = &pipe_state->push_constants;
            uint64_t offset =
               anv_address_physical(set->desc_surface_addr) -
               cmd_buffer->device->physical->va.internal_surface_state_pool.addr;
            assert((offset & ~ANV_DESCRIPTOR_SET_OFFSET_MASK) == 0);
            push->desc_surface_offsets[set_index] &= ~ANV_DESCRIPTOR_SET_OFFSET_MASK;
            push->desc_surface_offsets[set_index] |= offset;
            push->desc_sampler_offsets[set_index] =
               anv_address_physical(set->desc_sampler_addr) -
               cmd_buffer->device->physical->va.dynamic_state_pool.addr;

            anv_reloc_list_add_bo(cmd_buffer->batch.relocs,
                                  set->desc_surface_addr.bo);
            anv_reloc_list_add_bo(cmd_buffer->batch.relocs,
                                  set->desc_sampler_addr.bo);
         }
      }

      dirty_stages |= stages;
   }

   if (dynamic_offsets) {
      if (set_layout->vk.dynamic_descriptor_count > 0) {
         struct anv_push_constants *push = &pipe_state->push_constants;
         assert(layout != NULL);
         uint32_t dynamic_offset_start =
            layout->dynamic_descriptor_offset[set_index];
         uint32_t *push_offsets =
            &push->dynamic_offsets[dynamic_offset_start];

         memcpy(pipe_state->dynamic_offsets[set_index].offsets,
                *dynamic_offsets,
                sizeof(uint32_t) * MIN2(*dynamic_offset_count,
                                        set_layout->vk.dynamic_descriptor_count));

         /* Assert that everything is in range */
         assert(set_layout->vk.dynamic_descriptor_count <= *dynamic_offset_count);
         assert(dynamic_offset_start + set_layout->vk.dynamic_descriptor_count <=
                ARRAY_SIZE(push->dynamic_offsets));

         for (uint32_t i = 0; i < set_layout->vk.dynamic_descriptor_count; i++) {
            if (push_offsets[i] != (*dynamic_offsets)[i]) {
               pipe_state->dynamic_offsets[set_index].offsets[i] =
                  push_offsets[i] = (*dynamic_offsets)[i];
               /* dynamic_offset_stages[] elements could contain blanket
                * values like VK_SHADER_STAGE_ALL, so limit this to the
                * binding point's bits.
                */
               dirty_stages |= set_layout->dynamic_offset_stages[i] & stages;
            }
         }

         *dynamic_offsets += set_layout->vk.dynamic_descriptor_count;
         *dynamic_offset_count -= set_layout->vk.dynamic_descriptor_count;
      }
   }

   /* Update the push descriptor index tracking */
   if (anv_descriptor_set_is_push(set))
      pipe_state->push_descriptor_index = set_index;
   else if (pipe_state->push_descriptor_index == set_index)
      pipe_state->push_descriptor_index = UINT8_MAX;

   if (set->is_push)
      cmd_buffer->state.push_descriptors_dirty |= dirty_stages;
   else
      cmd_buffer->state.descriptors_dirty |= dirty_stages;
   cmd_buffer->state.push_constants_dirty |= dirty_stages;
   pipe_state->push_constants_data_dirty = true;
}

void anv_CmdBindDescriptorSets2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkBindDescriptorSetsInfoKHR*          pInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_pipeline_layout, layout, pInfo->layout);

   assert(pInfo->firstSet + pInfo->descriptorSetCount <= MAX_SETS);

   if (pInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      uint32_t dynamicOffsetCount = pInfo->dynamicOffsetCount;
      const uint32_t *pDynamicOffsets = pInfo->pDynamicOffsets;

      for (uint32_t i = 0; i < pInfo->descriptorSetCount; i++) {
         ANV_FROM_HANDLE(anv_descriptor_set, set, pInfo->pDescriptorSets[i]);
         if (set == NULL)
            continue;
         anv_cmd_buffer_bind_descriptor_set(cmd_buffer,
                                            VK_PIPELINE_BIND_POINT_COMPUTE,
                                            layout, pInfo->firstSet + i, set,
                                            &dynamicOffsetCount,
                                            &pDynamicOffsets);
      }
   }
   if (pInfo->stageFlags & ANV_GRAPHICS_STAGE_BITS) {
      uint32_t dynamicOffsetCount = pInfo->dynamicOffsetCount;
      const uint32_t *pDynamicOffsets = pInfo->pDynamicOffsets;

      for (uint32_t i = 0; i < pInfo->descriptorSetCount; i++) {
         ANV_FROM_HANDLE(anv_descriptor_set, set, pInfo->pDescriptorSets[i]);
         if (set == NULL)
            continue;
         anv_cmd_buffer_bind_descriptor_set(cmd_buffer,
                                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            layout, pInfo->firstSet + i, set,
                                            &dynamicOffsetCount,
                                            &pDynamicOffsets);
      }
   }
   if (pInfo->stageFlags & ANV_RT_STAGE_BITS) {
      uint32_t dynamicOffsetCount = pInfo->dynamicOffsetCount;
      const uint32_t *pDynamicOffsets = pInfo->pDynamicOffsets;

      for (uint32_t i = 0; i < pInfo->descriptorSetCount; i++) {
         ANV_FROM_HANDLE(anv_descriptor_set, set, pInfo->pDescriptorSets[i]);
         if (set == NULL)
            continue;
         anv_cmd_buffer_bind_descriptor_set(cmd_buffer,
                                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                            layout, pInfo->firstSet + i, set,
                                            &dynamicOffsetCount,
                                            &pDynamicOffsets);
      }
   }
}

void anv_CmdBindDescriptorBuffersEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    bufferCount,
    const VkDescriptorBufferBindingInfoEXT*     pBindingInfos)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   struct anv_cmd_state *state = &cmd_buffer->state;

   for (uint32_t i = 0; i < bufferCount; i++) {
      assert(pBindingInfos[i].address >= cmd_buffer->device->physical->va.dynamic_visible_pool.addr &&
             pBindingInfos[i].address < (cmd_buffer->device->physical->va.dynamic_visible_pool.addr +
                                         cmd_buffer->device->physical->va.dynamic_visible_pool.size));

      if (state->descriptor_buffers.address[i] != pBindingInfos[i].address) {
         state->descriptor_buffers.address[i] = pBindingInfos[i].address;
         if (pBindingInfos[i].usage & VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT)
            state->descriptor_buffers.surfaces_address = pBindingInfos[i].address;
         if (pBindingInfos[i].usage & VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT)
            state->descriptor_buffers.samplers_address = pBindingInfos[i].address;
         state->descriptor_buffers.dirty = true;
         state->descriptor_buffers.offsets_dirty = ~0;
      }
   }
}

static void
anv_cmd_buffer_set_descriptor_buffer_offsets(struct anv_cmd_buffer *cmd_buffer,
                                             VkPipelineBindPoint bind_point,
                                             struct vk_pipeline_layout *layout,
                                             uint32_t first_set,
                                             uint32_t set_count,
                                             const VkDeviceSize *buffer_offsets,
                                             const uint32_t *buffer_indices)
{
   for (uint32_t i = 0; i < set_count; i++) {
      const uint32_t set_index = first_set + i;

      const struct anv_descriptor_set_layout *set_layout =
         container_of(layout->set_layouts[set_index],
                      const struct anv_descriptor_set_layout, vk);
      VkShaderStageFlags stages;
      struct anv_cmd_pipeline_state *pipe_state =
         anv_cmd_buffer_get_pipeline_layout_state(cmd_buffer, bind_point,
                                                  set_layout, &stages);

      if (buffer_offsets[i] != pipe_state->descriptor_buffers[set_index].buffer_offset ||
          buffer_indices[i] != pipe_state->descriptor_buffers[set_index].buffer_index ||
          !pipe_state->descriptor_buffers[set_index].bound) {
         pipe_state->descriptor_buffers[set_index].buffer_index = buffer_indices[i];
         pipe_state->descriptor_buffers[set_index].buffer_offset = buffer_offsets[i];
         cmd_buffer->state.descriptors_dirty |= stages;
         cmd_buffer->state.descriptor_buffers.offsets_dirty |= stages;
      }
      pipe_state->descriptor_buffers[set_index].bound = true;
   }
}

void anv_CmdSetDescriptorBufferOffsets2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkSetDescriptorBufferOffsetsInfoEXT*  pSetDescriptorBufferOffsetsInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_pipeline_layout, layout, pSetDescriptorBufferOffsetsInfo->layout);

   if (pSetDescriptorBufferOffsetsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      anv_cmd_buffer_set_descriptor_buffer_offsets(cmd_buffer,
                                                   VK_PIPELINE_BIND_POINT_COMPUTE,
                                                   layout,
                                                   pSetDescriptorBufferOffsetsInfo->firstSet,
                                                   pSetDescriptorBufferOffsetsInfo->setCount,
                                                   pSetDescriptorBufferOffsetsInfo->pOffsets,
                                                   pSetDescriptorBufferOffsetsInfo->pBufferIndices);
   }
   if (pSetDescriptorBufferOffsetsInfo->stageFlags & ANV_GRAPHICS_STAGE_BITS) {
      anv_cmd_buffer_set_descriptor_buffer_offsets(cmd_buffer,
                                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   layout,
                                                   pSetDescriptorBufferOffsetsInfo->firstSet,
                                                   pSetDescriptorBufferOffsetsInfo->setCount,
                                                   pSetDescriptorBufferOffsetsInfo->pOffsets,
                                                   pSetDescriptorBufferOffsetsInfo->pBufferIndices);
   }
   if (pSetDescriptorBufferOffsetsInfo->stageFlags & ANV_RT_STAGE_BITS) {
      anv_cmd_buffer_set_descriptor_buffer_offsets(cmd_buffer,
                                                   VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                                   layout,
                                                   pSetDescriptorBufferOffsetsInfo->firstSet,
                                                   pSetDescriptorBufferOffsetsInfo->setCount,
                                                   pSetDescriptorBufferOffsetsInfo->pOffsets,
                                                   pSetDescriptorBufferOffsetsInfo->pBufferIndices);
   }

   anv_cmd_buffer_maybe_dirty_descriptor_mode(cmd_buffer,
                                              ANV_CMD_DESCRIPTOR_BUFFER_MODE_BUFFER);
}

void anv_CmdBindDescriptorBufferEmbeddedSamplers2EXT(
    VkCommandBuffer                             commandBuffer,
    const VkBindDescriptorBufferEmbeddedSamplersInfoEXT* pBindDescriptorBufferEmbeddedSamplersInfo)
{
   /* no-op */
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

   assert(firstBinding + bindingCount <= get_max_vbs(cmd_buffer->device->info));
   for (uint32_t i = 0; i < bindingCount; i++) {
      ANV_FROM_HANDLE(anv_buffer, buffer, pBuffers[i]);

      if (buffer == NULL) {
         vb[firstBinding + i] = (struct anv_vertex_binding) { 0 };
      } else {
         vb[firstBinding + i] = (struct anv_vertex_binding) {
            .addr = anv_address_physical(
               anv_address_add(buffer->address, pOffsets[i])),
            .size = vk_buffer_range(&buffer->vk, pOffsets[i],
                                    pSizes ? pSizes[i] : VK_WHOLE_SIZE),
            .mocs = anv_mocs(cmd_buffer->device, buffer->address.bo,
                             ISL_SURF_USAGE_VERTEX_BUFFER_BIT),
         };
      }
      cmd_buffer->state.gfx.vb_dirty |= 1 << (firstBinding + i);
   }

   if (pStrides != NULL) {
      vk_cmd_set_vertex_binding_strides(&cmd_buffer->vk, firstBinding,
                                        bindingCount, pStrides);
   }
}

void anv_CmdBindIndexBuffer2KHR(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkIndexType                                 indexType)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   if (cmd_buffer->state.gfx.index_type != indexType) {
      cmd_buffer->state.gfx.index_type = indexType;
      cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_INDEX_TYPE;
   }

   uint64_t index_addr = buffer ?
      anv_address_physical(anv_address_add(buffer->address, offset)) : 0;
   uint32_t index_size = buffer ? vk_buffer_range(&buffer->vk, offset, size) : 0;
   if (cmd_buffer->state.gfx.index_addr != index_addr ||
       cmd_buffer->state.gfx.index_size != index_size) {
      cmd_buffer->state.gfx.index_addr = index_addr;
      cmd_buffer->state.gfx.index_size = index_size;
      cmd_buffer->state.gfx.index_mocs =
         anv_mocs(cmd_buffer->device, buffer->address.bo,
                  ISL_SURF_USAGE_INDEX_BUFFER_BIT);
      cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_INDEX_BUFFER;
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
         xfb[firstBinding + i] = (struct anv_xfb_binding) { 0 };
      } else {
         ANV_FROM_HANDLE(anv_buffer, buffer, pBuffers[i]);
         xfb[firstBinding + i] = (struct anv_xfb_binding) {
            .addr = anv_address_physical(
               anv_address_add(buffer->address, pOffsets[i])),
            .size = vk_buffer_range(&buffer->vk, pOffsets[i],
                                    pSizes ? pSizes[i] : VK_WHOLE_SIZE),
            .mocs = anv_mocs(cmd_buffer->device, buffer->address.bo,
                             ISL_SURF_USAGE_STREAM_OUT_BIT),
         };
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
      UNREACHABLE("Invalid descriptor type");
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
   for (uint32_t i = 0; i < dwords; i++) {
      assert((a[i] & b[i]) == 0);
      p[i] = a[i] | b[i];
   }

   VG(VALGRIND_CHECK_MEM_IS_DEFINED(p, dwords * 4));

   return state;
}

struct anv_state
anv_cmd_buffer_gfx_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   const struct anv_push_constants *data =
      &cmd_buffer->state.gfx.base.push_constants;

   /* For Mesh/Task shaders the 3DSTATE_(MESH|TASK)_SHADER_DATA require a 64B
    * alignment.
    *
    * ATMS PRMs Volume 2d: Command Reference: Structures,
    * 3DSTATE_MESH_SHADER_DATA_BODY::Indirect Data Start Address:
    *
    *    "This pointer is relative to the General State Base Address. It is
    *     the 64-byte aligned address of the indirect data."
    */
   struct anv_state state =
      anv_cmd_buffer_alloc_temporary_state(cmd_buffer,
                                           sizeof(struct anv_push_constants),
                                           32 /* bottom 5 bits MBZ */);
   if (state.alloc_size == 0)
      return state;

   memcpy(state.map, data->client_data,
          cmd_buffer->state.gfx.base.push_constants_client_size);
   memcpy(state.map + sizeof(data->client_data),
          &data->desc_surface_offsets,
          sizeof(struct anv_push_constants) - sizeof(data->client_data));

   return state;
}

struct anv_state
anv_cmd_buffer_cs_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   struct anv_cmd_compute_state *comp_state = &cmd_buffer->state.compute;
   struct anv_cmd_pipeline_state *pipe_state = &comp_state->base;
   struct anv_push_constants *data = &pipe_state->push_constants;
   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(comp_state);
   const struct anv_push_range *range = &comp_state->shader->bind_map.push_ranges[0];

   const struct intel_cs_dispatch_info dispatch =
      brw_cs_get_dispatch_info(devinfo, cs_prog_data, NULL);
   const unsigned total_push_constants_size =
      brw_cs_push_const_total_size(cs_prog_data, dispatch.threads);
   if (total_push_constants_size == 0)
      return (struct anv_state) { .offset = 0 };

   const unsigned push_constant_alignment = 64;
   const unsigned aligned_total_push_constants_size =
      align(total_push_constants_size, push_constant_alignment);
   struct anv_state state;
   if (devinfo->verx10 >= 125) {
      state = anv_cmd_buffer_alloc_general_state(cmd_buffer,
                                                 aligned_total_push_constants_size,
                                                 push_constant_alignment);
   } else {
      state = anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                                 aligned_total_push_constants_size,
                                                 push_constant_alignment);
   }
   if (state.map == NULL)
      return state;

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

void anv_CmdPushConstants2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkPushConstantsInfoKHR*               pInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (pInfo->stageFlags & ANV_GRAPHICS_STAGE_BITS) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.gfx.base;

      memcpy(pipe_state->push_constants.client_data + pInfo->offset,
             pInfo->pValues, pInfo->size);
      pipe_state->push_constants_data_dirty = true;
      pipe_state->push_constants_client_size = MAX2(
         pipe_state->push_constants_client_size, pInfo->offset + pInfo->size);
   }
   if (pInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.compute.base;

      memcpy(pipe_state->push_constants.client_data + pInfo->offset,
             pInfo->pValues, pInfo->size);
      pipe_state->push_constants_data_dirty = true;
      pipe_state->push_constants_client_size = MAX2(
         pipe_state->push_constants_client_size, pInfo->offset + pInfo->size);
   }
   if (pInfo->stageFlags & ANV_RT_STAGE_BITS) {
      struct anv_cmd_pipeline_state *pipe_state =
         &cmd_buffer->state.rt.base;

      memcpy(pipe_state->push_constants.client_data + pInfo->offset,
             pInfo->pValues, pInfo->size);
      pipe_state->push_constants_data_dirty = true;
      pipe_state->push_constants_client_size = MAX2(
         pipe_state->push_constants_client_size, pInfo->offset + pInfo->size);
   }

   cmd_buffer->state.push_constants_dirty |= pInfo->stageFlags;
}

static struct anv_cmd_pipeline_state *
anv_cmd_buffer_get_pipe_state(struct anv_cmd_buffer *cmd_buffer,
                              VkPipelineBindPoint bind_point)
{
   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmd_buffer->state.gfx.base;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmd_buffer->state.compute.base;
   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
      return &cmd_buffer->state.rt.base;
      break;
   default:
      UNREACHABLE("invalid bind point");
   }
}

static void
anv_cmd_buffer_push_descriptor_sets(struct anv_cmd_buffer *cmd_buffer,
                                    VkPipelineBindPoint bind_point,
                                    const VkPushDescriptorSetInfoKHR *pInfo)
{
   VK_FROM_HANDLE(vk_pipeline_layout, layout, pInfo->layout);

   assert(pInfo->set < MAX_SETS);

   struct anv_descriptor_set_layout *set_layout =
      container_of(layout->set_layouts[pInfo->set],
                   struct anv_descriptor_set_layout, vk);
   struct anv_push_descriptor_set *push_set =
      &anv_cmd_buffer_get_pipe_state(cmd_buffer,
                                     bind_point)->push_descriptor;
   if (!anv_push_descriptor_set_init(cmd_buffer, push_set, set_layout))
      return;

   anv_descriptor_set_write(cmd_buffer->device, &push_set->set,
                            pInfo->descriptorWriteCount,
                            pInfo->pDescriptorWrites);

   anv_cmd_buffer_bind_descriptor_set(cmd_buffer, bind_point,
                                      layout, pInfo->set, &push_set->set,
                                      NULL, NULL);
}

void anv_CmdPushDescriptorSet2KHR(
    VkCommandBuffer                            commandBuffer,
    const VkPushDescriptorSetInfoKHR*          pInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (pInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
      anv_cmd_buffer_push_descriptor_sets(cmd_buffer,
                                          VK_PIPELINE_BIND_POINT_COMPUTE,
                                          pInfo);
   if (pInfo->stageFlags & ANV_GRAPHICS_STAGE_BITS)
      anv_cmd_buffer_push_descriptor_sets(cmd_buffer,
                                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          pInfo);
   if (pInfo->stageFlags & ANV_RT_STAGE_BITS)
      anv_cmd_buffer_push_descriptor_sets(cmd_buffer,
                                          VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                                          pInfo);
}

void anv_CmdPushDescriptorSetWithTemplate2KHR(
    VkCommandBuffer                                commandBuffer,
    const VkPushDescriptorSetWithTemplateInfoKHR*  pInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_descriptor_update_template, template,
                  pInfo->descriptorUpdateTemplate);
   VK_FROM_HANDLE(vk_pipeline_layout, layout, pInfo->layout);

   assert(pInfo->set < MAX_PUSH_DESCRIPTORS);

   struct anv_descriptor_set_layout *set_layout =
      container_of(layout->set_layouts[pInfo->set],
                   struct anv_descriptor_set_layout, vk);
   UNUSED VkShaderStageFlags stages;
   struct anv_cmd_pipeline_state *pipe_state =
      anv_cmd_buffer_get_pipeline_layout_state(cmd_buffer, template->bind_point,
                                               set_layout, &stages);
   struct anv_push_descriptor_set *push_set = &pipe_state->push_descriptor;
   if (!anv_push_descriptor_set_init(cmd_buffer, push_set, set_layout))
      return;

   anv_descriptor_set_write_template(cmd_buffer->device, &push_set->set,
                                     template,
                                     pInfo->pData);

   anv_cmd_buffer_bind_descriptor_set(cmd_buffer, template->bind_point,
                                      layout, pInfo->set, &push_set->set,
                                      NULL, NULL);
}

void
anv_cmd_buffer_set_rt_state(struct vk_command_buffer *vk_cmd_buffer,
                            VkDeviceSize scratch_size,
                            uint32_t ray_queries,
                            const uint8_t *dynamic_descriptor_offsets)
{
   struct anv_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct anv_cmd_buffer, vk);
   struct anv_cmd_ray_tracing_state *rt = &cmd_buffer->state.rt;

   rt->scratch_size = MAX2(rt->scratch_size, scratch_size);
   if (ray_queries > 0) {
      anv_cmd_buffer_set_rt_query_buffer(cmd_buffer, &rt->base, ray_queries,
                                         ANV_RT_STAGE_BITS);
   }

   if (maybe_update_dynamic_buffers_indices(&rt->base,
                                            dynamic_descriptor_offsets)) {
      cmd_buffer->state.push_constants_dirty |= ANV_RT_STAGE_BITS;
      rt->base.push_constants_data_dirty = true;
   }
}

void
anv_cmd_buffer_set_stack_size(struct vk_command_buffer *vk_cmd_buffer,
                              VkDeviceSize stack_size)
{
   struct anv_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct anv_cmd_buffer, vk);
   struct anv_device *device = cmd_buffer->device;
   struct anv_cmd_ray_tracing_state *rt = &cmd_buffer->state.rt;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   uint32_t stack_ids_per_dss = 2048; /* TODO */

   unsigned stack_size_log2 = util_logbase2_ceil(stack_size);
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
                                            ANV_BO_ALLOC_INTERNAL, /* alloc_flags */
                                            0, /* explicit_address */
                                            &new_bo);
      ANV_DMR_BO_ALLOC(&device->vk.base, new_bo, result);
      if (result != VK_SUCCESS) {
         rt->scratch.layout.total_size = 0;
         anv_batch_set_error(&cmd_buffer->batch, result);
         return;
      }

      bo = p_atomic_cmpxchg(&device->rt_scratch_bos[bucket], NULL, new_bo);
      if (bo != NULL) {
         ANV_DMR_BO_FREE(&device->vk.base, new_bo);
         anv_device_release_bo(device, new_bo);
      } else {
         bo = new_bo;
      }
   }

   rt->scratch.bo = bo;
}

void
anv_cmd_buffer_save_state(struct anv_cmd_buffer *cmd_buffer,
                          uint32_t flags,
                          struct anv_cmd_saved_state *state)
{
   state->flags = flags;

   /* we only support the compute pipeline at the moment */
   assert(state->flags & ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE);
   const struct anv_cmd_pipeline_state *pipe_state =
      &cmd_buffer->state.compute.base;

   if (state->flags & ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE)
      state->shader = &cmd_buffer->state.compute.shader->vk;

   if (state->flags & ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_0)
      state->descriptor_set[0] = pipe_state->descriptors[0];

   if (state->flags & ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_ALL) {
      for (uint32_t i = 0; i < MAX_SETS; i++) {
         state->descriptor_set[i] = pipe_state->descriptors[i];
      }
   }

   if (state->flags & ANV_CMD_SAVED_STATE_PUSH_CONSTANTS) {
      memcpy(state->push_constants, pipe_state->push_constants.client_data,
             sizeof(state->push_constants));
   }
}

void
anv_cmd_buffer_restore_state(struct anv_cmd_buffer *cmd_buffer,
                             struct anv_cmd_saved_state *state)
{
   VkCommandBuffer cmd_buffer_ = anv_cmd_buffer_to_handle(cmd_buffer);

   assert(state->flags & ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE);
   const VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
   const VkShaderStageFlags stage_flags = VK_SHADER_STAGE_COMPUTE_BIT;
   struct anv_cmd_pipeline_state *pipe_state = &cmd_buffer->state.compute.base;

   if (state->flags & ANV_CMD_SAVED_STATE_COMPUTE_PIPELINE) {
       if (state->shader) {
          mesa_shader_stage stage = MESA_SHADER_COMPUTE;
          anv_cmd_buffer_bind_shaders(&cmd_buffer->vk, 1, &stage, &state->shader);
       } else {
          cmd_buffer->state.compute.shader = NULL;
       }
   }

   if (state->flags & ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_0) {
      if (state->descriptor_set[0]) {
         anv_cmd_buffer_bind_descriptor_set(cmd_buffer, bind_point, NULL, 0,
                                            state->descriptor_set[0], NULL,
                                            NULL);
      } else {
         pipe_state->descriptors[0] = NULL;
      }
   }

   if (state->flags & ANV_CMD_SAVED_STATE_DESCRIPTOR_SET_ALL) {
      for (uint32_t i = 0; i < MAX_SETS; i++) {
         if (state->descriptor_set[i]) {
            anv_cmd_buffer_bind_descriptor_set(cmd_buffer, bind_point, NULL, i,
                                               state->descriptor_set[i], NULL,
                                               NULL);
         } else {
            pipe_state->descriptors[i] = NULL;
         }
      }
   }

   if (state->flags & ANV_CMD_SAVED_STATE_PUSH_CONSTANTS) {
      VkPushConstantsInfoKHR push_info = {
         .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
         .layout = VK_NULL_HANDLE,
         .stageFlags = stage_flags,
         .offset = 0,
         .size = sizeof(state->push_constants),
         .pValues = state->push_constants,
      };
      anv_CmdPushConstants2KHR(cmd_buffer_, &push_info);
   }
}

void
anv_cmd_write_buffer_cp(VkCommandBuffer commandBuffer,
                        VkDeviceAddress dstAddr,
                        void *data,
                        uint32_t size)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   anv_genX(cmd_buffer->device->info, cmd_write_buffer_cp)(cmd_buffer, dstAddr,
                                                           data, size);
}

void
anv_cmd_flush_buffer_write_cp(VkCommandBuffer commandBuffer)
{
   /* TODO: cmd_write_buffer_cp is implemented with MI store +
    * ForceWriteCompletionCheck so that should make the content globally
    * observable.
    *
    * If we encounter any functional or perf bottleneck issues, let's revisit
    * this helper and add ANV_PIPE_HDC_PIPELINE_FLUSH_BIT +
    * ANV_PIPE_UNTYPED_DATAPORT_CACHE_FLUSH_BIT +
    * ANV_PIPE_DATA_CACHE_FLUSH_BIT.
    */
}

void
anv_cmd_dispatch_unaligned(VkCommandBuffer commandBuffer,
                           uint32_t invocations_x,
                           uint32_t invocations_y,
                           uint32_t invocations_z)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   anv_genX(cmd_buffer->device->info, cmd_dispatch_unaligned)
      (commandBuffer, invocations_x, invocations_y, invocations_z);
}

static void
bind_compute_shader(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_shader *shader)
{
   struct anv_cmd_compute_state *comp_state = &cmd_buffer->state.compute;

   cmd_buffer->state.compute.shader = shader;
   if (shader == NULL)
      return;

   cmd_buffer->state.compute.pipeline_dirty = true;
   set_dirty_for_bind_map(cmd_buffer, MESA_SHADER_COMPUTE, &shader->bind_map);

   update_push_descriptor_flags(&comp_state->base,
                                &cmd_buffer->state.compute.shader, 1);

   if (shader->vk.ray_queries > 0) {
      assert(cmd_buffer->device->info->verx10 >= 125);
      anv_cmd_buffer_set_rt_query_buffer(cmd_buffer, &comp_state->base,
                                         shader->vk.ray_queries,
                                         VK_SHADER_STAGE_COMPUTE_BIT);
   }
}

static void
bind_graphics_shaders(struct anv_cmd_buffer *cmd_buffer,
                      struct anv_shader *new_shaders[ANV_GRAPHICS_SHADER_STAGE_COUNT])
{
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   struct anv_gfx_dynamic_state *hw_state = &gfx->dyn_state;
   uint32_t ray_queries = 0;

   static const enum anv_cmd_dirty_bits mesa_stage_to_dirty_bit[] = {
      [MESA_SHADER_VERTEX]    = ANV_CMD_DIRTY_VS,
      [MESA_SHADER_TESS_CTRL] = ANV_CMD_DIRTY_HS,
      [MESA_SHADER_TESS_EVAL] = ANV_CMD_DIRTY_DS,
      [MESA_SHADER_GEOMETRY]  = ANV_CMD_DIRTY_GS,
      [MESA_SHADER_TASK]      = ANV_CMD_DIRTY_TASK,
      [MESA_SHADER_MESH]      = ANV_CMD_DIRTY_MESH,
      [MESA_SHADER_FRAGMENT]  = ANV_CMD_DIRTY_PS,
   };

   gfx->active_stages = 0;
   gfx->instance_multiplier = 0;

   mesa_shader_stage new_streamout_stage = MESA_SHADER_NONE;
   /* Find the last pre-rasterization stage */
   for (uint32_t i = 0; i < ANV_GRAPHICS_SHADER_STAGE_COUNT; i++) {
      mesa_shader_stage s = ANV_GRAPHICS_SHADER_STAGE_COUNT - i - 1;
      if (new_shaders[s] == NULL)
         continue;

      assert(gfx->instance_multiplier == 0 ||
             gfx->instance_multiplier == new_shaders[s]->instance_multiplier);
      gfx->active_stages |= mesa_to_vk_shader_stage(s);
      gfx->instance_multiplier = new_shaders[s]->instance_multiplier;

      if (s == MESA_SHADER_FRAGMENT ||
          s == MESA_SHADER_TASK ||
          s == MESA_SHADER_TESS_CTRL)
         continue;

      new_streamout_stage = MAX2(new_streamout_stage, s);
   }

#define diff_fix_state_stage(bit, name, old_stage, shader)              \
      do {                                                              \
         /* Fixed states should always have matching sizes */           \
         assert(old_stage == MESA_SHADER_NONE ||                        \
                gfx->shaders[old_stage] == NULL ||                      \
                gfx->shaders[old_stage]->name.len == shader->name.len); \
         /* Don't bother memcmp if the state is already dirty */        \
         if (!BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_##bit) && \
             (old_stage == MESA_SHADER_NONE ||                          \
              gfx->shaders[old_stage] == NULL ||                        \
              memcmp(&gfx->shaders[old_stage]->cmd_data[                \
                        gfx->shaders[old_stage]->name.offset],          \
                     &shader->cmd_data[                                 \
                        shader->name.offset],                           \
                     4 * shader->name.len) != 0))                       \
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_##bit);      \
      } while (0)
#define diff_var_state_stage(bit, name, old_stage, shader)              \
      do {                                                              \
         /* Don't bother memcmp if the state is already dirty */        \
         /* Also if the new state is empty, avoid marking dirty */      \
         if (!BITSET_TEST(hw_state->pack_dirty, ANV_GFX_STATE_##bit) && \
             shader->name.len != 0 &&                                   \
             (old_stage == MESA_SHADER_NONE ||                          \
              gfx->shaders[old_stage] == NULL ||                        \
              gfx->shaders[old_stage]->name.len != shader->name.len ||  \
              memcmp(&gfx->shaders[old_stage]->cmd_data[                \
                        gfx->shaders[old_stage]->name.offset],          \
                     &shader->cmd_data[shader->name.offset],            \
                     4 * shader->name.len) != 0))                       \
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_##bit);      \
      } while (0)

   if (new_streamout_stage != MESA_SHADER_NONE) {
      /* Compare the stream instructions first because we go through the
       * stages of shaders and update gfx->shaders[], we can't compare the old
       * streamout configuration from the old vertex shader with the new
       * configuration of the tessellation shader.
       */
      diff_fix_state_stage(STREAMOUT,    so,           gfx->streamout_stage, new_shaders[new_streamout_stage]);
      diff_var_state_stage(SO_DECL_LIST, so_decl_list, gfx->streamout_stage, new_shaders[new_streamout_stage]);

      gfx->streamout_stage = new_streamout_stage;
   }

#undef diff_fix_state_stage
#undef diff_var_state_stage

   uint8_t dynamic_descriptors[MAX_SETS] = {};
   for (uint32_t s = 0; s < ANV_GRAPHICS_SHADER_STAGE_COUNT; s++) {
      struct anv_shader *shader = new_shaders[s];

      if (shader != NULL) {
         gfx->active_stages |= mesa_to_vk_shader_stage(s);

         ray_queries = MAX2(ray_queries, shader->vk.ray_queries);
         if (gfx->shaders[s] != shader)
            set_dirty_for_bind_map(cmd_buffer, s, &shader->bind_map);

         for (uint32_t i = 0; i < MAX_SETS; i++) {
            assert(dynamic_descriptors[i] == 0 ||
                   dynamic_descriptors[i] ==
                   shader->bind_map.dynamic_descriptors[i]);
            dynamic_descriptors[i] = shader->bind_map.dynamic_descriptors[i];
         }
      }

      if (gfx->shaders[s] != shader)
         gfx->dirty |= mesa_stage_to_dirty_bit[s];
      else
         continue;

#define diff_fix_state(bit, name)                                       \
      do {                                                              \
         /* Fixed states should always have matching sizes */           \
         assert(gfx->shaders[s] == NULL ||                              \
                gfx->shaders[s]->name.len == shader->name.len);         \
         /* Don't bother memcmp if the state is already dirty */        \
         if (!BITSET_TEST(hw_state->pack_dirty,                         \
                          ANV_GFX_STATE_##bit) &&                       \
             (gfx->shaders[s] == NULL ||                                \
              memcmp(&gfx->shaders[s]->cmd_data[                        \
                        gfx->shaders[s]->name.offset],                  \
                     &shader->cmd_data[                                 \
                        shader->name.offset],                           \
                     4 * shader->name.len) != 0))                       \
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_##bit);      \
      } while (0)
#define diff_var_state(bit, name)                                       \
      do {                                                              \
         /* Don't bother memcmp if the state is already dirty */        \
         /* Also if the new state is empty, avoid marking dirty */      \
         if (!BITSET_TEST(hw_state->pack_dirty,                         \
                          ANV_GFX_STATE_##bit) &&                       \
             shader->name.len != 0 &&                                   \
             (gfx->shaders[s] == NULL ||                                \
              gfx->shaders[s]->name.len != shader->name.len ||          \
              memcmp(&gfx->shaders[s]->cmd_data[                        \
                        gfx->shaders[s]->name.offset],                  \
                     &shader->cmd_data[shader->name.offset],            \
                     4 * shader->name.len) != 0))                       \
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_##bit);      \
      } while (0)

      switch (s) {
      case MESA_SHADER_VERTEX:
         if (shader != NULL) {
            diff_fix_state(VS,                       vs.vs);
            diff_fix_state(VF_SGVS,                  vs.vf_sgvs);
            if (cmd_buffer->device->info->ver >= 11)
               diff_fix_state(VF_SGVS_2,             vs.vf_sgvs_2);
            diff_fix_state(VF_COMPONENT_PACKING,     vs.vf_component_packing);
            diff_var_state(VF_SGVS_INSTANCING,       vs.vf_sgvs_instancing);
            gfx->vs_source_hash = shader->prog_data->source_hash;
         } else {
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_VS);
         }
         break;

      case MESA_SHADER_TESS_CTRL:
         if (shader != NULL)
            diff_fix_state(HS,                       hs.hs);
         else
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_HS);
         break;

      case MESA_SHADER_TESS_EVAL:
         if (shader != NULL) {
            diff_fix_state(DS,                       ds.ds);
            diff_fix_state(TE,                       ds.te);
         } else {
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_DS);
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_TE);
         }
         break;

      case MESA_SHADER_GEOMETRY:
         if (shader != NULL)
            diff_fix_state(GS,                       gs.gs);
         else
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_GS);
         break;

      case MESA_SHADER_MESH:
         if (shader != NULL) {
            diff_fix_state(MESH_CONTROL,             ms.control);
            diff_fix_state(MESH_SHADER,              ms.shader);
            diff_fix_state(MESH_DISTRIB,             ms.distrib);
            diff_fix_state(CLIP_MESH,                ms.clip);
         } else {
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_MESH_CONTROL);
         }
         break;

      case MESA_SHADER_TASK:
         if (shader != NULL) {
            diff_fix_state(TASK_CONTROL,             ts.control);
            diff_fix_state(TASK_SHADER,              ts.shader);
            diff_fix_state(TASK_REDISTRIB,           ts.redistrib);
         } else {
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_TASK_CONTROL);
         }
         break;

      case MESA_SHADER_FRAGMENT:
         if (shader != NULL) {
            diff_fix_state(WM,                       ps.wm);
            diff_fix_state(PS,                       ps.ps);
            diff_fix_state(PS_EXTRA,                 ps.ps_extra);
            gfx->fs_source_hash = shader->prog_data->source_hash;
         } else {
            BITSET_SET(hw_state->pack_dirty, ANV_GFX_STATE_PS_EXTRA);
         }
         break;

      default:
         UNREACHABLE("Invalid shader stage");
      }

      gfx->shaders[s] = shader;
   }

#undef diff_fix_state
#undef diff_var_state

   update_push_descriptor_flags(&gfx->base,
                                cmd_buffer->state.gfx.shaders,
                                ARRAY_SIZE(cmd_buffer->state.gfx.shaders));

   uint8_t dynamic_descriptor_count = 0;
   uint8_t dynamic_descriptor_offsets[MAX_SETS] = {};
   for (uint32_t i = 0; i < MAX_SETS; i++) {
      dynamic_descriptor_offsets[i] = dynamic_descriptor_count;
      dynamic_descriptor_count += dynamic_descriptors[i];
   }
   if (maybe_update_dynamic_buffers_indices(&gfx->base,
                                            dynamic_descriptor_offsets)) {
      cmd_buffer->state.push_constants_dirty |= gfx->active_stages;
      gfx->base.push_constants_data_dirty = true;
   }

   if (ray_queries > 0) {
      assert(cmd_buffer->device->info->verx10 >= 125);
      anv_cmd_buffer_set_rt_query_buffer(cmd_buffer, &gfx->base, ray_queries,
                                         gfx->active_stages);
   }
}

void
anv_cmd_buffer_bind_shaders(struct vk_command_buffer *vk_cmd_buffer,
                            uint32_t stage_count,
                            const mesa_shader_stage *stages,
                            struct vk_shader ** const vk_shaders)
{
   struct anv_shader ** const shaders = (struct anv_shader ** const)vk_shaders;
   struct anv_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct anv_cmd_buffer, vk);

   /* Append any scratch surface used by the shaders */
   for (uint32_t i = 0; i < stage_count; i++) {
      if (shaders[i] != NULL) {
         anv_reloc_list_append(cmd_buffer->batch.relocs,
                               &shaders[i]->relocs);
      }
   }

   struct anv_shader *cs_shader = cmd_buffer->state.compute.shader;
   struct anv_shader *gfx_shaders[ANV_GRAPHICS_SHADER_STAGE_COUNT];
   memcpy(gfx_shaders, cmd_buffer->state.gfx.shaders, sizeof(gfx_shaders));
   for (uint32_t i = 0; i < stage_count; i++) {
      if (mesa_shader_stage_is_compute(stages[i]))
         cs_shader = shaders[i];
      else
         gfx_shaders[stages[i]] = shaders[i];
   }

   if (cs_shader != cmd_buffer->state.compute.shader)
      bind_compute_shader(cmd_buffer, cs_shader);
   if (memcmp(gfx_shaders, cmd_buffer->state.gfx.shaders, sizeof(gfx_shaders)))
      bind_graphics_shaders(cmd_buffer, gfx_shaders);
}

struct anv_companion_prev_cmd_buffer_helper
anv_begin_companion_cmd_buffer_helper(struct anv_cmd_buffer **cmd_buffer,
                                      bool needs_companion)
{
   if (likely(!needs_companion))
      return (struct anv_companion_prev_cmd_buffer_helper) { 0 };

   struct anv_cmd_buffer* prev_cmd_buffer = *cmd_buffer;
   const struct intel_device_info *info = prev_cmd_buffer->device->info;

   const VkResult result = anv_cmd_buffer_ensure_rcs_companion(prev_cmd_buffer);
   if (result != VK_SUCCESS) {
      anv_batch_set_error(&prev_cmd_buffer->batch, result);
      return (struct anv_companion_prev_cmd_buffer_helper) { 0 };
   }

   assert(prev_cmd_buffer->companion_rcs_cmd_buffer != NULL);

   /* Re-emit the aux table register in every command buffer.  This way we're
    * ensured that we have the table even if this command buffer doesn't
    * initialize any images.
    */
   if (prev_cmd_buffer->device->info->has_aux_map) {
      anv_add_pending_pipe_bits(prev_cmd_buffer->companion_rcs_cmd_buffer,
                                 ANV_PIPE_AUX_TABLE_INVALIDATE_BIT,
                                 "new cmd buffer with aux-tt");
   }

   struct anv_state syncpoint =
      anv_genX(info, cmd_buffer_begin_companion_rcs_syncpoint)(prev_cmd_buffer);

   *cmd_buffer = prev_cmd_buffer->companion_rcs_cmd_buffer;

   return (struct anv_companion_prev_cmd_buffer_helper) {
      .prev_cmd_buffer = prev_cmd_buffer,
      .syncpoint = syncpoint,
   };
}

void
anv_end_companion_cmd_buffer_helper(struct anv_cmd_buffer **cmd_buffer,
                                    struct anv_companion_prev_cmd_buffer_helper prev_cmd_buffer)
{
   if (likely(!prev_cmd_buffer.prev_cmd_buffer))
      return;

   if (prev_cmd_buffer.syncpoint.alloc_size) {
      const struct intel_device_info *info = (*cmd_buffer)->device->info;
      anv_genX(info, cmd_buffer_end_companion_rcs_syncpoint)(prev_cmd_buffer.prev_cmd_buffer,
                                                             prev_cmd_buffer.syncpoint);
   }

   *cmd_buffer = prev_cmd_buffer.prev_cmd_buffer;
}
