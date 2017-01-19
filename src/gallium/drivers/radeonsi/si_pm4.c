/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *      Christian König <christian.koenig@amd.com>
 */

#include "radeon/r600_cs.h"
#include "util/u_memory.h"
#include "si_pipe.h"
#include "sid.h"

#define NUMBER_OF_STATES (sizeof(union si_state) / sizeof(struct si_pm4_state *))

void si_pm4_cmd_begin(struct si_pm4_state *state, unsigned opcode)
{
	state->last_opcode = opcode;
	state->last_pm4 = state->ndw++;
}

void si_pm4_cmd_add(struct si_pm4_state *state, uint32_t dw)
{
	state->pm4[state->ndw++] = dw;
}

void si_pm4_cmd_end(struct si_pm4_state *state, bool predicate)
{
	unsigned count;
	count = state->ndw - state->last_pm4 - 2;
	state->pm4[state->last_pm4] =
		PKT3(state->last_opcode, count, predicate)
		   | PKT3_SHADER_TYPE_S(state->compute_pkt);

	assert(state->ndw <= SI_PM4_MAX_DW);
}

void si_pm4_set_reg(struct si_pm4_state *state, unsigned reg, uint32_t val)
{
	unsigned opcode;

	if (reg >= SI_CONFIG_REG_OFFSET && reg < SI_CONFIG_REG_END) {
		opcode = PKT3_SET_CONFIG_REG;
		reg -= SI_CONFIG_REG_OFFSET;

	} else if (reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END) {
		opcode = PKT3_SET_SH_REG;
		reg -= SI_SH_REG_OFFSET;

	} else if (reg >= SI_CONTEXT_REG_OFFSET && reg < SI_CONTEXT_REG_END) {
		opcode = PKT3_SET_CONTEXT_REG;
		reg -= SI_CONTEXT_REG_OFFSET;

	} else if (reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END) {
		opcode = PKT3_SET_UCONFIG_REG;
		reg -= CIK_UCONFIG_REG_OFFSET;

	} else {
		R600_ERR("Invalid register offset %08x!\n", reg);
		return;
	}

	reg >>= 2;

	if (opcode != state->last_opcode || reg != (state->last_reg + 1)) {
		si_pm4_cmd_begin(state, opcode);
		si_pm4_cmd_add(state, reg);
	}

	state->last_reg = reg;
	si_pm4_cmd_add(state, val);
	si_pm4_cmd_end(state, false);
}

void si_pm4_add_bo(struct si_pm4_state *state,
                   struct r600_resource *bo,
                   enum radeon_bo_usage usage,
		   enum radeon_bo_priority priority)
{
	unsigned idx = state->nbo++;
	assert(idx < SI_PM4_MAX_BO);

	r600_resource_reference(&state->bo[idx], bo);
	state->bo_usage[idx] = usage;
	state->bo_priority[idx] = priority;
}

void si_pm4_clear_state(struct si_pm4_state *state)
{
	for (int i = 0; i < state->nbo; ++i)
		r600_resource_reference(&state->bo[i], NULL);
	r600_resource_reference(&state->indirect_buffer, NULL);
	state->nbo = 0;
	state->ndw = 0;
}

void si_pm4_free_state_simple(struct si_pm4_state *state)
{
	si_pm4_clear_state(state);
	FREE(state);
}

void si_pm4_free_state(struct si_context *sctx,
		       struct si_pm4_state *state,
		       unsigned idx)
{
	if (!state)
		return;

	if (idx != ~0 && sctx->emitted.array[idx] == state) {
		sctx->emitted.array[idx] = NULL;
	}

	si_pm4_free_state_simple(state);
}

void si_pm4_emit(struct si_context *sctx, struct si_pm4_state *state)
{
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;

	for (int i = 0; i < state->nbo; ++i) {
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, state->bo[i],
				      state->bo_usage[i], state->bo_priority[i]);
	}

	if (!state->indirect_buffer) {
		radeon_emit_array(cs, state->pm4, state->ndw);
	} else {
		struct r600_resource *ib = state->indirect_buffer;

		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, ib,
					  RADEON_USAGE_READ,
                                          RADEON_PRIO_IB2);

		radeon_emit(cs, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
		radeon_emit(cs, ib->gpu_address);
		radeon_emit(cs, (ib->gpu_address >> 32) & 0xffff);
		radeon_emit(cs, (ib->b.b.width0 >> 2) & 0xfffff);
	}
}

void si_pm4_emit_dirty(struct si_context *sctx)
{
	for (int i = 0; i < NUMBER_OF_STATES; ++i) {
		struct si_pm4_state *state = sctx->queued.array[i];

		if (!state || sctx->emitted.array[i] == state)
			continue;

		si_pm4_emit(sctx, state);
		sctx->emitted.array[i] = state;
	}
}

void si_pm4_reset_emitted(struct si_context *sctx)
{
	memset(&sctx->emitted, 0, sizeof(sctx->emitted));
}

void si_pm4_upload_indirect_buffer(struct si_context *sctx,
				   struct si_pm4_state *state)
{
	struct pipe_screen *screen = sctx->b.b.screen;
	unsigned aligned_ndw = align(state->ndw, 8);

	/* only supported on CIK and later */
	if (sctx->b.chip_class < CIK)
		return;

	assert(state->ndw);
	assert(aligned_ndw <= SI_PM4_MAX_DW);

	r600_resource_reference(&state->indirect_buffer, NULL);
	state->indirect_buffer = (struct r600_resource*)
		pipe_buffer_create(screen, 0,
				   PIPE_USAGE_DEFAULT, aligned_ndw * 4);
	if (!state->indirect_buffer)
		return;

	/* Pad the IB to 8 DWs to meet CP fetch alignment requirements. */
	if (sctx->screen->b.info.gfx_ib_pad_with_type2) {
		for (int i = state->ndw; i < aligned_ndw; i++)
			state->pm4[i] = 0x80000000; /* type2 nop packet */
	} else {
		for (int i = state->ndw; i < aligned_ndw; i++)
			state->pm4[i] = 0xffff1000; /* type3 nop packet */
	}

	pipe_buffer_write(&sctx->b.b, &state->indirect_buffer->b.b,
			  0, aligned_ndw *4, state->pm4);
}
