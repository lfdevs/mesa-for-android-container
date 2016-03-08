/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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

#ifndef IR3_SHADER_H_
#define IR3_SHADER_H_

#include "pipe/p_state.h"
#include "compiler/shader_enums.h"

#include "ir3.h"
#include "disasm.h"

/* driver param indices: */
enum ir3_driver_param {
	IR3_DP_VTXID_BASE = 0,
	IR3_DP_VTXCNT_MAX = 1,
	/* user-clip-plane components, up to 8x vec4's: */
	IR3_DP_UCP0_X     = 4,
	/* .... */
	IR3_DP_UCP7_W     = 35,
	IR3_DP_COUNT      = 36   /* must be aligned to vec4 */
};

/* Layout of constant registers:
 *
 *    num_uniform * vec4  -  user consts
 *    4 * vec4            -  UBO addresses
 *    if (vertex shader) {
 *        N * vec4        -  driver params (IR3_DP_*)
 *        1 * vec4        -  stream-out addresses
 *    }
 *
 * TODO this could be made more dynamic, to at least skip sections
 * that we don't need..
 */
#define IR3_UBOS_OFF         0  /* UBOs after user consts */
#define IR3_DRIVER_PARAM_OFF 4  /* driver params after UBOs */
#define IR3_TFBOS_OFF       (IR3_DRIVER_PARAM_OFF + IR3_DP_COUNT/4)

/* Configuration key used to identify a shader variant.. different
 * shader variants can be used to implement features not supported
 * in hw (two sided color), binning-pass vertex shader, etc.
 */
struct ir3_shader_key {
	union {
		struct {
			/*
			 * Combined Vertex/Fragment shader parameters:
			 */
			unsigned ucp_enables : 8;

			/* do we need to check {v,f}saturate_{s,t,r}? */
			unsigned has_per_samp : 1;

			/*
			 * Vertex shader variant parameters:
			 */
			unsigned binning_pass : 1;

			/*
			 * Fragment shader variant parameters:
			 */
			unsigned color_two_side : 1;
			unsigned half_precision : 1;
			/* used when shader needs to handle flat varyings (a4xx)
			 * for front/back color inputs to frag shader:
			 */
			unsigned rasterflat : 1;
		};
		uint32_t global;
	};

	/* bitmask of sampler which needs coords clamped for vertex
	 * shader:
	 */
	uint16_t vsaturate_s, vsaturate_t, vsaturate_r;

	/* bitmask of sampler which needs coords clamped for frag
	 * shader:
	 */
	uint16_t fsaturate_s, fsaturate_t, fsaturate_r;
};

static inline bool
ir3_shader_key_equal(struct ir3_shader_key *a, struct ir3_shader_key *b)
{
	/* slow-path if we need to check {v,f}saturate_{s,t,r} */
	if (a->has_per_samp || b->has_per_samp)
		return memcmp(a, b, sizeof(struct ir3_shader_key)) == 0;
	return a->global == b->global;
}

struct ir3_shader_variant {
	struct fd_bo *bo;

	/* variant id (for debug) */
	uint32_t id;

	struct ir3_shader_key key;

	struct ir3_info info;
	struct ir3 *ir;

	/* the instructions length is in units of instruction groups
	 * (4 instructions for a3xx, 16 instructions for a4xx.. each
	 * instruction is 2 dwords):
	 */
	unsigned instrlen;

	/* the constants length is in units of vec4's, and is the sum of
	 * the uniforms and the built-in compiler constants
	 */
	unsigned constlen;

	/* About Linkage:
	 *   + Let the frag shader determine the position/compmask for the
	 *     varyings, since it is the place where we know if the varying
	 *     is actually used, and if so, which components are used.  So
	 *     what the hw calls "outloc" is taken from the "inloc" of the
	 *     frag shader.
	 *   + From the vert shader, we only need the output regid
	 */

	/* for frag shader, pos_regid holds the frag_pos, ie. what is passed
	 * to bary.f instructions
	 */
	uint8_t pos_regid;
	bool frag_coord, frag_face, color0_mrt;

	/* NOTE: for input/outputs, slot is:
	 *   gl_vert_attrib  - for VS inputs
	 *   gl_varying_slot - for VS output / FS input
	 *   gl_frag_result  - for FS output
	 */

	/* varyings/outputs: */
	unsigned outputs_count;
	struct {
		uint8_t slot;
		uint8_t regid;
	} outputs[16 + 2];  /* +POSITION +PSIZE */
	bool writes_pos, writes_psize;

	/* attributes (VS) / varyings (FS):
	 * Note that sysval's should come *after* normal inputs.
	 */
	unsigned inputs_count;
	struct {
		uint8_t slot;
		uint8_t regid;
		uint8_t compmask;
		uint8_t ncomp;
		/* In theory inloc of fs should match outloc of vs.  Or
		 * rather the outloc of the vs is 8 plus the offset passed
		 * to bary.f.  Presumably that +8 is to account for
		 * gl_Position/gl_PointSize?
		 *
		 * NOTE inloc is currently aligned to 4 (we don't try
		 * to pack varyings).  Changing this would likely break
		 * assumptions in few places (like setting up of flat
		 * shading in fd3_program) so be sure to check all the
		 * spots where inloc is used.
		 */
		uint8_t inloc;
		/* vertex shader specific: */
		bool    sysval     : 1;   /* slot is a gl_system_value */
		/* fragment shader specific: */
		bool    bary       : 1;   /* fetched varying (vs one loaded into reg) */
		bool    rasterflat : 1;   /* special handling for emit->rasterflat */
		enum glsl_interp_qualifier interpolate;
	} inputs[16 + 2];  /* +POSITION +FACE */

	/* sum of input components (scalar).  For frag shaders, it only counts
	 * the varying inputs:
	 */
	unsigned total_in;

	/* For frag shaders, the total number of inputs (not scalar,
	 * ie. SP_VS_PARAM_REG.TOTALVSOUTVAR)
	 */
	unsigned varying_in;

	/* do we have one or more texture sample instructions: */
	bool has_samp;

	/* do we have kill instructions: */
	bool has_kill;

	/* const reg # of first immediate, ie. 1 == c1
	 * (not regid, because TGSI thinks in terms of vec4 registers,
	 * not scalar registers)
	 */
	unsigned first_driver_param;
	unsigned first_immediate;
	unsigned immediates_count;
	struct {
		uint32_t val[4];
	} immediates[64];

	/* shader variants form a linked list: */
	struct ir3_shader_variant *next;

	/* replicated here to avoid passing extra ptrs everywhere: */
	enum shader_t type;
	struct ir3_shader *shader;
};

typedef struct nir_shader nir_shader;

struct ir3_shader {
	enum shader_t type;

	/* shader id (for debug): */
	uint32_t id;
	uint32_t variant_count;

	struct ir3_compiler *compiler;

	struct pipe_context *pctx;    /* TODO replace w/ pipe_screen */
	nir_shader *nir;
	struct pipe_stream_output_info stream_output;

	struct ir3_shader_variant *variants;
};

void * ir3_shader_assemble(struct ir3_shader_variant *v, uint32_t gpu_id);

struct ir3_shader * ir3_shader_create(struct pipe_context *pctx,
		const struct pipe_shader_state *cso, enum shader_t type);
void ir3_shader_destroy(struct ir3_shader *shader);
struct ir3_shader_variant * ir3_shader_variant(struct ir3_shader *shader,
		struct ir3_shader_key key);
void ir3_shader_disasm(struct ir3_shader_variant *so, uint32_t *bin);

struct fd_ringbuffer;
void ir3_emit_consts(struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		const struct pipe_draw_info *info, uint32_t dirty);

static inline const char *
ir3_shader_stage(struct ir3_shader *shader)
{
	switch (shader->type) {
	case SHADER_VERTEX:     return "VERT";
	case SHADER_FRAGMENT:   return "FRAG";
	case SHADER_COMPUTE:    return "CL";
	default:
		unreachable("invalid type");
		return NULL;
	}
}

/*
 * Helper/util:
 */

#include "pipe/p_shader_tokens.h"

static inline int
ir3_find_output(const struct ir3_shader_variant *so, gl_varying_slot slot)
{
	int j;

	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].slot == slot)
			return j;

	/* it seems optional to have a OUT.BCOLOR[n] for each OUT.COLOR[n]
	 * in the vertex shader.. but the fragment shader doesn't know this
	 * so  it will always have both IN.COLOR[n] and IN.BCOLOR[n].  So
	 * at link time if there is no matching OUT.BCOLOR[n], we must map
	 * OUT.COLOR[n] to IN.BCOLOR[n].  And visa versa if there is only
	 * a OUT.BCOLOR[n] but no matching OUT.COLOR[n]
	 */
	if (slot == VARYING_SLOT_BFC0) {
		slot = VARYING_SLOT_COL0;
	} else if (slot == VARYING_SLOT_BFC1) {
		slot = VARYING_SLOT_COL1;
	} else if (slot == VARYING_SLOT_COL0) {
		slot = VARYING_SLOT_BFC0;
	} else if (slot == VARYING_SLOT_COL1) {
		slot = VARYING_SLOT_BFC1;
	} else {
		return 0;
	}

	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].slot == slot)
			return j;

	debug_assert(0);

	return 0;
}

static inline int
ir3_next_varying(const struct ir3_shader_variant *so, int i)
{
	while (++i < so->inputs_count)
		if (so->inputs[i].compmask && so->inputs[i].bary)
			break;
	return i;
}

static inline uint32_t
ir3_find_output_regid(const struct ir3_shader_variant *so, unsigned slot)
{
	int j;
	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].slot == slot)
			return so->outputs[j].regid;
	return regid(63, 0);
}

#endif /* IR3_SHADER_H_ */
