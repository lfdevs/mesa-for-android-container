/*
************************************************************************************************************************
*
*  Copyright (C) 2024-2026 Advanced Micro Devices, Inc.  All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/
/**
****************************************************************************************************
* @file  addrswizzlersimd.h
* @brief Contains CPU/swizzle-specific code for efficient CPU swizzling.
****************************************************************************************************
*/

#ifndef __ADDR_SWIZZLER_SIMD_H__
#define __ADDR_SWIZZLER_SIMD_H__

#include "addrswizzler.h"
#include "addrcommon.h"

#if !ADDR_ALLOW_SIMD
// Disabled
#define ADDR_HAS_AVX2 0
#define AVX2_FUNC
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
// x86 visual studio builds
#define ADDR_HAS_AVX2 1
#define AVX2_FUNC
#elif defined(__x86_64__) || defined(__i386__)
// x86 GCC/Clang builds
#define ADDR_HAS_AVX2 1
#define AVX2_FUNC [[gnu::target("avx2")]]
#else
// Unknown
#define ADDR_HAS_AVX2 0
#define AVX2_FUNC
#endif

#if !ADDR_ALLOW_SIMD
// Disabled
#define ADDR_HAS_NEON 0
#define NEON_FUNC
#elif defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM))
// arm visual studio builds
#define ADDR_HAS_NEON 1
#define NEON_FUNC
#elif (defined(__linux__) || defined(_WIN32)) && (defined(__aarch64__) || (defined(__arm__) && defined(__ARM_FP)))
// arm GCC/Clang builds on windows or linux
#define ADDR_HAS_NEON 1
#define NEON_FUNC
#else
// Unknown
#define ADDR_HAS_NEON 0
#define NEON_FUNC
#endif

#if ADDR_HAS_AVX2
#if _MSC_VER
#include <Windows.h>
#endif
#include <immintrin.h>

// Certain compiler versions lack this intrinsic.
#if (defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 8) || (_MSC_VER && !defined(_mm256_set_m128i))
#define _mm256_set_m128i(hi, lo) _mm256_inserti128_si256(_mm256_castsi128_si256(lo), (hi), 1)
#endif
#endif

#if ADDR_HAS_NEON
#if _WIN32
#include <immintrin.h>
#else
#include <arm_neon.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>
#endif
#endif

namespace Addr
{

#if ADDR_HAS_AVX2
static inline bool CpuSupportsAvx2() {
    // Use compiler builtins to check for support
#if _MSC_VER
    return IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE);
#elif defined(__GNUC__)
    return __builtin_cpu_supports("avx2");
#else
#error "What platform is this?"
#endif
}
#endif

#if ADDR_HAS_NEON
static inline bool CpuSupportsNeon() {
    // ARM can't check this without OS help. Use OS knowledge and helpers to check for support.
#if _WIN32
    return true; // Mandatory for WoA
#elif defined(__linux__) && defined(__aarch64__)
    return ((getauxval(AT_HWCAP) & HWCAP_ASIMD) != 0);
#elif defined(__linux__) && defined(__arm__)
    return ((getauxval(AT_HWCAP) & HWCAP_NEON) != 0);
#else
#error "What platform is this?"
#endif
}
#endif

constexpr UINT_64 GetMicroSwKey(const UINT_64* pEq, bool isPlanarMsaa = false)
{
    UINT_64 out = 0;

    // Microswizzles never have move than 1 xor, so just use the log2 of each bit (6 bits each = 48 bits)
    for (UINT_32 i = 0; i < 8; i++)
    {
        if (pEq[i] != 0)
        {
            out |= (static_cast<UINT_64>(ConstexprLog2(pEq[i]) + 1) << (i * 6));
        }
    }

    // Bits 48+ can be other things. The big one is if this is actually MSAA but the sample bits are in the
    // high part of the equation.
    if (isPlanarMsaa)
    {
        out |= (static_cast<UINT_64>(1) << 48);
    }

    return out;
}

#if ADDR_HAS_AVX2
// Ensures all non-temporal/stream stores have completed.
AVX2_FUNC static inline void NonTemporalStoreFence()
{
    _mm_sfence();
}

// Ensures all non-temporal/stream loads and stores have completed.
AVX2_FUNC static inline void NonTemporalLoadStoreFence()
{
    _mm_mfence();
}

AVX2_FUNC static inline void StreamCopyToImgAligned(
    void*       pImg, // Memory to write to, must be 256B aligned.
    const void* pBuf, // Memory to read from, can be unaligned.
    size_t      size) // Bytes to copy, must be 256B aligned.
{
    __m256i*       pAlignedOut  = reinterpret_cast<__m256i*>(pImg);
    const __m256i* pUnalignedIn = reinterpret_cast<const __m256i*>(pBuf);
    ADDR_ASSERT(PowTwoAlign(uint64_t(size), 256ULL) == uint64_t(size));
    while (size > 0)
    {
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        _mm256_stream_si256(pAlignedOut++, _mm256_loadu_si256(pUnalignedIn++));
        
        size -= 256;
    }
}

AVX2_FUNC static inline void StreamCopyFromImgAligned(
    void*       pBuf, // Memory to write to, can be unaligned.
    const void* pImg, // Memory to read from, must be 256B aligned.
    size_t      size) // Bytes to copy, must be 256B aligned.
{
    __m256i*       pUnalignedOut = reinterpret_cast<__m256i*>(pBuf);
    const __m256i* pAlignedIn    = reinterpret_cast<const __m256i*>(pImg);
    ADDR_ASSERT(PowTwoAlign(uint64_t(size), 256ULL) == uint64_t(size));
    while (size > 0)
    {
        // Use streaming loads to optimize memory behavior-- this requires aligned memory.
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        _mm256_storeu_si256(pUnalignedOut++, _mm256_stream_load_si256(pAlignedIn++));
        
        size -= 256;
    }
}

class MicroSw_2D_1BPE_AVX2
{
    MicroSw_2D_1BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, Y0, X2, Y1, Y2, X3, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 4;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Yes, that means double the load instructions
        // Each reg becomes: [ X3 X2 X1 X0 ]
        __m128i y0  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 0));
        __m128i y1  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 1));
        __m128i y2  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 2));
        __m128i y3  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 3));
        __m128i y4  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 4));
        __m128i y5  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 5));
        __m128i y6  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 6));
        __m128i y7  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 7));
        __m128i y8  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 8));
        __m128i y9  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 9));
        __m128i y10 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 10));
        __m128i y11 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 11));
        __m128i y12 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 12));
        __m128i y13 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 13));
        __m128i y14 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 14));
        __m128i y15 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 15));

        // First, concat the SSE regs so they end up in pairs that will unpack well
        // Each reg becomes: [ Y1 X3 X2 X1 X0 ]
        __m256i m1_0 = _mm256_set_m128i(y2, y0);
        __m256i m1_1 = _mm256_set_m128i(y3, y1);
        __m256i m1_2 = _mm256_set_m128i(y6, y4);
        __m256i m1_3 = _mm256_set_m128i(y7, y5);
        __m256i m1_4 = _mm256_set_m128i(y10, y8);
        __m256i m1_5 = _mm256_set_m128i(y11, y9);
        __m256i m1_6 = _mm256_set_m128i(y14, y12);
        __m256i m1_7 = _mm256_set_m128i(y15, y13);

        // Unpack to handle the rest of the swizzling within each reg
        // Each reg becomes: [ Y1 X2 Y0 X1 X0 ]
        __m256i m2_0 = _mm256_unpacklo_epi32(m1_0, m1_1);
        __m256i m2_1 = _mm256_unpackhi_epi32(m1_0, m1_1);
        __m256i m2_2 = _mm256_unpacklo_epi32(m1_2, m1_3);
        __m256i m2_3 = _mm256_unpackhi_epi32(m1_2, m1_3);
        __m256i m2_4 = _mm256_unpacklo_epi32(m1_4, m1_5);
        __m256i m2_5 = _mm256_unpackhi_epi32(m1_4, m1_5);
        __m256i m2_6 = _mm256_unpacklo_epi32(m1_6, m1_7);
        __m256i m2_7 = _mm256_unpackhi_epi32(m1_6, m1_7);

        // Move each reg around to handle high bit swizzling
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, m2_0);
        _mm256_stream_si256(pAlignedOut++, m2_2);
        _mm256_stream_si256(pAlignedOut++, m2_1);
        _mm256_stream_si256(pAlignedOut++, m2_3);
        _mm256_stream_si256(pAlignedOut++, m2_4);
        _mm256_stream_si256(pAlignedOut++, m2_6);
        _mm256_stream_si256(pAlignedOut++, m2_5);
        _mm256_stream_si256(pAlignedOut++, m2_7);
    }
};


class MicroSw_2D_2BPE_AVX2
{
    MicroSw_2D_2BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, X0, Y0, X1, Y1, X2, Y2, X3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 8, 1};
    static constexpr UINT_32       BpeLog2 = 1;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (16 col * 16b = 256b)
        // Each reg becomes: [ X3 X2 X1 X0 0 ]
        __m256i y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 0)));
        __m256i y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 1)));
        __m256i y2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 2)));
        __m256i y3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 3)));
        __m256i y4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 4)));
        __m256i y5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 5)));
        __m256i y6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 6)));
        __m256i y7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 7)));

        // Do a 32-bit zip/unpack operation to interleave within each 128b reg.
        // Each reg becomes: [ X2 X1 Y0 X0 0 ]
        __m256i m0_0 = _mm256_unpacklo_epi32(y0, y1);
        __m256i m0_1 = _mm256_unpackhi_epi32(y0, y1);
        __m256i m0_2 = _mm256_unpacklo_epi32(y2, y3);
        __m256i m0_3 = _mm256_unpackhi_epi32(y2, y3);
        __m256i m0_4 = _mm256_unpacklo_epi32(y4, y5);
        __m256i m0_5 = _mm256_unpackhi_epi32(y4, y5);
        __m256i m0_6 = _mm256_unpacklo_epi32(y6, y7);
        __m256i m0_7 = _mm256_unpackhi_epi32(y6, y7);

        // Then use a cross-lane dual permute to do a 128b interleave across y1
        // Each reg becomes: [ Y1 X1 Y0 X0 0 ]
        __m256i m1_0 = _mm256_permute2x128_si256(m0_0, m0_2, 0x20);
        __m256i m1_1 = _mm256_permute2x128_si256(m0_1, m0_3, 0x20);
        __m256i m1_2 = _mm256_permute2x128_si256(m0_4, m0_6, 0x20);
        __m256i m1_3 = _mm256_permute2x128_si256(m0_5, m0_7, 0x20);
        __m256i m1_4 = _mm256_permute2x128_si256(m0_0, m0_2, 0x31);
        __m256i m1_5 = _mm256_permute2x128_si256(m0_1, m0_3, 0x31);
        __m256i m1_6 = _mm256_permute2x128_si256(m0_4, m0_6, 0x31);
        __m256i m1_7 = _mm256_permute2x128_si256(m0_5, m0_7, 0x31);

        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, m1_0);
        _mm256_stream_si256(pAlignedOut++, m1_1);
        _mm256_stream_si256(pAlignedOut++, m1_2);
        _mm256_stream_si256(pAlignedOut++, m1_3);
        _mm256_stream_si256(pAlignedOut++, m1_4);
        _mm256_stream_si256(pAlignedOut++, m1_5);
        _mm256_stream_si256(pAlignedOut++, m1_6);
        _mm256_stream_si256(pAlignedOut++, m1_7);
    }
};


class MicroSw_2D_4BPE_AVX2
{
    MicroSw_2D_4BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, X0, Y0, X1, Y1, X2, Y2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = { 8, 8, 1};
    static constexpr UINT_32       BpeLog2 = 2;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (8 col * 32b = 256b)
        // Each reg becomes: [ X2 X1 X0 0 0 ]
        __m256i y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 0)));
        __m256i y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 1)));
        __m256i y2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 2)));
        __m256i y3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 3)));
        __m256i y4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 4)));
        __m256i y5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 5)));
        __m256i y6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 6)));
        __m256i y7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 7)));

        // In-register shuffling on the bottom 5 bits of which the bottom 3 are already right: (X1, Y0, *, *, *)
        // Start: y0/y1: [ 0 1 2 3 ] [ 4 5 6 7 ] (QWORDS)
        // Desired:      [ 0 4 1 5 ] [ 2 6 3 7 ]

        // Do a permute to reorder each register to have all values in the correct 128b lanes.
        // Swap 2/3 -> mask 0b11_01_10_00 -> 0xD8
        // Result:       [ 0 2 1 3 ] [ 4 6 5 7 ]
        // Each reg becomes: [ X1 X2 X0 0 0 ]
        __m256i perm0 = _mm256_permute4x64_epi64(y0, 0xD8);
        __m256i perm1 = _mm256_permute4x64_epi64(y1, 0xD8);
        __m256i perm2 = _mm256_permute4x64_epi64(y2, 0xD8);
        __m256i perm3 = _mm256_permute4x64_epi64(y3, 0xD8);
        __m256i perm4 = _mm256_permute4x64_epi64(y4, 0xD8);
        __m256i perm5 = _mm256_permute4x64_epi64(y5, 0xD8);
        __m256i perm6 = _mm256_permute4x64_epi64(y6, 0xD8);
        __m256i perm7 = _mm256_permute4x64_epi64(y7, 0xD8);

        // Then use unpack intrinsics to interleave two regs (within those lanes), which leaves it in the final place
        // Result:       [ 0 4 1 5 ] [ 2 6 3 7]
        // Each reg becomes: [ X1 Y0 X0 0 0 ]
        __m256i unpack0 = _mm256_unpacklo_epi64(perm0, perm1);
        __m256i unpack1 = _mm256_unpackhi_epi64(perm0, perm1);
        __m256i unpack2 = _mm256_unpacklo_epi64(perm2, perm3);
        __m256i unpack3 = _mm256_unpackhi_epi64(perm2, perm3);
        __m256i unpack4 = _mm256_unpacklo_epi64(perm4, perm5);
        __m256i unpack5 = _mm256_unpackhi_epi64(perm4, perm5);
        __m256i unpack6 = _mm256_unpacklo_epi64(perm6, perm7);
        __m256i unpack7 = _mm256_unpackhi_epi64(perm6, perm7);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, unpack0);
        _mm256_stream_si256(pAlignedOut++, unpack2);
        _mm256_stream_si256(pAlignedOut++, unpack1);
        _mm256_stream_si256(pAlignedOut++, unpack3);
        _mm256_stream_si256(pAlignedOut++, unpack4);
        _mm256_stream_si256(pAlignedOut++, unpack6);
        _mm256_stream_si256(pAlignedOut++, unpack5);
        _mm256_stream_si256(pAlignedOut++, unpack7);
    }
};

class MicroSw_2D_8BPE_AVX2
{
    MicroSw_2D_8BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, X0, Y0, X1, X2, Y1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {8, 4, 1};
    static constexpr UINT_32       BpeLog2 = 3;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 rows * (8 col * 64b = 256bx2)
        // Each reg becomes: [ X1 X0 0 0 0 ]
        __m256i y0a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 0) + 0)));
        __m256i y0b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 0) + 32)));
        __m256i y1a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 1) + 0)));
        __m256i y1b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 1) + 32)));
        __m256i y2a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 2) + 0)));
        __m256i y2b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 2) + 32)));
        __m256i y3a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 3) + 0)));
        __m256i y3b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 3) + 32)));

        // In-register shuffling on the bottom 5 bits of which the bottom 4 are already right: (Y0, *, *, *, *)
        // Start: y0a/y1a: [ 0 1 ] [ 2 3 ] (128b)
        // Desired:        [ 0 2 ] [ 1 3 ]
        // The magic mask value lines up with the numbers above in hex, so 0x20 means [ 0 2 ]
        // Each reg becomes: [ Y0 X0 0 0 0 ]
        __m256i perm0 = _mm256_permute2x128_si256(y0a, y1a, 0x20);
        __m256i perm1 = _mm256_permute2x128_si256(y0a, y1a, 0x31);
        __m256i perm2 = _mm256_permute2x128_si256(y2a, y3a, 0x20);
        __m256i perm3 = _mm256_permute2x128_si256(y2a, y3a, 0x31);
        __m256i perm4 = _mm256_permute2x128_si256(y0b, y1b, 0x20);
        __m256i perm5 = _mm256_permute2x128_si256(y0b, y1b, 0x31);
        __m256i perm6 = _mm256_permute2x128_si256(y2b, y3b, 0x20);
        __m256i perm7 = _mm256_permute2x128_si256(y2b, y3b, 0x31);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, perm0);
        _mm256_stream_si256(pAlignedOut++, perm1);
        _mm256_stream_si256(pAlignedOut++, perm4);
        _mm256_stream_si256(pAlignedOut++, perm5);
        _mm256_stream_si256(pAlignedOut++, perm2);
        _mm256_stream_si256(pAlignedOut++, perm3);
        _mm256_stream_si256(pAlignedOut++, perm6);
        _mm256_stream_si256(pAlignedOut++, perm7);
    }
};

class MicroSw_2D_16BPE_AVX2
{
    MicroSw_2D_16BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, 0, X0, Y0, X1, Y1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 4, 1};
    static constexpr UINT_32       BpeLog2 = 4;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 rows * (4 col * 128b = 256bx2)
        __m256i y0a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 0) + 0)));
        __m256i y0b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 0) + 32)));
        __m256i y1a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 1) + 0)));
        __m256i y1b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 1) + 32)));
        __m256i y2a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 2) + 0)));
        __m256i y2b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 2) + 32)));
        __m256i y3a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 3) + 0)));
        __m256i y3b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideY * 3) + 32)));

        // The top 3 bits of the swizzle are handled by the order of the registers here. The rest are already right.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, y0a);
        _mm256_stream_si256(pAlignedOut++, y1a);
        _mm256_stream_si256(pAlignedOut++, y0b);
        _mm256_stream_si256(pAlignedOut++, y1b);
        _mm256_stream_si256(pAlignedOut++, y2a);
        _mm256_stream_si256(pAlignedOut++, y3a);
        _mm256_stream_si256(pAlignedOut++, y2b);
        _mm256_stream_si256(pAlignedOut++, y3b);
    }
};


class MicroSw_3D_1BPE_AVX2
{
    MicroSw_3D_1BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, Z0, Y0, Y1, Z1, X2, Z2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {8, 4, 8};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 slices * 4 rows * (8 col * 8b = 64b)
        // We'll do one 64x4 gather-load for each slice.

        // Pre-compute the y offsets
        __m256i yOffsets = _mm256_set_epi64x((3 * bufStrideY), (2 * bufStrideY), (1 * bufStrideY), 0);

        // Then gather, incrementing the 'base' address for each slice.
        // Each reg becomes: [ Y1 Y0 X2 X1 X0 ]
        __m256i z0 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 0))), yOffsets, 1);
        __m256i z1 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 1))), yOffsets, 1);
        __m256i z2 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 2))), yOffsets, 1);
        __m256i z3 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 3))), yOffsets, 1);
        __m256i z4 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 4))), yOffsets, 1);
        __m256i z5 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 5))), yOffsets, 1);
        __m256i z6 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 6))), yOffsets, 1);
        __m256i z7 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 7))), yOffsets, 1);

        // First swap x2 and y0 bits, so the following unpack ends up with x2 across registers
        // Each reg becomes: [ Y1 X2 Y0 X1 X0 ]
        __m256i shuf0 = _mm256_shuffle_epi32(z0, 0b11011000);
        __m256i shuf1 = _mm256_shuffle_epi32(z1, 0b11011000);
        __m256i shuf2 = _mm256_shuffle_epi32(z2, 0b11011000);
        __m256i shuf3 = _mm256_shuffle_epi32(z3, 0b11011000);
        __m256i shuf4 = _mm256_shuffle_epi32(z4, 0b11011000);
        __m256i shuf5 = _mm256_shuffle_epi32(z5, 0b11011000);
        __m256i shuf6 = _mm256_shuffle_epi32(z6, 0b11011000);
        __m256i shuf7 = _mm256_shuffle_epi32(z7, 0b11011000);

        // Unpack to 32-bit interleave by z0.
        // Each reg becomes: [ Y1 Y0 Z0 X1 X0 ]
        __m256i unpack0 = _mm256_unpacklo_epi32(shuf0, shuf1);
        __m256i unpack1 = _mm256_unpackhi_epi32(shuf0, shuf1);
        __m256i unpack2 = _mm256_unpacklo_epi32(shuf2, shuf3);
        __m256i unpack3 = _mm256_unpackhi_epi32(shuf2, shuf3);
        __m256i unpack4 = _mm256_unpacklo_epi32(shuf4, shuf5);
        __m256i unpack5 = _mm256_unpackhi_epi32(shuf4, shuf5);
        __m256i unpack6 = _mm256_unpacklo_epi32(shuf6, shuf7);
        __m256i unpack7 = _mm256_unpackhi_epi32(shuf6, shuf7);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, unpack0);
        _mm256_stream_si256(pAlignedOut++, unpack2);
        _mm256_stream_si256(pAlignedOut++, unpack1);
        _mm256_stream_si256(pAlignedOut++, unpack3);
        _mm256_stream_si256(pAlignedOut++, unpack4);
        _mm256_stream_si256(pAlignedOut++, unpack6);
        _mm256_stream_si256(pAlignedOut++, unpack5);
        _mm256_stream_si256(pAlignedOut++, unpack7);
    }
};

class MicroSw_3D_2BPE_AVX2
{
    MicroSw_3D_2BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, X0, Z0, Y0, X1, Z1, Y1, Z2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 4, 8};
    static constexpr UINT_32       BpeLog2 = 1;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 slices * 4 rows * (4 col * 16b = 64b)
        // We'll do one 64x4 gather-load for each slice.

        // Pre-compute the y offsets, doing a pre-swizzle between y0 and y1 by changing the order of offsets.
        // The pre-swizzle is done so that y1 gets separated across different registers in the unpack below.
        __m256i yOffsets = _mm256_set_epi64x((3 * bufStrideY), (1 * bufStrideY), (2 * bufStrideY), 0);

        // Then gather, incrementing the 'base' address for each slice.
        // Each reg becomes: [ Y0 Y1 X1 X0 0 ]
        __m256i z0 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 0))), yOffsets, 1);
        __m256i z1 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 1))), yOffsets, 1);
        __m256i z2 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 2))), yOffsets, 1);
        __m256i z3 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 3))), yOffsets, 1);
        __m256i z4 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 4))), yOffsets, 1);
        __m256i z5 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 5))), yOffsets, 1);
        __m256i z6 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 6))), yOffsets, 1);
        __m256i z7 = _mm256_i64gather_epi64(reinterpret_cast<const INT_64*>(VoidPtrInc(pBuf, (bufStrideZ * 7))), yOffsets, 1);

        // Unpack to 32-bit interleave by z0.
        // Each reg becomes: [ Y0 X1 Z0 X0 0 ]
        __m256i unpack0 = _mm256_unpacklo_epi32(z0, z1);
        __m256i unpack1 = _mm256_unpackhi_epi32(z0, z1);
        __m256i unpack2 = _mm256_unpacklo_epi32(z2, z3);
        __m256i unpack3 = _mm256_unpackhi_epi32(z2, z3);
        __m256i unpack4 = _mm256_unpacklo_epi32(z4, z5);
        __m256i unpack5 = _mm256_unpackhi_epi32(z4, z5);
        __m256i unpack6 = _mm256_unpacklo_epi32(z6, z7);
        __m256i unpack7 = _mm256_unpackhi_epi32(z6, z7);

        // Then do a cross-lane permute to swap y0 and x1
        // Change [ 0 1 2 3 ] -> [ 0 2 1 3]
        // Each reg becomes: [ X1 Y0 Z0 X0 0 ]
        __m256i permute0 = _mm256_permute4x64_epi64(unpack0, 0b11011000);
        __m256i permute1 = _mm256_permute4x64_epi64(unpack1, 0b11011000);
        __m256i permute2 = _mm256_permute4x64_epi64(unpack2, 0b11011000);
        __m256i permute3 = _mm256_permute4x64_epi64(unpack3, 0b11011000);
        __m256i permute4 = _mm256_permute4x64_epi64(unpack4, 0b11011000);
        __m256i permute5 = _mm256_permute4x64_epi64(unpack5, 0b11011000);
        __m256i permute6 = _mm256_permute4x64_epi64(unpack6, 0b11011000);
        __m256i permute7 = _mm256_permute4x64_epi64(unpack7, 0b11011000);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, permute0);
        _mm256_stream_si256(pAlignedOut++, permute2);
        _mm256_stream_si256(pAlignedOut++, permute1);
        _mm256_stream_si256(pAlignedOut++, permute3);
        _mm256_stream_si256(pAlignedOut++, permute4);
        _mm256_stream_si256(pAlignedOut++, permute6);
        _mm256_stream_si256(pAlignedOut++, permute5);
        _mm256_stream_si256(pAlignedOut++, permute7);
    }
};


class MicroSw_3D_4BPE_AVX2
{
    MicroSw_3D_4BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, X0, Y0, X1, Z0, Y1, Z1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 4, 4};
    static constexpr UINT_32       BpeLog2 = 2;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 slices * 4 rows * (4 col * 32b = 128b)
        // Each reg becomes: [ X1 X0 0 0 ]
        __m128i z0y0 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 0)));
        __m128i z0y1 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 1)));
        __m128i z0y2 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 2)));
        __m128i z0y3 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 3)));
        __m128i z1y0 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 0)));
        __m128i z1y1 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 1)));
        __m128i z1y2 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 2)));
        __m128i z1y3 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 3)));
        __m128i z2y0 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 0)));
        __m128i z2y1 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 1)));
        __m128i z2y2 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 2)));
        __m128i z2y3 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 3)));
        __m128i z3y0 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 0)));
        __m128i z3y1 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 1)));
        __m128i z3y2 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 2)));
        __m128i z3y3 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 3)));

        // Concat two 128b together to form one 256b register, along y0.
        // Each reg becomes: [ Y0 X1 X0 0 0 ]
        __m256i concat0 = _mm256_set_m128i(z0y1, z0y0);
        __m256i concat1 = _mm256_set_m128i(z0y3, z0y2);
        __m256i concat2 = _mm256_set_m128i(z1y1, z1y0);
        __m256i concat3 = _mm256_set_m128i(z1y3, z1y2);
        __m256i concat4 = _mm256_set_m128i(z2y1, z2y0);
        __m256i concat5 = _mm256_set_m128i(z2y3, z2y2);
        __m256i concat6 = _mm256_set_m128i(z3y1, z3y0);
        __m256i concat7 = _mm256_set_m128i(z3y3, z3y2);

        // Then do a cross-lane permute to swap y0 and x1
        // Change [ 0 1 2 3 ] -> [ 0 2 1 3]
        // Each reg becomes: [ X1 Y0 X0 0 0 ]
        __m256i perm0 = _mm256_permute4x64_epi64(concat0, 0b11011000);
        __m256i perm1 = _mm256_permute4x64_epi64(concat1, 0b11011000);
        __m256i perm2 = _mm256_permute4x64_epi64(concat2, 0b11011000);
        __m256i perm3 = _mm256_permute4x64_epi64(concat3, 0b11011000);
        __m256i perm4 = _mm256_permute4x64_epi64(concat4, 0b11011000);
        __m256i perm5 = _mm256_permute4x64_epi64(concat5, 0b11011000);
        __m256i perm6 = _mm256_permute4x64_epi64(concat6, 0b11011000);
        __m256i perm7 = _mm256_permute4x64_epi64(concat7, 0b11011000);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, perm0);
        _mm256_stream_si256(pAlignedOut++, perm2);
        _mm256_stream_si256(pAlignedOut++, perm1);
        _mm256_stream_si256(pAlignedOut++, perm3);
        _mm256_stream_si256(pAlignedOut++, perm4);
        _mm256_stream_si256(pAlignedOut++, perm6);
        _mm256_stream_si256(pAlignedOut++, perm5);
        _mm256_stream_si256(pAlignedOut++, perm7);
    }
};

class MicroSw_3D_8BPE_AVX2
{
    MicroSw_3D_8BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, X0, Y0, Z0, X1, Z1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 2, 4};
    static constexpr UINT_32       BpeLog2 = 3;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 slices * 2 rows * (4 col * 64b = 256b)
        // Each reg becomes: [ X1 X0 0 0 0 ]
        __m256i z0y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 0))));
        __m256i z0y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + bufStrideY)));
        __m256i z1y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 1))));
        __m256i z1y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + bufStrideY)));
        __m256i z2y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 2))));
        __m256i z2y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + bufStrideY)));
        __m256i z3y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 3))));
        __m256i z3y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + bufStrideY)));

        // Use 128-bit permutes to swap the y0 bit (diff registers) with the x0 bit (128b boundary)
        // Start: y0/y1: [ 0 1 ] [ 2 3 ] (128b)
        // Desired:      [ 0 2 ] [ 1 3 ]
        // The magic mask value lines up with the numbers above in hex, so 0x20 means [ 0 2 ]
        // Each reg becomes: [ Y0 X0 0 0 0 ]
        __m256i z0x0 = _mm256_permute2x128_si256(z0y0, z0y1, 0x20);
        __m256i z0x1 = _mm256_permute2x128_si256(z0y0, z0y1, 0x31);
        __m256i z1x0 = _mm256_permute2x128_si256(z1y0, z1y1, 0x20);
        __m256i z1x1 = _mm256_permute2x128_si256(z1y0, z1y1, 0x31);
        __m256i z2x0 = _mm256_permute2x128_si256(z2y0, z2y1, 0x20);
        __m256i z2x1 = _mm256_permute2x128_si256(z2y0, z2y1, 0x31);
        __m256i z3x0 = _mm256_permute2x128_si256(z3y0, z3y1, 0x20);
        __m256i z3x1 = _mm256_permute2x128_si256(z3y0, z3y1, 0x31);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, z0x0);
        _mm256_stream_si256(pAlignedOut++, z1x0);
        _mm256_stream_si256(pAlignedOut++, z0x1);
        _mm256_stream_si256(pAlignedOut++, z1x1);
        _mm256_stream_si256(pAlignedOut++, z2x0);
        _mm256_stream_si256(pAlignedOut++, z3x0);
        _mm256_stream_si256(pAlignedOut++, z2x1);
        _mm256_stream_si256(pAlignedOut++, z3x1);
    }
};


class MicroSw_3D_16BPE_AVX2
{
    MicroSw_3D_16BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, 0, X0, Z0, Y0, Z1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {2, 2, 4};
    static constexpr UINT_32       BpeLog2 = 4;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 slices * 2 rows * (2 col * 128b = 256b)
        __m256i z0y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 0))));
        __m256i z0y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + bufStrideY)));
        __m256i z1y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 1))));
        __m256i z1y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + bufStrideY)));
        __m256i z2y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 2))));
        __m256i z2y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + bufStrideY)));
        __m256i z3y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 3))));
        __m256i z3y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + bufStrideY)));

        // The whole swizzle can be handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, z0y0);
        _mm256_stream_si256(pAlignedOut++, z1y0);
        _mm256_stream_si256(pAlignedOut++, z0y1);
        _mm256_stream_si256(pAlignedOut++, z1y1);
        _mm256_stream_si256(pAlignedOut++, z2y0);
        _mm256_stream_si256(pAlignedOut++, z3y0);
        _mm256_stream_si256(pAlignedOut++, z2y1);
        _mm256_stream_si256(pAlignedOut++, z3y1);
    }
};


class MicroSw_R_1BPE_AVX2
{
    MicroSw_R_1BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, X2, X3, Y0, Y1, Y2, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 4;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Yes, that means double the load instructions
        // Each reg becomes: [ X3 X2 X1 X0 ]
        __m128i y0  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 0));
        __m128i y1  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 1));
        __m128i y2  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 2));
        __m128i y3  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 3));
        __m128i y4  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 4));
        __m128i y5  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 5));
        __m128i y6  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 6));
        __m128i y7  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 7));
        __m128i y8  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 8));
        __m128i y9  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 9));
        __m128i y10 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 10));
        __m128i y11 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 11));
        __m128i y12 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 12));
        __m128i y13 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 13));
        __m128i y14 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 14));
        __m128i y15 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 15));

        // Concat two 128b together to form one 256b register, along y0.
        // Each reg becomes: [ Y0 X3 X2 X1 X0 ]
        __m256i concat0 = _mm256_set_m128i(y1, y0);
        __m256i concat1 = _mm256_set_m128i(y3, y2);
        __m256i concat2 = _mm256_set_m128i(y5, y4);
        __m256i concat3 = _mm256_set_m128i(y7, y6);
        __m256i concat4 = _mm256_set_m128i(y9, y8);
        __m256i concat5 = _mm256_set_m128i(y11, y10);
        __m256i concat6 = _mm256_set_m128i(y13, y12);
        __m256i concat7 = _mm256_set_m128i(y15, y14);

        // The top 3 bits of the swizzle are handled by the order of the registers here. The rest are already right.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, concat0);
        _mm256_stream_si256(pAlignedOut++, concat1);
        _mm256_stream_si256(pAlignedOut++, concat2);
        _mm256_stream_si256(pAlignedOut++, concat3);
        _mm256_stream_si256(pAlignedOut++, concat4);
        _mm256_stream_si256(pAlignedOut++, concat5);
        _mm256_stream_si256(pAlignedOut++, concat6);
        _mm256_stream_si256(pAlignedOut++, concat7);
    }
};

class MicroSw_R_2BPE_AVX2
{
    MicroSw_R_2BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, X0, X1, X2, Y0, Y1, Y2, X3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 8, 1};
    static constexpr UINT_32       BpeLog2 = 1;
    static constexpr UINT_32       ExpandX = 4;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (16 col * 16b = 256b)
        // Each reg becomes: [ X3 X2 X1 X0 0 ]
        __m256i y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 0)));
        __m256i y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 1)));
        __m256i y2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 2)));
        __m256i y3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 3)));
        __m256i y4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 4)));
        __m256i y5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 5)));
        __m256i y6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 6)));
        __m256i y7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 7)));

        // In-register shuffling on the bottom 5 bits of which the bottom 4 are already right: (Y0, *, *, *, *)
        // Start: y0/y1: [ 0 1 ] [ 2 3 ] (128b)
        // Desired:      [ 0 2 ] [ 1 3 ]
        // The magic mask value lines up with the numbers above in hex, so 0x20 means [ 0 2 ]
        // Each reg becomes: [ Y0 X2 X1 X0 0 ]
        __m256i perm0 = _mm256_permute2x128_si256(y0, y1, 0x20);
        __m256i perm1 = _mm256_permute2x128_si256(y0, y1, 0x31);
        __m256i perm2 = _mm256_permute2x128_si256(y2, y3, 0x20);
        __m256i perm3 = _mm256_permute2x128_si256(y2, y3, 0x31);
        __m256i perm4 = _mm256_permute2x128_si256(y4, y5, 0x20);
        __m256i perm5 = _mm256_permute2x128_si256(y4, y5, 0x31);
        __m256i perm6 = _mm256_permute2x128_si256(y6, y7, 0x20);
        __m256i perm7 = _mm256_permute2x128_si256(y6, y7, 0x31);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, perm0);
        _mm256_stream_si256(pAlignedOut++, perm2);
        _mm256_stream_si256(pAlignedOut++, perm4);
        _mm256_stream_si256(pAlignedOut++, perm6);
        _mm256_stream_si256(pAlignedOut++, perm1);
        _mm256_stream_si256(pAlignedOut++, perm3);
        _mm256_stream_si256(pAlignedOut++, perm5);
        _mm256_stream_si256(pAlignedOut++, perm7);
    }
};

class MicroSw_R_4BPE_AVX2
{
    MicroSw_R_4BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, X0, X1, Y0, Y1, X2, Y2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {8, 8, 1};
    static constexpr UINT_32       BpeLog2 = 2;
    static constexpr UINT_32       ExpandX = 4;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (8 col * 32b = 256b)
        // Each reg becomes: [ X2 X1 X0 0 0 ]
        __m256i y0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 0)));
        __m256i y1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 1)));
        __m256i y2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 2)));
        __m256i y3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 3)));
        __m256i y4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 4)));
        __m256i y5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 5)));
        __m256i y6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 6)));
        __m256i y7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(VoidPtrInc(pBuf, bufStrideY * 7)));

        // In-register shuffling on the bottom 5 bits of which the bottom 4 are already right: (Y0, *, *, *, *)
        // Start: y0/y1: [ 0 1 ] [ 2 3 ] (128b)
        // Desired:      [ 0 2 ] [ 1 3 ]
        // The magic mask value lines up with the numbers above in hex, so 0x20 means [ 0 2 ]
        // Each reg becomes: [ Y0 X1 X0 0 0 ]
        __m256i perm0 = _mm256_permute2x128_si256(y0, y1, 0x20);
        __m256i perm1 = _mm256_permute2x128_si256(y0, y1, 0x31);
        __m256i perm2 = _mm256_permute2x128_si256(y2, y3, 0x20);
        __m256i perm3 = _mm256_permute2x128_si256(y2, y3, 0x31);
        __m256i perm4 = _mm256_permute2x128_si256(y4, y5, 0x20);
        __m256i perm5 = _mm256_permute2x128_si256(y4, y5, 0x31);
        __m256i perm6 = _mm256_permute2x128_si256(y6, y7, 0x20);
        __m256i perm7 = _mm256_permute2x128_si256(y6, y7, 0x31);

        // The top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, perm0);
        _mm256_stream_si256(pAlignedOut++, perm2);
        _mm256_stream_si256(pAlignedOut++, perm1);
        _mm256_stream_si256(pAlignedOut++, perm3);
        _mm256_stream_si256(pAlignedOut++, perm4);
        _mm256_stream_si256(pAlignedOut++, perm6);
        _mm256_stream_si256(pAlignedOut++, perm5);
        _mm256_stream_si256(pAlignedOut++, perm7);
    }
};


class MicroSw_Z_1BPE_AVX2
{
    MicroSw_Z_1BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, Y0, X1, Y1, X2, Y2, X3, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 2;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Each reg becomes: [ X3 X2 X1 X0 ]
        __m128i y0  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*0));
        __m128i y1  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*1));
        __m128i y2  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*2));
        __m128i y3  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*3));
        __m128i y4  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*4));
        __m128i y5  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*5));
        __m128i y6  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*6));
        __m128i y7  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*7));
        __m128i y8  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*8));
        __m128i y9  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*9));
        __m128i y10 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*10));
        __m128i y11 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*11));
        __m128i y12 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*12));
        __m128i y13 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*13));
        __m128i y14 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*14));
        __m128i y15 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY*15));

        // First combine two 128-bit values (across y1)
        // Each reg becomes: [ Y1 X3 X2 X1 X0 ]
        __m256i concat0 = _mm256_set_m128i(y2, y0);
        __m256i concat1 = _mm256_set_m128i(y3, y1);
        __m256i concat2 = _mm256_set_m128i(y6, y4);
        __m256i concat3 = _mm256_set_m128i(y7, y5);
        __m256i concat4 = _mm256_set_m128i(y10, y8);
        __m256i concat5 = _mm256_set_m128i(y11, y9);
        __m256i concat6 = _mm256_set_m128i(y14, y12);
        __m256i concat7 = _mm256_set_m128i(y15, y13);

        // Then do a 16-bit interleave across y0. This is done in parallel on each 128b lane.
        // Each reg becomes: [ Y1 X2 X1 Y0 X0 ]
        __m256i unpack0 = _mm256_unpacklo_epi16(concat0, concat1);
        __m256i unpack1 = _mm256_unpackhi_epi16(concat0, concat1);
        __m256i unpack2 = _mm256_unpacklo_epi16(concat2, concat3);
        __m256i unpack3 = _mm256_unpackhi_epi16(concat2, concat3);
        __m256i unpack4 = _mm256_unpacklo_epi16(concat4, concat5);
        __m256i unpack5 = _mm256_unpackhi_epi16(concat4, concat5);
        __m256i unpack6 = _mm256_unpacklo_epi16(concat6, concat7);
        __m256i unpack7 = _mm256_unpackhi_epi16(concat6, concat7);

        // Then do a cross-lane permute to change our 128b interleave across y1 to a 64-bit interleave.
        // Change [ 0 1 2 3 ] -> [ 0 2 1 3]
        // Each reg becomes: [ X2 Y1 X1 Y0 X0 ]
        __m256i permute0 = _mm256_permute4x64_epi64(unpack0, 0b11011000);
        __m256i permute1 = _mm256_permute4x64_epi64(unpack1, 0b11011000);
        __m256i permute2 = _mm256_permute4x64_epi64(unpack2, 0b11011000);
        __m256i permute3 = _mm256_permute4x64_epi64(unpack3, 0b11011000);
        __m256i permute4 = _mm256_permute4x64_epi64(unpack4, 0b11011000);
        __m256i permute5 = _mm256_permute4x64_epi64(unpack5, 0b11011000);
        __m256i permute6 = _mm256_permute4x64_epi64(unpack6, 0b11011000);
        __m256i permute7 = _mm256_permute4x64_epi64(unpack7, 0b11011000);

        // Finally, the top 3 bits of the swizzle are handled by the order of the registers here.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, permute0);
        _mm256_stream_si256(pAlignedOut++, permute2);
        _mm256_stream_si256(pAlignedOut++, permute1);
        _mm256_stream_si256(pAlignedOut++, permute3);
        _mm256_stream_si256(pAlignedOut++, permute4);
        _mm256_stream_si256(pAlignedOut++, permute6);
        _mm256_stream_si256(pAlignedOut++, permute5);
        _mm256_stream_si256(pAlignedOut++, permute7);
    }
};


class MicroSw_D_1BPE_AVX2
{
    MicroSw_D_1BPE_AVX2() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, X2, Y1, Y0, Y2, X3, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 4;

    AVX2_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Yes, that means double the load instructions
        // Each reg becomes: [ X3 X2 X1 X0 ]
        __m128i y0  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 0));
        __m128i y1  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 1));
        __m128i y2  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 2));
        __m128i y3  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 3));
        __m128i y4  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 4));
        __m128i y5  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 5));
        __m128i y6  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 6));
        __m128i y7  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 7));
        __m128i y8  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 8));
        __m128i y9  = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 9));
        __m128i y10 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 10));
        __m128i y11 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 11));
        __m128i y12 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 12));
        __m128i y13 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 13));
        __m128i y14 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 14));
        __m128i y15 = _mm_loadu_si128((const __m128i*)VoidPtrInc(pBuf, bufStrideY * 15));

        // Concat two 128b together to form one 256b register, along y0.
        // Each reg becomes: [ Y0 X3 X2 X1 X0 ]
        __m256i concat0 = _mm256_set_m128i(y1, y0);
        __m256i concat1 = _mm256_set_m128i(y3, y2);
        __m256i concat2 = _mm256_set_m128i(y5, y4);
        __m256i concat3 = _mm256_set_m128i(y7, y6);
        __m256i concat4 = _mm256_set_m128i(y9, y8);
        __m256i concat5 = _mm256_set_m128i(y11, y10);
        __m256i concat6 = _mm256_set_m128i(y13, y12);
        __m256i concat7 = _mm256_set_m128i(y15, y14);

        // Then do a 64-bit interleave along y1.
        // Each reg becomes: [ Y0 Y1 X2 X1 X0 ]
        __m256i unpack0 = _mm256_unpacklo_epi64(concat0, concat1);
        __m256i unpack1 = _mm256_unpackhi_epi64(concat0, concat1);
        __m256i unpack2 = _mm256_unpacklo_epi64(concat2, concat3);
        __m256i unpack3 = _mm256_unpackhi_epi64(concat2, concat3);
        __m256i unpack4 = _mm256_unpacklo_epi64(concat4, concat5);
        __m256i unpack5 = _mm256_unpackhi_epi64(concat4, concat5);
        __m256i unpack6 = _mm256_unpacklo_epi64(concat6, concat7);
        __m256i unpack7 = _mm256_unpackhi_epi64(concat6, concat7);

        // The top 3 bits of the swizzle are handled by the order of the registers here. The rest are already right.
        // Use streaming stores to optimize memory behavior-- this requires aligned memory.
        __m256i* pAlignedOut = reinterpret_cast<__m256i*>(pImgMicroblock);
        _mm256_stream_si256(pAlignedOut++, unpack0);
        _mm256_stream_si256(pAlignedOut++, unpack2);
        _mm256_stream_si256(pAlignedOut++, unpack1);
        _mm256_stream_si256(pAlignedOut++, unpack3);
        _mm256_stream_si256(pAlignedOut++, unpack4);
        _mm256_stream_si256(pAlignedOut++, unpack6);
        _mm256_stream_si256(pAlignedOut++, unpack5);
        _mm256_stream_si256(pAlignedOut++, unpack7);
    }
};
#endif // ADDR_HAS_AVX2

#if ADDR_HAS_NEON
class MicroSw_2D_1BPE_NEON
{
    MicroSw_2D_1BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, Y0, X2, Y1, Y2, X3, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 4;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Each reg becomes: [ X3 X2 X1 X0 ]
        uint32x4_t y0  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint32x4_t y1  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint32x4_t y2  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint32x4_t y3  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint32x4_t y4  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint32x4_t y5  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint32x4_t y6  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint32x4_t y7  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));
        uint32x4_t y8  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 8))));
        uint32x4_t y9  = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 9))));
        uint32x4_t y10 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 10))));
        uint32x4_t y11 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 11))));
        uint32x4_t y12 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 12))));
        uint32x4_t y13 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 13))));
        uint32x4_t y14 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 14))));
        uint32x4_t y15 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 15))));

        // Do a 32-bit zip/unpack operation to interleave within each 128b reg.
        // Each reg becomes: [ X2 Y0 X1 X0 ]
        uint32x4_t comb0a = vzip1q_u32(y0, y1);
        uint32x4_t comb0b = vzip2q_u32(y0, y1);
        uint32x4_t comb1a = vzip1q_u32(y2, y3);
        uint32x4_t comb1b = vzip2q_u32(y2, y3);
        uint32x4_t comb2a = vzip1q_u32(y4, y5);
        uint32x4_t comb2b = vzip2q_u32(y4, y5);
        uint32x4_t comb3a = vzip1q_u32(y6, y7);
        uint32x4_t comb3b = vzip2q_u32(y6, y7);
        uint32x4_t comb4a = vzip1q_u32(y8, y9);
        uint32x4_t comb4b = vzip2q_u32(y8, y9);
        uint32x4_t comb5a = vzip1q_u32(y10, y11);
        uint32x4_t comb5b = vzip2q_u32(y10, y11);
        uint32x4_t comb6a = vzip1q_u32(y12, y13);
        uint32x4_t comb6b = vzip2q_u32(y12, y13);
        uint32x4_t comb7a = vzip1q_u32(y14, y15);
        uint32x4_t comb7b = vzip2q_u32(y14, y15);

        // The top 4 bits of the swizzle are handled by plain reg moves here.
        uint32x4x4_t out0 = { { comb0a, comb1a, comb2a, comb3a } };
        uint32x4x4_t out1 = { { comb0b, comb1b, comb2b, comb3b } };
        uint32x4x4_t out2 = { { comb4a, comb5a, comb6a, comb7a } };
        uint32x4x4_t out3 = { { comb4b, comb5b, comb6b, comb7b } };

        // And store them using the largest contiguous store we can.
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_2D_2BPE_NEON
{
    MicroSw_2D_2BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, X0, Y0, X1, Y1, X2, Y2, X3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 8, 1};
    static constexpr UINT_32       BpeLog2 = 1;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (16 col * 16b = 256b)
        // ARM can do a 256b load/store, but the actual values are 128b.
        // Each reg becomes: [ X2 X1 X0 0 ]
        uint32x4x2_t y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint32x4x2_t y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint32x4x2_t y2 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint32x4x2_t y3 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint32x4x2_t y4 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint32x4x2_t y5 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint32x4x2_t y6 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint32x4x2_t y7 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));

        // Do a 32-bit zip/unpack operation to interleave within each 128b reg.
        // Each reg becomes: [ X1 Y0 X0 0 ]
        uint32x4_t comb0a = vzip1q_u32(y0.val[0], y1.val[0]);
        uint32x4_t comb0b = vzip2q_u32(y0.val[0], y1.val[0]);
        uint32x4_t comb1a = vzip1q_u32(y0.val[1], y1.val[1]);
        uint32x4_t comb1b = vzip2q_u32(y0.val[1], y1.val[1]);
        uint32x4_t comb2a = vzip1q_u32(y2.val[0], y3.val[0]);
        uint32x4_t comb2b = vzip2q_u32(y2.val[0], y3.val[0]);
        uint32x4_t comb3a = vzip1q_u32(y2.val[1], y3.val[1]);
        uint32x4_t comb3b = vzip2q_u32(y2.val[1], y3.val[1]);
        uint32x4_t comb4a = vzip1q_u32(y4.val[0], y5.val[0]);
        uint32x4_t comb4b = vzip2q_u32(y4.val[0], y5.val[0]);
        uint32x4_t comb5a = vzip1q_u32(y4.val[1], y5.val[1]);
        uint32x4_t comb5b = vzip2q_u32(y4.val[1], y5.val[1]);
        uint32x4_t comb6a = vzip1q_u32(y6.val[0], y7.val[0]);
        uint32x4_t comb6b = vzip2q_u32(y6.val[0], y7.val[0]);
        uint32x4_t comb7a = vzip1q_u32(y6.val[1], y7.val[1]);
        uint32x4_t comb7b = vzip2q_u32(y6.val[1], y7.val[1]);

        // The top 4 bits of the swizzle are handled by plain reg moves here.
        uint32x4x4_t out0 = { { comb0a, comb2a, comb0b, comb2b } };
        uint32x4x4_t out1 = { { comb4a, comb6a, comb4b, comb6b} };
        uint32x4x4_t out2 = { { comb1a, comb3a, comb1b, comb3b } };
        uint32x4x4_t out3 = { { comb5a, comb7a, comb5b, comb7b } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};
class MicroSw_2D_4BPE_NEON
{
    MicroSw_2D_4BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, X0, Y0, X1, Y1, X2, Y2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = { 8, 8, 1};
    static constexpr UINT_32       BpeLog2 = 2;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (8 col * 32b = 256b)
        // ARM can do a 256b load/store, but the actual values are 128b.
        // Each reg becomes: [ X1 X0 0 0 ]
        uint64x2x2_t y0 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint64x2x2_t y1 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint64x2x2_t y2 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint64x2x2_t y3 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint64x2x2_t y4 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint64x2x2_t y5 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint64x2x2_t y6 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint64x2x2_t y7 = vld1q_u64_x2(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));

        // Do a 64-bit zip/unpack operation to interleave within each 128b reg.
        // Each reg becomes: [ Y0 X0 0 0 ]
        uint64x2_t comb0a = vzip1q_u64(y0.val[0], y1.val[0]);
        uint64x2_t comb0b = vzip2q_u64(y0.val[0], y1.val[0]);
        uint64x2_t comb1a = vzip1q_u64(y0.val[1], y1.val[1]);
        uint64x2_t comb1b = vzip2q_u64(y0.val[1], y1.val[1]);
        uint64x2_t comb2a = vzip1q_u64(y2.val[0], y3.val[0]);
        uint64x2_t comb2b = vzip2q_u64(y2.val[0], y3.val[0]);
        uint64x2_t comb3a = vzip1q_u64(y2.val[1], y3.val[1]);
        uint64x2_t comb3b = vzip2q_u64(y2.val[1], y3.val[1]);
        uint64x2_t comb4a = vzip1q_u64(y4.val[0], y5.val[0]);
        uint64x2_t comb4b = vzip2q_u64(y4.val[0], y5.val[0]);
        uint64x2_t comb5a = vzip1q_u64(y4.val[1], y5.val[1]);
        uint64x2_t comb5b = vzip2q_u64(y4.val[1], y5.val[1]);
        uint64x2_t comb6a = vzip1q_u64(y6.val[0], y7.val[0]);
        uint64x2_t comb6b = vzip2q_u64(y6.val[0], y7.val[0]);
        uint64x2_t comb7a = vzip1q_u64(y6.val[1], y7.val[1]);
        uint64x2_t comb7b = vzip2q_u64(y6.val[1], y7.val[1]);

        // The top 4 bits of the swizzle are handled by plain reg moves here.
        uint64x2x4_t out0 = { { comb0a, comb0b, comb2a, comb2b } };
        uint64x2x4_t out1 = { { comb1a, comb1b, comb3a, comb3b } };
        uint64x2x4_t out2 = { { comb4a, comb4b, comb6a, comb6b } };
        uint64x2x4_t out3 = { { comb5a, comb5b, comb7a, comb7b } };

        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }

};

class MicroSw_2D_8BPE_NEON
{
    MicroSw_2D_8BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, X0, Y0, X1, X2, Y1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {8, 4, 1};
    static constexpr UINT_32       BpeLog2 = 3;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {

        // Unaligned buffer loads for 4 rows * (8 col * 64b = 256bx2)
        // ARM can do a 512b load/store, but the actual values are 128b.
        uint32x4x4_t y0 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint32x4x4_t y1 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint32x4x4_t y2 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint32x4x4_t y3 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 bits need no handling.
        uint32x4x4_t out0 = { { y0.val[0],  y1.val[0], y0.val[1], y1.val[1] } };
        uint32x4x4_t out1 = { { y0.val[2],  y1.val[2], y0.val[3], y1.val[3] } };
        uint32x4x4_t out2 = { { y2.val[0],  y3.val[0], y2.val[1], y3.val[1] } };
        uint32x4x4_t out3 = { { y2.val[2],  y3.val[2], y2.val[3], y3.val[3] } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};
class MicroSw_2D_16BPE_NEON
{
    MicroSw_2D_16BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, 0, X0, Y0, X1, Y1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 4, 1};
    static constexpr UINT_32       BpeLog2 = 4;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {

        // Unaligned buffer loads for 4 rows * (4 col * 128b = 512b)
        // ARM can do a 512b load/store, but the actual values are 128b.
        uint32x4x4_t y0 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint32x4x4_t y1 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint32x4x4_t y2 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint32x4x4_t y3 = vld1q_u32_x4(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));

        // The top 3 bits of the swizzle are handled by plain reg moves here. The rest are already right.
        uint32x4x4_t out0 = { { y0.val[0],  y0.val[1], y1.val[0], y1.val[1] } };
        uint32x4x4_t out1 = { { y0.val[2],  y0.val[3], y1.val[2], y1.val[3] } };
        uint32x4x4_t out2 = { { y2.val[0],  y2.val[1], y3.val[0], y3.val[1] } };
        uint32x4x4_t out3 = { { y2.val[2],  y2.val[3], y3.val[2], y3.val[3] } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};
class MicroSw_3D_1BPE_NEON
{
    MicroSw_3D_1BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, Z0, Y0, Y1, Z1, X2, Z2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {8, 4, 8};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 2;
    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 slices * 4 rows * (4 col * 16b = 64b)
        // Do a lot of 64b (half reg) loads and join them
        // Each reg becomes: [ Y1 X2 X1 X0 ]
        uint32x4_t z0y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 2)))));
        uint32x4_t z0y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 3)))));
        uint32x4_t z1y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 2)))));
        uint32x4_t z1y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 3)))));
        uint32x4_t z2y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 2)))));
        uint32x4_t z2y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 3)))));
        uint32x4_t z3y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 2)))));
        uint32x4_t z3y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 3)))));
        uint32x4_t z4y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 2)))));
        uint32x4_t z4y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 3)))));
        uint32x4_t z5y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 2)))));
        uint32x4_t z5y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 3)))));
        uint32x4_t z6y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 2)))));
        uint32x4_t z6y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 3)))));
        uint32x4_t z7y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 2)))));
        uint32x4_t z7y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 3)))));

        // Now, do two 32-bit interleaves, first for y0, then for z0
        // Each reg becomes: [ X2 Y0 X1 X0 ]
        uint32x4_t ycomb0a = vzip1q_u32(z0y02, z0y13);
        uint32x4_t ycomb0b = vzip2q_u32(z0y02, z0y13);
        uint32x4_t ycomb1a = vzip1q_u32(z1y02, z1y13);
        uint32x4_t ycomb1b = vzip2q_u32(z1y02, z1y13);
        uint32x4_t ycomb2a = vzip1q_u32(z2y02, z2y13);
        uint32x4_t ycomb2b = vzip2q_u32(z2y02, z2y13);
        uint32x4_t ycomb3a = vzip1q_u32(z3y02, z3y13);
        uint32x4_t ycomb3b = vzip2q_u32(z3y02, z3y13);
        uint32x4_t ycomb4a = vzip1q_u32(z4y02, z4y13);
        uint32x4_t ycomb4b = vzip2q_u32(z4y02, z4y13);
        uint32x4_t ycomb5a = vzip1q_u32(z5y02, z5y13);
        uint32x4_t ycomb5b = vzip2q_u32(z5y02, z5y13);
        uint32x4_t ycomb6a = vzip1q_u32(z6y02, z6y13);
        uint32x4_t ycomb6b = vzip2q_u32(z6y02, z6y13);
        uint32x4_t ycomb7a = vzip1q_u32(z7y02, z7y13);
        uint32x4_t ycomb7b = vzip2q_u32(z7y02, z7y13);

        // Each reg becomes: [ Y0 Z0 X1 X0 ]
        uint32x4_t comb0a = vzip1q_u32(ycomb0a, ycomb1a);
        uint32x4_t comb0b = vzip2q_u32(ycomb0a, ycomb1a);
        uint32x4_t comb1a = vzip1q_u32(ycomb0b, ycomb1b);
        uint32x4_t comb1b = vzip2q_u32(ycomb0b, ycomb1b);
        uint32x4_t comb2a = vzip1q_u32(ycomb2a, ycomb3a);
        uint32x4_t comb2b = vzip2q_u32(ycomb2a, ycomb3a);
        uint32x4_t comb3a = vzip1q_u32(ycomb2b, ycomb3b);
        uint32x4_t comb3b = vzip2q_u32(ycomb2b, ycomb3b);
        uint32x4_t comb4a = vzip1q_u32(ycomb4a, ycomb5a);
        uint32x4_t comb4b = vzip2q_u32(ycomb4a, ycomb5a);
        uint32x4_t comb5a = vzip1q_u32(ycomb4b, ycomb5b);
        uint32x4_t comb5b = vzip2q_u32(ycomb4b, ycomb5b);
        uint32x4_t comb6a = vzip1q_u32(ycomb6a, ycomb7a);
        uint32x4_t comb6b = vzip2q_u32(ycomb6a, ycomb7a);
        uint32x4_t comb7a = vzip1q_u32(ycomb6b, ycomb7b);
        uint32x4_t comb7b = vzip2q_u32(ycomb6b, ycomb7b);

        // The top 4 bits of the swizzle are handled by plain reg moves here.
        uint32x4x4_t out0 = { { comb0a, comb1a, comb2a, comb3a } };
        uint32x4x4_t out1 = { { comb0b, comb1b, comb2b, comb3b } };
        uint32x4x4_t out2 = { { comb4a, comb5a, comb6a, comb7a } };
        uint32x4x4_t out3 = { { comb4b, comb5b, comb6b, comb7b } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_3D_2BPE_NEON
{
    MicroSw_3D_2BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, X0, Z0, Y0, X1, Z1, Y1, Z2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 4, 8};
    static constexpr UINT_32       BpeLog2 = 1;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {

        // Unaligned buffer loads for 8 slices * 4 rows * (4 col * 16b = 64b)
        // Do a lot of 64b (half reg) loads and join them
        // Each reg becomes: [ Y1 X1 X0 0 ]
        uint32x4_t z0y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 2)))));
        uint32x4_t z0y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 3)))));
        uint32x4_t z1y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 2)))));
        uint32x4_t z1y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 3)))));
        uint32x4_t z2y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 2)))));
        uint32x4_t z2y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 3)))));
        uint32x4_t z3y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 2)))));
        uint32x4_t z3y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 3)))));
        uint32x4_t z4y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 2)))));
        uint32x4_t z4y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 4) + (bufStrideY * 3)))));
        uint32x4_t z5y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 2)))));
        uint32x4_t z5y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 5) + (bufStrideY * 3)))));
        uint32x4_t z6y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 2)))));
        uint32x4_t z6y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 6) + (bufStrideY * 3)))));
        uint32x4_t z7y02 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 0)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 2)))));
        uint32x4_t z7y13 = vcombine_u32(
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 1)))),
            vld1_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 7) + (bufStrideY * 3)))));

        // Now, do two 32-bit interleaves, first for y0, then for z0
        // Each reg becomes: [ X1 Y0 X0 0 ]
        uint32x4_t ycomb0a = vzip1q_u32(z0y02, z0y13);
        uint32x4_t ycomb0b = vzip2q_u32(z0y02, z0y13);
        uint32x4_t ycomb1a = vzip1q_u32(z1y02, z1y13);
        uint32x4_t ycomb1b = vzip2q_u32(z1y02, z1y13);
        uint32x4_t ycomb2a = vzip1q_u32(z2y02, z2y13);
        uint32x4_t ycomb2b = vzip2q_u32(z2y02, z2y13);
        uint32x4_t ycomb3a = vzip1q_u32(z3y02, z3y13);
        uint32x4_t ycomb3b = vzip2q_u32(z3y02, z3y13);
        uint32x4_t ycomb4a = vzip1q_u32(z4y02, z4y13);
        uint32x4_t ycomb4b = vzip2q_u32(z4y02, z4y13);
        uint32x4_t ycomb5a = vzip1q_u32(z5y02, z5y13);
        uint32x4_t ycomb5b = vzip2q_u32(z5y02, z5y13);
        uint32x4_t ycomb6a = vzip1q_u32(z6y02, z6y13);
        uint32x4_t ycomb6b = vzip2q_u32(z6y02, z6y13);
        uint32x4_t ycomb7a = vzip1q_u32(z7y02, z7y13);
        uint32x4_t ycomb7b = vzip2q_u32(z7y02, z7y13);

        // Each reg becomes: [ Y0 Z0 X0 0 ]
        uint32x4_t comb0a = vzip1q_u32(ycomb0a, ycomb1a);
        uint32x4_t comb0b = vzip2q_u32(ycomb0a, ycomb1a);
        uint32x4_t comb1a = vzip1q_u32(ycomb0b, ycomb1b);
        uint32x4_t comb1b = vzip2q_u32(ycomb0b, ycomb1b);
        uint32x4_t comb2a = vzip1q_u32(ycomb2a, ycomb3a);
        uint32x4_t comb2b = vzip2q_u32(ycomb2a, ycomb3a);
        uint32x4_t comb3a = vzip1q_u32(ycomb2b, ycomb3b);
        uint32x4_t comb3b = vzip2q_u32(ycomb2b, ycomb3b);
        uint32x4_t comb4a = vzip1q_u32(ycomb4a, ycomb5a);
        uint32x4_t comb4b = vzip2q_u32(ycomb4a, ycomb5a);
        uint32x4_t comb5a = vzip1q_u32(ycomb4b, ycomb5b);
        uint32x4_t comb5b = vzip2q_u32(ycomb4b, ycomb5b);
        uint32x4_t comb6a = vzip1q_u32(ycomb6a, ycomb7a);
        uint32x4_t comb6b = vzip2q_u32(ycomb6a, ycomb7a);
        uint32x4_t comb7a = vzip1q_u32(ycomb6b, ycomb7b);
        uint32x4_t comb7b = vzip2q_u32(ycomb6b, ycomb7b);

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 bits need no handling.
        uint32x4x4_t out0 = { { comb0a, comb0b, comb2a, comb2b } };
        uint32x4x4_t out1 = { { comb1a, comb1b, comb3a, comb3b } };
        uint32x4x4_t out2 = { { comb4a, comb4b, comb6a, comb6b } };
        uint32x4x4_t out3 = { { comb5a, comb5b, comb7a, comb7b } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_3D_4BPE_NEON
{
    MicroSw_3D_4BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, X0, Y0, X1, Z0, Y1, Z1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 4, 4};
    static constexpr UINT_32       BpeLog2 = 2;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 rows * (8 col * 64b = 256bx2)
        // Each reg becomes: [ X1 X0 0 0 ]
        uint64x2_t z0y0 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 0))));
        uint64x2_t z0y1 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 1))));
        uint64x2_t z0y2 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 2))));
        uint64x2_t z0y3 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + (bufStrideY * 3))));
        uint64x2_t z1y0 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 0))));
        uint64x2_t z1y1 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 1))));
        uint64x2_t z1y2 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 2))));
        uint64x2_t z1y3 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + (bufStrideY * 3))));
        uint64x2_t z2y0 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 0))));
        uint64x2_t z2y1 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 1))));
        uint64x2_t z2y2 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 2))));
        uint64x2_t z2y3 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + (bufStrideY * 3))));
        uint64x2_t z3y0 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 0))));
        uint64x2_t z3y1 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 1))));
        uint64x2_t z3y2 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 2))));
        uint64x2_t z3y3 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + (bufStrideY * 3))));

        // Then do a 64-bit interleave across y0
        // Each reg becomes: [ Y0 X0 0 0 ]
        uint64x2_t comb0a = vzip1q_u64(z0y0, z0y1);
        uint64x2_t comb0b = vzip2q_u64(z0y0, z0y1);
        uint64x2_t comb1a = vzip1q_u64(z0y2, z0y3);
        uint64x2_t comb1b = vzip2q_u64(z0y2, z0y3);
        uint64x2_t comb2a = vzip1q_u64(z1y0, z1y1);
        uint64x2_t comb2b = vzip2q_u64(z1y0, z1y1);
        uint64x2_t comb3a = vzip1q_u64(z1y2, z1y3);
        uint64x2_t comb3b = vzip2q_u64(z1y2, z1y3);
        uint64x2_t comb4a = vzip1q_u64(z2y0, z2y1);
        uint64x2_t comb4b = vzip2q_u64(z2y0, z2y1);
        uint64x2_t comb5a = vzip1q_u64(z2y2, z2y3);
        uint64x2_t comb5b = vzip2q_u64(z2y2, z2y3);
        uint64x2_t comb6a = vzip1q_u64(z3y0, z3y1);
        uint64x2_t comb6b = vzip2q_u64(z3y0, z3y1);
        uint64x2_t comb7a = vzip1q_u64(z3y2, z3y3);
        uint64x2_t comb7b = vzip2q_u64(z3y2, z3y3);

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 bits need no handling.
        uint64x2x4_t out0 = { { comb0a, comb0b, comb2a, comb2b } };
        uint64x2x4_t out1 = { { comb1a, comb1b, comb3a, comb3b } };
        uint64x2x4_t out2 = { { comb4a, comb4b, comb6a, comb6b } };
        uint64x2x4_t out3 = { { comb5a, comb5b, comb7a, comb7b } };

        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_3D_8BPE_NEON
{
    MicroSw_3D_8BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, X0, Y0, Z0, X1, Z1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {4, 2, 4};
    static constexpr UINT_32       BpeLog2 = 3;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 slices * 2 rows * (4 col * 64b = 256b)
        // ARM can do a 256b load/store, but the actual values are 128b.
        // Each reg becomes: [ X0 0 0 0 ]
        uint32x4x2_t z0y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0))));
        uint32x4x2_t z0y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + bufStrideY)));
        uint32x4x2_t z1y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1))));
        uint32x4x2_t z1y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + bufStrideY)));
        uint32x4x2_t z2y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2))));
        uint32x4x2_t z2y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + bufStrideY)));
        uint32x4x2_t z3y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3))));
        uint32x4x2_t z3y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + bufStrideY)));

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 bits need no handling.
        uint32x4x4_t out0 = { { z0y0.val[0], z0y1.val[0], z1y0.val[0], z1y1.val[0] } };
        uint32x4x4_t out1 = { { z0y0.val[1], z0y1.val[1], z1y0.val[1], z1y1.val[1] } };
        uint32x4x4_t out2 = { { z2y0.val[0], z2y1.val[0], z3y0.val[0], z3y1.val[0] } };
        uint32x4x4_t out3 = { { z2y0.val[1], z2y1.val[1], z3y0.val[1], z3y1.val[1] } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_3D_16BPE_NEON
{
    MicroSw_3D_16BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, 0, 0, X0, Z0, Y0, Z1 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {2, 2, 4};
    static constexpr UINT_32       BpeLog2 = 4;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 4 slices * 2 rows * (4 col * 64b = 256b)
        // ARM can do a 256b load/store, but the actual values are 128b.
        uint32x4x2_t z0y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0))));
        uint32x4x2_t z0y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 0) + bufStrideY)));
        uint32x4x2_t z1y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1))));
        uint32x4x2_t z1y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 1) + bufStrideY)));
        uint32x4x2_t z2y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2))));
        uint32x4x2_t z2y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 2) + bufStrideY)));
        uint32x4x2_t z3y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3))));
        uint32x4x2_t z3y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideZ * 3) + bufStrideY)));

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 bits need no handling.
        uint32x4x4_t out0 = { { z0y0.val[0], z0y0.val[1], z1y0.val[0], z1y0.val[1] } };
        uint32x4x4_t out1 = { { z0y1.val[0], z0y1.val[1], z1y1.val[0], z1y1.val[1] } };
        uint32x4x4_t out2 = { { z2y0.val[0], z2y0.val[1], z3y0.val[0], z3y0.val[1] } };
        uint32x4x4_t out3 = { { z2y1.val[0], z2y1.val[1], z3y1.val[0], z3y1.val[1] } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_R_1BPE_NEON
{
    MicroSw_R_1BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, X2, X3, Y0, Y1, Y2, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 4;
    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Each reg becomes: [ X3 X2 X1 X0 ]
        uint32x4_t y0 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint32x4_t y1 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint32x4_t y2 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint32x4_t y3 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint32x4_t y4 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint32x4_t y5 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint32x4_t y6 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint32x4_t y7 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));
        uint32x4_t y8 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 8))));
        uint32x4_t y9 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 9))));
        uint32x4_t y10 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 10))));
        uint32x4_t y11 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 11))));
        uint32x4_t y12 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 12))));
        uint32x4_t y13 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 13))));
        uint32x4_t y14 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 14))));
        uint32x4_t y15 = vld1q_u32(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 15))));

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 are identity.
        uint32x4x4_t out0 = { { y0, y1, y2, y3} };
        uint32x4x4_t out1 = { { y4, y5, y6, y7 } };
        uint32x4x4_t out2 = { { y8, y9, y10, y11} };
        uint32x4x4_t out3 = { { y12, y13, y14, y15} };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_R_2BPE_NEON
{
    MicroSw_R_2BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, X0, X1, X2, Y0, Y1, Y2, X3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 8, 1};
    static constexpr UINT_32       BpeLog2 = 1;
    static constexpr UINT_32       ExpandX = 4;
    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (16 col * 16b = 256b)
        // ARM can do a 256b load/store, but the actual values are 128b.
        // Each reg becomes: [ X2 X1 X0 0 ]
        uint32x4x2_t y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint32x4x2_t y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint32x4x2_t y2 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint32x4x2_t y3 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint32x4x2_t y4 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint32x4x2_t y5 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint32x4x2_t y6 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint32x4x2_t y7 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 are identity.
        uint32x4x4_t out0 = { { y0.val[0], y1.val[0], y2.val[0], y3.val[0]} };
        uint32x4x4_t out1 = { { y4.val[0], y5.val[0], y6.val[0], y7.val[0] } };
        uint32x4x4_t out2 = { { y0.val[1], y1.val[1], y2.val[1], y3.val[1]} };
        uint32x4x4_t out3 = { { y4.val[1], y5.val[1], y6.val[1], y7.val[1] } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_R_4BPE_NEON
{
    MicroSw_R_4BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { 0, 0, X0, X1, Y0, Y1, X2, Y2 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {8, 8, 1};
    static constexpr UINT_32       BpeLog2 = 2;
    static constexpr UINT_32       ExpandX = 4;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 8 rows * (16 col * 16b = 256b)
        // ARM can do a 256b load/store, but the actual values are 128b.
        uint32x4x2_t y0 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint32x4x2_t y1 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint32x4x2_t y2 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint32x4x2_t y3 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint32x4x2_t y4 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint32x4x2_t y5 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint32x4x2_t y6 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint32x4x2_t y7 = vld1q_u32_x2(reinterpret_cast<const uint32_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));

        // The top 4 bits of the swizzle are handled by plain reg moves here. The bottom 4 are identity.
        uint32x4x4_t out0 = { { y0.val[0], y1.val[0], y2.val[0], y3.val[0]} };
        uint32x4x4_t out1 = { { y0.val[1], y1.val[1], y2.val[1], y3.val[1]} };
        uint32x4x4_t out2 = { { y4.val[0], y5.val[0], y6.val[0], y7.val[0] } };
        uint32x4x4_t out3 = { { y4.val[1], y5.val[1], y6.val[1], y7.val[1] } };

        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u32_x4(reinterpret_cast<uint32_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_Z_1BPE_NEON
{
    MicroSw_Z_1BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, Y0, X1, Y1, X2, Y2, X3, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 2;

    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Each reg becomes: [ X3 X2 X1 X0 ]
        uint16x8_t y0  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint16x8_t y1  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint16x8_t y2  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint16x8_t y3  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint16x8_t y4  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint16x8_t y5  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint16x8_t y6  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint16x8_t y7  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));
        uint16x8_t y8  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 8))));
        uint16x8_t y9  = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 9))));
        uint16x8_t y10 = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 10))));
        uint16x8_t y11 = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 11))));
        uint16x8_t y12 = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 12))));
        uint16x8_t y13 = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 13))));
        uint16x8_t y14 = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 14))));
        uint16x8_t y15 = vld1q_u16(reinterpret_cast<const uint16_t*>(VoidPtrInc(pBuf, (bufStrideY * 15))));

        // First do a 16-bit interleave across y0 and recast to 64-bits (recast is a nop, just C++-ness)
        // Each reg becomes: [ X2 X1 Y0 X0 ]
        uint64x2_t firstcomb0a = vreinterpretq_u64_u16(vzip1q_u16(y0, y1));
        uint64x2_t firstcomb0b = vreinterpretq_u64_u16(vzip2q_u16(y0, y1));
        uint64x2_t firstcomb1a = vreinterpretq_u64_u16(vzip1q_u16(y2, y3));
        uint64x2_t firstcomb1b = vreinterpretq_u64_u16(vzip2q_u16(y2, y3));
        uint64x2_t firstcomb2a = vreinterpretq_u64_u16(vzip1q_u16(y4, y5));
        uint64x2_t firstcomb2b = vreinterpretq_u64_u16(vzip2q_u16(y4, y5));
        uint64x2_t firstcomb3a = vreinterpretq_u64_u16(vzip1q_u16(y6, y7));
        uint64x2_t firstcomb3b = vreinterpretq_u64_u16(vzip2q_u16(y6, y7));
        uint64x2_t firstcomb4a = vreinterpretq_u64_u16(vzip1q_u16(y8, y9));
        uint64x2_t firstcomb4b = vreinterpretq_u64_u16(vzip2q_u16(y8, y9));
        uint64x2_t firstcomb5a = vreinterpretq_u64_u16(vzip1q_u16(y10, y11));
        uint64x2_t firstcomb5b = vreinterpretq_u64_u16(vzip2q_u16(y10, y11));
        uint64x2_t firstcomb6a = vreinterpretq_u64_u16(vzip1q_u16(y12, y13));
        uint64x2_t firstcomb6b = vreinterpretq_u64_u16(vzip2q_u16(y12, y13));
        uint64x2_t firstcomb7a = vreinterpretq_u64_u16(vzip1q_u16(y14, y15));
        uint64x2_t firstcomb7b = vreinterpretq_u64_u16(vzip2q_u16(y14, y15));

        // Then do a 64-bit interleave across y1
        // Each reg becomes: [ Y1 X1 Y0 X0 ]
        uint64x2_t comb0a = vzip1q_u64(firstcomb0a, firstcomb1a);
        uint64x2_t comb0b = vzip2q_u64(firstcomb0a, firstcomb1a);
        uint64x2_t comb1a = vzip1q_u64(firstcomb0b, firstcomb1b);
        uint64x2_t comb1b = vzip2q_u64(firstcomb0b, firstcomb1b);
        uint64x2_t comb2a = vzip1q_u64(firstcomb2a, firstcomb3a);
        uint64x2_t comb2b = vzip2q_u64(firstcomb2a, firstcomb3a);
        uint64x2_t comb3a = vzip1q_u64(firstcomb2b, firstcomb3b);
        uint64x2_t comb3b = vzip2q_u64(firstcomb2b, firstcomb3b);
        uint64x2_t comb4a = vzip1q_u64(firstcomb4a, firstcomb5a);
        uint64x2_t comb4b = vzip2q_u64(firstcomb4a, firstcomb5a);
        uint64x2_t comb5a = vzip1q_u64(firstcomb4b, firstcomb5b);
        uint64x2_t comb5b = vzip2q_u64(firstcomb4b, firstcomb5b);
        uint64x2_t comb6a = vzip1q_u64(firstcomb6a, firstcomb7a);
        uint64x2_t comb6b = vzip2q_u64(firstcomb6a, firstcomb7a);
        uint64x2_t comb7a = vzip1q_u64(firstcomb6b, firstcomb7b);
        uint64x2_t comb7b = vzip2q_u64(firstcomb6b, firstcomb7b);

        // Finally, the top 4 bits of the swizzle are handled by plain reg moves here.
        uint64x2x4_t out0 = { { comb0a, comb0b, comb2a, comb2b } };
        uint64x2x4_t out1 = { { comb1a, comb1b, comb3a, comb3b } };
        uint64x2x4_t out2 = { { comb4a, comb4b, comb6a, comb6b } };
        uint64x2x4_t out3 = { { comb5a, comb5b, comb7a, comb7b } };

        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};

class MicroSw_D_1BPE_NEON
{
    MicroSw_D_1BPE_NEON() = delete;
public:
    static constexpr UINT_64       MicroEq[8] = { X0, X1, X2, Y1, Y0, Y2, X3, Y3 };
    static constexpr ADDR_EXTENT3D MicroBlockExtent = {16, 16, 1};
    static constexpr UINT_32       BpeLog2 = 0;
    static constexpr UINT_32       ExpandX = 4;
    NEON_FUNC static void CopyMicroBlock(
        void*       pImgMicroblock,  // Microblock to write to
        const void* pBuf,            // Pointer to data starting from the first pixel of this block
        size_t      bufStrideY,      // Stride of each row in pBuf
        size_t      bufStrideZ)      // Stride of each slice in pBuf
    {
        // Unaligned buffer loads for 16 rows * (16 col * 8b = 128b)
        // Each reg becomes: [ X3 X2 X1 X0 ]
        uint64x2_t y0  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 0))));
        uint64x2_t y1  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 1))));
        uint64x2_t y2  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 2))));
        uint64x2_t y3  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 3))));
        uint64x2_t y4  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 4))));
        uint64x2_t y5  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 5))));
        uint64x2_t y6  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 6))));
        uint64x2_t y7  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 7))));
        uint64x2_t y8  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 8))));
        uint64x2_t y9  = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 9))));
        uint64x2_t y10 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 10))));
        uint64x2_t y11 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 11))));
        uint64x2_t y12 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 12))));
        uint64x2_t y13 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 13))));
        uint64x2_t y14 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 14))));
        uint64x2_t y15 = vld1q_u64(reinterpret_cast<const uint64_t*>(VoidPtrInc(pBuf, (bufStrideY * 15))));

        // Do a 64-bit zip/unpack operation to interleave within each 128b reg.
        // Each reg becomes: [ Y1 X2 X1 X0 ]
        uint64x2_t comb0a = vzip1q_u64(y0, y2);
        uint64x2_t comb0b = vzip2q_u64(y0, y2);
        uint64x2_t comb1a = vzip1q_u64(y1, y3);
        uint64x2_t comb1b = vzip2q_u64(y1, y3);
        uint64x2_t comb2a = vzip1q_u64(y4, y6);
        uint64x2_t comb2b = vzip2q_u64(y4, y6);
        uint64x2_t comb3a = vzip1q_u64(y5, y7);
        uint64x2_t comb3b = vzip2q_u64(y5, y7);
        uint64x2_t comb4a = vzip1q_u64(y8, y10);
        uint64x2_t comb4b = vzip2q_u64(y8, y10);
        uint64x2_t comb5a = vzip1q_u64(y9, y11);
        uint64x2_t comb5b = vzip2q_u64(y9, y11);
        uint64x2_t comb6a = vzip1q_u64(y12, y14);
        uint64x2_t comb6b = vzip2q_u64(y12, y14);
        uint64x2_t comb7a = vzip1q_u64(y13, y15);
        uint64x2_t comb7b = vzip2q_u64(y13, y15);

        // The top 4 bits of the swizzle are handled by plain reg moves here.
        uint64x2x4_t out0 = { { comb0a, comb1a, comb2a, comb3a } };
        uint64x2x4_t out1 = { { comb0b, comb1b, comb2b, comb3b } };
        uint64x2x4_t out2 = { { comb4a, comb5a, comb6a, comb7a } };
        uint64x2x4_t out3 = { { comb4b, comb5b, comb6b, comb7b } };

        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 0 * 64)), out0);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 1 * 64)), out1);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 2 * 64)), out2);
        vst1q_u64_x4(reinterpret_cast<uint64_t*>(VoidPtrInc(pImgMicroblock, 3 * 64)), out3);
    }
};
#endif // ADDR_HAS_NEON

}
#endif
