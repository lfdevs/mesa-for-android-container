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

#include "anv_private.h"

#include "i915/anv_batch_chain.h"

#include "drm-uapi/i915_drm.h"

static uint32_t
i915_gem_create(struct anv_device *device,
                const struct intel_memory_class_instance **regions,
                uint16_t num_regions, uint64_t size,
                enum anv_bo_alloc_flags alloc_flags,
                uint64_t *actual_size)
{
   if (unlikely(!device->info->mem.use_class_instance)) {
      assert(num_regions == 1 &&
             device->physical->sys.region == regions[0]);

      struct drm_i915_gem_create gem_create = {
            .size = size,
      };
      if (intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CREATE, &gem_create))
         return 0;

      *actual_size = gem_create.size;
      return gem_create.handle;
   }

   struct drm_i915_gem_memory_class_instance i915_regions[2];
   assert(num_regions <= ARRAY_SIZE(i915_regions));

   for (uint16_t i = 0; i < num_regions; i++) {
      i915_regions[i].memory_class = regions[i]->klass;
      i915_regions[i].memory_instance = regions[i]->instance;
   }

   uint32_t flags = 0;
   if (alloc_flags & (ANV_BO_ALLOC_MAPPED | ANV_BO_ALLOC_LOCAL_MEM_CPU_VISIBLE) &&
       !(alloc_flags & ANV_BO_ALLOC_NO_LOCAL_MEM))
      if (device->physical->vram_non_mappable.size > 0)
         flags |= I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS;

   struct drm_i915_gem_create_ext_memory_regions ext_regions = {
      .base = { .name = I915_GEM_CREATE_EXT_MEMORY_REGIONS },
      .num_regions = num_regions,
      .regions = (uintptr_t)i915_regions,
   };
   struct drm_i915_gem_create_ext gem_create = {
      .size = size,
      .extensions = (uintptr_t) &ext_regions,
      .flags = flags,
   };

   if (intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CREATE_EXT, &gem_create))
      return 0;

   *actual_size = gem_create.size;
   return gem_create.handle;
}

static void
i915_gem_close(struct anv_device *device, uint32_t handle)
{
   struct drm_gem_close close = {
      .handle = handle,
   };

   intel_ioctl(device->fd, DRM_IOCTL_GEM_CLOSE, &close);
}

static void *
i915_gem_mmap_offset(struct anv_device *device, struct anv_bo *bo,
                     uint64_t size, uint32_t flags)
{
   struct drm_i915_gem_mmap_offset gem_mmap = {
      .handle = bo->gem_handle,
      .flags = flags,
   };
   if (intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_MMAP_OFFSET, &gem_mmap))
      return MAP_FAILED;

   return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
               device->fd, gem_mmap.offset);
}

static void *
i915_gem_mmap_legacy(struct anv_device *device, struct anv_bo *bo, uint64_t offset,
                      uint64_t size, uint32_t flags)
{
   struct drm_i915_gem_mmap gem_mmap = {
      .handle = bo->gem_handle,
      .offset = offset,
      .size = size,
      .flags = flags,
   };
   if (intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_MMAP, &gem_mmap))
      return MAP_FAILED;

   return (void *)(uintptr_t) gem_mmap.addr_ptr;
}

static uint32_t
mmap_calc_flags(struct anv_device *device, struct anv_bo *bo,
                VkMemoryPropertyFlags property_flags)
{
   if (device->info->has_local_mem)
      return I915_MMAP_OFFSET_FIXED;

   uint32_t flags = 0;
   if (!device->info->has_llc &&
       (property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      flags |= I915_MMAP_WC;
   if (bo->map_wc)
      flags |= I915_MMAP_WC;
   if (!(property_flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
      flags |= I915_MMAP_WC;

   if (likely(device->physical->info.has_mmap_offset))
      flags = (flags & I915_MMAP_WC) ? I915_MMAP_OFFSET_WC : I915_MMAP_OFFSET_WB;
   return flags;
}

static void *
i915_gem_mmap(struct anv_device *device, struct anv_bo *bo, uint64_t offset,
              uint64_t size, VkMemoryPropertyFlags property_flags)
{
   const uint32_t flags = mmap_calc_flags(device, bo, property_flags);

   if (likely(device->physical->info.has_mmap_offset))
      return i915_gem_mmap_offset(device, bo, size, flags);
   return i915_gem_mmap_legacy(device, bo, offset, size, flags);
}

static int
i915_gem_vm_bind(struct anv_device *device, struct anv_bo *bo)
{
   return 0;
}

static int
i915_gem_vm_unbind(struct anv_device *device, struct anv_bo *bo)
{
   return 0;
}

const struct anv_kmd_backend *
anv_i915_kmd_backend_get(void)
{
   static const struct anv_kmd_backend i915_backend = {
      .gem_create = i915_gem_create,
      .gem_close = i915_gem_close,
      .gem_mmap = i915_gem_mmap,
      .gem_vm_bind = i915_gem_vm_bind,
      .gem_vm_unbind = i915_gem_vm_unbind,
      .execute_simple_batch = i915_execute_simple_batch,
      .queue_exec_locked = i915_queue_exec_locked,
      .queue_exec_trace = i915_queue_exec_trace,
   };
   return &i915_backend;
}
