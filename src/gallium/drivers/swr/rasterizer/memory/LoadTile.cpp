/****************************************************************************
* Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
* 
* @file LoadTile.cpp
* 
* @brief Functionality for Load
* 
******************************************************************************/
#include "common/os.h"
#include "common/formats.h"
#include "core/context.h"
#include "core/rdtsc_core.h"
#include "memory/TilingFunctions.h"
#include "memory/tilingtraits.h"
#include "memory/Convert.h"

typedef void(*PFN_LOAD_TILES)(const SWR_SURFACE_STATE*, uint8_t*, uint32_t, uint32_t, uint32_t);

//////////////////////////////////////////////////////////////////////////
/// Load Raster Tile Function Tables.
//////////////////////////////////////////////////////////////////////////
static PFN_LOAD_TILES sLoadTilesColorTable_SWR_TILE_NONE[NUM_SWR_FORMATS];
static PFN_LOAD_TILES sLoadTilesDepthTable_SWR_TILE_NONE[NUM_SWR_FORMATS];

static PFN_LOAD_TILES sLoadTilesColorTable_SWR_TILE_MODE_YMAJOR[NUM_SWR_FORMATS];
static PFN_LOAD_TILES sLoadTilesColorTable_SWR_TILE_MODE_XMAJOR[NUM_SWR_FORMATS];

static PFN_LOAD_TILES sLoadTilesDepthTable_SWR_TILE_MODE_YMAJOR[NUM_SWR_FORMATS];

//////////////////////////////////////////////////////////////////////////
/// LoadRasterTile
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct LoadRasterTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from hot tile source which is always float.
    /// @param pSrc - Pointer to raster tile.
    /// @param x, y - Coordinates to raster tile.
    /// @param output - output color
    INLINE static void SetSwizzledDstColor(
        const float srcColor[4],
        uint32_t x, uint32_t y,
        uint8_t* pDst)
    {
        typedef SimdTile<DstFormat, SrcFormat> SimdT;

        SimdT* pDstSimdTiles = (SimdT*)pDst;

        // Compute which simd tile we're accessing within 8x8 tile.
        //   i.e. Compute linear simd tile coordinate given (x, y) in pixel coordinates.
        uint32_t simdIndex = (y / SIMD_TILE_Y_DIM) * (KNOB_TILE_X_DIM / SIMD_TILE_X_DIM) + (x / SIMD_TILE_X_DIM);

        SimdT* pSimdTile = &pDstSimdTiles[simdIndex];

        uint32_t simdOffset = (y % SIMD_TILE_Y_DIM) * SIMD_TILE_X_DIM + (x % SIMD_TILE_X_DIM);

        pSimdTile->SetSwizzledColor(simdOffset, srcColor);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Loads an 8x8 raster tile from the src surface.
    /// @param pSrcSurface - Src surface state
    /// @param pDst - Destination hot tile pointer
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Load(
        const SWR_SURFACE_STATE* pSrcSurface,
        uint8_t* pDst,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex) // (x, y) pixel coordinate to start of raster tile.
    {
        uint32_t lodWidth = (pSrcSurface->width == 1) ? 1 : pSrcSurface->width >> pSrcSurface->lod;
        uint32_t lodHeight = (pSrcSurface->height == 1) ? 1 : pSrcSurface->height >> pSrcSurface->lod;

        // For each raster tile pixel (rx, ry)
        for (uint32_t ry = 0; ry < KNOB_TILE_Y_DIM; ++ry)
        {
            for (uint32_t rx = 0; rx < KNOB_TILE_X_DIM; ++rx)
            {
                if (((x + rx) < lodWidth) &&
                    ((y + ry) < lodHeight))
                {
                    uint8_t* pSrc = (uint8_t*)ComputeSurfaceAddress<false>(x + rx, y + ry, pSrcSurface->arrayIndex + renderTargetArrayIndex,
                                                                           pSrcSurface->arrayIndex + renderTargetArrayIndex, sampleNum, 
                                                                           pSrcSurface->lod, pSrcSurface);

                    float srcColor[4];
                    ConvertPixelToFloat<SrcFormat>(srcColor, pSrc);

                    // store pixel to hottile
                    SetSwizzledDstColor(srcColor, rx, ry, pDst);
                }
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// LoadMacroTile - Loads a macro tile which consists of raster tiles.
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct LoadMacroTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Load a macrotile to the destination surface.
    /// @param pSrc - Pointer to macro tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to macro tile
    static void Load(
        const SWR_SURFACE_STATE* pSrcSurface,
        uint8_t *pDstHotTile,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex)
    {
        // Load each raster tile from the hot tile to the destination surface.
        for (uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
        {
            for (uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
            {
                for (uint32_t sampleNum = 0; sampleNum < pSrcSurface->numSamples; sampleNum++)
                {
                    LoadRasterTile<TTraits, SrcFormat, DstFormat>::Load(pSrcSurface, pDstHotTile, 
                        (x + col), (y + row), sampleNum, renderTargetArrayIndex);
                    pDstHotTile += KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<DstFormat>::bpp / 8);
                }
            }
        }
    }
};


static void BUCKETS_START(UINT id)
{
#ifdef KNOB_ENABLE_RDTSC
    gBucketMgr.StartBucket(id);
#endif
}

static void BUCKETS_STOP(UINT id)
{
#ifdef KNOB_ENABLE_RDTSC
    gBucketMgr.StopBucket(id);
#endif
}

// on demand buckets for load tiles
static std::vector<int> sBuckets(NUM_SWR_FORMATS, -1);
static std::mutex sBucketMutex;

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a full hottile from a render surface
/// @param hPrivateContext - Handle to private DC
/// @param dstFormat - Format for hot tile.
/// @param renderTargetIndex - Index to src render target
/// @param x, y - Coordinates to raster tile.
/// @param pDstHotTile - Pointer to Hot Tile
void LoadHotTile(
    const SWR_SURFACE_STATE *pSrcSurface,
    SWR_FORMAT dstFormat,
    SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
    uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex,
    uint8_t *pDstHotTile)
{
    PFN_LOAD_TILES pfnLoadTiles = NULL;

    // don't need to load null surfaces
    if (pSrcSurface->type == SURFACE_NULL)
    {
        return;
    }

    // force 0 if requested renderTargetArrayIndex is OOB
    if (renderTargetArrayIndex >= pSrcSurface->depth)
    {
        renderTargetArrayIndex = 0;
    }

    if (renderTargetIndex < SWR_ATTACHMENT_DEPTH)
    {
        switch (pSrcSurface->tileMode)
        {
        case SWR_TILE_NONE:
            pfnLoadTiles = sLoadTilesColorTable_SWR_TILE_NONE[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_YMAJOR:
            pfnLoadTiles = sLoadTilesColorTable_SWR_TILE_MODE_YMAJOR[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_XMAJOR:
            pfnLoadTiles = sLoadTilesColorTable_SWR_TILE_MODE_XMAJOR[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_WMAJOR:
            SWR_ASSERT(pSrcSurface->format == R8_UINT);
            pfnLoadTiles = LoadMacroTile<TilingTraits<SWR_TILE_MODE_WMAJOR, 8>, R8_UINT, R8_UINT>::Load;
            break;
        default:
            SWR_ASSERT(0, "Unsupported tiling mode");
            break;
        }
    }
    else if (renderTargetIndex == SWR_ATTACHMENT_DEPTH)
    {
        // Currently depth can map to linear and tile-y.
        switch (pSrcSurface->tileMode)
        {
        case SWR_TILE_NONE:
            pfnLoadTiles = sLoadTilesDepthTable_SWR_TILE_NONE[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_YMAJOR:
            pfnLoadTiles = sLoadTilesDepthTable_SWR_TILE_MODE_YMAJOR[pSrcSurface->format];
            break;
        default:
            SWR_ASSERT(0, "Unsupported tiling mode");
            break;
        }
    }
    else
    {
        SWR_ASSERT(renderTargetIndex == SWR_ATTACHMENT_STENCIL);
        SWR_ASSERT(pSrcSurface->format == R8_UINT);
        switch (pSrcSurface->tileMode)
        {
        case SWR_TILE_NONE:
            pfnLoadTiles = LoadMacroTile<TilingTraits<SWR_TILE_NONE, 8>, R8_UINT, R8_UINT>::Load;
            break;
        case SWR_TILE_MODE_WMAJOR:
            pfnLoadTiles = LoadMacroTile<TilingTraits<SWR_TILE_MODE_WMAJOR, 8>, R8_UINT, R8_UINT>::Load;
            break;
        default:
            SWR_ASSERT(0, "Unsupported tiling mode");
            break;
        }
    }

    if (pfnLoadTiles == nullptr)
    {
        SWR_ASSERT(false, "Unsupported format for load tile");
        return;
    }

    // Load a macro tile.
#ifdef KNOB_ENABLE_RDTSC
    if (sBuckets[pSrcSurface->format] == -1)
    {
        // guard sBuckets update since storetiles is called by multiple threads
        sBucketMutex.lock();
        if (sBuckets[pSrcSurface->format] == -1)
        {
            const SWR_FORMAT_INFO& info = GetFormatInfo(pSrcSurface->format);
            BUCKET_DESC desc{ info.name, "", false, 0xffffffff };
            sBuckets[pSrcSurface->format] = gBucketMgr.RegisterBucket(desc);
        }
        sBucketMutex.unlock();
    }
#endif

    BUCKETS_START(sBuckets[pSrcSurface->format]);
    pfnLoadTiles(pSrcSurface, pDstHotTile, x, y, renderTargetArrayIndex);
    BUCKETS_STOP(sBuckets[pSrcSurface->format]);
}


//////////////////////////////////////////////////////////////////////////
/// INIT_LOAD_TILES_TABLE - Helper macro for setting up the tables.
#define INIT_LOAD_TILES_COLOR_TABLE(tilemode) \
    memset(sLoadTilesColorTable_##tilemode, 0, sizeof(sLoadTilesColorTable_##tilemode)); \
    \
    sLoadTilesColorTable_##tilemode[R32G32B32A32_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 128>, R32G32B32A32_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32B32A32_SINT]      = LoadMacroTile<TilingTraits<tilemode, 128>, R32G32B32A32_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32B32A32_UINT]      = LoadMacroTile<TilingTraits<tilemode, 128>, R32G32B32A32_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32B32X32_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 128>, R32G32B32X32_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32B32_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 96>, R32G32B32_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32B32_SINT]      = LoadMacroTile<TilingTraits<tilemode, 96>, R32G32B32_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32B32_UINT]      = LoadMacroTile<TilingTraits<tilemode, 96>, R32G32B32_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16A16_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 64>, R16G16B16A16_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16A16_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 64>, R16G16B16A16_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16A16_SINT]      = LoadMacroTile<TilingTraits<tilemode, 64>, R16G16B16A16_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16A16_UINT]      = LoadMacroTile<TilingTraits<tilemode, 64>, R16G16B16A16_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16A16_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 64>, R16G16B16A16_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 64>, R32G32_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32_SINT]      = LoadMacroTile<TilingTraits<tilemode, 64>, R32G32_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32G32_UINT]      = LoadMacroTile<TilingTraits<tilemode, 64>, R32G32_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16X16_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 64>, R16G16B16X16_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16X16_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 64>, R16G16B16X16_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B8G8R8A8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, B8G8R8A8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B8G8R8A8_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 32>, B8G8R8A8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R10G10B10A2_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, R10G10B10A2_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R10G10B10A2_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 32>, R10G10B10A2_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R10G10B10A2_UINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R10G10B10A2_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8A8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, R8G8B8A8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8A8_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 32>, R8G8B8A8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8A8_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, R8G8B8A8_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8A8_SINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R8G8B8A8_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8A8_UINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R8G8B8A8_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, R16G16_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, R16G16_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16_SINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R16G16_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16_UINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R16G16_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R16G16_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B10G10R10A2_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, B10G10R10A2_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B10G10R10A2_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 32>, B10G10R10A2_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R11G11B10_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R11G11B10_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32_SINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R32_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32_UINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R32_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R32_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R32_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[A32_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 32>, A32_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B8G8R8X8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, B8G8R8X8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B8G8R8X8_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 32>, B8G8R8X8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8X8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, R8G8B8X8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8X8_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 32>, R8G8B8X8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B10G10R10X2_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, B10G10R10X2_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B5G6R5_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, B5G6R5_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B5G6R5_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 16>, B5G6R5_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B5G5R5A1_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, B5G5R5A1_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B5G5R5A1_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 16>, B5G5R5A1_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B4G4R4A4_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, B4G4R4A4_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B4G4R4A4_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 16>, B4G4R4A4_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, R8G8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, R8G8_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8_SINT]      = LoadMacroTile<TilingTraits<tilemode, 16>, R8G8_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8_UINT]      = LoadMacroTile<TilingTraits<tilemode, 16>, R8G8_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, R16_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, R16_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16_SINT]      = LoadMacroTile<TilingTraits<tilemode, 16>, R16_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16_UINT]      = LoadMacroTile<TilingTraits<tilemode, 16>, R16_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 16>, R16_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[A16_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, A16_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[A16_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 16>, A16_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B5G5R5X1_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 16>, B5G5R5X1_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B5G5R5X1_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 16>, B5G5R5X1_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 8>, R8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 8>, R8_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8_SINT]      = LoadMacroTile<TilingTraits<tilemode, 8>, R8_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8_UINT]      = LoadMacroTile<TilingTraits<tilemode, 8>, R8_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[A8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 8>, A8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC1_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 64>, BC1_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC2_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 128>, BC2_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC3_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 128>, BC3_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC4_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 64>, BC4_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC5_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 128>, BC5_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC1_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 64>, BC1_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC2_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 128>, BC2_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC3_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 128>, BC3_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 24>, R8G8B8_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 24>, R8G8B8_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC4_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 64>, BC4_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[BC5_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 128>, BC5_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16_FLOAT]      = LoadMacroTile<TilingTraits<tilemode, 48>, R16G16B16_FLOAT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16_UNORM]      = LoadMacroTile<TilingTraits<tilemode, 48>, R16G16B16_UNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 48>, R16G16B16_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8_UNORM_SRGB]      = LoadMacroTile<TilingTraits<tilemode, 24>, R8G8B8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16_UINT]      = LoadMacroTile<TilingTraits<tilemode, 48>, R16G16B16_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R16G16B16_SINT]      = LoadMacroTile<TilingTraits<tilemode, 48>, R16G16B16_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R10G10B10A2_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, R10G10B10A2_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R10G10B10A2_SINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, R10G10B10A2_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B10G10R10A2_SNORM]      = LoadMacroTile<TilingTraits<tilemode, 32>, B10G10R10A2_SNORM, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B10G10R10A2_UINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, B10G10R10A2_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[B10G10R10A2_SINT]      = LoadMacroTile<TilingTraits<tilemode, 32>, B10G10R10A2_SINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8_UINT]      = LoadMacroTile<TilingTraits<tilemode, 24>, R8G8B8_UINT, R32G32B32A32_FLOAT>::Load; \
    sLoadTilesColorTable_##tilemode[R8G8B8_SINT]      = LoadMacroTile<TilingTraits<tilemode, 24>, R8G8B8_SINT, R32G32B32A32_FLOAT>::Load; \

//////////////////////////////////////////////////////////////////////////
/// INIT_LOAD_TILES_TABLE - Helper macro for setting up the tables.
#define INIT_LOAD_TILES_DEPTH_TABLE(tilemode) \
    memset(sLoadTilesDepthTable_##tilemode, 0, sizeof(sLoadTilesDepthTable_##tilemode)); \
    \
    sLoadTilesDepthTable_##tilemode[R16_UNORM] = LoadMacroTile<TilingTraits<tilemode, 16>, R16_UNORM, R32_FLOAT>::Load; \
    sLoadTilesDepthTable_##tilemode[R32_FLOAT] = LoadMacroTile<TilingTraits<tilemode, 32>, R32_FLOAT, R32_FLOAT>::Load; \
    sLoadTilesDepthTable_##tilemode[R24_UNORM_X8_TYPELESS] = LoadMacroTile<TilingTraits<tilemode, 32>, R24_UNORM_X8_TYPELESS, R32_FLOAT>::Load; \

//////////////////////////////////////////////////////////////////////////
/// @brief Sets up tables for LoadTile
void InitSimLoadTilesTable()
{
    INIT_LOAD_TILES_COLOR_TABLE(SWR_TILE_NONE);
    INIT_LOAD_TILES_DEPTH_TABLE(SWR_TILE_NONE);

    INIT_LOAD_TILES_COLOR_TABLE(SWR_TILE_MODE_YMAJOR);
    INIT_LOAD_TILES_COLOR_TABLE(SWR_TILE_MODE_XMAJOR);

    INIT_LOAD_TILES_DEPTH_TABLE(SWR_TILE_MODE_YMAJOR);
}
