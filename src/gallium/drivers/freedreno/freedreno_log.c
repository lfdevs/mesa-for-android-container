/*
 * Copyright © 2020 Google, Inc.
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
 */

#include <inttypes.h>
#include <stdarg.h>

#include "freedreno_log.h"
#include "freedreno_batch.h"
#include "freedreno_context.h"

#include "util/u_fifo.h"

/* A simple(ish) logging mechanism with timestamps recorded at the
 * corresponding point in the cmdstream.  The overall design is based
 * on fd_log_chunk's, which buffer up up to 'msgs_per_chunk' log msgs
 * and track an associated bo containing timestamps written from the
 * GPU.
 *
 * The fd_batch tracks a list of fd_log_chunk's.  When fd_log() is
 * called, msgs are pushed into the last (most recent) chunk until
 * it is full, at which point a new chunk is created.  And cmds are
 * inserted into the cmdstream to record the GPU timestamp that
 * corresponds with the log msg.
 *
 * When the batch is flushed, the list of log chunks is transferred
 * to the end of fd_context's list of chunks, and we attempt to pop
 * chunks from the from of the list if their timestamp bo is idle (ie.
 * the GPU has finished the batch that was writing the timestamps).
 *
 * NOTE: this is only appropriate for IB1 (ie. "gmem" level) cmdstream,
 * the IB2 (draw/binning) cmdstream can be executed multiple times,
 * which this mechanism is not designed to support.  Other existing
 * GL level queries (time-elapsed, amd-perfcntrs) are more appropriate
 * for profiling at that level.
 */

enum {
	bo_size = 0x1000,
	msgs_per_chunk = bo_size / sizeof(uint64_t),
};

struct fd_log_chunk {
	struct list_head node;

	unsigned num_msgs;
	struct util_fifo *msg_fifo;

	/* list of recorded 64b timestamps */
	struct fd_bo *timestamps_bo;

	bool last;          /* this chunk is last in submit */
	bool eof;           /* this chunk is last in frame */
	uint32_t *ring_cur;
};

static struct fd_log_chunk *
get_chunk(struct fd_batch *batch)
{
	struct fd_log_chunk *chunk;

	/* do we currently have a non-full chunk to append msgs to? */
	if (!list_is_empty(&batch->log_chunks)) {
		chunk = list_last_entry(&batch->log_chunks,
				struct fd_log_chunk, node);
		if (chunk->num_msgs < msgs_per_chunk)
			return chunk;
		/* we need to expand to add another chunk to the batch, so
		 * the current one is no longer the last one of the batch:
		 */
		chunk->last = false;
	}

	/* .. if not, then create a new one: */
	chunk = calloc(1, sizeof(*chunk));
	chunk->msg_fifo = u_fifo_create(msgs_per_chunk);
	chunk->timestamps_bo = fd_bo_new(batch->ctx->screen->dev, bo_size,
			DRM_FREEDRENO_GEM_TYPE_KMEM, "timestamps");
	chunk->last = true;

	list_addtail(&chunk->node, &batch->log_chunks);

	return chunk;
}

static void
free_chunk(struct fd_log_chunk *chunk)
{
	assert(chunk->msg_fifo->num == 0);
	u_fifo_destroy(chunk->msg_fifo);
	fd_bo_del(chunk->timestamps_bo);
	list_del(&chunk->node);
	free(chunk);
}

/* state to accumulate time across N chunks associated with a single batch: */
struct times {
	uint64_t last_time_ns;
	uint64_t first_time_ns;
};

static void
process_chunk(struct fd_context *ctx, struct fd_log_chunk *chunk, struct times *t)
{
	/* For first chunk of batch, accumulated times will be zerod: */
	if (!t->last_time_ns) {
		fprintf(ctx->log_out,
			"+----- TS -----+ +----- NS -----+ +-- Δ --+  +----- MSG -----\n");
	}

	uint64_t *timestamps = fd_bo_map(chunk->timestamps_bo);
	unsigned n = 0;
	char *msg;

	while (u_fifo_pop(chunk->msg_fifo, (void **)&msg)) {
		uint64_t ts = timestamps[n++];
		uint64_t ns = ctx->ts_to_ns(ts);
		int32_t delta;

		if (!t->first_time_ns)
			t->first_time_ns = ns;

		if (ns) {
			delta = t->last_time_ns ? ns - t->last_time_ns : 0;
			t->last_time_ns = ns;
		} else {
			/* we skipped recording the timestamp, so it should be
			 * the same as last msg:
			 */
			ns = t->last_time_ns;
			delta = 0;
		}

		fprintf(ctx->log_out, "%016"PRIu64" %016"PRIu64" %+9d: %s\n",
			ts, ns, delta, msg);
		free(msg);

	}

	if (chunk->last) {
		fprintf(ctx->log_out, "ELAPSED: %"PRIu64" ns\n",
			t->last_time_ns - t->first_time_ns);
		memset(t, 0, sizeof(*t));
	}

	if (chunk->eof)
		fprintf(ctx->log_out, "END OF FRAME %u\n", ctx->frame_nr++);
}

void
fd_log_process(struct fd_context *ctx, bool wait)
{
	struct times times = {0};

	while (!list_is_empty(&ctx->log_chunks)) {
		struct fd_log_chunk *chunk = list_first_entry(&ctx->log_chunks,
				struct fd_log_chunk, node);

		unsigned flags = DRM_FREEDRENO_PREP_READ;
		if (!wait)
			flags |= DRM_FREEDRENO_PREP_NOSYNC;

		int ret = fd_bo_cpu_prep(chunk->timestamps_bo, ctx->pipe, flags);
		if (ret)
			break;

		process_chunk(ctx, chunk, &times);
		free_chunk(chunk);
	}

	/* We expect that the last processed chunk was the last in it's
	 * batch, which should reset the times:
	 */
	if (times.last_time_ns) {
		fprintf(ctx->log_out, "WARNING: last processed chunk not last in batch?");
	}

	fflush(ctx->log_out);
}

void
fd_log_flush(struct fd_batch *batch)
{
	/* transfer batch's log chunks to context: */
	list_splicetail(&batch->log_chunks, &batch->ctx->log_chunks);
	list_inithead(&batch->log_chunks);
}

void
_fd_log(struct fd_batch *batch, const char *fmt, ...)
{
	struct fd_context *ctx = batch->ctx;
	struct fd_ringbuffer *ring = batch->nondraw ? batch->draw : batch->gmem;
	struct fd_log_chunk *chunk = get_chunk(batch);
	char *msg;

	va_list ap;
	va_start(ap, fmt);
	if (vasprintf(&msg, fmt, ap) < 0) {
		va_end(ap);
		return;
	}
	va_end(ap);

	u_fifo_add(chunk->msg_fifo, msg);

	assert(ctx->record_timestamp);

	/* If nothing else has been emitted to the ring since the last log msg,
	 * skip emitting another timestamp.
	 */
	if (ring->cur == chunk->ring_cur) {
		uint64_t *ts = fd_bo_map(chunk->timestamps_bo);
		/* zero signifies an invalid timestamp to process_chunk(), so it
		 * will use the last valid timestamp for this msg instead.
		 */
		ts[chunk->num_msgs] = 0;
	} else {
		ctx->record_timestamp(ring, chunk->timestamps_bo,
				chunk->num_msgs * sizeof(uint64_t));
	}

	chunk->num_msgs++;
	chunk->ring_cur = ring->cur;
}

void fd_log_eof(struct fd_context *ctx)
{
	if (!(fd_mesa_debug & FD_DBG_LOG))
		return;

	if (list_is_empty(&ctx->log_chunks)) {
		fprintf(ctx->log_out, "WARNING: no log chunks!\n");
		return;
	}

	struct fd_log_chunk *last_chunk = list_last_entry(&ctx->log_chunks,
			struct fd_log_chunk, node);
	last_chunk->eof = true;

	/* and process any chunks that are now idle/ready: */
	fd_log_process(ctx, false);
}
