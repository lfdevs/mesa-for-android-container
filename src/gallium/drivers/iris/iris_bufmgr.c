/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_bufmgr.c
 *
 * The Iris buffer manager.
 *
 * XXX: write better comments
 * - BOs
 * - Explain BO cache
 * - main interface to GEM in the kernel
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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <time.h>

#include "errno.h"
#ifndef ETIME
#define ETIME ETIMEDOUT
#endif
#include "common/gen_clflush.h"
#include "dev/gen_debug.h"
#include "common/gen_gem.h"
#include "dev/gen_device_info.h"
#include "main/macros.h"
#include "util/debug.h"
#include "util/macros.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/u_dynarray.h"
#include "util/vma.h"
#include "iris_bufmgr.h"
#include "iris_context.h"
#include "string.h"

#include "drm-uapi/i915_drm.h"

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x)
#endif

/* VALGRIND_FREELIKE_BLOCK unfortunately does not actually undo the earlier
 * VALGRIND_MALLOCLIKE_BLOCK but instead leaves vg convinced the memory is
 * leaked. All because it does not call VG(cli_free) from its
 * VG_USERREQ__FREELIKE_BLOCK handler. Instead of treating the memory like
 * and allocation, we mark it available for use upon mmapping and remove
 * it upon unmapping.
 */
#define VG_DEFINED(ptr, size) VG(VALGRIND_MAKE_MEM_DEFINED(ptr, size))
#define VG_NOACCESS(ptr, size) VG(VALGRIND_MAKE_MEM_NOACCESS(ptr, size))

#define PAGE_SIZE 4096

#define FILE_DEBUG_FLAG DEBUG_BUFMGR

/**
 * Call ioctl, restarting if it is interupted
 */
int
drm_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
}

static inline int
atomic_add_unless(int *v, int add, int unless)
{
   int c, old;
   c = p_atomic_read(v);
   while (c != unless && (old = p_atomic_cmpxchg(v, c, c + add)) != c)
      c = old;
   return c == unless;
}

static const char *
memzone_name(enum iris_memory_zone memzone)
{
   const char *names[] = {
      [IRIS_MEMZONE_SHADER]  = "shader",
      [IRIS_MEMZONE_BINDER]  = "binder",
      [IRIS_MEMZONE_SURFACE] = "surface",
      [IRIS_MEMZONE_DYNAMIC] = "dynamic",
      [IRIS_MEMZONE_OTHER]   = "other",
      [IRIS_MEMZONE_BORDER_COLOR_POOL] = "bordercolor",
   };
   assert(memzone < ARRAY_SIZE(names));
   return names[memzone];
}

struct bo_cache_bucket {
   /** List of cached BOs. */
   struct list_head head;

   /** Size of this bucket, in bytes. */
   uint64_t size;
};

struct iris_bufmgr {
   int fd;

   mtx_t lock;

   /** Array of lists of cached gem objects of power-of-two sizes */
   struct bo_cache_bucket cache_bucket[14 * 4];
   int num_buckets;
   time_t time;

   struct hash_table *name_table;
   struct hash_table *handle_table;

   struct util_vma_heap vma_allocator[IRIS_MEMZONE_COUNT];

   bool has_llc:1;
   bool bo_reuse:1;
};

static int bo_set_tiling_internal(struct iris_bo *bo, uint32_t tiling_mode,
                                  uint32_t stride);

static void bo_free(struct iris_bo *bo);

static uint64_t vma_alloc(struct iris_bufmgr *bufmgr,
                          enum iris_memory_zone memzone,
                          uint64_t size, uint64_t alignment);

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

static struct iris_bo *
hash_find_bo(struct hash_table *ht, unsigned int key)
{
   struct hash_entry *entry = _mesa_hash_table_search(ht, &key);
   return entry ? (struct iris_bo *) entry->data : NULL;
}

/**
 * This function finds the correct bucket fit for the input size.
 * The function works with O(1) complexity when the requested size
 * was queried instead of iterating the size through all the buckets.
 */
static struct bo_cache_bucket *
bucket_for_size(struct iris_bufmgr *bufmgr, uint64_t size)
{
   /* Calculating the pages and rounding up to the page size. */
   const unsigned pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

   /* Row  Bucket sizes    clz((x-1) | 3)   Row    Column
    *        in pages                      stride   size
    *   0:   1  2  3  4 -> 30 30 30 30        4       1
    *   1:   5  6  7  8 -> 29 29 29 29        4       1
    *   2:  10 12 14 16 -> 28 28 28 28        8       2
    *   3:  20 24 28 32 -> 27 27 27 27       16       4
    */
   const unsigned row = 30 - __builtin_clz((pages - 1) | 3);
   const unsigned row_max_pages = 4 << row;

   /* The '& ~2' is the special case for row 1. In row 1, max pages /
    * 2 is 2, but the previous row maximum is zero (because there is
    * no previous row). All row maximum sizes are power of 2, so that
    * is the only case where that bit will be set.
    */
   const unsigned prev_row_max_pages = (row_max_pages / 2) & ~2;
   int col_size_log2 = row - 1;
   col_size_log2 += (col_size_log2 < 0);

   const unsigned col = (pages - prev_row_max_pages +
                        ((1 << col_size_log2) - 1)) >> col_size_log2;

   /* Calculating the index based on the row and column. */
   const unsigned index = (row * 4) + (col - 1);

   return (index < bufmgr->num_buckets) ?
          &bufmgr->cache_bucket[index] : NULL;
}

enum iris_memory_zone
iris_memzone_for_address(uint64_t address)
{
   STATIC_ASSERT(IRIS_MEMZONE_OTHER_START   > IRIS_MEMZONE_DYNAMIC_START);
   STATIC_ASSERT(IRIS_MEMZONE_DYNAMIC_START > IRIS_MEMZONE_SURFACE_START);
   STATIC_ASSERT(IRIS_MEMZONE_SURFACE_START > IRIS_MEMZONE_BINDER_START);
   STATIC_ASSERT(IRIS_MEMZONE_BINDER_START  > IRIS_MEMZONE_SHADER_START);
   STATIC_ASSERT(IRIS_BORDER_COLOR_POOL_ADDRESS == IRIS_MEMZONE_DYNAMIC_START);

   if (address >= IRIS_MEMZONE_OTHER_START)
      return IRIS_MEMZONE_OTHER;

   if (address == IRIS_BORDER_COLOR_POOL_ADDRESS)
      return IRIS_MEMZONE_BORDER_COLOR_POOL;

   if (address > IRIS_MEMZONE_DYNAMIC_START)
      return IRIS_MEMZONE_DYNAMIC;

   if (address >= IRIS_MEMZONE_SURFACE_START)
      return IRIS_MEMZONE_SURFACE;

   if (address >= IRIS_MEMZONE_BINDER_START)
      return IRIS_MEMZONE_BINDER;

   return IRIS_MEMZONE_SHADER;
}

/**
 * Allocate a section of virtual memory for a buffer, assigning an address.
 *
 * This uses either the bucket allocator for the given size, or the large
 * object allocator (util_vma).
 */
static uint64_t
vma_alloc(struct iris_bufmgr *bufmgr,
          enum iris_memory_zone memzone,
          uint64_t size,
          uint64_t alignment)
{
   /* Force alignment to be some number of pages */
   alignment = ALIGN(alignment, PAGE_SIZE);

   if (memzone == IRIS_MEMZONE_BORDER_COLOR_POOL)
      return IRIS_BORDER_COLOR_POOL_ADDRESS;

   /* The binder handles its own allocations.  Return non-zero here. */
   if (memzone == IRIS_MEMZONE_BINDER)
      return IRIS_MEMZONE_BINDER_START;

   uint64_t addr =
      util_vma_heap_alloc(&bufmgr->vma_allocator[memzone], size, alignment);

   assert((addr >> 48ull) == 0);
   assert((addr % alignment) == 0);

   return gen_canonical_address(addr);
}

static void
vma_free(struct iris_bufmgr *bufmgr,
         uint64_t address,
         uint64_t size)
{
   if (address == IRIS_BORDER_COLOR_POOL_ADDRESS)
      return;

   /* Un-canonicalize the address. */
   address = gen_48b_address(address);

   if (address == 0ull)
      return;

   enum iris_memory_zone memzone = iris_memzone_for_address(address);

   /* The binder handles its own allocations. */
   if (memzone == IRIS_MEMZONE_BINDER)
      return;

   util_vma_heap_free(&bufmgr->vma_allocator[memzone], address, size);
}

int
iris_bo_busy(struct iris_bo *bo)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;
   struct drm_i915_gem_busy busy = { .handle = bo->gem_handle };

   int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_BUSY, &busy);
   if (ret == 0) {
      bo->idle = !busy.busy;
      return busy.busy;
   }
   return false;
}

int
iris_bo_madvise(struct iris_bo *bo, int state)
{
   struct drm_i915_gem_madvise madv = {
      .handle = bo->gem_handle,
      .madv = state,
      .retained = 1,
   };

   drm_ioctl(bo->bufmgr->fd, DRM_IOCTL_I915_GEM_MADVISE, &madv);

   return madv.retained;
}

/* drop the oldest entries that have been purged by the kernel */
static void
iris_bo_cache_purge_bucket(struct iris_bufmgr *bufmgr,
                          struct bo_cache_bucket *bucket)
{
   list_for_each_entry_safe(struct iris_bo, bo, &bucket->head, head) {
      if (iris_bo_madvise(bo, I915_MADV_DONTNEED))
         break;

      list_del(&bo->head);
      bo_free(bo);
   }
}

static struct iris_bo *
bo_calloc(void)
{
   struct iris_bo *bo = calloc(1, sizeof(*bo));
   if (bo) {
      bo->hash = _mesa_hash_pointer(bo);
   }
   return bo;
}

static struct iris_bo *
bo_alloc_internal(struct iris_bufmgr *bufmgr,
                  const char *name,
                  uint64_t size,
                  enum iris_memory_zone memzone,
                  unsigned flags,
                  uint32_t tiling_mode,
                  uint32_t stride)
{
   struct iris_bo *bo;
   unsigned int page_size = getpagesize();
   int ret;
   struct bo_cache_bucket *bucket;
   bool alloc_from_cache;
   uint64_t bo_size;
   bool zeroed = false;

   if (flags & BO_ALLOC_ZEROED)
      zeroed = true;

   if ((flags & BO_ALLOC_COHERENT) && !bufmgr->has_llc) {
      bo_size = MAX2(ALIGN(size, page_size), page_size);
      bucket = NULL;
      goto skip_cache;
   }

   /* Round the allocated size up to a power of two number of pages. */
   bucket = bucket_for_size(bufmgr, size);

   /* If we don't have caching at this size, don't actually round the
    * allocation up.
    */
   if (bucket == NULL) {
      bo_size = MAX2(ALIGN(size, page_size), page_size);
   } else {
      bo_size = bucket->size;
   }

   mtx_lock(&bufmgr->lock);
   /* Get a buffer out of the cache if available */
retry:
   alloc_from_cache = false;
   if (bucket != NULL && !list_empty(&bucket->head)) {
      /* If the last BO in the cache is idle, then reuse it.  Otherwise,
       * allocate a fresh buffer to avoid stalling.
       */
      bo = LIST_ENTRY(struct iris_bo, bucket->head.next, head);
      if (!iris_bo_busy(bo)) {
         alloc_from_cache = true;
         list_del(&bo->head);
      }

      if (alloc_from_cache) {
         if (!iris_bo_madvise(bo, I915_MADV_WILLNEED)) {
            bo_free(bo);
            iris_bo_cache_purge_bucket(bufmgr, bucket);
            goto retry;
         }

         if (bo_set_tiling_internal(bo, tiling_mode, stride)) {
            bo_free(bo);
            goto retry;
         }

         if (zeroed) {
            void *map = iris_bo_map(NULL, bo, MAP_WRITE | MAP_RAW);
            if (!map) {
               bo_free(bo);
               goto retry;
            }
            memset(map, 0, bo_size);
         }
      }
   }

   if (alloc_from_cache) {
      /* If the cached BO isn't in the right memory zone, free the old
       * memory and assign it a new address.
       */
      if (memzone != iris_memzone_for_address(bo->gtt_offset)) {
         vma_free(bufmgr, bo->gtt_offset, bo->size);
         bo->gtt_offset = 0ull;
      }
   } else {
skip_cache:
      bo = bo_calloc();
      if (!bo)
         goto err;

      bo->size = bo_size;
      bo->idle = true;

      struct drm_i915_gem_create create = { .size = bo_size };

      /* All new BOs we get from the kernel are zeroed, so we don't need to
       * worry about that here.
       */
      ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_CREATE, &create);
      if (ret != 0) {
         free(bo);
         goto err;
      }

      bo->gem_handle = create.handle;

      bo->bufmgr = bufmgr;

      bo->tiling_mode = I915_TILING_NONE;
      bo->swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
      bo->stride = 0;

      if (bo_set_tiling_internal(bo, tiling_mode, stride))
         goto err_free;

      /* Calling set_domain() will allocate pages for the BO outside of the
       * struct mutex lock in the kernel, which is more efficient than waiting
       * to create them during the first execbuf that uses the BO.
       */
      struct drm_i915_gem_set_domain sd = {
         .handle = bo->gem_handle,
         .read_domains = I915_GEM_DOMAIN_CPU,
         .write_domain = 0,
      };

      if (drm_ioctl(bo->bufmgr->fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &sd) != 0)
         goto err_free;
   }

   bo->name = name;
   p_atomic_set(&bo->refcount, 1);
   bo->reusable = bucket && bufmgr->bo_reuse;
   bo->cache_coherent = bufmgr->has_llc;
   bo->index = -1;
   bo->kflags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_PINNED;

   /* By default, capture all driver-internal buffers like shader kernels,
    * surface states, dynamic states, border colors, and so on.
    */
   if (memzone < IRIS_MEMZONE_OTHER)
      bo->kflags |= EXEC_OBJECT_CAPTURE;

   if (bo->gtt_offset == 0ull) {
      bo->gtt_offset = vma_alloc(bufmgr, memzone, bo->size, 1);

      if (bo->gtt_offset == 0ull)
         goto err_free;
   }

   mtx_unlock(&bufmgr->lock);

   if ((flags & BO_ALLOC_COHERENT) && !bo->cache_coherent) {
      struct drm_i915_gem_caching arg = {
         .handle = bo->gem_handle,
         .caching = 1,
      };
      if (drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_SET_CACHING, &arg) == 0) {
         bo->cache_coherent = true;
         bo->reusable = false;
      }
   }

   DBG("bo_create: buf %d (%s) (%s memzone) %llub\n", bo->gem_handle,
       bo->name, memzone_name(memzone), (unsigned long long) size);

   return bo;

err_free:
   bo_free(bo);
err:
   mtx_unlock(&bufmgr->lock);
   return NULL;
}

struct iris_bo *
iris_bo_alloc(struct iris_bufmgr *bufmgr,
              const char *name,
              uint64_t size,
              enum iris_memory_zone memzone)
{
   return bo_alloc_internal(bufmgr, name, size, memzone,
                            0, I915_TILING_NONE, 0);
}

struct iris_bo *
iris_bo_alloc_tiled(struct iris_bufmgr *bufmgr, const char *name,
                    uint64_t size, enum iris_memory_zone memzone,
                    uint32_t tiling_mode, uint32_t pitch, unsigned flags)
{
   return bo_alloc_internal(bufmgr, name, size, memzone,
                            flags, tiling_mode, pitch);
}

struct iris_bo *
iris_bo_create_userptr(struct iris_bufmgr *bufmgr, const char *name,
                       void *ptr, size_t size,
                       enum iris_memory_zone memzone)
{
   struct iris_bo *bo;

   bo = bo_calloc();
   if (!bo)
      return NULL;

   struct drm_i915_gem_userptr arg = {
      .user_ptr = (uintptr_t)ptr,
      .user_size = size,
   };
   if (drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_USERPTR, &arg))
      goto err_free;
   bo->gem_handle = arg.handle;

   /* Check the buffer for validity before we try and use it in a batch */
   struct drm_i915_gem_set_domain sd = {
      .handle = bo->gem_handle,
      .read_domains = I915_GEM_DOMAIN_CPU,
   };
   if (drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &sd))
      goto err_close;

   bo->name = name;
   bo->size = size;
   bo->map_cpu = ptr;

   bo->bufmgr = bufmgr;
   bo->kflags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_PINNED;
   bo->gtt_offset = vma_alloc(bufmgr, memzone, size, 1);
   if (bo->gtt_offset == 0ull)
      goto err_close;

   p_atomic_set(&bo->refcount, 1);
   bo->userptr = true;
   bo->cache_coherent = true;
   bo->index = -1;
   bo->idle = true;

   return bo;

err_close:
   drm_ioctl(bufmgr->fd, DRM_IOCTL_GEM_CLOSE, &bo->gem_handle);
err_free:
   free(bo);
   return NULL;
}

/**
 * Returns a iris_bo wrapping the given buffer object handle.
 *
 * This can be used when one application needs to pass a buffer object
 * to another.
 */
struct iris_bo *
iris_bo_gem_create_from_name(struct iris_bufmgr *bufmgr,
                             const char *name, unsigned int handle)
{
   struct iris_bo *bo;

   /* At the moment most applications only have a few named bo.
    * For instance, in a DRI client only the render buffers passed
    * between X and the client are named. And since X returns the
    * alternating names for the front/back buffer a linear search
    * provides a sufficiently fast match.
    */
   mtx_lock(&bufmgr->lock);
   bo = hash_find_bo(bufmgr->name_table, handle);
   if (bo) {
      iris_bo_reference(bo);
      goto out;
   }

   struct drm_gem_open open_arg = { .name = handle };
   int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_GEM_OPEN, &open_arg);
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
      iris_bo_reference(bo);
      goto out;
   }

   bo = bo_calloc();
   if (!bo)
      goto out;

   p_atomic_set(&bo->refcount, 1);

   bo->size = open_arg.size;
   bo->gtt_offset = 0;
   bo->bufmgr = bufmgr;
   bo->gem_handle = open_arg.handle;
   bo->name = name;
   bo->global_name = handle;
   bo->reusable = false;
   bo->external = true;
   bo->kflags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_PINNED;
   bo->gtt_offset = vma_alloc(bufmgr, IRIS_MEMZONE_OTHER, bo->size, 1);

   _mesa_hash_table_insert(bufmgr->handle_table, &bo->gem_handle, bo);
   _mesa_hash_table_insert(bufmgr->name_table, &bo->global_name, bo);

   struct drm_i915_gem_get_tiling get_tiling = { .handle = bo->gem_handle };
   ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling);
   if (ret != 0)
      goto err_unref;

   bo->tiling_mode = get_tiling.tiling_mode;
   bo->swizzle_mode = get_tiling.swizzle_mode;
   /* XXX stride is unknown */
   DBG("bo_create_from_handle: %d (%s)\n", handle, bo->name);

out:
   mtx_unlock(&bufmgr->lock);
   return bo;

err_unref:
   bo_free(bo);
   mtx_unlock(&bufmgr->lock);
   return NULL;
}

static void
bo_free(struct iris_bo *bo)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   if (bo->map_cpu && !bo->userptr) {
      VG_NOACCESS(bo->map_cpu, bo->size);
      munmap(bo->map_cpu, bo->size);
   }
   if (bo->map_wc) {
      VG_NOACCESS(bo->map_wc, bo->size);
      munmap(bo->map_wc, bo->size);
   }
   if (bo->map_gtt) {
      VG_NOACCESS(bo->map_gtt, bo->size);
      munmap(bo->map_gtt, bo->size);
   }

   if (bo->external) {
      struct hash_entry *entry;

      if (bo->global_name) {
         entry = _mesa_hash_table_search(bufmgr->name_table, &bo->global_name);
         _mesa_hash_table_remove(bufmgr->name_table, entry);
      }

      entry = _mesa_hash_table_search(bufmgr->handle_table, &bo->gem_handle);
      _mesa_hash_table_remove(bufmgr->handle_table, entry);
   }

   /* Close this object */
   struct drm_gem_close close = { .handle = bo->gem_handle };
   int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_GEM_CLOSE, &close);
   if (ret != 0) {
      DBG("DRM_IOCTL_GEM_CLOSE %d failed (%s): %s\n",
          bo->gem_handle, bo->name, strerror(errno));
   }

   vma_free(bo->bufmgr, bo->gtt_offset, bo->size);

   free(bo);
}

/** Frees all cached buffers significantly older than @time. */
static void
cleanup_bo_cache(struct iris_bufmgr *bufmgr, time_t time)
{
   int i;

   if (bufmgr->time == time)
      return;

   for (i = 0; i < bufmgr->num_buckets; i++) {
      struct bo_cache_bucket *bucket = &bufmgr->cache_bucket[i];

      list_for_each_entry_safe(struct iris_bo, bo, &bucket->head, head) {
         if (time - bo->free_time <= 1)
            break;

         list_del(&bo->head);

         bo_free(bo);
      }
   }

   bufmgr->time = time;
}

static void
bo_unreference_final(struct iris_bo *bo, time_t time)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;
   struct bo_cache_bucket *bucket;

   DBG("bo_unreference final: %d (%s)\n", bo->gem_handle, bo->name);

   bucket = NULL;
   if (bo->reusable)
      bucket = bucket_for_size(bufmgr, bo->size);
   /* Put the buffer into our internal cache for reuse if we can. */
   if (bucket && iris_bo_madvise(bo, I915_MADV_DONTNEED)) {
      bo->free_time = time;
      bo->name = NULL;

      list_addtail(&bo->head, &bucket->head);
   } else {
      bo_free(bo);
   }
}

void
iris_bo_unreference(struct iris_bo *bo)
{
   if (bo == NULL)
      return;

   assert(p_atomic_read(&bo->refcount) > 0);

   if (atomic_add_unless(&bo->refcount, -1, 1)) {
      struct iris_bufmgr *bufmgr = bo->bufmgr;
      struct timespec time;

      clock_gettime(CLOCK_MONOTONIC, &time);

      mtx_lock(&bufmgr->lock);

      if (p_atomic_dec_zero(&bo->refcount)) {
         bo_unreference_final(bo, time.tv_sec);
         cleanup_bo_cache(bufmgr, time.tv_sec);
      }

      mtx_unlock(&bufmgr->lock);
   }
}

static void
bo_wait_with_stall_warning(struct pipe_debug_callback *dbg,
                           struct iris_bo *bo,
                           const char *action)
{
   bool busy = dbg && !bo->idle;
   double elapsed = unlikely(busy) ? -get_time() : 0.0;

   iris_bo_wait_rendering(bo);

   if (unlikely(busy)) {
      elapsed += get_time();
      if (elapsed > 1e-5) /* 0.01ms */ {
         perf_debug(dbg, "%s a busy \"%s\" BO stalled and took %.03f ms.\n",
                    action, bo->name, elapsed * 1000);
      }
   }
}

static void
print_flags(unsigned flags)
{
   if (flags & MAP_READ)
      DBG("READ ");
   if (flags & MAP_WRITE)
      DBG("WRITE ");
   if (flags & MAP_ASYNC)
      DBG("ASYNC ");
   if (flags & MAP_PERSISTENT)
      DBG("PERSISTENT ");
   if (flags & MAP_COHERENT)
      DBG("COHERENT ");
   if (flags & MAP_RAW)
      DBG("RAW ");
   DBG("\n");
}

static void *
iris_bo_map_cpu(struct pipe_debug_callback *dbg,
                struct iris_bo *bo, unsigned flags)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   /* We disallow CPU maps for writing to non-coherent buffers, as the
    * CPU map can become invalidated when a batch is flushed out, which
    * can happen at unpredictable times.  You should use WC maps instead.
    */
   assert(bo->cache_coherent || !(flags & MAP_WRITE));

   if (!bo->map_cpu) {
      DBG("iris_bo_map_cpu: %d (%s)\n", bo->gem_handle, bo->name);

      struct drm_i915_gem_mmap mmap_arg = {
         .handle = bo->gem_handle,
         .size = bo->size,
      };
      int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg);
      if (ret != 0) {
         DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
         return NULL;
      }
      void *map = (void *) (uintptr_t) mmap_arg.addr_ptr;
      VG_DEFINED(map, bo->size);

      if (p_atomic_cmpxchg(&bo->map_cpu, NULL, map)) {
         VG_NOACCESS(map, bo->size);
         munmap(map, bo->size);
      }
   }
   assert(bo->map_cpu);

   DBG("iris_bo_map_cpu: %d (%s) -> %p, ", bo->gem_handle, bo->name,
       bo->map_cpu);
   print_flags(flags);

   if (!(flags & MAP_ASYNC)) {
      bo_wait_with_stall_warning(dbg, bo, "CPU mapping");
   }

   if (!bo->cache_coherent && !bo->bufmgr->has_llc) {
      /* If we're reusing an existing CPU mapping, the CPU caches may
       * contain stale data from the last time we read from that mapping.
       * (With the BO cache, it might even be data from a previous buffer!)
       * Even if it's a brand new mapping, the kernel may have zeroed the
       * buffer via CPU writes.
       *
       * We need to invalidate those cachelines so that we see the latest
       * contents, and so long as we only read from the CPU mmap we do not
       * need to write those cachelines back afterwards.
       *
       * On LLC, the emprical evidence suggests that writes from the GPU
       * that bypass the LLC (i.e. for scanout) do *invalidate* the CPU
       * cachelines. (Other reads, such as the display engine, bypass the
       * LLC entirely requiring us to keep dirty pixels for the scanout
       * out of any cache.)
       */
      gen_invalidate_range(bo->map_cpu, bo->size);
   }

   return bo->map_cpu;
}

static void *
iris_bo_map_wc(struct pipe_debug_callback *dbg,
               struct iris_bo *bo, unsigned flags)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   if (!bo->map_wc) {
      DBG("iris_bo_map_wc: %d (%s)\n", bo->gem_handle, bo->name);

      struct drm_i915_gem_mmap mmap_arg = {
         .handle = bo->gem_handle,
         .size = bo->size,
         .flags = I915_MMAP_WC,
      };
      int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg);
      if (ret != 0) {
         DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
         return NULL;
      }

      void *map = (void *) (uintptr_t) mmap_arg.addr_ptr;
      VG_DEFINED(map, bo->size);

      if (p_atomic_cmpxchg(&bo->map_wc, NULL, map)) {
         VG_NOACCESS(map, bo->size);
         munmap(map, bo->size);
      }
   }
   assert(bo->map_wc);

   DBG("iris_bo_map_wc: %d (%s) -> %p\n", bo->gem_handle, bo->name, bo->map_wc);
   print_flags(flags);

   if (!(flags & MAP_ASYNC)) {
      bo_wait_with_stall_warning(dbg, bo, "WC mapping");
   }

   return bo->map_wc;
}

/**
 * Perform an uncached mapping via the GTT.
 *
 * Write access through the GTT is not quite fully coherent. On low power
 * systems especially, like modern Atoms, we can observe reads from RAM before
 * the write via GTT has landed. A write memory barrier that flushes the Write
 * Combining Buffer (i.e. sfence/mfence) is not sufficient to order the later
 * read after the write as the GTT write suffers a small delay through the GTT
 * indirection. The kernel uses an uncached mmio read to ensure the GTT write
 * is ordered with reads (either by the GPU, WB or WC) and unconditionally
 * flushes prior to execbuf submission. However, if we are not informing the
 * kernel about our GTT writes, it will not flush before earlier access, such
 * as when using the cmdparser. Similarly, we need to be careful if we should
 * ever issue a CPU read immediately following a GTT write.
 *
 * Telling the kernel about write access also has one more important
 * side-effect. Upon receiving notification about the write, it cancels any
 * scanout buffering for FBC/PSR and friends. Later FBC/PSR is then flushed by
 * either SW_FINISH or DIRTYFB. The presumption is that we never write to the
 * actual scanout via a mmaping, only to a backbuffer and so all the FBC/PSR
 * tracking is handled on the buffer exchange instead.
 */
static void *
iris_bo_map_gtt(struct pipe_debug_callback *dbg,
                struct iris_bo *bo, unsigned flags)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   /* Get a mapping of the buffer if we haven't before. */
   if (bo->map_gtt == NULL) {
      DBG("bo_map_gtt: mmap %d (%s)\n", bo->gem_handle, bo->name);

      struct drm_i915_gem_mmap_gtt mmap_arg = { .handle = bo->gem_handle };

      /* Get the fake offset back... */
      int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg);
      if (ret != 0) {
         DBG("%s:%d: Error preparing buffer map %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
         return NULL;
      }

      /* and mmap it. */
      void *map = mmap(0, bo->size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, bufmgr->fd, mmap_arg.offset);
      if (map == MAP_FAILED) {
         DBG("%s:%d: Error mapping buffer %d (%s): %s .\n",
             __FILE__, __LINE__, bo->gem_handle, bo->name, strerror(errno));
         return NULL;
      }

      /* We don't need to use VALGRIND_MALLOCLIKE_BLOCK because Valgrind will
       * already intercept this mmap call. However, for consistency between
       * all the mmap paths, we mark the pointer as defined now and mark it
       * as inaccessible afterwards.
       */
      VG_DEFINED(map, bo->size);

      if (p_atomic_cmpxchg(&bo->map_gtt, NULL, map)) {
         VG_NOACCESS(map, bo->size);
         munmap(map, bo->size);
      }
   }
   assert(bo->map_gtt);

   DBG("bo_map_gtt: %d (%s) -> %p, ", bo->gem_handle, bo->name, bo->map_gtt);
   print_flags(flags);

   if (!(flags & MAP_ASYNC)) {
      bo_wait_with_stall_warning(dbg, bo, "GTT mapping");
   }

   return bo->map_gtt;
}

static bool
can_map_cpu(struct iris_bo *bo, unsigned flags)
{
   if (bo->cache_coherent)
      return true;

   /* Even if the buffer itself is not cache-coherent (such as a scanout), on
    * an LLC platform reads always are coherent (as they are performed via the
    * central system agent). It is just the writes that we need to take special
    * care to ensure that land in main memory and not stick in the CPU cache.
    */
   if (!(flags & MAP_WRITE) && bo->bufmgr->has_llc)
      return true;

   /* If PERSISTENT or COHERENT are set, the mmapping needs to remain valid
    * across batch flushes where the kernel will change cache domains of the
    * bo, invalidating continued access to the CPU mmap on non-LLC device.
    *
    * Similarly, ASYNC typically means that the buffer will be accessed via
    * both the CPU and the GPU simultaneously.  Batches may be executed that
    * use the BO even while it is mapped.  While OpenGL technically disallows
    * most drawing while non-persistent mappings are active, we may still use
    * the GPU for blits or other operations, causing batches to happen at
    * inconvenient times.
    *
    * If RAW is set, we expect the caller to be able to handle a WC buffer
    * more efficiently than the involuntary clflushes.
    */
   if (flags & (MAP_PERSISTENT | MAP_COHERENT | MAP_ASYNC | MAP_RAW))
      return false;

   return !(flags & MAP_WRITE);
}

void *
iris_bo_map(struct pipe_debug_callback *dbg,
            struct iris_bo *bo, unsigned flags)
{
   if (bo->tiling_mode != I915_TILING_NONE && !(flags & MAP_RAW))
      return iris_bo_map_gtt(dbg, bo, flags);

   void *map;

   if (can_map_cpu(bo, flags))
      map = iris_bo_map_cpu(dbg, bo, flags);
   else
      map = iris_bo_map_wc(dbg, bo, flags);

   /* Allow the attempt to fail by falling back to the GTT where necessary.
    *
    * Not every buffer can be mmaped directly using the CPU (or WC), for
    * example buffers that wrap stolen memory or are imported from other
    * devices. For those, we have little choice but to use a GTT mmapping.
    * However, if we use a slow GTT mmapping for reads where we expected fast
    * access, that order of magnitude difference in throughput will be clearly
    * expressed by angry users.
    *
    * We skip MAP_RAW because we want to avoid map_gtt's fence detiling.
    */
   if (!map && !(flags & MAP_RAW)) {
      perf_debug(dbg, "Fallback GTT mapping for %s with access flags %x\n",
                 bo->name, flags);
      map = iris_bo_map_gtt(dbg, bo, flags);
   }

   return map;
}

/** Waits for all GPU rendering with the object to have completed. */
void
iris_bo_wait_rendering(struct iris_bo *bo)
{
   /* We require a kernel recent enough for WAIT_IOCTL support.
    * See intel_init_bufmgr()
    */
   iris_bo_wait(bo, -1);
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
 * Similar to iris_bo_wait_rendering except a timeout parameter allows
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
iris_bo_wait(struct iris_bo *bo, int64_t timeout_ns)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   /* If we know it's idle, don't bother with the kernel round trip */
   if (bo->idle && !bo->external)
      return 0;

   struct drm_i915_gem_wait wait = {
      .bo_handle = bo->gem_handle,
      .timeout_ns = timeout_ns,
   };
   int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_WAIT, &wait);
   if (ret != 0)
      return -errno;

   bo->idle = true;

   return ret;
}

void
iris_bufmgr_destroy(struct iris_bufmgr *bufmgr)
{
   mtx_destroy(&bufmgr->lock);

   /* Free any cached buffer objects we were going to reuse */
   for (int i = 0; i < bufmgr->num_buckets; i++) {
      struct bo_cache_bucket *bucket = &bufmgr->cache_bucket[i];

      list_for_each_entry_safe(struct iris_bo, bo, &bucket->head, head) {
         list_del(&bo->head);

         bo_free(bo);
      }
   }

   _mesa_hash_table_destroy(bufmgr->name_table, NULL);
   _mesa_hash_table_destroy(bufmgr->handle_table, NULL);

   for (int z = 0; z < IRIS_MEMZONE_COUNT; z++) {
      if (z != IRIS_MEMZONE_BINDER)
         util_vma_heap_finish(&bufmgr->vma_allocator[z]);
   }

   free(bufmgr);
}

static int
bo_set_tiling_internal(struct iris_bo *bo, uint32_t tiling_mode,
                       uint32_t stride)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;
   struct drm_i915_gem_set_tiling set_tiling;
   int ret;

   if (bo->global_name == 0 &&
       tiling_mode == bo->tiling_mode && stride == bo->stride)
      return 0;

   memset(&set_tiling, 0, sizeof(set_tiling));
   do {
      /* set_tiling is slightly broken and overwrites the
       * input on the error path, so we have to open code
       * drm_ioctl.
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
iris_bo_get_tiling(struct iris_bo *bo, uint32_t *tiling_mode,
                  uint32_t *swizzle_mode)
{
   *tiling_mode = bo->tiling_mode;
   *swizzle_mode = bo->swizzle_mode;
   return 0;
}

struct iris_bo *
iris_bo_import_dmabuf(struct iris_bufmgr *bufmgr, int prime_fd)
{
   uint32_t handle;
   struct iris_bo *bo;

   mtx_lock(&bufmgr->lock);
   int ret = drmPrimeFDToHandle(bufmgr->fd, prime_fd, &handle);
   if (ret) {
      DBG("import_dmabuf: failed to obtain handle from fd: %s\n",
          strerror(errno));
      mtx_unlock(&bufmgr->lock);
      return NULL;
   }

   /*
    * See if the kernel has already returned this buffer to us. Just as
    * for named buffers, we must not create two bo's pointing at the same
    * kernel object
    */
   bo = hash_find_bo(bufmgr->handle_table, handle);
   if (bo) {
      iris_bo_reference(bo);
      goto out;
   }

   bo = bo_calloc();
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

   bo->bufmgr = bufmgr;

   bo->gem_handle = handle;
   _mesa_hash_table_insert(bufmgr->handle_table, &bo->gem_handle, bo);

   bo->name = "prime";
   bo->reusable = false;
   bo->external = true;
   bo->kflags = EXEC_OBJECT_SUPPORTS_48B_ADDRESS | EXEC_OBJECT_PINNED;
   bo->gtt_offset = vma_alloc(bufmgr, IRIS_MEMZONE_OTHER, bo->size, 1);

   struct drm_i915_gem_get_tiling get_tiling = { .handle = bo->gem_handle };
   if (drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling))
      goto err;

   bo->tiling_mode = get_tiling.tiling_mode;
   bo->swizzle_mode = get_tiling.swizzle_mode;
   /* XXX stride is unknown */

out:
   mtx_unlock(&bufmgr->lock);
   return bo;

err:
   bo_free(bo);
   mtx_unlock(&bufmgr->lock);
   return NULL;
}

static void
iris_bo_make_external_locked(struct iris_bo *bo)
{
   if (!bo->external) {
      _mesa_hash_table_insert(bo->bufmgr->handle_table, &bo->gem_handle, bo);
      bo->external = true;
   }
}

static void
iris_bo_make_external(struct iris_bo *bo)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   if (bo->external)
      return;

   mtx_lock(&bufmgr->lock);
   iris_bo_make_external_locked(bo);
   mtx_unlock(&bufmgr->lock);
}

int
iris_bo_export_dmabuf(struct iris_bo *bo, int *prime_fd)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   iris_bo_make_external(bo);

   if (drmPrimeHandleToFD(bufmgr->fd, bo->gem_handle,
                          DRM_CLOEXEC, prime_fd) != 0)
      return -errno;

   bo->reusable = false;

   return 0;
}

uint32_t
iris_bo_export_gem_handle(struct iris_bo *bo)
{
   iris_bo_make_external(bo);

   return bo->gem_handle;
}

int
iris_bo_flink(struct iris_bo *bo, uint32_t *name)
{
   struct iris_bufmgr *bufmgr = bo->bufmgr;

   if (!bo->global_name) {
      struct drm_gem_flink flink = { .handle = bo->gem_handle };

      if (drm_ioctl(bufmgr->fd, DRM_IOCTL_GEM_FLINK, &flink))
         return -errno;

      mtx_lock(&bufmgr->lock);
      if (!bo->global_name) {
         iris_bo_make_external_locked(bo);
         bo->global_name = flink.name;
         _mesa_hash_table_insert(bufmgr->name_table, &bo->global_name, bo);
      }
      mtx_unlock(&bufmgr->lock);

      bo->reusable = false;
   }

   *name = bo->global_name;
   return 0;
}

static void
add_bucket(struct iris_bufmgr *bufmgr, int size)
{
   unsigned int i = bufmgr->num_buckets;

   assert(i < ARRAY_SIZE(bufmgr->cache_bucket));

   list_inithead(&bufmgr->cache_bucket[i].head);
   bufmgr->cache_bucket[i].size = size;
   bufmgr->num_buckets++;

   assert(bucket_for_size(bufmgr, size) == &bufmgr->cache_bucket[i]);
   assert(bucket_for_size(bufmgr, size - 2048) == &bufmgr->cache_bucket[i]);
   assert(bucket_for_size(bufmgr, size + 1) != &bufmgr->cache_bucket[i]);
}

static void
init_cache_buckets(struct iris_bufmgr *bufmgr)
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
   add_bucket(bufmgr, PAGE_SIZE);
   add_bucket(bufmgr, PAGE_SIZE * 2);
   add_bucket(bufmgr, PAGE_SIZE * 3);

   /* Initialize the linked lists for BO reuse cache. */
   for (size = 4 * PAGE_SIZE; size <= cache_max_size; size *= 2) {
      add_bucket(bufmgr, size);

      add_bucket(bufmgr, size + size * 1 / 4);
      add_bucket(bufmgr, size + size * 2 / 4);
      add_bucket(bufmgr, size + size * 3 / 4);
   }
}

uint32_t
iris_create_hw_context(struct iris_bufmgr *bufmgr)
{
   struct drm_i915_gem_context_create create = { };
   int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);
   if (ret != 0) {
      DBG("DRM_IOCTL_I915_GEM_CONTEXT_CREATE failed: %s\n", strerror(errno));
      return 0;
   }

   return create.ctx_id;
}

int
iris_hw_context_set_priority(struct iris_bufmgr *bufmgr,
                            uint32_t ctx_id,
                            int priority)
{
   struct drm_i915_gem_context_param p = {
      .ctx_id = ctx_id,
      .param = I915_CONTEXT_PARAM_PRIORITY,
      .value = priority,
   };
   int err;

   err = 0;
   if (drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM, &p))
      err = -errno;

   return err;
}

void
iris_destroy_hw_context(struct iris_bufmgr *bufmgr, uint32_t ctx_id)
{
   struct drm_i915_gem_context_destroy d = { .ctx_id = ctx_id };

   if (ctx_id != 0 &&
       drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &d) != 0) {
      fprintf(stderr, "DRM_IOCTL_I915_GEM_CONTEXT_DESTROY failed: %s\n",
              strerror(errno));
   }
}

int
iris_reg_read(struct iris_bufmgr *bufmgr, uint32_t offset, uint64_t *result)
{
   struct drm_i915_reg_read reg_read = { .offset = offset };
   int ret = drm_ioctl(bufmgr->fd, DRM_IOCTL_I915_REG_READ, &reg_read);

   *result = reg_read.val;
   return ret;
}

static uint64_t
iris_gtt_size(int fd)
{
   /* We use the default (already allocated) context to determine
    * the default configuration of the virtual address space.
    */
   struct drm_i915_gem_context_param p = {
      .param = I915_CONTEXT_PARAM_GTT_SIZE,
   };
   if (!drm_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &p))
      return p.value;

   return 0;
}

/**
 * Initializes the GEM buffer manager, which uses the kernel to allocate, map,
 * and manage map buffer objections.
 *
 * \param fd File descriptor of the opened DRM device.
 */
struct iris_bufmgr *
iris_bufmgr_init(struct gen_device_info *devinfo, int fd)
{
   uint64_t gtt_size = iris_gtt_size(fd);
   if (gtt_size <= IRIS_MEMZONE_OTHER_START)
      return NULL;

   struct iris_bufmgr *bufmgr = calloc(1, sizeof(*bufmgr));
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

   if (mtx_init(&bufmgr->lock, mtx_plain) != 0) {
      free(bufmgr);
      return NULL;
   }

   bufmgr->has_llc = devinfo->has_llc;

   STATIC_ASSERT(IRIS_MEMZONE_SHADER_START == 0ull);
   const uint64_t _4GB = 1ull << 32;

   /* The STATE_BASE_ADDRESS size field can only hold 1 page shy of 4GB */
   const uint64_t _4GB_minus_1 = _4GB - PAGE_SIZE;

   util_vma_heap_init(&bufmgr->vma_allocator[IRIS_MEMZONE_SHADER],
                      PAGE_SIZE, _4GB_minus_1 - PAGE_SIZE);
   util_vma_heap_init(&bufmgr->vma_allocator[IRIS_MEMZONE_SURFACE],
                      IRIS_MEMZONE_SURFACE_START,
                      _4GB_minus_1 - IRIS_MAX_BINDERS * IRIS_BINDER_SIZE);
   util_vma_heap_init(&bufmgr->vma_allocator[IRIS_MEMZONE_DYNAMIC],
                      IRIS_MEMZONE_DYNAMIC_START + IRIS_BORDER_COLOR_POOL_SIZE,
                      _4GB_minus_1 - IRIS_BORDER_COLOR_POOL_SIZE);

   /* Leave the last 4GB out of the high vma range, so that no state
    * base address + size can overflow 48 bits.
    */
   util_vma_heap_init(&bufmgr->vma_allocator[IRIS_MEMZONE_OTHER],
                      IRIS_MEMZONE_OTHER_START,
                      (gtt_size - _4GB) - IRIS_MEMZONE_OTHER_START);

   // XXX: driconf
   bufmgr->bo_reuse = env_var_as_boolean("bo_reuse", true);

   init_cache_buckets(bufmgr);

   bufmgr->name_table =
      _mesa_hash_table_create(NULL, key_hash_uint, key_uint_equal);
   bufmgr->handle_table =
      _mesa_hash_table_create(NULL, key_hash_uint, key_uint_equal);

   return bufmgr;
}
