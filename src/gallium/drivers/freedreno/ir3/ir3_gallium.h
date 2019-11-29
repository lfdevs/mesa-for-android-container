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

#ifndef IR3_GALLIUM_H_
#define IR3_GALLIUM_H_

#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "ir3/ir3_shader.h"

struct ir3_shader * ir3_shader_create(struct ir3_compiler *compiler,
		const struct pipe_shader_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen);
struct ir3_shader *
ir3_shader_create_compute(struct ir3_compiler *compiler,
		const struct pipe_compute_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen);
struct ir3_shader_variant * ir3_shader_variant(struct ir3_shader *shader,
		struct ir3_shader_key key, bool binning_pass,
		struct pipe_debug_callback *debug);

struct fd_ringbuffer;
struct fd_context;
struct fd_screen;
struct fd_constbuf_stateobj;
struct fd_shaderbuf_stateobj;
struct fd_shaderimg_stateobj;

void ir3_user_consts_size(struct ir3_ubo_analysis_state *state,
		unsigned *packets, unsigned *size);
void ir3_emit_user_consts(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_constbuf_stateobj *constbuf);
void ir3_emit_ubos(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_constbuf_stateobj *constbuf);
void ir3_emit_ssbo_sizes(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_shaderbuf_stateobj *sb);
void ir3_emit_image_dims(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_shaderimg_stateobj *si);
void ir3_emit_immediates(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring);
void ir3_emit_link_map(struct fd_screen *screen,
		const struct ir3_shader_variant *producer,
		const struct ir3_shader_variant *v, struct fd_ringbuffer *ring);

static inline bool
ir3_needs_vs_driver_params(const struct ir3_shader_variant *v)
{
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t offset = const_state->offsets.driver_param;

	return v->constlen > offset;
}

void ir3_emit_vs_driver_params(const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_context *ctx,
		const struct pipe_draw_info *info);
void ir3_emit_vs_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx, const struct pipe_draw_info *info);
void ir3_emit_fs_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx);
void ir3_emit_cs_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx, const struct pipe_grid_info *info);

void ir3_prog_init(struct pipe_context *pctx);

#endif /* IR3_GALLIUM_H_ */
