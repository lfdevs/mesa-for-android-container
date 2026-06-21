/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based on amdgpu winsys.
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_AMDGPU_WINSYS_PUBLIC_H
#define RADV_AMDGPU_WINSYS_PUBLIC_H

#include "ac_gpu_info.h"
#include "ac_linux_drm.h"

#include "vk_sync.h"

VkResult radv_amdgpu_winsys_create(int fd, const struct radeon_info *info, uint64_t debug_flags,
                                   uint64_t perftest_flags, bool is_virtio, struct radeon_winsys **winsys);

struct radeon_winsys_info {
   struct radeon_info base;
   struct vk_sync_type syncobj_sync_type;
   uint32_t global_priority_mask;
};

VkResult radv_amdgpu_winsys_query_info(int fd, uint64_t debug_flags, bool is_virtio, struct radeon_winsys_info *info);

struct radeon_winsys_heap_info {
   uint64_t allocated_vram;
   uint64_t vram_usage;
   uint64_t allocated_vram_vis;
   uint64_t vram_vis_usage;
   uint64_t allocated_gtt;
   uint64_t gtt_usage;
};

int radv_amdgpu_winsys_query_heap_info(ac_drm_device *dev, struct radeon_winsys_heap_info *heap_info);

#endif /* RADV_AMDGPU_WINSYS_PUBLIC_H */
