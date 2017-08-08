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
* @file binner.h
*
* @brief Declaration for the macrotile binner
*
******************************************************************************/
#include "state.h"
#include "conservativeRast.h"
#include "utils.h"
//////////////////////////////////////////////////////////////////////////
/// @brief Offsets added to post-viewport vertex positions based on
/// raster state.
static const simdscalar g_pixelOffsets[SWR_PIXEL_LOCATION_UL + 1] =
{
    _simd_set1_ps(0.0f),    // SWR_PIXEL_LOCATION_CENTER
    _simd_set1_ps(0.5f),    // SWR_PIXEL_LOCATION_UL
};

#if USE_SIMD16_FRONTEND
static const simd16scalar g_pixelOffsets_simd16[SWR_PIXEL_LOCATION_UL + 1] =
{
    _simd16_set1_ps(0.0f),  // SWR_PIXEL_LOCATION_CENTER
    _simd16_set1_ps(0.5f),  // SWR_PIXEL_LOCATION_UL
};

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief Convert the X,Y coords of a triangle to the requested Fixed 
/// Point precision from FP32.
template <typename PT = FixedPointTraits<Fixed_16_8>>
INLINE simdscalari fpToFixedPointVertical(const simdscalar vIn)
{
    simdscalar vFixed = _simd_mul_ps(vIn, _simd_set1_ps(PT::ScaleT::value));
    return _simd_cvtps_epi32(vFixed);
}

#if USE_SIMD16_FRONTEND
template <typename PT = FixedPointTraits<Fixed_16_8>>
INLINE simd16scalari fpToFixedPointVertical(const simd16scalar vIn)
{
    simd16scalar vFixed = _simd16_mul_ps(vIn, _simd16_set1_ps(PT::ScaleT::value));
    return _simd16_cvtps_epi32(vFixed);
}

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief Helper function to set the X,Y coords of a triangle to the 
/// requested Fixed Point precision from FP32.
/// @param tri: simdvector[3] of FP triangle verts
/// @param vXi: fixed point X coords of tri verts
/// @param vYi: fixed point Y coords of tri verts
INLINE static void FPToFixedPoint(const simdvector * const tri, simdscalari(&vXi)[3], simdscalari(&vYi)[3])
{
    vXi[0] = fpToFixedPointVertical(tri[0].x);
    vYi[0] = fpToFixedPointVertical(tri[0].y);
    vXi[1] = fpToFixedPointVertical(tri[1].x);
    vYi[1] = fpToFixedPointVertical(tri[1].y);
    vXi[2] = fpToFixedPointVertical(tri[2].x);
    vYi[2] = fpToFixedPointVertical(tri[2].y);
}

#if USE_SIMD16_FRONTEND
INLINE static void FPToFixedPoint(const simd16vector * const tri, simd16scalari(&vXi)[3], simd16scalari(&vYi)[3])
{
    vXi[0] = fpToFixedPointVertical(tri[0].x);
    vYi[0] = fpToFixedPointVertical(tri[0].y);
    vXi[1] = fpToFixedPointVertical(tri[1].x);
    vYi[1] = fpToFixedPointVertical(tri[1].y);
    vXi[2] = fpToFixedPointVertical(tri[2].x);
    vYi[2] = fpToFixedPointVertical(tri[2].y);
}

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief Calculate bounding box for current triangle
/// @tparam CT: ConservativeRastFETraits type
/// @param vX: fixed point X position for triangle verts
/// @param vY: fixed point Y position for triangle verts
/// @param bbox: fixed point bbox
/// *Note*: expects vX, vY to be in the correct precision for the type 
/// of rasterization. This avoids unnecessary FP->fixed conversions.
template <typename CT>
INLINE void calcBoundingBoxIntVertical(const simdvector * const tri, simdscalari(&vX)[3], simdscalari(&vY)[3], simdBBox &bbox)
{
    simdscalari vMinX = vX[0];
    vMinX = _simd_min_epi32(vMinX, vX[1]);
    vMinX = _simd_min_epi32(vMinX, vX[2]);

    simdscalari vMaxX = vX[0];
    vMaxX = _simd_max_epi32(vMaxX, vX[1]);
    vMaxX = _simd_max_epi32(vMaxX, vX[2]);

    simdscalari vMinY = vY[0];
    vMinY = _simd_min_epi32(vMinY, vY[1]);
    vMinY = _simd_min_epi32(vMinY, vY[2]);

    simdscalari vMaxY = vY[0];
    vMaxY = _simd_max_epi32(vMaxY, vY[1]);
    vMaxY = _simd_max_epi32(vMaxY, vY[2]);

    bbox.xmin = vMinX;
    bbox.xmax = vMaxX;
    bbox.ymin = vMinY;
    bbox.ymax = vMaxY;
}

#if USE_SIMD16_FRONTEND
template <typename CT>
INLINE void calcBoundingBoxIntVertical(const simd16vector * const tri, simd16scalari(&vX)[3], simd16scalari(&vY)[3], simd16BBox &bbox)
{
    simd16scalari vMinX = vX[0];

    vMinX = _simd16_min_epi32(vMinX, vX[1]);
    vMinX = _simd16_min_epi32(vMinX, vX[2]);

    simd16scalari vMaxX = vX[0];

    vMaxX = _simd16_max_epi32(vMaxX, vX[1]);
    vMaxX = _simd16_max_epi32(vMaxX, vX[2]);

    simd16scalari vMinY = vY[0];

    vMinY = _simd16_min_epi32(vMinY, vY[1]);
    vMinY = _simd16_min_epi32(vMinY, vY[2]);

    simd16scalari vMaxY = vY[0];

    vMaxY = _simd16_max_epi32(vMaxY, vY[1]);
    vMaxY = _simd16_max_epi32(vMaxY, vY[2]);

    bbox.xmin = vMinX;
    bbox.xmax = vMaxX;
    bbox.ymin = vMinY;
    bbox.ymax = vMaxY;
}

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief FEConservativeRastT specialization of calcBoundingBoxIntVertical
/// Offsets BBox for conservative rast
template <>
INLINE void calcBoundingBoxIntVertical<FEConservativeRastT>(const simdvector * const tri, simdscalari(&vX)[3], simdscalari(&vY)[3], simdBBox &bbox)
{
    // FE conservative rast traits
    typedef FEConservativeRastT CT;

    simdscalari vMinX = vX[0];
    vMinX = _simd_min_epi32(vMinX, vX[1]);
    vMinX = _simd_min_epi32(vMinX, vX[2]);

    simdscalari vMaxX = vX[0];
    vMaxX = _simd_max_epi32(vMaxX, vX[1]);
    vMaxX = _simd_max_epi32(vMaxX, vX[2]);

    simdscalari vMinY = vY[0];
    vMinY = _simd_min_epi32(vMinY, vY[1]);
    vMinY = _simd_min_epi32(vMinY, vY[2]);

    simdscalari vMaxY = vY[0];
    vMaxY = _simd_max_epi32(vMaxY, vY[1]);
    vMaxY = _simd_max_epi32(vMaxY, vY[2]);

    /// Bounding box needs to be expanded by 1/512 before snapping to 16.8 for conservative rasterization
    /// expand bbox by 1/256; coverage will be correctly handled in the rasterizer.
    bbox.xmin = _simd_sub_epi32(vMinX, _simd_set1_epi32(CT::BoundingBoxOffsetT::value));
    bbox.xmax = _simd_add_epi32(vMaxX, _simd_set1_epi32(CT::BoundingBoxOffsetT::value));
    bbox.ymin = _simd_sub_epi32(vMinY, _simd_set1_epi32(CT::BoundingBoxOffsetT::value));
    bbox.ymax = _simd_add_epi32(vMaxY, _simd_set1_epi32(CT::BoundingBoxOffsetT::value));
}

#if USE_SIMD16_FRONTEND
template <>
INLINE void calcBoundingBoxIntVertical<FEConservativeRastT>(const simd16vector * const tri, simd16scalari(&vX)[3], simd16scalari(&vY)[3], simd16BBox &bbox)
{
    // FE conservative rast traits
    typedef FEConservativeRastT CT;

    simd16scalari vMinX = vX[0];
    vMinX = _simd16_min_epi32(vMinX, vX[1]);
    vMinX = _simd16_min_epi32(vMinX, vX[2]);

    simd16scalari vMaxX = vX[0];
    vMaxX = _simd16_max_epi32(vMaxX, vX[1]);
    vMaxX = _simd16_max_epi32(vMaxX, vX[2]);

    simd16scalari vMinY = vY[0];
    vMinY = _simd16_min_epi32(vMinY, vY[1]);
    vMinY = _simd16_min_epi32(vMinY, vY[2]);

    simd16scalari vMaxY = vY[0];
    vMaxY = _simd16_max_epi32(vMaxY, vY[1]);
    vMaxY = _simd16_max_epi32(vMaxY, vY[2]);

    /// Bounding box needs to be expanded by 1/512 before snapping to 16.8 for conservative rasterization
    /// expand bbox by 1/256; coverage will be correctly handled in the rasterizer.
    bbox.xmin = _simd16_sub_epi32(vMinX, _simd16_set1_epi32(CT::BoundingBoxOffsetT::value));
    bbox.xmax = _simd16_add_epi32(vMaxX, _simd16_set1_epi32(CT::BoundingBoxOffsetT::value));
    bbox.ymin = _simd16_sub_epi32(vMinY, _simd16_set1_epi32(CT::BoundingBoxOffsetT::value));
    bbox.ymax = _simd16_add_epi32(vMaxY, _simd16_set1_epi32(CT::BoundingBoxOffsetT::value));
}

#endif
