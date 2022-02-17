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

int
intel_gem_count_engines(const struct drm_i915_query_engine_info *info,
                        enum drm_i915_gem_engine_class engine_class)
{
   assert(info != NULL);
   int count = 0;
   for (int i = 0; i < info->num_engines; i++) {
      if (info->engines[i].engine.engine_class == engine_class)
         count++;
   }
   return count;
}

int
intel_gem_create_context_engines(int fd,
                                 const struct drm_i915_query_engine_info *info,
                                 int num_engines, uint16_t *engine_classes)
{
   assert(info != NULL);
   const size_t engine_inst_sz = 2 * sizeof(__u16); /* 1 class, 1 instance */
   const size_t engines_param_size =
      sizeof(__u64) /* extensions */ + num_engines * engine_inst_sz;

   void *engines_param = malloc(engines_param_size);
   assert(engines_param);
   *(__u64*)engines_param = 0;
   __u16 *class_inst_ptr = (__u16*)(((__u64*)engines_param) + 1);

   /* For each type of drm_i915_gem_engine_class of interest, we keep track of
    * the previous engine instance used.
    */
   int last_engine_idx[] = {
      [I915_ENGINE_CLASS_RENDER] = -1,
      [I915_ENGINE_CLASS_COPY] = -1,
   };

   int i915_engine_counts[] = {
      [I915_ENGINE_CLASS_RENDER] =
         intel_gem_count_engines(info, I915_ENGINE_CLASS_RENDER),
      [I915_ENGINE_CLASS_COPY] =
         intel_gem_count_engines(info, I915_ENGINE_CLASS_COPY),
   };

   /* For each queue, we look for the next instance that matches the class we
    * need.
    */
   for (int i = 0; i < num_engines; i++) {
      uint16_t engine_class = engine_classes[i];
      assert(engine_class == I915_ENGINE_CLASS_RENDER ||
             engine_class == I915_ENGINE_CLASS_COPY);
      if (i915_engine_counts[engine_class] <= 0) {
         free(engines_param);
         return -1;
      }

      /* Run through the engines reported by the kernel looking for the next
       * matching instance. We loop in case we want to create multiple
       * contexts on an engine instance.
       */
      int engine_instance = -1;
      for (int i = 0; i < info->num_engines; i++) {
         int *idx = &last_engine_idx[engine_class];
         if (++(*idx) >= info->num_engines)
            *idx = 0;
         if (info->engines[*idx].engine.engine_class == engine_class) {
            engine_instance = info->engines[*idx].engine.engine_instance;
            break;
         }
      }
      if (engine_instance < 0) {
         free(engines_param);
         return -1;
      }

      *class_inst_ptr++ = engine_class;
      *class_inst_ptr++ = engine_instance;
   }

   assert((uintptr_t)engines_param + engines_param_size ==
          (uintptr_t)class_inst_ptr);

   struct drm_i915_gem_context_create_ext_setparam set_engines = {
      .base = {
         .name = I915_CONTEXT_CREATE_EXT_SETPARAM,
      },
      .param = {
         .param = I915_CONTEXT_PARAM_ENGINES,
         .value = (uintptr_t)engines_param,
         .size = engines_param_size,
      }
   };
   struct drm_i915_gem_context_create_ext create = {
      .flags = I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS,
      .extensions = (uintptr_t)&set_engines,
   };
   int ret = intel_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT, &create);
   free(engines_param);
   if (ret == -1)
      return -1;

   return create.ctx_id;
}
