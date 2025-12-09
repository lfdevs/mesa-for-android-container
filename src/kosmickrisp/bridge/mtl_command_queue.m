/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_command_queue.h"

#include <Metal/MTLDevice.h>
#include <Metal/MTLCommandQueue.h>

mtl_command_queue *
mtl_new_command_queue(mtl_device *device, uint32_t cmd_buffer_count)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev newCommandQueueWithMaxCommandBufferCount:cmd_buffer_count];
   }
}

mtl_command_buffer *
mtl_new_command_buffer(mtl_command_queue *cmd_queue)
{
   @autoreleasepool {
      id<MTLCommandQueue> queue = (id<MTLCommandQueue>)cmd_queue;
      return [[queue commandBuffer] retain];
   }
}
