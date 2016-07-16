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

#include <stdint.h>

#define __gen_address_type uint64_t
#define __gen_user_data void

static inline uint64_t
__gen_combine_address(void *data, void *loc, uint64_t addr, uint32_t delta)
{
   return addr + delta;
}

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

#include "isl_priv.h"

#define __PASTE2(x, y) x ## y
#define __PASTE(x, y) __PASTE2(x, y)
#define isl_genX(x) __PASTE(isl_, genX(x))

#if GEN_GEN >= 8
static const uint8_t isl_to_gen_halign[] = {
    [4] = HALIGN4,
    [8] = HALIGN8,
    [16] = HALIGN16,
};
#elif GEN_GEN >= 7
static const uint8_t isl_to_gen_halign[] = {
    [4] = HALIGN_4,
    [8] = HALIGN_8,
};
#endif

#if GEN_GEN >= 8
static const uint8_t isl_to_gen_valign[] = {
    [4] = VALIGN4,
    [8] = VALIGN8,
    [16] = VALIGN16,
};
#elif GEN_GEN >= 6
static const uint8_t isl_to_gen_valign[] = {
    [2] = VALIGN_2,
    [4] = VALIGN_4,
};
#endif

#if GEN_GEN >= 8
static const uint8_t isl_to_gen_tiling[] = {
   [ISL_TILING_LINEAR]  = LINEAR,
   [ISL_TILING_X]       = XMAJOR,
   [ISL_TILING_Y0]      = YMAJOR,
   [ISL_TILING_Yf]      = YMAJOR,
   [ISL_TILING_Ys]      = YMAJOR,
   [ISL_TILING_W]       = WMAJOR,
};
#endif

static const uint32_t isl_to_gen_multisample_layout[] = {
   [ISL_MSAA_LAYOUT_NONE]           = MSFMT_MSS,
   [ISL_MSAA_LAYOUT_INTERLEAVED]    = MSFMT_DEPTH_STENCIL,
   [ISL_MSAA_LAYOUT_ARRAY]          = MSFMT_MSS,
};

static uint8_t
get_surftype(enum isl_surf_dim dim, isl_surf_usage_flags_t usage)
{
   switch (dim) {
   default:
      unreachable("bad isl_surf_dim");
   case ISL_SURF_DIM_1D:
      assert(!(usage & ISL_SURF_USAGE_CUBE_BIT));
      return SURFTYPE_1D;
   case ISL_SURF_DIM_2D:
      if (usage & ISL_SURF_USAGE_STORAGE_BIT) {
         /* Storage images are always plain 2-D, not cube */
         return SURFTYPE_2D;
      } else if (usage & ISL_SURF_USAGE_CUBE_BIT) {
         return SURFTYPE_CUBE;
      } else {
         return SURFTYPE_2D;
      }
   case ISL_SURF_DIM_3D:
      assert(!(usage & ISL_SURF_USAGE_CUBE_BIT));
      return SURFTYPE_3D;
   }
}

/**
 * Get the horizontal and vertical alignment in the units expected by the
 * hardware.  Note that this does NOT give you the actual hardware enum values
 * but an index into the isl_to_gen_[hv]align arrays above.
 */
static struct isl_extent3d
get_image_alignment(const struct isl_surf *surf)
{
   if (GEN_GEN >= 9) {
      if (isl_tiling_is_std_y(surf->tiling) ||
          surf->dim_layout == ISL_DIM_LAYOUT_GEN9_1D) {
         /* The hardware ignores the alignment values. Anyway, the surface's
          * true alignment is likely outside the enum range of HALIGN* and
          * VALIGN*.
          */
         return isl_extent3d(0, 0, 0);
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

#if GEN_GEN >= 8
static uint32_t
get_qpitch(const struct isl_surf *surf)
{
   switch (surf->dim_layout) {
   default:
      unreachable("Bad isl_surf_dim");
   case ISL_DIM_LAYOUT_GEN4_2D:
   case ISL_DIM_LAYOUT_GEN4_3D:
      if (GEN_GEN >= 9) {
         return isl_surf_get_array_pitch_el_rows(surf);
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
   case ISL_DIM_LAYOUT_GEN9_1D:
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
   }
}
#endif /* GEN_GEN >= 8 */

void
isl_genX(surf_fill_state_s)(const struct isl_device *dev, void *state,
                            const struct isl_surf_fill_state_info *restrict info)
{
   struct GENX(RENDER_SURFACE_STATE) s = { 0 };

   s.SurfaceType = get_surftype(info->surf->dim, info->view->usage);
   s.SurfaceFormat = info->view->format;

#if GEN_IS_HASWELL
   s.IntegerSurfaceFormat = isl_format_has_int_channel(s.SurfaceFormat);
#endif

   s.Width = info->surf->logical_level0_px.width - 1;
   s.Height = info->surf->logical_level0_px.height - 1;

   switch (s.SurfaceType) {
   case SURFTYPE_1D:
   case SURFTYPE_2D:
      s.MinimumArrayElement = info->view->base_array_layer;

      /* From the Broadwell PRM >> RENDER_SURFACE_STATE::Depth:
       *
       *    For SURFTYPE_1D, 2D, and CUBE: The range of this field is reduced
       *    by one for each increase from zero of Minimum Array Element. For
       *    example, if Minimum Array Element is set to 1024 on a 2D surface,
       *    the range of this field is reduced to [0,1023].
       *
       * In other words, 'Depth' is the number of array layers.
       */
      s.Depth = info->view->array_len - 1;

      /* From the Broadwell PRM >> RENDER_SURFACE_STATE::RenderTargetViewExtent:
       *
       *    For Render Target and Typed Dataport 1D and 2D Surfaces:
       *    This field must be set to the same value as the Depth field.
       */
      if (info->view->usage & (ISL_SURF_USAGE_RENDER_TARGET_BIT |
                               ISL_SURF_USAGE_STORAGE_BIT))
         s.RenderTargetViewExtent = s.Depth;
      break;
   case SURFTYPE_CUBE:
      s.MinimumArrayElement = info->view->base_array_layer;
      /* Same as SURFTYPE_2D, but divided by 6 */
      s.Depth = info->view->array_len / 6 - 1;
      if (info->view->usage & (ISL_SURF_USAGE_RENDER_TARGET_BIT |
                               ISL_SURF_USAGE_STORAGE_BIT))
         s.RenderTargetViewExtent = s.Depth;
      break;
   case SURFTYPE_3D:
      s.MinimumArrayElement = info->view->base_array_layer;

      /* From the Broadwell PRM >> RENDER_SURFACE_STATE::Depth:
       *
       *    If the volume texture is MIP-mapped, this field specifies the
       *    depth of the base MIP level.
       */
      s.Depth = info->surf->logical_level0_px.depth - 1;

      /* From the Broadwell PRM >> RENDER_SURFACE_STATE::RenderTargetViewExtent:
       *
       *    For Render Target and Typed Dataport 3D Surfaces: This field
       *    indicates the extent of the accessible 'R' coordinates minus 1 on
       *    the LOD currently being rendered to.
       *
       * The docs specify that this only matters for render targets and
       * surfaces used with typed dataport messages.  Prior to Ivy Bridge, the
       * Depth field has more bits than RenderTargetViewExtent so we can have
       * textures with more levels than we can render to.  In order to prevent
       * assert-failures in the packing function below, we only set the field
       * when it's actually going to be used by the hardware.
       */
      if (info->view->usage & (ISL_SURF_USAGE_RENDER_TARGET_BIT |
                               ISL_SURF_USAGE_STORAGE_BIT)) {
         s.RenderTargetViewExtent = isl_minify(info->surf->logical_level0_px.depth,
                                               info->view->base_level) - 1;
      }
      break;
   default:
      unreachable("bad SurfaceType");
   }

   s.SurfaceArray = info->surf->dim != ISL_SURF_DIM_3D;

   if (info->view->usage & ISL_SURF_USAGE_RENDER_TARGET_BIT) {
      /* For render target surfaces, the hardware interprets field
       * MIPCount/LOD as LOD. The Broadwell PRM says:
       *
       *    MIPCountLOD defines the LOD that will be rendered into.
       *    SurfaceMinLOD is ignored.
       */
      s.MIPCountLOD = info->view->base_level;
      s.SurfaceMinLOD = 0;
   } else {
      /* For non render target surfaces, the hardware interprets field
       * MIPCount/LOD as MIPCount.  The range of levels accessible by the
       * sampler engine is [SurfaceMinLOD, SurfaceMinLOD + MIPCountLOD].
       */
      s.SurfaceMinLOD = info->view->base_level;
      s.MIPCountLOD = MAX(info->view->levels, 1) - 1;
   }

#if GEN_GEN >= 9
   /* We don't use miptails yet.  The PRM recommends that you set "Mip Tail
    * Start LOD" to 15 to prevent the hardware from trying to use them.
    */
   s.TiledResourceMode = NONE;
   s.MipTailStartLOD = 15;
#endif

   const struct isl_extent3d image_align = get_image_alignment(info->surf);
   s.SurfaceVerticalAlignment = isl_to_gen_valign[image_align.height];
   s.SurfaceHorizontalAlignment = isl_to_gen_halign[image_align.width];

   if (info->surf->tiling == ISL_TILING_W) {
      /* From the Broadwell PRM documentation for this field:
       *
       *    "If the surface is a stencil buffer (and thus has Tile Mode set
       *    to TILEMODE_WMAJOR), the pitch must be set to 2x the value
       *    computed based on width, as the stencil buffer is stored with
       *    two rows interleaved."
       */
      s.SurfacePitch = info->surf->row_pitch * 2 - 1;
   } else if (info->surf->dim_layout == ISL_DIM_LAYOUT_GEN9_1D) {
      /* For gen9 1-D textures, surface pitch is ignored */
      s.SurfacePitch = 0;
   } else {
      s.SurfacePitch = info->surf->row_pitch - 1;
   }

#if GEN_GEN >= 8
   s.SurfaceQPitch = get_qpitch(info->surf) >> 2;
#elif GEN_GEN == 7
   s.SurfaceArraySpacing = info->surf->array_pitch_span ==
                           ISL_ARRAY_PITCH_SPAN_COMPACT;
#endif

#if GEN_GEN >= 8
   s.TileMode = isl_to_gen_tiling[info->surf->tiling];
#else
   s.TiledSurface = info->surf->tiling != ISL_TILING_LINEAR,
   s.TileWalk = info->surf->tiling == ISL_TILING_Y0 ? TILEWALK_YMAJOR :
                                                      TILEWALK_XMAJOR,
#endif

#if GEN_GEN >= 8
   s.RenderCacheReadWriteMode = WriteOnlyCache;
#else
   s.RenderCacheReadWriteMode = 0;
#endif

   if (info->view->usage & ISL_SURF_USAGE_CUBE_BIT) {
#if GEN_GEN >= 8
      s.CubeFaceEnablePositiveZ = 1;
      s.CubeFaceEnableNegativeZ = 1;
      s.CubeFaceEnablePositiveY = 1;
      s.CubeFaceEnableNegativeY = 1;
      s.CubeFaceEnablePositiveX = 1;
      s.CubeFaceEnableNegativeX = 1;
#else
      s.CubeFaceEnables = 0x3f;
#endif
   }

   s.MultisampledSurfaceStorageFormat =
      isl_to_gen_multisample_layout[info->surf->msaa_layout];
   s.NumberofMultisamples = ffs(info->surf->samples) - 1;

#if (GEN_GEN >= 8 || GEN_IS_HASWELL)
   s.ShaderChannelSelectRed = info->view->channel_select[0];
   s.ShaderChannelSelectGreen = info->view->channel_select[1];
   s.ShaderChannelSelectBlue = info->view->channel_select[2];
   s.ShaderChannelSelectAlpha = info->view->channel_select[3];
#endif

   s.SurfaceBaseAddress = info->address;
   s.MOCS = info->mocs;

#if GEN_GEN >= 8
   s.AuxiliarySurfaceMode = AUX_NONE;
#else
   s.MCSEnable = false;
#endif

#if GEN_GEN >= 8
   /* From the CHV PRM, Volume 2d, page 321 (RENDER_SURFACE_STATE dword 0
    * bit 9 "Sampler L2 Bypass Mode Disable" Programming Notes):
    *
    *    This bit must be set for the following surface types: BC2_UNORM
    *    BC3_UNORM BC5_UNORM BC5_SNORM BC7_UNORM
    */
   if (GEN_GEN >= 9 || dev->info->is_cherryview) {
      switch (info->view->format) {
      case ISL_FORMAT_BC2_UNORM:
      case ISL_FORMAT_BC3_UNORM:
      case ISL_FORMAT_BC5_UNORM:
      case ISL_FORMAT_BC5_SNORM:
      case ISL_FORMAT_BC7_UNORM:
         s.SamplerL2BypassModeDisable = true;
         break;
      default:
         break;
      }
   }
#endif

#if GEN_GEN >= 9
   s.RedClearColor = info->clear_color.u32[0];
   s.GreenClearColor = info->clear_color.u32[1];
   s.BlueClearColor = info->clear_color.u32[2];
   s.AlphaClearColor = info->clear_color.u32[3];
#elif GEN_GEN >= 7
   /* Prior to Sky Lake, we only have one bit for the clear color which
    * gives us 0 or 1 in whatever the surface's format happens to be.
    */
   if (isl_format_has_int_channel(info->view->format)) {
      for (unsigned i = 0; i < 4; i++) {
         assert(info->clear_color.u32[i] == 0 ||
                info->clear_color.u32[i] == 1);
      }
      s.RedClearColor = info->clear_color.u32[0] != 0;
      s.GreenClearColor = info->clear_color.u32[1] != 0;
      s.BlueClearColor = info->clear_color.u32[2] != 0;
      s.AlphaClearColor = info->clear_color.u32[3] != 0;
   } else {
      for (unsigned i = 0; i < 4; i++) {
         assert(info->clear_color.f32[i] == 0.0f ||
                info->clear_color.f32[i] == 1.0f);
      }
      s.RedClearColor = info->clear_color.f32[0] != 0.0f;
      s.GreenClearColor = info->clear_color.f32[1] != 0.0f;
      s.BlueClearColor = info->clear_color.f32[2] != 0.0f;
      s.AlphaClearColor = info->clear_color.f32[3] != 0.0f;
   }
#endif

   GENX(RENDER_SURFACE_STATE_pack)(NULL, state, &s);
}

void
isl_genX(buffer_fill_state_s)(void *state,
                              const struct isl_buffer_fill_state_info *restrict info)
{
   uint32_t num_elements = info->size / info->stride;

   if (GEN_GEN >= 7) {
      if (info->format == ISL_FORMAT_RAW) {
         assert(num_elements <= (1ull << 31));
         assert((num_elements & 3) == 0);
      } else {
         assert(num_elements <= (1ull << 27));
      }
   } else {
      assert(num_elements <= (1ull << 27));
   }

   struct GENX(RENDER_SURFACE_STATE) s = { 0, };

   s.SurfaceType = SURFTYPE_BUFFER;
   s.SurfaceArray = false;
   s.SurfaceFormat = info->format;
   s.SurfaceVerticalAlignment = isl_to_gen_valign[4];
   s.SurfaceHorizontalAlignment = isl_to_gen_halign[4];
   s.Height = ((num_elements - 1) >> 7) & 0x3fff;
   s.Width = (num_elements - 1) & 0x7f;
   s.Depth = ((num_elements - 1) >> 21) & 0x3ff;
   s.SurfacePitch = info->stride - 1;
   s.NumberofMultisamples = MULTISAMPLECOUNT_1;

#if (GEN_GEN >= 8)
   s.TileMode = LINEAR;
#else
   s.TiledSurface = false;
#endif

#if (GEN_GEN >= 8)
   s.RenderCacheReadWriteMode = WriteOnlyCache;
#else
   s.RenderCacheReadWriteMode = 0;
#endif

   s.SurfaceBaseAddress = info->address;
   s.MOCS = info->mocs;

#if (GEN_GEN >= 8 || GEN_IS_HASWELL)
   s.ShaderChannelSelectRed = SCS_RED;
   s.ShaderChannelSelectGreen = SCS_GREEN;
   s.ShaderChannelSelectBlue = SCS_BLUE;
   s.ShaderChannelSelectAlpha = SCS_ALPHA;
#endif

   GENX(RENDER_SURFACE_STATE_pack)(NULL, state, &s);
}
