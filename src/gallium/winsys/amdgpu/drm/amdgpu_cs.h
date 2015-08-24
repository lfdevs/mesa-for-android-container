/*
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
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

#ifndef AMDGPU_CS_H
#define AMDGPU_CS_H

#include "amdgpu_bo.h"
#include "util/u_memory.h"

struct amdgpu_ctx {
   struct amdgpu_winsys *ws;
   amdgpu_context_handle ctx;
   amdgpu_bo_handle user_fence_bo;
   uint64_t *user_fence_cpu_address_base;
   int refcount;
};

struct amdgpu_cs_buffer {
   struct amdgpu_winsys_bo *bo;
   enum radeon_bo_usage usage;
   enum radeon_bo_domain domains;
};


struct amdgpu_cs {
   struct radeon_winsys_cs base;
   struct amdgpu_ctx *ctx;

   /* Flush CS. */
   void (*flush_cs)(void *ctx, unsigned flags, struct pipe_fence_handle **fence);
   void *flush_data;

   /* A buffer out of which new IBs are allocated. */
   struct pb_buffer *big_ib_buffer; /* for holding the reference */
   struct amdgpu_winsys_bo *big_ib_winsys_buffer;
   uint8_t *ib_mapped;
   unsigned used_ib_space;

   /* amdgpu_cs_submit parameters */
   struct amdgpu_cs_request    request;
   struct amdgpu_cs_ib_info    ib;

   /* Relocs. */
   unsigned                    max_num_buffers;
   unsigned                    num_buffers;
   amdgpu_bo_handle            *handles;
   uint8_t                     *flags;
   struct amdgpu_cs_buffer     *buffers;

   int                         buffer_indices_hashlist[512];

   uint64_t                    used_vram;
   uint64_t                    used_gart;

   unsigned                    max_dependencies;
};

struct amdgpu_fence {
   struct pipe_reference reference;

   struct amdgpu_ctx *ctx;  /* submission context */
   struct amdgpu_cs_fence fence;
   uint64_t *user_fence_cpu_address;

   volatile int signalled;              /* bool (int for atomicity) */
};

static inline void amdgpu_ctx_unref(struct amdgpu_ctx *ctx)
{
   if (p_atomic_dec_zero(&ctx->refcount)) {
      amdgpu_cs_ctx_free(ctx->ctx);
      amdgpu_bo_free(ctx->user_fence_bo);
      FREE(ctx);
   }
}

static inline void amdgpu_fence_reference(struct pipe_fence_handle **dst,
                                          struct pipe_fence_handle *src)
{
   struct amdgpu_fence **rdst = (struct amdgpu_fence **)dst;
   struct amdgpu_fence *rsrc = (struct amdgpu_fence *)src;

   if (pipe_reference(&(*rdst)->reference, &rsrc->reference)) {
      amdgpu_ctx_unref((*rdst)->ctx);
      FREE(*rdst);
   }
   *rdst = rsrc;
}

int amdgpu_get_reloc(struct amdgpu_cs *csc, struct amdgpu_winsys_bo *bo);

static inline struct amdgpu_cs *
amdgpu_cs(struct radeon_winsys_cs *base)
{
   return (struct amdgpu_cs*)base;
}

static inline boolean
amdgpu_bo_is_referenced_by_cs(struct amdgpu_cs *cs,
                              struct amdgpu_winsys_bo *bo)
{
   int num_refs = bo->num_cs_references;
   return num_refs == bo->rws->num_cs ||
         (num_refs && amdgpu_get_reloc(cs, bo) != -1);
}

static inline boolean
amdgpu_bo_is_referenced_by_cs_with_usage(struct amdgpu_cs *cs,
                                         struct amdgpu_winsys_bo *bo,
                                         enum radeon_bo_usage usage)
{
   int index;

   if (!bo->num_cs_references)
      return FALSE;

   index = amdgpu_get_reloc(cs, bo);
   if (index == -1)
      return FALSE;

   return (cs->buffers[index].usage & usage) != 0;
}

static inline boolean
amdgpu_bo_is_referenced_by_any_cs(struct amdgpu_winsys_bo *bo)
{
   return bo->num_cs_references != 0;
}

bool amdgpu_fence_wait(struct pipe_fence_handle *fence, uint64_t timeout,
                       bool absolute);
void amdgpu_cs_init_functions(struct amdgpu_winsys *ws);

#endif
