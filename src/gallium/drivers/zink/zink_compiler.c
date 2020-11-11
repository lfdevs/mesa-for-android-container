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

#include "zink_context.h"
#include "zink_compiler.h"
#include "zink_program.h"
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
   /* a shader like this (shaders@glsl-fs-discard-04):

      uniform int j, k;

      void main()
      {
       for (int i = 0; i < j; i++) {
        if (i > k)
         continue;
        discard;
       }
       gl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);
      }



      will generate nir like:

      loop   {
         //snip
         if   ssa_11   {
            block   block_5:
            /   preds:   block_4   /
            vec1   32   ssa_17   =   iadd   ssa_50,   ssa_31
            /   succs:   block_7   /
         }   else   {
            block   block_6:
            /   preds:   block_4   /
            intrinsic   discard   ()   () <-- not last instruction
            vec1   32   ssa_23   =   iadd   ssa_50,   ssa_31 <-- dead code loop itr increment
            /   succs:   block_7   /
         }
         //snip
      }

      which means that we can't assert like this:

      assert(instr->intrinsic != nir_intrinsic_discard ||
             nir_block_last_instr(instr->instr.block) == &instr->instr);


      and it's unnecessary anyway since post-vtn optimizing will dce the instructions following the discard
    */

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
   .lower_ffma16 = true,
   .lower_ffma32 = true,
   .lower_ffma64 = true,
   .lower_fdph = true,
   .lower_flrp32 = true,
   .lower_fpow = true,
   .lower_fsat = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_mul_high = true,
   .lower_rotate = true,
   .lower_uadd_carry = true,
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

   return tgsi_to_nir(tokens, screen, false);
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

/* check for a genuine gl_PointSize output vs one from nir_lower_point_size_mov */
static bool
check_psiz(struct nir_shader *s)
{
   nir_foreach_shader_out_variable(var, s) {
      if (var->data.location == VARYING_SLOT_PSIZ) {
         /* genuine PSIZ outputs will have this set */
         return !!var->data.explicit_location;
      }
   }
   return false;
}

/* semi-copied from iris */
static void
update_so_info(struct zink_shader *sh,
               uint64_t outputs_written, bool have_psiz)
{
   uint8_t reverse_map[64] = {};
   unsigned slot = 0;
   while (outputs_written) {
      int bit = u_bit_scan64(&outputs_written);
      /* PSIZ from nir_lower_point_size_mov breaks stream output, so always skip it */
      if (bit == VARYING_SLOT_PSIZ && !have_psiz)
         continue;
      reverse_map[slot++] = bit;
   }

   for (unsigned i = 0; i < sh->streamout.so_info.num_outputs; i++) {
      struct pipe_stream_output *output = &sh->streamout.so_info.output[i];
      /* Map Gallium's condensed "slots" back to real VARYING_SLOT_* enums */
      sh->streamout.so_info_slots[i] = reverse_map[output->register_index];
   }
}

VkShaderModule
zink_shader_compile(struct zink_screen *screen, struct zink_shader *zs,
                    unsigned char *shader_slot_map, unsigned char *shader_slots_reserved)
{
   VkShaderModule mod = VK_NULL_HANDLE;
   void *streamout = NULL;
   if (zs->streamout.so_info_slots && (zs->nir->info.stage != MESA_SHADER_VERTEX || !zs->has_geometry_shader))
      streamout = &zs->streamout;
   struct spirv_shader *spirv = nir_to_spirv(zs->nir, streamout, shader_slot_map, shader_slots_reserved);
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

   if (vkCreateShaderModule(screen->dev, &smci, NULL, &mod) != VK_SUCCESS)
      mod = VK_NULL_HANDLE;

   /* TODO: determine if there's any reason to cache spirv output? */
   free(spirv->words);
   free(spirv);
   return mod;
}

struct zink_shader *
zink_shader_create(struct zink_screen *screen, struct nir_shader *nir,
                   const struct pipe_stream_output_info *so_info)
{
   struct zink_shader *ret = CALLOC_STRUCT(zink_shader);
   bool have_psiz = false;

   ret->programs = _mesa_pointer_set_create(NULL);

   /* only do uniforms -> ubo if we have uniforms, otherwise we're just
    * screwing with the bindings for no reason
    */
   if (nir->num_uniforms)
      NIR_PASS_V(nir, nir_lower_uniforms_to_ubo, 16);
   NIR_PASS_V(nir, nir_lower_ubo_vec4);
   NIR_PASS_V(nir, nir_lower_clip_halfz);
   if (nir->info.stage < MESA_SHADER_FRAGMENT)
      have_psiz = check_psiz(nir);
   if (nir->info.stage == MESA_SHADER_GEOMETRY)
      NIR_PASS_V(nir, nir_lower_gs_intrinsics, nir_lower_gs_intrinsics_per_stream);
   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   optimize_nir(nir);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, lower_discard_if);
   NIR_PASS_V(nir, nir_lower_fragcolor);
   NIR_PASS_V(nir, nir_convert_from_ssa, true);

   if (zink_debug & ZINK_DEBUG_NIR) {
      fprintf(stderr, "NIR shader:\n---8<---\n");
      nir_print_shader(nir, stderr);
      fprintf(stderr, "---8<---\n");
   }

   ret->num_bindings = 0;
   uint32_t cur_ubo = 0;
   /* UBO buffers are zero-indexed, but buffer 0 is always the one created by nir_lower_uniforms_to_ubo,
    * which means there is no buffer 0 if there are no uniforms
    */
   int ubo_index = !nir->num_uniforms;
   /* need to set up var->data.binding for UBOs, which means we need to start at
    * the "first" UBO, which is at the end of the list
    */
   foreach_list_typed_reverse(nir_variable, var, node, &nir->variables) {
      if (_nir_shader_variable_has_mode(var, nir_var_uniform |
                                        nir_var_mem_ubo |
                                        nir_var_mem_ssbo)) {
         if (var->data.mode == nir_var_mem_ubo) {
            /* ignore variables being accessed if they aren't the base of the UBO */
            if (var->data.location)
               continue;
            var->data.binding = cur_ubo++;

            int binding = zink_binding(nir->info.stage,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                       var->data.binding);
            ret->bindings[ret->num_bindings].index = ubo_index++;
            ret->bindings[ret->num_bindings].binding = binding;
            ret->bindings[ret->num_bindings].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            ret->num_bindings++;
         } else {
            assert(var->data.mode == nir_var_uniform);
            if (glsl_type_is_sampler(var->type)) {
               VkDescriptorType vktype = zink_sampler_type(var->type);
               int binding = zink_binding(nir->info.stage,
                                          vktype,
                                          var->data.binding);
               ret->bindings[ret->num_bindings].index = var->data.binding;
               ret->bindings[ret->num_bindings].binding = binding;
               ret->bindings[ret->num_bindings].type = vktype;
               ret->num_bindings++;
            } else if (glsl_type_is_array(var->type)) {
               /* need to unroll possible arrays of arrays before checking type
                * in order to handle ARB_arrays_of_arrays extension
                */
               const struct glsl_type *type = glsl_without_array(var->type);
               if (!glsl_type_is_sampler(type))
                  continue;
               VkDescriptorType vktype = zink_sampler_type(type);

               unsigned size = glsl_get_aoa_size(var->type);
               for (int i = 0; i < size; ++i) {
                  int binding = zink_binding(nir->info.stage,
                                             vktype,
                                             var->data.binding + i);
                  ret->bindings[ret->num_bindings].index = var->data.binding + i;
                  ret->bindings[ret->num_bindings].binding = binding;
                  ret->bindings[ret->num_bindings].type = vktype;
                  ret->num_bindings++;
               }
            }
         }
      }
   }

   ret->nir = nir;
   if (so_info) {
      memcpy(&ret->streamout.so_info, so_info, sizeof(struct pipe_stream_output_info));
      ret->streamout.so_info_slots = malloc(so_info->num_outputs * sizeof(unsigned int));
      assert(ret->streamout.so_info_slots);
      update_so_info(ret, nir->info.outputs_written, have_psiz);
   }

   return ret;
}

void
zink_shader_free(struct zink_context *ctx, struct zink_shader *shader)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   set_foreach(shader->programs, entry) {
      struct zink_gfx_program *prog = (void*)entry->key;
      _mesa_hash_table_remove_key(ctx->program_cache, prog->shaders);
      prog->shaders[pipe_shader_type_from_mesa(shader->nir->info.stage)] = NULL;
      zink_gfx_program_reference(screen, &prog, NULL);
   }
   _mesa_set_destroy(shader->programs, NULL);
   free(shader->streamout.so_info_slots);
   ralloc_free(shader->nir);
   FREE(shader);
}
