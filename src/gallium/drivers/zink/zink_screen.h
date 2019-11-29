/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ZINK_SCREEN_H
#define ZINK_SCREEN_H

#include "pipe/p_screen.h"
#include "util/slab.h"

#include <vulkan/vulkan.h>

extern uint32_t zink_debug;

#define ZINK_DEBUG_NIR 0x1
#define ZINK_DEBUG_SPIRV 0x2
#define ZINK_DEBUG_TGSI 0x4

struct zink_screen {
   struct pipe_screen base;

   struct sw_winsys *winsys;

   struct slab_parent_pool transfer_pool;

   VkInstance instance;
   VkPhysicalDevice pdev;

   VkPhysicalDeviceProperties props;
   VkPhysicalDeviceFeatures feats;
   VkPhysicalDeviceMemoryProperties mem_props;

   bool have_KHR_maintenance1;
   bool have_KHR_external_memory_fd;

   bool have_X8_D24_UNORM_PACK32;
   bool have_D24_UNORM_S8_UINT;

   uint32_t gfx_queue;
   VkDevice dev;

   PFN_vkGetMemoryFdKHR vk_GetMemoryFdKHR;
};

static inline struct zink_screen *
zink_screen(struct pipe_screen *pipe)
{
   return (struct zink_screen *)pipe;
}

VkFormat
zink_get_format(struct zink_screen *screen, enum pipe_format format);

#endif
