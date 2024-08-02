/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_BLEND_H
#define PANVK_BLEND_H

#include <stdbool.h>

#include "util/hash_table.h"
#include "util/simple_mtx.h"

#include "pan_blend.h"

#include "panvk_macros.h"
#include "panvk_mempool.h"

struct vk_color_blend_state;
struct panvk_device;

struct panvk_blend_shader {
   struct pan_blend_shader_key key;
   mali_ptr binary;
};

struct panvk_blend_shader_cache {
   struct panvk_pool bin_pool;
   struct hash_table *ht;
   simple_mtx_t lock;
};

#ifdef PAN_ARCH
VkResult panvk_per_arch(blend_shader_cache_init)(struct panvk_device *dev);

void panvk_per_arch(blend_shader_cache_cleanup)(struct panvk_device *dev);

VkResult panvk_per_arch(blend_emit_descs)(
   struct panvk_device *dev, const struct vk_color_blend_state *cb,
   const VkFormat *color_attachment_formats, uint8_t *color_attachment_samples,
   const struct pan_shader_info *fs_info, mali_ptr fs_code,
   struct mali_blend_packed *bds, bool *any_dest_read,
   bool *any_blend_const_load);

#endif

#endif
