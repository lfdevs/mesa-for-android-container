/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_sync.h"

#include <Metal/MTLEvent.h>

/* MTLFence */
mtl_fence *
mtl_new_fence(mtl_device *device)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return (mtl_fence *)[dev newFence];
   }
}

/* MTLEvent */
mtl_event *
mtl_new_event(mtl_device *device)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev newEvent];
   }
}

/* MTLSharedEvent */
mtl_shared_event *
mtl_new_shared_event(mtl_device *device)
{
   @autoreleasepool {
      id<MTLDevice> dev = (id<MTLDevice>)device;
      return [dev newSharedEvent];
   }
}

int
mtl_shared_event_wait_until_signaled_value(mtl_shared_event *event_handle, uint64_t value, uint64_t timeout_ms)
{
   @autoreleasepool {
      id<MTLSharedEvent> event = (id<MTLSharedEvent>)event_handle;
      return (int)[event waitUntilSignaledValue:value timeoutMS:timeout_ms];
   }
}

void
mtl_shared_event_set_signaled_value(mtl_shared_event *event_handle, uint64_t value)
{
   @autoreleasepool {
      id<MTLSharedEvent> event = (id<MTLSharedEvent>)event_handle;
      event.signaledValue = value;
   }
}

uint64_t
mtl_shared_event_get_signaled_value(mtl_shared_event *event_handle)
{
   @autoreleasepool {
      id<MTLSharedEvent> event = (id<MTLSharedEvent>)event_handle;
      return event.signaledValue;
   }
}
