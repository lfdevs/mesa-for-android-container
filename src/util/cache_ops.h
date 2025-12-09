/*
 * Copyright © 2025 Collabora Ltd.
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

#ifndef UTIL_CACHE_OPS_H
#define UTIL_CACHE_OPS_H

#include <stdbool.h>
#include <stddef.h>

#include "detect_arch.h"
#include "u_cpu_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Returns true if we have cache operations available */
static inline bool
util_has_cache_ops(void)
{
   /* TODO: Port to MSVC if and when we have Windows hardware drivers that
    * need cache flushing ops.
    */
#if defined(_MSC_VER)
   return false;
#endif

#if DETECT_ARCH_X86
   return util_get_cpu_caps()->has_sse2;
#elif DETECT_ARCH_X86_64 || DETECT_ARCH_AARCH64
   return true;
#else
   return false;
#endif
}

/** Returns the cache granularity
 *
 * This is the maximum number of bytes that may be overwritten as the result
 * of a cache flush or cache line eviction.  On big.LITTLE platforms, the
 * cache flush helpers may sometimes operate at a smaller granularity but may
 * also round up to at most util_cache_granularity().
 *
 * Vulkan drivers should return this as nonCoherentAtomSize.
 */
size_t util_cache_granularity(void);

/** Flushes a range to main memory */
void util_flush_range(void *start, size_t size);

/** Flushes a range to main memory and invalidates those cache lines */
void util_flush_inval_range(void *start, size_t size);

/** Flushes a range to main memory without fencing
 *
 * This is for the case where you have a lot of ranges to flush and want to
 * avoid unnecessary fencing.  In this case, call
 *
 *    util_pre_flush_fence()
 *    util_flush_range_no_fence()
 *    util_flush_range_no_fence()
 *    util_post_flush_fence()
 */
void util_flush_range_no_fence(void *start, size_t size);

/** Flushes a range to main memory and invalidates those cache lines without
 * fencing
 *
 * This is for the case where you have a lot of ranges to flush and invalidate
 * and want to avoid unnecessary fencing.  In this case, call
 *
 *    util_pre_flush_fence()
 *    util_flush_inval_range_no_fence()
 *    util_flush_range_no_fence()
 *    util_flush_inval_range_no_fence()
 *    util_post_flush_inval_fence()
 */
void util_flush_inval_range_no_fence(void *start, size_t size);

/** Fence between memory access and cache flush operations
 *
 * see util_flush_range_no_fence()
 */
void util_pre_flush_fence(void);

/** Fence between cache flush operations and memory access
 *
 * see util_flush_range_no_fence()
 */
void util_post_flush_fence(void);

/** Fence between cache invalidate operations and memory access
 *
 * see util_flush_inval_range_no_fence()
 */
void util_post_flush_inval_fence(void);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_CACHE_OPS_H */
