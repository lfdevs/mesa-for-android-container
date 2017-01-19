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

#include "amd/common/sid.h"

DEBUG_GET_ONCE_BOOL_OPTION(noop, "RADEON_NOOP", false)

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
      return false;
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

static struct pipe_fence_handle *
amdgpu_cs_get_next_fence(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   struct pipe_fence_handle *fence = NULL;

   if (debug_get_option_noop())
      return NULL;

   if (cs->next_fence) {
      amdgpu_fence_reference(&fence, cs->next_fence);
      return fence;
   }

   fence = amdgpu_fence_create(cs->ctx,
                               cs->csc->request.ip_type,
                               cs->csc->request.ip_instance,
                               cs->csc->request.ring);
   if (!fence)
      return NULL;

   amdgpu_fence_reference(&cs->next_fence, fence);
   return fence;
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

static bool amdgpu_cs_has_user_fence(struct amdgpu_cs_context *cs)
{
   return cs->request.ip_type != AMDGPU_HW_IP_UVD &&
          cs->request.ip_type != AMDGPU_HW_IP_VCE;
}

static bool amdgpu_cs_has_chaining(struct amdgpu_cs *cs)
{
   return cs->ctx->ws->info.chip_class >= CIK &&
          cs->ring_type == RING_GFX;
}

static unsigned amdgpu_cs_epilog_dws(enum ring_type ring_type)
{
   if (ring_type == RING_GFX)
      return 4; /* for chaining */

   return 0;
}

int amdgpu_lookup_buffer(struct amdgpu_cs_context *cs, struct amdgpu_winsys_bo *bo)
{
   unsigned hash = bo->unique_id & (ARRAY_SIZE(cs->buffer_indices_hashlist)-1);
   int i = cs->buffer_indices_hashlist[hash];
   struct amdgpu_cs_buffer *buffers;
   int num_buffers;

   if (bo->bo) {
      buffers = cs->real_buffers;
      num_buffers = cs->num_real_buffers;
   } else {
      buffers = cs->slab_buffers;
      num_buffers = cs->num_slab_buffers;
   }

   /* not found or found */
   if (i < 0 || (i < num_buffers && buffers[i].bo == bo))
      return i;

   /* Hash collision, look for the BO in the list of buffers linearly. */
   for (i = num_buffers - 1; i >= 0; i--) {
      if (buffers[i].bo == bo) {
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

static int
amdgpu_lookup_or_add_real_buffer(struct amdgpu_cs *acs, struct amdgpu_winsys_bo *bo)
{
   struct amdgpu_cs_context *cs = acs->csc;
   struct amdgpu_cs_buffer *buffer;
   unsigned hash;
   int idx = amdgpu_lookup_buffer(cs, bo);

   if (idx >= 0)
      return idx;

   /* New buffer, check if the backing array is large enough. */
   if (cs->num_real_buffers >= cs->max_real_buffers) {
      unsigned new_max =
         MAX2(cs->max_real_buffers + 16, (unsigned)(cs->max_real_buffers * 1.3));
      struct amdgpu_cs_buffer *new_buffers;
      amdgpu_bo_handle *new_handles;
      uint8_t *new_flags;

      new_buffers = MALLOC(new_max * sizeof(*new_buffers));
      new_handles = MALLOC(new_max * sizeof(*new_handles));
      new_flags = MALLOC(new_max * sizeof(*new_flags));

      if (!new_buffers || !new_handles || !new_flags) {
         fprintf(stderr, "amdgpu_lookup_or_add_buffer: allocation failed\n");
         FREE(new_buffers);
         FREE(new_handles);
         FREE(new_flags);
         return -1;
      }

      memcpy(new_buffers, cs->real_buffers, cs->num_real_buffers * sizeof(*new_buffers));
      memcpy(new_handles, cs->handles, cs->num_real_buffers * sizeof(*new_handles));
      memcpy(new_flags, cs->flags, cs->num_real_buffers * sizeof(*new_flags));

      FREE(cs->real_buffers);
      FREE(cs->handles);
      FREE(cs->flags);

      cs->max_real_buffers = new_max;
      cs->real_buffers = new_buffers;
      cs->handles = new_handles;
      cs->flags = new_flags;
   }

   idx = cs->num_real_buffers;
   buffer = &cs->real_buffers[idx];

   memset(buffer, 0, sizeof(*buffer));
   amdgpu_winsys_bo_reference(&buffer->bo, bo);
   cs->handles[idx] = bo->bo;
   cs->flags[idx] = 0;
   p_atomic_inc(&bo->num_cs_references);
   cs->num_real_buffers++;

   hash = bo->unique_id & (ARRAY_SIZE(cs->buffer_indices_hashlist)-1);
   cs->buffer_indices_hashlist[hash] = idx;

   if (bo->initial_domain & RADEON_DOMAIN_VRAM)
      acs->main.base.used_vram += bo->base.size;
   else if (bo->initial_domain & RADEON_DOMAIN_GTT)
      acs->main.base.used_gart += bo->base.size;

   return idx;
}

static int amdgpu_lookup_or_add_slab_buffer(struct amdgpu_cs *acs,
                                            struct amdgpu_winsys_bo *bo)
{
   struct amdgpu_cs_context *cs = acs->csc;
   struct amdgpu_cs_buffer *buffer;
   unsigned hash;
   int idx = amdgpu_lookup_buffer(cs, bo);
   int real_idx;

   if (idx >= 0)
      return idx;

   real_idx = amdgpu_lookup_or_add_real_buffer(acs, bo->u.slab.real);
   if (real_idx < 0)
      return -1;

   /* New buffer, check if the backing array is large enough. */
   if (cs->num_slab_buffers >= cs->max_slab_buffers) {
      unsigned new_max =
         MAX2(cs->max_slab_buffers + 16, (unsigned)(cs->max_slab_buffers * 1.3));
      struct amdgpu_cs_buffer *new_buffers;

      new_buffers = REALLOC(cs->slab_buffers,
                            cs->max_slab_buffers * sizeof(*new_buffers),
                            new_max * sizeof(*new_buffers));
      if (!new_buffers) {
         fprintf(stderr, "amdgpu_lookup_or_add_slab_buffer: allocation failed\n");
         return -1;
      }

      cs->max_slab_buffers = new_max;
      cs->slab_buffers = new_buffers;
   }

   idx = cs->num_slab_buffers;
   buffer = &cs->slab_buffers[idx];

   memset(buffer, 0, sizeof(*buffer));
   amdgpu_winsys_bo_reference(&buffer->bo, bo);
   buffer->u.slab.real_idx = real_idx;
   p_atomic_inc(&bo->num_cs_references);
   cs->num_slab_buffers++;

   hash = bo->unique_id & (ARRAY_SIZE(cs->buffer_indices_hashlist)-1);
   cs->buffer_indices_hashlist[hash] = idx;

   return idx;
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
   struct amdgpu_cs *acs = amdgpu_cs(rcs);
   struct amdgpu_cs_context *cs = acs->csc;
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;
   struct amdgpu_cs_buffer *buffer;
   int index;

   if (!bo->bo) {
      index = amdgpu_lookup_or_add_slab_buffer(acs, bo);
      if (index < 0)
         return 0;

      buffer = &cs->slab_buffers[index];
      buffer->usage |= usage;

      usage &= ~RADEON_USAGE_SYNCHRONIZED;
      index = buffer->u.slab.real_idx;
   } else {
      index = amdgpu_lookup_or_add_real_buffer(acs, bo);
      if (index < 0)
         return 0;
   }

   buffer = &cs->real_buffers[index];
   buffer->u.real.priority_usage |= 1llu << priority;
   buffer->usage |= usage;
   cs->flags[index] = MAX2(cs->flags[index], priority / 4);
   return index;
}

static bool amdgpu_ib_new_buffer(struct amdgpu_winsys *ws, struct amdgpu_ib *ib)
{
   struct pb_buffer *pb;
   uint8_t *mapped;
   unsigned buffer_size;

   /* Always create a buffer that is at least as large as the maximum seen IB
    * size, aligned to a power of two (and multiplied by 4 to reduce internal
    * fragmentation if chaining is not available). Limit to 512k dwords, which
    * is the largest power of two that fits into the size field of the
    * INDIRECT_BUFFER packet.
    */
   if (amdgpu_cs_has_chaining(amdgpu_cs_from_ib(ib)))
      buffer_size = 4 *util_next_power_of_two(ib->max_ib_size);
   else
      buffer_size = 4 *util_next_power_of_two(4 * ib->max_ib_size);

   buffer_size = MIN2(buffer_size, 4 * 512 * 1024);

   switch (ib->ib_type) {
   case IB_CONST_PREAMBLE:
      buffer_size = MAX2(buffer_size, 4 * 1024);
      break;
   case IB_CONST:
      buffer_size = MAX2(buffer_size, 16 * 1024 * 4);
      break;
   case IB_MAIN:
      buffer_size = MAX2(buffer_size, 8 * 1024 * 4);
      break;
   default:
      unreachable("unhandled IB type");
   }

   pb = ws->base.buffer_create(&ws->base, buffer_size,
                               ws->info.gart_page_size,
                               RADEON_DOMAIN_GTT,
                               RADEON_FLAG_CPU_ACCESS);
   if (!pb)
      return false;

   mapped = ws->base.buffer_map(pb, NULL, PIPE_TRANSFER_WRITE);
   if (!mapped) {
      pb_reference(&pb, NULL);
      return false;
   }

   pb_reference(&ib->big_ib_buffer, pb);
   pb_reference(&pb, NULL);

   ib->ib_mapped = mapped;
   ib->used_ib_space = 0;

   return true;
}

static unsigned amdgpu_ib_max_submit_dwords(enum ib_type ib_type)
{
   switch (ib_type) {
   case IB_MAIN:
      /* Smaller submits means the GPU gets busy sooner and there is less
       * waiting for buffers and fences. Proof:
       *   http://www.phoronix.com/scan.php?page=article&item=mesa-111-si&num=1
       */
      return 20 * 1024;
   case IB_CONST_PREAMBLE:
   case IB_CONST:
      /* There isn't really any reason to limit CE IB size beyond the natural
       * limit implied by the main IB, except perhaps GTT size. Just return
       * an extremely large value that we never get anywhere close to.
       */
      return 16 * 1024 * 1024;
   default:
      unreachable("bad ib_type");
   }
}

static bool amdgpu_get_new_ib(struct radeon_winsys *ws, struct amdgpu_cs *cs,
                              enum ib_type ib_type)
{
   struct amdgpu_winsys *aws = (struct amdgpu_winsys*)ws;
   /* Small IBs are better than big IBs, because the GPU goes idle quicker
    * and there is less waiting for buffers and fences. Proof:
    *   http://www.phoronix.com/scan.php?page=article&item=mesa-111-si&num=1
    */
   struct amdgpu_ib *ib = NULL;
   struct amdgpu_cs_ib_info *info = &cs->csc->ib[ib_type];
   unsigned ib_size = 0;

   switch (ib_type) {
   case IB_CONST_PREAMBLE:
      ib = &cs->const_preamble_ib;
      ib_size = 256 * 4;
      break;
   case IB_CONST:
      ib = &cs->const_ib;
      ib_size = 8 * 1024 * 4;
      break;
   case IB_MAIN:
      ib = &cs->main;
      ib_size = 4 * 1024 * 4;
      break;
   default:
      unreachable("unhandled IB type");
   }

   if (!amdgpu_cs_has_chaining(cs)) {
      ib_size = MAX2(ib_size,
                     4 * MIN2(util_next_power_of_two(ib->max_ib_size),
                              amdgpu_ib_max_submit_dwords(ib_type)));
   }

   ib->max_ib_size = ib->max_ib_size - ib->max_ib_size / 32;

   ib->base.prev_dw = 0;
   ib->base.num_prev = 0;
   ib->base.current.cdw = 0;
   ib->base.current.buf = NULL;

   /* Allocate a new buffer for IBs if the current buffer is all used. */
   if (!ib->big_ib_buffer ||
       ib->used_ib_space + ib_size > ib->big_ib_buffer->size) {
      if (!amdgpu_ib_new_buffer(aws, ib))
         return false;
   }

   info->ib_mc_address = amdgpu_winsys_bo(ib->big_ib_buffer)->va +
                         ib->used_ib_space;
   info->size = 0;
   ib->ptr_ib_size = &info->size;

   amdgpu_cs_add_buffer(&cs->main.base, ib->big_ib_buffer,
                        RADEON_USAGE_READ, 0, RADEON_PRIO_IB1);

   ib->base.current.buf = (uint32_t*)(ib->ib_mapped + ib->used_ib_space);

   ib_size = ib->big_ib_buffer->size - ib->used_ib_space;
   ib->base.current.max_dw = ib_size / 4 - amdgpu_cs_epilog_dws(cs->ring_type);
   return true;
}

static void amdgpu_ib_finalize(struct amdgpu_ib *ib)
{
   *ib->ptr_ib_size |= ib->base.current.cdw;
   ib->used_ib_space += ib->base.current.cdw * 4;
   ib->max_ib_size = MAX2(ib->max_ib_size, ib->base.prev_dw + ib->base.current.cdw);
}

static bool amdgpu_init_cs_context(struct amdgpu_cs_context *cs,
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

   for (i = 0; i < ARRAY_SIZE(cs->buffer_indices_hashlist); i++) {
      cs->buffer_indices_hashlist[i] = -1;
   }

   cs->request.number_of_ibs = 1;
   cs->request.ibs = &cs->ib[IB_MAIN];

   cs->ib[IB_CONST].flags = AMDGPU_IB_FLAG_CE;
   cs->ib[IB_CONST_PREAMBLE].flags = AMDGPU_IB_FLAG_CE |
                                     AMDGPU_IB_FLAG_PREAMBLE;

   return true;
}

static void amdgpu_cs_context_cleanup(struct amdgpu_cs_context *cs)
{
   unsigned i;

   for (i = 0; i < cs->num_real_buffers; i++) {
      p_atomic_dec(&cs->real_buffers[i].bo->num_cs_references);
      amdgpu_winsys_bo_reference(&cs->real_buffers[i].bo, NULL);
   }
   for (i = 0; i < cs->num_slab_buffers; i++) {
      p_atomic_dec(&cs->slab_buffers[i].bo->num_cs_references);
      amdgpu_winsys_bo_reference(&cs->slab_buffers[i].bo, NULL);
   }

   cs->num_real_buffers = 0;
   cs->num_slab_buffers = 0;
   amdgpu_fence_reference(&cs->fence, NULL);

   for (i = 0; i < ARRAY_SIZE(cs->buffer_indices_hashlist); i++) {
      cs->buffer_indices_hashlist[i] = -1;
   }
}

static void amdgpu_destroy_cs_context(struct amdgpu_cs_context *cs)
{
   amdgpu_cs_context_cleanup(cs);
   FREE(cs->flags);
   FREE(cs->real_buffers);
   FREE(cs->handles);
   FREE(cs->slab_buffers);
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

   util_queue_fence_init(&cs->flush_completed);

   cs->ctx = ctx;
   cs->flush_cs = flush;
   cs->flush_data = flush_ctx;
   cs->ring_type = ring_type;

   cs->main.ib_type = IB_MAIN;
   cs->const_ib.ib_type = IB_CONST;
   cs->const_preamble_ib.ib_type = IB_CONST_PREAMBLE;

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

   if (!amdgpu_get_new_ib(&ctx->ws->base, cs, IB_MAIN)) {
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

   if (!amdgpu_get_new_ib(&ws->base, cs, IB_CONST))
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

   if (!amdgpu_get_new_ib(&ws->base, cs, IB_CONST_PREAMBLE))
      return NULL;

   cs->csc->request.number_of_ibs = 3;
   cs->csc->request.ibs = &cs->csc->ib[IB_CONST_PREAMBLE];

   cs->cst->request.number_of_ibs = 3;
   cs->cst->request.ibs = &cs->cst->ib[IB_CONST_PREAMBLE];

   return &cs->const_preamble_ib.base;
}

static bool amdgpu_cs_validate(struct radeon_winsys_cs *rcs)
{
   return true;
}

static bool amdgpu_cs_check_space(struct radeon_winsys_cs *rcs, unsigned dw)
{
   struct amdgpu_ib *ib = amdgpu_ib(rcs);
   struct amdgpu_cs *cs = amdgpu_cs_from_ib(ib);
   unsigned requested_size = rcs->prev_dw + rcs->current.cdw + dw;
   uint64_t va;
   uint32_t *new_ptr_ib_size;

   assert(rcs->current.cdw <= rcs->current.max_dw);

   if (requested_size > amdgpu_ib_max_submit_dwords(ib->ib_type))
      return false;

   ib->max_ib_size = MAX2(ib->max_ib_size, requested_size);

   if (rcs->current.max_dw - rcs->current.cdw >= dw)
      return true;

   if (!amdgpu_cs_has_chaining(cs))
      return false;

   /* Allocate a new chunk */
   if (rcs->num_prev >= rcs->max_prev) {
      unsigned new_max_prev = MAX2(1, 2 * rcs->max_prev);
      struct radeon_winsys_cs_chunk *new_prev;

      new_prev = REALLOC(rcs->prev,
                         sizeof(*new_prev) * rcs->max_prev,
                         sizeof(*new_prev) * new_max_prev);
      if (!new_prev)
         return false;

      rcs->prev = new_prev;
      rcs->max_prev = new_max_prev;
   }

   if (!amdgpu_ib_new_buffer(cs->ctx->ws, ib))
      return false;

   assert(ib->used_ib_space == 0);
   va = amdgpu_winsys_bo(ib->big_ib_buffer)->va;

   /* This space was originally reserved. */
   rcs->current.max_dw += 4;
   assert(ib->used_ib_space + 4 * rcs->current.max_dw <= ib->big_ib_buffer->size);

   /* Pad with NOPs and add INDIRECT_BUFFER packet */
   while ((rcs->current.cdw & 7) != 4)
      radeon_emit(rcs, 0xffff1000); /* type3 nop packet */

   radeon_emit(rcs, PKT3(ib->ib_type == IB_MAIN ? PKT3_INDIRECT_BUFFER_CIK
                                           : PKT3_INDIRECT_BUFFER_CONST, 2, 0));
   radeon_emit(rcs, va);
   radeon_emit(rcs, va >> 32);
   new_ptr_ib_size = &rcs->current.buf[rcs->current.cdw];
   radeon_emit(rcs, S_3F2_CHAIN(1) | S_3F2_VALID(1));

   assert((rcs->current.cdw & 7) == 0);
   assert(rcs->current.cdw <= rcs->current.max_dw);

   *ib->ptr_ib_size |= rcs->current.cdw;
   ib->ptr_ib_size = new_ptr_ib_size;

   /* Hook up the new chunk */
   rcs->prev[rcs->num_prev].buf = rcs->current.buf;
   rcs->prev[rcs->num_prev].cdw = rcs->current.cdw;
   rcs->prev[rcs->num_prev].max_dw = rcs->current.cdw; /* no modifications */
   rcs->num_prev++;

   ib->base.prev_dw += ib->base.current.cdw;
   ib->base.current.cdw = 0;

   ib->base.current.buf = (uint32_t*)(ib->ib_mapped + ib->used_ib_space);
   ib->base.current.max_dw = ib->big_ib_buffer->size / 4 - amdgpu_cs_epilog_dws(cs->ring_type);

   amdgpu_cs_add_buffer(&cs->main.base, ib->big_ib_buffer,
                        RADEON_USAGE_READ, 0, RADEON_PRIO_IB1);

   return true;
}

static unsigned amdgpu_cs_get_buffer_list(struct radeon_winsys_cs *rcs,
                                          struct radeon_bo_list_item *list)
{
    struct amdgpu_cs_context *cs = amdgpu_cs(rcs)->csc;
    int i;

    if (list) {
        for (i = 0; i < cs->num_real_buffers; i++) {
            list[i].bo_size = cs->real_buffers[i].bo->base.size;
            list[i].vm_address = cs->real_buffers[i].bo->va;
            list[i].priority_usage = cs->real_buffers[i].u.real.priority_usage;
        }
    }
    return cs->num_real_buffers;
}

DEBUG_GET_ONCE_BOOL_OPTION(all_bos, "RADEON_ALL_BOS", false)

static void amdgpu_add_fence_dependency(struct amdgpu_cs *acs,
                                        struct amdgpu_cs_buffer *buffer)
{
   struct amdgpu_cs_context *cs = acs->csc;
   struct amdgpu_winsys_bo *bo = buffer->bo;
   struct amdgpu_cs_fence *dep;
   unsigned new_num_fences = 0;

   for (unsigned j = 0; j < bo->num_fences; ++j) {
      struct amdgpu_fence *bo_fence = (void *)bo->fences[j];
      unsigned idx;

      if (bo_fence->ctx == acs->ctx &&
         bo_fence->fence.ip_type == cs->request.ip_type &&
         bo_fence->fence.ip_instance == cs->request.ip_instance &&
         bo_fence->fence.ring == cs->request.ring)
         continue;

      if (amdgpu_fence_wait((void *)bo_fence, 0, false))
         continue;

      amdgpu_fence_reference(&bo->fences[new_num_fences], bo->fences[j]);
      new_num_fences++;

      if (!(buffer->usage & RADEON_USAGE_SYNCHRONIZED))
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

   for (unsigned j = new_num_fences; j < bo->num_fences; ++j)
      amdgpu_fence_reference(&bo->fences[j], NULL);

   bo->num_fences = new_num_fences;
}

/* Since the kernel driver doesn't synchronize execution between different
 * rings automatically, we have to add fence dependencies manually.
 */
static void amdgpu_add_fence_dependencies(struct amdgpu_cs *acs)
{
   struct amdgpu_cs_context *cs = acs->csc;
   int i;

   cs->request.number_of_dependencies = 0;

   for (i = 0; i < cs->num_real_buffers; i++)
      amdgpu_add_fence_dependency(acs, &cs->real_buffers[i]);
   for (i = 0; i < cs->num_slab_buffers; i++)
      amdgpu_add_fence_dependency(acs, &cs->slab_buffers[i]);
}

static void amdgpu_add_fence(struct amdgpu_winsys_bo *bo,
                             struct pipe_fence_handle *fence)
{
   if (bo->num_fences >= bo->max_fences) {
      unsigned new_max_fences = MAX2(1, bo->max_fences * 2);
      struct pipe_fence_handle **new_fences =
         REALLOC(bo->fences,
                 bo->num_fences * sizeof(*new_fences),
                 new_max_fences * sizeof(*new_fences));
      if (new_fences) {
         bo->fences = new_fences;
         bo->max_fences = new_max_fences;
      } else {
         fprintf(stderr, "amdgpu_add_fence: allocation failure, dropping fence\n");
         if (!bo->num_fences)
            return;

         bo->num_fences--; /* prefer to keep a more recent fence if possible */
         amdgpu_fence_reference(&bo->fences[bo->num_fences], NULL);
      }
   }

   bo->fences[bo->num_fences] = NULL;
   amdgpu_fence_reference(&bo->fences[bo->num_fences], fence);
   bo->num_fences++;
}

void amdgpu_cs_submit_ib(void *job, int thread_index)
{
   struct amdgpu_cs *acs = (struct amdgpu_cs*)job;
   struct amdgpu_winsys *ws = acs->ctx->ws;
   struct amdgpu_cs_context *cs = acs->cst;
   int i, r;

   cs->request.fence_info.handle = NULL;
   if (amdgpu_cs_has_user_fence(cs)) {
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
         cs->error_code = -ENOMEM;
         return;
      }

      LIST_FOR_EACH_ENTRY(bo, &ws->global_bo_list, u.real.global_list_item) {
         assert(num < ws->num_buffers);
         handles[num++] = bo->bo;
      }

      r = amdgpu_bo_list_create(ws->dev, ws->num_buffers,
                                handles, NULL,
                                &cs->request.resources);
      free(handles);
      pipe_mutex_unlock(ws->global_bo_list_lock);
   } else {
      r = amdgpu_bo_list_create(ws->dev, cs->num_real_buffers,
                                cs->handles, cs->flags,
                                &cs->request.resources);
   }

   if (r) {
      fprintf(stderr, "amdgpu: buffer list creation failed (%d)\n", r);
      cs->request.resources = NULL;
      amdgpu_fence_signalled(cs->fence);
      cs->error_code = r;
      goto cleanup;
   }

   r = amdgpu_cs_submit(acs->ctx->ctx, 0, &cs->request, 1);
   cs->error_code = r;
   if (r) {
      if (r == -ENOMEM)
         fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
      else
         fprintf(stderr, "amdgpu: The CS has been rejected, "
                 "see dmesg for more information (%i).\n", r);

      amdgpu_fence_signalled(cs->fence);
   } else {
      /* Success. */
      uint64_t *user_fence = NULL;
      if (amdgpu_cs_has_user_fence(cs))
         user_fence = acs->ctx->user_fence_cpu_address_base +
                      cs->request.fence_info.offset;
      amdgpu_fence_submitted(cs->fence, &cs->request, user_fence);
   }

   /* Cleanup. */
   if (cs->request.resources)
      amdgpu_bo_list_destroy(cs->request.resources);

cleanup:
   for (i = 0; i < cs->num_real_buffers; i++)
      p_atomic_dec(&cs->real_buffers[i].bo->num_active_ioctls);
   for (i = 0; i < cs->num_slab_buffers; i++)
      p_atomic_dec(&cs->slab_buffers[i].bo->num_active_ioctls);

   amdgpu_cs_context_cleanup(cs);
}

/* Make sure the previous submission is completed. */
void amdgpu_cs_sync_flush(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);

   /* Wait for any pending ioctl of this CS to complete. */
   util_queue_job_wait(&cs->flush_completed);
}

static int amdgpu_cs_flush(struct radeon_winsys_cs *rcs,
                           unsigned flags,
                           struct pipe_fence_handle **fence)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   struct amdgpu_winsys *ws = cs->ctx->ws;
   int error_code = 0;

   rcs->current.max_dw += amdgpu_cs_epilog_dws(cs->ring_type);

   switch (cs->ring_type) {
   case RING_DMA:
      /* pad DMA ring to 8 DWs */
      if (ws->info.chip_class <= SI) {
         while (rcs->current.cdw & 7)
            radeon_emit(rcs, 0xf0000000); /* NOP packet */
      } else {
         while (rcs->current.cdw & 7)
            radeon_emit(rcs, 0x00000000); /* NOP packet */
      }
      break;
   case RING_GFX:
      /* pad GFX ring to 8 DWs to meet CP fetch alignment requirements */
      if (ws->info.gfx_ib_pad_with_type2) {
         while (rcs->current.cdw & 7)
            radeon_emit(rcs, 0x80000000); /* type2 nop packet */
      } else {
         while (rcs->current.cdw & 7)
            radeon_emit(rcs, 0xffff1000); /* type3 nop packet */
      }

      /* Also pad the const IB. */
      if (cs->const_ib.ib_mapped)
         while (!cs->const_ib.base.current.cdw || (cs->const_ib.base.current.cdw & 7))
            radeon_emit(&cs->const_ib.base, 0xffff1000); /* type3 nop packet */

      if (cs->const_preamble_ib.ib_mapped)
         while (!cs->const_preamble_ib.base.current.cdw || (cs->const_preamble_ib.base.current.cdw & 7))
            radeon_emit(&cs->const_preamble_ib.base, 0xffff1000);
      break;
   case RING_UVD:
      while (rcs->current.cdw & 15)
         radeon_emit(rcs, 0x80000000); /* type2 nop packet */
      break;
   default:
      break;
   }

   if (rcs->current.cdw > rcs->current.max_dw) {
      fprintf(stderr, "amdgpu: command stream overflowed\n");
   }

   /* If the CS is not empty or overflowed.... */
   if (radeon_emitted(&cs->main.base, 0) &&
       cs->main.base.current.cdw <= cs->main.base.current.max_dw &&
       !debug_get_option_noop()) {
      struct amdgpu_cs_context *cur = cs->csc;
      unsigned i, num_buffers;

      /* Set IB sizes. */
      amdgpu_ib_finalize(&cs->main);

      if (cs->const_ib.ib_mapped)
         amdgpu_ib_finalize(&cs->const_ib);

      if (cs->const_preamble_ib.ib_mapped)
         amdgpu_ib_finalize(&cs->const_preamble_ib);

      /* Create a fence. */
      amdgpu_fence_reference(&cur->fence, NULL);
      if (cs->next_fence) {
         /* just move the reference */
         cur->fence = cs->next_fence;
         cs->next_fence = NULL;
      } else {
         cur->fence = amdgpu_fence_create(cs->ctx,
                                          cur->request.ip_type,
                                          cur->request.ip_instance,
                                          cur->request.ring);
      }
      if (fence)
         amdgpu_fence_reference(fence, cur->fence);

      amdgpu_cs_sync_flush(rcs);

      /* Prepare buffers.
       *
       * This fence must be held until the submission is queued to ensure
       * that the order of fence dependency updates matches the order of
       * submissions.
       */
      pipe_mutex_lock(ws->bo_fence_lock);
      amdgpu_add_fence_dependencies(cs);

      num_buffers = cur->num_real_buffers;
      for (i = 0; i < num_buffers; i++) {
         struct amdgpu_winsys_bo *bo = cur->real_buffers[i].bo;
         p_atomic_inc(&bo->num_active_ioctls);
         amdgpu_add_fence(bo, cur->fence);
      }

      num_buffers = cur->num_slab_buffers;
      for (i = 0; i < num_buffers; i++) {
         struct amdgpu_winsys_bo *bo = cur->slab_buffers[i].bo;
         p_atomic_inc(&bo->num_active_ioctls);
         amdgpu_add_fence(bo, cur->fence);
      }

      /* Swap command streams. "cst" is going to be submitted. */
      cs->csc = cs->cst;
      cs->cst = cur;

      /* Submit. */
      util_queue_add_job(&ws->cs_queue, cs, &cs->flush_completed,
                         amdgpu_cs_submit_ib, NULL);
      /* The submission has been queued, unlock the fence now. */
      pipe_mutex_unlock(ws->bo_fence_lock);

      if (!(flags & RADEON_FLUSH_ASYNC)) {
         amdgpu_cs_sync_flush(rcs);
         error_code = cur->error_code;
      }
   } else {
      amdgpu_cs_context_cleanup(cs->csc);
   }

   amdgpu_get_new_ib(&ws->base, cs, IB_MAIN);
   if (cs->const_ib.ib_mapped)
      amdgpu_get_new_ib(&ws->base, cs, IB_CONST);
   if (cs->const_preamble_ib.ib_mapped)
      amdgpu_get_new_ib(&ws->base, cs, IB_CONST_PREAMBLE);

   cs->main.base.used_gart = 0;
   cs->main.base.used_vram = 0;

   if (cs->ring_type == RING_GFX)
      ws->num_gfx_IBs++;
   else if (cs->ring_type == RING_DMA)
      ws->num_sdma_IBs++;

   return error_code;
}

static void amdgpu_cs_destroy(struct radeon_winsys_cs *rcs)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);

   amdgpu_cs_sync_flush(rcs);
   util_queue_fence_destroy(&cs->flush_completed);
   p_atomic_dec(&cs->ctx->ws->num_cs);
   pb_reference(&cs->main.big_ib_buffer, NULL);
   FREE(cs->main.base.prev);
   pb_reference(&cs->const_ib.big_ib_buffer, NULL);
   FREE(cs->const_ib.base.prev);
   pb_reference(&cs->const_preamble_ib.big_ib_buffer, NULL);
   FREE(cs->const_preamble_ib.base.prev);
   amdgpu_destroy_cs_context(&cs->csc1);
   amdgpu_destroy_cs_context(&cs->csc2);
   amdgpu_fence_reference(&cs->next_fence, NULL);
   FREE(cs);
}

static bool amdgpu_bo_is_referenced(struct radeon_winsys_cs *rcs,
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
   ws->base.cs_validate = amdgpu_cs_validate;
   ws->base.cs_check_space = amdgpu_cs_check_space;
   ws->base.cs_get_buffer_list = amdgpu_cs_get_buffer_list;
   ws->base.cs_flush = amdgpu_cs_flush;
   ws->base.cs_get_next_fence = amdgpu_cs_get_next_fence;
   ws->base.cs_is_buffer_referenced = amdgpu_bo_is_referenced;
   ws->base.cs_sync_flush = amdgpu_cs_sync_flush;
   ws->base.fence_wait = amdgpu_fence_wait_rel_timeout;
   ws->base.fence_reference = amdgpu_fence_reference;
}
