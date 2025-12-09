/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_encoder.h"

#include "kk_bo.h"
#include "kk_cmd_buffer.h"
#include "kk_queue.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"

#include "cl/kk_query.h"

static void
kk_encoder_start_internal(struct kk_encoder_internal *encoder,
                          mtl_device *device, mtl_command_queue *queue)
{
   encoder->cmd_buffer = mtl_new_command_buffer(queue);
   encoder->last_used = KK_ENC_NONE;
   encoder->fences = UTIL_DYNARRAY_INIT;
}

VkResult
kk_encoder_init(mtl_device *device, struct kk_queue *queue,
                struct kk_encoder **encoder)
{
   assert(encoder && device && queue);
   struct kk_encoder *enc = (struct kk_encoder *)malloc(sizeof(*enc));
   if (!enc)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   memset(enc, 0u, sizeof(*enc));
   enc->dev = device;
   kk_encoder_start_internal(&enc->main, device, queue->main.mtl_handle);
   kk_encoder_start_internal(&enc->pre_gfx, device, queue->pre_gfx.mtl_handle);
   enc->event = mtl_new_event(device);
   enc->imm_writes = UTIL_DYNARRAY_INIT;
   enc->copy_query_pool_result_infos = UTIL_DYNARRAY_INIT;

   *encoder = enc;
   return VK_SUCCESS;
}

mtl_render_encoder *
kk_encoder_start_render(struct kk_cmd_buffer *cmd,
                        mtl_render_pass_descriptor *descriptor,
                        uint32_t view_mask)
{
   struct kk_encoder *encoder = cmd->encoder;
   /* We must not already be in a render encoder */
   assert(encoder->main.last_used != KK_ENC_RENDER ||
          encoder->main.encoder == NULL);
   if (encoder->main.last_used != KK_ENC_RENDER) {
      kk_encoder_signal_fence_and_end(cmd);

      /* Before we start any render operation we need to ensure we have the
       * requried signals to insert pre_gfx execution before the render encoder
       * in case we need to insert commands to massage input data for things
       * like triangle fans. For this, we signal the value pre_gfx will wait on,
       * and we wait on the value pre_gfx will signal once completed.
       */
      encoder->signal_value_pre_gfx = encoder->event_value;
      mtl_encode_signal_event(encoder->main.cmd_buffer, encoder->event,
                              ++encoder->event_value);
      encoder->wait_value_pre_gfx = encoder->event_value;
      mtl_encode_wait_for_event(encoder->main.cmd_buffer, encoder->event,
                                ++encoder->event_value);

      encoder->main.encoder = mtl_new_render_command_encoder_with_descriptor(
         encoder->main.cmd_buffer, descriptor);
      if (encoder->main.wait_fence) {
         mtl_render_wait_for_fence(
            encoder->main.encoder,
            util_dynarray_top(&encoder->main.fences, mtl_fence *));
         encoder->main.wait_fence = false;
      }

      uint32_t layer_ids[KK_MAX_MULTIVIEW_VIEW_COUNT] = {};
      uint32_t count = 0u;
      u_foreach_bit(id, view_mask)
         layer_ids[count++] = id;
      if (view_mask == 0u) {
         layer_ids[count++] = 0;
      }
      mtl_set_vertex_amplification_count(encoder->main.encoder, layer_ids,
                                         count);
      encoder->main.user_heap_hash = UINT32_MAX;

      /* Bind read only data aka samplers' argument buffer. */
      struct kk_device *dev = kk_cmd_buffer_device(cmd);
      mtl_set_vertex_buffer(encoder->main.encoder, dev->samplers.table.bo->map,
                            0u, 1u);
      mtl_set_fragment_buffer(encoder->main.encoder,
                              dev->samplers.table.bo->map, 0u, 1u);
   }
   encoder->main.last_used = KK_ENC_RENDER;
   return encoder->main.encoder;
}

mtl_compute_encoder *
kk_encoder_start_compute(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   /* We must not already be in a render encoder */
   assert(encoder->main.last_used != KK_ENC_RENDER ||
          encoder->main.encoder == NULL);
   struct kk_encoder_internal *enc = &encoder->main;
   if (encoder->main.last_used != KK_ENC_COMPUTE) {
      kk_encoder_signal_fence_and_end(cmd);
      enc->encoder = mtl_new_compute_command_encoder(enc->cmd_buffer);
      if (enc->wait_fence) {
         mtl_compute_wait_for_fence(
            enc->encoder, util_dynarray_top(&enc->fences, mtl_fence *));
         enc->wait_fence = false;
      }
      enc->user_heap_hash = UINT32_MAX;

      /* Bind read only data aka samplers' argument buffer. */
      struct kk_device *dev = kk_cmd_buffer_device(cmd);
      mtl_compute_set_buffer(enc->encoder, dev->samplers.table.bo->map, 0u, 1u);
   }
   encoder->main.last_used = KK_ENC_COMPUTE;
   return encoder->main.encoder;
}

mtl_compute_encoder *
kk_encoder_start_blit(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   /* We must not already be in a render encoder */
   assert(encoder->main.last_used != KK_ENC_RENDER ||
          encoder->main.encoder == NULL);
   struct kk_encoder_internal *enc = &encoder->main;
   if (encoder->main.last_used != KK_ENC_BLIT) {
      kk_encoder_signal_fence_and_end(cmd);
      enc->encoder = mtl_new_blit_command_encoder(enc->cmd_buffer);
      if (enc->wait_fence) {
         mtl_compute_wait_for_fence(
            enc->encoder, util_dynarray_top(&enc->fences, mtl_fence *));
         enc->wait_fence = false;
      }
   }
   encoder->main.last_used = KK_ENC_BLIT;
   return encoder->main.encoder;
}

void
kk_encoder_end(struct kk_cmd_buffer *cmd)
{
   assert(cmd);

   kk_encoder_signal_fence_and_end(cmd);

   /* Let remaining render encoders run without waiting since we are done */
   mtl_encode_signal_event(cmd->encoder->pre_gfx.cmd_buffer,
                           cmd->encoder->event, cmd->encoder->event_value);
}

struct kk_imm_write_push {
   uint64_t buffer_address;
   uint32_t count;
};

void
upload_queue_writes(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *enc = cmd->encoder;

   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   uint32_t count = util_dynarray_num_elements(&enc->imm_writes, uint64_t) / 2u;
   if (count != 0) {
      mtl_compute_encoder *compute = kk_compute_encoder(cmd);
      struct kk_bo *bo = kk_cmd_allocate_buffer(cmd, enc->imm_writes.size, 8u);
      /* kk_cmd_allocate_buffer sets the cmd buffer error so we can just exit */
      if (!bo)
         return;
      memcpy(bo->cpu, enc->imm_writes.data, enc->imm_writes.size);
      struct kk_imm_write_push push_data = {
         .buffer_address = bo->gpu,
         .count = count,
      };
      kk_cmd_dispatch_pipeline(cmd, compute,
                               kk_device_lib_pipeline(dev, KK_LIB_IMM_WRITE),
                               &push_data, sizeof(push_data), count, 1, 1);
      enc->imm_writes.size = 0u;
   }

   count = util_dynarray_num_elements(&enc->copy_query_pool_result_infos,
                                      struct kk_copy_query_pool_results_info);
   if (count != 0u) {
      mtl_compute_encoder *compute = kk_compute_encoder(cmd);

      for (uint32_t i = 0u; i < count; ++i) {
         struct kk_copy_query_pool_results_info *push_data =
            util_dynarray_element(&enc->copy_query_pool_result_infos,
                                  struct kk_copy_query_pool_results_info, i);

         kk_cmd_dispatch_pipeline(
            cmd, compute, kk_device_lib_pipeline(dev, KK_LIB_COPY_QUERY),
            push_data, sizeof(*push_data), push_data->query_count, 1, 1);
      }
      enc->copy_query_pool_result_infos.size = 0u;
   }

   /* All immediate write done, reset encoder */
   kk_encoder_signal_fence_and_end(cmd);
}

void
kk_encoder_signal_fence_and_end(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   /* End pre_gfx */
   if (encoder->pre_gfx.encoder) {
      mtl_end_encoding(encoder->pre_gfx.encoder);
      mtl_release(encoder->pre_gfx.encoder);
      encoder->pre_gfx.encoder = NULL;

      /* We can start rendering once all pre-graphics work is done */
      mtl_encode_signal_event(encoder->pre_gfx.cmd_buffer, encoder->event,
                              encoder->event_value);
   }

   assert(encoder);
   enum kk_encoder_type type = encoder->main.last_used;
   struct kk_encoder_internal *enc = kk_encoder_get_internal(encoder, type);
   if (!enc || !enc->encoder)
      return;

   mtl_fence *fence = mtl_new_fence(encoder->dev);
   switch (type) {
   case KK_ENC_RENDER:
      mtl_render_update_fence(enc->encoder, fence);
      break;
   case KK_ENC_COMPUTE:
      mtl_compute_update_fence(enc->encoder, fence);
      break;
   case KK_ENC_BLIT:
      mtl_blit_update_fence(enc->encoder, fence);
      break;
   default:
      assert(0);
      break;
   }

   mtl_end_encoding(enc->encoder);
   mtl_release(enc->encoder);
   enc->encoder = NULL;
   enc->last_used = KK_ENC_NONE;
   enc->wait_fence = true;
   util_dynarray_append(&enc->fences, fence);

   if (cmd->drawable) {
      mtl_present_drawable(enc->cmd_buffer, cmd->drawable);
      cmd->drawable = NULL;
   }
   upload_queue_writes(cmd);
}

static void
kk_post_execution_release_internal(struct kk_encoder_internal *encoder)
{
   mtl_release(encoder->cmd_buffer);
   util_dynarray_foreach(&encoder->fences, mtl_fence *, fence)
      mtl_release(*fence);
   util_dynarray_fini(&encoder->fences);
}

static void
kk_post_execution_release(void *data)
{
   struct kk_encoder *encoder = data;
   kk_post_execution_release_internal(&encoder->main);
   kk_post_execution_release_internal(&encoder->pre_gfx);
   mtl_release(encoder->event);
   util_dynarray_fini(&encoder->imm_writes);
   util_dynarray_fini(&encoder->copy_query_pool_result_infos);
   free(encoder);
}

void
kk_encoder_submit(struct kk_encoder *encoder)
{
   assert(encoder);

   mtl_add_completed_handler(encoder->main.cmd_buffer,
                             kk_post_execution_release, encoder);

   mtl_command_buffer_commit(encoder->pre_gfx.cmd_buffer);
   mtl_command_buffer_commit(encoder->main.cmd_buffer);
}

mtl_render_encoder *
kk_render_encoder(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   /* Render encoders are created at vkBeginRendering only */
   assert(encoder->main.last_used == KK_ENC_RENDER && encoder->main.encoder);
   return (mtl_render_encoder *)encoder->main.encoder;
}

mtl_compute_encoder *
kk_compute_encoder(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   return encoder->main.last_used == KK_ENC_COMPUTE
             ? (mtl_blit_encoder *)encoder->main.encoder
             : kk_encoder_start_compute(cmd);
}

mtl_blit_encoder *
kk_blit_encoder(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   return encoder->main.last_used == KK_ENC_BLIT
             ? (mtl_blit_encoder *)encoder->main.encoder
             : kk_encoder_start_blit(cmd);
}

struct kk_encoder_internal *
kk_encoder_get_internal(struct kk_encoder *encoder, enum kk_encoder_type type)
{
   switch (type) {
   case KK_ENC_NONE:
      assert(encoder->main.last_used == KK_ENC_NONE);
      return NULL;
   case KK_ENC_RENDER:
      assert(encoder->main.last_used == KK_ENC_RENDER);
      return &encoder->main;
   case KK_ENC_COMPUTE:
      assert(encoder->main.last_used == KK_ENC_COMPUTE);
      return &encoder->main;
   case KK_ENC_BLIT:
      assert(encoder->main.last_used == KK_ENC_BLIT);
      return &encoder->main;
   default:
      assert(0);
      return NULL;
   }
}

static mtl_compute_encoder *
kk_encoder_pre_gfx_encoder(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   if (!encoder->pre_gfx.encoder) {
      /* Fast-forward all previous render encoders and wait for the last one */
      mtl_encode_signal_event(encoder->pre_gfx.cmd_buffer, encoder->event,
                              encoder->signal_value_pre_gfx);
      mtl_encode_wait_for_event(encoder->pre_gfx.cmd_buffer, encoder->event,
                                encoder->wait_value_pre_gfx);
      encoder->pre_gfx.encoder =
         mtl_new_compute_command_encoder(encoder->pre_gfx.cmd_buffer);
      struct kk_device *dev = kk_cmd_buffer_device(cmd);
      /* Xcode GPU capture doesn't like it when we declare an input buffer but
       * none is bound. Even if we never use it... */
      mtl_compute_set_buffer(encoder->pre_gfx.encoder,
                             dev->samplers.table.bo->map, 0u, 1u);
   }

   return encoder->pre_gfx.encoder;
}

struct kk_triangle_fan_info {
   uint64_t index_buffer;
   uint64_t out_ptr;
   uint64_t in_draw;
   uint64_t out_draw;
   uint32_t restart_index;
   uint32_t index_buffer_size_el;
   uint32_t in_el_size_B;
   uint32_t out_el_size_B;
   uint32_t flatshade_first;
   uint32_t mode;
};

static void
kk_encoder_render_triangle_fan_common(struct kk_cmd_buffer *cmd,
                                      struct kk_triangle_fan_info *info,
                                      mtl_buffer *indirect, mtl_buffer *index,
                                      uint32_t index_count,
                                      uint32_t in_el_size_B,
                                      uint32_t out_el_size_B)
{
   uint32_t index_buffer_size_B = index_count * out_el_size_B;
   uint32_t buffer_size_B =
      sizeof(VkDrawIndexedIndirectCommand) + index_buffer_size_B;
   struct kk_bo *index_buffer =
      kk_cmd_allocate_buffer(cmd, buffer_size_B, out_el_size_B);

   if (!index_buffer)
      return;

   info->out_ptr = index_buffer->gpu + sizeof(VkDrawIndexedIndirectCommand);
   info->out_draw = index_buffer->gpu;
   info->in_el_size_B = in_el_size_B;
   info->out_el_size_B = out_el_size_B;
   info->flatshade_first = true;
   mtl_compute_encoder *encoder = kk_encoder_pre_gfx_encoder(cmd);

   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   kk_cmd_dispatch_pipeline(cmd, encoder,
                            kk_device_lib_pipeline(dev, KK_LIB_TRIANGLE_FAN),
                            info, sizeof(*info), 1u, 1u, 1u);

   enum mtl_index_type index_type =
      index_size_in_bytes_to_mtl_index_type(out_el_size_B);
   mtl_render_encoder *enc = kk_render_encoder(cmd);
   mtl_draw_indexed_primitives_indirect(
      enc, cmd->state.gfx.primitive_type, index_type, index_buffer->map,
      sizeof(VkDrawIndexedIndirectCommand), index_buffer->map, 0u);
}

void
kk_encoder_render_triangle_fan_indirect(struct kk_cmd_buffer *cmd,
                                        mtl_buffer *indirect, uint64_t offset)
{
   enum mesa_prim mode = cmd->state.gfx.prim;
   uint32_t decomposed_index_count =
      u_decomposed_prims_for_vertices(mode, cmd->state.gfx.vb.max_vertices) *
      mesa_vertices_per_prim(mode);
   uint32_t el_size_B = decomposed_index_count < UINT16_MAX ? 2u : 4u;
   struct kk_triangle_fan_info info = {
      .in_draw = mtl_buffer_get_gpu_address(indirect) + offset,
      .restart_index = UINT32_MAX, /* No restart */
      .mode = mode,
   };
   kk_encoder_render_triangle_fan_common(
      cmd, &info, indirect, NULL, decomposed_index_count, el_size_B, el_size_B);
}

void
kk_encoder_render_triangle_fan_indexed_indirect(struct kk_cmd_buffer *cmd,
                                                mtl_buffer *indirect,
                                                uint64_t offset,
                                                bool increase_el_size)
{
   uint32_t el_size_B = cmd->state.gfx.index.bytes_per_index;

   enum mesa_prim mode = cmd->state.gfx.prim;
   uint32_t max_index_count =
      (mtl_buffer_get_length(cmd->state.gfx.index.handle) -
       cmd->state.gfx.index.offset) /
      el_size_B;
   uint32_t decomposed_index_count =
      u_decomposed_prims_for_vertices(mode, max_index_count) *
      mesa_vertices_per_prim(mode);

   struct kk_triangle_fan_info info = {
      .index_buffer = mtl_buffer_get_gpu_address(cmd->state.gfx.index.handle) +
                      cmd->state.gfx.index.offset,
      .in_draw = mtl_buffer_get_gpu_address(indirect) + offset,
      .restart_index =
         increase_el_size ? UINT32_MAX : cmd->state.gfx.index.restart,
      .index_buffer_size_el = max_index_count,
      .mode = mode,
   };
   uint32_t out_el_size_B = increase_el_size ? sizeof(uint32_t) : el_size_B;
   kk_encoder_render_triangle_fan_common(
      cmd, &info, indirect, cmd->state.gfx.index.handle, decomposed_index_count,
      el_size_B, out_el_size_B);
}
