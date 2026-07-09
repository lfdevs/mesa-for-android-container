/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VN_RENDERER_SIM_SYNCOBJ_H
#define VN_RENDERER_SIM_SYNCOBJ_H

struct util_sync_provider;

struct util_sync_provider *
vn_renderer_sim_syncobj_get_sync(void);

#endif /* VN_RENDERER_SIM_SYNCOBJ_H */
