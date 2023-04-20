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
#include "xe/intel_gem.h"

#include "common/intel_gem.h"

#include "drm-uapi/xe_drm.h"

bool
xe_gem_read_render_timestamp(int fd, uint64_t *value)
{
   struct drm_xe_mmio mmio = {
      .addr = RCS_TIMESTAMP,
      .flags = DRM_XE_MMIO_READ | DRM_XE_MMIO_64BIT,
   };
   if (intel_ioctl(fd, DRM_IOCTL_XE_MMIO, &mmio))
      return false;

   *value = mmio.value;
   return true;
}

bool
xe_gem_can_render_on_fd(int fd)
{
   struct drm_xe_device_query query = {
      .query = DRM_XE_DEVICE_QUERY_ENGINES,
   };
   return intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query) == 0;
}
