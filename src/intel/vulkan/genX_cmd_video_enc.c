/*
 * Copyright © 2024 Igalia
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"
#include "genX_mi_builder.h"

static int
anv_get_max_vmv_range(StdVideoH264LevelIdc level)
{
   int max_vmv_range;

   switch(level) {
   case STD_VIDEO_H264_LEVEL_IDC_1_0:
      max_vmv_range = 256;
      break;
   case STD_VIDEO_H264_LEVEL_IDC_1_1:
   case STD_VIDEO_H264_LEVEL_IDC_1_2:
   case STD_VIDEO_H264_LEVEL_IDC_1_3:
   case STD_VIDEO_H264_LEVEL_IDC_2_0:
      max_vmv_range = 512;
      break;
   case STD_VIDEO_H264_LEVEL_IDC_2_1:
   case STD_VIDEO_H264_LEVEL_IDC_2_2:
   case STD_VIDEO_H264_LEVEL_IDC_3_0:
      max_vmv_range = 1024;
      break;

   case STD_VIDEO_H264_LEVEL_IDC_3_1:
   case STD_VIDEO_H264_LEVEL_IDC_3_2:
   case STD_VIDEO_H264_LEVEL_IDC_4_0:
   case STD_VIDEO_H264_LEVEL_IDC_4_1:
   case STD_VIDEO_H264_LEVEL_IDC_4_2:
   case STD_VIDEO_H264_LEVEL_IDC_5_0:
   case STD_VIDEO_H264_LEVEL_IDC_5_1:
   case STD_VIDEO_H264_LEVEL_IDC_5_2:
   case STD_VIDEO_H264_LEVEL_IDC_6_0:
   case STD_VIDEO_H264_LEVEL_IDC_6_1:
   case STD_VIDEO_H264_LEVEL_IDC_6_2:
   default:
      max_vmv_range = 2048;
      break;
   }

   return max_vmv_range;
}

static bool
anv_post_deblock_enable(const StdVideoH264PictureParameterSet *pps, const VkVideoEncodeH264PictureInfoKHR *frame_info)
{

   if (!pps->flags.deblocking_filter_control_present_flag)
      return true;

   for (uint32_t slice_id = 0; slice_id < frame_info->naluSliceEntryCount; slice_id++) {
      const VkVideoEncodeH264NaluSliceInfoKHR *nalu = &frame_info->pNaluSliceEntries[slice_id];
      const StdVideoEncodeH264SliceHeader *slice_header = nalu->pStdSliceHeader;

      if (slice_header->disable_deblocking_filter_idc != 1)
         return true;
   }

   return false;
}

static uint8_t
anv_vdenc_h264_picture_type(StdVideoH264PictureType pic_type)
{
   if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_I || pic_type == STD_VIDEO_H264_PICTURE_TYPE_IDR) {
      return 0;
   } else {
      return 1;
   }
}

static uint8_t
anv_vdenc_h265_picture_type(StdVideoH265PictureType pic_type)
{
   if (pic_type == STD_VIDEO_H265_PICTURE_TYPE_I || pic_type == STD_VIDEO_H265_PICTURE_TYPE_IDR) {
      return 0;
   } else {
      return 2;
   }
}

static const uint8_t vdenc_const_qp_lambda[42] = {
   0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02,
   0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x07,
   0x07, 0x08, 0x09, 0x0a, 0x0c, 0x0d, 0x0f, 0x11, 0x13, 0x15,
   0x17, 0x1a, 0x1e, 0x21, 0x25, 0x2a, 0x2f, 0x35, 0x3b, 0x42,
   0x4a, 0x53,
};

/* P frame */
static const uint8_t vdenc_const_qp_lambda_p[42] = {
   0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02,
   0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x07,
   0x07, 0x08, 0x09, 0x0a, 0x0c, 0x0d, 0x0f, 0x11, 0x13, 0x15,
   0x17, 0x1a, 0x1e, 0x21, 0x25, 0x2a, 0x2f, 0x35, 0x3b, 0x42,
   0x4a, 0x53,
};

static const uint16_t vdenc_const_skip_threshold_p[27] = {
   0x0000, 0x0000, 0x0000, 0x0000, 0x0002, 0x0004, 0x0007, 0x000b,
   0x0011, 0x0019, 0x0023, 0x0032, 0x0044, 0x005b, 0x0077, 0x0099,
   0x00c2, 0x00f1, 0x0128, 0x0168, 0x01b0, 0x0201, 0x025c, 0x02c2,
   0x0333, 0x03b0, 0x0000,
};

static const uint16_t vdenc_const_sic_forward_transform_coeff_threshold_0_p[27] = {
   0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x07, 0x09, 0x0b, 0x0e,
   0x12, 0x14, 0x18, 0x1d, 0x20, 0x25, 0x2a, 0x34, 0x39, 0x3f,
   0x4e, 0x51, 0x5b, 0x63, 0x6f, 0x7f, 0x00,
};

static const uint8_t vdenc_const_sic_forward_transform_coeff_threshold_1_p[27] = {
   0x03, 0x04, 0x05, 0x05, 0x07, 0x09, 0x0b, 0x0e, 0x12, 0x17,
   0x1c, 0x21, 0x27, 0x2c, 0x33, 0x3b, 0x41, 0x51, 0x5c, 0x1a,
   0x1e, 0x21, 0x22, 0x26, 0x2c, 0x30, 0x00,
};

static const uint8_t vdenc_const_sic_forward_transform_coeff_threshold_2_p[27] = {
   0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x07, 0x09, 0x0b, 0x0e,
   0x12, 0x14, 0x18, 0x1d, 0x20, 0x25, 0x2a, 0x34, 0x39, 0x0f,
   0x13, 0x14, 0x16, 0x18, 0x1b, 0x1f, 0x00,
};

static const uint8_t vdenc_const_sic_forward_transform_coeff_threshold_3_p[27] = {
   0x04, 0x05, 0x06, 0x09, 0x0b, 0x0d, 0x12, 0x16, 0x1b, 0x23,
   0x2c, 0x33, 0x3d, 0x45, 0x4f, 0x5b, 0x66, 0x7f, 0x8e, 0x2a,
   0x2f, 0x32, 0x37, 0x3c, 0x45, 0x4c, 0x00,
};

static const int vdenc_mode_const[2][12][52] = {
    //INTRASLICE
    {
        //LUTMODE_INTRA_NONPRED
        {
            14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,         //QP=[0 ~12]
            16, 18, 22, 24, 13, 15, 16, 18, 13, 15, 15, 12, 14,         //QP=[13~25]
            12, 12, 10, 10, 11, 10, 10, 10, 9, 9, 8, 8, 8,              //QP=[26~38]
            8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7,                      //QP=[39~51]
        },

        //LUTMODE_INTRA_16x16, LUTMODE_INTRA
        {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[0 ~12]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[13~25]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[26~38]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[39~51]
        },

        //LUTMODE_INTRA_8x8
        {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  //QP=[0 ~12]
            0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,  //QP=[13~25]
            1, 1, 1, 1, 1, 4, 4, 4, 4, 6, 6, 6, 6,  //QP=[26~38]
            6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7,  //QP=[39~51]
        },

        //LUTMODE_INTRA_4x4
        {
            56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56, 56,   //QP=[0 ~12]
            64, 72, 80, 88, 48, 56, 64, 72, 53, 59, 64, 56, 64,   //QP=[13~25]
            57, 64, 58, 55, 64, 64, 64, 64, 59, 59, 60, 57, 50,   //QP=[26~38]
            46, 42, 38, 34, 31, 27, 23, 22, 19, 18, 16, 14, 13,   //QP=[39~51]
        },

        //LUTMODE_INTER_16x8, LUTMODE_INTER_8x16
        { 0, },

        //LUTMODE_INTER_8X8Q
        { 0, },

        //LUTMODE_INTER_8X4Q, LUTMODE_INTER_4X8Q, LUTMODE_INTER_16x8_FIELD
        { 0, },

        //LUTMODE_INTER_4X4Q, LUTMODE_INTER_8X8_FIELD
        { 0, },

        //LUTMODE_INTER_16x16, LUTMODE_INTER
        { 0, },

        //LUTMODE_INTER_BWD
        { 0, },

        //LUTMODE_REF_ID
        { 0, },

        //LUTMODE_INTRA_CHROMA
        { 0, },
    },

    //PREDSLICE
    {
        //LUTMODE_INTRA_NONPRED
        {
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,     //QP=[0 ~12]
            7, 8, 9, 10, 5, 6, 7, 8, 6, 7, 7, 7, 7,    //QP=[13~25]
            6, 7, 7, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7,     //QP=[26~38]
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,     //QP=[39~51]
        },

        //LUTMODE_INTRA_16x16, LUTMODE_INTRA
        {
            21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
            24, 28, 31, 35, 19, 21, 24, 28, 20, 24, 25, 21, 24,
            24, 24, 24, 21, 24, 24, 26, 24, 24, 24, 24, 24, 24,
            24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,

        },

        //LUTMODE_INTRA_8x8
        {
            26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,   //QP=[0 ~12]
            28, 32, 36, 40, 22, 26, 28, 32, 24, 26, 30, 26, 28,   //QP=[13~25]
            26, 28, 26, 26, 30, 28, 28, 28, 26, 28, 28, 26, 28,   //QP=[26~38]
            28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,   //QP=[39~51]
        },

        //LUTMODE_INTRA_4x4
        {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,   //QP=[0 ~12]
            72, 80, 88, 104, 56, 64, 72, 80, 58, 68, 76, 64, 68,  //QP=[13~25]
            64, 68, 68, 64, 70, 70, 70, 70, 68, 68, 68, 68, 68,   //QP=[26~38]
            68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68,   //QP=[39~51]
        },

        //LUTMODE_INTER_16x8, LUTMODE_INTER_8x16
        {
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,      //QP=[0 ~12]
            8, 9, 11, 12, 6, 7, 9, 10, 7, 8, 9, 8, 9,   //QP=[13~25]
            8, 9, 8, 8, 9, 9, 9, 9, 8, 8, 8, 8, 8,      //QP=[26~38]
            8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,      //QP=[39~51]
        },

        //LUTMODE_INTER_8X8Q
        {
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,   //QP=[0 ~12]
            2, 3, 3, 3, 2, 2, 2, 3, 2, 2, 2, 2, 3,   //QP=[13~25]
            2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,   //QP=[26~38]
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,   //QP=[39~51]
        },

        //LUTMODE_INTER_8X4Q, LUTMODE_INTER_4X8Q, LUTMODE_INTER_16X8_FIELD
        {
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   //QP=[0 ~12]
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   //QP=[13~25]
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   //QP=[26~38]
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   //QP=[39~51]
        },

        //LUTMODE_INTER_4X4Q, LUTMODE_INTER_8x8_FIELD
        {
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,   //QP=[0 ~12]
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,   //QP=[13~25]
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,   //QP=[26~38]
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,   //QP=[39~51]
        },

        //LUTMODE_INTER_16x16, LUTMODE_INTER
        {
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,   //QP=[0 ~12]
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,   //QP=[13~25]
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,   //QP=[26~38]
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,   //QP=[39~51]
        },

        //LUTMODE_INTER_BWD
        {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[0 ~12]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[13~25]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[26~38]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[39~51]
        },

        //LUTMODE_REF_ID
        {
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    //QP=[0 ~12]
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    //QP=[13~25]
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    //QP=[26~38]
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    //QP=[39~51]
        },

        //LUTMODE_INTRA_CHROMA
        {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[0 ~12]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[13~25]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[26~38]
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    //QP=[39~51]
        },
    },
};


#define VDENC_LUTMODE_INTRA_NONPRED             0x00
#define VDENC_LUTMODE_INTRA                     0x01
#define VDENC_LUTMODE_INTRA_16x16               0x01
#define VDENC_LUTMODE_INTRA_8x8                 0x02
#define VDENC_LUTMODE_INTRA_4x4                 0x03
#define VDENC_LUTMODE_INTER_16x8                0x04
#define VDENC_LUTMODE_INTER_8x16                0x04
#define VDENC_LUTMODE_INTER_8X8Q                0x05
#define VDENC_LUTMODE_INTER_8X4Q                0x06
#define VDENC_LUTMODE_INTER_4X8Q                0x06
#define VDENC_LUTMODE_INTER_16x8_FIELD          0x06
#define VDENC_LUTMODE_INTER_4X4Q                0x07
#define VDENC_LUTMODE_INTER_8x8_FIELD           0x07
#define VDENC_LUTMODE_INTER                     0x08
#define VDENC_LUTMODE_INTER_16x16               0x08
#define VDENC_LUTMODE_INTER_BWD                 0x09
#define VDENC_LUTMODE_REF_ID                    0x0A
#define VDENC_LUTMODE_INTRA_CHROMA              0x0B

static unsigned char
map_44_lut_value(unsigned int v, unsigned char max)
{
    unsigned int maxcost;
    int d;
    unsigned char ret;

    if (v == 0) {
        return 0;
    }

    maxcost = ((max & 15) << (max >> 4));

    if (v >= maxcost) {
        return max;
    }

    d = (int)(log((double)v) / log(2.0)) - 3;

    if (d < 0) {
        d = 0;
    }

    ret = (unsigned char)((d << 4) + (int)((v + (d == 0 ? 0 : (1 << (d - 1)))) >> d));
    ret = (ret & 0xf) == 0 ? (ret | 8) : ret;

    return ret;
}

static void update_costs(uint8_t *mode_cost, uint8_t *mv_cost, uint8_t *hme_mv_cost, int qp, StdVideoH264PictureType pic_type)
{
   int frame_type = anv_vdenc_h264_picture_type(pic_type);

   memset(mode_cost, 0, 12 * sizeof(uint8_t));
   memset(mv_cost, 0, 8 * sizeof(uint8_t));
   memset(hme_mv_cost, 0, 8 * sizeof(uint8_t));

   mode_cost[VDENC_LUTMODE_INTRA_NONPRED] = map_44_lut_value((uint32_t)(vdenc_mode_const[frame_type][VDENC_LUTMODE_INTRA_NONPRED][qp]), 0x6f);
   mode_cost[VDENC_LUTMODE_INTRA_16x16] = map_44_lut_value((uint32_t)(vdenc_mode_const[frame_type][VDENC_LUTMODE_INTRA_16x16][qp]), 0x8f);
   mode_cost[VDENC_LUTMODE_INTRA_8x8] = map_44_lut_value((uint32_t)(vdenc_mode_const[frame_type][VDENC_LUTMODE_INTRA_8x8][qp]), 0x8f);
   mode_cost[VDENC_LUTMODE_INTRA_4x4] = map_44_lut_value((uint32_t)(vdenc_mode_const[frame_type][VDENC_LUTMODE_INTRA_4x4][qp]), 0x8f);
}

static void
anv_h264_encode_video(struct anv_cmd_buffer *cmd, const VkVideoEncodeInfoKHR *enc_info)
{
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, enc_info->dstBuffer);

   struct anv_video_session *vid = cmd->video.vid;
   struct anv_video_session_params *params = cmd->video.params;

   const struct VkVideoEncodeH264PictureInfoKHR *frame_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);

   const StdVideoH264SequenceParameterSet *sps = vk_video_find_h264_enc_std_sps(&params->vk, frame_info->pStdPictureInfo->seq_parameter_set_id);
   const StdVideoH264PictureParameterSet *pps = vk_video_find_h264_enc_std_pps(&params->vk, frame_info->pStdPictureInfo->pic_parameter_set_id);
   const StdVideoEncodeH264ReferenceListsInfo *ref_list_info = frame_info->pStdPictureInfo->pRefLists;

   const struct anv_image_view *iv = anv_image_view_from_handle(enc_info->srcPictureResource.imageViewBinding);
   const struct anv_image *src_img = iv->image;
   bool post_deblock_enable = anv_post_deblock_enable(pps, frame_info);
   bool rc_disable = cmd->video.params->rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
   uint8_t dpb_idx[ANV_VIDEO_H264_MAX_NUM_REF_FRAME] = { 0,};

   const struct anv_image_view *base_ref_iv;
   if (enc_info->pSetupReferenceSlot) {
      base_ref_iv = anv_image_view_from_handle(enc_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
   } else {
      base_ref_iv = iv;
   }

   const struct anv_image *base_ref_img = base_ref_iv->image;

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.VideoPipelineCacheInvalidate = 1;
   };

#if GFX_VER >= 12
   anv_batch_emit(&cmd->batch, GENX(MI_FORCE_WAKEUP), wake) {
      wake.MFXPowerWellControl = 1;
      wake.MaskBits = 768;
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   anv_batch_emit(&cmd->batch, GENX(MFX_PIPE_MODE_SELECT), pipe_mode) {
      pipe_mode.StandardSelect = SS_AVC;
      pipe_mode.CodecSelect = Encode;
      pipe_mode.FrameStatisticsStreamOutEnable = true;
      pipe_mode.ScaledSurfaceEnable = false;
      pipe_mode.PreDeblockingOutputEnable = !post_deblock_enable;
      pipe_mode.PostDeblockingOutputEnable = post_deblock_enable;
      pipe_mode.StreamOutEnable = false;
      pipe_mode.VDEncMode = VM_VDEncMode;
      pipe_mode.DecoderShortFormatMode = LongFormatDriverInterface;
   }

#if GFX_VER >= 12
   anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }
#endif

   for (uint32_t i = 0; i < 2; i++) {
      anv_batch_emit(&cmd->batch, GENX(MFX_SURFACE_STATE), surface) {
         const struct anv_image *img_ = i == 0 ? base_ref_img : src_img;

         surface.Width = img_->vk.extent.width - 1;
         surface.Height = img_->vk.extent.height - 1;
         /* TODO. add a surface for MFX_ReconstructedScaledReferencePicture */
         surface.SurfaceID = i == 0 ? MFX_ReferencePicture : MFX_SourceInputPicture;
         surface.TileWalk = TW_YMAJOR;
         surface.TiledSurface = img_->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
         surface.SurfacePitch = img_->planes[0].primary_surface.isl.row_pitch_B - 1;
         surface.InterleaveChroma = true;
         surface.SurfaceFormat = MFX_PLANAR_420_8;

         surface.YOffsetforUCb = img_->planes[1].primary_surface.memory_range.offset /
            img_->planes[0].primary_surface.isl.row_pitch_B;
         surface.YOffsetforVCr = img_->planes[1].primary_surface.memory_range.offset /
            img_->planes[0].primary_surface.isl.row_pitch_B;
      }
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_PIPE_BUF_ADDR_STATE), buf) {
      if (post_deblock_enable) {
         buf.PostDeblockingDestinationAddress =
            anv_image_address(base_ref_img, &base_ref_img->planes[0].primary_surface.memory_range);
      } else {
         buf.PreDeblockingDestinationAddress =
            anv_image_address(base_ref_img, &base_ref_img->planes[0].primary_surface.memory_range);
      }
      buf.PreDeblockingDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.PreDeblockingDestinationAddress.bo, 0),
      };
      buf.PostDeblockingDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.PostDeblockingDestinationAddress.bo, 0),
      };

      buf.OriginalUncompressedPictureSourceAddress =
         anv_image_address(src_img, &src_img->planes[0].primary_surface.memory_range);
      buf.OriginalUncompressedPictureSourceAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.OriginalUncompressedPictureSourceAddress.bo, 0),
      };

      buf.StreamOutDataDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.IntraRowStoreScratchBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H264_INTRA_ROW_STORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H264_INTRA_ROW_STORE].offset
      };
      buf.IntraRowStoreScratchBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.IntraRowStoreScratchBufferAddress.bo, 0),
      };

      buf.DeblockingFilterRowStoreScratchAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H264_DEBLOCK_FILTER_ROW_STORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H264_DEBLOCK_FILTER_ROW_STORE].offset
      };
      buf.DeblockingFilterRowStoreScratchAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.DeblockingFilterRowStoreScratchAddress.bo, 0),
      };

      struct anv_bo *ref_bo = NULL;

      for (unsigned i = 0; i < enc_info->referenceSlotCount; i++) {
         const struct anv_image_view *ref_iv =
            anv_image_view_from_handle(enc_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
         int slot_idx = enc_info->pReferenceSlots[i].slotIndex;
         assert(slot_idx < ANV_VIDEO_H264_MAX_NUM_REF_FRAME);

         dpb_idx[slot_idx] = i;

         buf.ReferencePictureAddress[i] =
            anv_image_address(ref_iv->image, &ref_iv->image->planes[0].primary_surface.memory_range);

         if (i == 0)
            ref_bo = ref_iv->image->bindings[0].address.bo;
      }

      buf.ReferencePictureAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, ref_bo, 0),
      };

      buf.MBStatusBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.MBILDBStreamOutBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      buf.SecondMBILDBStreamOutBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      /* TODO. Add for scaled reference surface */
      buf.ScaledReferenceSurfaceAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.ScaledReferenceSurfaceAddress.bo, 0),
      };
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_IND_OBJ_BASE_ADDR_STATE), index_obj) {
      index_obj.MFXIndirectBitstreamObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      index_obj.MFXIndirectMVObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      index_obj.MFDIndirectITCOEFFObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      index_obj.MFDIndirectITDBLKObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      index_obj.MFCIndirectPAKBSEObjectAddress = anv_address_add(dst_buffer->address, 0);

      index_obj.MFCIndirectPAKBSEObjectAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, index_obj.MFCIndirectPAKBSEObjectAddress.bo, 0),
      };
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_BSP_BUF_BASE_ADDR_STATE), bsp) {
      bsp.BSDMPCRowStoreScratchBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H264_BSD_MPC_ROW_SCRATCH].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H264_BSD_MPC_ROW_SCRATCH].offset
      };

      bsp.BSDMPCRowStoreScratchBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, bsp.BSDMPCRowStoreScratchBufferAddress.bo, 0),
      };

      bsp.MPRRowStoreScratchBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      bsp.BitplaneReadBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_PIPE_MODE_SELECT), vdenc_pipe_mode) {
      vdenc_pipe_mode.StandardSelect = SS_AVC;
      vdenc_pipe_mode.PAKChromaSubSamplingType = _420;
#if GFX_VER >= 12
      //vdenc_pipe_mode.HMERegionPrefetchEnable = !vdenc_pipe_mode.TLBPrefetchEnable;
      vdenc_pipe_mode.SourceLumaPackedDataTLBPrefetchEnable = true;
      vdenc_pipe_mode.SourceChromaTLBPrefetchEnable = true;
      vdenc_pipe_mode.HzShift32Minus1Src = 3;
      vdenc_pipe_mode.PrefetchOffsetforSource = 4;
#endif
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_SRC_SURFACE_STATE), vdenc_surface) {
      vdenc_surface.SurfaceState.Width = src_img->vk.extent.width - 1;
      vdenc_surface.SurfaceState.Height = src_img->vk.extent.height - 1;
      vdenc_surface.SurfaceState.SurfaceFormat = VDENC_PLANAR_420_8;
      vdenc_surface.SurfaceState.SurfacePitch = src_img->planes[0].primary_surface.isl.row_pitch_B - 1;

#if GFX_VER == 9
      vdenc_surface.SurfaceState.InterleaveChroma = true;
#endif

      vdenc_surface.SurfaceState.TileWalk = TW_YMAJOR;
      vdenc_surface.SurfaceState.TiledSurface = src_img->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
      vdenc_surface.SurfaceState.YOffsetforUCb = src_img->planes[1].primary_surface.memory_range.offset /
         src_img->planes[0].primary_surface.isl.row_pitch_B;
      vdenc_surface.SurfaceState.YOffsetforVCr = src_img->planes[1].primary_surface.memory_range.offset /
         src_img->planes[0].primary_surface.isl.row_pitch_B;
      vdenc_surface.SurfaceState.Colorspaceselection = 1;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_REF_SURFACE_STATE), vdenc_surface) {
      vdenc_surface.SurfaceState.Width = base_ref_img->vk.extent.width - 1;
      vdenc_surface.SurfaceState.Height = base_ref_img->vk.extent.height - 1;
      vdenc_surface.SurfaceState.SurfaceFormat = VDENC_PLANAR_420_8;
#if GFX_VER == 9
      vdenc_surface.SurfaceState.InterleaveChroma = true;
#endif
      vdenc_surface.SurfaceState.SurfacePitch = base_ref_img->planes[0].primary_surface.isl.row_pitch_B - 1;

      vdenc_surface.SurfaceState.TileWalk = TW_YMAJOR;
      vdenc_surface.SurfaceState.TiledSurface = base_ref_img->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
      vdenc_surface.SurfaceState.YOffsetforUCb = base_ref_img->planes[1].primary_surface.memory_range.offset /
         base_ref_img->planes[0].primary_surface.isl.row_pitch_B;
      vdenc_surface.SurfaceState.YOffsetforVCr = base_ref_img->planes[1].primary_surface.memory_range.offset /
         base_ref_img->planes[0].primary_surface.isl.row_pitch_B;
   }

   /* TODO. add a cmd for VDENC_DS_REF_SURFACE_STATE */

   anv_batch_emit(&cmd->batch, GENX(VDENC_PIPE_BUF_ADDR_STATE), vdenc_buf) {
      /* TODO. add DSFWDREF and FWDREF */
      vdenc_buf.DSFWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.DSFWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.OriginalUncompressedPicture.Address =
         anv_image_address(src_img, &src_img->planes[0].primary_surface.memory_range);
      vdenc_buf.OriginalUncompressedPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.OriginalUncompressedPicture.Address.bo, 0),
      };

      vdenc_buf.StreamInDataPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.RowStoreScratchBuffer.Address = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H264_MPR_ROW_SCRATCH].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H264_MPR_ROW_SCRATCH].offset
      };

      vdenc_buf.RowStoreScratchBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.RowStoreScratchBuffer.Address.bo, 0),
      };

      const struct anv_image_view *ref_iv[2] = { 0, };
      for (unsigned i = 0; i < enc_info->referenceSlotCount && i < 2; i++)
         ref_iv[i] = anv_image_view_from_handle(enc_info->pReferenceSlots[i].pPictureResource->imageViewBinding);

      if (ref_iv[0]) {
         vdenc_buf.ColocatedMVReadBuffer.Address =
               anv_image_address(ref_iv[0]->image, &ref_iv[0]->image->vid_dmv_top_surface);
         vdenc_buf.FWDREF0.Address =
               anv_image_address(ref_iv[0]->image, &ref_iv[0]->image->planes[0].primary_surface.memory_range);
      }

      vdenc_buf.ColocatedMVReadBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.ColocatedMVReadBuffer.Address.bo, 0),
      };

      vdenc_buf.FWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF0.Address.bo, 0),
      };

      if (ref_iv[1])
         vdenc_buf.FWDREF1.Address =
               anv_image_address(ref_iv[1]->image, &ref_iv[1]->image->planes[0].primary_surface.memory_range);

      vdenc_buf.FWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF1.Address.bo, 0),
      };

      vdenc_buf.FWDREF2.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.BWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.VDEncStatisticsStreamOut.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

#if GFX_VER >= 11
      vdenc_buf.DSFWDREF04X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.DSFWDREF14X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncCURecordStreamOutBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncLCUPAK_OBJ_CMDBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.ScaledReferenceSurface8X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.ScaledReferenceSurface4X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VP9SegmentationMapStreamInBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VP9SegmentationMapStreamOutBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif
#if GFX_VER >= 12
      vdenc_buf.VDEncTileRowStoreBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncCumulativeCUCountStreamOutSurface.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncPaletteModeStreamOutSurface.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif
   }

   StdVideoH264PictureType pic_type;

   pic_type = frame_info->pStdPictureInfo->primary_pic_type;

   anv_batch_emit(&cmd->batch, GENX(VDENC_CONST_QPT_STATE), qpt) {
      if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_IDR || pic_type == STD_VIDEO_H264_PICTURE_TYPE_I) {
         for (uint32_t i = 0; i < 42; i++) {
            qpt.QPLambdaArrayIndex[i] = vdenc_const_qp_lambda[i];
         }
      } else {
         for (uint32_t i = 0; i < 42; i++) {
            qpt.QPLambdaArrayIndex[i] = vdenc_const_qp_lambda_p[i];
         }

         for (uint32_t i = 0; i < 27; i++) {
            qpt.SkipThresholdArrayIndex[i] = vdenc_const_skip_threshold_p[i];
            qpt.SICForwardTransformCoeffThresholdMatrix0ArrayIndex[i] = vdenc_const_sic_forward_transform_coeff_threshold_0_p[i];
            qpt.SICForwardTransformCoeffThresholdMatrix135ArrayIndex[i] = vdenc_const_sic_forward_transform_coeff_threshold_1_p[i];
            qpt.SICForwardTransformCoeffThresholdMatrix2ArrayIndex[i] = vdenc_const_sic_forward_transform_coeff_threshold_2_p[i];
            qpt.SICForwardTransformCoeffThresholdMatrix46ArrayIndex[i] = vdenc_const_sic_forward_transform_coeff_threshold_3_p[i];
         }

         if (!pps->flags.transform_8x8_mode_flag) {
            for (uint32_t i = 0; i < 27; i++) {
               qpt.SkipThresholdArrayIndex[i] /= 2;
            }
         }
      }
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_AVC_IMG_STATE), avc_img) {
      avc_img.FrameWidth = sps->pic_width_in_mbs_minus1;
      avc_img.FrameHeight = sps->pic_height_in_map_units_minus1;
      avc_img.FrameSize = (avc_img.FrameWidth + 1) * (avc_img.FrameHeight + 1);
      avc_img.ImageStructure = FramePicture;

      avc_img.WeightedBiPredictionIDC = pps->weighted_bipred_idc;
      avc_img.WeightedPredictionEnable = pps->flags.weighted_pred_flag;
      avc_img.RhoDomainRateControlEnable = false;
      avc_img.FirstChromaQPOffset = pps->chroma_qp_index_offset;
      avc_img.SecondChromaQPOffset = pps->second_chroma_qp_index_offset;

      avc_img.FieldPicture = false;
      avc_img.MBAFFMode = sps->flags.mb_adaptive_frame_field_flag;
      avc_img.FrameMBOnly = sps->flags.frame_mbs_only_flag;
      avc_img._8x8IDCTTransformMode = pps->flags.transform_8x8_mode_flag;
      avc_img.Direct8x8Inference = sps->flags.direct_8x8_inference_flag;
      avc_img.ConstrainedIntraPrediction = pps->flags.constrained_intra_pred_flag;
      avc_img.NonReferencePicture = false;
      avc_img.EntropyCodingSyncEnable = pps->flags.entropy_coding_mode_flag;
      avc_img.MBMVFormat = FOLLOW;
      avc_img.ChromaFormatIDC = sps->chroma_format_idc;
      avc_img.MVUnpackedEnable = true;

      avc_img.IntraMBMaxBitControl = true;
      avc_img.InterMBMaxBitControl = true;
      avc_img.FrameBitrateMaxReport = true;
      avc_img.FrameBitrateMinReport = true;
      avc_img.ForceIPCMControl = true;
      avc_img.TrellisQuantizationChromaDisable = true;

      avc_img.IntraMBConformanceMaxSize = 2700;
      avc_img.InterMBConformanceMaxSize = 4095;

      avc_img.FrameBitrateMin = 0;
      avc_img.FrameBitrateMinUnitMode = 1;
      avc_img.FrameBitrateMinUnit = 1;
      avc_img.FrameBitrateMax = (1 << 14) - 1;
      avc_img.FrameBitrateMaxUnitMode = 1;
      avc_img.FrameBitrateMaxUnit = 1;

      avc_img.NumberofReferenceFrames = enc_info->referenceSlotCount;
      if (pic_type != STD_VIDEO_H264_PICTURE_TYPE_IDR && pic_type != STD_VIDEO_H264_PICTURE_TYPE_I) {
         avc_img.NumberofActiveReferencePicturesfromL0 = pps->num_ref_idx_l0_default_active_minus1 + 1;
         avc_img.NumberofActiveReferencePicturesfromL1 = pps->num_ref_idx_l1_default_active_minus1 + 1;
      }
      avc_img.PicOrderPresent = pps->flags.bottom_field_pic_order_in_frame_present_flag;
      avc_img.DeltaPicOrderAlwaysZero = sps->flags.delta_pic_order_always_zero_flag;
      avc_img.PicOrderCountType = sps->pic_order_cnt_type;
      avc_img.DeblockingFilterControlPresent = pps->flags.deblocking_filter_control_present_flag;
      avc_img.RedundantPicCountPresent = pps->flags.redundant_pic_cnt_present_flag;
      avc_img.Log2MaxFrameNumber = sps->log2_max_frame_num_minus4;
      avc_img.Log2MaxPicOrderCountLSB = sps->log2_max_pic_order_cnt_lsb_minus4;
   }

   uint8_t     mode_cost[12];
   uint8_t     mv_cost[8];
   uint8_t     hme_mv_cost[8];

   anv_batch_emit(&cmd->batch, GENX(VDENC_IMG_STATE), vdenc_img) {
      uint32_t slice_qp = 0;
      for (uint32_t slice_id = 0; slice_id < frame_info->naluSliceEntryCount; slice_id++) {
         const VkVideoEncodeH264NaluSliceInfoKHR *nalu = &frame_info->pNaluSliceEntries[slice_id];
         slice_qp = rc_disable ? nalu->constantQp : pps->pic_init_qp_minus26 + 26;
      }

      update_costs(mode_cost, mv_cost, hme_mv_cost, slice_qp, pic_type);

      if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_IDR || pic_type == STD_VIDEO_H264_PICTURE_TYPE_I) {
         vdenc_img.IntraSADMeasureAdjustment = 2;
         vdenc_img.SubMBSubPartitionMask = 0x70;
         vdenc_img.CREPrefetchEnable = true;
         vdenc_img.Mode0Cost = 10;
         vdenc_img.Mode1Cost = 0;
         vdenc_img.Mode2Cost = 3;
         vdenc_img.Mode3Cost = 30;

      } else {
         vdenc_img.BidirectionalWeight = 0x20;
         vdenc_img.SubPelMode = 3;
         vdenc_img.BmeDisableForFbrMessage = true;
         vdenc_img.InterSADMeasureAdjustment = 2;
         vdenc_img.IntraSADMeasureAdjustment = 2;
         vdenc_img.SubMBSubPartitionMask = 0x70;
         vdenc_img.CREPrefetchEnable = true;

         vdenc_img.NonSkipZeroMVCostAdded = 1;
         vdenc_img.NonSkipMBModeCostAdded = 1;
         vdenc_img.RefIDCostModeSelect = 1;

         vdenc_img.Mode0Cost = 7;
         vdenc_img.Mode1Cost = 26;
         vdenc_img.Mode2Cost = 30;
         vdenc_img.Mode3Cost = 57;
         vdenc_img.Mode4Cost = 8;
         vdenc_img.Mode5Cost = 2;
         vdenc_img.Mode6Cost = 4;
         vdenc_img.Mode7Cost = 6;
         vdenc_img.Mode8Cost = 5;
         vdenc_img.Mode9Cost = 0;
         vdenc_img.RefIDCost = 4;
         vdenc_img.ChromaIntraModeCost = 0;

         vdenc_img.MVCost.MV0Cost = 0;
         vdenc_img.MVCost.MV1Cost = 6;
         vdenc_img.MVCost.MV2Cost = 6;
         vdenc_img.MVCost.MV3Cost = 9;
         vdenc_img.MVCost.MV4Cost = 10;
         vdenc_img.MVCost.MV5Cost = 13;
         vdenc_img.MVCost.MV6Cost = 14;
         vdenc_img.MVCost.MV7Cost = 24;

         vdenc_img.SadHaarThreshold0 = 800;
         vdenc_img.SadHaarThreshold1 = 1600;
         vdenc_img.SadHaarThreshold2 = 2400;
      }

      vdenc_img.PenaltyforIntra16x16NonDCPrediction = 36;
      vdenc_img.PenaltyforIntra8x8NonDCPrediction = 12;
      vdenc_img.PenaltyforIntra4x4NonDCPrediction = 4;
      vdenc_img.MaxQP = 0x33;
      vdenc_img.MinQP = 0x0a;
      vdenc_img.MaxDeltaQP = 0x0f;
      vdenc_img.MaxHorizontalMVRange = 0x2000;
      vdenc_img.MaxVerticalMVRange = 0x200;
      vdenc_img.SmallMbSizeInWord = 0xff;
      vdenc_img.LargeMbSizeInWord = 0xff;

      vdenc_img.Transform8x8 = pps->flags.transform_8x8_mode_flag;
      vdenc_img.VDEncExtendedPAK_OBJ_CMDEnable = true;
      vdenc_img.PictureWidth = sps->pic_width_in_mbs_minus1 + 1;
      vdenc_img.ForwardTransformSkipCheckEnable = true;
      vdenc_img.BlockBasedSkipEnable = true;
      vdenc_img.PictureHeight = sps->pic_height_in_map_units_minus1;
      vdenc_img.PictureType = anv_vdenc_h264_picture_type(pic_type);
      vdenc_img.ConstrainedIntraPrediction = pps->flags.constrained_intra_pred_flag;

      if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_P) {
         vdenc_img.HMERef1Disable =
            (ref_list_info->num_ref_idx_l1_active_minus1 + 1) == 1 ? true : false;
      }

      vdenc_img.SliceMBHeight = sps->pic_height_in_map_units_minus1;

      if (vdenc_img.Transform8x8) {
         vdenc_img.LumaIntraPartitionMask = 0;
      } else {
         vdenc_img.LumaIntraPartitionMask = (1 << 1);
      }

      vdenc_img.QpPrimeY = slice_qp;
      vdenc_img.MaxVerticalMVRange = anv_get_max_vmv_range(sps->level_idc);

      /* TODO. Update Mode/MV cost conditinally. */
      if (1) {
         vdenc_img.Mode0Cost = mode_cost[0];
         vdenc_img.Mode1Cost = mode_cost[1];
         vdenc_img.Mode2Cost = mode_cost[2];
         vdenc_img.Mode3Cost = mode_cost[3];
         vdenc_img.Mode4Cost = mode_cost[4];
         vdenc_img.Mode5Cost = mode_cost[5];
         vdenc_img.Mode6Cost = mode_cost[6];
         vdenc_img.Mode7Cost = mode_cost[7];
         vdenc_img.Mode8Cost = mode_cost[8];
         vdenc_img.Mode9Cost = mode_cost[9];
         vdenc_img.RefIDCost = mode_cost[10];
         vdenc_img.ChromaIntraModeCost = mode_cost[11];
      }
   }

   if (pps->flags.pic_scaling_matrix_present_flag) {
      /* TODO. */
      assert(0);
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_4x4_Intra_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               qm.ForwardQuantizerMatrix[m * 16 + q] = pps->pScalingLists->ScalingList4x4[m][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_4x4_Inter_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               qm.ForwardQuantizerMatrix[m * 16 + q] = pps->pScalingLists->ScalingList4x4[m + 3][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_8x8_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[q] = pps->pScalingLists->ScalingList8x8[0][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_8x8_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[q] = pps->pScalingLists->ScalingList8x8[3][q];
      }
   } else if (sps->flags.seq_scaling_matrix_present_flag) {
      /* TODO. */
      assert(0);
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_4x4_Intra_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               qm.ForwardQuantizerMatrix[m * 16 + q] = sps->pScalingLists->ScalingList4x4[m][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_4x4_Inter_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               qm.ForwardQuantizerMatrix[m * 16 + q] = sps->pScalingLists->ScalingList4x4[m + 3][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_8x8_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[q] = sps->pScalingLists->ScalingList8x8[0][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.DWordLength = 16;
         qm.AVC = AVC_8x8_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[q] = sps->pScalingLists->ScalingList8x8[3][q];
      }
   } else {
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.AVC = AVC_4x4_Intra_MATRIX;
         for (unsigned q = 0; q < 3 * 16; q++)
            qm.ForwardQuantizerMatrix[q] = 0x10;
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.AVC = AVC_4x4_Inter_MATRIX;
         for (unsigned q = 0; q < 3 * 16; q++)
            qm.ForwardQuantizerMatrix[q] = 0x10;
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.AVC = AVC_8x8_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[q] = 0x10;
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_QM_STATE), qm) {
         qm.AVC = AVC_8x8_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            qm.ForwardQuantizerMatrix[q] = 0x10;
      }
   }

   if (pps->flags.pic_scaling_matrix_present_flag) {
      /* TODO. */
      assert(0);
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_4x4_Intra_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               fqm.QuantizerMatrix8x8[m * 16 + q] = pps->pScalingLists->ScalingList4x4[m][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_4x4_Inter_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               fqm.QuantizerMatrix8x8[m * 16 + q] = pps->pScalingLists->ScalingList4x4[m + 3][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_8x8_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            fqm.QuantizerMatrix8x8[q] = pps->pScalingLists->ScalingList8x8[0][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_8x8_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            fqm.QuantizerMatrix8x8[q] = pps->pScalingLists->ScalingList8x8[3][q];
      }
   } else if (sps->flags.seq_scaling_matrix_present_flag) {
      /* TODO. */
      assert(0);
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_4x4_Intra_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               fqm.QuantizerMatrix8x8[m * 16 + q] = sps->pScalingLists->ScalingList4x4[m][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_4x4_Inter_MATRIX;
         for (unsigned m = 0; m < 3; m++)
            for (unsigned q = 0; q < 16; q++)
               fqm.QuantizerMatrix8x8[m * 16 + q] = sps->pScalingLists->ScalingList4x4[m + 3][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_8x8_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            fqm.QuantizerMatrix8x8[q] = sps->pScalingLists->ScalingList8x8[0][q];
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_8x8_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            fqm.QuantizerMatrix8x8[q] = sps->pScalingLists->ScalingList8x8[3][q];
      }
   } else {
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_4x4_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            if (q % 2 == 1)
              fqm.QuantizerMatrix8x8[q] = 0x10;
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_4x4_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            if (q % 2 == 1)
              fqm.QuantizerMatrix8x8[q] = 0x10;
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_8x8_Intra_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            if (q % 2 == 1)
               fqm.QuantizerMatrix8x8[q] = 0x10;
      }
      anv_batch_emit(&cmd->batch, GENX(MFX_FQM_STATE), fqm) {
         fqm.AVC = AVC_8x8_Inter_MATRIX;
         for (unsigned q = 0; q < 64; q++)
            if (q % 2 == 1)
               fqm.QuantizerMatrix8x8[q] = 0x10;
      }
   }

   for (uint32_t slice_id = 0; slice_id < frame_info->naluSliceEntryCount; slice_id++) {
      const VkVideoEncodeH264NaluSliceInfoKHR *nalu = &frame_info->pNaluSliceEntries[slice_id];
      const StdVideoEncodeH264SliceHeader *slice_header = nalu->pStdSliceHeader;
      const StdVideoEncodeH264SliceHeader *next_slice_header = NULL;

      bool is_last = (slice_id == frame_info->naluSliceEntryCount - 1);
      uint32_t slice_type = slice_header->slice_type % 5;
      uint32_t slice_qp = rc_disable ? nalu->constantQp : pps->pic_init_qp_minus26 + 26;

      if (!is_last)
         next_slice_header = slice_header + 1;

      if (slice_type != STD_VIDEO_H264_SLICE_TYPE_I) {
         anv_batch_emit(&cmd->batch, GENX(MFX_AVC_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 0;

            for (uint32_t i = 0; i < ref_list_info->num_ref_idx_l0_active_minus1 + 1; i++) {
               const VkVideoReferenceSlotInfoKHR ref_slot = enc_info->pReferenceSlots[i];
               ref.ReferenceListEntry[i] = dpb_idx[ref_slot.slotIndex];
            }
         }
      }

      if (slice_type == STD_VIDEO_H264_SLICE_TYPE_B) {
         anv_batch_emit(&cmd->batch, GENX(MFX_AVC_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 1;

            for (uint32_t i = 0; i < ref_list_info->num_ref_idx_l1_active_minus1 + 1; i++) {
               const VkVideoReferenceSlotInfoKHR ref_slot = enc_info->pReferenceSlots[i];
               ref.ReferenceListEntry[i] = dpb_idx[ref_slot.slotIndex];
            }
         }
      }

      if (pps->flags.weighted_pred_flag && slice_type == STD_VIDEO_H265_SLICE_TYPE_P) {
         /* TODO. */
         assert(0);
         anv_batch_emit(&cmd->batch, GENX(MFX_AVC_WEIGHTOFFSET_STATE), w) {
         }
      }

      if (pps->flags.weighted_pred_flag && slice_type == STD_VIDEO_H265_SLICE_TYPE_B) {
         /* TODO. */
         assert(0);
         anv_batch_emit(&cmd->batch, GENX(MFX_AVC_WEIGHTOFFSET_STATE), w) {
         }
      }

      const StdVideoEncodeH264WeightTable*      weight_table =  slice_header->pWeightTable;

      unsigned w_in_mb = align(src_img->vk.extent.width, ANV_MB_WIDTH) / ANV_MB_WIDTH;
      unsigned h_in_mb = align(src_img->vk.extent.height, ANV_MB_HEIGHT) / ANV_MB_HEIGHT;

      uint8_t slice_header_data[256] = { 0, };
      size_t slice_header_data_len_in_bytes = 0;
      vk_video_encode_h264_slice_header(frame_info->pStdPictureInfo,
                                        sps,
                                        pps,
                                        slice_header,
                                        slice_qp - (pps->pic_init_qp_minus26 + 26),
                                        &slice_header_data_len_in_bytes,
                                        &slice_header_data);
      uint32_t slice_header_data_len_in_bits = slice_header_data_len_in_bytes * 8;

      anv_batch_emit(&cmd->batch, GENX(MFX_AVC_SLICE_STATE), avc_slice) {
         avc_slice.SliceType = slice_type;

         if (slice_type != STD_VIDEO_H264_SLICE_TYPE_I && weight_table) {
            avc_slice.Log2WeightDenominatorLuma = weight_table->luma_log2_weight_denom;
            avc_slice.Log2WeightDenominatorChroma = weight_table->chroma_log2_weight_denom;
         }

         avc_slice.NumberofReferencePicturesinInterpredictionList0 =
            slice_type == STD_VIDEO_H264_SLICE_TYPE_I ? 0 : ref_list_info->num_ref_idx_l0_active_minus1 + 1;
         avc_slice.NumberofReferencePicturesinInterpredictionList1 =
            (slice_type == STD_VIDEO_H264_SLICE_TYPE_I ||
             slice_type == STD_VIDEO_H264_SLICE_TYPE_P) ? 0 : ref_list_info->num_ref_idx_l1_active_minus1 + 1;

         avc_slice.SliceAlphaC0OffsetDiv2 = slice_header->slice_alpha_c0_offset_div2 & 0x7;
         avc_slice.SliceBetaOffsetDiv2 = slice_header->slice_beta_offset_div2 & 0x7;
         avc_slice.SliceQuantizationParameter = slice_qp;
         avc_slice.CABACInitIDC = slice_header->cabac_init_idc;
         avc_slice.DisableDeblockingFilterIndicator =
            pps->flags.deblocking_filter_control_present_flag ? slice_header->disable_deblocking_filter_idc : 0;
         avc_slice.DirectPredictionType = slice_header->flags.direct_spatial_mv_pred_flag;

         avc_slice.SliceStartMBNumber = slice_header->first_mb_in_slice;
         avc_slice.SliceHorizontalPosition =
            slice_header->first_mb_in_slice % (w_in_mb);
         avc_slice.SliceVerticalPosition =
            slice_header->first_mb_in_slice / (w_in_mb);

         if (is_last) {
            avc_slice.NextSliceHorizontalPosition = 0;
            avc_slice.NextSliceVerticalPosition = h_in_mb;
         } else {
            avc_slice.NextSliceHorizontalPosition = next_slice_header->first_mb_in_slice % w_in_mb;
            avc_slice.NextSliceVerticalPosition = next_slice_header->first_mb_in_slice / w_in_mb;
         }

         avc_slice.SliceID = slice_id;
         avc_slice.CABACZeroWordInsertionEnable = 1;
         avc_slice.EmulationByteSliceInsertEnable = 1;
         avc_slice.SliceDataInsertionPresent = 1;
         avc_slice.HeaderInsertionPresent = 1;
         avc_slice.LastSliceGroup = is_last;
         avc_slice.RateControlCounterEnable = false;

         /* TODO. Available only when RateControlCounterEnable is true. */
         avc_slice.RateControlPanicType = CBPPanic;
         avc_slice.RateControlPanicEnable = false;
         avc_slice.RateControlTriggleMode = LooseRateControl;
         avc_slice.ResetRateControlCounter = true;
         avc_slice.IndirectPAKBSEDataStartAddress = enc_info->dstBufferOffset;

         avc_slice.RoundIntra = 5;
         avc_slice.RoundIntraEnable = true;
         /* TODO. Needs to get a different value of rounding inter under various conditions. */
         avc_slice.RoundInter = 2;
         avc_slice.RoundInterEnable = false;

         if (slice_type == STD_VIDEO_H264_SLICE_TYPE_P) {
            avc_slice.WeightedPredictionIndicator = pps->flags.weighted_pred_flag;
            avc_slice.NumberofReferencePicturesinInterpredictionList0 = ref_list_info->num_ref_idx_l0_active_minus1 + 1;
         } else if (slice_type == STD_VIDEO_H264_SLICE_TYPE_B) {
            avc_slice.WeightedPredictionIndicator = pps->weighted_bipred_idc;
            avc_slice.NumberofReferencePicturesinInterpredictionList0 = ref_list_info->num_ref_idx_l0_active_minus1 + 1;
            avc_slice.NumberofReferencePicturesinInterpredictionList1 = ref_list_info->num_ref_idx_l1_active_minus1 + 1;
         }
      }

      uint32_t length_in_dw, data_bits_in_last_dw;
      uint32_t *dw;

      /* Insert zero slice data */
      unsigned int insert_zero[] = { 0, };
      length_in_dw = 1;
      data_bits_in_last_dw = 8;

      dw = anv_batch_emitn(&cmd->batch, length_in_dw + 2, GENX(MFX_PAK_INSERT_OBJECT),
            .DataBitsInLastDW = data_bits_in_last_dw > 0 ? data_bits_in_last_dw : 32,
            .HeaderLengthExcludedFromSize =  ACCUMULATE);

      memcpy(dw + 2, insert_zero, length_in_dw * 4);

      slice_header_data_len_in_bits -= 8;

      length_in_dw = ALIGN(slice_header_data_len_in_bits, 32) >> 5;
      data_bits_in_last_dw = slice_header_data_len_in_bits & 0x1f;

      dw = anv_batch_emitn(&cmd->batch, length_in_dw + 2, GENX(MFX_PAK_INSERT_OBJECT),
               .LastHeader = true,
               .DataBitsInLastDW = data_bits_in_last_dw > 0 ? data_bits_in_last_dw : 32,
               .SliceHeaderIndicator = true,
               .HeaderLengthExcludedFromSize =  ACCUMULATE);

      memcpy(dw + 2, slice_header_data + 1, length_in_dw * 4);

      anv_batch_emit(&cmd->batch, GENX(VDENC_WEIGHTSOFFSETS_STATE), vdenc_offsets) {
         vdenc_offsets.WeightsForwardReference0 = 1;
         vdenc_offsets.WeightsForwardReference1 = 1;
         vdenc_offsets.WeightsForwardReference2 = 1;

      }

      anv_batch_emit(&cmd->batch, GENX(VDENC_WALKER_STATE), vdenc_walker) {
         vdenc_walker.NextSliceMBStartYPosition = h_in_mb;
         vdenc_walker.Log2WeightDenominatorLuma = weight_table ? weight_table->luma_log2_weight_denom : 0;
#if GFX_VER >= 12
         vdenc_walker.TileWidth = src_img->vk.extent.width - 1;
#endif
      }

      anv_batch_emit(&cmd->batch, GENX(VD_PIPELINE_FLUSH), flush) {
         flush.MFXPipelineDone = true;
         flush.VDENCPipelineDone = true;
         flush.VDCommandMessageParserDone = true;
         flush.VDENCPipelineCommandFlush = true;
      }
   }

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.DWordLength = 2;
      flush.VideoPipelineCacheInvalidate = 1;
   };

}

static uint8_t
anv_h265_get_ref_poc(const VkVideoEncodeInfoKHR *enc_info,
                     const StdVideoEncodeH265ReferenceListsInfo* ref_lists,
                     const bool l0,
                     const uint8_t slot_num,
                     bool *long_term)
{
   uint8_t ref_poc = 0xff;
   unsigned ref_cnt = l0 ? ref_lists->num_ref_idx_l0_active_minus1 + 1 :
                           ref_lists->num_ref_idx_l1_active_minus1 + 1;

   for (unsigned i = 0; i < ref_cnt; i++) {
      const VkVideoReferenceSlotInfoKHR ref_slot_info = enc_info->pReferenceSlots[i];
      const VkVideoEncodeH265DpbSlotInfoKHR *dpb =
            vk_find_struct_const(ref_slot_info.pNext, VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR);

      if (!dpb)
         return ref_poc;

      if (ref_slot_info.slotIndex == slot_num) {
         ref_poc = dpb->pStdReferenceInfo->PicOrderCntVal;
         *long_term |= dpb->pStdReferenceInfo->flags.used_for_long_term_reference;
         break;
      }
   }

   return ref_poc;
}

static void
scaling_list(struct anv_cmd_buffer *cmd_buffer,
             const StdVideoH265ScalingLists *scaling_list)
{
   /* 4x4, 8x8, 16x16, 32x32 */
   for (uint8_t size = 0; size < 4; size++) {
      /* Intra, Inter */
      for (uint8_t pred = 0; pred < 2; pred++) {
         /* Y, Cb, Cr */
         for (uint8_t color = 0; color < 3; color++) {
            if (size == 3 && color > 0)
               continue;

            anv_batch_emit(&cmd_buffer->batch, GENX(HCP_QM_STATE), qm) {
               qm.SizeID = size;
               qm.PredictionType = pred;
               qm.ColorComponent = color;

               qm.DCCoefficient = size > 1 ?
                  (size == 2 ? scaling_list->ScalingListDCCoef16x16[3 * pred + color] :
                               scaling_list->ScalingListDCCoef32x32[pred]) : 0;

               if (size == 0) {
                  for (uint8_t i = 0; i < 4; i++)
                     for (uint8_t j = 0; j < 4; j++)
                        qm.QuantizerMatrix8x8[4 * i + j] =
                           scaling_list->ScalingList4x4[3 * pred + color][4 * i + j];
               } else if (size == 1) {
                  for (uint8_t i = 0; i < 8; i++)
                     for (uint8_t j = 0; j < 8; j++)
                        qm.QuantizerMatrix8x8[8 * i + j] =
                           scaling_list->ScalingList8x8[3 * pred + color][8 * i + j];
               } else if (size == 2) {
                  for (uint8_t i = 0; i < 8; i++)
                     for (uint8_t j = 0; j < 8; j++)
                        qm.QuantizerMatrix8x8[8 * i + j] =
                           scaling_list->ScalingList16x16[3 * pred + color][8 * i + j];
               } else if (size == 3) {
                  for (uint8_t i = 0; i < 8; i++)
                     for (uint8_t j = 0; j < 8; j++)
                        qm.QuantizerMatrix8x8[8 * i + j] =
                           scaling_list->ScalingList32x32[pred][8 * i + j];
               }
            }
         }
      }
   }
}

static uint16_t
lcu_max_bits_size_allowed(const StdVideoH265SequenceParameterSet *sps)
{
   uint16_t log2_max_coding_block_size =
         sps->log2_diff_max_min_luma_coding_block_size +
         sps->log2_min_luma_coding_block_size_minus3 + 3;
   uint32_t raw_ctu_bits = (1 << (2 * log2_max_coding_block_size));

   switch (sps->chroma_format_idc)
   {
   case 1:
       raw_ctu_bits = raw_ctu_bits * 3 / 2;
       break;
   case 2:
       raw_ctu_bits = raw_ctu_bits * 2;
       break;
   case 3:
       raw_ctu_bits = raw_ctu_bits * 3;
       break;
   default:
       break;
   };

   raw_ctu_bits = raw_ctu_bits * (sps->bit_depth_luma_minus8 + 8);
   raw_ctu_bits = (5 * raw_ctu_bits / 3);

   return raw_ctu_bits & 0xffff;
}

static void
anv_h265_encode_video(struct anv_cmd_buffer *cmd, const VkVideoEncodeInfoKHR *enc_info)
{
   /* Supported on Gen12(+) for using VDEnc Mode */
#if GFX_VER >= 12
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, enc_info->dstBuffer);
   struct anv_video_session *vid = cmd->video.vid;
   struct anv_video_session_params *params = cmd->video.params;

   const struct VkVideoEncodeH265PictureInfoKHR *frame_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);

   const StdVideoH265VideoParameterSet *vps = vk_video_find_h265_enc_std_vps(&params->vk, frame_info->pStdPictureInfo->sps_video_parameter_set_id);
   const StdVideoH265SequenceParameterSet *sps = vk_video_find_h265_enc_std_sps(&params->vk, frame_info->pStdPictureInfo->pps_seq_parameter_set_id);
   const StdVideoH265PictureParameterSet *pps = vk_video_find_h265_enc_std_pps(&params->vk, frame_info->pStdPictureInfo->pps_pic_parameter_set_id);
   const StdVideoEncodeH265ReferenceListsInfo *ref_list_info = frame_info->pStdPictureInfo->pRefLists;

   const struct anv_image_view *iv = anv_image_view_from_handle(enc_info->srcPictureResource.imageViewBinding);
   const struct anv_image *src_img = iv->image;

   const struct anv_image_view *base_ref_iv;

   bool rc_disable = cmd->video.params->rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;

   if (enc_info->pSetupReferenceSlot) {
      base_ref_iv = anv_image_view_from_handle(enc_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
   } else {
      base_ref_iv = iv;
   }

   const struct anv_image *base_ref_img = base_ref_iv->image;
   uint8_t dpb_idx[ANV_VIDEO_H265_MAX_NUM_REF_FRAME] = { 0,};

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.VideoPipelineCacheInvalidate = 1;
   };


   anv_batch_emit(&cmd->batch, GENX(MI_FORCE_WAKEUP), wake) {
      wake.MFXPowerWellControl = 1;
      wake.HEVCPowerWellControl = 1;
      wake.MaskBits = 768;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_CONTROL_STATE), v) {
      v.VdencInitialization = true;
   }

   anv_batch_emit(&cmd->batch, GENX(VD_CONTROL_STATE), v) {
      v.PipelineInitialization = true;
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }

   anv_batch_emit(&cmd->batch, GENX(HCP_PIPE_MODE_SELECT), sel) {
      sel.CodecSelect = Encode;
      sel.CodecStandardSelect = HEVC;
      sel.VDEncMode = VM_VDEncMode;
   }

   anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
      mfx.MFXSyncControlFlag = 1;
   }


   for (uint32_t i = 0; i < 3; i++) {
      anv_batch_emit(&cmd->batch, GENX(HCP_SURFACE_STATE), ss) {
         struct anv_image *img_ = NULL;

         switch(i) {
            case 0:
               img_ = (struct anv_image *) src_img;
               ss.SurfaceID = HCP_SourceInputPicture;
               break;
            case 1:
               //img_ = (struct anv_image *) src_img;
               img_ = (struct anv_image *) base_ref_img;
               ss.SurfaceID = HCP_CurrentDecodedPicture;
               break;
            case 2:
               img_ = (struct anv_image *) base_ref_img;
               ss.SurfaceID = HCP_ReferencePicture;
               break;
            default:
               assert(0);
         }

         ss.SurfacePitch = img_->planes[0].primary_surface.isl.row_pitch_B - 1;
         ss.SurfaceFormat = PLANAR_420_8;

         ss.YOffsetforUCb = img_->planes[1].primary_surface.memory_range.offset /
                            img_->planes[0].primary_surface.isl.row_pitch_B;
         ss.YOffsetforVCr = ss.YOffsetforUCb;
      }
   }

   anv_batch_emit(&cmd->batch, GENX(HCP_PIPE_BUF_ADDR_STATE), buf) {
      buf.DecodedPictureAddress =
         anv_image_address(base_ref_img, &base_ref_img->planes[0].primary_surface.memory_range);

      buf.DecodedPictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.DecodedPictureAddress.bo, 0),
      };

      buf.DeblockingFilterLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_LINE].offset
      };

      buf.DeblockingFilterLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.DeblockingFilterLineBufferAddress.bo, 0),
      };

      buf.DeblockingFilterTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_LINE].offset
      };

      buf.DeblockingFilterTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.DeblockingFilterTileLineBufferAddress.bo, 0),
      };

      buf.DeblockingFilterTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_DEBLOCK_FILTER_ROW_STORE_TILE_COLUMN].offset
      };

      buf.DeblockingFilterTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.DeblockingFilterTileColumnBufferAddress.bo, 0),
      };

      buf.MetadataLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_LINE].offset
      };

      buf.MetadataLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.MetadataLineBufferAddress.bo, 0),
      };

      buf.MetadataTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_LINE].offset
      };

      buf.MetadataTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.MetadataTileLineBufferAddress.bo, 0),
      };

      buf.MetadataTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_METADATA_TILE_COLUMN].offset
      };

      buf.MetadataTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.MetadataTileColumnBufferAddress.bo, 0),
      };

      buf.SAOLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SAO_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SAO_LINE].offset
      };

      buf.SAOLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.SAOLineBufferAddress.bo, 0),
      };

      buf.SAOTileLineBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_LINE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_LINE].offset
      };

      buf.SAOTileLineBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.SAOTileLineBufferAddress.bo, 0),
      };

      buf.SAOTileColumnBufferAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_COLUMN].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SAO_TILE_COLUMN].offset
      };

      buf.SAOTileColumnBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.SAOTileColumnBufferAddress.bo, 0),
      };

      buf.CurrentMVTemporalBufferAddress = anv_image_address(src_img, &src_img->vid_dmv_top_surface);

      buf.CurrentMVTemporalBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.CurrentMVTemporalBufferAddress.bo, 0),
      };

      for (unsigned i = 0; i < enc_info->referenceSlotCount; i++) {
         const struct anv_image_view *ref_iv =
            anv_image_view_from_handle(enc_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
         int slot_idx = enc_info->pReferenceSlots[i].slotIndex;

         assert(slot_idx < ANV_VIDEO_H265_MAX_NUM_REF_FRAME);
         dpb_idx[slot_idx] = i;

         buf.ReferencePictureAddress[i] =
            anv_image_address(ref_iv->image, &ref_iv->image->planes[0].primary_surface.memory_range);
      }

      buf.ReferencePictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.OriginalUncompressedPictureSourceAddress =
         anv_image_address(src_img, &src_img->planes[0].primary_surface.memory_range);
      buf.OriginalUncompressedPictureSourceMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.OriginalUncompressedPictureSourceAddress.bo, 0),
      };

      buf.StreamOutDataDestinationMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.DecodedPictureStatusBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.LCUILDBStreamOutBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      for (unsigned i = 0; i < enc_info->referenceSlotCount; i++) {
         const struct anv_image_view *ref_iv =
            anv_image_view_from_handle(enc_info->pReferenceSlots[i].pPictureResource->imageViewBinding);

         buf.CollocatedMVTemporalBufferAddress[i] =
            anv_image_address(ref_iv->image, &ref_iv->image->vid_dmv_top_surface);
      }

      buf.CollocatedMVTemporalBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.CollocatedMVTemporalBufferAddress[0].bo, 0),
      };

      buf.VP9ProbabilityBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.VP9SegmentIDBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.VP9HVDLineRowStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.VP9HVDTileRowStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.SAOStreamOutDataDestinationBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      buf.FrameStatisticsStreamOutDataDestinationBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      buf.SSESourcePixelRowStoreBufferBaseAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SSE_SRC_PIX_ROW_STORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SSE_SRC_PIX_ROW_STORE].offset
      };

      buf.SSESourcePixelRowStoreBufferMemoryAddressAttributesReadWrite = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.SSESourcePixelRowStoreBufferBaseAddress.bo, 0),
      };

      buf.HCPScalabilitySliceStateBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      buf.HCPScalabilityCABACDecodedSyntaxElementsBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      buf.MVUpperRightColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      buf.IntraPredictionUpperRightColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      buf.IntraPredictionLeftReconColumnStoreBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
   }

   anv_batch_emit(&cmd->batch, GENX(HCP_IND_OBJ_BASE_ADDR_STATE), indirect) {
      indirect.HCPIndirectBitstreamObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      indirect.HCPIndirectCUObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      indirect.HCPPAKBSEObjectBaseAddress =
            anv_address_add(dst_buffer->address,  align(enc_info->dstBufferOffset, 4096));
      indirect.HCPPAKBSEObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, indirect.HCPPAKBSEObjectBaseAddress.bo, 0),
      };

      indirect.HCPVP9PAKCompressedHeaderSyntaxStreamInMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      indirect.HCPVP9PAKProbabilityCounterStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      indirect.HCPVP9PAKProbabilityDeltasStreamInMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      indirect.HCPVP9PAKTileRecordStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      indirect.HCPVP9PAKCULevelStatisticStreamOutMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
   }

   if (sps->flags.scaling_list_enabled_flag) {
      assert(0);
      /* FIXME */
      if (pps->flags.pps_scaling_list_data_present_flag) {
         scaling_list(cmd, pps->pScalingLists);
      } else if (sps->flags.sps_scaling_list_data_present_flag) {
         scaling_list(cmd, sps->pScalingLists);
      }
   } else {
      for (uint8_t size = 0; size < 4; size++) {
         for (uint8_t pred = 0; pred < 2; pred++) {
            anv_batch_emit(&cmd->batch, GENX(HCP_FQM_STATE), fqm) {
               fqm.SizeID = size;
               fqm.IntraInter = pred;
               fqm.ColorComponent = 0;
               fqm.FQMDCValue = size < 2 ? 0 : 0x1000;

               unsigned len = (size == 0) ? 32 : 128;

               for (uint8_t q = 0; q < len; q++) {
                  fqm.QuantizerMatrix8x8[q] = q % 2 == 0 ? 0 : 0x10;
               }
            }
         }
      }
   }

   if (sps->flags.scaling_list_enabled_flag) {
      assert(0);
      /* FIXME */
      if (pps->flags.pps_scaling_list_data_present_flag) {
         scaling_list(cmd, pps->pScalingLists);
      } else if (sps->flags.sps_scaling_list_data_present_flag) {
         scaling_list(cmd, sps->pScalingLists);
      }
   } else {
      for (uint8_t size = 0; size < 4; size++) {
         for (uint8_t pred = 0; pred < 2; pred++) {
            for (uint8_t color = 0; color < 3; color++) {

               if (size == 3 && color > 0)
                  continue;

               anv_batch_emit(&cmd->batch, GENX(HCP_QM_STATE), qm) {
                  qm.SizeID = size;
                  qm.PredictionType = pred;
                  qm.ColorComponent = color;
                  qm.DCCoefficient = (size > 1) ? 16 : 0;
                  unsigned len = (size == 0) ? 16 : 64;

                  for (uint8_t q = 0; q < len; q++)
                     qm.QuantizerMatrix8x8[q] = 0x10;
               }
            }
         }
      }
   }


   anv_batch_emit(&cmd->batch, GENX(VDENC_PIPE_MODE_SELECT), vdenc_pipe_mode) {
      vdenc_pipe_mode.StandardSelect = SS_HEVC;
      vdenc_pipe_mode.PAKChromaSubSamplingType = _420;
      vdenc_pipe_mode.HMERegionPrefetchEnable = !vdenc_pipe_mode.TLBPrefetchEnable;
      vdenc_pipe_mode.TopPrefetchEnableMode = 1;
      vdenc_pipe_mode.LeftPrefetchAtWrapAround = true;
      vdenc_pipe_mode.HzShift32Minus1 = 3;
      vdenc_pipe_mode.NumberofVerticalRequests = 11;
      vdenc_pipe_mode.NumberofHorizontalRequests = 2;

      vdenc_pipe_mode.SourceLumaPackedDataTLBPrefetchEnable = true;
      vdenc_pipe_mode.SourceChromaTLBPrefetchEnable = true;
      vdenc_pipe_mode.HzShift32Minus1Src = 3;
      vdenc_pipe_mode.PrefetchOffsetforSource = 4;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_SRC_SURFACE_STATE), vdenc_surface) {
      vdenc_surface.SurfaceState.Width = src_img->vk.extent.width - 1;
      vdenc_surface.SurfaceState.Height = src_img->vk.extent.height - 1;
      vdenc_surface.SurfaceState.SurfaceFormat = VDENC_PLANAR_420_8;

      vdenc_surface.SurfaceState.TileWalk = TW_YMAJOR;
      vdenc_surface.SurfaceState.TiledSurface = src_img->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
      vdenc_surface.SurfaceState.SurfacePitch = src_img->planes[0].primary_surface.isl.row_pitch_B - 1;
      vdenc_surface.SurfaceState.YOffsetforUCb = src_img->planes[1].primary_surface.memory_range.offset /
         src_img->planes[0].primary_surface.isl.row_pitch_B;
      vdenc_surface.SurfaceState.YOffsetforVCr = src_img->planes[1].primary_surface.memory_range.offset /
         src_img->planes[0].primary_surface.isl.row_pitch_B;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_REF_SURFACE_STATE), vdenc_surface) {
      vdenc_surface.SurfaceState.Width = base_ref_img->vk.extent.width - 1;
      vdenc_surface.SurfaceState.Height = base_ref_img->vk.extent.height - 1;
      vdenc_surface.SurfaceState.SurfaceFormat = VDENC_PLANAR_420_8;
      vdenc_surface.SurfaceState.SurfacePitch = base_ref_img->planes[0].primary_surface.isl.row_pitch_B - 1;

      vdenc_surface.SurfaceState.TileWalk = TW_YMAJOR;
      vdenc_surface.SurfaceState.TiledSurface = base_ref_img->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
      vdenc_surface.SurfaceState.YOffsetforUCb = base_ref_img->planes[1].primary_surface.memory_range.offset /
         base_ref_img->planes[0].primary_surface.isl.row_pitch_B;
      vdenc_surface.SurfaceState.YOffsetforVCr = base_ref_img->planes[1].primary_surface.memory_range.offset /
         base_ref_img->planes[0].primary_surface.isl.row_pitch_B;
   }

   /* TODO. add a cmd for VDENC_DS_REF_SURFACE_STATE */

   anv_batch_emit(&cmd->batch, GENX(VDENC_PIPE_BUF_ADDR_STATE), vdenc_buf) {
      /* TODO. add DSFWDREF and FWDREF */
      vdenc_buf.DSFWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.DSFWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.OriginalUncompressedPicture.Address =
         anv_image_address(src_img, &src_img->planes[0].primary_surface.memory_range);
      vdenc_buf.OriginalUncompressedPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.OriginalUncompressedPicture.Address.bo, 0),
      };

      vdenc_buf.StreamInDataPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.RowStoreScratchBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      const struct anv_image_view *ref_iv[3] = { 0, };

      for (unsigned i = 0; i < enc_info->referenceSlotCount && i < 3; i++)
         ref_iv[i] = anv_image_view_from_handle(enc_info->pReferenceSlots[i].pPictureResource->imageViewBinding);

      if (ref_iv[0]) {
         vdenc_buf.ColocatedMVReadBuffer.Address =
               anv_image_address(ref_iv[0]->image, &ref_iv[0]->image->vid_dmv_top_surface);
         vdenc_buf.FWDREF0.Address =
               anv_image_address(ref_iv[0]->image, &ref_iv[0]->image->planes[0].primary_surface.memory_range);
      }

      vdenc_buf.ColocatedMVReadBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.ColocatedMVReadBuffer.Address.bo, 0),
      };

      vdenc_buf.FWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF0.Address.bo, 0),
      };

      if (ref_iv[1])
         vdenc_buf.FWDREF1.Address =
               anv_image_address(ref_iv[1]->image, &ref_iv[1]->image->planes[0].primary_surface.memory_range);

      vdenc_buf.FWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF1.Address.bo, 0),
      };

      if (ref_iv[2])
         vdenc_buf.FWDREF2.Address =
               anv_image_address(ref_iv[2]->image, &ref_iv[2]->image->planes[0].primary_surface.memory_range);

      vdenc_buf.FWDREF2.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF2.Address.bo, 0),
      };

      vdenc_buf.BWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.VDEncStatisticsStreamOut.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.DSFWDREF04X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.DSFWDREF14X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncCURecordStreamOutBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncLCUPAK_OBJ_CMDBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.ScaledReferenceSurface8X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.ScaledReferenceSurface4X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VP9SegmentationMapStreamInBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VP9SegmentationMapStreamOutBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncTileRowStoreBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncCumulativeCUCountStreamOutSurface.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.VDEncPaletteModeStreamOutSurface.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_CMD1), cmd1) {
      /* Magic numbers taken from media-driver */
      cmd1.Values[0] = 0x5030200;
      cmd1.Values[1] = 0xb090806;
      cmd1.Values[2] = 0x1c140c04;
      cmd1.Values[3] = 0x3c342c24;
      cmd1.Values[4] = 0x5c544c44;
      cmd1.Values[5] = 0x1c140c04;
      cmd1.Values[6] = 0x3c342c24;
      cmd1.Values[7] = 0x5c544c44;
      cmd1.Values[13] = 0x0;
      cmd1.Values[14] = 0x0;
      cmd1.Values[15] &= 0xffff0000;

      cmd1.Values[18] = (cmd1.Values[18] & 0xff0000ff) | 0x140400;
      cmd1.Values[19] = 0x14141414;
      cmd1.Values[20] = 0x14141414;

      cmd1.Values[21] = 0x10101010;
      cmd1.Values[22] = 0x10101010;
      cmd1.Values[23] = 0x10101010;
      cmd1.Values[24] = 0x10101010;
      cmd1.Values[25] = 0x10101010;
      cmd1.Values[26] = 0x10101010;
      cmd1.Values[27] = 0x10101010;
      cmd1.Values[28] = 0x10101010;

      if (anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) == 0) {
         cmd1.Values[9] = 0x23131f0f;
         cmd1.Values[10] = (cmd1.Values[10] & 0xffff0000) | 0x2313;
         cmd1.Values[11] = 0x3e5c445c;
         cmd1.Values[12] = (cmd1.Values[12] & 0xff00) | 0x1e040044;
         cmd1.Values[15] = (cmd1.Values[15] & 0xffff) | 0x70000;
         cmd1.Values[16] = 0xd0e1007;
         cmd1.Values[17] = (cmd1.Values[17] & 0xffffff00) | 0x32;
         /* Handle Number of ROI */
         cmd1.Values[17] = (cmd1.Values[17] & 0xffff00ff) | 0x1e00;
         cmd1.Values[29] = (cmd1.Values[29] & 0xff000000) | 0x101010;
      } else {
         cmd1.Values[9] = 0x23131f0f;
         cmd1.Values[10] = 0x331b2313;
         cmd1.Values[11] = 0x476e4d6e;
         cmd1.Values[12] = 0x3604004d;
         cmd1.Values[15] = (cmd1.Values[15] & 0xffff) | 0x4150000;
         cmd1.Values[16] = 0x23231415;
         cmd1.Values[17] = (cmd1.Values[17] & 0xffffff00) | 0x3f;
         /* Handle Number of ROI */
         cmd1.Values[17] = (cmd1.Values[17] & 0xffff00ff) | 0x4400;
         cmd1.Values[29] = (cmd1.Values[29] & 0xff000000) | 0x232323;
      }
   }

   uint32_t frame_width_in_min_cb = sps->pic_width_in_luma_samples >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
   uint32_t frame_height_in_min_cb = sps->pic_height_in_luma_samples >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
   uint32_t width_in_pix = frame_width_in_min_cb << (sps->log2_min_luma_coding_block_size_minus3 + 3);
   uint32_t height_in_pix = frame_height_in_min_cb << (sps->log2_min_luma_coding_block_size_minus3 + 3);

   anv_batch_emit(&cmd->batch, GENX(HCP_PIC_STATE), pic) {
      pic.FrameWidthInMinimumCodingBlockSize = frame_width_in_min_cb - 1;
      pic.FrameHeightInMinimumCodingBlockSize = frame_height_in_min_cb - 1;
      pic.TransformSkipEnable = pps->flags.transform_skip_enabled_flag;
      pic.TransformSkipEnable = true;

      pic.MinCUSize = sps->log2_min_luma_coding_block_size_minus3;
      pic.LCUSize = sps->log2_diff_max_min_luma_coding_block_size + sps->log2_min_luma_coding_block_size_minus3;

      pic.MinTUSize = sps->log2_min_luma_transform_block_size_minus2;
      pic.MaxTUSize = sps->log2_diff_max_min_luma_transform_block_size + sps->log2_min_luma_transform_block_size_minus2;

      pic.MinPCMSize = 0;
      pic.MaxPCMSize = 0;

      pic.ChromaSubsampling = sps->chroma_format_idc;

      const StdVideoEncodeH265SliceSegmentHeader *slice_header = NULL;
      for (uint32_t slice_id = 0; slice_id < frame_info->naluSliceSegmentEntryCount; slice_id++) {
         const VkVideoEncodeH265NaluSliceSegmentInfoKHR *nalu = &frame_info->pNaluSliceSegmentEntries[slice_id];
         if (nalu) {
            slice_header = nalu->pStdSliceSegmentHeader;
            break;
        }
      }

      pic.CollocatedPictureIsISlice = false;
      pic.CurrentPictureIsISlice = false;

      pic.SampleAdaptiveOffsetEnable = sps->flags.sample_adaptive_offset_enabled_flag ? slice_header->flags.slice_sao_chroma_flag ||
         slice_header->flags.slice_sao_luma_flag : 0;
      pic.PCMEnable = sps->flags.pcm_enabled_flag;
      pic.CUQPDeltaEnable = pps->flags.cu_qp_delta_enabled_flag;
      pic.MaxDQPDepth = pps->diff_cu_qp_delta_depth;
      pic.PCMLoopFilterDisable = sps->flags.pcm_loop_filter_disabled_flag;
      pic.ConstrainedIntraPrediction = pps->flags.constrained_intra_pred_flag;
      pic.TilingEnable = pps->flags.tiles_enabled_flag;
      pic.WeightedBiPredicationEnable = pps->flags.weighted_bipred_flag;
      pic.WeightedPredicationEnable = pps->flags.weighted_pred_flag;
      pic.FieldPic = 0;
      pic.TopField = false;
      pic.TransformSkipEnable = pps->flags.transform_skip_enabled_flag;
      pic.AMPEnable = sps->flags.amp_enabled_flag;
      pic.TransquantBypassEnable = pps->flags.transquant_bypass_enabled_flag;
      pic.StrongIntraSmoothingEnable = sps->flags.strong_intra_smoothing_enabled_flag;
      pic.CUPacketStructure = 0;

      pic.PictureCbQPOffset = pps->pps_cb_qp_offset & 0x1f;
      pic.PictureCrQPOffset = pps->pps_cr_qp_offset & 0x1f;
      pic.IntraMaxTransformHierarchyDepth = 2;
      pic.InterMaxTransformHierarchyDepth = 2;
      pic.ChromaPCMSampleBitDepth = sps->pcm_sample_bit_depth_chroma_minus1 & 0xf;
      pic.LumaPCMSampleBitDepth = sps->pcm_sample_bit_depth_luma_minus1 & 0xf;

      pic.ChromaBitDepth = sps->bit_depth_chroma_minus8;
      pic.LumaBitDepth = sps->bit_depth_luma_minus8;

      pic.LCUMaxBitSizeAllowed = lcu_max_bits_size_allowed(sps);
      pic.CbQPOffsetList0 = pps->cb_qp_offset_list[0];
      pic.CbQPOffsetList1 = pps->cb_qp_offset_list[1];
      pic.CbQPOffsetList2 = pps->cb_qp_offset_list[2];
      pic.CbQPOffsetList3 = pps->cb_qp_offset_list[3];
      pic.CbQPOffsetList4 = pps->cb_qp_offset_list[4];
      pic.CbQPOffsetList5 = pps->cb_qp_offset_list[5];

      pic.CrQPOffsetList0 = pps->cr_qp_offset_list[0];
      pic.CrQPOffsetList1 = pps->cr_qp_offset_list[1];
      pic.CrQPOffsetList2 = pps->cr_qp_offset_list[2];
      pic.CrQPOffsetList3 = pps->cr_qp_offset_list[3];
      pic.CrQPOffsetList4 = pps->cr_qp_offset_list[4];
      pic.CrQPOffsetList5 = pps->cr_qp_offset_list[5];
      pic.FirstSliceSegmentInPic = true;
      pic.SSEEnable = true;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_CMD2), cmd2) {
      /* Magic numbers taken from media-driver */
      cmd2.Values9 = (cmd2.Values9 & 0xffff) | 0x43840000;
      cmd2.Values12 = 0xffffffff;
      cmd2.Values15 = 0x4e201f40;
      cmd2.Values16 = (cmd2.Values16 & 0xf0ff0000) | 0xf003300;
      cmd2.Values17 = (cmd2.Values17 & 0xfff00000) | 0x2710;
      cmd2.Values19 = (cmd2.Values19 & 0x80ffffff) | 0x18000000;
      cmd2.Values19 = (cmd2.Values19 & 0x80ffffff) | 0x18000000;
      cmd2.Values21 &= 0xfffffff;
      cmd2.Values22 = 0x1f001102;
      cmd2.Values23 = 0xaaaa1f00;
      cmd2.Values27 = (cmd2.Values27 & 0xffff0000) | 0x1a1a;

      cmd2.FrameWidthInPixelsMinusOne = width_in_pix - 1;
      cmd2.FrameHeightInPixelsMinusOne = height_in_pix - 1;
      cmd2.PictureType = anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type);
      cmd2.TemporalMVPEnableFlag =
            anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) == 0 ?
            0 : sps->flags.sps_temporal_mvp_enabled_flag;
      cmd2.TransformSkip = pps->flags.transform_skip_enabled_flag;

      if (anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) != 0) {
         cmd2.NumRefIdxL0MinusOne = ref_list_info->num_ref_idx_l0_active_minus1;
         cmd2.NumRefIdxL1MinusOne = ref_list_info->num_ref_idx_l1_active_minus1;
      }

      cmd2.Values5 = (cmd2.Values5 & 0xff83ffff) | 0x400000;
      cmd2.Values14 = (cmd2.Values14 & 0xffff) | 0x7d00000;
      cmd2.Values15 = 0x4e201f40;
      cmd2.Values17 = (cmd2.Values17 & 0xfff00000) | 0x2710;
      cmd2.Values18 = (cmd2.Values18 & 0xffff) | 0x600000;
      cmd2.Values19 = (cmd2.Values19 & 0xffff0000) | 0xc0;
      cmd2.Values20 &= 0xfffeffff;
      cmd2.TilingEnable = pps->flags.tiles_enabled_flag;

      if (anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) != 0) {
         const StdVideoEncodeH265ReferenceListsInfo* ref_lists = frame_info->pStdPictureInfo->pRefLists;

         bool long_term = false;
         uint8_t ref_slot = ref_lists->RefPicList0[0];
         uint8_t cur_poc = frame_info->pStdPictureInfo->PicOrderCntVal;
         uint8_t ref_poc = anv_h265_get_ref_poc(enc_info, ref_lists, true, ref_slot, &long_term);
         int8_t diff_poc = cur_poc - ref_poc;

         cmd2.POCNumberForRefid0InL0 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL0 |= long_term;

         ref_slot = ref_lists->RefPicList0[1];
         ref_poc = anv_h265_get_ref_poc(enc_info, ref_lists, true, ref_slot, &long_term);
         diff_poc = ref_poc == 0xff ? 0 : cur_poc - ref_poc;

         cmd2.POCNumberForRefid1InL0 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL0 |= long_term;

         ref_slot = ref_lists->RefPicList0[2];
         ref_poc = anv_h265_get_ref_poc(enc_info, ref_lists, true, ref_slot, &long_term);
         diff_poc = ref_poc == 0xff ? 0 : cur_poc - ref_poc;

         cmd2.POCNumberForRefid2InL0 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL0 |= long_term;


         ref_slot = ref_lists->RefPicList1[0];
         ref_poc = anv_h265_get_ref_poc(enc_info, ref_lists, false, ref_slot, &long_term);
         diff_poc = ref_poc == 0xff ? 0 : cur_poc - ref_poc;

         cmd2.POCNumberForRefid0InL1 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL1 |= long_term;

         cmd2.POCNumberForRefid1InL1 = cmd2.POCNumberForRefid2InL1 = cmd2.POCNumberForRefid0InL1;
         cmd2.SubPelMode = 3;
      }
   }

   for (uint32_t slice_id = 0; slice_id < frame_info->naluSliceSegmentEntryCount; slice_id++) {
      const VkVideoEncodeH265NaluSliceSegmentInfoKHR *nalu = &frame_info->pNaluSliceSegmentEntries[slice_id];
      const StdVideoEncodeH265SliceSegmentHeader *next_slice_header = NULL;
      StdVideoEncodeH265SliceSegmentHeader *slice_header =
            (StdVideoEncodeH265SliceSegmentHeader *)nalu->pStdSliceSegmentHeader;

      bool is_last = (slice_id == frame_info->naluSliceSegmentEntryCount - 1);
      uint32_t slice_type = slice_header->slice_type % 5;
      uint32_t slice_qp = rc_disable ? nalu->constantQp : pps->init_qp_minus26 + 26;
      uint32_t slice_qp_delta = slice_qp - (pps->init_qp_minus26 + 26);

      if (slice_type == STD_VIDEO_H265_SLICE_TYPE_P)
         slice_header->slice_type = slice_type = STD_VIDEO_H265_SLICE_TYPE_B;

      assert(slice_qp >= 10 && slice_qp <= 51);

      uint32_t ctb_size = 1 << (sps->log2_diff_max_min_luma_coding_block_size +
          sps->log2_min_luma_coding_block_size_minus3 + 3);
      uint32_t ctb_w = DIV_ROUND_UP(width_in_pix, ctb_size);
      uint32_t ctb_h = DIV_ROUND_UP(height_in_pix, ctb_size);

      if (!is_last)
         next_slice_header = slice_header + 1;

      if (slice_type != STD_VIDEO_H265_SLICE_TYPE_I) {
         anv_batch_emit(&cmd->batch, GENX(HCP_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 0;
            ref.NumberofReferenceIndexesActive = ref_list_info->num_ref_idx_l0_active_minus1;

            for (uint32_t i = 0; i < ref_list_info->num_ref_idx_l0_active_minus1 + 1; i++) {
               const VkVideoReferenceSlotInfoKHR ref_slot = enc_info->pReferenceSlots[i];
               const VkVideoEncodeH265DpbSlotInfoKHR *dpb =
                     vk_find_struct_const(ref_slot.pNext, VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR);

               ref.ReferenceListEntry[i].ListEntry = dpb_idx[ref_slot.slotIndex];

               unsigned ref_poc = dpb->pStdReferenceInfo->PicOrderCntVal;
               int32_t diff_poc = frame_info->pStdPictureInfo->PicOrderCntVal - ref_poc;


               ref.ReferenceListEntry[i].ReferencePicturetbValue = CLAMP(diff_poc, -128, 127) & 0xff;
               ref.ReferenceListEntry[i].TopField = true;
            }
         }
      }

      if (slice_type == STD_VIDEO_H265_SLICE_TYPE_B) {
         anv_batch_emit(&cmd->batch, GENX(HCP_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 1;
            ref.NumberofReferenceIndexesActive = ref_list_info->num_ref_idx_l1_active_minus1;

            for (uint32_t i = 0; i < ref_list_info->num_ref_idx_l1_active_minus1 + 1; i++) {
               const VkVideoReferenceSlotInfoKHR ref_slot = enc_info->pReferenceSlots[i];

               const VkVideoEncodeH265DpbSlotInfoKHR *dpb =
                     vk_find_struct_const(ref_slot.pNext, VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR);

               ref.ReferenceListEntry[i].ListEntry = dpb_idx[ref_slot.slotIndex];

               unsigned ref_poc = dpb->pStdReferenceInfo->PicOrderCntVal;
               int32_t diff_poc = frame_info->pStdPictureInfo->PicOrderCntVal - ref_poc;

               ref.ReferenceListEntry[i].ReferencePicturetbValue = CLAMP(diff_poc, -128, 127) & 0xff;
               ref.ReferenceListEntry[i].TopField = true;
            }
         }
      }

      uint8_t chroma_log2_weight_denom = 0;

      if ((pps->flags.weighted_pred_flag && (slice_type == STD_VIDEO_H265_SLICE_TYPE_P)) ||
            (pps->flags.weighted_bipred_flag && (slice_type == STD_VIDEO_H265_SLICE_TYPE_B))) {
         assert (slice_header->pWeightTable);

         uint16_t chroma_weight, chroma_offset;
         const StdVideoEncodeH265WeightTable *w_tbl = slice_header->pWeightTable;
         chroma_log2_weight_denom = w_tbl->luma_log2_weight_denom + w_tbl->delta_chroma_log2_weight_denom;

         anv_batch_emit(&cmd->batch, GENX(HCP_WEIGHTOFFSET_STATE), w) {
            w.ReferencePictureListSelect = 0;

            for (unsigned i = 0; i < STD_VIDEO_H265_MAX_NUM_LIST_REF; i++) {

               w.LumaOffsets[i].DeltaLumaWeightLX = w_tbl->delta_luma_weight_l0[i] & 0xff;
               w.LumaOffsets[i].LumaOffsetLX = w_tbl->luma_offset_l0[i] & 0xff;
               w.ChromaOffsets[i].DeltaChromaWeightLX0 = w_tbl->delta_chroma_weight_l0[i][0] & 0xff;
               w.ChromaOffsets[i].DeltaChromaWeightLX1 = w_tbl->delta_chroma_weight_l0[i][1] & 0xff;


               chroma_weight = (1 << chroma_log2_weight_denom) + w_tbl->delta_chroma_weight_l0[i][0];
               chroma_offset = CLAMP(w_tbl->delta_chroma_offset_l0[i][0] -
                  ((128 * chroma_weight) >> chroma_log2_weight_denom) + 128, -128, 127);
               w.ChromaOffsets[i].ChromaOffsetLX0 = chroma_offset & 0xff;

               chroma_weight = (1 << chroma_log2_weight_denom) + w_tbl->delta_chroma_weight_l0[i][1];
               chroma_offset = CLAMP(w_tbl->delta_chroma_offset_l0[i][1] -
                  ((128 * chroma_weight) >> chroma_log2_weight_denom) + 128, -128, 127);
               w.ChromaOffsets[i].ChromaOffsetLX1 = chroma_offset & 0xff;
            }
         }

         if (slice_type == STD_VIDEO_H265_SLICE_TYPE_B) {
            anv_batch_emit(&cmd->batch, GENX(HCP_WEIGHTOFFSET_STATE), w) {
               w.ReferencePictureListSelect = 1;

               for (unsigned i = 0; i < STD_VIDEO_H265_MAX_NUM_LIST_REF; i++) {
                  w.LumaOffsets[i].DeltaLumaWeightLX = w_tbl->delta_luma_weight_l1[i] & 0xff;
                  w.LumaOffsets[i].LumaOffsetLX = w_tbl->luma_offset_l1[i] & 0xff;
                  w.ChromaOffsets[i].DeltaChromaWeightLX0 = w_tbl->delta_chroma_weight_l1[i][0] & 0xff;
                  w.ChromaOffsets[i].DeltaChromaWeightLX1 = w_tbl->delta_chroma_weight_l1[i][1] & 0xff;

                  chroma_weight = (1 << chroma_log2_weight_denom) + w_tbl->delta_chroma_weight_l1[i][0];
                  chroma_offset = CLAMP(w_tbl->delta_chroma_offset_l1[i][0] -
                     ((128 * chroma_weight) >> chroma_log2_weight_denom) + 128, -128, 127);
                  w.ChromaOffsets[i].ChromaOffsetLX0 = chroma_offset & 0xff;

                  chroma_weight = (1 << chroma_log2_weight_denom) + w_tbl->delta_chroma_weight_l1[i][1];
                  chroma_offset = CLAMP(w_tbl->delta_chroma_offset_l1[i][1] -
                     ((128 * chroma_weight) >> chroma_log2_weight_denom) + 128, -128, 127);
                  w.ChromaOffsets[i].ChromaOffsetLX1 = chroma_offset & 0xff;
               }
            }
         }
      }

      uint8_t slice_header_data[256] = { 0, };
      size_t slice_header_data_len_in_bytes = 0;
      vk_video_encode_h265_slice_header(frame_info->pStdPictureInfo,
                                        vps,
                                        sps,
                                        pps,
                                        slice_header,
                                        slice_qp_delta,
                                        &slice_header_data_len_in_bytes,
                                        &slice_header_data);
      uint32_t slice_header_data_len_in_bits = slice_header_data_len_in_bytes * 8;

      anv_batch_emit(&cmd->batch, GENX(HCP_SLICE_STATE), slice) {
         slice.SliceHorizontalPosition = slice_header->slice_segment_address % ctb_w;
         slice.SliceVerticalPosition = slice_header->slice_segment_address / ctb_w;

         if (is_last) {
            slice.NextSliceHorizontalPosition = 0;
            slice.NextSliceVerticalPosition = 0;
         } else {
            slice.NextSliceHorizontalPosition = next_slice_header->slice_segment_address % ctb_w;
            slice.NextSliceVerticalPosition = next_slice_header->slice_segment_address / ctb_w;
         }

         slice.SliceType = slice_type;
         slice.LastSlice = is_last;
         slice.DependentSlice = slice_header->flags.dependent_slice_segment_flag;
         slice.SliceTemporalMVPEnable = frame_info->pStdPictureInfo->flags.slice_temporal_mvp_enabled_flag;;
         slice.SliceQP = slice_qp;
         slice.SliceCbQPOffset = slice_header->slice_cb_qp_offset;
         slice.SliceCrQPOffset = slice_header->slice_cr_qp_offset;
         slice.SliceHeaderDisableDeblockingFilter = slice_header->flags.slice_deblocking_filter_disabled_flag;
         slice.SliceTCOffsetDiv2 = slice_header->slice_tc_offset_div2;
         slice.SliceBetaOffsetDiv2 = slice_header->slice_beta_offset_div2;
         slice.SliceLoopFilterEnable = slice_header->flags.slice_loop_filter_across_slices_enabled_flag;
         slice.SliceSAOChroma = slice_header->flags.slice_sao_chroma_flag;
         slice.SliceSAOLuma = slice_header->flags.slice_sao_luma_flag;
         slice.MVDL1Zero = slice_header->flags.mvd_l1_zero_flag;
         slice.CollocatedFromL0 = slice_header->flags.collocated_from_l0_flag;
         /* TODO. Support Low Delay mode */
         slice.LowDelay = false;

         if (slice_type != STD_VIDEO_H265_SLICE_TYPE_I && slice_header->pWeightTable) {
            slice.Log2WeightDenominatorChroma = slice_header->pWeightTable->luma_log2_weight_denom +
               (chroma_log2_weight_denom - slice_header->pWeightTable->luma_log2_weight_denom);
            slice.Log2WeightDenominatorLuma = slice_header->pWeightTable->luma_log2_weight_denom;
         }
         slice.CABACInit = slice_header->flags.cabac_init_flag;
         slice.MaxMergeIndex = slice_header->MaxNumMergeCand - 1;

         slice.CollocatedMVTemporalBufferIndex = dpb_idx[slice_header->collocated_ref_idx];
         assert(slice.CollocatedMVTemporalBufferIndex < ANV_VIDEO_H265_HCP_NUM_REF_FRAME);

         /* For VDEnc mode */
         slice.RoundInter = 4;
         slice.RoundIntra = 10;

         slice.SliceHeaderLength = 0;
         slice.CABACZeroWordInsertionEnable = false;
         slice.EmulationByteSliceInsertEnable = true;
         slice.TailInsertionPresent = false;
         slice.SliceDataInsertionPresent = true;
         slice.HeaderInsertionPresent = true;

         slice.IndirectPAKBSEDataStartOffset = 0;
         slice.TransformSkipLambda = 162;
         slice.TransformSkipNumberofZeroCoeffsFactor0 = 42;
         slice.TransformSkipNumberofNonZeroCoeffsFactor0 = 72;
         slice.TransformSkipNumberofZeroCoeffsFactor1 = 32;
         slice.TransformSkipNumberofNonZeroCoeffsFactor1 = 77;

         slice.OriginalSliceStartCtbX = slice_header->slice_segment_address % ctb_w;
         slice.OriginalSliceStartCtbY = slice_header->slice_segment_address / ctb_w;
      }

      uint32_t *dw;
      uint32_t length_in_dw;
      uint32_t data_bits_in_last_dw;

      length_in_dw = ALIGN(slice_header_data_len_in_bits, 32) >> 5;
      data_bits_in_last_dw = slice_header_data_len_in_bits & 0x1f;

      dw = anv_batch_emitn(&cmd->batch, length_in_dw + 2, GENX(HCP_PAK_INSERT_OBJECT),
            .LastHeader = true,
            .EndofSlice = true,
            .DataBitsInLastDW = data_bits_in_last_dw > 0 ? data_bits_in_last_dw : 32,
            .SliceHeaderIndicator = true,
            .HeaderLengthExcludedFromSize =  ACCUMULATE);

      memcpy(dw + 2, slice_header_data, length_in_dw * 4);

      anv_batch_emit(&cmd->batch, GENX(VDENC_WEIGHTSOFFSETS_STATE), vdenc_offsets) {
         vdenc_offsets.WeightsForwardReference0 = 1;
         vdenc_offsets.WeightsForwardReference1 = 1;
         vdenc_offsets.WeightsForwardReference2 = 1;
         vdenc_offsets.HEVCVP9WeightsBackwardReference0 = 1;
      }

      anv_batch_emit(&cmd->batch, GENX(VDENC_WALKER_STATE), vdenc_walker) {
         uint32_t slice_block_rows = DIV_ROUND_UP(src_img->vk.extent.height, ANV_MAX_H265_CTB_SIZE);
         uint32_t slice_block_cols = DIV_ROUND_UP(src_img->vk.extent.width, ANV_MAX_H265_CTB_SIZE);
         uint32_t num_ctu_in_slice = slice_block_cols * slice_block_rows;

         vdenc_walker.MBLCUStartYPosition = slice_header->slice_segment_address % ctb_w;
         vdenc_walker.NextSliceMBLCUStartXPosition = (slice_header->slice_segment_address + num_ctu_in_slice) / ctb_h;
         vdenc_walker.NextSliceMBStartYPosition = (slice_header->slice_segment_address + num_ctu_in_slice) / ctb_w;
         vdenc_walker.NextSliceMBLCUStartXPosition = (slice_header->slice_segment_address + num_ctu_in_slice) / ctb_h;
         vdenc_walker.TileWidth = width_in_pix - 1;
         vdenc_walker.TileHeight = height_in_pix - 1;
      }

      anv_batch_emit(&cmd->batch, GENX(VD_PIPELINE_FLUSH), flush) {
         flush.MFXPipelineDone = true;
         flush.VDENCPipelineDone = true;
         flush.VDENCPipelineCommandFlush = true;
         flush.VDCommandMessageParserDone = true;
      }
   }

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.VideoPipelineCacheInvalidate = 1;
   };

   anv_batch_emit(&cmd->batch, GENX(VD_PIPELINE_FLUSH), flush) {
      flush.HEVCPipelineDone = true;
      flush.HEVCPipelineCommandFlush = true;
      flush.VDCommandMessageParserDone = true;
   }

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.VideoPipelineCacheInvalidate = 0;
   };

#endif // GFX_VER >= 12

}

static void
emit_query_mi_availability(struct mi_builder *b,
                           struct anv_address addr,
                           bool available)
{
   mi_store(b, mi_mem64(addr), mi_imm(available));
}


#if GFX_VER < 11
#define MFC_BITSTREAM_BYTECOUNT_FRAME_REG       0x128A0
#define HCP_BITSTREAM_BYTECOUNT_FRAME_REG       0x1E9A0
#elif GFX_VER >= 11
#define MFC_BITSTREAM_BYTECOUNT_FRAME_REG       0x1C08A0
#define HCP_BITSTREAM_BYTECOUNT_FRAME_REG       0x1C28A0
#endif

static void
handle_inline_query_end(struct anv_cmd_buffer *cmd_buffer,
                        const VkVideoInlineQueryInfoKHR *inline_query)
{
   uint32_t reg_addr;
   struct mi_builder b;
   ANV_FROM_HANDLE(anv_query_pool, pool, inline_query->queryPool);
   if (pool == VK_NULL_HANDLE)
      return;
   struct anv_address query_addr = {
      .bo = pool->bo,
      .offset = inline_query->firstQuery * pool->stride,
   };

   mi_builder_init(&b, cmd_buffer->device->info, &cmd_buffer->batch);
   const uint32_t mocs = anv_mocs_for_address(cmd_buffer->device, &query_addr);
   mi_builder_set_mocs(&b, mocs);

   if (pool->codec & VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) {
      reg_addr = MFC_BITSTREAM_BYTECOUNT_FRAME_REG;
   } else if (pool->codec & VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR) {
      reg_addr = HCP_BITSTREAM_BYTECOUNT_FRAME_REG;
   } else {
      unreachable("Invalid codec operation");
   }

   mi_store(&b, mi_mem64(anv_address_add(query_addr, 8)), mi_reg32(reg_addr));
   emit_query_mi_availability(&b, query_addr, true);
}

void
genX(CmdEncodeVideoKHR)(VkCommandBuffer commandBuffer,
                        const VkVideoEncodeInfoKHR *pEncodeInfo)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   const VkVideoInlineQueryInfoKHR *inline_query =
      vk_find_struct_const(pEncodeInfo->pNext, VIDEO_INLINE_QUERY_INFO_KHR);

   switch (cmd_buffer->video.vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      anv_h264_encode_video(cmd_buffer, pEncodeInfo);
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      anv_h265_encode_video(cmd_buffer, pEncodeInfo);
      break;
   default:
      assert(0);
   }

   if (inline_query)
      handle_inline_query_end(cmd_buffer, inline_query);
}
