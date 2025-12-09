/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_cmd_pool.h"

#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_physical_device.h"
#include "nvkmd/nvkmd.h"

static VkResult
nvk_cmd_mem_create(struct nvk_cmd_pool *pool, bool force_gart, struct nvk_cmd_mem **mem_out)
{
   struct nvk_device *dev = nvk_cmd_pool_device(pool);
   struct nvk_cmd_mem *mem;
   VkResult result;

   mem = vk_zalloc(&pool->vk.alloc, sizeof(*mem), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(pool, VK_ERROR_OUT_OF_HOST_MEMORY);

   const uint32_t flags = force_gart ? NVKMD_MEM_GART
                                     : NVKMD_MEM_LOCAL;

   result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &pool->vk.base,
                                       NVK_CMD_MEM_SIZE, 0,
                                       flags, NVKMD_MEM_MAP_WR,
                                       &mem->mem);
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, mem);
      return result;
   }

   *mem_out = mem;
   return VK_SUCCESS;
}

static void
nvk_cmd_mem_destroy(struct nvk_cmd_pool *pool, struct nvk_cmd_mem *mem)
{
   nvkmd_mem_unref(mem->mem);
   vk_free(&pool->vk.alloc, mem);
}

static VkResult
nvk_cmd_qmd_create(struct nvk_cmd_pool *pool, struct nvk_cmd_qmd **qmd_out)
{
   struct nvk_device *dev = nvk_cmd_pool_device(pool);
   struct nvk_cmd_qmd *qmd;
   VkResult result;

   qmd = vk_zalloc(&pool->vk.alloc, sizeof(*qmd), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (qmd == NULL)
      return vk_error(pool, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = nvk_heap_alloc(dev, &dev->qmd_heap,
                           NVK_CMD_QMD_SIZE, NVK_CMD_QMD_SIZE,
                           &qmd->addr, &qmd->map);
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, qmd);
      return result;
   }

   *qmd_out = qmd;
   return VK_SUCCESS;
}

static void
nvk_cmd_qmd_destroy(struct nvk_cmd_pool *pool, struct nvk_cmd_qmd *qmd)
{
   struct nvk_device *dev = nvk_cmd_pool_device(pool);

   nvk_heap_free(dev, &dev->qmd_heap, qmd->addr, NVK_CMD_QMD_SIZE);
   vk_free(&pool->vk.alloc, qmd);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateCommandPool(VkDevice _device,
                      const VkCommandPoolCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkCommandPool *pCmdPool)
{
   VK_FROM_HANDLE(nvk_device, device, _device);
   struct nvk_cmd_pool *pool;

   pool = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_pool_init(&device->vk, &pool->vk,
                                          pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pool);
      return result;
   }

   list_inithead(&pool->free_mem);
   list_inithead(&pool->free_gart_mem);
   list_inithead(&pool->free_qmd);

   *pCmdPool = nvk_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

static void
nvk_cmd_pool_destroy_mem(struct nvk_cmd_pool *pool)
{
   list_for_each_entry_safe(struct nvk_cmd_mem, mem, &pool->free_mem, link)
      nvk_cmd_mem_destroy(pool, mem);

   list_inithead(&pool->free_mem);

   list_for_each_entry_safe(struct nvk_cmd_mem, mem, &pool->free_gart_mem, link)
      nvk_cmd_mem_destroy(pool, mem);

   list_inithead(&pool->free_gart_mem);

   list_for_each_entry_safe(struct nvk_cmd_qmd, qmd, &pool->free_qmd, link)
      nvk_cmd_qmd_destroy(pool, qmd);
   list_inithead(&pool->free_qmd);
}

VkResult
nvk_cmd_pool_alloc_mem(struct nvk_cmd_pool *pool, bool force_gart,
                       struct nvk_cmd_mem **mem_out)
{
   const struct nvk_device *dev = nvk_cmd_pool_device(pool);
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_cmd_mem *mem = NULL;

   if (force_gart) {
      if (!list_is_empty(&pool->free_gart_mem))
         mem = list_first_entry(&pool->free_gart_mem, struct nvk_cmd_mem, link);
   } else {
      if (!list_is_empty(&pool->free_mem))
         mem = list_first_entry(&pool->free_mem, struct nvk_cmd_mem, link);
   }

   if (mem) {
      list_del(&mem->link);
   } else {
      VkResult result = nvk_cmd_mem_create(pool, force_gart, &mem);
      if (result != VK_SUCCESS)
         return result;
   }

   if (unlikely(pdev->debug_flags & NVK_DEBUG_TRASH_MEMORY)) {
      memset(mem->mem->map, 0xF1, mem->mem->size_B);
   }

   *mem_out = mem;
   return VK_SUCCESS;
}

VkResult
nvk_cmd_pool_alloc_qmd(struct nvk_cmd_pool *pool,
                       struct nvk_cmd_qmd **qmd_out)
{
   if (!list_is_empty(&pool->free_qmd)) {
      struct nvk_cmd_qmd *qmd =
         list_first_entry(&pool->free_qmd, struct nvk_cmd_qmd, link);
      list_del(&qmd->link);
      *qmd_out = qmd;
      return VK_SUCCESS;
   }

   return nvk_cmd_qmd_create(pool, qmd_out);
}

void
nvk_cmd_pool_free_mem_list(struct nvk_cmd_pool *pool,
                           struct list_head *mem_list)
{
   list_splicetail(mem_list, &pool->free_mem);
   list_inithead(mem_list);
}

void
nvk_cmd_pool_free_gart_mem_list(struct nvk_cmd_pool *pool,
                                struct list_head *mem_list)
{
   list_splicetail(mem_list, &pool->free_gart_mem);
   list_inithead(mem_list);
}

void
nvk_cmd_pool_free_qmd_list(struct nvk_cmd_pool *pool,
                           struct list_head *qmd_list)
{
   list_splicetail(qmd_list, &pool->free_qmd);
   list_inithead(qmd_list);
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyCommandPool(VkDevice _device,
                       VkCommandPool commandPool,
                       const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, device, _device);
   VK_FROM_HANDLE(nvk_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   vk_command_pool_finish(&pool->vk);
   nvk_cmd_pool_destroy_mem(pool);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR void VKAPI_CALL
nvk_TrimCommandPool(VkDevice device,
                    VkCommandPool commandPool,
                    VkCommandPoolTrimFlags flags)
{
   VK_FROM_HANDLE(nvk_cmd_pool, pool, commandPool);

   vk_command_pool_trim(&pool->vk, flags);
   nvk_cmd_pool_destroy_mem(pool);
}
