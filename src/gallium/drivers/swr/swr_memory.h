/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#pragma once

void LoadHotTile(
    const SWR_SURFACE_STATE *pSrcSurface,
    SWR_FORMAT dstFormat,
    SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
    UINT x, UINT y, uint32_t renderTargetArrayIndex,
    uint8_t *pDstHotTile);

void StoreHotTileToSurface(
    SWR_SURFACE_STATE *pDstSurface,
    SWR_FORMAT srcFormat,
    SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
    UINT x, UINT y, uint32_t renderTargetArrayIndex,
    uint8_t *pSrcHotTile);

void StoreHotTileClear(
    SWR_SURFACE_STATE *pDstSurface,
    SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
    UINT x,
    UINT y,
    uint32_t renderTargetArrayIndex,
    const float* pClearColor);

INLINE void
swr_LoadHotTile(HANDLE hPrivateContext,
                SWR_FORMAT dstFormat,
                SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
                UINT x, UINT y,
                uint32_t renderTargetArrayIndex, uint8_t* pDstHotTile)
{
   // Grab source surface state from private context
   swr_draw_context *pDC = (swr_draw_context*)hPrivateContext;
   SWR_SURFACE_STATE *pSrcSurface = &pDC->renderTargets[renderTargetIndex];

   LoadHotTile(pSrcSurface, dstFormat, renderTargetIndex, x, y, renderTargetArrayIndex, pDstHotTile);
}

INLINE void
swr_StoreHotTile(HANDLE hPrivateContext,
                 SWR_FORMAT srcFormat,
                 SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
                 UINT x, UINT y,
                 uint32_t renderTargetArrayIndex, uint8_t* pSrcHotTile)
{
   // Grab destination surface state from private context
   swr_draw_context *pDC = (swr_draw_context*)hPrivateContext;
   SWR_SURFACE_STATE *pDstSurface = &pDC->renderTargets[renderTargetIndex];

   StoreHotTileToSurface(pDstSurface, srcFormat, renderTargetIndex, x, y, renderTargetArrayIndex, pSrcHotTile);
}

INLINE void
swr_StoreHotTileClear(HANDLE hPrivateContext,
                      SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
                      UINT x,
                      UINT y,
                      uint32_t renderTargetArrayIndex,
                      const float* pClearColor)
{
   // Grab destination surface state from private context
   swr_draw_context *pDC = (swr_draw_context*)hPrivateContext;
   SWR_SURFACE_STATE *pDstSurface = &pDC->renderTargets[renderTargetIndex];

   StoreHotTileClear(pDstSurface, renderTargetIndex, x, y, renderTargetArrayIndex, pClearColor);
}

void InitSimLoadTilesTable();
void InitSimStoreTilesTable();
void InitSimClearTilesTable();

/* Init Load/Store/ClearTiles Tables */
INLINE void swr_InitMemoryModule()
{
   InitSimLoadTilesTable();
   InitSimStoreTilesTable();
   InitSimClearTilesTable();
}
