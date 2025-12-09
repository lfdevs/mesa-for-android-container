/*
 * Copyright (c) 2022 Alex Fan <alex.fan.q@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "util/detect.h"
#include "util/u_cpu_detect.h"
#include "util/u_debug.h"
#include "util/os_time.h"
#include <string>
#include <vector>
#include <mutex>
#include <cstdlib>
#include "lp_bld.h"
#include "lp_bld_debug.h"
#include "lp_bld_init.h"
#include "lp_bld_coro.h"
#include "lp_bld_misc.h"
#include "lp_bld_printf.h"
#include "lp_bld_passmgr.h"
#include "lp_bld_type.h"

#include <llvm/Config/llvm-config.h>
#include <llvm-c/Core.h>
#include <llvm-c/Orc.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Support.h>

#include <llvm-c/Analysis.h>
#if LLVM_VERSION_MAJOR < 17
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/Transforms/Utils.h>
#endif
#include <llvm-c/BitWriter.h>

#include <llvm/ADT/StringMap.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/ObjectCache.h>
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Casting.h>
#if LLVM_VERSION_MAJOR >= 18
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif
#include <llvm/Support/CBindingWrapping.h>
#if LLVM_USE_INTEL_JITEVENTS
#include <llvm/ExecutionEngine/JITEventListener.h>
#endif

/* conflict with ObjectLinkingLayer.h */
#include "util/u_memory.h"

#if DETECT_ARCH_RISCV64 == 1 || DETECT_ARCH_RISCV32 == 1 || DETECT_ARCH_LOONGARCH64 == 1 || (defined(_WIN32) && LLVM_VERSION_MAJOR >= 15)
/* use ObjectLinkingLayer (JITLINK backend) */
#define USE_JITLINK
#endif
/* else use old RTDyldObjectLinkingLayer (RuntimeDyld backend) */

namespace {

class LPObjectCacheORC : public llvm::ObjectCache {
private:
   bool has_object;
   std::string mid;
   struct lp_cached_code *cache_out;
public:
   LPObjectCacheORC(struct lp_cached_code *cache) {
      cache_out = cache;
      has_object = false;
   }

   ~LPObjectCacheORC() {
   }
   void notifyObjectCompiled(const llvm::Module *M, llvm::MemoryBufferRef Obj) override {
      const std::string ModuleID = M->getModuleIdentifier();
      if (has_object)
         fprintf(stderr, "CACHE ALREADY HAS MODULE OBJECT\n");
      if (mid == ModuleID)
         fprintf(stderr, "CACHING ANOTHER MODULE\n");
      has_object = true;
      mid = ModuleID;
      cache_out->data_size = Obj.getBufferSize();
      cache_out->data = malloc(cache_out->data_size);
      memcpy(cache_out->data, Obj.getBufferStart(), cache_out->data_size);
   }

   std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module *M) override {
      const std::string ModuleID = M->getModuleIdentifier();
      if (cache_out->data_size)
         return llvm::MemoryBuffer::getMemBuffer(llvm::StringRef((const char *)cache_out->data, cache_out->data_size), "", false);
      return NULL;
   }

};

class LPJit;

void lpjit_exit();

class LLVMEnsureMultithreaded {
public:
   LLVMEnsureMultithreaded()
   {
      if (!LLVMIsMultithreaded()) {
         LLVMStartMultithreaded();
      }
   }
};

LLVMEnsureMultithreaded lLVMEnsureMultithreaded;

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(llvm::orc::ThreadSafeContext,
                                   LLVMOrcThreadSafeContextRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(llvm::orc::IRTransformLayer,
                                   LLVMOrcIRTransformLayerRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(llvm::orc::JITDylib, LLVMOrcJITDylibRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(llvm::orc::JITTargetMachineBuilder,
                                   LLVMOrcJITTargetMachineBuilderRef)
LLVMTargetMachineRef wrap(const llvm::TargetMachine *P) {
   return reinterpret_cast<LLVMTargetMachineRef>(const_cast<llvm::TargetMachine*>(P));
}

llvm::ExitOnError ExitOnErr;

inline const char* get_module_name(LLVMModuleRef mod) {
   using llvm::Module;
   return llvm::unwrap(mod)->getModuleIdentifier().c_str();
}

once_flag init_lpjit_once_flag = ONCE_FLAG_INIT;

/* A JIT singleton built upon LLJIT */
class LPJit
{
public:
   static LPJit* get_instance() {
      call_once(&init_lpjit_once_flag, init_lpjit);
      return jit;
   }

   gallivm_state *find_gallivm_state(LLVMModuleRef mod) {
#if DEBUG
      using llvm::Module;
      auto I = gallivm_modules.find(llvm::unwrap(mod)->getModuleIdentifier());
      if (I == gallivm_modules.end()) {
         debug_printf("No gallivm state found for module: %s", get_module_name(mod));
         return NULL;
      }
      return I->second;
#endif
      return NULL;
   }

   static char *get_unique_name(const char* name) {
      LPJit* jit = get_instance();
      size_t size = name == NULL? 16: strlen(name) + 16;
      char *name_uniq = (char *)MALLOC(size);
      if (!name_uniq) {
         return NULL;
      }
      do {
         snprintf(name_uniq, size, "%s_%u", name, jit->jit_dylib_count++);
      } while(jit->lljit->getExecutionSession().getJITDylibByName(name_uniq));
      return name_uniq;
   }

   static LLVMOrcJITDylibRef create_jit_dylib(const char * name) {
      using llvm::orc::JITDylib;
      LPJit* jit = get_instance();
      JITDylib& tmp = ExitOnErr(jit->lljit->createJITDylib(name));
      return wrap(&tmp);
   }

   static void register_gallivm_state(gallivm_state *gallivm) {
#if DEBUG
      LPJit* jit = get_instance();
      jit->gallivm_modules[gallivm->module_name] = gallivm;
#endif
   }

   static void deregister_gallivm_state(gallivm_state *gallivm) {
#if DEBUG
      LPJit* jit = get_instance();
      (void)jit->gallivm_modules.erase(gallivm->module_name);
#endif
   }

   static void add_ir_module_to_jd(
         LLVMOrcThreadSafeContextRef ts_context,
         LLVMModuleRef mod,
         LLVMOrcJITDylibRef jd) {
      using llvm::Module;
      using llvm::orc::ThreadSafeModule;
      using llvm::orc::JITDylib;
      ThreadSafeModule tsm(
         std::unique_ptr<Module>(llvm::unwrap(mod)), *::unwrap(ts_context));
      ExitOnErr(get_instance()->lljit->addIRModule(
         *::unwrap(jd), std::move(tsm)
      ));
   }

   static void add_mapping_to_jd(
         LLVMValueRef sym,
         void *addr,
         LLVMOrcJITDylibRef jd) {
#if LLVM_VERSION_MAJOR >= 17
      using llvm::orc::ExecutorAddr;
      using llvm::orc::ExecutorSymbolDef;
      using llvm::JITSymbolFlags;
#else
      using llvm::JITEvaluatedSymbol;
#endif
      using llvm::orc::ExecutionSession;
      using llvm::orc::JITDylib;
      using llvm::orc::SymbolMap;
      JITDylib* JD = ::unwrap(jd);
      auto& es = LPJit::get_instance()->lljit->getExecutionSession();
      auto name = es.intern(llvm::unwrap(sym)->getName());
      SymbolMap map(1);
#if LLVM_VERSION_MAJOR >= 17
      map[name] = ExecutorSymbolDef(ExecutorAddr::fromPtr(addr), JITSymbolFlags::Exported);
#else
      map[name] = JITEvaluatedSymbol::fromPointer(addr);
#endif
      auto munit = llvm::orc::absoluteSymbols(map);
      llvm::cantFail(JD->define(std::move(munit)));
   }

   static void *lookup_in_jd(
         const char *func_name,
         LLVMOrcJITDylibRef jd,
         llvm::ObjectCache *objcache) {
      using llvm::orc::JITDylib;
      using llvm::JITEvaluatedSymbol;
      using llvm::orc::ExecutorAddr;
      JITDylib* JD = ::unwrap(jd);
      LPJit* jit = get_instance();
      auto &ircl = jit->lljit->getIRCompileLayer();
      auto &irc = ircl.getCompiler();
      auto &sc = dynamic_cast<llvm::orc::SimpleCompiler &>(irc);
      jit->lookup_mutex.lock();
      sc.setObjectCache(objcache);
      auto func = ExitOnErr(jit->lljit->lookup(*JD, func_name));
      jit->lookup_mutex.unlock();
#if LLVM_VERSION_MAJOR >= 15
      return func.toPtr<void *>();
#else
      return (void *)(func.getAddress());
#endif
   }

   static void remove_jd(LLVMOrcJITDylibRef jd) {
      using llvm::orc::ExecutionSession;
      using llvm::orc::JITDylib;
      auto& es = LPJit::get_instance()->lljit->getExecutionSession();
      ExitOnErr(es.removeJITDylib(* ::unwrap(jd)));
   }

   LLVMTargetMachineRef tm;

private:
   LPJit();
   ~LPJit() = default;
   LPJit(const LPJit&) = delete;
   LPJit& operator=(const LPJit&) = delete;

   friend void lpjit_exit();

   static void init_native_targets();
   llvm::orc::JITTargetMachineBuilder create_jtdb();

   static void init_lpjit() {
      jit = new LPJit;
      std::atexit(lpjit_exit);
   }
   static LPJit* jit;

   std::unique_ptr<llvm::orc::LLJIT> lljit;
   std::unique_ptr<llvm::TargetMachine> tm_unique;
   /* avoid name conflict */
   unsigned jit_dylib_count;

   std::mutex lookup_mutex;

#if DEBUG
   /* map from module name to gallivm_state */
   llvm::StringMap<gallivm_state *> gallivm_modules;
#endif
};

LPJit* LPJit::jit = NULL;

void lpjit_exit()
{
   delete LPJit::jit;
}

LLVMErrorRef module_transform(void *Ctx, LLVMModuleRef mod) {
   struct lp_passmgr *mgr;

   lp_passmgr_create(mod, &mgr);

   lp_passmgr_run(mgr, mod,
                  LPJit::get_instance()->tm,
                  get_module_name(mod));

   lp_passmgr_dispose(mgr);
   return LLVMErrorSuccess;
}

LLVMErrorRef module_transform_wrapper(
      void *Ctx, LLVMOrcThreadSafeModuleRef *ModInOut,
      LLVMOrcMaterializationResponsibilityRef MR) {
   return LLVMOrcThreadSafeModuleWithModuleDo(*ModInOut, *module_transform, Ctx);
}

LPJit::LPJit() :jit_dylib_count(0) {
   using namespace llvm::orc;

   lp_init_env_options();

   init_native_targets();
   JITTargetMachineBuilder JTMB = create_jtdb();
   tm_unique = ExitOnErr(JTMB.createTargetMachine());
   tm = wrap(tm_unique.get());

   /* Create an LLJIT instance with an ObjectLinkingLayer (JITLINK)
    * or RuntimeDyld as the base layer.
    * intel & perf listeners are not supported by ObjectLinkingLayer yet
    */
   lljit = ExitOnErr(
      LLJITBuilder()
         .setJITTargetMachineBuilder(std::move(JTMB))
#ifdef USE_JITLINK
         .setObjectLinkingLayerCreator(
#if LLVM_VERSION_MAJOR >= 21
            /* LLVM 21 removed the Triple argument */
            [&](ExecutionSession &ES) {
#else
            [&](ExecutionSession &ES, const llvm::Triple &TT) {
#endif
               return std::make_unique<ObjectLinkingLayer>(
                  ES, ExitOnErr(llvm::jitlink::InProcessMemoryManager::Create()));
            })
#else
#if LLVM_USE_INTEL_JITEVENTS
         .RegisterJITEventListener(
               llvm::JITEventListener::createIntelJITEventListener())
#endif
#endif
         .create());

   LLVMOrcIRTransformLayerRef TL = wrap(&lljit->getIRTransformLayer());
   LLVMOrcIRTransformLayerSetTransform(TL, *module_transform_wrapper, NULL);
}

void LPJit::init_native_targets() {

   lp_bld_init_native_targets();

   lp_build_init_native_width();

   lp_bld_ppc_disable_denorms();
}

llvm::orc::JITTargetMachineBuilder LPJit::create_jtdb() {
   using namespace llvm;
   using orc::JITTargetMachineBuilder;

#if defined(_WIN32) && LLVM_VERSION_MAJOR < 15
   /*
    * JITLink works on Windows, but only through ELF object format.
    *
    * XXX: We could use `LLVM_HOST_TRIPLE "-elf"` but LLVM_HOST_TRIPLE has
    * different strings for MinGW/MSVC, so better play it safe and be
    * explicit.
    */
#  ifdef _WIN64
   JITTargetMachineBuilder JTMB((Triple("x86_64-pc-win32-elf")));
#  else
   JITTargetMachineBuilder JTMB((Triple("i686-pc-win32-elf")));
#  endif
#else
   /*
    * llvm::sys::getProcessTriple() is bogus. It returns the host LLVM was
    * compiled on. Be careful when doing cross compilation
    */
   JITTargetMachineBuilder JTMB((Triple(sys::getProcessTriple())));
#endif

   TargetOptions options;
   /**
    * LLVM 3.1+ haven't more "extern unsigned llvm::StackAlignmentOverride" and
    * friends for configuring code generation options, like stack alignment.
    */
#if DETECT_ARCH_X86 == 1 && LLVM_VERSION_MAJOR < 13
   options.StackAlignmentOverride = 4;
#endif

#if DETECT_ARCH_RISCV64 == 1
#if defined(__riscv_float_abi_soft)
   options.MCOptions.ABIName = "lp64";
#elif defined(__riscv_float_abi_single)
   options.MCOptions.ABIName = "lp64f";
#elif defined(__riscv_float_abi_double)
   options.MCOptions.ABIName = "lp64d";
#else
#error "GALLIVM: unknown target riscv float abi"
#endif
#endif

#if DETECT_ARCH_RISCV32 == 1
#if defined(__riscv_float_abi_soft)
   options.MCOptions.ABIName = "ilp32";
#elif defined(__riscv_float_abi_single)
   options.MCOptions.ABIName = "ilp32f";
#elif defined(__riscv_float_abi_double)
   options.MCOptions.ABIName = "ilp32d";
#else
#error "GALLIVM: unknown target riscv float abi"
#endif
#endif

#if DETECT_ARCH_LOONGARCH64 == 1
#if defined(__loongarch_lp64) && defined(__loongarch_double_float)
   options.MCOptions.ABIName = "lp64d";
#else
#error "GALLIVM: unknown target loongarch float abi"
#endif
#endif

   JTMB.setOptions(options);

   std::vector<std::string> MAttrs;

   lp_build_fill_mattrs(MAttrs);

   JTMB.addFeatures(MAttrs);

   lp_build_dump_mattrs(MAttrs);

   std::string MCPU = llvm::sys::getHostCPUName().str();
   /*
    * Note that the MAttrs set above will be sort of ignored (since we should
    * not set any which would not be set by specifying the cpu anyway).
    * It ought to be safe though since getHostCPUName() should include bits
    * not only from the cpu but environment as well (for instance if it's safe
    * to use avx instructions which need OS support). According to
    * http://llvm.org/bugs/show_bug.cgi?id=19429 however if I understand this
    * right it may be necessary to specify older cpu (or disable mattrs) though
    * when not using MCJIT so no instructions are generated which the old JIT
    * can't handle. Not entirely sure if we really need to do anything yet.
    *
    * Not sure if the above is also the case for ORCJIT, but we need set CPU
    * manually since we don't use JITTargetMachineBuilder::detectHost()
    */

#if DETECT_ARCH_PPC_64 == 1
   /*
    * Large programs, e.g. gnome-shell and firefox, may tax the addressability
    * of the Medium code model once dynamically generated JIT-compiled shader
    * programs are linked in and relocated.  Yet the default code model as of
    * LLVM 8 is Medium or even Small.
    * The cost of changing from Medium to Large is negligible:
    * - an additional 8-byte pointer stored immediately before the shader entrypoint;
    * - change an add-immediate (addis) instruction to a load (ld).
    */
   JTMB.setCodeModel(CodeModel::Large);

#if UTIL_ARCH_LITTLE_ENDIAN
   /*
    * Versions of LLVM prior to 4.0 lacked a table entry for "POWER8NVL",
    * resulting in (big-endian) "generic" being returned on
    * little-endian Power8NVL systems.  The result was that code that
    * attempted to load the least significant 32 bits of a 64-bit quantity
    * from memory loaded the wrong half.  This resulted in failures in some
    * Piglit tests, e.g.
    * .../arb_gpu_shader_fp64/execution/conversion/frag-conversion-explicit-double-uint
    */
   if (MCPU == "generic")
      MCPU = "pwr8";
#endif
#endif

#if DETECT_ARCH_MIPS64 == 1
   /*
    * ls3a4000 CPU and ls2k1000 SoC is a mips64r5 compatible with MSA SIMD
    * instruction set implemented, while ls3a3000 is mips64r2 compatible
    * only. getHostCPUName() return "generic" on all loongson
    * mips CPU currently. So we override the MCPU to mips64r5 if MSA is
    * implemented, feedback to mips64r2 for all other ordinary mips64 cpu.
    */
   if (MCPU == "generic")
      MCPU = util_get_cpu_caps()->has_msa ? "mips64r5" : "mips64r2";
#endif

#if DETECT_ARCH_RISCV64 == 1
   /**
    * should be fixed with https://reviews.llvm.org/D121149 in llvm 15,
    * set it anyway for llvm 14
    */
   if (MCPU == "generic")
      MCPU = "generic-rv64";

   JTMB.setCodeModel(CodeModel::Medium);
   JTMB.setRelocationModel(Reloc::PIC_);
#endif

#if DETECT_ARCH_RISCV32 == 1
   /**
    * should be fixed with https://reviews.llvm.org/D121149 in llvm 15,
    * set it anyway for llvm 14
    */
   if (MCPU == "generic")
      MCPU = "generic-rv32";

   JTMB.setCodeModel(CodeModel::Medium);
   JTMB.setRelocationModel(Reloc::PIC_);
#endif

   JTMB.setCPU(MCPU);
   if (gallivm_debug & (GALLIVM_DEBUG_IR | GALLIVM_DEBUG_ASM | GALLIVM_DEBUG_DUMP_BC)) {
      debug_printf("llc -mcpu option: %s\n", MCPU.c_str());
   }

   return JTMB;
}


} /* Anonymous namespace */

bool
lp_build_init(void)
{
   (void)LPJit::get_instance();
   return true;
}

bool
init_gallivm_state(struct gallivm_state *gallivm, const char *name,
                   lp_context_ref *context, struct lp_cached_code *cache)
{
   assert(!gallivm->context);
   assert(!gallivm->_ts_context);
   assert(!gallivm->module);

   if (!lp_build_init())
      return false;

   gallivm->cache = cache;

   gallivm->_ts_context = context->tsref;
   gallivm->context = context->ref;

   gallivm->module_name = LPJit::get_unique_name(name);
   gallivm->module = LLVMModuleCreateWithNameInContext(gallivm->module_name,
                                                       gallivm->context);
#if DETECT_ARCH_X86 == 1
   lp_set_module_stack_alignment_override(gallivm->module, 4);
#endif
   gallivm->builder = LLVMCreateBuilderInContext(gallivm->context);
   gallivm->_per_module_jd = LPJit::create_jit_dylib(gallivm->module_name);

   gallivm->target = LLVMCreateTargetDataLayout(LPJit::get_instance()->tm);

   lp_build_coro_declare_malloc_hooks(gallivm);
   return true;
}

struct gallivm_state *
gallivm_create(const char *name, lp_context_ref *context,
               struct lp_cached_code *cache){
   struct gallivm_state *gallivm;

   gallivm = CALLOC_STRUCT(gallivm_state);
   if (gallivm) {
      if (!init_gallivm_state(gallivm, name, context, cache)) {
         FREE(gallivm);
         gallivm = NULL;
      }
   }

   assert(gallivm != NULL);
   return gallivm;
}

void
gallivm_destroy(struct gallivm_state *gallivm)
{
   LPJit::remove_jd(gallivm->_per_module_jd);
   gallivm->_per_module_jd = nullptr;
   FREE(gallivm);
}

void
gallivm_free_ir(struct gallivm_state *gallivm)
{
   if (gallivm->module)
      LLVMDisposeModule(gallivm->module);
   FREE(gallivm->module_name);

   if (gallivm->target) {
      LLVMDisposeTargetData(gallivm->target);
   }

   if (gallivm->builder)
      LLVMDisposeBuilder(gallivm->builder);

   if (gallivm->cache) {
      if (gallivm->cache->jit_obj_cache)
         lp_free_objcache(gallivm->cache->jit_obj_cache);
      free(gallivm->cache->data);
   }

   gallivm->target = NULL;
   gallivm->module=NULL;
   gallivm->module_name=NULL;
   gallivm->builder=NULL;
   gallivm->context=NULL;
   gallivm->_ts_context=NULL;
   gallivm->cache=NULL;
   LPJit::deregister_gallivm_state(gallivm);
}

void
gallivm_add_global_mapping(struct gallivm_state *gallivm, LLVMValueRef sym, void* addr)
{
   LPJit::add_mapping_to_jd(sym, addr, gallivm->_per_module_jd);
}

void
gallivm_compile_module(struct gallivm_state *gallivm)
{
   lp_init_printf_hook(gallivm);
   gallivm_add_global_mapping(gallivm, gallivm->debug_printf_hook,
         (void *)debug_printf);

   lp_init_clock_hook(gallivm);
   gallivm_add_global_mapping(gallivm, gallivm->get_time_hook,
         (void *)os_time_get_nano);

   lp_build_coro_add_malloc_hooks(gallivm);

   LPJit::add_ir_module_to_jd(gallivm->_ts_context, gallivm->module,
      gallivm->_per_module_jd);
   /* ownership of module is now transferred into orc jit,
    * disallow modifying it
    */
   LPJit::register_gallivm_state(gallivm);
   gallivm->module = nullptr;

   if (gallivm->cache) {
      if (!gallivm->cache->jit_obj_cache) {
         LPObjectCacheORC *objcache = new LPObjectCacheORC(gallivm->cache);
         gallivm->cache->jit_obj_cache = (void *)objcache;
      }
   }
   /* defer compilation till first lookup by gallivm_jit_function */
}

func_pointer
gallivm_jit_function(struct gallivm_state *gallivm,
                     LLVMValueRef func, const char *func_name)
{
   LPObjectCacheORC *objcache = NULL;
   if (gallivm->cache) {
      assert(gallivm->cache->jit_obj_cache);
      objcache = (LPObjectCacheORC *)gallivm->cache->jit_obj_cache;
   }

   return pointer_to_func(
      LPJit::lookup_in_jd(func_name, gallivm->_per_module_jd, objcache));
}

void
gallivm_stub_func(struct gallivm_state *gallivm, LLVMValueRef func)
{
   /*
    * ORCJIT cannot accept a function with absolutely no content at all.
    * Generate a "void func() {}" stub here.
    */
   LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(gallivm->context,
                                                           func, "entry");
   LLVMBuilderRef builder = gallivm->builder;
   assert(builder);
   LLVMPositionBuilderAtEnd(builder, block);
   LLVMBuildRetVoid(builder);
}
