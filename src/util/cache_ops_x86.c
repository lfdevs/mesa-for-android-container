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
#include "u_cpu_detect.h"

#define CACHELINE_SIZE 64
#define CACHELINE_MASK 63

size_t
util_cache_granularity(void)
{
   return util_get_cpu_caps()->cacheline;
}

/* Defined in cache_ops_x86_clflushopt.c */
#ifdef HAVE___BUILTIN_IA32_CLFLUSHOPT
void util_clflushopt_range(void *start, size_t size);
#endif

static void
util_clflush_range(void *start, size_t size)
{
   char *p = (char *) (((uintptr_t) start) & ~CACHELINE_MASK);
   char *end = ((char *) start) + size;

   while (p < end) {
      __builtin_ia32_clflush(p);
      p += CACHELINE_SIZE;
   }
}

void
util_flush_range_no_fence(void *start, size_t size)
{
#ifdef HAVE___BUILTIN_IA32_CLFLUSHOPT
   if (util_get_cpu_caps()->has_clflushopt) {
      util_clflushopt_range(start, size);
      return;
   }
#endif
   util_clflush_range(start, size);
}

void
util_flush_range(void *start, size_t size)
{
   __builtin_ia32_mfence();
#ifdef HAVE___BUILTIN_IA32_CLFLUSHOPT
   if (util_get_cpu_caps()->has_clflushopt) {
      util_clflushopt_range(start, size);
      /* clflushopt doesn't include an mfence like clflush */
      __builtin_ia32_mfence();
      return;
   }
#endif
   util_clflush_range(start, size);
   /* The last clflush acts as an mfence */
}

void
util_flush_inval_range_no_fence(void *start, size_t size)
{
   if (size == 0)
      return;

   /* Modern Atom CPUs (Baytrail+) have issues with clflush serialization,
    * where mfence is not a sufficient synchronization barrier.  We must
    * double clflush the last cacheline.  This guarantees it will be ordered
    * after the preceding clflushes, and then the mfence guards against
    * prefetches crossing the clflush boundary.
    *
    * See kernel commit 396f5d62d1a5fd99421855a08ffdef8edb43c76e
    * ("drm: Restore double clflush on the last partial cacheline")
    * and https://bugs.freedesktop.org/show_bug.cgi?id=92845.
    */
#ifdef HAVE___BUILTIN_IA32_CLFLUSHOPT
   if (util_get_cpu_caps()->has_clflushopt) {
      util_clflushopt_range(start, size);
      /* clflushopt doesn't include an mfence like clflush */
      __builtin_ia32_mfence();
      util_clflushopt_range((char *)start + size - 1, 1);
      return;
   }
#endif
   util_clflush_range(start, size);
   __builtin_ia32_clflush((char *)start + size - 1);
}

void
util_flush_inval_range(void *start, size_t size)
{
   util_flush_inval_range_no_fence(start, size);
   __builtin_ia32_mfence();
}

void
util_pre_flush_fence(void)
{
   __builtin_ia32_mfence();
}

void
util_post_flush_fence(void)
{
   __builtin_ia32_mfence();
}

void
util_post_flush_inval_fence(void)
{
   __builtin_ia32_mfence();
}
