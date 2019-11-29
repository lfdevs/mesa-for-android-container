/*
 * Copyright (c) 2018 Intel Corporation
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice (including the next
 *  paragraph) shall be included in all copies or substantial portions of the
 *  Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 */

#include "isl_gen9.h"
#include "isl_gen12.h"
#include "isl_priv.h"

void
isl_gen12_choose_image_alignment_el(const struct isl_device *dev,
                                    const struct isl_surf_init_info *restrict info,
                                    enum isl_tiling tiling,
                                    enum isl_dim_layout dim_layout,
                                    enum isl_msaa_layout msaa_layout,
                                    struct isl_extent3d *image_align_el)
{
   /* Handled by isl_choose_image_alignment_el */
   assert(info->format != ISL_FORMAT_HIZ);

   const struct isl_format_layout *fmtl = isl_format_get_layout(info->format);
   if (fmtl->txc == ISL_TXC_CCS) {
      /* This CCS compresses a 2D-view of the entire surface. */
      assert(info->levels == 1 && info->array_len == 1 && info->depth == 1);
      *image_align_el = isl_extent3d(1, 1, 1);
      return;
   }

   if (isl_surf_usage_is_depth(info->usage)) {
      /* The alignment parameters for depth buffers are summarized in the
       * following table:
       *
       *     Surface Format  |    MSAA     | Align Width | Align Height
       *    -----------------+-------------+-------------+--------------
       *       D16_UNORM     | 1x, 4x, 16x |      8      |      8
       *     ----------------+-------------+-------------+--------------
       *       D16_UNORM     |   2x, 8x    |     16      |      4
       *     ----------------+-------------+-------------+--------------
       *         other       |     any     |      8      |      4
       *    -----------------+-------------+-------------+--------------
       */
      assert(isl_is_pow2(info->samples));
      *image_align_el =
         info->format != ISL_FORMAT_R16_UNORM ?
         isl_extent3d(8, 4, 1) :
         (info->samples == 2 || info->samples == 8 ?
          isl_extent3d(16, 4, 1) : isl_extent3d(8, 8, 1));
   } else if (isl_surf_usage_is_stencil(info->usage)) {
      *image_align_el = isl_extent3d(16, 8, 1);
   } else {
      isl_gen9_choose_image_alignment_el(dev, info, tiling, dim_layout,
                                         msaa_layout, image_align_el);
   }
}
