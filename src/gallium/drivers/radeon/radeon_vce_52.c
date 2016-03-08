/**************************************************************************
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "util/u_memory.h"

#include "vl/vl_video_buffer.h"

#include "r600_pipe_common.h"
#include "radeon_video.h"
#include "radeon_vce.h"

static const unsigned profiles[7] = { 66, 77, 88, 100, 110, 122, 244 };

static void create(struct rvce_encoder *enc)
{
	enc->task_info(enc, 0x00000000, 0, 0, 0);

	RVCE_BEGIN(0x01000001); // create cmd
	RVCE_CS(0x00000000); // encUseCircularBuffer
	RVCE_CS(profiles[enc->base.profile -
		PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE]); // encProfile
	RVCE_CS(enc->base.level); // encLevel
	RVCE_CS(0x00000000); // encPicStructRestriction
	RVCE_CS(enc->base.width); // encImageWidth
	RVCE_CS(enc->base.height); // encImageHeight
	RVCE_CS(enc->luma->level[0].pitch_bytes); // encRefPicLumaPitch
	RVCE_CS(enc->chroma->level[0].pitch_bytes); // encRefPicChromaPitch
	RVCE_CS(align(enc->luma->npix_y, 16) / 8); // encRefYHeightInQw
	RVCE_CS(0x00000000); // encRefPic(Addr|Array)Mode, encPicStructRestriction, disableRDO

	RVCE_CS(0x00000000); // encPreEncodeContextBufferOffset
	RVCE_CS(0x00000000); // encPreEncodeInputLumaBufferOffset
	RVCE_CS(0x00000000); // encPreEncodeInputChromaBufferOffs
	RVCE_CS(0x00000000); // encPreEncodeMode|ChromaFlag|VBAQMode|SceneChangeSensitivity
	RVCE_END();
}

static void encode(struct rvce_encoder *enc)
{
	signed luma_offset, chroma_offset, bs_offset;
	unsigned dep, bs_idx = enc->bs_idx++;
	int i;

	if (enc->dual_inst) {
		if (bs_idx == 0)
			dep = 1;
		else if (enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_IDR)
			dep = 0;
		else
			dep = 2;
	} else
		dep = 0;

	enc->task_info(enc, 0x00000003, dep, 0, bs_idx);

	RVCE_BEGIN(0x05000001); // context buffer
	RVCE_READWRITE(enc->cpb.res->buf, enc->cpb.res->domains, 0); // encodeContextAddressHi/Lo
	RVCE_END();

	bs_offset = -(signed)(bs_idx * enc->bs_size);

	RVCE_BEGIN(0x05000004); // video bitstream buffer
	RVCE_WRITE(enc->bs_handle, RADEON_DOMAIN_GTT, bs_offset); // videoBitstreamRingAddressHi/Lo
	RVCE_CS(enc->bs_size); // videoBitstreamRingSize
	RVCE_END();

	if (enc->dual_pipe) {
		unsigned aux_offset = enc->cpb.res->buf->size -
			RVCE_MAX_AUX_BUFFER_NUM * RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE * 2;
		RVCE_BEGIN(0x05000002); // auxiliary buffer
		for (i = 0; i < 8; ++i) {
			RVCE_CS(aux_offset);
			aux_offset += RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE;
		}
		for (i = 0; i < 8; ++i)
			RVCE_CS(RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE);
		RVCE_END();
	}

	RVCE_BEGIN(0x03000001); // encode
	RVCE_CS(enc->pic.frame_num ? 0x0 : 0x11); // insertHeaders
	RVCE_CS(0x00000000); // pictureStructure
	RVCE_CS(enc->bs_size); // allowedMaxBitstreamSize
	RVCE_CS(0x00000000); // forceRefreshMap
	RVCE_CS(0x00000000); // insertAUD
	RVCE_CS(0x00000000); // endOfSequence
	RVCE_CS(0x00000000); // endOfStream
	RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
		enc->luma->level[0].offset); // inputPictureLumaAddressHi/Lo
	RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
		enc->chroma->level[0].offset); // inputPictureChromaAddressHi/Lo
	RVCE_CS(align(enc->luma->npix_y, 16)); // encInputFrameYPitch
	RVCE_CS(enc->luma->level[0].pitch_bytes); // encInputPicLumaPitch
	RVCE_CS(enc->chroma->level[0].pitch_bytes); // encInputPicChromaPitch
	if (enc->dual_pipe)
		RVCE_CS(0x00000000); // encInputPic(Addr|Array)Mode,encDisable(TwoPipeMode|MBOffloading)
	else
		RVCE_CS(0x00010000); // encInputPic(Addr|Array)Mode,encDisable(TwoPipeMode|MBOffloading)
	RVCE_CS(0x00000000); // encInputPicTileConfig
	RVCE_CS(enc->pic.picture_type); // encPicType
	RVCE_CS(enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_IDR); // encIdrFlag
	RVCE_CS(0x00000000); // encIdrPicId
	RVCE_CS(0x00000000); // encMGSKeyPic
	RVCE_CS(!enc->pic.not_referenced); // encReferenceFlag
	RVCE_CS(0x00000000); // encTemporalLayerIndex
	RVCE_CS(0x00000000); // num_ref_idx_active_override_flag
	RVCE_CS(0x00000000); // num_ref_idx_l0_active_minus1
	RVCE_CS(0x00000000); // num_ref_idx_l1_active_minus1

	i = enc->pic.frame_num - enc->pic.ref_idx_l0;
	if (i > 1 && enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_P) {
		RVCE_CS(0x00000001); // encRefListModificationOp
		RVCE_CS(i - 1);      // encRefListModificationNum
	} else {
		RVCE_CS(0x00000000); // encRefListModificationOp
		RVCE_CS(0x00000000); // encRefListModificationNum
	}

	for (i = 0; i < 3; ++i) {
		RVCE_CS(0x00000000); // encRefListModificationOp
		RVCE_CS(0x00000000); // encRefListModificationNum
	}
	for (i = 0; i < 4; ++i) {
		RVCE_CS(0x00000000); // encDecodedPictureMarkingOp
		RVCE_CS(0x00000000); // encDecodedPictureMarkingNum
		RVCE_CS(0x00000000); // encDecodedPictureMarkingIdx
		RVCE_CS(0x00000000); // encDecodedRefBasePictureMarkingOp
		RVCE_CS(0x00000000); // encDecodedRefBasePictureMarkingNum
	}

	// encReferencePictureL0[0]
	RVCE_CS(0x00000000); // pictureStructure
	if(enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_P ||
	   enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_B) {
		struct rvce_cpb_slot *l0 = l0_slot(enc);
		rvce_frame_offset(enc, l0, &luma_offset, &chroma_offset);
		RVCE_CS(l0->picture_type); // encPicType
		RVCE_CS(l0->frame_num); // frameNumber
		RVCE_CS(l0->pic_order_cnt); // pictureOrderCount
		RVCE_CS(luma_offset); // lumaOffset
		RVCE_CS(chroma_offset); // chromaOffset
	} else {
		RVCE_CS(0x00000000); // encPicType
		RVCE_CS(0x00000000); // frameNumber
		RVCE_CS(0x00000000); // pictureOrderCount
		RVCE_CS(0xffffffff); // lumaOffset
		RVCE_CS(0xffffffff); // chromaOffset
	}

	// encReferencePictureL0[1]
	RVCE_CS(0x00000000); // pictureStructure
	RVCE_CS(0x00000000); // encPicType
	RVCE_CS(0x00000000); // frameNumber
	RVCE_CS(0x00000000); // pictureOrderCount
	RVCE_CS(0xffffffff); // lumaOffset
	RVCE_CS(0xffffffff); // chromaOffset

	// encReferencePictureL1[0]
	RVCE_CS(0x00000000); // pictureStructure
	if(enc->pic.picture_type == PIPE_H264_ENC_PICTURE_TYPE_B) {
		struct rvce_cpb_slot *l1 = l1_slot(enc);
		rvce_frame_offset(enc, l1, &luma_offset, &chroma_offset);
		RVCE_CS(l1->picture_type); // encPicType
		RVCE_CS(l1->frame_num); // frameNumber
		RVCE_CS(l1->pic_order_cnt); // pictureOrderCount
		RVCE_CS(luma_offset); // lumaOffset
		RVCE_CS(chroma_offset); // chromaOffset
	} else {
		RVCE_CS(0x00000000); // encPicType
		RVCE_CS(0x00000000); // frameNumber
		RVCE_CS(0x00000000); // pictureOrderCount
		RVCE_CS(0xffffffff); // lumaOffset
		RVCE_CS(0xffffffff); // chromaOffset
	}

	rvce_frame_offset(enc, current_slot(enc), &luma_offset, &chroma_offset);
	RVCE_CS(luma_offset); // encReconstructedLumaOffset
	RVCE_CS(chroma_offset); // encReconstructedChromaOffset
	RVCE_CS(0x00000000); // encColocBufferOffset
	RVCE_CS(0x00000000); // encReconstructedRefBasePictureLumaOffset
	RVCE_CS(0x00000000); // encReconstructedRefBasePictureChromaOffset
	RVCE_CS(0x00000000); // encReferenceRefBasePictureLumaOffset
	RVCE_CS(0x00000000); // encReferenceRefBasePictureChromaOffset
	RVCE_CS(0x00000000); // pictureCount
	RVCE_CS(enc->pic.frame_num); // frameNumber
	RVCE_CS(enc->pic.pic_order_cnt); // pictureOrderCount
	RVCE_CS(0x00000000); // numIPicRemainInRCGOP
	RVCE_CS(0x00000000); // numPPicRemainInRCGOP
	RVCE_CS(0x00000000); // numBPicRemainInRCGOP
	RVCE_CS(0x00000000); // numIRPicRemainInRCGOP
	RVCE_CS(0x00000000); // enableIntraRefresh

	RVCE_CS(0x00000000); // aq_variance_en
	RVCE_CS(0x00000000); // aq_block_size
	RVCE_CS(0x00000000); // aq_mb_variance_sel
	RVCE_CS(0x00000000); // aq_frame_variance_sel
	RVCE_CS(0x00000000); // aq_param_a
	RVCE_CS(0x00000000); // aq_param_b
	RVCE_CS(0x00000000); // aq_param_c
	RVCE_CS(0x00000000); // aq_param_d
	RVCE_CS(0x00000000); // aq_param_e

	RVCE_CS(0x00000000); // contextInSFB
	RVCE_END();
}

void radeon_vce_52_init(struct rvce_encoder *enc)
{
	radeon_vce_50_init(enc);

	enc->create = create;
	enc->encode = encode;
}
