/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 * Copyright 2023 Red Hat Inc.
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
#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_device_memory.h"
#include "radv_entrypoints.h"
#include "radv_image_view.h"
#include "radv_physical_device.h"
#include "radv_query.h"
#include "radv_video.h"

#include "ac_vcn_enc.h"
#include "ac_vcn_enc_av1_default_cdf.h"

#define ENC_ALIGNMENT 256

#define RENCODE_V5_FW_INTERFACE_MAJOR_VERSION 1
#define RENCODE_V5_FW_INTERFACE_MINOR_VERSION 10

#define RENCODE_V4_FW_INTERFACE_MAJOR_VERSION 1
#define RENCODE_V4_FW_INTERFACE_MINOR_VERSION 11

#define RENCODE_V3_FW_INTERFACE_MAJOR_VERSION 1
#define RENCODE_V3_FW_INTERFACE_MINOR_VERSION 27

#define RENCODE_V2_FW_INTERFACE_MAJOR_VERSION 1
#define RENCODE_V2_FW_INTERFACE_MINOR_VERSION 20

#define RENCODE_FW_INTERFACE_MAJOR_VERSION 1
#define RENCODE_FW_INTERFACE_MINOR_VERSION 15

#define ENC_ALIGNMENT 256

struct radv_enc_av1_state {
   rvcn_enc_av1_tile_config_t tile_config;
   bool skip_mode_allowed;
   bool disallow_skip_mode;
};

void
radv_probe_video_encode(struct radv_physical_device *pdev)
{
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   pdev->video_encode_enabled = false;

   if (instance->debug_flags & RADV_DEBUG_NO_VIDEO)
      return;

   /* WRITE_MEMORY is needed for SetEvent and is required to pass CTS */
   if (radv_video_write_memory_supported(pdev)) {
      pdev->video_encode_enabled = true;
      return;
   }

   pdev->video_encode_enabled = !!(instance->perftest_flags & RADV_PERFTEST_VIDEO_ENCODE);
}

void
radv_init_physical_device_encoder(struct radv_physical_device *pdev)
{
   if (pdev->info.vcn_ip_version >= VCN_5_0_0) {
      pdev->enc_hw_ver = RADV_VIDEO_ENC_HW_5;
      pdev->encoder_interface_version = ((RENCODE_V5_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
                                         (RENCODE_V5_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
   } else if (pdev->info.vcn_ip_version >= VCN_4_0_0) {
      pdev->enc_hw_ver = RADV_VIDEO_ENC_HW_4;
      pdev->encoder_interface_version = ((RENCODE_V4_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
                                         (RENCODE_V4_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
   } else if (pdev->info.vcn_ip_version >= VCN_3_0_0) {
      pdev->enc_hw_ver = RADV_VIDEO_ENC_HW_3;
      pdev->encoder_interface_version = ((RENCODE_V3_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
                                         (RENCODE_V3_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
   } else if (pdev->info.vcn_ip_version >= VCN_2_0_0) {
      pdev->enc_hw_ver = RADV_VIDEO_ENC_HW_2;
      pdev->encoder_interface_version = ((RENCODE_V2_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
                                         (RENCODE_V2_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
   } else {
      pdev->enc_hw_ver = RADV_VIDEO_ENC_HW_1_2;
      pdev->encoder_interface_version = ((RENCODE_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
                                         (RENCODE_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
   }

   ac_vcn_enc_init_cmds(&pdev->vcn_enc_cmds, pdev->info.vcn_ip_version);
}

static uint32_t
radv_enc_rate_control_method(VkVideoEncodeRateControlModeFlagBitsKHR mode)
{
   switch (mode) {
   case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR:
      return RENCODE_RATE_CONTROL_METHOD_CBR;
   case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR:
      return RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
   case VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR:
   default:
      return RENCODE_RATE_CONTROL_METHOD_NONE;
   }
}

static void
radv_enc_rc_per_pic_default(struct radv_video_session *vid, rvcn_enc_rate_ctl_per_picture_t *per_pic)
{
   const uint32_t min_qp = vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR ? 1 : 0;
   const uint32_t max_qp = vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR ? 255 : 51;

   per_pic->qp_i = max_qp / 2;
   per_pic->qp_p = max_qp / 2;
   per_pic->qp_b = max_qp / 2;
   per_pic->min_qp_i = min_qp;
   per_pic->max_qp_i = max_qp;
   per_pic->min_qp_p = min_qp;
   per_pic->max_qp_p = max_qp;
   per_pic->min_qp_b = min_qp;
   per_pic->max_qp_b = max_qp;
   per_pic->max_au_size_i = 0;
   per_pic->max_au_size_p = 0;
   per_pic->max_au_size_b = 0;
   per_pic->enabled_filler_data = 1;
   per_pic->skip_frame_enable = 0;
   per_pic->enforce_hrd = 1;
}

/* to process invalid frame rate */
static void
radv_vcn_enc_invalid_frame_rate(uint32_t *den, uint32_t *num)
{
   if (*den == 0 || *num == 0) {
      *den = 1;
      *num = 30;
   }
}

static uint32_t
radv_vcn_per_frame_integer(uint32_t bitrate, uint32_t den, uint32_t num)
{
   uint64_t rate_den = (uint64_t)bitrate * (uint64_t)den;

   return (uint32_t)(rate_den / num);
}

static uint32_t
radv_vcn_per_frame_frac(uint32_t bitrate, uint32_t den, uint32_t num)
{
   uint64_t rate_den = (uint64_t)bitrate * (uint64_t)den;
   uint64_t remainder = rate_den % num;

   return (uint32_t)((remainder << 32) / num);
}

static void
radv_enc_set_emulation_prevention(struct radv_cmd_buffer *cmd_buffer, bool set)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   if (set != enc->emulation_prevention) {
      enc->emulation_prevention = set;
      enc->num_zeros = 0;
   }
}

static uint32_t
radv_enc_value_bits(uint32_t value)
{
   uint32_t i = 1;

   while (value > 1) {
      i++;
      value >>= 1;
   }

   return i;
}

static const unsigned index_to_shifts[4] = {24, 16, 8, 0};

static void
radv_enc_output_one_byte(struct radv_cmd_buffer *cmd_buffer, unsigned char byte)
{
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   if (enc->byte_index == 0)
      cs->b->buf[cs->b->cdw] = 0;
   cs->b->buf[cs->b->cdw] |= ((unsigned int)(byte) << index_to_shifts[enc->byte_index]);
   enc->byte_index++;

   if (enc->byte_index >= 4) {
      enc->byte_index = 0;
      cs->b->cdw++;
   }
}

static void
radv_enc_emulation_prevention(struct radv_cmd_buffer *cmd_buffer, unsigned char byte)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   if (enc->emulation_prevention) {
      if ((enc->num_zeros >= 2) && ((byte == 0x00) || (byte == 0x01) || (byte == 0x02) || (byte == 0x03))) {
         radv_enc_output_one_byte(cmd_buffer, 0x03);
         enc->bits_output += 8;
         enc->num_zeros = 0;
      }
      enc->num_zeros = (byte == 0 ? (enc->num_zeros + 1) : 0);
   }
}

static void
radv_enc_code_fixed_bits(struct radv_cmd_buffer *cmd_buffer, unsigned int value, unsigned int num_bits)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   unsigned int bits_to_pack = 0;
   enc->bits_size += num_bits;

   while (num_bits > 0) {
      unsigned int value_to_pack = value & (0xffffffff >> (32 - num_bits));
      bits_to_pack = num_bits > (32 - enc->bits_in_shifter) ? (32 - enc->bits_in_shifter) : num_bits;

      if (bits_to_pack < num_bits)
         value_to_pack = value_to_pack >> (num_bits - bits_to_pack);

      enc->shifter |= value_to_pack << (32 - enc->bits_in_shifter - bits_to_pack);
      num_bits -= bits_to_pack;
      enc->bits_in_shifter += bits_to_pack;

      while (enc->bits_in_shifter >= 8) {
         unsigned char output_byte = (unsigned char)(enc->shifter >> 24);
         enc->shifter <<= 8;
         radv_enc_emulation_prevention(cmd_buffer, output_byte);
         radv_enc_output_one_byte(cmd_buffer, output_byte);
         enc->bits_in_shifter -= 8;
         enc->bits_output += 8;
      }
   }
}

static void
radv_enc_reset(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   enc->emulation_prevention = false;
   enc->shifter = 0;
   enc->bits_in_shifter = 0;
   enc->bits_output = 0;
   enc->num_zeros = 0;
   enc->byte_index = 0;
   enc->bits_size = 0;
}

static void
radv_enc_byte_align(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   unsigned int num_padding_zeros = (32 - enc->bits_in_shifter) % 8;

   if (num_padding_zeros > 0)
      radv_enc_code_fixed_bits(cmd_buffer, 0, num_padding_zeros);
}

static void
radv_enc_flush_headers(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   if (enc->bits_in_shifter != 0) {
      unsigned char output_byte = (unsigned char)(enc->shifter >> 24);
      radv_enc_emulation_prevention(cmd_buffer, output_byte);
      radv_enc_output_one_byte(cmd_buffer, output_byte);
      enc->bits_output += enc->bits_in_shifter;
      enc->shifter = 0;
      enc->bits_in_shifter = 0;
      enc->num_zeros = 0;
   }

   if (enc->byte_index > 0) {
      cs->b->cdw++;
      enc->byte_index = 0;
   }
}

static void
radv_enc_code_ue(struct radv_cmd_buffer *cmd_buffer, unsigned int value)
{
   unsigned int x = 0;
   unsigned int ue_code = value + 1;
   value += 1;

   while (value) {
      value = (value >> 1);
      x += 1;
   }
   if (x > 1)
      radv_enc_code_fixed_bits(cmd_buffer, 0, x - 1);
   radv_enc_code_fixed_bits(cmd_buffer, ue_code, x);
}

static void
radv_enc_code_se(struct radv_cmd_buffer *cmd_buffer, int value)
{
   unsigned int v = 0;

   if (value != 0)
      v = (value < 0 ? ((unsigned int)(0 - value) << 1) : (((unsigned int)(value) << 1) - 1));

   radv_enc_code_ue(cmd_buffer, v);
}

static void
radv_enc_code_ns(struct radv_cmd_buffer *cmd_buffer, uint32_t value, uint32_t max)
{
   uint32_t w = 0;
   uint32_t m;
   uint32_t max_num = max;

   assert(value < max);

   while (max_num) {
      max_num >>= 1;
      w++;
   }
   m = (1 << w) - max;

   if (value < m) {
      radv_enc_code_fixed_bits(cmd_buffer, value, w - 1);
   } else {
      uint32_t diff = value - m;
      uint32_t out = (((diff >> 1) + m) << 1) | (diff & 0x1);
      radv_enc_code_fixed_bits(cmd_buffer, out, w);
   }
}

static uint32_t
radv_enc_h264_pic_type(enum StdVideoH264PictureType type)
{
   switch (type) {
   case STD_VIDEO_H264_PICTURE_TYPE_P:
      return RENCODE_PICTURE_TYPE_P;
   case STD_VIDEO_H264_PICTURE_TYPE_B:
      return RENCODE_PICTURE_TYPE_B;
   case STD_VIDEO_H264_PICTURE_TYPE_I:
   case STD_VIDEO_H264_PICTURE_TYPE_IDR:
   default:
      return RENCODE_PICTURE_TYPE_I;
   }
}

static uint32_t
radv_enc_h265_pic_type(enum StdVideoH265PictureType type)
{
   switch (type) {
   case STD_VIDEO_H265_PICTURE_TYPE_P:
      return RENCODE_PICTURE_TYPE_P;
   case STD_VIDEO_H265_PICTURE_TYPE_B:
      return RENCODE_PICTURE_TYPE_B;
   case STD_VIDEO_H265_PICTURE_TYPE_I:
   case STD_VIDEO_H265_PICTURE_TYPE_IDR:
   default:
      return RENCODE_PICTURE_TYPE_I;
   }
}

#define RADEON_ENC_CS(value) (cmd_buffer->cs->b->buf[cmd_buffer->cs->b->cdw++] = (value))

#define RADEON_ENC_BEGIN(cmd)                                                                                          \
   {                                                                                                                   \
      uint32_t *begin = &cmd_buffer->cs->b->buf[cmd_buffer->cs->b->cdw++];                                             \
      RADEON_ENC_CS(cmd)

#define RADEON_ENC_END()                                                                                               \
   *begin = (&cmd_buffer->cs->b->buf[cmd_buffer->cs->b->cdw] - begin) * 4;                                             \
   cmd_buffer->video.enc.total_task_size += *begin;                                                                    \
   }

/* this function has to be in pair with AV1 header copy instruction type at the end */
static void
radv_enc_av1_bs_copy_end(struct radv_cmd_buffer *cmd_buffer, uint32_t bits)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   assert(bits > 0);
   /* it must be dword aligned at the end */
   *enc->copy_start = DIV_ROUND_UP(bits, 32) * 4 + 12;
   *(enc->copy_start + 2) = bits;
}

/* av1 bitstream instruction type */
static void
radv_enc_av1_bs_instruction_type(struct radv_cmd_buffer *cmd_buffer, uint32_t inst, uint32_t obu_type)
{
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   struct radv_enc_state *enc = &cmd_buffer->video.enc;

   radv_enc_flush_headers(cmd_buffer);

   if (enc->bits_output)
      radv_enc_av1_bs_copy_end(cmd_buffer, enc->bits_output);

   enc->copy_start = &cs->b->buf[cs->b->cdw++];
   RADEON_ENC_CS(inst);

   if (inst != RENCODE_HEADER_INSTRUCTION_COPY) {
      *enc->copy_start = 8;
      if (inst == RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START) {
         *enc->copy_start += 4;
         RADEON_ENC_CS(obu_type);
      }
   } else
      RADEON_ENC_CS(0); /* allocate a dword for number of bits */

   radv_enc_reset(cmd_buffer);
}

static void
radv_enc_session_info(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   radv_cs_add_buffer(device->ws, cs->b, cmd_buffer->video.vid->sessionctx.mem->bo);

   uint64_t va = radv_buffer_get_va(cmd_buffer->video.vid->sessionctx.mem->bo);
   va += cmd_buffer->video.vid->sessionctx.offset;

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.session_info);
   RADEON_ENC_CS(pdev->encoder_interface_version);
   RADEON_ENC_CS(va >> 32);
   RADEON_ENC_CS(va & 0xffffffff);
   if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_3)
      RADEON_ENC_CS(RENCODE_ENGINE_TYPE_ENCODE);
   else
      RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void
radv_enc_task_info(struct radv_cmd_buffer *cmd_buffer, bool feedback)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   struct radv_enc_state *enc = &cmd_buffer->video.enc;

   enc->task_id++;
   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.task_info);
   enc->p_task_size = &cs->b->buf[cs->b->cdw++];
   RADEON_ENC_CS(enc->task_id);
   RADEON_ENC_CS(feedback ? 1 : 0);
   RADEON_ENC_END();
}

static VkExtent2D
radv_enc_aligned_coded_extent(const struct radv_physical_device *pdev, VkVideoCodecOperationFlagsKHR op,
                              VkExtent2D extent)
{
   uint32_t alignment_width;
   uint32_t alignment_height;

   switch (op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      alignment_width = 16;
      alignment_height = 16;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      alignment_width = 64;
      alignment_height = 16;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      if (pdev->enc_hw_ver == RADV_VIDEO_ENC_HW_4) {
         alignment_width = 64;
         alignment_height = 16;
      } else {
         alignment_width = 8;
         alignment_height = 2;
      }
      break;
   default:
      UNREACHABLE("Unsupported operation");
   }

   return (VkExtent2D){
      .width = align(extent.width, alignment_width),
      .height = align(extent.height, alignment_height),
   };
}

static void
radv_enc_session_init(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   VkExtent2D extent = {0, 0};

   if (enc_info) {
      extent = enc_info->srcPictureResource.codedExtent;
   } else {
      switch (vid->vk.op) {
      case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
         for (uint32_t i = 0; i < cmd_buffer->video.params->h264_enc.h264_sps_count; i++) {
            const StdVideoH264SequenceParameterSet *sps = &cmd_buffer->video.params->h264_enc.h264_sps[i].base;
            extent.width = MAX2((sps->pic_width_in_mbs_minus1 + 1) * 16, extent.width);
            extent.height = MAX2((sps->pic_height_in_map_units_minus1 + 1) * 16, extent.height);
         }
         break;
      case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
         for (uint32_t i = 0; i < cmd_buffer->video.params->h265_enc.h265_sps_count; i++) {
            const StdVideoH265SequenceParameterSet *sps = &cmd_buffer->video.params->h265_enc.h265_sps[i].base;
            extent.width = MAX2(sps->pic_width_in_luma_samples, extent.width);
            extent.height = MAX2(sps->pic_height_in_luma_samples, extent.height);
         }
         break;
      case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
         const StdVideoAV1SequenceHeader *seq = &cmd_buffer->video.params->av1_enc.seq_hdr.base;
         extent.width = seq->max_frame_width_minus_1 + 1;
         extent.height = seq->max_frame_height_minus_1 + 1;
         break;
      }
      default:
         UNREACHABLE("Unsupported operation");
      }
   }

   VkExtent2D aligned_extent = radv_enc_aligned_coded_extent(pdev, vid->vk.op, extent);
   uint32_t padding_width = aligned_extent.width - extent.width;
   uint32_t padding_height = aligned_extent.height - extent.height;

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.session_init);
   RADEON_ENC_CS(vid->enc_standard);
   RADEON_ENC_CS(aligned_extent.width);
   RADEON_ENC_CS(aligned_extent.height);
   RADEON_ENC_CS(padding_width);
   RADEON_ENC_CS(padding_height);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3)
      RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   if (pdev->enc_hw_ver == RADV_VIDEO_ENC_HW_4)
      RADEON_ENC_CS(vid->enc_wa_flags);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_4)
      RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void
radv_enc_layer_control(struct radv_cmd_buffer *cmd_buffer, uint32_t num_temporal_layers)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.layer_control);
   RADEON_ENC_CS(num_temporal_layers); // max num temporal layesr
   RADEON_ENC_CS(num_temporal_layers); // num temporal layers
   RADEON_ENC_END();
}

static void
radv_enc_layer_select(struct radv_cmd_buffer *cmd_buffer, int tl_idx)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.layer_select);
   RADEON_ENC_CS(tl_idx); // temporal layer index
   RADEON_ENC_END();
}

static void
radv_enc_slice_control(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);

   uint32_t num_mbs_in_slice;
   uint32_t width_in_mbs = DIV_ROUND_UP(enc_info->srcPictureResource.codedExtent.width, 16);
   uint32_t height_in_mbs = DIV_ROUND_UP(enc_info->srcPictureResource.codedExtent.height, 16);
   num_mbs_in_slice = DIV_ROUND_UP(width_in_mbs * height_in_mbs, h264_picture_info->naluSliceEntryCount);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.slice_control_h264);
   RADEON_ENC_CS(RENCODE_H264_SLICE_CONTROL_MODE_FIXED_MBS); // slice control mode
   RADEON_ENC_CS(num_mbs_in_slice);                          // num mbs per slice
   RADEON_ENC_END();
}

static void
radv_enc_spec_misc_h264(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);
   const StdVideoEncodeH264PictureInfo *pic = h264_picture_info->pStdPictureInfo;
   const StdVideoH264SequenceParameterSet *sps =
      vk_video_find_h264_enc_std_sps(cmd_buffer->video.params, pic->seq_parameter_set_id);
   const StdVideoH264PictureParameterSet *pps =
      vk_video_find_h264_enc_std_pps(cmd_buffer->video.params, pic->pic_parameter_set_id);
   const VkVideoEncodeH264NaluSliceInfoKHR *slice_info = &h264_picture_info->pNaluSliceEntries[0];

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.spec_misc_h264);
   RADEON_ENC_CS(pps->flags.constrained_intra_pred_flag); // constrained_intra_pred_flag
   RADEON_ENC_CS(pps->flags.entropy_coding_mode_flag);    // cabac enable
   RADEON_ENC_CS(slice_info->pStdSliceHeader->cabac_init_idc);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5)
      RADEON_ENC_CS(pps->flags.transform_8x8_mode_flag);
   RADEON_ENC_CS(1);                                          // half pel enabled
   RADEON_ENC_CS(1);                                          // quarter pel enabled
   RADEON_ENC_CS(cmd_buffer->video.vid->vk.h264.profile_idc); // profile_idc
   RADEON_ENC_CS(vk_video_get_h264_level(sps->level_idc));

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3) {
      RADEON_ENC_CS(1);                        // v3 b_picture_enabled
      RADEON_ENC_CS(pps->weighted_bipred_idc); // v3 weighted bipred idc
   }

   RADEON_ENC_END();
}

static void
radv_enc_spec_misc_hevc(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);
   const StdVideoEncodeH265PictureInfo *pic = h265_picture_info->pStdPictureInfo;
   const VkVideoEncodeH265NaluSliceSegmentInfoKHR *h265_slice = &h265_picture_info->pNaluSliceSegmentEntries[0];
   const StdVideoEncodeH265SliceSegmentHeader *slice = h265_slice->pStdSliceSegmentHeader;
   const StdVideoH265SequenceParameterSet *sps =
      vk_video_find_h265_enc_std_sps(cmd_buffer->video.params, pic->pps_seq_parameter_set_id);
   const StdVideoH265PictureParameterSet *pps =
      vk_video_find_h265_enc_std_pps(cmd_buffer->video.params, pic->pps_pic_parameter_set_id);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.spec_misc_hevc);
   RADEON_ENC_CS(sps->log2_min_luma_coding_block_size_minus3);
   RADEON_ENC_CS(!sps->flags.amp_enabled_flag);
   RADEON_ENC_CS(sps->flags.strong_intra_smoothing_enabled_flag);
   RADEON_ENC_CS(pps->flags.constrained_intra_pred_flag);
   RADEON_ENC_CS(slice->flags.cabac_init_flag);
   RADEON_ENC_CS(1); // enc->enc_pic.hevc_spec_misc.half_pel_enabled
   RADEON_ENC_CS(1); // enc->enc_pic.hevc_spec_misc.quarter_pel_enabled
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3) {
      RADEON_ENC_CS(!pps->flags.transform_skip_enabled_flag);
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5)
         RADEON_ENC_CS(0);
   }
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
      RADEON_ENC_CS(pps->flags.cu_qp_delta_enabled_flag);
   RADEON_ENC_END();
}

static void
radv_enc_slice_control_hevc(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);

   uint32_t width_in_ctb, height_in_ctb, num_ctbs_in_slice;

   width_in_ctb = DIV_ROUND_UP(enc_info->srcPictureResource.codedExtent.width, 64);
   height_in_ctb = DIV_ROUND_UP(enc_info->srcPictureResource.codedExtent.height, 64);
   num_ctbs_in_slice = DIV_ROUND_UP(width_in_ctb * height_in_ctb, h265_picture_info->naluSliceSegmentEntryCount);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.slice_control_hevc);
   RADEON_ENC_CS(RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_CTBS);
   RADEON_ENC_CS(num_ctbs_in_slice); // num_ctbs_in_slice
   RADEON_ENC_CS(num_ctbs_in_slice); // num_ctbs_in_slice_segment
   RADEON_ENC_END();
}

static int32_t
radv_enc_av1_get_relative_dist(uint32_t order_hint_bits_minus_1, uint32_t a, uint32_t b)
{
   uint32_t diff = a - b;
   uint32_t m = 1 << order_hint_bits_minus_1;
   diff = (diff & (m - 1)) - (diff & m);
   return diff;
}

static bool
radv_enc_av1_skip_mode_allowed(uint32_t order_hint_bits, uint32_t *ref_order_hint, uint32_t curr_order_hint,
                               uint32_t frames[2])
{
   int32_t forward_idx = -1, backward_idx = -1;
   uint32_t forward_hint = 0, backward_hint = 0;

   for (uint32_t i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++) {
      uint32_t ref_hint = ref_order_hint[i];
      int32_t dist = radv_enc_av1_get_relative_dist(order_hint_bits, ref_hint, curr_order_hint);
      if (dist < 0) {
         if (forward_idx < 0 || radv_enc_av1_get_relative_dist(order_hint_bits, ref_hint, forward_hint) > 0) {
            forward_idx = i;
            forward_hint = ref_hint;
         }
      } else if (dist > 0) {
         if (backward_idx < 0 || radv_enc_av1_get_relative_dist(order_hint_bits, ref_hint, backward_hint) < 0) {
            backward_idx = i;
            backward_hint = ref_hint;
         }
      }
   }

   if (forward_idx < 0)
      return false;

   if (backward_idx >= 0) {
      frames[0] = MIN2(forward_idx, backward_idx);
      frames[1] = MAX2(forward_idx, backward_idx);
      return true;
   }

   int32_t second_forward_idx = -1;
   uint32_t second_forward_hint;

   for (uint32_t i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++) {
      uint32_t ref_hint = ref_order_hint[i];
      if (radv_enc_av1_get_relative_dist(order_hint_bits, ref_hint, forward_hint) < 0) {
         if (second_forward_idx < 0 ||
             radv_enc_av1_get_relative_dist(order_hint_bits, ref_hint, second_forward_hint) > 0) {
            second_forward_idx = i;
            second_forward_hint = ref_hint;
         }
      }
   }

   if (second_forward_idx < 0)
      return false;

   frames[0] = MIN2(forward_idx, second_forward_idx);
   frames[1] = MAX2(forward_idx, second_forward_idx);
   return true;
}

static void
radv_enc_spec_misc_av1(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info,
                       struct radv_enc_av1_state *av1_state)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct vk_video_session_parameters *params = cmd_buffer->video.params;
   const struct VkVideoEncodeAV1PictureInfoKHR *av1_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_AV1_PICTURE_INFO_KHR);
   const StdVideoEncodeAV1PictureInfo *pic = av1_picture_info->pStdPictureInfo;
   const StdVideoAV1SequenceHeader *seq = &params->av1_enc.seq_hdr.base;
   VkExtent2D aligned_extent =
      radv_enc_aligned_coded_extent(pdev, vid->vk.op, enc_info->srcPictureResource.codedExtent);
   uint32_t sb_w = DIV_ROUND_UP(aligned_extent.width, 64);
   uint32_t sb_h = DIV_ROUND_UP(aligned_extent.height, 64);
   rvcn_enc_av1_tile_config_t *tile_config = &av1_state->tile_config;

   if (pic->pTileInfo) {
      /* 2 cols only supported for width > 4096. */
      if (aligned_extent.width <= 4096 && pic->pTileInfo->TileCols > 1) {
         tile_config->num_tile_cols = 1;
         tile_config->num_tile_rows = MIN2(pic->pTileInfo->TileRows * pic->pTileInfo->TileCols, sb_h);
         tile_config->uniform_tile_spacing = util_is_power_of_two_or_zero(tile_config->num_tile_rows);
      } else {
         tile_config->uniform_tile_spacing = pic->pTileInfo->flags.uniform_tile_spacing_flag;
         tile_config->num_tile_cols = pic->pTileInfo->TileCols;
         tile_config->num_tile_rows = pic->pTileInfo->TileRows;
         if (pic->pTileInfo->pWidthInSbsMinus1) {
            for (unsigned i = 0; i < pic->pTileInfo->TileCols; i++)
               tile_config->tile_widths[i] = pic->pTileInfo->pWidthInSbsMinus1[i] + 1;
         }
         if (pic->pTileInfo->pHeightInSbsMinus1) {
            for (unsigned i = 0; i < pic->pTileInfo->TileRows; i++)
               tile_config->tile_height[i] = pic->pTileInfo->pHeightInSbsMinus1[i] + 1;
         }
      }
      tile_config->context_update_tile_id = pic->pTileInfo->context_update_tile_id;
      tile_config->context_update_tile_id_mode = tile_config->context_update_tile_id == 0
                                                    ? RENCODE_AV1_CONTEXT_UPDATE_TILE_ID_MODE_DEFAULT
                                                    : RENCODE_AV1_CONTEXT_UPDATE_TILE_ID_MODE_CUSTOMIZED;
   } else {
      tile_config->num_tile_cols = aligned_extent.width > 4096 ? 2 : 1;
      uint32_t max_tile_width = DIV_ROUND_UP(aligned_extent.width, tile_config->num_tile_cols);
      uint32_t max_tile_height = (4096 * 2304) / max_tile_width;
      tile_config->num_tile_rows = DIV_ROUND_UP(aligned_extent.height, max_tile_height);
      tile_config->uniform_tile_spacing = util_is_power_of_two_or_zero(tile_config->num_tile_rows);
      tile_config->context_update_tile_id = 0;
      tile_config->context_update_tile_id_mode = RENCODE_AV1_CONTEXT_UPDATE_TILE_ID_MODE_DEFAULT;
   }

   if (tile_config->tile_widths[0] == 0) {
      uint32_t tile_w = DIV_ROUND_UP(sb_w, tile_config->num_tile_cols);
      if (tile_w * (tile_config->num_tile_cols - 1) >= sb_w) {
         tile_w = sb_w / tile_config->num_tile_cols;
         tile_config->uniform_tile_spacing = false;
      }
      for (unsigned i = 0; i < tile_config->num_tile_cols; i++) {
         if (i == tile_config->num_tile_cols - 1)
            tile_w = sb_w - (i * tile_w);
         tile_config->tile_widths[i] = tile_w;
      }
   }

   if (tile_config->tile_height[0] == 0) {
      uint32_t tile_h = DIV_ROUND_UP(sb_h, tile_config->num_tile_rows);
      if (tile_h * (tile_config->num_tile_rows - 1) >= sb_h) {
         tile_h = sb_h / tile_config->num_tile_rows;
         tile_config->uniform_tile_spacing = false;
      }
      for (unsigned i = 0; i < tile_config->num_tile_rows; i++) {
         if (i == tile_config->num_tile_rows - 1)
            tile_h = sb_h - (i * tile_h);
         tile_config->tile_height[i] = tile_h;
      }
   }

   tile_config->num_tile_groups = 1;
   tile_config->tile_groups[0].start = 0;
   tile_config->tile_groups[0].end = tile_config->num_tile_cols * tile_config->num_tile_rows - 1;
   tile_config->tile_size_bytes_minus_1 = 3;

   uint32_t precision = RENCODE_AV1_MV_PRECISION_DISALLOW_HIGH_PRECISION;
   if (pic->flags.allow_screen_content_tools && pic->flags.force_integer_mv)
      precision = RENCODE_AV1_MV_PRECISION_FORCE_INTEGER_MV;
   else if (pic->flags.allow_high_precision_mv)
      precision = RENCODE_AV1_MV_PRECISION_ALLOW_HIGH_PRECISION;

   av1_state->skip_mode_allowed =
      seq->flags.enable_order_hint &&
      av1_picture_info->predictionMode >= VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR;

   if (av1_state->skip_mode_allowed) {
      uint32_t skip_frames[2];
      uint32_t ref_order_hint[STD_VIDEO_AV1_REFS_PER_FRAME];
      for (unsigned i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++)
         ref_order_hint[i] = pic->ref_order_hint[pic->ref_frame_idx[i]];
      av1_state->skip_mode_allowed =
         radv_enc_av1_skip_mode_allowed(seq->order_hint_bits_minus_1, ref_order_hint, pic->order_hint, skip_frames);
      av1_state->disallow_skip_mode = !av1_state->skip_mode_allowed;
      /* Skip mode frames must match reference frames */
      if (av1_state->skip_mode_allowed) {
         av1_state->disallow_skip_mode = !pic->flags.skip_mode_present || skip_frames[0] != 0 ||
                                         av1_picture_info->referenceNameSlotIndices[skip_frames[1]] == -1;
      }
   } else {
      av1_state->disallow_skip_mode = 1;
   }

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.spec_misc_av1);
   RADEON_ENC_CS(pic->flags.allow_screen_content_tools);
   RADEON_ENC_CS(precision);
   RADEON_ENC_CS(seq->flags.enable_cdef);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
      if (seq->flags.enable_cdef) {
         RADEON_ENC_CS(pic->pCDEF->cdef_bits);
         RADEON_ENC_CS(pic->pCDEF->cdef_damping_minus_3);
         for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
            RADEON_ENC_CS(pic->pCDEF->cdef_y_pri_strength[i]);
         for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
            RADEON_ENC_CS(pic->pCDEF->cdef_y_sec_strength[i]);
         for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
            RADEON_ENC_CS(pic->pCDEF->cdef_uv_pri_strength[i]);
         for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
            RADEON_ENC_CS(pic->pCDEF->cdef_uv_sec_strength[i]);
      } else {
         for (int i = 0; i < (2 + 4 * RENCODE_AV1_CDEF_MAX_NUM); i++)
            RADEON_ENC_CS(0);
      }
      RADEON_ENC_CS(0); // allow intrabc
   }
   RADEON_ENC_CS(pic->flags.disable_cdf_update);
   RADEON_ENC_CS(pic->flags.disable_frame_end_update_cdf);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
      RADEON_ENC_CS(av1_state->disallow_skip_mode);
      RADEON_ENC_CS(pic->pQuantization ? pic->pQuantization->DeltaQYDc : 0);
      RADEON_ENC_CS(pic->pQuantization ? pic->pQuantization->DeltaQUDc : 0);
      RADEON_ENC_CS(pic->pQuantization ? pic->pQuantization->DeltaQUAc : 0);
      RADEON_ENC_CS(pic->pQuantization ? pic->pQuantization->DeltaQVDc : 0);
      RADEON_ENC_CS(pic->pQuantization ? pic->pQuantization->DeltaQVAc : 0);
   } else {
      RADEON_ENC_CS(tile_config->num_tile_cols * tile_config->num_tile_rows);
   }
   RADEON_ENC_CS(0); // enable screen content auto detection
   RADEON_ENC_CS(0); // screen content frame percentage threshold
   if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_5) {
      RADEON_ENC_CS(0xffffffff);
      RADEON_ENC_CS(0xffffffff);
   }
   RADEON_ENC_END();

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
      RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.tile_config_av1);
      RADEON_ENC_CS(tile_config->num_tile_cols);
      RADEON_ENC_CS(tile_config->num_tile_rows);
      for (int i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS; i++)
         RADEON_ENC_CS(tile_config->tile_widths[i]);
      for (int i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS; i++)
         RADEON_ENC_CS(tile_config->tile_height[i]);
      RADEON_ENC_CS(tile_config->num_tile_groups);
      for (int i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS * RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS; i++) {
         RADEON_ENC_CS(tile_config->tile_groups[i].start);
         RADEON_ENC_CS(tile_config->tile_groups[i].end);
      }
      RADEON_ENC_CS(tile_config->context_update_tile_id_mode);
      RADEON_ENC_CS(tile_config->context_update_tile_id);
      RADEON_ENC_CS(tile_config->tile_size_bytes_minus_1);
      RADEON_ENC_END();
   }
}

static void
radv_enc_rc_session_init(struct radv_cmd_buffer *cmd_buffer, uint32_t vbv_buffer_level)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.rc_session_init);
   RADEON_ENC_CS(radv_enc_rate_control_method(cmd_buffer->video.enc.rate_control_mode));
   RADEON_ENC_CS(vbv_buffer_level);
   RADEON_ENC_END();
}

static void
radv_enc_rc_layer_init(struct radv_cmd_buffer *cmd_buffer, rvcn_enc_rate_ctl_layer_init_t *layer_init)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.rc_layer_init);
   RADEON_ENC_CS(layer_init->target_bit_rate);                  // target bit rate
   RADEON_ENC_CS(layer_init->peak_bit_rate);                    // peak bit rate
   RADEON_ENC_CS(layer_init->frame_rate_num);                   // frame rate num
   RADEON_ENC_CS(layer_init->frame_rate_den);                   // frame rate dem
   RADEON_ENC_CS(layer_init->vbv_buffer_size);                  // vbv buffer size
   RADEON_ENC_CS(layer_init->avg_target_bits_per_picture);      // avg target bits per picture
   RADEON_ENC_CS(layer_init->peak_bits_per_picture_integer);    // peak bit per picture int
   RADEON_ENC_CS(layer_init->peak_bits_per_picture_fractional); // peak bit per picture fract
   RADEON_ENC_END();
}

static void
radv_enc_deblocking_filter_h264(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);
   const VkVideoEncodeH264NaluSliceInfoKHR *h264_slice = &h264_picture_info->pNaluSliceEntries[0];
   const StdVideoEncodeH264SliceHeader *slice = h264_slice->pStdSliceHeader;
   const StdVideoEncodeH264PictureInfo *pic = h264_picture_info->pStdPictureInfo;
   const StdVideoH264PictureParameterSet *pps =
      vk_video_find_h264_enc_std_pps(cmd_buffer->video.params, pic->pic_parameter_set_id);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.deblocking_filter_h264);
   RADEON_ENC_CS(slice->disable_deblocking_filter_idc);
   RADEON_ENC_CS(slice->slice_alpha_c0_offset_div2);
   RADEON_ENC_CS(slice->slice_beta_offset_div2);
   RADEON_ENC_CS(pps->chroma_qp_index_offset);
   RADEON_ENC_CS(pps->second_chroma_qp_index_offset);
   RADEON_ENC_END();
}

static void
radv_enc_deblocking_filter_hevc(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);
   const StdVideoEncodeH265PictureInfo *pic = h265_picture_info->pStdPictureInfo;
   const VkVideoEncodeH265NaluSliceSegmentInfoKHR *h265_slice = &h265_picture_info->pNaluSliceSegmentEntries[0];
   const StdVideoEncodeH265SliceSegmentHeader *slice = h265_slice->pStdSliceSegmentHeader;
   const StdVideoH265SequenceParameterSet *sps =
      vk_video_find_h265_enc_std_sps(cmd_buffer->video.params, pic->pps_seq_parameter_set_id);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.deblocking_filter_hevc);
   RADEON_ENC_CS(slice->flags.slice_loop_filter_across_slices_enabled_flag);
   RADEON_ENC_CS(slice->flags.slice_deblocking_filter_disabled_flag);
   RADEON_ENC_CS(slice->slice_beta_offset_div2);
   RADEON_ENC_CS(slice->slice_tc_offset_div2);
   RADEON_ENC_CS(slice->slice_cb_qp_offset);
   RADEON_ENC_CS(slice->slice_cr_qp_offset);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
      RADEON_ENC_CS(!sps->flags.sample_adaptive_offset_enabled_flag);
   RADEON_ENC_END();
}

static void
radv_enc_quality_params(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.quality_params);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
      RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void
radv_enc_latency(struct radv_cmd_buffer *cmd_buffer, VkVideoEncodeTuningModeKHR tuning_mode)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const bool low_latency = tuning_mode == VK_VIDEO_ENCODE_TUNING_MODE_LOW_LATENCY_KHR ||
                            tuning_mode == VK_VIDEO_ENCODE_TUNING_MODE_ULTRA_LOW_LATENCY_KHR;

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.enc_latency);
   RADEON_ENC_CS(low_latency ? 1000 : 0);
   RADEON_ENC_END();
}

static void
radv_enc_slice_header(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info,
                      unsigned num_active_l0_refs_minus1)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   uint32_t instruction[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   uint32_t num_bits[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);
   const StdVideoEncodeH264PictureInfo *pic = h264_picture_info->pStdPictureInfo;
   const StdVideoH264SequenceParameterSet *sps =
      vk_video_find_h264_enc_std_sps(cmd_buffer->video.params, pic->seq_parameter_set_id);
   const StdVideoH264PictureParameterSet *pps =
      vk_video_find_h264_enc_std_pps(cmd_buffer->video.params, pic->pic_parameter_set_id);
   const VkVideoEncodeH264NaluSliceInfoKHR *slice_info = &h264_picture_info->pNaluSliceEntries[0];

   unsigned int inst_index = 0;
   unsigned int cdw_start = 0;
   unsigned int cdw_filled = 0;
   unsigned int bits_copied = 0;

   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.slice_header);
   radv_enc_reset(cmd_buffer);
   radv_enc_set_emulation_prevention(cmd_buffer, false);

   cdw_start = cs->b->cdw;

   if (pic->flags.IdrPicFlag)
      radv_enc_code_fixed_bits(cmd_buffer, 0x65, 8);
   else if (!pic->flags.is_reference)
      radv_enc_code_fixed_bits(cmd_buffer, 0x01, 8);
   else
      radv_enc_code_fixed_bits(cmd_buffer, 0x41, 8);

   radv_enc_flush_headers(cmd_buffer);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = enc->bits_output - bits_copied;
   bits_copied = enc->bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_H264_HEADER_INSTRUCTION_FIRST_MB;
   inst_index++;

   switch (pic->primary_pic_type) {
   case STD_VIDEO_H264_PICTURE_TYPE_I:
   case STD_VIDEO_H264_PICTURE_TYPE_IDR:
   default:
      radv_enc_code_ue(cmd_buffer, 7);
      break;
   case STD_VIDEO_H264_PICTURE_TYPE_P:
      radv_enc_code_ue(cmd_buffer, 5);
      break;
   case STD_VIDEO_H264_PICTURE_TYPE_B:
      radv_enc_code_ue(cmd_buffer, 6);
      break;
   }
   radv_enc_code_ue(cmd_buffer, 0x0);

   unsigned int max_frame_num_bits = sps->log2_max_frame_num_minus4 + 4;
   radv_enc_code_fixed_bits(cmd_buffer, pic->frame_num % (1 << max_frame_num_bits), max_frame_num_bits);

   if (pic->flags.IdrPicFlag)
      radv_enc_code_ue(cmd_buffer, pic->idr_pic_id);

   if (sps->pic_order_cnt_type == STD_VIDEO_H264_POC_TYPE_0) {
      unsigned int max_poc_bits = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
      radv_enc_code_fixed_bits(cmd_buffer, pic->PicOrderCnt % (1 << max_poc_bits), max_poc_bits);
   }

   if (pps->flags.redundant_pic_cnt_present_flag)
      radv_enc_code_ue(cmd_buffer, 0);

   if (pic->primary_pic_type == STD_VIDEO_H264_PICTURE_TYPE_B) {
      radv_enc_code_fixed_bits(cmd_buffer, 1, 1); /* direct_spatial_mv_pred */
   }
   const StdVideoEncodeH264ReferenceListsInfo *ref_lists = pic->pRefLists;
   /* ref_pic_list_modification() */
   if (pic->primary_pic_type != STD_VIDEO_H264_PICTURE_TYPE_IDR &&
       pic->primary_pic_type != STD_VIDEO_H264_PICTURE_TYPE_I) {

      /* num ref idx active override flag
       * only override if it's different from the PPS value */
      bool ref_idx_active_override_flag = num_active_l0_refs_minus1 != pps->num_ref_idx_l0_default_active_minus1;
      radv_enc_code_fixed_bits(cmd_buffer, ref_idx_active_override_flag, 1);
      if (ref_idx_active_override_flag) {
         radv_enc_code_ue(cmd_buffer, num_active_l0_refs_minus1);
         if (pic->primary_pic_type == STD_VIDEO_H264_PICTURE_TYPE_B)
            radv_enc_code_ue(cmd_buffer, 0 /* num_active_l1_refs_minus1 */);
      }

      radv_enc_code_fixed_bits(cmd_buffer, ref_lists->flags.ref_pic_list_modification_flag_l0, 1);
      if (ref_lists->flags.ref_pic_list_modification_flag_l0) {
         for (unsigned op = 0; op < MIN2(ref_lists->refList0ModOpCount, num_active_l0_refs_minus1 + 1); op++) {
            const StdVideoEncodeH264RefListModEntry *entry = &ref_lists->pRefList0ModOperations[op];

            radv_enc_code_ue(cmd_buffer, entry->modification_of_pic_nums_idc);
            if (entry->modification_of_pic_nums_idc ==
                   STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_SHORT_TERM_SUBTRACT ||
                entry->modification_of_pic_nums_idc == STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_SHORT_TERM_ADD)
               radv_enc_code_ue(cmd_buffer, entry->abs_diff_pic_num_minus1);
            else if (entry->modification_of_pic_nums_idc == STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_LONG_TERM)
               radv_enc_code_ue(cmd_buffer, entry->long_term_pic_num);
         }
      }

      if (pic->primary_pic_type == STD_VIDEO_H264_PICTURE_TYPE_B) {
         radv_enc_code_fixed_bits(cmd_buffer, ref_lists->flags.ref_pic_list_modification_flag_l1, 1);
         if (ref_lists->flags.ref_pic_list_modification_flag_l1) {
            for (unsigned op = 0; op < MIN2(ref_lists->refList1ModOpCount, /* num_active_l1_refs_minus1 + */ 1); op++) {
               const StdVideoEncodeH264RefListModEntry *entry = &ref_lists->pRefList1ModOperations[op];

               radv_enc_code_ue(cmd_buffer, entry->modification_of_pic_nums_idc);
               if (entry->modification_of_pic_nums_idc ==
                      STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_SHORT_TERM_SUBTRACT ||
                   entry->modification_of_pic_nums_idc == STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_SHORT_TERM_ADD)
                  radv_enc_code_ue(cmd_buffer, entry->abs_diff_pic_num_minus1);
               else if (entry->modification_of_pic_nums_idc == STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_LONG_TERM)
                  radv_enc_code_ue(cmd_buffer, entry->long_term_pic_num);
            }
         }
      }
   }

   if (pic->flags.IdrPicFlag) {
      radv_enc_code_fixed_bits(cmd_buffer, 0x0, 1);
      radv_enc_code_fixed_bits(cmd_buffer, pic->flags.long_term_reference_flag, 1); /* long_term_reference_flag */
   } else if (pic->flags.is_reference) {
      radv_enc_code_fixed_bits(cmd_buffer, ref_lists->refPicMarkingOpCount > 0 ? 1 : 0, 1);
      for (unsigned op = 0; op < ref_lists->refPicMarkingOpCount; op++) {
         const StdVideoEncodeH264RefPicMarkingEntry *entry = &ref_lists->pRefPicMarkingOperations[op];
         radv_enc_code_ue(cmd_buffer, entry->memory_management_control_operation);
         if (entry->memory_management_control_operation == STD_VIDEO_H264_MEM_MGMT_CONTROL_OP_UNMARK_SHORT_TERM ||
             entry->memory_management_control_operation == STD_VIDEO_H264_MEM_MGMT_CONTROL_OP_MARK_LONG_TERM)
            radv_enc_code_ue(cmd_buffer, entry->difference_of_pic_nums_minus1);
         if (entry->memory_management_control_operation == STD_VIDEO_H264_MEM_MGMT_CONTROL_OP_UNMARK_LONG_TERM)
            radv_enc_code_ue(cmd_buffer, entry->long_term_pic_num);
         if (entry->memory_management_control_operation == STD_VIDEO_H264_MEM_MGMT_CONTROL_OP_MARK_LONG_TERM ||
             entry->memory_management_control_operation == STD_VIDEO_H264_MEM_MGMT_CONTROL_OP_MARK_CURRENT_AS_LONG_TERM)
            radv_enc_code_ue(cmd_buffer, entry->long_term_frame_idx);
         if (entry->memory_management_control_operation == STD_VIDEO_H264_MEM_MGMT_CONTROL_OP_SET_MAX_LONG_TERM_INDEX)
            radv_enc_code_ue(cmd_buffer, entry->max_long_term_frame_idx_plus1);
         if (entry->memory_management_control_operation == STD_VIDEO_H264_MEM_MGMT_CONTROL_OP_END)
            break;
      }
   }

   if (pic->primary_pic_type != STD_VIDEO_H264_PICTURE_TYPE_IDR &&
       pic->primary_pic_type != STD_VIDEO_H264_PICTURE_TYPE_I && pps->flags.entropy_coding_mode_flag)
      radv_enc_code_ue(cmd_buffer, slice_info->pStdSliceHeader->cabac_init_idc);

   radv_enc_flush_headers(cmd_buffer);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = enc->bits_output - bits_copied;
   bits_copied = enc->bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_H264_HEADER_INSTRUCTION_SLICE_QP_DELTA;
   inst_index++;

   if (pps->flags.deblocking_filter_control_present_flag) {
      radv_enc_code_ue(cmd_buffer, slice_info->pStdSliceHeader->disable_deblocking_filter_idc);
      if (!slice_info->pStdSliceHeader->disable_deblocking_filter_idc) {
         radv_enc_code_se(cmd_buffer, slice_info->pStdSliceHeader->slice_alpha_c0_offset_div2);
         radv_enc_code_se(cmd_buffer, slice_info->pStdSliceHeader->slice_beta_offset_div2);
      }
   }

   radv_enc_flush_headers(cmd_buffer);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = enc->bits_output - bits_copied;
   bits_copied = enc->bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_END;

   cdw_filled = cs->b->cdw - cdw_start;
   for (int i = 0; i < RENCODE_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS - cdw_filled; i++)
      RADEON_ENC_CS(0x00000000);
   for (int j = 0; j < RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS; j++) {
      RADEON_ENC_CS(instruction[j]);
      RADEON_ENC_CS(num_bits[j]);
   }
   RADEON_ENC_END();
}

static unsigned int
radv_enc_hevc_st_ref_pic_set(struct radv_cmd_buffer *cmd_buffer, const StdVideoH265SequenceParameterSet *sps,
                             const StdVideoH265ShortTermRefPicSet *rps)
{
   const StdVideoH265ShortTermRefPicSet *ref_rps;
   unsigned num_pic_total_curr = 0;
   unsigned int num_short_term_ref_pic_sets = sps->num_short_term_ref_pic_sets;
   unsigned int index = num_short_term_ref_pic_sets;

   if (!rps)
      return 0;

   if (index != 0)
      radv_enc_code_fixed_bits(cmd_buffer, rps->flags.inter_ref_pic_set_prediction_flag, 0x1);

   if (rps->flags.inter_ref_pic_set_prediction_flag) {
      /* in the slice case this is always true, but leave here to make spec alignment easier */
      if (index == num_short_term_ref_pic_sets)
         radv_enc_code_ue(cmd_buffer, rps->delta_idx_minus1);
      radv_enc_code_fixed_bits(cmd_buffer, rps->flags.delta_rps_sign, 0x1);
      radv_enc_code_ue(cmd_buffer, rps->abs_delta_rps_minus1);

      unsigned ref_rps_idx = index - (rps->delta_idx_minus1 + 1);

      if (ref_rps_idx == num_short_term_ref_pic_sets) {
         ref_rps = rps;
      } else {
         ref_rps = &sps->pShortTermRefPicSet[ref_rps_idx];
      }

      for (unsigned i = 0; i <= (ref_rps->num_negative_pics + ref_rps->num_positive_pics); i++) {
         radv_enc_code_fixed_bits(cmd_buffer, !!(rps->used_by_curr_pic_flag & (1 << i)), 0x1);
         if (!(rps->used_by_curr_pic_flag & (1 << i))) {
            radv_enc_code_fixed_bits(cmd_buffer, !!(rps->use_delta_flag & (1 << i)), 0x1);
         }
      }
   } else {
      radv_enc_code_ue(cmd_buffer, rps->num_negative_pics);
      radv_enc_code_ue(cmd_buffer, rps->num_positive_pics);

      for (int i = 0; i < rps->num_negative_pics; i++) {
         radv_enc_code_ue(cmd_buffer, rps->delta_poc_s0_minus1[i]);
         radv_enc_code_fixed_bits(cmd_buffer, !!(rps->used_by_curr_pic_s0_flag & (1 << i)), 0x1);
         if (rps->used_by_curr_pic_s0_flag & (1 << i))
            num_pic_total_curr++;
      }
      for (int i = 0; i < rps->num_positive_pics; i++) {
         radv_enc_code_ue(cmd_buffer, rps->delta_poc_s1_minus1[i]);
         radv_enc_code_fixed_bits(cmd_buffer, !!(rps->used_by_curr_pic_s1_flag & (1 << i)), 0x1);
         if (rps->used_by_curr_pic_s1_flag & (1 << i))
            num_pic_total_curr++;
      }
   }
   return num_pic_total_curr;
}

static void
radv_enc_slice_header_hevc(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   uint32_t instruction[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   uint32_t num_bits[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);
   const StdVideoEncodeH265PictureInfo *pic = h265_picture_info->pStdPictureInfo;
   const VkVideoEncodeH265NaluSliceSegmentInfoKHR *h265_slice = &h265_picture_info->pNaluSliceSegmentEntries[0];
   const StdVideoEncodeH265SliceSegmentHeader *slice = h265_slice->pStdSliceSegmentHeader;
   const StdVideoH265SequenceParameterSet *sps =
      vk_video_find_h265_enc_std_sps(cmd_buffer->video.params, pic->pps_seq_parameter_set_id);
   const StdVideoH265PictureParameterSet *pps =
      vk_video_find_h265_enc_std_pps(cmd_buffer->video.params, pic->pps_pic_parameter_set_id);
   unsigned int inst_index = 0;
   unsigned int cdw_start = 0;
   unsigned int cdw_filled = 0;
   unsigned int bits_copied = 0;
   unsigned int num_pic_total_curr = 0;
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   unsigned nal_unit_type = vk_video_get_h265_nal_unit(pic);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.slice_header);
   radv_enc_reset(cmd_buffer);
   radv_enc_set_emulation_prevention(cmd_buffer, false);

   cdw_start = cs->b->cdw;
   radv_enc_code_fixed_bits(cmd_buffer, 0x0, 1);
   radv_enc_code_fixed_bits(cmd_buffer, nal_unit_type, 6);
   radv_enc_code_fixed_bits(cmd_buffer, 0x0, 6);
   radv_enc_code_fixed_bits(cmd_buffer, pic->TemporalId + 1, 3);

   radv_enc_flush_headers(cmd_buffer);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = enc->bits_output - bits_copied;
   bits_copied = enc->bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_FIRST_SLICE;
   inst_index++;

   if ((nal_unit_type >= 16) && (nal_unit_type <= 23))
      radv_enc_code_fixed_bits(cmd_buffer, pic->flags.no_output_of_prior_pics_flag, 1);

   radv_enc_code_ue(cmd_buffer, pic->pps_pic_parameter_set_id);

   radv_enc_flush_headers(cmd_buffer);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = enc->bits_output - bits_copied;
   bits_copied = enc->bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_SEGMENT;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_DEPENDENT_SLICE_END;
   inst_index++;

   /* slice_type */
   switch (pic->pic_type) {
   case STD_VIDEO_H265_PICTURE_TYPE_I:
   case STD_VIDEO_H265_PICTURE_TYPE_IDR:
      radv_enc_code_ue(cmd_buffer, 0x2);
      break;
   case STD_VIDEO_H265_PICTURE_TYPE_P:
      radv_enc_code_ue(cmd_buffer, 0x1);
      break;
   case STD_VIDEO_H265_PICTURE_TYPE_B:
      radv_enc_code_ue(cmd_buffer, 0x0);
      break;
   default:
      radv_enc_code_ue(cmd_buffer, 0x1);
   }

   if (pps->flags.output_flag_present_flag)
      radv_enc_code_fixed_bits(cmd_buffer, pic->flags.pic_output_flag, 1);

   if ((nal_unit_type != 19) && nal_unit_type != 20) {
      /* slice_pic_order_cnt_lsb */
      unsigned int max_poc_bits = sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
      radv_enc_code_fixed_bits(cmd_buffer, pic->PicOrderCntVal % (1 << max_poc_bits), max_poc_bits);
      radv_enc_code_fixed_bits(cmd_buffer, pic->flags.short_term_ref_pic_set_sps_flag, 0x1);
      if (!pic->flags.short_term_ref_pic_set_sps_flag) {
         num_pic_total_curr = radv_enc_hevc_st_ref_pic_set(cmd_buffer, sps, pic->pShortTermRefPicSet);
      } else if (sps->num_short_term_ref_pic_sets > 1) {
         radv_enc_code_fixed_bits(cmd_buffer, pic->short_term_ref_pic_set_idx,
                                  util_logbase2_ceil(sps->num_short_term_ref_pic_sets));
      }

      if (sps->flags.long_term_ref_pics_present_flag) {
         const StdVideoEncodeH265LongTermRefPics *lt = pic->pLongTermRefPics;
         if (sps->num_long_term_ref_pics_sps > 0)
            radv_enc_code_ue(cmd_buffer, lt->num_long_term_sps);
         radv_enc_code_ue(cmd_buffer, lt->num_long_term_pics);
         for (unsigned i = 0; i < lt->num_long_term_sps + lt->num_long_term_pics; i++) {
            if (i < lt->num_long_term_sps) {
               if (sps->num_long_term_ref_pics_sps > 1)
                  radv_enc_code_fixed_bits(cmd_buffer, lt->lt_idx_sps[i],
                                           util_logbase2_ceil(sps->num_long_term_ref_pics_sps));
            } else {
               radv_enc_code_fixed_bits(cmd_buffer, lt->poc_lsb_lt[i], sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
               radv_enc_code_fixed_bits(cmd_buffer, !!(lt->used_by_curr_pic_lt_flag & (1 << i)), 1);
               if (lt->used_by_curr_pic_lt_flag & (1 << i))
                  num_pic_total_curr++;
            }
            radv_enc_code_fixed_bits(cmd_buffer, lt->delta_poc_msb_present_flag[i], 1);
            if (lt->delta_poc_msb_present_flag[i])
               radv_enc_code_ue(cmd_buffer, lt->delta_poc_msb_cycle_lt[i]);
         }
      }

      if (sps->flags.sps_temporal_mvp_enabled_flag)
         radv_enc_code_fixed_bits(cmd_buffer, pic->flags.slice_temporal_mvp_enabled_flag, 1);
   }

   if (sps->flags.sample_adaptive_offset_enabled_flag) {
      radv_enc_flush_headers(cmd_buffer);
      instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
      num_bits[inst_index] = enc->bits_output - bits_copied;
      bits_copied = enc->bits_output;
      inst_index++;

      instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_SAO_ENABLE;
      inst_index++;
   }

   if ((pic->pic_type == STD_VIDEO_H265_PICTURE_TYPE_P) || (pic->pic_type == STD_VIDEO_H265_PICTURE_TYPE_B)) {
      /* it never has to be 1 since we only support one L0 pic */
      radv_enc_code_fixed_bits(cmd_buffer, 0 /* slice->flags.num_ref_idx_active_override_flag */, 1);

      if (pps->flags.lists_modification_present_flag && num_pic_total_curr > 1) {
         const StdVideoEncodeH265ReferenceListsInfo *rl = pic->pRefLists;
         unsigned num_pic_bits = util_logbase2_ceil(num_pic_total_curr);
         unsigned num_ref_l0_minus1 = 0;
         radv_enc_code_fixed_bits(cmd_buffer, rl->flags.ref_pic_list_modification_flag_l0, 1);
         for (unsigned i = 0; i <= num_ref_l0_minus1; i++)
            radv_enc_code_fixed_bits(cmd_buffer, rl->list_entry_l0[i], num_pic_bits);
         if (pic->pic_type == STD_VIDEO_H265_PICTURE_TYPE_B) {
            unsigned num_ref_l1_minus1 = 0;
            radv_enc_code_fixed_bits(cmd_buffer, rl->flags.ref_pic_list_modification_flag_l1, 1);
            for (unsigned i = 0; i <= num_ref_l1_minus1; i++)
               radv_enc_code_fixed_bits(cmd_buffer, rl->list_entry_l1[i], num_pic_bits);
         }
      }
      if (pic->pic_type == STD_VIDEO_H265_PICTURE_TYPE_B)
         radv_enc_code_fixed_bits(cmd_buffer, slice->flags.mvd_l1_zero_flag, 1);
      if (pps->flags.cabac_init_present_flag)
         radv_enc_code_fixed_bits(cmd_buffer, slice->flags.cabac_init_flag, 1);
      if (pic->flags.slice_temporal_mvp_enabled_flag) {
         if (pic->pic_type == STD_VIDEO_H265_PICTURE_TYPE_B)
            radv_enc_code_fixed_bits(cmd_buffer, slice->flags.collocated_from_l0_flag, 1);
      }
      radv_enc_code_ue(cmd_buffer, 5 - slice->MaxNumMergeCand);
   }

   radv_enc_flush_headers(cmd_buffer);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = enc->bits_output - bits_copied;
   bits_copied = enc->bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_QP_DELTA;
   inst_index++;

   if (pps->flags.pps_slice_chroma_qp_offsets_present_flag) {
      radv_enc_code_se(cmd_buffer, slice->slice_cb_qp_offset);
      radv_enc_code_se(cmd_buffer, slice->slice_cr_qp_offset);
   }

   if (pps->flags.pps_slice_act_qp_offsets_present_flag) {
      radv_enc_code_se(cmd_buffer, slice->slice_act_y_qp_offset);
      radv_enc_code_se(cmd_buffer, slice->slice_act_cb_qp_offset);
      radv_enc_code_se(cmd_buffer, slice->slice_act_cr_qp_offset);
   }

   if (pps->flags.chroma_qp_offset_list_enabled_flag) {
      radv_enc_code_fixed_bits(cmd_buffer, slice->flags.cu_chroma_qp_offset_enabled_flag, 1);
   }

   if (pps->flags.deblocking_filter_override_enabled_flag) {
      radv_enc_code_fixed_bits(cmd_buffer, slice->flags.deblocking_filter_override_flag, 1);
      if (slice->flags.deblocking_filter_override_flag) {
         radv_enc_code_fixed_bits(cmd_buffer, slice->flags.slice_deblocking_filter_disabled_flag, 1);
         if (!slice->flags.slice_deblocking_filter_disabled_flag) {
            radv_enc_code_se(cmd_buffer, slice->slice_beta_offset_div2);
            radv_enc_code_se(cmd_buffer, slice->slice_tc_offset_div2);
         }
      }
   }
   if ((pps->flags.pps_loop_filter_across_slices_enabled_flag) &&
       (!slice->flags.slice_deblocking_filter_disabled_flag || slice->flags.slice_sao_luma_flag ||
        slice->flags.slice_sao_chroma_flag)) {

      if (slice->flags.slice_sao_luma_flag || slice->flags.slice_sao_chroma_flag) {
         instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_LOOP_FILTER_ACROSS_SLICES_ENABLE;
         inst_index++;
      } else {
         radv_enc_code_fixed_bits(cmd_buffer, slice->flags.slice_loop_filter_across_slices_enabled_flag, 1);
         radv_enc_flush_headers(cmd_buffer);
         instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
         num_bits[inst_index] = enc->bits_output - bits_copied;
         bits_copied = enc->bits_output;
         inst_index++;
      }
   }

   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_END;

   cdw_filled = cs->b->cdw - cdw_start;
   for (int i = 0; i < RENCODE_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS - cdw_filled; i++)
      RADEON_ENC_CS(0x00000000);
   for (int j = 0; j < RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS; j++) {
      RADEON_ENC_CS(instruction[j]);
      RADEON_ENC_CS(num_bits[j]);
   }
   RADEON_ENC_END();
}

static void
dpb_image_sizes(struct radv_image *image, uint32_t *luma_pitch, uint32_t *luma_size, uint32_t *chroma_size,
                uint32_t *colloc_bytes)
{
   uint32_t rec_alignment = 64;
   uint32_t aligned_width = align(image->vk.extent.width, rec_alignment);
   uint32_t aligned_height = align(image->vk.extent.height, rec_alignment);
   uint32_t pitch = align(aligned_width, ENC_ALIGNMENT);
   uint32_t aligned_dpb_height = MAX2(256, aligned_height);

   *luma_pitch = pitch;
   *luma_size = align(pitch * aligned_dpb_height, ENC_ALIGNMENT);
   *chroma_size = align(*luma_size / 2, ENC_ALIGNMENT);

   if (image->vk.format == VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 ||
       image->vk.format == VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16) {
      *luma_size *= 2;
      *chroma_size *= 2;
   }
   *colloc_bytes = (align((aligned_width / 16), 64) / 2) * (aligned_height / 16);
}

static void
radv_enc_ctx(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   struct radv_image_view *dpb_iv = NULL;
   struct radv_image *dpb = NULL;
   uint64_t va = 0;
   uint32_t luma_pitch = 0;
   int max_ref_slot_idx = 0;
   bool is_av1 = vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR;

   if (info->pSetupReferenceSlot) {
      dpb_iv = radv_image_view_from_handle(info->pSetupReferenceSlot->pPictureResource->imageViewBinding);
      if (info->pSetupReferenceSlot->slotIndex > max_ref_slot_idx)
         max_ref_slot_idx = info->pSetupReferenceSlot->slotIndex;
   }

   if (info->referenceSlotCount > 0) {
      dpb_iv = radv_image_view_from_handle(info->pReferenceSlots[0].pPictureResource->imageViewBinding);
      for (unsigned i = 0; i < info->referenceSlotCount; i++) {
         if (info->pReferenceSlots[i].slotIndex > max_ref_slot_idx)
            max_ref_slot_idx = info->pReferenceSlots[i].slotIndex;
      }
   }

   dpb = dpb_iv ? dpb_iv->image : vid->intra_only_dpb;
   assert(dpb);

   uint32_t luma_size = 0, chroma_size = 0, colloc_bytes = 0;
   dpb_image_sizes(dpb, &luma_pitch, &luma_size, &chroma_size, &colloc_bytes);
   radv_cs_add_buffer(device->ws, cs->b, dpb->bindings[0].bo);
   va = dpb->bindings[0].addr;

   uint32_t swizzle_mode = 0;

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_4)
      swizzle_mode = RENCODE_REC_SWIZZLE_MODE_256B_D;
   else if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
      swizzle_mode = RENCODE_REC_SWIZZLE_MODE_256B_S;

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.ctx);
   RADEON_ENC_CS(va >> 32);
   RADEON_ENC_CS(va & 0xffffffff);
   RADEON_ENC_CS(swizzle_mode);
   RADEON_ENC_CS(luma_pitch);           // rec_luma_pitch
   RADEON_ENC_CS(luma_pitch);           // rec_luma_pitch0); //rec_chromma_pitch
   RADEON_ENC_CS(max_ref_slot_idx + 1); // num_reconstructed_pictures

   int i;
   unsigned offset = 0;
   unsigned colloc_buffer_offset = 0;
   uint32_t sdb_frame_offset = offset;

   if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR && pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3) {
      colloc_buffer_offset = offset;
      offset += colloc_bytes;
   } else if (is_av1)
      offset += RENCODE_AV1_SDB_FRAME_CONTEXT_SIZE;

   for (i = 0; i < max_ref_slot_idx + 1; i++) {
      RADEON_ENC_CS(offset);
      offset += luma_size;
      RADEON_ENC_CS(offset);
      offset += chroma_size;

      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_4) {
         if (is_av1) {
            RADEON_ENC_CS(offset); /* unused offset 1 */
            offset += RENCODE_AV1_FRAME_CONTEXT_CDF_TABLE_SIZE;
            RADEON_ENC_CS(offset); /* unused offset 2 */
            offset += RENCODE_AV1_CDEF_ALGORITHM_FRAME_CONTEXT_SIZE;
         } else {
            RADEON_ENC_CS(0); /* unused offset 1 */
            RADEON_ENC_CS(0); /* unused offset 2 */
         }
      }
   }

   for (; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_4) {
         RADEON_ENC_CS(0); /* unused offset 1 */
         RADEON_ENC_CS(0); /* unused offset 2 */
      }
   }

   if (pdev->enc_hw_ver == RADV_VIDEO_ENC_HW_3) {
      RADEON_ENC_CS(colloc_buffer_offset);
   }
   RADEON_ENC_CS(0); // enc pic pre encode luma pitch
   RADEON_ENC_CS(0); // enc pic pre encode chroma pitch

   for (i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(0); // pre encode luma offset
      RADEON_ENC_CS(0); // pre encode chroma offset
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_4) {
         RADEON_ENC_CS(0); /* unused offset 1 */
         RADEON_ENC_CS(0); /* unused offset 2 */
      }
   }

   if (pdev->enc_hw_ver == RADV_VIDEO_ENC_HW_2) {
      RADEON_ENC_CS(0); // enc pic yuv luma offset
      RADEON_ENC_CS(0); // enc pic yuv chroma offset

      RADEON_ENC_CS(0); // two pass search center map offset

      // rgboffsets
      RADEON_ENC_CS(0); // red
      RADEON_ENC_CS(0); // green
      RADEON_ENC_CS(0); // blue
   } else if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3) {
      RADEON_ENC_CS(0); // red
      RADEON_ENC_CS(0); // green
      RADEON_ENC_CS(0); // blue
      RADEON_ENC_CS(0); // v3 two pass search center map offset
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_4) {
         if (is_av1) {
            RADEON_ENC_CS(sdb_frame_offset);
         } else {
            RADEON_ENC_CS(colloc_buffer_offset);
         }
      } else {
         RADEON_ENC_CS(0);
      }
      if (pdev->enc_hw_ver == RADV_VIDEO_ENC_HW_3) {
         RADEON_ENC_CS(0);
      }
   }
   RADEON_ENC_END();
}

static void
radv_enc_ctx2(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   uint32_t luma_pitch = 0, luma_size = 0, chroma_size = 0, colloc_bytes = 0;
   int max_ref_slot_idx = 0;
   const VkVideoPictureResourceInfoKHR *slots[RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES] = {NULL};
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   bool intra_only_dpb = false;

   if (info->pSetupReferenceSlot) {
      max_ref_slot_idx = info->pSetupReferenceSlot->slotIndex;
      slots[info->pSetupReferenceSlot->slotIndex] = info->pSetupReferenceSlot->pPictureResource;
   } else if (vid->vk.max_dpb_slots == 0) {
      intra_only_dpb = true;
      slots[0] = &info->srcPictureResource;
   } else {
      /* Workaround for CTS bug dEQP-VK.video.encode.h264.i_p_b_13_layered_src_general_layout:
       *   VUID-vkCmdEncodeVideoKHR-pEncodeInfo-08377
       *    pEncodeInfo->pSetupReferenceSlot must not be NULL unless the bound video session
       *    was created with VkVideoSessionCreateInfoKHR::maxDpbSlots equal to zero
       */
      slots[0] = info->pReferenceSlots[0].pPictureResource;
   }

   for (unsigned i = 0; i < info->referenceSlotCount; i++) {
      if (info->pReferenceSlots[i].slotIndex > max_ref_slot_idx)
         max_ref_slot_idx = info->pReferenceSlots[i].slotIndex;
      slots[info->pReferenceSlots[i].slotIndex] = info->pReferenceSlots[i].pPictureResource;
   }

   uint64_t va = 0;
   if (cmd_buffer->video.vid->ctx.mem) {
      va = radv_buffer_get_va(cmd_buffer->video.vid->ctx.mem->bo);
      va += cmd_buffer->video.vid->ctx.offset + VCN_ENC_AV1_DEFAULT_CDF_SIZE;
   }

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.ctx);
   RADEON_ENC_CS(va >> 32);
   RADEON_ENC_CS(va & 0xffffffff);
   RADEON_ENC_CS(max_ref_slot_idx + 1); // num_reconstructed_pictures

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      const VkVideoPictureResourceInfoKHR *res = slots[i];
      if (!res) {
         for (int j = 0; j < 15; j++)
            RADEON_ENC_CS(0);
         continue;
      }

      struct radv_image_view *dpb_iv = radv_image_view_from_handle(res->imageViewBinding);
      assert(dpb_iv != NULL);
      struct radv_image *dpb_img = intra_only_dpb ? vid->intra_only_dpb : dpb_iv->image;
      radv_cs_add_buffer(device->ws, cs->b, dpb_img->bindings[0].bo);
      dpb_image_sizes(dpb_img, &luma_pitch, &luma_size, &chroma_size, &colloc_bytes);

      uint32_t metadata_size = RENCODE_MAX_METADATA_BUFFER_SIZE_PER_FRAME;
      if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) {
         metadata_size += colloc_bytes;
      } else if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR) {
         metadata_size += RENCODE_AV1_FRAME_CONTEXT_CDF_TABLE_SIZE;
         metadata_size += RENCODE_AV1_CDEF_ALGORITHM_FRAME_CONTEXT_SIZE;
      }
      metadata_size = align(metadata_size, ENC_ALIGNMENT);

      uint32_t dpb_array_idx = !intra_only_dpb ? res->baseArrayLayer + dpb_iv->vk.base_array_layer : 0;
      uint64_t luma_va = dpb_img->bindings[0].addr + dpb_array_idx * (luma_size + chroma_size + metadata_size);
      uint64_t chroma_va = luma_va + luma_size;
      uint64_t fcb_va = chroma_va + chroma_size;

      RADEON_ENC_CS(luma_va >> 32);
      RADEON_ENC_CS(luma_va & 0xffffffff);
      RADEON_ENC_CS(luma_pitch);
      RADEON_ENC_CS(chroma_va >> 32);
      RADEON_ENC_CS(chroma_va & 0xffffffff);
      RADEON_ENC_CS(luma_pitch / 2);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(RENCODE_REC_SWIZZLE_MODE_256B_D_VCN5);
      RADEON_ENC_CS(fcb_va >> 32);
      RADEON_ENC_CS(fcb_va & 0xffffffff);
      RADEON_ENC_CS(RENCODE_MAX_METADATA_BUFFER_SIZE_PER_FRAME); // colloc/cdf offset
      RADEON_ENC_CS(RENCODE_MAX_METADATA_BUFFER_SIZE_PER_FRAME +
                    RENCODE_AV1_FRAME_CONTEXT_CDF_TABLE_SIZE); // cdef offset
      RADEON_ENC_CS(0);                                        // metadata offset
   }

   // pre-encode
   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES * 15; i++)
      RADEON_ENC_CS(0);

   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void
radv_enc_bitstream(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *buffer, VkDeviceSize offset)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   uint64_t va = vk_buffer_address(&buffer->vk, offset);
   radv_cs_add_buffer(device->ws, cs->b, buffer->bo);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.bitstream);
   RADEON_ENC_CS(RENCODE_REC_SWIZZLE_MODE_LINEAR);
   RADEON_ENC_CS(va >> 32);
   RADEON_ENC_CS(va & 0xffffffff);
   RADEON_ENC_CS(buffer->vk.size);
   RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void
radv_enc_feedback(struct radv_cmd_buffer *cmd_buffer, uint64_t feedback_query_va)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.feedback);
   RADEON_ENC_CS(RENCODE_FEEDBACK_BUFFER_MODE_LINEAR);
   RADEON_ENC_CS(feedback_query_va >> 32);
   RADEON_ENC_CS(feedback_query_va & 0xffffffff);
   RADEON_ENC_CS(16); // buffer_size
   RADEON_ENC_CS(40); // data_size
   RADEON_ENC_END();
}

static void
radv_enc_intra_refresh(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   const struct VkVideoEncodeIntraRefreshInfoKHR *intra_refresh =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_INTRA_REFRESH_INFO_KHR);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.intra_refresh);
   if (enc_info->flags & VK_VIDEO_ENCODE_INTRA_REFRESH_BIT_KHR && intra_refresh) {
      const uint32_t block_size = vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR ? 16 : 64;
      uint32_t size_in_blocks = 0;
      bool overlap = true;

      if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) {
         const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
            vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);
         const VkVideoEncodeH264NaluSliceInfoKHR *h264_slice = &h264_picture_info->pNaluSliceEntries[0];
         const StdVideoEncodeH264SliceHeader *slice = h264_slice->pStdSliceHeader;

         overlap = slice->disable_deblocking_filter_idc != STD_VIDEO_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLED;
      } else if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR) {
         const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
            vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);
         const VkVideoEncodeH265NaluSliceSegmentInfoKHR *h265_slice = &h265_picture_info->pNaluSliceSegmentEntries[0];
         const StdVideoEncodeH265SliceSegmentHeader *slice = h265_slice->pStdSliceSegmentHeader;

         overlap = slice->flags.slice_deblocking_filter_disabled_flag == 0;
      } /* AV1 always needs overlap */

      switch (vid->vk.intra_refresh_mode) {
      case VK_VIDEO_ENCODE_INTRA_REFRESH_MODE_BLOCK_BASED_BIT_KHR:
      case VK_VIDEO_ENCODE_INTRA_REFRESH_MODE_BLOCK_ROW_BASED_BIT_KHR:
         size_in_blocks = DIV_ROUND_UP(enc_info->srcPictureResource.codedExtent.height, block_size);
         RADEON_ENC_CS(RENCODE_INTRA_REFRESH_MODE_CTB_MB_ROWS);
         break;
      case VK_VIDEO_ENCODE_INTRA_REFRESH_MODE_BLOCK_COLUMN_BASED_BIT_KHR:
         size_in_blocks = DIV_ROUND_UP(enc_info->srcPictureResource.codedExtent.width, block_size);
         RADEON_ENC_CS(RENCODE_INTRA_REFRESH_MODE_CTB_MB_COLUMNS);
         break;
      default:
         assert(0);
         break;
      }

      const uint32_t region_size = DIV_ROUND_UP(size_in_blocks, intra_refresh->intraRefreshCycleDuration);
      RADEON_ENC_CS(region_size * intra_refresh->intraRefreshIndex);
      RADEON_ENC_CS(region_size + (overlap ? 1 : 0));
   } else {
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
   }
   RADEON_ENC_END();
}

static void
radv_enc_qp_map_input(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct VkVideoEncodeQuantizationMapInfoKHR *quantization_map_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_QUANTIZATION_MAP_INFO_KHR);
   const struct radv_image_view *qp_map_view =
      quantization_map_info ? radv_image_view_from_handle(quantization_map_info->quantizationMap) : NULL;
   const struct radv_image *qp_map = qp_map_view ? qp_map_view->image : NULL;
   struct radv_cmd_stream *cs = cmd_buffer->cs;

   if (!(enc_info->flags & VK_VIDEO_ENCODE_WITH_QUANTIZATION_DELTA_MAP_BIT_KHR) || !qp_map)
      return;

   const uint64_t va_in = qp_map->bindings[0].addr;
   radv_cs_add_buffer(device->ws, cs->b, qp_map->bindings[0].bo);

   const uint64_t va_out =
      radv_buffer_get_va(cmd_buffer->video.vid->qp_map.mem->bo) + cmd_buffer->video.vid->qp_map.offset;
   radv_cs_add_buffer(device->ws, cmd_buffer->cs->b, cmd_buffer->video.vid->qp_map.mem->bo);

   radv_vcn_sq_header(cs, &cmd_buffer->video.sq, RADEON_VCN_ENGINE_TYPE_COMMON);

   struct rvcn_cmn_engine_ib_package *ib_header = (struct rvcn_cmn_engine_ib_package *)&(cs->b->buf[cs->b->cdw]);
   ib_header->package_size =
      sizeof(struct rvcn_cmn_engine_ib_package) + sizeof(struct rvcn_cmn_engine_op_resolveinputparamlayout);
   cs->b->cdw++;
   ib_header->package_type = RADEON_VCN_IB_COMMON_OP_RESOLVEINPUTPARAMLAYOUT;
   cs->b->cdw++;

   struct rvcn_cmn_engine_op_resolveinputparamlayout *resolve_input =
      (struct rvcn_cmn_engine_op_resolveinputparamlayout *)&(cs->b->buf[cs->b->cdw]);
   resolve_input->map_type = RADEON_VCN_RESOLVE_INPUT_PARAM_LAYOUT_TYPE_QPMAP_INT16;
   resolve_input->map_width = qp_map->vk.extent.width;
   resolve_input->map_height = qp_map->vk.extent.height;
   resolve_input->input_buffer_address_lo = va_in & 0xffffffff;
   resolve_input->input_buffer_address_hi = va_in >> 32;
   resolve_input->input_buffer_pitch = qp_map->planes[0].surface.u.gfx9.surf_pitch;
   resolve_input->input_buffer_swizzle_mode = qp_map->planes[0].surface.u.gfx9.swizzle_mode;
   resolve_input->output_buffer_address_lo = va_out & 0xffffffff;
   resolve_input->output_buffer_address_hi = va_out >> 32;

   cs->b->cdw += sizeof(*resolve_input) / 4;
   radv_vcn_sq_tail(cs, &cmd_buffer->video.sq);
}

static void
radv_enc_qp_map(struct radv_cmd_buffer *cmd_buffer, const struct VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeQuantizationMapInfoKHR *quantiziation_map_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_QUANTIZATION_MAP_INFO_KHR);
   const struct radv_image_view *qp_map_view =
      quantiziation_map_info ? radv_image_view_from_handle(quantiziation_map_info->quantizationMap) : NULL;
   const struct radv_image *qp_map = qp_map_view ? qp_map_view->image : NULL;

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.enc_qp_map);
   if (enc_info->flags & VK_VIDEO_ENCODE_WITH_QUANTIZATION_DELTA_MAP_BIT_KHR && qp_map) {
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
         uint64_t va = radv_buffer_get_va(cmd_buffer->video.vid->qp_map.mem->bo);
         va += cmd_buffer->video.vid->qp_map.offset;
         RADEON_ENC_CS(RENCODE_QP_MAP_TYPE_DELTA);
         RADEON_ENC_CS(va >> 32);
         RADEON_ENC_CS(va & 0xffffffff);
         RADEON_ENC_CS(0);
      } else {
         uint32_t rc_method = radv_enc_rate_control_method(cmd_buffer->video.enc.rate_control_mode);
         const uint32_t qp_map_type =
            rc_method == RENCODE_RATE_CONTROL_METHOD_NONE ? RENCODE_QP_MAP_TYPE_DELTA : RENCODE_QP_MAP_TYPE_MAP_PA;
         radv_cs_add_buffer(device->ws, cmd_buffer->cs->b, qp_map->bindings[0].bo);
         const uint64_t va = qp_map->bindings[0].addr;
         RADEON_ENC_CS(qp_map_type);
         RADEON_ENC_CS(va >> 32);
         RADEON_ENC_CS(va & 0xffffffff);
         RADEON_ENC_CS(qp_map->planes[0].surface.u.gfx9.surf_pitch);
      }
   } else {
      RADEON_ENC_CS(RENCODE_QP_MAP_TYPE_NONE);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
   }
   RADEON_ENC_END();
}

static void
radv_enc_rc_per_pic(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info,
                    rvcn_enc_rate_ctl_per_picture_t *per_pic)
{
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   unsigned qp = per_pic->qp_i;

   if (enc_info && cmd_buffer->video.enc.rate_control_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
      switch (vid->vk.op) {
      case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
         const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
            vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);
         const VkVideoEncodeH264NaluSliceInfoKHR *h264_slice = &h264_picture_info->pNaluSliceEntries[0];
         qp = h264_slice->constantQp;
         break;
      }
      case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
         const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
            vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);
         const VkVideoEncodeH265NaluSliceSegmentInfoKHR *h265_slice = &h265_picture_info->pNaluSliceSegmentEntries[0];
         qp = h265_slice->constantQp;
         break;
      }
      case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
         const struct VkVideoEncodeAV1PictureInfoKHR *av1_picture_info =
            vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_AV1_PICTURE_INFO_KHR);
         qp = av1_picture_info->constantQIndex;
         break;
      }
      default:
         break;
      }
   }

   uint32_t cmd =
      pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5 ? pdev->vcn_enc_cmds.rc_per_pic : pdev->vcn_enc_cmds.rc_per_pic_ex;

   RADEON_ENC_BEGIN(cmd);
   RADEON_ENC_CS(qp); // qp_i
   RADEON_ENC_CS(qp); // qp_p
   RADEON_ENC_CS(qp); // qp_b
   RADEON_ENC_CS(per_pic->min_qp_i);
   RADEON_ENC_CS(per_pic->max_qp_i);
   RADEON_ENC_CS(per_pic->min_qp_p);
   RADEON_ENC_CS(per_pic->max_qp_p);
   RADEON_ENC_CS(per_pic->min_qp_b);
   RADEON_ENC_CS(per_pic->max_qp_b);
   RADEON_ENC_CS(per_pic->max_au_size_i);
   RADEON_ENC_CS(per_pic->max_au_size_p);
   RADEON_ENC_CS(per_pic->max_au_size_b);
   RADEON_ENC_CS(per_pic->enabled_filler_data);
   RADEON_ENC_CS(per_pic->skip_frame_enable);
   RADEON_ENC_CS(per_pic->enforce_hrd);
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3)
      RADEON_ENC_CS(0xFFFFFFFF); // qvbr_quality_level
   RADEON_ENC_END();
}

static void
radv_enc_params(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);
   const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);
   const struct VkVideoEncodeAV1PictureInfoKHR *av1_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_AV1_PICTURE_INFO_KHR);
   const StdVideoEncodeH264PictureInfo *h264_pic = h264_picture_info ? h264_picture_info->pStdPictureInfo : NULL;
   const StdVideoEncodeH265PictureInfo *h265_pic = h265_picture_info ? h265_picture_info->pStdPictureInfo : NULL;
   const StdVideoEncodeAV1PictureInfo *av1_pic = av1_picture_info ? av1_picture_info->pStdPictureInfo : NULL;
   struct radv_image_view *src_iv = radv_image_view_from_handle(enc_info->srcPictureResource.imageViewBinding);
   struct radv_image *src_img = src_iv->image;
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   uint32_t array_idx = enc_info->srcPictureResource.baseArrayLayer + src_iv->vk.base_array_layer;
   uint64_t va = src_img->bindings[0].addr;
   uint64_t luma_va = va + src_img->planes[0].surface.u.gfx9.surf_offset +
                      array_idx * src_img->planes[0].surface.u.gfx9.surf_slice_size;
   uint64_t chroma_va = src_img->plane_count > 1 ? (va + src_img->planes[1].surface.u.gfx9.surf_offset +
                                                    array_idx * src_img->planes[1].surface.u.gfx9.surf_slice_size)
                                                  : 0;
   uint32_t pic_type;
   unsigned int slot_idx = 0xffffffff;
   unsigned int max_layers = cmd_buffer->video.enc.rate_control_num_layers;

   radv_cs_add_buffer(device->ws, cs->b, src_img->bindings[0].bo);
   if (h264_pic) {
      switch (h264_pic->primary_pic_type) {
      case STD_VIDEO_H264_PICTURE_TYPE_P:
      case STD_VIDEO_H264_PICTURE_TYPE_B:
         slot_idx = h264_pic->pRefLists->RefPicList0[0];
         break;
      default:
         break;
      }
      pic_type = radv_enc_h264_pic_type(h264_pic->primary_pic_type);
      radv_enc_layer_select(cmd_buffer, MIN2(h264_pic->temporal_id, max_layers));
   } else if (h265_pic) {
      switch (h265_pic->pic_type) {
      case STD_VIDEO_H265_PICTURE_TYPE_P:
      case STD_VIDEO_H265_PICTURE_TYPE_B:
         slot_idx = h265_pic->pRefLists->RefPicList0[0];
         break;
      default:
         break;
      }
      pic_type = radv_enc_h265_pic_type(h265_pic->pic_type);
      radv_enc_layer_select(cmd_buffer, MIN2(h265_pic->TemporalId, max_layers));
   } else if (av1_pic) {
      switch (av1_pic->frame_type) {
      case STD_VIDEO_AV1_FRAME_TYPE_KEY:
      case STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY:
         pic_type = RENCODE_PICTURE_TYPE_I;
         break;
      default:
         if (av1_picture_info->predictionMode >= VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR)
            pic_type = RENCODE_PICTURE_TYPE_B;
         else
            pic_type = RENCODE_PICTURE_TYPE_P;
         slot_idx = av1_picture_info->referenceNameSlotIndices[0];
         break;
      }
      radv_enc_layer_select(cmd_buffer,
                            MIN2(av1_pic->pExtensionHeader ? av1_pic->pExtensionHeader->temporal_id : 0, max_layers));
   } else {
      assert(0);
      return;
   }

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.enc_params);
   RADEON_ENC_CS(pic_type);                 // pic type
   RADEON_ENC_CS(enc_info->dstBufferRange); // allowed max bitstream size
   RADEON_ENC_CS(luma_va >> 32);
   RADEON_ENC_CS(luma_va & 0xffffffff);
   RADEON_ENC_CS(chroma_va >> 32);
   RADEON_ENC_CS(chroma_va & 0xffffffff);
   RADEON_ENC_CS(src_img->planes[0].surface.u.gfx9.surf_pitch);   // luma pitch
   RADEON_ENC_CS(src_img->plane_count > 1 ? src_img->planes[1].surface.u.gfx9.surf_pitch : 0); // chroma pitch
   RADEON_ENC_CS(src_img->planes[0].surface.u.gfx9.swizzle_mode); // swizzle mode

   if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_5)
      RADEON_ENC_CS(slot_idx); // ref0_idx
   if (enc_info->pSetupReferenceSlot)
      RADEON_ENC_CS(enc_info->pSetupReferenceSlot->slotIndex); // reconstructed picture index
   else
      RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void
radv_enc_params_h264(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info,
                     uint32_t num_active_l0_refs)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);

   assert(h264_picture_info);

   unsigned slot_to_ref_idx[RADV_VIDEO_H264_MAX_DPB_SLOTS];
   memset(slot_to_ref_idx, 0xFF, sizeof(slot_to_ref_idx));
   for (unsigned idx = 0; idx < enc_info->referenceSlotCount; idx++) {
      slot_to_ref_idx[enc_info->pReferenceSlots[idx].slotIndex] = idx;
   }

   const StdVideoEncodeH264PictureInfo *h264_pic = h264_picture_info->pStdPictureInfo;
   unsigned slot_idx_0[2] = {0xffffffff, 0xffffffff};
   unsigned slot_idx_1 = 0xffffffff;
   const VkVideoEncodeH264DpbSlotInfoKHR *slot_info_0[2] = {NULL, NULL};
   const VkVideoEncodeH264DpbSlotInfoKHR *slot_info_1 = NULL;

   switch (h264_pic->primary_pic_type) {
   case STD_VIDEO_H264_PICTURE_TYPE_P:
      assert(num_active_l0_refs <= 2);
      for (unsigned i = 0; i < num_active_l0_refs; i++) {
         slot_idx_0[i] = h264_pic->pRefLists->RefPicList0[i];
         slot_info_0[i] = vk_find_struct_const(enc_info->pReferenceSlots[slot_to_ref_idx[slot_idx_0[i]]].pNext,
                                               VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR);
      }
      break;
   case STD_VIDEO_H264_PICTURE_TYPE_B:
      assert(num_active_l0_refs == 1);
      slot_idx_0[0] = h264_pic->pRefLists->RefPicList0[0];
      slot_idx_1 = h264_pic->pRefLists->RefPicList1[0];
      slot_info_0[0] = vk_find_struct_const(enc_info->pReferenceSlots[slot_to_ref_idx[slot_idx_0[0]]].pNext,
                                            VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR);
      slot_info_1 = vk_find_struct_const(enc_info->pReferenceSlots[slot_to_ref_idx[slot_idx_1]].pNext,
                                         VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR);
      break;
   default:
      break;
   }

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.enc_params_h264);

   if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_3) {
      RADEON_ENC_CS(RENCODE_H264_PICTURE_STRUCTURE_FRAME);
      RADEON_ENC_CS(RENCODE_H264_INTERLACING_MODE_PROGRESSIVE);
      RADEON_ENC_CS(RENCODE_H264_PICTURE_STRUCTURE_FRAME);
      RADEON_ENC_CS(0xffffffff); // reference_picture1_index
   } else if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_5) {
      RADEON_ENC_CS(RENCODE_H264_PICTURE_STRUCTURE_FRAME);
      RADEON_ENC_CS(h264_pic->PicOrderCnt);
      RADEON_ENC_CS(RENCODE_H264_INTERLACING_MODE_PROGRESSIVE);
      if (slot_info_0[0]) {
         RADEON_ENC_CS(radv_enc_h264_pic_type(slot_info_0[0]->pStdReferenceInfo->primary_pic_type));
         RADEON_ENC_CS(slot_info_0[0]->pStdReferenceInfo->flags.used_for_long_term_reference);
         RADEON_ENC_CS(RENCODE_H264_PICTURE_STRUCTURE_FRAME);
         RADEON_ENC_CS(slot_info_0[0]->pStdReferenceInfo->PicOrderCnt);
      } else {
         RADEON_ENC_CS(0); // l0 ref pic0 pic_type
         RADEON_ENC_CS(0); // l0 ref pic0 is long term
         RADEON_ENC_CS(0); // l0 ref pic0 picture structure
         RADEON_ENC_CS(0); // l0 ref pic0 pic order cnt
      }
      RADEON_ENC_CS(slot_idx_0[1]); // l0 ref pic1 index
      if (slot_info_0[1]) {
         RADEON_ENC_CS(radv_enc_h264_pic_type(slot_info_0[1]->pStdReferenceInfo->primary_pic_type));
         RADEON_ENC_CS(slot_info_0[1]->pStdReferenceInfo->flags.used_for_long_term_reference);
         RADEON_ENC_CS(RENCODE_H264_PICTURE_STRUCTURE_FRAME);
         RADEON_ENC_CS(slot_info_0[1]->pStdReferenceInfo->PicOrderCnt);
      } else {
         RADEON_ENC_CS(0); // l0 ref pic1 pic_type
         RADEON_ENC_CS(0); // l0 ref pic1 is long term
         RADEON_ENC_CS(0); // l0 ref pic1 picture structure
         RADEON_ENC_CS(0); // l0 ref pic1 pic order cnt
      }
      RADEON_ENC_CS(slot_idx_1); // l1 ref pic0 index
      if (slot_info_1) {
         RADEON_ENC_CS(radv_enc_h264_pic_type(slot_info_1->pStdReferenceInfo->primary_pic_type));
         RADEON_ENC_CS(slot_info_1->pStdReferenceInfo->flags.used_for_long_term_reference);
         RADEON_ENC_CS(RENCODE_H264_PICTURE_STRUCTURE_FRAME);
         RADEON_ENC_CS(slot_info_1->pStdReferenceInfo->PicOrderCnt);
      } else {
         RADEON_ENC_CS(0); // l1 ref pic0 pic_type
         RADEON_ENC_CS(0); // l1 ref pic0 is long term
         RADEON_ENC_CS(0); // l1 ref pic0 picture structure
         RADEON_ENC_CS(0); // l1 ref pic0 pic order cnt
      }
      RADEON_ENC_CS(h264_pic->flags.is_reference); // is reference
   } else {
      // V5
      RADEON_ENC_CS(RENCODE_H264_PICTURE_STRUCTURE_FRAME);
      RADEON_ENC_CS(h264_pic->PicOrderCnt);
      RADEON_ENC_CS(h264_pic->flags.is_reference);
      RADEON_ENC_CS(h264_pic->flags.long_term_reference_flag);
      RADEON_ENC_CS(RENCODE_H264_INTERLACING_MODE_PROGRESSIVE);
      RADEON_ENC_CS(slot_idx_0[0]); // ref_list0[0]
      RADEON_ENC_CS(slot_idx_0[1]); // ref_list0[1]
      for (int i = 2; i < RENCODE_H264_MAX_REFERENCE_LIST_SIZE; i++)
         RADEON_ENC_CS(0);                             // ref_list0[i]
      RADEON_ENC_CS(num_active_l0_refs);               // num_active_references_l0
      RADEON_ENC_CS(slot_idx_1);                       // ref_list1[0]
      for (int i = 1; i < RENCODE_H264_MAX_REFERENCE_LIST_SIZE; i++)
         RADEON_ENC_CS(0);
      RADEON_ENC_CS(slot_idx_1 != 0xffffffff ? 1 : 0); // num_active_references_l1
      RADEON_ENC_CS(0);                                // lsm_reference_pictures[0].list
      RADEON_ENC_CS(0);                                // lsm_reference_pictures[0].list_index
      RADEON_ENC_CS(num_active_l0_refs > 1 ? 0 : 1);   // lsm_reference_pictures[1].list
      RADEON_ENC_CS(num_active_l0_refs > 1 ? 1 : 0);   // lsm_reference_pictures[1].list_index
   }
   RADEON_ENC_END();
}

static void
radv_enc_params_hevc(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_5)
      return;

   const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);

   assert(h265_picture_info);

   const StdVideoEncodeH265PictureInfo *h265_pic = h265_picture_info->pStdPictureInfo;
   unsigned slot_idx_0 = 0xffffffff;

   switch (h265_pic->pic_type) {
   case STD_VIDEO_H265_PICTURE_TYPE_P:
      slot_idx_0 = h265_pic->pRefLists->RefPicList0[0];
      break;
   default:
      break;
   }

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.enc_params_hevc);
   RADEON_ENC_CS(slot_idx_0);
   for (int i = 1; i < RENCODE_HEVC_MAX_REFERENCE_LIST_SIZE; i++)
      RADEON_ENC_CS(0);
   RADEON_ENC_CS(slot_idx_0 != 0xffffffff ? 1 : 0); // num_active_references_l0
   RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static void
radv_enc_op_init(struct radv_cmd_buffer *cmd_buffer)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_INITIALIZE);
   RADEON_ENC_END();
}

static void
radv_enc_op_enc(struct radv_cmd_buffer *cmd_buffer)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_ENCODE);
   RADEON_ENC_END();
}

static void
radv_enc_op_init_rc(struct radv_cmd_buffer *cmd_buffer)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_INIT_RC);
   RADEON_ENC_END();
}

static void
radv_enc_op_init_rc_vbv(struct radv_cmd_buffer *cmd_buffer)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_INIT_RC_VBV_BUFFER_LEVEL);
   RADEON_ENC_END();
}

static void
radv_enc_op_preset(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_video_session *vid = cmd_buffer->video.vid;
   uint32_t preset_mode;

   if (vid->enc_preset_mode == RENCODE_PRESET_MODE_QUALITY)
      preset_mode = RENCODE_IB_OP_SET_QUALITY_ENCODING_MODE;
   else if (vid->enc_preset_mode == RENCODE_PRESET_MODE_BALANCE)
      preset_mode = RENCODE_IB_OP_SET_BALANCE_ENCODING_MODE;
   else
      preset_mode = RENCODE_IB_OP_SET_SPEED_ENCODING_MODE;

   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      const struct VkVideoEncodeH265PictureInfoKHR *h265_picture_info =
         vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H265_PICTURE_INFO_KHR);
      const StdVideoEncodeH265PictureInfo *pic = h265_picture_info->pStdPictureInfo;
      const StdVideoH265SequenceParameterSet *sps =
         vk_video_find_h265_enc_std_sps(cmd_buffer->video.params, pic->pps_seq_parameter_set_id);
      if (sps->flags.sample_adaptive_offset_enabled_flag && vid->enc_preset_mode == RENCODE_PRESET_MODE_SPEED)
         preset_mode = RENCODE_IB_OP_SET_BALANCE_ENCODING_MODE;
      break;
   }
   default:
      break;
   }

   RADEON_ENC_BEGIN(preset_mode);
   RADEON_ENC_END();
}

static uint32_t
radv_get_encode_color_volume(VkVideoEncodeRgbModelConversionFlagBitsVALVE model)
{
   switch (model) {
   case VK_VIDEO_ENCODE_RGB_MODEL_CONVERSION_YCBCR_709_BIT_VALVE:
      return RENCODE_COLOR_VOLUME_G22_BT709;
   case VK_VIDEO_ENCODE_RGB_MODEL_CONVERSION_YCBCR_2020_BIT_VALVE:
      return RENCODE_COLOR_VOLUME_G2084_BT2020;
   default:
      UNREACHABLE("Unsupported rgb model conversion.");
   }
}

static uint32_t
radv_get_encode_color_range(VkVideoEncodeRgbRangeCompressionFlagBitsVALVE range)
{
   switch (range) {
   case VK_VIDEO_ENCODE_RGB_RANGE_COMPRESSION_FULL_RANGE_BIT_VALVE:
      return RENCODE_COLOR_RANGE_FULL;
   case VK_VIDEO_ENCODE_RGB_RANGE_COMPRESSION_NARROW_RANGE_BIT_VALVE:
      return RENCODE_COLOR_RANGE_STUDIO;
   default:
      UNREACHABLE("Unsupported rgb range compression.");
   }
}

static uint32_t
radv_get_encode_chroma_location(VkVideoEncodeRgbChromaOffsetFlagBitsVALVE location)
{
   switch (location) {
   case VK_VIDEO_ENCODE_RGB_CHROMA_OFFSET_COSITED_EVEN_BIT_VALVE:
      return RENCODE_CHROMA_LOCATION_CO_SITE;
   case VK_VIDEO_ENCODE_RGB_CHROMA_OFFSET_MIDPOINT_BIT_VALVE:
      return RENCODE_CHROMA_LOCATION_INTERSTITIAL;
   default:
      UNREACHABLE("Unsupported chroma offset.");
   }
}

static void
radv_enc_input_format(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   uint32_t color_space;
   uint32_t color_bit_depth;
   uint32_t color_packing_format;
   uint32_t chroma_subsampling;

   switch (vid->vk.picture_format) {
   case VK_FORMAT_B8G8R8A8_UNORM:
      color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A8R8G8B8;
      chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case VK_FORMAT_R8G8B8A8_UNORM:
      color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A8B8G8R8;
      chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A2B10G10R10;
      chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
      color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A2R10G10B10;
      chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      color_packing_format = RENCODE_COLOR_PACKING_FORMAT_NV12;
      chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_2_0;
      color_space = RENCODE_COLOR_SPACE_YUV;
      break;
   case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
      color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      color_packing_format = RENCODE_COLOR_PACKING_FORMAT_P010;
      chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_2_0;
      color_space = RENCODE_COLOR_SPACE_YUV;
      break;
   default:
      assert(0);
      return;
   }

   // Color volume should match between input and output
   uint32_t color_volume = RENCODE_COLOR_VOLUME_G22_BT709;
   if (vid->vk.perform_rgb_conversion)
      color_volume = radv_get_encode_color_volume(vid->vk.rgb_conv.rgb_model);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.input_format);
   RADEON_ENC_CS(color_volume);               // input color volume
   RADEON_ENC_CS(color_space);                // input color space
   RADEON_ENC_CS(0);                          // input color range (ignored)
   RADEON_ENC_CS(chroma_subsampling);         // input chroma subsampling
   RADEON_ENC_CS(0);                          // input chroma location (ignored for RGB)
   RADEON_ENC_CS(color_bit_depth);            // input color bit depth
   RADEON_ENC_CS(color_packing_format);       // input color packing format
   RADEON_ENC_END();
}

static void
radv_enc_output_format(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   uint32_t color_bit_depth;

   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      if (vid->vk.h265.profile_idc == STD_VIDEO_H265_PROFILE_IDC_MAIN_10)
         color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      else
         color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      if (cmd_buffer->video.params->av1_enc.seq_hdr.color_config.BitDepth == 10)
         color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      else
         color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      break;
   default:
      assert(0);
      return;
   }

   uint32_t color_volume = 0;
   uint32_t color_range = RENCODE_COLOR_RANGE_STUDIO;
   uint32_t chroma_location = 0;
   if (vid->vk.perform_rgb_conversion) {
      color_volume = radv_get_encode_color_volume(vid->vk.rgb_conv.rgb_model);
      color_range = radv_get_encode_color_range(vid->vk.rgb_conv.rgb_range);
      chroma_location = radv_get_encode_chroma_location(vid->vk.rgb_conv.y_chroma_offset);
   }

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.output_format);
   RADEON_ENC_CS(color_volume); // output color volume
   RADEON_ENC_CS(color_range);  // output color range
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5)
      RADEON_ENC_CS(0);            // output chroma subsampling
   RADEON_ENC_CS(chroma_location); // output chroma location
   RADEON_ENC_CS(color_bit_depth); // output color bit depth
   RADEON_ENC_END();
}

static void
radv_enc_headers_h264(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   const struct radv_physical_device *pdev = radv_device_physical(radv_cmd_buffer_device(cmd_buffer));
   const struct VkVideoEncodeH264PictureInfoKHR *h264_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_H264_PICTURE_INFO_KHR);
   const StdVideoEncodeH264PictureInfo *pic = h264_picture_info->pStdPictureInfo;

   unsigned num_ref_l0 = 0;
   switch (pic->primary_pic_type) {
   case STD_VIDEO_H264_PICTURE_TYPE_P:
      num_ref_l0 =
         MIN2(pic->pRefLists->num_ref_idx_l0_active_minus1, pdev->info.vcn_ip_version >= VCN_3_0_0 ? 1 : 0) + 1;
      break;
   case STD_VIDEO_H264_PICTURE_TYPE_B:
      /* maxBPictureL0ReferenceCount = 1 when supported */
      num_ref_l0 = 1;
      break;
   case STD_VIDEO_H264_PICTURE_TYPE_IDR:
   case STD_VIDEO_H264_PICTURE_TYPE_I:
      num_ref_l0 = 0;
      break;
   default:
      UNREACHABLE("invalid primary_pic_type");
   }

   radv_enc_slice_header(cmd_buffer, enc_info, num_ref_l0 > 1 ? num_ref_l0 - 1 : 0);
   radv_enc_params(cmd_buffer, enc_info);
   radv_enc_params_h264(cmd_buffer, enc_info, num_ref_l0);
}

static void
radv_enc_headers_hevc(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   radv_enc_slice_header_hevc(cmd_buffer, enc_info);
   radv_enc_params(cmd_buffer, enc_info);
   radv_enc_params_hevc(cmd_buffer, enc_info);
}

static void
radv_enc_cdf_default_table(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct VkVideoEncodeAV1PictureInfoKHR *av1_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_AV1_PICTURE_INFO_KHR);
   const StdVideoEncodeAV1PictureInfo *av1_pic = av1_picture_info->pStdPictureInfo;

   radv_cs_add_buffer(device->ws, cs->b, cmd_buffer->video.vid->ctx.mem->bo);
   uint64_t va = radv_buffer_get_va(cmd_buffer->video.vid->ctx.mem->bo);
   va += cmd_buffer->video.vid->ctx.offset;
   uint32_t use_cdf_default = (av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY ||
                               av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY ||
                               av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_SWITCH ||
                               av1_pic->primary_ref_frame == STD_VIDEO_AV1_PRIMARY_REF_NONE);
   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.cdf_default_table_av1);
   RADEON_ENC_CS(use_cdf_default);
   if (pdev->enc_hw_ver == RADV_VIDEO_ENC_HW_4) {
      RADEON_ENC_CS(va & 0xffffffff);
      RADEON_ENC_CS(va >> 32);
   } else {
      RADEON_ENC_CS(va >> 32);
      RADEON_ENC_CS(va & 0xffffffff);
   }
   RADEON_ENC_END();
}

static void
radv_enc_params_av1(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_5)
      return;

   const struct VkVideoEncodeAV1PictureInfoKHR *av1_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_AV1_PICTURE_INFO_KHR);

   unsigned slot_idx_0 = 0xffffffff;
   unsigned slot_idx_1 = 0xffffffff;

   switch (av1_picture_info->predictionMode) {
   case VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR:
      slot_idx_0 = 0; /* LAST_FRAME */
      break;
   case VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR:
      slot_idx_0 = 0; /* LAST_FRAME */
      slot_idx_1 = 3; /* GOLDEN_FRAME */
      break;
   case VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_BIDIRECTIONAL_COMPOUND_KHR:
      slot_idx_0 = 0; /* LAST_FRAME */
      slot_idx_1 = 6; /* ALTREF_FRAME */
      break;
   default:
      break;
   }

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.enc_params_av1);
   for (int i = 0; i < RENCODE_AV1_REFS_PER_FRAME; i++)
      RADEON_ENC_CS(av1_picture_info->referenceNameSlotIndices[i]);
   RADEON_ENC_CS(slot_idx_0);
   RADEON_ENC_CS(slot_idx_1);
   RADEON_ENC_CS(av1_picture_info->pStdPictureInfo->order_hint);
   RADEON_ENC_END();
}

static void
radv_enc_av1_obu_header(struct radv_cmd_buffer *cmd_buffer, uint32_t obu_type,
                        const StdVideoEncodeAV1ExtensionHeader *ext_header)
{
   /* obu header () */
   /* obu_forbidden_bit */
   radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
   /* obu_type */
   radv_enc_code_fixed_bits(cmd_buffer, obu_type, 4);
   /* obu_extension_flag */
   radv_enc_code_fixed_bits(cmd_buffer, ext_header ? 1 : 0, 1);
   /* obu_has_size_field */
   radv_enc_code_fixed_bits(cmd_buffer, 1, 1);
   /* obu_reserved_1bit */
   radv_enc_code_fixed_bits(cmd_buffer, 0, 1);

   if (ext_header) {
      radv_enc_code_fixed_bits(cmd_buffer, ext_header->temporal_id, 3);
      radv_enc_code_fixed_bits(cmd_buffer, ext_header->spatial_id, 2);
      radv_enc_code_fixed_bits(cmd_buffer, 0, 3); /* reserved 3 bits */
   }
}

static void
radv_enc_av1_write_delta_q(struct radv_cmd_buffer *cmd_buffer, int32_t q)
{
   radv_enc_code_fixed_bits(cmd_buffer, !!q, 1);
   if (q)
      radv_enc_code_fixed_bits(cmd_buffer, q, 7);
}

static unsigned
radv_enc_av1_tile_log2(unsigned blk_size, unsigned target)
{
   unsigned k;
   for (k = 0; (blk_size << k) < target; k++)
      ;
   return k;
}

static void
radv_enc_av1_obu_instruction(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info,
                             struct radv_enc_av1_state *av1_state)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct vk_video_session_parameters *params = cmd_buffer->video.params;
   const struct VkVideoEncodeAV1PictureInfoKHR *av1_picture_info =
      vk_find_struct_const(enc_info->pNext, VIDEO_ENCODE_AV1_PICTURE_INFO_KHR);
   const StdVideoEncodeAV1PictureInfo *av1_pic = av1_picture_info->pStdPictureInfo;
   const StdVideoAV1SequenceHeader *seq = &params->av1_enc.seq_hdr.base;
   const StdVideoEncodeAV1ExtensionHeader *ext_header =
      av1_picture_info->generateObuExtensionHeader ? av1_pic->pExtensionHeader : NULL;
   bool frame_is_intra =
      av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY || av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_INTRA_ONLY;
   bool error_resilient_mode = false;
   VkExtent2D aligned_extent =
      radv_enc_aligned_coded_extent(pdev, vid->vk.op, enc_info->srcPictureResource.codedExtent);

   radv_enc_reset(cmd_buffer);

   RADEON_ENC_BEGIN(pdev->vcn_enc_cmds.bitstream_instruction_av1);

   /* OBU_FRAME */
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START,
                                    RENCODE_OBU_START_TYPE_FRAME);
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);
   radv_enc_av1_obu_header(cmd_buffer, RENCODE_OBU_TYPE_FRAME, ext_header);
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_SIZE, 0);
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   /*  uncompressed_header() */
   if (!seq->flags.reduced_still_picture_header) {
      /*  show_existing_frame  */
      radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
      /*  frame_type  */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->frame_type, 2);
      /*  show_frame  */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.show_frame, 1);
      if (!av1_pic->flags.show_frame)
         /*  showable_frame  */
         radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.showable_frame, 1);

      if ((av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_SWITCH) ||
          (av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY && av1_pic->flags.show_frame))
         error_resilient_mode = true;
      else {
         /*  error_resilient_mode  */
         radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.error_resilient_mode, 1);
         error_resilient_mode = av1_pic->flags.error_resilient_mode;
      }
   }

   /*  disable_cdf_update  */
   radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.disable_cdf_update, 1);

   if (seq->seq_force_screen_content_tools == STD_VIDEO_AV1_SELECT_SCREEN_CONTENT_TOOLS)
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.allow_screen_content_tools, 1);

   if (av1_pic->flags.allow_screen_content_tools && seq->seq_force_integer_mv == STD_VIDEO_AV1_SELECT_INTEGER_MV)
      /*  force_integer_mv  */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.force_integer_mv, 1);

   if (seq->flags.frame_id_numbers_present_flag)
      /*  current_frame_id  */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->current_frame_id,
                               seq->delta_frame_id_length_minus_2 + 2 + seq->additional_frame_id_length_minus_1 + 1);

   bool frame_size_override = false;
   if (av1_pic->frame_type == STD_VIDEO_AV1_FRAME_TYPE_SWITCH)
      frame_size_override = true;
   else if (!seq->flags.reduced_still_picture_header) {
      /*  frame_size_override_flag  */
      frame_size_override = false;
      radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
   }

   if (seq->flags.enable_order_hint)
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->order_hint, seq->order_hint_bits_minus_1 + 1);

   if (!frame_is_intra && !error_resilient_mode) {
      /*  primary_ref_frame - VCN4 can either use NONE (7) or LAST (0) */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->primary_ref_frame != 7 ? 0 : 7, 3);
   }

   if ((av1_pic->frame_type != STD_VIDEO_AV1_FRAME_TYPE_SWITCH) &&
       (av1_pic->frame_type != STD_VIDEO_AV1_FRAME_TYPE_KEY || !av1_pic->flags.show_frame))
      /*  refresh_frame_flags  */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->refresh_frame_flags, 8);

   if ((!frame_is_intra || av1_pic->refresh_frame_flags != 0xff) && error_resilient_mode &&
       seq->flags.enable_order_hint) {
      for (unsigned i = 0; i < STD_VIDEO_AV1_NUM_REF_FRAMES; i++)
         /*  ref_order_hint  */
         radv_enc_code_fixed_bits(cmd_buffer, av1_pic->ref_order_hint[i], seq->order_hint_bits_minus_1 + 1);
   }

   if (frame_is_intra) {
      /*  render_and_frame_size_different  */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.render_and_frame_size_different, 1);
      if (av1_pic->flags.render_and_frame_size_different) {
         /*  render_width_minus_1  */
         radv_enc_code_fixed_bits(cmd_buffer, av1_pic->render_width_minus_1, 16);
         /*  render_height_minus_1  */
         radv_enc_code_fixed_bits(cmd_buffer, av1_pic->render_height_minus_1, 16);
      }
      if (av1_pic->flags.allow_screen_content_tools)
         /*  allow_intrabc  */
         radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
   } else {
      if (seq->flags.enable_order_hint)
         /*  frame_refs_short_signaling  */
         radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
      for (unsigned i = 0; i < STD_VIDEO_AV1_REFS_PER_FRAME; i++) {
         /*  ref_frame_idx  */
         radv_enc_code_fixed_bits(cmd_buffer, av1_pic->ref_frame_idx[i], 3);
         if (seq->flags.frame_id_numbers_present_flag)
            radv_enc_code_fixed_bits(cmd_buffer, av1_pic->delta_frame_id_minus_1[i],
                                     seq->delta_frame_id_length_minus_2 + 2);
      }

      if (frame_size_override && !error_resilient_mode)
         /*  found_ref  */
         radv_enc_code_fixed_bits(cmd_buffer, 1, 1);
      else {
         if (frame_size_override) {
            /*  frame_width_minus_1  */
            uint32_t val = aligned_extent.width - 1;
            uint32_t used_bits = radv_enc_value_bits(val);
            radv_enc_code_fixed_bits(cmd_buffer, val, used_bits);
            /*  frame_height_minus_1  */
            val = aligned_extent.height - 1;
            used_bits = radv_enc_value_bits(val);
            radv_enc_code_fixed_bits(cmd_buffer, val, used_bits);
         }
         /*  render_and_frame_size_different  */
         radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.render_and_frame_size_different, 1);
         if (av1_pic->flags.render_and_frame_size_different) {
            /*  render_width_minus_1  */
            radv_enc_code_fixed_bits(cmd_buffer, av1_pic->render_width_minus_1, 16);
            /*  render_height_minus_1  */
            radv_enc_code_fixed_bits(cmd_buffer, av1_pic->render_height_minus_1, 16);
         }
      }

      if (!av1_pic->flags.allow_screen_content_tools || !av1_pic->flags.force_integer_mv)
         /*  allow_high_precision_mv  */
         radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_ALLOW_HIGH_PRECISION_MV, 0);

      /*  read_interpolation_filter  */
      radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_READ_INTERPOLATION_FILTER, 0);

      radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);
      /*  is_motion_mode_switchable  */
      radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
   }

   if (!seq->flags.reduced_still_picture_header && !av1_pic->flags.disable_cdf_update)
      /*  disable_frame_end_update_cdf  */
      radv_enc_code_fixed_bits(cmd_buffer, av1_pic->flags.disable_frame_end_update_cdf, 1);

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
      /*  tile_info  */
      uint32_t sb_cols = DIV_ROUND_UP(aligned_extent.width, 64);
      uint32_t sb_rows = DIV_ROUND_UP(aligned_extent.height, 64);
      uint32_t min_log2_tile_cols = radv_enc_av1_tile_log2(64, sb_cols);
      uint32_t min_log2_tiles = MAX2(min_log2_tile_cols, radv_enc_av1_tile_log2(64 * 36, sb_rows * sb_cols));
      uint32_t tile_cols_log2 = radv_enc_av1_tile_log2(1, av1_state->tile_config.num_tile_cols);
      uint32_t tile_rows_log2 = radv_enc_av1_tile_log2(1, av1_state->tile_config.num_tile_rows);

      radv_enc_code_fixed_bits(cmd_buffer, av1_state->tile_config.uniform_tile_spacing, 1);
      if (av1_state->tile_config.uniform_tile_spacing) {
         for (unsigned i = min_log2_tile_cols; i < tile_cols_log2; i++)
            radv_enc_code_fixed_bits(cmd_buffer, 1, 1);
         radv_enc_code_fixed_bits(cmd_buffer, 0, 1);

         for (unsigned i = min_log2_tiles - tile_cols_log2; i < tile_rows_log2; i++)
            radv_enc_code_fixed_bits(cmd_buffer, 1, 1);
         radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
      } else {
         uint32_t widest_tile_sb = 0;
         uint32_t start_sb = 0;
         for (unsigned i = 0; i < av1_state->tile_config.num_tile_cols; i++) {
            uint32_t max_width = MIN2(sb_cols - start_sb, 64);
            radv_enc_code_ns(cmd_buffer, av1_state->tile_config.tile_widths[i] - 1, max_width);
            widest_tile_sb = MAX2(av1_state->tile_config.tile_widths[i], widest_tile_sb);
            start_sb += av1_state->tile_config.tile_widths[i];
         }

         uint32_t max_tile_area_sb;
         if (min_log2_tiles > 0)
            max_tile_area_sb = (sb_rows * sb_cols) >> (min_log2_tiles + 1);
         else
            max_tile_area_sb = sb_rows * sb_cols;

         uint32_t max_tile_height_sb = MAX2(max_tile_area_sb / widest_tile_sb, 1);

         start_sb = 0;
         for (unsigned i = 0; i < av1_state->tile_config.num_tile_rows; i++) {
            uint32_t max_height = MIN2(sb_rows - start_sb, max_tile_height_sb);
            radv_enc_code_ns(cmd_buffer, av1_state->tile_config.tile_height[i] - 1, max_height);
            start_sb += av1_state->tile_config.tile_height[i];
         }
      }

      if (tile_cols_log2 || tile_rows_log2) {
         radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_V5_AV1_BITSTREAM_INSTRUCTION_CONTEXT_UPDATE_TILE_ID, 0);
         radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);
         radv_enc_code_fixed_bits(cmd_buffer, av1_state->tile_config.tile_size_bytes_minus_1, 2);
      }

      /*  quantization_params  */
      radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_V5_AV1_BITSTREAM_INSTRUCTION_BASE_Q_IDX, 0);

      radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

      radv_enc_av1_write_delta_q(cmd_buffer, av1_pic->pQuantization ? av1_pic->pQuantization->DeltaQYDc : 0);

      if (seq->pColorConfig && seq->pColorConfig->flags.separate_uv_delta_q)
         radv_enc_code_fixed_bits(cmd_buffer, 1, 1);

      radv_enc_av1_write_delta_q(cmd_buffer, av1_pic->pQuantization ? av1_pic->pQuantization->DeltaQUDc : 0);
      radv_enc_av1_write_delta_q(cmd_buffer, av1_pic->pQuantization ? av1_pic->pQuantization->DeltaQUAc : 0);

      if (seq->pColorConfig && seq->pColorConfig->flags.separate_uv_delta_q) {
         radv_enc_av1_write_delta_q(cmd_buffer, av1_pic->pQuantization ? av1_pic->pQuantization->DeltaQVDc : 0);
         radv_enc_av1_write_delta_q(cmd_buffer, av1_pic->pQuantization ? av1_pic->pQuantization->DeltaQVAc : 0);
      }

      /* using qmatrix */
      radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
   } else {
      /*  tile_info  */
      radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_V4_AV1_BITSTREAM_INSTRUCTION_TILE_INFO, 0);
      /*  quantization_params  */
      radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_V4_AV1_BITSTREAM_INSTRUCTION_QUANTIZATION_PARAMS, 0);
      radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);
   }
   /*  segmentation_enable  */
   radv_enc_code_fixed_bits(cmd_buffer, 0, 1);
   /*  delta_q_params  */
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_Q_PARAMS, 0);
   /*  delta_lf_params  */
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_LF_PARAMS, 0);
   /*  loop_filter_params  */
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_LOOP_FILTER_PARAMS, 0);
   /*  cdef_params  */
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_CDEF_PARAMS, 0);
   /*  lr_params  */
   /*  read_tx_mode  */
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_READ_TX_MODE, 0);

   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   if (!frame_is_intra) {
      /*  reference_select  */
      bool compound =
         av1_picture_info->predictionMode >= VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR;
      radv_enc_code_fixed_bits(cmd_buffer, compound, 1);
   }

   if (av1_state->skip_mode_allowed)
      /*  skip_mode_present  */
      radv_enc_code_fixed_bits(cmd_buffer, !av1_state->disallow_skip_mode, 1);

   /*  reduced_tx_set  */
   radv_enc_code_fixed_bits(cmd_buffer, 0, 1);

   if (!frame_is_intra)
      for (uint32_t ref = STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME; ref <= STD_VIDEO_AV1_REFERENCE_NAME_ALTREF_FRAME;
           ref++)
         /*  is_global  */
         radv_enc_code_fixed_bits(cmd_buffer, 0, 1);

   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_TILE_GROUP_OBU, 0);
   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_END, 0);

   radv_enc_av1_bs_instruction_type(cmd_buffer, RENCODE_AV1_BITSTREAM_INSTRUCTION_END, 0);

   RADEON_ENC_END();
}

static void
radv_enc_headers_av1(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info,
                     struct radv_enc_av1_state *state)
{
   radv_enc_av1_obu_instruction(cmd_buffer, enc_info, state);
   radv_enc_params(cmd_buffer, enc_info);
   radv_enc_params_av1(cmd_buffer, enc_info);
   radv_enc_cdf_default_table(cmd_buffer, enc_info);
}

static void
radv_vcn_encode_video(struct radv_cmd_buffer *cmd_buffer, const VkVideoEncodeInfoKHR *enc_info)
{
   VK_FROM_HANDLE(radv_buffer, dst_buffer, enc_info->dstBuffer);
   struct radv_video_session *vid = cmd_buffer->video.vid;
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_enc_state *enc = &cmd_buffer->video.enc;
   struct radv_cmd_stream *cs = cmd_buffer->cs;
   uint64_t feedback_query_va;
   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      break;
   default:
      assert(0);
      return;
   }

   radeon_check_space(device->ws, cs->b, 1600);

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5)
      radv_enc_qp_map_input(cmd_buffer, enc_info);

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
      radv_vcn_sq_header(cs, &cmd_buffer->video.sq, RADEON_VCN_ENGINE_TYPE_ENCODE);

   const struct VkVideoInlineQueryInfoKHR *inline_queries = NULL;
   if (vid->vk.flags & VK_VIDEO_SESSION_CREATE_INLINE_QUERIES_BIT_KHR) {
      inline_queries = vk_find_struct_const(enc_info->pNext, VIDEO_INLINE_QUERY_INFO_KHR);

      if (inline_queries) {
         VK_FROM_HANDLE(radv_query_pool, pool, inline_queries->queryPool);

         radv_cs_add_buffer(device->ws, cs->b, pool->bo);

         feedback_query_va = radv_buffer_get_va(pool->bo);
         feedback_query_va += pool->stride * inline_queries->firstQuery;
      }
   }

   if (!inline_queries)
      feedback_query_va = cmd_buffer->video.feedback_query_va;

   // before encode
   // session info
   radv_enc_session_info(cmd_buffer);

   cmd_buffer->video.enc.total_task_size = 0;

   // task info
   radv_enc_task_info(cmd_buffer, feedback_query_va);

   radv_enc_session_init(cmd_buffer, enc_info);

   /* Only update current frame QP with RC disabled. */
   if (cmd_buffer->video.enc.rate_control_mode == VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR) {
      rvcn_enc_rate_ctl_per_picture_t per_pic;
      radv_enc_rc_per_pic_default(vid, &per_pic);
      for (unsigned i = 0; i < cmd_buffer->video.enc.rate_control_num_layers; i++) {
         radv_enc_layer_select(cmd_buffer, i);
         radv_enc_rc_per_pic(cmd_buffer, enc_info, &per_pic);
      }
   }

   if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) {
      radv_enc_slice_control(cmd_buffer, enc_info);
      radv_enc_spec_misc_h264(cmd_buffer, enc_info);
      radv_enc_deblocking_filter_h264(cmd_buffer, enc_info);
      radv_enc_headers_h264(cmd_buffer, enc_info);
   } else if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR) {
      radv_enc_slice_control_hevc(cmd_buffer, enc_info);
      radv_enc_spec_misc_hevc(cmd_buffer, enc_info);
      radv_enc_deblocking_filter_hevc(cmd_buffer, enc_info);
      radv_enc_headers_hevc(cmd_buffer, enc_info);
   } else if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR) {
      struct radv_enc_av1_state state = {0};
      radv_enc_spec_misc_av1(cmd_buffer, enc_info, &state);
      radv_enc_headers_av1(cmd_buffer, enc_info, &state);
   }
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5)
      radv_enc_ctx2(cmd_buffer, enc_info);
   else
      radv_enc_ctx(cmd_buffer, enc_info);
   // bitstream
   radv_enc_bitstream(cmd_buffer, dst_buffer, enc_info->dstBufferOffset);

   // feedback
   radv_enc_feedback(cmd_buffer, feedback_query_va);

   // v2 encode statistics
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2) {
   }
   // intra_refresh
   radv_enc_intra_refresh(cmd_buffer, enc_info);
   // qp map
   radv_enc_qp_map(cmd_buffer, enc_info);
   // v2 input format
   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2) {
      radv_enc_input_format(cmd_buffer);
      radv_enc_output_format(cmd_buffer);
   }
   // v2 output format

   // op_preset
   radv_enc_op_preset(cmd_buffer, enc_info);
   // op_enc
   radv_enc_op_enc(cmd_buffer);

   *enc->p_task_size = enc->total_task_size;

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2) {
      radv_vcn_sq_tail(cs, &cmd_buffer->video.sq);
      if (feedback_query_va && radv_video_write_memory_supported(pdev) == RADV_VIDEO_WRITE_MEMORY_SUPPORT_FULL)
         radv_vcn_write_memory(cmd_buffer, feedback_query_va + RADV_ENC_FEEDBACK_STATUS_IDX * sizeof(uint32_t), 1);
   }
}

void
radv_video_enc_control_video_coding(struct radv_cmd_buffer *cmd_buffer, const VkVideoCodingControlInfoKHR *control_info)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_video_session *vid = cmd_buffer->video.vid;

   switch (vid->vk.op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      if (control_info->flags & VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR) {
         uint8_t *cdfptr = radv_buffer_map(device->ws, vid->ctx.mem->bo);
         cdfptr += vid->ctx.offset;
         memcpy(cdfptr, rvcn_av1_cdf_default_table, VCN_ENC_AV1_DEFAULT_CDF_SIZE);
         device->ws->buffer_unmap(device->ws, vid->ctx.mem->bo, false);
      }
      break;
   default:
      UNREACHABLE("Unsupported\n");
   }

   bool session_init = false;
   bool rate_control_init = false;
   uint32_t vbv_buffer_level = 64;
   rvcn_enc_rate_ctl_layer_init_t rc_layer_init[RADV_ENC_MAX_RATE_LAYER];
   rvcn_enc_rate_ctl_per_picture_t rc_per_pic[RADV_ENC_MAX_RATE_LAYER];

   if (control_info->flags & VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR) {
      uint32_t frame_rate_num = 30;
      uint32_t frame_rate_den = 1;

      cmd_buffer->video.enc.rate_control_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR;
      cmd_buffer->video.enc.rate_control_num_layers = 1;
      radv_enc_rc_per_pic_default(vid, &rc_per_pic[0]);
      rc_layer_init[0].frame_rate_den = frame_rate_den;
      rc_layer_init[0].frame_rate_num = frame_rate_num;
      rc_layer_init[0].vbv_buffer_size = 20000000;
      rc_layer_init[0].target_bit_rate = 16000;
      rc_layer_init[0].peak_bit_rate = 32000;
      rc_layer_init[0].avg_target_bits_per_picture = radv_vcn_per_frame_integer(16000, frame_rate_den, frame_rate_num);
      rc_layer_init[0].peak_bits_per_picture_integer =
         radv_vcn_per_frame_integer(32000, frame_rate_den, frame_rate_num);
      rc_layer_init[0].peak_bits_per_picture_fractional =
         radv_vcn_per_frame_frac(32000, frame_rate_den, frame_rate_num);

      session_init = true;
      rate_control_init = true;
   }

   if (control_info->flags & VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR) {
      const VkVideoEncodeRateControlInfoKHR *rate_control =
         vk_find_struct_const(control_info->pNext, VIDEO_ENCODE_RATE_CONTROL_INFO_KHR);
      assert(rate_control);
      const VkVideoEncodeH264RateControlInfoKHR *h264_rate_control =
         vk_find_struct_const(rate_control->pNext, VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR);
      const VkVideoEncodeH265RateControlInfoKHR *h265_rate_control =
         vk_find_struct_const(rate_control->pNext, VIDEO_ENCODE_H265_RATE_CONTROL_INFO_KHR);
      const VkVideoEncodeAV1RateControlInfoKHR *av1_rate_control =
         vk_find_struct_const(rate_control->pNext, VIDEO_ENCODE_AV1_RATE_CONTROL_INFO_KHR);

      cmd_buffer->video.enc.rate_control_mode = rate_control->rateControlMode;

      if (rate_control->rateControlMode != VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR) {
         if (h264_rate_control)
            cmd_buffer->video.enc.rate_control_num_layers = MAX2(1, h264_rate_control->temporalLayerCount);
         else if (h265_rate_control)
            cmd_buffer->video.enc.rate_control_num_layers = MAX2(1, h265_rate_control->subLayerCount);
         else if (av1_rate_control)
            cmd_buffer->video.enc.rate_control_num_layers = MAX2(1, av1_rate_control->temporalLayerCount);
         else
            cmd_buffer->video.enc.rate_control_num_layers = 1;

         for (unsigned l = 0; l < rate_control->layerCount; l++) {
            const VkVideoEncodeRateControlLayerInfoKHR *layer = &rate_control->pLayers[l];
            const VkVideoEncodeH264RateControlLayerInfoKHR *h264_layer =
               vk_find_struct_const(layer->pNext, VIDEO_ENCODE_H264_RATE_CONTROL_LAYER_INFO_KHR);
            const VkVideoEncodeH265RateControlLayerInfoKHR *h265_layer =
               vk_find_struct_const(layer->pNext, VIDEO_ENCODE_H265_RATE_CONTROL_LAYER_INFO_KHR);
            const VkVideoEncodeAV1RateControlLayerInfoKHR *av1_layer =
               vk_find_struct_const(layer->pNext, VIDEO_ENCODE_AV1_RATE_CONTROL_LAYER_INFO_KHR);
            uint32_t frame_rate_den, frame_rate_num;

            rc_layer_init[l].target_bit_rate = layer->averageBitrate;
            rc_layer_init[l].peak_bit_rate = layer->maxBitrate;
            frame_rate_den = layer->frameRateDenominator;
            frame_rate_num = layer->frameRateNumerator;
            radv_vcn_enc_invalid_frame_rate(&frame_rate_den, &frame_rate_num);
            rc_layer_init[l].frame_rate_den = frame_rate_den;
            rc_layer_init[l].frame_rate_num = frame_rate_num;
            rc_layer_init[l].vbv_buffer_size = (rate_control->virtualBufferSizeInMs / 1000.) * layer->averageBitrate;
            rc_layer_init[l].avg_target_bits_per_picture =
               radv_vcn_per_frame_integer(layer->averageBitrate, frame_rate_den, frame_rate_num);
            rc_layer_init[l].peak_bits_per_picture_integer =
               radv_vcn_per_frame_integer(layer->maxBitrate, frame_rate_den, frame_rate_num);
            rc_layer_init[l].peak_bits_per_picture_fractional =
               radv_vcn_per_frame_frac(layer->maxBitrate, frame_rate_den, frame_rate_num);

            radv_enc_rc_per_pic_default(vid, &rc_per_pic[l]);

            if (h264_layer) {
               if (h264_layer->useMinQp) {
                  rc_per_pic[l].min_qp_i = h264_layer->minQp.qpI;
                  rc_per_pic[l].min_qp_p = h264_layer->minQp.qpP;
                  rc_per_pic[l].min_qp_b = h264_layer->minQp.qpB;
               }
               if (h264_layer->useMaxQp) {
                  rc_per_pic[l].max_qp_i = h264_layer->maxQp.qpI;
                  rc_per_pic[l].max_qp_p = h264_layer->maxQp.qpP;
                  rc_per_pic[l].max_qp_b = h264_layer->maxQp.qpB;
               }
               if (h264_layer->useMaxFrameSize) {
                  rc_per_pic[l].max_au_size_i = h264_layer->maxFrameSize.frameISize;
                  rc_per_pic[l].max_au_size_p = h264_layer->maxFrameSize.framePSize;
                  rc_per_pic[l].max_au_size_b = h264_layer->maxFrameSize.frameBSize;
               }
            } else if (h265_layer) {
               if (h265_layer->useMinQp) {
                  rc_per_pic[l].min_qp_i = h265_layer->minQp.qpI;
                  rc_per_pic[l].min_qp_p = h265_layer->minQp.qpP;
                  rc_per_pic[l].min_qp_b = h265_layer->minQp.qpB;
               }
               if (h265_layer->useMaxQp) {
                  rc_per_pic[l].max_qp_i = h265_layer->maxQp.qpI;
                  rc_per_pic[l].max_qp_p = h265_layer->maxQp.qpP;
                  rc_per_pic[l].max_qp_b = h265_layer->maxQp.qpB;
               }
               if (h265_layer->useMaxFrameSize) {
                  rc_per_pic[l].max_au_size_i = h265_layer->maxFrameSize.frameISize;
                  rc_per_pic[l].max_au_size_p = h265_layer->maxFrameSize.framePSize;
                  rc_per_pic[l].max_au_size_b = h265_layer->maxFrameSize.frameBSize;
               }
            } else if (av1_layer) {
               if (av1_layer->useMinQIndex) {
                  rc_per_pic[l].min_qp_i = av1_layer->minQIndex.intraQIndex;
                  rc_per_pic[l].min_qp_p = av1_layer->minQIndex.predictiveQIndex;
                  rc_per_pic[l].min_qp_b = av1_layer->minQIndex.bipredictiveQIndex;
               }
               if (av1_layer->useMaxQIndex) {
                  rc_per_pic[l].max_qp_i = av1_layer->maxQIndex.intraQIndex;
                  rc_per_pic[l].max_qp_p = av1_layer->maxQIndex.predictiveQIndex;
                  rc_per_pic[l].max_qp_b = av1_layer->maxQIndex.bipredictiveQIndex;
               }
               if (av1_layer->useMaxFrameSize) {
                  rc_per_pic[l].max_au_size_i = av1_layer->maxFrameSize.intraFrameSize;
                  rc_per_pic[l].max_au_size_p = av1_layer->maxFrameSize.predictiveFrameSize;
                  rc_per_pic[l].max_au_size_b = av1_layer->maxFrameSize.bipredictiveFrameSize;
               }
            }
         }

         if (rate_control->virtualBufferSizeInMs > 0) {
            vbv_buffer_level =
               lroundf((float)rate_control->initialVirtualBufferSizeInMs / rate_control->virtualBufferSizeInMs * 64);
         }
      }

      rate_control_init = true;
   }

   if (!session_init && !rate_control_init)
      return;

   radeon_check_space(device->ws, cmd_buffer->cs->b, 256);

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
      radv_vcn_sq_header(cmd_buffer->cs, &cmd_buffer->video.sq, RADEON_VCN_ENGINE_TYPE_ENCODE);

   radv_enc_session_info(cmd_buffer);
   cmd_buffer->video.enc.total_task_size = 0;
   radv_enc_task_info(cmd_buffer, 0);

   if (session_init) {
      radv_enc_op_init(cmd_buffer);
      radv_enc_session_init(cmd_buffer, NULL);
      radv_enc_quality_params(cmd_buffer);
      radv_enc_latency(cmd_buffer, vid->vk.enc_usage.tuning_mode);
   }

   if (rate_control_init) {
      radv_enc_rc_session_init(cmd_buffer, vbv_buffer_level);
      radv_enc_layer_control(cmd_buffer, cmd_buffer->video.enc.rate_control_num_layers);
      for (unsigned i = 0; i < cmd_buffer->video.enc.rate_control_num_layers; i++) {
         radv_enc_layer_select(cmd_buffer, i);
         radv_enc_rc_layer_init(cmd_buffer, &rc_layer_init[i]);
         radv_enc_layer_select(cmd_buffer, i);
         radv_enc_rc_per_pic(cmd_buffer, NULL, &rc_per_pic[i]);
      }
      radv_enc_op_init_rc(cmd_buffer);
      radv_enc_op_init_rc_vbv(cmd_buffer);
   }

   *cmd_buffer->video.enc.p_task_size = cmd_buffer->video.enc.total_task_size;

   if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2)
      radv_vcn_sq_tail(cmd_buffer->cs, &cmd_buffer->video.sq);
}

void
radv_video_enc_begin_video_coding(struct radv_cmd_buffer *cmd_buffer, const VkVideoBeginCodingInfoKHR *pBeginInfo)
{
   const struct VkVideoEncodeRateControlInfoKHR *rc_info =
      vk_find_struct_const(pBeginInfo->pNext, VIDEO_ENCODE_RATE_CONTROL_INFO_KHR);

   if (rc_info) {
      cmd_buffer->video.enc.rate_control_mode = rc_info->rateControlMode;
      cmd_buffer->video.enc.rate_control_num_layers = MAX2(1, rc_info->layerCount);
   } else {
      /* If no VkVideoEncodeRateControlInfoKHR is included, then the presence of an empty
       * VkVideoEncodeRateControlInfoKHR structure is implied which indicates that the current
       * rate control mode is VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR.
       */
      cmd_buffer->video.enc.rate_control_mode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR;
      cmd_buffer->video.enc.rate_control_num_layers = 1;
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdEncodeVideoKHR(VkCommandBuffer commandBuffer, const VkVideoEncodeInfoKHR *pEncodeInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_vcn_encode_video(cmd_buffer, pEncodeInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR(
   VkPhysicalDevice physicalDevice, const VkPhysicalDeviceVideoEncodeQualityLevelInfoKHR *pQualityLevelInfo,
   VkVideoEncodeQualityLevelPropertiesKHR *pQualityLevelProperties)
{
   VK_FROM_HANDLE(radv_physical_device, pdev, physicalDevice);
   pQualityLevelProperties->preferredRateControlMode = VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DEFAULT_KHR;
   pQualityLevelProperties->preferredRateControlLayerCount = 0;

   switch (pQualityLevelInfo->pVideoProfile->videoCodecOperation) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      struct VkVideoEncodeH264QualityLevelPropertiesKHR *ext =
         vk_find_struct(pQualityLevelProperties->pNext, VIDEO_ENCODE_H264_QUALITY_LEVEL_PROPERTIES_KHR);
      if (ext) {
         ext->preferredRateControlFlags = VK_VIDEO_ENCODE_H264_RATE_CONTROL_ATTEMPT_HRD_COMPLIANCE_BIT_KHR;
         ext->preferredGopFrameCount = 60;
         ext->preferredIdrPeriod = 60;
         ext->preferredConsecutiveBFrameCount = 0;
         ext->preferredTemporalLayerCount = 1;
         ext->preferredConstantQp.qpI = 26;
         ext->preferredConstantQp.qpP = 26;
         ext->preferredConstantQp.qpB = 26;
         ext->preferredMaxL0ReferenceCount = 1;
         ext->preferredMaxL1ReferenceCount = 0;
         ext->preferredStdEntropyCodingModeFlag = 1;
      }
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      struct VkVideoEncodeH265QualityLevelPropertiesKHR *ext =
         vk_find_struct(pQualityLevelProperties->pNext, VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_KHR);
      if (ext) {
         ext->preferredRateControlFlags = VK_VIDEO_ENCODE_H265_RATE_CONTROL_ATTEMPT_HRD_COMPLIANCE_BIT_KHR;
         ext->preferredGopFrameCount = 60;
         ext->preferredIdrPeriod = 60;
         ext->preferredConsecutiveBFrameCount = 0;
         ext->preferredSubLayerCount = 1;
         ext->preferredConstantQp.qpI = 26;
         ext->preferredConstantQp.qpP = 26;
         ext->preferredConstantQp.qpB = 26;
         ext->preferredMaxL0ReferenceCount = 1;
         ext->preferredMaxL1ReferenceCount = 0;
      }
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
      struct VkVideoEncodeAV1QualityLevelPropertiesKHR *ext =
         vk_find_struct(pQualityLevelProperties->pNext, VIDEO_ENCODE_AV1_QUALITY_LEVEL_PROPERTIES_KHR);
      if (ext) {
         ext->preferredRateControlFlags =
            0; // https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/35767#note_2979437
         ext->preferredGopFrameCount = 60;
         ext->preferredKeyFramePeriod = 60;
         ext->preferredConsecutiveBipredictiveFrameCount = 0;
         ext->preferredTemporalLayerCount = 1;
         ext->preferredConstantQIndex.intraQIndex = 128;
         ext->preferredConstantQIndex.predictiveQIndex = 128;
         ext->preferredConstantQIndex.bipredictiveQIndex = 128;
         ext->preferredMaxSingleReferenceCount = 1;
         ext->preferredSingleReferenceNameMask =
            (1 << (STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME));
         if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
            ext->preferredMaxUnidirectionalCompoundReferenceCount = 2;
            ext->preferredMaxUnidirectionalCompoundGroup1ReferenceCount = 2;
            ext->preferredUnidirectionalCompoundReferenceNameMask =
               (1 << (STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME)) |
               (1 << (STD_VIDEO_AV1_REFERENCE_NAME_GOLDEN_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME));
            ext->preferredMaxBidirectionalCompoundReferenceCount = 2;
            ext->preferredMaxBidirectionalCompoundGroup1ReferenceCount = 1;
            ext->preferredMaxBidirectionalCompoundGroup2ReferenceCount = 1;
            ext->preferredBidirectionalCompoundReferenceNameMask =
               (1 << (STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME)) |
               (1 << (STD_VIDEO_AV1_REFERENCE_NAME_ALTREF_FRAME - STD_VIDEO_AV1_REFERENCE_NAME_LAST_FRAME));
         } else {
            ext->preferredMaxUnidirectionalCompoundReferenceCount = 0;
            ext->preferredMaxUnidirectionalCompoundGroup1ReferenceCount = 0;
            ext->preferredUnidirectionalCompoundReferenceNameMask = 0;
            ext->preferredMaxBidirectionalCompoundReferenceCount = 0;
            ext->preferredMaxBidirectionalCompoundGroup1ReferenceCount = 0;
            ext->preferredMaxBidirectionalCompoundGroup2ReferenceCount = 0;
            ext->preferredBidirectionalCompoundReferenceNameMask = 0;
         }
      }
      break;
   }
   default:
      break;
   }
   return VK_SUCCESS;
}

void
radv_video_patch_encode_session_parameters(struct radv_device *device, struct vk_video_session_parameters *params)
{
   struct radv_physical_device *pdev = radv_device_physical(device);

   switch (params->op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      for (unsigned i = 0; i < params->h264_enc.h264_pps_count; i++) {
         params->h264_enc.h264_pps[i].base.pic_init_qp_minus26 = 0;
         params->h264_enc.h264_pps[i].base.pic_init_qs_minus26 = 0;
         if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_5 ||
             !params->h264_enc.h264_pps[i].base.flags.entropy_coding_mode_flag)
            params->h264_enc.h264_pps[i].base.flags.transform_8x8_mode_flag = 0;

         params->h264_enc.h264_pps[i].base.num_ref_idx_l0_default_active_minus1 = 0;
         params->h264_enc.h264_pps[i].base.num_ref_idx_l1_default_active_minus1 = 0;
      }
      break;
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      for (unsigned i = 0; i < params->h265_enc.h265_sps_count; i++) {
         VkExtent2D extent = {
            .width = params->h265_enc.h265_sps[i].base.pic_width_in_luma_samples,
            .height = params->h265_enc.h265_sps[i].base.pic_height_in_luma_samples,
         };
         VkExtent2D aligned_extent = radv_enc_aligned_coded_extent(pdev, params->op, extent);

         /* Override the unaligned pic_{width,height} and make up for it with conformance window
          * cropping */
         params->h265_enc.h265_sps[i].base.pic_width_in_luma_samples = aligned_extent.width;
         params->h265_enc.h265_sps[i].base.pic_height_in_luma_samples = aligned_extent.height;

         if (aligned_extent.width != extent.width || aligned_extent.height != extent.height) {
            params->h265_enc.h265_sps[i].base.flags.conformance_window_flag = 1;
            params->h265_enc.h265_sps[i].base.conf_win_right_offset += (aligned_extent.width - extent.width) / 2;
            params->h265_enc.h265_sps[i].base.conf_win_bottom_offset += (aligned_extent.height - extent.height) / 2;
         }

         /* VCN supports only the following block sizes (resulting in 64x64 CTBs with any coding
          * block size) */
         params->h265_enc.h265_sps[i].base.log2_min_luma_coding_block_size_minus3 = 0;
         params->h265_enc.h265_sps[i].base.log2_diff_max_min_luma_coding_block_size = 3;
         params->h265_enc.h265_sps[i].base.log2_min_luma_transform_block_size_minus2 = 0;
         params->h265_enc.h265_sps[i].base.log2_diff_max_min_luma_transform_block_size = 3;
      }

      for (unsigned i = 0; i < params->h265_enc.h265_pps_count; i++) {
         /* cu_qp_delta needs to be enabled if rate control is enabled. VCN2 and newer can also enable
          * it with rate control disabled. Since we don't know what rate control will be used, we
          * need to always force enable it.
          * On VCN1 rate control modes are disabled.
          */
         params->h265_enc.h265_pps[i].base.flags.cu_qp_delta_enabled_flag = !!(pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_2);
         params->h265_enc.h265_pps[i].base.diff_cu_qp_delta_depth = 0;
         params->h265_enc.h265_pps[i].base.init_qp_minus26 = 0;
         params->h265_enc.h265_pps[i].base.flags.dependent_slice_segments_enabled_flag = 1;
         if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_3)
            params->h265_enc.h265_pps[i].base.flags.transform_skip_enabled_flag = 0;

         params->h265_enc.h265_pps[i].base.num_ref_idx_l0_default_active_minus1 = 0;
         params->h265_enc.h265_pps[i].base.num_ref_idx_l1_default_active_minus1 = 0;
      }
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
      /* If the resolution isn't aligned, we need to override it. */
      VkExtent2D extent = {
         .width = params->av1_enc.seq_hdr.base.max_frame_width_minus_1 + 1,
         .height = params->av1_enc.seq_hdr.base.max_frame_height_minus_1 + 1,
      };
      VkExtent2D aligned_extent = radv_enc_aligned_coded_extent(pdev, params->op, extent);
      params->av1_enc.seq_hdr.base.max_frame_width_minus_1 = aligned_extent.width - 1;
      params->av1_enc.seq_hdr.base.max_frame_height_minus_1 = aligned_extent.height - 1;

      /* Also override the bit length if they're too small now */
      if (aligned_extent.width >= (1 << (params->av1_enc.seq_hdr.base.frame_width_bits_minus_1 + 1)))
         params->av1_enc.seq_hdr.base.frame_width_bits_minus_1++;
      if (aligned_extent.height >= (1 << (params->av1_enc.seq_hdr.base.frame_height_bits_minus_1 + 1)))
         params->av1_enc.seq_hdr.base.frame_height_bits_minus_1++;

      /* AMD does not support loop restoration */
      params->av1_enc.seq_hdr.base.flags.enable_restoration = 0;

      /* If pColorConfig is NULL we need to force 10 bit here. */
      params->av1_enc.seq_hdr.color_config.BitDepth =
         params->luma_bit_depth == VK_VIDEO_COMPONENT_BIT_DEPTH_10_BIT_KHR ? 10 : 8;
      break;
   }
   default:
      break;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetEncodedVideoSessionParametersKHR(VkDevice device,
                                         const VkVideoEncodeSessionParametersGetInfoKHR *pVideoSessionParametersInfo,
                                         VkVideoEncodeSessionParametersFeedbackInfoKHR *pFeedbackInfo,
                                         size_t *pDataSize, void *pData)
{
   VK_FROM_HANDLE(vk_video_session_parameters, templ, pVideoSessionParametersInfo->videoSessionParameters);
   size_t total_size = 0;
   size_t size_limit = 0;

   if (pData)
      size_limit = *pDataSize;

   switch (templ->op) {
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR: {
      const struct VkVideoEncodeH264SessionParametersGetInfoKHR *h264_get_info =
         vk_find_struct_const(pVideoSessionParametersInfo->pNext, VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR);
      size_t sps_size = 0, pps_size = 0;
      if (h264_get_info->writeStdSPS) {
         const StdVideoH264SequenceParameterSet *sps = vk_video_find_h264_enc_std_sps(templ, h264_get_info->stdSPSId);
         assert(sps);
         vk_video_encode_h264_sps(sps, size_limit, &sps_size, pData);
      }
      if (h264_get_info->writeStdPPS) {
         const StdVideoH264PictureParameterSet *pps = vk_video_find_h264_enc_std_pps(templ, h264_get_info->stdPPSId);
         assert(pps);
         char *data_ptr = pData ? (char *)pData + sps_size : NULL;
         vk_video_encode_h264_pps(pps, templ->h264_enc.profile_idc == STD_VIDEO_H264_PROFILE_IDC_HIGH, size_limit,
                                  &pps_size, data_ptr);
         if (pFeedbackInfo) {
            struct VkVideoEncodeH264SessionParametersFeedbackInfoKHR *h264_feedback_info =
               vk_find_struct(pFeedbackInfo->pNext, VIDEO_ENCODE_H264_SESSION_PARAMETERS_FEEDBACK_INFO_KHR);
            pFeedbackInfo->hasOverrides = VK_TRUE;
            if (h264_feedback_info)
               h264_feedback_info->hasStdPPSOverrides = VK_TRUE;
         }
      }
      total_size = sps_size + pps_size;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR: {
      const struct VkVideoEncodeH265SessionParametersGetInfoKHR *h265_get_info =
         vk_find_struct_const(pVideoSessionParametersInfo->pNext, VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR);
      size_t sps_size = 0, pps_size = 0, vps_size = 0;
      if (h265_get_info->writeStdVPS) {
         const StdVideoH265VideoParameterSet *vps = vk_video_find_h265_enc_std_vps(templ, h265_get_info->stdVPSId);
         assert(vps);
         vk_video_encode_h265_vps(vps, size_limit, &vps_size, pData);
      }
      if (h265_get_info->writeStdSPS) {
         const StdVideoH265SequenceParameterSet *sps = vk_video_find_h265_enc_std_sps(templ, h265_get_info->stdSPSId);
         assert(sps);
         char *data_ptr = pData ? (char *)pData + vps_size : NULL;
         vk_video_encode_h265_sps(sps, size_limit, &sps_size, data_ptr);

         if (pFeedbackInfo) {
            struct VkVideoEncodeH265SessionParametersFeedbackInfoKHR *h265_feedback_info =
               vk_find_struct(pFeedbackInfo->pNext, VIDEO_ENCODE_H265_SESSION_PARAMETERS_FEEDBACK_INFO_KHR);
            pFeedbackInfo->hasOverrides = VK_TRUE;
            if (h265_feedback_info)
               h265_feedback_info->hasStdSPSOverrides = VK_TRUE;
         }
      }
      if (h265_get_info->writeStdPPS) {
         const StdVideoH265PictureParameterSet *pps = vk_video_find_h265_enc_std_pps(templ, h265_get_info->stdPPSId);
         assert(pps);
         char *data_ptr = pData ? (char *)pData + vps_size + sps_size : NULL;
         vk_video_encode_h265_pps(pps, size_limit, &pps_size, data_ptr);

         if (pFeedbackInfo) {
            struct VkVideoEncodeH265SessionParametersFeedbackInfoKHR *h265_feedback_info =
               vk_find_struct(pFeedbackInfo->pNext, VIDEO_ENCODE_H265_SESSION_PARAMETERS_FEEDBACK_INFO_KHR);
            pFeedbackInfo->hasOverrides = VK_TRUE;
            if (h265_feedback_info)
               h265_feedback_info->hasStdPPSOverrides = VK_TRUE;
         }
      }
      total_size = sps_size + pps_size + vps_size;
      break;
   }
   case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR: {
      struct vk_video_av1_seq_hdr *seq_hdr = &templ->av1_enc.seq_hdr;
      if (!seq_hdr)
         return VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR;
      vk_video_encode_av1_seq_hdr(templ, size_limit, &total_size, pData);
      break;
   }
   default:
      break;
   }

   *pDataSize = total_size;
   return VK_SUCCESS;
}

#define VCN_ENC_SESSION_SIZE 128 * 1024

VkResult
radv_video_get_encode_session_memory_requirements(struct radv_device *device, struct radv_video_session *vid,
                                                  uint32_t *pMemoryRequirementsCount,
                                                  VkVideoSessionMemoryRequirementsKHR *pMemoryRequirements)
{
   struct radv_physical_device *pdev = radv_device_physical(device);
   uint32_t memory_type_bits = (1u << pdev->memory_properties.memoryTypeCount) - 1;

   VK_OUTARRAY_MAKE_TYPED(VkVideoSessionMemoryRequirementsKHR, out, pMemoryRequirements, pMemoryRequirementsCount);

   vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, m)
   {
      m->memoryBindIndex = 0;
      m->memoryRequirements.size = VCN_ENC_SESSION_SIZE;
      m->memoryRequirements.alignment = 0;
      m->memoryRequirements.memoryTypeBits = memory_type_bits;
   }

   if (vid->vk.op == VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR) {
      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, m)
      {
         m->memoryBindIndex = RADV_BIND_ENCODE_AV1_CDF_STORE;
         m->memoryRequirements.size = VCN_ENC_AV1_DEFAULT_CDF_SIZE;
         if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5)
            m->memoryRequirements.size += RENCODE_AV1_SDB_FRAME_CONTEXT_SIZE;
         m->memoryRequirements.alignment = 0;
         m->memoryRequirements.memoryTypeBits = 0;
         for (unsigned i = 0; i < pdev->memory_properties.memoryTypeCount; i++)
            if (pdev->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
               m->memoryRequirements.memoryTypeBits |= (1 << i);
      }
   }

   if (vid->vk.flags & VK_VIDEO_SESSION_CREATE_ALLOW_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR &&
       pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
      const uint32_t texel_size = radv_video_get_qp_map_texel_size(vid->vk.op);
      const uint32_t map_width = DIV_ROUND_UP(vid->vk.max_coded.width, texel_size);
      const uint32_t map_height = DIV_ROUND_UP(vid->vk.max_coded.height, texel_size);

      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, m)
      {
         m->memoryBindIndex = RADV_BIND_ENCODE_QP_MAP;
         m->memoryRequirements.size = map_width * map_height * sizeof(int16_t);
         m->memoryRequirements.alignment = 0;
         m->memoryRequirements.memoryTypeBits = memory_type_bits;
      }
   }

   if (vid->intra_only_dpb) {
      vk_outarray_append_typed(VkVideoSessionMemoryRequirementsKHR, &out, m)
      {
         m->memoryBindIndex = RADV_BIND_INTRA_ONLY;
         m->memoryRequirements.size = vid->intra_only_dpb->size;
         m->memoryRequirements.alignment = vid->intra_only_dpb->alignment;
         m->memoryRequirements.memoryTypeBits = memory_type_bits;
      }
   }

   return vk_outarray_status(&out);
}

void
radv_video_get_enc_dpb_image(struct radv_device *device, const struct VkVideoProfileListInfoKHR *profile_list,
                             struct radv_image *image, struct radv_image_create_info *create_info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint32_t luma_pitch, luma_size, chroma_size, colloc_bytes;
   uint32_t num_reconstructed_pictures = image->vk.array_layers;
   bool has_h264_b_support = false;
   bool is_av1 = false;

   for (unsigned i = 0; i < profile_list->profileCount; i++) {
      if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) {
         if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_3) {
            has_h264_b_support = true;
         }
      }
      if (profile_list->pProfiles[i].videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR) {
         is_av1 = true;
      }
   }
   dpb_image_sizes(image, &luma_pitch, &luma_size, &chroma_size, &colloc_bytes);

   image->size = 0;

   if (pdev->enc_hw_ver < RADV_VIDEO_ENC_HW_5) {
      if (has_h264_b_support)
         image->size += colloc_bytes;
      if (is_av1)
         image->size += RENCODE_AV1_SDB_FRAME_CONTEXT_SIZE;
   }

   for (unsigned i = 0; i < num_reconstructed_pictures; i++) {
      unsigned metadata_size = 0;
      if (is_av1) {
         metadata_size += RENCODE_AV1_FRAME_CONTEXT_CDF_TABLE_SIZE;
         metadata_size += RENCODE_AV1_CDEF_ALGORITHM_FRAME_CONTEXT_SIZE;
      }
      if (pdev->enc_hw_ver >= RADV_VIDEO_ENC_HW_5) {
         metadata_size += RENCODE_MAX_METADATA_BUFFER_SIZE_PER_FRAME;
         if (has_h264_b_support)
            metadata_size += colloc_bytes;
      }

      image->size += luma_size;
      image->size += chroma_size;
      image->size += align(metadata_size, ENC_ALIGNMENT);
   }
   image->alignment = ENC_ALIGNMENT;
}

bool
radv_video_encode_av1_supported(const struct radv_physical_device *pdev)
{
   if (pdev->info.vcn_ip_version >= VCN_5_0_0) {
      return true;
   } else if (pdev->info.vcn_ip_version >= VCN_4_0_0) {
      return pdev->info.vcn_ip_version != VCN_4_0_3 && pdev->info.vcn_enc_minor_version >= 20;
   } else {
      return false;
   }
}

bool
radv_video_encode_qp_map_supported(const struct radv_physical_device *pdev)
{
   if (pdev->info.vcn_ip_version >= VCN_5_0_0)
      return radv_check_vcn_fw_version(pdev, 9, 9, 28);
   return true;
}

enum radv_video_write_memory_support
radv_video_write_memory_supported(const struct radv_physical_device *pdev)
{
   if (pdev->info.vcn_ip_version >= VCN_5_0_0) {
      return RADV_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   } else if (pdev->info.vcn_ip_version >= VCN_4_0_0) {
      if (pdev->info.vcn_enc_minor_version >= 22)
         return RADV_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   } else if (pdev->info.vcn_ip_version >= VCN_3_0_0) {
      if (pdev->info.vcn_enc_minor_version >= 33)
         return RADV_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   } else if (pdev->info.vcn_ip_version >= VCN_2_0_0) {
      if (pdev->info.vcn_enc_minor_version >= 24)
         return RADV_VIDEO_WRITE_MEMORY_SUPPORT_PCIE_ATOMICS;
   }
   return RADV_VIDEO_WRITE_MEMORY_SUPPORT_NONE;
}
