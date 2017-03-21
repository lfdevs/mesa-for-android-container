/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 *
 * Authors:
 *      Marek Olšák <marek.olsak@amd.com>
 */

/* Resource binding slots and sampler states (each described with 8 or
 * 4 dwords) are stored in lists in memory which is accessed by shaders
 * using scalar load instructions.
 *
 * This file is responsible for managing such lists. It keeps a copy of all
 * descriptors in CPU memory and re-uploads a whole list if some slots have
 * been changed.
 *
 * This code is also reponsible for updating shader pointers to those lists.
 *
 * Note that CP DMA can't be used for updating the lists, because a GPU hang
 * could leave the list in a mid-IB state and the next IB would get wrong
 * descriptors and the whole context would be unusable at that point.
 * (Note: The register shadowing can't be used due to the same reason)
 *
 * Also, uploading descriptors to newly allocated memory doesn't require
 * a KCACHE flush.
 *
 *
 * Possible scenarios for one 16 dword image+sampler slot:
 *
 *       | Image        | w/ FMASK   | Buffer       | NULL
 * [ 0: 3] Image[0:3]   | Image[0:3] | Null[0:3]    | Null[0:3]
 * [ 4: 7] Image[4:7]   | Image[4:7] | Buffer[0:3]  | 0
 * [ 8:11] Null[0:3]    | Fmask[0:3] | Null[0:3]    | Null[0:3]
 * [12:15] Sampler[0:3] | Fmask[4:7] | Sampler[0:3] | Sampler[0:3]
 *
 * FMASK implies MSAA, therefore no sampler state.
 * Sampler states are never unbound except when FMASK is bound.
 */

#include "radeon/r600_cs.h"
#include "si_pipe.h"
#include "sid.h"

#include "util/u_format.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"


/* NULL image and buffer descriptor for textures (alpha = 1) and images
 * (alpha = 0).
 *
 * For images, all fields must be zero except for the swizzle, which
 * supports arbitrary combinations of 0s and 1s. The texture type must be
 * any valid type (e.g. 1D). If the texture type isn't set, the hw hangs.
 *
 * For buffers, all fields must be zero. If they are not, the hw hangs.
 *
 * This is the only reason why the buffer descriptor must be in words [4:7].
 */
static uint32_t null_texture_descriptor[8] = {
	0,
	0,
	0,
	S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_1) |
	S_008F1C_TYPE(V_008F1C_SQ_RSRC_IMG_1D)
	/* the rest must contain zeros, which is also used by the buffer
	 * descriptor */
};

static uint32_t null_image_descriptor[8] = {
	0,
	0,
	0,
	S_008F1C_TYPE(V_008F1C_SQ_RSRC_IMG_1D)
	/* the rest must contain zeros, which is also used by the buffer
	 * descriptor */
};

static void si_init_descriptors(struct si_descriptors *desc,
				unsigned shader_userdata_index,
				unsigned element_dw_size,
				unsigned num_elements,
				const uint32_t *null_descriptor,
				unsigned *ce_offset)
{
	int i;

	assert(num_elements <= sizeof(desc->dirty_mask)*8);

	desc->list = CALLOC(num_elements, element_dw_size * 4);
	desc->element_dw_size = element_dw_size;
	desc->num_elements = num_elements;
	desc->dirty_mask = num_elements == 32 ? ~0u : (1u << num_elements) - 1;
	desc->shader_userdata_offset = shader_userdata_index * 4;

	if (ce_offset) {
		desc->ce_offset = *ce_offset;

		/* make sure that ce_offset stays 32 byte aligned */
		*ce_offset += align(element_dw_size * num_elements * 4, 32);
	}

	/* Initialize the array to NULL descriptors if the element size is 8. */
	if (null_descriptor) {
		assert(element_dw_size % 8 == 0);
		for (i = 0; i < num_elements * element_dw_size / 8; i++)
			memcpy(desc->list + i * 8, null_descriptor,
			       8 * 4);
	}
}

static void si_release_descriptors(struct si_descriptors *desc)
{
	r600_resource_reference(&desc->buffer, NULL);
	FREE(desc->list);
}

static bool si_ce_upload(struct si_context *sctx, unsigned ce_offset, unsigned size,
			 unsigned *out_offset, struct r600_resource **out_buf) {
	uint64_t va;

	u_suballocator_alloc(sctx->ce_suballocator, size, 64, out_offset,
			     (struct pipe_resource**)out_buf);
	if (!out_buf)
			return false;

	va = (*out_buf)->gpu_address + *out_offset;

	radeon_emit(sctx->ce_ib, PKT3(PKT3_DUMP_CONST_RAM, 3, 0));
	radeon_emit(sctx->ce_ib, ce_offset);
	radeon_emit(sctx->ce_ib, size / 4);
	radeon_emit(sctx->ce_ib, va);
	radeon_emit(sctx->ce_ib, va >> 32);

	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, *out_buf,
	                       RADEON_USAGE_READWRITE, RADEON_PRIO_DESCRIPTORS);

	sctx->ce_need_synchronization = true;
	return true;
}

static void si_ce_reinitialize_descriptors(struct si_context *sctx,
                                           struct si_descriptors *desc)
{
	if (desc->buffer) {
		struct r600_resource *buffer = (struct r600_resource*)desc->buffer;
		unsigned list_size = desc->num_elements * desc->element_dw_size * 4;
		uint64_t va = buffer->gpu_address + desc->buffer_offset;
		struct radeon_winsys_cs *ib = sctx->ce_preamble_ib;

		if (!ib)
			ib = sctx->ce_ib;

		list_size = align(list_size, 32);

		radeon_emit(ib, PKT3(PKT3_LOAD_CONST_RAM, 3, 0));
		radeon_emit(ib, va);
		radeon_emit(ib, va >> 32);
		radeon_emit(ib, list_size / 4);
		radeon_emit(ib, desc->ce_offset);

		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, desc->buffer,
		                    RADEON_USAGE_READ, RADEON_PRIO_DESCRIPTORS);
	}
	desc->ce_ram_dirty = false;
}

void si_ce_reinitialize_all_descriptors(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_DESCS; ++i)
		si_ce_reinitialize_descriptors(sctx, &sctx->descriptors[i]);
}

void si_ce_enable_loads(struct radeon_winsys_cs *ib)
{
	radeon_emit(ib, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
	radeon_emit(ib, CONTEXT_CONTROL_LOAD_ENABLE(1) |
	                CONTEXT_CONTROL_LOAD_CE_RAM(1));
	radeon_emit(ib, CONTEXT_CONTROL_SHADOW_ENABLE(1));
}

static bool si_upload_descriptors(struct si_context *sctx,
				  struct si_descriptors *desc,
				  struct r600_atom * atom)
{
	unsigned list_size = desc->num_elements * desc->element_dw_size * 4;

	if (!desc->dirty_mask)
		return true;

	if (sctx->ce_ib) {
		uint32_t const* list = (uint32_t const*)desc->list;

		if (desc->ce_ram_dirty)
			si_ce_reinitialize_descriptors(sctx, desc);

		while(desc->dirty_mask) {
			int begin, count;
			u_bit_scan_consecutive_range(&desc->dirty_mask, &begin,
						     &count);

			begin *= desc->element_dw_size;
			count *= desc->element_dw_size;

			radeon_emit(sctx->ce_ib,
			            PKT3(PKT3_WRITE_CONST_RAM, count, 0));
			radeon_emit(sctx->ce_ib, desc->ce_offset + begin * 4);
			radeon_emit_array(sctx->ce_ib, list + begin, count);
		}

		if (!si_ce_upload(sctx, desc->ce_offset, list_size,
		                           &desc->buffer_offset, &desc->buffer))
			return false;
	} else {
		void *ptr;

		u_upload_alloc(sctx->b.uploader, 0, list_size, 256,
			&desc->buffer_offset,
			(struct pipe_resource**)&desc->buffer, &ptr);
		if (!desc->buffer)
			return false; /* skip the draw call */

		util_memcpy_cpu_to_le32(ptr, desc->list, list_size);
		desc->gpu_list = ptr;

		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, desc->buffer,
	                            RADEON_USAGE_READ, RADEON_PRIO_DESCRIPTORS);
	}
	desc->dirty_mask = 0;

	if (atom)
		si_mark_atom_dirty(sctx, atom);

	return true;
}

static void
si_descriptors_begin_new_cs(struct si_context *sctx, struct si_descriptors *desc)
{
	desc->ce_ram_dirty = true;

	if (!desc->buffer)
		return;

	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, desc->buffer,
				  RADEON_USAGE_READ, RADEON_PRIO_DESCRIPTORS);
}

/* SAMPLER VIEWS */

static unsigned
si_sampler_descriptors_idx(unsigned shader)
{
	return SI_DESCS_FIRST_SHADER + shader * SI_NUM_SHADER_DESCS +
	       SI_SHADER_DESCS_SAMPLERS;
}

static struct si_descriptors *
si_sampler_descriptors(struct si_context *sctx, unsigned shader)
{
	return &sctx->descriptors[si_sampler_descriptors_idx(shader)];
}

static void si_release_sampler_views(struct si_sampler_views *views)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(views->views); i++) {
		pipe_sampler_view_reference(&views->views[i], NULL);
	}
}

static void si_sampler_view_add_buffer(struct si_context *sctx,
				       struct pipe_resource *resource,
				       enum radeon_bo_usage usage,
				       bool is_stencil_sampler,
				       bool check_mem)
{
	struct r600_resource *rres;
	struct r600_texture *rtex;
	enum radeon_bo_priority priority;

	if (!resource)
		return;

	if (resource->target != PIPE_BUFFER) {
		struct r600_texture *tex = (struct r600_texture*)resource;

		if (tex->is_depth && !r600_can_sample_zs(tex, is_stencil_sampler))
			resource = &tex->flushed_depth_texture->resource.b.b;
	}

	rres = (struct r600_resource*)resource;
	priority = r600_get_sampler_view_priority(rres);

	radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
					    rres, usage, priority,
					    check_mem);

	if (resource->target == PIPE_BUFFER)
		return;

	/* Now add separate DCC or HTILE. */
	rtex = (struct r600_texture*)resource;
	if (rtex->dcc_separate_buffer) {
		radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
						    rtex->dcc_separate_buffer, usage,
						    RADEON_PRIO_DCC, check_mem);
	}

	if (rtex->htile_buffer &&
	    rtex->tc_compatible_htile &&
	    !is_stencil_sampler) {
		radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
						    rtex->htile_buffer, usage,
						    RADEON_PRIO_HTILE, check_mem);
	}
}

static void si_sampler_views_begin_new_cs(struct si_context *sctx,
					  struct si_sampler_views *views)
{
	unsigned mask = views->enabled_mask;

	/* Add buffers to the CS. */
	while (mask) {
		int i = u_bit_scan(&mask);
		struct si_sampler_view *sview = (struct si_sampler_view *)views->views[i];

		si_sampler_view_add_buffer(sctx, sview->base.texture,
					   RADEON_USAGE_READ,
					   sview->is_stencil_sampler, false);
	}
}

/* Set buffer descriptor fields that can be changed by reallocations. */
static void si_set_buf_desc_address(struct r600_resource *buf,
				    uint64_t offset, uint32_t *state)
{
	uint64_t va = buf->gpu_address + offset;

	state[0] = va;
	state[1] &= C_008F04_BASE_ADDRESS_HI;
	state[1] |= S_008F04_BASE_ADDRESS_HI(va >> 32);
}

/* Set texture descriptor fields that can be changed by reallocations.
 *
 * \param tex			texture
 * \param base_level_info	information of the level of BASE_ADDRESS
 * \param base_level		the level of BASE_ADDRESS
 * \param first_level		pipe_sampler_view.u.tex.first_level
 * \param block_width		util_format_get_blockwidth()
 * \param is_stencil		select between separate Z & Stencil
 * \param state			descriptor to update
 */
void si_set_mutable_tex_desc_fields(struct r600_texture *tex,
				    const struct radeon_surf_level *base_level_info,
				    unsigned base_level, unsigned first_level,
				    unsigned block_width, bool is_stencil,
				    uint32_t *state)
{
	uint64_t va;
	unsigned pitch = base_level_info->nblk_x * block_width;

	if (tex->is_depth && !r600_can_sample_zs(tex, is_stencil)) {
		tex = tex->flushed_depth_texture;
		is_stencil = false;
	}

	va = tex->resource.gpu_address + base_level_info->offset;

	state[1] &= C_008F14_BASE_ADDRESS_HI;
	state[3] &= C_008F1C_TILING_INDEX;
	state[4] &= C_008F20_PITCH;
	state[6] &= C_008F28_COMPRESSION_EN;

	state[0] = va >> 8;
	state[1] |= S_008F14_BASE_ADDRESS_HI(va >> 40);
	state[3] |= S_008F1C_TILING_INDEX(si_tile_mode_index(tex, base_level,
							     is_stencil));
	state[4] |= S_008F20_PITCH(pitch - 1);

	if (tex->dcc_offset && first_level < tex->surface.num_dcc_levels) {
		state[6] |= S_008F28_COMPRESSION_EN(1);
		state[7] = ((!tex->dcc_separate_buffer ? tex->resource.gpu_address : 0) +
			    tex->dcc_offset +
			    base_level_info->dcc_offset) >> 8;
	} else if (tex->tc_compatible_htile) {
		state[6] |= S_008F28_COMPRESSION_EN(1);
		state[7] = tex->htile_buffer->gpu_address >> 8;
	}
}

static void si_set_sampler_view(struct si_context *sctx,
				unsigned shader,
				unsigned slot, struct pipe_sampler_view *view,
				bool disallow_early_out)
{
	struct si_sampler_views *views = &sctx->samplers[shader].views;
	struct si_sampler_view *rview = (struct si_sampler_view*)view;
	struct si_descriptors *descs = si_sampler_descriptors(sctx, shader);
	uint32_t *desc = descs->list + slot * 16;

	if (views->views[slot] == view && !disallow_early_out)
		return;

	if (view) {
		struct r600_texture *rtex = (struct r600_texture *)view->texture;

		assert(rtex); /* views with texture == NULL aren't supported */
		pipe_sampler_view_reference(&views->views[slot], view);
		memcpy(desc, rview->state, 8*4);

		if (rtex->resource.b.b.target == PIPE_BUFFER) {
			rtex->resource.bind_history |= PIPE_BIND_SAMPLER_VIEW;

			si_set_buf_desc_address(&rtex->resource,
						view->u.buf.offset,
						desc + 4);
		} else {
			bool is_separate_stencil =
				rtex->db_compatible &&
				rview->is_stencil_sampler;

			si_set_mutable_tex_desc_fields(rtex,
						       rview->base_level_info,
						       rview->base_level,
						       rview->base.u.tex.first_level,
						       rview->block_width,
						       is_separate_stencil,
						       desc);
		}

		if (rtex->resource.b.b.target != PIPE_BUFFER &&
		    rtex->fmask.size) {
			memcpy(desc + 8,
			       rview->fmask_state, 8*4);
		} else {
			/* Disable FMASK and bind sampler state in [12:15]. */
			memcpy(desc + 8,
			       null_texture_descriptor, 4*4);

			if (views->sampler_states[slot])
				memcpy(desc + 12,
				       views->sampler_states[slot]->val, 4*4);
		}

		views->enabled_mask |= 1u << slot;

		/* Since this can flush, it must be done after enabled_mask is
		 * updated. */
		si_sampler_view_add_buffer(sctx, view->texture,
					   RADEON_USAGE_READ,
					   rview->is_stencil_sampler, true);
	} else {
		pipe_sampler_view_reference(&views->views[slot], NULL);
		memcpy(desc, null_texture_descriptor, 8*4);
		/* Only clear the lower dwords of FMASK. */
		memcpy(desc + 8, null_texture_descriptor, 4*4);
		/* Re-set the sampler state if we are transitioning from FMASK. */
		if (views->sampler_states[slot])
			memcpy(desc + 12,
			       views->sampler_states[slot]->val, 4*4);

		views->enabled_mask &= ~(1u << slot);
	}

	descs->dirty_mask |= 1u << slot;
	sctx->descriptors_dirty |= 1u << si_sampler_descriptors_idx(shader);
}

static bool is_compressed_colortex(struct r600_texture *rtex)
{
	return rtex->cmask.size || rtex->fmask.size ||
	       (rtex->dcc_offset && rtex->dirty_level_mask);
}

static void si_update_compressed_tex_shader_mask(struct si_context *sctx,
						 unsigned shader)
{
	struct si_textures_info *samplers = &sctx->samplers[shader];
	unsigned shader_bit = 1 << shader;

	if (samplers->depth_texture_mask ||
	    samplers->compressed_colortex_mask ||
	    sctx->images[shader].compressed_colortex_mask)
		sctx->compressed_tex_shader_mask |= shader_bit;
	else
		sctx->compressed_tex_shader_mask &= ~shader_bit;
}

static void si_set_sampler_views(struct pipe_context *ctx,
				 enum pipe_shader_type shader, unsigned start,
                                 unsigned count,
				 struct pipe_sampler_view **views)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_textures_info *samplers = &sctx->samplers[shader];
	int i;

	if (!count || shader >= SI_NUM_SHADERS)
		return;

	for (i = 0; i < count; i++) {
		unsigned slot = start + i;

		if (!views || !views[i]) {
			samplers->depth_texture_mask &= ~(1u << slot);
			samplers->compressed_colortex_mask &= ~(1u << slot);
			si_set_sampler_view(sctx, shader, slot, NULL, false);
			continue;
		}

		si_set_sampler_view(sctx, shader, slot, views[i], false);

		if (views[i]->texture && views[i]->texture->target != PIPE_BUFFER) {
			struct r600_texture *rtex =
				(struct r600_texture*)views[i]->texture;
			struct si_sampler_view *rview = (struct si_sampler_view *)views[i];

			if (rtex->db_compatible &&
			    (!rtex->tc_compatible_htile || rview->is_stencil_sampler)) {
				samplers->depth_texture_mask |= 1u << slot;
			} else {
				samplers->depth_texture_mask &= ~(1u << slot);
			}
			if (is_compressed_colortex(rtex)) {
				samplers->compressed_colortex_mask |= 1u << slot;
			} else {
				samplers->compressed_colortex_mask &= ~(1u << slot);
			}

			if (rtex->dcc_offset &&
			    p_atomic_read(&rtex->framebuffers_bound))
				sctx->need_check_render_feedback = true;
		} else {
			samplers->depth_texture_mask &= ~(1u << slot);
			samplers->compressed_colortex_mask &= ~(1u << slot);
		}
	}

	si_update_compressed_tex_shader_mask(sctx, shader);
}

static void
si_samplers_update_compressed_colortex_mask(struct si_textures_info *samplers)
{
	unsigned mask = samplers->views.enabled_mask;

	while (mask) {
		int i = u_bit_scan(&mask);
		struct pipe_resource *res = samplers->views.views[i]->texture;

		if (res && res->target != PIPE_BUFFER) {
			struct r600_texture *rtex = (struct r600_texture *)res;

			if (is_compressed_colortex(rtex)) {
				samplers->compressed_colortex_mask |= 1u << i;
			} else {
				samplers->compressed_colortex_mask &= ~(1u << i);
			}
		}
	}
}

/* IMAGE VIEWS */

static unsigned
si_image_descriptors_idx(unsigned shader)
{
	return SI_DESCS_FIRST_SHADER + shader * SI_NUM_SHADER_DESCS +
	       SI_SHADER_DESCS_IMAGES;
}

static struct si_descriptors*
si_image_descriptors(struct si_context *sctx, unsigned shader)
{
	return &sctx->descriptors[si_image_descriptors_idx(shader)];
}

static void
si_release_image_views(struct si_images_info *images)
{
	unsigned i;

	for (i = 0; i < SI_NUM_IMAGES; ++i) {
		struct pipe_image_view *view = &images->views[i];

		pipe_resource_reference(&view->resource, NULL);
	}
}

static void
si_image_views_begin_new_cs(struct si_context *sctx, struct si_images_info *images)
{
	uint mask = images->enabled_mask;

	/* Add buffers to the CS. */
	while (mask) {
		int i = u_bit_scan(&mask);
		struct pipe_image_view *view = &images->views[i];

		assert(view->resource);

		si_sampler_view_add_buffer(sctx, view->resource,
					   RADEON_USAGE_READWRITE, false, false);
	}
}

static void
si_disable_shader_image(struct si_context *ctx, unsigned shader, unsigned slot)
{
	struct si_images_info *images = &ctx->images[shader];

	if (images->enabled_mask & (1u << slot)) {
		struct si_descriptors *descs = si_image_descriptors(ctx, shader);

		pipe_resource_reference(&images->views[slot].resource, NULL);
		images->compressed_colortex_mask &= ~(1 << slot);

		memcpy(descs->list + slot*8, null_image_descriptor, 8*4);
		images->enabled_mask &= ~(1u << slot);
		descs->dirty_mask |= 1u << slot;
		ctx->descriptors_dirty |= 1u << si_image_descriptors_idx(shader);
	}
}

static void
si_mark_image_range_valid(const struct pipe_image_view *view)
{
	struct r600_resource *res = (struct r600_resource *)view->resource;

	assert(res && res->b.b.target == PIPE_BUFFER);

	util_range_add(&res->valid_buffer_range,
		       view->u.buf.offset,
		       view->u.buf.offset + view->u.buf.size);
}

static void si_set_shader_image(struct si_context *ctx,
				unsigned shader,
				unsigned slot, const struct pipe_image_view *view,
				bool skip_decompress)
{
	struct si_screen *screen = ctx->screen;
	struct si_images_info *images = &ctx->images[shader];
	struct si_descriptors *descs = si_image_descriptors(ctx, shader);
	struct r600_resource *res;
	uint32_t *desc = descs->list + slot * 8;

	if (!view || !view->resource) {
		si_disable_shader_image(ctx, shader, slot);
		return;
	}

	res = (struct r600_resource *)view->resource;

	if (&images->views[slot] != view)
		util_copy_image_view(&images->views[slot], view);

	if (res->b.b.target == PIPE_BUFFER) {
		if (view->access & PIPE_IMAGE_ACCESS_WRITE)
			si_mark_image_range_valid(view);

		si_make_buffer_descriptor(screen, res,
					  view->format,
					  view->u.buf.offset,
					  view->u.buf.size,
					  descs->list + slot * 8);
		si_set_buf_desc_address(res, view->u.buf.offset, desc + 4);

		images->compressed_colortex_mask &= ~(1 << slot);
		res->bind_history |= PIPE_BIND_SHADER_IMAGE;
	} else {
		static const unsigned char swizzle[4] = { 0, 1, 2, 3 };
		struct r600_texture *tex = (struct r600_texture *)res;
		unsigned level = view->u.tex.level;
		unsigned width, height, depth;
		bool uses_dcc = tex->dcc_offset &&
				level < tex->surface.num_dcc_levels;

		assert(!tex->is_depth);
		assert(tex->fmask.size == 0);

		if (uses_dcc && !skip_decompress &&
		    (view->access & PIPE_IMAGE_ACCESS_WRITE ||
		     !vi_dcc_formats_compatible(res->b.b.format, view->format))) {
			/* If DCC can't be disabled, at least decompress it.
			 * The decompression is relatively cheap if the surface
			 * has been decompressed already.
			 */
			if (r600_texture_disable_dcc(&ctx->b, tex))
				uses_dcc = false;
			else
				ctx->b.decompress_dcc(&ctx->b.b, tex);
		}

		if (is_compressed_colortex(tex)) {
			images->compressed_colortex_mask |= 1 << slot;
		} else {
			images->compressed_colortex_mask &= ~(1 << slot);
		}

		if (uses_dcc &&
		    p_atomic_read(&tex->framebuffers_bound))
			ctx->need_check_render_feedback = true;

		/* Always force the base level to the selected level.
		 *
		 * This is required for 3D textures, where otherwise
		 * selecting a single slice for non-layered bindings
		 * fails. It doesn't hurt the other targets.
		 */
		width = u_minify(res->b.b.width0, level);
		height = u_minify(res->b.b.height0, level);
		depth = u_minify(res->b.b.depth0, level);

		si_make_texture_descriptor(screen, tex,
					   false, res->b.b.target,
					   view->format, swizzle,
					   0, 0,
					   view->u.tex.first_layer,
					   view->u.tex.last_layer,
					   width, height, depth,
					   desc, NULL);
		si_set_mutable_tex_desc_fields(tex, &tex->surface.level[level],
					       level, level,
					       util_format_get_blockwidth(view->format),
					       false, desc);
	}

	images->enabled_mask |= 1u << slot;
	descs->dirty_mask |= 1u << slot;
	ctx->descriptors_dirty |= 1u << si_image_descriptors_idx(shader);

	/* Since this can flush, it must be done after enabled_mask is updated. */
	si_sampler_view_add_buffer(ctx, &res->b.b,
				   RADEON_USAGE_READWRITE, false, true);
}

static void
si_set_shader_images(struct pipe_context *pipe,
		     enum pipe_shader_type shader,
		     unsigned start_slot, unsigned count,
		     const struct pipe_image_view *views)
{
	struct si_context *ctx = (struct si_context *)pipe;
	unsigned i, slot;

	assert(shader < SI_NUM_SHADERS);

	if (!count)
		return;

	assert(start_slot + count <= SI_NUM_IMAGES);

	if (views) {
		for (i = 0, slot = start_slot; i < count; ++i, ++slot)
			si_set_shader_image(ctx, shader, slot, &views[i], false);
	} else {
		for (i = 0, slot = start_slot; i < count; ++i, ++slot)
			si_set_shader_image(ctx, shader, slot, NULL, false);
	}

	si_update_compressed_tex_shader_mask(ctx, shader);
}

static void
si_images_update_compressed_colortex_mask(struct si_images_info *images)
{
	unsigned mask = images->enabled_mask;

	while (mask) {
		int i = u_bit_scan(&mask);
		struct pipe_resource *res = images->views[i].resource;

		if (res && res->target != PIPE_BUFFER) {
			struct r600_texture *rtex = (struct r600_texture *)res;

			if (is_compressed_colortex(rtex)) {
				images->compressed_colortex_mask |= 1 << i;
			} else {
				images->compressed_colortex_mask &= ~(1 << i);
			}
		}
	}
}

/* SAMPLER STATES */

static void si_bind_sampler_states(struct pipe_context *ctx,
                                   enum pipe_shader_type shader,
                                   unsigned start, unsigned count, void **states)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_textures_info *samplers = &sctx->samplers[shader];
	struct si_descriptors *desc = si_sampler_descriptors(sctx, shader);
	struct si_sampler_state **sstates = (struct si_sampler_state**)states;
	int i;

	if (!count || shader >= SI_NUM_SHADERS)
		return;

	for (i = 0; i < count; i++) {
		unsigned slot = start + i;

		if (!sstates[i] ||
		    sstates[i] == samplers->views.sampler_states[slot])
			continue;

#ifdef DEBUG
		assert(sstates[i]->magic == SI_SAMPLER_STATE_MAGIC);
#endif
		samplers->views.sampler_states[slot] = sstates[i];

		/* If FMASK is bound, don't overwrite it.
		 * The sampler state will be set after FMASK is unbound.
		 */
		if (samplers->views.views[slot] &&
		    samplers->views.views[slot]->texture &&
		    samplers->views.views[slot]->texture->target != PIPE_BUFFER &&
		    ((struct r600_texture*)samplers->views.views[slot]->texture)->fmask.size)
			continue;

		memcpy(desc->list + slot * 16 + 12, sstates[i]->val, 4*4);
		desc->dirty_mask |= 1u << slot;
		sctx->descriptors_dirty |= 1u << si_sampler_descriptors_idx(shader);
	}
}

/* BUFFER RESOURCES */

static void si_init_buffer_resources(struct si_buffer_resources *buffers,
				     struct si_descriptors *descs,
				     unsigned num_buffers,
				     unsigned shader_userdata_index,
				     enum radeon_bo_usage shader_usage,
				     enum radeon_bo_priority priority,
				     unsigned *ce_offset)
{
	buffers->shader_usage = shader_usage;
	buffers->priority = priority;
	buffers->buffers = CALLOC(num_buffers, sizeof(struct pipe_resource*));

	si_init_descriptors(descs, shader_userdata_index, 4,
			    num_buffers, NULL, ce_offset);
}

static void si_release_buffer_resources(struct si_buffer_resources *buffers,
					struct si_descriptors *descs)
{
	int i;

	for (i = 0; i < descs->num_elements; i++) {
		pipe_resource_reference(&buffers->buffers[i], NULL);
	}

	FREE(buffers->buffers);
}

static void si_buffer_resources_begin_new_cs(struct si_context *sctx,
					     struct si_buffer_resources *buffers)
{
	unsigned mask = buffers->enabled_mask;

	/* Add buffers to the CS. */
	while (mask) {
		int i = u_bit_scan(&mask);

		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
				      (struct r600_resource*)buffers->buffers[i],
				      buffers->shader_usage, buffers->priority);
	}
}

static void si_get_buffer_from_descriptors(struct si_buffer_resources *buffers,
					   struct si_descriptors *descs,
					   unsigned idx, struct pipe_resource **buf,
					   unsigned *offset, unsigned *size)
{
	pipe_resource_reference(buf, buffers->buffers[idx]);
	if (*buf) {
		struct r600_resource *res = r600_resource(*buf);
		const uint32_t *desc = descs->list + idx * 4;
		uint64_t va;

		*size = desc[2];

		assert(G_008F04_STRIDE(desc[1]) == 0);
		va = ((uint64_t)desc[1] << 32) | desc[0];

		assert(va >= res->gpu_address && va + *size <= res->gpu_address + res->bo_size);
		*offset = va - res->gpu_address;
	}
}

/* VERTEX BUFFERS */

static void si_vertex_buffers_begin_new_cs(struct si_context *sctx)
{
	struct si_descriptors *desc = &sctx->vertex_buffers;
	int count = sctx->vertex_elements ? sctx->vertex_elements->count : 0;
	int i;

	for (i = 0; i < count; i++) {
		int vb = sctx->vertex_elements->elements[i].vertex_buffer_index;

		if (vb >= ARRAY_SIZE(sctx->vertex_buffer))
			continue;
		if (!sctx->vertex_buffer[vb].buffer)
			continue;

		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
				      (struct r600_resource*)sctx->vertex_buffer[vb].buffer,
				      RADEON_USAGE_READ, RADEON_PRIO_VERTEX_BUFFER);
	}

	if (!desc->buffer)
		return;
	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
			      desc->buffer, RADEON_USAGE_READ,
			      RADEON_PRIO_DESCRIPTORS);
}

bool si_upload_vertex_buffer_descriptors(struct si_context *sctx)
{
	struct si_vertex_element *velems = sctx->vertex_elements;
	struct si_descriptors *desc = &sctx->vertex_buffers;
	unsigned i, count = velems->count;
	uint64_t va;
	uint32_t *ptr;

	if (!sctx->vertex_buffers_dirty || !count || !velems)
		return true;

	unsigned fix_size3 = velems->fix_size3;
	unsigned first_vb_use_mask = velems->first_vb_use_mask;

	/* Vertex buffer descriptors are the only ones which are uploaded
	 * directly through a staging buffer and don't go through
	 * the fine-grained upload path.
	 */
	u_upload_alloc(sctx->b.uploader, 0, count * 16, 256, &desc->buffer_offset,
		       (struct pipe_resource**)&desc->buffer, (void**)&ptr);
	if (!desc->buffer)
		return false;

	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
			      desc->buffer, RADEON_USAGE_READ,
			      RADEON_PRIO_DESCRIPTORS);

	assert(count <= SI_NUM_VERTEX_BUFFERS);

	for (i = 0; i < count; i++) {
		struct pipe_vertex_element *ve = &velems->elements[i];
		struct pipe_vertex_buffer *vb;
		struct r600_resource *rbuffer;
		unsigned offset;
		unsigned vbo_index = ve->vertex_buffer_index;
		uint32_t *desc = &ptr[i*4];

		vb = &sctx->vertex_buffer[vbo_index];
		rbuffer = (struct r600_resource*)vb->buffer;
		if (!rbuffer) {
			memset(desc, 0, 16);
			continue;
		}

		offset = vb->buffer_offset + ve->src_offset;
		va = rbuffer->gpu_address + offset;

		/* Fill in T# buffer resource description */
		desc[0] = va;
		desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) |
			  S_008F04_STRIDE(vb->stride);

		if (sctx->b.chip_class <= CIK && vb->stride) {
			/* Round up by rounding down and adding 1 */
			desc[2] = (vb->buffer->width0 - offset -
				   velems->format_size[i]) /
				  vb->stride + 1;
		} else {
			uint32_t size3;

			desc[2] = vb->buffer->width0 - offset;

			/* For attributes of size 3 with byte or short
			 * components, we use a 4-component data format.
			 *
			 * As a consequence, we have to round the buffer size
			 * up so that the hardware sees four components as
			 * being inside the buffer if and only if the first
			 * three components are in the buffer.
			 *
			 * Since the offset and stride are guaranteed to be
			 * 4-byte aligned, this alignment will never cross the
			 * winsys buffer boundary.
			 */
			size3 = (fix_size3 >> (2 * i)) & 3;
			if (vb->stride && size3) {
				assert(offset % 4 == 0 && vb->stride % 4 == 0);
				assert(size3 <= 2);
				desc[2] = align(desc[2], size3 * 2);
			}
		}

		desc[3] = velems->rsrc_word3[i];

		if (first_vb_use_mask & (1 << i)) {
			radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
					      (struct r600_resource*)vb->buffer,
					      RADEON_USAGE_READ, RADEON_PRIO_VERTEX_BUFFER);
		}
	}

	/* Don't flush the const cache. It would have a very negative effect
	 * on performance (confirmed by testing). New descriptors are always
	 * uploaded to a fresh new buffer, so I don't think flushing the const
	 * cache is needed. */
	si_mark_atom_dirty(sctx, &sctx->shader_userdata.atom);
	sctx->vertex_buffers_dirty = false;
	sctx->vertex_buffer_pointer_dirty = true;
	return true;
}


/* CONSTANT BUFFERS */

static unsigned
si_const_buffer_descriptors_idx(unsigned shader)
{
	return SI_DESCS_FIRST_SHADER + shader * SI_NUM_SHADER_DESCS +
	       SI_SHADER_DESCS_CONST_BUFFERS;
}

static struct si_descriptors *
si_const_buffer_descriptors(struct si_context *sctx, unsigned shader)
{
	return &sctx->descriptors[si_const_buffer_descriptors_idx(shader)];
}

void si_upload_const_buffer(struct si_context *sctx, struct r600_resource **rbuffer,
			    const uint8_t *ptr, unsigned size, uint32_t *const_offset)
{
	void *tmp;

	u_upload_alloc(sctx->b.uploader, 0, size, 256, const_offset,
		       (struct pipe_resource**)rbuffer, &tmp);
	if (*rbuffer)
		util_memcpy_cpu_to_le32(tmp, ptr, size);
}

static void si_set_constant_buffer(struct si_context *sctx,
				   struct si_buffer_resources *buffers,
				   unsigned descriptors_idx,
				   uint slot, const struct pipe_constant_buffer *input)
{
	struct si_descriptors *descs = &sctx->descriptors[descriptors_idx];
	assert(slot < descs->num_elements);
	pipe_resource_reference(&buffers->buffers[slot], NULL);

	/* CIK cannot unbind a constant buffer (S_BUFFER_LOAD is buggy
	 * with a NULL buffer). We need to use a dummy buffer instead. */
	if (sctx->b.chip_class == CIK &&
	    (!input || (!input->buffer && !input->user_buffer)))
		input = &sctx->null_const_buf;

	if (input && (input->buffer || input->user_buffer)) {
		struct pipe_resource *buffer = NULL;
		uint64_t va;

		/* Upload the user buffer if needed. */
		if (input->user_buffer) {
			unsigned buffer_offset;

			si_upload_const_buffer(sctx,
					       (struct r600_resource**)&buffer, input->user_buffer,
					       input->buffer_size, &buffer_offset);
			if (!buffer) {
				/* Just unbind on failure. */
				si_set_constant_buffer(sctx, buffers, descriptors_idx, slot, NULL);
				return;
			}
			va = r600_resource(buffer)->gpu_address + buffer_offset;
		} else {
			pipe_resource_reference(&buffer, input->buffer);
			va = r600_resource(buffer)->gpu_address + input->buffer_offset;
			/* Only track usage for non-user buffers. */
			r600_resource(buffer)->bind_history |= PIPE_BIND_CONSTANT_BUFFER;
		}

		/* Set the descriptor. */
		uint32_t *desc = descs->list + slot*4;
		desc[0] = va;
		desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) |
			  S_008F04_STRIDE(0);
		desc[2] = input->buffer_size;
		desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			  S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
			  S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);

		buffers->buffers[slot] = buffer;
		radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
						    (struct r600_resource*)buffer,
						    buffers->shader_usage,
						    buffers->priority, true);
		buffers->enabled_mask |= 1u << slot;
	} else {
		/* Clear the descriptor. */
		memset(descs->list + slot*4, 0, sizeof(uint32_t) * 4);
		buffers->enabled_mask &= ~(1u << slot);
	}

	descs->dirty_mask |= 1u << slot;
	sctx->descriptors_dirty |= 1u << descriptors_idx;
}

void si_set_rw_buffer(struct si_context *sctx,
		      uint slot, const struct pipe_constant_buffer *input)
{
	si_set_constant_buffer(sctx, &sctx->rw_buffers,
			                        SI_DESCS_RW_BUFFERS, slot, input);
}

static void si_pipe_set_constant_buffer(struct pipe_context *ctx,
					uint shader, uint slot,
					const struct pipe_constant_buffer *input)
{
	struct si_context *sctx = (struct si_context *)ctx;

	if (shader >= SI_NUM_SHADERS)
		return;

	si_set_constant_buffer(sctx, &sctx->const_buffers[shader],
			       si_const_buffer_descriptors_idx(shader),
			       slot, input);
}

void si_get_pipe_constant_buffer(struct si_context *sctx, uint shader,
				 uint slot, struct pipe_constant_buffer *cbuf)
{
	cbuf->user_buffer = NULL;
	si_get_buffer_from_descriptors(
		&sctx->const_buffers[shader],
		si_const_buffer_descriptors(sctx, shader),
		slot, &cbuf->buffer, &cbuf->buffer_offset, &cbuf->buffer_size);
}

/* SHADER BUFFERS */

static unsigned
si_shader_buffer_descriptors_idx(enum pipe_shader_type shader)
{
	return SI_DESCS_FIRST_SHADER + shader * SI_NUM_SHADER_DESCS +
	       SI_SHADER_DESCS_SHADER_BUFFERS;
}

static struct si_descriptors *
si_shader_buffer_descriptors(struct si_context *sctx,
				  enum pipe_shader_type shader)
{
	return &sctx->descriptors[si_shader_buffer_descriptors_idx(shader)];
}

static void si_set_shader_buffers(struct pipe_context *ctx,
				  enum pipe_shader_type shader,
				  unsigned start_slot, unsigned count,
				  const struct pipe_shader_buffer *sbuffers)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_buffer_resources *buffers = &sctx->shader_buffers[shader];
	struct si_descriptors *descs = si_shader_buffer_descriptors(sctx, shader);
	unsigned i;

	assert(start_slot + count <= SI_NUM_SHADER_BUFFERS);

	for (i = 0; i < count; ++i) {
		const struct pipe_shader_buffer *sbuffer = sbuffers ? &sbuffers[i] : NULL;
		struct r600_resource *buf;
		unsigned slot = start_slot + i;
		uint32_t *desc = descs->list + slot * 4;
		uint64_t va;

		if (!sbuffer || !sbuffer->buffer) {
			pipe_resource_reference(&buffers->buffers[slot], NULL);
			memset(desc, 0, sizeof(uint32_t) * 4);
			buffers->enabled_mask &= ~(1u << slot);
			descs->dirty_mask |= 1u << slot;
			sctx->descriptors_dirty |=
				1u << si_shader_buffer_descriptors_idx(shader);
			continue;
		}

		buf = (struct r600_resource *)sbuffer->buffer;
		va = buf->gpu_address + sbuffer->buffer_offset;

		desc[0] = va;
		desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) |
			  S_008F04_STRIDE(0);
		desc[2] = sbuffer->buffer_size;
		desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			  S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
			  S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);

		pipe_resource_reference(&buffers->buffers[slot], &buf->b.b);
		radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx, buf,
						    buffers->shader_usage,
						    buffers->priority, true);
		buf->bind_history |= PIPE_BIND_SHADER_BUFFER;

		buffers->enabled_mask |= 1u << slot;
		descs->dirty_mask |= 1u << slot;
		sctx->descriptors_dirty |=
			1u << si_shader_buffer_descriptors_idx(shader);

		util_range_add(&buf->valid_buffer_range, sbuffer->buffer_offset,
			       sbuffer->buffer_offset + sbuffer->buffer_size);
	}
}

void si_get_shader_buffers(struct si_context *sctx, uint shader,
			   uint start_slot, uint count,
			   struct pipe_shader_buffer *sbuf)
{
	struct si_buffer_resources *buffers = &sctx->shader_buffers[shader];
	struct si_descriptors *descs = si_shader_buffer_descriptors(sctx, shader);

	for (unsigned i = 0; i < count; ++i) {
		si_get_buffer_from_descriptors(
			buffers, descs, start_slot + i,
			&sbuf[i].buffer, &sbuf[i].buffer_offset,
			&sbuf[i].buffer_size);
	}
}

/* RING BUFFERS */

void si_set_ring_buffer(struct pipe_context *ctx, uint slot,
			struct pipe_resource *buffer,
			unsigned stride, unsigned num_records,
			bool add_tid, bool swizzle,
			unsigned element_size, unsigned index_stride, uint64_t offset)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_buffer_resources *buffers = &sctx->rw_buffers;
	struct si_descriptors *descs = &sctx->descriptors[SI_DESCS_RW_BUFFERS];

	/* The stride field in the resource descriptor has 14 bits */
	assert(stride < (1 << 14));

	assert(slot < descs->num_elements);
	pipe_resource_reference(&buffers->buffers[slot], NULL);

	if (buffer) {
		uint64_t va;

		va = r600_resource(buffer)->gpu_address + offset;

		switch (element_size) {
		default:
			assert(!"Unsupported ring buffer element size");
		case 0:
		case 2:
			element_size = 0;
			break;
		case 4:
			element_size = 1;
			break;
		case 8:
			element_size = 2;
			break;
		case 16:
			element_size = 3;
			break;
		}

		switch (index_stride) {
		default:
			assert(!"Unsupported ring buffer index stride");
		case 0:
		case 8:
			index_stride = 0;
			break;
		case 16:
			index_stride = 1;
			break;
		case 32:
			index_stride = 2;
			break;
		case 64:
			index_stride = 3;
			break;
		}

		if (sctx->b.chip_class >= VI && stride)
			num_records *= stride;

		/* Set the descriptor. */
		uint32_t *desc = descs->list + slot*4;
		desc[0] = va;
		desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) |
			  S_008F04_STRIDE(stride) |
			  S_008F04_SWIZZLE_ENABLE(swizzle);
		desc[2] = num_records;
		desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
			  S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
			  S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
			  S_008F0C_ELEMENT_SIZE(element_size) |
			  S_008F0C_INDEX_STRIDE(index_stride) |
			  S_008F0C_ADD_TID_ENABLE(add_tid);

		pipe_resource_reference(&buffers->buffers[slot], buffer);
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
				      (struct r600_resource*)buffer,
				      buffers->shader_usage, buffers->priority);
		buffers->enabled_mask |= 1u << slot;
	} else {
		/* Clear the descriptor. */
		memset(descs->list + slot*4, 0, sizeof(uint32_t) * 4);
		buffers->enabled_mask &= ~(1u << slot);
	}

	descs->dirty_mask |= 1u << slot;
	sctx->descriptors_dirty |= 1u << SI_DESCS_RW_BUFFERS;
}

/* STREAMOUT BUFFERS */

static void si_set_streamout_targets(struct pipe_context *ctx,
				     unsigned num_targets,
				     struct pipe_stream_output_target **targets,
				     const unsigned *offsets)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_buffer_resources *buffers = &sctx->rw_buffers;
	struct si_descriptors *descs = &sctx->descriptors[SI_DESCS_RW_BUFFERS];
	unsigned old_num_targets = sctx->b.streamout.num_targets;
	unsigned i, bufidx;

	/* We are going to unbind the buffers. Mark which caches need to be flushed. */
	if (sctx->b.streamout.num_targets && sctx->b.streamout.begin_emitted) {
		/* Since streamout uses vector writes which go through TC L2
		 * and most other clients can use TC L2 as well, we don't need
		 * to flush it.
		 *
		 * The only cases which requires flushing it is VGT DMA index
		 * fetching (on <= CIK) and indirect draw data, which are rare
		 * cases. Thus, flag the TC L2 dirtiness in the resource and
		 * handle it at draw call time.
		 */
		for (i = 0; i < sctx->b.streamout.num_targets; i++)
			if (sctx->b.streamout.targets[i])
				r600_resource(sctx->b.streamout.targets[i]->b.buffer)->TC_L2_dirty = true;

		/* Invalidate the scalar cache in case a streamout buffer is
		 * going to be used as a constant buffer.
		 *
		 * Invalidate TC L1, because streamout bypasses it (done by
		 * setting GLC=1 in the store instruction), but it can contain
		 * outdated data of streamout buffers.
		 *
		 * VS_PARTIAL_FLUSH is required if the buffers are going to be
		 * used as an input immediately.
		 */
		sctx->b.flags |= SI_CONTEXT_INV_SMEM_L1 |
				 SI_CONTEXT_INV_VMEM_L1 |
				 SI_CONTEXT_VS_PARTIAL_FLUSH;
	}

	/* All readers of the streamout targets need to be finished before we can
	 * start writing to the targets.
	 */
	if (num_targets)
		sctx->b.flags |= SI_CONTEXT_PS_PARTIAL_FLUSH |
		                 SI_CONTEXT_CS_PARTIAL_FLUSH;

	/* Streamout buffers must be bound in 2 places:
	 * 1) in VGT by setting the VGT_STRMOUT registers
	 * 2) as shader resources
	 */

	/* Set the VGT regs. */
	r600_set_streamout_targets(ctx, num_targets, targets, offsets);

	/* Set the shader resources.*/
	for (i = 0; i < num_targets; i++) {
		bufidx = SI_VS_STREAMOUT_BUF0 + i;

		if (targets[i]) {
			struct pipe_resource *buffer = targets[i]->buffer;
			uint64_t va = r600_resource(buffer)->gpu_address;

			/* Set the descriptor.
			 *
			 * On VI, the format must be non-INVALID, otherwise
			 * the buffer will be considered not bound and store
			 * instructions will be no-ops.
			 */
			uint32_t *desc = descs->list + bufidx*4;
			desc[0] = va;
			desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
			desc[2] = 0xffffffff;
			desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
				  S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
				  S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
				  S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
				  S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);

			/* Set the resource. */
			pipe_resource_reference(&buffers->buffers[bufidx],
						buffer);
			radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
							    (struct r600_resource*)buffer,
							    buffers->shader_usage,
							    RADEON_PRIO_SHADER_RW_BUFFER,
							    true);
			r600_resource(buffer)->bind_history |= PIPE_BIND_STREAM_OUTPUT;

			buffers->enabled_mask |= 1u << bufidx;
		} else {
			/* Clear the descriptor and unset the resource. */
			memset(descs->list + bufidx*4, 0,
			       sizeof(uint32_t) * 4);
			pipe_resource_reference(&buffers->buffers[bufidx],
						NULL);
			buffers->enabled_mask &= ~(1u << bufidx);
		}
		descs->dirty_mask |= 1u << bufidx;
	}
	for (; i < old_num_targets; i++) {
		bufidx = SI_VS_STREAMOUT_BUF0 + i;
		/* Clear the descriptor and unset the resource. */
		memset(descs->list + bufidx*4, 0, sizeof(uint32_t) * 4);
		pipe_resource_reference(&buffers->buffers[bufidx], NULL);
		buffers->enabled_mask &= ~(1u << bufidx);
		descs->dirty_mask |= 1u << bufidx;
	}

	sctx->descriptors_dirty |= 1u << SI_DESCS_RW_BUFFERS;
}

static void si_desc_reset_buffer_offset(struct pipe_context *ctx,
					uint32_t *desc, uint64_t old_buf_va,
					struct pipe_resource *new_buf)
{
	/* Retrieve the buffer offset from the descriptor. */
	uint64_t old_desc_va =
		desc[0] | ((uint64_t)G_008F04_BASE_ADDRESS_HI(desc[1]) << 32);

	assert(old_buf_va <= old_desc_va);
	uint64_t offset_within_buffer = old_desc_va - old_buf_va;

	/* Update the descriptor. */
	si_set_buf_desc_address(r600_resource(new_buf), offset_within_buffer,
				desc);
}

/* INTERNAL CONST BUFFERS */

static void si_set_polygon_stipple(struct pipe_context *ctx,
				   const struct pipe_poly_stipple *state)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct pipe_constant_buffer cb = {};
	unsigned stipple[32];
	int i;

	for (i = 0; i < 32; i++)
		stipple[i] = util_bitreverse(state->stipple[i]);

	cb.user_buffer = stipple;
	cb.buffer_size = sizeof(stipple);

	si_set_rw_buffer(sctx, SI_PS_CONST_POLY_STIPPLE, &cb);
}

/* TEXTURE METADATA ENABLE/DISABLE */

/* CMASK can be enabled (for fast clear) and disabled (for texture export)
 * while the texture is bound, possibly by a different context. In that case,
 * call this function to update compressed_colortex_masks.
 */
void si_update_compressed_colortex_masks(struct si_context *sctx)
{
	for (int i = 0; i < SI_NUM_SHADERS; ++i) {
		si_samplers_update_compressed_colortex_mask(&sctx->samplers[i]);
		si_images_update_compressed_colortex_mask(&sctx->images[i]);
		si_update_compressed_tex_shader_mask(sctx, i);
	}
}

/* BUFFER DISCARD/INVALIDATION */

/** Reset descriptors of buffer resources after \p buf has been invalidated. */
static void si_reset_buffer_resources(struct si_context *sctx,
				      struct si_buffer_resources *buffers,
				      unsigned descriptors_idx,
				      struct pipe_resource *buf,
				      uint64_t old_va)
{
	struct si_descriptors *descs = &sctx->descriptors[descriptors_idx];
	unsigned mask = buffers->enabled_mask;

	while (mask) {
		unsigned i = u_bit_scan(&mask);
		if (buffers->buffers[i] == buf) {
			si_desc_reset_buffer_offset(&sctx->b.b,
						    descs->list + i*4,
						    old_va, buf);
			descs->dirty_mask |= 1u << i;
			sctx->descriptors_dirty |= 1u << descriptors_idx;

			radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
							    (struct r600_resource *)buf,
							    buffers->shader_usage,
							    buffers->priority, true);
		}
	}
}

/* Reallocate a buffer a update all resource bindings where the buffer is
 * bound.
 *
 * This is used to avoid CPU-GPU synchronizations, because it makes the buffer
 * idle by discarding its contents. Apps usually tell us when to do this using
 * map_buffer flags, for example.
 */
static void si_invalidate_buffer(struct pipe_context *ctx, struct pipe_resource *buf)
{
	struct si_context *sctx = (struct si_context*)ctx;
	struct r600_resource *rbuffer = r600_resource(buf);
	unsigned i, shader;
	uint64_t old_va = rbuffer->gpu_address;
	unsigned num_elems = sctx->vertex_elements ?
				       sctx->vertex_elements->count : 0;

	/* Reallocate the buffer in the same pipe_resource. */
	r600_alloc_resource(&sctx->screen->b, rbuffer);

	/* We changed the buffer, now we need to bind it where the old one
	 * was bound. This consists of 2 things:
	 *   1) Updating the resource descriptor and dirtying it.
	 *   2) Adding a relocation to the CS, so that it's usable.
	 */

	/* Vertex buffers. */
	if (rbuffer->bind_history & PIPE_BIND_VERTEX_BUFFER) {
		for (i = 0; i < num_elems; i++) {
			int vb = sctx->vertex_elements->elements[i].vertex_buffer_index;

			if (vb >= ARRAY_SIZE(sctx->vertex_buffer))
				continue;
			if (!sctx->vertex_buffer[vb].buffer)
				continue;

			if (sctx->vertex_buffer[vb].buffer == buf) {
				sctx->vertex_buffers_dirty = true;
				break;
			}
		}
	}

	/* Streamout buffers. (other internal buffers can't be invalidated) */
	if (rbuffer->bind_history & PIPE_BIND_STREAM_OUTPUT) {
		for (i = SI_VS_STREAMOUT_BUF0; i <= SI_VS_STREAMOUT_BUF3; i++) {
			struct si_buffer_resources *buffers = &sctx->rw_buffers;
			struct si_descriptors *descs =
				&sctx->descriptors[SI_DESCS_RW_BUFFERS];

			if (buffers->buffers[i] != buf)
				continue;

			si_desc_reset_buffer_offset(ctx, descs->list + i*4,
						    old_va, buf);
			descs->dirty_mask |= 1u << i;
			sctx->descriptors_dirty |= 1u << SI_DESCS_RW_BUFFERS;

			radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
							    rbuffer, buffers->shader_usage,
							    RADEON_PRIO_SHADER_RW_BUFFER,
							    true);

			/* Update the streamout state. */
			if (sctx->b.streamout.begin_emitted)
				r600_emit_streamout_end(&sctx->b);
			sctx->b.streamout.append_bitmask =
					sctx->b.streamout.enabled_mask;
			r600_streamout_buffers_dirty(&sctx->b);
		}
	}

	/* Constant and shader buffers. */
	if (rbuffer->bind_history & PIPE_BIND_CONSTANT_BUFFER) {
		for (shader = 0; shader < SI_NUM_SHADERS; shader++)
			si_reset_buffer_resources(sctx, &sctx->const_buffers[shader],
						  si_const_buffer_descriptors_idx(shader),
						  buf, old_va);
	}

	if (rbuffer->bind_history & PIPE_BIND_SHADER_BUFFER) {
		for (shader = 0; shader < SI_NUM_SHADERS; shader++)
			si_reset_buffer_resources(sctx, &sctx->shader_buffers[shader],
						  si_shader_buffer_descriptors_idx(shader),
						  buf, old_va);
	}

	if (rbuffer->bind_history & PIPE_BIND_SAMPLER_VIEW) {
		/* Texture buffers - update bindings. */
		for (shader = 0; shader < SI_NUM_SHADERS; shader++) {
			struct si_sampler_views *views = &sctx->samplers[shader].views;
			struct si_descriptors *descs =
				si_sampler_descriptors(sctx, shader);
			unsigned mask = views->enabled_mask;

			while (mask) {
				unsigned i = u_bit_scan(&mask);
				if (views->views[i]->texture == buf) {
					si_desc_reset_buffer_offset(ctx,
								    descs->list +
								    i * 16 + 4,
								    old_va, buf);
					descs->dirty_mask |= 1u << i;
					sctx->descriptors_dirty |=
						1u << si_sampler_descriptors_idx(shader);

					radeon_add_to_buffer_list_check_mem(&sctx->b, &sctx->b.gfx,
									    rbuffer, RADEON_USAGE_READ,
									    RADEON_PRIO_SAMPLER_BUFFER,
									    true);
				}
			}
		}
	}

	/* Shader images */
	if (rbuffer->bind_history & PIPE_BIND_SHADER_IMAGE) {
		for (shader = 0; shader < SI_NUM_SHADERS; ++shader) {
			struct si_images_info *images = &sctx->images[shader];
			struct si_descriptors *descs =
				si_image_descriptors(sctx, shader);
			unsigned mask = images->enabled_mask;

			while (mask) {
				unsigned i = u_bit_scan(&mask);

				if (images->views[i].resource == buf) {
					if (images->views[i].access & PIPE_IMAGE_ACCESS_WRITE)
						si_mark_image_range_valid(&images->views[i]);

					si_desc_reset_buffer_offset(
						ctx, descs->list + i * 8 + 4,
						old_va, buf);
					descs->dirty_mask |= 1u << i;
					sctx->descriptors_dirty |=
						1u << si_image_descriptors_idx(shader);

					radeon_add_to_buffer_list_check_mem(
						&sctx->b, &sctx->b.gfx, rbuffer,
						RADEON_USAGE_READWRITE,
						RADEON_PRIO_SAMPLER_BUFFER, true);
				}
			}
		}
	}
}

/* Update mutable image descriptor fields of all bound textures. */
void si_update_all_texture_descriptors(struct si_context *sctx)
{
	unsigned shader;

	for (shader = 0; shader < SI_NUM_SHADERS; shader++) {
		struct si_sampler_views *samplers = &sctx->samplers[shader].views;
		struct si_images_info *images = &sctx->images[shader];
		unsigned mask;

		/* Images. */
		mask = images->enabled_mask;
		while (mask) {
			unsigned i = u_bit_scan(&mask);
			struct pipe_image_view *view = &images->views[i];

			if (!view->resource ||
			    view->resource->target == PIPE_BUFFER)
				continue;

			si_set_shader_image(sctx, shader, i, view, true);
		}

		/* Sampler views. */
		mask = samplers->enabled_mask;
		while (mask) {
			unsigned i = u_bit_scan(&mask);
			struct pipe_sampler_view *view = samplers->views[i];

			if (!view ||
			    !view->texture ||
			    view->texture->target == PIPE_BUFFER)
				continue;

			si_set_sampler_view(sctx, shader, i,
					    samplers->views[i], true);
		}

		si_update_compressed_tex_shader_mask(sctx, shader);
	}
}

/* SHADER USER DATA */

static void si_mark_shader_pointers_dirty(struct si_context *sctx,
					  unsigned shader)
{
	sctx->shader_pointers_dirty |=
		u_bit_consecutive(SI_DESCS_FIRST_SHADER + shader * SI_NUM_SHADER_DESCS,
				  SI_NUM_SHADER_DESCS);

	if (shader == PIPE_SHADER_VERTEX)
		sctx->vertex_buffer_pointer_dirty = sctx->vertex_buffers.buffer != NULL;

	si_mark_atom_dirty(sctx, &sctx->shader_userdata.atom);
}

static void si_shader_userdata_begin_new_cs(struct si_context *sctx)
{
	sctx->shader_pointers_dirty = u_bit_consecutive(0, SI_NUM_DESCS);
	sctx->vertex_buffer_pointer_dirty = sctx->vertex_buffers.buffer != NULL;
	si_mark_atom_dirty(sctx, &sctx->shader_userdata.atom);
}

/* Set a base register address for user data constants in the given shader.
 * This assigns a mapping from PIPE_SHADER_* to SPI_SHADER_USER_DATA_*.
 */
static void si_set_user_data_base(struct si_context *sctx,
				  unsigned shader, uint32_t new_base)
{
	uint32_t *base = &sctx->shader_userdata.sh_base[shader];

	if (*base != new_base) {
		*base = new_base;

		if (new_base)
			si_mark_shader_pointers_dirty(sctx, shader);
	}
}

/* This must be called when these shaders are changed from non-NULL to NULL
 * and vice versa:
 * - geometry shader
 * - tessellation control shader
 * - tessellation evaluation shader
 */
void si_shader_change_notify(struct si_context *sctx)
{
	/* VS can be bound as VS, ES, or LS. */
	if (sctx->tes_shader.cso)
		si_set_user_data_base(sctx, PIPE_SHADER_VERTEX,
				      R_00B530_SPI_SHADER_USER_DATA_LS_0);
	else if (sctx->gs_shader.cso)
		si_set_user_data_base(sctx, PIPE_SHADER_VERTEX,
				      R_00B330_SPI_SHADER_USER_DATA_ES_0);
	else
		si_set_user_data_base(sctx, PIPE_SHADER_VERTEX,
				      R_00B130_SPI_SHADER_USER_DATA_VS_0);

	/* TES can be bound as ES, VS, or not bound. */
	if (sctx->tes_shader.cso) {
		if (sctx->gs_shader.cso)
			si_set_user_data_base(sctx, PIPE_SHADER_TESS_EVAL,
					      R_00B330_SPI_SHADER_USER_DATA_ES_0);
		else
			si_set_user_data_base(sctx, PIPE_SHADER_TESS_EVAL,
					      R_00B130_SPI_SHADER_USER_DATA_VS_0);
	} else {
		si_set_user_data_base(sctx, PIPE_SHADER_TESS_EVAL, 0);
	}
}

static void si_emit_shader_pointer(struct si_context *sctx,
				   struct si_descriptors *desc,
				   unsigned sh_base)
{
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
	uint64_t va;

	assert(desc->buffer);

	va = desc->buffer->gpu_address +
	     desc->buffer_offset;

	radeon_emit(cs, PKT3(PKT3_SET_SH_REG, 2, 0));
	radeon_emit(cs, (sh_base + desc->shader_userdata_offset - SI_SH_REG_OFFSET) >> 2);
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);
}

void si_emit_graphics_shader_userdata(struct si_context *sctx,
                                      struct r600_atom *atom)
{
	unsigned mask;
	uint32_t *sh_base = sctx->shader_userdata.sh_base;
	struct si_descriptors *descs;

	descs = &sctx->descriptors[SI_DESCS_RW_BUFFERS];

	if (sctx->shader_pointers_dirty & (1 << SI_DESCS_RW_BUFFERS)) {
		si_emit_shader_pointer(sctx, descs,
				       R_00B030_SPI_SHADER_USER_DATA_PS_0);
		si_emit_shader_pointer(sctx, descs,
				       R_00B130_SPI_SHADER_USER_DATA_VS_0);
		si_emit_shader_pointer(sctx, descs,
				       R_00B230_SPI_SHADER_USER_DATA_GS_0);
		si_emit_shader_pointer(sctx, descs,
				       R_00B330_SPI_SHADER_USER_DATA_ES_0);
		si_emit_shader_pointer(sctx, descs,
				       R_00B430_SPI_SHADER_USER_DATA_HS_0);
	}

	mask = sctx->shader_pointers_dirty &
	       u_bit_consecutive(SI_DESCS_FIRST_SHADER,
				 SI_DESCS_FIRST_COMPUTE - SI_DESCS_FIRST_SHADER);

	while (mask) {
		unsigned i = u_bit_scan(&mask);
		unsigned shader = (i - SI_DESCS_FIRST_SHADER) / SI_NUM_SHADER_DESCS;
		unsigned base = sh_base[shader];

		if (base)
			si_emit_shader_pointer(sctx, descs + i, base);
	}
	sctx->shader_pointers_dirty &=
		~u_bit_consecutive(SI_DESCS_RW_BUFFERS, SI_DESCS_FIRST_COMPUTE);

	if (sctx->vertex_buffer_pointer_dirty) {
		si_emit_shader_pointer(sctx, &sctx->vertex_buffers,
				       sh_base[PIPE_SHADER_VERTEX]);
		sctx->vertex_buffer_pointer_dirty = false;
	}
}

void si_emit_compute_shader_userdata(struct si_context *sctx)
{
	unsigned base = R_00B900_COMPUTE_USER_DATA_0;
	struct si_descriptors *descs = sctx->descriptors;
	unsigned compute_mask =
		u_bit_consecutive(SI_DESCS_FIRST_COMPUTE, SI_NUM_SHADER_DESCS);
	unsigned mask = sctx->shader_pointers_dirty & compute_mask;

	while (mask) {
		unsigned i = u_bit_scan(&mask);

		si_emit_shader_pointer(sctx, descs + i, base);
	}
	sctx->shader_pointers_dirty &= ~compute_mask;
}

/* INIT/DEINIT/UPLOAD */

void si_init_all_descriptors(struct si_context *sctx)
{
	int i;
	unsigned ce_offset = 0;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		si_init_buffer_resources(&sctx->const_buffers[i],
					 si_const_buffer_descriptors(sctx, i),
					 SI_NUM_CONST_BUFFERS, SI_SGPR_CONST_BUFFERS,
					 RADEON_USAGE_READ, RADEON_PRIO_CONST_BUFFER,
					 &ce_offset);
		si_init_buffer_resources(&sctx->shader_buffers[i],
					 si_shader_buffer_descriptors(sctx, i),
					 SI_NUM_SHADER_BUFFERS, SI_SGPR_SHADER_BUFFERS,
					 RADEON_USAGE_READWRITE, RADEON_PRIO_SHADER_RW_BUFFER,
					 &ce_offset);

		si_init_descriptors(si_sampler_descriptors(sctx, i),
				    SI_SGPR_SAMPLERS, 16, SI_NUM_SAMPLERS,
				    null_texture_descriptor, &ce_offset);

		si_init_descriptors(si_image_descriptors(sctx, i),
				    SI_SGPR_IMAGES, 8, SI_NUM_IMAGES,
				    null_image_descriptor, &ce_offset);
	}

	si_init_buffer_resources(&sctx->rw_buffers,
				 &sctx->descriptors[SI_DESCS_RW_BUFFERS],
				 SI_NUM_RW_BUFFERS, SI_SGPR_RW_BUFFERS,
				 RADEON_USAGE_READWRITE, RADEON_PRIO_SHADER_RINGS,
				 &ce_offset);
	si_init_descriptors(&sctx->vertex_buffers, SI_SGPR_VERTEX_BUFFERS,
			    4, SI_NUM_VERTEX_BUFFERS, NULL, NULL);

	sctx->descriptors_dirty = u_bit_consecutive(0, SI_NUM_DESCS);

	assert(ce_offset <= 32768);

	/* Set pipe_context functions. */
	sctx->b.b.bind_sampler_states = si_bind_sampler_states;
	sctx->b.b.set_shader_images = si_set_shader_images;
	sctx->b.b.set_constant_buffer = si_pipe_set_constant_buffer;
	sctx->b.b.set_polygon_stipple = si_set_polygon_stipple;
	sctx->b.b.set_shader_buffers = si_set_shader_buffers;
	sctx->b.b.set_sampler_views = si_set_sampler_views;
	sctx->b.b.set_stream_output_targets = si_set_streamout_targets;
	sctx->b.invalidate_buffer = si_invalidate_buffer;

	/* Shader user data. */
	si_init_atom(sctx, &sctx->shader_userdata.atom, &sctx->atoms.s.shader_userdata,
		     si_emit_graphics_shader_userdata);

	/* Set default and immutable mappings. */
	si_set_user_data_base(sctx, PIPE_SHADER_VERTEX, R_00B130_SPI_SHADER_USER_DATA_VS_0);
	si_set_user_data_base(sctx, PIPE_SHADER_TESS_CTRL, R_00B430_SPI_SHADER_USER_DATA_HS_0);
	si_set_user_data_base(sctx, PIPE_SHADER_GEOMETRY, R_00B230_SPI_SHADER_USER_DATA_GS_0);
	si_set_user_data_base(sctx, PIPE_SHADER_FRAGMENT, R_00B030_SPI_SHADER_USER_DATA_PS_0);
}

bool si_upload_graphics_shader_descriptors(struct si_context *sctx)
{
	const unsigned mask = u_bit_consecutive(0, SI_DESCS_FIRST_COMPUTE);
	unsigned dirty = sctx->descriptors_dirty & mask;

	/* Assume nothing will go wrong: */
	sctx->shader_pointers_dirty |= dirty;

	while (dirty) {
		unsigned i = u_bit_scan(&dirty);

		if (!si_upload_descriptors(sctx, &sctx->descriptors[i],
					   &sctx->shader_userdata.atom))
			return false;
	}

	sctx->descriptors_dirty &= ~mask;
	return true;
}

bool si_upload_compute_shader_descriptors(struct si_context *sctx)
{
	/* Does not update rw_buffers as that is not needed for compute shaders
	 * and the input buffer is using the same SGPR's anyway.
	 */
	const unsigned mask = u_bit_consecutive(SI_DESCS_FIRST_COMPUTE,
						SI_NUM_DESCS - SI_DESCS_FIRST_COMPUTE);
	unsigned dirty = sctx->descriptors_dirty & mask;

	/* Assume nothing will go wrong: */
	sctx->shader_pointers_dirty |= dirty;

	while (dirty) {
		unsigned i = u_bit_scan(&dirty);

		if (!si_upload_descriptors(sctx, &sctx->descriptors[i], NULL))
			return false;
	}

	sctx->descriptors_dirty &= ~mask;

	return true;
}

void si_release_all_descriptors(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		si_release_buffer_resources(&sctx->const_buffers[i],
					    si_const_buffer_descriptors(sctx, i));
		si_release_buffer_resources(&sctx->shader_buffers[i],
					    si_shader_buffer_descriptors(sctx, i));
		si_release_sampler_views(&sctx->samplers[i].views);
		si_release_image_views(&sctx->images[i]);
	}
	si_release_buffer_resources(&sctx->rw_buffers,
				    &sctx->descriptors[SI_DESCS_RW_BUFFERS]);

	for (i = 0; i < SI_NUM_DESCS; ++i)
		si_release_descriptors(&sctx->descriptors[i]);
	si_release_descriptors(&sctx->vertex_buffers);
}

void si_all_descriptors_begin_new_cs(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		si_buffer_resources_begin_new_cs(sctx, &sctx->const_buffers[i]);
		si_buffer_resources_begin_new_cs(sctx, &sctx->shader_buffers[i]);
		si_sampler_views_begin_new_cs(sctx, &sctx->samplers[i].views);
		si_image_views_begin_new_cs(sctx, &sctx->images[i]);
	}
	si_buffer_resources_begin_new_cs(sctx, &sctx->rw_buffers);
	si_vertex_buffers_begin_new_cs(sctx);

	for (i = 0; i < SI_NUM_DESCS; ++i)
		si_descriptors_begin_new_cs(sctx, &sctx->descriptors[i]);

	si_shader_userdata_begin_new_cs(sctx);
}
