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

#include "compiler/intel_nir.h"
#include "compiler/brw/brw_compiler.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "dev/intel_debug.h"
#include "intel/compiler/intel_nir.h"
#include "util/macros.h"

#include "vk_nir.h"

#include "anv_internal_kernels.h"

static bool
lower_base_workgroup_id(nir_builder *b, nir_intrinsic_instr *intrin,
                        UNUSED void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_base_workgroup_id)
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);
   nir_def_rewrite_uses(&intrin->def, nir_imm_zero(b, 3, 32));
   return true;
}

static void
check_sends(struct genisa_stats *stats, unsigned send_count)
{
   assert(stats->spill_count == 0);
   assert(stats->fill_count == 0);
   assert(stats->send_messages == send_count);
}

static struct anv_shader_internal *
compile_shader(struct anv_device *device,
               enum anv_internal_kernel_name shader_name,
               mesa_shader_stage stage,
               const char *name,
               const void *hash_key,
               uint32_t hash_key_size,
               uint32_t sends_count_expectation)
{
   const nir_shader_compiler_options *nir_options =
      &device->physical->compiler->nir_options[stage];

   nir_builder b = nir_builder_init_simple_shader(stage, nir_options,
                                                  "%s", name);

   uint32_t uniform_size =
      anv_genX(device->info, call_internal_shader)(&b, shader_name);

   nir_shader *nir = b.shader;

   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_opt_cse);
   NIR_PASS(_, nir, nir_opt_gcm, true);

   nir_opt_peephole_select_options peephole_select_options = {
      .limit = 1,
   };
   NIR_PASS(_, nir, nir_opt_peephole_select, &peephole_select_options);

   NIR_PASS(_, nir, nir_lower_variable_initializers, ~0);

   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_split_per_member_structs);

   if (stage == MESA_SHADER_COMPUTE) {
      nir->info.workgroup_size[0] = 16;
      nir->info.workgroup_size[1] = 1;
      nir->info.workgroup_size[2] = 1;
   }

   struct brw_compiler *compiler = device->physical->compiler;
   struct brw_nir_compiler_opts opts = {};
   brw_preprocess_nir(compiler, nir, &opts);

   NIR_PASS(_, nir, nir_propagate_invariant, false);

   if (stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_input_attachments,
                 &(nir_input_attachment_options) { });
   } else {
      nir_lower_compute_system_values_options options = {
         .has_base_workgroup_id = true,
         .lower_cs_local_id_to_index = true,
         .lower_workgroup_id_to_index = mesa_shader_stage_is_mesh(stage),
      };
      NIR_PASS(_, nir, nir_lower_compute_system_values, &options);
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_base_workgroup_id,
                 nir_metadata_control_flow, NULL);
   }

   /* Reset sizes before gathering information */
   nir->global_mem_size = 0;
   nir->scratch_size = 0;
   nir->info.shared_size = 0;
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_opt_dce);

   union brw_any_prog_key key;
   memset(&key, 0, sizeof(key));

   union brw_any_prog_data prog_data;
   memset(&prog_data, 0, sizeof(prog_data));

   if (stage == MESA_SHADER_COMPUTE) {
      NIR_PASS(_, nir, brw_nir_lower_cs_intrinsics,
                 device->info, &prog_data.cs);
   }

   /* Do vectorizing here. For some reason when trying to do it in the back
    * this just isn't working.
    */
   nir_load_store_vectorize_options options = {
      .modes = nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_global,
      .callback = brw_nir_should_vectorize_mem,
      .robust_modes = (nir_variable_mode)0,
   };
   NIR_PASS(_, nir, nir_opt_load_store_vectorize, &options);

   nir->num_uniforms = uniform_size;

   void *temp_ctx = ralloc_context(NULL);

   prog_data.base.nr_params = nir->num_uniforms / 4;
   prog_data.base.param = rzalloc_array(temp_ctx, uint32_t, prog_data.base.nr_params);

   brw_nir_analyze_ubo_ranges(compiler, nir, prog_data.base.ubo_ranges);

   const unsigned *program;
   if (stage == MESA_SHADER_FRAGMENT) {
      struct genisa_stats stats[3];
      struct brw_compile_fs_params params = {
         .base = {
            .nir = nir,
            .log_data = device,
            .debug_flag = DEBUG_WM,
            .stats = stats,
            .mem_ctx = temp_ctx,
         },
         .key = &key.wm,
         .prog_data = &prog_data.wm,
      };
      program = brw_compile_fs(compiler, &params);

      if (!INTEL_DEBUG(DEBUG_SHADER_PRINT)) {
         unsigned stat_idx = 0;
         if (prog_data.wm.dispatch_8) {
            check_sends(&stats[stat_idx++], sends_count_expectation);
         }
         if (prog_data.wm.dispatch_16) {
            check_sends(&stats[stat_idx++], sends_count_expectation);
         }
         if (prog_data.wm.dispatch_32) {
            check_sends(&stats[stat_idx++], sends_count_expectation *
                                            (device->info->ver < 20 ? 2 : 1));
         }
      }
   } else {
      struct genisa_stats stats;
      struct brw_compile_cs_params params = {
         .base = {
            .nir = nir,
            .stats = &stats,
            .log_data = device,
            .debug_flag = DEBUG_CS,
            .mem_ctx = temp_ctx,
         },
         .key = &key.cs,
         .prog_data = &prog_data.cs,
      };
      program = brw_compile_cs(compiler, &params);

      if (!INTEL_DEBUG(DEBUG_SHADER_PRINT)) {
         check_sends(&stats, sends_count_expectation);
      }
   }

   assert(prog_data.base.total_scratch == 0);
   assert(program != NULL);
   struct anv_shader_internal *kernel = NULL;
   if (program == NULL)
      goto exit;

   struct anv_pipeline_bind_map empty_bind_map = {};
   struct anv_push_descriptor_info empty_push_desc_info = {};
   struct anv_shader_upload_params upload_params = {
      .stage               = nir->info.stage,
      .key_data            = hash_key,
      .key_size            = hash_key_size,
      .kernel_data         = program,
      .kernel_size         = prog_data.base.program_size,
      .prog_data           = &prog_data.base,
      .prog_data_size      = sizeof(prog_data),
      .bind_map            = &empty_bind_map,
      .push_desc_info      = &empty_push_desc_info,
   };

   kernel = anv_device_upload_kernel(device, device->internal_cache, &upload_params);

exit:
   ralloc_free(temp_ctx);
   ralloc_free(nir);

   return kernel;
}

VkResult
anv_device_get_internal_shader(struct anv_device *device,
                               enum anv_internal_kernel_name name,
                               struct anv_shader_internal **out_bin)
{
   const struct {
      struct {
         char name[40];
      } key;

      mesa_shader_stage stage;

      uint32_t        send_count;
   } internal_kernels[] = {
      [ANV_INTERNAL_KERNEL_GENERATED_DRAWS] = {
         .key        = {
            .name    = "anv-generated-indirect-draws",
         },
         .stage      = MESA_SHADER_FRAGMENT,
         .send_count =  (device->info->ver == 9 ?
                         /* 1 load +
                          * 4 stores +
                          * 2 * (2 loads + 2 stores) +
                          * 3 stores
                          */
                         16 :
                         /* 1 load +
                          * 2 * (2 loads + 3 stores) +
                          * 3 stores
                          */
                         14) +
         /* 3 loads + 3 stores */
         (intel_needs_workaround(device->info, 16011107343) ? 6 : 0) +
         /* 3 loads + 3 stores */
         (intel_needs_workaround(device->info, 22018402687) ? 6 : 0),
      },
      [ANV_INTERNAL_KERNEL_COPY_QUERY_RESULTS_COMPUTE] = {
         .key        = {
            .name    = "anv-copy-query-compute",
         },
         .stage      = MESA_SHADER_COMPUTE,
         .send_count = device->info->verx10 >= 125 ?
                       9 /* 4 loads + 4 stores + 1 EOT */ :
                       8 /* 3 loads + 4 stores + 1 EOT */,
      },
      [ANV_INTERNAL_KERNEL_COPY_QUERY_RESULTS_FRAGMENT] = {
         .key        = {
            .name    = "anv-copy-query-fragment",
         },
         .stage      = MESA_SHADER_FRAGMENT,
         .send_count = 8 /* 3 loads + 4 stores + 1 EOT */,
      },
      [ANV_INTERNAL_KERNEL_MEMCPY_COMPUTE] = {
         .key        = {
            .name    = "anv-memcpy-compute",
         },
         .stage      = MESA_SHADER_COMPUTE,
         .send_count = device->info->verx10 >= 125 ?
                       10 /* 5 loads (1 pull constants) + 4 stores + 1 EOT */ :
                       9 /* 4 loads + 4 stores + 1 EOT */,
      },
   };

   struct anv_shader_internal *bin =
      p_atomic_read(&device->internal_kernels[name]);
   if (bin != NULL) {
      *out_bin = bin;
      return VK_SUCCESS;
   }

   bin =
      anv_device_search_for_kernel(device,
                                   device->internal_cache,
                                   &internal_kernels[name].key,
                                   sizeof(internal_kernels[name].key),
                                   NULL);
   if (bin != NULL) {
      p_atomic_set(&device->internal_kernels[name], bin);
      *out_bin = bin;
      return VK_SUCCESS;
   }

   bin = compile_shader(device,
                        name,
                        internal_kernels[name].stage,
                        internal_kernels[name].key.name,
                        &internal_kernels[name].key,
                        sizeof(internal_kernels[name].key),
                        internal_kernels[name].send_count);
   if (bin == NULL)
      return vk_errorf(device, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "Unable to compiler internal kernel");

   /* The cache already has a reference and it's not going anywhere so
    * there is no need to hold a second reference.
    */
   anv_shader_internal_unref(device, bin);

   p_atomic_set(&device->internal_kernels[name], bin);

   *out_bin = bin;
   return VK_SUCCESS;
}

VkResult
anv_device_init_internal_kernels(struct anv_device *device)
{
   const struct intel_l3_weights w =
      intel_get_default_l3_weights(device->info,
                                   true /* wants_dc_cache */,
                                   false /* needs_slm */);
   device->internal_kernels_l3_config = intel_get_l3_config(device->info, w);

   return VK_SUCCESS;
}

void
anv_device_finish_internal_kernels(struct anv_device *device)
{
}
