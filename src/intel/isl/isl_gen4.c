/*
 * Copyright 2015 Intel Corporation
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

#include "isl_gen4.h"
#include "isl_priv.h"

bool
isl_gen4_choose_msaa_layout(const struct isl_device *dev,
                            const struct isl_surf_init_info *info,
                            enum isl_tiling tiling,
                            enum isl_msaa_layout *msaa_layout)
{
   /* Gen4 and Gen5 do not support MSAA */
   assert(info->samples >= 1);

   *msaa_layout = ISL_MSAA_LAYOUT_NONE;
   return true;
}

void
isl_gen4_choose_image_alignment_el(const struct isl_device *dev,
                                   const struct isl_surf_init_info *restrict info,
                                   enum isl_tiling tiling,
                                   enum isl_dim_layout dim_layout,
                                   enum isl_msaa_layout msaa_layout,
                                   struct isl_extent3d *image_align_el)
{
   assert(info->samples == 1);
   assert(msaa_layout == ISL_MSAA_LAYOUT_NONE);
   assert(!isl_tiling_is_std_y(tiling));

   /* Note that neither the surface's horizontal nor vertical image alignment
    * is programmable on gen4 nor gen5.
    *
    * From the G35 PRM (2008-01), Volume 1 Graphics Core, Section 6.17.3.4
    * Alignment Unit Size:
    *
    *    Note that the compressed formats are padded to a full compression
    *    cell.
    *
    *    +------------------------+--------+--------+
    *    | format                 | halign | valign |
    *    +------------------------+--------+--------+
    *    | YUV 4:2:2 formats      |      4 |      2 |
    *    | uncompressed formats   |      4 |      2 |
    *    +------------------------+--------+--------+
    */

   if (isl_format_is_compressed(info->format)) {
      *image_align_el = isl_extent3d(1, 1, 1);
      return;
   }

   *image_align_el = isl_extent3d(4, 2, 1);
}
