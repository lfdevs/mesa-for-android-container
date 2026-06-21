/*
 * Copyright © 2025 Raspberry Pi Ltd
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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


#include <stdbool.h>
#include <stdint.h>
#include "v3dv_limits.h"

#ifdef v3dX
#  include "broadcom/cle/v3dx_pack.h"
#else
#  define v3dX(x) v3d42_##x
#  include "v3dvx_format_table.h"
#  undef v3dX

#  define v3dX(x) v3d71_##x
#  include "v3dvx_format_table.h"
#  undef v3dX
#endif

struct v3dv_format_plane {
   /* One of V3D42_OUTPUT_IMAGE_FORMAT_*, or OUTPUT_IMAGE_FORMAT_NO */
   enum V3DX(Output_Image_Format) rt_type;

   /* One of V3D42_TEXTURE_DATA_FORMAT_*. */
   enum V3DX(Texture_Data_Formats) tex_type;

   /* Swizzle to apply to the RGBA shader output for storing to the tile
    * buffer, to the RGBA tile buffer to produce shader input (for
    * blending), and for turning the rgba8888 texture sampler return
    * value into shader rgba values.
    */
   uint8_t swizzle[4];

   /* Whether the return value is 16F/I/UI or 32F/I/UI. */
   uint8_t return_size;

   /* Needs software unorm packing */
   bool sw_unorm;

   /* Needs software snorm packing */
   bool sw_snorm;
};

struct v3dv_format {
   /* Non 0 plane count implies supported */
   uint8_t plane_count;

   struct v3dv_format_plane planes[V3DV_MAX_PLANE_COUNT];

   /* If the format supports (linear) filtering when texturing. */
   bool supports_filtering;
};
