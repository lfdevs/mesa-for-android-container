/*
 * Copyright (c) 2020 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

/* Index buffer min/max cache. We need to calculate the min/max for arbitrary
 * slices (start, start + count) of the index buffer at drawtime. As this can
 * be quite expensive, we cache. Conceptually, we just use a hash table mapping
 * the key (start, count) to the value (min, max). In practice, mesa's hash
 * table implementation is higher overhead than we would like and makes
 * handling memory usage a little complicated. So we use this data structure
 * instead. Searching is O(n) to the size, but the size is capped at the
 * PANFROST_MINMAX_SIZE constant (so this is a tradeoff between cache hit/miss
 * ratio and cache search speed). Note that keys are adjacent so we get cache
 * line alignment benefits. Insertion is O(1) and in-order until the cache
 * fills up, after that it evicts the oldest cached value in a ring facilitated
 * by index.
 */

#include "pan_minmax_cache.h"
#include "util/macros.h"

/*
 * note: a count of 0 would be an empty range, which we don't have to
 * cache; so returning a 0 to indicate "do not cache" is sensible
 * otherwise create a key that encodes the start, count, and index size
 */
static uint64_t
pan_calc_cache_key(struct pan_minmax_cache *cache, unsigned index_size,
                   unsigned start, unsigned count)
{
   uint64_t ht_key;

   if (!cache)
      return 0; /* do not cache if no cache! */

   /* we're going to store the item size in the upper bits of the count;
    * if the count is too big to do this safely, bail and do not use
    * the cache (this case is going to be horrible no matter what we do,
    * and is highly unlikely)
    */
   if (count > 0x3FFFFFFF)
      return 0;  /* do not cache */

   /* find log2(index_size) or die tryin' */
   switch (index_size) {
   case 1:
      index_size = 0;
      break;
   case 2:
      index_size = 1;
      break;
   case 4:
      index_size = 2;
      break;
   default:
      UNREACHABLE("unknown index size");
   }
   count = count | (index_size << 30);
   ht_key = ((uint64_t)count << 32) | start;
   return ht_key;
}

bool
pan_minmax_cache_get(struct pan_minmax_cache *cache, unsigned index_size,
                     unsigned start, unsigned count, unsigned *min_index,
                     unsigned *max_index)
{
   uint64_t ht_key = pan_calc_cache_key(cache, index_size, start, count);
   bool found = false;

   if (!ht_key)
      return false;


   for (unsigned i = 0; i < cache->size; ++i) {
      if (cache->keys[i] == ht_key) {
         uint64_t hit = cache->values[i];

         *min_index = hit & 0xffffffff;
         *max_index = hit >> 32;
         found = true;
         break;
      }
   }

   return found;
}

void
pan_minmax_cache_add(struct pan_minmax_cache *cache, unsigned index_size,
                     unsigned start, unsigned count, unsigned min_index,
                     unsigned max_index)
{
   uint64_t ht_key = pan_calc_cache_key(cache, index_size, start, count);
   uint64_t value = min_index | (((uint64_t)max_index) << 32);
   unsigned index = 0;

   if (!ht_key)
      return;

   if (cache->size == PANFROST_MINMAX_SIZE) {
      index = cache->index++;
      cache->index = cache->index % PANFROST_MINMAX_SIZE;
   } else {
      index = cache->size++;
   }

   cache->keys[index] = ht_key;
   cache->values[index] = value;
}

/* If we've been caching min/max indices and we update the index
 * buffer, that may invalidate the min/max. Check what's been cached vs
 * what we've written, and throw out invalid entries. */

void
pan_minmax_cache_invalidate(struct pan_minmax_cache *cache, unsigned index_size,
                            size_t offset, size_t size)
{
   /* Ensure there is a cache to invalidate and a write */
   if (!cache)
      return;

   /* convert offset and size to bytes, so that if we
      update a region using a different item size we
      still invalidate it */
   offset *= index_size;
   size *= index_size;
   unsigned valid_count = 0;

   for (unsigned i = 0; i < cache->size; ++i) {
      uint64_t key = cache->keys[i];

      /* the item size is in the upper 2 bits of the key
       * as above, convert size and count to bytes to make
       * region comparison agnostic to item size
       */
      uint32_t key_index_size = (key >> 62);
      size_t count = ((key >> 32) & 0x3fffffff) << key_index_size;
      size_t start = (key & 0xffffffff) << key_index_size;

      /* 1D range intersection */
      bool invalid = MAX2(offset, start) <
                     MIN2(offset + size, start + count);
      if (!invalid) {
         cache->keys[valid_count] = key;
         cache->values[valid_count] = cache->values[i];
         valid_count++;
      }
   }

   cache->size = valid_count;
   cache->index = 0;
}
