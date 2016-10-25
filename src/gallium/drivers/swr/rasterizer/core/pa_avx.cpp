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
* @file pa_avx.cpp
*
* @brief AVX implementation for primitive assembly.
*        N primitives are assembled at a time, where N is the SIMD width.
*        A state machine, that is specific for a given topology, drives the
*        assembly of vertices into triangles.
*
******************************************************************************/
#include "context.h"
#include "pa.h"
#include "frontend.h"

#if (KNOB_SIMD_WIDTH == 8)

bool PaTriList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriList2(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaTriListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[]);

bool PaTriStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaTriStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[]);

bool PaTriFan0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriFan1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaTriFanSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[]);

bool PaQuadList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaQuadList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaQuadListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[]);

bool PaLineLoop0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaLineLoop1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);

bool PaLineList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaLineList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaLineListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t index, __m128 verts[]);

bool PaLineStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaLineStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaLineStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 lineverts[]);

bool PaPoints0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaPointsSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[]);

bool PaRectList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaRectList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaRectList2(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
void PaRectListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[]);

template <uint32_t TotalControlPoints>
void PaPatchListSingle(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[])
{
    // We have an input of KNOB_SIMD_WIDTH * TotalControlPoints and we output
    // KNOB_SIMD_WIDTH * 1 patch.  This function is called once per attribute.
    // Each attribute has 4 components.

    /// @todo Optimize this

    float* pOutVec = (float*)verts;

    for (uint32_t cp = 0; cp < TotalControlPoints; ++cp)
    {
        uint32_t input_cp = primIndex * TotalControlPoints + cp;
        uint32_t input_vec = input_cp / KNOB_SIMD_WIDTH;
        uint32_t input_lane = input_cp % KNOB_SIMD_WIDTH;

        // Loop over all components of the attribute
        for (uint32_t i = 0; i < 4; ++i)
        {
            const float* pInputVec = (const float*)(&PaGetSimdVector(pa, input_vec, slot)[i]);
            pOutVec[cp * 4 + i] = pInputVec[input_lane];
        }
    }
}

template<uint32_t TotalControlPoints, uint32_t CurrentControlPoints = 1>
static bool PaPatchList(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(
        pa,
        PaPatchList<TotalControlPoints, CurrentControlPoints + 1>,
        PaPatchListSingle<TotalControlPoints>);

    return false;
}

template<uint32_t TotalControlPoints>
static bool PaPatchListTerm(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    // We have an input of KNOB_SIMD_WIDTH * TotalControlPoints and we output
    // KNOB_SIMD_WIDTH * 1 patch.  This function is called once per attribute.
    // Each attribute has 4 components.

    /// @todo Optimize this

    // Loop over all components of the attribute
    for (uint32_t i = 0; i < 4; ++i)
    {
        for (uint32_t cp = 0; cp < TotalControlPoints; ++cp)
        {
            float vec[KNOB_SIMD_WIDTH];
            for (uint32_t lane = 0; lane < KNOB_SIMD_WIDTH; ++lane)
            {
                uint32_t input_cp = lane * TotalControlPoints + cp;
                uint32_t input_vec = input_cp / KNOB_SIMD_WIDTH;
                uint32_t input_lane = input_cp % KNOB_SIMD_WIDTH;

                const float* pInputVec = (const float*)(&PaGetSimdVector(pa, input_vec, slot)[i]);
                vec[lane] = pInputVec[input_lane];
            }
            verts[cp][i] = _simd_loadu_ps(vec);
        }
    }

    SetNextPaState(
        pa,
        PaPatchList<TotalControlPoints>,
        PaPatchListSingle<TotalControlPoints>,
        0,
        KNOB_SIMD_WIDTH,
        true);

    return true;
}

#define PA_PATCH_LIST_TERMINATOR(N) \
    template<> bool PaPatchList<N, N>(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])\
                           { return PaPatchListTerm<N>(pa, slot, verts); }
PA_PATCH_LIST_TERMINATOR(1)
PA_PATCH_LIST_TERMINATOR(2)
PA_PATCH_LIST_TERMINATOR(3)
PA_PATCH_LIST_TERMINATOR(4)
PA_PATCH_LIST_TERMINATOR(5)
PA_PATCH_LIST_TERMINATOR(6)
PA_PATCH_LIST_TERMINATOR(7)
PA_PATCH_LIST_TERMINATOR(8)
PA_PATCH_LIST_TERMINATOR(9)
PA_PATCH_LIST_TERMINATOR(10)
PA_PATCH_LIST_TERMINATOR(11)
PA_PATCH_LIST_TERMINATOR(12)
PA_PATCH_LIST_TERMINATOR(13)
PA_PATCH_LIST_TERMINATOR(14)
PA_PATCH_LIST_TERMINATOR(15)
PA_PATCH_LIST_TERMINATOR(16)
PA_PATCH_LIST_TERMINATOR(17)
PA_PATCH_LIST_TERMINATOR(18)
PA_PATCH_LIST_TERMINATOR(19)
PA_PATCH_LIST_TERMINATOR(20)
PA_PATCH_LIST_TERMINATOR(21)
PA_PATCH_LIST_TERMINATOR(22)
PA_PATCH_LIST_TERMINATOR(23)
PA_PATCH_LIST_TERMINATOR(24)
PA_PATCH_LIST_TERMINATOR(25)
PA_PATCH_LIST_TERMINATOR(26)
PA_PATCH_LIST_TERMINATOR(27)
PA_PATCH_LIST_TERMINATOR(28)
PA_PATCH_LIST_TERMINATOR(29)
PA_PATCH_LIST_TERMINATOR(30)
PA_PATCH_LIST_TERMINATOR(31)
PA_PATCH_LIST_TERMINATOR(32)
#undef PA_PATCH_LIST_TERMINATOR

bool PaTriList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaTriList1, PaTriListSingle0);
    return false;    // Not enough vertices to assemble 4 or 8 triangles.
}

bool PaTriList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaTriList2, PaTriListSingle0);
    return false;    // Not enough vertices to assemble 8 triangles.
}

bool PaTriList2(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if KNOB_ARCH == KNOB_ARCH_AVX

    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);
    simdvector& c = PaGetSimdVector(pa, 2, slot);
    simdscalar    s;

    // Tri Pattern - provoking vertex is always v0
    //  v0 -> 0 3 6 9  12 15 18 21
    //  v1 -> 1 4 7 10 13 16 19 22
    //  v2 -> 2 5 8 11 14 17 20 23

    for (int i = 0; i < 4; ++i)
    {
        simdvector& v0 = verts[0];
        v0[i] = _simd_blend_ps(a[i], b[i], 0x92);
        v0[i] = _simd_blend_ps(v0[i], c[i], 0x24);
        v0[i] = _mm256_permute_ps(v0[i], 0x6C);
        s = _mm256_permute2f128_ps(v0[i], v0[i], 0x21);
        v0[i] = _simd_blend_ps(v0[i], s, 0x44);

        simdvector& v1 = verts[1];
        v1[i] = _simd_blend_ps(a[i], b[i], 0x24);
        v1[i] = _simd_blend_ps(v1[i], c[i], 0x49);
        v1[i] = _mm256_permute_ps(v1[i], 0xB1);
        s = _mm256_permute2f128_ps(v1[i], v1[i], 0x21);
        v1[i] = _simd_blend_ps(v1[i], s, 0x66);

        simdvector& v2 = verts[2];
        v2[i] = _simd_blend_ps(a[i], b[i], 0x49);
        v2[i] = _simd_blend_ps(v2[i], c[i], 0x92);
        v2[i] = _mm256_permute_ps(v2[i], 0xC6);
        s = _mm256_permute2f128_ps(v2[i], v2[i], 0x21);
        v2[i] = _simd_blend_ps(v2[i], s, 0x22);
    }

#elif KNOB_ARCH >= KNOB_ARCH_AVX2

    simdvector &a = PaGetSimdVector(pa, 0, slot);
    simdvector &b = PaGetSimdVector(pa, 1, slot);
    simdvector &c = PaGetSimdVector(pa, 2, slot);

    //  v0 -> a0 a3 a6 b1 b4 b7 c2 c5
    //  v1 -> a1 a4 a7 b2 b5 c0 c3 c6
    //  v2 -> a2 a5 b0 b3 b6 c1 c4 c7

    const simdscalari perm0 = _simd_set_epi32(5, 2, 7, 4, 1, 6, 3, 0);
    const simdscalari perm1 = _simd_set_epi32(6, 3, 0, 5, 2, 7, 4, 1);
    const simdscalari perm2 = _simd_set_epi32(7, 4, 1, 6, 3, 0, 5, 2);

    simdvector &v0 = verts[0];
    simdvector &v1 = verts[1];
    simdvector &v2 = verts[2];

    for (int i = 0; i < 4; ++i)
    {
        v0[i] = _simd_blend_ps(_simd_blend_ps(a[i], b[i], 0x92), c[i], 0x24);
        v0[i] = _mm256_permutevar8x32_ps(v0[i], perm0);

        v1[i] = _simd_blend_ps(_simd_blend_ps(a[i], b[i], 0x24), c[i], 0x49);
        v1[i] = _mm256_permutevar8x32_ps(v1[i], perm1);

        v2[i] = _simd_blend_ps(_simd_blend_ps(a[i], b[i], 0x49), c[i], 0x92);
        v2[i] = _mm256_permutevar8x32_ps(v2[i], perm2);
    }

#endif

    SetNextPaState(pa, PaTriList0, PaTriListSingle0, 0, KNOB_SIMD_WIDTH, true);
    return true;
}

void PaTriListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[])
{
    // We have 12 simdscalars contained within 3 simdvectors which
    // hold at least 8 triangles worth of data. We want to assemble a single
    // triangle with data in horizontal form.
    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);
    simdvector& c = PaGetSimdVector(pa, 2, slot);

    // Convert from vertical to horizontal.
    // Tri Pattern - provoking vertex is always v0
    //  v0 -> 0 3 6 9  12 15 18 21
    //  v1 -> 1 4 7 10 13 16 19 22
    //  v2 -> 2 5 8 11 14 17 20 23
    switch(primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane4(a);
        verts[2] = swizzleLane5(a);
        break;
    case 2:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane0(b);
        break;
    case 3:
        verts[0] = swizzleLane1(b);
        verts[1] = swizzleLane2(b);
        verts[2] = swizzleLane3(b);
        break;
    case 4:
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        verts[2] = swizzleLane6(b);
        break;
    case 5:
        verts[0] = swizzleLane7(b);
        verts[1] = swizzleLane0(c);
        verts[2] = swizzleLane1(c);
        break;
    case 6:
        verts[0] = swizzleLane2(c);
        verts[1] = swizzleLane3(c);
        verts[2] = swizzleLane4(c);
        break;
    case 7:
        verts[0] = swizzleLane5(c);
        verts[1] = swizzleLane6(c);
        verts[2] = swizzleLane7(c);
        break;
    };
}

bool PaTriStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaTriStrip1, PaTriStripSingle0);
    return false;    // Not enough vertices to assemble 8 triangles.
}

bool PaTriStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);
    simdscalar  s;

    for(int i = 0; i < 4; ++i)
    {
        simdscalar a0 = a[i];
        simdscalar b0 = b[i];

        // Tri Pattern - provoking vertex is always v0
        //  v0 -> 01234567
        //  v1 -> 13355779
        //  v2 -> 22446688
        simdvector& v0 = verts[0];
        v0[i] = a0;

        //  s -> 4567891011 
        s = _mm256_permute2f128_ps(a0, b0, 0x21);
        //  s -> 23456789
        s = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(1, 0, 3, 2));

        simdvector& v1 = verts[1];
        //  v1 -> 13355779
        v1[i] = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(3, 1, 3, 1));

        simdvector& v2 = verts[2];
        //  v2 -> 22446688
        v2[i] = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(2, 2, 2, 2));
    }

    SetNextPaState(pa, PaTriStrip1, PaTriStripSingle0, 0, KNOB_SIMD_WIDTH);
    return true;
}

void PaTriStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[])
{
    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

    // Convert from vertical to horizontal.
    // Tri Pattern - provoking vertex is always v0
    //  v0 -> 01234567
    //  v1 -> 13355779
    //  v2 -> 22446688
    switch(primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane1(a);
        verts[1] = swizzleLane3(a);
        verts[2] = swizzleLane2(a);
        break;
    case 2:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        verts[2] = swizzleLane4(a);
        break;
    case 3:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane4(a);
        break;
    case 4:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane6(a);
        break;
    case 5:
        verts[0] = swizzleLane5(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane6(a);
        break;
    case 6:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane0(b);
        break;
    case 7:
        verts[0] = swizzleLane7(a);
        verts[1] = swizzleLane1(b);
        verts[2] = swizzleLane0(b);
        break;
    };
}

bool PaTriFan0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    simdvector& a = PaGetSimdVector(pa, pa.cur, slot);

    // Extract vertex 0 to every lane of first vector
    for(int i = 0; i < 4; ++i)
    {
        __m256 a0 = a[i];
        simdvector& v0 = verts[0];
        v0[i] = _simd_shuffle_ps(a0, a0, _MM_SHUFFLE(0, 0, 0, 0));
        v0[i] = _mm256_permute2f128_ps(v0[i], a0, 0x00);
    }

    // store off leading vertex for attributes
    simdvertex* pVertex = (simdvertex*)pa.pStreamBase;
    pa.leadingVertex = pVertex[pa.cur];

    SetNextPaState(pa, PaTriFan1, PaTriFanSingle0);
    return false;    // Not enough vertices to assemble 8 triangles.
}

bool PaTriFan1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    simdvector& leadVert = pa.leadingVertex.attrib[slot];
    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);
    simdscalar    s;

    // need to fill vectors 1/2 with new verts, and v0 with anchor vert.
    for(int i = 0; i < 4; ++i)
    {
        simdscalar a0 = a[i];
        simdscalar b0 = b[i];

        __m256 comp = leadVert[i];
        simdvector& v0 = verts[0];
        v0[i] = _simd_shuffle_ps(comp, comp, _MM_SHUFFLE(0, 0, 0, 0));
        v0[i] = _mm256_permute2f128_ps(v0[i], comp, 0x00);

        simdvector& v2 = verts[2];
        s = _mm256_permute2f128_ps(a0, b0, 0x21);
        v2[i] = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(1, 0, 3, 2));

        simdvector& v1 = verts[1];
        v1[i] = _simd_shuffle_ps(a0, v2[i], _MM_SHUFFLE(2, 1, 2, 1));
    }

    SetNextPaState(pa, PaTriFan1, PaTriFanSingle0, 0, KNOB_SIMD_WIDTH);
    return true;
}

void PaTriFanSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[])
{
    // vert 0 from leading vertex
    simdvector& lead = pa.leadingVertex.attrib[slot];
    verts[0] = swizzleLane0(lead);

    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

    // vert 1
    if (primIndex < 7)
    {
        verts[1] = swizzleLaneN(a, primIndex + 1);
    }
    else
    {
        verts[1] = swizzleLane0(b);
    }

    // vert 2
    if (primIndex < 6)
    {
        verts[2] = swizzleLaneN(a, primIndex + 2);
    }
    else
    {
        verts[2] = swizzleLaneN(b, primIndex - 6);
    }
}

bool PaQuadList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaQuadList1, PaQuadListSingle0);
    return false;    // Not enough vertices to assemble 8 triangles.
}

bool PaQuadList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);
    simdscalar    s1, s2;

    for(int i = 0; i < 4; ++i)
    {
        simdscalar a0 = a[i];
        simdscalar b0 = b[i];

        s1 = _mm256_permute2f128_ps(a0, b0, 0x20);
        s2 = _mm256_permute2f128_ps(a0, b0, 0x31);

        simdvector& v0 = verts[0];
        v0[i] = _simd_shuffle_ps(s1, s2, _MM_SHUFFLE(0, 0, 0, 0));

        simdvector& v1 = verts[1];
        v1[i] = _simd_shuffle_ps(s1, s2, _MM_SHUFFLE(2, 1, 2, 1));

        simdvector& v2 = verts[2];
        v2[i] = _simd_shuffle_ps(s1, s2, _MM_SHUFFLE(3, 2, 3, 2));
    }

    SetNextPaState(pa, PaQuadList0, PaQuadListSingle0, 0, KNOB_SIMD_WIDTH, true);
    return true;
}

void PaQuadListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[])
{
    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);

    switch (primIndex)
    {
    case 0:
        // triangle 0 - 0 1 2
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;

    case 1:
        // triangle 1 - 0 2 3
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane2(a);
        verts[2] = swizzleLane3(a);
        break;

    case 2:
        // triangle 2 - 4 5 6
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane6(a);
        break;

    case 3:
        // triangle 3 - 4 6 7
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane6(a);
        verts[2] = swizzleLane7(a);
        break;

    case 4:
        // triangle 4 - 8 9 10 (0 1 2)
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane1(b);
        verts[2] = swizzleLane2(b);
        break;

    case 5:
        // triangle 1 - 0 2 3
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane2(b);
        verts[2] = swizzleLane3(b);
        break;

    case 6:
        // triangle 2 - 4 5 6
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        verts[2] = swizzleLane6(b);
        break;

    case 7:
        // triangle 3 - 4 6 7
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane6(b);
        verts[2] = swizzleLane7(b);
        break;
    }
}

void PaLineLoopSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t lineIndex, __m128 verts[])
{
    PaLineStripSingle0(pa, slot, lineIndex, verts);

    if (pa.numPrimsComplete + lineIndex == pa.numPrims - 1) {
        simdvector &start = PaGetSimdVector(pa, pa.first, slot);
        verts[1] = swizzleLane0(start);
    }
}

bool PaLineLoop0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaLineLoop1, PaLineLoopSingle0);
    return false;
}

bool PaLineLoop1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    PaLineStrip1(pa, slot, verts);

    if (pa.numPrimsComplete + KNOB_SIMD_WIDTH > pa.numPrims - 1) {
        // loop reconnect now
        int lane = pa.numPrims - pa.numPrimsComplete - 1;
        simdvector &start = PaGetSimdVector(pa, pa.first, slot);
        for (int i = 0; i < 4; i++) {
            float *startVtx = (float *)&(start[i]);
            float *targetVtx = (float *)&(verts[1][i]);
            targetVtx[lane] = startVtx[0];
        }
    }

    SetNextPaState(pa, PaLineLoop1, PaLineLoopSingle0, 0, KNOB_SIMD_WIDTH);
    return true;
}


bool PaLineList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaLineList1, PaLineListSingle0);
    return false;    // Not enough vertices to assemble 8 lines
}

bool PaLineList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);
    /// @todo: verify provoking vertex is correct
    // Line list 0  1  2  3  4  5  6  7
    //           8  9 10 11 12 13 14 15

    // shuffle:
    //           0 2 4 6 8 10 12 14
    //           1 3 5 7 9 11 13 15

    for (uint32_t i = 0; i < 4; ++i)
    {
        // 0 1 2 3 8 9 10 11
        __m256 vALowBLow = _mm256_permute2f128_ps(a.v[i], b.v[i], 0x20);
        // 4 5 6 7 12 13 14 15
        __m256 vAHighBHigh = _mm256_permute2f128_ps(a.v[i], b.v[i], 0x31);

        // 0 2 4 6 8 10 12 14
        verts[0].v[i] = _mm256_shuffle_ps(vALowBLow, vAHighBHigh, _MM_SHUFFLE(2, 0, 2, 0));
        // 1 3 5 7 9 11 13 15
        verts[1].v[i] = _mm256_shuffle_ps(vALowBLow, vAHighBHigh, _MM_SHUFFLE(3, 1, 3, 1));
    }

    SetNextPaState(pa, PaLineList0, PaLineListSingle0, 0, KNOB_SIMD_WIDTH, true);
    return true;
}

void PaLineListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[])
{
    simdvector &a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector &b = PaGetSimdVector(pa, pa.cur, slot);

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        break;
    case 1:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        break;
    case 2:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        break;
    case 3:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        break;
    case 4:
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane1(b);
        break;
    case 5:
        verts[0] = swizzleLane2(b);
        verts[1] = swizzleLane3(b);
        break;
    case 6:
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        break;
    case 7:
        verts[0] = swizzleLane6(b);
        verts[1] = swizzleLane7(b);
        break;
    }
}

bool PaLineStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaLineStrip1, PaLineStripSingle0);
    return false;    // Not enough vertices to assemble 8 lines
}

bool PaLineStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

    /// @todo: verify provoking vertex is correct
    // Line list 0  1  2  3  4  5  6  7
    //           8  9 10 11 12 13 14 15

    // shuffle:
    //           0  1  2  3  4  5  6  7
    //           1  2  3  4  5  6  7  8

    verts[0] = a;

    for(uint32_t i = 0; i < 4; ++i)
    {
        // 1 2 3 x 5 6 7 x
        __m256 vPermA = _mm256_permute_ps(a.v[i], 0x39); // indices hi->low 00 11 10 01 (0 3 2 1)
        // 4 5 6 7 8 9 10 11
        __m256 vAHighBLow = _mm256_permute2f128_ps(a.v[i], b.v[i], 0x21);

        // x x x 4 x x x 8
        __m256 vPermB = _mm256_permute_ps(vAHighBLow, 0); // indices hi->low  (0 0 0 0)

        verts[1].v[i] = _mm256_blend_ps(vPermA, vPermB, 0x88);
    }

    SetNextPaState(pa, PaLineStrip1, PaLineStripSingle0, 0, KNOB_SIMD_WIDTH);
    return true;
}

void PaLineStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t lineIndex, __m128 verts[])
{
    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

    switch (lineIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        break;
    case 1:
        verts[0] = swizzleLane1(a);
        verts[1] = swizzleLane2(a);
        break;
    case 2:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        break;
    case 3:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane4(a);
        break;
    case 4:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        break;
    case 5:
        verts[0] = swizzleLane5(a);
        verts[1] = swizzleLane6(a);
        break;
    case 6:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        break;
    case 7:
        verts[0] = swizzleLane7(a);
        verts[1] = swizzleLane0(b);
        break;
    }
}

bool PaPoints0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    simdvector& a = PaGetSimdVector(pa, pa.cur, slot);

    verts[0] = a;  // points only have 1 vertex.

    SetNextPaState(pa, PaPoints0, PaPointsSingle0, 0, KNOB_SIMD_WIDTH, true);
    return true;
}

void PaPointsSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, __m128 verts[])
{
    simdvector &a = PaGetSimdVector(pa, pa.cur, slot);
    switch(primIndex)
    {
    case 0: 
        verts[0] = swizzleLane0(a);
        break;
    case 1: 
        verts[0] = swizzleLane1(a);
        break;
    case 2: 
        verts[0] = swizzleLane2(a);
        break;
    case 3: 
        verts[0] = swizzleLane3(a);
        break;
    case 4: 
        verts[0] = swizzleLane4(a);
        break;
    case 5: 
        verts[0] = swizzleLane5(a);
        break;
    case 6: 
        verts[0] = swizzleLane6(a);
        break;
    case 7: 
        verts[0] = swizzleLane7(a);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief State 1 for RECT_LIST topology.
///        There is not enough to assemble 8 triangles.
bool PaRectList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaRectList1, PaRectListSingle0);
    return false;
}
 
//////////////////////////////////////////////////////////////////////////
/// @brief State 1 for RECT_LIST topology.
///   Rect lists has the following format.
///             w          x          y           z
///      v2 o---o   v5 o---o   v8 o---o   v11 o---o
///         | \ |      | \ |      | \ |       | \ |
///      v1 o---o   v4 o---o   v7 o---o   v10 o---o
///            v0         v3         v6          v9
/// 
///   Only 3 vertices of the rectangle are supplied. The 4th vertex is implied.
/// 
///   tri0 = { v0, v1, v2 }  tri1 = { v0, v2, w } <-- w = v0 - v1 + v2
///   tri2 = { v3, v4, v5 }  tri3 = { v3, v5, x } <-- x = v3 - v4 + v5
///   etc.
/// 
///   PA outputs 3 simdvectors for each of the triangle vertices v0, v1, v2
///   where v0 contains all the first vertices for 8 triangles.
/// 
///     Result:
///      verts[0] = { v0, v0, v3, v3, v6, v6, v9, v9 }
///      verts[1] = { v1, v2, v4, v5, v7, v8, v10, v11 }
///      verts[2] = { v2,  w, v5,  x, v8,  y, v11, z }
///
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output which is either a position (slot 0) or attribute.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1, etc.
bool PaRectList1(
    PA_STATE_OPT& pa,
    uint32_t slot,
    simdvector verts[])
{
    // SIMD vectors a and b are the last two vertical outputs from the vertex shader.
    simdvector& a = PaGetSimdVector(pa, 0, slot);   // a[] = { v0, v1,  v2,  v3,  v4,  v5,  v6,  v7 }
    simdvector& b = PaGetSimdVector(pa, 1, slot);   // b[] = { v8, v9, v10, v11, v12, v13, v14, v15 }

    __m256 tmp0, tmp1, tmp2;

    // Loop over each component in the simdvector.
    for(int i = 0; i < 4; ++i)
    {
        simdvector& v0 = verts[0];                          // verts[0] needs to be { v0, v0, v3, v3, v6, v6, v9, v9 }
        tmp0 = _mm256_permute2f128_ps(b[i], b[i], 0x01);  // tmp0 = { v12, v13, v14, v15, v8, v9, v10, v11 }
        v0[i] = _mm256_blend_ps(a[i], tmp0, 0x20);        //   v0 = {  v0,   *,   *,  v3,  *, v9,  v6,  * } where * is don't care.
        tmp1  = _mm256_permute_ps(v0[i], 0xF0);           // tmp1 = {  v0,  v0,  v3,  v3,  *,  *,  *,  * }
        v0[i] = _mm256_permute_ps(v0[i], 0x5A);           //   v0 = {   *,   *,   *,   *,  v6, v6, v9, v9 }
        v0[i] = _mm256_blend_ps(tmp1, v0[i], 0xF0);       //   v0 = {  v0,  v0,  v3,  v3,  v6, v6, v9, v9 }

        /// NOTE This is a bit expensive due to conflicts between vertices in 'a' and 'b'.
        ///      AVX2 should make this much cheaper.
        simdvector& v1 = verts[1];                          // verts[1] needs to be { v1, v2, v4, v5, v7, v8, v10, v11 }
        v1[i] = _mm256_permute_ps(a[i], 0x09);            //   v1 = { v1, v2,  *,  *,  *, *,  *, * }
        tmp1  = _mm256_permute_ps(a[i], 0x43);            // tmp1 = {  *,  *,  *,  *, v7, *, v4, v5 }
        tmp2  = _mm256_blend_ps(v1[i], tmp1, 0xF0);       // tmp2 = { v1, v2,  *,  *, v7, *, v4, v5 }
        tmp1  = _mm256_permute2f128_ps(tmp2, tmp2, 0x1);  // tmp1 = { v7,  *, v4,  v5, *  *,  *,  * }
        v1[i] = _mm256_permute_ps(tmp0, 0xE0);            //   v1 = {  *,  *,  *,  *,  *, v8, v10, v11 }
        v1[i] = _mm256_blend_ps(tmp2, v1[i], 0xE0);       //   v1 = { v1, v2,  *,  *, v7, v8, v10, v11 }
        v1[i] = _mm256_blend_ps(v1[i], tmp1, 0x0C);       //   v1 = { v1, v2, v4, v5, v7, v8, v10, v11 }

        // verts[2] = { v2,  w, v5,  x, v8,  y, v11, z }
        simdvector& v2 = verts[2];                          // verts[2] needs to be { v2,  w, v5,  x, v8,  y, v11, z }
        v2[i] = _mm256_permute_ps(tmp0, 0x30);            //   v2 = { *, *, *, *, v8, *, v11, * }
        tmp1  = _mm256_permute_ps(tmp2, 0x31);            // tmp1 = { v2, *, v5, *, *, *, *, * }
        v2[i] = _mm256_blend_ps(tmp1, v2[i], 0xF0);

        // Need to compute 4th implied vertex for the rectangle.
        tmp2  = _mm256_sub_ps(v0[i], v1[i]);
        tmp2  = _mm256_add_ps(tmp2, v2[i]);               // tmp2 = {  w,  *,  x, *, y,  *,  z,  * }
        tmp2  = _mm256_permute_ps(tmp2, 0xA0);            // tmp2 = {  *,  w,  *, x, *,   y,  *,  z }
        v2[i] = _mm256_blend_ps(v2[i], tmp2, 0xAA);       //   v2 = { v2,  w, v5, x, v8,  y, v11, z }
    }

    SetNextPaState(pa, PaRectList1, PaRectListSingle0, 0, KNOB_SIMD_WIDTH, true);
    return true;
}

//////////////////////////////////////////////////////////////////////////
/// @brief State 2 for RECT_LIST topology.
///        Not implemented unless there is a use case for more then 8 rects.
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output which is either a position (slot 0) or attribute.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1, etc.
bool PaRectList2(
    PA_STATE_OPT& pa,
    uint32_t slot,
    simdvector verts[])
{
    SWR_ASSERT(0); // Is rect list used for anything other then clears?
    SetNextPaState(pa, PaRectList0, PaRectListSingle0, 0, KNOB_SIMD_WIDTH, true);
    return true;
}

//////////////////////////////////////////////////////////////////////////
/// @brief This procedure is called by the Binner to assemble the attributes.
///        Unlike position, which is stored vertically, the attributes are
///        stored horizontally. The outputs from the VS, labeled as 'a' and
///        'b' are vertical. This function needs to transpose the lanes
///        containing the vertical attribute data into horizontal form.
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output for a given attribute.
/// @param primIndex - Binner processes each triangle individually.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1, etc.
void PaRectListSingle0(
    PA_STATE_OPT& pa,
    uint32_t slot,
    uint32_t primIndex,
    __m128 verts[])
{
    // We have 12 simdscalars contained within 3 simdvectors which
    // hold at least 8 triangles worth of data. We want to assemble a single
    // triangle with data in horizontal form.
    simdvector& a = PaGetSimdVector(pa, 0, slot);

    // Convert from vertical to horizontal.
    switch(primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane2(a);
        verts[2] = _mm_blend_ps(verts[0], verts[1], 0x2);
        break;
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        SWR_ASSERT(0);
        break;
    };
}

PA_STATE_OPT::PA_STATE_OPT(DRAW_CONTEXT *in_pDC, uint32_t in_numPrims, uint8_t* pStream, uint32_t in_streamSizeInVerts, 
    bool in_isStreaming, PRIMITIVE_TOPOLOGY topo) : PA_STATE(in_pDC, pStream, in_streamSizeInVerts), numPrims(in_numPrims), numPrimsComplete(0), numSimdPrims(0), 
    cur(0), prev(0), first(0), counter(0), reset(false), pfnPaFunc(nullptr), isStreaming(in_isStreaming)
{
    const API_STATE& state = GetApiState(pDC);

    this->binTopology = topo == TOP_UNKNOWN ? state.topology : topo;

    switch (this->binTopology)
    {
        case TOP_TRIANGLE_LIST:
            this->pfnPaFunc = PaTriList0;
            break;
        case TOP_TRIANGLE_STRIP:
            this->pfnPaFunc = PaTriStrip0;
            break;
        case TOP_TRIANGLE_FAN:
            this->pfnPaFunc = PaTriFan0;
            break;
        case TOP_QUAD_LIST:
            this->pfnPaFunc = PaQuadList0;
            this->numPrims = in_numPrims * 2;    // Convert quad primitives into triangles
            break;
        case TOP_QUAD_STRIP:
            // quad strip pattern when decomposed into triangles is the same as verts strips
            this->pfnPaFunc = PaTriStrip0;
            this->numPrims = in_numPrims * 2;    // Convert quad primitives into triangles
            break;
        case TOP_LINE_LIST:
            this->pfnPaFunc = PaLineList0;
            this->numPrims = in_numPrims;
            break;
        case TOP_LINE_STRIP:
            this->pfnPaFunc = PaLineStrip0;
            this->numPrims = in_numPrims;
            break;
        case TOP_LINE_LOOP:
            this->pfnPaFunc = PaLineLoop0;
            this->numPrims = in_numPrims;
            break;
        case TOP_POINT_LIST:
            // use point binner and rasterizer if supported
            this->pfnPaFunc = PaPoints0;
            this->numPrims = in_numPrims;
            break;
        case TOP_RECT_LIST:
            this->pfnPaFunc = PaRectList0;
            this->numPrims = in_numPrims * 2;
            break;

        case TOP_PATCHLIST_1:
            this->pfnPaFunc = PaPatchList<1>;
            break;
        case TOP_PATCHLIST_2:
            this->pfnPaFunc = PaPatchList<2>;
            break;
        case TOP_PATCHLIST_3:
            this->pfnPaFunc = PaPatchList<3>;
            break;
        case TOP_PATCHLIST_4:
            this->pfnPaFunc = PaPatchList<4>;
            break;
        case TOP_PATCHLIST_5:
            this->pfnPaFunc = PaPatchList<5>;
            break;
        case TOP_PATCHLIST_6:
            this->pfnPaFunc = PaPatchList<6>;
            break;
        case TOP_PATCHLIST_7:
            this->pfnPaFunc = PaPatchList<7>;
            break;
        case TOP_PATCHLIST_8:
            this->pfnPaFunc = PaPatchList<8>;
            break;
        case TOP_PATCHLIST_9:
            this->pfnPaFunc = PaPatchList<9>;
            break;
        case TOP_PATCHLIST_10:
            this->pfnPaFunc = PaPatchList<10>;
            break;
        case TOP_PATCHLIST_11:
            this->pfnPaFunc = PaPatchList<11>;
            break;
        case TOP_PATCHLIST_12:
            this->pfnPaFunc = PaPatchList<12>;
            break;
        case TOP_PATCHLIST_13:
            this->pfnPaFunc = PaPatchList<13>;
            break;
        case TOP_PATCHLIST_14:
            this->pfnPaFunc = PaPatchList<14>;
            break;
        case TOP_PATCHLIST_15:
            this->pfnPaFunc = PaPatchList<15>;
            break;
        case TOP_PATCHLIST_16:
            this->pfnPaFunc = PaPatchList<16>;
            break;
        case TOP_PATCHLIST_17:
            this->pfnPaFunc = PaPatchList<17>;
            break;
        case TOP_PATCHLIST_18:
            this->pfnPaFunc = PaPatchList<18>;
            break;
        case TOP_PATCHLIST_19:
            this->pfnPaFunc = PaPatchList<19>;
            break;
        case TOP_PATCHLIST_20:
            this->pfnPaFunc = PaPatchList<20>;
            break;
        case TOP_PATCHLIST_21:
            this->pfnPaFunc = PaPatchList<21>;
            break;
        case TOP_PATCHLIST_22:
            this->pfnPaFunc = PaPatchList<22>;
            break;
        case TOP_PATCHLIST_23:
            this->pfnPaFunc = PaPatchList<23>;
            break;
        case TOP_PATCHLIST_24:
            this->pfnPaFunc = PaPatchList<24>;
            break;
        case TOP_PATCHLIST_25:
            this->pfnPaFunc = PaPatchList<25>;
            break;
        case TOP_PATCHLIST_26:
            this->pfnPaFunc = PaPatchList<26>;
            break;
        case TOP_PATCHLIST_27:
            this->pfnPaFunc = PaPatchList<27>;
            break;
        case TOP_PATCHLIST_28:
            this->pfnPaFunc = PaPatchList<28>;
            break;
        case TOP_PATCHLIST_29:
            this->pfnPaFunc = PaPatchList<29>;
            break;
        case TOP_PATCHLIST_30:
            this->pfnPaFunc = PaPatchList<30>;
            break;
        case TOP_PATCHLIST_31:
            this->pfnPaFunc = PaPatchList<31>;
            break;
        case TOP_PATCHLIST_32:
            this->pfnPaFunc = PaPatchList<32>;
            break;

        default:
            SWR_ASSERT(0);
            break;
    };

    this->pfnPaFuncReset = this->pfnPaFunc;

    //    simdscalari id8 = _mm256_set_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    //    simdscalari id4 = _mm256_set_epi32(0, 0, 1, 1, 2, 2, 3, 3);
    simdscalari id8 = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
    simdscalari id4 = _mm256_set_epi32(3, 3, 2, 2, 1, 1, 0, 0);

    switch(this->binTopology)
    {
        case TOP_TRIANGLE_LIST:
        case TOP_TRIANGLE_STRIP:
        case TOP_TRIANGLE_FAN:
        case TOP_LINE_STRIP:
        case TOP_LINE_LIST:
        case TOP_LINE_LOOP:
            this->primIDIncr = 8;
            this->primID = id8;
            break;
        case TOP_QUAD_LIST:
        case TOP_QUAD_STRIP:
        case TOP_RECT_LIST:
            this->primIDIncr = 4;
            this->primID = id4;
            break;
        case TOP_POINT_LIST:
            this->primIDIncr = 8;
            this->primID = id8;
            break;
        case TOP_PATCHLIST_1:
        case TOP_PATCHLIST_2:
        case TOP_PATCHLIST_3:
        case TOP_PATCHLIST_4:
        case TOP_PATCHLIST_5:
        case TOP_PATCHLIST_6:
        case TOP_PATCHLIST_7:
        case TOP_PATCHLIST_8:
        case TOP_PATCHLIST_9:
        case TOP_PATCHLIST_10:
        case TOP_PATCHLIST_11:
        case TOP_PATCHLIST_12:
        case TOP_PATCHLIST_13:
        case TOP_PATCHLIST_14:
        case TOP_PATCHLIST_15:
        case TOP_PATCHLIST_16:
        case TOP_PATCHLIST_17:
        case TOP_PATCHLIST_18:
        case TOP_PATCHLIST_19:
        case TOP_PATCHLIST_20:
        case TOP_PATCHLIST_21:
        case TOP_PATCHLIST_22:
        case TOP_PATCHLIST_23:
        case TOP_PATCHLIST_24:
        case TOP_PATCHLIST_25:
        case TOP_PATCHLIST_26:
        case TOP_PATCHLIST_27:
        case TOP_PATCHLIST_28:
        case TOP_PATCHLIST_29:
        case TOP_PATCHLIST_30:
        case TOP_PATCHLIST_31:
        case TOP_PATCHLIST_32:
            // Always run KNOB_SIMD_WIDTH number of patches at a time.
            this->primIDIncr = 8;
            this->primID = id8;
            break;

        default:
            SWR_ASSERT(0);
            break;
    };

}
#endif
