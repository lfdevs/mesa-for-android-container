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
#include "si_shader.h"
#include "sid.h"

#include "util/u_memory.h"
#include "util/u_upload_mgr.h"


/* NULL image and buffer descriptor.
 *
 * For images, all fields must be zero except for the swizzle, which
 * supports arbitrary combinations of 0s and 1s. The texture type must be
 * any valid type (e.g. 1D). If the texture type isn't set, the hw hangs.
 *
 * For buffers, all fields must be zero. If they are not, the hw hangs.
 *
 * This is the only reason why the buffer descriptor must be in words [4:7].
 */
static uint32_t null_descriptor[8] = {
	0,
	0,
	0,
	S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_1) |
	S_008F1C_TYPE(V_008F1C_SQ_RSRC_IMG_1D)
	/* the rest must contain zeros, which is also used by the buffer
	 * descriptor */
};

static void si_init_descriptors(struct si_descriptors *desc,
				unsigned shader_userdata_index,
				unsigned element_dw_size,
				unsigned num_elements)
{
	int i;

	assert(num_elements <= sizeof(desc->enabled_mask)*8);

	desc->list = CALLOC(num_elements, element_dw_size * 4);
	desc->element_dw_size = element_dw_size;
	desc->num_elements = num_elements;
	desc->list_dirty = true; /* upload the list before the next draw */
	desc->shader_userdata_offset = shader_userdata_index * 4;

	/* Initialize the array to NULL descriptors if the element size is 8. */
	if (element_dw_size % 8 == 0)
		for (i = 0; i < num_elements * element_dw_size / 8; i++)
			memcpy(desc->list + i*8, null_descriptor,
			       sizeof(null_descriptor));
}

static void si_release_descriptors(struct si_descriptors *desc)
{
	pipe_resource_reference((struct pipe_resource**)&desc->buffer, NULL);
	FREE(desc->list);
}

static bool si_upload_descriptors(struct si_context *sctx,
				  struct si_descriptors *desc)
{
	unsigned list_size = desc->num_elements * desc->element_dw_size * 4;
	void *ptr;

	if (!desc->list_dirty)
		return true;

	u_upload_alloc(sctx->b.uploader, 0, list_size, 256,
		       &desc->buffer_offset,
		       (struct pipe_resource**)&desc->buffer, &ptr);
	if (!desc->buffer)
		return false; /* skip the draw call */

	util_memcpy_cpu_to_le32(ptr, desc->list, list_size);

	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, desc->buffer,
			      RADEON_USAGE_READ, RADEON_PRIO_DESCRIPTORS);

	desc->list_dirty = false;
	desc->pointer_dirty = true;
	si_mark_atom_dirty(sctx, &sctx->shader_userdata.atom);
	return true;
}

/* SAMPLER VIEWS */

static void si_release_sampler_views(struct si_sampler_views *views)
{
	int i;

	for (i = 0; i < Elements(views->views); i++) {
		pipe_sampler_view_reference(&views->views[i], NULL);
	}
	si_release_descriptors(&views->desc);
}

static void si_sampler_view_add_buffers(struct si_context *sctx,
					struct si_sampler_view *rview)
{
	if (rview->resource) {
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
			rview->resource, RADEON_USAGE_READ,
			r600_get_sampler_view_priority(rview->resource));
	}

	if (rview->dcc_buffer && rview->dcc_buffer != rview->resource) {
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
			rview->dcc_buffer, RADEON_USAGE_READ,
			RADEON_PRIO_DCC);
	}
}

static void si_sampler_views_begin_new_cs(struct si_context *sctx,
					  struct si_sampler_views *views)
{
	uint64_t mask = views->desc.enabled_mask;

	/* Add buffers to the CS. */
	while (mask) {
		int i = u_bit_scan64(&mask);
		struct si_sampler_view *rview =
			(struct si_sampler_view*)views->views[i];

		si_sampler_view_add_buffers(sctx, rview);
	}

	if (!views->desc.buffer)
		return;
	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx, views->desc.buffer,
			      RADEON_USAGE_READWRITE, RADEON_PRIO_DESCRIPTORS);
}

static void si_set_sampler_view(struct si_context *sctx,
				struct si_sampler_views *views,
				unsigned slot, struct pipe_sampler_view *view)
{
	if (views->views[slot] == view)
		return;

	if (view) {
		struct si_sampler_view *rview =
			(struct si_sampler_view*)view;
		struct r600_texture *rtex = (struct r600_texture*)view->texture;

		si_sampler_view_add_buffers(sctx, rview);

		pipe_sampler_view_reference(&views->views[slot], view);
		memcpy(views->desc.list + slot * 16, rview->state, 8*4);

		if (view->texture && view->texture->target != PIPE_BUFFER &&
		    rtex->fmask.size) {
			memcpy(views->desc.list + slot*16 + 8,
			       rview->fmask_state, 8*4);
		} else {
			/* Disable FMASK and bind sampler state in [12:15]. */
			memcpy(views->desc.list + slot*16 + 8,
			       null_descriptor, 4*4);

			if (views->sampler_states[slot])
				memcpy(views->desc.list + slot*16 + 12,
				       views->sampler_states[slot], 4*4);
		}

		views->desc.enabled_mask |= 1llu << slot;
	} else {
		pipe_sampler_view_reference(&views->views[slot], NULL);
		memcpy(views->desc.list + slot*16, null_descriptor, 8*4);
		/* Only clear the lower dwords of FMASK. */
		memcpy(views->desc.list + slot*16 + 8, null_descriptor, 4*4);
		views->desc.enabled_mask &= ~(1llu << slot);
	}

	views->desc.list_dirty = true;
}

static void si_set_sampler_views(struct pipe_context *ctx,
				 unsigned shader, unsigned start,
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
			samplers->depth_texture_mask &= ~(1 << slot);
			samplers->compressed_colortex_mask &= ~(1 << slot);
			si_set_sampler_view(sctx, &samplers->views, slot, NULL);
			continue;
		}

		si_set_sampler_view(sctx, &samplers->views, slot, views[i]);

		if (views[i]->texture && views[i]->texture->target != PIPE_BUFFER) {
			struct r600_texture *rtex =
				(struct r600_texture*)views[i]->texture;

			if (rtex->is_depth && !rtex->is_flushing_texture) {
				samplers->depth_texture_mask |= 1 << slot;
			} else {
				samplers->depth_texture_mask &= ~(1 << slot);
			}
			if (rtex->cmask.size || rtex->fmask.size ||
			    (rtex->dcc_buffer && rtex->dirty_level_mask)) {
				samplers->compressed_colortex_mask |= 1 << slot;
			} else {
				samplers->compressed_colortex_mask &= ~(1 << slot);
			}
		} else {
			samplers->depth_texture_mask &= ~(1 << slot);
			samplers->compressed_colortex_mask &= ~(1 << slot);
		}
	}
}

/* SAMPLER STATES */

static void si_bind_sampler_states(struct pipe_context *ctx, unsigned shader,
                                   unsigned start, unsigned count, void **states)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_textures_info *samplers = &sctx->samplers[shader];
	struct si_descriptors *desc = &samplers->views.desc;
	struct si_sampler_state **sstates = (struct si_sampler_state**)states;
	int i;

	if (!count || shader >= SI_NUM_SHADERS)
		return;

	for (i = 0; i < count; i++) {
		unsigned slot = start + i;

		if (!sstates[i] ||
		    sstates[i] == samplers->views.sampler_states[slot])
			continue;

		samplers->views.sampler_states[slot] = sstates[i];

		/* If FMASK is bound, don't overwrite it.
		 * The sampler state will be set after FMASK is unbound.
		 */
		if (samplers->views.views[i] &&
		    samplers->views.views[i]->texture &&
		    samplers->views.views[i]->texture->target != PIPE_BUFFER &&
		    ((struct r600_texture*)samplers->views.views[i]->texture)->fmask.size)
			continue;

		memcpy(desc->list + slot * 16 + 12, sstates[i]->val, 4*4);
		desc->list_dirty = true;
	}
}

/* BUFFER RESOURCES */

static void si_init_buffer_resources(struct si_buffer_resources *buffers,
				     unsigned num_buffers,
				     unsigned shader_userdata_index,
				     enum radeon_bo_usage shader_usage,
				     enum radeon_bo_priority priority)
{
	buffers->shader_usage = shader_usage;
	buffers->priority = priority;
	buffers->buffers = CALLOC(num_buffers, sizeof(struct pipe_resource*));

	si_init_descriptors(&buffers->desc, shader_userdata_index, 4,
			    num_buffers);
}

static void si_release_buffer_resources(struct si_buffer_resources *buffers)
{
	int i;

	for (i = 0; i < buffers->desc.num_elements; i++) {
		pipe_resource_reference(&buffers->buffers[i], NULL);
	}

	FREE(buffers->buffers);
	si_release_descriptors(&buffers->desc);
}

static void si_buffer_resources_begin_new_cs(struct si_context *sctx,
					     struct si_buffer_resources *buffers)
{
	uint64_t mask = buffers->desc.enabled_mask;

	/* Add buffers to the CS. */
	while (mask) {
		int i = u_bit_scan64(&mask);

		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
				      (struct r600_resource*)buffers->buffers[i],
				      buffers->shader_usage, buffers->priority);
	}

	if (!buffers->desc.buffer)
		return;
	radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
			      buffers->desc.buffer, RADEON_USAGE_READWRITE,
			      RADEON_PRIO_DESCRIPTORS);
}

/* VERTEX BUFFERS */

static void si_vertex_buffers_begin_new_cs(struct si_context *sctx)
{
	struct si_descriptors *desc = &sctx->vertex_buffers;
	int count = sctx->vertex_elements ? sctx->vertex_elements->count : 0;
	int i;

	for (i = 0; i < count; i++) {
		int vb = sctx->vertex_elements->elements[i].vertex_buffer_index;

		if (vb >= Elements(sctx->vertex_buffer))
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

static bool si_upload_vertex_buffer_descriptors(struct si_context *sctx)
{
	struct si_descriptors *desc = &sctx->vertex_buffers;
	bool bound[SI_NUM_VERTEX_BUFFERS] = {};
	unsigned i, count = sctx->vertex_elements->count;
	uint64_t va;
	uint32_t *ptr;

	if (!sctx->vertex_buffers_dirty)
		return true;
	if (!count || !sctx->vertex_elements)
		return true;

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
		struct pipe_vertex_element *ve = &sctx->vertex_elements->elements[i];
		struct pipe_vertex_buffer *vb;
		struct r600_resource *rbuffer;
		unsigned offset;
		uint32_t *desc = &ptr[i*4];

		if (ve->vertex_buffer_index >= Elements(sctx->vertex_buffer)) {
			memset(desc, 0, 16);
			continue;
		}

		vb = &sctx->vertex_buffer[ve->vertex_buffer_index];
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

		if (sctx->b.chip_class <= CIK && vb->stride)
			/* Round up by rounding down and adding 1 */
			desc[2] = (vb->buffer->width0 - offset -
				   sctx->vertex_elements->format_size[i]) /
				  vb->stride + 1;
		else
			desc[2] = vb->buffer->width0 - offset;

		desc[3] = sctx->vertex_elements->rsrc_word3[i];

		if (!bound[ve->vertex_buffer_index]) {
			radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
					      (struct r600_resource*)vb->buffer,
					      RADEON_USAGE_READ, RADEON_PRIO_VERTEX_BUFFER);
			bound[ve->vertex_buffer_index] = true;
		}
	}

	/* Don't flush the const cache. It would have a very negative effect
	 * on performance (confirmed by testing). New descriptors are always
	 * uploaded to a fresh new buffer, so I don't think flushing the const
	 * cache is needed. */
	desc->pointer_dirty = true;
	si_mark_atom_dirty(sctx, &sctx->shader_userdata.atom);
	sctx->vertex_buffers_dirty = false;
	return true;
}


/* CONSTANT BUFFERS */

void si_upload_const_buffer(struct si_context *sctx, struct r600_resource **rbuffer,
			    const uint8_t *ptr, unsigned size, uint32_t *const_offset)
{
	void *tmp;

	u_upload_alloc(sctx->b.uploader, 0, size, 256, const_offset,
		       (struct pipe_resource**)rbuffer, &tmp);
	if (*rbuffer)
		util_memcpy_cpu_to_le32(tmp, ptr, size);
}

static void si_set_constant_buffer(struct pipe_context *ctx, uint shader, uint slot,
				   struct pipe_constant_buffer *input)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_buffer_resources *buffers = &sctx->const_buffers[shader];

	if (shader >= SI_NUM_SHADERS)
		return;

	assert(slot < buffers->desc.num_elements);
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
				si_set_constant_buffer(ctx, shader, slot, NULL);
				return;
			}
			va = r600_resource(buffer)->gpu_address + buffer_offset;
		} else {
			pipe_resource_reference(&buffer, input->buffer);
			va = r600_resource(buffer)->gpu_address + input->buffer_offset;
		}

		/* Set the descriptor. */
		uint32_t *desc = buffers->desc.list + slot*4;
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
		radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
				      (struct r600_resource*)buffer,
				      buffers->shader_usage, buffers->priority);
		buffers->desc.enabled_mask |= 1llu << slot;
	} else {
		/* Clear the descriptor. */
		memset(buffers->desc.list + slot*4, 0, sizeof(uint32_t) * 4);
		buffers->desc.enabled_mask &= ~(1llu << slot);
	}

	buffers->desc.list_dirty = true;
}

/* RING BUFFERS */

void si_set_ring_buffer(struct pipe_context *ctx, uint shader, uint slot,
			struct pipe_resource *buffer,
			unsigned stride, unsigned num_records,
			bool add_tid, bool swizzle,
			unsigned element_size, unsigned index_stride, uint64_t offset)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_buffer_resources *buffers = &sctx->rw_buffers[shader];

	if (shader >= SI_NUM_SHADERS)
		return;

	/* The stride field in the resource descriptor has 14 bits */
	assert(stride < (1 << 14));

	assert(slot < buffers->desc.num_elements);
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
		uint32_t *desc = buffers->desc.list + slot*4;
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
		buffers->desc.enabled_mask |= 1llu << slot;
	} else {
		/* Clear the descriptor. */
		memset(buffers->desc.list + slot*4, 0, sizeof(uint32_t) * 4);
		buffers->desc.enabled_mask &= ~(1llu << slot);
	}

	buffers->desc.list_dirty = true;
}

/* STREAMOUT BUFFERS */

static void si_set_streamout_targets(struct pipe_context *ctx,
				     unsigned num_targets,
				     struct pipe_stream_output_target **targets,
				     const unsigned *offsets)
{
	struct si_context *sctx = (struct si_context *)ctx;
	struct si_buffer_resources *buffers = &sctx->rw_buffers[PIPE_SHADER_VERTEX];
	unsigned old_num_targets = sctx->b.streamout.num_targets;
	unsigned i, bufidx;

	/* We are going to unbind the buffers. Mark which caches need to be flushed. */
	if (sctx->b.streamout.num_targets && sctx->b.streamout.begin_emitted) {
		/* Since streamout uses vector writes which go through TC L2
		 * and most other clients can use TC L2 as well, we don't need
		 * to flush it.
		 *
		 * The only case which requires flushing it is VGT DMA index
		 * fetching, which is a rare case. Thus, flag the TC L2
		 * dirtiness in the resource and handle it when index fetching
		 * is used.
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

	/* Streamout buffers must be bound in 2 places:
	 * 1) in VGT by setting the VGT_STRMOUT registers
	 * 2) as shader resources
	 */

	/* Set the VGT regs. */
	r600_set_streamout_targets(ctx, num_targets, targets, offsets);

	/* Set the shader resources.*/
	for (i = 0; i < num_targets; i++) {
		bufidx = SI_SO_BUF_OFFSET + i;

		if (targets[i]) {
			struct pipe_resource *buffer = targets[i]->buffer;
			uint64_t va = r600_resource(buffer)->gpu_address;

			/* Set the descriptor.
			 *
			 * On VI, the format must be non-INVALID, otherwise
			 * the buffer will be considered not bound and store
			 * instructions will be no-ops.
			 */
			uint32_t *desc = buffers->desc.list + bufidx*4;
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
			radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
					      (struct r600_resource*)buffer,
					      buffers->shader_usage, buffers->priority);
			buffers->desc.enabled_mask |= 1llu << bufidx;
		} else {
			/* Clear the descriptor and unset the resource. */
			memset(buffers->desc.list + bufidx*4, 0,
			       sizeof(uint32_t) * 4);
			pipe_resource_reference(&buffers->buffers[bufidx],
						NULL);
			buffers->desc.enabled_mask &= ~(1llu << bufidx);
		}
	}
	for (; i < old_num_targets; i++) {
		bufidx = SI_SO_BUF_OFFSET + i;
		/* Clear the descriptor and unset the resource. */
		memset(buffers->desc.list + bufidx*4, 0, sizeof(uint32_t) * 4);
		pipe_resource_reference(&buffers->buffers[bufidx], NULL);
		buffers->desc.enabled_mask &= ~(1llu << bufidx);
	}

	buffers->desc.list_dirty = true;
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
	uint64_t va = r600_resource(new_buf)->gpu_address + offset_within_buffer;

	desc[0] = va;
	desc[1] = (desc[1] & C_008F04_BASE_ADDRESS_HI) |
		  S_008F04_BASE_ADDRESS_HI(va >> 32);
}

/* BUFFER DISCARD/INVALIDATION */

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
	unsigned i, shader, alignment = rbuffer->buf->alignment;
	uint64_t old_va = rbuffer->gpu_address;
	unsigned num_elems = sctx->vertex_elements ?
				       sctx->vertex_elements->count : 0;
	struct si_sampler_view *view;

	/* Reallocate the buffer in the same pipe_resource. */
	r600_init_resource(&sctx->screen->b, rbuffer, rbuffer->b.b.width0,
			   alignment, TRUE);

	/* We changed the buffer, now we need to bind it where the old one
	 * was bound. This consists of 2 things:
	 *   1) Updating the resource descriptor and dirtying it.
	 *   2) Adding a relocation to the CS, so that it's usable.
	 */

	/* Vertex buffers. */
	for (i = 0; i < num_elems; i++) {
		int vb = sctx->vertex_elements->elements[i].vertex_buffer_index;

		if (vb >= Elements(sctx->vertex_buffer))
			continue;
		if (!sctx->vertex_buffer[vb].buffer)
			continue;

		if (sctx->vertex_buffer[vb].buffer == buf) {
			sctx->vertex_buffers_dirty = true;
			break;
		}
	}

	/* Read/Write buffers. */
	for (shader = 0; shader < SI_NUM_SHADERS; shader++) {
		struct si_buffer_resources *buffers = &sctx->rw_buffers[shader];
		uint64_t mask = buffers->desc.enabled_mask;

		while (mask) {
			i = u_bit_scan64(&mask);
			if (buffers->buffers[i] == buf) {
				si_desc_reset_buffer_offset(ctx, buffers->desc.list + i*4,
							    old_va, buf);
				buffers->desc.list_dirty = true;

				radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
						      rbuffer, buffers->shader_usage,
						      buffers->priority);

				if (i >= SI_SO_BUF_OFFSET && shader == PIPE_SHADER_VERTEX) {
					/* Update the streamout state. */
					if (sctx->b.streamout.begin_emitted) {
						r600_emit_streamout_end(&sctx->b);
					}
					sctx->b.streamout.append_bitmask =
						sctx->b.streamout.enabled_mask;
					r600_streamout_buffers_dirty(&sctx->b);
				}
			}
		}
	}

	/* Constant buffers. */
	for (shader = 0; shader < SI_NUM_SHADERS; shader++) {
		struct si_buffer_resources *buffers = &sctx->const_buffers[shader];
		uint64_t mask = buffers->desc.enabled_mask;

		while (mask) {
			unsigned i = u_bit_scan64(&mask);
			if (buffers->buffers[i] == buf) {
				si_desc_reset_buffer_offset(ctx, buffers->desc.list + i*4,
							    old_va, buf);
				buffers->desc.list_dirty = true;

				radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
						      rbuffer, buffers->shader_usage,
						      buffers->priority);
			}
		}
	}

	/* Texture buffers - update virtual addresses in sampler view descriptors. */
	LIST_FOR_EACH_ENTRY(view, &sctx->b.texture_buffers, list) {
		if (view->base.texture == buf) {
			si_desc_reset_buffer_offset(ctx, &view->state[4], old_va, buf);
		}
	}
	/* Texture buffers - update bindings. */
	for (shader = 0; shader < SI_NUM_SHADERS; shader++) {
		struct si_sampler_views *views = &sctx->samplers[shader].views;
		uint64_t mask = views->desc.enabled_mask;

		while (mask) {
			unsigned i = u_bit_scan64(&mask);
			if (views->views[i]->texture == buf) {
				si_desc_reset_buffer_offset(ctx,
							    views->desc.list +
							    i * 16 + 4,
							    old_va, buf);
				views->desc.list_dirty = true;

				radeon_add_to_buffer_list(&sctx->b, &sctx->b.gfx,
						      rbuffer, RADEON_USAGE_READ,
						      RADEON_PRIO_SAMPLER_BUFFER);
			}
		}
	}
}

/* SHADER USER DATA */

static void si_mark_shader_pointers_dirty(struct si_context *sctx,
					  unsigned shader)
{
	sctx->const_buffers[shader].desc.pointer_dirty = true;
	sctx->rw_buffers[shader].desc.pointer_dirty = true;
	sctx->samplers[shader].views.desc.pointer_dirty = true;

	if (shader == PIPE_SHADER_VERTEX)
		sctx->vertex_buffers.pointer_dirty = true;

	si_mark_atom_dirty(sctx, &sctx->shader_userdata.atom);
}

static void si_shader_userdata_begin_new_cs(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		si_mark_shader_pointers_dirty(sctx, i);
	}
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
				   unsigned sh_base, bool keep_dirty)
{
	struct radeon_winsys_cs *cs = sctx->b.gfx.cs;
	uint64_t va;

	if (!desc->pointer_dirty || !desc->buffer)
		return;

	va = desc->buffer->gpu_address +
	     desc->buffer_offset;

	radeon_emit(cs, PKT3(PKT3_SET_SH_REG, 2, 0));
	radeon_emit(cs, (sh_base + desc->shader_userdata_offset - SI_SH_REG_OFFSET) >> 2);
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);

	desc->pointer_dirty = keep_dirty;
}

void si_emit_shader_userdata(struct si_context *sctx, struct r600_atom *atom)
{
	unsigned i;
	uint32_t *sh_base = sctx->shader_userdata.sh_base;

	if (sctx->gs_shader.cso) {
		/* The VS copy shader needs these for clipping, streamout, and rings. */
		unsigned vs_base = R_00B130_SPI_SHADER_USER_DATA_VS_0;
		unsigned i = PIPE_SHADER_VERTEX;

		si_emit_shader_pointer(sctx, &sctx->const_buffers[i].desc, vs_base, true);
		si_emit_shader_pointer(sctx, &sctx->rw_buffers[i].desc, vs_base, true);

		if (sctx->tes_shader.cso) {
			/* The TESSEVAL shader needs this for the ESGS ring buffer. */
			si_emit_shader_pointer(sctx, &sctx->rw_buffers[i].desc,
					       R_00B330_SPI_SHADER_USER_DATA_ES_0, true);
		}
	} else if (sctx->tes_shader.cso) {
		/* The TESSEVAL shader needs this for streamout. */
		si_emit_shader_pointer(sctx, &sctx->rw_buffers[PIPE_SHADER_VERTEX].desc,
				       R_00B130_SPI_SHADER_USER_DATA_VS_0, true);
	}

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		unsigned base = sh_base[i];

		if (!base)
			continue;

		if (i != PIPE_SHADER_TESS_EVAL)
			si_emit_shader_pointer(sctx, &sctx->rw_buffers[i].desc, base, false);

		si_emit_shader_pointer(sctx, &sctx->const_buffers[i].desc, base, false);
		si_emit_shader_pointer(sctx, &sctx->samplers[i].views.desc, base, false);
	}
	si_emit_shader_pointer(sctx, &sctx->vertex_buffers, sh_base[PIPE_SHADER_VERTEX], false);
}

/* INIT/DEINIT/UPLOAD */

void si_init_all_descriptors(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		si_init_buffer_resources(&sctx->const_buffers[i],
					 SI_NUM_CONST_BUFFERS, SI_SGPR_CONST_BUFFERS,
					 RADEON_USAGE_READ, RADEON_PRIO_CONST_BUFFER);
		si_init_buffer_resources(&sctx->rw_buffers[i],
					 SI_NUM_RW_BUFFERS, SI_SGPR_RW_BUFFERS,
					 RADEON_USAGE_READWRITE, RADEON_PRIO_RINGS_STREAMOUT);

		si_init_descriptors(&sctx->samplers[i].views.desc,
				    SI_SGPR_SAMPLERS, 16, SI_NUM_SAMPLERS);
	}

	si_init_descriptors(&sctx->vertex_buffers, SI_SGPR_VERTEX_BUFFERS,
			    4, SI_NUM_VERTEX_BUFFERS);

	/* Set pipe_context functions. */
	sctx->b.b.bind_sampler_states = si_bind_sampler_states;
	sctx->b.b.set_constant_buffer = si_set_constant_buffer;
	sctx->b.b.set_sampler_views = si_set_sampler_views;
	sctx->b.b.set_stream_output_targets = si_set_streamout_targets;
	sctx->b.invalidate_buffer = si_invalidate_buffer;

	/* Shader user data. */
	si_init_atom(sctx, &sctx->shader_userdata.atom, &sctx->atoms.s.shader_userdata,
		     si_emit_shader_userdata);

	/* Set default and immutable mappings. */
	si_set_user_data_base(sctx, PIPE_SHADER_VERTEX, R_00B130_SPI_SHADER_USER_DATA_VS_0);
	si_set_user_data_base(sctx, PIPE_SHADER_TESS_CTRL, R_00B430_SPI_SHADER_USER_DATA_HS_0);
	si_set_user_data_base(sctx, PIPE_SHADER_GEOMETRY, R_00B230_SPI_SHADER_USER_DATA_GS_0);
	si_set_user_data_base(sctx, PIPE_SHADER_FRAGMENT, R_00B030_SPI_SHADER_USER_DATA_PS_0);
}

bool si_upload_shader_descriptors(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		if (!si_upload_descriptors(sctx, &sctx->const_buffers[i].desc) ||
		    !si_upload_descriptors(sctx, &sctx->rw_buffers[i].desc) ||
		    !si_upload_descriptors(sctx, &sctx->samplers[i].views.desc))
			return false;
	}
	return si_upload_vertex_buffer_descriptors(sctx);
}

void si_release_all_descriptors(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		si_release_buffer_resources(&sctx->const_buffers[i]);
		si_release_buffer_resources(&sctx->rw_buffers[i]);
		si_release_sampler_views(&sctx->samplers[i].views);
	}
	si_release_descriptors(&sctx->vertex_buffers);
}

void si_all_descriptors_begin_new_cs(struct si_context *sctx)
{
	int i;

	for (i = 0; i < SI_NUM_SHADERS; i++) {
		si_buffer_resources_begin_new_cs(sctx, &sctx->const_buffers[i]);
		si_buffer_resources_begin_new_cs(sctx, &sctx->rw_buffers[i]);
		si_sampler_views_begin_new_cs(sctx, &sctx->samplers[i].views);
	}
	si_vertex_buffers_begin_new_cs(sctx);
	si_shader_userdata_begin_new_cs(sctx);
}
