/*
 * Mesa 3-D graphics library
 *
 * Copyright © 2021, Google Inc.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <hardware/gralloc.h>

#include "util/log.h"
#include "util/u_memory.h"

#include "u_gralloc_internal.h"

/* The following defines are selectively copied over from auto-generated
 * AIDL sources which are no longer released as headers in VNDK from api
 * level 35 on.
 * These need to be kept in sync with AIDL sources and additional
 * defines to be added as needed.
 */
enum cros_gralloc0_dataspace {
   DATASPACE_STANDARD_MASK = 63 << 16,
   DATASPACE_STANDARD_BT709 = 1 << 16,
   DATASPACE_STANDARD_BT601_625 = 2 << 16,
   DATASPACE_STANDARD_BT601_625_UNADJUSTED = 3 << 16,
   DATASPACE_STANDARD_BT601_525 = 4 << 16,
   DATASPACE_STANDARD_BT601_525_UNADJUSTED = 5 << 16,
   DATASPACE_STANDARD_BT2020 = 6 << 16,
   DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE = 7 << 16,
   DATASPACE_RANGE_MASK= 7 << 27,
   DATASPACE_RANGE_FULL = 1 << 27,
   DATASPACE_RANGE_LIMITED = 2 << 27,
};

enum cros_gralloc0_chroma_siting {
   CHROMA_SITING_SITED_NONE = 0,
   CHROMA_SITING_SITED_UNKNOWN = 1,
   CHROMA_SITING_SITED_INTERSTITIAL = 2,
   CHROMA_SITING_COSITED_HORIZONTAL = 3,
   CHROMA_SITING_COSITED_VERTICAL = 4,
   CHROMA_SITING_COSITED_BOTH = 5,
};

/* More recent CrOS gralloc has a perform op that fills out the struct below
 * with canonical information about the buffer and its modifier, planes,
 * offsets and strides.  If we have this, we can skip straight to
 * createImageFromDmaBufs2() and avoid all the guessing and recalculations.
 * This also gives us the modifier and plane offsets/strides for multiplanar
 * compressed buffers (eg Intel CCS buffers) in order to make that work in
 * Android.
 */

struct cros_gralloc {
   struct u_gralloc base;
   gralloc_module_t *gralloc_module;
};

static const char cros_gralloc_module_name[] = "CrOS Gralloc";

#define CROS_GRALLOC_DRM_GET_BUFFER_INFO               4
#define CROS_GRALLOC_DRM_GET_USAGE                     5
#define CROS_GRALLOC_DRM_GET_BUFFER_COLOR_INFO         6
#define CROS_GRALLOC_DRM_GET_USAGE_FRONT_RENDERING_BIT 0x1

struct cros_gralloc0_buffer_info {
   uint32_t drm_fourcc;
   int num_fds;
   int fds[4];
   uint64_t modifier;
   int offset[4];
   int stride[4];
};

struct cros_gralloc0_buffer_color_info {
   int32_t dataspace;
   int32_t chroma_siting;
};

static int
cros_get_buffer_info(struct u_gralloc *gralloc,
                     struct u_gralloc_buffer_handle *hnd,
                     struct u_gralloc_buffer_basic_info *out)
{
   struct cros_gralloc0_buffer_info info;
   struct cros_gralloc *gr = (struct cros_gralloc *)gralloc;
   gralloc_module_t *gr_mod = gr->gralloc_module;

   if (gr_mod->perform(gr_mod, CROS_GRALLOC_DRM_GET_BUFFER_INFO, hnd->handle,
                       &info) == 0) {
      out->drm_fourcc = info.drm_fourcc;
      out->modifier = info.modifier;
      out->num_planes = info.num_fds;
      for (int i = 0; i < out->num_planes; i++) {
         out->fds[i] = info.fds[i];
         out->offsets[i] = info.offset[i];
         out->strides[i] = info.stride[i];
      }

      return 0;
   }

   return -EINVAL;
}

static int
cros_get_front_rendering_usage(struct u_gralloc *gralloc, uint64_t *out_usage)
{
   struct cros_gralloc *gr = (struct cros_gralloc *)gralloc;
   uint32_t front_rendering_usage = 0;

   if (gr->gralloc_module->perform(
          gr->gralloc_module, CROS_GRALLOC_DRM_GET_USAGE,
          CROS_GRALLOC_DRM_GET_USAGE_FRONT_RENDERING_BIT,
          &front_rendering_usage) == 0) {
      *out_usage = front_rendering_usage;
      return 0;
   }

   return -ENOTSUP;
}

static int
cros_get_buffer_color_info(struct u_gralloc * gralloc,
                           struct u_gralloc_buffer_handle *hnd,
                           struct u_gralloc_buffer_color_info *out)
{
   struct cros_gralloc0_buffer_color_info color_info;
   struct cros_gralloc *gr = (struct cros_gralloc *)gralloc;
   gralloc_module_t *gr_mod = gr->gralloc_module;

   if (gr_mod->perform(gr_mod, CROS_GRALLOC_DRM_GET_BUFFER_COLOR_INFO, hnd->handle,
                       &color_info)) {
      /* Return default values if CROS_GRALLOC_DRM_GET_BUFFER_COLOR_INFO
       * fails or is not implemented for backwards compat.
       */
      *out = (struct u_gralloc_buffer_color_info){
         .yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC601,
         .sample_range = __DRI_YUV_NARROW_RANGE,
         .horizontal_siting = __DRI_YUV_CHROMA_SITING_0_5,
         .vertical_siting = __DRI_YUV_CHROMA_SITING_0_5,
      };
      return 0;
   }

   /* default to __DRI_YUV_COLOR_SPACE_ITU_REC601 */
   int32_t standard = color_info.dataspace & DATASPACE_STANDARD_MASK;
   switch (standard) {
   case DATASPACE_STANDARD_BT709:
      out->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC709;
      break;
   case DATASPACE_STANDARD_BT2020:
   case DATASPACE_STANDARD_BT2020_CONSTANT_LUMINANCE:
      out->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC2020;
      break;
   case DATASPACE_STANDARD_BT601_625:
   case DATASPACE_STANDARD_BT601_625_UNADJUSTED:
   case DATASPACE_STANDARD_BT601_525:
   case DATASPACE_STANDARD_BT601_525_UNADJUSTED:
   default:
      out->yuv_color_space = __DRI_YUV_COLOR_SPACE_ITU_REC601;
      break;
   }

   /* default to __DRI_YUV_NARROW_RANGE */
   int32_t range = color_info.dataspace & DATASPACE_RANGE_MASK;
   switch (range) {
   case DATASPACE_RANGE_FULL:
      out->sample_range = __DRI_YUV_FULL_RANGE;
      break;
   case DATASPACE_RANGE_LIMITED:
   default:
      out->sample_range = __DRI_YUV_NARROW_RANGE;
      break;
   }

   /* default to __DRI_YUV_CHROMA_SITING_0_5 */
   switch (color_info.chroma_siting) {
   case CHROMA_SITING_COSITED_HORIZONTAL:
      out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0;
      out->vertical_siting = __DRI_YUV_CHROMA_SITING_0_5;
      break;
   case CHROMA_SITING_COSITED_VERTICAL:
      out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0_5;
      out->vertical_siting = __DRI_YUV_CHROMA_SITING_0;
      break;
   case CHROMA_SITING_COSITED_BOTH:
      out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0;
      out->vertical_siting = __DRI_YUV_CHROMA_SITING_0;
      break;
   case CHROMA_SITING_SITED_INTERSTITIAL:
   default:
      out->horizontal_siting = __DRI_YUV_CHROMA_SITING_0_5;
      out->vertical_siting = __DRI_YUV_CHROMA_SITING_0_5;
      break;
   }

   return 0;
}

static int
destroy(struct u_gralloc *gralloc)
{
   struct cros_gralloc *gr = (struct cros_gralloc *)gralloc;
   if (gr->gralloc_module)
      dlclose(gr->gralloc_module->common.dso);

   FREE(gr);

   return 0;
}

struct u_gralloc *
u_gralloc_cros_api_create()
{
   struct cros_gralloc *gr = CALLOC_STRUCT(cros_gralloc);
   int err = 0;

   err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                       (const hw_module_t **)&gr->gralloc_module);

   if (err)
      goto fail;

   if (strcmp(gr->gralloc_module->common.name, cros_gralloc_module_name) != 0)
      goto fail;

   if (!gr->gralloc_module->perform) {
      mesa_logw("Oops. CrOS gralloc doesn't have perform callback");
      goto fail;
   }

   gr->base.ops.get_buffer_basic_info = cros_get_buffer_info;
   gr->base.ops.get_buffer_color_info = cros_get_buffer_color_info;
   gr->base.ops.get_front_rendering_usage = cros_get_front_rendering_usage;
   gr->base.ops.destroy = destroy;

   mesa_logi("Using gralloc0 CrOS API");

   return &gr->base;

fail:
   destroy(&gr->base);

   return NULL;
}
