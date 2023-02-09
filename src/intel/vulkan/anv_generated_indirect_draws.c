/*
 * Copyright © 2022 Intel Corporation
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

#include "anv_private.h"

#include "compiler/brw_compiler.h"
#include "compiler/brw_nir.h"
#include "compiler/spirv/nir_spirv.h"
#include "dev/intel_debug.h"
#include "util/macros.h"

#include "anv_generated_indirect_draws.h"

#include "shaders/generated_draws_spv.h"
#include "shaders/generated_draws_count_spv.h"

/* This pass takes vulkan descriptor bindings 0 & 1 and turns them into global
 * 64bit addresses. Binding 2 is left UBO that would normally be accessed
 * through the binding table but it fully promoted to push constants.
 *
 * As a result we're not using the binding table at all which is nice because
 * of the side command buffer we use for the generating shader does not
 * interact with the binding table allocation.
 */
static bool
lower_vulkan_descriptors_instr(nir_builder *b, nir_instr *instr, void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_vulkan_descriptor)
      return false;

   nir_instr *res_index_instr = intrin->src[0].ssa->parent_instr;
   assert(res_index_instr->type == nir_instr_type_intrinsic);
   nir_intrinsic_instr *res_index_intrin =
      nir_instr_as_intrinsic(res_index_instr);
   assert(res_index_intrin->intrinsic == nir_intrinsic_vulkan_resource_index);

   b->cursor = nir_after_instr(instr);

   nir_ssa_def *desc_value = NULL;
   switch (nir_intrinsic_binding(res_index_intrin)) {
   case 0: {
      desc_value =
         nir_load_ubo(b, 1, 64,
                      nir_imm_int(b, 2),
                      nir_imm_int(b,
                                  offsetof(struct anv_generate_indirect_params,
                                           indirect_data_addr)),
                      .align_mul = 8,
                      .align_offset = 0,
                      .range_base = 0,
                      .range = ~0);
      desc_value =
         nir_vec4(b,
                  nir_unpack_64_2x32_split_x(b, desc_value),
                  nir_unpack_64_2x32_split_y(b, desc_value),
                  nir_imm_int(b, 0),
                  nir_imm_int(b, 0));
      break;
   }

   case 1: {
      desc_value =
         nir_load_ubo(b, 1, 64,
                      nir_imm_int(b, 2),
                      nir_imm_int(b,
                                  offsetof(struct anv_generate_indirect_params,
                                           generated_cmds_addr)),
                      .align_mul = 8,
                      .align_offset = 0,
                      .range_base = 0,
                      .range = ~0);
      desc_value =
         nir_vec4(b,
                  nir_unpack_64_2x32_split_x(b, desc_value),
                  nir_unpack_64_2x32_split_y(b, desc_value),
                  nir_imm_int(b, 0),
                  nir_imm_int(b, 0));
      break;
   }

   case 2:
      desc_value =
         nir_vec2(b,
                  nir_imm_int(b, 2),
                  nir_imm_int(b, 0));
      break;
   }

   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, desc_value);

   return true;
}

static bool
lower_vulkan_descriptors(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader,
                                       lower_vulkan_descriptors_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       NULL);
}

static struct anv_shader_bin *
compile_upload_spirv(struct anv_device *device,
                     const void *key,
                     uint32_t key_size,
                     const uint32_t *spirv_source,
                     uint32_t spirv_source_size,
                     uint32_t sends_count_expectation)
{
   struct spirv_to_nir_options spirv_options = {
      .caps = {
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_64bit_global_32bit_offset,
      .environment = NIR_SPIRV_VULKAN,
      .create_library = false,
   };
   const nir_shader_compiler_options *nir_options =
      device->physical->compiler->nir_options[MESA_SHADER_FRAGMENT];

   nir_shader* nir =
      spirv_to_nir(spirv_source, spirv_source_size,
                   NULL, 0, MESA_SHADER_FRAGMENT, "main",
                   &spirv_options, nir_options);

   assert(nir != NULL);

   nir->info.internal = true;

   nir_validate_shader(nir, "after spirv_to_nir");
   nir_validate_ssa_dominance(nir, "after spirv_to_nir");

   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_opt_deref);

   NIR_PASS_V(nir, nir_lower_vars_to_ssa);
   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_dce);
   NIR_PASS_V(nir, nir_opt_cse);
   NIR_PASS_V(nir, nir_opt_gcm, true);
   NIR_PASS_V(nir, nir_opt_peephole_select, 1, false, false);
   NIR_PASS_V(nir, nir_opt_dce);

   NIR_PASS_V(nir, nir_lower_variable_initializers, ~0);

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   struct brw_compiler *compiler = device->physical->compiler;
   struct brw_nir_compiler_opts opts = {};
   brw_preprocess_nir(compiler, nir, &opts);

   NIR_PASS_V(nir, nir_propagate_invariant, false);

   NIR_PASS_V(nir, nir_lower_input_attachments,
            &(nir_input_attachment_options) {
               .use_fragcoord_sysval = true,
               .use_layer_id_sysval = true,
            });

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* Do vectorizing here. For some reason when trying to do it in the back
    * this just isn't working.
    */
   nir_load_store_vectorize_options options = {
      .modes = nir_var_mem_ubo | nir_var_mem_ssbo,
      .callback = brw_nir_should_vectorize_mem,
      .robust_modes = (nir_variable_mode)0,
   };
   NIR_PASS_V(nir, nir_opt_load_store_vectorize, &options);

   NIR_PASS_V(nir, lower_vulkan_descriptors);
   NIR_PASS_V(nir, nir_opt_dce);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ubo,
              nir_address_format_32bit_index_offset);
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ssbo,
              nir_address_format_64bit_global_32bit_offset);

   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_constant_folding);
   NIR_PASS_V(nir, nir_opt_dce);

   struct brw_wm_prog_key wm_key;
   memset(&wm_key, 0, sizeof(wm_key));

   struct brw_wm_prog_data wm_prog_data = {
      .base.nr_params = nir->num_uniforms / 4,
   };

   brw_nir_analyze_ubo_ranges(compiler, nir, NULL, wm_prog_data.base.ubo_ranges);

   struct brw_compile_stats stats[3];
   struct brw_compile_fs_params params = {
      .nir = nir,
      .key = &wm_key,
      .prog_data = &wm_prog_data,
      .stats = stats,
      .log_data = device,
      .debug_flag = DEBUG_WM,
   };
   const unsigned *program = brw_compile_fs(compiler, nir, &params);

   unsigned stat_idx = 0;
   if (wm_prog_data.dispatch_8) {
      assert(stats[stat_idx].spills == 0);
      assert(stats[stat_idx].fills == 0);
      assert(stats[stat_idx].sends == sends_count_expectation);
      stat_idx++;
   }
   if (wm_prog_data.dispatch_16) {
      assert(stats[stat_idx].spills == 0);
      assert(stats[stat_idx].fills == 0);
      assert(stats[stat_idx].sends == sends_count_expectation);
      stat_idx++;
   }
   if (wm_prog_data.dispatch_32) {
      assert(stats[stat_idx].spills == 0);
      assert(stats[stat_idx].fills == 0);
      assert(stats[stat_idx].sends == sends_count_expectation);
      stat_idx++;
   }

   struct anv_pipeline_bind_map bind_map;
   memset(&bind_map, 0, sizeof(bind_map));

   struct anv_push_descriptor_info push_desc_info = {};

   struct anv_shader_bin *kernel =
      anv_device_upload_kernel(device,
                               device->internal_cache,
                               nir->info.stage,
                               key, key_size, program,
                               wm_prog_data.base.program_size,
                               &wm_prog_data.base, sizeof(wm_prog_data),
                               NULL, 0, NULL, &bind_map,
                               &push_desc_info);

   ralloc_free(nir);

   return kernel;
}

VkResult
anv_device_init_generated_indirect_draws(struct anv_device *device)
{
   if (device->info->ver < 11)
      return VK_SUCCESS;

   const struct intel_l3_weights w =
      intel_get_default_l3_weights(device->info,
                                   true /* wants_dc_cache */,
                                   false /* needs_slm */);
   device->generated_draw_l3_config = intel_get_l3_config(device->info, w);

   struct {
      char name[40];
   } indirect_draws_key = {
      .name = "anv-generated-indirect-draws",
   }, indirect_draws_count_key = {
      .name = "anv-generated-indirect-draws-count",
   };

   device->generated_draw_kernel =
      anv_device_search_for_kernel(device,
                                   device->internal_cache,
                                   &indirect_draws_key,
                                   sizeof(indirect_draws_key),
                                   NULL);
   if (device->generated_draw_kernel == NULL) {
      device->generated_draw_kernel =
         compile_upload_spirv(device,
                              &indirect_draws_key,
                              sizeof(indirect_draws_key),
                              generated_draws_spv_source,
                              ARRAY_SIZE(generated_draws_spv_source),
                              10 /* 2 * (2 loads + 3 stores) */);
   }
   if (device->generated_draw_kernel == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_bin_unref(device, device->generated_draw_kernel);

   device->generated_draw_count_kernel =
      anv_device_search_for_kernel(device,
                                   device->internal_cache,
                                   &indirect_draws_count_key,
                                   sizeof(indirect_draws_count_key),
                                   NULL);
   if (device->generated_draw_count_kernel == NULL) {
      device->generated_draw_count_kernel =
         compile_upload_spirv(device,
                              &indirect_draws_count_key,
                              sizeof(indirect_draws_count_key),
                              generated_draws_count_spv_source,
                              ARRAY_SIZE(generated_draws_count_spv_source),
                              11 /* 2 * (3 loads + 3 stores) */);
   }
   if (device->generated_draw_count_kernel == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_bin_unref(device, device->generated_draw_count_kernel);

   return VK_SUCCESS;
}

void
anv_device_finish_generated_indirect_draws(struct anv_device *device)
{
}
