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

#include "binner.h"
#include "context.h"
#include "frontend.h"
#include "conservativeRast.h"
#include "pa.h"
#include "rasterizer.h"
#include "rdtsc_core.h"
#include "tilemgr.h"

// Function Prototype
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupLinesImpl(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    typename SIMD_T::Vec4 prim[],
    typename SIMD_T::Float recipW[],
    uint32_t primMask,
    typename SIMD_T::Integer const &primID,
    typename SIMD_T::Integer const &viewportIdx);

template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupPointsImpl(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    typename SIMD_T::Vec4 prim[],
    uint32_t primMask,
    typename SIMD_T::Integer const &primID,
    typename SIMD_T::Integer const &viewportIdx);

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
    uint32_t constantInterpMask = IsDegenerate::value ? 0xFFFFFFFF : backendState.constantInterpolationMask;
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
            inputSlot = backendState.vertexAttribOffset + attribSwizzle.sourceAttrib;

        }
        else
        {
            inputSlot = backendState.vertexAttribOffset + i;
        }

        simd4scalar attrib[3];    // triangle attribs (always 4 wide)
        float* pAttribStart = pBuffer;

        if (HasConstantInterpT::value || IsDegenerate::value)
        {
            if (CheckBit(constantInterpMask, i))
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
                    SIMD128::store_ps(pBuffer, attrib[vid]);
                    pBuffer += 4;
                }
            }
            else
            {
                pa.AssembleSingle(inputSlot, triIndex, attrib);

                for (uint32_t i = 0; i < NumVertsT::value; ++i)
                {
                    SIMD128::store_ps(pBuffer, attrib[i]);
                    pBuffer += 4;
                }
            }
        }
        else
        {
            pa.AssembleSingle(inputSlot, triIndex, attrib);

            for (uint32_t i = 0; i < NumVertsT::value; ++i)
            {
                SIMD128::store_ps(pBuffer, attrib[i]);
                pBuffer += 4;
            }
        }

        // pad out the attrib buffer to 3 verts to ensure the triangle
        // interpolation code in the pixel shader works correctly for the
        // 3 topologies - point, line, tri.  This effectively zeros out the
        // effect of the missing vertices in the triangle interpolation.
        for (uint32_t v = NumVertsT::value; v < 3; ++v)
        {
            SIMD128::store_ps(pBuffer, attrib[NumVertsT::value - 1]);
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
static void GatherScissors(const SWR_RECT *pScissorsInFixedPoint, const uint32_t *pViewportIndex,
    simdscalari &scisXmin, simdscalari &scisYmin, simdscalari &scisXmax, simdscalari &scisYmax)
{
    scisXmin = _simd_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].xmin,
        pScissorsInFixedPoint[pViewportIndex[1]].xmin,
        pScissorsInFixedPoint[pViewportIndex[2]].xmin,
        pScissorsInFixedPoint[pViewportIndex[3]].xmin,
        pScissorsInFixedPoint[pViewportIndex[4]].xmin,
        pScissorsInFixedPoint[pViewportIndex[5]].xmin,
        pScissorsInFixedPoint[pViewportIndex[6]].xmin,
        pScissorsInFixedPoint[pViewportIndex[7]].xmin);
    scisYmin = _simd_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].ymin,
        pScissorsInFixedPoint[pViewportIndex[1]].ymin,
        pScissorsInFixedPoint[pViewportIndex[2]].ymin,
        pScissorsInFixedPoint[pViewportIndex[3]].ymin,
        pScissorsInFixedPoint[pViewportIndex[4]].ymin,
        pScissorsInFixedPoint[pViewportIndex[5]].ymin,
        pScissorsInFixedPoint[pViewportIndex[6]].ymin,
        pScissorsInFixedPoint[pViewportIndex[7]].ymin);
    scisXmax = _simd_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].xmax,
        pScissorsInFixedPoint[pViewportIndex[1]].xmax,
        pScissorsInFixedPoint[pViewportIndex[2]].xmax,
        pScissorsInFixedPoint[pViewportIndex[3]].xmax,
        pScissorsInFixedPoint[pViewportIndex[4]].xmax,
        pScissorsInFixedPoint[pViewportIndex[5]].xmax,
        pScissorsInFixedPoint[pViewportIndex[6]].xmax,
        pScissorsInFixedPoint[pViewportIndex[7]].xmax);
    scisYmax = _simd_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].ymax,
        pScissorsInFixedPoint[pViewportIndex[1]].ymax,
        pScissorsInFixedPoint[pViewportIndex[2]].ymax,
        pScissorsInFixedPoint[pViewportIndex[3]].ymax,
        pScissorsInFixedPoint[pViewportIndex[4]].ymax,
        pScissorsInFixedPoint[pViewportIndex[5]].ymax,
        pScissorsInFixedPoint[pViewportIndex[6]].ymax,
        pScissorsInFixedPoint[pViewportIndex[7]].ymax);
}

static void GatherScissors(const SWR_RECT *pScissorsInFixedPoint, const uint32_t *pViewportIndex,
    simd16scalari &scisXmin, simd16scalari &scisYmin, simd16scalari &scisXmax, simd16scalari &scisYmax)
{
    scisXmin = _simd16_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].xmin,
        pScissorsInFixedPoint[pViewportIndex[1]].xmin,
        pScissorsInFixedPoint[pViewportIndex[2]].xmin,
        pScissorsInFixedPoint[pViewportIndex[3]].xmin,
        pScissorsInFixedPoint[pViewportIndex[4]].xmin,
        pScissorsInFixedPoint[pViewportIndex[5]].xmin,
        pScissorsInFixedPoint[pViewportIndex[6]].xmin,
        pScissorsInFixedPoint[pViewportIndex[7]].xmin,
        pScissorsInFixedPoint[pViewportIndex[8]].xmin,
        pScissorsInFixedPoint[pViewportIndex[9]].xmin,
        pScissorsInFixedPoint[pViewportIndex[10]].xmin,
        pScissorsInFixedPoint[pViewportIndex[11]].xmin,
        pScissorsInFixedPoint[pViewportIndex[12]].xmin,
        pScissorsInFixedPoint[pViewportIndex[13]].xmin,
        pScissorsInFixedPoint[pViewportIndex[14]].xmin,
        pScissorsInFixedPoint[pViewportIndex[15]].xmin);

    scisYmin = _simd16_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].ymin,
        pScissorsInFixedPoint[pViewportIndex[1]].ymin,
        pScissorsInFixedPoint[pViewportIndex[2]].ymin,
        pScissorsInFixedPoint[pViewportIndex[3]].ymin,
        pScissorsInFixedPoint[pViewportIndex[4]].ymin,
        pScissorsInFixedPoint[pViewportIndex[5]].ymin,
        pScissorsInFixedPoint[pViewportIndex[6]].ymin,
        pScissorsInFixedPoint[pViewportIndex[7]].ymin,
        pScissorsInFixedPoint[pViewportIndex[8]].ymin,
        pScissorsInFixedPoint[pViewportIndex[9]].ymin,
        pScissorsInFixedPoint[pViewportIndex[10]].ymin,
        pScissorsInFixedPoint[pViewportIndex[11]].ymin,
        pScissorsInFixedPoint[pViewportIndex[12]].ymin,
        pScissorsInFixedPoint[pViewportIndex[13]].ymin,
        pScissorsInFixedPoint[pViewportIndex[14]].ymin,
        pScissorsInFixedPoint[pViewportIndex[15]].ymin);

    scisXmax = _simd16_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].xmax,
        pScissorsInFixedPoint[pViewportIndex[1]].xmax,
        pScissorsInFixedPoint[pViewportIndex[2]].xmax,
        pScissorsInFixedPoint[pViewportIndex[3]].xmax,
        pScissorsInFixedPoint[pViewportIndex[4]].xmax,
        pScissorsInFixedPoint[pViewportIndex[5]].xmax,
        pScissorsInFixedPoint[pViewportIndex[6]].xmax,
        pScissorsInFixedPoint[pViewportIndex[7]].xmax,
        pScissorsInFixedPoint[pViewportIndex[8]].xmax,
        pScissorsInFixedPoint[pViewportIndex[9]].xmax,
        pScissorsInFixedPoint[pViewportIndex[10]].xmax,
        pScissorsInFixedPoint[pViewportIndex[11]].xmax,
        pScissorsInFixedPoint[pViewportIndex[12]].xmax,
        pScissorsInFixedPoint[pViewportIndex[13]].xmax,
        pScissorsInFixedPoint[pViewportIndex[14]].xmax,
        pScissorsInFixedPoint[pViewportIndex[15]].xmax);

    scisYmax = _simd16_set_epi32(
        pScissorsInFixedPoint[pViewportIndex[0]].ymax,
        pScissorsInFixedPoint[pViewportIndex[1]].ymax,
        pScissorsInFixedPoint[pViewportIndex[2]].ymax,
        pScissorsInFixedPoint[pViewportIndex[3]].ymax,
        pScissorsInFixedPoint[pViewportIndex[4]].ymax,
        pScissorsInFixedPoint[pViewportIndex[5]].ymax,
        pScissorsInFixedPoint[pViewportIndex[6]].ymax,
        pScissorsInFixedPoint[pViewportIndex[7]].ymax,
        pScissorsInFixedPoint[pViewportIndex[8]].ymax,
        pScissorsInFixedPoint[pViewportIndex[9]].ymax,
        pScissorsInFixedPoint[pViewportIndex[10]].ymax,
        pScissorsInFixedPoint[pViewportIndex[11]].ymax,
        pScissorsInFixedPoint[pViewportIndex[12]].ymax,
        pScissorsInFixedPoint[pViewportIndex[13]].ymax,
        pScissorsInFixedPoint[pViewportIndex[14]].ymax,
        pScissorsInFixedPoint[pViewportIndex[15]].ymax);
}

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
void ProcessUserClipDist(const SWR_BACKEND_STATE& state, PA_STATE& pa, uint32_t primIndex, float *pRecipW, float* pUserClipBuffer)
{
    DWORD clipDist;
    uint32_t clipDistMask = state.clipDistanceMask;
    while (_BitScanForward(&clipDist, clipDistMask))
    {
        clipDistMask &= ~(1 << clipDist);
        uint32_t clipSlot = clipDist >> 2;
        uint32_t clipComp = clipDist & 0x3;
        uint32_t clipAttribSlot = clipSlot == 0 ?
            state.vertexClipCullOffset : state.vertexClipCullOffset + 1;

        simd4scalar primClipDist[3];
        pa.AssembleSingle(clipAttribSlot, primIndex, primClipDist);

        float vertClipDist[NumVerts];
        for (uint32_t e = 0; e < NumVerts; ++e)
        {
            OSALIGNSIMD(float) aVertClipDist[4];
            SIMD128::store_ps(aVertClipDist, primClipDist[e]);
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

INLINE
void TransposeVertices(simd4scalar(&dst)[8], const simdscalar &src0, const simdscalar &src1, const simdscalar &src2)
{
    vTranspose3x8(dst, src0, src1, src2);
}

INLINE
void TransposeVertices(simd4scalar(&dst)[16], const simd16scalar &src0, const simd16scalar &src1, const simd16scalar &src2)
{
    vTranspose4x16(reinterpret_cast<simd16scalar(&)[4]>(dst), src0, src1, src2, _simd16_setzero_ps());
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
template <typename SIMD_T, uint32_t SIMD_WIDTH, typename CT>
void SIMDCALL BinTrianglesImpl(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    typename SIMD_T::Vec4 tri[3],
    uint32_t triMask,
    typename SIMD_T::Integer const &primID)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(FEBinTriangles, pDC->drawId);

    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_FRONTEND_STATE& feState = state.frontendState;

    MacroTileMgr *pTileMgr = pDC->pTileMgr;

    typename SIMD_T::Float vRecipW0 = SIMD_T::set1_ps(1.0f);
    typename SIMD_T::Float vRecipW1 = SIMD_T::set1_ps(1.0f);
    typename SIMD_T::Float vRecipW2 = SIMD_T::set1_ps(1.0f);

    typename SIMD_T::Integer viewportIdx = SIMD_T::setzero_si();
    typename SIMD_T::Vec4 vpiAttrib[3];
    typename SIMD_T::Integer vpai = SIMD_T::setzero_si();

    if (state.backendState.readViewportArrayIndex)
    {
        pa.Assemble(VERTEX_SGV_SLOT, vpiAttrib);

        vpai = SIMD_T::castps_si(vpiAttrib[0][VERTEX_SGV_VAI_COMP]);
    }


    if (state.backendState.readViewportArrayIndex) // VPAIOffsets are guaranteed 0-15 -- no OOB issues if they are offsets from 0 
    {
        // OOB indices => forced to zero.
        vpai = SIMD_T::max_epi32(vpai, SIMD_T::setzero_si());
        typename SIMD_T::Integer vNumViewports = SIMD_T::set1_epi32(KNOB_NUM_VIEWPORTS_SCISSORS);
        typename SIMD_T::Integer vClearMask = SIMD_T::cmplt_epi32(vpai, vNumViewports);
        viewportIdx = SIMD_T::and_si(vClearMask, vpai);
    }

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
        vRecipW0 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), tri[0].w);
        vRecipW1 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), tri[1].w);
        vRecipW2 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), tri[2].w);

        tri[0].v[0] = SIMD_T::mul_ps(tri[0].v[0], vRecipW0);
        tri[1].v[0] = SIMD_T::mul_ps(tri[1].v[0], vRecipW1);
        tri[2].v[0] = SIMD_T::mul_ps(tri[2].v[0], vRecipW2);

        tri[0].v[1] = SIMD_T::mul_ps(tri[0].v[1], vRecipW0);
        tri[1].v[1] = SIMD_T::mul_ps(tri[1].v[1], vRecipW1);
        tri[2].v[1] = SIMD_T::mul_ps(tri[2].v[1], vRecipW2);

        tri[0].v[2] = SIMD_T::mul_ps(tri[0].v[2], vRecipW0);
        tri[1].v[2] = SIMD_T::mul_ps(tri[1].v[2], vRecipW1);
        tri[2].v[2] = SIMD_T::mul_ps(tri[2].v[2], vRecipW2);

        // Viewport transform to screen space coords
        if (state.backendState.readViewportArrayIndex)
        {
            viewportTransform<3>(tri, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<3>(tri, state.vpMatrices);
        }
    }

    // Adjust for pixel center location
    typename SIMD_T::Float offset = SwrPixelOffsets<SIMD_T>::GetOffset(rastState.pixelLocation);

    tri[0].x = SIMD_T::add_ps(tri[0].x, offset);
    tri[0].y = SIMD_T::add_ps(tri[0].y, offset);

    tri[1].x = SIMD_T::add_ps(tri[1].x, offset);
    tri[1].y = SIMD_T::add_ps(tri[1].y, offset);

    tri[2].x = SIMD_T::add_ps(tri[2].x, offset);
    tri[2].y = SIMD_T::add_ps(tri[2].y, offset);

    // Set vXi, vYi to required fixed point precision
    typename SIMD_T::Integer vXi[3], vYi[3];
    FPToFixedPoint<SIMD_T>(tri, vXi, vYi);

    // triangle setup
    typename SIMD_T::Integer vAi[3], vBi[3];
    triangleSetupABIntVertical(vXi, vYi, vAi, vBi);

    // determinant
    typename SIMD_T::Integer vDet[2];
    calcDeterminantIntVertical(vAi, vBi, vDet);

    // cull zero area
    uint32_t maskLo = SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpeq_epi64(vDet[0], SIMD_T::setzero_si())));
    uint32_t maskHi = SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpeq_epi64(vDet[1], SIMD_T::setzero_si())));

    uint32_t cullZeroAreaMask = maskLo | (maskHi << (SIMD_WIDTH / 2));

    // don't cull degenerate triangles if we're conservatively rasterizing
    uint32_t origTriMask = triMask;
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
        maskLo = SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(vDet[0], SIMD_T::setzero_si())));
        maskHi = SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(vDet[1], SIMD_T::setzero_si())));
    }
    else
    {
        maskLo = SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(SIMD_T::setzero_si(), vDet[0])));
        maskHi = SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(SIMD_T::setzero_si(), vDet[1])));
    }
    frontWindingTris = maskLo | (maskHi << (SIMD_WIDTH / 2));

    // cull
    uint32_t cullTris;
    switch ((SWR_CULLMODE)rastState.cullMode)
    {
    case SWR_CULLMODE_BOTH:  cullTris = 0xffffffff; break;
    case SWR_CULLMODE_NONE:  cullTris = 0x0; break;
    case SWR_CULLMODE_FRONT: cullTris = frontWindingTris; break;
        // 0 area triangles are marked as backfacing, which is required behavior for conservative rast
    case SWR_CULLMODE_BACK:  cullTris = ~frontWindingTris; break;
    default: SWR_INVALID("Invalid cull mode: %d", rastState.cullMode); cullTris = 0x0; break;
    }

    triMask &= ~cullTris;

    if (origTriMask ^ triMask)
    {
        RDTSC_EVENT(FECullZeroAreaAndBackface, _mm_popcnt_u32(origTriMask ^ triMask), 0);
    }

    /// Note: these variable initializations must stay above any 'goto endBenTriangles'
    // compute per tri backface
    uint32_t frontFaceMask = frontWindingTris;
    uint32_t *pPrimID = (uint32_t *)&primID;
    const uint32_t *pViewportIndex = (uint32_t *)&viewportIdx;
    DWORD triIndex = 0;

    uint32_t edgeEnable;
    PFN_WORK_FUNC pfnWork;
    if (CT::IsConservativeT::value)
    {
        // determine which edges of the degenerate tri, if any, are valid to rasterize.
        // used to call the appropriate templated rasterizer function
        if (cullZeroAreaMask > 0)
        {
            // e0 = v1-v0
            const typename SIMD_T::Integer x0x1Mask = SIMD_T::cmpeq_epi32(vXi[0], vXi[1]);
            const typename SIMD_T::Integer y0y1Mask = SIMD_T::cmpeq_epi32(vYi[0], vYi[1]);

            uint32_t e0Mask = SIMD_T::movemask_ps(SIMD_T::castsi_ps(SIMD_T::and_si(x0x1Mask, y0y1Mask)));

            // e1 = v2-v1
            const typename SIMD_T::Integer x1x2Mask = SIMD_T::cmpeq_epi32(vXi[1], vXi[2]);
            const typename SIMD_T::Integer y1y2Mask = SIMD_T::cmpeq_epi32(vYi[1], vYi[2]);

            uint32_t e1Mask = SIMD_T::movemask_ps(SIMD_T::castsi_ps(SIMD_T::and_si(x1x2Mask, y1y2Mask)));

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
        pfnWork = GetRasterizerFunc(rastState.sampleCount, rastState.bIsCenterPattern, (rastState.conservativeRast > 0),
            (SWR_INPUT_COVERAGE)pDC->pState->state.psState.inputCoverage, EdgeValToEdgeState(ALL_EDGES_VALID), (state.scissorsTileAligned == false));
    }

    SIMDBBOX_T<SIMD_T> bbox;

    if (!triMask)
    {
        goto endBinTriangles;
    }

    // Calc bounding box of triangles
    calcBoundingBoxIntVertical<SIMD_T, CT>(vXi, vYi, bbox);

    // determine if triangle falls between pixel centers and discard
    // only discard for non-MSAA case and when conservative rast is disabled
    // (xmin + 127) & ~255
    // (xmax + 128) & ~255
    if ((rastState.sampleCount == SWR_MULTISAMPLE_1X || rastState.bIsCenterPattern) &&
        (!CT::IsConservativeT::value))
    {
        origTriMask = triMask;

        int cullCenterMask;

        {
            typename SIMD_T::Integer xmin = SIMD_T::add_epi32(bbox.xmin, SIMD_T::set1_epi32(127));
            xmin = SIMD_T::and_si(xmin, SIMD_T::set1_epi32(~255));
            typename SIMD_T::Integer xmax = SIMD_T::add_epi32(bbox.xmax, SIMD_T::set1_epi32(128));
            xmax = SIMD_T::and_si(xmax, SIMD_T::set1_epi32(~255));

            typename SIMD_T::Integer vMaskH = SIMD_T::cmpeq_epi32(xmin, xmax);

            typename SIMD_T::Integer ymin = SIMD_T::add_epi32(bbox.ymin, SIMD_T::set1_epi32(127));
            ymin = SIMD_T::and_si(ymin, SIMD_T::set1_epi32(~255));
            typename SIMD_T::Integer ymax = SIMD_T::add_epi32(bbox.ymax, SIMD_T::set1_epi32(128));
            ymax = SIMD_T::and_si(ymax, SIMD_T::set1_epi32(~255));

            typename SIMD_T::Integer vMaskV = SIMD_T::cmpeq_epi32(ymin, ymax);

            vMaskV = SIMD_T::or_si(vMaskH, vMaskV);
            cullCenterMask = SIMD_T::movemask_ps(SIMD_T::castsi_ps(vMaskV));
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
    {
        typename SIMD_T::Integer scisXmin, scisYmin, scisXmax, scisYmax;

        if (state.backendState.readViewportArrayIndex)
        {
            GatherScissors(&state.scissorsInFixedPoint[0], pViewportIndex, scisXmin, scisYmin, scisXmax, scisYmax);
        }
        else // broadcast fast path for non-VPAI case.
        {
            scisXmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmin);
            scisYmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymin);
            scisXmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmax);
            scisYmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymax);
        }

        // Make triangle bbox inclusive
        bbox.xmax = SIMD_T::sub_epi32(bbox.xmax, SIMD_T::set1_epi32(1));
        bbox.ymax = SIMD_T::sub_epi32(bbox.ymax, SIMD_T::set1_epi32(1));

        bbox.xmin = SIMD_T::max_epi32(bbox.xmin, scisXmin);
        bbox.ymin = SIMD_T::max_epi32(bbox.ymin, scisYmin);
        bbox.xmax = SIMD_T::min_epi32(bbox.xmax, scisXmax);
        bbox.ymax = SIMD_T::min_epi32(bbox.ymax, scisYmax);
    }

    if (CT::IsConservativeT::value)
    {
        // in the case where a degenerate triangle is on a scissor edge, we need to make sure the primitive bbox has
        // some area. Bump the xmax/ymax edges out 

        typename SIMD_T::Integer topEqualsBottom = SIMD_T::cmpeq_epi32(bbox.ymin, bbox.ymax);
        bbox.ymax = SIMD_T::blendv_epi32(bbox.ymax, SIMD_T::add_epi32(bbox.ymax, SIMD_T::set1_epi32(1)), topEqualsBottom);

        typename SIMD_T::Integer leftEqualsRight = SIMD_T::cmpeq_epi32(bbox.xmin, bbox.xmax);
        bbox.xmax = SIMD_T::blendv_epi32(bbox.xmax, SIMD_T::add_epi32(bbox.xmax, SIMD_T::set1_epi32(1)), leftEqualsRight);
    }

    // Cull tris completely outside scissor
    {
        typename SIMD_T::Integer maskOutsideScissorX = SIMD_T::cmpgt_epi32(bbox.xmin, bbox.xmax);
        typename SIMD_T::Integer maskOutsideScissorY = SIMD_T::cmpgt_epi32(bbox.ymin, bbox.ymax);
        typename SIMD_T::Integer maskOutsideScissorXY = SIMD_T::or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = SIMD_T::movemask_ps(SIMD_T::castsi_ps(maskOutsideScissorXY));
        triMask = triMask & ~maskOutsideScissor;
    }

endBinTriangles:


    // Send surviving triangles to the line or point binner based on fill mode
    if (rastState.fillMode == SWR_FILLMODE_WIREFRAME)
    {
        // Simple non-conformant wireframe mode, useful for debugging
        // construct 3 SIMD lines out of the triangle and call the line binner for each SIMD
        typename SIMD_T::Vec4 line[2];
        typename SIMD_T::Float recipW[2];

        line[0] = tri[0];
        line[1] = tri[1];
        recipW[0] = vRecipW0;
        recipW[1] = vRecipW1;

        BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx);

        line[0] = tri[1];
        line[1] = tri[2];
        recipW[0] = vRecipW1;
        recipW[1] = vRecipW2;

        BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx);

        line[0] = tri[2];
        line[1] = tri[0];
        recipW[0] = vRecipW2;
        recipW[1] = vRecipW0;

        BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx);

        AR_END(FEBinTriangles, 1);
        return;
    }
    else if (rastState.fillMode == SWR_FILLMODE_POINT)
    {
        // Bin 3 points
        BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(pDC, pa, workerId, &tri[0], triMask, primID, viewportIdx);
        BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(pDC, pa, workerId, &tri[1], triMask, primID, viewportIdx);
        BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(pDC, pa, workerId, &tri[2], triMask, primID, viewportIdx);

        AR_END(FEBinTriangles, 1);
        return;
    }

    // Convert triangle bbox to macrotile units.
    bbox.xmin = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmin);
    bbox.ymin = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymin);
    bbox.xmax = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmax);
    bbox.ymax = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymax);

    OSALIGNSIMD16(uint32_t) aMTLeft[SIMD_WIDTH], aMTRight[SIMD_WIDTH], aMTTop[SIMD_WIDTH], aMTBottom[SIMD_WIDTH];

    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTLeft),   bbox.xmin);
    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTRight),  bbox.xmax);
    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTTop),    bbox.ymin);
    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTBottom), bbox.ymax);

    // transpose verts needed for backend
    /// @todo modify BE to take non-transformed verts
    simd4scalar vHorizX[SIMD_WIDTH];
    simd4scalar vHorizY[SIMD_WIDTH];
    simd4scalar vHorizZ[SIMD_WIDTH];
    simd4scalar vHorizW[SIMD_WIDTH];

    TransposeVertices(vHorizX, tri[0].x, tri[1].x, tri[2].x);
    TransposeVertices(vHorizY, tri[0].y, tri[1].y, tri[2].y);
    TransposeVertices(vHorizZ, tri[0].z, tri[1].z, tri[2].z);
    TransposeVertices(vHorizW, vRecipW0, vRecipW1, vRecipW2);

    // store render target array index
    OSALIGNSIMD16(uint32_t) aRTAI[SIMD_WIDTH];
    if (state.backendState.readRenderTargetArrayIndex)
    {
        typename SIMD_T::Vec4 vRtai[3];
        pa.Assemble(VERTEX_SGV_SLOT, vRtai);
        typename SIMD_T::Integer vRtaii;
        vRtaii = SIMD_T::castps_si(vRtai[0][VERTEX_SGV_RTAI_COMP]);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), vRtaii);
    }
    else
    {
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), SIMD_T::setzero_si());
    }


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
            work.pfnWork = GetRasterizerFunc(rastState.sampleCount, rastState.bIsCenterPattern, (rastState.conservativeRast > 0),
                (SWR_INPUT_COVERAGE)pDC->pState->state.psState.inputCoverage, EdgeValToEdgeState(triEdgeEnable), (state.scissorsTileAligned == false));

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

        SIMD128::store_ps(&desc.pTriBuffer[0],  vHorizX[triIndex]);
        SIMD128::store_ps(&desc.pTriBuffer[4],  vHorizY[triIndex]);
        SIMD128::store_ps(&desc.pTriBuffer[8],  vHorizZ[triIndex]);
        SIMD128::store_ps(&desc.pTriBuffer[12], vHorizW[triIndex]);

        // store user clip distances
        if (state.backendState.clipDistanceMask)
        {
            uint32_t numClipDist = _mm_popcnt_u32(state.backendState.clipDistanceMask);
            desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 3 * sizeof(float));
            ProcessUserClipDist<3>(state.backendState, pa, triIndex, &desc.pTriBuffer[12], desc.pUserClipBuffer);
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

template <typename CT>
void BinTriangles(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    simdvector tri[3],
    uint32_t triMask,
    simdscalari const &primID)
{
    BinTrianglesImpl<SIMD256, KNOB_SIMD_WIDTH, CT>(pDC, pa, workerId, tri, triMask, primID);
}

#if USE_SIMD16_FRONTEND
template <typename CT>
void SIMDCALL BinTriangles_simd16(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    simd16vector tri[3],
    uint32_t triMask,
    simd16scalari const &primID)
{
    BinTrianglesImpl<SIMD512, KNOB_SIMD16_WIDTH, CT>(pDC, pa, workerId, tri, triMask, primID);
}

#endif
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

#if USE_SIMD16_FRONTEND
struct FEBinTrianglesChooser_simd16
{
    typedef PFN_PROCESS_PRIMS_SIMD16 FuncType;

    template <typename... ArgsB>
    static FuncType GetFunc()
    {
        return BinTriangles_simd16<ConservativeRastFETraits<ArgsB...>>;
    }
};

// Selector for correct templated BinTrinagles function
PFN_PROCESS_PRIMS_SIMD16 GetBinTrianglesFunc_simd16(bool IsConservative)
{
    return TemplateArgUnroller<FEBinTrianglesChooser_simd16>::GetFunc(IsConservative);
}

#endif

template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupPointsImpl(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    typename SIMD_T::Vec4 prim[],
    uint32_t primMask,
    typename SIMD_T::Integer const &primID,
    typename SIMD_T::Integer const &viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(FEBinPoints, pDC->drawId);

    typename SIMD_T::Vec4 &primVerts = prim[0];

    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const uint32_t *pViewportIndex = (uint32_t *)&viewportIdx;

    // Select attribute processor
    PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(1,
        state.backendState.swizzleEnable, state.backendState.constantInterpolationMask);

    // convert to fixed point
    typename SIMD_T::Integer vXi, vYi;

    vXi = fpToFixedPointVertical<SIMD_T>(primVerts.x);
    vYi = fpToFixedPointVertical<SIMD_T>(primVerts.y);

    if (CanUseSimplePoints(pDC))
    {
        // adjust for ymin-xmin rule
        vXi = SIMD_T::sub_epi32(vXi, SIMD_T::set1_epi32(1));
        vYi = SIMD_T::sub_epi32(vYi, SIMD_T::set1_epi32(1));

        // cull points off the ymin-xmin edge of the viewport
        primMask &= ~SIMD_T::movemask_ps(SIMD_T::castsi_ps(vXi));
        primMask &= ~SIMD_T::movemask_ps(SIMD_T::castsi_ps(vYi));

        // compute macro tile coordinates 
        typename SIMD_T::Integer macroX = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(vXi);
        typename SIMD_T::Integer macroY = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(vYi);

        OSALIGNSIMD16(uint32_t) aMacroX[SIMD_WIDTH], aMacroY[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMacroX), macroX);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMacroY), macroY);

        // compute raster tile coordinates
        typename SIMD_T::Integer rasterX = SIMD_T::template srai_epi32<KNOB_TILE_X_DIM_SHIFT + FIXED_POINT_SHIFT>(vXi);
        typename SIMD_T::Integer rasterY = SIMD_T::template srai_epi32<KNOB_TILE_Y_DIM_SHIFT + FIXED_POINT_SHIFT>(vYi);

        // compute raster tile relative x,y for coverage mask
        typename SIMD_T::Integer tileAlignedX = SIMD_T::template slli_epi32<KNOB_TILE_X_DIM_SHIFT>(rasterX);
        typename SIMD_T::Integer tileAlignedY = SIMD_T::template slli_epi32<KNOB_TILE_Y_DIM_SHIFT>(rasterY);

        typename SIMD_T::Integer tileRelativeX = SIMD_T::sub_epi32(SIMD_T::template srai_epi32<FIXED_POINT_SHIFT>(vXi), tileAlignedX);
        typename SIMD_T::Integer tileRelativeY = SIMD_T::sub_epi32(SIMD_T::template srai_epi32<FIXED_POINT_SHIFT>(vYi), tileAlignedY);

        OSALIGNSIMD16(uint32_t) aTileRelativeX[SIMD_WIDTH];
        OSALIGNSIMD16(uint32_t) aTileRelativeY[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aTileRelativeX), tileRelativeX);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aTileRelativeY), tileRelativeY);

        OSALIGNSIMD16(uint32_t) aTileAlignedX[SIMD_WIDTH];
        OSALIGNSIMD16(uint32_t) aTileAlignedY[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aTileAlignedX), tileAlignedX);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aTileAlignedY), tileAlignedY);

        OSALIGNSIMD16(float) aZ[SIMD_WIDTH];
        SIMD_T::store_ps(reinterpret_cast<float *>(aZ), primVerts.z);

        // store render target array index
        OSALIGNSIMD16(uint32_t) aRTAI[SIMD_WIDTH];
        if (state.backendState.readRenderTargetArrayIndex)
        {
            typename SIMD_T::Vec4 vRtai;
            pa.Assemble(VERTEX_SGV_SLOT, &vRtai);
            typename SIMD_T::Integer vRtaii = SIMD_T::castps_si(vRtai[VERTEX_SGV_RTAI_COMP]);
            SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), vRtaii);
        }
        else
        {
            SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), SIMD_T::setzero_si());
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
        typename SIMD_T::Float vPointSize;

        if (rastState.pointParam)
        {
            typename SIMD_T::Vec4 size[3];
            pa.Assemble(VERTEX_SGV_SLOT, size);
            vPointSize = size[0][VERTEX_SGV_POINT_SIZE_COMP];
        }
        else
        {
            vPointSize = SIMD_T::set1_ps(rastState.pointSize);
        }

        // bloat point to bbox
        SIMDBBOX_T<SIMD_T> bbox;

        bbox.xmin = bbox.xmax = vXi;
        bbox.ymin = bbox.ymax = vYi;

        typename SIMD_T::Float vHalfWidth = SIMD_T::mul_ps(vPointSize, SIMD_T::set1_ps(0.5f));
        typename SIMD_T::Integer vHalfWidthi = fpToFixedPointVertical<SIMD_T>(vHalfWidth);

        bbox.xmin = SIMD_T::sub_epi32(bbox.xmin, vHalfWidthi);
        bbox.xmax = SIMD_T::add_epi32(bbox.xmax, vHalfWidthi);
        bbox.ymin = SIMD_T::sub_epi32(bbox.ymin, vHalfWidthi);
        bbox.ymax = SIMD_T::add_epi32(bbox.ymax, vHalfWidthi);

        // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge is exclusive.
        // Gather the AOS effective scissor rects based on the per-prim VP index.
        /// @todo:  Look at speeding this up -- weigh against corresponding costs in rasterizer.
        {
            typename SIMD_T::Integer scisXmin, scisYmin, scisXmax, scisYmax;

            if (state.backendState.readViewportArrayIndex)
            {
                GatherScissors(&state.scissorsInFixedPoint[0], pViewportIndex, scisXmin, scisYmin, scisXmax, scisYmax);
            }
            else // broadcast fast path for non-VPAI case.
            {
                scisXmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmin);
                scisYmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymin);
                scisXmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmax);
                scisYmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymax);
            }

            bbox.xmin = SIMD_T::max_epi32(bbox.xmin, scisXmin);
            bbox.ymin = SIMD_T::max_epi32(bbox.ymin, scisYmin);
            bbox.xmax = SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.xmax, SIMD_T::set1_epi32(1)), scisXmax);
            bbox.ymax = SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.ymax, SIMD_T::set1_epi32(1)), scisYmax);
        }

        // Cull bloated points completely outside scissor
        typename SIMD_T::Integer maskOutsideScissorX = SIMD_T::cmpgt_epi32(bbox.xmin, bbox.xmax);
        typename SIMD_T::Integer maskOutsideScissorY = SIMD_T::cmpgt_epi32(bbox.ymin, bbox.ymax);
        typename SIMD_T::Integer maskOutsideScissorXY = SIMD_T::or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = SIMD_T::movemask_ps(SIMD_T::castsi_ps(maskOutsideScissorXY));
        primMask = primMask & ~maskOutsideScissor;

        // Convert bbox to macrotile units.
        bbox.xmin = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmin);
        bbox.ymin = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymin);
        bbox.xmax = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmax);
        bbox.ymax = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymax);

        OSALIGNSIMD16(uint32_t) aMTLeft[SIMD_WIDTH], aMTRight[SIMD_WIDTH], aMTTop[SIMD_WIDTH], aMTBottom[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTLeft),   bbox.xmin);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTRight),  bbox.xmax);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTTop),    bbox.ymin);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTBottom), bbox.ymax);

        // store render target array index
        OSALIGNSIMD16(uint32_t) aRTAI[SIMD_WIDTH];
        if (state.backendState.readRenderTargetArrayIndex)
        {
            typename SIMD_T::Vec4 vRtai[2];
            pa.Assemble(VERTEX_SGV_SLOT, vRtai);
            typename SIMD_T::Integer vRtaii = SIMD_T::castps_si(vRtai[0][VERTEX_SGV_RTAI_COMP]);
            SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), vRtaii);
        }
        else
        {
            SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), SIMD_T::setzero_si());
        }

        OSALIGNSIMD16(float) aPointSize[SIMD_WIDTH];
        SIMD_T::store_ps(reinterpret_cast<float *>(aPointSize), vPointSize);

        uint32_t *pPrimID = (uint32_t *)&primID;

        OSALIGNSIMD16(float) aPrimVertsX[SIMD_WIDTH];
        OSALIGNSIMD16(float) aPrimVertsY[SIMD_WIDTH];
        OSALIGNSIMD16(float) aPrimVertsZ[SIMD_WIDTH];

        SIMD_T::store_ps(reinterpret_cast<float *>(aPrimVertsX), primVerts.x);
        SIMD_T::store_ps(reinterpret_cast<float *>(aPrimVertsY), primVerts.y);
        SIMD_T::store_ps(reinterpret_cast<float *>(aPrimVertsZ), primVerts.z);

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
            if (backendState.clipDistanceMask)
            {
                uint32_t numClipDist = _mm_popcnt_u32(backendState.clipDistanceMask);
                desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 3 * sizeof(float));
                float dists[8];
                float one = 1.0f;
                ProcessUserClipDist<1>(backendState, pa, primIndex, &one, dists);
                for (uint32_t i = 0; i < numClipDist; i++) {
                    desc.pUserClipBuffer[3 * i + 0] = 0.0f;
                    desc.pUserClipBuffer[3 * i + 1] = 0.0f;
                    desc.pUserClipBuffer[3 * i + 2] = dists[i];
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
/// @brief Bin SIMD points to the backend.  Only supports point size of 1
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains point position data for SIMDs worth of points.
/// @param primID - Primitive ID for each point.
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPointsImpl(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    typename SIMD_T::Vec4 prim[3],
    uint32_t primMask,
    typename SIMD_T::Integer const &primID)
{
    const API_STATE& state = GetApiState(pDC);
    const SWR_FRONTEND_STATE& feState = state.frontendState;
    const SWR_RASTSTATE& rastState = state.rastState;

    // Read back viewport index if required
    typename SIMD_T::Integer viewportIdx = SIMD_T::setzero_si();
    typename SIMD_T::Vec4 vpiAttrib[1];
    typename SIMD_T::Integer vpai = SIMD_T::setzero_si();

    if (state.backendState.readViewportArrayIndex)
    {
        pa.Assemble(VERTEX_SGV_SLOT, vpiAttrib);

        vpai = SIMD_T::castps_si(vpiAttrib[0][VERTEX_SGV_VAI_COMP]);
    }


    if (state.backendState.readViewportArrayIndex) // VPAIOffsets are guaranteed 0-15 -- no OOB issues if they are offsets from 0 
    {
        // OOB indices => forced to zero.
        vpai = SIMD_T::max_epi32(vpai, SIMD_T::setzero_si());
        typename SIMD_T::Integer vNumViewports = SIMD_T::set1_epi32(KNOB_NUM_VIEWPORTS_SCISSORS);
        typename SIMD_T::Integer vClearMask = SIMD_T::cmplt_epi32(vpai, vNumViewports);
        viewportIdx = SIMD_T::and_si(vClearMask, vpai);
    }

    if (!feState.vpTransformDisable)
    {
        // perspective divide
        typename SIMD_T::Float vRecipW0 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), prim[0].w);

        prim[0].x = SIMD_T::mul_ps(prim[0].x, vRecipW0);
        prim[0].y = SIMD_T::mul_ps(prim[0].y, vRecipW0);
        prim[0].z = SIMD_T::mul_ps(prim[0].z, vRecipW0);

        // viewport transform to screen coords
        if (state.backendState.readViewportArrayIndex)
        {
            viewportTransform<1>(prim, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<1>(prim, state.vpMatrices);
        }
    }

    typename SIMD_T::Float offset = SwrPixelOffsets<SIMD_T>::GetOffset(rastState.pixelLocation);

    prim[0].x = SIMD_T::add_ps(prim[0].x, offset);
    prim[0].y = SIMD_T::add_ps(prim[0].y, offset);

    BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(
        pDC,
        pa,
        workerId,
        prim,
        primMask,
        primID,
        viewportIdx);
}

void BinPoints(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    simdvector prim[3],
    uint32_t primMask,
    simdscalari const &primID)
{
    BinPointsImpl<SIMD256, KNOB_SIMD_WIDTH>(
        pDC,
        pa,
        workerId,
        prim,
        primMask,
        primID);
}

#if USE_SIMD16_FRONTEND
void SIMDCALL BinPoints_simd16(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    simd16vector prim[3],
    uint32_t primMask,
    simd16scalari const &primID)
{
    BinPointsImpl<SIMD512, KNOB_SIMD16_WIDTH>(
        pDC,
        pa,
        workerId,
        prim,
        primMask,
        primID);
}

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief Bin SIMD lines to the backend.
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains line position data for SIMDs worth of points.
/// @param primID - Primitive ID for each line.
/// @param viewportIdx - Viewport Array Index for each line.
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupLinesImpl(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    typename SIMD_T::Vec4 prim[],
    typename SIMD_T::Float recipW[],
    uint32_t primMask,
    typename SIMD_T::Integer const &primID,
    typename SIMD_T::Integer const &viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(FEBinLines, pDC->drawId);

    const API_STATE &state = GetApiState(pDC);
    const SWR_RASTSTATE &rastState = state.rastState;

    // Select attribute processor
    PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(2,
        state.backendState.swizzleEnable, state.backendState.constantInterpolationMask);

    typename SIMD_T::Float &vRecipW0 = recipW[0];
    typename SIMD_T::Float &vRecipW1 = recipW[1];

    // convert to fixed point
    typename SIMD_T::Integer vXi[2], vYi[2];

    vXi[0] = fpToFixedPointVertical<SIMD_T>(prim[0].x);
    vYi[0] = fpToFixedPointVertical<SIMD_T>(prim[0].y);
    vXi[1] = fpToFixedPointVertical<SIMD_T>(prim[1].x);
    vYi[1] = fpToFixedPointVertical<SIMD_T>(prim[1].y);

    // compute x-major vs y-major mask
    typename SIMD_T::Integer xLength = SIMD_T::abs_epi32(SIMD_T::sub_epi32(vXi[0], vXi[1]));
    typename SIMD_T::Integer yLength = SIMD_T::abs_epi32(SIMD_T::sub_epi32(vYi[0], vYi[1]));
    typename SIMD_T::Float vYmajorMask = SIMD_T::castsi_ps(SIMD_T::cmpgt_epi32(yLength, xLength));
    uint32_t yMajorMask = SIMD_T::movemask_ps(vYmajorMask);

    // cull zero-length lines
    typename SIMD_T::Integer vZeroLengthMask = SIMD_T::cmpeq_epi32(xLength, SIMD_T::setzero_si());
    vZeroLengthMask = SIMD_T::and_si(vZeroLengthMask, SIMD_T::cmpeq_epi32(yLength, SIMD_T::setzero_si()));

    primMask &= ~SIMD_T::movemask_ps(SIMD_T::castsi_ps(vZeroLengthMask));

    uint32_t *pPrimID = (uint32_t *)&primID;
    const uint32_t *pViewportIndex = (uint32_t *)&viewportIdx;

    // Calc bounding box of lines
    SIMDBBOX_T<SIMD_T> bbox;
    bbox.xmin = SIMD_T::min_epi32(vXi[0], vXi[1]);
    bbox.xmax = SIMD_T::max_epi32(vXi[0], vXi[1]);
    bbox.ymin = SIMD_T::min_epi32(vYi[0], vYi[1]);
    bbox.ymax = SIMD_T::max_epi32(vYi[0], vYi[1]);

    // bloat bbox by line width along minor axis
    typename SIMD_T::Float vHalfWidth = SIMD_T::set1_ps(rastState.lineWidth / 2.0f);
    typename SIMD_T::Integer vHalfWidthi = fpToFixedPointVertical<SIMD_T>(vHalfWidth);

    SIMDBBOX_T<SIMD_T> bloatBox;

    bloatBox.xmin = SIMD_T::sub_epi32(bbox.xmin, vHalfWidthi);
    bloatBox.xmax = SIMD_T::add_epi32(bbox.xmax, vHalfWidthi);
    bloatBox.ymin = SIMD_T::sub_epi32(bbox.ymin, vHalfWidthi);
    bloatBox.ymax = SIMD_T::add_epi32(bbox.ymax, vHalfWidthi);

    bbox.xmin = SIMD_T::blendv_epi32(bbox.xmin, bloatBox.xmin, vYmajorMask);
    bbox.xmax = SIMD_T::blendv_epi32(bbox.xmax, bloatBox.xmax, vYmajorMask);
    bbox.ymin = SIMD_T::blendv_epi32(bloatBox.ymin, bbox.ymin, vYmajorMask);
    bbox.ymax = SIMD_T::blendv_epi32(bloatBox.ymax, bbox.ymax, vYmajorMask);

    // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge is exclusive.
    {
        typename SIMD_T::Integer scisXmin, scisYmin, scisXmax, scisYmax;

        if (state.backendState.readViewportArrayIndex)
        {
            GatherScissors(&state.scissorsInFixedPoint[0], pViewportIndex, scisXmin, scisYmin, scisXmax, scisYmax);
        }
        else // broadcast fast path for non-VPAI case.
        {
            scisXmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmin);
            scisYmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymin);
            scisXmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmax);
            scisYmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymax);
        }

        bbox.xmin = SIMD_T::max_epi32(bbox.xmin, scisXmin);
        bbox.ymin = SIMD_T::max_epi32(bbox.ymin, scisYmin);
        bbox.xmax = SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.xmax, SIMD_T::set1_epi32(1)), scisXmax);
        bbox.ymax = SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.ymax, SIMD_T::set1_epi32(1)), scisYmax);
    }

    // Cull prims completely outside scissor
    {
        typename SIMD_T::Integer maskOutsideScissorX = SIMD_T::cmpgt_epi32(bbox.xmin, bbox.xmax);
        typename SIMD_T::Integer maskOutsideScissorY = SIMD_T::cmpgt_epi32(bbox.ymin, bbox.ymax);
        typename SIMD_T::Integer maskOutsideScissorXY = SIMD_T::or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = SIMD_T::movemask_ps(SIMD_T::castsi_ps(maskOutsideScissorXY));
        primMask = primMask & ~maskOutsideScissor;
    }

    // transpose verts needed for backend
    /// @todo modify BE to take non-transformed verts
    simd4scalar vHorizX[SIMD_WIDTH];
    simd4scalar vHorizY[SIMD_WIDTH];
    simd4scalar vHorizZ[SIMD_WIDTH];
    simd4scalar vHorizW[SIMD_WIDTH];

    if (!primMask)
    {
        goto endBinLines;
    }

    // Convert triangle bbox to macrotile units.
    bbox.xmin = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmin);
    bbox.ymin = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymin);
    bbox.xmax = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmax);
    bbox.ymax = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymax);

    OSALIGNSIMD16(uint32_t) aMTLeft[SIMD_WIDTH], aMTRight[SIMD_WIDTH], aMTTop[SIMD_WIDTH], aMTBottom[SIMD_WIDTH];

    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTLeft),   bbox.xmin);
    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTRight),  bbox.xmax);
    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTTop),    bbox.ymin);
    SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aMTBottom), bbox.ymax);

    TransposeVertices(vHorizX, prim[0].x, prim[1].x, SIMD_T::setzero_ps());
    TransposeVertices(vHorizY, prim[0].y, prim[1].y, SIMD_T::setzero_ps());
    TransposeVertices(vHorizZ, prim[0].z, prim[1].z, SIMD_T::setzero_ps());
    TransposeVertices(vHorizW, vRecipW0,  vRecipW1,  SIMD_T::setzero_ps());

    // store render target array index
    OSALIGNSIMD16(uint32_t) aRTAI[SIMD_WIDTH];
    if (state.backendState.readRenderTargetArrayIndex)
    {
        typename SIMD_T::Vec4 vRtai[2];
        pa.Assemble(VERTEX_SGV_SLOT, vRtai);
        typename SIMD_T::Integer vRtaii = SIMD_T::castps_si(vRtai[0][VERTEX_SGV_RTAI_COMP]);
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), vRtaii);
    }
    else
    {
        SIMD_T::store_si(reinterpret_cast<typename SIMD_T::Integer *>(aRTAI), SIMD_T::setzero_si());
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

        _mm_store_ps(&desc.pTriBuffer[0],  vHorizX[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[4],  vHorizY[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[8],  vHorizZ[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[12], vHorizW[primIndex]);

        // store user clip distances
        if (state.backendState.clipDistanceMask)
        {
            uint32_t numClipDist = _mm_popcnt_u32(state.backendState.clipDistanceMask);
            desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 2 * sizeof(float));
            ProcessUserClipDist<2>(state.backendState, pa, primIndex, &desc.pTriBuffer[12], desc.pUserClipBuffer);
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
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void SIMDCALL BinLinesImpl(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    typename SIMD_T::Vec4 prim[3],
    uint32_t primMask,
    typename SIMD_T::Integer const &primID)
{
    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_FRONTEND_STATE& feState = state.frontendState;

    typename SIMD_T::Float vRecipW[2] = { SIMD_T::set1_ps(1.0f), SIMD_T::set1_ps(1.0f) };

    typename SIMD_T::Integer viewportIdx = SIMD_T::setzero_si();
    typename SIMD_T::Vec4 vpiAttrib[2];
    typename SIMD_T::Integer vpai = SIMD_T::setzero_si();

    if (state.backendState.readViewportArrayIndex)
    {
        pa.Assemble(VERTEX_SGV_SLOT, vpiAttrib);

        vpai = SIMD_T::castps_si(vpiAttrib[0][VERTEX_SGV_VAI_COMP]);
    }


    if (state.backendState.readViewportArrayIndex) // VPAIOffsets are guaranteed 0-15 -- no OOB issues if they are offsets from 0 
    {
        // OOB indices => forced to zero.
        vpai = SIMD_T::max_epi32(vpai, SIMD_T::setzero_si());
        typename SIMD_T::Integer vNumViewports = SIMD_T::set1_epi32(KNOB_NUM_VIEWPORTS_SCISSORS);
        typename SIMD_T::Integer vClearMask = SIMD_T::cmplt_epi32(vpai, vNumViewports);
        viewportIdx = SIMD_T::and_si(vClearMask, vpai);
    }

    if (!feState.vpTransformDisable)
    {
        // perspective divide
        vRecipW[0] = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), prim[0].w);
        vRecipW[1] = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), prim[1].w);

        prim[0].v[0] = SIMD_T::mul_ps(prim[0].v[0], vRecipW[0]);
        prim[1].v[0] = SIMD_T::mul_ps(prim[1].v[0], vRecipW[1]);

        prim[0].v[1] = SIMD_T::mul_ps(prim[0].v[1], vRecipW[0]);
        prim[1].v[1] = SIMD_T::mul_ps(prim[1].v[1], vRecipW[1]);

        prim[0].v[2] = SIMD_T::mul_ps(prim[0].v[2], vRecipW[0]);
        prim[1].v[2] = SIMD_T::mul_ps(prim[1].v[2], vRecipW[1]);

        // viewport transform to screen coords
        if (state.backendState.readViewportArrayIndex)
        {
            viewportTransform<2>(prim, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<2>(prim, state.vpMatrices);
        }
    }

    // adjust for pixel center location
    typename SIMD_T::Float offset = SwrPixelOffsets<SIMD_T>::GetOffset(rastState.pixelLocation);

    prim[0].x = SIMD_T::add_ps(prim[0].x, offset);
    prim[0].y = SIMD_T::add_ps(prim[0].y, offset);

    prim[1].x = SIMD_T::add_ps(prim[1].x, offset);
    prim[1].y = SIMD_T::add_ps(prim[1].y, offset);

    BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(
        pDC,
        pa,
        workerId,
        prim,
        vRecipW,
        primMask,
        primID,
        viewportIdx);
}

void BinLines(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    simdvector prim[],
    uint32_t primMask,
    simdscalari const &primID)
{
    BinLinesImpl<SIMD256, KNOB_SIMD_WIDTH>(pDC, pa, workerId, prim, primMask, primID);
}

#if USE_SIMD16_FRONTEND
void SIMDCALL BinLines_simd16(
    DRAW_CONTEXT *pDC,
    PA_STATE &pa,
    uint32_t workerId,
    simd16vector prim[3],
    uint32_t primMask,
    simd16scalari const &primID)
{
    BinLinesImpl<SIMD512, KNOB_SIMD16_WIDTH>(pDC, pa, workerId, prim, primMask, primID);
}

#endif
