/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_CMD_POOL_H
#define KK_CMD_POOL_H

#include "kk_private.h"

#include "vk_command_pool.h"

#define KK_CMD_BO_SIZE     (1024 * 128)
#define KK_CMD_POOL_BO_MAX 32

/* Recyclable command buffer BO, used for both push buffers and upload */
struct kk_cmd_bo {
   struct kk_bo *bo;

   /** Link in kk_cmd_pool::free_bos or kk_cmd_buffer::bos */
   struct list_head link;
};

struct kk_cmd_pool {
   struct vk_command_pool vk;

   /** List of kk_cmd_bo */
   struct list_head free_bos;
   uint32_t num_free_bos;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_cmd_pool, vk.base, VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)

static inline struct kk_device *
kk_cmd_pool_device(struct kk_cmd_pool *pool)
{
   return (struct kk_device *)pool->vk.base.device;
}

VkResult kk_cmd_pool_alloc_bo(struct kk_cmd_pool *pool,
                              struct kk_cmd_bo **bo_out);

void kk_cmd_pool_free_bo_list(struct kk_cmd_pool *pool, struct list_head *bos);

#endif /* KK_CMD_POOL_H */
