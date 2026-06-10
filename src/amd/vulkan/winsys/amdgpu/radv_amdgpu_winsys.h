/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * based on amdgpu winsys.
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_AMDGPU_WINSYS_H
#define RADV_AMDGPU_WINSYS_H

#include <pthread.h>
#include "util/list.h"
#include "util/rwlock.h"
#include "util/simple_mtx.h"
#include "ac_gpu_info.h"
#include "ac_linux_drm.h"
#include "radv_radeon_winsys.h"

#include "vk_sync.h"
#include "vk_sync_timeline.h"

/**
 * Process-global per-GPU allocation tracker.
 *
 * Tracks userspace BO allocation counters across all winsys instances for
 * the same GPU within this process. This ensures VK_EXT_memory_budget
 * reports correct process-wide usage even with multiple VkInstance objects.
 */
struct radv_amdgpu_alloc_tracker {
   uintptr_t cookie;
   alignas(8) uint64_t allocated_vram;
   alignas(8) uint64_t allocated_vram_vis;
   alignas(8) uint64_t allocated_gtt;
   uint32_t refcount;
};

struct radv_amdgpu_winsys {
   struct radeon_winsys base;
   ac_drm_device *dev;
   int fd;

   struct radeon_info info;

   bool debug_all_bos;
   bool debug_log_bos;
   bool dump_ibs;
   FILE *bo_history_logfile;
   bool chain_ib;
   bool zero_all_vram_allocs;
   bool debug_vm;
   uint64_t perftest;

   struct radv_amdgpu_alloc_tracker *alloc_tracker;

   /* Global BO list */
   struct {
      struct radv_amdgpu_winsys_bo **bos;
      uint32_t count;
      uint32_t capacity;
      struct u_rwlock lock;
   } global_bo_list;

   /* BO log */
   struct u_rwlock log_bo_list_lock;
   struct list_head log_bo_list;

   simple_mtx_t vm_ioctl_lock;
   uint32_t vm_timeline_syncobj;
   uint64_t vm_timeline_seq_num;

   struct {
      /* A zero-allocated BO used to map the LOW address space of virtual allocations. */
      struct radeon_winsys_bo *bo;
      simple_mtx_t lock;
   } null_prt_bug;
};

static inline struct radv_amdgpu_winsys *
radv_amdgpu_winsys(struct radeon_winsys *base)
{
   return (struct radv_amdgpu_winsys *)base;
}

#endif /* RADV_AMDGPU_WINSYS_H */
