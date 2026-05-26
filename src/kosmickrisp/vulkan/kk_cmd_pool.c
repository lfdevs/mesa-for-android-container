/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_cmd_pool.h"

#include "kk_device.h"
#include "kk_entrypoints.h"
#include "kk_physical_device.h"

static VkResult
kk_cmd_bo_create(struct kk_cmd_pool *pool, struct kk_cmd_bo **bo_out)
{
   struct kk_device *dev = kk_cmd_pool_device(pool);

   struct kk_cmd_bo *bo = vk_zalloc(&pool->vk.alloc, sizeof(*bo), 8,
                                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (bo == NULL)
      return vk_error(pool, VK_ERROR_OUT_OF_HOST_MEMORY);

   const VkResult result =
      kk_alloc_bo(dev, &pool->vk.base, KK_CMD_BO_SIZE, 0, &bo->bo);
   if (result != VK_SUCCESS) {
      vk_free(&pool->vk.alloc, bo);
      return vk_error(pool, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   *bo_out = bo;
   return VK_SUCCESS;
}

static void
kk_cmd_bo_destroy(struct kk_cmd_pool *pool, struct kk_cmd_bo *bo)
{
   struct kk_device *dev = kk_cmd_pool_device(pool);
   kk_destroy_bo(dev, bo->bo);
   vk_free(&pool->vk.alloc, bo);
}

static void
kk_cmd_pool_destroy_bos(struct kk_cmd_pool *pool)
{
   list_for_each_entry_safe(struct kk_cmd_bo, bo, &pool->free_bos, link)
      kk_cmd_bo_destroy(pool, bo);

   list_inithead(&pool->free_bos);
}

VkResult
kk_cmd_pool_alloc_bo(struct kk_cmd_pool *pool, struct kk_cmd_bo **bo_out)
{
   struct kk_cmd_bo *bo = NULL;
   if (!list_is_empty(&pool->free_bos)) {
      bo = list_first_entry(&pool->free_bos, struct kk_cmd_bo, link);
      pool->num_free_bos--;
   }

   if (bo) {
      list_del(&bo->link);
      *bo_out = bo;
      return VK_SUCCESS;
   }

   return kk_cmd_bo_create(pool, bo_out);
}

void
kk_cmd_pool_free_bo_list(struct kk_cmd_pool *pool, struct list_head *bos)
{
   list_for_each_entry_safe(struct kk_cmd_bo, bo, bos, link) {
      list_del(&bo->link);
      if (pool->num_free_bos > KK_CMD_POOL_BO_MAX) {
         kk_cmd_bo_destroy(pool, bo);
      } else {
         list_addtail(&bo->link, &pool->free_bos);
         pool->num_free_bos++;
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateCommandPool(VkDevice _device,
                     const VkCommandPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkCommandPool *pCmdPool)
{
   VK_FROM_HANDLE(kk_device, device, _device);
   struct kk_cmd_pool *pool;

   pool = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_command_pool_init(&device->vk, &pool->vk, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pool);
      return result;
   }

   list_inithead(&pool->free_bos);

   *pCmdPool = kk_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyCommandPool(VkDevice _device, VkCommandPool commandPool,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, device, _device);
   VK_FROM_HANDLE(kk_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   vk_command_pool_finish(&pool->vk);
   kk_cmd_pool_destroy_bos(pool);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR void VKAPI_CALL
kk_TrimCommandPool(VkDevice device, VkCommandPool commandPool,
                   VkCommandPoolTrimFlags flags)
{
   VK_FROM_HANDLE(kk_cmd_pool, pool, commandPool);

   vk_command_pool_trim(&pool->vk, flags);
   kk_cmd_pool_destroy_bos(pool);
}
