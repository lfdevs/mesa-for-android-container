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

#include "util/u_format.h"
#include "util/u_format_rgtc.h"
#include "util/u_format_zs.h"
#include "util/u_inlines.h"
#include "util/u_transfer.h"
#include "util/u_string.h"
#include "util/u_surface.h"
#include "util/set.h"

#include "freedreno_resource.h"
#include "freedreno_batch_cache.h"
#include "freedreno_screen.h"
#include "freedreno_surface.h"
#include "freedreno_context.h"
#include "freedreno_query_hw.h"
#include "freedreno_util.h"

#include <errno.h>

/* XXX this should go away, needed for 'struct winsys_handle' */
#include "state_tracker/drm_driver.h"

static void
fd_invalidate_resource(struct fd_context *ctx, struct pipe_resource *prsc)
{
	int i;

	/* Go through the entire state and see if the resource is bound
	 * anywhere. If it is, mark the relevant state as dirty. This is called on
	 * realloc_bo.
	 */

	/* Constbufs */
	for (i = 1; i < PIPE_MAX_CONSTANT_BUFFERS && !(ctx->dirty & FD_DIRTY_CONSTBUF); i++) {
		if (ctx->constbuf[PIPE_SHADER_VERTEX].cb[i].buffer == prsc)
			ctx->dirty |= FD_DIRTY_CONSTBUF;
		if (ctx->constbuf[PIPE_SHADER_FRAGMENT].cb[i].buffer == prsc)
			ctx->dirty |= FD_DIRTY_CONSTBUF;
	}

	/* VBOs */
	for (i = 0; i < ctx->vtx.vertexbuf.count && !(ctx->dirty & FD_DIRTY_VTXBUF); i++) {
		if (ctx->vtx.vertexbuf.vb[i].buffer == prsc)
			ctx->dirty |= FD_DIRTY_VTXBUF;
	}

	/* Index buffer */
	if (ctx->indexbuf.buffer == prsc)
		ctx->dirty |= FD_DIRTY_INDEXBUF;

	/* Textures */
	for (i = 0; i < ctx->verttex.num_textures && !(ctx->dirty & FD_DIRTY_VERTTEX); i++) {
		if (ctx->verttex.textures[i] && (ctx->verttex.textures[i]->texture == prsc))
			ctx->dirty |= FD_DIRTY_VERTTEX;
	}
	for (i = 0; i < ctx->fragtex.num_textures && !(ctx->dirty & FD_DIRTY_FRAGTEX); i++) {
		if (ctx->fragtex.textures[i] && (ctx->fragtex.textures[i]->texture == prsc))
			ctx->dirty |= FD_DIRTY_FRAGTEX;
	}
}

static void
realloc_bo(struct fd_resource *rsc, uint32_t size)
{
	struct fd_screen *screen = fd_screen(rsc->base.b.screen);
	uint32_t flags = DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM; /* TODO */

	/* if we start using things other than write-combine,
	 * be sure to check for PIPE_RESOURCE_FLAG_MAP_COHERENT
	 */

	if (rsc->bo)
		fd_bo_del(rsc->bo);

	rsc->bo = fd_bo_new(screen->dev, size, flags);
	rsc->timestamp = 0;
	util_range_set_empty(&rsc->valid_buffer_range);
	fd_bc_invalidate_resource(rsc, true);
}

static void
do_blit(struct fd_context *ctx, const struct pipe_blit_info *blit, bool fallback)
{
	/* TODO size threshold too?? */
	if ((blit->src.resource->target != PIPE_BUFFER) && !fallback) {
		/* do blit on gpu: */
		fd_blitter_pipe_begin(ctx, false, true, FD_STAGE_BLIT);
		util_blitter_blit(ctx->blitter, blit);
		fd_blitter_pipe_end(ctx);
	} else {
		/* do blit on cpu: */
		util_resource_copy_region(&ctx->base,
				blit->dst.resource, blit->dst.level, blit->dst.box.x,
				blit->dst.box.y, blit->dst.box.z,
				blit->src.resource, blit->src.level, &blit->src.box);
	}
}

static bool
fd_try_shadow_resource(struct fd_context *ctx, struct fd_resource *rsc,
		unsigned level, unsigned usage, const struct pipe_box *box)
{
	struct pipe_context *pctx = &ctx->base;
	struct pipe_resource *prsc = &rsc->base.b;
	bool fallback = false;

	if (prsc->next)
		return false;

	/* TODO: somehow munge dimensions and format to copy unsupported
	 * render target format to something that is supported?
	 */
	if (!pctx->screen->is_format_supported(pctx->screen,
			prsc->format, prsc->target, prsc->nr_samples,
			PIPE_BIND_RENDER_TARGET))
		fallback = true;

	/* these cases should be handled elsewhere.. just for future
	 * reference in case this gets split into a more generic(ish)
	 * helper.
	 */
	debug_assert(!(usage & PIPE_TRANSFER_READ));
	debug_assert(!(usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE));

	/* if we do a gpu blit to clone the whole resource, we'll just
	 * end up stalling on that.. so only allow if we can discard
	 * current range (and blit, possibly cpu or gpu, the rest)
	 */
	if (!(usage & PIPE_TRANSFER_DISCARD_RANGE))
		return false;

	bool whole_level = util_texrange_covers_whole_level(prsc, level,
		box->x, box->y, box->z, box->width, box->height, box->depth);

	/* TODO need to be more clever about current level */
	if ((prsc->target >= PIPE_TEXTURE_2D) && !whole_level)
		return false;

	struct pipe_resource *pshadow =
		pctx->screen->resource_create(pctx->screen, prsc);

	if (!pshadow)
		return false;

	assert(!ctx->in_shadow);
	ctx->in_shadow = true;

	/* get rid of any references that batch-cache might have to us (which
	 * should empty/destroy rsc->batches hashset)
	 */
	fd_bc_invalidate_resource(rsc, false);

	pipe_mutex_lock(ctx->screen->lock);

	/* Swap the backing bo's, so shadow becomes the old buffer,
	 * blit from shadow to new buffer.  From here on out, we
	 * cannot fail.
	 *
	 * Note that we need to do it in this order, otherwise if
	 * we go down cpu blit path, the recursive transfer_map()
	 * sees the wrong status..
	 */
	struct fd_resource *shadow = fd_resource(pshadow);

	DBG("shadow: %p (%d) -> %p (%d)\n", rsc, rsc->base.b.reference.count,
			shadow, shadow->base.b.reference.count);

	/* TODO valid_buffer_range?? */
	swap(rsc->bo,        shadow->bo);
	swap(rsc->timestamp, shadow->timestamp);
	swap(rsc->write_batch,   shadow->write_batch);

	/* at this point, the newly created shadow buffer is not referenced
	 * by any batches, but the existing rsc (probably) is.  We need to
	 * transfer those references over:
	 */
	debug_assert(shadow->batch_mask == 0);
	struct fd_batch *batch;
	foreach_batch(batch, &ctx->screen->batch_cache, rsc->batch_mask) {
		struct set_entry *entry = _mesa_set_search(batch->resources, rsc);
		_mesa_set_remove(batch->resources, entry);
		_mesa_set_add(batch->resources, shadow);
	}
	swap(rsc->batch_mask, shadow->batch_mask);

	pipe_mutex_unlock(ctx->screen->lock);

	struct pipe_blit_info blit = {0};
	blit.dst.resource = prsc;
	blit.dst.format   = prsc->format;
	blit.src.resource = pshadow;
	blit.src.format   = pshadow->format;
	blit.mask = util_format_get_mask(prsc->format);
	blit.filter = PIPE_TEX_FILTER_NEAREST;

#define set_box(field, val) do {     \
		blit.dst.field = (val);      \
		blit.src.field = (val);      \
	} while (0)

	/* blit the other levels in their entirety: */
	for (unsigned l = 0; l <= prsc->last_level; l++) {
		if (l == level)
			continue;

		/* just blit whole level: */
		set_box(level, l);
		set_box(box.width,  u_minify(prsc->width0, l));
		set_box(box.height, u_minify(prsc->height0, l));
		set_box(box.depth,  u_minify(prsc->depth0, l));

		do_blit(ctx, &blit, fallback);
	}

	/* deal w/ current level specially, since we might need to split
	 * it up into a couple blits:
	 */
	if (!whole_level) {
		set_box(level, level);

		switch (prsc->target) {
		case PIPE_BUFFER:
		case PIPE_TEXTURE_1D:
			set_box(box.y, 0);
			set_box(box.z, 0);
			set_box(box.height, 1);
			set_box(box.depth, 1);

			if (box->x > 0) {
				set_box(box.x, 0);
				set_box(box.width, box->x);

				do_blit(ctx, &blit, fallback);
			}
			if ((box->x + box->width) < u_minify(prsc->width0, level)) {
				set_box(box.x, box->x + box->width);
				set_box(box.width, u_minify(prsc->width0, level) - (box->x + box->width));

				do_blit(ctx, &blit, fallback);
			}
			break;
		case PIPE_TEXTURE_2D:
			/* TODO */
		default:
			unreachable("TODO");
		}
	}

	ctx->in_shadow = false;

	pipe_resource_reference(&pshadow, NULL);

	return true;
}

static unsigned
fd_resource_layer_offset(struct fd_resource *rsc,
						 struct fd_resource_slice *slice,
						 unsigned layer)
{
	if (rsc->layer_first)
		return layer * rsc->layer_size;
	else
		return layer * slice->size0;
}

static void
fd_resource_flush_z32s8(struct fd_transfer *trans, const struct pipe_box *box)
{
	struct fd_resource *rsc = fd_resource(trans->base.resource);
	struct fd_resource_slice *slice = fd_resource_slice(rsc, trans->base.level);
	struct fd_resource_slice *sslice = fd_resource_slice(rsc->stencil, trans->base.level);
	enum pipe_format format = trans->base.resource->format;

	float *depth = fd_bo_map(rsc->bo) + slice->offset +
		fd_resource_layer_offset(rsc, slice, trans->base.box.z) +
		(trans->base.box.y + box->y) * slice->pitch * 4 + (trans->base.box.x + box->x) * 4;
	uint8_t *stencil = fd_bo_map(rsc->stencil->bo) + sslice->offset +
		fd_resource_layer_offset(rsc->stencil, sslice, trans->base.box.z) +
		(trans->base.box.y + box->y) * sslice->pitch + trans->base.box.x + box->x;

	if (format != PIPE_FORMAT_X32_S8X24_UINT)
		util_format_z32_float_s8x24_uint_unpack_z_float(
				depth, slice->pitch * 4,
				trans->staging, trans->base.stride,
				box->width, box->height);

	util_format_z32_float_s8x24_uint_unpack_s_8uint(
			stencil, sslice->pitch,
			trans->staging, trans->base.stride,
			box->width, box->height);
}

static void
fd_resource_flush_rgtc(struct fd_transfer *trans, const struct pipe_box *box)
{
	struct fd_resource *rsc = fd_resource(trans->base.resource);
	struct fd_resource_slice *slice = fd_resource_slice(rsc, trans->base.level);
	enum pipe_format format = trans->base.resource->format;

	uint8_t *data = fd_bo_map(rsc->bo) + slice->offset +
		fd_resource_layer_offset(rsc, slice, trans->base.box.z) +
		((trans->base.box.y + box->y) * slice->pitch +
		 trans->base.box.x + box->x) * rsc->cpp;

	uint8_t *source = trans->staging +
		util_format_get_nblocksy(format, box->y) * trans->base.stride +
		util_format_get_stride(format, box->x);

	switch (format) {
	case PIPE_FORMAT_RGTC1_UNORM:
	case PIPE_FORMAT_RGTC1_SNORM:
	case PIPE_FORMAT_LATC1_UNORM:
	case PIPE_FORMAT_LATC1_SNORM:
		util_format_rgtc1_unorm_unpack_rgba_8unorm(
				data, slice->pitch * rsc->cpp,
				source, trans->base.stride,
				box->width, box->height);
		break;
	case PIPE_FORMAT_RGTC2_UNORM:
	case PIPE_FORMAT_RGTC2_SNORM:
	case PIPE_FORMAT_LATC2_UNORM:
	case PIPE_FORMAT_LATC2_SNORM:
		util_format_rgtc2_unorm_unpack_rgba_8unorm(
				data, slice->pitch * rsc->cpp,
				source, trans->base.stride,
				box->width, box->height);
		break;
	default:
		assert(!"Unexpected format\n");
		break;
	}
}

static void
fd_resource_flush(struct fd_transfer *trans, const struct pipe_box *box)
{
	enum pipe_format format = trans->base.resource->format;

	switch (format) {
	case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
	case PIPE_FORMAT_X32_S8X24_UINT:
		fd_resource_flush_z32s8(trans, box);
		break;
	case PIPE_FORMAT_RGTC1_UNORM:
	case PIPE_FORMAT_RGTC1_SNORM:
	case PIPE_FORMAT_RGTC2_UNORM:
	case PIPE_FORMAT_RGTC2_SNORM:
	case PIPE_FORMAT_LATC1_UNORM:
	case PIPE_FORMAT_LATC1_SNORM:
	case PIPE_FORMAT_LATC2_UNORM:
	case PIPE_FORMAT_LATC2_SNORM:
		fd_resource_flush_rgtc(trans, box);
		break;
	default:
		assert(!"Unexpected staging transfer type");
		break;
	}
}

static void fd_resource_transfer_flush_region(struct pipe_context *pctx,
		struct pipe_transfer *ptrans,
		const struct pipe_box *box)
{
	struct fd_resource *rsc = fd_resource(ptrans->resource);
	struct fd_transfer *trans = fd_transfer(ptrans);

	if (ptrans->resource->target == PIPE_BUFFER)
		util_range_add(&rsc->valid_buffer_range,
					   ptrans->box.x + box->x,
					   ptrans->box.x + box->x + box->width);

	if (trans->staging)
		fd_resource_flush(trans, box);
}

static void
fd_resource_transfer_unmap(struct pipe_context *pctx,
		struct pipe_transfer *ptrans)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd_resource *rsc = fd_resource(ptrans->resource);
	struct fd_transfer *trans = fd_transfer(ptrans);

	if (trans->staging && !(ptrans->usage & PIPE_TRANSFER_FLUSH_EXPLICIT)) {
		struct pipe_box box;
		u_box_2d(0, 0, ptrans->box.width, ptrans->box.height, &box);
		fd_resource_flush(trans, &box);
	}

	if (!(ptrans->usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
		fd_bo_cpu_fini(rsc->bo);
		if (rsc->stencil)
			fd_bo_cpu_fini(rsc->stencil->bo);
	}

	util_range_add(&rsc->valid_buffer_range,
				   ptrans->box.x,
				   ptrans->box.x + ptrans->box.width);

	pipe_resource_reference(&ptrans->resource, NULL);
	slab_free(&ctx->transfer_pool, ptrans);

	free(trans->staging);
}

static void *
fd_resource_transfer_map(struct pipe_context *pctx,
		struct pipe_resource *prsc,
		unsigned level, unsigned usage,
		const struct pipe_box *box,
		struct pipe_transfer **pptrans)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd_resource *rsc = fd_resource(prsc);
	struct fd_resource_slice *slice = fd_resource_slice(rsc, level);
	struct fd_transfer *trans;
	struct pipe_transfer *ptrans;
	enum pipe_format format = prsc->format;
	uint32_t op = 0;
	uint32_t offset;
	char *buf;
	int ret = 0;

	DBG("prsc=%p, level=%u, usage=%x, box=%dx%d+%d,%d", prsc, level, usage,
		box->width, box->height, box->x, box->y);

	ptrans = slab_alloc(&ctx->transfer_pool);
	if (!ptrans)
		return NULL;

	/* slab_alloc_st() doesn't zero: */
	trans = fd_transfer(ptrans);
	memset(trans, 0, sizeof(*trans));

	pipe_resource_reference(&ptrans->resource, prsc);
	ptrans->level = level;
	ptrans->usage = usage;
	ptrans->box = *box;
	ptrans->stride = util_format_get_nblocksx(format, slice->pitch) * rsc->cpp;
	ptrans->layer_stride = rsc->layer_first ? rsc->layer_size : slice->size0;

	if (ctx->in_shadow && !(usage & PIPE_TRANSFER_READ))
		usage |= PIPE_TRANSFER_UNSYNCHRONIZED;

	if (usage & PIPE_TRANSFER_READ)
		op |= DRM_FREEDRENO_PREP_READ;

	if (usage & PIPE_TRANSFER_WRITE)
		op |= DRM_FREEDRENO_PREP_WRITE;

	if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) {
		realloc_bo(rsc, fd_bo_size(rsc->bo));
		if (rsc->stencil)
			realloc_bo(rsc->stencil, fd_bo_size(rsc->stencil->bo));
		fd_invalidate_resource(ctx, prsc);
	} else if ((usage & PIPE_TRANSFER_WRITE) &&
			   prsc->target == PIPE_BUFFER &&
			   !util_ranges_intersect(&rsc->valid_buffer_range,
									  box->x, box->x + box->width)) {
		/* We are trying to write to a previously uninitialized range. No need
		 * to wait.
		 */
	} else if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
		struct fd_batch *write_batch = NULL;

		/* hold a reference, so it doesn't disappear under us: */
		fd_batch_reference(&write_batch, rsc->write_batch);

		if ((usage & PIPE_TRANSFER_WRITE) && write_batch &&
				write_batch->back_blit) {
			/* if only thing pending is a back-blit, we can discard it: */
			fd_batch_reset(write_batch);
		}

		/* If the GPU is writing to the resource, or if it is reading from the
		 * resource and we're trying to write to it, flush the renders.
		 */
		bool needs_flush = pending(rsc, !!(usage & PIPE_TRANSFER_WRITE));
		bool busy = needs_flush || (0 != fd_bo_cpu_prep(rsc->bo,
				ctx->screen->pipe, op | DRM_FREEDRENO_PREP_NOSYNC));

		/* if we need to flush/stall, see if we can make a shadow buffer
		 * to avoid this:
		 *
		 * TODO we could go down this path !reorder && !busy_for_read
		 * ie. we only *don't* want to go down this path if the blit
		 * will trigger a flush!
		 */
		if (ctx->screen->reorder && busy && !(usage & PIPE_TRANSFER_READ)) {
			if (fd_try_shadow_resource(ctx, rsc, level, usage, box)) {
				needs_flush = busy = false;
				fd_invalidate_resource(ctx, prsc);
			}
		}

		if (needs_flush) {
			if (usage & PIPE_TRANSFER_WRITE) {
				struct fd_batch *batch, *last_batch = NULL;
				foreach_batch(batch, &ctx->screen->batch_cache, rsc->batch_mask) {
					fd_batch_reference(&last_batch, batch);
					fd_batch_flush(batch, false);
				}
				if (last_batch) {
					fd_batch_sync(last_batch);
					fd_batch_reference(&last_batch, NULL);
				}
				assert(rsc->batch_mask == 0);
			} else {
				fd_batch_flush(write_batch, true);
			}
			assert(!rsc->write_batch);
		}

		fd_batch_reference(&write_batch, NULL);

		/* The GPU keeps track of how the various bo's are being used, and
		 * will wait if necessary for the proper operation to have
		 * completed.
		 */
		if (busy) {
			ret = fd_bo_cpu_prep(rsc->bo, ctx->screen->pipe, op);
			if (ret)
				goto fail;
		}
	}

	buf = fd_bo_map(rsc->bo);
	if (!buf)
		goto fail;

	offset = slice->offset +
		box->y / util_format_get_blockheight(format) * ptrans->stride +
		box->x / util_format_get_blockwidth(format) * rsc->cpp +
		fd_resource_layer_offset(rsc, slice, box->z);

	if (prsc->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
		prsc->format == PIPE_FORMAT_X32_S8X24_UINT) {
		assert(trans->base.box.depth == 1);

		trans->base.stride = trans->base.box.width * rsc->cpp * 2;
		trans->staging = malloc(trans->base.stride * trans->base.box.height);
		if (!trans->staging)
			goto fail;

		/* if we're not discarding the whole range (or resource), we must copy
		 * the real data in.
		 */
		if (!(usage & (PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE |
					   PIPE_TRANSFER_DISCARD_RANGE))) {
			struct fd_resource_slice *sslice =
				fd_resource_slice(rsc->stencil, level);
			void *sbuf = fd_bo_map(rsc->stencil->bo);
			if (!sbuf)
				goto fail;

			float *depth = (float *)(buf + slice->offset +
				fd_resource_layer_offset(rsc, slice, box->z) +
				box->y * slice->pitch * 4 + box->x * 4);
			uint8_t *stencil = sbuf + sslice->offset +
				fd_resource_layer_offset(rsc->stencil, sslice, box->z) +
				box->y * sslice->pitch + box->x;

			if (format != PIPE_FORMAT_X32_S8X24_UINT)
				util_format_z32_float_s8x24_uint_pack_z_float(
						trans->staging, trans->base.stride,
						depth, slice->pitch * 4,
						box->width, box->height);

			util_format_z32_float_s8x24_uint_pack_s_8uint(
					trans->staging, trans->base.stride,
					stencil, sslice->pitch,
					box->width, box->height);
		}

		buf = trans->staging;
		offset = 0;
	} else if (rsc->internal_format != format &&
			   util_format_description(format)->layout == UTIL_FORMAT_LAYOUT_RGTC) {
		assert(trans->base.box.depth == 1);

		trans->base.stride = util_format_get_stride(
				format, trans->base.box.width);
		trans->staging = malloc(
				util_format_get_2d_size(format, trans->base.stride,
										trans->base.box.height));
		if (!trans->staging)
			goto fail;

		/* if we're not discarding the whole range (or resource), we must copy
		 * the real data in.
		 */
		if (!(usage & (PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE |
					   PIPE_TRANSFER_DISCARD_RANGE))) {
			uint8_t *rgba8 = (uint8_t *)buf + slice->offset +
				fd_resource_layer_offset(rsc, slice, box->z) +
				box->y * slice->pitch * rsc->cpp + box->x * rsc->cpp;

			switch (format) {
			case PIPE_FORMAT_RGTC1_UNORM:
			case PIPE_FORMAT_RGTC1_SNORM:
			case PIPE_FORMAT_LATC1_UNORM:
			case PIPE_FORMAT_LATC1_SNORM:
				util_format_rgtc1_unorm_pack_rgba_8unorm(
					trans->staging, trans->base.stride,
					rgba8, slice->pitch * rsc->cpp,
					box->width, box->height);
				break;
			case PIPE_FORMAT_RGTC2_UNORM:
			case PIPE_FORMAT_RGTC2_SNORM:
			case PIPE_FORMAT_LATC2_UNORM:
			case PIPE_FORMAT_LATC2_SNORM:
				util_format_rgtc2_unorm_pack_rgba_8unorm(
					trans->staging, trans->base.stride,
					rgba8, slice->pitch * rsc->cpp,
					box->width, box->height);
				break;
			default:
				assert(!"Unexpected format");
				break;
			}
		}

		buf = trans->staging;
		offset = 0;
	}

	*pptrans = ptrans;

	return buf + offset;

fail:
	fd_resource_transfer_unmap(pctx, ptrans);
	return NULL;
}

static void
fd_resource_destroy(struct pipe_screen *pscreen,
		struct pipe_resource *prsc)
{
	struct fd_resource *rsc = fd_resource(prsc);
	fd_bc_invalidate_resource(rsc, true);
	if (rsc->bo)
		fd_bo_del(rsc->bo);
	util_range_destroy(&rsc->valid_buffer_range);
	FREE(rsc);
}

static boolean
fd_resource_get_handle(struct pipe_screen *pscreen,
		struct pipe_resource *prsc,
		struct winsys_handle *handle)
{
	struct fd_resource *rsc = fd_resource(prsc);

	return fd_screen_bo_get_handle(pscreen, rsc->bo,
			rsc->slices[0].pitch * rsc->cpp, handle);
}


static const struct u_resource_vtbl fd_resource_vtbl = {
		.resource_get_handle      = fd_resource_get_handle,
		.resource_destroy         = fd_resource_destroy,
		.transfer_map             = fd_resource_transfer_map,
		.transfer_flush_region    = fd_resource_transfer_flush_region,
		.transfer_unmap           = fd_resource_transfer_unmap,
};

static uint32_t
setup_slices(struct fd_resource *rsc, uint32_t alignment, enum pipe_format format)
{
	struct pipe_resource *prsc = &rsc->base.b;
	enum util_format_layout layout = util_format_description(format)->layout;
	uint32_t pitchalign = fd_screen(prsc->screen)->gmem_alignw;
	uint32_t level, size = 0;
	uint32_t width = prsc->width0;
	uint32_t height = prsc->height0;
	uint32_t depth = prsc->depth0;
	/* in layer_first layout, the level (slice) contains just one
	 * layer (since in fact the layer contains the slices)
	 */
	uint32_t layers_in_level = rsc->layer_first ? 1 : prsc->array_size;

	for (level = 0; level <= prsc->last_level; level++) {
		struct fd_resource_slice *slice = fd_resource_slice(rsc, level);
		uint32_t blocks;

		if (layout == UTIL_FORMAT_LAYOUT_ASTC)
			slice->pitch = width =
				util_align_npot(width, pitchalign * util_format_get_blockwidth(format));
		else
			slice->pitch = width = align(width, pitchalign);
		slice->offset = size;
		blocks = util_format_get_nblocks(format, width, height);
		/* 1d array and 2d array textures must all have the same layer size
		 * for each miplevel on a3xx. 3d textures can have different layer
		 * sizes for high levels, but the hw auto-sizer is buggy (or at least
		 * different than what this code does), so as soon as the layer size
		 * range gets into range, we stop reducing it.
		 */
		if (prsc->target == PIPE_TEXTURE_3D && (
					level == 1 ||
					(level > 1 && rsc->slices[level - 1].size0 > 0xf000)))
			slice->size0 = align(blocks * rsc->cpp, alignment);
		else if (level == 0 || rsc->layer_first || alignment == 1)
			slice->size0 = align(blocks * rsc->cpp, alignment);
		else
			slice->size0 = rsc->slices[level - 1].size0;

		size += slice->size0 * depth * layers_in_level;

		width = u_minify(width, 1);
		height = u_minify(height, 1);
		depth = u_minify(depth, 1);
	}

	return size;
}

static uint32_t
slice_alignment(struct pipe_screen *pscreen, const struct pipe_resource *tmpl)
{
	/* on a3xx, 2d array and 3d textures seem to want their
	 * layers aligned to page boundaries:
	 */
	switch (tmpl->target) {
	case PIPE_TEXTURE_3D:
	case PIPE_TEXTURE_1D_ARRAY:
	case PIPE_TEXTURE_2D_ARRAY:
		return 4096;
	default:
		return 1;
	}
}

/* special case to resize query buf after allocated.. */
void
fd_resource_resize(struct pipe_resource *prsc, uint32_t sz)
{
	struct fd_resource *rsc = fd_resource(prsc);

	debug_assert(prsc->width0 == 0);
	debug_assert(prsc->target == PIPE_BUFFER);
	debug_assert(prsc->bind == PIPE_BIND_QUERY_BUFFER);

	prsc->width0 = sz;
	realloc_bo(rsc, setup_slices(rsc, 1, prsc->format));
}

/**
 * Create a new texture object, using the given template info.
 */
static struct pipe_resource *
fd_resource_create(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl)
{
	struct fd_resource *rsc = CALLOC_STRUCT(fd_resource);
	struct pipe_resource *prsc = &rsc->base.b;
	enum pipe_format format = tmpl->format;
	uint32_t size, alignment;

	DBG("%p: target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
			"nr_samples=%u, usage=%u, bind=%x, flags=%x", prsc,
			tmpl->target, util_format_name(format),
			tmpl->width0, tmpl->height0, tmpl->depth0,
			tmpl->array_size, tmpl->last_level, tmpl->nr_samples,
			tmpl->usage, tmpl->bind, tmpl->flags);

	if (!rsc)
		return NULL;

	*prsc = *tmpl;

	pipe_reference_init(&prsc->reference, 1);

	prsc->screen = pscreen;

	util_range_init(&rsc->valid_buffer_range);

	rsc->base.vtbl = &fd_resource_vtbl;

	if (format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)
		format = PIPE_FORMAT_Z32_FLOAT;
	else if (fd_screen(pscreen)->gpu_id < 400 &&
			 util_format_description(format)->layout == UTIL_FORMAT_LAYOUT_RGTC)
		format = PIPE_FORMAT_R8G8B8A8_UNORM;
	rsc->internal_format = format;
	rsc->cpp = util_format_get_blocksize(format);

	assert(rsc->cpp);

	alignment = slice_alignment(pscreen, tmpl);
	if (is_a4xx(fd_screen(pscreen)) || is_a5xx(fd_screen(pscreen))) {
		switch (tmpl->target) {
		case PIPE_TEXTURE_3D:
			rsc->layer_first = false;
			break;
		default:
			rsc->layer_first = true;
			alignment = 1;
			break;
		}
	}

	size = setup_slices(rsc, alignment, format);

	/* special case for hw-query buffer, which we need to allocate before we
	 * know the size:
	 */
	if (size == 0) {
		/* note, semi-intention == instead of & */
		debug_assert(prsc->bind == PIPE_BIND_QUERY_BUFFER);
		return prsc;
	}

	if (rsc->layer_first) {
		rsc->layer_size = align(size, 4096);
		size = rsc->layer_size * prsc->array_size;
	}

	realloc_bo(rsc, size);
	if (!rsc->bo)
		goto fail;

	/* There is no native Z32F_S8 sampling or rendering format, so this must
	 * be emulated via two separate textures. The depth texture still keeps
	 * its Z32F_S8 format though, and we also keep a reference to a separate
	 * S8 texture.
	 */
	if (tmpl->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
		struct pipe_resource stencil = *tmpl;
		stencil.format = PIPE_FORMAT_S8_UINT;
		rsc->stencil = fd_resource(fd_resource_create(pscreen, &stencil));
		if (!rsc->stencil)
			goto fail;
	}

	return prsc;
fail:
	fd_resource_destroy(pscreen, prsc);
	return NULL;
}

/**
 * Create a texture from a winsys_handle. The handle is often created in
 * another process by first creating a pipe texture and then calling
 * resource_get_handle.
 */
static struct pipe_resource *
fd_resource_from_handle(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl,
		struct winsys_handle *handle, unsigned usage)
{
	struct fd_resource *rsc = CALLOC_STRUCT(fd_resource);
	struct fd_resource_slice *slice = &rsc->slices[0];
	struct pipe_resource *prsc = &rsc->base.b;
	uint32_t pitchalign = fd_screen(pscreen)->gmem_alignw;

	DBG("target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
			"nr_samples=%u, usage=%u, bind=%x, flags=%x",
			tmpl->target, util_format_name(tmpl->format),
			tmpl->width0, tmpl->height0, tmpl->depth0,
			tmpl->array_size, tmpl->last_level, tmpl->nr_samples,
			tmpl->usage, tmpl->bind, tmpl->flags);

	if (!rsc)
		return NULL;

	*prsc = *tmpl;

	pipe_reference_init(&prsc->reference, 1);

	prsc->screen = pscreen;

	util_range_init(&rsc->valid_buffer_range);

	rsc->bo = fd_screen_bo_from_handle(pscreen, handle);
	if (!rsc->bo)
		goto fail;

	rsc->base.vtbl = &fd_resource_vtbl;
	rsc->cpp = util_format_get_blocksize(tmpl->format);
	slice->pitch = handle->stride / rsc->cpp;
	slice->offset = handle->offset;
	slice->size0 = handle->stride * prsc->height0;

	if ((slice->pitch < align(prsc->width0, pitchalign)) ||
			(slice->pitch & (pitchalign - 1)))
		goto fail;

	assert(rsc->cpp);

	return prsc;

fail:
	fd_resource_destroy(pscreen, prsc);
	return NULL;
}

/**
 * _copy_region using pipe (3d engine)
 */
static bool
fd_blitter_pipe_copy_region(struct fd_context *ctx,
		struct pipe_resource *dst,
		unsigned dst_level,
		unsigned dstx, unsigned dsty, unsigned dstz,
		struct pipe_resource *src,
		unsigned src_level,
		const struct pipe_box *src_box)
{
	/* not until we allow rendertargets to be buffers */
	if (dst->target == PIPE_BUFFER || src->target == PIPE_BUFFER)
		return false;

	if (!util_blitter_is_copy_supported(ctx->blitter, dst, src))
		return false;

	/* TODO we could discard if dst box covers dst level fully.. */
	fd_blitter_pipe_begin(ctx, false, false, FD_STAGE_BLIT);
	util_blitter_copy_texture(ctx->blitter,
			dst, dst_level, dstx, dsty, dstz,
			src, src_level, src_box);
	fd_blitter_pipe_end(ctx);

	return true;
}

/**
 * Copy a block of pixels from one resource to another.
 * The resource must be of the same format.
 * Resources with nr_samples > 1 are not allowed.
 */
static void
fd_resource_copy_region(struct pipe_context *pctx,
		struct pipe_resource *dst,
		unsigned dst_level,
		unsigned dstx, unsigned dsty, unsigned dstz,
		struct pipe_resource *src,
		unsigned src_level,
		const struct pipe_box *src_box)
{
	struct fd_context *ctx = fd_context(pctx);

	/* TODO if we have 2d core, or other DMA engine that could be used
	 * for simple copies and reasonably easily synchronized with the 3d
	 * core, this is where we'd plug it in..
	 */

	/* try blit on 3d pipe: */
	if (fd_blitter_pipe_copy_region(ctx,
			dst, dst_level, dstx, dsty, dstz,
			src, src_level, src_box))
		return;

	/* else fallback to pure sw: */
	util_resource_copy_region(pctx,
			dst, dst_level, dstx, dsty, dstz,
			src, src_level, src_box);
}

bool
fd_render_condition_check(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);

	if (!ctx->cond_query)
		return true;

	union pipe_query_result res = { 0 };
	bool wait =
		ctx->cond_mode != PIPE_RENDER_COND_NO_WAIT &&
		ctx->cond_mode != PIPE_RENDER_COND_BY_REGION_NO_WAIT;

	if (pctx->get_query_result(pctx, ctx->cond_query, wait, &res))
			return (bool)res.u64 != ctx->cond_cond;

	return true;
}

/**
 * Optimal hardware path for blitting pixels.
 * Scaling, format conversion, up- and downsampling (resolve) are allowed.
 */
static void
fd_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
	struct fd_context *ctx = fd_context(pctx);
	struct pipe_blit_info info = *blit_info;
	bool discard = false;

	if (info.src.resource->nr_samples > 1 &&
			info.dst.resource->nr_samples <= 1 &&
			!util_format_is_depth_or_stencil(info.src.resource->format) &&
			!util_format_is_pure_integer(info.src.resource->format)) {
		DBG("color resolve unimplemented");
		return;
	}

	if (info.render_condition_enable && !fd_render_condition_check(pctx))
		return;

	if (!info.scissor_enable && !info.alpha_blend) {
		discard = util_texrange_covers_whole_level(info.dst.resource,
				info.dst.level, info.dst.box.x, info.dst.box.y,
				info.dst.box.z, info.dst.box.width,
				info.dst.box.height, info.dst.box.depth);
	}

	if (util_try_blit_via_copy_region(pctx, &info)) {
		return; /* done */
	}

	if (info.mask & PIPE_MASK_S) {
		DBG("cannot blit stencil, skipping");
		info.mask &= ~PIPE_MASK_S;
	}

	if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
		DBG("blit unsupported %s -> %s",
				util_format_short_name(info.src.resource->format),
				util_format_short_name(info.dst.resource->format));
		return;
	}

	fd_blitter_pipe_begin(ctx, info.render_condition_enable, discard, FD_STAGE_BLIT);
	util_blitter_blit(ctx->blitter, &info);
	fd_blitter_pipe_end(ctx);
}

void
fd_blitter_pipe_begin(struct fd_context *ctx, bool render_cond, bool discard,
		enum fd_render_stage stage)
{
	util_blitter_save_fragment_constant_buffer_slot(ctx->blitter,
			ctx->constbuf[PIPE_SHADER_FRAGMENT].cb);
	util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vtx.vertexbuf.vb);
	util_blitter_save_vertex_elements(ctx->blitter, ctx->vtx.vtx);
	util_blitter_save_vertex_shader(ctx->blitter, ctx->prog.vp);
	util_blitter_save_so_targets(ctx->blitter, ctx->streamout.num_targets,
			ctx->streamout.targets);
	util_blitter_save_rasterizer(ctx->blitter, ctx->rasterizer);
	util_blitter_save_viewport(ctx->blitter, &ctx->viewport);
	util_blitter_save_scissor(ctx->blitter, &ctx->scissor);
	util_blitter_save_fragment_shader(ctx->blitter, ctx->prog.fp);
	util_blitter_save_blend(ctx->blitter, ctx->blend);
	util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->zsa);
	util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref);
	util_blitter_save_sample_mask(ctx->blitter, ctx->sample_mask);
	util_blitter_save_framebuffer(ctx->blitter,
			ctx->batch ? &ctx->batch->framebuffer : NULL);
	util_blitter_save_fragment_sampler_states(ctx->blitter,
			ctx->fragtex.num_samplers,
			(void **)ctx->fragtex.samplers);
	util_blitter_save_fragment_sampler_views(ctx->blitter,
			ctx->fragtex.num_textures, ctx->fragtex.textures);
	if (!render_cond)
		util_blitter_save_render_condition(ctx->blitter,
			ctx->cond_query, ctx->cond_cond, ctx->cond_mode);

	if (ctx->batch)
		fd_hw_query_set_stage(ctx->batch, ctx->batch->draw, stage);

	ctx->in_blit = discard;
}

void
fd_blitter_pipe_end(struct fd_context *ctx)
{
	if (ctx->batch)
		fd_hw_query_set_stage(ctx->batch, ctx->batch->draw, FD_STAGE_NULL);
	ctx->in_blit = false;
}

static void
fd_flush_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
	struct fd_resource *rsc = fd_resource(prsc);

	if (rsc->write_batch)
		fd_batch_flush(rsc->write_batch, true);

	assert(!rsc->write_batch);
}

void
fd_resource_screen_init(struct pipe_screen *pscreen)
{
	pscreen->resource_create = fd_resource_create;
	pscreen->resource_from_handle = fd_resource_from_handle;
	pscreen->resource_get_handle = u_resource_get_handle_vtbl;
	pscreen->resource_destroy = u_resource_destroy_vtbl;
}

void
fd_resource_context_init(struct pipe_context *pctx)
{
	pctx->transfer_map = u_transfer_map_vtbl;
	pctx->transfer_flush_region = u_transfer_flush_region_vtbl;
	pctx->transfer_unmap = u_transfer_unmap_vtbl;
	pctx->buffer_subdata = u_default_buffer_subdata;
	pctx->texture_subdata = u_default_texture_subdata;
	pctx->create_surface = fd_create_surface;
	pctx->surface_destroy = fd_surface_destroy;
	pctx->resource_copy_region = fd_resource_copy_region;
	pctx->blit = fd_blit;
	pctx->flush_resource = fd_flush_resource;
}
