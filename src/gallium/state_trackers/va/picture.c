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

   context->target_id = render_target;
   surf->ctx = context_id;
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

   if (context->decoder->entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      context->needs_begin_frame = true;

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

static void
getEncParamPreset(vlVaContext *context)
{
   //motion estimation preset
   context->desc.h264enc.motion_est.motion_est_quarter_pixel = 0x00000001;
   context->desc.h264enc.motion_est.lsmvert = 0x00000002;
   context->desc.h264enc.motion_est.enc_disable_sub_mode = 0x00000078;
   context->desc.h264enc.motion_est.enc_en_ime_overw_dis_subm = 0x00000001;
   context->desc.h264enc.motion_est.enc_ime_overw_dis_subm_no = 0x00000001;
   context->desc.h264enc.motion_est.enc_ime2_search_range_x = 0x00000004;
   context->desc.h264enc.motion_est.enc_ime2_search_range_y = 0x00000004;

   //pic control preset
   context->desc.h264enc.pic_ctrl.enc_cabac_enable = 0x00000001;
   context->desc.h264enc.pic_ctrl.enc_constraint_set_flags = 0x00000040;

   //rate control
   context->desc.h264enc.rate_ctrl.vbv_buffer_size = 20000000;
   context->desc.h264enc.rate_ctrl.vbv_buf_lv = 48;
   context->desc.h264enc.rate_ctrl.fill_data_enable = 1;
   context->desc.h264enc.rate_ctrl.enforce_hrd = 1;
   context->desc.h264enc.enable_vui = false;
   if (context->desc.h264enc.rate_ctrl.frame_rate_num == 0)
      context->desc.h264enc.rate_ctrl.frame_rate_num = 30;
   context->desc.h264enc.rate_ctrl.target_bits_picture =
      context->desc.h264enc.rate_ctrl.target_bitrate / context->desc.h264enc.rate_ctrl.frame_rate_num;
   context->desc.h264enc.rate_ctrl.peak_bits_picture_integer =
      context->desc.h264enc.rate_ctrl.peak_bitrate / context->desc.h264enc.rate_ctrl.frame_rate_num;
   context->desc.h264enc.rate_ctrl.peak_bits_picture_fraction = 0;

   context->desc.h264enc.ref_pic_mode = 0x00000201;
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

      context->needs_begin_frame = true;
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

   if (context->needs_begin_frame) {
      context->decoder->begin_frame(context->decoder, context->target,
         &context->desc.base);
      context->needs_begin_frame = false;
   }
   context->decoder->decode_bitstream(context->decoder, context->target, &context->desc.base,
      num_buffers, (const void * const*)buffers, sizes);
}

static VAStatus
handleVAEncMiscParameterTypeRateControl(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterRateControl *rc = (VAEncMiscParameterRateControl *)misc->data;
   if (context->desc.h264enc.rate_ctrl.rate_ctrl_method ==
       PIPE_H264_ENC_RATE_CONTROL_METHOD_CONSTANT)
      context->desc.h264enc.rate_ctrl.target_bitrate = rc->bits_per_second;
   else
      context->desc.h264enc.rate_ctrl.target_bitrate = rc->bits_per_second * (rc->target_percentage / 100.0);
   context->desc.h264enc.rate_ctrl.peak_bitrate = rc->bits_per_second;
   if (context->desc.h264enc.rate_ctrl.target_bitrate < 2000000)
      context->desc.h264enc.rate_ctrl.vbv_buffer_size = MIN2((context->desc.h264enc.rate_ctrl.target_bitrate * 2.75), 2000000);
   else
      context->desc.h264enc.rate_ctrl.vbv_buffer_size = context->desc.h264enc.rate_ctrl.target_bitrate;

   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVAEncMiscParameterTypeFrameRate(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterFrameRate *fr = (VAEncMiscParameterFrameRate *)misc->data;
   context->desc.h264enc.rate_ctrl.frame_rate_num = fr->framerate;
   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVAEncSequenceParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSequenceParameterBufferH264 *h264 = (VAEncSequenceParameterBufferH264 *)buf->data;
   if (!context->decoder) {
      context->templat.max_references = h264->max_num_ref_frames;
      context->templat.level = h264->level_idc;
      context->decoder = drv->pipe->create_video_codec(drv->pipe, &context->templat);
      if (!context->decoder)
         return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   context->gop_coeff = ((1024 + h264->intra_idr_period - 1) / h264->intra_idr_period + 1) / 2 * 2;
   if (context->gop_coeff > VL_VA_ENC_GOP_COEFF)
      context->gop_coeff = VL_VA_ENC_GOP_COEFF;
   context->desc.h264enc.gop_size = h264->intra_idr_period * context->gop_coeff;
   context->desc.h264enc.rate_ctrl.frame_rate_num = h264->time_scale / 2;
   context->desc.h264enc.rate_ctrl.frame_rate_den = 1;
   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVAEncMiscParameterBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus vaStatus = VA_STATUS_SUCCESS;
   VAEncMiscParameterBuffer *misc;
   misc = buf->data;

   switch (misc->type) {
   case VAEncMiscParameterTypeRateControl:
      vaStatus = handleVAEncMiscParameterTypeRateControl(context, misc);
      break;

   case VAEncMiscParameterTypeFrameRate:
      vaStatus = handleVAEncMiscParameterTypeFrameRate(context, misc);
      break;

   default:
      break;
   }

   return vaStatus;
}

static VAStatus
handleVAEncPictureParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncPictureParameterBufferH264 *h264;
   vlVaBuffer *coded_buf;

   h264 = buf->data;
   context->desc.h264enc.frame_num = h264->frame_num;
   context->desc.h264enc.not_referenced = false;
   context->desc.h264enc.is_idr = (h264->pic_fields.bits.idr_pic_flag == 1);
   context->desc.h264enc.pic_order_cnt = h264->CurrPic.TopFieldOrderCnt;
   if (context->desc.h264enc.gop_cnt == 0)
      context->desc.h264enc.i_remain = context->gop_coeff;
   else if (context->desc.h264enc.frame_num == 1)
      context->desc.h264enc.i_remain--;

   context->desc.h264enc.p_remain = context->desc.h264enc.gop_size - context->desc.h264enc.gop_cnt - context->desc.h264enc.i_remain;

   coded_buf = handle_table_get(drv->htab, h264->coded_buf);
   if (!coded_buf->derived_surface.resource)
      coded_buf->derived_surface.resource = pipe_buffer_create(drv->pipe->screen, PIPE_BIND_VERTEX_BUFFER,
                                            PIPE_USAGE_STREAM, coded_buf->size);
   context->coded_buf = coded_buf;

   context->desc.h264enc.frame_idx[h264->CurrPic.picture_id] = h264->frame_num;
   if (context->desc.h264enc.is_idr)
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_IDR;
   else
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_P;

   context->desc.h264enc.quant_i_frames = h264->pic_init_qp;
   context->desc.h264enc.quant_b_frames = h264->pic_init_qp;
   context->desc.h264enc.quant_p_frames = h264->pic_init_qp;
   context->desc.h264enc.gop_cnt++;
   if (context->desc.h264enc.gop_cnt == context->desc.h264enc.gop_size)
      context->desc.h264enc.gop_cnt = 0;

   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVAEncSliceParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSliceParameterBufferH264 *h264;

   h264 = buf->data;
   context->desc.h264enc.ref_idx_l0 = VA_INVALID_ID;
   context->desc.h264enc.ref_idx_l1 = VA_INVALID_ID;

   for (int i = 0; i < 32; i++) {
      if (h264->RefPicList0[i].picture_id != VA_INVALID_ID) {
         if (context->desc.h264enc.ref_idx_l0 == VA_INVALID_ID)
            context->desc.h264enc.ref_idx_l0 = context->desc.h264enc.frame_idx[h264->RefPicList0[i].picture_id];
      }
      if (h264->RefPicList1[i].picture_id != VA_INVALID_ID && h264->slice_type == 1) {
         if (context->desc.h264enc.ref_idx_l1 == VA_INVALID_ID)
            context->desc.h264enc.ref_idx_l1 = context->desc.h264enc.frame_idx[h264->RefPicList1[i].picture_id];
      }
   }

   if (h264->slice_type == 1)
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_B;
   else if (h264->slice_type == 0)
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_P;
   else if (h264->slice_type == 2) {
      if (context->desc.h264enc.is_idr){
         context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_IDR;
         context->desc.h264enc.idr_pic_id++;
	   } else
         context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_I;
   } else
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_SKIP;

   return VA_STATUS_SUCCESS;
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

      case VAEncSequenceParameterBufferType:
         vaStatus = handleVAEncSequenceParameterBufferType(drv, context, buf);
         break;

      case VAEncMiscParameterBufferType:
         vaStatus = handleVAEncMiscParameterBufferType(context, buf);
         break;

      case VAEncPictureParameterBufferType:
         vaStatus = handleVAEncPictureParameterBufferType(drv, context, buf);
         break;

      case VAEncSliceParameterBufferType:
         vaStatus = handleVAEncSliceParameterBufferType(drv, context, buf);
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
   vlVaBuffer *coded_buf;
   vlVaSurface *surf;
   void *feedback;

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

   pipe_mutex_lock(drv->mutex);
   surf = handle_table_get(drv->htab, context->target_id);
   context->mpeg4.frame_num++;

   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      coded_buf = context->coded_buf;
      getEncParamPreset(context);
      context->desc.h264enc.frame_num_cnt++;
      context->decoder->begin_frame(context->decoder, context->target, &context->desc.base);
      context->decoder->encode_bitstream(context->decoder, context->target,
                                         coded_buf->derived_surface.resource, &feedback);
      surf->feedback = feedback;
      surf->coded_buf = coded_buf;
   }

   context->decoder->end_frame(context->decoder, context->target, &context->desc.base);
   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      int idr_period = context->desc.h264enc.gop_size / context->gop_coeff;
      int p_remain_in_idr = idr_period - context->desc.h264enc.frame_num;
      surf->frame_num_cnt = context->desc.h264enc.frame_num_cnt;
      surf->force_flushed = false;
      if (context->first_single_submitted) {
         context->decoder->flush(context->decoder);
         context->first_single_submitted = false;
         surf->force_flushed = true;
      }
      if (p_remain_in_idr == 1) {
         if ((context->desc.h264enc.frame_num_cnt % 2) != 0) {
            context->decoder->flush(context->decoder);
            context->first_single_submitted = true;
         }
         else
            context->first_single_submitted = false;
         surf->force_flushed = true;
      }
   }
   pipe_mutex_unlock(drv->mutex);
   return VA_STATUS_SUCCESS;
}
