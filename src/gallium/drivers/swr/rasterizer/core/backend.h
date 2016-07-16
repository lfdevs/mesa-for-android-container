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
* @file backend.h
*
* @brief Backend handles rasterization, pixel shading and output merger
*        operations.
*
******************************************************************************/
#pragma once

#include "common/os.h"
#include "core/context.h"
#include "core/multisample.h"
#include "rdtsc_core.h"

void ProcessComputeBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t threadGroupId, void*& pSpillFillBuffer);
void ProcessSyncBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData);
void ProcessQueryStatsBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData);
void ProcessClearBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData);
void ProcessStoreTileBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pData);
void ProcessDiscardInvalidateTilesBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pData);
void BackendNullPS(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers);
void InitClearTilesTable();
simdmask ComputeUserClipMask(uint8_t clipMask, float* pUserClipBuffer, simdscalar vI, simdscalar vJ);
void InitBackendFuncTables();
void InitCPSFuncTables();
void CalcSampleBarycentrics(const BarycentricCoeffs& coeffs, SWR_PS_CONTEXT &psContext);

enum SWR_BACKEND_FUNCS
{
    SWR_BACKEND_SINGLE_SAMPLE,
    SWR_BACKEND_MSAA_PIXEL_RATE,
    SWR_BACKEND_MSAA_SAMPLE_RATE,
    SWR_BACKEND_FUNCS_MAX,
};

#if KNOB_SIMD_WIDTH == 8
extern const __m256 vCenterOffsetsX;
extern const __m256 vCenterOffsetsY;
extern const __m256 vULOffsetsX;
extern const __m256 vULOffsetsY;
#define MASK 0xff
#endif

INLINE static uint32_t RasterTileColorOffset(uint32_t sampleNum)
{
    static const uint32_t RasterTileColorOffsets[16]
    { 0,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8),
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 2,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 3,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 4,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 5,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 6,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 7,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 8,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 9,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 10,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 11,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 12,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 13,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 14,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 15,
    };
    assert(sampleNum < 16);
    return RasterTileColorOffsets[sampleNum];
}

INLINE static uint32_t RasterTileDepthOffset(uint32_t sampleNum)
{
    static const uint32_t RasterTileDepthOffsets[16]
    { 0,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8),
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 2,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 3,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 4,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 5,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 6,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 7,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 8,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 9,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 10,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 11,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 12,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 13,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 14,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 15,
    };
    assert(sampleNum < 16);
    return RasterTileDepthOffsets[sampleNum];
}

INLINE static uint32_t RasterTileStencilOffset(uint32_t sampleNum)
{
    static const uint32_t RasterTileStencilOffsets[16]
    { 0,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8),
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 2,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 3,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 4,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 5,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 6,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 7,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 8,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 9,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 10,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 11,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 12,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 13,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 14,
      (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) * 15,
    };
    assert(sampleNum < 16);
    return RasterTileStencilOffsets[sampleNum];
}

template<typename T>
INLINE void generateInputCoverage(const uint64_t *const coverageMask, uint32_t (&inputMask)[KNOB_SIMD_WIDTH], const uint32_t sampleMask)
{

    // will need to update for avx512
    assert(KNOB_SIMD_WIDTH == 8);

    __m256i mask[2];
    __m256i sampleCoverage[2];
    if(T::bIsStandardPattern)
    {
        __m256i src = _mm256_set1_epi32(0);
        __m256i index0 = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0), index1;

        if(T::MultisampleT::numSamples == 1)
        {
            mask[0] = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, -1);
        }
        else if(T::MultisampleT::numSamples == 2)
        {
            mask[0] = _mm256_set_epi32(0, 0, 0, 0, 0, 0, -1, -1);
        }
        else if(T::MultisampleT::numSamples == 4)
        {
            mask[0] = _mm256_set_epi32(0, 0, 0, 0, -1, -1, -1, -1);
        }
        else if(T::MultisampleT::numSamples == 8)
        {
            mask[0] = _mm256_set1_epi32(-1);
        }
        else if(T::MultisampleT::numSamples == 16)
        {
            mask[0] = _mm256_set1_epi32(-1);
            mask[1] = _mm256_set1_epi32(-1);
            index1 = _mm256_set_epi32(15, 14, 13, 12, 11, 10, 9, 8);
        }

        // gather coverage for samples 0-7
        sampleCoverage[0] = _mm256_castps_si256(_simd_mask_i32gather_ps(_mm256_castsi256_ps(src), (const float*)coverageMask, index0, _mm256_castsi256_ps(mask[0]), 8));
        if(T::MultisampleT::numSamples > 8)
        {
            // gather coverage for samples 8-15
            sampleCoverage[1] = _mm256_castps_si256(_simd_mask_i32gather_ps(_mm256_castsi256_ps(src), (const float*)coverageMask, index1, _mm256_castsi256_ps(mask[1]), 8));
        }
    }
    else
    {
        // center coverage is the same for all samples; just broadcast to the sample slots
        uint32_t centerCoverage = ((uint32_t)(*coverageMask) & MASK);
        if(T::MultisampleT::numSamples == 1)
        {
            sampleCoverage[0] = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, centerCoverage);
        }
        else if(T::MultisampleT::numSamples == 2)
        {
            sampleCoverage[0] = _mm256_set_epi32(0, 0, 0, 0, 0, 0, centerCoverage, centerCoverage);
        }
        else if(T::MultisampleT::numSamples == 4)
        {
            sampleCoverage[0] = _mm256_set_epi32(0, 0, 0, 0, centerCoverage, centerCoverage, centerCoverage, centerCoverage);
        }
        else if(T::MultisampleT::numSamples == 8)
        {
            sampleCoverage[0] = _mm256_set1_epi32(centerCoverage);
        }
        else if(T::MultisampleT::numSamples == 16)
        {
            sampleCoverage[0] = _mm256_set1_epi32(centerCoverage);
            sampleCoverage[1] = _mm256_set1_epi32(centerCoverage);
        }
    }

    mask[0] = _mm256_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0xC, 0x8, 0x4, 0x0,
                              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0xC, 0x8, 0x4, 0x0);
    // pull out the the 8bit 4x2 coverage for samples 0-7 into the lower 32 bits of each 128bit lane
    __m256i packedCoverage0 = _simd_shuffle_epi8(sampleCoverage[0], mask[0]);

    __m256i packedCoverage1;
    if(T::MultisampleT::numSamples > 8)
    {
        // pull out the the 8bit 4x2 coverage for samples 8-15 into the lower 32 bits of each 128bit lane
        packedCoverage1 = _simd_shuffle_epi8(sampleCoverage[1], mask[0]);
    }

#if (KNOB_ARCH == KNOB_ARCH_AVX)
    // pack lower 32 bits of each 128 bit lane into lower 64 bits of single 128 bit lane 
    __m256i hiToLow = _mm256_permute2f128_si256(packedCoverage0, packedCoverage0, 0x83);
    __m256 shufRes = _mm256_shuffle_ps(_mm256_castsi256_ps(hiToLow), _mm256_castsi256_ps(hiToLow), _MM_SHUFFLE(1, 1, 0, 1));
    packedCoverage0 = _mm256_castps_si256(_mm256_blend_ps(_mm256_castsi256_ps(packedCoverage0), shufRes, 0xFE));

    __m256i packedSampleCoverage;
    if(T::MultisampleT::numSamples > 8)
    {
        // pack lower 32 bits of each 128 bit lane into upper 64 bits of single 128 bit lane
        hiToLow = _mm256_permute2f128_si256(packedCoverage1, packedCoverage1, 0x83);
        shufRes = _mm256_shuffle_ps(_mm256_castsi256_ps(hiToLow), _mm256_castsi256_ps(hiToLow), _MM_SHUFFLE(1, 1, 0, 1));
        shufRes = _mm256_blend_ps(_mm256_castsi256_ps(packedCoverage1), shufRes, 0xFE);
        packedCoverage1 = _mm256_castps_si256(_mm256_castpd_ps(_mm256_shuffle_pd(_mm256_castps_pd(shufRes), _mm256_castps_pd(shufRes), 0x01)));
        packedSampleCoverage = _mm256_castps_si256(_mm256_blend_ps(_mm256_castsi256_ps(packedCoverage0), _mm256_castsi256_ps(packedCoverage1), 0xFC));
    }
    else
    {
        packedSampleCoverage = packedCoverage0;
    }
#else
    __m256i permMask = _mm256_set_epi32(0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x4, 0x0);
    // pack lower 32 bits of each 128 bit lane into lower 64 bits of single 128 bit lane 
    packedCoverage0 = _mm256_permutevar8x32_epi32(packedCoverage0, permMask);

    __m256i packedSampleCoverage;
    if(T::MultisampleT::numSamples > 8)
    {
        permMask = _mm256_set_epi32(0x7, 0x7, 0x7, 0x7, 0x4, 0x0, 0x7, 0x7);
        // pack lower 32 bits of each 128 bit lane into upper 64 bits of single 128 bit lane
        packedCoverage1 = _mm256_permutevar8x32_epi32(packedCoverage1, permMask);

        // blend coverage masks for samples 0-7 and samples 8-15 into single 128 bit lane
        packedSampleCoverage = _mm256_blend_epi32(packedCoverage0, packedCoverage1, 0x0C);
    }
    else
    {
        packedSampleCoverage = packedCoverage0;
    }
#endif

    for(int32_t i = KNOB_SIMD_WIDTH - 1; i >= 0; i--)
    {
        // convert packed sample coverage masks into single coverage masks for all samples for each pixel in the 4x2
        inputMask[i] = _simd_movemask_epi8(packedSampleCoverage);

        if(!T::bForcedSampleCount)
        {
            // input coverage has to be anded with sample mask if MSAA isn't forced on
            inputMask[i] &= sampleMask;
        }

        // shift to the next pixel in the 4x2
        packedSampleCoverage = _simd_slli_epi32(packedSampleCoverage, 1);
    }
}

template<typename T>
INLINE void generateInputCoverage(const uint64_t *const coverageMask, __m256 &inputCoverage, const uint32_t sampleMask)
{
    uint32_t inputMask[KNOB_SIMD_WIDTH]; 
    generateInputCoverage<T>(coverageMask, inputMask, sampleMask);
    inputCoverage = _simd_castsi_ps(_mm256_set_epi32(inputMask[7], inputMask[6], inputMask[5], inputMask[4], inputMask[3], inputMask[2], inputMask[1], inputMask[0]));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Centroid behaves exactly as follows :
// (1) If all samples in the primitive are covered, the attribute is evaluated at the pixel center (even if the sample pattern does not happen to 
//     have a sample location there).
// (2) Else the attribute is evaluated at the first covered sample, in increasing order of sample index, where sample coverage is after ANDing the 
//     coverage with the SampleMask Rasterizer State.
// (3) If no samples are covered, such as on helper pixels executed off the bounds of a primitive to fill out 2x2 pixel stamps, the attribute is 
//     evaluated as follows : If the SampleMask Rasterizer state is a subset of the samples in the pixel, then the first sample covered by the 
//     SampleMask Rasterizer State is the evaluation point.Otherwise (full SampleMask), the pixel center is the evaluation point.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<typename T>
INLINE void CalcCentroidPos(SWR_PS_CONTEXT &psContext, const uint64_t *const coverageMask, const uint32_t sampleMask,
                            const simdscalar vXSamplePosUL, const simdscalar vYSamplePosUL)
{
    uint32_t inputMask[KNOB_SIMD_WIDTH];
    generateInputCoverage<T>(coverageMask, inputMask, sampleMask);

    // Case (2) - partially covered pixel

    // scan for first covered sample per pixel in the 4x2 span
    unsigned long sampleNum[KNOB_SIMD_WIDTH];
    (inputMask[0] > 0) ? (_BitScanForward(&sampleNum[0], inputMask[0])) : (sampleNum[0] = 0);
    (inputMask[1] > 0) ? (_BitScanForward(&sampleNum[1], inputMask[1])) : (sampleNum[1] = 0);
    (inputMask[2] > 0) ? (_BitScanForward(&sampleNum[2], inputMask[2])) : (sampleNum[2] = 0);
    (inputMask[3] > 0) ? (_BitScanForward(&sampleNum[3], inputMask[3])) : (sampleNum[3] = 0);
    (inputMask[4] > 0) ? (_BitScanForward(&sampleNum[4], inputMask[4])) : (sampleNum[4] = 0);
    (inputMask[5] > 0) ? (_BitScanForward(&sampleNum[5], inputMask[5])) : (sampleNum[5] = 0);
    (inputMask[6] > 0) ? (_BitScanForward(&sampleNum[6], inputMask[6])) : (sampleNum[6] = 0);
    (inputMask[7] > 0) ? (_BitScanForward(&sampleNum[7], inputMask[7])) : (sampleNum[7] = 0);

    // look up and set the sample offsets from UL pixel corner for first covered sample 
    __m256 vXSample = _mm256_set_ps(T::MultisampleT::X(sampleNum[7]),
                                    T::MultisampleT::X(sampleNum[6]),
                                    T::MultisampleT::X(sampleNum[5]),
                                    T::MultisampleT::X(sampleNum[4]),
                                    T::MultisampleT::X(sampleNum[3]),
                                    T::MultisampleT::X(sampleNum[2]),
                                    T::MultisampleT::X(sampleNum[1]),
                                    T::MultisampleT::X(sampleNum[0]));

    __m256 vYSample = _mm256_set_ps(T::MultisampleT::Y(sampleNum[7]),
                                    T::MultisampleT::Y(sampleNum[6]),
                                    T::MultisampleT::Y(sampleNum[5]),
                                    T::MultisampleT::Y(sampleNum[4]),
                                    T::MultisampleT::Y(sampleNum[3]),
                                    T::MultisampleT::Y(sampleNum[2]),
                                    T::MultisampleT::Y(sampleNum[1]),
                                    T::MultisampleT::Y(sampleNum[0]));
    // add sample offset to UL pixel corner
    vXSample = _simd_add_ps(vXSamplePosUL, vXSample);
    vYSample = _simd_add_ps(vYSamplePosUL, vYSample);

    // Case (1) and case (3b) - All samples covered or not covered with full SampleMask
    static const __m256i vFullyCoveredMask = T::MultisampleT::FullSampleMask();
    __m256i vInputCoveragei =  _mm256_set_epi32(inputMask[7], inputMask[6], inputMask[5], inputMask[4], inputMask[3], inputMask[2], inputMask[1], inputMask[0]);
    __m256i vAllSamplesCovered = _simd_cmpeq_epi32(vInputCoveragei, vFullyCoveredMask);

    static const __m256i vZero = _simd_setzero_si();
    const __m256i vSampleMask = _simd_and_si(_simd_set1_epi32(sampleMask), vFullyCoveredMask);
    __m256i vNoSamplesCovered = _simd_cmpeq_epi32(vInputCoveragei, vZero);
    __m256i vIsFullSampleMask = _simd_cmpeq_epi32(vSampleMask, vFullyCoveredMask);
    __m256i vCase3b = _simd_and_si(vNoSamplesCovered, vIsFullSampleMask);

    __m256i vEvalAtCenter = _simd_or_si(vAllSamplesCovered, vCase3b);

    // set the centroid position based on results from above
    psContext.vX.centroid = _simd_blendv_ps(vXSample, psContext.vX.center, _simd_castsi_ps(vEvalAtCenter));
    psContext.vY.centroid = _simd_blendv_ps(vYSample, psContext.vY.center, _simd_castsi_ps(vEvalAtCenter));

    // Case (3a) No samples covered and partial sample mask
    __m256i vSomeSampleMaskSamples = _simd_cmplt_epi32(vSampleMask, vFullyCoveredMask);
    // sample mask should never be all 0's for this case, but handle it anyways
    unsigned long firstCoveredSampleMaskSample = 0;
    (sampleMask > 0) ? (_BitScanForward(&firstCoveredSampleMaskSample, sampleMask)) : (firstCoveredSampleMaskSample = 0);

    __m256i vCase3a = _simd_and_si(vNoSamplesCovered, vSomeSampleMaskSamples);

    vXSample = _simd_set1_ps(T::MultisampleT::X(firstCoveredSampleMaskSample));
    vYSample = _simd_set1_ps(T::MultisampleT::Y(firstCoveredSampleMaskSample));

    // blend in case 3a pixel locations
    psContext.vX.centroid = _simd_blendv_ps(psContext.vX.centroid, vXSample, _simd_castsi_ps(vCase3a));
    psContext.vY.centroid = _simd_blendv_ps(psContext.vY.centroid, vYSample, _simd_castsi_ps(vCase3a));
}

INLINE void CalcCentroidBarycentrics(const BarycentricCoeffs& coeffs, SWR_PS_CONTEXT &psContext,
                                     const simdscalar vXSamplePosUL, const simdscalar vYSamplePosUL)
{
    // evaluate I,J
    psContext.vI.centroid = vplaneps(coeffs.vIa, coeffs.vIb, coeffs.vIc, psContext.vX.centroid, psContext.vY.centroid);
    psContext.vJ.centroid = vplaneps(coeffs.vJa, coeffs.vJb, coeffs.vJc, psContext.vX.centroid, psContext.vY.centroid);
    psContext.vI.centroid = _simd_mul_ps(psContext.vI.centroid, coeffs.vRecipDet);
    psContext.vJ.centroid = _simd_mul_ps(psContext.vJ.centroid, coeffs.vRecipDet);

    // interpolate 1/w
    psContext.vOneOverW.centroid = vplaneps(coeffs.vAOneOverW, coeffs.vBOneOverW, coeffs.vCOneOverW, psContext.vI.centroid, psContext.vJ.centroid);
}

template<typename T>
INLINE uint32_t GetNumOMSamples(SWR_MULTISAMPLE_COUNT blendSampleCount)
{
    // RT has to be single sample if we're in forcedMSAA mode
    if(T::bForcedSampleCount && (T::MultisampleT::sampleCount > SWR_MULTISAMPLE_1X))
    {
        return 1;
    }
    // unless we're forced to single sample, in which case we run the OM at the sample count of the RT
    else if(T::bForcedSampleCount && (T::MultisampleT::sampleCount == SWR_MULTISAMPLE_1X))
    {
        return GetNumSamples(blendSampleCount);
    }
    // else we're in normal MSAA mode and rasterizer and OM are running at the same sample count
    else
    {
        return T::MultisampleT::numSamples;
    }
}

template<typename T>
struct PixelRateZTestLoop
{
    PixelRateZTestLoop(DRAW_CONTEXT *DC, const SWR_TRIANGLE_DESC &Work, const BarycentricCoeffs& Coeffs, const API_STATE& apiState, 
                       uint8_t*& depthBase, uint8_t*& stencilBase, const uint8_t ClipDistanceMask) :
                       work(Work), coeffs(Coeffs), state(apiState), psState(apiState.psState),
                       clipDistanceMask(ClipDistanceMask), pDepthBase(depthBase), pStencilBase(stencilBase) {};
           
    INLINE
    uint32_t operator()(simdscalar& activeLanes, SWR_PS_CONTEXT& psContext, 
                        const CORE_BUCKETS BEDepthBucket, uint32_t currentSimdIn8x8 = 0)
    {
        uint32_t statCount = 0;
        simdscalar anyDepthSamplePassed = _simd_setzero_ps();
        for(uint32_t sample = 0; sample < T::MultisampleT::numCoverageSamples; sample++)
        {
            const uint8_t *pCoverageMask = (uint8_t*)&work.coverageMask[sample];
            vCoverageMask[sample] = _simd_and_ps(activeLanes, vMask(pCoverageMask[currentSimdIn8x8] & MASK));

            if(!_simd_movemask_ps(vCoverageMask[sample]))
            {
                vCoverageMask[sample] = depthPassMask[sample] = stencilPassMask[sample] = _simd_setzero_ps();
                continue;
            }

            RDTSC_START(BEBarycentric);
            // calculate per sample positions
            psContext.vX.sample = _simd_add_ps(psContext.vX.UL, T::MultisampleT::vX(sample));
            psContext.vY.sample = _simd_add_ps(psContext.vY.UL, T::MultisampleT::vY(sample));

            // calc I & J per sample
            CalcSampleBarycentrics(coeffs, psContext);

            if(psState.writesODepth)
            {
                // broadcast and test oDepth(psContext.vZ) written from the PS for each sample
                vZ[sample] = psContext.vZ;
            }
            else
            {
                vZ[sample] = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.sample, psContext.vJ.sample);
                vZ[sample] = state.pfnQuantizeDepth(vZ[sample]);
            }
            RDTSC_STOP(BEBarycentric, 0, 0);

            ///@todo: perspective correct vs non-perspective correct clipping?
            // if clip distances are enabled, we need to interpolate for each sample
            if(clipDistanceMask)
            {
                uint8_t clipMask = ComputeUserClipMask(clipDistanceMask, work.pUserClipBuffer,
                                                       psContext.vI.sample, psContext.vJ.sample);
                vCoverageMask[sample] = _simd_and_ps(vCoverageMask[sample], vMask(~clipMask));
            }

            // offset depth/stencil buffers current sample
            uint8_t *pDepthSample = pDepthBase + RasterTileDepthOffset(sample);
            uint8_t * pStencilSample = pStencilBase + RasterTileStencilOffset(sample);

            // ZTest for this sample
            RDTSC_START(BEDepthBucket);
            depthPassMask[sample] = vCoverageMask[sample];
            stencilPassMask[sample] = vCoverageMask[sample];
            depthPassMask[sample] = DepthStencilTest(&state, work.triFlags.frontFacing, vZ[sample], pDepthSample, 
                                                     vCoverageMask[sample], pStencilSample, &stencilPassMask[sample]);
            RDTSC_STOP(BEDepthBucket, 0, 0);

            // early-exit if no pixels passed depth or earlyZ is forced on
            if(psState.forceEarlyZ || !_simd_movemask_ps(depthPassMask[sample]))
            {
                DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, vZ[sample],
                                  pDepthSample, depthPassMask[sample], vCoverageMask[sample], pStencilSample, stencilPassMask[sample]);

                if(!_simd_movemask_ps(depthPassMask[sample]))
                {
                    continue;
                }
            }
            anyDepthSamplePassed = _simd_or_ps(anyDepthSamplePassed, depthPassMask[sample]);
            uint32_t statMask = _simd_movemask_ps(depthPassMask[sample]);
            statCount += _mm_popcnt_u32(statMask);
        }

        activeLanes = _simd_and_ps(anyDepthSamplePassed, activeLanes);
        // return number of samples that passed depth and coverage
        return statCount;
    }

    // saved depth/stencil/coverage masks and interpolated Z used in OM and DepthWrite
    simdscalar vZ[T::MultisampleT::numCoverageSamples];
    simdscalar vCoverageMask[T::MultisampleT::numCoverageSamples];
    simdscalar depthPassMask[T::MultisampleT::numCoverageSamples];
    simdscalar stencilPassMask[T::MultisampleT::numCoverageSamples];

private:
    // functor inputs
    const SWR_TRIANGLE_DESC& work;
    const BarycentricCoeffs& coeffs;
    const API_STATE& state;
    const SWR_PS_STATE& psState;
    const uint8_t clipDistanceMask;
    uint8_t*& pDepthBase;
    uint8_t*& pStencilBase;
};

INLINE void CalcPixelBarycentrics(const BarycentricCoeffs& coeffs, SWR_PS_CONTEXT &psContext)
{
    // evaluate I,J
    psContext.vI.center = vplaneps(coeffs.vIa, coeffs.vIb, coeffs.vIc, psContext.vX.center, psContext.vY.center);
    psContext.vJ.center = vplaneps(coeffs.vJa, coeffs.vJb, coeffs.vJc, psContext.vX.center, psContext.vY.center);
    psContext.vI.center = _simd_mul_ps(psContext.vI.center, coeffs.vRecipDet);
    psContext.vJ.center = _simd_mul_ps(psContext.vJ.center, coeffs.vRecipDet);

    // interpolate 1/w
    psContext.vOneOverW.center = vplaneps(coeffs.vAOneOverW, coeffs.vBOneOverW, coeffs.vCOneOverW, psContext.vI.center, psContext.vJ.center);
}

INLINE void CalcSampleBarycentrics(const BarycentricCoeffs& coeffs, SWR_PS_CONTEXT &psContext)
{
    // evaluate I,J
    psContext.vI.sample = vplaneps(coeffs.vIa, coeffs.vIb, coeffs.vIc, psContext.vX.sample, psContext.vY.sample);
    psContext.vJ.sample = vplaneps(coeffs.vJa, coeffs.vJb, coeffs.vJc, psContext.vX.sample, psContext.vY.sample);
    psContext.vI.sample = _simd_mul_ps(psContext.vI.sample, coeffs.vRecipDet);
    psContext.vJ.sample = _simd_mul_ps(psContext.vJ.sample, coeffs.vRecipDet);

    // interpolate 1/w
    psContext.vOneOverW.sample = vplaneps(coeffs.vAOneOverW, coeffs.vBOneOverW, coeffs.vCOneOverW, psContext.vI.sample, psContext.vJ.sample);
}

INLINE void OutputMerger(SWR_PS_CONTEXT &psContext, uint8_t* (&pColorBase)[SWR_NUM_RENDERTARGETS], uint32_t sample, const SWR_BLEND_STATE *pBlendState,
                         const PFN_BLEND_JIT_FUNC (&pfnBlendFunc)[SWR_NUM_RENDERTARGETS], simdscalar &coverageMask, simdscalar depthPassMask, const uint32_t NumRT)
{
    // type safety guaranteed from template instantiation in BEChooser<>::GetFunc
    const uint32_t rasterTileColorOffset = RasterTileColorOffset(sample);
    simdvector blendOut;

    for(uint32_t rt = 0; rt < NumRT; ++rt)
    {
        uint8_t *pColorSample = pColorBase[rt] + rasterTileColorOffset;

        const SWR_RENDER_TARGET_BLEND_STATE *pRTBlend = &pBlendState->renderTarget[rt];
        // pfnBlendFunc may not update all channels.  Initialize with PS output.
        /// TODO: move this into the blend JIT.
        blendOut = psContext.shaded[rt];

        // Blend outputs and update coverage mask for alpha test
        if(pfnBlendFunc[rt] != nullptr)
        {
            pfnBlendFunc[rt](
                pBlendState,
                psContext.shaded[rt],
                psContext.shaded[1],
                sample,
                pColorSample,
                blendOut,
                &psContext.oMask,
                (simdscalari*)&coverageMask);
        }

        // final write mask 
        simdscalari outputMask = _simd_castps_si(_simd_and_ps(coverageMask, depthPassMask));

        ///@todo can only use maskstore fast path if bpc is 32. Assuming hot tile is RGBA32_FLOAT.
        static_assert(KNOB_COLOR_HOT_TILE_FORMAT == R32G32B32A32_FLOAT, "Unsupported hot tile format");

        const uint32_t simd = KNOB_SIMD_WIDTH * sizeof(float);

        // store with color mask
        if(!pRTBlend->writeDisableRed)
        {
            _simd_maskstore_ps((float*)pColorSample, outputMask, blendOut.x);
        }
        if(!pRTBlend->writeDisableGreen)
        {
            _simd_maskstore_ps((float*)(pColorSample + simd), outputMask, blendOut.y);
        }
        if(!pRTBlend->writeDisableBlue)
        {
            _simd_maskstore_ps((float*)(pColorSample + simd * 2), outputMask, blendOut.z);
        }
        if(!pRTBlend->writeDisableAlpha)
        {
            _simd_maskstore_ps((float*)(pColorSample + simd * 3), outputMask, blendOut.w);
        }
    }
}

template<uint32_t sampleCountT = SWR_MULTISAMPLE_1X, uint32_t samplePattern = SWR_MSAA_STANDARD_PATTERN,
         uint32_t coverage = 0, uint32_t centroid = 0, uint32_t forced = 0, uint32_t canEarlyZ = 0>
struct SwrBackendTraits
{
    static const bool bIsStandardPattern = (samplePattern == SWR_MSAA_STANDARD_PATTERN);
    static const bool bInputCoverage = (coverage == 1);
    static const bool bCentroidPos = (centroid == 1);
    static const bool bForcedSampleCount = (forced == 1);
    static const bool bCanEarlyZ = (canEarlyZ == 1);
    typedef MultisampleTraits<(SWR_MULTISAMPLE_COUNT)sampleCountT, (bIsStandardPattern) ? SWR_MSAA_STANDARD_PATTERN : SWR_MSAA_CENTER_PATTERN> MultisampleT;
};
