/*
 * Copyright © 2025 Collabora Ltd. and Igalia S.L.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "cache_ops.h"

#include "util/macros.h"
#include "util/u_atomic.h"

static uint32_t
get_ctr_el0(void)
{
   uint32_t ctr_el0;
   __asm("mrs\t%x0, ctr_el0" : "=r"(ctr_el0));
   return ctr_el0;
}

static uint32_t
get_ctr_cwg(void)
{
   return (get_ctr_el0() >> 24) & 0xf;
}

size_t
util_cache_granularity(void)
{
   static uint32_t cached_size = 0;
   uint32_t size = p_atomic_read(&cached_size);
   if (likely(size > 0))
      return size;

   /* We use CTR_EL0.CWG as the cache granularity.  According to Arm:
    *
    *    "CWG, [27:24]
    *
    *    Cache write-back granule. Log2 of the number of words of the maximum
    *    size of memory that can be overwritten as a result of the eviction of
    *    a cache entry that has had a memory location in it modified"
    *
    * On big.LITTLE CPUs, Linux will trap on fetching CTR_EL0 and take the
    * maximum across all CPU cores so this should really be the maximum that
    * drivers and clients can assume.
    */
   size = 4 << ((get_ctr_el0() >> 24) & 0xf);

   p_atomic_set(&cached_size, size);
   return size;
}

static size_t
get_dmin_line(void)
{
   static uint32_t cached_size = 0;
   uint32_t size = p_atomic_read(&cached_size);
   if (likely(size > 0))
      return size;

   /* For walking cache lines, we want to use CTR_EL0.DminLine as the step
    * size.  According to Arm:
    *
    *    "DminLine, [19:16]
    *
    *    Log2 of the number of words in the smallest cache line of all the
    *    data and unified caches that the core controls"
    *
    * On big.LITTLE CPUs, Linux will trap on fetching CTR_EL0 and take the
    * minimum across all CPU cores so this should be safe no matter what core
    * we happen to be living on.
    */
   size = 4 << ((get_ctr_el0() >> 16) & 0xf);

   p_atomic_set(&cached_size, size);
   return size;
}

static void
flush_l1_cacheline(UNUSED void *p)
{
   /* Clean data cache. */
   __asm volatile("dc cvac, %0" : : "r" (p) : "memory");
}

static void
flush_inval_l1_cacheline(UNUSED void *p)
{
   /* Clean and Invalidate data cache, there is no separate Invalidate. */
   __asm volatile("dc civac, %0" : : "r" (p) : "memory");
}

static void
data_sync_bar(void)
{
   __asm volatile("dsb sy");
}

void
util_flush_range_no_fence(void *start, size_t size)
{
   uintptr_t l1_cacheline_size = get_dmin_line();
   char *p = (char *) (((uintptr_t) start) & ~(l1_cacheline_size - 1));
   char *end = ((char *) start) + size;

   while (p < end) {
      flush_l1_cacheline(p);
      p += l1_cacheline_size;
   }
}

void
util_flush_inval_range_no_fence(void *start, size_t size)
{
   uintptr_t l1_cacheline_size = get_dmin_line();
   char *p = (char *) (((uintptr_t) start) & ~(l1_cacheline_size - 1));
   char *end = ((char *) start) + size;

   while (p < end) {
      flush_inval_l1_cacheline(p);
      p += l1_cacheline_size;
   }
}

void
util_flush_range(void *p, size_t size)
{
   if (size == 0)
      return;

   util_pre_flush_fence();
   util_flush_range_no_fence(p, size);
   util_post_flush_fence();
}

void
util_flush_inval_range(void *p, size_t size)
{
   if (size == 0)
      return;

   util_pre_flush_fence();
   util_flush_inval_range_no_fence(p, size);
   util_post_flush_inval_fence();
}

void
util_pre_flush_fence(void)
{
   /* From the Arm ® Architecture Reference Manual (revision L.b):
    *
    *    "All data cache instructions, other than DC ZVA, DC GVA, and DC GZVA
    *    that specify an address: [...] Execute in program order relative to
    *    other data cache instructions, other than DC ZVA, DC GVA, and DC GZVA
    *    that specify an address within the same cache line of minimum size,
    *    as indicated by CTR_EL0.DMinLine."
    *
    * So cache flush operations are properly ordered against memory accesses
    * and there's nothing we need to do to ensure that prior writes land
    * before the cache flush operations flush the data.
    *
    * In the case where this pre_flush_fence() is called before a flush/inval
    * used for a GPU -> CPU barrier, there is also nothing to do because it's
    * the responsibility of the GPU to ensure that all memory writes have
    * landed before we see this on the CPU side.
    */
}

void
util_post_flush_fence(void)
{
   /* From the Arm ® Architecture Reference Manual (revision L.b):
    *
    *    "A cache maintenance instruction can complete at any time after it is
    *    executed, but is only guaranteed to be complete, and its effects
    *    visible to other observers, following a DSB instruction executed by
    *    the PE that executed the cache maintenance instruction."
    *
    * In order to ensure that the GPU sees data flushed by pror cache flushes,
    * we need to execute a DSB to ensure the flushes land.
    */
   data_sync_bar();
}

void
util_post_flush_inval_fence(void)
{
   /* From the Arm ® Architecture Reference Manual (revision L.b):
    *
    *    "All data cache instructions, other than DC ZVA, DC GVA, and DC GZVA
    *    that specify an address: [...] Execute in program order relative to
    *    other data cache instructions, other than DC ZVA, DC GVA, and DC GZVA
    *    that specify an address within the same cache line of minimum size,
    *    as indicated by CTR_EL0.DMinLine."
    *
    * This seems to imply that memory access that happens after the cache
    * flush/invalidate operation would be properly ordered with respect to it.
    * However, the manual also says:
    *
    *    "A cache maintenance instruction can complete at any time after it is
    *    executed, but is only guaranteed to be complete, and its effects
    *    visible to other observers, following a DSB instruction executed by
    *    the PE that executed the cache maintenance instruction."
    *
    * In practice, it appears that the ordering guarantees only really apply
    * to the queue order in the data cache and not the order in which
    * operations complete.  In other words, a read which is queued after the
    * invalidate may still use the stale cache line unless we explicitly
    * insert a DSB between them.
    */
   data_sync_bar();
}
