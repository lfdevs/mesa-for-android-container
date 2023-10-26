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

#include "common/xe/intel_engine.h"

#include "anv_private.h"

#include "xe/anv_batch_chain.h"

#include "drm-uapi/gpu_scheduler.h"
#include "drm-uapi/xe_drm.h"

static uint32_t
xe_gem_create(struct anv_device *device,
              const struct intel_memory_class_instance **regions,
              uint16_t regions_count, uint64_t size,
              enum anv_bo_alloc_flags alloc_flags,
              uint64_t *actual_size)
{
   uint32_t flags = 0;
   if (alloc_flags & ANV_BO_ALLOC_SCANOUT)
      flags |= XE_GEM_CREATE_FLAG_SCANOUT;
   if ((alloc_flags & (ANV_BO_ALLOC_MAPPED | ANV_BO_ALLOC_LOCAL_MEM_CPU_VISIBLE)) &&
       !(alloc_flags & ANV_BO_ALLOC_NO_LOCAL_MEM) &&
       device->physical->vram_non_mappable.size > 0)
      flags |= XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM;

   struct drm_xe_gem_create gem_create = {
     /* From xe_drm.h: If a VM is specified, this BO must:
      * 1. Only ever be bound to that VM.
      * 2. Cannot be exported as a PRIME fd.
      */
     .vm_id = alloc_flags & ANV_BO_ALLOC_EXTERNAL ? 0 : device->vm_id,
     .size = align64(size, device->info->mem_alignment),
     .flags = flags,
   };
   for (uint16_t i = 0; i < regions_count; i++)
      gem_create.flags |= BITFIELD_BIT(regions[i]->instance);

   if (intel_ioctl(device->fd, DRM_IOCTL_XE_GEM_CREATE, &gem_create))
      return 0;

   *actual_size = gem_create.size;
   return gem_create.handle;
}

static void
xe_gem_close(struct anv_device *device, struct anv_bo *bo)
{
   if (bo->from_host_ptr)
      return;

   struct drm_gem_close close = {
      .handle = bo->gem_handle,
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
xe_vm_bind_op(struct anv_device *device, int num_binds,
              struct anv_vm_bind *binds)
{
   int ret;

   struct drm_xe_vm_bind args = {
      .vm_id = device->vm_id,
      .num_binds = num_binds,
      .bind = {},
   };

   STACK_ARRAY(struct drm_xe_vm_bind_op, xe_binds_stackarray, num_binds);
   struct drm_xe_vm_bind_op *xe_binds;
   if (num_binds > 1) {
      if (!xe_binds_stackarray)
         return -ENOMEM;

      xe_binds = xe_binds_stackarray;
      args.vector_of_binds = (uintptr_t)xe_binds;
   } else {
      xe_binds = &args.bind;
   }

   for (int i = 0; i < num_binds; i++) {
      struct anv_vm_bind *bind = &binds[i];
      struct anv_bo *bo = bind->bo;

      struct drm_xe_vm_bind_op *xe_bind = &xe_binds[i];
      *xe_bind = (struct drm_xe_vm_bind_op) {
         .obj = 0,
         .obj_offset = bind->bo_offset,
         .range = bind->size,
         .addr = intel_48b_address(bind->address),
         .tile_mask = 0,
         .op = XE_VM_BIND_OP_UNMAP,
         .flags = 0,
         .region = 0,
      };

      if (bind->op == ANV_VM_BIND) {
         if (!bo) {
            xe_bind->op = XE_VM_BIND_OP_MAP;
            xe_bind->flags |= XE_VM_BIND_FLAG_NULL;
            assert(xe_bind->obj_offset == 0);
         } else if (bo->from_host_ptr) {
            xe_bind->op = XE_VM_BIND_OP_MAP_USERPTR;
         } else {
            xe_bind->op = XE_VM_BIND_OP_MAP;
            xe_bind->obj = bo->gem_handle;
         }
      }

      /* userptr and bo_offset are an union! */
      if (bo && bo->from_host_ptr)
         xe_bind->userptr = (uintptr_t)bo->map;
   }

   ret = intel_ioctl(device->fd, DRM_IOCTL_XE_VM_BIND, &args);
   STACK_ARRAY_FINISH(xe_binds_stackarray);

   return ret;
}

static int
xe_vm_bind(struct anv_device *device, int num_binds,
           struct anv_vm_bind *binds)
{
   return xe_vm_bind_op(device, num_binds, binds);
}

static int xe_vm_bind_bo(struct anv_device *device, struct anv_bo *bo)
{
   struct anv_vm_bind bind = {
      .bo = bo,
      .address = bo->offset,
      .bo_offset = 0,
      .size = bo->actual_size,
      .op = ANV_VM_BIND,
   };
   return xe_vm_bind_op(device, 1, &bind);
}

static int xe_vm_unbind_bo(struct anv_device *device, struct anv_bo *bo)
{
   struct anv_vm_bind bind = {
      .bo = bo,
      .address = bo->offset,
      .bo_offset = 0,
      .size = bo->actual_size,
      .op = ANV_VM_UNBIND,
   };
   return xe_vm_bind_op(device, 1, &bind);
}

static uint32_t
xe_gem_create_userptr(struct anv_device *device, void *mem, uint64_t size)
{
   /* We return the workaround BO gem_handle here, because Xe doesn't
    * create handles for userptrs. But we still need to make it look
    * to the rest of Anv that the operation succeeded.
    */
   return device->workaround_bo->gem_handle;
}

static uint32_t
xe_bo_alloc_flags_to_bo_flags(struct anv_device *device,
                              enum anv_bo_alloc_flags alloc_flags)
{
   return 0;
}

const struct anv_kmd_backend *
anv_xe_kmd_backend_get(void)
{
   static const struct anv_kmd_backend xe_backend = {
      .gem_create = xe_gem_create,
      .gem_create_userptr = xe_gem_create_userptr,
      .gem_close = xe_gem_close,
      .gem_mmap = xe_gem_mmap,
      .vm_bind = xe_vm_bind,
      .vm_bind_bo = xe_vm_bind_bo,
      .vm_unbind_bo = xe_vm_unbind_bo,
      .execute_simple_batch = xe_execute_simple_batch,
      .queue_exec_locked = xe_queue_exec_locked,
      .queue_exec_trace = xe_queue_exec_utrace_locked,
      .bo_alloc_flags_to_bo_flags = xe_bo_alloc_flags_to_bo_flags,
   };
   return &xe_backend;
}
