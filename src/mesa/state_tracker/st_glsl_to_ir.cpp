/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2010 Intel Corporation
 * Copyright © 2011 Bryan Cain
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "compiler/glsl/glsl_parser_extras.h"
#include "compiler/glsl/ir_optimization.h"
#include "compiler/glsl/program.h"

#include "st_nir.h"
#include "st_shader_cache.h"
#include "st_program.h"

static GLboolean
link_shader(struct gl_context *ctx, struct gl_shader_program *prog)
{
   GLboolean ret;
   struct st_context *sctx = st_context(ctx);
   struct pipe_screen *pscreen = sctx->screen;

   /* Return early if we are loading the shader from on-disk cache */
   if (st_load_nir_from_disk_cache(ctx, prog)) {
      return GL_TRUE;
   }

   assert(prog->data->LinkStatus);

   /* Skip the GLSL steps when using SPIR-V. */
   if (prog->data->spirv) {
      return st_link_nir(ctx, prog);
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (prog->_LinkedShaders[i] == NULL)
         continue;

      struct gl_linked_shader *shader = prog->_LinkedShaders[i];
      exec_list *ir = shader->ir;
      gl_shader_stage stage = shader->Stage;
      const struct gl_shader_compiler_options *options =
            &ctx->Const.ShaderCompilerOptions[stage];

      enum pipe_shader_type ptarget = pipe_shader_type_from_mesa(stage);
      bool have_dround = pscreen->get_shader_param(pscreen, ptarget,
                                                   PIPE_SHADER_CAP_DROUND_SUPPORTED);

      if (!pscreen->get_param(pscreen, PIPE_CAP_INT64_DIVMOD))
         lower_64bit_integer_instructions(ir, DIV64 | MOD64);

      lower_packing_builtins(ir, ctx->Extensions.ARB_shading_language_packing,
                             ctx->Extensions.ARB_gpu_shader5,
                             ctx->st->has_half_float_packing);
      do_mat_op_to_vec(ir);

      if (stage == MESA_SHADER_FRAGMENT && pscreen->get_param(pscreen, PIPE_CAP_FBFETCH))
         lower_blend_equation_advanced(
            shader, ctx->Extensions.KHR_blend_equation_advanced_coherent);

      lower_instructions(ir, have_dround,
                         ctx->Extensions.ARB_gpu_shader5);

      do_vec_index_to_cond_assign(ir);
      if (options->MaxIfDepth == 0) {
         lower_discard(ir);
      }

      validate_ir_tree(ir);
   }

   ret = st_link_nir(ctx, prog);

   return ret;
}

extern "C" {

/**
 * Link a shader.
 */
GLboolean
st_link_shader(struct gl_context *ctx, struct gl_shader_program *prog)
{
   struct pipe_context *pctx = st_context(ctx)->pipe;

   GLboolean ret = link_shader(ctx, prog);
    
   if (pctx->link_shader) {
      void *driver_handles[PIPE_SHADER_TYPES];
      memset(driver_handles, 0, sizeof(driver_handles));

      for (uint32_t i = 0; i < MESA_SHADER_STAGES; ++i) {
         struct gl_linked_shader *shader = prog->_LinkedShaders[i];
         if (shader) {
            struct gl_program *p = shader->Program;
            if (p && p->variants) {
               enum pipe_shader_type type = pipe_shader_type_from_mesa(shader->Stage);
               driver_handles[type] = p->variants->driver_shader;
            }
         }
      }

      pctx->link_shader(pctx, driver_handles);
   }

   return ret;
}

} /* extern "C" */
