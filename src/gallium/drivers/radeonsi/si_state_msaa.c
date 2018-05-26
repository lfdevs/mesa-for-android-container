/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "si_build_pm4.h"

/* For MSAA sample positions. */
#define FILL_SREG(s0x, s0y, s1x, s1y, s2x, s2y, s3x, s3y)  \
	(((s0x) & 0xf) | (((unsigned)(s0y) & 0xf) << 4) |		   \
	(((unsigned)(s1x) & 0xf) << 8) | (((unsigned)(s1y) & 0xf) << 12) |	   \
	(((unsigned)(s2x) & 0xf) << 16) | (((unsigned)(s2y) & 0xf) << 20) |	   \
	 (((unsigned)(s3x) & 0xf) << 24) | (((unsigned)(s3y) & 0xf) << 28))

/* 2xMSAA
 * There are two locations (4, 4), (-4, -4). */
static const uint32_t sample_locs_2x[4] = {
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
};
/* 4xMSAA
 * There are 4 locations: (-2, -6), (6, -2), (-6, 2), (2, 6). */
static const uint32_t sample_locs_4x[4] = {
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
};

/* Cayman 8xMSAA */
static const uint32_t sample_locs_8x[] = {
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
};
/* Cayman 16xMSAA */
static const uint32_t sample_locs_16x[] = {
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
};

static void si_get_sample_position(struct pipe_context *ctx, unsigned sample_count,
				   unsigned sample_index, float *out_value)
{
	int offset, index;
	struct {
		int idx:4;
	} val;

	switch (sample_count) {
	case 1:
	default:
		out_value[0] = out_value[1] = 0.5;
		break;
	case 2:
		offset = 4 * (sample_index * 2);
		val.idx = (sample_locs_2x[0] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (sample_locs_2x[0] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	case 4:
		offset = 4 * (sample_index * 2);
		val.idx = (sample_locs_4x[0] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (sample_locs_4x[0] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	case 8:
		offset = 4 * (sample_index % 4 * 2);
		index = (sample_index / 4) * 4;
		val.idx = (sample_locs_8x[index] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (sample_locs_8x[index] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	case 16:
		offset = 4 * (sample_index % 4 * 2);
		index = (sample_index / 4) * 4;
		val.idx = (sample_locs_16x[index] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (sample_locs_16x[index] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	}
}

void si_emit_sample_locations(struct radeon_winsys_cs *cs, int nr_samples)
{
	switch (nr_samples) {
	default:
	case 1:
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 0);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, 0);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, 0);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, 0);
		break;
	case 2:
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs_2x[0]);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs_2x[1]);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs_2x[2]);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs_2x[3]);
		break;
	case 4:
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, sample_locs_4x[0]);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, sample_locs_4x[1]);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, sample_locs_4x[2]);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, sample_locs_4x[3]);
		break;
	case 8:
		radeon_set_context_reg_seq(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 14);
		radeon_emit(cs, sample_locs_8x[0]);
		radeon_emit(cs, sample_locs_8x[4]);
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		radeon_emit(cs, sample_locs_8x[1]);
		radeon_emit(cs, sample_locs_8x[5]);
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		radeon_emit(cs, sample_locs_8x[2]);
		radeon_emit(cs, sample_locs_8x[6]);
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		radeon_emit(cs, sample_locs_8x[3]);
		radeon_emit(cs, sample_locs_8x[7]);
		break;
	case 16:
		radeon_set_context_reg_seq(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 16);
		radeon_emit(cs, sample_locs_16x[0]);
		radeon_emit(cs, sample_locs_16x[4]);
		radeon_emit(cs, sample_locs_16x[8]);
		radeon_emit(cs, sample_locs_16x[12]);
		radeon_emit(cs, sample_locs_16x[1]);
		radeon_emit(cs, sample_locs_16x[5]);
		radeon_emit(cs, sample_locs_16x[9]);
		radeon_emit(cs, sample_locs_16x[13]);
		radeon_emit(cs, sample_locs_16x[2]);
		radeon_emit(cs, sample_locs_16x[6]);
		radeon_emit(cs, sample_locs_16x[10]);
		radeon_emit(cs, sample_locs_16x[14]);
		radeon_emit(cs, sample_locs_16x[3]);
		radeon_emit(cs, sample_locs_16x[7]);
		radeon_emit(cs, sample_locs_16x[11]);
		radeon_emit(cs, sample_locs_16x[15]);
		break;
	}
}

void si_init_msaa_functions(struct si_context *sctx)
{
	int i;

	sctx->b.get_sample_position = si_get_sample_position;

	si_get_sample_position(&sctx->b, 1, 0, sctx->sample_locations_1x[0]);

	for (i = 0; i < 2; i++)
		si_get_sample_position(&sctx->b, 2, i, sctx->sample_locations_2x[i]);
	for (i = 0; i < 4; i++)
		si_get_sample_position(&sctx->b, 4, i, sctx->sample_locations_4x[i]);
	for (i = 0; i < 8; i++)
		si_get_sample_position(&sctx->b, 8, i, sctx->sample_locations_8x[i]);
	for (i = 0; i < 16; i++)
		si_get_sample_position(&sctx->b, 16, i, sctx->sample_locations_16x[i]);
}
