/*
 * Copyright © 2018 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"
#include "panvk_mempool.h"
#include "panvk_priv_bo.h"

#include "kmod/pan_kmod.h"

#include "util/log.h"
#include "util/os_time.h"

#include <string.h>

void
panvk_bo_pool_cleanup(struct panvk_bo_pool *bo_pool)
{
   list_for_each_entry_safe(struct panvk_priv_bo, bo, &bo_pool->free_bos,
                            node) {
      list_del(&bo->node);
      panvk_priv_bo_unref(bo);
   }
}

void
panvk_slab_cache_init(struct panvk_slab_cache *cache, uint64_t window_ns,
                      uint32_t evict_windows)
{
   memset(cache, 0, sizeof(*cache));
   simple_mtx_init(&cache->lock, mtx_plain);
   list_inithead(&cache->bos);
   list_inithead(&cache->evict_bos);
   cache->window_ns = window_ns;
   cache->window_start_ns = os_time_get_nano();
#ifdef HAVE_PAN_KMOD_KBASE
   cache->evict_windows = evict_windows;
#endif
}

void
panvk_slab_cache_finish(struct panvk_slab_cache *cache)
{
   list_for_each_entry_safe(struct panvk_priv_bo, bo, &cache->bos, node) {
      list_del(&bo->node);
      p_atomic_dec(&panvk_mem_stats.slab_cache_count);
      p_atomic_add(&panvk_mem_stats.slab_cache_B,
                   -(int64_t)pan_kmod_bo_size(bo->bo));
      panvk_priv_bo_unref(bo);
   }
   /* kbase frees evictable regions like any other. */
   list_for_each_entry_safe(struct panvk_priv_bo, bo, &cache->evict_bos,
                            node) {
      list_del(&bo->node);
      p_atomic_dec(&panvk_mem_stats.slab_evict_count);
      p_atomic_add(&panvk_mem_stats.slab_evict_B,
                   -(int64_t)pan_kmod_bo_size(bo->bo));
      panvk_priv_bo_unref(bo);
   }
   simple_mtx_destroy(&cache->lock);
}

static struct panvk_priv_bo *
panvk_slab_cache_take(struct panvk_slab_cache *cache)
{
   struct panvk_priv_bo *bo = NULL;
   bool evictable = false;

   simple_mtx_lock(&cache->lock);
   if (cache->count) {
      bo = list_last_entry(&cache->bos, struct panvk_priv_bo, node);
      list_del(&bo->node);
      cache->count--;
      cache->low = MIN2(cache->low, cache->count);
   } else if (cache->evict_count) {
      bo = list_last_entry(&cache->evict_bos, struct panvk_priv_bo, node);
      list_del(&bo->node);
      cache->evict_count--;
      evictable = true;
   }
   simple_mtx_unlock(&cache->lock);

   if (!bo)
      return NULL;

   const int64_t size = pan_kmod_bo_size(bo->bo);

   if (evictable) {
      p_atomic_dec(&panvk_mem_stats.slab_evict_count);
      p_atomic_add(&panvk_mem_stats.slab_evict_B, -size);
#ifdef HAVE_PAN_KMOD_KBASE
      /* Needed again and fully backed, or freed. */
      if (kbase_kmod_bo_set_evictable(bo->bo, false)) {
         p_atomic_inc(&panvk_mem_stats.slab_evict_lost);
         panvk_priv_bo_unref(bo);
         return NULL;
      }
#endif
      p_atomic_inc(&panvk_mem_stats.slab_evict_reused);
   } else {
      p_atomic_dec(&panvk_mem_stats.slab_cache_count);
      p_atomic_add(&panvk_mem_stats.slab_cache_B, -size);
   }

   p_atomic_inc(&panvk_mem_stats.slab_cache_hits);
   return bo;
}

#ifdef HAVE_PAN_KMOD_KBASE
/* Marks the slabs on drop evictable and moves them to the cache's evict
 * list, outside the cache lock (FLAGS_CHANGE takes the mm write lock). The
 * first refusal turns marking off for the cache. Returns the slabs moved.
 */
static unsigned
panvk_slab_cache_mark(struct panvk_slab_cache *cache, struct list_head *drop,
                      uint32_t window)
{
   struct list_head marked;
   unsigned n = 0;
   uint64_t n_B = 0;
   int ret = 0;

   list_inithead(&marked);
   list_for_each_entry_safe(struct panvk_priv_bo, bo, drop, node) {
      ret = kbase_kmod_bo_set_evictable(bo->bo, true);
      if (ret)
         break;

      list_del(&bo->node);
      list_addtail(&bo->node, &marked);
      bo->cache_window = window;
      n_B += pan_kmod_bo_size(bo->bo);
      n++;
   }

   simple_mtx_lock(&cache->lock);
   list_splicetail(&marked, &cache->evict_bos);
   cache->evict_count += n;
   if (ret)
      cache->evict_windows = 0;
   simple_mtx_unlock(&cache->lock);

   if (ret) {
      mesa_logw("panvk: BASE_MEM_DONT_NEED refused (%s), command buffer "
                "slabs are freed instead",
                strerror(-ret));
   }

   p_atomic_add(&panvk_mem_stats.slab_evict_count, n);
   p_atomic_add(&panvk_mem_stats.slab_evict_B, n_B);
   return n;
}
#endif

void
panvk_bo_pool_give_to_cache(struct panvk_bo_pool *bo_pool, unsigned keep)
{
   struct panvk_slab_cache *cache = bo_pool->cache;
   struct list_head give, drop, expired;
   unsigned have = list_length(&bo_pool->free_bos), given = 0, dropped = 0;
   uint64_t given_B = 0, dropped_B = 0;
   uint32_t window = 0, evict_windows = 0;

   list_inithead(&give);
   list_inithead(&drop);
   list_inithead(&expired);
   for (; have > keep; have--, given++) {
      struct panvk_priv_bo *bo =
         list_first_entry(&bo_pool->free_bos, struct panvk_priv_bo, node);
      list_del(&bo->node);
      list_addtail(&bo->node, &give);
      given_B += pan_kmod_bo_size(bo->bo);
   }

   const uint64_t now = os_time_get_nano();

   simple_mtx_lock(&cache->lock);
   list_splicetail(&give, &cache->bos);
   cache->count += given;
   cache->high = MAX2(cache->high, cache->count);

   if (now - cache->window_start_ns >= cache->window_ns) {
      /* With takes from the tail, the low oldest slabs were not used in
       * this window. */
      const uint32_t margin = MAX2((cache->high - cache->low) / 4, 2);
      uint32_t idle = MIN2(cache->low, cache->count);

      for (; idle > margin; idle--, dropped++) {
         struct panvk_priv_bo *bo =
            list_first_entry(&cache->bos, struct panvk_priv_bo, node);
         list_del(&bo->node);
         list_addtail(&bo->node, &drop);
         dropped_B += pan_kmod_bo_size(bo->bo);
      }

      cache->count -= dropped;
      cache->low = cache->high = cache->count;
      cache->window_start_ns = now;
      window = ++cache->window_seq;
      evict_windows = cache->evict_windows;

      /* Evictable slabs nobody took in evict_windows windows; the list is
       * in marking order. */
      list_for_each_entry_safe(struct panvk_priv_bo, bo, &cache->evict_bos,
                               node) {
         if (window - bo->cache_window < cache->evict_windows)
            break;

         list_del(&bo->node);
         list_addtail(&bo->node, &expired);
         cache->evict_count--;
      }
   }
   simple_mtx_unlock(&cache->lock);

   p_atomic_add(&panvk_mem_stats.slab_cache_count,
                (int64_t)given - (int64_t)dropped);
   p_atomic_add(&panvk_mem_stats.slab_cache_B,
                (int64_t)given_B - (int64_t)dropped_B);

#ifdef HAVE_PAN_KMOD_KBASE
   if (evict_windows && dropped)
      panvk_slab_cache_mark(cache, &drop, window);
#endif

   list_for_each_entry_safe(struct panvk_priv_bo, bo, &expired, node) {
      p_atomic_dec(&panvk_mem_stats.slab_evict_count);
      p_atomic_add(&panvk_mem_stats.slab_evict_B,
                   -(int64_t)pan_kmod_bo_size(bo->bo));
      list_del(&bo->node);
      list_addtail(&bo->node, &drop);
   }

   list_for_each_entry_safe(struct panvk_priv_bo, bo, &drop, node) {
      p_atomic_inc(&panvk_mem_stats.slab_cache_freed_bos);
      p_atomic_add(&panvk_mem_stats.slab_cache_freed_B,
                   pan_kmod_bo_size(bo->bo));
      list_del(&bo->node);
      panvk_priv_bo_unref(bo);
   }
}

/* Knockoff u_upload_mgr. Uploads wherever we left off, allocating new entries
 * when needed.
 *
 * In "owned" mode, a single parent owns the entire pool, and the pool owns all
 * created BOs. All BOs are tracked and addable as
 * panvk_pool_get_bo_handles. Freeing occurs at the level of an entire pool.
 * This is useful for streaming uploads, where the batch owns the pool.
 *
 * In "unowned" mode, the pool is freestanding. It does not track created BOs
 * or hold references. Instead, the consumer must manage the created BOs. This
 * is more flexible, enabling non-transient CSO state or shader code to be
 * packed with conservative lifetime handling.
 */

static struct panvk_priv_bo *
panvk_pool_alloc_backing(struct panvk_pool *pool, size_t sz)
{
   size_t bo_sz = ALIGN_POT(MAX2(pool->base.slab_size, sz),
                            panvk_get_gpu_page_size(pool->dev));
   struct panvk_priv_bo *bo = NULL;

   /* If there's a free BO in our BO pool, let's pick it. */
   if (pool->bo_pool && bo_sz == pool->base.slab_size &&
       !list_is_empty(&pool->bo_pool->free_bos)) {
      bo =
         list_first_entry(&pool->bo_pool->free_bos, struct panvk_priv_bo, node);
      list_del(&bo->node);
   } else {
      /* Else an idle slab of the same kind from the device cache. */
      if (pool->bo_pool && pool->bo_pool->cache &&
          bo_sz == pool->base.slab_size)
         bo = panvk_slab_cache_take(pool->bo_pool->cache);

      if (bo == NULL && pool->big_bo_pool) {
         list_for_each_entry_safe(struct panvk_priv_bo, pooled_bo,
                                  &pool->big_bo_pool->free_bos, node) {
            const size_t picked_bo_sz = pan_kmod_bo_size(pooled_bo->bo);
            /* check if we have any sufficient match */
            if (picked_bo_sz >= bo_sz) {
               bo = pooled_bo;
               list_del(&bo->node);
               break;
            }
         }
      }

      if (bo == NULL) {
         /* We don't know what the BO will be used for, so let's flag it
          * RW and attach it to both the fragment and vertex/tiler jobs.
          * TODO: if we want fine grained BO assignment we should pass
          * flags to this function and keep the read/write,
          * fragment/vertex+tiler pools separate.
          */
         VkResult result =
            panvk_priv_bo_create_pool(pool, bo_sz, pool->props.create_flags,
                                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &bo);

         /* Pool allocations are indirect, meaning there's no VkResult returned
          * and no way for the caller to know why the device memory allocation
          * failed. We want to propagate host allocation failures, so set
          * errno to -ENOMEM if panvk_priv_bo_create() returns
          * VK_ERROR_OUT_OF_HOST_MEMORY.
          * We expect the caller to check the returned pointer and catch the
          * host allocation failure with a call to panvk_error(). */
         if (result == VK_ERROR_OUT_OF_HOST_MEMORY) {
            assert(bo == NULL);
            errno = -ENOMEM;
         }
      }
   }

   if (bo == NULL)
      return NULL;

   if (pool->props.owns_bos) {
      if (pan_kmod_bo_size(bo->bo) == pool->base.slab_size)
         list_addtail(&bo->node, &pool->bos);
      else
         list_addtail(&bo->node, &pool->big_bos);
      pool->bo_count++;
   }

   size_t new_remaining_size = pan_kmod_bo_size(bo->bo) - sz;
   size_t prev_remaining_size =
      pool->transient_bo
         ? pan_kmod_bo_size(pool->transient_bo->bo) - pool->transient_offset
         : 0;

   /* If there's less room in the new BO after the allocation, we stick to the
    * previous one. We also don't hold on BOs that are bigger than the pool
    * allocation granularity, to avoid memory fragmentation (retaining a big
    * BO which has just one tiny allocation active is not great). */
   if (prev_remaining_size < new_remaining_size &&
       (pool->props.owns_bos || bo_sz <= pool->base.slab_size)) {
      if (!pool->props.owns_bos)
         panvk_priv_bo_unref(pool->transient_bo);

      pool->transient_bo = bo;
      pool->transient_offset = 0;
   }

   return bo;
}

struct panvk_priv_mem
panvk_pool_alloc_mem(struct panvk_pool *pool, struct panvk_pool_alloc_info info)
{
   struct panvk_physical_device *pdev =
      to_panvk_physical_device(pool->dev->vk.physical);

   /* Make sure objects allocated from shared BOs are aligned on a cacheline. */
   if (!pool->props.owns_bos &&
       (pool->props.create_flags & PAN_KMOD_BO_FLAG_WB_MMAP)) {
      info.alignment =
         MAX2(pdev->vk.properties.nonCoherentAtomSize, info.alignment);
   }

   assert(info.alignment == util_next_power_of_two(info.alignment));

   if (pool->props.needs_locking)
      simple_mtx_lock(&pool->lock);

   /* Find or create a suitable BO */
   struct panvk_priv_bo *bo = pool->transient_bo;
   unsigned offset = ALIGN_POT(pool->transient_offset, info.alignment);

   /* If we don't fit, allocate a new backing */
   if (unlikely(bo == NULL || offset + info.size > pan_kmod_bo_size(bo->bo))) {
      bo = panvk_pool_alloc_backing(pool, info.size);
      offset = 0;
   }

   if (bo != NULL && pool->transient_bo == bo) {
      pool->transient_offset = offset + info.size;
      if (!pool->props.owns_bos)
         panvk_priv_bo_ref(bo);
   }

   uint32_t flags = 0;

   if (pool->props.owns_bos)
      flags |= PANVK_PRIV_MEM_OWNED_BY_POOL;

   assert(!((uintptr_t)bo & 7));
   assert(!(flags & ~7));

   struct panvk_priv_mem ret = {
      .bo = (uintptr_t)bo | flags,
      .offset = offset,
   };

   if (pool->props.needs_locking)
      simple_mtx_unlock(&pool->lock);

   return ret;
}

static struct pan_ptr
panvk_pool_alloc_aligned(struct panvk_pool *pool, size_t sz, unsigned alignment)
{
   /* We just return the host/dev address, so callers can't
    * release the BO ref they acquired. */
   assert(pool->props.owns_bos);

   struct panvk_pool_alloc_info info = {
      .size = sz,
      .alignment = alignment,
   };
   struct panvk_priv_mem mem = panvk_pool_alloc_mem(pool, info);

   return (struct pan_ptr){
      .cpu = panvk_priv_mem_host_addr(mem),
      .gpu = panvk_priv_mem_dev_addr(mem),
   };
}
PAN_POOL_ALLOCATOR(struct panvk_pool, panvk_pool_alloc_aligned)

void
panvk_pool_init(struct panvk_pool *pool, struct panvk_device *dev,
                struct panvk_bo_pool *bo_pool,
                struct panvk_bo_pool *big_bo_pool,
                const struct panvk_pool_properties *props)
{
   memset(pool, 0, sizeof(*pool));
   pool->props = *props;
   simple_mtx_init(&pool->lock, mtx_plain);
   pan_pool_init(&pool->base, pool->props.slab_size);
   pool->dev = dev;
   pool->bo_pool = bo_pool;
   pool->big_bo_pool = big_bo_pool;

   list_inithead(&pool->bos);
   list_inithead(&pool->big_bos);

   if (props->prealloc)
      panvk_pool_alloc_backing(pool, pool->base.slab_size);
}

void
panvk_pool_reset(struct panvk_pool *pool)
{
   if (pool->bo_pool) {
      list_splicetail(&pool->bos, &pool->bo_pool->free_bos);
      list_inithead(&pool->bos);
   } else {
      list_for_each_entry_safe(struct panvk_priv_bo, bo, &pool->bos, node) {
         list_del(&bo->node);
         panvk_priv_bo_unref(bo);
      }
   }

   if (pool->big_bo_pool) {
      list_splicetail(&pool->big_bos, &pool->big_bo_pool->free_bos);
      list_inithead(&pool->big_bos);
   } else {
      list_for_each_entry_safe(struct panvk_priv_bo, bo, &pool->big_bos, node) {
         list_del(&bo->node);
         panvk_priv_bo_unref(bo);
      }
   }

   if (!pool->props.owns_bos)
      panvk_priv_bo_unref(pool->transient_bo);

   pool->bo_count = 0;
   pool->transient_bo = NULL;
}

void
panvk_pool_cleanup(struct panvk_pool *pool)
{
   panvk_pool_reset(pool);
}

void
panvk_pool_get_bo_handles(struct panvk_pool *pool, uint32_t *handles)
{
   unsigned idx = 0;

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->bos, node)
      handles[idx++] = pan_kmod_bo_handle(bo->bo);

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->big_bos, node)
      handles[idx++] = pan_kmod_bo_handle(bo->bo);
}

void
panvk_pool_get_bos(struct panvk_pool *pool, struct pan_kmod_bo **bos)
{
   unsigned idx = 0;

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->bos, node)
      bos[idx++] = bo->bo;

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->big_bos, node)
      bos[idx++] = bo->bo;
}

void
panvk_pool_flush_maps(struct panvk_pool *pool)
{
   /* Don't walk the buffers if we know the synchronization is not needed. */
   if (!(pool->props.create_flags & PAN_KMOD_BO_FLAG_WB_MMAP))
      return;

   /* csf-v11 221: the transient BO is used only up to transient_offset,
    * so its unused tail is not flushed. Other BOs have no used-size record
    * and are flushed whole. PANVK_POOL_FLUSH_FULL=1 flushes everything.
    */
   static int full = -1;
   if (full < 0) {
      const char *e = getenv("PANVK_POOL_FLUSH_FULL");
      full = e && e[0] == '1';
   }

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->bos, node) {
      size_t size = bo->bo->size;
      if (!full && bo == pool->transient_bo)
         size = MIN2(ALIGN_POT((size_t)pool->transient_offset, 64), size);
      if (size)
         panvk_priv_bo_flush(bo, 0, size);
   }

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->big_bos, node) {
      size_t size = bo->bo->size;
      if (!full && bo == pool->transient_bo)
         size = MIN2(ALIGN_POT((size_t)pool->transient_offset, 64), size);
      if (size)
         panvk_priv_bo_flush(bo, 0, size);
   }
}

void
panvk_pool_invalidate_maps(struct panvk_pool *pool)
{
   /* Don't walk the buffers if we know the synchronization is not needed. */
   if (!(pool->props.create_flags & PAN_KMOD_BO_FLAG_WB_MMAP))
      return;

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->bos, node)
      panvk_priv_bo_invalidate(bo, 0, bo->bo->size);

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->big_bos, node)
      panvk_priv_bo_invalidate(bo, 0, bo->bo->size);
}
