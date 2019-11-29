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

#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"

#include "nir/tgsi_to_nir.h"

#include "freedreno_context.h"
#include "freedreno_util.h"

#include "ir3/ir3_shader.h"
#include "ir3/ir3_gallium.h"
#include "ir3/ir3_compiler.h"
#include "ir3/ir3_nir.h"

static void
dump_shader_info(struct ir3_shader_variant *v, bool binning_pass,
		struct pipe_debug_callback *debug)
{
	if (!unlikely(fd_mesa_debug & FD_DBG_SHADERDB))
		return;

	pipe_debug_message(debug, SHADER_INFO,
			"%s shader: %u inst, %u dwords, "
			"%u half, %u full, %u constlen, "
			"%u (ss), %u (sy), %d max_sun, %d loops\n",
			ir3_shader_stage(v),
			v->info.instrs_count,
			v->info.sizedwords,
			v->info.max_half_reg + 1,
			v->info.max_reg + 1,
			v->constlen,
			v->info.ss, v->info.sy,
			v->max_sun, v->loops);
}

struct ir3_shader_variant *
ir3_shader_variant(struct ir3_shader *shader, struct ir3_shader_key key,
		bool binning_pass, struct pipe_debug_callback *debug)
{
	struct ir3_shader_variant *v;
	bool created = false;

	/* some shader key values only apply to vertex or frag shader,
	 * so normalize the key to avoid constructing multiple identical
	 * variants:
	 */
	ir3_normalize_key(&key, shader->type);

	v = ir3_shader_get_variant(shader, &key, binning_pass, &created);

	if (created) {
		dump_shader_info(v, binning_pass, debug);
	}

	return v;
}

static void
copy_stream_out(struct ir3_stream_output_info *i,
		const struct pipe_stream_output_info *p)
{
	STATIC_ASSERT(ARRAY_SIZE(i->stride) == ARRAY_SIZE(p->stride));
	STATIC_ASSERT(ARRAY_SIZE(i->output) == ARRAY_SIZE(p->output));

	i->num_outputs = p->num_outputs;
	for (int n = 0; n < ARRAY_SIZE(i->stride); n++)
		i->stride[n] = p->stride[n];

	for (int n = 0; n < ARRAY_SIZE(i->output); n++) {
		i->output[n].register_index  = p->output[n].register_index;
		i->output[n].start_component = p->output[n].start_component;
		i->output[n].num_components  = p->output[n].num_components;
		i->output[n].output_buffer   = p->output[n].output_buffer;
		i->output[n].dst_offset      = p->output[n].dst_offset;
		i->output[n].stream          = p->output[n].stream;
	}
}

struct ir3_shader *
ir3_shader_create(struct ir3_compiler *compiler,
		const struct pipe_shader_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen)
{
	nir_shader *nir;
	if (cso->type == PIPE_SHADER_IR_NIR) {
		/* we take ownership of the reference: */
		nir = cso->ir.nir;
	} else {
		debug_assert(cso->type == PIPE_SHADER_IR_TGSI);
		if (ir3_shader_debug & IR3_DBG_DISASM) {
			tgsi_dump(cso->tokens, 0);
		}
		nir = tgsi_to_nir(cso->tokens, screen);
	}

	struct ir3_shader *shader = ir3_shader_from_nir(compiler, nir);

	copy_stream_out(&shader->stream_output, &cso->stream_output);

	if (fd_mesa_debug & FD_DBG_SHADERDB) {
		/* if shader-db run, create a standard variant immediately
		 * (as otherwise nothing will trigger the shader to be
		 * actually compiled)
		 */
		static struct ir3_shader_key key; /* static is implicitly zeroed */
		ir3_shader_variant(shader, key, false, debug);

		if (nir->info.stage != MESA_SHADER_FRAGMENT)
			ir3_shader_variant(shader, key, true, debug);
	}
	return shader;
}

/* a bit annoying that compute-shader and normal shader state objects
 * aren't a bit more aligned.
 */
struct ir3_shader *
ir3_shader_create_compute(struct ir3_compiler *compiler,
		const struct pipe_compute_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen)
{
	nir_shader *nir;
	if (cso->ir_type == PIPE_SHADER_IR_NIR) {
		/* we take ownership of the reference: */
		nir = (nir_shader *)cso->prog;
	} else {
		debug_assert(cso->ir_type == PIPE_SHADER_IR_TGSI);
		if (ir3_shader_debug & IR3_DBG_DISASM) {
			tgsi_dump(cso->prog, 0);
		}
		nir = tgsi_to_nir(cso->prog, screen);
	}

	struct ir3_shader *shader = ir3_shader_from_nir(compiler, nir);

	return shader;
}

/* This has to reach into the fd_context a bit more than the rest of
 * ir3, but it needs to be aligned with the compiler, so both agree
 * on which const regs hold what.  And the logic is identical between
 * a3xx/a4xx, the only difference is small details in the actual
 * CP_LOAD_STATE packets (which is handled inside the generation
 * specific ctx->emit_const(_bo)() fxns)
 */

#include "freedreno_resource.h"

static inline bool
is_stateobj(struct fd_ringbuffer *ring)
{
	/* XXX this is an ugly way to differentiate.. */
	return !!(ring->flags & FD_RINGBUFFER_STREAMING);
}

static inline void
ring_wfi(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
	/* when we emit const state via ring (IB2) we need a WFI, but when
	 * it is emit'd via stateobj, we don't
	 */
	if (is_stateobj(ring))
		return;

	fd_wfi(batch, ring);
}

static void
emit_const(struct fd_screen *screen, struct fd_ringbuffer *ring,
		const struct ir3_shader_variant *v, uint32_t dst_offset,
		uint32_t offset, uint32_t size,
		const void *user_buffer, struct pipe_resource *buffer)
{
	assert(dst_offset + size <= v->constlen * 4);

	screen->emit_const(ring, v->type, dst_offset,
			offset, size, user_buffer, buffer);
}

/**
 * Indirectly calculates size of cmdstream needed for ir3_emit_user_consts().
 * Returns number of packets, and total size of all the payload.
 *
 * The value can be a worst-case, ie. some shader variants may not read all
 * consts, etc.
 *
 * Returns size in dwords.
 */
void
ir3_user_consts_size(struct ir3_ubo_analysis_state *state,
		unsigned *packets, unsigned *size)
{
	*packets = *size = 0;

	for (uint32_t i = 0; i < ARRAY_SIZE(state->range); i++) {
		if (state->range[i].start < state->range[i].end) {
			*size += state->range[i].end - state->range[i].start;
			(*packets)++;
		}
	}
}

void
ir3_emit_user_consts(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_constbuf_stateobj *constbuf)
{
	struct ir3_ubo_analysis_state *state;
	state = &v->shader->ubo_state;

	for (uint32_t i = 0; i < ARRAY_SIZE(state->range); i++) {
		struct pipe_constant_buffer *cb = &constbuf->cb[i];

		if (state->range[i].start < state->range[i].end &&
			constbuf->enabled_mask & (1 << i)) {

			uint32_t size = state->range[i].end - state->range[i].start;
			uint32_t offset = cb->buffer_offset + state->range[i].start;

			/* and even if the start of the const buffer is before
			 * first_immediate, the end may not be:
			 */
			size = MIN2(size, (16 * v->constlen) - state->range[i].offset);

			if (size == 0)
				continue;

			/* things should be aligned to vec4: */
			debug_assert((state->range[i].offset % 16) == 0);
			debug_assert((size % 16) == 0);
			debug_assert((offset % 16) == 0);

			emit_const(screen, ring, v, state->range[i].offset / 4,
							offset, size / 4, cb->user_buffer, cb->buffer);
		}
	}
}

void
ir3_emit_ubos(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_constbuf_stateobj *constbuf)
{
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t offset = const_state->offsets.ubo;
	if (v->constlen > offset) {
		uint32_t params = const_state->num_ubos;
		uint32_t offsets[params];
		struct pipe_resource *prscs[params];

		for (uint32_t i = 0; i < params; i++) {
			const uint32_t index = i + 1;   /* UBOs start at index 1 */
			struct pipe_constant_buffer *cb = &constbuf->cb[index];
			assert(!cb->user_buffer);

			if ((constbuf->enabled_mask & (1 << index)) && cb->buffer) {
				offsets[i] = cb->buffer_offset;
				prscs[i] = cb->buffer;
			} else {
				offsets[i] = 0;
				prscs[i] = NULL;
			}
		}

		assert(offset * 4 + params < v->constlen * 4);

		screen->emit_const_bo(ring, v->type, false, offset * 4, params, prscs, offsets);
	}
}

void
ir3_emit_ssbo_sizes(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_shaderbuf_stateobj *sb)
{
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t offset = const_state->offsets.ssbo_sizes;
	if (v->constlen > offset) {
		uint32_t sizes[align(const_state->ssbo_size.count, 4)];
		unsigned mask = const_state->ssbo_size.mask;

		while (mask) {
			unsigned index = u_bit_scan(&mask);
			unsigned off = const_state->ssbo_size.off[index];
			sizes[off] = sb->sb[index].buffer_size;
		}

		emit_const(screen, ring, v, offset * 4,
			0, ARRAY_SIZE(sizes), sizes, NULL);
	}
}

void
ir3_emit_image_dims(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_shaderimg_stateobj *si)
{
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t offset = const_state->offsets.image_dims;
	if (v->constlen > offset) {
		uint32_t dims[align(const_state->image_dims.count, 4)];
		unsigned mask = const_state->image_dims.mask;

		while (mask) {
			struct pipe_image_view *img;
			struct fd_resource *rsc;
			unsigned index = u_bit_scan(&mask);
			unsigned off = const_state->image_dims.off[index];

			img = &si->si[index];
			rsc = fd_resource(img->resource);

			dims[off + 0] = util_format_get_blocksize(img->format);
			if (img->resource->target != PIPE_BUFFER) {
				unsigned lvl = img->u.tex.level;
				/* note for 2d/cube/etc images, even if re-interpreted
				 * as a different color format, the pixel size should
				 * be the same, so use original dimensions for y and z
				 * stride:
				 */
				dims[off + 1] = rsc->slices[lvl].pitch * rsc->cpp;
				/* see corresponding logic in fd_resource_offset(): */
				if (rsc->layer_first) {
					dims[off + 2] = rsc->layer_size;
				} else {
					dims[off + 2] = rsc->slices[lvl].size0;
				}
			} else {
				/* For buffer-backed images, the log2 of the format's
				 * bytes-per-pixel is placed on the 2nd slot. This is useful
				 * when emitting image_size instructions, for which we need
				 * to divide by bpp for image buffers. Since the bpp
				 * can only be power-of-two, the division is implemented
				 * as a SHR, and for that it is handy to have the log2 of
				 * bpp as a constant. (log2 = first-set-bit - 1)
				 */
				dims[off + 1] = ffs(dims[off + 0]) - 1;
			}
		}
		uint32_t size = MIN2(ARRAY_SIZE(dims), v->constlen * 4 - offset * 4);

		emit_const(screen, ring, v, offset * 4, 0, size, dims, NULL);
	}
}

void
ir3_emit_immediates(struct fd_screen *screen, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring)
{
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t base = const_state->offsets.immediate;
	int size = const_state->immediates_count;

	/* truncate size to avoid writing constants that shader
	 * does not use:
	 */
	size = MIN2(size + base, v->constlen) - base;

	/* convert out of vec4: */
	base *= 4;
	size *= 4;

	if (size > 0) {
		emit_const(screen, ring, v, base,
			0, size, const_state->immediates[0].val, NULL);
	}
}

static uint32_t
link_geometry_stages(const struct ir3_shader_variant *producer,
		const struct ir3_shader_variant *consumer,
		uint32_t *locs)
{
	uint32_t num_loc = 0;

	nir_foreach_variable(in_var, &consumer->shader->nir->inputs) {
		nir_foreach_variable(out_var, &producer->shader->nir->outputs) {
			if (in_var->data.location == out_var->data.location) {
				locs[in_var->data.driver_location] =
					producer->shader->output_loc[out_var->data.driver_location] * 4;

				debug_assert(num_loc <= in_var->data.driver_location + 1);
				num_loc = in_var->data.driver_location + 1;
			}
		}
	}

	return num_loc;
}

void
ir3_emit_link_map(struct fd_screen *screen,
		const struct ir3_shader_variant *producer,
		const struct ir3_shader_variant *v, struct fd_ringbuffer *ring)
{
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t base = const_state->offsets.primitive_map;
	uint32_t patch_locs[MAX_VARYING] = { }, num_loc;

	num_loc = link_geometry_stages(producer, v, patch_locs);

	int size = DIV_ROUND_UP(num_loc, 4);

	/* truncate size to avoid writing constants that shader
	 * does not use:
	 */
	size = MIN2(size + base, v->constlen) - base;

	/* convert out of vec4: */
	base *= 4;
	size *= 4;

	if (size > 0)
		emit_const(screen, ring, v, base, 0, size, patch_locs, NULL);
}

/* emit stream-out buffers: */
static void
emit_tfbos(struct fd_context *ctx, const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring)
{
	/* streamout addresses after driver-params: */
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t offset = const_state->offsets.tfbo;
	if (v->constlen > offset) {
		struct fd_streamout_stateobj *so = &ctx->streamout;
		struct ir3_stream_output_info *info = &v->shader->stream_output;
		uint32_t params = 4;
		uint32_t offsets[params];
		struct pipe_resource *prscs[params];

		for (uint32_t i = 0; i < params; i++) {
			struct pipe_stream_output_target *target = so->targets[i];

			if (target) {
				offsets[i] = (so->offsets[i] * info->stride[i] * 4) +
						target->buffer_offset;
				prscs[i] = target->buffer;
			} else {
				offsets[i] = 0;
				prscs[i] = NULL;
			}
		}

		assert(offset * 4 + params < v->constlen * 4);

		ctx->screen->emit_const_bo(ring, v->type, true, offset * 4, params, prscs, offsets);
	}
}

static uint32_t
max_tf_vtx(struct fd_context *ctx, const struct ir3_shader_variant *v)
{
	struct fd_streamout_stateobj *so = &ctx->streamout;
	struct ir3_stream_output_info *info = &v->shader->stream_output;
	uint32_t maxvtxcnt = 0x7fffffff;

	if (ctx->screen->gpu_id >= 500)
		return 0;
	if (v->binning_pass)
		return 0;
	if (v->shader->stream_output.num_outputs == 0)
		return 0;
	if (so->num_targets == 0)
		return 0;

	/* offset to write to is:
	 *
	 *   total_vtxcnt = vtxcnt + offsets[i]
	 *   offset = total_vtxcnt * stride[i]
	 *
	 *   offset =   vtxcnt * stride[i]       ; calculated in shader
	 *            + offsets[i] * stride[i]   ; calculated at emit_tfbos()
	 *
	 * assuming for each vtx, each target buffer will have data written
	 * up to 'offset + stride[i]', that leaves maxvtxcnt as:
	 *
	 *   buffer_size = (maxvtxcnt * stride[i]) + stride[i]
	 *   maxvtxcnt   = (buffer_size - stride[i]) / stride[i]
	 *
	 * but shader is actually doing a less-than (rather than less-than-
	 * equal) check, so we can drop the -stride[i].
	 *
	 * TODO is assumption about `offset + stride[i]` legit?
	 */
	for (unsigned i = 0; i < so->num_targets; i++) {
		struct pipe_stream_output_target *target = so->targets[i];
		unsigned stride = info->stride[i] * 4;   /* convert dwords->bytes */
		if (target) {
			uint32_t max = target->buffer_size / stride;
			maxvtxcnt = MIN2(maxvtxcnt, max);
		}
	}

	return maxvtxcnt;
}

static void
emit_common_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx, enum pipe_shader_type t)
{
	enum fd_dirty_shader_state dirty = ctx->dirty_shader[t];

	/* When we use CP_SET_DRAW_STATE objects to emit constant state,
	 * if we emit any of it we need to emit all.  This is because
	 * we are using the same state-group-id each time for uniform
	 * state, and if previous update is never evaluated (due to no
	 * visible primitives in the current tile) then the new stateobj
	 * completely replaces the old one.
	 *
	 * Possibly if we split up different parts of the const state to
	 * different state-objects we could avoid this.
	 */
	if (dirty && is_stateobj(ring))
		dirty = ~0;

	if (dirty & (FD_DIRTY_SHADER_PROG | FD_DIRTY_SHADER_CONST)) {
		struct fd_constbuf_stateobj *constbuf;
		bool shader_dirty;

		constbuf = &ctx->constbuf[t];
		shader_dirty = !!(dirty & FD_DIRTY_SHADER_PROG);

		ring_wfi(ctx->batch, ring);

		ir3_emit_user_consts(ctx->screen, v, ring, constbuf);
		ir3_emit_ubos(ctx->screen, v, ring, constbuf);
		if (shader_dirty)
			ir3_emit_immediates(ctx->screen, v, ring);
	}

	if (dirty & (FD_DIRTY_SHADER_PROG | FD_DIRTY_SHADER_SSBO)) {
		struct fd_shaderbuf_stateobj *sb = &ctx->shaderbuf[t];
		ring_wfi(ctx->batch, ring);
		ir3_emit_ssbo_sizes(ctx->screen, v, ring, sb);
	}

	if (dirty & (FD_DIRTY_SHADER_PROG | FD_DIRTY_SHADER_IMAGE)) {
		struct fd_shaderimg_stateobj *si = &ctx->shaderimg[t];
		ring_wfi(ctx->batch, ring);
		ir3_emit_image_dims(ctx->screen, v, ring, si);
	}
}

void
ir3_emit_vs_driver_params(const struct ir3_shader_variant *v,
		struct fd_ringbuffer *ring, struct fd_context *ctx,
		const struct pipe_draw_info *info)
{
	debug_assert(ir3_needs_vs_driver_params(v));

	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t offset = const_state->offsets.driver_param;
	uint32_t vertex_params[IR3_DP_VS_COUNT] = {
			[IR3_DP_VTXID_BASE] = info->index_size ?
					info->index_bias : info->start,
					[IR3_DP_VTXCNT_MAX] = max_tf_vtx(ctx, v),
	};
	/* if no user-clip-planes, we don't need to emit the
	 * entire thing:
	 */
	uint32_t vertex_params_size = 4;

	if (v->key.ucp_enables) {
		struct pipe_clip_state *ucp = &ctx->ucp;
		unsigned pos = IR3_DP_UCP0_X;
		for (unsigned i = 0; pos <= IR3_DP_UCP7_W; i++) {
			for (unsigned j = 0; j < 4; j++) {
				vertex_params[pos] = fui(ucp->ucp[i][j]);
				pos++;
			}
		}
		vertex_params_size = ARRAY_SIZE(vertex_params);
	}

	vertex_params_size = MAX2(vertex_params_size, const_state->num_driver_params);

	bool needs_vtxid_base =
		ir3_find_sysval_regid(v, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) != regid(63, 0);

	/* for indirect draw, we need to copy VTXID_BASE from
	 * indirect-draw parameters buffer.. which is annoying
	 * and means we can't easily emit these consts in cmd
	 * stream so need to copy them to bo.
	 */
	if (info->indirect && needs_vtxid_base) {
		struct pipe_draw_indirect_info *indirect = info->indirect;
		struct pipe_resource *vertex_params_rsc =
				pipe_buffer_create(&ctx->screen->base,
						PIPE_BIND_CONSTANT_BUFFER, PIPE_USAGE_STREAM,
						vertex_params_size * 4);
		unsigned src_off = info->indirect->offset;;
		void *ptr;

		ptr = fd_bo_map(fd_resource(vertex_params_rsc)->bo);
		memcpy(ptr, vertex_params, vertex_params_size * 4);

		if (info->index_size) {
			/* indexed draw, index_bias is 4th field: */
			src_off += 3 * 4;
		} else {
			/* non-indexed draw, start is 3rd field: */
			src_off += 2 * 4;
		}

		/* copy index_bias or start from draw params: */
		ctx->screen->mem_to_mem(ring, vertex_params_rsc, 0,
				indirect->buffer, src_off, 1);

		emit_const(ctx->screen, ring, v, offset * 4, 0,
				vertex_params_size, NULL, vertex_params_rsc);

		pipe_resource_reference(&vertex_params_rsc, NULL);
	} else {
		emit_const(ctx->screen, ring, v, offset * 4, 0,
				vertex_params_size, vertex_params, NULL);
	}

	/* if needed, emit stream-out buffer addresses: */
	if (vertex_params[IR3_DP_VTXCNT_MAX] > 0) {
		emit_tfbos(ctx, v, ring);
	}
}

void
ir3_emit_vs_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx, const struct pipe_draw_info *info)
{
	debug_assert(v->type == MESA_SHADER_VERTEX);

	emit_common_consts(v, ring, ctx, PIPE_SHADER_VERTEX);

	/* emit driver params every time: */
	if (info && ir3_needs_vs_driver_params(v)) {
		ring_wfi(ctx->batch, ring);
		ir3_emit_vs_driver_params(v, ring, ctx, info);
	}
}

void
ir3_emit_fs_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx)
{
	debug_assert(v->type == MESA_SHADER_FRAGMENT);

	emit_common_consts(v, ring, ctx, PIPE_SHADER_FRAGMENT);
}

/* emit compute-shader consts: */
void
ir3_emit_cs_consts(const struct ir3_shader_variant *v, struct fd_ringbuffer *ring,
		struct fd_context *ctx, const struct pipe_grid_info *info)
{
	debug_assert(gl_shader_stage_is_compute(v->type));

	emit_common_consts(v, ring, ctx, PIPE_SHADER_COMPUTE);

	/* emit compute-shader driver-params: */
	const struct ir3_const_state *const_state = &v->shader->const_state;
	uint32_t offset = const_state->offsets.driver_param;
	if (v->constlen > offset) {
		ring_wfi(ctx->batch, ring);

		if (info->indirect) {
			struct pipe_resource *indirect = NULL;
			unsigned indirect_offset;

			/* This is a bit awkward, but CP_LOAD_STATE.EXT_SRC_ADDR needs
			 * to be aligned more strongly than 4 bytes.  So in this case
			 * we need a temporary buffer to copy NumWorkGroups.xyz to.
			 *
			 * TODO if previous compute job is writing to info->indirect,
			 * we might need a WFI.. but since we currently flush for each
			 * compute job, we are probably ok for now.
			 */
			if (info->indirect_offset & 0xf) {
				indirect = pipe_buffer_create(&ctx->screen->base,
					PIPE_BIND_COMMAND_ARGS_BUFFER, PIPE_USAGE_STREAM,
					0x1000);
				indirect_offset = 0;

				ctx->screen->mem_to_mem(ring, indirect, 0, info->indirect,
						info->indirect_offset, 3);
			} else {
				pipe_resource_reference(&indirect, info->indirect);
				indirect_offset = info->indirect_offset;
			}

			emit_const(ctx->screen, ring, v, offset * 4,
					indirect_offset, 4, NULL, indirect);

			pipe_resource_reference(&indirect, NULL);
		} else {
			uint32_t compute_params[IR3_DP_CS_COUNT] = {
				[IR3_DP_NUM_WORK_GROUPS_X] = info->grid[0],
				[IR3_DP_NUM_WORK_GROUPS_Y] = info->grid[1],
				[IR3_DP_NUM_WORK_GROUPS_Z] = info->grid[2],
				[IR3_DP_LOCAL_GROUP_SIZE_X] = info->block[0],
				[IR3_DP_LOCAL_GROUP_SIZE_Y] = info->block[1],
				[IR3_DP_LOCAL_GROUP_SIZE_Z] = info->block[2],
			};
			uint32_t size = MIN2(const_state->num_driver_params,
					v->constlen * 4 - offset * 4);

			emit_const(ctx->screen, ring, v, offset * 4, 0, size,
					compute_params, NULL);
		}
	}
}

static void *
ir3_shader_state_create(struct pipe_context *pctx, const struct pipe_shader_state *cso)
{
	struct fd_context *ctx = fd_context(pctx);
	struct ir3_compiler *compiler = ctx->screen->compiler;
	return ir3_shader_create(compiler, cso, &ctx->debug, pctx->screen);
}

static void
ir3_shader_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct ir3_shader *so = hwcso;
	ir3_shader_destroy(so);
}

void
ir3_prog_init(struct pipe_context *pctx)
{
	pctx->create_vs_state = ir3_shader_state_create;
	pctx->delete_vs_state = ir3_shader_state_delete;

	pctx->create_tcs_state = ir3_shader_state_create;
	pctx->delete_tcs_state = ir3_shader_state_delete;

	pctx->create_tes_state = ir3_shader_state_create;
	pctx->delete_tes_state = ir3_shader_state_delete;

	pctx->create_gs_state = ir3_shader_state_create;
	pctx->delete_gs_state = ir3_shader_state_delete;

	pctx->create_fs_state = ir3_shader_state_create;
	pctx->delete_fs_state = ir3_shader_state_delete;
}
