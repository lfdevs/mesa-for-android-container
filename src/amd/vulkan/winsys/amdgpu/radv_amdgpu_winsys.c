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
#include <string.h>
#include "drm-uapi/amdgpu_drm.h"
#include "ac_linux_drm.h"
#include "ac_surface.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_winsys_public.h"
#include "radv_debug.h"
#include "vk_drm_syncobj.h"
#include "xf86drm.h"

static void
radv_amdgpu_winsys_query_info(struct radeon_winsys *rws, struct radeon_info *gpu_info)
{
   *gpu_info = ((struct radv_amdgpu_winsys *)rws)->info;
}

static uint64_t
radv_amdgpu_winsys_query_value(struct radeon_winsys *rws, enum radeon_value_id value)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   struct amdgpu_heap_info heap = {0};
   uint64_t retval = 0;

   switch (value) {
   case RADEON_ALLOCATED_VRAM:
      return ws->allocated_vram;
   case RADEON_ALLOCATED_VRAM_VIS:
      return ws->allocated_vram_vis;
   case RADEON_ALLOCATED_GTT:
      return ws->allocated_gtt;
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

static simple_mtx_t winsys_creation_mutex = SIMPLE_MTX_INITIALIZER;
static struct hash_table *winsyses = NULL;

static void
radv_amdgpu_winsys_destroy(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   bool destroy = false;

   simple_mtx_lock(&winsys_creation_mutex);
   if (!--ws->refcount) {
      _mesa_hash_table_remove_key(winsyses, (void *)ac_drm_device_get_cookie(ws->dev));

      /* Clean the hashtable up if empty, though there is no
       * empty function. */
      if (_mesa_hash_table_num_entries(winsyses) == 0) {
         _mesa_hash_table_destroy(winsyses, NULL);
         winsyses = NULL;
      }

      destroy = true;
   }
   simple_mtx_unlock(&winsys_creation_mutex);
   if (!destroy)
      return;

   u_rwlock_destroy(&ws->global_bo_list.lock);
   free(ws->global_bo_list.bos);

   ac_drm_cs_destroy_syncobj(ws->dev, ws->vm_timeline_syncobj);
   simple_mtx_destroy(&ws->vm_ioctl_lock);

   if (ws->bo_history_logfile)
      fclose(ws->bo_history_logfile);

   u_rwlock_destroy(&ws->log_bo_list_lock);
   ac_drm_device_deinitialize(ws->dev);
   FREE(rws);
}

static int
radv_amdgpu_winsys_get_fd(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   return ws->fd;
}

static const struct vk_sync_type *const *
radv_amdgpu_winsys_get_sync_types(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   return ws->sync_types;
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

static uint64_t
radv_amdgpu_winsys_filter_perftest_flags(uint64_t perftest_flags)
{
   return perftest_flags &
          (RADV_PERFTEST_NO_GTT_SPILL | RADV_PERFTEST_LOCAL_BOS | RADV_PERFTEST_NO_SAM | RADV_PERFTEST_SAM);
}

VkResult
radv_amdgpu_winsys_create(int fd, uint64_t debug_flags, uint64_t perftest_flags, bool is_virtio,
                          struct radeon_winsys **winsys)
{
   VkResult result = VK_SUCCESS;

   uint32_t drm_major, drm_minor, r;
   ac_drm_device *dev;
   struct radv_amdgpu_winsys *ws = NULL;

   perftest_flags = radv_amdgpu_winsys_filter_perftest_flags(perftest_flags);

   r = ac_drm_device_initialize(fd, is_virtio, &drm_major, &drm_minor, &dev);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to initialize device.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   /* We have to keep this lock till insertion. */
   simple_mtx_lock(&winsys_creation_mutex);
   if (!winsyses)
      winsyses = _mesa_pointer_hash_table_create(NULL);
   if (!winsyses) {
      fprintf(stderr, "radv/amdgpu: failed to alloc winsys hash table.\n");
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   struct hash_entry *entry = _mesa_hash_table_search(winsyses, (void *)ac_drm_device_get_cookie(dev));
   if (entry) {
      ws = (struct radv_amdgpu_winsys *)entry->data;
      ++ws->refcount;
   }

   if (is_virtio && (perftest_flags & RADV_PERFTEST_LOCAL_BOS)) {
      /* virtio doesn't support VM_ALWAYS_VALID, so disable options that requires it. */
      fprintf(stderr, "RADV_PERFTEST=localbos is not supported with virtio.\n");
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (ws) {
      simple_mtx_unlock(&winsys_creation_mutex);
      ac_drm_device_deinitialize(dev);

      /* Check that options don't differ from the existing winsys. */
      if (((debug_flags & RADV_DEBUG_ALL_BOS) && !ws->debug_all_bos) ||
          ((debug_flags & RADV_DEBUG_HANG) && !ws->debug_log_bos) ||
          ((debug_flags & RADV_DEBUG_NO_IB_CHAINING) && ws->chain_ib) || (perftest_flags != ws->perftest)) {
         fprintf(stderr, "radv/amdgpu: Found options that differ from the existing winsys.\n");
         return VK_ERROR_INITIALIZATION_FAILED;
      }

      /* RADV_DEBUG_ZERO_VRAM is the only option that is allowed to be set again. */
      if (debug_flags & RADV_DEBUG_ZERO_VRAM)
         ws->zero_all_vram_allocs = true;

      *winsys = &ws->base;
      return VK_SUCCESS;
   }

   ws = calloc(1, sizeof(struct radv_amdgpu_winsys));
   if (!ws) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   ws->refcount = 1;
   ws->dev = dev;
   ws->fd = ac_drm_device_get_fd(dev);
   ws->info.drm_major = drm_major;
   ws->info.drm_minor = drm_minor;
   ws->info.is_virtio = is_virtio;

   enum ac_query_gpu_info_result info_result = ac_query_gpu_info(fd, ws->dev, &ws->info, true);
   if (info_result != AC_QUERY_GPU_INFO_SUCCESS) {
      result = info_result == AC_QUERY_GPU_INFO_FAIL ? VK_ERROR_INITIALIZATION_FAILED : VK_ERROR_INCOMPATIBLE_DRIVER;
      goto winsys_fail;
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
      ws->info.max_submitted_ibs[ip_type] = 1;

   ws->info.ip[AMD_IP_SDMA].num_queues = MIN2(ws->info.ip[AMD_IP_SDMA].num_queues, MAX_RINGS_PER_TYPE);
   ws->info.ip[AMD_IP_COMPUTE].num_queues = MIN2(ws->info.ip[AMD_IP_COMPUTE].num_queues, MAX_RINGS_PER_TYPE);

   ws->chain_ib = !(debug_flags & RADV_DEBUG_NO_IB_CHAINING);
   ws->debug_all_bos = !!(debug_flags & RADV_DEBUG_ALL_BOS);
   ws->debug_log_bos = debug_flags & RADV_DEBUG_HANG;
   ws->dump_ibs = !!(debug_flags & RADV_DEBUG_DUMP_IBS);

   if (debug_flags & RADV_DEBUG_DUMP_BO_HISTORY) {
      ws->bo_history_logfile = fopen("/tmp/radv_bo_history.log", "w+");
      if (!ws->bo_history_logfile)
         fprintf(stderr, "radv/amdgpu: Failed to create /tmp/radv_bo_history.log.\n");
   }

   int num_sync_types = 0;

   ws->syncobj_sync_type = vk_drm_syncobj_get_type_from_provider(ac_drm_device_get_sync_provider(dev));
   if (ws->syncobj_sync_type.features) {
      if (!ws->info.has_timeline_syncobj && ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE) {
         /* Disable timeline feature if it was disabled in the driver. */
         assert(is_virtio);
         ws->syncobj_sync_type.get_value = NULL;
         ws->syncobj_sync_type.features &= ~VK_SYNC_FEATURE_TIMELINE;
      }

      ws->sync_types[num_sync_types++] = &ws->syncobj_sync_type;
      if (!(ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE)) {
         ws->emulated_timeline_sync_type = vk_sync_timeline_get_type(&ws->syncobj_sync_type);
         ws->sync_types[num_sync_types++] = &ws->emulated_timeline_sync_type.sync;
      }
   }

   ws->sync_types[num_sync_types++] = NULL;
   assert(num_sync_types <= ARRAY_SIZE(ws->sync_types));

   if (ac_drm_cs_create_syncobj2(ws->dev, 0, &ws->vm_timeline_syncobj))
      goto winsys_fail;

   simple_mtx_init(&ws->vm_ioctl_lock, mtx_plain);

   ws->perftest = perftest_flags;
   ws->zero_all_vram_allocs = debug_flags & RADV_DEBUG_ZERO_VRAM;
   u_rwlock_init(&ws->global_bo_list.lock);
   list_inithead(&ws->log_bo_list);
   u_rwlock_init(&ws->log_bo_list_lock);
   ws->base.query_info = radv_amdgpu_winsys_query_info;
   ws->base.query_value = radv_amdgpu_winsys_query_value;
   ws->base.read_registers = radv_amdgpu_winsys_read_registers;
   ws->base.query_gpuvm_fault = radv_amdgpu_winsys_query_gpuvm_fault;
   ws->base.destroy = radv_amdgpu_winsys_destroy;
   ws->base.get_fd = radv_amdgpu_winsys_get_fd;
   ws->base.get_sync_types = radv_amdgpu_winsys_get_sync_types;
   ws->base.get_sync_provider = radv_amdgpu_winsys_get_sync_provider;
   ws->base.copy_sync_payloads = vk_drm_syncobj_copy_payloads;
   ws->base.reserve_vmid = radv_amdgpu_winsys_reserve_vmid;
   ws->base.unreserve_vmid = radv_amdgpu_winsys_unreserve_vmid;
   radv_amdgpu_bo_init_functions(ws);
   radv_amdgpu_cs_init_functions(ws);

   _mesa_hash_table_insert(winsyses, (void *)ac_drm_device_get_cookie(dev), ws);
   simple_mtx_unlock(&winsys_creation_mutex);

   *winsys = &ws->base;

   return result;

winsys_fail:
   free(ws);
fail:
   if (winsyses && _mesa_hash_table_num_entries(winsyses) == 0) {
      _mesa_hash_table_destroy(winsyses, NULL);
      winsyses = NULL;
   }
   simple_mtx_unlock(&winsys_creation_mutex);
   ac_drm_device_deinitialize(dev);
   return result;
}
