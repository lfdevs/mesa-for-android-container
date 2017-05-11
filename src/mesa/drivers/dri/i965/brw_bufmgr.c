/**************************************************************************
 *
 * Copyright © 2007 Red Hat Inc.
 * Copyright © 2007-2012 Intel Corporation
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 *          Keith Whitwell <keithw-at-tungstengraphics-dot-com>
 *          Eric Anholt <eric@anholt.net>
 *          Dave Airlie <airlied@linux.ie>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86drm.h>
#include <util/u_atomic.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>

#include "errno.h"
#ifndef ETIME
#define ETIME ETIMEDOUT
#endif
#include "common/gen_debug.h"
#include "common/gen_device_info.h"
#include "libdrm_macros.h"
#include "main/macros.h"
#include "util/macros.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "brw_bufmgr.h"
#include "brw_context.h"
#include "string.h"

#include "i915_drm.h"

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x)
#endif

#define memclear(s) memset(&s, 0, sizeof(s))

#define FILE_DEBUG_FLAG DEBUG_BUFMGR

static inline int
atomic_add_unless(int *v, int add, int unless)
{
   int c, old;
   c = p_atomic_read(v);
   while (c != unless && (old = p_atomic_cmpxchg(v, c, c + add)) != c)
      c = old;
   return c == unless;
}

struct bo_cache_bucket {
   struct list_head head;
   uint64_t size;
};

struct brw_bufmgr {
   int fd;

   pthread_mutex_t lock;

   /** Array of lists of cached gem objects of power-of-two sizes */
   struct bo_cache_bucket cache_bucket[14 * 4];
   int num_buckets;
   time_t time;

   struct hash_table *name_table;
   struct hash_table *handle_table;

   bool has_llc:1;
   bool bo_reuse:1;
};

static int bo_set_tiling_internal(struct brw_bo *bo, uint32_t tiling_mode,
                                  uint32_t stride);

static void bo_free(struct brw_bo *bo);

static uint32_t
key_hash_uint(const void *key)
{
   return _mesa_hash_data(key, 4);
}

static bool
key_uint_equal(const void *a, const void *b)
{
   return *((unsigned *) a) == *((unsigned *) b);
}

static struct brw_bo *
hash_find_bo(struct hash_table *ht, unsigned int key)
{
   struct hash_entry *entry = _mesa_hash_table_search(ht, &key);
   return entry ? (struct brw_bo *) entry->data : NULL;
}

static uint64_t
bo_tile_size(struct brw_bufmgr *bufmgr, uint64_t size, uint32_t tiling)
{
   if (tiling == I915_TILING_NONE)
      return size;

   /* 965+ just need multiples of page size for tiling */
   return ALIGN(size, 4096);
}

/*
 * Round a given pitch up to the minimum required for X tiling on a
 * given chip.  We use 512 as the minimum to allow for a later tiling
 * change.
 */
static uint32_t
bo_tile_pitch(struct brw_bufmgr *bufmgr, uint32_t pitch, uint32_t tiling)
{
   unsigned long tile_width;

   /* If untiled, then just align it so that we can do rendering
    * to it with the 3D engine.
    */
   if (tiling == I915_TILING_NONE)
      return ALIGN(pitch, 64);

   if (tiling == I915_TILING_X)
      tile_width = 512;
   else
      tile_width = 128;

   /* 965 is flexible */
   return ALIGN(pitch, tile_width);
}

static struct bo_cache_bucket *
bucket_for_size(struct brw_bufmgr *bufmgr, uint64_t size)
{
   int i;

   for (i = 0; i < bufmgr->num_buckets; i++) {
      struct bo_cache_bucket *bucket = &bufmgr->cache_bucket[i];
      if (bucket->size >= size) {
         return bucket;
      }
   }

   return NULL;
}

inline void
brw_bo_reference(struct brw_bo *bo)
{
   p_atomic_inc(&bo->refcount);
}

int
brw_bo_busy(struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   struct drm_i915_gem_busy busy;
   int ret;

   memclear(busy);
   busy.handle = bo->gem_handle;

   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_BUSY, &busy);
   if (ret == 0) {
      bo->idle = !busy.busy;
      return busy.busy;
   }
   return false;
}

int
brw_bo_madvise(struct brw_bo *bo, int state)
{
   struct drm_i915_gem_madvise madv;

   memclear(madv);
   madv.handle = bo->gem_handle;
   madv.madv = state;
   madv.retained = 1;
   drmIoctl(bo->bufmgr->fd, DRM_IOCTL_I915_GEM_MADVISE, &madv);

   return madv.retained;
}

/* drop the oldest entries that have been purged by the kernel */
static void
brw_bo_cache_purge_bucket(struct brw_bufmgr *bufmgr,
                          struct bo_cache_bucket *bucket)
{
   list_for_each_entry_safe(struct brw_bo, bo, &bucket->head, head) {
      if (brw_bo_madvise(bo, I915_MADV_DONTNEED))
         break;

      list_del(&bo->head);
      bo_free(bo);
   }
}

static struct brw_bo *
bo_alloc_internal(struct brw_bufmgr *bufmgr,
                  const char *name,
                  uint64_t size,
                  unsigned flags,
                  uint32_t tiling_mode,
                  uint32_t stride, uint64_t alignment)
{
   struct brw_bo *bo;
   unsigned int page_size = getpagesize();
   int ret;
   struct bo_cache_bucket *bucket;
   bool alloc_from_cache;
   uint64_t bo_size;
   bool for_render = false;

   if (flags & BO_ALLOC_FOR_RENDER)
      for_render = true;

   /* Round the allocated size up to a power of two number of pages. */
   bucket = bucket_for_size(bufmgr, size);

   /* If we don't have caching at this size, don't actually round the
    * allocation up.
    */
   if (bucket == NULL) {
      bo_size = size;
      if (bo_size < page_size)
         bo_size = page_size;
   } else {
      bo_size = bucket->size;
   }

   pthread_mutex_lock(&bufmgr->lock);
   /* Get a buffer out of the cache if available */
retry:
   alloc_from_cache = false;
   if (bucket != NULL && !list_empty(&bucket->head)) {
      if (for_render) {
         /* Allocate new render-target BOs from the tail (MRU)
          * of the list, as it will likely be hot in the GPU
          * cache and in the aperture for us.
          */
         bo = LIST_ENTRY(struct brw_bo, bucket->head.prev, head);
         list_del(&bo->head);
         alloc_from_cache = true;
         bo->align = alignment;
      } else {
         assert(alignment == 0);
         /* For non-render-target BOs (where we're probably
          * going to map it first thing in order to fill it
          * with data), check if the last BO in the cache is
          * unbusy, and only reuse in that case. Otherwise,
          * allocating a new buffer is probably faster than
          * waiting for the GPU to finish.
          */
         bo = LIST_ENTRY(struct brw_bo, bucket->head.next, head);
         if (!brw_bo_busy(bo)) {
            alloc_from_cache = true;
            list_del(&bo->head);
         }
      }

      if (alloc_from_cache) {
         if (!brw_bo_madvise(bo, I915_MADV_WILLNEED)) {
            bo_free(bo);
            brw_bo_cache_purge_bucket(bufmgr, bucket);
            goto retry;
         }

         if (bo_set_tiling_internal(bo, tiling_mode, stride)) {
            bo_free(bo);
            goto retry;
         }
      }
   }

   if (!alloc_from_cache) {
      struct drm_i915_gem_create create;

      bo = calloc(1, sizeof(*bo));
      if (!bo)
         goto err;

      bo->size = bo_size;

      memclear(create);
      create.size = bo_size;

      ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_CREATE, &create);
      if (ret != 0) {
         free(bo);
         goto err;
      }

      bo->gem_handle = create.handle;
      _mesa_hash_table_insert(bufmgr->handle_table, &bo->gem_handle, bo);

      bo->bufmgr = bufmgr;
      bo->align = alignment;

      bo->tiling_mode = I915_TILING_NONE;
      bo->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
      bo->stride = 0;

      if (bo_set_tiling_internal(bo, tiling_mode, stride))
         goto err_free;
   }

   bo->name = name;
   p_atomic_set(&bo->refcount, 1);
   bo->reusable = true;

   pthread_mutex_unlock(&bufmgr->lock);

   DBG("bo_create: buf %d (%s) %ldb\n", bo->gem_handle, bo->name, size);

   return bo;

err_free:
   bo_free(bo);
err:
   pthread_mutex_unlock(&bufmgr->lock);
   return NULL;
}

struct brw_bo *
brw_bo_alloc(struct brw_bufmgr *bufmgr,
             const char *name, uint64_t size, uint64_t alignment)
{
   return bo_alloc_internal(bufmgr, name, size, 0, I915_TILING_NONE, 0, 0);
}

struct brw_bo *
brw_bo_alloc_tiled(struct brw_bufmgr *bufmgr, const char *name,
                   int x, int y, int cpp, uint32_t tiling,
                   uint32_t *pitch, unsigned flags)
{
   uint64_t size;
   uint32_t stride;
   unsigned long aligned_y, height_alignment;

   /* If we're tiled, our allocations are in 8 or 32-row blocks,
    * so failure to align our height means that we won't allocate
    * enough pages.
    *
    * If we're untiled, we still have to align to 2 rows high
    * because the data port accesses 2x2 blocks even if the
    * bottom row isn't to be rendered, so failure to align means
    * we could walk off the end of the GTT and fault.  This is
    * documented on 965, and may be the case on older chipsets
    * too so we try to be careful.
    */
   aligned_y = y;
   height_alignment = 2;

   if (tiling == I915_TILING_X)
      height_alignment = 8;
   else if (tiling == I915_TILING_Y)
      height_alignment = 32;
   aligned_y = ALIGN(y, height_alignment);

   stride = x * cpp;
   stride = bo_tile_pitch(bufmgr, stride, tiling);
   size = stride * aligned_y;
   size = bo_tile_size(bufmgr, size, tiling);
   *pitch = stride;

   if (tiling == I915_TILING_NONE)
      stride = 0;

   return bo_alloc_internal(bufmgr, name, size, flags, tiling, stride, 0);
}

/**
 * Returns a brw_bo wrapping the given buffer object handle.
 *
 * This can be used when one application needs to pass a buffer object
 * to another.
 */
struct brw_bo *
brw_bo_gem_create_from_name(struct brw_bufmgr *bufmgr,
                            const char *name, unsigned int handle)
{
   struct brw_bo *bo;
   int ret;
   struct drm_gem_open open_arg;
   struct drm_i915_gem_get_tiling get_tiling;

   /* At the moment most applications only have a few named bo.
    * For instance, in a DRI client only the render buffers passed
    * between X and the client are named. And since X returns the
    * alternating names for the front/back buffer a linear search
    * provides a sufficiently fast match.
    */
   pthread_mutex_lock(&bufmgr->lock);
   bo = hash_find_bo(bufmgr->name_table, handle);
   if (bo) {
      brw_bo_reference(bo);
      goto out;
   }

   memclear(open_arg);
   open_arg.name = handle;
   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_GEM_OPEN, &open_arg);
   if (ret != 0) {
      DBG("Couldn't reference %s handle 0x%08x: %s\n",
          name, handle, strerror(errno));
      bo = NULL;
      goto out;
   }
   /* Now see if someone has used a prime handle to get this
    * object from the kernel before by looking through the list
    * again for a matching gem_handle
    */
   bo = hash_find_bo(bufmgr->handle_table, open_arg.handle);
   if (bo) {
      brw_bo_reference(bo);
      goto out;
   }

   bo = calloc(1, sizeof(*bo));
   if (!bo)
      goto out;

   p_atomic_set(&bo->refcount, 1);

   bo->size = open_arg.size;
   bo->offset64 = 0;
   bo->virtual = NULL;
   bo->bufmgr = bufmgr;
   bo->gem_handle = open_arg.handle;
   bo->name = name;
   bo->global_name = handle;
   bo->reusable = false;

   _mesa_hash_table_insert(bufmgr->handle_table, &bo->gem_handle, bo);
   _mesa_hash_table_insert(bufmgr->name_table, &bo->global_name, bo);

   memclear(get_tiling);
   get_tiling.handle = bo->gem_handle;
   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling);
   if (ret != 0)
      goto err_unref;

   bo->tiling_mode = get_tiling.tiling_mode;
   bo->swizzle_mode = get_tiling.swizzle_mode;
   /* XXX stride is unknown */
   DBG("bo_create_from_handle: %d (%s)\n", handle, bo->name);

out:
   pthread_mutex_unlock(&bufmgr->lock);
   return bo;

err_unref:
   bo_free(bo);
   pthread_mutex_unlock(&bufmgr->lock);
   return NULL;
}

static void
bo_free(struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   struct drm_gem_close close;
   struct hash_entry *entry;
   int ret;

   if (bo->mem_virtual) {
      VG(VALGRIND_FREELIKE_BLOCK(bo->mem_virtual, 0));
      drm_munmap(bo->mem_virtual, bo->size);
   }
   if (bo->wc_virtual) {
      VG(VALGRIND_FREELIKE_BLOCK(bo->wc_virtual, 0));
      drm_munmap(bo->wc_virtual, bo->size);
   }
   if (bo->gtt_virtual) {
      drm_munmap(bo->gtt_virtual, bo->size);
   }

   if (bo->global_name) {
      entry = _mesa_hash_table_search(bufmgr->name_table, &bo->global_name);
      _mesa_hash_table_remove(bufmgr->name_table, entry);
   }
   entry = _mesa_hash_table_search(bufmgr->handle_table, &bo->gem_handle);
   _mesa_hash_table_remove(bufmgr->handle_table, entry);

   /* Close this object */
   memclear(close);
   close.handle = bo->gem_handle;
   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_GEM_CLOSE, &close);
   if (ret != 0) {
      DBG("DRM_IOCTL_GEM_CLOSE %d failed (%s): %s\n",
          bo->gem_handle, bo->name, strerror(errno));
   }
   free(bo);
}

static void
bo_mark_mmaps_incoherent(struct brw_bo *bo)
{
#if HAVE_VALGRIND
   if (bo->mem_virtual)
      VALGRIND_MAKE_MEM_NOACCESS(bo->mem_virtual, bo->size);

   if (bo->wc_virtual)
      VALGRIND_MAKE_MEM_NOACCESS(bo->wc_virtual, bo->size);

   if (bo->gtt_virtual)
      VALGRIND_MAKE_MEM_NOACCESS(bo->gtt_virtual, bo->size);
#endif
}

/** Frees all cached buffers significantly older than @time. */
static void
cleanup_bo_cache(struct brw_bufmgr *bufmgr, time_t time)
{
   int i;

   if (bufmgr->time == time)
      return;

   for (i = 0; i < bufmgr->num_buckets; i++) {
      struct bo_cache_bucket *bucket = &bufmgr->cache_bucket[i];

      list_for_each_entry_safe(struct brw_bo, bo, &bucket->head, head) {
         if (time - bo->free_time <= 1)
            break;

         list_del(&bo->head);

         bo_free(bo);
      }
   }

   bufmgr->time = time;
}

static void
bo_unreference_final(struct brw_bo *bo, time_t time)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   struct bo_cache_bucket *bucket;

   DBG("bo_unreference final: %d (%s)\n", bo->gem_handle, bo->name);

   /* Clear any left-over mappings */
   if (bo->map_count) {
      DBG("bo freed with non-zero map-count %d\n", bo->map_count);
      bo->map_count = 0;
      bo_mark_mmaps_incoherent(bo);
   }

   bucket = bucket_for_size(bufmgr, bo->size);
   /* Put the buffer into our internal cache for reuse if we can. */
   if (bufmgr->bo_reuse && bo->reusable && bucket != NULL &&
       brw_bo_madvise(bo, I915_MADV_DONTNEED)) {
      bo->free_time = time;

      bo->name = NULL;

      list_addtail(&bo->head, &bucket->head);
   } else {
      bo_free(bo);
   }
}

void
brw_bo_unreference(struct brw_bo *bo)
{
   if (bo == NULL)
      return;

   assert(p_atomic_read(&bo->refcount) > 0);

   if (atomic_add_unless(&bo->refcount, -1, 1)) {
      struct brw_bufmgr *bufmgr = bo->bufmgr;
      struct timespec time;

      clock_gettime(CLOCK_MONOTONIC, &time);

      pthread_mutex_lock(&bufmgr->lock);

      if (p_atomic_dec_zero(&bo->refcount)) {
         bo_unreference_final(bo, time.tv_sec);
         cleanup_bo_cache(bufmgr, time.tv_sec);
      }

      pthread_mutex_unlock(&bufmgr->lock);
   }
}

static void
set_domain(struct brw_context *brw, const char *action,
           struct brw_bo *bo, uint32_t read_domains, uint32_t write_domain)
{
   struct drm_i915_gem_set_domain sd = {
      .handle = bo->gem_handle,
      .read_domains = read_domains,
      .write_domain = write_domain,
   };

   double elapsed = unlikely(brw && brw->perf_debug) ? -get_time() : 0.0;

   if (drmIoctl(bo->bufmgr->fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &sd) != 0) {
      DBG("%s:%d: Error setting memory domains %d (%08x %08x): %s.\n",
          __FILE__, __LINE__, bo->gem_handle, read_domains, write_domain,
          strerror(errno));
   }

   if (unlikely(brw && brw->perf_debug)) {
      elapsed += get_time();
      if (elapsed > 1e-5) /* 0.01ms */
         perf_debug("%s a busy \"%s\" BO stalled and took %.03f ms.\n",
                    action, bo->name, elapsed * 1000);
   }
}

int
brw_bo_map(struct brw_context *brw, struct brw_bo *bo, int write_enable)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   int ret;

   pthread_mutex_lock(&bufmgr->lock);

   if (!bo->mem_virtual) {
      struct drm_i915_gem_mmap mmap_arg;

      DBG("bo_map: %d (%s), map_count=%d\n",
          bo->gem_handle, bo->name, bo->map_count);

      memclear(mmap_arg);
      mmap_arg.handle = bo->gem_handle;
      mmap_arg.size = bo->size;
      ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg);
      if (ret != 0) {
         ret = -errno;
         DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
         pthread_mutex_unlock(&bufmgr->lock);
         return ret;
      }
      bo->map_count++;
      VG(VALGRIND_MALLOCLIKE_BLOCK(mmap_arg.addr_ptr, mmap_arg.size, 0, 1));
      bo->mem_virtual = (void *) (uintptr_t) mmap_arg.addr_ptr;
   }
   DBG("bo_map: %d (%s) -> %p\n", bo->gem_handle, bo->name, bo->mem_virtual);
   bo->virtual = bo->mem_virtual;

   set_domain(brw, "CPU mapping", bo, I915_GEM_DOMAIN_CPU,
              write_enable ? I915_GEM_DOMAIN_CPU : 0);

   bo_mark_mmaps_incoherent(bo);
   VG(VALGRIND_MAKE_MEM_DEFINED(bo->mem_virtual, bo->size));
   pthread_mutex_unlock(&bufmgr->lock);

   return 0;
}

static int
map_gtt(struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   int ret;

   /* Get a mapping of the buffer if we haven't before. */
   if (bo->gtt_virtual == NULL) {
      struct drm_i915_gem_mmap_gtt mmap_arg;

      DBG("bo_map_gtt: mmap %d (%s), map_count=%d\n",
          bo->gem_handle, bo->name, bo->map_count);

      memclear(mmap_arg);
      mmap_arg.handle = bo->gem_handle;

      /* Get the fake offset back... */
      ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg);
      if (ret != 0) {
         ret = -errno;
         DBG("%s:%d: Error preparing buffer map %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
         return ret;
      }

      /* and mmap it */
      bo->gtt_virtual = drm_mmap(0, bo->size, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, bufmgr->fd, mmap_arg.offset);
      if (bo->gtt_virtual == MAP_FAILED) {
         bo->gtt_virtual = NULL;
         ret = -errno;
         DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
         return ret;
      }
   }

   bo->map_count++;
   bo->virtual = bo->gtt_virtual;

   DBG("bo_map_gtt: %d (%s) -> %p\n", bo->gem_handle, bo->name,
       bo->gtt_virtual);

   return 0;
}

int
brw_bo_map_gtt(struct brw_context *brw, struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   int ret;

   pthread_mutex_lock(&bufmgr->lock);

   ret = map_gtt(bo);
   if (ret) {
      pthread_mutex_unlock(&bufmgr->lock);
      return ret;
   }

   /* Now move it to the GTT domain so that the GPU and CPU
    * caches are flushed and the GPU isn't actively using the
    * buffer.
    *
    * The pagefault handler does this domain change for us when
    * it has unbound the BO from the GTT, but it's up to us to
    * tell it when we're about to use things if we had done
    * rendering and it still happens to be bound to the GTT.
    */
   set_domain(brw, "GTT mapping", bo,
              I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);

   bo_mark_mmaps_incoherent(bo);
   VG(VALGRIND_MAKE_MEM_DEFINED(bo->gtt_virtual, bo->size));
   pthread_mutex_unlock(&bufmgr->lock);

   return 0;
}

/**
 * Performs a mapping of the buffer object like the normal GTT
 * mapping, but avoids waiting for the GPU to be done reading from or
 * rendering to the buffer.
 *
 * This is used in the implementation of GL_ARB_map_buffer_range: The
 * user asks to create a buffer, then does a mapping, fills some
 * space, runs a drawing command, then asks to map it again without
 * synchronizing because it guarantees that it won't write over the
 * data that the GPU is busy using (or, more specifically, that if it
 * does write over the data, it acknowledges that rendering is
 * undefined).
 */

int
brw_bo_map_unsynchronized(struct brw_context *brw, struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   int ret;

   /* If the CPU cache isn't coherent with the GTT, then use a
    * regular synchronized mapping.  The problem is that we don't
    * track where the buffer was last used on the CPU side in
    * terms of brw_bo_map vs brw_bo_map_gtt, so
    * we would potentially corrupt the buffer even when the user
    * does reasonable things.
    */
   if (!bufmgr->has_llc)
      return brw_bo_map_gtt(brw, bo);

   pthread_mutex_lock(&bufmgr->lock);

   ret = map_gtt(bo);
   if (ret == 0) {
      bo_mark_mmaps_incoherent(bo);
      VG(VALGRIND_MAKE_MEM_DEFINED(bo->gtt_virtual, bo->size));
   }

   pthread_mutex_unlock(&bufmgr->lock);

   return ret;
}

int
brw_bo_unmap(struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   int ret = 0;

   pthread_mutex_lock(&bufmgr->lock);

   if (bo->map_count <= 0) {
      DBG("attempted to unmap an unmapped bo\n");
      pthread_mutex_unlock(&bufmgr->lock);
      /* Preserve the old behaviour of just treating this as a
       * no-op rather than reporting the error.
       */
      return 0;
   }

   if (--bo->map_count == 0) {
      bo_mark_mmaps_incoherent(bo);
      bo->virtual = NULL;
   }
   pthread_mutex_unlock(&bufmgr->lock);

   return ret;
}

int
brw_bo_subdata(struct brw_bo *bo, uint64_t offset,
               uint64_t size, const void *data)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   struct drm_i915_gem_pwrite pwrite;
   int ret;

   memclear(pwrite);
   pwrite.handle = bo->gem_handle;
   pwrite.offset = offset;
   pwrite.size = size;
   pwrite.data_ptr = (uint64_t) (uintptr_t) data;
   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_PWRITE, &pwrite);
   if (ret != 0) {
      ret = -errno;
      DBG("%s:%d: Error writing data to buffer %d: "
          "(%"PRIu64" %"PRIu64") %s .\n",
          __FILE__, __LINE__, bo->gem_handle, offset, size, strerror(errno));
   }

   return ret;
}

int
brw_bo_get_subdata(struct brw_bo *bo, uint64_t offset,
                   uint64_t size, void *data)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   struct drm_i915_gem_pread pread;
   int ret;

   memclear(pread);
   pread.handle = bo->gem_handle;
   pread.offset = offset;
   pread.size = size;
   pread.data_ptr = (uint64_t) (uintptr_t) data;
   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_PREAD, &pread);
   if (ret != 0) {
      ret = -errno;
      DBG("%s:%d: Error reading data from buffer %d: "
          "(%"PRIu64" %"PRIu64") %s .\n",
          __FILE__, __LINE__, bo->gem_handle, offset, size, strerror(errno));
   }

   return ret;
}

/** Waits for all GPU rendering with the object to have completed. */
void
brw_bo_wait_rendering(struct brw_context *brw, struct brw_bo *bo)
{
   set_domain(brw, "waiting for",
              bo, I915_GEM_DOMAIN_GTT, I915_GEM_DOMAIN_GTT);
}

/**
 * Waits on a BO for the given amount of time.
 *
 * @bo: buffer object to wait for
 * @timeout_ns: amount of time to wait in nanoseconds.
 *   If value is less than 0, an infinite wait will occur.
 *
 * Returns 0 if the wait was successful ie. the last batch referencing the
 * object has completed within the allotted time. Otherwise some negative return
 * value describes the error. Of particular interest is -ETIME when the wait has
 * failed to yield the desired result.
 *
 * Similar to brw_bo_wait_rendering except a timeout parameter allows
 * the operation to give up after a certain amount of time. Another subtle
 * difference is the internal locking semantics are different (this variant does
 * not hold the lock for the duration of the wait). This makes the wait subject
 * to a larger userspace race window.
 *
 * The implementation shall wait until the object is no longer actively
 * referenced within a batch buffer at the time of the call. The wait will
 * not guarantee that the buffer is re-issued via another thread, or an flinked
 * handle. Userspace must make sure this race does not occur if such precision
 * is important.
 *
 * Note that some kernels have broken the inifite wait for negative values
 * promise, upgrade to latest stable kernels if this is the case.
 */
int
brw_bo_wait(struct brw_bo *bo, int64_t timeout_ns)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   struct drm_i915_gem_wait wait;
   int ret;

   memclear(wait);
   wait.bo_handle = bo->gem_handle;
   wait.timeout_ns = timeout_ns;
   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_WAIT, &wait);
   if (ret == -1)
      return -errno;

   return ret;
}

void
brw_bufmgr_destroy(struct brw_bufmgr *bufmgr)
{
   pthread_mutex_destroy(&bufmgr->lock);

   /* Free any cached buffer objects we were going to reuse */
   for (int i = 0; i < bufmgr->num_buckets; i++) {
      struct bo_cache_bucket *bucket = &bufmgr->cache_bucket[i];

      list_for_each_entry_safe(struct brw_bo, bo, &bucket->head, head) {
         list_del(&bo->head);

         bo_free(bo);
      }
   }

   _mesa_hash_table_destroy(bufmgr->name_table, NULL);
   _mesa_hash_table_destroy(bufmgr->handle_table, NULL);

   free(bufmgr);
}

static int
bo_set_tiling_internal(struct brw_bo *bo, uint32_t tiling_mode,
                       uint32_t stride)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;
   struct drm_i915_gem_set_tiling set_tiling;
   int ret;

   if (bo->global_name == 0 &&
       tiling_mode == bo->tiling_mode && stride == bo->stride)
      return 0;

   memset(&set_tiling, 0, sizeof(set_tiling));
   do {
      /* set_tiling is slightly broken and overwrites the
       * input on the error path, so we have to open code
       * rmIoctl.
       */
      set_tiling.handle = bo->gem_handle;
      set_tiling.tiling_mode = tiling_mode;
      set_tiling.stride = stride;

      ret = ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
   if (ret == -1)
      return -errno;

   bo->tiling_mode = set_tiling.tiling_mode;
   bo->swizzle_mode = set_tiling.swizzle_mode;
   bo->stride = set_tiling.stride;
   return 0;
}

int
brw_bo_get_tiling(struct brw_bo *bo, uint32_t *tiling_mode,
                  uint32_t *swizzle_mode)
{
   *tiling_mode = bo->tiling_mode;
   *swizzle_mode = bo->swizzle_mode;
   return 0;
}

struct brw_bo *
brw_bo_gem_create_from_prime(struct brw_bufmgr *bufmgr, int prime_fd,
                             int size)
{
   int ret;
   uint32_t handle;
   struct brw_bo *bo;
   struct drm_i915_gem_get_tiling get_tiling;

   pthread_mutex_lock(&bufmgr->lock);
   ret = drmPrimeFDToHandle(bufmgr->fd, prime_fd, &handle);
   if (ret) {
      DBG("create_from_prime: failed to obtain handle from fd: %s\n",
          strerror(errno));
      pthread_mutex_unlock(&bufmgr->lock);
      return NULL;
   }

   /*
    * See if the kernel has already returned this buffer to us. Just as
    * for named buffers, we must not create two bo's pointing at the same
    * kernel object
    */
   bo = hash_find_bo(bufmgr->handle_table, handle);
   if (bo) {
      brw_bo_reference(bo);
      goto out;
   }

   bo = calloc(1, sizeof(*bo));
   if (!bo)
      goto out;

   p_atomic_set(&bo->refcount, 1);

   /* Determine size of bo.  The fd-to-handle ioctl really should
    * return the size, but it doesn't.  If we have kernel 3.12 or
    * later, we can lseek on the prime fd to get the size.  Older
    * kernels will just fail, in which case we fall back to the
    * provided (estimated or guess size). */
   ret = lseek(prime_fd, 0, SEEK_END);
   if (ret != -1)
      bo->size = ret;
   else
      bo->size = size;

   bo->bufmgr = bufmgr;

   bo->gem_handle = handle;
   _mesa_hash_table_insert(bufmgr->handle_table, &bo->gem_handle, bo);

   bo->name = "prime";
   bo->reusable = false;

   memclear(get_tiling);
   get_tiling.handle = bo->gem_handle;
   if (drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling))
      goto err;

   bo->tiling_mode = get_tiling.tiling_mode;
   bo->swizzle_mode = get_tiling.swizzle_mode;
   /* XXX stride is unknown */

out:
   pthread_mutex_unlock(&bufmgr->lock);
   return bo;

err:
   bo_free(bo);
   pthread_mutex_unlock(&bufmgr->lock);
   return NULL;
}

int
brw_bo_gem_export_to_prime(struct brw_bo *bo, int *prime_fd)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;

   if (drmPrimeHandleToFD(bufmgr->fd, bo->gem_handle,
                          DRM_CLOEXEC, prime_fd) != 0)
      return -errno;

   bo->reusable = false;

   return 0;
}

int
brw_bo_flink(struct brw_bo *bo, uint32_t *name)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;

   if (!bo->global_name) {
      struct drm_gem_flink flink;

      memclear(flink);
      flink.handle = bo->gem_handle;
      if (drmIoctl(bufmgr->fd, DRM_IOCTL_GEM_FLINK, &flink))
         return -errno;

      pthread_mutex_lock(&bufmgr->lock);
      if (!bo->global_name) {
         bo->global_name = flink.name;
         bo->reusable = false;

         _mesa_hash_table_insert(bufmgr->name_table, &bo->global_name, bo);
      }
      pthread_mutex_unlock(&bufmgr->lock);
   }

   *name = bo->global_name;
   return 0;
}

/**
 * Enables unlimited caching of buffer objects for reuse.
 *
 * This is potentially very memory expensive, as the cache at each bucket
 * size is only bounded by how many buffers of that size we've managed to have
 * in flight at once.
 */
void
brw_bufmgr_enable_reuse(struct brw_bufmgr *bufmgr)
{
   bufmgr->bo_reuse = true;
}

static void
add_bucket(struct brw_bufmgr *bufmgr, int size)
{
   unsigned int i = bufmgr->num_buckets;

   assert(i < ARRAY_SIZE(bufmgr->cache_bucket));

   list_inithead(&bufmgr->cache_bucket[i].head);
   bufmgr->cache_bucket[i].size = size;
   bufmgr->num_buckets++;
}

static void
init_cache_buckets(struct brw_bufmgr *bufmgr)
{
   uint64_t size, cache_max_size = 64 * 1024 * 1024;

   /* OK, so power of two buckets was too wasteful of memory.
    * Give 3 other sizes between each power of two, to hopefully
    * cover things accurately enough.  (The alternative is
    * probably to just go for exact matching of sizes, and assume
    * that for things like composited window resize the tiled
    * width/height alignment and rounding of sizes to pages will
    * get us useful cache hit rates anyway)
    */
   add_bucket(bufmgr, 4096);
   add_bucket(bufmgr, 4096 * 2);
   add_bucket(bufmgr, 4096 * 3);

   /* Initialize the linked lists for BO reuse cache. */
   for (size = 4 * 4096; size <= cache_max_size; size *= 2) {
      add_bucket(bufmgr, size);

      add_bucket(bufmgr, size + size * 1 / 4);
      add_bucket(bufmgr, size + size * 2 / 4);
      add_bucket(bufmgr, size + size * 3 / 4);
   }
}

uint32_t
brw_create_hw_context(struct brw_bufmgr *bufmgr)
{
   struct drm_i915_gem_context_create create;
   int ret;

   memclear(create);
   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);
   if (ret != 0) {
      DBG("DRM_IOCTL_I915_GEM_CONTEXT_CREATE failed: %s\n", strerror(errno));
      return 0;
   }

   return create.ctx_id;
}

void
brw_destroy_hw_context(struct brw_bufmgr *bufmgr, uint32_t ctx_id)
{
   struct drm_i915_gem_context_destroy d = {.ctx_id = ctx_id };

   if (ctx_id != 0 &&
       drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &d) != 0) {
      fprintf(stderr, "DRM_IOCTL_I915_GEM_CONTEXT_DESTROY failed: %s\n",
              strerror(errno));
   }
}

int
brw_reg_read(struct brw_bufmgr *bufmgr, uint32_t offset, uint64_t *result)
{
   struct drm_i915_reg_read reg_read;
   int ret;

   memclear(reg_read);
   reg_read.offset = offset;

   ret = drmIoctl(bufmgr->fd, DRM_IOCTL_I915_REG_READ, &reg_read);

   *result = reg_read.val;
   return ret;
}

void *
brw_bo_map__gtt(struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;

   if (bo->gtt_virtual)
      return bo->gtt_virtual;

   pthread_mutex_lock(&bufmgr->lock);
   if (bo->gtt_virtual == NULL) {
      struct drm_i915_gem_mmap_gtt mmap_arg;
      void *ptr;

      DBG("bo_map_gtt: mmap %d (%s), map_count=%d\n",
          bo->gem_handle, bo->name, bo->map_count);

      memclear(mmap_arg);
      mmap_arg.handle = bo->gem_handle;

      /* Get the fake offset back... */
      ptr = MAP_FAILED;
      if (drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg) == 0) {
         /* and mmap it */
         ptr = drm_mmap(0, bo->size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, bufmgr->fd, mmap_arg.offset);
      }
      if (ptr == MAP_FAILED) {
         --bo->map_count;
         ptr = NULL;
      }

      bo->gtt_virtual = ptr;
   }
   pthread_mutex_unlock(&bufmgr->lock);

   return bo->gtt_virtual;
}

void *
brw_bo_map__cpu(struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;

   if (bo->mem_virtual)
      return bo->mem_virtual;

   pthread_mutex_lock(&bufmgr->lock);
   if (!bo->mem_virtual) {
      struct drm_i915_gem_mmap mmap_arg;

      DBG("bo_map: %d (%s), map_count=%d\n",
          bo->gem_handle, bo->name, bo->map_count);

      memclear(mmap_arg);
      mmap_arg.handle = bo->gem_handle;
      mmap_arg.size = bo->size;
      if (drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg)) {
         DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
      } else {
         bo->map_count++;
         VG(VALGRIND_MALLOCLIKE_BLOCK
            (mmap_arg.addr_ptr, mmap_arg.size, 0, 1));
         bo->mem_virtual = (void *) (uintptr_t) mmap_arg.addr_ptr;
      }
   }
   pthread_mutex_unlock(&bufmgr->lock);

   return bo->mem_virtual;
}

void *
brw_bo_map__wc(struct brw_bo *bo)
{
   struct brw_bufmgr *bufmgr = bo->bufmgr;

   if (bo->wc_virtual)
      return bo->wc_virtual;

   pthread_mutex_lock(&bufmgr->lock);
   if (!bo->wc_virtual) {
      struct drm_i915_gem_mmap mmap_arg;

      DBG("bo_map: %d (%s), map_count=%d\n",
          bo->gem_handle, bo->name, bo->map_count);

      memclear(mmap_arg);
      mmap_arg.handle = bo->gem_handle;
      mmap_arg.size = bo->size;
      mmap_arg.flags = I915_MMAP_WC;
      if (drmIoctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg)) {
         DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
      } else {
         bo->map_count++;
         VG(VALGRIND_MALLOCLIKE_BLOCK
            (mmap_arg.addr_ptr, mmap_arg.size, 0, 1));
         bo->wc_virtual = (void *) (uintptr_t) mmap_arg.addr_ptr;
      }
   }
   pthread_mutex_unlock(&bufmgr->lock);

   return bo->wc_virtual;
}

/**
 * Initializes the GEM buffer manager, which uses the kernel to allocate, map,
 * and manage map buffer objections.
 *
 * \param fd File descriptor of the opened DRM device.
 */
struct brw_bufmgr *
brw_bufmgr_init(struct gen_device_info *devinfo, int fd, int batch_size)
{
   struct brw_bufmgr *bufmgr;

   bufmgr = calloc(1, sizeof(*bufmgr));
   if (bufmgr == NULL)
      return NULL;

   /* Handles to buffer objects belong to the device fd and are not
    * reference counted by the kernel.  If the same fd is used by
    * multiple parties (threads sharing the same screen bufmgr, or
    * even worse the same device fd passed to multiple libraries)
    * ownership of those handles is shared by those independent parties.
    *
    * Don't do this! Ensure that each library/bufmgr has its own device
    * fd so that its namespace does not clash with another.
    */
   bufmgr->fd = fd;

   if (pthread_mutex_init(&bufmgr->lock, NULL) != 0) {
      free(bufmgr);
      return NULL;
   }

   bufmgr->has_llc = devinfo->has_llc;

   init_cache_buckets(bufmgr);

   bufmgr->name_table =
      _mesa_hash_table_create(NULL, key_hash_uint, key_uint_equal);
   bufmgr->handle_table =
      _mesa_hash_table_create(NULL, key_hash_uint, key_uint_equal);

   return bufmgr;
}
