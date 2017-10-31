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

#pragma push_macro("DEBUG")
#undef DEBUG

#if defined(_WIN32)
#include "llvm/ADT/Triple.h"
#endif
#include "llvm/IR/Function.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Config/llvm-config.h"

#if LLVM_VERSION_MAJOR < 4
#include "llvm/Bitcode/ReaderWriter.h"
#else
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeReader.h"
#endif

#if LLVM_USE_INTEL_JITEVENTS
#include "llvm/ExecutionEngine/JITEventListener.h"
#endif

#pragma pop_macro("DEBUG")

#include "JitManager.h"
#include "jit_api.h"
#include "fetch_jit.h"

#include "core/state.h"

#include "gen_state_llvm.h"

#include <sstream>
#if defined(_WIN32)
#include <psapi.h>
#include <cstring>

#define INTEL_OUTPUT_DIR "c:\\Intel"
#define SWR_OUTPUT_DIR INTEL_OUTPUT_DIR "\\SWR"
#define JITTER_OUTPUT_DIR SWR_OUTPUT_DIR "\\Jitter"
#endif // _WIN32

#if defined(__APPLE) || defined(FORCE_LINUX) || defined(__linux__) || defined(__gnu_linux__)
#include <pwd.h>
#include <sys/stat.h>
#endif


using namespace llvm;
using namespace SwrJit;

//////////////////////////////////////////////////////////////////////////
/// @brief Contructor for JitManager.
/// @param simdWidth - SIMD width to be used in generated program.
JitManager::JitManager(uint32_t simdWidth, const char *arch, const char* core)
    : mContext(), mBuilder(mContext), mIsModuleFinalized(true), mJitNumber(0), mVWidth(simdWidth), mArch(arch)
{
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetDisassembler();

    TargetOptions    tOpts;
    tOpts.AllowFPOpFusion = FPOpFusion::Fast;
    tOpts.NoInfsFPMath = false;
    tOpts.NoNaNsFPMath = false;
    tOpts.UnsafeFPMath = false;
#if defined(_DEBUG)
#if LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 7
    tOpts.NoFramePointerElim = true;
#endif
#endif

    //tOpts.PrintMachineCode    = true;

    mCore = std::string(core);
    std::transform(mCore.begin(), mCore.end(), mCore.begin(), ::tolower);

    std::unique_ptr<Module> newModule(new Module("", mContext));
    mpCurrentModule = newModule.get();

    StringRef hostCPUName;

    hostCPUName = sys::getHostCPUName();

#if defined(_WIN32)
    // Needed for MCJIT on windows
    Triple hostTriple(sys::getProcessTriple());
    hostTriple.setObjectFormat(Triple::ELF);
    mpCurrentModule->setTargetTriple(hostTriple.getTriple());
#endif // _WIN32

    mpExec = EngineBuilder(std::move(newModule))
        .setTargetOptions(tOpts)
        .setOptLevel(CodeGenOpt::Aggressive)
        .setMCPU(hostCPUName)
        .create();

    if (KNOB_JIT_ENABLE_CACHE)
    {
        mCache.SetCpu(hostCPUName);
        mpExec->setObjectCache(&mCache);
    }

#if LLVM_USE_INTEL_JITEVENTS
    JITEventListener *vTune = JITEventListener::createIntelJITEventListener();
    mpExec->RegisterJITEventListener(vTune);
#endif

    mFP32Ty = Type::getFloatTy(mContext);   // float type
    mInt8Ty = Type::getInt8Ty(mContext);
    mInt32Ty = Type::getInt32Ty(mContext);   // int type
    mInt64Ty = Type::getInt64Ty(mContext);   // int type

    // fetch function signature
#if USE_SIMD16_SHADERS
    // typedef void(__cdecl *PFN_FETCH_FUNC)(SWR_FETCH_CONTEXT& fetchInfo, simd16vertex& out);
#else
    // typedef void(__cdecl *PFN_FETCH_FUNC)(SWR_FETCH_CONTEXT& fetchInfo, simdvertex& out);
#endif
    std::vector<Type*> fsArgs;
    fsArgs.push_back(PointerType::get(Gen_SWR_FETCH_CONTEXT(this), 0));
#if USE_SIMD16_SHADERS
    fsArgs.push_back(PointerType::get(Gen_simd16vertex(this), 0));
#else
    fsArgs.push_back(PointerType::get(Gen_simdvertex(this), 0));
#endif

    mFetchShaderTy = FunctionType::get(Type::getVoidTy(mContext), fsArgs, false);

    mSimtFP32Ty = VectorType::get(mFP32Ty, mVWidth);
    mSimtInt32Ty = VectorType::get(mInt32Ty, mVWidth);

    mSimdVectorTy = ArrayType::get(mSimtFP32Ty, 4);
    mSimdVectorInt32Ty = ArrayType::get(mSimtInt32Ty, 4);

#if USE_SIMD16_SHADERS
    mSimd16FP32Ty = ArrayType::get(mSimtFP32Ty, 2);
    mSimd16Int32Ty = ArrayType::get(mSimtInt32Ty, 2);

    mSimd16VectorFP32Ty = ArrayType::get(mSimd16FP32Ty, 4);
    mSimd16VectorInt32Ty = ArrayType::get(mSimd16Int32Ty, 4);

#endif
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
        CreateDirectoryPath(INTEL_OUTPUT_DIR);
        CreateDirectoryPath(SWR_OUTPUT_DIR);
        CreateDirectoryPath(JITTER_OUTPUT_DIR);
    }
#endif
}

//////////////////////////////////////////////////////////////////////////
/// @brief Create new LLVM module.
void JitManager::SetupNewModule()
{
    SWR_ASSERT(mIsModuleFinalized == true && "Current module is not finalized!");
    
    std::unique_ptr<Module> newModule(new Module("", mContext));
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
/// @brief Dump function x86 assembly to file.
/// @note This should only be called after the module has been jitted to x86 and the
///       module will not be further accessed.
void JitManager::DumpAsm(Function* pFunction, const char* fileName)
{
    if (KNOB_DUMP_SHADER_IR)
    {

#if defined(_WIN32)
        DWORD pid = GetCurrentProcessId();
        char procname[MAX_PATH];
        GetModuleFileNameA(NULL, procname, MAX_PATH);
        const char* pBaseName = strrchr(procname, '\\');
        std::stringstream outDir;
        outDir << JITTER_OUTPUT_DIR << pBaseName << "_" << pid << std::ends;
        CreateDirectoryPath(outDir.str().c_str());
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

        raw_fd_ostream filestream(fName, EC, llvm::sys::fs::F_None);

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
        char procname[MAX_PATH];
        GetModuleFileNameA(NULL, procname, MAX_PATH);
        const char* pBaseName = strrchr(procname, '\\');
        std::stringstream outDir;
        outDir << JITTER_OUTPUT_DIR << pBaseName << "_" << pid << std::ends;
        CreateDirectoryPath(outDir.str().c_str());
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
    bool g_DllActive = true;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Create JIT context.
    /// @param simdWidth - SIMD width to be used in generated program.
    HANDLE JITCALL JitCreateContext(uint32_t targetSimdWidth, const char* arch, const char* core)
    {
        return new JitManager(targetSimdWidth, arch, core);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Destroy JIT context.
    void JITCALL JitDestroyContext(HANDLE hJitContext)
    {
        if (g_DllActive)
        {
            delete reinterpret_cast<JitManager*>(hJitContext);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// JitCache
//////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
/// JitCacheFileHeader
//////////////////////////////////////////////////////////////////////////
struct JitCacheFileHeader
{
    void Init(uint32_t llCRC, uint32_t objCRC, const std::string& moduleID, const std::string& cpu, uint64_t bufferSize)
    {
        m_MagicNumber = JC_MAGIC_NUMBER;
        m_BufferSize = bufferSize;
        m_llCRC = llCRC;
        m_platformKey = JC_PLATFORM_KEY;
        m_objCRC = objCRC;
        strncpy(m_ModuleID, moduleID.c_str(), JC_STR_MAX_LEN - 1);
        m_ModuleID[JC_STR_MAX_LEN - 1] = 0;
        strncpy(m_Cpu, cpu.c_str(), JC_STR_MAX_LEN - 1);
        m_Cpu[JC_STR_MAX_LEN - 1] = 0;
    }

    bool IsValid(uint32_t llCRC, const std::string& moduleID, const std::string& cpu)
    {
        if ((m_MagicNumber != JC_MAGIC_NUMBER) ||
            (m_llCRC != llCRC) ||
            (m_platformKey != JC_PLATFORM_KEY))
        {
            return false;
        }

        m_ModuleID[JC_STR_MAX_LEN - 1] = 0;
        if (strncmp(moduleID.c_str(), m_ModuleID, JC_STR_MAX_LEN - 1))
        {
            return false;
        }

        m_Cpu[JC_STR_MAX_LEN - 1] = 0;
        if (strncmp(cpu.c_str(), m_Cpu, JC_STR_MAX_LEN - 1))
        {
            return false;
        }

        return true;
    }

    uint64_t GetBufferSize() const { return m_BufferSize; }
    uint64_t GetBufferCRC() const { return m_objCRC; }

private:
    static const uint64_t   JC_MAGIC_NUMBER = 0xfedcba9876543211ULL;
    static const size_t     JC_STR_MAX_LEN = 32;
    static const uint32_t   JC_PLATFORM_KEY =
        (LLVM_VERSION_MAJOR << 24)  |
        (LLVM_VERSION_MINOR << 16)  |
        (LLVM_VERSION_PATCH << 8)   |
        ((sizeof(void*) > sizeof(uint32_t)) ? 1 : 0);

    uint64_t m_MagicNumber;
    uint64_t m_BufferSize;
    uint32_t m_llCRC;
    uint32_t m_platformKey;
    uint32_t m_objCRC;
    char m_ModuleID[JC_STR_MAX_LEN];
    char m_Cpu[JC_STR_MAX_LEN];
};

static inline uint32_t ComputeModuleCRC(const llvm::Module* M)
{
    std::string bitcodeBuffer;
    raw_string_ostream bitcodeStream(bitcodeBuffer);

    llvm::WriteBitcodeToFile(M, bitcodeStream);
    //M->print(bitcodeStream, nullptr, false);

    bitcodeStream.flush();

    return ComputeCRC(0, bitcodeBuffer.data(), bitcodeBuffer.size());
}

/// constructor
JitCache::JitCache()
{
#if defined(__APPLE) || defined(FORCE_LINUX) || defined(__linux__) || defined(__gnu_linux__)
    if (strncmp(KNOB_JIT_CACHE_DIR.c_str(), "~/", 2) == 0) {
        char *homedir;
        if (!(homedir = getenv("HOME"))) {
            homedir = getpwuid(getuid())->pw_dir;
        }
        mCacheDir = homedir;
        mCacheDir += (KNOB_JIT_CACHE_DIR.c_str() + 1);
    } else
#endif
    {
        mCacheDir = KNOB_JIT_CACHE_DIR;
    }
}

/// notifyObjectCompiled - Provides a pointer to compiled code for Module M.
void JitCache::notifyObjectCompiled(const llvm::Module *M, llvm::MemoryBufferRef Obj)
{
    const std::string& moduleID = M->getModuleIdentifier();
    if (!moduleID.length())
    {
        return;
    }

    if (!llvm::sys::fs::exists(mCacheDir.str()) &&
        llvm::sys::fs::create_directories(mCacheDir.str()))
    {
        SWR_INVALID("Unable to create directory: %s", mCacheDir.c_str());
        return;
    }

    llvm::SmallString<MAX_PATH> filePath = mCacheDir;
    llvm::sys::path::append(filePath, moduleID);

    std::error_code err;
    llvm::raw_fd_ostream fileObj(filePath.c_str(), err, llvm::sys::fs::F_None);

    uint32_t objcrc = ComputeCRC(0, Obj.getBufferStart(), Obj.getBufferSize());

    JitCacheFileHeader header;
    header.Init(mCurrentModuleCRC, objcrc, moduleID, mCpu, Obj.getBufferSize());

    fileObj.write((const char*)&header, sizeof(header));
    fileObj << Obj.getBuffer();
    fileObj.flush();
}

/// Returns a pointer to a newly allocated MemoryBuffer that contains the
/// object which corresponds with Module M, or 0 if an object is not
/// available.
std::unique_ptr<llvm::MemoryBuffer> JitCache::getObject(const llvm::Module* M)
{
    const std::string& moduleID = M->getModuleIdentifier();
    mCurrentModuleCRC = ComputeModuleCRC(M);

    if (!moduleID.length())
    {
        return nullptr;
    }

    if (!llvm::sys::fs::exists(mCacheDir))
    {
        return nullptr;
    }

    llvm::SmallString<MAX_PATH> filePath = mCacheDir;
    llvm::sys::path::append(filePath, moduleID);

    FILE* fpIn = fopen(filePath.c_str(), "rb");
    if (!fpIn)
    {
        return nullptr;
    }

    std::unique_ptr<llvm::MemoryBuffer> pBuf = nullptr;
    do
    {
        JitCacheFileHeader header;
        if (!fread(&header, sizeof(header), 1, fpIn))
        {
            break;
        }

        if (!header.IsValid(mCurrentModuleCRC, moduleID, mCpu))
        {
            break;
        }

        pBuf = llvm::MemoryBuffer::getNewUninitMemBuffer(size_t(header.GetBufferSize()));
        if (!fread(const_cast<char*>(pBuf->getBufferStart()), header.GetBufferSize(), 1, fpIn))
        {
            pBuf = nullptr;
            break;
        }

        if (header.GetBufferCRC() != ComputeCRC(0, pBuf->getBufferStart(), pBuf->getBufferSize()))
        {
            SWR_TRACE("Invalid object cache file, ignoring: %s", filePath.c_str());
            pBuf = nullptr;
            break;
        }
    } while (0);

    fclose(fpIn);

    return pBuf;
}
