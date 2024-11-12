/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_indirect_execution_set.h"

#include "nvk_cmd_buffer.h"
#include "nvk_entrypoints.h"
#include "nvk_device.h"
#include "nvk_shader.h"
#include "vk_pipeline.h"

static void *
nvk_ies_map(struct nvk_indirect_execution_set *ies, uint32_t index)
{
   assert(index < ies->count);
   return ies->mem->map + (index * (size_t)ies->stride_B);
}

void
nvk_ies_cs_qmd_init(struct nvk_physical_device *pdev,
                    struct nvk_ies_cs_qmd *qmd,
                    struct nvk_shader *shader)
{
   struct nak_qmd_info qmd_info = {
      .addr = shader->hdr_addr,
      .smem_size = shader->info.cs.smem_size,
      .smem_max = NVK_MAX_SHARED_SIZE,
   };

   assert(shader->cbuf_map.cbuf_count <= ARRAY_SIZE(qmd_info.cbufs));
   for (uint32_t c = 0; c < shader->cbuf_map.cbuf_count; c++) {
      const struct nvk_cbuf *cbuf = &shader->cbuf_map.cbufs[c];
      switch (cbuf->type) {
      case NVK_CBUF_TYPE_ROOT_DESC:
         /* This one gets patched with the actual address */
         assert(c == 0);
         qmd_info.cbufs[qmd_info.num_cbufs++] = (struct nak_qmd_cbuf) {
            .index = c,
            .addr = 0xc0ffee000,
            .size = sizeof(struct nvk_root_descriptor_table),
         };
         break;

      case NVK_CBUF_TYPE_SHADER_DATA:
         qmd_info.cbufs[qmd_info.num_cbufs++] = (struct nak_qmd_cbuf) {
            .index = c,
            .addr = shader->data_addr,
            .size = shader->data_size,
         };
         break;

      default:
         unreachable("Unsupported cbuf type");
      }
   }

   nak_fill_qmd(&pdev->info, &shader->info, &qmd_info,
                qmd->qmd, sizeof(qmd->qmd));
}

static void
nvk_ies_set_cs(struct nvk_device *dev,
               struct nvk_indirect_execution_set *ies,
               uint32_t index,
               struct nvk_shader *shader)
{
   struct nvk_ies_cs_qmd qmd = {};
   nvk_ies_cs_qmd_init(nvk_device_physical(dev), &qmd, shader);

   assert(sizeof(qmd) <= ies->stride_B);
   memcpy(nvk_ies_map(ies, index), &qmd, sizeof(qmd));
}

uint16_t
nvk_ies_gfx_pipeline_max_dw_count(struct nvk_physical_device *pdev,
                                  VkShaderStageFlags stages)
{
   gl_shader_stage last_vtgm = MESA_SHADER_VERTEX;
   u_foreach_bit(s, stages) {
      gl_shader_stage stage = vk_to_mesa_shader_stage(1 << s);
      if (stage != MESA_SHADER_FRAGMENT)
         last_vtgm = stage;
   }

   uint16_t push_dw = 0;
   u_foreach_bit(s, stages) {
      gl_shader_stage stage = vk_to_mesa_shader_stage(1 << s);
      push_dw += nvk_max_shader_push_dw(pdev, stage, stage == last_vtgm);
   }

   return push_dw;
}

static uint32_t
nvk_ies_stride_gfx_pipeline(struct nvk_physical_device *pdev,
                            VkShaderStageFlags stages)
{
   return sizeof(struct nvk_ies_gfx_pipeline) +
          (4 * nvk_ies_gfx_pipeline_max_dw_count(pdev, stages));
}

static void
nvk_ies_set_gfx_pipeline(struct nvk_device *dev,
                         struct nvk_indirect_execution_set *ies,
                         uint32_t index,
                         struct vk_pipeline *pipeline)
{
   gl_shader_stage last_vtgm = MESA_SHADER_VERTEX;
   struct nvk_shader *type_shader[6] = {};
   u_foreach_bit(s, pipeline->stages) {
      gl_shader_stage stage = vk_to_mesa_shader_stage(1 << s);
      struct vk_shader *vk_shader = vk_pipeline_get_shader(pipeline, stage);
      struct nvk_shader *shader =
         container_of(vk_shader, struct nvk_shader, vk);

      assert(shader->info.stage == stage);

      if (stage != MESA_SHADER_FRAGMENT)
         last_vtgm = stage;

      uint32_t type = mesa_to_nv9097_shader_type(stage);
      type_shader[type] = shader;
   }

   void *map = nvk_ies_map(ies, index);

   uint16_t dw_count = 0;
   for (uint32_t i = 0; i < ARRAY_SIZE(type_shader); i++) {
      if (type_shader[i] == NULL)
         continue;

      const uint16_t s_dw_count = type_shader[i]->info.stage == last_vtgm
                                  ? type_shader[i]->vtgm_push_dw_count
                                  : type_shader[i]->push_dw_count;
      memcpy(map + sizeof(struct nvk_ies_gfx_pipeline) + dw_count * 4,
             type_shader[i]->push_dw, s_dw_count * 4);
      dw_count += s_dw_count;
   }

   struct nvk_ies_gfx_pipeline hdr = {
      .dw_count = dw_count,
   };
   memcpy(map, &hdr, sizeof(hdr));
}

uint16_t
nvk_ies_gfx_shader_max_dw_count(struct nvk_physical_device *pdev,
                                VkShaderStageFlags stages,
                                bool last_vtgm)
{
   /* Each entry is a single shader so take the max */
   uint16_t max_push_dw = 0;
   u_foreach_bit(s, stages) {
      gl_shader_stage stage = vk_to_mesa_shader_stage(1 << s);
      uint16_t push_dw = nvk_max_shader_push_dw(pdev, stage, last_vtgm);
      max_push_dw = MAX2(max_push_dw, push_dw);
   }

   return max_push_dw;
}

static uint32_t
nvk_ies_stride_gfx_shader(struct nvk_physical_device *pdev,
                          VkShaderStageFlags stages)
{
   return sizeof(struct nvk_ies_gfx_shader) +
          (4 * nvk_ies_gfx_shader_max_dw_count(pdev, stages, true));
}

static void
nvk_ies_set_gfx_shader(struct nvk_device *dev,
                       struct nvk_indirect_execution_set *ies,
                       uint32_t index,
                       struct nvk_shader *shader)
{
   struct nvk_ies_gfx_shader hdr = {
      .dw_count = shader->push_dw_count,
      .vtgm_dw_count = shader->vtgm_push_dw_count,
   };

   void *map = nvk_ies_map(ies, index);
   memcpy(map, &hdr, sizeof(hdr));
   memcpy(map + sizeof(hdr), shader->push_dw,
          4 * MAX2(shader->push_dw_count, shader->vtgm_push_dw_count));
}

static void
nvk_ies_set_pipeline(struct nvk_device *dev,
                     struct nvk_indirect_execution_set *ies,
                     uint32_t index, struct vk_pipeline *pipeline)
{
   switch (ies->type) {
   case NVK_IES_TYPE_CS_QMD: {
      struct vk_shader *vk_shader =
         vk_pipeline_get_shader(pipeline, MESA_SHADER_COMPUTE);
      struct nvk_shader *shader =
         container_of(vk_shader, struct nvk_shader, vk);
      nvk_ies_set_cs(dev, ies, index, shader);
      break;
   }

   case NVK_IES_TYPE_GFX_PIPELINE: {
      nvk_ies_set_gfx_pipeline(dev, ies, index, pipeline);
      break;
   }

   default:
      unreachable("Invalid indirect execution set type");
   }
}

static void
nvk_ies_set_shader(struct nvk_device *dev,
                   struct nvk_indirect_execution_set *ies,
                   uint32_t index, struct nvk_shader *shader)
{
   switch (ies->type) {
   case NVK_IES_TYPE_CS_QMD: {
      nvk_ies_set_cs(dev, ies, index, shader);
      break;
   }

   case NVK_IES_TYPE_GFX_SHADER: {
      nvk_ies_set_gfx_shader(dev, ies, index, shader);
      break;
   }

   default:
      unreachable("Invalid indirect execution set type");
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateIndirectExecutionSetEXT(VkDevice _device,
                                  const VkIndirectExecutionSetCreateInfoEXT *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator,
                                  VkIndirectExecutionSetEXT *pIndirectExecutionSet)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   struct nvk_indirect_execution_set *ies =
      vk_object_zalloc(&dev->vk, pAllocator, sizeof(*ies),
                       VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT);
   if (ies == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   switch (pCreateInfo->type) {
   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT: {
      VK_FROM_HANDLE(vk_pipeline, pipeline,
                     pCreateInfo->info.pPipelineInfo->initialPipeline);

      ies->count = pCreateInfo->info.pPipelineInfo->maxPipelineCount;
      if (pipeline->stages & VK_SHADER_STAGE_COMPUTE_BIT) {
         assert(pipeline->stages == VK_SHADER_STAGE_COMPUTE_BIT);
         ies->type = NVK_IES_TYPE_CS_QMD;
         ies->stride_B = sizeof(struct nvk_ies_cs_qmd);
      } else if (pipeline->stages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
         assert(!(pipeline->stages & ~NVK_SHADER_STAGE_GRAPHICS_BITS));
         ies->type = NVK_IES_TYPE_GFX_PIPELINE;
         ies->stride_B = nvk_ies_stride_gfx_pipeline(pdev, pipeline->stages);
      } else {
         unreachable("Unknown shader stage");
      }
      break;
   }

   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT: {
      const VkIndirectExecutionSetShaderInfoEXT *info =
         pCreateInfo->info.pShaderInfo;

      VkShaderStageFlags stages = 0;
      for (uint32_t i = 0; i < info->shaderCount; i++) {
         VK_FROM_HANDLE(nvk_shader, shader, info->pInitialShaders[i]);
         stages |= mesa_to_vk_shader_stage(shader->vk.stage);
      }

      ies->count = info->maxShaderCount;
      if (stages & VK_SHADER_STAGE_COMPUTE_BIT) {
         assert(stages == VK_SHADER_STAGE_COMPUTE_BIT);
         ies->type = NVK_IES_TYPE_CS_QMD;
         ies->stride_B = sizeof(struct nvk_ies_cs_qmd);
      } else if (stages & NVK_SHADER_STAGE_GRAPHICS_BITS) {
         assert(!(stages & ~NVK_SHADER_STAGE_GRAPHICS_BITS));
         ies->type = NVK_IES_TYPE_GFX_SHADER;
         ies->stride_B = nvk_ies_stride_gfx_shader(pdev, stages);
      } else {
         unreachable("Unknown shader stage");
      }
      break;
   }

   default:
      unreachable("Unknown indirect execution set info type");
   }

   size_t size = ies->count * (size_t)ies->stride_B;
   result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &dev->vk.base,
                                       size, 0, NVKMD_MEM_LOCAL,
                                       NVKMD_MEM_MAP_WR, &ies->mem);
   if (result != VK_SUCCESS) {
      vk_object_free(&dev->vk, pAllocator, ies);
      return result;
   }

   switch (pCreateInfo->type) {
   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT: {
      VK_FROM_HANDLE(vk_pipeline, pipeline,
                     pCreateInfo->info.pPipelineInfo->initialPipeline);
      nvk_ies_set_pipeline(dev, ies, 0, pipeline);
      break;
   }

   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT: {
      const VkIndirectExecutionSetShaderInfoEXT *info =
         pCreateInfo->info.pShaderInfo;

      for (uint32_t i = 0; i < info->shaderCount; i++) {
         VK_FROM_HANDLE(nvk_shader, shader, info->pInitialShaders[i]);
         nvk_ies_set_shader(dev, ies, i, shader);
      }
      break;
   }

   default:
      unreachable("Unknown indirect execution set info type");
   }

   *pIndirectExecutionSet = nvk_indirect_execution_set_to_handle(ies);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyIndirectExecutionSetEXT(VkDevice _device,
                                   VkIndirectExecutionSetEXT indirectExecutionSet,
                                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_indirect_execution_set, ies, indirectExecutionSet);

   if (ies == NULL)
      return;

   nvkmd_mem_unref(ies->mem);

   vk_object_free(&dev->vk, pAllocator, ies);
}

VKAPI_ATTR void VKAPI_CALL
nvk_UpdateIndirectExecutionSetPipelineEXT(
    VkDevice _device,
    VkIndirectExecutionSetEXT indirectExecutionSet,
    uint32_t executionSetWriteCount,
    const VkWriteIndirectExecutionSetPipelineEXT *pExecutionSetWrites)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_indirect_execution_set, ies, indirectExecutionSet);

   for (uint32_t i = 0; i < executionSetWriteCount; i++) {
      VK_FROM_HANDLE(vk_pipeline, pipeline, pExecutionSetWrites[i].pipeline);
      nvk_ies_set_pipeline(dev, ies, pExecutionSetWrites[i].index, pipeline);
   }
}

VKAPI_ATTR void VKAPI_CALL
nvk_UpdateIndirectExecutionSetShaderEXT(
    VkDevice _device,
    VkIndirectExecutionSetEXT indirectExecutionSet,
    uint32_t executionSetWriteCount,
    const VkWriteIndirectExecutionSetShaderEXT *pExecutionSetWrites)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_indirect_execution_set, ies, indirectExecutionSet);

   for (uint32_t i = 0; i < executionSetWriteCount; i++) {
      VK_FROM_HANDLE(nvk_shader, shader, pExecutionSetWrites[i].shader);
      nvk_ies_set_shader(dev, ies, pExecutionSetWrites[i].index, shader);
   }
}
