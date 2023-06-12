/*
 * Copyright © 2015 Intel Corporation
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

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "util/mesa-sha1.h"
#include "util/os_time.h"
#include "common/intel_l3_config.h"
#include "common/intel_disasm.h"
#include "common/intel_sample_positions.h"
#include "anv_private.h"
#include "compiler/brw_nir.h"
#include "compiler/brw_nir_rt.h"
#include "anv_nir.h"
#include "nir/nir_vulkan.h"
#include "nir/nir_xfb_info.h"
#include "spirv/nir_spirv.h"
#include "vk_pipeline.h"
#include "vk_render_pass.h"
#include "vk_util.h"

/* Needed for SWIZZLE macros */
#include "program/prog_instruction.h"

struct lower_set_vtx_and_prim_count_state {
   nir_variable *primitive_count;
};

static nir_variable *
anv_nir_prim_count_store(nir_builder *b, nir_ssa_def *val)
{
   nir_variable *primitive_count =
         nir_variable_create(b->shader,
                             nir_var_shader_out,
                             glsl_uint_type(),
                             "gl_PrimitiveCountNV");
   primitive_count->data.location = VARYING_SLOT_PRIMITIVE_COUNT;
   primitive_count->data.interpolation = INTERP_MODE_NONE;

   nir_ssa_def *local_invocation_index = nir_build_load_local_invocation_index(b);

   nir_ssa_def *cmp = nir_ieq(b, local_invocation_index,
                                  nir_imm_int(b, 0));
   nir_if *if_stmt = nir_push_if(b, cmp);
   {
      nir_deref_instr *prim_count_deref = nir_build_deref_var(b, primitive_count);
      nir_store_deref(b, prim_count_deref, val, 1);
   }
   nir_pop_if(b, if_stmt);

   return primitive_count;
}

static bool
anv_nir_lower_set_vtx_and_prim_count_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_set_vertex_and_primitive_count)
      return false;

   /* Detect some cases of invalid primitive count. They might lead to URB
    * memory corruption, where workgroups overwrite each other output memory.
    */
   if (nir_src_is_const(intrin->src[1]) &&
         nir_src_as_uint(intrin->src[1]) > b->shader->info.mesh.max_primitives_out) {
      assert(!"number of primitives bigger than max specified");
   }

   struct lower_set_vtx_and_prim_count_state *state = data;
   /* this intrinsic should show up only once */
   assert(state->primitive_count == NULL);

   b->cursor = nir_before_instr(&intrin->instr);

   state->primitive_count = anv_nir_prim_count_store(b, intrin->src[1].ssa);

   nir_instr_remove(instr);

   return true;
}

static bool
anv_nir_lower_set_vtx_and_prim_count(nir_shader *nir)
{
   struct lower_set_vtx_and_prim_count_state state = { NULL, };

   nir_shader_instructions_pass(nir,
                                anv_nir_lower_set_vtx_and_prim_count_instr,
                                nir_metadata_none,
                                &state);

   /* If we didn't find set_vertex_and_primitive_count, then we have to
    * insert store of value 0 to primitive_count.
    */
   if (state.primitive_count == NULL) {
      nir_builder b;
      nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
      nir_builder_init(&b, entrypoint);
      b.cursor = nir_before_block(nir_start_block(entrypoint));
      nir_ssa_def *zero = nir_imm_int(&b, 0);
      state.primitive_count = anv_nir_prim_count_store(&b, zero);
   }

   assert(state.primitive_count != NULL);
   return true;
}

/* Eventually, this will become part of anv_CreateShader.  Unfortunately,
 * we can't do that yet because we don't have the ability to copy nir.
 */
static nir_shader *
anv_shader_stage_to_nir(struct anv_device *device,
                        const VkPipelineShaderStageCreateInfo *stage_info,
                        void *mem_ctx)
{
   const struct anv_physical_device *pdevice = device->physical;
   const struct anv_instance *instance = pdevice->instance;
   const struct brw_compiler *compiler = pdevice->compiler;
   gl_shader_stage stage = vk_to_mesa_shader_stage(stage_info->stage);
   const nir_shader_compiler_options *nir_options =
      compiler->nir_options[stage];

   const bool rt_enabled = ANV_SUPPORT_RT && pdevice->info.has_ray_tracing;
   const struct spirv_to_nir_options spirv_options = {
      .caps = {
         .demote_to_helper_invocation = true,
         .derivative_group = true,
         .descriptor_array_dynamic_indexing = true,
         .descriptor_array_non_uniform_indexing = true,
         .descriptor_indexing = true,
         .device_group = true,
         .draw_parameters = true,
         .float16 = true,
         .float32_atomic_add = pdevice->info.has_lsc,
         .float32_atomic_min_max = true,
         .float64 = true,
         .float64_atomic_min_max = pdevice->info.has_lsc,
         .fragment_shader_sample_interlock = true,
         .fragment_shader_pixel_interlock = true,
         .geometry_streams = true,
         /* When using Vulkan 1.3 or KHR_format_feature_flags2 is enabled, the
          * read/write without format is per format, so just report true. It's
          * up to the application to check.
          */
         .image_read_without_format = instance->vk.app_info.api_version >= VK_API_VERSION_1_3 || device->vk.enabled_extensions.KHR_format_feature_flags2,
         .image_write_without_format = true,
         .int8 = true,
         .int16 = true,
         .int64 = true,
         .int64_atomics = true,
         .integer_functions2 = true,
         .mesh_shading = pdevice->vk.supported_extensions.EXT_mesh_shader,
         .mesh_shading_nv = pdevice->vk.supported_extensions.NV_mesh_shader,
         .min_lod = true,
         .multiview = true,
         .physical_storage_buffer_address = true,
         .post_depth_coverage = true,
         .runtime_descriptor_array = true,
         .float_controls = true,
         .ray_cull_mask = rt_enabled,
         .ray_query = rt_enabled,
         .ray_tracing = rt_enabled,
         .shader_clock = true,
         .shader_viewport_index_layer = true,
         .stencil_export = true,
         .storage_8bit = true,
         .storage_16bit = true,
         .subgroup_arithmetic = true,
         .subgroup_basic = true,
         .subgroup_ballot = true,
         .subgroup_dispatch = true,
         .subgroup_quad = true,
         .subgroup_uniform_control_flow = true,
         .subgroup_shuffle = true,
         .subgroup_vote = true,
         .tessellation = true,
         .transform_feedback = true,
         .variable_pointers = true,
         .vk_memory_model = true,
         .vk_memory_model_device_scope = true,
         .workgroup_memory_explicit_layout = true,
         .fragment_shading_rate = pdevice->info.ver >= 11,
      },
      .ubo_addr_format =
         anv_nir_ubo_addr_format(pdevice, device->robust_buffer_access),
      .ssbo_addr_format =
          anv_nir_ssbo_addr_format(pdevice, device->robust_buffer_access),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_logical,

      /* TODO: Consider changing this to an address format that has the NULL
       * pointer equals to 0.  That might be a better format to play nice
       * with certain code / code generators.
       */
      .shared_addr_format = nir_address_format_32bit_offset,

      .min_ubo_alignment = ANV_UBO_ALIGNMENT,
      .min_ssbo_alignment = ANV_SSBO_ALIGNMENT,
   };

   nir_shader *nir;
   VkResult result =
      vk_pipeline_shader_stage_to_nir(&device->vk, stage_info,
                                      &spirv_options, nir_options,
                                      mem_ctx, &nir);
   if (result != VK_SUCCESS)
      return NULL;

   if (INTEL_DEBUG(intel_debug_flag_for_shader_stage(stage))) {
      fprintf(stderr, "NIR (from SPIR-V) for %s shader:\n",
              gl_shader_stage_name(stage));
      nir_print_shader(nir, stderr);
   }

   NIR_PASS_V(nir, nir_lower_io_to_temporaries,
              nir_shader_get_entrypoint(nir), true, false);

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .point_coord = true,
   };
   NIR_PASS(_, nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

   const nir_opt_access_options opt_access_options = {
      .is_vulkan = true,
   };
   NIR_PASS(_, nir, nir_opt_access, &opt_access_options);

   /* Vulkan uses the separate-shader linking model */
   nir->info.separate_shader = true;

   struct brw_nir_compiler_opts opts = {
      .softfp64 = device->fp64_nir,
      .robust_image_access =
         device->vk.enabled_features.robustImageAccess ||
         device->vk.enabled_features.robustImageAccess2,
   };
   brw_preprocess_nir(compiler, nir, &opts);

   if (nir->info.stage == MESA_SHADER_MESH && !nir->info.mesh.nv) {
      NIR_PASS(_, nir, anv_nir_lower_set_vtx_and_prim_count);
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_out, NULL);
   }

   return nir;
}

VkResult
anv_pipeline_init(struct anv_pipeline *pipeline,
                  struct anv_device *device,
                  enum anv_pipeline_type type,
                  VkPipelineCreateFlags flags,
                  const VkAllocationCallbacks *pAllocator)
{
   VkResult result;

   memset(pipeline, 0, sizeof(*pipeline));

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   pipeline->device = device;

   /* It's the job of the child class to provide actual backing storage for
    * the batch by setting batch.start, batch.next, and batch.end.
    */
   pipeline->batch.alloc = pAllocator ? pAllocator : &device->vk.alloc;
   pipeline->batch.relocs = &pipeline->batch_relocs;
   pipeline->batch.status = VK_SUCCESS;

   result = anv_reloc_list_init(&pipeline->batch_relocs,
                                pipeline->batch.alloc);
   if (result != VK_SUCCESS)
      return result;

   pipeline->mem_ctx = ralloc_context(NULL);

   pipeline->type = type;
   pipeline->flags = flags;

   util_dynarray_init(&pipeline->executables, pipeline->mem_ctx);

   return VK_SUCCESS;
}

void
anv_pipeline_finish(struct anv_pipeline *pipeline,
                    struct anv_device *device,
                    const VkAllocationCallbacks *pAllocator)
{
   anv_reloc_list_finish(&pipeline->batch_relocs,
                         pAllocator ? pAllocator : &device->vk.alloc);
   ralloc_free(pipeline->mem_ctx);
   vk_object_base_finish(&pipeline->base);
}

void anv_DestroyPipeline(
    VkDevice                                    _device,
    VkPipeline                                  _pipeline,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_pipeline, pipeline, _pipeline);

   if (!pipeline)
      return;

   switch (pipeline->type) {
   case ANV_PIPELINE_GRAPHICS: {
      struct anv_graphics_pipeline *gfx_pipeline =
         anv_pipeline_to_graphics(pipeline);

      for (unsigned s = 0; s < ARRAY_SIZE(gfx_pipeline->shaders); s++) {
         if (gfx_pipeline->shaders[s])
            anv_shader_bin_unref(device, gfx_pipeline->shaders[s]);
      }
      break;
   }

   case ANV_PIPELINE_COMPUTE: {
      struct anv_compute_pipeline *compute_pipeline =
         anv_pipeline_to_compute(pipeline);

      if (compute_pipeline->cs)
         anv_shader_bin_unref(device, compute_pipeline->cs);

      break;
   }

   case ANV_PIPELINE_RAY_TRACING: {
      struct anv_ray_tracing_pipeline *rt_pipeline =
         anv_pipeline_to_ray_tracing(pipeline);

      util_dynarray_foreach(&rt_pipeline->shaders,
                            struct anv_shader_bin *, shader) {
         anv_shader_bin_unref(device, *shader);
      }
      break;
   }

   default:
      unreachable("invalid pipeline type");
   }

   anv_pipeline_finish(pipeline, device, pAllocator);
   vk_free2(&device->vk.alloc, pAllocator, pipeline);
}

static void
populate_sampler_prog_key(const struct intel_device_info *devinfo,
                          struct brw_sampler_prog_key_data *key)
{
   for (int i = 0; i < BRW_MAX_SAMPLERS; i++) {
      /* Assume color sampler, no swizzling. (Works for BDW+) */
      key->swizzles[i] = SWIZZLE_XYZW;
   }
}

static void
populate_base_prog_key(const struct anv_device *device,
                       bool robust_buffer_acccess,
                       struct brw_base_prog_key *key)
{
   key->robust_buffer_access = robust_buffer_acccess;
   key->limit_trig_input_range =
      device->physical->instance->limit_trig_input_range;

   populate_sampler_prog_key(device->info, &key->tex);
}

static void
populate_vs_prog_key(const struct anv_device *device,
                     bool robust_buffer_acccess,
                     struct brw_vs_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_acccess, &key->base);
}

static void
populate_tcs_prog_key(const struct anv_device *device,
                      bool robust_buffer_acccess,
                      unsigned input_vertices,
                      struct brw_tcs_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_acccess, &key->base);

   key->input_vertices = input_vertices;
}

static void
populate_tes_prog_key(const struct anv_device *device,
                      bool robust_buffer_acccess,
                      struct brw_tes_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_acccess, &key->base);
}

static void
populate_gs_prog_key(const struct anv_device *device,
                     bool robust_buffer_acccess,
                     struct brw_gs_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_acccess, &key->base);
}

static bool
pipeline_has_coarse_pixel(const struct anv_graphics_pipeline *pipeline,
                          const BITSET_WORD *dynamic,
                          const struct vk_multisample_state *ms,
                          const struct vk_fragment_shading_rate_state *fsr)
{
   /* The Vulkan 1.2.199 spec says:
    *
    *    "If any of the following conditions are met, Cxy' must be set to
    *    {1,1}:
    *
    *     * If Sample Shading is enabled.
    *     * [...]"
    *
    * And "sample shading" is defined as follows:
    *
    *    "Sample shading is enabled for a graphics pipeline:
    *
    *     * If the interface of the fragment shader entry point of the
    *       graphics pipeline includes an input variable decorated with
    *       SampleId or SamplePosition. In this case minSampleShadingFactor
    *       takes the value 1.0.
    *
    *     * Else if the sampleShadingEnable member of the
    *       VkPipelineMultisampleStateCreateInfo structure specified when
    *       creating the graphics pipeline is set to VK_TRUE. In this case
    *       minSampleShadingFactor takes the value of
    *       VkPipelineMultisampleStateCreateInfo::minSampleShading.
    *
    *    Otherwise, sample shading is considered disabled."
    *
    * The first bullet above is handled by the back-end compiler because those
    * inputs both force per-sample dispatch.  The second bullet is handled
    * here.  Note that this sample shading being enabled has nothing to do
    * with minSampleShading.
    */
   if (ms != NULL && ms->sample_shading_enable)
      return false;

   /* Not dynamic & pipeline has a 1x1 fragment shading rate with no
    * possibility for element of the pipeline to change the value.
    */
   if (!BITSET_TEST(dynamic, MESA_VK_DYNAMIC_FSR) &&
       fsr->fragment_size.width <= 1 &&
       fsr->fragment_size.height <= 1 &&
       fsr->combiner_ops[0] == VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR &&
       fsr->combiner_ops[1] == VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR)
      return false;

   return true;
}

static void
populate_task_prog_key(const struct anv_device *device,
                       bool robust_buffer_access,
                       struct brw_task_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_access, &key->base);
}

static void
populate_mesh_prog_key(const struct anv_device *device,
                       bool robust_buffer_access,
                       struct brw_mesh_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_access, &key->base);
}

static void
populate_wm_prog_key(const struct anv_graphics_pipeline *pipeline,
                     bool robust_buffer_acccess,
                     const BITSET_WORD *dynamic,
                     const struct vk_multisample_state *ms,
                     const struct vk_fragment_shading_rate_state *fsr,
                     const struct vk_render_pass_state *rp,
                     struct brw_wm_prog_key *key)
{
   const struct anv_device *device = pipeline->base.device;

   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_acccess, &key->base);

   /* We set this to 0 here and set to the actual value before we call
    * brw_compile_fs.
    */
   key->input_slots_valid = 0;

   /* XXX Vulkan doesn't appear to specify */
   key->clamp_fragment_color = false;

   key->ignore_sample_mask_out = false;

   assert(rp->color_attachment_count <= MAX_RTS);
   /* Consider all inputs as valid until look at the NIR variables. */
   key->color_outputs_valid = (1u << rp->color_attachment_count) - 1;
   key->nr_color_regions = rp->color_attachment_count;

   /* To reduce possible shader recompilations we would need to know if
    * there is a SampleMask output variable to compute if we should emit
    * code to workaround the issue that hardware disables alpha to coverage
    * when there is SampleMask output.
    */
   key->alpha_to_coverage = ms != NULL && ms->alpha_to_coverage_enable ?
      BRW_ALWAYS : BRW_NEVER;

   /* Vulkan doesn't support fixed-function alpha test */
   key->alpha_test_replicate_alpha = false;

   if (ms != NULL) {
      /* We should probably pull this out of the shader, but it's fairly
       * harmless to compute it and then let dead-code take care of it.
       */
      if (ms->rasterization_samples > 1) {
         key->persample_interp =
            (ms->sample_shading_enable &&
             (ms->min_sample_shading * ms->rasterization_samples) > 1) ?
            BRW_ALWAYS : BRW_NEVER;
         key->multisample_fbo = BRW_ALWAYS;
      }

      if (device->physical->instance->sample_mask_out_opengl_behaviour)
         key->ignore_sample_mask_out = !key->multisample_fbo;
   }

   key->coarse_pixel =
      !key->persample_interp &&
      device->vk.enabled_extensions.KHR_fragment_shading_rate &&
      pipeline_has_coarse_pixel(pipeline, dynamic, ms, fsr);
}

static void
populate_cs_prog_key(const struct anv_device *device,
                     bool robust_buffer_acccess,
                     struct brw_cs_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_acccess, &key->base);
}

static void
populate_bs_prog_key(const struct anv_device *device,
                     bool robust_buffer_access,
                     uint32_t ray_flags,
                     struct brw_bs_prog_key *key)
{
   memset(key, 0, sizeof(*key));

   populate_base_prog_key(device, robust_buffer_access, &key->base);

   key->pipeline_ray_flags = ray_flags;
}

struct anv_pipeline_stage {
   gl_shader_stage stage;

   const VkPipelineShaderStageCreateInfo *info;

   unsigned char shader_sha1[20];

   union brw_any_prog_key key;

   struct {
      gl_shader_stage stage;
      unsigned char sha1[20];
   } cache_key;

   nir_shader *nir;

   struct anv_push_descriptor_info push_desc_info;

   struct anv_pipeline_binding surface_to_descriptor[256];
   struct anv_pipeline_binding sampler_to_descriptor[256];
   struct anv_pipeline_bind_map bind_map;

   bool uses_bt_for_push_descs;

   union brw_any_prog_data prog_data;

   uint32_t num_stats;
   struct brw_compile_stats stats[3];
   char *disasm[3];

   VkPipelineCreationFeedback feedback;

   const unsigned *code;

   struct anv_shader_bin *bin;
};

static void
anv_pipeline_hash_graphics(struct anv_graphics_pipeline *pipeline,
                           struct anv_pipeline_layout *layout,
                           struct anv_pipeline_stage *stages,
                           unsigned char *sha1_out)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   _mesa_sha1_update(&ctx, &pipeline->view_mask,
                     sizeof(pipeline->view_mask));

   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   const struct anv_device *device = pipeline->base.device;

   const bool rba = device->robust_buffer_access;
   _mesa_sha1_update(&ctx, &rba, sizeof(rba));

   for (uint32_t s = 0; s < ANV_GRAPHICS_SHADER_STAGE_COUNT; s++) {
      if (stages[s].info) {
         _mesa_sha1_update(&ctx, stages[s].shader_sha1,
                           sizeof(stages[s].shader_sha1));
         _mesa_sha1_update(&ctx, &stages[s].key, brw_prog_key_size(s));
      }
   }

   if (stages[MESA_SHADER_MESH].info || stages[MESA_SHADER_TASK].info) {
      const bool afs = device->physical->instance->assume_full_subgroups;
      _mesa_sha1_update(&ctx, &afs, sizeof(afs));
   }

   _mesa_sha1_final(&ctx, sha1_out);
}

static void
anv_pipeline_hash_compute(struct anv_compute_pipeline *pipeline,
                          struct anv_pipeline_layout *layout,
                          struct anv_pipeline_stage *stage,
                          unsigned char *sha1_out)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   const struct anv_device *device = pipeline->base.device;

   const bool rba = device->robust_buffer_access;
   _mesa_sha1_update(&ctx, &rba, sizeof(rba));

   const bool afs = device->physical->instance->assume_full_subgroups;
   _mesa_sha1_update(&ctx, &afs, sizeof(afs));

   _mesa_sha1_update(&ctx, stage->shader_sha1,
                     sizeof(stage->shader_sha1));
   _mesa_sha1_update(&ctx, &stage->key.cs, sizeof(stage->key.cs));

   _mesa_sha1_final(&ctx, sha1_out);
}

static void
anv_pipeline_hash_ray_tracing_shader(struct anv_ray_tracing_pipeline *pipeline,
                                     struct anv_pipeline_layout *layout,
                                     struct anv_pipeline_stage *stage,
                                     unsigned char *sha1_out)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   if (layout != NULL)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   const bool rba = pipeline->base.device->robust_buffer_access;
   _mesa_sha1_update(&ctx, &rba, sizeof(rba));

   _mesa_sha1_update(&ctx, stage->shader_sha1, sizeof(stage->shader_sha1));
   _mesa_sha1_update(&ctx, &stage->key, sizeof(stage->key.bs));

   _mesa_sha1_final(&ctx, sha1_out);
}

static void
anv_pipeline_hash_ray_tracing_combined_shader(struct anv_ray_tracing_pipeline *pipeline,
                                              struct anv_pipeline_layout *layout,
                                              struct anv_pipeline_stage *intersection,
                                              struct anv_pipeline_stage *any_hit,
                                              unsigned char *sha1_out)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   if (layout != NULL)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   const bool rba = pipeline->base.device->robust_buffer_access;
   _mesa_sha1_update(&ctx, &rba, sizeof(rba));

   _mesa_sha1_update(&ctx, intersection->shader_sha1, sizeof(intersection->shader_sha1));
   _mesa_sha1_update(&ctx, &intersection->key, sizeof(intersection->key.bs));
   _mesa_sha1_update(&ctx, any_hit->shader_sha1, sizeof(any_hit->shader_sha1));
   _mesa_sha1_update(&ctx, &any_hit->key, sizeof(any_hit->key.bs));

   _mesa_sha1_final(&ctx, sha1_out);
}

static nir_shader *
anv_pipeline_stage_get_nir(struct anv_pipeline *pipeline,
                           struct vk_pipeline_cache *cache,
                           void *mem_ctx,
                           struct anv_pipeline_stage *stage)
{
   const struct brw_compiler *compiler =
      pipeline->device->physical->compiler;
   const nir_shader_compiler_options *nir_options =
      compiler->nir_options[stage->stage];
   nir_shader *nir;

   nir = anv_device_search_for_nir(pipeline->device, cache,
                                   nir_options,
                                   stage->shader_sha1,
                                   mem_ctx);
   if (nir) {
      assert(nir->info.stage == stage->stage);
      return nir;
   }

   nir = anv_shader_stage_to_nir(pipeline->device, stage->info, mem_ctx);
   if (nir) {
      anv_device_upload_nir(pipeline->device, cache, nir, stage->shader_sha1);
      return nir;
   }

   return NULL;
}

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *_pipeline_layout, uint32_t set,
                        uint32_t binding, uint32_t array_index)
{
   const struct anv_pipeline_layout *pipeline_layout = _pipeline_layout;

   assert(set < MAX_SETS);
   assert(binding < pipeline_layout->set[set].layout->binding_count);
   const struct anv_descriptor_set_binding_layout *bind_layout =
      &pipeline_layout->set[set].layout->binding[binding];

   if (bind_layout->immutable_samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->array_size - 1);

   const struct anv_sampler *sampler =
      bind_layout->immutable_samplers[array_index];

   return sampler && sampler->conversion ? &sampler->conversion->state : NULL;
}

static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
   *align = comp_size * (length == 3 ? 4 : length);
}

static void
anv_pipeline_lower_nir(struct anv_pipeline *pipeline,
                       void *mem_ctx,
                       struct anv_pipeline_stage *stage,
                       struct anv_pipeline_layout *layout,
                       bool use_primitive_replication)
{
   const struct anv_physical_device *pdevice = pipeline->device->physical;
   const struct brw_compiler *compiler = pdevice->compiler;

   struct brw_stage_prog_data *prog_data = &stage->prog_data.base;
   nir_shader *nir = stage->nir;

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_wpos_center);
      NIR_PASS(_, nir, nir_lower_input_attachments,
               &(nir_input_attachment_options) {
                   .use_fragcoord_sysval = true,
                   .use_layer_id_sysval = true,
               });
   }

   if ((nir->info.stage == MESA_SHADER_MESH ||
         nir->info.stage == MESA_SHADER_TASK) && !nir->info.mesh.nv) {
      /* We can't/shouldn't lower id to index for NV_mesh_shader, because:
       * 3DMESH_1D doesn't expose registers needed for
       * nir_intrinsic_load_num_workgroups (generated by this pass)
       * and we can't unify NV with EXT, because 3DMESH_3D doesn't support
       * vkCmdDrawMeshTasksNV.firstTask.
       */
      nir_lower_compute_system_values_options options = {
            .lower_cs_local_id_to_index = true,
            .lower_workgroup_id_to_index = true,
      };

      NIR_PASS(_, nir, nir_lower_compute_system_values, &options);
   }

   NIR_PASS(_, nir, nir_vk_lower_ycbcr_tex, lookup_ycbcr_conversion, layout);

   if (pipeline->type == ANV_PIPELINE_GRAPHICS) {
      struct anv_graphics_pipeline *gfx_pipeline =
         anv_pipeline_to_graphics(pipeline);
      NIR_PASS(_, nir, anv_nir_lower_multiview, gfx_pipeline->view_mask,
               use_primitive_replication);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   NIR_PASS(_, nir, brw_nir_lower_storage_image, compiler->devinfo);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_global,
            nir_address_format_64bit_global);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);

   NIR_PASS(_, nir, brw_nir_lower_ray_queries, &pdevice->info);

   stage->push_desc_info.used_descriptors =
      anv_nir_compute_used_push_descriptors(nir, layout);

   /* Apply the actual pipeline layout to UBOs, SSBOs, and textures */
   NIR_PASS_V(nir, anv_nir_apply_pipeline_layout,
              pdevice, pipeline->device->robust_buffer_access,
              layout, &stage->bind_map);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo,
            anv_nir_ubo_addr_format(pdevice, pipeline->device->robust_buffer_access));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ssbo,
            anv_nir_ssbo_addr_format(pdevice, pipeline->device->robust_buffer_access));

   /* First run copy-prop to get rid of all of the vec() that address
    * calculations often create and then constant-fold so that, when we
    * get to anv_nir_lower_ubo_loads, we can detect constant offsets.
    */
   NIR_PASS(_, nir, nir_copy_prop);
   NIR_PASS(_, nir, nir_opt_constant_folding);

   /* Required for nir_divergence_analysis() which is needed for
    * anv_nir_lower_ubo_loads.
    */
   NIR_PASS(_, nir, nir_convert_to_lcssa, true, true);
   nir_divergence_analysis(nir);

   NIR_PASS(_, nir, anv_nir_lower_ubo_loads);

   NIR_PASS(_, nir, nir_opt_remove_phis);

   enum nir_lower_non_uniform_access_type lower_non_uniform_access_types =
      nir_lower_non_uniform_texture_access | nir_lower_non_uniform_image_access;

   /* In practice, most shaders do not have non-uniform-qualified
    * accesses (see
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/17558#note_1475069)
    * thus a cheaper and likely to fail check is run first.
    */
   if (nir_has_non_uniform_access(nir, lower_non_uniform_access_types)) {
      NIR_PASS(_, nir, nir_opt_non_uniform_access);

      /* We don't support non-uniform UBOs and non-uniform SSBO access is
      * handled naturally by falling back to A64 messages.
      */
      NIR_PASS(_, nir, nir_lower_non_uniform_access,
               &(nir_lower_non_uniform_access_options) {
                  .types = lower_non_uniform_access_types,
                  .callback = NULL,
               });
   }

   NIR_PASS_V(nir, anv_nir_compute_push_layout,
              pdevice, pipeline->device->robust_buffer_access,
              prog_data, &stage->bind_map, mem_ctx);

   if (gl_shader_stage_uses_workgroup(nir->info.stage)) {
      if (!nir->info.shared_memory_explicit_layout) {
         NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
                  nir_var_mem_shared, shared_type_info);
      }

      NIR_PASS(_, nir, nir_lower_explicit_io,
               nir_var_mem_shared, nir_address_format_32bit_offset);

      if (nir->info.zero_initialize_shared_memory &&
          nir->info.shared_size > 0) {
         /* The effective Shared Local Memory size is at least 1024 bytes and
          * is always rounded to a power of two, so it is OK to align the size
          * used by the shader to chunk_size -- which does simplify the logic.
          */
         const unsigned chunk_size = 16;
         const unsigned shared_size = ALIGN(nir->info.shared_size, chunk_size);
         assert(shared_size <=
                intel_calculate_slm_size(compiler->devinfo->ver, nir->info.shared_size));

         NIR_PASS(_, nir, nir_zero_initialize_shared_memory,
                  shared_size, chunk_size);
      }
   }

   if (gl_shader_stage_is_compute(nir->info.stage) ||
       gl_shader_stage_is_mesh(nir->info.stage))
      NIR_PASS(_, nir, brw_nir_lower_cs_intrinsics);

   stage->push_desc_info.used_set_buffer =
      anv_nir_loads_push_desc_buffer(nir, layout, &stage->bind_map);
   stage->push_desc_info.fully_promoted_ubo_descriptors =
      anv_nir_push_desc_ubo_fully_promoted(nir, layout, &stage->bind_map);

   stage->nir = nir;
}

static void
anv_pipeline_link_vs(const struct brw_compiler *compiler,
                     struct anv_pipeline_stage *vs_stage,
                     struct anv_pipeline_stage *next_stage)
{
   if (next_stage)
      brw_nir_link_shaders(compiler, vs_stage->nir, next_stage->nir);
}

static void
anv_pipeline_compile_vs(const struct brw_compiler *compiler,
                        void *mem_ctx,
                        struct anv_graphics_pipeline *pipeline,
                        struct anv_pipeline_stage *vs_stage)
{
   /* When using Primitive Replication for multiview, each view gets its own
    * position slot.
    */
   uint32_t pos_slots =
      (vs_stage->nir->info.per_view_outputs & VARYING_BIT_POS) ?
      MAX2(1, util_bitcount(pipeline->view_mask)) : 1;

   /* Only position is allowed to be per-view */
   assert(!(vs_stage->nir->info.per_view_outputs & ~VARYING_BIT_POS));

   brw_compute_vue_map(compiler->devinfo,
                       &vs_stage->prog_data.vs.base.vue_map,
                       vs_stage->nir->info.outputs_written,
                       vs_stage->nir->info.separate_shader,
                       pos_slots);

   vs_stage->num_stats = 1;

   struct brw_compile_vs_params params = {
      .nir = vs_stage->nir,
      .key = &vs_stage->key.vs,
      .prog_data = &vs_stage->prog_data.vs,
      .stats = vs_stage->stats,
      .log_data = pipeline->base.device,
   };

   vs_stage->code = brw_compile_vs(compiler, mem_ctx, &params);
}

static void
merge_tess_info(struct shader_info *tes_info,
                const struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 ||
          tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == 0 ||
          tes_info->tess._primitive_mode == 0 ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;
}

static void
anv_pipeline_link_tcs(const struct brw_compiler *compiler,
                      struct anv_pipeline_stage *tcs_stage,
                      struct anv_pipeline_stage *tes_stage)
{
   assert(tes_stage && tes_stage->stage == MESA_SHADER_TESS_EVAL);

   brw_nir_link_shaders(compiler, tcs_stage->nir, tes_stage->nir);

   nir_lower_patch_vertices(tes_stage->nir,
                            tcs_stage->nir->info.tess.tcs_vertices_out,
                            NULL);

   /* Copy TCS info into the TES info */
   merge_tess_info(&tes_stage->nir->info, &tcs_stage->nir->info);

   /* Whacking the key after cache lookup is a bit sketchy, but all of
    * this comes from the SPIR-V, which is part of the hash used for the
    * pipeline cache.  So it should be safe.
    */
   tcs_stage->key.tcs._tes_primitive_mode =
      tes_stage->nir->info.tess._primitive_mode;
}

static void
anv_pipeline_compile_tcs(const struct brw_compiler *compiler,
                         void *mem_ctx,
                         struct anv_device *device,
                         struct anv_pipeline_stage *tcs_stage,
                         struct anv_pipeline_stage *prev_stage)
{
   tcs_stage->key.tcs.outputs_written =
      tcs_stage->nir->info.outputs_written;
   tcs_stage->key.tcs.patch_outputs_written =
      tcs_stage->nir->info.patch_outputs_written;

   tcs_stage->num_stats = 1;

   struct brw_compile_tcs_params params = {
      .nir = tcs_stage->nir,
      .key = &tcs_stage->key.tcs,
      .prog_data = &tcs_stage->prog_data.tcs,
      .stats = tcs_stage->stats,
      .log_data = device,
   };

   tcs_stage->code = brw_compile_tcs(compiler, mem_ctx, &params);
}

static void
anv_pipeline_link_tes(const struct brw_compiler *compiler,
                      struct anv_pipeline_stage *tes_stage,
                      struct anv_pipeline_stage *next_stage)
{
   if (next_stage)
      brw_nir_link_shaders(compiler, tes_stage->nir, next_stage->nir);
}

static void
anv_pipeline_compile_tes(const struct brw_compiler *compiler,
                         void *mem_ctx,
                         struct anv_device *device,
                         struct anv_pipeline_stage *tes_stage,
                         struct anv_pipeline_stage *tcs_stage)
{
   tes_stage->key.tes.inputs_read =
      tcs_stage->nir->info.outputs_written;
   tes_stage->key.tes.patch_inputs_read =
      tcs_stage->nir->info.patch_outputs_written;

   tes_stage->num_stats = 1;

   struct brw_compile_tes_params params = {
      .nir = tes_stage->nir,
      .key = &tes_stage->key.tes,
      .prog_data = &tes_stage->prog_data.tes,
      .input_vue_map = &tcs_stage->prog_data.tcs.base.vue_map,
      .stats = tes_stage->stats,
      .log_data = device,
   };

   tes_stage->code = brw_compile_tes(compiler, mem_ctx, &params);
}

static void
anv_pipeline_link_gs(const struct brw_compiler *compiler,
                     struct anv_pipeline_stage *gs_stage,
                     struct anv_pipeline_stage *next_stage)
{
   if (next_stage)
      brw_nir_link_shaders(compiler, gs_stage->nir, next_stage->nir);
}

static void
anv_pipeline_compile_gs(const struct brw_compiler *compiler,
                        void *mem_ctx,
                        struct anv_device *device,
                        struct anv_pipeline_stage *gs_stage,
                        struct anv_pipeline_stage *prev_stage)
{
   brw_compute_vue_map(compiler->devinfo,
                       &gs_stage->prog_data.gs.base.vue_map,
                       gs_stage->nir->info.outputs_written,
                       gs_stage->nir->info.separate_shader, 1);

   gs_stage->num_stats = 1;

   struct brw_compile_gs_params params = {
      .nir = gs_stage->nir,
      .key = &gs_stage->key.gs,
      .prog_data = &gs_stage->prog_data.gs,
      .stats = gs_stage->stats,
      .log_data = device,
   };

   gs_stage->code = brw_compile_gs(compiler, mem_ctx, &params);
}

static void
anv_pipeline_link_task(const struct brw_compiler *compiler,
                       struct anv_pipeline_stage *task_stage,
                       struct anv_pipeline_stage *next_stage)
{
   assert(next_stage);
   assert(next_stage->stage == MESA_SHADER_MESH);
   brw_nir_link_shaders(compiler, task_stage->nir, next_stage->nir);
}

static void
anv_pipeline_compile_task(const struct brw_compiler *compiler,
                          void *mem_ctx,
                          struct anv_device *device,
                          struct anv_pipeline_stage *task_stage)
{
   task_stage->num_stats = 1;

   struct brw_compile_task_params params = {
      .nir = task_stage->nir,
      .key = &task_stage->key.task,
      .prog_data = &task_stage->prog_data.task,
      .stats = task_stage->stats,
      .log_data = device,
   };

   task_stage->code = brw_compile_task(compiler, mem_ctx, &params);
}

static void
anv_pipeline_link_mesh(const struct brw_compiler *compiler,
                       struct anv_pipeline_stage *mesh_stage,
                       struct anv_pipeline_stage *next_stage)
{
   if (next_stage) {
      brw_nir_link_shaders(compiler, mesh_stage->nir, next_stage->nir);
   }
}

static void
anv_pipeline_compile_mesh(const struct brw_compiler *compiler,
                          void *mem_ctx,
                          struct anv_device *device,
                          struct anv_pipeline_stage *mesh_stage,
                          struct anv_pipeline_stage *prev_stage)
{
   mesh_stage->num_stats = 1;

   struct brw_compile_mesh_params params = {
      .nir = mesh_stage->nir,
      .key = &mesh_stage->key.mesh,
      .prog_data = &mesh_stage->prog_data.mesh,
      .stats = mesh_stage->stats,
      .log_data = device,
   };

   if (prev_stage) {
      assert(prev_stage->stage == MESA_SHADER_TASK);
      params.tue_map = &prev_stage->prog_data.task.map;
   }

   mesh_stage->code = brw_compile_mesh(compiler, mem_ctx, &params);
}

static void
anv_pipeline_link_fs(const struct brw_compiler *compiler,
                     struct anv_pipeline_stage *stage,
                     const struct vk_render_pass_state *rp)
{
   /* Initially the valid outputs value is set to all possible render targets
    * valid (see populate_wm_prog_key()), before we look at the shader
    * variables. Here we look at the output variables of the shader an compute
    * a correct number of render target outputs.
    */
   stage->key.wm.color_outputs_valid = 0;
   nir_foreach_shader_out_variable_safe(var, stage->nir) {
      if (var->data.location < FRAG_RESULT_DATA0)
         continue;

      const unsigned rt = var->data.location - FRAG_RESULT_DATA0;
      const unsigned array_len =
         glsl_type_is_array(var->type) ? glsl_get_length(var->type) : 1;
      assert(rt + array_len <= MAX_RTS);

      stage->key.wm.color_outputs_valid |= BITFIELD_RANGE(rt, array_len);
   }
   stage->key.wm.color_outputs_valid &=
      (1u << rp->color_attachment_count) - 1;
   stage->key.wm.nr_color_regions =
      util_last_bit(stage->key.wm.color_outputs_valid);

   unsigned num_rt_bindings;
   struct anv_pipeline_binding rt_bindings[MAX_RTS];
   if (stage->key.wm.nr_color_regions > 0) {
      assert(stage->key.wm.nr_color_regions <= MAX_RTS);
      for (unsigned rt = 0; rt < stage->key.wm.nr_color_regions; rt++) {
         if (stage->key.wm.color_outputs_valid & BITFIELD_BIT(rt)) {
            rt_bindings[rt] = (struct anv_pipeline_binding) {
               .set = ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS,
               .index = rt,
               .binding = UINT32_MAX,

            };
         } else {
            /* Setup a null render target */
            rt_bindings[rt] = (struct anv_pipeline_binding) {
               .set = ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS,
               .index = UINT32_MAX,
               .binding = UINT32_MAX,
            };
         }
      }
      num_rt_bindings = stage->key.wm.nr_color_regions;
   } else {
      /* Setup a null render target */
      rt_bindings[0] = (struct anv_pipeline_binding) {
         .set = ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS,
         .index = UINT32_MAX,
      };
      num_rt_bindings = 1;
   }

   assert(num_rt_bindings <= MAX_RTS);
   assert(stage->bind_map.surface_count == 0);
   typed_memcpy(stage->bind_map.surface_to_descriptor,
                rt_bindings, num_rt_bindings);
   stage->bind_map.surface_count += num_rt_bindings;
}

static void
anv_pipeline_compile_fs(const struct brw_compiler *compiler,
                        void *mem_ctx,
                        struct anv_device *device,
                        struct anv_pipeline_stage *fs_stage,
                        struct anv_pipeline_stage *prev_stage)
{
   /* TODO: we could set this to 0 based on the information in nir_shader, but
    * we need this before we call spirv_to_nir.
    */
   assert(prev_stage);

   struct brw_compile_fs_params params = {
      .nir = fs_stage->nir,
      .key = &fs_stage->key.wm,
      .prog_data = &fs_stage->prog_data.wm,

      .allow_spilling = true,
      .stats = fs_stage->stats,
      .log_data = device,
   };

   if (prev_stage->stage == MESA_SHADER_MESH) {
      params.mue_map = &prev_stage->prog_data.mesh.map;
      /* TODO(mesh): Slots valid, do we even use/rely on it? */
   } else {
      fs_stage->key.wm.input_slots_valid =
         prev_stage->prog_data.vue.vue_map.slots_valid;
   }

   fs_stage->code = brw_compile_fs(compiler, mem_ctx, &params);

   fs_stage->num_stats = (uint32_t)fs_stage->prog_data.wm.dispatch_8 +
                         (uint32_t)fs_stage->prog_data.wm.dispatch_16 +
                         (uint32_t)fs_stage->prog_data.wm.dispatch_32;
}

static void
anv_pipeline_add_executable(struct anv_pipeline *pipeline,
                            struct anv_pipeline_stage *stage,
                            struct brw_compile_stats *stats,
                            uint32_t code_offset)
{
   char *nir = NULL;
   if (stage->nir &&
       (pipeline->flags &
        VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR)) {
      nir = nir_shader_as_str(stage->nir, pipeline->mem_ctx);
   }

   char *disasm = NULL;
   if (stage->code &&
       (pipeline->flags &
        VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR)) {
      char *stream_data = NULL;
      size_t stream_size = 0;
      FILE *stream = open_memstream(&stream_data, &stream_size);

      uint32_t push_size = 0;
      for (unsigned i = 0; i < 4; i++)
         push_size += stage->bind_map.push_ranges[i].length;
      if (push_size > 0) {
         fprintf(stream, "Push constant ranges:\n");
         for (unsigned i = 0; i < 4; i++) {
            if (stage->bind_map.push_ranges[i].length == 0)
               continue;

            fprintf(stream, "    RANGE%d (%dB): ", i,
                    stage->bind_map.push_ranges[i].length * 32);

            switch (stage->bind_map.push_ranges[i].set) {
            case ANV_DESCRIPTOR_SET_NULL:
               fprintf(stream, "NULL");
               break;

            case ANV_DESCRIPTOR_SET_PUSH_CONSTANTS:
               fprintf(stream, "Vulkan push constants and API params");
               break;

            case ANV_DESCRIPTOR_SET_DESCRIPTORS:
               fprintf(stream, "Descriptor buffer for set %d (start=%dB)",
                       stage->bind_map.push_ranges[i].index,
                       stage->bind_map.push_ranges[i].start * 32);
               break;

            case ANV_DESCRIPTOR_SET_NUM_WORK_GROUPS:
               unreachable("gl_NumWorkgroups is never pushed");

            case ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS:
               unreachable("Color attachments can't be pushed");

            default:
               fprintf(stream, "UBO (set=%d binding=%d start=%dB)",
                       stage->bind_map.push_ranges[i].set,
                       stage->bind_map.push_ranges[i].index,
                       stage->bind_map.push_ranges[i].start * 32);
               break;
            }
            fprintf(stream, "\n");
         }
         fprintf(stream, "\n");
      }

      /* Creating this is far cheaper than it looks.  It's perfectly fine to
       * do it for every binary.
       */
      intel_disassemble(&pipeline->device->physical->compiler->isa,
                        stage->code, code_offset, stream);

      fclose(stream);

      /* Copy it to a ralloc'd thing */
      disasm = ralloc_size(pipeline->mem_ctx, stream_size + 1);
      memcpy(disasm, stream_data, stream_size);
      disasm[stream_size] = 0;

      free(stream_data);
   }

   const struct anv_pipeline_executable exe = {
      .stage = stage->stage,
      .stats = *stats,
      .nir = nir,
      .disasm = disasm,
   };
   util_dynarray_append(&pipeline->executables,
                        struct anv_pipeline_executable, exe);
}

static void
anv_pipeline_add_executables(struct anv_pipeline *pipeline,
                             struct anv_pipeline_stage *stage,
                             struct anv_shader_bin *bin)
{
   if (stage->stage == MESA_SHADER_FRAGMENT) {
      /* We pull the prog data and stats out of the anv_shader_bin because
       * the anv_pipeline_stage may not be fully populated if we successfully
       * looked up the shader in a cache.
       */
      const struct brw_wm_prog_data *wm_prog_data =
         (const struct brw_wm_prog_data *)bin->prog_data;
      struct brw_compile_stats *stats = bin->stats;

      if (wm_prog_data->dispatch_8) {
         anv_pipeline_add_executable(pipeline, stage, stats++, 0);
      }

      if (wm_prog_data->dispatch_16) {
         anv_pipeline_add_executable(pipeline, stage, stats++,
                                     wm_prog_data->prog_offset_16);
      }

      if (wm_prog_data->dispatch_32) {
         anv_pipeline_add_executable(pipeline, stage, stats++,
                                     wm_prog_data->prog_offset_32);
      }
   } else {
      anv_pipeline_add_executable(pipeline, stage, bin->stats, 0);
   }

   pipeline->ray_queries = MAX2(pipeline->ray_queries, bin->prog_data->ray_queries);

   if (bin->push_desc_info.used_set_buffer) {
      pipeline->use_push_descriptor_buffer |=
         BITFIELD_BIT(mesa_to_vk_shader_stage(stage->stage));
   }
   if (bin->push_desc_info.used_descriptors &
       ~bin->push_desc_info.fully_promoted_ubo_descriptors)
      pipeline->use_push_descriptor |= mesa_to_vk_shader_stage(stage->stage);
}

static void
anv_graphics_pipeline_init_keys(struct anv_graphics_pipeline *pipeline,
                                const struct vk_graphics_pipeline_state *state,
                                struct anv_pipeline_stage *stages)
{
   for (uint32_t s = 0; s < ANV_GRAPHICS_SHADER_STAGE_COUNT; s++) {
      if (!stages[s].info)
         continue;

      int64_t stage_start = os_time_get_nano();

      vk_pipeline_hash_shader_stage(stages[s].info, NULL, stages[s].shader_sha1);

      const struct anv_device *device = pipeline->base.device;
      switch (stages[s].stage) {
      case MESA_SHADER_VERTEX:
         populate_vs_prog_key(device,
                              pipeline->base.device->robust_buffer_access,
                              &stages[s].key.vs);
         break;
      case MESA_SHADER_TESS_CTRL:
         populate_tcs_prog_key(device,
                               pipeline->base.device->robust_buffer_access,
                               state->ts->patch_control_points,
                               &stages[s].key.tcs);
         break;
      case MESA_SHADER_TESS_EVAL:
         populate_tes_prog_key(device,
                               pipeline->base.device->robust_buffer_access,
                               &stages[s].key.tes);
         break;
      case MESA_SHADER_GEOMETRY:
         populate_gs_prog_key(device,
                              pipeline->base.device->robust_buffer_access,
                              &stages[s].key.gs);
         break;
      case MESA_SHADER_FRAGMENT: {
         populate_wm_prog_key(pipeline,
                              pipeline->base.device->robust_buffer_access,
                              state->dynamic, state->ms, state->fsr, state->rp,
                              &stages[s].key.wm);
         break;
      }
      case MESA_SHADER_TASK:
         populate_task_prog_key(device,
                                pipeline->base.device->robust_buffer_access,
                                &stages[s].key.task);
         break;
      case MESA_SHADER_MESH:
         populate_mesh_prog_key(device,
                                pipeline->base.device->robust_buffer_access,
                                &stages[s].key.mesh);
         break;
      default:
         unreachable("Invalid graphics shader stage");
      }

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
      stages[s].feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
   }

   assert(pipeline->active_stages & VK_SHADER_STAGE_VERTEX_BIT ||
          pipeline->active_stages & VK_SHADER_STAGE_MESH_BIT_EXT);
}

static bool
anv_graphics_pipeline_load_cached_shaders(struct anv_graphics_pipeline *pipeline,
                                          struct vk_pipeline_cache *cache,
                                          struct anv_pipeline_stage *stages,
                                          VkPipelineCreationFeedbackEXT *pipeline_feedback)
{
   unsigned found = 0;
   unsigned cache_hits = 0;
   for (unsigned s = 0; s < ANV_GRAPHICS_SHADER_STAGE_COUNT; s++) {
      if (!stages[s].info)
         continue;

      int64_t stage_start = os_time_get_nano();

      bool cache_hit;
      struct anv_shader_bin *bin =
         anv_device_search_for_kernel(pipeline->base.device, cache,
                                      &stages[s].cache_key,
                                      sizeof(stages[s].cache_key), &cache_hit);
      if (bin) {
         found++;
         pipeline->shaders[s] = bin;
      }

      if (cache_hit) {
         cache_hits++;
         stages[s].feedback.flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      }
      stages[s].feedback.duration += os_time_get_nano() - stage_start;
   }

   if (found == __builtin_popcount(pipeline->active_stages)) {
      if (cache_hits == found) {
         pipeline_feedback->flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      }
      /* We found all our shaders in the cache.  We're done. */
      for (unsigned s = 0; s < ARRAY_SIZE(pipeline->shaders); s++) {
         if (!stages[s].info)
            continue;

         anv_pipeline_add_executables(&pipeline->base, &stages[s],
                                      pipeline->shaders[s]);
      }
      return true;
   } else if (found > 0) {
      /* We found some but not all of our shaders. This shouldn't happen most
       * of the time but it can if we have a partially populated pipeline
       * cache.
       */
      assert(found < __builtin_popcount(pipeline->active_stages));

      vk_perf(VK_LOG_OBJS(cache ? &cache->base :
                                  &pipeline->base.device->vk.base),
              "Found a partial pipeline in the cache.  This is "
              "most likely caused by an incomplete pipeline cache "
              "import or export");

      /* We're going to have to recompile anyway, so just throw away our
       * references to the shaders in the cache.  We'll get them out of the
       * cache again as part of the compilation process.
       */
      for (unsigned s = 0; s < ARRAY_SIZE(pipeline->shaders); s++) {
         stages[s].feedback.flags = 0;
         if (pipeline->shaders[s]) {
            anv_shader_bin_unref(pipeline->base.device, pipeline->shaders[s]);
            pipeline->shaders[s] = NULL;
         }
      }
   }

   return false;
}

static const gl_shader_stage graphics_shader_order[] = {
   MESA_SHADER_VERTEX,
   MESA_SHADER_TESS_CTRL,
   MESA_SHADER_TESS_EVAL,
   MESA_SHADER_GEOMETRY,

   MESA_SHADER_TASK,
   MESA_SHADER_MESH,

   MESA_SHADER_FRAGMENT,
};

static VkResult
anv_graphics_pipeline_load_nir(struct anv_graphics_pipeline *pipeline,
                               struct vk_pipeline_cache *cache,
                               struct anv_pipeline_stage *stages,
                               void *pipeline_ctx)
{
   for (unsigned i = 0; i < ARRAY_SIZE(graphics_shader_order); i++) {
      gl_shader_stage s = graphics_shader_order[i];
      if (!stages[s].info)
         continue;

      int64_t stage_start = os_time_get_nano();

      assert(stages[s].stage == s);
      assert(pipeline->shaders[s] == NULL);

      stages[s].bind_map = (struct anv_pipeline_bind_map) {
         .surface_to_descriptor = stages[s].surface_to_descriptor,
         .sampler_to_descriptor = stages[s].sampler_to_descriptor
      };

      stages[s].nir = anv_pipeline_stage_get_nir(&pipeline->base, cache,
                                                 pipeline_ctx,
                                                 &stages[s]);
      if (stages[s].nir == NULL) {
         return vk_error(pipeline, VK_ERROR_UNKNOWN);
      }

      nir_shader_gather_info(stages[s].nir, nir_shader_get_entrypoint(stages[s].nir));

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
   }

   return VK_SUCCESS;
}

static void
anv_fixup_subgroup_size(struct anv_device *device, struct shader_info *info)
{
   switch (info->stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_TASK:
   case MESA_SHADER_MESH:
      break;
   default:
      return;
   }

   unsigned local_size = info->workgroup_size[0] *
                         info->workgroup_size[1] *
                         info->workgroup_size[2];

   /* Games don't always request full subgroups when they should,
    * which can cause bugs, as they may expect bigger size of the
    * subgroup than we choose for the execution.
    */
   if (device->physical->instance->assume_full_subgroups &&
       info->uses_wide_subgroup_intrinsics &&
       info->subgroup_size == SUBGROUP_SIZE_API_CONSTANT &&
       local_size &&
       local_size % BRW_SUBGROUP_SIZE == 0)
      info->subgroup_size = SUBGROUP_SIZE_FULL_SUBGROUPS;

   /* If the client requests that we dispatch full subgroups but doesn't
    * allow us to pick a subgroup size, we have to smash it to the API
    * value of 32.  Performance will likely be terrible in this case but
    * there's nothing we can do about that.  The client should have chosen
    * a size.
    */
   if (info->subgroup_size == SUBGROUP_SIZE_FULL_SUBGROUPS)
      info->subgroup_size = BRW_SUBGROUP_SIZE;
}

static VkResult
anv_graphics_pipeline_compile(struct anv_graphics_pipeline *pipeline,
                              struct vk_pipeline_cache *cache,
                              const VkGraphicsPipelineCreateInfo *info,
                              const struct vk_graphics_pipeline_state *state)
{
   ANV_FROM_HANDLE(anv_pipeline_layout, layout, info->layout);
   VkResult result;

   VkPipelineCreationFeedbackEXT pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   int64_t pipeline_start = os_time_get_nano();

   struct anv_device *device = pipeline->base.device;
   const struct intel_device_info *devinfo = device->info;
   const struct brw_compiler *compiler = device->physical->compiler;

   struct anv_pipeline_stage stages[ANV_GRAPHICS_SHADER_STAGE_COUNT] = {};
   for (uint32_t i = 0; i < info->stageCount; i++) {
      gl_shader_stage stage = vk_to_mesa_shader_stage(info->pStages[i].stage);
      stages[stage].stage = stage;
      stages[stage].info = &info->pStages[i];
   }

   anv_graphics_pipeline_init_keys(pipeline, state, stages);

   unsigned char sha1[20];
   anv_pipeline_hash_graphics(pipeline, layout, stages, sha1);

   for (unsigned s = 0; s < ARRAY_SIZE(stages); s++) {
      if (!stages[s].info)
         continue;

      stages[s].cache_key.stage = s;
      memcpy(stages[s].cache_key.sha1, sha1, sizeof(sha1));
   }

   const bool skip_cache_lookup =
      (pipeline->base.flags & VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR);
   if (!skip_cache_lookup) {
      bool found_all_shaders =
         anv_graphics_pipeline_load_cached_shaders(pipeline, cache, stages,
                                                   &pipeline_feedback);
      if (found_all_shaders)
         goto done;
   }

   if (info->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
      return VK_PIPELINE_COMPILE_REQUIRED;

   void *pipeline_ctx = ralloc_context(NULL);

   result = anv_graphics_pipeline_load_nir(pipeline, cache, stages,
                                           pipeline_ctx);
   if (result != VK_SUCCESS)
      goto fail;

   if (stages[MESA_SHADER_MESH].info && stages[MESA_SHADER_FRAGMENT].info) {
      anv_apply_per_prim_attr_wa(stages[MESA_SHADER_MESH].nir,
                                 stages[MESA_SHADER_FRAGMENT].nir,
                                 device,
                                 info);
   }

   /* Walk backwards to link */
   struct anv_pipeline_stage *next_stage = NULL;
   for (int i = ARRAY_SIZE(graphics_shader_order) - 1; i >= 0; i--) {
      gl_shader_stage s = graphics_shader_order[i];
      struct anv_pipeline_stage *stage = &stages[s];

      if (!stage->info)
         continue;

      switch (s) {
      case MESA_SHADER_VERTEX:
         anv_pipeline_link_vs(compiler, stage, next_stage);
         break;
      case MESA_SHADER_TESS_CTRL:
         anv_pipeline_link_tcs(compiler, stage, next_stage);
         break;
      case MESA_SHADER_TESS_EVAL:
         anv_pipeline_link_tes(compiler, stage, next_stage);
         break;
      case MESA_SHADER_GEOMETRY:
         anv_pipeline_link_gs(compiler, stage, next_stage);
         break;
      case MESA_SHADER_TASK:
         anv_pipeline_link_task(compiler, stage, next_stage);
         break;
      case MESA_SHADER_MESH:
         anv_pipeline_link_mesh(compiler, stage, next_stage);
         break;
      case MESA_SHADER_FRAGMENT:
         anv_pipeline_link_fs(compiler, stage, state->rp);
         break;
      default:
         unreachable("Invalid graphics shader stage");
      }

      next_stage = stage;
   }

   bool use_primitive_replication = false;
   if (devinfo->ver >= 12 && pipeline->view_mask != 0) {
      /* For some pipelines HW Primitive Replication can be used instead of
       * instancing to implement Multiview.  This depend on how viewIndex is
       * used in all the active shaders, so this check can't be done per
       * individual shaders.
       */
      nir_shader *shaders[ANV_GRAPHICS_SHADER_STAGE_COUNT] = {};
      for (unsigned s = 0; s < ARRAY_SIZE(shaders); s++)
         shaders[s] = stages[s].nir;

      use_primitive_replication =
         anv_check_for_primitive_replication(device, pipeline->active_stages,
                                             shaders, pipeline->view_mask);
   }

   struct anv_pipeline_stage *prev_stage = NULL;
   for (unsigned i = 0; i < ARRAY_SIZE(graphics_shader_order); i++) {
      gl_shader_stage s = graphics_shader_order[i];
      struct anv_pipeline_stage *stage = &stages[s];

      if (!stage->info)
         continue;

      int64_t stage_start = os_time_get_nano();

      void *stage_ctx = ralloc_context(NULL);

      anv_pipeline_lower_nir(&pipeline->base, stage_ctx, stage, layout,
                             use_primitive_replication);

      struct shader_info *cur_info = &stage->nir->info;

      if (prev_stage && compiler->nir_options[s]->unify_interfaces) {
         struct shader_info *prev_info = &prev_stage->nir->info;

         prev_info->outputs_written |= cur_info->inputs_read &
                  ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);
         cur_info->inputs_read |= prev_info->outputs_written &
                  ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);
         prev_info->patch_outputs_written |= cur_info->patch_inputs_read;
         cur_info->patch_inputs_read |= prev_info->patch_outputs_written;
      }

      ralloc_free(stage_ctx);

      anv_fixup_subgroup_size(device, cur_info);

      stage->feedback.duration += os_time_get_nano() - stage_start;

      prev_stage = stage;
   }

   /* In the case the platform can write the primitive variable shading rate,
    * figure out the last geometry stage that should write the primitive
    * shading rate, and ensure it is marked as used there. The backend will
    * write a default value if the shader doesn't actually write it.
    *
    * We iterate backwards in the stage and stop on the first shader that can
    * set the value.
    */
   if (devinfo->has_coarse_pixel_primitive_and_cb &&
       stages[MESA_SHADER_FRAGMENT].info &&
       stages[MESA_SHADER_FRAGMENT].key.wm.coarse_pixel &&
       !stages[MESA_SHADER_FRAGMENT].nir->info.fs.uses_sample_shading &&
       stages[MESA_SHADER_MESH].info == NULL) {
      struct anv_pipeline_stage *last_psr = NULL;

      for (unsigned i = 0; i < ARRAY_SIZE(graphics_shader_order); i++) {
         gl_shader_stage s =
            graphics_shader_order[ARRAY_SIZE(graphics_shader_order) - i - 1];

         if (!stages[s].info ||
             !gl_shader_stage_can_set_fragment_shading_rate(s))
            continue;

         last_psr = &stages[s];
         break;
      }

      assert(last_psr);
      last_psr->nir->info.outputs_written |= VARYING_BIT_PRIMITIVE_SHADING_RATE;
   }

   prev_stage = NULL;
   for (unsigned i = 0; i < ARRAY_SIZE(graphics_shader_order); i++) {
      gl_shader_stage s = graphics_shader_order[i];
      struct anv_pipeline_stage *stage = &stages[s];

      if (!stage->info)
         continue;

      int64_t stage_start = os_time_get_nano();

      void *stage_ctx = ralloc_context(NULL);

      switch (s) {
      case MESA_SHADER_VERTEX:
         anv_pipeline_compile_vs(compiler, stage_ctx, pipeline,
                                 stage);
         break;
      case MESA_SHADER_TESS_CTRL:
         anv_pipeline_compile_tcs(compiler, stage_ctx, device,
                                  stage, prev_stage);
         break;
      case MESA_SHADER_TESS_EVAL:
         anv_pipeline_compile_tes(compiler, stage_ctx, device,
                                  stage, prev_stage);
         break;
      case MESA_SHADER_GEOMETRY:
         anv_pipeline_compile_gs(compiler, stage_ctx, device,
                                 stage, prev_stage);
         break;
      case MESA_SHADER_TASK:
         anv_pipeline_compile_task(compiler, stage_ctx, device,
                                   stage);
         break;
      case MESA_SHADER_MESH:
         anv_pipeline_compile_mesh(compiler, stage_ctx, device,
                                   stage, prev_stage);
         break;
      case MESA_SHADER_FRAGMENT:
         anv_pipeline_compile_fs(compiler, stage_ctx, device,
                                 stage, prev_stage);
         break;
      default:
         unreachable("Invalid graphics shader stage");
      }
      if (stage->code == NULL) {
         ralloc_free(stage_ctx);
         result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto fail;
      }

      anv_nir_validate_push_layout(&stage->prog_data.base,
                                   &stage->bind_map);

      struct anv_shader_bin *bin =
         anv_device_upload_kernel(device, cache, s,
                                  &stage->cache_key,
                                  sizeof(stage->cache_key),
                                  stage->code,
                                  stage->prog_data.base.program_size,
                                  &stage->prog_data.base,
                                  brw_prog_data_size(s),
                                  stage->stats, stage->num_stats,
                                  stage->nir->xfb_info,
                                  &stage->bind_map,
                                  &stage->push_desc_info);
      if (!bin) {
         ralloc_free(stage_ctx);
         result = vk_error(pipeline, VK_ERROR_OUT_OF_HOST_MEMORY);
         goto fail;
      }

      anv_pipeline_add_executables(&pipeline->base, stage, bin);

      pipeline->shaders[s] = bin;
      ralloc_free(stage_ctx);

      stage->feedback.duration += os_time_get_nano() - stage_start;

      prev_stage = stage;
   }

   ralloc_free(pipeline_ctx);

done:

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   const VkPipelineCreationFeedbackCreateInfo *create_feedback =
      vk_find_struct_const(info->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (create_feedback) {
      *create_feedback->pPipelineCreationFeedback = pipeline_feedback;

      uint32_t stage_count = create_feedback->pipelineStageCreationFeedbackCount;
      assert(stage_count == 0 || info->stageCount == stage_count);
      for (uint32_t i = 0; i < stage_count; i++) {
         gl_shader_stage s = vk_to_mesa_shader_stage(info->pStages[i].stage);
         create_feedback->pPipelineStageCreationFeedbacks[i] = stages[s].feedback;
      }
   }

   return VK_SUCCESS;

fail:
   ralloc_free(pipeline_ctx);

   for (unsigned s = 0; s < ARRAY_SIZE(pipeline->shaders); s++) {
      if (pipeline->shaders[s])
         anv_shader_bin_unref(device, pipeline->shaders[s]);
   }

   return result;
}

static VkResult
anv_pipeline_compile_cs(struct anv_compute_pipeline *pipeline,
                        struct vk_pipeline_cache *cache,
                        const VkComputePipelineCreateInfo *info)
{
   ASSERTED const VkPipelineShaderStageCreateInfo *sinfo = &info->stage;
   assert(sinfo->stage == VK_SHADER_STAGE_COMPUTE_BIT);

   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   int64_t pipeline_start = os_time_get_nano();

   struct anv_device *device = pipeline->base.device;
   const struct brw_compiler *compiler = device->physical->compiler;

   struct anv_pipeline_stage stage = {
      .stage = MESA_SHADER_COMPUTE,
      .info = &info->stage,
      .cache_key = {
         .stage = MESA_SHADER_COMPUTE,
      },
      .feedback = {
         .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
      },
   };
   vk_pipeline_hash_shader_stage(&info->stage, NULL, stage.shader_sha1);

   struct anv_shader_bin *bin = NULL;

   populate_cs_prog_key(device, device->robust_buffer_access, &stage.key.cs);

   ANV_FROM_HANDLE(anv_pipeline_layout, layout, info->layout);

   const bool skip_cache_lookup =
      (pipeline->base.flags & VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR);

   anv_pipeline_hash_compute(pipeline, layout, &stage, stage.cache_key.sha1);

   bool cache_hit = false;
   if (!skip_cache_lookup) {
      bin = anv_device_search_for_kernel(device, cache,
                                         &stage.cache_key,
                                         sizeof(stage.cache_key),
                                         &cache_hit);
   }

   if (bin == NULL &&
       (info->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT))
      return VK_PIPELINE_COMPILE_REQUIRED;

   void *mem_ctx = ralloc_context(NULL);
   if (bin == NULL) {
      int64_t stage_start = os_time_get_nano();

      stage.bind_map = (struct anv_pipeline_bind_map) {
         .surface_to_descriptor = stage.surface_to_descriptor,
         .sampler_to_descriptor = stage.sampler_to_descriptor
      };

      /* Set up a binding for the gl_NumWorkGroups */
      stage.bind_map.surface_count = 1;
      stage.bind_map.surface_to_descriptor[0] = (struct anv_pipeline_binding) {
         .set = ANV_DESCRIPTOR_SET_NUM_WORK_GROUPS,
         .binding = UINT32_MAX,
      };

      stage.nir = anv_pipeline_stage_get_nir(&pipeline->base, cache, mem_ctx, &stage);
      if (stage.nir == NULL) {
         ralloc_free(mem_ctx);
         return vk_error(pipeline, VK_ERROR_UNKNOWN);
      }

      anv_pipeline_lower_nir(&pipeline->base, mem_ctx, &stage, layout,
                             false /* use_primitive_replication */);

      anv_fixup_subgroup_size(device, &stage.nir->info);

      stage.num_stats = 1;

      struct brw_compile_cs_params params = {
         .nir = stage.nir,
         .key = &stage.key.cs,
         .prog_data = &stage.prog_data.cs,
         .stats = stage.stats,
         .log_data = device,
      };

      stage.code = brw_compile_cs(compiler, mem_ctx, &params);
      if (stage.code == NULL) {
         ralloc_free(mem_ctx);
         return vk_error(pipeline, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      anv_nir_validate_push_layout(&stage.prog_data.base, &stage.bind_map);

      if (!stage.prog_data.cs.uses_num_work_groups) {
         assert(stage.bind_map.surface_to_descriptor[0].set ==
                ANV_DESCRIPTOR_SET_NUM_WORK_GROUPS);
         stage.bind_map.surface_to_descriptor[0].set = ANV_DESCRIPTOR_SET_NULL;
      }

      const unsigned code_size = stage.prog_data.base.program_size;
      bin = anv_device_upload_kernel(device, cache,
                                     MESA_SHADER_COMPUTE,
                                     &stage.cache_key, sizeof(stage.cache_key),
                                     stage.code, code_size,
                                     &stage.prog_data.base,
                                     sizeof(stage.prog_data.cs),
                                     stage.stats, stage.num_stats,
                                     NULL, &stage.bind_map,
                                     &stage.push_desc_info);
      if (!bin) {
         ralloc_free(mem_ctx);
         return vk_error(pipeline, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      stage.feedback.duration = os_time_get_nano() - stage_start;
   }

   anv_pipeline_add_executables(&pipeline->base, &stage, bin);

   ralloc_free(mem_ctx);

   if (cache_hit) {
      stage.feedback.flags |=
         VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      pipeline_feedback.flags |=
         VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
   }
   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   const VkPipelineCreationFeedbackCreateInfo *create_feedback =
      vk_find_struct_const(info->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (create_feedback) {
      *create_feedback->pPipelineCreationFeedback = pipeline_feedback;

      if (create_feedback->pipelineStageCreationFeedbackCount) {
         assert(create_feedback->pipelineStageCreationFeedbackCount == 1);
         create_feedback->pPipelineStageCreationFeedbacks[0] = stage.feedback;
      }
   }

   pipeline->cs = bin;

   return VK_SUCCESS;
}

static VkResult
anv_compute_pipeline_create(struct anv_device *device,
                            struct vk_pipeline_cache *cache,
                            const VkComputePipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipeline)
{
   struct anv_compute_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_pipeline_init(&pipeline->base, device,
                              ANV_PIPELINE_COMPUTE, pCreateInfo->flags,
                              pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   anv_batch_set_storage(&pipeline->base.batch, ANV_NULL_ADDRESS,
                         pipeline->batch_data, sizeof(pipeline->batch_data));

   result = anv_pipeline_compile_cs(pipeline, cache, pCreateInfo);
   if (result != VK_SUCCESS) {
      anv_pipeline_finish(&pipeline->base, device, pAllocator);
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   anv_genX(device->info, compute_pipeline_emit)(pipeline);

   *pPipeline = anv_pipeline_to_handle(&pipeline->base);

   return pipeline->base.batch.status;
}

VkResult anv_CreateComputePipelines(
    VkDevice                                    _device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    count,
    const VkComputePipelineCreateInfo*          pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(vk_pipeline_cache, pipeline_cache, pipelineCache);

   VkResult result = VK_SUCCESS;

   unsigned i;
   for (i = 0; i < count; i++) {
      VkResult res = anv_compute_pipeline_create(device, pipeline_cache,
                                                 &pCreateInfos[i],
                                                 pAllocator, &pPipelines[i]);

      if (res == VK_SUCCESS)
         continue;

      /* Bail out on the first error != VK_PIPELINE_COMPILE_REQUIRED as it
       * is not obvious what error should be report upon 2 different failures.
       * */
      result = res;
      if (res != VK_PIPELINE_COMPILE_REQUIRED)
         break;

      pPipelines[i] = VK_NULL_HANDLE;

      if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
         break;
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

/**
 * Calculate the desired L3 partitioning based on the current state of the
 * pipeline.  For now this simply returns the conservative defaults calculated
 * by get_default_l3_weights(), but we could probably do better by gathering
 * more statistics from the pipeline state (e.g. guess of expected URB usage
 * and bound surfaces), or by using feed-back from performance counters.
 */
void
anv_pipeline_setup_l3_config(struct anv_pipeline *pipeline, bool needs_slm)
{
   const struct intel_device_info *devinfo = pipeline->device->info;

   const struct intel_l3_weights w =
      intel_get_default_l3_weights(devinfo, true, needs_slm);

   pipeline->l3_config = intel_get_l3_config(devinfo, w);
}

static uint32_t
get_vs_input_elements(const struct brw_vs_prog_data *vs_prog_data)
{
   /* Pull inputs_read out of the VS prog data */
   const uint64_t inputs_read = vs_prog_data->inputs_read;
   const uint64_t double_inputs_read =
      vs_prog_data->double_inputs_read & inputs_read;
   assert((inputs_read & ((1 << VERT_ATTRIB_GENERIC0) - 1)) == 0);
   const uint32_t elements = inputs_read >> VERT_ATTRIB_GENERIC0;
   const uint32_t elements_double = double_inputs_read >> VERT_ATTRIB_GENERIC0;

   return __builtin_popcount(elements) -
          __builtin_popcount(elements_double) / 2;
}

static VkResult
anv_graphics_pipeline_init(struct anv_graphics_pipeline *pipeline,
                           struct anv_device *device,
                           struct vk_pipeline_cache *cache,
                           const struct VkGraphicsPipelineCreateInfo *pCreateInfo,
                           const struct vk_graphics_pipeline_state *state,
                           const VkAllocationCallbacks *alloc)
{
   VkResult result;

   result = anv_pipeline_init(&pipeline->base, device,
                              ANV_PIPELINE_GRAPHICS, pCreateInfo->flags,
                              alloc);
   if (result != VK_SUCCESS)
      return result;

   anv_batch_set_storage(&pipeline->base.batch, ANV_NULL_ADDRESS,
                         pipeline->batch_data, sizeof(pipeline->batch_data));

   pipeline->active_stages = 0;
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++)
      pipeline->active_stages |= pCreateInfo->pStages[i].stage;

   if (pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
      pipeline->active_stages |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;

   if (anv_pipeline_is_mesh(pipeline))
      assert(device->physical->vk.supported_extensions.NV_mesh_shader ||
             device->physical->vk.supported_extensions.EXT_mesh_shader);

   pipeline->dynamic_state.vi = &pipeline->vertex_input;
   pipeline->dynamic_state.ms.sample_locations = &pipeline->sample_locations;
   vk_dynamic_graphics_state_fill(&pipeline->dynamic_state, state);

   pipeline->view_mask = state->rp->view_mask;

   result = anv_graphics_pipeline_compile(pipeline, cache, pCreateInfo, state);
   if (result != VK_SUCCESS) {
      anv_pipeline_finish(&pipeline->base, device, alloc);
      return result;
   }

   anv_pipeline_setup_l3_config(&pipeline->base, false);

   if (anv_pipeline_is_primitive(pipeline)) {
      const struct brw_vs_prog_data *vs_prog_data = get_vs_prog_data(pipeline);

      /* The total number of vertex elements we need to program. We might need
       * a couple more to implement some of the draw parameters.
       */
      pipeline->svgs_count =
         (vs_prog_data->uses_vertexid ||
          vs_prog_data->uses_instanceid ||
          vs_prog_data->uses_firstvertex ||
          vs_prog_data->uses_baseinstance) + vs_prog_data->uses_drawid;

      pipeline->vs_input_elements = get_vs_input_elements(vs_prog_data);

      pipeline->vertex_input_elems =
         (BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_VI) ?
          0 : pipeline->vs_input_elements) + pipeline->svgs_count;

      /* Our implementation of VK_KHR_multiview uses instancing to draw the
       * different views when primitive replication cannot be used.  If the
       * client asks for instancing, we need to multiply by the client's
       * instance count at draw time and instance divisor in the vertex
       * bindings by the number of views ensure that we repeat the client's
       * per-instance data once for each view.
       */
      const bool uses_primitive_replication =
         anv_pipeline_get_last_vue_prog_data(pipeline)->vue_map.num_pos_slots > 1;
      pipeline->instance_multiplier = 1;
      if (pipeline->view_mask && !uses_primitive_replication)
         pipeline->instance_multiplier = util_bitcount(pipeline->view_mask);
   } else {
      assert(anv_pipeline_is_mesh(pipeline));
      /* TODO(mesh): Mesh vs. Multiview with Instancing. */
   }

   /* Store line mode and rasterization samples, these are used
    * for dynamic primitive topology.
    */
   pipeline->rasterization_samples =
      state->ms != NULL ? state->ms->rasterization_samples : 1;
   pipeline->patch_control_points =
      state->ts != NULL ? state->ts->patch_control_points : 0;

   return VK_SUCCESS;
}

static VkResult
anv_graphics_pipeline_create(struct anv_device *device,
                             struct vk_pipeline_cache *cache,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkPipeline *pPipeline)
{
   struct anv_graphics_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_graphics_pipeline_all_state all;
   struct vk_graphics_pipeline_state state = { };
   result = vk_graphics_pipeline_state_fill(&device->vk, &state, pCreateInfo,
                                            NULL /* sp_info */,
                                            &all, NULL, 0, NULL);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   result = anv_graphics_pipeline_init(pipeline, device, cache,
                                       pCreateInfo, &state, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   anv_genX(device->info, graphics_pipeline_emit)(pipeline, &state);

   *pPipeline = anv_pipeline_to_handle(&pipeline->base);

   return pipeline->base.batch.status;
}

VkResult anv_CreateGraphicsPipelines(
    VkDevice                                    _device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    count,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(vk_pipeline_cache, pipeline_cache, pipelineCache);

   VkResult result = VK_SUCCESS;

   unsigned i;
   for (i = 0; i < count; i++) {
      VkResult res = anv_graphics_pipeline_create(device,
                                                  pipeline_cache,
                                                  &pCreateInfos[i],
                                                  pAllocator, &pPipelines[i]);

      if (res == VK_SUCCESS)
         continue;

      /* Bail out on the first error != VK_PIPELINE_COMPILE_REQUIRED as it
       * is not obvious what error should be report upon 2 different failures.
       * */
      result = res;
      if (res != VK_PIPELINE_COMPILE_REQUIRED)
         break;

      pPipelines[i] = VK_NULL_HANDLE;

      if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
         break;
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

static VkResult
compile_upload_rt_shader(struct anv_ray_tracing_pipeline *pipeline,
                         struct vk_pipeline_cache *cache,
                         nir_shader *nir,
                         struct anv_pipeline_stage *stage,
                         struct anv_shader_bin **shader_out,
                         void *mem_ctx)
{
   const struct brw_compiler *compiler =
      pipeline->base.device->physical->compiler;
   const struct intel_device_info *devinfo = compiler->devinfo;

   nir_shader **resume_shaders = NULL;
   uint32_t num_resume_shaders = 0;
   if (nir->info.stage != MESA_SHADER_COMPUTE) {
      const nir_lower_shader_calls_options opts = {
         .address_format = nir_address_format_64bit_global,
         .stack_alignment = BRW_BTD_STACK_ALIGN,
         .localized_loads = true,
         .vectorizer_callback = brw_nir_should_vectorize_mem,
         .vectorizer_data = NULL,
      };

      NIR_PASS(_, nir, nir_lower_shader_calls, &opts,
               &resume_shaders, &num_resume_shaders, mem_ctx);
      NIR_PASS(_, nir, brw_nir_lower_shader_calls, &stage->key.bs);
      NIR_PASS_V(nir, brw_nir_lower_rt_intrinsics, devinfo);
   }

   for (unsigned i = 0; i < num_resume_shaders; i++) {
      NIR_PASS(_,resume_shaders[i], brw_nir_lower_shader_calls, &stage->key.bs);
      NIR_PASS_V(resume_shaders[i], brw_nir_lower_rt_intrinsics, devinfo);
   }

   struct brw_compile_bs_params params = {
      .nir = nir,
      .key = &stage->key.bs,
      .prog_data = &stage->prog_data.bs,
      .num_resume_shaders = num_resume_shaders,
      .resume_shaders = resume_shaders,

      .stats = stage->stats,
      .log_data = pipeline->base.device,
   };

   stage->code = brw_compile_bs(compiler, mem_ctx, &params);
   if (stage->code == NULL)
      return vk_error(pipeline, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Ray-tracing shaders don't have a "real" bind map */
   struct anv_pipeline_bind_map empty_bind_map = {};

   const unsigned code_size = stage->prog_data.base.program_size;
   struct anv_shader_bin *bin =
      anv_device_upload_kernel(pipeline->base.device,
                               cache,
                               stage->stage,
                               &stage->cache_key, sizeof(stage->cache_key),
                               stage->code, code_size,
                               &stage->prog_data.base,
                               sizeof(stage->prog_data.bs),
                               stage->stats, 1,
                               NULL, &empty_bind_map,
                               &stage->push_desc_info);
   if (bin == NULL)
      return vk_error(pipeline, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* TODO: Figure out executables for resume shaders */
   anv_pipeline_add_executables(&pipeline->base, stage, bin);
   util_dynarray_append(&pipeline->shaders, struct anv_shader_bin *, bin);

   pipeline->scratch_size =
      MAX2(pipeline->scratch_size, bin->prog_data->total_scratch);

   *shader_out = bin;

   return VK_SUCCESS;
}

static bool
is_rt_stack_size_dynamic(const VkRayTracingPipelineCreateInfoKHR *info)
{
   if (info->pDynamicState == NULL)
      return false;

   for (unsigned i = 0; i < info->pDynamicState->dynamicStateCount; i++) {
      if (info->pDynamicState->pDynamicStates[i] ==
          VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR)
         return true;
   }

   return false;
}

static void
anv_pipeline_compute_ray_tracing_stacks(struct anv_ray_tracing_pipeline *pipeline,
                                        const VkRayTracingPipelineCreateInfoKHR *info,
                                        uint32_t *stack_max)
{
   if (is_rt_stack_size_dynamic(info)) {
      pipeline->stack_size = 0; /* 0 means dynamic */
   } else {
      /* From the Vulkan spec:
       *
       *    "If the stack size is not set explicitly, the stack size for a
       *    pipeline is:
       *
       *       rayGenStackMax +
       *       min(1, maxPipelineRayRecursionDepth) ×
       *       max(closestHitStackMax, missStackMax,
       *           intersectionStackMax + anyHitStackMax) +
       *       max(0, maxPipelineRayRecursionDepth-1) ×
       *       max(closestHitStackMax, missStackMax) +
       *       2 × callableStackMax"
       */
      pipeline->stack_size =
         stack_max[MESA_SHADER_RAYGEN] +
         MIN2(1, info->maxPipelineRayRecursionDepth) *
         MAX4(stack_max[MESA_SHADER_CLOSEST_HIT],
              stack_max[MESA_SHADER_MISS],
              stack_max[MESA_SHADER_INTERSECTION],
              stack_max[MESA_SHADER_ANY_HIT]) +
         MAX2(0, (int)info->maxPipelineRayRecursionDepth - 1) *
         MAX2(stack_max[MESA_SHADER_CLOSEST_HIT],
              stack_max[MESA_SHADER_MISS]) +
         2 * stack_max[MESA_SHADER_CALLABLE];

      /* This is an extremely unlikely case but we need to set it to some
       * non-zero value so that we don't accidentally think it's dynamic.
       * Our minimum stack size is 2KB anyway so we could set to any small
       * value we like.
       */
      if (pipeline->stack_size == 0)
         pipeline->stack_size = 1;
   }
}

static enum brw_rt_ray_flags
anv_pipeline_get_pipeline_ray_flags(VkPipelineCreateFlags flags)
{
   uint32_t ray_flags = 0;

   const bool rt_skip_triangles =
      flags & VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR;
   const bool rt_skip_aabbs =
      flags & VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR;
   assert(!(rt_skip_triangles && rt_skip_aabbs));

   if (rt_skip_triangles)
      ray_flags |= BRW_RT_RAY_FLAG_SKIP_TRIANGLES;
   else if (rt_skip_aabbs)
      ray_flags |= BRW_RT_RAY_FLAG_SKIP_AABBS;

   return ray_flags;
}

static struct anv_pipeline_stage *
anv_pipeline_init_ray_tracing_stages(struct anv_ray_tracing_pipeline *pipeline,
                                     const VkRayTracingPipelineCreateInfoKHR *info,
                                     void *pipeline_ctx)
{
   ANV_FROM_HANDLE(anv_pipeline_layout, layout, info->layout);

   /* Create enough stage entries for all shader modules plus potential
    * combinaisons in the groups.
    */
   struct anv_pipeline_stage *stages =
      rzalloc_array(pipeline_ctx, struct anv_pipeline_stage, info->stageCount);

   enum brw_rt_ray_flags ray_flags =
      anv_pipeline_get_pipeline_ray_flags(pipeline->base.flags);

   for (uint32_t i = 0; i < info->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &info->pStages[i];
      if (vk_pipeline_shader_stage_is_null(sinfo))
         continue;

      int64_t stage_start = os_time_get_nano();

      stages[i] = (struct anv_pipeline_stage) {
         .stage = vk_to_mesa_shader_stage(sinfo->stage),
         .info = sinfo,
         .cache_key = {
            .stage = vk_to_mesa_shader_stage(sinfo->stage),
         },
         .feedback = {
            .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
         },
      };

      populate_bs_prog_key(pipeline->base.device,
                           pipeline->base.device->robust_buffer_access,
                           ray_flags,
                           &stages[i].key.bs);

      vk_pipeline_hash_shader_stage(sinfo, NULL, stages[i].shader_sha1);

      if (stages[i].stage != MESA_SHADER_INTERSECTION) {
         anv_pipeline_hash_ray_tracing_shader(pipeline, layout, &stages[i],
                                              stages[i].cache_key.sha1);
      }

      stages[i].feedback.duration += os_time_get_nano() - stage_start;
   }

   for (uint32_t i = 0; i < info->groupCount; i++) {
      const VkRayTracingShaderGroupCreateInfoKHR *ginfo = &info->pGroups[i];

      if (ginfo->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR)
         continue;

      int64_t stage_start = os_time_get_nano();

      uint32_t intersection_idx = ginfo->intersectionShader;
      assert(intersection_idx < info->stageCount);

      uint32_t any_hit_idx = ginfo->anyHitShader;
      if (any_hit_idx != VK_SHADER_UNUSED_KHR) {
         assert(any_hit_idx < info->stageCount);
         anv_pipeline_hash_ray_tracing_combined_shader(pipeline,
                                                       layout,
                                                       &stages[intersection_idx],
                                                       &stages[any_hit_idx],
                                                       stages[intersection_idx].cache_key.sha1);
      } else {
         anv_pipeline_hash_ray_tracing_shader(pipeline, layout,
                                              &stages[intersection_idx],
                                              stages[intersection_idx].cache_key.sha1);
      }

      stages[intersection_idx].feedback.duration += os_time_get_nano() - stage_start;
   }

   return stages;
}

static bool
anv_ray_tracing_pipeline_load_cached_shaders(struct anv_ray_tracing_pipeline *pipeline,
                                             struct vk_pipeline_cache *cache,
                                             const VkRayTracingPipelineCreateInfoKHR *info,
                                             struct anv_pipeline_stage *stages,
                                             uint32_t *stack_max)
{
   uint32_t shaders = 0, cache_hits = 0;
   for (uint32_t i = 0; i < info->stageCount; i++) {
      if (stages[i].info == NULL)
         continue;

      shaders++;

      int64_t stage_start = os_time_get_nano();

      bool cache_hit;
      stages[i].bin = anv_device_search_for_kernel(pipeline->base.device, cache,
                                                   &stages[i].cache_key,
                                                   sizeof(stages[i].cache_key),
                                                   &cache_hit);
      if (cache_hit) {
         cache_hits++;
         stages[i].feedback.flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      }

      if (stages[i].bin != NULL) {
         anv_pipeline_add_executables(&pipeline->base, &stages[i], stages[i].bin);
         util_dynarray_append(&pipeline->shaders, struct anv_shader_bin *, stages[i].bin);

         uint32_t stack_size =
            brw_bs_prog_data_const(stages[i].bin->prog_data)->max_stack_size;
         stack_max[stages[i].stage] =
            MAX2(stack_max[stages[i].stage], stack_size);
      }

      stages[i].feedback.duration += os_time_get_nano() - stage_start;
   }

   return cache_hits == shaders;
}

static VkResult
anv_pipeline_compile_ray_tracing(struct anv_ray_tracing_pipeline *pipeline,
                                 struct vk_pipeline_cache *cache,
                                 const VkRayTracingPipelineCreateInfoKHR *info)
{
   const struct intel_device_info *devinfo = pipeline->base.device->info;
   VkResult result;

   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   int64_t pipeline_start = os_time_get_nano();

   void *pipeline_ctx = ralloc_context(NULL);

   struct anv_pipeline_stage *stages =
      anv_pipeline_init_ray_tracing_stages(pipeline, info, pipeline_ctx);

   ANV_FROM_HANDLE(anv_pipeline_layout, layout, info->layout);

   const bool skip_cache_lookup =
      (pipeline->base.flags & VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR);

   uint32_t stack_max[MESA_VULKAN_SHADER_STAGES] = {};

   if (!skip_cache_lookup &&
       anv_ray_tracing_pipeline_load_cached_shaders(pipeline, cache, info,
                                                    stages, stack_max)) {
      pipeline_feedback.flags |=
         VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      goto done;
   }

   if (info->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
      ralloc_free(pipeline_ctx);
      return VK_PIPELINE_COMPILE_REQUIRED;
   }

   for (uint32_t i = 0; i < info->stageCount; i++) {
      if (stages[i].info == NULL)
         continue;

      int64_t stage_start = os_time_get_nano();

      stages[i].nir = anv_pipeline_stage_get_nir(&pipeline->base, cache,
                                                 pipeline_ctx, &stages[i]);
      if (stages[i].nir == NULL) {
         ralloc_free(pipeline_ctx);
         return vk_error(pipeline, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      anv_pipeline_lower_nir(&pipeline->base, pipeline_ctx, &stages[i],
                             layout, false /* use_primitive_replication */);

      stages[i].feedback.duration += os_time_get_nano() - stage_start;
   }

   for (uint32_t i = 0; i < info->stageCount; i++) {
      if (stages[i].info == NULL)
         continue;

      /* Shader found in cache already. */
      if (stages[i].bin != NULL)
         continue;

      /* We handle intersection shaders as part of the group */
      if (stages[i].stage == MESA_SHADER_INTERSECTION)
         continue;

      int64_t stage_start = os_time_get_nano();

      void *stage_ctx = ralloc_context(pipeline_ctx);

      nir_shader *nir = nir_shader_clone(stage_ctx, stages[i].nir);
      switch (stages[i].stage) {
      case MESA_SHADER_RAYGEN:
         brw_nir_lower_raygen(nir);
         break;

      case MESA_SHADER_ANY_HIT:
         brw_nir_lower_any_hit(nir, devinfo);
         break;

      case MESA_SHADER_CLOSEST_HIT:
         brw_nir_lower_closest_hit(nir);
         break;

      case MESA_SHADER_MISS:
         brw_nir_lower_miss(nir);
         break;

      case MESA_SHADER_INTERSECTION:
         unreachable("These are handled later");

      case MESA_SHADER_CALLABLE:
         brw_nir_lower_callable(nir);
         break;

      default:
         unreachable("Invalid ray-tracing shader stage");
      }

      result = compile_upload_rt_shader(pipeline, cache, nir, &stages[i],
                                        &stages[i].bin, stage_ctx);
      if (result != VK_SUCCESS) {
         ralloc_free(pipeline_ctx);
         return result;
      }

      uint32_t stack_size =
         brw_bs_prog_data_const(stages[i].bin->prog_data)->max_stack_size;
      stack_max[stages[i].stage] = MAX2(stack_max[stages[i].stage], stack_size);

      ralloc_free(stage_ctx);

      stages[i].feedback.duration += os_time_get_nano() - stage_start;
   }

 done:
   for (uint32_t i = 0; i < info->groupCount; i++) {
      const VkRayTracingShaderGroupCreateInfoKHR *ginfo = &info->pGroups[i];
      struct anv_rt_shader_group *group = &pipeline->groups[i];
      group->type = ginfo->type;
      switch (ginfo->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         assert(ginfo->generalShader < info->stageCount);
         group->general = stages[ginfo->generalShader].bin;
         break;

      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         if (ginfo->anyHitShader < info->stageCount)
            group->any_hit = stages[ginfo->anyHitShader].bin;

         if (ginfo->closestHitShader < info->stageCount)
            group->closest_hit = stages[ginfo->closestHitShader].bin;
         break;

      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR: {
         if (ginfo->closestHitShader < info->stageCount)
            group->closest_hit = stages[ginfo->closestHitShader].bin;

         uint32_t intersection_idx = info->pGroups[i].intersectionShader;
         assert(intersection_idx < info->stageCount);

         /* Only compile this stage if not already found in the cache. */
         if (stages[intersection_idx].bin == NULL) {
            /* The any-hit and intersection shader have to be combined */
            uint32_t any_hit_idx = info->pGroups[i].anyHitShader;
            const nir_shader *any_hit = NULL;
            if (any_hit_idx < info->stageCount)
               any_hit = stages[any_hit_idx].nir;

            void *group_ctx = ralloc_context(pipeline_ctx);
            nir_shader *intersection =
               nir_shader_clone(group_ctx, stages[intersection_idx].nir);

            brw_nir_lower_combined_intersection_any_hit(intersection, any_hit,
                                                        devinfo);

            result = compile_upload_rt_shader(pipeline, cache,
                                              intersection,
                                              &stages[intersection_idx],
                                              &group->intersection,
                                              group_ctx);
            ralloc_free(group_ctx);
            if (result != VK_SUCCESS)
               return result;
         } else {
            group->intersection = stages[intersection_idx].bin;
         }

         uint32_t stack_size =
            brw_bs_prog_data_const(group->intersection->prog_data)->max_stack_size;
         stack_max[MESA_SHADER_INTERSECTION] =
            MAX2(stack_max[MESA_SHADER_INTERSECTION], stack_size);

         break;
      }

      default:
         unreachable("Invalid ray tracing shader group type");
      }
   }

   ralloc_free(pipeline_ctx);

   anv_pipeline_compute_ray_tracing_stacks(pipeline, info, stack_max);

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   const VkPipelineCreationFeedbackCreateInfo *create_feedback =
      vk_find_struct_const(info->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (create_feedback) {
      *create_feedback->pPipelineCreationFeedback = pipeline_feedback;

      uint32_t stage_count = create_feedback->pipelineStageCreationFeedbackCount;
      assert(stage_count == 0 || info->stageCount == stage_count);
      for (uint32_t i = 0; i < stage_count; i++) {
         gl_shader_stage s = vk_to_mesa_shader_stage(info->pStages[i].stage);
         create_feedback->pPipelineStageCreationFeedbacks[i] = stages[s].feedback;
      }
   }

   return VK_SUCCESS;
}

VkResult
anv_device_init_rt_shaders(struct anv_device *device)
{
   device->bvh_build_method = ANV_BVH_BUILD_METHOD_NEW_SAH;

   if (!device->vk.enabled_extensions.KHR_ray_tracing_pipeline)
      return VK_SUCCESS;

   bool cache_hit;

   struct brw_rt_trampoline {
      char name[16];
      struct brw_cs_prog_key key;
   } trampoline_key = {
      .name = "rt-trampoline",
   };
   device->rt_trampoline =
      anv_device_search_for_kernel(device, device->internal_cache,
                                   &trampoline_key, sizeof(trampoline_key),
                                   &cache_hit);
   if (device->rt_trampoline == NULL) {

      void *tmp_ctx = ralloc_context(NULL);
      nir_shader *trampoline_nir =
         brw_nir_create_raygen_trampoline(device->physical->compiler, tmp_ctx);

      trampoline_nir->info.subgroup_size = SUBGROUP_SIZE_REQUIRE_16;

      struct anv_push_descriptor_info push_desc_info = {};
      struct anv_pipeline_bind_map bind_map = {
         .surface_count = 0,
         .sampler_count = 0,
      };
      uint32_t dummy_params[4] = { 0, };
      struct brw_cs_prog_data trampoline_prog_data = {
         .base.nr_params = 4,
         .base.param = dummy_params,
         .uses_inline_data = true,
         .uses_btd_stack_ids = true,
      };
      struct brw_compile_cs_params params = {
         .nir = trampoline_nir,
         .key = &trampoline_key.key,
         .prog_data = &trampoline_prog_data,
         .log_data = device,
      };
      const unsigned *tramp_data =
         brw_compile_cs(device->physical->compiler, tmp_ctx, &params);

      device->rt_trampoline =
         anv_device_upload_kernel(device, device->internal_cache,
                                  MESA_SHADER_COMPUTE,
                                  &trampoline_key, sizeof(trampoline_key),
                                  tramp_data,
                                  trampoline_prog_data.base.program_size,
                                  &trampoline_prog_data.base,
                                  sizeof(trampoline_prog_data),
                                  NULL, 0, NULL, &bind_map,
                                  &push_desc_info);

      ralloc_free(tmp_ctx);

      if (device->rt_trampoline == NULL)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_bin_unref(device, device->rt_trampoline);

   struct brw_rt_trivial_return {
      char name[16];
      struct brw_bs_prog_key key;
   } return_key = {
      .name = "rt-trivial-ret",
   };
   device->rt_trivial_return =
      anv_device_search_for_kernel(device, device->internal_cache,
                                   &return_key, sizeof(return_key),
                                   &cache_hit);
   if (device->rt_trivial_return == NULL) {
      void *tmp_ctx = ralloc_context(NULL);
      nir_shader *trivial_return_nir =
         brw_nir_create_trivial_return_shader(device->physical->compiler, tmp_ctx);

      NIR_PASS_V(trivial_return_nir, brw_nir_lower_rt_intrinsics, device->info);

      struct anv_push_descriptor_info push_desc_info = {};
      struct anv_pipeline_bind_map bind_map = {
         .surface_count = 0,
         .sampler_count = 0,
      };
      struct brw_bs_prog_data return_prog_data = { 0, };
      struct brw_compile_bs_params params = {
         .nir = trivial_return_nir,
         .key = &return_key.key,
         .prog_data = &return_prog_data,

         .log_data = device,
      };
      const unsigned *return_data =
         brw_compile_bs(device->physical->compiler, tmp_ctx, &params);

      device->rt_trivial_return =
         anv_device_upload_kernel(device, device->internal_cache,
                                  MESA_SHADER_CALLABLE,
                                  &return_key, sizeof(return_key),
                                  return_data, return_prog_data.base.program_size,
                                  &return_prog_data.base, sizeof(return_prog_data),
                                  NULL, 0, NULL, &bind_map,
                                  &push_desc_info);

      ralloc_free(tmp_ctx);

      if (device->rt_trivial_return == NULL)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   /* The cache already has a reference and it's not going anywhere so there
    * is no need to hold a second reference.
    */
   anv_shader_bin_unref(device, device->rt_trivial_return);

   return VK_SUCCESS;
}

void
anv_device_finish_rt_shaders(struct anv_device *device)
{
   if (!device->vk.enabled_extensions.KHR_ray_tracing_pipeline)
      return;
}

static VkResult
anv_ray_tracing_pipeline_init(struct anv_ray_tracing_pipeline *pipeline,
                              struct anv_device *device,
                              struct vk_pipeline_cache *cache,
                              const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                              const VkAllocationCallbacks *alloc)
{
   VkResult result;

   util_dynarray_init(&pipeline->shaders, pipeline->base.mem_ctx);

   result = anv_pipeline_compile_ray_tracing(pipeline, cache, pCreateInfo);
   if (result != VK_SUCCESS)
      goto fail;

   anv_pipeline_setup_l3_config(&pipeline->base, /* needs_slm */ false);

   return VK_SUCCESS;

fail:
   util_dynarray_foreach(&pipeline->shaders,
                         struct anv_shader_bin *, shader) {
      anv_shader_bin_unref(device, *shader);
   }
   return result;
}

static void
assert_rt_stage_index_valid(const VkRayTracingPipelineCreateInfoKHR* pCreateInfo,
                            uint32_t stage_idx,
                            VkShaderStageFlags valid_stages)
{
   if (stage_idx == VK_SHADER_UNUSED_KHR)
      return;

   assert(stage_idx <= pCreateInfo->stageCount);
   assert(util_bitcount(pCreateInfo->pStages[stage_idx].stage) == 1);
   assert(pCreateInfo->pStages[stage_idx].stage & valid_stages);
}

static VkResult
anv_ray_tracing_pipeline_create(
    VkDevice                                    _device,
    struct vk_pipeline_cache *                  cache,
    const VkRayTracingPipelineCreateInfoKHR*    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipeline)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);

   uint32_t group_count = pCreateInfo->groupCount;
   if (pCreateInfo->pLibraryInfo) {
      for (uint32_t l = 0; l < pCreateInfo->pLibraryInfo->libraryCount; l++) {
         ANV_FROM_HANDLE(anv_pipeline, library,
                         pCreateInfo->pLibraryInfo->pLibraries[l]);
         struct anv_ray_tracing_pipeline *rt_library =
            anv_pipeline_to_ray_tracing(library);
         group_count += rt_library->group_count;
      }
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct anv_ray_tracing_pipeline, pipeline, 1);
   VK_MULTIALLOC_DECL(&ma, struct anv_rt_shader_group, groups, group_count);
   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = anv_pipeline_init(&pipeline->base, device,
                              ANV_PIPELINE_RAY_TRACING, pCreateInfo->flags,
                              pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   pipeline->group_count = group_count;
   pipeline->groups = groups;

   ASSERTED const VkShaderStageFlags ray_tracing_stages =
      VK_SHADER_STAGE_RAYGEN_BIT_KHR |
      VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
      VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
      VK_SHADER_STAGE_MISS_BIT_KHR |
      VK_SHADER_STAGE_INTERSECTION_BIT_KHR |
      VK_SHADER_STAGE_CALLABLE_BIT_KHR;

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++)
      assert((pCreateInfo->pStages[i].stage & ~ray_tracing_stages) == 0);

   for (uint32_t i = 0; i < pCreateInfo->groupCount; i++) {
      const VkRayTracingShaderGroupCreateInfoKHR *ginfo =
         &pCreateInfo->pGroups[i];
      assert_rt_stage_index_valid(pCreateInfo, ginfo->generalShader,
                                  VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                  VK_SHADER_STAGE_MISS_BIT_KHR |
                                  VK_SHADER_STAGE_CALLABLE_BIT_KHR);
      assert_rt_stage_index_valid(pCreateInfo, ginfo->closestHitShader,
                                  VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
      assert_rt_stage_index_valid(pCreateInfo, ginfo->anyHitShader,
                                  VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
      assert_rt_stage_index_valid(pCreateInfo, ginfo->intersectionShader,
                                  VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
      switch (ginfo->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         assert(ginfo->generalShader < pCreateInfo->stageCount);
         assert(ginfo->anyHitShader == VK_SHADER_UNUSED_KHR);
         assert(ginfo->closestHitShader == VK_SHADER_UNUSED_KHR);
         assert(ginfo->intersectionShader == VK_SHADER_UNUSED_KHR);
         break;

      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         assert(ginfo->generalShader == VK_SHADER_UNUSED_KHR);
         assert(ginfo->intersectionShader == VK_SHADER_UNUSED_KHR);
         break;

      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         assert(ginfo->generalShader == VK_SHADER_UNUSED_KHR);
         break;

      default:
         unreachable("Invalid ray-tracing shader group type");
      }
   }

   result = anv_ray_tracing_pipeline_init(pipeline, device, cache,
                                          pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      anv_pipeline_finish(&pipeline->base, device, pAllocator);
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   /* Compute the size of the scratch BO (for register spilling) by taking the
    * max of all the shaders in the pipeline.
    */
   util_dynarray_foreach(&pipeline->shaders, struct anv_shader_bin *, shader) {
      pipeline->scratch_size =
         MAX2(pipeline->scratch_size, (*shader)->prog_data->total_scratch);
   }

   if (pCreateInfo->pLibraryInfo) {
      uint32_t g = pCreateInfo->groupCount;
      for (uint32_t l = 0; l < pCreateInfo->pLibraryInfo->libraryCount; l++) {
         ANV_FROM_HANDLE(anv_pipeline, library,
                         pCreateInfo->pLibraryInfo->pLibraries[l]);
         struct anv_ray_tracing_pipeline *rt_library =
            anv_pipeline_to_ray_tracing(library);
         for (uint32_t lg = 0; lg < rt_library->group_count; lg++)
            pipeline->groups[g++] = rt_library->groups[lg];

         /* Also account for all the pipeline libraries for the size of the
          * scratch BO.
          */
         pipeline->scratch_size =
            MAX2(pipeline->scratch_size, rt_library->scratch_size);
      }
   }

   anv_genX(device->info, ray_tracing_pipeline_emit)(pipeline);

   *pPipeline = anv_pipeline_to_handle(&pipeline->base);

   return pipeline->base.batch.status;
}

VkResult
anv_CreateRayTracingPipelinesKHR(
    VkDevice                                    _device,
    VkDeferredOperationKHR                      deferredOperation,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkRayTracingPipelineCreateInfoKHR*    pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines)
{
   ANV_FROM_HANDLE(vk_pipeline_cache, pipeline_cache, pipelineCache);

   VkResult result = VK_SUCCESS;

   unsigned i;
   for (i = 0; i < createInfoCount; i++) {
      VkResult res = anv_ray_tracing_pipeline_create(_device, pipeline_cache,
                                                     &pCreateInfos[i],
                                                     pAllocator, &pPipelines[i]);

      if (res == VK_SUCCESS)
         continue;

      /* Bail out on the first error as it is not obvious what error should be
       * report upon 2 different failures. */
      result = res;
      if (result != VK_PIPELINE_COMPILE_REQUIRED)
         break;

      pPipelines[i] = VK_NULL_HANDLE;

      if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
         break;
   }

   for (; i < createInfoCount; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

#define WRITE_STR(field, ...) ({                               \
   memset(field, 0, sizeof(field));                            \
   UNUSED int i = snprintf(field, sizeof(field), __VA_ARGS__); \
   assert(i > 0 && i < sizeof(field));                         \
})

VkResult anv_GetPipelineExecutablePropertiesKHR(
    VkDevice                                    device,
    const VkPipelineInfoKHR*                    pPipelineInfo,
    uint32_t*                                   pExecutableCount,
    VkPipelineExecutablePropertiesKHR*          pProperties)
{
   ANV_FROM_HANDLE(anv_pipeline, pipeline, pPipelineInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out,
                          pProperties, pExecutableCount);

   util_dynarray_foreach (&pipeline->executables, struct anv_pipeline_executable, exe) {
      vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props) {
         gl_shader_stage stage = exe->stage;
         props->stages = mesa_to_vk_shader_stage(stage);

         unsigned simd_width = exe->stats.dispatch_width;
         if (stage == MESA_SHADER_FRAGMENT) {
            WRITE_STR(props->name, "%s%d %s",
                      simd_width ? "SIMD" : "vec",
                      simd_width ? simd_width : 4,
                      _mesa_shader_stage_to_string(stage));
         } else {
            WRITE_STR(props->name, "%s", _mesa_shader_stage_to_string(stage));
         }
         WRITE_STR(props->description, "%s%d %s shader",
                   simd_width ? "SIMD" : "vec",
                   simd_width ? simd_width : 4,
                   _mesa_shader_stage_to_string(stage));

         /* The compiler gives us a dispatch width of 0 for vec4 but Vulkan
          * wants a subgroup size of 1.
          */
         props->subgroupSize = MAX2(simd_width, 1);
      }
   }

   return vk_outarray_status(&out);
}

static const struct anv_pipeline_executable *
anv_pipeline_get_executable(struct anv_pipeline *pipeline, uint32_t index)
{
   assert(index < util_dynarray_num_elements(&pipeline->executables,
                                             struct anv_pipeline_executable));
   return util_dynarray_element(
      &pipeline->executables, struct anv_pipeline_executable, index);
}

VkResult anv_GetPipelineExecutableStatisticsKHR(
    VkDevice                                    device,
    const VkPipelineExecutableInfoKHR*          pExecutableInfo,
    uint32_t*                                   pStatisticCount,
    VkPipelineExecutableStatisticKHR*           pStatistics)
{
   ANV_FROM_HANDLE(anv_pipeline, pipeline, pExecutableInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out,
                          pStatistics, pStatisticCount);

   const struct anv_pipeline_executable *exe =
      anv_pipeline_get_executable(pipeline, pExecutableInfo->executableIndex);

   const struct brw_stage_prog_data *prog_data;
   switch (pipeline->type) {
   case ANV_PIPELINE_GRAPHICS: {
      prog_data = anv_pipeline_to_graphics(pipeline)->shaders[exe->stage]->prog_data;
      break;
   }
   case ANV_PIPELINE_COMPUTE: {
      prog_data = anv_pipeline_to_compute(pipeline)->cs->prog_data;
      break;
   }
   case ANV_PIPELINE_RAY_TRACING: {
      struct anv_shader_bin **shader =
         util_dynarray_element(&anv_pipeline_to_ray_tracing(pipeline)->shaders,
                               struct anv_shader_bin *,
                               pExecutableInfo->executableIndex);
      prog_data = (*shader)->prog_data;
      break;
   }
   default:
      unreachable("invalid pipeline type");
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instruction Count");
      WRITE_STR(stat->description,
                "Number of GEN instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.instructions;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "SEND Count");
      WRITE_STR(stat->description,
                "Number of instructions in the final generated shader "
                "executable which access external units such as the "
                "constant cache or the sampler.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.sends;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Loop Count");
      WRITE_STR(stat->description,
                "Number of loops (not unrolled) in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.loops;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Cycle Count");
      WRITE_STR(stat->description,
                "Estimate of the number of EU cycles required to execute "
                "the final generated executable.  This is an estimate only "
                "and may vary greatly from actual run-time performance.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.cycles;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Spill Count");
      WRITE_STR(stat->description,
                "Number of scratch spill operations.  This gives a rough "
                "estimate of the cost incurred due to spilling temporary "
                "values to memory.  If this is non-zero, you may want to "
                "adjust your shader to reduce register pressure.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.spills;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Fill Count");
      WRITE_STR(stat->description,
                "Number of scratch fill operations.  This gives a rough "
                "estimate of the cost incurred due to spilling temporary "
                "values to memory.  If this is non-zero, you may want to "
                "adjust your shader to reduce register pressure.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.fills;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Scratch Memory Size");
      WRITE_STR(stat->description,
                "Number of bytes of scratch memory required by the "
                "generated shader executable.  If this is non-zero, you "
                "may want to adjust your shader to reduce register "
                "pressure.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = prog_data->total_scratch;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Max dispatch width");
      WRITE_STR(stat->description,
                "Largest SIMD dispatch width.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_dispatch_width;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Max live registers");
      WRITE_STR(stat->description,
                "Maximum number of registers used across the entire shader.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_live_registers;
   }

   if (gl_shader_stage_uses_workgroup(exe->stage)) {
      vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
         WRITE_STR(stat->name, "Workgroup Memory Size");
         WRITE_STR(stat->description,
                   "Number of bytes of workgroup shared memory used by this "
                   "shader including any padding.");
         stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
         stat->value.u64 = prog_data->total_shared;
      }
   }

   return vk_outarray_status(&out);
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR* ir,
              const char *data)
{
   ir->isText = VK_TRUE;

   size_t data_len = strlen(data) + 1;

   if (ir->pData == NULL) {
      ir->dataSize = data_len;
      return true;
   }

   strncpy(ir->pData, data, ir->dataSize);
   if (ir->dataSize < data_len)
      return false;

   ir->dataSize = data_len;
   return true;
}

VkResult anv_GetPipelineExecutableInternalRepresentationsKHR(
    VkDevice                                    device,
    const VkPipelineExecutableInfoKHR*          pExecutableInfo,
    uint32_t*                                   pInternalRepresentationCount,
    VkPipelineExecutableInternalRepresentationKHR* pInternalRepresentations)
{
   ANV_FROM_HANDLE(anv_pipeline, pipeline, pExecutableInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          pInternalRepresentations, pInternalRepresentationCount);
   bool incomplete_text = false;

   const struct anv_pipeline_executable *exe =
      anv_pipeline_get_executable(pipeline, pExecutableInfo->executableIndex);

   if (exe->nir) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "Final NIR");
         WRITE_STR(ir->description,
                   "Final NIR before going into the back-end compiler");

         if (!write_ir_text(ir, exe->nir))
            incomplete_text = true;
      }
   }

   if (exe->disasm) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "GEN Assembly");
         WRITE_STR(ir->description,
                   "Final GEN assembly for the generated shader binary");

         if (!write_ir_text(ir, exe->disasm))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}

VkResult
anv_GetRayTracingShaderGroupHandlesKHR(
    VkDevice                                    _device,
    VkPipeline                                  _pipeline,
    uint32_t                                    firstGroup,
    uint32_t                                    groupCount,
    size_t                                      dataSize,
    void*                                       pData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_pipeline, pipeline, _pipeline);

   if (pipeline->type != ANV_PIPELINE_RAY_TRACING)
      return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);

   struct anv_ray_tracing_pipeline *rt_pipeline =
      anv_pipeline_to_ray_tracing(pipeline);

   assert(firstGroup + groupCount <= rt_pipeline->group_count);
   for (uint32_t i = 0; i < groupCount; i++) {
      struct anv_rt_shader_group *group = &rt_pipeline->groups[firstGroup + i];
      memcpy(pData, group->handle, sizeof(group->handle));
      pData += sizeof(group->handle);
   }

   return VK_SUCCESS;
}

VkResult
anv_GetRayTracingCaptureReplayShaderGroupHandlesKHR(
    VkDevice                                    _device,
    VkPipeline                                  pipeline,
    uint32_t                                    firstGroup,
    uint32_t                                    groupCount,
    size_t                                      dataSize,
    void*                                       pData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   unreachable("Unimplemented");
   return vk_error(device, VK_ERROR_FEATURE_NOT_PRESENT);
}

VkDeviceSize
anv_GetRayTracingShaderGroupStackSizeKHR(
    VkDevice                                    device,
    VkPipeline                                  _pipeline,
    uint32_t                                    group,
    VkShaderGroupShaderKHR                      groupShader)
{
   ANV_FROM_HANDLE(anv_pipeline, pipeline, _pipeline);
   assert(pipeline->type == ANV_PIPELINE_RAY_TRACING);

   struct anv_ray_tracing_pipeline *rt_pipeline =
      anv_pipeline_to_ray_tracing(pipeline);

   assert(group < rt_pipeline->group_count);

   struct anv_shader_bin *bin;
   switch (groupShader) {
   case VK_SHADER_GROUP_SHADER_GENERAL_KHR:
      bin = rt_pipeline->groups[group].general;
      break;

   case VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR:
      bin = rt_pipeline->groups[group].closest_hit;
      break;

   case VK_SHADER_GROUP_SHADER_ANY_HIT_KHR:
      bin = rt_pipeline->groups[group].any_hit;
      break;

   case VK_SHADER_GROUP_SHADER_INTERSECTION_KHR:
      bin = rt_pipeline->groups[group].intersection;
      break;

   default:
      unreachable("Invalid VkShaderGroupShader enum");
   }

   if (bin == NULL)
      return 0;

   return brw_bs_prog_data_const(bin->prog_data)->max_stack_size;
}
