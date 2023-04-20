/*
 * Copyright © 2015 Intel Corporation
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

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"
#include "common/intel_gem.h"

void *
anv_gem_mmap(struct anv_device *device, struct anv_bo *bo, uint64_t offset,
             uint64_t size, VkMemoryPropertyFlags property_flags)
{
   void *map = device->kmd_backend->gem_mmap(device, bo, offset, size,
                                             property_flags);

   if (map != MAP_FAILED)
      VG(VALGRIND_MALLOCLIKE_BLOCK(map, size, 0, 1));

   return map;
}

/* This is just a wrapper around munmap, but it also notifies valgrind that
 * this map is no longer valid.  Pair this with gem_mmap().
 */
void
anv_gem_munmap(struct anv_device *device, void *p, uint64_t size)
{
   VG(VALGRIND_FREELIKE_BLOCK(p, 0));
   munmap(p, size);
}

uint32_t
anv_gem_userptr(struct anv_device *device, void *mem, size_t size)
{
   struct drm_i915_gem_userptr userptr = {
      .user_ptr = (__u64)((unsigned long) mem),
      .user_size = size,
      .flags = 0,
   };

   if (device->physical->info.has_userptr_probe)
      userptr.flags |= I915_USERPTR_PROBE;

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_USERPTR, &userptr);
   if (ret == -1)
      return 0;

   return userptr.handle;
}

int
anv_gem_set_caching(struct anv_device *device,
                    uint32_t gem_handle, uint32_t caching)
{
   /* Guard by has_caching_uapi */
   if (unlikely(device->info->kmd_type != INTEL_KMD_TYPE_I915)) {
      assert(!"Missing implementation of anv_gem_set_caching\n");
      return -1;
   }

   struct drm_i915_gem_caching gem_caching = {
      .handle = gem_handle,
      .caching = caching,
   };

   return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_SET_CACHING, &gem_caching);
}

/**
 * On error, \a timeout_ns holds the remaining time.
 */
int
anv_gem_wait(struct anv_device *device, uint32_t gem_handle, int64_t *timeout_ns)
{
   /* Only called from i915 code path and from anv_bo_sync that is not
    * supported in Xe
    */
   if (unlikely(device->info->kmd_type != INTEL_KMD_TYPE_I915)) {
      assert(!"Missing implementation of anv_gem_wait\n");
      return -1;
   }

   struct drm_i915_gem_wait wait = {
      .bo_handle = gem_handle,
      .timeout_ns = *timeout_ns,
      .flags = 0,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_WAIT, &wait);
   *timeout_ns = wait.timeout_ns;

   return ret;
}

/** Return -1 on error. */
int
anv_gem_get_tiling(struct anv_device *device, uint32_t gem_handle)
{
   if (!device->info->has_tiling_uapi)
      return -1;

   struct drm_i915_gem_get_tiling get_tiling = {
      .handle = gem_handle,
   };

   /* FIXME: On discrete platforms we don't have DRM_IOCTL_I915_GEM_GET_TILING
    * anymore, so we will need another way to get the tiling. Apparently this
    * is only used in Android code, so we may need some other way to
    * communicate the tiling mode.
    */
   if (intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_GET_TILING, &get_tiling)) {
      assert(!"Failed to get BO tiling");
      return -1;
   }

   return get_tiling.tiling_mode;
}

int
anv_gem_set_tiling(struct anv_device *device,
                   uint32_t gem_handle, uint32_t stride, uint32_t tiling)
{
   int ret;

   /* On discrete platforms we don't have DRM_IOCTL_I915_GEM_SET_TILING. So
    * nothing needs to be done.
    */
   if (!device->info->has_tiling_uapi)
      return 0;

   /* set_tiling overwrites the input on the error path, so we have to open
    * code intel_ioctl.
    */
   do {
      struct drm_i915_gem_set_tiling set_tiling = {
         .handle = gem_handle,
         .tiling_mode = tiling,
         .stride = stride,
      };

      ret = ioctl(device->fd, DRM_IOCTL_I915_GEM_SET_TILING, &set_tiling);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret;
}

int
anv_gem_handle_to_fd(struct anv_device *device, uint32_t gem_handle)
{
   struct drm_prime_handle args = {
      .handle = gem_handle,
      .flags = DRM_CLOEXEC | DRM_RDWR,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
   if (ret == -1)
      return -1;

   return args.fd;
}

uint32_t
anv_gem_fd_to_handle(struct anv_device *device, int fd)
{
   struct drm_prime_handle args = {
      .fd = fd,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &args);
   if (ret == -1)
      return 0;

   return args.handle;
}

const struct anv_kmd_backend *anv_stub_kmd_backend_get(void)
{
   return NULL;
}
