/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2018 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

#include "radeonsi/si_pipe.h"
#include "radeonsi/si_query.h"
#include "util/u_format.h"
#include "util/u_log.h"
#include "util/u_memory.h"
#include "util/u_pack_color.h"
#include "util/u_resource.h"
#include "util/u_surface.h"
#include "util/os_time.h"
#include <errno.h>
#include <inttypes.h>
#include "state_tracker/drm_driver.h"
#include "amd/common/sid.h"

static enum radeon_surf_mode
si_choose_tiling(struct si_screen *sscreen,
		 const struct pipe_resource *templ, bool tc_compatible_htile);


bool si_prepare_for_dma_blit(struct si_context *sctx,
			     struct r600_texture *rdst,
			     unsigned dst_level, unsigned dstx,
			     unsigned dsty, unsigned dstz,
			     struct r600_texture *rsrc,
			     unsigned src_level,
			     const struct pipe_box *src_box)
{
	if (!sctx->dma_cs)
		return false;

	if (rdst->surface.bpe != rsrc->surface.bpe)
		return false;

	/* MSAA: Blits don't exist in the real world. */
	if (rsrc->resource.b.b.nr_samples > 1 ||
	    rdst->resource.b.b.nr_samples > 1)
		return false;

	/* Depth-stencil surfaces:
	 *   When dst is linear, the DB->CB copy preserves HTILE.
	 *   When dst is tiled, the 3D path must be used to update HTILE.
	 */
	if (rsrc->is_depth || rdst->is_depth)
		return false;

	/* DCC as:
	 *   src: Use the 3D path. DCC decompression is expensive.
	 *   dst: Use the 3D path to compress the pixels with DCC.
	 */
	if (vi_dcc_enabled(rsrc, src_level) ||
	    vi_dcc_enabled(rdst, dst_level))
		return false;

	/* CMASK as:
	 *   src: Both texture and SDMA paths need decompression. Use SDMA.
	 *   dst: If overwriting the whole texture, discard CMASK and use
	 *        SDMA. Otherwise, use the 3D path.
	 */
	if (rdst->cmask.size && rdst->dirty_level_mask & (1 << dst_level)) {
		/* The CMASK clear is only enabled for the first level. */
		assert(dst_level == 0);
		if (!util_texrange_covers_whole_level(&rdst->resource.b.b, dst_level,
						      dstx, dsty, dstz, src_box->width,
						      src_box->height, src_box->depth))
			return false;

		si_texture_discard_cmask(sctx->screen, rdst);
	}

	/* All requirements are met. Prepare textures for SDMA. */
	if (rsrc->cmask.size && rsrc->dirty_level_mask & (1 << src_level))
		sctx->b.flush_resource(&sctx->b, &rsrc->resource.b.b);

	assert(!(rsrc->dirty_level_mask & (1 << src_level)));
	assert(!(rdst->dirty_level_mask & (1 << dst_level)));

	return true;
}

/* Same as resource_copy_region, except that both upsampling and downsampling are allowed. */
static void si_copy_region_with_blit(struct pipe_context *pipe,
				     struct pipe_resource *dst,
				     unsigned dst_level,
				     unsigned dstx, unsigned dsty, unsigned dstz,
				     struct pipe_resource *src,
				     unsigned src_level,
				     const struct pipe_box *src_box)
{
	struct pipe_blit_info blit;

	memset(&blit, 0, sizeof(blit));
	blit.src.resource = src;
	blit.src.format = src->format;
	blit.src.level = src_level;
	blit.src.box = *src_box;
	blit.dst.resource = dst;
	blit.dst.format = dst->format;
	blit.dst.level = dst_level;
	blit.dst.box.x = dstx;
	blit.dst.box.y = dsty;
	blit.dst.box.z = dstz;
	blit.dst.box.width = src_box->width;
	blit.dst.box.height = src_box->height;
	blit.dst.box.depth = src_box->depth;
	blit.mask = util_format_get_mask(src->format) &
		    util_format_get_mask(dst->format);
	blit.filter = PIPE_TEX_FILTER_NEAREST;

	if (blit.mask) {
		pipe->blit(pipe, &blit);
	}
}

/* Copy from a full GPU texture to a transfer's staging one. */
static void si_copy_to_staging_texture(struct pipe_context *ctx, struct r600_transfer *rtransfer)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct pipe_transfer *transfer = (struct pipe_transfer*)rtransfer;
	struct pipe_resource *dst = &rtransfer->staging->b.b;
	struct pipe_resource *src = transfer->resource;

	if (src->nr_samples > 1) {
		si_copy_region_with_blit(ctx, dst, 0, 0, 0, 0,
					   src, transfer->level, &transfer->box);
		return;
	}

	sctx->dma_copy(ctx, dst, 0, 0, 0, 0, src, transfer->level,
		       &transfer->box);
}

/* Copy from a transfer's staging texture to a full GPU one. */
static void si_copy_from_staging_texture(struct pipe_context *ctx, struct r600_transfer *rtransfer)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct pipe_transfer *transfer = (struct pipe_transfer*)rtransfer;
	struct pipe_resource *dst = transfer->resource;
	struct pipe_resource *src = &rtransfer->staging->b.b;
	struct pipe_box sbox;

	u_box_3d(0, 0, 0, transfer->box.width, transfer->box.height, transfer->box.depth, &sbox);

	if (dst->nr_samples > 1) {
		si_copy_region_with_blit(ctx, dst, transfer->level,
					   transfer->box.x, transfer->box.y, transfer->box.z,
					   src, 0, &sbox);
		return;
	}

	sctx->dma_copy(ctx, dst, transfer->level,
		       transfer->box.x, transfer->box.y, transfer->box.z,
		       src, 0, &sbox);
}

static unsigned si_texture_get_offset(struct si_screen *sscreen,
				      struct r600_texture *rtex, unsigned level,
				      const struct pipe_box *box,
				      unsigned *stride,
				      unsigned *layer_stride)
{
	if (sscreen->info.chip_class >= GFX9) {
		*stride = rtex->surface.u.gfx9.surf_pitch * rtex->surface.bpe;
		*layer_stride = rtex->surface.u.gfx9.surf_slice_size;

		if (!box)
			return 0;

		/* Each texture is an array of slices. Each slice is an array
		 * of mipmap levels. */
		return box->z * rtex->surface.u.gfx9.surf_slice_size +
		       rtex->surface.u.gfx9.offset[level] +
		       (box->y / rtex->surface.blk_h *
			rtex->surface.u.gfx9.surf_pitch +
			box->x / rtex->surface.blk_w) * rtex->surface.bpe;
	} else {
		*stride = rtex->surface.u.legacy.level[level].nblk_x *
			  rtex->surface.bpe;
		assert((uint64_t)rtex->surface.u.legacy.level[level].slice_size_dw * 4 <= UINT_MAX);
		*layer_stride = (uint64_t)rtex->surface.u.legacy.level[level].slice_size_dw * 4;

		if (!box)
			return rtex->surface.u.legacy.level[level].offset;

		/* Each texture is an array of mipmap levels. Each level is
		 * an array of slices. */
		return rtex->surface.u.legacy.level[level].offset +
		       box->z * (uint64_t)rtex->surface.u.legacy.level[level].slice_size_dw * 4 +
		       (box->y / rtex->surface.blk_h *
		        rtex->surface.u.legacy.level[level].nblk_x +
		        box->x / rtex->surface.blk_w) * rtex->surface.bpe;
	}
}

static int si_init_surface(struct si_screen *sscreen,
			   struct radeon_surf *surface,
			   const struct pipe_resource *ptex,
			   enum radeon_surf_mode array_mode,
			   unsigned pitch_in_bytes_override,
			   unsigned offset,
			   bool is_imported,
			   bool is_scanout,
			   bool is_flushed_depth,
			   bool tc_compatible_htile)
{
	const struct util_format_description *desc =
		util_format_description(ptex->format);
	bool is_depth, is_stencil;
	int r;
	unsigned i, bpe, flags = 0;

	is_depth = util_format_has_depth(desc);
	is_stencil = util_format_has_stencil(desc);

	if (!is_flushed_depth &&
	    ptex->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
		bpe = 4; /* stencil is allocated separately on evergreen */
	} else {
		bpe = util_format_get_blocksize(ptex->format);
		assert(util_is_power_of_two_or_zero(bpe));
	}

	if (!is_flushed_depth && is_depth) {
		flags |= RADEON_SURF_ZBUFFER;

		if (tc_compatible_htile &&
		    (sscreen->info.chip_class >= GFX9 ||
		     array_mode == RADEON_SURF_MODE_2D)) {
			/* TC-compatible HTILE only supports Z32_FLOAT.
			 * GFX9 also supports Z16_UNORM.
			 * On VI, promote Z16 to Z32. DB->CB copies will convert
			 * the format for transfers.
			 */
			if (sscreen->info.chip_class == VI)
				bpe = 4;

			flags |= RADEON_SURF_TC_COMPATIBLE_HTILE;
		}

		if (is_stencil)
			flags |= RADEON_SURF_SBUFFER;
	}

	if (sscreen->info.chip_class >= VI &&
	    (ptex->flags & SI_RESOURCE_FLAG_DISABLE_DCC ||
	     ptex->format == PIPE_FORMAT_R9G9B9E5_FLOAT ||
	     /* DCC MSAA array textures are disallowed due to incomplete clear impl. */
	     (ptex->nr_samples >= 2 &&
	      (!sscreen->dcc_msaa_allowed || ptex->array_size > 1))))
		flags |= RADEON_SURF_DISABLE_DCC;

	if (ptex->bind & PIPE_BIND_SCANOUT || is_scanout) {
		/* This should catch bugs in gallium users setting incorrect flags. */
		assert(ptex->nr_samples <= 1 &&
		       ptex->array_size == 1 &&
		       ptex->depth0 == 1 &&
		       ptex->last_level == 0 &&
		       !(flags & RADEON_SURF_Z_OR_SBUFFER));

		flags |= RADEON_SURF_SCANOUT;
	}

	if (ptex->bind & PIPE_BIND_SHARED)
		flags |= RADEON_SURF_SHAREABLE;
	if (is_imported)
		flags |= RADEON_SURF_IMPORTED | RADEON_SURF_SHAREABLE;
	if (!(ptex->flags & SI_RESOURCE_FLAG_FORCE_TILING))
		flags |= RADEON_SURF_OPTIMIZE_FOR_SPACE;

	r = sscreen->ws->surface_init(sscreen->ws, ptex, flags, bpe,
				      array_mode, surface);
	if (r) {
		return r;
	}

	unsigned pitch = pitch_in_bytes_override / bpe;

	if (sscreen->info.chip_class >= GFX9) {
		if (pitch) {
			surface->u.gfx9.surf_pitch = pitch;
			surface->u.gfx9.surf_slice_size =
				(uint64_t)pitch * surface->u.gfx9.surf_height * bpe;
		}
		surface->u.gfx9.surf_offset = offset;
	} else {
		if (pitch) {
			surface->u.legacy.level[0].nblk_x = pitch;
			surface->u.legacy.level[0].slice_size_dw =
				((uint64_t)pitch * surface->u.legacy.level[0].nblk_y * bpe) / 4;
		}
		if (offset) {
			for (i = 0; i < ARRAY_SIZE(surface->u.legacy.level); ++i)
				surface->u.legacy.level[i].offset += offset;
		}
	}
	return 0;
}

static void si_texture_init_metadata(struct si_screen *sscreen,
				     struct r600_texture *rtex,
				     struct radeon_bo_metadata *metadata)
{
	struct radeon_surf *surface = &rtex->surface;

	memset(metadata, 0, sizeof(*metadata));

	if (sscreen->info.chip_class >= GFX9) {
		metadata->u.gfx9.swizzle_mode = surface->u.gfx9.surf.swizzle_mode;
	} else {
		metadata->u.legacy.microtile = surface->u.legacy.level[0].mode >= RADEON_SURF_MODE_1D ?
					   RADEON_LAYOUT_TILED : RADEON_LAYOUT_LINEAR;
		metadata->u.legacy.macrotile = surface->u.legacy.level[0].mode >= RADEON_SURF_MODE_2D ?
					   RADEON_LAYOUT_TILED : RADEON_LAYOUT_LINEAR;
		metadata->u.legacy.pipe_config = surface->u.legacy.pipe_config;
		metadata->u.legacy.bankw = surface->u.legacy.bankw;
		metadata->u.legacy.bankh = surface->u.legacy.bankh;
		metadata->u.legacy.tile_split = surface->u.legacy.tile_split;
		metadata->u.legacy.mtilea = surface->u.legacy.mtilea;
		metadata->u.legacy.num_banks = surface->u.legacy.num_banks;
		metadata->u.legacy.stride = surface->u.legacy.level[0].nblk_x * surface->bpe;
		metadata->u.legacy.scanout = (surface->flags & RADEON_SURF_SCANOUT) != 0;
	}
}

static void si_surface_import_metadata(struct si_screen *sscreen,
				       struct radeon_surf *surf,
				       struct radeon_bo_metadata *metadata,
				       enum radeon_surf_mode *array_mode,
				       bool *is_scanout)
{
	if (sscreen->info.chip_class >= GFX9) {
		if (metadata->u.gfx9.swizzle_mode > 0)
			*array_mode = RADEON_SURF_MODE_2D;
		else
			*array_mode = RADEON_SURF_MODE_LINEAR_ALIGNED;

		*is_scanout = metadata->u.gfx9.swizzle_mode == 0 ||
			      metadata->u.gfx9.swizzle_mode % 4 == 2;

		surf->u.gfx9.surf.swizzle_mode = metadata->u.gfx9.swizzle_mode;
	} else {
		surf->u.legacy.pipe_config = metadata->u.legacy.pipe_config;
		surf->u.legacy.bankw = metadata->u.legacy.bankw;
		surf->u.legacy.bankh = metadata->u.legacy.bankh;
		surf->u.legacy.tile_split = metadata->u.legacy.tile_split;
		surf->u.legacy.mtilea = metadata->u.legacy.mtilea;
		surf->u.legacy.num_banks = metadata->u.legacy.num_banks;

		if (metadata->u.legacy.macrotile == RADEON_LAYOUT_TILED)
			*array_mode = RADEON_SURF_MODE_2D;
		else if (metadata->u.legacy.microtile == RADEON_LAYOUT_TILED)
			*array_mode = RADEON_SURF_MODE_1D;
		else
			*array_mode = RADEON_SURF_MODE_LINEAR_ALIGNED;

		*is_scanout = metadata->u.legacy.scanout;
	}
}

void si_eliminate_fast_color_clear(struct si_context *sctx,
				   struct r600_texture *rtex)
{
	struct si_screen *sscreen = sctx->screen;
	struct pipe_context *ctx = &sctx->b;

	if (ctx == sscreen->aux_context)
		mtx_lock(&sscreen->aux_context_lock);

	unsigned n = sctx->num_decompress_calls;
	ctx->flush_resource(ctx, &rtex->resource.b.b);

	/* Flush only if any fast clear elimination took place. */
	if (n != sctx->num_decompress_calls)
		ctx->flush(ctx, NULL, 0);

	if (ctx == sscreen->aux_context)
		mtx_unlock(&sscreen->aux_context_lock);
}

void si_texture_discard_cmask(struct si_screen *sscreen,
			      struct r600_texture *rtex)
{
	if (!rtex->cmask.size)
		return;

	assert(rtex->resource.b.b.nr_samples <= 1);

	/* Disable CMASK. */
	memset(&rtex->cmask, 0, sizeof(rtex->cmask));
	rtex->cmask.base_address_reg = rtex->resource.gpu_address >> 8;
	rtex->dirty_level_mask = 0;

	rtex->cb_color_info &= ~S_028C70_FAST_CLEAR(1);

	if (rtex->cmask_buffer != &rtex->resource)
	    r600_resource_reference(&rtex->cmask_buffer, NULL);

	/* Notify all contexts about the change. */
	p_atomic_inc(&sscreen->dirty_tex_counter);
	p_atomic_inc(&sscreen->compressed_colortex_counter);
}

static bool si_can_disable_dcc(struct r600_texture *rtex)
{
	/* We can't disable DCC if it can be written by another process. */
	return rtex->dcc_offset &&
	       (!rtex->resource.b.is_shared ||
		!(rtex->resource.external_usage & PIPE_HANDLE_USAGE_WRITE));
}

static bool si_texture_discard_dcc(struct si_screen *sscreen,
				   struct r600_texture *rtex)
{
	if (!si_can_disable_dcc(rtex))
		return false;

	assert(rtex->dcc_separate_buffer == NULL);

	/* Disable DCC. */
	rtex->dcc_offset = 0;

	/* Notify all contexts about the change. */
	p_atomic_inc(&sscreen->dirty_tex_counter);
	return true;
}

/**
 * Disable DCC for the texture. (first decompress, then discard metadata).
 *
 * There is unresolved multi-context synchronization issue between
 * screen::aux_context and the current context. If applications do this with
 * multiple contexts, it's already undefined behavior for them and we don't
 * have to worry about that. The scenario is:
 *
 * If context 1 disables DCC and context 2 has queued commands that write
 * to the texture via CB with DCC enabled, and the order of operations is
 * as follows:
 *   context 2 queues draw calls rendering to the texture, but doesn't flush
 *   context 1 disables DCC and flushes
 *   context 1 & 2 reset descriptors and FB state
 *   context 2 flushes (new compressed tiles written by the draw calls)
 *   context 1 & 2 read garbage, because DCC is disabled, yet there are
 *   compressed tiled
 *
 * \param sctx  the current context if you have one, or rscreen->aux_context
 *              if you don't.
 */
bool si_texture_disable_dcc(struct si_context *sctx,
			    struct r600_texture *rtex)
{
	struct si_screen *sscreen = sctx->screen;

	if (!si_can_disable_dcc(rtex))
		return false;

	if (&sctx->b == sscreen->aux_context)
		mtx_lock(&sscreen->aux_context_lock);

	/* Decompress DCC. */
	si_decompress_dcc(sctx, rtex);
	sctx->b.flush(&sctx->b, NULL, 0);

	if (&sctx->b == sscreen->aux_context)
		mtx_unlock(&sscreen->aux_context_lock);

	return si_texture_discard_dcc(sscreen, rtex);
}

static void si_reallocate_texture_inplace(struct si_context *sctx,
					  struct r600_texture *rtex,
					  unsigned new_bind_flag,
					  bool invalidate_storage)
{
	struct pipe_screen *screen = sctx->b.screen;
	struct r600_texture *new_tex;
	struct pipe_resource templ = rtex->resource.b.b;
	unsigned i;

	templ.bind |= new_bind_flag;

	if (rtex->resource.b.is_shared)
		return;

	if (new_bind_flag == PIPE_BIND_LINEAR) {
		if (rtex->surface.is_linear)
			return;

		/* This fails with MSAA, depth, and compressed textures. */
		if (si_choose_tiling(sctx->screen, &templ, false) !=
		    RADEON_SURF_MODE_LINEAR_ALIGNED)
			return;
	}

	new_tex = (struct r600_texture*)screen->resource_create(screen, &templ);
	if (!new_tex)
		return;

	/* Copy the pixels to the new texture. */
	if (!invalidate_storage) {
		for (i = 0; i <= templ.last_level; i++) {
			struct pipe_box box;

			u_box_3d(0, 0, 0,
				 u_minify(templ.width0, i), u_minify(templ.height0, i),
				 util_num_layers(&templ, i), &box);

			sctx->dma_copy(&sctx->b, &new_tex->resource.b.b, i, 0, 0, 0,
				       &rtex->resource.b.b, i, &box);
		}
	}

	if (new_bind_flag == PIPE_BIND_LINEAR) {
		si_texture_discard_cmask(sctx->screen, rtex);
		si_texture_discard_dcc(sctx->screen, rtex);
	}

	/* Replace the structure fields of rtex. */
	rtex->resource.b.b.bind = templ.bind;
	pb_reference(&rtex->resource.buf, new_tex->resource.buf);
	rtex->resource.gpu_address = new_tex->resource.gpu_address;
	rtex->resource.vram_usage = new_tex->resource.vram_usage;
	rtex->resource.gart_usage = new_tex->resource.gart_usage;
	rtex->resource.bo_size = new_tex->resource.bo_size;
	rtex->resource.bo_alignment = new_tex->resource.bo_alignment;
	rtex->resource.domains = new_tex->resource.domains;
	rtex->resource.flags = new_tex->resource.flags;
	rtex->size = new_tex->size;
	rtex->db_render_format = new_tex->db_render_format;
	rtex->db_compatible = new_tex->db_compatible;
	rtex->can_sample_z = new_tex->can_sample_z;
	rtex->can_sample_s = new_tex->can_sample_s;
	rtex->surface = new_tex->surface;
	rtex->fmask = new_tex->fmask;
	rtex->cmask = new_tex->cmask;
	rtex->cb_color_info = new_tex->cb_color_info;
	rtex->last_msaa_resolve_target_micro_mode = new_tex->last_msaa_resolve_target_micro_mode;
	rtex->htile_offset = new_tex->htile_offset;
	rtex->tc_compatible_htile = new_tex->tc_compatible_htile;
	rtex->depth_cleared = new_tex->depth_cleared;
	rtex->stencil_cleared = new_tex->stencil_cleared;
	rtex->dcc_gather_statistics = new_tex->dcc_gather_statistics;
	rtex->framebuffers_bound = new_tex->framebuffers_bound;

	if (new_bind_flag == PIPE_BIND_LINEAR) {
		assert(!rtex->htile_offset);
		assert(!rtex->cmask.size);
		assert(!rtex->fmask.size);
		assert(!rtex->dcc_offset);
		assert(!rtex->is_depth);
	}

	r600_texture_reference(&new_tex, NULL);

	p_atomic_inc(&sctx->screen->dirty_tex_counter);
}

static uint32_t si_get_bo_metadata_word1(struct si_screen *sscreen)
{
	return (ATI_VENDOR_ID << 16) | sscreen->info.pci_id;
}

static void si_query_opaque_metadata(struct si_screen *sscreen,
				     struct r600_texture *rtex,
			             struct radeon_bo_metadata *md)
{
	struct pipe_resource *res = &rtex->resource.b.b;
	static const unsigned char swizzle[] = {
		PIPE_SWIZZLE_X,
		PIPE_SWIZZLE_Y,
		PIPE_SWIZZLE_Z,
		PIPE_SWIZZLE_W
	};
	uint32_t desc[8], i;
	bool is_array = util_texture_is_array(res->target);

	/* DRM 2.x.x doesn't support this. */
	if (sscreen->info.drm_major != 3)
		return;

	assert(rtex->dcc_separate_buffer == NULL);
	assert(rtex->fmask.size == 0);

	/* Metadata image format format version 1:
	 * [0] = 1 (metadata format identifier)
	 * [1] = (VENDOR_ID << 16) | PCI_ID
	 * [2:9] = image descriptor for the whole resource
	 *         [2] is always 0, because the base address is cleared
	 *         [9] is the DCC offset bits [39:8] from the beginning of
	 *             the buffer
	 * [10:10+LAST_LEVEL] = mipmap level offset bits [39:8] for each level
	 */

	md->metadata[0] = 1; /* metadata image format version 1 */

	/* TILE_MODE_INDEX is ambiguous without a PCI ID. */
	md->metadata[1] = si_get_bo_metadata_word1(sscreen);

	si_make_texture_descriptor(sscreen, rtex, true,
				   res->target, res->format,
				   swizzle, 0, res->last_level, 0,
				   is_array ? res->array_size - 1 : 0,
				   res->width0, res->height0, res->depth0,
				   desc, NULL);

	si_set_mutable_tex_desc_fields(sscreen, rtex, &rtex->surface.u.legacy.level[0],
				       0, 0, rtex->surface.blk_w, false, desc);

	/* Clear the base address and set the relative DCC offset. */
	desc[0] = 0;
	desc[1] &= C_008F14_BASE_ADDRESS_HI;
	desc[7] = rtex->dcc_offset >> 8;

	/* Dwords [2:9] contain the image descriptor. */
	memcpy(&md->metadata[2], desc, sizeof(desc));
	md->size_metadata = 10 * 4;

	/* Dwords [10:..] contain the mipmap level offsets. */
	if (sscreen->info.chip_class <= VI) {
		for (i = 0; i <= res->last_level; i++)
			md->metadata[10+i] = rtex->surface.u.legacy.level[i].offset >> 8;

		md->size_metadata += (1 + res->last_level) * 4;
	}
}

static void si_apply_opaque_metadata(struct si_screen *sscreen,
				     struct r600_texture *rtex,
			             struct radeon_bo_metadata *md)
{
	uint32_t *desc = &md->metadata[2];

	if (sscreen->info.chip_class < VI)
		return;

	/* Return if DCC is enabled. The texture should be set up with it
	 * already.
	 */
	if (md->size_metadata >= 10 * 4 && /* at least 2(header) + 8(desc) dwords */
	    md->metadata[0] != 0 &&
	    md->metadata[1] == si_get_bo_metadata_word1(sscreen) &&
	    G_008F28_COMPRESSION_EN(desc[6])) {
		rtex->dcc_offset = (uint64_t)desc[7] << 8;
		return;
	}

	/* Disable DCC. These are always set by texture_from_handle and must
	 * be cleared here.
	 */
	rtex->dcc_offset = 0;
}

static boolean si_texture_get_handle(struct pipe_screen* screen,
				     struct pipe_context *ctx,
				     struct pipe_resource *resource,
				     struct winsys_handle *whandle,
				     unsigned usage)
{
	struct si_screen *sscreen = (struct si_screen*)screen;
	struct si_context *sctx;
	struct r600_resource *res = (struct r600_resource*)resource;
	struct r600_texture *rtex = (struct r600_texture*)resource;
	struct radeon_bo_metadata metadata;
	bool update_metadata = false;
	unsigned stride, offset, slice_size;
	bool flush = false;

	ctx = threaded_context_unwrap_sync(ctx);
	sctx = (struct si_context*)(ctx ? ctx : sscreen->aux_context);

	if (resource->target != PIPE_BUFFER) {
		/* This is not supported now, but it might be required for OpenCL
		 * interop in the future.
		 */
		if (resource->nr_samples > 1 || rtex->is_depth)
			return false;

		/* Move a suballocated texture into a non-suballocated allocation. */
		if (sscreen->ws->buffer_is_suballocated(res->buf) ||
		    rtex->surface.tile_swizzle ||
		    (rtex->resource.flags & RADEON_FLAG_NO_INTERPROCESS_SHARING &&
		     sscreen->info.has_local_buffers &&
		     whandle->type != DRM_API_HANDLE_TYPE_KMS)) {
			assert(!res->b.is_shared);
			si_reallocate_texture_inplace(sctx, rtex,
							PIPE_BIND_SHARED, false);
			flush = true;
			assert(res->b.b.bind & PIPE_BIND_SHARED);
			assert(res->flags & RADEON_FLAG_NO_SUBALLOC);
			assert(!(res->flags & RADEON_FLAG_NO_INTERPROCESS_SHARING));
			assert(rtex->surface.tile_swizzle == 0);
		}

		/* Since shader image stores don't support DCC on VI,
		 * disable it for external clients that want write
		 * access.
		 */
		if (usage & PIPE_HANDLE_USAGE_WRITE && rtex->dcc_offset) {
			if (si_texture_disable_dcc(sctx, rtex)) {
				update_metadata = true;
				/* si_texture_disable_dcc flushes the context */
				flush = false;
			}
		}

		if (!(usage & PIPE_HANDLE_USAGE_EXPLICIT_FLUSH) &&
		    (rtex->cmask.size || rtex->dcc_offset)) {
			/* Eliminate fast clear (both CMASK and DCC) */
			si_eliminate_fast_color_clear(sctx, rtex);
			/* eliminate_fast_color_clear flushes the context */
			flush = false;

			/* Disable CMASK if flush_resource isn't going
			 * to be called.
			 */
			if (rtex->cmask.size)
				si_texture_discard_cmask(sscreen, rtex);
		}

		/* Set metadata. */
		if (!res->b.is_shared || update_metadata) {
			si_texture_init_metadata(sscreen, rtex, &metadata);
			si_query_opaque_metadata(sscreen, rtex, &metadata);

			sscreen->ws->buffer_set_metadata(res->buf, &metadata);
		}

		if (sscreen->info.chip_class >= GFX9) {
			offset = rtex->surface.u.gfx9.surf_offset;
			stride = rtex->surface.u.gfx9.surf_pitch *
				 rtex->surface.bpe;
			slice_size = rtex->surface.u.gfx9.surf_slice_size;
		} else {
			offset = rtex->surface.u.legacy.level[0].offset;
			stride = rtex->surface.u.legacy.level[0].nblk_x *
				 rtex->surface.bpe;
			slice_size = (uint64_t)rtex->surface.u.legacy.level[0].slice_size_dw * 4;
		}
	} else {
		/* Buffer exports are for the OpenCL interop. */
		/* Move a suballocated buffer into a non-suballocated allocation. */
		if (sscreen->ws->buffer_is_suballocated(res->buf) ||
		    /* A DMABUF export always fails if the BO is local. */
		    (rtex->resource.flags & RADEON_FLAG_NO_INTERPROCESS_SHARING &&
		     sscreen->info.has_local_buffers)) {
			assert(!res->b.is_shared);

			/* Allocate a new buffer with PIPE_BIND_SHARED. */
			struct pipe_resource templ = res->b.b;
			templ.bind |= PIPE_BIND_SHARED;

			struct pipe_resource *newb =
				screen->resource_create(screen, &templ);
			if (!newb)
				return false;

			/* Copy the old buffer contents to the new one. */
			struct pipe_box box;
			u_box_1d(0, newb->width0, &box);
			sctx->b.resource_copy_region(&sctx->b, newb, 0, 0, 0, 0,
						     &res->b.b, 0, &box);
			flush = true;
			/* Move the new buffer storage to the old pipe_resource. */
			si_replace_buffer_storage(&sctx->b, &res->b.b, newb);
			pipe_resource_reference(&newb, NULL);

			assert(res->b.b.bind & PIPE_BIND_SHARED);
			assert(res->flags & RADEON_FLAG_NO_SUBALLOC);
		}

		/* Buffers */
		offset = 0;
		stride = 0;
		slice_size = 0;
	}

	if (flush)
		sctx->b.flush(&sctx->b, NULL, 0);

	if (res->b.is_shared) {
		/* USAGE_EXPLICIT_FLUSH must be cleared if at least one user
		 * doesn't set it.
		 */
		res->external_usage |= usage & ~PIPE_HANDLE_USAGE_EXPLICIT_FLUSH;
		if (!(usage & PIPE_HANDLE_USAGE_EXPLICIT_FLUSH))
			res->external_usage &= ~PIPE_HANDLE_USAGE_EXPLICIT_FLUSH;
	} else {
		res->b.is_shared = true;
		res->external_usage = usage;
	}

	return sscreen->ws->buffer_get_handle(res->buf, stride, offset,
					      slice_size, whandle);
}

static void si_texture_destroy(struct pipe_screen *screen,
			       struct pipe_resource *ptex)
{
	struct r600_texture *rtex = (struct r600_texture*)ptex;
	struct r600_resource *resource = &rtex->resource;

	r600_texture_reference(&rtex->flushed_depth_texture, NULL);

	if (rtex->cmask_buffer != &rtex->resource) {
	    r600_resource_reference(&rtex->cmask_buffer, NULL);
	}
	pb_reference(&resource->buf, NULL);
	r600_resource_reference(&rtex->dcc_separate_buffer, NULL);
	r600_resource_reference(&rtex->last_dcc_separate_buffer, NULL);
	FREE(rtex);
}

static const struct u_resource_vtbl si_texture_vtbl;

/* The number of samples can be specified independently of the texture. */
void si_texture_get_fmask_info(struct si_screen *sscreen,
			       struct r600_texture *rtex,
			       unsigned nr_samples,
			       struct r600_fmask_info *out)
{
	/* FMASK is allocated like an ordinary texture. */
	struct pipe_resource templ = rtex->resource.b.b;
	struct radeon_surf fmask = {};
	unsigned flags, bpe;

	memset(out, 0, sizeof(*out));

	if (sscreen->info.chip_class >= GFX9) {
		out->alignment = rtex->surface.u.gfx9.fmask_alignment;
		out->size = rtex->surface.u.gfx9.fmask_size;
		out->tile_swizzle = rtex->surface.u.gfx9.fmask_tile_swizzle;
		return;
	}

	templ.nr_samples = 1;
	flags = rtex->surface.flags | RADEON_SURF_FMASK;

	switch (nr_samples) {
	case 2:
	case 4:
		bpe = 1;
		break;
	case 8:
		bpe = 4;
		break;
	default:
		PRINT_ERR("Invalid sample count for FMASK allocation.\n");
		return;
	}

	if (sscreen->ws->surface_init(sscreen->ws, &templ, flags, bpe,
				      RADEON_SURF_MODE_2D, &fmask)) {
		PRINT_ERR("Got error in surface_init while allocating FMASK.\n");
		return;
	}

	assert(fmask.u.legacy.level[0].mode == RADEON_SURF_MODE_2D);

	out->slice_tile_max = (fmask.u.legacy.level[0].nblk_x * fmask.u.legacy.level[0].nblk_y) / 64;
	if (out->slice_tile_max)
		out->slice_tile_max -= 1;

	out->tile_mode_index = fmask.u.legacy.tiling_index[0];
	out->pitch_in_pixels = fmask.u.legacy.level[0].nblk_x;
	out->bank_height = fmask.u.legacy.bankh;
	out->tile_swizzle = fmask.tile_swizzle;
	out->alignment = MAX2(256, fmask.surf_alignment);
	out->size = fmask.surf_size;
}

static void si_texture_allocate_fmask(struct si_screen *sscreen,
				      struct r600_texture *rtex)
{
	si_texture_get_fmask_info(sscreen, rtex,
				    rtex->resource.b.b.nr_samples, &rtex->fmask);

	rtex->fmask.offset = align64(rtex->size, rtex->fmask.alignment);
	rtex->size = rtex->fmask.offset + rtex->fmask.size;
}

void si_texture_get_cmask_info(struct si_screen *sscreen,
			       struct r600_texture *rtex,
			       struct r600_cmask_info *out)
{
	unsigned pipe_interleave_bytes = sscreen->info.pipe_interleave_bytes;
	unsigned num_pipes = sscreen->info.num_tile_pipes;
	unsigned cl_width, cl_height;

	if (sscreen->info.chip_class >= GFX9) {
		out->alignment = rtex->surface.u.gfx9.cmask_alignment;
		out->size = rtex->surface.u.gfx9.cmask_size;
		return;
	}

	switch (num_pipes) {
	case 2:
		cl_width = 32;
		cl_height = 16;
		break;
	case 4:
		cl_width = 32;
		cl_height = 32;
		break;
	case 8:
		cl_width = 64;
		cl_height = 32;
		break;
	case 16: /* Hawaii */
		cl_width = 64;
		cl_height = 64;
		break;
	default:
		assert(0);
		return;
	}

	unsigned base_align = num_pipes * pipe_interleave_bytes;

	unsigned width = align(rtex->resource.b.b.width0, cl_width*8);
	unsigned height = align(rtex->resource.b.b.height0, cl_height*8);
	unsigned slice_elements = (width * height) / (8*8);

	/* Each element of CMASK is a nibble. */
	unsigned slice_bytes = slice_elements / 2;

	out->slice_tile_max = (width * height) / (128*128);
	if (out->slice_tile_max)
		out->slice_tile_max -= 1;

	out->alignment = MAX2(256, base_align);
	out->size = util_num_layers(&rtex->resource.b.b, 0) *
		    align(slice_bytes, base_align);
}

static void si_texture_allocate_cmask(struct si_screen *sscreen,
				      struct r600_texture *rtex)
{
	si_texture_get_cmask_info(sscreen, rtex, &rtex->cmask);

	rtex->cmask.offset = align64(rtex->size, rtex->cmask.alignment);
	rtex->size = rtex->cmask.offset + rtex->cmask.size;

	rtex->cb_color_info |= S_028C70_FAST_CLEAR(1);
}

static void si_texture_get_htile_size(struct si_screen *sscreen,
				      struct r600_texture *rtex)
{
	unsigned cl_width, cl_height, width, height;
	unsigned slice_elements, slice_bytes, pipe_interleave_bytes, base_align;
	unsigned num_pipes = sscreen->info.num_tile_pipes;

	assert(sscreen->info.chip_class <= VI);

	rtex->surface.htile_size = 0;

	/* HTILE is broken with 1D tiling on old kernels and CIK. */
	if (sscreen->info.chip_class >= CIK &&
	    rtex->surface.u.legacy.level[0].mode == RADEON_SURF_MODE_1D &&
	    sscreen->info.drm_major == 2 && sscreen->info.drm_minor < 38)
		return;

	/* Overalign HTILE on P2 configs to work around GPU hangs in
	 * piglit/depthstencil-render-miplevels 585.
	 *
	 * This has been confirmed to help Kabini & Stoney, where the hangs
	 * are always reproducible. I think I have seen the test hang
	 * on Carrizo too, though it was very rare there.
	 */
	if (sscreen->info.chip_class >= CIK && num_pipes < 4)
		num_pipes = 4;

	switch (num_pipes) {
	case 1:
		cl_width = 32;
		cl_height = 16;
		break;
	case 2:
		cl_width = 32;
		cl_height = 32;
		break;
	case 4:
		cl_width = 64;
		cl_height = 32;
		break;
	case 8:
		cl_width = 64;
		cl_height = 64;
		break;
	case 16:
		cl_width = 128;
		cl_height = 64;
		break;
	default:
		assert(0);
		return;
	}

	width = align(rtex->resource.b.b.width0, cl_width * 8);
	height = align(rtex->resource.b.b.height0, cl_height * 8);

	slice_elements = (width * height) / (8 * 8);
	slice_bytes = slice_elements * 4;

	pipe_interleave_bytes = sscreen->info.pipe_interleave_bytes;
	base_align = num_pipes * pipe_interleave_bytes;

	rtex->surface.htile_alignment = base_align;
	rtex->surface.htile_size =
		util_num_layers(&rtex->resource.b.b, 0) *
		align(slice_bytes, base_align);
}

static void si_texture_allocate_htile(struct si_screen *sscreen,
				      struct r600_texture *rtex)
{
	if (sscreen->info.chip_class <= VI && !rtex->tc_compatible_htile)
		si_texture_get_htile_size(sscreen, rtex);

	if (!rtex->surface.htile_size)
		return;

	rtex->htile_offset = align(rtex->size, rtex->surface.htile_alignment);
	rtex->size = rtex->htile_offset + rtex->surface.htile_size;
}

void si_print_texture_info(struct si_screen *sscreen,
			   struct r600_texture *rtex, struct u_log_context *log)
{
	int i;

	/* Common parameters. */
	u_log_printf(log, "  Info: npix_x=%u, npix_y=%u, npix_z=%u, blk_w=%u, "
		"blk_h=%u, array_size=%u, last_level=%u, "
		"bpe=%u, nsamples=%u, flags=0x%x, %s\n",
		rtex->resource.b.b.width0, rtex->resource.b.b.height0,
		rtex->resource.b.b.depth0, rtex->surface.blk_w,
		rtex->surface.blk_h,
		rtex->resource.b.b.array_size, rtex->resource.b.b.last_level,
		rtex->surface.bpe, rtex->resource.b.b.nr_samples,
		rtex->surface.flags, util_format_short_name(rtex->resource.b.b.format));

	if (sscreen->info.chip_class >= GFX9) {
		u_log_printf(log, "  Surf: size=%"PRIu64", slice_size=%"PRIu64", "
			"alignment=%u, swmode=%u, epitch=%u, pitch=%u\n",
			rtex->surface.surf_size,
			rtex->surface.u.gfx9.surf_slice_size,
			rtex->surface.surf_alignment,
			rtex->surface.u.gfx9.surf.swizzle_mode,
			rtex->surface.u.gfx9.surf.epitch,
			rtex->surface.u.gfx9.surf_pitch);

		if (rtex->fmask.size) {
			u_log_printf(log, "  FMASK: offset=%"PRIu64", size=%"PRIu64", "
				"alignment=%u, swmode=%u, epitch=%u\n",
				rtex->fmask.offset,
				rtex->surface.u.gfx9.fmask_size,
				rtex->surface.u.gfx9.fmask_alignment,
				rtex->surface.u.gfx9.fmask.swizzle_mode,
				rtex->surface.u.gfx9.fmask.epitch);
		}

		if (rtex->cmask.size) {
			u_log_printf(log, "  CMask: offset=%"PRIu64", size=%"PRIu64", "
				"alignment=%u, rb_aligned=%u, pipe_aligned=%u\n",
				rtex->cmask.offset,
				rtex->surface.u.gfx9.cmask_size,
				rtex->surface.u.gfx9.cmask_alignment,
				rtex->surface.u.gfx9.cmask.rb_aligned,
				rtex->surface.u.gfx9.cmask.pipe_aligned);
		}

		if (rtex->htile_offset) {
			u_log_printf(log, "  HTile: offset=%"PRIu64", size=%u, alignment=%u, "
				"rb_aligned=%u, pipe_aligned=%u\n",
				rtex->htile_offset,
				rtex->surface.htile_size,
				rtex->surface.htile_alignment,
				rtex->surface.u.gfx9.htile.rb_aligned,
				rtex->surface.u.gfx9.htile.pipe_aligned);
		}

		if (rtex->dcc_offset) {
			u_log_printf(log, "  DCC: offset=%"PRIu64", size=%u, "
				"alignment=%u, pitch_max=%u, num_dcc_levels=%u\n",
				rtex->dcc_offset, rtex->surface.dcc_size,
				rtex->surface.dcc_alignment,
				rtex->surface.u.gfx9.dcc_pitch_max,
				rtex->surface.num_dcc_levels);
		}

		if (rtex->surface.u.gfx9.stencil_offset) {
			u_log_printf(log, "  Stencil: offset=%"PRIu64", swmode=%u, epitch=%u\n",
				rtex->surface.u.gfx9.stencil_offset,
				rtex->surface.u.gfx9.stencil.swizzle_mode,
				rtex->surface.u.gfx9.stencil.epitch);
		}
		return;
	}

	u_log_printf(log, "  Layout: size=%"PRIu64", alignment=%u, bankw=%u, "
		"bankh=%u, nbanks=%u, mtilea=%u, tilesplit=%u, pipeconfig=%u, scanout=%u\n",
		rtex->surface.surf_size, rtex->surface.surf_alignment, rtex->surface.u.legacy.bankw,
		rtex->surface.u.legacy.bankh, rtex->surface.u.legacy.num_banks, rtex->surface.u.legacy.mtilea,
		rtex->surface.u.legacy.tile_split, rtex->surface.u.legacy.pipe_config,
		(rtex->surface.flags & RADEON_SURF_SCANOUT) != 0);

	if (rtex->fmask.size)
		u_log_printf(log, "  FMask: offset=%"PRIu64", size=%"PRIu64", alignment=%u, pitch_in_pixels=%u, "
			"bankh=%u, slice_tile_max=%u, tile_mode_index=%u\n",
			rtex->fmask.offset, rtex->fmask.size, rtex->fmask.alignment,
			rtex->fmask.pitch_in_pixels, rtex->fmask.bank_height,
			rtex->fmask.slice_tile_max, rtex->fmask.tile_mode_index);

	if (rtex->cmask.size)
		u_log_printf(log, "  CMask: offset=%"PRIu64", size=%"PRIu64", alignment=%u, "
			"slice_tile_max=%u\n",
			rtex->cmask.offset, rtex->cmask.size, rtex->cmask.alignment,
			rtex->cmask.slice_tile_max);

	if (rtex->htile_offset)
		u_log_printf(log, "  HTile: offset=%"PRIu64", size=%u, "
			"alignment=%u, TC_compatible = %u\n",
			rtex->htile_offset, rtex->surface.htile_size,
			rtex->surface.htile_alignment,
			rtex->tc_compatible_htile);

	if (rtex->dcc_offset) {
		u_log_printf(log, "  DCC: offset=%"PRIu64", size=%u, alignment=%u\n",
			rtex->dcc_offset, rtex->surface.dcc_size,
			rtex->surface.dcc_alignment);
		for (i = 0; i <= rtex->resource.b.b.last_level; i++)
			u_log_printf(log, "  DCCLevel[%i]: enabled=%u, offset=%u, "
				"fast_clear_size=%u\n",
				i, i < rtex->surface.num_dcc_levels,
				rtex->surface.u.legacy.level[i].dcc_offset,
				rtex->surface.u.legacy.level[i].dcc_fast_clear_size);
	}

	for (i = 0; i <= rtex->resource.b.b.last_level; i++)
		u_log_printf(log, "  Level[%i]: offset=%"PRIu64", slice_size=%"PRIu64", "
			"npix_x=%u, npix_y=%u, npix_z=%u, nblk_x=%u, nblk_y=%u, "
			"mode=%u, tiling_index = %u\n",
			i, rtex->surface.u.legacy.level[i].offset,
			(uint64_t)rtex->surface.u.legacy.level[i].slice_size_dw * 4,
			u_minify(rtex->resource.b.b.width0, i),
			u_minify(rtex->resource.b.b.height0, i),
			u_minify(rtex->resource.b.b.depth0, i),
			rtex->surface.u.legacy.level[i].nblk_x,
			rtex->surface.u.legacy.level[i].nblk_y,
			rtex->surface.u.legacy.level[i].mode,
			rtex->surface.u.legacy.tiling_index[i]);

	if (rtex->surface.has_stencil) {
		u_log_printf(log, "  StencilLayout: tilesplit=%u\n",
			rtex->surface.u.legacy.stencil_tile_split);
		for (i = 0; i <= rtex->resource.b.b.last_level; i++) {
			u_log_printf(log, "  StencilLevel[%i]: offset=%"PRIu64", "
				"slice_size=%"PRIu64", npix_x=%u, "
				"npix_y=%u, npix_z=%u, nblk_x=%u, nblk_y=%u, "
				"mode=%u, tiling_index = %u\n",
				i, rtex->surface.u.legacy.stencil_level[i].offset,
				(uint64_t)rtex->surface.u.legacy.stencil_level[i].slice_size_dw * 4,
				u_minify(rtex->resource.b.b.width0, i),
				u_minify(rtex->resource.b.b.height0, i),
				u_minify(rtex->resource.b.b.depth0, i),
				rtex->surface.u.legacy.stencil_level[i].nblk_x,
				rtex->surface.u.legacy.stencil_level[i].nblk_y,
				rtex->surface.u.legacy.stencil_level[i].mode,
				rtex->surface.u.legacy.stencil_tiling_index[i]);
		}
	}
}

/* Common processing for r600_texture_create and r600_texture_from_handle */
static struct r600_texture *
si_texture_create_object(struct pipe_screen *screen,
			 const struct pipe_resource *base,
			 struct pb_buffer *buf,
			 struct radeon_surf *surface)
{
	struct r600_texture *rtex;
	struct r600_resource *resource;
	struct si_screen *sscreen = (struct si_screen*)screen;

	rtex = CALLOC_STRUCT(r600_texture);
	if (!rtex)
		return NULL;

	resource = &rtex->resource;
	resource->b.b = *base;
	resource->b.b.next = NULL;
	resource->b.vtbl = &si_texture_vtbl;
	pipe_reference_init(&resource->b.b.reference, 1);
	resource->b.b.screen = screen;

	/* don't include stencil-only formats which we don't support for rendering */
	rtex->is_depth = util_format_has_depth(util_format_description(rtex->resource.b.b.format));

	rtex->surface = *surface;
	rtex->size = rtex->surface.surf_size;

	rtex->tc_compatible_htile = rtex->surface.htile_size != 0 &&
				    (rtex->surface.flags &
				     RADEON_SURF_TC_COMPATIBLE_HTILE);

	/* TC-compatible HTILE:
	 * - VI only supports Z32_FLOAT.
	 * - GFX9 only supports Z32_FLOAT and Z16_UNORM. */
	if (rtex->tc_compatible_htile) {
		if (sscreen->info.chip_class >= GFX9 &&
		    base->format == PIPE_FORMAT_Z16_UNORM)
			rtex->db_render_format = base->format;
		else {
			rtex->db_render_format = PIPE_FORMAT_Z32_FLOAT;
			rtex->upgraded_depth = base->format != PIPE_FORMAT_Z32_FLOAT &&
					       base->format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
		}
	} else {
		rtex->db_render_format = base->format;
	}

	/* Applies to GCN. */
	rtex->last_msaa_resolve_target_micro_mode = rtex->surface.micro_tile_mode;

	/* Disable separate DCC at the beginning. DRI2 doesn't reuse buffers
	 * between frames, so the only thing that can enable separate DCC
	 * with DRI2 is multiple slow clears within a frame.
	 */
	rtex->ps_draw_ratio = 0;

	if (rtex->is_depth) {
		if (sscreen->info.chip_class >= GFX9) {
			rtex->can_sample_z = true;
			rtex->can_sample_s = true;
		} else {
			rtex->can_sample_z = !rtex->surface.u.legacy.depth_adjusted;
			rtex->can_sample_s = !rtex->surface.u.legacy.stencil_adjusted;
		}

		if (!(base->flags & (SI_RESOURCE_FLAG_TRANSFER |
				     SI_RESOURCE_FLAG_FLUSHED_DEPTH))) {
			rtex->db_compatible = true;

			if (!(sscreen->debug_flags & DBG(NO_HYPERZ)))
				si_texture_allocate_htile(sscreen, rtex);
		}
	} else {
		if (base->nr_samples > 1 &&
		    !buf &&
		    !(sscreen->debug_flags & DBG(NO_FMASK))) {
			si_texture_allocate_fmask(sscreen, rtex);
			si_texture_allocate_cmask(sscreen, rtex);
			rtex->cmask_buffer = &rtex->resource;

			if (!rtex->fmask.size || !rtex->cmask.size) {
				FREE(rtex);
				return NULL;
			}
		}

		/* Shared textures must always set up DCC here.
		 * If it's not present, it will be disabled by
		 * apply_opaque_metadata later.
		 */
		if (rtex->surface.dcc_size &&
		    (buf || !(sscreen->debug_flags & DBG(NO_DCC))) &&
		    !(rtex->surface.flags & RADEON_SURF_SCANOUT)) {
			/* Reserve space for the DCC buffer. */
			rtex->dcc_offset = align64(rtex->size, rtex->surface.dcc_alignment);
			rtex->size = rtex->dcc_offset + rtex->surface.dcc_size;
		}
	}

	/* Now create the backing buffer. */
	if (!buf) {
		si_init_resource_fields(sscreen, resource, rtex->size,
					  rtex->surface.surf_alignment);

		if (!si_alloc_resource(sscreen, resource)) {
			FREE(rtex);
			return NULL;
		}
	} else {
		resource->buf = buf;
		resource->gpu_address = sscreen->ws->buffer_get_virtual_address(resource->buf);
		resource->bo_size = buf->size;
		resource->bo_alignment = buf->alignment;
		resource->domains = sscreen->ws->buffer_get_initial_domain(resource->buf);
		if (resource->domains & RADEON_DOMAIN_VRAM)
			resource->vram_usage = buf->size;
		else if (resource->domains & RADEON_DOMAIN_GTT)
			resource->gart_usage = buf->size;
	}

	if (rtex->cmask.size) {
		/* Initialize the cmask to 0xCC (= compressed state). */
		si_screen_clear_buffer(sscreen, &rtex->cmask_buffer->b.b,
					 rtex->cmask.offset, rtex->cmask.size,
					 0xCCCCCCCC);
	}
	if (rtex->htile_offset) {
		uint32_t clear_value = 0;

		if (sscreen->info.chip_class >= GFX9 || rtex->tc_compatible_htile)
			clear_value = 0x0000030F;

		si_screen_clear_buffer(sscreen, &rtex->resource.b.b,
					 rtex->htile_offset,
					 rtex->surface.htile_size,
					 clear_value);
	}

	/* Initialize DCC only if the texture is not being imported. */
	if (!buf && rtex->dcc_offset) {
		si_screen_clear_buffer(sscreen, &rtex->resource.b.b,
					 rtex->dcc_offset,
					 rtex->surface.dcc_size,
					 0xFFFFFFFF);
	}

	/* Initialize the CMASK base register value. */
	rtex->cmask.base_address_reg =
		(rtex->resource.gpu_address + rtex->cmask.offset) >> 8;

	if (sscreen->debug_flags & DBG(VM)) {
		fprintf(stderr, "VM start=0x%"PRIX64"  end=0x%"PRIX64" | Texture %ix%ix%i, %i levels, %i samples, %s\n",
			rtex->resource.gpu_address,
			rtex->resource.gpu_address + rtex->resource.buf->size,
			base->width0, base->height0, util_num_layers(base, 0), base->last_level+1,
			base->nr_samples ? base->nr_samples : 1, util_format_short_name(base->format));
	}

	if (sscreen->debug_flags & DBG(TEX)) {
		puts("Texture:");
		struct u_log_context log;
		u_log_context_init(&log);
		si_print_texture_info(sscreen, rtex, &log);
		u_log_new_page_print(&log, stdout);
		fflush(stdout);
		u_log_context_destroy(&log);
	}

	return rtex;
}

static enum radeon_surf_mode
si_choose_tiling(struct si_screen *sscreen,
		 const struct pipe_resource *templ, bool tc_compatible_htile)
{
	const struct util_format_description *desc = util_format_description(templ->format);
	bool force_tiling = templ->flags & SI_RESOURCE_FLAG_FORCE_TILING;
	bool is_depth_stencil = util_format_is_depth_or_stencil(templ->format) &&
				!(templ->flags & SI_RESOURCE_FLAG_FLUSHED_DEPTH);

	/* MSAA resources must be 2D tiled. */
	if (templ->nr_samples > 1)
		return RADEON_SURF_MODE_2D;

	/* Transfer resources should be linear. */
	if (templ->flags & SI_RESOURCE_FLAG_TRANSFER)
		return RADEON_SURF_MODE_LINEAR_ALIGNED;

	/* Avoid Z/S decompress blits by forcing TC-compatible HTILE on VI,
	 * which requires 2D tiling.
	 */
	if (sscreen->info.chip_class == VI && tc_compatible_htile)
		return RADEON_SURF_MODE_2D;

	/* Handle common candidates for the linear mode.
	 * Compressed textures and DB surfaces must always be tiled.
	 */
	if (!force_tiling &&
	    !is_depth_stencil &&
	    !util_format_is_compressed(templ->format)) {
		if (sscreen->debug_flags & DBG(NO_TILING))
			return RADEON_SURF_MODE_LINEAR_ALIGNED;

		/* Tiling doesn't work with the 422 (SUBSAMPLED) formats. */
		if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED)
			return RADEON_SURF_MODE_LINEAR_ALIGNED;

		/* Cursors are linear on SI.
		 * (XXX double-check, maybe also use RADEON_SURF_SCANOUT) */
		if (templ->bind & PIPE_BIND_CURSOR)
			return RADEON_SURF_MODE_LINEAR_ALIGNED;

		if (templ->bind & PIPE_BIND_LINEAR)
			return RADEON_SURF_MODE_LINEAR_ALIGNED;

		/* Textures with a very small height are recommended to be linear. */
		if (templ->target == PIPE_TEXTURE_1D ||
		    templ->target == PIPE_TEXTURE_1D_ARRAY ||
		    /* Only very thin and long 2D textures should benefit from
		     * linear_aligned. */
		    (templ->width0 > 8 && templ->height0 <= 2))
			return RADEON_SURF_MODE_LINEAR_ALIGNED;

		/* Textures likely to be mapped often. */
		if (templ->usage == PIPE_USAGE_STAGING ||
		    templ->usage == PIPE_USAGE_STREAM)
			return RADEON_SURF_MODE_LINEAR_ALIGNED;
	}

	/* Make small textures 1D tiled. */
	if (templ->width0 <= 16 || templ->height0 <= 16 ||
	    (sscreen->debug_flags & DBG(NO_2D_TILING)))
		return RADEON_SURF_MODE_1D;

	/* The allocator will switch to 1D if needed. */
	return RADEON_SURF_MODE_2D;
}

struct pipe_resource *si_texture_create(struct pipe_screen *screen,
					const struct pipe_resource *templ)
{
	struct si_screen *sscreen = (struct si_screen*)screen;
	struct radeon_surf surface = {0};
	bool is_flushed_depth = templ->flags & SI_RESOURCE_FLAG_FLUSHED_DEPTH;
	bool tc_compatible_htile =
		sscreen->info.chip_class >= VI &&
		/* There are issues with TC-compatible HTILE on Tonga (and
		 * Iceland is the same design), and documented bug workarounds
		 * don't help. For example, this fails:
		 *   piglit/bin/tex-miplevel-selection 'texture()' 2DShadow -auto
		 */
		sscreen->info.family != CHIP_TONGA &&
		sscreen->info.family != CHIP_ICELAND &&
		(templ->flags & PIPE_RESOURCE_FLAG_TEXTURING_MORE_LIKELY) &&
		!(sscreen->debug_flags & DBG(NO_HYPERZ)) &&
		!is_flushed_depth &&
		templ->nr_samples <= 1 && /* TC-compat HTILE is less efficient with MSAA */
		util_format_is_depth_or_stencil(templ->format);

	int r;

	r = si_init_surface(sscreen, &surface, templ,
			    si_choose_tiling(sscreen, templ, tc_compatible_htile),
			    0, 0, false, false, is_flushed_depth,
			    tc_compatible_htile);
	if (r) {
		return NULL;
	}

	return (struct pipe_resource *)
	       si_texture_create_object(screen, templ, NULL, &surface);
}

static struct pipe_resource *si_texture_from_handle(struct pipe_screen *screen,
						    const struct pipe_resource *templ,
						    struct winsys_handle *whandle,
						    unsigned usage)
{
	struct si_screen *sscreen = (struct si_screen*)screen;
	struct pb_buffer *buf = NULL;
	unsigned stride = 0, offset = 0;
	enum radeon_surf_mode array_mode;
	struct radeon_surf surface = {};
	int r;
	struct radeon_bo_metadata metadata = {};
	struct r600_texture *rtex;
	bool is_scanout;

	/* Support only 2D textures without mipmaps */
	if ((templ->target != PIPE_TEXTURE_2D && templ->target != PIPE_TEXTURE_RECT) ||
	      templ->depth0 != 1 || templ->last_level != 0)
		return NULL;

	buf = sscreen->ws->buffer_from_handle(sscreen->ws, whandle, &stride, &offset);
	if (!buf)
		return NULL;

	sscreen->ws->buffer_get_metadata(buf, &metadata);
	si_surface_import_metadata(sscreen, &surface, &metadata,
				     &array_mode, &is_scanout);

	r = si_init_surface(sscreen, &surface, templ, array_mode, stride,
			      offset, true, is_scanout, false, false);
	if (r) {
		return NULL;
	}

	rtex = si_texture_create_object(screen, templ, buf, &surface);
	if (!rtex)
		return NULL;

	rtex->resource.b.is_shared = true;
	rtex->resource.external_usage = usage;

	si_apply_opaque_metadata(sscreen, rtex, &metadata);

	assert(rtex->surface.tile_swizzle == 0);
	return &rtex->resource.b.b;
}

bool si_init_flushed_depth_texture(struct pipe_context *ctx,
				   struct pipe_resource *texture,
				   struct r600_texture **staging)
{
	struct r600_texture *rtex = (struct r600_texture*)texture;
	struct pipe_resource resource;
	struct r600_texture **flushed_depth_texture = staging ?
			staging : &rtex->flushed_depth_texture;
	enum pipe_format pipe_format = texture->format;

	if (!staging) {
		if (rtex->flushed_depth_texture)
			return true; /* it's ready */

		if (!rtex->can_sample_z && rtex->can_sample_s) {
			switch (pipe_format) {
			case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
				/* Save memory by not allocating the S plane. */
				pipe_format = PIPE_FORMAT_Z32_FLOAT;
				break;
			case PIPE_FORMAT_Z24_UNORM_S8_UINT:
			case PIPE_FORMAT_S8_UINT_Z24_UNORM:
				/* Save memory bandwidth by not copying the
				 * stencil part during flush.
				 *
				 * This potentially increases memory bandwidth
				 * if an application uses both Z and S texturing
				 * simultaneously (a flushed Z24S8 texture
				 * would be stored compactly), but how often
				 * does that really happen?
				 */
				pipe_format = PIPE_FORMAT_Z24X8_UNORM;
				break;
			default:;
			}
		} else if (!rtex->can_sample_s && rtex->can_sample_z) {
			assert(util_format_has_stencil(util_format_description(pipe_format)));

			/* DB->CB copies to an 8bpp surface don't work. */
			pipe_format = PIPE_FORMAT_X24S8_UINT;
		}
	}

	memset(&resource, 0, sizeof(resource));
	resource.target = texture->target;
	resource.format = pipe_format;
	resource.width0 = texture->width0;
	resource.height0 = texture->height0;
	resource.depth0 = texture->depth0;
	resource.array_size = texture->array_size;
	resource.last_level = texture->last_level;
	resource.nr_samples = texture->nr_samples;
	resource.usage = staging ? PIPE_USAGE_STAGING : PIPE_USAGE_DEFAULT;
	resource.bind = texture->bind & ~PIPE_BIND_DEPTH_STENCIL;
	resource.flags = texture->flags | SI_RESOURCE_FLAG_FLUSHED_DEPTH;

	if (staging)
		resource.flags |= SI_RESOURCE_FLAG_TRANSFER;

	*flushed_depth_texture = (struct r600_texture *)ctx->screen->resource_create(ctx->screen, &resource);
	if (*flushed_depth_texture == NULL) {
		PRINT_ERR("failed to create temporary texture to hold flushed depth\n");
		return false;
	}
	return true;
}

/**
 * Initialize the pipe_resource descriptor to be of the same size as the box,
 * which is supposed to hold a subregion of the texture "orig" at the given
 * mipmap level.
 */
static void si_init_temp_resource_from_box(struct pipe_resource *res,
					   struct pipe_resource *orig,
					   const struct pipe_box *box,
					   unsigned level, unsigned flags)
{
	memset(res, 0, sizeof(*res));
	res->format = orig->format;
	res->width0 = box->width;
	res->height0 = box->height;
	res->depth0 = 1;
	res->array_size = 1;
	res->usage = flags & SI_RESOURCE_FLAG_TRANSFER ? PIPE_USAGE_STAGING : PIPE_USAGE_DEFAULT;
	res->flags = flags;

	/* We must set the correct texture target and dimensions for a 3D box. */
	if (box->depth > 1 && util_max_layer(orig, level) > 0) {
		res->target = PIPE_TEXTURE_2D_ARRAY;
		res->array_size = box->depth;
	} else {
		res->target = PIPE_TEXTURE_2D;
	}
}

static bool si_can_invalidate_texture(struct si_screen *sscreen,
				      struct r600_texture *rtex,
				      unsigned transfer_usage,
				      const struct pipe_box *box)
{
	return !rtex->resource.b.is_shared &&
		!(transfer_usage & PIPE_TRANSFER_READ) &&
		rtex->resource.b.b.last_level == 0 &&
		util_texrange_covers_whole_level(&rtex->resource.b.b, 0,
						 box->x, box->y, box->z,
						 box->width, box->height,
						 box->depth);
}

static void si_texture_invalidate_storage(struct si_context *sctx,
					  struct r600_texture *rtex)
{
	struct si_screen *sscreen = sctx->screen;

	/* There is no point in discarding depth and tiled buffers. */
	assert(!rtex->is_depth);
	assert(rtex->surface.is_linear);

	/* Reallocate the buffer in the same pipe_resource. */
	si_alloc_resource(sscreen, &rtex->resource);

	/* Initialize the CMASK base address (needed even without CMASK). */
	rtex->cmask.base_address_reg =
		(rtex->resource.gpu_address + rtex->cmask.offset) >> 8;

	p_atomic_inc(&sscreen->dirty_tex_counter);

	sctx->num_alloc_tex_transfer_bytes += rtex->size;
}

static void *si_texture_transfer_map(struct pipe_context *ctx,
				     struct pipe_resource *texture,
				     unsigned level,
				     unsigned usage,
				     const struct pipe_box *box,
				     struct pipe_transfer **ptransfer)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct r600_texture *rtex = (struct r600_texture*)texture;
	struct r600_transfer *trans;
	struct r600_resource *buf;
	unsigned offset = 0;
	char *map;
	bool use_staging_texture = false;

	assert(!(texture->flags & SI_RESOURCE_FLAG_TRANSFER));
	assert(box->width && box->height && box->depth);

	/* Depth textures use staging unconditionally. */
	if (!rtex->is_depth) {
		/* Degrade the tile mode if we get too many transfers on APUs.
		 * On dGPUs, the staging texture is always faster.
		 * Only count uploads that are at least 4x4 pixels large.
		 */
		if (!sctx->screen->info.has_dedicated_vram &&
		    level == 0 &&
		    box->width >= 4 && box->height >= 4 &&
		    p_atomic_inc_return(&rtex->num_level0_transfers) == 10) {
			bool can_invalidate =
				si_can_invalidate_texture(sctx->screen, rtex,
							    usage, box);

			si_reallocate_texture_inplace(sctx, rtex,
							PIPE_BIND_LINEAR,
							can_invalidate);
		}

		/* Tiled textures need to be converted into a linear texture for CPU
		 * access. The staging texture is always linear and is placed in GART.
		 *
		 * Reading from VRAM or GTT WC is slow, always use the staging
		 * texture in this case.
		 *
		 * Use the staging texture for uploads if the underlying BO
		 * is busy.
		 */
		if (!rtex->surface.is_linear)
			use_staging_texture = true;
		else if (usage & PIPE_TRANSFER_READ)
			use_staging_texture =
				rtex->resource.domains & RADEON_DOMAIN_VRAM ||
				rtex->resource.flags & RADEON_FLAG_GTT_WC;
		/* Write & linear only: */
		else if (si_rings_is_buffer_referenced(sctx, rtex->resource.buf,
						       RADEON_USAGE_READWRITE) ||
			 !sctx->ws->buffer_wait(rtex->resource.buf, 0,
						RADEON_USAGE_READWRITE)) {
			/* It's busy. */
			if (si_can_invalidate_texture(sctx->screen, rtex,
							usage, box))
				si_texture_invalidate_storage(sctx, rtex);
			else
				use_staging_texture = true;
		}
	}

	trans = CALLOC_STRUCT(r600_transfer);
	if (!trans)
		return NULL;
	pipe_resource_reference(&trans->b.b.resource, texture);
	trans->b.b.level = level;
	trans->b.b.usage = usage;
	trans->b.b.box = *box;

	if (rtex->is_depth) {
		struct r600_texture *staging_depth;

		if (rtex->resource.b.b.nr_samples > 1) {
			/* MSAA depth buffers need to be converted to single sample buffers.
			 *
			 * Mapping MSAA depth buffers can occur if ReadPixels is called
			 * with a multisample GLX visual.
			 *
			 * First downsample the depth buffer to a temporary texture,
			 * then decompress the temporary one to staging.
			 *
			 * Only the region being mapped is transfered.
			 */
			struct pipe_resource resource;

			si_init_temp_resource_from_box(&resource, texture, box, level, 0);

			if (!si_init_flushed_depth_texture(ctx, &resource, &staging_depth)) {
				PRINT_ERR("failed to create temporary texture to hold untiled copy\n");
				goto fail_trans;
			}

			if (usage & PIPE_TRANSFER_READ) {
				struct pipe_resource *temp = ctx->screen->resource_create(ctx->screen, &resource);
				if (!temp) {
					PRINT_ERR("failed to create a temporary depth texture\n");
					goto fail_trans;
				}

				si_copy_region_with_blit(ctx, temp, 0, 0, 0, 0, texture, level, box);
				si_blit_decompress_depth(ctx, (struct r600_texture*)temp, staging_depth,
							 0, 0, 0, box->depth, 0, 0);
				pipe_resource_reference(&temp, NULL);
			}

			/* Just get the strides. */
			si_texture_get_offset(sctx->screen, staging_depth, level, NULL,
						&trans->b.b.stride,
						&trans->b.b.layer_stride);
		} else {
			/* XXX: only readback the rectangle which is being mapped? */
			/* XXX: when discard is true, no need to read back from depth texture */
			if (!si_init_flushed_depth_texture(ctx, texture, &staging_depth)) {
				PRINT_ERR("failed to create temporary texture to hold untiled copy\n");
				goto fail_trans;
			}

			si_blit_decompress_depth(ctx, rtex, staging_depth,
						 level, level,
						 box->z, box->z + box->depth - 1,
						 0, 0);

			offset = si_texture_get_offset(sctx->screen, staging_depth,
							 level, box,
							 &trans->b.b.stride,
							 &trans->b.b.layer_stride);
		}

		trans->staging = (struct r600_resource*)staging_depth;
		buf = trans->staging;
	} else if (use_staging_texture) {
		struct pipe_resource resource;
		struct r600_texture *staging;

		si_init_temp_resource_from_box(&resource, texture, box, level,
						 SI_RESOURCE_FLAG_TRANSFER);
		resource.usage = (usage & PIPE_TRANSFER_READ) ?
			PIPE_USAGE_STAGING : PIPE_USAGE_STREAM;

		/* Create the temporary texture. */
		staging = (struct r600_texture*)ctx->screen->resource_create(ctx->screen, &resource);
		if (!staging) {
			PRINT_ERR("failed to create temporary texture to hold untiled copy\n");
			goto fail_trans;
		}
		trans->staging = &staging->resource;

		/* Just get the strides. */
		si_texture_get_offset(sctx->screen, staging, 0, NULL,
					&trans->b.b.stride,
					&trans->b.b.layer_stride);

		if (usage & PIPE_TRANSFER_READ)
			si_copy_to_staging_texture(ctx, trans);
		else
			usage |= PIPE_TRANSFER_UNSYNCHRONIZED;

		buf = trans->staging;
	} else {
		/* the resource is mapped directly */
		offset = si_texture_get_offset(sctx->screen, rtex, level, box,
						 &trans->b.b.stride,
						 &trans->b.b.layer_stride);
		buf = &rtex->resource;
	}

	if (!(map = si_buffer_map_sync_with_rings(sctx, buf, usage)))
		goto fail_trans;

	*ptransfer = &trans->b.b;
	return map + offset;

fail_trans:
	r600_resource_reference(&trans->staging, NULL);
	pipe_resource_reference(&trans->b.b.resource, NULL);
	FREE(trans);
	return NULL;
}

static void si_texture_transfer_unmap(struct pipe_context *ctx,
				      struct pipe_transfer* transfer)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct r600_transfer *rtransfer = (struct r600_transfer*)transfer;
	struct pipe_resource *texture = transfer->resource;
	struct r600_texture *rtex = (struct r600_texture*)texture;

	if ((transfer->usage & PIPE_TRANSFER_WRITE) && rtransfer->staging) {
		if (rtex->is_depth && rtex->resource.b.b.nr_samples <= 1) {
			ctx->resource_copy_region(ctx, texture, transfer->level,
						  transfer->box.x, transfer->box.y, transfer->box.z,
						  &rtransfer->staging->b.b, transfer->level,
						  &transfer->box);
		} else {
			si_copy_from_staging_texture(ctx, rtransfer);
		}
	}

	if (rtransfer->staging) {
		sctx->num_alloc_tex_transfer_bytes += rtransfer->staging->buf->size;
		r600_resource_reference(&rtransfer->staging, NULL);
	}

	/* Heuristic for {upload, draw, upload, draw, ..}:
	 *
	 * Flush the gfx IB if we've allocated too much texture storage.
	 *
	 * The idea is that we don't want to build IBs that use too much
	 * memory and put pressure on the kernel memory manager and we also
	 * want to make temporary and invalidated buffers go idle ASAP to
	 * decrease the total memory usage or make them reusable. The memory
	 * usage will be slightly higher than given here because of the buffer
	 * cache in the winsys.
	 *
	 * The result is that the kernel memory manager is never a bottleneck.
	 */
	if (sctx->num_alloc_tex_transfer_bytes > sctx->screen->info.gart_size / 4) {
		si_flush_gfx_cs(sctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);
		sctx->num_alloc_tex_transfer_bytes = 0;
	}

	pipe_resource_reference(&transfer->resource, NULL);
	FREE(transfer);
}

static const struct u_resource_vtbl si_texture_vtbl =
{
	NULL,				/* get_handle */
	si_texture_destroy,		/* resource_destroy */
	si_texture_transfer_map,	/* transfer_map */
	u_default_transfer_flush_region, /* transfer_flush_region */
	si_texture_transfer_unmap,	/* transfer_unmap */
};

/* DCC channel type categories within which formats can be reinterpreted
 * while keeping the same DCC encoding. The swizzle must also match. */
enum dcc_channel_type {
	dcc_channel_float,
	/* uint and sint can be merged if we never use TC-compatible DCC clear
	 * encoding with the clear value of 1. */
	dcc_channel_uint,
	dcc_channel_sint,
	dcc_channel_uint_10_10_10_2,
	dcc_channel_incompatible,
};

/* Return the type of DCC encoding. */
static enum dcc_channel_type
vi_get_dcc_channel_type(const struct util_format_description *desc)
{
	int i;

	/* Find the first non-void channel. */
	for (i = 0; i < desc->nr_channels; i++)
		if (desc->channel[i].type != UTIL_FORMAT_TYPE_VOID)
			break;
	if (i == desc->nr_channels)
		return dcc_channel_incompatible;

	switch (desc->channel[i].size) {
	case 32:
	case 16:
	case 8:
		if (desc->channel[i].type == UTIL_FORMAT_TYPE_FLOAT)
			return dcc_channel_float;
		if (desc->channel[i].type == UTIL_FORMAT_TYPE_UNSIGNED)
			return dcc_channel_uint;
		return dcc_channel_sint;
	case 10:
		return dcc_channel_uint_10_10_10_2;
	default:
		return dcc_channel_incompatible;
	}
}

/* Return if it's allowed to reinterpret one format as another with DCC enabled. */
bool vi_dcc_formats_compatible(enum pipe_format format1,
			       enum pipe_format format2)
{
	const struct util_format_description *desc1, *desc2;
	enum dcc_channel_type type1, type2;
	int i;

	if (format1 == format2)
		return true;

	desc1 = util_format_description(format1);
	desc2 = util_format_description(format2);

	if (desc1->nr_channels != desc2->nr_channels)
		return false;

	/* Swizzles must be the same. */
	for (i = 0; i < desc1->nr_channels; i++)
		if (desc1->swizzle[i] <= PIPE_SWIZZLE_W &&
		    desc2->swizzle[i] <= PIPE_SWIZZLE_W &&
		    desc1->swizzle[i] != desc2->swizzle[i])
			return false;

	type1 = vi_get_dcc_channel_type(desc1);
	type2 = vi_get_dcc_channel_type(desc2);

	return type1 != dcc_channel_incompatible &&
	       type2 != dcc_channel_incompatible &&
	       type1 == type2;
}

bool vi_dcc_formats_are_incompatible(struct pipe_resource *tex,
				     unsigned level,
				     enum pipe_format view_format)
{
	struct r600_texture *rtex = (struct r600_texture *)tex;

	return vi_dcc_enabled(rtex, level) &&
	       !vi_dcc_formats_compatible(tex->format, view_format);
}

/* This can't be merged with the above function, because
 * vi_dcc_formats_compatible should be called only when DCC is enabled. */
void vi_disable_dcc_if_incompatible_format(struct si_context *sctx,
					   struct pipe_resource *tex,
					   unsigned level,
					   enum pipe_format view_format)
{
	struct r600_texture *rtex = (struct r600_texture *)tex;

	if (vi_dcc_formats_are_incompatible(tex, level, view_format))
		if (!si_texture_disable_dcc(sctx, (struct r600_texture*)tex))
			si_decompress_dcc(sctx, rtex);
}

struct pipe_surface *si_create_surface_custom(struct pipe_context *pipe,
					      struct pipe_resource *texture,
					      const struct pipe_surface *templ,
					      unsigned width0, unsigned height0,
					      unsigned width, unsigned height)
{
	struct r600_surface *surface = CALLOC_STRUCT(r600_surface);

	if (!surface)
		return NULL;

	assert(templ->u.tex.first_layer <= util_max_layer(texture, templ->u.tex.level));
	assert(templ->u.tex.last_layer <= util_max_layer(texture, templ->u.tex.level));

	pipe_reference_init(&surface->base.reference, 1);
	pipe_resource_reference(&surface->base.texture, texture);
	surface->base.context = pipe;
	surface->base.format = templ->format;
	surface->base.width = width;
	surface->base.height = height;
	surface->base.u = templ->u;

	surface->width0 = width0;
	surface->height0 = height0;

	surface->dcc_incompatible =
		texture->target != PIPE_BUFFER &&
		vi_dcc_formats_are_incompatible(texture, templ->u.tex.level,
						templ->format);
	return &surface->base;
}

static struct pipe_surface *si_create_surface(struct pipe_context *pipe,
					      struct pipe_resource *tex,
					      const struct pipe_surface *templ)
{
	unsigned level = templ->u.tex.level;
	unsigned width = u_minify(tex->width0, level);
	unsigned height = u_minify(tex->height0, level);
	unsigned width0 = tex->width0;
	unsigned height0 = tex->height0;

	if (tex->target != PIPE_BUFFER && templ->format != tex->format) {
		const struct util_format_description *tex_desc
			= util_format_description(tex->format);
		const struct util_format_description *templ_desc
			= util_format_description(templ->format);

		assert(tex_desc->block.bits == templ_desc->block.bits);

		/* Adjust size of surface if and only if the block width or
		 * height is changed. */
		if (tex_desc->block.width != templ_desc->block.width ||
		    tex_desc->block.height != templ_desc->block.height) {
			unsigned nblks_x = util_format_get_nblocksx(tex->format, width);
			unsigned nblks_y = util_format_get_nblocksy(tex->format, height);

			width = nblks_x * templ_desc->block.width;
			height = nblks_y * templ_desc->block.height;

			width0 = util_format_get_nblocksx(tex->format, width0);
			height0 = util_format_get_nblocksy(tex->format, height0);
		}
	}

	return si_create_surface_custom(pipe, tex, templ,
					  width0, height0,
					  width, height);
}

static void si_surface_destroy(struct pipe_context *pipe,
			       struct pipe_surface *surface)
{
	pipe_resource_reference(&surface->texture, NULL);
	FREE(surface);
}

unsigned si_translate_colorswap(enum pipe_format format, bool do_endian_swap)
{
	const struct util_format_description *desc = util_format_description(format);

#define HAS_SWIZZLE(chan,swz) (desc->swizzle[chan] == PIPE_SWIZZLE_##swz)

	if (format == PIPE_FORMAT_R11G11B10_FLOAT) /* isn't plain */
		return V_028C70_SWAP_STD;

	if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
		return ~0U;

	switch (desc->nr_channels) {
	case 1:
		if (HAS_SWIZZLE(0,X))
			return V_028C70_SWAP_STD; /* X___ */
		else if (HAS_SWIZZLE(3,X))
			return V_028C70_SWAP_ALT_REV; /* ___X */
		break;
	case 2:
		if ((HAS_SWIZZLE(0,X) && HAS_SWIZZLE(1,Y)) ||
		    (HAS_SWIZZLE(0,X) && HAS_SWIZZLE(1,NONE)) ||
		    (HAS_SWIZZLE(0,NONE) && HAS_SWIZZLE(1,Y)))
			return V_028C70_SWAP_STD; /* XY__ */
		else if ((HAS_SWIZZLE(0,Y) && HAS_SWIZZLE(1,X)) ||
			 (HAS_SWIZZLE(0,Y) && HAS_SWIZZLE(1,NONE)) ||
		         (HAS_SWIZZLE(0,NONE) && HAS_SWIZZLE(1,X)))
			/* YX__ */
			return (do_endian_swap ? V_028C70_SWAP_STD : V_028C70_SWAP_STD_REV);
		else if (HAS_SWIZZLE(0,X) && HAS_SWIZZLE(3,Y))
			return V_028C70_SWAP_ALT; /* X__Y */
		else if (HAS_SWIZZLE(0,Y) && HAS_SWIZZLE(3,X))
			return V_028C70_SWAP_ALT_REV; /* Y__X */
		break;
	case 3:
		if (HAS_SWIZZLE(0,X))
			return (do_endian_swap ? V_028C70_SWAP_STD_REV : V_028C70_SWAP_STD);
		else if (HAS_SWIZZLE(0,Z))
			return V_028C70_SWAP_STD_REV; /* ZYX */
		break;
	case 4:
		/* check the middle channels, the 1st and 4th channel can be NONE */
		if (HAS_SWIZZLE(1,Y) && HAS_SWIZZLE(2,Z)) {
			return V_028C70_SWAP_STD; /* XYZW */
		} else if (HAS_SWIZZLE(1,Z) && HAS_SWIZZLE(2,Y)) {
			return V_028C70_SWAP_STD_REV; /* WZYX */
		} else if (HAS_SWIZZLE(1,Y) && HAS_SWIZZLE(2,X)) {
			return V_028C70_SWAP_ALT; /* ZYXW */
		} else if (HAS_SWIZZLE(1,Z) && HAS_SWIZZLE(2,W)) {
			/* YZWX */
			if (desc->is_array)
				return V_028C70_SWAP_ALT_REV;
			else
				return (do_endian_swap ? V_028C70_SWAP_ALT : V_028C70_SWAP_ALT_REV);
		}
		break;
	}
	return ~0U;
}

/* PIPELINE_STAT-BASED DCC ENABLEMENT FOR DISPLAYABLE SURFACES */

static void vi_dcc_clean_up_context_slot(struct si_context *sctx,
					 int slot)
{
	int i;

	if (sctx->dcc_stats[slot].query_active)
		vi_separate_dcc_stop_query(sctx,
					   sctx->dcc_stats[slot].tex);

	for (i = 0; i < ARRAY_SIZE(sctx->dcc_stats[slot].ps_stats); i++)
		if (sctx->dcc_stats[slot].ps_stats[i]) {
			sctx->b.destroy_query(&sctx->b,
					      sctx->dcc_stats[slot].ps_stats[i]);
			sctx->dcc_stats[slot].ps_stats[i] = NULL;
		}

	r600_texture_reference(&sctx->dcc_stats[slot].tex, NULL);
}

/**
 * Return the per-context slot where DCC statistics queries for the texture live.
 */
static unsigned vi_get_context_dcc_stats_index(struct si_context *sctx,
					       struct r600_texture *tex)
{
	int i, empty_slot = -1;

	/* Remove zombie textures (textures kept alive by this array only). */
	for (i = 0; i < ARRAY_SIZE(sctx->dcc_stats); i++)
		if (sctx->dcc_stats[i].tex &&
		    sctx->dcc_stats[i].tex->resource.b.b.reference.count == 1)
			vi_dcc_clean_up_context_slot(sctx, i);

	/* Find the texture. */
	for (i = 0; i < ARRAY_SIZE(sctx->dcc_stats); i++) {
		/* Return if found. */
		if (sctx->dcc_stats[i].tex == tex) {
			sctx->dcc_stats[i].last_use_timestamp = os_time_get();
			return i;
		}

		/* Record the first seen empty slot. */
		if (empty_slot == -1 && !sctx->dcc_stats[i].tex)
			empty_slot = i;
	}

	/* Not found. Remove the oldest member to make space in the array. */
	if (empty_slot == -1) {
		int oldest_slot = 0;

		/* Find the oldest slot. */
		for (i = 1; i < ARRAY_SIZE(sctx->dcc_stats); i++)
			if (sctx->dcc_stats[oldest_slot].last_use_timestamp >
			    sctx->dcc_stats[i].last_use_timestamp)
				oldest_slot = i;

		/* Clean up the oldest slot. */
		vi_dcc_clean_up_context_slot(sctx, oldest_slot);
		empty_slot = oldest_slot;
	}

	/* Add the texture to the new slot. */
	r600_texture_reference(&sctx->dcc_stats[empty_slot].tex, tex);
	sctx->dcc_stats[empty_slot].last_use_timestamp = os_time_get();
	return empty_slot;
}

static struct pipe_query *
vi_create_resuming_pipestats_query(struct si_context *sctx)
{
	struct si_query_hw *query = (struct si_query_hw*)
		sctx->b.create_query(&sctx->b, PIPE_QUERY_PIPELINE_STATISTICS, 0);

	query->flags |= SI_QUERY_HW_FLAG_BEGIN_RESUMES;
	return (struct pipe_query*)query;
}

/**
 * Called when binding a color buffer.
 */
void vi_separate_dcc_start_query(struct si_context *sctx,
				 struct r600_texture *tex)
{
	unsigned i = vi_get_context_dcc_stats_index(sctx, tex);

	assert(!sctx->dcc_stats[i].query_active);

	if (!sctx->dcc_stats[i].ps_stats[0])
		sctx->dcc_stats[i].ps_stats[0] = vi_create_resuming_pipestats_query(sctx);

	/* begin or resume the query */
	sctx->b.begin_query(&sctx->b, sctx->dcc_stats[i].ps_stats[0]);
	sctx->dcc_stats[i].query_active = true;
}

/**
 * Called when unbinding a color buffer.
 */
void vi_separate_dcc_stop_query(struct si_context *sctx,
				struct r600_texture *tex)
{
	unsigned i = vi_get_context_dcc_stats_index(sctx, tex);

	assert(sctx->dcc_stats[i].query_active);
	assert(sctx->dcc_stats[i].ps_stats[0]);

	/* pause or end the query */
	sctx->b.end_query(&sctx->b, sctx->dcc_stats[i].ps_stats[0]);
	sctx->dcc_stats[i].query_active = false;
}

static bool vi_should_enable_separate_dcc(struct r600_texture *tex)
{
	/* The minimum number of fullscreen draws per frame that is required
	 * to enable DCC. */
	return tex->ps_draw_ratio + tex->num_slow_clears >= 5;
}

/* Called by fast clear. */
void vi_separate_dcc_try_enable(struct si_context *sctx,
				struct r600_texture *tex)
{
	/* The intent is to use this with shared displayable back buffers,
	 * but it's not strictly limited only to them.
	 */
	if (!tex->resource.b.is_shared ||
	    !(tex->resource.external_usage & PIPE_HANDLE_USAGE_EXPLICIT_FLUSH) ||
	    tex->resource.b.b.target != PIPE_TEXTURE_2D ||
	    tex->resource.b.b.last_level > 0 ||
	    !tex->surface.dcc_size)
		return;

	if (tex->dcc_offset)
		return; /* already enabled */

	/* Enable the DCC stat gathering. */
	if (!tex->dcc_gather_statistics) {
		tex->dcc_gather_statistics = true;
		vi_separate_dcc_start_query(sctx, tex);
	}

	if (!vi_should_enable_separate_dcc(tex))
		return; /* stats show that DCC decompression is too expensive */

	assert(tex->surface.num_dcc_levels);
	assert(!tex->dcc_separate_buffer);

	si_texture_discard_cmask(sctx->screen, tex);

	/* Get a DCC buffer. */
	if (tex->last_dcc_separate_buffer) {
		assert(tex->dcc_gather_statistics);
		assert(!tex->dcc_separate_buffer);
		tex->dcc_separate_buffer = tex->last_dcc_separate_buffer;
		tex->last_dcc_separate_buffer = NULL;
	} else {
		tex->dcc_separate_buffer = (struct r600_resource*)
			si_aligned_buffer_create(sctx->b.screen,
						   SI_RESOURCE_FLAG_UNMAPPABLE,
						   PIPE_USAGE_DEFAULT,
						   tex->surface.dcc_size,
						   tex->surface.dcc_alignment);
		if (!tex->dcc_separate_buffer)
			return;
	}

	/* dcc_offset is the absolute GPUVM address. */
	tex->dcc_offset = tex->dcc_separate_buffer->gpu_address;

	/* no need to flag anything since this is called by fast clear that
	 * flags framebuffer state
	 */
}

/**
 * Called by pipe_context::flush_resource, the place where DCC decompression
 * takes place.
 */
void vi_separate_dcc_process_and_reset_stats(struct pipe_context *ctx,
					     struct r600_texture *tex)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct pipe_query *tmp;
	unsigned i = vi_get_context_dcc_stats_index(sctx, tex);
	bool query_active = sctx->dcc_stats[i].query_active;
	bool disable = false;

	if (sctx->dcc_stats[i].ps_stats[2]) {
		union pipe_query_result result;

		/* Read the results. */
		ctx->get_query_result(ctx, sctx->dcc_stats[i].ps_stats[2],
				      true, &result);
		si_query_hw_reset_buffers(sctx,
					  (struct si_query_hw*)
					  sctx->dcc_stats[i].ps_stats[2]);

		/* Compute the approximate number of fullscreen draws. */
		tex->ps_draw_ratio =
			result.pipeline_statistics.ps_invocations /
			(tex->resource.b.b.width0 * tex->resource.b.b.height0);
		sctx->last_tex_ps_draw_ratio = tex->ps_draw_ratio;

		disable = tex->dcc_separate_buffer &&
			  !vi_should_enable_separate_dcc(tex);
	}

	tex->num_slow_clears = 0;

	/* stop the statistics query for ps_stats[0] */
	if (query_active)
		vi_separate_dcc_stop_query(sctx, tex);

	/* Move the queries in the queue by one. */
	tmp = sctx->dcc_stats[i].ps_stats[2];
	sctx->dcc_stats[i].ps_stats[2] = sctx->dcc_stats[i].ps_stats[1];
	sctx->dcc_stats[i].ps_stats[1] = sctx->dcc_stats[i].ps_stats[0];
	sctx->dcc_stats[i].ps_stats[0] = tmp;

	/* create and start a new query as ps_stats[0] */
	if (query_active)
		vi_separate_dcc_start_query(sctx, tex);

	if (disable) {
		assert(!tex->last_dcc_separate_buffer);
		tex->last_dcc_separate_buffer = tex->dcc_separate_buffer;
		tex->dcc_separate_buffer = NULL;
		tex->dcc_offset = 0;
		/* no need to flag anything since this is called after
		 * decompression that re-sets framebuffer state
		 */
	}
}

static struct pipe_memory_object *
si_memobj_from_handle(struct pipe_screen *screen,
		      struct winsys_handle *whandle,
		      bool dedicated)
{
	struct si_screen *sscreen = (struct si_screen*)screen;
	struct r600_memory_object *memobj = CALLOC_STRUCT(r600_memory_object);
	struct pb_buffer *buf = NULL;
	uint32_t stride, offset;

	if (!memobj)
		return NULL;

	buf = sscreen->ws->buffer_from_handle(sscreen->ws, whandle,
					      &stride, &offset);
	if (!buf) {
		free(memobj);
		return NULL;
	}

	memobj->b.dedicated = dedicated;
	memobj->buf = buf;
	memobj->stride = stride;
	memobj->offset = offset;

	return (struct pipe_memory_object *)memobj;

}

static void
si_memobj_destroy(struct pipe_screen *screen,
		  struct pipe_memory_object *_memobj)
{
	struct r600_memory_object *memobj = (struct r600_memory_object *)_memobj;

	pb_reference(&memobj->buf, NULL);
	free(memobj);
}

static struct pipe_resource *
si_texture_from_memobj(struct pipe_screen *screen,
		       const struct pipe_resource *templ,
		       struct pipe_memory_object *_memobj,
		       uint64_t offset)
{
	int r;
	struct si_screen *sscreen = (struct si_screen*)screen;
	struct r600_memory_object *memobj = (struct r600_memory_object *)_memobj;
	struct r600_texture *rtex;
	struct radeon_surf surface = {};
	struct radeon_bo_metadata metadata = {};
	enum radeon_surf_mode array_mode;
	bool is_scanout;
	struct pb_buffer *buf = NULL;

	if (memobj->b.dedicated) {
		sscreen->ws->buffer_get_metadata(memobj->buf, &metadata);
		si_surface_import_metadata(sscreen, &surface, &metadata,
				     &array_mode, &is_scanout);
	} else {
		/**
		 * The bo metadata is unset for un-dedicated images. So we fall
		 * back to linear. See answer to question 5 of the
		 * VK_KHX_external_memory spec for some details.
		 *
		 * It is possible that this case isn't going to work if the
		 * surface pitch isn't correctly aligned by default.
		 *
		 * In order to support it correctly we require multi-image
		 * metadata to be syncrhonized between radv and radeonsi. The
		 * semantics of associating multiple image metadata to a memory
		 * object on the vulkan export side are not concretely defined
		 * either.
		 *
		 * All the use cases we are aware of at the moment for memory
		 * objects use dedicated allocations. So lets keep the initial
		 * implementation simple.
		 *
		 * A possible alternative is to attempt to reconstruct the
		 * tiling information when the TexParameter TEXTURE_TILING_EXT
		 * is set.
		 */
		array_mode = RADEON_SURF_MODE_LINEAR_ALIGNED;
		is_scanout = false;

	}

	r = si_init_surface(sscreen, &surface, templ,
			      array_mode, memobj->stride,
			      offset, true, is_scanout,
			      false, false);
	if (r)
		return NULL;

	rtex = si_texture_create_object(screen, templ, memobj->buf, &surface);
	if (!rtex)
		return NULL;

	/* r600_texture_create_object doesn't increment refcount of
	 * memobj->buf, so increment it here.
	 */
	pb_reference(&buf, memobj->buf);

	rtex->resource.b.is_shared = true;
	rtex->resource.external_usage = PIPE_HANDLE_USAGE_READ_WRITE;

	si_apply_opaque_metadata(sscreen, rtex, &metadata);

	return &rtex->resource.b.b;
}

static bool si_check_resource_capability(struct pipe_screen *screen,
					 struct pipe_resource *resource,
					 unsigned bind)
{
	struct r600_texture *tex = (struct r600_texture*)resource;

	/* Buffers only support the linear flag. */
	if (resource->target == PIPE_BUFFER)
		return (bind & ~PIPE_BIND_LINEAR) == 0;

	if (bind & PIPE_BIND_LINEAR && !tex->surface.is_linear)
		return false;

	if (bind & PIPE_BIND_SCANOUT && !tex->surface.is_displayable)
		return false;

	/* TODO: PIPE_BIND_CURSOR - do we care? */
	return true;
}

void si_init_screen_texture_functions(struct si_screen *sscreen)
{
	sscreen->b.resource_from_handle = si_texture_from_handle;
	sscreen->b.resource_get_handle = si_texture_get_handle;
	sscreen->b.resource_from_memobj = si_texture_from_memobj;
	sscreen->b.memobj_create_from_handle = si_memobj_from_handle;
	sscreen->b.memobj_destroy = si_memobj_destroy;
	sscreen->b.check_resource_capability = si_check_resource_capability;
}

void si_init_context_texture_functions(struct si_context *sctx)
{
	sctx->b.create_surface = si_create_surface;
	sctx->b.surface_destroy = si_surface_destroy;
}
