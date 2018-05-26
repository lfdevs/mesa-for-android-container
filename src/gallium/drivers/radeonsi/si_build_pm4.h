/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 */

/**
 * This file contains helpers for writing commands to commands streams.
 */

#ifndef SI_BUILD_PM4_H
#define SI_BUILD_PM4_H

#include "si_pipe.h"
#include "sid.h"

static inline void radeon_set_config_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg < SI_CONTEXT_REG_OFFSET);
	assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_CONFIG_REG, num, 0));
	radeon_emit(cs, (reg - SI_CONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_config_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_config_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg >= SI_CONTEXT_REG_OFFSET);
	assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, num, 0));
	radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2);
}

static inline void radeon_set_context_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_context_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_context_reg_idx(struct radeon_winsys_cs *cs,
					      unsigned reg, unsigned idx,
					      unsigned value)
{
	assert(reg >= SI_CONTEXT_REG_OFFSET);
	assert(cs->current.cdw + 3 <= cs->current.max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_CONTEXT_REG, 1, 0));
	radeon_emit(cs, (reg - SI_CONTEXT_REG_OFFSET) >> 2 | (idx << 28));
	radeon_emit(cs, value);
}

static inline void radeon_set_sh_reg_seq(struct radeon_winsys_cs *cs, unsigned reg, unsigned num)
{
	assert(reg >= SI_SH_REG_OFFSET && reg < SI_SH_REG_END);
	assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
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
	assert(cs->current.cdw + 2 + num <= cs->current.max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, num, 0));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2);
}

static inline void radeon_set_uconfig_reg(struct radeon_winsys_cs *cs, unsigned reg, unsigned value)
{
	radeon_set_uconfig_reg_seq(cs, reg, 1);
	radeon_emit(cs, value);
}

static inline void radeon_set_uconfig_reg_idx(struct radeon_winsys_cs *cs,
					      unsigned reg, unsigned idx,
					      unsigned value)
{
	assert(reg >= CIK_UCONFIG_REG_OFFSET && reg < CIK_UCONFIG_REG_END);
	assert(cs->current.cdw + 3 <= cs->current.max_dw);
	radeon_emit(cs, PKT3(PKT3_SET_UCONFIG_REG, 1, 0));
	radeon_emit(cs, (reg - CIK_UCONFIG_REG_OFFSET) >> 2 | (idx << 28));
	radeon_emit(cs, value);
}

#endif
