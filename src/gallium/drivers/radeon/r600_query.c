/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2014 Marek Olšák <marek.olsak@amd.com>
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

#include "r600_query.h"
#include "r600_cs.h"
#include "util/u_memory.h"

/* Queries without buffer handling or suspend/resume. */
struct r600_query_sw {
	struct r600_query b;

	uint64_t begin_result;
	uint64_t end_result;
	/* Fence for GPU_FINISHED. */
	struct pipe_fence_handle *fence;
};

static void r600_query_sw_destroy(struct r600_common_context *rctx,
				  struct r600_query *rquery)
{
	struct pipe_screen *screen = rctx->b.screen;
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;

	screen->fence_reference(screen, &query->fence, NULL);
	FREE(query);
}

static enum radeon_value_id winsys_id_from_type(unsigned type)
{
	switch (type) {
	case R600_QUERY_REQUESTED_VRAM: return RADEON_REQUESTED_VRAM_MEMORY;
	case R600_QUERY_REQUESTED_GTT: return RADEON_REQUESTED_GTT_MEMORY;
	case R600_QUERY_BUFFER_WAIT_TIME: return RADEON_BUFFER_WAIT_TIME_NS;
	case R600_QUERY_NUM_CS_FLUSHES: return RADEON_NUM_CS_FLUSHES;
	case R600_QUERY_NUM_BYTES_MOVED: return RADEON_NUM_BYTES_MOVED;
	case R600_QUERY_VRAM_USAGE: return RADEON_VRAM_USAGE;
	case R600_QUERY_GTT_USAGE: return RADEON_GTT_USAGE;
	case R600_QUERY_GPU_TEMPERATURE: return RADEON_GPU_TEMPERATURE;
	case R600_QUERY_CURRENT_GPU_SCLK: return RADEON_CURRENT_SCLK;
	case R600_QUERY_CURRENT_GPU_MCLK: return RADEON_CURRENT_MCLK;
	default: unreachable("query type does not correspond to winsys id");
	}
}

static boolean r600_query_sw_begin(struct r600_common_context *rctx,
				   struct r600_query *rquery)
{
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;

	switch(query->b.type) {
	case PIPE_QUERY_TIMESTAMP_DISJOINT:
	case PIPE_QUERY_GPU_FINISHED:
		break;
	case R600_QUERY_DRAW_CALLS:
		query->begin_result = rctx->num_draw_calls;
		break;
	case R600_QUERY_REQUESTED_VRAM:
	case R600_QUERY_REQUESTED_GTT:
	case R600_QUERY_VRAM_USAGE:
	case R600_QUERY_GTT_USAGE:
	case R600_QUERY_GPU_TEMPERATURE:
	case R600_QUERY_CURRENT_GPU_SCLK:
	case R600_QUERY_CURRENT_GPU_MCLK:
		query->begin_result = 0;
		break;
	case R600_QUERY_BUFFER_WAIT_TIME:
	case R600_QUERY_NUM_CS_FLUSHES:
	case R600_QUERY_NUM_BYTES_MOVED: {
		enum radeon_value_id ws_id = winsys_id_from_type(query->b.type);
		query->begin_result = rctx->ws->query_value(rctx->ws, ws_id);
		break;
	}
	case R600_QUERY_GPU_LOAD:
		query->begin_result = r600_gpu_load_begin(rctx->screen);
		break;
	case R600_QUERY_NUM_COMPILATIONS:
		query->begin_result = p_atomic_read(&rctx->screen->num_compilations);
		break;
	case R600_QUERY_NUM_SHADERS_CREATED:
		query->begin_result = p_atomic_read(&rctx->screen->num_shaders_created);
		break;
	case R600_QUERY_GPIN_ASIC_ID:
	case R600_QUERY_GPIN_NUM_SIMD:
	case R600_QUERY_GPIN_NUM_RB:
	case R600_QUERY_GPIN_NUM_SPI:
	case R600_QUERY_GPIN_NUM_SE:
		break;
	default:
		unreachable("r600_query_sw_begin: bad query type");
	}

	return TRUE;
}

static void r600_query_sw_end(struct r600_common_context *rctx,
			      struct r600_query *rquery)
{
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;

	switch(query->b.type) {
	case PIPE_QUERY_TIMESTAMP_DISJOINT:
		break;
	case PIPE_QUERY_GPU_FINISHED:
		rctx->b.flush(&rctx->b, &query->fence, 0);
		break;
	case R600_QUERY_DRAW_CALLS:
		query->end_result = rctx->num_draw_calls;
		break;
	case R600_QUERY_REQUESTED_VRAM:
	case R600_QUERY_REQUESTED_GTT:
	case R600_QUERY_VRAM_USAGE:
	case R600_QUERY_GTT_USAGE:
	case R600_QUERY_GPU_TEMPERATURE:
	case R600_QUERY_CURRENT_GPU_SCLK:
	case R600_QUERY_CURRENT_GPU_MCLK:
	case R600_QUERY_BUFFER_WAIT_TIME:
	case R600_QUERY_NUM_CS_FLUSHES:
	case R600_QUERY_NUM_BYTES_MOVED: {
		enum radeon_value_id ws_id = winsys_id_from_type(query->b.type);
		query->end_result = rctx->ws->query_value(rctx->ws, ws_id);
		break;
	}
	case R600_QUERY_GPU_LOAD:
		query->end_result = r600_gpu_load_end(rctx->screen,
						      query->begin_result);
		query->begin_result = 0;
		break;
	case R600_QUERY_NUM_COMPILATIONS:
		query->end_result = p_atomic_read(&rctx->screen->num_compilations);
		break;
	case R600_QUERY_NUM_SHADERS_CREATED:
		query->end_result = p_atomic_read(&rctx->screen->num_shaders_created);
		break;
	case R600_QUERY_GPIN_ASIC_ID:
	case R600_QUERY_GPIN_NUM_SIMD:
	case R600_QUERY_GPIN_NUM_RB:
	case R600_QUERY_GPIN_NUM_SPI:
	case R600_QUERY_GPIN_NUM_SE:
		break;
	default:
		unreachable("r600_query_sw_end: bad query type");
	}
}

static boolean r600_query_sw_get_result(struct r600_common_context *rctx,
					struct r600_query *rquery,
					boolean wait,
					union pipe_query_result *result)
{
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;

	switch (query->b.type) {
	case PIPE_QUERY_TIMESTAMP_DISJOINT:
		/* Convert from cycles per millisecond to cycles per second (Hz). */
		result->timestamp_disjoint.frequency =
			(uint64_t)rctx->screen->info.clock_crystal_freq * 1000;
		result->timestamp_disjoint.disjoint = FALSE;
		return TRUE;
	case PIPE_QUERY_GPU_FINISHED: {
		struct pipe_screen *screen = rctx->b.screen;
		result->b = screen->fence_finish(screen, query->fence,
						 wait ? PIPE_TIMEOUT_INFINITE : 0);
		return result->b;
	}

	case R600_QUERY_GPIN_ASIC_ID:
		result->u32 = 0;
		return TRUE;
	case R600_QUERY_GPIN_NUM_SIMD:
		result->u32 = rctx->screen->info.num_good_compute_units;
		return TRUE;
	case R600_QUERY_GPIN_NUM_RB:
		result->u32 = rctx->screen->info.num_render_backends;
		return TRUE;
	case R600_QUERY_GPIN_NUM_SPI:
		result->u32 = 1; /* all supported chips have one SPI per SE */
		return TRUE;
	case R600_QUERY_GPIN_NUM_SE:
		result->u32 = rctx->screen->info.max_se;
		return TRUE;
	}

	result->u64 = query->end_result - query->begin_result;

	switch (query->b.type) {
	case R600_QUERY_BUFFER_WAIT_TIME:
	case R600_QUERY_GPU_TEMPERATURE:
		result->u64 /= 1000;
		break;
	case R600_QUERY_CURRENT_GPU_SCLK:
	case R600_QUERY_CURRENT_GPU_MCLK:
		result->u64 *= 1000000;
		break;
	}

	return TRUE;
}

static struct r600_query_ops sw_query_ops = {
	.destroy = r600_query_sw_destroy,
	.begin = r600_query_sw_begin,
	.end = r600_query_sw_end,
	.get_result = r600_query_sw_get_result
};

static struct pipe_query *r600_query_sw_create(struct pipe_context *ctx,
					       unsigned query_type)
{
	struct r600_query_sw *query;

	query = CALLOC_STRUCT(r600_query_sw);
	if (!query)
		return NULL;

	query->b.type = query_type;
	query->b.ops = &sw_query_ops;

	return (struct pipe_query *)query;
}

void r600_query_hw_destroy(struct r600_common_context *rctx,
			   struct r600_query *rquery)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;
	struct r600_query_buffer *prev = query->buffer.previous;

	/* Release all query buffers. */
	while (prev) {
		struct r600_query_buffer *qbuf = prev;
		prev = prev->previous;
		pipe_resource_reference((struct pipe_resource**)&qbuf->buf, NULL);
		FREE(qbuf);
	}

	pipe_resource_reference((struct pipe_resource**)&query->buffer.buf, NULL);
	FREE(rquery);
}

static struct r600_resource *r600_new_query_buffer(struct r600_common_context *ctx,
						   struct r600_query_hw *query)
{
	unsigned buf_size = MAX2(query->result_size, 4096);

	/* Queries are normally read by the CPU after
	 * being written by the gpu, hence staging is probably a good
	 * usage pattern.
	 */
	struct r600_resource *buf = (struct r600_resource*)
		pipe_buffer_create(ctx->b.screen, PIPE_BIND_CUSTOM,
				   PIPE_USAGE_STAGING, buf_size);

	if (query->flags & R600_QUERY_HW_FLAG_PREDICATE)
		query->ops->prepare_buffer(ctx, query, buf);

	return buf;
}

static void r600_query_hw_prepare_buffer(struct r600_common_context *ctx,
					 struct r600_query_hw *query,
					 struct r600_resource *buffer)
{
	/* Callers ensure that the buffer is currently unused by the GPU. */
	uint32_t *results = ctx->ws->buffer_map(buffer->buf, NULL,
						PIPE_TRANSFER_WRITE |
						PIPE_TRANSFER_UNSYNCHRONIZED);

	memset(results, 0, buffer->b.b.width0);

	if (query->b.type == PIPE_QUERY_OCCLUSION_COUNTER ||
	    query->b.type == PIPE_QUERY_OCCLUSION_PREDICATE) {
		unsigned num_results;
		unsigned i, j;

		/* Set top bits for unused backends. */
		num_results = buffer->b.b.width0 / (16 * ctx->max_db);
		for (j = 0; j < num_results; j++) {
			for (i = 0; i < ctx->max_db; i++) {
				if (!(ctx->backend_mask & (1<<i))) {
					results[(i * 4)+1] = 0x80000000;
					results[(i * 4)+3] = 0x80000000;
				}
			}
			results += 4 * ctx->max_db;
		}
	}
}

static struct r600_query_ops query_hw_ops = {
	.destroy = r600_query_hw_destroy,
	.begin = r600_query_hw_begin,
	.end = r600_query_hw_end,
	.get_result = r600_query_hw_get_result,
};

static void r600_query_hw_do_emit_start(struct r600_common_context *ctx,
					struct r600_query_hw *query,
					struct r600_resource *buffer,
					uint64_t va);
static void r600_query_hw_do_emit_stop(struct r600_common_context *ctx,
				       struct r600_query_hw *query,
				       struct r600_resource *buffer,
				       uint64_t va);
static void r600_query_hw_add_result(struct r600_common_context *ctx,
				     struct r600_query_hw *, void *buffer,
				     union pipe_query_result *result);
static void r600_query_hw_clear_result(struct r600_query_hw *,
				       union pipe_query_result *);

static struct r600_query_hw_ops query_hw_default_hw_ops = {
	.prepare_buffer = r600_query_hw_prepare_buffer,
	.emit_start = r600_query_hw_do_emit_start,
	.emit_stop = r600_query_hw_do_emit_stop,
	.clear_result = r600_query_hw_clear_result,
	.add_result = r600_query_hw_add_result,
};

boolean r600_query_hw_init(struct r600_common_context *rctx,
			   struct r600_query_hw *query)
{
	query->buffer.buf = r600_new_query_buffer(rctx, query);
	if (!query->buffer.buf)
		return FALSE;

	return TRUE;
}

static struct pipe_query *r600_query_hw_create(struct r600_common_context *rctx,
					       unsigned query_type,
					       unsigned index)
{
	struct r600_query_hw *query = CALLOC_STRUCT(r600_query_hw);
	if (!query)
		return NULL;

	query->b.type = query_type;
	query->b.ops = &query_hw_ops;
	query->ops = &query_hw_default_hw_ops;

	switch (query_type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
		query->result_size = 16 * rctx->max_db;
		query->num_cs_dw_begin = 6;
		query->num_cs_dw_end = 6;
		query->flags |= R600_QUERY_HW_FLAG_PREDICATE;
		break;
	case PIPE_QUERY_TIME_ELAPSED:
		query->result_size = 16;
		query->num_cs_dw_begin = 8;
		query->num_cs_dw_end = 8;
		query->flags = R600_QUERY_HW_FLAG_TIMER;
		break;
	case PIPE_QUERY_TIMESTAMP:
		query->result_size = 8;
		query->num_cs_dw_end = 8;
		query->flags = R600_QUERY_HW_FLAG_TIMER |
			       R600_QUERY_HW_FLAG_NO_START;
		break;
	case PIPE_QUERY_PRIMITIVES_EMITTED:
	case PIPE_QUERY_PRIMITIVES_GENERATED:
	case PIPE_QUERY_SO_STATISTICS:
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		/* NumPrimitivesWritten, PrimitiveStorageNeeded. */
		query->result_size = 32;
		query->num_cs_dw_begin = 6;
		query->num_cs_dw_end = 6;
		query->stream = index;
		query->flags |= R600_QUERY_HW_FLAG_PREDICATE;
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
		/* 11 values on EG, 8 on R600. */
		query->result_size = (rctx->chip_class >= EVERGREEN ? 11 : 8) * 16;
		query->num_cs_dw_begin = 6;
		query->num_cs_dw_end = 6;
		break;
	default:
		assert(0);
		FREE(query);
		return NULL;
	}

	if (!r600_query_hw_init(rctx, query)) {
		FREE(query);
		return NULL;
	}

	return (struct pipe_query *)query;
}

static void r600_update_occlusion_query_state(struct r600_common_context *rctx,
					      unsigned type, int diff)
{
	if (type == PIPE_QUERY_OCCLUSION_COUNTER ||
	    type == PIPE_QUERY_OCCLUSION_PREDICATE) {
		bool old_enable = rctx->num_occlusion_queries != 0;
		bool enable;

		rctx->num_occlusion_queries += diff;
		assert(rctx->num_occlusion_queries >= 0);

		enable = rctx->num_occlusion_queries != 0;

		if (enable != old_enable) {
			rctx->set_occlusion_query_state(&rctx->b, enable);
		}
	}
}

static unsigned event_type_for_stream(struct r600_query_hw *query)
{
	switch (query->stream) {
	default:
	case 0: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS;
	case 1: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS1;
	case 2: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS2;
	case 3: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS3;
	}
}

static void r600_query_hw_do_emit_start(struct r600_common_context *ctx,
					struct r600_query_hw *query,
					struct r600_resource *buffer,
					uint64_t va)
{
	struct radeon_winsys_cs *cs = ctx->gfx.cs;

	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va);
		radeon_emit(cs, (va >> 32) & 0xFFFF);
		break;
	case PIPE_QUERY_PRIMITIVES_EMITTED:
	case PIPE_QUERY_PRIMITIVES_GENERATED:
	case PIPE_QUERY_SO_STATISTICS:
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(event_type_for_stream(query)) | EVENT_INDEX(3));
		radeon_emit(cs, va);
		radeon_emit(cs, (va >> 32) & 0xFFFF);
		break;
	case PIPE_QUERY_TIME_ELAPSED:
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5));
		radeon_emit(cs, va);
		radeon_emit(cs, (3 << 29) | ((va >> 32) & 0xFFFF));
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_SAMPLE_PIPELINESTAT) | EVENT_INDEX(2));
		radeon_emit(cs, va);
		radeon_emit(cs, (va >> 32) & 0xFFFF);
		break;
	default:
		assert(0);
	}
	r600_emit_reloc(ctx, &ctx->gfx, query->buffer.buf, RADEON_USAGE_WRITE,
			RADEON_PRIO_QUERY);
}

static void r600_query_hw_emit_start(struct r600_common_context *ctx,
				     struct r600_query_hw *query)
{
	uint64_t va;

	r600_update_occlusion_query_state(ctx, query->b.type, 1);
	r600_update_prims_generated_query_state(ctx, query->b.type, 1);

	ctx->need_gfx_cs_space(&ctx->b, query->num_cs_dw_begin + query->num_cs_dw_end,
			       TRUE);

	/* Get a new query buffer if needed. */
	if (query->buffer.results_end + query->result_size > query->buffer.buf->b.b.width0) {
		struct r600_query_buffer *qbuf = MALLOC_STRUCT(r600_query_buffer);
		*qbuf = query->buffer;
		query->buffer.buf = r600_new_query_buffer(ctx, query);
		query->buffer.results_end = 0;
		query->buffer.previous = qbuf;
	}

	/* emit begin query */
	va = query->buffer.buf->gpu_address + query->buffer.results_end;

	query->ops->emit_start(ctx, query, query->buffer.buf, va);

	if (query->flags & R600_QUERY_HW_FLAG_TIMER)
		ctx->num_cs_dw_timer_queries_suspend += query->num_cs_dw_end;
	else
		ctx->num_cs_dw_nontimer_queries_suspend += query->num_cs_dw_end;
}

static void r600_query_hw_do_emit_stop(struct r600_common_context *ctx,
				       struct r600_query_hw *query,
				       struct r600_resource *buffer,
				       uint64_t va)
{
	struct radeon_winsys_cs *cs = ctx->gfx.cs;

	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
		va += 8;
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va);
		radeon_emit(cs, (va >> 32) & 0xFFFF);
		break;
	case PIPE_QUERY_PRIMITIVES_EMITTED:
	case PIPE_QUERY_PRIMITIVES_GENERATED:
	case PIPE_QUERY_SO_STATISTICS:
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		va += query->result_size/2;
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(event_type_for_stream(query)) | EVENT_INDEX(3));
		radeon_emit(cs, va);
		radeon_emit(cs, (va >> 32) & 0xFFFF);
		break;
	case PIPE_QUERY_TIME_ELAPSED:
		va += query->result_size/2;
		/* fall through */
	case PIPE_QUERY_TIMESTAMP:
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_CACHE_FLUSH_AND_INV_TS_EVENT) | EVENT_INDEX(5));
		radeon_emit(cs, va);
		radeon_emit(cs, (3 << 29) | ((va >> 32) & 0xFFFF));
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
		va += query->result_size/2;
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_SAMPLE_PIPELINESTAT) | EVENT_INDEX(2));
		radeon_emit(cs, va);
		radeon_emit(cs, (va >> 32) & 0xFFFF);
		break;
	default:
		assert(0);
	}
	r600_emit_reloc(ctx, &ctx->gfx, query->buffer.buf, RADEON_USAGE_WRITE,
			RADEON_PRIO_QUERY);
}

static void r600_query_hw_emit_stop(struct r600_common_context *ctx,
				    struct r600_query_hw *query)
{
	uint64_t va;

	/* The queries which need begin already called this in begin_query. */
	if (query->flags & R600_QUERY_HW_FLAG_NO_START) {
		ctx->need_gfx_cs_space(&ctx->b, query->num_cs_dw_end, FALSE);
	}

	/* emit end query */
	va = query->buffer.buf->gpu_address + query->buffer.results_end;

	query->ops->emit_stop(ctx, query, query->buffer.buf, va);

	query->buffer.results_end += query->result_size;

	if (!(query->flags & R600_QUERY_HW_FLAG_NO_START)) {
		if (query->flags & R600_QUERY_HW_FLAG_TIMER)
			ctx->num_cs_dw_timer_queries_suspend -= query->num_cs_dw_end;
		else
			ctx->num_cs_dw_nontimer_queries_suspend -= query->num_cs_dw_end;
	}

	r600_update_occlusion_query_state(ctx, query->b.type, -1);
	r600_update_prims_generated_query_state(ctx, query->b.type, -1);
}

static void r600_emit_query_predication(struct r600_common_context *ctx,
					struct r600_atom *atom)
{
	struct radeon_winsys_cs *cs = ctx->gfx.cs;
	struct r600_query_hw *query = (struct r600_query_hw *)ctx->render_cond;
	struct r600_query_buffer *qbuf;
	uint32_t op;
	bool flag_wait;

	if (!query)
		return;

	flag_wait = ctx->render_cond_mode == PIPE_RENDER_COND_WAIT ||
		    ctx->render_cond_mode == PIPE_RENDER_COND_BY_REGION_WAIT;

	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
		op = PRED_OP(PREDICATION_OP_ZPASS);
		break;
	case PIPE_QUERY_PRIMITIVES_EMITTED:
	case PIPE_QUERY_PRIMITIVES_GENERATED:
	case PIPE_QUERY_SO_STATISTICS:
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		op = PRED_OP(PREDICATION_OP_PRIMCOUNT);
		break;
	default:
		assert(0);
		return;
	}

	/* if true then invert, see GL_ARB_conditional_render_inverted */
	if (ctx->render_cond_invert)
		op |= PREDICATION_DRAW_NOT_VISIBLE; /* Draw if not visable/overflow */
	else
		op |= PREDICATION_DRAW_VISIBLE; /* Draw if visable/overflow */

	op |= flag_wait ? PREDICATION_HINT_WAIT : PREDICATION_HINT_NOWAIT_DRAW;
	
	/* emit predicate packets for all data blocks */
	for (qbuf = &query->buffer; qbuf; qbuf = qbuf->previous) {
		unsigned results_base = 0;
		uint64_t va = qbuf->buf->gpu_address;

		while (results_base < qbuf->results_end) {
			radeon_emit(cs, PKT3(PKT3_SET_PREDICATION, 1, 0));
			radeon_emit(cs, va + results_base);
			radeon_emit(cs, op | (((va + results_base) >> 32) & 0xFF));
			r600_emit_reloc(ctx, &ctx->gfx, qbuf->buf, RADEON_USAGE_READ,
					RADEON_PRIO_QUERY);
			results_base += query->result_size;

			/* set CONTINUE bit for all packets except the first */
			op |= PREDICATION_CONTINUE;
		}
	}
}

static struct pipe_query *r600_create_query(struct pipe_context *ctx, unsigned query_type, unsigned index)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;

	if (query_type == PIPE_QUERY_TIMESTAMP_DISJOINT ||
	    query_type == PIPE_QUERY_GPU_FINISHED ||
	    query_type >= PIPE_QUERY_DRIVER_SPECIFIC)
		return r600_query_sw_create(ctx, query_type);

	return r600_query_hw_create(rctx, query_type, index);
}

static void r600_destroy_query(struct pipe_context *ctx, struct pipe_query *query)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	rquery->ops->destroy(rctx, rquery);
}

static boolean r600_begin_query(struct pipe_context *ctx,
                                struct pipe_query *query)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	return rquery->ops->begin(rctx, rquery);
}

static void r600_query_hw_reset_buffers(struct r600_common_context *rctx,
					struct r600_query_hw *query)
{
	struct r600_query_buffer *prev = query->buffer.previous;

	/* Discard the old query buffers. */
	while (prev) {
		struct r600_query_buffer *qbuf = prev;
		prev = prev->previous;
		pipe_resource_reference((struct pipe_resource**)&qbuf->buf, NULL);
		FREE(qbuf);
	}

	if (query->flags & R600_QUERY_HW_FLAG_PREDICATE) {
		/* Obtain a new buffer if the current one can't be mapped without a stall. */
		if (r600_rings_is_buffer_referenced(rctx, query->buffer.buf->buf, RADEON_USAGE_READWRITE) ||
		    !rctx->ws->buffer_wait(query->buffer.buf->buf, 0, RADEON_USAGE_READWRITE)) {
			pipe_resource_reference((struct pipe_resource**)&query->buffer.buf, NULL);
			query->buffer.buf = r600_new_query_buffer(rctx, query);
		} else {
			query->ops->prepare_buffer(rctx, query, query->buffer.buf);
		}
	}

	query->buffer.results_end = 0;
	query->buffer.previous = NULL;
}

boolean r600_query_hw_begin(struct r600_common_context *rctx,
			    struct r600_query *rquery)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;

	if (query->flags & R600_QUERY_HW_FLAG_NO_START) {
		assert(0);
		return false;
	}

	r600_query_hw_reset_buffers(rctx, query);

	r600_query_hw_emit_start(rctx, query);

	if (query->flags & R600_QUERY_HW_FLAG_TIMER)
		LIST_ADDTAIL(&query->list, &rctx->active_timer_queries);
	else
		LIST_ADDTAIL(&query->list, &rctx->active_nontimer_queries);
   return true;
}

static void r600_end_query(struct pipe_context *ctx, struct pipe_query *query)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	rquery->ops->end(rctx, rquery);
}

void r600_query_hw_end(struct r600_common_context *rctx,
			      struct r600_query *rquery)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;

	if (query->flags & R600_QUERY_HW_FLAG_NO_START)
		r600_query_hw_reset_buffers(rctx, query);

	r600_query_hw_emit_stop(rctx, query);

	if (!(query->flags & R600_QUERY_HW_FLAG_NO_START))
		LIST_DELINIT(&query->list);
}

static unsigned r600_query_read_result(void *map, unsigned start_index, unsigned end_index,
				       bool test_status_bit)
{
	uint32_t *current_result = (uint32_t*)map;
	uint64_t start, end;

	start = (uint64_t)current_result[start_index] |
		(uint64_t)current_result[start_index+1] << 32;
	end = (uint64_t)current_result[end_index] |
	      (uint64_t)current_result[end_index+1] << 32;

	if (!test_status_bit ||
	    ((start & 0x8000000000000000UL) && (end & 0x8000000000000000UL))) {
		return end - start;
	}
	return 0;
}

static void r600_query_hw_add_result(struct r600_common_context *ctx,
				     struct r600_query_hw *query,
				     void *buffer,
				     union pipe_query_result *result)
{
	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER: {
		unsigned results_base = 0;
		while (results_base != query->result_size) {
			result->u64 +=
				r600_query_read_result(buffer + results_base, 0, 2, true);
			results_base += 16;
		}
		break;
	}
	case PIPE_QUERY_OCCLUSION_PREDICATE: {
		unsigned results_base = 0;
		while (results_base != query->result_size) {
			result->b = result->b ||
				r600_query_read_result(buffer + results_base, 0, 2, true) != 0;
			results_base += 16;
		}
		break;
	}
	case PIPE_QUERY_TIME_ELAPSED:
		result->u64 += r600_query_read_result(buffer, 0, 2, false);
		break;
	case PIPE_QUERY_TIMESTAMP:
	{
		uint32_t *current_result = (uint32_t*)buffer;
		result->u64 = (uint64_t)current_result[0] |
			      (uint64_t)current_result[1] << 32;
		break;
	}
	case PIPE_QUERY_PRIMITIVES_EMITTED:
		/* SAMPLE_STREAMOUTSTATS stores this structure:
		 * {
		 *    u64 NumPrimitivesWritten;
		 *    u64 PrimitiveStorageNeeded;
		 * }
		 * We only need NumPrimitivesWritten here. */
		result->u64 += r600_query_read_result(buffer, 2, 6, true);
		break;
	case PIPE_QUERY_PRIMITIVES_GENERATED:
		/* Here we read PrimitiveStorageNeeded. */
		result->u64 += r600_query_read_result(buffer, 0, 4, true);
		break;
	case PIPE_QUERY_SO_STATISTICS:
		result->so_statistics.num_primitives_written +=
			r600_query_read_result(buffer, 2, 6, true);
		result->so_statistics.primitives_storage_needed +=
			r600_query_read_result(buffer, 0, 4, true);
		break;
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		result->b = result->b ||
			r600_query_read_result(buffer, 2, 6, true) !=
			r600_query_read_result(buffer, 0, 4, true);
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
		if (ctx->chip_class >= EVERGREEN) {
			result->pipeline_statistics.ps_invocations +=
				r600_query_read_result(buffer, 0, 22, false);
			result->pipeline_statistics.c_primitives +=
				r600_query_read_result(buffer, 2, 24, false);
			result->pipeline_statistics.c_invocations +=
				r600_query_read_result(buffer, 4, 26, false);
			result->pipeline_statistics.vs_invocations +=
				r600_query_read_result(buffer, 6, 28, false);
			result->pipeline_statistics.gs_invocations +=
				r600_query_read_result(buffer, 8, 30, false);
			result->pipeline_statistics.gs_primitives +=
				r600_query_read_result(buffer, 10, 32, false);
			result->pipeline_statistics.ia_primitives +=
				r600_query_read_result(buffer, 12, 34, false);
			result->pipeline_statistics.ia_vertices +=
				r600_query_read_result(buffer, 14, 36, false);
			result->pipeline_statistics.hs_invocations +=
				r600_query_read_result(buffer, 16, 38, false);
			result->pipeline_statistics.ds_invocations +=
				r600_query_read_result(buffer, 18, 40, false);
			result->pipeline_statistics.cs_invocations +=
				r600_query_read_result(buffer, 20, 42, false);
		} else {
			result->pipeline_statistics.ps_invocations +=
				r600_query_read_result(buffer, 0, 16, false);
			result->pipeline_statistics.c_primitives +=
				r600_query_read_result(buffer, 2, 18, false);
			result->pipeline_statistics.c_invocations +=
				r600_query_read_result(buffer, 4, 20, false);
			result->pipeline_statistics.vs_invocations +=
				r600_query_read_result(buffer, 6, 22, false);
			result->pipeline_statistics.gs_invocations +=
				r600_query_read_result(buffer, 8, 24, false);
			result->pipeline_statistics.gs_primitives +=
				r600_query_read_result(buffer, 10, 26, false);
			result->pipeline_statistics.ia_primitives +=
				r600_query_read_result(buffer, 12, 28, false);
			result->pipeline_statistics.ia_vertices +=
				r600_query_read_result(buffer, 14, 30, false);
		}
#if 0 /* for testing */
		printf("Pipeline stats: IA verts=%llu, IA prims=%llu, VS=%llu, HS=%llu, "
		       "DS=%llu, GS=%llu, GS prims=%llu, Clipper=%llu, "
		       "Clipper prims=%llu, PS=%llu, CS=%llu\n",
		       result->pipeline_statistics.ia_vertices,
		       result->pipeline_statistics.ia_primitives,
		       result->pipeline_statistics.vs_invocations,
		       result->pipeline_statistics.hs_invocations,
		       result->pipeline_statistics.ds_invocations,
		       result->pipeline_statistics.gs_invocations,
		       result->pipeline_statistics.gs_primitives,
		       result->pipeline_statistics.c_invocations,
		       result->pipeline_statistics.c_primitives,
		       result->pipeline_statistics.ps_invocations,
		       result->pipeline_statistics.cs_invocations);
#endif
		break;
	default:
		assert(0);
	}
}

static boolean r600_get_query_result(struct pipe_context *ctx,
				     struct pipe_query *query, boolean wait,
				     union pipe_query_result *result)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	return rquery->ops->get_result(rctx, rquery, wait, result);
}

static void r600_query_hw_clear_result(struct r600_query_hw *query,
				       union pipe_query_result *result)
{
	util_query_clear_result(result, query->b.type);
}

boolean r600_query_hw_get_result(struct r600_common_context *rctx,
				 struct r600_query *rquery,
				 boolean wait, union pipe_query_result *result)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;
	struct r600_query_buffer *qbuf;

	query->ops->clear_result(query, result);

	for (qbuf = &query->buffer; qbuf; qbuf = qbuf->previous) {
		unsigned results_base = 0;
		void *map;

		map = r600_buffer_map_sync_with_rings(rctx, qbuf->buf,
						      PIPE_TRANSFER_READ |
						      (wait ? 0 : PIPE_TRANSFER_DONTBLOCK));
		if (!map)
			return FALSE;

		while (results_base != qbuf->results_end) {
			query->ops->add_result(rctx, query, map + results_base,
					       result);
			results_base += query->result_size;
		}
	}

	/* Convert the time to expected units. */
	if (rquery->type == PIPE_QUERY_TIME_ELAPSED ||
	    rquery->type == PIPE_QUERY_TIMESTAMP) {
		result->u64 = (1000000 * result->u64) / rctx->screen->info.clock_crystal_freq;
	}
	return TRUE;
}

static void r600_render_condition(struct pipe_context *ctx,
				  struct pipe_query *query,
				  boolean condition,
				  uint mode)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query_hw *rquery = (struct r600_query_hw *)query;
	struct r600_query_buffer *qbuf;
	struct r600_atom *atom = &rctx->render_cond_atom;

	rctx->render_cond = query;
	rctx->render_cond_invert = condition;
	rctx->render_cond_mode = mode;

	/* Compute the size of SET_PREDICATION packets. */
	atom->num_dw = 0;
	if (query) {
		for (qbuf = &rquery->buffer; qbuf; qbuf = qbuf->previous)
			atom->num_dw += (qbuf->results_end / rquery->result_size) * 5;
	}

	rctx->set_atom_dirty(rctx, atom, query != NULL);
}

static void r600_suspend_queries(struct r600_common_context *ctx,
				 struct list_head *query_list,
				 unsigned *num_cs_dw_queries_suspend)
{
	struct r600_query_hw *query;

	LIST_FOR_EACH_ENTRY(query, query_list, list) {
		r600_query_hw_emit_stop(ctx, query);
	}
	assert(*num_cs_dw_queries_suspend == 0);
}

void r600_suspend_nontimer_queries(struct r600_common_context *ctx)
{
	r600_suspend_queries(ctx, &ctx->active_nontimer_queries,
			     &ctx->num_cs_dw_nontimer_queries_suspend);
}

void r600_suspend_timer_queries(struct r600_common_context *ctx)
{
	r600_suspend_queries(ctx, &ctx->active_timer_queries,
			     &ctx->num_cs_dw_timer_queries_suspend);
}

static unsigned r600_queries_num_cs_dw_for_resuming(struct r600_common_context *ctx,
						    struct list_head *query_list)
{
	struct r600_query_hw *query;
	unsigned num_dw = 0;

	LIST_FOR_EACH_ENTRY(query, query_list, list) {
		/* begin + end */
		num_dw += query->num_cs_dw_begin + query->num_cs_dw_end;

		/* Workaround for the fact that
		 * num_cs_dw_nontimer_queries_suspend is incremented for every
		 * resumed query, which raises the bar in need_cs_space for
		 * queries about to be resumed.
		 */
		num_dw += query->num_cs_dw_end;
	}
	/* primitives generated query */
	num_dw += ctx->streamout.enable_atom.num_dw;
	/* guess for ZPASS enable or PERFECT_ZPASS_COUNT enable updates */
	num_dw += 13;

	return num_dw;
}

static void r600_resume_queries(struct r600_common_context *ctx,
				struct list_head *query_list,
				unsigned *num_cs_dw_queries_suspend)
{
	struct r600_query_hw *query;
	unsigned num_cs_dw = r600_queries_num_cs_dw_for_resuming(ctx, query_list);

	assert(*num_cs_dw_queries_suspend == 0);

	/* Check CS space here. Resuming must not be interrupted by flushes. */
	ctx->need_gfx_cs_space(&ctx->b, num_cs_dw, TRUE);

	LIST_FOR_EACH_ENTRY(query, query_list, list) {
		r600_query_hw_emit_start(ctx, query);
	}
}

void r600_resume_nontimer_queries(struct r600_common_context *ctx)
{
	r600_resume_queries(ctx, &ctx->active_nontimer_queries,
			    &ctx->num_cs_dw_nontimer_queries_suspend);
}

void r600_resume_timer_queries(struct r600_common_context *ctx)
{
	r600_resume_queries(ctx, &ctx->active_timer_queries,
			    &ctx->num_cs_dw_timer_queries_suspend);
}

/* Get backends mask */
void r600_query_init_backend_mask(struct r600_common_context *ctx)
{
	struct radeon_winsys_cs *cs = ctx->gfx.cs;
	struct r600_resource *buffer;
	uint32_t *results;
	unsigned num_backends = ctx->screen->info.num_render_backends;
	unsigned i, mask = 0;

	/* if backend_map query is supported by the kernel */
	if (ctx->screen->info.r600_gb_backend_map_valid) {
		unsigned num_tile_pipes = ctx->screen->info.num_tile_pipes;
		unsigned backend_map = ctx->screen->info.r600_gb_backend_map;
		unsigned item_width, item_mask;

		if (ctx->chip_class >= EVERGREEN) {
			item_width = 4;
			item_mask = 0x7;
		} else {
			item_width = 2;
			item_mask = 0x3;
		}

		while(num_tile_pipes--) {
			i = backend_map & item_mask;
			mask |= (1<<i);
			backend_map >>= item_width;
		}
		if (mask != 0) {
			ctx->backend_mask = mask;
			return;
		}
	}

	/* otherwise backup path for older kernels */

	/* create buffer for event data */
	buffer = (struct r600_resource*)
		pipe_buffer_create(ctx->b.screen, PIPE_BIND_CUSTOM,
				   PIPE_USAGE_STAGING, ctx->max_db*16);
	if (!buffer)
		goto err;

	/* initialize buffer with zeroes */
	results = r600_buffer_map_sync_with_rings(ctx, buffer, PIPE_TRANSFER_WRITE);
	if (results) {
		memset(results, 0, ctx->max_db * 4 * 4);

		/* emit EVENT_WRITE for ZPASS_DONE */
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, buffer->gpu_address);
		radeon_emit(cs, buffer->gpu_address >> 32);

		r600_emit_reloc(ctx, &ctx->gfx, buffer,
                                RADEON_USAGE_WRITE, RADEON_PRIO_QUERY);

		/* analyze results */
		results = r600_buffer_map_sync_with_rings(ctx, buffer, PIPE_TRANSFER_READ);
		if (results) {
			for(i = 0; i < ctx->max_db; i++) {
				/* at least highest bit will be set if backend is used */
				if (results[i*4 + 1])
					mask |= (1<<i);
			}
		}
	}

	pipe_resource_reference((struct pipe_resource**)&buffer, NULL);

	if (mask != 0) {
		ctx->backend_mask = mask;
		return;
	}

err:
	/* fallback to old method - set num_backends lower bits to 1 */
	ctx->backend_mask = (~((uint32_t)0))>>(32-num_backends);
	return;
}

#define XFULL(name_, query_type_, type_, result_type_, group_id_) \
	{ \
		.name = name_, \
		.query_type = R600_QUERY_##query_type_, \
		.type = PIPE_DRIVER_QUERY_TYPE_##type_, \
		.result_type = PIPE_DRIVER_QUERY_RESULT_TYPE_##result_type_, \
		.group_id = group_id_ \
	}

#define X(name_, query_type_, type_, result_type_) \
	XFULL(name_, query_type_, type_, result_type_, ~(unsigned)0)

#define XG(group_, name_, query_type_, type_, result_type_) \
	XFULL(name_, query_type_, type_, result_type_, R600_QUERY_GROUP_##group_)

static struct pipe_driver_query_info r600_driver_query_list[] = {
	X("num-compilations",		NUM_COMPILATIONS,	UINT64, CUMULATIVE),
	X("num-shaders-created",	NUM_SHADERS_CREATED,	UINT64, CUMULATIVE),
	X("draw-calls",			DRAW_CALLS,		UINT64, CUMULATIVE),
	X("requested-VRAM",		REQUESTED_VRAM,		BYTES, AVERAGE),
	X("requested-GTT",		REQUESTED_GTT,		BYTES, AVERAGE),
	X("buffer-wait-time",		BUFFER_WAIT_TIME,	MICROSECONDS, CUMULATIVE),
	X("num-cs-flushes",		NUM_CS_FLUSHES,		UINT64, CUMULATIVE),
	X("num-bytes-moved",		NUM_BYTES_MOVED,	BYTES, CUMULATIVE),
	X("VRAM-usage",			VRAM_USAGE,		BYTES, AVERAGE),
	X("GTT-usage",			GTT_USAGE,		BYTES, AVERAGE),

	/* GPIN queries are for the benefit of old versions of GPUPerfStudio,
	 * which use it as a fallback path to detect the GPU type.
	 *
	 * Note: The names of these queries are significant for GPUPerfStudio
	 * (and possibly their order as well). */
	XG(GPIN, "GPIN_000",		GPIN_ASIC_ID,		UINT, AVERAGE),
	XG(GPIN, "GPIN_001",		GPIN_NUM_SIMD,		UINT, AVERAGE),
	XG(GPIN, "GPIN_002",		GPIN_NUM_RB,		UINT, AVERAGE),
	XG(GPIN, "GPIN_003",		GPIN_NUM_SPI,		UINT, AVERAGE),
	XG(GPIN, "GPIN_004",		GPIN_NUM_SE,		UINT, AVERAGE),

	/* The following queries must be at the end of the list because their
	 * availability is adjusted dynamically based on the DRM version. */
	X("GPU-load",			GPU_LOAD,		UINT64, AVERAGE),
	X("temperature",		GPU_TEMPERATURE,	UINT64, AVERAGE),
	X("shader-clock",		CURRENT_GPU_SCLK,	HZ, AVERAGE),
	X("memory-clock",		CURRENT_GPU_MCLK,	HZ, AVERAGE),
};

#undef X
#undef XG
#undef XFULL

static unsigned r600_get_num_queries(struct r600_common_screen *rscreen)
{
	if (rscreen->info.drm_major == 2 && rscreen->info.drm_minor >= 42)
		return Elements(r600_driver_query_list);
	else if (rscreen->info.drm_major == 3)
		return Elements(r600_driver_query_list) - 3;
	else
		return Elements(r600_driver_query_list) - 4;
}

static int r600_get_driver_query_info(struct pipe_screen *screen,
				      unsigned index,
				      struct pipe_driver_query_info *info)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen*)screen;
	unsigned num_queries = r600_get_num_queries(rscreen);

	if (!info) {
		unsigned num_perfcounters =
			r600_get_perfcounter_info(rscreen, 0, NULL);

		return num_queries + num_perfcounters;
	}

	if (index >= num_queries)
		return r600_get_perfcounter_info(rscreen, index - num_queries, info);

	*info = r600_driver_query_list[index];

	switch (info->query_type) {
	case R600_QUERY_REQUESTED_VRAM:
	case R600_QUERY_VRAM_USAGE:
		info->max_value.u64 = rscreen->info.vram_size;
		break;
	case R600_QUERY_REQUESTED_GTT:
	case R600_QUERY_GTT_USAGE:
		info->max_value.u64 = rscreen->info.gart_size;
		break;
	case R600_QUERY_GPU_TEMPERATURE:
		info->max_value.u64 = 125;
		break;
	}

	if (info->group_id != ~(unsigned)0 && rscreen->perfcounters)
		info->group_id += rscreen->perfcounters->num_groups;

	return 1;
}

/* Note: Unfortunately, GPUPerfStudio hardcodes the order of hardware
 * performance counter groups, so be careful when changing this and related
 * functions.
 */
static int r600_get_driver_query_group_info(struct pipe_screen *screen,
					    unsigned index,
					    struct pipe_driver_query_group_info *info)
{
	struct r600_common_screen *rscreen = (struct r600_common_screen *)screen;
	unsigned num_pc_groups = 0;

	if (rscreen->perfcounters)
		num_pc_groups = rscreen->perfcounters->num_groups;

	if (!info)
		return num_pc_groups + R600_NUM_SW_QUERY_GROUPS;

	if (index < num_pc_groups)
		return r600_get_perfcounter_group_info(rscreen, index, info);

	index -= num_pc_groups;
	if (index >= R600_NUM_SW_QUERY_GROUPS)
		return 0;

	info->name = "GPIN";
	info->max_active_queries = 5;
	info->num_queries = 5;
	return 1;
}

void r600_query_init(struct r600_common_context *rctx)
{
	rctx->b.create_query = r600_create_query;
	rctx->b.create_batch_query = r600_create_batch_query;
	rctx->b.destroy_query = r600_destroy_query;
	rctx->b.begin_query = r600_begin_query;
	rctx->b.end_query = r600_end_query;
	rctx->b.get_query_result = r600_get_query_result;
	rctx->render_cond_atom.emit = r600_emit_query_predication;

	if (((struct r600_common_screen*)rctx->b.screen)->info.num_render_backends > 0)
	    rctx->b.render_condition = r600_render_condition;

	LIST_INITHEAD(&rctx->active_nontimer_queries);
	LIST_INITHEAD(&rctx->active_timer_queries);
}

void r600_init_screen_query_functions(struct r600_common_screen *rscreen)
{
	rscreen->b.get_driver_query_info = r600_get_driver_query_info;
	rscreen->b.get_driver_query_group_info = r600_get_driver_query_group_info;
}
