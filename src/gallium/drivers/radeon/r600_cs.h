/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * Authors: Marek Olšák <maraeo@gmail.com>
 */

/**
 * This file contains helpers for writing commands to commands streams.
 */

#ifndef R600_CS_H
#define R600_CS_H

#include "r600_pipe_common.h"
#include "r600d_common.h"

/**
 * Add a buffer to the buffer list for the given command stream (CS).
 *
 * All buffers used by a CS must be added to the list. This tells the kernel
 * driver which buffers are used by GPU commands. Other buffers can
 * be swapped out (not accessible) during execution.
 *
 * The buffer list becomes empty after every context flush and must be
 * rebuilt.
 */
static inline unsigned radeon_add_to_buffer_list(struct r600_common_context *rctx,
						 struct r600_ring *ring,
						 struct r600_resource *rbo,
						 enum radeon_bo_usage usage,
						 enum radeon_bo_priority priority)
{
	assert(usage);
	return rctx->ws->cs_add_buffer(ring->cs, rbo->buf, usage,
				      rbo->domains, priority) * 4;
}

static inline void r600_emit_reloc(struct r600_common_context *rctx,
				   struct r600_ring *ring, struct r600_resource *rbo,
				   enum radeon_bo_usage usage,
				   enum radeon_bo_priority priority)
{
	struct radeon_winsys_cs *cs = ring->cs;
	bool has_vm = ((struct r600_common_screen*)rctx->b.screen)->info.has_virtual_memory;
	unsigned reloc = radeon_add_to_buffer_list(rctx, ring, rbo, usage, priority);

	if (!has_vm) {
		radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
		radeon_emit(cs, reloc);
	}
}

static inline void radeon_set_config_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg < R600_CONTEXT_REG_OFFSET);
	assert(cs->cdw+2+num <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_CONFIG_REG, num, 0));
	radeon_emit(cs, (reg - R600_CONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_config_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_config_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg >= R600_CONTEXT_REG_OFFSET);
	assert(cs->cdw+2+num <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, num, 0));
	radeon_emit(cs, (reg - R600_CONTEXT_REG_OFFSET) >> 2);
}

static inline void radeon_set_context_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_context_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_sh_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END);
	assert(cs->cdw+2+num <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_SH_REG, num, 0));
	radeon_emit(cs, (reg - SI_SH_REG_OFFSET) >> 2);
}

static inline void radeon_set_sh_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_sh_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
	assert(cs->cdw+2+num <= cs->max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, num, 0));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_uconfig_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_uconfig_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

#endif
