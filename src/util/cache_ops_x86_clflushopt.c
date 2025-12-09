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

#include <stdint.h>
#include "util/u_cpu_detect.h"

#ifndef HAVE___BUILTIN_IA32_CLFLUSHOPT
#error "Compiler doesn't support clflushopt!"
#endif

void util_clflushopt_range(void *start, size_t size);

void
util_clflushopt_range(void *start, size_t size)
{
   const struct util_cpu_caps_t *cpu_caps = util_get_cpu_caps();
   assert(cpu_caps->has_clflushopt);
   assert(cpu_caps->cacheline > 0);
   uint8_t *p = (uint8_t *) (((uintptr_t) start) &
                            ~((uintptr_t)cpu_caps->cacheline - 1));
   uint8_t *end = (uint8_t *)start + size;

   while (p < end) {
      __builtin_ia32_clflushopt(p);
      p += cpu_caps->cacheline;
   }
}
