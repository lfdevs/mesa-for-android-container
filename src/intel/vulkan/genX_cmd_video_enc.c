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
#include "genxml/genX_video_pack.h"

#define MI_BUILDER_CAN_WRITE_BATCH false
#include "genX_mi_builder.h"

#if GFX_VERx10 >= 125
#include "h264_vdenc_tables.h"
#include "h265_vdenc_tables.h"
#include "av1_vdenc_tables.h"
#endif

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
   } else if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_P) {
      return 1;
   } else {
      return 2;
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

#if GFX_VERx10 < 125
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
#endif

static int32_t
anv_h264_dpb_slot_poc(const VkVideoEncodeInfoKHR *enc_info, uint8_t slot_index)
{
   for (unsigned j = 0; j < enc_info->referenceSlotCount; j++) {
      if (enc_info->pReferenceSlots[j].slotIndex != (int32_t)slot_index)
         continue;
      const VkVideoEncodeH264DpbSlotInfoKHR *dpb =
         vk_find_struct_const(enc_info->pReferenceSlots[j].pNext,
                              VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR);
      if (dpb && dpb->pStdReferenceInfo)
         return dpb->pStdReferenceInfo->PicOrderCnt;
   }
   return 0;
}

static void
anv_h264_encode_video(struct anv_cmd_buffer *cmd, const VkVideoEncodeInfoKHR *enc_info)
{
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, enc_info->dstBuffer);

   struct anv_video_session *vid = cmd->video.vid;
   struct vk_video_session_parameters *params = cmd->video.params;

   const struct VkVideoEncodeH264PictureInfoKHR *frame_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);

   const StdVideoH264SequenceParameterSet *sps = vk_video_find_h264_enc_std_sps(params, frame_info->pStdPictureInfo->seq_parameter_set_id);
   const StdVideoH264PictureParameterSet *pps = vk_video_find_h264_enc_std_pps(params, frame_info->pStdPictureInfo->pic_parameter_set_id);
   const StdVideoEncodeH264ReferenceListsInfo *ref_list_info = frame_info->pStdPictureInfo->pRefLists;

   const struct anv_image_view *iv = anv_image_view_from_handle(enc_info->srcPictureResource.imageViewBinding);
   const struct anv_image *src_img = iv->image;
   bool post_deblock_enable = anv_post_deblock_enable(pps, frame_info);
   bool rc_disable = cmd->video.vid->rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
   uint8_t dpb_idx[ANV_VIDEO_H264_MAX_NUM_REF_FRAME] = { 0,};

   const struct anv_image_view *base_ref_iv;
   uint32_t base_ref_array_layer;
   if (enc_info->pSetupReferenceSlot) {
      base_ref_iv = anv_image_view_from_handle(enc_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
      base_ref_array_layer = enc_info->pSetupReferenceSlot->pPictureResource->baseArrayLayer;
   } else {
      base_ref_iv = iv;
      base_ref_array_layer = enc_info->srcPictureResource.baseArrayLayer;
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

   anv_batch_emit(&cmd->batch, GENX(VDENC_CONTROL_STATE), v) {
      v.VdencInitialization = true;
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

         surface.Width = (i == 0 ? img_->vk.extent.width :
                          enc_info->srcPictureResource.codedExtent.width) - 1;
         surface.Height = (i == 0 ? img_->vk.extent.height :
                           enc_info->srcPictureResource.codedExtent.height) - 1;
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
            anv_image_dpb_address(base_ref_iv, base_ref_array_layer);
      } else {
         buf.PreDeblockingDestinationAddress =
            anv_image_dpb_address(base_ref_iv, base_ref_array_layer);
      }
      buf.PreDeblockingDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.PreDeblockingDestinationAddress.bo, 0),
      };
      buf.PostDeblockingDestinationAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.PostDeblockingDestinationAddress.bo, 0),
      };

      buf.OriginalUncompressedPictureSourceAddress =
         anv_image_dpb_address(iv, enc_info->srcPictureResource.baseArrayLayer);
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
            anv_image_dpb_address(ref_iv, enc_info->pReferenceSlots[i].pPictureResource->baseArrayLayer);

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
      vdenc_surface.SurfaceState.Width = enc_info->srcPictureResource.codedExtent.width - 1;
      vdenc_surface.SurfaceState.Height = enc_info->srcPictureResource.codedExtent.height - 1;
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
#if GFX_VERx10 == 125
      vdenc_buf.DSBWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif

      vdenc_buf.OriginalUncompressedPicture.Address =
         anv_image_dpb_address(iv, enc_info->srcPictureResource.baseArrayLayer);
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
               anv_image_dmv_top_address(ref_iv[0], enc_info->pReferenceSlots[0].pPictureResource->baseArrayLayer);
         vdenc_buf.FWDREF0.Address =
               anv_image_dpb_address(ref_iv[0], enc_info->pReferenceSlots[0].pPictureResource->baseArrayLayer);
      }

      vdenc_buf.ColocatedMVReadBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.ColocatedMVReadBuffer.Address.bo, 0),
      };

      vdenc_buf.FWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF0.Address.bo, 0),
      };

      if (ref_iv[1])
         vdenc_buf.FWDREF1.Address =
               anv_image_dpb_address(ref_iv[1], enc_info->pReferenceSlots[1].pPictureResource->baseArrayLayer);

      vdenc_buf.FWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF1.Address.bo, 0),
      };

      vdenc_buf.FWDREF2.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      /* B-frame backward (L1) reference recon surface. */
      if (frame_info->pStdPictureInfo->primary_pic_type == STD_VIDEO_H264_PICTURE_TYPE_B &&
          ref_list_info) {
         uint8_t bwd_slot = ref_list_info->RefPicList1[0];
         for (unsigned j = 0; bwd_slot != STD_VIDEO_H264_NO_REFERENCE_PICTURE &&
                              j < enc_info->referenceSlotCount; j++) {
            if (enc_info->pReferenceSlots[j].slotIndex != (int32_t)bwd_slot)
               continue;
            const struct anv_image_view *bwd_iv = anv_image_view_from_handle(
               enc_info->pReferenceSlots[j].pPictureResource->imageViewBinding);
            vdenc_buf.BWDREF0.Address = anv_image_dpb_address(
               bwd_iv, enc_info->pReferenceSlots[j].pPictureResource->baseArrayLayer);
            break;
         }
      }
      vdenc_buf.BWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.BWDREF0.Address.bo, 0),
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
#if GFX_VERx10 < 125
      vdenc_buf.VDEncCURecordStreamOutBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#else
      vdenc_buf.DSBWDREF04X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif
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

#if GFX_VERx10 == 125
      vdenc_buf.IntraPredictionRowStoreBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.ColocatedMVAVCWriteBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.Additional4XDSFWDREF.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif
   }

   StdVideoH264PictureType pic_type;

   pic_type = frame_info->pStdPictureInfo->primary_pic_type;

#if GFX_VERx10 < 125
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
#endif

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

#if GFX_VERx10 >= 125
   /* VDENC_CONST_QPT_STATE_CMD and VDENC_IMG_STATE has been changed to
    * VDENC_CMD3 and VDENC_AVC_IMG_STATE_CMD for Gen125 */
   {
      uint32_t slice_qp = 0;
      for (uint32_t slice_id = 0; slice_id < frame_info->naluSliceEntryCount; slice_id++) {
         const VkVideoEncodeH264NaluSliceInfoKHR *nalu = &frame_info->pNaluSliceEntries[slice_id];
         slice_qp = rc_disable ? nalu->constantQp : pps->pic_init_qp_minus26 + 26;
      }

      /* The h264_vdenc_cmd3_table is taken from media-driver.
       *
       * TODO: a P-frame in a B GOP uses type 1 instead of 2, which needs the GOP's B-frame count
       * (VkVideoEncodeH264RateControlInfoKHR::consecutiveBFrameCount); not plumbed through yet.
       */
      uint32_t cmd3_qp = CLAMP(slice_qp, 10, 51);
      uint8_t cmd3_type;
      if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_B)
         cmd3_type = enc_info->pSetupReferenceSlot ? 4 : 3;
      else if (pic_type == STD_VIDEO_H264_PICTURE_TYPE_P)
         cmd3_type = 2;
      else
         cmd3_type = 0;
      anv_batch_emit(&cmd->batch, GENX(VDENC_CMD3), cmd3) {
         for (unsigned i = 0; i < 22; i++)
            cmd3.Values[i] = h264_vdenc_cmd3_table[cmd3_type][cmd3_qp][i];

         if (cmd3_type == 0 &&
             (!ref_list_info || ref_list_info->num_ref_idx_l0_active_minus1 == 0))
            cmd3.Values[12] &= 0xf0ff;
      }

      bool is_bframe = pic_type == STD_VIDEO_H264_PICTURE_TYPE_B;
      bool is_inter = is_bframe || pic_type == STD_VIDEO_H264_PICTURE_TYPE_P;

      struct GENX(VDENC_AVC_IMG_STATE) img = {
         GENX(VDENC_AVC_IMG_STATE_header),
         .PictureType              = anv_vdenc_h264_picture_type(pic_type),
         .Transform8x8Flag         = pps->flags.transform_8x8_mode_flag,
         .SubpelMode               = 3,
         .PictureWidth             = sps->pic_width_in_mbs_minus1 + 1,
         .PictureHeightMinusOne    = sps->pic_height_in_map_units_minus1,
         .MinQp                    = 0x0a,
         .MaxQp                    = 0x33,
         .QpPrimeY                 = slice_qp,
         .POCNumberForCurrentPicture = frame_info->pStdPictureInfo->PicOrderCnt & 0xff,
      };

      if (is_inter) {
         /* Collocated MV write only when this frame is kept as a reference; collocated MV read
          * and the bidirectional weight are B-frame only. */
         img.CollocMVWREn = enc_info->pSetupReferenceSlot != NULL;

         uint32_t num_l0_minus1 =
            ref_list_info ? ref_list_info->num_ref_idx_l0_active_minus1 : 0;
         img.NumberOfL0ReferencesMinusOne = num_l0_minus1;

         /* Forward (L0) reference picture ids and their POCs; unused entries are 0xf. */
         uint8_t fwd_ref_idx[3] = { 0xf, 0xf, 0xf };
         int32_t fwd_ref_poc[3] = { 0, 0, 0 };
         for (unsigned i = 0; ref_list_info && i <= num_l0_minus1 && i < 3; i++) {
            uint8_t slot = ref_list_info->RefPicList0[i];
            if (slot == STD_VIDEO_H264_NO_REFERENCE_PICTURE)
               continue;
            fwd_ref_idx[i] = dpb_idx[slot] & 0xf;
            fwd_ref_poc[i] = anv_h264_dpb_slot_poc(enc_info, slot);
         }
         img.FwdRefIdx0ReferencePicture = fwd_ref_idx[0];
         img.FwdRefIdx1ReferencePicture = fwd_ref_idx[1];
         img.FwdRefIdx2ReferencePicture = fwd_ref_idx[2];
         img.POCNumberForFwdRef0 = fwd_ref_poc[0] & 0xff;
         img.POCNumberForFwdRef1 = fwd_ref_poc[1] & 0xff;
         img.POCNumberForFwdRef2 = fwd_ref_poc[2] & 0xff;

         if (is_bframe && ref_list_info) {
            uint8_t slot = ref_list_info->RefPicList1[0];
            img.CollocMVRDEn = true;
            img.BidirectionalWeight = 0x20;
            img.NumberOfL1ReferencesMinusOne = ref_list_info->num_ref_idx_l1_active_minus1;
            if (slot != STD_VIDEO_H264_NO_REFERENCE_PICTURE) {
               img.BwdRefIdx0ReferencePicture = dpb_idx[slot] & 0xf;
               img.POCNumberForBwdRef0 = anv_h264_dpb_slot_poc(enc_info, slot) & 0xff;
            }
         }
      }

      /* h264_vdenc_avc_img_state[targetUsage - 1][type][...];
       * TargetUsage is fixed to 4 (Normal/Balanced; 1 = Quality, 7 = Speed).
       * type 0 = I, 1 = P, 2 = B non-ref, 3 = B ref.
       *
       * TODO: the rest, intra-refresh / A-stepping / Wa_18011246551 / stream-in
       * are all 0 for now.
       */
      uint8_t img_type = !is_inter ? 0 : !is_bframe ? 1 :
                         (enc_info->pSetupReferenceSlot ? 3 : 2);
      const uint32_t *cost = h264_vdenc_avc_img_state[4 - 1][img_type][0][0][0][0];

      uint32_t *dw = anv_batch_emitn(&cmd->batch, 20, GENX(VDENC_AVC_IMG_STATE));
      GENX(VDENC_AVC_IMG_STATE_pack)(&cmd->batch, dw, &img);
      for (unsigned i = 0; i < 19; i++)
         dw[i + 1] |= cost[i];

      uint32_t level = vk_video_get_h264_level(sps->level_idc);
      dw[8] = (dw[8] & 0xffff) |
              (level <= 52 ? h264_vdenc_avc_img_state_dw8[level] : 0x02000000);
   }
#else
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
      vdenc_img.PictureType = anv_vdenc_h264_picture_type(pic_type) == 0 ? 0 : 1;
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
#endif

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

      unsigned w_in_mb = sps->pic_width_in_mbs_minus1 + 1;
      unsigned h_in_mb = sps->pic_height_in_map_units_minus1 + 1;

      uint8_t slice_header_data[256] = { 0, };
      size_t slice_header_data_len_in_bits = 0;
      vk_video_encode_h264_slice_header(frame_info->pStdPictureInfo,
                                        sps,
                                        pps,
                                        slice_header,
                                        slice_qp - (pps->pic_init_qp_minus26 + 26),
                                        &slice_header_data_len_in_bits,
                                        &slice_header_data);

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

      length_in_dw = align((uint32_t)slice_header_data_len_in_bits, 32) >> 5;
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

#if GFX_VERx10 >= 125
      anv_batch_emit(&cmd->batch, GENX(VDENC_AVC_SLICE_STATE), slice_state) {
         slice_state.RoundIntra = 5;
         slice_state.RoundIntraEnable = true;
         if (slice_type == STD_VIDEO_H264_SLICE_TYPE_I) {
            slice_state.RoundInter = 2;
            slice_state.RoundInterEnable = false;
         } else {
            slice_state.RoundInter = 3;
            slice_state.RoundInterEnable = false;
         }
         slice_state.Log2WeightDenomLuma =
            weight_table ? weight_table->luma_log2_weight_denom : 0;
      }
#endif

      anv_batch_emit(&cmd->batch, GENX(VDENC_WALKER_STATE), vdenc_walker) {
         vdenc_walker.NextSliceMBStartYPosition = h_in_mb;
#if GFX_VERx10 < 125
         vdenc_walker.Log2WeightDenominatorLuma = weight_table ? weight_table->luma_log2_weight_denom : 0;
#if GFX_VER >= 12
         vdenc_walker.TileWidth = src_img->vk.extent.width - 1;
#endif
#else
         vdenc_walker.FirstSuperSlice = 1;
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

#if GFX_VER >= 12
static const uint8_t hcp_transform_skip_coeffs_tbl[4][2][2][2][2] =
{
   { { { { 42, 37 },{ 32, 40 } },{ { 40, 40 },{ 32, 45 } } },
     { { { 29, 48 },{ 26, 53 } },{ { 26, 56 },{ 24, 62 } } } },
   { { { { 42, 40 },{ 32, 45 } },{ { 40, 46 },{ 32, 48 } } },
     { { { 26, 53 },{ 24, 58 } },{ { 32, 53 },{ 26, 64 } } } },
   { { { { 38, 42 },{ 32, 51 } },{ { 43, 43 },{ 35, 46 } } },
     { { { 26, 56 },{ 24, 64 } },{ { 35, 50 },{ 32, 57 } } } },
   { { { { 35, 46 },{ 32, 52 } },{ { 51, 42 },{ 38, 53 } } },
     { { { 29, 56 },{ 29, 70 } },{ { 38, 47 },{ 37, 64 } } } },
};

static const uint16_t hcp_transform_skip_lambda_tbl[52] =
{
   149, 149, 149, 149, 149, 149, 149, 149,
   149, 149, 149, 149, 149, 149, 149, 149,
   149, 149, 149, 149, 149, 149, 149, 149,
   149, 162, 174, 186, 199, 211, 224, 236,
   249, 261, 273, 286, 298, 298, 298, 298,
   298, 298, 298, 298, 298, 298, 298, 298,
   298, 298, 298, 298
};

#if GFX_VERx10 < 125
static const uint32_t hevc_sad_qp_lambda_tbl[3][42] =
{
   /* I-frame: QP 10-51 */
   {
      0x30002, 0x30002, 0x30002, 0x30003, 0x40004, 0x40005, 0x50006,
      0x60008, 0x6000a, 0x7000c, 0x8000f, 0x90013, 0xa0018, 0xb001e,
      0xc0026, 0xe0030, 0x10003d, 0x12004d, 0x140061, 0x16007a, 0x19009a,
      0x1c00c2, 0x1f00f4, 0x230133, 0x270183, 0x2c01e8, 0x320266, 0x380306,
      0x3e03cf, 0x4604cd, 0x4f060c, 0x58079f, 0x63099a, 0x6f0c18, 0x7d0f3d,
      0x8c1333, 0x9d1831, 0xb11e7a, 0xc62666, 0xdf3062, 0xfa3cf5, 0x1184ccd
   },
   /* P-frame: QP 10-51 */
   {
      0x30003, 0x30003, 0x30003, 0x40003, 0x40004, 0x50005, 0x50007,
      0x60008, 0x6000a, 0x7000d, 0x80011, 0x90015, 0xa001a, 0xb0021,
      0xd002a, 0xe0034, 0x100042, 0x120053, 0x140069, 0x170084, 0x1a00a6,
      0x1d00d2, 0x210108, 0x24014d, 0x2901a3, 0x2e0210, 0x34029a, 0x3a0347,
      0x410421, 0x490533, 0x52068d, 0x5c0841, 0x670a66, 0x740d1a, 0x821082,
      0x9214cd, 0xa41a35, 0xb82105, 0xce299a, 0xe8346a, 0x1044209, 0x1245333
   },
   /* B-frame: QP 10-51 */
   {
      0x30003, 0x30003, 0x30003, 0x40003, 0x40004, 0x50005, 0x50007,
      0x60008, 0x6000a, 0x7000d, 0x80011, 0x90015, 0xa001a, 0xb0021,
      0xd002a, 0xe0034, 0x100042, 0x120053, 0x140069, 0x170084, 0x1a00a6,
      0x1d00d2, 0x210108, 0x24014d, 0x2901a3, 0x2e0210, 0x34029a, 0x3a0347,
      0x410421, 0x490533, 0x52068d, 0x5c0841, 0x670a66, 0x740d1a, 0x821082,
      0x9214cd, 0xa41a35, 0xb82105, 0xce299a, 0xe8346a, 0x1044209, 0x1245333
   }
};
#endif

#endif // GFX_VER >= 12

static uint8_t
anv_h265_get_ref_poc(const VkVideoEncodeInfoKHR *enc_info,
                     const uint8_t slot_num,
                     bool *long_term)
{
   uint8_t ref_poc = 0xff;

   for (unsigned i = 0; i < enc_info->referenceSlotCount; i++) {
      const VkVideoReferenceSlotInfoKHR ref_slot_info = enc_info->pReferenceSlots[i];
      const VkVideoEncodeH265DpbSlotInfoKHR *dpb =
            vk_find_struct_const(ref_slot_info.pNext, VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR);

      if (!dpb)
         continue;

      if (ref_slot_info.slotIndex == slot_num) {
         ref_poc = dpb->pStdReferenceInfo->PicOrderCntVal;
         *long_term |= dpb->pStdReferenceInfo->flags.used_for_long_term_reference;
         break;
      }
   }

   return ref_poc;
}

static const VkVideoReferenceSlotInfoKHR *
anv_h265_find_ref_slot(const VkVideoEncodeInfoKHR *enc_info,
                       const uint8_t slot_num)
{
   for (unsigned i = 0; i < enc_info->referenceSlotCount; i++) {
      if (enc_info->pReferenceSlots[i].slotIndex == slot_num)
         return &enc_info->pReferenceSlots[i];
   }

   return NULL;
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
   struct vk_video_session_parameters *params = cmd->video.params;

   const struct VkVideoEncodeH265PictureInfoKHR *frame_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);

   const StdVideoH265VideoParameterSet *vps = vk_video_find_h265_enc_std_vps(params, frame_info->pStdPictureInfo->sps_video_parameter_set_id);
   const StdVideoH265SequenceParameterSet *sps = vk_video_find_h265_enc_std_sps(params, frame_info->pStdPictureInfo->pps_seq_parameter_set_id);
   const StdVideoH265PictureParameterSet *pps = vk_video_find_h265_enc_std_pps(params, frame_info->pStdPictureInfo->pps_pic_parameter_set_id);
   StdVideoEncodeH265ReferenceListsInfo* ref_lists =
      (struct StdVideoEncodeH265ReferenceListsInfo *)frame_info->pStdPictureInfo->pRefLists;
   const bool is_10bit = (sps->bit_depth_luma_minus8 == 2) || (sps->bit_depth_chroma_minus8 == 2);

   const struct anv_image_view *iv = anv_image_view_from_handle(enc_info->srcPictureResource.imageViewBinding);
   const struct anv_image *src_img = iv->image;

   const struct anv_image_view *base_ref_iv;
   uint32_t base_ref_array_layer;

   bool rc_disable = cmd->video.vid->rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
   bool is_low_delay = true;

   if (ref_lists) {
      for (unsigned list = 0; list < 2; list++) {
         const uint8_t *ref_pic_list = list == 0 ? ref_lists->RefPicList0 :
                                                   ref_lists->RefPicList1;
         unsigned ref_cnt = (list == 0 ? ref_lists->num_ref_idx_l0_active_minus1 :
                                         ref_lists->num_ref_idx_l1_active_minus1) + 1;

         for (unsigned i = 0; i < ref_cnt; i++) {
            const VkVideoReferenceSlotInfoKHR *slot;
            const VkVideoEncodeH265DpbSlotInfoKHR *dpb;

            if (ref_pic_list[i] == STD_VIDEO_H265_NO_REFERENCE_PICTURE)
               continue;

            slot = anv_h265_find_ref_slot(enc_info, ref_pic_list[i]);

            if (!slot)
               continue;

            dpb = vk_find_struct_const(slot->pNext, VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR);

            if (dpb && dpb->pStdReferenceInfo->PicOrderCntVal >
                       frame_info->pStdPictureInfo->PicOrderCntVal)
               is_low_delay = false;
         }
      }
   }

   if (enc_info->pSetupReferenceSlot) {
      base_ref_iv = anv_image_view_from_handle(enc_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
      base_ref_array_layer = enc_info->pSetupReferenceSlot->pPictureResource->baseArrayLayer;
   } else {
      base_ref_iv = iv;
      base_ref_array_layer = enc_info->srcPictureResource.baseArrayLayer;
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
         ss.SurfaceFormat = is_10bit ? P010 : PLANAR_420_8;

         ss.YOffsetforUCb = img_->planes[1].primary_surface.memory_range.offset /
                            img_->planes[0].primary_surface.isl.row_pitch_B;
         ss.YOffsetforVCr = ss.YOffsetforUCb;
      }
   }

   anv_batch_emit(&cmd->batch, GENX(HCP_PIPE_BUF_ADDR_STATE), buf) {
      buf.DecodedPictureAddress =
         anv_image_dpb_address(base_ref_iv, base_ref_array_layer);

      buf.DecodedPictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.DecodedPictureAddress.bo, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
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

      buf.CurrentMVTemporalBufferAddress =
         anv_image_dmv_top_address(iv, enc_info->srcPictureResource.baseArrayLayer);

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
            anv_image_dpb_address(ref_iv, enc_info->pReferenceSlots[i].pPictureResource->baseArrayLayer);
      }

      buf.ReferencePictureMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
#if GFX_VERx10 >= 125
         .TiledResourceMode = TRMODE_TILEF,
#endif
      };

      buf.OriginalUncompressedPictureSourceAddress =
         anv_image_dpb_address(iv, enc_info->srcPictureResource.baseArrayLayer);
      buf.OriginalUncompressedPictureSourceMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.OriginalUncompressedPictureSourceAddress.bo, 0),
      };

      buf.StreamOutDataDestinationAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_PAK_STREAMOUT].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_PAK_STREAMOUT].offset
      };

      buf.StreamOutDataDestinationMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.StreamOutDataDestinationAddress.bo, 0),
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
            anv_image_dmv_top_address(ref_iv, enc_info->pReferenceSlots[i].pPictureResource->baseArrayLayer);
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

      buf.SAOStreamOutDataDestinationBufferBaseAddress = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_SAO_STREAMOUT].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_SAO_STREAMOUT].offset
      };
      buf.SAOStreamOutDataDestinationBufferMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
         .MOCS = anv_mocs(cmd->device, buf.SAOStreamOutDataDestinationBufferBaseAddress.bo, 0),
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
      vdenc_pipe_mode.BitDepth = is_10bit ? 2 : 0;
      vdenc_pipe_mode.PAKChromaSubSamplingType = _420;
      vdenc_pipe_mode.OutputRangeControlAfterColorSpaceConversion = true;
      vdenc_pipe_mode.IsRandomAccess = !is_low_delay;
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
      vdenc_surface.SurfaceState.Width = enc_info->srcPictureResource.codedExtent.width - 1;
      vdenc_surface.SurfaceState.Height = enc_info->srcPictureResource.codedExtent.height - 1;
      vdenc_surface.SurfaceState.SurfaceFormat = is_10bit ? VDENC_P010 : VDENC_PLANAR_420_8;

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
      vdenc_surface.SurfaceState.SurfaceFormat = is_10bit ? VDENC_P010 : VDENC_PLANAR_420_8;
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

#if GFX_VERx10 == 125
      vdenc_buf.DSBWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif

      vdenc_buf.OriginalUncompressedPicture.Address =
         anv_image_dpb_address(iv, enc_info->srcPictureResource.baseArrayLayer);
      vdenc_buf.OriginalUncompressedPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.OriginalUncompressedPicture.Address.bo, 0),
      };

      vdenc_buf.StreamInDataPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.RowStoreScratchBuffer.Address = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_H265_VDENC_INTRA_ROW_STORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_H265_VDENC_INTRA_ROW_STORE].offset
      };

      vdenc_buf.RowStoreScratchBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.RowStoreScratchBuffer.Address.bo, 0),
      };

      const struct anv_image_view *ref_iv[3] = { 0, };
      const struct anv_image_view *bwd_ref_iv = NULL;
      uint32_t ref_layer[3] = { 0, };
      uint32_t bwd_ref_layer = 0;

      if (ref_lists) {
         for (unsigned i = 0; i < ref_lists->num_ref_idx_l0_active_minus1 + 1 && i < 3; i++) {
            const VkVideoReferenceSlotInfoKHR *slot =
               anv_h265_find_ref_slot(enc_info, ref_lists->RefPicList0[i]);

            if (!slot)
               continue;

            ref_iv[i] = anv_image_view_from_handle(slot->pPictureResource->imageViewBinding);
            ref_layer[i] = slot->pPictureResource->baseArrayLayer;
         }

         if (!is_low_delay) {
            const VkVideoReferenceSlotInfoKHR *slot =
               anv_h265_find_ref_slot(enc_info, ref_lists->RefPicList1[0]);

            if (slot) {
               bwd_ref_iv = anv_image_view_from_handle(slot->pPictureResource->imageViewBinding);
               bwd_ref_layer = slot->pPictureResource->baseArrayLayer;
            }
         }
      }

      if (ref_iv[0]) {
         vdenc_buf.ColocatedMVReadBuffer.Address =
               anv_image_dmv_top_address(ref_iv[0], ref_layer[0]);
         vdenc_buf.FWDREF0.Address =
               anv_image_dpb_address(ref_iv[0], ref_layer[0]);
      }

      vdenc_buf.ColocatedMVReadBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.ColocatedMVReadBuffer.Address.bo, 0),
      };

      vdenc_buf.FWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF0.Address.bo, 0),
      };

      if (ref_iv[1])
         vdenc_buf.FWDREF1.Address =
               anv_image_dpb_address(ref_iv[1], ref_layer[1]);

      vdenc_buf.FWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF1.Address.bo, 0),
      };

      if (ref_iv[2])
         vdenc_buf.FWDREF2.Address =
               anv_image_dpb_address(ref_iv[2], ref_layer[2]);

      vdenc_buf.FWDREF2.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.FWDREF2.Address.bo, 0),
      };

      if (bwd_ref_iv)
         vdenc_buf.BWDREF0.Address =
               anv_image_dpb_address(bwd_ref_iv, bwd_ref_layer);

      vdenc_buf.BWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.BWDREF0.Address.bo, 0),
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
#if GFX_VERx10 < 125
      vdenc_buf.VDEncCURecordStreamOutBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#else
      vdenc_buf.DSBWDREF04X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif
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

#if GFX_VERx10 == 125
      vdenc_buf.IntraPredictionRowStoreBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.ColocatedMVAVCWriteBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.Additional4XDSFWDREF.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
#endif
   }

#if GFX_VERx10 >= 125
   bool is_intra =
      anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) == 0;
   uint32_t cmd1_qp = rc_disable ? frame_info->pNaluSliceSegmentEntries[0].constantQp :
                      pps->init_qp_minus26 + 26;

   /* TODO: handling low-delay / B-GOP weights for P/B frames. */
   double weight = is_intra ? 0.60 : 0.65;
   double num = weight * hevc_vdenc_cmd1_qp_scale[cmd1_qp - 1];
   uint32_t par0 = MIN2(65535u, (uint32_t)(num * 4 + 0.5));
   uint32_t par1 = MIN2(65535u, (uint32_t)(sqrt(num) * 4 + 0.5));

   static const uint8_t par2[8]  = { 0, 2, 3, 5, 6, 8, 9, 11 };
   static const uint8_t par3[12] = { 4, 12, 20, 28, 36, 44, 52, 60, 68, 76, 84, 92 };

   uint32_t v[32] = { 0 };
   for (unsigned i = 0; i < 8; i++)
      v[i / 4] |= (uint32_t)par2[i] << (8 * (i % 4));
   for (unsigned i = 0; i < 12; i++) {
      v[2 + i / 4] |= (uint32_t)par3[i] << (8 * (i % 4));
      v[5 + i / 4] |= (uint32_t)par3[i] << (8 * (i % 4));
   }
   v[12] = 4u << 16;
   v[15] = (uint32_t)(is_intra ? 21 : 7) << 16 | (uint32_t)(is_intra ? 0 : 4) << 24;
   v[18] = 20u << 16;
   v[19] = v[20] = 0x0c0c0c0c;
   v[21] = par0 | par1 << 16;
   for (unsigned i = 22; i <= 29; i++)
      v[i] = 0x10101010;
   v[30] = (uint32_t)(is_intra ? 16 : 20) |
           (uint32_t)(is_intra ? 16 : 20) << 8 |
           (uint32_t)(is_intra ? 47 : 20) << 16;

   anv_batch_emit(&cmd->batch, GENX(VDENC_CMD1), cmd1) {
      for (unsigned i = 0; i < 32; i++)
         cmd1.Values[i] = v[i];
   }
#else
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
#endif

   uint32_t frame_width_in_min_cb = sps->pic_width_in_luma_samples >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
   uint32_t frame_height_in_min_cb = sps->pic_height_in_luma_samples >> (sps->log2_min_luma_coding_block_size_minus3 + 3);
   uint32_t width_in_pix = frame_width_in_min_cb << (sps->log2_min_luma_coding_block_size_minus3 + 3);
   uint32_t height_in_pix = frame_height_in_min_cb << (sps->log2_min_luma_coding_block_size_minus3 + 3);

   anv_batch_emit(&cmd->batch, GENX(HCP_PIC_STATE), pic) {
      pic.FrameWidthInMinimumCodingBlockSize = frame_width_in_min_cb - 1;
      pic.FrameHeightInMinimumCodingBlockSize = frame_height_in_min_cb - 1;
      pic.TransformSkipEnable = pps->flags.transform_skip_enabled_flag;
      pic.PAKTransformSkipEnable = pps->flags.transform_skip_enabled_flag;

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
      pic.MaxDQPDepth = pps->flags.cu_qp_delta_enabled_flag ? pps->diff_cu_qp_delta_depth : 0;
      pic.PCMLoopFilterDisable = sps->flags.pcm_loop_filter_disabled_flag;
      pic.ConstrainedIntraPrediction = pps->flags.constrained_intra_pred_flag;
      pic.TilingEnable = pps->flags.tiles_enabled_flag;
      pic.WeightedBiPredicationEnable = pps->flags.weighted_bipred_flag;
      pic.WeightedPredicationEnable = pps->flags.weighted_pred_flag;
      pic.FieldPic = 0;
      pic.TopField = false;
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
      /* for VDENC mode */
      pic.RhoDomainRateControlEnable = true;
      pic.FractionalQPAdjustmentEnable = true;
      pic.RhoDomainFrameLevelQP = pps->init_qp_minus26 + 26;
   }

   uint32_t frame_qp = rc_disable ?
      frame_info->pNaluSliceSegmentEntries[0].constantQp :
      pps->init_qp_minus26 + 26;

   anv_batch_emit(&cmd->batch, GENX(VDENC_CMD2), cmd2) {
      /* Reference index mapping VDEnc hands to PAK: one byte per reference,
       * L0[0..2] followed by L1[0]. It has to agree with the list entries
       * programmed in HCP_REF_IDX_STATE.
       *
       * 0x7 tags a slot VDEnc does not use. Gen125 tags the backward slot
       * that way when the frame has no backward reference, Gen12 leaves it
       * at 0.
       */
      const unsigned l1_entry = 3;
#if GFX_VERx10 >= 125
      uint8_t ref_idx[4] = { 0x7, 0x7, 0x7, 0x7 };
#else
      uint8_t ref_idx[4] = { 0, 0x7, 0x7, 0 };
#endif

#if GFX_VERx10 >= 125
      /* Target usage is fixed to 4. The last two lookup indices are
       * pps_curr_pic_ref_enabled_flag, which is not supported here, and
       * Wa_22011549751, which is in effect and also decides the intra
       * reference indices below.
       *
       * TODO: Target usage from the quality level;
       * poc from the ref structure for hierarchical B;
       * port the dw51/dw53 LUTs; derive data[19]/data[23] for inter.
       */
      uint32_t pic_type = anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type);
      uint32_t target_usage = 4;
      uint32_t low_delay = is_low_delay;
      uint32_t num_l0_is0 = pic_type == 0 ? 1 : (ref_lists->num_ref_idx_l0_active_minus1 == 0);

      cmd2.Values5  |= hevc_vdenc_cmd2_dw5[pic_type];
      cmd2.Values7  |= hevc_vdenc_cmd2_dw7[pic_type][num_l0_is0][low_delay][0][1];
      cmd2.Values9  |= hevc_vdenc_cmd2_dw9[pic_type][target_usage][low_delay][0][1];
      cmd2.Values12 |= hevc_vdenc_cmd2_dw12[target_usage];
      cmd2.Values19 |= 0x98000000;
      cmd2.Values23 |= 0xcccc0000;
      cmd2.Values53 |= hevc_vdenc_cmd2_dw52[target_usage];
      cmd2.Values55 |= hevc_vdenc_cmd2_dw54[target_usage][0];
      cmd2.Values52 |= pic_type == 0 ? 0x20003552 : 0x22223552;
      cmd2.Values54 |= pic_type == 0 ? 0x80000000 : 0xff000000;

      if (pic_type == 0) {
         /* Wa_22011549751 also forces intra frames to low delay B with both
          * reference indices at 0. The dw7/dw9 lookups above assume the same
          * workaround is in effect.
          */
         ref_idx[0] = 0;
         ref_idx[l1_entry] = 0;
      } else {
         for (unsigned i = 0;
              i < ref_lists->num_ref_idx_l0_active_minus1 + 1 && i < l1_entry;
              i++) {
            uint8_t slot = ref_lists->RefPicList0[i];

            if (slot != STD_VIDEO_H265_NO_REFERENCE_PICTURE)
               ref_idx[i] = dpb_idx[slot];
         }

         /* VDEnc only takes a backward reference when the frame references
          * the future.
          */
         if (!is_low_delay) {
            uint8_t slot = ref_lists->RefPicList1[0];

            if (slot != STD_VIDEO_H265_NO_REFERENCE_PICTURE)
               ref_idx[l1_entry] = dpb_idx[slot];
         }
      }
#else
      cmd2.Values5  = (cmd2.Values5 & 0xff83ffff) | 0x400000;
      cmd2.Values9  = (cmd2.Values9 & 0xffff) | 0x43840000;
      cmd2.Values12 = 0xffffffff;
      cmd2.Values14 = (cmd2.Values14 & 0xffff) | 0x7d00000;
      cmd2.Values15 = 0x4e201f40;
      cmd2.Values17 = (cmd2.Values17 & 0xfff00000) | 0x2710;
      cmd2.Values18 = (cmd2.Values18 & 0xffff) | 0x600000;
      cmd2.Values19 = (cmd2.Values19 & 0x80ffffff) | 0x18000000;
      cmd2.Values20 &= 0xfffeffff;
      cmd2.Values21 &= 0xfffffff;
      cmd2.Values22 = 0x1f001102;
      cmd2.Values23 = 0xaaaa1f00;

      bool is_inter =
         anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) != 0;

      if (is_inter) {
         if (ref_lists->num_ref_idx_l0_active_minus1 == 0)
            cmd2.Values7 |= 0x80000;

         if (is_low_delay) {
            cmd2.Values8 = 0;
            cmd2.Values9 &= 0xffff0000;
         } else {
            cmd2.Values7 &= 0xfff7feff;
            cmd2.Values8 = 0x54555555;
            cmd2.Values9 = (cmd2.Values9 & 0xffff0000) | 0x5555;
         }

         for (unsigned i = 0;
              i < ref_lists->num_ref_idx_l0_active_minus1 + 1 && i < l1_entry;
              i++) {
            uint8_t slot = ref_lists->RefPicList0[i];

            ref_idx[i] = slot == STD_VIDEO_H265_NO_REFERENCE_PICTURE ?
                         0 : dpb_idx[slot];
         }

         /* VDEnc only takes a backward reference when the frame references
          * the future.
          */
         if (!is_low_delay) {
            uint8_t slot = ref_lists->RefPicList1[0];

            if (slot != STD_VIDEO_H265_NO_REFERENCE_PICTURE)
               ref_idx[l1_entry] = dpb_idx[slot];
         }
      }
#endif

      for (unsigned i = 0; i < ARRAY_SIZE(ref_idx); i++)
         cmd2.Values11 |= (uint32_t)ref_idx[i] << (8 * i);

      cmd2.Values11 |= 1u << 31;

      cmd2.Values16 = (cmd2.Values16 & 0xf0ff0000) | 0xf003300;
      cmd2.QpPrimeYAc = frame_qp;

      cmd2.FrameWidthInPixelsMinusOne = width_in_pix - 1;
      cmd2.FrameHeightInPixelsMinusOne = height_in_pix - 1;
#if GFX_VERx10 >= 125
      /* Wa_22011549751: intra frames are programmed as low delay B. */
      cmd2.PictureType = is_low_delay ? 3 : 2;
#else
      cmd2.PictureType = anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) == 0 ?
                         0 : (is_low_delay ? 3 : 2);
#endif
      cmd2.TemporalMVPEnableFlag =
            anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) == 0 ?
            0 : sps->flags.sps_temporal_mvp_enabled_flag;
      cmd2.TransformSkip = pps->flags.transform_skip_enabled_flag;
      cmd2.TilingEnable = pps->flags.tiles_enabled_flag;

      if (anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) != 0) {
         /* Handle GPB(Generalized P and B frames) */
         if (frame_info->pStdPictureInfo->pic_type == STD_VIDEO_H265_PICTURE_TYPE_P) {
            ref_lists->num_ref_idx_l1_active_minus1 = ref_lists->num_ref_idx_l0_active_minus1;
            for (int i = 0; i< STD_VIDEO_H265_MAX_NUM_LIST_REF; i++) {
               ref_lists->RefPicList1[i] = ref_lists->RefPicList0[i];
               ref_lists->list_entry_l1[i] = ref_lists->list_entry_l0[i];
            }
         }

         cmd2.NumRefIdxL0MinusOne = ref_lists->num_ref_idx_l0_active_minus1;
         cmd2.NumRefIdxL1MinusOne = ref_lists->num_ref_idx_l1_active_minus1;

         bool long_term = false;
         uint8_t ref_slot = ref_lists->RefPicList0[0];
         uint8_t cur_poc = frame_info->pStdPictureInfo->PicOrderCntVal;
         uint8_t ref_poc = anv_h265_get_ref_poc(enc_info, ref_slot, &long_term);
         int8_t diff_poc = cur_poc - ref_poc;

         cmd2.POCNumberForRefid0InL0 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL0 |= long_term;

         ref_slot = ref_lists->RefPicList0[1];
         ref_poc = anv_h265_get_ref_poc(enc_info, ref_slot, &long_term);
         diff_poc = ref_poc == 0xff ? 0 : cur_poc - ref_poc;

         cmd2.POCNumberForRefid1InL0 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL0 |= long_term;

         ref_slot = ref_lists->RefPicList0[2];
         ref_poc = anv_h265_get_ref_poc(enc_info, ref_slot, &long_term);
         diff_poc = ref_poc == 0xff ? 0 : cur_poc - ref_poc;

         cmd2.POCNumberForRefid2InL0 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL0 |= long_term;


         ref_slot = ref_lists->RefPicList1[0];
         ref_poc = anv_h265_get_ref_poc(enc_info, ref_slot, &long_term);
         diff_poc = ref_poc == 0xff ? 0 : cur_poc - ref_poc;

         cmd2.POCNumberForRefid0InL1 = CLAMP(diff_poc, -16, 16);
         cmd2.LongTermReferenceFlagsL1 |= long_term;

         cmd2.POCNumberForRefid1InL1 = cmd2.POCNumberForRefid1InL0;
         cmd2.POCNumberForRefid2InL1 = cmd2.POCNumberForRefid2InL0;
         cmd2.SubPelMode = 3;
      }

#if GFX_VERx10 < 125
      int tbl_idx = anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type);
      cmd2.Values26 = hevc_sad_qp_lambda_tbl[tbl_idx][CLAMP(frame_qp, 10, 51) - 10];
#endif
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
         if (pps->num_ref_idx_l0_default_active_minus1 != ref_lists->num_ref_idx_l0_active_minus1 ||
             pps->num_ref_idx_l1_default_active_minus1 != ref_lists->num_ref_idx_l1_active_minus1) {
            slice_header->flags.num_ref_idx_active_override_flag = true;
         }

         anv_batch_emit(&cmd->batch, GENX(HCP_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 0;
            ref.NumberofReferenceIndexesActive = ref_lists->num_ref_idx_l0_active_minus1;

            for (uint32_t i = 0; i < ref_lists->num_ref_idx_l0_active_minus1 + 1; i++) {
               uint8_t slot = ref_lists->RefPicList0[i];
               bool long_term = false;

               if (slot == STD_VIDEO_H265_NO_REFERENCE_PICTURE)
                  continue;

               uint8_t ref_poc = anv_h265_get_ref_poc(enc_info, slot, &long_term);
               int32_t diff_poc = frame_info->pStdPictureInfo->PicOrderCntVal - ref_poc;

               ref.ReferenceListEntry[i].ListEntry = dpb_idx[slot];
               ref.ReferenceListEntry[i].ReferencePicturetbValue = CLAMP(diff_poc, -128, 127) & 0xff;
               ref.ReferenceListEntry[i].LongTermReference = long_term;
               ref.ReferenceListEntry[i].TopField = true;
            }
         }
      }

      if (slice_type == STD_VIDEO_H265_SLICE_TYPE_B) {
         anv_batch_emit(&cmd->batch, GENX(HCP_REF_IDX_STATE), ref) {
            ref.ReferencePictureListSelect = 1;
            ref.NumberofReferenceIndexesActive = ref_lists->num_ref_idx_l1_active_minus1;

            for (uint32_t i = 0; i < ref_lists->num_ref_idx_l1_active_minus1 + 1; i++) {
               uint8_t slot = ref_lists->RefPicList1[i];
               bool long_term = false;

               if (slot == STD_VIDEO_H265_NO_REFERENCE_PICTURE)
                  continue;

               uint8_t ref_poc = anv_h265_get_ref_poc(enc_info, slot, &long_term);
               int32_t diff_poc = frame_info->pStdPictureInfo->PicOrderCntVal - ref_poc;

               ref.ReferenceListEntry[i].ListEntry = dpb_idx[slot];
               ref.ReferenceListEntry[i].ReferencePicturetbValue = CLAMP(diff_poc, -128, 127) & 0xff;
               ref.ReferenceListEntry[i].LongTermReference = long_term;
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
         slice.MVDL1Zero = 0; /* Only for decoder */
         slice.CollocatedFromL0 = slice_header->flags.collocated_from_l0_flag;
         slice.LowDelay = is_low_delay;

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
         slice.TransformSkipLambda = hcp_transform_skip_lambda_tbl[slice_qp];

         int qp_idx = 0;
         if (slice_qp <= 22) {
            qp_idx = 0;
         } else if (slice_qp <= 27) {
            qp_idx = 1;
         } else if (slice_qp <= 32) {
            qp_idx = 2;
         } else {
            qp_idx = 3;
         }

         if (anv_vdenc_h265_picture_type(frame_info->pStdPictureInfo->pic_type) == 0) {
            slice.TransformSkipNumberofZeroCoeffsFactor0 = hcp_transform_skip_coeffs_tbl[qp_idx][0][0][0][0];
            slice.TransformSkipNumberofZeroCoeffsFactor1 = hcp_transform_skip_coeffs_tbl[qp_idx][0][0][1][0];
            slice.TransformSkipNumberofNonZeroCoeffsFactor0 = hcp_transform_skip_coeffs_tbl[qp_idx][0][0][0][1] + 32;
            slice.TransformSkipNumberofNonZeroCoeffsFactor1 = hcp_transform_skip_coeffs_tbl[qp_idx][0][0][1][1] + 32;
         } else {
            slice.TransformSkipNumberofZeroCoeffsFactor0 = hcp_transform_skip_coeffs_tbl[qp_idx][1][0][0][0];
            slice.TransformSkipNumberofZeroCoeffsFactor1 = hcp_transform_skip_coeffs_tbl[qp_idx][1][0][1][0];
            slice.TransformSkipNumberofNonZeroCoeffsFactor0 = hcp_transform_skip_coeffs_tbl[qp_idx][1][0][0][1] + 32;
            slice.TransformSkipNumberofNonZeroCoeffsFactor1 = hcp_transform_skip_coeffs_tbl[qp_idx][1][0][1][1] + 32;
         }

         slice.OriginalSliceStartCtbX = slice_header->slice_segment_address % ctb_w;
         slice.OriginalSliceStartCtbY = slice_header->slice_segment_address / ctb_w;
      }

      uint32_t *dw;
      uint32_t length_in_dw;
      uint32_t data_bits_in_last_dw;

      length_in_dw = align((uint32_t)slice_header_data_len_in_bits, 32) >> 5;
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

#if GFX_VERx10 >= 125
       /* TODO: Different setting according to the target usage;
        * SCC (palette/IBC) and bit-depth > 8 adjustments;
        * per-tile origin/size/offsets once multi-tile is supported.
        */
      uint32_t qp_idx = slice_qp <= 12 ? 0 : slice_qp <= 17 ? 1 : slice_qp <= 22 ? 2 :
                        slice_qp <= 27 ? 3 : slice_qp <= 32 ? 4 : slice_qp <= 37 ? 5 :
                        slice_qp <= 42 ? 6 : slice_qp <= 47 ? 7 : slice_qp <= 49 ? 8 : 9;
      const uint32_t *param = h265_vdenc_hevc_tile_slice_params[1][qp_idx];

      anv_batch_emit(&cmd->batch, GENX(VDENC_HEVC_VP9_TILE_SLICE_STATE), til) {
         til.NumParEngine = 0;
         til.TileWidth = width_in_pix - 1;
         til.TileHeight = height_in_pix - 1;

         til.TileSliceParam0  = 0;
         til.TileSliceParam1  = param[7];
         til.TileSliceParam2  = 0;
         til.TileSliceParam3  = param[9];
         til.TileSliceParam4  = param[8];
         til.TileSliceParam5  = 1;
         til.TileSliceParam6  = param[4];
         til.TileSliceParam7  = param[2];
         til.TileSliceParam8  = param[3];
         til.TileSliceParam9  = param[1];
         til.TileSliceParam10 = param[5];
         til.TileSliceParam11 = 2;
         til.TileSliceParam12 = 72;
         til.TileSliceParam13 = 1;
         til.TileSliceParam14 = 0;
         til.TileSliceParam15 = param[0];
         til.TileSliceParam16 = 63;
         til.TileSliceParam17 = 63;
         til.TileSliceParam18 = 63;
         til.TileSliceParam19 = 6;
         til.TileSliceParam20 = 0x5;
      }
#endif

      anv_batch_emit(&cmd->batch, GENX(VDENC_WALKER_STATE), vdenc_walker) {
         uint32_t slice_block_rows = DIV_ROUND_UP(height_in_pix, ANV_MAX_H265_CTB_SIZE);
         uint32_t slice_block_cols = DIV_ROUND_UP(width_in_pix, ANV_MAX_H265_CTB_SIZE);
         uint32_t num_ctu_in_slice = slice_block_cols * slice_block_rows;

         vdenc_walker.MBLCUStartYPosition = slice_header->slice_segment_address % ctb_w;
         vdenc_walker.NextSliceMBLCUStartXPosition = (slice_header->slice_segment_address + num_ctu_in_slice) / ctb_h;
         vdenc_walker.NextSliceMBStartYPosition = (slice_header->slice_segment_address + num_ctu_in_slice) / ctb_w;
         vdenc_walker.NextSliceMBLCUStartXPosition = (slice_header->slice_segment_address + num_ctu_in_slice) / ctb_h;
#if GFX_VERx10 < 125
         vdenc_walker.TileWidth = width_in_pix - 1;
         vdenc_walker.TileHeight = height_in_pix - 1;
#endif
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

#define AVP_BITSTREAM_BYTECOUNT_TILE_NOHEADER_REG 0x1C2B4C
#define AVP_BITSTREAM_BYTECOUNT_TILE_REG          0x1C2B48

static int32_t
anv_av1_relative_dist(int32_t m, uint32_t a, uint32_t b)
{
   if (!m)
      return 0;
   int32_t diff = (int32_t)a - (int32_t)b;
   return (diff & (m - 1)) - (diff & m);
}

static void
anv_av1_encode_video(struct anv_cmd_buffer *cmd, const VkVideoEncodeInfoKHR *enc_info)
{
   /* TODO: AV1 VDEnc encoding is only validated on DG2 (gfx 12.5) for now */
#if GFX_VERx10 == 125
   ANV_FROM_HANDLE(anv_buffer, dst_buffer, enc_info->dstBuffer);
   struct anv_video_session *vid = cmd->video.vid;
   struct vk_video_session_parameters *params = cmd->video.params;

   const struct VkVideoEncodeAV1PictureInfoKHR *frame_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_AV1_PICTURE_INFO_KHR);

   const StdVideoEncodeAV1PictureInfo *pic_info = frame_info->pStdPictureInfo;
   const StdVideoAV1SequenceHeader *seq_hdr = &params->av1_enc.seq_hdr.base;
   const bool is_10bit = seq_hdr->pColorConfig->BitDepth == 10;

   const struct anv_image_view *iv =
      anv_image_view_from_handle(enc_info->srcPictureResource.imageViewBinding);
   const struct anv_image *src_img = iv->image;

   const struct anv_image_view *recon_iv;
   uint32_t recon_array_layer;

   if (enc_info->pSetupReferenceSlot) {
      recon_iv = anv_image_view_from_handle(enc_info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
      recon_array_layer = enc_info->pSetupReferenceSlot->pPictureResource->baseArrayLayer;
   } else {
      recon_iv = iv;
      recon_array_layer = enc_info->srcPictureResource.baseArrayLayer;
   }

   const struct anv_image *recon_img = recon_iv->image;

   /* Reference name (LAST..ALTREF) to bound DPB slot image mapping.
    * Unset names fall back to the first valid reference.
    */
   const struct anv_image_view *ref_name_iv[STD_VIDEO_AV1_REFS_PER_FRAME] = { 0, };
   uint32_t ref_name_layer[STD_VIDEO_AV1_REFS_PER_FRAME] = { 0, };
   const struct anv_image_view *first_ref_iv = NULL;
   uint32_t first_ref_layer = 0;

   for (uint32_t name = 0; name < STD_VIDEO_AV1_REFS_PER_FRAME; name++) {
      int32_t slot = frame_info->referenceNameSlotIndices[name];
      if (slot < 0)
         continue;
      for (uint32_t i = 0; i < enc_info->referenceSlotCount; i++) {
         if (enc_info->pReferenceSlots[i].slotIndex != slot)
            continue;
         ref_name_iv[name] = anv_image_view_from_handle(
            enc_info->pReferenceSlots[i].pPictureResource->imageViewBinding);
         ref_name_layer[name] =
            enc_info->pReferenceSlots[i].pPictureResource->baseArrayLayer;
         if (!first_ref_iv) {
            first_ref_iv = ref_name_iv[name];
            first_ref_layer = ref_name_layer[name];
         }
         break;
      }
   }

   bool rc_disable = vid->rc_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;

   bool is_intra = pic_info->frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY ||
                   pic_info->frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY;
   uint32_t base_q_idx = rc_disable ? frame_info->constantQIndex :
      (pic_info->pQuantization ? pic_info->pQuantization->base_q_idx : 0);

   if (base_q_idx == 0) base_q_idx = 1;
   bool reference_select = !is_intra &&
      frame_info->predictionMode >= VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR;

   const struct anv_image_view *primary_ref_iv = NULL;
   uint32_t primary_ref_layer = 0;
   if (!is_intra && pic_info->primary_ref_frame < STD_VIDEO_AV1_PRIMARY_REF_NONE) {
      if (ref_name_iv[pic_info->primary_ref_frame]) {
         primary_ref_iv = ref_name_iv[pic_info->primary_ref_frame];
         primary_ref_layer = ref_name_layer[pic_info->primary_ref_frame];
      } else {
         primary_ref_iv = first_ref_iv;
         primary_ref_layer = first_ref_layer;
      }
   }

   bool low_delay = true;
   uint32_t num_ref_l0 = 0, num_ref_l1 = 0;
   int8_t poc_l0[4] = { 1, 2, 3, 4 };
   int8_t poc_l1[4] = { -1, -2, -3, -4 };
   uint8_t frame_idx_l0[3] = { 0, 0, 0 };
   uint8_t frame_idx_l1 = 0;

   if (is_intra) {
      poc_l0[0] = poc_l0[1] = poc_l0[2] = 0;
      poc_l1[0] = 0;
   } else {
      frame_idx_l0[0] = frame_idx_l0[1] = frame_idx_l0[2] = 0x7;
      frame_idx_l1 = 0x7;

      int32_t m = seq_hdr->flags.enable_order_hint ?
         1 << seq_hdr->order_hint_bits_minus_1 : 0;
      uint32_t seen_slots = 0;
      for (uint32_t i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++) {
         int32_t slot = frame_info->referenceNameSlotIndices[i];
         if (slot < 0 || (seen_slots & (1u << slot)))
            continue;
         seen_slots |= 1u << slot;

         int32_t diff = (int32_t)pic_info->ref_order_hint[pic_info->ref_frame_idx[i]] -
                        (int32_t)pic_info->order_hint;
         if (m)
            diff = (diff & (m - 1)) - (diff & m);

         if (diff > 0) {
            low_delay = false;
            if (num_ref_l1 < 1) {
               poc_l1[0] = -diff;
               frame_idx_l1 = slot;
               num_ref_l1++;
            }
         } else if (num_ref_l0 < 3) {
            poc_l0[num_ref_l0] = -diff;
            frame_idx_l0[num_ref_l0] = slot;
            num_ref_l0++;
         }
      }
   }

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.VideoPipelineCacheInvalidate = 1;
   }

   anv_batch_emit(&cmd->batch, GENX(MI_FORCE_WAKEUP), wake) {
      /* TODO: confirm AV1/VDEnc power well control bits */
      wake.MFXPowerWellControl = 1;
      wake.MaskBits = 768;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_CONTROL_STATE), v) {
      v.VdencInitialization = true;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_PIPE_MODE_SELECT), vdenc_pipe_mode) {
      vdenc_pipe_mode.StandardSelect = 3; /* TODO: SS_AV1 */
      vdenc_pipe_mode.BitDepth = is_10bit ? 2 : 0;
      vdenc_pipe_mode.PAKChromaSubSamplingType = _420;
      vdenc_pipe_mode.OutputRangeControlAfterColorSpaceConversion = true;
      vdenc_pipe_mode.HMERegionPrefetchEnable = !vdenc_pipe_mode.TLBPrefetchEnable;
      vdenc_pipe_mode.TopPrefetchEnableMode = 0;
      vdenc_pipe_mode.LeftPrefetchAtWrapAround = true;
      vdenc_pipe_mode.VerticalShift32Minus1 = 2;
      vdenc_pipe_mode.HzShift32Minus1 = 3;
      vdenc_pipe_mode.NumberofVerticalRequests = 6;
      vdenc_pipe_mode.NumberofHorizontalRequests = 2;
      vdenc_pipe_mode.SourceLumaPackedDataTLBPrefetchEnable = false;
      vdenc_pipe_mode.SourceChromaTLBPrefetchEnable = false;
      vdenc_pipe_mode.HzShift32Minus1Src = 0;
      vdenc_pipe_mode.PrefetchOffsetforSource = 0;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_SRC_SURFACE_STATE), vdenc_surface) {
      vdenc_surface.SurfaceState.Width = src_img->vk.extent.width - 1;
      vdenc_surface.SurfaceState.Height = src_img->vk.extent.height - 1;
      vdenc_surface.SurfaceState.Colorspaceselection = 1;
      vdenc_surface.SurfaceState.SurfaceFormat = is_10bit ? VDENC_P010 : VDENC_PLANAR_420_8;
      vdenc_surface.SurfaceState.TileWalk = TW_YMAJOR;
      vdenc_surface.SurfaceState.TiledSurface = src_img->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
      vdenc_surface.SurfaceState.SurfacePitch = src_img->planes[0].primary_surface.isl.row_pitch_B - 1;
      vdenc_surface.SurfaceState.ChromaDownsampleFilterControl = 7;
      vdenc_surface.SurfaceState.YOffsetforUCb = src_img->planes[1].primary_surface.memory_range.offset /
         src_img->planes[0].primary_surface.isl.row_pitch_B;
      vdenc_surface.SurfaceState.YOffsetforVCr = src_img->planes[1].primary_surface.memory_range.offset /
         src_img->planes[0].primary_surface.isl.row_pitch_B;
   }

   anv_batch_emit(&cmd->batch, GENX(VDENC_REF_SURFACE_STATE), vdenc_surface) {
      vdenc_surface.SurfaceState.Width = recon_img->vk.extent.width - 1;
      vdenc_surface.SurfaceState.Height = recon_img->vk.extent.height - 1;
      vdenc_surface.SurfaceState.SurfaceFormat = is_10bit ? VDENC_P010 : VDENC_PLANAR_420_8;
      vdenc_surface.SurfaceState.TileWalk = TW_YMAJOR;
      vdenc_surface.SurfaceState.TiledSurface = recon_img->planes[0].primary_surface.isl.tiling != ISL_TILING_LINEAR;
      vdenc_surface.SurfaceState.SurfacePitch = recon_img->planes[0].primary_surface.isl.row_pitch_B - 1;
      vdenc_surface.SurfaceState.YOffsetforUCb = recon_img->planes[1].primary_surface.memory_range.offset /
         recon_img->planes[0].primary_surface.isl.row_pitch_B;
      vdenc_surface.SurfaceState.YOffsetforVCr = recon_img->planes[1].primary_surface.memory_range.offset /
         recon_img->planes[0].primary_surface.isl.row_pitch_B;
   }

   /* TODO: VDENC_DS_REF_SURFACE_STATE (8X/4X downscaled) - needs DS scratch surface allocation */

   anv_batch_emit(&cmd->batch, GENX(VDENC_PIPE_BUF_ADDR_STATE), vdenc_buf) {
      vdenc_buf.DSFWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.DSFWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.DSBWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.OriginalUncompressedPicture.Address =
         anv_image_dpb_address(iv, enc_info->srcPictureResource.baseArrayLayer);
      vdenc_buf.OriginalUncompressedPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.OriginalUncompressedPicture.Address.bo, 0),
      };

      vdenc_buf.StreamInDataPicture.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };

      vdenc_buf.RowStoreScratchBuffer.Address = (struct anv_address) { NULL, 0x25080 };
      vdenc_buf.RowStoreScratchBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
         .CacheSelect = 1,
      };

      if (primary_ref_iv) {
         vdenc_buf.ColocatedMVReadBuffer.Address =
               anv_image_dmv_top_address(primary_ref_iv, primary_ref_layer);
      }
      vdenc_buf.ColocatedMVReadBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vdenc_buf.ColocatedMVReadBuffer.Address.bo, 0),
      };
      vdenc_buf.FWDREF0.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.FWDREF1.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
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
      vdenc_buf.DSFWDREF04X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.DSFWDREF14X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.DSBWDREF04X.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
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

      vdenc_buf.IntraPredictionRowStoreBuffer.Address = (struct anv_address) {
         vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_LINE_ROWSTORE].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_LINE_ROWSTORE].offset
      };
      vdenc_buf.IntraPredictionRowStoreBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_LINE_ROWSTORE].mem->bo, 0),
      };

      vdenc_buf.ColocatedMVAVCWriteBuffer.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
      vdenc_buf.Additional4XDSFWDREF.PictureFields = (struct GENX(VDENC_SURFACE_CONTROL_BITS)) {
         .MOCS = anv_mocs(cmd->device, NULL, 0),
      };
   }

   /* Multi-tile scaffold (phase 1): the per-tile block runs once for now.
    * Per-tile geometry / bitstream tile sizes / PAK readback come later.
    * tg_leb128_addr is hoisted so the post-loop size patch still sees it.
    */
   struct anv_address tg_leb128_addr;
   uint32_t tg_payload_prefix = 0;
   const StdVideoAV1TileInfo *ti = pic_info->pTileInfo;
   uint32_t num_tile_cols = ti ? ti->TileCols : 1;
   uint32_t num_tile_rows = ti ? ti->TileRows : 1;
   /* AV1 uniform tiling (5.9.15): the requested TileCols/TileRows may not
    * divide the frame evenly, so the actual tile count derived by the decoder
    * (tileWidthSb = ceil(sb / 2^log2); num = ceil(sb / tileWidthSb)) can be
    * smaller. Recompute it so we never emit a zero-width/height tail tile. */
   if (!ti || ti->flags.uniform_tile_spacing_flag) {
      uint32_t sb_sz = seq_hdr->flags.use_128x128_superblock ? 128 : 64;
      uint32_t pw_sb = DIV_ROUND_UP(src_img->vk.extent.width, sb_sz);
      uint32_t ph_sb = DIV_ROUND_UP(src_img->vk.extent.height, sb_sz);
      uint32_t cw = 1, ch = 1;
      while (cw < num_tile_cols) cw <<= 1;
      while (ch < num_tile_rows) ch <<= 1;
      num_tile_cols = DIV_ROUND_UP(pw_sb, DIV_ROUND_UP(pw_sb, cw));
      num_tile_rows = DIV_ROUND_UP(ph_sb, DIV_ROUND_UP(ph_sb, ch));
   }

   uint32_t num_tiles = num_tile_cols * num_tile_rows;
   uint32_t cu_tile_offset = 0;
   uint32_t lcu_tile_offset = 0;

   for (uint32_t tile_idx = 0; tile_idx < num_tiles; tile_idx++) {
      anv_batch_emit(&cmd->batch, GENX(AVP_VD_CONTROL_STATE), v) {
         v.VDControlState.PipelineInitialization = true;
      }

      anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
         mfx.MFXSyncControlFlag = 1;
      }

      anv_batch_emit(&cmd->batch, GENX(AVP_PIPE_MODE_SELECT), avp_pipe_mode) {
         avp_pipe_mode.CodecSelect = Encode;
         avp_pipe_mode.VDENCMode = true;
         avp_pipe_mode.FrameReconstructionDisable =
            pic_info->refresh_frame_flags == 0;
         avp_pipe_mode.DownscaledSourcePixelPrefetchLength = 4;
         avp_pipe_mode.DownscaledSourcePixelPrefetchEnable = true;
      }

      anv_batch_emit(&cmd->batch, GENX(MFX_WAIT), mfx) {
         mfx.MFXSyncControlFlag = 1;
      }

      for (uint32_t i = 0; i < 4; i++) {
         anv_batch_emit(&cmd->batch, GENX(AVP_SURFACE_STATE), avp_surface) {
            const struct anv_image *img_ = NULL;
            switch (i) {
            case 0:
               img_ = src_img;
               avp_surface.SurfaceID = 1;
               break;
            case 1:
               img_ = src_img;
               avp_surface.SurfaceID = 3;
               break;
            case 2:
               img_ = recon_img;
               avp_surface.SurfaceID = 0;
               break;
            case 3:
               img_ = recon_img;
               avp_surface.SurfaceID = 15;
               break;
            default:
               assert(0);
            }

            avp_surface.SurfacePitchMinus1 = img_->planes[0].primary_surface.isl.row_pitch_B - 1;
            avp_surface.SurfaceFormat = is_10bit ? AVP_P010 : AVP_PLANAR_420_8;
            avp_surface.YOffsetforUCb = img_->planes[1].primary_surface.memory_range.offset /
               img_->planes[0].primary_surface.isl.row_pitch_B;
            avp_surface.YOffsetforVCr = avp_surface.YOffsetforUCb;
         }
      }

      if (!is_intra) {
         for (uint32_t r = 0; r < 8; r++) {
            anv_batch_emit(&cmd->batch, GENX(AVP_SURFACE_STATE), avp_surface) {
               const struct anv_image *img_;
               if (r > 0 && ref_name_iv[r - 1])
                  img_ = ref_name_iv[r - 1]->image;
               else if (r > 0 && first_ref_iv)
                  img_ = first_ref_iv->image;
               else
                  img_ = recon_img;

               avp_surface.SurfaceID = 6 + r;
               avp_surface.SurfacePitchMinus1 = img_->planes[0].primary_surface.isl.row_pitch_B - 1;
               avp_surface.SurfaceFormat = is_10bit ? AVP_P010 : AVP_PLANAR_420_8;
               avp_surface.YOffsetforUCb = img_->planes[1].primary_surface.memory_range.offset /
                  img_->planes[0].primary_surface.isl.row_pitch_B;
               avp_surface.YOffsetforVCr = avp_surface.YOffsetforUCb;
            }
         }
      }

      anv_batch_emit(&cmd->batch, GENX(AVP_PIPE_BUF_ADDR_STATE), buf) {
         buf.DecodedOutputFrameBufferAddress =
            anv_image_dpb_address(recon_iv, recon_array_layer);
         buf.DecodedOutputFrameBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.DecodedOutputFrameBufferAddress.bo, 0),
            .TiledResourceMode = TRMODE_TILEF,
         };

         buf.CurrentFrameMVWriteBufferAddress =
            anv_image_dmv_top_address(recon_iv, recon_array_layer);
         buf.CurrentFrameMVWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.CurrentFrameMVWriteBufferAddress.bo, 0),
         };

         /* References by name: slot 0 is INTRA_FRAME (current recon), slots 1..7
          * are LAST..ALTREF. Collocated MV temporal buffers follow the same
          * layout with the current frame MV buffer.
          */
         if (!is_intra) {
            buf.ReferencePictureAddress[0] =
               anv_image_dpb_address(recon_iv, recon_array_layer);

            for (uint32_t name = 0; name < STD_VIDEO_AV1_REFS_PER_FRAME; name++) {
               const struct anv_image_view *r_iv =
                  ref_name_iv[name] ? ref_name_iv[name] : first_ref_iv;
               uint32_t r_layer =
                  ref_name_iv[name] ? ref_name_layer[name] : first_ref_layer;

               if (r_iv)
                  buf.ReferencePictureAddress[1 + name] =
                     anv_image_dpb_address(r_iv, r_layer);
               else
                  buf.ReferencePictureAddress[1 + name] =
                     anv_image_dpb_address(recon_iv, recon_array_layer);
            }
         }

         buf.ReferencePictureAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.ReferencePictureAddress[0].bo, 0),
            .TiledResourceMode = TRMODE_TILEF,
         };

         buf.CollocatedMVTemporalBufferAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };

         if (pic_info->flags.allow_intrabc) {
            buf.IntraBCDecodedOutputFrameBufferAddress =
               anv_image_dpb_address(recon_iv, recon_array_layer);
         }
         buf.IntraBCDecodedOutputFrameBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.IntraBCDecodedOutputFrameBufferAddress.bo, 0),
            .TiledResourceMode = TRMODE_TILEF,
         };

         buf.BitstreamLineRowstoreBufferAddress = (struct anv_address) { NULL, 0 };
         buf.BitstreamLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
         buf.IntraPredictionLineRowstoreBufferAddress = (struct anv_address) { NULL, 0x6000 };
         buf.IntraPredictionLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };

         buf.SpatialMotionVectorLineBufferAddress = (struct anv_address) { NULL, 0x2000 };
         buf.SpatialMotionVectorLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
         buf.DeblockerFilterLineYBufferAddress = (struct anv_address) { NULL, 0xa000 };
         buf.DeblockerFilterLineYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
         buf.DeblockerFilterLineUBufferAddress = (struct anv_address) { NULL, 0x15000 };
         buf.DeblockerFilterLineUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
         buf.DeblockerFilterLineVBufferAddress = (struct anv_address) { NULL, 0x18000 };
         buf.DeblockerFilterLineVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
         buf.CDEFFilterLineBufferAddress = (struct anv_address) { NULL, 0x1b000 };
         buf.CDEFFilterLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
            .RowStoreScratchBufferCacheSelect = 1,
         };
         buf.BitstreamTileLineRowstoreBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_TILE_LINE_ROWSTORE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_TILE_LINE_ROWSTORE].offset
         };
         buf.BitstreamTileLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_BITSTREAM_TILE_LINE_ROWSTORE].mem->bo, 0),
         };
         buf.IntraPredictionTileLineRowstoreBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_TILE_LINE_ROWSTORE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_TILE_LINE_ROWSTORE].offset
         };
         buf.IntraPredictionTileLineRowstoreBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_INTRA_PREDICTION_TILE_LINE_ROWSTORE].mem->bo, 0),
         };
         buf.SpatialMotionVectorTileLineBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_TILE_LINE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_TILE_LINE].offset
         };
         buf.SpatialMotionVectorTileLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_SPATIAL_MOTION_VECTOR_TILE_LINE].mem->bo, 0),
         };
         buf.DeblockerFilterTileLineYBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_Y].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_Y].offset
         };
         buf.DeblockerFilterTileLineYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_Y].mem->bo, 0),
         };
         buf.DeblockerFilterTileLineUBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_U].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_U].offset
         };
         buf.DeblockerFilterTileLineUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_U].mem->bo, 0),
         };
         buf.DeblockerFilterTileLineVBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_V].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_V].offset
         };
         buf.DeblockerFilterTileLineVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_LINE_V].mem->bo, 0),
         };
         buf.DeblockerFilterTileColumnYBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_Y].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_Y].offset
         };
         buf.DeblockerFilterTileColumnYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_Y].mem->bo, 0),
         };
         buf.DeblockerFilterTileColumnUBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_U].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_U].offset
         };
         buf.DeblockerFilterTileColumnUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_U].mem->bo, 0),
         };
         buf.DeblockerFilterTileColumnVBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_V].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_V].offset
         };
         buf.DeblockerFilterTileColumnVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_DEBLOCKER_FILTER_TILE_COLUMN_V].mem->bo, 0),
         };
         buf.CDEFFilterTileLineBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_LINE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_LINE].offset
         };
         buf.CDEFFilterTileLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_LINE].mem->bo, 0),
         };
         buf.CDEFFilterTileColumnBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_COLUMN].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_COLUMN].offset
         };
         buf.CDEFFilterTileColumnBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TILE_COLUMN].mem->bo, 0),
         };
         buf.CDEFFilterMetaTileLineBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_LINE].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_LINE].offset
         };
         buf.CDEFFilterMetaTileLineBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_LINE].mem->bo, 0),
         };
         buf.CDEFFilterMetaTileColumnBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_COLUMN].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_COLUMN].offset
         };
         buf.CDEFFilterMetaTileColumnBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_META_TILE_COLUMN].mem->bo, 0),
         };
         buf.CDEFFilterTopLeftCornerBufferAddress = (struct anv_address) {
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TOP_LEFT_CORNER].mem->bo,
            vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TOP_LEFT_CORNER].offset
         };
         buf.CDEFFilterTopLeftCornerBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_CDEF_FILTER_TOP_LEFT_CORNER].mem->bo, 0),
         };
         buf.LoopRestorationMetaTileColumnBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.LoopRestorationFilterTileLineYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.LoopRestorationFilterTileLineUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.LoopRestorationFilterTileLineVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.LoopRestorationFilterTileColumnYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.LoopRestorationFilterTileColumnUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.LoopRestorationFilterTileColumnVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.SuperResTileColumnYBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.SuperResTileColumnUBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.SuperResTileColumnVBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };

         assert(vid->cdf_initialized);

         if (primary_ref_iv && !pic_info->flags.disable_frame_end_update_cdf) {
            buf.CDFTablesInitializationBufferAddress =
               anv_image_av1_table_address(primary_ref_iv, primary_ref_layer);
         } else {
            uint32_t cdf_qindex = rc_disable ? frame_info->constantQIndex :
               (pic_info->pQuantization ? pic_info->pQuantization->base_q_idx : 0);
            uint32_t cdf_index = cdf_qindex <= 20 ? 0 :
                                 cdf_qindex <= 60 ? 1 :
                                 cdf_qindex <= 120 ? 2 : 3;
            buf.CDFTablesInitializationBufferAddress = (struct anv_address) {
               vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + cdf_index].mem->bo,
               vid->vid_mem[ANV_VID_MEM_AV1_CDF_DEFAULTS_0 + cdf_index].offset
            };
         }
         buf.CDFTablesInitializationBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.CDFTablesInitializationBufferAddress.bo, 0),
         };
         buf.CDFTablesBackwardAdaptationBufferAddress =
            anv_image_av1_table_address(recon_iv, recon_array_layer);
         buf.CDFTablesBackwardAdaptationBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.CDFTablesBackwardAdaptationBufferAddress.bo, 0),
         };
         buf.DecodedBlockDataStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.OriginalUncompressedPictureSourceBufferAddress =
            anv_image_dpb_address(iv, enc_info->srcPictureResource.baseArrayLayer);
         buf.OriginalUncompressedPictureSourceBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.OriginalUncompressedPictureSourceBufferAddress.bo, 0),
         };
         buf.DownscaledUncompressedPictureSourceBufferAddress =
            anv_image_dpb_address(iv, enc_info->srcPictureResource.baseArrayLayer);
         buf.DownscaledUncompressedPictureSourceBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, buf.DownscaledUncompressedPictureSourceBufferAddress.bo, 0),
         };
         buf.AV1SegmentIDReadBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.AV1SegmentIDWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.DecodedFrameStatusErrorBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.TileSizeStreamoutBufferAddress = anv_address_add(
            (struct anv_address) {
               vid->vid_mem[ANV_VID_MEM_AV1_TILE_SIZE_STREAMOUT].mem->bo,
               vid->vid_mem[ANV_VID_MEM_AV1_TILE_SIZE_STREAMOUT].offset
            }, tile_idx * 64);
         buf.TileSizeStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, vid->vid_mem[ANV_VID_MEM_AV1_TILE_SIZE_STREAMOUT].mem->bo, 0),
         };
         buf.TileStatisticsStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.CUStreamoutBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.SSELineReadWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.SSETileLineReadWriteBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
         buf.PostCDEFPixelsBufferAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
      }

      anv_batch_emit(&cmd->batch, GENX(AVP_IND_OBJ_BASE_ADDR_STATE), ind) {
         ind.AVPIndirectBitstreamObjectBaseAddress =
            anv_address_add(dst_buffer->address, enc_info->dstBufferOffset & ~4095);

         ind.AVPIndirectBitstreamObjectAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, dst_buffer->address.bo, 0),
         };

         ind.AVPIndirectCUObjectMemoryAddressAttributes = (struct GENX(MEMORYADDRESSATTRIBUTES)) {
            .MOCS = anv_mocs(cmd->device, NULL, 0),
         };
      }

      const StdVideoAV1Quantization *quant = pic_info->pQuantization;
      bool coded_lossless = base_q_idx == 0 &&
         (!quant || (quant->DeltaQYDc == 0 &&
                     quant->DeltaQUDc == 0 && quant->DeltaQUAc == 0 &&
                     quant->DeltaQVDc == 0 && quant->DeltaQVAc == 0));
      bool intra_only = pic_info->frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY ||
                        pic_info->frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY;
      uint32_t frame_width = src_img->vk.extent.width;
      uint32_t frame_height = src_img->vk.extent.height;

      anv_batch_emit(&cmd->batch, GENX(AVP_PIC_STATE), pic) {
         pic.FrameWidth = frame_width - 1;
         pic.FrameHeight = frame_height - 1;

         if (num_tiles > 1) {
            pic.EnableBitstreamStitchingInHardware = true;
            pic.MinFrameSize = DIV_ROUND_UP(13 * 64, 16);
            pic.MinFrameSizeUnits = 3;
         }

         if (seq_hdr->pColorConfig->BitDepth == 12)
            pic.SequencePixelBitDepthIdc = SeqPix_12bit;
         else if (seq_hdr->pColorConfig->BitDepth == 10)
            pic.SequencePixelBitDepthIdc = SeqPix_10bit;
         else
            pic.SequencePixelBitDepthIdc = SeqPix_8bit;
         pic.SequenceChromaSubSamplingFormat = SS_420;

         pic.SequenceSuperblockSizeUsed = seq_hdr->flags.use_128x128_superblock;
         pic.SequenceEnableOrderHintFlag = seq_hdr->flags.enable_order_hint;
         pic.SequenceOrderHintBitsMinus1 = seq_hdr->flags.enable_order_hint ? seq_hdr->order_hint_bits_minus_1 : 0;
         pic.SequenceEnableFilterIntraFlag = seq_hdr->flags.enable_filter_intra;
         pic.SequenceEnableIntraEdgeFilterFlag = seq_hdr->flags.enable_intra_edge_filter;
         pic.SequenceEnableDualFilterFlag = seq_hdr->flags.enable_dual_filter;
         pic.SequenceEnableInterIntraCompoundFlag = seq_hdr->flags.enable_interintra_compound;
         pic.SequenceEnableMaskedCompoundFlag = seq_hdr->flags.enable_masked_compound;
         pic.SequenceEnableJointCompoundFlag = seq_hdr->flags.enable_jnt_comp;
         pic.HeaderPresentFlag = tile_idx == 0;
         pic.AllowScreenContentToolsFlag = pic_info->flags.allow_screen_content_tools;
         pic.ForceIntegerMVFlag = pic_info->flags.force_integer_mv;
         pic.AllowWarpedMotionFlag = pic_info->flags.allow_warped_motion;
         pic.UseCDEFFilterFlag = seq_hdr->flags.enable_cdef && !pic_info->flags.allow_intrabc &&
                                 !coded_lossless;
         pic.UseSuperResFlag = pic_info->flags.use_superres;

         /* Loop restoration is not supported by the encoder HW, so keep it disabled. */
         pic.FrameLevelLoopRestorationFilterEnable = false;
         pic.PostCDEFFilteredReconPixelsWriteoutEnable = true;
         pic.FrameType = pic_info->frame_type;
         pic.IntraOnlyFlag = intra_only;
         pic.ErrorResilientModeFlag = pic_info->flags.error_resilient_mode;
         pic.AllowIntraBCFlag = pic_info->flags.allow_intrabc;
         pic.PrimaryReferenceFrameIdx =
            (pic_info->frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY ||
             pic_info->frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY) ?
             7 : pic_info->primary_ref_frame;

         pic.SegmentationEnableFlag = pic_info->flags.segmentation_enabled;
         pic.SegmentationUpdateMapFlag = pic_info->flags.segmentation_update_map;
         pic.SegmentationTemporalUpdateFlag = intra_only ? 0 : pic_info->flags.segmentation_temporal_update;
         pic.DeltaQPresentFlag = pic_info->flags.delta_q_present;
         pic.DeltaQRes = pic_info->delta_q_res;
         pic.FrameCodedLosslessMode = coded_lossless;

         pic.BaseQindex = base_q_idx;
         pic.YdcdeltaQ = quant ? quant->DeltaQYDc : 0;
         pic.UdcdeltaQ = quant ? quant->DeltaQUDc : 0;
         pic.UacdeltaQ = quant ? quant->DeltaQUAc : 0;
         pic.VdcdeltaQ = quant ? quant->DeltaQVDc : 0;
         pic.VacdeltaQ = quant ? quant->DeltaQVAc : 0;

         pic.AllowHighPrecisionMV = pic_info->flags.allow_high_precision_mv;
         pic.McompFilterType = pic_info->interpolation_filter;
         pic.MotionModeSwitchableFlag = pic_info->flags.is_motion_mode_switchable;
         pic.FrameLevelReferenceModeSelect = reference_select;
         pic.UseReferenceFrameMVSetFlag = pic_info->flags.use_ref_frame_mvs &&
                                          seq_hdr->flags.enable_order_hint;
         pic.CurrentFrameOrderHint = pic_info->order_hint;
         pic.ReducedTxSetUsed = pic_info->flags.reduced_tx_set;
         pic.FrameTransformMode = coded_lossless ? STD_VIDEO_AV1_TX_MODE_ONLY_4X4 :
            (pic_info->TxMode == STD_VIDEO_AV1_TX_MODE_LARGEST ?
             STD_VIDEO_AV1_TX_MODE_LARGEST : STD_VIDEO_AV1_TX_MODE_SELECT);
         pic.SkipModePresentFlag = pic_info->flags.skip_mode_present;

         if (pic_info->pGlobalMotion) {
            pic.GlobalMotionType1 = pic_info->pGlobalMotion->GmType[1];
            pic.GlobalMotionType2 = pic_info->pGlobalMotion->GmType[2];
            pic.GlobalMotionType3 = pic_info->pGlobalMotion->GmType[3];
            pic.GlobalMotionType4 = pic_info->pGlobalMotion->GmType[4];
            pic.GlobalMotionType5 = pic_info->pGlobalMotion->GmType[5];
            pic.GlobalMotionType6 = pic_info->pGlobalMotion->GmType[6];
            pic.GlobalMotionType7 = pic_info->pGlobalMotion->GmType[7];
         }

         if (!is_intra) {
            pic.ReferenceFrameIdx1 = pic_info->ref_frame_idx[0];
            pic.ReferenceFrameIdx2 = pic_info->ref_frame_idx[1];
            pic.ReferenceFrameIdx3 = pic_info->ref_frame_idx[2];
            pic.ReferenceFrameIdx4 = pic_info->ref_frame_idx[3];
            pic.ReferenceFrameIdx5 = pic_info->ref_frame_idx[4];
            pic.ReferenceFrameIdx6 = pic_info->ref_frame_idx[5];
            pic.ReferenceFrameIdx7 = pic_info->ref_frame_idx[6];
         }

         /* Per-reference order hint / side / sign bias:
          * a keyframe treats every reference as having the current order hint,
          * which makes ReferenceFrameSide 0xfe. Same order hint or a later one
          * sets the side bit; only a later one sets the sign bias bit.
          * TODO: per-ref dims/scale factors for references of other sizes. */
         int32_t dist_m = seq_hdr->flags.enable_order_hint ?
            1 << seq_hdr->order_hint_bits_minus_1 : 0;
         uint32_t ref_order_hints[8];
         uint32_t ref_frame_side = 0;
         uint32_t ref_sign_bias = 0;
         ref_order_hints[0] = pic_info->order_hint;
         for (uint32_t name = 0; name < STD_VIDEO_AV1_REFS_PER_FRAME; name++) {
            uint32_t hint = is_intra ? pic_info->order_hint :
               pic_info->ref_order_hint[pic_info->ref_frame_idx[name] & 7];
            ref_order_hints[1 + name] = hint;
            if (!is_intra && frame_info->referenceNameSlotIndices[name] < 0)
               continue;
            int32_t dist = anv_av1_relative_dist(dist_m, hint, pic_info->order_hint);
            if (dist > 0 || hint == pic_info->order_hint)
               ref_frame_side |= 1u << (1 + name);
            if (dist > 0)
               ref_sign_bias |= 1u << (1 + name);
         }
         for (uint32_t r = 0; r < 8; r++)
            pic.ReferenceFrameOrderHint[r] = ref_order_hints[r];
         pic.ReferenceFrameSignBias = ref_sign_bias;

         pic.IntraFrameWidthinPixelMinus1 = frame_width - 1;
         pic.IntraFrameHeightinPixelMinus1 = frame_height - 1;
         pic.LastFrameWidthinPixelMinus1 = frame_width - 1;
         pic.LastFrameHeightinPixelMinus1 = frame_height - 1;
         pic.Last2FrameWidthinPixelMinus1 = frame_width - 1;
         pic.Last2FrameHeightinPixelMinus1 = frame_height - 1;
         pic.Last3FrameWidthinPixelMinus1 = frame_width - 1;
         pic.Last3FrameHeightinPixelMinus1 = frame_height - 1;
         pic.GoldenFrameWidthinPixelMinus1 = frame_width - 1;
         pic.GoldenFrameHeightinPixelMinus1 = frame_height - 1;
         pic.BWDREFFrameWidthinPixelMinus1 = frame_width - 1;
         pic.BWDREFFrameHeightinPixelMinus1 = frame_height - 1;
         pic.ALTREF2FrameWidthinPixelMinus1 = frame_width - 1;
         pic.ALTREF2FrameHeightinPixelMinus1 = frame_height - 1;
         pic.ALTREFFrameWidthinPixelMinus1 = frame_width - 1;
         pic.ALTREFFrameHeightinPixelMinus1 = frame_height - 1;

         pic.VerticalScaleFactorForIntra = 16384;
         pic.HorizontalScaleFactorForIntra = 16384;
         pic.VerticalScaleFactorForLast = 16384;
         pic.HorizontalScaleFactorForLast = 16384;
         pic.VerticalScaleFactorForLast2 = 16384;
         pic.HorizontalScaleFactorForLast2 = 16384;
         pic.VerticalScaleFactorForLast3 = 16384;
         pic.HorizontalScaleFactorForLast3 = 16384;
         pic.VerticalScaleFactorForGolden = 16384;
         pic.HorizontalScaleFactorForGolden = 16384;
         pic.VerticalScaleFactorForBWDREF = 16384;
         pic.HorizontalScaleFactorForBWDREF = 16384;
         pic.VerticalScaleFactorForALTREF2 = 16384;
         pic.HorizontalScaleFactorForALTREF2 = 16384;
         pic.VerticalScaleFactorForALTREF = 16384;
         pic.HorizontalScaleFactorForALTREF = 16384;
         pic.ReferenceFrameSide = ref_frame_side;

         if (!is_intra && !low_delay && pic_info->flags.skip_mode_present) {
            int32_t fwd_hint = -1, bwd_hint = -1;
            int32_t fwd_idx = -1, bwd_idx = -1;
            for (uint32_t name = 0; name < STD_VIDEO_AV1_REFS_PER_FRAME; name++) {
               if (frame_info->referenceNameSlotIndices[name] < 0)
                  continue;
               uint32_t hint = ref_order_hints[1 + name];
               if (anv_av1_relative_dist(dist_m, hint, pic_info->order_hint) < 0) {
                  if (fwd_idx < 0 ||
                      anv_av1_relative_dist(dist_m, hint, fwd_hint) > 0) {
                     fwd_idx = 1 + name;
                     fwd_hint = hint;
                  }
               } else if (anv_av1_relative_dist(dist_m, hint, pic_info->order_hint) > 0) {
                  if (bwd_idx < 0 ||
                      anv_av1_relative_dist(dist_m, hint, bwd_hint) < 0) {
                     bwd_idx = 1 + name;
                     bwd_hint = hint;
                  }
               }
            }
            if (fwd_idx >= 0 && bwd_idx >= 0) {
               pic.SkipModeFrame0 = MIN2(fwd_idx, bwd_idx);
               pic.SkipModeFrame1 = MAX2(fwd_idx, bwd_idx);
            } else if (fwd_idx >= 0) {
               int32_t snd_idx = -1, snd_hint = -1;
               for (uint32_t name = 0; name < STD_VIDEO_AV1_REFS_PER_FRAME; name++) {
                  if (frame_info->referenceNameSlotIndices[name] < 0)
                     continue;
                  uint32_t hint = ref_order_hints[1 + name];
                  if (anv_av1_relative_dist(dist_m, hint, fwd_hint) < 0 &&
                      (snd_idx < 0 ||
                       anv_av1_relative_dist(dist_m, hint, snd_hint) > 0)) {
                     snd_idx = 1 + name;
                     snd_hint = hint;
                  }
               }
               if (snd_idx >= 0) {
                  pic.SkipModeFrame0 = MIN2(fwd_idx, snd_idx);
                  pic.SkipModeFrame1 = MAX2(fwd_idx, snd_idx);
               }
            }
         }
      }

      /* SavedOrderHints / ActiveReferenceBitmask are only consumed for motion
       * field projection (use_ref_frame_mvs);
       * The encode std headers carry no per-reference saved order hints,
       * so supporting use_ref_frame_mvs=1 needs DPB slot state tracking
       * in the session.
       * TODO: SavedOrderHints + ActiveReferenceBitmask for use_ref_frame_mvs. */
      anv_batch_emit(&cmd->batch, GENX(AVP_INTER_PRED_STATE), inter) {
      }

      anv_batch_emit(&cmd->batch, GENX(AVP_SEGMENT_STATE), seg) {
         seg.SegmentID = 0;
         seg.SegmentLosslessFlag = coded_lossless;
         /* TODO: per-segment features when segmentation_enabled */
         if (!quant || !quant->flags.using_qmatrix) {
            seg.SegmentLumaYQMLevel = 15;
            seg.SegmentChromaUQMLevel = 15;
            seg.SegmentChromaVQMLevel = 15;
         } else {
            seg.SegmentLumaYQMLevel = quant->qm_y;
            seg.SegmentChromaUQMLevel = quant->qm_u;
            seg.SegmentChromaVQMLevel = quant->qm_v;
         }
      }

      const StdVideoAV1LoopFilter *lf = pic_info->pLoopFilter;
      const StdVideoAV1CDEF *cdef = pic_info->pCDEF;
      uint32_t cdef_y_strength[8] = { 0, }, cdef_uv_strength[8] = { 0, };
      if (cdef) {
         for (unsigned i = 0; i < (1u << cdef->cdef_bits); i++) {
            cdef_y_strength[i] = (cdef->cdef_y_pri_strength[i] << 2) + cdef->cdef_y_sec_strength[i];
            cdef_uv_strength[i] = (cdef->cdef_uv_pri_strength[i] << 2) + cdef->cdef_uv_sec_strength[i];
         }
      }

      anv_batch_emit(&cmd->batch, GENX(AVP_INLOOP_FILTER_STATE), fil) {
         if (lf) {
            fil.LumaYDeblockerFilterLevelVertical = lf->loop_filter_level[0];
            fil.LumaYDeblockerFilterLevelHorizontal = lf->loop_filter_level[1];
            fil.ChromaUDeblockerFilterLevel = lf->loop_filter_level[2];
            fil.ChromaVDeblockerFilterLevel = lf->loop_filter_level[3];
            fil.DeblockerFilterSharpnessLevel = lf->loop_filter_sharpness;
            fil.DeblockerFilterModeRefDeltaEnableFlag = lf->flags.loop_filter_delta_enabled;
            fil.DeblockerFilterRefDeltas0 = lf->loop_filter_ref_deltas[0];
            fil.DeblockerFilterRefDeltas1 = lf->loop_filter_ref_deltas[1];
            fil.DeblockerFilterRefDeltas2 = lf->loop_filter_ref_deltas[2];
            fil.DeblockerFilterRefDeltas3 = lf->loop_filter_ref_deltas[3];
            fil.DeblockerFilterRefDeltas4 = lf->loop_filter_ref_deltas[4];
            fil.DeblockerFilterRefDeltas5 = lf->loop_filter_ref_deltas[5];
            fil.DeblockerFilterRefDeltas6 = lf->loop_filter_ref_deltas[6];
            fil.DeblockerFilterRefDeltas7 = lf->loop_filter_ref_deltas[7];
            fil.DeblockerFilterModeDeltas0 = lf->loop_filter_mode_deltas[0];
            fil.DeblockerFilterModeDeltas1 = lf->loop_filter_mode_deltas[1];
         }
         fil.DeblockerDeltaLFResolution = pic_info->delta_lf_res;
         fil.DeblockerFilterDeltaLFMultiFlag = pic_info->flags.delta_lf_multi;
         fil.DeblockerFilterDeltaLFPresentFlag = pic_info->flags.delta_lf_present;

         fil.CDEFYStrength0 = cdef_y_strength[0];
         fil.CDEFYStrength1 = cdef_y_strength[1];
         fil.CDEFYStrength2 = cdef_y_strength[2];
         fil.CDEFYStrength3 = cdef_y_strength[3];
         fil.CDEFYStrength4 = cdef_y_strength[4];
         fil.CDEFYStrength5 = cdef_y_strength[5];
         fil.CDEFYStrength6 = cdef_y_strength[6];
         fil.CDEFYStrength7 = cdef_y_strength[7];
         fil.CDEFUVStrength0 = cdef_uv_strength[0];
         fil.CDEFUVStrength1 = cdef_uv_strength[1];
         fil.CDEFUVStrength2 = cdef_uv_strength[2];
         fil.CDEFUVStrength3 = cdef_uv_strength[3];
         fil.CDEFUVStrength4 = cdef_uv_strength[4];
         fil.CDEFUVStrength5 = cdef_uv_strength[5];
         fil.CDEFUVStrength6 = cdef_uv_strength[6];
         fil.CDEFUVStrength7 = cdef_uv_strength[7];
         if (cdef) {
            fil.CDEFBits = cdef->cdef_bits;
            fil.CDEFFilterDmpaingFactorMinus3 = cdef->cdef_damping_minus_3;
         }

         /* TODO: super-res and loop-restoration unit size when those tools are enabled */
      }

      uint32_t sb_size = seq_hdr->flags.use_128x128_superblock ? 128 : 64;
      uint32_t pic_width_in_sb = DIV_ROUND_UP(frame_width, sb_size);
      uint32_t pic_height_in_sb = DIV_ROUND_UP(frame_height, sb_size);

      uint32_t tile_col = tile_idx % num_tile_cols;
      uint32_t tile_row = tile_idx / num_tile_cols;
      uint32_t col_start_sb, row_start_sb, tile_w_sb, tile_h_sb;
      if (!ti || ti->flags.uniform_tile_spacing_flag) {
         /* Uniform spacing: pWidthInSbsMinus1/pHeightInSbsMinus1 may be NULL.
          * Tile size = ceil(pic_in_sb / next_pow2(tiles)) per AV1 5.9.15. */
         uint32_t cols_pow2 = 1, rows_pow2 = 1;
         while (cols_pow2 < num_tile_cols) cols_pow2 <<= 1;
         while (rows_pow2 < num_tile_rows) rows_pow2 <<= 1;
         uint32_t tw = DIV_ROUND_UP(pic_width_in_sb, cols_pow2);
         uint32_t th = DIV_ROUND_UP(pic_height_in_sb, rows_pow2);
         col_start_sb = tile_col * tw;
         row_start_sb = tile_row * th;
         tile_w_sb = MIN2(tw, pic_width_in_sb - col_start_sb);
         tile_h_sb = MIN2(th, pic_height_in_sb - row_start_sb);
      } else {
         col_start_sb = 0;
         for (uint32_t c = 0; c < tile_col; c++)
            col_start_sb += ti->pWidthInSbsMinus1[c] + 1;
         row_start_sb = 0;
         for (uint32_t r = 0; r < tile_row; r++)
            row_start_sb += ti->pHeightInSbsMinus1[r] + 1;
         tile_w_sb = (tile_col == num_tile_cols - 1) ?
            pic_width_in_sb - col_start_sb : ti->pWidthInSbsMinus1[tile_col] + 1;
         tile_h_sb = (tile_row == num_tile_rows - 1) ?
            pic_height_in_sb - row_start_sb : ti->pHeightInSbsMinus1[tile_row] + 1;
      }

      anv_batch_emit(&cmd->batch, GENX(AVP_TILE_CODING), til) {
         til.FrameTileID = tile_idx;
         til.TGTileNum = tile_idx;
         til.TileGroupID = 0;
         til.TileColumnPositioninSBUnit = col_start_sb;
         til.TileRowPositioninSBUnit = row_start_sb;
         til.TileWidthinSBMinus1 = tile_w_sb - 1;
         til.TileHeightinSBMinus1 = tile_h_sb - 1;
         til.FirstTileinaFrame = tile_idx == 0;
         til.IsLastTileofColumnFlag = tile_row == num_tile_rows - 1;
         til.IsLastTileofRowFlag = tile_col == num_tile_cols - 1;
         til.IsStartTileofTileGroupFlag = tile_idx == 0;
         til.IsEndTileofTileGroupFlag = tile_idx == num_tiles - 1;
         til.IsLastTileofFrameFlag = tile_idx == num_tiles - 1;
         til.NumberofActiveBEPipes = 1;
         til.NumofTileColumnsinFrameMinus1 = num_tile_cols - 1;
         til.NumofTileRowsinFrameMinus1 = num_tile_rows - 1;
         til.DisableFrameContextUpdateFlag =
            pic_info->flags.disable_frame_end_update_cdf ||
            (tile_idx != (ti ? ti->context_update_tile_id : 0));
      }

      /* The sequence header OBU is not inserted here: the app writes it into the
       * bitstream itself (anv_GetEncodedVideoSessionParametersKHR). The frame and
       * tile-group headers are inserted only on the first tile. */
      if (tile_idx == 0) {
         uint32_t frame_hdr[32] = { 0 };
         size_t hdr_size = 0;
         ASSERTED VkResult result =
            vk_video_encode_av1_frame_hdr(params, pic_info, base_q_idx,
                                          reference_select, false /* restoration_support */,
                                          frame_width, frame_height,
                                          sizeof(frame_hdr), &hdr_size, frame_hdr);
         assert(result == VK_SUCCESS);
         uint32_t num_dw = DIV_ROUND_UP(hdr_size, 4);
         uint32_t last_bits = (hdr_size & 3) ? (hdr_size & 3) * 8 : 32;
         uint32_t *dw = anv_batch_emitn(&cmd->batch, num_dw + 2,
                                        GENX(AVP_PAK_INSERT_OBJECT),
                                        .DataBitsInLastDW = last_bits);
         memcpy(dw + 2, frame_hdr, num_dw * 4);

         /* The tile group obu_size is patched after PAK runs with a
          * MI_STORE_REGISTER_MEM of one dword (the fixed 4-byte leb128), so it
          * must land dword aligned in the bitstream buffer: pad with a PADDING
          * OBU (minimum 2 bytes) so that hdr_size + pad_len + 1 is a multiple
          * of 4. */
         uint32_t pad_len = (4 - ((hdr_size + 1) & 3)) & 3;
         if (pad_len == 1)
            pad_len = 5;
         if (pad_len) {
            /* The padding OBU payload must begin with AV1 trailing bits */
            uint32_t pad_obu[2] = { 0x7a | (pad_len - 2) << 8, 0 };
            if (pad_len > 2)
               pad_obu[0] |= 0x80u << 16;
            uint32_t pad_dw = DIV_ROUND_UP(pad_len, 4);
            uint32_t pad_last_bits = (pad_len & 3) ? (pad_len & 3) * 8 : 32;
            dw = anv_batch_emitn(&cmd->batch, pad_dw + 2,
                                 GENX(AVP_PAK_INSERT_OBJECT),
                                 .DataBitsInLastDW = pad_last_bits);
            memcpy(dw + 2, pad_obu, pad_dw * 4);
         }

         /* Tile group OBU header with obu_has_size_field = 1 and a 4-byte
          * non-minimal leb128 placeholder. It will be done below with MI_MATH
          * from the tile bytecount register once PAK completes.
          * A single tile group has no tile start/end bits,
          * so the tile data follows directly. */
         static const uint32_t obu_tile_group[2] = {
            0x80808022, 0x00000000,
         };
         /* NumTiles > 1: append tile_start_and_end_present_flag
          * (0, single tile group) + byte_alignment as one extra
          * 0x00 byte after obu_size. */
         dw = anv_batch_emitn(&cmd->batch, ARRAY_SIZE(obu_tile_group) + 2,
                              GENX(AVP_PAK_INSERT_OBJECT),
                              .LastHeader = true,
                              .DataBitsInLastDW = num_tiles > 1 ? 16 : 8);
         memcpy(dw + 2, obu_tile_group, sizeof(obu_tile_group));

         tg_leb128_addr = anv_address_add(dst_buffer->address,
                                          (enc_info->dstBufferOffset & ~4095) +
                                          hdr_size + pad_len + 1);
         assert((tg_leb128_addr.offset & 3) == 0);
         tg_payload_prefix = hdr_size + pad_len + 5;
      }

      anv_batch_emit(&cmd->batch, GENX(VDENC_WEIGHTSOFFSETS_STATE), wo) {
         wo.WeightsForwardReference0 = 1;
         wo.WeightsForwardReference1 = 1;
         wo.WeightsForwardReference2 = 1;
         wo.HEVCVP9WeightsBackwardReference0 = 1;
         wo.CbWeightsForwardReference0 = 1;
         wo.CbWeightsForwardReference1 = 1;
         wo.CbWeightsForwardReference2 = 1;
         wo.CbWeightsBackwardReference0 = 1;
         wo.CrWeightsForwardReference0 = 1;
         wo.CrWeightsForwardReference1 = 1;
         wo.CrWeightsForwardReference2 = 1;
         wo.CrWeightsBackwardReference0 = 1;
      }

      /* VDENC_CMD1: 32-dword rate-distortion cost/lambda state. v[N] packs
       * command dword N+1. Every lane is a fixed cost constant except the two
       * 16-bit values in dw22, which are indexed by the frame QP. */
      uint16_t param0 = av1_vdenc_cmd1_par0[is_intra ? 0 : 1][base_q_idx];
      uint16_t param1 = av1_vdenc_cmd1_par1[is_intra ? 0 : 1][base_q_idx];

      static const uint8_t param2[8]  = { 0, 2, 3, 5, 6, 8, 9, 11 };
      static const uint8_t param3[12] = { 4, 14, 24, 34, 44, 54, 64, 74, 84, 94, 104, 114 };
      static const uint8_t param4[12] = { 3, 9, 14, 19, 24, 29, 34, 39, 44, 49, 54, 60 };

      uint32_t v[32] = { 0 };
      /* One byte per lane: param2 -> dw1..dw2, param3 -> dw3..dw5, param4 -> dw6..dw8. */
      for (unsigned i = 0; i < 8; i++)
         v[i / 4] |= (uint32_t)param2[i] << (8 * (i % 4));
      for (unsigned i = 0; i < 12; i++) {
         v[2 + i / 4] |= (uint32_t)param3[i] << (8 * (i % 4));
         v[5 + i / 4] |= (uint32_t)param4[i] << (8 * (i % 4));
      }
      v[21] = param0 | param1 << 16; /* dw22: QP-indexed cost (low | high) */

      if (is_intra) {
         /* Intra cost constants, one byte per lane. */
         v[12] = 42u << 24;                              /* dw13 */
         v[15] = 21u << 16;                              /* dw16 */
         v[16] = 21u | 47u << 8 | 16u << 16 | 16u << 24; /* dw17 */
         v[17] = 30u | 30u << 8 | 58u << 16 | 20u << 24; /* dw18 */
         v[18] = 20u << 16;                              /* dw19 */
      } else {
         /* Inter cost constants; low_delay and random-access differ only in
          * the dw19 lanes. */
         v[8]  = 6u | 3u << 8 | 10u << 16;               /* dw9  */
         v[9]  = 5u | 23u << 8 | 6u << 16 | 26u << 24;   /* dw10 */
         v[10] = 5u | 21u << 8;                          /* dw11 */
         v[11] = 92u | 19u << 8 | 92u << 16 | 18u << 24; /* dw12 */
         v[12] = 15u | 4u << 8 | 4u << 16 | 54u << 24;   /* dw13 */
         v[15] = 21u << 16;                              /* dw16 */
         v[16] = 21u | 23u << 8 | 24u << 16 | 27u << 24; /* dw17 */
         v[17] = 41u | 68u << 8 | 37u << 16 | 37u << 24; /* dw18 */
         v[18] = low_delay ? 12u << 16 :
                 3u << 8 | 12u << 16 | 12u << 24;        /* dw19 */
         v[30] = 20u | 20u << 8 | 20u << 16;             /* dw31 */
      }

      /* dw20..dw30 fixed cost constants (byte-replicated per dword). */
      v[19] = 0x05050505; /* dw20 */
      v[20] = 0x0c0c0c0c; /* dw21 */
      v[22] = 0x12121212; /* dw23 */
      v[23] = 0x10101010; /* dw24 */
      v[24] = 0x10101010; /* dw25 */
      v[25] = 0x16161616; /* dw26 */
      v[26] = 0x10101010; /* dw27 */
      v[27] = 0x10101010; /* dw28 */
      v[28] = 0x10101010; /* dw29 */
      v[29] = 0x1a1a1a1a; /* dw30 */

      anv_batch_emit(&cmd->batch, GENX(VDENC_CMD1), cmd1) {
         for (unsigned i = 0; i < 32; i++)
            cmd1.Values[i] = v[i];
      }

      anv_batch_emit(&cmd->batch, GENX(VDENC_HEVC_VP9_TILE_SLICE_STATE), til) {
         uint32_t ctb_size = 64;
         bool tile_enable = true;
         uint32_t tile_w_pix = MIN2(tile_w_sb * ctb_size,
                                    frame_width - col_start_sb * ctb_size);
         uint32_t tile_h_pix = MIN2(tile_h_sb * ctb_size,
                                    frame_height - row_start_sb * ctb_size);

         til.NumParEngine = 0;
         til.TileNumber = tile_idx;
         til.TileRowStoreSelect = 0;
         til.TileStartCTBX = col_start_sb * ctb_size;
         til.TileStartCTBY = row_start_sb * ctb_size;
         til.TileWidth = tile_w_pix - 1;
         til.TileHeight = tile_h_pix - 1;
         til.StreaminOffsetEnable = tile_enable;
         til.TileStreaminOffset =
            4 * (row_start_sb * pic_width_in_sb + col_start_sb * tile_h_sb);
         til.RowStoreOffsetEnable = row_start_sb == 0 ? tile_enable : false;
         til.TileStreamoutOffsetEnable = tile_enable;
         til.TileStreamoutOffset = tile_idx * 19;
         til.LCUStreamOutOffsetEnable = tile_enable;
         til.TileLCUStreamOutOffset = lcu_tile_offset;

         /* Tile-slice tuning constants. */
         til.TileSliceParam12 = 0x3f;
         til.TileSliceParam11 = 2;
         til.TileSliceParam16 = 0x3f;
         til.TileSliceParam17 = 0x3f;
         til.TileSliceParam18 = 0x3f;
         til.TileCumulativeCUCountStreamoutOffsetEnable = 1;
         til.TileCumulativeCUCountStreamoutOffset = cu_tile_offset;
      }
      cu_tile_offset += DIV_ROUND_UP(tile_w_sb * tile_h_sb * 2, 64);
      uint32_t max_cu = (sb_size / 8) * (sb_size / 8);
      lcu_tile_offset += DIV_ROUND_UP(2 * 4 * tile_w_sb * tile_h_sb * (5 + max_cu * 8), 64);

      anv_batch_emit(&cmd->batch, GENX(VDENC_CMD2), cmd2) {
         int32_t y_dc_delta_q = pic_info->pQuantization ? pic_info->pQuantization->DeltaQYDc : 0;
         uint32_t qp_prime_y_dc = CLAMP((int32_t)base_q_idx + y_dc_delta_q, 0, 255);

         /* AV1_I=0/P=1/B=2/GPB=3. */
         uint32_t picture_type;
         if (is_intra)
            picture_type = 1;
         else if (frame_info->predictionMode <= VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR)
            picture_type = 1;
         else if (frame_info->predictionMode == VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR)
            picture_type = 3;
         else
            picture_type = 2;

         bool tiling = pic_info->pTileInfo &&
                       (pic_info->pTileInfo->TileCols > 1 || pic_info->pTileInfo->TileRows > 1);

         cmd2.FrameWidthInPixelsMinusOne = align(frame_width, 8) - 1;
         cmd2.FrameHeightInPixelsMinusOne = align(frame_height, 8) - 1;
         cmd2.PictureType = picture_type;
         cmd2.TemporalMVPEnableFlag = !is_intra && pic_info->flags.use_ref_frame_mvs &&
                                      seq_hdr->flags.enable_order_hint;
         cmd2.TilingEnable = tiling;

         /* POC deltas / DPB indices from the shared reference split */
         cmd2.POCNumberForRefid0InL0 = poc_l0[0];
         cmd2.POCNumberForRefid0InL1 = poc_l1[0];
         cmd2.POCNumberForRefid1InL0 = poc_l0[1];
         cmd2.POCNumberForRefid1InL1 = poc_l1[1];
         cmd2.POCNumberForRefid2InL0 = poc_l0[2];
         cmd2.POCNumberForRefid2InL1 = poc_l1[2];
         cmd2.POCNumberForRefid3InL0 = poc_l0[3];
         cmd2.POCNumberForRefid3InL1 = poc_l1[3];
         cmd2.NumRefIdxL0MinusOne = num_ref_l0 > 0 ? num_ref_l0 - 1 : 0;
         cmd2.NumRefIdxL1MinusOne = num_ref_l1 > 0 ? num_ref_l1 - 1 : 0;

         cmd2.MinQp = 0;
         cmd2.MaxQp = 255;
         cmd2.IntraRefreshMBSizeMinusOne = 1;

         cmd2.QpPrimeYDc = qp_prime_y_dc;
         cmd2.QpPrimeYAc = base_q_idx;
         /* qpForSegs: segmentation disabled puts base_q_idx in all 8 segments.
          * TODO: per-segment QP from pSegmentation. */
         cmd2.Values24 = base_q_idx * 0x01010101u;
         cmd2.Values25 = base_q_idx * 0x01010101u;

         uint32_t tu = 4, frame_type = is_intra ? 0 : pic_info->frame_type;
         uint32_t l0_ref = num_ref_l0, l1_ref = num_ref_l1;
         uint32_t l1_ctrl_not0 = reference_select;
         uint32_t wa_549751 = 1, wa_2209975292 = 0, wa_14010476401 = 0, wa_22011531258 = 0;

         uint32_t data[57] = { 0 };
         data[6]  = av1_vdenc_cmd2_dw6[wa_2209975292];
         data[7]  = av1_vdenc_cmd2_dw7[low_delay][frame_type][wa_549751][l0_ref];
         data[8]  = av1_vdenc_cmd2_dw8[tu][low_delay][frame_type][l1_ctrl_not0];
         data[9]  = av1_vdenc_cmd2_dw9[tu][low_delay][frame_type][l1_ctrl_not0][wa_549751];
         data[11] = av1_vdenc_cmd2_dw11[low_delay][l0_ref][l1_ref];
         data[51] = av1_vdenc_cmd2_dw51[tu][frame_type][wa_549751];
         data[52] = av1_vdenc_cmd2_dw52[tu];
         data[53] = av1_vdenc_cmd2_dw53[tu][frame_type][wa_549751][wa_14010476401];
         data[54] = av1_vdenc_cmd2_dw54[tu][wa_22011531258];
         data[56] = av1_vdenc_cmd2_dw56[low_delay][l0_ref][l1_ref];

         static const uint8_t dws_idx[30] = {
            2, 5, 12, 14, 15, 16, 17, 18, 19, 20, 27, 28, 29, 30, 31, 32, 35, 37,
            39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
         };
         for (unsigned i = 0; i < 30; i++)
            data[dws_idx[i]] |= av1_vdenc_cmd2_dws[i];

         cmd2.Values2  = data[2];
         cmd2.Values5  = data[5];
         cmd2.Values6  = data[6];
         cmd2.Values7  = data[7];
         cmd2.Values8  = data[8];
         cmd2.Values9  = data[9];
         /* per-list DPB indices (FwdRef0/1/2 bits 0/8/16, BwdRef0 bit 24,
          * 3 bits each) on top of the data[11] cost bits. */
         cmd2.Values11 = data[11] |
                         frame_idx_l0[0] | frame_idx_l0[1] << 8 |
                         frame_idx_l0[2] << 16 | frame_idx_l1 << 24;
         cmd2.Values12 = data[12];
         cmd2.Values14 = data[14];
         cmd2.Values15 = data[15];
         cmd2.Values16 = data[16];
         cmd2.Values17 = data[17];
         cmd2.Values18 = data[18];
         cmd2.Values19 = data[19];
         cmd2.Values20 = data[20];
         cmd2.Values27 = data[27];
         cmd2.Values28 = data[28];
         cmd2.Values29 = data[29];
         cmd2.Values30 = data[30];
         cmd2.Values31 = data[31];
         cmd2.Values35 = data[35];
         cmd2.Values37 = data[37];
         cmd2.Values40 = data[39];
         cmd2.Values41 = data[40];
         cmd2.Values42 = data[41];
         cmd2.Values43 = data[42];
         cmd2.Values44 = data[43];
         cmd2.Values45 = data[44];
         cmd2.Values46 = data[45];
         cmd2.Values47 = data[46];
         cmd2.Values48 = data[47];
         cmd2.Values49 = data[48];
         cmd2.Values50 = data[49];
         cmd2.Values51 = data[50];
         cmd2.Values52 = data[51];
         cmd2.Values53 = data[52];
         cmd2.Values54 = data[53];
         cmd2.Values55 = data[54];
         cmd2.Values57 = data[56];

         /* CQP uses fixedRounding = intra 6 / inter 2.
          * TODO: adaptiveRounding derives param_inter from the previous frame
          * statistics streamout. */
         uint32_t param_intra = 6, param_inter = 2;
         cmd2.Values32 = data[32] |
                         param_inter << 16 | param_inter << 20 |
                         param_intra << 24 | param_intra << 28;
         cmd2.Values33 = param_inter | param_inter << 4 |
                         param_inter << 8 | param_inter << 12 |
                         param_intra << 16 | param_intra << 20 |
                         param_inter << 24 | param_inter << 28;
         cmd2.Values34 = param_inter | param_inter << 4 |
                         param_intra << 8 | param_intra << 12 |
                         param_inter << 16 | param_inter << 20;

         cmd2.AV1L0RefID0 = 1;
         cmd2.AV1L1RefID0 = 1;
         cmd2.AV1L0RefID1 = 1;
         cmd2.AV1L1RefID1 = 1;
         cmd2.AV1L0RefID2 = 1;
         cmd2.AV1L1RefID2 = 1;
         cmd2.AV1L0RefID3 = 1;
         cmd2.AV1L1RefID3 = 1;
      }

      anv_batch_emit(&cmd->batch, GENX(VDENC_WALKER_STATE), walker) {
         walker.DWordLength = 1;
         walker.FirstSuperSlice = true;
         walker.MBLCUStartXPosition = col_start_sb;
         walker.MBLCUStartYPosition = row_start_sb;
         walker.NextSliceMBStartYPosition = row_start_sb + tile_h_sb;
         walker.NextSliceMBLCUStartXPosition = col_start_sb + tile_w_sb;
      }

      anv_batch_emit(&cmd->batch, GENX(VD_PIPELINE_FLUSH), flush) {
         flush.VDENCPipelineDone = true;
         flush.VDCommandMessageParserDone = true;
         flush.VDENCPipelineCommandFlush = true;
      }

      /* Accumulate this tile's with-header bytecount into the scratch dword.
       * Mirrors media-driver Av1VdencPkt::ReadPakMmioRegisters: the per-tile
       * BITSTREAM_BYTECOUNT_TILE reg is loaded and added to the running total,
       * with the first tile seeding it. Done for every frame (single-tile seeds
       * the whole frame) so both the multi-tile obu_size patch and the query
       * bytesWritten feedback can read the frame total from scratch. */
      anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
         flush.DWordLength = 2;
         flush.VideoPipelineCacheInvalidate = 1;
      };

      struct mi_builder ab;
      mi_builder_init(&ab, cmd->device->info, &cmd->batch);
      struct anv_address tile_bs_accum = {
         vid->vid_mem[ANV_VID_MEM_AV1_ENCODE_TILE_BITSTREAM_ACCUM].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_ENCODE_TILE_BITSTREAM_ACCUM].offset
      };
      mi_builder_set_mocs(&ab, anv_mocs_for_address(cmd->device, &tile_bs_accum));
      struct mi_value cur = mi_reg32(AVP_BITSTREAM_BYTECOUNT_TILE_REG);
      if (tile_idx == 0)
         mi_store(&ab, mi_mem32(tile_bs_accum), cur);
      else
         mi_store(&ab, mi_mem32(tile_bs_accum),
                  mi_iadd(&ab, mi_mem32(tile_bs_accum), cur));

      anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
         flush.DWordLength = 2;
      };
   } /* end per-tile loop */

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.DWordLength = 2;
      flush.VideoPipelineCacheInvalidate = 1;
   };

   anv_batch_emit(&cmd->batch, GENX(VD_PIPELINE_FLUSH), flush) {
      flush.VDCommandMessageParserDone = true;
   }

   anv_batch_emit(&cmd->batch, GENX(MI_FLUSH_DW), flush) {
      flush.DWordLength = 2;
   };

   /* Tile group obu_size back-annotation:
    * media-driver does this with the HuC Av1BackAnnotation packet.
    * Without HuC, encode the tile bytecount PAK wrote into a
    * 4-byte leb128 with MI_MATH and store it over the placeholder.
    */
   struct mi_builder b;
   mi_builder_init(&b, cmd->device->info, &cmd->batch);
   mi_builder_set_mocs(&b, anv_mocs_for_address(cmd->device, &tg_leb128_addr));

   struct mi_value tile_bytes;
   if (num_tiles > 1) {
      /* Running sum of per-tile with-header bytecounts (whole frame output);
       * subtract the frame-header/OBU-header prefix to get the tile group
       * OBU payload size. */
      struct anv_address tile_bs_accum = {
         vid->vid_mem[ANV_VID_MEM_AV1_ENCODE_TILE_BITSTREAM_ACCUM].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_ENCODE_TILE_BITSTREAM_ACCUM].offset
      };
      tile_bytes = mi_isub(&b, mi_mem32(tile_bs_accum), mi_imm(tg_payload_prefix));
   } else {
      tile_bytes =
         mi_value_to_gpr(&b, mi_reg32(AVP_BITSTREAM_BYTECOUNT_TILE_NOHEADER_REG));
   }
   struct mi_value leb0 =
      mi_ior(&b, mi_iand(&b, mi_value_ref(&b, tile_bytes), mi_imm(0x7f)),
             mi_imm(0x80));
   struct mi_value leb1 =
      mi_ishl_imm(&b,
                  mi_ior(&b,
                         mi_iand(&b,
                                 mi_ushr32_imm(&b, mi_value_ref(&b, tile_bytes), 7),
                                 mi_imm(0x7f)),
                         mi_imm(0x80)), 8);
   struct mi_value leb2 =
      mi_ishl_imm(&b,
                  mi_ior(&b,
                         mi_iand(&b,
                                 mi_ushr32_imm(&b, mi_value_ref(&b, tile_bytes), 14),
                                 mi_imm(0x7f)),
                         mi_imm(0x80)), 16);
   struct mi_value leb3 =
      mi_ishl_imm(&b, mi_ushr32_imm(&b, tile_bytes, 21), 24);

   struct mi_value leb128 =
      mi_ior(&b, mi_ior(&b, leb0, leb1), mi_ior(&b, leb2, leb3));
   mi_store(&b, mi_mem32(tg_leb128_addr), leb128);

#endif // GFX_VERx10 == 125
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
   } else if (pool->codec & VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR) {
      /* AV1 BITSTREAM_BYTECOUNT_TILE is per-tile; the frame total (all tiles)
       * is the running sum accumulated into the encode scratch dword. */
      struct anv_video_session *vid = cmd_buffer->video.vid;
      struct anv_address tile_bs_accum = {
         vid->vid_mem[ANV_VID_MEM_AV1_ENCODE_TILE_BITSTREAM_ACCUM].mem->bo,
         vid->vid_mem[ANV_VID_MEM_AV1_ENCODE_TILE_BITSTREAM_ACCUM].offset
      };
      mi_store(&b, mi_mem64(anv_address_add(query_addr, 8)), mi_mem32(tile_bs_accum));
      emit_query_mi_availability(&b, query_addr, true);
      return;
   } else {
      UNREACHABLE("Invalid codec operation");
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
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      anv_av1_encode_video(cmd_buffer, pEncodeInfo);
      break;
   default:
      assert(0);
   }

   if (inline_query)
      handle_inline_query_end(cmd_buffer, inline_query);
}
