/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADEON_BITSTREAM_H
#define RADEON_BITSTREAM_H

#include "pipe/p_video_state.h"
#include "winsys/radeon_winsys.h"

#define AV1_SELECT_SCREEN_CONTENT_TOOLS  2
#define AV1_SELECT_INTEGER_MV            2

struct radeon_bitstream {
   bool emulation_prevention;
   uint32_t shifter;
   uint32_t bits_in_shifter;
   uint32_t num_zeros;
   uint32_t byte_index;
   uint32_t bits_output;
   uint32_t bits_size;
   uint8_t *buf;
   struct radeon_cmdbuf *cs;
};

void radeon_bs_reset(struct radeon_bitstream *bs, uint8_t *out, struct radeon_cmdbuf *cs);
void radeon_bs_set_emulation_prevention(struct radeon_bitstream *bs, bool set);
void radeon_bs_byte_align(struct radeon_bitstream *bs);
void radeon_bs_flush_headers(struct radeon_bitstream *bs);

void radeon_bs_code_fixed_bits(struct radeon_bitstream *bs, uint32_t value, uint32_t num_bits);
void radeon_bs_code_ue(struct radeon_bitstream *bs, uint32_t value);
void radeon_bs_code_se(struct radeon_bitstream *bs, int32_t value);
void radeon_bs_code_uvlc(struct radeon_bitstream *bs, uint32_t value);
void radeon_bs_code_ns(struct radeon_bitstream *bs, uint32_t value, uint32_t max);

void radeon_bs_h264_sps(struct radeon_bitstream *bs, uint8_t nal_byte, struct pipe_h264_enc_seq_param *sps);
void radeon_bs_h264_pps(struct radeon_bitstream *bs, uint8_t nal_byte, struct pipe_h264_enc_pic_control *pps);
void radeon_bs_hevc_vps(struct radeon_bitstream *bs, struct pipe_h265_enc_vid_param *vps);
void radeon_bs_hevc_sps(struct radeon_bitstream *bs, struct pipe_h265_enc_seq_param *sps);
void radeon_bs_hevc_pps(struct radeon_bitstream *bs, struct pipe_h265_enc_pic_param *pps);
void radeon_bs_av1_seq(struct radeon_bitstream *bs, uint8_t *obu_bytes, struct pipe_av1_enc_seq_param *seq);

uint32_t radeon_bs_hevc_st_ref_pic_set(struct radeon_bitstream *bs,
                                       uint32_t index,
                                       uint32_t num_short_term_ref_pic_sets,
                                       struct pipe_h265_st_ref_pic_set *st_rps);

#endif
