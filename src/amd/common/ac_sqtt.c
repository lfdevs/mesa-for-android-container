/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2020 Valve Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ac_sqtt.h"

#include "ac_gpu_info.h"
#include "util/u_math.h"

uint64_t
ac_thread_trace_get_info_offset(unsigned se)
{
   return sizeof(struct ac_thread_trace_info) * se;
}

uint64_t
ac_thread_trace_get_data_offset(struct ac_thread_trace_data *data, unsigned se)
{
   uint64_t data_offset;

   data_offset = align64(sizeof(struct ac_thread_trace_info) * 4,
               1 << SQTT_BUFFER_ALIGN_SHIFT);
   data_offset += data->buffer_size * se;

   return data_offset;
}

uint64_t
ac_thread_trace_get_info_va(uint64_t va, unsigned se)
{
   return va + ac_thread_trace_get_info_offset(se);
}

uint64_t
ac_thread_trace_get_data_va(struct ac_thread_trace_data *data, uint64_t va, unsigned se)
{
   return va + ac_thread_trace_get_data_offset(data, se);
}

bool
ac_is_thread_trace_complete(struct radeon_info *rad_info,
                            const struct ac_thread_trace_info *info)
{
   if (rad_info->chip_class == GFX10) {
      /* GFX10 doesn't have THREAD_TRACE_CNTR but it reports the
       * number of dropped bytes for all SEs via
       * THREAD_TRACE_DROPPED_CNTR.
       */
      return info->gfx10_dropped_cntr == 0;
   }

   /* Otherwise, compare the current thread trace offset with the number
    * of written bytes.
    */
   return info->cur_offset == info->gfx9_write_counter;
}

uint32_t
ac_get_expected_buffer_size(struct radeon_info *rad_info,
                            const struct ac_thread_trace_info *info)
{
   if (rad_info->chip_class == GFX10) {
      uint32_t dropped_cntr_per_se = info->gfx10_dropped_cntr / rad_info->max_se;
      return ((info->cur_offset * 32) + dropped_cntr_per_se) / 1024;
   }

   return (info->gfx9_write_counter * 32) / 1024;
}
