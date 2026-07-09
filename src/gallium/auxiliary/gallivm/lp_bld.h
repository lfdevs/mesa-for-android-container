/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
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

/**
 * @file
 * Wrapper for LLVM header file #includes.
 */


#ifndef LP_BLD_H
#define LP_BLD_H


/**
 * @file
 * LLVM IR building helpers interfaces.
 *
 * We use LLVM-C bindings for now. They are not documented, but follow the C++
 * interfaces very closely, and appear to be complete enough for code
 * genration. See
 * http://npcontemplation.blogspot.com/2008/06/secret-of-llvm-c-bindings.html
 * for a standalone example.
 */

#include <llvm/Config/llvm-config.h>

#include <llvm-c/Core.h>

#if GALLIVM_USE_ORCJIT
#include <llvm-c/Orc.h>
#endif

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "util/simple_mtx.h"

/**
 * Redefine these LLVM entrypoints as invalid macros to make sure we
 * don't accidentally use them.  We need to use the functions which
 * take an explicit LLVMContextRef parameter.
 */
#define LLVMInt1Type ILLEGAL_LLVM_FUNCTION
#define LLVMInt8Type ILLEGAL_LLVM_FUNCTION
#define LLVMInt16Type ILLEGAL_LLVM_FUNCTION
#define LLVMInt32Type ILLEGAL_LLVM_FUNCTION
#define LLVMInt64Type ILLEGAL_LLVM_FUNCTION
#define LLVMIntType ILLEGAL_LLVM_FUNCTION
#define LLVMFloatType ILLEGAL_LLVM_FUNCTION
#define LLVMDoubleType ILLEGAL_LLVM_FUNCTION
#define LLVMX86FP80Type ILLEGAL_LLVM_FUNCTION
#define LLVMFP128Type ILLEGAL_LLVM_FUNCTION
#define LLVMPPCFP128Type ILLEGAL_LLVM_FUNCTION
#define LLVMStructType ILLEGAL_LLVM_FUNCTION
#define LLVMVoidType ILLEGAL_LLVM_FUNCTION
#define LLVMLabelType ILLEGAL_LLVM_FUNCTION
#define LLVMOpaqueType ILLEGAL_LLVM_FUNCTION
#define LLVMUnionType ILLEGAL_LLVM_FUNCTION
#define LLVMMDString ILLEGAL_LLVM_FUNCTION
#define LLVMMDNode ILLEGAL_LLVM_FUNCTION
#define LLVMConstString ILLEGAL_LLVM_FUNCTION
#define LLVMConstStruct ILLEGAL_LLVM_FUNCTION
#define LLVMAppendBasicBlock ILLEGAL_LLVM_FUNCTION
#define LLVMInsertBasicBlock ILLEGAL_LLVM_FUNCTION
#define LLVMCreateBuilder ILLEGAL_LLVM_FUNCTION

/* ORC's ThreadSafeContext only locks during add-to-JIT; IR building
 * touches the LLVMContext without that lock, so a separate mutex is
 * needed for shareable shaders. Aliasing copies (owned=false) share
 * the same mutex pointer.
 */
typedef struct lp_context_ref {
   LLVMContextRef ref;
#if GALLIVM_USE_ORCJIT
   LLVMOrcThreadSafeContextRef tsref;
#endif
   simple_mtx_t *mutex;
   bool owned;
} lp_context_ref;

static inline void
lp_context_create(lp_context_ref *context)
{
   assert(context != NULL);

   context->mutex = NULL;

#if GALLIVM_USE_ORCJIT
#if LLVM_VERSION_MAJOR >= 21
   context->ref = LLVMContextCreate();
   /* Ownership of ref is then transferred to tsref */
   context->tsref = LLVMOrcCreateNewThreadSafeContextFromLLVMContext(context->ref);
#else
   context->tsref = LLVMOrcCreateNewThreadSafeContext();
   context->ref = LLVMOrcThreadSafeContextGetContext(context->tsref);
#endif
#else
   context->ref = LLVMContextCreate();
#endif
   context->owned = true;
#if LLVM_VERSION_MAJOR == 15
   if (context->ref) {
      LLVMContextSetOpaquePointers(context->ref, false);
   }
#endif
}

/* Like lp_context_create, but also creates the mutex that serializes
 * LLVMContext access for shareable shaders. Only the screen's shared context
 * needs it; per-context users call lp_context_create.
 */
static inline void
lp_context_create_thread_safe(lp_context_ref *context)
{
   assert(context != NULL);

   simple_mtx_t *mutex = (simple_mtx_t *)malloc(sizeof(simple_mtx_t));
   if (!mutex) {
      /* Caller checks ref == NULL to detect failure. */
      context->ref = NULL;
#if GALLIVM_USE_ORCJIT
      context->tsref = NULL;
#endif
      context->owned = false;
      return;
   }
   simple_mtx_init(mutex, mtx_plain);

   lp_context_create(context);
   context->mutex = mutex;
}

static inline void
lp_context_destroy(lp_context_ref *context)
{
   assert(context != NULL);
   if (context->owned) {
#if GALLIVM_USE_ORCJIT
      LLVMOrcDisposeThreadSafeContext(context->tsref);
#else
      LLVMContextDispose(context->ref);
#endif
      context->ref = NULL;
      if (context->mutex) {
         simple_mtx_destroy(context->mutex);
         free(context->mutex);
      }
   }
}


#endif /* LP_BLD_H */
