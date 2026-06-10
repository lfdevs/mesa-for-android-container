/*
 * Copyright © 2017 Google.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DEBUG_H
#define RADV_DEBUG_H

#include "radv_instance.h"
#include "radv_physical_device.h"

struct radv_device;

struct radv_backed_buffer {
   VkDeviceMemory memory;
   VkBuffer buffer;
   void *map;
};

enum radv_memory_type {
   radv_memory_type_invisible_vram,
   radv_memory_type_visible_vram,
   radv_memory_type_gtt,
};

VkResult radv_backed_buffer_init(struct radv_device *device, struct radv_backed_buffer *buffer, uint64_t size,
                                 enum radv_memory_type memory_type, VkBufferUsageFlags2 usage, bool map);

void radv_backed_buffer_finish(struct radv_device *device, struct radv_backed_buffer *buffer);

uint64_t radv_backed_buffer_get_va(struct radv_device *device, struct radv_backed_buffer *buffer);

#endif /* RADV_DEBUG_H */
