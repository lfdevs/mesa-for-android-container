/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>

#include "panvk_cmd_pool.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "vk_common_entrypoints.h"

#include "vk_alloc.h"
#include "vk_log.h"

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateCommandPool(VkDevice _device,
                        const VkCommandPoolCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkCommandPool *pCmdPool)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_cmd_pool *pool;

   pool = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_command_pool_init(&device->vk, &pool->vk, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pool);
      return result;
   }

   panvk_bo_pool_init(&pool->cs_bo_pool);
   panvk_bo_pool_init(&pool->desc_bo_pool);
   panvk_bo_pool_init(&pool->varying_bo_pool);
   panvk_bo_pool_init(&pool->tls_bo_pool);
   panvk_bo_pool_init(&pool->tls_big_bo_pool);
   list_inithead(&pool->push_sets);
   *pCmdPool = panvk_cmd_pool_to_handle(pool);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_DestroyCommandPool(VkDevice _device, VkCommandPool commandPool,
                         const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   vk_command_pool_finish(&pool->vk);

   panvk_bo_pool_cleanup(&pool->cs_bo_pool);
   panvk_bo_pool_cleanup(&pool->desc_bo_pool);
   panvk_bo_pool_cleanup(&pool->varying_bo_pool);
   panvk_bo_pool_cleanup(&pool->tls_bo_pool);
   panvk_bo_pool_cleanup(&pool->tls_big_bo_pool);

   list_for_each_entry_safe(struct panvk_cmd_pool_obj, obj, &pool->push_sets,
                            node) {
      list_del(&obj->node);
      vk_free(&pool->vk.alloc, obj);
   }

   vk_free2(&device->vk.alloc, pAllocator, pool);
}

/* csf-v11 220: vkResetCommandPool(RELEASE_RESOURCES) and
 * vkTrimCommandPool free the pool's BO free lists (desc, varying, TLS, CS,
 * GS slabs) and the cached push sets. Before, the free lists only grew.
 */
static bool
panvk_cmd_pool_trim_enabled(void)
{
   static int v = -1;
   if (v < 0) {
      const char *e = getenv("PANVK_CMD_POOL_TRIM");
      v = !(e && e[0] == '0');
   }
   return v;
}

static void
panvk_cmd_pool_release_free_bos(struct panvk_cmd_pool *pool)
{
   panvk_bo_pool_cleanup(&pool->cs_bo_pool);
   panvk_bo_pool_cleanup(&pool->desc_bo_pool);
   panvk_bo_pool_cleanup(&pool->varying_bo_pool);
   panvk_bo_pool_cleanup(&pool->tls_bo_pool);
   panvk_bo_pool_cleanup(&pool->tls_big_bo_pool);

   list_for_each_entry_safe(struct panvk_cmd_pool_obj, obj, &pool->push_sets,
                            node) {
      list_del(&obj->node);
      vk_free(&pool->vk.alloc, obj);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_ResetCommandPool(VkDevice _device, VkCommandPool commandPool,
                       VkCommandPoolResetFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_pool, pool, commandPool);

   const bool release_resources =
      (flags & VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT) != 0;

   /* csf-v11 220: PANVK_CMD_POOL_TRIM=0 restores the old order (only
    * tls_big_bo_pool cleaned, before the command buffers give their BOs
    * back, so nothing was released).
    */
   if (!panvk_cmd_pool_trim_enabled()) {
      if (release_resources)
         panvk_bo_pool_cleanup(&pool->tls_big_bo_pool);
      return vk_common_ResetCommandPool(_device, commandPool, flags);
   }

   VkResult result = vk_common_ResetCommandPool(_device, commandPool, flags);

   /* The command buffer resets above splice their BOs into the pool free
    * lists, so free those only now.
    */
   if (result == VK_SUCCESS && release_resources)
      panvk_cmd_pool_release_free_bos(pool);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_TrimCommandPool(VkDevice _device, VkCommandPool commandPool,
                      VkCommandPoolTrimFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_pool, pool, commandPool);

   vk_common_TrimCommandPool(_device, commandPool, flags);

   /* Only free-list BOs: they belong to reset command buffers, which are
    * not pending, and the pool is externally synchronized.
    */
   if (panvk_cmd_pool_trim_enabled())
      panvk_cmd_pool_release_free_bos(pool);
}
