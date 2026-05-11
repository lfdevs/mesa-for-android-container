/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "genxml/genX_bits.h"

#include "anv_private.h"

enum anv_dgc_stage
anv_vk_stage_to_dgc_stage(VkShaderStageFlags vk_stage)
{
   switch (vk_stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
      return ANV_DGC_STAGE_VERTEX;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      return ANV_DGC_STAGE_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      return ANV_DGC_STAGE_TESS_EVAL;
   case VK_SHADER_STAGE_GEOMETRY_BIT:
      return ANV_DGC_STAGE_GEOMETRY;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return ANV_DGC_STAGE_FRAGMENT;
   case VK_SHADER_STAGE_TASK_BIT_EXT:
      return ANV_DGC_STAGE_TASK;
   case VK_SHADER_STAGE_MESH_BIT_EXT:
      return ANV_DGC_STAGE_MESH;
   case VK_SHADER_STAGE_COMPUTE_BIT:
      return ANV_DGC_STAGE_COMPUTE;
   default:
      UNREACHABLE("Unhandled stage");
   }
}

uint32_t
anv_vk_stages_to_generated_stages(VkShaderStageFlags vk_stages)
{
   uint32_t gen_stages = 0;
   anv_foreach_vk_stage(stage, vk_stages)
      gen_stages |= BITFIELD_BIT(anv_vk_stage_to_dgc_stage(stage));
   return gen_stages;
}

void
anv_write_gfx_indirect_descriptor(struct anv_device *device,
                                  struct anv_dgc_gfx_descriptor *descriptor,
                                  struct anv_cmd_graphics_state *gfx)
{
   struct anv_dgc_push_stage_state empty_push = {};

   if (intel_needs_workaround(device->info, 16011107343) &&
       gfx->shaders[MESA_SHADER_TESS_CTRL] != NULL) {
      memcpy(&descriptor->final_commands[descriptor->final_commands_size],
             gfx->dyn_state.packed.hs,
             _3DSTATE_HS_length(device->info) * 4);
      descriptor->final_commands_size += _3DSTATE_HS_length(device->info) * 4;
   }

   if (intel_needs_workaround(device->info, 22018402687) &&
       gfx->shaders[MESA_SHADER_TESS_EVAL] != NULL) {
      memcpy(&descriptor->final_commands[descriptor->final_commands_size],
             gfx->dyn_state.packed.ds,
             _3DSTATE_DS_length(device->info) * 4);
      descriptor->final_commands_size += _3DSTATE_DS_length(device->info) * 4;
   }
   assert(descriptor->final_commands_size <= sizeof(descriptor->final_commands));

   if (device->info->ver == 9) {
      const struct brw_vs_prog_data *vs_prog_data = get_gfx_vs_prog_data(gfx);

      descriptor->draw_params =
         ((vs_prog_data->uses_firstvertex || vs_prog_data->uses_baseinstance) ?
          ANV_DGC_DRAW_PARAM_BASE_INSTANCE_VERTEX : 0) |
         (vs_prog_data->uses_drawid ? ANV_DGC_DRAW_PARAM_DRAW_ID : 0);
   }

   anv_foreach_vk_stage(vk_stage, ANV_GRAPHICS_STAGE_BITS) {
      enum anv_dgc_stage gen_stage = anv_vk_stage_to_dgc_stage(vk_stage);
      enum mesa_shader_stage stage = vk_to_mesa_shader_stage(vk_stage);

      if ((gfx->active_stages & vk_stage) == 0) {
         descriptor->push_constants.stages[gen_stage] = empty_push;
         continue;
      }

      const struct anv_pipeline_bind_map *bind_map =
         &gfx->shaders[stage]->bind_map;
      if ((bind_map->push_ranges[0].length == 0 ||
           bind_map->push_ranges[0].set != ANV_DESCRIPTOR_SET_PUSH_CONSTANTS) &&
          bind_map->inline_dwords_count == 0) {
         descriptor->push_constants.stages[gen_stage] = empty_push;
         continue;
      }

      if (stage == MESA_SHADER_MESH &&
          intel_needs_workaround(device->info, 18019110168)) {
         const struct brw_mesh_prog_data *mesh_prog_data = get_gfx_mesh_prog_data(gfx);
         descriptor->wa_18019110168_remapping_table_offset =
            gfx->shaders[MESA_SHADER_MESH]->kernel.offset +
            mesh_prog_data->wa_18019110168_mapping_offset;
      }

      if (stage == MESA_SHADER_MESH || stage == MESA_SHADER_TASK) {
         descriptor->push_constants.stages[gen_stage].bindless.inline_dwords_count =
            bind_map->inline_dwords_count;
         assert(sizeof(bind_map->inline_dwords) ==
                sizeof(descriptor->push_constants.stages[gen_stage].bindless.inline_dwords));
         memcpy(descriptor->push_constants.stages[gen_stage].bindless.inline_dwords,
                bind_map->inline_dwords, sizeof(bind_map->inline_dwords));
      } else {
         for (uint32_t i = 0; i < ARRAY_SIZE(bind_map->push_ranges); i++) {
            const struct anv_push_range *range = &bind_map->push_ranges[i];
            if (range->length == 0)
               break;

            /* We should have compiler all the indirectly bindable shaders in
             * such a way that it's the only types of push constants we should
             * see.
             */
            assert(range->set == ANV_DESCRIPTOR_SET_PUSH_CONSTANTS ||
                   range->set == ANV_DESCRIPTOR_SET_DESCRIPTORS ||
                   range->set == ANV_DESCRIPTOR_SET_NULL ||
                   range->set == ANV_DESCRIPTOR_SET_PER_PRIM_PADDING);

            struct anv_dgc_push_stage_slot *slot =
               &descriptor->push_constants.stages[gen_stage].legacy.slots[i];

            slot->push_data_size = 32 * range->length;

            slot->push_data_offset = 32 * range->start;
            slot->type = ANV_DGC_PUSH_SLOT_TYPE_PUSH_CONSTANTS;
            descriptor->push_constants.stages[gen_stage].legacy.n_slots++;
         }
      }
      descriptor->push_constants.active_stages |= 1u << gen_stage;
   }
}

static void
write_cs_set_entry(struct anv_device *device,
                   struct anv_indirect_execution_set *indirect_set,
                   uint32_t entry, struct anv_shader *shader)
{
   struct anv_dgc_cs_descriptor descriptor;
   anv_genX(device->info, write_cs_descriptor)(&descriptor, device, shader);

   const struct brw_cs_prog_data *prog_data =
      brw_cs_prog_data_const(shader->prog_data);

   if (device->info->verx10 < 125)
      anv_reloc_list_append(&indirect_set->relocs, &shader->relocs);

   memcpy(indirect_set->bo->map + entry * indirect_set->stride,
          &descriptor, sizeof(descriptor));

   indirect_set->uses_systolic |= prog_data->uses_systolic;
   indirect_set->max_scratch = MAX2(indirect_set->max_scratch,
                                    prog_data->base.total_scratch);
   indirect_set->max_ray_queries = MAX2(indirect_set->max_ray_queries,
                                        shader->prog_data->ray_queries);
}

static void
write_rt_set_entry(struct anv_indirect_execution_set *indirect_set,
                   uint32_t entry, struct vk_pipeline *pipeline)
{
   indirect_set->max_scratch = MAX2(indirect_set->max_scratch,
                                    vk_pipeline_get_rt_scratch_size(pipeline));
   indirect_set->max_ray_queries = MAX2(indirect_set->max_ray_queries,
                                        vk_pipeline_get_rt_ray_queries(pipeline));
}

VkResult anv_CreateIndirectExecutionSetEXT(
   VkDevice                                    _device,
   const VkIndirectExecutionSetCreateInfoEXT*  pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkIndirectExecutionSetEXT*                  pIndirectExecutionSet)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   struct anv_indirect_execution_set *indirect_set =
      vk_object_zalloc(&device->vk, pAllocator,
                       sizeof(struct anv_indirect_execution_set),
                       VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT);
   if (indirect_set == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      anv_reloc_list_init(&indirect_set->relocs,
                          pAllocator ? pAllocator : &device->vk.alloc,
                          device->physical->uses_relocs);
   if (result != VK_SUCCESS)
      goto fail_object;

   struct vk_pipeline *vk_pipeline = NULL;
   struct vk_shader *vk_shader = NULL;
   VkPipelineBindPoint bind_point;
   uint32_t entry_count;
   if (pCreateInfo->type == VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT) {
      entry_count = pCreateInfo->info.pPipelineInfo->maxPipelineCount;
      vk_pipeline =
         vk_pipeline_from_handle(pCreateInfo->info.pPipelineInfo->initialPipeline);
      bind_point = vk_pipeline->bind_point;
      if (vk_pipeline->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE)
         vk_shader = vk_pipeline_get_shader(vk_pipeline, MESA_SHADER_COMPUTE);
   } else {
      entry_count = pCreateInfo->info.pShaderInfo->maxShaderCount;
      vk_shader =
         vk_shader_from_handle(pCreateInfo->info.pShaderInfo->pInitialShaders[0]);
      bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
   }

   enum anv_bo_alloc_flags alloc_flags =
      ANV_BO_ALLOC_CAPTURE |
      ANV_BO_ALLOC_MAPPED |
      ANV_BO_ALLOC_HOST_CACHED_COHERENT;

   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_COMPUTE: {
      struct anv_shader *shader = container_of(vk_shader, struct anv_shader, vk);

      /* Alignment required for
       * MEDIA_INTERFACE_DESCRIPTOR_LOAD::InterfaceDescriptorDataStartAddress
       */
      STATIC_ASSERT(sizeof(struct anv_dgc_cs_descriptor) % 64 == 0);

      indirect_set->stride = sizeof(struct anv_dgc_cs_descriptor);

      uint32_t size = align(entry_count * indirect_set->stride, 4096);

      /* Generations up to Gfx12.0 have a structures describing the compute
       * shader that needs to live in the dynamic state heap.
       */
      if (device->info->verx10 <= 120)
         alloc_flags |= ANV_BO_ALLOC_DYNAMIC_VISIBLE_POOL;

      result = anv_device_alloc_bo(device, "indirect-exec-set", size,
                                   alloc_flags, 0 /* explicit_address */,
                                   &indirect_set->bo);
      if (result != VK_SUCCESS)
         goto fail_relocs;

      indirect_set->bind_map = anv_pipeline_bind_map_clone(
         device, pAllocator, &shader->bind_map);
      if (indirect_set->bind_map == NULL) {
         result = vk_errorf(device, VK_ERROR_OUT_OF_HOST_MEMORY,
                            "Fail to allocate bind map");
         goto fail_bo;
      }

      write_cs_set_entry(device, indirect_set, 0, shader);
      break;
   }

   case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR: {
      VK_FROM_HANDLE(vk_pipeline, pipeline,
                     pCreateInfo->info.pPipelineInfo->initialPipeline);
      write_rt_set_entry(indirect_set, 0, pipeline);
      break;
   }

   default:
      UNREACHABLE("Unsupported indirect pipeline type");
   }

   *pIndirectExecutionSet = anv_indirect_execution_set_to_handle(indirect_set);

   return VK_SUCCESS;

 fail_bo:
   anv_device_release_bo(device, indirect_set->bo);
 fail_relocs:
   anv_reloc_list_finish(&indirect_set->relocs);
 fail_object:
   vk_object_free(&device->vk, pAllocator, indirect_set);
   return result;
}

void anv_DestroyIndirectExecutionSetEXT(
   VkDevice                                    _device,
   VkIndirectExecutionSetEXT                   indirectExecutionSet,
   const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_indirect_execution_set, indirect_set, indirectExecutionSet);

   vk_free2(&device->vk.alloc, pAllocator, indirect_set->bind_map);
   anv_reloc_list_finish(&indirect_set->relocs);
   if (indirect_set->bo)
      anv_device_release_bo(device, indirect_set->bo);
   vk_object_free(&device->vk, pAllocator, indirect_set);
}

void anv_UpdateIndirectExecutionSetPipelineEXT(
   VkDevice                                    _device,
   VkIndirectExecutionSetEXT                   indirectExecutionSet,
   uint32_t                                    executionSetWriteCount,
   const VkWriteIndirectExecutionSetPipelineEXT* pExecutionSetWrites)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_indirect_execution_set, indirect_set, indirectExecutionSet);

   for (uint32_t i = 0; i < executionSetWriteCount; i++) {
      VK_FROM_HANDLE(vk_pipeline, pipeline, pExecutionSetWrites[i].pipeline);

      switch (pipeline->bind_point) {
      case VK_PIPELINE_BIND_POINT_COMPUTE: {
         struct vk_shader *vk_shader =
            vk_pipeline_get_shader(pipeline, MESA_SHADER_COMPUTE);
         struct anv_shader *shader = container_of(vk_shader, struct anv_shader, vk);
         write_cs_set_entry(device, indirect_set,
                            pExecutionSetWrites[i].index, shader);
         break;
      }

      case VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR:
         write_rt_set_entry(indirect_set, pExecutionSetWrites[i].index, pipeline);
         break;

      default:
         UNREACHABLE("Unsupported indirect pipeline type");
      }
   }
}

void anv_UpdateIndirectExecutionSetShaderEXT(
   VkDevice                                    _device,
   VkIndirectExecutionSetEXT                   indirectExecutionSet,
   uint32_t                                    executionSetWriteCount,
   const VkWriteIndirectExecutionSetShaderEXT* pExecutionSetWrites)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_indirect_execution_set, indirect_set, indirectExecutionSet);

   for (uint32_t i = 0; i < executionSetWriteCount; i++) {
      VK_FROM_HANDLE(vk_shader, vk_shader, pExecutionSetWrites[i].shader);
      assert(vk_shader->stage == MESA_SHADER_COMPUTE);
      struct anv_shader *shader = container_of(vk_shader, struct anv_shader, vk);
      write_cs_set_entry(device, indirect_set,
                         pExecutionSetWrites[i].index, shader);
   }
}
