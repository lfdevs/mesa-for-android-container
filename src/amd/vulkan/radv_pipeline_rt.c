/*
 * Copyright © 2021 Google
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

#include "nir/nir.h"

#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"

struct rt_handle_hash_entry {
   uint32_t key;
   char hash[20];
};

static uint32_t
handle_from_stages(struct radv_device *device, const VkPipelineShaderStageCreateInfo *stages,
                   unsigned stage_count, bool replay_namespace)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   radv_hash_rt_stages(&ctx, stages, stage_count);
   unsigned char hash[20];
   _mesa_sha1_final(&ctx, hash);

   uint32_t ret;
   memcpy(&ret, hash, sizeof(ret));

   /* Leave the low half for resume shaders etc. */
   ret |= 1u << 31;

   /* Ensure we have dedicated space for replayable shaders */
   ret &= ~(1u << 30);
   ret |= replay_namespace << 30;

   simple_mtx_lock(&device->rt_handles_mtx);

   struct hash_entry *he = NULL;
   for (;;) {
      he = _mesa_hash_table_search(device->rt_handles, &ret);
      if (!he)
         break;

      if (memcmp(he->data, hash, sizeof(hash)) == 0)
         break;

      ++ret;
   }

   if (!he) {
      struct rt_handle_hash_entry *e = ralloc(device->rt_handles, struct rt_handle_hash_entry);
      e->key = ret;
      memcpy(e->hash, hash, sizeof(e->hash));
      _mesa_hash_table_insert(device->rt_handles, &e->key, &e->hash);
   }

   simple_mtx_unlock(&device->rt_handles_mtx);

   return ret;
}

static VkResult
radv_create_group_handles(struct radv_device *device,
                          const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                          struct radv_ray_tracing_module *groups)
{
   bool capture_replay = pCreateInfo->flags &
                         VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR;
   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         if (group_info->generalShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.general_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->generalShader], 1, capture_replay);
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.closest_hit_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->closestHitShader], 1, capture_replay);
         if (group_info->intersectionShader != VK_SHADER_UNUSED_KHR) {
            VkPipelineShaderStageCreateInfo stages[2];
            unsigned cnt = 0;
            stages[cnt++] = pCreateInfo->pStages[group_info->intersectionShader];
            if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
               stages[cnt++] = pCreateInfo->pStages[group_info->anyHitShader];
            groups[i].handle.intersection_index =
               handle_from_stages(device, stages, cnt, capture_replay);
         }
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.closest_hit_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->closestHitShader], 1, capture_replay);
         if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
            groups[i].handle.any_hit_index = handle_from_stages(
               device, &pCreateInfo->pStages[group_info->anyHitShader], 1, capture_replay);
         break;
      case VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR:
         unreachable("VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR");
      }

      if (capture_replay) {
         if (group_info->pShaderGroupCaptureReplayHandle &&
             memcmp(group_info->pShaderGroupCaptureReplayHandle, &groups[i].handle,
                    sizeof(groups[i].handle)) != 0) {
            return VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
         }
      }
   }

   return VK_SUCCESS;
}

static VkRayTracingPipelineCreateInfoKHR
radv_create_merged_rt_create_info(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   VkRayTracingPipelineCreateInfoKHR local_create_info = *pCreateInfo;
   uint32_t total_stages = pCreateInfo->stageCount;
   uint32_t total_groups = pCreateInfo->groupCount;

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_lib_pipeline *library_pipeline =
            radv_pipeline_to_ray_tracing_lib(pipeline);

         total_stages += library_pipeline->stage_count;
         total_groups += library_pipeline->group_count;
      }
   }
   VkPipelineShaderStageCreateInfo *stages = NULL;
   VkRayTracingShaderGroupCreateInfoKHR *groups = NULL;
   local_create_info.stageCount = total_stages;
   local_create_info.groupCount = total_groups;
   local_create_info.pStages = stages =
      malloc(sizeof(VkPipelineShaderStageCreateInfo) * total_stages);
   if (!local_create_info.pStages)
      return local_create_info;
   local_create_info.pGroups = groups =
      malloc(sizeof(VkRayTracingShaderGroupCreateInfoKHR) * total_groups);
   if (!local_create_info.pGroups) {
      free((void *)local_create_info.pStages);
      /* Some compilers throw use-after-free errors despite all callers immediately returning
       * VK_ERROR_OUT_OF_HOST_MEMORY in this case, circumvent those by setting pStages to NULL */
      local_create_info.pStages = NULL;
      return local_create_info;
   }

   total_stages = pCreateInfo->stageCount;
   total_groups = pCreateInfo->groupCount;
   for (unsigned j = 0; j < pCreateInfo->stageCount; ++j)
      stages[j] = pCreateInfo->pStages[j];
   for (unsigned j = 0; j < pCreateInfo->groupCount; ++j)
      groups[j] = pCreateInfo->pGroups[j];

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_ray_tracing_lib_pipeline *library_pipeline =
            radv_pipeline_to_ray_tracing_lib(pipeline);

         for (unsigned j = 0; j < library_pipeline->stage_count; ++j)
            stages[total_stages + j] = library_pipeline->stages[j];
         for (unsigned j = 0; j < library_pipeline->group_count; ++j) {
            VkRayTracingShaderGroupCreateInfoKHR *dst = &groups[total_groups + j];
            *dst = library_pipeline->group_infos[j];
            if (dst->generalShader != VK_SHADER_UNUSED_KHR)
               dst->generalShader += total_stages;
            if (dst->closestHitShader != VK_SHADER_UNUSED_KHR)
               dst->closestHitShader += total_stages;
            if (dst->anyHitShader != VK_SHADER_UNUSED_KHR)
               dst->anyHitShader += total_stages;
            if (dst->intersectionShader != VK_SHADER_UNUSED_KHR)
               dst->intersectionShader += total_stages;
         }
         total_stages += library_pipeline->stage_count;
         total_groups += library_pipeline->group_count;
      }
   }
   return local_create_info;
}

static VkResult
radv_rt_pipeline_compile(struct radv_ray_tracing_pipeline *pipeline,
                         struct radv_pipeline_layout *pipeline_layout, struct radv_device *device,
                         struct vk_pipeline_cache *cache,
                         const struct radv_pipeline_key *pipeline_key,
                         const VkPipelineShaderStageCreateInfo *pStage,
                         const VkPipelineCreateFlags flags, const uint8_t *custom_hash,
                         const VkPipelineCreationFeedbackCreateInfo *creation_feedback,
                         struct radv_ray_tracing_module *rt_groups, uint32_t num_rt_groups)
{
   struct radv_shader_binary *binaries[MESA_VULKAN_SHADER_STAGES] = {NULL};
   unsigned char hash[20];
   bool keep_executable_info = radv_pipeline_capture_shaders(device, flags);
   bool keep_statistic_info = radv_pipeline_capture_shader_stats(device, flags);
   struct radv_pipeline_stage rt_stage = {0};
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   VkResult result = VK_SUCCESS;

   int64_t pipeline_start = os_time_get_nano();

   radv_pipeline_stage_init(pStage, &rt_stage, vk_to_mesa_shader_stage(pStage->stage));

   if (custom_hash) {
      memcpy(hash, custom_hash, 20);
   } else {
      radv_hash_shaders(hash, &rt_stage, 1, pipeline_layout, pipeline_key,
                        radv_get_hash_flags(device, keep_statistic_info));
   }

   pipeline->base.base.pipeline_hash = *(uint64_t *)hash;

   bool found_in_application_cache = true;
   if (!keep_executable_info && radv_pipeline_cache_search(device, cache, &pipeline->base.base,
                                                           hash, &found_in_application_cache)) {
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
   rt_stage.nir =
      radv_shader_spirv_to_nir(device, &rt_stage, pipeline_key, pipeline->base.base.is_internal);

   radv_optimize_nir(rt_stage.nir, pipeline_key->optimisations_disabled);

   /* Gather info again, information such as outputs_read can be out-of-date. */
   nir_shader_gather_info(rt_stage.nir, nir_shader_get_entrypoint(rt_stage.nir));

   rt_stage.feedback.duration += os_time_get_nano() - stage_start;

   /* Run the shader info pass. */
   radv_nir_shader_info_init(&rt_stage.info);
   radv_nir_shader_info_pass(device, rt_stage.nir, MESA_SHADER_NONE, pipeline_layout, pipeline_key,
                             pipeline->base.base.type, false, &rt_stage.info);

   radv_declare_shader_args(device, pipeline_key, &rt_stage.info, rt_stage.stage, MESA_SHADER_NONE,
                            RADV_SHADER_TYPE_DEFAULT, &rt_stage.args);

   rt_stage.info.user_sgprs_locs = rt_stage.args.user_sgprs_locs;
   rt_stage.info.inline_push_constant_mask = rt_stage.args.ac.inline_push_const_mask;

   stage_start = os_time_get_nano();

   /* Postprocess NIR. */
   radv_postprocess_nir(device, pipeline_layout, pipeline_key, MESA_SHADER_NONE, &rt_stage);

   if (radv_can_dump_shader(device, rt_stage.nir, false))
      nir_print_shader(rt_stage.nir, stderr);

   /* Compile NIR shader to AMD assembly. */
   pipeline->base.base.shaders[rt_stage.stage] =
      radv_shader_nir_to_asm(device, cache, &rt_stage, &rt_stage.nir, 1, pipeline_key,
                             keep_executable_info, keep_statistic_info, &binaries[rt_stage.stage]);

   rt_stage.feedback.duration += os_time_get_nano() - stage_start;

   if (keep_executable_info) {
      struct radv_shader *shader = pipeline->base.base.shaders[rt_stage.stage];

      if (rt_stage.spirv.size) {
         shader->spirv = malloc(rt_stage.spirv.size);
         memcpy(shader->spirv, rt_stage.spirv.data, rt_stage.spirv.size);
         shader->spirv_size = rt_stage.spirv.size;
      }
   }

   if (!keep_executable_info) {
      radv_pipeline_cache_insert(device, cache, &pipeline->base.base, NULL, hash);
   }

   free(binaries[rt_stage.stage]);
   if (radv_can_dump_shader_stats(device, rt_stage.nir)) {
      radv_dump_shader_stats(device, &pipeline->base.base,
                             pipeline->base.base.shaders[rt_stage.stage], rt_stage.stage, stderr);
   }
   ralloc_free(rt_stage.nir);

done:
   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

      if (creation_feedback->pipelineStageCreationFeedbackCount) {
         assert(creation_feedback->pipelineStageCreationFeedbackCount == 1);
         creation_feedback->pPipelineStageCreationFeedbacks[0] = rt_stage.feedback;
      }
   }

   return result;
}

static VkResult
radv_rt_pipeline_library_create(VkDevice _device, VkPipelineCache _cache,
                                const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_ray_tracing_lib_pipeline *pipeline;
   VkResult result = VK_SUCCESS;

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);
   if (!local_create_info.pStages || !local_create_info.pGroups)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   size_t pipeline_size =
      sizeof(*pipeline) + local_create_info.groupCount * sizeof(struct radv_ray_tracing_module);
   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, pipeline_size, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pipeline) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_RAY_TRACING_LIB);

   pipeline->ctx = ralloc_context(NULL);

   result = radv_create_group_handles(device, &local_create_info, pipeline->groups);
   if (result != VK_SUCCESS)
      goto pipeline_fail;

   if (local_create_info.stageCount) {
      pipeline->stages = radv_copy_shader_stage_create_info(
         device, local_create_info.stageCount, local_create_info.pStages, pipeline->ctx);
      if (!pipeline->stages) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto pipeline_fail;
      }

      pipeline->stage_count = local_create_info.stageCount;
   }

   if (local_create_info.groupCount) {
      size_t size = sizeof(VkRayTracingShaderGroupCreateInfoKHR) * local_create_info.groupCount;
      pipeline->group_count = local_create_info.groupCount;
      pipeline->group_infos = ralloc_size(pipeline->ctx, size);
      if (!pipeline->group_infos) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto pipeline_fail;
      }
      memcpy(pipeline->group_infos, local_create_info.pGroups, size);
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);

pipeline_fail:
   if (result != VK_SUCCESS)
      radv_pipeline_destroy(device, &pipeline->base, pAllocator);
fail:
   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return result;
}

void
radv_destroy_ray_tracing_lib_pipeline(struct radv_device *device,
                                      struct radv_ray_tracing_lib_pipeline *pipeline)
{
   ralloc_free(pipeline->ctx);
}

static bool
radv_rt_pipeline_has_dynamic_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   if (!pCreateInfo->pDynamicState)
      return false;

   for (unsigned i = 0; i < pCreateInfo->pDynamicState->dynamicStateCount; ++i) {
      if (pCreateInfo->pDynamicState->pDynamicStates[i] ==
          VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR)
         return true;
   }

   return false;
}

static unsigned
compute_rt_stack_size(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                      const struct radv_ray_tracing_module *groups)
{
   if (radv_rt_pipeline_has_dynamic_stack_size(pCreateInfo))
      return -1u;

   unsigned raygen_size = 0;
   unsigned callable_size = 0;
   unsigned chit_size = 0;
   unsigned miss_size = 0;
   unsigned non_recursive_size = 0;

   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      non_recursive_size = MAX2(groups[i].stack_size.non_recursive_size, non_recursive_size);

      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      uint32_t shader_id = VK_SHADER_UNUSED_KHR;
      unsigned size = groups[i].stack_size.recursive_size;

      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         shader_id = group_info->generalShader;
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         shader_id = group_info->closestHitShader;
         break;
      default:
         break;
      }
      if (shader_id == VK_SHADER_UNUSED_KHR)
         continue;

      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[shader_id];
      switch (stage->stage) {
      case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
         raygen_size = MAX2(raygen_size, size);
         break;
      case VK_SHADER_STAGE_MISS_BIT_KHR:
         miss_size = MAX2(miss_size, size);
         break;
      case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
         chit_size = MAX2(chit_size, size);
         break;
      case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
         callable_size = MAX2(callable_size, size);
         break;
      default:
         unreachable("Invalid stage type in RT shader");
      }
   }
   return raygen_size +
          MIN2(pCreateInfo->maxPipelineRayRecursionDepth, 1) *
             MAX2(MAX2(chit_size, miss_size), non_recursive_size) +
          MAX2(0, (int)(pCreateInfo->maxPipelineRayRecursionDepth) - 1) *
             MAX2(chit_size, miss_size) +
          2 * callable_size;
}

static struct radv_pipeline_key
radv_generate_rt_pipeline_key(const struct radv_device *device,
                              const struct radv_ray_tracing_pipeline *pipeline,
                              VkPipelineCreateFlags flags)
{
   struct radv_pipeline_key key = radv_generate_pipeline_key(device, &pipeline->base.base, flags);
   key.cs.compute_subgroup_size = device->physical_device->rt_wave_size;

   return key;
}

static void
combine_config(struct ac_shader_config *config, struct ac_shader_config *other)
{
   config->num_sgprs = MAX2(config->num_sgprs, other->num_sgprs);
   config->num_vgprs = MAX2(config->num_vgprs, other->num_vgprs);
   config->num_shared_vgprs = MAX2(config->num_shared_vgprs, other->num_shared_vgprs);
   config->spilled_sgprs = MAX2(config->spilled_sgprs, other->spilled_sgprs);
   config->spilled_vgprs = MAX2(config->spilled_vgprs, other->spilled_vgprs);
   config->lds_size = MAX2(config->lds_size, other->lds_size);
   config->scratch_bytes_per_wave =
      MAX2(config->scratch_bytes_per_wave, other->scratch_bytes_per_wave);

   assert(config->float_mode == other->float_mode);
}

static void
postprocess_rt_config(struct ac_shader_config *config, enum amd_gfx_level gfx_level,
                      unsigned wave_size)
{
   config->rsrc1 = (config->rsrc1 & C_00B848_VGPRS) |
                   S_00B848_VGPRS((config->num_vgprs - 1) / (wave_size == 32 ? 8 : 4));
   if (gfx_level < GFX10)
      config->rsrc1 =
         (config->rsrc1 & C_00B848_SGPRS) | S_00B848_SGPRS((config->num_sgprs - 1) / 8);

   config->rsrc2 = (config->rsrc2 & C_00B84C_LDS_SIZE) | S_00B84C_LDS_SIZE(config->lds_size);
   config->rsrc3 = (config->rsrc3 & C_00B8A0_SHARED_VGPR_CNT) |
                   S_00B8A0_SHARED_VGPR_CNT(config->num_shared_vgprs / 8);
}

static VkResult
radv_rt_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                        const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(vk_pipeline_cache, cache, _cache);
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   VkResult result;
   struct radv_ray_tracing_pipeline *rt_pipeline = NULL;
   uint8_t hash[20];
   nir_shader *shader = NULL;
   bool keep_statistic_info = radv_pipeline_capture_shader_stats(device, pCreateInfo->flags);

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
      return radv_rt_pipeline_library_create(_device, _cache, pCreateInfo, pAllocator, pPipeline);

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);
   if (!local_create_info.pStages || !local_create_info.pGroups)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   struct vk_shader_module module = {.base.type = VK_OBJECT_TYPE_SHADER_MODULE};

   VkPipelineShaderStageCreateInfo stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = NULL,
      .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
      .module = vk_shader_module_to_handle(&module),
      .pName = "main",
   };
   VkPipelineCreateFlags flags =
      pCreateInfo->flags | VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

   size_t pipeline_size =
      sizeof(*rt_pipeline) + local_create_info.groupCount * sizeof(struct radv_ray_tracing_module);
   rt_pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, pipeline_size, 8,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (rt_pipeline == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   radv_pipeline_init(device, &rt_pipeline->base.base, RADV_PIPELINE_RAY_TRACING);
   rt_pipeline->group_count = local_create_info.groupCount;

   result = radv_create_group_handles(device, &local_create_info, rt_pipeline->groups);
   if (result != VK_SUCCESS)
      goto pipeline_fail;

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   struct radv_pipeline_key key = radv_generate_rt_pipeline_key(device, rt_pipeline, pCreateInfo->flags);

   radv_hash_rt_shaders(hash, &local_create_info, &key, rt_pipeline->groups,
                        radv_get_hash_flags(device, keep_statistic_info));

   /* First check if we can get things from the cache before we take the expensive step of
    * generating the nir. */
   result = radv_rt_pipeline_compile(rt_pipeline, pipeline_layout, device, cache, &key, &stage,
                                     flags, hash, creation_feedback, rt_pipeline->groups,
                                     rt_pipeline->group_count);

   if (result != VK_SUCCESS && result != VK_PIPELINE_COMPILE_REQUIRED)
      goto pipeline_fail;

   if (result == VK_PIPELINE_COMPILE_REQUIRED) {
      if (pCreateInfo->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
         goto pipeline_fail;

      shader = create_rt_shader(device, &local_create_info, rt_pipeline->groups, &key);
      module.nir = shader;
      result = radv_rt_pipeline_compile(rt_pipeline, pipeline_layout, device, cache, &key, &stage,
                                        pCreateInfo->flags, hash, creation_feedback,
                                        rt_pipeline->groups, rt_pipeline->group_count);
      if (result != VK_SUCCESS)
         goto shader_fail;
   }

   rt_pipeline->stack_size = compute_rt_stack_size(&local_create_info, rt_pipeline->groups);
   rt_pipeline->base.base.shaders[MESA_SHADER_COMPUTE] = radv_create_rt_prolog(device);

   combine_config(&rt_pipeline->base.base.shaders[MESA_SHADER_COMPUTE]->config,
                  &rt_pipeline->base.base.shaders[MESA_SHADER_RAYGEN]->config);

   postprocess_rt_config(&rt_pipeline->base.base.shaders[MESA_SHADER_COMPUTE]->config,
                         device->physical_device->rad_info.gfx_level,
                         device->physical_device->rt_wave_size);

   radv_compute_pipeline_init(device, &rt_pipeline->base, pipeline_layout);

   radv_rmv_log_compute_pipeline_create(device, pCreateInfo->flags, &rt_pipeline->base.base, false);

   *pPipeline = radv_pipeline_to_handle(&rt_pipeline->base.base);
shader_fail:
   ralloc_free(shader);
pipeline_fail:
   if (result != VK_SUCCESS)
      radv_pipeline_destroy(device, &rt_pipeline->base.base, pAllocator);
fail:
   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return result;
}

void
radv_destroy_ray_tracing_pipeline(struct radv_device *device,
                                  struct radv_ray_tracing_pipeline *pipeline)
{
   if (pipeline->base.base.shaders[MESA_SHADER_COMPUTE])
      radv_shader_unref(device, pipeline->base.base.shaders[MESA_SHADER_COMPUTE]);
   if (pipeline->base.base.shaders[MESA_SHADER_RAYGEN])
      radv_shader_unref(device, pipeline->base.base.shaders[MESA_SHADER_RAYGEN]);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateRayTracingPipelinesKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                  VkPipelineCache pipelineCache, uint32_t count,
                                  const VkRayTracingPipelineCreateInfoKHR *pCreateInfos,
                                  const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = radv_rt_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator,
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

   if (result != VK_SUCCESS)
      return result;

   /* Work around Portal RTX not handling VK_OPERATION_NOT_DEFERRED_KHR correctly. */
   if (deferredOperation != VK_NULL_HANDLE)
      return VK_OPERATION_DEFERRED_KHR;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline _pipeline, uint32_t firstGroup,
                                        uint32_t groupCount, size_t dataSize, void *pData)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_module *groups;
   if (pipeline->type == RADV_PIPELINE_RAY_TRACING_LIB) {
      groups = radv_pipeline_to_ray_tracing_lib(pipeline)->groups;
   } else {
      groups = radv_pipeline_to_ray_tracing(pipeline)->groups;
   }
   char *data = pData;

   STATIC_ASSERT(sizeof(struct radv_pipeline_group_handle) <= RADV_RT_HANDLE_SIZE);

   memset(data, 0, groupCount * RADV_RT_HANDLE_SIZE);

   for (uint32_t i = 0; i < groupCount; ++i) {
      memcpy(data + i * RADV_RT_HANDLE_SIZE, &groups[firstGroup + i].handle,
             sizeof(struct radv_pipeline_group_handle));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL
radv_GetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline _pipeline, uint32_t group,
                                          VkShaderGroupShaderKHR groupShader)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   const struct radv_pipeline_shader_stack_size *stack_size =
      &rt_pipeline->groups[group].stack_size;

   if (groupShader == VK_SHADER_GROUP_SHADER_ANY_HIT_KHR ||
       groupShader == VK_SHADER_GROUP_SHADER_INTERSECTION_KHR)
      return stack_size->non_recursive_size;
   else
      return stack_size->recursive_size;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline,
                                                     uint32_t firstGroup, uint32_t groupCount,
                                                     size_t dataSize, void *pData)
{
   return radv_GetRayTracingShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount,
                                                  dataSize, pData);
}
