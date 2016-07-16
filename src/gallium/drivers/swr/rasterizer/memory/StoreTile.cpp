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
* @file StoreTile.cpp
* 
* @brief Functionality for Store.
* 
******************************************************************************/
#include "common/os.h"
#include "common/formats.h"
#include "core/context.h"
#include "core/rdtsc_core.h"
#include "core/format_conversion.h"

#include "memory/TilingFunctions.h"
#include "memory/tilingtraits.h"
#include "memory/Convert.h"
#include "core/multisample.h"

#include <array>
#include <sstream>

typedef void(*PFN_STORE_TILES)(uint8_t*, SWR_SURFACE_STATE*, uint32_t, uint32_t, uint32_t);

//////////////////////////////////////////////////////////////////////////
/// Store Raster Tile Function Tables.
//////////////////////////////////////////////////////////////////////////
static PFN_STORE_TILES sStoreTilesTableColor[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS] = {};
static PFN_STORE_TILES sStoreTilesTableDepth[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS] = {};
static PFN_STORE_TILES sStoreTilesTableStencil[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS] = {};

//////////////////////////////////////////////////////////////////////////
/// StorePixels
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <size_t PixelSize, size_t NumDests>
struct StorePixels
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests]) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<8, 2>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[2])
    {
        // Each 4-pixel row is 4 bytes.
        const uint16_t* pPixSrc = (const uint16_t*)pSrc;

        // Unswizzle from SWR-Z order
        uint16_t* pRow = (uint16_t*)ppDsts[0];
        pRow[0] = pPixSrc[0];
        pRow[1] = pPixSrc[2];

        pRow = (uint16_t*)ppDsts[1];
        pRow[0] = pPixSrc[1];
        pRow[1] = pPixSrc[3];
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<16, 2>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[2])
    {
        // Each 4-pixel row is 8 bytes.
        const uint32_t* pPixSrc = (const uint32_t*)pSrc;

        // Unswizzle from SWR-Z order
        uint32_t* pRow = (uint32_t*)ppDsts[0];
        pRow[0] = pPixSrc[0];
        pRow[1] = pPixSrc[2];

        pRow = (uint32_t*)ppDsts[1];
        pRow[0] = pPixSrc[1];
        pRow[1] = pPixSrc[3];
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<32, 2>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[2])
    {
        // Each 4-pixel row is 16-bytes
        __m128i *pZRow01 = (__m128i*)pSrc;
        __m128i vQuad00 = _mm_load_si128(pZRow01);
        __m128i vQuad01 = _mm_load_si128(pZRow01 + 1);

        __m128i vRow00 = _mm_unpacklo_epi64(vQuad00, vQuad01);
        __m128i vRow10 = _mm_unpackhi_epi64(vQuad00, vQuad01);

        _mm_storeu_si128((__m128i*)ppDsts[0], vRow00);
        _mm_storeu_si128((__m128i*)ppDsts[1], vRow10);
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<64, 4>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[4])
    {
        // Each 4-pixel row is 32 bytes.
        const __m128i* pPixSrc = (const __m128i*)pSrc;

        // order of pointers match SWR-Z layout
        __m128i** pvDsts = (__m128i**)&ppDsts[0];
        *pvDsts[0] = pPixSrc[0];
        *pvDsts[1] = pPixSrc[1];
        *pvDsts[2] = pPixSrc[2];
        *pvDsts[3] = pPixSrc[3];
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<128, 8>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[8])
    {
        // Each 4-pixel row is 64 bytes.
        const __m128i* pPixSrc = (const __m128i*)pSrc;

        // Unswizzle from SWR-Z order
        __m128i** pvDsts = (__m128i**)&ppDsts[0];
        *pvDsts[0] = pPixSrc[0];
        *pvDsts[1] = pPixSrc[2];
        *pvDsts[2] = pPixSrc[1];
        *pvDsts[3] = pPixSrc[3];
        *pvDsts[4] = pPixSrc[4];
        *pvDsts[5] = pPixSrc[6];
        *pvDsts[6] = pPixSrc[5];
        *pvDsts[7] = pPixSrc[7];
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Conversion for SIMD pixel (4x2 or 2x2)
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct ConvertPixelsSOAtoAOS
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        static const uint32_t MAX_RASTER_TILE_BYTES = 128; // 8 pixels * 16 bytes per pixel

        OSALIGNSIMD(uint8_t) soaTile[MAX_RASTER_TILE_BYTES];
        OSALIGNSIMD(uint8_t) aosTile[MAX_RASTER_TILE_BYTES];

        // Convert from SrcFormat --> DstFormat
        simdvector src;
        LoadSOA<SrcFormat>(pSrc, src);
        StoreSOA<DstFormat>(src, soaTile);

        // Convert from SOA --> AOS
        FormatTraits<DstFormat>::TransposeT::Transpose(soaTile, aosTile);

        // Store data into destination
        StorePixels<FormatTraits<DstFormat>::bpp, NumDests>::Store(aosTile, ppDsts);
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Conversion for SIMD pixel (4x2 or 2x2)
/// Specialization for no format conversion
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT Format>
struct ConvertPixelsSOAtoAOS<Format, Format>
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        static const uint32_t MAX_RASTER_TILE_BYTES = 128; // 8 pixels * 16 bytes per pixel

        OSALIGNSIMD(uint8_t) aosTile[MAX_RASTER_TILE_BYTES];

        // Convert from SOA --> AOS
        FormatTraits<Format>::TransposeT::Transpose(pSrc, aosTile);

        // Store data into destination
        StorePixels<FormatTraits<Format>::bpp, NumDests>::Store(aosTile, ppDsts);
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Specialization conversion for B5G6R6_UNORM
//////////////////////////////////////////////////////////////////////////
template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, B5G6R5_UNORM >
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        static const SWR_FORMAT SrcFormat = R32G32B32A32_FLOAT;
        static const SWR_FORMAT DstFormat = B5G6R5_UNORM;
        static const uint32_t MAX_RASTER_TILE_BYTES = 128; // 8 pixels * 16 bytes per pixel

        OSALIGNSIMD(uint8_t) aosTile[MAX_RASTER_TILE_BYTES];

        // Load hot-tile
        simdvector src, dst;
        LoadSOA<SrcFormat>(pSrc, src);

        // deswizzle
        dst.x = src[FormatTraits<DstFormat>::swizzle(0)];
        dst.y = src[FormatTraits<DstFormat>::swizzle(1)];
        dst.z = src[FormatTraits<DstFormat>::swizzle(2)];

        // clamp
        dst.x = Clamp<DstFormat>(dst.x, 0);
        dst.y = Clamp<DstFormat>(dst.y, 1);
        dst.z = Clamp<DstFormat>(dst.z, 2);

        // normalize
        dst.x = Normalize<DstFormat>(dst.x, 0);
        dst.y = Normalize<DstFormat>(dst.y, 1);
        dst.z = Normalize<DstFormat>(dst.z, 2);

        // pack
        simdscalari packed = _simd_castps_si(dst.x);
        packed = _simd_or_si(packed, _simd_slli_epi32(_simd_castps_si(dst.y), FormatTraits<DstFormat>::GetBPC(0)));
        packed = _simd_or_si(packed, _simd_slli_epi32(_simd_castps_si(dst.z), FormatTraits<DstFormat>::GetBPC(0) +
                                                                              FormatTraits<DstFormat>::GetBPC(1)));

        // pack low 16 bits of each 32 bit lane to low 128 bits of dst
        uint32_t *pPacked = (uint32_t*)&packed;
        uint16_t *pAosTile = (uint16_t*)&aosTile[0];
        for (uint32_t t = 0; t < KNOB_SIMD_WIDTH; ++t)
        {
            *pAosTile++ = *pPacked++;
        }

        // Store data into destination
        StorePixels<FormatTraits<DstFormat>::bpp, NumDests>::Store(aosTile, ppDsts);
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Conversion for SIMD pixel (4x2 or 2x2)
//////////////////////////////////////////////////////////////////////////
template<>
struct ConvertPixelsSOAtoAOS<R32_FLOAT, R24_UNORM_X8_TYPELESS>
{
    static const SWR_FORMAT SrcFormat = R32_FLOAT;
    static const SWR_FORMAT DstFormat = R24_UNORM_X8_TYPELESS;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        static const uint32_t MAX_RASTER_TILE_BYTES = 128; // 8 pixels * 16 bytes per pixel

        OSALIGNSIMD(uint8_t) soaTile[MAX_RASTER_TILE_BYTES];
        OSALIGNSIMD(uint8_t) aosTile[MAX_RASTER_TILE_BYTES];

        // Convert from SrcFormat --> DstFormat
        simdvector src;
        LoadSOA<SrcFormat>(pSrc, src);
        StoreSOA<DstFormat>(src, soaTile);

        // Convert from SOA --> AOS
        FormatTraits<DstFormat>::TransposeT::Transpose(soaTile, aosTile);

        // Store data into destination but don't overwrite the X8 bits
        // Each 4-pixel row is 16-bytes
        __m128i *pZRow01 = (__m128i*)aosTile;
        __m128i vQuad00 = _mm_load_si128(pZRow01);
        __m128i vQuad01 = _mm_load_si128(pZRow01 + 1);

        __m128i vRow00 = _mm_unpacklo_epi64(vQuad00, vQuad01);
        __m128i vRow10 = _mm_unpackhi_epi64(vQuad00, vQuad01);

        __m128i vDst0 = _mm_loadu_si128((const __m128i*)ppDsts[0]);
        __m128i vDst1 = _mm_loadu_si128((const __m128i*)ppDsts[1]);

        __m128i vMask = _mm_set1_epi32(0xFFFFFF);

        vDst0 = _mm_andnot_si128(vMask, vDst0);
        vDst0 = _mm_or_si128(vDst0, _mm_and_si128(vRow00, vMask));
        vDst1 = _mm_andnot_si128(vMask, vDst1);
        vDst1 = _mm_or_si128(vDst1, _mm_and_si128(vRow10, vMask));

        _mm_storeu_si128((__m128i*)ppDsts[0], vDst0);
        _mm_storeu_si128((__m128i*)ppDsts[1], vDst1);
    }
};

template<SWR_FORMAT DstFormat>
INLINE static void FlatConvert(const uint8_t* pSrc, uint8_t* pDst, uint8_t* pDst1)
{
    static const uint32_t offset = sizeof(simdscalar);

    // swizzle rgba -> bgra while we load
    simdscalar vComp0 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(0))*offset)); // float32 rrrrrrrr 
    simdscalar vComp1 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(1))*offset)); // float32 gggggggg
    simdscalar vComp2 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(2))*offset)); // float32 bbbbbbbb 
    simdscalar vComp3 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(3))*offset)); // float32 aaaaaaaa 

    // clamp
    vComp0 = _simd_max_ps(vComp0, _simd_setzero_ps());
    vComp0 = _simd_min_ps(vComp0, _simd_set1_ps(1.0f));

    vComp1 = _simd_max_ps(vComp1, _simd_setzero_ps());
    vComp1 = _simd_min_ps(vComp1, _simd_set1_ps(1.0f));

    vComp2 = _simd_max_ps(vComp2, _simd_setzero_ps());
    vComp2 = _simd_min_ps(vComp2, _simd_set1_ps(1.0f));

    vComp3 = _simd_max_ps(vComp3, _simd_setzero_ps());
    vComp3 = _simd_min_ps(vComp3, _simd_set1_ps(1.0f));

    if (FormatTraits<DstFormat>::isSRGB)
    {
        // Gamma-correct only rgb
        vComp0 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(0, vComp0);
        vComp1 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(1, vComp1);
        vComp2 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(2, vComp2);
    }

    // convert float components from 0.0f .. 1.0f to correct scale for 0 .. 255 dest format
    vComp0 = _simd_mul_ps(vComp0, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(0))); 
    vComp1 = _simd_mul_ps(vComp1, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(1)));
    vComp2 = _simd_mul_ps(vComp2, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(2)));
    vComp3 = _simd_mul_ps(vComp3, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(3)));

    // moving to 8 wide integer vector types
    __m256i src0 = _simd_cvtps_epi32(vComp0); // padded byte rrrrrrrr
    __m256i src1 = _simd_cvtps_epi32(vComp1); // padded byte gggggggg 
    __m256i src2 = _simd_cvtps_epi32(vComp2); // padded byte bbbbbbbb 
    __m256i src3 = _simd_cvtps_epi32(vComp3); // padded byte aaaaaaaa

#if KNOB_ARCH == KNOB_ARCH_AVX

    // splitting into two sets of 4 wide integer vector types
    // because AVX doesn't have instructions to support this operation at 8 wide
    __m128i srcLo0 = _mm256_castsi256_si128(src0); // 000r000r000r000r
    __m128i srcLo1 = _mm256_castsi256_si128(src1); // 000g000g000g000g
    __m128i srcLo2 = _mm256_castsi256_si128(src2); // 000b000b000b000b
    __m128i srcLo3 = _mm256_castsi256_si128(src3); // 000a000a000a000a

    __m128i srcHi0 = _mm256_extractf128_si256(src0, 1); // 000r000r000r000r
    __m128i srcHi1 = _mm256_extractf128_si256(src1, 1); // 000g000g000g000g
    __m128i srcHi2 = _mm256_extractf128_si256(src2, 1); // 000b000b000b000b
    __m128i srcHi3 = _mm256_extractf128_si256(src3, 1); // 000a000a000a000a

    srcLo1 = _mm_slli_si128(srcLo1, 1); // 00g000g000g000g0
    srcHi1 = _mm_slli_si128(srcHi1, 1); // 00g000g000g000g0
    srcLo2 = _mm_slli_si128(srcLo2, 2); // 0b000b000b000b00
    srcHi2 = _mm_slli_si128(srcHi2, 2); // 0b000b000b000b00
    srcLo3 = _mm_slli_si128(srcLo3, 3); // a000a000a000a000
    srcHi3 = _mm_slli_si128(srcHi3, 3); // a000a000a000a000

    srcLo0 = _mm_or_si128(srcLo0, srcLo1); // 00gr00gr00gr00gr
    srcLo2 = _mm_or_si128(srcLo2, srcLo3); // ab00ab00ab00ab00

    srcHi0 = _mm_or_si128(srcHi0, srcHi1); // 00gr00gr00gr00gr
    srcHi2 = _mm_or_si128(srcHi2, srcHi3); // ab00ab00ab00ab00

    srcLo0 = _mm_or_si128(srcLo0, srcLo2); // abgrabgrabgrabgr
    srcHi0 = _mm_or_si128(srcHi0, srcHi2); // abgrabgrabgrabgr

    // unpack into rows that get the tiling order correct
    __m128i vRow00 = _mm_unpacklo_epi64(srcLo0, srcHi0);  // abgrabgrabgrabgrabgrabgrabgrabgr
    __m128i vRow10 = _mm_unpackhi_epi64(srcLo0, srcHi0);

    __m256i final = _mm256_castsi128_si256(vRow00);
    final = _mm256_insertf128_si256(final, vRow10, 1);

#elif KNOB_ARCH == KNOB_ARCH_AVX2

    // logic is as above, only wider
    src1 = _mm256_slli_si256(src1, 1);
    src2 = _mm256_slli_si256(src2, 2);
    src3 = _mm256_slli_si256(src3, 3);

    src0 = _mm256_or_si256(src0, src1);
    src2 = _mm256_or_si256(src2, src3);

    __m256i final = _mm256_or_si256(src0, src2);
        
    // adjust the data to get the tiling order correct 0 1 2 3 -> 0 2 1 3
    final = _mm256_permute4x64_epi64(final, 0xD8);

#endif

    _mm256_storeu2_m128i((__m128i*)pDst1, (__m128i*)pDst, final);
}

template<SWR_FORMAT DstFormat>
INLINE static void FlatConvertNoAlpha(const uint8_t* pSrc, uint8_t* pDst, uint8_t* pDst1)
{
    static const uint32_t offset = sizeof(simdscalar);

    // swizzle rgba -> bgra while we load
    simdscalar vComp0 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(0))*offset)); // float32 rrrrrrrr 
    simdscalar vComp1 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(1))*offset)); // float32 gggggggg
    simdscalar vComp2 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(2))*offset)); // float32 bbbbbbbb 
                                                                                                            // clamp
    vComp0 = _simd_max_ps(vComp0, _simd_setzero_ps());
    vComp0 = _simd_min_ps(vComp0, _simd_set1_ps(1.0f));

    vComp1 = _simd_max_ps(vComp1, _simd_setzero_ps());
    vComp1 = _simd_min_ps(vComp1, _simd_set1_ps(1.0f));

    vComp2 = _simd_max_ps(vComp2, _simd_setzero_ps());
    vComp2 = _simd_min_ps(vComp2, _simd_set1_ps(1.0f));

    if (FormatTraits<DstFormat>::isSRGB)
    {
        // Gamma-correct only rgb
        vComp0 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(0, vComp0);
        vComp1 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(1, vComp1);
        vComp2 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(2, vComp2);
    }

    // convert float components from 0.0f .. 1.0f to correct scale for 0 .. 255 dest format
    vComp0 = _simd_mul_ps(vComp0, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(0)));
    vComp1 = _simd_mul_ps(vComp1, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(1)));
    vComp2 = _simd_mul_ps(vComp2, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(2)));

    // moving to 8 wide integer vector types
    __m256i src0 = _simd_cvtps_epi32(vComp0); // padded byte rrrrrrrr
    __m256i src1 = _simd_cvtps_epi32(vComp1); // padded byte gggggggg 
    __m256i src2 = _simd_cvtps_epi32(vComp2); // padded byte bbbbbbbb 

#if KNOB_ARCH == KNOB_ARCH_AVX

                                              // splitting into two sets of 4 wide integer vector types
                                              // because AVX doesn't have instructions to support this operation at 8 wide
    __m128i srcLo0 = _mm256_castsi256_si128(src0); // 000r000r000r000r
    __m128i srcLo1 = _mm256_castsi256_si128(src1); // 000g000g000g000g
    __m128i srcLo2 = _mm256_castsi256_si128(src2); // 000b000b000b000b

    __m128i srcHi0 = _mm256_extractf128_si256(src0, 1); // 000r000r000r000r
    __m128i srcHi1 = _mm256_extractf128_si256(src1, 1); // 000g000g000g000g
    __m128i srcHi2 = _mm256_extractf128_si256(src2, 1); // 000b000b000b000b

    srcLo1 = _mm_slli_si128(srcLo1, 1); // 00g000g000g000g0
    srcHi1 = _mm_slli_si128(srcHi1, 1); // 00g000g000g000g0
    srcLo2 = _mm_slli_si128(srcLo2, 2); // 0b000b000b000b00
    srcHi2 = _mm_slli_si128(srcHi2, 2); // 0b000b000b000b00

    srcLo0 = _mm_or_si128(srcLo0, srcLo1); // 00gr00gr00gr00gr

    srcHi0 = _mm_or_si128(srcHi0, srcHi1); // 00gr00gr00gr00gr

    srcLo0 = _mm_or_si128(srcLo0, srcLo2); // 0bgr0bgr0bgr0bgr
    srcHi0 = _mm_or_si128(srcHi0, srcHi2); // 0bgr0bgr0bgr0bgr

                                           // unpack into rows that get the tiling order correct
    __m128i vRow00 = _mm_unpacklo_epi64(srcLo0, srcHi0);  // 0bgr0bgr0bgr0bgr0bgr0bgr0bgr0bgr
    __m128i vRow10 = _mm_unpackhi_epi64(srcLo0, srcHi0);

    __m256i final = _mm256_castsi128_si256(vRow00);
    final = _mm256_insertf128_si256(final, vRow10, 1);

#elif KNOB_ARCH == KNOB_ARCH_AVX2

                                              // logic is as above, only wider
    src1 = _mm256_slli_si256(src1, 1);
    src2 = _mm256_slli_si256(src2, 2);

    src0 = _mm256_or_si256(src0, src1);

    __m256i final = _mm256_or_si256(src0, src2);

    // adjust the data to get the tiling order correct 0 1 2 3 -> 0 2 1 3
    final = _mm256_permute4x64_epi64(final, 0xD8);

#endif

    _mm256_storeu2_m128i((__m128i*)pDst1, (__m128i*)pDst, final);
}

template<>
struct ConvertPixelsSOAtoAOS<R32G32B32A32_FLOAT, B8G8R8A8_UNORM>
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<B8G8R8A8_UNORM>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS<R32G32B32A32_FLOAT, B8G8R8X8_UNORM>
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<B8G8R8X8_UNORM>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, B8G8R8A8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<B8G8R8A8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, B8G8R8X8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<B8G8R8X8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8A8_UNORM >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<R8G8B8A8_UNORM>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8X8_UNORM >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<R8G8B8X8_UNORM>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8A8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<R8G8B8A8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8X8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<R8G8B8X8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1]);
    }
};

//////////////////////////////////////////////////////////////////////////
/// StoreRasterTile
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct StoreRasterTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from hot tile source which is always float.
    /// @param pSrc - Pointer to raster tile.
    /// @param x, y - Coordinates to raster tile.
    /// @param output - output color
    INLINE static void GetSwizzledSrcColor(
        uint8_t* pSrc,
        uint32_t x, uint32_t y,
        float outputColor[4])
    {
        typedef SimdTile<SrcFormat, DstFormat> SimdT;

        SimdT* pSrcSimdTiles = (SimdT*)pSrc;

        // Compute which simd tile we're accessing within 8x8 tile.
        //   i.e. Compute linear simd tile coordinate given (x, y) in pixel coordinates.
        uint32_t simdIndex = (y / SIMD_TILE_Y_DIM) * (KNOB_TILE_X_DIM / SIMD_TILE_X_DIM) + (x / SIMD_TILE_X_DIM);

        SimdT* pSimdTile = &pSrcSimdTiles[simdIndex];

        uint32_t simdOffset = (y % SIMD_TILE_Y_DIM) * SIMD_TILE_X_DIM + (x % SIMD_TILE_X_DIM);

        pSimdTile->GetSwizzledColor(simdOffset, outputColor);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex) // (x, y) pixel coordinate to start of raster tile.
    {
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        // For each raster tile pixel (rx, ry)
        for (uint32_t ry = 0; ry < KNOB_TILE_Y_DIM; ++ry)
        {
            for (uint32_t rx = 0; rx < KNOB_TILE_X_DIM; ++rx)
            {
                // Perform bounds checking.
                if (((x + rx) < lodWidth) &&
                    ((y + ry) < lodHeight))
                {
                    float srcColor[4];
                    GetSwizzledSrcColor(pSrc, rx, ry, srcColor);

                    uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false>((x + rx), (y + ry), 
                        pDstSurface->arrayIndex + renderTargetArrayIndex, pDstSurface->arrayIndex + renderTargetArrayIndex, 
                        sampleNum, pDstSurface->lod, pDstSurface);
                    ConvertPixelFromFloat<DstFormat>(pDst, srcColor);
                }
            }
        }
    }
};

template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile : StoreRasterTile<TTraits, SrcFormat, DstFormat>
{};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 8bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 8>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 8>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t* pDst = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex, 
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        uint8_t* ppRows[] = { pDst, pDst + pDstSurface->pitch };

        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM / SIMD_TILE_Y_DIM; ++row)
        {
            uint8_t* ppStartRows[] = { ppRows[0], ppRows[1] };

            for (uint32_t col = 0; col < KNOB_TILE_X_DIM / SIMD_TILE_X_DIM; ++col)
            {
                // Format conversion and convert from SOA to AOS, and store the rows.
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppRows);

                ppRows[0] += KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;
                ppRows[1] += KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;
                pSrc += SRC_BYTES_PER_PIXEL * KNOB_SIMD_WIDTH;
            }

            ppRows[0] = ppStartRows[0] + 2 * pDstSurface->pitch;
            ppRows[1] = ppStartRows[1] + 2 * pDstSurface->pitch;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 16bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 16>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 16>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t* pDst = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex, 
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        uint8_t* ppRows[] = { pDst, pDst + pDstSurface->pitch };

        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM / SIMD_TILE_Y_DIM; ++row)
        {
            uint8_t* ppStartRows[] = { ppRows[0], ppRows[1] };

            for (uint32_t col = 0; col < KNOB_TILE_X_DIM / SIMD_TILE_X_DIM; ++col)
            {
                // Format conversion and convert from SOA to AOS, and store the rows.
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppRows);

                ppRows[0] += KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;
                ppRows[1] += KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;
                pSrc += SRC_BYTES_PER_PIXEL * KNOB_SIMD_WIDTH;
            }

            ppRows[0] = ppStartRows[0] + 2 * pDstSurface->pitch;
            ppRows[1] = ppStartRows[1] + 2 * pDstSurface->pitch;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 32bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 32>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 32>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t* pDst = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        uint8_t* ppRows[] = { pDst, pDst + pDstSurface->pitch };

        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM / SIMD_TILE_Y_DIM; ++row)
        {
            uint8_t* ppStartRows[] = { ppRows[0], ppRows[1] };

            for (uint32_t col = 0; col < KNOB_TILE_X_DIM / SIMD_TILE_X_DIM; ++col)
            {
                // Format conversion and convert from SOA to AOS, and store the rows.
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppRows);

                ppRows[0] += KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;
                ppRows[1] += KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;
                pSrc += SRC_BYTES_PER_PIXEL * KNOB_SIMD_WIDTH;
            }

            ppRows[0] = ppStartRows[0] + 2 * pDstSurface->pitch;
            ppRows[1] = ppStartRows[1] + 2 * pDstSurface->pitch;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 64bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 64>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 64>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t MAX_DST_COLUMN_BYTES = 16;
    static const size_t SRC_COLUMN_BYTES = KNOB_SIMD_WIDTH * SRC_BYTES_PER_PIXEL;
    static const size_t DST_COLUMN_BYTES_PER_SRC = KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t* pDst = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        uint8_t* ppDsts[] =
        {
            pDst,                                               // row 0, col 0
            pDst + pDstSurface->pitch,                          // row 1, col 0
            pDst + MAX_DST_COLUMN_BYTES,                        // row 0, col 1
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES,   // row 1, col 1
        };

        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM / SIMD_TILE_Y_DIM; ++row)
        {
            uint8_t* ppStartRows[] =
            {
                ppDsts[0],
                ppDsts[1],
                ppDsts[2],
                ppDsts[3],
            };

            for (uint32_t col = 0; col < KNOB_TILE_X_DIM / SIMD_TILE_X_DIM; ++col)
            {
                // Format conversion and convert from SOA to AOS, and store the rows.
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

                ppDsts[0] += DST_COLUMN_BYTES_PER_SRC;
                ppDsts[1] += DST_COLUMN_BYTES_PER_SRC;
                ppDsts[2] += DST_COLUMN_BYTES_PER_SRC;
                ppDsts[3] += DST_COLUMN_BYTES_PER_SRC;
                pSrc += SRC_COLUMN_BYTES;
            }

            ppDsts[0] = ppStartRows[0] + 2 * pDstSurface->pitch;
            ppDsts[1] = ppStartRows[1] + 2 * pDstSurface->pitch;
            ppDsts[2] = ppStartRows[2] + 2 * pDstSurface->pitch;
            ppDsts[3] = ppStartRows[3] + 2 * pDstSurface->pitch;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 128bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 128>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 128>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t MAX_DST_COLUMN_BYTES = 16;
    static const size_t SRC_COLUMN_BYTES = KNOB_SIMD_WIDTH * SRC_BYTES_PER_PIXEL;
    static const size_t DST_COLUMN_BYTES_PER_SRC = KNOB_SIMD_WIDTH * DST_BYTES_PER_PIXEL / 2;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t* pDst = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        struct DstPtrs
        {
            uint8_t* ppDsts[8];
        } ptrs;

        // Need 8 pointers, 4 columns of 2 rows each
        for (uint32_t y = 0; y < 2; ++y)
        {
            for (uint32_t x = 0; x < 4; ++x)
            {
                ptrs.ppDsts[x * 2 + y] = pDst + y * pDstSurface->pitch + x * MAX_DST_COLUMN_BYTES;
            }
        }

        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM / SIMD_TILE_Y_DIM; ++row)
        {
            DstPtrs startPtrs = ptrs;

            for (uint32_t col = 0; col < KNOB_TILE_X_DIM / SIMD_TILE_X_DIM; ++col)
            {
                // Format conversion and convert from SOA to AOS, and store the rows.
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ptrs.ppDsts);

                ptrs.ppDsts[0] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[1] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[2] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[3] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[4] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[5] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[6] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[7] += DST_COLUMN_BYTES_PER_SRC;
                pSrc += SRC_COLUMN_BYTES;
            }

            ptrs.ppDsts[0] = startPtrs.ppDsts[0] + 2 * pDstSurface->pitch;
            ptrs.ppDsts[1] = startPtrs.ppDsts[1] + 2 * pDstSurface->pitch;
            ptrs.ppDsts[2] = startPtrs.ppDsts[2] + 2 * pDstSurface->pitch;
            ptrs.ppDsts[3] = startPtrs.ppDsts[3] + 2 * pDstSurface->pitch;
            ptrs.ppDsts[4] = startPtrs.ppDsts[4] + 2 * pDstSurface->pitch;
            ptrs.ppDsts[5] = startPtrs.ppDsts[5] + 2 * pDstSurface->pitch;
            ptrs.ppDsts[6] = startPtrs.ppDsts[6] + 2 * pDstSurface->pitch;
            ptrs.ppDsts[7] = startPtrs.ppDsts[7] + 2 * pDstSurface->pitch;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 8bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 8>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 8>, SrcFormat, DstFormat> GenericStoreTile;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 2 x 4-wide columns in an 8x8 raster tile.
        uint8_t* pCol0 = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        // Increment by a whole SIMD. 4x2 for AVX. 2x2 for SSE.
        uint32_t pSrcInc = (FormatTraits<SrcFormat>::bpp * KNOB_SIMD_WIDTH) / 8;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM; row += SIMD_TILE_Y_DIM)
        {
            uint32_t rowOffset = row * DestRowWidthBytes;

            uint8_t* pRow = pCol0 + rowOffset;
            uint8_t* ppDsts[] = { pRow, pRow + DestRowWidthBytes };

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;

            ppDsts[0] += DestRowWidthBytes / 4;
            ppDsts[1] += DestRowWidthBytes / 4;

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 16bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 16>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 16>, SrcFormat, DstFormat> GenericStoreTile;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 2 x 4-wide columns in an 8x8 raster tile.
        uint8_t* pCol0 = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        // Increment by a whole SIMD. 4x2 for AVX. 2x2 for SSE.
        uint32_t pSrcInc = (FormatTraits<SrcFormat>::bpp * KNOB_SIMD_WIDTH) / 8;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM; row += SIMD_TILE_Y_DIM)
        {
            uint32_t rowOffset = row * DestRowWidthBytes;

            uint8_t* pRow = pCol0 + rowOffset;
            uint8_t* ppDsts[] = { pRow, pRow + DestRowWidthBytes };

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;

            ppDsts[0] += DestRowWidthBytes / 2;
            ppDsts[1] += DestRowWidthBytes / 2;

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_XMAJOR specialization for 32bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_XMAJOR, 32>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_XMAJOR, 32>, SrcFormat, DstFormat> GenericStoreTile;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 512;                   // 512B rows

                                                                     // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileX is a row-major tiling mode where each 4KB tile consist of 8 x 512B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        uint8_t *pRow0 = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex, 
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        uint8_t* pRow1 = pRow0 + DestRowWidthBytes;

        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM; row += SIMD_TILE_Y_DIM)
        {
            for (uint32_t col = 0; col < KNOB_TILE_X_DIM; col += SIMD_TILE_X_DIM)
            {
                uint32_t xRowOffset = col * (FormatTraits<DstFormat>::bpp / 8);

                uint8_t* ppDsts[] = { pRow0 + xRowOffset, pRow1 + xRowOffset };
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

                // Increment by a whole SIMD. 4x2 for AVX. 2x2 for SSE.
                pSrc += (FormatTraits<SrcFormat>::bpp * KNOB_SIMD_WIDTH) / 8;
            }

            pRow0 += (DestRowWidthBytes * 2);
            pRow1 += (DestRowWidthBytes * 2);
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 32bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 32>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 32>, SrcFormat, DstFormat> GenericStoreTile;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows
        static const uint32_t DestColumnBytes = DestRowWidthBytes * 32;  // 16B x 32 rows.

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 2 x 4-wide columns in an 8x8 raster tile.
        uint8_t* pCol0 = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex, 
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        // Increment by a whole SIMD. 4x2 for AVX. 2x2 for SSE.
        uint32_t pSrcInc = (FormatTraits<SrcFormat>::bpp * KNOB_SIMD_WIDTH) / 8;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM; row += SIMD_TILE_Y_DIM)
        {
            uint32_t rowOffset = row * DestRowWidthBytes;

            uint8_t* pRow = pCol0 + rowOffset;
            uint8_t* ppDsts[] = { pRow, pRow + DestRowWidthBytes };

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;

            ppDsts[0] += DestColumnBytes;
            ppDsts[1] += DestColumnBytes;

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 64bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 64>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 64>, SrcFormat, DstFormat> GenericStoreTile;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows
        static const uint32_t DestColumnBytes = DestRowWidthBytes * 32;  // 16B x 32 rows.

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 2 x 4-wide columns in an 8x8 raster tile.
        uint8_t* pCol0 = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex, 
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        uint8_t* pCol1 = pCol0 + DestColumnBytes;

        // There are 4 columns, each 2 pixels wide when we have 64bpp pixels.
        // Increment by a whole SIMD. 4x2 for AVX. 2x2 for SSE.
        uint32_t pSrcInc = (FormatTraits<SrcFormat>::bpp * KNOB_SIMD_WIDTH) / 8;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM; row += SIMD_TILE_Y_DIM)
        {
            uint32_t rowOffset = row * DestRowWidthBytes;
            uint8_t* ppDsts[] =
            {
                pCol0 + rowOffset,
                pCol0 + rowOffset + DestRowWidthBytes,
                pCol1 + rowOffset,
                pCol1 + rowOffset + DestRowWidthBytes,
            };

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;

            ppDsts[0] += DestColumnBytes * 2;
            ppDsts[1] += DestColumnBytes * 2;
            ppDsts[2] += DestColumnBytes * 2;
            ppDsts[3] += DestColumnBytes * 2;

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);
            pSrc += pSrcInc;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_YMAJOR specialization for 128bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 128>, SrcFormat, DstFormat >
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 128>, SrcFormat, DstFormat> GenericStoreTile;
 
    static const size_t TILE_Y_COL_WIDTH_BYTES  = 16;
    static const size_t TILE_Y_ROWS             = 32;
    static const size_t TILE_Y_COL_BYTES        = TILE_Y_ROWS * TILE_Y_COL_WIDTH_BYTES;
    
    static const size_t DST_BYTES_PER_PIXEL     = FormatTraits<DstFormat>::bpp / 8;
    static const size_t SRC_BYTES_PER_PIXEL     = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t MAX_DST_COLUMN_BYTES    = 16;

    static const size_t SRC_COLUMN_BYTES        = KNOB_SIMD_WIDTH * SRC_BYTES_PER_PIXEL;
    static const size_t DST_COLUMN_BYTES_PER_SRC = TILE_Y_COL_BYTES * 4;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x + KNOB_TILE_X_DIM > lodWidth ||
            y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t* pDst = (uint8_t*)ComputeSurfaceAddress<false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex, 
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);
        struct DstPtrs
        {
            uint8_t* ppDsts[8];
        } ptrs;

        // Need 8 pointers, 4 columns of 2 rows each
        for (uint32_t y = 0; y < 2; ++y)
        {
            for (uint32_t x = 0; x < 4; ++x)
            {
                ptrs.ppDsts[x * 2 + y] = pDst + y * TILE_Y_COL_WIDTH_BYTES + x * TILE_Y_COL_BYTES;
            }
        }

        for (uint32_t row = 0; row < KNOB_TILE_Y_DIM / SIMD_TILE_Y_DIM; ++row)
        {
            DstPtrs startPtrs = ptrs;

            for (uint32_t col = 0; col < KNOB_TILE_X_DIM / SIMD_TILE_X_DIM; ++col)
            {
                // Format conversion and convert from SOA to AOS, and store the rows.
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ptrs.ppDsts);

                ptrs.ppDsts[0] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[1] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[2] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[3] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[4] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[5] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[6] += DST_COLUMN_BYTES_PER_SRC;
                ptrs.ppDsts[7] += DST_COLUMN_BYTES_PER_SRC;
                pSrc += SRC_COLUMN_BYTES;
            }

            ptrs.ppDsts[0] = startPtrs.ppDsts[0] + 2 * TILE_Y_COL_WIDTH_BYTES;
            ptrs.ppDsts[1] = startPtrs.ppDsts[1] + 2 * TILE_Y_COL_WIDTH_BYTES;
            ptrs.ppDsts[2] = startPtrs.ppDsts[2] + 2 * TILE_Y_COL_WIDTH_BYTES;
            ptrs.ppDsts[3] = startPtrs.ppDsts[3] + 2 * TILE_Y_COL_WIDTH_BYTES;
            ptrs.ppDsts[4] = startPtrs.ppDsts[4] + 2 * TILE_Y_COL_WIDTH_BYTES;
            ptrs.ppDsts[5] = startPtrs.ppDsts[5] + 2 * TILE_Y_COL_WIDTH_BYTES;
            ptrs.ppDsts[6] = startPtrs.ppDsts[6] + 2 * TILE_Y_COL_WIDTH_BYTES;
            ptrs.ppDsts[7] = startPtrs.ppDsts[7] + 2 * TILE_Y_COL_WIDTH_BYTES;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// StoreMacroTile - Stores a macro tile which consists of raster tiles.
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct StoreMacroTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores a macrotile to the destination surface using safe implementation.
    /// @param pSrc - Pointer to macro tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to macro tile
    static void StoreGeneric(
        uint8_t *pSrcHotTile,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex)
    {
        // Store each raster tile from the hot tile to the destination surface.
        for(uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
        {
            for(uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
            {
                for(uint32_t sampleNum = 0; sampleNum < pDstSurface->numSamples; sampleNum++)
                {
                    StoreRasterTile<TTraits, SrcFormat, DstFormat>::Store (pSrcHotTile, pDstSurface, (x + col), (y + row), sampleNum, 
                        renderTargetArrayIndex);
                    pSrcHotTile += KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<SrcFormat>::bpp / 8);
                }
            }
        }
    }

    typedef void(*PFN_STORE_TILES_INTERNAL)(uint8_t*, SWR_SURFACE_STATE*, uint32_t, uint32_t, uint32_t, uint32_t);
    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores a macrotile to the destination surface.
    /// @param pSrc - Pointer to macro tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to macro tile
    static void Store(
        uint8_t *pSrcHotTile,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex)
    {
        PFN_STORE_TILES_INTERNAL pfnStore[SWR_MAX_NUM_MULTISAMPLES];
        for(uint32_t sampleNum = 0; sampleNum < pDstSurface->numSamples; sampleNum++)
        {
            size_t dstSurfAddress = (size_t)ComputeSurfaceAddress<false>(
                0,
                0,
                pDstSurface->arrayIndex + renderTargetArrayIndex, // z for 3D surfaces
                pDstSurface->arrayIndex + renderTargetArrayIndex, // array index for 2D arrays
                sampleNum,
                pDstSurface->lod,
                pDstSurface);

            // Only support generic store-tile if lod surface doesn't start on a page boundary and is non-linear
            bool bForceGeneric = ((pDstSurface->tileMode != SWR_TILE_NONE) && (0 != (dstSurfAddress & 0xfff))) || (pDstSurface->bInterleavedSamples);

            pfnStore[sampleNum] = (bForceGeneric || KNOB_USE_GENERIC_STORETILE) ? StoreRasterTile<TTraits, SrcFormat, DstFormat>::Store : OptStoreRasterTile<TTraits, SrcFormat, DstFormat>::Store;
        }

        // Store each raster tile from the hot tile to the destination surface.
        for(uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
        {
            for(uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
            {
                for(uint32_t sampleNum = 0; sampleNum < pDstSurface->numSamples; sampleNum++)
                {
                    pfnStore[sampleNum](pSrcHotTile, pDstSurface, (x + col), (y + row), sampleNum, renderTargetArrayIndex);
                    pSrcHotTile += KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<SrcFormat>::bpp / 8);
                }
            }
        }
    }
};

static void BUCKETS_START(UINT id)
{
#ifdef KNOB_ENABLE_RDTSC
    gBucketMgr.StartBucket(id);
#endif
}

static void BUCKETS_STOP(UINT id)
{
#ifdef KNOB_ENABLE_RDTSC
    gBucketMgr.StopBucket(id);
#endif
}

// on demand buckets for store tiles
static std::mutex sBucketMutex;
static std::vector<int32_t> sBuckets(NUM_SWR_FORMATS, -1);

//////////////////////////////////////////////////////////////////////////
/// @brief Deswizzles and stores a full hottile to a render surface
/// @param hPrivateContext - Handle to private DC
/// @param srcFormat - Format for hot tile.
/// @param renderTargetIndex - Index to destination render target
/// @param x, y - Coordinates to raster tile.
/// @param pSrcHotTile - Pointer to Hot Tile
void StoreHotTile(
    SWR_SURFACE_STATE *pDstSurface,
    SWR_FORMAT srcFormat,
    SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
    uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex,
    uint8_t *pSrcHotTile)
{
    if (pDstSurface->type == SURFACE_NULL)
    {
        return;
    }

    // force 0 if requested renderTargetArrayIndex is OOB
    if (renderTargetArrayIndex >= pDstSurface->depth)
    {
        renderTargetArrayIndex = 0;
    }

    PFN_STORE_TILES pfnStoreTiles = nullptr;

    if (renderTargetIndex <= SWR_ATTACHMENT_COLOR7)
    {
        pfnStoreTiles = sStoreTilesTableColor[pDstSurface->tileMode][pDstSurface->format];
    }
    else if (renderTargetIndex == SWR_ATTACHMENT_DEPTH)
    {
        pfnStoreTiles = sStoreTilesTableDepth[pDstSurface->tileMode][pDstSurface->format];
    }
    else
    {
        pfnStoreTiles = sStoreTilesTableStencil[pDstSurface->tileMode][pDstSurface->format];
    }

    if(nullptr == pfnStoreTiles)
    {
        SWR_ASSERT(false, "Invalid pixel format / tile mode for store tiles");
        return;
    }

    // Store a macro tile
#ifdef KNOB_ENABLE_RDTSC
    if (sBuckets[pDstSurface->format] == -1)
    {
        // guard sBuckets update since storetiles is called by multiple threads
        sBucketMutex.lock();
        if (sBuckets[pDstSurface->format] == -1)
        {
            const SWR_FORMAT_INFO& info = GetFormatInfo(pDstSurface->format);
            BUCKET_DESC desc{info.name, "", false, 0xffffffff};
            sBuckets[pDstSurface->format] = gBucketMgr.RegisterBucket(desc);
        }
        sBucketMutex.unlock();
    }
#endif

    BUCKETS_START(sBuckets[pDstSurface->format]);
    pfnStoreTiles(pSrcHotTile, pDstSurface, x, y, renderTargetArrayIndex);
    BUCKETS_STOP(sBuckets[pDstSurface->format]);
}

//////////////////////////////////////////////////////////////////////////
/// InitStoreTilesTable - Helper for setting up the tables.
template <SWR_TILE_MODE TileModeT, size_t NumTileModesT, size_t ArraySizeT>
void InitStoreTilesTableColor(
    PFN_STORE_TILES (&table)[NumTileModesT][ArraySizeT])
{
    table[TileModeT][R32G32B32A32_FLOAT]        = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, R32G32B32A32_FLOAT>::Store;
    table[TileModeT][R32G32B32A32_SINT]         = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, R32G32B32A32_SINT>::Store;
    table[TileModeT][R32G32B32A32_UINT]         = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, R32G32B32A32_UINT>::Store;
    table[TileModeT][R32G32B32X32_FLOAT]        = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, R32G32B32X32_FLOAT>::Store;
    table[TileModeT][R32G32B32_FLOAT]           = StoreMacroTile<TilingTraits<TileModeT, 96>, R32G32B32A32_FLOAT, R32G32B32_FLOAT>::Store;
    table[TileModeT][R32G32B32_SINT]            = StoreMacroTile<TilingTraits<TileModeT, 96>, R32G32B32A32_FLOAT, R32G32B32_SINT>::Store;
    table[TileModeT][R32G32B32_UINT]            = StoreMacroTile<TilingTraits<TileModeT, 96>, R32G32B32A32_FLOAT, R32G32B32_UINT>::Store;
    table[TileModeT][R16G16B16A16_UNORM]        = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R16G16B16A16_UNORM>::Store;
    table[TileModeT][R16G16B16A16_SNORM]        = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R16G16B16A16_SNORM>::Store;
    table[TileModeT][R16G16B16A16_SINT]         = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R16G16B16A16_SINT>::Store;
    table[TileModeT][R16G16B16A16_UINT]         = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R16G16B16A16_UINT>::Store;
    table[TileModeT][R16G16B16A16_FLOAT]        = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R16G16B16A16_FLOAT>::Store;
    table[TileModeT][R32G32_FLOAT]              = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R32G32_FLOAT>::Store;
    table[TileModeT][R32G32_SINT]               = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R32G32_SINT>::Store;
    table[TileModeT][R32G32_UINT]               = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R32G32_UINT>::Store;
    table[TileModeT][R16G16B16X16_UNORM]        = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R16G16B16X16_UNORM>::Store;
    table[TileModeT][R16G16B16X16_FLOAT]        = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, R16G16B16X16_FLOAT>::Store;
    table[TileModeT][B8G8R8A8_UNORM]            = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B8G8R8A8_UNORM>::Store;
    table[TileModeT][B8G8R8A8_UNORM_SRGB]       = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B8G8R8A8_UNORM_SRGB>::Store;
    
    // 101010_2, 565, 555_1, and 444_4 formats force generic store tile for now
    table[TileModeT][R10G10B10A2_UNORM]         = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R10G10B10A2_UNORM>::StoreGeneric;
    table[TileModeT][R10G10B10A2_UNORM_SRGB]    = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R10G10B10A2_UNORM_SRGB>::StoreGeneric;
    table[TileModeT][R10G10B10A2_UINT]          = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R10G10B10A2_UINT>::StoreGeneric;

    table[TileModeT][R8G8B8A8_UNORM]            = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R8G8B8A8_UNORM>::Store;
    table[TileModeT][R8G8B8A8_UNORM_SRGB]       = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R8G8B8A8_UNORM_SRGB>::Store;
    table[TileModeT][R8G8B8A8_SNORM]            = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R8G8B8A8_SNORM>::Store;
    table[TileModeT][R8G8B8A8_SINT]             = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R8G8B8A8_SINT>::Store;
    table[TileModeT][R8G8B8A8_UINT]             = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R8G8B8A8_UINT>::Store;
    table[TileModeT][R16G16_UNORM]              = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R16G16_UNORM>::Store;
    table[TileModeT][R16G16_SNORM]              = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R16G16_SNORM>::Store;
    table[TileModeT][R16G16_SINT]               = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R16G16_SINT>::Store;
    table[TileModeT][R16G16_UINT]               = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R16G16_UINT>::Store;
    table[TileModeT][R16G16_FLOAT]              = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R16G16_FLOAT>::Store;
    
    // 101010_2, 565, 555_1, and 444_4 formats force generic store tile for now
    table[TileModeT][B10G10R10A2_UNORM]         = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B10G10R10A2_UNORM>::StoreGeneric;
    table[TileModeT][B10G10R10A2_UNORM_SRGB]    = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B10G10R10A2_UNORM_SRGB>::StoreGeneric;
    table[TileModeT][R11G11B10_FLOAT]           = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R11G11B10_FLOAT>::StoreGeneric;

    table[TileModeT][R32_SINT]                  = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R32_SINT>::Store;
    table[TileModeT][R32_UINT]                  = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R32_UINT>::Store;
    table[TileModeT][R32_FLOAT]                 = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R32_FLOAT>::Store;
    table[TileModeT][A32_FLOAT]                 = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, A32_FLOAT>::Store;
    table[TileModeT][B8G8R8X8_UNORM]            = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B8G8R8X8_UNORM>::Store;
    table[TileModeT][B8G8R8X8_UNORM_SRGB]       = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B8G8R8X8_UNORM_SRGB>::Store;
    table[TileModeT][R8G8B8X8_UNORM]            = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R8G8B8X8_UNORM>::Store;
    table[TileModeT][R8G8B8X8_UNORM_SRGB]       = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R8G8B8X8_UNORM_SRGB>::Store;
    
    // 101010_2, 565, 555_1, and 444_4 formats force generic store tile for now
    table[TileModeT][B10G10R10X2_UNORM]         = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B10G10R10X2_UNORM>::StoreGeneric;
    table[TileModeT][B5G6R5_UNORM]              = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B5G6R5_UNORM>::Store;
    table[TileModeT][B5G6R5_UNORM_SRGB]         = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B5G6R5_UNORM_SRGB>::StoreGeneric;
    table[TileModeT][B5G5R5A1_UNORM]            = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B5G5R5A1_UNORM>::StoreGeneric;
    table[TileModeT][B5G5R5A1_UNORM_SRGB]       = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B5G5R5A1_UNORM_SRGB>::StoreGeneric;
    table[TileModeT][B4G4R4A4_UNORM]            = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B4G4R4A4_UNORM>::StoreGeneric;
    table[TileModeT][B4G4R4A4_UNORM_SRGB]       = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B4G4R4A4_UNORM_SRGB>::StoreGeneric;

    table[TileModeT][R8G8_UNORM]                = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R8G8_UNORM>::Store;
    table[TileModeT][R8G8_SNORM]                = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R8G8_SNORM>::Store;
    table[TileModeT][R8G8_SINT]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R8G8_SINT>::Store;
    table[TileModeT][R8G8_UINT]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R8G8_UINT>::Store;
    table[TileModeT][R16_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R16_UNORM>::Store;
    table[TileModeT][R16_SNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R16_SNORM>::Store;
    table[TileModeT][R16_SINT]                  = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R16_SINT>::Store;
    table[TileModeT][R16_UINT]                  = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R16_UINT>::Store;
    table[TileModeT][R16_FLOAT]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, R16_FLOAT>::Store;
    table[TileModeT][A16_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, A16_UNORM>::Store;
    table[TileModeT][A16_FLOAT]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, A16_FLOAT>::Store;
    
    // 101010_2, 565, 555_1, and 444_4 formats force generic store tile for now
    table[TileModeT][B5G5R5X1_UNORM]            = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B5G5R5X1_UNORM>::StoreGeneric;
    table[TileModeT][B5G5R5X1_UNORM_SRGB]       = StoreMacroTile<TilingTraits<TileModeT, 16>, R32G32B32A32_FLOAT, B5G5R5X1_UNORM_SRGB>::StoreGeneric;

    table[TileModeT][R8_UNORM]                  = StoreMacroTile<TilingTraits<TileModeT, 8>, R32G32B32A32_FLOAT, R8_UNORM>::Store;
    table[TileModeT][R8_SNORM]                  = StoreMacroTile<TilingTraits<TileModeT, 8>, R32G32B32A32_FLOAT, R8_SNORM>::Store;
    table[TileModeT][R8_SINT]                   = StoreMacroTile<TilingTraits<TileModeT, 8>, R32G32B32A32_FLOAT, R8_SINT>::Store;
    table[TileModeT][R8_UINT]                   = StoreMacroTile<TilingTraits<TileModeT, 8>, R32G32B32A32_FLOAT, R8_UINT>::Store;
    table[TileModeT][A8_UNORM]                  = StoreMacroTile<TilingTraits<TileModeT, 8>, R32G32B32A32_FLOAT, A8_UNORM>::Store;
    table[TileModeT][BC1_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, BC1_UNORM>::Store;
    table[TileModeT][BC2_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, BC2_UNORM>::Store;
    table[TileModeT][BC3_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, BC3_UNORM>::Store;
    table[TileModeT][BC4_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, BC4_UNORM>::Store;
    table[TileModeT][BC5_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, BC5_UNORM>::Store;
    table[TileModeT][BC1_UNORM_SRGB]            = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, BC1_UNORM_SRGB>::Store;
    table[TileModeT][BC2_UNORM_SRGB]            = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, BC2_UNORM_SRGB>::Store;
    table[TileModeT][BC3_UNORM_SRGB]            = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, BC3_UNORM_SRGB>::Store;
    table[TileModeT][R8G8B8_UNORM]              = StoreMacroTile<TilingTraits<TileModeT, 24>, R32G32B32A32_FLOAT, R8G8B8_UNORM>::Store;
    table[TileModeT][R8G8B8_SNORM]              = StoreMacroTile<TilingTraits<TileModeT, 24>, R32G32B32A32_FLOAT, R8G8B8_SNORM>::Store;
    table[TileModeT][BC4_SNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 64>, R32G32B32A32_FLOAT, BC4_SNORM>::Store;
    table[TileModeT][BC5_SNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 128>, R32G32B32A32_FLOAT, BC5_SNORM>::Store;
    table[TileModeT][R16G16B16_FLOAT]           = StoreMacroTile<TilingTraits<TileModeT, 48>, R32G32B32A32_FLOAT, R16G16B16_FLOAT>::Store;
    table[TileModeT][R16G16B16_UNORM]           = StoreMacroTile<TilingTraits<TileModeT, 48>, R32G32B32A32_FLOAT, R16G16B16_UNORM>::Store;
    table[TileModeT][R16G16B16_SNORM]           = StoreMacroTile<TilingTraits<TileModeT, 48>, R32G32B32A32_FLOAT, R16G16B16_SNORM>::Store;
    table[TileModeT][R8G8B8_UNORM_SRGB]         = StoreMacroTile<TilingTraits<TileModeT, 24>, R32G32B32A32_FLOAT, R8G8B8_UNORM_SRGB>::Store;
    table[TileModeT][R16G16B16_UINT]            = StoreMacroTile<TilingTraits<TileModeT, 48>, R32G32B32A32_FLOAT, R16G16B16_UINT>::Store;
    table[TileModeT][R16G16B16_SINT]            = StoreMacroTile<TilingTraits<TileModeT, 48>, R32G32B32A32_FLOAT, R16G16B16_SINT>::Store;

    // 101010_2, 565, 555_1, and 444_4 formats force generic store tile for now
    table[TileModeT][R10G10B10A2_SNORM]         = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R10G10B10A2_SNORM>::StoreGeneric;
    table[TileModeT][R10G10B10A2_SINT]          = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, R10G10B10A2_SINT>::StoreGeneric;
    table[TileModeT][B10G10R10A2_SNORM]         = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B10G10R10A2_SNORM>::StoreGeneric;
    table[TileModeT][B10G10R10A2_UINT]          = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B10G10R10A2_UINT>::StoreGeneric;
    table[TileModeT][B10G10R10A2_SINT]          = StoreMacroTile<TilingTraits<TileModeT, 32>, R32G32B32A32_FLOAT, B10G10R10A2_SINT>::StoreGeneric;

    table[TileModeT][R8G8B8_UINT]               = StoreMacroTile<TilingTraits<TileModeT, 24>, R32G32B32A32_FLOAT, R8G8B8_UINT>::Store;
    table[TileModeT][R8G8B8_SINT]               = StoreMacroTile<TilingTraits<TileModeT, 24>, R32G32B32A32_FLOAT, R8G8B8_SINT>::Store;
}

//////////////////////////////////////////////////////////////////////////
/// INIT_STORE_TILES_TABLE - Helper macro for setting up the tables.
template <SWR_TILE_MODE TileModeT, size_t NumTileModes, size_t ArraySizeT>
void InitStoreTilesTableDepth(
    PFN_STORE_TILES(&table)[NumTileModes][ArraySizeT])
{
    table[TileModeT][R32_FLOAT]                 = StoreMacroTile<TilingTraits<TileModeT, 32>, R32_FLOAT, R32_FLOAT>::Store;
    table[TileModeT][R24_UNORM_X8_TYPELESS]     = StoreMacroTile<TilingTraits<TileModeT, 32>, R32_FLOAT, R24_UNORM_X8_TYPELESS>::Store;
    table[TileModeT][R16_UNORM]                 = StoreMacroTile<TilingTraits<TileModeT, 16>, R32_FLOAT, R16_UNORM>::Store;
}

template <SWR_TILE_MODE TileModeT, size_t NumTileModes, size_t ArraySizeT>
void InitStoreTilesTableStencil(
    PFN_STORE_TILES(&table)[NumTileModes][ArraySizeT])
{
    table[TileModeT][R8_UINT]                   = StoreMacroTile<TilingTraits<TileModeT, 8>, R8_UINT, R8_UINT>::Store;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Sets up tables for StoreTile
void InitSimStoreTilesTable()
{
    memset(sStoreTilesTableColor, 0, sizeof(sStoreTilesTableColor));
    memset(sStoreTilesTableDepth, 0, sizeof(sStoreTilesTableDepth));

    InitStoreTilesTableColor<SWR_TILE_NONE>(sStoreTilesTableColor);
    InitStoreTilesTableDepth<SWR_TILE_NONE>(sStoreTilesTableDepth);
    InitStoreTilesTableStencil<SWR_TILE_NONE>(sStoreTilesTableStencil);

    InitStoreTilesTableColor<SWR_TILE_MODE_YMAJOR>(sStoreTilesTableColor);
    InitStoreTilesTableColor<SWR_TILE_MODE_XMAJOR>(sStoreTilesTableColor);

    InitStoreTilesTableDepth<SWR_TILE_MODE_YMAJOR>(sStoreTilesTableDepth);
    InitStoreTilesTableStencil<SWR_TILE_MODE_WMAJOR>(sStoreTilesTableStencil);

    // special color hot tile -> 8-bit WMAJOR
    sStoreTilesTableColor[SWR_TILE_MODE_WMAJOR][R8_UINT] = StoreMacroTile<TilingTraits<SWR_TILE_MODE_WMAJOR, 8>, R32G32B32A32_FLOAT, R8_UINT>::Store;
}
