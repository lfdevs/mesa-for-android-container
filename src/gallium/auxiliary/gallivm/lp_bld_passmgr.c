/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_debug.h"
#include "util/os_time.h"
#include "lp_bld_debug.h"
#include "lp_bld_passmgr.h"

#define USE_NEW_PASS (LLVM_VERSION_MAJOR >= 15)

#if USE_NEW_PASS == 1
#include <llvm-c/Transforms/PassBuilder.h>
#else
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/Transforms/Utils.h>
#if LLVM_VERSION_MAJOR <= 8 && (DETECT_ARCH_AARCH64 || DETECT_ARCH_ARM || DETECT_ARCH_S390 || DETECT_ARCH_MIPS64)
#include <llvm-c/Transforms/IPO.h>
#endif
#include <llvm-c/Transforms/Coroutines.h>
#endif

#if USE_NEW_PASS == 0
struct lp_passmgr {
   LLVMPassManagerRef passmgr;
   LLVMPassManagerRef cgpassmgr;
};
#else
struct lp_passmgr;
#endif

bool
lp_passmgr_create(LLVMModuleRef module, struct lp_passmgr **mgr_p)
{
   struct lp_passmgr *mgr = NULL;
#if USE_NEW_PASS == 0
   mgr = CALLOC_STRUCT(lp_passmgr);
   if (!mgr)
      return false;

   mgr->passmgr = LLVMCreateFunctionPassManagerForModule(module);
   if (!mgr->passmgr) {
      free(mgr);
      return false;
   }

   mgr->cgpassmgr = LLVMCreatePassManager();
   /*
    * TODO: some per module pass manager with IPO passes might be helpful -
    * the generated texture functions may benefit from inlining if they are
    * simple, or constant propagation into them, etc.
    */

#if LLVM_VERSION_MAJOR <= 8 && (DETECT_ARCH_AARCH64 || DETECT_ARCH_ARM || DETECT_ARCH_S390 || DETECT_ARCH_MIPS64)
   LLVMAddArgumentPromotionPass(mgr->cgpassmgr);
   LLVMAddFunctionAttrsPass(mgr->cgpassmgr);
#endif
   LLVMAddCoroEarlyPass(mgr->cgpassmgr);
   LLVMAddCoroSplitPass(mgr->cgpassmgr);
   LLVMAddCoroElidePass(mgr->cgpassmgr);

   if ((gallivm_perf & GALLIVM_PERF_NO_OPT) == 0) {
      /*
       * TODO: Evaluate passes some more - keeping in mind
       * both quality of generated code and compile times.
       */
      /*
       * NOTE: if you change this, don't forget to change the output
       * with GALLIVM_DEBUG_DUMP_BC in gallivm_compile_module.
       */
      LLVMAddScalarReplAggregatesPass(mgr->passmgr);
      LLVMAddEarlyCSEPass(mgr->passmgr);
      LLVMAddCFGSimplificationPass(mgr->passmgr);
      /*
       * FIXME: LICM is potentially quite useful. However, for some
       * rather crazy shaders the compile time can reach _hours_ per shader,
       * due to licm implying lcssa (since llvm 3.5), which can take forever.
       * Even for sane shaders, the cost of licm is rather high (and not just
       * due to lcssa, licm itself too), though mostly only in cases when it
       * can actually move things, so having to disable it is a pity.
       * LLVMAddLICMPass(mgr->passmgr);
       */
      LLVMAddReassociatePass(mgr->passmgr);
      LLVMAddPromoteMemoryToRegisterPass(mgr->passmgr);
#if LLVM_VERSION_MAJOR <= 11
      LLVMAddConstantPropagationPass(mgr->passmgr);
#else
      LLVMAddInstructionSimplifyPass(mgr->passmgr);
#endif
      LLVMAddInstructionCombiningPass(mgr->passmgr);
      LLVMAddGVNPass(mgr->passmgr);
   }
   else {
      /* We need at least this pass to prevent the backends to fail in
       * unexpected ways.
       */
      LLVMAddPromoteMemoryToRegisterPass(mgr->passmgr);
   }
   LLVMAddCoroCleanupPass(mgr->passmgr);
#endif
   *mgr_p = mgr;
   return true;
}

void
lp_passmgr_run(struct lp_passmgr *mgr,
               LLVMModuleRef module,
               LLVMTargetMachineRef tm,
               const char *module_name)
{
   int64_t time_begin;

   if (gallivm_debug & GALLIVM_DEBUG_PERF)
      time_begin = os_time_get();

#if USE_NEW_PASS == 1
   char passes[1024];
   passes[0] = 0;

   /*
    * there should be some way to combine these two pass runs but I'm not seeing it,
    * at the time of writing.
    */
   strcpy(passes, "default<O0>");

   LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
   LLVMRunPasses(module, passes, tm, opts);

   if (!(gallivm_perf & GALLIVM_PERF_NO_OPT))
#if LLVM_VERSION_MAJOR >= 18
      strcpy(passes, "sroa,early-cse,simplifycfg,reassociate,mem2reg,instsimplify,instcombine<no-verify-fixpoint>");
#else
      strcpy(passes, "sroa,early-cse,simplifycfg,reassociate,mem2reg,instsimplify,instcombine");
#endif
   else
      strcpy(passes, "mem2reg");

   LLVMRunPasses(module, passes, tm, opts);
   LLVMDisposePassBuilderOptions(opts);
#else
   LLVMRunPassManager(mgr->cgpassmgr, module);
   /* Run optimization passes */
   LLVMInitializeFunctionPassManager(mgr->passmgr);
   LLVMValueRef func;
   func = LLVMGetFirstFunction(module);
   while (func) {
      if (0) {
         debug_printf("optimizing func %s...\n", LLVMGetValueName(func));
      }

   /* Disable frame pointer omission on debug/profile builds */
   /* XXX: And workaround http://llvm.org/PR21435 */
#if MESA_DEBUG || defined(PROFILE) || DETECT_ARCH_X86 || DETECT_ARCH_X86_64
      LLVMAddTargetDependentFunctionAttr(func, "no-frame-pointer-elim", "true");
      LLVMAddTargetDependentFunctionAttr(func, "no-frame-pointer-elim-non-leaf", "true");
#endif

      LLVMRunFunctionPassManager(mgr->passmgr, func);
      func = LLVMGetNextFunction(func);
   }
   LLVMFinalizeFunctionPassManager(mgr->passmgr);
#endif

   if (gallivm_debug & GALLIVM_DEBUG_PERF) {
      int64_t time_end = os_time_get();
      int time_msec = (int)((time_end - time_begin) / 1000);
      assert(module_name);
      debug_printf("optimizing module %s took %d msec\n",
                   module_name, time_msec);
   }
}

void
lp_passmgr_dispose(struct lp_passmgr *mgr)
{
#if USE_NEW_PASS == 0
   if (mgr->passmgr) {
      LLVMDisposePassManager(mgr->passmgr);
      mgr->passmgr = NULL;
   }

   if (mgr->cgpassmgr) {
      LLVMDisposePassManager(mgr->cgpassmgr);
      mgr->cgpassmgr = NULL;
   }
   FREE(mgr);
#endif
}
