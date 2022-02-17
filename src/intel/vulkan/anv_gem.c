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
#include "common/intel_defines.h"
#include "common/intel_gem.h"

/**
 * Wrapper around DRM_IOCTL_I915_GEM_CREATE.
 *
 * Return gem handle, or 0 on failure. Gem handles are never 0.
 */
uint32_t
anv_gem_create(struct anv_device *device, uint64_t size)
{
   struct drm_i915_gem_create gem_create = {
      .size = size,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CREATE, &gem_create);
   if (ret != 0) {
      /* FIXME: What do we do if this fails? */
      return 0;
   }

   return gem_create.handle;
}

void
anv_gem_close(struct anv_device *device, uint32_t gem_handle)
{
   struct drm_gem_close close = {
      .handle = gem_handle,
   };

   intel_ioctl(device->fd, DRM_IOCTL_GEM_CLOSE, &close);
}

uint32_t
anv_gem_create_regions(struct anv_device *device, uint64_t anv_bo_size,
                       uint32_t num_regions,
                       struct drm_i915_gem_memory_class_instance *regions)
{
   struct drm_i915_gem_create_ext_memory_regions ext_regions = {
      .base = { .name = I915_GEM_CREATE_EXT_MEMORY_REGIONS },
      .num_regions = num_regions,
      .regions = (uintptr_t)regions,
   };

   struct drm_i915_gem_create_ext gem_create = {
      .size = anv_bo_size,
      .extensions = (uintptr_t) &ext_regions,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CREATE_EXT,
                         &gem_create);
   if (ret != 0) {
      return 0;
   }

   return gem_create.handle;
}

/**
 * Wrapper around DRM_IOCTL_I915_GEM_MMAP. Returns MAP_FAILED on error.
 */
static void*
anv_gem_mmap_offset(struct anv_device *device, uint32_t gem_handle,
                    uint64_t offset, uint64_t size, uint32_t flags)
{
   struct drm_i915_gem_mmap_offset gem_mmap = {
      .handle = gem_handle,
      .flags = device->info.has_local_mem ? I915_MMAP_OFFSET_FIXED :
         (flags & I915_MMAP_WC) ? I915_MMAP_OFFSET_WC : I915_MMAP_OFFSET_WB,
   };
   assert(offset == 0);

   /* Get the fake offset back */
   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_MMAP_OFFSET, &gem_mmap);
   if (ret != 0)
      return MAP_FAILED;

   /* And map it */
   void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    device->fd, gem_mmap.offset);
   return map;
}

static void*
anv_gem_mmap_legacy(struct anv_device *device, uint32_t gem_handle,
                    uint64_t offset, uint64_t size, uint32_t flags)
{
   assert(!device->info.has_local_mem);

   struct drm_i915_gem_mmap gem_mmap = {
      .handle = gem_handle,
      .offset = offset,
      .size = size,
      .flags = flags,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_MMAP, &gem_mmap);
   if (ret != 0)
      return MAP_FAILED;

   return (void *)(uintptr_t) gem_mmap.addr_ptr;
}

/**
 * Wrapper around DRM_IOCTL_I915_GEM_MMAP. Returns MAP_FAILED on error.
 */
void*
anv_gem_mmap(struct anv_device *device, uint32_t gem_handle,
             uint64_t offset, uint64_t size, uint32_t flags)
{
   void *map;
   if (device->physical->has_mmap_offset)
      map = anv_gem_mmap_offset(device, gem_handle, offset, size, flags);
   else
      map = anv_gem_mmap_legacy(device, gem_handle, offset, size, flags);

   if (map != MAP_FAILED)
      VG(VALGRIND_MALLOCLIKE_BLOCK(map, size, 0, 1));

   return map;
}

/* This is just a wrapper around munmap, but it also notifies valgrind that
 * this map is no longer valid.  Pair this with anv_gem_mmap().
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

   if (device->physical->has_userptr_probe)
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
   struct drm_i915_gem_caching gem_caching = {
      .handle = gem_handle,
      .caching = caching,
   };

   return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_SET_CACHING, &gem_caching);
}

int
anv_gem_set_domain(struct anv_device *device, uint32_t gem_handle,
                   uint32_t read_domains, uint32_t write_domain)
{
   struct drm_i915_gem_set_domain gem_set_domain = {
      .handle = gem_handle,
      .read_domains = read_domains,
      .write_domain = write_domain,
   };

   return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &gem_set_domain);
}

/**
 * Returns 0, 1, or negative to indicate error
 */
int
anv_gem_busy(struct anv_device *device, uint32_t gem_handle)
{
   struct drm_i915_gem_busy busy = {
      .handle = gem_handle,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_BUSY, &busy);
   if (ret < 0)
      return ret;

   return busy.busy != 0;
}

/**
 * On error, \a timeout_ns holds the remaining time.
 */
int
anv_gem_wait(struct anv_device *device, uint32_t gem_handle, int64_t *timeout_ns)
{
   struct drm_i915_gem_wait wait = {
      .bo_handle = gem_handle,
      .timeout_ns = *timeout_ns,
      .flags = 0,
   };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_WAIT, &wait);
   *timeout_ns = wait.timeout_ns;

   return ret;
}

int
anv_gem_execbuffer(struct anv_device *device,
                   struct drm_i915_gem_execbuffer2 *execbuf)
{
   if (execbuf->flags & I915_EXEC_FENCE_OUT)
      return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_EXECBUFFER2_WR, execbuf);
   else
      return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf);
}

/** Return -1 on error. */
int
anv_gem_get_tiling(struct anv_device *device, uint32_t gem_handle)
{
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
   if (!device->info.has_tiling_uapi)
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
anv_gem_get_param(int fd, uint32_t param)
{
   int tmp;

   drm_i915_getparam_t gp = {
      .param = param,
      .value = &tmp,
   };

   int ret = intel_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
   if (ret == 0)
      return tmp;

   return 0;
}

bool
anv_gem_has_context_priority(int fd, int priority)
{
   return !anv_gem_set_context_param(fd, 0, I915_CONTEXT_PARAM_PRIORITY,
                                     priority);
}

int
anv_gem_create_context(struct anv_device *device)
{
   struct drm_i915_gem_context_create create = { 0 };

   int ret = intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &create);
   if (ret == -1)
      return -1;

   return create.ctx_id;
}

int
anv_gem_destroy_context(struct anv_device *device, int context)
{
   struct drm_i915_gem_context_destroy destroy = {
      .ctx_id = context,
   };

   return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &destroy);
}

int
anv_gem_set_context_param(int fd, int context, uint32_t param, uint64_t value)
{
   struct drm_i915_gem_context_param p = {
      .ctx_id = context,
      .param = param,
      .value = value,
   };
   int err = 0;

   if (intel_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM, &p))
      err = -errno;
   return err;
}

int
anv_gem_context_get_reset_stats(int fd, int context,
                                uint32_t *active, uint32_t *pending)
{
   struct drm_i915_reset_stats stats = {
      .ctx_id = context,
   };

   int ret = intel_ioctl(fd, DRM_IOCTL_I915_GET_RESET_STATS, &stats);
   if (ret == 0) {
      *active = stats.batch_active;
      *pending = stats.batch_pending;
   }

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

int
anv_gem_reg_read(int fd, uint32_t offset, uint64_t *result)
{
   struct drm_i915_reg_read args = {
      .offset = offset
   };

   int ret = intel_ioctl(fd, DRM_IOCTL_I915_REG_READ, &args);

   *result = args.val;
   return ret;
}

struct drm_i915_query_engine_info *
anv_gem_get_engine_info(int fd)
{
   return intel_i915_query_alloc(fd, DRM_I915_QUERY_ENGINE_INFO, NULL);
}
