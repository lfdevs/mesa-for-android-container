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
* @file JitManager.h
*
* @brief JitManager contains the LLVM data structures used for JIT generation
*
* Notes:
*
******************************************************************************/
#pragma once

#include "common/os.h"
#include "common/isa.hpp"

#if defined(_WIN32)
#pragma warning(disable : 4146 4244 4267 4800 4996)
#endif

// llvm 3.7+ reuses "DEBUG" as an enum value
#pragma push_macro("DEBUG")
#undef DEBUG

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"

#include "llvm/Config/llvm-config.h"
#ifndef LLVM_VERSION_MAJOR
#include "llvm/Config/config.h"
#endif

#ifndef HAVE_LLVM
#define HAVE_LLVM ((LLVM_VERSION_MAJOR << 8) | LLVM_VERSION_MINOR)
#endif

#include "llvm/IR/Verifier.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/Support/FileSystem.h"
#define LLVM_F_NONE sys::fs::F_None

#include "llvm/Analysis/Passes.h"

#if HAVE_LLVM == 0x306
#include "llvm/PassManager.h"
using FunctionPassManager = llvm::FunctionPassManager;
using PassManager = llvm::PassManager;
#else
#include "llvm/IR/LegacyPassManager.h"
using FunctionPassManager = llvm::legacy::FunctionPassManager;
using PassManager = llvm::legacy::PassManager;
#endif

#include "llvm/CodeGen/Passes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/DynamicLibrary.h"


#pragma pop_macro("DEBUG")

//////////////////////////////////////////////////////////////////////////
/// JitInstructionSet
/// @brief Subclass of InstructionSet that allows users to override
/// the reporting of support for certain ISA features.  This allows capping
/// the jitted code to a certain feature level, e.g. jit AVX level code on 
/// a platform that supports AVX2.
//////////////////////////////////////////////////////////////////////////
class JitInstructionSet : public InstructionSet
{
public:
    JitInstructionSet(const char* requestedIsa) : isaRequest(requestedIsa)
    {
        std::transform(isaRequest.begin(), isaRequest.end(), isaRequest.begin(), ::tolower);

        if(isaRequest == "avx")
        {
            bForceAVX = true;
            bForceAVX2 = false;
            bForceAVX512 = false;
        }
        else if(isaRequest == "avx2")
        {
            bForceAVX = false;
            bForceAVX2 = true;
            bForceAVX512 = false;
        }
        #if 0
        else if(isaRequest == "avx512")
        {
            bForceAVX = false;
            bForceAVX2 = false;
            bForceAVX512 = true;
        }
        #endif
    };

    bool AVX2(void) { return bForceAVX ? 0 : InstructionSet::AVX2(); }
    bool AVX512F(void) { return (bForceAVX | bForceAVX2) ? 0 : InstructionSet::AVX512F(); }
    bool BMI2(void) { return bForceAVX ? 0 : InstructionSet::BMI2(); }

private:
    bool bForceAVX = false;
    bool bForceAVX2 = false;
    bool bForceAVX512 = false;
    std::string isaRequest;
};



struct JitLLVMContext : llvm::LLVMContext
{
};


//////////////////////////////////////////////////////////////////////////
/// JitManager
//////////////////////////////////////////////////////////////////////////
struct JitManager
{
    JitManager(uint32_t w, const char* arch, const char* core);
    ~JitManager(){};

    JitLLVMContext          mContext;   ///< LLVM compiler
    llvm::IRBuilder<>       mBuilder;   ///< LLVM IR Builder
    llvm::ExecutionEngine*  mpExec;

    // Need to be rebuilt after a JIT and before building new IR
    llvm::Module* mpCurrentModule;
    bool mIsModuleFinalized;
    uint32_t mJitNumber;

    uint32_t                 mVWidth;

    // Built in types.
    llvm::Type*                mInt8Ty;
    llvm::Type*                mInt32Ty;
    llvm::Type*                mInt64Ty;
    llvm::Type*                mFP32Ty;
    llvm::StructType*          mV4FP32Ty;
    llvm::StructType*          mV4Int32Ty;

    llvm::Type* mSimtFP32Ty;
    llvm::Type* mSimtInt32Ty;

    llvm::Type* mSimdVectorInt32Ty;
    llvm::Type* mSimdVectorTy;

    // fetch shader types
    llvm::FunctionType*        mFetchShaderTy;

    JitInstructionSet mArch;
    std::string mCore;

    void SetupNewModule();
    bool SetupModuleFromIR(const uint8_t *pIR);

    void DumpAsm(llvm::Function* pFunction, const char* fileName);
    static void DumpToFile(llvm::Function *f, const char *fileName);
};
