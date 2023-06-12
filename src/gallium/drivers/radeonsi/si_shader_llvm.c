/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include "ac_nir.h"
#include "ac_nir_to_llvm.h"
#include "ac_rtld.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "tgsi/tgsi_from_mesa.h"
#include "util/u_memory.h"
#include "util/u_prim.h"

struct si_llvm_diagnostics {
   struct util_debug_callback *debug;
   unsigned retval;
};

static void si_diagnostic_handler(LLVMDiagnosticInfoRef di, void *context)
{
   struct si_llvm_diagnostics *diag = (struct si_llvm_diagnostics *)context;
   LLVMDiagnosticSeverity severity = LLVMGetDiagInfoSeverity(di);
   const char *severity_str = NULL;

   switch (severity) {
   case LLVMDSError:
      severity_str = "error";
      break;
   case LLVMDSWarning:
      severity_str = "warning";
      break;
   case LLVMDSRemark:
   case LLVMDSNote:
   default:
      return;
   }

   char *description = LLVMGetDiagInfoDescription(di);

   util_debug_message(diag->debug, SHADER_INFO, "LLVM diagnostic (%s): %s", severity_str,
                      description);

   if (severity == LLVMDSError) {
      diag->retval = 1;
      fprintf(stderr, "LLVM triggered Diagnostic Handler: %s\n", description);
   }

   LLVMDisposeMessage(description);
}

bool si_compile_llvm(struct si_screen *sscreen, struct si_shader_binary *binary,
                     struct ac_shader_config *conf, struct ac_llvm_compiler *compiler,
                     struct ac_llvm_context *ac, struct util_debug_callback *debug,
                     gl_shader_stage stage, const char *name, bool less_optimized)
{
   unsigned count = p_atomic_inc_return(&sscreen->num_compilations);

   if (si_can_dump_shader(sscreen, stage, SI_DUMP_LLVM_IR)) {
      fprintf(stderr, "radeonsi: Compiling shader %d\n", count);

      fprintf(stderr, "%s LLVM IR:\n\n", name);
      ac_dump_module(ac->module);
      fprintf(stderr, "\n");
   }

   if (sscreen->record_llvm_ir) {
      char *ir = LLVMPrintModuleToString(ac->module);
      binary->llvm_ir_string = strdup(ir);
      LLVMDisposeMessage(ir);
   }

   if (!si_replace_shader(count, binary)) {
      struct ac_compiler_passes *passes = compiler->passes;

      if (less_optimized && compiler->low_opt_passes)
         passes = compiler->low_opt_passes;

      struct si_llvm_diagnostics diag = {debug};
      LLVMContextSetDiagnosticHandler(ac->context, si_diagnostic_handler, &diag);

      if (!ac_compile_module_to_elf(passes, ac->module, (char **)&binary->elf_buffer,
                                    &binary->elf_size))
         diag.retval = 1;

      if (diag.retval != 0) {
         util_debug_message(debug, SHADER_INFO, "LLVM compilation failed");
         return false;
      }
   }

   struct ac_rtld_binary rtld;
   if (!ac_rtld_open(&rtld, (struct ac_rtld_open_info){
                               .info = &sscreen->info,
                               .shader_type = stage,
                               .wave_size = ac->wave_size,
                               .num_parts = 1,
                               .elf_ptrs = &binary->elf_buffer,
                               .elf_sizes = &binary->elf_size}))
      return false;

   bool ok = ac_rtld_read_config(&sscreen->info, &rtld, conf);
   ac_rtld_close(&rtld);
   return ok;
}

void si_llvm_context_init(struct si_shader_context *ctx, struct si_screen *sscreen,
                          struct ac_llvm_compiler *compiler, unsigned wave_size,
                          bool exports_color_null, bool exports_mrtz,
                          enum ac_float_mode float_mode)
{
   memset(ctx, 0, sizeof(*ctx));
   ctx->screen = sscreen;
   ctx->compiler = compiler;

   ac_llvm_context_init(&ctx->ac, compiler, sscreen->info.gfx_level, sscreen->info.family,
                        sscreen->info.has_3d_cube_border_color_mipmap, float_mode,
                        wave_size, 64, exports_color_null, exports_mrtz);
}

void si_llvm_create_func(struct si_shader_context *ctx, const char *name, LLVMTypeRef *return_types,
                         unsigned num_return_elems, unsigned max_workgroup_size)
{
   LLVMTypeRef ret_type;
   enum ac_llvm_calling_convention call_conv;

   if (num_return_elems)
      ret_type = LLVMStructTypeInContext(ctx->ac.context, return_types, num_return_elems, true);
   else
      ret_type = ctx->ac.voidt;

   gl_shader_stage real_stage = ctx->stage;

   /* LS is merged into HS (TCS), and ES is merged into GS. */
   if (ctx->screen->info.gfx_level >= GFX9 && ctx->stage <= MESA_SHADER_GEOMETRY) {
      if (ctx->shader->key.ge.as_ls)
         real_stage = MESA_SHADER_TESS_CTRL;
      else if (ctx->shader->key.ge.as_es || ctx->shader->key.ge.as_ngg)
         real_stage = MESA_SHADER_GEOMETRY;
   }

   switch (real_stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      call_conv = AC_LLVM_AMDGPU_VS;
      break;
   case MESA_SHADER_TESS_CTRL:
      call_conv = AC_LLVM_AMDGPU_HS;
      break;
   case MESA_SHADER_GEOMETRY:
      call_conv = AC_LLVM_AMDGPU_GS;
      break;
   case MESA_SHADER_FRAGMENT:
      call_conv = AC_LLVM_AMDGPU_PS;
      break;
   case MESA_SHADER_COMPUTE:
      call_conv = AC_LLVM_AMDGPU_CS;
      break;
   default:
      unreachable("Unhandle shader type");
   }

   /* Setup the function */
   ctx->return_type = ret_type;
   ctx->main_fn = ac_build_main(&ctx->args->ac, &ctx->ac, call_conv, name, ret_type, ctx->ac.module);
   ctx->return_value = LLVMGetUndef(ctx->return_type);

   if (ctx->screen->info.address32_hi) {
      ac_llvm_add_target_dep_function_attr(ctx->main_fn.value, "amdgpu-32bit-address-high-bits",
                                           ctx->screen->info.address32_hi);
   }

   if (ctx->stage <= MESA_SHADER_GEOMETRY && ctx->shader->key.ge.as_ngg &&
       si_shader_uses_streamout(ctx->shader))
      ac_llvm_add_target_dep_function_attr(ctx->main_fn.value, "amdgpu-gds-size", 256);

   ac_llvm_set_workgroup_size(ctx->main_fn.value, max_workgroup_size);
   ac_llvm_set_target_features(ctx->main_fn.value, &ctx->ac);
}

void si_llvm_create_main_func(struct si_shader_context *ctx)
{
   struct si_shader *shader = ctx->shader;
   LLVMTypeRef returns[AC_MAX_ARGS];
   unsigned i;

   for (i = 0; i < ctx->args->ac.num_sgprs_returned; i++)
      returns[i] = ctx->ac.i32; /* SGPR */
   for (; i < ctx->args->ac.return_count; i++)
      returns[i] = ctx->ac.f32; /* VGPR */

   si_llvm_create_func(ctx, "main", returns, ctx->args->ac.return_count,
                       si_get_max_workgroup_size(shader));

   /* Reserve register locations for VGPR inputs the PS prolog may need. */
   if (ctx->stage == MESA_SHADER_FRAGMENT && !ctx->shader->is_monolithic) {
      ac_llvm_add_target_dep_function_attr(
         ctx->main_fn.value, "InitialPSInputAddr",
         S_0286D0_PERSP_SAMPLE_ENA(1) | S_0286D0_PERSP_CENTER_ENA(1) |
            S_0286D0_PERSP_CENTROID_ENA(1) | S_0286D0_LINEAR_SAMPLE_ENA(1) |
            S_0286D0_LINEAR_CENTER_ENA(1) | S_0286D0_LINEAR_CENTROID_ENA(1) |
            S_0286D0_FRONT_FACE_ENA(1) | S_0286D0_ANCILLARY_ENA(1) |
            S_0286D0_SAMPLE_COVERAGE_ENA(1) | S_0286D0_POS_FIXED_PT_ENA(1));
   }


   if (ctx->stage <= MESA_SHADER_GEOMETRY &&
       (shader->key.ge.as_ls || ctx->stage == MESA_SHADER_TESS_CTRL)) {
      /* The LSHS size is not known until draw time, so we append it
       * at the end of whatever LDS use there may be in the rest of
       * the shader (currently none, unless LLVM decides to do its
       * own LDS-based lowering).
       */
      ctx->ac.lds = (struct ac_llvm_pointer) {
         .value = LLVMAddGlobalInAddressSpace(ctx->ac.module, LLVMArrayType(ctx->ac.i32, 0),
                                                "__lds_end", AC_ADDR_SPACE_LDS),
         .pointee_type = LLVMArrayType(ctx->ac.i32, 0)
      };
      LLVMSetAlignment(ctx->ac.lds.value, 256);
   }

   if (ctx->stage == MESA_SHADER_VERTEX) {
      ctx->abi.vertex_id = ac_get_arg(&ctx->ac, ctx->args->ac.vertex_id);
      ctx->abi.instance_id = ac_get_arg(&ctx->ac, ctx->args->ac.instance_id);
      if (ctx->args->ac.vs_rel_patch_id.used)
         ctx->abi.vs_rel_patch_id = ac_get_arg(&ctx->ac, ctx->args->ac.vs_rel_patch_id);

      /* Non-monolithic shaders apply the LS-HS input VGPR hw bug workaround in
       * the VS prolog, while monolithic shaders apply it here.
       */
      if (shader->is_monolithic && shader->key.ge.part.vs.prolog.ls_vgpr_fix)
         ac_fixup_ls_hs_input_vgprs(&ctx->ac, &ctx->abi, &ctx->args->ac);
   } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
      ctx->abi.persp_centroid = ac_get_arg(&ctx->ac, ctx->args->ac.persp_centroid);
      ctx->abi.linear_centroid = ac_get_arg(&ctx->ac, ctx->args->ac.linear_centroid);
   }
}

void si_llvm_optimize_module(struct si_shader_context *ctx)
{
   /* Dump LLVM IR before any optimization passes */
   if (si_can_dump_shader(ctx->screen, ctx->stage, SI_DUMP_INIT_LLVM_IR))
      ac_dump_module(ctx->ac.module);

   /* Run the pass */
   LLVMRunPassManager(ctx->compiler->passmgr, ctx->ac.module);
   LLVMDisposeBuilder(ctx->ac.builder);
}

void si_llvm_dispose(struct si_shader_context *ctx)
{
   LLVMDisposeModule(ctx->ac.module);
   LLVMContextDispose(ctx->ac.context);
   ac_llvm_context_dispose(&ctx->ac);
}

/**
 * Load a dword from a constant buffer.
 */
LLVMValueRef si_buffer_load_const(struct si_shader_context *ctx, LLVMValueRef resource,
                                  LLVMValueRef offset)
{
   return ac_build_buffer_load(&ctx->ac, resource, 1, NULL, offset, NULL, ctx->ac.f32,
                               0, true, true);
}

void si_llvm_build_ret(struct si_shader_context *ctx, LLVMValueRef ret)
{
   if (LLVMGetTypeKind(LLVMTypeOf(ret)) == LLVMVoidTypeKind)
      LLVMBuildRetVoid(ctx->ac.builder);
   else
      LLVMBuildRet(ctx->ac.builder, ret);
}

LLVMValueRef si_insert_input_ret(struct si_shader_context *ctx, LLVMValueRef ret,
                                 struct ac_arg param, unsigned return_index)
{
   return LLVMBuildInsertValue(ctx->ac.builder, ret, ac_get_arg(&ctx->ac, param), return_index, "");
}

LLVMValueRef si_insert_input_ret_float(struct si_shader_context *ctx, LLVMValueRef ret,
                                       struct ac_arg param, unsigned return_index)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef p = ac_get_arg(&ctx->ac, param);

   return LLVMBuildInsertValue(builder, ret, ac_to_float(&ctx->ac, p), return_index, "");
}

LLVMValueRef si_insert_input_ptr(struct si_shader_context *ctx, LLVMValueRef ret,
                                 struct ac_arg param, unsigned return_index)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   LLVMValueRef ptr = ac_get_arg(&ctx->ac, param);
   ptr = LLVMBuildPtrToInt(builder, ptr, ctx->ac.i32, "");
   return LLVMBuildInsertValue(builder, ret, ptr, return_index, "");
}

LLVMValueRef si_prolog_get_internal_bindings(struct si_shader_context *ctx)
{
   LLVMValueRef ptr[2], list;
   bool merged_shader = si_is_merged_shader(ctx->shader);

   ptr[0] = LLVMGetParam(ctx->main_fn.value, (merged_shader ? 8 : 0) + SI_SGPR_INTERNAL_BINDINGS);
   list =
      LLVMBuildIntToPtr(ctx->ac.builder, ptr[0], ac_array_in_const32_addr_space(ctx->ac.v4i32), "");
   return list;
}

/* Ensure that the esgs ring is declared.
 *
 * We declare it with 64KB alignment as a hint that the
 * pointer value will always be 0.
 */
void si_llvm_declare_esgs_ring(struct si_shader_context *ctx)
{
   if (ctx->esgs_ring)
      return;

   assert(!LLVMGetNamedGlobal(ctx->ac.module, "esgs_ring"));

   ctx->esgs_ring = LLVMAddGlobalInAddressSpace(ctx->ac.module, LLVMArrayType(ctx->ac.i32, 0),
                                                "esgs_ring", AC_ADDR_SPACE_LDS);
   LLVMSetLinkage(ctx->esgs_ring, LLVMExternalLinkage);
   LLVMSetAlignment(ctx->esgs_ring, 64 * 1024);
}

static void si_init_exec_from_input(struct si_shader_context *ctx, struct ac_arg param,
                                    unsigned bitoffset)
{
   LLVMValueRef args[] = {
      ac_get_arg(&ctx->ac, param),
      LLVMConstInt(ctx->ac.i32, bitoffset, 0),
   };
   ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.init.exec.from.input", ctx->ac.voidt, args, 2, 0);
}

/**
 * Get the value of a shader input parameter and extract a bitfield.
 */
static LLVMValueRef unpack_llvm_param(struct si_shader_context *ctx, LLVMValueRef value,
                                      unsigned rshift, unsigned bitwidth)
{
   if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMFloatTypeKind)
      value = ac_to_integer(&ctx->ac, value);

   if (rshift)
      value = LLVMBuildLShr(ctx->ac.builder, value, LLVMConstInt(ctx->ac.i32, rshift, 0), "");

   if (rshift + bitwidth < 32) {
      unsigned mask = (1 << bitwidth) - 1;
      value = LLVMBuildAnd(ctx->ac.builder, value, LLVMConstInt(ctx->ac.i32, mask, 0), "");
   }

   return value;
}

LLVMValueRef si_unpack_param(struct si_shader_context *ctx, struct ac_arg param, unsigned rshift,
                             unsigned bitwidth)
{
   LLVMValueRef value = ac_get_arg(&ctx->ac, param);

   return unpack_llvm_param(ctx, value, rshift, bitwidth);
}

static void si_llvm_declare_compute_memory(struct si_shader_context *ctx)
{
   struct si_shader_selector *sel = ctx->shader->selector;
   unsigned lds_size = sel->info.base.shared_size;

   LLVMTypeRef i8p = LLVMPointerType(ctx->ac.i8, AC_ADDR_SPACE_LDS);
   LLVMValueRef var;

   assert(!ctx->ac.lds.value);

   LLVMTypeRef type = LLVMArrayType(ctx->ac.i8, lds_size);
   var = LLVMAddGlobalInAddressSpace(ctx->ac.module, type,
                                     "compute_lds", AC_ADDR_SPACE_LDS);
   LLVMSetAlignment(var, 64 * 1024);

   ctx->ac.lds = (struct ac_llvm_pointer) {
      .value = LLVMBuildBitCast(ctx->ac.builder, var, i8p, ""),
      .pointee_type = type,
   };
}

/**
 * Given a list of shader part functions, build a wrapper function that
 * runs them in sequence to form a monolithic shader.
 */
void si_build_wrapper_function(struct si_shader_context *ctx, struct ac_llvm_pointer *parts,
                               unsigned num_parts, unsigned main_part,
                               unsigned next_shader_first_part,
                               enum ac_arg_type *main_arg_types, bool same_thread_count)
{
   LLVMBuilderRef builder = ctx->ac.builder;
   /* PS epilog has one arg per color component; gfx9 merged shader
    * prologs need to forward 40 SGPRs.
    */
   LLVMValueRef initial[AC_MAX_ARGS], out[AC_MAX_ARGS];
   LLVMTypeRef function_type;
   unsigned num_first_params;
   unsigned num_out, initial_num_out;
   ASSERTED unsigned num_out_sgpr;         /* used in debug checks */
   ASSERTED unsigned initial_num_out_sgpr; /* used in debug checks */
   unsigned num_sgprs, num_vgprs;
   unsigned gprs;

   memset(ctx->args, 0, sizeof(*ctx->args));

   for (unsigned i = 0; i < num_parts; ++i) {
      ac_add_function_attr(ctx->ac.context, parts[i].value, -1, "alwaysinline");
      LLVMSetLinkage(parts[i].value, LLVMPrivateLinkage);
   }

   /* The parameters of the wrapper function correspond to those of the
    * first part in terms of SGPRs and VGPRs, but we use the types of the
    * main part to get the right types. This is relevant for the
    * dereferenceable attribute on descriptor table pointers.
    */
   num_sgprs = 0;
   num_vgprs = 0;

   function_type = parts[0].pointee_type;
   num_first_params = LLVMCountParamTypes(function_type);

   for (unsigned i = 0; i < num_first_params; ++i) {
      LLVMValueRef param = LLVMGetParam(parts[0].value, i);

      if (ac_is_sgpr_param(param)) {
         assert(num_vgprs == 0);
         num_sgprs += ac_get_type_size(LLVMTypeOf(param)) / 4;
      } else {
         num_vgprs += ac_get_type_size(LLVMTypeOf(param)) / 4;
      }
   }

   gprs = 0;
   while (gprs < num_sgprs + num_vgprs) {
      LLVMValueRef param = LLVMGetParam(parts[main_part].value, ctx->args->ac.arg_count);
      LLVMTypeRef type = LLVMTypeOf(param);
      unsigned size = ac_get_type_size(type) / 4;
      enum ac_arg_type arg_type = main_arg_types[ctx->args->ac.arg_count];
      assert(arg_type != AC_ARG_INVALID);

      ac_add_arg(&ctx->args->ac, gprs < num_sgprs ? AC_ARG_SGPR : AC_ARG_VGPR, size, arg_type, NULL);

      assert(ac_is_sgpr_param(param) == (gprs < num_sgprs));
      assert(gprs + size <= num_sgprs + num_vgprs &&
             (gprs >= num_sgprs || gprs + size <= num_sgprs));

      gprs += size;
   }

   /* Prepare the return type. */
   unsigned num_returns = 0;
   LLVMTypeRef returns[AC_MAX_ARGS], last_func_type, return_type;

   last_func_type = parts[num_parts - 1].pointee_type;
   return_type = LLVMGetReturnType(last_func_type);

   switch (LLVMGetTypeKind(return_type)) {
   case LLVMStructTypeKind:
      num_returns = LLVMCountStructElementTypes(return_type);
      assert(num_returns <= ARRAY_SIZE(returns));
      LLVMGetStructElementTypes(return_type, returns);
      break;
   case LLVMVoidTypeKind:
      break;
   default:
      unreachable("unexpected type");
   }

   si_llvm_create_func(ctx, "wrapper", returns, num_returns,
                       si_get_max_workgroup_size(ctx->shader));

   if (si_is_merged_shader(ctx->shader) && !same_thread_count)
      ac_init_exec_full_mask(&ctx->ac);

   /* Record the arguments of the function as if they were an output of
    * a previous part.
    */
   num_out = 0;
   num_out_sgpr = 0;

   for (unsigned i = 0; i < ctx->args->ac.arg_count; ++i) {
      LLVMValueRef param = LLVMGetParam(ctx->main_fn.value, i);
      LLVMTypeRef param_type = LLVMTypeOf(param);
      LLVMTypeRef out_type = ctx->args->ac.args[i].file == AC_ARG_SGPR ? ctx->ac.i32 : ctx->ac.f32;
      unsigned size = ac_get_type_size(param_type) / 4;

      if (size == 1) {
         if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
            param = LLVMBuildPtrToInt(builder, param, ctx->ac.i32, "");
            param_type = ctx->ac.i32;
         }

         if (param_type != out_type)
            param = LLVMBuildBitCast(builder, param, out_type, "");
         out[num_out++] = param;
      } else {
         LLVMTypeRef vector_type = LLVMVectorType(out_type, size);

         if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
            param = LLVMBuildPtrToInt(builder, param, ctx->ac.i64, "");
            param_type = ctx->ac.i64;
         }

         if (param_type != vector_type)
            param = LLVMBuildBitCast(builder, param, vector_type, "");

         for (unsigned j = 0; j < size; ++j)
            out[num_out++] =
               LLVMBuildExtractElement(builder, param, LLVMConstInt(ctx->ac.i32, j, 0), "");
      }

      if (ctx->args->ac.args[i].file == AC_ARG_SGPR)
         num_out_sgpr = num_out;
   }

   memcpy(initial, out, sizeof(out));
   initial_num_out = num_out;
   initial_num_out_sgpr = num_out_sgpr;

   /* Now chain the parts. */
   LLVMValueRef ret = NULL;
   for (unsigned part = 0; part < num_parts; ++part) {
      LLVMValueRef in[AC_MAX_ARGS];
      LLVMTypeRef ret_type;
      unsigned out_idx = 0;
      unsigned num_params = LLVMCountParams(parts[part].value);

      /* Merged shaders are executed conditionally depending
       * on the number of enabled threads passed in the input SGPRs. */
      if (si_is_multi_part_shader(ctx->shader) && part == 0) {
         if (same_thread_count) {
            struct ac_arg arg;
            arg.arg_index = 3;
            arg.used = true;

            si_init_exec_from_input(ctx, arg, 0);
         } else {
            LLVMValueRef ena, count = initial[3];

            count = LLVMBuildAnd(builder, count, LLVMConstInt(ctx->ac.i32, 0x7f, 0), "");
            ena = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), count, "");
            ac_build_ifcc(&ctx->ac, ena, 6506);
         }
      }

      /* Derive arguments for the next part from outputs of the
       * previous one.
       */
      for (unsigned param_idx = 0; param_idx < num_params; ++param_idx) {
         LLVMValueRef param;
         LLVMTypeRef param_type;
         bool is_sgpr;
         unsigned param_size;
         LLVMValueRef arg = NULL;

         param = LLVMGetParam(parts[part].value, param_idx);
         param_type = LLVMTypeOf(param);
         param_size = ac_get_type_size(param_type) / 4;
         is_sgpr = ac_is_sgpr_param(param);

         if (is_sgpr) {
            ac_add_function_attr(ctx->ac.context, parts[part].value, param_idx + 1, "inreg");
         } else if (out_idx < num_out_sgpr) {
            /* Skip returned SGPRs the current part doesn't
             * declare on the input. */
            out_idx = num_out_sgpr;
         }

         assert(out_idx + param_size <= (is_sgpr ? num_out_sgpr : num_out));

         if (param_size == 1)
            arg = out[out_idx];
         else
            arg = ac_build_gather_values(&ctx->ac, &out[out_idx], param_size);

         if (LLVMTypeOf(arg) != param_type) {
            if (LLVMGetTypeKind(param_type) == LLVMPointerTypeKind) {
               if (LLVMGetPointerAddressSpace(param_type) == AC_ADDR_SPACE_CONST_32BIT) {
                  arg = LLVMBuildBitCast(builder, arg, ctx->ac.i32, "");
                  arg = LLVMBuildIntToPtr(builder, arg, param_type, "");
               } else {
                  arg = LLVMBuildBitCast(builder, arg, ctx->ac.i64, "");
                  arg = LLVMBuildIntToPtr(builder, arg, param_type, "");
               }
            } else {
               arg = LLVMBuildBitCast(builder, arg, param_type, "");
            }
         }

         in[param_idx] = arg;
         out_idx += param_size;
      }

      ret = ac_build_call(&ctx->ac, parts[part].pointee_type, parts[part].value, in, num_params);

      if (!same_thread_count &&
          si_is_multi_part_shader(ctx->shader) && part + 1 == next_shader_first_part) {
         ac_build_endif(&ctx->ac, 6506);

         /* The second half of the merged shader should use
          * the inputs from the toplevel (wrapper) function,
          * not the return value from the last call.
          *
          * That's because the last call was executed condi-
          * tionally, so we can't consume it in the main
          * block.
          */
         memcpy(out, initial, sizeof(initial));
         num_out = initial_num_out;
         num_out_sgpr = initial_num_out_sgpr;

         /* Execute the second shader conditionally based on the number of
          * enabled threads there.
          */
         if (ctx->stage == MESA_SHADER_TESS_CTRL) {
            LLVMValueRef ena, count = initial[3];

            count = LLVMBuildLShr(builder, count, LLVMConstInt(ctx->ac.i32, 8, 0), "");
            count = LLVMBuildAnd(builder, count, LLVMConstInt(ctx->ac.i32, 0x7f, 0), "");
            ena = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), count, "");
            ac_build_ifcc(&ctx->ac, ena, 6507);
         }
         continue;
      }

      /* Extract the returned GPRs. */
      ret_type = LLVMTypeOf(ret);
      num_out = 0;
      num_out_sgpr = 0;

      if (LLVMGetTypeKind(ret_type) != LLVMVoidTypeKind) {
         assert(LLVMGetTypeKind(ret_type) == LLVMStructTypeKind);

         unsigned ret_size = LLVMCountStructElementTypes(ret_type);

         for (unsigned i = 0; i < ret_size; ++i) {
            LLVMValueRef val = LLVMBuildExtractValue(builder, ret, i, "");

            assert(num_out < ARRAY_SIZE(out));
            out[num_out++] = val;

            if (LLVMTypeOf(val) == ctx->ac.i32) {
               assert(num_out_sgpr + 1 == num_out);
               num_out_sgpr = num_out;
            }
         }
      }
   }

   /* Close the conditional wrapping the second shader. */
   if (ctx->stage == MESA_SHADER_TESS_CTRL &&
       !same_thread_count && si_is_multi_part_shader(ctx->shader))
      ac_build_endif(&ctx->ac, 6507);

   if (LLVMGetTypeKind(LLVMTypeOf(ret)) == LLVMVoidTypeKind)
      LLVMBuildRetVoid(builder);
   else
      LLVMBuildRet(builder, ret);
}

static LLVMValueRef si_llvm_load_intrinsic(struct ac_shader_abi *abi, nir_intrinsic_instr *intrin)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ring_tess_offchip_amd:
      return ctx->tess_offchip_ring;

   case nir_intrinsic_load_tess_rel_patch_id_amd:
      return si_get_rel_patch_id(ctx);

   case nir_intrinsic_load_ring_esgs_amd:
      return ctx->esgs_ring;

   case nir_intrinsic_load_ring_gsvs_amd:
      return ctx->gsvs_ring[nir_intrinsic_stream_id(intrin)];

   case nir_intrinsic_load_lds_ngg_scratch_base_amd:
      return LLVMBuildPtrToInt(ctx->ac.builder, ctx->gs_ngg_scratch.value, ctx->ac.i32, "");

   case nir_intrinsic_load_lds_ngg_gs_out_vertex_base_amd:
      return LLVMBuildPtrToInt(ctx->ac.builder, ctx->gs_ngg_emit, ctx->ac.i32, "");

   default:
      return NULL;
   }
}

static LLVMValueRef si_llvm_load_sampler_desc(struct ac_shader_abi *abi, LLVMValueRef index,
                                              enum ac_descriptor_type desc_type)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMBuilderRef builder = ctx->ac.builder;

   if (index && LLVMTypeOf(index) == ctx->ac.i32) {
      bool is_vec4 = false;

      switch (desc_type) {
      case AC_DESC_IMAGE:
         /* The image is at [0:7]. */
         index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->ac.i32, 2, 0), "");
         break;
      case AC_DESC_BUFFER:
         /* The buffer is in [4:7]. */
         index = ac_build_imad(&ctx->ac, index, LLVMConstInt(ctx->ac.i32, 4, 0), ctx->ac.i32_1);
         is_vec4 = true;
         break;
      case AC_DESC_FMASK:
         /* The FMASK is at [8:15]. */
         assert(ctx->screen->info.gfx_level < GFX11);
         index = ac_build_imad(&ctx->ac, index, LLVMConstInt(ctx->ac.i32, 2, 0), ctx->ac.i32_1);
         break;
      case AC_DESC_SAMPLER:
         /* The sampler state is at [12:15]. */
         index = ac_build_imad(&ctx->ac, index, LLVMConstInt(ctx->ac.i32, 4, 0),
                               LLVMConstInt(ctx->ac.i32, 3, 0));
         is_vec4 = true;
         break;
      default:
         unreachable("invalid desc");
      }

      struct ac_llvm_pointer list = {
         .value = ac_get_arg(&ctx->ac, ctx->args->samplers_and_images),
         .pointee_type = is_vec4 ? ctx->ac.v4i32 : ctx->ac.v8i32,
      };

      return ac_build_load_to_sgpr(&ctx->ac, list, index);
   }

   return index;
}

static bool si_llvm_translate_nir(struct si_shader_context *ctx, struct si_shader *shader,
                                  struct nir_shader *nir, bool free_nir)
{
   struct si_shader_selector *sel = shader->selector;
   const struct si_shader_info *info = &sel->info;

   ctx->shader = shader;
   ctx->stage = shader->is_gs_copy_shader ? MESA_SHADER_VERTEX : sel->stage;

   ctx->num_const_buffers = info->base.num_ubos;
   ctx->num_shader_buffers = info->base.num_ssbos;

   ctx->num_samplers = BITSET_LAST_BIT(info->base.textures_used);
   ctx->num_images = info->base.num_images;

   ctx->abi.intrinsic_load = si_llvm_load_intrinsic;
   ctx->abi.load_sampler_desc = si_llvm_load_sampler_desc;

   si_llvm_create_main_func(ctx);

   if (ctx->stage <= MESA_SHADER_GEOMETRY &&
       (ctx->shader->key.ge.as_es || ctx->stage == MESA_SHADER_GEOMETRY))
      si_preload_esgs_ring(ctx);

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      /* preload instance_divisor_constbuf to be used for input load after culling */
      if (ctx->shader->key.ge.opt.ngg_culling &&
          ctx->shader->key.ge.part.vs.prolog.instance_divisor_is_fetched) {
         struct ac_llvm_pointer buf = ac_get_ptr_arg(&ctx->ac, &ctx->args->ac, ctx->args->internal_bindings);
         ctx->instance_divisor_constbuf =
            ac_build_load_to_sgpr(
               &ctx->ac, buf, LLVMConstInt(ctx->ac.i32, SI_VS_CONST_INSTANCE_DIVISORS, 0));
      }

      /* preload GSVS ring for GS copy shader */
      if (shader->is_gs_copy_shader) {
         ctx->gsvs_ring[0] =
            ac_build_load_to_sgpr(
               &ctx->ac,
               ac_get_ptr_arg(&ctx->ac, &ctx->args->ac, ctx->args->internal_bindings),
               LLVMConstInt(ctx->ac.i32, SI_RING_GSVS, 0));
      }
      break;

   case MESA_SHADER_TESS_CTRL:
      si_llvm_init_tcs_callbacks(ctx);
      si_llvm_preload_tess_rings(ctx);
      break;

   case MESA_SHADER_TESS_EVAL:
      si_llvm_preload_tess_rings(ctx);
      break;

   case MESA_SHADER_GEOMETRY:
      si_llvm_init_gs_callbacks(ctx);

      if (ctx->shader->key.ge.as_ngg) {
         LLVMTypeRef ai32 = LLVMArrayType(ctx->ac.i32, gfx10_ngg_get_scratch_dw_size(shader));
         ctx->gs_ngg_scratch = (struct ac_llvm_pointer) {
            .value = LLVMAddGlobalInAddressSpace(ctx->ac.module, ai32, "ngg_scratch", AC_ADDR_SPACE_LDS),
            .pointee_type = ai32
         };
         LLVMSetInitializer(ctx->gs_ngg_scratch.value, LLVMGetUndef(ai32));
         LLVMSetAlignment(ctx->gs_ngg_scratch.value, 8);

         ctx->gs_ngg_emit = LLVMAddGlobalInAddressSpace(
            ctx->ac.module, LLVMArrayType(ctx->ac.i32, 0), "ngg_emit", AC_ADDR_SPACE_LDS);
         LLVMSetLinkage(ctx->gs_ngg_emit, LLVMExternalLinkage);
         LLVMSetAlignment(ctx->gs_ngg_emit, 4);
      } else {
         si_preload_gs_rings(ctx);
      }
      break;

   case MESA_SHADER_FRAGMENT: {
      unsigned colors_read = ctx->shader->selector->info.colors_read;
      LLVMValueRef main_fn = ctx->main_fn.value;

      LLVMValueRef undef = LLVMGetUndef(ctx->ac.f32);

      unsigned offset = SI_PARAM_POS_FIXED_PT + 1;

      if (colors_read & 0x0f) {
         unsigned mask = colors_read & 0x0f;
         LLVMValueRef values[4];
         values[0] = mask & 0x1 ? LLVMGetParam(main_fn, offset++) : undef;
         values[1] = mask & 0x2 ? LLVMGetParam(main_fn, offset++) : undef;
         values[2] = mask & 0x4 ? LLVMGetParam(main_fn, offset++) : undef;
         values[3] = mask & 0x8 ? LLVMGetParam(main_fn, offset++) : undef;
         ctx->abi.color0 = ac_to_integer(&ctx->ac, ac_build_gather_values(&ctx->ac, values, 4));
      }
      if (colors_read & 0xf0) {
         unsigned mask = (colors_read & 0xf0) >> 4;
         LLVMValueRef values[4];
         values[0] = mask & 0x1 ? LLVMGetParam(main_fn, offset++) : undef;
         values[1] = mask & 0x2 ? LLVMGetParam(main_fn, offset++) : undef;
         values[2] = mask & 0x4 ? LLVMGetParam(main_fn, offset++) : undef;
         values[3] = mask & 0x8 ? LLVMGetParam(main_fn, offset++) : undef;
         ctx->abi.color1 = ac_to_integer(&ctx->ac, ac_build_gather_values(&ctx->ac, values, 4));
      }

      ctx->abi.num_interp = si_get_ps_num_interp(shader);

      ctx->abi.kill_ps_if_inf_interp =
         ctx->screen->options.no_infinite_interp &&
         (ctx->shader->selector->info.uses_persp_center ||
          ctx->shader->selector->info.uses_persp_centroid ||
          ctx->shader->selector->info.uses_persp_sample);
      break;
   }

   case MESA_SHADER_COMPUTE:
      if (nir->info.cs.user_data_components_amd) {
         ctx->abi.user_data = ac_get_arg(&ctx->ac, ctx->args->cs_user_data);
         ctx->abi.user_data = ac_build_expand_to_vec4(&ctx->ac, ctx->abi.user_data,
                                                      nir->info.cs.user_data_components_amd);
      }

      if (ctx->shader->selector->info.base.shared_size)
         si_llvm_declare_compute_memory(ctx);
      break;

   default:
      break;
   }

   if ((ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL) &&
       shader->key.ge.as_ngg && !shader->key.ge.as_es) {
      /* Unconditionally declare scratch space base for streamout and
       * vertex compaction. Whether space is actually allocated is
       * determined during linking / PM4 creation.
       */
      si_llvm_declare_esgs_ring(ctx);
      ctx->ac.lds.value = ctx->esgs_ring;
      ctx->ac.lds.pointee_type = ctx->ac.i32;

      /* This is really only needed when streamout and / or vertex
       * compaction is enabled.
       */
      if (si_shader_uses_streamout(shader) || shader->key.ge.opt.ngg_culling) {
         LLVMTypeRef asi32 = LLVMArrayType(ctx->ac.i32, gfx10_ngg_get_scratch_dw_size(shader));
         ctx->gs_ngg_scratch = (struct ac_llvm_pointer) {
            .value = LLVMAddGlobalInAddressSpace(ctx->ac.module, asi32, "ngg_scratch", AC_ADDR_SPACE_LDS),
            .pointee_type = asi32
         };
         LLVMSetInitializer(ctx->gs_ngg_scratch.value, LLVMGetUndef(asi32));
         LLVMSetAlignment(ctx->gs_ngg_scratch.value, 8);
      }
   }

   /* For merged shaders (VS-TCS, VS-GS, TES-GS): */
   if (ctx->screen->info.gfx_level >= GFX9 && si_is_merged_shader(shader)) {
      /* Set EXEC = ~0 before the first shader. For monolithic shaders, the wrapper
       * function does this.
       */
      if (ctx->stage == MESA_SHADER_TESS_EVAL) {
         /* TES has only 1 shader part, therefore it doesn't use the wrapper function. */
         if (!shader->is_monolithic || !shader->key.ge.as_es)
            ac_init_exec_full_mask(&ctx->ac);
      } else if (ctx->stage == MESA_SHADER_VERTEX) {
         if (shader->is_monolithic) {
            /* Only mono VS with TCS/GS present has wrapper function. */
            if (!shader->key.ge.as_ls && !shader->key.ge.as_es)
               ac_init_exec_full_mask(&ctx->ac);
         } else {
            /* If the prolog is present, EXEC is set there instead. */
             if (!si_vs_needs_prolog(sel, &shader->key.ge.part.vs.prolog))
                ac_init_exec_full_mask(&ctx->ac);
         }
      }

      /* NGG VS and NGG TES: nir ngg lowering send gs_alloc_req at the beginning when culling
       * is disabled, but GFX10 may hang if not all waves are launched before gs_alloc_req.
       * We work around this HW bug by inserting a barrier before gs_alloc_req.
       */
      if (ctx->screen->info.gfx_level == GFX10 &&
          (ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL) &&
          shader->key.ge.as_ngg && !shader->key.ge.as_es && !shader->key.ge.opt.ngg_culling)
         ac_build_s_barrier(&ctx->ac, ctx->stage);

      LLVMValueRef thread_enabled = NULL;

      if ((ctx->stage == MESA_SHADER_GEOMETRY && !shader->key.ge.as_ngg) ||
          (ctx->stage == MESA_SHADER_TESS_CTRL && !shader->is_monolithic)) {
         /* Wrap both shaders in an if statement according to the number of enabled threads
          * there. For monolithic TCS, the if statement is inserted by the wrapper function,
          * not here. For NGG GS, the if statement is inserted by nir lowering.
          */
         thread_enabled = si_is_gs_thread(ctx); /* 2nd shader: thread enabled bool */
      } else if ((shader->key.ge.as_ls || shader->key.ge.as_es) && !shader->is_monolithic) {
         /* For monolithic LS (VS before TCS) and ES (VS before GS and TES before GS),
          * the if statement is inserted by the wrapper function.
          */
         thread_enabled = si_is_es_thread(ctx); /* 1st shader: thread enabled bool */
      }

      if (thread_enabled) {
         ctx->merged_wrap_if_entry_block = LLVMGetInsertBlock(ctx->ac.builder);
         ctx->merged_wrap_if_label = 11500;
         ac_build_ifcc(&ctx->ac, thread_enabled, ctx->merged_wrap_if_label);
      }

      /* Execute a barrier before the second shader in
       * a merged shader.
       *
       * Execute the barrier inside the conditional block,
       * so that empty waves can jump directly to s_endpgm,
       * which will also signal the barrier.
       *
       * This is possible in gfx9, because an empty wave for the second shader does not insert
       * any ending. With NGG, empty waves may still be required to export data (e.g. GS output
       * vertices), so we cannot let them exit early.
       *
       * If the shader is TCS and the TCS epilog is present
       * and contains a barrier, it will wait there and then
       * reach s_endpgm.
       */
      if (ctx->stage == MESA_SHADER_TESS_CTRL) {
         /* We need the barrier only if TCS inputs are read from LDS. */
         if (!shader->key.ge.opt.same_patch_vertices ||
             shader->selector->info.base.inputs_read &
             ~shader->selector->info.tcs_vgpr_only_inputs) {
            ac_build_waitcnt(&ctx->ac, AC_WAIT_LGKM);

            /* If both input and output patches are wholly in one wave, we don't need a barrier.
             * That's true when both VS and TCS have the same number of patch vertices and
             * the wave size is a multiple of the number of patch vertices.
             */
            if (!shader->key.ge.opt.same_patch_vertices ||
                ctx->ac.wave_size % sel->info.base.tess.tcs_vertices_out != 0)
               ac_build_s_barrier(&ctx->ac, ctx->stage);
         }
      } else if (ctx->stage == MESA_SHADER_GEOMETRY) {
         ac_build_waitcnt(&ctx->ac, AC_WAIT_LGKM);
         ac_build_s_barrier(&ctx->ac, ctx->stage);
      }
   }

   ctx->abi.clamp_shadow_reference = true;
   ctx->abi.robust_buffer_access = true;
   ctx->abi.convert_undef_to_zero = true;
   ctx->abi.load_grid_size_from_user_sgpr = true;
   ctx->abi.clamp_div_by_zero = ctx->screen->options.clamp_div_by_zero ||
                                info->options & SI_PROFILE_CLAMP_DIV_BY_ZERO;
   ctx->abi.use_waterfall_for_divergent_tex_samplers = true;
   ctx->abi.disable_aniso_single_level = true;
   ctx->abi.conformant_trunc_coord = ctx->screen->info.conformant_trunc_coord;

   bool ls_need_output =
      ctx->stage == MESA_SHADER_VERTEX && shader->key.ge.as_ls &&
      shader->key.ge.opt.same_patch_vertices;

   bool tcs_need_output =
      ctx->stage == MESA_SHADER_TESS_CTRL && info->tessfactors_are_def_in_all_invocs;

   bool ps_need_output = ctx->stage == MESA_SHADER_FRAGMENT;

   if (ls_need_output || tcs_need_output || ps_need_output) {
      for (unsigned i = 0; i < info->num_outputs; i++) {
         LLVMTypeRef type = ctx->ac.f32;

         /* Only FS uses unpacked f16. Other stages pack 16-bit outputs into low and high bits of f32. */
         if (nir->info.stage == MESA_SHADER_FRAGMENT &&
             nir_alu_type_get_type_size(ctx->shader->selector->info.output_type[i]) == 16)
            type = ctx->ac.f16;

         for (unsigned j = 0; j < 4; j++) {
            ctx->abi.outputs[i * 4 + j] = ac_build_alloca_undef(&ctx->ac, type, "");
            ctx->abi.is_16bit[i * 4 + j] = type == ctx->ac.f16;
         }
      }
   }

   if (!ac_nir_translate(&ctx->ac, &ctx->abi, &ctx->args->ac, nir))
      return false;

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      if (shader->key.ge.as_ls)
         si_llvm_ls_build_end(ctx);
      else if (shader->key.ge.as_es)
         si_llvm_es_build_end(ctx);
      break;

   case MESA_SHADER_TESS_CTRL:
      if (!shader->is_monolithic)
         si_llvm_tcs_build_end(ctx);
      break;

   case MESA_SHADER_TESS_EVAL:
      if (ctx->shader->key.ge.as_es)
         si_llvm_es_build_end(ctx);
      break;

   case MESA_SHADER_GEOMETRY:
      if (!ctx->shader->key.ge.as_ngg)
         si_llvm_gs_build_end(ctx);
      break;

   case MESA_SHADER_FRAGMENT:
      if (!shader->is_monolithic)
         si_llvm_ps_build_end(ctx);
      break;

   default:
      break;
   }

   si_llvm_build_ret(ctx, ctx->return_value);

   if (free_nir)
      ralloc_free(nir);
   return true;
}

static bool si_should_optimize_less(struct ac_llvm_compiler *compiler,
                                    struct si_shader_selector *sel)
{
   if (!compiler->low_opt_passes)
      return false;

   /* Assume a slow CPU. */
   assert(!sel->screen->info.has_dedicated_vram && sel->screen->info.gfx_level <= GFX8);

   /* For a crazy dEQP test containing 2597 memory opcodes, mostly
    * buffer stores. */
   return sel->stage == MESA_SHADER_COMPUTE && sel->info.num_memory_stores > 1000;
}

bool si_llvm_compile_shader(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                            struct si_shader *shader, struct si_shader_args *args,
                            struct util_debug_callback *debug, struct nir_shader *nir)
{
   struct si_shader_selector *sel = shader->selector;
   struct si_shader_context ctx;
   enum ac_float_mode float_mode = nir->info.stage == MESA_SHADER_KERNEL ? AC_FLOAT_MODE_DEFAULT : AC_FLOAT_MODE_DEFAULT_OPENGL;

   bool exports_color_null = sel->info.colors_written;
   bool exports_mrtz = sel->info.writes_z || sel->info.writes_stencil || sel->info.writes_samplemask;
   if (!exports_mrtz && !exports_color_null)
      exports_color_null = si_shader_uses_discard(shader) || sscreen->info.gfx_level < GFX10;

   si_llvm_context_init(&ctx, sscreen, compiler, shader->wave_size, exports_color_null, exports_mrtz,
                        float_mode);
   ctx.args = args;

   if (!si_llvm_translate_nir(&ctx, shader, nir, false)) {
      si_llvm_dispose(&ctx);
      return false;
   }

   if (shader->is_monolithic && sel->stage == MESA_SHADER_TESS_CTRL) {
      /* Preserve main arguments. */
      enum ac_arg_type main_arg_types[AC_MAX_ARGS];

      if (sscreen->info.gfx_level >= GFX9) {
         struct si_shader_selector *ls = shader->key.ge.part.tcs.ls;
         struct ac_llvm_pointer parts[2];

         /* TCS main part */
         parts[1] = ctx.main_fn;

         struct si_shader shader_ls = {};
         shader_ls.selector = ls;
         shader_ls.key.ge.part.vs.prolog = shader->key.ge.part.tcs.ls_prolog;
         shader_ls.key.ge.as_ls = 1;
         shader_ls.key.ge.mono = shader->key.ge.mono;
         shader_ls.key.ge.opt = shader->key.ge.opt;
         shader_ls.key.ge.opt.inline_uniforms = false; /* only TCS can inline uniforms */
         shader_ls.is_monolithic = true;

         si_init_shader_args(&shader_ls, ctx.args);

         bool free_nir;
         nir = si_get_nir_shader(&shader_ls, ctx.args, &free_nir,
                                 sel->info.tcs_vgpr_only_inputs, NULL);
         si_update_shader_binary_info(shader, nir);

         if (!si_llvm_translate_nir(&ctx, &shader_ls, nir, free_nir)) {
            si_llvm_dispose(&ctx);
            return false;
         }
         shader->info.uses_instanceid |=
            ls->info.uses_instanceid || shader_ls.info.uses_instanceid;
         parts[0] = ctx.main_fn;

         for (int i = 0; i < ctx.args->ac.arg_count; i++)
            main_arg_types[i] = ctx.args->ac.args[i].type;
         main_arg_types[MIN2(AC_MAX_ARGS - 1, ctx.args->ac.arg_count)] = AC_ARG_INVALID;

         /* Reset the shader context. */
         ctx.shader = shader;
         ctx.stage = MESA_SHADER_TESS_CTRL;

         si_build_wrapper_function(&ctx, parts, 2, 0, 1, main_arg_types,
                                   shader->key.ge.opt.same_patch_vertices);
      }
   } else if (shader->is_monolithic && sel->stage == MESA_SHADER_GEOMETRY) {
      if (ctx.screen->info.gfx_level >= GFX9) {
         enum ac_arg_type main_arg_types[AC_MAX_ARGS];

         struct si_shader_selector *es = shader->key.ge.part.gs.es;
         struct ac_llvm_pointer es_main = {};
         struct ac_llvm_pointer gs_main = ctx.main_fn;

         /* ES main part */
         struct si_shader shader_es = {};
         shader_es.selector = es;
         shader_es.key.ge.part.vs.prolog = shader->key.ge.part.gs.vs_prolog;
         shader_es.key.ge.as_es = 1;
         shader_es.key.ge.as_ngg = shader->key.ge.as_ngg;
         shader_es.key.ge.mono = shader->key.ge.mono;
         shader_es.key.ge.opt = shader->key.ge.opt;
         shader_es.key.ge.opt.inline_uniforms = false; /* only GS can inline uniforms */
         /* kill_outputs was computed based on GS outputs so we can't use it to kill VS outputs */
         shader_es.key.ge.opt.kill_outputs = 0;
         shader_es.is_monolithic = true;

         si_init_shader_args(&shader_es, ctx.args);

         bool free_nir;
         nir = si_get_nir_shader(&shader_es, ctx.args, &free_nir, 0, NULL);
         si_update_shader_binary_info(shader, nir);

         if (!si_llvm_translate_nir(&ctx, &shader_es, nir, free_nir)) {
            si_llvm_dispose(&ctx);
            return false;
         }
         shader->info.uses_instanceid |=
            es->info.uses_instanceid || shader_es.info.uses_instanceid;
         es_main = ctx.main_fn;

         /* Preserve main (= es_main) arguments. */
         for (int i = 0; i < ctx.args->ac.arg_count; i++)
            main_arg_types[i] = ctx.args->ac.args[i].type;
         main_arg_types[MIN2(AC_MAX_ARGS - 1, ctx.args->ac.arg_count)] = AC_ARG_INVALID;

         /* Reset the shader context. */
         ctx.shader = shader;
         ctx.stage = MESA_SHADER_GEOMETRY;

         /* Prepare the array of shader parts. */
         struct ac_llvm_pointer parts[2] = {es_main, gs_main};

         si_build_wrapper_function(&ctx, parts, 2, 0, 1, main_arg_types, false);
      } else {
         /* Nothing to do for gfx6-8. The shader has only 1 part and it's ctx.main_fn. */
      }
   } else if (shader->is_monolithic && sel->stage == MESA_SHADER_FRAGMENT) {
      si_llvm_build_monolithic_ps(&ctx, shader);
   }

   si_llvm_optimize_module(&ctx);

   /* Make sure the input is a pointer and not integer followed by inttoptr. */
   assert(LLVMGetTypeKind(LLVMTypeOf(LLVMGetParam(ctx.main_fn.value, 0))) == LLVMPointerTypeKind);

   /* Compile to bytecode. */
   if (!si_compile_llvm(sscreen, &shader->binary, &shader->config, compiler, &ctx.ac, debug,
                        sel->stage, si_get_shader_name(shader),
                        si_should_optimize_less(compiler, shader->selector))) {
      si_llvm_dispose(&ctx);
      fprintf(stderr, "LLVM failed to compile shader\n");
      return false;
   }

   si_llvm_dispose(&ctx);
   return true;
}
