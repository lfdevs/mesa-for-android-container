/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "util/u_memory.h"
#include "ac_nir.h"

static LLVMValueRef get_vertex_index(struct si_shader_context *ctx,
                                     struct si_vs_prolog_bits *key, unsigned input_index,
                                     LLVMValueRef instance_divisor_constbuf,
                                     unsigned start_instance, unsigned base_vertex)
{
   LLVMValueRef instance_id = ctx->abi.instance_id_replaced ?
      ctx->abi.instance_id_replaced : ctx->abi.instance_id;
   LLVMValueRef vertex_id = ctx->abi.vertex_id_replaced ?
      ctx->abi.vertex_id_replaced : ctx->abi.vertex_id;

   bool divisor_is_one = key->instance_divisor_is_one & (1u << input_index);
   bool divisor_is_fetched =key->instance_divisor_is_fetched & (1u << input_index);

   LLVMValueRef index = NULL;
   if (divisor_is_one)
      index = instance_id;
   else if (divisor_is_fetched) {
      LLVMValueRef udiv_factors[4];

      for (unsigned j = 0; j < 4; j++) {
         udiv_factors[j] = si_buffer_load_const(
            ctx, instance_divisor_constbuf,
            LLVMConstInt(ctx->ac.i32, input_index * 16 + j * 4, 0));
         udiv_factors[j] = ac_to_integer(&ctx->ac, udiv_factors[j]);
      }

      /* The faster NUW version doesn't work when InstanceID == UINT_MAX.
       * Such InstanceID might not be achievable in a reasonable time though.
       */
      index = ac_build_fast_udiv_nuw(
         &ctx->ac, instance_id, udiv_factors[0],
         udiv_factors[1], udiv_factors[2], udiv_factors[3]);
   }

   if (divisor_is_one || divisor_is_fetched) {
      /* Add StartInstance. */
      index = LLVMBuildAdd(ctx->ac.builder, index,
                           LLVMGetParam(ctx->main_fn.value, start_instance), "");
   } else {
      /* VertexID + BaseVertex */
      index = LLVMBuildAdd(ctx->ac.builder, vertex_id,
                           LLVMGetParam(ctx->main_fn.value, base_vertex), "");
   }

   return index;
}

/**
 * Build the vertex shader prolog function.
 *
 * The inputs are the same as VS (a lot of SGPRs and 4 VGPR system values).
 * All inputs are returned unmodified. The vertex load indices are
 * stored after them, which will be used by the API VS for fetching inputs.
 *
 * For example, the expected outputs for instance_divisors[] = {0, 1, 2} are:
 *   input_v0,
 *   input_v1,
 *   input_v2,
 *   input_v3,
 *   (VertexID + BaseVertex),
 *   (InstanceID + StartInstance),
 *   (InstanceID / 2 + StartInstance)
 */
void si_llvm_build_vs_prolog(struct si_shader_context *ctx, union si_shader_part_key *key,
                             UNUSED bool separate_prolog)
{
   LLVMTypeRef *returns;
   LLVMValueRef ret, func;
   int num_returns, i;
   unsigned first_vs_vgpr = key->vs_prolog.num_merged_next_stage_vgprs;
   unsigned num_input_vgprs =
      key->vs_prolog.num_merged_next_stage_vgprs + 4;
   struct ac_arg input_sgpr_param[key->vs_prolog.num_input_sgprs];
   struct ac_arg input_vgpr_param[10];
   LLVMValueRef input_vgprs[10];
   unsigned num_all_input_regs = key->vs_prolog.num_input_sgprs + num_input_vgprs;
   unsigned user_sgpr_base = key->vs_prolog.num_merged_next_stage_vgprs ? 8 : 0;

   memset(ctx->args, 0, sizeof(*ctx->args));

   /* 4 preloaded VGPRs + vertex load indices as prolog outputs */
   returns = alloca((num_all_input_regs + key->vs_prolog.num_inputs) * sizeof(LLVMTypeRef));
   num_returns = 0;

   /* Declare input and output SGPRs. */
   for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
      ac_add_arg(&ctx->args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &input_sgpr_param[i]);
      returns[num_returns++] = ctx->ac.i32;
   }

   /* Preloaded VGPRs (outputs must be floats) */
   for (i = 0; i < num_input_vgprs; i++) {
      ac_add_arg(&ctx->args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &input_vgpr_param[i]);
      returns[num_returns++] = ctx->ac.f32;
   }

   /* Vertex load indices. */
   for (i = 0; i < key->vs_prolog.num_inputs; i++)
      returns[num_returns++] = ctx->ac.f32;

   /* Create the function. */
   si_llvm_create_func(ctx, "vs_prolog", returns, num_returns, 0);
   func = ctx->main_fn.value;

   for (i = 0; i < num_input_vgprs; i++) {
      input_vgprs[i] = ac_get_arg(&ctx->ac, input_vgpr_param[i]);
   }

   if (key->vs_prolog.num_merged_next_stage_vgprs) {
      if (!key->vs_prolog.is_monolithic)
         ac_init_exec_full_mask(&ctx->ac);

      if (key->vs_prolog.as_ls && ctx->screen->info.has_ls_vgpr_init_bug) {
         /* If there are no HS threads, SPI loads the LS VGPRs
          * starting at VGPR 0. Shift them back to where they
          * belong.
          */
         LLVMValueRef has_hs_threads =
            LLVMBuildICmp(ctx->ac.builder, LLVMIntNE,
                          si_unpack_param(ctx, input_sgpr_param[3], 8, 8), ctx->ac.i32_0, "");

         for (i = 4; i > 0; --i) {
            input_vgprs[i + 1] = LLVMBuildSelect(ctx->ac.builder, has_hs_threads,
                                                 input_vgprs[i + 1], input_vgprs[i - 1], "");
         }
      }
   }

   unsigned vertex_id_vgpr = first_vs_vgpr;
   unsigned instance_id_vgpr = ctx->screen->info.gfx_level >= GFX10
                                  ? first_vs_vgpr + 3
                                  : first_vs_vgpr + (key->vs_prolog.as_ls ? 2 : 1);

   ctx->abi.vertex_id = input_vgprs[vertex_id_vgpr];
   ctx->abi.instance_id = input_vgprs[instance_id_vgpr];
   ctx->abi.vertex_id_replaced = NULL;
   ctx->abi.instance_id_replaced = NULL;

   /* Copy inputs to outputs. This should be no-op, as the registers match,
    * but it will prevent the compiler from overwriting them unintentionally.
    */
   ret = ctx->return_value;
   for (i = 0; i < key->vs_prolog.num_input_sgprs; i++) {
      LLVMValueRef p = LLVMGetParam(func, i);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, p, i, "");
   }
   for (i = 0; i < num_input_vgprs; i++) {
      LLVMValueRef p = input_vgprs[i];

      if (i == vertex_id_vgpr)
         p = ctx->abi.vertex_id;
      else if (i == instance_id_vgpr)
         p = ctx->abi.instance_id;

      p = ac_to_float(&ctx->ac, p);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, p, key->vs_prolog.num_input_sgprs + i, "");
   }

   /* Compute vertex load indices from instance divisors. */
   LLVMValueRef instance_divisor_constbuf = NULL;

   if (key->vs_prolog.states.instance_divisor_is_fetched) {
      LLVMValueRef list = si_prolog_get_internal_bindings(ctx);
      LLVMValueRef buf_index = LLVMConstInt(ctx->ac.i32, SI_VS_CONST_INSTANCE_DIVISORS, 0);
      instance_divisor_constbuf = ac_build_load_to_sgpr(&ctx->ac,
         (struct ac_llvm_pointer) { .v = list, .t = ctx->ac.v4i32 }, buf_index);
   }

   for (i = 0; i < key->vs_prolog.num_inputs; i++) {
      LLVMValueRef index = get_vertex_index(ctx, &key->vs_prolog.states, i,
                                            instance_divisor_constbuf,
                                            user_sgpr_base + SI_SGPR_START_INSTANCE,
                                            user_sgpr_base + SI_SGPR_BASE_VERTEX);

      index = ac_to_float(&ctx->ac, index);
      ret = LLVMBuildInsertValue(ctx->ac.builder, ret, index, ctx->args->ac.arg_count + i, "");
   }

   si_llvm_build_ret(ctx, ret);
}
