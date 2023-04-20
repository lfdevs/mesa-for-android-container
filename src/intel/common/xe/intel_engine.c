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
#include "xe/intel_engine.h"

#include <stdlib.h>

#include "common/intel_gem.h"

#include "drm-uapi/xe_drm.h"

static enum intel_engine_class
xe_engine_class_to_intel(uint16_t xe)
{
   switch (xe) {
   case DRM_XE_ENGINE_CLASS_RENDER:
      return INTEL_ENGINE_CLASS_RENDER;
   case DRM_XE_ENGINE_CLASS_COPY:
      return INTEL_ENGINE_CLASS_COPY;
   case DRM_XE_ENGINE_CLASS_VIDEO_DECODE:
      return INTEL_ENGINE_CLASS_VIDEO;
   case DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE:
      return INTEL_ENGINE_CLASS_VIDEO_ENHANCE;
   case DRM_XE_ENGINE_CLASS_COMPUTE:
      return INTEL_ENGINE_CLASS_COMPUTE;
   default:
      return INTEL_ENGINE_CLASS_INVALID;
   }
}

uint16_t
intel_engine_class_to_xe(enum intel_engine_class intel)
{
   switch (intel) {
   case INTEL_ENGINE_CLASS_RENDER:
      return DRM_XE_ENGINE_CLASS_RENDER;
   case INTEL_ENGINE_CLASS_COPY:
      return DRM_XE_ENGINE_CLASS_COPY;
   case INTEL_ENGINE_CLASS_VIDEO:
      return DRM_XE_ENGINE_CLASS_VIDEO_DECODE;
   case INTEL_ENGINE_CLASS_VIDEO_ENHANCE:
      return DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE;
   case INTEL_ENGINE_CLASS_COMPUTE:
      return DRM_XE_ENGINE_CLASS_COMPUTE;
   default:
      return -1;
   }
}

struct intel_query_engine_info *
xe_engine_get_info(int fd)
{
   struct drm_xe_device_query query = {
      .query = DRM_XE_DEVICE_QUERY_ENGINES,
   };
   if (intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query))
      return NULL;

   struct drm_xe_engine_class_instance *xe_engines = calloc(1, query.size);
   if (!xe_engines)
      return NULL;

   query.data = (uintptr_t)xe_engines;
   if (intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query))
      goto error_free_xe_engines;

   const uint32_t engines_count = query.size / sizeof(*xe_engines);
   struct intel_query_engine_info *intel_engines_info;
   intel_engines_info = calloc(1, sizeof(*intel_engines_info) +
                               sizeof(*intel_engines_info->engines) *
                               engines_count);
   if (!intel_engines_info) {
      goto error_free_xe_engines;
      return NULL;
   }

   for (uint32_t i = 0; i < engines_count; i++) {
      struct drm_xe_engine_class_instance *xe_engine = &xe_engines[i];
      struct intel_engine_class_instance *intel_engine = &intel_engines_info->engines[i];

      intel_engine->engine_class = xe_engine_class_to_intel(xe_engine->engine_class);
      intel_engine->engine_instance = xe_engine->engine_instance;
   }

   intel_engines_info->num_engines = engines_count;
   free(xe_engines);
   return intel_engines_info;

error_free_xe_engines:
   free(xe_engines);
   return NULL;
}
