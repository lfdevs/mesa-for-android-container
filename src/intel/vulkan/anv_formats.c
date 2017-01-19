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
#include "vk_format_info.h"

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

#define swiz_fmt(__vk_fmt, __hw_fmt, __swizzle)     \
   [__vk_fmt] = { \
      .isl_format = __hw_fmt, \
      .swizzle = __swizzle, \
   }

#define fmt(__vk_fmt, __hw_fmt) \
   swiz_fmt(__vk_fmt, __hw_fmt, RGBA)

/* HINT: For array formats, the ISL name should match the VK name.  For
 * packed formats, they should have the channels in reverse order from each
 * other.  The reason for this is that, for packed formats, the ISL (and
 * bspec) names are in LSB -> MSB order while VK formats are MSB -> LSB.
 */
static const struct anv_format anv_formats[] = {
   fmt(VK_FORMAT_UNDEFINED,               ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_R4G4_UNORM_PACK8,        ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_R4G4B4A4_UNORM_PACK16,   ISL_FORMAT_A4B4G4R4_UNORM),
   swiz_fmt(VK_FORMAT_B4G4R4A4_UNORM_PACK16,   ISL_FORMAT_A4B4G4R4_UNORM,  BGRA),
   fmt(VK_FORMAT_R5G6B5_UNORM_PACK16,     ISL_FORMAT_B5G6R5_UNORM),
   swiz_fmt(VK_FORMAT_B5G6R5_UNORM_PACK16,     ISL_FORMAT_B5G6R5_UNORM, BGRA),
   fmt(VK_FORMAT_R5G5B5A1_UNORM_PACK16,   ISL_FORMAT_A1B5G5R5_UNORM),
   fmt(VK_FORMAT_B5G5R5A1_UNORM_PACK16,   ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_A1R5G5B5_UNORM_PACK16,   ISL_FORMAT_B5G5R5A1_UNORM),
   fmt(VK_FORMAT_R8_UNORM,                ISL_FORMAT_R8_UNORM),
   fmt(VK_FORMAT_R8_SNORM,                ISL_FORMAT_R8_SNORM),
   fmt(VK_FORMAT_R8_USCALED,              ISL_FORMAT_R8_USCALED),
   fmt(VK_FORMAT_R8_SSCALED,              ISL_FORMAT_R8_SSCALED),
   fmt(VK_FORMAT_R8_UINT,                 ISL_FORMAT_R8_UINT),
   fmt(VK_FORMAT_R8_SINT,                 ISL_FORMAT_R8_SINT),
   fmt(VK_FORMAT_R8_SRGB,                 ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_R8G8_UNORM,              ISL_FORMAT_R8G8_UNORM),
   fmt(VK_FORMAT_R8G8_SNORM,              ISL_FORMAT_R8G8_SNORM),
   fmt(VK_FORMAT_R8G8_USCALED,            ISL_FORMAT_R8G8_USCALED),
   fmt(VK_FORMAT_R8G8_SSCALED,            ISL_FORMAT_R8G8_SSCALED),
   fmt(VK_FORMAT_R8G8_UINT,               ISL_FORMAT_R8G8_UINT),
   fmt(VK_FORMAT_R8G8_SINT,               ISL_FORMAT_R8G8_SINT),
   fmt(VK_FORMAT_R8G8_SRGB,               ISL_FORMAT_UNSUPPORTED), /* L8A8_UNORM_SRGB */
   fmt(VK_FORMAT_R8G8B8_UNORM,            ISL_FORMAT_R8G8B8_UNORM),
   fmt(VK_FORMAT_R8G8B8_SNORM,            ISL_FORMAT_R8G8B8_SNORM),
   fmt(VK_FORMAT_R8G8B8_USCALED,          ISL_FORMAT_R8G8B8_USCALED),
   fmt(VK_FORMAT_R8G8B8_SSCALED,          ISL_FORMAT_R8G8B8_SSCALED),
   fmt(VK_FORMAT_R8G8B8_UINT,             ISL_FORMAT_R8G8B8_UINT),
   fmt(VK_FORMAT_R8G8B8_SINT,             ISL_FORMAT_R8G8B8_SINT),
   fmt(VK_FORMAT_R8G8B8_SRGB,             ISL_FORMAT_R8G8B8_UNORM_SRGB),
   fmt(VK_FORMAT_R8G8B8A8_UNORM,          ISL_FORMAT_R8G8B8A8_UNORM),
   fmt(VK_FORMAT_R8G8B8A8_SNORM,          ISL_FORMAT_R8G8B8A8_SNORM),
   fmt(VK_FORMAT_R8G8B8A8_USCALED,        ISL_FORMAT_R8G8B8A8_USCALED),
   fmt(VK_FORMAT_R8G8B8A8_SSCALED,        ISL_FORMAT_R8G8B8A8_SSCALED),
   fmt(VK_FORMAT_R8G8B8A8_UINT,           ISL_FORMAT_R8G8B8A8_UINT),
   fmt(VK_FORMAT_R8G8B8A8_SINT,           ISL_FORMAT_R8G8B8A8_SINT),
   fmt(VK_FORMAT_R8G8B8A8_SRGB,           ISL_FORMAT_R8G8B8A8_UNORM_SRGB),
   fmt(VK_FORMAT_A8B8G8R8_UNORM_PACK32,   ISL_FORMAT_R8G8B8A8_UNORM),
   fmt(VK_FORMAT_A8B8G8R8_SNORM_PACK32,   ISL_FORMAT_R8G8B8A8_SNORM),
   fmt(VK_FORMAT_A8B8G8R8_USCALED_PACK32, ISL_FORMAT_R8G8B8A8_USCALED),
   fmt(VK_FORMAT_A8B8G8R8_SSCALED_PACK32, ISL_FORMAT_R8G8B8A8_SSCALED),
   fmt(VK_FORMAT_A8B8G8R8_UINT_PACK32,    ISL_FORMAT_R8G8B8A8_UINT),
   fmt(VK_FORMAT_A8B8G8R8_SINT_PACK32,    ISL_FORMAT_R8G8B8A8_SINT),
   fmt(VK_FORMAT_A8B8G8R8_SRGB_PACK32,    ISL_FORMAT_R8G8B8A8_UNORM_SRGB),
   fmt(VK_FORMAT_A2R10G10B10_UNORM_PACK32, ISL_FORMAT_B10G10R10A2_UNORM),
   fmt(VK_FORMAT_A2R10G10B10_SNORM_PACK32, ISL_FORMAT_B10G10R10A2_SNORM),
   fmt(VK_FORMAT_A2R10G10B10_USCALED_PACK32, ISL_FORMAT_B10G10R10A2_USCALED),
   fmt(VK_FORMAT_A2R10G10B10_SSCALED_PACK32, ISL_FORMAT_B10G10R10A2_SSCALED),
   fmt(VK_FORMAT_A2R10G10B10_UINT_PACK32, ISL_FORMAT_B10G10R10A2_UINT),
   fmt(VK_FORMAT_A2R10G10B10_SINT_PACK32, ISL_FORMAT_B10G10R10A2_SINT),
   fmt(VK_FORMAT_A2B10G10R10_UNORM_PACK32, ISL_FORMAT_R10G10B10A2_UNORM),
   fmt(VK_FORMAT_A2B10G10R10_SNORM_PACK32, ISL_FORMAT_R10G10B10A2_SNORM),
   fmt(VK_FORMAT_A2B10G10R10_USCALED_PACK32, ISL_FORMAT_R10G10B10A2_USCALED),
   fmt(VK_FORMAT_A2B10G10R10_SSCALED_PACK32, ISL_FORMAT_R10G10B10A2_SSCALED),
   fmt(VK_FORMAT_A2B10G10R10_UINT_PACK32, ISL_FORMAT_R10G10B10A2_UINT),
   fmt(VK_FORMAT_A2B10G10R10_SINT_PACK32, ISL_FORMAT_R10G10B10A2_SINT),
   fmt(VK_FORMAT_R16_UNORM,               ISL_FORMAT_R16_UNORM),
   fmt(VK_FORMAT_R16_SNORM,               ISL_FORMAT_R16_SNORM),
   fmt(VK_FORMAT_R16_USCALED,             ISL_FORMAT_R16_USCALED),
   fmt(VK_FORMAT_R16_SSCALED,             ISL_FORMAT_R16_SSCALED),
   fmt(VK_FORMAT_R16_UINT,                ISL_FORMAT_R16_UINT),
   fmt(VK_FORMAT_R16_SINT,                ISL_FORMAT_R16_SINT),
   fmt(VK_FORMAT_R16_SFLOAT,              ISL_FORMAT_R16_FLOAT),
   fmt(VK_FORMAT_R16G16_UNORM,            ISL_FORMAT_R16G16_UNORM),
   fmt(VK_FORMAT_R16G16_SNORM,            ISL_FORMAT_R16G16_SNORM),
   fmt(VK_FORMAT_R16G16_USCALED,          ISL_FORMAT_R16G16_USCALED),
   fmt(VK_FORMAT_R16G16_SSCALED,          ISL_FORMAT_R16G16_SSCALED),
   fmt(VK_FORMAT_R16G16_UINT,             ISL_FORMAT_R16G16_UINT),
   fmt(VK_FORMAT_R16G16_SINT,             ISL_FORMAT_R16G16_SINT),
   fmt(VK_FORMAT_R16G16_SFLOAT,           ISL_FORMAT_R16G16_FLOAT),
   fmt(VK_FORMAT_R16G16B16_UNORM,         ISL_FORMAT_R16G16B16_UNORM),
   fmt(VK_FORMAT_R16G16B16_SNORM,         ISL_FORMAT_R16G16B16_SNORM),
   fmt(VK_FORMAT_R16G16B16_USCALED,       ISL_FORMAT_R16G16B16_USCALED),
   fmt(VK_FORMAT_R16G16B16_SSCALED,       ISL_FORMAT_R16G16B16_SSCALED),
   fmt(VK_FORMAT_R16G16B16_UINT,          ISL_FORMAT_R16G16B16_UINT),
   fmt(VK_FORMAT_R16G16B16_SINT,          ISL_FORMAT_R16G16B16_SINT),
   fmt(VK_FORMAT_R16G16B16_SFLOAT,        ISL_FORMAT_R16G16B16_FLOAT),
   fmt(VK_FORMAT_R16G16B16A16_UNORM,      ISL_FORMAT_R16G16B16A16_UNORM),
   fmt(VK_FORMAT_R16G16B16A16_SNORM,      ISL_FORMAT_R16G16B16A16_SNORM),
   fmt(VK_FORMAT_R16G16B16A16_USCALED,    ISL_FORMAT_R16G16B16A16_USCALED),
   fmt(VK_FORMAT_R16G16B16A16_SSCALED,    ISL_FORMAT_R16G16B16A16_SSCALED),
   fmt(VK_FORMAT_R16G16B16A16_UINT,       ISL_FORMAT_R16G16B16A16_UINT),
   fmt(VK_FORMAT_R16G16B16A16_SINT,       ISL_FORMAT_R16G16B16A16_SINT),
   fmt(VK_FORMAT_R16G16B16A16_SFLOAT,     ISL_FORMAT_R16G16B16A16_FLOAT),
   fmt(VK_FORMAT_R32_UINT,                ISL_FORMAT_R32_UINT),
   fmt(VK_FORMAT_R32_SINT,                ISL_FORMAT_R32_SINT),
   fmt(VK_FORMAT_R32_SFLOAT,              ISL_FORMAT_R32_FLOAT),
   fmt(VK_FORMAT_R32G32_UINT,             ISL_FORMAT_R32G32_UINT),
   fmt(VK_FORMAT_R32G32_SINT,             ISL_FORMAT_R32G32_SINT),
   fmt(VK_FORMAT_R32G32_SFLOAT,           ISL_FORMAT_R32G32_FLOAT),
   fmt(VK_FORMAT_R32G32B32_UINT,          ISL_FORMAT_R32G32B32_UINT),
   fmt(VK_FORMAT_R32G32B32_SINT,          ISL_FORMAT_R32G32B32_SINT),
   fmt(VK_FORMAT_R32G32B32_SFLOAT,        ISL_FORMAT_R32G32B32_FLOAT),
   fmt(VK_FORMAT_R32G32B32A32_UINT,       ISL_FORMAT_R32G32B32A32_UINT),
   fmt(VK_FORMAT_R32G32B32A32_SINT,       ISL_FORMAT_R32G32B32A32_SINT),
   fmt(VK_FORMAT_R32G32B32A32_SFLOAT,     ISL_FORMAT_R32G32B32A32_FLOAT),
   fmt(VK_FORMAT_R64_UINT,                ISL_FORMAT_R64_PASSTHRU),
   fmt(VK_FORMAT_R64_SINT,                ISL_FORMAT_R64_PASSTHRU),
   fmt(VK_FORMAT_R64_SFLOAT,              ISL_FORMAT_R64_PASSTHRU),
   fmt(VK_FORMAT_R64G64_UINT,             ISL_FORMAT_R64G64_PASSTHRU),
   fmt(VK_FORMAT_R64G64_SINT,             ISL_FORMAT_R64G64_PASSTHRU),
   fmt(VK_FORMAT_R64G64_SFLOAT,           ISL_FORMAT_R64G64_PASSTHRU),
   fmt(VK_FORMAT_R64G64B64_UINT,          ISL_FORMAT_R64G64B64_PASSTHRU),
   fmt(VK_FORMAT_R64G64B64_SINT,          ISL_FORMAT_R64G64B64_PASSTHRU),
   fmt(VK_FORMAT_R64G64B64_SFLOAT,        ISL_FORMAT_R64G64B64_PASSTHRU),
   fmt(VK_FORMAT_R64G64B64A64_UINT,       ISL_FORMAT_R64G64B64A64_PASSTHRU),
   fmt(VK_FORMAT_R64G64B64A64_SINT,       ISL_FORMAT_R64G64B64A64_PASSTHRU),
   fmt(VK_FORMAT_R64G64B64A64_SFLOAT,     ISL_FORMAT_R64G64B64A64_PASSTHRU),
   fmt(VK_FORMAT_B10G11R11_UFLOAT_PACK32, ISL_FORMAT_R11G11B10_FLOAT),
   fmt(VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,  ISL_FORMAT_R9G9B9E5_SHAREDEXP),

   fmt(VK_FORMAT_D16_UNORM,               ISL_FORMAT_R16_UNORM),
   fmt(VK_FORMAT_X8_D24_UNORM_PACK32,     ISL_FORMAT_R24_UNORM_X8_TYPELESS),
   fmt(VK_FORMAT_D32_SFLOAT,              ISL_FORMAT_R32_FLOAT),
   fmt(VK_FORMAT_S8_UINT,                 ISL_FORMAT_R8_UINT),
   fmt(VK_FORMAT_D16_UNORM_S8_UINT,       ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_D24_UNORM_S8_UINT,       ISL_FORMAT_R24_UNORM_X8_TYPELESS),
   fmt(VK_FORMAT_D32_SFLOAT_S8_UINT,      ISL_FORMAT_R32_FLOAT),

   fmt(VK_FORMAT_BC1_RGB_UNORM_BLOCK,     ISL_FORMAT_DXT1_RGB),
   fmt(VK_FORMAT_BC1_RGB_SRGB_BLOCK,      ISL_FORMAT_DXT1_RGB_SRGB),
   fmt(VK_FORMAT_BC1_RGBA_UNORM_BLOCK,    ISL_FORMAT_BC1_UNORM),
   fmt(VK_FORMAT_BC1_RGBA_SRGB_BLOCK,     ISL_FORMAT_BC1_UNORM_SRGB),
   fmt(VK_FORMAT_BC2_UNORM_BLOCK,         ISL_FORMAT_BC2_UNORM),
   fmt(VK_FORMAT_BC2_SRGB_BLOCK,          ISL_FORMAT_BC2_UNORM_SRGB),
   fmt(VK_FORMAT_BC3_UNORM_BLOCK,         ISL_FORMAT_BC3_UNORM),
   fmt(VK_FORMAT_BC3_SRGB_BLOCK,          ISL_FORMAT_BC3_UNORM_SRGB),
   fmt(VK_FORMAT_BC4_UNORM_BLOCK,         ISL_FORMAT_BC4_UNORM),
   fmt(VK_FORMAT_BC4_SNORM_BLOCK,         ISL_FORMAT_BC4_SNORM),
   fmt(VK_FORMAT_BC5_UNORM_BLOCK,         ISL_FORMAT_BC5_UNORM),
   fmt(VK_FORMAT_BC5_SNORM_BLOCK,         ISL_FORMAT_BC5_SNORM),
   fmt(VK_FORMAT_BC6H_UFLOAT_BLOCK,       ISL_FORMAT_BC6H_UF16),
   fmt(VK_FORMAT_BC6H_SFLOAT_BLOCK,       ISL_FORMAT_BC6H_SF16),
   fmt(VK_FORMAT_BC7_UNORM_BLOCK,         ISL_FORMAT_BC7_UNORM),
   fmt(VK_FORMAT_BC7_SRGB_BLOCK,          ISL_FORMAT_BC7_UNORM_SRGB),
   fmt(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK, ISL_FORMAT_ETC2_RGB8),
   fmt(VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,  ISL_FORMAT_ETC2_SRGB8),
   fmt(VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK, ISL_FORMAT_ETC2_RGB8_PTA),
   fmt(VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK, ISL_FORMAT_ETC2_SRGB8_PTA),
   fmt(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK, ISL_FORMAT_ETC2_EAC_RGBA8),
   fmt(VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK, ISL_FORMAT_ETC2_EAC_SRGB8_A8),
   fmt(VK_FORMAT_EAC_R11_UNORM_BLOCK,     ISL_FORMAT_EAC_R11),
   fmt(VK_FORMAT_EAC_R11_SNORM_BLOCK,     ISL_FORMAT_EAC_SIGNED_R11),
   fmt(VK_FORMAT_EAC_R11G11_UNORM_BLOCK,  ISL_FORMAT_EAC_RG11),
   fmt(VK_FORMAT_EAC_R11G11_SNORM_BLOCK,  ISL_FORMAT_EAC_SIGNED_RG11),
   fmt(VK_FORMAT_ASTC_4x4_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_4X4_U8SRGB),
   fmt(VK_FORMAT_ASTC_5x4_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_5X4_U8SRGB),
   fmt(VK_FORMAT_ASTC_5x5_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_5X5_U8SRGB),
   fmt(VK_FORMAT_ASTC_6x5_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_6X5_U8SRGB),
   fmt(VK_FORMAT_ASTC_6x6_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_6X6_U8SRGB),
   fmt(VK_FORMAT_ASTC_8x5_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_8X5_U8SRGB),
   fmt(VK_FORMAT_ASTC_8x6_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_8X6_U8SRGB),
   fmt(VK_FORMAT_ASTC_8x8_SRGB_BLOCK,     ISL_FORMAT_ASTC_LDR_2D_8X8_U8SRGB),
   fmt(VK_FORMAT_ASTC_10x5_SRGB_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_10X5_U8SRGB),
   fmt(VK_FORMAT_ASTC_10x6_SRGB_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_10X6_U8SRGB),
   fmt(VK_FORMAT_ASTC_10x8_SRGB_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_10X8_U8SRGB),
   fmt(VK_FORMAT_ASTC_10x10_SRGB_BLOCK,   ISL_FORMAT_ASTC_LDR_2D_10X10_U8SRGB),
   fmt(VK_FORMAT_ASTC_12x10_SRGB_BLOCK,   ISL_FORMAT_ASTC_LDR_2D_12X10_U8SRGB),
   fmt(VK_FORMAT_ASTC_12x12_SRGB_BLOCK,   ISL_FORMAT_ASTC_LDR_2D_12X12_U8SRGB),
   fmt(VK_FORMAT_ASTC_4x4_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_4X4_FLT16),
   fmt(VK_FORMAT_ASTC_5x4_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_5X4_FLT16),
   fmt(VK_FORMAT_ASTC_5x5_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_5X5_FLT16),
   fmt(VK_FORMAT_ASTC_6x5_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_6X5_FLT16),
   fmt(VK_FORMAT_ASTC_6x6_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_6X6_FLT16),
   fmt(VK_FORMAT_ASTC_8x5_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_8X5_FLT16),
   fmt(VK_FORMAT_ASTC_8x6_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_8X6_FLT16),
   fmt(VK_FORMAT_ASTC_8x8_UNORM_BLOCK,    ISL_FORMAT_ASTC_LDR_2D_8X8_FLT16),
   fmt(VK_FORMAT_ASTC_10x5_UNORM_BLOCK,   ISL_FORMAT_ASTC_LDR_2D_10X5_FLT16),
   fmt(VK_FORMAT_ASTC_10x6_UNORM_BLOCK,   ISL_FORMAT_ASTC_LDR_2D_10X6_FLT16),
   fmt(VK_FORMAT_ASTC_10x8_UNORM_BLOCK,   ISL_FORMAT_ASTC_LDR_2D_10X8_FLT16),
   fmt(VK_FORMAT_ASTC_10x10_UNORM_BLOCK,  ISL_FORMAT_ASTC_LDR_2D_10X10_FLT16),
   fmt(VK_FORMAT_ASTC_12x10_UNORM_BLOCK,  ISL_FORMAT_ASTC_LDR_2D_12X10_FLT16),
   fmt(VK_FORMAT_ASTC_12x12_UNORM_BLOCK,  ISL_FORMAT_ASTC_LDR_2D_12X12_FLT16),
   fmt(VK_FORMAT_B8G8R8_UNORM,            ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8_SNORM,            ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8_USCALED,          ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8_SSCALED,          ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8_UINT,             ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8_SINT,             ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8_SRGB,             ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8A8_UNORM,          ISL_FORMAT_B8G8R8A8_UNORM),
   fmt(VK_FORMAT_B8G8R8A8_SNORM,          ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8A8_USCALED,        ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8A8_SSCALED,        ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8A8_UINT,           ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8A8_SINT,           ISL_FORMAT_UNSUPPORTED),
   fmt(VK_FORMAT_B8G8R8A8_SRGB,           ISL_FORMAT_B8G8R8A8_UNORM_SRGB),
};

#undef fmt

/**
 * Exactly one bit must be set in \a aspect.
 */
struct anv_format
anv_get_format(const struct gen_device_info *devinfo, VkFormat vk_format,
               VkImageAspectFlags aspect, VkImageTiling tiling)
{
   struct anv_format format = anv_formats[vk_format];

   if (format.isl_format == ISL_FORMAT_UNSUPPORTED)
      return format;

   if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
      assert(vk_format_aspects(vk_format) & VK_IMAGE_ASPECT_STENCIL_BIT);
      format.isl_format = ISL_FORMAT_R8_UINT;
      return format;
   }

   if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
      assert(vk_format_aspects(vk_format) & VK_IMAGE_ASPECT_DEPTH_BIT);
      return format;
   }

   assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
   assert(vk_format_aspects(vk_format) == VK_IMAGE_ASPECT_COLOR_BIT);

   const struct isl_format_layout *isl_layout =
      isl_format_get_layout(format.isl_format);

   if (tiling == VK_IMAGE_TILING_OPTIMAL &&
       !util_is_power_of_two(isl_layout->bpb)) {
      /* Tiled formats *must* be power-of-two because we need up upload
       * them with the render pipeline.  For 3-channel formats, we fix
       * this by switching them over to RGBX or RGBA formats under the
       * hood.
       */
      enum isl_format rgbx = isl_format_rgb_to_rgbx(format.isl_format);
      if (rgbx != ISL_FORMAT_UNSUPPORTED &&
          isl_format_supports_rendering(devinfo, rgbx)) {
         format.isl_format = rgbx;
      } else {
         format.isl_format = isl_format_rgb_to_rgba(format.isl_format);
         format.swizzle = ISL_SWIZZLE(RED, GREEN, BLUE, ONE);
      }
   }

   /* The B4G4R4A4 format isn't available prior to Broadwell so we have to fall
    * back to a format with a more complex swizzle.
    */
   if (vk_format == VK_FORMAT_B4G4R4A4_UNORM_PACK16 && devinfo->gen < 8) {
      return (struct anv_format) {
         .isl_format = ISL_FORMAT_B4G4R4A4_UNORM,
         .swizzle = ISL_SWIZZLE(GREEN, RED, ALPHA, BLUE),
      };
   }

   return format;
}

// Format capabilities

static VkFormatFeatureFlags
get_image_format_properties(const struct gen_device_info *devinfo,
                            enum isl_format base, struct anv_format format)
{
   if (format.isl_format == ISL_FORMAT_UNSUPPORTED)
      return 0;

   VkFormatFeatureFlags flags = 0;
   if (isl_format_supports_sampling(devinfo, format.isl_format)) {
      flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_BLIT_SRC_BIT;

      if (isl_format_supports_filtering(devinfo, format.isl_format))
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
   }

   /* We can render to swizzled formats.  However, if the alpha channel is
    * moved, then blending won't work correctly.  The PRM tells us
    * straight-up not to render to such a surface.
    */
   if (isl_format_supports_rendering(devinfo, format.isl_format) &&
       format.swizzle.a == ISL_CHANNEL_SELECT_ALPHA) {
      flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
               VK_FORMAT_FEATURE_BLIT_DST_BIT;

      if (isl_format_supports_alpha_blending(devinfo, format.isl_format))
         flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
   }

   /* Load/store is determined based on base format.  This prevents RGB
    * formats from showing up as load/store capable.
    */
   if (isl_is_storage_image_format(base))
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;

   if (base == ISL_FORMAT_R32_SINT || base == ISL_FORMAT_R32_UINT)
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;

   return flags;
}

static VkFormatFeatureFlags
get_buffer_format_properties(const struct gen_device_info *devinfo,
                             enum isl_format format)
{
   if (format == ISL_FORMAT_UNSUPPORTED)
      return 0;

   VkFormatFeatureFlags flags = 0;
   if (isl_format_supports_sampling(devinfo, format) &&
       !isl_format_is_compressed(format))
      flags |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;

   if (isl_format_supports_vertex_fetch(devinfo, format))
      flags |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;

   if (isl_is_storage_image_format(format))
      flags |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;

   if (format == ISL_FORMAT_R32_SINT || format == ISL_FORMAT_R32_UINT)
      flags |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;

   return flags;
}

static void
anv_physical_device_get_format_properties(struct anv_physical_device *physical_device,
                                          VkFormat format,
                                          VkFormatProperties *out_properties)
{
   int gen = physical_device->info.gen * 10;
   if (physical_device->info.is_haswell)
      gen += 5;

   VkFormatFeatureFlags linear = 0, tiled = 0, buffer = 0;
   if (anv_formats[format].isl_format == ISL_FORMAT_UNSUPPORTED) {
      /* Nothing to do here */
   } else if (vk_format_is_depth_or_stencil(format)) {
      tiled |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
      if (physical_device->info.gen >= 8)
         tiled |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

      tiled |= VK_FORMAT_FEATURE_BLIT_SRC_BIT |
               VK_FORMAT_FEATURE_BLIT_DST_BIT;
   } else {
      struct anv_format linear_fmt, tiled_fmt;
      linear_fmt = anv_get_format(&physical_device->info, format,
                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                  VK_IMAGE_TILING_LINEAR);
      tiled_fmt = anv_get_format(&physical_device->info, format,
                                 VK_IMAGE_ASPECT_COLOR_BIT,
                                 VK_IMAGE_TILING_OPTIMAL);

      linear = get_image_format_properties(&physical_device->info,
                                           linear_fmt.isl_format, linear_fmt);
      tiled = get_image_format_properties(&physical_device->info,
                                          linear_fmt.isl_format, tiled_fmt);
      buffer = get_buffer_format_properties(&physical_device->info,
                                            linear_fmt.isl_format);

      /* XXX: We handle 3-channel formats by switching them out for RGBX or
       * RGBA formats behind-the-scenes.  This works fine for textures
       * because the upload process will fill in the extra channel.
       * We could also support it for render targets, but it will take
       * substantially more work and we have enough RGBX formats to handle
       * what most clients will want.
       */
      if (linear_fmt.isl_format != ISL_FORMAT_UNSUPPORTED &&
          !util_is_power_of_two(isl_format_layouts[linear_fmt.isl_format].bpb) &&
          isl_format_rgb_to_rgbx(linear_fmt.isl_format) == ISL_FORMAT_UNSUPPORTED) {
         tiled &= ~VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT &
                  ~VK_FORMAT_FEATURE_BLIT_DST_BIT;
      }

      /* ASTC textures must be in Y-tiled memory */
      if (isl_format_get_layout(linear_fmt.isl_format)->txc == ISL_TXC_ASTC)
         linear = 0;
   }

   out_properties->linearTilingFeatures = linear;
   out_properties->optimalTilingFeatures = tiled;
   out_properties->bufferFeatures = buffer;

   return;
}


void anv_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);

   anv_physical_device_get_format_properties(
               physical_device,
               format,
               pFormatProperties);
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
   VkFormatProperties format_props;
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;

   if (anv_formats[format].isl_format == ISL_FORMAT_UNSUPPORTED)
      goto unsupported;

   anv_physical_device_get_format_properties(physical_device, format,
                                             &format_props);

   /* Extract the VkFormatFeatureFlags that are relevant for the queried
    * tiling.
    */
   if (tiling == VK_IMAGE_TILING_LINEAR) {
      format_feature_flags = format_props.linearTilingFeatures;
   } else if (tiling == VK_IMAGE_TILING_OPTIMAL) {
      format_feature_flags = format_props.optimalTilingFeatures;
   } else {
      unreachable("bad VkImageTiling");
   }

   switch (type) {
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
   if (type == VK_IMAGE_TYPE_1D &&
       isl_format_is_compressed(anv_formats[format].isl_format)) {
       goto unsupported;
   }

   if (tiling == VK_IMAGE_TILING_OPTIMAL &&
       type == VK_IMAGE_TYPE_2D &&
       (format_feature_flags & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(createFlags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      sampleCounts = isl_device_get_sample_counts(&physical_device->isl_dev);
   }

   if (usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
      /* Accept transfers on anything we can sample from or renderer to. */
      if (!(format_feature_flags & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))) {
         goto unsupported;
      }
   }

   if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) {
      /* Nothing to check. */
   }

   if (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      /* Ignore this flag because it was removed from the
       * provisional_I_20150910 header.
       */
   }

   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* FINISHME: Accurately calculate
       * VkImageFormatProperties::maxResourceSize.
       */
      .maxResourceSize = UINT32_MAX,
   };

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
