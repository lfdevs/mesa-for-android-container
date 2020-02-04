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

#include "pipe/p_state.h"
#include "util/hash_table.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"

#include "freedreno_gmem.h"
#include "freedreno_context.h"
#include "freedreno_fence.h"
#include "freedreno_resource.h"
#include "freedreno_query_hw.h"
#include "freedreno_util.h"

/*
 * GMEM is the small (ie. 256KiB for a200, 512KiB for a220, etc) tile buffer
 * inside the GPU.  All rendering happens to GMEM.  Larger render targets
 * are split into tiles that are small enough for the color (and depth and/or
 * stencil, if enabled) buffers to fit within GMEM.  Before rendering a tile,
 * if there was not a clear invalidating the previous tile contents, we need
 * to restore the previous tiles contents (system mem -> GMEM), and after all
 * the draw calls, before moving to the next tile, we need to save the tile
 * contents (GMEM -> system mem).
 *
 * The code in this file handles dealing with GMEM and tiling.
 *
 * The structure of the ringbuffer ends up being:
 *
 *     +--<---<-- IB ---<---+---<---+---<---<---<--+
 *     |                    |       |              |
 *     v                    ^       ^              ^
 *   ------------------------------------------------------
 *     | clear/draw cmds | Tile0 | Tile1 | .... | TileN |
 *   ------------------------------------------------------
 *                       ^
 *                       |
 *                       address submitted in issueibcmds
 *
 * Where the per-tile section handles scissor setup, mem2gmem restore (if
 * needed), IB to draw cmds earlier in the ringbuffer, and then gmem2mem
 * resolve.
 */

#define BIN_DEBUG 0

/*
 * GMEM Cache:
 *
 * Caches GMEM state based on a given framebuffer state.  The key is
 * meant to be the minimal set of data that results in a unique gmem
 * configuration, avoiding multiple keys arriving at the same gmem
 * state.  For example, the render target format is not part of the
 * key, only the size per pixel.  And the max_scissor bounds is not
 * part of they key, only the minx/miny (after clamping to tile
 * alignment) and width/height.  This ensures that slightly different
 * max_scissor which would result in the same gmem state, do not
 * become different keys that map to the same state.
 */

struct gmem_key {
	uint16_t minx, miny;
	uint16_t width, height;
	uint8_t gmem_page_align;      /* alignment in multiples of 0x1000 to reduce key size */
	uint8_t nr_cbufs;
	uint8_t cbuf_cpp[MAX_RENDER_TARGETS];
	uint8_t zsbuf_cpp[2];
};

static uint32_t
gmem_key_hash(const void *_key)
{
	const struct gmem_key *key = _key;
	return _mesa_hash_data(key, sizeof(*key));
}

static bool
gmem_key_equals(const void *_a, const void *_b)
{
	const struct gmem_key *a = _a;
	const struct gmem_key *b = _b;
	return memcmp(a, b, sizeof(*a)) == 0;
}

static uint32_t bin_width(struct fd_screen *screen)
{
	if (is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen))
		return 1024;
	if (is_a3xx(screen))
		return 992;
	return 512;
}

static uint32_t
total_size(struct gmem_key *key, uint32_t bin_w, uint32_t bin_h,
		struct fd_gmem_stateobj *gmem)
{
	uint32_t gmem_align = key->gmem_page_align * 0x1000;
	uint32_t total = 0, i;

	for (i = 0; i < MAX_RENDER_TARGETS; i++) {
		if (key->cbuf_cpp[i]) {
			gmem->cbuf_base[i] = align(total, gmem_align);
			total = gmem->cbuf_base[i] + key->cbuf_cpp[i] * bin_w * bin_h;
		}
	}

	if (key->zsbuf_cpp[0]) {
		gmem->zsbuf_base[0] = align(total, gmem_align);
		total = gmem->zsbuf_base[0] + key->zsbuf_cpp[0] * bin_w * bin_h;
	}

	if (key->zsbuf_cpp[1]) {
		gmem->zsbuf_base[1] = align(total, gmem_align);
		total = gmem->zsbuf_base[1] + key->zsbuf_cpp[1] * bin_w * bin_h;
	}

	return total;
}

static struct fd_gmem_stateobj *
gmem_stateobj_init(struct fd_screen *screen, struct gmem_key *key)
{
	struct fd_gmem_stateobj *gmem =
			rzalloc(screen->gmem_cache.ht, struct fd_gmem_stateobj);
	pipe_reference_init(&gmem->reference, 1);
	gmem->screen = screen;
	gmem->key = key;
	list_inithead(&gmem->node);

	const uint32_t gmem_alignw = screen->gmem_alignw;
	const uint32_t gmem_alignh = screen->gmem_alignh;
	const unsigned npipes = screen->num_vsc_pipes;
	const uint32_t gmem_size = screen->gmemsize_bytes;
	uint32_t nbins_x = 1, nbins_y = 1;
	uint32_t bin_w, bin_h;
	uint32_t max_width = bin_width(screen);
	uint32_t i, j, t, xoff, yoff;
	uint32_t tpp_x, tpp_y;
	int tile_n[npipes];

	bin_w = align(key->width, gmem_alignw);
	bin_h = align(key->height, gmem_alignh);

	/* first, find a bin width that satisfies the maximum width
	 * restrictions:
	 */
	while (bin_w > max_width) {
		nbins_x++;
		bin_w = align(key->width / nbins_x, gmem_alignw);
	}

	if (fd_mesa_debug & FD_DBG_MSGS) {
		debug_printf("binning input: cbuf cpp:");
		for (i = 0; i < key->nr_cbufs; i++)
			debug_printf(" %d", key->cbuf_cpp[i]);
		debug_printf(", zsbuf cpp: %d; %dx%d\n",
				key->zsbuf_cpp[0], key->width, key->height);
	}

	/* then find a bin width/height that satisfies the memory
	 * constraints:
	 */
	while (total_size(key, bin_w, bin_h, gmem) > gmem_size) {
		if (bin_w > bin_h) {
			nbins_x++;
			bin_w = align(key->width / nbins_x, gmem_alignw);
		} else {
			nbins_y++;
			bin_h = align(key->height / nbins_y, gmem_alignh);
		}
	}

	DBG("using %d bins of size %dx%d", nbins_x*nbins_y, bin_w, bin_h);

	memcpy(gmem->cbuf_cpp, key->cbuf_cpp, sizeof(key->cbuf_cpp));
	memcpy(gmem->zsbuf_cpp, key->zsbuf_cpp, sizeof(key->zsbuf_cpp));
	gmem->bin_h = bin_h;
	gmem->bin_w = bin_w;
	gmem->nbins_x = nbins_x;
	gmem->nbins_y = nbins_y;
	gmem->minx = key->minx;
	gmem->miny = key->miny;
	gmem->width = key->width;
	gmem->height = key->height;

	/*
	 * Assign tiles and pipes:
	 *
	 * At some point it might be worth playing with different
	 * strategies and seeing if that makes much impact on
	 * performance.
	 */

#define div_round_up(v, a)  (((v) + (a) - 1) / (a))
	/* figure out number of tiles per pipe: */
	if (is_a20x(screen)) {
		/* for a20x we want to minimize the number of "pipes"
		 * binning data has 3 bits for x/y (8x8) but the edges are used to
		 * cull off-screen vertices with hw binning, so we have 6x6 pipes
		 */
		tpp_x = 6;
		tpp_y = 6;
	} else {
		tpp_x = tpp_y = 1;
		while (div_round_up(nbins_y, tpp_y) > npipes)
			tpp_y += 2;
		while ((div_round_up(nbins_y, tpp_y) *
				div_round_up(nbins_x, tpp_x)) > npipes)
			tpp_x += 1;
	}

	gmem->maxpw = tpp_x;
	gmem->maxph = tpp_y;

	/* configure pipes: */
	xoff = yoff = 0;
	for (i = 0; i < npipes; i++) {
		struct fd_vsc_pipe *pipe = &gmem->vsc_pipe[i];

		if (xoff >= nbins_x) {
			xoff = 0;
			yoff += tpp_y;
		}

		if (yoff >= nbins_y) {
			break;
		}

		pipe->x = xoff;
		pipe->y = yoff;
		pipe->w = MIN2(tpp_x, nbins_x - xoff);
		pipe->h = MIN2(tpp_y, nbins_y - yoff);

		xoff += tpp_x;
	}

	/* number of pipes to use for a20x */
	gmem->num_vsc_pipes = MAX2(1, i);

	for (; i < npipes; i++) {
		struct fd_vsc_pipe *pipe = &gmem->vsc_pipe[i];
		pipe->x = pipe->y = pipe->w = pipe->h = 0;
	}

	if (BIN_DEBUG) {
		printf("%dx%d ... tpp=%dx%d\n", nbins_x, nbins_y, tpp_x, tpp_y);
		for (i = 0; i < ARRAY_SIZE(gmem->vsc_pipe); i++) {
			struct fd_vsc_pipe *pipe = &gmem->vsc_pipe[i];
			printf("pipe[%d]: %ux%u @ %u,%u\n", i,
					pipe->w, pipe->h, pipe->x, pipe->y);
		}
	}

	/* configure tiles: */
	t = 0;
	yoff = key->miny;
	memset(tile_n, 0, sizeof(tile_n));
	for (i = 0; i < nbins_y; i++) {
		uint32_t bw, bh;

		xoff = key->minx;

		/* clip bin height: */
		bh = MIN2(bin_h, key->miny + key->height - yoff);

		for (j = 0; j < nbins_x; j++) {
			struct fd_tile *tile = &gmem->tile[t];
			uint32_t p;

			assert(t < ARRAY_SIZE(gmem->tile));

			/* pipe number: */
			p = ((i / tpp_y) * div_round_up(nbins_x, tpp_x)) + (j / tpp_x);
			assert(p < gmem->num_vsc_pipes);

			/* clip bin width: */
			bw = MIN2(bin_w, key->minx + key->width - xoff);
			tile->n = !is_a20x(screen) ? tile_n[p]++ :
				((i % tpp_y + 1) << 3 | (j % tpp_x + 1));
			tile->p = p;
			tile->bin_w = bw;
			tile->bin_h = bh;
			tile->xoff = xoff;
			tile->yoff = yoff;

			if (BIN_DEBUG) {
				printf("tile[%d]: p=%u, bin=%ux%u+%u+%u\n", t,
						p, bw, bh, xoff, yoff);
			}

			t++;

			xoff += bw;
		}

		yoff += bh;
	}

	if (BIN_DEBUG) {
		t = 0;
		for (i = 0; i < nbins_y; i++) {
			for (j = 0; j < nbins_x; j++) {
				struct fd_tile *tile = &gmem->tile[t++];
				printf("|p:%u n:%u|", tile->p, tile->n);
			}
			printf("\n");
		}
	}

	return gmem;
}

void
__fd_gmem_destroy(struct fd_gmem_stateobj *gmem)
{
	struct fd_gmem_cache *cache = &gmem->screen->gmem_cache;

	pipe_mutex_assert_locked(gmem->screen->lock);

	_mesa_hash_table_remove_key(cache->ht, gmem->key);
	list_del(&gmem->node);

	ralloc_free(gmem->key);
	ralloc_free(gmem);
}

static struct gmem_key *
key_init(struct fd_batch *batch)
{
	struct fd_screen *screen = batch->ctx->screen;
	struct pipe_framebuffer_state *pfb = &batch->framebuffer;
	bool has_zs = pfb->zsbuf && !!(batch->gmem_reason & (FD_GMEM_DEPTH_ENABLED |
		FD_GMEM_STENCIL_ENABLED | FD_GMEM_CLEARS_DEPTH_STENCIL));
	struct gmem_key *key = rzalloc(screen->gmem_cache.ht, struct gmem_key);

	if (has_zs) {
		struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);
		key->zsbuf_cpp[0] = rsc->layout.cpp;
		if (rsc->stencil)
			key->zsbuf_cpp[1] = rsc->stencil->layout.cpp;
	} else {
		/* we might have a zsbuf, but it isn't used */
		batch->restore &= ~(FD_BUFFER_DEPTH | FD_BUFFER_STENCIL);
		batch->resolve &= ~(FD_BUFFER_DEPTH | FD_BUFFER_STENCIL);
	}

	key->nr_cbufs = pfb->nr_cbufs;
	for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
		if (pfb->cbufs[i])
			key->cbuf_cpp[i] = util_format_get_blocksize(pfb->cbufs[i]->format);
		else
			key->cbuf_cpp[i] = 4;
		/* if MSAA, color buffers are super-sampled in GMEM: */
		key->cbuf_cpp[i] *= pfb->samples;
	}

	if (fd_mesa_debug & FD_DBG_NOSCIS) {
		key->minx = 0;
		key->miny = 0;
		key->width = pfb->width;
		key->height = pfb->height;
	} else {
		struct pipe_scissor_state *scissor = &batch->max_scissor;

		/* round down to multiple of alignment: */
		key->minx = scissor->minx & ~(screen->gmem_alignw - 1);
		key->miny = scissor->miny & ~(screen->gmem_alignh - 1);
		key->width = scissor->maxx - key->minx;
		key->height = scissor->maxy - key->miny;
	}

	if (is_a20x(screen) && batch->cleared) {
		/* under normal circumstances the requirement would be 4K
		 * but the fast clear path requires an alignment of 32K
		 */
		key->gmem_page_align = 8;
	} else {
		// TODO re-check this across gens.. maybe it should only
		// be a single page in some cases:
		key->gmem_page_align = 4;
	}

	return key;
}

static struct fd_gmem_stateobj *
lookup_gmem_state(struct fd_batch *batch)
{
	struct fd_screen *screen = batch->ctx->screen;
	struct fd_gmem_cache *cache = &screen->gmem_cache;
	struct fd_gmem_stateobj *gmem = NULL;
	struct gmem_key *key = key_init(batch);
	uint32_t hash = gmem_key_hash(key);

	mtx_lock(&screen->lock);

	struct hash_entry *entry =
		_mesa_hash_table_search_pre_hashed(cache->ht, hash, key);
	if (entry) {
		ralloc_free(key);
		goto found;
	}

	/* limit the # of cached gmem states, discarding the least
	 * recently used state if needed:
	 */
	if (cache->ht->entries >= 20) {
		struct fd_gmem_stateobj *last =
			list_last_entry(&cache->lru, struct fd_gmem_stateobj, node);
		fd_gmem_reference(&last, NULL);
	}

	entry = _mesa_hash_table_insert_pre_hashed(cache->ht,
			hash, key, gmem_stateobj_init(screen, key));

found:
	fd_gmem_reference(&gmem, entry->data);
	/* Move to the head of the LRU: */
	list_delinit(&gmem->node);
	list_add(&gmem->node, &cache->lru);

	mtx_unlock(&screen->lock);

	return gmem;
}

/*
 * GMEM render pass
 */

static void
render_tiles(struct fd_batch *batch, struct fd_gmem_stateobj *gmem)
{
	struct fd_context *ctx = batch->ctx;
	int i;

	mtx_lock(&ctx->gmem_lock);

	ctx->emit_tile_init(batch);

	if (batch->restore)
		ctx->stats.batch_restore++;

	for (i = 0; i < (gmem->nbins_x * gmem->nbins_y); i++) {
		struct fd_tile *tile = &gmem->tile[i];

		DBG("bin_h=%d, yoff=%d, bin_w=%d, xoff=%d",
			tile->bin_h, tile->yoff, tile->bin_w, tile->xoff);

		ctx->emit_tile_prep(batch, tile);

		if (batch->restore) {
			ctx->emit_tile_mem2gmem(batch, tile);
		}

		ctx->emit_tile_renderprep(batch, tile);

		if (ctx->query_prepare_tile)
			ctx->query_prepare_tile(batch, i, batch->gmem);

		/* emit IB to drawcmds: */
		if (ctx->emit_tile) {
			ctx->emit_tile(batch, tile);
		} else {
			ctx->screen->emit_ib(batch->gmem, batch->draw);
		}
		fd_reset_wfi(batch);

		/* emit gmem2mem to transfer tile back to system memory: */
		ctx->emit_tile_gmem2mem(batch, tile);
	}

	if (ctx->emit_tile_fini)
		ctx->emit_tile_fini(batch);

	mtx_unlock(&ctx->gmem_lock);
}

static void
render_sysmem(struct fd_batch *batch)
{
	struct fd_context *ctx = batch->ctx;

	ctx->emit_sysmem_prep(batch);

	if (ctx->query_prepare_tile)
		ctx->query_prepare_tile(batch, 0, batch->gmem);

	/* emit IB to drawcmds: */
	ctx->screen->emit_ib(batch->gmem, batch->draw);
	fd_reset_wfi(batch);

	if (ctx->emit_sysmem_fini)
		ctx->emit_sysmem_fini(batch);
}

static void
flush_ring(struct fd_batch *batch)
{
	uint32_t timestamp;
	int out_fence_fd = -1;

	fd_submit_flush(batch->submit, batch->in_fence_fd,
			batch->needs_out_fence_fd ? &out_fence_fd : NULL,
			&timestamp);

	fd_fence_populate(batch->fence, timestamp, out_fence_fd);
}

void
fd_gmem_render_tiles(struct fd_batch *batch)
{
	struct fd_context *ctx = batch->ctx;
	struct pipe_framebuffer_state *pfb = &batch->framebuffer;
	bool sysmem = false;

	if (ctx->emit_sysmem_prep && !batch->nondraw) {
		if (batch->cleared || batch->gmem_reason ||
				((batch->num_draws > 5) && !batch->blit) ||
				(pfb->samples > 1)) {
			DBG("GMEM: cleared=%x, gmem_reason=%x, num_draws=%u, samples=%u",
				batch->cleared, batch->gmem_reason, batch->num_draws,
				pfb->samples);
		} else if (!(fd_mesa_debug & FD_DBG_NOBYPASS)) {
			sysmem = true;
		}

		/* For ARB_framebuffer_no_attachments: */
		if ((pfb->nr_cbufs == 0) && !pfb->zsbuf) {
			sysmem = true;
		}
	}

	if (fd_mesa_debug & FD_DBG_NOGMEM)
		sysmem = true;

	/* Layered rendering always needs bypass. */
	for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
		struct pipe_surface *psurf = pfb->cbufs[i];
		if (!psurf)
			continue;
		if (psurf->u.tex.first_layer < psurf->u.tex.last_layer)
			sysmem = true;
	}

	/* Tessellation doesn't seem to support tiled rendering so fall back to
	 * bypass.
	 */
	if (batch->tessellation) {
		debug_assert(ctx->emit_sysmem_prep);
		sysmem = true;
	}

	fd_reset_wfi(batch);

	ctx->stats.batch_total++;

	if (batch->nondraw) {
		DBG("%p: rendering non-draw", batch);
		ctx->stats.batch_nondraw++;
	} else if (sysmem) {
		DBG("%p: rendering sysmem %ux%u (%s/%s), num_draws=%u",
			batch, pfb->width, pfb->height,
			util_format_short_name(pipe_surface_format(pfb->cbufs[0])),
			util_format_short_name(pipe_surface_format(pfb->zsbuf)),
			batch->num_draws);
		if (ctx->query_prepare)
			ctx->query_prepare(batch, 1);
		render_sysmem(batch);
		ctx->stats.batch_sysmem++;
	} else {
		struct fd_gmem_stateobj *gmem = lookup_gmem_state(batch);
		batch->gmem_state = gmem;
		DBG("%p: rendering %dx%d tiles %ux%u (%s/%s)",
			batch, pfb->width, pfb->height, gmem->nbins_x, gmem->nbins_y,
			util_format_short_name(pipe_surface_format(pfb->cbufs[0])),
			util_format_short_name(pipe_surface_format(pfb->zsbuf)));
		if (ctx->query_prepare)
			ctx->query_prepare(batch, gmem->nbins_x * gmem->nbins_y);
		render_tiles(batch, gmem);
		batch->gmem_state = NULL;

		mtx_lock(&ctx->screen->lock);
		fd_gmem_reference(&gmem, NULL);
		mtx_unlock(&ctx->screen->lock);

		ctx->stats.batch_gmem++;
	}

	flush_ring(batch);
}

/* When deciding whether a tile needs mem2gmem, we need to take into
 * account the scissor rect(s) that were cleared.  To simplify we only
 * consider the last scissor rect for each buffer, since the common
 * case would be a single clear.
 */
bool
fd_gmem_needs_restore(struct fd_batch *batch, const struct fd_tile *tile,
		uint32_t buffers)
{
	if (!(batch->restore & buffers))
		return false;

	return true;
}

void
fd_gmem_screen_init(struct pipe_screen *pscreen)
{
	struct fd_gmem_cache *cache = &fd_screen(pscreen)->gmem_cache;

	cache->ht = _mesa_hash_table_create(NULL, gmem_key_hash, gmem_key_equals);
	list_inithead(&cache->lru);
}

void
fd_gmem_screen_fini(struct pipe_screen *pscreen)
{
	struct fd_gmem_cache *cache = &fd_screen(pscreen)->gmem_cache;

	_mesa_hash_table_destroy(cache->ht, NULL);
}
