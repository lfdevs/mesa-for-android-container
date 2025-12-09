/**************************************************************************
 *
 * Copyright 2019 Red Hat.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **************************************************************************/

#ifndef LP_BLD_NIR_H
#define LP_BLD_NIR_H

#include <llvm-c/Core.h>

#include "lp_bld_type.h"
#include "nir.h"

struct gallivm_state;
struct nir_shader;
struct lp_img_params;
struct lp_build_tgsi_params;
struct lp_build_sampler_aos;

/*
 * 2 reserved functions args for each function call,
 * exec mask and context.
 */
#define LP_RESV_FUNC_ARGS 2

void lp_build_nir_soa(struct gallivm_state *gallivm,
                      struct nir_shader *shader,
                      const struct lp_build_tgsi_params *params,
                      LLVMValueRef (*outputs)[4]);

void lp_build_nir_soa_func(struct gallivm_state *gallivm,
                           struct nir_shader *shader,
                           nir_function_impl *impl,
                           const struct lp_build_tgsi_params *params,
                           LLVMValueRef (*outputs)[4]);

void lp_build_nir_aos(struct gallivm_state *gallivm,
                      struct nir_shader *shader,
                      struct lp_type type,
                      const unsigned char swizzles[4],
                      LLVMValueRef consts_ptr,
                      const LLVMValueRef *inputs,
                      LLVMValueRef *outputs,
                      const struct lp_build_sampler_aos *sampler);

struct lp_build_fn {
   LLVMTypeRef fn_type;
   LLVMValueRef fn;
};

void
lp_build_nir_soa_prepasses(struct nir_shader *nir);

void
lp_build_opt_nir(struct nir_shader *nir);


static inline LLVMValueRef
lp_nir_array_build_gather_values(LLVMBuilderRef builder,
                                 LLVMValueRef * values,
                                 unsigned value_count)
{
   LLVMTypeRef arr_type = LLVMArrayType(LLVMTypeOf(values[0]), value_count);
   LLVMValueRef arr = LLVMGetUndef(arr_type);

   for (unsigned i = 0; i < value_count; i++) {
      arr = LLVMBuildInsertValue(builder, arr, values[i], i, "");
   }
   return arr;
}

LLVMAtomicRMWBinOp
lp_translate_atomic_op(nir_atomic_op op);

uint32_t
lp_build_nir_sample_key(mesa_shader_stage stage, nir_tex_instr *instr);


void lp_img_op_from_intrinsic(struct lp_img_params *params, nir_intrinsic_instr *instr);

uint32_t lp_packed_img_op_from_intrinsic(nir_intrinsic_instr *instr);

enum lp_nir_call_context_args {
   LP_NIR_CALL_CONTEXT_CONTEXT,
   LP_NIR_CALL_CONTEXT_RESOURCES,
   LP_NIR_CALL_CONTEXT_SHARED,
   LP_NIR_CALL_CONTEXT_SCRATCH,
   LP_NIR_CALL_CONTEXT_WORK_DIM,
   LP_NIR_CALL_CONTEXT_THREAD_ID_0,
   LP_NIR_CALL_CONTEXT_THREAD_ID_1,
   LP_NIR_CALL_CONTEXT_THREAD_ID_2,
   LP_NIR_CALL_CONTEXT_BLOCK_ID_0,
   LP_NIR_CALL_CONTEXT_BLOCK_ID_1,
   LP_NIR_CALL_CONTEXT_BLOCK_ID_2,
   LP_NIR_CALL_CONTEXT_GRID_SIZE_0,
   LP_NIR_CALL_CONTEXT_GRID_SIZE_1,
   LP_NIR_CALL_CONTEXT_GRID_SIZE_2,
   LP_NIR_CALL_CONTEXT_BLOCK_SIZE_0,
   LP_NIR_CALL_CONTEXT_BLOCK_SIZE_1,
   LP_NIR_CALL_CONTEXT_BLOCK_SIZE_2,
   LP_NIR_CALL_CONTEXT_MAX_ARGS,
};

LLVMTypeRef
lp_build_cs_func_call_context(struct gallivm_state *gallivm, int length,
                              LLVMTypeRef context_type, LLVMTypeRef resources_type);



#endif
