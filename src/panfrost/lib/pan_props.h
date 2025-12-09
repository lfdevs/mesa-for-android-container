/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#ifndef PAN_PROPS_H
#define PAN_PROPS_H

#include <stdbool.h>
#include <stdint.h>

#include "util/macros.h"

#include "pan_model.h"

struct pan_kmod_dev;
struct pan_kmod_dev_props;
struct pan_kmod_vm;

#define ARM_VENDOR_ID 0x13B5

unsigned pan_query_l2_slices(const struct pan_kmod_dev_props *props);

struct pan_tiler_features
pan_query_tiler_features(const struct pan_kmod_dev_props *props);

unsigned pan_query_thread_tls_alloc(const struct pan_kmod_dev_props *props);

uint32_t pan_query_compressed_formats(const struct pan_kmod_dev_props *props);

unsigned pan_query_core_count(const struct pan_kmod_dev_props *props,
                              unsigned *core_id_range);

bool pan_query_afbc(const struct pan_kmod_dev_props *props);

bool pan_query_afrc(const struct pan_kmod_dev_props *props);

unsigned pan_query_tib_size(const struct pan_model *model);

unsigned pan_query_z_tib_size(const struct pan_model *model);

static inline unsigned
pan_query_optimal_tib_size(unsigned arch, const struct pan_model *model)
{
   unsigned tib_size = pan_query_tib_size(model);

   /* On V5, as well as V7 and later, we can disable pipelining to gain some
    * extra tib memory.
    */
   if (arch > 4 && arch != 6)
      return tib_size / 2;

   return tib_size;
}

static inline unsigned
pan_query_optimal_z_tib_size(unsigned arch, const struct pan_model *model)
{
   unsigned tib_size = pan_query_z_tib_size(model);

   /* On V5, as well as V7 and later, we can disable pipelining to gain some
    * extra tib memory.
    */
   if (arch > 4 && arch != 6)
      return tib_size / 2;

   return tib_size;
}

uint64_t pan_clamp_to_usable_va_range(const struct pan_kmod_dev *dev,
                                      uint64_t va);

uint64_t pan_choose_gpu_va_alignment(const struct pan_kmod_vm *vm,
                                     uint64_t size);

unsigned pan_compute_max_thread_count(const struct pan_kmod_dev_props *props,
                                      unsigned work_reg_count);

static inline unsigned
pan_max_effective_tile_size(unsigned arch)
{
   if (arch >= 12)
      return 64 * 64;

   if (arch >= 10)
      return 32 * 32;

   return 16 * 16;
}

static inline unsigned
pan_meta_tile_size(unsigned arch)
{
   if (arch >= 12)
      return 64;

   return 32;
}

static inline uint32_t
pan_get_max_cbufs(unsigned arch, unsigned max_tib_size)
{
   if (arch < 5)
      return 1;

   const unsigned min_msaa = 4;            /* Vulkan *requires* at least 4x MSAA support */
   const unsigned max_cbuf_format = 4 * 4; /* R32G32B32A32 */
   const unsigned min_tile_size = 4 * 4;

   unsigned max_cbufs =
      max_tib_size / (min_msaa * max_cbuf_format * min_tile_size);

   return MIN2(max_cbufs, 8);
}

static inline unsigned
pan_get_max_msaa(unsigned arch, unsigned max_tib_size, unsigned max_cbuf_atts,
                 unsigned format_size)
{
   if (arch < 5)
      return 8;

   assert(max_cbuf_atts > 0);
   assert(format_size > 0);

   /* When using an internal format with less than 32-bit per pixels, we're
    * currently using either AU (Additional precision, Unorm) or PU (Padded
    * precision, Unorm), meaning that we need additional bits in the tilebuffer
    * that's used by dithering.
    */
   format_size = MAX2(format_size, 4);

   const unsigned min_tile_size = 4 * 4;
   unsigned max_msaa = max_tib_size / (max_cbuf_atts * format_size *
                                       min_tile_size);
   return MIN2(max_msaa, 16);
}

#endif
