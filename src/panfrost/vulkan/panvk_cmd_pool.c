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

   pool = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
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
   panvk_bo_pool_init(&pool->gs_bo_pool);
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
   panvk_bo_pool_cleanup(&pool->gs_bo_pool);
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
   /* jm-v7 112: the v6/v7 geometry emulation slabs (jm-v7 030). */
   panvk_bo_pool_cleanup(&pool->gs_bo_pool);
   panvk_bo_pool_cleanup(&pool->varying_bo_pool);
   panvk_bo_pool_cleanup(&pool->tls_bo_pool);
   panvk_bo_pool_cleanup(&pool->tls_big_bo_pool);

   list_for_each_entry_safe(struct panvk_cmd_pool_obj, obj, &pool->push_sets,
                            node) {
      list_del(&obj->node);
      vk_free(&pool->vk.alloc, obj);
   }
}

/* csf-v11 226: DXVK resets its command pools with flags = 0 after every
 * submission and never trims them, so 220 freed nothing there and each
 * pool's free lists stayed at the largest command list it ever recorded
 * (loading screens included). On a flags = 0 reset, keep what the command
 * buffers just gave back plus a quarter (at least 2 slabs) for the next
 * recording, and free older free-list BOs beyond that.
 * PANVK_CMD_POOL_AUTOTRIM=0 (or PANVK_CMD_POOL_TRIM=0) is the old
 * behaviour. The JM pools (descriptor, TLS, varying and GS slabs) go back
 * to the same free lists at reset and only hold BOs no pending command
 * buffer uses, so v6/v7/v9 trim the same way unless
 * PANVK_JM_CMD_POOL_AUTOTRIM=0 (114: v10+ only). */
static bool
panvk_cmd_pool_autotrim_enabled(struct panvk_device *dev)
{
   static int v = -1;
   if (v < 0)
      v = debug_get_bool_option("PANVK_CMD_POOL_AUTOTRIM", true);
   const struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   return v && (pan_arch(pdev->kmod.dev->props.gpu_id) >= 10 ||
                dev->cmdbuf_opt.jm_pool_autotrim);
}

#define PANVK_CMD_POOL_BO_POOLS 6

/* csf-v11 231: 226 kept only what the last reset gave back (+25%), so a pool
 * whose recordings vary in size freed BOs at a light reset and allocated
 * them again at the next heavy one: 656k BOs / 41.9 GB in 6 min of
 * Fallout 4, with cmd_cs flat. Now each free list keeps the largest count
 * it got back at any reset in the current and the previous window of
 * PANVK_CMD_POOL_AUTOTRIM_WINDOW resets (default 64), plus a quarter (at
 * least 2); a spike is freed one to two windows after it ends.
 * PANVK_CMD_POOL_AUTOTRIM_WINDOW=0 = the 226 behaviour. */
static unsigned
panvk_cmd_pool_autotrim_window(void)
{
   static int v = -1;
   if (v < 0)
      v = MAX2((int)debug_get_num_option("PANVK_CMD_POOL_AUTOTRIM_WINDOW", 64),
               0);
   return v;
}

static void
panvk_cmd_pool_bo_pools(struct panvk_cmd_pool *pool,
                        struct panvk_bo_pool *out[PANVK_CMD_POOL_BO_POOLS])
{
   out[0] = &pool->cs_bo_pool;
   out[1] = &pool->desc_bo_pool;
   out[2] = &pool->varying_bo_pool;
   out[3] = &pool->gs_bo_pool;
   out[4] = &pool->tls_bo_pool;
   out[5] = &pool->tls_big_bo_pool;
}

static void
panvk_bo_pool_trim_to(struct panvk_bo_pool *bo_pool, unsigned have,
                      unsigned keep)
{
   while (have > keep && !list_is_empty(&bo_pool->free_bos)) {
      struct panvk_priv_bo *bo =
         list_last_entry(&bo_pool->free_bos, struct panvk_priv_bo, node);
      list_del(&bo->node);
      p_atomic_inc(&panvk_mem_stats.cmdpool_trim_bos);
      p_atomic_add(&panvk_mem_stats.cmdpool_trim_B, pan_kmod_bo_size(bo->bo));
      panvk_priv_bo_unref(bo);
      have--;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_ResetCommandPool(VkDevice _device, VkCommandPool commandPool,
                       VkCommandPoolResetFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_pool, pool, commandPool);
   VK_FROM_HANDLE(panvk_device, device, _device);

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

   struct panvk_bo_pool *bo_pools[PANVK_CMD_POOL_BO_POOLS];
   unsigned before[PANVK_CMD_POOL_BO_POOLS] = {0};
   const bool autotrim =
      !release_resources && panvk_cmd_pool_autotrim_enabled(device);
   if (autotrim) {
      panvk_cmd_pool_bo_pools(pool, bo_pools);
      for (unsigned i = 0; i < PANVK_CMD_POOL_BO_POOLS; i++)
         before[i] = list_length(&bo_pools[i]->free_bos);
   }

   VkResult result = vk_common_ResetCommandPool(_device, commandPool, flags);

   /* The command buffer resets above splice their BOs into the pool free
    * lists, so free those only now.
    */
   if (result == VK_SUCCESS && release_resources)
      panvk_cmd_pool_release_free_bos(pool);

   if (result == VK_SUCCESS && autotrim) {
      const unsigned window = panvk_cmd_pool_autotrim_window();
      for (unsigned i = 0; i < PANVK_CMD_POOL_BO_POOLS; i++) {
         const unsigned after = list_length(&bo_pools[i]->free_bos);
         const unsigned used = after > before[i] ? after - before[i] : 0;
         unsigned base = used;
         if (window) {
            pool->autotrim.cur[i] = MAX2(pool->autotrim.cur[i], used);
            base = MAX2(pool->autotrim.cur[i], pool->autotrim.prev[i]);
         }
         panvk_bo_pool_trim_to(bo_pools[i], after, base + MAX2(base / 4, 2));
      }
      if (window && ++pool->autotrim.resets >= window) {
         pool->autotrim.resets = 0;
         for (unsigned i = 0; i < PANVK_CMD_POOL_BO_POOLS; i++) {
            pool->autotrim.prev[i] = pool->autotrim.cur[i];
            pool->autotrim.cur[i] = 0;
         }
      }
   }

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
