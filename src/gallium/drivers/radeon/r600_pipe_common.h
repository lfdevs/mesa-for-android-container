/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * This file contains common screen and context structures and functions
 * for r600g and radeonsi.
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

#define R600_RESOURCE_FLAG_TRANSFER		(PIPE_RESOURCE_FLAG_DRV_PRIV << 0)
#define R600_RESOURCE_FLAG_FLUSHED_DEPTH	(PIPE_RESOURCE_FLAG_DRV_PRIV << 1)
#define R600_RESOURCE_FLAG_FORCE_TILING		(PIPE_RESOURCE_FLAG_DRV_PRIV << 2)
#define R600_RESOURCE_FLAG_DISABLE_DCC		(PIPE_RESOURCE_FLAG_DRV_PRIV << 3)
#define R600_RESOURCE_FLAG_UNMAPPABLE		(PIPE_RESOURCE_FLAG_DRV_PRIV << 4)
#define R600_RESOURCE_FLAG_READ_ONLY		(PIPE_RESOURCE_FLAG_DRV_PRIV << 5)

/* Debug flags. */
enum {
	/* Shader logging options: */
	DBG_VS = PIPE_SHADER_VERTEX,
	DBG_PS = PIPE_SHADER_FRAGMENT,
	DBG_GS = PIPE_SHADER_GEOMETRY,
	DBG_TCS = PIPE_SHADER_TESS_CTRL,
	DBG_TES = PIPE_SHADER_TESS_EVAL,
	DBG_CS = PIPE_SHADER_COMPUTE,
	DBG_NO_IR,
	DBG_NO_TGSI,
	DBG_NO_ASM,
	DBG_PREOPT_IR,

	/* Shader compiler options the shader cache should be aware of: */
	DBG_FS_CORRECT_DERIVS_AFTER_KILL,
	DBG_UNSAFE_MATH,
	DBG_SI_SCHED,

	/* Shader compiler options (with no effect on the shader cache): */
	DBG_CHECK_IR,
	DBG_PRECOMPILE,
	DBG_NIR,
	DBG_MONOLITHIC_SHADERS,
	DBG_NO_OPT_VARIANT,

	/* Information logging options: */
	DBG_INFO,
	DBG_TEX,
	DBG_COMPUTE,
	DBG_VM,

	/* Driver options: */
	DBG_FORCE_DMA,
	DBG_NO_ASYNC_DMA,
	DBG_NO_WC,
	DBG_CHECK_VM,
	DBG_RESERVE_VMID,

	/* 3D engine options: */
	DBG_SWITCH_ON_EOP,
	DBG_NO_OUT_OF_ORDER,
	DBG_NO_DPBB,
	DBG_NO_DFSM,
	DBG_DPBB,
	DBG_DFSM,
	DBG_NO_HYPERZ,
	DBG_NO_RB_PLUS,
	DBG_NO_2D_TILING,
	DBG_NO_TILING,
	DBG_NO_DCC,
	DBG_NO_DCC_CLEAR,
	DBG_NO_DCC_FB,
	DBG_NO_DCC_MSAA,
	DBG_DCC_MSAA,

	/* Tests: */
	DBG_TEST_DMA,
	DBG_TEST_VMFAULT_CP,
	DBG_TEST_VMFAULT_SDMA,
	DBG_TEST_VMFAULT_SHADER,
};

#define DBG_ALL_SHADERS		(((1 << (DBG_CS + 1)) - 1))
#define DBG(name)		(1ull << DBG_##name)

#define R600_MAP_BUFFER_ALIGNMENT 64

#define SI_MAX_VARIABLE_THREADS_PER_BLOCK 1024

struct r600_common_context;
struct r600_perfcounters;
struct tgsi_shader_info;
struct r600_qbo_state;

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

struct r600_mmio_counter {
	unsigned busy;
	unsigned idle;
};

union r600_mmio_counters {
	struct {
		/* For global GPU load including SDMA. */
		struct r600_mmio_counter gpu;

		/* GRBM_STATUS */
		struct r600_mmio_counter spi;
		struct r600_mmio_counter gui;
		struct r600_mmio_counter ta;
		struct r600_mmio_counter gds;
		struct r600_mmio_counter vgt;
		struct r600_mmio_counter ia;
		struct r600_mmio_counter sx;
		struct r600_mmio_counter wd;
		struct r600_mmio_counter bci;
		struct r600_mmio_counter sc;
		struct r600_mmio_counter pa;
		struct r600_mmio_counter db;
		struct r600_mmio_counter cp;
		struct r600_mmio_counter cb;

		/* SRBM_STATUS2 */
		struct r600_mmio_counter sdma;

		/* CP_STAT */
		struct r600_mmio_counter pfp;
		struct r600_mmio_counter meq;
		struct r600_mmio_counter me;
		struct r600_mmio_counter surf_sync;
		struct r600_mmio_counter cp_dma;
		struct r600_mmio_counter scratch_ram;
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
	void (*emit)(struct r600_common_context *ctx, struct r600_atom *state);
	unsigned short		id;
};

struct r600_ring {
	struct radeon_winsys_cs		*cs;
	void (*flush)(void *ctx, unsigned flags,
		      struct pipe_fence_handle **fence);
};

/* Saved CS data for debugging features. */
struct radeon_saved_cs {
	uint32_t			*ib;
	unsigned			num_dw;

	struct radeon_bo_list_item	*bo_list;
	unsigned			bo_count;
};

struct r600_common_context {
	struct pipe_context b; /* base class */

	struct si_screen		*screen;
	struct radeon_winsys		*ws;
	struct radeon_winsys_ctx	*ctx;
	enum radeon_family		family;
	enum chip_class			chip_class;
	struct r600_ring		gfx;
	struct r600_ring		dma;
	struct pipe_fence_handle	*last_gfx_fence;
	struct pipe_fence_handle	*last_sdma_fence;
	struct r600_resource		*eop_bug_scratch;
	struct u_upload_mgr		*cached_gtt_allocator;
	unsigned			num_gfx_cs_flushes;
	unsigned			initial_gfx_cs_size;
	unsigned			gpu_reset_counter;
	unsigned			last_dirty_tex_counter;
	unsigned			last_compressed_colortex_counter;
	unsigned			last_num_draw_calls;

	struct threaded_context		*tc;
	struct u_suballocator		*allocator_zeroed_memory;
	struct slab_child_pool		pool_transfers;
	struct slab_child_pool		pool_transfers_unsync; /* for threaded_context */

	/* Current unaccounted memory usage. */
	uint64_t			vram;
	uint64_t			gtt;

	/* Additional context states. */
	unsigned flags; /* flush flags */

	/* Queries. */
	/* Maintain the list of active queries for pausing between IBs. */
	int				num_occlusion_queries;
	int				num_perfect_occlusion_queries;
	struct list_head		active_queries;
	unsigned			num_cs_dw_queries_suspend;
	/* Misc stats. */
	unsigned			num_draw_calls;
	unsigned			num_decompress_calls;
	unsigned			num_mrt_draw_calls;
	unsigned			num_prim_restart_calls;
	unsigned			num_spill_draw_calls;
	unsigned			num_compute_calls;
	unsigned			num_spill_compute_calls;
	unsigned			num_dma_calls;
	unsigned			num_cp_dma_calls;
	unsigned			num_vs_flushes;
	unsigned			num_ps_flushes;
	unsigned			num_cs_flushes;
	unsigned			num_cb_cache_flushes;
	unsigned			num_db_cache_flushes;
	unsigned			num_L2_invalidates;
	unsigned			num_L2_writebacks;
	unsigned			num_resident_handles;
	uint64_t			num_alloc_tex_transfer_bytes;
	unsigned			last_tex_ps_draw_ratio; /* for query */

	/* Render condition. */
	struct r600_atom		render_cond_atom;
	struct pipe_query		*render_cond;
	unsigned			render_cond_mode;
	bool				render_cond_invert;
	bool				render_cond_force_off; /* for u_blitter */

	/* Statistics gathering for the DCC enablement heuristic. It can't be
	 * in r600_texture because r600_texture can be shared by multiple
	 * contexts. This is for back buffers only. We shouldn't get too many
	 * of those.
	 *
	 * X11 DRI3 rotates among a finite set of back buffers. They should
	 * all fit in this array. If they don't, separate DCC might never be
	 * enabled by DCC stat gathering.
	 */
	struct {
		struct r600_texture		*tex;
		/* Query queue: 0 = usually active, 1 = waiting, 2 = readback. */
		struct pipe_query		*ps_stats[3];
		/* If all slots are used and another slot is needed,
		 * the least recently used slot is evicted based on this. */
		int64_t				last_use_timestamp;
		bool				query_active;
	} dcc_stats[5];

	struct pipe_device_reset_callback device_reset_callback;
	struct u_log_context		*log;

	void				*query_result_shader;

	/* Copy one resource to another using async DMA. */
	void (*dma_copy)(struct pipe_context *ctx,
			 struct pipe_resource *dst,
			 unsigned dst_level,
			 unsigned dst_x, unsigned dst_y, unsigned dst_z,
			 struct pipe_resource *src,
			 unsigned src_level,
			 const struct pipe_box *src_box);

	void (*dma_clear_buffer)(struct pipe_context *ctx, struct pipe_resource *dst,
				 uint64_t offset, uint64_t size, unsigned value);

	void (*blit_decompress_depth)(struct pipe_context *ctx,
				      struct r600_texture *texture,
				      struct r600_texture *staging,
				      unsigned first_level, unsigned last_level,
				      unsigned first_layer, unsigned last_layer,
				      unsigned first_sample, unsigned last_sample);

	void (*decompress_dcc)(struct pipe_context *ctx,
			       struct r600_texture *rtex);

	/* Reallocate the buffer and update all resource bindings where
	 * the buffer is bound, including all resource descriptors. */
	void (*invalidate_buffer)(struct pipe_context *ctx, struct pipe_resource *buf);

	/* Update all resource bindings where the buffer is bound, including
	 * all resource descriptors. This is invalidate_buffer without
	 * the invalidation. */
	void (*rebind_buffer)(struct pipe_context *ctx, struct pipe_resource *buf,
			      uint64_t old_gpu_address);

	/* Enable or disable occlusion queries. */
	void (*set_occlusion_query_state)(struct pipe_context *ctx,
					  bool old_enable,
					  bool old_perfect_enable);

	void (*save_qbo_state)(struct pipe_context *ctx, struct r600_qbo_state *st);

	/* This ensures there is enough space in the command stream. */
	void (*need_gfx_cs_space)(struct pipe_context *ctx, unsigned num_dw,
				  bool include_draw_vbo);

	void (*set_atom_dirty)(struct r600_common_context *ctx,
			       struct r600_atom *atom, bool dirty);

	void (*check_vm_faults)(struct r600_common_context *ctx,
				struct radeon_saved_cs *saved,
				enum ring_type ring);
};

/* r600_buffer_common.c */
bool si_rings_is_buffer_referenced(struct r600_common_context *ctx,
				   struct pb_buffer *buf,
				   enum radeon_bo_usage usage);
void *si_buffer_map_sync_with_rings(struct r600_common_context *ctx,
				    struct r600_resource *resource,
				    unsigned usage);
void si_init_resource_fields(struct si_screen *sscreen,
			     struct r600_resource *res,
			     uint64_t size, unsigned alignment);
bool si_alloc_resource(struct si_screen *sscreen,
		       struct r600_resource *res);
struct pipe_resource *si_aligned_buffer_create(struct pipe_screen *screen,
					       unsigned flags,
					       unsigned usage,
					       unsigned size,
					       unsigned alignment);
void si_replace_buffer_storage(struct pipe_context *ctx,
			       struct pipe_resource *dst,
			       struct pipe_resource *src);
void si_init_screen_buffer_functions(struct si_screen *sscreen);
void si_init_buffer_functions(struct si_context *sctx);

/* r600_common_pipe.c */
void si_gfx_write_event_eop(struct r600_common_context *ctx,
			    unsigned event, unsigned event_flags,
			    unsigned data_sel,
			    struct r600_resource *buf, uint64_t va,
			    uint32_t new_fence, unsigned query_type);
unsigned si_gfx_write_fence_dwords(struct si_screen *screen);
void si_gfx_wait_fence(struct r600_common_context *ctx,
		       uint64_t va, uint32_t ref, uint32_t mask);
bool si_common_context_init(struct r600_common_context *rctx,
			    struct si_screen *sscreen,
			    unsigned context_flags);
void si_common_context_cleanup(struct r600_common_context *rctx);
void si_screen_clear_buffer(struct si_screen *sscreen, struct pipe_resource *dst,
			    uint64_t offset, uint64_t size, unsigned value);
void si_need_dma_space(struct r600_common_context *ctx, unsigned num_dw,
		       struct r600_resource *dst, struct r600_resource *src);
void si_save_cs(struct radeon_winsys *ws, struct radeon_winsys_cs *cs,
		struct radeon_saved_cs *saved, bool get_buffer_list);
void si_clear_saved_cs(struct radeon_saved_cs *saved);
bool si_check_device_reset(struct r600_common_context *rctx);

/* r600_gpu_load.c */
void si_gpu_load_kill_thread(struct si_screen *sscreen);
uint64_t si_begin_counter(struct si_screen *sscreen, unsigned type);
unsigned si_end_counter(struct si_screen *sscreen, unsigned type,
			uint64_t begin);

/* r600_perfcounters.c */
void si_perfcounters_destroy(struct si_screen *sscreen);

/* r600_query.c */
void si_init_screen_query_functions(struct si_screen *sscreen);
void si_init_query_functions(struct r600_common_context *rctx);
void si_suspend_queries(struct r600_common_context *ctx);
void si_resume_queries(struct r600_common_context *ctx);

/* r600_texture.c */
bool si_prepare_for_dma_blit(struct r600_common_context *rctx,
			     struct r600_texture *rdst,
			     unsigned dst_level, unsigned dstx,
			     unsigned dsty, unsigned dstz,
			     struct r600_texture *rsrc,
			     unsigned src_level,
			     const struct pipe_box *src_box);
void si_texture_get_fmask_info(struct si_screen *sscreen,
			       struct r600_texture *rtex,
			       unsigned nr_samples,
			       struct r600_fmask_info *out);
void si_texture_get_cmask_info(struct si_screen *sscreen,
			       struct r600_texture *rtex,
			       struct r600_cmask_info *out);
bool si_init_flushed_depth_texture(struct pipe_context *ctx,
				   struct pipe_resource *texture,
				   struct r600_texture **staging);
void si_print_texture_info(struct si_screen *sscreen,
			   struct r600_texture *rtex, struct u_log_context *log);
struct pipe_resource *si_texture_create(struct pipe_screen *screen,
					const struct pipe_resource *templ);
bool vi_dcc_formats_compatible(enum pipe_format format1,
			       enum pipe_format format2);
bool vi_dcc_formats_are_incompatible(struct pipe_resource *tex,
				     unsigned level,
				     enum pipe_format view_format);
void vi_disable_dcc_if_incompatible_format(struct r600_common_context *rctx,
					   struct pipe_resource *tex,
					   unsigned level,
					   enum pipe_format view_format);
struct pipe_surface *si_create_surface_custom(struct pipe_context *pipe,
					      struct pipe_resource *texture,
					      const struct pipe_surface *templ,
					      unsigned width0, unsigned height0,
					      unsigned width, unsigned height);
unsigned si_translate_colorswap(enum pipe_format format, bool do_endian_swap);
void vi_separate_dcc_try_enable(struct r600_common_context *rctx,
				struct r600_texture *tex);
void vi_separate_dcc_start_query(struct pipe_context *ctx,
				 struct r600_texture *tex);
void vi_separate_dcc_stop_query(struct pipe_context *ctx,
				struct r600_texture *tex);
void vi_separate_dcc_process_and_reset_stats(struct pipe_context *ctx,
					     struct r600_texture *tex);
bool si_texture_disable_dcc(struct r600_common_context *rctx,
			    struct r600_texture *rtex);
void si_init_screen_texture_functions(struct si_screen *sscreen);
void si_init_context_texture_functions(struct r600_common_context *rctx);


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

#define R600_ERR(fmt, args...) \
	fprintf(stderr, "EE %s:%d %s - " fmt, __FILE__, __LINE__, __func__, ##args)

#endif
