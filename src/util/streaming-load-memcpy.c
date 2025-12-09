/*
 * Copyright © 2013 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Matt Turner <mattst88@gmail.com>
 *
 */

#include "util/detect_arch.h"
#include "util/streaming-load-memcpy.h"
#include "util/u_cpu_detect.h"
#include "util/u_math.h"
#ifdef USE_SSE41
#include <smmintrin.h>
#endif

/* Copies memory from src to dst, using non-temporal load instructions to get
 * streaming read performance from uncached memory.
 */
void
util_streaming_load_memcpy(void *restrict dst, void *restrict src, size_t len)
{
   char *restrict d = dst;
   char *restrict s = src;

#if defined(USE_SSE41) || (DETECT_ARCH_AARCH64 && DETECT_CC_GCC)
   /* If dst and src are not co-aligned, or if non-temporal load instructions
    * are not present, fallback to memcpy(). */
   if (((uintptr_t)d & 15) != ((uintptr_t)s & 15)
#if defined(USE_SSE41)
       || !util_get_cpu_caps()->has_sse4_1
#endif
       ) {
      memcpy(d, s, len);
      return;
   }

   /* memcpy() the misaligned header. At the end of this if block, <d> and <s>
    * are aligned to a 16-byte boundary or <len> == 0.
    */
   if ((uintptr_t)d & 15) {
      uintptr_t bytes_before_alignment_boundary = 16 - ((uintptr_t)d & 15);
      assert(bytes_before_alignment_boundary < 16);

      memcpy(d, s, MIN2(bytes_before_alignment_boundary, len));

      d = (char *)align_uintptr((uintptr_t)d, 16);
      s = (char *)align_uintptr((uintptr_t)s, 16);
      len -= MIN2(bytes_before_alignment_boundary, len);
   }

#if defined(USE_SSE41)
   if (len >= 64)
      _mm_mfence();

   while (len >= 64) {
      __m128i *dst_cacheline = (__m128i *)d;
      __m128i *src_cacheline = (__m128i *)s;

      __m128i temp1 = _mm_stream_load_si128(src_cacheline + 0);
      __m128i temp2 = _mm_stream_load_si128(src_cacheline + 1);
      __m128i temp3 = _mm_stream_load_si128(src_cacheline + 2);
      __m128i temp4 = _mm_stream_load_si128(src_cacheline + 3);

      _mm_store_si128(dst_cacheline + 0, temp1);
      _mm_store_si128(dst_cacheline + 1, temp2);
      _mm_store_si128(dst_cacheline + 2, temp3);
      _mm_store_si128(dst_cacheline + 3, temp4);

      d += 64;
      s += 64;
      len -= 64;
   }

#elif DETECT_ARCH_AARCH64 && DETECT_CC_GCC
   if (len >= 64) {
      __asm__ volatile(
         /* Memory barrier for loads completion in the non-shareable domain:
          * https://developer.arm.com/documentation/102336/0100/Limiting-the-scope-of-memory-barriers */
         "  dmb nshld\n"

         /* Allow branching on negative flag using subs. */
         "  sub %[len], %[len], #64\n"

         /* Based on ARM optimized routines, using non-temporal loads:
          * https://github.com/ARM-software/optimized-routines/blob/master/string/aarch64/memcpy-sve.S */
         "loop64:\n"
         "  ldnp q0, q1, [%[s]]\n"
         "  stp q0, q1, [%[d]]\n"
         "  ldnp q0, q1, [%[s], #32]\n"
         "  stp q0, q1, [%[d], #32]\n"
         "  add %[s], %[s], #64\n"
         "  add %[d], %[d], #64\n"
         "  subs %[len], %[len], #64\n"
         "  b.pl loop64\n"

         /* Restore <len>. */
         "  add %[len], %[len], #64\n"

         : [d]"+r"(d), [s]"+r"(s), [len]"+r"(len) :
         : "v0", "v1", "cc", "memory");
   }
#endif

#endif
   /* memcpy() the tail. */
   if (len) {
      memcpy(d, s, len);
   }
}
