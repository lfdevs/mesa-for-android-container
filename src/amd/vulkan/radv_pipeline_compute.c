/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
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

#include "meta/radv_meta.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_serialize.h"
#include "nir/nir_vulkan.h"
#include "nir/radv_nir.h"
#include "spirv/nir_spirv.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/os_time.h"
#include "util/u_atomic.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"
#include "vk_pipeline.h"
#include "vk_render_pass.h"
#include "vk_util.h"

#include "util/u_debug.h"
#include "ac_binary.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "aco_interface.h"
#include "sid.h"
#include "vk_format.h"

void
radv_pipeline_emit_hw_cs(const struct radv_physical_device *pdevice, struct radeon_cmdbuf *cs,
                         const struct radv_shader *shader)
{
   uint64_t va = radv_shader_get_va(shader);

   radeon_set_sh_reg(cs, R_00B830_COMPUTE_PGM_LO, va >> 8);

   radeon_set_sh_reg_seq(cs, R_00B848_COMPUTE_PGM_RSRC1, 2);
   radeon_emit(cs, shader->config.rsrc1);
   radeon_emit(cs, shader->config.rsrc2);
   if (pdevice->rad_info.gfx_level >= GFX10) {
      radeon_set_sh_reg(cs, R_00B8A0_COMPUTE_PGM_RSRC3, shader->config.rsrc3);
   }
}

void
radv_pipeline_emit_compute_state(const struct radv_physical_device *pdevice,
                                 struct radeon_cmdbuf *cs, const struct radv_shader *shader)
{
   unsigned threads_per_threadgroup;
   unsigned threadgroups_per_cu = 1;
   unsigned waves_per_threadgroup;
   unsigned max_waves_per_sh = 0;

   /* Calculate best compute resource limits. */
   threads_per_threadgroup =
      shader->info.cs.block_size[0] * shader->info.cs.block_size[1] * shader->info.cs.block_size[2];
   waves_per_threadgroup = DIV_ROUND_UP(threads_per_threadgroup, shader->info.wave_size);

   if (pdevice->rad_info.gfx_level >= GFX10 && waves_per_threadgroup == 1)
      threadgroups_per_cu = 2;

   radeon_set_sh_reg(cs, R_00B854_COMPUTE_RESOURCE_LIMITS,
                     ac_get_compute_resource_limits(&pdevice->rad_info, waves_per_threadgroup,
                                                    max_waves_per_sh, threadgroups_per_cu));

   radeon_set_sh_reg_seq(cs, R_00B81C_COMPUTE_NUM_THREAD_X, 3);
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[0]));
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[1]));
   radeon_emit(cs, S_00B81C_NUM_THREAD_FULL(shader->info.cs.block_size[2]));
}

static void
radv_compute_generate_pm4(const struct radv_device *device, struct radv_compute_pipeline *pipeline)
{
   struct radv_physical_device *pdevice = device->physical_device;
   struct radv_shader *shader = pipeline->base.shaders[MESA_SHADER_COMPUTE];
   struct radeon_cmdbuf *cs = &pipeline->base.cs;

   cs->max_dw = pdevice->rad_info.gfx_level >= GFX10 ? 19 : 16;
   cs->buf = malloc(cs->max_dw * 4);

   radv_pipeline_emit_hw_cs(pdevice, cs, shader);
   radv_pipeline_emit_compute_state(pdevice, cs, shader);

   assert(pipeline->base.cs.cdw <= pipeline->base.cs.max_dw);
}

static struct radv_pipeline_key
radv_generate_compute_pipeline_key(const struct radv_device *device,
                                   struct radv_compute_pipeline *pipeline,
                                   const VkComputePipelineCreateInfo *pCreateInfo)
{
   const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->stage;
   struct radv_pipeline_key key =
      radv_generate_pipeline_key(device, &pipeline->base, pCreateInfo->flags);

   const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *subgroup_size =
      vk_find_struct_const(stage->pNext, PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);

   if (subgroup_size) {
      assert(subgroup_size->requiredSubgroupSize == 32 ||
             subgroup_size->requiredSubgroupSize == 64);
      key.cs.compute_subgroup_size = subgroup_size->requiredSubgroupSize;
   } else if (stage->flags & VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT) {
      key.cs.require_full_subgroups = true;
   }

   return key;
}

void
radv_compute_pipeline_init(const struct radv_device *device, struct radv_compute_pipeline *pipeline,
                           const struct radv_pipeline_layout *layout)
{
   pipeline->base.need_indirect_descriptor_sets |=
      radv_shader_need_indirect_descriptor_sets(pipeline->base.shaders[MESA_SHADER_COMPUTE]);
   radv_pipeline_init_scratch(device, &pipeline->base);

   pipeline->base.push_constant_size = layout->push_constant_size;
   pipeline->base.dynamic_offset_count = layout->dynamic_offset_count;

   pipeline->base.shader_upload_seq = pipeline->base.shaders[MESA_SHADER_COMPUTE]->upload_seq;

   radv_compute_generate_pm4(device, pipeline);
}

static VkResult
radv_compute_pipeline_compile(struct radv_compute_pipeline *pipeline,
                              struct radv_pipeline_layout *pipeline_layout,
                              struct radv_device *device, struct vk_pipeline_cache *cache,
                              const struct radv_pipeline_key *pipeline_key,
                              const VkPipelineShaderStageCreateInfo *pStage,
                              const VkPipelineCreateFlags flags,
                              const VkPipelineCreationFeedbackCreateInfo *creation_feedback)
{
   struct radv_shader_binary *binaries[MESA_VULKAN_SHADER_STAGES] = {NULL};
   unsigned char hash[20];
   bool keep_executable_info = radv_pipeline_capture_shaders(device, flags);
   bool keep_statistic_info = radv_pipeline_capture_shader_stats(device, flags);
   struct radv_pipeline_stage cs_stage = {0};
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   VkResult result = VK_SUCCESS;

   int64_t pipeline_start = os_time_get_nano();

   radv_pipeline_stage_init(pStage, &cs_stage, MESA_SHADER_COMPUTE);

   radv_hash_shaders(hash, &cs_stage, 1, pipeline_layout, pipeline_key,
                     radv_get_hash_flags(device, keep_statistic_info));

   pipeline->base.pipeline_hash = *(uint64_t *)hash;

   bool found_in_application_cache = true;
   if (!keep_executable_info && radv_pipeline_cache_search(device, cache, &pipeline->base, hash,
                                                           &found_in_application_cache)) {
      if (found_in_application_cache)
         pipeline_feedback.flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      result = VK_SUCCESS;
      goto done;
   }

   if (flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
      return VK_PIPELINE_COMPILE_REQUIRED;

   int64_t stage_start = os_time_get_nano();

   /* Compile SPIR-V shader to NIR. */
   cs_stage.nir =
      radv_shader_spirv_to_nir(device, &cs_stage, pipeline_key, pipeline->base.is_internal);

   radv_optimize_nir(cs_stage.nir, pipeline_key->optimisations_disabled);

   /* Gather info again, information such as outputs_read can be out-of-date. */
   nir_shader_gather_info(cs_stage.nir, nir_shader_get_entrypoint(cs_stage.nir));

   cs_stage.feedback.duration += os_time_get_nano() - stage_start;

   /* Run the shader info pass. */
   radv_nir_shader_info_init(&cs_stage.info);
   radv_nir_shader_info_pass(device, cs_stage.nir, MESA_SHADER_NONE, pipeline_layout, pipeline_key,
                             pipeline->base.type, false, &cs_stage.info);

   radv_declare_shader_args(device, pipeline_key, &cs_stage.info, MESA_SHADER_COMPUTE,
                            MESA_SHADER_NONE, RADV_SHADER_TYPE_DEFAULT, &cs_stage.args);

   cs_stage.info.user_sgprs_locs = cs_stage.args.user_sgprs_locs;
   cs_stage.info.inline_push_constant_mask = cs_stage.args.ac.inline_push_const_mask;

   stage_start = os_time_get_nano();

   /* Postprocess NIR. */
   radv_postprocess_nir(device, pipeline_layout, pipeline_key, MESA_SHADER_NONE, &cs_stage);

   if (radv_can_dump_shader(device, cs_stage.nir, false))
      nir_print_shader(cs_stage.nir, stderr);

   /* Compile NIR shader to AMD assembly. */
   pipeline->base.shaders[MESA_SHADER_COMPUTE] = radv_shader_nir_to_asm(
      device, cache, &cs_stage, &cs_stage.nir, 1, pipeline_key, keep_executable_info,
      keep_statistic_info, &binaries[MESA_SHADER_COMPUTE]);

   cs_stage.feedback.duration += os_time_get_nano() - stage_start;

   if (keep_executable_info) {
      struct radv_shader *shader = pipeline->base.shaders[MESA_SHADER_COMPUTE];

      if (cs_stage.spirv.size) {
         shader->spirv = malloc(cs_stage.spirv.size);
         memcpy(shader->spirv, cs_stage.spirv.data, cs_stage.spirv.size);
         shader->spirv_size = cs_stage.spirv.size;
      }
   }

   if (!keep_executable_info) {
      radv_pipeline_cache_insert(device, cache, &pipeline->base, NULL, hash);
   }

   free(binaries[MESA_SHADER_COMPUTE]);
   if (radv_can_dump_shader_stats(device, cs_stage.nir)) {
      radv_dump_shader_stats(device, &pipeline->base, pipeline->base.shaders[MESA_SHADER_COMPUTE],
                             MESA_SHADER_COMPUTE, stderr);
   }
   ralloc_free(cs_stage.nir);

done:
   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

      if (creation_feedback->pipelineStageCreationFeedbackCount) {
         assert(creation_feedback->pipelineStageCreationFeedbackCount == 1);
         creation_feedback->pPipelineStageCreationFeedbacks[0] = cs_stage.feedback;
      }
   }

   return result;
}

VkResult
radv_compute_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                             const VkComputePipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(vk_pipeline_cache, cache, _cache);
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   struct radv_compute_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL) {
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_COMPUTE);
   pipeline->base.is_internal = _cache == device->meta_state.cache;

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   struct radv_pipeline_key key = radv_generate_compute_pipeline_key(device, pipeline, pCreateInfo);

   result =
      radv_compute_pipeline_compile(pipeline, pipeline_layout, device, cache, &key,
                                    &pCreateInfo->stage, pCreateInfo->flags, creation_feedback);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base, pAllocator);
      return result;
   }

   radv_compute_pipeline_init(device, pipeline, pipeline_layout);

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);
   radv_rmv_log_compute_pipeline_create(device, pCreateInfo->flags, &pipeline->base,
                                        pipeline->base.is_internal);
   return VK_SUCCESS;
}

static VkResult
radv_create_compute_pipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                              const VkComputePipelineCreateInfo *pCreateInfos,
                              const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = radv_compute_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator,
                                       &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}

void
radv_destroy_compute_pipeline(struct radv_device *device, struct radv_compute_pipeline *pipeline)
{
   if (pipeline->base.shaders[MESA_SHADER_COMPUTE])
      radv_shader_unref(device, pipeline->base.shaders[MESA_SHADER_COMPUTE]);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateComputePipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                            const VkComputePipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   return radv_create_compute_pipelines(_device, pipelineCache, count, pCreateInfos, pAllocator,
                                        pPipelines);
}
