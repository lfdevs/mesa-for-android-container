/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_CONTEXT_H_
#define FD6_CONTEXT_H_

#include "util/u_upload_mgr.h"

#include "freedreno_context.h"

#include "ir3/ir3_shader.h"

#include "a6xx.xml.h"

struct fd6_context {
	struct fd_context base;

	/* Two buffers related to hw binning / visibility stream (VSC).
	 * Compared to previous generations
	 *   (1) we cannot specify individual buffers per VSC, instead
	 *       just a pitch and base address
	 *   (2) there is a second smaller buffer, for something.. we
	 *       also stash VSC_BIN_SIZE at end of 2nd buffer.
	 */
	struct fd_bo *vsc_data, *vsc_data2;

	unsigned vsc_data_pitch, vsc_data2_pitch;

	/* The 'control' mem BO is used for various housekeeping
	 * functions.  See 'struct fd6_control'
	 */
	struct fd_bo *control_mem;
	uint32_t seqno;

	struct u_upload_mgr *border_color_uploader;
	struct pipe_resource *border_color_buf;

	/* if *any* of bits are set in {v,f}saturate_{s,t,r} */
	bool vsaturate, fsaturate;

	/* bitmask of sampler which needs coords clamped for vertex
	 * shader:
	 */
	uint16_t vsaturate_s, vsaturate_t, vsaturate_r;

	/* bitmask of sampler which needs coords clamped for frag
	 * shader:
	 */
	uint16_t fsaturate_s, fsaturate_t, fsaturate_r;

	/* some state changes require a different shader variant.  Keep
	 * track of this so we know when we need to re-emit shader state
	 * due to variant change.  See fixup_shader_state()
	 */
	struct ir3_shader_key last_key;

	/* number of active samples-passed queries: */
	int samples_passed_queries;

	/* maps per-shader-stage state plus variant key to hw
	 * program stateobj:
	 */
	struct ir3_cache *shader_cache;

	/* cached stateobjs to avoid hashtable lookup when not dirty: */
	const struct fd6_program_state *prog;

	uint16_t tex_seqno;
	struct hash_table *tex_cache;

	/* collection of magic register values which differ between
	 * various different a6xx
	 */
	struct {
		uint32_t RB_UNKNOWN_8E04_blit;    /* value for CP_BLIT's */
		uint32_t RB_CCU_CNTL_bypass;      /* for sysmem rendering */
		uint32_t RB_CCU_CNTL_gmem;        /* for GMEM rendering */
		uint32_t PC_UNKNOWN_9805;
		uint32_t SP_UNKNOWN_A0F8;

	} magic;
};

static inline struct fd6_context *
fd6_context(struct fd_context *ctx)
{
	return (struct fd6_context *)ctx;
}

struct pipe_context *
fd6_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags);


/* This struct defines the layout of the fd6_context::control buffer: */
struct fd6_control {
	uint32_t seqno;          /* seqno for async CP_EVENT_WRITE, etc */
	uint32_t _pad0;
	volatile uint32_t vsc_overflow;
	uint32_t _pad1;
	/* flag set from cmdstream when VSC overflow detected: */
	uint32_t vsc_scratch;
	uint32_t _pad2;
	uint32_t _pad3;
	uint32_t _pad4;

	/* scratch space for VPC_SO[i].FLUSH_BASE_LO/HI, start on 32 byte boundary. */
	struct {
		uint32_t offset;
		uint32_t pad[7];
	} flush_base[4];
};

#define control_ptr(fd6_ctx, member)  \
	(fd6_ctx)->control_mem, offsetof(struct fd6_control, member), 0, 0


static inline void
emit_marker6(struct fd_ringbuffer *ring, int scratch_idx)
{
	extern unsigned marker_cnt;
	unsigned reg = REG_A6XX_CP_SCRATCH_REG(scratch_idx);
	OUT_PKT4(ring, reg, 1);
	OUT_RING(ring, ++marker_cnt);
}

#endif /* FD6_CONTEXT_H_ */
