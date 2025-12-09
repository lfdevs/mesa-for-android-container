/*
 * Copyright © 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_video_codec.h"

#include "util/u_video.h"

#include "va_private.h"

/*
 * in->quality = 0; without any settings, it is using speed preset
 *                  and no preencode and no vbaq. It is the fastest setting.
 * in->quality = 1; suggested setting, with balanced preset, and
 *                  preencode and vbaq
 * in->quality = others; it is the customized setting
 *                  with valid bit (bit #0) set to "1"
 *                  for example:
 *
 *                  0x3  (balance preset, no pre-encoding, no vbaq)
 *                  0x13 (balanced preset, no pre-encoding, vbaq)
 *                  0x13 (balanced preset, no pre-encoding, vbaq)
 *                  0x9  (speed preset, pre-encoding, no vbaq)
 *                  0x19 (speed preset, pre-encoding, vbaq)
 *
 *                  The quality value has to be treated as a combination
 *                  of preset mode, pre-encoding and vbaq settings.
 *                  The quality and speed could be vary according to
 *                  different settings,
 */
void
vlVaHandleVAEncMiscParameterTypeQualityLevel(struct pipe_enc_quality_modes *p, vlVaQualityBits *in)
{
   if (!in->quality) {
      p->level = 0;
      p->preset_mode = PRESET_MODE_SPEED;
      p->pre_encode_mode = PREENCODING_MODE_DISABLE;
      p->vbaq_mode = VBAQ_DISABLE;
      return;
   }

   if (p->level != in->quality) {
      if (in->quality == 1) {
         p->preset_mode = PRESET_MODE_BALANCE;
         p->pre_encode_mode = PREENCODING_MODE_DEFAULT;
         p->vbaq_mode = VBAQ_AUTO;
      } else {
         p->preset_mode = in->preset_mode;
         p->pre_encode_mode = in->pre_encode_mode;
         p->vbaq_mode = in->vbaq_mode;
      }
   }
   p->level = in->quality;
}

static VAStatus
handleVAEncMiscParameterTypeRateControl(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeRateControlH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeRateControlHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeRateControlAV1(context, misc);
      break;
#endif
   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeFrameRate(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeFrameRateH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeFrameRateHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeFrameRateAV1(context, misc);
      break;
#endif
   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeTemporalLayer(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeTemporalLayerH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeTemporalLayerHEVC(context, misc);
      break;

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncSequenceParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncSequenceParameterBufferTypeH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncSequenceParameterBufferTypeHEVC(drv, context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncSequenceParameterBufferTypeAV1(drv, context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeQualityLevel(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeQualityLevelH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeQualityLevelHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeQualityLevelAV1(context, misc);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeMaxFrameSize(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeMaxFrameSizeH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeMaxFrameSizeHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeMaxFrameSizeAV1(context, misc);
      break;
#endif

   default:
      break;
   }

   return status;
}
static VAStatus
handleVAEncMiscParameterTypeHRD(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeHRDH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeHRDHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeHRDAV1(context, misc);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeMaxSliceSize(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;
   VAEncMiscParameterMaxSliceSize *max_slice_size_buffer = (VAEncMiscParameterMaxSliceSize *)misc->data;
   switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         context->desc.h264enc.slice_mode = PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE;
         context->desc.h264enc.max_slice_bytes = max_slice_size_buffer->max_slice_size;
      } break;
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         context->desc.h265enc.slice_mode = PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE;
         context->desc.h265enc.max_slice_bytes = max_slice_size_buffer->max_slice_size;
      } break;
      default:
         break;
   }
   return status;
}

static VAStatus
handleVAEncMiscParameterTypeRIR(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;
   struct pipe_enc_intra_refresh *p_intra_refresh = NULL;

   switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         p_intra_refresh = &context->desc.h264enc.intra_refresh;
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         p_intra_refresh = &context->desc.h265enc.intra_refresh;
         break;
#if VA_CHECK_VERSION(1, 16, 0)
      case PIPE_VIDEO_FORMAT_AV1:
         p_intra_refresh = &context->desc.av1enc.intra_refresh;
         break;
#endif
      default:
         return status;
   };

   VAEncMiscParameterRIR *ir = (VAEncMiscParameterRIR *)misc->data;

   if (ir->rir_flags.value == VA_ENC_INTRA_REFRESH_ROLLING_ROW)
      p_intra_refresh->mode = INTRA_REFRESH_MODE_UNIT_ROWS;
   else if (ir->rir_flags.value == VA_ENC_INTRA_REFRESH_ROLLING_COLUMN)
      p_intra_refresh->mode = INTRA_REFRESH_MODE_UNIT_COLUMNS;
   else if (ir->rir_flags.value) /* if any other values to use the default one*/
      p_intra_refresh->mode = INTRA_REFRESH_MODE_UNIT_COLUMNS;
   else /* if no mode specified then no intra-refresh */
      p_intra_refresh->mode = INTRA_REFRESH_MODE_NONE;

   /* intra refresh should be started with sequence level headers */
   p_intra_refresh->need_sequence_header = 0;
   if (p_intra_refresh->mode) {
      p_intra_refresh->region_size = ir->intra_insert_size;
      p_intra_refresh->offset = ir->intra_insertion_location;
      if (p_intra_refresh->offset == 0)
         p_intra_refresh->need_sequence_header = 1;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeROI(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;
   struct pipe_enc_roi *proi= NULL;
   switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         proi = &context->desc.h264enc.roi;
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         proi = &context->desc.h265enc.roi;
         break;
#if VA_CHECK_VERSION(1, 16, 0)
      case PIPE_VIDEO_FORMAT_AV1:
         proi = &context->desc.av1enc.roi;
         break;
#endif
      default:
         break;
   };

   if (proi) {
      VAEncMiscParameterBufferROI *roi = (VAEncMiscParameterBufferROI *)misc->data;
      /* do not support priority type, and the maximum region is 32  */
      if ((roi->num_roi > 0 && roi->roi_flags.bits.roi_value_is_qp_delta == 0)
           || roi->num_roi > PIPE_ENC_ROI_REGION_NUM_MAX)
         status = VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;
      else {
         uint32_t i;
         VAEncROI *src = roi->roi;

         proi->num = roi->num_roi;
         for (i = 0; i < roi->num_roi; i++) {
            proi->region[i].valid = true;
            proi->region[i].x = src->roi_rectangle.x;
            proi->region[i].y = src->roi_rectangle.y;
            proi->region[i].width = src->roi_rectangle.width;
            proi->region[i].height = src->roi_rectangle.height;
            proi->region[i].qp_value = (int32_t)CLAMP(src->roi_value, roi->min_delta_qp, roi->max_delta_qp);
            src++;
         }

         for (; i < PIPE_ENC_ROI_REGION_NUM_MAX; i++)
            proi->region[i].valid = false;
      }
   }

   return status;
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

   case VAEncMiscParameterTypeTemporalLayerStructure:
      vaStatus = handleVAEncMiscParameterTypeTemporalLayer(context, misc);
      break;

   case VAEncMiscParameterTypeQualityLevel:
      vaStatus = handleVAEncMiscParameterTypeQualityLevel(context, misc);
      break;

   case VAEncMiscParameterTypeMaxFrameSize:
      vaStatus = handleVAEncMiscParameterTypeMaxFrameSize(context, misc);
      break;

   case VAEncMiscParameterTypeHRD:
      vaStatus = handleVAEncMiscParameterTypeHRD(context, misc);
      break;

   case VAEncMiscParameterTypeRIR:
      vaStatus = handleVAEncMiscParameterTypeRIR(context, misc);
      break;

   case VAEncMiscParameterTypeMaxSliceSize:
      vaStatus = handleVAEncMiscParameterTypeMaxSliceSize(context, misc);
      break;

   case VAEncMiscParameterTypeROI:
      vaStatus = handleVAEncMiscParameterTypeROI(context, misc);
      break;

   default:
      break;
   }

   return vaStatus;
}

static VAStatus
handleVAEncPictureParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncPictureParameterBufferTypeH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncPictureParameterBufferTypeHEVC(drv, context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncPictureParameterBufferTypeAV1(drv, context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncSliceParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncSliceParameterBufferTypeH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncSliceParameterBufferTypeHEVC(drv, context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncSliceParameterBufferTypeAV1(drv, context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncPackedHeaderParameterBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAEncPackedHeaderParameterBuffer *param = buf->data;

   context->packed_header_emulation_bytes = param->has_emulation_bytes;
   context->packed_header_type = param->type;

   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVAEncPackedHeaderDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncPackedHeaderDataBufferTypeH264(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncPackedHeaderDataBufferTypeHEVC(context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncPackedHeaderDataBufferTypeAV1(context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAStatsStatisticsBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   if (context->decoder->entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      return VA_STATUS_ERROR_UNIMPLEMENTED;

   if (!buf->derived_surface.resource)
      buf->derived_surface.resource = pipe_buffer_create(drv->pipe->screen, PIPE_BIND_VERTEX_BUFFER,
                                            PIPE_USAGE_STREAM, buf->size);

   context->target->statistics_data = buf->derived_surface.resource;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleEncBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus vaStatus = VA_STATUS_SUCCESS;

   switch (buf->type) {
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

   case VAEncPackedHeaderParameterBufferType:
      vaStatus = handleVAEncPackedHeaderParameterBufferType(context, buf);
      break;

   case VAEncPackedHeaderDataBufferType:
      vaStatus = handleVAEncPackedHeaderDataBufferType(context, buf);
      break;

   case VAStatsStatisticsBufferType:
      vaStatus = handleVAStatsStatisticsBufferType(drv, context, buf);
      break;

   default:
      break;
   }

   return vaStatus;
}
