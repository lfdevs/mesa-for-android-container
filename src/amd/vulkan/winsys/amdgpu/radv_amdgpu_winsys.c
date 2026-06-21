/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * based on amdgpu winsys.
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "radv_amdgpu_winsys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "drm-uapi/amdgpu_drm.h"
#include "tools/radv_debug.h"
#include "util/hash_table.h"
#include "util/u_memory.h"
#include "ac_linux_drm.h"
#include "ac_surface.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_winsys_public.h"
#include "vk_drm_syncobj.h"

static uint64_t
radv_amdgpu_winsys_query_value(struct radeon_winsys *rws, enum radeon_value_id value)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   struct amdgpu_heap_info heap = {0};
   uint64_t retval = 0;

   switch (value) {
   case RADEON_ALLOCATED_VRAM:
      return ws->alloc_tracker->allocated_vram;
   case RADEON_ALLOCATED_VRAM_VIS:
      return ws->alloc_tracker->allocated_vram_vis;
   case RADEON_ALLOCATED_GTT:
      return ws->alloc_tracker->allocated_gtt;
   case RADEON_TIMESTAMP:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_TIMESTAMP, 8, &retval);
      return retval;
   case RADEON_NUM_BYTES_MOVED:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_NUM_BYTES_MOVED, 8, &retval);
      return retval;
   case RADEON_NUM_EVICTIONS:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_NUM_EVICTIONS, 8, &retval);
      return retval;
   case RADEON_NUM_VRAM_CPU_PAGE_FAULTS:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_NUM_VRAM_CPU_PAGE_FAULTS, 8, &retval);
      return retval;
   case RADEON_VRAM_USAGE:
      ac_drm_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &heap);
      return heap.heap_usage;
   case RADEON_VRAM_VIS_USAGE:
      ac_drm_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED, &heap);
      return heap.heap_usage;
   case RADEON_GTT_USAGE:
      ac_drm_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_GTT, 0, &heap);
      return heap.heap_usage;
   case RADEON_GPU_TEMPERATURE:
      ac_drm_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GPU_TEMP, 4, &retval);
      return retval;
   case RADEON_CURRENT_SCLK:
      ac_drm_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, 4, &retval);
      return retval;
   case RADEON_CURRENT_MCLK:
      ac_drm_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_MCLK, 4, &retval);
      return retval;
   default:
      UNREACHABLE("invalid query value");
   }

   return 0;
}

static bool
radv_amdgpu_winsys_read_registers(struct radeon_winsys *rws, unsigned reg_offset, unsigned num_registers, uint32_t *out)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;

   return ac_drm_read_mm_registers(ws->dev, reg_offset / 4, num_registers, 0xffffffff, 0, out) == 0;
}

static bool
radv_amdgpu_winsys_query_gpuvm_fault(struct radeon_winsys *rws, struct radv_winsys_gpuvm_fault_info *fault_info)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   struct drm_amdgpu_info_gpuvm_fault gpuvm_fault = {0};
   int r;

   r = ac_drm_query_info(ws->dev, AMDGPU_INFO_GPUVM_FAULT, sizeof(gpuvm_fault), &gpuvm_fault);
   if (r < 0) {
      fprintf(stderr, "radv/amdgpu: Failed to query the last GPUVM fault (%d).\n", r);
      return false;
   }

   /* When the GPUVM fault status is 0, no faults happened. */
   if (!gpuvm_fault.status)
      return false;

   fault_info->addr = gpuvm_fault.addr;
   fault_info->status = gpuvm_fault.status;
   fault_info->vmhub = gpuvm_fault.vmhub;

   return true;
}

static simple_mtx_t tracker_mutex = SIMPLE_MTX_INITIALIZER;
static struct hash_table *alloc_trackers = NULL;

static struct radv_amdgpu_alloc_tracker *
radv_amdgpu_alloc_tracker_acquire(uintptr_t cookie)
{
   struct radv_amdgpu_alloc_tracker *tracker = NULL;

   simple_mtx_lock(&tracker_mutex);

   if (!alloc_trackers)
      alloc_trackers = _mesa_pointer_hash_table_create(NULL);
   if (!alloc_trackers) {
      simple_mtx_unlock(&tracker_mutex);
      return NULL;
   }

   struct hash_entry *entry = _mesa_hash_table_search(alloc_trackers, (void *)cookie);
   if (entry) {
      tracker = entry->data;
      tracker->refcount++;
   } else {
      tracker = calloc(1, sizeof(*tracker));
      if (!tracker) {
         simple_mtx_unlock(&tracker_mutex);
         return NULL;
      }

      tracker->refcount = 1;
      tracker->cookie = cookie; /* used for release. */
      _mesa_hash_table_insert(alloc_trackers, (void *)cookie, tracker);
   }

   simple_mtx_unlock(&tracker_mutex);
   return tracker;
}

static void
radv_amdgpu_alloc_tracker_release(struct radv_amdgpu_alloc_tracker *tracker)
{
   simple_mtx_lock(&tracker_mutex);

   if (!--tracker->refcount) {
      _mesa_hash_table_remove_key(alloc_trackers, (void *)tracker->cookie);
      free(tracker);

      if (_mesa_hash_table_num_entries(alloc_trackers) == 0) {
         _mesa_hash_table_destroy(alloc_trackers, NULL);
         alloc_trackers = NULL;
      }
   }

   simple_mtx_unlock(&tracker_mutex);
}

static void
radv_amdgpu_winsys_destroy(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;

   if (ws->info.compiler_info.has_smem_with_null_prt_bug) {
      simple_mtx_destroy(&ws->null_prt_bug.lock);
      if (ws->null_prt_bug.bo)
         ws->base.buffer_destroy(&ws->base, ws->null_prt_bug.bo);
   }

   u_rwlock_destroy(&ws->global_bo_list.lock);
   free(ws->global_bo_list.bos);

   ac_drm_cs_destroy_syncobj(ws->dev, ws->vm_timeline_syncobj);
   simple_mtx_destroy(&ws->vm_ioctl_lock);

   if (ws->bo_history_logfile)
      fclose(ws->bo_history_logfile);

   u_rwlock_destroy(&ws->log_bo_list_lock);

   radv_amdgpu_alloc_tracker_release(ws->alloc_tracker);

   ac_drm_device_deinitialize(ws->dev);
   FREE(rws);
}

static int
radv_amdgpu_winsys_get_fd(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   return ws->fd;
}

static struct util_sync_provider *
radv_amdgpu_winsys_get_sync_provider(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   struct util_sync_provider *p = ac_drm_device_get_sync_provider(ws->dev);
   /* vk_device owns the provider, so we need to clone it. */
   return p->clone(p);
}

static int
radv_amdgpu_winsys_reserve_vmid(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   return ac_drm_vm_reserve_vmid(ws->dev, 0);
}

static void
radv_amdgpu_winsys_unreserve_vmid(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   ac_drm_vm_unreserve_vmid(ws->dev, 0);
}

VkResult
radv_amdgpu_winsys_create(int fd, const struct radeon_info *info, uint64_t debug_flags, uint64_t perftest_flags,
                          bool is_virtio, struct radeon_winsys **winsys)
{
   VkResult result = VK_SUCCESS;

   uint32_t drm_major, drm_minor, r;
   ac_drm_device *dev;
   struct radv_amdgpu_winsys *ws = NULL;

   r = ac_drm_device_initialize(fd, is_virtio, &drm_major, &drm_minor, &dev);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to initialize device.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   ws = calloc(1, sizeof(struct radv_amdgpu_winsys));
   if (!ws) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   ws->dev = dev;
   ws->fd = ac_drm_device_get_fd(dev);

   ws->alloc_tracker = radv_amdgpu_alloc_tracker_acquire(ac_drm_device_get_cookie(dev));
   if (!ws->alloc_tracker) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto winsys_fail;
   }

   memcpy(&ws->info, info, sizeof(ws->info));

   ws->chain_ib = !(debug_flags & RADV_DEBUG_NO_IB_CHAINING);
   ws->debug_all_bos = !!(debug_flags & RADV_DEBUG_ALL_BOS);
   ws->debug_log_bos = debug_flags & RADV_DEBUG_HANG;
   ws->dump_ibs = !!(debug_flags & RADV_DEBUG_DUMP_IBS);

   if (debug_flags & RADV_DEBUG_DUMP_BO_HISTORY) {
      ws->bo_history_logfile = fopen("/tmp/radv_bo_history.log", "w+");
      if (!ws->bo_history_logfile)
         fprintf(stderr, "radv/amdgpu: Failed to create /tmp/radv_bo_history.log.\n");
   }

   if (ac_drm_cs_create_syncobj2(ws->dev, 0, &ws->vm_timeline_syncobj))
      goto winsys_fail;

   simple_mtx_init(&ws->vm_ioctl_lock, mtx_plain);

   ws->perftest = perftest_flags;
   ws->zero_all_vram_allocs = debug_flags & RADV_DEBUG_ZERO_VRAM;
   ws->debug_vm = debug_flags & RADV_DEBUG_VM;
   u_rwlock_init(&ws->global_bo_list.lock);
   list_inithead(&ws->log_bo_list);
   u_rwlock_init(&ws->log_bo_list_lock);
   ws->base.query_value = radv_amdgpu_winsys_query_value;
   ws->base.read_registers = radv_amdgpu_winsys_read_registers;
   ws->base.query_gpuvm_fault = radv_amdgpu_winsys_query_gpuvm_fault;
   ws->base.destroy = radv_amdgpu_winsys_destroy;
   ws->base.get_fd = radv_amdgpu_winsys_get_fd;
   ws->base.get_sync_provider = radv_amdgpu_winsys_get_sync_provider;
   ws->base.copy_sync_payloads = vk_drm_syncobj_copy_payloads;
   ws->base.reserve_vmid = radv_amdgpu_winsys_reserve_vmid;
   ws->base.unreserve_vmid = radv_amdgpu_winsys_unreserve_vmid;
   radv_amdgpu_bo_init_functions(ws);
   radv_amdgpu_cs_init_functions(ws);

   if (ws->info.compiler_info.has_smem_with_null_prt_bug)
      simple_mtx_init(&ws->null_prt_bug.lock, mtx_plain);

   *winsys = &ws->base;

   return result;

winsys_fail:
   if (ws->alloc_tracker)
      radv_amdgpu_alloc_tracker_release(ws->alloc_tracker);
   free(ws);
fail:
   ac_drm_device_deinitialize(dev);
   return result;
}

static VkResult
radv_amdgpu_ctx_is_priority_permitted(ac_drm_device *dev, enum radeon_ctx_priority priority)
{
   uint32_t amdgpu_priority = radeon_to_amdgpu_priority(priority);
   uint32_t ctx_handle;
   int r;

   r = ac_drm_cs_ctx_create2(dev, amdgpu_priority, &ctx_handle);
   if (r && r == -EACCES) {
      return VK_ERROR_NOT_PERMITTED;
   } else if (r) {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   ac_drm_cs_ctx_free(dev, ctx_handle);
   return VK_SUCCESS;
}

VkResult
radv_amdgpu_winsys_query_info(int fd, uint64_t debug_flags, bool is_virtio, struct radeon_winsys_info *info)
{
   uint32_t drm_major, drm_minor, r;
   VkResult result = VK_SUCCESS;
   ac_drm_device *dev;

   memset(info, 0, sizeof(*info));

   r = ac_drm_device_initialize(fd, is_virtio, &drm_major, &drm_minor, &dev);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to initialize device.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   info->base.drm_major = drm_major;
   info->base.drm_minor = drm_minor;
   info->base.is_virtio = is_virtio;

   enum ac_query_gpu_info_result info_result =
      ac_query_gpu_info(fd, dev, &info->base, true, !(debug_flags & RADV_DEBUG_NO_CACHE_COMPAT));
   if (info_result != AC_QUERY_GPU_INFO_SUCCESS) {
      result = info_result == AC_QUERY_GPU_INFO_FAIL ? VK_ERROR_INITIALIZATION_FAILED : VK_ERROR_INCOMPATIBLE_DRIVER;
      goto fail;
   }

   /*
    * Override the max submits on video queues.
    * If you submit multiple session contexts in the same IB sequence the
    * hardware gets upset as it expects a kernel fence to be emitted to reset
    * the session context in the hardware.
    * Avoid this problem by never submitted more than one IB at a time.
    * This possibly should be fixed in the kernel, and if it is this can be
    * resolved.
    */
   for (enum amd_ip_type ip_type = AMD_IP_UVD; ip_type <= AMD_IP_VCN_ENC; ip_type++)
      info->base.max_submitted_ibs[ip_type] = 1;

   info->base.ip[AMD_IP_SDMA].num_queues = MIN2(info->base.ip[AMD_IP_SDMA].num_queues, MAX_RINGS_PER_TYPE);
   info->base.ip[AMD_IP_COMPUTE].num_queues = MIN2(info->base.ip[AMD_IP_COMPUTE].num_queues, MAX_RINGS_PER_TYPE);

   info->syncobj_sync_type = vk_drm_syncobj_get_type(fd);

   /* Determine which context priorities are supported. */
   for (uint32_t p = RADEON_CTX_PRIORITY_LOW; p <= RADEON_CTX_PRIORITY_REALTIME; p++) {
      if (radv_amdgpu_ctx_is_priority_permitted(dev, p) != VK_SUCCESS)
         continue;

      info->global_priority_mask |= BITFIELD_BIT(p);
   }

fail:
   ac_drm_device_deinitialize(dev);
   return result;
}

int
radv_amdgpu_winsys_query_heap_info(ac_drm_device *dev, struct radeon_winsys_heap_info *heap_info)
{
   struct amdgpu_heap_info heap_vram = {0}, heap_vram_vis = {0}, heap_gtt = {0};
   struct radv_amdgpu_alloc_tracker *alloc_tracker;
   int r;

   memset(heap_info, 0, sizeof(*heap_info));

   alloc_tracker = radv_amdgpu_alloc_tracker_acquire(ac_drm_device_get_cookie(dev));
   if (!alloc_tracker)
      return -1;

   /* Allocated memory for the current process. */
   heap_info->allocated_vram = alloc_tracker->allocated_vram;
   heap_info->allocated_vram_vis = alloc_tracker->allocated_vram_vis;
   heap_info->allocated_gtt = alloc_tracker->allocated_gtt;

   /* VRAM usage. */
   r = ac_drm_query_heap_info(dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &heap_vram);
   if (!r)
      heap_info->vram_usage = heap_vram.heap_usage;

   /* VRAM visible usage. */
   r = ac_drm_query_heap_info(dev, AMDGPU_GEM_DOMAIN_VRAM, AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED, &heap_vram_vis);
   if (!r)
      heap_info->vram_vis_usage = heap_vram_vis.heap_usage;

   /* GTT usage. */
   r = ac_drm_query_heap_info(dev, AMDGPU_GEM_DOMAIN_GTT, 0, &heap_gtt);
   if (!r)
      heap_info->gtt_usage = heap_gtt.heap_usage;

   radv_amdgpu_alloc_tracker_release(alloc_tracker);

   return 0;
}
