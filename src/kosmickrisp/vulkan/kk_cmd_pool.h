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

struct kk_cmd_pool {
   struct vk_command_pool vk;

   /** List of nvk_cmd_mem */
   struct list_head free_mem;
   struct list_head free_gart_mem;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_cmd_pool, vk.base, VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)

static inline struct kk_device *
kk_cmd_pool_device(struct kk_cmd_pool *pool)
{
   return (struct kk_device *)pool->vk.base.device;
}

#endif /* KK_CMD_POOL_H */
