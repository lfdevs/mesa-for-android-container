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
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_formats.c
 *
 * Converts Gallium formats (PIPE_FORMAT_*) to hardware ones (ISL_FORMAT_*).
 * Provides information about which formats support what features.
 */

#include "util/bitscan.h"
#include "util/macros.h"
#include "util/u_format.h"

#include "iris_resource.h"
#include "iris_screen.h"

static enum isl_format
iris_isl_format_for_pipe_format(enum pipe_format pf)
{
   static const enum isl_format table[PIPE_FORMAT_COUNT] = {
      [0 ... PIPE_FORMAT_COUNT-1] = ISL_FORMAT_UNSUPPORTED,

      [PIPE_FORMAT_B8G8R8A8_UNORM]          = ISL_FORMAT_B8G8R8A8_UNORM,
      [PIPE_FORMAT_B8G8R8X8_UNORM]          = ISL_FORMAT_B8G8R8X8_UNORM,
      [PIPE_FORMAT_B5G5R5A1_UNORM]          = ISL_FORMAT_B5G5R5A1_UNORM,
      [PIPE_FORMAT_B4G4R4A4_UNORM]          = ISL_FORMAT_B4G4R4A4_UNORM,
      [PIPE_FORMAT_B5G6R5_UNORM]            = ISL_FORMAT_B5G6R5_UNORM,
      [PIPE_FORMAT_R10G10B10A2_UNORM]       = ISL_FORMAT_R10G10B10A2_UNORM,

      [PIPE_FORMAT_Z16_UNORM]               = ISL_FORMAT_R16_UNORM,
      [PIPE_FORMAT_Z32_UNORM]               = ISL_FORMAT_R32_UNORM,
      [PIPE_FORMAT_Z32_FLOAT]               = ISL_FORMAT_R32_FLOAT,

      /* We translate the combined depth/stencil formats to depth only here */
      [PIPE_FORMAT_Z24_UNORM_S8_UINT]       = ISL_FORMAT_R24_UNORM_X8_TYPELESS,
      [PIPE_FORMAT_Z24X8_UNORM]             = ISL_FORMAT_R24_UNORM_X8_TYPELESS,
      [PIPE_FORMAT_Z32_FLOAT_S8X24_UINT]    = ISL_FORMAT_R32_FLOAT,

      [PIPE_FORMAT_S8_UINT]                 = ISL_FORMAT_R8_UINT,
      [PIPE_FORMAT_X24S8_UINT]              = ISL_FORMAT_R8_UINT,
      [PIPE_FORMAT_X32_S8X24_UINT]          = ISL_FORMAT_R8_UINT,

      [PIPE_FORMAT_R64_FLOAT]               = ISL_FORMAT_R64_FLOAT,
      [PIPE_FORMAT_R64G64_FLOAT]            = ISL_FORMAT_R64G64_FLOAT,
      [PIPE_FORMAT_R64G64B64_FLOAT]         = ISL_FORMAT_R64G64B64_FLOAT,
      [PIPE_FORMAT_R64G64B64A64_FLOAT]      = ISL_FORMAT_R64G64B64A64_FLOAT,
      [PIPE_FORMAT_R32_FLOAT]               = ISL_FORMAT_R32_FLOAT,
      [PIPE_FORMAT_R32G32_FLOAT]            = ISL_FORMAT_R32G32_FLOAT,
      [PIPE_FORMAT_R32G32B32_FLOAT]         = ISL_FORMAT_R32G32B32_FLOAT,
      [PIPE_FORMAT_R32G32B32A32_FLOAT]      = ISL_FORMAT_R32G32B32A32_FLOAT,
      [PIPE_FORMAT_R32_UNORM]               = ISL_FORMAT_R32_UNORM,
      [PIPE_FORMAT_R32G32_UNORM]            = ISL_FORMAT_R32G32_UNORM,
      [PIPE_FORMAT_R32G32B32_UNORM]         = ISL_FORMAT_R32G32B32_UNORM,
      [PIPE_FORMAT_R32G32B32A32_UNORM]      = ISL_FORMAT_R32G32B32A32_UNORM,
      [PIPE_FORMAT_R32_USCALED]             = ISL_FORMAT_R32_USCALED,
      [PIPE_FORMAT_R32G32_USCALED]          = ISL_FORMAT_R32G32_USCALED,
      [PIPE_FORMAT_R32G32B32_USCALED]       = ISL_FORMAT_R32G32B32_USCALED,
      [PIPE_FORMAT_R32G32B32A32_USCALED]    = ISL_FORMAT_R32G32B32A32_USCALED,
      [PIPE_FORMAT_R32_SNORM]               = ISL_FORMAT_R32_SNORM,
      [PIPE_FORMAT_R32G32_SNORM]            = ISL_FORMAT_R32G32_SNORM,
      [PIPE_FORMAT_R32G32B32_SNORM]         = ISL_FORMAT_R32G32B32_SNORM,
      [PIPE_FORMAT_R32G32B32A32_SNORM]      = ISL_FORMAT_R32G32B32A32_SNORM,
      [PIPE_FORMAT_R32_SSCALED]             = ISL_FORMAT_R32_SSCALED,
      [PIPE_FORMAT_R32G32_SSCALED]          = ISL_FORMAT_R32G32_SSCALED,
      [PIPE_FORMAT_R32G32B32_SSCALED]       = ISL_FORMAT_R32G32B32_SSCALED,
      [PIPE_FORMAT_R32G32B32A32_SSCALED]    = ISL_FORMAT_R32G32B32A32_SSCALED,
      [PIPE_FORMAT_R16_UNORM]               = ISL_FORMAT_R16_UNORM,
      [PIPE_FORMAT_R16G16_UNORM]            = ISL_FORMAT_R16G16_UNORM,
      [PIPE_FORMAT_R16G16B16_UNORM]         = ISL_FORMAT_R16G16B16_UNORM,
      [PIPE_FORMAT_R16G16B16A16_UNORM]      = ISL_FORMAT_R16G16B16A16_UNORM,
      [PIPE_FORMAT_R16_USCALED]             = ISL_FORMAT_R16_USCALED,
      [PIPE_FORMAT_R16G16_USCALED]          = ISL_FORMAT_R16G16_USCALED,
      [PIPE_FORMAT_R16G16B16_USCALED]       = ISL_FORMAT_R16G16B16_USCALED,
      [PIPE_FORMAT_R16G16B16A16_USCALED]    = ISL_FORMAT_R16G16B16A16_USCALED,
      [PIPE_FORMAT_R16_SNORM]               = ISL_FORMAT_R16_SNORM,
      [PIPE_FORMAT_R16G16_SNORM]            = ISL_FORMAT_R16G16_SNORM,
      [PIPE_FORMAT_R16G16B16_SNORM]         = ISL_FORMAT_R16G16B16_SNORM,
      [PIPE_FORMAT_R16G16B16A16_SNORM]      = ISL_FORMAT_R16G16B16A16_SNORM,
      [PIPE_FORMAT_R16_SSCALED]             = ISL_FORMAT_R16_SSCALED,
      [PIPE_FORMAT_R16G16_SSCALED]          = ISL_FORMAT_R16G16_SSCALED,
      [PIPE_FORMAT_R16G16B16_SSCALED]       = ISL_FORMAT_R16G16B16_SSCALED,
      [PIPE_FORMAT_R16G16B16A16_SSCALED]    = ISL_FORMAT_R16G16B16A16_SSCALED,
      [PIPE_FORMAT_R8_UNORM]                = ISL_FORMAT_R8_UNORM,
      [PIPE_FORMAT_R8G8_UNORM]              = ISL_FORMAT_R8G8_UNORM,
      [PIPE_FORMAT_R8G8B8_UNORM]            = ISL_FORMAT_R8G8B8_UNORM,
      [PIPE_FORMAT_R8G8B8A8_UNORM]          = ISL_FORMAT_R8G8B8A8_UNORM,
      [PIPE_FORMAT_R8_USCALED]              = ISL_FORMAT_R8_USCALED,
      [PIPE_FORMAT_R8G8_USCALED]            = ISL_FORMAT_R8G8_USCALED,
      [PIPE_FORMAT_R8G8B8_USCALED]          = ISL_FORMAT_R8G8B8_USCALED,
      [PIPE_FORMAT_R8G8B8A8_USCALED]        = ISL_FORMAT_R8G8B8A8_USCALED,
      [PIPE_FORMAT_R8_SNORM]                = ISL_FORMAT_R8_SNORM,
      [PIPE_FORMAT_R8G8_SNORM]              = ISL_FORMAT_R8G8_SNORM,
      [PIPE_FORMAT_R8G8B8_SNORM]            = ISL_FORMAT_R8G8B8_SNORM,
      [PIPE_FORMAT_R8G8B8A8_SNORM]          = ISL_FORMAT_R8G8B8A8_SNORM,
      [PIPE_FORMAT_R8_SSCALED]              = ISL_FORMAT_R8_SSCALED,
      [PIPE_FORMAT_R8G8_SSCALED]            = ISL_FORMAT_R8G8_SSCALED,
      [PIPE_FORMAT_R8G8B8_SSCALED]          = ISL_FORMAT_R8G8B8_SSCALED,
      [PIPE_FORMAT_R8G8B8A8_SSCALED]        = ISL_FORMAT_R8G8B8A8_SSCALED,
      [PIPE_FORMAT_R32_FIXED]               = ISL_FORMAT_R32_SFIXED,
      [PIPE_FORMAT_R32G32_FIXED]            = ISL_FORMAT_R32G32_SFIXED,
      [PIPE_FORMAT_R32G32B32_FIXED]         = ISL_FORMAT_R32G32B32_SFIXED,
      [PIPE_FORMAT_R32G32B32A32_FIXED]      = ISL_FORMAT_R32G32B32A32_SFIXED,
      [PIPE_FORMAT_R16_FLOAT]               = ISL_FORMAT_R16_FLOAT,
      [PIPE_FORMAT_R16G16_FLOAT]            = ISL_FORMAT_R16G16_FLOAT,
      [PIPE_FORMAT_R16G16B16_FLOAT]         = ISL_FORMAT_R16G16B16_FLOAT,
      [PIPE_FORMAT_R16G16B16A16_FLOAT]      = ISL_FORMAT_R16G16B16A16_FLOAT,

      [PIPE_FORMAT_R8G8B8_SRGB]             = ISL_FORMAT_R8G8B8_UNORM_SRGB,
      [PIPE_FORMAT_B8G8R8A8_SRGB]           = ISL_FORMAT_B8G8R8A8_UNORM_SRGB,
      [PIPE_FORMAT_B8G8R8X8_SRGB]           = ISL_FORMAT_B8G8R8X8_UNORM_SRGB,
      [PIPE_FORMAT_R8G8B8A8_SRGB]           = ISL_FORMAT_R8G8B8A8_UNORM_SRGB,

      [PIPE_FORMAT_DXT1_RGB]                = ISL_FORMAT_BC1_UNORM,
      [PIPE_FORMAT_DXT1_RGBA]               = ISL_FORMAT_BC1_UNORM,
      [PIPE_FORMAT_DXT3_RGBA]               = ISL_FORMAT_BC2_UNORM,
      [PIPE_FORMAT_DXT5_RGBA]               = ISL_FORMAT_BC3_UNORM,

      [PIPE_FORMAT_DXT1_SRGB]               = ISL_FORMAT_BC1_UNORM_SRGB,
      [PIPE_FORMAT_DXT1_SRGBA]              = ISL_FORMAT_BC1_UNORM_SRGB,
      [PIPE_FORMAT_DXT3_SRGBA]              = ISL_FORMAT_BC2_UNORM_SRGB,
      [PIPE_FORMAT_DXT5_SRGBA]              = ISL_FORMAT_BC3_UNORM_SRGB,

      [PIPE_FORMAT_RGTC1_UNORM]             = ISL_FORMAT_BC4_UNORM,
      [PIPE_FORMAT_RGTC1_SNORM]             = ISL_FORMAT_BC4_SNORM,
      [PIPE_FORMAT_RGTC2_UNORM]             = ISL_FORMAT_BC5_UNORM,
      [PIPE_FORMAT_RGTC2_SNORM]             = ISL_FORMAT_BC5_SNORM,

      [PIPE_FORMAT_R10G10B10A2_USCALED]     = ISL_FORMAT_R10G10B10A2_USCALED,
      [PIPE_FORMAT_R11G11B10_FLOAT]         = ISL_FORMAT_R11G11B10_FLOAT,
      [PIPE_FORMAT_R9G9B9E5_FLOAT]          = ISL_FORMAT_R9G9B9E5_SHAREDEXP,
      [PIPE_FORMAT_R1_UNORM]                = ISL_FORMAT_R1_UNORM,
      [PIPE_FORMAT_R10G10B10X2_USCALED]     = ISL_FORMAT_R10G10B10X2_USCALED,
      [PIPE_FORMAT_B10G10R10A2_UNORM]       = ISL_FORMAT_B10G10R10A2_UNORM,
      [PIPE_FORMAT_R8G8B8X8_UNORM]          = ISL_FORMAT_R8G8B8X8_UNORM,

      /* Just use red formats for these - they're actually renderable,
       * and faster to sample than the legacy L/I/A/LA formats.
       */
      [PIPE_FORMAT_I8_UNORM]                = ISL_FORMAT_R8_UNORM,
      [PIPE_FORMAT_I8_UINT]                 = ISL_FORMAT_R8_UINT,
      [PIPE_FORMAT_I8_SINT]                 = ISL_FORMAT_R8_SINT,
      [PIPE_FORMAT_I8_SNORM]                = ISL_FORMAT_R8_SNORM,
      [PIPE_FORMAT_I16_UINT]                = ISL_FORMAT_R16_UINT,
      [PIPE_FORMAT_I16_UNORM]               = ISL_FORMAT_R16_UNORM,
      [PIPE_FORMAT_I16_SINT]                = ISL_FORMAT_R16_SINT,
      [PIPE_FORMAT_I16_SNORM]               = ISL_FORMAT_R16_SNORM,
      [PIPE_FORMAT_I16_FLOAT]               = ISL_FORMAT_R16_FLOAT,
      [PIPE_FORMAT_I32_UINT]                = ISL_FORMAT_R32_UINT,
      [PIPE_FORMAT_I32_SINT]                = ISL_FORMAT_R32_SINT,
      [PIPE_FORMAT_I32_FLOAT]               = ISL_FORMAT_R32_FLOAT,

      [PIPE_FORMAT_L8_UINT]                 = ISL_FORMAT_R8_UINT,
      [PIPE_FORMAT_L8_UNORM]                = ISL_FORMAT_R8_UNORM,
      [PIPE_FORMAT_L8_SINT]                 = ISL_FORMAT_R8_SINT,
      [PIPE_FORMAT_L8_SNORM]                = ISL_FORMAT_R8_SNORM,
      [PIPE_FORMAT_L16_UINT]                = ISL_FORMAT_R16_UINT,
      [PIPE_FORMAT_L16_UNORM]               = ISL_FORMAT_R16_UNORM,
      [PIPE_FORMAT_L16_SINT]                = ISL_FORMAT_R16_SINT,
      [PIPE_FORMAT_L16_SNORM]               = ISL_FORMAT_R16_SNORM,
      [PIPE_FORMAT_L16_FLOAT]               = ISL_FORMAT_R16_FLOAT,
      [PIPE_FORMAT_L32_UINT]                = ISL_FORMAT_R32_UINT,
      [PIPE_FORMAT_L32_SINT]                = ISL_FORMAT_R32_SINT,
      [PIPE_FORMAT_L32_FLOAT]               = ISL_FORMAT_R32_FLOAT,

      /* We also map alpha and luminance-alpha formats to red as well,
       * though most of these (other than A8_UNORM) will be non-renderable.
       */
      [PIPE_FORMAT_A8_UINT]                 = ISL_FORMAT_R8_UINT,
      [PIPE_FORMAT_A8_UNORM]                = ISL_FORMAT_R8_UNORM,
      [PIPE_FORMAT_A8_SINT]                 = ISL_FORMAT_R8_SINT,
      [PIPE_FORMAT_A8_SNORM]                = ISL_FORMAT_R8_SNORM,
      [PIPE_FORMAT_A16_UINT]                = ISL_FORMAT_R16_UINT,
      [PIPE_FORMAT_A16_UNORM]               = ISL_FORMAT_R16_UNORM,
      [PIPE_FORMAT_A16_SINT]                = ISL_FORMAT_R16_SINT,
      [PIPE_FORMAT_A16_SNORM]               = ISL_FORMAT_R16_SNORM,
      [PIPE_FORMAT_A16_FLOAT]               = ISL_FORMAT_R16_FLOAT,
      [PIPE_FORMAT_A32_UINT]                = ISL_FORMAT_R32_UINT,
      [PIPE_FORMAT_A32_SINT]                = ISL_FORMAT_R32_SINT,
      [PIPE_FORMAT_A32_FLOAT]               = ISL_FORMAT_R32_FLOAT,

      [PIPE_FORMAT_L8A8_UINT]               = ISL_FORMAT_R8G8_UINT,
      [PIPE_FORMAT_L8A8_UNORM]              = ISL_FORMAT_R8G8_UNORM,
      [PIPE_FORMAT_L8A8_SINT]               = ISL_FORMAT_R8G8_SINT,
      [PIPE_FORMAT_L8A8_SNORM]              = ISL_FORMAT_R8G8_SNORM,
      [PIPE_FORMAT_L16A16_UINT]             = ISL_FORMAT_R16G16_UINT,
      [PIPE_FORMAT_L16A16_UNORM]            = ISL_FORMAT_R16G16_UNORM,
      [PIPE_FORMAT_L16A16_SINT]             = ISL_FORMAT_R16G16_SINT,
      [PIPE_FORMAT_L16A16_SNORM]            = ISL_FORMAT_R16G16_SNORM,
      [PIPE_FORMAT_L16A16_FLOAT]            = ISL_FORMAT_R16G16_FLOAT,
      [PIPE_FORMAT_L32A32_UINT]             = ISL_FORMAT_R32G32_UINT,
      [PIPE_FORMAT_L32A32_SINT]             = ISL_FORMAT_R32G32_SINT,
      [PIPE_FORMAT_L32A32_FLOAT]            = ISL_FORMAT_R32G32_FLOAT,

      /* Sadly, we have to use luminance[-alpha] formats for sRGB decoding. */
      [PIPE_FORMAT_R8_SRGB]                 = ISL_FORMAT_L8_UNORM_SRGB,
      [PIPE_FORMAT_L8_SRGB]                 = ISL_FORMAT_L8_UNORM_SRGB,
      [PIPE_FORMAT_L8A8_SRGB]               = ISL_FORMAT_L8A8_UNORM_SRGB,

      [PIPE_FORMAT_R10G10B10A2_SSCALED]     = ISL_FORMAT_R10G10B10A2_SSCALED,
      [PIPE_FORMAT_R10G10B10A2_SNORM]       = ISL_FORMAT_R10G10B10A2_SNORM,

      [PIPE_FORMAT_B10G10R10A2_USCALED]     = ISL_FORMAT_B10G10R10A2_USCALED,
      [PIPE_FORMAT_B10G10R10A2_SSCALED]     = ISL_FORMAT_B10G10R10A2_SSCALED,
      [PIPE_FORMAT_B10G10R10A2_SNORM]       = ISL_FORMAT_B10G10R10A2_SNORM,

      [PIPE_FORMAT_R8_UINT]                 = ISL_FORMAT_R8_UINT,
      [PIPE_FORMAT_R8G8_UINT]               = ISL_FORMAT_R8G8_UINT,
      [PIPE_FORMAT_R8G8B8_UINT]             = ISL_FORMAT_R8G8B8_UINT,
      [PIPE_FORMAT_R8G8B8A8_UINT]           = ISL_FORMAT_R8G8B8A8_UINT,

      [PIPE_FORMAT_R8_SINT]                 = ISL_FORMAT_R8_SINT,
      [PIPE_FORMAT_R8G8_SINT]               = ISL_FORMAT_R8G8_SINT,
      [PIPE_FORMAT_R8G8B8_SINT]             = ISL_FORMAT_R8G8B8_SINT,
      [PIPE_FORMAT_R8G8B8A8_SINT]           = ISL_FORMAT_R8G8B8A8_SINT,

      [PIPE_FORMAT_R16_UINT]                = ISL_FORMAT_R16_UINT,
      [PIPE_FORMAT_R16G16_UINT]             = ISL_FORMAT_R16G16_UINT,
      [PIPE_FORMAT_R16G16B16_UINT]          = ISL_FORMAT_R16G16B16_UINT,
      [PIPE_FORMAT_R16G16B16A16_UINT]       = ISL_FORMAT_R16G16B16A16_UINT,

      [PIPE_FORMAT_R16_SINT]                = ISL_FORMAT_R16_SINT,
      [PIPE_FORMAT_R16G16_SINT]             = ISL_FORMAT_R16G16_SINT,
      [PIPE_FORMAT_R16G16B16_SINT]          = ISL_FORMAT_R16G16B16_SINT,
      [PIPE_FORMAT_R16G16B16A16_SINT]       = ISL_FORMAT_R16G16B16A16_SINT,

      [PIPE_FORMAT_R32_UINT]                = ISL_FORMAT_R32_UINT,
      [PIPE_FORMAT_R32G32_UINT]             = ISL_FORMAT_R32G32_UINT,
      [PIPE_FORMAT_R32G32B32_UINT]          = ISL_FORMAT_R32G32B32_UINT,
      [PIPE_FORMAT_R32G32B32A32_UINT]       = ISL_FORMAT_R32G32B32A32_UINT,

      [PIPE_FORMAT_R32_SINT]                = ISL_FORMAT_R32_SINT,
      [PIPE_FORMAT_R32G32_SINT]             = ISL_FORMAT_R32G32_SINT,
      [PIPE_FORMAT_R32G32B32_SINT]          = ISL_FORMAT_R32G32B32_SINT,
      [PIPE_FORMAT_R32G32B32A32_SINT]       = ISL_FORMAT_R32G32B32A32_SINT,

      [PIPE_FORMAT_B10G10R10A2_UINT]        = ISL_FORMAT_B10G10R10A2_UINT,

      [PIPE_FORMAT_ETC1_RGB8]               = ISL_FORMAT_ETC1_RGB8,

      [PIPE_FORMAT_R8G8B8X8_SRGB]           = ISL_FORMAT_R8G8B8X8_UNORM_SRGB,
      [PIPE_FORMAT_B10G10R10X2_UNORM]       = ISL_FORMAT_B10G10R10X2_UNORM,
      [PIPE_FORMAT_R16G16B16X16_UNORM]      = ISL_FORMAT_R16G16B16X16_UNORM,
      [PIPE_FORMAT_R16G16B16X16_FLOAT]      = ISL_FORMAT_R16G16B16X16_FLOAT,
      [PIPE_FORMAT_R32G32B32X32_FLOAT]      = ISL_FORMAT_R32G32B32X32_FLOAT,

      [PIPE_FORMAT_R10G10B10A2_UINT]        = ISL_FORMAT_R10G10B10A2_UINT,

      [PIPE_FORMAT_B5G6R5_SRGB]             = ISL_FORMAT_B5G6R5_UNORM_SRGB,

      [PIPE_FORMAT_BPTC_RGBA_UNORM]         = ISL_FORMAT_BC7_UNORM,
      [PIPE_FORMAT_BPTC_SRGBA]              = ISL_FORMAT_BC7_UNORM_SRGB,
      [PIPE_FORMAT_BPTC_RGB_FLOAT]          = ISL_FORMAT_BC6H_SF16,
      [PIPE_FORMAT_BPTC_RGB_UFLOAT]         = ISL_FORMAT_BC6H_UF16,

      [PIPE_FORMAT_ETC2_RGB8]               = ISL_FORMAT_ETC2_RGB8,
      [PIPE_FORMAT_ETC2_SRGB8]              = ISL_FORMAT_ETC2_SRGB8,
      [PIPE_FORMAT_ETC2_RGB8A1]             = ISL_FORMAT_ETC2_RGB8_PTA,
      [PIPE_FORMAT_ETC2_SRGB8A1]            = ISL_FORMAT_ETC2_SRGB8_PTA,
      [PIPE_FORMAT_ETC2_RGBA8]              = ISL_FORMAT_ETC2_EAC_RGBA8,
      [PIPE_FORMAT_ETC2_SRGBA8]             = ISL_FORMAT_ETC2_EAC_SRGB8_A8,
      [PIPE_FORMAT_ETC2_R11_UNORM]          = ISL_FORMAT_EAC_R11,
      [PIPE_FORMAT_ETC2_R11_SNORM]          = ISL_FORMAT_EAC_SIGNED_R11,
      [PIPE_FORMAT_ETC2_RG11_UNORM]         = ISL_FORMAT_EAC_RG11,
      [PIPE_FORMAT_ETC2_RG11_SNORM]         = ISL_FORMAT_EAC_SIGNED_RG11,


      [PIPE_FORMAT_ASTC_4x4]                = ISL_FORMAT_ASTC_LDR_2D_4X4_FLT16,
      [PIPE_FORMAT_ASTC_5x4]                = ISL_FORMAT_ASTC_LDR_2D_5X4_FLT16,
      [PIPE_FORMAT_ASTC_5x5]                = ISL_FORMAT_ASTC_LDR_2D_5X5_FLT16,
      [PIPE_FORMAT_ASTC_6x5]                = ISL_FORMAT_ASTC_LDR_2D_6X5_FLT16,
      [PIPE_FORMAT_ASTC_6x6]                = ISL_FORMAT_ASTC_LDR_2D_6X6_FLT16,
      [PIPE_FORMAT_ASTC_8x5]                = ISL_FORMAT_ASTC_LDR_2D_8X5_FLT16,
      [PIPE_FORMAT_ASTC_8x6]                = ISL_FORMAT_ASTC_LDR_2D_8X6_FLT16,
      [PIPE_FORMAT_ASTC_8x8]                = ISL_FORMAT_ASTC_LDR_2D_8X8_FLT16,
      [PIPE_FORMAT_ASTC_10x5]               = ISL_FORMAT_ASTC_LDR_2D_10X5_FLT16,
      [PIPE_FORMAT_ASTC_10x6]               = ISL_FORMAT_ASTC_LDR_2D_10X6_FLT16,
      [PIPE_FORMAT_ASTC_10x8]               = ISL_FORMAT_ASTC_LDR_2D_10X8_FLT16,
      [PIPE_FORMAT_ASTC_10x10]              = ISL_FORMAT_ASTC_LDR_2D_10X10_FLT16,
      [PIPE_FORMAT_ASTC_12x10]              = ISL_FORMAT_ASTC_LDR_2D_12X10_FLT16,
      [PIPE_FORMAT_ASTC_12x12]              = ISL_FORMAT_ASTC_LDR_2D_12X12_FLT16,

      [PIPE_FORMAT_ASTC_4x4_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_4X4_U8SRGB,
      [PIPE_FORMAT_ASTC_5x4_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_5X4_U8SRGB,
      [PIPE_FORMAT_ASTC_5x5_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_5X5_U8SRGB,
      [PIPE_FORMAT_ASTC_6x5_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_6X5_U8SRGB,
      [PIPE_FORMAT_ASTC_6x6_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_6X6_U8SRGB,
      [PIPE_FORMAT_ASTC_8x5_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_8X5_U8SRGB,
      [PIPE_FORMAT_ASTC_8x6_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_8X6_U8SRGB,
      [PIPE_FORMAT_ASTC_8x8_SRGB]           = ISL_FORMAT_ASTC_LDR_2D_8X8_U8SRGB,
      [PIPE_FORMAT_ASTC_10x5_SRGB]          = ISL_FORMAT_ASTC_LDR_2D_10X5_U8SRGB,
      [PIPE_FORMAT_ASTC_10x6_SRGB]          = ISL_FORMAT_ASTC_LDR_2D_10X6_U8SRGB,
      [PIPE_FORMAT_ASTC_10x8_SRGB]          = ISL_FORMAT_ASTC_LDR_2D_10X8_U8SRGB,
      [PIPE_FORMAT_ASTC_10x10_SRGB]         = ISL_FORMAT_ASTC_LDR_2D_10X10_U8SRGB,
      [PIPE_FORMAT_ASTC_12x10_SRGB]         = ISL_FORMAT_ASTC_LDR_2D_12X10_U8SRGB,
      [PIPE_FORMAT_ASTC_12x12_SRGB]         = ISL_FORMAT_ASTC_LDR_2D_12X12_U8SRGB,

      [PIPE_FORMAT_A1B5G5R5_UNORM]          = ISL_FORMAT_A1B5G5R5_UNORM,
   };
   assert(pf < PIPE_FORMAT_COUNT);
   return table[pf];
}

struct iris_format_info
iris_format_for_usage(const struct gen_device_info *devinfo,
                      enum pipe_format pformat,
                      isl_surf_usage_flags_t usage)
{
   enum isl_format format = iris_isl_format_for_pipe_format(pformat);
   struct isl_swizzle swizzle = ISL_SWIZZLE_IDENTITY;

   if (!util_format_is_srgb(pformat)) {
      if (util_format_is_intensity(pformat)) {
         swizzle = ISL_SWIZZLE(RED, RED, RED, RED);
      } else if (util_format_is_luminance(pformat)) {
         swizzle = ISL_SWIZZLE(RED, RED, RED, ONE);
      } else if (util_format_is_luminance_alpha(pformat)) {
         swizzle = ISL_SWIZZLE(RED, RED, RED, GREEN);
      } else if (util_format_is_alpha(pformat)) {
         swizzle = ISL_SWIZZLE(ZERO, ZERO, ZERO, RED);
      }
   }

   if (pformat == PIPE_FORMAT_DXT1_RGB ||
       pformat == PIPE_FORMAT_DXT1_SRGB) {
      swizzle = ISL_SWIZZLE(RED, GREEN, BLUE, ONE);
   }

   if ((usage & ISL_SURF_USAGE_RENDER_TARGET_BIT) &&
       pformat == PIPE_FORMAT_A8_UNORM) {
      /* Most of the hardware A/LA formats are not renderable, except
       * for A8_UNORM.  SURFACE_STATE's shader channel select fields
       * cannot be used to swap RGB and A channels when rendering (as
       * it could impact alpha blending), so we have to use the actual
       * A8_UNORM format when rendering.
       */
      format = ISL_FORMAT_A8_UNORM;
      swizzle = ISL_SWIZZLE_IDENTITY;
   }

   /* We choose RGBA over RGBX for rendering the hardware doesn't support
    * rendering to RGBX. However, when this internal override is used on Gen9+,
    * fast clears don't work correctly.
    *
    * i965 fixes this by pretending to not support RGBX formats, and the higher
    * layers of Mesa pick the RGBA format instead. Gallium doesn't work that
    * way, and might choose a different format, like BGRX instead of RGBX,
    * which will also cause problems when sampling from a surface fast cleared
    * as RGBX. So we always choose RGBA instead of RGBX explicitly
    * here.
    */
   if (isl_format_is_rgbx(format) &&
       !isl_format_supports_rendering(devinfo, format)) {
      format = isl_format_rgbx_to_rgba(format);
      swizzle = ISL_SWIZZLE(RED, GREEN, BLUE, ONE);
   }

   return (struct iris_format_info) { .fmt = format, .swizzle = swizzle };
}

/**
 * The pscreen->is_format_supported() driver hook.
 *
 * Returns true if the given format is supported for the given usage
 * (PIPE_BIND_*) and sample count.
 */
boolean
iris_is_format_supported(struct pipe_screen *pscreen,
                         enum pipe_format pformat,
                         enum pipe_texture_target target,
                         unsigned sample_count,
                         unsigned storage_sample_count,
                         unsigned usage)
{
   struct iris_screen *screen = (struct iris_screen *) pscreen;
   const struct gen_device_info *devinfo = &screen->devinfo;
   uint32_t max_samples = devinfo->gen == 8 ? 8 : 16;

   // XXX: msaa max
   if (sample_count > max_samples || !util_is_power_of_two_or_zero(sample_count))
      return false;

   if (pformat == PIPE_FORMAT_NONE)
      return true;

   enum isl_format format = iris_isl_format_for_pipe_format(pformat);

   if (format == ISL_FORMAT_UNSUPPORTED)
      return false;

   const struct isl_format_layout *fmtl = isl_format_get_layout(format);
   const bool is_integer = isl_format_has_int_channel(format);
   bool supported = true;

   if (sample_count > 1)
      supported &= isl_format_supports_multisampling(devinfo, format);

   if (usage & PIPE_BIND_DEPTH_STENCIL) {
      supported &= format == ISL_FORMAT_R32_FLOAT_X8X24_TYPELESS ||
                   format == ISL_FORMAT_R32_FLOAT ||
                   format == ISL_FORMAT_R24_UNORM_X8_TYPELESS ||
                   format == ISL_FORMAT_R16_UNORM ||
                   format == ISL_FORMAT_R8_UINT;
   }

   if (usage & PIPE_BIND_RENDER_TARGET) {
      /* Alpha and luminance-alpha formats other than A8_UNORM are not
       * renderable.  For texturing, we can use R or RG formats with
       * shader channel selects (SCS) to swizzle the data into the correct
       * channels.  But for render targets, the hardware prohibits using
       * SCS to move shader outputs between the RGB and A channels, as it
       * would alter what data is used for alpha blending.
       *
       * For BLORP, we can apply the swizzle in the shader.  But for
       * general rendering, this would mean recompiling the shader, which
       * we'd like to avoid doing.  So we mark these formats non-renderable.
       *
       * We do support A8_UNORM as it's required and is renderable.
       */
      if (pformat != PIPE_FORMAT_A8_UNORM &&
          (util_format_is_alpha(pformat) ||
           util_format_is_luminance_alpha(pformat)))
         supported = false;

      enum isl_format rt_format = format;

      if (isl_format_is_rgbx(format) &&
          !isl_format_supports_rendering(devinfo, format))
         rt_format = isl_format_rgbx_to_rgba(format);

      supported &= isl_format_supports_rendering(devinfo, rt_format);

      if (!is_integer)
         supported &= isl_format_supports_alpha_blending(devinfo, rt_format);
   }

   if (usage & PIPE_BIND_SHADER_IMAGE) {
      /* Dataport doesn't support compression, and we can't resolve an MCS
       * compressed surface.  (Buffer images may have sample count of 0.)
       */
      supported &= sample_count <= 1;

      // XXX: allow untyped reads
      supported &= isl_format_supports_typed_reads(devinfo, format) &&
                   isl_format_supports_typed_writes(devinfo, format);
   }

   if (usage & PIPE_BIND_SAMPLER_VIEW) {
      supported &= isl_format_supports_sampling(devinfo, format);
      if (!is_integer)
         supported &= isl_format_supports_filtering(devinfo, format);

      /* Don't advertise 3-component RGB formats.  This ensures that they
       * are renderable from an API perspective since the state tracker will
       * fall back to RGBA or RGBX, which are renderable.  We want to render
       * internally for copies and blits, even if the application doesn't.
       *
       * We do need to advertise 32-bit RGB for texture buffers though.
       */
      supported &= fmtl->bpb != 24 && fmtl->bpb != 48 &&
                   (fmtl->bpb != 96 || target == PIPE_BUFFER);
   }

   if (usage & PIPE_BIND_VERTEX_BUFFER)
      supported &= isl_format_supports_vertex_fetch(devinfo, format);

   if (usage & PIPE_BIND_INDEX_BUFFER) {
      supported &= format == ISL_FORMAT_R8_UINT ||
                   format == ISL_FORMAT_R16_UINT ||
                   format == ISL_FORMAT_R32_UINT;
   }

   if (usage & PIPE_BIND_CONSTANT_BUFFER) {
      // XXX:
   }

   if (usage & PIPE_BIND_STREAM_OUTPUT) {
      // XXX:
   }

   if (usage & PIPE_BIND_CURSOR) {
      // XXX:
   }

   if (usage & PIPE_BIND_CUSTOM) {
      // XXX:
   }

   if (usage & PIPE_BIND_SHADER_BUFFER) {
      // XXX:
   }

   if (usage & PIPE_BIND_COMPUTE_RESOURCE) {
      // XXX:
   }

   if (usage & PIPE_BIND_COMMAND_ARGS_BUFFER) {
      // XXX:
   }

   if (usage & PIPE_BIND_QUERY_BUFFER) {
      // XXX:
   }

   return supported;
}

