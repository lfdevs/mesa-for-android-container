/**************************************************************************
 *
 * Copyright 2010 Thomas Balling Sørensen & Orasanu Lucian.
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "pipe/p_screen.h"
#include "pipe/p_video_codec.h"
#include "util/u_memory.h"
#include "util/u_handle_table.h"
#include "util/u_video.h"
#include "vl/vl_deint_filter.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

#include <va/va_drmcommon.h>

static struct VADriverVTable vtable =
{
   &vlVaTerminate,
   &vlVaQueryConfigProfiles,
   &vlVaQueryConfigEntrypoints,
   &vlVaGetConfigAttributes,
   &vlVaCreateConfig,
   &vlVaDestroyConfig,
   &vlVaQueryConfigAttributes,
   &vlVaCreateSurfaces,
   &vlVaDestroySurfaces,
   &vlVaCreateContext,
   &vlVaDestroyContext,
   &vlVaCreateBuffer,
   &vlVaBufferSetNumElements,
   &vlVaMapBuffer,
   &vlVaUnmapBuffer,
   &vlVaDestroyBuffer,
   &vlVaBeginPicture,
   &vlVaRenderPicture,
   &vlVaEndPicture,
   &vlVaSyncSurface,
   &vlVaQuerySurfaceStatus,
   &vlVaQuerySurfaceError,
   &vlVaPutSurface,
   &vlVaQueryImageFormats,
   &vlVaCreateImage,
   &vlVaDeriveImage,
   &vlVaDestroyImage,
   &vlVaSetImagePalette,
   &vlVaGetImage,
   &vlVaPutImage,
   &vlVaQuerySubpictureFormats,
   &vlVaCreateSubpicture,
   &vlVaDestroySubpicture,
   &vlVaSubpictureImage,
   &vlVaSetSubpictureChromakey,
   &vlVaSetSubpictureGlobalAlpha,
   &vlVaAssociateSubpicture,
   &vlVaDeassociateSubpicture,
   &vlVaQueryDisplayAttributes,
   &vlVaGetDisplayAttributes,
   &vlVaSetDisplayAttributes,
   &vlVaBufferInfo,
   &vlVaLockSurface,
   &vlVaUnlockSurface,
   NULL, /* DEPRECATED VaGetSurfaceAttributes */
   &vlVaCreateSurfaces2,
   &vlVaQuerySurfaceAttributes,
   &vlVaAcquireBufferHandle,
   &vlVaReleaseBufferHandle
};

static struct VADriverVTableVPP vtable_vpp =
{
   1,
   &vlVaQueryVideoProcFilters,
   &vlVaQueryVideoProcFilterCaps,
   &vlVaQueryVideoProcPipelineCaps
};

PUBLIC VAStatus
VA_DRIVER_INIT_FUNC(VADriverContextP ctx)
{
   vlVaDriver *drv;
   struct drm_state *drm_info;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = CALLOC(1, sizeof(vlVaDriver));
   if (!drv)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   switch (ctx->display_type) {
   case VA_DISPLAY_ANDROID:
      FREE(drv);
      return VA_STATUS_ERROR_UNIMPLEMENTED;
   case VA_DISPLAY_GLX:
   case VA_DISPLAY_X11:
#if defined(HAVE_DRI3)
      drv->vscreen = vl_dri3_screen_create(ctx->native_dpy, ctx->x11_screen);
#endif
      if (!drv->vscreen)
         drv->vscreen = vl_dri2_screen_create(ctx->native_dpy, ctx->x11_screen);
      if (!drv->vscreen)
         goto error_screen;
      break;
   case VA_DISPLAY_WAYLAND:
   case VA_DISPLAY_DRM:
   case VA_DISPLAY_DRM_RENDERNODES: {
      drm_info = (struct drm_state *) ctx->drm_state;

      if (!drm_info || drm_info->fd < 0) {
         FREE(drv);
         return VA_STATUS_ERROR_INVALID_PARAMETER;
      }

      drv->vscreen = vl_drm_screen_create(drm_info->fd);
      if (!drv->vscreen)
         goto error_screen;
      }
      break;
   default:
      FREE(drv);
      return VA_STATUS_ERROR_INVALID_DISPLAY;
   }

   drv->pipe = drv->vscreen->pscreen->context_create(drv->vscreen->pscreen,
                                                     drv->vscreen, 0);
   if (!drv->pipe)
      goto error_pipe;

   drv->htab = handle_table_create();
   if (!drv->htab)
      goto error_htab;

   vl_compositor_init(&drv->compositor, drv->pipe);
   vl_compositor_init_state(&drv->cstate, drv->pipe);

   vl_csc_get_matrix(VL_CSC_COLOR_STANDARD_BT_601, NULL, true, &drv->csc);
   vl_compositor_set_csc_matrix(&drv->cstate, (const vl_csc_matrix *)&drv->csc);
   pipe_mutex_init(drv->mutex);

   ctx->pDriverData = (void *)drv;
   ctx->version_major = 0;
   ctx->version_minor = 1;
   *ctx->vtable = vtable;
   *ctx->vtable_vpp = vtable_vpp;
   ctx->max_profiles = PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH - PIPE_VIDEO_PROFILE_UNKNOWN;
   ctx->max_entrypoints = 1;
   ctx->max_attributes = 1;
   ctx->max_image_formats = VL_VA_MAX_IMAGE_FORMATS;
   ctx->max_subpic_formats = 1;
   ctx->max_display_attributes = 1;
   ctx->str_vendor = "mesa gallium vaapi";

   return VA_STATUS_SUCCESS;

error_htab:
   drv->pipe->destroy(drv->pipe);

error_pipe:
   drv->vscreen->destroy(drv->vscreen);

error_screen:
   FREE(drv);
   return VA_STATUS_ERROR_ALLOCATION_FAILED;
}

VAStatus
vlVaCreateContext(VADriverContextP ctx, VAConfigID config_id, int picture_width,
                  int picture_height, int flag, VASurfaceID *render_targets,
                  int num_render_targets, VAContextID *context_id)
{
   vlVaDriver *drv;
   vlVaContext *context;
   int is_vpp;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   is_vpp = config_id == PIPE_VIDEO_PROFILE_UNKNOWN && !picture_width &&
            !picture_height && !flag && !render_targets && !num_render_targets;

   if (!(picture_width && picture_height) && !is_vpp)
      return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;

   drv = VL_VA_DRIVER(ctx);
   context = CALLOC(1, sizeof(vlVaContext));
   if (!context)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   if (is_vpp) {
      context->decoder = NULL;
      if (!drv->compositor.upload) {
         FREE(context);
         return VA_STATUS_ERROR_INVALID_CONTEXT;
      }
   } else {
      context->templat.profile = config_id;
      context->templat.entrypoint = PIPE_VIDEO_ENTRYPOINT_BITSTREAM;
      context->templat.chroma_format = PIPE_VIDEO_CHROMA_FORMAT_420;
      context->templat.width = picture_width;
      context->templat.height = picture_height;
      context->templat.expect_chunked_decode = true;

      switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_MPEG12:
      case PIPE_VIDEO_FORMAT_VC1:
      case PIPE_VIDEO_FORMAT_MPEG4:
         context->templat.max_references = 2;
         break;

      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         context->templat.max_references = 0;
         context->desc.h264.pps = CALLOC_STRUCT(pipe_h264_pps);
         if (!context->desc.h264.pps) {
            FREE(context);
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
         }
         context->desc.h264.pps->sps = CALLOC_STRUCT(pipe_h264_sps);
         if (!context->desc.h264.pps->sps) {
            FREE(context->desc.h264.pps);
            FREE(context);
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
         }
         break;

     case PIPE_VIDEO_FORMAT_HEVC:
         context->templat.max_references = num_render_targets;
         context->desc.h265.pps = CALLOC_STRUCT(pipe_h265_pps);
         if (!context->desc.h265.pps) {
            FREE(context);
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
         }
         context->desc.h265.pps->sps = CALLOC_STRUCT(pipe_h265_sps);
         if (!context->desc.h265.pps->sps) {
            FREE(context->desc.h265.pps);
            FREE(context);
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
         }
         break;

      default:
         break;
      }
   }

   context->desc.base.profile = config_id;
   pipe_mutex_lock(drv->mutex);
   *context_id = handle_table_add(drv->htab, context);
   pipe_mutex_unlock(drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaDestroyContext(VADriverContextP ctx, VAContextID context_id)
{
   vlVaDriver *drv;
   vlVaContext *context;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   pipe_mutex_lock(drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (context->decoder) {
      if (u_reduce_video_profile(context->decoder->profile) ==
            PIPE_VIDEO_FORMAT_MPEG4_AVC) {
         FREE(context->desc.h264.pps->sps);
         FREE(context->desc.h264.pps);
      }
      if (u_reduce_video_profile(context->decoder->profile) ==
            PIPE_VIDEO_FORMAT_HEVC) {
         FREE(context->desc.h265.pps->sps);
         FREE(context->desc.h265.pps);
      }
      context->decoder->destroy(context->decoder);
   }
   if (context->deint) {
      vl_deint_filter_cleanup(context->deint);
      FREE(context->deint);
   }
   FREE(context);
   handle_table_remove(drv->htab, context_id);
   pipe_mutex_unlock(drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaTerminate(VADriverContextP ctx)
{
   vlVaDriver *drv;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = ctx->pDriverData;
   vl_compositor_cleanup_state(&drv->cstate);
   vl_compositor_cleanup(&drv->compositor);
   drv->pipe->destroy(drv->pipe);
   drv->vscreen->destroy(drv->vscreen);
   handle_table_destroy(drv->htab);
   pipe_mutex_destroy(drv->mutex);
   FREE(drv);

   return VA_STATUS_SUCCESS;
}
