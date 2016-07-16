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
* @file JitManager.cpp
* 
* @brief Implementation if the Jit Manager.
* 
* Notes:
* 
******************************************************************************/
#if defined(_WIN32)
#pragma warning(disable: 4800 4146 4244 4267 4355 4996)
#endif

#include "jit_api.h"
#include "JitManager.h"
#include "fetch_jit.h"

#if defined(_WIN32)
#include "llvm/ADT/Triple.h"
#endif
#include "llvm/IR/Function.h"
#include "llvm/Support/DynamicLibrary.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/FormattedStream.h"

#if LLVM_USE_INTEL_JITEVENTS
#include "llvm/ExecutionEngine/JITEventListener.h"
#endif

#include "core/state.h"

#include "state_llvm.h"

#include <sstream>
#if defined(_WIN32)
#include <psapi.h>
#include <cstring>

#define INTEL_OUTPUT_DIR "c:\\Intel"
#define SWR_OUTPUT_DIR INTEL_OUTPUT_DIR "\\SWR"
#define JITTER_OUTPUT_DIR SWR_OUTPUT_DIR "\\Jitter"
#endif

using namespace llvm;

//////////////////////////////////////////////////////////////////////////
/// @brief Contructor for JitManager.
/// @param simdWidth - SIMD width to be used in generated program.
JitManager::JitManager(uint32_t simdWidth, const char *arch)
    : mContext(), mBuilder(mContext), mIsModuleFinalized(true), mJitNumber(0), mVWidth(simdWidth), mArch(arch)
{
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetDisassembler();

    TargetOptions    tOpts;
    tOpts.AllowFPOpFusion = FPOpFusion::Fast;
    tOpts.NoInfsFPMath = false;
    tOpts.NoNaNsFPMath = false;
    tOpts.UnsafeFPMath = true;
#if defined(_DEBUG)
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7
    tOpts.NoFramePointerElim = true;
#endif
#endif

    //tOpts.PrintMachineCode    = true;

    std::stringstream fnName("JitModule", std::ios_base::in | std::ios_base::out | std::ios_base::ate);
    fnName << mJitNumber++;
    std::unique_ptr<Module> newModule(new Module(fnName.str(), mContext));
    mpCurrentModule = newModule.get();

    auto &&EB = EngineBuilder(std::move(newModule));
    EB.setTargetOptions(tOpts);
    EB.setOptLevel(CodeGenOpt::Aggressive);

    StringRef hostCPUName;

    // force JIT to use the same CPU arch as the rest of swr
    if(mArch.AVX512F())
    {
        assert(0 && "Implement AVX512 jitter");
        hostCPUName = sys::getHostCPUName();
        if (mVWidth == 0)
        {
            mVWidth = 16;
        }
    }
    else if(mArch.AVX2())
    {
        hostCPUName = StringRef("core-avx2");
        if (mVWidth == 0)
        {
            mVWidth = 8;
        }
    }
    else if(mArch.AVX())
    {
        if (mArch.F16C())
        {
            hostCPUName = StringRef("core-avx-i");
        }
        else
        {
            hostCPUName = StringRef("corei7-avx");
        }
        if (mVWidth == 0)
        {
            mVWidth = 8;
        }
    }
    else
    {
        hostCPUName = sys::getHostCPUName();
        if (mVWidth == 0)
        {
            mVWidth = 8; // 4?
        }
    }

    EB.setMCPU(hostCPUName);

#if defined(_WIN32)
    // Needed for MCJIT on windows
    Triple hostTriple(sys::getProcessTriple());
    hostTriple.setObjectFormat(Triple::ELF);
    mpCurrentModule->setTargetTriple(hostTriple.getTriple());
#endif // _WIN32

    mpExec = EB.create();

#if LLVM_USE_INTEL_JITEVENTS
    JITEventListener *vTune = JITEventListener::createIntelJITEventListener();
    mpExec->RegisterJITEventListener(vTune);
#endif

    mFP32Ty = Type::getFloatTy(mContext);   // float type
    mInt8Ty = Type::getInt8Ty(mContext);
    mInt32Ty = Type::getInt32Ty(mContext);   // int type
    mInt64Ty = Type::getInt64Ty(mContext);   // int type
    mV4FP32Ty = StructType::get(mContext, std::vector<Type*>(4, mFP32Ty), false); // vector4 float type (represented as structure)
    mV4Int32Ty = StructType::get(mContext, std::vector<Type*>(4, mInt32Ty), false); // vector4 int type

    // fetch function signature
    // typedef void(__cdecl *PFN_FETCH_FUNC)(SWR_FETCH_CONTEXT& fetchInfo, simdvertex& out);
    std::vector<Type*> fsArgs;
    fsArgs.push_back(PointerType::get(Gen_SWR_FETCH_CONTEXT(this), 0));
    fsArgs.push_back(PointerType::get(Gen_simdvertex(this), 0));

    mFetchShaderTy = FunctionType::get(Type::getVoidTy(mContext), fsArgs, false);

    mSimtFP32Ty = VectorType::get(mFP32Ty, mVWidth);
    mSimtInt32Ty = VectorType::get(mInt32Ty, mVWidth);

    mSimdVectorTy = StructType::get(mContext, std::vector<Type*>(4, mSimtFP32Ty), false);
    mSimdVectorInt32Ty = StructType::get(mContext, std::vector<Type*>(4, mSimtInt32Ty), false);

#if defined(_WIN32)
    // explicitly instantiate used symbols from potentially staticly linked libs
    sys::DynamicLibrary::AddSymbol("exp2f", &exp2f);
    sys::DynamicLibrary::AddSymbol("log2f", &log2f);
    sys::DynamicLibrary::AddSymbol("sinf", &sinf);
    sys::DynamicLibrary::AddSymbol("cosf", &cosf);
    sys::DynamicLibrary::AddSymbol("powf", &powf);
#endif

#if defined(_WIN32)
    if (KNOB_DUMP_SHADER_IR)
    {
        CreateDirectory(INTEL_OUTPUT_DIR, NULL);
        CreateDirectory(SWR_OUTPUT_DIR, NULL);
        CreateDirectory(JITTER_OUTPUT_DIR, NULL);
    }
#endif
}

//////////////////////////////////////////////////////////////////////////
/// @brief Create new LLVM module.
void JitManager::SetupNewModule()
{
    SWR_ASSERT(mIsModuleFinalized == true && "Current module is not finalized!");
    
    std::stringstream fnName("JitModule", std::ios_base::in | std::ios_base::out | std::ios_base::ate);
    fnName << mJitNumber++;
    std::unique_ptr<Module> newModule(new Module(fnName.str(), mContext));
    mpCurrentModule = newModule.get();
#if defined(_WIN32)
    // Needed for MCJIT on windows
    Triple hostTriple(sys::getProcessTriple());
    hostTriple.setObjectFormat(Triple::ELF);
    newModule->setTargetTriple(hostTriple.getTriple());
#endif // _WIN32

    mpExec->addModule(std::move(newModule));
    mIsModuleFinalized = false;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Create new LLVM module from IR.
bool JitManager::SetupModuleFromIR(const uint8_t *pIR)
{
    std::unique_ptr<MemoryBuffer> pMem = MemoryBuffer::getMemBuffer(StringRef((const char*)pIR), "");

    SMDiagnostic Err;
    std::unique_ptr<Module> newModule = parseIR(pMem.get()->getMemBufferRef(), Err, mContext);

    if (newModule == nullptr)
    {
        SWR_ASSERT(0, "Parse failed! Check Err for details.");
        return false;
    }

    mpCurrentModule = newModule.get();
#if defined(_WIN32)
    // Needed for MCJIT on windows
    Triple hostTriple(sys::getProcessTriple());
    hostTriple.setObjectFormat(Triple::ELF);
    newModule->setTargetTriple(hostTriple.getTriple());
#endif // _WIN32

    mpExec->addModule(std::move(newModule));
    mIsModuleFinalized = false;

    return true;
}


//////////////////////////////////////////////////////////////////////////
/// @brief Dump function x86 assembly to file.
/// @note This should only be called after the module has been jitted to x86 and the
///       module will not be further accessed.
void JitManager::DumpAsm(Function* pFunction, const char* fileName)
{
    if (KNOB_DUMP_SHADER_IR)
    {

#if defined(_WIN32)
        DWORD pid = GetCurrentProcessId();
        TCHAR procname[MAX_PATH];
        GetModuleFileName(NULL, procname, MAX_PATH);
        const char* pBaseName = strrchr(procname, '\\');
        std::stringstream outDir;
        outDir << JITTER_OUTPUT_DIR << pBaseName << "_" << pid << std::ends;
        CreateDirectory(outDir.str().c_str(), NULL);
#endif

        std::error_code EC;
        Module* pModule = pFunction->getParent();
        const char *funcName = pFunction->getName().data();
        char fName[256];
#if defined(_WIN32)
        sprintf(fName, "%s\\%s.%s.asm", outDir.str().c_str(), funcName, fileName);
#else
        sprintf(fName, "%s.%s.asm", funcName, fileName);
#endif

#if HAVE_LLVM == 0x306
        raw_fd_ostream fd(fName, EC, llvm::sys::fs::F_None);
        formatted_raw_ostream filestream(fd);
#else
        raw_fd_ostream filestream(fName, EC, llvm::sys::fs::F_None);
#endif

        legacy::PassManager* pMPasses = new legacy::PassManager();
        auto* pTarget = mpExec->getTargetMachine();
        pTarget->Options.MCOptions.AsmVerbose = true;
        pTarget->addPassesToEmitFile(*pMPasses, filestream, TargetMachine::CGFT_AssemblyFile);
        pMPasses->run(*pModule);
        delete pMPasses;
        pTarget->Options.MCOptions.AsmVerbose = false;
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Dump function to file.
void JitManager::DumpToFile(Function *f, const char *fileName)
{
    if (KNOB_DUMP_SHADER_IR)
    {
#if defined(_WIN32)
        DWORD pid = GetCurrentProcessId();
        TCHAR procname[MAX_PATH];
        GetModuleFileName(NULL, procname, MAX_PATH);
        const char* pBaseName = strrchr(procname, '\\');
        std::stringstream outDir;
        outDir << JITTER_OUTPUT_DIR << pBaseName << "_" << pid << std::ends;
        CreateDirectory(outDir.str().c_str(), NULL);
#endif

        std::error_code EC;
        const char *funcName = f->getName().data();
        char fName[256];
#if defined(_WIN32)
        sprintf(fName, "%s\\%s.%s.ll", outDir.str().c_str(), funcName, fileName);
#else
        sprintf(fName, "%s.%s.ll", funcName, fileName);
#endif
        raw_fd_ostream fd(fName, EC, llvm::sys::fs::F_None);
        Module* pModule = f->getParent();
        pModule->print(fd, nullptr);

#if defined(_WIN32)
        sprintf(fName, "%s\\cfg.%s.%s.dot", outDir.str().c_str(), funcName, fileName);
#else
        sprintf(fName, "cfg.%s.%s.dot", funcName, fileName);
#endif
        fd.flush();

        raw_fd_ostream fd_cfg(fName, EC, llvm::sys::fs::F_Text);
        WriteGraph(fd_cfg, (const Function*)f);

        fd_cfg.flush();
    }
}

extern "C"
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Create JIT context.
    /// @param simdWidth - SIMD width to be used in generated program.
    HANDLE JITCALL JitCreateContext(uint32_t targetSimdWidth, const char* arch)
    {
        return new JitManager(targetSimdWidth, arch);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Destroy JIT context.
    void JITCALL JitDestroyContext(HANDLE hJitContext)
    {
        delete reinterpret_cast<JitManager*>(hJitContext);
    }
}
