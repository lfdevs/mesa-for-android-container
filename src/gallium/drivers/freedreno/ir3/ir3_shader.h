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

#include "ir3.h"
#include "disasm.h"

/* driver param indices: */
enum ir3_driver_param {
	IR3_DP_VTXID_BASE = 0,
	IR3_DP_VTXCNT_MAX = 1,
};

/* internal semantic used for passing vtxcnt to vertex shader to
 * implement transform feedback:
 */
#define IR3_SEMANTIC_VTXCNT (TGSI_SEMANTIC_COUNT + 0)

typedef uint16_t ir3_semantic;  /* semantic name + index */
static inline ir3_semantic
ir3_semantic_name(uint8_t name, uint16_t index)
{
	return (name << 8) | (index & 0xff);
}

static inline uint8_t sem2name(ir3_semantic sem)
{
	return sem >> 8;
}

static inline uint16_t sem2idx(ir3_semantic sem)
{
	return sem & 0xff;
}

/* Configuration key used to identify a shader variant.. different
 * shader variants can be used to implement features not supported
 * in hw (two sided color), binning-pass vertex shader, etc.
 */
struct ir3_shader_key {
	union {
		struct {
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
			/* used when shader needs to handle flat varyings (a4xx),
			 * for TGSI_INTERPOLATE_COLOR:
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

	/* varyings/outputs: */
	unsigned outputs_count;
	struct {
		ir3_semantic semantic;
		uint8_t regid;
	} outputs[16 + 2];  /* +POSITION +PSIZE */
	bool writes_pos, writes_psize;

	/* vertices/inputs: */
	unsigned inputs_count;
	struct {
		ir3_semantic semantic;
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
		uint8_t bary;
		uint8_t interpolate;
	} inputs[16 + 2];  /* +POSITION +FACE */

	unsigned total_in;       /* sum of inputs (scalar) */

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

struct ir3_shader {
	enum shader_t type;

	/* shader id (for debug): */
	uint32_t id;
	uint32_t variant_count;

	struct ir3_compiler *compiler;

	struct pipe_context *pctx;
	const struct tgsi_token *tokens;
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
ir3_find_output(const struct ir3_shader_variant *so, ir3_semantic semantic)
{
	int j;

	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].semantic == semantic)
			return j;

	/* it seems optional to have a OUT.BCOLOR[n] for each OUT.COLOR[n]
	 * in the vertex shader.. but the fragment shader doesn't know this
	 * so  it will always have both IN.COLOR[n] and IN.BCOLOR[n].  So
	 * at link time if there is no matching OUT.BCOLOR[n], we must map
	 * OUT.COLOR[n] to IN.BCOLOR[n].  And visa versa if there is only
	 * a OUT.BCOLOR[n] but no matching OUT.COLOR[n]
	 */
	if (sem2name(semantic) == TGSI_SEMANTIC_BCOLOR) {
		unsigned idx = sem2idx(semantic);
		semantic = ir3_semantic_name(TGSI_SEMANTIC_COLOR, idx);
	} else if (sem2name(semantic) == TGSI_SEMANTIC_COLOR) {
		unsigned idx = sem2idx(semantic);
		semantic = ir3_semantic_name(TGSI_SEMANTIC_BCOLOR, idx);
	} else {
		return 0;
	}

	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].semantic == semantic)
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
ir3_find_output_regid(const struct ir3_shader_variant *so, ir3_semantic semantic)
{
	int j;
	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].semantic == semantic)
			return so->outputs[j].regid;
	return regid(63, 0);
}

#endif /* IR3_SHADER_H_ */
