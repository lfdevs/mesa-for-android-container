/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_PUSH_CONSTANT_H
#define PANVK_CMD_PUSH_CONSTANT_H

#include <stdint.h>

#include "genxml/gen_macros.h"

#include "pan_pool.h"

#define MAX_PUSH_CONSTANTS_SIZE 128

struct panvk_push_constant_state {
   uint8_t data[MAX_PUSH_CONSTANTS_SIZE];
};

static inline mali_ptr
panvk_cmd_prepare_push_uniforms(struct pan_pool *desc_pool_base,
                                struct panvk_push_constant_state *push,
                                void *sysvals, unsigned sysvals_sz)
{
   struct panfrost_ptr push_uniforms =
      pan_pool_alloc_aligned(desc_pool_base, 512, 16);

   /* The first half is used for push constants. */
   memcpy(push_uniforms.cpu, push->data, sizeof(push->data));

   /* The second half is used for sysvals. */
   memcpy((uint8_t *)push_uniforms.cpu + 256, sysvals, sysvals_sz);

   return push_uniforms.gpu;
}

static inline void
panvk_cmd_push_constants(struct panvk_push_constant_state *push,
                         VkShaderStageFlags stages, uint32_t offset,
                         uint32_t size, const void *values)
{
   memcpy(push->data + offset, values, size);
}

#endif
