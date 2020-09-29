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

#include "ac_nir_to_llvm.h"
#include "ac_rtld.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "tgsi/tgsi_from_mesa.h"
#include "util/u_memory.h"

struct si_llvm_diagnostics {
   struct pipe_debug_callback *debug;
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

   pipe_debug_message(diag->debug, SHADER_INFO, "LLVM diagnostic (%s): %s", severity_str,
                      description);

   if (severity == LLVMDSError) {
      diag->retval = 1;
      fprintf(stderr, "LLVM triggered Diagnostic Handler: %s\n", description);
   }

   LLVMDisposeMessage(description);
}

bool si_compile_llvm(struct si_screen *sscreen, struct si_shader_binary *binary,
                     struct ac_shader_config *conf, struct ac_llvm_compiler *compiler,
                     struct ac_llvm_context *ac, struct pipe_debug_callback *debug,
                     enum pipe_shader_type shader_type, const char *name, bool less_optimized)
{
   unsigned count = p_atomic_inc_return(&sscreen->num_compilations);

   if (si_can_dump_shader(sscreen, shader_type)) {
      fprintf(stderr, "radeonsi: Compiling shader %d\n", count);

      if (!(sscreen->debug_flags & (DBG(NO_IR) | DBG(PREOPT_IR)))) {
         fprintf(stderr, "%s LLVM IR:\n\n", name);
         ac_dump_module(ac->module);
         fprintf(stderr, "\n");
      }
   }

   if (sscreen->record_llvm_ir) {
      char *ir = LLVMPrintModuleToString(ac->module);
      binary->llvm_ir_string = strdup(ir);
      LLVMDisposeMessage(ir);
   }

   if (!si_replace_shader(count, binary)) {
      struct ac_compiler_passes *passes = compiler->passes;

      if (ac->wave_size == 32)
         passes = compiler->passes_wave32;
      else if (less_optimized && compiler->low_opt_passes)
         passes = compiler->low_opt_passes;

      struct si_llvm_diagnostics diag = {debug};
      LLVMContextSetDiagnosticHandler(ac->context, si_diagnostic_handler, &diag);

      if (!ac_compile_module_to_elf(passes, ac->module, (char **)&binary->elf_buffer,
                                    &binary->elf_size))
         diag.retval = 1;

      if (diag.retval != 0) {
         pipe_debug_message(debug, SHADER_INFO, "LLVM compilation failed");
         return false;
      }
   }

   struct ac_rtld_binary rtld;
   if (!ac_rtld_open(&rtld, (struct ac_rtld_open_info){
                               .info = &sscreen->info,
                               .shader_type = tgsi_processor_to_shader_stage(shader_type),
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
                          struct ac_llvm_compiler *compiler, unsigned wave_size)
{
   memset(ctx, 0, sizeof(*ctx));
   ctx->screen = sscreen;
   ctx->compiler = compiler;

   ac_llvm_context_init(&ctx->ac, compiler, sscreen->info.chip_class, sscreen->info.family,
                        AC_FLOAT_MODE_DEFAULT_OPENGL, wave_size, 64);
}

void si_llvm_create_func(struct si_shader_context *ctx, const char *name, LLVMTypeRef *return_types,
                         unsigned num_return_elems, unsigned max_workgroup_size)
{
   LLVMTypeRef ret_type;
   enum ac_llvm_calling_convention call_conv;
   enum pipe_shader_type real_shader_type;

   if (num_return_elems)
      ret_type = LLVMStructTypeInContext(ctx->ac.context, return_types, num_return_elems, true);
   else
      ret_type = ctx->ac.voidt;

   real_shader_type = ctx->type;

   /* LS is merged into HS (TCS), and ES is merged into GS. */
   if (ctx->screen->info.chip_class >= GFX9) {
      if (ctx->shader->key.as_ls)
         real_shader_type = PIPE_SHADER_TESS_CTRL;
      else if (ctx->shader->key.as_es || ctx->shader->key.as_ngg)
         real_shader_type = PIPE_SHADER_GEOMETRY;
   }

   switch (real_shader_type) {
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_TESS_EVAL:
      call_conv = AC_LLVM_AMDGPU_VS;
      break;
   case PIPE_SHADER_TESS_CTRL:
      call_conv = AC_LLVM_AMDGPU_HS;
      break;
   case PIPE_SHADER_GEOMETRY:
      call_conv = AC_LLVM_AMDGPU_GS;
      break;
   case PIPE_SHADER_FRAGMENT:
      call_conv = AC_LLVM_AMDGPU_PS;
      break;
   case PIPE_SHADER_COMPUTE:
      call_conv = AC_LLVM_AMDGPU_CS;
      break;
   default:
      unreachable("Unhandle shader type");
   }

   /* Setup the function */
   ctx->return_type = ret_type;
   ctx->main_fn = ac_build_main(&ctx->args, &ctx->ac, call_conv, name, ret_type, ctx->ac.module);
   ctx->return_value = LLVMGetUndef(ctx->return_type);

   if (ctx->screen->info.address32_hi) {
      ac_llvm_add_target_dep_function_attr(ctx->main_fn, "amdgpu-32bit-address-high-bits",
                                           ctx->screen->info.address32_hi);
   }

   LLVMAddTargetDependentFunctionAttr(ctx->main_fn, "no-signed-zeros-fp-math", "true");

   ac_llvm_set_workgroup_size(ctx->main_fn, max_workgroup_size);
}

void si_llvm_optimize_module(struct si_shader_context *ctx)
{
   /* Dump LLVM IR before any optimization passes */
   if (ctx->screen->debug_flags & DBG(PREOPT_IR) && si_can_dump_shader(ctx->screen, ctx->type))
      LLVMDumpModule(ctx->ac.module);

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
   return ac_build_buffer_load(&ctx->ac, resource, 1, NULL, offset, NULL, 0, 0, true, true);
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

LLVMValueRef si_prolog_get_rw_buffers(struct si_shader_context *ctx)
{
   LLVMValueRef ptr[2], list;
   bool merged_shader = si_is_merged_shader(ctx->shader);

   ptr[0] = LLVMGetParam(ctx->main_fn, (merged_shader ? 8 : 0) + SI_SGPR_RW_BUFFERS);
   list =
      LLVMBuildIntToPtr(ctx->ac.builder, ptr[0], ac_array_in_const32_addr_space(ctx->ac.v4i32), "");
   return list;
}

LLVMValueRef si_build_gather_64bit(struct si_shader_context *ctx, LLVMTypeRef type,
                                   LLVMValueRef val1, LLVMValueRef val2)
{
   LLVMValueRef values[2] = {
      ac_to_integer(&ctx->ac, val1),
      ac_to_integer(&ctx->ac, val2),
   };
   LLVMValueRef result = ac_build_gather_values(&ctx->ac, values, 2);
   return LLVMBuildBitCast(ctx->ac.builder, result, type, "");
}

void si_llvm_emit_barrier(struct si_shader_context *ctx)
{
   /* GFX6 only (thanks to a hw bug workaround):
    * The real barrier instruction isn’t needed, because an entire patch
    * always fits into a single wave.
    */
   if (ctx->screen->info.chip_class == GFX6 && ctx->type == PIPE_SHADER_TESS_CTRL) {
      ac_build_waitcnt(&ctx->ac, AC_WAIT_LGKM | AC_WAIT_VLOAD | AC_WAIT_VSTORE);
      return;
   }

   ac_build_s_barrier(&ctx->ac);
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

void si_init_exec_from_input(struct si_shader_context *ctx, struct ac_arg param, unsigned bitoffset)
{
   LLVMValueRef args[] = {
      ac_get_arg(&ctx->ac, param),
      LLVMConstInt(ctx->ac.i32, bitoffset, 0),
   };
   ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.init.exec.from.input", ctx->ac.voidt, args, 2,
                      AC_FUNC_ATTR_CONVERGENT);
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

LLVMValueRef si_get_primitive_id(struct si_shader_context *ctx, unsigned swizzle)
{
   if (swizzle > 0)
      return ctx->ac.i32_0;

   switch (ctx->type) {
   case PIPE_SHADER_VERTEX:
      return ac_get_arg(&ctx->ac, ctx->vs_prim_id);
   case PIPE_SHADER_TESS_CTRL:
      return ac_get_arg(&ctx->ac, ctx->args.tcs_patch_id);
   case PIPE_SHADER_TESS_EVAL:
      return ac_get_arg(&ctx->ac, ctx->args.tes_patch_id);
   case PIPE_SHADER_GEOMETRY:
      return ac_get_arg(&ctx->ac, ctx->args.gs_prim_id);
   default:
      assert(0);
      return ctx->ac.i32_0;
   }
}

LLVMValueRef si_llvm_get_block_size(struct ac_shader_abi *abi)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);

   LLVMValueRef values[3];
   LLVMValueRef result;
   unsigned i;
   unsigned *properties = ctx->shader->selector->info.properties;

   if (properties[TGSI_PROPERTY_CS_FIXED_BLOCK_WIDTH] != 0) {
      unsigned sizes[3] = {properties[TGSI_PROPERTY_CS_FIXED_BLOCK_WIDTH],
                           properties[TGSI_PROPERTY_CS_FIXED_BLOCK_HEIGHT],
                           properties[TGSI_PROPERTY_CS_FIXED_BLOCK_DEPTH]};

      for (i = 0; i < 3; ++i)
         values[i] = LLVMConstInt(ctx->ac.i32, sizes[i], 0);

      result = ac_build_gather_values(&ctx->ac, values, 3);
   } else {
      result = ac_get_arg(&ctx->ac, ctx->block_size);
   }

   return result;
}

void si_llvm_declare_compute_memory(struct si_shader_context *ctx)
{
   struct si_shader_selector *sel = ctx->shader->selector;
   unsigned lds_size = sel->info.properties[TGSI_PROPERTY_CS_LOCAL_SIZE];

   LLVMTypeRef i8p = LLVMPointerType(ctx->ac.i8, AC_ADDR_SPACE_LDS);
   LLVMValueRef var;

   assert(!ctx->ac.lds);

   var = LLVMAddGlobalInAddressSpace(ctx->ac.module, LLVMArrayType(ctx->ac.i8, lds_size),
                                     "compute_lds", AC_ADDR_SPACE_LDS);
   LLVMSetAlignment(var, 64 * 1024);

   ctx->ac.lds = LLVMBuildBitCast(ctx->ac.builder, var, i8p, "");
}

bool si_nir_build_llvm(struct si_shader_context *ctx, struct nir_shader *nir)
{
   if (nir->info.stage == MESA_SHADER_VERTEX) {
      si_llvm_load_vs_inputs(ctx, nir);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      unsigned colors_read = ctx->shader->selector->info.colors_read;
      LLVMValueRef main_fn = ctx->main_fn;

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

      ctx->abi.interp_at_sample_force_center =
         ctx->shader->key.mono.u.ps.interpolate_at_sample_force_center;

      ctx->abi.kill_ps_if_inf_interp =
         (ctx->screen->debug_flags & DBG(KILL_PS_INF_INTERP)) &&
         (ctx->shader->selector->info.uses_persp_center ||
          ctx->shader->selector->info.uses_persp_centroid ||
          ctx->shader->selector->info.uses_persp_sample);

   } else if (nir->info.stage == MESA_SHADER_COMPUTE) {
      if (nir->info.cs.user_data_components_amd) {
         ctx->abi.user_data = ac_get_arg(&ctx->ac, ctx->cs_user_data);
         ctx->abi.user_data = ac_build_expand_to_vec4(&ctx->ac, ctx->abi.user_data,
                                                      nir->info.cs.user_data_components_amd);
      }
   }

   ctx->abi.inputs = &ctx->inputs[0];
   ctx->abi.clamp_shadow_reference = true;
   ctx->abi.robust_buffer_access = true;
   ctx->abi.convert_undef_to_zero = true;
   ctx->abi.clamp_div_by_zero = ctx->screen->options.clamp_div_by_zero;

   if (ctx->shader->selector->info.properties[TGSI_PROPERTY_CS_LOCAL_SIZE]) {
      assert(gl_shader_stage_is_compute(nir->info.stage));
      si_llvm_declare_compute_memory(ctx);
   }
   ac_nir_translate(&ctx->ac, &ctx->abi, &ctx->args, nir);

   return true;
}

/**
 * Given a list of shader part functions, build a wrapper function that
 * runs them in sequence to form a monolithic shader.
 */
void si_build_wrapper_function(struct si_shader_context *ctx, LLVMValueRef *parts,
                               unsigned num_parts, unsigned main_part,
                               unsigned next_shader_first_part)
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

   memset(&ctx->args, 0, sizeof(ctx->args));

   for (unsigned i = 0; i < num_parts; ++i) {
      ac_add_function_attr(ctx->ac.context, parts[i], -1, AC_FUNC_ATTR_ALWAYSINLINE);
      LLVMSetLinkage(parts[i], LLVMPrivateLinkage);
   }

   /* The parameters of the wrapper function correspond to those of the
    * first part in terms of SGPRs and VGPRs, but we use the types of the
    * main part to get the right types. This is relevant for the
    * dereferenceable attribute on descriptor table pointers.
    */
   num_sgprs = 0;
   num_vgprs = 0;

   function_type = LLVMGetElementType(LLVMTypeOf(parts[0]));
   num_first_params = LLVMCountParamTypes(function_type);

   for (unsigned i = 0; i < num_first_params; ++i) {
      LLVMValueRef param = LLVMGetParam(parts[0], i);

      if (ac_is_sgpr_param(param)) {
         assert(num_vgprs == 0);
         num_sgprs += ac_get_type_size(LLVMTypeOf(param)) / 4;
      } else {
         num_vgprs += ac_get_type_size(LLVMTypeOf(param)) / 4;
      }
   }

   gprs = 0;
   while (gprs < num_sgprs + num_vgprs) {
      LLVMValueRef param = LLVMGetParam(parts[main_part], ctx->args.arg_count);
      LLVMTypeRef type = LLVMTypeOf(param);
      unsigned size = ac_get_type_size(type) / 4;

      /* This is going to get casted anyways, so we don't have to
       * have the exact same type. But we do have to preserve the
       * pointer-ness so that LLVM knows about it.
       */
      enum ac_arg_type arg_type = AC_ARG_INT;
      if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
         type = LLVMGetElementType(type);

         if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
            if (LLVMGetVectorSize(type) == 4)
               arg_type = AC_ARG_CONST_DESC_PTR;
            else if (LLVMGetVectorSize(type) == 8)
               arg_type = AC_ARG_CONST_IMAGE_PTR;
            else
               assert(0);
         } else if (type == ctx->ac.f32) {
            arg_type = AC_ARG_CONST_FLOAT_PTR;
         } else {
            assert(0);
         }
      }

      ac_add_arg(&ctx->args, gprs < num_sgprs ? AC_ARG_SGPR : AC_ARG_VGPR, size, arg_type, NULL);

      assert(ac_is_sgpr_param(param) == (gprs < num_sgprs));
      assert(gprs + size <= num_sgprs + num_vgprs &&
             (gprs >= num_sgprs || gprs + size <= num_sgprs));

      gprs += size;
   }

   /* Prepare the return type. */
   unsigned num_returns = 0;
   LLVMTypeRef returns[AC_MAX_ARGS], last_func_type, return_type;

   last_func_type = LLVMGetElementType(LLVMTypeOf(parts[num_parts - 1]));
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

   if (si_is_merged_shader(ctx->shader))
      ac_init_exec_full_mask(&ctx->ac);

   /* Record the arguments of the function as if they were an output of
    * a previous part.
    */
   num_out = 0;
   num_out_sgpr = 0;

   for (unsigned i = 0; i < ctx->args.arg_count; ++i) {
      LLVMValueRef param = LLVMGetParam(ctx->main_fn, i);
      LLVMTypeRef param_type = LLVMTypeOf(param);
      LLVMTypeRef out_type = ctx->args.args[i].file == AC_ARG_SGPR ? ctx->ac.i32 : ctx->ac.f32;
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

      if (ctx->args.args[i].file == AC_ARG_SGPR)
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
      unsigned num_params = LLVMCountParams(parts[part]);

      /* Merged shaders are executed conditionally depending
       * on the number of enabled threads passed in the input SGPRs. */
      if (si_is_multi_part_shader(ctx->shader) && part == 0) {
         LLVMValueRef ena, count = initial[3];

         count = LLVMBuildAnd(builder, count, LLVMConstInt(ctx->ac.i32, 0x7f, 0), "");
         ena = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), count, "");
         ac_build_ifcc(&ctx->ac, ena, 6506);
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

         param = LLVMGetParam(parts[part], param_idx);
         param_type = LLVMTypeOf(param);
         param_size = ac_get_type_size(param_type) / 4;
         is_sgpr = ac_is_sgpr_param(param);

         if (is_sgpr) {
            ac_add_function_attr(ctx->ac.context, parts[part], param_idx + 1, AC_FUNC_ATTR_INREG);
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

      ret = ac_build_call(&ctx->ac, parts[part], in, num_params);

      if (si_is_multi_part_shader(ctx->shader) && part + 1 == next_shader_first_part) {
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

   /* Return the value from the last part. */
   if (LLVMGetTypeKind(LLVMTypeOf(ret)) == LLVMVoidTypeKind)
      LLVMBuildRetVoid(builder);
   else
      LLVMBuildRet(builder, ret);
}
