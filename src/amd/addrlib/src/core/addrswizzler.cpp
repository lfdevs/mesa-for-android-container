
/*
************************************************************************************************************************
*
*  Copyright (C) 2024-2026 Advanced Micro Devices, Inc.  All rights reserved.
*  SPDX-License-Identifier: MIT
*
***********************************************************************************************************************/

/**
****************************************************************************************************
* @file  addrswizzler.cpp
* @brief Contains code for efficient CPU swizzling.
****************************************************************************************************
*/

#include "addrswizzler.h"
#include "addrswizzlersimd.h"

namespace Addr
{

/**
****************************************************************************************************
*   LutAddresser::LutAddresser
*
*   @brief
*       Constructor for the LutAddresser class.
****************************************************************************************************
*/
LutAddresser::LutAddresser()
    :
    m_pXLut(&m_lutData[0]),
    m_pYLut(&m_lutData[0]),
    m_pZLut(&m_lutData[0]),
    m_pSLut(&m_lutData[0]),
    m_xLutMask(0),
    m_yLutMask(0),
    m_zLutMask(0),
    m_sLutMask(0),
    m_blockBits(0),
    m_blockSize(),
    m_bpeLog2(0),
    m_bit(),
    m_lutData()
{
}

/**
****************************************************************************************************
*   LutAddresser::Init
*
*   @brief
*       Calculates general properties about the swizzle
****************************************************************************************************
*/
void LutAddresser::Init(
    const ADDR_BIT_SETTING* pEq,
    UINT_32                 eqSize,
    ADDR_EXTENT3D           blockSize,
    UINT_8                  blockBits)
{
    ADDR_ASSERT(eqSize <= ADDR_MAX_EQUATION_BIT);
    memcpy(&m_bit[0], pEq, sizeof(ADDR_BIT_SETTING) * eqSize);
    m_blockSize = blockSize;
    m_blockBits = blockBits;

    InitSwizzleProps();
    InitLuts();
}

/**
****************************************************************************************************
*   LutAddresser::InitSwizzleProps
*
*   @brief
*       Calculates general properties about the swizzle
****************************************************************************************************
*/
void LutAddresser::InitSwizzleProps()
{
    // Calculate BPE from the swizzle. This can be derived from the number of invalid low bits.
    m_bpeLog2 = 0;
    for (UINT_32 i = 0; i < MaxElementBytesLog2; i++)
    {
        if (m_bit[i].value != 0)
        {
            break;
        }
        m_bpeLog2++;
    }

    // Generate a mask/size for each channel's LUT. This may be larger than the block size.
    // If a given 'source' bit (eg. 'x0') is used for any part of the equation, fill that in the mask.
    for (UINT_32 i = 0; i < ADDR_MAX_EQUATION_BIT; i++)
    {
        m_xLutMask |= m_bit[i].x;
        m_yLutMask |= m_bit[i].y;
        m_zLutMask |= m_bit[i].z;
        m_sLutMask |= m_bit[i].s;
    }

    // Derive the microblock size from the swizzle equation.
    UINT_32 xMbMask = 0;
    UINT_32 yMbMask = 0;
    UINT_32 zMbMask = 0;
    for (UINT_32 i = 0; i < 8; i++)
    {
        xMbMask |= m_bit[i].x;
        yMbMask |= m_bit[i].y;
        zMbMask |= m_bit[i].z;
    }
    m_microBlockSize.width  = xMbMask + 1;
    m_microBlockSize.height = yMbMask + 1;
    m_microBlockSize.depth  = zMbMask + 1;
    ADDR_ASSERT(IsPow2(m_microBlockSize.width));
    ADDR_ASSERT(IsPow2(m_microBlockSize.height));
    ADDR_ASSERT(IsPow2(m_microBlockSize.depth));

    // An expandX of 1 is a no-op
    m_maxExpandX = 1;
    if (m_sLutMask == 0)
    {
        // Calculate expandX from the swizzle. This can be derived from the number of consecutive,
        // increasing low x bits
        for (UINT_32 i = 0; i < 3; i++)
        {
            const auto& curBit = m_bit[m_bpeLog2 + i];
            ADDR_ASSERT(curBit.value != 0);
            if ((IsPow2(curBit.value) == false) || // More than one bit contributes
                (curBit.x == 0)                 || // Bit is from Y/Z/S channel
                (curBit.x != m_maxExpandX))        // X bits are out of order
            {
                break;
            }
            m_maxExpandX *= 2;
        }
    }
}

/**
****************************************************************************************************
*   LutAddresser::InitLuts
*
*   @brief
*       Creates lookup tables for each channel.
****************************************************************************************************
*/
void LutAddresser::InitLuts()
{
    UINT_32 curOffset = 0;
    m_pXLut = &m_lutData[0];
    for (UINT_32 x = 0; x < (m_xLutMask + 1); x++)
    {
        m_pXLut[x] = EvalEquation(x, 0, 0, 0);
    }
    curOffset += m_xLutMask + 1;
    ADDR_ASSERT(curOffset <= MaxLutSize);

    if (m_yLutMask != 0)
    {
        m_pYLut = &m_lutData[curOffset];
        for (UINT_32 y = 0; y < (m_yLutMask + 1); y++)
        {
            m_pYLut[y] = EvalEquation(0, y, 0, 0);
        }
        curOffset += m_yLutMask + 1;
        ADDR_ASSERT(curOffset <= MaxLutSize);
    }
    else
    {
        m_pYLut = &m_lutData[0];
        ADDR_ASSERT(m_pYLut[0] == 0);
    }

    if (m_zLutMask != 0)
    {
        m_pZLut = &m_lutData[curOffset];
        for (UINT_32 z = 0; z < (m_zLutMask + 1); z++)
        {
            m_pZLut[z] = EvalEquation(0, 0, z, 0);
        }
        curOffset += m_zLutMask + 1;
        ADDR_ASSERT(curOffset <= MaxLutSize);
    }
    else
    {
        m_pZLut = &m_lutData[0];
        ADDR_ASSERT(m_pZLut[0] == 0);
    }

    if (m_sLutMask != 0)
    {
        m_pSLut = &m_lutData[curOffset];
        for (UINT_32 s = 0; s < (m_sLutMask + 1); s++)
        {
            m_pSLut[s] = EvalEquation(0, 0, 0, s);
        }
        curOffset += m_sLutMask + 1;
        ADDR_ASSERT(curOffset <= MaxLutSize);
    }
    else
    {
        m_pSLut = &m_lutData[0];
        ADDR_ASSERT(m_pSLut[0] == 0);
    }
}

/**
****************************************************************************************************
*   LutAddresser::EvalEquation
*
*   @brief
*       Evaluates the equation at a given coordinate manually.
****************************************************************************************************
*/
UINT_32 LutAddresser::EvalEquation(
    UINT_32 x,
    UINT_32 y,
    UINT_32 z,
    UINT_32 s)
{
    UINT_32 out = 0;

    for (UINT_32 i = 0; i < ADDR_MAX_EQUATION_BIT; i++)
    {
        if (m_bit[i].value == 0)
        {
            if (out != 0)
            {
                // Invalid bits at the top of the equation
                break;
            }
            else
            {
                continue;
            }
        }

        if (x != 0)
        {
            UINT_32 xSrcs = m_bit[i].x;
            while (xSrcs != 0)
            {
                UINT_32 xIdx = BitMaskScanForward(xSrcs);
                out ^= (((x >> xIdx) & 1) << i);
                xSrcs = UnsetLeastBit(xSrcs);
            }
        }

        if (y != 0)
        {
            UINT_32 ySrcs = m_bit[i].y;
            while (ySrcs != 0)
            {
                UINT_32 yIdx = BitMaskScanForward(ySrcs);
                out ^= (((y >> yIdx) & 1) << i);
                ySrcs = UnsetLeastBit(ySrcs);
            }
        }

        if (z != 0)
        {
            UINT_32 zSrcs = m_bit[i].z;
            while (zSrcs != 0)
            {
                UINT_32 zIdx = BitMaskScanForward(zSrcs);
                out ^= (((z >> zIdx) & 1) << i);
                zSrcs = UnsetLeastBit(zSrcs);
            }
        }

        if (s != 0)
        {
            UINT_32 sSrcs = m_bit[i].s;
            while (sSrcs != 0)
            {
                UINT_32 sIdx = BitMaskScanForward(sSrcs);
                out ^= (((s >> sIdx) & 1) << i);
                sSrcs = UnsetLeastBit(sSrcs);
            }
        }
    }

    return out;
}


/**
****************************************************************************************************
*   CopyRowUnaligned
*
*   @brief
*       Copies a single row to or from a surface.
****************************************************************************************************
*/
template <int BPELog2, int ExpandX, bool ImgIsDest>
void CopyRowUnaligned(
    void*               pRowImgBlockStart, // Pointer to the image block at x=0
    void*               pBuf,              // Pointer to data at x=0
    UINT_32             xStart,            // x value to start at
    UINT_32             xEnd,              // x value to finish at (not inclusive)
    UINT_32             rowXor,            // Value to XOR in for each address (makes up PBX and y/z coords)
    const LutAddresser& addresser)
{
    UINT_32 x = xStart;
    constexpr UINT_32  PixBytes = (1 << BPELog2);

    // Most swizzles pack 2-4 pixels horizontally. Take advantage of this even in non-microblock-aligned
    // regions to commonly do 2-4x less work. This is still way less good than copying by whole microblocks though.
    if (ExpandX > 1)
    {
        // Unaligned left edge
        for (; x < Min(xEnd, PowTwoAlign(xStart, ExpandX)); x++)
        {
            UINT_32 blk = (x >> addresser.GetBlockXBits());
            void* pImgBlock = VoidPtrInc(pRowImgBlockStart, blk << addresser.GetBlockBits());
            void* pPix = VoidPtrInc(pImgBlock, rowXor ^ addresser.GetAddressX(x));
            if (ImgIsDest)
            {
                memcpy(pPix, VoidPtrInc(pBuf, x * PixBytes), PixBytes);
            }
            else
            {
                memcpy(VoidPtrInc(pBuf, x * PixBytes), pPix, PixBytes);
            }
        }
        // Aligned middle
        for (; x < PowTwoAlignDown(xEnd, ExpandX); x += ExpandX)
        {
            UINT_32 blk = (x >> addresser.GetBlockXBits());
            void* pImgBlock = VoidPtrInc(pRowImgBlockStart, blk << addresser.GetBlockBits());
            void* pPix = VoidPtrInc(pImgBlock, rowXor ^ addresser.GetAddressX(x));
            if (ImgIsDest)
            {
                memcpy(pPix, VoidPtrInc(pBuf, x * PixBytes), PixBytes * ExpandX);
            }
            else
            {
                memcpy(VoidPtrInc(pBuf, x * PixBytes), pPix, PixBytes * ExpandX);
            }
        }
    }
    // Unaligned end (or the whole thing when ExpandX == 1)
    for (; x < xEnd; x++)
    {
        // Get the index of the block within the slice
        UINT_32 blk = (x >> addresser.GetBlockXBits());
        // Apply that index to get the base address of the current block.
        void* pImgBlock = VoidPtrInc(pRowImgBlockStart, blk << addresser.GetBlockBits());
        // Grab the x-xor and XOR it all together, adding to get the final address
        void* pPix = VoidPtrInc(pImgBlock, rowXor ^ addresser.GetAddressX(x));
        if (ImgIsDest)
        {
            memcpy(pPix, VoidPtrInc(pBuf, x * PixBytes), PixBytes);
        }
        else
        {
            memcpy(VoidPtrInc(pBuf, x * PixBytes), pPix, PixBytes);
        }
    }
}

/**
****************************************************************************************************
*   CopyImgUnaligned
*
*   @brief
*       Copies an arbitrary 3D pixel region to or from a surface.
****************************************************************************************************
*/
template <int BPELog2, int ExpandX, bool ImgIsDest>
void CopyImgUnaligned(
    void*               pImgBlockStart, // Block corresponding to beginning of image
    void*               pBuf,           // Pointer to data starting from the copy origin.
    size_t              bufStrideY,     // Stride of each row in pBuf
    size_t              bufStrideZ,     // Stride of each slice in pBuf
    UINT_32             imageBlocksY,   // Width of the image slice, in blocks.
    UINT_32             imageBlocksZ,   // Depth pitch of the image slice, in blocks.
    ADDR_COORD3D        origin,         // Absolute origin, in elements
    ADDR_EXTENT3D       extent,         // Size to copy, in elements
    UINT_32             pipeBankXor,    // Final value to xor in
    BOOL_32             isInMipTail,    // True if this is in the mip tail.
    const LutAddresser& addresser)
{
    constexpr UINT_32  PixBytes = (1 << BPELog2);

    // Apply a negative x/y offset now so later code can do eg. pBuf[x] instead of pBuf[x - origin.x]
    // Keep the z offset.
    pBuf = VoidPtrDec(pBuf, origin.x * PixBytes);

    void* pSliceBuf = pBuf;
    // Do things one slice/row at a time for unaligned regions.
    for (UINT_32 z = origin.z; z < (origin.z + extent.depth); z++)
    {
        UINT_32 sliceXor = pipeBankXor ^ addresser.GetAddressZ(z);
        UINT_32 zBlk = (z >> addresser.GetBlockZBits()) * imageBlocksZ;
        void* pRowBuf = pSliceBuf;
        for (UINT_32 y = origin.y; y < (origin.y + extent.height); y++)
        {
            UINT_32 yBlk = (y >> addresser.GetBlockYBits()) * imageBlocksY;
            UINT_32 rowXor = sliceXor ^ addresser.GetAddressY(y);
            UINT_64 rowOffset = ((zBlk + yBlk) << addresser.GetBlockBits());

            void* pImgBlockRow = VoidPtrInc(pImgBlockStart, rowOffset);

            CopyRowUnaligned<BPELog2, ExpandX, ImgIsDest>(
                pImgBlockRow,
                pRowBuf,
                origin.x,
                origin.x + extent.width,
                rowXor,
                addresser);

            pRowBuf = VoidPtrInc(pRowBuf, bufStrideY);
        }
        pSliceBuf = VoidPtrInc(pSliceBuf, bufStrideZ);
    }
}


/**
****************************************************************************************************
*   HandleUnalignedRegions
*
*   @brief
*       Does unaligned copies for any X/Y/Z edges that are not fully aligned, fixing up the
*       copy region and pointer to point at the aligned region that remains.
****************************************************************************************************
*/
template <int BPELog2, int ExpandX>
void HandleUnalignedRegions(
    void*               pImgBlockStart, // Block corresponding to beginning of image
    void**              ppBuf,          // Pointer to pointer to data starting from the copy origin.
    size_t              bufStrideY,     // Stride of each row in pBuf
    size_t              bufStrideZ,     // Stride of each slice in pBuf
    UINT_32             imageBlocksY,   // Width of the image slice, in blocks.
    UINT_32             imageBlocksZ,   // Depth pitch of the image slice, in blocks.
    ADDR_COORD3D*       pOrigin,        // Absolute origin, in elements
    ADDR_EXTENT3D*      pExtent,        // Size to copy, in elements
    ADDR_EXTENT3D       align,          // Size to align on, in elements
    UINT_32             pipeBankXor,    // Final value to xor in
    BOOL_32             isInMipTail,    // True if this is in the mip tail.
    const LutAddresser& addresser)
{
    constexpr bool ImgIsDest = true;

    // Go through the start/end of the x/y/z extents and copy the parts that aren't aligned.
    if (pOrigin->x != PowTwoAlign(pOrigin->x, align.width))
    {
        UINT_32 xSize = Min(pOrigin->x + pExtent->width, PowTwoAlign(pOrigin->x, align.width)) - pOrigin->x;
        CopyImgUnaligned<BPELog2, ExpandX, ImgIsDest>(
            pImgBlockStart,
            *ppBuf,
            bufStrideY,
            bufStrideZ,
            imageBlocksY,
            imageBlocksZ,
            *pOrigin,
            { xSize, pExtent->height, pExtent->depth},
            pipeBankXor,
            isInMipTail,
            addresser);
        pExtent->width -= xSize;
        pOrigin->x     += xSize;
        *ppBuf = VoidPtrInc(*ppBuf, xSize << BPELog2);
    }
    if (pOrigin->y != PowTwoAlign(pOrigin->y, align.height))
    {
        UINT_32 ySize = Min(pOrigin->y + pExtent->height, PowTwoAlign(pOrigin->y, align.height)) - pOrigin->y;
        CopyImgUnaligned<BPELog2, ExpandX, ImgIsDest>(
            pImgBlockStart,
            *ppBuf,
            bufStrideY,
            bufStrideZ,
            imageBlocksY,
            imageBlocksZ,
            *pOrigin,
            { pExtent->width, ySize, pExtent->depth},
            pipeBankXor,
            isInMipTail,
            addresser);
        pExtent->height -= ySize;
        pOrigin->y      += ySize;
        *ppBuf = VoidPtrInc(*ppBuf, ySize * bufStrideY);
    }
    if (pOrigin->z != PowTwoAlign(pOrigin->z, align.depth))
    {
        UINT_32 zSize = Min(pOrigin->z + pExtent->depth, PowTwoAlign(pOrigin->z, align.depth)) - pOrigin->z;
        CopyImgUnaligned<BPELog2, ExpandX, ImgIsDest>(
            pImgBlockStart,
            *ppBuf,
            bufStrideY,
            bufStrideZ,
            imageBlocksY,
            imageBlocksZ,
            *pOrigin,
            { pExtent->width, pExtent->height, zSize },
            pipeBankXor,
            isInMipTail,
            addresser);
        pExtent->depth -= zSize;
        pOrigin->z     += zSize;
        *ppBuf = VoidPtrInc(*ppBuf, zSize * bufStrideZ);
    }

    // At this point the starts are aligned, so we can care about just size rather than origin+size.
    if ((pExtent->width) != PowTwoAlignDown(pExtent->width, align.width))
    {
        UINT_32 xAlignedSize = PowTwoAlignDown(pOrigin->x + pExtent->width, align.width) - pOrigin->x;
        void* pBuf = VoidPtrInc(*ppBuf, xAlignedSize << BPELog2);
        CopyImgUnaligned<BPELog2, ExpandX, ImgIsDest>(
            pImgBlockStart,
            pBuf,
            bufStrideY,
            bufStrideZ,
            imageBlocksY,
            imageBlocksZ,
            { pOrigin->x + xAlignedSize, pOrigin->y, pOrigin->z},
            { pExtent->width - xAlignedSize, pExtent->height, pExtent->depth },
            pipeBankXor,
            isInMipTail,
            addresser);
        pExtent->width = xAlignedSize;
    }

    if ((pExtent->height) != PowTwoAlignDown(pExtent->height, align.height))
    {
        UINT_32 yAlignedSize = PowTwoAlignDown(pOrigin->y + pExtent->height, align.height) - pOrigin->y;
        void* pBuf = VoidPtrInc(*ppBuf, yAlignedSize * bufStrideY);
        CopyImgUnaligned<BPELog2, ExpandX, ImgIsDest>(
            pImgBlockStart,
            pBuf,
            bufStrideY,
            bufStrideZ,
            imageBlocksY,
            imageBlocksZ,
            { pOrigin->x, pOrigin->y + yAlignedSize, pOrigin->z},
            { pExtent->width, pExtent->height - yAlignedSize, pExtent->depth },
            pipeBankXor,
            isInMipTail,
            addresser);
        pExtent->height = yAlignedSize;
    }

    if ((pExtent->depth) != PowTwoAlignDown(pExtent->depth, align.depth))
    {
        UINT_32 zAlignedSize = PowTwoAlignDown(pOrigin->z + pExtent->depth, align.depth) - pOrigin->z;
        void* pBuf = VoidPtrInc(*ppBuf, zAlignedSize * bufStrideZ);
        CopyImgUnaligned<BPELog2, ExpandX, ImgIsDest>(
            pImgBlockStart,
            pBuf,
            bufStrideY,
            bufStrideZ,
            imageBlocksY,
            imageBlocksZ,
            { pOrigin->x, pOrigin->y, pOrigin->z + zAlignedSize },
            { pExtent->width, pExtent->height, pExtent->depth - zAlignedSize },
            pipeBankXor,
            isInMipTail,
            addresser);
        pExtent->depth = zAlignedSize;
    }
}

/**
****************************************************************************************************
*   CopyMemImgHybrid
*
*   @brief
*       Copies a 3D pixel region to a surface. Uses fast copies for fully covered microblocks.
****************************************************************************************************
*/
template <class MicroSw>
AVX2_FUNC NEON_FUNC void CopyMemImgHybrid(
    void*               pImgBlockStart, // Block corresponding to beginning of image
    void*               pBuf,           // Pointer to data starting from the copy origin.
    size_t              bufStrideY,     // Stride of each row in pBuf
    size_t              bufStrideZ,     // Stride of each slice in pBuf
    UINT_32             imageBlocksY,   // Width of the image slice, in blocks.
    UINT_32             imageBlocksZ,   // Depth pitch of the image slice, in blocks.
    ADDR_COORD3D        origin,         // Absolute origin, in elements
    ADDR_EXTENT3D       extent,         // Size to copy, in elements
    UINT_32             pipeBankXor,    // Final value to xor in
    BOOL_32             isInMipTail,    // True if this is in the mip tail.
    const LutAddresser& addresser)
{
    // Handle unaligned edges in x/y/z and fixup the extents to match.
    HandleUnalignedRegions<MicroSw::BpeLog2, MicroSw::ExpandX>(
        pImgBlockStart,
        &pBuf,
        bufStrideY,
        bufStrideZ,
        imageBlocksY,
        imageBlocksZ,
        &origin,
        &extent,
        MicroSw::MicroBlockExtent,
        pipeBankXor,
        isInMipTail,
        addresser
    );

    // Apply a negative x/y offset now so later code can do eg. pBuf[x] instead of pBuf[x - origin.x]
    // Keep the z offset.
    pBuf = VoidPtrDec(pBuf, origin.x << MicroSw::BpeLog2);

    void* pSliceBuf = pBuf;
    // Do things one slice/row at a time for unaligned regions.
    for (UINT_32 z = origin.z; z < (origin.z + extent.depth); z += MicroSw::MicroBlockExtent.depth)
    {
        UINT_32 sliceXor = pipeBankXor ^ addresser.GetAddressZ(z);
        UINT_32 zBlk = (z >> addresser.GetBlockZBits()) * imageBlocksZ;
        void* pRowBuf = pSliceBuf;
        for (UINT_32 y = origin.y; y < (origin.y + extent.height); y += MicroSw::MicroBlockExtent.height)
        {
            UINT_32 yBlk = ((y >> addresser.GetBlockYBits()) * imageBlocksY) + zBlk;
            UINT_32 rowXor = sliceXor ^ addresser.GetAddressY(y);

            for (UINT_32 x = origin.x; x < (origin.x + extent.width); x += MicroSw::MicroBlockExtent.width)
            {
                UINT_32 xBlk = (x >> addresser.GetBlockXBits()) + yBlk;
                UINT_64 offset = (xBlk << addresser.GetBlockBits());
                offset ^= rowXor;
                offset ^= addresser.GetAddressX(x);

                void* pPix = VoidPtrInc(pImgBlockStart, offset);
                void* pPixBuf = VoidPtrInc(pRowBuf, x << MicroSw::BpeLog2);

                MicroSw::CopyMicroBlock(
                    pPix,
                    pPixBuf,
                    bufStrideY,
                    bufStrideZ
                );
            }
            pRowBuf = VoidPtrInc(pRowBuf, bufStrideY * MicroSw::MicroBlockExtent.height);
        }
        pSliceBuf = VoidPtrInc(pSliceBuf, bufStrideZ * MicroSw::MicroBlockExtent.depth);
    }
}

/**
****************************************************************************************************
*   CopyMemImgMicroblocks
*
*   @brief
*       Copies the microblocks of a 3D pixel region to/from a surface.
****************************************************************************************************
*/
template <bool ImgIsDest, bool NonTemporal>
AVX2_FUNC NEON_FUNC void CopyMemImgMicroblocks(
    void*               pImgBlockStart, // Block corresponding to beginning of image
    void*               pBuf,           // Pointer to data starting from the copy origin.
    size_t              bufStrideY,     // Stride of each row in pBuf, ignored.
    size_t              bufStrideZ,     // Stride of each slice in pBuf, ignored.
    UINT_32             imageBlocksY,   // Width of the image slice, in blocks.
    UINT_32             imageBlocksZ,   // Depth pitch of the image slice, in blocks.
    ADDR_COORD3D        origin,         // Absolute origin, in elements
    ADDR_EXTENT3D       extent,         // Size to copy, in elements
    UINT_32             pipeBankXor,    // Final value to xor in
    BOOL_32             isInMipTail,    // True if this is in the mip tail.
    const LutAddresser& addresser)
{
    // Pad out our dims to microblock boundaries.
    origin.x = PowTwoAlignDown(origin.x, addresser.GetMicroBlockX());
    origin.y = PowTwoAlignDown(origin.y, addresser.GetMicroBlockY());
    origin.z = PowTwoAlignDown(origin.z, addresser.GetMicroBlockZ());
    extent.width  = PowTwoAlign(extent.width,  addresser.GetMicroBlockX());
    extent.height = PowTwoAlign(extent.height, addresser.GetMicroBlockY());
    extent.depth  = PowTwoAlign(extent.depth,  addresser.GetMicroBlockZ());

    // Calculate the address of the first pixel in each microblock (256B), then copy it.
    for (UINT_32 z = origin.z; z < (origin.z + extent.depth); z += addresser.GetMicroBlockZ())
    {
        UINT_32 sliceXor = pipeBankXor ^ addresser.GetAddressZ(z);
        UINT_32 zBlk = (z >> addresser.GetBlockZBits()) * imageBlocksZ;
        for (UINT_32 y = origin.y; y < (origin.y + extent.height); y += addresser.GetMicroBlockY())
        {
            UINT_32 yBlk = ((y >> addresser.GetBlockYBits()) * imageBlocksY) + zBlk;
            UINT_32 rowXor = sliceXor ^ addresser.GetAddressY(y);

            for (UINT_32 x = origin.x; x < (origin.x + extent.width); x += addresser.GetMicroBlockX())
            {
                UINT_32 xBlk = (x >> addresser.GetBlockXBits()) + yBlk;
                UINT_64 offset = (xBlk << addresser.GetBlockBits());
                offset ^= rowXor;
                offset ^= addresser.GetAddressX(x);

                void* pPix = VoidPtrInc(pImgBlockStart, offset);
                constexpr UINT_32 CopySize = 1 << 8;

#if ADDR_HAS_AVX2
                if (NonTemporal && ImgIsDest)
                {
                    StreamCopyToImgAligned(pPix, pBuf, CopySize);
                }
                else if (NonTemporal)
                {
                    StreamCopyFromImgAligned(pBuf, pPix, CopySize);
                }
                else
#endif
                if (ImgIsDest)
                {
                    memcpy(pPix, pBuf, CopySize);
                }
                else
                {
                    memcpy(pBuf, pPix, CopySize);
                }
                pBuf = VoidPtrInc(pBuf, CopySize);
            }
        }
    }
}

/**
****************************************************************************************************
*   CopyMemImgBlocks
*
*   @brief
*       Copies the blocks of a 3D pixel region to/from a surface.
****************************************************************************************************
*/
template <bool ImgIsDest, bool NonTemporal>
AVX2_FUNC NEON_FUNC void CopyMemImgBlocks(
    void*               pImgBlockStart, // Block corresponding to beginning of image
    void*               pBuf,           // Pointer to data starting from the copy origin.
    size_t              bufStrideY,     // Stride of each row in pBuf, ignored.
    size_t              bufStrideZ,     // Stride of each slice in pBuf, ignored.
    UINT_32             imageBlocksY,   // Width of the image slice, in blocks.
    UINT_32             imageBlocksZ,   // Depth pitch of the image slice, in blocks.
    ADDR_COORD3D        origin,         // Absolute origin, in elements
    ADDR_EXTENT3D       extent,         // Size to copy, in elements
    UINT_32             pipeBankXor,    // Final value to xor in
    BOOL_32             isInMipTail,    // True if this is in the mip tail.
    const LutAddresser& addresser)
{
    if (isInMipTail)
    {
        return CopyMemImgMicroblocks<ImgIsDest, NonTemporal>(
            pImgBlockStart,
            pBuf,
            bufStrideY,
            bufStrideZ,
            imageBlocksY,
            imageBlocksZ,
            origin,
            extent,
            pipeBankXor,
            isInMipTail,
            addresser
        );
    }

    // Pad out our dims to block boundaries.
    origin.x = PowTwoAlignDown(origin.x, addresser.GetBlockX());
    origin.y = PowTwoAlignDown(origin.y, addresser.GetBlockY());
    origin.z = PowTwoAlignDown(origin.z, addresser.GetBlockZ());
    extent.width  = PowTwoAlign(extent.width,  addresser.GetBlockX());
    extent.height = PowTwoAlign(extent.height, addresser.GetBlockY());
    extent.depth  = PowTwoAlign(extent.depth,  addresser.GetBlockZ());

    // Copy block by block. No complex swizzling here, everything is in (strided) typewriter order.
    for (UINT_32 z = origin.z; z < (origin.z + extent.depth); z += addresser.GetBlockZ())
    {
        UINT_32 zBlk = (z >> addresser.GetBlockZBits()) * imageBlocksZ;
        for (UINT_32 y = origin.y; y < (origin.y + extent.height); y += addresser.GetBlockY())
        {
            UINT_32 yBlk = ((y >> addresser.GetBlockYBits()) * imageBlocksY) + zBlk;
            UINT_32 xBlkStart = (origin.x >> addresser.GetBlockXBits()) + yBlk;
            UINT_32 numXBlk = extent.width >> addresser.GetBlockXBits();
            UINT_64 offset = (xBlkStart << addresser.GetBlockBits());

            void* pPix = VoidPtrInc(pImgBlockStart, offset);
            UINT_32 copySize = numXBlk << addresser.GetBlockBits();

#if ADDR_HAS_AVX2
            if (NonTemporal && ImgIsDest)
            {
                StreamCopyToImgAligned(pPix, pBuf, copySize);
            }
            else if (NonTemporal)
            {
                StreamCopyFromImgAligned(pBuf, pPix, copySize);
            }
            else
#endif
            if (ImgIsDest)
            {
                memcpy(pPix, pBuf, copySize);
            }
            else
            {
                memcpy(pBuf, pPix, copySize);
            }
            pBuf = VoidPtrInc(pBuf, copySize);
        }
    }
}

/**
****************************************************************************************************
*   LutAddresser::GetCopyMemImgFunc
*
*   @brief
*       Determines and returns which copy function to use for copying to images
****************************************************************************************************
*/
UnalignedCopyMemImgFunc LutAddresser::GetCopyMemImgFunc(
    ADDR_COPY_FLAGS flags
    ) const
{
    UnalignedCopyMemImgFunc pfnRet = nullptr;
    // This key encodes how the bottom 8 bits (256B) are formed, so we can match to the correct optimized
    // swizzle function (they are all swizzle-agnostic beyond those 256B).
    UINT_64 microSwKey = GetMicroSwKey(reinterpret_cast<const UINT_64*>(&m_bit[0]));

    if (flags.blockMemcpy)
    {
#if ADDR_HAS_AVX2
        if (CpuSupportsAvx2())
        {
            pfnRet = CopyMemImgBlocks<true, true>;
        }
        else
#endif
        {
            pfnRet = CopyMemImgBlocks<true, false>;
        }
    }

    if ((pfnRet == nullptr) && flags.hybridMemcpy)
    {
#if ADDR_HAS_AVX2
        if (CpuSupportsAvx2())
        {
            pfnRet = CopyMemImgMicroblocks<true, true>;
        }
        else
#endif
        {
            pfnRet = CopyMemImgMicroblocks<true, false>;
        }
    }

    // If this is one of the known microswizzles and CPU support is present, use a hybrid copy that does
    // SIMD swizzling for aligned regions and falls back for unaligned edges.
#if ADDR_HAS_AVX2
    static constexpr struct {
        UINT_64                 microSwKey;
        UnalignedCopyMemImgFunc pfn;
    } AvxFuncs[] = {
        { GetMicroSwKey(MicroSw_2D_1BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_2D_1BPE_AVX2>},
        { GetMicroSwKey(MicroSw_2D_2BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_2D_2BPE_AVX2>},
        { GetMicroSwKey(MicroSw_2D_4BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_2D_4BPE_AVX2>},
        { GetMicroSwKey(MicroSw_2D_8BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_2D_8BPE_AVX2>},
        { GetMicroSwKey(MicroSw_2D_16BPE_AVX2::MicroEq), CopyMemImgHybrid<MicroSw_2D_16BPE_AVX2>},
        { GetMicroSwKey(MicroSw_3D_1BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_3D_1BPE_AVX2>},
        { GetMicroSwKey(MicroSw_3D_2BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_3D_2BPE_AVX2>},
        { GetMicroSwKey(MicroSw_3D_4BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_3D_4BPE_AVX2>},
        { GetMicroSwKey(MicroSw_3D_8BPE_AVX2::MicroEq),  CopyMemImgHybrid<MicroSw_3D_8BPE_AVX2>},
        { GetMicroSwKey(MicroSw_3D_16BPE_AVX2::MicroEq), CopyMemImgHybrid<MicroSw_3D_16BPE_AVX2>},
        { GetMicroSwKey(MicroSw_R_1BPE_AVX2::MicroEq),   CopyMemImgHybrid<MicroSw_R_1BPE_AVX2>},
        { GetMicroSwKey(MicroSw_R_2BPE_AVX2::MicroEq),   CopyMemImgHybrid<MicroSw_R_2BPE_AVX2>},
        { GetMicroSwKey(MicroSw_R_4BPE_AVX2::MicroEq),   CopyMemImgHybrid<MicroSw_R_4BPE_AVX2>},
        { GetMicroSwKey(MicroSw_Z_1BPE_AVX2::MicroEq),   CopyMemImgHybrid<MicroSw_Z_1BPE_AVX2>},
        { GetMicroSwKey(MicroSw_D_1BPE_AVX2::MicroEq),   CopyMemImgHybrid<MicroSw_D_1BPE_AVX2>}
    };
    if ((pfnRet == nullptr) && CpuSupportsAvx2())
    {
        for (const auto& func : AvxFuncs)
        {
            if (func.microSwKey == microSwKey)
            {
                pfnRet = func.pfn;
                break;
            }
        }
    }
#endif // ADDR_HAS_AVX2

#if ADDR_HAS_NEON
    static constexpr struct {
        UINT_64                 microSwKey;
        UnalignedCopyMemImgFunc pfn;
    } NeonFuncs[] = {
        { GetMicroSwKey(MicroSw_2D_1BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_2D_1BPE_NEON>},
        { GetMicroSwKey(MicroSw_2D_2BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_2D_2BPE_NEON>},
        { GetMicroSwKey(MicroSw_2D_4BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_2D_4BPE_NEON>},
        { GetMicroSwKey(MicroSw_2D_8BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_2D_8BPE_NEON>},
        { GetMicroSwKey(MicroSw_2D_16BPE_NEON::MicroEq), CopyMemImgHybrid<MicroSw_2D_16BPE_NEON>},
        { GetMicroSwKey(MicroSw_3D_1BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_3D_1BPE_NEON>},
        { GetMicroSwKey(MicroSw_3D_2BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_3D_2BPE_NEON>},
        { GetMicroSwKey(MicroSw_3D_4BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_3D_4BPE_NEON>},
        { GetMicroSwKey(MicroSw_3D_8BPE_NEON::MicroEq),  CopyMemImgHybrid<MicroSw_3D_8BPE_NEON>},
        { GetMicroSwKey(MicroSw_3D_16BPE_NEON::MicroEq), CopyMemImgHybrid<MicroSw_3D_16BPE_NEON>},
        { GetMicroSwKey(MicroSw_R_1BPE_NEON::MicroEq),   CopyMemImgHybrid<MicroSw_R_1BPE_NEON>},
        { GetMicroSwKey(MicroSw_R_2BPE_NEON::MicroEq),   CopyMemImgHybrid<MicroSw_R_2BPE_NEON>},
        { GetMicroSwKey(MicroSw_R_4BPE_NEON::MicroEq),   CopyMemImgHybrid<MicroSw_R_4BPE_NEON>},
        { GetMicroSwKey(MicroSw_Z_1BPE_NEON::MicroEq),   CopyMemImgHybrid<MicroSw_Z_1BPE_NEON>},
        { GetMicroSwKey(MicroSw_D_1BPE_NEON::MicroEq),   CopyMemImgHybrid<MicroSw_D_1BPE_NEON>}
    };
    if ((pfnRet == nullptr) && CpuSupportsNeon())
    {
        for (const auto& func : NeonFuncs)
        {
            if (func.microSwKey == microSwKey)
            {
                pfnRet = func.pfn;
                break;
            }
        }
    }
#endif // ADDR_HAS_NEON

    // While these are all the same function, the codegen gets really bad if the size of each pixel
    // is not known at compile time. Hence, templates.
    const UnalignedCopyMemImgFunc Funcs[MaxElementBytesLog2][3] =
    {
        // ExpandX =  1, 2, 4
        { CopyImgUnaligned<0, 1, true>, CopyImgUnaligned<0, 2, true>, CopyImgUnaligned<0, 4, true> }, // 1BPE
        { CopyImgUnaligned<1, 1, true>, CopyImgUnaligned<1, 2, true>, CopyImgUnaligned<1, 4, true> }, // 2BPE
        { CopyImgUnaligned<2, 1, true>, CopyImgUnaligned<2, 2, true>, CopyImgUnaligned<2, 4, true> }, // 4BPE
        { CopyImgUnaligned<3, 1, true>, CopyImgUnaligned<3, 2, true>, CopyImgUnaligned<3, 4, true> }, // 8BPE
        { CopyImgUnaligned<4, 1, true>, CopyImgUnaligned<4, 2, true>, CopyImgUnaligned<4, 4, true> }, // 16BPE
    };

    // Fallback functions
    if (pfnRet == nullptr)
    {
        ADDR_ASSERT(m_bpeLog2 < MaxElementBytesLog2);
        pfnRet = Funcs[m_bpeLog2][Min(2U, Log2(m_maxExpandX))];
    }
    return pfnRet;
}

/**
****************************************************************************************************
*   LutAddresser::GetCopyImgMemFunc
*
*   @brief
*       Determines and returns which copy function to use for copying from images
****************************************************************************************************
*/
UnalignedCopyMemImgFunc LutAddresser::GetCopyImgMemFunc(
    ADDR_COPY_FLAGS flags
    ) const
{
    UnalignedCopyMemImgFunc pfnRet = nullptr;
    if (flags.blockMemcpy)
    {
#if ADDR_HAS_AVX2
        if (CpuSupportsAvx2())
        {
            pfnRet = CopyMemImgBlocks<false, true>;
        }
        else
#endif
        {
            pfnRet = CopyMemImgBlocks<false, false>;
        }
    }

    if ((pfnRet == nullptr) && flags.hybridMemcpy)
    {
#if ADDR_HAS_AVX2
        if (CpuSupportsAvx2())
        {
            pfnRet = CopyMemImgMicroblocks<false, true>;
        }
        else
#endif
        {
            pfnRet = CopyMemImgMicroblocks<false, false>;
        }
    }
    // While these are all the same function, the codegen gets really bad if the size of each pixel
    // is not known at compile time. Hence, templates.
    const UnalignedCopyMemImgFunc Funcs[MaxElementBytesLog2][3] =
    {
        // ExpandX =  1, 2, 4
        { CopyImgUnaligned<0, 1, false>, CopyImgUnaligned<0, 2, false>, CopyImgUnaligned<0, 4, false> }, // 1BPE
        { CopyImgUnaligned<1, 1, false>, CopyImgUnaligned<1, 2, false>, CopyImgUnaligned<1, 4, false> }, // 2BPE
        { CopyImgUnaligned<2, 1, false>, CopyImgUnaligned<2, 2, false>, CopyImgUnaligned<2, 4, false> }, // 4BPE
        { CopyImgUnaligned<3, 1, false>, CopyImgUnaligned<3, 2, false>, CopyImgUnaligned<3, 4, false> }, // 8BPE
        { CopyImgUnaligned<4, 1, false>, CopyImgUnaligned<4, 2, false>, CopyImgUnaligned<4, 4, false> }, // 16BPE
    };

    ADDR_ASSERT(m_bpeLog2 < MaxElementBytesLog2);
    if (pfnRet == nullptr)
    {
        pfnRet = Funcs[m_bpeLog2][Min(2U, Log2(m_maxExpandX))];
    }
    return pfnRet;
}

/**
****************************************************************************************************
*   LutAddresser::DoCopyImgMemPreFlushes
*
*   @brief
*       Does any flushes required for nontemporal SIMD instructions to access the image memory.
****************************************************************************************************
*/
void LutAddresser::DoCopyImgMemPreFlushes(
    ADDR_COPY_FLAGS flags
    ) const
{
#if ADDR_HAS_AVX2
    if ((flags.blockMemcpy || flags.hybridMemcpy) && CpuSupportsAvx2())
    {
        // Loads are weakly ordered, and we need to ensure they start after the previous copy
        NonTemporalLoadStoreFence();
    }
#endif
}

/**
****************************************************************************************************
*   LutAddresser::DoCopyMemImgPostFlushes
*
*   @brief
*       Does any flushes required for nontemporal SIMD instructions to access the image memory.
****************************************************************************************************
*/
void LutAddresser::DoCopyMemImgPostFlushes(
    ADDR_COPY_FLAGS flags
    ) const
{
#if ADDR_HAS_AVX2
    if (CpuSupportsAvx2())
    {
        // Stores are weakly ordered, and we need to ensure they finish before the next submission
        // or copy.
        NonTemporalStoreFence();
    }
#endif
}


#if __cplusplus < 201703L
// Constexpr arrays need an additional definition at namespace scope until c++17
#if ADDR_HAS_AVX2
constexpr ADDR_EXTENT3D MicroSw_2D_1BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_2BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_4BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_8BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_16BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_1BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_2BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_4BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_8BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_16BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_R_1BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_R_2BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_R_4BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_Z_1BPE_AVX2::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_D_1BPE_AVX2::MicroBlockExtent;
#endif
#if ADDR_HAS_NEON
constexpr ADDR_EXTENT3D MicroSw_2D_1BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_2BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_4BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_8BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_2D_16BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_1BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_2BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_4BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_8BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_3D_16BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_R_1BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_R_2BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_R_4BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_Z_1BPE_NEON::MicroBlockExtent;
constexpr ADDR_EXTENT3D MicroSw_D_1BPE_NEON::MicroBlockExtent;
#endif

#endif

}
