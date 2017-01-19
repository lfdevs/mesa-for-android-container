
/****************************************************************************
* Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
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
* @file state_llvm.h
* 
* @brief auto-generated file
* 
* DO NOT EDIT
* 
******************************************************************************/

#pragma once

namespace SwrJit
{
    using namespace llvm;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for simdvertex
    INLINE static StructType *Gen_simdvertex(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(ArrayType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 4), KNOB_NUM_ATTRIBUTES) );    // attrib

        return StructType::get(ctx, members, false);
    }

    static const uint32_t simdvertex_attrib = 0;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_VS_CONTEXT
    INLINE static StructType *Gen_SWR_VS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( PointerType::get(Gen_simdvertex(pJitMgr), 0) );    // pVin
        members.push_back( PointerType::get(Gen_simdvertex(pJitMgr), 0) );    // pVout
        members.push_back( Type::getInt32Ty(ctx) );    // InstanceID
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // VertexID
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // mask

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VS_CONTEXT_pVin = 0;
    static const uint32_t SWR_VS_CONTEXT_pVout = 1;
    static const uint32_t SWR_VS_CONTEXT_InstanceID = 2;
    static const uint32_t SWR_VS_CONTEXT_VertexID = 3;
    static const uint32_t SWR_VS_CONTEXT_mask = 4;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for ScalarAttrib
    INLINE static StructType *Gen_ScalarAttrib(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getFloatTy(ctx) );    // x
        members.push_back( Type::getFloatTy(ctx) );    // y
        members.push_back( Type::getFloatTy(ctx) );    // z
        members.push_back( Type::getFloatTy(ctx) );    // w

        return StructType::get(ctx, members, false);
    }

    static const uint32_t ScalarAttrib_x = 0;
    static const uint32_t ScalarAttrib_y = 1;
    static const uint32_t ScalarAttrib_z = 2;
    static const uint32_t ScalarAttrib_w = 3;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for ScalarCPoint
    INLINE static StructType *Gen_ScalarCPoint(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(Gen_ScalarAttrib(pJitMgr), KNOB_NUM_ATTRIBUTES) );    // attrib

        return StructType::get(ctx, members, false);
    }

    static const uint32_t ScalarCPoint_attrib = 0;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_TESSELLATION_FACTORS
    INLINE static StructType *Gen_SWR_TESSELLATION_FACTORS(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(Type::getFloatTy(ctx), SWR_NUM_OUTER_TESS_FACTORS) );    // OuterTessFactors
        members.push_back( ArrayType::get(Type::getFloatTy(ctx), SWR_NUM_INNER_TESS_FACTORS) );    // InnerTessFactors

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_TESSELLATION_FACTORS_OuterTessFactors = 0;
    static const uint32_t SWR_TESSELLATION_FACTORS_InnerTessFactors = 1;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for ScalarPatch
    INLINE static StructType *Gen_ScalarPatch(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Gen_SWR_TESSELLATION_FACTORS(pJitMgr) );    // tessFactors
        members.push_back( ArrayType::get(Gen_ScalarCPoint(pJitMgr), MAX_NUM_VERTS_PER_PRIM) );    // cp
        members.push_back( Gen_ScalarCPoint(pJitMgr) );    // patchData

        return StructType::get(ctx, members, false);
    }

    static const uint32_t ScalarPatch_tessFactors = 0;
    static const uint32_t ScalarPatch_cp = 1;
    static const uint32_t ScalarPatch_patchData = 2;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_HS_CONTEXT
    INLINE static StructType *Gen_SWR_HS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(Gen_simdvertex(pJitMgr), MAX_NUM_VERTS_PER_PRIM) );    // vert
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // PrimitiveID
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // mask
        members.push_back( PointerType::get(Gen_ScalarPatch(pJitMgr), 0) );    // pCPout

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_HS_CONTEXT_vert = 0;
    static const uint32_t SWR_HS_CONTEXT_PrimitiveID = 1;
    static const uint32_t SWR_HS_CONTEXT_mask = 2;
    static const uint32_t SWR_HS_CONTEXT_pCPout = 3;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_DS_CONTEXT
    INLINE static StructType *Gen_SWR_DS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // PrimitiveID
        members.push_back( Type::getInt32Ty(ctx) );    // vectorOffset
        members.push_back( Type::getInt32Ty(ctx) );    // vectorStride
        members.push_back( PointerType::get(Gen_ScalarPatch(pJitMgr), 0) );    // pCpIn
        members.push_back( PointerType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 0) );    // pDomainU
        members.push_back( PointerType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 0) );    // pDomainV
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // mask
        members.push_back( PointerType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 0) );    // pOutputData

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_DS_CONTEXT_PrimitiveID = 0;
    static const uint32_t SWR_DS_CONTEXT_vectorOffset = 1;
    static const uint32_t SWR_DS_CONTEXT_vectorStride = 2;
    static const uint32_t SWR_DS_CONTEXT_pCpIn = 3;
    static const uint32_t SWR_DS_CONTEXT_pDomainU = 4;
    static const uint32_t SWR_DS_CONTEXT_pDomainV = 5;
    static const uint32_t SWR_DS_CONTEXT_mask = 6;
    static const uint32_t SWR_DS_CONTEXT_pOutputData = 7;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_GS_CONTEXT
    INLINE static StructType *Gen_SWR_GS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(Gen_simdvertex(pJitMgr), MAX_NUM_VERTS_PER_PRIM) );    // vert
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // PrimitiveID
        members.push_back( Type::getInt32Ty(ctx) );    // InstanceID
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // mask
        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pStream
        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pCutOrStreamIdBuffer
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // vertexCount

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_GS_CONTEXT_vert = 0;
    static const uint32_t SWR_GS_CONTEXT_PrimitiveID = 1;
    static const uint32_t SWR_GS_CONTEXT_InstanceID = 2;
    static const uint32_t SWR_GS_CONTEXT_mask = 3;
    static const uint32_t SWR_GS_CONTEXT_pStream = 4;
    static const uint32_t SWR_GS_CONTEXT_pCutOrStreamIdBuffer = 5;
    static const uint32_t SWR_GS_CONTEXT_vertexCount = 6;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for PixelPositions
    INLINE static StructType *Gen_PixelPositions(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );    // UL
        members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );    // center
        members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );    // sample
        members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );    // centroid

        return StructType::get(ctx, members, false);
    }

    static const uint32_t PixelPositions_UL = 0;
    static const uint32_t PixelPositions_center = 1;
    static const uint32_t PixelPositions_sample = 2;
    static const uint32_t PixelPositions_centroid = 3;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_PS_CONTEXT
    INLINE static StructType *Gen_SWR_PS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Gen_PixelPositions(pJitMgr) );    // vX
        members.push_back( Gen_PixelPositions(pJitMgr) );    // vY
        members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );    // vZ
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // activeMask
        members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );    // inputMask
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // oMask
        members.push_back( Gen_PixelPositions(pJitMgr) );    // vI
        members.push_back( Gen_PixelPositions(pJitMgr) );    // vJ
        members.push_back( Gen_PixelPositions(pJitMgr) );    // vOneOverW
        members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );    // pAttribs
        members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );    // pPerspAttribs
        members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );    // pRecipW
        members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );    // I
        members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );    // J
        members.push_back( Type::getFloatTy(ctx) );    // recipDet
        members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );    // pSamplePosX
        members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );    // pSamplePosY
        members.push_back( ArrayType::get(ArrayType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 4), SWR_NUM_RENDERTARGETS) );    // shaded
        members.push_back( Type::getInt32Ty(ctx) );    // frontFace
        members.push_back( Type::getInt32Ty(ctx) );    // primID
        members.push_back( Type::getInt32Ty(ctx) );    // sampleIndex
        members.push_back( Type::getInt32Ty(ctx) );    // rasterizerSampleCount

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_PS_CONTEXT_vX = 0;
    static const uint32_t SWR_PS_CONTEXT_vY = 1;
    static const uint32_t SWR_PS_CONTEXT_vZ = 2;
    static const uint32_t SWR_PS_CONTEXT_activeMask = 3;
    static const uint32_t SWR_PS_CONTEXT_inputMask = 4;
    static const uint32_t SWR_PS_CONTEXT_oMask = 5;
    static const uint32_t SWR_PS_CONTEXT_vI = 6;
    static const uint32_t SWR_PS_CONTEXT_vJ = 7;
    static const uint32_t SWR_PS_CONTEXT_vOneOverW = 8;
    static const uint32_t SWR_PS_CONTEXT_pAttribs = 9;
    static const uint32_t SWR_PS_CONTEXT_pPerspAttribs = 10;
    static const uint32_t SWR_PS_CONTEXT_pRecipW = 11;
    static const uint32_t SWR_PS_CONTEXT_I = 12;
    static const uint32_t SWR_PS_CONTEXT_J = 13;
    static const uint32_t SWR_PS_CONTEXT_recipDet = 14;
    static const uint32_t SWR_PS_CONTEXT_pSamplePosX = 15;
    static const uint32_t SWR_PS_CONTEXT_pSamplePosY = 16;
    static const uint32_t SWR_PS_CONTEXT_shaded = 17;
    static const uint32_t SWR_PS_CONTEXT_frontFace = 18;
    static const uint32_t SWR_PS_CONTEXT_primID = 19;
    static const uint32_t SWR_PS_CONTEXT_sampleIndex = 20;
    static const uint32_t SWR_PS_CONTEXT_rasterizerSampleCount = 21;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_CS_CONTEXT
    INLINE static StructType *Gen_SWR_CS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // tileCounter
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), 3) );    // dispatchDims
        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pTGSM
        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pSpillFillBuffer

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_CS_CONTEXT_tileCounter = 0;
    static const uint32_t SWR_CS_CONTEXT_dispatchDims = 1;
    static const uint32_t SWR_CS_CONTEXT_pTGSM = 2;
    static const uint32_t SWR_CS_CONTEXT_pSpillFillBuffer = 3;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_SURFACE_STATE
    INLINE static StructType *Gen_SWR_SURFACE_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pBaseAddress
        members.push_back( Type::getInt32Ty(ctx) );    // type
        members.push_back( Type::getInt32Ty(ctx) );    // format
        members.push_back( Type::getInt32Ty(ctx) );    // width
        members.push_back( Type::getInt32Ty(ctx) );    // height
        members.push_back( Type::getInt32Ty(ctx) );    // depth
        members.push_back( Type::getInt32Ty(ctx) );    // numSamples
        members.push_back( Type::getInt32Ty(ctx) );    // samplePattern
        members.push_back( Type::getInt32Ty(ctx) );    // pitch
        members.push_back( Type::getInt32Ty(ctx) );    // qpitch
        members.push_back( Type::getInt32Ty(ctx) );    // minLod
        members.push_back( Type::getInt32Ty(ctx) );    // maxLod
        members.push_back( Type::getFloatTy(ctx) );    // resourceMinLod
        members.push_back( Type::getInt32Ty(ctx) );    // lod
        members.push_back( Type::getInt32Ty(ctx) );    // arrayIndex
        members.push_back( Type::getInt32Ty(ctx) );    // tileMode
        members.push_back( Type::getInt32Ty(ctx) );    // halign
        members.push_back( Type::getInt32Ty(ctx) );    // valign
        members.push_back( Type::getInt32Ty(ctx) );    // xOffset
        members.push_back( Type::getInt32Ty(ctx) );    // yOffset
        members.push_back( ArrayType::get(ArrayType::get(Type::getInt32Ty(ctx), 15), 2) );    // lodOffsets
        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pAuxBaseAddress
        members.push_back( Type::getInt32Ty(ctx) );    // auxMode
        members.push_back( Type::getInt8Ty(ctx) );    // bInterleavedSamples

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_SURFACE_STATE_pBaseAddress = 0;
    static const uint32_t SWR_SURFACE_STATE_type = 1;
    static const uint32_t SWR_SURFACE_STATE_format = 2;
    static const uint32_t SWR_SURFACE_STATE_width = 3;
    static const uint32_t SWR_SURFACE_STATE_height = 4;
    static const uint32_t SWR_SURFACE_STATE_depth = 5;
    static const uint32_t SWR_SURFACE_STATE_numSamples = 6;
    static const uint32_t SWR_SURFACE_STATE_samplePattern = 7;
    static const uint32_t SWR_SURFACE_STATE_pitch = 8;
    static const uint32_t SWR_SURFACE_STATE_qpitch = 9;
    static const uint32_t SWR_SURFACE_STATE_minLod = 10;
    static const uint32_t SWR_SURFACE_STATE_maxLod = 11;
    static const uint32_t SWR_SURFACE_STATE_resourceMinLod = 12;
    static const uint32_t SWR_SURFACE_STATE_lod = 13;
    static const uint32_t SWR_SURFACE_STATE_arrayIndex = 14;
    static const uint32_t SWR_SURFACE_STATE_tileMode = 15;
    static const uint32_t SWR_SURFACE_STATE_halign = 16;
    static const uint32_t SWR_SURFACE_STATE_valign = 17;
    static const uint32_t SWR_SURFACE_STATE_xOffset = 18;
    static const uint32_t SWR_SURFACE_STATE_yOffset = 19;
    static const uint32_t SWR_SURFACE_STATE_lodOffsets = 20;
    static const uint32_t SWR_SURFACE_STATE_pAuxBaseAddress = 21;
    static const uint32_t SWR_SURFACE_STATE_auxMode = 22;
    static const uint32_t SWR_SURFACE_STATE_bInterleavedSamples = 23;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_VERTEX_BUFFER_STATE
    INLINE static StructType *Gen_SWR_VERTEX_BUFFER_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // index
        members.push_back( Type::getInt32Ty(ctx) );    // pitch
        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pData
        members.push_back( Type::getInt32Ty(ctx) );    // size
        members.push_back( Type::getInt32Ty(ctx) );    // numaNode
        members.push_back( Type::getInt32Ty(ctx) );    // maxVertex
        members.push_back( Type::getInt32Ty(ctx) );    // partialInboundsSize

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VERTEX_BUFFER_STATE_index = 0;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_pitch = 1;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_pData = 2;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_size = 3;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_numaNode = 4;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_maxVertex = 5;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_partialInboundsSize = 6;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_INDEX_BUFFER_STATE
    INLINE static StructType *Gen_SWR_INDEX_BUFFER_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // format
        members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );    // pIndices
        members.push_back( Type::getInt32Ty(ctx) );    // size

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_INDEX_BUFFER_STATE_format = 0;
    static const uint32_t SWR_INDEX_BUFFER_STATE_pIndices = 1;
    static const uint32_t SWR_INDEX_BUFFER_STATE_size = 2;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_FETCH_CONTEXT
    INLINE static StructType *Gen_SWR_FETCH_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( PointerType::get(Gen_SWR_VERTEX_BUFFER_STATE(pJitMgr), 0) );    // pStreams
        members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );    // pIndices
        members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );    // pLastIndex
        members.push_back( Type::getInt32Ty(ctx) );    // CurInstance
        members.push_back( Type::getInt32Ty(ctx) );    // BaseVertex
        members.push_back( Type::getInt32Ty(ctx) );    // StartVertex
        members.push_back( Type::getInt32Ty(ctx) );    // StartInstance
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // VertexID
        members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );    // CutMask

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_FETCH_CONTEXT_pStreams = 0;
    static const uint32_t SWR_FETCH_CONTEXT_pIndices = 1;
    static const uint32_t SWR_FETCH_CONTEXT_pLastIndex = 2;
    static const uint32_t SWR_FETCH_CONTEXT_CurInstance = 3;
    static const uint32_t SWR_FETCH_CONTEXT_BaseVertex = 4;
    static const uint32_t SWR_FETCH_CONTEXT_StartVertex = 5;
    static const uint32_t SWR_FETCH_CONTEXT_StartInstance = 6;
    static const uint32_t SWR_FETCH_CONTEXT_VertexID = 7;
    static const uint32_t SWR_FETCH_CONTEXT_CutMask = 8;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_STREAMOUT_BUFFER
    INLINE static StructType *Gen_SWR_STREAMOUT_BUFFER(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt8Ty(ctx) );    // enable
        members.push_back( Type::getInt8Ty(ctx) );    // soWriteEnable
        members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );    // pBuffer
        members.push_back( Type::getInt32Ty(ctx) );    // bufferSize
        members.push_back( Type::getInt32Ty(ctx) );    // pitch
        members.push_back( Type::getInt32Ty(ctx) );    // streamOffset
        members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );    // pWriteOffset

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_STREAMOUT_BUFFER_enable = 0;
    static const uint32_t SWR_STREAMOUT_BUFFER_soWriteEnable = 1;
    static const uint32_t SWR_STREAMOUT_BUFFER_pBuffer = 2;
    static const uint32_t SWR_STREAMOUT_BUFFER_bufferSize = 3;
    static const uint32_t SWR_STREAMOUT_BUFFER_pitch = 4;
    static const uint32_t SWR_STREAMOUT_BUFFER_streamOffset = 5;
    static const uint32_t SWR_STREAMOUT_BUFFER_pWriteOffset = 6;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_STREAMOUT_STATE
    INLINE static StructType *Gen_SWR_STREAMOUT_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt8Ty(ctx) );    // soEnable
        members.push_back( ArrayType::get(Type::getInt8Ty(ctx), MAX_SO_STREAMS) );    // streamEnable
        members.push_back( Type::getInt8Ty(ctx) );    // rasterizerDisable
        members.push_back( Type::getInt32Ty(ctx) );    // streamToRasterizer
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), MAX_SO_STREAMS) );    // streamMasks
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), MAX_SO_STREAMS) );    // streamNumEntries

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_STREAMOUT_STATE_soEnable = 0;
    static const uint32_t SWR_STREAMOUT_STATE_streamEnable = 1;
    static const uint32_t SWR_STREAMOUT_STATE_rasterizerDisable = 2;
    static const uint32_t SWR_STREAMOUT_STATE_streamToRasterizer = 3;
    static const uint32_t SWR_STREAMOUT_STATE_streamMasks = 4;
    static const uint32_t SWR_STREAMOUT_STATE_streamNumEntries = 5;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_STREAMOUT_CONTEXT
    INLINE static StructType *Gen_SWR_STREAMOUT_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );    // pPrimData
        members.push_back( ArrayType::get(PointerType::get(Gen_SWR_STREAMOUT_BUFFER(pJitMgr), 0), MAX_SO_STREAMS) );    // pBuffer
        members.push_back( Type::getInt32Ty(ctx) );    // numPrimsWritten
        members.push_back( Type::getInt32Ty(ctx) );    // numPrimStorageNeeded

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_STREAMOUT_CONTEXT_pPrimData = 0;
    static const uint32_t SWR_STREAMOUT_CONTEXT_pBuffer = 1;
    static const uint32_t SWR_STREAMOUT_CONTEXT_numPrimsWritten = 2;
    static const uint32_t SWR_STREAMOUT_CONTEXT_numPrimStorageNeeded = 3;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_GS_STATE
    INLINE static StructType *Gen_SWR_GS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt8Ty(ctx) );    // gsEnable
        members.push_back( Type::getInt32Ty(ctx) );    // numInputAttribs
        members.push_back( Type::getInt32Ty(ctx) );    // outputTopology
        members.push_back( Type::getInt32Ty(ctx) );    // maxNumVerts
        members.push_back( Type::getInt32Ty(ctx) );    // instanceCount
        members.push_back( Type::getInt8Ty(ctx) );    // emitsRenderTargetArrayIndex
        members.push_back( Type::getInt8Ty(ctx) );    // emitsPrimitiveID
        members.push_back( Type::getInt8Ty(ctx) );    // emitsViewportArrayIndex
        members.push_back( Type::getInt8Ty(ctx) );    // isSingleStream
        members.push_back( Type::getInt32Ty(ctx) );    // singleStreamID

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_GS_STATE_gsEnable = 0;
    static const uint32_t SWR_GS_STATE_numInputAttribs = 1;
    static const uint32_t SWR_GS_STATE_outputTopology = 2;
    static const uint32_t SWR_GS_STATE_maxNumVerts = 3;
    static const uint32_t SWR_GS_STATE_instanceCount = 4;
    static const uint32_t SWR_GS_STATE_emitsRenderTargetArrayIndex = 5;
    static const uint32_t SWR_GS_STATE_emitsPrimitiveID = 6;
    static const uint32_t SWR_GS_STATE_emitsViewportArrayIndex = 7;
    static const uint32_t SWR_GS_STATE_isSingleStream = 8;
    static const uint32_t SWR_GS_STATE_singleStreamID = 9;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_TS_STATE
    INLINE static StructType *Gen_SWR_TS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt8Ty(ctx) );    // tsEnable
        members.push_back( Type::getInt32Ty(ctx) );    // tsOutputTopology
        members.push_back( Type::getInt32Ty(ctx) );    // partitioning
        members.push_back( Type::getInt32Ty(ctx) );    // domain
        members.push_back( Type::getInt32Ty(ctx) );    // postDSTopology
        members.push_back( Type::getInt32Ty(ctx) );    // numHsInputAttribs
        members.push_back( Type::getInt32Ty(ctx) );    // numHsOutputAttribs
        members.push_back( Type::getInt32Ty(ctx) );    // numDsOutputAttribs

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_TS_STATE_tsEnable = 0;
    static const uint32_t SWR_TS_STATE_tsOutputTopology = 1;
    static const uint32_t SWR_TS_STATE_partitioning = 2;
    static const uint32_t SWR_TS_STATE_domain = 3;
    static const uint32_t SWR_TS_STATE_postDSTopology = 4;
    static const uint32_t SWR_TS_STATE_numHsInputAttribs = 5;
    static const uint32_t SWR_TS_STATE_numHsOutputAttribs = 6;
    static const uint32_t SWR_TS_STATE_numDsOutputAttribs = 7;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_RENDER_TARGET_BLEND_STATE
    INLINE static StructType *Gen_SWR_RENDER_TARGET_BLEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt8Ty(ctx) );    // writeDisableRed
        members.push_back( Type::getInt8Ty(ctx) );    // writeDisableGreen
        members.push_back( Type::getInt8Ty(ctx) );    // writeDisableBlue
        members.push_back( Type::getInt8Ty(ctx) );    // writeDisableAlpha

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableRed = 0;
    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableGreen = 1;
    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableBlue = 2;
    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableAlpha = 3;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_BLEND_STATE
    INLINE static StructType *Gen_SWR_BLEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(Type::getFloatTy(ctx), 4) );    // constantColor
        members.push_back( Type::getInt32Ty(ctx) );    // alphaTestReference
        members.push_back( Type::getInt32Ty(ctx) );    // sampleMask
        members.push_back( Type::getInt32Ty(ctx) );    // sampleCount
        members.push_back( ArrayType::get(Gen_SWR_RENDER_TARGET_BLEND_STATE(pJitMgr), SWR_NUM_RENDERTARGETS) );    // renderTarget

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_BLEND_STATE_constantColor = 0;
    static const uint32_t SWR_BLEND_STATE_alphaTestReference = 1;
    static const uint32_t SWR_BLEND_STATE_sampleMask = 2;
    static const uint32_t SWR_BLEND_STATE_sampleCount = 3;
    static const uint32_t SWR_BLEND_STATE_renderTarget = 4;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_FRONTEND_STATE
    INLINE static StructType *Gen_SWR_FRONTEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt8Ty(ctx) );    // vpTransformDisable
        members.push_back( Type::getInt8Ty(ctx) );    // bEnableCutIndex
        members.push_back( Type::getInt32Ty(ctx) );    // triFan
        members.push_back( Type::getInt32Ty(ctx) );    // lineStripList
        members.push_back( Type::getInt32Ty(ctx) );    // triStripList

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_FRONTEND_STATE_vpTransformDisable = 0;
    static const uint32_t SWR_FRONTEND_STATE_bEnableCutIndex = 1;
    static const uint32_t SWR_FRONTEND_STATE_triFan = 2;
    static const uint32_t SWR_FRONTEND_STATE_lineStripList = 3;
    static const uint32_t SWR_FRONTEND_STATE_triStripList = 4;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_VIEWPORT_MATRIX
    INLINE static StructType *Gen_SWR_VIEWPORT_MATRIX(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getFloatTy(ctx) );    // m00
        members.push_back( Type::getFloatTy(ctx) );    // m11
        members.push_back( Type::getFloatTy(ctx) );    // m22
        members.push_back( Type::getFloatTy(ctx) );    // m30
        members.push_back( Type::getFloatTy(ctx) );    // m31
        members.push_back( Type::getFloatTy(ctx) );    // m32

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VIEWPORT_MATRIX_m00 = 0;
    static const uint32_t SWR_VIEWPORT_MATRIX_m11 = 1;
    static const uint32_t SWR_VIEWPORT_MATRIX_m22 = 2;
    static const uint32_t SWR_VIEWPORT_MATRIX_m30 = 3;
    static const uint32_t SWR_VIEWPORT_MATRIX_m31 = 4;
    static const uint32_t SWR_VIEWPORT_MATRIX_m32 = 5;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_VIEWPORT_MATRICES
    INLINE static StructType *Gen_SWR_VIEWPORT_MATRICES(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );    // m00
        members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );    // m11
        members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );    // m22
        members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );    // m30
        members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );    // m31
        members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );    // m32

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VIEWPORT_MATRICES_m00 = 0;
    static const uint32_t SWR_VIEWPORT_MATRICES_m11 = 1;
    static const uint32_t SWR_VIEWPORT_MATRICES_m22 = 2;
    static const uint32_t SWR_VIEWPORT_MATRICES_m30 = 3;
    static const uint32_t SWR_VIEWPORT_MATRICES_m31 = 4;
    static const uint32_t SWR_VIEWPORT_MATRICES_m32 = 5;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_VIEWPORT
    INLINE static StructType *Gen_SWR_VIEWPORT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getFloatTy(ctx) );    // x
        members.push_back( Type::getFloatTy(ctx) );    // y
        members.push_back( Type::getFloatTy(ctx) );    // width
        members.push_back( Type::getFloatTy(ctx) );    // height
        members.push_back( Type::getFloatTy(ctx) );    // minZ
        members.push_back( Type::getFloatTy(ctx) );    // maxZ

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VIEWPORT_x = 0;
    static const uint32_t SWR_VIEWPORT_y = 1;
    static const uint32_t SWR_VIEWPORT_width = 2;
    static const uint32_t SWR_VIEWPORT_height = 3;
    static const uint32_t SWR_VIEWPORT_minZ = 4;
    static const uint32_t SWR_VIEWPORT_maxZ = 5;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_MULTISAMPLE_POS
    INLINE static StructType *Gen_SWR_MULTISAMPLE_POS(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // x
        members.push_back( Type::getInt32Ty(ctx) );    // y

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_MULTISAMPLE_POS_x = 0;
    static const uint32_t SWR_MULTISAMPLE_POS_y = 1;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_RASTSTATE
    INLINE static StructType *Gen_SWR_RASTSTATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // cullMode
        members.push_back( Type::getInt32Ty(ctx) );    // fillMode
        members.push_back( Type::getInt32Ty(ctx) );    // frontWinding
        members.push_back( Type::getInt32Ty(ctx) );    // scissorEnable
        members.push_back( Type::getInt32Ty(ctx) );    // depthClipEnable
        members.push_back( Type::getInt32Ty(ctx) );    // clipHalfZ
        members.push_back( Type::getInt32Ty(ctx) );    // pointParam
        members.push_back( Type::getInt32Ty(ctx) );    // pointSpriteEnable
        members.push_back( Type::getInt32Ty(ctx) );    // pointSpriteTopOrigin
        members.push_back( Type::getInt32Ty(ctx) );    // msaaRastEnable
        members.push_back( Type::getInt32Ty(ctx) );    // forcedSampleCount
        members.push_back( Type::getInt32Ty(ctx) );    // pixelOffset
        members.push_back( Type::getInt32Ty(ctx) );    // depthBiasPreAdjusted
        members.push_back( Type::getInt32Ty(ctx) );    // conservativeRast
        members.push_back( Type::getFloatTy(ctx) );    // pointSize
        members.push_back( Type::getFloatTy(ctx) );    // lineWidth
        members.push_back( Type::getFloatTy(ctx) );    // depthBias
        members.push_back( Type::getFloatTy(ctx) );    // slopeScaledDepthBias
        members.push_back( Type::getFloatTy(ctx) );    // depthBiasClamp
        members.push_back( Type::getInt32Ty(ctx) );    // depthFormat
        members.push_back( Type::getInt32Ty(ctx) );    // rastMode
        members.push_back( Type::getInt32Ty(ctx) );    // sampleCount
        members.push_back( Type::getInt32Ty(ctx) );    // pixelLocation
        members.push_back( ArrayType::get(Gen_SWR_MULTISAMPLE_POS(pJitMgr), SWR_MAX_NUM_MULTISAMPLES) );    // iSamplePos
        members.push_back( Type::getInt32Ty(ctx) );    // samplePattern
        members.push_back( Type::getInt8Ty(ctx) );    // cullDistanceMask
        members.push_back( Type::getInt8Ty(ctx) );    // clipDistanceMask

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_RASTSTATE_cullMode = 0;
    static const uint32_t SWR_RASTSTATE_fillMode = 1;
    static const uint32_t SWR_RASTSTATE_frontWinding = 2;
    static const uint32_t SWR_RASTSTATE_scissorEnable = 3;
    static const uint32_t SWR_RASTSTATE_depthClipEnable = 4;
    static const uint32_t SWR_RASTSTATE_clipHalfZ = 5;
    static const uint32_t SWR_RASTSTATE_pointParam = 6;
    static const uint32_t SWR_RASTSTATE_pointSpriteEnable = 7;
    static const uint32_t SWR_RASTSTATE_pointSpriteTopOrigin = 8;
    static const uint32_t SWR_RASTSTATE_msaaRastEnable = 9;
    static const uint32_t SWR_RASTSTATE_forcedSampleCount = 10;
    static const uint32_t SWR_RASTSTATE_pixelOffset = 11;
    static const uint32_t SWR_RASTSTATE_depthBiasPreAdjusted = 12;
    static const uint32_t SWR_RASTSTATE_conservativeRast = 13;
    static const uint32_t SWR_RASTSTATE_pointSize = 14;
    static const uint32_t SWR_RASTSTATE_lineWidth = 15;
    static const uint32_t SWR_RASTSTATE_depthBias = 16;
    static const uint32_t SWR_RASTSTATE_slopeScaledDepthBias = 17;
    static const uint32_t SWR_RASTSTATE_depthBiasClamp = 18;
    static const uint32_t SWR_RASTSTATE_depthFormat = 19;
    static const uint32_t SWR_RASTSTATE_rastMode = 20;
    static const uint32_t SWR_RASTSTATE_sampleCount = 21;
    static const uint32_t SWR_RASTSTATE_pixelLocation = 22;
    static const uint32_t SWR_RASTSTATE_iSamplePos = 23;
    static const uint32_t SWR_RASTSTATE_samplePattern = 24;
    static const uint32_t SWR_RASTSTATE_cullDistanceMask = 25;
    static const uint32_t SWR_RASTSTATE_clipDistanceMask = 26;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_ATTRIB_SWIZZLE
    INLINE static StructType *Gen_SWR_ATTRIB_SWIZZLE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt16Ty(ctx) );    // sourceAttrib
        members.push_back( Type::getInt16Ty(ctx) );    // constantSource
        members.push_back( Type::getInt16Ty(ctx) );    // componentOverrideMask

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_ATTRIB_SWIZZLE_sourceAttrib = 0;
    static const uint32_t SWR_ATTRIB_SWIZZLE_constantSource = 1;
    static const uint32_t SWR_ATTRIB_SWIZZLE_componentOverrideMask = 2;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_BACKEND_STATE
    INLINE static StructType *Gen_SWR_BACKEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // constantInterpolationMask
        members.push_back( Type::getInt32Ty(ctx) );    // pointSpriteTexCoordMask
        members.push_back( Type::getInt8Ty(ctx) );    // numAttributes
        members.push_back( ArrayType::get(Type::getInt8Ty(ctx), 32) );    // numComponents
        members.push_back( Type::getInt8Ty(ctx) );    // swizzleEnable
        members.push_back( ArrayType::get(Gen_SWR_ATTRIB_SWIZZLE(pJitMgr), 32) );    // swizzleMap

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_BACKEND_STATE_constantInterpolationMask = 0;
    static const uint32_t SWR_BACKEND_STATE_pointSpriteTexCoordMask = 1;
    static const uint32_t SWR_BACKEND_STATE_numAttributes = 2;
    static const uint32_t SWR_BACKEND_STATE_numComponents = 3;
    static const uint32_t SWR_BACKEND_STATE_swizzleEnable = 4;
    static const uint32_t SWR_BACKEND_STATE_swizzleMap = 5;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_PS_STATE
    INLINE static StructType *Gen_SWR_PS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // pfnPixelShader
        members.push_back( Type::getInt32Ty(ctx) );    // killsPixel
        members.push_back( Type::getInt32Ty(ctx) );    // inputCoverage
        members.push_back( Type::getInt32Ty(ctx) );    // writesODepth
        members.push_back( Type::getInt32Ty(ctx) );    // usesSourceDepth
        members.push_back( Type::getInt32Ty(ctx) );    // shadingRate
        members.push_back( Type::getInt32Ty(ctx) );    // numRenderTargets
        members.push_back( Type::getInt32Ty(ctx) );    // posOffset
        members.push_back( Type::getInt32Ty(ctx) );    // barycentricsMask
        members.push_back( Type::getInt32Ty(ctx) );    // usesUAV
        members.push_back( Type::getInt32Ty(ctx) );    // forceEarlyZ

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_PS_STATE_pfnPixelShader = 0;
    static const uint32_t SWR_PS_STATE_killsPixel = 1;
    static const uint32_t SWR_PS_STATE_inputCoverage = 2;
    static const uint32_t SWR_PS_STATE_writesODepth = 3;
    static const uint32_t SWR_PS_STATE_usesSourceDepth = 4;
    static const uint32_t SWR_PS_STATE_shadingRate = 5;
    static const uint32_t SWR_PS_STATE_numRenderTargets = 6;
    static const uint32_t SWR_PS_STATE_posOffset = 7;
    static const uint32_t SWR_PS_STATE_barycentricsMask = 8;
    static const uint32_t SWR_PS_STATE_usesUAV = 9;
    static const uint32_t SWR_PS_STATE_forceEarlyZ = 10;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for SWR_DEPTH_BOUNDS_STATE
    INLINE static StructType *Gen_SWR_DEPTH_BOUNDS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt8Ty(ctx) );    // depthBoundsTestEnable
        members.push_back( Type::getFloatTy(ctx) );    // depthBoundsTestMinValue
        members.push_back( Type::getFloatTy(ctx) );    // depthBoundsTestMaxValue

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_DEPTH_BOUNDS_STATE_depthBoundsTestEnable = 0;
    static const uint32_t SWR_DEPTH_BOUNDS_STATE_depthBoundsTestMinValue = 1;
    static const uint32_t SWR_DEPTH_BOUNDS_STATE_depthBoundsTestMaxValue = 2;

}
