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
#include "util/u_cpu_detect.h"
#include "lp_bld_debug.h"
#include "lp_bld_init.h"
#include "lp_bld_type.h"

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>

unsigned gallivm_perf = 0;

static const struct debug_named_value lp_bld_perf_flags[] = {
   { "brilinear", GALLIVM_PERF_BRILINEAR, "enable brilinear optimization" },
   { "rho_approx", GALLIVM_PERF_RHO_APPROX, "enable rho_approx optimization" },
   { "no_quad_lod", GALLIVM_PERF_NO_QUAD_LOD, "disable quad_lod optimization" },
   { "no_aos_sampling", GALLIVM_PERF_NO_AOS_SAMPLING, "disable aos sampling optimization" },
   { "nopt",   GALLIVM_PERF_NO_OPT, "disable optimization passes to speed up shader compilation" },
   DEBUG_NAMED_VALUE_END
};

unsigned gallivm_debug = 0;

static const struct debug_named_value lp_bld_debug_flags[] = {
   { "tgsi",   GALLIVM_DEBUG_TGSI, NULL },
   { "ir",     GALLIVM_DEBUG_IR, NULL },
   { "asm",    GALLIVM_DEBUG_ASM, NULL },
   { "perf",   GALLIVM_DEBUG_PERF, NULL },
   { "gc",     GALLIVM_DEBUG_GC, NULL },
/* Don't allow setting DUMP_BC for release builds, since writing the files may be an issue with setuid. */
#if MESA_DEBUG
   { "dumpbc", GALLIVM_DEBUG_DUMP_BC, NULL },
#endif
   { "symbols", GALLIVM_DEBUG_SYMBOLS, NULL },
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(gallivm_debug, "GALLIVM_DEBUG", lp_bld_debug_flags, 0)

unsigned lp_native_vector_width;

unsigned
lp_build_init_native_width(void)
{
   // Default to 256 until we're confident llvmpipe with 512 is as correct and not slower than 256
   lp_native_vector_width = MIN2(util_get_cpu_caps()->max_vector_bits, 256);
   assert(lp_native_vector_width);

   lp_native_vector_width = debug_get_num_option("LP_NATIVE_VECTOR_WIDTH", lp_native_vector_width);
   assert(lp_native_vector_width);

   return lp_native_vector_width;
}

void
lp_init_env_options(void)
{
   gallivm_debug = debug_get_option_gallivm_debug();

   if (!__normal_user())
      gallivm_debug &= ~GALLIVM_DEBUG_SYMBOLS;

   gallivm_perf = debug_get_flags_option("GALLIVM_PERF", lp_bld_perf_flags, 0 );
}

unsigned
gallivm_get_perf_flags(void)
{
   return gallivm_perf;
}

void
lp_init_clock_hook(struct gallivm_state *gallivm)
{
   if (gallivm->get_time_hook)
      return;

   LLVMTypeRef get_time_type = LLVMFunctionType(LLVMInt64TypeInContext(gallivm->context), NULL, 0, 1);
   gallivm->get_time_hook = LLVMAddFunction(gallivm->module, "get_time_hook", get_time_type);
}

/**
 * Validate a function.
 * Verification is only done with debug builds.
 */
void
gallivm_verify_function(struct gallivm_state *gallivm,
                        LLVMValueRef func)
{
   /* Verify the LLVM IR.  If invalid, dump and abort */
#if MESA_DEBUG
   if (LLVMVerifyFunction(func, LLVMPrintMessageAction)) {
      lp_debug_dump_value(func);
      assert(0);
      return;
   }
#endif

   if (gallivm_debug & GALLIVM_DEBUG_IR) {
      /* Print the LLVM IR to stderr */
      lp_debug_dump_value(func);
      debug_printf("\n");
   }
}
