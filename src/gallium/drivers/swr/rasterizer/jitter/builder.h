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
* @file builder.h
* 
* @brief Includes all the builder related functionality
* 
* Notes:
* 
******************************************************************************/
#pragma once

#include "JitManager.h"
#include "common/formats.h"

namespace SwrJit
{
    using namespace llvm;
    struct Builder
    {
        Builder(JitManager *pJitMgr);
        IRBuilder<>* IRB() { return mpIRBuilder; };
        JitManager* JM() { return mpJitMgr; }

        JitManager* mpJitMgr;
        IRBuilder<>* mpIRBuilder;

        uint32_t             mVWidth;

        // Built in types.
        Type*                mVoidTy;
        Type*                mInt1Ty;
        Type*                mInt8Ty;
        Type*                mInt16Ty;
        Type*                mInt32Ty;
        Type*                mInt64Ty;
        Type*                mIntPtrTy;
        Type*                mFP16Ty;
        Type*                mFP32Ty;
        Type*                mDoubleTy;
        Type*                mInt8PtrTy;
        Type*                mInt16PtrTy;
        Type*                mInt32PtrTy;
        Type*                mSimdFP16Ty;
        Type*                mSimdFP32Ty;
        Type*                mSimdInt1Ty;
        Type*                mSimdInt16Ty;
        Type*                mSimdInt32Ty;
        Type*                mSimdInt64Ty;
        Type*                mSimdIntPtrTy;
        Type*                mSimdVectorTy;
        Type*                mSimdVectorTRTy;
        StructType*          mV4FP32Ty;
        StructType*          mV4Int32Ty;

#include "builder_gen.h"
#include "builder_x86.h"
#include "builder_misc.h"
#include "builder_math.h"
    };
}
