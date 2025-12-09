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

#include "util/macros.h"

#include "kmod/pan_kmod.h"
#include "pan_props.h"

#include <genxml/gen_macros.h>

unsigned
pan_query_l2_slices(const struct pan_kmod_dev_props *props)
{
   /* L2_SLICES is MEM_FEATURES[11:8] minus(1) */
   return ((props->mem_features >> 8) & 0xF) + 1;
}

struct pan_tiler_features
pan_query_tiler_features(const struct pan_kmod_dev_props *props)
{
   /* Default value (2^9 bytes and 8 levels) to match old behaviour */
   uint32_t raw = props->tiler_features;

   /* Bin size is log2 in the first byte, max levels in the second byte */
   return (struct pan_tiler_features){
      .bin_size = (1 << (raw & BITFIELD_MASK(5))),
      .max_levels = (raw >> 8) & BITFIELD_MASK(4),
   };
}

unsigned
pan_query_core_count(const struct pan_kmod_dev_props *props,
                     unsigned *core_id_range)
{
   /* On older kernels, worst-case to 16 cores */

   unsigned mask = props->shader_present;

   /* Some cores might be absent. In some cases, we care
    * about the range of core IDs (that is, the greatest core ID + 1). If
    * the core mask is contiguous, this equals the core count.
    */
   *core_id_range = util_last_bit(mask);

   /* The actual core count skips overs the gaps */
   return util_bitcount(mask);
}

unsigned
pan_query_thread_tls_alloc(const struct pan_kmod_dev_props *props)
{
   return props->max_tls_instance_per_core ?: props->max_threads_per_core;
}

unsigned
pan_compute_max_thread_count(const struct pan_kmod_dev_props *props,
                             unsigned work_reg_count)
{
   unsigned aligned_reg_count;

   /* 4, 8 or 16 registers per shader on Midgard
    * 32 or 64 registers per shader on Bifrost
    */
   if (pan_arch(props->gpu_id) <= 5) {
      aligned_reg_count = util_next_power_of_two(MAX2(work_reg_count, 4));
      assert(aligned_reg_count <= 16);
   } else {
      aligned_reg_count = work_reg_count <= 32 ? 32 : 64;
   }

   return MIN3(props->max_threads_per_wg, props->max_threads_per_core,
               props->num_registers_per_core / aligned_reg_count);
}

uint32_t
pan_query_compressed_formats(const struct pan_kmod_dev_props *props)
{
   return props->texture_features[0];
}

/* Check for AFBC hardware support. AFBC is introduced in v5. Implementations
 * may omit it, signaled as a nonzero value in the AFBC_FEATURES property. */

bool
pan_query_afbc(const struct pan_kmod_dev_props *props)
{
   unsigned reg = props->afbc_features;

   return (pan_arch(props->gpu_id) >= 5) && (reg == 0);
}

/* Check for AFRC hardware support. AFRC is introduced in v10. Implementations
 * may omit it, signaled in bit 25 of TEXTURE_FEATURES_0 property. */

bool
pan_query_afrc(const struct pan_kmod_dev_props *props)
{
   return (pan_arch(props->gpu_id) >= 10) &&
          (props->texture_features[0] & (1 << 25));
}

/*
 * To pipeline multiple tiles, a given tile may use at most half of the tile
 * buffer. This function returns the optimal size (assuming pipelining).
 *
 * For Mali-G510 and Mali-G310, we will need extra logic to query the tilebuffer
 * size for the particular variant. The CORE_FEATURES register might help.
 */
unsigned
pan_query_tib_size(const struct pan_model *model)
{
   /* Preconditions ensure the returned value is a multiple of 1 KiB, the
    * granularity of the colour buffer allocation field.
    */
   assert(model->tilebuffer.color_size >= 2048);
   assert(util_is_power_of_two_nonzero(model->tilebuffer.color_size));

   return model->tilebuffer.color_size;
}

unsigned
pan_query_z_tib_size(const struct pan_model *model)
{
   /* Preconditions ensure the returned value is a multiple of 1 KiB, the
    * granularity of the colour buffer allocation field.
    */
   assert(model->tilebuffer.z_size >= 1024);
   assert(util_is_power_of_two_nonzero(model->tilebuffer.z_size));

   return model->tilebuffer.z_size;
}

uint64_t
pan_clamp_to_usable_va_range(const struct pan_kmod_dev *dev, uint64_t va)
{
   struct pan_kmod_va_range user_va_range =
      pan_kmod_dev_query_user_va_range(dev);

   if (va < user_va_range.start)
      return user_va_range.start;
   else if (va > user_va_range.start + user_va_range.size)
      return user_va_range.start + user_va_range.size;

   return va;
}

uint64_t
pan_choose_gpu_va_alignment(const struct pan_kmod_vm *vm, uint64_t size)
{
   assert(vm->pgsize_bitmap != 0);

   uint64_t align = 0;
   u_foreach_bit64(pgsize_bit, vm->pgsize_bitmap) {
      uint64_t pgsize = (uint64_t)1 << pgsize_bit;
      if (align > 0 && pgsize > size)
         break;
      align = pgsize;
   }
   return align;
}
