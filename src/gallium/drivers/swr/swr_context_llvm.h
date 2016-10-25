
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
* @file swr_context_llvm.h
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
    /// Generate LLVM type information for swr_jit_texture
    INLINE static StructType *Gen_swr_jit_texture(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getInt32Ty(ctx) );    // width
        members.push_back( Type::getInt32Ty(ctx) );    // height
        members.push_back( Type::getInt32Ty(ctx) );    // depth
        members.push_back( Type::getInt32Ty(ctx) );    // first_level
        members.push_back( Type::getInt32Ty(ctx) );    // last_level
        members.push_back( PointerType::get(Type::getInt8Ty(ctx), 0) );    // base_ptr
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), PIPE_MAX_TEXTURE_LEVELS) );    // row_stride
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), PIPE_MAX_TEXTURE_LEVELS) );    // img_stride
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), PIPE_MAX_TEXTURE_LEVELS) );    // mip_offsets

        return StructType::get(ctx, members, false);
    }

    static const uint32_t swr_jit_texture_width = 0;
    static const uint32_t swr_jit_texture_height = 1;
    static const uint32_t swr_jit_texture_depth = 2;
    static const uint32_t swr_jit_texture_first_level = 3;
    static const uint32_t swr_jit_texture_last_level = 4;
    static const uint32_t swr_jit_texture_base_ptr = 5;
    static const uint32_t swr_jit_texture_row_stride = 6;
    static const uint32_t swr_jit_texture_img_stride = 7;
    static const uint32_t swr_jit_texture_mip_offsets = 8;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for swr_jit_sampler
    INLINE static StructType *Gen_swr_jit_sampler(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( Type::getFloatTy(ctx) );    // min_lod
        members.push_back( Type::getFloatTy(ctx) );    // max_lod
        members.push_back( Type::getFloatTy(ctx) );    // lod_bias
        members.push_back( ArrayType::get(Type::getFloatTy(ctx), 4) );    // border_color

        return StructType::get(ctx, members, false);
    }

    static const uint32_t swr_jit_sampler_min_lod = 0;
    static const uint32_t swr_jit_sampler_max_lod = 1;
    static const uint32_t swr_jit_sampler_lod_bias = 2;
    static const uint32_t swr_jit_sampler_border_color = 3;

    //////////////////////////////////////////////////////////////////////////
    /// Generate LLVM type information for swr_draw_context
    INLINE static StructType *Gen_swr_draw_context(JitManager* pJitMgr)
    {
        LLVMContext& ctx = pJitMgr->mContext;
        std::vector<Type*> members;

        members.push_back( ArrayType::get(PointerType::get(Type::getFloatTy(ctx), 0), PIPE_MAX_CONSTANT_BUFFERS) );    // constantVS
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), PIPE_MAX_CONSTANT_BUFFERS) );    // num_constantsVS
        members.push_back( ArrayType::get(PointerType::get(Type::getFloatTy(ctx), 0), PIPE_MAX_CONSTANT_BUFFERS) );    // constantFS
        members.push_back( ArrayType::get(Type::getInt32Ty(ctx), PIPE_MAX_CONSTANT_BUFFERS) );    // num_constantsFS
        members.push_back( ArrayType::get(Gen_swr_jit_texture(pJitMgr), PIPE_MAX_SHADER_SAMPLER_VIEWS) );    // texturesVS
        members.push_back( ArrayType::get(Gen_swr_jit_sampler(pJitMgr), PIPE_MAX_SAMPLERS) );    // samplersVS
        members.push_back( ArrayType::get(Gen_swr_jit_texture(pJitMgr), PIPE_MAX_SHADER_SAMPLER_VIEWS) );    // texturesFS
        members.push_back( ArrayType::get(Gen_swr_jit_sampler(pJitMgr), PIPE_MAX_SAMPLERS) );    // samplersFS
        members.push_back( ArrayType::get(ArrayType::get(Type::getFloatTy(ctx), 4), PIPE_MAX_CLIP_PLANES) );    // userClipPlanes
        members.push_back( ArrayType::get(Gen_SWR_SURFACE_STATE(pJitMgr), SWR_NUM_ATTACHMENTS) );    // renderTargets
        members.push_back( PointerType::get(Type::getInt32Ty(ctx), 0) );    // pStats

        return StructType::get(ctx, members, false);
    }

    static const uint32_t swr_draw_context_constantVS = 0;
    static const uint32_t swr_draw_context_num_constantsVS = 1;
    static const uint32_t swr_draw_context_constantFS = 2;
    static const uint32_t swr_draw_context_num_constantsFS = 3;
    static const uint32_t swr_draw_context_texturesVS = 4;
    static const uint32_t swr_draw_context_samplersVS = 5;
    static const uint32_t swr_draw_context_texturesFS = 6;
    static const uint32_t swr_draw_context_samplersFS = 7;
    static const uint32_t swr_draw_context_userClipPlanes = 8;
    static const uint32_t swr_draw_context_renderTargets = 9;
    static const uint32_t swr_draw_context_pStats = 10;

}
