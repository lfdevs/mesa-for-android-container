/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_command_queue.h"

#include <Metal/MTLDevice.h>
#include <Metal/MTLCommandQueue.h>

mtl_command_queue *
mtl_new_command_queue(mtl_device *device)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev newMTL4CommandQueue];
   }
}

void
mtl_command_queue_add_residency_set(mtl_command_queue *cmd_queue,
                                    mtl_residency_set *set)
{
   @autoreleasepool {
      id<MTL4CommandQueue> queue = (id<MTL4CommandQueue>)cmd_queue;
      id<MTLResidencySet> s = (id<MTLResidencySet>)set;
      return [queue addResidencySet:s];
   }
}

void
mtl_command_queue_remove_residency_set(mtl_command_queue *cmd_queue,
                                       mtl_residency_set *set)
{
   @autoreleasepool {
      id<MTL4CommandQueue> queue = (id<MTL4CommandQueue>)cmd_queue;
      id<MTLResidencySet> s = (id<MTLResidencySet>)set;
      return [queue removeResidencySet:s];
   }
}

void
mtl_signal_event(mtl_command_queue *queue, mtl_event *event, uint64_t value)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTLEvent> e = (id<MTLEvent>)event;
      [q signalEvent:e value:value];
   }
}

void
mtl_wait_for_event(mtl_command_queue *queue, mtl_event *event, uint64_t value)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTLEvent> e = (id<MTLEvent>)event;
      [q waitForEvent:e value:value];
   }
}

void
mtl_command_queue_commit(mtl_command_queue *queue,
                         mtl_command_buffer **command_buffers, uint32_t count)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTL4CommandBuffer> *cmds = (id<MTL4CommandBuffer> *)command_buffers;
      [q commit:cmds count:count];
   }
}

void
mtl_command_queue_signal_drawable(mtl_command_queue *queue, void *drawable)
{
   @autoreleasepool {
      id<MTL4CommandQueue> q = (id<MTL4CommandQueue>)queue;
      id<MTLDrawable> d = (id<MTLDrawable>)drawable;
      [q signalDrawable:d];
   }
}

void
mtl_drawable_present(void *drawable)
{
   @autoreleasepool {
      id<MTLDrawable> d = (id<MTLDrawable>)drawable;
      [d present];
   }
}
