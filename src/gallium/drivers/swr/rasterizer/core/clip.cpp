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
* @file clip.cpp
*
* @brief Implementation for clipping
*
******************************************************************************/

#include <assert.h>

#include "common/os.h"
#include "core/clip.h"

// Temp storage used by the clipper
THREAD simdvertex tlsTempVertices[7];

float ComputeInterpFactor(float boundaryCoord0, float boundaryCoord1)
{
    return (boundaryCoord0 / (boundaryCoord0 - boundaryCoord1));
}

template<SWR_CLIPCODES ClippingPlane>
inline void intersect(
    int s,                       // index to first edge vertex v0 in pInPts.
    int p,                       // index to second edge vertex v1 in pInPts.
    const float *pInPts,         // array of all the input positions.
    const float *pInAttribs,     // array of all attributes for all vertex. All the attributes for each vertex is contiguous.
    int numInAttribs,            // number of attributes per vertex.
    int i,                       // output index.
    float *pOutPts,              // array of output positions. We'll write our new intersection point at i*4.
    float *pOutAttribs)          // array of output attributes. We'll write our new attributes at i*numInAttribs.
{
    float t;

    // Find the parameter of the intersection.
    //        t = (v1.w - v1.x) / ((v2.x - v1.x) - (v2.w - v1.w)) for x = w (RIGHT) plane, etc.
    const float *v1 = &pInPts[s*4];
    const float *v2 = &pInPts[p*4];

    switch (ClippingPlane)
    {
    case FRUSTUM_LEFT:      t = ComputeInterpFactor(v1[3] + v1[0], v2[3] + v2[0]); break;
    case FRUSTUM_RIGHT:     t = ComputeInterpFactor(v1[3] - v1[0], v2[3] - v2[0]); break;
    case FRUSTUM_TOP:       t = ComputeInterpFactor(v1[3] + v1[1], v2[3] + v2[1]); break;
    case FRUSTUM_BOTTOM:    t = ComputeInterpFactor(v1[3] - v1[1], v2[3] - v2[1]); break;
    case FRUSTUM_NEAR:      t = ComputeInterpFactor(v1[2], v2[2]); break;
    case FRUSTUM_FAR:       t = ComputeInterpFactor(v1[3] - v1[2], v2[3] - v2[2]); break;
    default: SWR_ASSERT(false, "invalid clipping plane: %d", ClippingPlane);
    };


    const float *a1 = &pInAttribs[s*numInAttribs];
    const float *a2 = &pInAttribs[p*numInAttribs];

    float *pOutP    = &pOutPts[i*4];
    float *pOutA    = &pOutAttribs[i*numInAttribs];

    // Interpolate new position.
    for(int j = 0; j < 4; ++j)
    {
        pOutP[j] = v1[j] + (v2[j]-v1[j])*t;
    }

    // Interpolate Attributes
    for(int attr = 0; attr < numInAttribs; ++attr)
    {
        pOutA[attr] = a1[attr] + (a2[attr]-a1[attr])*t;
    }
}


// Checks whether vertex v lies inside clipping plane
// in homogenous coords check -w < {x,y,z} < w;
//
template<SWR_CLIPCODES ClippingPlane>
inline int inside(const float v[4])
{
    switch (ClippingPlane)
    {
    case FRUSTUM_LEFT   : return (v[0]>=-v[3]);
    case FRUSTUM_RIGHT  : return (v[0]<= v[3]);
    case FRUSTUM_TOP    : return (v[1]>=-v[3]);
    case FRUSTUM_BOTTOM : return (v[1]<= v[3]);
    case FRUSTUM_NEAR   : return (v[2]>=0.0f);
    case FRUSTUM_FAR    : return (v[2]<= v[3]);
    default:
        SWR_ASSERT(false, "invalid clipping plane: %d", ClippingPlane);
        return 0;
    }
}


// Clips a polygon in homogenous coordinates to a particular clipping plane.
// Takes in vertices of the polygon (InPts) and the clipping plane
// Puts the vertices of the clipped polygon in OutPts
// Returns number of points in clipped polygon
//
template<SWR_CLIPCODES ClippingPlane>
int ClipTriToPlane( const float *pInPts, int numInPts,
                    const float *pInAttribs, int numInAttribs,
                    float *pOutPts, float *pOutAttribs)
{
    int i=0; // index number of OutPts, # of vertices in OutPts = i div 4;

    for (int j = 0; j < numInPts; ++j)
    {
        int s = j;
        int p = (j + 1) % numInPts;

        int s_in = inside<ClippingPlane>(&pInPts[s*4]);
        int p_in = inside<ClippingPlane>(&pInPts[p*4]);

        // test if vertex is to be added to output vertices
        if (s_in != p_in)  // edge crosses clipping plane
        {
            // find point of intersection
            intersect<ClippingPlane>(s, p, pInPts, pInAttribs, numInAttribs, i, pOutPts, pOutAttribs);
            i++;
        }
        if (p_in) // 2nd vertex is inside clipping volume, add it to output
        {
            // Copy 2nd vertex position of edge over to output.
            for(int k = 0; k < 4; ++k)
            {
                pOutPts[i*4 + k] = pInPts[p*4 + k];
            }
            // Copy 2nd vertex attributes of edge over to output.
            for(int attr = 0; attr < numInAttribs; ++attr)
            {
                pOutAttribs[i*numInAttribs+attr] = pInAttribs[p*numInAttribs+attr];
            }
            i++;
        }
        // edge does not cross clipping plane and vertex outside clipping volume
        //  => do not add vertex
    }
    return i;
}



void Clip(const float *pTriangle, const float *pAttribs, int numAttribs, float *pOutTriangles, int *numVerts, float *pOutAttribs)
{
    // temp storage to hold at least 6 sets of vertices, the max number that can be created during clipping
    OSALIGNSIMD(float) tempPts[6 * 4];
    OSALIGNSIMD(float) tempAttribs[6 * KNOB_NUM_ATTRIBUTES * 4];

    // we opt to clip to viewport frustum to produce smaller triangles for rasterization precision
    int NumOutPts = ClipTriToPlane<FRUSTUM_NEAR>(pTriangle, 3, pAttribs, numAttribs, tempPts, tempAttribs);
    NumOutPts = ClipTriToPlane<FRUSTUM_FAR>(tempPts, NumOutPts, tempAttribs, numAttribs, pOutTriangles, pOutAttribs);
    NumOutPts = ClipTriToPlane<FRUSTUM_LEFT>(pOutTriangles, NumOutPts, pOutAttribs, numAttribs, tempPts, tempAttribs);
    NumOutPts = ClipTriToPlane<FRUSTUM_RIGHT>(tempPts, NumOutPts, tempAttribs, numAttribs, pOutTriangles, pOutAttribs);
    NumOutPts = ClipTriToPlane<FRUSTUM_BOTTOM>(pOutTriangles, NumOutPts, pOutAttribs, numAttribs, tempPts, tempAttribs);
    NumOutPts = ClipTriToPlane<FRUSTUM_TOP>(tempPts, NumOutPts, tempAttribs, numAttribs, pOutTriangles, pOutAttribs);

    SWR_ASSERT(NumOutPts <= 6);

    *numVerts = NumOutPts;
    return;
}

void ClipTriangles(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simdvector prims[], uint32_t primMask, simdscalari primId, simdscalari viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;
    AR_BEGIN(FEClipTriangles, pDC->drawId);
    Clipper<3> clipper(workerId, pDC);
    clipper.ExecuteStage(pa, prims, primMask, primId, viewportIdx);
    AR_END(FEClipTriangles, 1);
}

void ClipLines(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simdvector prims[], uint32_t primMask, simdscalari primId, simdscalari viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;
    AR_BEGIN(FEClipLines, pDC->drawId);
    Clipper<2> clipper(workerId, pDC);
    clipper.ExecuteStage(pa, prims, primMask, primId, viewportIdx);
    AR_END(FEClipLines, 1);
}
void ClipPoints(DRAW_CONTEXT *pDC, PA_STATE& pa, uint32_t workerId, simdvector prims[], uint32_t primMask, simdscalari primId, simdscalari viewportIdx)
{
    SWR_CONTEXT *pContext = pDC->pContext;
    AR_BEGIN(FEClipPoints, pDC->drawId);
    Clipper<1> clipper(workerId, pDC);
    clipper.ExecuteStage(pa, prims, primMask, primId, viewportIdx);
    AR_END(FEClipPoints, 1);
}

