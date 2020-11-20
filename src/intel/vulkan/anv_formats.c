/*
 * Copyright © 2015 Intel Corporation
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

#include "anv_private.h"
#include "drm-uapi/drm_fourcc.h"
#include "vk_enum_to_str.h"
#include "vk_format_info.h"
#include "vk_util.h"

/*
 * gcc-4 and earlier don't allow compound literals where a constant
 * is required in -std=c99/gnu99 mode, so we can't use ISL_SWIZZLE()
 * here. -std=c89/gnu89 would allow it, but we depend on c99 features
 * so using -std=c89/gnu89 is not an option. Starting from gcc-5
 * compound literals can also be considered constant in -std=c99/gnu99
 * mode.
 */
#define _ISL_SWIZZLE(r, g, b, a) { \
      ISL_CHANNEL_SELECT_##r, \
      ISL_CHANNEL_SELECT_##g, \
      ISL_CHANNEL_SELECT_##b, \
      ISL_CHANNEL_SELECT_##a, \
}

#define RGBA _ISL_SWIZZLE(RED, GREEN, BLUE, ALPHA)
#define BGRA _ISL_SWIZZLE(BLUE, GREEN, RED, ALPHA)
#define RGB1 _ISL_SWIZZLE(RED, GREEN, BLUE, ONE)

#define swiz_fmt1(__vk_fmt, __hw_fmt, __swizzle) \
   [VK_ENUM_OFFSET(__vk_fmt)] = { \
      .planes = { \
         { .isl_format = __hw_fmt, .swizzle = __swizzle, \
           .denominator_scales = { 1, 1, }, \
           .aspect = VK_IMAGE_ASPECT_COLOR_BIT, \
         }, \
      }, \
      .vk_format = __vk_fmt, \
      .n_planes = 1, \
   }

#define fmt1(__vk_fmt, __hw_fmt) \
   swiz_fmt1(__vk_fmt, __hw_fmt, RGBA)

#define d_fmt(__vk_fmt, __hw_fmt) \
   [VK_ENUM_OFFSET(__vk_fmt)] = { \
      .planes = { \
         { .isl_format = __hw_fmt, .swizzle = RGBA, \
           .denominator_scales = { 1, 1, }, \
           .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, \
         }, \
      }, \
      .vk_format = __vk_fmt, \
      .n_planes = 1, \
   }

#define s_fmt(__vk_fmt, __hw_fmt) \
   [VK_ENUM_OFFSET(__vk_fmt)] = { \
      .planes = { \
         { .isl_format = __hw_fmt, .swizzle = RGBA, \
           .denominator_scales = { 1, 1, }, \
           .aspect = VK_IMAGE_ASPECT_STENCIL_BIT, \
         }, \
      }, \
      .vk_format = __vk_fmt, \
      .n_planes = 1, \
   }

#define ds_fmt2(__vk_fmt, __fmt1, __fmt2) \
   [VK_ENUM_OFFSET(__vk_fmt)] = { \
      .planes = { \
         { .isl_format = __fmt1, .swizzle = RGBA, \
           .denominator_scales = { 1, 1, }, \
           .aspect = VK_IMAGE_ASPECT_DEPTH_BIT, \
         }, \
         { .isl_format = __fmt2, .swizzle = RGBA, \
           .denominator_scales = { 1, 1, }, \
           .aspect = VK_IMAGE_ASPECT_STENCIL_BIT, \
         }, \
      }, \
      .vk_format = __vk_fmt, \
      .n_planes = 2, \
   }

#define fmt_unsupported(__vk_fmt) \
   [VK_ENUM_OFFSET(__vk_fmt)] = { \
      .planes = { \
         { .isl_format = ISL_FORMAT_UNSUPPORTED, }, \
      }, \
      .vk_format = VK_FORMAT_UNDEFINED, \
   }

#define y_plane(__plane, __hw_fmt, __swizzle, __ycbcr_swizzle, dhs, dvs) \
   { .isl_format = __hw_fmt, \
     .swizzle = __swizzle, \
     .ycbcr_swizzle = __ycbcr_swizzle, \
     .denominator_scales = { dhs, dvs, }, \
     .has_chroma = false, \
     .aspect = VK_IMAGE_ASPECT_PLANE_0_BIT, /* Y plane is always plane 0 */ \
   }

#define chroma_plane(__plane, __hw_fmt, __swizzle, __ycbcr_swizzle, dhs, dvs) \
   { .isl_format = __hw_fmt, \
     .swizzle = __swizzle, \
     .ycbcr_swizzle = __ycbcr_swizzle, \
     .denominator_scales = { dhs, dvs, }, \
     .has_chroma = true, \
     .aspect = VK_IMAGE_ASPECT_PLANE_ ## __plane ## _BIT, \
   }

#define ycbcr_fmt(__vk_fmt, __n_planes, ...) \
   [VK_ENUM_OFFSET(__vk_fmt)] = { \
      .planes = { \
         __VA_ARGS__, \
      }, \
      .vk_format = __vk_fmt, \
      .n_planes = __n_planes, \
      .can_ycbcr = true, \
   }

/* HINT: For array formats, the ISL name should match the VK name.  For
 * packed formats, they should have the channels in reverse order from each
 * other.  The reason for this is that, for packed formats, the ISL (and
 * bspec) names are in LSB -> MSB order while VK formats are MSB -> LSB.
 */
static const struct anv_format main_formats[] = {
   fmt_unsupported(VK_FORMAT_UNDEFINED),
   fmt_unsupported(VK_FORMAT_R4G4_UNORM_PACK8),
   fmt1(VK_FORMAT_R4G4B4A4_UNORM_PACK16,             ISL_FORMAT_A4B4G4R4_UNORM),
   swiz_fmt1(VK_FORMAT_B4G4R4A4_UNORM_PACK16,        ISL_FORMAT_A4B4G4R4_UNORM,  BGRA),
   fmt1(VK_FORMAT_R5G6B5_UNORM_PACK16,               ISL_FORMAT_B5G6R5_UNORM),
   fmt_unsupported(VK_FORMAT_B5G6R5_UNORM_PACK16),
   fmt1(VK_FORMAT_R5G5B5A1_UNORM_PACK16,             ISL_FORMAT_A1B5G5R5_UNORM),
   fmt_unsupported(VK_FORMAT_B5G5R5A1_UNORM_PACK16),
   fmt1(VK_FORMAT_A1R5G5B5_UNORM_PACK16,             ISL_FORMAT_B5G5R5A1_UNORM),
   fmt1(VK_FORMAT_R8_UNORM,                          ISL_FORMAT_R8_UNORM),
   fmt1(VK_FORMAT_R8_SNORM,                          ISL_FORMAT_R8_SNORM),
   fmt1(VK_FORMAT_R8_USCALED,                        ISL_FORMAT_R8_USCALED),
   fmt1(VK_FORMAT_R8_SSCALED,                        ISL_FORMAT_R8_SSCALED),
   fmt1(VK_FORMAT_R8_UINT,                           ISL_FORMAT_R8_UINT),
   fmt1(VK_FORMAT_R8_SINT,                           ISL_FORMAT_R8_SINT),
   swiz_fmt1(VK_FORMAT_R8_SRGB,                      ISL_FORMAT_L8_UNORM_SRGB,
                                                     _ISL_SWIZZLE(RED, ZERO, ZERO, ONE)),
   fmt1(VK_FORMAT_R8G8_UNORM,                        ISL_FORMAT_R8G8_UNORM),
   fmt1(VK_FORMAT_R8G8_SNORM,                        ISL_FORMAT_R8G8_SNORM),
   fmt1(VK_FORMAT_R8G8_USCALED,                      ISL_FORMAT_R8G8_USCALED),
   fmt1(VK_FORMAT_R8G8_SSCALED,                      ISL_FORMAT_R8G8_SSCALED),
   fmt1(VK_FORMAT_R8G8_UINT,                         ISL_FORMAT_R8G8_UINT),
   fmt1(VK_FORMAT_R8G8_SINT,                         ISL_FORMAT_R8G8_SINT),
   fmt_unsupported(VK_FORMAT_R8G8_SRGB),             /* L8A8_UNORM_SRGB */
   fmt1(VK_FORMAT_R8G8B8_UNORM,                      ISL_FORMAT_R8G8B8_UNORM),
   fmt1(VK_FORMAT_R8G8B8_SNORM,                      ISL_FORMAT_R8G8B8_SNORM),
   fmt1(VK_FORMAT_R8G8B8_USCALED,                    ISL_FORMAT_R8G8B8_USCALED),
   fmt1(VK_FORMAT_R8G8B8_SSCALED,                    ISL_FORMAT_R8G8B8_SSCALED),
   fmt1(VK_FORMAT_R8G8B8_UINT,                       ISL_FORMAT_R8G8B8_UINT),
   fmt1(VK_FORMAT_R8G8B8_SINT,                       ISL_FORMAT_R8G8B8_SINT),
   fmt1(VK_FORMAT_R8G8B8_SRGB,                       ISL_FORMAT_R8G8B8_UNORM_SRGB),
   fmt1(VK_FORMAT_R8G8B8A8_UNORM,                    ISL_FORMAT_R8G8B8A8_UNORM),
   fmt1(VK_FORMAT_R8G8B8A8_SNORM,                    ISL_FORMAT_R8G8B8A8_SNORM),
   fmt1(VK_FORMAT_R8G8B8A8_USCALED,                  ISL_FORMAT_R8G8B8A8_USCALED),
   fmt1(VK_FORMAT_R8G8B8A8_SSCALED,                  ISL_FORMAT_R8G8B8A8_SSCALED),
   fmt1(VK_FORMAT_R8G8B8A8_UINT,                     ISL_FORMAT_R8G8B8A8_UINT),
   fmt1(VK_FORMAT_R8G8B8A8_SINT,                     ISL_FORMAT_R8G8B8A8_SINT),
   fmt1(VK_FORMAT_R8G8B8A8_SRGB,                     ISL_FORMAT_R8G8B8A8_UNORM_SRGB),
   fmt1(VK_FORMAT_A8B8G8R8_UNORM_PACK32,             ISL_FORMAT_R8G8B8A8_UNORM),
   fmt1(VK_FORMAT_A8B8G8R8_SNORM_PACK32,             ISL_FORMAT_R8G8B8A8_SNORM),
   fmt1(VK_FORMAT_A8B8G8R8_USCALED_PACK32,           ISL_FORMAT_R8G8B8A8_USCALED),
   fmt1(VK_FORMAT_A8B8G8R8_SSCALED_PACK32,           ISL_FORMAT_R8G8B8A8_SSCALED),
   fmt1(VK_FORMAT_A8B8G8R8_UINT_PACK32,              ISL_FORMAT_R8G8B8A8_UINT),
   fmt1(VK_FORMAT_A8B8G8R8_SINT_PACK32,              ISL_FORMAT_R8G8B8A8_SINT),
   fmt1(VK_FORMAT_A8B8G8R8_SRGB_PACK32,              ISL_FORMAT_R8G8B8A8_UNORM_SRGB),
   fmt1(VK_FORMAT_A2R10G10B10_UNORM_PACK32,          ISL_FORMAT_B10G10R10A2_UNORM),
   fmt1(VK_FORMAT_A2R10G10B10_SNORM_PACK32,          ISL_FORMAT_B10G10R10A2_SNORM),
   fmt1(VK_FORMAT_A2R10G10B10_USCALED_PACK32,        ISL_FORMAT_B10G10R10A2_USCALED),
   fmt1(VK_FORMAT_A2R10G10B10_SSCALED_PACK32,        ISL_FORMAT_B10G10R10A2_SSCALED),
   fmt1(VK_FORMAT_A2R10G10B10_UINT_PACK32,           ISL_FORMAT_B10G10R10A2_UINT),
   fmt1(VK_FORMAT_A2R10G10B10_SINT_PACK32,           ISL_FORMAT_B10G10R10A2_SINT),
   fmt1(VK_FORMAT_A2B10G10R10_UNORM_PACK32,          ISL_FORMAT_R10G10B10A2_UNORM),
   fmt1(VK_FORMAT_A2B10G10R10_SNORM_PACK32,          ISL_FORMAT_R10G10B10A2_SNORM),
   fmt1(VK_FORMAT_A2B10G10R10_USCALED_PACK32,        ISL_FORMAT_R10G10B10A2_USCALED),
   fmt1(VK_FORMAT_A2B10G10R10_SSCALED_PACK32,        ISL_FORMAT_R10G10B10A2_SSCALED),
   fmt1(VK_FORMAT_A2B10G10R10_UINT_PACK32,           ISL_FORMAT_R10G10B10A2_UINT),
   fmt1(VK_FORMAT_A2B10G10R10_SINT_PACK32,           ISL_FORMAT_R10G10B10A2_SINT),
   fmt1(VK_FORMAT_R16_UNORM,                         ISL_FORMAT_R16_UNORM),
   fmt1(VK_FORMAT_R16_SNORM,                         ISL_FORMAT_R16_SNORM),
   fmt1(VK_FORMAT_R16_USCALED,                       ISL_FORMAT_R16_USCALED),
   fmt1(VK_FORMAT_R16_SSCALED,                       ISL_FORMAT_R16_SSCALED),
   fmt1(VK_FORMAT_R16_UINT,                          ISL_FORMAT_R16_UINT),
   fmt1(VK_FORMAT_R16_SINT,                          ISL_FORMAT_R16_SINT),
   fmt1(VK_FORMAT_R16_SFLOAT,                        ISL_FORMAT_R16_FLOAT),
   fmt1(VK_FORMAT_R16G16_UNORM,                      ISL_FORMAT_R16G16_UNORM),
   fmt1(VK_FORMAT_R16G16_SNORM,                      ISL_FORMAT_R16G16_SNORM),
   fmt1(VK_FORMAT_R16G16_USCALED,                    ISL_FORMAT_R16G16_USCALED),
   fmt1(VK_FORMAT_R16G16_SSCALED,                    ISL_FORMAT_R16G16_SSCALED),
   fmt1(VK_FORMAT_R16G16_UINT,                       ISL_FORMAT_R16G16_UINT),
   fmt1(VK_FORMAT_R16G16_SINT,                       ISL_FORMAT_R16G16_SINT),
   fmt1(VK_FORMAT_R16G16_SFLOAT,                     ISL_FORMAT_R16G16_FLOAT),
   fmt1(VK_FORMAT_R16G16B16_UNORM,                   ISL_FORMAT_R16G16B16_UNORM),
   fmt1(VK_FORMAT_R16G16B16_SNORM,                   ISL_FORMAT_R16G16B16_SNORM),
   fmt1(VK_FORMAT_R16G16B16_USCALED,                 ISL_FORMAT_R16G16B16_USCALED),
   fmt1(VK_FORMAT_R16G16B16_SSCALED,                 ISL_FORMAT_R16G16B16_SSCALED),
   fmt1(VK_FORMAT_R16G16B16_UINT,                    ISL_FORMAT_R16G16B16_UINT),
   fmt1(VK_FORMAT_R16G16B16_SINT,                    ISL_FORMAT_R16G16B16_SINT),
   fmt1(VK_FORMAT_R16G16B16_SFLOAT,                  ISL_FORMAT_R16G16B16_FLOAT),
   fmt1(VK_FORMAT_R16G16B16A16_UNORM,                ISL_FORMAT_R16G16B16A16_UNORM),
   fmt1(VK_FORMAT_R16G16B16A16_SNORM,                ISL_FORMAT_R16G16B16A16_SNORM),
   fmt1(VK_FORMAT_R16G16B16A16_USCALED,              ISL_FORMAT_R16G16B16A16_USCALED),
   fmt1(VK_FORMAT_R16G16B16A16_SSCALED,              ISL_FORMAT_R16G16B16A16_SSCALED),
   fmt1(VK_FORMAT_R16G16B16A16_UINT,                 ISL_FORMAT_R16G16B16A16_UINT),
   fmt1(VK_FORMAT_R16G16B16A16_SINT,                 ISL_FORMAT_R16G16B16A16_SINT),
   fmt1(VK_FORMAT_R16G16B16A16_SFLOAT,               ISL_FORMAT_R16G16B16A16_FLOAT),
   fmt1(VK_FORMAT_R32_UINT,                          ISL_FORMAT_R32_UINT),
   fmt1(VK_FORMAT_R32_SINT,                          ISL_FORMAT_R32_SINT),
   fmt1(VK_FORMAT_R32_SFLOAT,                        ISL_FORMAT_R32_FLOAT),
   fmt1(VK_FORMAT_R32G32_UINT,                       ISL_FORMAT_R32G32_UINT),
   fmt1(VK_FORMAT_R32G32_SINT,                       ISL_FORMAT_R32G32_SINT),
   fmt1(VK_FORMAT_R32G32_SFLOAT,                     ISL_FORMAT_R32G32_FLOAT),
   fmt1(VK_FORMAT_R32G32B32_UINT,                    ISL_FORMAT_R32G32B32_UINT),
   fmt1(VK_FORMAT_R32G32B32_SINT,                    ISL_FORMAT_R32G32B32_SINT),
   fmt1(VK_FORMAT_R32G32B32_SFLOAT,                  ISL_FORMAT_R32G32B32_FLOAT),
   fmt1(VK_FORMAT_R32G32B32A32_UINT,                 ISL_FORMAT_R32G32B32A32_UINT),
   fmt1(VK_FORMAT_R32G32B32A32_SINT,                 ISL_FORMAT_R32G32B32A32_SINT),
   fmt1(VK_FORMAT_R32G32B32A32_SFLOAT,               ISL_FORMAT_R32G32B32A32_FLOAT),
   fmt1(VK_FORMAT_R64_UINT,                          ISL_FORMAT_R64_PASSTHRU),
   fmt1(VK_FORMAT_R64_SINT,                          ISL_FORMAT_R64_PASSTHRU),
   fmt1(VK_FORMAT_R64_SFLOAT,                        ISL_FORMAT_R64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64_UINT,                       ISL_FORMAT_R64G64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64_SINT,                       ISL_FORMAT_R64G64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64_SFLOAT,                     ISL_FORMAT_R64G64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64B64_UINT,                    ISL_FORMAT_R64G64B64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64B64_SINT,                    ISL_FORMAT_R64G64B64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64B64_SFLOAT,                  ISL_FORMAT_R64G64B64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64B64A64_UINT,                 ISL_FORMAT_R64G64B64A64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64B64A64_SINT,                 ISL_FORMAT_R64G64B64A64_PASSTHRU),
   fmt1(VK_FORMAT_R64G64B64A64_SFLOAT,               ISL_FORMAT_R64G64B64A64_PASSTHRU),
   fmt1(VK_FORMAT_B10G11R11_UFLOAT_PACK32,           ISL_FORMAT_R11G11B10_FLOAT),
   fmt1(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,            ISL_FORMAT_R9G9B9E5_SHAREDEXP),

   d_fmt(VK_FORMAT_D16_UNORM,                        ISL_FORMAT_R16_UNORM),
   d_fmt(VK_FORMAT_X8_D24_UNORM_PACK32,              ISL_FORMAT_R24_UNORM_X8_TYPELESS),
   d_fmt(VK_FORMAT_D32_SFLOAT,                       ISL_FORMAT_R32_FLOAT),
   s_fmt(VK_FORMAT_S8_UINT,                          ISL_FORMAT_R8_UINT),
   fmt_unsupported(VK_FORMAT_D16_UNORM_S8_UINT),
   ds_fmt2(VK_FORMAT_D24_UNORM_S8_UINT,              ISL_FORMAT_R24_UNORM_X8_TYPELESS, ISL_FORMAT_R8_UINT),
   ds_fmt2(VK_FORMAT_D32_SFLOAT_S8_UINT,             ISL_FORMAT_R32_FLOAT, ISL_FORMAT_R8_UINT),

   swiz_fmt1(VK_FORMAT_BC1_RGB_UNORM_BLOCK,          ISL_FORMAT_BC1_UNORM, RGB1),
   swiz_fmt1(VK_FORMAT_BC1_RGB_SRGB_BLOCK,           ISL_FORMAT_BC1_UNORM_SRGB, RGB1),
   fmt1(VK_FORMAT_BC1_RGBA_UNORM_BLOCK,              ISL_FORMAT_BC1_UNORM),
   fmt1(VK_FORMAT_BC1_RGBA_SRGB_BLOCK,               ISL_FORMAT_BC1_UNORM_SRGB),
   fmt1(VK_FORMAT_BC2_UNORM_BLOCK,                   ISL_FORMAT_BC2_UNORM),
   fmt1(VK_FORMAT_BC2_SRGB_BLOCK,                    ISL_FORMAT_BC2_UNORM_SRGB),
   fmt1(VK_FORMAT_BC3_UNORM_BLOCK,                   ISL_FORMAT_BC3_UNORM),
   fmt1(VK_FORMAT_BC3_SRGB_BLOCK,                    ISL_FORMAT_BC3_UNORM_SRGB),
   fmt1(VK_FORMAT_BC4_UNORM_BLOCK,                   ISL_FORMAT_BC4_UNORM),
   fmt1(VK_FORMAT_BC4_SNORM_BLOCK,                   ISL_FORMAT_BC4_SNORM),
   fmt1(VK_FORMAT_BC5_UNORM_BLOCK,                   ISL_FORMAT_BC5_UNORM),
   fmt1(VK_FORMAT_BC5_SNORM_BLOCK,                   ISL_FORMAT_BC5_SNORM),
   fmt1(VK_FORMAT_BC6H_UFLOAT_BLOCK,                 ISL_FORMAT_BC6H_UF16),
   fmt1(VK_FORMAT_BC6H_SFLOAT_BLOCK,                 ISL_FORMAT_BC6H_SF16),
   fmt1(VK_FORMAT_BC7_UNORM_BLOCK,                   ISL_FORMAT_BC7_UNORM),
   fmt1(VK_FORMAT_BC7_SRGB_BLOCK,                    ISL_FORMAT_BC7_UNORM_SRGB),
   fmt1(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,           ISL_FORMAT_ETC2_RGB8),
   fmt1(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,            ISL_FORMAT_ETC2_SRGB8),
   fmt1(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,         ISL_FORMAT_ETC2_RGB8_PTA),
   fmt1(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,          ISL_FORMAT_ETC2_SRGB8_PTA),
   fmt1(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,         ISL_FORMAT_ETC2_EAC_RGBA8),
   fmt1(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,          ISL_FORMAT_ETC2_EAC_SRGB8_A8),
   fmt1(VK_FORMAT_EAC_R11_UNORM_BLOCK,               ISL_FORMAT_EAC_R11),
   fmt1(VK_FORMAT_EAC_R11_SNORM_BLOCK,               ISL_FORMAT_EAC_SIGNED_R11),
   fmt1(VK_FORMAT_EAC_R11G11_UNORM_BLOCK,            ISL_FORMAT_EAC_RG11),
   fmt1(VK_FORMAT_EAC_R11G11_SNORM_BLOCK,            ISL_FORMAT_EAC_SIGNED_RG11),
   fmt1(VK_FORMAT_ASTC_4x4_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_4X4_U8SRGB),
   fmt1(VK_FORMAT_ASTC_5x4_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_5X4_U8SRGB),
   fmt1(VK_FORMAT_ASTC_5x5_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_5X5_U8SRGB),
   fmt1(VK_FORMAT_ASTC_6x5_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_6X5_U8SRGB),
   fmt1(VK_FORMAT_ASTC_6x6_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_6X6_U8SRGB),
   fmt1(VK_FORMAT_ASTC_8x5_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_8X5_U8SRGB),
   fmt1(VK_FORMAT_ASTC_8x6_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_8X6_U8SRGB),
   fmt1(VK_FORMAT_ASTC_8x8_SRGB_BLOCK,               ISL_FORMAT_ASTC_LDR_2D_8X8_U8SRGB),
   fmt1(VK_FORMAT_ASTC_10x5_SRGB_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_10X5_U8SRGB),
   fmt1(VK_FORMAT_ASTC_10x6_SRGB_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_10X6_U8SRGB),
   fmt1(VK_FORMAT_ASTC_10x8_SRGB_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_10X8_U8SRGB),
   fmt1(VK_FORMAT_ASTC_10x10_SRGB_BLOCK,             ISL_FORMAT_ASTC_LDR_2D_10X10_U8SRGB),
   fmt1(VK_FORMAT_ASTC_12x10_SRGB_BLOCK,             ISL_FORMAT_ASTC_LDR_2D_12X10_U8SRGB),
   fmt1(VK_FORMAT_ASTC_12x12_SRGB_BLOCK,             ISL_FORMAT_ASTC_LDR_2D_12X12_U8SRGB),
   fmt1(VK_FORMAT_ASTC_4x4_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_4X4_FLT16),
   fmt1(VK_FORMAT_ASTC_5x4_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_5X4_FLT16),
   fmt1(VK_FORMAT_ASTC_5x5_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_5X5_FLT16),
   fmt1(VK_FORMAT_ASTC_6x5_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_6X5_FLT16),
   fmt1(VK_FORMAT_ASTC_6x6_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_6X6_FLT16),
   fmt1(VK_FORMAT_ASTC_8x5_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_8X5_FLT16),
   fmt1(VK_FORMAT_ASTC_8x6_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_8X6_FLT16),
   fmt1(VK_FORMAT_ASTC_8x8_UNORM_BLOCK,              ISL_FORMAT_ASTC_LDR_2D_8X8_FLT16),
   fmt1(VK_FORMAT_ASTC_10x5_UNORM_BLOCK,             ISL_FORMAT_ASTC_LDR_2D_10X5_FLT16),
   fmt1(VK_FORMAT_ASTC_10x6_UNORM_BLOCK,             ISL_FORMAT_ASTC_LDR_2D_10X6_FLT16),
   fmt1(VK_FORMAT_ASTC_10x8_UNORM_BLOCK,             ISL_FORMAT_ASTC_LDR_2D_10X8_FLT16),
   fmt1(VK_FORMAT_ASTC_10x10_UNORM_BLOCK,            ISL_FORMAT_ASTC_LDR_2D_10X10_FLT16),
   fmt1(VK_FORMAT_ASTC_12x10_UNORM_BLOCK,            ISL_FORMAT_ASTC_LDR_2D_12X10_FLT16),
   fmt1(VK_FORMAT_ASTC_12x12_UNORM_BLOCK,            ISL_FORMAT_ASTC_LDR_2D_12X12_FLT16),
   fmt_unsupported(VK_FORMAT_B8G8R8_UNORM),
   fmt_unsupported(VK_FORMAT_B8G8R8_SNORM),
   fmt_unsupported(VK_FORMAT_B8G8R8_USCALED),
   fmt_unsupported(VK_FORMAT_B8G8R8_SSCALED),
   fmt_unsupported(VK_FORMAT_B8G8R8_UINT),
   fmt_unsupported(VK_FORMAT_B8G8R8_SINT),
   fmt_unsupported(VK_FORMAT_B8G8R8_SRGB),
   fmt1(VK_FORMAT_B8G8R8A8_UNORM,                    ISL_FORMAT_B8G8R8A8_UNORM),
   fmt_unsupported(VK_FORMAT_B8G8R8A8_SNORM),
   fmt_unsupported(VK_FORMAT_B8G8R8A8_USCALED),
   fmt_unsupported(VK_FORMAT_B8G8R8A8_SSCALED),
   fmt_unsupported(VK_FORMAT_B8G8R8A8_UINT),
   fmt_unsupported(VK_FORMAT_B8G8R8A8_SINT),
   fmt1(VK_FORMAT_B8G8R8A8_SRGB,                     ISL_FORMAT_B8G8R8A8_UNORM_SRGB),
};

static const struct anv_format _4444_formats[] = {
   fmt1(VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT, ISL_FORMAT_B4G4R4A4_UNORM),
   fmt_unsupported(VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT),
};

static const struct anv_format ycbcr_formats[] = {
   ycbcr_fmt(VK_FORMAT_G8B8G8R8_422_UNORM, 1,
             y_plane(0, ISL_FORMAT_YCRCB_SWAPUV, RGBA, _ISL_SWIZZLE(BLUE, GREEN, RED, ZERO), 1, 1)),
   ycbcr_fmt(VK_FORMAT_B8G8R8G8_422_UNORM, 1,
             y_plane(0, ISL_FORMAT_YCRCB_SWAPUVY, RGBA, _ISL_SWIZZLE(BLUE, GREEN, RED, ZERO), 1, 1)),
   ycbcr_fmt(VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM, 3,
             y_plane(0, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(BLUE, ZERO, ZERO, ZERO), 2, 2),
             chroma_plane(2, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(RED, ZERO, ZERO, ZERO), 2, 2)),
   ycbcr_fmt(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 2,
             y_plane(0, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R8G8_UNORM, RGBA, _ISL_SWIZZLE(BLUE, RED, ZERO, ZERO), 2, 2)),
   ycbcr_fmt(VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM, 3,
             y_plane(0, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(BLUE, ZERO, ZERO, ZERO), 2, 1),
             chroma_plane(2, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(RED, ZERO, ZERO, ZERO), 2, 1)),
   ycbcr_fmt(VK_FORMAT_G8_B8R8_2PLANE_422_UNORM, 2,
             y_plane(0, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R8G8_UNORM, RGBA, _ISL_SWIZZLE(BLUE, RED, ZERO, ZERO), 2, 1)),
   ycbcr_fmt(VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM, 3,
             y_plane(0, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(BLUE, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(2, ISL_FORMAT_R8_UNORM, RGBA, _ISL_SWIZZLE(RED, ZERO, ZERO, ZERO), 1, 1)),

   fmt_unsupported(VK_FORMAT_R10X6_UNORM_PACK16),
   fmt_unsupported(VK_FORMAT_R10X6G10X6_UNORM_2PACK16),
   fmt_unsupported(VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16),
   fmt_unsupported(VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16),
   fmt_unsupported(VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16),
   fmt_unsupported(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_R12X4_UNORM_PACK16),
   fmt_unsupported(VK_FORMAT_R12X4G12X4_UNORM_2PACK16),
   fmt_unsupported(VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16),
   fmt_unsupported(VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16),
   fmt_unsupported(VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16),
   fmt_unsupported(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16),
   fmt_unsupported(VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16),
   /* TODO: it is possible to enable the following 2 formats, but that
    * requires further refactoring of how we handle multiplanar formats.
    */
   fmt_unsupported(VK_FORMAT_G16B16G16R16_422_UNORM),
   fmt_unsupported(VK_FORMAT_B16G16R16G16_422_UNORM),

   ycbcr_fmt(VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM, 3,
             y_plane(0, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(BLUE, ZERO, ZERO, ZERO), 2, 2),
             chroma_plane(2, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(RED, ZERO, ZERO, ZERO), 2, 2)),
   ycbcr_fmt(VK_FORMAT_G16_B16R16_2PLANE_420_UNORM, 2,
             y_plane(0, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R16G16_UNORM, RGBA, _ISL_SWIZZLE(BLUE, RED, ZERO, ZERO), 2, 2)),
   ycbcr_fmt(VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM, 3,
             y_plane(0, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(BLUE, ZERO, ZERO, ZERO), 2, 1),
             chroma_plane(2, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(RED, ZERO, ZERO, ZERO), 2, 1)),
   ycbcr_fmt(VK_FORMAT_G16_B16R16_2PLANE_422_UNORM, 2,
             y_plane(0, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R16G16_UNORM, RGBA, _ISL_SWIZZLE(BLUE, RED, ZERO, ZERO), 2, 1)),
   ycbcr_fmt(VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM, 3,
             y_plane(0, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(GREEN, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(1, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(BLUE, ZERO, ZERO, ZERO), 1, 1),
             chroma_plane(2, ISL_FORMAT_R16_UNORM, RGBA, _ISL_SWIZZLE(RED, ZERO, ZERO, ZERO), 1, 1)),
};

#undef _fmt
#undef swiz_fmt1
#undef fmt1
#undef fmt

static const struct {
   const struct anv_format *formats;
   uint32_t n_formats;
} anv_formats[] = {
   [0]                                       = { .formats = main_formats,
                                                 .n_formats = ARRAY_SIZE(main_formats), },
   [_VK_EXT_4444_formats_number]             = { .formats = _4444_formats,
                                                 .n_formats = ARRAY_SIZE(_4444_formats), },
   [_VK_KHR_sampler_ycbcr_conversion_number] = { .formats = ycbcr_formats,
                                                 .n_formats = ARRAY_SIZE(ycbcr_formats), },
};

const struct anv_format *
anv_get_format(VkFormat vk_format)
{
   uint32_t enum_offset = VK_ENUM_OFFSET(vk_format);
   uint32_t ext_number = VK_ENUM_EXTENSION(vk_format);

   if (ext_number >= ARRAY_SIZE(anv_formats) ||
       enum_offset >= anv_formats[ext_number].n_formats)
      return NULL;

   const struct anv_format *format =
      &anv_formats[ext_number].formats[enum_offset];
   if (format->planes[0].isl_format == ISL_FORMAT_UNSUPPORTED)
      return NULL;

   return format;
}

/**
 * Exactly one bit must be set in \a aspect.
 */
struct anv_format_plane
anv_get_format_plane(const struct gen_device_info *devinfo, VkFormat vk_format,
                     VkImageAspectFlagBits aspect, VkImageTiling tiling)
{
   const struct anv_format *format = anv_get_format(vk_format);
   const struct anv_format_plane unsupported = {
      .isl_format = ISL_FORMAT_UNSUPPORTED,
   };

   if (format == NULL)
      return unsupported;

   uint32_t plane = anv_image_aspect_to_plane(vk_format_aspects(vk_format), aspect);
   struct anv_format_plane plane_format = format->planes[plane];
   if (plane_format.isl_format == ISL_FORMAT_UNSUPPORTED)
      return unsupported;

   if (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      assert(vk_format_aspects(vk_format) &
             (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));

      /* There's no reason why we strictly can't support depth or stencil with
       * modifiers but there's also no reason why we should.
       */
      if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         return unsupported;

      return plane_format;
   }

   assert((aspect & ~VK_IMAGE_ASPECT_ANY_COLOR_BIT_ANV) == 0);

   const struct isl_format_layout *isl_layout =
      isl_format_get_layout(plane_format.isl_format);

   /* On Ivy Bridge we don't even have enough 24 and 48-bit formats that we
    * can reliably do texture upload with BLORP so just don't claim support
    * for any of them.
    */
   if (devinfo->gen == 7 && !devinfo->is_haswell &&
       (isl_layout->bpb == 24 || isl_layout->bpb == 48))
      return unsupported;

   if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      /* No non-power-of-two fourcc formats exist */
      if (!util_is_power_of_two_or_zero(isl_layout->bpb))
         return unsupported;

      if (isl_format_is_compressed(plane_format.isl_format))
         return unsupported;
   }

   if (tiling == VK_IMAGE_TILING_OPTIMAL &&
       !util_is_power_of_two_or_zero(isl_layout->bpb)) {
      /* Tiled formats *must* be power-of-two because we need up upload
       * them with the render pipeline.  For 3-channel formats, we fix
       * this by switching them over to RGBX or RGBA formats under the
       * hood.
       */
      enum isl_format rgbx = isl_format_rgb_to_rgbx(plane_format.isl_format);
      if (rgbx != ISL_FORMAT_UNSUPPORTED &&
          isl_format_supports_rendering(devinfo, rgbx)) {
         plane_format.isl_format = rgbx;
      } else {
         plane_format.isl_format =
            isl_format_rgb_to_rgba(plane_format.isl_format);
         plane_format.swizzle = ISL_SWIZZLE(RED, GREEN, BLUE, ONE);
      }
   }

   /* The B4G4R4A4 format isn't available prior to Broadwell so we have to fall
    * back to a format with a more complex swizzle.
    */
   if (vk_format == VK_FORMAT_B4G4R4A4_UNORM_PACK16 && devinfo->gen < 8) {
      plane_format.isl_format = ISL_FORMAT_B4G4R4A4_UNORM;
      plane_format.swizzle = ISL_SWIZZLE(GREEN, RED, ALPHA, BLUE);
   }

   return plane_format;
}

// Format capabilities

VkFormatFeatureFlags
anv_get_image_format_features(const struct gen_device_info *devinfo,
                              VkFormat vk_format,
                              const struct anv_format *anv_format,
                              VkImageTiling vk_tiling)
{
   VkFormatFeatureFlags flags = 0;

   if (anv_format == NULL)
      return 0;

   const VkImageAspectFlags aspects = vk_format_aspects(vk_format);

   if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      if (vk_tiling == VK_IMAGE_TILING_LINEAR)
         return 0;

      flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_BLIT_SRC_BIT |
               VK_FORMAT_FEATURE_BLIT_DST_BIT |
               VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
               VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

      if ((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) && devinfo->gen >= 9)
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT_EXT;

      return flags;
   }

   const struct anv_format_plane plane_format =
      anv_get_format_plane(devinfo, vk_format, VK_IMAGE_ASPECT_COLOR_BIT,
                           vk_tiling);

   if (plane_format.isl_format == ISL_FORMAT_UNSUPPORTED)
      return 0;

   struct anv_format_plane base_plane_format = plane_format;
   if (vk_tiling != VK_IMAGE_TILING_LINEAR) {
      base_plane_format = anv_get_format_plane(devinfo, vk_format,
                                               VK_IMAGE_ASPECT_COLOR_BIT,
                                               VK_IMAGE_TILING_LINEAR);
   }

   enum isl_format base_isl_format = base_plane_format.isl_format;

   /* ASTC textures must be in Y-tiled memory */
   if (vk_tiling == VK_IMAGE_TILING_LINEAR &&
       isl_format_get_layout(plane_format.isl_format)->txc == ISL_TXC_ASTC)
      return 0;

   /* ASTC requires nasty workarounds on BSW so we just disable it for now.
    *
    * TODO: Figure out the ASTC workarounds and re-enable on BSW.
    */
   if (devinfo->gen < 9 &&
       isl_format_get_layout(plane_format.isl_format)->txc == ISL_TXC_ASTC)
      return 0;

   if (isl_format_supports_sampling(devinfo, plane_format.isl_format)) {
      flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

      if (devinfo->gen >= 9)
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT_EXT;

      if (isl_format_supports_filtering(devinfo, plane_format.isl_format))
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
   }

   /* We can render to swizzled formats.  However, if the alpha channel is
    * moved, then blending won't work correctly.  The PRM tells us
    * straight-up not to render to such a surface.
    */
   if (isl_format_supports_rendering(devinfo, plane_format.isl_format) &&
       plane_format.swizzle.a == ISL_CHANNEL_SELECT_ALPHA) {
      flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;

      if (isl_format_supports_alpha_blending(devinfo, plane_format.isl_format))
         flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
   }

   /* Load/store is determined based on base format.  This prevents RGB
    * formats from showing up as load/store capable.
    */
   if (isl_is_storage_image_format(base_isl_format))
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;

   if (base_isl_format == ISL_FORMAT_R32_SINT ||
       base_isl_format == ISL_FORMAT_R32_UINT ||
       base_isl_format == ISL_FORMAT_R32_FLOAT)
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;

   if (flags) {
      flags |= VK_FORMAT_FEATURE_BLIT_SRC_BIT |
               VK_FORMAT_FEATURE_BLIT_DST_BIT |
               VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
               VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   }

   /* XXX: We handle 3-channel formats by switching them out for RGBX or
    * RGBA formats behind-the-scenes.  This works fine for textures
    * because the upload process will fill in the extra channel.
    * We could also support it for render targets, but it will take
    * substantially more work and we have enough RGBX formats to handle
    * what most clients will want.
    */
   if (vk_tiling == VK_IMAGE_TILING_OPTIMAL &&
       base_isl_format != ISL_FORMAT_UNSUPPORTED &&
       !util_is_power_of_two_or_zero(isl_format_layouts[base_isl_format].bpb) &&
       isl_format_rgb_to_rgbx(base_isl_format) == ISL_FORMAT_UNSUPPORTED) {
      flags &= ~VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
      flags &= ~VK_FORMAT_FEATURE_BLIT_DST_BIT;
   }

   if (anv_format->can_ycbcr) {
      /* The sampler doesn't have support for mid point when it handles YUV on
       * its own.
       */
      if (isl_format_is_yuv(anv_format->planes[0].isl_format)) {
         /* TODO: We've disabled linear implicit reconstruction with the
          * sampler. The failures show a slightly out of range values on the
          * bottom left of the sampled image.
          */
         flags |= VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;
      } else {
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT |
                  VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT |
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT;
      }

      /* We can support cosited chroma locations when handle planes with our
       * own shader snippets.
       */
      for (unsigned p = 0; p < anv_format->n_planes; p++) {
         if (anv_format->planes[p].denominator_scales[0] > 1 ||
             anv_format->planes[p].denominator_scales[1] > 1) {
            flags |= VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT;
            break;
         }
      }

      if (anv_format->n_planes > 1)
         flags |= VK_FORMAT_FEATURE_DISJOINT_BIT;

      const VkFormatFeatureFlags disallowed_ycbcr_image_features =
         VK_FORMAT_FEATURE_BLIT_SRC_BIT |
         VK_FORMAT_FEATURE_BLIT_DST_BIT |
         VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
         VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
         VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;

      flags &= ~disallowed_ycbcr_image_features;
   }

   return flags;
}

static VkFormatFeatureFlags
get_buffer_format_features(const struct gen_device_info *devinfo,
                           VkFormat vk_format,
                           const struct anv_format *anv_format)
{
   VkFormatFeatureFlags flags = 0;

   if (anv_format == NULL)
      return 0;

   const enum isl_format isl_format = anv_format->planes[0].isl_format;

   if (isl_format == ISL_FORMAT_UNSUPPORTED)
      return 0;

   if (anv_format->n_planes > 1)
      return 0;

   if (anv_format->can_ycbcr)
      return 0;

   if (vk_format_is_depth_or_stencil(vk_format))
      return 0;

   if (isl_format_supports_sampling(devinfo, isl_format) &&
       !isl_format_is_compressed(isl_format))
      flags |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;

   if (isl_format_supports_vertex_fetch(devinfo, isl_format))
      flags |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;

   if (isl_is_storage_image_format(isl_format))
      flags |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;

   if (isl_format == ISL_FORMAT_R32_SINT || isl_format == ISL_FORMAT_R32_UINT)
      flags |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;

   return flags;
}

static void
get_wsi_format_modifier_properties_list(const struct anv_physical_device *physical_device,
                                        VkFormat vk_format,
                                        VkDrmFormatModifierPropertiesListEXT *list)
{
   const struct anv_format *anv_format = anv_get_format(vk_format);

   VK_OUTARRAY_MAKE(out, list->pDrmFormatModifierProperties,
                    &list->drmFormatModifierCount);

   /* This is a simplified list where all the modifiers are available */
   assert(vk_format == VK_FORMAT_B8G8R8_SRGB ||
          vk_format == VK_FORMAT_B8G8R8_UNORM ||
          vk_format == VK_FORMAT_B8G8R8A8_SRGB ||
          vk_format == VK_FORMAT_B8G8R8A8_UNORM);

   uint64_t modifiers[] = {
      DRM_FORMAT_MOD_LINEAR,
      I915_FORMAT_MOD_X_TILED,
      I915_FORMAT_MOD_Y_TILED,
      I915_FORMAT_MOD_Y_TILED_CCS,
   };

   for (uint32_t i = 0; i < ARRAY_SIZE(modifiers); i++) {
      const struct isl_drm_modifier_info *mod_info =
         isl_drm_modifier_get_info(modifiers[i]);

      if (mod_info->aux_usage == ISL_AUX_USAGE_CCS_E &&
          !isl_format_supports_ccs_e(&physical_device->info,
                                     anv_format->planes[0].isl_format))
         continue;

      /* Gen12's CCS layout changes compared to Gen9-11. */
      if (mod_info->modifier == I915_FORMAT_MOD_Y_TILED_CCS &&
          physical_device->info.gen >= 12)
         continue;

      vk_outarray_append(&out, mod_props) {
         mod_props->drmFormatModifier = modifiers[i];
         if (isl_drm_modifier_has_aux(modifiers[i]))
            mod_props->drmFormatModifierPlaneCount = 2;
         else
            mod_props->drmFormatModifierPlaneCount = anv_format->n_planes;
      }
   }
}

void anv_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    vk_format,
    VkFormatProperties*                         pFormatProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   const struct gen_device_info *devinfo = &physical_device->info;
   const struct anv_format *anv_format = anv_get_format(vk_format);

   *pFormatProperties = (VkFormatProperties) {
      .linearTilingFeatures =
         anv_get_image_format_features(devinfo, vk_format, anv_format,
                                       VK_IMAGE_TILING_LINEAR),
      .optimalTilingFeatures =
         anv_get_image_format_features(devinfo, vk_format, anv_format,
                                       VK_IMAGE_TILING_OPTIMAL),
      .bufferFeatures =
         get_buffer_format_features(devinfo, vk_format, anv_format),
   };
}

void anv_GetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties2*                        pFormatProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   anv_GetPhysicalDeviceFormatProperties(physicalDevice, format,
                                         &pFormatProperties->formatProperties);

   vk_foreach_struct(ext, pFormatProperties->pNext) {
      /* Use unsigned since some cases are not in the VkStructureType enum. */
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT:
         get_wsi_format_modifier_properties_list(physical_device, format,
                                                 (void *)ext);
         break;
      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

static VkResult
anv_get_image_format_properties(
   struct anv_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageFormatProperties *pImageFormatProperties,
   VkSamplerYcbcrConversionImageFormatProperties *pYcbcrImageFormatProperties)
{
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   const struct gen_device_info *devinfo = &physical_device->info;
   const struct anv_format *format = anv_get_format(info->format);

   if (format == NULL)
      goto unsupported;

   assert(format->vk_format == info->format);
   format_feature_flags = anv_get_image_format_features(devinfo, info->format,
                                                        format, info->tiling);

   switch (info->type) {
   default:
      unreachable("bad VkImageType");
   case VK_IMAGE_TYPE_1D:
      maxExtent.width = 16384;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 2048;
      sampleCounts = VK_SAMPLE_COUNT_1_BIT;
      break;
   case VK_IMAGE_TYPE_2D:
      /* FINISHME: Does this really differ for cube maps? The documentation
       * for RENDER_SURFACE_STATE suggests so.
       */
      maxExtent.width = 16384;
      maxExtent.height = 16384;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent.width = 2048;
      maxExtent.height = 2048;
      maxExtent.depth = 2048;
      maxMipLevels = 12; /* log2(maxWidth) + 1 */
      maxArraySize = 1;
      break;
   }

   /* Our hardware doesn't support 1D compressed textures.
    *    From the SKL PRM, RENDER_SURFACE_STATE::SurfaceFormat:
    *    * This field cannot be a compressed (BC*, DXT*, FXT*, ETC*, EAC*) format
    *       if the Surface Type is SURFTYPE_1D.
    *    * This field cannot be ASTC format if the Surface Type is SURFTYPE_1D.
    */
   if (info->type == VK_IMAGE_TYPE_1D &&
       isl_format_is_compressed(format->planes[0].isl_format)) {
       goto unsupported;
   }

   if (info->tiling == VK_IMAGE_TILING_OPTIMAL &&
       info->type == VK_IMAGE_TYPE_2D &&
       (format_feature_flags & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(info->usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      sampleCounts = isl_device_get_sample_counts(&physical_device->isl_dev);
   }

   if (info->usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
      /* Accept transfers on anything we can sample from or renderer to. */
      if (!(format_feature_flags & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (info->flags & VK_IMAGE_CREATE_DISJOINT_BIT) {
      /* From the Vulkan 1.2.149 spec, VkImageCreateInfo:
       *
       *    If format is a multi-planar format, and if imageCreateFormatFeatures
       *    (as defined in Image Creation Limits) does not contain
       *    VK_FORMAT_FEATURE_DISJOINT_BIT, then flags must not contain
       *    VK_IMAGE_CREATE_DISJOINT_BIT.
       */
      if (format->n_planes > 1 &&
          !(format_feature_flags & VK_FORMAT_FEATURE_DISJOINT_BIT)) {
         goto unsupported;
      }

      /* From the Vulkan 1.2.149 spec, VkImageCreateInfo:
       *
       * If format is not a multi-planar format, and flags does not include
       * VK_IMAGE_CREATE_ALIAS_BIT, flags must not contain
       * VK_IMAGE_CREATE_DISJOINT_BIT.
       */
      if (format->n_planes == 1 &&
          !(info->flags & VK_IMAGE_CREATE_ALIAS_BIT)) {
          goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) {
      /* Nothing to check. */
   }

   if (info->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      /* Ignore this flag because it was removed from the
       * provisional_I_20150910 header.
       */
   }

   if (info->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *modifier_info =
         vk_find_struct_const(info->pNext,
                              PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);

      /* Modifiers are only supported on simple 2D images */
      if (info->type != VK_IMAGE_TYPE_2D)
         goto unsupported;
      maxArraySize = 1;
      maxMipLevels = 1;
      assert(sampleCounts == VK_SAMPLE_COUNT_1_BIT);

      /* Modifiers are not yet supported for YCbCr */
      const struct anv_format *format = anv_get_format(info->format);
      if (format->n_planes > 1)
         goto unsupported;

      const struct isl_drm_modifier_info *isl_mod_info =
         isl_drm_modifier_get_info(modifier_info->drmFormatModifier);
      if (isl_mod_info->aux_usage == ISL_AUX_USAGE_CCS_E) {
         /* If we have a CCS modifier, ensure that the format supports CCS
          * and, if VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT is set, all of the
          * formats in the format list are CCS compatible.
          */
         const VkImageFormatListCreateInfoKHR *fmt_list =
            vk_find_struct_const(info->pNext,
                                 IMAGE_FORMAT_LIST_CREATE_INFO_KHR);
         if (!anv_formats_ccs_e_compatible(devinfo, info->flags,
                                           info->format, info->tiling,
                                           fmt_list))
            goto unsupported;
      }
   }

   /* From the bspec section entitled "Surface Layout and Tiling",
    * pre-gen9 has a 2 GB limitation of the size in bytes,
    * gen9 and gen10 have a 256 GB limitation and gen11+
    * has a 16 TB limitation.
    */
   uint64_t maxResourceSize = 0;
   if (devinfo->gen < 9)
      maxResourceSize = (uint64_t) 1 << 31;
   else if (devinfo->gen < 11)
      maxResourceSize = (uint64_t) 1 << 38;
   else
      maxResourceSize = (uint64_t) 1 << 44;

   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* FINISHME: Accurately calculate
       * VkImageFormatProperties::maxResourceSize.
       */
      .maxResourceSize = maxResourceSize,
   };

   if (pYcbcrImageFormatProperties) {
      pYcbcrImageFormatProperties->combinedImageSamplerDescriptorCount =
         format->n_planes;
   }

   return VK_SUCCESS;

unsupported:
   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = { 0, 0, 0 },
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

VkResult anv_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          createFlags,
    VkImageFormatProperties*                    pImageFormatProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);

   const VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = NULL,
      .format = format,
      .type = type,
      .tiling = tiling,
      .usage = usage,
      .flags = createFlags,
   };

   return anv_get_image_format_properties(physical_device, &info,
                                          pImageFormatProperties, NULL);
}

static const VkExternalMemoryProperties prime_fd_props = {
   /* If we can handle external, then we can both import and export it. */
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   /* For the moment, let's not support mixing and matching */
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
};

static const VkExternalMemoryProperties userptr_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes = 0,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
};

static const VkExternalMemoryProperties android_buffer_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
};


static const VkExternalMemoryProperties android_image_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
};

VkResult anv_GetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2*     base_info,
    VkImageFormatProperties2*                   base_props)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
   VkAndroidHardwareBufferUsageANDROID *android_usage = NULL;
   VkResult result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *) s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT:
         /* anv_get_image_format_properties will handle this */
         break;
      case VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO_EXT:
         /* Ignore but don't warn */
         break;
      default:
         anv_debug_ignored_stype(s->sType);
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *) s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (void *) s;
         break;
      case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID:
         android_usage = (void *) s;
         break;
      default:
         anv_debug_ignored_stype(s->sType);
         break;
      }
   }

   result = anv_get_image_format_properties(physical_device, base_info,
               &base_props->imageFormatProperties, ycbcr_props);
   if (result != VK_SUCCESS)
      goto fail;

   bool ahw_supported =
      physical_device->supported_extensions.ANDROID_external_memory_android_hardware_buffer;

   if (ahw_supported && android_usage) {
      android_usage->androidHardwareBufferUsage =
         anv_ahw_usage_from_vk_usage(base_info->flags,
                                     base_info->usage);

      /* Limit maxArrayLayers to 1 for AHardwareBuffer based images for now. */
      base_props->imageFormatProperties.maxArrayLayers = 1;
   }

   /* From the Vulkan 1.0.42 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
         if (external_props)
            external_props->externalMemoryProperties = prime_fd_props;
         break;
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
         if (external_props)
            external_props->externalMemoryProperties = userptr_props;
         break;
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID:
         if (ahw_supported && external_props) {
            external_props->externalMemoryProperties = android_image_props;
            break;
         }
      /* fallthrough - if ahw not supported */
      default:
         /* From the Vulkan 1.0.42 spec:
          *
          *    If handleType is not compatible with the [parameters] specified
          *    in VkPhysicalDeviceImageFormatInfo2, then
          *    vkGetPhysicalDeviceImageFormatProperties2 returns
          *    VK_ERROR_FORMAT_NOT_SUPPORTED.
          */
         result = vk_errorfi(physical_device->instance, physical_device,
                             VK_ERROR_FORMAT_NOT_SUPPORTED,
                             "unsupported VkExternalMemoryTypeFlagBits 0x%x",
                             external_info->handleType);
         goto fail;
      }
   }

   return VK_SUCCESS;

 fail:
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      /* From the Vulkan 1.0.42 spec:
       *
       *    If the combination of parameters to
       *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by
       *    the implementation for use in vkCreateImage, then all members of
       *    imageFormatProperties will be filled with zero.
       */
      base_props->imageFormatProperties = (VkImageFormatProperties) {};
   }

   return result;
}

void anv_GetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    uint32_t                                    samples,
    VkImageUsageFlags                           usage,
    VkImageTiling                               tiling,
    uint32_t*                                   pNumProperties,
    VkSparseImageFormatProperties*              pProperties)
{
   /* Sparse images are not yet supported. */
   *pNumProperties = 0;
}

void anv_GetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t*                                   pPropertyCount,
    VkSparseImageFormatProperties2*             pProperties)
{
   /* Sparse images are not yet supported. */
   *pPropertyCount = 0;
}

void anv_GetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice                             physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo*    pExternalBufferInfo,
    VkExternalBufferProperties*                  pExternalBufferProperties)
{
   /* The Vulkan 1.0.42 spec says "handleType must be a valid
    * VkExternalMemoryHandleTypeFlagBits value" in
    * VkPhysicalDeviceExternalBufferInfo. This differs from
    * VkPhysicalDeviceExternalImageFormatInfo, which surprisingly permits
    * handleType == 0.
    */
   assert(pExternalBufferInfo->handleType != 0);

   /* All of the current flags are for sparse which we don't support yet.
    * Even when we do support it, doing sparse on external memory sounds
    * sketchy.  Also, just disallowing flags is the safe option.
    */
   if (pExternalBufferInfo->flags)
      goto unsupported;

   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);

   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      pExternalBufferProperties->externalMemoryProperties = prime_fd_props;
      return;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      pExternalBufferProperties->externalMemoryProperties = userptr_props;
      return;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID:
      if (physical_device->supported_extensions.ANDROID_external_memory_android_hardware_buffer) {
         pExternalBufferProperties->externalMemoryProperties = android_buffer_props;
         return;
      }
      /* fallthrough if ahw not supported */
   default:
      goto unsupported;
   }

 unsupported:
   /* From the Vulkan 1.1.113 spec:
    *
    *    compatibleHandleTypes must include at least handleType.
    */
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties) {
         .compatibleHandleTypes = pExternalBufferInfo->handleType,
      };
}

VkResult anv_CreateSamplerYcbcrConversion(
    VkDevice                                    _device,
    const VkSamplerYcbcrConversionCreateInfo*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversion*                   pYcbcrConversion)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_ycbcr_conversion *conversion;

   /* Search for VkExternalFormatANDROID and resolve the format. */
   struct anv_format *ext_format = NULL;
   const VkExternalFormatANDROID *ext_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_FORMAT_ANDROID);

   uint64_t format = ext_info ? ext_info->externalFormat : 0;
   if (format) {
      assert(pCreateInfo->format == VK_FORMAT_UNDEFINED);
      ext_format = (struct anv_format *) (uintptr_t) format;
   }

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO);

   conversion = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*conversion), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!conversion)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(conversion, 0, sizeof(*conversion));

   vk_object_base_init(&device->vk, &conversion->base,
                       VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION);
   conversion->format = anv_get_format(pCreateInfo->format);
   conversion->ycbcr_model = pCreateInfo->ycbcrModel;
   conversion->ycbcr_range = pCreateInfo->ycbcrRange;

   /* The Vulkan 1.1.95 spec says "When creating an external format conversion,
    * the value of components if ignored."
    */
   if (!ext_format) {
      conversion->mapping[0] = pCreateInfo->components.r;
      conversion->mapping[1] = pCreateInfo->components.g;
      conversion->mapping[2] = pCreateInfo->components.b;
      conversion->mapping[3] = pCreateInfo->components.a;
   }

   conversion->chroma_offsets[0] = pCreateInfo->xChromaOffset;
   conversion->chroma_offsets[1] = pCreateInfo->yChromaOffset;
   conversion->chroma_filter = pCreateInfo->chromaFilter;

   /* Setup external format. */
   if (ext_format)
      conversion->format = ext_format;

   bool has_chroma_subsampled = false;
   for (uint32_t p = 0; p < conversion->format->n_planes; p++) {
      if (conversion->format->planes[p].has_chroma &&
          (conversion->format->planes[p].denominator_scales[0] > 1 ||
           conversion->format->planes[p].denominator_scales[1] > 1))
         has_chroma_subsampled = true;
   }
   conversion->chroma_reconstruction = has_chroma_subsampled &&
      (conversion->chroma_offsets[0] == VK_CHROMA_LOCATION_COSITED_EVEN ||
       conversion->chroma_offsets[1] == VK_CHROMA_LOCATION_COSITED_EVEN);

   *pYcbcrConversion = anv_ycbcr_conversion_to_handle(conversion);

   return VK_SUCCESS;
}

void anv_DestroySamplerYcbcrConversion(
    VkDevice                                    _device,
    VkSamplerYcbcrConversion                    YcbcrConversion,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_ycbcr_conversion, conversion, YcbcrConversion);

   if (!conversion)
      return;

   vk_object_base_finish(&conversion->base);
   vk_free2(&device->vk.alloc, pAllocator, conversion);
}
