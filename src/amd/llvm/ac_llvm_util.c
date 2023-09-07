/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
/* based on pieces from si_pipe.c and radeon_llvm_emit.c */
#include "ac_llvm_util.h"

#include "ac_llvm_build.h"
#include "c11/threads.h"
#include "util/bitscan.h"
#include "util/u_math.h"
#include <llvm-c/Core.h>
#include <llvm-c/Support.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void ac_init_llvm_target(void)
{
   LLVMInitializeAMDGPUTargetInfo();
   LLVMInitializeAMDGPUTarget();
   LLVMInitializeAMDGPUTargetMC();
   LLVMInitializeAMDGPUAsmPrinter();

   /* For inline assembly. */
   LLVMInitializeAMDGPUAsmParser();

   /* For ACO disassembly. */
   LLVMInitializeAMDGPUDisassembler();

   const char *argv[] = {
      /* error messages prefix */
      "mesa",
      "-amdgpu-atomic-optimizations=true",
   };

   ac_reset_llvm_all_options_occurrences();
   LLVMParseCommandLineOptions(ARRAY_SIZE(argv), argv, NULL);

   ac_llvm_run_atexit_for_destructors();
}

PUBLIC void ac_init_shared_llvm_once(void)
{
   static once_flag ac_init_llvm_target_once_flag = ONCE_FLAG_INIT;
   call_once(&ac_init_llvm_target_once_flag, ac_init_llvm_target);
}

#if !LLVM_IS_SHARED
static once_flag ac_init_static_llvm_target_once_flag = ONCE_FLAG_INIT;
static void ac_init_static_llvm_once(void)
{
   call_once(&ac_init_static_llvm_target_once_flag, ac_init_llvm_target);
}
#endif

void ac_init_llvm_once(void)
{
#if LLVM_IS_SHARED
   ac_init_shared_llvm_once();
#else
   ac_init_static_llvm_once();
#endif
}

LLVMTargetRef ac_get_llvm_target(const char *triple)
{
   LLVMTargetRef target = NULL;
   char *err_message = NULL;

   if (LLVMGetTargetFromTriple(triple, &target, &err_message)) {
      fprintf(stderr, "Cannot find target for triple %s ", triple);
      if (err_message) {
         fprintf(stderr, "%s\n", err_message);
      }
      LLVMDisposeMessage(err_message);
      return NULL;
   }
   return target;
}

const char *ac_get_llvm_processor_name(enum radeon_family family)
{
   switch (family) {
   case CHIP_TAHITI:
      return "tahiti";
   case CHIP_PITCAIRN:
      return "pitcairn";
   case CHIP_VERDE:
      return "verde";
   case CHIP_OLAND:
      return "oland";
   case CHIP_HAINAN:
      return "hainan";
   case CHIP_BONAIRE:
      return "bonaire";
   case CHIP_KABINI:
      return "kabini";
   case CHIP_KAVERI:
      return "kaveri";
   case CHIP_HAWAII:
      return "hawaii";
   case CHIP_TONGA:
      return "tonga";
   case CHIP_ICELAND:
      return "iceland";
   case CHIP_CARRIZO:
      return "carrizo";
   case CHIP_FIJI:
      return "fiji";
   case CHIP_STONEY:
      return "stoney";
   case CHIP_POLARIS10:
      return "polaris10";
   case CHIP_POLARIS11:
   case CHIP_POLARIS12:
   case CHIP_VEGAM:
      return "polaris11";
   case CHIP_VEGA10:
      return "gfx900";
   case CHIP_RAVEN:
      return "gfx902";
   case CHIP_VEGA12:
      return "gfx904";
   case CHIP_VEGA20:
      return "gfx906";
   case CHIP_RAVEN2:
   case CHIP_RENOIR:
      return "gfx909";
   case CHIP_MI100:
      return "gfx908";
   case CHIP_MI200:
      return "gfx90a";
   case CHIP_GFX940:
      return "gfx940";
   case CHIP_NAVI10:
      return "gfx1010";
   case CHIP_NAVI12:
      return "gfx1011";
   case CHIP_NAVI14:
      return "gfx1012";
   case CHIP_NAVI21:
      return "gfx1030";
   case CHIP_NAVI22:
      return "gfx1031";
   case CHIP_NAVI23:
      return "gfx1032";
   case CHIP_VANGOGH:
      return "gfx1033";
   case CHIP_NAVI24:
      return "gfx1034";
   case CHIP_REMBRANDT:
      return "gfx1035";
   case CHIP_RAPHAEL_MENDOCINO:
      return "gfx1036";
   case CHIP_GFX1100:
      return "gfx1100";
   case CHIP_GFX1101:
      return "gfx1101";
   case CHIP_GFX1102:
      return "gfx1102";
   case CHIP_GFX1103_R1:
   case CHIP_GFX1103_R2:
      return "gfx1103";
   default:
      return "";
   }
}

static LLVMTargetMachineRef ac_create_target_machine(enum radeon_family family,
                                                     enum ac_target_machine_options tm_options,
                                                     LLVMCodeGenOptLevel level,
                                                     const char **out_triple)
{
   assert(family >= CHIP_TAHITI);
   const char *triple = (tm_options & AC_TM_SUPPORTS_SPILL) ? "amdgcn-mesa-mesa3d" : "amdgcn--";
   LLVMTargetRef target = ac_get_llvm_target(triple);
   const char *name = ac_get_llvm_processor_name(family);

   LLVMTargetMachineRef tm =
      LLVMCreateTargetMachine(target, triple, name, "", level,
                              LLVMRelocDefault, LLVMCodeModelDefault);

   if (!ac_is_llvm_processor_supported(tm, name)) {
      LLVMDisposeTargetMachine(tm);
      fprintf(stderr, "amd: LLVM doesn't support %s, bailing out...\n", name);
      return NULL;
   }

   if (out_triple)
      *out_triple = triple;

   return tm;
}

LLVMAttributeRef ac_get_llvm_attribute(LLVMContextRef ctx, const char *str)
{
   return LLVMCreateEnumAttribute(ctx, LLVMGetEnumAttributeKindForName(str, strlen(str)), 0);
}

void ac_add_function_attr(LLVMContextRef ctx, LLVMValueRef function, int attr_idx,
                          const char *attr)
{
   assert(LLVMIsAFunction(function));
   LLVMAddAttributeAtIndex(function, attr_idx, ac_get_llvm_attribute(ctx, attr));
}

void ac_dump_module(LLVMModuleRef module)
{
   char *str = LLVMPrintModuleToString(module);
   fprintf(stderr, "%s", str);
   LLVMDisposeMessage(str);
}

void ac_llvm_add_target_dep_function_attr(LLVMValueRef F, const char *name, unsigned value)
{
   char str[16];

   snprintf(str, sizeof(str), "0x%x", value);
   LLVMAddTargetDependentFunctionAttr(F, name, str);
}

void ac_llvm_set_workgroup_size(LLVMValueRef F, unsigned size)
{
   if (!size)
      return;

   char str[32];
   snprintf(str, sizeof(str), "%u,%u", size, size);
   LLVMAddTargetDependentFunctionAttr(F, "amdgpu-flat-work-group-size", str);
}

void ac_llvm_set_target_features(LLVMValueRef F, struct ac_llvm_context *ctx, bool wgp_mode)
{
   char features[2048];

   snprintf(features, sizeof(features), "+DumpCode%s%s%s",
            /* GFX9 has broken VGPR indexing, so always promote alloca to scratch. */
            ctx->gfx_level == GFX9 ? ",-promote-alloca" : "",
            /* Wave32 is the default. */
            ctx->gfx_level >= GFX10 && ctx->wave_size == 64 ?
               ",+wavefrontsize64,-wavefrontsize32" : "",
            ctx->gfx_level >= GFX10 && !wgp_mode ? ",+cumode" : "");

   LLVMAddTargetDependentFunctionAttr(F, "target-features", features);
}

bool ac_init_llvm_compiler(struct ac_llvm_compiler *compiler, enum radeon_family family,
                           enum ac_target_machine_options tm_options)
{
   const char *triple;
   memset(compiler, 0, sizeof(*compiler));

   compiler->tm = ac_create_target_machine(family, tm_options, LLVMCodeGenLevelDefault, &triple);
   if (!compiler->tm)
      return false;

   if (tm_options & AC_TM_CREATE_LOW_OPT) {
      compiler->low_opt_tm =
         ac_create_target_machine(family, tm_options, LLVMCodeGenLevelLess, NULL);
      if (!compiler->low_opt_tm)
         goto fail;
   }

   compiler->target_library_info = ac_create_target_library_info(triple);
   if (!compiler->target_library_info)
      goto fail;

   compiler->passmgr =
      ac_create_passmgr(compiler->target_library_info, tm_options & AC_TM_CHECK_IR);
   if (!compiler->passmgr)
      goto fail;

   return true;
fail:
   ac_destroy_llvm_compiler(compiler);
   return false;
}

void ac_destroy_llvm_compiler(struct ac_llvm_compiler *compiler)
{
   ac_destroy_llvm_passes(compiler->passes);
   ac_destroy_llvm_passes(compiler->low_opt_passes);

   if (compiler->passmgr)
      LLVMDisposePassManager(compiler->passmgr);
   if (compiler->target_library_info)
      ac_dispose_target_library_info(compiler->target_library_info);
   if (compiler->low_opt_tm)
      LLVMDisposeTargetMachine(compiler->low_opt_tm);
   if (compiler->tm)
      LLVMDisposeTargetMachine(compiler->tm);
}
