/****************************************************************************
* Copyright (C) 2014-2017 Intel Corporation.   All Rights Reserved.
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
* @file gen_state_llvm.h
*
* @brief auto-generated file
*
* DO NOT EDIT
*
* Generation Command Line:
*   ./rasterizer/codegen/gen_llvm_types.py
*     --input
*     ./rasterizer/core/state.h
*     --output
*     rasterizer/jitter/gen_state_llvm.h
*
******************************************************************************/
#pragma once

namespace SwrJit
{
    using namespace llvm;

    INLINE static StructType *Gen_simdvertex(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* attrib */ members.push_back( ArrayType::get(ArrayType::get(VectorType::get(Type::getFloatTy(ctx), 8), 4), SWR_VTX_NUM_SLOTS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t simdvertex_attrib = 0;

    INLINE static StructType *Gen_simd16vertex(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* attrib */ members.push_back( ArrayType::get(ArrayType::get(VectorType::get(Type::getFloatTy(ctx), 16), 4), SWR_VTX_NUM_SLOTS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t simd16vertex_attrib = 0;

    INLINE static StructType *Gen_SWR_VS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* pVin            */ members.push_back( PointerType::get(Gen_simdvertex(pJitMgr), 0) );
        /* pVout           */ members.push_back( PointerType::get(Gen_simdvertex(pJitMgr), 0) );
        /* InstanceID      */ members.push_back( Type::getInt32Ty(ctx) );
        /* VertexID        */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* mask            */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* AlternateOffset */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VS_CONTEXT_pVin            = 0;
    static const uint32_t SWR_VS_CONTEXT_pVout           = 1;
    static const uint32_t SWR_VS_CONTEXT_InstanceID      = 2;
    static const uint32_t SWR_VS_CONTEXT_VertexID        = 3;
    static const uint32_t SWR_VS_CONTEXT_mask            = 4;
    static const uint32_t SWR_VS_CONTEXT_AlternateOffset = 5;

    INLINE static StructType *Gen_ScalarAttrib(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* x */ members.push_back( Type::getFloatTy(ctx) );
        /* y */ members.push_back( Type::getFloatTy(ctx) );
        /* z */ members.push_back( Type::getFloatTy(ctx) );
        /* w */ members.push_back( Type::getFloatTy(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t ScalarAttrib_x = 0;
    static const uint32_t ScalarAttrib_y = 1;
    static const uint32_t ScalarAttrib_z = 2;
    static const uint32_t ScalarAttrib_w = 3;

    INLINE static StructType *Gen_ScalarCPoint(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* attrib */ members.push_back( ArrayType::get(Gen_ScalarAttrib(pJitMgr), SWR_VTX_NUM_SLOTS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t ScalarCPoint_attrib = 0;

    INLINE static StructType *Gen_SWR_TESSELLATION_FACTORS(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* OuterTessFactors */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), SWR_NUM_OUTER_TESS_FACTORS) );
        /* InnerTessFactors */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), SWR_NUM_INNER_TESS_FACTORS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_TESSELLATION_FACTORS_OuterTessFactors = 0;
    static const uint32_t SWR_TESSELLATION_FACTORS_InnerTessFactors = 1;

    INLINE static StructType *Gen_ScalarPatch(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* tessFactors */ members.push_back( Gen_SWR_TESSELLATION_FACTORS(pJitMgr) );
        /* cp          */ members.push_back( ArrayType::get(Gen_ScalarCPoint(pJitMgr), MAX_NUM_VERTS_PER_PRIM) );
        /* patchData   */ members.push_back( Gen_ScalarCPoint(pJitMgr) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t ScalarPatch_tessFactors = 0;
    static const uint32_t ScalarPatch_cp          = 1;
    static const uint32_t ScalarPatch_patchData   = 2;

    INLINE static StructType *Gen_SWR_HS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* vert        */ members.push_back( ArrayType::get(Gen_simdvertex(pJitMgr), MAX_NUM_VERTS_PER_PRIM) );
        /* PrimitiveID */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* mask        */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* pCPout      */ members.push_back( PointerType::get(Gen_ScalarPatch(pJitMgr), 0) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_HS_CONTEXT_vert        = 0;
    static const uint32_t SWR_HS_CONTEXT_PrimitiveID = 1;
    static const uint32_t SWR_HS_CONTEXT_mask        = 2;
    static const uint32_t SWR_HS_CONTEXT_pCPout      = 3;

    INLINE static StructType *Gen_SWR_DS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* PrimitiveID  */ members.push_back( Type::getInt32Ty(ctx) );
        /* vectorOffset */ members.push_back( Type::getInt32Ty(ctx) );
        /* vectorStride */ members.push_back( Type::getInt32Ty(ctx) );
        /* pCpIn        */ members.push_back( PointerType::get(Gen_ScalarPatch(pJitMgr), 0) );
        /* pDomainU     */ members.push_back( PointerType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 0) );
        /* pDomainV     */ members.push_back( PointerType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 0) );
        /* mask         */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* pOutputData  */ members.push_back( PointerType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), 0) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_DS_CONTEXT_PrimitiveID  = 0;
    static const uint32_t SWR_DS_CONTEXT_vectorOffset = 1;
    static const uint32_t SWR_DS_CONTEXT_vectorStride = 2;
    static const uint32_t SWR_DS_CONTEXT_pCpIn        = 3;
    static const uint32_t SWR_DS_CONTEXT_pDomainU     = 4;
    static const uint32_t SWR_DS_CONTEXT_pDomainV     = 5;
    static const uint32_t SWR_DS_CONTEXT_mask         = 6;
    static const uint32_t SWR_DS_CONTEXT_pOutputData  = 7;

    INLINE static StructType *Gen_SWR_GS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* vert                 */ members.push_back( ArrayType::get(Gen_simdvertex(pJitMgr), MAX_NUM_VERTS_PER_PRIM) );
        /* PrimitiveID          */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* InstanceID           */ members.push_back( Type::getInt32Ty(ctx) );
        /* mask                 */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* pStream              */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* pCutOrStreamIdBuffer */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* vertexCount          */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_GS_CONTEXT_vert                 = 0;
    static const uint32_t SWR_GS_CONTEXT_PrimitiveID          = 1;
    static const uint32_t SWR_GS_CONTEXT_InstanceID           = 2;
    static const uint32_t SWR_GS_CONTEXT_mask                 = 3;
    static const uint32_t SWR_GS_CONTEXT_pStream              = 4;
    static const uint32_t SWR_GS_CONTEXT_pCutOrStreamIdBuffer = 5;
    static const uint32_t SWR_GS_CONTEXT_vertexCount          = 6;

    INLINE static StructType *Gen_PixelPositions(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* UL       */ members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );
        /* center   */ members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );
        /* sample   */ members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );
        /* centroid */ members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t PixelPositions_UL       = 0;
    static const uint32_t PixelPositions_center   = 1;
    static const uint32_t PixelPositions_sample   = 2;
    static const uint32_t PixelPositions_centroid = 3;

    INLINE static StructType *Gen_SWR_PS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* vX                     */ members.push_back( Gen_PixelPositions(pJitMgr) );
        /* vY                     */ members.push_back( Gen_PixelPositions(pJitMgr) );
        /* vZ                     */ members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );
        /* activeMask             */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* inputMask              */ members.push_back( VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth) );
        /* oMask                  */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* vI                     */ members.push_back( Gen_PixelPositions(pJitMgr) );
        /* vJ                     */ members.push_back( Gen_PixelPositions(pJitMgr) );
        /* vOneOverW              */ members.push_back( Gen_PixelPositions(pJitMgr) );
        /* pAttribs               */ members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );
        /* pPerspAttribs          */ members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );
        /* pRecipW                */ members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );
        /* I                      */ members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );
        /* J                      */ members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );
        /* recipDet               */ members.push_back( Type::getFloatTy(ctx) );
        /* pSamplePosX            */ members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );
        /* pSamplePosY            */ members.push_back( PointerType::get(Type::getFloatTy(ctx), 0) );
        /* shaded                 */ members.push_back( ArrayType::get(ArrayType::get(VectorType::get(Type::getFloatTy(ctx), 8), 4), SWR_NUM_RENDERTARGETS) );
        /* frontFace              */ members.push_back( Type::getInt32Ty(ctx) );
        /* sampleIndex            */ members.push_back( Type::getInt32Ty(ctx) );
        /* renderTargetArrayIndex */ members.push_back( Type::getInt32Ty(ctx) );
        /* rasterizerSampleCount  */ members.push_back( Type::getInt32Ty(ctx) );
        /* pColorBuffer           */ members.push_back( ArrayType::get(PointerType::get(Type::getInt8Ty(ctx), 0), SWR_NUM_RENDERTARGETS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_PS_CONTEXT_vX                     = 0;
    static const uint32_t SWR_PS_CONTEXT_vY                     = 1;
    static const uint32_t SWR_PS_CONTEXT_vZ                     = 2;
    static const uint32_t SWR_PS_CONTEXT_activeMask             = 3;
    static const uint32_t SWR_PS_CONTEXT_inputMask              = 4;
    static const uint32_t SWR_PS_CONTEXT_oMask                  = 5;
    static const uint32_t SWR_PS_CONTEXT_vI                     = 6;
    static const uint32_t SWR_PS_CONTEXT_vJ                     = 7;
    static const uint32_t SWR_PS_CONTEXT_vOneOverW              = 8;
    static const uint32_t SWR_PS_CONTEXT_pAttribs               = 9;
    static const uint32_t SWR_PS_CONTEXT_pPerspAttribs          = 10;
    static const uint32_t SWR_PS_CONTEXT_pRecipW                = 11;
    static const uint32_t SWR_PS_CONTEXT_I                      = 12;
    static const uint32_t SWR_PS_CONTEXT_J                      = 13;
    static const uint32_t SWR_PS_CONTEXT_recipDet               = 14;
    static const uint32_t SWR_PS_CONTEXT_pSamplePosX            = 15;
    static const uint32_t SWR_PS_CONTEXT_pSamplePosY            = 16;
    static const uint32_t SWR_PS_CONTEXT_shaded                 = 17;
    static const uint32_t SWR_PS_CONTEXT_frontFace              = 18;
    static const uint32_t SWR_PS_CONTEXT_sampleIndex            = 19;
    static const uint32_t SWR_PS_CONTEXT_renderTargetArrayIndex = 20;
    static const uint32_t SWR_PS_CONTEXT_rasterizerSampleCount  = 21;
    static const uint32_t SWR_PS_CONTEXT_pColorBuffer           = 22;

    INLINE static StructType *Gen_SWR_CS_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* tileCounter         */ members.push_back( Type::getInt32Ty(ctx) );
        /* dispatchDims        */ members.push_back( ArrayType::get(Type::getInt32Ty(ctx), 3) );
        /* pTGSM               */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* pSpillFillBuffer    */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* pScratchSpace       */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* scratchSpacePerSimd */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_CS_CONTEXT_tileCounter         = 0;
    static const uint32_t SWR_CS_CONTEXT_dispatchDims        = 1;
    static const uint32_t SWR_CS_CONTEXT_pTGSM               = 2;
    static const uint32_t SWR_CS_CONTEXT_pSpillFillBuffer    = 3;
    static const uint32_t SWR_CS_CONTEXT_pScratchSpace       = 4;
    static const uint32_t SWR_CS_CONTEXT_scratchSpacePerSimd = 5;

    INLINE static StructType *Gen_SWR_SURFACE_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* pBaseAddress        */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* type                */ members.push_back( Type::getInt32Ty(ctx) );
        /* format              */ members.push_back( Type::getInt32Ty(ctx) );
        /* width               */ members.push_back( Type::getInt32Ty(ctx) );
        /* height              */ members.push_back( Type::getInt32Ty(ctx) );
        /* depth               */ members.push_back( Type::getInt32Ty(ctx) );
        /* numSamples          */ members.push_back( Type::getInt32Ty(ctx) );
        /* samplePattern       */ members.push_back( Type::getInt32Ty(ctx) );
        /* pitch               */ members.push_back( Type::getInt32Ty(ctx) );
        /* qpitch              */ members.push_back( Type::getInt32Ty(ctx) );
        /* minLod              */ members.push_back( Type::getInt32Ty(ctx) );
        /* maxLod              */ members.push_back( Type::getInt32Ty(ctx) );
        /* resourceMinLod      */ members.push_back( Type::getFloatTy(ctx) );
        /* lod                 */ members.push_back( Type::getInt32Ty(ctx) );
        /* arrayIndex          */ members.push_back( Type::getInt32Ty(ctx) );
        /* tileMode            */ members.push_back( Type::getInt32Ty(ctx) );
        /* halign              */ members.push_back( Type::getInt32Ty(ctx) );
        /* valign              */ members.push_back( Type::getInt32Ty(ctx) );
        /* xOffset             */ members.push_back( Type::getInt32Ty(ctx) );
        /* yOffset             */ members.push_back( Type::getInt32Ty(ctx) );
        /* lodOffsets          */ members.push_back( ArrayType::get(ArrayType::get(Type::getInt32Ty(ctx), 15), 2) );
        /* pAuxBaseAddress     */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* auxMode             */ members.push_back( Type::getInt32Ty(ctx) );
        /* bInterleavedSamples */ members.push_back( Type::getInt8Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_SURFACE_STATE_pBaseAddress        = 0;
    static const uint32_t SWR_SURFACE_STATE_type                = 1;
    static const uint32_t SWR_SURFACE_STATE_format              = 2;
    static const uint32_t SWR_SURFACE_STATE_width               = 3;
    static const uint32_t SWR_SURFACE_STATE_height              = 4;
    static const uint32_t SWR_SURFACE_STATE_depth               = 5;
    static const uint32_t SWR_SURFACE_STATE_numSamples          = 6;
    static const uint32_t SWR_SURFACE_STATE_samplePattern       = 7;
    static const uint32_t SWR_SURFACE_STATE_pitch               = 8;
    static const uint32_t SWR_SURFACE_STATE_qpitch              = 9;
    static const uint32_t SWR_SURFACE_STATE_minLod              = 10;
    static const uint32_t SWR_SURFACE_STATE_maxLod              = 11;
    static const uint32_t SWR_SURFACE_STATE_resourceMinLod      = 12;
    static const uint32_t SWR_SURFACE_STATE_lod                 = 13;
    static const uint32_t SWR_SURFACE_STATE_arrayIndex          = 14;
    static const uint32_t SWR_SURFACE_STATE_tileMode            = 15;
    static const uint32_t SWR_SURFACE_STATE_halign              = 16;
    static const uint32_t SWR_SURFACE_STATE_valign              = 17;
    static const uint32_t SWR_SURFACE_STATE_xOffset             = 18;
    static const uint32_t SWR_SURFACE_STATE_yOffset             = 19;
    static const uint32_t SWR_SURFACE_STATE_lodOffsets          = 20;
    static const uint32_t SWR_SURFACE_STATE_pAuxBaseAddress     = 21;
    static const uint32_t SWR_SURFACE_STATE_auxMode             = 22;
    static const uint32_t SWR_SURFACE_STATE_bInterleavedSamples = 23;

    INLINE static StructType *Gen_SWR_VERTEX_BUFFER_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* index               */ members.push_back( Type::getInt32Ty(ctx) );
        /* pitch               */ members.push_back( Type::getInt32Ty(ctx) );
        /* pData               */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* size                */ members.push_back( Type::getInt32Ty(ctx) );
        /* numaNode            */ members.push_back( Type::getInt32Ty(ctx) );
        /* minVertex           */ members.push_back( Type::getInt32Ty(ctx) );
        /* maxVertex           */ members.push_back( Type::getInt32Ty(ctx) );
        /* partialInboundsSize */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VERTEX_BUFFER_STATE_index               = 0;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_pitch               = 1;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_pData               = 2;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_size                = 3;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_numaNode            = 4;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_minVertex           = 5;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_maxVertex           = 6;
    static const uint32_t SWR_VERTEX_BUFFER_STATE_partialInboundsSize = 7;

    INLINE static StructType *Gen_SWR_INDEX_BUFFER_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* format   */ members.push_back( Type::getInt32Ty(ctx) );
        /* pIndices */ members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );
        /* size     */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_INDEX_BUFFER_STATE_format   = 0;
    static const uint32_t SWR_INDEX_BUFFER_STATE_pIndices = 1;
    static const uint32_t SWR_INDEX_BUFFER_STATE_size     = 2;

    INLINE static StructType *Gen_SWR_FETCH_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* pStreams      */ members.push_back( PointerType::get(Gen_SWR_VERTEX_BUFFER_STATE(pJitMgr), 0) );
        /* pIndices      */ members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );
        /* pLastIndex    */ members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );
        /* CurInstance   */ members.push_back( Type::getInt32Ty(ctx) );
        /* BaseVertex    */ members.push_back( Type::getInt32Ty(ctx) );
        /* StartVertex   */ members.push_back( Type::getInt32Ty(ctx) );
        /* StartInstance */ members.push_back( Type::getInt32Ty(ctx) );
        /* VertexID      */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );
        /* CutMask       */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), pJitMgr->mVWidth) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_FETCH_CONTEXT_pStreams      = 0;
    static const uint32_t SWR_FETCH_CONTEXT_pIndices      = 1;
    static const uint32_t SWR_FETCH_CONTEXT_pLastIndex    = 2;
    static const uint32_t SWR_FETCH_CONTEXT_CurInstance   = 3;
    static const uint32_t SWR_FETCH_CONTEXT_BaseVertex    = 4;
    static const uint32_t SWR_FETCH_CONTEXT_StartVertex   = 5;
    static const uint32_t SWR_FETCH_CONTEXT_StartInstance = 6;
    static const uint32_t SWR_FETCH_CONTEXT_VertexID      = 7;
    static const uint32_t SWR_FETCH_CONTEXT_CutMask       = 8;

    INLINE static StructType *Gen_SWR_STREAMOUT_BUFFER(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* enable        */ members.push_back( Type::getInt8Ty(ctx) );
        /* soWriteEnable */ members.push_back( Type::getInt8Ty(ctx) );
        /* pBuffer       */ members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );
        /* bufferSize    */ members.push_back( Type::getInt32Ty(ctx) );
        /* pitch         */ members.push_back( Type::getInt32Ty(ctx) );
        /* streamOffset  */ members.push_back( Type::getInt32Ty(ctx) );
        /* pWriteOffset  */ members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_STREAMOUT_BUFFER_enable        = 0;
    static const uint32_t SWR_STREAMOUT_BUFFER_soWriteEnable = 1;
    static const uint32_t SWR_STREAMOUT_BUFFER_pBuffer       = 2;
    static const uint32_t SWR_STREAMOUT_BUFFER_bufferSize    = 3;
    static const uint32_t SWR_STREAMOUT_BUFFER_pitch         = 4;
    static const uint32_t SWR_STREAMOUT_BUFFER_streamOffset  = 5;
    static const uint32_t SWR_STREAMOUT_BUFFER_pWriteOffset  = 6;

    INLINE static StructType *Gen_SWR_STREAMOUT_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* soEnable           */ members.push_back( Type::getInt8Ty(ctx) );
        /* streamEnable       */ members.push_back( ArrayType::get(Type::getInt8Ty(ctx), MAX_SO_STREAMS) );
        /* rasterizerDisable  */ members.push_back( Type::getInt8Ty(ctx) );
        /* streamToRasterizer */ members.push_back( Type::getInt32Ty(ctx) );
        /* streamMasks        */ members.push_back( ArrayType::get(Type::getInt32Ty(ctx), MAX_SO_STREAMS) );
        /* streamNumEntries   */ members.push_back( ArrayType::get(Type::getInt32Ty(ctx), MAX_SO_STREAMS) );
        /* vertexAttribOffset */ members.push_back( ArrayType::get(Type::getInt32Ty(ctx), MAX_SO_STREAMS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_STREAMOUT_STATE_soEnable           = 0;
    static const uint32_t SWR_STREAMOUT_STATE_streamEnable       = 1;
    static const uint32_t SWR_STREAMOUT_STATE_rasterizerDisable  = 2;
    static const uint32_t SWR_STREAMOUT_STATE_streamToRasterizer = 3;
    static const uint32_t SWR_STREAMOUT_STATE_streamMasks        = 4;
    static const uint32_t SWR_STREAMOUT_STATE_streamNumEntries   = 5;
    static const uint32_t SWR_STREAMOUT_STATE_vertexAttribOffset = 6;

    INLINE static StructType *Gen_SWR_STREAMOUT_CONTEXT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* pPrimData            */ members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );
        /* pBuffer              */ members.push_back( ArrayType::get(PointerType::get(Gen_SWR_STREAMOUT_BUFFER(pJitMgr), 0), MAX_SO_STREAMS) );
        /* numPrimsWritten      */ members.push_back( Type::getInt32Ty(ctx) );
        /* numPrimStorageNeeded */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_STREAMOUT_CONTEXT_pPrimData            = 0;
    static const uint32_t SWR_STREAMOUT_CONTEXT_pBuffer              = 1;
    static const uint32_t SWR_STREAMOUT_CONTEXT_numPrimsWritten      = 2;
    static const uint32_t SWR_STREAMOUT_CONTEXT_numPrimStorageNeeded = 3;

    INLINE static StructType *Gen_SWR_GS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* gsEnable           */ members.push_back( Type::getInt8Ty(ctx) );
        /* numInputAttribs    */ members.push_back( Type::getInt32Ty(ctx) );
        /* outputTopology     */ members.push_back( Type::getInt32Ty(ctx) );
        /* maxNumVerts        */ members.push_back( Type::getInt32Ty(ctx) );
        /* instanceCount      */ members.push_back( Type::getInt32Ty(ctx) );
        /* isSingleStream     */ members.push_back( Type::getInt8Ty(ctx) );
        /* singleStreamID     */ members.push_back( Type::getInt32Ty(ctx) );
        /* vertexAttribOffset */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_GS_STATE_gsEnable           = 0;
    static const uint32_t SWR_GS_STATE_numInputAttribs    = 1;
    static const uint32_t SWR_GS_STATE_outputTopology     = 2;
    static const uint32_t SWR_GS_STATE_maxNumVerts        = 3;
    static const uint32_t SWR_GS_STATE_instanceCount      = 4;
    static const uint32_t SWR_GS_STATE_isSingleStream     = 5;
    static const uint32_t SWR_GS_STATE_singleStreamID     = 6;
    static const uint32_t SWR_GS_STATE_vertexAttribOffset = 7;

    INLINE static StructType *Gen_SWR_TS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* tsEnable           */ members.push_back( Type::getInt8Ty(ctx) );
        /* tsOutputTopology   */ members.push_back( Type::getInt32Ty(ctx) );
        /* partitioning       */ members.push_back( Type::getInt32Ty(ctx) );
        /* domain             */ members.push_back( Type::getInt32Ty(ctx) );
        /* postDSTopology     */ members.push_back( Type::getInt32Ty(ctx) );
        /* numHsInputAttribs  */ members.push_back( Type::getInt32Ty(ctx) );
        /* numHsOutputAttribs */ members.push_back( Type::getInt32Ty(ctx) );
        /* numDsOutputAttribs */ members.push_back( Type::getInt32Ty(ctx) );
        /* vertexAttribOffset */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_TS_STATE_tsEnable           = 0;
    static const uint32_t SWR_TS_STATE_tsOutputTopology   = 1;
    static const uint32_t SWR_TS_STATE_partitioning       = 2;
    static const uint32_t SWR_TS_STATE_domain             = 3;
    static const uint32_t SWR_TS_STATE_postDSTopology     = 4;
    static const uint32_t SWR_TS_STATE_numHsInputAttribs  = 5;
    static const uint32_t SWR_TS_STATE_numHsOutputAttribs = 6;
    static const uint32_t SWR_TS_STATE_numDsOutputAttribs = 7;
    static const uint32_t SWR_TS_STATE_vertexAttribOffset = 8;

    INLINE static StructType *Gen_SWR_RENDER_TARGET_BLEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* writeDisableRed   */ members.push_back( Type::getInt8Ty(ctx) );
        /* writeDisableGreen */ members.push_back( Type::getInt8Ty(ctx) );
        /* writeDisableBlue  */ members.push_back( Type::getInt8Ty(ctx) );
        /* writeDisableAlpha */ members.push_back( Type::getInt8Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableRed   = 0;
    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableGreen = 1;
    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableBlue  = 2;
    static const uint32_t SWR_RENDER_TARGET_BLEND_STATE_writeDisableAlpha = 3;

    INLINE static StructType *Gen_SWR_BLEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* constantColor      */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), 4) );
        /* alphaTestReference */ members.push_back( Type::getInt32Ty(ctx) );
        /* sampleMask         */ members.push_back( Type::getInt32Ty(ctx) );
        /* sampleCount        */ members.push_back( Type::getInt32Ty(ctx) );
        /* renderTarget       */ members.push_back( ArrayType::get(Gen_SWR_RENDER_TARGET_BLEND_STATE(pJitMgr), SWR_NUM_RENDERTARGETS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_BLEND_STATE_constantColor      = 0;
    static const uint32_t SWR_BLEND_STATE_alphaTestReference = 1;
    static const uint32_t SWR_BLEND_STATE_sampleMask         = 2;
    static const uint32_t SWR_BLEND_STATE_sampleCount        = 3;
    static const uint32_t SWR_BLEND_STATE_renderTarget       = 4;

    INLINE static StructType *Gen_SWR_FRONTEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* vpTransformDisable */ members.push_back( Type::getInt8Ty(ctx) );
        /* bEnableCutIndex    */ members.push_back( Type::getInt8Ty(ctx) );
        /* triFan             */ members.push_back( Type::getInt32Ty(ctx) );
        /* lineStripList      */ members.push_back( Type::getInt32Ty(ctx) );
        /* triStripList       */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_FRONTEND_STATE_vpTransformDisable = 0;
    static const uint32_t SWR_FRONTEND_STATE_bEnableCutIndex    = 1;
    static const uint32_t SWR_FRONTEND_STATE_triFan             = 2;
    static const uint32_t SWR_FRONTEND_STATE_lineStripList      = 3;
    static const uint32_t SWR_FRONTEND_STATE_triStripList       = 4;

    INLINE static StructType *Gen_SWR_VIEWPORT_MATRIX(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* m00 */ members.push_back( Type::getFloatTy(ctx) );
        /* m11 */ members.push_back( Type::getFloatTy(ctx) );
        /* m22 */ members.push_back( Type::getFloatTy(ctx) );
        /* m30 */ members.push_back( Type::getFloatTy(ctx) );
        /* m31 */ members.push_back( Type::getFloatTy(ctx) );
        /* m32 */ members.push_back( Type::getFloatTy(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VIEWPORT_MATRIX_m00 = 0;
    static const uint32_t SWR_VIEWPORT_MATRIX_m11 = 1;
    static const uint32_t SWR_VIEWPORT_MATRIX_m22 = 2;
    static const uint32_t SWR_VIEWPORT_MATRIX_m30 = 3;
    static const uint32_t SWR_VIEWPORT_MATRIX_m31 = 4;
    static const uint32_t SWR_VIEWPORT_MATRIX_m32 = 5;

    INLINE static StructType *Gen_SWR_VIEWPORT_MATRICES(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* m00 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );
        /* m11 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );
        /* m22 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );
        /* m30 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );
        /* m31 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );
        /* m32 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), KNOB_NUM_VIEWPORTS_SCISSORS) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VIEWPORT_MATRICES_m00 = 0;
    static const uint32_t SWR_VIEWPORT_MATRICES_m11 = 1;
    static const uint32_t SWR_VIEWPORT_MATRICES_m22 = 2;
    static const uint32_t SWR_VIEWPORT_MATRICES_m30 = 3;
    static const uint32_t SWR_VIEWPORT_MATRICES_m31 = 4;
    static const uint32_t SWR_VIEWPORT_MATRICES_m32 = 5;

    INLINE static StructType *Gen_SWR_VIEWPORT(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* x      */ members.push_back( Type::getFloatTy(ctx) );
        /* y      */ members.push_back( Type::getFloatTy(ctx) );
        /* width  */ members.push_back( Type::getFloatTy(ctx) );
        /* height */ members.push_back( Type::getFloatTy(ctx) );
        /* minZ   */ members.push_back( Type::getFloatTy(ctx) );
        /* maxZ   */ members.push_back( Type::getFloatTy(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_VIEWPORT_x      = 0;
    static const uint32_t SWR_VIEWPORT_y      = 1;
    static const uint32_t SWR_VIEWPORT_width  = 2;
    static const uint32_t SWR_VIEWPORT_height = 3;
    static const uint32_t SWR_VIEWPORT_minZ   = 4;
    static const uint32_t SWR_VIEWPORT_maxZ   = 5;

    INLINE static StructType *Gen_SWR_MULTISAMPLE_POS(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* _xi                */ members.push_back( ArrayType::get(Type::getInt32Ty(ctx), SWR_MAX_NUM_MULTISAMPLES) );
        /* _yi                */ members.push_back( ArrayType::get(Type::getInt32Ty(ctx), SWR_MAX_NUM_MULTISAMPLES) );
        /* _x                 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), SWR_MAX_NUM_MULTISAMPLES) );
        /* _y                 */ members.push_back( ArrayType::get(Type::getFloatTy(ctx), SWR_MAX_NUM_MULTISAMPLES) );
        /* _vXi               */ members.push_back( ArrayType::get(VectorType::get(Type::getInt32Ty(ctx), 4), SWR_MAX_NUM_MULTISAMPLES) );
        /* _vYi               */ members.push_back( ArrayType::get(VectorType::get(Type::getInt32Ty(ctx), 4), SWR_MAX_NUM_MULTISAMPLES) );
        /* _vX                */ members.push_back( ArrayType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), SWR_MAX_NUM_MULTISAMPLES) );
        /* _vY                */ members.push_back( ArrayType::get(VectorType::get(Type::getFloatTy(ctx), pJitMgr->mVWidth), SWR_MAX_NUM_MULTISAMPLES) );
        /* tileSampleOffsetsX */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), 4) );
        /* tileSampleOffsetsY */ members.push_back( VectorType::get(Type::getInt32Ty(ctx), 4) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_MULTISAMPLE_POS__xi                = 0;
    static const uint32_t SWR_MULTISAMPLE_POS__yi                = 1;
    static const uint32_t SWR_MULTISAMPLE_POS__x                 = 2;
    static const uint32_t SWR_MULTISAMPLE_POS__y                 = 3;
    static const uint32_t SWR_MULTISAMPLE_POS__vXi               = 4;
    static const uint32_t SWR_MULTISAMPLE_POS__vYi               = 5;
    static const uint32_t SWR_MULTISAMPLE_POS__vX                = 6;
    static const uint32_t SWR_MULTISAMPLE_POS__vY                = 7;
    static const uint32_t SWR_MULTISAMPLE_POS_tileSampleOffsetsX = 8;
    static const uint32_t SWR_MULTISAMPLE_POS_tileSampleOffsetsY = 9;

    INLINE static StructType *Gen_SWR_RASTSTATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* cullMode             */ members.push_back( Type::getInt32Ty(ctx) );
        /* fillMode             */ members.push_back( Type::getInt32Ty(ctx) );
        /* frontWinding         */ members.push_back( Type::getInt32Ty(ctx) );
        /* scissorEnable        */ members.push_back( Type::getInt32Ty(ctx) );
        /* depthClipEnable      */ members.push_back( Type::getInt32Ty(ctx) );
        /* clipHalfZ            */ members.push_back( Type::getInt32Ty(ctx) );
        /* pointParam           */ members.push_back( Type::getInt32Ty(ctx) );
        /* pointSpriteEnable    */ members.push_back( Type::getInt32Ty(ctx) );
        /* pointSpriteTopOrigin */ members.push_back( Type::getInt32Ty(ctx) );
        /* forcedSampleCount    */ members.push_back( Type::getInt32Ty(ctx) );
        /* pixelOffset          */ members.push_back( Type::getInt32Ty(ctx) );
        /* depthBiasPreAdjusted */ members.push_back( Type::getInt32Ty(ctx) );
        /* conservativeRast     */ members.push_back( Type::getInt32Ty(ctx) );
        /* pointSize            */ members.push_back( Type::getFloatTy(ctx) );
        /* lineWidth            */ members.push_back( Type::getFloatTy(ctx) );
        /* depthBias            */ members.push_back( Type::getFloatTy(ctx) );
        /* slopeScaledDepthBias */ members.push_back( Type::getFloatTy(ctx) );
        /* depthBiasClamp       */ members.push_back( Type::getFloatTy(ctx) );
        /* depthFormat          */ members.push_back( Type::getInt32Ty(ctx) );
        /* sampleCount          */ members.push_back( Type::getInt32Ty(ctx) );
        /* pixelLocation        */ members.push_back( Type::getInt32Ty(ctx) );
        /* samplePositions      */ members.push_back( ArrayType::get(Type::getInt8Ty(ctx), sizeof(SWR_MULTISAMPLE_POS)) );
        /* bIsCenterPattern     */ members.push_back( Type::getInt32Ty(ctx) );
        /* cullDistanceMask     */ members.push_back( Type::getInt8Ty(ctx) );
        /* clipDistanceMask     */ members.push_back( Type::getInt8Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_RASTSTATE_cullMode             = 0;
    static const uint32_t SWR_RASTSTATE_fillMode             = 1;
    static const uint32_t SWR_RASTSTATE_frontWinding         = 2;
    static const uint32_t SWR_RASTSTATE_scissorEnable        = 3;
    static const uint32_t SWR_RASTSTATE_depthClipEnable      = 4;
    static const uint32_t SWR_RASTSTATE_clipHalfZ            = 5;
    static const uint32_t SWR_RASTSTATE_pointParam           = 6;
    static const uint32_t SWR_RASTSTATE_pointSpriteEnable    = 7;
    static const uint32_t SWR_RASTSTATE_pointSpriteTopOrigin = 8;
    static const uint32_t SWR_RASTSTATE_forcedSampleCount    = 9;
    static const uint32_t SWR_RASTSTATE_pixelOffset          = 10;
    static const uint32_t SWR_RASTSTATE_depthBiasPreAdjusted = 11;
    static const uint32_t SWR_RASTSTATE_conservativeRast     = 12;
    static const uint32_t SWR_RASTSTATE_pointSize            = 13;
    static const uint32_t SWR_RASTSTATE_lineWidth            = 14;
    static const uint32_t SWR_RASTSTATE_depthBias            = 15;
    static const uint32_t SWR_RASTSTATE_slopeScaledDepthBias = 16;
    static const uint32_t SWR_RASTSTATE_depthBiasClamp       = 17;
    static const uint32_t SWR_RASTSTATE_depthFormat          = 18;
    static const uint32_t SWR_RASTSTATE_sampleCount          = 19;
    static const uint32_t SWR_RASTSTATE_pixelLocation        = 20;
    static const uint32_t SWR_RASTSTATE_samplePositions      = 21;
    static const uint32_t SWR_RASTSTATE_bIsCenterPattern     = 22;
    static const uint32_t SWR_RASTSTATE_cullDistanceMask     = 23;
    static const uint32_t SWR_RASTSTATE_clipDistanceMask     = 24;

    INLINE static StructType *Gen_SWR_ATTRIB_SWIZZLE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* sourceAttrib          */ members.push_back( Type::getInt16Ty(ctx) );
        /* constantSource        */ members.push_back( Type::getInt16Ty(ctx) );
        /* componentOverrideMask */ members.push_back( Type::getInt16Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_ATTRIB_SWIZZLE_sourceAttrib          = 0;
    static const uint32_t SWR_ATTRIB_SWIZZLE_constantSource        = 1;
    static const uint32_t SWR_ATTRIB_SWIZZLE_componentOverrideMask = 2;

    INLINE static StructType *Gen_SWR_BACKEND_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* constantInterpolationMask  */ members.push_back( Type::getInt32Ty(ctx) );
        /* pointSpriteTexCoordMask    */ members.push_back( Type::getInt32Ty(ctx) );
        /* numAttributes              */ members.push_back( Type::getInt8Ty(ctx) );
        /* numComponents              */ members.push_back( ArrayType::get(Type::getInt8Ty(ctx), 32) );
        /* swizzleEnable              */ members.push_back( Type::getInt8Ty(ctx) );
        /* swizzleMap                 */ members.push_back( ArrayType::get(Gen_SWR_ATTRIB_SWIZZLE(pJitMgr), 32) );
        /* readRenderTargetArrayIndex */ members.push_back( Type::getInt8Ty(ctx) );
        /* readViewportArrayIndex     */ members.push_back( Type::getInt8Ty(ctx) );
        /* vertexAttribOffset         */ members.push_back( Type::getInt32Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_BACKEND_STATE_constantInterpolationMask  = 0;
    static const uint32_t SWR_BACKEND_STATE_pointSpriteTexCoordMask    = 1;
    static const uint32_t SWR_BACKEND_STATE_numAttributes              = 2;
    static const uint32_t SWR_BACKEND_STATE_numComponents              = 3;
    static const uint32_t SWR_BACKEND_STATE_swizzleEnable              = 4;
    static const uint32_t SWR_BACKEND_STATE_swizzleMap                 = 5;
    static const uint32_t SWR_BACKEND_STATE_readRenderTargetArrayIndex = 6;
    static const uint32_t SWR_BACKEND_STATE_readViewportArrayIndex     = 7;
    static const uint32_t SWR_BACKEND_STATE_vertexAttribOffset         = 8;

    INLINE static StructType *Gen_SWR_PS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* pfnPixelShader   */ members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );
        /* killsPixel       */ members.push_back( Type::getInt32Ty(ctx) );
        /* inputCoverage    */ members.push_back( Type::getInt32Ty(ctx) );
        /* writesODepth     */ members.push_back( Type::getInt32Ty(ctx) );
        /* usesSourceDepth  */ members.push_back( Type::getInt32Ty(ctx) );
        /* shadingRate      */ members.push_back( Type::getInt32Ty(ctx) );
        /* posOffset        */ members.push_back( Type::getInt32Ty(ctx) );
        /* barycentricsMask */ members.push_back( Type::getInt32Ty(ctx) );
        /* usesUAV          */ members.push_back( Type::getInt32Ty(ctx) );
        /* forceEarlyZ      */ members.push_back( Type::getInt32Ty(ctx) );
        /* renderTargetMask */ members.push_back( Type::getInt8Ty(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_PS_STATE_pfnPixelShader   = 0;
    static const uint32_t SWR_PS_STATE_killsPixel       = 1;
    static const uint32_t SWR_PS_STATE_inputCoverage    = 2;
    static const uint32_t SWR_PS_STATE_writesODepth     = 3;
    static const uint32_t SWR_PS_STATE_usesSourceDepth  = 4;
    static const uint32_t SWR_PS_STATE_shadingRate      = 5;
    static const uint32_t SWR_PS_STATE_posOffset        = 6;
    static const uint32_t SWR_PS_STATE_barycentricsMask = 7;
    static const uint32_t SWR_PS_STATE_usesUAV          = 8;
    static const uint32_t SWR_PS_STATE_forceEarlyZ      = 9;
    static const uint32_t SWR_PS_STATE_renderTargetMask = 10;

    INLINE static StructType *Gen_SWR_DEPTH_BOUNDS_STATE(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;
        
        /* depthBoundsTestEnable   */ members.push_back( Type::getInt8Ty(ctx) );
        /* depthBoundsTestMinValue */ members.push_back( Type::getFloatTy(ctx) );
        /* depthBoundsTestMaxValue */ members.push_back( Type::getFloatTy(ctx) );

        return StructType::get(ctx, members, false);
    }

    static const uint32_t SWR_DEPTH_BOUNDS_STATE_depthBoundsTestEnable   = 0;
    static const uint32_t SWR_DEPTH_BOUNDS_STATE_depthBoundsTestMinValue = 1;
    static const uint32_t SWR_DEPTH_BOUNDS_STATE_depthBoundsTestMaxValue = 2;

} // ns SwrJit


