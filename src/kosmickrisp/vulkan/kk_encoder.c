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

#include "kosmickrisp/libkk/kk_query.h"
#include "libkk_shaders.h"

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

   upload_queue_writes(cmd);
   if (encoder->main.last_used != KK_ENC_RENDER) {
      kk_encoder_signal_fence_and_end(cmd);

      /* Before we start any render operation we need to ensure we have the
       * requried signals to insert pre_gfx execution before the render encoder
       * in case we need to insert commands to massage input data for things
       * like triangle fans. For this, we signal the value pre_gfx will wait on,
       * and we wait on the value pre_gfx will signal once completed.
       */
      encoder->wait_value_pre_gfx = encoder->event_value;
      mtl_encode_wait_for_event(encoder->main.cmd_buffer, encoder->event,
                                ++encoder->event_value);
      encoder->signal_value_pre_gfx = encoder->event_value;

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

void
kk_encoder_end(struct kk_cmd_buffer *cmd)
{
   assert(cmd);

   kk_encoder_signal_fence_and_end(cmd);

   /* Ensure all writes are done before we really end encoding. We could have
    * writes to be done in the following case: endRenderPasss->endQuery->cmdEnd */
   upload_queue_writes(cmd);
   kk_encoder_signal_fence_and_end(cmd);

   struct kk_encoder *encoder = cmd->encoder;
   if (encoder->last_signaled_value_pre_gfx != encoder->signal_value_pre_gfx) {
      mtl_encode_signal_event(encoder->pre_gfx.cmd_buffer, encoder->event,
                              encoder->signal_value_pre_gfx);
      encoder->last_signaled_value_pre_gfx = encoder->signal_value_pre_gfx;
   }
}

struct kk_imm_write_push {
   uint64_t buffer_address;
   uint32_t count;
};

void
upload_queue_writes(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *enc = cmd->encoder;

   if (enc->imm_writes.size == 0u &&
       enc->copy_query_pool_result_infos.size == 0u)
      return;

   uint32_t count =
      util_dynarray_num_elements(&enc->imm_writes, struct libkk_imm_write);
   if (count != 0) {
      uint64_t addr =
         kk_pool_upload(cmd, enc->imm_writes.data, enc->imm_writes.size,
                        sizeof(struct libkk_imm_write))
            .gpu;
      /* kk_cmd_allocate_buffer sets the cmd buffer error so we can just exit */
      if (!addr)
         return;
      struct mtl_size grid = {count, 1, 1};
      libkk_write_u32_array(cmd, grid, false, addr);
      enc->imm_writes.size = 0u;
   }

   count = util_dynarray_num_elements(&enc->copy_query_pool_result_infos,
                                      struct libkk_copy_queries_args);
   if (count != 0u) {
      for (uint32_t i = 0u; i < count; ++i) {
         struct kk_copy_query_pool_results_info *push_data =
            util_dynarray_element(&enc->copy_query_pool_result_infos,
                                  struct kk_copy_query_pool_results_info, i);

         struct mtl_size grid = {push_data->query_count, 1, 1};
         const struct libkk_copy_queries_args *data =
            (const struct libkk_copy_queries_args *)push_data;
         libkk_copy_queries_struct(cmd, grid, false, *data);
      }
      enc->copy_query_pool_result_infos.size = 0u;
   }
}

static void
kk_encoder_signal_fence(struct kk_encoder *encoder)
{
   assert(encoder);
   enum kk_encoder_type type = encoder->main.last_used;
   struct kk_encoder_internal *main_enc = &encoder->main;
   if (!main_enc->encoder)
      return;

   mtl_fence *fence = mtl_new_fence(encoder->dev);
   switch (type) {
   case KK_ENC_RENDER:
      mtl_render_update_fence(main_enc->encoder, fence);
      break;
   case KK_ENC_COMPUTE:
      mtl_compute_update_fence(main_enc->encoder, fence);
      break;
   case KK_ENC_BLIT:
      mtl_blit_update_fence(main_enc->encoder, fence);
      break;
   default:
      assert(0);
      break;
   }
   main_enc->wait_fence = true;
   util_dynarray_append(&main_enc->fences, fence);
}

static void
kk_encoder_internal_end_encoding(struct kk_encoder_internal *internal_encoder)
{
   assert(internal_encoder && internal_encoder->encoder);

   mtl_end_encoding(internal_encoder->encoder);
   mtl_release(internal_encoder->encoder);
   internal_encoder->encoder = NULL;
   internal_encoder->last_used = KK_ENC_NONE;
}

void
kk_encoder_signal_fence_and_end(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   assert(encoder);
   /* End pre_gfx */
   if (encoder->pre_gfx.encoder) {
      kk_encoder_internal_end_encoding(&encoder->pre_gfx);

      /* We can start rendering once all pre-graphics work is done */
      mtl_encode_signal_event(encoder->pre_gfx.cmd_buffer, encoder->event,
                              encoder->signal_value_pre_gfx);
      encoder->last_signaled_value_pre_gfx = encoder->signal_value_pre_gfx;
   }

   if (encoder->main.last_used != KK_ENC_NONE) {
      /* kk_encoder_internal_end_encoding will change this value to NONE */
      enum kk_encoder_type last = encoder->main.last_used;
      kk_encoder_signal_fence(encoder);
      kk_encoder_internal_end_encoding(&encoder->main);
      if (last == KK_ENC_RENDER) {
         mtl_encode_signal_event(encoder->main.cmd_buffer, encoder->event,
                                 ++encoder->event_value);
      }
   }

   if (cmd->drawable) {
      mtl_present_drawable(encoder->main.cmd_buffer, cmd->drawable);
      cmd->drawable = NULL;
   }
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

   struct kk_graphics_state *gfx = &cmd->state.gfx;
   if (gfx->need_to_start_render_pass) {
      mtl_render_pass_descriptor_set_default_raster_sample_count(
         cmd->state.gfx.render_pass_descriptor, gfx->sample_count);
      gfx->need_to_start_render_pass = false;
      kk_encoder_start_render(cmd, gfx->render_pass_descriptor,
                              gfx->render.view_mask);
   }
   /* Render encoders are created at vkBeginRendering only */
   assert(encoder->main.last_used == KK_ENC_RENDER && encoder->main.encoder);
   return (mtl_render_encoder *)encoder->main.encoder;
}

mtl_compute_encoder *
kk_compute_encoder(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder_internal *encoder = &cmd->encoder->main;

   if (encoder->last_used == KK_ENC_COMPUTE)
      return (mtl_compute_encoder *)encoder->encoder;

   kk_encoder_signal_fence_and_end(cmd);
   encoder->encoder = mtl_new_compute_command_encoder(encoder->cmd_buffer);
   if (encoder->wait_fence) {
      mtl_compute_wait_for_fence(
         encoder->encoder, util_dynarray_top(&encoder->fences, mtl_fence *));
      encoder->wait_fence = false;
   }
   encoder->user_heap_hash = UINT32_MAX;

   /* Bind read only data aka samplers' argument buffer. */
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   mtl_compute_set_buffer(encoder->encoder, dev->samplers.table.bo->map, 0u,
                          1u);
   encoder->last_used = KK_ENC_COMPUTE;
   upload_queue_writes(cmd);
   return (mtl_compute_encoder *)encoder->encoder;
}

mtl_blit_encoder *
kk_blit_encoder(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder_internal *encoder = &cmd->encoder->main;

   if (encoder->last_used == KK_ENC_BLIT)
      return (mtl_blit_encoder *)encoder->encoder;

   upload_queue_writes(cmd);
   kk_encoder_signal_fence_and_end(cmd);
   encoder->encoder = mtl_new_blit_command_encoder(encoder->cmd_buffer);
   if (encoder->wait_fence) {
      mtl_compute_wait_for_fence(
         encoder->encoder, util_dynarray_top(&encoder->fences, mtl_fence *));
      encoder->wait_fence = false;
   }
   encoder->last_used = KK_ENC_BLIT;
   return (mtl_blit_encoder *)encoder->encoder;
}

mtl_compute_encoder *
kk_encoder_pre_gfx_encoder(struct kk_cmd_buffer *cmd)
{
   struct kk_encoder *encoder = cmd->encoder;
   if (!encoder->pre_gfx.encoder) {
      /* Fast-forward all previous render encoders and wait for the last one */
      uint32_t last_signaled = (encoder->wait_value_pre_gfx - 1u);
      if (encoder->wait_value_pre_gfx != 0u &&
          encoder->last_signaled_value_pre_gfx != last_signaled) {
         mtl_encode_signal_event(encoder->pre_gfx.cmd_buffer, encoder->event,
                                 last_signaled);
         encoder->last_signaled_value_pre_gfx = last_signaled;
      }
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
