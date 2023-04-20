/*
 * Copyright © 2020 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "intel_gem.h"
#include "drm-uapi/i915_drm.h"

#include "i915/intel_engine.h"
#include "i915/intel_gem.h"
#include "xe/intel_gem.h"

bool
intel_gem_supports_syncobj_wait(int fd)
{
   int ret;

   struct drm_syncobj_create create = {
      .flags = 0,
   };
   ret = intel_ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
   if (ret)
      return false;

   uint32_t syncobj = create.handle;

   struct drm_syncobj_wait wait = {
      .handles = (uint64_t)(uintptr_t)&create,
      .count_handles = 1,
      .timeout_nsec = 0,
      .flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
   };
   ret = intel_ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait);

   struct drm_syncobj_destroy destroy = {
      .handle = syncobj,
   };
   intel_ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);

   /* If it timed out, then we have the ioctl and it supports the
    * DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT flag.
    */
   return ret == -1 && errno == ETIME;
}

bool
intel_gem_create_context(int fd, uint32_t *context_id)
{
   return i915_gem_create_context(fd, context_id);
}

bool
intel_gem_destroy_context(int fd, uint32_t context_id)
{
   return i915_gem_destroy_context(fd, context_id);
}

bool
intel_gem_create_context_engines(int fd,
                                 const struct intel_query_engine_info *info,
                                 int num_engines, enum intel_engine_class *engine_classes,
                                 uint32_t *context_id)
{
   return i915_gem_create_context_engines(fd, info, num_engines,
                                          engine_classes, context_id);
}

bool
intel_gem_set_context_param(int fd, uint32_t context, uint32_t param,
                            uint64_t value)
{
   return i915_gem_set_context_param(fd, context, param, value);
}

bool
intel_gem_get_context_param(int fd, uint32_t context, uint32_t param,
                            uint64_t *value)
{
   return i915_gem_get_context_param(fd, context, param, value);
}

bool
intel_gem_read_render_timestamp(int fd,
                                enum intel_kmd_type kmd_type,
                                uint64_t *value)
{
   switch (kmd_type) {
   case INTEL_KMD_TYPE_I915:
      return i915_gem_read_render_timestamp(fd, value);
   case INTEL_KMD_TYPE_XE:
      return xe_gem_read_render_timestamp(fd, value);
   default:
      unreachable("Missing");
      return false;
   }
}

bool
intel_gem_create_context_ext(int fd, enum intel_gem_create_context_flags flags,
                             uint32_t *ctx_id)
{
   return i915_gem_create_context_ext(fd, flags, ctx_id);
}

bool
intel_gem_supports_protected_context(int fd, enum intel_kmd_type kmd_type)
{
   switch (kmd_type) {
   case INTEL_KMD_TYPE_I915:
      return i915_gem_supports_protected_context(fd);
   case INTEL_KMD_TYPE_XE:
      /* TODO: so far Xe don't have support for protected contexts/engines */
      return false;
   default:
      unreachable("Missing");
      return false;
   }
}

bool
intel_gem_get_param(int fd, uint32_t param, int *value)
{
   return i915_gem_get_param(fd, param, value);
}

bool
intel_gem_can_render_on_fd(int fd, enum intel_kmd_type kmd_type)
{
   switch (kmd_type) {
   case INTEL_KMD_TYPE_I915:
      return i915_gem_can_render_on_fd(fd);
   case INTEL_KMD_TYPE_XE:
      return xe_gem_can_render_on_fd(fd);
   default:
      unreachable("Missing");
      return false;
   }
}
