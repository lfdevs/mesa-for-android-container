/*
 * Copyright (c) 2012-2015 Etnaviv Project
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
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#include "etnaviv_shader.h"

#include "etnaviv_compiler.h"
#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_util.h"

#include "util/u_math.h"
#include "util/u_memory.h"

/* Link vs and fs together: fill in shader_state from vs and fs
 * as this function is called every time a new fs or vs is bound, the goal is to
 * do little processing as possible here, and to precompute as much as possible in
 * the vs/fs shader_object.
 *
 * XXX we could cache the link result for a certain set of VS/PS; usually a pair
 * of VS and PS will be used together anyway.
 */
static bool
etna_link_shaders(struct etna_context *ctx, struct compiled_shader_state *cs,
                  const struct etna_shader *vs, const struct etna_shader *fs)
{
   struct etna_shader_link_info link = { };

   assert(vs->processor == PIPE_SHADER_VERTEX);
   assert(fs->processor == PIPE_SHADER_FRAGMENT);

#ifdef DEBUG
   if (DBG_ENABLED(ETNA_DBG_DUMP_SHADERS)) {
      etna_dump_shader(vs);
      etna_dump_shader(fs);
   }
#endif

   if (etna_link_shader(&link, vs, fs)) {
      /* linking failed: some fs inputs do not have corresponding
       * vs outputs */
      assert(0);

      return false;
   }

   if (DBG_ENABLED(ETNA_DBG_LINKER_MSGS)) {
      debug_printf("link result:\n");
      debug_printf("  vs  -> fs  comps use     pa_attr\n");

      for (int idx = 0; idx < link.num_varyings; ++idx)
         debug_printf("  t%-2u -> t%-2u %-5.*s %u,%u,%u,%u 0x%08x\n",
                      link.varyings[idx].reg, idx + 1,
                      link.varyings[idx].num_components, "xyzw",
                      link.varyings[idx].use[0], link.varyings[idx].use[1],
                      link.varyings[idx].use[2], link.varyings[idx].use[3],
                      link.varyings[idx].pa_attributes);
   }

   /* set last_varying_2x flag if the last varying has 1 or 2 components */
   bool last_varying_2x = false;
   if (link.num_varyings > 0 && link.varyings[link.num_varyings - 1].num_components <= 2)
      last_varying_2x = true;

   cs->RA_CONTROL = VIVS_RA_CONTROL_UNK0 |
                    COND(last_varying_2x, VIVS_RA_CONTROL_LAST_VARYING_2X);

   cs->PA_ATTRIBUTE_ELEMENT_COUNT = VIVS_PA_ATTRIBUTE_ELEMENT_COUNT_COUNT(link.num_varyings);
   for (int idx = 0; idx < link.num_varyings; ++idx)
      cs->PA_SHADER_ATTRIBUTES[idx] = link.varyings[idx].pa_attributes;

   cs->VS_END_PC = vs->code_size / 4;
   cs->VS_OUTPUT_COUNT = 1 + link.num_varyings; /* position + varyings */

   /* vs outputs (varyings) */
   DEFINE_ETNA_BITARRAY(vs_output, 16, 8) = {0};
   int varid = 0;
   etna_bitarray_set(vs_output, 8, varid++, vs->vs_pos_out_reg);
   for (int idx = 0; idx < link.num_varyings; ++idx)
      etna_bitarray_set(vs_output, 8, varid++, link.varyings[idx].reg);
   if (vs->vs_pointsize_out_reg >= 0)
      etna_bitarray_set(vs_output, 8, varid++, vs->vs_pointsize_out_reg); /* pointsize is last */

   for (int idx = 0; idx < ARRAY_SIZE(cs->VS_OUTPUT); ++idx)
      cs->VS_OUTPUT[idx] = vs_output[idx];

   if (vs->vs_pointsize_out_reg != -1) {
      /* vertex shader outputs point coordinate, provide extra output and make
       * sure PA config is
       * not masked */
      cs->PA_CONFIG = ~0;
      cs->VS_OUTPUT_COUNT_PSIZE = cs->VS_OUTPUT_COUNT + 1;
   } else {
      /* vertex shader does not output point coordinate, make sure thate
       * POINT_SIZE_ENABLE is masked
       * and no extra output is given */
      cs->PA_CONFIG = ~VIVS_PA_CONFIG_POINT_SIZE_ENABLE;
      cs->VS_OUTPUT_COUNT_PSIZE = cs->VS_OUTPUT_COUNT;
   }

   cs->VS_LOAD_BALANCING = vs->vs_load_balancing;
   cs->VS_START_PC = 0;

   cs->PS_END_PC = fs->code_size / 4;
   cs->PS_OUTPUT_REG = fs->ps_color_out_reg;
   cs->PS_INPUT_COUNT =
      VIVS_PS_INPUT_COUNT_COUNT(link.num_varyings + 1) | /* Number of inputs plus position */
      VIVS_PS_INPUT_COUNT_UNK8(fs->input_count_unk8);
   cs->PS_TEMP_REGISTER_CONTROL =
      VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(MAX2(fs->num_temps, link.num_varyings + 1));
   cs->PS_CONTROL = VIVS_PS_CONTROL_UNK1; /* XXX when can we set BYPASS? */
   cs->PS_START_PC = 0;

   /* Precompute PS_INPUT_COUNT and TEMP_REGISTER_CONTROL in the case of MSAA
    * mode, avoids some fumbling in sync_context. */
   cs->PS_INPUT_COUNT_MSAA =
      VIVS_PS_INPUT_COUNT_COUNT(link.num_varyings + 2) | /* MSAA adds another input */
      VIVS_PS_INPUT_COUNT_UNK8(fs->input_count_unk8);
   cs->PS_TEMP_REGISTER_CONTROL_MSAA =
      VIVS_PS_TEMP_REGISTER_CONTROL_NUM_TEMPS(MAX2(fs->num_temps, link.num_varyings + 2));

   uint32_t total_components = 0;
   DEFINE_ETNA_BITARRAY(num_components, ETNA_NUM_VARYINGS, 4) = {0};
   DEFINE_ETNA_BITARRAY(component_use, 4 * ETNA_NUM_VARYINGS, 2) = {0};
   for (int idx = 0; idx < link.num_varyings; ++idx) {
      const struct etna_varying *varying = &link.varyings[idx];

      etna_bitarray_set(num_components, 4, idx, varying->num_components);
      for (int comp = 0; comp < varying->num_components; ++comp) {
         etna_bitarray_set(component_use, 2, total_components, varying->use[comp]);
         total_components += 1;
      }
   }

   cs->GL_VARYING_TOTAL_COMPONENTS =
      VIVS_GL_VARYING_TOTAL_COMPONENTS_NUM(align(total_components, 2));
   cs->GL_VARYING_NUM_COMPONENTS = num_components[0];
   cs->GL_VARYING_COMPONENT_USE[0] = component_use[0];
   cs->GL_VARYING_COMPONENT_USE[1] = component_use[1];

   /* reference instruction memory */
   cs->vs_inst_mem_size = vs->code_size;
   cs->VS_INST_MEM = vs->code;
   cs->ps_inst_mem_size = fs->code_size;
   cs->PS_INST_MEM = fs->code;

   return true;
}

bool
etna_shader_link(struct etna_context *ctx)
{
   if (!ctx->vs || !ctx->fs)
      return false;

   /* re-link vs and fs if needed */
   return etna_link_shaders(ctx, &ctx->shader_state, ctx->vs, ctx->fs);
}

static bool
etna_shader_update_vs_inputs(struct etna_context *ctx,
                             struct compiled_shader_state *cs,
                             const struct etna_shader *vs,
                             const struct compiled_vertex_elements_state *ves)
{
   unsigned num_temps, cur_temp, num_vs_inputs;

   if (!vs)
      return false;

   /* Number of vertex elements determines number of VS inputs. Otherwise,
    * the GPU crashes. Allocate any unused vertex elements to VS temporary
    * registers. */
   num_vs_inputs = MAX2(ves->num_elements, vs->infile.num_reg);
   if (num_vs_inputs != ves->num_elements) {
      BUG("Number of elements %u does not match the number of VS inputs %zu",
          ctx->vertex_elements->num_elements, ctx->vs->infile.num_reg);
      return false;
   }

   cur_temp = vs->num_temps;
   num_temps = num_vs_inputs - vs->infile.num_reg + cur_temp;

   cs->VS_INPUT_COUNT = VIVS_VS_INPUT_COUNT_COUNT(num_vs_inputs) |
                        VIVS_VS_INPUT_COUNT_UNK8(vs->input_count_unk8);
   cs->VS_TEMP_REGISTER_CONTROL =
      VIVS_VS_TEMP_REGISTER_CONTROL_NUM_TEMPS(num_temps);

   /* vs inputs (attributes) */
   DEFINE_ETNA_BITARRAY(vs_input, 16, 8) = {0};
   for (int idx = 0; idx < num_vs_inputs; ++idx) {
      if (idx < vs->infile.num_reg)
         etna_bitarray_set(vs_input, 8, idx, vs->infile.reg[idx].reg);
      else
         etna_bitarray_set(vs_input, 8, idx, cur_temp++);
   }

   for (int idx = 0; idx < ARRAY_SIZE(cs->VS_INPUT); ++idx)
      cs->VS_INPUT[idx] = vs_input[idx];

   return true;
}

bool
etna_shader_update_vertex(struct etna_context *ctx)
{
   return etna_shader_update_vs_inputs(ctx, &ctx->shader_state, ctx->vs,
                                       ctx->vertex_elements);
}

static void *
etna_create_shader_state(struct pipe_context *pctx,
                         const struct pipe_shader_state *pss)
{
   struct etna_context *ctx = etna_context(pctx);

   return etna_compile_shader(&ctx->specs, pss->tokens);
}

static void
etna_delete_shader_state(struct pipe_context *pctx, void *ss)
{
   etna_destroy_shader(ss);
}

static void
etna_bind_fs_state(struct pipe_context *pctx, void *fss_)
{
   struct etna_context *ctx = etna_context(pctx);
   struct etna_shader *fss = fss_;

   if (ctx->fs == fss) /* skip if already bound */
      return;

   assert(fss == NULL || fss->processor == PIPE_SHADER_FRAGMENT);
   ctx->fs = fss;
   ctx->dirty |= ETNA_DIRTY_SHADER;
}

static void
etna_bind_vs_state(struct pipe_context *pctx, void *vss_)
{
   struct etna_context *ctx = etna_context(pctx);
   struct etna_shader *vss = vss_;

   if (ctx->vs == vss) /* skip if already bound */
      return;

   assert(vss == NULL || vss->processor == PIPE_SHADER_VERTEX);
   ctx->vs = vss;
   ctx->dirty |= ETNA_DIRTY_SHADER;
}

void
etna_shader_init(struct pipe_context *pctx)
{
   pctx->create_fs_state = etna_create_shader_state;
   pctx->bind_fs_state = etna_bind_fs_state;
   pctx->delete_fs_state = etna_delete_shader_state;
   pctx->create_vs_state = etna_create_shader_state;
   pctx->bind_vs_state = etna_bind_vs_state;
   pctx->delete_vs_state = etna_delete_shader_state;
}
