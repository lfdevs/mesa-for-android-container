/**************************************************************************
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#include <stdio.h>

#include "pipe/p_video_codec.h"

#include "util/u_video.h"

#include "si_pipe.h"
#include "radeon_vcn_enc.h"

#define RENCODE_FW_INTERFACE_MAJOR_VERSION   1
#define RENCODE_FW_INTERFACE_MINOR_VERSION   0

static void radeon_enc_sq_begin(struct radeon_encoder *enc)
{
   rvcn_sq_header(&enc->cs, &enc->sq, true);
   enc->mq_begin(enc);
   rvcn_sq_tail(&enc->cs, &enc->sq);
}

static void radeon_enc_sq_encode(struct radeon_encoder *enc)
{
   rvcn_sq_header(&enc->cs, &enc->sq, true);
   enc->mq_encode(enc);
   rvcn_sq_tail(&enc->cs, &enc->sq);
}

static void radeon_enc_sq_destroy(struct radeon_encoder *enc)
{
   rvcn_sq_header(&enc->cs, &enc->sq, true);
   enc->mq_destroy(enc);
   rvcn_sq_tail(&enc->cs, &enc->sq);
}

static void radeon_enc_session_init(struct radeon_encoder *enc)
{
   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      enc->enc_pic.session_init.encode_standard = RENCODE_ENCODE_STANDARD_H264;
      enc->enc_pic.session_init.aligned_picture_width = align(enc->base.width, 16);
   } else if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC) {
      enc->enc_pic.session_init.encode_standard = RENCODE_ENCODE_STANDARD_HEVC;
      enc->enc_pic.session_init.aligned_picture_width = align(enc->base.width, 64);
   }
   enc->enc_pic.session_init.aligned_picture_height = align(enc->base.height, 16);
   enc->enc_pic.session_init.padding_width =
      enc->enc_pic.session_init.aligned_picture_width - enc->base.width;
   enc->enc_pic.session_init.padding_height =
      enc->enc_pic.session_init.aligned_picture_height - enc->base.height;
   enc->enc_pic.session_init.pre_encode_mode = RENCODE_PREENCODE_MODE_NONE;
   enc->enc_pic.session_init.pre_encode_chroma_enabled = false;
   enc->enc_pic.session_init.slice_output_enabled = false;
   enc->enc_pic.session_init.display_remote = 0;

   RADEON_ENC_BEGIN(enc->cmd.session_init);
   RADEON_ENC_CS(enc->enc_pic.session_init.encode_standard);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_mode);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_chroma_enabled);
   RADEON_ENC_CS(enc->enc_pic.session_init.slice_output_enabled);
   RADEON_ENC_CS(enc->enc_pic.session_init.display_remote);
   RADEON_ENC_END();
}

static void radeon_enc_ctx(struct radeon_encoder *enc)
{
   enc->enc_pic.ctx_buf.swizzle_mode = 0;
   enc->enc_pic.ctx_buf.two_pass_search_center_map_offset = 0;
   enc->enc_pic.ctx_buf.colloc_buffer_offset = enc->dpb_size;

   uint32_t aligned_width = enc->enc_pic.session_init.aligned_picture_width;
   uint32_t aligned_height = enc->enc_pic.session_init.aligned_picture_height;

   enc->enc_pic.ctx_buf.rec_luma_pitch = align(aligned_width, enc->alignment);
   enc->enc_pic.ctx_buf.rec_chroma_pitch = align(aligned_width, enc->alignment);

   int luma_size = enc->enc_pic.ctx_buf.rec_luma_pitch * align(aligned_height, enc->alignment);
   if (enc->enc_pic.bit_depth_luma_minus8 == 2)
      luma_size *= 2;
   int chroma_size = align(luma_size / 2, enc->alignment);
   int offset = 0;

   for (int i = 0; i < enc->enc_pic.ctx_buf.num_reconstructed_pictures; i++) {
      offset += luma_size;
      offset += chroma_size;
   }

   assert(offset == enc->dpb_size);

   RADEON_ENC_BEGIN(enc->cmd.ctx);
   RADEON_ENC_READWRITE(enc->cpb.res->buf, enc->cpb.res->domains, 0);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.num_reconstructed_pictures);

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures_v4_0[i].luma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures_v4_0[i].chroma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures_v4_0[i].unused.unused_offset1);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures_v4_0[i].unused.unused_offset2);
   }

   //  2: 1 pre encode pitch * 2 (luma + chroma)
   // 136: 34 pre encode reconstructed pics * 4 (luma + chroma offsets + unused union)
   //  3: 1 pre encode input pic * 3 (r,g,b offset union)
   //----
   // 141

   for (int i = 0; i < 141; i++)
      RADEON_ENC_CS(0x00000000);

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.two_pass_search_center_map_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.colloc_buffer_offset);
   RADEON_ENC_END();
}

void radeon_enc_4_0_init(struct radeon_encoder *enc)
{
   radeon_enc_3_0_init(enc);

   enc->session_init = radeon_enc_session_init;
   enc->ctx = radeon_enc_ctx;
   enc->mq_begin = enc->begin;
   enc->mq_encode = enc->encode;
   enc->mq_destroy = enc->destroy;
   enc->begin = radeon_enc_sq_begin;
   enc->encode = radeon_enc_sq_encode;
   enc->destroy = radeon_enc_sq_destroy;

   enc->enc_pic.session_info.interface_version =
      ((RENCODE_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
      (RENCODE_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
}
