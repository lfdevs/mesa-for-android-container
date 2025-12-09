/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_SYNC_H
#define MTL_SYNC_H 1

#include "mtl_types.h"

#include <inttypes.h>

/* MTLFence */
mtl_fence *mtl_new_fence(mtl_device *device);

/* MTLEvent */
mtl_event *mtl_new_event(mtl_device *device);

/* MTLSharedEvent */
mtl_shared_event *mtl_new_shared_event(mtl_device *device);
int mtl_shared_event_wait_until_signaled_value(mtl_shared_event *event_handle,
                                               uint64_t value,
                                               uint64_t timeout_ms);
uint64_t mtl_shared_event_get_signaled_value(mtl_shared_event *event_handle);
void mtl_shared_event_set_signaled_value(mtl_shared_event *event_handle,
                                         uint64_t value);

#endif /* MTL_SYNC_H */