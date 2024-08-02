/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>

#include "intel_common.h"

#include "intel_engine.h"

/* Updates intel_device_info fields that has dependencies on intel/common
 * functions.
 */
void intel_common_update_device_info(int fd, struct intel_device_info *devinfo)
{
   struct intel_query_engine_info *engine_info;
   enum intel_engine_class klass;

   engine_info = intel_engine_get_info(fd, devinfo->kmd_type);
   if (!engine_info)
      return;

   devinfo->has_compute_engine = intel_engines_count(engine_info,
                                                     INTEL_ENGINE_CLASS_COMPUTE);

   for (klass = 0; klass < INTEL_ENGINE_CLASS_INVALID; klass++)
      devinfo->engine_class_supported_count[klass] =
         intel_engines_supported_count(fd, devinfo, engine_info, klass);

   free(engine_info);
}
