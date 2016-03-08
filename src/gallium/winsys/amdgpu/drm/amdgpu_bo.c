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

#include "amdgpu_cs.h"

#include "os/os_time.h"
#include "state_tracker/drm_driver.h"
#include <amdgpu_drm.h>
#include <xf86drm.h>
#include <stdio.h>

static inline struct amdgpu_winsys_bo *amdgpu_winsys_bo(struct pb_buffer *bo)
{
   return (struct amdgpu_winsys_bo *)bo;
}

static bool amdgpu_bo_wait(struct pb_buffer *_buf, uint64_t timeout,
                           enum radeon_bo_usage usage)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);
   struct amdgpu_winsys *ws = bo->ws;
   int i;

   if (bo->is_shared) {
      /* We can't use user fences for shared buffers, because user fences
       * are local to this process only. If we want to wait for all buffer
       * uses in all processes, we have to use amdgpu_bo_wait_for_idle.
       */
      bool buffer_busy = true;
      int r;

      r = amdgpu_bo_wait_for_idle(bo->bo, timeout, &buffer_busy);
      if (r)
         fprintf(stderr, "%s: amdgpu_bo_wait_for_idle failed %i\n", __func__,
                 r);
      return !buffer_busy;
   }

   if (timeout == 0) {
      /* Timeout == 0 is quite simple. */
      pipe_mutex_lock(ws->bo_fence_lock);
      for (i = 0; i < RING_LAST; i++)
         if (bo->fence[i]) {
            if (amdgpu_fence_wait(bo->fence[i], 0, false)) {
               /* Release the idle fence to avoid checking it again later. */
               amdgpu_fence_reference(&bo->fence[i], NULL);
            } else {
               pipe_mutex_unlock(ws->bo_fence_lock);
               return false;
            }
         }
      pipe_mutex_unlock(ws->bo_fence_lock);
      return true;

   } else {
      struct pipe_fence_handle *fence[RING_LAST] = {};
      bool fence_idle[RING_LAST] = {};
      bool buffer_idle = true;
      int64_t abs_timeout = os_time_get_absolute_timeout(timeout);

      /* Take references to all fences, so that we can wait for them
       * without the lock. */
      pipe_mutex_lock(ws->bo_fence_lock);
      for (i = 0; i < RING_LAST; i++)
         amdgpu_fence_reference(&fence[i], bo->fence[i]);
      pipe_mutex_unlock(ws->bo_fence_lock);

      /* Now wait for the fences. */
      for (i = 0; i < RING_LAST; i++) {
         if (fence[i]) {
            if (amdgpu_fence_wait(fence[i], abs_timeout, true))
               fence_idle[i] = true;
            else
               buffer_idle = false;
         }
      }

      /* Release idle fences to avoid checking them again later. */
      pipe_mutex_lock(ws->bo_fence_lock);
      for (i = 0; i < RING_LAST; i++) {
         if (fence[i] == bo->fence[i] && fence_idle[i])
            amdgpu_fence_reference(&bo->fence[i], NULL);

         amdgpu_fence_reference(&fence[i], NULL);
      }
      pipe_mutex_unlock(ws->bo_fence_lock);

      return buffer_idle;
   }
}

static enum radeon_bo_domain amdgpu_bo_get_initial_domain(
      struct pb_buffer *buf)
{
   return ((struct amdgpu_winsys_bo*)buf)->initial_domain;
}

void amdgpu_bo_destroy(struct pb_buffer *_buf)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);
   int i;

   pipe_mutex_lock(bo->ws->global_bo_list_lock);
   LIST_DEL(&bo->global_list_item);
   bo->ws->num_buffers--;
   pipe_mutex_unlock(bo->ws->global_bo_list_lock);

   amdgpu_bo_va_op(bo->bo, 0, bo->base.size, bo->va, 0, AMDGPU_VA_OP_UNMAP);
   amdgpu_va_range_free(bo->va_handle);
   amdgpu_bo_free(bo->bo);

   for (i = 0; i < RING_LAST; i++)
      amdgpu_fence_reference(&bo->fence[i], NULL);

   if (bo->initial_domain & RADEON_DOMAIN_VRAM)
      bo->ws->allocated_vram -= align(bo->base.size, bo->ws->gart_page_size);
   else if (bo->initial_domain & RADEON_DOMAIN_GTT)
      bo->ws->allocated_gtt -= align(bo->base.size, bo->ws->gart_page_size);
   FREE(bo);
}

static void amdgpu_bo_destroy_or_cache(struct pb_buffer *_buf)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);

   if (bo->use_reusable_pool)
      pb_cache_add_buffer(&bo->cache_entry);
   else
      amdgpu_bo_destroy(_buf);
}

static void *amdgpu_bo_map(struct pb_buffer *buf,
                           struct radeon_winsys_cs *rcs,
                           enum pipe_transfer_usage usage)
{
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;
   struct amdgpu_cs *cs = (struct amdgpu_cs*)rcs;
   int r;
   void *cpu = NULL;

   /* If it's not unsynchronized bo_map, flush CS if needed and then wait. */
   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
      /* DONTBLOCK doesn't make sense with UNSYNCHRONIZED. */
      if (usage & PIPE_TRANSFER_DONTBLOCK) {
         if (!(usage & PIPE_TRANSFER_WRITE)) {
            /* Mapping for read.
             *
             * Since we are mapping for read, we don't need to wait
             * if the GPU is using the buffer for read too
             * (neither one is changing it).
             *
             * Only check whether the buffer is being used for write. */
            if (cs && amdgpu_bo_is_referenced_by_cs_with_usage(cs, bo,
                                                               RADEON_USAGE_WRITE)) {
               cs->flush_cs(cs->flush_data, RADEON_FLUSH_ASYNC, NULL);
               return NULL;
            }

            if (!amdgpu_bo_wait((struct pb_buffer*)bo, 0,
                                RADEON_USAGE_WRITE)) {
               return NULL;
            }
         } else {
            if (cs && amdgpu_bo_is_referenced_by_cs(cs, bo)) {
               cs->flush_cs(cs->flush_data, RADEON_FLUSH_ASYNC, NULL);
               return NULL;
            }

            if (!amdgpu_bo_wait((struct pb_buffer*)bo, 0,
                                RADEON_USAGE_READWRITE)) {
               return NULL;
            }
         }
      } else {
         uint64_t time = os_time_get_nano();

         if (!(usage & PIPE_TRANSFER_WRITE)) {
            /* Mapping for read.
             *
             * Since we are mapping for read, we don't need to wait
             * if the GPU is using the buffer for read too
             * (neither one is changing it).
             *
             * Only check whether the buffer is being used for write. */
            if (cs && amdgpu_bo_is_referenced_by_cs_with_usage(cs, bo,
                                                               RADEON_USAGE_WRITE)) {
               cs->flush_cs(cs->flush_data, 0, NULL);
            }
            amdgpu_bo_wait((struct pb_buffer*)bo, PIPE_TIMEOUT_INFINITE,
                           RADEON_USAGE_WRITE);
         } else {
            /* Mapping for write. */
            if (cs && amdgpu_bo_is_referenced_by_cs(cs, bo))
               cs->flush_cs(cs->flush_data, 0, NULL);

            amdgpu_bo_wait((struct pb_buffer*)bo, PIPE_TIMEOUT_INFINITE,
                           RADEON_USAGE_READWRITE);
         }

         bo->ws->buffer_wait_time += os_time_get_nano() - time;
      }
   }

   /* If the buffer is created from user memory, return the user pointer. */
   if (bo->user_ptr)
       return bo->user_ptr;

   r = amdgpu_bo_cpu_map(bo->bo, &cpu);
   if (r) {
      /* Clear the cache and try again. */
      pb_cache_release_all_buffers(&bo->ws->bo_cache);
      r = amdgpu_bo_cpu_map(bo->bo, &cpu);
   }
   return r ? NULL : cpu;
}

static void amdgpu_bo_unmap(struct pb_buffer *buf)
{
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;

   amdgpu_bo_cpu_unmap(bo->bo);
}

static const struct pb_vtbl amdgpu_winsys_bo_vtbl = {
   amdgpu_bo_destroy_or_cache
   /* other functions are never called */
};

static void amdgpu_add_buffer_to_global_list(struct amdgpu_winsys_bo *bo)
{
   struct amdgpu_winsys *ws = bo->ws;

   pipe_mutex_lock(ws->global_bo_list_lock);
   LIST_ADDTAIL(&bo->global_list_item, &ws->global_bo_list);
   ws->num_buffers++;
   pipe_mutex_unlock(ws->global_bo_list_lock);
}

static struct amdgpu_winsys_bo *amdgpu_create_bo(struct amdgpu_winsys *ws,
                                                 unsigned size,
                                                 unsigned alignment,
                                                 unsigned usage,
                                                 enum radeon_bo_domain initial_domain,
                                                 unsigned flags)
{
   struct amdgpu_bo_alloc_request request = {0};
   amdgpu_bo_handle buf_handle;
   uint64_t va = 0;
   struct amdgpu_winsys_bo *bo;
   amdgpu_va_handle va_handle;
   int r;

   assert(initial_domain & RADEON_DOMAIN_VRAM_GTT);
   bo = CALLOC_STRUCT(amdgpu_winsys_bo);
   if (!bo) {
      return NULL;
   }

   pb_cache_init_entry(&ws->bo_cache, &bo->cache_entry, &bo->base);
   request.alloc_size = size;
   request.phys_alignment = alignment;

   if (initial_domain & RADEON_DOMAIN_VRAM)
      request.preferred_heap |= AMDGPU_GEM_DOMAIN_VRAM;
   if (initial_domain & RADEON_DOMAIN_GTT)
      request.preferred_heap |= AMDGPU_GEM_DOMAIN_GTT;

   if (flags & RADEON_FLAG_CPU_ACCESS)
      request.flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
   if (flags & RADEON_FLAG_NO_CPU_ACCESS)
      request.flags |= AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
   if (flags & RADEON_FLAG_GTT_WC)
      request.flags |= AMDGPU_GEM_CREATE_CPU_GTT_USWC;

   r = amdgpu_bo_alloc(ws->dev, &request, &buf_handle);
   if (r) {
      fprintf(stderr, "amdgpu: Failed to allocate a buffer:\n");
      fprintf(stderr, "amdgpu:    size      : %d bytes\n", size);
      fprintf(stderr, "amdgpu:    alignment : %d bytes\n", alignment);
      fprintf(stderr, "amdgpu:    domains   : %d\n", initial_domain);
      goto error_bo_alloc;
   }

   r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
                             size, alignment, 0, &va, &va_handle, 0);
   if (r)
      goto error_va_alloc;

   r = amdgpu_bo_va_op(buf_handle, 0, size, va, 0, AMDGPU_VA_OP_MAP);
   if (r)
      goto error_va_map;

   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment = alignment;
   bo->base.usage = usage;
   bo->base.size = size;
   bo->base.vtbl = &amdgpu_winsys_bo_vtbl;
   bo->ws = ws;
   bo->bo = buf_handle;
   bo->va = va;
   bo->va_handle = va_handle;
   bo->initial_domain = initial_domain;
   bo->unique_id = __sync_fetch_and_add(&ws->next_bo_unique_id, 1);

   if (initial_domain & RADEON_DOMAIN_VRAM)
      ws->allocated_vram += align(size, ws->gart_page_size);
   else if (initial_domain & RADEON_DOMAIN_GTT)
      ws->allocated_gtt += align(size, ws->gart_page_size);

   amdgpu_add_buffer_to_global_list(bo);

   return bo;

error_va_map:
   amdgpu_va_range_free(va_handle);

error_va_alloc:
   amdgpu_bo_free(buf_handle);

error_bo_alloc:
   FREE(bo);
   return NULL;
}

bool amdgpu_bo_can_reclaim(struct pb_buffer *_buf)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);

   if (amdgpu_bo_is_referenced_by_any_cs(bo)) {
      return false;
   }

   return amdgpu_bo_wait(_buf, 0, RADEON_USAGE_READWRITE);
}

static unsigned eg_tile_split(unsigned tile_split)
{
   switch (tile_split) {
   case 0:     tile_split = 64;    break;
   case 1:     tile_split = 128;   break;
   case 2:     tile_split = 256;   break;
   case 3:     tile_split = 512;   break;
   default:
   case 4:     tile_split = 1024;  break;
   case 5:     tile_split = 2048;  break;
   case 6:     tile_split = 4096;  break;
   }
   return tile_split;
}

static unsigned eg_tile_split_rev(unsigned eg_tile_split)
{
   switch (eg_tile_split) {
   case 64:    return 0;
   case 128:   return 1;
   case 256:   return 2;
   case 512:   return 3;
   default:
   case 1024:  return 4;
   case 2048:  return 5;
   case 4096:  return 6;
   }
}

static void amdgpu_bo_get_tiling(struct pb_buffer *_buf,
                                 enum radeon_bo_layout *microtiled,
                                 enum radeon_bo_layout *macrotiled,
                                 unsigned *bankw, unsigned *bankh,
                                 unsigned *tile_split,
                                 unsigned *stencil_tile_split,
                                 unsigned *mtilea,
                                 bool *scanout)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);
   struct amdgpu_bo_info info = {0};
   uint32_t tiling_flags;
   int r;

   r = amdgpu_bo_query_info(bo->bo, &info);
   if (r)
      return;

   tiling_flags = info.metadata.tiling_info;

   *microtiled = RADEON_LAYOUT_LINEAR;
   *macrotiled = RADEON_LAYOUT_LINEAR;

   if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == 4)  /* 2D_TILED_THIN1 */
      *macrotiled = RADEON_LAYOUT_TILED;
   else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == 2) /* 1D_TILED_THIN1 */
      *microtiled = RADEON_LAYOUT_TILED;

   if (bankw && tile_split && mtilea && tile_split) {
      *bankw = 1 << AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
      *bankh = 1 << AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
      *tile_split = eg_tile_split(AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT));
      *mtilea = 1 << AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
   }
   if (scanout)
      *scanout = AMDGPU_TILING_GET(tiling_flags, MICRO_TILE_MODE) == 0; /* DISPLAY */
}

static void amdgpu_bo_set_tiling(struct pb_buffer *_buf,
                                 struct radeon_winsys_cs *rcs,
                                 enum radeon_bo_layout microtiled,
                                 enum radeon_bo_layout macrotiled,
                                 unsigned pipe_config,
                                 unsigned bankw, unsigned bankh,
                                 unsigned tile_split,
                                 unsigned stencil_tile_split,
                                 unsigned mtilea, unsigned num_banks,
                                 uint32_t pitch,
                                 bool scanout)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);
   struct amdgpu_bo_metadata metadata = {0};
   uint32_t tiling_flags = 0;

   if (macrotiled == RADEON_LAYOUT_TILED)
      tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 4); /* 2D_TILED_THIN1 */
   else if (microtiled == RADEON_LAYOUT_TILED)
      tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 2); /* 1D_TILED_THIN1 */
   else
      tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 1); /* LINEAR_ALIGNED */

   tiling_flags |= AMDGPU_TILING_SET(PIPE_CONFIG, pipe_config);
   tiling_flags |= AMDGPU_TILING_SET(BANK_WIDTH, util_logbase2(bankw));
   tiling_flags |= AMDGPU_TILING_SET(BANK_HEIGHT, util_logbase2(bankh));
   if (tile_split)
      tiling_flags |= AMDGPU_TILING_SET(TILE_SPLIT, eg_tile_split_rev(tile_split));
   tiling_flags |= AMDGPU_TILING_SET(MACRO_TILE_ASPECT, util_logbase2(mtilea));
   tiling_flags |= AMDGPU_TILING_SET(NUM_BANKS, util_logbase2(num_banks)-1);

   if (scanout)
      tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 0); /* DISPLAY_MICRO_TILING */
   else
      tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 1); /* THIN_MICRO_TILING */

   metadata.tiling_info = tiling_flags;

   amdgpu_bo_set_metadata(bo->bo, &metadata);
}

static struct pb_buffer *
amdgpu_bo_create(struct radeon_winsys *rws,
                 unsigned size,
                 unsigned alignment,
                 boolean use_reusable_pool,
                 enum radeon_bo_domain domain,
                 enum radeon_bo_flag flags)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_winsys_bo *bo;
   unsigned usage = 0;

   /* Don't use VRAM if the GPU doesn't have much. This is only the initial
    * domain. The kernel is free to move the buffer if it wants to.
    *
    * 64MB means no VRAM by todays standards.
    */
   if (domain & RADEON_DOMAIN_VRAM && ws->info.vram_size <= 64*1024*1024) {
      domain = RADEON_DOMAIN_GTT;
      flags = RADEON_FLAG_GTT_WC;
   }

   /* Align size to page size. This is the minimum alignment for normal
    * BOs. Aligning this here helps the cached bufmgr. Especially small BOs,
    * like constant/uniform buffers, can benefit from better and more reuse.
    */
   size = align(size, ws->gart_page_size);

   /* Only set one usage bit each for domains and flags, or the cache manager
    * might consider different sets of domains / flags compatible
    */
   if (domain == RADEON_DOMAIN_VRAM_GTT)
      usage = 1 << 2;
   else
      usage = domain >> 1;
   assert(flags < sizeof(usage) * 8 - 3);
   usage |= 1 << (flags + 3);

   /* Get a buffer from the cache. */
   if (use_reusable_pool) {
       bo = (struct amdgpu_winsys_bo*)
            pb_cache_reclaim_buffer(&ws->bo_cache, size, alignment,
                                    usage);
       if (bo)
          return &bo->base;
   }

   /* Create a new one. */
   bo = amdgpu_create_bo(ws, size, alignment, usage, domain, flags);
   if (!bo) {
      /* Clear the cache and try again. */
      pb_cache_release_all_buffers(&ws->bo_cache);
      bo = amdgpu_create_bo(ws, size, alignment, usage, domain, flags);
      if (!bo)
         return NULL;
   }

   bo->use_reusable_pool = use_reusable_pool;
   return &bo->base;
}

static struct pb_buffer *amdgpu_bo_from_handle(struct radeon_winsys *rws,
                                               struct winsys_handle *whandle,
                                               unsigned *stride)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_winsys_bo *bo;
   enum amdgpu_bo_handle_type type;
   struct amdgpu_bo_import_result result = {0};
   uint64_t va;
   amdgpu_va_handle va_handle;
   struct amdgpu_bo_info info = {0};
   enum radeon_bo_domain initial = 0;
   int r;

   /* Initialize the structure. */
   bo = CALLOC_STRUCT(amdgpu_winsys_bo);
   if (!bo) {
      return NULL;
   }

   switch (whandle->type) {
   case DRM_API_HANDLE_TYPE_SHARED:
      type = amdgpu_bo_handle_type_gem_flink_name;
      break;
   case DRM_API_HANDLE_TYPE_FD:
      type = amdgpu_bo_handle_type_dma_buf_fd;
      break;
   default:
      return NULL;
   }

   r = amdgpu_bo_import(ws->dev, type, whandle->handle, &result);
   if (r)
      goto error;

   /* Get initial domains. */
   r = amdgpu_bo_query_info(result.buf_handle, &info);
   if (r)
      goto error_query;

   r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
                             result.alloc_size, 1 << 20, 0, &va, &va_handle, 0);
   if (r)
      goto error_query;

   r = amdgpu_bo_va_op(result.buf_handle, 0, result.alloc_size, va, 0, AMDGPU_VA_OP_MAP);
   if (r)
      goto error_va_map;

   if (info.preferred_heap & AMDGPU_GEM_DOMAIN_VRAM)
      initial |= RADEON_DOMAIN_VRAM;
   if (info.preferred_heap & AMDGPU_GEM_DOMAIN_GTT)
      initial |= RADEON_DOMAIN_GTT;


   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment = info.phys_alignment;
   bo->base.usage = PB_USAGE_GPU_WRITE | PB_USAGE_GPU_READ;
   bo->bo = result.buf_handle;
   bo->base.size = result.alloc_size;
   bo->base.vtbl = &amdgpu_winsys_bo_vtbl;
   bo->ws = ws;
   bo->va = va;
   bo->va_handle = va_handle;
   bo->initial_domain = initial;
   bo->unique_id = __sync_fetch_and_add(&ws->next_bo_unique_id, 1);
   bo->is_shared = true;

   if (stride)
      *stride = whandle->stride;

   if (bo->initial_domain & RADEON_DOMAIN_VRAM)
      ws->allocated_vram += align(bo->base.size, ws->gart_page_size);
   else if (bo->initial_domain & RADEON_DOMAIN_GTT)
      ws->allocated_gtt += align(bo->base.size, ws->gart_page_size);

   amdgpu_add_buffer_to_global_list(bo);

   return &bo->base;

error_va_map:
   amdgpu_va_range_free(va_handle);

error_query:
   amdgpu_bo_free(result.buf_handle);

error:
   FREE(bo);
   return NULL;
}

static boolean amdgpu_bo_get_handle(struct pb_buffer *buffer,
                                    unsigned stride,
                                    struct winsys_handle *whandle)
{
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(buffer);
   enum amdgpu_bo_handle_type type;
   int r;

   bo->use_reusable_pool = false;

   switch (whandle->type) {
   case DRM_API_HANDLE_TYPE_SHARED:
      type = amdgpu_bo_handle_type_gem_flink_name;
      break;
   case DRM_API_HANDLE_TYPE_FD:
      type = amdgpu_bo_handle_type_dma_buf_fd;
      break;
   case DRM_API_HANDLE_TYPE_KMS:
      type = amdgpu_bo_handle_type_kms;
      break;
   default:
      return FALSE;
   }

   r = amdgpu_bo_export(bo->bo, type, &whandle->handle);
   if (r)
      return FALSE;

   whandle->stride = stride;
   bo->is_shared = true;
   return TRUE;
}

static struct pb_buffer *amdgpu_bo_from_ptr(struct radeon_winsys *rws,
					    void *pointer, unsigned size)
{
    struct amdgpu_winsys *ws = amdgpu_winsys(rws);
    amdgpu_bo_handle buf_handle;
    struct amdgpu_winsys_bo *bo;
    uint64_t va;
    amdgpu_va_handle va_handle;

    bo = CALLOC_STRUCT(amdgpu_winsys_bo);
    if (!bo)
        return NULL;

    if (amdgpu_create_bo_from_user_mem(ws->dev, pointer, size, &buf_handle))
        goto error;

    if (amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
                              size, 1 << 12, 0, &va, &va_handle, 0))
        goto error_va_alloc;

    if (amdgpu_bo_va_op(buf_handle, 0, size, va, 0, AMDGPU_VA_OP_MAP))
        goto error_va_map;

    /* Initialize it. */
    pipe_reference_init(&bo->base.reference, 1);
    bo->bo = buf_handle;
    bo->base.alignment = 0;
    bo->base.usage = PB_USAGE_GPU_WRITE | PB_USAGE_GPU_READ;
    bo->base.size = size;
    bo->base.vtbl = &amdgpu_winsys_bo_vtbl;
    bo->ws = ws;
    bo->user_ptr = pointer;
    bo->va = va;
    bo->va_handle = va_handle;
    bo->initial_domain = RADEON_DOMAIN_GTT;
    bo->unique_id = __sync_fetch_and_add(&ws->next_bo_unique_id, 1);

    ws->allocated_gtt += align(bo->base.size, ws->gart_page_size);

    amdgpu_add_buffer_to_global_list(bo);

    return (struct pb_buffer*)bo;

error_va_map:
    amdgpu_va_range_free(va_handle);

error_va_alloc:
    amdgpu_bo_free(buf_handle);

error:
    FREE(bo);
    return NULL;
}

static bool amdgpu_bo_is_user_ptr(struct pb_buffer *buf)
{
   return ((struct amdgpu_winsys_bo*)buf)->user_ptr != NULL;
}

static uint64_t amdgpu_bo_get_va(struct pb_buffer *buf)
{
   return ((struct amdgpu_winsys_bo*)buf)->va;
}

void amdgpu_bo_init_functions(struct amdgpu_winsys *ws)
{
   ws->base.buffer_set_tiling = amdgpu_bo_set_tiling;
   ws->base.buffer_get_tiling = amdgpu_bo_get_tiling;
   ws->base.buffer_map = amdgpu_bo_map;
   ws->base.buffer_unmap = amdgpu_bo_unmap;
   ws->base.buffer_wait = amdgpu_bo_wait;
   ws->base.buffer_create = amdgpu_bo_create;
   ws->base.buffer_from_handle = amdgpu_bo_from_handle;
   ws->base.buffer_from_ptr = amdgpu_bo_from_ptr;
   ws->base.buffer_is_user_ptr = amdgpu_bo_is_user_ptr;
   ws->base.buffer_get_handle = amdgpu_bo_get_handle;
   ws->base.buffer_get_virtual_address = amdgpu_bo_get_va;
   ws->base.buffer_get_initial_domain = amdgpu_bo_get_initial_domain;
}
