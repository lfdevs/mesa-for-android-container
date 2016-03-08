/*
 * Copyright © 2008 Jérôme Glisse
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

#ifndef AMDGPU_BO_H
#define AMDGPU_BO_H

#include "amdgpu_winsys.h"
#include "pipebuffer/pb_bufmgr.h"

struct amdgpu_winsys_bo {
   struct pb_buffer base;
   struct pb_cache_entry cache_entry;

   struct amdgpu_winsys *ws;
   void *user_ptr; /* from buffer_from_ptr */

   amdgpu_bo_handle bo;
   uint32_t unique_id;
   amdgpu_va_handle va_handle;
   uint64_t va;
   enum radeon_bo_domain initial_domain;
   bool use_reusable_pool;

   /* how many command streams is this bo referenced in? */
   int num_cs_references;

   /* whether buffer_get_handle or buffer_from_handle was called,
    * it can only transition from false to true
    */
   volatile int is_shared; /* bool (int for atomicity) */

   /* Fences for buffer synchronization. */
   struct pipe_fence_handle *fence[RING_LAST];

   struct list_head global_list_item;
};

bool amdgpu_bo_can_reclaim(struct pb_buffer *_buf);
void amdgpu_bo_destroy(struct pb_buffer *_buf);
void amdgpu_bo_init_functions(struct amdgpu_winsys *ws);

static inline
void amdgpu_winsys_bo_reference(struct amdgpu_winsys_bo **dst,
                                struct amdgpu_winsys_bo *src)
{
   pb_reference((struct pb_buffer**)dst, (struct pb_buffer*)src);
}

#endif
