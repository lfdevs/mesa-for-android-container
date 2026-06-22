/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_command_buffer.h"

#include <Metal/MTL4CommandBuffer.h>
#include <Metal/MTL4CommandAllocator.h>

void
mtl_command_allocator_reset(mtl_command_allocator *allocator)
{
   @autoreleasepool {
      id<MTL4CommandAllocator> alloc = (id<MTL4CommandAllocator>)allocator;
      [alloc reset];
   }
}

void
mtl_begin_command_buffer(mtl_command_buffer *command_buffer,
                         mtl_command_allocator *allocator)
{
   @autoreleasepool {
      id<MTL4CommandBuffer> cmd_buf = (id<MTL4CommandBuffer>)command_buffer;
      id<MTL4CommandAllocator> alloc = (id<MTL4CommandAllocator>)allocator;
      [cmd_buf beginCommandBufferWithAllocator:alloc];
   }
}

void
mtl_end_command_buffer(mtl_command_buffer *command_buffer)
{
   @autoreleasepool {
      id<MTL4CommandBuffer> cmd_buf = (id<MTL4CommandBuffer>)command_buffer;
      [cmd_buf endCommandBuffer];
   }
}
