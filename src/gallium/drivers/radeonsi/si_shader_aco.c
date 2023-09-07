/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "si_shader_internal.h"
#include "si_pipe.h"
#include "ac_hw_stage.h"
#include "aco_interface.h"

static void
si_aco_compiler_debug(void *private_data, enum aco_compiler_debug_level level,
                      const char *message)
{
   struct util_debug_callback *debug = private_data;

   util_debug_message(debug, SHADER_INFO, "%s\n", message);
}

static void
si_fill_aco_options(struct si_shader *shader, struct aco_compiler_options *options,
                    struct util_debug_callback *debug)
{
   const struct si_shader_selector *sel = shader->selector;

   options->dump_shader =
      si_can_dump_shader(sel->screen, sel->stage, SI_DUMP_ACO_IR) ||
      si_can_dump_shader(sel->screen, sel->stage, SI_DUMP_ASM);
   options->dump_preoptir = si_can_dump_shader(sel->screen, sel->stage, SI_DUMP_INIT_ACO_IR);
   options->record_ir = sel->screen->record_llvm_ir;
   options->is_opengl = true;

   options->load_grid_size_from_user_sgpr = true;
   options->family = sel->screen->info.family;
   options->gfx_level = sel->screen->info.gfx_level;
   options->address32_hi = sel->screen->info.address32_hi;

   options->debug.func = si_aco_compiler_debug;
   options->debug.private_data = debug;
}

static enum ac_hw_stage
si_select_hw_stage(const gl_shader_stage stage, const union si_shader_key *const key,
                   const enum amd_gfx_level gfx_level)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
      if (key->ge.as_ngg)
         return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
      else if (key->ge.as_es)
         return gfx_level >= GFX9 ? AC_HW_LEGACY_GEOMETRY_SHADER : AC_HW_EXPORT_SHADER;
      else if (key->ge.as_ls)
         return gfx_level >= GFX9 ? AC_HW_HULL_SHADER : AC_HW_LOCAL_SHADER;
      else
         return AC_HW_VERTEX_SHADER;
   case MESA_SHADER_TESS_CTRL:
      return AC_HW_HULL_SHADER;
   case MESA_SHADER_GEOMETRY:
      if (key->ge.as_ngg)
         return AC_HW_NEXT_GEN_GEOMETRY_SHADER;
      else
         return AC_HW_LEGACY_GEOMETRY_SHADER;
   case MESA_SHADER_FRAGMENT:
      return AC_HW_PIXEL_SHADER;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return AC_HW_COMPUTE_SHADER;
   default:
      unreachable("Unsupported HW stage");
   }
}

static void
si_fill_aco_shader_info(struct si_shader *shader, struct aco_shader_info *info)
{
   const struct si_shader_selector *sel = shader->selector;
   const union si_shader_key *key = &shader->key;
   const enum amd_gfx_level gfx_level = sel->screen->info.gfx_level;
   gl_shader_stage stage = shader->is_gs_copy_shader ? MESA_SHADER_VERTEX : sel->stage;

   info->wave_size = shader->wave_size;
   info->workgroup_size = si_get_max_workgroup_size(shader);
   /* aco need non-zero value */
   if (!info->workgroup_size)
      info->workgroup_size = info->wave_size;

   info->image_2d_view_of_3d = gfx_level == GFX9;
   info->hw_stage = si_select_hw_stage(stage, key, gfx_level);

   if (stage <= MESA_SHADER_GEOMETRY && key->ge.as_ngg && !key->ge.as_es) {
      info->has_ngg_culling = key->ge.opt.ngg_culling;
      info->has_ngg_early_prim_export = gfx10_ngg_export_prim_early(shader);
   }

   switch (stage) {
   case MESA_SHADER_FRAGMENT:
      info->ps.num_interp = si_get_ps_num_interp(shader);
      info->ps.spi_ps_input = shader->config.spi_ps_input_ena;
      break;
   default:
      break;
   }
}

static void
si_aco_build_shader_binary(void **data, const struct ac_shader_config *config,
                           const char *llvm_ir_str, unsigned llvm_ir_size, const char *disasm_str,
                           unsigned disasm_size, uint32_t *statistics, uint32_t stats_size,
                           uint32_t exec_size, const uint32_t *code, uint32_t code_dw,
                           const struct aco_symbol *symbols, unsigned num_symbols)
{
   struct si_shader *shader = (struct si_shader *)data;

   unsigned code_size = code_dw * 4;
   char *buffer = MALLOC(code_size + disasm_size);
   memcpy(buffer, code, code_size);

   shader->binary.type = SI_SHADER_BINARY_RAW;
   shader->binary.code_buffer = buffer;
   shader->binary.code_size = code_size;

   if (disasm_size) {
      memcpy(buffer + code_size, disasm_str, disasm_size);
      shader->binary.disasm_string = buffer + code_size;
      shader->binary.disasm_size = disasm_size;
   }

   if (llvm_ir_size) {
      shader->binary.llvm_ir_string = MALLOC(llvm_ir_size);
      memcpy(shader->binary.llvm_ir_string, llvm_ir_str, llvm_ir_size);
   }

   if (num_symbols) {
      unsigned symbol_size = num_symbols * sizeof(*symbols);
      void *data = MALLOC(symbol_size);
      memcpy(data, symbols, symbol_size);
      shader->binary.symbols = data;
      shader->binary.num_symbols = num_symbols;
   }

   shader->config = *config;
}

bool
si_aco_compile_shader(struct si_shader *shader,
                      struct si_shader_args *args,
                      struct nir_shader *nir,
                      struct util_debug_callback *debug)
{
   struct aco_compiler_options options = {0};
   si_fill_aco_options(shader, &options, debug);

   struct aco_shader_info info = {0};
   si_fill_aco_shader_info(shader, &info);

   aco_compile_shader(&options, &info, 1, &nir, &args->ac,
                      si_aco_build_shader_binary, (void **)shader);

   return true;
}

void
si_aco_resolve_symbols(struct si_shader *shader, uint32_t *code, uint64_t scratch_va)
{
   const struct aco_symbol *symbols = (struct aco_symbol *)shader->binary.symbols;
   const struct si_shader_selector *sel = shader->selector;
   const union si_shader_key *key = &shader->key;

   for (int i = 0; i < shader->binary.num_symbols; i++) {
      uint32_t value = 0;

      switch (symbols[i].id) {
      case aco_symbol_scratch_addr_lo:
         value = scratch_va;
         break;
      case aco_symbol_scratch_addr_hi:
         value = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32);

         if (sel->screen->info.gfx_level >= GFX11)
            value |= S_008F04_SWIZZLE_ENABLE_GFX11(1);
         else
            value |= S_008F04_SWIZZLE_ENABLE_GFX6(1);
         break;
      case aco_symbol_lds_ngg_scratch_base:
         assert(sel->stage <= MESA_SHADER_GEOMETRY && key->ge.as_ngg);
         value = shader->gs_info.esgs_ring_size * 4;
         if (sel->stage == MESA_SHADER_GEOMETRY)
            value += shader->ngg.ngg_emit_size * 4;
         value = ALIGN(value, 8);
         break;
      case aco_symbol_lds_ngg_gs_out_vertex_base:
         assert(sel->stage == MESA_SHADER_GEOMETRY && key->ge.as_ngg);
         value = shader->gs_info.esgs_ring_size * 4;
         break;
      default:
         unreachable("invalid aco symbol");
         break;
      }

      code[symbols[i].offset] = value;
   }
}
