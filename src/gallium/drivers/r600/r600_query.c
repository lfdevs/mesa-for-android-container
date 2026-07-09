/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2014 Marek Olšák <marek.olsak@amd.com>
 * SPDX-License-Identifier: MIT
 */

#include "r600_query.h"
#include "r600_pipe.h"
#include "r600_cs.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/os_time.h"
#include "nir/nir_builder.h"

#define R600_MAX_STREAMS 4

struct r600_hw_query_params {
	unsigned start_offset;
	unsigned end_offset;
	unsigned fence_offset;
	unsigned pair_stride;
	unsigned pair_count;
};

/* Queries without buffer handling or suspend/resume. */
struct r600_query_sw {
	struct r600_query b;

	uint64_t begin_result;
	uint64_t end_result;

	uint64_t begin_time;
	uint64_t end_time;

	/* Fence for GPU_FINISHED. */
	struct pipe_fence_handle *fence;
};

static void r600_query_sw_destroy(struct r600_common_screen *rscreen,
				  struct r600_query *rquery)
{
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;

	rscreen->b.fence_reference(&rscreen->b, &query->fence, NULL);
	FREE(query);
}

static enum radeon_value_id winsys_id_from_type(unsigned type)
{
	switch (type) {
	case R600_QUERY_REQUESTED_VRAM: return RADEON_REQUESTED_VRAM_MEMORY;
	case R600_QUERY_REQUESTED_GTT: return RADEON_REQUESTED_GTT_MEMORY;
	case R600_QUERY_MAPPED_VRAM: return RADEON_MAPPED_VRAM;
	case R600_QUERY_MAPPED_GTT: return RADEON_MAPPED_GTT;
	case R600_QUERY_BUFFER_WAIT_TIME: return RADEON_BUFFER_WAIT_TIME_NS;
	case R600_QUERY_NUM_MAPPED_BUFFERS: return RADEON_NUM_MAPPED_BUFFERS;
	case R600_QUERY_NUM_GFX_IBS: return RADEON_NUM_GFX_IBS;
	case R600_QUERY_NUM_SDMA_IBS: return RADEON_NUM_SDMA_IBS;
	case R600_QUERY_GFX_BO_LIST_SIZE: return RADEON_GFX_BO_LIST_COUNTER;
	case R600_QUERY_NUM_BYTES_MOVED: return RADEON_NUM_BYTES_MOVED;
	case R600_QUERY_NUM_EVICTIONS: return RADEON_NUM_EVICTIONS;
	case R600_QUERY_NUM_VRAM_CPU_PAGE_FAULTS: return RADEON_NUM_VRAM_CPU_PAGE_FAULTS;
	case R600_QUERY_VRAM_USAGE: return RADEON_VRAM_USAGE;
	case R600_QUERY_VRAM_VIS_USAGE: return RADEON_VRAM_VIS_USAGE;
	case R600_QUERY_GTT_USAGE: return RADEON_GTT_USAGE;
	case R600_QUERY_GPU_TEMPERATURE: return RADEON_GPU_TEMPERATURE;
	case R600_QUERY_CURRENT_GPU_SCLK: return RADEON_CURRENT_SCLK;
	case R600_QUERY_CURRENT_GPU_MCLK: return RADEON_CURRENT_MCLK;
	case R600_QUERY_CS_THREAD_BUSY: return RADEON_CS_THREAD_TIME;
	default: UNREACHABLE("query type does not correspond to winsys id");
	}
}

static bool r600_query_sw_begin(struct r600_common_context *rctx,
				struct r600_query *rquery)
{
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;
	enum radeon_value_id ws_id;

	switch(query->b.type) {
	case PIPE_QUERY_TIMESTAMP_DISJOINT:
	case PIPE_QUERY_GPU_FINISHED:
		break;
	case R600_QUERY_DRAW_CALLS:
		query->begin_result = rctx->num_draw_calls;
		break;
	case R600_QUERY_DECOMPRESS_CALLS:
		query->begin_result = rctx->num_decompress_calls;
		break;
	case R600_QUERY_MRT_DRAW_CALLS:
		query->begin_result = rctx->num_mrt_draw_calls;
		break;
	case R600_QUERY_PRIM_RESTART_CALLS:
		query->begin_result = rctx->num_prim_restart_calls;
		break;
	case R600_QUERY_SPILL_DRAW_CALLS:
		query->begin_result = rctx->num_spill_draw_calls;
		break;
	case R600_QUERY_COMPUTE_CALLS:
		query->begin_result = rctx->num_compute_calls;
		break;
	case R600_QUERY_SPILL_COMPUTE_CALLS:
		query->begin_result = rctx->num_spill_compute_calls;
		break;
	case R600_QUERY_DMA_CALLS:
		query->begin_result = rctx->num_dma_calls;
		break;
	case R600_QUERY_CP_DMA_CALLS:
		query->begin_result = rctx->num_cp_dma_calls;
		break;
	case R600_QUERY_NUM_VS_FLUSHES:
		query->begin_result = rctx->num_vs_flushes;
		break;
	case R600_QUERY_NUM_PS_FLUSHES:
		query->begin_result = rctx->num_ps_flushes;
		break;
	case R600_QUERY_NUM_CS_FLUSHES:
		query->begin_result = rctx->num_cs_flushes;
		break;
	case R600_QUERY_NUM_CB_CACHE_FLUSHES:
		query->begin_result = rctx->num_cb_cache_flushes;
		break;
	case R600_QUERY_NUM_DB_CACHE_FLUSHES:
		query->begin_result = rctx->num_db_cache_flushes;
		break;
	case R600_QUERY_NUM_RESIDENT_HANDLES:
		query->begin_result = rctx->num_resident_handles;
		break;
	case R600_QUERY_TC_OFFLOADED_SLOTS:
		query->begin_result = rctx->tc ? rctx->tc->num_offloaded_slots : 0;
		break;
	case R600_QUERY_TC_DIRECT_SLOTS:
		query->begin_result = rctx->tc ? rctx->tc->num_direct_slots : 0;
		break;
	case R600_QUERY_TC_NUM_SYNCS:
		query->begin_result = rctx->tc ? rctx->tc->num_syncs : 0;
		break;
	case R600_QUERY_REQUESTED_VRAM:
	case R600_QUERY_REQUESTED_GTT:
	case R600_QUERY_MAPPED_VRAM:
	case R600_QUERY_MAPPED_GTT:
	case R600_QUERY_VRAM_USAGE:
	case R600_QUERY_VRAM_VIS_USAGE:
	case R600_QUERY_GTT_USAGE:
	case R600_QUERY_GPU_TEMPERATURE:
	case R600_QUERY_CURRENT_GPU_SCLK:
	case R600_QUERY_CURRENT_GPU_MCLK:
	case R600_QUERY_NUM_MAPPED_BUFFERS:
		query->begin_result = 0;
		break;
	case R600_QUERY_BUFFER_WAIT_TIME:
	case R600_QUERY_NUM_GFX_IBS:
	case R600_QUERY_NUM_SDMA_IBS:
	case R600_QUERY_NUM_BYTES_MOVED:
	case R600_QUERY_NUM_EVICTIONS:
	case R600_QUERY_NUM_VRAM_CPU_PAGE_FAULTS: {
		enum radeon_value_id ws_id = winsys_id_from_type(query->b.type);
		query->begin_result = rctx->ws->query_value(rctx->ws, ws_id);
		break;
	}
	case R600_QUERY_GFX_BO_LIST_SIZE:
		ws_id = winsys_id_from_type(query->b.type);
		query->begin_result = rctx->ws->query_value(rctx->ws, ws_id);
		query->begin_time = rctx->ws->query_value(rctx->ws,
							  RADEON_NUM_GFX_IBS);
		break;
	case R600_QUERY_CS_THREAD_BUSY:
		ws_id = winsys_id_from_type(query->b.type);
		query->begin_result = rctx->ws->query_value(rctx->ws, ws_id);
		query->begin_time = os_time_get_nano();
		break;
	case R600_QUERY_GALLIUM_THREAD_BUSY:
		query->begin_result =
			rctx->tc ? util_queue_get_thread_time_nano(&rctx->tc->queue, 0) : 0;
		query->begin_time = os_time_get_nano();
		break;
	case R600_QUERY_GPU_LOAD:
	case R600_QUERY_GPU_SHADERS_BUSY:
	case R600_QUERY_GPU_TA_BUSY:
	case R600_QUERY_GPU_GDS_BUSY:
	case R600_QUERY_GPU_VGT_BUSY:
	case R600_QUERY_GPU_IA_BUSY:
	case R600_QUERY_GPU_SX_BUSY:
	case R600_QUERY_GPU_WD_BUSY:
	case R600_QUERY_GPU_BCI_BUSY:
	case R600_QUERY_GPU_SC_BUSY:
	case R600_QUERY_GPU_PA_BUSY:
	case R600_QUERY_GPU_DB_BUSY:
	case R600_QUERY_GPU_CP_BUSY:
	case R600_QUERY_GPU_CB_BUSY:
	case R600_QUERY_GPU_SDMA_BUSY:
	case R600_QUERY_GPU_PFP_BUSY:
	case R600_QUERY_GPU_MEQ_BUSY:
	case R600_QUERY_GPU_ME_BUSY:
	case R600_QUERY_GPU_SURF_SYNC_BUSY:
	case R600_QUERY_GPU_CP_DMA_BUSY:
	case R600_QUERY_GPU_SCRATCH_RAM_BUSY:
		query->begin_result = r600_begin_counter(rctx->screen,
							 query->b.type);
		break;
	case R600_QUERY_NUM_COMPILATIONS:
		query->begin_result = p_atomic_read(&rctx->screen->num_compilations);
		break;
	case R600_QUERY_NUM_SHADERS_CREATED:
		query->begin_result = p_atomic_read(&rctx->screen->num_shaders_created);
		break;
	case R600_QUERY_NUM_SHADER_CACHE_HITS:
		query->begin_result =
			p_atomic_read(&rctx->screen->num_shader_cache_hits);
		break;
	case R600_QUERY_GPIN_ASIC_ID:
	case R600_QUERY_GPIN_NUM_SIMD:
	case R600_QUERY_GPIN_NUM_RB:
	case R600_QUERY_GPIN_NUM_SPI:
	case R600_QUERY_GPIN_NUM_SE:
		break;
	default:
		UNREACHABLE("r600_query_sw_begin: bad query type");
	}

	return true;
}

static bool r600_query_sw_end(struct r600_common_context *rctx,
			      struct r600_query *rquery)
{
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;
	enum radeon_value_id ws_id;

	switch(query->b.type) {
	case PIPE_QUERY_TIMESTAMP_DISJOINT:
		break;
	case PIPE_QUERY_GPU_FINISHED:
		rctx->b.flush(&rctx->b, &query->fence, PIPE_FLUSH_DEFERRED);
		break;
	case R600_QUERY_DRAW_CALLS:
		query->end_result = rctx->num_draw_calls;
		break;
	case R600_QUERY_DECOMPRESS_CALLS:
		query->end_result = rctx->num_decompress_calls;
		break;
	case R600_QUERY_MRT_DRAW_CALLS:
		query->end_result = rctx->num_mrt_draw_calls;
		break;
	case R600_QUERY_PRIM_RESTART_CALLS:
		query->end_result = rctx->num_prim_restart_calls;
		break;
	case R600_QUERY_SPILL_DRAW_CALLS:
		query->end_result = rctx->num_spill_draw_calls;
		break;
	case R600_QUERY_COMPUTE_CALLS:
		query->end_result = rctx->num_compute_calls;
		break;
	case R600_QUERY_SPILL_COMPUTE_CALLS:
		query->end_result = rctx->num_spill_compute_calls;
		break;
	case R600_QUERY_DMA_CALLS:
		query->end_result = rctx->num_dma_calls;
		break;
	case R600_QUERY_CP_DMA_CALLS:
		query->end_result = rctx->num_cp_dma_calls;
		break;
	case R600_QUERY_NUM_VS_FLUSHES:
		query->end_result = rctx->num_vs_flushes;
		break;
	case R600_QUERY_NUM_PS_FLUSHES:
		query->end_result = rctx->num_ps_flushes;
		break;
	case R600_QUERY_NUM_CS_FLUSHES:
		query->end_result = rctx->num_cs_flushes;
		break;
	case R600_QUERY_NUM_CB_CACHE_FLUSHES:
		query->end_result = rctx->num_cb_cache_flushes;
		break;
	case R600_QUERY_NUM_DB_CACHE_FLUSHES:
		query->end_result = rctx->num_db_cache_flushes;
		break;
	case R600_QUERY_NUM_RESIDENT_HANDLES:
		query->end_result = rctx->num_resident_handles;
		break;
	case R600_QUERY_TC_OFFLOADED_SLOTS:
		query->end_result = rctx->tc ? rctx->tc->num_offloaded_slots : 0;
		break;
	case R600_QUERY_TC_DIRECT_SLOTS:
		query->end_result = rctx->tc ? rctx->tc->num_direct_slots : 0;
		break;
	case R600_QUERY_TC_NUM_SYNCS:
		query->end_result = rctx->tc ? rctx->tc->num_syncs : 0;
		break;
	case R600_QUERY_REQUESTED_VRAM:
	case R600_QUERY_REQUESTED_GTT:
	case R600_QUERY_MAPPED_VRAM:
	case R600_QUERY_MAPPED_GTT:
	case R600_QUERY_VRAM_USAGE:
	case R600_QUERY_VRAM_VIS_USAGE:
	case R600_QUERY_GTT_USAGE:
	case R600_QUERY_GPU_TEMPERATURE:
	case R600_QUERY_CURRENT_GPU_SCLK:
	case R600_QUERY_CURRENT_GPU_MCLK:
	case R600_QUERY_BUFFER_WAIT_TIME:
	case R600_QUERY_NUM_MAPPED_BUFFERS:
	case R600_QUERY_NUM_GFX_IBS:
	case R600_QUERY_NUM_SDMA_IBS:
	case R600_QUERY_NUM_BYTES_MOVED:
	case R600_QUERY_NUM_EVICTIONS:
	case R600_QUERY_NUM_VRAM_CPU_PAGE_FAULTS: {
		enum radeon_value_id ws_id = winsys_id_from_type(query->b.type);
		query->end_result = rctx->ws->query_value(rctx->ws, ws_id);
		break;
	}
	case R600_QUERY_GFX_BO_LIST_SIZE:
		ws_id = winsys_id_from_type(query->b.type);
		query->end_result = rctx->ws->query_value(rctx->ws, ws_id);
		query->end_time = rctx->ws->query_value(rctx->ws,
							RADEON_NUM_GFX_IBS);
		break;
	case R600_QUERY_CS_THREAD_BUSY:
		ws_id = winsys_id_from_type(query->b.type);
		query->end_result = rctx->ws->query_value(rctx->ws, ws_id);
		query->end_time = os_time_get_nano();
		break;
	case R600_QUERY_GALLIUM_THREAD_BUSY:
		query->end_result =
			rctx->tc ? util_queue_get_thread_time_nano(&rctx->tc->queue, 0) : 0;
		query->end_time = os_time_get_nano();
		break;
	case R600_QUERY_GPU_LOAD:
	case R600_QUERY_GPU_SHADERS_BUSY:
	case R600_QUERY_GPU_TA_BUSY:
	case R600_QUERY_GPU_GDS_BUSY:
	case R600_QUERY_GPU_VGT_BUSY:
	case R600_QUERY_GPU_IA_BUSY:
	case R600_QUERY_GPU_SX_BUSY:
	case R600_QUERY_GPU_WD_BUSY:
	case R600_QUERY_GPU_BCI_BUSY:
	case R600_QUERY_GPU_SC_BUSY:
	case R600_QUERY_GPU_PA_BUSY:
	case R600_QUERY_GPU_DB_BUSY:
	case R600_QUERY_GPU_CP_BUSY:
	case R600_QUERY_GPU_CB_BUSY:
	case R600_QUERY_GPU_SDMA_BUSY:
	case R600_QUERY_GPU_PFP_BUSY:
	case R600_QUERY_GPU_MEQ_BUSY:
	case R600_QUERY_GPU_ME_BUSY:
	case R600_QUERY_GPU_SURF_SYNC_BUSY:
	case R600_QUERY_GPU_CP_DMA_BUSY:
	case R600_QUERY_GPU_SCRATCH_RAM_BUSY:
		query->end_result = r600_end_counter(rctx->screen,
						     query->b.type,
						     query->begin_result);
		query->begin_result = 0;
		break;
	case R600_QUERY_NUM_COMPILATIONS:
		query->end_result = p_atomic_read(&rctx->screen->num_compilations);
		break;
	case R600_QUERY_NUM_SHADERS_CREATED:
		query->end_result = p_atomic_read(&rctx->screen->num_shaders_created);
		break;
	case R600_QUERY_NUM_SHADER_CACHE_HITS:
		query->end_result =
			p_atomic_read(&rctx->screen->num_shader_cache_hits);
		break;
	case R600_QUERY_GPIN_ASIC_ID:
	case R600_QUERY_GPIN_NUM_SIMD:
	case R600_QUERY_GPIN_NUM_RB:
	case R600_QUERY_GPIN_NUM_SPI:
	case R600_QUERY_GPIN_NUM_SE:
		break;
	default:
		UNREACHABLE("r600_query_sw_end: bad query type");
	}

	return true;
}

static bool r600_query_sw_get_result(struct r600_common_context *rctx,
				     struct r600_query *rquery,
				     bool wait,
				     union pipe_query_result *result)
{
	struct r600_query_sw *query = (struct r600_query_sw *)rquery;

	switch (query->b.type) {
	case PIPE_QUERY_TIMESTAMP_DISJOINT:
		/* Convert from cycles per millisecond to cycles per second (Hz). */
		result->timestamp_disjoint.frequency =
			(uint64_t)rctx->screen->info.clock_crystal_freq * 1000;
		result->timestamp_disjoint.disjoint = false;
		return true;
	case PIPE_QUERY_GPU_FINISHED: {
		struct pipe_screen *screen = rctx->b.screen;
		struct pipe_context *ctx = rquery->b.flushed ? NULL : &rctx->b;

		result->b = screen->fence_finish(screen, ctx, query->fence,
						 wait ? OS_TIMEOUT_INFINITE : 0);
		return result->b;
	}

	case R600_QUERY_GFX_BO_LIST_SIZE:
		result->u64 = (query->end_result - query->begin_result) /
			      (query->end_time - query->begin_time);
		return true;
	case R600_QUERY_CS_THREAD_BUSY:
	case R600_QUERY_GALLIUM_THREAD_BUSY:
		result->u64 = (query->end_result - query->begin_result) * 100 /
			      (query->end_time - query->begin_time);
		return true;
	case R600_QUERY_GPIN_ASIC_ID:
		result->u32 = 0;
		return true;
	case R600_QUERY_GPIN_NUM_SIMD:
		result->u32 = rctx->screen->info.num_cu;
		return true;
	case R600_QUERY_GPIN_NUM_RB:
		result->u32 = rctx->screen->info.max_render_backends;
		return true;
	case R600_QUERY_GPIN_NUM_SPI:
		result->u32 = 1; /* all supported chips have one SPI per SE */
		return true;
	case R600_QUERY_GPIN_NUM_SE:
		result->u32 = rctx->screen->info.max_se;
		return true;
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

	return true;
}


static struct r600_query_ops sw_query_ops = {
	.destroy = r600_query_sw_destroy,
	.begin = r600_query_sw_begin,
	.end = r600_query_sw_end,
	.get_result = r600_query_sw_get_result,
	.get_result_resource = NULL
};

static struct pipe_query *r600_query_sw_create(unsigned query_type)
{
	struct r600_query_sw *query;

	query = CALLOC_STRUCT(r600_query_sw);
	if (!query)
		return NULL;

	query->b.type = query_type;
	query->b.ops = &sw_query_ops;

	return (struct pipe_query *)query;
}

void r600_query_hw_destroy(struct r600_common_screen *rscreen,
			   struct r600_query *rquery)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;
	struct r600_query_buffer *prev = query->buffer.previous;

	/* Release all query buffers. */
	while (prev) {
		struct r600_query_buffer *qbuf = prev;
		prev = prev->previous;
		r600_resource_reference(&qbuf->buf, NULL);
		FREE(qbuf);
	}

	r600_resource_reference(&query->buffer.buf, NULL);
	FREE(rquery);
}

static struct r600_resource *r600_new_query_buffer(struct r600_common_screen *rscreen,
						   struct r600_query_hw *query)
{
	unsigned buf_size = MAX2(query->result_size,
				 rscreen->info.min_alloc_size);

	/* Queries are normally read by the CPU after
	 * being written by the gpu, hence staging is probably a good
	 * usage pattern.
	 */
	struct r600_resource *buf =
		r600_as_resource(pipe_buffer_create(&rscreen->b, 0,
						 PIPE_USAGE_STAGING, buf_size));
	if (!buf)
		return NULL;

	if (!query->ops->prepare_buffer(rscreen, query, buf)) {
		r600_resource_reference(&buf, NULL);
		return NULL;
	}

	return buf;
}

static bool r600_query_hw_prepare_buffer(struct r600_common_screen *rscreen,
					 struct r600_query_hw *query,
					 struct r600_resource *buffer)
{
	/* Callers ensure that the buffer is currently unused by the GPU. */
	uint32_t *results = rscreen->ws->buffer_map(rscreen->ws, buffer->buf, NULL,
						   PIPE_MAP_WRITE |
						   PIPE_MAP_UNSYNCHRONIZED);
	if (!results)
		return false;

	memset(results, 0, buffer->b.b.width0);

	if (query->b.type == PIPE_QUERY_OCCLUSION_COUNTER ||
	    query->b.type == PIPE_QUERY_OCCLUSION_PREDICATE ||
	    query->b.type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
		unsigned max_rbs = rscreen->info.max_render_backends;
		unsigned enabled_rb_mask = rscreen->info.enabled_rb_mask;
		unsigned num_results;
		unsigned i, j;

		/* Set top bits for unused backends. */
		num_results = buffer->b.b.width0 / query->result_size;
		for (j = 0; j < num_results; j++) {
			for (i = 0; i < max_rbs; i++) {
				if (!(enabled_rb_mask & (1<<i))) {
					results[(i * 4)+1] = 0x80000000;
					results[(i * 4)+3] = 0x80000000;
				}
			}
			results += 4 * max_rbs;
		}
	}

	return true;
}

static void r600_query_hw_get_result_resource(struct r600_common_context *rctx,
                                              struct r600_query *rquery,
                                              enum pipe_query_flags flags,
                                              enum pipe_query_value_type result_type,
                                              int index,
                                              struct pipe_resource *resource,
                                              unsigned offset);

static struct r600_query_ops query_hw_ops = {
	.destroy = r600_query_hw_destroy,
	.begin = r600_query_hw_begin,
	.end = r600_query_hw_end,
	.get_result = r600_query_hw_get_result,
	.get_result_resource = r600_query_hw_get_result_resource,
};

static void r600_query_hw_do_emit_start(struct r600_common_context *ctx,
					struct r600_query_hw *query,
					struct r600_resource *buffer,
					uint64_t va);
static void r600_query_hw_do_emit_stop(struct r600_common_context *ctx,
				       struct r600_query_hw *query,
				       struct r600_resource *buffer,
				       uint64_t va);
static void r600_query_hw_add_result(struct r600_common_screen *rscreen,
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

bool r600_query_hw_init(struct r600_common_screen *rscreen,
			struct r600_query_hw *query)
{
	query->buffer.buf = r600_new_query_buffer(rscreen, query);
	if (!query->buffer.buf)
		return false;

	return true;
}

static struct pipe_query *r600_query_hw_create(struct r600_common_screen *rscreen,
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
  	case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
		query->result_size = 16 * rscreen->info.max_render_backends;
		query->result_size += 16; /* for the fence + alignment */
		query->num_cs_dw_begin = 6;
		query->num_cs_dw_end = 6 + r600_gfx_write_fence_dwords(rscreen);
		break;
	case PIPE_QUERY_TIME_ELAPSED:
		query->result_size = 24;
		query->num_cs_dw_begin = 8;
		query->num_cs_dw_end = 8 + r600_gfx_write_fence_dwords(rscreen);
		break;
	case PIPE_QUERY_TIMESTAMP:
		query->result_size = 16;
		query->num_cs_dw_end = 8 + r600_gfx_write_fence_dwords(rscreen);
		query->flags = R600_QUERY_HW_FLAG_NO_START;
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
		break;
	case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
		/* NumPrimitivesWritten, PrimitiveStorageNeeded. */
		query->result_size = 32 * R600_MAX_STREAMS;
		query->num_cs_dw_begin = 6 * R600_MAX_STREAMS;
		query->num_cs_dw_end = 6 * R600_MAX_STREAMS;
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
		/* 11 values on EG, 8 on R600. */
		query->result_size = (rscreen->gfx_level >= EVERGREEN ? 11 : 8) * 16;
		query->result_size += 8; /* for the fence + alignment */
		query->num_cs_dw_begin = 6;
		query->num_cs_dw_end = 6 + r600_gfx_write_fence_dwords(rscreen);
		break;
	default:
		assert(0);
		FREE(query);
		return NULL;
	}

	if (!r600_query_hw_init(rscreen, query)) {
		FREE(query);
		return NULL;
	}

	return (struct pipe_query *)query;
}

static void r600_update_occlusion_query_state(struct r600_common_context *rctx,
					      unsigned type, int diff)
{
	if (type == PIPE_QUERY_OCCLUSION_COUNTER ||
	    type == PIPE_QUERY_OCCLUSION_PREDICATE ||
	    type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
		bool old_enable = rctx->num_occlusion_queries != 0;
		bool old_perfect_enable =
			rctx->num_perfect_occlusion_queries != 0;
		bool enable, perfect_enable;

		rctx->num_occlusion_queries += diff;
		assert(rctx->num_occlusion_queries >= 0);

		if (type != PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
			rctx->num_perfect_occlusion_queries += diff;
			assert(rctx->num_perfect_occlusion_queries >= 0);
		}

		enable = rctx->num_occlusion_queries != 0;
		perfect_enable = rctx->num_perfect_occlusion_queries != 0;

		if (enable != old_enable || perfect_enable != old_perfect_enable) {
			struct r600_context *ctx = (struct r600_context*)rctx;
			r600_mark_atom_dirty(ctx, &ctx->db_misc_state.atom);
		}
	}
}

static unsigned event_type_for_stream(unsigned stream)
{
	switch (stream) {
	default:
	case 0: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS;
	case 1: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS1;
	case 2: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS2;
	case 3: return EVENT_TYPE_SAMPLE_STREAMOUTSTATS3;
	}
}

static void emit_sample_streamout(struct radeon_cmdbuf *cs, uint64_t va,
				  unsigned stream)
{
	radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
	radeon_emit(cs, EVENT_TYPE(event_type_for_stream(stream)) | EVENT_INDEX(3));
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);
}

static void r600_query_hw_do_emit_start(struct r600_common_context *ctx,
					struct r600_query_hw *query,
					struct r600_resource *buffer,
					uint64_t va)
{
	struct radeon_cmdbuf *cs = &ctx->gfx.cs;

	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
  	case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		break;
	case PIPE_QUERY_PRIMITIVES_EMITTED:
	case PIPE_QUERY_PRIMITIVES_GENERATED:
	case PIPE_QUERY_SO_STATISTICS:
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		emit_sample_streamout(cs, va, query->stream);
		break;
	case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
		for (unsigned stream = 0; stream < R600_MAX_STREAMS; ++stream) {
			emit_sample_streamout(cs, va + 32 * stream, stream);
			r600_emit_reloc(ctx, &ctx->gfx, query->buffer.buf, RADEON_USAGE_WRITE |
					RADEON_PRIO_QUERY, 0);
		}
		return;
	case PIPE_QUERY_TIME_ELAPSED:
		/* Write the timestamp after the last draw is done.
		 * (bottom-of-pipe)
		 */
		r600_gfx_write_event_eop(ctx, EVENT_TYPE_BOTTOM_OF_PIPE_TS,
					 0, EOP_DATA_SEL_TIMESTAMP,
					 NULL, va, 0, query->b.type);
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_SAMPLE_PIPELINESTAT) | EVENT_INDEX(2));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		break;
	default:
		assert(0);
	}
	r600_emit_reloc(ctx, &ctx->gfx, query->buffer.buf, RADEON_USAGE_WRITE |
			RADEON_PRIO_QUERY, 0);
}

static void r600_query_hw_emit_start(struct r600_common_context *ctx,
				     struct r600_query_hw *query)
{
	uint64_t va;

	if (!query->buffer.buf)
		return; // previous buffer allocation failure

	r600_update_occlusion_query_state(ctx, query->b.type, 1);
	r600_update_prims_generated_query_state(ctx, query->b.type, 1);

	ctx->need_gfx_cs_space(&ctx->b, query->num_cs_dw_begin + query->num_cs_dw_end,
			       true);

	/* Get a new query buffer if needed. */
	if (query->buffer.results_end + query->result_size > query->buffer.buf->b.b.width0) {
		struct r600_query_buffer *qbuf = MALLOC_STRUCT(r600_query_buffer);
		*qbuf = query->buffer;
		query->buffer.results_end = 0;
		query->buffer.previous = qbuf;
		query->buffer.buf = r600_new_query_buffer(ctx->screen, query);
		if (!query->buffer.buf)
			return;
	}

	/* emit begin query */
	va = query->buffer.buf->gpu_address + query->buffer.results_end;

	query->ops->emit_start(ctx, query, query->buffer.buf, va);

	ctx->num_cs_dw_queries_suspend += query->num_cs_dw_end;
}

static void r600_query_hw_do_emit_stop(struct r600_common_context *ctx,
				       struct r600_query_hw *query,
				       struct r600_resource *buffer,
				       uint64_t va)
{
	struct radeon_cmdbuf *cs = &ctx->gfx.cs;
	uint64_t fence_va = 0;

	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
	case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
		va += 8;
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);

		fence_va = va + ctx->screen->info.max_render_backends * 16 - 8;
		break;
	case PIPE_QUERY_PRIMITIVES_EMITTED:
	case PIPE_QUERY_PRIMITIVES_GENERATED:
	case PIPE_QUERY_SO_STATISTICS:
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		va += 16;
		emit_sample_streamout(cs, va, query->stream);
		break;
	case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
		va += 16;
		for (unsigned stream = 0; stream < R600_MAX_STREAMS; ++stream) {
			emit_sample_streamout(cs, va + 32 * stream, stream);
			r600_emit_reloc(ctx, &ctx->gfx, query->buffer.buf, RADEON_USAGE_WRITE |
					RADEON_PRIO_QUERY, 0);
		}
		return;
	case PIPE_QUERY_TIME_ELAPSED:
		va += 8;
		FALLTHROUGH;
	case PIPE_QUERY_TIMESTAMP:
		r600_gfx_write_event_eop(ctx, EVENT_TYPE_BOTTOM_OF_PIPE_TS,
					 0, EOP_DATA_SEL_TIMESTAMP, NULL, va,
					 0, query->b.type);
		fence_va = va + 8;
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS: {
		unsigned sample_size = (query->result_size - 8) / 2;

		va += sample_size;
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_SAMPLE_PIPELINESTAT) | EVENT_INDEX(2));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);

		fence_va = va + sample_size;
		break;
	}
	default:
		assert(0);
	}
	r600_emit_reloc(ctx, &ctx->gfx, query->buffer.buf, RADEON_USAGE_WRITE |
			RADEON_PRIO_QUERY, 0);

	if (fence_va)
		r600_gfx_write_event_eop(ctx, EVENT_TYPE_BOTTOM_OF_PIPE_TS, 0,
					 EOP_DATA_SEL_VALUE_32BIT,
					 query->buffer.buf, fence_va, 0x80000000,
					 query->b.type);
}

static void r600_query_hw_emit_stop(struct r600_common_context *ctx,
				    struct r600_query_hw *query)
{
	uint64_t va;

	if (!query->buffer.buf)
		return; // previous buffer allocation failure

	/* The queries which need begin already called this in begin_query. */
	if (query->flags & R600_QUERY_HW_FLAG_NO_START) {
		ctx->need_gfx_cs_space(&ctx->b, query->num_cs_dw_end, false);
	}

	/* emit end query */
	va = query->buffer.buf->gpu_address + query->buffer.results_end;

	query->ops->emit_stop(ctx, query, query->buffer.buf, va);

	query->buffer.results_end += query->result_size;

	if (!(query->flags & R600_QUERY_HW_FLAG_NO_START))
		ctx->num_cs_dw_queries_suspend -= query->num_cs_dw_end;

	r600_update_occlusion_query_state(ctx, query->b.type, -1);
	r600_update_prims_generated_query_state(ctx, query->b.type, -1);
}

static void emit_set_predicate(struct r600_common_context *ctx,
			       struct r600_resource *buf, uint64_t va,
			       uint32_t op)
{
	struct radeon_cmdbuf *cs = &ctx->gfx.cs;

	radeon_emit(cs, PKT3(PKT3_SET_PREDICATION, 1, 0));
	radeon_emit(cs, va);
	radeon_emit(cs, op | ((va >> 32) & 0xFF));
	r600_emit_reloc(ctx, &ctx->gfx, buf, RADEON_USAGE_READ |
			RADEON_PRIO_QUERY, 0);
}

static void r600_emit_query_predication(struct r600_common_context *ctx,
					struct r600_atom *atom)
{
	struct r600_query_hw *query = (struct r600_query_hw *)ctx->render_cond;
	struct r600_query_buffer *qbuf;
	uint32_t op;
	bool flag_wait, invert;

	if (!query)
		return;

	invert = ctx->render_cond_invert;
	flag_wait = ctx->render_cond_mode == PIPE_RENDER_COND_WAIT ||
		    ctx->render_cond_mode == PIPE_RENDER_COND_BY_REGION_WAIT;

	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
	case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
		op = PRED_OP(PREDICATION_OP_ZPASS);
		break;
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
	case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
		op = PRED_OP(PREDICATION_OP_PRIMCOUNT);
		invert = !invert;
		break;
	default:
		assert(0);
		return;
	}

	/* if true then invert, see GL_ARB_conditional_render_inverted */
	if (invert)
		op |= PREDICATION_DRAW_NOT_VISIBLE; /* Draw if not visible or overflow */
	else
		op |= PREDICATION_DRAW_VISIBLE; /* Draw if visible or no overflow */

	op |= flag_wait ? PREDICATION_HINT_WAIT : PREDICATION_HINT_NOWAIT_DRAW;

	/* emit predicate packets for all data blocks */
	for (qbuf = &query->buffer; qbuf; qbuf = qbuf->previous) {
		unsigned results_base = 0;
		uint64_t va_base = qbuf->buf->gpu_address;

		while (results_base < qbuf->results_end) {
			uint64_t va = va_base + results_base;

			if (query->b.type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
				for (unsigned stream = 0; stream < R600_MAX_STREAMS; ++stream) {
					emit_set_predicate(ctx, qbuf->buf, va + 32 * stream, op);

					/* set CONTINUE bit for all packets except the first */
					op |= PREDICATION_CONTINUE;
				}
			} else {
				emit_set_predicate(ctx, qbuf->buf, va, op);
				op |= PREDICATION_CONTINUE;
			}

			results_base += query->result_size;
		}
	}
}

static struct pipe_query *r600_create_query(struct pipe_context *ctx, unsigned query_type, unsigned index)
{
	struct r600_common_screen *rscreen =
		(struct r600_common_screen *)ctx->screen;

	if (query_type == PIPE_QUERY_TIMESTAMP_DISJOINT ||
	    query_type == PIPE_QUERY_GPU_FINISHED ||
	    query_type >= PIPE_QUERY_DRIVER_SPECIFIC)
		return r600_query_sw_create(query_type);

	return r600_query_hw_create(rscreen, query_type, index);
}

static void r600_destroy_query(struct pipe_context *ctx, struct pipe_query *query)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	rquery->ops->destroy(rctx->screen, rquery);
}

static bool r600_begin_query(struct pipe_context *ctx,
			     struct pipe_query *query)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	return rquery->ops->begin(rctx, rquery);
}

void r600_query_hw_reset_buffers(struct r600_common_context *rctx,
				 struct r600_query_hw *query)
{
	struct r600_query_buffer *prev = query->buffer.previous;

	/* Discard the old query buffers. */
	while (prev) {
		struct r600_query_buffer *qbuf = prev;
		prev = prev->previous;
		r600_resource_reference(&qbuf->buf, NULL);
		FREE(qbuf);
	}

	query->buffer.results_end = 0;
	query->buffer.previous = NULL;

	/* Obtain a new buffer if the current one can't be mapped without a stall. */
	if (r600_rings_is_buffer_referenced(rctx, query->buffer.buf->buf, RADEON_USAGE_READWRITE) ||
	    !rctx->ws->buffer_wait(rctx->ws, query->buffer.buf->buf, 0, RADEON_USAGE_READWRITE)) {
		r600_resource_reference(&query->buffer.buf, NULL);
		query->buffer.buf = r600_new_query_buffer(rctx->screen, query);
	} else {
		if (!query->ops->prepare_buffer(rctx->screen, query, query->buffer.buf))
			r600_resource_reference(&query->buffer.buf, NULL);
	}
}

bool r600_query_hw_begin(struct r600_common_context *rctx,
			 struct r600_query *rquery)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;

	if (query->flags & R600_QUERY_HW_FLAG_NO_START) {
		assert(0);
		return false;
	}

	if (!(query->flags & R600_QUERY_HW_FLAG_BEGIN_RESUMES))
		r600_query_hw_reset_buffers(rctx, query);

	r600_query_hw_emit_start(rctx, query);
	if (!query->buffer.buf)
		return false;

	list_addtail(&query->list, &rctx->active_queries);
	return true;
}

static bool r600_end_query(struct pipe_context *ctx, struct pipe_query *query)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	return rquery->ops->end(rctx, rquery);
}

bool r600_query_hw_end(struct r600_common_context *rctx,
		       struct r600_query *rquery)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;

	if (query->flags & R600_QUERY_HW_FLAG_NO_START)
		r600_query_hw_reset_buffers(rctx, query);

	r600_query_hw_emit_stop(rctx, query);

	if (!(query->flags & R600_QUERY_HW_FLAG_NO_START))
		list_delinit(&query->list);

	if (!query->buffer.buf)
		return false;

	return true;
}

static void r600_get_hw_query_params(struct r600_common_context *rctx,
				     struct r600_query_hw *rquery, int index,
				     struct r600_hw_query_params *params)
{
	unsigned max_rbs = rctx->screen->info.max_render_backends;

	params->pair_stride = 0;
	params->pair_count = 1;

	switch (rquery->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER:
	case PIPE_QUERY_OCCLUSION_PREDICATE:
	case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
		params->start_offset = 0;
		params->end_offset = 8;
		params->fence_offset = max_rbs * 16;
		params->pair_stride = 16;
		params->pair_count = max_rbs;
		break;
	case PIPE_QUERY_TIME_ELAPSED:
		params->start_offset = 0;
		params->end_offset = 8;
		params->fence_offset = 16;
		break;
	case PIPE_QUERY_TIMESTAMP:
		params->start_offset = 0;
		params->end_offset = 0;
		params->fence_offset = 8;
		break;
	case PIPE_QUERY_PRIMITIVES_EMITTED:
		params->start_offset = 8;
		params->end_offset = 24;
		params->fence_offset = params->end_offset + 4;
		break;
	case PIPE_QUERY_PRIMITIVES_GENERATED:
		params->start_offset = 0;
		params->end_offset = 16;
		params->fence_offset = params->end_offset + 4;
		break;
	case PIPE_QUERY_SO_STATISTICS:
		params->start_offset = 8 - index * 8;
		params->end_offset = 24 - index * 8;
		params->fence_offset = params->end_offset + 4;
		break;
	case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
		params->pair_count = R600_MAX_STREAMS;
		params->pair_stride = 32;
		FALLTHROUGH;
	case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
		params->start_offset = 0;
		params->end_offset = 16;

		/* We can reuse the high dword of the last 64-bit value as a
		 * fence: it is initialized as 0, and the high bit is set by
		 * the write of the streamout stats event.
		 */
		params->fence_offset = rquery->result_size - 4;
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
	{
		/* Offsets apply to EG+ */
		static const unsigned offsets[] = {56, 48, 24, 32, 40, 16, 8, 0, 64, 72, 80};
		params->start_offset = offsets[index];
		params->end_offset = 88 + offsets[index];
		params->fence_offset = 2 * 88;
		break;
	}
	default:
		UNREACHABLE("r600_get_hw_query_params unsupported");
	}
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

static void r600_query_hw_add_result(struct r600_common_screen *rscreen,
				     struct r600_query_hw *query,
				     void *buffer,
				     union pipe_query_result *result)
{
	unsigned max_rbs = rscreen->info.max_render_backends;

	switch (query->b.type) {
	case PIPE_QUERY_OCCLUSION_COUNTER: {
		for (unsigned i = 0; i < max_rbs; ++i) {
			unsigned results_base = i * 16;
			result->u64 +=
				r600_query_read_result(buffer + results_base, 0, 2, true);
		}
		break;
	}
	case PIPE_QUERY_OCCLUSION_PREDICATE:
	case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE: {
		for (unsigned i = 0; i < max_rbs; ++i) {
			unsigned results_base = i * 16;
			result->b = result->b ||
				r600_query_read_result(buffer + results_base, 0, 2, true) != 0;
		}
		break;
	}
	case PIPE_QUERY_TIME_ELAPSED:
		result->u64 += r600_query_read_result(buffer, 0, 2, false);
		break;
	case PIPE_QUERY_TIMESTAMP:
		result->u64 = *(uint64_t*)buffer;
		break;
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
	case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
		for (unsigned stream = 0; stream < R600_MAX_STREAMS; ++stream) {
			result->b = result->b ||
				r600_query_read_result(buffer, 2, 6, true) !=
				r600_query_read_result(buffer, 0, 4, true);
			buffer = (char *)buffer + 32;
		}
		break;
	case PIPE_QUERY_PIPELINE_STATISTICS:
		if (rscreen->gfx_level >= EVERGREEN) {
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

static bool r600_get_query_result(struct pipe_context *ctx,
				  struct pipe_query *query, bool wait,
				  union pipe_query_result *result)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	return rquery->ops->get_result(rctx, rquery, wait, result);
}

static void r600_get_query_result_resource(struct pipe_context *ctx,
                                           struct pipe_query *query,
                                           enum pipe_query_flags flags,
                                           enum pipe_query_value_type result_type,
                                           int index,
                                           struct pipe_resource *resource,
                                           unsigned offset)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query *rquery = (struct r600_query *)query;

	rquery->ops->get_result_resource(rctx, rquery, flags, result_type, index,
	                                 resource, offset);
}

static void r600_query_hw_clear_result(struct r600_query_hw *query,
				       union pipe_query_result *result)
{
	util_query_clear_result(result, query->b.type);
}

bool r600_query_hw_get_result(struct r600_common_context *rctx,
			      struct r600_query *rquery,
			      bool wait, union pipe_query_result *result)
{
	struct r600_common_screen *rscreen = rctx->screen;
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;
	struct r600_query_buffer *qbuf;

	query->ops->clear_result(query, result);

	for (qbuf = &query->buffer; qbuf; qbuf = qbuf->previous) {
		unsigned usage = PIPE_MAP_READ |
				 (wait ? 0 : PIPE_MAP_DONTBLOCK);
		unsigned results_base = 0;
		void *map;

		if (rquery->b.flushed)
			map = rctx->ws->buffer_map(rctx->ws, qbuf->buf->buf, NULL, usage);
		else
			map = r600_buffer_map_sync_with_rings(rctx, qbuf->buf, usage);

		if (!map)
			return false;

		while (results_base != qbuf->results_end) {
			query->ops->add_result(rscreen, query, map + results_base,
					       result);
			results_base += query->result_size;
		}
	}

	/* Convert the time to expected units. */
	if (rquery->type == PIPE_QUERY_TIME_ELAPSED ||
	    rquery->type == PIPE_QUERY_TIMESTAMP) {
		result->u64 = (1000000 * result->u64) / rscreen->info.clock_crystal_freq;
	}
	return true;
}

enum r600_query_result_shader_config {
	R600_QUERY_READ_PREV = 1,
	R600_QUERY_WRITE_CHAIN = 2,
	R600_QUERY_WRITE_AVAIL = 4,
	R600_QUERY_BOOL = 8,
	R600_QUERY_ONE_DWORD = 16,
	R600_QUERY_TIMESTAMP = 32,
	R600_QUERY_RESULT64 = 64,
	R600_QUERY_SIGNED32 = 128,
	R600_QUERY_SO_OVERFLOW = 256,
};

struct r600_query_result_accum_vars {
	nir_variable *accum;
	nir_variable *result_available;
};

#define QUERY_RESULT_BUF nir_imm_int(b, 0)
#define QUERY_PREV_SUMMARY_BUF nir_imm_int(b, 1)
#define NEXT_SUMMARY_OR_USER_BUF nir_imm_int(b, 2)

static void r600_query_result_load_one_dword(nir_builder *b, nir_def *fence_offset,
					      nir_def *result_offset,
					      const struct r600_query_result_accum_vars *vars)
{
	nir_def *fence_value = nir_load_ssbo(b, 1, 32, QUERY_RESULT_BUF,
				      nir_iadd(b, fence_offset, result_offset));
	nir_def *is_fence_set = nir_ilt_imm(b, fence_value, 0);
	nir_store_var(b, vars->result_available, nir_inot(b, is_fence_set), 0x1);
	nir_def *result = nir_load_ssbo(b, 2, 32, QUERY_RESULT_BUF, result_offset);
	result = nir_bcsel(b, is_fence_set, nir_pack_64_2x32(b, result), nir_imm_int64(b, 0));
	nir_store_var(b, vars->accum, result, 0x1);
}

static void r600_query_result_accumulate_pairs(nir_builder *b, nir_function_impl *impl,
					       nir_def *config_bitfield,
					       nir_def *result_count,
					       nir_def *result_stride,
					       nir_def *fence_offset,
					       nir_def *pair_stride,
					       nir_def *pair_count,
					       nir_def *end_offset,
					       nir_def *result_offset,
					       const struct r600_query_result_accum_vars *vars)
{
	nir_def *_0i = nir_imm_int(b, 0);

	nir_store_var(b, vars->accum, nir_imm_int64(b, 0), 0x1);

	nir_def *cfg_prev_acc_result = nir_test_mask(b, config_bitfield, R600_QUERY_READ_PREV);
	nir_if *if_prev_acc_result = nir_push_if(b, cfg_prev_acc_result); {
		nir_def *result = nir_load_ssbo(b, 3, 32, QUERY_PREV_SUMMARY_BUF, _0i);
		nir_def *result_data = nir_channels(b, result, 3);
		nir_store_var(b, vars->accum, nir_pack_64_2x32(b, result_data), 0x1);
		nir_store_var(b, vars->result_available, nir_i2b(b, nir_channel(b, result, 2)), 0x1);
	}
	nir_pop_if(b, if_prev_acc_result);

	nir_def *availability_mask_value = nir_load_var(b, vars->result_available);
	nir_if *if_not_z = nir_push_if(b, nir_inot(b, availability_mask_value)); {

		/* Accumulate query result pairs across result buffers:
		 * for (result_index = 0; result_index < result_count; ++result_index) {
		 *   fence = load_fence(result_index)
		 *   if (!fence.valid) break  // Stop when we hit an invalid fence
		 *   for (pair_index = 0; pair_index < pair_count; ++pair_index) {
		 *     pair_start = load64(result_offset + pair_index * pair_stride)
		 *     pair_end = load64(result_offset + pair_index * pair_stride + end_offset)
		 *     accumulator += (pair_end - pair_start)
		 *     if (SO_OVERFLOW_mode) {
		 *       fence_start = load64(...)
		 *       fence_end = load64(...)
		 *       accumulator -= (fence_end - fence_start)
		 *     }
		 *   }
		 * }
		 */

		nir_variable *outer_loop_iter =
			nir_local_variable_create(impl, glsl_uint_type(), "outer_loop_iter");
		nir_variable *inner_loop_iter =
			nir_local_variable_create(impl, glsl_uint_type(), "inner_loop_iter");

		nir_store_var(b, outer_loop_iter, _0i, 0x1);
		nir_loop *loop_outer = nir_push_loop(b); {
			nir_def *result_index = nir_load_var(b, outer_loop_iter);

			nir_break_if(b, nir_uge(b, result_index, result_count));

			nir_def *pitch_outer_loop = nir_imul(b, result_index, result_stride);
			nir_def *address = nir_iadd(b, pitch_outer_loop,
						nir_iadd(b, fence_offset,
							 result_offset));
			nir_def *value = nir_load_ssbo(b, 1, 32, QUERY_RESULT_BUF, address);
			nir_def *fence_not_set = nir_inot(b, nir_ilt_imm(b, value, 0));
			nir_store_var(b, vars->result_available, fence_not_set, 0x1);

			nir_break_if(b, fence_not_set);

			nir_store_var(b, inner_loop_iter, _0i, 0x1);
			nir_loop *loop_inner = nir_push_loop(b); {
				nir_def *i = nir_load_var(b, inner_loop_iter);
				nir_def *pitch_inner_loop = nir_imul(b, i, pair_stride);
				nir_def *base =
					nir_iadd(b, pitch_outer_loop,
						 nir_iadd(b,  pitch_inner_loop,
								result_offset));
				nir_def *end_base = nir_iadd(b, base, end_offset);
				nir_def *first_result = nir_load_ssbo(b, 1, 64, QUERY_RESULT_BUF, base);
				nir_def *second_result = nir_load_ssbo(b, 1, 64, QUERY_RESULT_BUF,
													end_base);
				nir_def *first_fence = nir_load_ssbo(b, 1, 64, QUERY_RESULT_BUF,
					      						nir_iadd_imm(b, base, 8));
				nir_def *second_fence = nir_load_ssbo(b, 1, 64, QUERY_RESULT_BUF,
				       							nir_iadd_imm(b, end_base, 8));
				nir_def *start_half_pair = nir_isub(b, second_result, first_result);

				nir_def *end_half_pair = nir_isub(b, second_fence, first_fence);
				nir_def *difference = nir_isub(b, start_half_pair, end_half_pair);

				nir_def *cfg_so_overflow_mode = nir_test_mask(b, config_bitfield, R600_QUERY_SO_OVERFLOW);
				nir_def *sum = nir_iadd(b, nir_load_var(b, vars->accum),
						 nir_bcsel(b, cfg_so_overflow_mode, difference, start_half_pair));

				i = nir_iadd_imm(b, i, 1);

				nir_store_var(b, inner_loop_iter, i, 0x1);
				nir_store_var(b, vars->accum, sum, 0x1);

				nir_def *is_pair_count_exceeded = nir_uge(b, i, pair_count);
				nir_break_if(b, is_pair_count_exceeded);
			}
			nir_pop_loop(b, loop_inner);

			nir_store_var(b, outer_loop_iter, nir_iadd_imm(b, result_index, 1), 0x1);
		}
		nir_pop_loop(b, loop_outer);
	}
	nir_pop_if(b, if_not_z);
}

static void r600_query_result_store_output(nir_builder *b,
					   struct r600_common_context *rctx,
					   nir_def *config_bitfield,
					   nir_def *result_offset,
					   const struct r600_query_result_accum_vars *vars)
{
	nir_def *_0i = nir_imm_int(b, 0);
	nir_def *accum_value = nir_load_var(b, vars->accum);
	nir_def *availability_value = nir_load_var(b, vars->result_available);

	/* Store query results to output buffer based on configuration:
	 * if (write_chain) {
	 *   // Store accumulated values for chaining to next grid invocation
	 *   output = (accum_lo, accum_hi, availability_mask)
	 * } else if (write_available) {
	 *   // Store query availability flag (!fence_valid) and optional 64-bit padding
	 *   output = (fence_valid ? 0 : 1)
	 *   if (is_64bit) output += padding_word
	 * } else {
	 *   // Store actual query result with optional conversions
	 *   if (apply_timestamp)
	 *     result = convert_to_timestamp(x, y)
	 *   if (convert_to_bool)
	 *     result = (result != 0) ? 1 : 0
	 *   if (is_64bit) {
	 *     output = (result_low, result_high)
	 *   } else {
	 *     output = result_low (clamped for signed mode)
	 *   }
	 * }
	 */
	nir_def *is_acc_chaining = nir_test_mask(b, config_bitfield, R600_QUERY_WRITE_CHAIN);
	nir_if *if_acc_chaining = nir_push_if(b, is_acc_chaining); {
		nir_def *accum_unpacked = nir_unpack_64_2x32(b, accum_value);
		nir_store_ssbo(b, nir_vec3(b, nir_channel(b, accum_unpacked, 0), nir_channel(b, accum_unpacked, 1), nir_b2i32(b, availability_value)), NEXT_SUMMARY_OR_USER_BUF,
		       result_offset);
	} nir_push_else(b, if_acc_chaining); {
		nir_def *cfg_write_result_available = nir_test_mask(b, config_bitfield, R600_QUERY_WRITE_AVAIL);
		nir_if *if_write_result_available = nir_push_if(b, cfg_write_result_available); {
			nir_store_ssbo(b,
			       nir_b2i32(b, nir_inot(b, availability_value)),
				       NEXT_SUMMARY_OR_USER_BUF, result_offset);

			nir_def *is_result_64_bits = nir_test_mask(b, config_bitfield, R600_QUERY_RESULT64);
			nir_if *if_result_64_bits = nir_push_if(b, is_result_64_bits); {
				nir_store_ssbo(b, _0i, NEXT_SUMMARY_OR_USER_BUF, nir_iadd_imm(b, result_offset, 4));
			}
			nir_pop_if(b, if_result_64_bits);
		} nir_push_else(b, if_write_result_available); {
			nir_def *result_pending = nir_inot(b, availability_value);
			nir_if *if_result_pending = nir_push_if(b, result_pending); {
				nir_def *cfg_apply_timestamp = nir_test_mask(b, config_bitfield, R600_QUERY_TIMESTAMP);
				nir_def *clock_crystal_frequency =
					nir_imm_int64(b, rctx->screen->info.clock_crystal_freq);
				nir_def *xy_million = nir_imul(b, accum_value, nir_imm_int64(b, 1000000));
				nir_def *ts_converted = nir_udiv(b, xy_million, clock_crystal_frequency);
				nir_def *n = nir_bcsel(b, cfg_apply_timestamp, ts_converted, accum_value);

				nir_def *cfg_convert_to_bool = nir_test_mask(b, config_bitfield, R600_QUERY_BOOL);
				n = nir_bcsel(b, cfg_convert_to_bool, nir_b2i64(b, nir_ine_imm(b, n, 0)), n);

				nir_def *cfg_result_64_bits = nir_test_mask(b, config_bitfield, R600_QUERY_RESULT64);
				nir_if *if_result_64_bits = nir_push_if(b, cfg_result_64_bits); {
					nir_store_ssbo(b, nir_unpack_64_2x32(b, n), NEXT_SUMMARY_OR_USER_BUF, result_offset);
				} nir_push_else(b, if_result_64_bits); {
					nir_def *nx = nir_unpack_64_2x32_split_x(b, n);
					nir_def *ny = nir_unpack_64_2x32_split_y(b, n);
					nir_def *is_y = nir_ine(b, ny, _0i);
					nx = nir_bcsel(b, is_y, nir_imm_int(b, UINT32_MAX), nx);
					nir_def *cfg_signed_32bit_result = nir_test_mask(b, config_bitfield, R600_QUERY_SIGNED32);
					nir_def *min = nir_umin(b, nx, nir_imm_int(b, INT_MAX));
					nx = nir_bcsel(b, cfg_signed_32bit_result, min, nx);
					nir_store_ssbo(b, nx, NEXT_SUMMARY_OR_USER_BUF, result_offset);
				}
				nir_pop_if(b, if_result_64_bits);
			}
			nir_pop_if(b, if_result_pending);
		}
		nir_pop_if(b, if_write_result_available);
	}
	nir_pop_if(b, if_acc_chaining);
}

/* Create the compute shader that is used to collect the results.
 *
 * One compute grid with a single thread is launched for every query result
 * buffer. The thread (optionally) reads a previous summary buffer, then
 * accumulates data from the query result buffer, and writes the result either
 * to a summary buffer to be consumed by the next grid invocation or to the
 * user-supplied buffer.
 *
 * Data layout:
 *
 * CONST
 *  0.x = end_offset
 *  0.y = result_stride
 *  0.z = result_count
 *  0.w = bit field:
 *          1: read previously accumulated values
 *          2: write accumulated values for chaining
 *          4: write result available
 *          8: convert result to boolean (0/1)
 *         16: only read one dword and use that as result
 *         32: apply timestamp conversion
 *         64: store full 64 bits result
 *        128: store signed 32 bits result
 *        256: SO_OVERFLOW mode: take the difference of two successive half-pairs
 *  1.x = fence_offset
 *  1.y = pair_stride
 *  1.z = pair_count
 *  1.w = result_offset
 *  2.x = buffer0 offset
 *
 * BUFFER[0] = query result buffer
 * BUFFER[1] = previous summary buffer
 * BUFFER[2] = next summary buffer or user-supplied buffer
 */
static void r600_create_query_result_shader(struct r600_common_context *rctx)
{
	const struct nir_shader_compiler_options *options =
		rctx->b.screen->nir_options[MESA_SHADER_COMPUTE];
	nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options,
						      "r600_create_query_result_cs");
	struct pipe_compute_state state = {};

	b.shader->info.workgroup_size[0] = 1;
	b.shader->info.workgroup_size[1] = 1;
	b.shader->info.workgroup_size[2] = 1;
	b.shader->info.num_ubos = 1;
	b.shader->info.num_ssbos = 3;
	b.shader->num_uniforms = 3;

	/* uint64_t accum = 0; bool availability_mask = false; */
	nir_function_impl *impl = nir_shader_get_entrypoint(b.shader);
	nir_variable *accum = nir_local_variable_create(impl, glsl_uint64_t_type(), "accum");
	nir_variable *availability_mask = nir_local_variable_create(impl, glsl_bool_type(), "availability_mask");
	nir_store_var(&b, availability_mask, nir_imm_false(&b), 0x1);
	const struct r600_query_result_accum_vars accum_vars = {accum, availability_mask};

	/* consts[0] = {end_offset, result_stride, result_count, config}
	 * consts[1] = {fence_offset, pair_stride, pair_count, buffer_offset}
	 * consts[2] = {buffer0_offset, 0, 0, 0}
	 */
	nir_def *default_ubo = nir_imm_int(&b, 0);
	nir_def *config_0 = nir_load_ubo(&b, 4, 32, default_ubo, nir_imm_int(&b, 0), .range_base = 0, .range = 16);
	nir_def *config_1 = nir_load_ubo(&b, 4, 32, default_ubo, nir_imm_int(&b, 16), .range_base = 16, .range = 16);
	nir_def *config_2 = nir_load_ubo(&b, 4, 32, default_ubo, nir_imm_int(&b, 32), .range_base = 32, .range = 16);

	/* Decode UBO payload into named fields to make the shader logic readable. */
	nir_def *end_offset = nir_channel(&b, config_0, 0);
	nir_def *result_stride = nir_channel(&b, config_0, 1);
	nir_def *result_count = nir_channel(&b, config_0, 2);
	nir_def *config_bitfield = nir_channel(&b, config_0, 3);
	nir_def *fence_offset = nir_channel(&b, config_1, 0);
	nir_def *pair_stride = nir_channel(&b, config_1, 1);
	nir_def *pair_count = nir_channel(&b, config_1, 2);
	nir_def *result_offset = nir_channel(&b, config_1, 3);
	nir_def *buffer0_offset = nir_channel(&b, config_2, 0);

	/* Check result availability for timestamp queries. */
	nir_def *is_one_dword_result =
		nir_test_mask(&b, config_bitfield, R600_QUERY_ONE_DWORD);
	nir_if *if_one_dword_result = nir_push_if(&b, is_one_dword_result); {
		r600_query_result_load_one_dword(&b, fence_offset, buffer0_offset, &accum_vars);
	} nir_push_else(&b, if_one_dword_result); {
		r600_query_result_accumulate_pairs(&b, impl, config_bitfield, result_count,
						  result_stride, fence_offset,
						  pair_stride, pair_count,
						  end_offset, buffer0_offset,
						  &accum_vars);
	}
	nir_pop_if(&b, if_one_dword_result);

	r600_query_result_store_output(&b, rctx, config_bitfield, result_offset, &accum_vars);

	rctx->b.screen->finalize_nir(rctx->b.screen, b.shader, true);

	state.ir_type = PIPE_SHADER_IR_NIR;
	state.prog = b.shader;



	rctx->query_result_shader = rctx->b.create_compute_state(&rctx->b, &state);
}

static void r600_restore_qbo_state(struct r600_common_context *rctx,
				   struct r600_qbo_state *st)
{
	rctx->b.bind_compute_state(&rctx->b, st->saved_compute);
	rctx->b.set_constant_buffer(&rctx->b, MESA_SHADER_COMPUTE, 0, &st->saved_const0);
	rctx->b.set_shader_buffers(&rctx->b, MESA_SHADER_COMPUTE, 0, 3, st->saved_ssbo, ~0);
	for (unsigned i = 0; i < 3; ++i)
		pipe_resource_reference(&st->saved_ssbo[i].buffer, NULL);
}

static void r600_query_hw_get_result_resource(struct r600_common_context *rctx,
                                              struct r600_query *rquery,
                                              enum pipe_query_flags flags,
                                              enum pipe_query_value_type result_type,
                                              int index,
                                              struct pipe_resource *resource,
                                              unsigned offset)
{
	struct r600_query_hw *query = (struct r600_query_hw *)rquery;
	struct r600_query_buffer *qbuf;
	struct r600_query_buffer *qbuf_prev;
	struct pipe_resource *tmp_buffer = NULL;
	unsigned tmp_buffer_offset = 0;
	struct r600_qbo_state saved_state = {};
	struct pipe_grid_info grid = {};
	struct pipe_constant_buffer constant_buffer = {};
	struct pipe_shader_buffer ssbo[3];
	struct r600_hw_query_params params;
	struct {
		uint32_t end_offset;
		uint32_t result_stride;
		uint32_t result_count;
		uint32_t config;
		uint32_t fence_offset;
		uint32_t pair_stride;
		uint32_t pair_count;
		uint32_t buffer_offset;
		uint32_t buffer0_offset;
		uint32_t pad[3];
	} consts;

	if (!rctx->query_result_shader) {
		r600_create_query_result_shader(rctx);
		if (!rctx->query_result_shader)
			return;
	}

	if (query->buffer.previous) {
		u_suballocator_alloc(&rctx->allocator_zeroed_memory, 16, 256,
				     &tmp_buffer_offset, &tmp_buffer);
		if (!tmp_buffer)
			return;
	}

	rctx->save_qbo_state(&rctx->b, &saved_state);

	r600_get_hw_query_params(rctx, query, index >= 0 ? index : 0, &params);
	consts.end_offset = params.end_offset - params.start_offset;
	consts.fence_offset = params.fence_offset - params.start_offset;
	consts.result_stride = query->result_size;
	consts.pair_stride = params.pair_stride;
	consts.pair_count = params.pair_count;

	constant_buffer.buffer_size = sizeof(consts);
	constant_buffer.user_buffer = &consts;

	ssbo[1].buffer = tmp_buffer;
	ssbo[1].buffer_offset = tmp_buffer_offset;
	ssbo[1].buffer_size = 16;

	ssbo[2] = ssbo[1];

	rctx->b.bind_compute_state(&rctx->b, rctx->query_result_shader);

	grid.block[0] = 1;
	grid.block[1] = 1;
	grid.block[2] = 1;
	grid.grid[0] = 1;
	grid.grid[1] = 1;
	grid.grid[2] = 1;

	consts.config = 0;
	if (index < 0)
		consts.config |= 4;
	if (query->b.type == PIPE_QUERY_OCCLUSION_PREDICATE ||
	    query->b.type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE)
		consts.config |= 8;
	else if (query->b.type == PIPE_QUERY_SO_OVERFLOW_PREDICATE ||
		 query->b.type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE)
		consts.config |= 8 | 256;
	else if (query->b.type == PIPE_QUERY_TIMESTAMP ||
		 query->b.type == PIPE_QUERY_TIME_ELAPSED)
		consts.config |= 32;

	switch (result_type) {
	case PIPE_QUERY_TYPE_U64:
	case PIPE_QUERY_TYPE_I64:
		consts.config |= 64;
		break;
	case PIPE_QUERY_TYPE_I32:
		consts.config |= 128;
		break;
	case PIPE_QUERY_TYPE_U32:
		break;
	}

	rctx->flags |= rctx->screen->barrier_flags.cp_to_L2;

	for (qbuf = &query->buffer; qbuf; qbuf = qbuf_prev) {
		if (query->b.type != PIPE_QUERY_TIMESTAMP) {
			qbuf_prev = qbuf->previous;
			consts.result_count = qbuf->results_end / query->result_size;
			consts.config &= ~3;
			if (qbuf != &query->buffer)
				consts.config |= 1;
			if (qbuf->previous)
				consts.config |= 2;
		} else {
			/* Only read the last timestamp. */
			qbuf_prev = NULL;
			consts.result_count = 0;
			consts.config |= 16;
			params.start_offset += qbuf->results_end - query->result_size;
		}

		ssbo[0].buffer = &qbuf->buf->b.b;
		ssbo[0].buffer_offset = params.start_offset & ~0xff;
		ssbo[0].buffer_size = qbuf->results_end - ssbo[0].buffer_offset;
		consts.buffer0_offset = (params.start_offset & 0xff);
		if (!qbuf->previous) {

			ssbo[2].buffer = resource;
			ssbo[2].buffer_offset = offset & ~0xff;
			ssbo[2].buffer_size = offset + 8;
			consts.buffer_offset = (offset & 0xff);
		} else
			consts.buffer_offset = 0;

		rctx->b.set_constant_buffer(&rctx->b, MESA_SHADER_COMPUTE, 0, &constant_buffer);

		rctx->b.set_shader_buffers(&rctx->b, MESA_SHADER_COMPUTE, 0, 3, ssbo, ~0);

		if ((flags & PIPE_QUERY_WAIT) && qbuf == &query->buffer) {
			uint64_t va;

			/* Wait for result availability. Wait only for readiness
			 * of the last entry, since the fence writes should be
			 * serialized in the CP.
			 */
			va = qbuf->buf->gpu_address + qbuf->results_end - query->result_size;
			va += params.fence_offset;

			r600_gfx_wait_fence(rctx, qbuf->buf, va, 0x80000000, 0x80000000);
		}

		rctx->b.launch_grid(&rctx->b, &grid);
		rctx->flags |= rctx->screen->barrier_flags.compute_to_L2;
	}

	r600_restore_qbo_state(rctx, &saved_state);
	pipe_resource_reference(&tmp_buffer, NULL);
}

static void r600_render_condition(struct pipe_context *ctx,
				  struct pipe_query *query,
				  bool condition,
				  enum pipe_render_cond_flag mode)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_query_hw *rquery = (struct r600_query_hw *)query;
	struct r600_query_buffer *qbuf;
	struct r600_atom *atom = &rctx->render_cond_atom;

	/* Compute the size of SET_PREDICATION packets. */
	atom->num_dw = 0;
	if (query) {
		for (qbuf = &rquery->buffer; qbuf; qbuf = qbuf->previous)
			atom->num_dw += (qbuf->results_end / rquery->result_size) * 5;

		if (rquery->b.type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE)
			atom->num_dw *= R600_MAX_STREAMS;
	}

	rctx->render_cond = query;
	rctx->render_cond_invert = condition;
	rctx->render_cond_mode = mode;

	rctx->set_atom_dirty(rctx, atom, query != NULL);
}

void r600_suspend_queries(struct r600_common_context *ctx)
{
	struct r600_query_hw *query;

	LIST_FOR_EACH_ENTRY(query, &ctx->active_queries, list) {
		r600_query_hw_emit_stop(ctx, query);
	}
	assert(ctx->num_cs_dw_queries_suspend == 0);
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

void r600_resume_queries(struct r600_common_context *ctx)
{
	struct r600_query_hw *query;
	unsigned num_cs_dw = r600_queries_num_cs_dw_for_resuming(ctx, &ctx->active_queries);

	assert(ctx->num_cs_dw_queries_suspend == 0);

	/* Check CS space here. Resuming must not be interrupted by flushes. */
	ctx->need_gfx_cs_space(&ctx->b, num_cs_dw, true);

	LIST_FOR_EACH_ENTRY(query, &ctx->active_queries, list) {
		r600_query_hw_emit_start(ctx, query);
	}
}

/* Fix radeon_info::enabled_rb_mask for R600, R700, EVERGREEN, NI. */
void r600_query_fix_enabled_rb_mask(struct r600_common_screen *rscreen)
{
	struct r600_common_context *ctx =
		(struct r600_common_context*)rscreen->aux_context;
	struct radeon_cmdbuf *cs = &ctx->gfx.cs;
	struct r600_resource *buffer;
	uint32_t *results;
	unsigned i, mask = 0;
	unsigned max_rbs;
	
	if (ctx->family == CHIP_JUNIPER) {
		/*
		 * Fix for predication lockups - the chip can only ever have
		 * 4 RBs, however it looks like the predication logic assumes
		 * there's 8, trying to read results from query buffers never
		 * written to. By increasing this number we'll write the
		 * status bit for these as per the normal disabled rb logic.
		 */
		ctx->screen->info.max_render_backends = 8;
	}
	max_rbs = ctx->screen->info.max_render_backends;

	assert(rscreen->gfx_level <= CAYMAN);

	/*
	 * if backend_map query is supported by the kernel.
	 * Note the kernel drm driver for a long time never filled in the
	 * associated data on eg/cm, only on r600/r700, hence ignore the valid
	 * bit there if the map is zero.
	 * (Albeit some chips with just one active rb can have a valid 0 map.)
	 */ 
	if (rscreen->info.r600_gb_backend_map_valid &&
	    (ctx->gfx_level < EVERGREEN || rscreen->info.r600_gb_backend_map != 0)) {
		unsigned num_tile_pipes = rscreen->info.num_tile_pipes;
		unsigned backend_map = rscreen->info.r600_gb_backend_map;
		unsigned item_width, item_mask;

		if (ctx->gfx_level >= EVERGREEN) {
			item_width = 4;
			item_mask = 0x7;
		} else {
			item_width = 2;
			item_mask = 0x3;
		}

		while (num_tile_pipes--) {
			i = backend_map & item_mask;
			mask |= (1<<i);
			backend_map >>= item_width;
		}
		if (mask != 0) {
			rscreen->info.enabled_rb_mask = mask;
			return;
		}
	}

	/* otherwise backup path for older kernels */

	/* create buffer for event data */
	buffer = r600_as_resource(pipe_buffer_create(ctx->b.screen, 0,
						  PIPE_USAGE_STAGING, max_rbs * 16));
	if (!buffer)
		return;

	/* initialize buffer with zeroes */
	results = r600_buffer_map_sync_with_rings(ctx, buffer, PIPE_MAP_WRITE);
	if (results) {
		memset(results, 0, max_rbs * 4 * 4);

		/* emit EVENT_WRITE for ZPASS_DONE */
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, buffer->gpu_address);
		radeon_emit(cs, buffer->gpu_address >> 32);

		r600_emit_reloc(ctx, &ctx->gfx, buffer,
                                RADEON_USAGE_WRITE | RADEON_PRIO_QUERY, 0);

		/* analyze results */
		results = r600_buffer_map_sync_with_rings(ctx, buffer, PIPE_MAP_READ);
		if (results) {
			for(i = 0; i < max_rbs; i++) {
				/* at least highest bit will be set if backend is used */
				if (results[i*4 + 1])
					mask |= (1<<i);
			}
		}
	}

	r600_resource_reference(&buffer, NULL);

	if (mask) {
		if (rscreen->debug_flags & DBG_INFO &&
		    mask != rscreen->info.enabled_rb_mask) {
			printf("enabled_rb_mask (fixed) = 0x%x\n", mask);
		}
		rscreen->info.enabled_rb_mask = mask;
	}
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

static const struct pipe_driver_query_info r600_driver_query_list[] = {
	X("num-compilations",		NUM_COMPILATIONS,	UINT64, CUMULATIVE),
	X("num-shaders-created",	NUM_SHADERS_CREATED,	UINT64, CUMULATIVE),
	X("num-shader-cache-hits",	NUM_SHADER_CACHE_HITS,	UINT64, CUMULATIVE),
	X("draw-calls",			DRAW_CALLS,		UINT64, AVERAGE),
	X("decompress-calls",		DECOMPRESS_CALLS,	UINT64, AVERAGE),
	X("MRT-draw-calls",		MRT_DRAW_CALLS,		UINT64, AVERAGE),
	X("prim-restart-calls",		PRIM_RESTART_CALLS,	UINT64, AVERAGE),
	X("spill-draw-calls",		SPILL_DRAW_CALLS,	UINT64, AVERAGE),
	X("compute-calls",		COMPUTE_CALLS,		UINT64, AVERAGE),
	X("spill-compute-calls",	SPILL_COMPUTE_CALLS,	UINT64, AVERAGE),
	X("dma-calls",			DMA_CALLS,		UINT64, AVERAGE),
	X("cp-dma-calls",		CP_DMA_CALLS,		UINT64, AVERAGE),
	X("num-vs-flushes",		NUM_VS_FLUSHES,		UINT64, AVERAGE),
	X("num-ps-flushes",		NUM_PS_FLUSHES,		UINT64, AVERAGE),
	X("num-cs-flushes",		NUM_CS_FLUSHES,		UINT64, AVERAGE),
	X("num-CB-cache-flushes",	NUM_CB_CACHE_FLUSHES,	UINT64, AVERAGE),
	X("num-DB-cache-flushes",	NUM_DB_CACHE_FLUSHES,	UINT64, AVERAGE),
	X("num-resident-handles",	NUM_RESIDENT_HANDLES,	UINT64, AVERAGE),
	X("tc-offloaded-slots",		TC_OFFLOADED_SLOTS,     UINT64, AVERAGE),
	X("tc-direct-slots",		TC_DIRECT_SLOTS,	UINT64, AVERAGE),
	X("tc-num-syncs",		TC_NUM_SYNCS,		UINT64, AVERAGE),
	X("CS-thread-busy",		CS_THREAD_BUSY,		UINT64, AVERAGE),
	X("gallium-thread-busy",	GALLIUM_THREAD_BUSY,	UINT64, AVERAGE),
	X("requested-VRAM",		REQUESTED_VRAM,		BYTES, AVERAGE),
	X("requested-GTT",		REQUESTED_GTT,		BYTES, AVERAGE),
	X("mapped-VRAM",		MAPPED_VRAM,		BYTES, AVERAGE),
	X("mapped-GTT",			MAPPED_GTT,		BYTES, AVERAGE),
	X("buffer-wait-time",		BUFFER_WAIT_TIME,	MICROSECONDS, CUMULATIVE),
	X("num-mapped-buffers",		NUM_MAPPED_BUFFERS,	UINT64, AVERAGE),
	X("num-GFX-IBs",		NUM_GFX_IBS,		UINT64, AVERAGE),
	X("num-SDMA-IBs",		NUM_SDMA_IBS,		UINT64, AVERAGE),
	X("GFX-BO-list-size",		GFX_BO_LIST_SIZE,	UINT64, AVERAGE),
	X("num-bytes-moved",		NUM_BYTES_MOVED,	BYTES, CUMULATIVE),
	X("num-evictions",		NUM_EVICTIONS,		UINT64, CUMULATIVE),
	X("VRAM-CPU-page-faults",	NUM_VRAM_CPU_PAGE_FAULTS, UINT64, CUMULATIVE),
	X("VRAM-usage",			VRAM_USAGE,		BYTES, AVERAGE),
	X("VRAM-vis-usage",		VRAM_VIS_USAGE,		BYTES, AVERAGE),
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

	X("temperature",		GPU_TEMPERATURE,	UINT64, AVERAGE),
	X("shader-clock",		CURRENT_GPU_SCLK,	HZ, AVERAGE),
	X("memory-clock",		CURRENT_GPU_MCLK,	HZ, AVERAGE),

	/* The following queries must be at the end of the list because their
	 * availability is adjusted dynamically based on the DRM version. */
	X("GPU-load",			GPU_LOAD,		UINT64, AVERAGE),
	X("GPU-shaders-busy",		GPU_SHADERS_BUSY,	UINT64, AVERAGE),
	X("GPU-ta-busy",		GPU_TA_BUSY,		UINT64, AVERAGE),
	X("GPU-gds-busy",		GPU_GDS_BUSY,		UINT64, AVERAGE),
	X("GPU-vgt-busy",		GPU_VGT_BUSY,		UINT64, AVERAGE),
	X("GPU-ia-busy",		GPU_IA_BUSY,		UINT64, AVERAGE),
	X("GPU-sx-busy",		GPU_SX_BUSY,		UINT64, AVERAGE),
	X("GPU-wd-busy",		GPU_WD_BUSY,		UINT64, AVERAGE),
	X("GPU-bci-busy",		GPU_BCI_BUSY,		UINT64, AVERAGE),
	X("GPU-sc-busy",		GPU_SC_BUSY,		UINT64, AVERAGE),
	X("GPU-pa-busy",		GPU_PA_BUSY,		UINT64, AVERAGE),
	X("GPU-db-busy",		GPU_DB_BUSY,		UINT64, AVERAGE),
	X("GPU-cp-busy",		GPU_CP_BUSY,		UINT64, AVERAGE),
	X("GPU-cb-busy",		GPU_CB_BUSY,		UINT64, AVERAGE),
	X("GPU-sdma-busy",		GPU_SDMA_BUSY,		UINT64, AVERAGE),
	X("GPU-pfp-busy",		GPU_PFP_BUSY,		UINT64, AVERAGE),
	X("GPU-meq-busy",		GPU_MEQ_BUSY,		UINT64, AVERAGE),
	X("GPU-me-busy",		GPU_ME_BUSY,		UINT64, AVERAGE),
	X("GPU-surf-sync-busy",		GPU_SURF_SYNC_BUSY,	UINT64, AVERAGE),
	X("GPU-cp-dma-busy",		GPU_CP_DMA_BUSY,	UINT64, AVERAGE),
	X("GPU-scratch-ram-busy",	GPU_SCRATCH_RAM_BUSY,	UINT64, AVERAGE),
};

#undef X
#undef XG
#undef XFULL

static unsigned r600_get_num_queries(struct r600_common_screen *rscreen)
{
	return ARRAY_SIZE(r600_driver_query_list);
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
	case R600_QUERY_MAPPED_VRAM:
		info->max_value.u64 = (uint64_t)rscreen->info.vram_size_kb * 1024;
		break;
	case R600_QUERY_REQUESTED_GTT:
	case R600_QUERY_GTT_USAGE:
	case R600_QUERY_MAPPED_GTT:
		info->max_value.u64 = (uint64_t)rscreen->info.gart_size_kb * 1024;
		break;
	case R600_QUERY_GPU_TEMPERATURE:
		info->max_value.u64 = 125;
		break;
	case R600_QUERY_VRAM_VIS_USAGE:
		info->max_value.u64 = (uint64_t)rscreen->info.vram_vis_size_kb * 1024;
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
	rctx->b.get_query_result_resource = r600_get_query_result_resource;
	rctx->render_cond_atom.emit = r600_emit_query_predication;

	if (((struct r600_common_screen*)rctx->b.screen)->info.max_render_backends > 0)
	    rctx->b.render_condition = r600_render_condition;

	list_inithead(&rctx->active_queries);
}

void r600_init_screen_query_functions(struct r600_common_screen *rscreen)
{
	rscreen->b.get_driver_query_info = r600_get_driver_query_info;
	rscreen->b.get_driver_query_group_info = r600_get_driver_query_group_info;
}
