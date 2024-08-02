/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_CMD_POOL_H
#define NVK_CMD_POOL_H

#include "nvk_private.h"

#include "vk_command_pool.h"

#define NVK_CMD_MEM_SIZE 64*1024

struct nvkmd_mem;

/* Recyclable command buffer BO, used for both push buffers and upload */
struct nvk_cmd_mem {
   struct nvkmd_mem *mem;

   /** Link in nvk_cmd_pool::free_bos or nvk_cmd_buffer::bos */
   struct list_head link;
};

struct nvk_cmd_pool {
   struct vk_command_pool vk;

   /** List of nvk_cmd_mem */
   struct list_head free_mem;
   struct list_head free_gart_mem;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(nvk_cmd_pool, vk.base, VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)

static inline struct nvk_device *
nvk_cmd_pool_device(struct nvk_cmd_pool *pool)
{
   return (struct nvk_device *)pool->vk.base.device;
}

VkResult nvk_cmd_pool_alloc_mem(struct nvk_cmd_pool *pool,
                                bool force_gart,
                                struct nvk_cmd_mem **mem_out);

void nvk_cmd_pool_free_mem_list(struct nvk_cmd_pool *pool,
                                struct list_head *mem_list);
void nvk_cmd_pool_free_gart_mem_list(struct nvk_cmd_pool *pool,
                                     struct list_head *mem_list);
#endif /* NVK_CMD_POOL_H */
