/*
 * Copyright (c) 2017-2019 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/u_memory.h"
#include "util/ralloc.h"
#include "util/u_debug.h"

#include "tgsi/tgsi_dump.h"
#include "compiler/nir/nir.h"
#include "nir/tgsi_to_nir.h"

#include "pipe/p_state.h"

#include "lima_screen.h"
#include "lima_context.h"
#include "lima_program.h"
#include "lima_bo.h"
#include "ir/lima_ir.h"

static const nir_shader_compiler_options vs_nir_options = {
   .lower_ffma = true,
   .lower_fpow = true,
   .lower_ffract = true,
   .lower_fdiv = true,
   .lower_fmod = true,
   .lower_fsqrt = true,
   .lower_sub = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_ftrunc = true,
   /* could be implemented by clamp */
   .lower_fsat = true,
   .lower_bitops = true,
   .lower_rotate = true,
   .lower_sincos = true,
   .lower_fceil = true,
};

static const nir_shader_compiler_options fs_nir_options = {
   .lower_ffma = true,
   .lower_fpow = true,
   .lower_fdiv = true,
   .lower_fmod = true,
   .lower_sub = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_fsign = true,
   .lower_rotate = true,
   .lower_fdot = true,
   .lower_bitops = true,
   .lower_vector_cmp = true,
};

static const struct nir_lower_tex_options tex_options = {
   .lower_txp = ~0u,
};

const void *
lima_program_get_compiler_options(enum pipe_shader_type shader)
{
   switch (shader) {
   case PIPE_SHADER_VERTEX:
      return &vs_nir_options;
   case PIPE_SHADER_FRAGMENT:
      return &fs_nir_options;
   default:
      return NULL;
   }
}

static int
type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

void
lima_program_optimize_vs_nir(struct nir_shader *s)
{
   bool progress;

   NIR_PASS_V(s, nir_lower_viewport_transform);
   NIR_PASS_V(s, nir_lower_io, nir_var_all, type_size, 0);
   NIR_PASS_V(s, nir_lower_load_const_to_scalar);
   NIR_PASS_V(s, lima_nir_lower_uniform_to_scalar);
   NIR_PASS_V(s, nir_lower_io_to_scalar,
              nir_var_shader_in|nir_var_shader_out);

   do {
      progress = false;

      NIR_PASS_V(s, nir_lower_vars_to_ssa);
      NIR_PASS(progress, s, nir_lower_alu_to_scalar, NULL, NULL);
      NIR_PASS(progress, s, nir_lower_phis_to_scalar);
      NIR_PASS(progress, s, nir_copy_prop);
      NIR_PASS(progress, s, nir_opt_remove_phis);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      NIR_PASS(progress, s, nir_opt_cse);
      NIR_PASS(progress, s, nir_opt_peephole_select, 8, true, true);
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_undef);
      NIR_PASS(progress, s, nir_opt_loop_unroll,
               nir_var_shader_in |
               nir_var_shader_out |
               nir_var_function_temp);
   } while (progress);

   NIR_PASS_V(s, nir_lower_int_to_float);
   /* Run opt_algebraic between int_to_float and bool_to_float because
    * int_to_float emits ftrunc, and ftrunc lowering generates bool ops
    */
   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_algebraic);
   } while (progress);

   NIR_PASS_V(s, nir_lower_bool_to_float);

   NIR_PASS_V(s, nir_copy_prop);
   NIR_PASS_V(s, nir_opt_dce);
   NIR_PASS_V(s, nir_lower_locals_to_regs);
   NIR_PASS_V(s, nir_convert_from_ssa, true);
   NIR_PASS_V(s, nir_remove_dead_variables, nir_var_function_temp);
   nir_sweep(s);
}

static bool
lima_alu_to_scalar_filter_cb(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   switch (alu->op) {
   case nir_op_frcp:
   case nir_op_frsq:
   case nir_op_flog2:
   case nir_op_fexp2:
   case nir_op_fsqrt:
   case nir_op_fsin:
   case nir_op_fcos:
      return true;
   default:
      break;
   }

   /* nir vec4 fcsel assumes that each component of the condition will be
    * used to select the same component from the two options, but Utgard PP
    * has only 1 component condition. If all condition components are not the
    * same we need to lower it to scalar.
    */
   switch (alu->op) {
   case nir_op_bcsel:
   case nir_op_fcsel:
      break;
   default:
      return false;
   }

   int num_components = nir_dest_num_components(alu->dest.dest);

   uint8_t swizzle = alu->src[0].swizzle[0];

   for (int i = 1; i < num_components; i++)
      if (alu->src[0].swizzle[i] != swizzle)
         return true;

   return false;
}

void
lima_program_optimize_fs_nir(struct nir_shader *s)
{
   bool progress;

   NIR_PASS_V(s, nir_lower_fragcoord_wtrans);
   NIR_PASS_V(s, nir_lower_io, nir_var_all, type_size, 0);
   NIR_PASS_V(s, nir_lower_regs_to_ssa);
   NIR_PASS_V(s, nir_lower_tex, &tex_options);

   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_vectorize);
   } while (progress);

   do {
      progress = false;

      NIR_PASS_V(s, nir_lower_vars_to_ssa);
      NIR_PASS(progress, s, nir_lower_alu_to_scalar, lima_alu_to_scalar_filter_cb, NULL);
      NIR_PASS(progress, s, nir_copy_prop);
      NIR_PASS(progress, s, nir_opt_remove_phis);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      NIR_PASS(progress, s, nir_opt_cse);
      NIR_PASS(progress, s, nir_opt_peephole_select, 8, true, true);
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_undef);
      NIR_PASS(progress, s, nir_opt_loop_unroll,
               nir_var_shader_in |
               nir_var_shader_out |
               nir_var_function_temp);
      NIR_PASS(progress, s, lima_nir_split_load_input);
   } while (progress);

   NIR_PASS_V(s, nir_lower_int_to_float);
   NIR_PASS_V(s, nir_lower_bool_to_float);

   /* Some ops must be lowered after being converted from int ops,
    * so re-run nir_opt_algebraic after int lowering. */
   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_algebraic);
   } while (progress);

   /* Must be run after optimization loop */
   NIR_PASS_V(s, lima_nir_scale_trig);

   /* Lower modifiers */
   NIR_PASS_V(s, nir_lower_to_source_mods, nir_lower_all_source_mods);
   NIR_PASS_V(s, nir_copy_prop);
   NIR_PASS_V(s, nir_opt_dce);

   NIR_PASS_V(s, nir_lower_locals_to_regs);
   NIR_PASS_V(s, nir_convert_from_ssa, true);
   NIR_PASS_V(s, nir_remove_dead_variables, nir_var_function_temp);

   NIR_PASS_V(s, nir_move_vec_src_uses_to_dest);
   NIR_PASS_V(s, nir_lower_vec_to_movs);

   nir_sweep(s);
}

static void *
lima_create_fs_state(struct pipe_context *pctx,
                     const struct pipe_shader_state *cso)
{
   struct lima_context *ctx = lima_context(pctx);
   struct lima_screen *screen = lima_screen(pctx->screen);
   struct lima_fs_shader_state *so = rzalloc(NULL, struct lima_fs_shader_state);

   if (!so)
      return NULL;

   nir_shader *nir;
   if (cso->type == PIPE_SHADER_IR_NIR)
      nir = cso->ir.nir;
   else {
      assert(cso->type == PIPE_SHADER_IR_TGSI);

      nir = tgsi_to_nir(cso->tokens, pctx->screen);
   }

   lima_program_optimize_fs_nir(nir);

   if (lima_debug & LIMA_DEBUG_PP)
      nir_print_shader(nir, stdout);

   if (!ppir_compile_nir(so, nir, screen->pp_ra, &ctx->debug)) {
      ralloc_free(so);
      return NULL;
   }

   return so;
}

static void
lima_bind_fs_state(struct pipe_context *pctx, void *hwcso)
{
   struct lima_context *ctx = lima_context(pctx);

   ctx->fs = hwcso;
   ctx->dirty |= LIMA_CONTEXT_DIRTY_SHADER_FRAG;
}

static void
lima_delete_fs_state(struct pipe_context *pctx, void *hwcso)
{
   struct lima_fs_shader_state *so = hwcso;

   if (so->bo)
      lima_bo_unreference(so->bo);

   ralloc_free(so);
}

bool
lima_update_vs_state(struct lima_context *ctx)
{
   struct lima_vs_shader_state *vs = ctx->vs;
   if (!vs->bo) {
      struct lima_screen *screen = lima_screen(ctx->base.screen);
      vs->bo = lima_bo_create(screen, vs->shader_size, 0);
      if (!vs->bo) {
         fprintf(stderr, "lima: create vs shader bo fail\n");
         return false;
      }

      memcpy(lima_bo_map(vs->bo), vs->shader, vs->shader_size);
      ralloc_free(vs->shader);
      vs->shader = NULL;
   }

   return true;
}

bool
lima_update_fs_state(struct lima_context *ctx)
{
   struct lima_fs_shader_state *fs = ctx->fs;
   if (!fs->bo) {
      struct lima_screen *screen = lima_screen(ctx->base.screen);
      fs->bo = lima_bo_create(screen, fs->shader_size, 0);
      if (!fs->bo) {
         fprintf(stderr, "lima: create fs shader bo fail\n");
         return false;
      }

      memcpy(lima_bo_map(fs->bo), fs->shader, fs->shader_size);
      ralloc_free(fs->shader);
      fs->shader = NULL;
   }

   ctx->pp_max_stack_size = MAX2(ctx->pp_max_stack_size, ctx->fs->stack_size);

   return true;
}

static void *
lima_create_vs_state(struct pipe_context *pctx,
                     const struct pipe_shader_state *cso)
{
   struct lima_context *ctx = lima_context(pctx);
   struct lima_vs_shader_state *so = rzalloc(NULL, struct lima_vs_shader_state);

   if (!so)
      return NULL;

   nir_shader *nir;
   if (cso->type == PIPE_SHADER_IR_NIR)
      nir = cso->ir.nir;
   else {
      assert(cso->type == PIPE_SHADER_IR_TGSI);

      nir = tgsi_to_nir(cso->tokens, pctx->screen);
   }

   lima_program_optimize_vs_nir(nir);

   if (lima_debug & LIMA_DEBUG_GP)
      nir_print_shader(nir, stdout);

   if (!gpir_compile_nir(so, nir, &ctx->debug)) {
      ralloc_free(so);
      return NULL;
   }

   return so;
}

static void
lima_bind_vs_state(struct pipe_context *pctx, void *hwcso)
{
   struct lima_context *ctx = lima_context(pctx);

   ctx->vs = hwcso;
   ctx->dirty |= LIMA_CONTEXT_DIRTY_SHADER_VERT;
}

static void
lima_delete_vs_state(struct pipe_context *pctx, void *hwcso)
{
   struct lima_vs_shader_state *so = hwcso;

   if (so->bo)
      lima_bo_unreference(so->bo);

   ralloc_free(so);
}

void
lima_program_init(struct lima_context *ctx)
{
   ctx->base.create_fs_state = lima_create_fs_state;
   ctx->base.bind_fs_state = lima_bind_fs_state;
   ctx->base.delete_fs_state = lima_delete_fs_state;

   ctx->base.create_vs_state = lima_create_vs_state;
   ctx->base.bind_vs_state = lima_bind_vs_state;
   ctx->base.delete_vs_state = lima_delete_vs_state;
}
