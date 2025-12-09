/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "d3d12_blit.h"
#include "d3d12_cmd_signature.h"
#include "d3d12_context.h"
#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
#include "d3d12_compiler.h"
#include "d3d12_compute_transforms.h"
#include "nir_to_dxil.h"
#ifdef _WIN32
#include "dxil_validator.h"
#endif
#endif // HAVE_GALLIUM_D3D12_GRAPHICS
#include "d3d12_debug.h"
#include "d3d12_fence.h"
#include "d3d12_format.h"
#include "d3d12_query.h"
#include "d3d12_resource.h"
#include "d3d12_root_signature.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"
#ifdef HAVE_GALLIUM_D3D12_VIDEO
#include "d3d12_video_dec.h"
#include "d3d12_video_enc.h"
#include "d3d12_video_proc.h"
#include "d3d12_video_buffer.h"
#endif
#include "indices/u_primconvert.h"
#include "util/u_atomic.h"
#include "util/u_blitter.h"
#include "util/u_dual_blend.h"
#include "util/u_framebuffer.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/u_pstipple.h"
#include "util/u_sample_positions.h"
#include "util/u_dl.h"
#include <dxguids/dxguids.h>
#include <string.h>
#include "d3d12_interop_public.h"

static void
d3d12_context_destroy(struct pipe_context *pctx)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   if (ctx->priority_manager)
   {
      struct d3d12_screen *screen = d3d12_screen(pctx->screen);
      if (ctx->priority_manager->unregister_work_queue(ctx->priority_manager, screen->cmdqueue) != 0)
      {
         debug_printf("D3D12: Failed to unregister command queue with frontend priority manager\n");
         assert(false);
      }

      mtx_destroy(&ctx->priority_manager_lock);
   }

   struct d3d12_screen *screen = d3d12_screen(pctx->screen);
   mtx_lock(&screen->submit_mutex);
   list_del(&ctx->context_list_entry);
   if (ctx->id != D3D12_CONTEXT_NO_ID)
      screen->context_id_list[screen->context_id_count++] = ctx->id;
   mtx_unlock(&screen->submit_mutex);


#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if ((screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0) && !(ctx->flags & PIPE_CONTEXT_MEDIA_ONLY)) {
      util_blitter_destroy(ctx->blitter); // Must be called before d3d12_destroy_batch
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   // Batch must be destroyed before the rest of the state objects below
   d3d12_end_batch(ctx, d3d12_current_batch(ctx));
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->batches); ++i)
      d3d12_destroy_batch(ctx, &ctx->batches[i]);
   ctx->cmdlist->Release();
   if (ctx->cmdlist2)
      ctx->cmdlist2->Release();
   if (ctx->cmdlist8)
      ctx->cmdlist8->Release();

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if ((screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0) && !(ctx->flags & PIPE_CONTEXT_MEDIA_ONLY)) {
#ifdef _WIN32
      dxil_destroy_validator(ctx->dxil_validator);
#endif // _WIN32

#ifndef _GAMING_XBOX
      if (ctx->dev_config)
         ctx->dev_config->Release();
#endif // _GAMING_XBOX

      if (ctx->timestamp_query)
         pctx->destroy_query(pctx, ctx->timestamp_query);

      util_unreference_framebuffer_state(&ctx->fb);
      d3d12_compute_pipeline_state_cache_destroy(ctx);
      d3d12_root_signature_cache_destroy(ctx);
      d3d12_cmd_signature_cache_destroy(ctx);
      d3d12_compute_transform_cache_destroy(ctx);
      d3d12_descriptor_pool_free(ctx->sampler_pool);
      d3d12_gs_variant_cache_destroy(ctx);
      d3d12_tcs_variant_cache_destroy(ctx);
      d3d12_gfx_pipeline_state_cache_destroy(ctx);
      util_primconvert_destroy(ctx->primconvert);
      pipe_resource_reference(&ctx->pstipple.texture, nullptr);
      pipe_sampler_view_reference(&ctx->pstipple.sampler_view, nullptr);
      util_dynarray_fini(&ctx->recently_destroyed_bos);
      FREE(ctx->pstipple.sampler_cso);
      if (pctx->stream_uploader)
         u_upload_destroy(pctx->stream_uploader);
      if (pctx->const_uploader)
         u_upload_destroy(pctx->const_uploader);
      if (!ctx->queries_disabled) {
         u_suballocator_destroy(&ctx->query_allocator);
      }  
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   slab_destroy_child(&ctx->transfer_pool);
   slab_destroy_child(&ctx->transfer_pool_unsync);
   d3d12_context_state_table_destroy(ctx);

   FREE(ctx);
}

bool
d3d12_flush_cmdlist(struct d3d12_context *ctx)
{
   if (!ctx->has_commands)
      return false;

   d3d12_end_batch(ctx, d3d12_current_batch(ctx));

   ctx->current_batch_idx++;
   if (ctx->current_batch_idx == ARRAY_SIZE(ctx->batches))
      ctx->current_batch_idx = 0;

   d3d12_start_batch(ctx, d3d12_current_batch(ctx));
   ctx->has_commands = false;
   return true;
}

void
d3d12_flush_cmdlist_and_wait(struct d3d12_context *ctx)
{
   struct d3d12_batch *batch = d3d12_current_batch(ctx);

   d3d12_foreach_submitted_batch(ctx, old_batch)
      d3d12_reset_batch(ctx, old_batch, OS_TIMEOUT_INFINITE);
   if (d3d12_flush_cmdlist(ctx))
      d3d12_reset_batch(ctx, batch, OS_TIMEOUT_INFINITE);
}

static void
d3d12_flush(struct pipe_context *pipe,
            struct pipe_fence_handle **fence,
            unsigned flags)
{
   struct d3d12_context *ctx = d3d12_context(pipe);
   struct d3d12_batch *batch = d3d12_current_batch(ctx);

   bool flushed = d3d12_flush_cmdlist(ctx);

   if (fence) {
      if (flushed)
         d3d12_fence_reference((struct d3d12_fence **)fence, batch->fence);
      else
         *fence = (pipe_fence_handle *)d3d12_create_fence(d3d12_screen(ctx->base.screen), false);
   }
}

static void
d3d12_flush_resource(struct pipe_context *pctx,
                     struct pipe_resource *pres)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_resource *res = d3d12_resource(pres);

   d3d12_transition_resource_state(ctx, res,
                                   D3D12_RESOURCE_STATE_COMMON,
                                   D3D12_TRANSITION_FLAG_INVALIDATE_BINDINGS);
   d3d12_apply_resource_states(ctx, false);
}

static void
d3d12_signal(struct pipe_context *pipe,
             struct pipe_fence_handle *pfence,
             uint64_t value)
{
   struct d3d12_screen *screen = d3d12_screen(pipe->screen);
   struct d3d12_fence *fence = d3d12_fence(pfence);
   d3d12_flush_cmdlist(d3d12_context(pipe));
   d3d12_fence_signal_impl(fence, screen->cmdqueue, value);
}

static void
d3d12_wait(struct pipe_context *pipe, struct pipe_fence_handle *pfence, uint64_t value)
{
   struct d3d12_screen *screen = d3d12_screen(pipe->screen);
   struct d3d12_fence *fence = d3d12_fence(pfence);
   d3d12_flush_cmdlist(d3d12_context(pipe));
   d3d12_fence_wait_impl(fence, screen->cmdqueue, value);
}

static void
d3d12_replace_buffer_storage(struct pipe_context *pctx,
   struct pipe_resource *pdst,
   struct pipe_resource *psrc,
   unsigned minimum_num_rebinds,
   uint32_t rebind_mask,
   uint32_t delete_buffer_id)
{
   struct d3d12_resource *dst = d3d12_resource(pdst);
   struct d3d12_resource *src = d3d12_resource(psrc);

   struct d3d12_bo *old_bo = dst->bo;
   d3d12_bo_reference(src->bo);
   dst->bo = src->bo;
   p_atomic_inc(&dst->generation_id);
#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   struct d3d12_context *ctx = d3d12_context(pctx);
   if ((d3d12_screen(pctx->screen)->max_feature_level >= D3D_FEATURE_LEVEL_11_0)
      && !(ctx->flags & PIPE_CONTEXT_MEDIA_ONLY))
      d3d12_rebind_buffer(ctx, dst);
#endif // HAVE_GALLIUM_D3D12_GRAPHICS
   d3d12_bo_unreference(old_bo);
}

static void
d3d12_memory_barrier(struct pipe_context *pctx, unsigned flags)
{
#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   struct d3d12_context *ctx = d3d12_context(pctx);
   if (flags & PIPE_BARRIER_VERTEX_BUFFER)
      ctx->state_dirty |= D3D12_DIRTY_VERTEX_BUFFERS;
   if (flags & PIPE_BARRIER_INDEX_BUFFER)
      ctx->state_dirty |= D3D12_DIRTY_INDEX_BUFFER;
   if (flags & PIPE_BARRIER_FRAMEBUFFER)
      ctx->state_dirty |= D3D12_DIRTY_FRAMEBUFFER;
   if (flags & PIPE_BARRIER_STREAMOUT_BUFFER)
      ctx->state_dirty |= D3D12_DIRTY_STREAM_OUTPUT;

   /* TODO:
    * PIPE_BARRIER_INDIRECT_BUFFER
    */

   for (unsigned i = 0; i < D3D12_GFX_SHADER_STAGES; ++i) {
      if (flags & PIPE_BARRIER_CONSTANT_BUFFER)
         ctx->shader_dirty[i] |= D3D12_SHADER_DIRTY_CONSTBUF;
      if (flags & PIPE_BARRIER_TEXTURE)
         ctx->shader_dirty[i] |= D3D12_SHADER_DIRTY_SAMPLER_VIEWS;
      if (flags & PIPE_BARRIER_SHADER_BUFFER)
         ctx->shader_dirty[i] |= D3D12_SHADER_DIRTY_SSBO;
      if (flags & PIPE_BARRIER_IMAGE)
         ctx->shader_dirty[i] |= D3D12_SHADER_DIRTY_IMAGE;
   }
   
   /* Indicate that UAVs shouldn't override transitions. Ignore barriers that are only
    * for UAVs or other fixed-function state that doesn't need a draw to resolve.
    */
   const unsigned ignored_barrier_flags =
      PIPE_BARRIER_IMAGE |
      PIPE_BARRIER_SHADER_BUFFER |
      PIPE_BARRIER_UPDATE |
      PIPE_BARRIER_MAPPED_BUFFER |
      PIPE_BARRIER_QUERY_BUFFER;
   d3d12_current_batch(ctx)->pending_memory_barrier = (flags & ~ignored_barrier_flags) != 0;

   if (flags & (PIPE_BARRIER_IMAGE | PIPE_BARRIER_SHADER_BUFFER)) {
      D3D12_RESOURCE_BARRIER uavBarrier;
      uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      uavBarrier.UAV.pResource = nullptr;
      ctx->cmdlist->ResourceBarrier(1, &uavBarrier);
      ctx->has_commands = true;
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS
}

static void
d3d12_texture_barrier(struct pipe_context *pctx, unsigned flags)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   /* D3D doesn't really have an equivalent in the legacy barrier model. When using enhanced barriers,
    * this could be a more specific global barrier. But for now, just flush the world with an aliasing barrier. */
   D3D12_RESOURCE_BARRIER aliasingBarrier;
   aliasingBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
   aliasingBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
   aliasingBarrier.Aliasing.pResourceBefore = nullptr;
   aliasingBarrier.Aliasing.pResourceAfter = nullptr;
   ctx->cmdlist->ResourceBarrier(1, &aliasingBarrier);
   ctx->has_commands = true;
}

static enum pipe_reset_status
d3d12_get_reset_status(struct pipe_context *pctx)
{
   struct d3d12_screen *screen = d3d12_screen(pctx->screen);
   HRESULT hr = screen->dev->GetDeviceRemovedReason();
   switch (hr) {
   case DXGI_ERROR_DEVICE_HUNG:
   case DXGI_ERROR_INVALID_CALL:
      return PIPE_GUILTY_CONTEXT_RESET;
   case DXGI_ERROR_DEVICE_RESET:
      return PIPE_INNOCENT_CONTEXT_RESET;
   default:
      return SUCCEEDED(hr) ? PIPE_NO_RESET : PIPE_UNKNOWN_CONTEXT_RESET;
   }
}

#ifdef HAVE_GALLIUM_D3D12_VIDEO
struct pipe_video_codec*
d3d12_video_create_codec(struct pipe_context *context,
                         const struct pipe_video_codec *templat)
{
    if (templat->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
        return d3d12_video_encoder_create_encoder(context, templat);
    } else if (templat->entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
        return d3d12_video_create_decoder(context, templat);
    } else if (templat->entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {
        return d3d12_video_processor_create(context, templat);
    } else {
        debug_printf("D3D12: Unsupported video codec entrypoint %d\n", templat->entrypoint);
        return nullptr;
    }
}
#endif

int
d3d12_context_set_queue_priority(struct d3d12_context_queue_priority_manager* manager,
                                     ID3D12CommandQueue *d3d12_queue,
                                     const uint32_t* global_priority,
                                     const uint32_t* local_priority)
{
   if (!global_priority || !local_priority)
      return -1;

   struct d3d12_context* ctx12 = d3d12_context(manager->context);
   mtx_lock(&ctx12->priority_manager_lock);
   {
      // Set the queue priority
      ComPtr<ID3D12CommandQueue1> prio_iface;
      if(FAILED(d3d12_queue->QueryInterface(IID_PPV_ARGS(&prio_iface))))
      {
         mtx_unlock(&ctx12->priority_manager_lock);
         return -1;
      }

      D3D12_COMMAND_QUEUE_GLOBAL_PRIORITY global_priority_val = static_cast<D3D12_COMMAND_QUEUE_GLOBAL_PRIORITY>(*global_priority);
      if(FAILED(prio_iface->SetGlobalPriority(global_priority_val)))
      {
         mtx_unlock(&ctx12->priority_manager_lock);
         return -1;
      }

      D3D12_COMMAND_QUEUE_PROCESS_PRIORITY local_priority_val = static_cast<D3D12_COMMAND_QUEUE_PROCESS_PRIORITY>(*local_priority);
      if(FAILED(prio_iface->SetProcessPriority(local_priority_val)))
      {
         mtx_unlock(&ctx12->priority_manager_lock);
         return -1;
      }
   }

   mtx_unlock(&ctx12->priority_manager_lock);
   return 0;
}

int
d3d12_context_get_queue_priority(struct d3d12_context_queue_priority_manager* manager,
                                     ID3D12CommandQueue *d3d12_queue,
                                     uint32_t *global_priority,
                                     uint32_t *local_priority)
{
   struct d3d12_context* ctx12 = d3d12_context(manager->context);

   mtx_lock(&ctx12->priority_manager_lock);
   {
      ComPtr<ID3D12CommandQueue1> prio_iface;
      if (FAILED(d3d12_queue->QueryInterface(IID_PPV_ARGS(&prio_iface))))
      {
         mtx_unlock(&ctx12->priority_manager_lock);
         return -1;
      }

      if (global_priority)
      {
         D3D12_COMMAND_QUEUE_GLOBAL_PRIORITY global_priority_val = {};
         if (FAILED(prio_iface->GetGlobalPriority(&global_priority_val)))
         {
            mtx_unlock(&ctx12->priority_manager_lock);
            return -1;
         }
         *global_priority = static_cast<uint32_t>(global_priority_val);
      }

      if (local_priority)
      {
         D3D12_COMMAND_QUEUE_PROCESS_PRIORITY local_priority_val = {};
         if (FAILED(prio_iface->GetProcessPriority(&local_priority_val)))
         {
            mtx_unlock(&ctx12->priority_manager_lock);
            return -1;
         }
         *local_priority = static_cast<uint32_t>(local_priority_val);
      }
   }
   mtx_unlock(&ctx12->priority_manager_lock);
   return 0;
}

int
d3d12_context_set_queue_priority_manager(struct pipe_context *ctx, struct d3d12_context_queue_priority_manager *priority_manager)
{
   if (ctx && priority_manager)
   {
      struct d3d12_context *d3d12_ctx = d3d12_context(ctx);
      d3d12_ctx->priority_manager = (struct d3d12_context_queue_priority_manager*) priority_manager;

      // Validate that the frontend has set all required function pointers
      assert ( d3d12_ctx->priority_manager->register_work_queue );
      assert ( d3d12_ctx->priority_manager->unregister_work_queue );

      // Initialize the priority manager lock
      if (thrd_success != mtx_init(&d3d12_ctx->priority_manager_lock, mtx_plain))
      {
         debug_printf("D3D12: Failed to initialize context priority manager lock\n");
         return 1;
      }

      //
      // Fill the function pointers for the context queue priority manager that the frontend expects
      //

      d3d12_ctx->priority_manager->context = ctx;
      d3d12_ctx->priority_manager->set_queue_priority = d3d12_context_set_queue_priority;
      d3d12_ctx->priority_manager->get_queue_priority = d3d12_context_get_queue_priority;

      // Register the context's command queue with the frontend's priority manager
      struct d3d12_screen *screen = d3d12_screen(ctx->screen);
      if (d3d12_ctx->priority_manager->register_work_queue(d3d12_ctx->priority_manager, screen->cmdqueue) != 0)
      {
         debug_printf("D3D12: Failed to register command queue with frontend priority manager\n");
         return 1;
      }
   }

   return 0;
}

int
d3d12_video_encoder_set_max_async_queue_depth(struct pipe_context *ctx, uint32_t max_async_depth)
{
   if (max_async_depth > 8) {
      debug_printf("d3d12_video_encoder_set_max_async_queue_depth: max_async_depth must be between 1 and 8\n");
      return -1;
   }

   struct d3d12_context *d3d12_ctx = d3d12_context(ctx);
   d3d12_ctx->max_video_encoding_async_depth = max_async_depth;
   return 0;
}

struct pipe_context *
d3d12_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);
   if (FAILED(screen->dev->GetDeviceRemovedReason())) {
      /* Attempt recovery, but this may fail */
      screen->deinit(screen);
      if (!screen->init(screen)) {
         debug_printf("D3D12: failed to reset screen\n");
         return nullptr;
      }
   }

   if ((screen->max_feature_level < D3D_FEATURE_LEVEL_11_0) &&
      !(flags & PIPE_CONTEXT_MEDIA_ONLY))
   {
      debug_printf("D3D12: Underlying screen maximum supported feature level is lower than D3D_FEATURE_LEVEL_11_0. The caller to context_create must pass PIPE_CONTEXT_MEDIA_ONLY in flags.\n");
      return NULL;
   }

#ifndef HAVE_GALLIUM_D3D12_VIDEO
   if (flags & PIPE_CONTEXT_MEDIA_ONLY)
   {
      debug_printf("D3D12: context_create passed PIPE_CONTEXT_MEDIA_ONLY in flags but no media support found.\n");
      return NULL;
   }
#endif // ifndef HAVE_GALLIUM_D3D12_VIDEO

   struct d3d12_context *ctx = CALLOC_STRUCT(d3d12_context);
   if (!ctx)
      return NULL;

   ctx->max_video_encoding_async_depth = static_cast<uint32_t>(debug_get_num_option("D3D12_VIDEO_ENC_ASYNC_DEPTH", 8));
   ctx->base.screen = pscreen;
   ctx->base.priv = priv;

   ctx->base.destroy = d3d12_context_destroy;
   ctx->base.flush = d3d12_flush;
   ctx->base.flush_resource = d3d12_flush_resource;
   ctx->base.fence_server_signal = d3d12_signal;
   ctx->base.fence_server_sync = d3d12_wait;
   ctx->base.memory_barrier = d3d12_memory_barrier;
   ctx->base.texture_barrier = d3d12_texture_barrier;

   ctx->base.get_device_reset_status = d3d12_get_reset_status;
   ctx->flags = flags;
   d3d12_context_resource_init(&ctx->base);
   d3d12_context_copy_init(&ctx->base);

#ifdef HAVE_GALLIUM_D3D12_VIDEO
   ctx->base.create_video_codec = d3d12_video_create_codec;
   ctx->base.create_video_buffer = d3d12_video_buffer_create;
   ctx->base.video_buffer_from_handle = d3d12_video_buffer_from_handle;
#endif

   slab_create_child(&ctx->transfer_pool, &d3d12_screen(pscreen)->transfer_pool);
   slab_create_child(&ctx->transfer_pool_unsync, &d3d12_screen(pscreen)->transfer_pool);

   d3d12_context_state_table_init(ctx);

   ctx->queries_disabled = true; // Disabled by default, re-enable if supported FL below

#ifdef HAVE_GALLIUM_D3D12_GRAPHICS
   if ((screen->max_feature_level >= D3D_FEATURE_LEVEL_11_0) && !(flags & PIPE_CONTEXT_MEDIA_ONLY)) {
#ifndef _GAMING_XBOX
      (void)screen->dev->QueryInterface(&ctx->dev_config);
#endif

      d3d12_context_blit_init(&ctx->base);

      u_suballocator_init(&ctx->so_allocator, &ctx->base, 4096, 0,
                     PIPE_USAGE_DEFAULT,
                     0, false);

      ctx->has_flat_varyings = false;
      ctx->missing_dual_src_outputs = false;
      ctx->manual_depth_range = false;
      
      d3d12_compute_pipeline_state_cache_init(ctx);
      d3d12_root_signature_cache_init(ctx);
      d3d12_cmd_signature_cache_init(ctx);
      d3d12_compute_transform_cache_init(ctx);

      ctx->D3D12SerializeVersionedRootSignature =
         (PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE)util_dl_get_proc_address(screen->d3d12_mod, "D3D12SerializeVersionedRootSignature");

      ctx->base.stream_uploader = u_upload_create_default(&ctx->base);
      ctx->base.const_uploader = u_upload_create_default(&ctx->base);

      ctx->base.get_sample_position = u_default_get_sample_position;
      ctx->base.get_sample_position = u_default_get_sample_position;

     d3d12_init_graphics_context_functions(ctx);

      ctx->gfx_pipeline_state.sample_mask = ~0;

      d3d12_context_query_init(&ctx->base);
      ctx->queries_disabled = false;

      struct primconvert_config cfg = {};
      cfg.primtypes_mask = 1 << MESA_PRIM_POINTS |
                           1 << MESA_PRIM_LINES |
                           1 << MESA_PRIM_LINE_STRIP |
                           1 << MESA_PRIM_TRIANGLES |
                           1 << MESA_PRIM_TRIANGLE_STRIP;
      cfg.restart_primtypes_mask = cfg.primtypes_mask;
      cfg.fixed_prim_restart = true;
      ctx->primconvert = util_primconvert_create_config(&ctx->base, &cfg);
      if (!ctx->primconvert) {
         debug_printf("D3D12: failed to create primconvert\n");
         return NULL;
      }

      d3d12_gfx_pipeline_state_cache_init(ctx);
      d3d12_gs_variant_cache_init(ctx);
      d3d12_tcs_variant_cache_init(ctx);

      ctx->sampler_pool = d3d12_descriptor_pool_new(screen,
                                                   D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                                   64);
      if (!ctx->sampler_pool) {
         FREE(ctx);
         return NULL;
      }
      d3d12_init_null_sampler(ctx);

      ctx->blitter = util_blitter_create(&ctx->base);
      if (!ctx->blitter)
         return NULL;

      if (!d3d12_init_polygon_stipple(&ctx->base)) {
         debug_printf("D3D12: failed to initialize polygon stipple resources\n");
         FREE(ctx);
         return NULL;
      }
#ifdef _WIN32
         if (!(d3d12_debug & D3D12_DEBUG_EXPERIMENTAL) ||
            (d3d12_debug & D3D12_DEBUG_DISASS))
            ctx->dxil_validator = dxil_create_validator(NULL);
#endif
   }
#endif // HAVE_GALLIUM_D3D12_GRAPHICS

   ctx->submit_id = (uint64_t)p_atomic_add_return(&screen->ctx_count, 1) << 32ull;

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->batches); ++i) {
      if (!d3d12_init_batch(ctx, &ctx->batches[i])) {
         FREE(ctx);
         return NULL;
      }
   }
   d3d12_start_batch(ctx, &ctx->batches[0]);

   mtx_lock(&screen->submit_mutex);
   list_addtail(&ctx->context_list_entry, &screen->context_list);
   if (screen->context_id_count > 0)
      ctx->id = screen->context_id_list[--screen->context_id_count];
   else
      ctx->id = D3D12_CONTEXT_NO_ID;
   mtx_unlock(&screen->submit_mutex);

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->batches); ++i) {
      ctx->batches[i].ctx_id = ctx->id;
      ctx->batches[i].ctx_index = i;
   }

   if (flags & PIPE_CONTEXT_PREFER_THREADED) {
      struct pipe_context *ret = threaded_context_create(&ctx->base,
         &screen->transfer_pool,
         d3d12_replace_buffer_storage,
         NULL,
         &ctx->threaded_context);
      if (ctx->threaded_context) {
         ctx->threaded_context->bytes_replaced_limit = 1024 * 1024 * 1024; /* 1GiB */
         threaded_context_init_bytes_mapped_limit(ctx->threaded_context, 4);
      }
      return ret;
   }

   return &ctx->base;
}
