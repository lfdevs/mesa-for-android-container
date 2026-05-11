/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"

#include "compiler/nir/nir.h"
#include "gfx/si_gfx.h"
#include "mm/si_mm.h"

static void decref_implicit_resource(struct hash_entry *entry)
{
   pipe_resource_reference((struct pipe_resource**)&entry->data, NULL);
}

static void si_destroy_context(struct pipe_context *context)
{
   struct si_context *sctx = (struct si_context *)context;

   si_fini_gfx_context(sctx);
   si_fini_mm_context(sctx);

   if (sctx->ctx)
      sctx->ws->ctx_destroy(sctx->ctx);

   if (sctx->dirty_implicit_resources)
      _mesa_hash_table_destroy(sctx->dirty_implicit_resources,
                               decref_implicit_resource);

   if (sctx->b.stream_uploader)
      u_upload_destroy(sctx->b.stream_uploader);
   if (sctx->b.const_uploader && sctx->b.const_uploader != sctx->b.stream_uploader)
      u_upload_destroy(sctx->b.const_uploader);
   if (sctx->cached_gtt_allocator)
      u_upload_destroy(sctx->cached_gtt_allocator);

   slab_destroy_child(&sctx->pool_transfers);
   slab_destroy_child(&sctx->pool_transfers_unsync);

   u_suballocator_destroy(&sctx->allocator_zeroed_memory);

   _mesa_hash_table_destroy(sctx->tex_handles, NULL);
   _mesa_hash_table_destroy(sctx->img_handles, NULL);

   util_dynarray_fini(&sctx->resident_tex_handles);
   util_dynarray_fini(&sctx->resident_img_handles);
   util_dynarray_fini(&sctx->resident_tex_needs_color_decompress);
   util_dynarray_fini(&sctx->resident_img_needs_color_decompress);
   util_dynarray_fini(&sctx->resident_tex_needs_depth_decompress);

   if (!(sctx->context_flags & SI_CONTEXT_FLAG_AUX))
      p_atomic_dec(&context->screen->num_contexts);

   FREE(sctx);
}

static enum pipe_reset_status si_get_reset_status(struct pipe_context *ctx)
{
   struct si_context *sctx = (struct si_context *)ctx;
   if (sctx->context_flags & SI_CONTEXT_FLAG_AUX)
      return PIPE_NO_RESET;

   bool needs_reset, reset_completed;
   enum pipe_reset_status status = sctx->ws->ctx_query_reset_status(sctx->ctx, false,
                                                                    &needs_reset, &reset_completed);

   if (status != PIPE_NO_RESET) {
      if (sctx->has_reset_been_notified && reset_completed)
         return PIPE_NO_RESET;

      sctx->has_reset_been_notified = true;

      if (!(sctx->context_flags & SI_CONTEXT_FLAG_AUX)) {
         /* Call the gallium frontend to set a no-op API dispatch. */
         if (needs_reset && sctx->device_reset_callback.reset)
            sctx->device_reset_callback.reset(sctx->device_reset_callback.data, status);
      }
   }
   return status;
}

static void si_set_device_reset_callback(struct pipe_context *ctx,
                                         const struct pipe_device_reset_callback *cb)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (cb)
      sctx->device_reset_callback = *cb;
   else
      memset(&sctx->device_reset_callback, 0, sizeof(sctx->device_reset_callback));
}

static void si_set_debug_callback(struct pipe_context *ctx, const struct util_debug_callback *cb)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_screen *screen = sctx->screen;

   util_queue_finish(&screen->shader_compiler_queue);
   util_queue_finish(&screen->shader_compiler_queue_opt_variants);

   if (cb)
      sctx->debug = *cb;
   else
      memset(&sctx->debug, 0, sizeof(sctx->debug));
}

static void si_set_log_context(struct pipe_context *ctx, struct u_log_context *log)
{
   struct si_context *sctx = (struct si_context *)ctx;
   sctx->log = log;

   if (log)
      u_log_add_auto_logger(log, si_auto_log_cs, sctx);
}

static void si_set_context_param(struct pipe_context *ctx, enum pipe_context_param param,
                                 unsigned value)
{
   struct radeon_winsys *ws = ((struct si_context *)ctx)->ws;

   switch (param) {
   case PIPE_CONTEXT_PARAM_UPDATE_THREAD_SCHEDULING:
      ws->pin_threads_to_L3_cache(ws, value);
      break;
   default:;
   }
}

static void si_set_frontend_noop(struct pipe_context *ctx, bool enable)
{
   struct si_context *sctx = (struct si_context *)ctx;

   ctx->flush(ctx, NULL, PIPE_FLUSH_ASYNC);
   sctx->is_noop = enable;
}

struct pipe_context *si_create_context(struct pipe_screen *screen, unsigned flags)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   STATIC_ASSERT(DBG_COUNT <= 64);

   struct si_context *sctx = CALLOC_STRUCT(si_context);

   if (!sctx) {
      mesa_loge("can't allocate a context");
      return NULL;
   }

   sctx->b.screen = screen; /* this must be set first */
   sctx->b.priv = NULL;
   sctx->b.destroy = si_destroy_context;
   sctx->screen = sscreen; /* Easy accessing of screen/winsys. */
   sctx->is_debug = (flags & PIPE_CONTEXT_DEBUG) != 0;
   sctx->context_flags = flags;

   slab_create_child(&sctx->pool_transfers, &sscreen->pool_transfers);
   slab_create_child(&sctx->pool_transfers_unsync, &sscreen->pool_transfers);

   sctx->ws = sscreen->ws;
   sctx->family = sscreen->info.family;
   sctx->gfx_level = sscreen->info.gfx_level;
   sctx->vcn_ip_ver = sscreen->info.vcn_ip_version;

   /* Initialize the context handle and the command stream. */
   sctx->ctx = sctx->ws->ctx_create(sctx->ws, sctx->context_flags);
   if (!sctx->ctx) {
      mesa_loge("can't create radeon_winsys_ctx");
      goto fail;
   }

   /* Initialize private allocators. */
   u_suballocator_init(&sctx->allocator_zeroed_memory, &sctx->b, 128 * 1024, 0,
                       PIPE_USAGE_DEFAULT,
                       SI_RESOURCE_FLAG_CLEAR | SI_RESOURCE_FLAG_32BIT, false);

   sctx->cached_gtt_allocator = u_upload_create(&sctx->b, 16 * 1024, 0, PIPE_USAGE_STAGING, 0);
   if (!sctx->cached_gtt_allocator) {
      mesa_loge("can't create cached_gtt_allocator");
      goto fail;
   }

   /* Initialize public allocators. Unify uploaders as follows:
    * - dGPUs: The const uploader writes to VRAM and the stream uploader writes to RAM.
    * - APUs: There is only one uploader instance writing to RAM. VRAM has the same perf on APUs.
    */
   bool is_apu = !sscreen->info.has_dedicated_vram;
   sctx->b.stream_uploader =
      u_upload_create(&sctx->b, 1024 * 1024, 0,
                      sscreen->debug_flags & DBG(NO_WC_STREAM) ? PIPE_USAGE_STAGING
                                                               : PIPE_USAGE_STREAM,
                      SI_RESOURCE_FLAG_32BIT); /* same flags as const_uploader */
   if (!sctx->b.stream_uploader) {
      mesa_loge("can't create stream_uploader");
      goto fail;
   }

   if (is_apu) {
      sctx->b.const_uploader = sctx->b.stream_uploader;
   } else {
      sctx->b.const_uploader =
         u_upload_create(&sctx->b, 256 * 1024, 0, PIPE_USAGE_DEFAULT,
                         SI_RESOURCE_FLAG_32BIT);
      if (!sctx->b.const_uploader) {
         mesa_loge("can't create const_uploader");
         goto fail;
      }
   }

   sctx->b.set_debug_callback = si_set_debug_callback;
   sctx->b.set_log_context = si_set_log_context;
   sctx->b.set_context_param = si_set_context_param;
   sctx->b.get_device_reset_status = si_get_reset_status;
   sctx->b.set_device_reset_callback = si_set_device_reset_callback;
   sctx->b.set_frontend_noop = si_set_frontend_noop;

   list_inithead(&sctx->active_queries);
   si_init_buffer_functions(sctx);
   si_init_fence_functions(sctx);
   si_init_context_texture_functions(sctx);

   /* Bindless handles. */
   sctx->tex_handles = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
   sctx->img_handles = _mesa_hash_table_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

   sctx->resident_tex_handles = UTIL_DYNARRAY_INIT;
   sctx->resident_img_handles = UTIL_DYNARRAY_INIT;
   sctx->resident_tex_needs_color_decompress = UTIL_DYNARRAY_INIT;
   sctx->resident_img_needs_color_decompress = UTIL_DYNARRAY_INIT;
   sctx->resident_tex_needs_depth_decompress = UTIL_DYNARRAY_INIT;

   sctx->dirty_implicit_resources = _mesa_pointer_hash_table_create(NULL);
   if (!sctx->dirty_implicit_resources) {
      mesa_loge("can't create dirty_implicit_resources");
      goto fail;
   }

   if (!(flags & PIPE_CONTEXT_MEDIA_ONLY)) {
      if (!si_init_gfx_context(sscreen, sctx, flags))
         goto fail;
   }

   /* PIPE_CONTEXT_COMPUTE_ONLY doesn't mean no multimedia, it means no graphics so always
    * init mm but don't fail if it reports an error.
    */
   si_init_mm_context(sscreen, sctx, flags);

   if (!(flags & SI_CONTEXT_FLAG_AUX)) {
      p_atomic_inc(&screen->num_contexts);

      /* Check if the aux_context needs to be recreated */
      for (unsigned i = 0; i < ARRAY_SIZE(sscreen->aux_contexts); i++) {
         if (!sscreen->aux_contexts[i].ctx)
            continue;

         struct si_context *saux = si_get_aux_context(sscreen, &sscreen->aux_contexts[i]);
         enum pipe_reset_status status =
            sctx->ws->ctx_query_reset_status(saux->ctx, true, NULL, NULL);

         if (status != PIPE_NO_RESET) {
            /* We lost the aux_context, create a new one */
            unsigned context_flags = saux->context_flags;
            saux->b.destroy(&saux->b);

            saux = (struct si_context *)si_create_context(&sscreen->b, context_flags);
            if (sscreen->options.aux_debug)
               saux->b.set_log_context(&saux->b, &sscreen->aux_contexts[i].log);

            sscreen->aux_contexts[i].ctx = &saux->b;
         }
         si_put_aux_context_flush(&sscreen->aux_contexts[i]);
      }

      simple_mtx_lock(&sscreen->async_compute_context_lock);
      if (sscreen->async_compute_context) {
         struct si_context *compute_ctx = (struct si_context*)sscreen->async_compute_context;
         enum pipe_reset_status status =
            sctx->ws->ctx_query_reset_status(compute_ctx->ctx, true, NULL, NULL);

         if (status != PIPE_NO_RESET) {
            sscreen->async_compute_context->destroy(sscreen->async_compute_context);
            sscreen->async_compute_context = NULL;
         }
      }
      simple_mtx_unlock(&sscreen->async_compute_context_lock);

      si_reset_debug_log_buffer(sctx);
   }

   return &sctx->b;
fail:
   mesa_loge("Failed to create a context.");
   si_destroy_context(&sctx->b);
   return NULL;
}

struct si_context *si_get_aux_context(struct si_screen *sscreen, struct si_aux_context *actx)
{
   mtx_lock(&actx->lock);
   /* Init aux_context on demand. */
   if (actx->ctx == NULL) {
      bool compute = !sscreen->info.has_graphics ||
                     actx == &sscreen->aux_context.compute_resource_init ||
                     actx == &sscreen->aux_context.shader_upload;
      actx->ctx =
         si_create_context(&sscreen->b,
                           SI_CONTEXT_FLAG_AUX | PIPE_CONTEXT_LOSE_CONTEXT_ON_RESET |
                           (sscreen->options.aux_debug ? PIPE_CONTEXT_DEBUG : 0) |
                           (compute ? PIPE_CONTEXT_COMPUTE_ONLY : 0));
      assert(actx->ctx);

      if (sscreen->options.aux_debug) {
         u_log_context_init(&actx->log);

         struct pipe_context *ctx = actx->ctx;
         ctx->set_log_context(ctx, &actx->log);
      }
   }
   return (struct si_context*)actx->ctx;
}

void si_put_aux_context_flush(struct si_aux_context *ctx)
{
   ctx->ctx->flush(ctx->ctx, NULL, 0);
   mtx_unlock(&ctx->lock);
}
