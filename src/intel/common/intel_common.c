/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>

#include "dev/intel_debug.h"

#include "intel_common.h"

#include "intel_engine.h"

#include "util/compiler.h"

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

void
intel_compute_engine_async_threads_limit(const struct intel_device_info *devinfo,
                                         uint32_t hw_threads_in_wg,
                                         bool slm_or_barrier_enabled,
                                         uint8_t *ret_pixel_async_compute_thread_limit,
                                         uint8_t *ret_z_pass_async_compute_thread_limit,
                                         uint8_t *ret_np_z_async_throttle_settings)
{
   /* Spec recommended SW values.
    * IMPORTANT: values set to this variables are HW values
    */
   uint8_t pixel_async_compute_thread_limit = 2;
   uint8_t z_pass_async_compute_thread_limit = 0;
   uint8_t np_z_async_throttle_settings = 0;
   bool has_vrt = devinfo->verx10 >= 300 && !INTEL_DEBUG(DEBUG_NO_VRT);

   /* When VRT is enabled async threads limits don't have effect */
   if (!slm_or_barrier_enabled || has_vrt) {
      *ret_pixel_async_compute_thread_limit = pixel_async_compute_thread_limit;
      *ret_z_pass_async_compute_thread_limit = z_pass_async_compute_thread_limit;
      *ret_np_z_async_throttle_settings = np_z_async_throttle_settings;
      return;
   }

   if (devinfo->verx10 >= 200) {
      /* Spec give us a table with Throttle value | SIMD | MAX API threads(LWS).
       * HW threads = MAX API threads(LWS) / SIMD
       */
      switch (hw_threads_in_wg) {
      case 0 ... 2:
         /* Minimum is Max 2 but lets use spec recommended value below */
         FALLTHROUGH;
      case 3 ... 8:
         /* Max 8 */
         pixel_async_compute_thread_limit = 2;
         break;
      case 9 ... 16:
         /* Max 16 */
         pixel_async_compute_thread_limit = 3;
         break;
      case 17 ... 24:
         /* Max 24 */
         pixel_async_compute_thread_limit = 4;
         break;
      case 25 ... 32:
         /* Max 32 */
         pixel_async_compute_thread_limit = 5;
         break;
      case 33 ... 40:
         /* Max 40 */
         pixel_async_compute_thread_limit = 6;
         break;
      case 41 ... 48:
         /* Max 48 */
         pixel_async_compute_thread_limit = 7;
         break;
      default:
         /* No limit applied */
         pixel_async_compute_thread_limit = 0;
      }

      switch (hw_threads_in_wg) {
      case 0 ... 32:
         /* Minimum is Max 32 but lets use spec recommended value below */
         FALLTHROUGH;
      case 33 ... 40:
         /* Minimum is Max 40 but lets use spec recommended value below */
         FALLTHROUGH;
      case 41 ... 48:
         /* Minimum is Max 48 but lets use spec recommended value below */
         FALLTHROUGH;
      case 49 ... 56:
         /* Minimum is Max 56 but lets use spec recommended value below */
         FALLTHROUGH;
      case 57 ... 60:
         /* Max 60 */
         z_pass_async_compute_thread_limit = 0;
         break;
      default:
         /* No limit applied */
         z_pass_async_compute_thread_limit = 1;
      }

      switch (hw_threads_in_wg) {
      case 0 ... 32:
         /* Max 32 */
         np_z_async_throttle_settings = 1;
         break;
      case 33 ... 40:
         /* Max 40 */
         np_z_async_throttle_settings = 2;
         break;
      case 41 ... 48:
         /* Max 48 */
         np_z_async_throttle_settings = 3;
         break;
      default:
         /* Use the same settings as the Pixel shader Async compute setting,
          * for values >= async compute settings disables the limits
          */
         np_z_async_throttle_settings = 0;
      }
   } else {
      switch (hw_threads_in_wg) {
      case 0 ... 4:
         /* Minimum is Max 2 but lets use spec recommended value below */
         FALLTHROUGH;
      case 5 ... 16:
         /* Max 8 */
         pixel_async_compute_thread_limit = 2;
         break;
      case 17 ... 32:
         /* Max 16 */
         pixel_async_compute_thread_limit = 3;
         break;
      case 33 ... 48:
         /* Max 24 */
         pixel_async_compute_thread_limit = 4;
         break;
      case 49 ... 64:
         /* Max 32 */
         pixel_async_compute_thread_limit = 5;
         break;
      case 65 ... 80:
         /* Max 40 */
         pixel_async_compute_thread_limit = 6;
         break;
      case 81 ... 96:
         /* Max 48 */
         pixel_async_compute_thread_limit = 7;
         break;
      default:
         /* No limit applied */
         pixel_async_compute_thread_limit = 0;
      }

      switch (hw_threads_in_wg) {
      case 0 ... 64:
         /* Minimum is Max 32 but lets use spec recommended value below */
         FALLTHROUGH;
      case 65 ... 80:
         /* Minimum is Max 40 but lets use spec recommended value below */
         FALLTHROUGH;
      case 81 ... 96:
         /* Minimum is Max 48 but lets use spec recommended value below */
         FALLTHROUGH;
      case 97 ... 112:
         /* Minimum is Max 56 but lets use spec recommended value below */
         FALLTHROUGH;
      case 113 ... 120:
         /* Max 60 */
         z_pass_async_compute_thread_limit = 0;
         break;
      default:
         /* Max 64/No limit applied */
         z_pass_async_compute_thread_limit = 1;
      }

      switch (hw_threads_in_wg) {
      case 0 ... 64:
         /* Max 32 */
         np_z_async_throttle_settings = 1;
         break;
      case 65 ... 80:
         /* Max 40 */
         np_z_async_throttle_settings = 2;
         break;
      case 81 ... 96:
         /* Max 48 */
         np_z_async_throttle_settings = 3;
         break;
      default:
         /* Use the same settings as the Pixel shader Async compute setting,
          * for values >= async compute settings disables the limits
          */
         np_z_async_throttle_settings = 0;
      }
   }

   assert(np_z_async_throttle_settings != 0 || pixel_async_compute_thread_limit == 0);
   *ret_pixel_async_compute_thread_limit = pixel_async_compute_thread_limit;
   *ret_z_pass_async_compute_thread_limit = z_pass_async_compute_thread_limit;
   *ret_np_z_async_throttle_settings = np_z_async_throttle_settings;
}

int
intel_compute_threads_group_dispatch_size(uint32_t hw_threads_in_wg)
{
   /* Following value calculated based on overdispatch is disabled. In case if
    * compute overdispatch disabled set to 1, then we need to use TG Size 1.
    */
   switch (hw_threads_in_wg) {
   case 0 ... 16:
      return 0;
   case 17 ... 32:
      return 1;
   default:
      return 2;
   }
}
