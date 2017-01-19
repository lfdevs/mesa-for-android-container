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
* @file backend.cpp
*
* @brief Backend handles rasterization, pixel shading and output merger
*        operations.
*
******************************************************************************/

#include <smmintrin.h>

#include "backend.h"
#include "depthstencil.h"
#include "tilemgr.h"
#include "memory/tilingtraits.h"
#include "core/multisample.h"

#include <algorithm>

typedef void(*PFN_CLEAR_TILES)(DRAW_CONTEXT*, SWR_RENDERTARGET_ATTACHMENT rt, uint32_t, uint32_t, DWORD[4], const SWR_RECT& rect);
static PFN_CLEAR_TILES sClearTilesTable[NUM_SWR_FORMATS];

//////////////////////////////////////////////////////////////////////////
/// @brief Process compute work.
/// @param pDC - pointer to draw context (dispatch).
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param threadGroupId - the linear index for the thread group within the dispatch.
void ProcessComputeBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t threadGroupId, void*& pSpillFillBuffer)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(BEDispatch, pDC->drawId);

    const COMPUTE_DESC* pTaskData = (COMPUTE_DESC*)pDC->pDispatch->GetTasksData();
    SWR_ASSERT(pTaskData != nullptr);

    // Ensure spill fill memory has been allocated.
    size_t spillFillSize = pDC->pState->state.totalSpillFillSize;
    if (spillFillSize && pSpillFillBuffer == nullptr)
    {
        pSpillFillBuffer = pDC->pArena->AllocAlignedSync(spillFillSize, KNOB_SIMD_BYTES);
    }

    const API_STATE& state = GetApiState(pDC);

    SWR_CS_CONTEXT csContext{ 0 };
    csContext.tileCounter = threadGroupId;
    csContext.dispatchDims[0] = pTaskData->threadGroupCountX;
    csContext.dispatchDims[1] = pTaskData->threadGroupCountY;
    csContext.dispatchDims[2] = pTaskData->threadGroupCountZ;
    csContext.pTGSM = pContext->ppScratch[workerId];
    csContext.pSpillFillBuffer = (uint8_t*)pSpillFillBuffer;

    state.pfnCsFunc(GetPrivateState(pDC), &csContext);

    UPDATE_STAT_BE(CsInvocations, state.totalThreadsInGroup);

    AR_END(BEDispatch, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Process shutdown.
/// @param pDC - pointer to draw context (dispatch).
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param threadGroupId - the linear index for the thread group within the dispatch.
void ProcessShutdownBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData)
{
    // Dummy function
}

void ProcessSyncBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData)
{
    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroTile, x, y);
    SWR_ASSERT(x == 0 && y == 0);
}

template<SWR_FORMAT format>
void ClearRasterTile(uint8_t *pTileBuffer, simdvector &value)
{
    auto lambda = [&](int32_t comp)
    {
        FormatTraits<format>::storeSOA(comp, pTileBuffer, value.v[comp]);

        pTileBuffer += (KNOB_SIMD_WIDTH * FormatTraits<format>::GetBPC(comp) / 8);
    };

    const uint32_t numIter = (KNOB_TILE_Y_DIM / SIMD_TILE_Y_DIM) * (KNOB_TILE_X_DIM / SIMD_TILE_X_DIM);

    for (uint32_t i = 0; i < numIter; ++i)
    {
        UnrollerL<0, FormatTraits<format>::numComps, 1>::step(lambda);
    }
}

#if USE_8x2_TILE_BACKEND
template<SWR_FORMAT format>
void ClearRasterTile(uint8_t *pTileBuffer, simd16vector &value)
{
    auto lambda = [&](int32_t comp)
    {
        FormatTraits<format>::storeSOA(comp, pTileBuffer, value.v[comp]);

        pTileBuffer += (KNOB_SIMD16_WIDTH * FormatTraits<format>::GetBPC(comp) / 8);
    };

    const uint32_t numIter = (KNOB_TILE_Y_DIM / SIMD16_TILE_Y_DIM) * (KNOB_TILE_X_DIM / SIMD16_TILE_X_DIM);

    for (uint32_t i = 0; i < numIter; ++i)
    {
        UnrollerL<0, FormatTraits<format>::numComps, 1>::step(lambda);
    }
}

#endif
template<SWR_FORMAT format>
INLINE void ClearMacroTile(DRAW_CONTEXT *pDC, SWR_RENDERTARGET_ATTACHMENT rt, uint32_t macroTile, uint32_t renderTargetArrayIndex, DWORD clear[4], const SWR_RECT& rect)
{
    // convert clear color to hottile format
    // clear color is in RGBA float/uint32
#if USE_8x2_TILE_BACKEND
    simd16vector vClear;
    for (uint32_t comp = 0; comp < FormatTraits<format>::numComps; ++comp)
    {
        simd16scalar vComp;
        vComp = _simd16_load1_ps((const float*)&clear[comp]);
        if (FormatTraits<format>::isNormalized(comp))
        {
            vComp = _simd16_mul_ps(vComp, _simd16_set1_ps(FormatTraits<format>::fromFloat(comp)));
            vComp = _simd16_castsi_ps(_simd16_cvtps_epi32(vComp));
        }
        vComp = FormatTraits<format>::pack(comp, vComp);
        vClear.v[FormatTraits<format>::swizzle(comp)] = vComp;
    }

#else
    simdvector vClear;
    for (uint32_t comp = 0; comp < FormatTraits<format>::numComps; ++comp)
    {
        simdscalar vComp;
        vComp = _simd_load1_ps((const float*)&clear[comp]);
        if (FormatTraits<format>::isNormalized(comp))
        {
            vComp = _simd_mul_ps(vComp, _simd_set1_ps(FormatTraits<format>::fromFloat(comp)));
            vComp = _simd_castsi_ps(_simd_cvtps_epi32(vComp));
        }
        vComp = FormatTraits<format>::pack(comp, vComp);
        vClear.v[FormatTraits<format>::swizzle(comp)] = vComp;
    }

#endif
    uint32_t tileX, tileY;
    MacroTileMgr::getTileIndices(macroTile, tileX, tileY);

    // Init to full macrotile
    SWR_RECT clearTile =
    {
        KNOB_MACROTILE_X_DIM * int32_t(tileX),
        KNOB_MACROTILE_Y_DIM * int32_t(tileY),
        KNOB_MACROTILE_X_DIM * int32_t(tileX + 1),
        KNOB_MACROTILE_Y_DIM * int32_t(tileY + 1),
    };

    // intersect with clear rect
    clearTile &= rect;

    // translate to local hottile origin
    clearTile.Translate(-int32_t(tileX) * KNOB_MACROTILE_X_DIM, -int32_t(tileY) * KNOB_MACROTILE_Y_DIM);

    // Make maximums inclusive (needed for convert to raster tiles)
    clearTile.xmax -= 1;
    clearTile.ymax -= 1;

    // convert to raster tiles
    clearTile.ymin >>= (KNOB_TILE_Y_DIM_SHIFT);
    clearTile.ymax >>= (KNOB_TILE_Y_DIM_SHIFT);
    clearTile.xmin >>= (KNOB_TILE_X_DIM_SHIFT);
    clearTile.xmax >>= (KNOB_TILE_X_DIM_SHIFT);

    const int32_t numSamples = GetNumSamples(pDC->pState->state.rastState.sampleCount);
    // compute steps between raster tile samples / raster tiles / macro tile rows
    const uint32_t rasterTileSampleStep = KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<format>::bpp / 8;
    const uint32_t rasterTileStep = (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<format>::bpp / 8)) * numSamples;
    const uint32_t macroTileRowStep = (KNOB_MACROTILE_X_DIM / KNOB_TILE_X_DIM) * rasterTileStep;
    const uint32_t pitch = (FormatTraits<format>::bpp * KNOB_MACROTILE_X_DIM / 8);

    HOTTILE *pHotTile = pDC->pContext->pHotTileMgr->GetHotTile(pDC->pContext, pDC, macroTile, rt, true, numSamples, renderTargetArrayIndex);
    uint32_t rasterTileStartOffset = (ComputeTileOffset2D< TilingTraits<SWR_TILE_SWRZ, FormatTraits<format>::bpp > >(pitch, clearTile.xmin, clearTile.ymin)) * numSamples;
    uint8_t* pRasterTileRow = pHotTile->pBuffer + rasterTileStartOffset; //(ComputeTileOffset2D< TilingTraits<SWR_TILE_SWRZ, FormatTraits<format>::bpp > >(pitch, x, y)) * numSamples;

    // loop over all raster tiles in the current hot tile
    for (int32_t y = clearTile.ymin; y <= clearTile.ymax; ++y)
    {
        uint8_t* pRasterTile = pRasterTileRow;
        for (int32_t x = clearTile.xmin; x <= clearTile.xmax; ++x)
        {
            for( int32_t sampleNum = 0; sampleNum < numSamples; sampleNum++)
            {
                ClearRasterTile<format>(pRasterTile, vClear);
                pRasterTile += rasterTileSampleStep;
            }
        }
        pRasterTileRow += macroTileRowStep;
    }

    pHotTile->state = HOTTILE_DIRTY;
}


void ProcessClearBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    if (KNOB_FAST_CLEAR)
    {
        CLEAR_DESC *pClear = (CLEAR_DESC*)pUserData;
        SWR_MULTISAMPLE_COUNT sampleCount = pDC->pState->state.rastState.sampleCount;
        uint32_t numSamples = GetNumSamples(sampleCount);

        SWR_ASSERT(pClear->attachmentMask != 0); // shouldn't be here without a reason.

        AR_BEGIN(BEClear, pDC->drawId);

        if (pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR)
        {
            unsigned long rt = 0;
            uint32_t mask = pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR;
            while (_BitScanForward(&rt, mask))
            {
                mask &= ~(1 << rt);

                HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTile(pContext, pDC, macroTile, (SWR_RENDERTARGET_ATTACHMENT)rt, true, numSamples, pClear->renderTargetArrayIndex);

                // All we want to do here is to mark the hot tile as being in a "needs clear" state.
                pHotTile->clearData[0] = *(DWORD*)&(pClear->clearRTColor[0]);
                pHotTile->clearData[1] = *(DWORD*)&(pClear->clearRTColor[1]);
                pHotTile->clearData[2] = *(DWORD*)&(pClear->clearRTColor[2]);
                pHotTile->clearData[3] = *(DWORD*)&(pClear->clearRTColor[3]);
                pHotTile->state = HOTTILE_CLEAR;
            }
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_DEPTH_BIT)
        {
            HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTile(pContext, pDC, macroTile, SWR_ATTACHMENT_DEPTH, true, numSamples, pClear->renderTargetArrayIndex);
            pHotTile->clearData[0] = *(DWORD*)&pClear->clearDepth;
            pHotTile->state = HOTTILE_CLEAR;
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_STENCIL_BIT)
        {
            HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTile(pContext, pDC, macroTile, SWR_ATTACHMENT_STENCIL, true, numSamples, pClear->renderTargetArrayIndex);

            pHotTile->clearData[0] = pClear->clearStencil;
            pHotTile->state = HOTTILE_CLEAR;
        }

        AR_END(BEClear, 1);
    }
    else
    {
        // Legacy clear
        CLEAR_DESC *pClear = (CLEAR_DESC*)pUserData;
        AR_BEGIN(BEClear, pDC->drawId);

        if (pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR)
        {
            DWORD clearData[4];
            clearData[0] = *(DWORD*)&(pClear->clearRTColor[0]);
            clearData[1] = *(DWORD*)&(pClear->clearRTColor[1]);
            clearData[2] = *(DWORD*)&(pClear->clearRTColor[2]);
            clearData[3] = *(DWORD*)&(pClear->clearRTColor[3]);

            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[KNOB_COLOR_HOT_TILE_FORMAT];
            SWR_ASSERT(pfnClearTiles != nullptr);

            unsigned long rt = 0;
            uint32_t mask = pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR;
            while (_BitScanForward(&rt, mask))
            {
                mask &= ~(1 << rt);

                pfnClearTiles(pDC, (SWR_RENDERTARGET_ATTACHMENT)rt, macroTile, pClear->renderTargetArrayIndex, clearData, pClear->rect);
            }
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_DEPTH_BIT)
        {
            DWORD clearData[4];
            clearData[0] = *(DWORD*)&pClear->clearDepth;
            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[KNOB_DEPTH_HOT_TILE_FORMAT];
            SWR_ASSERT(pfnClearTiles != nullptr);

            pfnClearTiles(pDC, SWR_ATTACHMENT_DEPTH, macroTile, pClear->renderTargetArrayIndex, clearData, pClear->rect);
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_STENCIL_BIT)
        {
            DWORD clearData[4];
            clearData[0] = pClear->clearStencil;
            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[KNOB_STENCIL_HOT_TILE_FORMAT];

            pfnClearTiles(pDC, SWR_ATTACHMENT_STENCIL, macroTile, pClear->renderTargetArrayIndex, clearData, pClear->rect);
        }

        AR_END(BEClear, 1);
    }
}

void ProcessStoreTileBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, STORE_TILES_DESC* pDesc, 
    SWR_RENDERTARGET_ATTACHMENT attachment)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(BEStoreTiles, pDC->drawId);

    SWR_FORMAT srcFormat;
    switch (attachment)
    {
    case SWR_ATTACHMENT_COLOR0:
    case SWR_ATTACHMENT_COLOR1:
    case SWR_ATTACHMENT_COLOR2:
    case SWR_ATTACHMENT_COLOR3:
    case SWR_ATTACHMENT_COLOR4:
    case SWR_ATTACHMENT_COLOR5:
    case SWR_ATTACHMENT_COLOR6:
    case SWR_ATTACHMENT_COLOR7: srcFormat = KNOB_COLOR_HOT_TILE_FORMAT; break;
    case SWR_ATTACHMENT_DEPTH: srcFormat = KNOB_DEPTH_HOT_TILE_FORMAT; break;
    case SWR_ATTACHMENT_STENCIL: srcFormat = KNOB_STENCIL_HOT_TILE_FORMAT; break;
    default: SWR_ASSERT(false, "Unknown attachment: %d", attachment); srcFormat = KNOB_COLOR_HOT_TILE_FORMAT; break;
    }

    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroTile, x, y);

    // Only need to store the hottile if it's been rendered to...
    HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTileNoLoad(pContext, pDC, macroTile, attachment, false);
    if (pHotTile)
    {
        // clear if clear is pending (i.e., not rendered to), then mark as dirty for store.
        if (pHotTile->state == HOTTILE_CLEAR)
        {
            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[srcFormat];
            SWR_ASSERT(pfnClearTiles != nullptr);

            pfnClearTiles(pDC, attachment, macroTile, pHotTile->renderTargetArrayIndex, pHotTile->clearData, pDesc->rect);
        }

        if (pHotTile->state == HOTTILE_DIRTY || pDesc->postStoreTileState == (SWR_TILE_STATE)HOTTILE_DIRTY)
        {
            int32_t destX = KNOB_MACROTILE_X_DIM * x;
            int32_t destY = KNOB_MACROTILE_Y_DIM * y;

            pContext->pfnStoreTile(GetPrivateState(pDC), srcFormat,
                attachment, destX, destY, pHotTile->renderTargetArrayIndex, pHotTile->pBuffer);
        }
        

        if (pHotTile->state == HOTTILE_DIRTY || pHotTile->state == HOTTILE_RESOLVED)
        {
            pHotTile->state = (HOTTILE_STATE)pDesc->postStoreTileState;
        }
    }
    AR_END(BEStoreTiles, 1);
}

void ProcessStoreTilesBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pData)
{
    STORE_TILES_DESC *pDesc = (STORE_TILES_DESC*)pData;

    unsigned long rt = 0;
    uint32_t mask = pDesc->attachmentMask;
    while (_BitScanForward(&rt, mask))
    {
        mask &= ~(1 << rt);
        ProcessStoreTileBE(pDC, workerId, macroTile, pDesc, (SWR_RENDERTARGET_ATTACHMENT)rt);
    }
}

void ProcessDiscardInvalidateTilesBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pData)
{
    DISCARD_INVALIDATE_TILES_DESC *pDesc = (DISCARD_INVALIDATE_TILES_DESC *)pData;
    SWR_CONTEXT *pContext = pDC->pContext;

    const int32_t numSamples = GetNumSamples(pDC->pState->state.rastState.sampleCount);

    for (uint32_t i = 0; i < SWR_NUM_ATTACHMENTS; ++i)
    {
        if (pDesc->attachmentMask & (1 << i))
        {
            HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTileNoLoad(
                pContext, pDC, macroTile, (SWR_RENDERTARGET_ATTACHMENT)i, pDesc->createNewTiles, numSamples);
            if (pHotTile)
            {
                pHotTile->state = (HOTTILE_STATE)pDesc->newTileState;
            }
        }
    }
}

#if KNOB_SIMD_WIDTH == 8
const __m256 vCenterOffsetsX = {0.5, 1.5, 0.5, 1.5, 2.5, 3.5, 2.5, 3.5};
const __m256 vCenterOffsetsY = {0.5, 0.5, 1.5, 1.5, 0.5, 0.5, 1.5, 1.5};
const __m256 vULOffsetsX = {0.0, 1.0, 0.0, 1.0, 2.0, 3.0, 2.0, 3.0};
const __m256 vULOffsetsY = {0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0};
#else
#error Unsupported vector width
#endif

simdmask ComputeUserClipMask(uint8_t clipMask, float* pUserClipBuffer, simdscalar vI, simdscalar vJ)
{
    simdscalar vClipMask = _simd_setzero_ps();
    uint32_t numClipDistance = _mm_popcnt_u32(clipMask);

    for (uint32_t i = 0; i < numClipDistance; ++i)
    {
        // pull triangle clip distance values from clip buffer
        simdscalar vA = _simd_broadcast_ss(pUserClipBuffer++);
        simdscalar vB = _simd_broadcast_ss(pUserClipBuffer++);
        simdscalar vC = _simd_broadcast_ss(pUserClipBuffer++);

        // interpolate
        simdscalar vInterp = vplaneps(vA, vB, vC, vI, vJ);
        
        // clip if interpolated clip distance is < 0 || NAN
        simdscalar vCull = _simd_cmp_ps(_simd_setzero_ps(), vInterp, _CMP_NLE_UQ);

        vClipMask = _simd_or_ps(vClipMask, vCull);
    }

    return _simd_movemask_ps(vClipMask);
}

template<typename T>
void BackendSingleSample(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(BESingleSampleBackend, pDC->drawId);
    AR_BEGIN(BESetup, pDC->drawId);

    const API_STATE &state = GetApiState(pDC);

    BarycentricCoeffs coeffs;
    SetupBarycentricCoeffs(&coeffs, work);

    uint8_t *pColorBuffer[SWR_NUM_RENDERTARGETS], *pDepthBuffer, *pStencilBuffer;
    SetupRenderBuffers(pColorBuffer, &pDepthBuffer, &pStencilBuffer, state.psState.numRenderTargets, renderBuffers);

    SWR_PS_CONTEXT psContext;
    SetupPixelShaderContext<T>(&psContext, work);

    AR_END(BESetup, 1);

    psContext.vY.UL     = _simd_add_ps(vULOffsetsY,     _simd_set1_ps(static_cast<float>(y)));
    psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps(static_cast<float>(y)));

    const simdscalar dy = _simd_set1_ps(static_cast<float>(SIMD_TILE_Y_DIM));

    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        psContext.vX.UL     = _simd_add_ps(vULOffsetsX,     _simd_set1_ps(static_cast<float>(x)));
        psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps(static_cast<float>(x)));

        const simdscalar dx = _simd_set1_ps(static_cast<float>(SIMD_TILE_X_DIM));

        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
#if USE_8x2_TILE_BACKEND
            const bool useAlternateOffset = ((xx & SIMD_TILE_X_DIM) != 0);

#endif
            simdmask coverageMask = work.coverageMask[0] & MASK;

            if (coverageMask)
            {
                if (state.depthHottileEnable && state.depthBoundsState.depthBoundsTestEnable)
                {
                    static_assert(KNOB_DEPTH_HOT_TILE_FORMAT == R32_FLOAT, "Unsupported depth hot tile format");

                    const simdscalar z = _simd_load_ps(reinterpret_cast<const float *>(pDepthBuffer));

                    const float minz = state.depthBoundsState.depthBoundsTestMinValue;
                    const float maxz = state.depthBoundsState.depthBoundsTestMaxValue;

                    coverageMask &= CalcDepthBoundsAcceptMask(z, minz, maxz);
                }

                if (T::InputCoverage != SWR_INPUT_COVERAGE_NONE)
                {
                    const uint64_t* pCoverageMask = (T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE) ? &work.innerCoverageMask : &work.coverageMask[0];

                    generateInputCoverage<T, T::InputCoverage>(pCoverageMask, psContext.inputMask, state.blendState.sampleMask);
                }

                AR_BEGIN(BEBarycentric, pDC->drawId);

                CalcPixelBarycentrics(coeffs, psContext);

                CalcCentroid<T, true>(&psContext, coeffs, work.coverageMask, state.blendState.sampleMask);

                // interpolate and quantize z
                psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.center, psContext.vJ.center);
                psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);

                AR_END(BEBarycentric, 1);

                // interpolate user clip distance if available
                if (state.rastState.clipDistanceMask)
                {
                    coverageMask &= ~ComputeUserClipMask(state.rastState.clipDistanceMask, work.pUserClipBuffer, psContext.vI.center, psContext.vJ.center);
                }

                simdscalar vCoverageMask = vMask(coverageMask);
                simdscalar depthPassMask = vCoverageMask;
                simdscalar stencilPassMask = vCoverageMask;

                // Early-Z?
                if (T::bCanEarlyZ)
                {
                    AR_BEGIN(BEEarlyDepthTest, pDC->drawId);
                    depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing, work.triFlags.viewportIndex,
                                                     psContext.vZ, pDepthBuffer, vCoverageMask, pStencilBuffer, &stencilPassMask);
                    AR_EVENT(EarlyDepthStencilInfoSingleSample(_simd_movemask_ps(depthPassMask), _simd_movemask_ps(vCoverageMask), _simd_movemask_ps(stencilPassMask)));
                    AR_END(BEEarlyDepthTest, 0);

                    // early-exit if no pixels passed depth or earlyZ is forced on
                    if (state.psState.forceEarlyZ || !_simd_movemask_ps(depthPassMask))
                    {
                        DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                            pDepthBuffer, depthPassMask, vCoverageMask, pStencilBuffer, stencilPassMask);

                        if (!_simd_movemask_ps(depthPassMask))
                        {
                            goto Endtile;
                        }
                    }
                }

                psContext.sampleIndex = 0;
                psContext.activeMask = _simd_castps_si(vCoverageMask);

                // execute pixel shader
                AR_BEGIN(BEPixelShader, pDC->drawId);
                UPDATE_STAT_BE(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(vCoverageMask)));
                state.psState.pfnPixelShader(GetPrivateState(pDC), &psContext);
                AR_END(BEPixelShader, 0);

                vCoverageMask = _simd_castsi_ps(psContext.activeMask);

                // late-Z
                if (!T::bCanEarlyZ)
                {
                    AR_BEGIN(BELateDepthTest, pDC->drawId);
                    depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing, work.triFlags.viewportIndex,
                                                        psContext.vZ, pDepthBuffer, vCoverageMask, pStencilBuffer, &stencilPassMask);
                    AR_EVENT(LateDepthStencilInfoSingleSample(_simd_movemask_ps(depthPassMask), _simd_movemask_ps(vCoverageMask), _simd_movemask_ps(stencilPassMask)));
                    AR_END(BELateDepthTest, 0);

                    if (!_simd_movemask_ps(depthPassMask))
                    {
                        // need to call depth/stencil write for stencil write
                        DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                            pDepthBuffer, depthPassMask, vCoverageMask, pStencilBuffer, stencilPassMask);
                        goto Endtile;
                    }
                }

                uint32_t statMask = _simd_movemask_ps(depthPassMask);
                uint32_t statCount = _mm_popcnt_u32(statMask);
                UPDATE_STAT_BE(DepthPassCount, statCount);

                // output merger
                AR_BEGIN(BEOutputMerger, pDC->drawId);
#if USE_8x2_TILE_BACKEND
                OutputMerger8x2(psContext, pColorBuffer, 0, &state.blendState, state.pfnBlendFunc, vCoverageMask, depthPassMask, state.psState.numRenderTargets, state.colorHottileEnable, useAlternateOffset);
#else
                OutputMerger4x2(psContext, pColorBuffer, 0, &state.blendState, state.pfnBlendFunc, vCoverageMask, depthPassMask, state.psState.numRenderTargets);
#endif

                // do final depth write after all pixel kills
                if (!state.psState.forceEarlyZ)
                {
                    DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                        pDepthBuffer, depthPassMask, vCoverageMask, pStencilBuffer, stencilPassMask);
                }
                AR_END(BEOutputMerger, 0);
            }

Endtile:
            AR_BEGIN(BEEndTile, pDC->drawId);

            work.coverageMask[0] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            if(T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE)
            {
                work.innerCoverageMask >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

#if USE_8x2_TILE_BACKEND
            if (useAlternateOffset)
            {
                for (uint32_t rt = 0; rt < state.psState.numRenderTargets; ++rt)
                {
                    pColorBuffer[rt] += (2 * KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
                }
            }
#else
            for (uint32_t rt = 0; rt < state.psState.numRenderTargets; ++rt)
            {
                pColorBuffer[rt] += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
            }
#endif
            pDepthBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            AR_END(BEEndTile, 0);

            psContext.vX.UL     = _simd_add_ps(psContext.vX.UL,     dx);
            psContext.vX.center = _simd_add_ps(psContext.vX.center, dx);
        }

        psContext.vY.UL     = _simd_add_ps(psContext.vY.UL,     dy);
        psContext.vY.center = _simd_add_ps(psContext.vY.center, dy);
    }

    AR_END(BESingleSampleBackend, 0);
}

template<typename T>
void BackendSampleRate(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(BESampleRateBackend, pDC->drawId);
    AR_BEGIN(BESetup, pDC->drawId);

    const API_STATE &state = GetApiState(pDC);

    BarycentricCoeffs coeffs;
    SetupBarycentricCoeffs(&coeffs, work);

    uint8_t *pColorBuffer[SWR_NUM_RENDERTARGETS], *pDepthBuffer, *pStencilBuffer;
    SetupRenderBuffers(pColorBuffer, &pDepthBuffer, &pStencilBuffer, state.psState.numRenderTargets, renderBuffers);

    SWR_PS_CONTEXT psContext;
    SetupPixelShaderContext<T>(&psContext, work);

    AR_END(BESetup, 0);

    psContext.vY.UL     = _simd_add_ps(vULOffsetsY,     _simd_set1_ps(static_cast<float>(y)));
    psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps(static_cast<float>(y)));

    const simdscalar dy = _simd_set1_ps(static_cast<float>(SIMD_TILE_Y_DIM));

    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        psContext.vX.UL     = _simd_add_ps(vULOffsetsX,     _simd_set1_ps(static_cast<float>(x)));
        psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps(static_cast<float>(x)));

        const simdscalar dx = _simd_set1_ps(static_cast<float>(SIMD_TILE_X_DIM));

        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
#if USE_8x2_TILE_BACKEND
            const bool useAlternateOffset = ((xx & SIMD_TILE_X_DIM) != 0);

#endif
            if (T::InputCoverage != SWR_INPUT_COVERAGE_NONE)
            {
                const uint64_t* pCoverageMask = (T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE) ? &work.innerCoverageMask : &work.coverageMask[0];

                generateInputCoverage<T, T::InputCoverage>(pCoverageMask, psContext.inputMask, state.blendState.sampleMask);
            }

            AR_BEGIN(BEBarycentric, pDC->drawId);

            CalcPixelBarycentrics(coeffs, psContext);

            CalcCentroid<T, false>(&psContext, coeffs, work.coverageMask, state.blendState.sampleMask);

            AR_END(BEBarycentric, 0);

            for (uint32_t sample = 0; sample < T::MultisampleT::numSamples; sample++)
            {
                simdmask coverageMask = work.coverageMask[sample] & MASK;

                if (coverageMask)
                {
                    // offset depth/stencil buffers current sample
                    uint8_t *pDepthSample = pDepthBuffer + RasterTileDepthOffset(sample);
                    uint8_t *pStencilSample = pStencilBuffer + RasterTileStencilOffset(sample);

                    if (state.depthHottileEnable && state.depthBoundsState.depthBoundsTestEnable)
                    {
                        static_assert(KNOB_DEPTH_HOT_TILE_FORMAT == R32_FLOAT, "Unsupported depth hot tile format");

                        const simdscalar z = _simd_load_ps(reinterpret_cast<const float *>(pDepthSample));

                        const float minz = state.depthBoundsState.depthBoundsTestMinValue;
                        const float maxz = state.depthBoundsState.depthBoundsTestMaxValue;

                        coverageMask &= CalcDepthBoundsAcceptMask(z, minz, maxz);
                    }

                    AR_BEGIN(BEBarycentric, pDC->drawId);

                    // calculate per sample positions
                    psContext.vX.sample = _simd_add_ps(psContext.vX.UL, T::MultisampleT::vX(sample));
                    psContext.vY.sample = _simd_add_ps(psContext.vY.UL, T::MultisampleT::vY(sample));

                    CalcSampleBarycentrics(coeffs, psContext);

                    // interpolate and quantize z
                    psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.sample, psContext.vJ.sample);
                    psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);

                    AR_END(BEBarycentric, 0);

                    // interpolate user clip distance if available
                    if (state.rastState.clipDistanceMask)
                    {
                        coverageMask &= ~ComputeUserClipMask(state.rastState.clipDistanceMask, work.pUserClipBuffer, psContext.vI.sample, psContext.vJ.sample);
                    }

                    simdscalar vCoverageMask = vMask(coverageMask);
                    simdscalar depthPassMask = vCoverageMask;
                    simdscalar stencilPassMask = vCoverageMask;

                    // Early-Z?
                    if (T::bCanEarlyZ)
                    {
                        AR_BEGIN(BEEarlyDepthTest, pDC->drawId);
                        depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing, work.triFlags.viewportIndex,
                                              psContext.vZ, pDepthSample, vCoverageMask, pStencilSample, &stencilPassMask);
                        AR_EVENT(EarlyDepthStencilInfoSampleRate(_simd_movemask_ps(depthPassMask), _simd_movemask_ps(vCoverageMask), _simd_movemask_ps(stencilPassMask)));
                        AR_END(BEEarlyDepthTest, 0);

                        // early-exit if no samples passed depth or earlyZ is forced on.
                        if (state.psState.forceEarlyZ || !_simd_movemask_ps(depthPassMask))
                        {
                            DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                                pDepthSample, depthPassMask, vCoverageMask, pStencilSample, stencilPassMask);

                            if (!_simd_movemask_ps(depthPassMask))
                            {
                                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
                                continue;
                            }
                        }
                    }

                    psContext.sampleIndex = sample;
                    psContext.activeMask = _simd_castps_si(vCoverageMask);

                    // execute pixel shader
                    AR_BEGIN(BEPixelShader, pDC->drawId);
                    UPDATE_STAT_BE(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(vCoverageMask)));
                    state.psState.pfnPixelShader(GetPrivateState(pDC), &psContext);
                    AR_END(BEPixelShader, 0);

                    vCoverageMask = _simd_castsi_ps(psContext.activeMask);

                    // late-Z
                    if (!T::bCanEarlyZ)
                    {
                        AR_BEGIN(BELateDepthTest, pDC->drawId);
                        depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing, work.triFlags.viewportIndex,
                                              psContext.vZ, pDepthSample, vCoverageMask, pStencilSample, &stencilPassMask);
                        AR_EVENT(LateDepthStencilInfoSampleRate(_simd_movemask_ps(depthPassMask), _simd_movemask_ps(vCoverageMask), _simd_movemask_ps(stencilPassMask)));
                        AR_END(BELateDepthTest, 0);

                        if (!_simd_movemask_ps(depthPassMask))
                        {
                            // need to call depth/stencil write for stencil write
                            DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                                pDepthSample, depthPassMask, vCoverageMask, pStencilSample, stencilPassMask);

                            work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
                            continue;
                        }
                    }

                    uint32_t statMask = _simd_movemask_ps(depthPassMask);
                    uint32_t statCount = _mm_popcnt_u32(statMask);
                    UPDATE_STAT_BE(DepthPassCount, statCount);

                    // output merger
                    AR_BEGIN(BEOutputMerger, pDC->drawId);
#if USE_8x2_TILE_BACKEND
                    OutputMerger8x2(psContext, pColorBuffer, sample, &state.blendState, state.pfnBlendFunc, vCoverageMask, depthPassMask, state.psState.numRenderTargets, state.colorHottileEnable, useAlternateOffset);
#else
                    OutputMerger4x2(psContext, pColorBuffer, sample, &state.blendState, state.pfnBlendFunc, vCoverageMask, depthPassMask, state.psState.numRenderTargets);
#endif

                    // do final depth write after all pixel kills
                    if (!state.psState.forceEarlyZ)
                    {
                        DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                            pDepthSample, depthPassMask, vCoverageMask, pStencilSample, stencilPassMask);
                    }
                    AR_END(BEOutputMerger, 0);
                }
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

Endtile:
            ATTR_UNUSED;

            AR_BEGIN(BEEndTile, pDC->drawId);

            if(T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE)
            {
                work.innerCoverageMask >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

#if USE_8x2_TILE_BACKEND
            if (useAlternateOffset)
            {
                for (uint32_t rt = 0; rt < state.psState.numRenderTargets; ++rt)
                {
                    pColorBuffer[rt] += (2 * KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
                }
            }
#else
            for (uint32_t rt = 0; rt < state.psState.numRenderTargets; ++rt)
            {
                pColorBuffer[rt] += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
            }
#endif
            pDepthBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            AR_END(BEEndTile, 0);

            psContext.vX.UL     = _simd_add_ps(psContext.vX.UL,     dx);
            psContext.vX.center = _simd_add_ps(psContext.vX.center, dx);
        }

        psContext.vY.UL     = _simd_add_ps(psContext.vY.UL,     dy);
        psContext.vY.center = _simd_add_ps(psContext.vY.center, dy);
    }

    AR_END(BESampleRateBackend, 0);
}

template<typename T>
void BackendPixelRate(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(BEPixelRateBackend, pDC->drawId);
    AR_BEGIN(BESetup, pDC->drawId);

    const API_STATE &state = GetApiState(pDC);

    BarycentricCoeffs coeffs;
    SetupBarycentricCoeffs(&coeffs, work);

    uint8_t *pColorBuffer[SWR_NUM_RENDERTARGETS], *pDepthBuffer, *pStencilBuffer;
    SetupRenderBuffers(pColorBuffer, &pDepthBuffer, &pStencilBuffer, state.psState.numRenderTargets, renderBuffers);

    SWR_PS_CONTEXT psContext;
    SetupPixelShaderContext<T>(&psContext, work);

    AR_END(BESetup, 0);

    PixelRateZTestLoop<T> PixelRateZTest(pDC, workerId, work, coeffs, state, pDepthBuffer, pStencilBuffer, state.rastState.clipDistanceMask);

    psContext.vY.UL     = _simd_add_ps(vULOffsetsY,     _simd_set1_ps(static_cast<float>(y)));
    psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps(static_cast<float>(y)));

    const simdscalar dy = _simd_set1_ps(static_cast<float>(SIMD_TILE_Y_DIM));

    for(uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        psContext.vX.UL     = _simd_add_ps(vULOffsetsX,     _simd_set1_ps(static_cast<float>(x)));
        psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps(static_cast<float>(x)));

        const simdscalar dx = _simd_set1_ps(static_cast<float>(SIMD_TILE_X_DIM));

        for(uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
#if USE_8x2_TILE_BACKEND
            const bool useAlternateOffset = ((xx & SIMD_TILE_X_DIM) != 0);

#endif
            simdscalar activeLanes;
            if(!(work.anyCoveredSamples & MASK)) {goto Endtile;};
            activeLanes = vMask(work.anyCoveredSamples & MASK);

            if (T::InputCoverage != SWR_INPUT_COVERAGE_NONE)
            {
                const uint64_t* pCoverageMask = (T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE) ? &work.innerCoverageMask : &work.coverageMask[0];

                generateInputCoverage<T, T::InputCoverage>(pCoverageMask, psContext.inputMask, state.blendState.sampleMask);
            }

            AR_BEGIN(BEBarycentric, pDC->drawId);

            CalcPixelBarycentrics(coeffs, psContext);

            CalcCentroid<T, false>(&psContext, coeffs, work.coverageMask, state.blendState.sampleMask);

            AR_END(BEBarycentric, 0);

            if(T::bForcedSampleCount)
            {
                // candidate pixels (that passed coverage) will cause shader invocation if any bits in the samplemask are set
                const simdscalar vSampleMask = _simd_castsi_ps(_simd_cmpgt_epi32(_simd_set1_epi32(state.blendState.sampleMask), _simd_setzero_si()));
                activeLanes = _simd_and_ps(activeLanes, vSampleMask);
            }

            // Early-Z?
            if(T::bCanEarlyZ && !T::bForcedSampleCount)
            {
                uint32_t depthPassCount = PixelRateZTest(activeLanes, psContext, BEEarlyDepthTest);
                UPDATE_STAT_BE(DepthPassCount, depthPassCount);
                AR_EVENT(EarlyDepthInfoPixelRate(depthPassCount, _simd_movemask_ps(activeLanes)));
            }

            // if we have no covered samples that passed depth at this point, go to next tile
            if(!_simd_movemask_ps(activeLanes)) { goto Endtile; };

            if(state.psState.usesSourceDepth)
            {
                AR_BEGIN(BEBarycentric, pDC->drawId);
                // interpolate and quantize z
                psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.center, psContext.vJ.center);
                psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);
                AR_END(BEBarycentric, 0);
            }

            // pixels that are currently active
            psContext.activeMask = _simd_castps_si(activeLanes);
            psContext.oMask = T::MultisampleT::FullSampleMask();

            // execute pixel shader
            AR_BEGIN(BEPixelShader, pDC->drawId);
            state.psState.pfnPixelShader(GetPrivateState(pDC), &psContext);
            UPDATE_STAT_BE(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(activeLanes)));
            AR_END(BEPixelShader, 0);

            // update active lanes to remove any discarded or oMask'd pixels
            activeLanes = _simd_castsi_ps(_simd_and_si(psContext.activeMask, _simd_cmpgt_epi32(psContext.oMask, _simd_setzero_si())));
            if(!_simd_movemask_ps(activeLanes)) { goto Endtile; };

            // late-Z
            if(!T::bCanEarlyZ && !T::bForcedSampleCount)
            {
                uint32_t depthPassCount = PixelRateZTest(activeLanes, psContext, BELateDepthTest);
                UPDATE_STAT_BE(DepthPassCount, depthPassCount);
                AR_EVENT(LateDepthInfoPixelRate(depthPassCount, _simd_movemask_ps(activeLanes)));
            }

            // if we have no covered samples that passed depth at this point, skip OM and go to next tile
            if(!_simd_movemask_ps(activeLanes)) { goto Endtile; };

            // output merger
            // loop over all samples, broadcasting the results of the PS to all passing pixels
            for(uint32_t sample = 0; sample < GetNumOMSamples<T>(state.blendState.sampleCount); sample++)
            {
                AR_BEGIN(BEOutputMerger, pDC->drawId);
                // center pattern does a single coverage/depth/stencil test, standard pattern tests all samples
                uint32_t coverageSampleNum = (T::bIsStandardPattern) ? sample : 0;
                simdscalar coverageMask, depthMask;
                if(T::bForcedSampleCount)
                {
                    coverageMask = depthMask = activeLanes;
                }
                else
                {
                    coverageMask = PixelRateZTest.vCoverageMask[coverageSampleNum];
                    depthMask = PixelRateZTest.depthPassMask[coverageSampleNum];
                    if(!_simd_movemask_ps(depthMask))
                    {
                        // stencil should already have been written in early/lateZ tests
                        AR_END(BEOutputMerger, 0);
                        continue;
                    }
                }
                
                // broadcast the results of the PS to all passing pixels
#if USE_8x2_TILE_BACKEND
                OutputMerger8x2(psContext, pColorBuffer, sample, &state.blendState, state.pfnBlendFunc, coverageMask, depthMask, state.psState.numRenderTargets, state.colorHottileEnable, useAlternateOffset);
#else
                OutputMerger4x2(psContext, pColorBuffer, sample, &state.blendState, state.pfnBlendFunc, coverageMask, depthMask, state.psState.numRenderTargets);
#endif

                if(!state.psState.forceEarlyZ && !T::bForcedSampleCount)
                {
                    uint8_t *pDepthSample = pDepthBuffer + RasterTileDepthOffset(sample);
                    uint8_t * pStencilSample = pStencilBuffer + RasterTileStencilOffset(sample);

                    DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, PixelRateZTest.vZ[coverageSampleNum],
                                      pDepthSample, depthMask, coverageMask, pStencilSample, PixelRateZTest.stencilPassMask[coverageSampleNum]);
                }
                AR_END(BEOutputMerger, 0);
            }
Endtile:
            AR_BEGIN(BEEndTile, pDC->drawId);

            for(uint32_t sample = 0; sample < T::MultisampleT::numCoverageSamples; sample++)
            {
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

            if(T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE)
            {
                work.innerCoverageMask >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }
            work.anyCoveredSamples >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);

#if USE_8x2_TILE_BACKEND
            if (useAlternateOffset)
            {
                for (uint32_t rt = 0; rt < state.psState.numRenderTargets; ++rt)
                {
                    pColorBuffer[rt] += (2 * KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
                }
            }
#else
            for(uint32_t rt = 0; rt < state.psState.numRenderTargets; ++rt)
            {
                pColorBuffer[rt] += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
            }
            pDepthBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;
#endif

            AR_END(BEEndTile, 0);

            psContext.vX.UL     = _simd_add_ps(psContext.vX.UL,     dx);
            psContext.vX.center = _simd_add_ps(psContext.vX.center, dx);
        }

        psContext.vY.UL     = _simd_add_ps(psContext.vY.UL,     dy);
        psContext.vY.center = _simd_add_ps(psContext.vY.center, dy);
    }

    AR_END(BEPixelRateBackend, 0);
}
// optimized backend flow with NULL PS
template<uint32_t sampleCountT>
void BackendNullPS(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers)
{
    SWR_CONTEXT *pContext = pDC->pContext;

    AR_BEGIN(BENullBackend, pDC->drawId);
    ///@todo: handle center multisample pattern
    typedef SwrBackendTraits<sampleCountT, SWR_MSAA_STANDARD_PATTERN> T;
    AR_BEGIN(BESetup, pDC->drawId);

    const API_STATE &state = GetApiState(pDC);

    BarycentricCoeffs coeffs;
    SetupBarycentricCoeffs(&coeffs, work);

    uint8_t *pDepthBuffer, *pStencilBuffer;
    SetupRenderBuffers(NULL, &pDepthBuffer, &pStencilBuffer, 0, renderBuffers);

    SWR_PS_CONTEXT psContext;
    // skip SetupPixelShaderContext(&psContext, ...); // not needed here

    AR_END(BESetup, 0);

    simdscalar vYSamplePosUL = _simd_add_ps(vULOffsetsY, _simd_set1_ps(static_cast<float>(y)));

    const simdscalar dy = _simd_set1_ps(static_cast<float>(SIMD_TILE_Y_DIM));

    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        simdscalar vXSamplePosUL = _simd_add_ps(vULOffsetsX, _simd_set1_ps(static_cast<float>(x)));

        const simdscalar dx = _simd_set1_ps(static_cast<float>(SIMD_TILE_X_DIM));

        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            // iterate over active samples
            unsigned long sample = 0;
            uint32_t sampleMask = state.blendState.sampleMask;
            while (_BitScanForward(&sample, sampleMask))
            {
                sampleMask &= ~(1 << sample);

                simdmask coverageMask = work.coverageMask[sample] & MASK;

                if (coverageMask)
                {
                    // offset depth/stencil buffers current sample
                    uint8_t *pDepthSample = pDepthBuffer + RasterTileDepthOffset(sample);
                    uint8_t *pStencilSample = pStencilBuffer + RasterTileStencilOffset(sample);

                    if (state.depthHottileEnable && state.depthBoundsState.depthBoundsTestEnable)
                    {
                        static_assert(KNOB_DEPTH_HOT_TILE_FORMAT == R32_FLOAT, "Unsupported depth hot tile format");

                        const simdscalar z = _simd_load_ps(reinterpret_cast<const float *>(pDepthSample));

                        const float minz = state.depthBoundsState.depthBoundsTestMinValue;
                        const float maxz = state.depthBoundsState.depthBoundsTestMaxValue;

                        coverageMask &= CalcDepthBoundsAcceptMask(z, minz, maxz);
                    }

                    AR_BEGIN(BEBarycentric, pDC->drawId);

                    // calculate per sample positions
                    psContext.vX.sample = _simd_add_ps(vXSamplePosUL, T::MultisampleT::vX(sample));
                    psContext.vY.sample = _simd_add_ps(vYSamplePosUL, T::MultisampleT::vY(sample));

                    CalcSampleBarycentrics(coeffs, psContext);

                    // interpolate and quantize z
                    psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.sample, psContext.vJ.sample);
                    psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);

                    AR_END(BEBarycentric, 0);

                    // interpolate user clip distance if available
                    if (state.rastState.clipDistanceMask)
                    {
                        coverageMask &= ~ComputeUserClipMask(state.rastState.clipDistanceMask, work.pUserClipBuffer, psContext.vI.sample, psContext.vJ.sample);
                    }

                    simdscalar vCoverageMask = vMask(coverageMask);
                    simdscalar stencilPassMask = vCoverageMask;

                    AR_BEGIN(BEEarlyDepthTest, pDC->drawId);
                    simdscalar depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing, work.triFlags.viewportIndex,
                        psContext.vZ, pDepthSample, vCoverageMask, pStencilSample, &stencilPassMask);
                    AR_EVENT(EarlyDepthStencilInfoNullPS(_simd_movemask_ps(depthPassMask), _simd_movemask_ps(vCoverageMask), _simd_movemask_ps(stencilPassMask)));
                    DepthStencilWrite(&state.vp[work.triFlags.viewportIndex], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                        pDepthSample, depthPassMask, vCoverageMask, pStencilSample, stencilPassMask);
                    AR_END(BEEarlyDepthTest, 0);

                    uint32_t statMask = _simd_movemask_ps(depthPassMask);
                    uint32_t statCount = _mm_popcnt_u32(statMask);
                    UPDATE_STAT_BE(DepthPassCount, statCount);
                }

Endtile:
                ATTR_UNUSED;
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

            pDepthBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            vXSamplePosUL = _simd_add_ps(vXSamplePosUL, dx);
        }

        vYSamplePosUL = _simd_add_ps(vYSamplePosUL, dy);
    }

    AR_END(BENullBackend, 0);
}

void InitClearTilesTable()
{
    memset(sClearTilesTable, 0, sizeof(sClearTilesTable));

    sClearTilesTable[R8G8B8A8_UNORM] = ClearMacroTile<R8G8B8A8_UNORM>;
    sClearTilesTable[B8G8R8A8_UNORM] = ClearMacroTile<B8G8R8A8_UNORM>;
    sClearTilesTable[R32_FLOAT] = ClearMacroTile<R32_FLOAT>;
    sClearTilesTable[R32G32B32A32_FLOAT] = ClearMacroTile<R32G32B32A32_FLOAT>;
    sClearTilesTable[R8_UINT] = ClearMacroTile<R8_UINT>;
}

PFN_BACKEND_FUNC gBackendNullPs[SWR_MULTISAMPLE_TYPE_COUNT];
PFN_BACKEND_FUNC gBackendSingleSample[SWR_INPUT_COVERAGE_COUNT]
                                     [2] // centroid
                                     [2] // canEarlyZ
                                     = {};
PFN_BACKEND_FUNC gBackendPixelRateTable[SWR_MULTISAMPLE_TYPE_COUNT]
                                       [SWR_MSAA_SAMPLE_PATTERN_COUNT]
                                       [SWR_INPUT_COVERAGE_COUNT]
                                       [2] // centroid
                                       [2] // forcedSampleCount
                                       [2] // canEarlyZ
                                       = {};
PFN_BACKEND_FUNC gBackendSampleRateTable[SWR_MULTISAMPLE_TYPE_COUNT]
                                        [SWR_INPUT_COVERAGE_COUNT]
                                        [2] // centroid
                                        [2] // canEarlyZ
                                        = {};

// Recursive template used to auto-nest conditionals.  Converts dynamic enum function
// arguments to static template arguments.
template <uint32_t... ArgsT>
struct BEChooser
{
    // Last Arg Terminator
    static PFN_BACKEND_FUNC GetFunc(SWR_BACKEND_FUNCS tArg)
    {
        switch(tArg)
        {
        case SWR_BACKEND_SINGLE_SAMPLE: return BackendSingleSample<SwrBackendTraits<ArgsT...>>; break;
        case SWR_BACKEND_MSAA_PIXEL_RATE: return BackendPixelRate<SwrBackendTraits<ArgsT...>>; break;
        case SWR_BACKEND_MSAA_SAMPLE_RATE: return BackendSampleRate<SwrBackendTraits<ArgsT...>>; break;
        default:
            SWR_ASSERT(0 && "Invalid backend func\n");
            return nullptr;
            break;
        }
    }

    // Recursively parse args
    template <typename... TArgsT>
    static PFN_BACKEND_FUNC GetFunc(SWR_MSAA_SAMPLE_PATTERN tArg, TArgsT... remainingArgs)
    {
        switch(tArg)
        {
        case SWR_MSAA_CENTER_PATTERN: return BEChooser<ArgsT..., SWR_MSAA_CENTER_PATTERN>::GetFunc(remainingArgs...); break;
        case SWR_MSAA_STANDARD_PATTERN: return BEChooser<ArgsT..., SWR_MSAA_STANDARD_PATTERN>::GetFunc(remainingArgs...); break;
        default:
        SWR_ASSERT(0 && "Invalid sample pattern\n");
        return BEChooser<ArgsT..., SWR_MSAA_STANDARD_PATTERN>::GetFunc(remainingArgs...);
        break;
        }
    }

    // Recursively parse args
    template <typename... TArgsT>
    static PFN_BACKEND_FUNC GetFunc(SWR_INPUT_COVERAGE tArg, TArgsT... remainingArgs)
    {
        switch(tArg)
        {
        case SWR_INPUT_COVERAGE_NONE: return BEChooser<ArgsT..., SWR_INPUT_COVERAGE_NONE>::GetFunc(remainingArgs...); break;
        case SWR_INPUT_COVERAGE_NORMAL: return BEChooser<ArgsT..., SWR_INPUT_COVERAGE_NORMAL>::GetFunc(remainingArgs...); break;
        case SWR_INPUT_COVERAGE_INNER_CONSERVATIVE: return BEChooser<ArgsT..., SWR_INPUT_COVERAGE_INNER_CONSERVATIVE>::GetFunc(remainingArgs...); break;
        default:
        SWR_ASSERT(0 && "Invalid sample pattern\n");
        return BEChooser<ArgsT..., SWR_INPUT_COVERAGE_NONE>::GetFunc(remainingArgs...);
        break;
        }
    }

    // Recursively parse args
    template <typename... TArgsT>
    static PFN_BACKEND_FUNC GetFunc(SWR_MULTISAMPLE_COUNT tArg, TArgsT... remainingArgs)
    {
        switch(tArg)
        {
        case SWR_MULTISAMPLE_1X: return BEChooser<ArgsT..., SWR_MULTISAMPLE_1X>::GetFunc(remainingArgs...); break;
        case SWR_MULTISAMPLE_2X: return BEChooser<ArgsT..., SWR_MULTISAMPLE_2X>::GetFunc(remainingArgs...); break;
        case SWR_MULTISAMPLE_4X: return BEChooser<ArgsT..., SWR_MULTISAMPLE_4X>::GetFunc(remainingArgs...); break;
        case SWR_MULTISAMPLE_8X: return BEChooser<ArgsT..., SWR_MULTISAMPLE_8X>::GetFunc(remainingArgs...); break;
        case SWR_MULTISAMPLE_16X: return BEChooser<ArgsT..., SWR_MULTISAMPLE_16X>::GetFunc(remainingArgs...); break;
        default:
        SWR_ASSERT(0 && "Invalid sample count\n");
        return BEChooser<ArgsT..., SWR_MULTISAMPLE_1X>::GetFunc(remainingArgs...);
        break;
        }
    }

    // Recursively parse args
    template <typename... TArgsT>
    static PFN_BACKEND_FUNC GetFunc(bool tArg, TArgsT... remainingArgs)
    {
        if(tArg == true)
        {
            return BEChooser<ArgsT..., 1>::GetFunc(remainingArgs...);
        }

        return BEChooser<ArgsT..., 0>::GetFunc(remainingArgs...);
    }
};

void InitBackendSingleFuncTable(PFN_BACKEND_FUNC (&table)[SWR_INPUT_COVERAGE_COUNT][2][2])
{
    for(uint32_t inputCoverage = 0; inputCoverage < SWR_INPUT_COVERAGE_COUNT; inputCoverage++)
    {
        for(uint32_t isCentroid = 0; isCentroid < 2; isCentroid++)
        {
            for(uint32_t canEarlyZ = 0; canEarlyZ < 2; canEarlyZ++)
            {
                table[inputCoverage][isCentroid][canEarlyZ] =
                    BEChooser<>::GetFunc(SWR_MULTISAMPLE_1X, SWR_MSAA_STANDARD_PATTERN, (SWR_INPUT_COVERAGE)inputCoverage,
                                         (isCentroid > 0), false, (canEarlyZ > 0), SWR_BACKEND_SINGLE_SAMPLE);
            }
        }
    }
}

void InitBackendPixelFuncTable(PFN_BACKEND_FUNC (&table)[SWR_MULTISAMPLE_TYPE_COUNT][SWR_MSAA_SAMPLE_PATTERN_COUNT][SWR_INPUT_COVERAGE_COUNT][2][2][2])
{
    for(uint32_t sampleCount = SWR_MULTISAMPLE_1X; sampleCount < SWR_MULTISAMPLE_TYPE_COUNT; sampleCount++)
    {
        for(uint32_t samplePattern = SWR_MSAA_CENTER_PATTERN; samplePattern < SWR_MSAA_SAMPLE_PATTERN_COUNT; samplePattern++)
        {
            for(uint32_t inputCoverage = 0; inputCoverage < SWR_INPUT_COVERAGE_COUNT; inputCoverage++)
            {
                for(uint32_t isCentroid = 0; isCentroid < 2; isCentroid++)
                {
                    for(uint32_t forcedSampleCount = 0; forcedSampleCount < 2; forcedSampleCount++)
                    {
                        for(uint32_t canEarlyZ = 0; canEarlyZ < 2; canEarlyZ++)
                        {
                            table[sampleCount][samplePattern][inputCoverage][isCentroid][forcedSampleCount][canEarlyZ] =
                                BEChooser<>::GetFunc((SWR_MULTISAMPLE_COUNT)sampleCount, (SWR_MSAA_SAMPLE_PATTERN)samplePattern, (SWR_INPUT_COVERAGE)inputCoverage, 
                                                        (isCentroid > 0), (forcedSampleCount > 0), (canEarlyZ > 0), SWR_BACKEND_MSAA_PIXEL_RATE);
                        }
                    }
                }
            }
        }
    }
}

void InitBackendSampleFuncTable(PFN_BACKEND_FUNC (&table)[SWR_MULTISAMPLE_TYPE_COUNT][SWR_INPUT_COVERAGE_COUNT][2][2])
{
    for(uint32_t sampleCount = SWR_MULTISAMPLE_1X; sampleCount < SWR_MULTISAMPLE_TYPE_COUNT; sampleCount++)
    {
        for(uint32_t inputCoverage = 0; inputCoverage < SWR_INPUT_COVERAGE_COUNT; inputCoverage++)
        {
            for(uint32_t centroid = 0; centroid < 2; centroid++)
            {
                for(uint32_t canEarlyZ = 0; canEarlyZ < 2; canEarlyZ++)
                {
                    table[sampleCount][inputCoverage][centroid][canEarlyZ] =
                        BEChooser<>::GetFunc((SWR_MULTISAMPLE_COUNT)sampleCount, SWR_MSAA_STANDARD_PATTERN, (SWR_INPUT_COVERAGE)inputCoverage, 
                                             (centroid > 0), false, (canEarlyZ > 0), (SWR_BACKEND_FUNCS)SWR_BACKEND_MSAA_SAMPLE_RATE);
                }
            }
        }
    }
}

void InitBackendFuncTables()
{    
    InitBackendSingleFuncTable(gBackendSingleSample);
    InitBackendPixelFuncTable(gBackendPixelRateTable);
    InitBackendSampleFuncTable(gBackendSampleRateTable);

    gBackendNullPs[SWR_MULTISAMPLE_1X] = &BackendNullPS < SWR_MULTISAMPLE_1X > ;
    gBackendNullPs[SWR_MULTISAMPLE_2X] = &BackendNullPS < SWR_MULTISAMPLE_2X > ;
    gBackendNullPs[SWR_MULTISAMPLE_4X] = &BackendNullPS < SWR_MULTISAMPLE_4X > ;
    gBackendNullPs[SWR_MULTISAMPLE_8X] = &BackendNullPS < SWR_MULTISAMPLE_8X > ;
    gBackendNullPs[SWR_MULTISAMPLE_16X] = &BackendNullPS < SWR_MULTISAMPLE_16X > ;
}
