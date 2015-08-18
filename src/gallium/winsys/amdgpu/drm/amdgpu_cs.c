/*
 * Copyright © 2008 Jérôme Glisse
 * Copyright © 2010 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */
/*
 * Authors:
 *      Marek Olšák <maraeo@gmail.com>
 */

#include "amdgpu_cs.h"
#include "os/os_time.h"
#include <stdio.h>
#include <amdgpu_drm.h>


/* FENCES */

static struct pipe_fence_handle *
amdgpu_fence_create(struct amdgpu_ctx *ctx, unsigned ip_type,
                    unsigned ip_instance, unsigned ring)
{
   struct amdgpu_fence *fence = CALLOC_STRUCT(amdgpu_fence);

   fence->reference.count = 1;
   fence->ctx = ctx;
   fence->fence.context = ctx->ctx;
   fence->fence.ip_type = ip_type;
   fence->fence.ip_instance = ip_instance;
   fence->fence.ring = ring;
   p_atomic_inc(&ctx->refcount);
   return (struct pipe_fence_handle *)fence;
}

static void amdgpu_fence_submitted(struct pipe_fence_handle *fence,
				struct amdgpu_cs_request* request,
				uint64_t *user_fence_cpu_address)
{
   struct amdgpu_fence *rfence = (struct amdgpu_fence*)fence;

   rfence->fence.fence = request->seq_no;
   rfence->user_fence_cpu_address = user_fence_cpu_address;
}

static void amdgpu_fence_signalled(struct pipe_fence_handle *fence)
{
   struct amdgpu_fence *rfence = (struct amdgpu_fence*)fence;

   rfence->signalled = true;
}

bool amdgpu_fence_wait(struct pipe_fence_handle *fence, uint64_t timeout,
                       bool absolute)
{
   struct amdgpu_fence *rfence = (struct amdgpu_fence*)fence;
   uint32_t expired;
   int64_t abs_timeout;
   uint64_t *user_fence_cpu;
   int r;

   if (rfence->signalled)
      return true;

   if (absolute)
      abs_timeout = timeout;
   else
      abs_timeout = os_time_get_absolute_timeout(timeout);

   user_fence_cpu = rfence->user_fence_cpu_address;
   if (user_fence_cpu && *user_fence_cpu >= rfence->fence.fence) {
	rfence->signalled = true;
	return true;
   }
   /* Now use the libdrm query. */
   r = amdgpu_cs_query_fence_status(&rfence->fence,
				    abs_timeout,
				    AMDGPU_QUERY_FENCE_TIMEOUT_IS_ABSOLUTE,
				    &expired);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_cs_query_fence_status failed.\n");
      return FALSE;
   }

   if (expired) {
      /* This variable can only transition from false to true, so it doesn't
       * matter if threads race for it. */
      rfence->signalled = true;
      return true;
   }
   return false;
}

static bool amdgpu_fence_wait_rel_timeout(struct radeon_winsys *rws,
                                          struct pipe_fence_handle *fence,
                                          uint64_t timeout)
{
   return amdgpu_fence_wait(fence, timeout, false);
}

/* CONTEXTS */

static struct radeon_winsys_ctx *amdgpu_ctx_create(struct radeon_winsys *ws)
{
   struct amdgpu_ctx *ctx = CALLOC_STRUCT(amdgpu_ctx);
   int r;
   struct amdgpu_bo_alloc_request alloc_buffer = {};
   amdgpu_bo_handle buf_handle;

   ctx->ws = amdgpu_winsys(ws);
   ctx->refcount = 1;

   r = amdgpu_cs_ctx_create(ctx->ws->dev, &ctx->ctx);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_cs_ctx_create failed. (%i)\n", r);
      FREE(ctx);
      return NULL;
   }

   alloc_buffer.alloc_size = 4 * 1024;
   alloc_buffer.phys_alignment = 4 *1024;
   alloc_buffer.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;

   r = amdgpu_bo_alloc(ctx->ws->dev, &alloc_buffer, &buf_handle);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_bo_alloc failed. (%i)\n", r);
      amdgpu_cs_ctx_free(ctx->ctx);
      FREE(ctx);
      return NULL;
   }

   r = amdgpu_bo_cpu_map(buf_handle, (void**)&ctx->user_fence_cpu_address_base);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_bo_cpu_map failed. (%i)\n", r);
      amdgpu_bo_free(buf_handle);
      amdgpu_cs_ctx_free(ctx->ctx);
      FREE(ctx);
      return NULL;
   }

   memset(ctx->user_fence_cpu_address_base, 0, alloc_buffer.alloc_size);
   ctx->user_fence_bo = buf_handle;

   return (struct radeon_winsys_ctx*)ctx;
}

static void amdgpu_ctx_destroy(struct radeon_winsys_ctx *rwctx)
{
   amdgpu_ctx_unref((struct amdgpu_ctx*)rwctx);
}

static enum pipe_reset_status
amdgpu_ctx_query_reset_status(struct radeon_winsys_ctx *rwctx)
{
   struct amdgpu_ctx *ctx = (struct amdgpu_ctx*)rwctx;
   uint32_t result, hangs;
   int r;

   r = amdgpu_cs_query_reset_state(ctx->ctx, &result, &hangs);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_cs_query_reset_state failed. (%i)\n", r);
      return PIPE_NO_RESET;
   }

   switch (result) {
   case AMDGPU_CTX_GUILTY_RESET:
      return PIPE_GUILTY_CONTEXT_RESET;
   case AMDGPU_CTX_INNOCENT_RESET:
      return PIPE_INNOCENT_CONTEXT_RESET;
   case AMDGPU_CTX_UNKNOWN_RESET:
      return PIPE_UNKNOWN_CONTEXT_RESET;
   case AMDGPU_CTX_NO_RESET:
   default:
      return PIPE_NO_RESET;
   }
}

/* COMMAND SUBMISSION */

static bool amdgpu_get_new_ib(struct amdgpu_cs *cs)
{
   /* The maximum size is 4MB - 1B, which is unaligned.
    * Use aligned size 4MB - 16B. */
   const unsigned max_ib_size = (1024 * 1024 - 16) * 4;
   const unsigned min_ib_size = 24 * 1024 * 4;

   cs->base.cdw = 0;
   cs->base.buf = NULL;

   /* Allocate a new buffer for IBs if the current buffer is all used. */
   if (!cs->big_ib_buffer ||
       cs->used_ib_space + min_ib_size > cs->big_ib_buffer->size) {
      struct radeon_winsys *ws = &cs->ctx->ws->base;
      struct radeon_winsys_cs_handle *winsys_bo;

      pb_reference(&cs->big_ib_buffer, NULL);
      cs->big_ib_winsys_buffer = NULL;
      cs->ib_mapped = NULL;
      cs->used_ib_space = 0;

      cs->big_ib_buffer = ws->buffer_create(ws, max_ib_size,
                                            4096, true,
                                            RADEON_DOMAIN_GTT,
                                            RADEON_FLAG_CPU_ACCESS);
      if (!cs->big_ib_buffer)
         return false;

      winsys_bo = ws->buffer_get_cs_handle(cs->big_ib_buffer);

      cs->ib_mapped = ws->buffer_map(winsys_bo, NULL, PIPE_TRANSFER_WRITE);
      if (!cs->ib_mapped) {
         pb_reference(&cs->big_ib_buffer, NULL);
         return false;
      }

      cs->big_ib_winsys_buffer = (struct amdgpu_winsys_bo*)winsys_bo;
   }

   cs->ib.ib_mc_address = cs->big_ib_winsys_buffer->va + cs->used_ib_space;
   cs->base.buf = (uint32_t*)(cs->ib_mapped + cs->used_ib_space);
   cs->base.max_dw = (cs->big_ib_buffer->size - cs->used_ib_space) / 4;
   return true;
}

static boolean amdgpu_init_cs_context(struct amdgpu_cs *cs,
                                      enum ring_type ring_type)
{
   int i;

   switch (ring_type) {
   case RING_DMA:
      cs->request.ip_type = AMDGPU_HW_IP_DMA;
      break;

   case RING_UVD:
      cs->request.ip_type = AMDGPU_HW_IP_UVD;
      break;

   case RING_VCE:
      cs->request.ip_type = AMDGPU_HW_IP_VCE;
      break;

   case RING_COMPUTE:
      cs->request.ip_type = AMDGPU_HW_IP_COMPUTE;
      break;

   default:
   case RING_GFX:
      cs->request.ip_type = AMDGPU_HW_IP_GFX;
      break;
   }

   cs->request.number_of_ibs = 1;
   cs->request.ibs = &cs->ib;

   cs->max_num_buffers = 512;
   cs->buffers = (struct amdgpu_cs_buffer*)
                  CALLOC(1, cs->max_num_buffers * sizeof(struct amdgpu_cs_buffer));
   if (!cs->buffers) {
      return FALSE;
   }

   cs->handles = CALLOC(1, cs->max_num_buffers * sizeof(amdgpu_bo_handle));
   if (!cs->handles) {
      FREE(cs->buffers);
      return FALSE;
   }

   cs->flags = CALLOC(1, cs->max_num_buffers);
   if (!cs->flags) {
      FREE(cs->handles);
      FREE(cs->buffers);
      return FALSE;
   }

   for (i = 0; i < Elements(cs->buffer_indices_hashlist); i++) {
      cs->buffer_indices_hashlist[i] = -1;
   }
   return TRUE;
}

static void amdgpu_cs_context_cleanup(struct amdgpu_cs *cs)
{
   unsigned i;

   for (i = 0; i < cs->num_buffers; i++) {
      p_atomic_dec(&cs->buffers[i].bo->num_cs_references);
      amdgpu_winsys_bo_reference(&cs->buffers[i].bo, NULL);
      cs->handles[i] = NULL;
      cs->flags[i] = 0;
   }

   cs->num_buffers = 0;
   cs->used_gart = 0;
   cs->used_vram = 0;

   for (i = 0; i < Elements(cs->buffer_indices_hashlist); i++) {
      cs->buffer_indices_hashlist[i] = -1;
   }
}

static void amdgpu_destroy_cs_context(struct amdgpu_cs *cs)
{
   amdgpu_cs_context_cleanup(cs);
   FREE(cs->flags);
   FREE(cs->buffers);
   FREE(cs->handles);
   FREE(cs->request.dependencies);
}


static struct radeon_winsys_cs *
amdgpu_cs_create(struct radeon_winsys_ctx *rwctx,
                 enum ring_type ring_type,
                 void (*flush)(void *ctx, unsigned flags,
                               struct pipe_fence_handle **fence),
                 void *flush_ctx,
                 struct radeon_winsys_cs_handle *trace_buf)
{
   struct amdgpu_ctx *ctx = (struct amdgpu_ctx*)rwctx;
   struct amdgpu_cs *cs;

   cs = CALLOC_STRUCT(amdgpu_cs);
   if (!cs) {
      return NULL;
   }

   cs->ctx = ctx;
   cs->flush_cs = flush;
   cs->flush_data = flush_ctx;
   cs->base.ring_type = ring_type;

   if (!amdgpu_init_cs_context(cs, ring_type)) {
      FREE(cs);
      return NULL;
   }

   if (!amdgpu_get_new_ib(cs)) {
      amdgpu_destroy_cs_context(cs);
      FREE(cs);
      return NULL;
   }

   p_atomic_inc(&ctx->ws->num_cs);
   return &cs->base;
}

#define OUT_CS(cs, value) (cs)->buf[(cs)->cdw++] = (value)

int amdgpu_get_reloc(struct amdgpu_cs *cs, struct amdgpu_winsys_bo *bo)
{
   unsigned hash = bo->unique_id & (Elements(cs->buffer_indices_hashlist)-1);
   int i = cs->buffer_indices_hashlist[hash];

   /* not found or found */
   if (i == -1 || cs->buffers[i].bo == bo)
      return i;

   /* Hash collision, look for the BO in the list of relocs linearly. */
   for (i = cs->num_buffers - 1; i >= 0; i--) {
      if (cs->buffers[i].bo == bo) {
         /* Put this reloc in the hash list.
          * This will prevent additional hash collisions if there are
          * several consecutive get_reloc calls for the same buffer.
          *
          * Example: Assuming buffers A,B,C collide in the hash list,
          * the following sequence of relocs:
          *         AAAAAAAAAAABBBBBBBBBBBBBBCCCCCCCC
          * will collide here: ^ and here:   ^,
          * meaning that we should get very few collisions in the end. */
         cs->buffer_indices_hashlist[hash] = i;
         return i;
      }
   }
   return -1;
}

static unsigned amdgpu_add_reloc(struct amdgpu_cs *cs,
                                 struct amdgpu_winsys_bo *bo,
                                 enum radeon_bo_usage usage,
                                 enum radeon_bo_domain domains,
                                 unsigned priority,
                                 enum radeon_bo_domain *added_domains)
{
   struct amdgpu_cs_buffer *reloc;
   unsigned hash = bo->unique_id & (Elements(cs->buffer_indices_hashlist)-1);
   int i = -1;

   priority = MIN2(priority, 15);
   *added_domains = 0;

   i = amdgpu_get_reloc(cs, bo);

   if (i >= 0) {
      reloc = &cs->buffers[i];
      reloc->usage |= usage;
      *added_domains = domains & ~reloc->domains;
      reloc->domains |= domains;
      cs->flags[i] = MAX2(cs->flags[i], priority);
      return i;
   }

   /* New relocation, check if the backing array is large enough. */
   if (cs->num_buffers >= cs->max_num_buffers) {
      uint32_t size;
      cs->max_num_buffers += 10;

      size = cs->max_num_buffers * sizeof(struct amdgpu_cs_buffer);
      cs->buffers = realloc(cs->buffers, size);

      size = cs->max_num_buffers * sizeof(amdgpu_bo_handle);
      cs->handles = realloc(cs->handles, size);

      cs->flags = realloc(cs->flags, cs->max_num_buffers);
   }

   /* Initialize the new relocation. */
   cs->buffers[cs->num_buffers].bo = NULL;
   amdgpu_winsys_bo_reference(&cs->buffers[cs->num_buffers].bo, bo);
   cs->handles[cs->num_buffers] = bo->bo;
   cs->flags[cs->num_buffers] = priority;
   p_atomic_inc(&bo->num_cs_references);
   reloc = &cs->buffers[cs->num_buffers];
   reloc->bo = bo;
   reloc->usage = usage;
   reloc->domains = domains;

   cs->buffer_indices_hashlist[hash] = cs->num_buffers;

   *added_domains = domains;
   return cs->num_buffers++;
}

static unsigned amdgpu_cs_add_reloc(struct radeon_winsys_cs *rcs,
                                    struct radeon_winsys_cs_handle *buf,
                                    enum radeon_bo_usage usage,
                                    enum radeon_bo_domain domains,
                                    enum radeon_bo_priority priority)
{
   /* Don't use the "domains" parameter. Amdgpu doesn't support changing
    * the buffer placement during command submission.
    */
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;
   enum radeon_bo_domain added_domains;
   unsigned index = amdgpu_add_reloc(cs, bo, usage, bo->initial_domain,
                                     priority, &added_domains);

   if (added_domains & RADEON_DOMAIN_GTT)
      cs->used_gart += bo->base.size;
   if (added_domains & RADEON_DOMAIN_VRAM)
      cs->used_vram += bo->base.size;

   return index;
}

static int amdgpu_cs_get_reloc(struct radeon_winsys_cs *rcs,
                               struct radeon_winsys_cs_handle *buf)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);

   return amdgpu_get_reloc(cs, (struct amdgpu_winsys_bo*)buf);
}

static boolean amdgpu_cs_validate(struct radeon_winsys_cs *rcs)
{
   return TRUE;
}

static boolean amdgpu_cs_memory_below_limit(struct radeon_winsys_cs *rcs, uint64_t vram, uint64_t gtt)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   boolean status =
         (cs->used_gart + gtt) < cs->ctx->ws->info.gart_size * 0.7 &&
         (cs->used_vram + vram) < cs->ctx->ws->info.vram_size * 0.7;

   return status;
}

static void amdgpu_cs_do_submission(struct amdgpu_cs *cs,
                                    struct pipe_fence_handle **out_fence)
{
   struct amdgpu_winsys *ws = cs->ctx->ws;
   struct pipe_fence_handle *fence;
   int i, j, r;

   /* Create a fence. */
   fence = amdgpu_fence_create(cs->ctx,
                               cs->request.ip_type,
                               cs->request.ip_instance,
                               cs->request.ring);
   if (out_fence)
      amdgpu_fence_reference(out_fence, fence);

   cs->request.number_of_dependencies = 0;

   /* Since the kernel driver doesn't synchronize execution between different
    * rings automatically, we have to add fence dependencies manually. */
   pipe_mutex_lock(ws->bo_fence_lock);
   for (i = 0; i < cs->num_buffers; i++) {
      for (j = 0; j < RING_LAST; j++) {
         struct amdgpu_cs_fence *dep;
         unsigned idx;

         struct amdgpu_fence *bo_fence = (void *)cs->buffers[i].bo->fence[j];
         if (!bo_fence)
            continue;

         if (bo_fence->ctx == cs->ctx &&
             bo_fence->fence.ip_type == cs->request.ip_type &&
             bo_fence->fence.ip_instance == cs->request.ip_instance &&
             bo_fence->fence.ring == cs->request.ring)
            continue;

         if (amdgpu_fence_wait((void *)bo_fence, 0, false))
            continue;

         idx = cs->request.number_of_dependencies++;
         if (idx >= cs->max_dependencies) {
            unsigned size;

            cs->max_dependencies = idx + 8;
            size = cs->max_dependencies * sizeof(struct amdgpu_cs_fence);
            cs->request.dependencies = realloc(cs->request.dependencies, size);
         }

         dep = &cs->request.dependencies[idx];
         memcpy(dep, &bo_fence->fence, sizeof(*dep));
      }
   }

   cs->request.fence_info.handle = NULL;
   if (cs->request.ip_type != AMDGPU_HW_IP_UVD && cs->request.ip_type != AMDGPU_HW_IP_VCE) {
	cs->request.fence_info.handle = cs->ctx->user_fence_bo;
	cs->request.fence_info.offset = cs->base.ring_type;
   }

   r = amdgpu_cs_submit(cs->ctx->ctx, 0, &cs->request, 1);
   if (r) {
      if (r == -ENOMEM)
         fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
      else
         fprintf(stderr, "amdgpu: The CS has been rejected, "
                 "see dmesg for more information.\n");

      amdgpu_fence_signalled(fence);
   } else {
      /* Success. */
      uint64_t *user_fence = NULL;
      if (cs->request.ip_type != AMDGPU_HW_IP_UVD && cs->request.ip_type != AMDGPU_HW_IP_VCE)
         user_fence = cs->ctx->user_fence_cpu_address_base +
                      cs->request.fence_info.offset;
      amdgpu_fence_submitted(fence, &cs->request, user_fence);

      for (i = 0; i < cs->num_buffers; i++)
         amdgpu_fence_reference(&cs->buffers[i].bo->fence[cs->base.ring_type],
                                fence);
   }
   pipe_mutex_unlock(ws->bo_fence_lock);
   amdgpu_fence_reference(&fence, NULL);
}

static void amdgpu_cs_sync_flush(struct radeon_winsys_cs *rcs)
{
   /* no-op */
}

DEBUG_GET_ONCE_BOOL_OPTION(noop, "RADEON_NOOP", FALSE)

static void amdgpu_cs_flush(struct radeon_winsys_cs *rcs,
                            unsigned flags,
                            struct pipe_fence_handle **fence,
                            uint32_t cs_trace_id)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   struct amdgpu_winsys *ws = cs->ctx->ws;

   switch (cs->base.ring_type) {
   case RING_DMA:
      /* pad DMA ring to 8 DWs */
      if (ws->info.chip_class <= SI) {
         while (rcs->cdw & 7)
            OUT_CS(&cs->base, 0xf0000000); /* NOP packet */
      } else {
         while (rcs->cdw & 7)
            OUT_CS(&cs->base, 0x00000000); /* NOP packet */
      }
      break;
   case RING_GFX:
      /* pad DMA ring to 8 DWs to meet CP fetch alignment requirements
             * r6xx, requires at least 4 dw alignment to avoid a hw bug.
             */
      if (ws->info.chip_class <= SI) {
         while (rcs->cdw & 7)
            OUT_CS(&cs->base, 0x80000000); /* type2 nop packet */
      } else {
         while (rcs->cdw & 7)
            OUT_CS(&cs->base, 0xffff1000); /* type3 nop packet */
      }
      break;
   case RING_UVD:
      while (rcs->cdw & 15)
         OUT_CS(&cs->base, 0x80000000); /* type2 nop packet */
      break;
   default:
      break;
   }

   if (rcs->cdw > rcs->max_dw) {
      fprintf(stderr, "amdgpu: command stream overflowed\n");
   }

   amdgpu_cs_add_reloc(rcs, (void*)cs->big_ib_winsys_buffer,
		       RADEON_USAGE_READ, 0, RADEON_PRIO_MIN);

   /* If the CS is not empty or overflowed.... */
   if (cs->base.cdw && cs->base.cdw <= cs->base.max_dw && !debug_get_option_noop()) {
      int r;

      r = amdgpu_bo_list_create(ws->dev, cs->num_buffers,
                                cs->handles, cs->flags,
                                &cs->request.resources);

      if (r) {
         fprintf(stderr, "amdgpu: resource list creation failed (%d)\n", r);
         cs->request.resources = NULL;
	 goto cleanup;
      }

      cs->ib.size = cs->base.cdw;
      cs->used_ib_space += cs->base.cdw * 4;

      amdgpu_cs_do_submission(cs, fence);

      /* Cleanup. */
      if (cs->request.resources)
         amdgpu_bo_list_destroy(cs->request.resources);
   }

cleanup:
   amdgpu_cs_context_cleanup(cs);
   amdgpu_get_new_ib(cs);

   ws->num_cs_flushes++;
}

static void amdgpu_cs_destroy(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);

   amdgpu_destroy_cs_context(cs);
   p_atomic_dec(&cs->ctx->ws->num_cs);
   pb_reference(&cs->big_ib_buffer, NULL);
   FREE(cs);
}

static boolean amdgpu_bo_is_referenced(struct radeon_winsys_cs *rcs,
                                       struct radeon_winsys_cs_handle *_buf,
                                       enum radeon_bo_usage usage)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)_buf;

   return amdgpu_bo_is_referenced_by_cs_with_usage(cs, bo, usage);
}

void amdgpu_cs_init_functions(struct amdgpu_winsys *ws)
{
   ws->base.ctx_create = amdgpu_ctx_create;
   ws->base.ctx_destroy = amdgpu_ctx_destroy;
   ws->base.ctx_query_reset_status = amdgpu_ctx_query_reset_status;
   ws->base.cs_create = amdgpu_cs_create;
   ws->base.cs_destroy = amdgpu_cs_destroy;
   ws->base.cs_add_reloc = amdgpu_cs_add_reloc;
   ws->base.cs_get_reloc = amdgpu_cs_get_reloc;
   ws->base.cs_validate = amdgpu_cs_validate;
   ws->base.cs_memory_below_limit = amdgpu_cs_memory_below_limit;
   ws->base.cs_flush = amdgpu_cs_flush;
   ws->base.cs_is_buffer_referenced = amdgpu_bo_is_referenced;
   ws->base.cs_sync_flush = amdgpu_cs_sync_flush;
   ws->base.fence_wait = amdgpu_fence_wait_rel_timeout;
   ws->base.fence_reference = amdgpu_fence_reference;
}
