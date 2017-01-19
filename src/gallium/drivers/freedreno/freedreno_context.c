/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#include "freedreno_context.h"
#include "freedreno_draw.h"
#include "freedreno_fence.h"
#include "freedreno_program.h"
#include "freedreno_resource.h"
#include "freedreno_texture.h"
#include "freedreno_state.h"
#include "freedreno_gmem.h"
#include "freedreno_query.h"
#include "freedreno_query_hw.h"
#include "freedreno_util.h"

static void
fd_context_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence,
		unsigned flags)
{
	struct fd_context *ctx = fd_context(pctx);

	if (flags & PIPE_FLUSH_FENCE_FD)
		ctx->batch->needs_out_fence_fd = true;

	if (!ctx->screen->reorder) {
		fd_batch_flush(ctx->batch, true);
	} else {
		fd_bc_flush(&ctx->screen->batch_cache, ctx);
	}

	if (fence) {
		/* if there hasn't been any rendering submitted yet, we might not
		 * have actually created a fence
		 */
		if (!ctx->last_fence || ctx->batch->needs_out_fence_fd) {
			ctx->batch->needs_flush = true;
			fd_gmem_render_noop(ctx->batch);
			fd_batch_reset(ctx->batch);
		}
		fd_fence_ref(pctx->screen, fence, ctx->last_fence);
	}
}

/**
 * emit marker string as payload of a no-op packet, which can be
 * decoded by cffdump.
 */
static void
fd_emit_string_marker(struct pipe_context *pctx, const char *string, int len)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd_ringbuffer *ring;
	const uint32_t *buf = (const void *)string;

	if (!ctx->batch)
		return;

	ring = ctx->batch->draw;

	/* max packet size is 0x3fff dwords: */
	len = MIN2(len, 0x3fff * 4);

	if (ctx->screen->gpu_id >= 500)
		OUT_PKT7(ring, CP_NOP, align(len, 4) / 4);
	else
		OUT_PKT3(ring, CP_NOP, align(len, 4) / 4);
	while (len >= 4) {
		OUT_RING(ring, *buf);
		buf++;
		len -= 4;
	}

	/* copy remainder bytes without reading past end of input string: */
	if (len > 0) {
		uint32_t w = 0;
		memcpy(&w, buf, len);
		OUT_RING(ring, w);
	}
}

void
fd_context_destroy(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	unsigned i;

	DBG("");

	if (ctx->screen->reorder)
		util_queue_destroy(&ctx->flush_queue);

	fd_batch_reference(&ctx->batch, NULL);  /* unref current batch */
	fd_bc_invalidate_context(ctx);

	fd_fence_ref(pctx->screen, &ctx->last_fence, NULL);

	fd_prog_fini(pctx);
	fd_hw_query_fini(pctx);

	if (ctx->blitter)
		util_blitter_destroy(ctx->blitter);

	if (ctx->clear_rs_state)
		pctx->delete_rasterizer_state(pctx, ctx->clear_rs_state);

	if (ctx->primconvert)
		util_primconvert_destroy(ctx->primconvert);

	slab_destroy_child(&ctx->transfer_pool);

	for (i = 0; i < ARRAY_SIZE(ctx->pipe); i++) {
		struct fd_vsc_pipe *pipe = &ctx->pipe[i];
		if (!pipe->bo)
			break;
		fd_bo_del(pipe->bo);
	}

	fd_device_del(ctx->dev);

	if (fd_mesa_debug & (FD_DBG_BSTAT | FD_DBG_MSGS)) {
		printf("batch_total=%u, batch_sysmem=%u, batch_gmem=%u, batch_restore=%u\n",
			(uint32_t)ctx->stats.batch_total, (uint32_t)ctx->stats.batch_sysmem,
			(uint32_t)ctx->stats.batch_gmem, (uint32_t)ctx->stats.batch_restore);
	}

	FREE(ctx);
}

static void
fd_set_debug_callback(struct pipe_context *pctx,
		const struct pipe_debug_callback *cb)
{
	struct fd_context *ctx = fd_context(pctx);

	if (cb)
		ctx->debug = *cb;
	else
		memset(&ctx->debug, 0, sizeof(ctx->debug));
}

/* TODO we could combine a few of these small buffers (solid_vbuf,
 * blit_texcoord_vbuf, and vsc_size_mem, into a single buffer and
 * save a tiny bit of memory
 */

static struct pipe_resource *
create_solid_vertexbuf(struct pipe_context *pctx)
{
	static const float init_shader_const[] = {
			-1.000000, +1.000000, +1.000000,
			+1.000000, -1.000000, +1.000000,
	};
	struct pipe_resource *prsc = pipe_buffer_create(pctx->screen,
			PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE, sizeof(init_shader_const));
	pipe_buffer_write(pctx, prsc, 0,
			sizeof(init_shader_const), init_shader_const);
	return prsc;
}

static struct pipe_resource *
create_blit_texcoord_vertexbuf(struct pipe_context *pctx)
{
	struct pipe_resource *prsc = pipe_buffer_create(pctx->screen,
			PIPE_BIND_CUSTOM, PIPE_USAGE_DYNAMIC, 16);
	return prsc;
}

void
fd_context_setup_common_vbos(struct fd_context *ctx)
{
	struct pipe_context *pctx = &ctx->base;

	ctx->solid_vbuf = create_solid_vertexbuf(pctx);
	ctx->blit_texcoord_vbuf = create_blit_texcoord_vertexbuf(pctx);

	/* setup solid_vbuf_state: */
	ctx->solid_vbuf_state.vtx = pctx->create_vertex_elements_state(
			pctx, 1, (struct pipe_vertex_element[]){{
				.vertex_buffer_index = 0,
				.src_offset = 0,
				.src_format = PIPE_FORMAT_R32G32B32_FLOAT,
			}});
	ctx->solid_vbuf_state.vertexbuf.count = 1;
	ctx->solid_vbuf_state.vertexbuf.vb[0].stride = 12;
	ctx->solid_vbuf_state.vertexbuf.vb[0].buffer = ctx->solid_vbuf;

	/* setup blit_vbuf_state: */
	ctx->blit_vbuf_state.vtx = pctx->create_vertex_elements_state(
			pctx, 2, (struct pipe_vertex_element[]){{
				.vertex_buffer_index = 0,
				.src_offset = 0,
				.src_format = PIPE_FORMAT_R32G32_FLOAT,
			}, {
				.vertex_buffer_index = 1,
				.src_offset = 0,
				.src_format = PIPE_FORMAT_R32G32B32_FLOAT,
			}});
	ctx->blit_vbuf_state.vertexbuf.count = 2;
	ctx->blit_vbuf_state.vertexbuf.vb[0].stride = 8;
	ctx->blit_vbuf_state.vertexbuf.vb[0].buffer = ctx->blit_texcoord_vbuf;
	ctx->blit_vbuf_state.vertexbuf.vb[1].stride = 12;
	ctx->blit_vbuf_state.vertexbuf.vb[1].buffer = ctx->solid_vbuf;
}

void
fd_context_cleanup_common_vbos(struct fd_context *ctx)
{
	struct pipe_context *pctx = &ctx->base;

	pctx->delete_vertex_elements_state(pctx, ctx->solid_vbuf_state.vtx);
	pctx->delete_vertex_elements_state(pctx, ctx->blit_vbuf_state.vtx);

	pipe_resource_reference(&ctx->solid_vbuf, NULL);
	pipe_resource_reference(&ctx->blit_texcoord_vbuf, NULL);
}

struct pipe_context *
fd_context_init(struct fd_context *ctx, struct pipe_screen *pscreen,
		const uint8_t *primtypes, void *priv)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct pipe_context *pctx;
	int i;

	ctx->screen = screen;

	ctx->primtypes = primtypes;
	ctx->primtype_mask = 0;
	for (i = 0; i < PIPE_PRIM_MAX; i++)
		if (primtypes[i])
			ctx->primtype_mask |= (1 << i);

	/* need some sane default in case state tracker doesn't
	 * set some state:
	 */
	ctx->sample_mask = 0xffff;

	pctx = &ctx->base;
	pctx->screen = pscreen;
	pctx->priv = priv;
	pctx->flush = fd_context_flush;
	pctx->emit_string_marker = fd_emit_string_marker;
	pctx->set_debug_callback = fd_set_debug_callback;
	pctx->create_fence_fd = fd_create_fence_fd;
	pctx->fence_server_sync = fd_fence_server_sync;

	/* TODO what about compute?  Ideally it creates it's own independent
	 * batches per compute job (since it isn't using tiling, so no point
	 * in getting involved with the re-ordering madness)..
	 */
	if (!screen->reorder) {
		ctx->batch = fd_bc_alloc_batch(&screen->batch_cache, ctx);
	}

	slab_create_child(&ctx->transfer_pool, &screen->transfer_pool);

	fd_draw_init(pctx);
	fd_resource_context_init(pctx);
	fd_query_context_init(pctx);
	fd_texture_init(pctx);
	fd_state_init(pctx);
	fd_hw_query_init(pctx);

	ctx->blitter = util_blitter_create(pctx);
	if (!ctx->blitter)
		goto fail;

	ctx->primconvert = util_primconvert_create(pctx, ctx->primtype_mask);
	if (!ctx->primconvert)
		goto fail;

	return pctx;

fail:
	pctx->destroy(pctx);
	return NULL;
}
