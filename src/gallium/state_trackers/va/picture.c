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

#include "vl/vl_vlc.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

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

   pipe_mutex_lock(drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   surf = handle_table_get(drv->htab, render_target);
   pipe_mutex_unlock(drv->mutex);
   if (!surf || !surf->buffer)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   context->target = surf->buffer;

   if (!context->decoder) {

      /* VPP */
      if (context->templat.profile == PIPE_VIDEO_PROFILE_UNKNOWN &&
          context->target->buffer_format != PIPE_FORMAT_B8G8R8A8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_R8G8B8A8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_B8G8R8X8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_R8G8B8X8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_NV12)
         return VA_STATUS_ERROR_UNIMPLEMENTED;

      return VA_STATUS_SUCCESS;
   }

   context->decoder->begin_frame(context->decoder, context->target, &context->desc.base);

   return VA_STATUS_SUCCESS;
}

void
vlVaGetReferenceFrame(vlVaDriver *drv, VASurfaceID surface_id,
                      struct pipe_video_buffer **ref_frame)
{
   vlVaSurface *surf = handle_table_get(drv->htab, surface_id);
   if (surf)
      *ref_frame = surf->buffer;
   else
      *ref_frame = NULL;
}

static VAStatus
handlePictureParameterBuffer(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus vaStatus = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG12:
      vlVaHandlePictureParameterBufferMPEG12(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      vlVaHandlePictureParameterBufferH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_VC1:
      vlVaHandlePictureParameterBufferVC1(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4:
      vlVaHandlePictureParameterBufferMPEG4(drv, context, buf);
      break;

  case PIPE_VIDEO_FORMAT_HEVC:
      vlVaHandlePictureParameterBufferHEVC(drv, context, buf);
      break;

   default:
      break;
   }

   /* Create the decoder once max_references is known. */
   if (!context->decoder) {
      if (!context->target)
         return VA_STATUS_ERROR_INVALID_CONTEXT;

      if (context->templat.max_references == 0)
         return VA_STATUS_ERROR_INVALID_BUFFER;

      if (u_reduce_video_profile(context->templat.profile) ==
          PIPE_VIDEO_FORMAT_MPEG4_AVC)
         context->templat.level = u_get_h264_level(context->templat.width,
            context->templat.height, &context->templat.max_references);

      context->decoder = drv->pipe->create_video_codec(drv->pipe,
         &context->templat);

      if (!context->decoder)
         return VA_STATUS_ERROR_ALLOCATION_FAILED;

      context->decoder->begin_frame(context->decoder, context->target,
         &context->desc.base);
   }

   return vaStatus;
}

static void
handleIQMatrixBuffer(vlVaContext *context, vlVaBuffer *buf)
{
   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG12:
      vlVaHandleIQMatrixBufferMPEG12(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      vlVaHandleIQMatrixBufferH264(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4:
      vlVaHandleIQMatrixBufferMPEG4(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      vlVaHandleIQMatrixBufferHEVC(context, buf);
      break;

   default:
      break;
   }
}

static void
handleSliceParameterBuffer(vlVaContext *context, vlVaBuffer *buf)
{
   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG12:
      vlVaHandleSliceParameterBufferMPEG12(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_VC1:
      vlVaHandleSliceParameterBufferVC1(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      vlVaHandleSliceParameterBufferH264(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4:
      vlVaHandleSliceParameterBufferMPEG4(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      vlVaHandleSliceParameterBufferHEVC(context, buf);
      break;

   default:
      break;
   }
}

static unsigned int
bufHasStartcode(vlVaBuffer *buf, unsigned int code, unsigned int bits)
{
   struct vl_vlc vlc = {0};
   int i;

   /* search the first 64 bytes for a startcode */
   vl_vlc_init(&vlc, 1, (const void * const*)&buf->data, &buf->size);
   for (i = 0; i < 64 && vl_vlc_bits_left(&vlc) >= bits; ++i) {
      if (vl_vlc_peekbits(&vlc, bits) == code)
         return 1;
      vl_vlc_eatbits(&vlc, 8);
      vl_vlc_fillbits(&vlc);
   }

   return 0;
}

static void
handleVASliceDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   enum pipe_video_format format;
   unsigned num_buffers = 0;
   void * const *buffers[2];
   unsigned sizes[2];
   static const uint8_t start_code_h264[] = { 0x00, 0x00, 0x01 };
   static const uint8_t start_code_h265[] = { 0x00, 0x00, 0x01 };
   static const uint8_t start_code_vc1[] = { 0x00, 0x00, 0x01, 0x0d };

   format = u_reduce_video_profile(context->templat.profile);
   switch (format) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      if (bufHasStartcode(buf, 0x000001, 24))
         break;

      buffers[num_buffers] = (void *const)&start_code_h264;
      sizes[num_buffers++] = sizeof(start_code_h264);
      break;
   case PIPE_VIDEO_FORMAT_HEVC:
      if (bufHasStartcode(buf, 0x000001, 24))
         break;

      buffers[num_buffers] = (void *const)&start_code_h265;
      sizes[num_buffers++] = sizeof(start_code_h265);
      break;
   case PIPE_VIDEO_FORMAT_VC1:
      if (bufHasStartcode(buf, 0x0000010d, 32) ||
          bufHasStartcode(buf, 0x0000010c, 32) ||
          bufHasStartcode(buf, 0x0000010b, 32))
         break;

      if (context->decoder->profile == PIPE_VIDEO_PROFILE_VC1_ADVANCED) {
         buffers[num_buffers] = (void *const)&start_code_vc1;
         sizes[num_buffers++] = sizeof(start_code_vc1);
      }
      break;
   case PIPE_VIDEO_FORMAT_MPEG4:
      if (bufHasStartcode(buf, 0x000001, 24))
         break;

      vlVaDecoderFixMPEG4Startcode(context);
      buffers[num_buffers] = (void *)context->mpeg4.start_code;
      sizes[num_buffers++] = context->mpeg4.start_code_size;
   default:
      break;
   }

   buffers[num_buffers] = buf->data;
   sizes[num_buffers] = buf->size;
   ++num_buffers;
   context->decoder->decode_bitstream(context->decoder, context->target, &context->desc.base,
      num_buffers, (const void * const*)buffers, sizes);
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

   pipe_mutex_lock(drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      pipe_mutex_unlock(drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   for (i = 0; i < num_buffers; ++i) {
      vlVaBuffer *buf = handle_table_get(drv->htab, buffers[i]);
      if (!buf) {
         pipe_mutex_unlock(drv->mutex);
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      switch (buf->type) {
      case VAPictureParameterBufferType:
         vaStatus = handlePictureParameterBuffer(drv, context, buf);
         break;

      case VAIQMatrixBufferType:
         handleIQMatrixBuffer(context, buf);
         break;

      case VASliceParameterBufferType:
         handleSliceParameterBuffer(context, buf);
         break;

      case VASliceDataBufferType:
         handleVASliceDataBufferType(context, buf);
         break;
      case VAProcPipelineParameterBufferType:
         vaStatus = vlVaHandleVAProcPipelineParameterBufferType(drv, context, buf);
         break;

      default:
         break;
      }
   }
   pipe_mutex_unlock(drv->mutex);

   return vaStatus;
}

VAStatus
vlVaEndPicture(VADriverContextP ctx, VAContextID context_id)
{
   vlVaDriver *drv;
   vlVaContext *context;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   pipe_mutex_lock(drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   pipe_mutex_unlock(drv->mutex);
   if (!context)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!context->decoder) {
      if (context->templat.profile != PIPE_VIDEO_PROFILE_UNKNOWN)
         return VA_STATUS_ERROR_INVALID_CONTEXT;

      /* VPP */
      return VA_STATUS_SUCCESS;
   }

   context->mpeg4.frame_num++;
   context->decoder->end_frame(context->decoder, context->target, &context->desc.base);

   return VA_STATUS_SUCCESS;
}
