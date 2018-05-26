/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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

/**
 * This file is going to be removed.
 */

#ifndef R600_PIPE_COMMON_H
#define R600_PIPE_COMMON_H

#include <stdio.h>

#include "amd/common/ac_binary.h"

#include "radeon/radeon_winsys.h"

#include "util/disk_cache.h"
#include "util/u_blitter.h"
#include "util/list.h"
#include "util/u_range.h"
#include "util/slab.h"
#include "util/u_suballoc.h"
#include "util/u_transfer.h"
#include "util/u_threaded_context.h"

struct u_log_context;
struct si_screen;
struct si_context;
struct si_perfcounters;
struct tgsi_shader_info;
struct si_qbo_state;

/* Only 32-bit buffer allocations are supported, gallium doesn't support more
 * at the moment.
 */
struct r600_resource {
	struct threaded_resource	b;

	/* Winsys objects. */
	struct pb_buffer		*buf;
	uint64_t			gpu_address;
	/* Memory usage if the buffer placement is optimal. */
	uint64_t			vram_usage;
	uint64_t			gart_usage;

	/* Resource properties. */
	uint64_t			bo_size;
	unsigned			bo_alignment;
	enum radeon_bo_domain		domains;
	enum radeon_bo_flag		flags;
	unsigned			bind_history;
	int				max_forced_staging_uploads;

	/* The buffer range which is initialized (with a write transfer,
	 * streamout, DMA, or as a random access target). The rest of
	 * the buffer is considered invalid and can be mapped unsynchronized.
	 *
	 * This allows unsychronized mapping of a buffer range which hasn't
	 * been used yet. It's for applications which forget to use
	 * the unsynchronized map flag and expect the driver to figure it out.
         */
	struct util_range		valid_buffer_range;

	/* For buffers only. This indicates that a write operation has been
	 * performed by TC L2, but the cache hasn't been flushed.
	 * Any hw block which doesn't use or bypasses TC L2 should check this
	 * flag and flush the cache before using the buffer.
	 *
	 * For example, TC L2 must be flushed if a buffer which has been
	 * modified by a shader store instruction is about to be used as
	 * an index buffer. The reason is that VGT DMA index fetching doesn't
	 * use TC L2.
	 */
	bool				TC_L2_dirty;

	/* Whether the resource has been exported via resource_get_handle. */
	unsigned			external_usage; /* PIPE_HANDLE_USAGE_* */

	/* Whether this resource is referenced by bindless handles. */
	bool				texture_handle_allocated;
	bool				image_handle_allocated;
};

struct r600_transfer {
	struct threaded_transfer	b;
	struct r600_resource		*staging;
	unsigned			offset;
};

struct r600_fmask_info {
	uint64_t offset;
	uint64_t size;
	unsigned alignment;
	unsigned pitch_in_pixels;
	unsigned bank_height;
	unsigned slice_tile_max;
	unsigned tile_mode_index;
	unsigned tile_swizzle;
};

struct r600_cmask_info {
	uint64_t offset;
	uint64_t size;
	unsigned alignment;
	unsigned slice_tile_max;
	uint64_t base_address_reg;
};

struct r600_texture {
	struct r600_resource		resource;

	struct radeon_surf		surface;
	uint64_t			size;
	struct r600_texture		*flushed_depth_texture;

	/* Colorbuffer compression and fast clear. */
	struct r600_fmask_info		fmask;
	struct r600_cmask_info		cmask;
	struct r600_resource		*cmask_buffer;
	uint64_t			dcc_offset; /* 0 = disabled */
	unsigned			cb_color_info; /* fast clear enable bit */
	unsigned			color_clear_value[2];
	unsigned			last_msaa_resolve_target_micro_mode;
	unsigned			num_level0_transfers;

	/* Depth buffer compression and fast clear. */
	uint64_t			htile_offset;
	float				depth_clear_value;
	uint16_t			dirty_level_mask; /* each bit says if that mipmap is compressed */
	uint16_t			stencil_dirty_level_mask; /* each bit says if that mipmap is compressed */
	enum pipe_format		db_render_format:16;
	uint8_t				stencil_clear_value;
	bool				tc_compatible_htile:1;
	bool				depth_cleared:1; /* if it was cleared at least once */
	bool				stencil_cleared:1; /* if it was cleared at least once */
	bool				upgraded_depth:1; /* upgraded from unorm to Z32_FLOAT */
	bool				is_depth:1;
	bool				db_compatible:1;
	bool				can_sample_z:1;
	bool				can_sample_s:1;

	/* We need to track DCC dirtiness, because st/dri usually calls
	 * flush_resource twice per frame (not a bug) and we don't wanna
	 * decompress DCC twice. Also, the dirty tracking must be done even
	 * if DCC isn't used, because it's required by the DCC usage analysis
	 * for a possible future enablement.
	 */
	bool				separate_dcc_dirty:1;
	/* Statistics gathering for the DCC enablement heuristic. */
	bool				dcc_gather_statistics:1;
	/* Counter that should be non-zero if the texture is bound to a
	 * framebuffer.
	 */
	unsigned                        framebuffers_bound;
	/* Whether the texture is a displayable back buffer and needs DCC
	 * decompression, which is expensive. Therefore, it's enabled only
	 * if statistics suggest that it will pay off and it's allocated
	 * separately. It can't be bound as a sampler by apps. Limited to
	 * target == 2D and last_level == 0. If enabled, dcc_offset contains
	 * the absolute GPUVM address, not the relative one.
	 */
	struct r600_resource		*dcc_separate_buffer;
	/* When DCC is temporarily disabled, the separate buffer is here. */
	struct r600_resource		*last_dcc_separate_buffer;
	/* Estimate of how much this color buffer is written to in units of
	 * full-screen draws: ps_invocations / (width * height)
	 * Shader kills, late Z, and blending with trivial discards make it
	 * inaccurate (we need to count CB updates, not PS invocations).
	 */
	unsigned			ps_draw_ratio;
	/* The number of clears since the last DCC usage analysis. */
	unsigned			num_slow_clears;
};

struct r600_surface {
	struct pipe_surface		base;

	/* These can vary with block-compressed textures. */
	uint16_t width0;
	uint16_t height0;

	bool color_initialized:1;
	bool depth_initialized:1;

	/* Misc. color flags. */
	bool color_is_int8:1;
	bool color_is_int10:1;
	bool dcc_incompatible:1;

	/* Color registers. */
	unsigned cb_color_info;
	unsigned cb_color_view;
	unsigned cb_color_attrib;
	unsigned cb_color_attrib2;	/* GFX9 and later */
	unsigned cb_dcc_control;	/* VI and later */
	unsigned spi_shader_col_format:8;	/* no blending, no alpha-to-coverage. */
	unsigned spi_shader_col_format_alpha:8;	/* alpha-to-coverage */
	unsigned spi_shader_col_format_blend:8;	/* blending without alpha. */
	unsigned spi_shader_col_format_blend_alpha:8; /* blending with alpha. */

	/* DB registers. */
	uint64_t db_depth_base;		/* DB_Z_READ/WRITE_BASE */
	uint64_t db_stencil_base;
	uint64_t db_htile_data_base;
	unsigned db_depth_info;
	unsigned db_z_info;
	unsigned db_z_info2;		/* GFX9+ */
	unsigned db_depth_view;
	unsigned db_depth_size;
	unsigned db_depth_slice;
	unsigned db_stencil_info;
	unsigned db_stencil_info2;	/* GFX9+ */
	unsigned db_htile_surface;
};

struct si_mmio_counter {
	unsigned busy;
	unsigned idle;
};

union si_mmio_counters {
	struct {
		/* For global GPU load including SDMA. */
		struct si_mmio_counter gpu;

		/* GRBM_STATUS */
		struct si_mmio_counter spi;
		struct si_mmio_counter gui;
		struct si_mmio_counter ta;
		struct si_mmio_counter gds;
		struct si_mmio_counter vgt;
		struct si_mmio_counter ia;
		struct si_mmio_counter sx;
		struct si_mmio_counter wd;
		struct si_mmio_counter bci;
		struct si_mmio_counter sc;
		struct si_mmio_counter pa;
		struct si_mmio_counter db;
		struct si_mmio_counter cp;
		struct si_mmio_counter cb;

		/* SRBM_STATUS2 */
		struct si_mmio_counter sdma;

		/* CP_STAT */
		struct si_mmio_counter pfp;
		struct si_mmio_counter meq;
		struct si_mmio_counter me;
		struct si_mmio_counter surf_sync;
		struct si_mmio_counter cp_dma;
		struct si_mmio_counter scratch_ram;
	} named;
	unsigned array[0];
};

struct r600_memory_object {
	struct pipe_memory_object	b;
	struct pb_buffer		*buf;
	uint32_t			stride;
	uint32_t			offset;
};

/* This encapsulates a state or an operation which can emitted into the GPU
 * command stream. */
struct r600_atom {
	void (*emit)(struct si_context *ctx, struct r600_atom *state);
	unsigned short		id;
};

/* Saved CS data for debugging features. */
struct radeon_saved_cs {
	uint32_t			*ib;
	unsigned			num_dw;

	struct radeon_bo_list_item	*bo_list;
	unsigned			bo_count;
};

/* r600_perfcounters.c */
void si_perfcounters_destroy(struct si_screen *sscreen);


/* Inline helpers. */

static inline struct r600_resource *r600_resource(struct pipe_resource *r)
{
	return (struct r600_resource*)r;
}

static inline void
r600_resource_reference(struct r600_resource **ptr, struct r600_resource *res)
{
	pipe_resource_reference((struct pipe_resource **)ptr,
				(struct pipe_resource *)res);
}

static inline void
r600_texture_reference(struct r600_texture **ptr, struct r600_texture *res)
{
	pipe_resource_reference((struct pipe_resource **)ptr, &res->resource.b.b);
}

static inline bool
vi_dcc_enabled(struct r600_texture *tex, unsigned level)
{
	return tex->dcc_offset && level < tex->surface.num_dcc_levels;
}

#endif
