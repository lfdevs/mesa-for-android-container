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
 *
 * Authors: Marek Olšák <maraeo@gmail.com>
 *
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

#define ATI_VENDOR_ID 0x1002

#define R600_RESOURCE_FLAG_TRANSFER		(PIPE_RESOURCE_FLAG_DRV_PRIV << 0)
#define R600_RESOURCE_FLAG_FLUSHED_DEPTH	(PIPE_RESOURCE_FLAG_DRV_PRIV << 1)
#define R600_RESOURCE_FLAG_FORCE_TILING		(PIPE_RESOURCE_FLAG_DRV_PRIV << 2)
#define R600_RESOURCE_FLAG_DISABLE_DCC		(PIPE_RESOURCE_FLAG_DRV_PRIV << 3)
#define R600_RESOURCE_FLAG_UNMAPPABLE		(PIPE_RESOURCE_FLAG_DRV_PRIV << 4)

#define R600_CONTEXT_STREAMOUT_FLUSH		(1u << 0)
/* Pipeline & streamout query controls. */
#define R600_CONTEXT_START_PIPELINE_STATS	(1u << 1)
#define R600_CONTEXT_STOP_PIPELINE_STATS	(1u << 2)
#define R600_CONTEXT_FLUSH_FOR_RENDER_COND	(1u << 3)
#define R600_CONTEXT_PRIVATE_FLAG		(1u << 4)

/* special primitive types */
#define R600_PRIM_RECTANGLE_LIST	PIPE_PRIM_MAX

#define R600_NOT_QUERY		0xffffffff

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
	DBG_NO_DISCARD_RANGE,
	DBG_NO_WC,
	DBG_CHECK_VM,

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

enum r600_coherency {
	R600_COHERENCY_NONE, /* no cache flushes needed */
	R600_COHERENCY_SHADER,
	R600_COHERENCY_CB_META,
};

#ifdef PIPE_ARCH_BIG_ENDIAN
#define R600_BIG_ENDIAN 1
#else
#define R600_BIG_ENDIAN 0
#endif

struct r600_common_context;
struct r600_perfcounters;
struct tgsi_shader_info;
struct r600_qbo_state;

void si_radeon_shader_binary_init(struct ac_shader_binary *b);
void si_radeon_shader_binary_clean(struct ac_shader_binary *b);

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

	uint64_t			size;
	unsigned			num_level0_transfers;
	enum pipe_format		db_render_format;
	bool				is_depth;
	bool				db_compatible;
	bool				can_sample_z;
	bool				can_sample_s;
	unsigned			dirty_level_mask; /* each bit says if that mipmap is compressed */
	unsigned			stencil_dirty_level_mask; /* each bit says if that mipmap is compressed */
	struct r600_texture		*flushed_depth_texture;
	struct radeon_surf		surface;

	/* Colorbuffer compression and fast clear. */
	struct r600_fmask_info		fmask;
	struct r600_cmask_info		cmask;
	struct r600_resource		*cmask_buffer;
	uint64_t			dcc_offset; /* 0 = disabled */
	unsigned			cb_color_info; /* fast clear enable bit */
	unsigned			color_clear_value[2];
	unsigned			last_msaa_resolve_target_micro_mode;

	/* Depth buffer compression and fast clear. */
	uint64_t			htile_offset;
	bool				tc_compatible_htile;
	bool				depth_cleared; /* if it was cleared at least once */
	float				depth_clear_value;
	bool				stencil_cleared; /* if it was cleared at least once */
	uint8_t				stencil_clear_value;
	bool				upgraded_depth; /* upgraded from unorm to Z32_FLOAT */

	bool				non_disp_tiling; /* R600-Cayman only */

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
	/* We need to track DCC dirtiness, because st/dri usually calls
	 * flush_resource twice per frame (not a bug) and we don't wanna
	 * decompress DCC twice. Also, the dirty tracking must be done even
	 * if DCC isn't used, because it's required by the DCC usage analysis
	 * for a possible future enablement.
	 */
	bool				separate_dcc_dirty;
	/* Statistics gathering for the DCC enablement heuristic. */
	bool				dcc_gather_statistics;
	/* Estimate of how much this color buffer is written to in units of
	 * full-screen draws: ps_invocations / (width * height)
	 * Shader kills, late Z, and blending with trivial discards make it
	 * inaccurate (we need to count CB updates, not PS invocations).
	 */
	unsigned			ps_draw_ratio;
	/* The number of clears since the last DCC usage analysis. */
	unsigned			num_slow_clears;

	/* Counter that should be non-zero if the texture is bound to a
	 * framebuffer. Implemented in radeonsi only.
	 */
	uint32_t			framebuffers_bound;
};

struct r600_surface {
	struct pipe_surface		base;

	/* These can vary with block-compressed textures. */
	unsigned width0;
	unsigned height0;

	bool color_initialized;
	bool depth_initialized;

	/* Misc. color flags. */
	bool alphatest_bypass;
	bool export_16bpc;
	bool color_is_int8;
	bool color_is_int10;
	bool dcc_incompatible;

	/* Color registers. */
	unsigned cb_color_info;
	unsigned cb_color_base;
	unsigned cb_color_view;
	unsigned cb_color_size;		/* R600 only */
	unsigned cb_color_dim;		/* EG only */
	unsigned cb_color_pitch;	/* EG and later */
	unsigned cb_color_slice;	/* EG and later */
	unsigned cb_color_attrib;	/* EG and later */
	unsigned cb_color_attrib2;	/* GFX9 and later */
	unsigned cb_dcc_control;	/* VI and later */
	unsigned cb_color_fmask;	/* CB_COLORn_FMASK (EG and later) or CB_COLORn_FRAG (r600) */
	unsigned cb_color_fmask_slice;	/* EG and later */
	unsigned cb_color_cmask;	/* CB_COLORn_TILE (r600 only) */
	unsigned cb_color_mask;		/* R600 only */
	unsigned spi_shader_col_format;		/* SI+, no blending, no alpha-to-coverage. */
	unsigned spi_shader_col_format_alpha;	/* SI+, alpha-to-coverage */
	unsigned spi_shader_col_format_blend;	/* SI+, blending without alpha. */
	unsigned spi_shader_col_format_blend_alpha; /* SI+, blending with alpha. */
	struct r600_resource *cb_buffer_fmask; /* Used for FMASK relocations. R600 only */
	struct r600_resource *cb_buffer_cmask; /* Used for CMASK relocations. R600 only */

	/* DB registers. */
	uint64_t db_depth_base;		/* DB_Z_READ/WRITE_BASE (EG and later) or DB_DEPTH_BASE (r600) */
	uint64_t db_stencil_base;	/* EG and later */
	uint64_t db_htile_data_base;
	unsigned db_depth_info;		/* R600 only, then SI and later */
	unsigned db_z_info;		/* EG and later */
	unsigned db_z_info2;		/* GFX9+ */
	unsigned db_depth_view;
	unsigned db_depth_size;
	unsigned db_depth_slice;	/* EG and later */
	unsigned db_stencil_info;	/* EG and later */
	unsigned db_stencil_info2;	/* GFX9+ */
	unsigned db_prefetch_limit;	/* R600 only */
	unsigned db_htile_surface;
	unsigned db_preload_control;	/* EG and later */
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

struct r600_common_screen {
	struct pipe_screen		b;
	struct radeon_winsys		*ws;
	enum radeon_family		family;
	enum chip_class			chip_class;
	struct radeon_info		info;
	uint64_t			debug_flags;
	bool				has_cp_dma;
	bool				has_streamout;
	bool				has_rbplus;     /* if RB+ registers exist */
	bool				rbplus_allowed; /* if RB+ is allowed */

	struct disk_cache		*disk_shader_cache;

	struct slab_parent_pool		pool_transfers;

	/* Texture filter settings. */
	int				force_aniso; /* -1 = disabled */

	/* Auxiliary context. Mainly used to initialize resources.
	 * It must be locked prior to using and flushed before unlocking. */
	struct pipe_context		*aux_context;
	mtx_t				aux_context_lock;

	/* This must be in the screen, because UE4 uses one context for
	 * compilation and another one for rendering.
	 */
	unsigned			num_compilations;
	/* Along with ST_DEBUG=precompile, this should show if applications
	 * are loading shaders on demand. This is a monotonic counter.
	 */
	unsigned			num_shaders_created;
	unsigned			num_shader_cache_hits;

	/* GPU load thread. */
	mtx_t				gpu_load_mutex;
	thrd_t				gpu_load_thread;
	union r600_mmio_counters	mmio_counters;
	volatile unsigned		gpu_load_stop_thread; /* bool */

	char				renderer_string[100];

	/* Performance counters. */
	struct r600_perfcounters	*perfcounters;

	/* If pipe_screen wants to recompute and re-emit the framebuffer,
	 * sampler, and image states of all contexts, it should atomically
	 * increment this.
	 *
	 * Each context will compare this with its own last known value of
	 * the counter before drawing and re-emit the states accordingly.
	 */
	unsigned			dirty_tex_counter;

	/* Atomically increment this counter when an existing texture's
	 * metadata is enabled or disabled in a way that requires changing
	 * contexts' compressed texture binding masks.
	 */
	unsigned			compressed_colortex_counter;

	struct {
		/* Context flags to set so that all writes from earlier jobs
		 * in the CP are seen by L2 clients.
		 */
		unsigned cp_to_L2;

		/* Context flags to set so that all writes from earlier jobs
		 * that end in L2 are seen by CP.
		 */
		unsigned L2_to_cp;

		/* Context flags to set so that all writes from earlier
		 * compute jobs are seen by L2 clients.
		 */
		unsigned compute_to_L2;
	} barrier_flags;

	void (*query_opaque_metadata)(struct r600_common_screen *rscreen,
				      struct r600_texture *rtex,
				      struct radeon_bo_metadata *md);

	void (*apply_opaque_metadata)(struct r600_common_screen *rscreen,
				    struct r600_texture *rtex,
				    struct radeon_bo_metadata *md);
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

	struct r600_common_screen	*screen;
	struct radeon_winsys		*ws;
	struct radeon_winsys_ctx	*ctx;
	enum radeon_family		family;
	enum chip_class			chip_class;
	struct r600_ring		gfx;
	struct r600_ring		dma;
	struct pipe_fence_handle	*last_gfx_fence;
	struct pipe_fence_handle	*last_sdma_fence;
	struct r600_resource		*eop_bug_scratch;
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

	struct pipe_debug_callback	debug;
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

	void (*clear_buffer)(struct pipe_context *ctx, struct pipe_resource *dst,
			     uint64_t offset, uint64_t size, unsigned value,
			     enum r600_coherency coher);

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
void si_buffer_subdata(struct pipe_context *ctx,
		       struct pipe_resource *buffer,
		       unsigned usage, unsigned offset,
		       unsigned size, const void *data);
void si_init_resource_fields(struct r600_common_screen *rscreen,
			     struct r600_resource *res,
			     uint64_t size, unsigned alignment);
bool si_alloc_resource(struct r600_common_screen *rscreen,
		       struct r600_resource *res);
struct pipe_resource *si_buffer_create(struct pipe_screen *screen,
				       const struct pipe_resource *templ,
				       unsigned alignment);
struct pipe_resource *si_aligned_buffer_create(struct pipe_screen *screen,
					       unsigned flags,
					       unsigned usage,
					       unsigned size,
					       unsigned alignment);
struct pipe_resource *
si_buffer_from_user_memory(struct pipe_screen *screen,
			   const struct pipe_resource *templ,
			   void *user_memory);
void si_invalidate_resource(struct pipe_context *ctx,
			    struct pipe_resource *resource);
void si_replace_buffer_storage(struct pipe_context *ctx,
			       struct pipe_resource *dst,
			       struct pipe_resource *src);

/* r600_common_pipe.c */
void si_gfx_write_event_eop(struct r600_common_context *ctx,
			    unsigned event, unsigned event_flags,
			    unsigned data_sel,
			    struct r600_resource *buf, uint64_t va,
			    uint32_t new_fence, unsigned query_type);
unsigned si_gfx_write_fence_dwords(struct r600_common_screen *screen);
void si_gfx_wait_fence(struct r600_common_context *ctx,
		       uint64_t va, uint32_t ref, uint32_t mask);
bool si_common_screen_init(struct r600_common_screen *rscreen,
			   struct radeon_winsys *ws);
void si_destroy_common_screen(struct r600_common_screen *rscreen);
void si_preflush_suspend_features(struct r600_common_context *ctx);
void si_postflush_resume_features(struct r600_common_context *ctx);
bool si_common_context_init(struct r600_common_context *rctx,
			    struct r600_common_screen *rscreen,
			    unsigned context_flags);
void si_common_context_cleanup(struct r600_common_context *rctx);
bool si_can_dump_shader(struct r600_common_screen *rscreen,
			unsigned processor);
bool si_extra_shader_checks(struct r600_common_screen *rscreen,
			    unsigned processor);
void si_screen_clear_buffer(struct r600_common_screen *rscreen, struct pipe_resource *dst,
			    uint64_t offset, uint64_t size, unsigned value);
struct pipe_resource *si_resource_create_common(struct pipe_screen *screen,
						const struct pipe_resource *templ);
const char *si_get_llvm_processor_name(enum radeon_family family);
void si_need_dma_space(struct r600_common_context *ctx, unsigned num_dw,
		       struct r600_resource *dst, struct r600_resource *src);
void si_save_cs(struct radeon_winsys *ws, struct radeon_winsys_cs *cs,
		struct radeon_saved_cs *saved, bool get_buffer_list);
void si_clear_saved_cs(struct radeon_saved_cs *saved);
bool si_check_device_reset(struct r600_common_context *rctx);

/* r600_gpu_load.c */
void si_gpu_load_kill_thread(struct r600_common_screen *rscreen);
uint64_t si_begin_counter(struct r600_common_screen *rscreen, unsigned type);
unsigned si_end_counter(struct r600_common_screen *rscreen, unsigned type,
			uint64_t begin);

/* r600_perfcounters.c */
void si_perfcounters_destroy(struct r600_common_screen *rscreen);

/* r600_query.c */
void si_init_screen_query_functions(struct r600_common_screen *rscreen);
void si_init_query_functions(struct r600_common_context *rctx);
void si_suspend_queries(struct r600_common_context *ctx);
void si_resume_queries(struct r600_common_context *ctx);

/* r600_test_dma.c */
void si_test_dma(struct r600_common_screen *rscreen);

/* r600_texture.c */
bool si_prepare_for_dma_blit(struct r600_common_context *rctx,
			     struct r600_texture *rdst,
			     unsigned dst_level, unsigned dstx,
			     unsigned dsty, unsigned dstz,
			     struct r600_texture *rsrc,
			     unsigned src_level,
			     const struct pipe_box *src_box);
void si_texture_get_fmask_info(struct r600_common_screen *rscreen,
			       struct r600_texture *rtex,
			       unsigned nr_samples,
			       struct r600_fmask_info *out);
bool si_init_flushed_depth_texture(struct pipe_context *ctx,
				   struct pipe_resource *texture,
				   struct r600_texture **staging);
void si_print_texture_info(struct r600_common_screen *rscreen,
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
void vi_separate_dcc_start_query(struct pipe_context *ctx,
				 struct r600_texture *tex);
void vi_separate_dcc_stop_query(struct pipe_context *ctx,
				struct r600_texture *tex);
void vi_separate_dcc_process_and_reset_stats(struct pipe_context *ctx,
					     struct r600_texture *tex);
void vi_dcc_clear_level(struct r600_common_context *rctx,
			struct r600_texture *rtex,
			unsigned level, unsigned clear_value);
void si_do_fast_color_clear(struct r600_common_context *rctx,
			    struct pipe_framebuffer_state *fb,
			    struct r600_atom *fb_state,
			    unsigned *buffers, ubyte *dirty_cbufs,
			    const union pipe_color_union *color);
bool si_texture_disable_dcc(struct r600_common_context *rctx,
			    struct r600_texture *rtex);
void si_init_screen_texture_functions(struct r600_common_screen *rscreen);
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

static inline void
r600_context_add_resource_size(struct pipe_context *ctx, struct pipe_resource *r)
{
	struct r600_common_context *rctx = (struct r600_common_context *)ctx;
	struct r600_resource *res = (struct r600_resource *)r;

	if (res) {
		/* Add memory usage for need_gfx_cs_space */
		rctx->vram += res->vram_usage;
		rctx->gtt += res->gart_usage;
	}
}

#define     SQ_TEX_XY_FILTER_POINT                         0x00
#define     SQ_TEX_XY_FILTER_BILINEAR                      0x01
#define     SQ_TEX_XY_FILTER_ANISO_POINT                   0x02
#define     SQ_TEX_XY_FILTER_ANISO_BILINEAR                0x03

static inline unsigned eg_tex_filter(unsigned filter, unsigned max_aniso)
{
	if (filter == PIPE_TEX_FILTER_LINEAR)
		return max_aniso > 1 ? SQ_TEX_XY_FILTER_ANISO_BILINEAR
				     : SQ_TEX_XY_FILTER_BILINEAR;
	else
		return max_aniso > 1 ? SQ_TEX_XY_FILTER_ANISO_POINT
				     : SQ_TEX_XY_FILTER_POINT;
}

static inline unsigned r600_tex_aniso_filter(unsigned filter)
{
	if (filter < 2)
		return 0;
	if (filter < 4)
		return 1;
	if (filter < 8)
		return 2;
	if (filter < 16)
		return 3;
	return 4;
}

static inline enum radeon_bo_priority
r600_get_sampler_view_priority(struct r600_resource *res)
{
	if (res->b.b.target == PIPE_BUFFER)
		return RADEON_PRIO_SAMPLER_BUFFER;

	if (res->b.b.nr_samples > 1)
		return RADEON_PRIO_SAMPLER_TEXTURE_MSAA;

	return RADEON_PRIO_SAMPLER_TEXTURE;
}

static inline bool
r600_can_sample_zs(struct r600_texture *tex, bool stencil_sampler)
{
	return (stencil_sampler && tex->can_sample_s) ||
	       (!stencil_sampler && tex->can_sample_z);
}

static inline bool
vi_dcc_enabled(struct r600_texture *tex, unsigned level)
{
	return tex->dcc_offset && level < tex->surface.num_dcc_levels;
}

static inline bool
r600_htile_enabled(struct r600_texture *tex, unsigned level)
{
	return tex->htile_offset && level == 0;
}

static inline bool
vi_tc_compat_htile_enabled(struct r600_texture *tex, unsigned level)
{
	assert(!tex->tc_compatible_htile || tex->htile_offset);
	return tex->tc_compatible_htile && level == 0;
}

#define COMPUTE_DBG(rscreen, fmt, args...) \
	do { \
		if ((rscreen->b.debug_flags & DBG(COMPUTE))) fprintf(stderr, fmt, ##args); \
	} while (0);

#define R600_ERR(fmt, args...) \
	fprintf(stderr, "EE %s:%d %s - " fmt, __FILE__, __LINE__, __func__, ##args)

static inline int S_FIXED(float value, unsigned frac_bits)
{
	return value * (1 << frac_bits);
}

#endif
