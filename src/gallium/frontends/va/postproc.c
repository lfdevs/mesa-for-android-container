/**************************************************************************
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "util/u_handle_table.h"
#include "util/u_memory.h"

#include "vl/vl_defines.h"
#include "vl/vl_video_buffer.h"
#include "vl/vl_deint_filter.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

static VAProcColorStandardType vpp_color_standards[] = {
   VAProcColorStandardBT601,
   VAProcColorStandardBT709,
   VAProcColorStandardBT2020,
   VAProcColorStandardExplicit
};

VAStatus
vlVaQueryVideoProcPipelineCaps(VADriverContextP ctx, VAContextID context,
                               VABufferID *filters, unsigned int num_filters,
                               VAProcPipelineCaps *pipeline_cap)
{
   unsigned int i = 0;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!pipeline_cap)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (num_filters && !filters)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   pipeline_cap->pipeline_flags = 0;
   pipeline_cap->filter_flags = 0;
   pipeline_cap->num_forward_references = 0;
   pipeline_cap->num_backward_references = 0;
   pipeline_cap->rotation_flags = VA_ROTATION_NONE;
   pipeline_cap->mirror_flags = VA_MIRROR_NONE;

   struct pipe_screen *pscreen = VL_VA_PSCREEN(ctx);
   bool media_only = !pscreen->caps.graphics && !pscreen->caps.compute;
   uint32_t pipe_orientation_flags = pscreen->get_video_param(pscreen,
                                                              PIPE_VIDEO_PROFILE_UNKNOWN,
                                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                              PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES);

   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_ROTATION_90)
      pipeline_cap->rotation_flags |= (1 << VA_ROTATION_90);
   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_ROTATION_180)
      pipeline_cap->rotation_flags |= (1 << VA_ROTATION_180);
   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_ROTATION_270)
      pipeline_cap->rotation_flags |= (1 << VA_ROTATION_270);

   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_FLIP_HORIZONTAL)
      pipeline_cap->mirror_flags |= VA_MIRROR_HORIZONTAL;
   if (!media_only || pipe_orientation_flags & PIPE_VIDEO_VPP_FLIP_VERTICAL)
      pipeline_cap->mirror_flags |= VA_MIRROR_VERTICAL;

   pipeline_cap->input_color_standards = vpp_color_standards;
   pipeline_cap->num_input_color_standards = ARRAY_SIZE(vpp_color_standards);
   pipeline_cap->output_color_standards = vpp_color_standards;
   pipeline_cap->num_output_color_standards = ARRAY_SIZE(vpp_color_standards);

   pipeline_cap->max_input_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                            PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                            PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH);

   pipeline_cap->max_input_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT);

   pipeline_cap->min_input_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                            PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                            PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH);

   pipeline_cap->min_input_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT);

   pipeline_cap->max_output_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH);

   pipeline_cap->max_output_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                              PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT);

   pipeline_cap->min_output_width = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                             PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                             PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH);

   pipeline_cap->min_output_height = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                              PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT);

   uint32_t pipe_blend_modes = pscreen->get_video_param(pscreen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                                        PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                                        PIPE_VIDEO_CAP_VPP_BLEND_MODES);

   pipeline_cap->blend_flags = 0;
   if (pipe_blend_modes & PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA)
      pipeline_cap->blend_flags |= VA_BLEND_GLOBAL_ALPHA;

   vlVaDriver *drv = VL_VA_DRIVER(ctx);

   mtx_lock(&drv->mutex);
   for (i = 0; i < num_filters; i++) {
      vlVaBuffer *buf = handle_table_get(drv->htab, filters[i]);
      VAProcFilterParameterBufferBase *filter;

      if (!buf || buf->type != VAProcFilterParameterBufferType) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      filter = buf->data;
      switch (filter->type) {
      case VAProcFilterDeinterlacing: {
         VAProcFilterParameterBufferDeinterlacing *deint = buf->data;
         if (deint->algorithm == VAProcDeinterlacingMotionAdaptive) {
            pipeline_cap->num_forward_references = 2;
            pipeline_cap->num_backward_references = 1;
         }
         break;
      }
      default:
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_UNIMPLEMENTED;
      }
   }
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaQueryVideoProcFilters(VADriverContextP ctx, VAContextID context_id,
                          VAProcFilterType *filters, unsigned int *num_filters)
{
   vlVaDriver *drv = VL_VA_DRIVER(ctx);
   vlVaContext *context;
   unsigned int num = 0;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!num_filters || !filters)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE &&
       context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      filters[num++] = VAProcFilterDeinterlacing;

   mtx_unlock(&drv->mutex);

   *num_filters = num;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaQueryVideoProcFilterCaps(VADriverContextP ctx, VAContextID context_id,
                             VAProcFilterType type, void *filter_caps,
                             unsigned int *num_filter_caps)
{
   vlVaDriver *drv = VL_VA_DRIVER(ctx);
   vlVaContext *context;
   unsigned int i;
   bool supports_filters;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!filter_caps || !num_filter_caps)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   supports_filters = context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE &&
                      context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM;

   mtx_unlock(&drv->mutex);

   i = 0;

   switch (type) {
   case VAProcFilterNone:
      break;
   case VAProcFilterDeinterlacing: {
      VAProcFilterCapDeinterlacing *deint = filter_caps;

      if (!supports_filters)
         return VA_STATUS_ERROR_UNIMPLEMENTED;

      if (*num_filter_caps < 3) {
         *num_filter_caps = 3;
         return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
      }

      deint[i++].type = VAProcDeinterlacingBob;
      deint[i++].type = VAProcDeinterlacingWeave;
      deint[i++].type = VAProcDeinterlacingMotionAdaptive;
      break;
   }

   case VAProcFilterNoiseReduction:
   case VAProcFilterSharpening:
   case VAProcFilterColorBalance:
   case VAProcFilterSkinToneEnhancement:
      return VA_STATUS_ERROR_UNIMPLEMENTED;
   default:
      assert(0);
   }

   *num_filter_caps = i;

   return VA_STATUS_SUCCESS;
}

static const VARectangle *
vlVaRegionDefault(const VARectangle *region, vlVaSurface *surf,
                  VARectangle *def)
{
   if (region)
      return region;

   def->x = 0;
   def->y = 0;
   def->width = surf->templat.width;
   def->height = surf->templat.height;

   return def;
}

VAStatus
vlVaPostProcCompositor(vlVaDriver *drv,
                       struct pipe_video_buffer *src,
                       struct pipe_video_buffer *dst,
                       enum vl_compositor_deinterlace deinterlace,
                       struct pipe_vpp_desc *param)
{
   struct pipe_surface *surfaces;
   enum vl_compositor_rotation rotation;
   enum vl_compositor_mirror mirror;
   bool src_yuv = util_format_is_yuv(src->buffer_format);
   bool dst_yuv = util_format_is_yuv(dst->buffer_format);

   if (!drv->cstate.pipe)
      return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;

   /* Subsampled formats not supported */
   if (util_format_is_subsampled_422(dst->buffer_format))
      return VA_STATUS_ERROR_UNIMPLEMENTED;

   surfaces = dst->get_surfaces(dst);
   if (!surfaces[0].texture)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   if (util_format_get_nr_components(src->buffer_format) == 1) {
      /* Identity */
      vl_csc_get_rgbyuv_matrix(PIPE_VIDEO_VPP_MCF_RGB, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &drv->cstate.yuv2rgb);
      vl_csc_get_rgbyuv_matrix(PIPE_VIDEO_VPP_MCF_RGB, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &drv->cstate.rgb2yuv);
   } else if (src_yuv == dst_yuv) {
      if (!src_yuv) {
         /* RGB to RGB */
         vl_csc_get_rgbyuv_matrix(PIPE_VIDEO_VPP_MCF_RGB, src->buffer_format, dst->buffer_format,
                                  param->in_color_range, param->out_color_range, &drv->cstate.yuv2rgb);
      } else {
         /* YUV to YUV (convert to RGB for transfer function and primaries) */
         enum pipe_format rgb_format = PIPE_FORMAT_B8G8R8A8_UNORM;
         vl_csc_get_rgbyuv_matrix(param->in_matrix_coefficients, src->buffer_format, rgb_format,
                                  param->in_color_range, PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL,
                                  &drv->cstate.yuv2rgb);
         vl_csc_get_rgbyuv_matrix(param->out_matrix_coefficients, rgb_format, dst->buffer_format,
                                  PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL, param->out_color_range,
                                  &drv->cstate.rgb2yuv);
      }
   } else if (src_yuv) {
      /* YUV to RGB */
      vl_csc_get_rgbyuv_matrix(param->in_matrix_coefficients, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &drv->cstate.yuv2rgb);
   } else {
      /* RGB to YUV */
      vl_csc_get_rgbyuv_matrix(param->out_matrix_coefficients, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &drv->cstate.rgb2yuv);
   }

   vl_csc_get_primaries_matrix(param->in_color_primaries, param->out_color_primaries,
                               &drv->cstate.primaries);

   drv->cstate.in_transfer_characteristic = param->in_transfer_characteristics;
   drv->cstate.out_transfer_characteristic = param->out_transfer_characteristics;

   if (src_yuv || dst_yuv) {
      enum pipe_format format = src_yuv ? src->buffer_format : dst->buffer_format;
      enum pipe_video_vpp_chroma_siting chroma_siting =
         src_yuv ? param->in_chroma_siting : param->out_chroma_siting;

      drv->cstate.chroma_location = VL_COMPOSITOR_LOCATION_NONE;

      if (util_format_get_plane_height(format, 1, 4) != 4) {
         if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_TOP)
            drv->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_VERTICAL_TOP;
         else if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_BOTTOM)
            drv->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_VERTICAL_BOTTOM;
         else
            drv->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_VERTICAL_CENTER;
      }

      if (util_format_is_subsampled_422(format) ||
          util_format_get_plane_width(format, 1, 4) != 4) {
         if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_CENTER)
            drv->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_HORIZONTAL_CENTER;
         else
            drv->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_HORIZONTAL_LEFT;
      }
   }

   if (param->orientation & PIPE_VIDEO_VPP_ROTATION_90)
      rotation = VL_COMPOSITOR_ROTATE_90;
   else if (param->orientation & PIPE_VIDEO_VPP_ROTATION_180)
      rotation = VL_COMPOSITOR_ROTATE_180;
   else if (param->orientation & PIPE_VIDEO_VPP_ROTATION_270)
      rotation = VL_COMPOSITOR_ROTATE_270;
   else
      rotation = VL_COMPOSITOR_ROTATE_0;

   if (param->orientation & PIPE_VIDEO_VPP_FLIP_VERTICAL)
      mirror = VL_COMPOSITOR_MIRROR_VERTICAL;
   else if (param->orientation & PIPE_VIDEO_VPP_FLIP_HORIZONTAL)
      mirror = VL_COMPOSITOR_MIRROR_HORIZONTAL;
   else
      mirror = VL_COMPOSITOR_MIRROR_NONE;

   vl_compositor_clear_layers(&drv->cstate);
   vl_compositor_set_layer_rotation(&drv->cstate, 0, rotation);
   vl_compositor_set_layer_mirror(&drv->cstate, 0, mirror);

   if (dst_yuv) {
      if (src_yuv) {
         /* YUV -> YUV */
         if (src->interlaced == dst->interlaced)
            deinterlace = VL_COMPOSITOR_NONE;
         vl_compositor_yuv_deint_full(&drv->cstate, &drv->compositor,
                                      src, dst, &param->src_region, &param->dst_region,
                                      deinterlace);
      } else {
         /* RGB -> YUV */
         vl_compositor_convert_rgb_to_yuv(&drv->cstate, &drv->compositor, 0,
                                          ((struct vl_video_buffer *)src)->resources[0],
                                          dst, &param->src_region, &param->dst_region);
      }
   } else {
      /* YUV/RGB -> RGB */
      vl_compositor_set_buffer_layer(&drv->cstate, &drv->compositor, 0, src,
                                     &param->src_region, NULL, deinterlace);
      vl_compositor_set_layer_dst_area(&drv->cstate, 0, &param->dst_region);
      vl_compositor_render(&drv->cstate, &drv->compositor, &surfaces[0], NULL, false);
   }

   return VA_STATUS_SUCCESS;
}

static VAStatus
vlVaVidEngineBlit(vlVaDriver *drv,
                  vlVaContext *context,
                  struct pipe_video_buffer *src,
                  struct pipe_video_buffer *dst,
                  enum vl_compositor_deinterlace deinterlace,
                  struct pipe_vpp_desc *param)
{
   if (deinterlace != VL_COMPOSITOR_NONE)
      return VA_STATUS_ERROR_UNIMPLEMENTED;

   if (!context->decoder || !context->decoder->process_frame)
      return VA_STATUS_ERROR_UNIMPLEMENTED;

   if (!drv->pipe->screen->is_video_format_supported(drv->pipe->screen,
                                                     src->buffer_format,
                                                     PIPE_VIDEO_PROFILE_UNKNOWN,
                                                     PIPE_VIDEO_ENTRYPOINT_PROCESSING))
      return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;

   if (!drv->pipe->screen->is_video_format_supported(drv->pipe->screen,
                                                     dst->buffer_format,
                                                     PIPE_VIDEO_PROFILE_UNKNOWN,
                                                     PIPE_VIDEO_ENTRYPOINT_PROCESSING))
      return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;

   if (context->needs_begin_frame) {
      context->decoder->begin_frame(context->decoder, dst,
                                    &context->desc.base);
      context->needs_begin_frame = false;
   }

   if (context->decoder->process_frame(context->decoder, src, param))
      return VA_STATUS_ERROR_OPERATION_FAILED;

   return VA_STATUS_SUCCESS;
}

static struct pipe_video_buffer *
vlVaApplyDeint(vlVaDriver *drv, vlVaContext *context,
               VAProcPipelineParameterBuffer *param,
               struct pipe_video_buffer *current,
               unsigned field)
{
   vlVaSurface *prevprev, *prev, *next;

   if (param->num_forward_references < 2 ||
       param->num_backward_references < 1)
      return current;

   prevprev = handle_table_get(drv->htab, param->forward_references[1]);
   prev = handle_table_get(drv->htab, param->forward_references[0]);
   next = handle_table_get(drv->htab, param->backward_references[0]);

   if (!prevprev || !prev || !next)
      return current;

   if (context->deint && (context->deint->video_width != current->width ||
       context->deint->video_height != current->height ||
       context->deint->interleaved != !current->interlaced)) {
      vl_deint_filter_cleanup(context->deint);
      FREE(context->deint);
      context->deint = NULL;
   }

   if (!context->deint) {
      context->deint = MALLOC(sizeof(struct vl_deint_filter));
      if (!vl_deint_filter_init(context->deint, drv->pipe, current->width,
                                current->height, false, false, !current->interlaced)) {
         FREE(context->deint);
         context->deint = NULL;
         return current;
      }
   }

   if (!vl_deint_filter_check_buffers(context->deint, prevprev->buffer,
                                      prev->buffer, current, next->buffer))
      return current;

   vl_deint_filter_render(context->deint, prevprev->buffer, prev->buffer,
                          current, next->buffer, field);

   return context->deint->video_buffer;
}

static void
vlVaGetColorProperties(VAProcColorStandardType standard,
                       enum pipe_video_vpp_color_primaries *primaries,
                       enum pipe_video_vpp_transfer_characteristic *trc,
                       enum pipe_video_vpp_matrix_coefficients *coeffs)
{
   switch (standard) {
   case VAProcColorStandardBT601:
      *primaries = PIPE_VIDEO_VPP_PRI_SMPTE170M;
      *trc = PIPE_VIDEO_VPP_TRC_SMPTE170M;
      *coeffs = PIPE_VIDEO_VPP_MCF_SMPTE170M;
      break;
   case VAProcColorStandardBT709:
      *primaries = PIPE_VIDEO_VPP_PRI_BT709;
      *trc = PIPE_VIDEO_VPP_TRC_BT709;
      *coeffs = PIPE_VIDEO_VPP_MCF_BT709;
      break;
   case VAProcColorStandardBT2020:
      *primaries = PIPE_VIDEO_VPP_PRI_BT2020;
      *trc = PIPE_VIDEO_VPP_TRC_SMPTE2084;
      *coeffs = PIPE_VIDEO_VPP_MCF_BT2020_NCL;
      break;
   default:
      *primaries = PIPE_VIDEO_VPP_PRI_UNSPECIFIED;
      *trc = PIPE_VIDEO_VPP_TRC_UNSPECIFIED;
      *coeffs = PIPE_VIDEO_VPP_MCF_UNSPECIFIED;
      break;
   }
}

VAStatus
vlVaHandleVAProcPipelineParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   enum vl_compositor_deinterlace deinterlace = VL_COMPOSITOR_NONE;
   VARectangle def_src_region, def_dst_region;
   const VARectangle *src_region, *dst_region;
   VAProcPipelineParameterBuffer *param;
   struct pipe_video_buffer *src, *dst;
   vlVaSurface *src_surface, *dst_surface;
   unsigned i;
   struct pipe_vpp_desc vpp = {0};

   if (!drv || !context)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!buf || !buf->data)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (!context->target)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   param = buf->data;

   src_surface = handle_table_get(drv->htab, param->surface);
   dst_surface = handle_table_get(drv->htab, context->target_id);
   if (!src_surface || !dst_surface)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   /* Encode/Decode processing */
   if (context->templat.entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE ||
       context->templat.entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      if (param->num_additional_outputs < 1 || !param->additional_outputs)
         return VA_STATUS_ERROR_INVALID_PARAMETER;

      dst_surface = handle_table_get(drv->htab, param->additional_outputs[0]);
   }

   src_region = vlVaRegionDefault(param->surface_region, src_surface, &def_src_region);
   dst_region = vlVaRegionDefault(param->output_region, dst_surface, &def_dst_region);

   src = vlVaGetSurfaceBuffer(drv, src_surface);
   dst = vlVaGetSurfaceBuffer(drv, dst_surface);
   if (!src || !dst)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   for (i = 0; i < param->num_filters; i++) {
      vlVaBuffer *buf = handle_table_get(drv->htab, param->filters[i]);
      VAProcFilterParameterBufferBase *filter;

      if (!buf || buf->type != VAProcFilterParameterBufferType)
         return VA_STATUS_ERROR_INVALID_BUFFER;

      filter = buf->data;
      switch (filter->type) {
      case VAProcFilterDeinterlacing: {
         VAProcFilterParameterBufferDeinterlacing *deint = buf->data;
         switch (deint->algorithm) {
         case VAProcDeinterlacingBob:
            if (deint->flags & VA_DEINTERLACING_BOTTOM_FIELD)
               deinterlace = VL_COMPOSITOR_BOB_BOTTOM;
            else
               deinterlace = VL_COMPOSITOR_BOB_TOP;
            break;

         case VAProcDeinterlacingWeave:
            deinterlace = VL_COMPOSITOR_WEAVE;
            break;

         case VAProcDeinterlacingMotionAdaptive:
            src = vlVaApplyDeint(drv, context, param, src,
                                 !!(deint->flags & VA_DEINTERLACING_BOTTOM_FIELD));
             deinterlace = VL_COMPOSITOR_MOTION_ADAPTIVE;
            break;

         default:
            return VA_STATUS_ERROR_UNIMPLEMENTED;
         }
         drv->compositor.deinterlace = deinterlace;
         break;
      }

      default:
         return VA_STATUS_ERROR_UNIMPLEMENTED;
      }
   }

   vpp.src_region.x0 = src_region->x;
   vpp.src_region.y0 = src_region->y;
   vpp.src_region.x1 = src_region->x + src_region->width;
   vpp.src_region.y1 = src_region->y + src_region->height;

   vpp.dst_region.x0 = dst_region->x;
   vpp.dst_region.y0 = dst_region->y;
   vpp.dst_region.x1 = dst_region->x + dst_region->width;
   vpp.dst_region.y1 = dst_region->y + dst_region->height;

   switch (param->rotation_state) {
   case VA_ROTATION_90:
      vpp.orientation = PIPE_VIDEO_VPP_ROTATION_90;
      break;
   case VA_ROTATION_180:
      vpp.orientation = PIPE_VIDEO_VPP_ROTATION_180;
      break;
   case VA_ROTATION_270:
      vpp.orientation = PIPE_VIDEO_VPP_ROTATION_270;
      break;
   default:
      vpp.orientation = PIPE_VIDEO_VPP_ORIENTATION_DEFAULT;
      break;
   }

   switch (param->mirror_state) {
   case VA_MIRROR_HORIZONTAL:
      vpp.orientation |= PIPE_VIDEO_VPP_FLIP_HORIZONTAL;
      break;
   case VA_MIRROR_VERTICAL:
      vpp.orientation |= PIPE_VIDEO_VPP_FLIP_VERTICAL;
      break;
   default:
      break;
   }

   if (param->blend_state) {
      if (param->blend_state->flags & VA_BLEND_GLOBAL_ALPHA) {
         vpp.blend.mode = PIPE_VIDEO_VPP_BLEND_MODE_GLOBAL_ALPHA;
         vpp.blend.global_alpha = param->blend_state->global_alpha;
      }
   }

   /* Output background color */
   vpp.background_color = param->output_background_color;

   if (param->surface_color_standard == VAProcColorStandardExplicit) {
      vpp.in_color_primaries = param->input_color_properties.colour_primaries;
      vpp.in_transfer_characteristics = param->input_color_properties.transfer_characteristics;
      vpp.in_matrix_coefficients = param->input_color_properties.matrix_coefficients;
   } else {
      vlVaGetColorProperties(param->surface_color_standard, &vpp.in_color_primaries,
                             &vpp.in_transfer_characteristics, &vpp.in_matrix_coefficients);
   }

   if (vpp.in_color_primaries == PIPE_VIDEO_VPP_PRI_UNSPECIFIED)
      vpp.in_color_primaries = PIPE_VIDEO_VPP_PRI_BT709;

   if (vpp.in_transfer_characteristics == PIPE_VIDEO_VPP_TRC_UNSPECIFIED)
      vpp.in_transfer_characteristics = PIPE_VIDEO_VPP_TRC_GAMMA22;

   if (vpp.in_matrix_coefficients == PIPE_VIDEO_VPP_MCF_UNSPECIFIED ||
       (vpp.in_matrix_coefficients == PIPE_VIDEO_VPP_MCF_RGB &&
        util_format_is_yuv(src->buffer_format)))
      vpp.in_matrix_coefficients = PIPE_VIDEO_VPP_MCF_BT709;

   /* Input surface color range */
   switch (param->input_color_properties.color_range) {
   case VA_SOURCE_RANGE_REDUCED:
      vpp.in_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED;
      break;
   case VA_SOURCE_RANGE_FULL:
      vpp.in_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL;
      break;
   default:
      vpp.in_color_range = util_format_is_yuv(src->buffer_format) ?
         PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED : PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL;
      break;
   }

   /* Input surface chroma sample location */
   vpp.in_chroma_siting = PIPE_VIDEO_VPP_CHROMA_SITING_NONE;
   if (param->input_color_properties.chroma_sample_location & VA_CHROMA_SITING_VERTICAL_TOP)
      vpp.in_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_TOP;
   else if (param->input_color_properties.chroma_sample_location & VA_CHROMA_SITING_VERTICAL_CENTER)
      vpp.in_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_CENTER;
   else if (param->input_color_properties.chroma_sample_location & VA_CHROMA_SITING_VERTICAL_BOTTOM)
      vpp.in_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_BOTTOM;
   if (param->input_color_properties.chroma_sample_location & VA_CHROMA_SITING_HORIZONTAL_LEFT)
      vpp.in_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_LEFT;
   else if (param->input_color_properties.chroma_sample_location & VA_CHROMA_SITING_HORIZONTAL_CENTER)
      vpp.in_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_CENTER;

   if (param->output_color_standard == VAProcColorStandardExplicit) {
      vpp.out_color_primaries = param->output_color_properties.colour_primaries;
      vpp.out_transfer_characteristics = param->output_color_properties.transfer_characteristics;
      vpp.out_matrix_coefficients = param->output_color_properties.matrix_coefficients;
   } else {
      vlVaGetColorProperties(param->surface_color_standard, &vpp.out_color_primaries,
                             &vpp.out_transfer_characteristics, &vpp.out_matrix_coefficients);
   }

   if (vpp.out_color_primaries == PIPE_VIDEO_VPP_PRI_UNSPECIFIED)
      vpp.out_color_primaries = PIPE_VIDEO_VPP_PRI_BT709;

   if (vpp.out_transfer_characteristics == PIPE_VIDEO_VPP_TRC_UNSPECIFIED)
      vpp.out_transfer_characteristics = PIPE_VIDEO_VPP_TRC_GAMMA22;

   if (vpp.out_matrix_coefficients == PIPE_VIDEO_VPP_MCF_UNSPECIFIED ||
       (vpp.out_matrix_coefficients == PIPE_VIDEO_VPP_MCF_RGB &&
        util_format_is_yuv(dst->buffer_format)))
      vpp.out_matrix_coefficients = PIPE_VIDEO_VPP_MCF_BT709;

   /* Output surface color range */
   switch (param->output_color_properties.color_range) {
   case VA_SOURCE_RANGE_REDUCED:
      vpp.out_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED;
      break;
   case VA_SOURCE_RANGE_FULL:
      vpp.out_color_range = PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL;
      break;
   default:
      vpp.out_color_range = util_format_is_yuv(dst->buffer_format) ?
         PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED : PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL;
      break;
   }

   /* Output surface chroma sample location */
   vpp.out_chroma_siting = PIPE_VIDEO_VPP_CHROMA_SITING_NONE;
   if (param->output_color_properties.chroma_sample_location & VA_CHROMA_SITING_VERTICAL_TOP)
      vpp.out_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_TOP;
   else if (param->output_color_properties.chroma_sample_location & VA_CHROMA_SITING_VERTICAL_CENTER)
      vpp.out_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_CENTER;
   else if (param->output_color_properties.chroma_sample_location & VA_CHROMA_SITING_VERTICAL_BOTTOM)
      vpp.out_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_BOTTOM;
   if (param->output_color_properties.chroma_sample_location & VA_CHROMA_SITING_HORIZONTAL_LEFT)
      vpp.out_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_LEFT;
   else if (param->output_color_properties.chroma_sample_location & VA_CHROMA_SITING_HORIZONTAL_CENTER)
      vpp.out_chroma_siting |= PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_CENTER;

   if (param->filter_flags & VA_FILTER_SCALING_FAST)
      vpp.filter_flags |= PIPE_VIDEO_VPP_FILTER_FLAG_SCALING_FAST;

   vpp.base.in_fence = src_surface->fence;

   /* Encode/Decode processing */
   if (context->templat.entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE ||
       context->templat.entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      vpp.dst = dst;
      vpp.base.out_fence = &dst_surface->fence;
      context->proc.vpp = vpp;
      context->proc.dst_surface = dst_surface;
      vlVaSetSurfaceContext(drv, dst_surface, context);
      return VA_STATUS_SUCCESS;
   }

   if (vlVaVidEngineBlit(drv, context, src, dst, deinterlace, &vpp) == VA_STATUS_SUCCESS)
      return VA_STATUS_SUCCESS;

   VAStatus ret =
      vlVaPostProcCompositor(drv, src, dst, deinterlace, &vpp);
   vlVaSurfaceFlush(drv, dst_surface);
   return ret;
}
