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
   fence->submission_in_progress = true;
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
   rfence->submission_in_progress = false;
}

static void amdgpu_fence_signalled(struct pipe_fence_handle *fence)
{
   struct amdgpu_fence *rfence = (struct amdgpu_fence*)fence;

   rfence->signalled = true;
   rfence->submission_in_progress = false;
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

   /* The fence might not have a number assigned if its IB is being
    * submitted in the other thread right now. Wait until the submission
    * is done. */
   if (!os_wait_until_zero_abs_timeout(&rfence->submission_in_progress,
                                       abs_timeout))
      return false;

   user_fence_cpu = rfence->user_fence_cpu_address;
   if (user_fence_cpu) {
      if (*user_fence_cpu >= rfence->fence.fence) {
         rfence->signalled = true;
         return true;
      }

      /* No timeout, just query: no need for the ioctl. */
      if (!absolute && !timeout)
         return false;
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

   if (!ctx)
      return NULL;

   ctx->ws = amdgpu_winsys(ws);
   ctx->refcount = 1;

   r = amdgpu_cs_ctx_create(ctx->ws->dev, &ctx->ctx);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_cs_ctx_create failed. (%i)\n", r);
      goto error_create;
   }

   alloc_buffer.alloc_size = ctx->ws->info.gart_page_size;
   alloc_buffer.phys_alignment = ctx->ws->info.gart_page_size;
   alloc_buffer.preferred_heap = AMDGPU_GEM_DOMAIN_GTT;

   r = amdgpu_bo_alloc(ctx->ws->dev, &alloc_buffer, &buf_handle);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_bo_alloc failed. (%i)\n", r);
      goto error_user_fence_alloc;
   }

   r = amdgpu_bo_cpu_map(buf_handle, (void**)&ctx->user_fence_cpu_address_base);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_bo_cpu_map failed. (%i)\n", r);
      goto error_user_fence_map;
   }

   memset(ctx->user_fence_cpu_address_base, 0, alloc_buffer.alloc_size);
   ctx->user_fence_bo = buf_handle;

   return (struct radeon_winsys_ctx*)ctx;

error_user_fence_map:
   amdgpu_bo_free(buf_handle);
error_user_fence_alloc:
   amdgpu_cs_ctx_free(ctx->ctx);
error_create:
   FREE(ctx);
   return NULL;
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

static bool amdgpu_get_new_ib(struct radeon_winsys *ws, struct amdgpu_ib *ib,
                              struct amdgpu_cs_ib_info *info, unsigned ib_type)
{
   struct amdgpu_winsys *aws = (struct amdgpu_winsys*)ws;
   /* Small IBs are better than big IBs, because the GPU goes idle quicker
    * and there is less waiting for buffers and fences. Proof:
    *   http://www.phoronix.com/scan.php?page=article&item=mesa-111-si&num=1
    */
   unsigned buffer_size, ib_size;

   switch (ib_type) {
   case IB_CONST_PREAMBLE:
      buffer_size = 4 * 1024 * 4;
      ib_size = 1024 * 4;
      break;
   case IB_CONST:
      buffer_size = 512 * 1024 * 4;
      ib_size = 128 * 1024 * 4;
      break;
   case IB_MAIN:
      buffer_size = 128 * 1024 * 4;
      ib_size = 20 * 1024 * 4;
      break;
   default:
      unreachable("unhandled IB type");
   }

   ib->base.cdw = 0;
   ib->base.buf = NULL;

   /* Allocate a new buffer for IBs if the current buffer is all used. */
   if (!ib->big_ib_buffer ||
       ib->used_ib_space + ib_size > ib->big_ib_buffer->size) {

      pb_reference(&ib->big_ib_buffer, NULL);
      ib->ib_mapped = NULL;
      ib->used_ib_space = 0;

      ib->big_ib_buffer = ws->buffer_create(ws, buffer_size,
                                            aws->info.gart_page_size,
                                            RADEON_DOMAIN_GTT,
                                            RADEON_FLAG_CPU_ACCESS);
      if (!ib->big_ib_buffer)
         return false;

      ib->ib_mapped = ws->buffer_map(ib->big_ib_buffer, NULL,
                                     PIPE_TRANSFER_WRITE);
      if (!ib->ib_mapped) {
         pb_reference(&ib->big_ib_buffer, NULL);
         return false;
      }
   }

   info->ib_mc_address = amdgpu_winsys_bo(ib->big_ib_buffer)->va +
                         ib->used_ib_space;
   ib->base.buf = (uint32_t*)(ib->ib_mapped + ib->used_ib_space);
   ib->base.max_dw = ib_size / 4;
   return true;
}

static boolean amdgpu_init_cs_context(struct amdgpu_cs_context *cs,
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

   for (i = 0; i < ARRAY_SIZE(cs->buffer_indices_hashlist); i++) {
      cs->buffer_indices_hashlist[i] = -1;
   }

   cs->request.number_of_ibs = 1;
   cs->request.ibs = &cs->ib[IB_MAIN];

   cs->ib[IB_CONST].flags = AMDGPU_IB_FLAG_CE;
   cs->ib[IB_CONST_PREAMBLE].flags = AMDGPU_IB_FLAG_CE |
                                     AMDGPU_IB_FLAG_PREAMBLE;

   return TRUE;
}

static void amdgpu_cs_context_cleanup(struct amdgpu_cs_context *cs)
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
   amdgpu_fence_reference(&cs->fence, NULL);

   for (i = 0; i < ARRAY_SIZE(cs->buffer_indices_hashlist); i++) {
      cs->buffer_indices_hashlist[i] = -1;
   }
}

static void amdgpu_destroy_cs_context(struct amdgpu_cs_context *cs)
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
                 void *flush_ctx)
{
   struct amdgpu_ctx *ctx = (struct amdgpu_ctx*)rwctx;
   struct amdgpu_cs *cs;

   cs = CALLOC_STRUCT(amdgpu_cs);
   if (!cs) {
      return NULL;
   }

   pipe_semaphore_init(&cs->flush_completed, 1);

   cs->ctx = ctx;
   cs->flush_cs = flush;
   cs->flush_data = flush_ctx;
   cs->ring_type = ring_type;

   if (!amdgpu_init_cs_context(&cs->csc1, ring_type)) {
      FREE(cs);
      return NULL;
   }

   if (!amdgpu_init_cs_context(&cs->csc2, ring_type)) {
      amdgpu_destroy_cs_context(&cs->csc1);
      FREE(cs);
      return NULL;
   }

   /* Set the first submission context as current. */
   cs->csc = &cs->csc1;
   cs->cst = &cs->csc2;

   if (!amdgpu_get_new_ib(&ctx->ws->base, &cs->main, &cs->csc->ib[IB_MAIN],
                          IB_MAIN)) {
      amdgpu_destroy_cs_context(&cs->csc2);
      amdgpu_destroy_cs_context(&cs->csc1);
      FREE(cs);
      return NULL;
   }

   p_atomic_inc(&ctx->ws->num_cs);
   return &cs->main.base;
}

static struct radeon_winsys_cs *
amdgpu_cs_add_const_ib(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = (struct amdgpu_cs*)rcs;
   struct amdgpu_winsys *ws = cs->ctx->ws;

   /* only one const IB can be added */
   if (cs->ring_type != RING_GFX || cs->const_ib.ib_mapped)
      return NULL;

   if (!amdgpu_get_new_ib(&ws->base, &cs->const_ib, &cs->csc->ib[IB_CONST],
                          IB_CONST))
      return NULL;

   cs->csc->request.number_of_ibs = 2;
   cs->csc->request.ibs = &cs->csc->ib[IB_CONST];

   cs->cst->request.number_of_ibs = 2;
   cs->cst->request.ibs = &cs->cst->ib[IB_CONST];

   return &cs->const_ib.base;
}

static struct radeon_winsys_cs *
amdgpu_cs_add_const_preamble_ib(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = (struct amdgpu_cs*)rcs;
   struct amdgpu_winsys *ws = cs->ctx->ws;

   /* only one const preamble IB can be added and only when the const IB has
    * also been mapped */
   if (cs->ring_type != RING_GFX || !cs->const_ib.ib_mapped ||
       cs->const_preamble_ib.ib_mapped)
      return NULL;

   if (!amdgpu_get_new_ib(&ws->base, &cs->const_preamble_ib,
                          &cs->csc->ib[IB_CONST_PREAMBLE], IB_CONST_PREAMBLE))
      return NULL;

   cs->csc->request.number_of_ibs = 3;
   cs->csc->request.ibs = &cs->csc->ib[IB_CONST_PREAMBLE];

   cs->cst->request.number_of_ibs = 3;
   cs->cst->request.ibs = &cs->cst->ib[IB_CONST_PREAMBLE];

   return &cs->const_preamble_ib.base;
}

#define OUT_CS(cs, value) (cs)->buf[(cs)->cdw++] = (value)

int amdgpu_lookup_buffer(struct amdgpu_cs_context *cs, struct amdgpu_winsys_bo *bo)
{
   unsigned hash = bo->unique_id & (ARRAY_SIZE(cs->buffer_indices_hashlist)-1);
   int i = cs->buffer_indices_hashlist[hash];

   /* not found or found */
   if (i == -1 || cs->buffers[i].bo == bo)
      return i;

   /* Hash collision, look for the BO in the list of buffers linearly. */
   for (i = cs->num_buffers - 1; i >= 0; i--) {
      if (cs->buffers[i].bo == bo) {
         /* Put this buffer in the hash list.
          * This will prevent additional hash collisions if there are
          * several consecutive lookup_buffer calls for the same buffer.
          *
          * Example: Assuming buffers A,B,C collide in the hash list,
          * the following sequence of buffers:
          *         AAAAAAAAAAABBBBBBBBBBBBBBCCCCCCCC
          * will collide here: ^ and here:   ^,
          * meaning that we should get very few collisions in the end. */
         cs->buffer_indices_hashlist[hash] = i;
         return i;
      }
   }
   return -1;
}

static unsigned amdgpu_add_buffer(struct amdgpu_cs *acs,
                                 struct amdgpu_winsys_bo *bo,
                                 enum radeon_bo_usage usage,
                                 enum radeon_bo_domain domains,
                                 unsigned priority,
                                 enum radeon_bo_domain *added_domains)
{
   struct amdgpu_cs_context *cs = acs->csc;
   struct amdgpu_cs_buffer *buffer;
   unsigned hash = bo->unique_id & (ARRAY_SIZE(cs->buffer_indices_hashlist)-1);
   int i = -1;

   assert(priority < 64);
   *added_domains = 0;

   i = amdgpu_lookup_buffer(cs, bo);

   if (i >= 0) {
      buffer = &cs->buffers[i];
      buffer->priority_usage |= 1llu << priority;
      buffer->usage |= usage;
      *added_domains = domains & ~buffer->domains;
      buffer->domains |= domains;
      cs->flags[i] = MAX2(cs->flags[i], priority / 4);
      return i;
   }

   /* New buffer, check if the backing array is large enough. */
   if (cs->num_buffers >= cs->max_num_buffers) {
      uint32_t size;
      cs->max_num_buffers += 10;

      size = cs->max_num_buffers * sizeof(struct amdgpu_cs_buffer);
      cs->buffers = realloc(cs->buffers, size);

      size = cs->max_num_buffers * sizeof(amdgpu_bo_handle);
      cs->handles = realloc(cs->handles, size);

      cs->flags = realloc(cs->flags, cs->max_num_buffers);
   }

   /* Initialize the new buffer. */
   cs->buffers[cs->num_buffers].bo = NULL;
   amdgpu_winsys_bo_reference(&cs->buffers[cs->num_buffers].bo, bo);
   cs->handles[cs->num_buffers] = bo->bo;
   cs->flags[cs->num_buffers] = priority / 4;
   p_atomic_inc(&bo->num_cs_references);
   buffer = &cs->buffers[cs->num_buffers];
   buffer->bo = bo;
   buffer->priority_usage = 1llu << priority;
   buffer->usage = usage;
   buffer->domains = domains;

   cs->buffer_indices_hashlist[hash] = cs->num_buffers;

   *added_domains = domains;
   return cs->num_buffers++;
}

static unsigned amdgpu_cs_add_buffer(struct radeon_winsys_cs *rcs,
                                    struct pb_buffer *buf,
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
   unsigned index = amdgpu_add_buffer(cs, bo, usage, bo->initial_domain,
                                     priority, &added_domains);

   if (added_domains & RADEON_DOMAIN_VRAM)
      cs->csc->used_vram += bo->base.size;
   else if (added_domains & RADEON_DOMAIN_GTT)
      cs->csc->used_gart += bo->base.size;

   return index;
}

static int amdgpu_cs_lookup_buffer(struct radeon_winsys_cs *rcs,
                               struct pb_buffer *buf)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);

   return amdgpu_lookup_buffer(cs->csc, (struct amdgpu_winsys_bo*)buf);
}

static boolean amdgpu_cs_validate(struct radeon_winsys_cs *rcs)
{
   return TRUE;
}

static boolean amdgpu_cs_memory_below_limit(struct radeon_winsys_cs *rcs, uint64_t vram, uint64_t gtt)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   struct amdgpu_winsys *ws = cs->ctx->ws;

   vram += cs->csc->used_vram;
   gtt += cs->csc->used_gart;

   /* Anything that goes above the VRAM size should go to GTT. */
   if (vram > ws->info.vram_size)
       gtt += vram - ws->info.vram_size;

   /* Now we just need to check if we have enough GTT. */
   return gtt < ws->info.gart_size * 0.7;
}

static uint64_t amdgpu_cs_query_memory_usage(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs_context *cs = amdgpu_cs(rcs)->csc;

   return cs->used_vram + cs->used_gart;
}

static unsigned amdgpu_cs_get_buffer_list(struct radeon_winsys_cs *rcs,
                                          struct radeon_bo_list_item *list)
{
    struct amdgpu_cs_context *cs = amdgpu_cs(rcs)->csc;
    int i;

    if (list) {
        for (i = 0; i < cs->num_buffers; i++) {
            pb_reference(&list[i].buf, &cs->buffers[i].bo->base);
            list[i].vm_address = cs->buffers[i].bo->va;
            list[i].priority_usage = cs->buffers[i].priority_usage;
        }
    }
    return cs->num_buffers;
}

DEBUG_GET_ONCE_BOOL_OPTION(all_bos, "RADEON_ALL_BOS", FALSE)

/* Since the kernel driver doesn't synchronize execution between different
 * rings automatically, we have to add fence dependencies manually.
 */
static void amdgpu_add_fence_dependencies(struct amdgpu_cs *acs)
{
   struct amdgpu_cs_context *cs = acs->csc;
   int i, j;

   cs->request.number_of_dependencies = 0;

   for (i = 0; i < cs->num_buffers; i++) {
      for (j = 0; j < RING_LAST; j++) {
         struct amdgpu_cs_fence *dep;
         unsigned idx;

         struct amdgpu_fence *bo_fence = (void *)cs->buffers[i].bo->fence[j];
         if (!bo_fence)
            continue;

         if (bo_fence->ctx == acs->ctx &&
             bo_fence->fence.ip_type == cs->request.ip_type &&
             bo_fence->fence.ip_instance == cs->request.ip_instance &&
             bo_fence->fence.ring == cs->request.ring)
            continue;

         if (amdgpu_fence_wait((void *)bo_fence, 0, false))
            continue;

         if (bo_fence->submission_in_progress)
            os_wait_until_zero(&bo_fence->submission_in_progress,
                               PIPE_TIMEOUT_INFINITE);

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
}

void amdgpu_cs_submit_ib(struct amdgpu_cs *acs)
{
   struct amdgpu_winsys *ws = acs->ctx->ws;
   struct amdgpu_cs_context *cs = acs->cst;
   int i, r;

   cs->request.fence_info.handle = NULL;
   if (cs->request.ip_type != AMDGPU_HW_IP_UVD &&
       cs->request.ip_type != AMDGPU_HW_IP_VCE) {
	cs->request.fence_info.handle = acs->ctx->user_fence_bo;
	cs->request.fence_info.offset = acs->ring_type;
   }

   /* Create the buffer list.
    * Use a buffer list containing all allocated buffers if requested.
    */
   if (debug_get_option_all_bos()) {
      struct amdgpu_winsys_bo *bo;
      amdgpu_bo_handle *handles;
      unsigned num = 0;

      pipe_mutex_lock(ws->global_bo_list_lock);

      handles = malloc(sizeof(handles[0]) * ws->num_buffers);
      if (!handles) {
         pipe_mutex_unlock(ws->global_bo_list_lock);
         amdgpu_cs_context_cleanup(cs);
         return;
      }

      LIST_FOR_EACH_ENTRY(bo, &ws->global_bo_list, global_list_item) {
         assert(num < ws->num_buffers);
         handles[num++] = bo->bo;
      }

      r = amdgpu_bo_list_create(ws->dev, ws->num_buffers,
                                handles, NULL,
                                &cs->request.resources);
      free(handles);
      pipe_mutex_unlock(ws->global_bo_list_lock);
   } else {
      r = amdgpu_bo_list_create(ws->dev, cs->num_buffers,
                                cs->handles, cs->flags,
                                &cs->request.resources);
   }

   if (r) {
      fprintf(stderr, "amdgpu: buffer list creation failed (%d)\n", r);
      cs->request.resources = NULL;
      amdgpu_fence_signalled(cs->fence);
      goto cleanup;
   }

   r = amdgpu_cs_submit(acs->ctx->ctx, 0, &cs->request, 1);
   if (r) {
      if (r == -ENOMEM)
         fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
      else
         fprintf(stderr, "amdgpu: The CS has been rejected, "
                 "see dmesg for more information.\n");

      amdgpu_fence_signalled(cs->fence);
   } else {
      /* Success. */
      uint64_t *user_fence = NULL;
      if (cs->request.ip_type != AMDGPU_HW_IP_UVD &&
          cs->request.ip_type != AMDGPU_HW_IP_VCE)
         user_fence = acs->ctx->user_fence_cpu_address_base +
                      cs->request.fence_info.offset;
      amdgpu_fence_submitted(cs->fence, &cs->request, user_fence);
   }

   /* Cleanup. */
   if (cs->request.resources)
      amdgpu_bo_list_destroy(cs->request.resources);

cleanup:
   for (i = 0; i < cs->num_buffers; i++)
      p_atomic_dec(&cs->buffers[i].bo->num_active_ioctls);

   amdgpu_cs_context_cleanup(cs);
}

/* Make sure the previous submission is completed. */
void amdgpu_cs_sync_flush(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);

   /* Wait for any pending ioctl of this CS to complete. */
   if (cs->ctx->ws->thread) {
      /* wait and set the semaphore to "busy" */
      pipe_semaphore_wait(&cs->flush_completed);
      /* set the semaphore to "idle" */
      pipe_semaphore_signal(&cs->flush_completed);
   }
}

DEBUG_GET_ONCE_BOOL_OPTION(noop, "RADEON_NOOP", FALSE)

static void amdgpu_cs_flush(struct radeon_winsys_cs *rcs,
                            unsigned flags,
                            struct pipe_fence_handle **fence)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   struct amdgpu_winsys *ws = cs->ctx->ws;

   switch (cs->ring_type) {
   case RING_DMA:
      /* pad DMA ring to 8 DWs */
      while (rcs->cdw & 7)
         OUT_CS(rcs, 0x00000000); /* NOP packet */
      break;
   case RING_GFX:
      /* pad GFX ring to 8 DWs to meet CP fetch alignment requirements */
      while (rcs->cdw & 7)
         OUT_CS(rcs, 0xffff1000); /* type3 nop packet */

      /* Also pad the const IB. */
      if (cs->const_ib.ib_mapped)
         while (!cs->const_ib.base.cdw || (cs->const_ib.base.cdw & 7))
            OUT_CS(&cs->const_ib.base, 0xffff1000); /* type3 nop packet */

      if (cs->const_preamble_ib.ib_mapped)
         while (!cs->const_preamble_ib.base.cdw || (cs->const_preamble_ib.base.cdw & 7))
            OUT_CS(&cs->const_preamble_ib.base, 0xffff1000);
      break;
   case RING_UVD:
      while (rcs->cdw & 15)
         OUT_CS(rcs, 0x80000000); /* type2 nop packet */
      break;
   default:
      break;
   }

   if (rcs->cdw > rcs->max_dw) {
      fprintf(stderr, "amdgpu: command stream overflowed\n");
   }

   amdgpu_cs_add_buffer(rcs, cs->main.big_ib_buffer,
                        RADEON_USAGE_READ, 0, RADEON_PRIO_IB1);

   if (cs->const_ib.ib_mapped)
      amdgpu_cs_add_buffer(rcs, cs->const_ib.big_ib_buffer,
                           RADEON_USAGE_READ, 0, RADEON_PRIO_IB1);

   if (cs->const_preamble_ib.ib_mapped)
      amdgpu_cs_add_buffer(rcs, cs->const_preamble_ib.big_ib_buffer,
                           RADEON_USAGE_READ, 0, RADEON_PRIO_IB1);

   /* If the CS is not empty or overflowed.... */
   if (cs->main.base.cdw && cs->main.base.cdw <= cs->main.base.max_dw &&
       !debug_get_option_noop()) {
      struct amdgpu_cs_context *cur = cs->csc;
      unsigned i, num_buffers = cur->num_buffers;

      /* Set IB sizes. */
      cur->ib[IB_MAIN].size = cs->main.base.cdw;
      cs->main.used_ib_space += cs->main.base.cdw * 4;

      if (cs->const_ib.ib_mapped) {
         cur->ib[IB_CONST].size = cs->const_ib.base.cdw;
         cs->const_ib.used_ib_space += cs->const_ib.base.cdw * 4;
      }

      if (cs->const_preamble_ib.ib_mapped) {
         cur->ib[IB_CONST_PREAMBLE].size = cs->const_preamble_ib.base.cdw;
         cs->const_preamble_ib.used_ib_space += cs->const_preamble_ib.base.cdw * 4;
      }

      /* Create a fence. */
      amdgpu_fence_reference(&cur->fence, NULL);
      cur->fence = amdgpu_fence_create(cs->ctx,
                                           cur->request.ip_type,
                                           cur->request.ip_instance,
                                           cur->request.ring);
      if (fence)
         amdgpu_fence_reference(fence, cur->fence);

      /* Prepare buffers. */
      pipe_mutex_lock(ws->bo_fence_lock);
      amdgpu_add_fence_dependencies(cs);
      for (i = 0; i < num_buffers; i++) {
         p_atomic_inc(&cur->buffers[i].bo->num_active_ioctls);
         amdgpu_fence_reference(&cur->buffers[i].bo->fence[cs->ring_type],
                                cur->fence);
      }
      pipe_mutex_unlock(ws->bo_fence_lock);

      amdgpu_cs_sync_flush(rcs);

      /* Swap command streams. "cst" is going to be submitted. */
      cs->csc = cs->cst;
      cs->cst = cur;

      /* Submit. */
      if (ws->thread && (flags & RADEON_FLUSH_ASYNC)) {
         /* Set the semaphore to "busy". */
         pipe_semaphore_wait(&cs->flush_completed);
         amdgpu_ws_queue_cs(ws, cs);
      } else {
         amdgpu_cs_submit_ib(cs);
      }
   } else {
      amdgpu_cs_context_cleanup(cs->csc);
   }

   amdgpu_get_new_ib(&ws->base, &cs->main, &cs->csc->ib[IB_MAIN], IB_MAIN);
   if (cs->const_ib.ib_mapped)
      amdgpu_get_new_ib(&ws->base, &cs->const_ib, &cs->csc->ib[IB_CONST],
                        IB_CONST);
   if (cs->const_preamble_ib.ib_mapped)
      amdgpu_get_new_ib(&ws->base, &cs->const_preamble_ib,
                        &cs->csc->ib[IB_CONST_PREAMBLE], IB_CONST_PREAMBLE);

   ws->num_cs_flushes++;
}

static void amdgpu_cs_destroy(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);

   amdgpu_cs_sync_flush(rcs);
   pipe_semaphore_destroy(&cs->flush_completed);
   p_atomic_dec(&cs->ctx->ws->num_cs);
   pb_reference(&cs->main.big_ib_buffer, NULL);
   pb_reference(&cs->const_ib.big_ib_buffer, NULL);
   pb_reference(&cs->const_preamble_ib.big_ib_buffer, NULL);
   amdgpu_destroy_cs_context(&cs->csc1);
   amdgpu_destroy_cs_context(&cs->csc2);
   FREE(cs);
}

static boolean amdgpu_bo_is_referenced(struct radeon_winsys_cs *rcs,
                                       struct pb_buffer *_buf,
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
   ws->base.cs_add_const_ib = amdgpu_cs_add_const_ib;
   ws->base.cs_add_const_preamble_ib = amdgpu_cs_add_const_preamble_ib;
   ws->base.cs_destroy = amdgpu_cs_destroy;
   ws->base.cs_add_buffer = amdgpu_cs_add_buffer;
   ws->base.cs_lookup_buffer = amdgpu_cs_lookup_buffer;
   ws->base.cs_validate = amdgpu_cs_validate;
   ws->base.cs_memory_below_limit = amdgpu_cs_memory_below_limit;
   ws->base.cs_query_memory_usage = amdgpu_cs_query_memory_usage;
   ws->base.cs_get_buffer_list = amdgpu_cs_get_buffer_list;
   ws->base.cs_flush = amdgpu_cs_flush;
   ws->base.cs_is_buffer_referenced = amdgpu_bo_is_referenced;
   ws->base.cs_sync_flush = amdgpu_cs_sync_flush;
   ws->base.fence_wait = amdgpu_fence_wait_rel_timeout;
   ws->base.fence_reference = amdgpu_fence_reference;
}
