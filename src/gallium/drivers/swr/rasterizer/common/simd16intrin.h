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

#ifndef __SWR_SIMD16INTRIN_H__
#define __SWR_SIMD16INTRIN_H__

#if ENABLE_AVX512_SIMD16

#if KNOB_SIMD16_WIDTH == 16

#if ENABLE_AVX512_EMULATION
struct simd16scalar
{
    __m256  lo;
    __m256  hi;
};
struct simd16scalard
{
    __m256d lo;
    __m256d hi;
};
struct simd16scalari
{
    __m256i lo;
    __m256i hi;
};
typedef uint16_t simd16mask;

#define _simd16_masklo(mask) ((mask) & 0xFF)
#define _simd16_maskhi(mask) (((mask) >> 8))
#define _simd16_setmask(hi, lo) (((hi) << 8) | (lo))

#else
typedef __m512 simd16scalar;
typedef __m512d simd16scalard;
typedef __m512i simd16scalari;
typedef __mmask16 simd16mask;
#endif//ENABLE_AVX512_EMULATION
#else
#error Unsupported vector width
#endif//KNOB_SIMD16_WIDTH == 16

OSALIGN(union, KNOB_SIMD16_BYTES) simd16vector
{
    simd16scalar  v[4];
    struct
    {
        simd16scalar x, y, z, w;
    };

    simd16scalar& operator[] (const int i) { return v[i]; }
    const simd16scalar& operator[] (const int i) const { return v[i]; }
};

#if ENABLE_AVX512_EMULATION

#define SIMD16_EMU_AVX512_0(type, func, intrin) \
INLINE type func()\
{\
    type result;\
\
    result.lo = intrin();\
    result.hi = intrin();\
\
    return result;\
}

#define SIMD16_EMU_AVX512_1(type, func, intrin) \
INLINE type func(type a)\
{\
    type result;\
\
    result.lo = intrin(a.lo);\
    result.hi = intrin(a.hi);\
\
    return result;\
}

#define SIMD16_EMU_AVX512_2(type, func, intrin) \
INLINE type func(type a, type b)\
{\
    type result;\
\
    result.lo = intrin(a.lo, b.lo);\
    result.hi = intrin(a.hi, b.hi);\
\
    return result;\
}

#define SIMD16_EMU_AVX512_3(type, func, intrin) \
INLINE type func(type a, type b, type c)\
{\
    type result;\
\
    result.lo = intrin(a.lo, b.lo, c.lo);\
    result.hi = intrin(a.hi, b.hi, c.hi);\
\
    return result;\
}

SIMD16_EMU_AVX512_0(simd16scalar, _simd16_setzero_ps, _mm256_setzero_ps)
SIMD16_EMU_AVX512_0(simd16scalari, _simd16_setzero_si, _mm256_setzero_si256)

INLINE simd16scalar _simd16_set1_ps(float a)
{
    simd16scalar result;

    result.lo = _mm256_set1_ps(a);
    result.hi = _mm256_set1_ps(a);

    return result;
}

INLINE simd16scalari _simd16_set1_epi8(char a)
{
    simd16scalari result;

    result.lo = _mm256_set1_epi8(a);
    result.hi = _mm256_set1_epi8(a);

    return result;
}

INLINE simd16scalari _simd16_set1_epi32(int a)
{
    simd16scalari result;

    result.lo = _mm256_set1_epi32(a);
    result.hi = _mm256_set1_epi32(a);

    return result;
}

INLINE simd16scalar _simd16_set_ps(float e15, float e14, float e13, float e12, float e11, float e10, float e9, float e8, float e7, float e6, float e5, float e4, float e3, float e2, float e1, float e0)
{
    simd16scalar result;

    result.lo = _mm256_set_ps(e7, e6, e5, e4, e3, e2, e1, e0);
    result.hi = _mm256_set_ps(e15, e14, e13, e12, e11, e10, e9, e8);

    return result;
}

INLINE simd16scalari _simd16_set_epi32(int e15, int e14, int e13, int e12, int e11, int e10, int e9, int e8, int e7, int e6, int e5, int e4, int e3, int e2, int e1, int e0)
{
    simd16scalari result;

    result.lo = _mm256_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0);
    result.hi = _mm256_set_epi32(e15, e14, e13, e12, e11, e10, e9, e8);

    return result;
}

INLINE simd16scalar _simd16_set_ps(float e7, float e6, float e5, float e4, float e3, float e2, float e1, float e0)
{
    simd16scalar result;

    result.lo = _mm256_set_ps(e7, e6, e5, e4, e3, e2, e1, e0);
    result.hi = _mm256_set_ps(e7, e6, e5, e4, e3, e2, e1, e0);

    return result;
}

INLINE simd16scalari _simd16_set_epi32(int e7, int e6, int e5, int e4, int e3, int e2, int e1, int e0)
{
    simd16scalari result;

    result.lo = _mm256_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0);
    result.hi = _mm256_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0);

    return result;
}

INLINE simd16scalar _simd16_load_ps(float const *m)
{
    simd16scalar result;

    float const *n = reinterpret_cast<float const *>(reinterpret_cast<uint8_t const *>(m) + sizeof(result.lo));

    result.lo = _mm256_load_ps(m);
    result.hi = _mm256_load_ps(n);

    return result;
}

INLINE simd16scalar _simd16_loadu_ps(float const *m)
{
    simd16scalar result;

    float const *n = reinterpret_cast<float const *>(reinterpret_cast<uint8_t const *>(m) + sizeof(result.lo));

    result.lo = _mm256_loadu_ps(m);
    result.hi = _mm256_loadu_ps(n);

    return result;
}

INLINE simd16scalar _simd16_load1_ps(float const *m)
{
    simd16scalar result;

    result.lo = _mm256_broadcast_ss(m);
    result.hi = _mm256_broadcast_ss(m);

    return result;
}

INLINE simd16scalari _simd16_load_si(simd16scalari const *m)
{
    simd16scalari result;

    result.lo = _mm256_load_si256(&m[0].lo);
    result.hi = _mm256_load_si256(&m[0].hi);

    return result;
}

INLINE simd16scalari _simd16_loadu_si(simd16scalari const *m)
{
    simd16scalari result;

    result.lo = _mm256_loadu_si256(&m[0].lo);
    result.hi = _mm256_loadu_si256(&m[0].hi);

    return result;
}

INLINE simd16scalar _simd16_broadcast_ss(float const *m)
{
    simd16scalar result;

    result.lo = _mm256_broadcast_ss(m);
    result.hi = _mm256_broadcast_ss(m);

    return result;
}

INLINE simd16scalar _simd16_broadcast_ps(__m128 const *m)
{
    simd16scalar result;

    result.lo = _mm256_broadcast_ps(m);
    result.hi = _mm256_broadcast_ps(m);

    return result;
}

INLINE void _simd16_store_ps(float *m, simd16scalar a)
{
    float *n = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(m) + sizeof(a.lo));

    _mm256_store_ps(m, a.lo);
    _mm256_store_ps(n, a.hi);
}

INLINE void _simd16_maskstore_ps(float *m, simd16scalari mask, simd16scalar a)
{
    float *n = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(m) + sizeof(a.lo));

    _mm256_maskstore_ps(m, mask.lo, a.lo);
    _mm256_maskstore_ps(n, mask.hi, a.hi);
}

INLINE void _simd16_store_si(simd16scalari *m, simd16scalari a)
{
    _mm256_store_si256(&m[0].lo, a.lo);
    _mm256_store_si256(&m[0].hi, a.hi);
}

INLINE simdscalar _simd16_extract_ps(simd16scalar a, int imm8)
{
    switch (imm8)
    {
    case 0:
        return a.lo;
    case 1:
        return a.hi;
    }
    return _simd_set1_ps(0.0f);
}

INLINE simdscalari _simd16_extract_si(simd16scalari a, int imm8)
{
    switch (imm8)
    {
    case 0:
        return a.lo;
    case 1:
        return a.hi;
    }
    return _simd_set1_epi32(0);
}

INLINE simd16scalar _simd16_insert_ps(simd16scalar a, simdscalar b, int imm8)
{
    switch (imm8)
    {
    case 0:
        a.lo = b;
        break;
    case 1:
        a.hi = b;
        break;
    }
    return a;
}

INLINE simd16scalari _simd16_insert_si(simd16scalari a, simdscalari b, int imm8)
{
    switch (imm8)
    {
    case 0:
        a.lo = b;
        break;
    case 1:
        a.hi = b;
        break;
    }
    return a;
}

template <simd16mask mask>
INLINE simd16scalar _simd16_blend_ps_temp(simd16scalar a, simd16scalar b)
{
    simd16scalar result;

    result.lo = _mm256_blend_ps(a.lo, b.lo, _simd16_masklo(mask));
    result.hi = _mm256_blend_ps(a.hi, b.hi, _simd16_maskhi(mask));

    return result;
}

#define _simd16_blend_ps(a, b, mask) _simd16_blend_ps_temp<mask>(a, b)

SIMD16_EMU_AVX512_3(simd16scalar, _simd16_blendv_ps, _mm256_blendv_ps)

INLINE simd16scalari _simd16_blendv_epi32(simd16scalari a, simd16scalari b, const simd16scalar mask)
{
    simd16scalari result;

    result.lo = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.lo), _mm256_castsi256_ps(b.lo), mask.lo));
    result.hi = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.hi), _mm256_castsi256_ps(b.hi), mask.hi));

    return result;
}

INLINE simd16scalari _simd16_blendv_epi32(simd16scalari a, simd16scalari b, const simd16scalari mask)
{
    simd16scalari result;

    result.lo = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.lo), _mm256_castsi256_ps(b.lo), _mm256_castsi256_ps(mask.lo)));
    result.hi = _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(a.hi), _mm256_castsi256_ps(b.hi), _mm256_castsi256_ps(mask.hi)));

    return result;
}

SIMD16_EMU_AVX512_2(simd16scalar, _simd16_mul_ps, _mm256_mul_ps)
SIMD16_EMU_AVX512_2(simd16scalar, _simd16_add_ps, _mm256_add_ps)
SIMD16_EMU_AVX512_2(simd16scalar, _simd16_sub_ps, _mm256_sub_ps)
SIMD16_EMU_AVX512_1(simd16scalar, _simd16_rsqrt_ps, _mm256_rsqrt_ps)
SIMD16_EMU_AVX512_2(simd16scalar, _simd16_min_ps, _mm256_min_ps)
SIMD16_EMU_AVX512_2(simd16scalar, _simd16_max_ps, _mm256_max_ps)

INLINE simd16mask _simd16_movemask_ps(simd16scalar a)
{
    simd16mask mask;

    reinterpret_cast<uint8_t *>(&mask)[0] = _mm256_movemask_ps(a.lo);
    reinterpret_cast<uint8_t *>(&mask)[1] = _mm256_movemask_ps(a.hi);

    return mask;
}

INLINE simd16mask _simd16_movemask_pd(simd16scalard a)
{
    simd16mask mask;

    reinterpret_cast<uint8_t *>(&mask)[0] = _mm256_movemask_pd(a.lo);
    reinterpret_cast<uint8_t *>(&mask)[1] = _mm256_movemask_pd(a.hi);

    return mask;
}

INLINE simd16mask _simd16_movemask_epi8(simd16scalari a)
{
    simd16mask mask;

    reinterpret_cast<uint8_t *>(&mask)[0] = _mm256_movemask_epi8(a.lo);
    reinterpret_cast<uint8_t *>(&mask)[1] = _mm256_movemask_epi8(a.hi);

    return mask;
}

INLINE simd16scalari _simd16_cvtps_epi32(simd16scalar a)
{
    simd16scalari result;

    result.lo = _mm256_cvtps_epi32(a.lo);
    result.hi = _mm256_cvtps_epi32(a.hi);

    return result;
}

INLINE simd16scalari _simd16_cvttps_epi32(simd16scalar a)
{
    simd16scalari result;

    result.lo = _mm256_cvttps_epi32(a.lo);
    result.hi = _mm256_cvttps_epi32(a.hi);

    return result;
}

INLINE simd16scalar _simd16_cvtepi32_ps(simd16scalari a)
{
    simd16scalar result;

    result.lo = _mm256_cvtepi32_ps(a.lo);
    result.hi = _mm256_cvtepi32_ps(a.hi);

    return result;
}

template <int comp>
INLINE simd16scalar _simd16_cmp_ps(simd16scalar a, simd16scalar b)
{
    simd16scalar result;

    result.lo = _mm256_cmp_ps(a.lo, b.lo, comp);
    result.hi = _mm256_cmp_ps(a.hi, b.hi, comp);

    return result;
}

#define _simd16_cmplt_ps(a, b) _simd16_cmp_ps<_CMP_LT_OQ>(a, b)
#define _simd16_cmpgt_ps(a, b) _simd16_cmp_ps<_CMP_GT_OQ>(a, b)
#define _simd16_cmpneq_ps(a, b) _simd16_cmp_ps<_CMP_NEQ_OQ>(a, b)
#define _simd16_cmpeq_ps(a, b) _simd16_cmp_ps<_CMP_EQ_OQ>(a, b)
#define _simd16_cmpge_ps(a, b) _simd16_cmp_ps<_CMP_GE_OQ>(a, b)
#define _simd16_cmple_ps(a, b) _simd16_cmp_ps<_CMP_LE_OQ>(a, b)

SIMD16_EMU_AVX512_2(simd16scalar, _simd16_and_ps, _simd_and_ps)
SIMD16_EMU_AVX512_2(simd16scalar, _simd16_or_ps, _simd_or_ps)
SIMD16_EMU_AVX512_1(simd16scalar, _simd16_rcp_ps, _simd_rcp_ps)
SIMD16_EMU_AVX512_2(simd16scalar, _simd16_div_ps, _simd_div_ps)

INLINE simd16scalar _simd16_castsi_ps(simd16scalari a)
{
    return *reinterpret_cast<simd16scalar *>(&a);
}

INLINE simd16scalari _simd16_castps_si(simd16scalar a)
{
    return *reinterpret_cast<simd16scalari *>(&a);
}

INLINE simd16scalard _simd16_castsi_pd(simd16scalari a)
{
    return *reinterpret_cast<simd16scalard *>(&a);
}

INLINE simd16scalari _simd16_castpd_si(simd16scalard a)
{
    return *reinterpret_cast<simd16scalari *>(&a);
}

INLINE simd16scalar _simd16_castpd_ps(simd16scalard a)
{
    return *reinterpret_cast<simd16scalar *>(&a);
}

INLINE simd16scalard _simd16_castps_pd(simd16scalar a)
{
    return *reinterpret_cast<simd16scalard *>(&a);
}

SIMD16_EMU_AVX512_2(simd16scalar, _simd16_andnot_ps, _mm256_andnot_ps)

template <int mode>
INLINE simd16scalar _simd16_round_ps_temp(simd16scalar a)
{
    simd16scalar result;

    result.lo = _mm256_round_ps(a.lo, mode);
    result.hi = _mm256_round_ps(a.hi, mode);

    return result;
}

#define _simd16_round_ps(a, mode) _simd16_round_ps_temp<mode>(a)

SIMD16_EMU_AVX512_2(simd16scalari, _simd16_mul_epi32, _simd_mul_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_mullo_epi32, _simd_mullo_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_sub_epi32, _simd_sub_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_sub_epi64, _simd_sub_epi64)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_min_epi32, _simd_min_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_max_epi32, _simd_max_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_min_epu32, _simd_min_epu32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_max_epu32, _simd_max_epu32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_add_epi32, _simd_add_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_and_si, _simd_and_si)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_andnot_si, _simd_andnot_si)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_or_si, _simd_or_si)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_xor_si, _simd_xor_si)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpeq_epi32, _simd_cmpeq_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpgt_epi32, _simd_cmpgt_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmplt_epi32, _simd_cmplt_epi32)

INLINE int _simd16_testz_ps(simd16scalar a, simd16scalar b)
{
    int lo = _mm256_testz_ps(a.lo, b.lo);
    int hi = _mm256_testz_ps(a.hi, b.hi);

    return lo & hi;
}

#define _simd16_cmplt_epi32(a, b) _simd16_cmpgt_epi32(b, a)

SIMD16_EMU_AVX512_2(simd16scalar, _simd16_unpacklo_ps, _simd_unpacklo_ps)
SIMD16_EMU_AVX512_2(simd16scalar, _simd16_unpackhi_ps, _simd_unpackhi_ps)
SIMD16_EMU_AVX512_2(simd16scalard, _simd16_unpacklo_pd, _simd_unpacklo_pd)
SIMD16_EMU_AVX512_2(simd16scalard, _simd16_unpackhi_pd, _simd_unpackhi_pd)

SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpacklo_epi8, _simd_unpacklo_epi8)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpackhi_epi8, _simd_unpackhi_epi8)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpacklo_epi16, _simd_unpacklo_epi16)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpackhi_epi16, _simd_unpackhi_epi16)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpacklo_epi32, _simd_unpacklo_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpackhi_epi32, _simd_unpackhi_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpacklo_epi64, _simd_unpacklo_epi64)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_unpackhi_epi64, _simd_unpackhi_epi64)

template <int imm8>
INLINE simd16scalari _simd16_slli_epi32_temp(simd16scalari a)
{
    simd16scalari result;

    result.lo = _simd_slli_epi32(a.lo, imm8);
    result.hi = _simd_slli_epi32(a.hi, imm8);

    return result;
}

#define _simd16_slli_epi32(a, imm8) _simd16_slli_epi32_temp<imm8>(a)

template <int imm8>
INLINE simd16scalari _simd16_srai_epi32_temp(simd16scalari a)
{
    simd16scalari result;

    result.lo = _simd_srai_epi32(a.lo, imm8);
    result.hi = _simd_srai_epi32(a.hi, imm8);

    return result;
}

#define _simd16_srai_epi32(a, imm8) _simd16_srai_epi32_temp<imm8>(a)

template <int imm8>
INLINE simd16scalari _simd16_srli_epi32_temp(simd16scalari a)
{
    simd16scalari result;

    result.lo = _simd_srli_epi32(a.lo, imm8);
    result.hi = _simd_srli_epi32(a.hi, imm8);

    return result;
}

#define _simd16_srli_epi32(a, imm8) _simd16_srli_epi32_temp<imm8>(a)

SIMD16_EMU_AVX512_3(simd16scalar, _simd16_fmadd_ps, _simd_fmadd_ps)
SIMD16_EMU_AVX512_3(simd16scalar, _simd16_fmsub_ps, _simd_fmsub_ps)

//__m256 _simd_i32gather_ps(const float* pBase, __m256i vOffsets, const int scale)
template <int scale>
INLINE simd16scalar _simd16_i32gather_ps_temp(const float *m, simd16scalari index)
{
    simd16scalar result;

    result.lo = _simd_i32gather_ps(m, index.lo, scale);
    result.hi = _simd_i32gather_ps(m, index.hi, scale);

    return result;
}

#define _simd16_i32gather_ps(m, index, scale) _simd16_i32gather_ps_temp<scale>(m, index)

//__m256 _simd_mask_i32gather_ps(__m256 vSrc, const float* pBase, __m256i vOffsets, __m256 vMask, const int scale)
template <int scale>
INLINE simd16scalar _simd16_mask_i32gather_ps_temp(simd16scalar a, const float *m, simd16scalari index, simd16scalari mask)
{
    simd16scalar result;

    result.lo = _simd_mask_i32gather_ps(a.lo, m, index.lo, _simd_castsi_ps(mask.lo), scale);
    result.hi = _simd_mask_i32gather_ps(a.hi, m, index.hi, _simd_castsi_ps(mask.hi), scale);

    return result;
}

#define _simd16_mask_i32gather_ps(a, m, index, mask, scale) _simd16_mask_i32gather_ps_temp<scale>(a, m, mask, index)

SIMD16_EMU_AVX512_2(simd16scalari, _simd16_shuffle_epi8, _simd_shuffle_epi8)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_adds_epu8, _simd_adds_epu8)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_subs_epu8, _simd_subs_epu8)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_add_epi8, _simd_add_epi8)
SIMD16_EMU_AVX512_1(simd16scalari, _simd16_abs_epi32, _simd_abs_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpeq_epi64, _simd_cmpeq_epi64)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpgt_epi64, _simd_cmpgt_epi64)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpeq_epi16, _simd_cmpeq_epi16)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpgt_epi16, _simd_cmpgt_epi16)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpeq_epi8, _simd_cmpeq_epi8)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_cmpgt_epi8, _simd_cmpgt_epi8)

INLINE simd16scalar _simd16_permute_ps(simd16scalar a, simd16scalari i)
{
    simd16scalar result;

    const simdscalari mask = _simd_set1_epi32(7);

    simdscalar lolo = _simd_permute_ps(a.lo, _simd_and_si(i.lo, mask));
    simdscalar lohi = _simd_permute_ps(a.hi, _simd_and_si(i.lo, mask));

    simdscalar hilo = _simd_permute_ps(a.lo, _simd_and_si(i.hi, mask));
    simdscalar hihi = _simd_permute_ps(a.hi, _simd_and_si(i.hi, mask));

    result.lo = _simd_blendv_ps(lolo, lohi, _simd_castsi_ps(_simd_cmpgt_epi32(i.lo, mask)));
    result.hi = _simd_blendv_ps(hilo, hihi, _simd_castsi_ps(_simd_cmpgt_epi32(i.hi, mask)));

    return result;
}

INLINE simd16scalari _simd16_permute_epi32(simd16scalari a, simd16scalari i)
{
    return _simd16_castps_si(_simd16_permute_ps(_simd16_castsi_ps(a), i));
}

SIMD16_EMU_AVX512_2(simd16scalari, _simd16_srlv_epi32, _simd_srlv_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_sllv_epi32, _simd_sllv_epi32)

template <int imm8>
INLINE simd16scalar _simd16_permute2f128_ps_temp(simd16scalar a, simd16scalar b)
{
    simd16scalar result;

    result.lo = _simd_permute2f128_ps(a.lo, a.hi, ((imm8 & 0x03) << 0) | ((imm8 & 0x0C) << 2));
    result.hi = _simd_permute2f128_ps(b.lo, b.hi, ((imm8 & 0x30) >> 4) | ((imm8 & 0xC0) >> 2));

    return result;
}

#define _simd16_permute2f128_ps(a, b, imm8) _simd16_permute2f128_ps_temp<imm8>(a, b)

template <int imm8>
INLINE simd16scalard _simd16_permute2f128_pd_temp(simd16scalard a, simd16scalard b)
{
    simd16scalard result;

    result.lo = _simd_permute2f128_pd(a.lo, a.hi, ((imm8 & 0x03) << 0) | ((imm8 & 0x0C) << 2));
    result.hi = _simd_permute2f128_pd(b.lo, b.hi, ((imm8 & 0x30) >> 4) | ((imm8 & 0xC0) >> 2));

    return result;
}

#define _simd16_permute2f128_pd(a, b, imm8) _simd16_permute2f128_pd_temp<imm8>(a, b)

template <int imm8>
INLINE simd16scalari _simd16_permute2f128_si_temp(simd16scalari a, simd16scalari b)
{
    simd16scalari result;

    result.lo = _simd_permute2f128_si(a.lo, a.hi, ((imm8 & 0x03) << 0) | ((imm8 & 0x0C) << 2));
    result.hi = _simd_permute2f128_si(b.lo, b.hi, ((imm8 & 0x30) >> 4) | ((imm8 & 0xC0) >> 2));

    return result;
}

#define _simd16_permute2f128_si(a, b, imm8) _simd16_permute2f128_si_temp<imm8>(a, b)

template <int imm8>
INLINE simd16scalar _simd16_shuffle_ps_temp(simd16scalar a, simd16scalar b)
{
    simd16scalar result;

    result.lo = _simd_shuffle_ps(a.lo, b.lo, imm8);
    result.hi = _simd_shuffle_ps(a.hi, b.hi, imm8);

    return result;
}

#define _simd16_shuffle_ps(a, b, imm8) _simd16_shuffle_ps_temp<imm8>(a, b)

template <int imm8>
INLINE simd16scalard _simd16_shuffle_pd_temp(simd16scalard a, simd16scalard b)
{
    simd16scalard result;

    result.lo = _simd_shuffle_pd(a.lo, b.lo, (imm8 & 15));
    result.hi = _simd_shuffle_pd(a.hi, b.hi, (imm8 >> 4));

    return result;
}

#define _simd16_shuffle_pd(a, b, imm8) _simd16_shuffle_pd_temp<imm8>(a, b)

template <int imm8>
INLINE simd16scalari _simd16_shuffle_epi32_temp(simd16scalari a, simd16scalari b)
{
    return _simd16_castps_si(_simd16_shuffle_ps(_simd16_castsi_ps(a), _simd16_castsi_ps(b), imm8));
}

#define _simd16_shuffle_epi32(a, b, imm8) _simd16_shuffle_epi32_temp<imm8>(a, b)

template <int imm8>
INLINE simd16scalari _simd16_shuffle_epi64_temp(simd16scalari a, simd16scalari b)
{
    return _simd16_castpd_si(_simd16_shuffle_pd(_simd16_castsi_pd(a), _simd16_castsi_pd(b), imm8));
}

#define _simd16_shuffle_epi64(a, b, imm8) _simd16_shuffle_epi64_temp<imm8>(a, b)

INLINE simd16scalari _simd16_cvtepu8_epi16(simdscalari a)
{
    simd16scalari result;

    result.lo = _simd_cvtepu8_epi16(_mm256_extractf128_si256(a, 0));
    result.hi = _simd_cvtepu8_epi16(_mm256_extractf128_si256(a, 1));

    return result;
}

INLINE simd16scalari _simd16_cvtepu8_epi32(__m128i a)
{
    simd16scalari result;

    result.lo = _simd_cvtepu8_epi32(a);
    result.hi = _simd_cvtepu8_epi32(_mm_srli_si128(a, 8));

    return result;
}

INLINE simd16scalari _simd16_cvtepu16_epi32(simdscalari a)
{
    simd16scalari result;

    result.lo = _simd_cvtepu16_epi32(_mm256_extractf128_si256(a, 0));
    result.hi = _simd_cvtepu16_epi32(_mm256_extractf128_si256(a, 1));

    return result;
}

SIMD16_EMU_AVX512_2(simd16scalari, _simd16_packus_epi16, _simd_packus_epi16)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_packs_epi16, _simd_packs_epi16)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_packus_epi32, _simd_packus_epi32)
SIMD16_EMU_AVX512_2(simd16scalari, _simd16_packs_epi32, _simd_packs_epi32)

INLINE simd16mask _simd16_int2mask(int mask)
{
    return mask;
}

INLINE int _simd16_mask2int(simd16mask mask)
{
    return mask;
}

INLINE simd16mask _simd16_cmplt_ps_mask(simd16scalar a, simd16scalar b)
{
    return _simd16_movemask_ps(_simd16_cmplt_ps(a, b));
}

// convert bitmask to vector mask
INLINE simd16scalar vMask16(int32_t mask)
{
    simd16scalari temp = _simd16_set1_epi32(mask);

    simd16scalari bits = _simd16_set_epi32(0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001);

    simd16scalari result = _simd16_cmplt_epi32(_simd16_setzero_si(), _simd16_and_si(temp, bits));

    return _simd16_castsi_ps(result);
}

#else

INLINE simd16mask _simd16_scalari2mask(simd16scalari mask)
{
    return _mm512_cmpneq_epu32_mask(mask, _mm512_setzero_epi32());
}

#if 0
INLINE simd16mask _simd16_scalard2mask(simd16scalard mask)
{
    return _mm512_cmpneq_epu64_mask(mask, _mm512_setzero_epi64());
}
#endif

#define _simd16_setzero_ps      _mm512_setzero_ps
#define _simd16_setzero_si      _mm512_setzero_si512
#define _simd16_set1_ps         _mm512_set1_ps
#define _simd16_set1_epi8       _mm512_set1_epi8
#define _simd16_set1_epi32      _mm512_set1_epi32

INLINE simd16scalar _simd16_set_ps(float e15, float e14, float e13, float e12, float e11, float e10, float e9, float e8, float e7, float e6, float e5, float e4, float e3, float e2, float e1, float e0)
{
    return _mm512_set_ps(e15, e14, e13, e12, e11, e10, e9, e8, e7, e6, e5, e4, e3, e2, e1, e0);
}

INLINE simd16scalari _simd16_set_epi32(int e15, int e14, int e13, int e12, int e11, int e10, int e9, int e8, int e7, int e6, int e5, int e4, int e3, int e2, int e1, int e0)
{
    return _mm512_set_epi32(e15, e14, e13, e12, e11, e10, e9, e8, e7, e6, e5, e4, e3, e2, e1, e0);
}

INLINE simd16scalar _simd16_set_ps(float e7, float e6, float e5, float e4, float e3, float e2, float e1, float e0)
{
    return _mm512_set_ps(e7, e6, e5, e4, e3, e2, e1, e0, e7, e6, e5, e4, e3, e2, e1, e0);
}

INLINE simd16scalari _simd16_set_epi32(int e7, int e6, int e5, int e4, int e3, int e2, int e1, int e0)
{
    return _mm512_set_epi32(e7, e6, e5, e4, e3, e2, e1, e0, e7, e6, e5, e4, e3, e2, e1, e0);
}

#define _simd16_load_ps         _mm512_load_ps
#define _simd16_loadu_ps        _mm512_loadu_ps
#if 1
#define _simd16_load1_ps        _simd16_broadcast_ss
#endif
#define _simd16_load_si         _mm512_load_si512
#define _simd16_loadu_si        _mm512_loadu_si512
#define _simd16_broadcast_ss(m) _mm512_extload_ps(m, _MM_UPCONV_PS_NONE, _MM_BROADCAST_1X16, 0)
#define _simd16_broadcast_ps(m) _mm512_extload_ps(m, _MM_UPCONV_PS_NONE, _MM_BROADCAST_4X16, 0)
#define _simd16_store_ps        _mm512_store_ps
#define _simd16_store_si        _mm512_store_si512
#define _simd16_extract_ps      _mm512_extractf32x8_ps
#define _simd16_extract_si      _mm512_extracti32x8_epi32
#define _simd16_insert_ps       _mm512_insertf32x8
#define _simd16_insert_si       _mm512_inserti32x8

INLINE void _simd16_maskstore_ps(float *m, simd16scalari mask, simd16scalar a)
{
    simd16mask k = _simd16_scalari2mask(mask);

    _mm512_mask_store_ps(m, k, a);
}

#define _simd16_blend_ps(a, b, mask)    _mm512_mask_blend_ps(mask, a, b)

INLINE simd16scalar _simd16_blendv_ps(simd16scalar a, simd16scalar b, const simd16scalar mask)
{
    simd16mask k = _simd16_scalari2mask(_mm512_castps_si512(mask));

    _mm512_mask_blend_ps(k, a, b);
}

INLINE simd16scalari _simd16_blendv_epi32(simd16scalari a, simd16scalari b, const simd16scalar mask)
{
    simd16mask k = _simd16_scalari2mask(_mm512_castps_si512(mask));

    _mm512_mask_blend_epi32(k, a, b);
}

INLINE simd16scalari _simd16_blendv_epi32(simd16scalari a, simd16scalari b, const simd16scalari mask)
{
    simd16mask k = _simd16_scalari2mask(mask);

    _mm512_mask_blend_epi32(k, a, b);
}

#define _simd16_mul_ps          _mm512_mul_ps
#define _simd16_add_ps          _mm512_add_ps
#define _simd16_sub_ps          _mm512_sub_ps
#define _simd16_rsqrt_ps        _mm512_rsqrt14_ps
#define _simd16_min_ps          _mm512_min_ps
#define _simd16_max_ps          _mm512_max_ps

INLINE simd16mask _simd16_movemask_ps(simd16scalar a)
{
    return  _simd16_scalari2mask(_mm512_castps_si512(a));
}

#if 0
INLINE simd16mask _simd16_movemask_pd(simd16scalard a)
{
    return  _simd16_scalard2mask(_mm512i_castpd_si512(a));
}
#endif

#if 0
INLINE int _simd16_movemask_epi8(simd16scalari a)
{
    return  _simd16_scalar2mask(a);
}
#endif

#define _simd16_cvtps_epi32     _mm512_cvtps_epi32
#define _simd16_cvttps_epi32    _mm512_cvttps_epi32
#define _simd16_cvtepi32_ps     _mm512_cvtepi32_ps

template <int comp>
INLINE simd16scalar _simd16_cmp_ps_temp(simd16scalar a, simd16scalar b)
{
    simd16mask k = _mm512_cmpeq_ps_mask(a, b);

    return _mm512_castsi512_ps(_mm512_mask_blend_epi32(k, _mm512_setzero_epi32(), _mm512_set1_epi32(0xFFFFFFFF)));
}

#define _simd16_cmp_ps(a, b, comp)  _simd16_cmp_ps_temp<comp>(a, b)

#define _simd16_cmplt_ps(a, b)      _simd16_cmp_ps<_CMP_LT_OQ>(a, b)
#define _simd16_cmpgt_ps(a, b)      _simd16_cmp_ps<_CMP_GT_OQ>(a, b)
#define _simd16_cmpneq_ps(a, b)     _simd16_cmp_ps<_CMP_NEQ_OQ>(a, b)
#define _simd16_cmpeq_ps(a, b)      _simd16_cmp_ps<_CMP_EQ_OQ>(a, b)
#define _simd16_cmpge_ps(a, b)      _simd16_cmp_ps<_CMP_GE_OQ>(a, b)
#define _simd16_cmple_ps(a, b)      _simd16_cmp_ps<_CMP_LE_OQ>(a, b)

#define _simd16_castsi_ps           _mm512_castsi512_ps
#define _simd16_castps_si           _mm512_castps_si512
#define _simd16_castsi_pd           _mm512_castsi512_pd
#define _simd16_castpd_si           _mm512_castpd_si512
#define _simd16_castpd_ps           _mm512_castpd_ps
#define _simd16_castps_pd           _mm512_castps_pd

#define _simd16_andnot_ps           _mm512_andnot_ps

template <int mode>
INLINE simd16scalar _simd16_round_ps_temp(simd16scalar a)
{
    return _mm512_roundscale_ps(a, mode);
}

#define _simd16_round_ps(a, mode) _simd16_round_ps_temp<mode>(a)

#define _simd16_mul_epi32         _mm512_mul_epi32
#define _simd16_mullo_epi32       _mm512_mullo_epi32
#define _simd16_sub_epi32         _mm512_sub_epi32
#define _simd16_sub_epi64         _mm512_sub_epi64
#define _simd16_min_epi32         _mm512_min_epi32
#define _simd16_max_epi32         _mm512_max_epi32
#define _simd16_min_epu32         _mm512_min_epu32
#define _simd16_max_epu32         _mm512_max_epu32
#define _simd16_add_epi32         _mm512_add_epi32
#define _simd16_and_si            _mm512_and_si512
#define _simd16_andnot_si         _mm512_andnot_si512
#define _simd16_or_si             _mm512_or_si512
#define _simd16_xor_si            _mm512_xor_si512

INLINE simd16scalari _simd16_cmpeq_epi32(simd16scalari a, simd16scalari b)
{
    simd16mask k = _mm512_cmpeq_epi32_mask(a, b);

    return _mm512_mask_blend_epi32(k, _mm512_setzero_epi32(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE simd16scalari _simd16_cmpgt_epi32(simd16scalari a, simd16scalari b)
{
    simd16mask k = _mm512_cmpgt_epi32_mask(a, b);

    return _mm512_mask_blend_epi32(k, _mm512_setzero_epi32(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE simd16scalari _simd16_cmplt_epi32(simd16scalari a, simd16scalari b)
{
    simd16mask k = _mm512_cmplt_epi32_mask(a, b);

    return _mm512_mask_blend_epi32(k, _mm512_setzero_epi32(), _mm512_set1_epi32(0xFFFFFFFF));
}

#if 0
INLINE int _simd16_testz_ps(simd16scalar a, simd16scalar b)
{
    int lo = _mm256_testz_ps(a.lo, b.lo);
    int hi = _mm256_testz_ps(a.hi, b.hi);

    return lo & hi;
}

#endif

#define _simd16_unpacklo_ps       _mm512_unpacklo_ps
#define _simd16_unpackhi_ps       _mm512_unpackhi_ps
#define _simd16_unpacklo_pd       _mm512_unpacklo_pd
#define _simd16_unpackhi_pd       _mm512_unpackhi_pd
#define _simd16_unpacklo_epi8     _mm512_unpacklo_epi8
#define _simd16_unpackhi_epi8     _mm512_unpackhi_epi8
#define _simd16_unpacklo_epi16    _mm512_unpacklo_epi16
#define _simd16_unpackhi_epi16    _mm512_unpackhi_epi16
#define _simd16_unpacklo_epi32    _mm512_unpacklo_epi32
#define _simd16_unpackhi_epi32    _mm512_unpackhi_epi32
#define _simd16_unpacklo_epi64    _mm512_unpacklo_epi64
#define _simd16_unpackhi_epi64    _mm512_unpackhi_epi64
#define _simd16_slli_epi32        _mm512_slli_epi32
#define _simd16_srli_epi32        _mm512_srli_epi32
#define _simd16_srai_epi32        _mm512_srai_epi32
#define _simd16_fmadd_ps          _mm512_fmadd_ps
#define _simd16_fmsub_ps          _mm512_fmsub_ps
#define _simd16_adds_epu8         _mm512_adds_epu8
#define _simd16_subs_epu8         _mm512_subs_epu8
#define _simd16_add_epi8          _mm512_add_epi8
#define _simd16_shuffle_epi8      _mm512_shuffle_epi8

#define _simd16_fmadd_ps          _mm512_fmadd_ps
#define _simd16_fmsub_ps          _mm512_fmsub_ps

#define _simd16_i32gather_ps(m, index, scale)               _mm512_i32gather_ps(index, m, scale)
#define _simd16_mask_i32gather_ps(a, m, index, mask, scale) _mm512_mask_i32gather_ps(a, m, index, mask, scale)

#define _simd16_abs_epi32         _mm512_abs_epi32
#define _simd16_cmpeq_epi64       _mm512_abs_epi32

INLINE simd16scalari _simd16_cmpeq_epi64(simd16scalari a, simd16scalari b)
{
    __mmask8 k = _mm512_cmpeq_epi64_mask(a, b);

    return _mm512_mask_blend_epi64(k, _mm512_setzero_si512(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE simd16scalari _simd16_cmpgt_epi64(simd16scalari a, simd16scalari b)
{
    __mmask8 k = _mm512_cmpgt_epi64_mask(a, b);

    return _mm512_mask_blend_epi64(k, _mm512_setzero_si512(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE simd16scalari _simd16_cmpeq_epi16(simd16scalari a, simd16scalari b)
{
    __mmask32 k = _mm512_cmpeq_epi16_mask(a, b);

    return _mm512_mask_blend_epi16(k, _mm512_setzero_si512(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE simd16scalari _simd16_cmpgt_epi16(simd16scalari a, simd16scalari b)
{
    __mmask32 k = _mm512_cmpgt_epi16_mask(a, b);

    return _mm512_mask_blend_epi16(k, _mm512_setzero_si512(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE simd16scalari _simd16_cmpeq_epi8(simd16scalari a, simd16scalari b)
{
    __mmask64 k = _mm512_cmpeq_epi8_mask(a, b);

    return _mm512_mask_blend_epi8(k, _mm512_setzero_si512(), _mm512_set1_epi32(0xFFFFFFFF));
}

INLINE simd16scalari _simd16_cmpgt_epi8(simd16scalari a, simd16scalari b)
{
    __mmask64 k = _mm512_cmpgt_epi8_mask(a, b);

    return _mm512_mask_blend_epi8(k, _mm512_setzero_si512(), _mm512_set1_epi32(0xFFFFFFFF));
}

#define _simd16_permute_ps(a, i)        _mm512_permutexvar_ps(i, a)
#define _simd16_permute_epi32(a, i)     _mm512_permutexvar_epi32(i, a)
#define _simd16_sllv_epi32              _mm512_srlv_epi32
#define _simd16_srlv_epi32              _mm512_sllv_epi32
#define _simd16_permute2f128_ps         _mm512_shuffle_f32x4
#define _simd16_permute2f128_pd         _mm512_shuffle_f64x2
#define _simd16_permute2f128_si         _mm512_shuffle_i32x4
#define _simd16_shuffle_ps              _mm512_shuffle_ps
#define _simd16_shuffle_pd              _mm512_shuffle_pd
#define _simd16_cvtepu8_epi16           _mm512_cvtepu8_epi16
#define _simd16_cvtepu8_epi32           _mm512_cvtepu8_epi32
#define _simd16_cvtepu16_epi32          _mm512_cvtepu16_epi32
#define _simd16_packus_epi16            _mm512_packus_epi16
#define _simd16_packs_epi16             _mm512_packs_epi16
#define _simd16_packus_epi32            _mm512_packus_epi32
#define _simd16_packs_epi32             _mm512_packs_epi32

template <int imm8>
INLINE simd16scalari _simd16_shuffle_epi32_temp(simd16scalari a, simd16scalari b)
{
    return _simd16_castps_si(_simd16_shuffle_ps(_simd16_castsi_ps(a), _simd16_castsi_ps(b), imm8));
}

#define _simd16_shuffle_epi32(a, b, imm8) _simd16_shuffle_epi32_temp<imm8>(a, b)

template <int imm8>
INLINE simd16scalari _simd16_shuffle_epi64_temp(simd16scalari a, simd16scalari b)
{
    return _simd16_castpd_si(_simd16_shuffle_pd(_simd16_castsi_pd(a), _simd16_castsi_pd(b), imm8));
}

#define _simd16_shuffle_epi64(a, b, imm8) _simd16_shuffle_epi64_temp<imm8>(a, b)

INLINE simd16mask _simd16_int2mask(int mask)
{
    return _mm512_int2mask(mask);
}

INLINE int _simd16_mask2int(simd16mask mask)
{
    return _mm512_mask2int(mask);
}

INLINE simd16mask _simd16_cmplt_ps_mask(simd16scalar a, simd16scalar b)
{
    return _mm512_cmplt_ps_mask(a, b);
}

// convert bitmask to vector mask
INLINE simd16scalar vMask16(int32_t mask)
{
    simd16scalari temp = _simd16_set1_epi32(mask);

    simd16scalari bits = _simd16_set_epi32(0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001);

    simd16scalari result = _simd16_cmplt_epi32(_simd16_setzero_si(), _simd16_and_si(temp, bits));

    return _simd16_castsi_ps(result);
}

#endif//ENABLE_AVX512_EMULATION

#endif//ENABLE_AVX512_SIMD16

#endif//__SWR_SIMD16INTRIN_H_
