/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_pipeline.h"

#include "venus-protocol/vn_protocol_driver_pipeline.h"
#include "venus-protocol/vn_protocol_driver_pipeline_cache.h"
#include "venus-protocol/vn_protocol_driver_pipeline_layout.h"
#include "venus-protocol/vn_protocol_driver_shader_module.h"

#include "vn_device.h"
#include "vn_physical_device.h"

/* shader module commands */

VkResult
vn_CreateShaderModule(VkDevice device,
                      const VkShaderModuleCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkShaderModule *pShaderModule)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_shader_module *mod =
      vk_zalloc(alloc, sizeof(*mod), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!mod)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&mod->base, VK_OBJECT_TYPE_SHADER_MODULE, &dev->base);

   VkShaderModule mod_handle = vn_shader_module_to_handle(mod);
   vn_async_vkCreateShaderModule(dev->instance, device, pCreateInfo, NULL,
                                 &mod_handle);

   *pShaderModule = mod_handle;

   return VK_SUCCESS;
}

void
vn_DestroyShaderModule(VkDevice device,
                       VkShaderModule shaderModule,
                       const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_shader_module *mod = vn_shader_module_from_handle(shaderModule);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!mod)
      return;

   vn_async_vkDestroyShaderModule(dev->instance, device, shaderModule, NULL);

   vn_object_base_fini(&mod->base);
   vk_free(alloc, mod);
}

/* pipeline layout commands */

VkResult
vn_CreatePipelineLayout(VkDevice device,
                        const VkPipelineLayoutCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkPipelineLayout *pPipelineLayout)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_pipeline_layout *layout =
      vk_zalloc(alloc, sizeof(*layout), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!layout)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&layout->base, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                       &dev->base);

   VkPipelineLayout layout_handle = vn_pipeline_layout_to_handle(layout);
   vn_async_vkCreatePipelineLayout(dev->instance, device, pCreateInfo, NULL,
                                   &layout_handle);

   *pPipelineLayout = layout_handle;

   return VK_SUCCESS;
}

void
vn_DestroyPipelineLayout(VkDevice device,
                         VkPipelineLayout pipelineLayout,
                         const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_pipeline_layout *layout =
      vn_pipeline_layout_from_handle(pipelineLayout);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!layout)
      return;

   vn_async_vkDestroyPipelineLayout(dev->instance, device, pipelineLayout,
                                    NULL);

   vn_object_base_fini(&layout->base);
   vk_free(alloc, layout);
}

/* pipeline cache commands */

VkResult
vn_CreatePipelineCache(VkDevice device,
                       const VkPipelineCacheCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkPipelineCache *pPipelineCache)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_pipeline_cache *cache =
      vk_zalloc(alloc, sizeof(*cache), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cache)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&cache->base, VK_OBJECT_TYPE_PIPELINE_CACHE,
                       &dev->base);

   VkPipelineCacheCreateInfo local_create_info;
   if (pCreateInfo->initialDataSize) {
      const struct vk_pipeline_cache_header *header =
         pCreateInfo->pInitialData;

      local_create_info = *pCreateInfo;
      local_create_info.initialDataSize -= header->header_size;
      local_create_info.pInitialData += header->header_size;
      pCreateInfo = &local_create_info;
   }

   VkPipelineCache cache_handle = vn_pipeline_cache_to_handle(cache);
   vn_async_vkCreatePipelineCache(dev->instance, device, pCreateInfo, NULL,
                                  &cache_handle);

   *pPipelineCache = cache_handle;

   return VK_SUCCESS;
}

void
vn_DestroyPipelineCache(VkDevice device,
                        VkPipelineCache pipelineCache,
                        const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_pipeline_cache *cache =
      vn_pipeline_cache_from_handle(pipelineCache);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!cache)
      return;

   vn_async_vkDestroyPipelineCache(dev->instance, device, pipelineCache,
                                   NULL);

   vn_object_base_fini(&cache->base);
   vk_free(alloc, cache);
}

VkResult
vn_GetPipelineCacheData(VkDevice device,
                        VkPipelineCache pipelineCache,
                        size_t *pDataSize,
                        void *pData)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_physical_device *physical_dev = dev->physical_device;

   struct vk_pipeline_cache_header *header = pData;
   VkResult result;
   if (!pData) {
      result = vn_call_vkGetPipelineCacheData(dev->instance, device,
                                              pipelineCache, pDataSize, NULL);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);

      *pDataSize += sizeof(*header);
      return VK_SUCCESS;
   }

   if (*pDataSize <= sizeof(*header)) {
      *pDataSize = 0;
      return VK_INCOMPLETE;
   }

   const VkPhysicalDeviceProperties *props =
      &physical_dev->properties.vulkan_1_0;
   header->header_size = sizeof(*header);
   header->header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
   header->vendor_id = props->vendorID;
   header->device_id = props->deviceID;
   memcpy(header->uuid, props->pipelineCacheUUID, VK_UUID_SIZE);

   *pDataSize -= header->header_size;
   result =
      vn_call_vkGetPipelineCacheData(dev->instance, device, pipelineCache,
                                     pDataSize, pData + header->header_size);
   if (result < VK_SUCCESS)
      return vn_error(dev->instance, result);

   *pDataSize += header->header_size;

   return result;
}

VkResult
vn_MergePipelineCaches(VkDevice device,
                       VkPipelineCache dstCache,
                       uint32_t srcCacheCount,
                       const VkPipelineCache *pSrcCaches)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   vn_async_vkMergePipelineCaches(dev->instance, device, dstCache,
                                  srcCacheCount, pSrcCaches);

   return VK_SUCCESS;
}

/* pipeline commands */

struct vn_graphics_pipeline_create_info_fix {
   bool ignore_tessellation_state;

   /* Ignore the following:
    *    pViewportState
    *    pMultisampleState
    *    pDepthStencilState
    *    pColorBlendState
    */
   bool ignore_raster_dedicated_states;
};

static const VkGraphicsPipelineCreateInfo *
vn_fix_graphics_pipeline_create_info(
   struct vn_device *dev,
   uint32_t create_info_count,
   const VkGraphicsPipelineCreateInfo *create_infos,
   const VkAllocationCallbacks *alloc,
   VkGraphicsPipelineCreateInfo **out)
{
   VN_TRACE_FUNC();
   VkGraphicsPipelineCreateInfo *infos = NULL;

   /* Defer allocation until we find a needed fix. */
   struct vn_graphics_pipeline_create_info_fix *fixes = NULL;

   for (uint32_t i = 0; i < create_info_count; i++) {
      const VkGraphicsPipelineCreateInfo *info = &create_infos[i];
      struct vn_graphics_pipeline_create_info_fix fix = { 0 };
      bool any_fix = false;

      VkShaderStageFlags stages = 0;
      for (uint32_t j = 0; j < info->stageCount; j++) {
         stages |= info->pStages[j].stage;
      }

      /* Fix pTessellationState?
       *    VUID-VkGraphicsPipelineCreateInfo-pStages-00731
       */
      if (info->pTessellationState &&
          (!(stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) ||
           !(stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))) {
         fix.ignore_tessellation_state = true;
         any_fix = true;
      }

      bool ignore_raster_dedicated_states =
         !info->pRasterizationState ||
         info->pRasterizationState->rasterizerDiscardEnable == VK_TRUE;
      if (ignore_raster_dedicated_states && info->pDynamicState) {
         for (uint32_t j = 0; j < info->pDynamicState->dynamicStateCount;
              j++) {
            if (info->pDynamicState->pDynamicStates[j] ==
                VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE) {
               ignore_raster_dedicated_states = false;
               break;
            }
         }
      }

      /* FIXME: Conditions for ignoring pDepthStencilState and
       * pColorBlendState miss some cases that depend on the render pass. Make
       * them agree with the VUIDs.
       */
      if (ignore_raster_dedicated_states &&
          (info->pViewportState || info->pMultisampleState ||
           info->pDepthStencilState || info->pColorBlendState)) {
         fix.ignore_raster_dedicated_states = true;
         any_fix = true;
      }

      if (any_fix) {
         if (!fixes) {
            fixes = vk_zalloc(alloc, create_info_count * sizeof(fixes[0]),
                              VN_DEFAULT_ALIGN,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
            if (!fixes)
               return NULL;
         }

         fixes[i] = fix;
      }
   }

   if (!fixes)
      return create_infos;

   infos = vk_alloc(alloc, sizeof(*infos) * create_info_count,
                    VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!infos) {
      vk_free(alloc, fixes);
      return NULL;
   }

   memcpy(infos, create_infos, sizeof(*infos) * create_info_count);

   for (uint32_t i = 0; i < create_info_count; i++) {
      VkGraphicsPipelineCreateInfo *info = &infos[i];
      struct vn_graphics_pipeline_create_info_fix fix = fixes[i];

      if (fix.ignore_tessellation_state)
         info->pTessellationState = NULL;

      if (fix.ignore_raster_dedicated_states) {
         info->pViewportState = NULL;
         info->pMultisampleState = NULL;
         info->pDepthStencilState = NULL;
         info->pColorBlendState = NULL;
      }
   }

   vk_free(alloc, fixes);

   *out = infos;
   return infos;
}

VkResult
vn_CreateGraphicsPipelines(VkDevice device,
                           VkPipelineCache pipelineCache,
                           uint32_t createInfoCount,
                           const VkGraphicsPipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;
   VkGraphicsPipelineCreateInfo *local_infos = NULL;

   pCreateInfos = vn_fix_graphics_pipeline_create_info(
      dev, createInfoCount, pCreateInfos, alloc, &local_infos);
   if (!pCreateInfos)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < createInfoCount; i++) {
      struct vn_pipeline *pipeline =
         vk_zalloc(alloc, sizeof(*pipeline), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!pipeline) {
         for (uint32_t j = 0; j < i; j++)
            vk_free(alloc, vn_pipeline_from_handle(pPipelines[j]));

         if (local_infos)
            vk_free(alloc, local_infos);

         memset(pPipelines, 0, sizeof(*pPipelines) * createInfoCount);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      vn_object_base_init(&pipeline->base, VK_OBJECT_TYPE_PIPELINE,
                          &dev->base);

      VkPipeline pipeline_handle = vn_pipeline_to_handle(pipeline);
      pPipelines[i] = pipeline_handle;
   }

   vn_async_vkCreateGraphicsPipelines(dev->instance, device, pipelineCache,
                                      createInfoCount, pCreateInfos, NULL,
                                      pPipelines);

   if (local_infos)
      vk_free(alloc, local_infos);

   return VK_SUCCESS;
}

VkResult
vn_CreateComputePipelines(VkDevice device,
                          VkPipelineCache pipelineCache,
                          uint32_t createInfoCount,
                          const VkComputePipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipeline *pPipelines)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   for (uint32_t i = 0; i < createInfoCount; i++) {
      struct vn_pipeline *pipeline =
         vk_zalloc(alloc, sizeof(*pipeline), VN_DEFAULT_ALIGN,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!pipeline) {
         for (uint32_t j = 0; j < i; j++)
            vk_free(alloc, vn_pipeline_from_handle(pPipelines[j]));
         memset(pPipelines, 0, sizeof(*pPipelines) * createInfoCount);
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      vn_object_base_init(&pipeline->base, VK_OBJECT_TYPE_PIPELINE,
                          &dev->base);

      VkPipeline pipeline_handle = vn_pipeline_to_handle(pipeline);
      pPipelines[i] = pipeline_handle;
   }

   vn_async_vkCreateComputePipelines(dev->instance, device, pipelineCache,
                                     createInfoCount, pCreateInfos, NULL,
                                     pPipelines);

   return VK_SUCCESS;
}

void
vn_DestroyPipeline(VkDevice device,
                   VkPipeline _pipeline,
                   const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_pipeline *pipeline = vn_pipeline_from_handle(_pipeline);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!pipeline)
      return;

   vn_async_vkDestroyPipeline(dev->instance, device, _pipeline, NULL);

   vn_object_base_fini(&pipeline->base);
   vk_free(alloc, pipeline);
}
