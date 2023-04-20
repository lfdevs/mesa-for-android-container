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
#include "iris_kmd_backend.h"

#include <sys/mman.h>

#include "common/intel_gem.h"
#include "dev/intel_debug.h"
#include "iris/iris_bufmgr.h"

#include "drm-uapi/xe_drm.h"

#define FILE_DEBUG_FLAG DEBUG_BUFMGR

static uint32_t
xe_gem_create(struct iris_bufmgr *bufmgr,
              const struct intel_memory_class_instance **regions,
              uint16_t regions_count, uint64_t size,
              enum iris_heap heap_flags, unsigned alloc_flags)
{
   /* Xe still don't have support for protected content */
   if (alloc_flags & BO_ALLOC_PROTECTED)
      return -EINVAL;

   uint32_t vm_id = iris_bufmgr_get_global_vm_id(bufmgr);
   vm_id = alloc_flags & BO_ALLOC_SHARED ? 0 : vm_id;

   struct drm_xe_gem_create gem_create = {
     .vm_id = vm_id,
     .size = align64(size, iris_bufmgr_get_device_info(bufmgr)->mem_alignment),
     /* TODO: we might need to consider scanout for shared buffers too as we
      * do not know what the process this is shared with will do with it
      */
     .flags = alloc_flags & BO_ALLOC_SCANOUT ? XE_GEM_CREATE_FLAG_SCANOUT : 0,
   };
   for (uint16_t i = 0; i < regions_count; i++)
      gem_create.flags |= BITFIELD_BIT(regions[i]->instance);

   if (intel_ioctl(iris_bufmgr_get_fd(bufmgr), DRM_IOCTL_XE_GEM_CREATE,
                   &gem_create))
      return 0;

   return gem_create.handle;
}

static void *
xe_gem_mmap(struct iris_bufmgr *bufmgr, struct iris_bo *bo)
{
   struct drm_xe_gem_mmap_offset args = {
      .handle = bo->gem_handle,
   };
   if (intel_ioctl(iris_bufmgr_get_fd(bufmgr), DRM_IOCTL_XE_GEM_MMAP_OFFSET, &args))
      return NULL;

   void *map = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    iris_bufmgr_get_fd(bufmgr), args.offset);
   return map != MAP_FAILED ? map : NULL;
}

static inline int
xe_gem_vm_bind_op(struct iris_bo *bo, uint32_t op)
{
   struct drm_syncobj_create create = {};
   int ret = intel_ioctl(iris_bufmgr_get_fd(bo->bufmgr),
                         DRM_IOCTL_SYNCOBJ_CREATE, &create);
   if (ret) {
      DBG("vm_bind_op: Unable to create SYNCOBJ(%i)", ret);
      return ret;
   }

   struct drm_xe_sync sync = {
      .flags = DRM_XE_SYNC_SYNCOBJ | DRM_XE_SYNC_SIGNAL,
      .handle = create.handle,
   };
   /* Old compilers do not allow declarations after 'goto label' */
   struct drm_syncobj_destroy destroy = {
      .handle = create.handle,
   };

   uint32_t handle = op == XE_VM_BIND_OP_UNMAP ? 0 : bo->gem_handle;
   uint64_t obj_offset = 0;

   if (bo->real.userptr) {
      handle = 0;
      obj_offset = (uintptr_t)bo->real.map;
      if (op == XE_VM_BIND_OP_MAP)
         op = XE_VM_BIND_OP_MAP_USERPTR;
   }

   struct drm_xe_vm_bind args = {
      .vm_id = iris_bufmgr_get_global_vm_id(bo->bufmgr),
      .num_binds = 1,
      .bind.obj = handle,
      .bind.obj_offset = obj_offset,
      .bind.range = align64(bo->size,
                            iris_bufmgr_get_device_info(bo->bufmgr)->mem_alignment),
      .bind.addr = intel_48b_address(bo->address),
      .bind.op = op,
      .num_syncs = 1,
      .syncs = (uintptr_t)&sync,
   };
   ret = intel_ioctl(iris_bufmgr_get_fd(bo->bufmgr), DRM_IOCTL_XE_VM_BIND, &args);
   if (ret) {
      DBG("vm_bind_op: DRM_IOCTL_XE_VM_BIND failed(%i)", ret);
      goto bind_error;
   }

   struct drm_syncobj_wait wait = {
      .handles = (uintptr_t)&create.handle,
      .timeout_nsec = INT64_MAX,
      .count_handles = 1,
      .flags = 0,
      .first_signaled = 0,
      .pad = 0,
   };
   ret = intel_ioctl(iris_bufmgr_get_fd(bo->bufmgr), DRM_IOCTL_SYNCOBJ_WAIT, &wait);
   if (ret)
      DBG("vm_bind_op: DRM_IOCTL_SYNCOBJ_WAIT failed(%i)", ret);

bind_error:
   if (intel_ioctl(iris_bufmgr_get_fd(bo->bufmgr), DRM_IOCTL_SYNCOBJ_DESTROY, &destroy))
      DBG("vm_bind_op: Unable to destroy SYNCOBJ(%i)", ret);
   return ret;
}

static bool
xe_gem_vm_bind(struct iris_bo *bo)
{
   return xe_gem_vm_bind_op(bo, XE_VM_BIND_OP_MAP) == 0;
}

static bool
xe_gem_vm_unbind(struct iris_bo *bo)
{
   return xe_gem_vm_bind_op(bo, XE_VM_BIND_OP_UNMAP) == 0;
}

static bool
xe_bo_madvise(struct iris_bo *bo, enum iris_madvice state)
{
   /* Only applicable if VM was created with DRM_XE_VM_CREATE_FAULT_MODE but
    * that is not compatible with DRM_XE_VM_CREATE_SCRATCH_PAGE
    *
    * So returning as retained.
    */
   return true;
}

static int
xe_bo_set_caching(struct iris_bo *bo, bool cached)
{
   /* Xe don't have caching UAPI so this function should never be called */
   assert(0);
   return -1;
}

const struct iris_kmd_backend *xe_get_backend(void)
{
   static const struct iris_kmd_backend xe_backend = {
      .gem_create = xe_gem_create,
      .gem_mmap = xe_gem_mmap,
      .gem_vm_bind = xe_gem_vm_bind,
      .gem_vm_unbind = xe_gem_vm_unbind,
      .bo_madvise = xe_bo_madvise,
      .bo_set_caching = xe_bo_set_caching,
   };
   return &xe_backend;
}
