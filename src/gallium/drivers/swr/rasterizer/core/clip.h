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
* @file clip.h
*
* @brief Definitions for clipping
*
******************************************************************************/
#pragma once

#include "common/simdintrin.h"
#include "core/context.h"
#include "core/pa.h"
#include "rdtsc_core.h"

// Temp storage used by the clipper
extern THREAD simdvertex tlsTempVertices[7];
#if USE_SIMD16_FRONTEND
extern THREAD simd16vertex tlsTempVertices_simd16[7];
#endif

enum SWR_CLIPCODES
{
    // Shift clip codes out of the mantissa to prevent denormalized values when used in float compare.
    // Guardband is able to use a single high-bit with 4 separate LSBs, because it computes a union, rather than intersection, of clipcodes.
#define CLIPCODE_SHIFT 23
    FRUSTUM_LEFT    = (0x01 << CLIPCODE_SHIFT),
    FRUSTUM_TOP     = (0x02 << CLIPCODE_SHIFT),
    FRUSTUM_RIGHT   = (0x04 << CLIPCODE_SHIFT),
    FRUSTUM_BOTTOM  = (0x08 << CLIPCODE_SHIFT),

    FRUSTUM_NEAR    = (0x10 << CLIPCODE_SHIFT),
    FRUSTUM_FAR     = (0x20 << CLIPCODE_SHIFT),

    NEGW            = (0x40 << CLIPCODE_SHIFT),

    GUARDBAND_LEFT   = (0x80 << CLIPCODE_SHIFT | 0x1),
    GUARDBAND_TOP    = (0x80 << CLIPCODE_SHIFT | 0x2),
    GUARDBAND_RIGHT  = (0x80 << CLIPCODE_SHIFT | 0x4),
    GUARDBAND_BOTTOM = (0x80 << CLIPCODE_SHIFT | 0x8)
};

#define GUARDBAND_CLIP_MASK (FRUSTUM_NEAR|FRUSTUM_FAR|GUARDBAND_LEFT|GUARDBAND_TOP|GUARDBAND_RIGHT|GUARDBAND_BOTTOM|NEGW)

INLINE
void ComputeClipCodes(const API_STATE& state, const simdvector& vertex, simdscalar& clipCodes, simdscalari viewportIndexes)
{
    clipCodes = _simd_setzero_ps();

    // -w
    simdscalar vNegW = _simd_mul_ps(vertex.w, _simd_set1_ps(-1.0f));

    // FRUSTUM_LEFT
    simdscalar vRes = _simd_cmplt_ps(vertex.x, vNegW);
    clipCodes = _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(FRUSTUM_LEFT)));

    // FRUSTUM_TOP
    vRes = _simd_cmplt_ps(vertex.y, vNegW);
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(FRUSTUM_TOP))));

    // FRUSTUM_RIGHT
    vRes = _simd_cmpgt_ps(vertex.x, vertex.w);
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(FRUSTUM_RIGHT))));

    // FRUSTUM_BOTTOM
    vRes = _simd_cmpgt_ps(vertex.y, vertex.w);
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(FRUSTUM_BOTTOM))));

    if (state.rastState.depthClipEnable)
    {
        // FRUSTUM_NEAR
        // DX clips depth [0..w], GL clips [-w..w]
        if (state.rastState.clipHalfZ)
        {
            vRes = _simd_cmplt_ps(vertex.z, _simd_setzero_ps());
        }
        else
        {
            vRes = _simd_cmplt_ps(vertex.z, vNegW);
        }
        clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(FRUSTUM_NEAR))));

        // FRUSTUM_FAR
        vRes = _simd_cmpgt_ps(vertex.z, vertex.w);
        clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(FRUSTUM_FAR))));
    }

    // NEGW
    vRes = _simd_cmple_ps(vertex.w, _simd_setzero_ps());
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(NEGW))));

    // GUARDBAND_LEFT
    simdscalar gbMult = _simd_mul_ps(vNegW, _simd_i32gather_ps(&state.gbState.left[0], viewportIndexes, 4));
    vRes = _simd_cmplt_ps(vertex.x, gbMult);
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(GUARDBAND_LEFT))));

    // GUARDBAND_TOP
    gbMult = _simd_mul_ps(vNegW, _simd_i32gather_ps(&state.gbState.top[0], viewportIndexes, 4));
    vRes = _simd_cmplt_ps(vertex.y, gbMult);
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(GUARDBAND_TOP))));

    // GUARDBAND_RIGHT
    gbMult = _simd_mul_ps(vertex.w, _simd_i32gather_ps(&state.gbState.right[0], viewportIndexes, 4));
    vRes = _simd_cmpgt_ps(vertex.x, gbMult);
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(GUARDBAND_RIGHT))));

    // GUARDBAND_BOTTOM
    gbMult = _simd_mul_ps(vertex.w, _simd_i32gather_ps(&state.gbState.bottom[0], viewportIndexes, 4));
    vRes = _simd_cmpgt_ps(vertex.y, gbMult);
    clipCodes = _simd_or_ps(clipCodes, _simd_and_ps(vRes, _simd_castsi_ps(_simd_set1_epi32(GUARDBAND_BOTTOM))));
}

#if USE_SIMD16_FRONTEND
INLINE
void ComputeClipCodes(const API_STATE& state, const simd16vector& vertex, simd16scalar& clipCodes, simd16scalari viewportIndexes)
{
    clipCodes = _simd16_setzero_ps();

    // -w
    simd16scalar vNegW = _simd16_mul_ps(vertex.w, _simd16_set1_ps(-1.0f));

    // FRUSTUM_LEFT
    simd16scalar vRes = _simd16_cmplt_ps(vertex.x, vNegW);
    clipCodes = _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(FRUSTUM_LEFT)));

    // FRUSTUM_TOP
    vRes = _simd16_cmplt_ps(vertex.y, vNegW);
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(FRUSTUM_TOP))));

    // FRUSTUM_RIGHT
    vRes = _simd16_cmpgt_ps(vertex.x, vertex.w);
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(FRUSTUM_RIGHT))));

    // FRUSTUM_BOTTOM
    vRes = _simd16_cmpgt_ps(vertex.y, vertex.w);
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(FRUSTUM_BOTTOM))));

    if (state.rastState.depthClipEnable)
    {
        // FRUSTUM_NEAR
        // DX clips depth [0..w], GL clips [-w..w]
        if (state.rastState.clipHalfZ)
        {
            vRes = _simd16_cmplt_ps(vertex.z, _simd16_setzero_ps());
        }
        else
        {
            vRes = _simd16_cmplt_ps(vertex.z, vNegW);
        }
        clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(FRUSTUM_NEAR))));

        // FRUSTUM_FAR
        vRes = _simd16_cmpgt_ps(vertex.z, vertex.w);
        clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(FRUSTUM_FAR))));
    }

    // NEGW
    vRes = _simd16_cmple_ps(vertex.w, _simd16_setzero_ps());
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(NEGW))));

    // GUARDBAND_LEFT
    simd16scalar gbMult = _simd16_mul_ps(vNegW, _simd16_i32gather_ps(&state.gbState.left[0], viewportIndexes, 4));
    vRes = _simd16_cmplt_ps(vertex.x, gbMult);
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(GUARDBAND_LEFT))));

    // GUARDBAND_TOP
    gbMult = _simd16_mul_ps(vNegW, _simd16_i32gather_ps(&state.gbState.top[0], viewportIndexes, 4));
    vRes = _simd16_cmplt_ps(vertex.y, gbMult);
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(GUARDBAND_TOP))));

    // GUARDBAND_RIGHT
    gbMult = _simd16_mul_ps(vertex.w, _simd16_i32gather_ps(&state.gbState.right[0], viewportIndexes, 4));
    vRes = _simd16_cmpgt_ps(vertex.x, gbMult);
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(GUARDBAND_RIGHT))));

    // GUARDBAND_BOTTOM
    gbMult = _simd16_mul_ps(vertex.w, _simd16_i32gather_ps(&state.gbState.bottom[0], viewportIndexes, 4));
    vRes = _simd16_cmpgt_ps(vertex.y, gbMult);
    clipCodes = _simd16_or_ps(clipCodes, _simd16_and_ps(vRes, _simd16_castsi_ps(_simd16_set1_epi32(GUARDBAND_BOTTOM))));
}

#endif
template<uint32_t NumVertsPerPrim>
class Clipper
{
public:
    Clipper(uint32_t in_workerId, DRAW_CONTEXT* in_pDC) :
        workerId(in_workerId), pDC(in_pDC), state(GetApiState(in_pDC))
    {
        static_assert(NumVertsPerPrim >= 1 && NumVertsPerPrim <= 3, "Invalid NumVertsPerPrim");
    }

    void ComputeClipCodes(simdvector vertex[], simdscalari viewportIndexes)
    {
        for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
        {
            ::ComputeClipCodes(this->state, vertex[i], this->clipCodes[i], viewportIndexes);
        }
    }

#if USE_SIMD16_FRONTEND
    void ComputeClipCodes(simd16vector vertex[], simd16scalari viewportIndexes)
    {
        for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
        {
            ::ComputeClipCodes(this->state, vertex[i], this->clipCodes_simd16[i], viewportIndexes);
        }
    }

#endif
    simdscalar ComputeClipCodeIntersection()
    {
        simdscalar result = this->clipCodes[0];
        for (uint32_t i = 1; i < NumVertsPerPrim; ++i)
        {
            result = _simd_and_ps(result, this->clipCodes[i]);
        }
        return result;
    }

#if USE_SIMD16_FRONTEND
    simd16scalar ComputeClipCodeIntersection_simd16()
    {
        simd16scalar result = this->clipCodes_simd16[0];
        for (uint32_t i = 1; i < NumVertsPerPrim; ++i)
        {
            result = _simd16_and_ps(result, this->clipCodes_simd16[i]);
        }
        return result;
    }

#endif
    simdscalar ComputeClipCodeUnion()
    {
        simdscalar result = this->clipCodes[0];
        for (uint32_t i = 1; i < NumVertsPerPrim; ++i)
        {
            result = _simd_or_ps(result, this->clipCodes[i]);
        }
        return result;
    }

#if USE_SIMD16_FRONTEND
    simd16scalar ComputeClipCodeUnion_simd16()
    {
        simd16scalar result = this->clipCodes_simd16[0];
        for (uint32_t i = 1; i < NumVertsPerPrim; ++i)
        {
            result = _simd16_or_ps(result, this->clipCodes_simd16[i]);
        }
        return result;
    }

#endif
    int ComputeNegWMask()
    {
        simdscalar clipCodeUnion = ComputeClipCodeUnion();
        clipCodeUnion = _simd_and_ps(clipCodeUnion, _simd_castsi_ps(_simd_set1_epi32(NEGW)));
        return _simd_movemask_ps(_simd_cmpneq_ps(clipCodeUnion, _simd_setzero_ps()));
    }

    int ComputeClipMask()
    {
        simdscalar clipUnion = ComputeClipCodeUnion();
        clipUnion = _simd_and_ps(clipUnion, _simd_castsi_ps(_simd_set1_epi32(GUARDBAND_CLIP_MASK)));
        return _simd_movemask_ps(_simd_cmpneq_ps(clipUnion, _simd_setzero_ps()));
    }

#if USE_SIMD16_FRONTEND
    int ComputeClipMask_simd16()
    {
        simd16scalar clipUnion = ComputeClipCodeUnion_simd16();
        clipUnion = _simd16_and_ps(clipUnion, _simd16_castsi_ps(_simd16_set1_epi32(GUARDBAND_CLIP_MASK)));
        return _simd16_movemask_ps(_simd16_cmpneq_ps(clipUnion, _simd16_setzero_ps()));
    }

#endif
    // clipper is responsible for culling any prims with NAN coordinates
    int ComputeNaNMask(simdvector prim[])
    {
        simdscalar vNanMask = _simd_setzero_ps();
        for (uint32_t e = 0; e < NumVertsPerPrim; ++e)
        {
            simdscalar vNan01 = _simd_cmp_ps(prim[e].v[0], prim[e].v[1], _CMP_UNORD_Q);
            vNanMask = _simd_or_ps(vNanMask, vNan01);
            simdscalar vNan23 = _simd_cmp_ps(prim[e].v[2], prim[e].v[3], _CMP_UNORD_Q);
            vNanMask = _simd_or_ps(vNanMask, vNan23);
        }

        return _simd_movemask_ps(vNanMask);
    }

#if USE_SIMD16_FRONTEND
    int ComputeNaNMask(simd16vector prim[])
    {
        simd16scalar vNanMask = _simd16_setzero_ps();
        for (uint32_t e = 0; e < NumVertsPerPrim; ++e)
        {
            simd16scalar vNan01 = _simd16_cmp_ps(prim[e].v[0], prim[e].v[1], _CMP_UNORD_Q);
            vNanMask = _simd16_or_ps(vNanMask, vNan01);
            simd16scalar vNan23 = _simd16_cmp_ps(prim[e].v[2], prim[e].v[3], _CMP_UNORD_Q);
            vNanMask = _simd16_or_ps(vNanMask, vNan23);
        }

        return _simd16_movemask_ps(vNanMask);
    }

#endif
    int ComputeUserClipCullMask(PA_STATE& pa, simdvector prim[])
    {
        uint8_t cullMask = this->state.rastState.cullDistanceMask;
        simdscalar vClipCullMask = _simd_setzero_ps();
        DWORD index;

        simdvector vClipCullDistLo[3];
        simdvector vClipCullDistHi[3];

        pa.Assemble(VERTEX_CLIPCULL_DIST_LO_SLOT, vClipCullDistLo);
        pa.Assemble(VERTEX_CLIPCULL_DIST_HI_SLOT, vClipCullDistHi);
        while (_BitScanForward(&index, cullMask))
        {
            cullMask &= ~(1 << index);
            uint32_t slot = index >> 2;
            uint32_t component = index & 0x3;

            simdscalar vCullMaskElem = _simd_set1_ps(-1.0f);
            for (uint32_t e = 0; e < NumVertsPerPrim; ++e)
            {
                simdscalar vCullComp;
                if (slot == 0)
                {
                    vCullComp = vClipCullDistLo[e][component];
                }
                else
                {
                    vCullComp = vClipCullDistHi[e][component];
                }

                // cull if cull distance < 0 || NAN
                simdscalar vCull = _simd_cmp_ps(_mm256_setzero_ps(), vCullComp, _CMP_NLE_UQ);
                vCullMaskElem = _simd_and_ps(vCullMaskElem, vCull);
            }
            vClipCullMask = _simd_or_ps(vClipCullMask, vCullMaskElem);
        }

        // clipper should also discard any primitive with NAN clip distance
        uint8_t clipMask = this->state.rastState.clipDistanceMask;
        while (_BitScanForward(&index, clipMask))
        {
            clipMask &= ~(1 << index);
            uint32_t slot = index >> 2;
            uint32_t component = index & 0x3;

            for (uint32_t e = 0; e < NumVertsPerPrim; ++e)
            {
                simdscalar vClipComp;
                if (slot == 0)
                {
                    vClipComp = vClipCullDistLo[e][component];
                }
                else
                {
                    vClipComp = vClipCullDistHi[e][component];
                }

                simdscalar vClip = _simd_cmp_ps(vClipComp, vClipComp, _CMP_UNORD_Q);
                vClipCullMask = _simd_or_ps(vClipCullMask, vClip);
            }
        }

        return _simd_movemask_ps(vClipCullMask);
    }

#if USE_SIMD16_FRONTEND
    int ComputeUserClipCullMask(PA_STATE& pa, simd16vector prim[])
    {
        uint8_t cullMask = this->state.rastState.cullDistanceMask;
        simd16scalar vClipCullMask = _simd16_setzero_ps();

        simd16vector vClipCullDistLo[3];
        simd16vector vClipCullDistHi[3];

        pa.Assemble_simd16(VERTEX_CLIPCULL_DIST_LO_SLOT, vClipCullDistLo);
        pa.Assemble_simd16(VERTEX_CLIPCULL_DIST_HI_SLOT, vClipCullDistHi);

        DWORD index;
        while (_BitScanForward(&index, cullMask))
        {
            cullMask &= ~(1 << index);
            uint32_t slot = index >> 2;
            uint32_t component = index & 0x3;

            simd16scalar vCullMaskElem = _simd16_set1_ps(-1.0f);
            for (uint32_t e = 0; e < NumVertsPerPrim; ++e)
            {
                simd16scalar vCullComp;
                if (slot == 0)
                {
                    vCullComp = vClipCullDistLo[e][component];
                }
                else
                {
                    vCullComp = vClipCullDistHi[e][component];
                }

                // cull if cull distance < 0 || NAN
                simd16scalar vCull = _simd16_cmp_ps(_simd16_setzero_ps(), vCullComp, _CMP_NLE_UQ);
                vCullMaskElem = _simd16_and_ps(vCullMaskElem, vCull);
            }
            vClipCullMask = _simd16_or_ps(vClipCullMask, vCullMaskElem);
        }

        // clipper should also discard any primitive with NAN clip distance
        uint8_t clipMask = this->state.rastState.clipDistanceMask;
        while (_BitScanForward(&index, clipMask))
        {
            clipMask &= ~(1 << index);
            uint32_t slot = index >> 2;
            uint32_t component = index & 0x3;

            for (uint32_t e = 0; e < NumVertsPerPrim; ++e)
            {
                simd16scalar vClipComp;
                if (slot == 0)
                {
                    vClipComp = vClipCullDistLo[e][component];
                }
                else
                {
                    vClipComp = vClipCullDistHi[e][component];
                }

                simd16scalar vClip = _simd16_cmp_ps(vClipComp, vClipComp, _CMP_UNORD_Q);
                vClipCullMask = _simd16_or_ps(vClipCullMask, vClip);
            }
        }

        return _simd16_movemask_ps(vClipCullMask);
    }

#endif
    // clip SIMD primitives
    void ClipSimd(const simdscalar& vPrimMask, const simdscalar& vClipMask, PA_STATE& pa, const simdscalari& vPrimId)
    {
        // input/output vertex store for clipper
        simdvertex vertices[7]; // maximum 7 verts generated per triangle

        LONG constantInterpMask = this->state.backendState.constantInterpolationMask;
        uint32_t provokingVertex = 0;
        if(pa.binTopology == TOP_TRIANGLE_FAN)
        {
            provokingVertex = this->state.frontendState.provokingVertex.triFan;
        }
        ///@todo: line topology for wireframe?

        // assemble pos
        simdvector tmpVector[NumVertsPerPrim];
        pa.Assemble(VERTEX_POSITION_SLOT, tmpVector);
        for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
        {
            vertices[i].attrib[VERTEX_POSITION_SLOT] = tmpVector[i];
        }

        // assemble attribs
        const SWR_BACKEND_STATE& backendState = this->state.backendState;

        int32_t maxSlot = -1;
        for (uint32_t slot = 0; slot < backendState.numAttributes; ++slot)
        {
            // Compute absolute attrib slot in vertex array
            uint32_t mapSlot = backendState.swizzleEnable ? backendState.swizzleMap[slot].sourceAttrib : slot;
            maxSlot = std::max<int32_t>(maxSlot, mapSlot);
            uint32_t inputSlot = backendState.vertexAttribOffset + mapSlot;

            pa.Assemble(inputSlot, tmpVector);

            // if constant interpolation enabled for this attribute, assign the provoking
            // vertex values to all edges
            if (_bittest(&constantInterpMask, slot))
            {
                for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
                {
                    vertices[i].attrib[inputSlot] = tmpVector[provokingVertex];
                }
            }
            else
            {
                for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
                {
                    vertices[i].attrib[inputSlot] = tmpVector[i];
                }
            }
        }

        // assemble user clip distances if enabled
        if (this->state.rastState.clipDistanceMask & 0xf)
        {
            pa.Assemble(VERTEX_CLIPCULL_DIST_LO_SLOT, tmpVector);
            for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
            {
                vertices[i].attrib[VERTEX_CLIPCULL_DIST_LO_SLOT] = tmpVector[i];
            }
        }

        if (this->state.rastState.clipDistanceMask & 0xf0)
        {
            pa.Assemble(VERTEX_CLIPCULL_DIST_HI_SLOT, tmpVector);
            for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
            {
                vertices[i].attrib[VERTEX_CLIPCULL_DIST_HI_SLOT] = tmpVector[i];
            }
        }

        uint32_t numAttribs = maxSlot + 1;

        simdscalari vNumClippedVerts = ClipPrims((float*)&vertices[0], vPrimMask, vClipMask, numAttribs);

        // set up new PA for binning clipped primitives
        PFN_PROCESS_PRIMS pfnBinFunc = nullptr;
        PRIMITIVE_TOPOLOGY clipTopology = TOP_UNKNOWN;
        if (NumVertsPerPrim == 3)
        {
            pfnBinFunc = GetBinTrianglesFunc((pa.pDC->pState->state.rastState.conservativeRast > 0));
            clipTopology = TOP_TRIANGLE_FAN;

            // so that the binner knows to bloat wide points later
            if (pa.binTopology == TOP_POINT_LIST)
                clipTopology = TOP_POINT_LIST;

        }
        else if (NumVertsPerPrim == 2)
        {
            pfnBinFunc = BinLines;
            clipTopology = TOP_LINE_LIST;
        }
        else
        {
            SWR_ASSERT(0 && "Unexpected points in clipper.");
        }
        
        uint32_t* pVertexCount = (uint32_t*)&vNumClippedVerts;
        uint32_t* pPrimitiveId = (uint32_t*)&vPrimId;

        const simdscalari vOffsets = _mm256_set_epi32(
            0 * sizeof(simdvertex),  // unused lane
            6 * sizeof(simdvertex),
            5 * sizeof(simdvertex),
            4 * sizeof(simdvertex),
            3 * sizeof(simdvertex),
            2 * sizeof(simdvertex),
            1 * sizeof(simdvertex),
            0 * sizeof(simdvertex));

        // only need to gather 7 verts
        // @todo dynamic mask based on actual # of verts generated per lane
        const simdscalar vMask = _mm256_set_ps(0, -1, -1, -1, -1, -1, -1, -1);

        uint32_t numClippedPrims = 0;
#if USE_SIMD16_FRONTEND
        const uint32_t numPrims = pa.NumPrims();
        const uint32_t numPrims_lo = std::min<uint32_t>(numPrims, KNOB_SIMD_WIDTH);

        SWR_ASSERT(numPrims <= numPrims_lo);

        for (uint32_t inputPrim = 0; inputPrim < numPrims_lo; ++inputPrim)
#else
        for (uint32_t inputPrim = 0; inputPrim < pa.NumPrims(); ++inputPrim)
#endif
        {
            uint32_t numEmittedVerts = pVertexCount[inputPrim];
            if (numEmittedVerts < NumVertsPerPrim)
            {
                continue;
            }
            SWR_ASSERT(numEmittedVerts <= 7, "Unexpected vertex count from clipper.");

            uint32_t numEmittedPrims = GetNumPrims(clipTopology, numEmittedVerts);
            numClippedPrims += numEmittedPrims;

            // tranpose clipper output so that each lane's vertices are in SIMD order
            // set aside space for 2 vertices, as the PA will try to read up to 16 verts
            // for triangle fan
#if USE_SIMD16_FRONTEND
            simd16vertex transposedPrims[2];
#else
            simdvertex transposedPrims[2];
#endif

            // transpose pos
            uint8_t* pBase = (uint8_t*)(&vertices[0].attrib[VERTEX_POSITION_SLOT]) + sizeof(float) * inputPrim;

#if USE_SIMD16_FRONTEND
            // TEMPORARY WORKAROUND for bizarre VS2015 code-gen bug
            static const float *dummy = reinterpret_cast<const float *>(pBase);
#endif

            for (uint32_t c = 0; c < 4; ++c)
            {
#if USE_SIMD16_FRONTEND
                simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                transposedPrims[0].attrib[VERTEX_POSITION_SLOT][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
#else
                transposedPrims[0].attrib[VERTEX_POSITION_SLOT][c] = _simd_mask_i32gather_ps(_mm256_undefined_ps(), (const float*)pBase, vOffsets, vMask, 1);
#endif
                pBase += sizeof(simdscalar);
            }

            // transpose attribs
            pBase = (uint8_t*)(&vertices[0].attrib[backendState.vertexAttribOffset]) + sizeof(float) * inputPrim;
            for (uint32_t attrib = 0; attrib < numAttribs; ++attrib)
            {
                uint32_t attribSlot = backendState.vertexAttribOffset + attrib;
                for (uint32_t c = 0; c < 4; ++c)
                {
#if USE_SIMD16_FRONTEND
                    simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                    transposedPrims[0].attrib[attribSlot][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
#else
                    transposedPrims[0].attrib[attribSlot][c] = _simd_mask_i32gather_ps(_mm256_undefined_ps(), (const float*)pBase, vOffsets, vMask, 1);
#endif
                    pBase += sizeof(simdscalar);
                }
            }

            // transpose user clip distances if enabled
            if (this->state.rastState.clipDistanceMask & 0xf)
            {
                pBase = (uint8_t*)(&vertices[0].attrib[VERTEX_CLIPCULL_DIST_LO_SLOT]) + sizeof(float) * inputPrim;
                for (uint32_t c = 0; c < 4; ++c)
                {
#if USE_SIMD16_FRONTEND
                    simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                    transposedPrims[0].attrib[VERTEX_CLIPCULL_DIST_LO_SLOT][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
#else
                    transposedPrims[0].attrib[VERTEX_CLIPCULL_DIST_LO_SLOT][c] = _simd_mask_i32gather_ps(_mm256_undefined_ps(), (const float*)pBase, vOffsets, vMask, 1);
#endif
                    pBase += sizeof(simdscalar);
                }
            }

            if (this->state.rastState.clipDistanceMask & 0xf0)
            {
                pBase = (uint8_t*)(&vertices[0].attrib[VERTEX_CLIPCULL_DIST_HI_SLOT]) + sizeof(float) * inputPrim;
                for (uint32_t c = 0; c < 4; ++c)
                {
#if USE_SIMD16_FRONTEND
                    simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                    transposedPrims[0].attrib[VERTEX_CLIPCULL_DIST_HI_SLOT][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
#else
                    transposedPrims[0].attrib[VERTEX_CLIPCULL_DIST_HI_SLOT][c] = _simd_mask_i32gather_ps(_mm256_undefined_ps(), (const float*)pBase, vOffsets, vMask, 1);
#endif
                    pBase += sizeof(simdscalar);
                }
            }

            PA_STATE_OPT clipPa(this->pDC, numEmittedPrims, (uint8_t*)&transposedPrims[0], numEmittedVerts, SWR_VTX_NUM_SLOTS, true, clipTopology);

            while (clipPa.GetNextStreamOutput())
            {
                do
                {
#if USE_SIMD16_FRONTEND
                    simd16vector attrib_simd16[NumVertsPerPrim];
                    bool assemble = clipPa.Assemble_simd16(VERTEX_POSITION_SLOT, attrib_simd16);

                    if (assemble)
                    {
                        static const uint32_t primMaskMap[] = { 0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff };

                        simdvector attrib[NumVertsPerPrim];
                        for (uint32_t i = 0; i < NumVertsPerPrim; i += 1)
                        {
                            for (uint32_t j = 0; j < 4; j += 1)
                            {
                                attrib[i][j] = _simd16_extract_ps(attrib_simd16[i][j], 0);
                            }
                        }

                        clipPa.useAlternateOffset = false;
                        pfnBinFunc(this->pDC, clipPa, this->workerId, attrib, primMaskMap[numEmittedPrims], _simd_set1_epi32(pPrimitiveId[inputPrim]));
                    }
#else
                    simdvector attrib[NumVertsPerPrim];
                    bool assemble = clipPa.Assemble(VERTEX_POSITION_SLOT, attrib);
                    if (assemble)
                    {
                        static const uint32_t primMaskMap[] = { 0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff };
                        pfnBinFunc(this->pDC, clipPa, this->workerId, attrib, primMaskMap[numEmittedPrims], _simd_set1_epi32(pPrimitiveId[inputPrim]));
                    }
#endif
                } while (clipPa.NextPrim());
            }
        }

        // update global pipeline stat
        UPDATE_STAT_FE(CPrimitives, numClippedPrims);
    }
    
#if USE_SIMD16_FRONTEND
    void ClipSimd(const simd16scalar& vPrimMask, const simd16scalar& vClipMask, PA_STATE& pa, const simd16scalari& vPrimId)
    {
        // input/output vertex store for clipper
        simd16vertex vertices[7]; // maximum 7 verts generated per triangle

        LONG constantInterpMask = this->state.backendState.constantInterpolationMask;
        uint32_t provokingVertex = 0;
        if (pa.binTopology == TOP_TRIANGLE_FAN)
        {
            provokingVertex = this->state.frontendState.provokingVertex.triFan;
        }
        ///@todo: line topology for wireframe?

        // assemble pos
        simd16vector tmpVector[NumVertsPerPrim];
        pa.Assemble_simd16(VERTEX_POSITION_SLOT, tmpVector);
        for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
        {
            vertices[i].attrib[VERTEX_POSITION_SLOT] = tmpVector[i];
        }

        // assemble attribs
        const SWR_BACKEND_STATE& backendState = this->state.backendState;

        int32_t maxSlot = -1;
        for (uint32_t slot = 0; slot < backendState.numAttributes; ++slot)
        {
            // Compute absolute attrib slot in vertex array
            uint32_t mapSlot = backendState.swizzleEnable ? backendState.swizzleMap[slot].sourceAttrib : slot;
            maxSlot = std::max<int32_t>(maxSlot, mapSlot);
            uint32_t inputSlot = backendState.vertexAttribOffset + mapSlot;

            pa.Assemble_simd16(inputSlot, tmpVector);

            // if constant interpolation enabled for this attribute, assign the provoking
            // vertex values to all edges
            if (_bittest(&constantInterpMask, slot))
            {
                for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
                {
                    vertices[i].attrib[inputSlot] = tmpVector[provokingVertex];
                }
            }
            else
            {
                for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
                {
                    vertices[i].attrib[inputSlot] = tmpVector[i];
                }
            }
        }

        // assemble user clip distances if enabled
        if (this->state.rastState.clipDistanceMask & 0xf)
        {
            pa.Assemble_simd16(VERTEX_CLIPCULL_DIST_LO_SLOT, tmpVector);
            for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
            {
                vertices[i].attrib[VERTEX_CLIPCULL_DIST_LO_SLOT] = tmpVector[i];
            }
        }

        if (this->state.rastState.clipDistanceMask & 0xf0)
        {
            pa.Assemble_simd16(VERTEX_CLIPCULL_DIST_HI_SLOT, tmpVector);
            for (uint32_t i = 0; i < NumVertsPerPrim; ++i)
            {
                vertices[i].attrib[VERTEX_CLIPCULL_DIST_HI_SLOT] = tmpVector[i];
            }
        }

        uint32_t numAttribs = maxSlot + 1;

        simd16scalari vNumClippedVerts = ClipPrims((float*)&vertices[0], vPrimMask, vClipMask, numAttribs);

        // set up new PA for binning clipped primitives
        PFN_PROCESS_PRIMS_SIMD16 pfnBinFunc = nullptr;
        PRIMITIVE_TOPOLOGY clipTopology = TOP_UNKNOWN;
        if (NumVertsPerPrim == 3)
        {
            pfnBinFunc = GetBinTrianglesFunc_simd16((pa.pDC->pState->state.rastState.conservativeRast > 0));
            clipTopology = TOP_TRIANGLE_FAN;

            // so that the binner knows to bloat wide points later
            if (pa.binTopology == TOP_POINT_LIST)
                clipTopology = TOP_POINT_LIST;

        }
        else if (NumVertsPerPrim == 2)
        {
            pfnBinFunc = BinLines_simd16;
            clipTopology = TOP_LINE_LIST;
        }
        else
        {
            SWR_ASSERT(0 && "Unexpected points in clipper.");
        }

        uint32_t* pVertexCount = (uint32_t*)&vNumClippedVerts;
        uint32_t* pPrimitiveId = (uint32_t*)&vPrimId;

        const simdscalari vOffsets = _simd_set_epi32(
            0 * sizeof(simd16vertex),   // unused lane
            6 * sizeof(simd16vertex),
            5 * sizeof(simd16vertex),
            4 * sizeof(simd16vertex),
            3 * sizeof(simd16vertex),
            2 * sizeof(simd16vertex),
            1 * sizeof(simd16vertex),
            0 * sizeof(simd16vertex));

        // only need to gather 7 verts
        // @todo dynamic mask based on actual # of verts generated per lane
        const simdscalar vMask = _mm256_set_ps(0, -1, -1, -1, -1, -1, -1, -1);

        uint32_t numClippedPrims = 0;

        // tranpose clipper output so that each lane's vertices are in SIMD order
        // set aside space for 2 vertices, as the PA will try to read up to 16 verts
        // for triangle fan

#if defined(_DEBUG)
        // TODO: need to increase stack size, allocating SIMD16-widened transposedPrims causes stack overflow in debug builds
        simd16vertex *transposedPrims = reinterpret_cast<simd16vertex *>(malloc(sizeof(simd16vertex) * 2));

#else
        simd16vertex transposedPrims[2];

#endif
        for (uint32_t inputPrim = 0; inputPrim < pa.NumPrims(); ++inputPrim)
        {
            uint32_t numEmittedVerts = pVertexCount[inputPrim];
            if (numEmittedVerts < NumVertsPerPrim)
            {
                continue;
            }
            SWR_ASSERT(numEmittedVerts <= 7, "Unexpected vertex count from clipper.");

            uint32_t numEmittedPrims = GetNumPrims(clipTopology, numEmittedVerts);
            numClippedPrims += numEmittedPrims;

            // tranpose clipper output so that each lane's vertices are in SIMD order
            // set aside space for 2 vertices, as the PA will try to read up to 16 verts
            // for triangle fan

            // transpose pos
            uint8_t* pBase = (uint8_t*)(&vertices[0].attrib[VERTEX_POSITION_SLOT]) + sizeof(float) * inputPrim;

#if 0
            // TEMPORARY WORKAROUND for bizarre VS2015 code-gen bug
            static const float *dummy = reinterpret_cast<const float *>(pBase);
#endif

            for (uint32_t c = 0; c < 4; ++c)
            {
                simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                transposedPrims[0].attrib[VERTEX_POSITION_SLOT][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
                pBase += sizeof(simd16scalar);
            }

            // transpose attribs
            pBase = (uint8_t*)(&vertices[0].attrib[backendState.vertexAttribOffset]) + sizeof(float) * inputPrim;
            for (uint32_t attrib = 0; attrib < numAttribs; ++attrib)
            {
                uint32_t attribSlot = backendState.vertexAttribOffset + attrib;
                for (uint32_t c = 0; c < 4; ++c)
                {
                    simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                    transposedPrims[0].attrib[attribSlot][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
                    pBase += sizeof(simd16scalar);
                }
            }

            // transpose user clip distances if enabled
            if (this->state.rastState.clipDistanceMask & 0xf)
            {
                pBase = (uint8_t*)(&vertices[0].attrib[VERTEX_CLIPCULL_DIST_LO_SLOT]) + sizeof(float) * inputPrim;
                for (uint32_t c = 0; c < 4; ++c)
                {
                    simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                    transposedPrims[0].attrib[VERTEX_CLIPCULL_DIST_LO_SLOT][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
                    pBase += sizeof(simd16scalar);
                }
            }

            if (this->state.rastState.clipDistanceMask & 0xf0)
            {
                pBase = (uint8_t*)(&vertices[0].attrib[VERTEX_CLIPCULL_DIST_HI_SLOT]) + sizeof(float) * inputPrim;
                for (uint32_t c = 0; c < 4; ++c)
                {
                    simdscalar temp = _simd_mask_i32gather_ps(_simd_setzero_ps(), (const float *)pBase, vOffsets, vMask, 1);
                    transposedPrims[0].attrib[VERTEX_CLIPCULL_DIST_HI_SLOT][c] = _simd16_insert_ps(_simd16_setzero_ps(), temp, 0);
                    pBase += sizeof(simd16scalar);
                }
            }

            PA_STATE_OPT clipPa(this->pDC, numEmittedPrims, (uint8_t*)&transposedPrims[0], numEmittedVerts, SWR_VTX_NUM_SLOTS, true, clipTopology);

            while (clipPa.GetNextStreamOutput())
            {
                do
                {
                    simd16vector attrib[NumVertsPerPrim];
                    bool assemble = clipPa.Assemble_simd16(VERTEX_POSITION_SLOT, attrib);

                    if (assemble)
                    {
                        static const uint32_t primMaskMap[] = { 0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff };

                        clipPa.useAlternateOffset = false;
                        pfnBinFunc(this->pDC, clipPa, this->workerId, attrib, primMaskMap[numEmittedPrims], _simd16_set1_epi32(pPrimitiveId[inputPrim]));
                    }

                } while (clipPa.NextPrim());
            }
        }

#if defined(_DEBUG)
        free(transposedPrims);

#endif
        // update global pipeline stat
        UPDATE_STAT_FE(CPrimitives, numClippedPrims);
    }

#endif
    // execute the clipper stage
    void ExecuteStage(PA_STATE& pa, simdvector prim[], uint32_t primMask, simdscalari primId)
    {
        SWR_ASSERT(this->pDC != nullptr);
        SWR_CONTEXT* pContext = this->pDC->pContext;
        const API_STATE& apiState = this->pDC->pState->state;

        // set up binner based on PA state
        PFN_PROCESS_PRIMS pfnBinner;
        switch (pa.binTopology)
        {
        case TOP_POINT_LIST:
            pfnBinner = BinPoints;
            break;
        case TOP_LINE_LIST:
        case TOP_LINE_STRIP:
        case TOP_LINE_LOOP:
        case TOP_LINE_LIST_ADJ:
        case TOP_LISTSTRIP_ADJ:
            pfnBinner = BinLines;
            break;
        default:
            pfnBinner = GetBinTrianglesFunc((apiState.rastState.conservativeRast > 0));
            break;
        };

        // update clipper invocations pipeline stat
        uint32_t numInvoc = _mm_popcnt_u32(primMask);
        UPDATE_STAT_FE(CInvocations, numInvoc);
        
        // Read back viewport index if required
        simdscalari viewportIdx = _simd_set1_epi32(0);
        if (state.backendState.readViewportArrayIndex)
        {
            simdvector vpiAttrib[NumVertsPerPrim];
            pa.Assemble(VERTEX_SGV_SLOT, vpiAttrib);
            simdscalari vpai = _simd_castps_si(vpiAttrib[0][VERTEX_SGV_VAI_COMP]);

            // OOB indices => forced to zero.
            simdscalari vNumViewports = _simd_set1_epi32(KNOB_NUM_VIEWPORTS_SCISSORS);
            simdscalari vClearMask = _simd_cmplt_epi32(vpai, vNumViewports);
            viewportIdx = _simd_and_si(vClearMask, vpai);
        }

        ComputeClipCodes(prim, viewportIdx);

        // cull prims with NAN coords
        primMask &= ~ComputeNaNMask(prim);

        // user cull distance cull 
        if (this->state.rastState.cullDistanceMask)
        {
            primMask &= ~ComputeUserClipCullMask(pa, prim);
        }

        // cull prims outside view frustum
        simdscalar clipIntersection = ComputeClipCodeIntersection();
        int validMask = primMask & _simd_movemask_ps(_simd_cmpeq_ps(clipIntersection, _simd_setzero_ps()));

        // skip clipping for points
        uint32_t clipMask = 0;
        if (NumVertsPerPrim != 1)
        {
            clipMask = primMask & ComputeClipMask();
        }

        if (clipMask)
        {
            AR_BEGIN(FEGuardbandClip, pa.pDC->drawId);
            // we have to clip tris, execute the clipper, which will also
            // call the binner
            ClipSimd(vMask(primMask), vMask(clipMask), pa, primId);
            AR_END(FEGuardbandClip, 1);
        }
        else if (validMask)
        {
            // update CPrimitives pipeline state
            UPDATE_STAT_FE(CPrimitives, _mm_popcnt_u32(validMask));

            // forward valid prims directly to binner
            pfnBinner(this->pDC, pa, this->workerId, prim, validMask, primId);
        }
    }

#if USE_SIMD16_FRONTEND
    void ExecuteStage(PA_STATE& pa, simd16vector prim[], uint32_t primMask, simd16scalari primId)
    {
        SWR_ASSERT(pa.pDC != nullptr);
        SWR_CONTEXT* pContext = pa.pDC->pContext;

        // set up binner based on PA state
        PFN_PROCESS_PRIMS_SIMD16 pfnBinner;
        switch (pa.binTopology)
        {
        case TOP_POINT_LIST:
            pfnBinner = BinPoints_simd16;
            break;
        case TOP_LINE_LIST:
        case TOP_LINE_STRIP:
        case TOP_LINE_LOOP:
        case TOP_LINE_LIST_ADJ:
        case TOP_LISTSTRIP_ADJ:
            pfnBinner = BinLines_simd16;
            break;
        default:
            pfnBinner = GetBinTrianglesFunc_simd16((pa.pDC->pState->state.rastState.conservativeRast > 0));
            break;
        };

        // update clipper invocations pipeline stat
        uint32_t numInvoc = _mm_popcnt_u32(primMask);
        UPDATE_STAT_FE(CInvocations, numInvoc);

        // Read back viewport index if required
        simd16scalari viewportIdx = _simd16_set1_epi32(0);
        if (state.backendState.readViewportArrayIndex)
        {
            simd16vector vpiAttrib[NumVertsPerPrim];
            pa.Assemble_simd16(VERTEX_SGV_SLOT, vpiAttrib);

            // OOB indices => forced to zero.
            simd16scalari vpai = _simd16_castps_si(vpiAttrib[0][VERTEX_SGV_VAI_COMP]);
            simd16scalari vNumViewports = _simd16_set1_epi32(KNOB_NUM_VIEWPORTS_SCISSORS);
            simd16scalari vClearMask = _simd16_cmplt_epi32(vpai, vNumViewports);
            viewportIdx = _simd16_and_si(vClearMask, vpai);
        }
        ComputeClipCodes(prim, viewportIdx);

        // cull prims with NAN coords
        primMask &= ~ComputeNaNMask(prim);

        // user cull distance cull 
        if (this->state.rastState.cullDistanceMask)
        {
            primMask &= ~ComputeUserClipCullMask(pa, prim);
        }

        // cull prims outside view frustum
        simd16scalar clipIntersection = ComputeClipCodeIntersection_simd16();
        int validMask = primMask & _simd16_movemask_ps(_simd16_cmpeq_ps(clipIntersection, _simd16_setzero_ps()));

        // skip clipping for points
        uint32_t clipMask = 0;
        if (NumVertsPerPrim != 1)
        {
            clipMask = primMask & ComputeClipMask_simd16();
        }

        if (clipMask)
        {
            AR_BEGIN(FEGuardbandClip, pa.pDC->drawId);
            // we have to clip tris, execute the clipper, which will also
            // call the binner
            ClipSimd(vMask(primMask), vMask(clipMask), pa, primId);
            AR_END(FEGuardbandClip, 1);
        }
        else if (validMask)
        {
            // update CPrimitives pipeline state
            UPDATE_STAT_FE(CPrimitives, _mm_popcnt_u32(validMask));

            // forward valid prims directly to binner
            pfnBinner(this->pDC, pa, this->workerId, prim, validMask, primId);
        }
    }

#endif
private:
    inline simdscalar ComputeInterpFactor(simdscalar boundaryCoord0, simdscalar boundaryCoord1)
    {
        return _simd_div_ps(boundaryCoord0, _simd_sub_ps(boundaryCoord0, boundaryCoord1));
    }

#if USE_SIMD16_FRONTEND
    inline simd16scalar ComputeInterpFactor(simd16scalar boundaryCoord0, simd16scalar boundaryCoord1)
    {
        return _simd16_div_ps(boundaryCoord0, _simd16_sub_ps(boundaryCoord0, boundaryCoord1));
    }

#endif
    inline simdscalari ComputeOffsets(uint32_t attrib, simdscalari vIndices, uint32_t component)
    {
        const uint32_t simdVertexStride = sizeof(simdvertex);
        const uint32_t componentStride = sizeof(simdscalar);
        const uint32_t attribStride = sizeof(simdvector);
        const __m256i vElemOffset = _mm256_set_epi32(7 * sizeof(float), 6 * sizeof(float), 5 * sizeof(float), 4 * sizeof(float),
            3 * sizeof(float), 2 * sizeof(float), 1 * sizeof(float), 0 * sizeof(float));

        // step to the simdvertex
        simdscalari vOffsets = _simd_mullo_epi32(vIndices, _simd_set1_epi32(simdVertexStride));

        // step to the attribute and component
        vOffsets = _simd_add_epi32(vOffsets, _simd_set1_epi32(attribStride * attrib + componentStride * component));

        // step to the lane
        vOffsets = _simd_add_epi32(vOffsets, vElemOffset);

        return vOffsets;
    }

#if USE_SIMD16_FRONTEND
    inline simd16scalari ComputeOffsets(uint32_t attrib, simd16scalari vIndices, uint32_t component)
    {
        const uint32_t simdVertexStride = sizeof(simd16vertex);
        const uint32_t componentStride = sizeof(simd16scalar);
        const uint32_t attribStride = sizeof(simd16vector);
        const simd16scalari vElemOffset = _simd16_set_epi32(
            15 * sizeof(float), 14 * sizeof(float), 13 * sizeof(float), 12 * sizeof(float),
            11 * sizeof(float), 10 * sizeof(float),  9 * sizeof(float),  8 * sizeof(float),
             7 * sizeof(float),  6 * sizeof(float),  5 * sizeof(float),  4 * sizeof(float),
             3 * sizeof(float),  2 * sizeof(float),  1 * sizeof(float),  0 * sizeof(float));

        // step to the simdvertex
        simd16scalari vOffsets = _simd16_mullo_epi32(vIndices, _simd16_set1_epi32(simdVertexStride));

        // step to the attribute and component
        vOffsets = _simd16_add_epi32(vOffsets, _simd16_set1_epi32(attribStride * attrib + componentStride * component));

        // step to the lane
        vOffsets = _simd16_add_epi32(vOffsets, vElemOffset);

        return vOffsets;
    }

#endif
    // gathers a single component for a given attribute for each SIMD lane
    inline simdscalar GatherComponent(const float* pBuffer, uint32_t attrib, simdscalar vMask, simdscalari vIndices, uint32_t component)
    {
        simdscalari vOffsets = ComputeOffsets(attrib, vIndices, component);
        simdscalar vSrc = _mm256_undefined_ps();
        return _simd_mask_i32gather_ps(vSrc, pBuffer, vOffsets, vMask, 1);
    }

#if USE_SIMD16_FRONTEND
    inline simd16scalar GatherComponent(const float* pBuffer, uint32_t attrib, simd16scalar vMask, simd16scalari vIndices, uint32_t component)
    {
        simd16scalari vOffsets = ComputeOffsets(attrib, vIndices, component);
        simd16scalar vSrc = _simd16_setzero_ps();
        return _simd16_mask_i32gather_ps(vSrc, pBuffer, vOffsets, vMask, 1);
    }

#endif
    inline void ScatterComponent(const float* pBuffer, uint32_t attrib, simdscalar vMask, simdscalari vIndices, uint32_t component, simdscalar vSrc)
    {
        simdscalari vOffsets = ComputeOffsets(attrib, vIndices, component);

        uint32_t* pOffsets = (uint32_t*)&vOffsets;
        float* pSrc = (float*)&vSrc;
        uint32_t mask = _simd_movemask_ps(vMask);
        DWORD lane;
        while (_BitScanForward(&lane, mask))
        {
            mask &= ~(1 << lane);
            uint8_t* pBuf = (uint8_t*)pBuffer + pOffsets[lane];
            *(float*)pBuf = pSrc[lane];
        }
    }

#if USE_SIMD16_FRONTEND
    inline void ScatterComponent(const float* pBuffer, uint32_t attrib, simd16scalar vMask, simd16scalari vIndices, uint32_t component, simd16scalar vSrc)
    {
        simd16scalari vOffsets = ComputeOffsets(attrib, vIndices, component);

        uint32_t* pOffsets = (uint32_t*)&vOffsets;
        float* pSrc = (float*)&vSrc;
        uint32_t mask = _simd16_movemask_ps(vMask);
        DWORD lane;
        while (_BitScanForward(&lane, mask))
        {
            mask &= ~(1 << lane);
            uint8_t* pBuf = (uint8_t*)pBuffer + pOffsets[lane];
            *(float*)pBuf = pSrc[lane];
        }
    }

#endif
    template<SWR_CLIPCODES ClippingPlane>
    inline void intersect(
        const simdscalar& vActiveMask,  // active lanes to operate on
        const simdscalari& s,           // index to first edge vertex v0 in pInPts.
        const simdscalari& p,           // index to second edge vertex v1 in pInPts.
        const simdvector& v1,           // vertex 0 position
        const simdvector& v2,           // vertex 1 position
        simdscalari& outIndex,          // output index.
        const float *pInVerts,          // array of all the input positions.
        uint32_t numInAttribs,          // number of attributes per vertex.
        float *pOutVerts)               // array of output positions. We'll write our new intersection point at i*4.
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        // compute interpolation factor
        simdscalar t;
        switch (ClippingPlane)
        {
        case FRUSTUM_LEFT:      t = ComputeInterpFactor(_simd_add_ps(v1[3], v1[0]), _simd_add_ps(v2[3], v2[0])); break;
        case FRUSTUM_RIGHT:     t = ComputeInterpFactor(_simd_sub_ps(v1[3], v1[0]), _simd_sub_ps(v2[3], v2[0])); break;
        case FRUSTUM_TOP:       t = ComputeInterpFactor(_simd_add_ps(v1[3], v1[1]), _simd_add_ps(v2[3], v2[1])); break;
        case FRUSTUM_BOTTOM:    t = ComputeInterpFactor(_simd_sub_ps(v1[3], v1[1]), _simd_sub_ps(v2[3], v2[1])); break;
        case FRUSTUM_NEAR:      
            // DX Znear plane is 0, GL is -w
            if (this->state.rastState.clipHalfZ)
            {
                t = ComputeInterpFactor(v1[2], v2[2]);
            }
            else
            {
                t = ComputeInterpFactor(_simd_add_ps(v1[3], v1[2]), _simd_add_ps(v2[3], v2[2]));
            }
            break;
        case FRUSTUM_FAR:       t = ComputeInterpFactor(_simd_sub_ps(v1[3], v1[2]), _simd_sub_ps(v2[3], v2[2])); break;
        default: SWR_INVALID("invalid clipping plane: %d", ClippingPlane);
        };

        // interpolate position and store
        for (uint32_t c = 0; c < 4; ++c)
        {
            simdscalar vOutPos = _simd_fmadd_ps(_simd_sub_ps(v2[c], v1[c]), t, v1[c]);
            ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, vActiveMask, outIndex, c, vOutPos);
        }

        // interpolate attributes and store
        for (uint32_t a = 0; a < numInAttribs; ++a)
        {
            uint32_t attribSlot = vertexAttribOffset + a;
            for (uint32_t c = 0; c < 4; ++c)
            {
                simdscalar vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                simdscalar vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                simdscalar vOutAttrib = _simd_fmadd_ps(_simd_sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }

        // interpolate clip distance if enabled
        if (this->state.rastState.clipDistanceMask & 0xf)
        {
            uint32_t attribSlot = VERTEX_CLIPCULL_DIST_LO_SLOT;
            for (uint32_t c = 0; c < 4; ++c)
            {
                simdscalar vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                simdscalar vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                simdscalar vOutAttrib = _simd_fmadd_ps(_simd_sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }

        if (this->state.rastState.clipDistanceMask & 0xf0)
        {
            uint32_t attribSlot = VERTEX_CLIPCULL_DIST_HI_SLOT;
            for (uint32_t c = 0; c < 4; ++c)
            {
                simdscalar vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                simdscalar vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                simdscalar vOutAttrib = _simd_fmadd_ps(_simd_sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }
    }

#if USE_SIMD16_FRONTEND
    template<SWR_CLIPCODES ClippingPlane>
    inline void intersect(
        const simd16scalar& vActiveMask,// active lanes to operate on
        const simd16scalari& s,         // index to first edge vertex v0 in pInPts.
        const simd16scalari& p,         // index to second edge vertex v1 in pInPts.
        const simd16vector& v1,         // vertex 0 position
        const simd16vector& v2,         // vertex 1 position
        simd16scalari& outIndex,        // output index.
        const float *pInVerts,          // array of all the input positions.
        uint32_t numInAttribs,          // number of attributes per vertex.
        float *pOutVerts)               // array of output positions. We'll write our new intersection point at i*4.
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        // compute interpolation factor
        simd16scalar t;
        switch (ClippingPlane)
        {
        case FRUSTUM_LEFT:      t = ComputeInterpFactor(_simd16_add_ps(v1[3], v1[0]), _simd16_add_ps(v2[3], v2[0])); break;
        case FRUSTUM_RIGHT:     t = ComputeInterpFactor(_simd16_sub_ps(v1[3], v1[0]), _simd16_sub_ps(v2[3], v2[0])); break;
        case FRUSTUM_TOP:       t = ComputeInterpFactor(_simd16_add_ps(v1[3], v1[1]), _simd16_add_ps(v2[3], v2[1])); break;
        case FRUSTUM_BOTTOM:    t = ComputeInterpFactor(_simd16_sub_ps(v1[3], v1[1]), _simd16_sub_ps(v2[3], v2[1])); break;
        case FRUSTUM_NEAR:
            // DX Znear plane is 0, GL is -w
            if (this->state.rastState.clipHalfZ)
            {
                t = ComputeInterpFactor(v1[2], v2[2]);
            }
            else
            {
                t = ComputeInterpFactor(_simd16_add_ps(v1[3], v1[2]), _simd16_add_ps(v2[3], v2[2]));
            }
            break;
        case FRUSTUM_FAR:       t = ComputeInterpFactor(_simd16_sub_ps(v1[3], v1[2]), _simd16_sub_ps(v2[3], v2[2])); break;
        default: SWR_INVALID("invalid clipping plane: %d", ClippingPlane);
        };

        // interpolate position and store
        for (uint32_t c = 0; c < 4; ++c)
        {
            simd16scalar vOutPos = _simd16_fmadd_ps(_simd16_sub_ps(v2[c], v1[c]), t, v1[c]);
            ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, vActiveMask, outIndex, c, vOutPos);
        }

        // interpolate attributes and store
        for (uint32_t a = 0; a < numInAttribs; ++a)
        {
            uint32_t attribSlot = vertexAttribOffset + a;
            for (uint32_t c = 0; c < 4; ++c)
            {
                simd16scalar vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                simd16scalar vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                simd16scalar vOutAttrib = _simd16_fmadd_ps(_simd16_sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }

        // interpolate clip distance if enabled
        if (this->state.rastState.clipDistanceMask & 0xf)
        {
            uint32_t attribSlot = VERTEX_CLIPCULL_DIST_LO_SLOT;
            for (uint32_t c = 0; c < 4; ++c)
            {
                simd16scalar vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                simd16scalar vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                simd16scalar vOutAttrib = _simd16_fmadd_ps(_simd16_sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }

        if (this->state.rastState.clipDistanceMask & 0xf0)
        {
            uint32_t attribSlot = VERTEX_CLIPCULL_DIST_HI_SLOT;
            for (uint32_t c = 0; c < 4; ++c)
            {
                simd16scalar vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                simd16scalar vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                simd16scalar vOutAttrib = _simd16_fmadd_ps(_simd16_sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }
    }

#endif
    template<SWR_CLIPCODES ClippingPlane>
    inline simdscalar inside(const simdvector& v)
    {
        switch (ClippingPlane)
        {
        case FRUSTUM_LEFT:      return _simd_cmpge_ps(v[0], _simd_mul_ps(v[3], _simd_set1_ps(-1.0f)));
        case FRUSTUM_RIGHT:     return _simd_cmple_ps(v[0], v[3]);
        case FRUSTUM_TOP:       return _simd_cmpge_ps(v[1], _simd_mul_ps(v[3], _simd_set1_ps(-1.0f)));
        case FRUSTUM_BOTTOM:    return _simd_cmple_ps(v[1], v[3]);
        case FRUSTUM_NEAR:      return _simd_cmpge_ps(v[2], this->state.rastState.clipHalfZ ? _simd_setzero_ps() : _simd_mul_ps(v[3], _simd_set1_ps(-1.0f)));
        case FRUSTUM_FAR:       return _simd_cmple_ps(v[2], v[3]);
        default:
            SWR_INVALID("invalid clipping plane: %d", ClippingPlane);
            return _simd_setzero_ps();
        }
    }

#if USE_SIMD16_FRONTEND
    template<SWR_CLIPCODES ClippingPlane>
    inline simd16scalar inside(const simd16vector& v)
    {
        switch (ClippingPlane)
        {
        case FRUSTUM_LEFT:      return _simd16_cmpge_ps(v[0], _simd16_mul_ps(v[3], _simd16_set1_ps(-1.0f)));
        case FRUSTUM_RIGHT:     return _simd16_cmple_ps(v[0], v[3]);
        case FRUSTUM_TOP:       return _simd16_cmpge_ps(v[1], _simd16_mul_ps(v[3], _simd16_set1_ps(-1.0f)));
        case FRUSTUM_BOTTOM:    return _simd16_cmple_ps(v[1], v[3]);
        case FRUSTUM_NEAR:      return _simd16_cmpge_ps(v[2], this->state.rastState.clipHalfZ ? _simd16_setzero_ps() : _simd16_mul_ps(v[3], _simd16_set1_ps(-1.0f)));
        case FRUSTUM_FAR:       return _simd16_cmple_ps(v[2], v[3]);
        default:
            SWR_INVALID("invalid clipping plane: %d", ClippingPlane);
            return _simd16_setzero_ps();
        }
    }

#endif
    template<SWR_CLIPCODES ClippingPlane>
    simdscalari ClipTriToPlane(const float* pInVerts, const simdscalari& vNumInPts, uint32_t numInAttribs, float* pOutVerts)
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        simdscalari vCurIndex = _simd_setzero_si();
        simdscalari vOutIndex = _simd_setzero_si();
        simdscalar vActiveMask = _simd_castsi_ps(_simd_cmplt_epi32(vCurIndex, vNumInPts));

        while (!_simd_testz_ps(vActiveMask, vActiveMask)) // loop until activeMask is empty
        {
            simdscalari s = vCurIndex;
            simdscalari p = _simd_add_epi32(s, _simd_set1_epi32(1));
            simdscalari underFlowMask = _simd_cmpgt_epi32(vNumInPts, p);
            p = _simd_castps_si(_simd_blendv_ps(_simd_setzero_ps(), _simd_castsi_ps(p), _simd_castsi_ps(underFlowMask)));

            // gather position
            simdvector vInPos0, vInPos1;
            for (uint32_t c = 0; c < 4; ++c)
            {
                vInPos0[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, s, c);
                vInPos1[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, p, c);
            }

            // compute inside mask
            simdscalar s_in = inside<ClippingPlane>(vInPos0);
            simdscalar p_in = inside<ClippingPlane>(vInPos1);

            // compute intersection mask (s_in != p_in)
            simdscalar intersectMask = _simd_xor_ps(s_in, p_in);
            intersectMask = _simd_and_ps(intersectMask, vActiveMask);

            // store s if inside
            s_in = _simd_and_ps(s_in, vActiveMask);
            if (!_simd_testz_ps(s_in, s_in))
            {
                // store position
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, s_in, vOutIndex, c, vInPos0[c]);
                }

                // store attribs
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simdscalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // store clip distance if enabled
                if (this->state.rastState.clipDistanceMask & 0xf)
                {
                    uint32_t attribSlot = VERTEX_CLIPCULL_DIST_LO_SLOT;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simdscalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                if (this->state.rastState.clipDistanceMask & 0xf0)
                {
                    uint32_t attribSlot = VERTEX_CLIPCULL_DIST_HI_SLOT;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simdscalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = _simd_blendv_epi32(vOutIndex, _simd_add_epi32(vOutIndex, _simd_set1_epi32(1)), s_in);
            }

            // compute and store intersection
            if (!_simd_testz_ps(intersectMask, intersectMask))
            {
                intersect<ClippingPlane>(intersectMask, s, p, vInPos0, vInPos1, vOutIndex, pInVerts, numInAttribs, pOutVerts);

                // increment outIndex for active lanes
                vOutIndex = _simd_blendv_epi32(vOutIndex, _simd_add_epi32(vOutIndex, _simd_set1_epi32(1)), intersectMask);
            }

            // increment loop index and update active mask
            vCurIndex = _simd_add_epi32(vCurIndex, _simd_set1_epi32(1));
            vActiveMask = _simd_castsi_ps(_simd_cmplt_epi32(vCurIndex, vNumInPts));
        }

        return vOutIndex;
    }

#if USE_SIMD16_FRONTEND
    template<SWR_CLIPCODES ClippingPlane>
    simd16scalari ClipTriToPlane(const float* pInVerts, const simd16scalari& vNumInPts, uint32_t numInAttribs, float* pOutVerts)
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        simd16scalari vCurIndex = _simd16_setzero_si();
        simd16scalari vOutIndex = _simd16_setzero_si();
        simd16scalar vActiveMask = _simd16_castsi_ps(_simd16_cmplt_epi32(vCurIndex, vNumInPts));

        while (!_simd16_testz_ps(vActiveMask, vActiveMask)) // loop until activeMask is empty
        {
            simd16scalari s = vCurIndex;
            simd16scalari p = _simd16_add_epi32(s, _simd16_set1_epi32(1));
            simd16scalari underFlowMask = _simd16_cmpgt_epi32(vNumInPts, p);
            p = _simd16_castps_si(_simd16_blendv_ps(_simd16_setzero_ps(), _simd16_castsi_ps(p), _simd16_castsi_ps(underFlowMask)));

            // gather position
            simd16vector vInPos0, vInPos1;
            for (uint32_t c = 0; c < 4; ++c)
            {
                vInPos0[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, s, c);
                vInPos1[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, p, c);
            }

            // compute inside mask
            simd16scalar s_in = inside<ClippingPlane>(vInPos0);
            simd16scalar p_in = inside<ClippingPlane>(vInPos1);

            // compute intersection mask (s_in != p_in)
            simd16scalar intersectMask = _simd16_xor_ps(s_in, p_in);
            intersectMask = _simd16_and_ps(intersectMask, vActiveMask);

            // store s if inside
            s_in = _simd16_and_ps(s_in, vActiveMask);
            if (!_simd16_testz_ps(s_in, s_in))
            {
                // store position
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, s_in, vOutIndex, c, vInPos0[c]);
                }

                // store attribs
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simd16scalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // store clip distance if enabled
                if (this->state.rastState.clipDistanceMask & 0xf)
                {
                    uint32_t attribSlot = VERTEX_CLIPCULL_DIST_LO_SLOT;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simd16scalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                if (this->state.rastState.clipDistanceMask & 0xf0)
                {
                    uint32_t attribSlot = VERTEX_CLIPCULL_DIST_HI_SLOT;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simd16scalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = _simd16_blendv_epi32(vOutIndex, _simd16_add_epi32(vOutIndex, _simd16_set1_epi32(1)), s_in);
            }

            // compute and store intersection
            if (!_simd16_testz_ps(intersectMask, intersectMask))
            {
                intersect<ClippingPlane>(intersectMask, s, p, vInPos0, vInPos1, vOutIndex, pInVerts, numInAttribs, pOutVerts);

                // increment outIndex for active lanes
                vOutIndex = _simd16_blendv_epi32(vOutIndex, _simd16_add_epi32(vOutIndex, _simd16_set1_epi32(1)), intersectMask);
            }

            // increment loop index and update active mask
            vCurIndex = _simd16_add_epi32(vCurIndex, _simd16_set1_epi32(1));
            vActiveMask = _simd16_castsi_ps(_simd16_cmplt_epi32(vCurIndex, vNumInPts));
        }

        return vOutIndex;
    }

#endif
    template<SWR_CLIPCODES ClippingPlane>
    simdscalari ClipLineToPlane(const float* pInVerts, const simdscalari& vNumInPts, uint32_t numInAttribs, float* pOutVerts)
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        simdscalari vCurIndex = _simd_setzero_si();
        simdscalari vOutIndex = _simd_setzero_si();
        simdscalar vActiveMask = _simd_castsi_ps(_simd_cmplt_epi32(vCurIndex, vNumInPts));

        if (!_simd_testz_ps(vActiveMask, vActiveMask))
        {
            simdscalari s = vCurIndex;
            simdscalari p = _simd_add_epi32(s, _simd_set1_epi32(1));

            // gather position
            simdvector vInPos0, vInPos1;
            for (uint32_t c = 0; c < 4; ++c)
            {
                vInPos0[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, s, c);
                vInPos1[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, p, c);
            }

            // compute inside mask
            simdscalar s_in = inside<ClippingPlane>(vInPos0);
            simdscalar p_in = inside<ClippingPlane>(vInPos1);

            // compute intersection mask (s_in != p_in)
            simdscalar intersectMask = _simd_xor_ps(s_in, p_in);
            intersectMask = _simd_and_ps(intersectMask, vActiveMask);

            // store s if inside
            s_in = _simd_and_ps(s_in, vActiveMask);
            if (!_simd_testz_ps(s_in, s_in))
            {
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, s_in, vOutIndex, c, vInPos0[c]);
                }

                // interpolate attributes and store
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simdscalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = _simd_blendv_epi32(vOutIndex, _simd_add_epi32(vOutIndex, _simd_set1_epi32(1)), s_in);
            }

            // compute and store intersection
            if (!_simd_testz_ps(intersectMask, intersectMask))
            {
                intersect<ClippingPlane>(intersectMask, s, p, vInPos0, vInPos1, vOutIndex, pInVerts, numInAttribs, pOutVerts);

                // increment outIndex for active lanes
                vOutIndex = _simd_blendv_epi32(vOutIndex, _simd_add_epi32(vOutIndex, _simd_set1_epi32(1)), intersectMask);
            }

            // store p if inside
            p_in = _simd_and_ps(p_in, vActiveMask);
            if (!_simd_testz_ps(p_in, p_in))
            {
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, p_in, vOutIndex, c, vInPos1[c]);
                }

                // interpolate attributes and store
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simdscalar vAttrib = GatherComponent(pInVerts, attribSlot, p_in, p, c);
                        ScatterComponent(pOutVerts, attribSlot, p_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = _simd_blendv_epi32(vOutIndex, _simd_add_epi32(vOutIndex, _simd_set1_epi32(1)), p_in);
            }
        }

        return vOutIndex;
    }

#if USE_SIMD16_FRONTEND
    template<SWR_CLIPCODES ClippingPlane>
    simd16scalari ClipLineToPlane(const float* pInVerts, const simd16scalari& vNumInPts, uint32_t numInAttribs, float* pOutVerts)
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        simd16scalari vCurIndex = _simd16_setzero_si();
        simd16scalari vOutIndex = _simd16_setzero_si();
        simd16scalar vActiveMask = _simd16_castsi_ps(_simd16_cmplt_epi32(vCurIndex, vNumInPts));

        if (!_simd16_testz_ps(vActiveMask, vActiveMask))
        {
            simd16scalari s = vCurIndex;
            simd16scalari p = _simd16_add_epi32(s, _simd16_set1_epi32(1));

            // gather position
            simd16vector vInPos0, vInPos1;
            for (uint32_t c = 0; c < 4; ++c)
            {
                vInPos0[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, s, c);
                vInPos1[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, p, c);
            }

            // compute inside mask
            simd16scalar s_in = inside<ClippingPlane>(vInPos0);
            simd16scalar p_in = inside<ClippingPlane>(vInPos1);

            // compute intersection mask (s_in != p_in)
            simd16scalar intersectMask = _simd16_xor_ps(s_in, p_in);
            intersectMask = _simd16_and_ps(intersectMask, vActiveMask);

            // store s if inside
            s_in = _simd16_and_ps(s_in, vActiveMask);
            if (!_simd16_testz_ps(s_in, s_in))
            {
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, s_in, vOutIndex, c, vInPos0[c]);
                }

                // interpolate attributes and store
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simd16scalar vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = _simd16_blendv_epi32(vOutIndex, _simd16_add_epi32(vOutIndex, _simd16_set1_epi32(1)), s_in);
            }

            // compute and store intersection
            if (!_simd16_testz_ps(intersectMask, intersectMask))
            {
                intersect<ClippingPlane>(intersectMask, s, p, vInPos0, vInPos1, vOutIndex, pInVerts, numInAttribs, pOutVerts);

                // increment outIndex for active lanes
                vOutIndex = _simd16_blendv_epi32(vOutIndex, _simd16_add_epi32(vOutIndex, _simd16_set1_epi32(1)), intersectMask);
            }

            // store p if inside
            p_in = _simd16_and_ps(p_in, vActiveMask);
            if (!_simd16_testz_ps(p_in, p_in))
            {
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, p_in, vOutIndex, c, vInPos1[c]);
                }

                // interpolate attributes and store
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        simd16scalar vAttrib = GatherComponent(pInVerts, attribSlot, p_in, p, c);
                        ScatterComponent(pOutVerts, attribSlot, p_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = _simd16_blendv_epi32(vOutIndex, _simd16_add_epi32(vOutIndex, _simd16_set1_epi32(1)), p_in);
            }
        }

        return vOutIndex;
    }
#endif
    //////////////////////////////////////////////////////////////////////////
    /// @brief Vertical clipper. Clips SIMD primitives at a time
    /// @param pVertices - pointer to vertices in SOA form. Clipper will read input and write results to this buffer
    /// @param vPrimMask - mask of valid input primitives, including non-clipped prims
    /// @param numAttribs - number of valid input attribs, including position
    simdscalari ClipPrims(float* pVertices, const simdscalar& vPrimMask, const simdscalar& vClipMask, int numAttribs)
    {
        // temp storage
        float* pTempVerts = (float*)&tlsTempVertices[0];

        // zero out num input verts for non-active lanes
        simdscalari vNumInPts = _simd_set1_epi32(NumVertsPerPrim);
        vNumInPts = _simd_blendv_epi32(_simd_setzero_si(), vNumInPts, vClipMask);

        // clip prims to frustum
        simdscalari vNumOutPts;
        if (NumVertsPerPrim == 3)
        {
            vNumOutPts = ClipTriToPlane<FRUSTUM_NEAR>(pVertices, vNumInPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_FAR>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipTriToPlane<FRUSTUM_LEFT>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_RIGHT>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipTriToPlane<FRUSTUM_BOTTOM>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_TOP>(pTempVerts, vNumOutPts, numAttribs, pVertices);
        }
        else
        {
            SWR_ASSERT(NumVertsPerPrim == 2);
            vNumOutPts = ClipLineToPlane<FRUSTUM_NEAR>(pVertices, vNumInPts, numAttribs, pTempVerts);
            vNumOutPts = ClipLineToPlane<FRUSTUM_FAR>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipLineToPlane<FRUSTUM_LEFT>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipLineToPlane<FRUSTUM_RIGHT>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipLineToPlane<FRUSTUM_BOTTOM>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipLineToPlane<FRUSTUM_TOP>(pTempVerts, vNumOutPts, numAttribs, pVertices);
        }

        // restore num verts for non-clipped, active lanes
        simdscalar vNonClippedMask = _simd_andnot_ps(vClipMask, vPrimMask);
        vNumOutPts = _simd_blendv_epi32(vNumOutPts, _simd_set1_epi32(NumVertsPerPrim), vNonClippedMask);

        return vNumOutPts;
    }

#if USE_SIMD16_FRONTEND
    simd16scalari ClipPrims(float* pVertices, const simd16scalar& vPrimMask, const simd16scalar& vClipMask, int numAttribs)
    {
        // temp storage
        float* pTempVerts = (float*)&tlsTempVertices_simd16[0];

        // zero out num input verts for non-active lanes
        simd16scalari vNumInPts = _simd16_set1_epi32(NumVertsPerPrim);
        vNumInPts = _simd16_blendv_epi32(_simd16_setzero_si(), vNumInPts, vClipMask);

        // clip prims to frustum
        simd16scalari vNumOutPts;
        if (NumVertsPerPrim == 3)
        {
            vNumOutPts = ClipTriToPlane<FRUSTUM_NEAR>(pVertices, vNumInPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_FAR>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipTriToPlane<FRUSTUM_LEFT>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_RIGHT>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipTriToPlane<FRUSTUM_BOTTOM>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_TOP>(pTempVerts, vNumOutPts, numAttribs, pVertices);
        }
        else
        {
            SWR_ASSERT(NumVertsPerPrim == 2);
            vNumOutPts = ClipLineToPlane<FRUSTUM_NEAR>(pVertices, vNumInPts, numAttribs, pTempVerts);
            vNumOutPts = ClipLineToPlane<FRUSTUM_FAR>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipLineToPlane<FRUSTUM_LEFT>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipLineToPlane<FRUSTUM_RIGHT>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts = ClipLineToPlane<FRUSTUM_BOTTOM>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipLineToPlane<FRUSTUM_TOP>(pTempVerts, vNumOutPts, numAttribs, pVertices);
        }

        // restore num verts for non-clipped, active lanes
        simd16scalar vNonClippedMask = _simd16_andnot_ps(vClipMask, vPrimMask);
        vNumOutPts = _simd16_blendv_epi32(vNumOutPts, _simd16_set1_epi32(NumVertsPerPrim), vNonClippedMask);

        return vNumOutPts;
    }

#endif
    const uint32_t workerId{ 0 };
    DRAW_CONTEXT* pDC{ nullptr };
    const API_STATE& state;
    simdscalar clipCodes[NumVertsPerPrim];
#if USE_SIMD16_FRONTEND
    simd16scalar clipCodes_simd16[NumVertsPerPrim];
#endif
};


// pipeline stage functions
void ClipTriangles(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simdvector prims[], uint32_t primMask, simdscalari primId);
void ClipLines(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simdvector prims[], uint32_t primMask, simdscalari primId);
void ClipPoints(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simdvector prims[], uint32_t primMask, simdscalari primId);
#if USE_SIMD16_FRONTEND
void SIMDCALL ClipTriangles_simd16(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simd16vector prims[], uint32_t primMask, simd16scalari primId);
void SIMDCALL ClipLines_simd16(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simd16vector prims[], uint32_t primMask, simd16scalari primId);
void SIMDCALL ClipPoints_simd16(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simd16vector prims[], uint32_t primMask, simd16scalari primId);
#endif

