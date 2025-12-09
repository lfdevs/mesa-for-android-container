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

#include "cache_ops.h"
#include "util/macros.h"

size_t
util_cache_granularity()
{
   return 0;
}

void
util_flush_range(void *start, size_t size)
{
   UNREACHABLE("Cache ops are not implemented on this platform");
}

void
util_flush_inval_range(void *start, size_t size)
{
   UNREACHABLE("Cache ops are not implemented on this platform");
}

void
util_flush_range_no_fence(void *start, size_t size)
{
   UNREACHABLE("Cache ops are not implemented on this platform");
}

void
util_flush_inval_range_no_fence(void *start, size_t size)
{
   UNREACHABLE("Cache ops are not implemented on this platform");
}

void util_pre_flush_fence(void)
{
   UNREACHABLE("Cache ops are not implemented on this platform");
}

void util_post_flush_fence(void)
{
   UNREACHABLE("Cache ops are not implemented on this platform");
}

void util_post_flush_inval_fence(void)
{
   UNREACHABLE("Cache ops are not implemented on this platform");
}
