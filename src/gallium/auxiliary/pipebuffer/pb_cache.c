/**************************************************************************
 *
 * Copyright 2007-2008 VMware, Inc.
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "pb_cache.h"
#include "util/u_memory.h"
#include "util/u_time.h"


/**
 * Actually destroy the buffer.
 */
static void
destroy_buffer_locked(struct pb_cache_entry *entry)
{
   struct pb_cache *mgr = entry->mgr;
   struct pb_buffer *buf = entry->buffer;

   assert(!pipe_is_referenced(&buf->reference));
   if (entry->head.next) {
      LIST_DEL(&entry->head);
      assert(mgr->num_buffers);
      --mgr->num_buffers;
      mgr->cache_size -= buf->size;
   }
   mgr->destroy_buffer(buf);
}

/**
 * Free as many cache buffers from the list head as possible.
 */
static void
release_expired_buffers_locked(struct list_head *cache)
{
   struct list_head *curr, *next;
   struct pb_cache_entry *entry;
   int64_t now;

   now = os_time_get();

   curr = cache->next;
   next = curr->next;
   while (curr != cache) {
      entry = LIST_ENTRY(struct pb_cache_entry, curr, head);

      if (!os_time_timeout(entry->start, entry->end, now))
         break;

      destroy_buffer_locked(entry);

      curr = next;
      next = curr->next;
   }
}

/**
 * Add a buffer to the cache. This is typically done when the buffer is
 * being released.
 */
void
pb_cache_add_buffer(struct pb_cache_entry *entry)
{
   struct pb_cache *mgr = entry->mgr;
   struct list_head *cache = &mgr->buckets[entry->bucket_index];
   struct pb_buffer *buf = entry->buffer;
   unsigned i;

   pipe_mutex_lock(mgr->mutex);
   assert(!pipe_is_referenced(&buf->reference));

   for (i = 0; i < ARRAY_SIZE(mgr->buckets); i++)
      release_expired_buffers_locked(&mgr->buckets[i]);

   /* Directly release any buffer that exceeds the limit. */
   if (mgr->cache_size + buf->size > mgr->max_cache_size) {
      mgr->destroy_buffer(buf);
      pipe_mutex_unlock(mgr->mutex);
      return;
   }

   entry->start = os_time_get();
   entry->end = entry->start + mgr->usecs;
   LIST_ADDTAIL(&entry->head, cache);
   ++mgr->num_buffers;
   mgr->cache_size += buf->size;
   pipe_mutex_unlock(mgr->mutex);
}

/**
 * \return 1   if compatible and can be reclaimed
 *         0   if incompatible
 *        -1   if compatible and can't be reclaimed
 */
static int
pb_cache_is_buffer_compat(struct pb_cache_entry *entry,
                          pb_size size, unsigned alignment, unsigned usage)
{
   struct pb_cache *mgr = entry->mgr;
   struct pb_buffer *buf = entry->buffer;

   if (!pb_check_usage(usage, buf->usage))
      return 0;

   /* be lenient with size */
   if (buf->size < size ||
       buf->size > (unsigned) (mgr->size_factor * size))
      return 0;

   if (usage & mgr->bypass_usage)
      return 0;

   if (!pb_check_alignment(alignment, buf->alignment))
      return 0;

   return mgr->can_reclaim(buf) ? 1 : -1;
}

/**
 * Find a compatible buffer in the cache, return it, and remove it
 * from the cache.
 */
struct pb_buffer *
pb_cache_reclaim_buffer(struct pb_cache *mgr, pb_size size,
                        unsigned alignment, unsigned usage,
                        unsigned bucket_index)
{
   struct pb_cache_entry *entry;
   struct pb_cache_entry *cur_entry;
   struct list_head *cur, *next;
   int64_t now;
   int ret = 0;
   struct list_head *cache = &mgr->buckets[bucket_index];

   pipe_mutex_lock(mgr->mutex);

   entry = NULL;
   cur = cache->next;
   next = cur->next;

   /* search in the expired buffers, freeing them in the process */
   now = os_time_get();
   while (cur != cache) {
      cur_entry = LIST_ENTRY(struct pb_cache_entry, cur, head);

      if (!entry && (ret = pb_cache_is_buffer_compat(cur_entry, size,
                                                     alignment, usage) > 0))
         entry = cur_entry;
      else if (os_time_timeout(cur_entry->start, cur_entry->end, now))
         destroy_buffer_locked(cur_entry);
      else
         /* This buffer (and all hereafter) are still hot in cache */
         break;

      /* the buffer is busy (and probably all remaining ones too) */
      if (ret == -1)
         break;

      cur = next;
      next = cur->next;
   }

   /* keep searching in the hot buffers */
   if (!entry && ret != -1) {
      while (cur != cache) {
         cur_entry = LIST_ENTRY(struct pb_cache_entry, cur, head);
         ret = pb_cache_is_buffer_compat(cur_entry, size, alignment, usage);

         if (ret > 0) {
            entry = cur_entry;
            break;
         }
         if (ret == -1)
            break;
         /* no need to check the timeout here */
         cur = next;
         next = cur->next;
      }
   }

   /* found a compatible buffer, return it */
   if (entry) {
      struct pb_buffer *buf = entry->buffer;

      mgr->cache_size -= buf->size;
      LIST_DEL(&entry->head);
      --mgr->num_buffers;
      pipe_mutex_unlock(mgr->mutex);
      /* Increase refcount */
      pipe_reference_init(&buf->reference, 1);
      return buf;
   }

   pipe_mutex_unlock(mgr->mutex);
   return NULL;
}

/**
 * Empty the cache. Useful when there is not enough memory.
 */
void
pb_cache_release_all_buffers(struct pb_cache *mgr)
{
   struct list_head *curr, *next;
   struct pb_cache_entry *buf;
   unsigned i;

   pipe_mutex_lock(mgr->mutex);
   for (i = 0; i < ARRAY_SIZE(mgr->buckets); i++) {
      struct list_head *cache = &mgr->buckets[i];

      curr = cache->next;
      next = curr->next;
      while (curr != cache) {
         buf = LIST_ENTRY(struct pb_cache_entry, curr, head);
         destroy_buffer_locked(buf);
         curr = next;
         next = curr->next;
      }
   }
   pipe_mutex_unlock(mgr->mutex);
}

void
pb_cache_init_entry(struct pb_cache *mgr, struct pb_cache_entry *entry,
                    struct pb_buffer *buf, unsigned bucket_index)
{
   memset(entry, 0, sizeof(*entry));
   entry->buffer = buf;
   entry->mgr = mgr;
   entry->bucket_index = bucket_index;
}

/**
 * Initialize a caching buffer manager.
 *
 * @param mgr     The cache buffer manager
 * @param usecs   Unused buffers may be released from the cache after this
 *                time
 * @param size_factor  Declare buffers that are size_factor times bigger than
 *                     the requested size as cache hits.
 * @param bypass_usage  Bitmask. If (requested usage & bypass_usage) != 0,
 *                      buffer allocation requests are rejected.
 * @param maximum_cache_size  Maximum size of all unused buffers the cache can
 *                            hold.
 * @param destroy_buffer  Function that destroys a buffer for good.
 * @param can_reclaim     Whether a buffer can be reclaimed (e.g. is not busy)
 */
void
pb_cache_init(struct pb_cache *mgr, uint usecs, float size_factor,
              unsigned bypass_usage, uint64_t maximum_cache_size,
              void (*destroy_buffer)(struct pb_buffer *buf),
              bool (*can_reclaim)(struct pb_buffer *buf))
{
   unsigned i;

   for (i = 0; i < ARRAY_SIZE(mgr->buckets); i++)
      LIST_INITHEAD(&mgr->buckets[i]);

   pipe_mutex_init(mgr->mutex);
   mgr->cache_size = 0;
   mgr->max_cache_size = maximum_cache_size;
   mgr->usecs = usecs;
   mgr->num_buffers = 0;
   mgr->bypass_usage = bypass_usage;
   mgr->size_factor = size_factor;
   mgr->destroy_buffer = destroy_buffer;
   mgr->can_reclaim = can_reclaim;
}

/**
 * Deinitialize the manager completely.
 */
void
pb_cache_deinit(struct pb_cache *mgr)
{
   pb_cache_release_all_buffers(mgr);
   pipe_mutex_destroy(mgr->mutex);
}
