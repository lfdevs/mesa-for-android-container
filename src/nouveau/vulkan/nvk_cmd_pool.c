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

   uint32_t flags = NVKMD_MEM_GART;
   if (force_gart)
      assert(flags & NVKMD_MEM_GART);
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
}

VkResult
nvk_cmd_pool_alloc_mem(struct nvk_cmd_pool *pool, bool force_gart,
                       struct nvk_cmd_mem **mem_out)
{
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
      *mem_out = mem;
      return VK_SUCCESS;
   }

   return nvk_cmd_mem_create(pool, force_gart, mem_out);
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
