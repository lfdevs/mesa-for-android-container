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
///
/// Can't use templated variable because we must stick with C++11 features.
/// Template variables were introduced with C++14
template <typename SIMD_T>
struct SwrPixelOffsets
{
public:
    INLINE static typename SIMD_T::Float GetOffset(uint32_t loc)
    {
        SWR_ASSERT(loc <= 1);

        return SIMD_T::set1_ps(loc ? 0.5f : 0.0f);
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Convert the X,Y coords of a triangle to the requested Fixed 
/// Point precision from FP32.
template <typename SIMD_T, typename PT = FixedPointTraits<Fixed_16_8>>
INLINE typename SIMD_T::Integer fpToFixedPointVertical(const typename SIMD_T::Float &vIn)
{
    return SIMD_T::cvtps_epi32(SIMD_T::mul_ps(vIn, SIMD_T::set1_ps(PT::ScaleT::value)));
}

//////////////////////////////////////////////////////////////////////////
/// @brief Helper function to set the X,Y coords of a triangle to the 
/// requested Fixed Point precision from FP32.
/// @param tri: simdvector[3] of FP triangle verts
/// @param vXi: fixed point X coords of tri verts
/// @param vYi: fixed point Y coords of tri verts
template <typename SIMD_T>
INLINE static void FPToFixedPoint(const typename SIMD_T::Vec4 *const tri, typename SIMD_T::Integer(&vXi)[3], typename SIMD_T::Integer(&vYi)[3])
{
    vXi[0] = fpToFixedPointVertical<SIMD_T>(tri[0].x);
    vYi[0] = fpToFixedPointVertical<SIMD_T>(tri[0].y);
    vXi[1] = fpToFixedPointVertical<SIMD_T>(tri[1].x);
    vYi[1] = fpToFixedPointVertical<SIMD_T>(tri[1].y);
    vXi[2] = fpToFixedPointVertical<SIMD_T>(tri[2].x);
    vYi[2] = fpToFixedPointVertical<SIMD_T>(tri[2].y);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Calculate bounding box for current triangle
/// @tparam CT: ConservativeRastFETraits type
/// @param vX: fixed point X position for triangle verts
/// @param vY: fixed point Y position for triangle verts
/// @param bbox: fixed point bbox
/// *Note*: expects vX, vY to be in the correct precision for the type 
/// of rasterization. This avoids unnecessary FP->fixed conversions.
template <typename SIMD_T, typename CT>
INLINE void calcBoundingBoxIntVertical(const typename SIMD_T::Integer(&vX)[3], const typename SIMD_T::Integer(&vY)[3], SIMDBBOX_T<SIMD_T> &bbox)
{
    typename SIMD_T::Integer vMinX = vX[0];

    vMinX = SIMD_T::min_epi32(vMinX, vX[1]);
    vMinX = SIMD_T::min_epi32(vMinX, vX[2]);

    typename SIMD_T::Integer vMaxX = vX[0];

    vMaxX = SIMD_T::max_epi32(vMaxX, vX[1]);
    vMaxX = SIMD_T::max_epi32(vMaxX, vX[2]);

    typename SIMD_T::Integer vMinY = vY[0];

    vMinY = SIMD_T::min_epi32(vMinY, vY[1]);
    vMinY = SIMD_T::min_epi32(vMinY, vY[2]);

    typename SIMD_T::Integer vMaxY = vY[0];

    vMaxY = SIMD_T::max_epi32(vMaxY, vY[1]);
    vMaxY = SIMD_T::max_epi32(vMaxY, vY[2]);

    if (CT::BoundingBoxOffsetT::value != 0)
    {
        /// Bounding box needs to be expanded by 1/512 before snapping to 16.8 for conservative rasterization
        /// expand bbox by 1/256; coverage will be correctly handled in the rasterizer.

        const typename SIMD_T::Integer value = SIMD_T::set1_epi32(CT::BoundingBoxOffsetT::value);

        vMinX = SIMD_T::sub_epi32(vMinX, value);
        vMaxX = SIMD_T::add_epi32(vMaxX, value);
        vMinY = SIMD_T::sub_epi32(vMinY, value);
        vMaxY = SIMD_T::add_epi32(vMaxY, value);
    }

    bbox.xmin = vMinX;
    bbox.xmax = vMaxX;
    bbox.ymin = vMinY;
    bbox.ymax = vMaxY;
}
