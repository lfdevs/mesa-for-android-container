/*
 * Copyright 2016 Intel Corporation
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

#ifndef ISL_SURFACE_STATE_H
#define ISL_SURFACE_STATE_H

#include <stdint.h>

/**
 * @file isl_surface_state.h
 *
 * ============================= GENXML CODE =============================
 *              [This file is compiled once per generation.]
 * =======================================================================
 *
 * Helpers for encoding SURFACE_STATE and XY_BLOCK_COPY_BLT commands.
 */

UNUSED static uint8_t
isl_encode_halign(uint8_t halign)
{
   switch (halign) {
#if GFX_VERx10 >= 125
   case  16: return HALIGN_16;
   case  32: return HALIGN_32;
   case  64: return HALIGN_64;
   case 128: return HALIGN_128;
#elif GFX_VER >= 8
   case   4: return HALIGN_4;
   case   8: return HALIGN_8;
   case  16: return HALIGN_16;
#elif GFX_VER >= 7
   case   4: return HALIGN_4;
   case   8: return HALIGN_8;
#endif
   default: UNREACHABLE("Invalid halign");
   }
}

UNUSED static uint8_t
isl_encode_valign(uint8_t valign)
{
   switch (valign) {
#if GFX_VER >= 8
   case   4: return VALIGN_4;
   case   8: return VALIGN_8;
   case  16: return VALIGN_16;
#elif GFX_VER >= 6
   case   2: return VALIGN_2;
   case   4: return VALIGN_4;
#endif
   default: UNREACHABLE("Invalid valign");
   }
}

/**
 * Get the horizontal and vertical alignment in the units expected by the
 * hardware.  Note that this does NOT give you the actual hardware enum values
 * but an index into the isl_encode_[hv]align arrays above.
 */
UNUSED static struct isl_extent3d
isl_get_image_alignment(const struct isl_surf *surf)
{
   if (GFX_VERx10 >= 125) {
      if (isl_tiling_is_64(surf->tiling)) {
         /* The hardware ignores the alignment values. Anyway, the surface's
          * true alignment is likely outside the enum range of HALIGN* and
          * VALIGN*.
          */
         return isl_extent3d(128, 4, 1);
      } else if (isl_format_get_layout(surf->format)->bpb % 3 == 0) {
         /* On XeHP, RENDER_SURFACE_STATE.SurfaceHorizontalAlignment is in
          * units of elements for 24, 48, and 96 bpb formats.
          */
         return isl_surf_get_image_alignment_el(surf);
      } else {
         /* On XeHP, RENDER_SURFACE_STATE.SurfaceHorizontalAlignment is in
          * units of bytes for formats that are powers of two.
          */
         const uint32_t bs = isl_format_get_layout(surf->format)->bpb / 8;
         return isl_extent3d(surf->image_alignment_el.w * bs,
                             surf->image_alignment_el.h,
                             surf->image_alignment_el.d);
      }
   } else if (GFX_VER >= 9) {
      if (isl_tiling_is_std_y(surf->tiling) ||
          surf->dim_layout == ISL_DIM_LAYOUT_GFX9_1D) {
         /* The hardware ignores the alignment values. Anyway, the surface's
          * true alignment is likely outside the enum range of HALIGN* and
          * VALIGN*.
          */
         return isl_extent3d(4, 4, 1);
      } else {
         /* In Skylake, RENDER_SUFFACE_STATE.SurfaceVerticalAlignment is in units
          * of surface elements (not pixels nor samples). For compressed formats,
          * a "surface element" is defined as a compression block.  For example,
          * if SurfaceVerticalAlignment is VALIGN_4 and SurfaceFormat is an ETC2
          * format (ETC2 has a block height of 4), then the vertical alignment is
          * 4 compression blocks or, equivalently, 16 pixels.
          */
         return isl_surf_get_image_alignment_el(surf);
      }
   } else {
      /* Pre-Skylake, RENDER_SUFFACE_STATE.SurfaceVerticalAlignment is in
       * units of surface samples.  For example, if SurfaceVerticalAlignment
       * is VALIGN_4 and the surface is singlesampled, then for any surface
       * format (compressed or not) the vertical alignment is
       * 4 pixels.
       */
      return isl_surf_get_image_alignment_sa(surf);
   }
}

UNUSED static uint32_t
isl_get_qpitch(const struct isl_surf *surf)
{
   switch (surf->dim_layout) {
   default:
      UNREACHABLE("Bad isl_surf_dim");
   case ISL_DIM_LAYOUT_GFX4_2D:
      if (GFX_VER >= 9) {
         if (surf->dim == ISL_SURF_DIM_3D && surf->tiling == ISL_TILING_W) {
            /* This is rather annoying and completely undocumented.  It
             * appears that the hardware has a bug (or undocumented feature)
             * regarding stencil buffers most likely related to the way
             * W-tiling is handled as modified Y-tiling.  If you bind a 3-D
             * stencil buffer normally, and use texelFetch on it, the z or
             * array index will get implicitly multiplied by 2 for no obvious
             * reason.  The fix appears to be to divide qpitch by 2 for
             * W-tiled surfaces.
             */
            return isl_surf_get_array_pitch_el_rows(surf) / 2;
         } else {
            return isl_surf_get_array_pitch_el_rows(surf);
         }
      } else {
         /* From the Broadwell PRM for RENDER_SURFACE_STATE.QPitch
          *
          *    "This field must be set to an integer multiple of the Surface
          *    Vertical Alignment. For compressed textures (BC*, FXT1,
          *    ETC*, and EAC* Surface Formats), this field is in units of
          *    rows in the uncompressed surface, and must be set to an
          *    integer multiple of the vertical alignment parameter "j"
          *    defined in the Common Surface Formats section."
          */
         return isl_surf_get_array_pitch_sa_rows(surf);
      }
   case ISL_DIM_LAYOUT_GFX9_1D:
      /* QPitch is usually expressed as rows of surface elements (where
       * a surface element is an compression block or a single surface
       * sample). Skylake 1D is an outlier.
       *
       * From the Skylake BSpec >> Memory Views >> Common Surface
       * Formats >> Surface Layout and Tiling >> 1D Surfaces:
       *
       *    Surface QPitch specifies the distance in pixels between array
       *    slices.
       */
      return isl_surf_get_array_pitch_el(surf);
   case ISL_DIM_LAYOUT_GFX4_3D:
      /* QPitch doesn't make sense for ISL_DIM_LAYOUT_GFX4_3D since it uses a
       * different pitch at each LOD.  Also, the QPitch field is ignored for
       * these surfaces.  From the Broadwell PRM documentation for QPitch:
       *
       *    This field specifies the distance in rows between array slices. It
       *    is used only in the following cases:
       *     - Surface Array is enabled OR
       *     - Number of Mulitsamples is not NUMSAMPLES_1 and Multisampled
       *       Surface Storage Format set to MSFMT_MSS OR
       *     - Surface Type is SURFTYPE_CUBE
       *
       * None of the three conditions above can possibly apply to a 3D surface
       * so it is safe to just set QPitch to 0.
       */
      return 0;
   }
}

#if GFX_VER >= 20

/*
 * Returns compression format encoding for Unified Lossless Compression
 */
UNUSED static uint8_t
isl_get_render_compression_format(enum isl_format format)
{
   /* The spec gives no CMFs to NPOT RGB formats. Remap them to RGBA. */
   if (isl_format_get_layout(format)->bpb % 3 == 0)
      format = isl_format_rgb_to_rgba(format);

   /* Bspec 63919 (r60413):
    *
    *   Table "[Enumeration] UNIFIED_COMPRESSION_FORMAT"
    *
    * These ISL formats have explicitly defined CMF values in the spec.
    */
   switch(format) {
   case ISL_FORMAT_R8_UNORM:
   case ISL_FORMAT_R8_UINT:
   case ISL_FORMAT_R8_SNORM:
   case ISL_FORMAT_R8_SINT:
   case ISL_FORMAT_A8_UNORM:
   case ISL_FORMAT_R24_UNORM_X8_TYPELESS:
      return CMF_R8;
   case ISL_FORMAT_R8G8_UNORM:
   case ISL_FORMAT_R8G8_UINT:
   case ISL_FORMAT_R8G8_SNORM:
   case ISL_FORMAT_R8G8_SINT:
   case ISL_FORMAT_B5G6R5_UNORM:
   case ISL_FORMAT_B5G6R5_UNORM_SRGB:
   case ISL_FORMAT_B5G5R5A1_UNORM:
   case ISL_FORMAT_B5G5R5A1_UNORM_SRGB:
   case ISL_FORMAT_B4G4R4A4_UNORM:
   case ISL_FORMAT_B4G4R4A4_UNORM_SRGB:
   case ISL_FORMAT_B5G5R5X1_UNORM:
   case ISL_FORMAT_B5G5R5X1_UNORM_SRGB:
   case ISL_FORMAT_A1B5G5R5_UNORM:
   case ISL_FORMAT_A4B4G4R4_UNORM:
      return CMF_R8_G8;
   case ISL_FORMAT_R8G8B8A8_UNORM:
   case ISL_FORMAT_R8G8B8A8_UINT:
   case ISL_FORMAT_R8G8B8A8_SNORM:
   case ISL_FORMAT_R8G8B8A8_SINT:
   case ISL_FORMAT_R8G8B8A8_UNORM_SRGB:
   case ISL_FORMAT_B8G8R8A8_UNORM_SRGB:
   case ISL_FORMAT_B8G8R8A8_UNORM:
   case ISL_FORMAT_B8G8R8X8_UNORM:
      return CMF_R8_G8_B8_A8;
   case ISL_FORMAT_R10G10B10A2_UNORM:
   case ISL_FORMAT_R10G10B10A2_UNORM_SRGB:
   case ISL_FORMAT_R10G10B10_FLOAT_A2_UNORM:
   case ISL_FORMAT_R10G10B10A2_UINT:
   case ISL_FORMAT_B10G10R10A2_UNORM:
   case ISL_FORMAT_B10G10R10X2_UNORM:
   case ISL_FORMAT_B10G10R10A2_UNORM_SRGB:
      return CMF_R10_G10_B10_A2;
   case ISL_FORMAT_R11G11B10_FLOAT:
      return CMF_R11_G11_B10;
   case ISL_FORMAT_R16_UNORM:
   case ISL_FORMAT_R16_UINT:
   case ISL_FORMAT_R16_SNORM:
   case ISL_FORMAT_R16_SINT:
   case ISL_FORMAT_R16_FLOAT:
      return CMF_R16;
   case ISL_FORMAT_R16G16_UNORM:
   case ISL_FORMAT_R16G16_UINT:
   case ISL_FORMAT_R16G16_SNORM:
   case ISL_FORMAT_R16G16_SINT:
   case ISL_FORMAT_R16G16_FLOAT:
      return CMF_R16_G16;
   case ISL_FORMAT_R16G16B16A16_UNORM:
   case ISL_FORMAT_R16G16B16A16_UINT:
   case ISL_FORMAT_R16G16B16A16_SNORM:
   case ISL_FORMAT_R16G16B16A16_SINT:
   case ISL_FORMAT_R16G16B16A16_FLOAT:
   case ISL_FORMAT_R16G16B16X16_FLOAT:
      return CMF_R16_G16_B16_A16;
   case ISL_FORMAT_R32_UNORM:
   case ISL_FORMAT_R32_UINT:
   case ISL_FORMAT_R32_SNORM:
   case ISL_FORMAT_R32_SINT:
   case ISL_FORMAT_R32_FLOAT:
      return CMF_R32;
   case ISL_FORMAT_R32G32_UNORM:
   case ISL_FORMAT_R32G32_UINT:
   case ISL_FORMAT_R32G32_SNORM:
   case ISL_FORMAT_R32G32_SINT:
   case ISL_FORMAT_R32G32_FLOAT:
      return CMF_R32_G32;
   case ISL_FORMAT_R32G32B32A32_UNORM:
   case ISL_FORMAT_R32G32B32A32_UINT:
   case ISL_FORMAT_R32G32B32A32_SNORM:
   case ISL_FORMAT_R32G32B32A32_SINT:
   case ISL_FORMAT_R32G32B32A32_FLOAT:
   case ISL_FORMAT_R32G32B32X32_FLOAT:
      return CMF_R32_G32_B32_A32;
   case ISL_FORMAT_EAC_R11:
   case ISL_FORMAT_EAC_RG11:
   case ISL_FORMAT_EAC_SIGNED_R11:
   case ISL_FORMAT_EAC_SIGNED_RG11:
   case ISL_FORMAT_ETC1_RGB8:
   case ISL_FORMAT_ETC2_RGB8:
   case ISL_FORMAT_ETC2_SRGB8:
   case ISL_FORMAT_ETC2_RGB8_PTA:
   case ISL_FORMAT_ETC2_SRGB8_PTA:
   case ISL_FORMAT_ETC2_EAC_RGBA8:
   case ISL_FORMAT_ETC2_EAC_SRGB8_A8:
   case ISL_FORMAT_BC1_UNORM:
   case ISL_FORMAT_BC2_UNORM:
   case ISL_FORMAT_BC3_UNORM:
   case ISL_FORMAT_BC4_UNORM:
   case ISL_FORMAT_BC5_UNORM:
   case ISL_FORMAT_BC1_UNORM_SRGB:
   case ISL_FORMAT_BC2_UNORM_SRGB:
   case ISL_FORMAT_BC3_UNORM_SRGB:
   case ISL_FORMAT_BC4_SNORM:
   case ISL_FORMAT_BC5_SNORM:
   case ISL_FORMAT_BC6H_SF16:
   case ISL_FORMAT_BC7_UNORM:
   case ISL_FORMAT_BC7_UNORM_SRGB:
   case ISL_FORMAT_BC6H_UF16:
      return CMF_ML8;
   /* These formats are not in the CMF table in the Bspec 63919 (r60413).
    * We choose CMF values for them by their number of channels x channel bit
    * size.
    */
   case ISL_FORMAT_L8A8_UNORM_SRGB:
      return CMF_R8_G8;
   case ISL_FORMAT_L8_UNORM_SRGB:
      return CMF_R8;
   case ISL_FORMAT_R9G9B9E5_SHAREDEXP:
      return CMF_R11_G11_B10;
   default:
      UNREACHABLE("Unsupported render compression format!");
      return 0;
   }
}

#elif GFX_VERx10 >= 125

/*
 * Returns compression format encoding for Unified Lossless Compression
 */
UNUSED static uint8_t
isl_get_render_compression_format(enum isl_format format)
{
   /* From the Bspec, Enumeration_RenderCompressionFormat section (53726): */
   switch(format) {
   case ISL_FORMAT_R32G32B32A32_FLOAT:
   case ISL_FORMAT_R32G32B32X32_FLOAT:
   case ISL_FORMAT_R32G32B32A32_SINT:
      return 0x0;
   case ISL_FORMAT_R32G32B32A32_UINT:
      return 0x1;
   case ISL_FORMAT_R32G32_FLOAT:
   case ISL_FORMAT_R32G32_SINT:
      return 0x2;
   case ISL_FORMAT_R32G32_UINT:
      return 0x3;
   case ISL_FORMAT_R16G16B16A16_UNORM:
   case ISL_FORMAT_R16G16B16X16_UNORM:
   case ISL_FORMAT_R16G16B16A16_UINT:
      return 0x4;
   case ISL_FORMAT_R16G16B16A16_SNORM:
   case ISL_FORMAT_R16G16B16A16_SINT:
   case ISL_FORMAT_R16G16B16A16_FLOAT:
   case ISL_FORMAT_R16G16B16X16_FLOAT:
      return 0x5;
   case ISL_FORMAT_R16G16_UNORM:
   case ISL_FORMAT_R16G16_UINT:
      return 0x6;
   case ISL_FORMAT_R16G16_SNORM:
   case ISL_FORMAT_R16G16_SINT:
   case ISL_FORMAT_R16G16_FLOAT:
      return 0x7;
   case ISL_FORMAT_B8G8R8A8_UNORM:
   case ISL_FORMAT_B8G8R8X8_UNORM:
   case ISL_FORMAT_B8G8R8A8_UNORM_SRGB:
   case ISL_FORMAT_B8G8R8X8_UNORM_SRGB:
   case ISL_FORMAT_R8G8B8A8_UNORM:
   case ISL_FORMAT_R8G8B8X8_UNORM:
   case ISL_FORMAT_R8G8B8A8_UNORM_SRGB:
   case ISL_FORMAT_R8G8B8X8_UNORM_SRGB:
   case ISL_FORMAT_R8G8B8A8_UINT:
      return 0x8;
   case ISL_FORMAT_R8G8B8A8_SNORM:
   case ISL_FORMAT_R8G8B8A8_SINT:
      return 0x9;
   case ISL_FORMAT_B5G6R5_UNORM:
   case ISL_FORMAT_B5G6R5_UNORM_SRGB:
   case ISL_FORMAT_B5G5R5A1_UNORM:
   case ISL_FORMAT_B5G5R5A1_UNORM_SRGB:
   case ISL_FORMAT_B4G4R4A4_UNORM:
   case ISL_FORMAT_B4G4R4A4_UNORM_SRGB:
   case ISL_FORMAT_B5G5R5X1_UNORM:
   case ISL_FORMAT_B5G5R5X1_UNORM_SRGB:
   case ISL_FORMAT_A1B5G5R5_UNORM:
   case ISL_FORMAT_A4B4G4R4_UNORM:
   case ISL_FORMAT_R8G8_UNORM:
   case ISL_FORMAT_R8G8_UINT:
      return 0xA;
   case ISL_FORMAT_R8G8_SNORM:
   case ISL_FORMAT_R8G8_SINT:
      return 0xB;
   case ISL_FORMAT_R10G10B10A2_UNORM:
   case ISL_FORMAT_R10G10B10A2_UNORM_SRGB:
   case ISL_FORMAT_R10G10B10_FLOAT_A2_UNORM:
   case ISL_FORMAT_R10G10B10A2_UINT:
   case ISL_FORMAT_B10G10R10A2_UNORM:
   case ISL_FORMAT_B10G10R10X2_UNORM:
   case ISL_FORMAT_B10G10R10A2_UNORM_SRGB:
      return 0xC;
   case ISL_FORMAT_R11G11B10_FLOAT:
      return 0xD;
   case ISL_FORMAT_R32_SINT:
   case ISL_FORMAT_R32_FLOAT:
      return 0x10;
   case ISL_FORMAT_R32_UINT:
   case ISL_FORMAT_R24_UNORM_X8_TYPELESS:
      return 0x11;
   case ISL_FORMAT_R16_UNORM:
   case ISL_FORMAT_R16_UINT:
      return 0x14;
   case ISL_FORMAT_R16_SNORM:
   case ISL_FORMAT_R16_SINT:
   case ISL_FORMAT_R16_FLOAT:
      return 0x15;
   case ISL_FORMAT_R8_UNORM:
   case ISL_FORMAT_R8_UINT:
   case ISL_FORMAT_A8_UNORM:
      return 0x18;
   case ISL_FORMAT_R8_SNORM:
   case ISL_FORMAT_R8_SINT:
      return 0x19;
   default:
      UNREACHABLE("Unsupported render compression format!");
      return 0;
   }
}

#endif /* #if GFX_VERx10 >= 125 */

#endif
