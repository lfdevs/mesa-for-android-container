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

typedef void(*PFN_CLEAR_TILES)(DRAW_CONTEXT*, SWR_RENDERTARGET_ATTACHMENT rt, uint32_t, DWORD[4]);
static PFN_CLEAR_TILES sClearTilesTable[NUM_SWR_FORMATS];

//////////////////////////////////////////////////////////////////////////
/// @brief Process compute work.
/// @param pDC - pointer to draw context (dispatch).
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param threadGroupId - the linear index for the thread group within the dispatch.
void ProcessComputeBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t threadGroupId, void*& pSpillFillBuffer)
{
    RDTSC_START(BEDispatch);

    SWR_CONTEXT *pContext = pDC->pContext;

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
    csContext.pTGSM = pContext->pScratch[workerId];
    csContext.pSpillFillBuffer = (uint8_t*)pSpillFillBuffer;

    state.pfnCsFunc(GetPrivateState(pDC), &csContext);

    UPDATE_STAT(CsInvocations, state.totalThreadsInGroup);

    RDTSC_STOP(BEDispatch, 1, 0);
}

void ProcessSyncBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData)
{
    SYNC_DESC *pSync = (SYNC_DESC*)pUserData;

    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroTile, x, y);
    SWR_ASSERT(x == 0 && y == 0);

    if (pSync->pfnCallbackFunc != nullptr)
    {
        pSync->pfnCallbackFunc(pSync->userData, pSync->userData2, pSync->userData3);
    }
}

void ProcessQueryStatsBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pUserData)
{
    QUERY_DESC* pQueryDesc = (QUERY_DESC*)pUserData;
    SWR_STATS* pStats = pQueryDesc->pStats;
    SWR_CONTEXT *pContext = pDC->pContext;

    SWR_ASSERT(pStats != nullptr);

    for (uint32_t i = 0; i < pContext->NumWorkerThreads; ++i)
    {
        pStats->DepthPassCount += pContext->stats[i].DepthPassCount;

        pStats->IaVertices    += pContext->stats[i].IaVertices;
        pStats->IaPrimitives  += pContext->stats[i].IaPrimitives;
        pStats->VsInvocations += pContext->stats[i].VsInvocations;
        pStats->HsInvocations += pContext->stats[i].HsInvocations;
        pStats->DsInvocations += pContext->stats[i].DsInvocations;
        pStats->GsInvocations += pContext->stats[i].GsInvocations;
        pStats->PsInvocations += pContext->stats[i].PsInvocations;
        pStats->CInvocations  += pContext->stats[i].CInvocations;
        pStats->CsInvocations += pContext->stats[i].CsInvocations;
        pStats->CPrimitives   += pContext->stats[i].CPrimitives;
        pStats->GsPrimitives  += pContext->stats[i].GsPrimitives;

        for (uint32_t stream = 0; stream < MAX_SO_STREAMS; ++stream)
        {
            pStats->SoWriteOffset[stream] += pContext->stats[i].SoWriteOffset[stream];

            /// @note client is required to provide valid write offset before every draw, so we clear
            /// out the contents of the write offset when storing stats
            pContext->stats[i].SoWriteOffset[stream] = 0;

            pStats->SoPrimStorageNeeded[stream] += pContext->stats[i].SoPrimStorageNeeded[stream];
            pStats->SoNumPrimsWritten[stream] += pContext->stats[i].SoNumPrimsWritten[stream];
        }
    }
}

template<SWR_FORMAT format>
void ClearRasterTile(uint8_t *pTileBuffer, simdvector &value)
{
    auto lambda = [&](int comp)
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

template<SWR_FORMAT format>
INLINE void ClearMacroTile(DRAW_CONTEXT *pDC, SWR_RENDERTARGET_ATTACHMENT rt, uint32_t macroTile, DWORD clear[4])
{
    // convert clear color to hottile format
    // clear color is in RGBA float/uint32
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

    uint32_t tileX, tileY;
    MacroTileMgr::getTileIndices(macroTile, tileX, tileY);
    const API_STATE& state = GetApiState(pDC);
    
    int top = KNOB_MACROTILE_Y_DIM_FIXED * tileY;
    int bottom = top + KNOB_MACROTILE_Y_DIM_FIXED - 1;
    int left = KNOB_MACROTILE_X_DIM_FIXED * tileX;
    int right = left + KNOB_MACROTILE_X_DIM_FIXED - 1;

    // intersect with scissor
    top = std::max(top, state.scissorInFixedPoint.top);
    left = std::max(left, state.scissorInFixedPoint.left);
    bottom = std::min(bottom, state.scissorInFixedPoint.bottom);
    right = std::min(right, state.scissorInFixedPoint.right);

    // translate to local hottile origin
    top -= KNOB_MACROTILE_Y_DIM_FIXED * tileY;
    bottom -= KNOB_MACROTILE_Y_DIM_FIXED * tileY;
    left -= KNOB_MACROTILE_X_DIM_FIXED * tileX;
    right -= KNOB_MACROTILE_X_DIM_FIXED * tileX;

    // convert to raster tiles
    top >>= (KNOB_TILE_Y_DIM_SHIFT + FIXED_POINT_SHIFT);
    bottom >>= (KNOB_TILE_Y_DIM_SHIFT + FIXED_POINT_SHIFT);
    left >>= (KNOB_TILE_X_DIM_SHIFT + FIXED_POINT_SHIFT);
    right >>= (KNOB_TILE_X_DIM_SHIFT + FIXED_POINT_SHIFT);

    const int numSamples = GetNumSamples(pDC->pState->state.rastState.sampleCount);
    // compute steps between raster tile samples / raster tiles / macro tile rows
    const uint32_t rasterTileSampleStep = KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<format>::bpp / 8;
    const uint32_t rasterTileStep = (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<format>::bpp / 8)) * numSamples;
    const uint32_t macroTileRowStep = (KNOB_MACROTILE_X_DIM / KNOB_TILE_X_DIM) * rasterTileStep;
    const uint32_t pitch = (FormatTraits<format>::bpp * KNOB_MACROTILE_X_DIM / 8);

    HOTTILE *pHotTile = pDC->pContext->pHotTileMgr->GetHotTile(pDC->pContext, pDC, macroTile, rt, true, numSamples);
    uint32_t rasterTileStartOffset = (ComputeTileOffset2D< TilingTraits<SWR_TILE_SWRZ, FormatTraits<format>::bpp > >(pitch, left, top)) * numSamples;
    uint8_t* pRasterTileRow = pHotTile->pBuffer + rasterTileStartOffset; //(ComputeTileOffset2D< TilingTraits<SWR_TILE_SWRZ, FormatTraits<format>::bpp > >(pitch, x, y)) * numSamples;

    // loop over all raster tiles in the current hot tile
    for (int y = top; y <= bottom; ++y)
    {
        uint8_t* pRasterTile = pRasterTileRow;
        for (int x = left; x <= right; ++x)
        {
            for( int sampleNum = 0; sampleNum < numSamples; sampleNum++)
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
    if (KNOB_FAST_CLEAR)
    {
        CLEAR_DESC *pClear = (CLEAR_DESC*)pUserData;
        SWR_CONTEXT *pContext = pDC->pContext;
        SWR_MULTISAMPLE_COUNT sampleCount = pDC->pState->state.rastState.sampleCount;
        uint32_t numSamples = GetNumSamples(sampleCount);

        SWR_ASSERT(pClear->flags.bits != 0); // shouldn't be here without a reason.

        RDTSC_START(BEClear);

        if (pClear->flags.mask & SWR_CLEAR_COLOR)
        {
            HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTile(pContext, pDC, macroTile, SWR_ATTACHMENT_COLOR0, true, numSamples);
            // All we want to do here is to mark the hot tile as being in a "needs clear" state.
            pHotTile->clearData[0] = *(DWORD*)&(pClear->clearRTColor[0]);
            pHotTile->clearData[1] = *(DWORD*)&(pClear->clearRTColor[1]);
            pHotTile->clearData[2] = *(DWORD*)&(pClear->clearRTColor[2]);
            pHotTile->clearData[3] = *(DWORD*)&(pClear->clearRTColor[3]);
            pHotTile->state = HOTTILE_CLEAR;
        }

        if (pClear->flags.mask & SWR_CLEAR_DEPTH)
        {
            HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTile(pContext, pDC, macroTile, SWR_ATTACHMENT_DEPTH, true, numSamples);
            pHotTile->clearData[0] = *(DWORD*)&pClear->clearDepth;
            pHotTile->state = HOTTILE_CLEAR;
        }

        if (pClear->flags.mask & SWR_CLEAR_STENCIL)
        {
            HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTile(pContext, pDC, macroTile, SWR_ATTACHMENT_STENCIL, true, numSamples);

            pHotTile->clearData[0] = *(DWORD*)&pClear->clearStencil;
            pHotTile->state = HOTTILE_CLEAR;
        }

        RDTSC_STOP(BEClear, 0, 0);
    }
    else
    {
        // Legacy clear
        CLEAR_DESC *pClear = (CLEAR_DESC*)pUserData;
        RDTSC_START(BEClear);

        if (pClear->flags.mask & SWR_CLEAR_COLOR)
        {
            /// @todo clear data should come in as RGBA32_FLOAT
            DWORD clearData[4];
            float clearFloat[4];
            clearFloat[0] = ((uint8_t*)(&pClear->clearRTColor))[0] / 255.0f;
            clearFloat[1] = ((uint8_t*)(&pClear->clearRTColor))[1] / 255.0f;
            clearFloat[2] = ((uint8_t*)(&pClear->clearRTColor))[2] / 255.0f;
            clearFloat[3] = ((uint8_t*)(&pClear->clearRTColor))[3] / 255.0f;
            clearData[0] = *(DWORD*)&clearFloat[0];
            clearData[1] = *(DWORD*)&clearFloat[1];
            clearData[2] = *(DWORD*)&clearFloat[2];
            clearData[3] = *(DWORD*)&clearFloat[3];

            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[KNOB_COLOR_HOT_TILE_FORMAT];
            SWR_ASSERT(pfnClearTiles != nullptr);

            pfnClearTiles(pDC, SWR_ATTACHMENT_COLOR0, macroTile, clearData);
        }

        if (pClear->flags.mask & SWR_CLEAR_DEPTH)
        {
            DWORD clearData[4];
            clearData[0] = *(DWORD*)&pClear->clearDepth;
            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[KNOB_DEPTH_HOT_TILE_FORMAT];
            SWR_ASSERT(pfnClearTiles != nullptr);

            pfnClearTiles(pDC, SWR_ATTACHMENT_DEPTH, macroTile, clearData);
        }

        if (pClear->flags.mask & SWR_CLEAR_STENCIL)
        {
            uint32_t value = pClear->clearStencil;
            DWORD clearData[4];
            clearData[0] = *(DWORD*)&value;
            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[KNOB_STENCIL_HOT_TILE_FORMAT];

            pfnClearTiles(pDC, SWR_ATTACHMENT_STENCIL, macroTile, clearData);
        }

        RDTSC_STOP(BEClear, 0, 0);
    }
}


void ProcessStoreTileBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pData)
{
    RDTSC_START(BEStoreTiles);
    STORE_TILES_DESC *pDesc = (STORE_TILES_DESC*)pData;
    SWR_CONTEXT *pContext = pDC->pContext;

#ifdef KNOB_ENABLE_RDTSC
    uint32_t numTiles = 0;
#endif
    SWR_FORMAT srcFormat;
    switch (pDesc->attachment)
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
    default: SWR_ASSERT(false, "Unknown attachment: %d", pDesc->attachment); srcFormat = KNOB_COLOR_HOT_TILE_FORMAT; break;
    }

    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroTile, x, y);

    // Only need to store the hottile if it's been rendered to...
    HOTTILE *pHotTile = pContext->pHotTileMgr->GetHotTile(pContext, pDC, macroTile, pDesc->attachment, false);
    if (pHotTile)
    {
        // clear if clear is pending (i.e., not rendered to), then mark as dirty for store.
        if (pHotTile->state == HOTTILE_CLEAR)
        {
            PFN_CLEAR_TILES pfnClearTiles = sClearTilesTable[srcFormat];
            SWR_ASSERT(pfnClearTiles != nullptr);

            pfnClearTiles(pDC, pDesc->attachment, macroTile, pHotTile->clearData);
        }

        if (pHotTile->state == HOTTILE_DIRTY || pDesc->postStoreTileState == (SWR_TILE_STATE)HOTTILE_DIRTY)
        {
            int destX = KNOB_MACROTILE_X_DIM * x;
            int destY = KNOB_MACROTILE_Y_DIM * y;

            pContext->pfnStoreTile(GetPrivateState(pDC), srcFormat,
                pDesc->attachment, destX, destY, pHotTile->renderTargetArrayIndex, pHotTile->pBuffer);
        }
        

        if (pHotTile->state == HOTTILE_DIRTY || pHotTile->state == HOTTILE_RESOLVED)
        {
            pHotTile->state = (HOTTILE_STATE)pDesc->postStoreTileState;
        }
    }
    RDTSC_STOP(BEStoreTiles, numTiles, pDC->drawId);
}


void ProcessDiscardInvalidateTilesBE(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t macroTile, void *pData)
{
    DISCARD_INVALIDATE_TILES_DESC *pDesc = (DISCARD_INVALIDATE_TILES_DESC *)pData;
    SWR_CONTEXT *pContext = pDC->pContext;

    const int numSamples = GetNumSamples(pDC->pState->state.rastState.sampleCount);

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

INLINE
bool CanEarlyZ(const SWR_PS_STATE *pPSState)
{
    return (pPSState->forceEarlyZ || (!pPSState->writesODepth && !pPSState->usesSourceDepth && !pPSState->usesUAV));
}

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
    RDTSC_START(BESingleSampleBackend);
    RDTSC_START(BESetup);

    SWR_CONTEXT *pContext = pDC->pContext;
    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_PS_STATE *pPSState = &state.psState;
    const SWR_BLEND_STATE *pBlendState = &state.blendState;
    uint64_t coverageMask = work.coverageMask[0];

    // broadcast scalars
    BarycentricCoeffs coeffs;
    coeffs.vIa = _simd_broadcast_ss(&work.I[0]);
    coeffs.vIb = _simd_broadcast_ss(&work.I[1]);
    coeffs.vIc = _simd_broadcast_ss(&work.I[2]);

    coeffs.vJa = _simd_broadcast_ss(&work.J[0]);
    coeffs.vJb = _simd_broadcast_ss(&work.J[1]);
    coeffs.vJc = _simd_broadcast_ss(&work.J[2]);

    coeffs.vZa = _simd_broadcast_ss(&work.Z[0]);
    coeffs.vZb = _simd_broadcast_ss(&work.Z[1]);
    coeffs.vZc = _simd_broadcast_ss(&work.Z[2]);

    coeffs.vRecipDet = _simd_broadcast_ss(&work.recipDet);

    coeffs.vAOneOverW = _simd_broadcast_ss(&work.OneOverW[0]);
    coeffs.vBOneOverW = _simd_broadcast_ss(&work.OneOverW[1]);
    coeffs.vCOneOverW = _simd_broadcast_ss(&work.OneOverW[2]);

    uint8_t *pColorBase[SWR_NUM_RENDERTARGETS];
    uint32_t NumRT = state.psState.numRenderTargets;
    for(uint32_t rt = 0; rt < NumRT; ++rt)
    {
        pColorBase[rt] = renderBuffers.pColor[rt];
    }
    uint8_t *pDepthBase = renderBuffers.pDepth, *pStencilBase = renderBuffers.pStencil;
    RDTSC_STOP(BESetup, 0, 0);

    SWR_PS_CONTEXT psContext;
    psContext.pAttribs = work.pAttribs;
    psContext.pPerspAttribs = work.pPerspAttribs;
    psContext.frontFace = work.triFlags.frontFacing;
    psContext.primID = work.triFlags.primID;

    // save Ia/Ib/Ic and Ja/Jb/Jc if we need to reevaluate i/j/k in the shader because of pull attribs
    psContext.I = work.I;
    psContext.J = work.J;
    psContext.recipDet = work.recipDet;
    psContext.pRecipW = work.pRecipW;
    psContext.pSamplePosX = (const float*)&T::MultisampleT::samplePosX;
    psContext.pSamplePosY = (const float*)&T::MultisampleT::samplePosY;

    for(uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        // UL pixel corner
        psContext.vY.UL = _simd_add_ps(vULOffsetsY, _simd_set1_ps((float)yy));
        // pixel center
        psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps((float)yy));

        for(uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            if(coverageMask & MASK)
            {
                psContext.vX.UL = _simd_add_ps(vULOffsetsX, _simd_set1_ps((float)xx));
                // pixel center
                psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps((float)xx));

                if(T::bInputCoverage)
                {
                    generateInputCoverage<T>(&work.coverageMask[0], psContext.inputMask, pBlendState->sampleMask);
                }

                RDTSC_START(BEBarycentric);
                CalcPixelBarycentrics(coeffs, psContext);

                if(T::bCentroidPos)
                {
                    // for 1x case, centroid is pixel center
                    psContext.vX.centroid = psContext.vX.center;
                    psContext.vY.centroid = psContext.vY.center;
                    psContext.vI.centroid = psContext.vI.center;
                    psContext.vJ.centroid = psContext.vJ.center;
                    psContext.vOneOverW.centroid = psContext.vOneOverW.center;
                }

                // interpolate and quantize z
                psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.center, psContext.vJ.center);
                psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);
                RDTSC_STOP(BEBarycentric, 0, 0);

                simdmask clipCoverageMask = coverageMask & MASK;
                // interpolate user clip distance if available
                if(rastState.clipDistanceMask)
                {
                    clipCoverageMask &= ~ComputeUserClipMask(rastState.clipDistanceMask, work.pUserClipBuffer,
                                                             psContext.vI.center, psContext.vJ.center);
                }

                simdscalar vCoverageMask = vMask(clipCoverageMask);
                simdscalar depthPassMask = vCoverageMask;
                simdscalar stencilPassMask = vCoverageMask;

                // Early-Z?
                if(T::bCanEarlyZ)
                {
                    RDTSC_START(BEEarlyDepthTest);
                    depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing,
                                                        psContext.vZ, pDepthBase, vCoverageMask, pStencilBase, &stencilPassMask);
                    RDTSC_STOP(BEEarlyDepthTest, 0, 0);

                    // early-exit if no pixels passed depth or earlyZ is forced on
                    if(pPSState->forceEarlyZ || !_simd_movemask_ps(depthPassMask))
                    {
                        DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                                            pDepthBase, depthPassMask, vCoverageMask, pStencilBase, stencilPassMask);

                        if (!_simd_movemask_ps(depthPassMask))
                        {
                            goto Endtile;
                        }
                    }
                }

                psContext.sampleIndex = 0;
                psContext.activeMask = _simd_castps_si(vCoverageMask);

                // execute pixel shader
                RDTSC_START(BEPixelShader);
                UPDATE_STAT(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(vCoverageMask)));
                state.psState.pfnPixelShader(GetPrivateState(pDC), &psContext);
                RDTSC_STOP(BEPixelShader, 0, 0);

                vCoverageMask = _simd_castsi_ps(psContext.activeMask);

                // late-Z
                if(!T::bCanEarlyZ)
                {
                    RDTSC_START(BELateDepthTest);
                    depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing,
                                                        psContext.vZ, pDepthBase, vCoverageMask, pStencilBase, &stencilPassMask);
                    RDTSC_STOP(BELateDepthTest, 0, 0);

                    if(!_simd_movemask_ps(depthPassMask))
                    {
                        // need to call depth/stencil write for stencil write
                        DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                                            pDepthBase, depthPassMask, vCoverageMask, pStencilBase, stencilPassMask);
                        goto Endtile;
                    }
                }

                uint32_t statMask = _simd_movemask_ps(depthPassMask);
                uint32_t statCount = _mm_popcnt_u32(statMask);
                UPDATE_STAT(DepthPassCount, statCount);

                // output merger
                RDTSC_START(BEOutputMerger);
                OutputMerger(psContext, pColorBase, 0, pBlendState, state.pfnBlendFunc, vCoverageMask, depthPassMask, pPSState->numRenderTargets);

                // do final depth write after all pixel kills
                if (!pPSState->forceEarlyZ)
                {
                    DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                        pDepthBase, depthPassMask, vCoverageMask, pStencilBase, stencilPassMask);
                }
                RDTSC_STOP(BEOutputMerger, 0, 0);
            }

Endtile:
            RDTSC_START(BEEndTile);
            coverageMask >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            pDepthBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            for(uint32_t rt = 0; rt < NumRT; ++rt)
            {
                pColorBase[rt] += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
            }
            RDTSC_STOP(BEEndTile, 0, 0);
        }
    }
    RDTSC_STOP(BESingleSampleBackend, 0, 0);
}

template<typename T>
void BackendSampleRate(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers)
{
    RDTSC_START(BESampleRateBackend);
    RDTSC_START(BESetup);

    SWR_CONTEXT *pContext = pDC->pContext;
    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_PS_STATE *pPSState = &state.psState;
    const SWR_BLEND_STATE *pBlendState = &state.blendState;

    // broadcast scalars
    BarycentricCoeffs coeffs;
    coeffs.vIa = _simd_broadcast_ss(&work.I[0]);
    coeffs.vIb = _simd_broadcast_ss(&work.I[1]);
    coeffs.vIc = _simd_broadcast_ss(&work.I[2]);

    coeffs.vJa = _simd_broadcast_ss(&work.J[0]);
    coeffs.vJb = _simd_broadcast_ss(&work.J[1]);
    coeffs.vJc = _simd_broadcast_ss(&work.J[2]);

    coeffs.vZa = _simd_broadcast_ss(&work.Z[0]);
    coeffs.vZb = _simd_broadcast_ss(&work.Z[1]);
    coeffs.vZc = _simd_broadcast_ss(&work.Z[2]);

    coeffs.vRecipDet = _simd_broadcast_ss(&work.recipDet);

    coeffs.vAOneOverW = _simd_broadcast_ss(&work.OneOverW[0]);
    coeffs.vBOneOverW = _simd_broadcast_ss(&work.OneOverW[1]);
    coeffs.vCOneOverW = _simd_broadcast_ss(&work.OneOverW[2]);

    uint8_t *pColorBase[SWR_NUM_RENDERTARGETS];
    uint32_t NumRT = state.psState.numRenderTargets;
    for(uint32_t rt = 0; rt < NumRT; ++rt)
    {
        pColorBase[rt] = renderBuffers.pColor[rt];
    }
    uint8_t *pDepthBase = renderBuffers.pDepth, *pStencilBase = renderBuffers.pStencil;
    RDTSC_STOP(BESetup, 0, 0);

    SWR_PS_CONTEXT psContext;
    psContext.pAttribs = work.pAttribs;
    psContext.pPerspAttribs = work.pPerspAttribs;
    psContext.pRecipW = work.pRecipW;
    psContext.frontFace = work.triFlags.frontFacing;
    psContext.primID = work.triFlags.primID;

    // save Ia/Ib/Ic and Ja/Jb/Jc if we need to reevaluate i/j/k in the shader because of pull attribs
    psContext.I = work.I;
    psContext.J = work.J;
    psContext.recipDet = work.recipDet;
    psContext.pSamplePosX = (const float*)&T::MultisampleT::samplePosX;
    psContext.pSamplePosY = (const float*)&T::MultisampleT::samplePosY;

    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        // UL pixel corner
        psContext.vY.UL = _simd_add_ps(vULOffsetsY, _simd_set1_ps((float)yy));
        // pixel center
        psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps((float)yy));
        
        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            psContext.vX.UL = _simd_add_ps(vULOffsetsX, _simd_set1_ps((float)xx));
            // pixel center
            psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps((float)xx));

            RDTSC_START(BEBarycentric);
            CalcPixelBarycentrics(coeffs, psContext);
            RDTSC_STOP(BEBarycentric, 0, 0);

            if(T::bInputCoverage)
            {
                generateInputCoverage<T>(&work.coverageMask[0], psContext.inputMask, pBlendState->sampleMask);
            }

            if(T::bCentroidPos)
            {
                ///@ todo: don't need to genererate input coverage 2x if input coverage and centroid
                RDTSC_START(BEBarycentric);
                if(T::bIsStandardPattern)
                {
                    CalcCentroidPos<T>(psContext, &work.coverageMask[0], pBlendState->sampleMask, psContext.vX.UL, psContext.vY.UL);
                }
                else
                {
                    psContext.vX.centroid = _simd_add_ps(psContext.vX.UL, _simd_set1_ps(0.5f));
                    psContext.vY.centroid = _simd_add_ps(psContext.vY.UL, _simd_set1_ps(0.5f));
                }
                CalcCentroidBarycentrics(coeffs, psContext, psContext.vX.UL, psContext.vY.UL);
                RDTSC_STOP(BEBarycentric, 0, 0);
            }

            for(uint32_t sample = 0; sample < T::MultisampleT::numSamples; sample++)
            {
                simdmask coverageMask = work.coverageMask[sample] & MASK;
                if (coverageMask)
                {
                    RDTSC_START(BEBarycentric);
                    // calculate per sample positions
                    psContext.vX.sample = _simd_add_ps(psContext.vX.UL, T::MultisampleT::vX(sample));
                    psContext.vY.sample = _simd_add_ps(psContext.vY.UL, T::MultisampleT::vY(sample));

                    CalcSampleBarycentrics(coeffs, psContext);

                    // interpolate and quantize z
                    psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.sample, psContext.vJ.sample);
                    psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);
                    RDTSC_STOP(BEBarycentric, 0, 0);

                    // interpolate user clip distance if available
                    if (rastState.clipDistanceMask)
                    {
                        coverageMask &= ~ComputeUserClipMask(rastState.clipDistanceMask, work.pUserClipBuffer,
                            psContext.vI.sample, psContext.vJ.sample);
                    }

                    simdscalar vCoverageMask = vMask(coverageMask);
                    simdscalar depthPassMask = vCoverageMask;
                    simdscalar stencilPassMask = vCoverageMask;

                    // offset depth/stencil buffers current sample
                    uint8_t *pDepthSample = pDepthBase + RasterTileDepthOffset(sample);
                    uint8_t *pStencilSample = pStencilBase + RasterTileStencilOffset(sample);

                    // Early-Z?
                    if (T::bCanEarlyZ)
                    {
                        RDTSC_START(BEEarlyDepthTest);
                        depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing,
                                              psContext.vZ, pDepthSample, vCoverageMask, pStencilSample, &stencilPassMask);
                        RDTSC_STOP(BEEarlyDepthTest, 0, 0);

                        // early-exit if no samples passed depth or earlyZ is forced on.
                        if (pPSState->forceEarlyZ || !_simd_movemask_ps(depthPassMask))
                        {
                            DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
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
                    RDTSC_START(BEPixelShader);
                    UPDATE_STAT(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(vCoverageMask)));
                    state.psState.pfnPixelShader(GetPrivateState(pDC), &psContext);
                    RDTSC_STOP(BEPixelShader, 0, 0);

                    vCoverageMask = _simd_castsi_ps(psContext.activeMask);

                    // late-Z
                    if (!T::bCanEarlyZ)
                    {
                        RDTSC_START(BELateDepthTest);
                        depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing,
                                              psContext.vZ, pDepthSample, vCoverageMask, pStencilSample, &stencilPassMask);
                        RDTSC_STOP(BELateDepthTest, 0, 0);

                        if (!_simd_movemask_ps(depthPassMask))
                        {
                            // need to call depth/stencil write for stencil write
                            DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                                pDepthSample, depthPassMask, vCoverageMask, pStencilSample, stencilPassMask);

                            work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
                            continue;
                        }
                    }

                    uint32_t statMask = _simd_movemask_ps(depthPassMask);
                    uint32_t statCount = _mm_popcnt_u32(statMask);
                    UPDATE_STAT(DepthPassCount, statCount);

                    // output merger
                    RDTSC_START(BEOutputMerger);
                    OutputMerger(psContext, pColorBase, sample, pBlendState, state.pfnBlendFunc, vCoverageMask, depthPassMask, pPSState->numRenderTargets);

                    // do final depth write after all pixel kills
                    if (!pPSState->forceEarlyZ)
                    {
                        DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                            pDepthSample, depthPassMask, vCoverageMask, pStencilSample, stencilPassMask);
                    }
                    RDTSC_STOP(BEOutputMerger, 0, 0);
                }
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }
            RDTSC_START(BEEndTile);
            pDepthBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            for (uint32_t rt = 0; rt < NumRT; ++rt)
            {
                pColorBase[rt] += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
            }
            RDTSC_STOP(BEEndTile, 0, 0);
        }
    }
    RDTSC_STOP(BESampleRateBackend, 0, 0);
}

template<typename T>
void BackendPixelRate(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers)
{
    RDTSC_START(BEPixelRateBackend);
    RDTSC_START(BESetup);

    SWR_CONTEXT *pContext = pDC->pContext;
    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;
    const SWR_PS_STATE *pPSState = &state.psState;
    const SWR_BLEND_STATE *pBlendState = &state.blendState;

    // broadcast scalars
    BarycentricCoeffs coeffs;
    coeffs.vIa = _simd_broadcast_ss(&work.I[0]);
    coeffs.vIb = _simd_broadcast_ss(&work.I[1]);
    coeffs.vIc = _simd_broadcast_ss(&work.I[2]);

    coeffs.vJa = _simd_broadcast_ss(&work.J[0]);
    coeffs.vJb = _simd_broadcast_ss(&work.J[1]);
    coeffs.vJc = _simd_broadcast_ss(&work.J[2]);

    coeffs.vZa = _simd_broadcast_ss(&work.Z[0]);
    coeffs.vZb = _simd_broadcast_ss(&work.Z[1]);
    coeffs.vZc = _simd_broadcast_ss(&work.Z[2]);

    coeffs.vRecipDet = _simd_broadcast_ss(&work.recipDet);

    coeffs.vAOneOverW = _simd_broadcast_ss(&work.OneOverW[0]);
    coeffs.vBOneOverW = _simd_broadcast_ss(&work.OneOverW[1]);
    coeffs.vCOneOverW = _simd_broadcast_ss(&work.OneOverW[2]);

    uint8_t *pColorBase[SWR_NUM_RENDERTARGETS];
    uint32_t NumRT = state.psState.numRenderTargets;
    for(uint32_t rt = 0; rt < NumRT; ++rt)
    {
        pColorBase[rt] = renderBuffers.pColor[rt];
    }
    uint8_t *pDepthBase = renderBuffers.pDepth, *pStencilBase = renderBuffers.pStencil;
    RDTSC_STOP(BESetup, 0, 0);

    SWR_PS_CONTEXT psContext;
    psContext.pAttribs = work.pAttribs;
    psContext.pPerspAttribs = work.pPerspAttribs;
    psContext.frontFace = work.triFlags.frontFacing;
    psContext.primID = work.triFlags.primID;
    psContext.pRecipW = work.pRecipW;
    // save Ia/Ib/Ic and Ja/Jb/Jc if we need to reevaluate i/j/k in the shader because of pull attribs
    psContext.I = work.I;
    psContext.J = work.J;
    psContext.recipDet = work.recipDet;
    psContext.pSamplePosX = (const float*)&T::MultisampleT::samplePosX;
    psContext.pSamplePosY = (const float*)&T::MultisampleT::samplePosY;
    psContext.sampleIndex = 0;
    
    PixelRateZTestLoop<T> PixelRateZTest(pDC, work, coeffs, state, pDepthBase, pStencilBase, rastState.clipDistanceMask);

    for(uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        psContext.vY.UL = _simd_add_ps(vULOffsetsY, _simd_set1_ps((float)yy));
        psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps((float)yy));
        for(uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            simdscalar activeLanes;
            if(!(work.anyCoveredSamples & MASK)) {goto Endtile;};
            activeLanes = vMask(work.anyCoveredSamples & MASK);

            psContext.vX.UL = _simd_add_ps(vULOffsetsX, _simd_set1_ps((float)xx));
            // set pixel center positions
            psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps((float)xx));

            RDTSC_START(BEBarycentric);
            CalcPixelBarycentrics(coeffs, psContext);
            RDTSC_STOP(BEBarycentric, 0, 0);

            if (T::bInputCoverage)
            {
                generateInputCoverage<T>(&work.coverageMask[0], psContext.inputMask, pBlendState->sampleMask);
            }

            if(T::bCentroidPos)
            {
                ///@ todo: don't need to genererate input coverage 2x if input coverage and centroid
                RDTSC_START(BEBarycentric);
                if(T::bIsStandardPattern)
                {
                    CalcCentroidPos<T>(psContext, &work.coverageMask[0], pBlendState->sampleMask, psContext.vX.UL, psContext.vY.UL);
                }
                else
                {
                    psContext.vX.centroid = _simd_add_ps(psContext.vX.UL, _simd_set1_ps(0.5f));
                    psContext.vY.centroid = _simd_add_ps(psContext.vY.UL, _simd_set1_ps(0.5f));
                }
                CalcCentroidBarycentrics(coeffs, psContext, psContext.vX.UL, psContext.vY.UL);
                RDTSC_STOP(BEBarycentric, 0, 0);
            }

            if(T::bForcedSampleCount)
            {
                // candidate pixels (that passed coverage) will cause shader invocation if any bits in the samplemask are set
                const simdscalar vSampleMask = _simd_castsi_ps(_simd_cmpgt_epi32(_simd_set1_epi32(pBlendState->sampleMask), _simd_setzero_si()));
                activeLanes = _simd_and_ps(activeLanes, vSampleMask);
            }

            // Early-Z?
            if(T::bCanEarlyZ && !T::bForcedSampleCount)
            {
                uint32_t depthPassCount = PixelRateZTest(activeLanes, psContext, BEEarlyDepthTest);
                UPDATE_STAT(DepthPassCount, depthPassCount);
            }

            // if we have no covered samples that passed depth at this point, go to next tile
            if(!_simd_movemask_ps(activeLanes)) { goto Endtile; };

            if(pPSState->usesSourceDepth)
            {
                RDTSC_START(BEBarycentric);
                // interpolate and quantize z
                psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.center, psContext.vJ.center);
                psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);
                RDTSC_STOP(BEBarycentric, 0, 0);
            }

            // pixels that are currently active
            psContext.activeMask = _simd_castps_si(activeLanes);
            psContext.oMask = T::MultisampleT::FullSampleMask();

            // execute pixel shader
            RDTSC_START(BEPixelShader);
            state.psState.pfnPixelShader(GetPrivateState(pDC), &psContext);
            UPDATE_STAT(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(activeLanes)));
            RDTSC_STOP(BEPixelShader, 0, 0);

            // update active lanes to remove any discarded or oMask'd pixels
            activeLanes = _simd_castsi_ps(_simd_and_si(psContext.activeMask, _simd_cmpgt_epi32(psContext.oMask, _simd_setzero_si())));
            if(!_simd_movemask_ps(activeLanes)) { goto Endtile; };

            // late-Z
            if(!T::bCanEarlyZ && !T::bForcedSampleCount)
            {
                uint32_t depthPassCount = PixelRateZTest(activeLanes, psContext, BELateDepthTest);
                UPDATE_STAT(DepthPassCount, depthPassCount);
            }

            // if we have no covered samples that passed depth at this point, skip OM and go to next tile
            if(!_simd_movemask_ps(activeLanes)) { goto Endtile; };

            // output merger
            // loop over all samples, broadcasting the results of the PS to all passing pixels
            for(uint32_t sample = 0; sample < GetNumOMSamples<T>(pBlendState->sampleCount); sample++)
            {
                RDTSC_START(BEOutputMerger);
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
                        RDTSC_STOP(BEOutputMerger, 0, 0);
                        continue;
                    }
                }
                
                // broadcast the results of the PS to all passing pixels
                OutputMerger(psContext, pColorBase, sample, pBlendState, state.pfnBlendFunc, coverageMask, depthMask, pPSState->numRenderTargets);

                if(!pPSState->forceEarlyZ && !T::bForcedSampleCount)
                {
                    uint8_t *pDepthSample = pDepthBase + RasterTileDepthOffset(sample);
                    uint8_t * pStencilSample = pStencilBase + RasterTileStencilOffset(sample);

                    DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, PixelRateZTest.vZ[coverageSampleNum],
                                      pDepthSample, depthMask, coverageMask, pStencilSample, PixelRateZTest.stencilPassMask[coverageSampleNum]);
                }
                RDTSC_STOP(BEOutputMerger, 0, 0);        
            }
Endtile:
            RDTSC_START(BEEndTile);
            for(uint32_t sample = 0; sample < T::MultisampleT::numCoverageSamples; sample++)
            {
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

            work.anyCoveredSamples >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            pDepthBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            for(uint32_t rt = 0; rt < NumRT; ++rt)
            {
                pColorBase[rt] += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
            }
            RDTSC_STOP(BEEndTile, 0, 0);
        }
    }
    RDTSC_STOP(BEPixelRateBackend, 0, 0);
}
// optimized backend flow with NULL PS
template<uint32_t sampleCountT>
void BackendNullPS(DRAW_CONTEXT *pDC, uint32_t workerId, uint32_t x, uint32_t y, SWR_TRIANGLE_DESC &work, RenderOutputBuffers &renderBuffers)
{
    RDTSC_START(BENullBackend);
    ///@todo: handle center multisample pattern
    typedef SwrBackendTraits<sampleCountT, SWR_MSAA_STANDARD_PATTERN> T;
    RDTSC_START(BESetup);

    SWR_CONTEXT *pContext = pDC->pContext;
    const API_STATE& state = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = pDC->pState->state.rastState;

    // broadcast scalars
    BarycentricCoeffs coeffs;
    coeffs.vIa = _simd_broadcast_ss(&work.I[0]);
    coeffs.vIb = _simd_broadcast_ss(&work.I[1]);
    coeffs.vIc = _simd_broadcast_ss(&work.I[2]);

    coeffs.vJa = _simd_broadcast_ss(&work.J[0]);
    coeffs.vJb = _simd_broadcast_ss(&work.J[1]);
    coeffs.vJc = _simd_broadcast_ss(&work.J[2]);

    coeffs.vZa = _simd_broadcast_ss(&work.Z[0]);
    coeffs.vZb = _simd_broadcast_ss(&work.Z[1]);
    coeffs.vZc = _simd_broadcast_ss(&work.Z[2]);

    coeffs.vRecipDet = _simd_broadcast_ss(&work.recipDet);

    uint8_t *pDepthBase = renderBuffers.pDepth, *pStencilBase = renderBuffers.pStencil;

    RDTSC_STOP(BESetup, 0, 0);

    SWR_PS_CONTEXT psContext;
    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        // UL pixel corner
        simdscalar vYSamplePosUL = _simd_add_ps(vULOffsetsY, _simd_set1_ps((float)yy));

        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            // UL pixel corners
            simdscalar vXSamplePosUL = _simd_add_ps(vULOffsetsX, _simd_set1_ps((float)xx));

            // iterate over active samples
            unsigned long sample = 0;
            uint32_t sampleMask = state.blendState.sampleMask;
            while (_BitScanForward(&sample, sampleMask))
            {
                sampleMask &= ~(1 << sample);
                simdmask coverageMask = work.coverageMask[sample] & MASK;
                if (coverageMask)
                {
                    RDTSC_START(BEBarycentric);
                    // calculate per sample positions
                    psContext.vX.sample = _simd_add_ps(vXSamplePosUL, T::MultisampleT::vX(sample));
                    psContext.vY.sample = _simd_add_ps(vYSamplePosUL, T::MultisampleT::vY(sample));

                    CalcSampleBarycentrics(coeffs, psContext);

                    // interpolate and quantize z
                    psContext.vZ = vplaneps(coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.sample, psContext.vJ.sample);
                    psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);

                    RDTSC_STOP(BEBarycentric, 0, 0);

                    // interpolate user clip distance if available
                    if (rastState.clipDistanceMask)
                    {
                        coverageMask &= ~ComputeUserClipMask(rastState.clipDistanceMask, work.pUserClipBuffer,
                            psContext.vI.sample, psContext.vJ.sample);
                    }

                    simdscalar vCoverageMask = vMask(coverageMask);
                    simdscalar stencilPassMask = vCoverageMask;

                    // offset depth/stencil buffers current sample
                    uint8_t *pDepthSample = pDepthBase + RasterTileDepthOffset(sample);
                    uint8_t *pStencilSample = pStencilBase + RasterTileStencilOffset(sample);

                    RDTSC_START(BEEarlyDepthTest);
                    simdscalar depthPassMask = DepthStencilTest(&state, work.triFlags.frontFacing,
                        psContext.vZ, pDepthSample, vCoverageMask, pStencilSample, &stencilPassMask);
                    DepthStencilWrite(&state.vp[0], &state.depthStencilState, work.triFlags.frontFacing, psContext.vZ,
                        pDepthSample, depthPassMask, vCoverageMask, pStencilSample, stencilPassMask);
                    RDTSC_STOP(BEEarlyDepthTest, 0, 0);

                    uint32_t statMask = _simd_movemask_ps(depthPassMask);
                    uint32_t statCount = _mm_popcnt_u32(statMask);
                    UPDATE_STAT(DepthPassCount, statCount);
                }
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }
            pDepthBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBase += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;
        }
    }
    RDTSC_STOP(BENullBackend, 0, 0);
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

PFN_BACKEND_FUNC gBackendNullPs[SWR_MULTISAMPLE_TYPE_MAX];
PFN_BACKEND_FUNC gBackendSingleSample[2] // input coverage
                                     [2] // centroid
                                     [2] // canEarlyZ
                                     = {};
PFN_BACKEND_FUNC gBackendPixelRateTable[SWR_MULTISAMPLE_TYPE_MAX]
                                       [SWR_MSAA_SAMPLE_PATTERN_MAX]
                                       [SWR_INPUT_COVERAGE_MAX]
                                       [2] // centroid
                                       [2] // forcedSampleCount
                                       [2] // canEarlyZ
                                       = {};
PFN_BACKEND_FUNC gBackendSampleRateTable[SWR_MULTISAMPLE_TYPE_MAX][SWR_INPUT_COVERAGE_MAX]
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

void InitBackendSingleFuncTable(PFN_BACKEND_FUNC (&table)[2][2][2])
{
    for(uint32_t inputCoverage = SWR_INPUT_COVERAGE_NONE; inputCoverage < SWR_INPUT_COVERAGE_MAX; inputCoverage++)
    {
        for(uint32_t isCentroid = 0; isCentroid < 2; isCentroid++)
        {
            for(uint32_t canEarlyZ = 0; canEarlyZ < 2; canEarlyZ++)
            {
                table[inputCoverage][isCentroid][canEarlyZ] =
                    BEChooser<>::GetFunc(SWR_MULTISAMPLE_1X, SWR_MSAA_STANDARD_PATTERN, (inputCoverage == SWR_INPUT_COVERAGE_NORMAL),
                                         (isCentroid > 0), false, (canEarlyZ > 0), SWR_BACKEND_SINGLE_SAMPLE);
            }
        }
    }
}

void InitBackendPixelFuncTable(PFN_BACKEND_FUNC (&table)[SWR_MULTISAMPLE_TYPE_MAX][SWR_MSAA_SAMPLE_PATTERN_MAX][SWR_INPUT_COVERAGE_MAX]
                                                        [2][2][2])
{
    for(uint32_t sampleCount = SWR_MULTISAMPLE_1X; sampleCount < SWR_MULTISAMPLE_TYPE_MAX; sampleCount++)
    {
        for(uint32_t samplePattern = SWR_MSAA_CENTER_PATTERN; samplePattern < SWR_MSAA_SAMPLE_PATTERN_MAX; samplePattern++)
        {
            for(uint32_t inputCoverage = SWR_INPUT_COVERAGE_NONE; inputCoverage < SWR_INPUT_COVERAGE_MAX; inputCoverage++)
            {
                for(uint32_t isCentroid = 0; isCentroid < 2; isCentroid++)
                {
                    for(uint32_t forcedSampleCount = 0; forcedSampleCount < 2; forcedSampleCount++)
                    {
                        for(uint32_t canEarlyZ = 0; canEarlyZ < 2; canEarlyZ++)
                        {
                            table[sampleCount][samplePattern][inputCoverage][isCentroid][forcedSampleCount][canEarlyZ] =
                                BEChooser<>::GetFunc((SWR_MULTISAMPLE_COUNT)sampleCount, (SWR_MSAA_SAMPLE_PATTERN)samplePattern, (inputCoverage == SWR_INPUT_COVERAGE_NORMAL), 
                                                        (isCentroid > 0), (forcedSampleCount > 0), (canEarlyZ > 0), SWR_BACKEND_MSAA_PIXEL_RATE);
                        }
                    }
                }
            }
        }
    }
}

void InitBackendSampleFuncTable(PFN_BACKEND_FUNC (&table)[SWR_MULTISAMPLE_TYPE_MAX][SWR_INPUT_COVERAGE_MAX][2][2])
{
    for(uint32_t sampleCount = SWR_MULTISAMPLE_1X; sampleCount < SWR_MULTISAMPLE_TYPE_MAX; sampleCount++)
    {
        for(uint32_t inputCoverage = SWR_INPUT_COVERAGE_NONE; inputCoverage < SWR_INPUT_COVERAGE_MAX; inputCoverage++)
        {
            for(uint32_t centroid = 0; centroid < 2; centroid++)
            {
                for(uint32_t canEarlyZ = 0; canEarlyZ < 2; canEarlyZ++)
                {
                    table[sampleCount][inputCoverage][centroid][canEarlyZ] =
                        BEChooser<>::GetFunc((SWR_MULTISAMPLE_COUNT)sampleCount, SWR_MSAA_STANDARD_PATTERN, (inputCoverage == SWR_INPUT_COVERAGE_NORMAL), 
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
