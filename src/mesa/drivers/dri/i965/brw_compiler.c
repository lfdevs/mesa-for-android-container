/*
 * Copyright © 2015-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_compiler.h"
#include "brw_context.h"
#include "compiler/nir/nir.h"
#include "main/errors.h"
#include "util/debug.h"

static void
shader_debug_log_mesa(void *data, const char *fmt, ...)
{
   struct brw_context *brw = (struct brw_context *)data;
   va_list args;

   va_start(args, fmt);
   GLuint msg_id = 0;
   _mesa_gl_vdebug(&brw->ctx, &msg_id,
                   MESA_DEBUG_SOURCE_SHADER_COMPILER,
                   MESA_DEBUG_TYPE_OTHER,
                   MESA_DEBUG_SEVERITY_NOTIFICATION, fmt, args);
   va_end(args);
}

static void
shader_perf_log_mesa(void *data, const char *fmt, ...)
{
   struct brw_context *brw = (struct brw_context *)data;

   va_list args;
   va_start(args, fmt);

   if (unlikely(INTEL_DEBUG & DEBUG_PERF)) {
      va_list args_copy;
      va_copy(args_copy, args);
      vfprintf(stderr, fmt, args_copy);
      va_end(args_copy);
   }

   if (brw->perf_debug) {
      GLuint msg_id = 0;
      _mesa_gl_vdebug(&brw->ctx, &msg_id,
                      MESA_DEBUG_SOURCE_SHADER_COMPILER,
                      MESA_DEBUG_TYPE_PERFORMANCE,
                      MESA_DEBUG_SEVERITY_MEDIUM, fmt, args);
   }
   va_end(args);
}

#define COMMON_OPTIONS                                                        \
   /* In order to help allow for better CSE at the NIR level we tell NIR to   \
    * split all ffma instructions during opt_algebraic and we then re-combine \
    * them as a later step.                                                   \
    */                                                                        \
   .lower_ffma = true,                                                        \
   .lower_sub = true,                                                         \
   .lower_fdiv = true,                                                        \
   .lower_scmp = true,                                                        \
   .lower_fmod = true,                                                        \
   .lower_bitfield_extract = true,                                            \
   .lower_bitfield_insert = true,                                             \
   .lower_uadd_carry = true,                                                  \
   .lower_usub_borrow = true,                                                 \
   .lower_fdiv = true,                                                        \
   .native_integers = true

static const struct nir_shader_compiler_options scalar_nir_options = {
   COMMON_OPTIONS,
   .lower_pack_half_2x16 = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_unpack_half_2x16 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
};

static const struct nir_shader_compiler_options vector_nir_options = {
   COMMON_OPTIONS,

   /* In the vec4 backend, our dpN instruction replicates its result to all the
    * components of a vec4.  We would like NIR to give us replicated fdot
    * instructions because it can optimize better for us.
    */
   .fdot_replicates = true,

   .lower_pack_snorm_2x16 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
};

struct brw_compiler *
brw_compiler_create(void *mem_ctx, const struct brw_device_info *devinfo)
{
   struct brw_compiler *compiler = rzalloc(mem_ctx, struct brw_compiler);

   compiler->devinfo = devinfo;
   compiler->shader_debug_log = shader_debug_log_mesa;
   compiler->shader_perf_log = shader_perf_log_mesa;

   brw_fs_alloc_reg_sets(compiler);
   brw_vec4_alloc_reg_set(compiler);

   compiler->scalar_stage[MESA_SHADER_VERTEX] =
      devinfo->gen >= 8 && !(INTEL_DEBUG & DEBUG_VEC4VS);
   compiler->scalar_stage[MESA_SHADER_TESS_CTRL] = false;
   compiler->scalar_stage[MESA_SHADER_TESS_EVAL] =
      devinfo->gen >= 8 && env_var_as_boolean("INTEL_SCALAR_TES", true);
   compiler->scalar_stage[MESA_SHADER_GEOMETRY] =
      devinfo->gen >= 8 && env_var_as_boolean("INTEL_SCALAR_GS", false);
   compiler->scalar_stage[MESA_SHADER_FRAGMENT] = true;
   compiler->scalar_stage[MESA_SHADER_COMPUTE] = true;

   /* We want the GLSL compiler to emit code that uses condition codes */
   for (int i = 0; i < MESA_SHADER_STAGES; i++) {
      compiler->glsl_compiler_options[i].MaxUnrollIterations = 32;
      compiler->glsl_compiler_options[i].MaxIfDepth =
         devinfo->gen < 6 ? 16 : UINT_MAX;

      compiler->glsl_compiler_options[i].EmitCondCodes = true;
      compiler->glsl_compiler_options[i].EmitNoNoise = true;
      compiler->glsl_compiler_options[i].EmitNoMainReturn = true;
      compiler->glsl_compiler_options[i].EmitNoIndirectInput = true;
      compiler->glsl_compiler_options[i].EmitNoIndirectUniform = false;
      compiler->glsl_compiler_options[i].LowerClipDistance = true;

      bool is_scalar = compiler->scalar_stage[i];

      compiler->glsl_compiler_options[i].EmitNoIndirectOutput = is_scalar;
      compiler->glsl_compiler_options[i].EmitNoIndirectTemp = is_scalar;
      compiler->glsl_compiler_options[i].OptimizeForAOS = !is_scalar;

      /* !ARB_gpu_shader5 */
      if (devinfo->gen < 7)
         compiler->glsl_compiler_options[i].EmitNoIndirectSampler = true;

      compiler->glsl_compiler_options[i].NirOptions =
         is_scalar ? &scalar_nir_options : &vector_nir_options;

      compiler->glsl_compiler_options[i].LowerBufferInterfaceBlocks = true;
   }

   compiler->glsl_compiler_options[MESA_SHADER_TESS_CTRL].EmitNoIndirectInput = false;
   compiler->glsl_compiler_options[MESA_SHADER_TESS_EVAL].EmitNoIndirectInput = false;

   if (compiler->scalar_stage[MESA_SHADER_GEOMETRY])
      compiler->glsl_compiler_options[MESA_SHADER_GEOMETRY].EmitNoIndirectInput = false;

   compiler->glsl_compiler_options[MESA_SHADER_COMPUTE]
      .LowerShaderSharedVariables = true;

   return compiler;
}
