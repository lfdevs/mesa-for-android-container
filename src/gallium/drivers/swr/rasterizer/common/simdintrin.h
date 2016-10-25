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
****************************************************************************/

#ifndef __SWR_SIMDINTRIN_H__
#define __SWR_SIMDINTRIN_H__

#include "os.h"

#include <cassert>

#include <emmintrin.h>
#include <immintrin.h>
#include <xmmintrin.h>

#if KNOB_SIMD_WIDTH == 8 
typedef __m256 simdscalar;
typedef __m256i simdscalari;
typedef uint8_t simdmask;
#elif KNOB_SIMD_WIDTH == 16
#if ENABLE_AVX512_EMULATION
struct simdscalar
{
    __m256  lo;
    __m256  hi;
};
struct simdscalard
{
    __m256d lo;
    __m256d hi;
};
struct simdscalari
{
    __m256i lo;
    __m256i hi;
};
typedef uint16_t simdmask;
#else
typedef __m512 simdscalar;
typedef __m512d simdscalard;
typedef __m512i simdscalari;
typedef __mask16 simdmask;
#endif
#else
#error Unsupported vector width
#endif

// simd vector
OSALIGNSIMD(union) simdvector
{
    simdscalar  v[4];
    struct
    {
        simdscalar x, y, z, w;
    };

    simdscalar& operator[] (const int i) { return v[i]; }
    const simdscalar& operator[] (const int i) const { return v[i]; }
};

#if KNOB_SIMD_WIDTH == 8
#define _simd128_maskstore_ps _mm_maskstore_ps
#define _simd_load_ps _mm256_load_ps
#define _simd_load1_ps _mm256_broadcast_ss
#define _simd_loadu_ps _mm256_loadu_ps
#define _simd_setzero_ps _mm256_setzero_ps
#define _simd_set1_ps   _mm256_set1_ps
#define _simd_blend_ps  _mm256_blend_ps
#define _simd_blendv_ps _mm256_blendv_ps
#define _simd_store_ps _mm256_store_ps
#define _simd_mul_ps _mm256_mul_ps
#define _simd_add_ps _mm256_add_ps
#define _simd_sub_ps _mm256_sub_ps
#define _simd_rsqrt_ps _mm256_rsqrt_ps
#define _simd_min_ps _mm256_min_ps
#define _simd_max_ps _mm256_max_ps
#define _simd_movemask_ps _mm256_movemask_ps
#define _simd_cvtps_epi32 _mm256_cvtps_epi32
#define _simd_cvttps_epi32 _mm256_cvttps_epi32
#define _simd_cvtepi32_ps _mm256_cvtepi32_ps
#define _simd_cmplt_ps(a, b) _mm256_cmp_ps(a, b, _CMP_LT_OQ)
#define _simd_cmpgt_ps(a, b) _mm256_cmp_ps(a, b, _CMP_GT_OQ)
#define _simd_cmpneq_ps(a, b) _mm256_cmp_ps(a, b, _CMP_NEQ_OQ)
#define _simd_cmpeq_ps(a, b) _mm256_cmp_ps(a, b, _CMP_EQ_OQ)
#define _simd_cmpge_ps(a, b) _mm256_cmp_ps(a, b, _CMP_GE_OQ)
#define _simd_cmple_ps(a, b) _mm256_cmp_ps(a, b, _CMP_LE_OQ)
#define _simd_cmp_ps(a, b, imm) _mm256_cmp_ps(a, b, imm)
#define _simd_and_ps _mm256_and_ps
#define _simd_or_ps _mm256_or_ps

#define _simd_rcp_ps _mm256_rcp_ps
#define _simd_div_ps _mm256_div_ps
#define _simd_castsi_ps _mm256_castsi256_ps
#define _simd_andnot_ps _mm256_andnot_ps
#define _simd_round_ps _mm256_round_ps
#define _simd_castpd_ps _mm256_castpd_ps
#define _simd_broadcast_ps(a) _mm256_broadcast_ps((const __m128*)(a))
#define _simd_stream_ps _mm256_stream_ps

#define _simd_load_sd _mm256_load_sd
#define _simd_movemask_pd _mm256_movemask_pd
#define _simd_castsi_pd _mm256_castsi256_pd

// emulated integer simd
#define SIMD_EMU_EPI(func, intrin) \
INLINE \
__m256i func(__m256i a, __m256i b)\
{\
    __m128i aHi = _mm256_extractf128_si256(a, 1);\
    __m128i bHi = _mm256_extractf128_si256(b, 1);\
    __m128i aLo = _mm256_castsi256_si128(a);\
    __m128i bLo = _mm256_castsi256_si128(b);\
\
    __m128i subLo = intrin(aLo, bLo);\
    __m128i subHi = intrin(aHi, bHi);\
\
    __m256i result = _mm256_castsi128_si256(subLo);\
            result = _mm256_insertf128_si256(result, subHi, 1);\
\
    return result;\
}

#if (KNOB_ARCH == KNOB_ARCH_AVX)
INLINE
__m256 _simdemu_permute_ps(__m256 a, __m256i b)
{
    __m128 aHi = _mm256_extractf128_ps(a, 1);
    __m128i bHi = _mm256_extractf128_si256(b, 1);
    __m128 aLo = _mm256_castps256_ps128(a);
    __m128i bLo = _mm256_castsi256_si128(b);

    __m128i indexHi = _mm_cmpgt_epi32(bLo, _mm_set1_epi32(3));
    __m128 resLow = _mm_permutevar_ps(aLo, _mm_and_si128(bLo, _mm_set1_epi32(0x3)));
    __m128 resHi = _mm_permutevar_ps(aHi, _mm_and_si128(bLo, _mm_set1_epi32(0x3)));
    __m128 blendLowRes = _mm_blendv_ps(resLow, resHi, _mm_castsi128_ps(indexHi));

    indexHi = _mm_cmpgt_epi32(bHi, _mm_set1_epi32(3));
    resLow = _mm_permutevar_ps(aLo, _mm_and_si128(bHi, _mm_set1_epi32(0x3)));
    resHi = _mm_permutevar_ps(aHi, _mm_and_si128(bHi, _mm_set1_epi32(0x3)));
    __m128 blendHiRes = _mm_blendv_ps(resLow, resHi, _mm_castsi128_ps(indexHi));

    __m256 result = _mm256_castps128_ps256(blendLowRes);
    result = _mm256_insertf128_ps(result, blendHiRes, 1);

    return result;
}

INLINE
__m256i _simdemu_permute_epi32(__m256i a, __m256i b)
{
    return _mm256_castps_si256(_simdemu_permute_ps(_mm256_castsi256_ps(a), b));
}

INLINE
__m256i _simdemu_srlv_epi32(__m256i vA, __m256i vCount)
{
    int32_t aHi, aLow, countHi, countLow;
    __m128i vAHi = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 1));
    __m128i vALow = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 0));
    __m128i vCountHi = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 1));
    __m128i vCountLow = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 0));

    aHi = _mm_extract_epi32(vAHi, 0);
    countHi = _mm_extract_epi32(vCountHi, 0);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 0);

    aLow = _mm_extract_epi32(vALow, 0);
    countLow = _mm_extract_epi32(vCountLow, 0);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 0);

    aHi = _mm_extract_epi32(vAHi, 1);
    countHi = _mm_extract_epi32(vCountHi, 1);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 1);

    aLow = _mm_extract_epi32(vALow, 1);
    countLow = _mm_extract_epi32(vCountLow, 1);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 1);

    aHi = _mm_extract_epi32(vAHi, 2);
    countHi = _mm_extract_epi32(vCountHi, 2);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 2);

    aLow = _mm_extract_epi32(vALow, 2);
    countLow = _mm_extract_epi32(vCountLow, 2);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 2);

    aHi = _mm_extract_epi32(vAHi, 3);
    countHi = _mm_extract_epi32(vCountHi, 3);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 3);

    aLow = _mm_extract_epi32(vALow, 3);
    countLow = _mm_extract_epi32(vCountLow, 3);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 3);

    __m256i ret = _mm256_set1_epi32(0);
    ret = _mm256_insertf128_si256(ret, vAHi, 1);
    ret = _mm256_insertf128_si256(ret, vALow, 0);
    return ret;
}


INLINE
__m256i _simdemu_sllv_epi32(__m256i vA, __m256i vCount)
{
    int32_t aHi, aLow, countHi, countLow;
    __m128i vAHi = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 1));
    __m128i vALow = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 0));
    __m128i vCountHi = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 1));
    __m128i vCountLow = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 0));

    aHi = _mm_extract_epi32(vAHi, 0);
    countHi = _mm_extract_epi32(vCountHi, 0);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 0);

    aLow = _mm_extract_epi32(vALow, 0);
    countLow = _mm_extract_epi32(vCountLow, 0);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 0);

    aHi = _mm_extract_epi32(vAHi, 1);
    countHi = _mm_extract_epi32(vCountHi, 1);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 1);

    aLow = _mm_extract_epi32(vALow, 1);
    countLow = _mm_extract_epi32(vCountLow, 1);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 1);

    aHi = _mm_extract_epi32(vAHi, 2);
    countHi = _mm_extract_epi32(vCountHi, 2);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 2);

    aLow = _mm_extract_epi32(vALow, 2);
    countLow = _mm_extract_epi32(vCountLow, 2);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 2);

    aHi = _mm_extract_epi32(vAHi, 3);
    countHi = _mm_extract_epi32(vCountHi, 3);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 3);

    aLow = _mm_extract_epi32(vALow, 3);
    countLow = _mm_extract_epi32(vCountLow, 3);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 3);

    __m256i ret = _mm256_set1_epi32(0);
    ret = _mm256_insertf128_si256(ret, vAHi, 1);
    ret = _mm256_insertf128_si256(ret, vALow, 0);
    return ret;
}

#define _simd_mul_epi32 _simdemu_mul_epi32
#define _simd_mullo_epi32 _simdemu_mullo_epi32
#define _simd_sub_epi32 _simdemu_sub_epi32
#define _simd_sub_epi64 _simdemu_sub_epi64
#define _simd_min_epi32 _simdemu_min_epi32
#define _simd_min_epu32 _simdemu_min_epu32
#define _simd_max_epi32 _simdemu_max_epi32
#define _simd_max_epu32 _simdemu_max_epu32
#define _simd_add_epi32 _simdemu_add_epi32
#define _simd_and_si _simdemu_and_si
#define _simd_andnot_si _simdemu_andnot_si
#define _simd_cmpeq_epi32 _simdemu_cmpeq_epi32
#define _simd_cmplt_epi32 _simdemu_cmplt_epi32
#define _simd_cmpgt_epi32 _simdemu_cmpgt_epi32
#define _simd_or_si _simdemu_or_si
#define _simd_xor_si _simdemu_xor_si
#define _simd_castps_si _mm256_castps_si256
#define _simd_adds_epu8 _simdemu_adds_epu8
#define _simd_subs_epu8 _simdemu_subs_epu8
#define _simd_add_epi8 _simdemu_add_epi8
#define _simd_cmpeq_epi64 _simdemu_cmpeq_epi64
#define _simd_cmpgt_epi64 _simdemu_cmpgt_epi64
#define _simd_cmpgt_epi8 _simdemu_cmpgt_epi8
#define _simd_cmpeq_epi8 _simdemu_cmpeq_epi8
#define _simd_cmpgt_epi16 _simdemu_cmpgt_epi16
#define _simd_cmpeq_epi16 _simdemu_cmpeq_epi16
#define _simd_movemask_epi8 _simdemu_movemask_epi8
#define _simd_permute_ps _simdemu_permute_ps
#define _simd_permute_epi32 _simdemu_permute_epi32
#define _simd_srlv_epi32 _simdemu_srlv_epi32
#define _simd_sllv_epi32 _simdemu_sllv_epi32

SIMD_EMU_EPI(_simdemu_mul_epi32, _mm_mul_epi32)
SIMD_EMU_EPI(_simdemu_mullo_epi32, _mm_mullo_epi32)
SIMD_EMU_EPI(_simdemu_sub_epi32, _mm_sub_epi32)
SIMD_EMU_EPI(_simdemu_sub_epi64, _mm_sub_epi64)
SIMD_EMU_EPI(_simdemu_min_epi32, _mm_min_epi32)
SIMD_EMU_EPI(_simdemu_min_epu32, _mm_min_epu32)
SIMD_EMU_EPI(_simdemu_max_epi32, _mm_max_epi32)
SIMD_EMU_EPI(_simdemu_max_epu32, _mm_max_epu32)
SIMD_EMU_EPI(_simdemu_add_epi32, _mm_add_epi32)
SIMD_EMU_EPI(_simdemu_and_si, _mm_and_si128)
SIMD_EMU_EPI(_simdemu_andnot_si, _mm_andnot_si128)
SIMD_EMU_EPI(_simdemu_cmpeq_epi32, _mm_cmpeq_epi32)
SIMD_EMU_EPI(_simdemu_cmplt_epi32, _mm_cmplt_epi32)
SIMD_EMU_EPI(_simdemu_cmpgt_epi32, _mm_cmpgt_epi32)
SIMD_EMU_EPI(_simdemu_or_si, _mm_or_si128)
SIMD_EMU_EPI(_simdemu_xor_si, _mm_xor_si128)
SIMD_EMU_EPI(_simdemu_adds_epu8, _mm_adds_epu8)
SIMD_EMU_EPI(_simdemu_subs_epu8, _mm_subs_epu8)
SIMD_EMU_EPI(_simdemu_add_epi8, _mm_add_epi8)
SIMD_EMU_EPI(_simdemu_cmpeq_epi64, _mm_cmpeq_epi64)
SIMD_EMU_EPI(_simdemu_cmpgt_epi64, _mm_cmpgt_epi64)
SIMD_EMU_EPI(_simdemu_cmpgt_epi8, _mm_cmpgt_epi8)
SIMD_EMU_EPI(_simdemu_cmpeq_epi8, _mm_cmpeq_epi8)
SIMD_EMU_EPI(_simdemu_cmpgt_epi16, _mm_cmpgt_epi16)
SIMD_EMU_EPI(_simdemu_cmpeq_epi16, _mm_cmpeq_epi16)

#define _simd_unpacklo_epi32(a, b) _mm256_castps_si256(_mm256_unpacklo_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)))
#define _simd_unpackhi_epi32(a, b) _mm256_castps_si256(_mm256_unpackhi_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)))
#define _simd_unpacklo_epi64(a, b) _mm256_castpd_si256(_mm256_unpacklo_pd(_mm256_castsi256_pd(a), _mm256_castsi256_pd(b)))
#define _simd_unpackhi_epi64(a, b) _mm256_castpd_si256(_mm256_unpackhi_pd(_mm256_castsi256_pd(a), _mm256_castsi256_pd(b)))

#define _simd_slli_epi32(a,i) _simdemu_slli_epi32(a,i)
#define _simd_srai_epi32(a,i) _simdemu_srai_epi32(a,i)
#define _simd_srli_epi32(a,i) _simdemu_srli_epi32(a,i)
#define _simd_srlisi_ps(a,i) _mm256_castsi256_ps(_simdemu_srli_si128<i>(_mm256_castps_si256(a)))

#define _simd128_fmadd_ps _mm_fmaddemu_ps
#define _simd_fmadd_ps _mm_fmaddemu256_ps
#define _simd_fmsub_ps _mm_fmsubemu256_ps
#define _simd_shuffle_epi8 _simdemu_shuffle_epi8 
SIMD_EMU_EPI(_simdemu_shuffle_epi8, _mm_shuffle_epi8)

INLINE
__m128 _mm_fmaddemu_ps(__m128 a, __m128 b, __m128 c)
{
    __m128 res = _mm_mul_ps(a, b);
    res = _mm_add_ps(res, c);
    return res;
}

INLINE
__m256 _mm_fmaddemu256_ps(__m256 a, __m256 b, __m256 c)
{
    __m256 res = _mm256_mul_ps(a, b);
    res = _mm256_add_ps(res, c);
    return res;
}

INLINE
__m256 _mm_fmsubemu256_ps(__m256 a, __m256 b, __m256 c)
{
    __m256 res = _mm256_mul_ps(a, b);
    res = _mm256_sub_ps(res, c);
    return res;
}

INLINE
__m256 _simd_i32gather_ps(const float* pBase, __m256i vOffsets, const int scale)
{
    uint32_t *pOffsets = (uint32_t*)&vOffsets;
    simdscalar vResult;
    float* pResult = (float*)&vResult;
    for (uint32_t i = 0; i < KNOB_SIMD_WIDTH; ++i)
    {
        uint32_t offset = pOffsets[i];
        offset = offset * scale;
        pResult[i] = *(float*)(((const uint8_t*)pBase + offset));
    }

    return vResult;
}

INLINE
__m256 _simd_mask_i32gather_ps(__m256 vSrc, const float* pBase, __m256i vOffsets, __m256 vMask, const int scale)
{
    uint32_t *pOffsets = (uint32_t*)&vOffsets;
    simdscalar vResult = vSrc;
    float* pResult = (float*)&vResult;
    DWORD index;
    uint32_t mask = _simd_movemask_ps(vMask);
    while (_BitScanForward(&index, mask))
    {
        mask &= ~(1 << index);
        uint32_t offset = pOffsets[index];
        offset = offset * scale;
        pResult[index] = *(float*)(((const uint8_t*)pBase + offset));
    }

    return vResult;
}

INLINE
__m256i _simd_abs_epi32(__m256i a)
{
        __m128i aHi = _mm256_extractf128_si256(a, 1);
        __m128i aLo = _mm256_castsi256_si128(a);
        __m128i absLo = _mm_abs_epi32(aLo);
        __m128i absHi = _mm_abs_epi32(aHi);
        __m256i result = _mm256_castsi128_si256(absLo);
        result = _mm256_insertf128_si256(result, absHi, 1);
        return result;
}

INLINE 
int _simdemu_movemask_epi8(__m256i a)
{
    __m128i aHi = _mm256_extractf128_si256(a, 1);
    __m128i aLo = _mm256_castsi256_si128(a);

    int resHi = _mm_movemask_epi8(aHi);
    int resLo = _mm_movemask_epi8(aLo);

    return (resHi << 16) | resLo;
}

INLINE
__m256i _simd_cvtepu8_epi32(__m128i a)
{
    __m128i resultlo = _mm_cvtepu8_epi32(a);
    __m128i resulthi = _mm_shuffle_epi8(a, _mm_set_epi32(0x80808007, 0x80808006, 0x80808005, 0x80808004));

    __m256i result = _mm256_castsi128_si256(resultlo);

    return _mm256_insertf128_si256(result, resulthi, 1);
}

INLINE
__m256i _simd_cvtepu16_epi32(__m128i a)
{
    __m128i resultlo = _mm_cvtepu16_epi32(a);
    __m128i resulthi = _mm_shuffle_epi8(a, _mm_set_epi32(0x80800F0E, 0x80800D0C, 0x80800B0A, 0x80800908));

    __m256i result = _mm256_castsi128_si256(resultlo);

    return _mm256_insertf128_si256(result, resulthi, 1);
}

INLINE
__m256i _simd_packus_epi32(__m256i a, __m256i b)
{
    __m128i alo = _mm256_extractf128_si256(a, 0);
    __m128i ahi = _mm256_extractf128_si256(a, 1);

    __m128i blo = _mm256_extractf128_si256(b, 0);
    __m128i bhi = _mm256_extractf128_si256(b, 1);

    __m128i resultlo = _mm_packus_epi32(alo, blo);
    __m128i resulthi = _mm_packus_epi32(ahi, bhi);

    __m256i result = _mm256_castsi128_si256(resultlo);

    return _mm256_insertf128_si256(result, resulthi, 1);
}

INLINE
__m256i _simd_packs_epi32(__m256i a, __m256i b)
{
    __m128i alo = _mm256_extractf128_si256(a, 0);
    __m128i ahi = _mm256_extractf128_si256(a, 1);

    __m128i blo = _mm256_extractf128_si256(b, 0);
    __m128i bhi = _mm256_extractf128_si256(b, 1);

    __m128i resultlo = _mm_packs_epi32(alo, blo);
    __m128i resulthi = _mm_packs_epi32(ahi, bhi);

    __m256i result = _mm256_castsi128_si256(resultlo);

    return _mm256_insertf128_si256(result, resulthi, 1);
}

#else

#define _simd_mul_epi32 _mm256_mul_epi32
#define _simd_mullo_epi32 _mm256_mullo_epi32
#define _simd_sub_epi32 _mm256_sub_epi32
#define _simd_sub_epi64 _mm256_sub_epi64
#define _simd_min_epi32 _mm256_min_epi32
#define _simd_max_epi32 _mm256_max_epi32
#define _simd_min_epu32 _mm256_min_epu32
#define _simd_max_epu32 _mm256_max_epu32
#define _simd_add_epi32 _mm256_add_epi32
#define _simd_and_si _mm256_and_si256
#define _simd_andnot_si _mm256_andnot_si256
#define _simd_cmpeq_epi32 _mm256_cmpeq_epi32
#define _simd_cmplt_epi32(a,b) _mm256_cmpgt_epi32(b,a)
#define _simd_cmpgt_epi32(a,b) _mm256_cmpgt_epi32(a,b)
#define _simd_or_si _mm256_or_si256
#define _simd_xor_si _mm256_xor_si256
#define _simd_castps_si _mm256_castps_si256

#define _simd_unpacklo_epi32 _mm256_unpacklo_epi32
#define _simd_unpackhi_epi32 _mm256_unpackhi_epi32
#define _simd_unpacklo_epi64 _mm256_unpacklo_epi64
#define _simd_unpackhi_epi64 _mm256_unpackhi_epi64

#define _simd_srli_si(a,i) _simdemu_srli_si128<i>(a)
#define _simd_slli_epi32 _mm256_slli_epi32
#define _simd_srai_epi32 _mm256_srai_epi32
#define _simd_srli_epi32 _mm256_srli_epi32
#define _simd_srlisi_ps(a,i) _mm256_castsi256_ps(_simdemu_srli_si128<i>(_mm256_castps_si256(a)))
#define _simd128_fmadd_ps _mm_fmadd_ps
#define _simd_fmadd_ps _mm256_fmadd_ps
#define _simd_fmsub_ps _mm256_fmsub_ps
#define _simd_shuffle_epi8 _mm256_shuffle_epi8 
#define _simd_adds_epu8 _mm256_adds_epu8
#define _simd_subs_epu8 _mm256_subs_epu8
#define _simd_add_epi8 _mm256_add_epi8
#define _simd_i32gather_ps _mm256_i32gather_ps
#define _simd_mask_i32gather_ps _mm256_mask_i32gather_ps
#define _simd_abs_epi32 _mm256_abs_epi32

#define _simd_cmpeq_epi64 _mm256_cmpeq_epi64
#define _simd_cmpgt_epi64 _mm256_cmpgt_epi64
#define _simd_cmpgt_epi8  _mm256_cmpgt_epi8
#define _simd_cmpeq_epi8  _mm256_cmpeq_epi8
#define _simd_cmpgt_epi16  _mm256_cmpgt_epi16
#define _simd_cmpeq_epi16  _mm256_cmpeq_epi16
#define _simd_movemask_epi8 _mm256_movemask_epi8
#define _simd_permute_ps _mm256_permutevar8x32_ps
#define _simd_permute_epi32 _mm256_permutevar8x32_epi32
#define _simd_srlv_epi32 _mm256_srlv_epi32
#define _simd_sllv_epi32 _mm256_sllv_epi32
#define _simd_cvtepu8_epi32 _mm256_cvtepu8_epi32
#define _simd_cvtepu16_epi32 _mm256_cvtepu16_epi32
#define _simd_packus_epi32 _mm256_packus_epi32
#define _simd_packs_epi32 _mm256_packs_epi32

#endif

#define _simd_permute2f128_ps _mm256_permute2f128_ps
#define _simd_permute2f128_pd _mm256_permute2f128_pd
#define _simd_permute2f128_si _mm256_permute2f128_si256
#define _simd_shuffle_ps _mm256_shuffle_ps
#define _simd_shuffle_pd _mm256_shuffle_pd
#define _simd_shuffle_epi32(a, b, imm8) _mm256_castps_si256(_mm256_shuffle_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b), imm8))
#define _simd_shuffle_epi64(a, b, imm8) _mm256_castps_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a), _mm256_castsi256_pd(b), imm8))
#define _simd_set1_epi32 _mm256_set1_epi32
#define _simd_set_epi32 _mm256_set_epi32
#define _simd_set1_epi8 _mm256_set1_epi8
#define _simd_setzero_si _mm256_setzero_si256
#define _simd_cvttps_epi32 _mm256_cvttps_epi32
#define _simd_store_si _mm256_store_si256
#define _simd_broadcast_ss _mm256_broadcast_ss
#define _simd_maskstore_ps _mm256_maskstore_ps
#define _simd_load_si _mm256_load_si256
#define _simd_loadu_si _mm256_loadu_si256
#define _simd_sub_ps _mm256_sub_ps
#define _simd_testz_ps _mm256_testz_ps
#define _simd_xor_ps _mm256_xor_ps

INLINE
simdscalari _simd_blendv_epi32(simdscalari a, simdscalari b, simdscalar mask)
{
    return _simd_castps_si(_simd_blendv_ps(_simd_castsi_ps(a), _simd_castsi_ps(b), mask));
}

INLINE
simdscalari _simd_blendv_epi32(simdscalari a, simdscalari b, simdscalari mask)
{
    return _simd_castps_si(_simd_blendv_ps(_simd_castsi_ps(a), _simd_castsi_ps(b), _simd_castsi_ps(mask)));
}

// convert bitmask to vector mask
INLINE
simdscalar vMask(int32_t mask)
{
    __m256i vec = _mm256_set1_epi32(mask);
    const __m256i bit = _mm256_set_epi32(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
    vec = _simd_and_si(vec, bit);
    vec = _simd_cmplt_epi32(_mm256_setzero_si256(), vec);
    return _simd_castsi_ps(vec);
}

INLINE
void _simd_mov(simdscalar &r, unsigned int rlane, simdscalar& s, unsigned int slane)
{
    OSALIGNSIMD(float) rArray[KNOB_SIMD_WIDTH], sArray[KNOB_SIMD_WIDTH];
    _mm256_store_ps(rArray, r);
    _mm256_store_ps(sArray, s);
    rArray[rlane] = sArray[slane];
    r = _mm256_load_ps(rArray);
}

INLINE __m256i _simdemu_slli_epi32(__m256i a, uint32_t i)
{
    __m128i aHi = _mm256_extractf128_si256(a, 1);
    __m128i aLo = _mm256_castsi256_si128(a);

    __m128i resHi = _mm_slli_epi32(aHi, i);
    __m128i resLo = _mm_slli_epi32(aLo, i);

    __m256i result = _mm256_castsi128_si256(resLo);
            result = _mm256_insertf128_si256(result, resHi, 1);

    return result;
}

INLINE __m256i _simdemu_srai_epi32(__m256i a, uint32_t i)
{
    __m128i aHi = _mm256_extractf128_si256(a, 1);
    __m128i aLo = _mm256_castsi256_si128(a);

    __m128i resHi = _mm_srai_epi32(aHi, i);
    __m128i resLo = _mm_srai_epi32(aLo, i);

    __m256i result = _mm256_castsi128_si256(resLo);
            result = _mm256_insertf128_si256(result, resHi, 1);

    return result;
}

INLINE __m256i _simdemu_srli_epi32(__m256i a, uint32_t i)
{
    __m128i aHi = _mm256_extractf128_si256(a, 1);
    __m128i aLo = _mm256_castsi256_si128(a);

    __m128i resHi = _mm_srli_epi32(aHi, i);
    __m128i resLo = _mm_srli_epi32(aLo, i);

    __m256i result = _mm256_castsi128_si256(resLo);
    result = _mm256_insertf128_si256(result, resHi, 1);

    return result;
}

INLINE
void _simdvec_transpose(simdvector &v)
{
    SWR_ASSERT(false, "Need to implement 8 wide version");
}

#elif KNOB_SIMD_WIDTH == 16

#if ENABLE_AVX512_EMULATION

#define SIMD_EMU_AVX512_0(type, func, intrin) \
INLINE type func()\
{\
    type result;\
\
    result.lo = intrin();\
    result.hi = intrin();\
\
    return result;\
}

#define SIMD_EMU_AVX512_1(type, func, intrin) \
INLINE type func(type a)\
{\
    type result;\
\
    result.lo = intrin(a.lo);\
    result.hi = intrin(a.hi);\
\
    return result;\
}

#define SIMD_EMU_AVX512_2(type, func, intrin) \
INLINE type func(type a, type b)\
{\
    type result;\
\
    result.lo = intrin(a.lo, b.lo);\
    result.hi = intrin(a.hi, b.hi);\
\
    return result;\
}

#define SIMD_EMU_AVX512_3(type, func, intrin) \
INLINE type func(type a, type b, type c)\
{\
    type result;\
\
    result.lo = intrin(a.lo, b.lo, c.lo);\
    result.hi = intrin(a.hi, b.hi, c.hi);\
\
    return result;\
}

SIMD_EMU_AVX512_0(simdscalar, _simd_setzero_ps, _mm256_setzero_ps)
SIMD_EMU_AVX512_0(simdscalari, _simd_setzero_si, _mm256_setzero_si256)

INLINE simdscalar _simd_set1_ps(float a)
{
    simdscalar result;

    result.lo = _mm256_set1_ps(a);
    result.hi = _mm256_set1_ps(a);

    return result;
}

INLINE simdscalari _simd_set1_epi8(char a)
{
    simdscalari result;

    result.lo = _mm256_set1_epi8(a);
    result.hi = _mm256_set1_epi8(a);

    return result;
}

INLINE simdscalari _simd_set1_epi32(int a)
{
    simdscalari result;

    result.lo = _mm256_set1_epi32(a);
    result.hi = _mm256_set1_epi32(a);

    return result;
}

INLINE simdscalari _simd_set_epi32(int e7, int e6, int e5, int e4, int e3, int e2, int e1, int e0)
{
    simdscalari result;

    result.lo = _mm256_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0);
    result.hi = _mm256_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0);

    return result;
}

INLINE simdscalari _simd_set_epi32(int e15, int e14, int e13, int e12, int e11, int e10, int e9, int e8, int e7, int e6, int e5, int e4, int e3, int e2, int e1, int e0)
{
    simdscalari result;

    result.lo = _mm256_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0);
    result.hi = _mm256_set_epi32(e15, e14, e13, e12, e11, e10, e9, e8);

    return result;
}

INLINE simdscalar _simd_load_ps(float const *m)
{
    float const *n = reinterpret_cast<float const *>(reinterpret_cast<uint8_t const *>(m) + sizeof(simdscalar::lo));

    simdscalar result;

    result.lo = _mm256_load_ps(m);
    result.hi = _mm256_load_ps(n);

    return result;
}

INLINE simdscalar _simd_loadu_ps(float const *m)
{
    float const *n = reinterpret_cast<float const *>(reinterpret_cast<uint8_t const *>(m) + sizeof(simdscalar::lo));

    simdscalar result;

    result.lo = _mm256_loadu_ps(m);
    result.hi = _mm256_loadu_ps(n);

    return result;
}

INLINE simdscalar _simd_load1_ps(float const *m)
{
    simdscalar result;

    result.lo = _mm256_broadcast_ss(m);
    result.hi = _mm256_broadcast_ss(m);

    return result;
}

INLINE simdscalari _simd_load_si(simdscalari const *m)
{
    simdscalari result;

    result.lo = _mm256_load_si256(&m[0].lo);
    result.hi = _mm256_load_si256(&m[0].hi);

    return result;
}

INLINE simdscalari _simd_loadu_si(simdscalari const *m)
{
    simdscalari result;

    result.lo = _mm256_loadu_si256(&m[0].lo);
    result.hi = _mm256_loadu_si256(&m[0].hi);

    return result;
}

INLINE simdscalar _simd_broadcast_ss(float const *m)
{
    simdscalar result;

    result.lo = _mm256_broadcast_ss(m);
    result.hi = _mm256_broadcast_ss(m);

    return result;
}

INLINE simdscalar _simd_broadcast_ps(__m128 const *m)
{
    simdscalar result;

    result.lo = _mm256_broadcast_ps(m);
    result.hi = _mm256_broadcast_ps(m);

    return result;
}

INLINE void _simd_store_ps(float *m, simdscalar a)
{
    float *n = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(m) + sizeof(simdscalar::lo));

    _mm256_store_ps(m, a.lo);
    _mm256_store_ps(n, a.hi);
}

INLINE void _simd_maskstore_ps(float *m, simdscalari mask, simdscalar a)
{
    float *n = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(m) + sizeof(simdscalar::lo));

    _mm256_maskstore_ps(m, mask.lo, a.lo);
    _mm256_maskstore_ps(n, mask.hi, a.hi);
}

INLINE void _simd_store_si(simdscalari *m, simdscalari a)
{
    _mm256_store_si256(&m[0].lo, a.lo);
    _mm256_store_si256(&m[0].hi, a.hi);
}

INLINE simdscalar _simd_blend_ps(simdscalar a, simdscalar b, const simdmask mask)
{
    simdscalar result;

    result.lo = _mm256_blend_ps(a.lo, b.lo, reinterpret_cast<const uint8_t *>(&mask)[0]);
    result.hi = _mm256_blend_ps(a.hi, b.hi, reinterpret_cast<const uint8_t *>(&mask)[1]);

    return result;
}

SIMD_EMU_AVX512_3(simdscalar, _simd_blendv_ps, _mm256_blendv_ps)

INLINE simdscalari _simd_blendv_epi32(simdscalari a, simdscalari b, const simdscalar mask)
{
    simdscalari result;

    result.lo = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.lo), _mm256_castsi256_ps(b.lo), mask.lo));
    result.hi = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.hi), _mm256_castsi256_ps(b.hi), mask.hi));

    return result;
}

INLINE simdscalari _simd_blendv_epi32(simdscalari a, simdscalari b, const simdscalari mask)
{
    simdscalari result;

    result.lo = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.lo), _mm256_castsi256_ps(b.lo), _mm256_castsi256_ps(mask.lo)));
    result.hi = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.hi), _mm256_castsi256_ps(b.hi), _mm256_castsi256_ps(mask.hi)));

    return result;
}

SIMD_EMU_AVX512_2(simdscalar, _simd_mul_ps, _mm256_mul_ps)
SIMD_EMU_AVX512_2(simdscalar, _simd_add_ps, _mm256_add_ps)
SIMD_EMU_AVX512_2(simdscalar, _simd_sub_ps, _mm256_sub_ps)
SIMD_EMU_AVX512_1(simdscalar, _simd_rsqrt_ps, _mm256_rsqrt_ps)
SIMD_EMU_AVX512_2(simdscalar, _simd_min_ps, _mm256_min_ps)
SIMD_EMU_AVX512_2(simdscalar, _simd_max_ps, _mm256_max_ps)

INLINE simdmask _simd_movemask_ps(simdscalar a)
{
    simdmask mask;

    reinterpret_cast<uint8_t *>(&mask)[0] = _mm256_movemask_ps(a.lo);
    reinterpret_cast<uint8_t *>(&mask)[1] = _mm256_movemask_ps(a.hi);

    return mask;
}

INLINE simdmask _simd_movemask_pd(simdscalard a)
{
    simdmask mask;

    reinterpret_cast<uint8_t *>(&mask)[0] = _mm256_movemask_pd(a.lo);
    reinterpret_cast<uint8_t *>(&mask)[1] = _mm256_movemask_pd(a.hi);

    return mask;
}

INLINE simdmask _simd_movemask_epi8(simdscalari a)
{
    simdmask mask;

    reinterpret_cast<uint8_t *>(&mask)[0] = _mm256_movemask_epi8(a.lo);
    reinterpret_cast<uint8_t *>(&mask)[1] = _mm256_movemask_epi8(a.hi);

    return mask;
}

INLINE simdscalari _simd_cvtps_epi32(simdscalar a)
{
    simdscalari result;

    result.lo = _mm256_cvtps_epi32(a.lo);
    result.hi = _mm256_cvtps_epi32(a.hi);

    return result;
}

INLINE simdscalari _simd_cvttps_epi32(simdscalar a)
{
    simdscalari result;

    result.lo = _mm256_cvttps_epi32(a.lo);
    result.hi = _mm256_cvttps_epi32(a.hi);

    return result;
}

INLINE simdscalar _simd_cvtepi32_ps(simdscalari a)
{
    simdscalar result;

    result.lo = _mm256_cvtepi32_ps(a.lo);
    result.hi = _mm256_cvtepi32_ps(a.hi);

    return result;
}

INLINE simdscalar _simd_cmp_ps(simdscalar a, simdscalar b, const int comp)
{
    simdscalar result;

    result.lo = _mm256_cmp_ps(a.lo, b.lo, comp);
    result.hi = _mm256_cmp_ps(a.hi, b.hi, comp);

    return result;
}

#define _simd_cmplt_ps(a, b) _simd_cmp_ps(a, b, _CMP_LT_OQ)
#define _simd_cmpgt_ps(a, b) _simd_cmp_ps(a, b, _CMP_GT_OQ)
#define _simd_cmpneq_ps(a, b) _simd_cmp_ps(a, b, _CMP_NEQ_OQ)
#define _simd_cmpeq_ps(a, b) _simd_cmp_ps(a, b, _CMP_EQ_OQ)
#define _simd_cmpge_ps(a, b) _simd_cmp_ps(a, b, _CMP_GE_OQ)
#define _simd_cmple_ps(a, b) _simd_cmp_ps(a, b, _CMP_LE_OQ)

SIMD_EMU_AVX512_2(simdscalar, _simd_and_ps, _mm256_and_ps)
SIMD_EMU_AVX512_2(simdscalar, _simd_or_ps, _mm256_or_ps)
SIMD_EMU_AVX512_1(simdscalar, _simd_rcp_ps, _mm256_rcp_ps)
SIMD_EMU_AVX512_2(simdscalar, _simd_div_ps, _mm256_div_ps)

INLINE simdscalar _simd_castsi_ps(simdscalari a)
{
    return *reinterpret_cast<simdscalar *>(&a);
}

INLINE simdscalari _simd_castps_si(simdscalar a)
{
    return *reinterpret_cast<simdscalari *>(&a);
}

INLINE simdscalard _simd_castsi_pd(simdscalari a)
{
    return *reinterpret_cast<simdscalard *>(&a);
}

INLINE simdscalari _simd_castpd_si(simdscalard a)
{
    return *reinterpret_cast<simdscalari *>(&a);
}

INLINE simdscalar _simd_castpd_ps(simdscalard a)
{
    return *reinterpret_cast<simdscalar *>(&a);
}

INLINE simdscalard _simd_castps_pd(simdscalar a)
{
    return *reinterpret_cast<simdscalard *>(&a);
}

SIMD_EMU_AVX512_2(simdscalar, _simd_andnot_ps, _mm256_andnot_ps)

INLINE simdscalar _simd_round_ps(simdscalar a, const int mode)
{
    simdscalar result;

    result.lo = _mm256_round_ps(a.lo, mode);
    result.hi = _mm256_round_ps(a.hi, mode);

    return result;
}

SIMD_EMU_AVX512_2(simdscalari, _simd_mul_epi32, _mm256_mul_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_mullo_epi32, _mm256_mullo_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_sub_epi32, _mm256_sub_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_sub_epi64, _mm256_sub_epi64)
SIMD_EMU_AVX512_2(simdscalari, _simd_min_epi32, _mm256_min_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_max_epi32, _mm256_max_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_min_epu32, _mm256_min_epu32)
SIMD_EMU_AVX512_2(simdscalari, _simd_max_epu32, _mm256_max_epu32)
SIMD_EMU_AVX512_2(simdscalari, _simd_add_epi32, _mm256_add_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_and_si, _mm256_and_si256)
SIMD_EMU_AVX512_2(simdscalari, _simd_andnot_si, _mm256_andnot_si256)
SIMD_EMU_AVX512_2(simdscalari, _simd_or_si, _mm256_or_si256)
SIMD_EMU_AVX512_2(simdscalari, _simd_xor_si, _mm256_xor_si256)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpeq_epi32, _mm256_cmpeq_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpgt_epi32, _mm256_cmpgt_epi32)

INLINE int _simd_testz_ps(simdscalar a, simdscalar b)
{
    int lo = _mm256_testz_ps(a.lo, b.lo);
    int hi = _mm256_testz_ps(a.hi, b.hi);

    return lo & hi;
}

#define _simd_cmplt_epi32(a, b) _simd_cmpgt_epi32(b, a)

SIMD_EMU_AVX512_2(simdscalari, _simd_unpacklo_epi32, _mm256_unpacklo_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_unpackhi_epi32, _mm256_unpackhi_epi32)

INLINE simdscalari _simd_slli_epi32(simdscalari a, const int imm8)
{
    simdscalari result;

    result.lo = _mm256_slli_epi32(a.lo, imm8);
    result.hi = _mm256_slli_epi32(a.hi, imm8);

    return result;
}

INLINE simdscalari _simd_srai_epi32(simdscalari a, const int imm8)
{
    simdscalari result;

    result.lo = _mm256_srai_epi32(a.lo, imm8);
    result.hi = _mm256_srai_epi32(a.hi, imm8);

    return result;
}

INLINE simdscalari _simd_srli_epi32(simdscalari a, const int imm8)
{
    simdscalari result;

    result.lo = _mm256_srli_epi32(a.lo, imm8);
    result.hi = _mm256_srli_epi32(a.hi, imm8);

    return result;
}

#define _simd128_fmadd_ps _mm_fmadd_ps

SIMD_EMU_AVX512_3(simdscalar, _simd_fmadd_ps, _mm256_fmadd_ps)
SIMD_EMU_AVX512_3(simdscalar, _simd_fmsub_ps, _mm256_fmsub_ps)

SIMD_EMU_AVX512_2(simdscalari, _simd_shuffle_epi8, _mm256_shuffle_epi8)
SIMD_EMU_AVX512_2(simdscalari, _simd_adds_epu8, _mm256_adds_epu8)
SIMD_EMU_AVX512_2(simdscalari, _simd_subs_epu8, _mm256_subs_epu8)
SIMD_EMU_AVX512_2(simdscalari, _simd_add_epi8, _mm256_add_epi8)

INLINE simdscalar _simd_i32gather_ps(float const *m, simdscalari a, const int imm8)
{
    simdscalar result;

    result.lo = _mm256_i32gather_ps(m, a.lo, imm8);
    result.hi = _mm256_i32gather_ps(m, a.hi, imm8);

    return result;
}

SIMD_EMU_AVX512_1(simdscalari, _simd_abs_epi32, _mm256_abs_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpeq_epi64, _mm256_cmpeq_epi64)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpgt_epi64, _mm256_cmpgt_epi64)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpeq_epi16, _mm256_cmpeq_epi16)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpgt_epi16, _mm256_cmpgt_epi16)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpeq_epi8, _mm256_cmpeq_epi8)
SIMD_EMU_AVX512_2(simdscalari, _simd_cmpgt_epi8, _mm256_cmpgt_epi8)

INLINE simdscalar _simd_permute_ps(simdscalar a, simdscalari b)
{
    simdscalar result;

    result.lo = _mm256_permutevar8x32_ps(a.lo, b.lo);
    result.hi = _mm256_permutevar8x32_ps(a.hi, b.hi);

    return result;
}

SIMD_EMU_AVX512_2(simdscalari, _simd_permute_epi32, _mm256_permutevar8x32_epi32)

SIMD_EMU_AVX512_2(simdscalari, _simd_srlv_epi32, _mm256_srlv_epi32)
SIMD_EMU_AVX512_2(simdscalari, _simd_sllv_epi32, _mm256_sllv_epi32)

INLINE simdscalar _simd_shuffle_ps(simdscalar a, simdscalar b, const int imm8)
{
    simdscalar result;

    result.lo = _mm256_shuffle_ps(a.lo, b.lo, imm8);
    result.hi = _mm256_shuffle_ps(a.hi, b.hi, imm8);

    return result;
}

// convert bitmask to vector mask
INLINE simdscalar vMask(int32_t mask)
{
    simdscalari temp = _simd_set1_epi32(mask);

    simdscalari bits = _simd_set_epi32(0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001);

    simdscalari result = _simd_cmplt_epi32(_simd_setzero_si(), _simd_and_si(temp, bits));

    return _simd_castsi_ps(result);
}

#else

INLINE __m512 _m512_broadcast_ss(void const *m)
{
    return _mm512_extload_ps(m, _MM_UPCONV_PS_NONE, _MM_BROADCAST_1X16, 0);
}

INLINE __m512 _m512_broadcast_ps(void const *m)
{
    return _mm512_extload_ps(m, _MM_UPCONV_PS_NONE, _MM_BROADCAST_4X16, 0);
}

INLINE __m512 _m512_blend_ps(__m512 a, __m512 b, const int mask)
{
    const __mask16 mask16 = _mm512_int2mask(mask);

    return _mm512_mask_blend_ps(mask16, a, b);
}

INLINE __m512 _m512_blendv_ps(__m512 a, __m512 b, __m512 mask)
{
    const __mask16 mask16 = _mm512_cmpeq_ps_mask(mask, _mm512_setzero_ps());

    return _mm512_mask_blend_ps(mask16, a, b);
}

INLINE int _m512_movemask_ps(__m512 a)
{
    __m512 mask = _mm512_set1_epi32(0x80000000);

    __m512 temp = _mm512_and_epi32(a, mask);

    const __mask16 mask16 = _mm512_cmpeq_epu32_mask(temp, mask);

    return _mm512mask2int(mask16);
}

INLINE int _m512_movemask_pd(__m512 a)
{
    __m512 mask = _mm512_set1_epi64(0x8000000000000000);

    __m512 temp = _mm512_and_epi64(a, mask);

    const __mask16 mask16 = _mm512_cmpeq_epu64_mask(temp, mask);

    return _mm512mask2int(mask16);
}

INLINE __m512 _m512_cmp_ps(__m512 a, __m512 b, __m512 comp)
{
    const __mask16 mask16 = _mm512_cmpeq_ps_mask(a, b, comp);

    return _mm512_mask_blend_epi32(mask16, _mm512_setzero_epi32(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE __m512 _mm512_cmplt_epi32(__m512 a, __m512 b)
{
    const __mask16 mask16 = _mm512_cmplt_epi32_mask(a, b);

    return _mm512_mask_blend_epi32(mask16, _mm512_setzero_epi32(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE __m512 _mm512_cmpgt_epi32(__m512 a, __m512 b)
{
    const __mask16 mask16 = _mm512_cmpgt_epi32_mask(a, b);

    return _mm512_mask_blend_epi32(mask16, _mm512_setzero_epi32(), _mm512_set1_epi32(0xFFFFFFFF));
}

#define _simd_load_ps _mm512_load_ps
#define _simd_load1_ps _mm256_broadcast_ss
#define _simd_loadu_ps _mm512_loadu_ps
#define _simd_setzero_ps _mm512_setzero_ps
#define _simd_set1_ps _mm512_set1_ps
#define _simd_blend_ps  _mm512_blend_ps
#define _simd_blendv_ps _mm512_blendv_ps
#define _simd_store_ps _mm512_store_ps
#define _simd_mul_ps _mm512_mul_ps
#define _simd_add_ps _mm512_add_ps
#define _simd_sub_ps _mm512_sub_ps
#define _simd_rsqrt_ps _mm512_rsqrt28_ps
#define _simd_min_ps _mm512_min_ps
#define _simd_max_ps _mm512_max_ps
#define _simd_movemask_ps _mm512_movemask_ps
#define _simd_cvtps_epi32 _mm512_cvtps_epi32
#define _simd_cvttps_epi32 _mm512_cvttps_epi32
#define _simd_cvtepi32_ps _mm512_cvtepi32_ps
#define _simd_cmplt_ps(a, b) _mm512_cmp_ps(a, b, _CMP_LT_OQ)
#define _simd_cmpgt_ps(a, b) _mm512_cmp_ps(a, b, _CMP_GT_OQ)
#define _simd_cmpneq_ps(a, b) _mm512_cmp_ps(a, b, _CMP_NEQ_OQ)
#define _simd_cmpeq_ps(a, b) _mm512_cmp_ps(a, b, _CMP_EQ_OQ)
#define _simd_cmpge_ps(a, b) _mm512_cmp_ps(a, b, _CMP_GE_OQ)
#define _simd_cmple_ps(a, b) _mm512_cmp_ps(a, b, _CMP_LE_OQ)
#define _simd_cmp_ps(a, b, comp) _mm512_cmp_ps(a, b, comp)
#define _simd_and_ps _mm512_and_ps
#define _simd_or_ps _mm512_or_ps
#define _simd_rcp_ps _mm512_rcp28_ps
#define _simd_div_ps _mm512_div_ps
#define _simd_castsi_ps _mm512_castsi512_ps
#define _simd_andnot_ps _mm512_andnot_ps
#define _simd_round_ps _mm512_round_ps
#define _simd_castpd_ps _mm512_castpd_ps
#define _simd_broadcast_ps _m512_broadcast_ps
#define _simd_movemask_pd _mm512_movemask_pd
#define _simd_castsi_pd _mm512_castsi512_pd

#define _simd_mul_epi32 _mm512_mul_epi32
#define _simd_mullo_epi32 _mm512_mullo_epi32
#define _simd_sub_epi32 _mm512_sub_epi32
#define _simd_sub_epi64 _mm512_sub_epi64
#define _simd_min_epi32 _mm512_min_epi32
#define _simd_max_epi32 _mm512_max_epi32
#define _simd_min_epu32 _mm512_min_epu32
#define _simd_max_epu32 _mm512_max_epu32
#define _simd_add_epi32 _mm512_add_epi32
#define _simd_and_si _mm512_and_si512
#define _simd_andnot_si _mm512_andnot_si512
#define _simd_cmpeq_epi32 _mm512_cmpeq_epi32
#define _simd_cmplt_epi32(a,b) _mm256_cmpgt_epi32(b,a)
#define _simd_cmpgt_epi32(a,b) _mm256_cmpgt_epi32(a,b)
#define _simd_or_si _mm512_or_si512
#define _simd_castps_si _mm512_castps_si512

#endif

#else
#error Unsupported vector width
#endif

// Populates a simdvector from a vector. So p = xyzw becomes xxxx yyyy zzzz wwww.
INLINE
void _simdvec_load_ps(simdvector& r, const float *p)
{
    r[0] = _simd_set1_ps(p[0]);
    r[1] = _simd_set1_ps(p[1]);
    r[2] = _simd_set1_ps(p[2]);
    r[3] = _simd_set1_ps(p[3]);
}

INLINE
void _simdvec_mov(simdvector& r, const simdscalar& s)
{
    r[0] = s;
    r[1] = s;
    r[2] = s;
    r[3] = s;
}

INLINE
void _simdvec_mov(simdvector& r, const simdvector& v)
{
    r[0] = v[0];
    r[1] = v[1];
    r[2] = v[2];
    r[3] = v[3];
}

#if 0
// just move a lane from the source simdvector to dest simdvector
INLINE
void _simdvec_mov(simdvector &r, unsigned int rlane, simdvector& s, unsigned int slane)
{
    _simd_mov(r[0], rlane, s[0], slane);
    _simd_mov(r[1], rlane, s[1], slane);
    _simd_mov(r[2], rlane, s[2], slane);
    _simd_mov(r[3], rlane, s[3], slane);
}

#endif
INLINE
void _simdvec_dp3_ps(simdscalar& r, const simdvector& v0, const simdvector& v1)
{
    simdscalar tmp;
    r   = _simd_mul_ps(v0[0], v1[0]);   // (v0.x*v1.x)

    tmp = _simd_mul_ps(v0[1], v1[1]);       // (v0.y*v1.y)
    r   = _simd_add_ps(r, tmp);         // (v0.x*v1.x) + (v0.y*v1.y)

    tmp = _simd_mul_ps(v0[2], v1[2]);   // (v0.z*v1.z)
    r   = _simd_add_ps(r, tmp);         // (v0.x*v1.x) + (v0.y*v1.y) + (v0.z*v1.z)
}

INLINE
void _simdvec_dp4_ps(simdscalar& r, const simdvector& v0, const simdvector& v1)
{
    simdscalar tmp;
    r   = _simd_mul_ps(v0[0], v1[0]);   // (v0.x*v1.x)

    tmp = _simd_mul_ps(v0[1], v1[1]);       // (v0.y*v1.y)
    r   = _simd_add_ps(r, tmp);         // (v0.x*v1.x) + (v0.y*v1.y)

    tmp = _simd_mul_ps(v0[2], v1[2]);   // (v0.z*v1.z)
    r   = _simd_add_ps(r, tmp);         // (v0.x*v1.x) + (v0.y*v1.y) + (v0.z*v1.z)

    tmp = _simd_mul_ps(v0[3], v1[3]);   // (v0.w*v1.w)
    r   = _simd_add_ps(r, tmp);         // (v0.x*v1.x) + (v0.y*v1.y) + (v0.z*v1.z)
}

INLINE
simdscalar _simdvec_rcp_length_ps(const simdvector& v)
{
    simdscalar length;
    _simdvec_dp4_ps(length, v, v);
    return _simd_rsqrt_ps(length);
}

INLINE
void _simdvec_normalize_ps(simdvector& r, const simdvector& v)
{
    simdscalar vecLength;
    vecLength = _simdvec_rcp_length_ps(v);

    r[0] = _simd_mul_ps(v[0], vecLength);
    r[1] = _simd_mul_ps(v[1], vecLength);
    r[2] = _simd_mul_ps(v[2], vecLength);
    r[3] = _simd_mul_ps(v[3], vecLength);
}

INLINE
void _simdvec_mul_ps(simdvector& r, const simdvector& v, const simdscalar& s)
{
    r[0] = _simd_mul_ps(v[0], s);
    r[1] = _simd_mul_ps(v[1], s);
    r[2] = _simd_mul_ps(v[2], s);
    r[3] = _simd_mul_ps(v[3], s);
}

INLINE
void _simdvec_mul_ps(simdvector& r, const simdvector& v0, const simdvector& v1)
{
    r[0] = _simd_mul_ps(v0[0], v1[0]);
    r[1] = _simd_mul_ps(v0[1], v1[1]);
    r[2] = _simd_mul_ps(v0[2], v1[2]);
    r[3] = _simd_mul_ps(v0[3], v1[3]);
}

INLINE
void _simdvec_add_ps(simdvector& r, const simdvector& v0, const simdvector& v1)
{
    r[0] = _simd_add_ps(v0[0], v1[0]);
    r[1] = _simd_add_ps(v0[1], v1[1]);
    r[2] = _simd_add_ps(v0[2], v1[2]);
    r[3] = _simd_add_ps(v0[3], v1[3]);
}

INLINE
void _simdvec_min_ps(simdvector& r, const simdvector& v0, const simdscalar& s)
{
    r[0] = _simd_min_ps(v0[0], s);
    r[1] = _simd_min_ps(v0[1], s);
    r[2] = _simd_min_ps(v0[2], s);
    r[3] = _simd_min_ps(v0[3], s);
}

INLINE
void _simdvec_max_ps(simdvector& r, const simdvector& v0, const simdscalar& s)
{
    r[0] = _simd_max_ps(v0[0], s);
    r[1] = _simd_max_ps(v0[1], s);
    r[2] = _simd_max_ps(v0[2], s);
    r[3] = _simd_max_ps(v0[3], s);
}

// Matrix4x4 * Vector4
//   outVec.x = (m00 * v.x) + (m01 * v.y) + (m02 * v.z) + (m03 * v.w)
//   outVec.y = (m10 * v.x) + (m11 * v.y) + (m12 * v.z) + (m13 * v.w)
//   outVec.z = (m20 * v.x) + (m21 * v.y) + (m22 * v.z) + (m23 * v.w)
//   outVec.w = (m30 * v.x) + (m31 * v.y) + (m32 * v.z) + (m33 * v.w)
INLINE
void _simd_mat4x4_vec4_multiply(
    simdvector& result,
    const float *pMatrix,
    const simdvector& v)
{
    simdscalar m;
    simdscalar r0;
    simdscalar r1;

    m   = _simd_load1_ps(pMatrix + 0*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 0*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 0*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 0*4 + 3);    // m[row][3]
    r1  = _simd_mul_ps(m, v[3]);                // (m3 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * v.w)
    result[0] = r0;

    m   = _simd_load1_ps(pMatrix + 1*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 1*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 1*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 1*4 + 3);    // m[row][3]
    r1  = _simd_mul_ps(m, v[3]);                // (m3 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * v.w)
    result[1] = r0;

    m   = _simd_load1_ps(pMatrix + 2*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 2*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 2*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 2*4 + 3);    // m[row][3]
    r1  = _simd_mul_ps(m, v[3]);                // (m3 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * v.w)
    result[2] = r0;

    m   = _simd_load1_ps(pMatrix + 3*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 3*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 3*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 3*4 + 3);    // m[row][3]
    r1  = _simd_mul_ps(m, v[3]);                // (m3 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * v.w)
    result[3] = r0;
}

// Matrix4x4 * Vector3 - Direction Vector where w = 0.
//   outVec.x = (m00 * v.x) + (m01 * v.y) + (m02 * v.z) + (m03 * 0)
//   outVec.y = (m10 * v.x) + (m11 * v.y) + (m12 * v.z) + (m13 * 0)
//   outVec.z = (m20 * v.x) + (m21 * v.y) + (m22 * v.z) + (m23 * 0)
//   outVec.w = (m30 * v.x) + (m31 * v.y) + (m32 * v.z) + (m33 * 0)
INLINE
void _simd_mat3x3_vec3_w0_multiply(
    simdvector& result,
    const float *pMatrix,
    const simdvector& v)
{
    simdscalar m;
    simdscalar r0;
    simdscalar r1;

    m   = _simd_load1_ps(pMatrix + 0*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 0*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 0*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    result[0] = r0;

    m   = _simd_load1_ps(pMatrix + 1*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 1*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 1*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    result[1] = r0;

    m   = _simd_load1_ps(pMatrix + 2*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 2*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 2*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    result[2] = r0;

    result[3] = _simd_setzero_ps();
}

// Matrix4x4 * Vector3 - Position vector where w = 1.
//   outVec.x = (m00 * v.x) + (m01 * v.y) + (m02 * v.z) + (m03 * 1)
//   outVec.y = (m10 * v.x) + (m11 * v.y) + (m12 * v.z) + (m13 * 1)
//   outVec.z = (m20 * v.x) + (m21 * v.y) + (m22 * v.z) + (m23 * 1)
//   outVec.w = (m30 * v.x) + (m31 * v.y) + (m32 * v.z) + (m33 * 1)
INLINE
void _simd_mat4x4_vec3_w1_multiply(
    simdvector& result,
    const float *pMatrix,
    const simdvector& v)
{
    simdscalar m;
    simdscalar r0;
    simdscalar r1;

    m   = _simd_load1_ps(pMatrix + 0*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 0*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 0*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 0*4 + 3);    // m[row][3]
    r0  = _simd_add_ps(r0, m);                  // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * 1)
    result[0] = r0;

    m   = _simd_load1_ps(pMatrix + 1*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 1*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 1*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 1*4 + 3);    // m[row][3]
    r0  = _simd_add_ps(r0, m);                  // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * 1)
    result[1] = r0;

    m   = _simd_load1_ps(pMatrix + 2*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 2*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 2*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 2*4 + 3);    // m[row][3]
    r0  = _simd_add_ps(r0, m);                  // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * 1)
    result[2] = r0;

    m   = _simd_load1_ps(pMatrix + 3*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 3*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 3*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 3*4 + 3);    // m[row][3]
    result[3]   = _simd_add_ps(r0, m);          // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * 1)
}

INLINE
void _simd_mat4x3_vec3_w1_multiply(
    simdvector& result,
    const float *pMatrix,
    const simdvector& v)
{
    simdscalar m;
    simdscalar r0;
    simdscalar r1;

    m   = _simd_load1_ps(pMatrix + 0*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 0*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 0*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 0*4 + 3);    // m[row][3]
    r0  = _simd_add_ps(r0, m);                  // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * 1)
    result[0] = r0;

    m   = _simd_load1_ps(pMatrix + 1*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 1*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 1*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 1*4 + 3);    // m[row][3]
    r0  = _simd_add_ps(r0, m);                  // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * 1)
    result[1] = r0;

    m   = _simd_load1_ps(pMatrix + 2*4 + 0);    // m[row][0]
    r0  = _simd_mul_ps(m, v[0]);                // (m00 * v.x)
    m   = _simd_load1_ps(pMatrix + 2*4 + 1);    // m[row][1]
    r1  = _simd_mul_ps(m, v[1]);                // (m1 * v.y)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y)
    m   = _simd_load1_ps(pMatrix + 2*4 + 2);    // m[row][2]
    r1  = _simd_mul_ps(m, v[2]);                // (m2 * v.z)
    r0  = _simd_add_ps(r0, r1);                 // (m0 * v.x) + (m1 * v.y) + (m2 * v.z)
    m   = _simd_load1_ps(pMatrix + 2*4 + 3);    // m[row][3]
    r0  = _simd_add_ps(r0, m);                  // (m0 * v.x) + (m1 * v.y) + (m2 * v.z) + (m2 * 1)
    result[2] = r0;
    result[3] = _simd_set1_ps(1.0f);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Compute plane equation vA * vX + vB * vY + vC
INLINE simdscalar vplaneps(simdscalar vA, simdscalar vB, simdscalar vC, simdscalar &vX, simdscalar &vY)
{
    simdscalar vOut = _simd_fmadd_ps(vA, vX, vC);
    vOut = _simd_fmadd_ps(vB, vY, vOut);
    return vOut;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Compute plane equation vA * vX + vB * vY + vC
INLINE __m128 vplaneps128(__m128 vA, __m128 vB, __m128 vC, __m128 &vX, __m128 &vY)
{
    __m128 vOut = _simd128_fmadd_ps(vA, vX, vC);
    vOut = _simd128_fmadd_ps(vB, vY, vOut);
    return vOut;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Interpolates a single component.
/// @param vI - barycentric I
/// @param vJ - barycentric J
/// @param pInterpBuffer - pointer to attribute barycentric coeffs
template<UINT Attrib, UINT Comp, UINT numComponents = 4>
static INLINE simdscalar InterpolateComponent(simdscalar vI, simdscalar vJ, const float *pInterpBuffer)
{
    const float *pInterpA = &pInterpBuffer[Attrib * 3 * numComponents + 0 + Comp];
    const float *pInterpB = &pInterpBuffer[Attrib * 3 * numComponents + numComponents + Comp];
    const float *pInterpC = &pInterpBuffer[Attrib * 3 * numComponents + numComponents * 2 + Comp];

    simdscalar vA = _simd_broadcast_ss(pInterpA);
    simdscalar vB = _simd_broadcast_ss(pInterpB);
    simdscalar vC = _simd_broadcast_ss(pInterpC);

    simdscalar vk = _simd_sub_ps(_simd_sub_ps(_simd_set1_ps(1.0f), vI), vJ);
    vC = _simd_mul_ps(vk, vC);
    
    return vplaneps(vA, vB, vC, vI, vJ);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Interpolates a single component.
/// @param vI - barycentric I
/// @param vJ - barycentric J
/// @param pInterpBuffer - pointer to attribute barycentric coeffs
template<UINT Attrib, UINT Comp, UINT numComponents = 4>
static INLINE __m128 InterpolateComponent(__m128 vI, __m128 vJ, const float *pInterpBuffer)
{
    const float *pInterpA = &pInterpBuffer[Attrib * 3 * numComponents + 0 + Comp];
    const float *pInterpB = &pInterpBuffer[Attrib * 3 * numComponents + numComponents + Comp];
    const float *pInterpC = &pInterpBuffer[Attrib * 3 * numComponents + numComponents * 2 + Comp];

    __m128 vA = _mm_broadcast_ss(pInterpA);
    __m128 vB = _mm_broadcast_ss(pInterpB);
    __m128 vC = _mm_broadcast_ss(pInterpC);

    __m128 vk = _mm_sub_ps(_mm_sub_ps(_mm_set1_ps(1.0f), vI), vJ);
    vC = _mm_mul_ps(vk, vC);

    return vplaneps128(vA, vB, vC, vI, vJ);
}

static INLINE __m128 _simd128_abs_ps(__m128 a)
{
    __m128i ai = _mm_castps_si128(a);
    return _mm_castsi128_ps(_mm_and_si128(ai, _mm_set1_epi32(0x7fffffff)));
}

static INLINE simdscalar _simd_abs_ps(simdscalar a)
{
    simdscalari ai = _simd_castps_si(a);
    return _simd_castsi_ps(_simd_and_si(ai, _simd_set1_epi32(0x7fffffff)));
}

INLINE
UINT pdep_u32(UINT a, UINT mask)
{
#if KNOB_ARCH >= KNOB_ARCH_AVX2
    return _pdep_u32(a, mask);
#else
    UINT result = 0;

    // copied from http://wm.ite.pl/articles/pdep-soft-emu.html 
    // using bsf instead of funky loop
    DWORD maskIndex;
    while (_BitScanForward(&maskIndex, mask))
    {
        // 1. isolate lowest set bit of mask
        const UINT lowest = 1 << maskIndex;

        // 2. populate LSB from src
        const UINT LSB = (UINT)((int)(a << 31) >> 31);

        // 3. copy bit from mask
        result |= LSB & lowest;

        // 4. clear lowest bit
        mask &= ~lowest;

        // 5. prepare for next iteration
        a >>= 1;
    }

    return result;
#endif
}

INLINE
UINT pext_u32(UINT a, UINT mask)
{
#if KNOB_ARCH >= KNOB_ARCH_AVX2
    return _pext_u32(a, mask);
#else
    UINT result = 0;
    DWORD maskIndex;
    uint32_t currentBit = 0;
    while (_BitScanForward(&maskIndex, mask))
    {
        // 1. isolate lowest set bit of mask
        const UINT lowest = 1 << maskIndex;

        // 2. copy bit from mask
        result |= ((a & lowest) > 0) << currentBit++;

        // 3. clear lowest bit
        mask &= ~lowest;
    }
    return result;
#endif
}

#if ENABLE_AVX512_SIMD16
#include "simd16intrin.h"
#endif//ENABLE_AVX512_SIMD16

#endif//__SWR_SIMDINTRIN_H__
