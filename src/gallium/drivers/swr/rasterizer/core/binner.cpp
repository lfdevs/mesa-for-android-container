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
* @file binner.cpp
*
* @brief Implementation for the macrotile binner
*
******************************************************************************/

#include "context.h"
#include "frontend.h"
#include "conservativeRast.h"
#include "pa.h"
#include "rasterizer.h"
#include "rdtsc_core.h"
#include "tilemgr.h"

// Function Prototype
void BinPostSetupLines(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simdvector prims[3], simdscalar vRecipW[2], uint32_t primMask, simdscalari primID, simdscalari viewportIdx);

//////////////////////////////////////////////////////////////////////////
/// @brief Offsets added to post-viewport vertex positions based on
/// raster state.
static const simdscalar g_pixelOffsets[SWR_PIXEL_LOCATION_UL + 1] =
{
    _simd_set1_ps(0.0f), // SWR_PIXEL_LOCATION_CENTER
    _simd_set1_ps(0.5f), // SWR_PIXEL_LOCATION_UL
};

//////////////////////////////////////////////////////////////////////////
/// @brief Convert the X,Y coords of a triangle to the requested Fixed 
/// Point precision from FP32.
template <typename PT = FixedPointTraits<Fixed_16_8>>
INLINE simdscalari fpToFixedPointVertical(const simdscalar vIn)
{
    simdscalar vFixed = _simd_mul_ps(vIn, _simd_set1_ps(PT::ScaleT::value));
    return _simd_cvtps_epi32(vFixed);
}

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

//////////////////////////////////////////////////////////////////////////
/// @brief Processes attributes for the backend based on linkage mask and
///        linkage map.  Essentially just doing an SOA->AOS conversion and pack.
/// @param pDC - Draw context
/// @param pa - Primitive Assembly state
/// @param linkageMask - Specifies which VS outputs are routed to PS.
/// @param pLinkageMap - maps VS attribute slot to PS slot
/// @param triIndex - Triangle to process attributes for
/// @param pBuffer - Output result
template<typename NumVertsT, typename IsSwizzledT, typename HasConstantInterpT, typename IsDegenerate>
INLINE void ProcessAttributes(
    DRAW_CONTEXT *pDC,
    PA_STATE&pa,
    uint32_t triIndex,
    uint32_t primId,
    float *pBuffer)
{
    static_assert(NumVertsT::value > 0 && NumVertsT::value <= 3, "Invalid value for NumVertsT");
    const SWR_BACKEND_STATE& backendState = pDC->pState->state.backendState;
    // Conservative Rasterization requires degenerate tris to have constant attribute interpolation
    LONG constantInterpMask = IsDegenerate::value ? 0xFFFFFFFF : backendState.constantInterpolationMask;
    const uint32_t provokingVertex = pDC->pState->state.frontendState.topologyProvokingVertex;
    const PRIMITIVE_TOPOLOGY topo = pDC->pState->state.topology;

    static const float constTable[3][4] = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f }
    };

    for (uint32_t i = 0; i < backendState.numAttributes; ++i)
    {
        uint32_t inputSlot;
        if (IsSwizzledT::value)
        {
            SWR_ATTRIB_SWIZZLE attribSwizzle = backendState.swizzleMap[i];
            inputSlot = VERTEX_ATTRIB_START_SLOT + attribSwizzle.sourceAttrib;

        }
        else
        {
            inputSlot = VERTEX_ATTRIB_START_SLOT + i;
        }

        __m128 attrib[3];    // triangle attribs (always 4 wide)
        float* pAttribStart = pBuffer;

        if (HasConstantInterpT::value || IsDegenerate::value)
        {
            if (_bittest(&constantInterpMask, i))
            {
                uint32_t vid;
                uint32_t adjustedTriIndex;
                static const uint32_t tristripProvokingVertex[] = { 0, 2, 1 };
                static const int32_t quadProvokingTri[2][4] = { { 0, 0, 0, 1 },{ 0, -1, 0, 0 } };
                static const uint32_t quadProvokingVertex[2][4] = { { 0, 1, 2, 2 },{ 0, 1, 1, 2 } };
                static const int32_t qstripProvokingTri[2][4] = { { 0, 0, 0, 1 },{ -1, 0, 0, 0 } };
                static const uint32_t qstripProvokingVertex[2][4] = { { 0, 1, 2, 1 },{ 0, 0, 2, 1 } };

                switch (topo) {
                case TOP_QUAD_LIST:
                    adjustedTriIndex = triIndex + quadProvokingTri[triIndex & 1][provokingVertex];
                    vid = quadProvokingVertex[triIndex & 1][provokingVertex];
                    break;
                case TOP_QUAD_STRIP:
                    adjustedTriIndex = triIndex + qstripProvokingTri[triIndex & 1][provokingVertex];
                    vid = qstripProvokingVertex[triIndex & 1][provokingVertex];
                    break;
                case TOP_TRIANGLE_STRIP:
                    adjustedTriIndex = triIndex;
                    vid = (triIndex & 1)
                        ? tristripProvokingVertex[provokingVertex]
                        : provokingVertex;
                    break;
                default:
                    adjustedTriIndex = triIndex;
                    vid = provokingVertex;
                    break;
                }

                pa.AssembleSingle(inputSlot, adjustedTriIndex, attrib);

                for (uint32_t i = 0; i < NumVertsT::value; ++i)
                {
                    _mm_store_ps(pBuffer, attrib[vid]);
                    pBuffer += 4;
                }
            }
            else
            {
                pa.AssembleSingle(inputSlot, triIndex, attrib);

                for (uint32_t i = 0; i < NumVertsT::value; ++i)
                {
                    _mm_store_ps(pBuffer, attrib[i]);
                    pBuffer += 4;
                }
            }
        }
        else
        {
            pa.AssembleSingle(inputSlot, triIndex, attrib);

            for (uint32_t i = 0; i < NumVertsT::value; ++i)
            {
                _mm_store_ps(pBuffer, attrib[i]);
                pBuffer += 4;
            }
        }

        // pad out the attrib buffer to 3 verts to ensure the triangle
        // interpolation code in the pixel shader works correctly for the
        // 3 topologies - point, line, tri.  This effectively zeros out the
        // effect of the missing vertices in the triangle interpolation.
        for (uint32_t v = NumVertsT::value; v < 3; ++v)
        {
            _mm_store_ps(pBuffer, attrib[NumVertsT::value - 1]);
            pBuffer += 4;
        }

        // check for constant source overrides
        if (IsSwizzledT::value)
        {
            uint32_t mask = backendState.swizzleMap[i].componentOverrideMask;
            if (mask)
            {
                DWORD comp;
                while (_BitScanForward(&comp, mask))
                {
                    mask &= ~(1 << comp);

                    float constantValue = 0.0f;
                    switch ((SWR_CONSTANT_SOURCE)backendState.swizzleMap[i].constantSource)
                    {
                    case SWR_CONSTANT_SOURCE_CONST_0000:
                    case SWR_CONSTANT_SOURCE_CONST_0001_FLOAT:
                    case SWR_CONSTANT_SOURCE_CONST_1111_FLOAT:
                        constantValue = constTable[backendState.swizzleMap[i].constantSource][comp];
                        break;
                    case SWR_CONSTANT_SOURCE_PRIM_ID:
                        constantValue = *(float*)&primId;
                        break;
                    }

                    // apply constant value to all 3 vertices
                    for (uint32_t v = 0; v < 3; ++v)
                    {
                        pAttribStart[comp + v * 4] = constantValue;
                    }
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief  Gather scissor rect data based on per-prim viewport indices.
/// @param pScissorsInFixedPoint - array of scissor rects in 16.8 fixed point.
/// @param pViewportIndex - array of per-primitive vewport indexes.
/// @param scisXmin - output vector of per-prmitive scissor rect Xmin data.
/// @param scisYmin - output vector of per-prmitive scissor rect Ymin data.
/// @param scisXmax - output vector of per-prmitive scissor rect Xmax data.
/// @param scisYmax - output vector of per-prmitive scissor rect Ymax data.
//
/// @todo:  Look at speeding this up -- weigh against corresponding costs in rasterizer.
template<size_t SimdWidth>
struct GatherScissors
{
    static void Gather(const SWR_RECT* pScissorsInFixedPoint, const uint32_t* pViewportIndex,
        simdscalari &scisXmin, simdscalari &scisYmin,
        simdscalari &scisXmax, simdscalari &scisYmax)
    {
        SWR_ASSERT(0, "Unhandled Simd Width in Scissor Rect Gather");
    }
};

template<>
struct GatherScissors<8>
{
    static void Gather(const SWR_RECT* pScissorsInFixedPoint, const uint32_t* pViewportIndex,
        simdscalari &scisXmin, simdscalari &scisYmin,
        simdscalari &scisXmax, simdscalari &scisYmax)
    {
        scisXmin = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[0]].xmin,
            pScissorsInFixedPoint[pViewportIndex[1]].xmin,
            pScissorsInFixedPoint[pViewportIndex[2]].xmin,
            pScissorsInFixedPoint[pViewportIndex[3]].xmin,
            pScissorsInFixedPoint[pViewportIndex[4]].xmin,
            pScissorsInFixedPoint[pViewportIndex[5]].xmin,
            pScissorsInFixedPoint[pViewportIndex[6]].xmin,
            pScissorsInFixedPoint[pViewportIndex[7]].xmin);
        scisYmin = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[0]].ymin,
            pScissorsInFixedPoint[pViewportIndex[1]].ymin,
            pScissorsInFixedPoint[pViewportIndex[2]].ymin,
            pScissorsInFixedPoint[pViewportIndex[3]].ymin,
            pScissorsInFixedPoint[pViewportIndex[4]].ymin,
            pScissorsInFixedPoint[pViewportIndex[5]].ymin,
            pScissorsInFixedPoint[pViewportIndex[6]].ymin,
            pScissorsInFixedPoint[pViewportIndex[7]].ymin);
        scisXmax = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[0]].xmax,
            pScissorsInFixedPoint[pViewportIndex[1]].xmax,
            pScissorsInFixedPoint[pViewportIndex[2]].xmax,
            pScissorsInFixedPoint[pViewportIndex[3]].xmax,
            pScissorsInFixedPoint[pViewportIndex[4]].xmax,
            pScissorsInFixedPoint[pViewportIndex[5]].xmax,
            pScissorsInFixedPoint[pViewportIndex[6]].xmax,
            pScissorsInFixedPoint[pViewportIndex[7]].xmax);
        scisYmax = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[0]].ymax,
            pScissorsInFixedPoint[pViewportIndex[1]].ymax,
            pScissorsInFixedPoint[pViewportIndex[2]].ymax,
            pScissorsInFixedPoint[pViewportIndex[3]].ymax,
            pScissorsInFixedPoint[pViewportIndex[4]].ymax,
            pScissorsInFixedPoint[pViewportIndex[5]].ymax,
            pScissorsInFixedPoint[pViewportIndex[6]].ymax,
            pScissorsInFixedPoint[pViewportIndex[7]].ymax);
    }
};

typedef void(*PFN_PROCESS_ATTRIBUTES)(DRAW_CONTEXT*, PA_STATE&, uint32_t, uint32_t, float*);

struct ProcessAttributesChooser
{
    typedef PFN_PROCESS_ATTRIBUTES FuncType;

    template <typename... ArgsB>
    static FuncType GetFunc()
    {
        return ProcessAttributes<ArgsB...>;
    }
};

PFN_PROCESS_ATTRIBUTES GetProcessAttributesFunc(uint32_t NumVerts, bool IsSwizzled, bool HasConstantInterp, bool IsDegenerate = false)
{
    return TemplateArgUnroller<ProcessAttributesChooser>::GetFunc(IntArg<1, 3>{NumVerts}, IsSwizzled, HasConstantInterp, IsDegenerate);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Processes enabled user clip distances. Loads the active clip
///        distances from the PA, sets up barycentric equations, and
///        stores the results to the output buffer
/// @param pa - Primitive Assembly state
/// @param primIndex - primitive index to process
/// @param clipDistMask - mask of enabled clip distances
/// @param pUserClipBuffer - buffer to store results
template<uint32_t NumVerts>
void ProcessUserClipDist(PA_STATE& pa, uint32_t primIndex, uint8_t clipDistMask, float *pRecipW, float* pUserClipBuffer)
{
    DWORD clipDist;
    while (_BitScanForward(&clipDist, clipDistMask))
    {
        clipDistMask &= ~(1 << clipDist);
        uint32_t clipSlot = clipDist >> 2;
        uint32_t clipComp = clipDist & 0x3;
        uint32_t clipAttribSlot = clipSlot == 0 ?
            VERTEX_CLIPCULL_DIST_LO_SLOT : VERTEX_CLIPCULL_DIST_HI_SLOT;

        __m128 primClipDist[3];
        pa.AssembleSingle(clipAttribSlot, primIndex, primClipDist);

        float vertClipDist[NumVerts];
        for (uint32_t e = 0; e < NumVerts; ++e)
        {
            OSALIGNSIMD(float) aVertClipDist[4];
            _mm_store_ps(aVertClipDist, primClipDist[e]);
            vertClipDist[e] = aVertClipDist[clipComp];
        };

        // setup plane equations for barycentric interpolation in the backend
        float baryCoeff[NumVerts];
        float last = vertClipDist[NumVerts - 1] * pRecipW[NumVerts - 1];
        for (uint32_t e = 0; e < NumVerts - 1; ++e)
        {
            baryCoeff[e] = vertClipDist[e] * pRecipW[e] - last;
        }
        baryCoeff[NumVerts - 1] = last;

        for (uint32_t e = 0; e < NumVerts; ++e)
        {
            *(pUserClipBuffer++) = baryCoeff[e];
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Bin triangle primitives to macro tiles. Performs setup, clipping
///        culling, viewport transform, etc.
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains triangle position data for SIMDs worth of triangles.
/// @param primID - Primitive ID for each triangle.
/// @param viewportIdx - viewport array index for each triangle.
/// @tparam CT - ConservativeRastFETraits
template <typename CT>
void BinTriangles(
    DRAW_CONTEXT *pDC,
    PA_STATE& pa,
    uint32_t workerId,
    simdvector tri[3],
    uint32_t triMask,
    simdscalari primID,
    simdscalari viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(FEBinTriangles, pDC->drawId);

    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_FRONTEND_STATE& feState = state.frontendState;
    const SWR_GS_STATE& gsState = state.gsState;
    MacroTileMgr *pTileMgr = pDC->pTileMgr;

    simdscalar vRecipW0 = _simd_set1_ps(1.0f);
    simdscalar vRecipW1 = _simd_set1_ps(1.0f);
    simdscalar vRecipW2 = _simd_set1_ps(1.0f);

    if (feState.vpTransformDisable)
    {
        // RHW is passed in directly when VP transform is disabled
        vRecipW0 = tri[0].v[3];
        vRecipW1 = tri[1].v[3];
        vRecipW2 = tri[2].v[3];
    }
    else
    {
        // Perspective divide
        vRecipW0 = _simd_div_ps(_simd_set1_ps(1.0f), tri[0].w);
        vRecipW1 = _simd_div_ps(_simd_set1_ps(1.0f), tri[1].w);
        vRecipW2 = _simd_div_ps(_simd_set1_ps(1.0f), tri[2].w);

        tri[0].v[0] = _simd_mul_ps(tri[0].v[0], vRecipW0);
        tri[1].v[0] = _simd_mul_ps(tri[1].v[0], vRecipW1);
        tri[2].v[0] = _simd_mul_ps(tri[2].v[0], vRecipW2);

        tri[0].v[1] = _simd_mul_ps(tri[0].v[1], vRecipW0);
        tri[1].v[1] = _simd_mul_ps(tri[1].v[1], vRecipW1);
        tri[2].v[1] = _simd_mul_ps(tri[2].v[1], vRecipW2);

        tri[0].v[2] = _simd_mul_ps(tri[0].v[2], vRecipW0);
        tri[1].v[2] = _simd_mul_ps(tri[1].v[2], vRecipW1);
        tri[2].v[2] = _simd_mul_ps(tri[2].v[2], vRecipW2);

        // Viewport transform to screen space coords
        if (state.gsState.emitsViewportArrayIndex)
        {
            viewportTransform<3>(tri, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<3>(tri, state.vpMatrices);
        }
    }

    // Adjust for pixel center location
    simdscalar offset = g_pixelOffsets[rastState.pixelLocation];
    tri[0].x = _simd_add_ps(tri[0].x, offset);
    tri[0].y = _simd_add_ps(tri[0].y, offset);

    tri[1].x = _simd_add_ps(tri[1].x, offset);
    tri[1].y = _simd_add_ps(tri[1].y, offset);

    tri[2].x = _simd_add_ps(tri[2].x, offset);
    tri[2].y = _simd_add_ps(tri[2].y, offset);

    simdscalari vXi[3], vYi[3];
    // Set vXi, vYi to required fixed point precision
    FPToFixedPoint(tri, vXi, vYi);

    // triangle setup
    simdscalari vAi[3], vBi[3];
    triangleSetupABIntVertical(vXi, vYi, vAi, vBi);

    // determinant
    simdscalari vDet[2];
    calcDeterminantIntVertical(vAi, vBi, vDet);

    // cull zero area
    int maskLo = _simd_movemask_pd(_simd_castsi_pd(_simd_cmpeq_epi64(vDet[0], _simd_setzero_si())));
    int maskHi = _simd_movemask_pd(_simd_castsi_pd(_simd_cmpeq_epi64(vDet[1], _simd_setzero_si())));

    int cullZeroAreaMask = maskLo | (maskHi << (KNOB_SIMD_WIDTH / 2));

    uint32_t origTriMask = triMask;
    // don't cull degenerate triangles if we're conservatively rasterizing
    if (rastState.fillMode == SWR_FILLMODE_SOLID && !CT::IsConservativeT::value)
    {
        triMask &= ~cullZeroAreaMask;
    }

    // determine front winding tris
    // CW  +det
    // CCW det < 0;
    // 0 area triangles are marked as backfacing regardless of winding order,
    // which is required behavior for conservative rast and wireframe rendering
    uint32_t frontWindingTris;
    if (rastState.frontWinding == SWR_FRONTWINDING_CW)
    {
        maskLo = _simd_movemask_pd(_simd_castsi_pd(_simd_cmpgt_epi64(vDet[0], _simd_setzero_si())));
        maskHi = _simd_movemask_pd(_simd_castsi_pd(_simd_cmpgt_epi64(vDet[1], _simd_setzero_si())));
    }
    else
    {
        maskLo = _simd_movemask_pd(_simd_castsi_pd(_simd_cmpgt_epi64(_simd_setzero_si(), vDet[0])));
        maskHi = _simd_movemask_pd(_simd_castsi_pd(_simd_cmpgt_epi64(_simd_setzero_si(), vDet[1])));
    }
    frontWindingTris = maskLo | (maskHi << (KNOB_SIMD_WIDTH / 2));

    // cull
    uint32_t cullTris;
    switch ((SWR_CULLMODE)rastState.cullMode)
    {
    case SWR_CULLMODE_BOTH:  cullTris = 0xffffffff; break;
    case SWR_CULLMODE_NONE:  cullTris = 0x0; break;
    case SWR_CULLMODE_FRONT: cullTris = frontWindingTris; break;
        // 0 area triangles are marked as backfacing, which is required behavior for conservative rast
    case SWR_CULLMODE_BACK:  cullTris = ~frontWindingTris; break;
    default: SWR_ASSERT(false, "Invalid cull mode: %d", rastState.cullMode); cullTris = 0x0; break;
    }

    triMask &= ~cullTris;

    if (origTriMask ^ triMask)
    {
        RDTSC_EVENT(FECullZeroAreaAndBackface, _mm_popcnt_u32(origTriMask ^ triMask), 0);
    }

    // Simple non-conformant wireframe mode, useful for debugging
    if (rastState.fillMode == SWR_FILLMODE_WIREFRAME)
    {
        // construct 3 SIMD lines out of the triangle and call the line binner for each SIMD
        simdvector line[2];
        simdscalar recipW[2];
        line[0] = tri[0];
        line[1] = tri[1];
        recipW[0] = vRecipW0;
        recipW[1] = vRecipW1;
        BinPostSetupLines(pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx);

        line[0] = tri[1];
        line[1] = tri[2];
        recipW[0] = vRecipW1;
        recipW[1] = vRecipW2;
        BinPostSetupLines(pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx);

        line[0] = tri[2];
        line[1] = tri[0];
        recipW[0] = vRecipW2;
        recipW[1] = vRecipW0;
        BinPostSetupLines(pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx);

        AR_END(FEBinTriangles, 1);
        return;
    }

    /// Note: these variable initializations must stay above any 'goto endBenTriangles'
    // compute per tri backface
    uint32_t frontFaceMask = frontWindingTris;
    uint32_t *pPrimID = (uint32_t *)&primID;
    const uint32_t *pViewportIndex = (uint32_t *)&viewportIdx;
    DWORD triIndex = 0;
    // for center sample pattern, all samples are at pixel center; calculate coverage
    // once at center and broadcast the results in the backend
    const SWR_MULTISAMPLE_COUNT sampleCount = (rastState.samplePattern == SWR_MSAA_STANDARD_PATTERN) ? rastState.sampleCount : SWR_MULTISAMPLE_1X;
    uint32_t edgeEnable;
    PFN_WORK_FUNC pfnWork;
    if (CT::IsConservativeT::value)
    {
        // determine which edges of the degenerate tri, if any, are valid to rasterize.
        // used to call the appropriate templated rasterizer function
        if (cullZeroAreaMask > 0)
        {
            // e0 = v1-v0
            simdscalari x0x1Mask = _simd_cmpeq_epi32(vXi[0], vXi[1]);
            simdscalari y0y1Mask = _simd_cmpeq_epi32(vYi[0], vYi[1]);
            uint32_t e0Mask = _simd_movemask_ps(_simd_castsi_ps(_simd_and_si(x0x1Mask, y0y1Mask)));

            // e1 = v2-v1
            simdscalari x1x2Mask = _simd_cmpeq_epi32(vXi[1], vXi[2]);
            simdscalari y1y2Mask = _simd_cmpeq_epi32(vYi[1], vYi[2]);
            uint32_t e1Mask = _simd_movemask_ps(_simd_castsi_ps(_simd_and_si(x1x2Mask, y1y2Mask)));

            // e2 = v0-v2
            // if v0 == v1 & v1 == v2, v0 == v2
            uint32_t e2Mask = e0Mask & e1Mask;
            SWR_ASSERT(KNOB_SIMD_WIDTH == 8, "Need to update degenerate mask code for avx512");

            // edge order: e0 = v0v1, e1 = v1v2, e2 = v0v2
            // 32 bit binary: 0000 0000 0010 0100 1001 0010 0100 1001
            e0Mask = pdep_u32(e0Mask, 0x00249249);
            // 32 bit binary: 0000 0000 0100 1001 0010 0100 1001 0010
            e1Mask = pdep_u32(e1Mask, 0x00492492);
            // 32 bit binary: 0000 0000 1001 0010 0100 1001 0010 0100
            e2Mask = pdep_u32(e2Mask, 0x00924924);

            edgeEnable = (0x00FFFFFF & (~(e0Mask | e1Mask | e2Mask)));
        }
        else
        {
            edgeEnable = 0x00FFFFFF;
        }
    }
    else
    {
        // degenerate triangles won't be sent to rasterizer; just enable all edges
        pfnWork = GetRasterizerFunc(sampleCount, (rastState.conservativeRast > 0),
            (SWR_INPUT_COVERAGE)pDC->pState->state.psState.inputCoverage, ALL_EDGES_VALID,
            (state.scissorsTileAligned == false));
    }

    if (!triMask)
    {
        goto endBinTriangles;
    }

    // Calc bounding box of triangles
    simdBBox bbox;
    calcBoundingBoxIntVertical<CT>(tri, vXi, vYi, bbox);

    // determine if triangle falls between pixel centers and discard
    // only discard for non-MSAA case and when conservative rast is disabled
    // (xmin + 127) & ~255
    // (xmax + 128) & ~255
    if (rastState.sampleCount == SWR_MULTISAMPLE_1X && (!CT::IsConservativeT::value))
    {
        origTriMask = triMask;

        int cullCenterMask;
        {
            simdscalari xmin = _simd_add_epi32(bbox.xmin, _simd_set1_epi32(127));
            xmin = _simd_and_si(xmin, _simd_set1_epi32(~255));
            simdscalari xmax = _simd_add_epi32(bbox.xmax, _simd_set1_epi32(128));
            xmax = _simd_and_si(xmax, _simd_set1_epi32(~255));

            simdscalari vMaskH = _simd_cmpeq_epi32(xmin, xmax);

            simdscalari ymin = _simd_add_epi32(bbox.ymin, _simd_set1_epi32(127));
            ymin = _simd_and_si(ymin, _simd_set1_epi32(~255));
            simdscalari ymax = _simd_add_epi32(bbox.ymax, _simd_set1_epi32(128));
            ymax = _simd_and_si(ymax, _simd_set1_epi32(~255));

            simdscalari vMaskV = _simd_cmpeq_epi32(ymin, ymax);
            vMaskV = _simd_or_si(vMaskH, vMaskV);
            cullCenterMask = _simd_movemask_ps(_simd_castsi_ps(vMaskV));
        }

        triMask &= ~cullCenterMask;

        if (origTriMask ^ triMask)
        {
            RDTSC_EVENT(FECullBetweenCenters, _mm_popcnt_u32(origTriMask ^ triMask), 0);
        }
    }

    // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge is exclusive.
    // Gather the AOS effective scissor rects based on the per-prim VP index.
    /// @todo:  Look at speeding this up -- weigh against corresponding costs in rasterizer.
    simdscalari scisXmin, scisYmin, scisXmax, scisYmax;
    if (state.gsState.emitsViewportArrayIndex)
    {
        GatherScissors<KNOB_SIMD_WIDTH>::Gather(&state.scissorsInFixedPoint[0], pViewportIndex,
            scisXmin, scisYmin, scisXmax, scisYmax);
    }
    else // broadcast fast path for non-VPAI case.
    {
        scisXmin = _simd_set1_epi32(state.scissorsInFixedPoint[0].xmin);
        scisYmin = _simd_set1_epi32(state.scissorsInFixedPoint[0].ymin);
        scisXmax = _simd_set1_epi32(state.scissorsInFixedPoint[0].xmax);
        scisYmax = _simd_set1_epi32(state.scissorsInFixedPoint[0].ymax);
    }

    bbox.xmin = _simd_max_epi32(bbox.xmin, scisXmin);
    bbox.ymin = _simd_max_epi32(bbox.ymin, scisYmin);
    bbox.xmax = _simd_min_epi32(_simd_sub_epi32(bbox.xmax, _simd_set1_epi32(1)), scisXmax);
    bbox.ymax = _simd_min_epi32(_simd_sub_epi32(bbox.ymax, _simd_set1_epi32(1)), scisYmax);

    if (CT::IsConservativeT::value)
    {
        // in the case where a degenerate triangle is on a scissor edge, we need to make sure the primitive bbox has
        // some area. Bump the xmax/ymax edges out 
        simdscalari topEqualsBottom = _simd_cmpeq_epi32(bbox.ymin, bbox.ymax);
        bbox.ymax = _simd_blendv_epi32(bbox.ymax, _simd_add_epi32(bbox.ymax, _simd_set1_epi32(1)), topEqualsBottom);
        simdscalari leftEqualsRight = _simd_cmpeq_epi32(bbox.xmin, bbox.xmax);
        bbox.xmax = _simd_blendv_epi32(bbox.xmax, _simd_add_epi32(bbox.xmax, _simd_set1_epi32(1)), leftEqualsRight);
    }

    // Cull tris completely outside scissor
    {
        simdscalari maskOutsideScissorX = _simd_cmpgt_epi32(bbox.xmin, bbox.xmax);
        simdscalari maskOutsideScissorY = _simd_cmpgt_epi32(bbox.ymin, bbox.ymax);
        simdscalari maskOutsideScissorXY = _simd_or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = _simd_movemask_ps(_simd_castsi_ps(maskOutsideScissorXY));
        triMask = triMask & ~maskOutsideScissor;
    }

    if (!triMask)
    {
        goto endBinTriangles;
    }

    // Convert triangle bbox to macrotile units.
    bbox.xmin = _simd_srai_epi32(bbox.xmin, KNOB_MACROTILE_X_DIM_FIXED_SHIFT);
    bbox.ymin = _simd_srai_epi32(bbox.ymin, KNOB_MACROTILE_Y_DIM_FIXED_SHIFT);
    bbox.xmax = _simd_srai_epi32(bbox.xmax, KNOB_MACROTILE_X_DIM_FIXED_SHIFT);
    bbox.ymax = _simd_srai_epi32(bbox.ymax, KNOB_MACROTILE_Y_DIM_FIXED_SHIFT);

    OSALIGNSIMD(uint32_t) aMTLeft[KNOB_SIMD_WIDTH], aMTRight[KNOB_SIMD_WIDTH], aMTTop[KNOB_SIMD_WIDTH], aMTBottom[KNOB_SIMD_WIDTH];
    _simd_store_si((simdscalari*)aMTLeft, bbox.xmin);
    _simd_store_si((simdscalari*)aMTRight, bbox.xmax);
    _simd_store_si((simdscalari*)aMTTop, bbox.ymin);
    _simd_store_si((simdscalari*)aMTBottom, bbox.ymax);

    // transpose verts needed for backend
    /// @todo modify BE to take non-transformed verts
    __m128 vHorizX[8], vHorizY[8], vHorizZ[8], vHorizW[8];
    vTranspose3x8(vHorizX, tri[0].x, tri[1].x, tri[2].x);
    vTranspose3x8(vHorizY, tri[0].y, tri[1].y, tri[2].y);
    vTranspose3x8(vHorizZ, tri[0].z, tri[1].z, tri[2].z);
    vTranspose3x8(vHorizW, vRecipW0, vRecipW1, vRecipW2);

    // store render target array index
    OSALIGNSIMD(uint32_t) aRTAI[KNOB_SIMD_WIDTH];
    if (gsState.gsEnable && gsState.emitsRenderTargetArrayIndex)
    {
        simdvector vRtai[3];
        pa.Assemble(VERTEX_RTAI_SLOT, vRtai);
        simdscalari vRtaii;
        vRtaii = _simd_castps_si(vRtai[0].x);
        _simd_store_si((simdscalari*)aRTAI, vRtaii);
    }
    else
    {
        _simd_store_si((simdscalari*)aRTAI, _simd_setzero_si());
    }

endBinTriangles:

    // scan remaining valid triangles and bin each separately
    while (_BitScanForward(&triIndex, triMask))
    {
        uint32_t linkageCount = state.backendState.numAttributes;
        uint32_t numScalarAttribs = linkageCount * 4;

        BE_WORK work;
        work.type = DRAW;

        bool isDegenerate;
        if (CT::IsConservativeT::value)
        {
            // only rasterize valid edges if we have a degenerate primitive
            int32_t triEdgeEnable = (edgeEnable >> (triIndex * 3)) & ALL_EDGES_VALID;
            work.pfnWork = GetRasterizerFunc(sampleCount, (rastState.conservativeRast > 0),
                (SWR_INPUT_COVERAGE)pDC->pState->state.psState.inputCoverage, triEdgeEnable,
                (state.scissorsTileAligned == false));

            // Degenerate triangles are required to be constant interpolated
            isDegenerate = (triEdgeEnable != ALL_EDGES_VALID) ? true : false;
        }
        else
        {
            isDegenerate = false;
            work.pfnWork = pfnWork;
        }

        // Select attribute processor
        PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(3,
            state.backendState.swizzleEnable, state.backendState.constantInterpolationMask, isDegenerate);

        TRIANGLE_WORK_DESC &desc = work.desc.tri;

        desc.triFlags.frontFacing = state.forceFront ? 1 : ((frontFaceMask >> triIndex) & 1);
        desc.triFlags.primID = pPrimID[triIndex];
        desc.triFlags.renderTargetArrayIndex = aRTAI[triIndex];
        desc.triFlags.viewportIndex = pViewportIndex[triIndex];

        auto pArena = pDC->pArena;
        SWR_ASSERT(pArena != nullptr);

        // store active attribs
        float *pAttribs = (float*)pArena->AllocAligned(numScalarAttribs * 3 * sizeof(float), 16);
        desc.pAttribs = pAttribs;
        desc.numAttribs = linkageCount;
        pfnProcessAttribs(pDC, pa, triIndex, pPrimID[triIndex], desc.pAttribs);

        // store triangle vertex data
        desc.pTriBuffer = (float*)pArena->AllocAligned(4 * 4 * sizeof(float), 16);

        _mm_store_ps(&desc.pTriBuffer[0], vHorizX[triIndex]);
        _mm_store_ps(&desc.pTriBuffer[4], vHorizY[triIndex]);
        _mm_store_ps(&desc.pTriBuffer[8], vHorizZ[triIndex]);
        _mm_store_ps(&desc.pTriBuffer[12], vHorizW[triIndex]);

        // store user clip distances
        if (rastState.clipDistanceMask)
        {
            uint32_t numClipDist = _mm_popcnt_u32(rastState.clipDistanceMask);
            desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 3 * sizeof(float));
            ProcessUserClipDist<3>(pa, triIndex, rastState.clipDistanceMask, &desc.pTriBuffer[12], desc.pUserClipBuffer);
        }

        for (uint32_t y = aMTTop[triIndex]; y <= aMTBottom[triIndex]; ++y)
        {
            for (uint32_t x = aMTLeft[triIndex]; x <= aMTRight[triIndex]; ++x)
            {
#if KNOB_ENABLE_TOSS_POINTS
                if (!KNOB_TOSS_SETUP_TRIS)
#endif
                {
                    pTileMgr->enqueue(x, y, &work);
                }
            }
        }
                     triMask &= ~(1 << triIndex);
    }

    AR_END(FEBinTriangles, 1);
}

struct FEBinTrianglesChooser
{
    typedef PFN_PROCESS_PRIMS FuncType;

    template <typename... ArgsB>
    static FuncType GetFunc()
    {
        return BinTriangles<ConservativeRastFETraits<ArgsB...>>;
    }
};

// Selector for correct templated BinTrinagles function
PFN_PROCESS_PRIMS GetBinTrianglesFunc(bool IsConservative)
{
    return TemplateArgUnroller<FEBinTrianglesChooser>::GetFunc(IsConservative);
}


//////////////////////////////////////////////////////////////////////////
/// @brief Bin SIMD points to the backend.  Only supports point size of 1
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains point position data for SIMDs worth of points.
/// @param primID - Primitive ID for each point.
void BinPoints(
    DRAW_CONTEXT *pDC,
    PA_STATE& pa,
    uint32_t workerId,
    simdvector prim[3],
    uint32_t primMask,
    simdscalari primID,
    simdscalari viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(FEBinPoints, pDC->drawId);

    simdvector& primVerts = prim[0];

    const API_STATE& state = GetApiState(pDC);
    const SWR_FRONTEND_STATE& feState = state.frontendState;
    const SWR_GS_STATE& gsState = state.gsState;
    const SWR_RASTSTATE& rastState = state.rastState;
    const uint32_t *pViewportIndex = (uint32_t *)&viewportIdx;

    // Select attribute processor
    PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(1,
        state.backendState.swizzleEnable, state.backendState.constantInterpolationMask);

    if (!feState.vpTransformDisable)
    {
        // perspective divide
        simdscalar vRecipW0 = _simd_div_ps(_simd_set1_ps(1.0f), primVerts.w);
        primVerts.x = _simd_mul_ps(primVerts.x, vRecipW0);
        primVerts.y = _simd_mul_ps(primVerts.y, vRecipW0);
        primVerts.z = _simd_mul_ps(primVerts.z, vRecipW0);

        // viewport transform to screen coords
        if (state.gsState.emitsViewportArrayIndex)
        {
            viewportTransform<1>(&primVerts, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<1>(&primVerts, state.vpMatrices);
        }
    }

    // adjust for pixel center location
    simdscalar offset = g_pixelOffsets[rastState.pixelLocation];
    primVerts.x = _simd_add_ps(primVerts.x, offset);
    primVerts.y = _simd_add_ps(primVerts.y, offset);

    // convert to fixed point
    simdscalari vXi, vYi;
    vXi = fpToFixedPointVertical(primVerts.x);
    vYi = fpToFixedPointVertical(primVerts.y);

    if (CanUseSimplePoints(pDC))
    {
        // adjust for ymin-xmin rule
        vXi = _simd_sub_epi32(vXi, _simd_set1_epi32(1));
        vYi = _simd_sub_epi32(vYi, _simd_set1_epi32(1));

        // cull points off the ymin-xmin edge of the viewport
        primMask &= ~_simd_movemask_ps(_simd_castsi_ps(vXi));
        primMask &= ~_simd_movemask_ps(_simd_castsi_ps(vYi));

        // compute macro tile coordinates 
        simdscalari macroX = _simd_srai_epi32(vXi, KNOB_MACROTILE_X_DIM_FIXED_SHIFT);
        simdscalari macroY = _simd_srai_epi32(vYi, KNOB_MACROTILE_Y_DIM_FIXED_SHIFT);

        OSALIGNSIMD(uint32_t) aMacroX[KNOB_SIMD_WIDTH], aMacroY[KNOB_SIMD_WIDTH];
        _simd_store_si((simdscalari*)aMacroX, macroX);
        _simd_store_si((simdscalari*)aMacroY, macroY);

        // compute raster tile coordinates
        simdscalari rasterX = _simd_srai_epi32(vXi, KNOB_TILE_X_DIM_SHIFT + FIXED_POINT_SHIFT);
        simdscalari rasterY = _simd_srai_epi32(vYi, KNOB_TILE_Y_DIM_SHIFT + FIXED_POINT_SHIFT);

        // compute raster tile relative x,y for coverage mask
        simdscalari tileAlignedX = _simd_slli_epi32(rasterX, KNOB_TILE_X_DIM_SHIFT);
        simdscalari tileAlignedY = _simd_slli_epi32(rasterY, KNOB_TILE_Y_DIM_SHIFT);

        simdscalari tileRelativeX = _simd_sub_epi32(_simd_srai_epi32(vXi, FIXED_POINT_SHIFT), tileAlignedX);
        simdscalari tileRelativeY = _simd_sub_epi32(_simd_srai_epi32(vYi, FIXED_POINT_SHIFT), tileAlignedY);

        OSALIGNSIMD(uint32_t) aTileRelativeX[KNOB_SIMD_WIDTH];
        OSALIGNSIMD(uint32_t) aTileRelativeY[KNOB_SIMD_WIDTH];
        _simd_store_si((simdscalari*)aTileRelativeX, tileRelativeX);
        _simd_store_si((simdscalari*)aTileRelativeY, tileRelativeY);

        OSALIGNSIMD(uint32_t) aTileAlignedX[KNOB_SIMD_WIDTH];
        OSALIGNSIMD(uint32_t) aTileAlignedY[KNOB_SIMD_WIDTH];
        _simd_store_si((simdscalari*)aTileAlignedX, tileAlignedX);
        _simd_store_si((simdscalari*)aTileAlignedY, tileAlignedY);

        OSALIGNSIMD(float) aZ[KNOB_SIMD_WIDTH];
        _simd_store_ps((float*)aZ, primVerts.z);

        // store render target array index
        OSALIGNSIMD(uint32_t) aRTAI[KNOB_SIMD_WIDTH];
        if (gsState.gsEnable && gsState.emitsRenderTargetArrayIndex)
        {
            simdvector vRtai;
            pa.Assemble(VERTEX_RTAI_SLOT, &vRtai);
            simdscalari vRtaii = _simd_castps_si(vRtai.x);
            _simd_store_si((simdscalari*)aRTAI, vRtaii);
        }
        else
        {
            _simd_store_si((simdscalari*)aRTAI, _simd_setzero_si());
        }

        uint32_t *pPrimID = (uint32_t *)&primID;
        DWORD primIndex = 0;

        const SWR_BACKEND_STATE& backendState = pDC->pState->state.backendState;

        // scan remaining valid triangles and bin each separately
        while (_BitScanForward(&primIndex, primMask))
        {
            uint32_t linkageCount = backendState.numAttributes;
            uint32_t numScalarAttribs = linkageCount * 4;

            BE_WORK work;
            work.type = DRAW;

            TRIANGLE_WORK_DESC &desc = work.desc.tri;

            // points are always front facing
            desc.triFlags.frontFacing = 1;
            desc.triFlags.primID = pPrimID[primIndex];
            desc.triFlags.renderTargetArrayIndex = aRTAI[primIndex];
            desc.triFlags.viewportIndex = pViewportIndex[primIndex];

            work.pfnWork = RasterizeSimplePoint;

            auto pArena = pDC->pArena;
            SWR_ASSERT(pArena != nullptr);

            // store attributes
            float *pAttribs = (float*)pArena->AllocAligned(3 * numScalarAttribs * sizeof(float), 16);
            desc.pAttribs = pAttribs;
            desc.numAttribs = linkageCount;

            pfnProcessAttribs(pDC, pa, primIndex, pPrimID[primIndex], pAttribs);

            // store raster tile aligned x, y, perspective correct z
            float *pTriBuffer = (float*)pArena->AllocAligned(4 * sizeof(float), 16);
            desc.pTriBuffer = pTriBuffer;
            *(uint32_t*)pTriBuffer++ = aTileAlignedX[primIndex];
            *(uint32_t*)pTriBuffer++ = aTileAlignedY[primIndex];
            *pTriBuffer = aZ[primIndex];

            uint32_t tX = aTileRelativeX[primIndex];
            uint32_t tY = aTileRelativeY[primIndex];

            // pack the relative x,y into the coverageMask, the rasterizer will
            // generate the true coverage mask from it
            work.desc.tri.triFlags.coverageMask = tX | (tY << 4);

            // bin it
            MacroTileMgr *pTileMgr = pDC->pTileMgr;
#if KNOB_ENABLE_TOSS_POINTS
            if (!KNOB_TOSS_SETUP_TRIS)
#endif
            {
                pTileMgr->enqueue(aMacroX[primIndex], aMacroY[primIndex], &work);
            }
            primMask &= ~(1 << primIndex);
        }
    }
    else
    {
        // non simple points need to be potentially binned to multiple macro tiles
        simdscalar vPointSize;
        if (rastState.pointParam)
        {
            simdvector size[3];
            pa.Assemble(VERTEX_POINT_SIZE_SLOT, size);
            vPointSize = size[0].x;
        }
        else
        {
            vPointSize = _simd_set1_ps(rastState.pointSize);
        }

        // bloat point to bbox
        simdBBox bbox;
        bbox.xmin = bbox.xmax = vXi;
        bbox.ymin = bbox.ymax = vYi;

        simdscalar vHalfWidth = _simd_mul_ps(vPointSize, _simd_set1_ps(0.5f));
        simdscalari vHalfWidthi = fpToFixedPointVertical(vHalfWidth);
        bbox.xmin = _simd_sub_epi32(bbox.xmin, vHalfWidthi);
        bbox.xmax = _simd_add_epi32(bbox.xmax, vHalfWidthi);
        bbox.ymin = _simd_sub_epi32(bbox.ymin, vHalfWidthi);
        bbox.ymax = _simd_add_epi32(bbox.ymax, vHalfWidthi);

        // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge is exclusive.
        // Gather the AOS effective scissor rects based on the per-prim VP index.
        /// @todo:  Look at speeding this up -- weigh against corresponding costs in rasterizer.
        simdscalari scisXmin, scisYmin, scisXmax, scisYmax;
        if (state.gsState.emitsViewportArrayIndex)
        {
            GatherScissors<KNOB_SIMD_WIDTH>::Gather(&state.scissorsInFixedPoint[0], pViewportIndex,
                scisXmin, scisYmin, scisXmax, scisYmax);
        }
        else // broadcast fast path for non-VPAI case.
        {
            scisXmin = _simd_set1_epi32(state.scissorsInFixedPoint[0].xmin);
            scisYmin = _simd_set1_epi32(state.scissorsInFixedPoint[0].ymin);
            scisXmax = _simd_set1_epi32(state.scissorsInFixedPoint[0].xmax);
            scisYmax = _simd_set1_epi32(state.scissorsInFixedPoint[0].ymax);
        }

        bbox.xmin = _simd_max_epi32(bbox.xmin, scisXmin);
        bbox.ymin = _simd_max_epi32(bbox.ymin, scisYmin);
        bbox.xmax = _simd_min_epi32(_simd_sub_epi32(bbox.xmax, _simd_set1_epi32(1)), scisXmax);
        bbox.ymax = _simd_min_epi32(_simd_sub_epi32(bbox.ymax, _simd_set1_epi32(1)), scisYmax);

        // Cull bloated points completely outside scissor
        simdscalari maskOutsideScissorX = _simd_cmpgt_epi32(bbox.xmin, bbox.xmax);
        simdscalari maskOutsideScissorY = _simd_cmpgt_epi32(bbox.ymin, bbox.ymax);
        simdscalari maskOutsideScissorXY = _simd_or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = _simd_movemask_ps(_simd_castsi_ps(maskOutsideScissorXY));
        primMask = primMask & ~maskOutsideScissor;

        // Convert bbox to macrotile units.
        bbox.xmin = _simd_srai_epi32(bbox.xmin, KNOB_MACROTILE_X_DIM_FIXED_SHIFT);
        bbox.ymin = _simd_srai_epi32(bbox.ymin, KNOB_MACROTILE_Y_DIM_FIXED_SHIFT);
        bbox.xmax = _simd_srai_epi32(bbox.xmax, KNOB_MACROTILE_X_DIM_FIXED_SHIFT);
        bbox.ymax = _simd_srai_epi32(bbox.ymax, KNOB_MACROTILE_Y_DIM_FIXED_SHIFT);

        OSALIGNSIMD(uint32_t) aMTLeft[KNOB_SIMD_WIDTH], aMTRight[KNOB_SIMD_WIDTH], aMTTop[KNOB_SIMD_WIDTH], aMTBottom[KNOB_SIMD_WIDTH];
        _simd_store_si((simdscalari*)aMTLeft, bbox.xmin);
        _simd_store_si((simdscalari*)aMTRight, bbox.xmax);
        _simd_store_si((simdscalari*)aMTTop, bbox.ymin);
        _simd_store_si((simdscalari*)aMTBottom, bbox.ymax);

        // store render target array index
        OSALIGNSIMD(uint32_t) aRTAI[KNOB_SIMD_WIDTH];
        if (gsState.gsEnable && gsState.emitsRenderTargetArrayIndex)
        {
            simdvector vRtai[2];
            pa.Assemble(VERTEX_RTAI_SLOT, vRtai);
            simdscalari vRtaii = _simd_castps_si(vRtai[0].x);
            _simd_store_si((simdscalari*)aRTAI, vRtaii);
        }
        else
        {
            _simd_store_si((simdscalari*)aRTAI, _simd_setzero_si());
        }

        OSALIGNSIMD(float) aPointSize[KNOB_SIMD_WIDTH];
        _simd_store_ps((float*)aPointSize, vPointSize);

        uint32_t *pPrimID = (uint32_t *)&primID;

        OSALIGNSIMD(float) aPrimVertsX[KNOB_SIMD_WIDTH];
        OSALIGNSIMD(float) aPrimVertsY[KNOB_SIMD_WIDTH];
        OSALIGNSIMD(float) aPrimVertsZ[KNOB_SIMD_WIDTH];

        _simd_store_ps((float*)aPrimVertsX, primVerts.x);
        _simd_store_ps((float*)aPrimVertsY, primVerts.y);
        _simd_store_ps((float*)aPrimVertsZ, primVerts.z);

        // scan remaining valid prims and bin each separately
        const SWR_BACKEND_STATE& backendState = state.backendState;
        DWORD primIndex;
        while (_BitScanForward(&primIndex, primMask))
        {
            uint32_t linkageCount = backendState.numAttributes;
            uint32_t numScalarAttribs = linkageCount * 4;

            BE_WORK work;
            work.type = DRAW;

            TRIANGLE_WORK_DESC &desc = work.desc.tri;

            desc.triFlags.frontFacing = 1;
            desc.triFlags.primID = pPrimID[primIndex];
            desc.triFlags.pointSize = aPointSize[primIndex];
            desc.triFlags.renderTargetArrayIndex = aRTAI[primIndex];
            desc.triFlags.viewportIndex = pViewportIndex[primIndex];

            work.pfnWork = RasterizeTriPoint;

            auto pArena = pDC->pArena;
            SWR_ASSERT(pArena != nullptr);

            // store active attribs
            desc.pAttribs = (float*)pArena->AllocAligned(numScalarAttribs * 3 * sizeof(float), 16);
            desc.numAttribs = linkageCount;
            pfnProcessAttribs(pDC, pa, primIndex, pPrimID[primIndex], desc.pAttribs);

            // store point vertex data
            float *pTriBuffer = (float*)pArena->AllocAligned(4 * sizeof(float), 16);
            desc.pTriBuffer = pTriBuffer;
            *pTriBuffer++ = aPrimVertsX[primIndex];
            *pTriBuffer++ = aPrimVertsY[primIndex];
            *pTriBuffer = aPrimVertsZ[primIndex];

            // store user clip distances
            if (rastState.clipDistanceMask)
            {
                uint32_t numClipDist = _mm_popcnt_u32(rastState.clipDistanceMask);
                desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 3 * sizeof(float));
                float dists[8];
                float one = 1.0f;
                ProcessUserClipDist<1>(pa, primIndex, rastState.clipDistanceMask, &one, dists);
                for (uint32_t i = 0; i < numClipDist; i++) {
                    desc.pUserClipBuffer[3*i + 0] = 0.0f;
                    desc.pUserClipBuffer[3*i + 1] = 0.0f;
                    desc.pUserClipBuffer[3*i + 2] = dists[i];
                }
            }

            MacroTileMgr *pTileMgr = pDC->pTileMgr;
            for (uint32_t y = aMTTop[primIndex]; y <= aMTBottom[primIndex]; ++y)
            {
                for (uint32_t x = aMTLeft[primIndex]; x <= aMTRight[primIndex]; ++x)
                {
#if KNOB_ENABLE_TOSS_POINTS
                    if (!KNOB_TOSS_SETUP_TRIS)
#endif
                    {
                        pTileMgr->enqueue(x, y, &work);
                    }
                }
            }

            primMask &= ~(1 << primIndex);
        }
    }

    AR_END(FEBinPoints, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Bin SIMD lines to the backend.
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains line position data for SIMDs worth of points.
/// @param primID - Primitive ID for each line.
/// @param viewportIdx - Viewport Array Index for each line.
void BinPostSetupLines(
    DRAW_CONTEXT *pDC,
    PA_STATE& pa,
    uint32_t workerId,
    simdvector prim[],
    simdscalar recipW[],
    uint32_t primMask,
    simdscalari primID,
    simdscalari viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(FEBinLines, pDC->drawId);

    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_FRONTEND_STATE& feState = state.frontendState;
    const SWR_GS_STATE& gsState = state.gsState;

    // Select attribute processor
    PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(2,
        state.backendState.swizzleEnable, state.backendState.constantInterpolationMask);

    simdscalar& vRecipW0 = recipW[0];
    simdscalar& vRecipW1 = recipW[1];

    // convert to fixed point
    simdscalari vXi[2], vYi[2];
    vXi[0] = fpToFixedPointVertical(prim[0].x);
    vYi[0] = fpToFixedPointVertical(prim[0].y);
    vXi[1] = fpToFixedPointVertical(prim[1].x);
    vYi[1] = fpToFixedPointVertical(prim[1].y);

    // compute x-major vs y-major mask
    simdscalari xLength = _simd_abs_epi32(_simd_sub_epi32(vXi[0], vXi[1]));
    simdscalari yLength = _simd_abs_epi32(_simd_sub_epi32(vYi[0], vYi[1]));
    simdscalar vYmajorMask = _simd_castsi_ps(_simd_cmpgt_epi32(yLength, xLength));
    uint32_t yMajorMask = _simd_movemask_ps(vYmajorMask);

    // cull zero-length lines
    simdscalari vZeroLengthMask = _simd_cmpeq_epi32(xLength, _simd_setzero_si());
    vZeroLengthMask = _simd_and_si(vZeroLengthMask, _simd_cmpeq_epi32(yLength, _simd_setzero_si()));

    primMask &= ~_simd_movemask_ps(_simd_castsi_ps(vZeroLengthMask));

    uint32_t *pPrimID = (uint32_t *)&primID;
    const uint32_t *pViewportIndex = (uint32_t *)&viewportIdx;

    simdscalar vUnused = _simd_setzero_ps();

    // Calc bounding box of lines
    simdBBox bbox;
    bbox.xmin = _simd_min_epi32(vXi[0], vXi[1]);
    bbox.xmax = _simd_max_epi32(vXi[0], vXi[1]);
    bbox.ymin = _simd_min_epi32(vYi[0], vYi[1]);
    bbox.ymax = _simd_max_epi32(vYi[0], vYi[1]);

    // bloat bbox by line width along minor axis
    simdscalar vHalfWidth = _simd_set1_ps(rastState.lineWidth / 2.0f);
    simdscalari vHalfWidthi = fpToFixedPointVertical(vHalfWidth);
    simdBBox bloatBox;
    bloatBox.xmin = _simd_sub_epi32(bbox.xmin, vHalfWidthi);
    bloatBox.xmax = _simd_add_epi32(bbox.xmax, vHalfWidthi);
    bloatBox.ymin = _simd_sub_epi32(bbox.ymin, vHalfWidthi);
    bloatBox.ymax = _simd_add_epi32(bbox.ymax, vHalfWidthi);

    bbox.xmin = _simd_blendv_epi32(bbox.xmin, bloatBox.xmin, vYmajorMask);
    bbox.xmax = _simd_blendv_epi32(bbox.xmax, bloatBox.xmax, vYmajorMask);
    bbox.ymin = _simd_blendv_epi32(bloatBox.ymin, bbox.ymin, vYmajorMask);
    bbox.ymax = _simd_blendv_epi32(bloatBox.ymax, bbox.ymax, vYmajorMask);

    // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge is exclusive.
    simdscalari scisXmin, scisYmin, scisXmax, scisYmax;
    if (state.gsState.emitsViewportArrayIndex)
    {
        GatherScissors<KNOB_SIMD_WIDTH>::Gather(&state.scissorsInFixedPoint[0], pViewportIndex,
            scisXmin, scisYmin, scisXmax, scisYmax);
    }
    else // broadcast fast path for non-VPAI case.
    {
        scisXmin = _simd_set1_epi32(state.scissorsInFixedPoint[0].xmin);
        scisYmin = _simd_set1_epi32(state.scissorsInFixedPoint[0].ymin);
        scisXmax = _simd_set1_epi32(state.scissorsInFixedPoint[0].xmax);
        scisYmax = _simd_set1_epi32(state.scissorsInFixedPoint[0].ymax);
    }

    bbox.xmin = _simd_max_epi32(bbox.xmin, scisXmin);
    bbox.ymin = _simd_max_epi32(bbox.ymin, scisYmin);
    bbox.xmax = _simd_min_epi32(_simd_sub_epi32(bbox.xmax, _simd_set1_epi32(1)), scisXmax);
    bbox.ymax = _simd_min_epi32(_simd_sub_epi32(bbox.ymax, _simd_set1_epi32(1)), scisYmax);

    // Cull prims completely outside scissor
    {
        simdscalari maskOutsideScissorX = _simd_cmpgt_epi32(bbox.xmin, bbox.xmax);
        simdscalari maskOutsideScissorY = _simd_cmpgt_epi32(bbox.ymin, bbox.ymax);
        simdscalari maskOutsideScissorXY = _simd_or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = _simd_movemask_ps(_simd_castsi_ps(maskOutsideScissorXY));
        primMask = primMask & ~maskOutsideScissor;
    }

    if (!primMask)
    {
        goto endBinLines;
    }

    // Convert triangle bbox to macrotile units.
    bbox.xmin = _simd_srai_epi32(bbox.xmin, KNOB_MACROTILE_X_DIM_FIXED_SHIFT);
    bbox.ymin = _simd_srai_epi32(bbox.ymin, KNOB_MACROTILE_Y_DIM_FIXED_SHIFT);
    bbox.xmax = _simd_srai_epi32(bbox.xmax, KNOB_MACROTILE_X_DIM_FIXED_SHIFT);
    bbox.ymax = _simd_srai_epi32(bbox.ymax, KNOB_MACROTILE_Y_DIM_FIXED_SHIFT);

    OSALIGNSIMD(uint32_t) aMTLeft[KNOB_SIMD_WIDTH], aMTRight[KNOB_SIMD_WIDTH], aMTTop[KNOB_SIMD_WIDTH], aMTBottom[KNOB_SIMD_WIDTH];
    _simd_store_si((simdscalari*)aMTLeft, bbox.xmin);
    _simd_store_si((simdscalari*)aMTRight, bbox.xmax);
    _simd_store_si((simdscalari*)aMTTop, bbox.ymin);
    _simd_store_si((simdscalari*)aMTBottom, bbox.ymax);

    // transpose verts needed for backend
    /// @todo modify BE to take non-transformed verts
    __m128 vHorizX[8], vHorizY[8], vHorizZ[8], vHorizW[8];
    vTranspose3x8(vHorizX, prim[0].x, prim[1].x, vUnused);
    vTranspose3x8(vHorizY, prim[0].y, prim[1].y, vUnused);
    vTranspose3x8(vHorizZ, prim[0].z, prim[1].z, vUnused);
    vTranspose3x8(vHorizW, vRecipW0, vRecipW1, vUnused);

    // store render target array index
    OSALIGNSIMD(uint32_t) aRTAI[KNOB_SIMD_WIDTH];
    if (gsState.gsEnable && gsState.emitsRenderTargetArrayIndex)
    {
        simdvector vRtai[2];
        pa.Assemble(VERTEX_RTAI_SLOT, vRtai);
        simdscalari vRtaii = _simd_castps_si(vRtai[0].x);
        _simd_store_si((simdscalari*)aRTAI, vRtaii);
    }
    else
    {
        _simd_store_si((simdscalari*)aRTAI, _simd_setzero_si());
    }

    // scan remaining valid prims and bin each separately
    DWORD primIndex;
    while (_BitScanForward(&primIndex, primMask))
    {
        uint32_t linkageCount = state.backendState.numAttributes;
        uint32_t numScalarAttribs = linkageCount * 4;

        BE_WORK work;
        work.type = DRAW;

        TRIANGLE_WORK_DESC &desc = work.desc.tri;

        desc.triFlags.frontFacing = 1;
        desc.triFlags.primID = pPrimID[primIndex];
        desc.triFlags.yMajor = (yMajorMask >> primIndex) & 1;
        desc.triFlags.renderTargetArrayIndex = aRTAI[primIndex];
        desc.triFlags.viewportIndex = pViewportIndex[primIndex];

        work.pfnWork = RasterizeLine;

        auto pArena = pDC->pArena;
        SWR_ASSERT(pArena != nullptr);

        // store active attribs
        desc.pAttribs = (float*)pArena->AllocAligned(numScalarAttribs * 3 * sizeof(float), 16);
        desc.numAttribs = linkageCount;
        pfnProcessAttribs(pDC, pa, primIndex, pPrimID[primIndex], desc.pAttribs);

        // store line vertex data
        desc.pTriBuffer = (float*)pArena->AllocAligned(4 * 4 * sizeof(float), 16);
        _mm_store_ps(&desc.pTriBuffer[0], vHorizX[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[4], vHorizY[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[8], vHorizZ[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[12], vHorizW[primIndex]);

        // store user clip distances
        if (rastState.clipDistanceMask)
        {
            uint32_t numClipDist = _mm_popcnt_u32(rastState.clipDistanceMask);
            desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 2 * sizeof(float));
            ProcessUserClipDist<2>(pa, primIndex, rastState.clipDistanceMask, &desc.pTriBuffer[12], desc.pUserClipBuffer);
        }

        MacroTileMgr *pTileMgr = pDC->pTileMgr;
        for (uint32_t y = aMTTop[primIndex]; y <= aMTBottom[primIndex]; ++y)
        {
            for (uint32_t x = aMTLeft[primIndex]; x <= aMTRight[primIndex]; ++x)
            {
#if KNOB_ENABLE_TOSS_POINTS
                if (!KNOB_TOSS_SETUP_TRIS)
#endif
                {
                    pTileMgr->enqueue(x, y, &work);
                }
            }
        }

        primMask &= ~(1 << primIndex);
    }

endBinLines:

    AR_END(FEBinLines, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Bin SIMD lines to the backend.
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains line position data for SIMDs worth of points.
/// @param primID - Primitive ID for each line.
/// @param viewportIdx - Viewport Array Index for each line.
void BinLines(
    DRAW_CONTEXT *pDC,
    PA_STATE& pa,
    uint32_t workerId,
    simdvector prim[],
    uint32_t primMask,
    simdscalari primID,
    simdscalari viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_FRONTEND_STATE& feState = state.frontendState;
    const SWR_GS_STATE& gsState = state.gsState;

    // Select attribute processor
    PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(2,
        state.backendState.swizzleEnable, state.backendState.constantInterpolationMask);

    simdscalar vRecipW[2] = { _simd_set1_ps(1.0f), _simd_set1_ps(1.0f) };

    if (!feState.vpTransformDisable)
    {
        // perspective divide
        vRecipW[0] = _simd_div_ps(_simd_set1_ps(1.0f), prim[0].w);
        vRecipW[1] = _simd_div_ps(_simd_set1_ps(1.0f), prim[1].w);

        prim[0].v[0] = _simd_mul_ps(prim[0].v[0], vRecipW[0]);
        prim[1].v[0] = _simd_mul_ps(prim[1].v[0], vRecipW[1]);

        prim[0].v[1] = _simd_mul_ps(prim[0].v[1], vRecipW[0]);
        prim[1].v[1] = _simd_mul_ps(prim[1].v[1], vRecipW[1]);

        prim[0].v[2] = _simd_mul_ps(prim[0].v[2], vRecipW[0]);
        prim[1].v[2] = _simd_mul_ps(prim[1].v[2], vRecipW[1]);

        // viewport transform to screen coords
        if (state.gsState.emitsViewportArrayIndex)
        {
            viewportTransform<2>(prim, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<2>(prim, state.vpMatrices);
        }
    }

    // adjust for pixel center location
    simdscalar offset = g_pixelOffsets[rastState.pixelLocation];
    prim[0].x = _simd_add_ps(prim[0].x, offset);
    prim[0].y = _simd_add_ps(prim[0].y, offset);

    prim[1].x = _simd_add_ps(prim[1].x, offset);
    prim[1].y = _simd_add_ps(prim[1].y, offset);

    BinPostSetupLines(
        pDC,
        pa,
        workerId,
        prim,
        vRecipW,
        primMask,
        primID,
        viewportIdx);
}
