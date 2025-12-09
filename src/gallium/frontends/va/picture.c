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

#include "pipe/p_video_codec.h"

#include "util/u_handle_table.h"
#include "util/u_video.h"
#include "util/u_memory.h"
#include "util/set.h"

#include "vl/vl_winsys.h"

#include "va_private.h"

void
vlVaSetSurfaceContext(vlVaDriver *drv, vlVaSurface *surf, vlVaContext *context)
{
   if (surf->ctx == context)
      return;

   if (surf->ctx) {
      assert(_mesa_set_search(surf->ctx->surfaces, surf));
      _mesa_set_remove_key(surf->ctx->surfaces, surf);

      /* Only drivers supporting PIPE_VIDEO_ENTRYPOINT_PROCESSING will create
       * decoder for postproc context and thus be able to wait on and destroy
       * the surface fence. On other drivers we need to destroy the fence here
       * otherwise vaQuerySurfaceStatus/vaSyncSurface will fail and we'll also
       * potentially leak the fence.
       */
      if (surf->fence && !context->decoder &&
          context->templat.entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING &&
          surf->ctx->decoder && surf->ctx->decoder->destroy_fence &&
          !drv->pipe->screen->get_video_param(drv->pipe->screen,
                                              PIPE_VIDEO_PROFILE_UNKNOWN,
                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                              PIPE_VIDEO_CAP_SUPPORTED)) {
         surf->ctx->decoder->destroy_fence(surf->ctx->decoder, surf->fence);
         surf->fence = NULL;
      }
   }

   surf->ctx = context;
   _mesa_set_add(surf->ctx->surfaces, surf);
}

static void
vlVaSetBufferContext(vlVaDriver *drv, vlVaBuffer *buf, vlVaContext *context)
{
   if (buf->ctx == context)
      return;

   if (buf->ctx) {
      assert(_mesa_set_search(buf->ctx->buffers, buf));
      _mesa_set_remove_key(buf->ctx->buffers, buf);
   }

   buf->ctx = context;
   _mesa_set_add(buf->ctx->buffers, buf);
}

VAStatus
vlVaBeginPicture(VADriverContextP ctx, VAContextID context_id, VASurfaceID render_target)
{
   vlVaDriver *drv;
   vlVaContext *context;
   vlVaSurface *surf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG12) {
      context->desc.mpeg12.intra_matrix = NULL;
      context->desc.mpeg12.non_intra_matrix = NULL;
   }

   surf = handle_table_get(drv->htab, render_target);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   if (surf->coded_buf) {
      surf->coded_buf->coded_surf = NULL;
      surf->coded_buf = NULL;
   }

   /* Encode only reads from the surface and doesn't set surface fence. */
   if (context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      vlVaSetSurfaceContext(drv, surf, context);

   context->target_id = render_target;
   context->target = surf->buffer;

   if (context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      context->needs_begin_frame = true;

   if (!context->decoder) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_SUCCESS;
   }

   /* meta data and seis are per picture basis, it needs to be
    * cleared before rendering the picture. */
   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      switch (u_reduce_video_profile(context->templat.profile)) {
         case PIPE_VIDEO_FORMAT_AV1:
            context->desc.av1enc.metadata_flags.value = 0;
            context->desc.av1enc.roi.num = 0;
            context->desc.av1enc.intra_refresh.mode = INTRA_REFRESH_MODE_NONE;
            break;
         case PIPE_VIDEO_FORMAT_HEVC:
            context->desc.h265enc.roi.num = 0;
            context->desc.h265enc.intra_refresh.mode = INTRA_REFRESH_MODE_NONE;
            break;
         case PIPE_VIDEO_FORMAT_MPEG4_AVC:
            context->desc.h264enc.roi.num = 0;
            context->desc.h264enc.intra_refresh.mode = INTRA_REFRESH_MODE_NONE;
            break;
         default:
            break;
      }
   }

   context->slice_data_offset = 0;
   context->have_slice_params = false;
   context->proc.dst_surface = NULL;

   mtx_unlock(&drv->mutex);
   return VA_STATUS_SUCCESS;
}

void
vlVaGetReferenceFrame(vlVaDriver *drv, VASurfaceID surface_id,
                      struct pipe_video_buffer **ref_frame)
{
   vlVaSurface *surf = handle_table_get(drv->htab, surface_id);
   if (surf)
      *ref_frame = vlVaGetSurfaceBuffer(drv, surf);
   else
      *ref_frame = NULL;
}

VAStatus
vlVaRenderPicture(VADriverContextP ctx, VAContextID context_id, VABufferID *buffers, int num_buffers)
{
   vlVaDriver *drv;
   vlVaContext *context;
   VAStatus vaStatus = VA_STATUS_SUCCESS;

   unsigned i;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (!context->target_id) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   for (i = 0; i < num_buffers && vaStatus == VA_STATUS_SUCCESS; ++i) {
      vlVaBuffer *buf = handle_table_get(drv->htab, buffers[i]);
      if (!buf) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      switch (buf->type) {
      case VAPictureParameterBufferType:
      case VAIQMatrixBufferType:
      case VASliceParameterBufferType:
      case VASliceDataBufferType:
      case VAHuffmanTableBufferType:
      case VAProtectedSliceDataBufferType:
         vaStatus = vlVaHandleDecBufferType(drv, context, buf);
         break;

      case VAProcPipelineParameterBufferType:
         vaStatus = vlVaHandleVAProcPipelineParameterBufferType(drv, context, buf);
         break;

      case VAEncSequenceParameterBufferType:
      case VAEncMiscParameterBufferType:
      case VAEncPictureParameterBufferType:
      case VAEncSliceParameterBufferType:
      case VAEncPackedHeaderParameterBufferType:
      case VAEncPackedHeaderDataBufferType:
      case VAStatsStatisticsBufferType:
         vaStatus = vlVaHandleEncBufferType(drv, context, buf);
         break;

      default:
         break;
      }
   }

   if (context->decoder &&
       context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM &&
       context->bs.buffers.size) {
      context->decoder->decode_bitstream(context->decoder, context->target, &context->desc.base,
         util_dynarray_num_elements(&context->bs.buffers, void *), context->bs.buffers.data, context->bs.sizes.data);
      util_dynarray_clear(&context->bs.buffers);
      util_dynarray_clear(&context->bs.sizes);
   }

   mtx_unlock(&drv->mutex);

   return vaStatus;
}

static bool vlVaQueryApplyFilmGrainAV1(vlVaContext *context,
                                 int *output_id,
                                 struct pipe_video_buffer ***out_target)
{
   struct pipe_av1_picture_desc *av1 = NULL;

   if (u_reduce_video_profile(context->templat.profile) != PIPE_VIDEO_FORMAT_AV1 ||
       context->decoder->entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return false;

   av1 = &context->desc.av1;
   if (!av1->picture_parameter.film_grain_info.film_grain_info_fields.apply_grain)
      return false;

   *output_id = av1->picture_parameter.current_frame_id;
   *out_target = &av1->film_grain_target;
   return true;
}

static void vlVaClearRawHeaders(struct util_dynarray *headers)
{
   util_dynarray_foreach(headers, struct pipe_enc_raw_header, header)
      FREE(header->buffer);
   util_dynarray_clear(headers);
}

VAStatus
vlVaEndPicture(VADriverContextP ctx, VAContextID context_id)
{
   vlVaDriver *drv;
   vlVaContext *context;
   vlVaBuffer *coded_buf;
   vlVaSurface *surf;
   void *feedback = NULL;
   bool apply_av1_fg = false;
   struct pipe_video_buffer **out_target;
   int output_id;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (!context->target_id) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   output_id = context->target_id;
   context->target_id = 0;

   if (!context->decoder) {
      if (context->templat.profile != PIPE_VIDEO_PROFILE_UNKNOWN) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_CONTEXT;
      }

      /* VPP */
      mtx_unlock(&drv->mutex);
      return VA_STATUS_SUCCESS;
   }

   if (context->needs_begin_frame) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   out_target = &context->target;
   apply_av1_fg = vlVaQueryApplyFilmGrainAV1(context, &output_id, &out_target);

   surf = handle_table_get(drv->htab, output_id);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   if (apply_av1_fg) {
      vlVaSetSurfaceContext(drv, surf, context);
      *out_target = surf->buffer;
   }

   context->mpeg4.frame_num++;

   if ((bool)(surf->templat.bind & PIPE_BIND_PROTECTED) != context->desc.base.protected_playback) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      coded_buf = context->coded_buf;
      context->desc.base.out_fence = &coded_buf->fence;
      if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC)
         context->desc.h264enc.frame_num_cnt++;

      if (coded_buf->coded_surf)
         coded_buf->coded_surf->coded_buf = NULL;
      vlVaGetBufferFeedback(coded_buf);
      vlVaSetBufferContext(drv, coded_buf, context);

      int driver_metadata_support = drv->pipe->screen->get_video_param(drv->pipe->screen,
                                                                       context->decoder->profile,
                                                                       context->decoder->entrypoint,
                                                                       PIPE_VIDEO_CAP_ENC_SUPPORTS_FEEDBACK_METADATA);
      if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC)
         context->desc.h264enc.requested_metadata = driver_metadata_support;
      else if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_HEVC)
         context->desc.h265enc.requested_metadata = driver_metadata_support;
      else if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_AV1)
         context->desc.av1enc.requested_metadata = driver_metadata_support;

      context->desc.base.in_fence = surf->fence;
      if (context->proc.dst_surface) {
         if (!context->decoder->process_frame ||
             context->decoder->process_frame(context->decoder, context->target, &context->proc.vpp) != 0) {
            VAStatus ret = vlVaPostProcCompositor(drv, context->target, context->proc.vpp.dst,
                                                  VL_COMPOSITOR_NONE, &context->proc.vpp);
            vlVaSurfaceFlush(drv, context->proc.dst_surface);
            if (ret != VA_STATUS_SUCCESS) {
               mtx_unlock(&drv->mutex);
               return ret;
            }
         }
         context->target = context->proc.vpp.dst;
      }
      context->decoder->begin_frame(context->decoder, context->target, &context->desc.base);
      context->decoder->encode_bitstream(context->decoder, context->target,
                                         coded_buf->derived_surface.resource, &feedback);
      coded_buf->feedback = feedback;
      coded_buf->coded_surf = surf;
      surf->coded_buf = coded_buf;
   } else if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      context->desc.base.out_fence = &surf->fence;
   } else if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {
      context->desc.base.out_fence = &surf->fence;
   }

   if (!drv->pipe->screen->is_video_format_supported(drv->pipe->screen,
                                                     context->target->buffer_format,
                                                     context->decoder->profile,
                                                     context->decoder->entrypoint)) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   /* when there are external handles, we can't set PIPE_FLUSH_ASYNC */
   if (context->desc.base.out_fence)
      context->desc.base.flush_flags = drv->has_external_handles ? 0 : PIPE_FLUSH_ASYNC;

   if (context->decoder->end_frame(context->decoder, context->target, &context->desc.base) != 0) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   if (drv->pipe->screen->get_video_param(drv->pipe->screen,
                           context->decoder->profile,
                           context->decoder->entrypoint,
                           PIPE_VIDEO_CAP_REQUIRES_FLUSH_ON_END_FRAME))
      context->decoder->flush(context->decoder);


   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      if (context->proc.dst_surface) {
         if (!context->decoder->process_frame ||
             context->decoder->process_frame(context->decoder, context->target, &context->proc.vpp) != 0) {
            VAStatus ret = vlVaPostProcCompositor(drv, context->target, context->proc.vpp.dst,
                                                  VL_COMPOSITOR_NONE, &context->proc.vpp);
            vlVaSurfaceFlush(drv, context->proc.dst_surface);
            if (ret != VA_STATUS_SUCCESS) {
               mtx_unlock(&drv->mutex);
               return ret;
            }
         }
      }
   } else if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_AV1:
         context->desc.av1enc.frame_num++;
         vlVaClearRawHeaders(&context->desc.av1enc.raw_headers);
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         context->desc.h265enc.frame_num++;
         vlVaClearRawHeaders(&context->desc.h265enc.raw_headers);
         break;
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         if (!context->desc.h264enc.not_referenced)
            context->desc.h264enc.frame_num++;
         vlVaClearRawHeaders(&context->desc.h264enc.raw_headers);
         break;
      default:
         break;
      }
   }

   mtx_unlock(&drv->mutex);
   return VA_STATUS_SUCCESS;
}

void
vlVaAddRawHeader(struct util_dynarray *headers, uint8_t type, uint32_t size,
                 uint8_t *buf, bool is_slice, uint32_t emulation_bytes_start)
{
   struct pipe_enc_raw_header header = {
      .type = type,
      .is_slice = is_slice,
   };
   if (emulation_bytes_start) {
      uint32_t pos = emulation_bytes_start, num_zeros = 0;
      header.buffer = MALLOC(size * 3 / 2);
      memcpy(header.buffer, buf, emulation_bytes_start);
      for (uint32_t i = emulation_bytes_start; i < size; i++) {
         uint8_t byte = buf[i];
         if (num_zeros >= 2 && byte <= 0x03) {
            header.buffer[pos++] = 0x03;
            num_zeros = 0;
         }
         header.buffer[pos++] = byte;
         num_zeros = byte == 0x00 ? num_zeros + 1 : 0;
      }
      header.size = pos;
   } else {
      header.size = size;
      header.buffer = MALLOC(header.size);
      memcpy(header.buffer, buf, size);
   }
   util_dynarray_append(headers, header);
}
