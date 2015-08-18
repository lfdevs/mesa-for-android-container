/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2012-2013 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "genhw/genhw.h"
#include "ilo_state_surface.h"

static bool
surface_valid_sampler_format(const struct ilo_dev *dev,
                             enum ilo_state_surface_access access,
                             enum gen_surface_format format)
{
   /*
    * This table is based on:
    *
    *  - the Sandy Bridge PRM, volume 4 part 1, page 88-97
    *  - the Ivy Bridge PRM, volume 4 part 1, page 84-87
    */
   static const struct sampler_cap {
      int sampling;
      int filtering;
      int shadow_map;
      int chroma_key;
   } caps[] = {
#define CAP(sampling, filtering, shadow_map, chroma_key) \
      { ILO_GEN(sampling), ILO_GEN(filtering), ILO_GEN(shadow_map), ILO_GEN(chroma_key) }
      [GEN6_FORMAT_R32G32B32A32_FLOAT]       = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_R32G32B32A32_SINT]        = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R32G32B32A32_UINT]        = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R32G32B32X32_FLOAT]       = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_R32G32B32_FLOAT]          = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_R32G32B32_SINT]           = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R32G32B32_UINT]           = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16G16B16A16_UNORM]       = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R16G16B16A16_SNORM]       = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R16G16B16A16_SINT]        = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16G16B16A16_UINT]        = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16G16B16A16_FLOAT]       = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R32G32_FLOAT]             = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_R32G32_SINT]              = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R32G32_UINT]              = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R32_FLOAT_X8X24_TYPELESS] = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_X32_TYPELESS_G8X24_UINT]  = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_L32A32_FLOAT]             = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_R16G16B16X16_UNORM]       = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R16G16B16X16_FLOAT]       = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_A32X32_FLOAT]             = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_L32X32_FLOAT]             = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_I32X32_FLOAT]             = CAP(  1,   5,   0,   0),
      [GEN6_FORMAT_B8G8R8A8_UNORM]           = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_B8G8R8A8_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R10G10B10A2_UNORM]        = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R10G10B10A2_UNORM_SRGB]   = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R10G10B10A2_UINT]         = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R10G10B10_SNORM_A2_UNORM] = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8B8A8_UNORM]           = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8B8A8_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8B8A8_SNORM]           = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8B8A8_SINT]            = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R8G8B8A8_UINT]            = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16G16_UNORM]             = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R16G16_SNORM]             = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R16G16_SINT]              = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16G16_UINT]              = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16G16_FLOAT]             = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B10G10R10A2_UNORM]        = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B10G10R10A2_UNORM_SRGB]   = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R11G11B10_FLOAT]          = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R32_SINT]                 = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R32_UINT]                 = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R32_FLOAT]                = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_R24_UNORM_X8_TYPELESS]    = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_X24_TYPELESS_G8_UINT]     = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_L16A16_UNORM]             = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_I24X8_UNORM]              = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_L24X8_UNORM]              = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_A24X8_UNORM]              = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_I32_FLOAT]                = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_L32_FLOAT]                = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_A32_FLOAT]                = CAP(  1,   5,   1,   0),
      [GEN6_FORMAT_B8G8R8X8_UNORM]           = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_B8G8R8X8_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8B8X8_UNORM]           = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8B8X8_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R9G9B9E5_SHAREDEXP]       = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B10G10R10X2_UNORM]        = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_L16A16_FLOAT]             = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B5G6R5_UNORM]             = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_B5G6R5_UNORM_SRGB]        = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B5G5R5A1_UNORM]           = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_B5G5R5A1_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B4G4R4A4_UNORM]           = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_B4G4R4A4_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8_UNORM]               = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8_SNORM]               = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_R8G8_SINT]                = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R8G8_UINT]                = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16_UNORM]                = CAP(  1,   1,   1,   0),
      [GEN6_FORMAT_R16_SNORM]                = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R16_SINT]                 = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16_UINT]                 = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R16_FLOAT]                = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_A8P8_UNORM_PALETTE0]      = CAP(  5,   5,   0,   0),
      [GEN6_FORMAT_A8P8_UNORM_PALETTE1]      = CAP(  5,   5,   0,   0),
      [GEN6_FORMAT_I16_UNORM]                = CAP(  1,   1,   1,   0),
      [GEN6_FORMAT_L16_UNORM]                = CAP(  1,   1,   1,   0),
      [GEN6_FORMAT_A16_UNORM]                = CAP(  1,   1,   1,   0),
      [GEN6_FORMAT_L8A8_UNORM]               = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_I16_FLOAT]                = CAP(  1,   1,   1,   0),
      [GEN6_FORMAT_L16_FLOAT]                = CAP(  1,   1,   1,   0),
      [GEN6_FORMAT_A16_FLOAT]                = CAP(  1,   1,   1,   0),
      [GEN6_FORMAT_L8A8_UNORM_SRGB]          = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_R5G5_SNORM_B6_UNORM]      = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_P8A8_UNORM_PALETTE0]      = CAP(  5,   5,   0,   0),
      [GEN6_FORMAT_P8A8_UNORM_PALETTE1]      = CAP(  5,   5,   0,   0),
      [GEN6_FORMAT_R8_UNORM]                 = CAP(  1,   1,   0, 4.5),
      [GEN6_FORMAT_R8_SNORM]                 = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8_SINT]                  = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_R8_UINT]                  = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_A8_UNORM]                 = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_I8_UNORM]                 = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_L8_UNORM]                 = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_P4A4_UNORM_PALETTE0]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_A4P4_UNORM_PALETTE0]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_P8_UNORM_PALETTE0]        = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_L8_UNORM_SRGB]            = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_P8_UNORM_PALETTE1]        = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_P4A4_UNORM_PALETTE1]      = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_A4P4_UNORM_PALETTE1]      = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_DXT1_RGB_SRGB]            = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_R1_UNORM]                 = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_YCRCB_NORMAL]             = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_YCRCB_SWAPUVY]            = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_P2_UNORM_PALETTE0]        = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_P2_UNORM_PALETTE1]        = CAP(4.5, 4.5,   0,   0),
      [GEN6_FORMAT_BC1_UNORM]                = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_BC2_UNORM]                = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_BC3_UNORM]                = CAP(  1,   1,   0,   1),
      [GEN6_FORMAT_BC4_UNORM]                = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_BC5_UNORM]                = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_BC1_UNORM_SRGB]           = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_BC2_UNORM_SRGB]           = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_BC3_UNORM_SRGB]           = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_MONO8]                    = CAP(  1,   0,   0,   0),
      [GEN6_FORMAT_YCRCB_SWAPUV]             = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_YCRCB_SWAPY]              = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_DXT1_RGB]                 = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_FXT1]                     = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_BC4_SNORM]                = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_BC5_SNORM]                = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R16G16B16_FLOAT]          = CAP(  5,   5,   0,   0),
      [GEN6_FORMAT_BC6H_SF16]                = CAP(  7,   7,   0,   0),
      [GEN6_FORMAT_BC7_UNORM]                = CAP(  7,   7,   0,   0),
      [GEN6_FORMAT_BC7_UNORM_SRGB]           = CAP(  7,   7,   0,   0),
      [GEN6_FORMAT_BC6H_UF16]                = CAP(  7,   7,   0,   0),
#undef CAP
   };

   ILO_DEV_ASSERT(dev, 6, 8);

   return (format < ARRAY_SIZE(caps) && caps[format].sampling &&
           ilo_dev_gen(dev) >= caps[format].sampling);
}

static bool
surface_valid_dp_format(const struct ilo_dev *dev,
                        enum ilo_state_surface_access access,
                        enum gen_surface_format format)
{
   /*
    * This table is based on:
    *
    *  - the Sandy Bridge PRM, volume 4 part 1, page 88-97
    *  - the Ivy Bridge PRM, volume 4 part 1, page 172, 252-253, and 277-278
    *  - the Haswell PRM, volume 7, page 262-264
    */
   static const struct dp_cap {
      int rt_write;
      int rt_write_blending;
      int typed_write;
      int media_color_processing;
   } caps[] = {
#define CAP(rt_write, rt_write_blending, typed_write, media_color_processing) \
      { ILO_GEN(rt_write), ILO_GEN(rt_write_blending), ILO_GEN(typed_write), ILO_GEN(media_color_processing) }
      [GEN6_FORMAT_R32G32B32A32_FLOAT]       = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_R32G32B32A32_SINT]        = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R32G32B32A32_UINT]        = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16G16B16A16_UNORM]       = CAP(  1, 4.5,   7,   6),
      [GEN6_FORMAT_R16G16B16A16_SNORM]       = CAP(  1,   6,   7,   0),
      [GEN6_FORMAT_R16G16B16A16_SINT]        = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16G16B16A16_UINT]        = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16G16B16A16_FLOAT]       = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_R32G32_FLOAT]             = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_R32G32_SINT]              = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R32G32_UINT]              = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_B8G8R8A8_UNORM]           = CAP(  1,   1,   7,   6),
      [GEN6_FORMAT_B8G8R8A8_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R10G10B10A2_UNORM]        = CAP(  1,   1,   7,   6),
      [GEN6_FORMAT_R10G10B10A2_UNORM_SRGB]   = CAP(  0,   0,   0,   6),
      [GEN6_FORMAT_R10G10B10A2_UINT]         = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R8G8B8A8_UNORM]           = CAP(  1,   1,   7,   6),
      [GEN6_FORMAT_R8G8B8A8_UNORM_SRGB]      = CAP(  1,   1,   0,   6),
      [GEN6_FORMAT_R8G8B8A8_SNORM]           = CAP(  1,   6,   7,   0),
      [GEN6_FORMAT_R8G8B8A8_SINT]            = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R8G8B8A8_UINT]            = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16G16_UNORM]             = CAP(  1, 4.5,   7,   0),
      [GEN6_FORMAT_R16G16_SNORM]             = CAP(  1,   6,   7,   0),
      [GEN6_FORMAT_R16G16_SINT]              = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16G16_UINT]              = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16G16_FLOAT]             = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_B10G10R10A2_UNORM]        = CAP(  1,   1,   7,   6),
      [GEN6_FORMAT_B10G10R10A2_UNORM_SRGB]   = CAP(  1,   1,   0,   6),
      [GEN6_FORMAT_R11G11B10_FLOAT]          = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_R32_SINT]                 = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R32_UINT]                 = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R32_FLOAT]                = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_B8G8R8X8_UNORM]           = CAP(  0,   0,   0,   6),
      [GEN6_FORMAT_B5G6R5_UNORM]             = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_B5G6R5_UNORM_SRGB]        = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B5G5R5A1_UNORM]           = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_B5G5R5A1_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_B4G4R4A4_UNORM]           = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_B4G4R4A4_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8G8_UNORM]               = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_R8G8_SNORM]               = CAP(  1,   6,   7,   0),
      [GEN6_FORMAT_R8G8_SINT]                = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R8G8_UINT]                = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16_UNORM]                = CAP(  1, 4.5,   7,   7),
      [GEN6_FORMAT_R16_SNORM]                = CAP(  1,   6,   7,   0),
      [GEN6_FORMAT_R16_SINT]                 = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16_UINT]                 = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R16_FLOAT]                = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_B5G5R5X1_UNORM]           = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_B5G5R5X1_UNORM_SRGB]      = CAP(  1,   1,   0,   0),
      [GEN6_FORMAT_R8_UNORM]                 = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_R8_SNORM]                 = CAP(  1,   6,   7,   0),
      [GEN6_FORMAT_R8_SINT]                  = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_R8_UINT]                  = CAP(  1,   0,   7,   0),
      [GEN6_FORMAT_A8_UNORM]                 = CAP(  1,   1,   7,   0),
      [GEN6_FORMAT_YCRCB_NORMAL]             = CAP(  1,   0,   0,   6),
      [GEN6_FORMAT_YCRCB_SWAPUVY]            = CAP(  1,   0,   0,   6),
      [GEN6_FORMAT_YCRCB_SWAPUV]             = CAP(  1,   0,   0,   6),
      [GEN6_FORMAT_YCRCB_SWAPY]              = CAP(  1,   0,   0,   6),
#undef CAP
   };

   ILO_DEV_ASSERT(dev, 6, 8);

   if (format >= ARRAY_SIZE(caps))
      return false;

   switch (access) {
   case ILO_STATE_SURFACE_ACCESS_DP_RENDER:
      return (caps[format].rt_write &&
              ilo_dev_gen(dev) >= caps[format].rt_write);
   case ILO_STATE_SURFACE_ACCESS_DP_TYPED:
      return (caps[format].typed_write &&
              ilo_dev_gen(dev) >= caps[format].typed_write);
   case ILO_STATE_SURFACE_ACCESS_DP_UNTYPED:
      return (format == GEN6_FORMAT_RAW);
   case ILO_STATE_SURFACE_ACCESS_DP_DATA:
      /* ignored, but can it be raw? */
      assert(format != GEN6_FORMAT_RAW);
      return true;
   default:
      return false;
   }
}

static bool
surface_valid_svb_format(const struct ilo_dev *dev,
                         enum gen_surface_format format)
{
   ILO_DEV_ASSERT(dev, 6, 8);

   /*
    * This table is based on:
    *
    *  - the Sandy Bridge PRM, volume 4 part 1, page 88-97
    *  - the Ivy Bridge PRM, volume 2 part 1, page 195
    *  - the Haswell PRM, volume 7, page 535
    */
   switch (format) {
   case GEN6_FORMAT_R32G32B32A32_FLOAT:
   case GEN6_FORMAT_R32G32B32A32_SINT:
   case GEN6_FORMAT_R32G32B32A32_UINT:
   case GEN6_FORMAT_R32G32B32_FLOAT:
   case GEN6_FORMAT_R32G32B32_SINT:
   case GEN6_FORMAT_R32G32B32_UINT:
   case GEN6_FORMAT_R32G32_FLOAT:
   case GEN6_FORMAT_R32G32_SINT:
   case GEN6_FORMAT_R32G32_UINT:
   case GEN6_FORMAT_R32_SINT:
   case GEN6_FORMAT_R32_UINT:
   case GEN6_FORMAT_R32_FLOAT:
      return true;
   default:
      return false;
   }
}

bool
ilo_state_surface_valid_format(const struct ilo_dev *dev,
                               enum ilo_state_surface_access access,
                               enum gen_surface_format format)
{
   bool valid;

   switch (access) {
   case ILO_STATE_SURFACE_ACCESS_SAMPLER:
      valid = surface_valid_sampler_format(dev, access, format);
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_RENDER:
   case ILO_STATE_SURFACE_ACCESS_DP_TYPED:
   case ILO_STATE_SURFACE_ACCESS_DP_UNTYPED:
   case ILO_STATE_SURFACE_ACCESS_DP_DATA:
      valid = surface_valid_dp_format(dev, access, format);
      break;
   case ILO_STATE_SURFACE_ACCESS_DP_SVB:
      valid = surface_valid_svb_format(dev, format);
      break;
   default:
      valid = false;
      break;
   }

   return valid;
}
