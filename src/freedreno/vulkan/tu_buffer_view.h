/*
 * Copyright © 2024 Valentine Burley
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_BUFFER_VIEW_H
#define TU_BUFFER_VIEW_H

#include <stdint.h>

#include "vk_buffer_view.h"
#include "vk_object.h"
#include "vulkan/vulkan_core.h"

#include "fdl/freedreno_layout.h"

struct tu_buffer;

struct tu_buffer_view
{
   struct vk_buffer_view vk;

   uint32_t descriptor[FDL6_TEX_CONST_DWORDS];

   struct tu_buffer *buffer;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(tu_buffer_view, vk.base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)

#endif /* TU_BUFFER_VIEW_H */
