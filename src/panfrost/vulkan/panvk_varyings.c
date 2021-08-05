/*
 * Copyright (C) 2021 Collabora Ltd.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "panvk_private.h"
#include "panvk_varyings.h"

#include "pan_pool.h"

unsigned
panvk_varyings_buf_count(const struct panvk_device *dev,
                         struct panvk_varyings_info *varyings)
{
   const struct panfrost_device *pdev = &dev->physical_device->pdev;

   return util_bitcount(varyings->buf_mask) + (pan_is_bifrost(pdev) ? 1 : 0);
}

void
panvk_varyings_alloc(struct panvk_varyings_info *varyings,
                     struct pan_pool *varying_mem_pool,
                     unsigned vertex_count)
{
   for (unsigned i = 0; i < PANVK_VARY_BUF_MAX; i++) {
      if (!(varyings->buf_mask & (1 << i))) continue;

      unsigned buf_idx = panvk_varying_buf_index(varyings, i);
      unsigned size = varyings->buf[buf_idx].stride * vertex_count;
      if (!size)
         continue;

      struct panfrost_ptr ptr =
         pan_pool_alloc_aligned(varying_mem_pool, size, 64);

      varyings->buf[buf_idx].size = size;
      varyings->buf[buf_idx].address = ptr.gpu;
      varyings->buf[buf_idx].cpu = ptr.cpu;
   }
}
