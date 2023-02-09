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

static VkRayTracingPipelineCreateInfoKHR
radv_create_merged_rt_create_info(const VkRayTracingPipelineCreateInfoKHR *pCreateInfo)
{
   VkRayTracingPipelineCreateInfoKHR local_create_info = *pCreateInfo;
   uint32_t total_stages = pCreateInfo->stageCount;
   uint32_t total_groups = pCreateInfo->groupCount;

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_library_pipeline *library_pipeline = radv_pipeline_to_library(pipeline);

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
   local_create_info.pGroups = groups =
      malloc(sizeof(VkRayTracingShaderGroupCreateInfoKHR) * total_groups);
   if (!local_create_info.pStages || !local_create_info.pGroups)
      return local_create_info;

   total_stages = pCreateInfo->stageCount;
   total_groups = pCreateInfo->groupCount;
   for (unsigned j = 0; j < pCreateInfo->stageCount; ++j)
      stages[j] = pCreateInfo->pStages[j];
   for (unsigned j = 0; j < pCreateInfo->groupCount; ++j)
      groups[j] = pCreateInfo->pGroups[j];

   if (pCreateInfo->pLibraryInfo) {
      for (unsigned i = 0; i < pCreateInfo->pLibraryInfo->libraryCount; ++i) {
         RADV_FROM_HANDLE(radv_pipeline, pipeline, pCreateInfo->pLibraryInfo->pLibraries[i]);
         struct radv_library_pipeline *library_pipeline = radv_pipeline_to_library(pipeline);

         for (unsigned j = 0; j < library_pipeline->stage_count; ++j)
            stages[total_stages + j] = library_pipeline->stages[j];
         for (unsigned j = 0; j < library_pipeline->group_count; ++j) {
            VkRayTracingShaderGroupCreateInfoKHR *dst = &groups[total_groups + j];
            *dst = library_pipeline->groups[j];
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

static void
vk_shader_module_finish(void *_module)
{
   struct vk_shader_module *module = _module;
   vk_object_base_finish(&module->base);
}

static VkResult
radv_rt_pipeline_library_create(VkDevice _device, VkPipelineCache _cache,
                                const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_library_pipeline *pipeline;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_LIBRARY);

   pipeline->ctx = ralloc_context(NULL);

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);
   if (!local_create_info.pStages || !local_create_info.pGroups)
      goto fail;

   if (local_create_info.stageCount) {
      pipeline->stage_count = local_create_info.stageCount;

      size_t size = sizeof(VkPipelineShaderStageCreateInfo) * local_create_info.stageCount;
      pipeline->stages = ralloc_size(pipeline->ctx, size);
      if (!pipeline->stages)
         goto fail;

      memcpy(pipeline->stages, local_create_info.pStages, size);

      pipeline->hashes =
         ralloc_size(pipeline->ctx, sizeof(*pipeline->hashes) * local_create_info.stageCount);
      if (!pipeline->hashes)
         goto fail;

      pipeline->identifiers =
         ralloc_size(pipeline->ctx, sizeof(*pipeline->identifiers) * local_create_info.stageCount);
      if (!pipeline->identifiers)
         goto fail;

      for (uint32_t i = 0; i < local_create_info.stageCount; i++) {
         RADV_FROM_HANDLE(vk_shader_module, module, pipeline->stages[i].module);

         const VkPipelineShaderStageModuleIdentifierCreateInfoEXT *iinfo =
            vk_find_struct_const(local_create_info.pStages[i].pNext,
                                 PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT);

         if (module) {
            struct vk_shader_module *new_module =
               ralloc_size(pipeline->ctx, sizeof(struct vk_shader_module) + module->size);
            if (!new_module)
               goto fail;

            ralloc_set_destructor(new_module, vk_shader_module_finish);
            vk_object_base_init(&device->vk, &new_module->base, VK_OBJECT_TYPE_SHADER_MODULE);

            new_module->nir = NULL;
            memcpy(new_module->sha1, module->sha1, sizeof(module->sha1));
            new_module->size = module->size;
            memcpy(new_module->data, module->data, module->size);

            const VkSpecializationInfo *spec = pipeline->stages[i].pSpecializationInfo;
            if (spec) {
               VkSpecializationInfo *new_spec = ralloc(pipeline->ctx, VkSpecializationInfo);
               if (!new_spec)
                  goto fail;

               new_spec->mapEntryCount = spec->mapEntryCount;
               uint32_t map_entries_size = sizeof(VkSpecializationMapEntry) * spec->mapEntryCount;
               new_spec->pMapEntries = ralloc_size(pipeline->ctx, map_entries_size);
               if (!new_spec->pMapEntries)
                  goto fail;
               memcpy((void *)new_spec->pMapEntries, spec->pMapEntries, map_entries_size);

               new_spec->dataSize = spec->dataSize;
               new_spec->pData = ralloc_size(pipeline->ctx, spec->dataSize);
               if (!new_spec->pData)
                  goto fail;
               memcpy((void *)new_spec->pData, spec->pData, spec->dataSize);

               pipeline->stages[i].pSpecializationInfo = new_spec;
            }

            pipeline->stages[i].module = vk_shader_module_to_handle(new_module);
            pipeline->stages[i].pName = ralloc_strdup(pipeline->ctx, pipeline->stages[i].pName);
            if (!pipeline->stages[i].pName)
               goto fail;
            pipeline->stages[i].pNext = NULL;
         } else {
            assert(iinfo);
            pipeline->identifiers[i].identifierSize =
               MIN2(iinfo->identifierSize, sizeof(pipeline->hashes[i].sha1));
            memcpy(pipeline->hashes[i].sha1, iinfo->pIdentifier,
                   pipeline->identifiers[i].identifierSize);
            pipeline->stages[i].module = VK_NULL_HANDLE;
            pipeline->stages[i].pNext = &pipeline->identifiers[i];
            pipeline->identifiers[i].sType =
               VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_MODULE_IDENTIFIER_CREATE_INFO_EXT;
            pipeline->identifiers[i].pNext = NULL;
            pipeline->identifiers[i].pIdentifier = pipeline->hashes[i].sha1;
         }
      }
   }

   if (local_create_info.groupCount) {
      size_t size = sizeof(VkRayTracingShaderGroupCreateInfoKHR) * local_create_info.groupCount;
      pipeline->group_count = local_create_info.groupCount;
      pipeline->groups = ralloc_size(pipeline->ctx, size);
      if (!pipeline->groups)
         goto fail;
      memcpy(pipeline->groups, local_create_info.pGroups, size);
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);

   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return VK_SUCCESS;
fail:
   ralloc_free(pipeline->ctx);
   free((void *)local_create_info.pGroups);
   free((void *)local_create_info.pStages);
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

bool
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

static struct radv_pipeline_key
radv_generate_rt_pipeline_key(const struct radv_ray_tracing_pipeline *pipeline,
                              VkPipelineCreateFlags flags)
{
   struct radv_pipeline_key key = radv_generate_pipeline_key(&pipeline->base.base, flags);
   key.cs.compute_subgroup_size = pipeline->base.base.device->physical_device->rt_wave_size;

   return key;
}

static VkResult
radv_rt_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                        const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
   RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   VkResult result;
   struct radv_ray_tracing_pipeline *rt_pipeline = NULL;
   uint8_t hash[20];
   nir_shader *shader = NULL;
   bool keep_statistic_info =
      (pCreateInfo->flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR) ||
      (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS) || device->keep_shader_info;

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
      return radv_rt_pipeline_library_create(_device, _cache, pCreateInfo, pAllocator, pPipeline);

   VkRayTracingPipelineCreateInfoKHR local_create_info =
      radv_create_merged_rt_create_info(pCreateInfo);
   if (!local_create_info.pStages || !local_create_info.pGroups) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   struct vk_shader_module module = {.base.type = VK_OBJECT_TYPE_SHADER_MODULE};

   VkPipelineShaderStageCreateInfo stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = NULL,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_to_handle(&module),
      .pName = "main",
   };
   VkPipelineCreateFlags flags =
      pCreateInfo->flags | VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;

   rt_pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*rt_pipeline), 8,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (rt_pipeline == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   radv_pipeline_init(device, &rt_pipeline->base.base, RADV_PIPELINE_RAY_TRACING);
   rt_pipeline->group_count = local_create_info.groupCount;

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   struct radv_pipeline_key key = radv_generate_rt_pipeline_key(rt_pipeline, pCreateInfo->flags);
   UNUSED gl_shader_stage last_vgt_api_stage = MESA_SHADER_NONE;

   radv_hash_rt_shaders(hash, &local_create_info, &key,
                        radv_get_hash_flags(device, keep_statistic_info));

   /* First check if we can get things from the cache before we take the expensive step of
    * generating the nir. */
   result = radv_create_shaders(
      &rt_pipeline->base.base, pipeline_layout, device, cache, &key, &stage, 1, flags, hash,
      creation_feedback, &rt_pipeline->stack_sizes, &rt_pipeline->group_count, 0, &last_vgt_api_stage);

   if (result != VK_SUCCESS && result != VK_PIPELINE_COMPILE_REQUIRED)
      goto pipeline_fail;

   if (result == VK_PIPELINE_COMPILE_REQUIRED) {
      if (pCreateInfo->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
         goto pipeline_fail;

      rt_pipeline->stack_sizes =
         calloc(sizeof(*rt_pipeline->stack_sizes), local_create_info.groupCount);
      if (!rt_pipeline->stack_sizes) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto pipeline_fail;
      }

      shader = create_rt_shader(device, &local_create_info, rt_pipeline->stack_sizes, &key);
      module.nir = shader;
      result = radv_create_shaders(&rt_pipeline->base.base, pipeline_layout, device, cache, &key,
                                   &stage, 1, pCreateInfo->flags, hash, creation_feedback,
                                   &rt_pipeline->stack_sizes, &rt_pipeline->group_count, 0,
                                   &last_vgt_api_stage);
      if (result != VK_SUCCESS)
         goto shader_fail;
   }

   radv_compute_pipeline_init(&rt_pipeline->base, pipeline_layout);

   rt_pipeline->group_handles =
      calloc(sizeof(*rt_pipeline->group_handles), local_create_info.groupCount);
   if (!rt_pipeline->group_handles) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto shader_fail;
   }

   rt_pipeline->dynamic_stack_size = radv_rt_pipeline_has_dynamic_stack_size(pCreateInfo);

   /* For General and ClosestHit shaders, we can use the shader ID directly as handle.
    * As (potentially different) AnyHit shaders are inlined, for Intersection shaders
    * we use the Group ID.
    */
   for (unsigned i = 0; i < local_create_info.groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &local_create_info.pGroups[i];
      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR:
         if (group_info->generalShader != VK_SHADER_UNUSED_KHR)
            rt_pipeline->group_handles[i].general_index = group_info->generalShader + 2;
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            rt_pipeline->group_handles[i].closest_hit_index = group_info->closestHitShader + 2;
         if (group_info->intersectionShader != VK_SHADER_UNUSED_KHR)
            rt_pipeline->group_handles[i].intersection_index = i + 2;
         break;
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         if (group_info->closestHitShader != VK_SHADER_UNUSED_KHR)
            rt_pipeline->group_handles[i].closest_hit_index = group_info->closestHitShader + 2;
         if (group_info->anyHitShader != VK_SHADER_UNUSED_KHR)
            rt_pipeline->group_handles[i].any_hit_index = i + 2;
         break;
      case VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR:
         unreachable("VK_SHADER_GROUP_SHADER_MAX_ENUM_KHR");
      }

      if (pCreateInfo->flags &
          VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR) {
         if (group_info->pShaderGroupCaptureReplayHandle &&
             memcmp(group_info->pShaderGroupCaptureReplayHandle, &rt_pipeline->group_handles[i],
                    sizeof(rt_pipeline->group_handles[i])) != 0) {
            result = VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS;
            goto shader_fail;
         }
      }
   }

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

   RADV_FROM_HANDLE(radv_device, device, _device);
   for (uint32_t j = 0; j < count; ++j)
      radv_rmv_log_compute_pipeline_create(device, pCreateInfos[i].flags,
                                           radv_pipeline_from_handle(pPipelines[j]), false);

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
   struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   char *data = pData;

   STATIC_ASSERT(sizeof(*rt_pipeline->group_handles) <= RADV_RT_HANDLE_SIZE);

   memset(data, 0, groupCount * RADV_RT_HANDLE_SIZE);

   for (uint32_t i = 0; i < groupCount; ++i) {
      memcpy(data + i * RADV_RT_HANDLE_SIZE, &rt_pipeline->group_handles[firstGroup + i],
             sizeof(*rt_pipeline->group_handles));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkDeviceSize VKAPI_CALL
radv_GetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline _pipeline, uint32_t group,
                                          VkShaderGroupShaderKHR groupShader)
{
   RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
   struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   const struct radv_pipeline_shader_stack_size *stack_size = &rt_pipeline->stack_sizes[group];

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
