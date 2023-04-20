/*
 * Copyright © 2023 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <sys/mman.h>
#include <xf86drm.h>

#include "anv_private.h"

#include "xe/anv_batch_chain.h"

#include "drm-uapi/xe_drm.h"

static uint32_t
xe_gem_create(struct anv_device *device,
              const struct intel_memory_class_instance **regions,
              uint16_t regions_count, uint64_t size,
              enum anv_bo_alloc_flags alloc_flags,
              uint64_t *actual_size)
{
   struct drm_xe_gem_create gem_create = {
     /* From xe_drm.h: If a VM is specified, this BO must:
      * 1. Only ever be bound to that VM.
      * 2. Cannot be exported as a PRIME fd.
      */
     .vm_id = alloc_flags & ANV_BO_ALLOC_EXTERNAL ? 0 : device->vm_id,
     .size = align64(size, device->info->mem_alignment),
     .flags = alloc_flags & ANV_BO_ALLOC_SCANOUT ? XE_GEM_CREATE_FLAG_SCANOUT : 0,
   };
   for (uint16_t i = 0; i < regions_count; i++)
      gem_create.flags |= BITFIELD_BIT(regions[i]->instance);

   if (intel_ioctl(device->fd, DRM_IOCTL_XE_GEM_CREATE, &gem_create))
      return 0;

   *actual_size = gem_create.size;
   return gem_create.handle;
}

static void
xe_gem_close(struct anv_device *device, uint32_t handle)
{
   struct drm_gem_close close = {
      .handle = handle,
   };
   intel_ioctl(device->fd, DRM_IOCTL_GEM_CLOSE, &close);
}

static void *
xe_gem_mmap(struct anv_device *device, struct anv_bo *bo, uint64_t offset,
            uint64_t size, VkMemoryPropertyFlags property_flags)
{
   struct drm_xe_gem_mmap_offset args = {
      .handle = bo->gem_handle,
   };
   if (intel_ioctl(device->fd, DRM_IOCTL_XE_GEM_MMAP_OFFSET, &args))
      return MAP_FAILED;

   return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
               device->fd, args.offset);
}

static inline int
xe_gem_vm_bind_op(struct anv_device *device, struct anv_bo *bo, uint32_t op)
{
   uint32_t syncobj_handle;
   int ret = drmSyncobjCreate(device->fd, 0, &syncobj_handle);

   if (ret)
      return ret;

   struct drm_xe_sync sync = {
      .flags = DRM_XE_SYNC_SYNCOBJ | DRM_XE_SYNC_SIGNAL,
      .handle = syncobj_handle,
   };
   struct drm_xe_vm_bind args = {
      .vm_id = device->vm_id,
      .num_binds = 1,
      .bind.obj = op == XE_VM_BIND_OP_UNMAP ? 0 : bo->gem_handle,
      .bind.obj_offset = 0,
      .bind.range = bo->actual_size,
      .bind.addr = intel_48b_address(bo->offset),
      .bind.op = op,
      .num_syncs = 1,
      .syncs = (uintptr_t)&sync,
   };
   ret = intel_ioctl(device->fd, DRM_IOCTL_XE_VM_BIND, &args);
   if (ret)
      goto bind_error;

   struct drm_syncobj_wait wait = {
      .handles = (uintptr_t)&syncobj_handle,
      .timeout_nsec = INT64_MAX,
      .count_handles = 1,
      .flags = 0,
      .first_signaled = 0,
      .pad = 0,
   };
   intel_ioctl(device->fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait);

bind_error:
   drmSyncobjDestroy(device->fd, syncobj_handle);
   return ret;
}

static int xe_gem_vm_bind(struct anv_device *device, struct anv_bo *bo)
{
   return xe_gem_vm_bind_op(device, bo, XE_VM_BIND_OP_MAP);
}

static int xe_gem_vm_unbind(struct anv_device *device, struct anv_bo *bo)
{
   return xe_gem_vm_bind_op(device, bo, XE_VM_BIND_OP_UNMAP);
}

const struct anv_kmd_backend *
anv_xe_kmd_backend_get(void)
{
   static const struct anv_kmd_backend xe_backend = {
      .gem_create = xe_gem_create,
      .gem_close = xe_gem_close,
      .gem_mmap = xe_gem_mmap,
      .gem_vm_bind = xe_gem_vm_bind,
      .gem_vm_unbind = xe_gem_vm_unbind,
      .execute_simple_batch = xe_execute_simple_batch,
      .queue_exec_locked = xe_queue_exec_locked,
      .queue_exec_trace = xe_queue_exec_utrace_locked,
   };
   return &xe_backend;
}
