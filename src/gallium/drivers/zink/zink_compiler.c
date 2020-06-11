/*
 * Copyright 2018 Collabora Ltd.
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

#include "zink_compiler.h"
#include "zink_screen.h"
#include "nir_to_spirv/nir_to_spirv.h"

#include "pipe/p_state.h"

#include "nir.h"
#include "compiler/nir/nir_builder.h"

#include "nir/tgsi_to_nir.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_from_mesa.h"

#include "util/u_memory.h"

static bool
lower_instr(nir_intrinsic_instr *instr, nir_builder *b)
{
   b->cursor = nir_before_instr(&instr->instr);

   if (instr->intrinsic == nir_intrinsic_load_ubo) {
      nir_ssa_def *old_idx = nir_ssa_for_src(b, instr->src[0], 1);
      nir_ssa_def *new_idx = nir_iadd(b, old_idx, nir_imm_int(b, 1));
      nir_instr_rewrite_src(&instr->instr, &instr->src[0],
                            nir_src_for_ssa(new_idx));
      return true;
   }

   if (instr->intrinsic == nir_intrinsic_load_uniform) {
      nir_ssa_def *ubo_idx = nir_imm_int(b, 0);
      nir_ssa_def *ubo_offset =
         nir_iadd(b, nir_imm_int(b, nir_intrinsic_base(instr)),
                  nir_ssa_for_src(b, instr->src[0], 1));

      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_ubo);
      load->num_components = instr->num_components;
      load->src[0] = nir_src_for_ssa(ubo_idx);
      load->src[1] = nir_src_for_ssa(ubo_offset);
      assert(instr->dest.ssa.bit_size >= 8);
      nir_intrinsic_set_align(load, instr->dest.ssa.bit_size / 8, 0);
      nir_ssa_dest_init(&load->instr, &load->dest,
                        load->num_components, instr->dest.ssa.bit_size,
                        instr->dest.ssa.name);
      nir_builder_instr_insert(b, &load->instr);
      nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(&load->dest.ssa));

      nir_instr_remove(&instr->instr);
      return true;
   }

   return false;
}

static bool
lower_uniforms_to_ubo(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_instr(nir_instr_as_intrinsic(instr),
                                          &builder);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }

   if (progress) {
      assert(shader->num_uniforms > 0);
      const struct glsl_type *type = glsl_array_type(glsl_vec4_type(),
                                                     shader->num_uniforms, 0);
      nir_variable *ubo = nir_variable_create(shader, nir_var_mem_ubo, type,
                                              "uniform_0");
      ubo->data.binding = 0;

      struct glsl_struct_field field = {
         .type = type,
         .name = "data",
         .location = -1,
      };
      ubo->interface_type =
            glsl_interface_type(&field, 1, GLSL_INTERFACE_PACKING_STD430,
                                false, "__ubo0_interface");
   }

   return progress;
}

static bool
lower_discard_if_instr(nir_intrinsic_instr *instr, nir_builder *b)
{
   if (instr->intrinsic == nir_intrinsic_discard_if) {
      b->cursor = nir_before_instr(&instr->instr);

      nir_if *if_stmt = nir_push_if(b, nir_ssa_for_src(b, instr->src[0], 1));
      nir_intrinsic_instr *discard =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard);
      nir_builder_instr_insert(b, &discard->instr);
      nir_pop_if(b, if_stmt);
      nir_instr_remove(&instr->instr);
      return true;
   }
   assert(instr->intrinsic != nir_intrinsic_discard ||
          nir_block_last_instr(instr->instr.block) == &instr->instr);

   return false;
}

static bool
lower_discard_if(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_discard_if_instr(
                                                  nir_instr_as_intrinsic(instr),
                                                  &builder);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_dominance);
      }
   }

   return progress;
}

static const struct nir_shader_compiler_options nir_options = {
   .lower_all_io_to_temps = true,
   .lower_ffma = true,
   .lower_fdph = true,
   .lower_flrp32 = true,
   .lower_fpow = true,
   .lower_fsat = true,
};

const void *
zink_get_compiler_options(struct pipe_screen *screen,
                          enum pipe_shader_ir ir,
                          enum pipe_shader_type shader)
{
   assert(ir == PIPE_SHADER_IR_NIR);
   return &nir_options;
}

struct nir_shader *
zink_tgsi_to_nir(struct pipe_screen *screen, const struct tgsi_token *tokens)
{
   if (zink_debug & ZINK_DEBUG_TGSI) {
      fprintf(stderr, "TGSI shader:\n---8<---\n");
      tgsi_dump_to_file(tokens, 0, stderr);
      fprintf(stderr, "---8<---\n\n");
   }

   return tgsi_to_nir(tokens, screen);
}

static void
optimize_nir(struct nir_shader *s)
{
   bool progress;
   do {
      progress = false;
      NIR_PASS_V(s, nir_lower_vars_to_ssa);
      NIR_PASS(progress, s, nir_copy_prop);
      NIR_PASS(progress, s, nir_opt_remove_phis);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      NIR_PASS(progress, s, nir_opt_cse);
      NIR_PASS(progress, s, nir_opt_peephole_select, 8, true, true);
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_undef);
      NIR_PASS(progress, s, zink_nir_lower_b2b);
   } while (progress);
}

struct zink_shader *
zink_compile_nir(struct zink_screen *screen, struct nir_shader *nir)
{
   struct zink_shader *ret = CALLOC_STRUCT(zink_shader);

   NIR_PASS_V(nir, lower_uniforms_to_ubo);
   NIR_PASS_V(nir, nir_lower_clip_halfz);
   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   optimize_nir(nir);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp);
   NIR_PASS_V(nir, lower_discard_if);
   NIR_PASS_V(nir, nir_convert_from_ssa, true);

   if (zink_debug & ZINK_DEBUG_NIR) {
      fprintf(stderr, "NIR shader:\n---8<---\n");
      nir_print_shader(nir, stderr);
      fprintf(stderr, "---8<---\n");
   }

   ret->num_bindings = 0;
   nir_foreach_variable(var, &nir->uniforms) {
      if (var->data.mode == nir_var_mem_ubo) {
         int binding = zink_binding(nir->info.stage,
                                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                    var->data.binding);
         ret->bindings[ret->num_bindings].index = var->data.binding;
         ret->bindings[ret->num_bindings].binding = binding;
         ret->bindings[ret->num_bindings].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         ret->num_bindings++;
      } else {
         assert(var->data.mode == nir_var_uniform);
         if (glsl_type_is_array(var->type) &&
             glsl_type_is_sampler(glsl_get_array_element(var->type))) {
            for (int i = 0; i < glsl_get_length(var->type); ++i) {
               int binding = zink_binding(nir->info.stage,
                                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                          var->data.binding + i);
               ret->bindings[ret->num_bindings].index = var->data.binding + i;
               ret->bindings[ret->num_bindings].binding = binding;
               ret->bindings[ret->num_bindings].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
               ret->num_bindings++;
            }
         } else if (glsl_type_is_sampler(var->type)) {
            int binding = zink_binding(nir->info.stage,
                                       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       var->data.binding);
            ret->bindings[ret->num_bindings].index = var->data.binding;
            ret->bindings[ret->num_bindings].binding = binding;
            ret->bindings[ret->num_bindings].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            ret->num_bindings++;
         }
      }
   }

   ret->info = nir->info;

   struct spirv_shader *spirv = nir_to_spirv(nir);
   assert(spirv);

   if (zink_debug & ZINK_DEBUG_SPIRV) {
      char buf[256];
      static int i;
      snprintf(buf, sizeof(buf), "dump%02d.spv", i++);
      FILE *fp = fopen(buf, "wb");
      if (fp) {
         fwrite(spirv->words, sizeof(uint32_t), spirv->num_words, fp);
         fclose(fp);
         fprintf(stderr, "wrote '%s'...\n", buf);
      }
   }

   VkShaderModuleCreateInfo smci = {};
   smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   smci.codeSize = spirv->num_words * sizeof(uint32_t);
   smci.pCode = spirv->words;

   if (vkCreateShaderModule(screen->dev, &smci, NULL, &ret->shader_module) != VK_SUCCESS)
      return NULL;

   return ret;
}

void
zink_shader_free(struct zink_screen *screen, struct zink_shader *shader)
{
   vkDestroyShaderModule(screen->dev, shader->shader_module, NULL);
   FREE(shader);
}
