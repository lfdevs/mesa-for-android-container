/**************************************************************************
 *
 * Copyright 2009 Younes Manton.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_math.h"
#include "util/u_debug.h"

#include "vl_csc.h"

static const vl_csc_matrix identity =
{
   { 1.0f, 0.0f, 0.0f, 0.0f, },
   { 0.0f, 1.0f, 0.0f, 0.0f, },
   { 0.0f, 0.0f, 1.0f, 0.0f, }
};

static unsigned format_bpc(enum pipe_format format)
{
   const enum pipe_format plane_format = util_format_get_plane_format(format, 0);
   const struct util_format_description *desc = util_format_description(plane_format);

   for (unsigned i = 0; i < desc->nr_channels; i++) {
      if (desc->channel[i].type != UTIL_FORMAT_TYPE_VOID)
         return desc->channel[i].size;
   }
   UNREACHABLE("invalid format description");
}

void vl_csc_get_rgbyuv_matrix(enum pipe_video_vpp_matrix_coefficients coefficients,
                              enum pipe_format in_format, enum pipe_format out_format,
                              enum pipe_video_vpp_color_range in_color_range,
                              enum pipe_video_vpp_color_range out_color_range,
                              vl_csc_matrix *matrix)
{
   const bool in_yuv = util_format_is_yuv(in_format);
   const bool out_yuv = util_format_is_yuv(out_format);
   const unsigned bpc = format_bpc(in_format);
   const float scale = (1 << bpc) / ((1 << bpc) - 1.0);
   const float r_min = 16.0 / 256.0 * scale;
   const float r_max = 235.0 / 256.0 * scale;
   const float c_mid = 128.0 / 256.0 * scale;
   const float c_max = 240.0 / 256.0 * scale;
   float r_scale, g_scale, r_bias, g_bias;

   memcpy(matrix, &identity, sizeof(vl_csc_matrix));

   if (in_yuv != out_yuv && coefficients == PIPE_VIDEO_VPP_MCF_RGB)
      return;

   /* Convert input to full range, chroma to [-0.5,0.5]. */
   if (in_color_range == PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED) {
      r_scale = 1.0 / (r_max - r_min);
      g_scale = in_yuv ? 0.5 / (c_max - c_mid) : r_scale;
      r_bias = -r_min;
      g_bias = in_yuv ? -c_mid : r_bias;
   } else if (in_yuv) {
      r_scale = 1.0;
      g_scale = 0.5 / (1.0 - c_mid);
      r_bias = 0.0;
      g_bias = -c_mid;
   } else {
      r_scale = g_scale = 1.0;
      r_bias = g_bias = 0.0;
   }

   for (unsigned i = 0; i < 3; i++) {
      (*matrix)[i][i] = i == 0 ? r_scale : g_scale;
      (*matrix)[i][3] = (i == 0 ? r_bias : g_bias) * (*matrix)[i][i];
   }

   if (in_yuv != out_yuv) {
      float cr, cg, cb;

      switch (coefficients) {
      case PIPE_VIDEO_VPP_MCF_BT470BG:
      case PIPE_VIDEO_VPP_MCF_SMPTE170M:
         cr = 0.299;
         cb = 0.114;
         break;
      case PIPE_VIDEO_VPP_MCF_SMPTE240M:
         cr = 0.212;
         cb = 0.087;
         break;
      case PIPE_VIDEO_VPP_MCF_BT2020_NCL:
         cr = 0.2627;
         cb = 0.0593;
         break;
      case PIPE_VIDEO_VPP_MCF_BT709:
      default:
         cr = 0.2126;
         cb = 0.0722;
         break;
      }

      cg = 1.0 - cb - cr;

      if (in_yuv) {
         /* YUV to RGB */
         (*matrix)[0][0] = 1.0;
         (*matrix)[0][1] = 0.0;
         (*matrix)[0][2] = 2.0 - 2.0 * cr;
         (*matrix)[0][3] = 0.0;
         (*matrix)[1][0] = 1.0;
         (*matrix)[1][1] = (-cb / cg) * (2.0 - 2.0 * cb);
         (*matrix)[1][2] = (-cr / cg) * (2.0 - 2.0 * cr);
         (*matrix)[1][3] = 0.0;
         (*matrix)[2][0] = 1.0;
         (*matrix)[2][1] = 2.0 - 2.0 * cb;
         (*matrix)[2][2] = 0.0;
         (*matrix)[2][3] = 0.0;
      } else {
         /* RGB to YUV */
         (*matrix)[0][0] = cr;
         (*matrix)[0][1] = cg;
         (*matrix)[0][2] = cb;
         (*matrix)[0][3] = 0.0;
         (*matrix)[1][0] = (0.5 / (cb - 1.0)) * cr;
         (*matrix)[1][1] = (0.5 / (cb - 1.0)) * cg;
         (*matrix)[1][2] = 0.5;
         (*matrix)[1][3] = 0.0;
         (*matrix)[2][0] = 0.5;
         (*matrix)[2][1] = (0.5 / (cr - 1.0)) * cg;
         (*matrix)[2][2] = (0.5 / (cr - 1.0)) * cb;
         (*matrix)[2][3] = 0.0;
      }

      for (unsigned i = 0; i < 3; i++) {
         for (unsigned j = 0; j < 3; j++) {
            (*matrix)[i][j] *= j == 0 ? r_scale : g_scale;
            (*matrix)[i][3] += (*matrix)[i][j] * (j == 0 ? r_bias : g_bias);
         }
      }
   }

   /* Convert output to reduced range, chroma to [0.0,1.0]. */
   if (out_color_range == PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_REDUCED) {
      r_scale = (r_max - r_min) / 1.0;
      g_scale = in_yuv ? r_scale : (c_max - c_mid) / 0.5;
      r_bias = r_min;
      g_bias = in_yuv ? r_bias : c_mid;
   } else if (out_yuv) {
      r_scale = 1.0;
      g_scale = (1.0 - c_mid) / 0.5;
      r_bias = 0.0;
      g_bias = c_mid;
   } else {
      r_scale = g_scale = 1.0;
      r_bias = g_bias = 0.0;
   }

   for (unsigned i = 0; i < 3; i++) {
      for (unsigned j = 0; j < 4; j++)
         (*matrix)[i][j] *= i == 0 ? r_scale : g_scale;
      (*matrix)[i][3] += i == 0 ? r_bias : g_bias;
   }
}

void vl_csc_get_primaries_matrix(enum pipe_video_vpp_color_primaries in_color_primaries,
                                 enum pipe_video_vpp_color_primaries out_color_primaries,
                                 vl_csc_matrix *matrix)
{
   if (in_color_primaries == out_color_primaries) {
      memcpy(matrix, &identity, sizeof(vl_csc_matrix));
      return;
   }

   switch (in_color_primaries) {
   case PIPE_VIDEO_VPP_PRI_SMPTE170M:
   case PIPE_VIDEO_VPP_PRI_SMPTE240M:
      switch (out_color_primaries) {
      case PIPE_VIDEO_VPP_PRI_BT2020:
         memcpy(matrix, &(vl_csc_matrix){
            { 0.595254, 0.349314, 0.055432, 0.0 },
            { 0.081244, 0.891503, 0.027253, 0.0 },
            { 0.015512, 0.081912, 0.902576, 0.0 },
         }, sizeof(vl_csc_matrix));
         break;

      case PIPE_VIDEO_VPP_PRI_BT709:
      default:
         memcpy(matrix, &(vl_csc_matrix){
            {  0.939543,  0.050181, 0.010276, 0.0 },
            {  0.017772,  0.965793, 0.016435, 0.0 },
            { -0.001622, -0.004370, 1.005991, 0.0 },
         }, sizeof(vl_csc_matrix));
         break;
      }
      break;

   case PIPE_VIDEO_VPP_PRI_BT2020:
      switch (out_color_primaries) {
      case PIPE_VIDEO_VPP_PRI_SMPTE170M:
      case PIPE_VIDEO_VPP_PRI_SMPTE240M:
         memcpy(matrix, &(vl_csc_matrix){
            {  1.776133, -0.687820, -0.088313, 0.0 },
            { -0.161375,  1.187315, -0.025940, 0.0 },
            { -0.015881, -0.095931,  1.111812, 0.0 },
         }, sizeof(vl_csc_matrix));
         break;

      case PIPE_VIDEO_VPP_PRI_BT709:
      default:
         memcpy(matrix, &(vl_csc_matrix){
            {  1.660491, -0.587641, -0.072850, 0.0 },
            { -0.124550,  1.132900, -0.008349, 0.0 },
            { -0.018151, -0.100579,  1.118729, 0.0 },
         }, sizeof(vl_csc_matrix));
         break;
      }
      break;

   case PIPE_VIDEO_VPP_PRI_BT709:
   default:
      switch (out_color_primaries) {
      case PIPE_VIDEO_VPP_PRI_SMPTE170M:
      case PIPE_VIDEO_VPP_PRI_SMPTE240M:
         memcpy(matrix, &(vl_csc_matrix){
            {  1.065379, -0.055401, -0.009978, 0.0 },
            { -0.019633,  1.036363, -0.016731, 0.0 },
            {  0.001632,  0.004412,  0.993956, 0.0 },
         }, sizeof(vl_csc_matrix));
         break;

      case PIPE_VIDEO_VPP_PRI_BT2020:
         memcpy(matrix, &(vl_csc_matrix){
            { 0.627404, 0.329283, 0.043313, 0.0 },
            { 0.069097, 0.919540, 0.011362, 0.0 },
            { 0.016391, 0.088013, 0.895595, 0.0 },
         }, sizeof(vl_csc_matrix));
         break;

      default:
         memcpy(matrix, &identity, sizeof(vl_csc_matrix));
      }
      break;
   }
}
