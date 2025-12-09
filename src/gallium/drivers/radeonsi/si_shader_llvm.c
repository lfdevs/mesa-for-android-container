/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_debug.h"
#include "ac_nir.h"
#include "ac_nir_to_llvm.h"
#include "ac_rtld.h"
#include "nir.h"
#include "si_pipe.h"
#include "si_shader_internal.h"
#include "si_shader_llvm.h"
#include "sid.h"
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
      mesa_loge("LLVM triggered Diagnostic Handler: %s", description);
   }

   LLVMDisposeMessage(description);
}

static bool si_compile_llvm(struct si_screen *sscreen, struct si_shader_binary *binary,
                            struct ac_shader_config *conf, struct ac_llvm_compiler *compiler,
                            struct ac_llvm_context *ac, struct util_debug_callback *debug,
                            mesa_shader_stage stage, const char *name)
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
      struct ac_backend_optimizer *beo = compiler->beo;

      struct si_llvm_diagnostics diag = {debug};
      LLVMContextSetDiagnosticHandler(ac->context, si_diagnostic_handler, &diag);

      if (!ac_compile_module_to_elf(beo, ac->module, (char **)&binary->code_buffer,
                                    &binary->code_size))
         diag.retval = 1;

      if (diag.retval != 0) {
         util_debug_message(debug, SHADER_INFO, "LLVM compilation failed");
         return false;
      }

      binary->type = SI_SHADER_BINARY_ELF;
   }

   struct ac_rtld_binary rtld;
   if (!ac_rtld_open(&rtld, (struct ac_rtld_open_info){
                               .info = &sscreen->info,
                               .shader_type = stage,
                               .wave_size = ac->wave_size,
                               .num_parts = 1,
                               .elf_ptrs = &binary->code_buffer,
                               .elf_sizes = &binary->code_size}))
      return false;

   bool ok = ac_rtld_read_config(&sscreen->info, &rtld, conf);
   ac_rtld_close(&rtld);
   return ok;
}

static void si_llvm_context_init(struct si_shader_context *ctx, struct si_screen *sscreen,
                                 struct ac_llvm_compiler *compiler, unsigned wave_size,
                                 bool exports_color_null, bool exports_mrtz,
                                 enum ac_float_mode float_mode)
{
   memset(ctx, 0, sizeof(*ctx));
   ctx->screen = sscreen;
   ctx->compiler = compiler;

   ac_llvm_context_init(&ctx->ac, compiler, &sscreen->info, float_mode,
                        wave_size, exports_color_null, exports_mrtz);
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

   mesa_shader_stage real_stage = ctx->stage;

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
   case MESA_SHADER_KERNEL:
      call_conv = AC_LLVM_AMDGPU_CS;
      break;
   default:
      UNREACHABLE("Unhandle shader type");
   }

   /* Setup the function */
   ctx->return_type = ret_type;
   ctx->main_fn = ac_build_main(&ctx->args->ac, &ctx->ac, call_conv, name, ret_type, ctx->ac.module);
   ctx->return_value = LLVMGetUndef(ctx->return_type);

   if (ctx->screen->info.address32_hi) {
      ac_llvm_add_target_dep_function_attr(ctx->main_fn.value, "amdgpu-32bit-address-high-bits",
                                           ctx->screen->info.address32_hi);
   }

   ac_llvm_set_workgroup_size(ctx->main_fn.value, max_workgroup_size);
   ac_llvm_set_target_features(ctx->main_fn.value, &ctx->ac, false);
}

static void si_llvm_create_main_func(struct si_shader_context *ctx)
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
         ctx->main_fn.value, "InitialPSInputAddr", SI_SPI_PS_INPUT_ADDR_FOR_PROLOG);
   }
}

static void si_llvm_optimize_module(struct si_shader_context *ctx)
{
   /* Dump LLVM IR before any optimization passes */
   if (si_can_dump_shader(ctx->screen, ctx->stage, SI_DUMP_INIT_LLVM_IR))
      ac_dump_module(ctx->ac.module);

   /* Run the pass */
   ac_llvm_optimize_module(ctx->compiler->meo, ctx->ac.module);
}

static void si_llvm_dispose(struct si_shader_context *ctx)
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

LLVMValueRef si_prolog_get_internal_binding_slot(struct si_shader_context *ctx, unsigned slot)
{
   LLVMValueRef list = LLVMBuildIntToPtr(
      ctx->ac.builder, ac_get_arg(&ctx->ac, ctx->args->internal_bindings),
      LLVMPointerTypeInContext(ctx->ac.context, AC_ADDR_SPACE_CONST_32BIT), "");
   LLVMValueRef index = LLVMConstInt(ctx->ac.i32, slot, 0);

   return ac_build_load_to_sgpr(&ctx->ac,
                                (struct ac_llvm_pointer) { .t = ctx->ac.v4i32, .v = list },
                                index);
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

/**
 * Given two parts (LS/HS or ES/GS) of a merged shader, build a wrapper function that
 * runs them in sequence to form a monolithic shader.
 */
static void si_build_wrapper_function(struct si_shader_context *ctx,
                                      struct ac_llvm_pointer parts[2],
                                      bool same_thread_count)
{
   LLVMBuilderRef builder = ctx->ac.builder;

   for (unsigned i = 0; i < 2; ++i) {
      ac_add_function_attr(ctx->ac.context, parts[i].value, -1, "alwaysinline");
      LLVMSetLinkage(parts[i].value, LLVMPrivateLinkage);
   }

   si_llvm_create_func(ctx, "wrapper", NULL, 0, si_get_max_workgroup_size(ctx->shader));
   ac_init_exec_full_mask(&ctx->ac);

   LLVMValueRef count = ac_get_arg(&ctx->ac, ctx->args->ac.merged_wave_info);
   count = LLVMBuildAnd(builder, count, LLVMConstInt(ctx->ac.i32, 0x7f, 0), "");

   LLVMValueRef ena = LLVMBuildICmp(builder, LLVMIntULT, ac_get_thread_id(&ctx->ac), count, "");
   ac_build_ifcc(&ctx->ac, ena, 6506);

   LLVMValueRef params[AC_MAX_ARGS];
   unsigned num_params = LLVMCountParams(ctx->main_fn.value);
   LLVMGetParams(ctx->main_fn.value, params);

   /* wrapper function has same parameter as first part shader */
   LLVMValueRef ret =
      ac_build_call(&ctx->ac, parts[0].pointee_type, parts[0].value, params, num_params);

   if (LLVMGetTypeKind(LLVMTypeOf(ret)) != LLVMVoidTypeKind) {
      LLVMValueRef ret_var = ac_build_alloca_undef(&ctx->ac, LLVMTypeOf(ret), "");
      LLVMBuildStore(builder, ret, ret_var);
      ac_build_endif(&ctx->ac, 6506);

      ret = LLVMBuildLoad2(builder, LLVMTypeOf(ret), ret_var, "");
   } else {
      ac_build_endif(&ctx->ac, 6506);
   }

   if (same_thread_count) {
      LLVMTypeRef type = LLVMTypeOf(ret);
      assert(LLVMGetTypeKind(type) == LLVMStructTypeKind);

      /* output of first part shader is the input of the second part */
      num_params = LLVMCountStructElementTypes(type);
      assert(num_params == LLVMCountParams(parts[1].value));

      for (unsigned i = 0; i < num_params; i++) {
         LLVMValueRef ret_value = LLVMBuildExtractValue(builder, ret, i, "");;
         LLVMTypeRef ret_type = LLVMTypeOf(ret_value);
         LLVMTypeRef param_type = LLVMTypeOf(LLVMGetParam(parts[1].value, i));

         assert(ac_get_type_size(ret_type) == 4);
         assert(ac_get_type_size(param_type) == 4);

         if (ret_type == ctx->ac.f32) {
            /* Returned VGPRs only: Pass the returned value to the next shader. */
            params[i] = LLVMBuildBitCast(builder, ret_value, param_type, "");
         } else {
            /* Use input SGPRs from the wrapper function params instead of the return value of
             * the previous shader.
             */
            assert(ret_type == ctx->ac.i32);
         }
      }
   } else {
      /* The second half of the merged shader should use
       * the inputs from the toplevel (wrapper) function,
       * not the return value from the last call.
       *
       * That's because the last call was executed condi-
       * tionally, so we can't consume it in the main
       * block.
       */

      /* Second part params are same as the preceding params of the first part. */
      num_params = LLVMCountParams(parts[1].value);
   }

   ac_build_call(&ctx->ac, parts[1].pointee_type, parts[1].value, params, num_params);
   LLVMBuildRetVoid(builder);
}

static LLVMValueRef si_llvm_load_sampler_desc(struct ac_shader_abi *abi, LLVMValueRef index,
                                              enum ac_descriptor_type desc_type)
{
   struct si_shader_context *ctx = si_shader_context_from_abi(abi);
   LLVMBuilderRef builder = ctx->ac.builder;

   /* This is only used by divergent sampler and image indexing to build the waterfall loop. */
   if (index && LLVMTypeOf(index) == ctx->ac.i32) {
      bool is_vec4 = false;

      switch (desc_type) {
      case AC_DESC_IMAGE:
         /* The image is at [0:7]. */
         index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->ac.i32, 2, 0), "");
         break;
      case AC_DESC_BUFFER:
         /* The buffer is in [0:3]. */
         index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->ac.i32, 4, 0), "");
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
         UNREACHABLE("invalid desc");
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
                                  struct nir_shader *nir)
{
   struct si_shader_selector *sel = shader->selector;
   const struct si_shader_info *info = &sel->info;

   ctx->shader = shader;
   ctx->stage = shader->is_gs_copy_shader ? MESA_SHADER_VERTEX : nir->info.stage;

   ctx->abi.load_sampler_desc = si_llvm_load_sampler_desc;

   si_llvm_create_main_func(ctx);

   switch (ctx->stage) {
   case MESA_SHADER_TESS_CTRL:
      si_llvm_init_tcs_callbacks(ctx);
      break;

   case MESA_SHADER_FRAGMENT: {
      ctx->abi.kill_ps_if_inf_interp =
         ctx->screen->options.no_infinite_interp &&
         (ctx->shader->selector->info.uses_persp_center ||
          ctx->shader->selector->info.uses_persp_centroid ||
          ctx->shader->selector->info.uses_persp_sample);
      break;
   }

   default:
      break;
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
            ac_init_exec_full_mask(&ctx->ac);
         }
      }

      /* NGG VS and NGG TES: nir ngg lowering send gs_alloc_req at the beginning when culling
       * is disabled, but GFX10 may hang if not all waves are launched before gs_alloc_req.
       * We work around this HW bug by inserting a barrier before gs_alloc_req.
       */
      if (ctx->screen->info.gfx_level == GFX10 &&
          (ctx->stage == MESA_SHADER_VERTEX || ctx->stage == MESA_SHADER_TESS_EVAL) &&
          shader->key.ge.as_ngg && !shader->key.ge.as_es && !si_shader_culling_enabled(shader))
         ac_build_s_barrier(&ctx->ac, ctx->stage);

      LLVMValueRef thread_enabled = NULL;

      if (ctx->stage == MESA_SHADER_GEOMETRY && !shader->key.ge.as_ngg) {
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
         ac_build_ifcc(&ctx->ac, thread_enabled, SI_MERGED_WRAP_IF_LABEL);
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
             shader->selector->info.tcs_inputs_via_lds) {
            ac_build_waitcnt(&ctx->ac, AC_WAIT_DS);

            /* If both input and output patches are wholly in one wave, we don't need a barrier.
             * That's true when both VS and TCS have the same number of patch vertices and
             * the wave size is a multiple of the number of patch vertices.
             */
            if (!shader->key.ge.opt.same_patch_vertices ||
                ctx->ac.wave_size % nir->info.tess.tcs_vertices_out != 0)
               ac_build_s_barrier(&ctx->ac, ctx->stage);
         }
      } else if (ctx->stage == MESA_SHADER_GEOMETRY) {
         ac_build_waitcnt(&ctx->ac, AC_WAIT_DS);
         ac_build_s_barrier(&ctx->ac, ctx->stage);
      }
   }

   ctx->abi.clamp_shadow_reference = true;
   ctx->abi.robust_buffer_access = true;
   ctx->abi.load_grid_size_from_user_sgpr = true;
   ctx->abi.clamp_div_by_zero = ctx->screen->options.clamp_div_by_zero ||
                                info->options & SI_PROFILE_CLAMP_DIV_BY_ZERO;
   ctx->abi.disable_aniso_single_level = true;

   if (!ac_nir_translate(&ctx->ac, &ctx->abi, &ctx->args->ac, nir))
      return false;

   switch (ctx->stage) {
   case MESA_SHADER_VERTEX:
      if (shader->key.ge.as_ls)
         si_llvm_ls_build_end(ctx);
      else if (shader->key.ge.as_es)
         si_llvm_es_build_end(ctx);
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
   return true;
}

static void assert_registers_equal(struct si_screen *sscreen, unsigned reg, unsigned nir_value,
                                   unsigned llvm_value, bool allow_zero)
{
   if (nir_value != llvm_value) {
      mesa_loge("Unexpected non-matching shader config:");
      fprintf(stderr, "From NIR:\n");
      ac_dump_reg(stderr, sscreen->info.gfx_level, sscreen->info.family, reg, nir_value, ~0);
      fprintf(stderr, "From LLVM:\n");
      ac_dump_reg(stderr, sscreen->info.gfx_level, sscreen->info.family, reg, llvm_value, ~0);
   }
   if (0)
      printf("nir_value = 0x%x, llvm_value = 0x%x\n", nir_value, llvm_value);
   assert(nir_value || allow_zero);
   assert(llvm_value || allow_zero);
   assert(nir_value == llvm_value);
}

bool si_llvm_compile_shader(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                            struct si_shader *shader, struct si_linked_shaders *linked,
                            struct util_debug_callback *debug)
{
   struct si_shader_selector *sel = shader->selector;
   struct si_shader_context ctx;
   nir_shader *nir = linked->consumer.nir;
   enum ac_float_mode float_mode = nir->info.stage == MESA_SHADER_KERNEL ?
                                       AC_FLOAT_MODE_DEFAULT : AC_FLOAT_MODE_DEFAULT_OPENGL;
   bool exports_color_null = false;
   bool exports_mrtz = false;

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      exports_color_null = sel->info.colors_written;
      exports_mrtz = shader->info.writes_z || shader->info.writes_stencil ||
                     shader->info.writes_sample_mask ||
                     shader->key.ps.part.epilog.alpha_to_coverage_via_mrtz;
      if (!exports_mrtz && !exports_color_null)
         exports_color_null = shader->info.uses_discard || sscreen->info.gfx_level < GFX10;
   }

   si_llvm_context_init(&ctx, sscreen, compiler, shader->wave_size, exports_color_null, exports_mrtz,
                        float_mode);
   ctx.args = &linked->consumer.args;

   if (!si_llvm_translate_nir(&ctx, shader, nir)) {
      si_llvm_dispose(&ctx);
      return false;
   }

   /* For merged shader stage. */
   if (linked->producer.nir) {
      /* LS or ES shader. */
      ctx.args = &linked->producer.args;

      struct ac_llvm_pointer parts[2];
      parts[1] = ctx.main_fn;

      if (!si_llvm_translate_nir(&ctx, linked->producer.shader, linked->producer.nir)) {
         si_llvm_dispose(&ctx);
         return false;
      }

      parts[0] = ctx.main_fn;

      /* Reset the shader context. */
      ctx.shader = shader;
      ctx.stage = nir->info.stage;

      bool same_thread_count = shader->key.ge.opt.same_patch_vertices;
      si_build_wrapper_function(&ctx, parts, same_thread_count);
   }

   si_llvm_optimize_module(&ctx);

   /* Make sure the input is a pointer and not integer followed by inttoptr. */
   assert(LLVMGetTypeKind(LLVMTypeOf(LLVMGetParam(ctx.main_fn.value, 0))) == LLVMPointerTypeKind);

   /* Compile to bytecode. */
   struct ac_shader_config config = {0};

   bool success = si_compile_llvm(sscreen, &shader->binary, &config, compiler, &ctx.ac, debug,
                                  nir->info.stage, si_get_shader_name(shader));
   si_llvm_dispose(&ctx);
   if (!success) {
      mesa_loge("LLVM failed to compile shader");
      return false;
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      assert_registers_equal(sscreen, R_0286CC_SPI_PS_INPUT_ENA, shader->config.spi_ps_input_ena,
                             config.spi_ps_input_ena, !shader->is_monolithic);
      assert_registers_equal(sscreen, R_0286D0_SPI_PS_INPUT_ADDR, shader->config.spi_ps_input_addr,
                             config.spi_ps_input_addr, false);
   }
   shader->config = config;
   return true;
}

bool si_llvm_build_shader_part(struct si_screen *sscreen, mesa_shader_stage stage,
                               bool prolog, struct ac_llvm_compiler *compiler,
                               struct util_debug_callback *debug, const char *name,
                               struct si_shader_part *result)
{
   union si_shader_part_key *key = &result->key;

   struct si_shader_selector sel = {};
   sel.screen = sscreen;

   struct si_shader shader = {};
   shader.selector = &sel;
   bool wave32 = false;
   bool exports_color_null = false;
   bool exports_mrtz = false;

   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      if (prolog) {
         shader.key.ps.part.prolog = key->ps_prolog.states;
         wave32 = key->ps_prolog.wave32;
         exports_color_null = key->ps_prolog.states.poly_stipple;
      } else {
         shader.key.ps.part.epilog = key->ps_epilog.states;
         wave32 = key->ps_epilog.wave32;
         exports_color_null = key->ps_epilog.colors_written;
         exports_mrtz = (key->ps_epilog.writes_z && !key->ps_epilog.states.kill_z) ||
                        (key->ps_epilog.writes_stencil && !key->ps_epilog.states.kill_stencil) ||
                        (key->ps_epilog.writes_samplemask && !key->ps_epilog.states.kill_samplemask);
         if (!exports_mrtz && !exports_color_null)
            exports_color_null = key->ps_epilog.uses_discard || sscreen->info.gfx_level < GFX10;
      }
      break;
   default:
      UNREACHABLE("bad shader part");
   }

   struct si_shader_context ctx;
   si_llvm_context_init(&ctx, sscreen, compiler, wave32 ? 32 : 64, exports_color_null, exports_mrtz,
                        AC_FLOAT_MODE_DEFAULT_OPENGL);

   ctx.shader = &shader;
   ctx.stage = stage;

   struct si_shader_args args;
   ctx.args = &args;

   void (*build)(struct si_shader_context *, union si_shader_part_key *);

   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      build = prolog ? si_llvm_build_ps_prolog : si_llvm_build_ps_epilog;
      break;
   default:
      UNREACHABLE("bad shader part");
   }

   build(&ctx, key);

   /* Compile. */
   si_llvm_optimize_module(&ctx);

   struct ac_shader_config config = {0};
   bool ret = si_compile_llvm(sscreen, &result->binary, &config, compiler,
                              &ctx.ac, debug, ctx.stage, name);
   result->num_vgprs = config.num_vgprs;
   result->num_sgprs = config.num_sgprs;

   si_llvm_dispose(&ctx);
   return ret;
}
