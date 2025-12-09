/*
 * Copyright 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AMDGPU_NOOP_DRM_SHIM_H
#define AMDGPU_NOOP_DRM_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

void drm_shim_amdgpu_select_device(const char *gpu_id);

#ifdef __cplusplus
}
#endif

#endif
