/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "mtl_sync.h"

/* MTLFence */
mtl_fence *
mtl_new_fence(mtl_device *device)
{
   return NULL;
}

/* MTLEvent */
mtl_event *
mtl_new_event(mtl_device *device)
{
   return NULL;
}

/* MTLSharedEvent */
mtl_shared_event *
mtl_new_shared_event(mtl_device *device)
{
   return NULL;
}

int
mtl_shared_event_wait_until_signaled_value(mtl_shared_event *event_handle,
                                           uint64_t value, uint64_t timeout_ms)
{
   return 0;
}

uint64_t
mtl_shared_event_get_signaled_value(mtl_shared_event *event_handle)
{
   return 0u;
}

void
mtl_shared_event_set_signaled_value(mtl_shared_event *event_handle,
                                    uint64_t value)
{
}
