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
#ifndef SI_PIPE_H
#define SI_PIPE_H

#include "si_shader.h"

#include "util/u_dynarray.h"
#include "util/u_idalloc.h"

#ifdef PIPE_ARCH_BIG_ENDIAN
#define SI_BIG_ENDIAN 1
#else
#define SI_BIG_ENDIAN 0
#endif

#define ATI_VENDOR_ID			0x1002

#define SI_NOT_QUERY			0xffffffff

/* The base vertex and primitive restart can be any number, but we must pick
 * one which will mean "unknown" for the purpose of state tracking and
 * the number shouldn't be a commonly-used one. */
#define SI_BASE_VERTEX_UNKNOWN		INT_MIN
#define SI_RESTART_INDEX_UNKNOWN	INT_MIN
#define SI_NUM_SMOOTH_AA_SAMPLES	8
#define SI_GS_PER_ES			128
/* Alignment for optimal CP DMA performance. */
#define SI_CPDMA_ALIGNMENT		32

/* Pipeline & streamout query controls. */
#define SI_CONTEXT_START_PIPELINE_STATS	(1 << 0)
#define SI_CONTEXT_STOP_PIPELINE_STATS	(1 << 1)
#define SI_CONTEXT_FLUSH_FOR_RENDER_COND (1 << 2)
/* Instruction cache. */
#define SI_CONTEXT_INV_ICACHE		(1 << 3)
/* SMEM L1, other names: KCACHE, constant cache, DCACHE, data cache */
#define SI_CONTEXT_INV_SMEM_L1		(1 << 4)
/* VMEM L1 can optionally be bypassed (GLC=1). Other names: TC L1 */
#define SI_CONTEXT_INV_VMEM_L1		(1 << 5)
/* Used by everything except CB/DB, can be bypassed (SLC=1). Other names: TC L2 */
#define SI_CONTEXT_INV_GLOBAL_L2	(1 << 6)
/* Write dirty L2 lines back to memory (shader and CP DMA stores), but don't
 * invalidate L2. SI-CIK can't do it, so they will do complete invalidation. */
#define SI_CONTEXT_WRITEBACK_GLOBAL_L2	(1 << 7)
/* Writeback & invalidate the L2 metadata cache. It can only be coupled with
 * a CB or DB flush. */
#define SI_CONTEXT_INV_L2_METADATA	(1 << 8)
/* Framebuffer caches. */
#define SI_CONTEXT_FLUSH_AND_INV_DB	(1 << 9)
#define SI_CONTEXT_FLUSH_AND_INV_DB_META (1 << 10)
#define SI_CONTEXT_FLUSH_AND_INV_CB	(1 << 11)
/* Engine synchronization. */
#define SI_CONTEXT_VS_PARTIAL_FLUSH	(1 << 12)
#define SI_CONTEXT_PS_PARTIAL_FLUSH	(1 << 13)
#define SI_CONTEXT_CS_PARTIAL_FLUSH	(1 << 14)
#define SI_CONTEXT_VGT_FLUSH		(1 << 15)
#define SI_CONTEXT_VGT_STREAMOUT_SYNC	(1 << 16)

#define SI_PREFETCH_VBO_DESCRIPTORS	(1 << 0)
#define SI_PREFETCH_LS			(1 << 1)
#define SI_PREFETCH_HS			(1 << 2)
#define SI_PREFETCH_ES			(1 << 3)
#define SI_PREFETCH_GS			(1 << 4)
#define SI_PREFETCH_VS			(1 << 5)
#define SI_PREFETCH_PS			(1 << 6)

#define SI_MAX_BORDER_COLORS		4096
#define SI_MAX_VIEWPORTS		16
#define SIX_BITS			0x3F
#define SI_MAP_BUFFER_ALIGNMENT		64
#define SI_MAX_VARIABLE_THREADS_PER_BLOCK 1024

#define SI_RESOURCE_FLAG_TRANSFER	(PIPE_RESOURCE_FLAG_DRV_PRIV << 0)
#define SI_RESOURCE_FLAG_FLUSHED_DEPTH	(PIPE_RESOURCE_FLAG_DRV_PRIV << 1)
#define SI_RESOURCE_FLAG_FORCE_TILING	(PIPE_RESOURCE_FLAG_DRV_PRIV << 2)
#define SI_RESOURCE_FLAG_DISABLE_DCC	(PIPE_RESOURCE_FLAG_DRV_PRIV << 3)
#define SI_RESOURCE_FLAG_UNMAPPABLE	(PIPE_RESOURCE_FLAG_DRV_PRIV << 4)
#define SI_RESOURCE_FLAG_READ_ONLY	(PIPE_RESOURCE_FLAG_DRV_PRIV << 5)
#define SI_RESOURCE_FLAG_32BIT		(PIPE_RESOURCE_FLAG_DRV_PRIV << 6)

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
	DBG_NO_FMASK,

	/* Tests: */
	DBG_TEST_DMA,
	DBG_TEST_VMFAULT_CP,
	DBG_TEST_VMFAULT_SDMA,
	DBG_TEST_VMFAULT_SHADER,
};

#define DBG_ALL_SHADERS		(((1 << (DBG_CS + 1)) - 1))
#define DBG(name)		(1ull << DBG_##name)

struct si_compute;
struct hash_table;
struct u_suballocator;

struct si_screen {
	struct pipe_screen		b;
	struct radeon_winsys		*ws;
	struct disk_cache		*disk_shader_cache;

	struct radeon_info		info;
	uint64_t			debug_flags;
	char				renderer_string[183];

	unsigned			gs_table_depth;
	unsigned			tess_offchip_block_dw_size;
	unsigned			tess_offchip_ring_size;
	unsigned			tess_factor_ring_size;
	unsigned			vgt_hs_offchip_param;
	bool				has_clear_state;
	bool				has_distributed_tess;
	bool				has_draw_indirect_multi;
	bool				has_out_of_order_rast;
	bool				assume_no_z_fights;
	bool				commutative_blend_add;
	bool				clear_db_cache_before_clear;
	bool				has_msaa_sample_loc_bug;
	bool				has_ls_vgpr_init_bug;
	bool				dpbb_allowed;
	bool				dfsm_allowed;
	bool				llvm_has_working_vgpr_indexing;

	/* Whether shaders are monolithic (1-part) or separate (3-part). */
	bool				use_monolithic_shaders;
	bool				record_llvm_ir;
	bool				has_rbplus;     /* if RB+ registers exist */
	bool				rbplus_allowed; /* if RB+ is allowed */
	bool				dcc_msaa_allowed;
	bool				cpdma_prefetch_writes_memory;

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
	union si_mmio_counters	mmio_counters;
	volatile unsigned		gpu_load_stop_thread; /* bool */

	/* Performance counters. */
	struct si_perfcounters	*perfcounters;

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
	} barrier_flags;

	mtx_t			shader_parts_mutex;
	struct si_shader_part		*vs_prologs;
	struct si_shader_part		*tcs_epilogs;
	struct si_shader_part		*gs_prologs;
	struct si_shader_part		*ps_prologs;
	struct si_shader_part		*ps_epilogs;

	/* Shader cache in memory.
	 *
	 * Design & limitations:
	 * - The shader cache is per screen (= per process), never saved to
	 *   disk, and skips redundant shader compilations from TGSI to bytecode.
	 * - It can only be used with one-variant-per-shader support, in which
	 *   case only the main (typically middle) part of shaders is cached.
	 * - Only VS, TCS, TES, PS are cached, out of which only the hw VS
	 *   variants of VS and TES are cached, so LS and ES aren't.
	 * - GS and CS aren't cached, but it's certainly possible to cache
	 *   those as well.
	 */
	mtx_t			shader_cache_mutex;
	struct hash_table		*shader_cache;

	/* Shader compiler queue for multithreaded compilation. */
	struct util_queue		shader_compiler_queue;
	/* Use at most 3 normal compiler threads on quadcore and better.
	 * Hyperthreaded CPUs report the number of threads, but we want
	 * the number of cores. */
	LLVMTargetMachineRef		tm[3]; /* used by the queue only */

	struct util_queue		shader_compiler_queue_low_priority;
	/* Use at most 2 low priority threads on quadcore and better.
	 * We want to minimize the impact on multithreaded Mesa. */
	LLVMTargetMachineRef		tm_low_priority[2]; /* at most 2 threads */
};

struct si_blend_color {
	struct r600_atom		atom;
	struct pipe_blend_color		state;
	bool				any_nonzeros;
};

struct si_sampler_view {
	struct pipe_sampler_view	base;
        /* [0..7] = image descriptor
         * [4..7] = buffer descriptor */
	uint32_t			state[8];
	uint32_t			fmask_state[8];
	const struct legacy_surf_level	*base_level_info;
	ubyte				base_level;
	ubyte				block_width;
	bool is_stencil_sampler;
	bool is_integer;
	bool dcc_incompatible;
};

#define SI_SAMPLER_STATE_MAGIC 0x34f1c35a

struct si_sampler_state {
#ifdef DEBUG
	unsigned			magic;
#endif
	uint32_t			val[4];
	uint32_t			integer_val[4];
	uint32_t			upgraded_depth_val[4];
};

struct si_cs_shader_state {
	struct si_compute		*program;
	struct si_compute		*emitted_program;
	unsigned			offset;
	bool				initialized;
	bool				uses_scratch;
};

struct si_samplers {
	struct pipe_sampler_view	*views[SI_NUM_SAMPLERS];
	struct si_sampler_state		*sampler_states[SI_NUM_SAMPLERS];

	/* The i-th bit is set if that element is enabled (non-NULL resource). */
	unsigned			enabled_mask;
	uint32_t			needs_depth_decompress_mask;
	uint32_t			needs_color_decompress_mask;
};

struct si_images {
	struct pipe_image_view		views[SI_NUM_IMAGES];
	uint32_t			needs_color_decompress_mask;
	unsigned			enabled_mask;
};

struct si_framebuffer {
	struct r600_atom		atom;
	struct pipe_framebuffer_state	state;
	unsigned			colorbuf_enabled_4bit;
	unsigned			spi_shader_col_format;
	unsigned			spi_shader_col_format_alpha;
	unsigned			spi_shader_col_format_blend;
	unsigned			spi_shader_col_format_blend_alpha;
	ubyte				nr_samples:5; /* at most 16xAA */
	ubyte				log_samples:3; /* at most 4 = 16xAA */
	ubyte				compressed_cb_mask;
	ubyte				uncompressed_cb_mask;
	ubyte				color_is_int8;
	ubyte				color_is_int10;
	ubyte				dirty_cbufs;
	bool				dirty_zsbuf;
	bool				any_dst_linear;
	bool				CB_has_shader_readable_metadata;
	bool				DB_has_shader_readable_metadata;
};

struct si_signed_scissor {
	int minx;
	int miny;
	int maxx;
	int maxy;
};

struct si_scissors {
	struct r600_atom		atom;
	unsigned			dirty_mask;
	struct pipe_scissor_state	states[SI_MAX_VIEWPORTS];
};

struct si_viewports {
	struct r600_atom		atom;
	unsigned			dirty_mask;
	unsigned			depth_range_dirty_mask;
	struct pipe_viewport_state	states[SI_MAX_VIEWPORTS];
	struct si_signed_scissor	as_scissor[SI_MAX_VIEWPORTS];
};

struct si_clip_state {
	struct r600_atom		atom;
	struct pipe_clip_state		state;
	bool				any_nonzeros;
};

struct si_sample_locs {
	struct r600_atom	atom;
	unsigned		nr_samples;
};

struct si_sample_mask {
	struct r600_atom	atom;
	uint16_t		sample_mask;
};

struct si_streamout_target {
	struct pipe_stream_output_target b;

	/* The buffer where BUFFER_FILLED_SIZE is stored. */
	struct r600_resource	*buf_filled_size;
	unsigned		buf_filled_size_offset;
	bool			buf_filled_size_valid;

	unsigned		stride_in_dw;
};

struct si_streamout {
	struct r600_atom		begin_atom;
	bool				begin_emitted;

	unsigned			enabled_mask;
	unsigned			num_targets;
	struct si_streamout_target	*targets[PIPE_MAX_SO_BUFFERS];

	unsigned			append_bitmask;
	bool				suspended;

	/* External state which comes from the vertex shader,
	 * it must be set explicitly when binding a shader. */
	uint16_t			*stride_in_dw;
	unsigned			enabled_stream_buffers_mask; /* stream0 buffers0-3 in 4 LSB */

	/* The state of VGT_STRMOUT_BUFFER_(CONFIG|EN). */
	unsigned			hw_enabled_mask;

	/* The state of VGT_STRMOUT_(CONFIG|EN). */
	struct r600_atom		enable_atom;
	bool				streamout_enabled;
	bool				prims_gen_query_enabled;
	int				num_prims_gen_queries;
};

/* A shader state consists of the shader selector, which is a constant state
 * object shared by multiple contexts and shouldn't be modified, and
 * the current shader variant selected for this context.
 */
struct si_shader_ctx_state {
	struct si_shader_selector	*cso;
	struct si_shader		*current;
};

#define SI_NUM_VGT_PARAM_KEY_BITS 12
#define SI_NUM_VGT_PARAM_STATES (1 << SI_NUM_VGT_PARAM_KEY_BITS)

/* The IA_MULTI_VGT_PARAM key used to index the table of precomputed values.
 * Some fields are set by state-change calls, most are set by draw_vbo.
 */
union si_vgt_param_key {
	struct {
#ifdef PIPE_ARCH_LITTLE_ENDIAN
		unsigned prim:4;
		unsigned uses_instancing:1;
		unsigned multi_instances_smaller_than_primgroup:1;
		unsigned primitive_restart:1;
		unsigned count_from_stream_output:1;
		unsigned line_stipple_enabled:1;
		unsigned uses_tess:1;
		unsigned tess_uses_prim_id:1;
		unsigned uses_gs:1;
		unsigned _pad:32 - SI_NUM_VGT_PARAM_KEY_BITS;
#else /* PIPE_ARCH_BIG_ENDIAN */
		unsigned _pad:32 - SI_NUM_VGT_PARAM_KEY_BITS;
		unsigned uses_gs:1;
		unsigned tess_uses_prim_id:1;
		unsigned uses_tess:1;
		unsigned line_stipple_enabled:1;
		unsigned count_from_stream_output:1;
		unsigned primitive_restart:1;
		unsigned multi_instances_smaller_than_primgroup:1;
		unsigned uses_instancing:1;
		unsigned prim:4;
#endif
	} u;
	uint32_t index;
};

struct si_texture_handle
{
	unsigned			desc_slot;
	bool				desc_dirty;
	struct pipe_sampler_view	*view;
	struct si_sampler_state		sstate;
};

struct si_image_handle
{
	unsigned			desc_slot;
	bool				desc_dirty;
	struct pipe_image_view		view;
};

struct si_saved_cs {
	struct pipe_reference	reference;
	struct si_context	*ctx;
	struct radeon_saved_cs	gfx;
	struct r600_resource	*trace_buf;
	unsigned		trace_id;

	unsigned		gfx_last_dw;
	bool			flushed;
	int64_t			time_flush;
};

struct si_context {
	struct pipe_context		b; /* base class */

	enum radeon_family		family;
	enum chip_class			chip_class;

	struct radeon_winsys		*ws;
	struct radeon_winsys_ctx	*ctx;
	struct radeon_winsys_cs		*gfx_cs;
	struct radeon_winsys_cs		*dma_cs;
	struct pipe_fence_handle	*last_gfx_fence;
	struct pipe_fence_handle	*last_sdma_fence;
	struct r600_resource		*eop_bug_scratch;
	struct u_upload_mgr		*cached_gtt_allocator;
	struct threaded_context		*tc;
	struct u_suballocator		*allocator_zeroed_memory;
	struct slab_child_pool		pool_transfers;
	struct slab_child_pool		pool_transfers_unsync; /* for threaded_context */
	struct pipe_device_reset_callback device_reset_callback;
	struct u_log_context		*log;
	void				*query_result_shader;
	struct blitter_context		*blitter;
	void				*custom_dsa_flush;
	void				*custom_blend_resolve;
	void				*custom_blend_fmask_decompress;
	void				*custom_blend_eliminate_fastclear;
	void				*custom_blend_dcc_decompress;
	void				*vs_blit_pos;
	void				*vs_blit_pos_layered;
	void				*vs_blit_color;
	void				*vs_blit_color_layered;
	void				*vs_blit_texcoord;
	struct si_screen		*screen;
	struct pipe_debug_callback	debug;
	LLVMTargetMachineRef		tm; /* only non-threaded compilation */
	struct si_shader_ctx_state	fixed_func_tcs_shader;
	struct r600_resource		*wait_mem_scratch;
	unsigned			wait_mem_number;
	uint16_t			prefetch_L2_mask;

	bool				gfx_flush_in_progress:1;
	bool				gfx_last_ib_is_busy:1;
	bool				compute_is_busy:1;

	unsigned			num_gfx_cs_flushes;
	unsigned			initial_gfx_cs_size;
	unsigned			gpu_reset_counter;
	unsigned			last_dirty_tex_counter;
	unsigned			last_compressed_colortex_counter;
	unsigned			last_num_draw_calls;
	unsigned			flags; /* flush flags */
	/* Current unaccounted memory usage. */
	uint64_t			vram;
	uint64_t			gtt;

	/* Atoms (direct states). */
	union si_state_atoms		atoms;
	unsigned			dirty_atoms; /* mask */
	/* PM4 states (precomputed immutable states) */
	unsigned			dirty_states;
	union si_state			queued;
	union si_state			emitted;

	/* Atom declarations. */
	struct si_framebuffer		framebuffer;
	struct si_sample_locs		msaa_sample_locs;
	struct r600_atom		db_render_state;
	struct r600_atom		dpbb_state;
	struct r600_atom		msaa_config;
	struct si_sample_mask		sample_mask;
	struct r600_atom		cb_render_state;
	unsigned			last_cb_target_mask;
	struct si_blend_color		blend_color;
	struct r600_atom		clip_regs;
	struct si_clip_state		clip_state;
	struct si_shader_data		shader_pointers;
	struct si_stencil_ref		stencil_ref;
	struct r600_atom		spi_map;
	struct si_scissors		scissors;
	struct si_streamout		streamout;
	struct si_viewports		viewports;

	/* Precomputed states. */
	struct si_pm4_state		*init_config;
	struct si_pm4_state		*init_config_gs_rings;
	bool				init_config_has_vgt_flush;
	struct si_pm4_state		*vgt_shader_config[4];

	/* shaders */
	struct si_shader_ctx_state	ps_shader;
	struct si_shader_ctx_state	gs_shader;
	struct si_shader_ctx_state	vs_shader;
	struct si_shader_ctx_state	tcs_shader;
	struct si_shader_ctx_state	tes_shader;
	struct si_cs_shader_state	cs_shader_state;

	/* shader information */
	struct si_vertex_elements	*vertex_elements;
	unsigned			sprite_coord_enable;
	bool				flatshade;
	bool				do_update_shaders;

	/* vertex buffer descriptors */
	uint32_t *vb_descriptors_gpu_list;
	struct r600_resource *vb_descriptors_buffer;
	unsigned vb_descriptors_offset;

	/* shader descriptors */
	struct si_descriptors		descriptors[SI_NUM_DESCS];
	unsigned			descriptors_dirty;
	unsigned			shader_pointers_dirty;
	unsigned			shader_needs_decompress_mask;
	struct si_buffer_resources	rw_buffers;
	struct si_buffer_resources	const_and_shader_buffers[SI_NUM_SHADERS];
	struct si_samplers		samplers[SI_NUM_SHADERS];
	struct si_images		images[SI_NUM_SHADERS];

	/* other shader resources */
	struct pipe_constant_buffer	null_const_buf; /* used for set_constant_buffer(NULL) on CIK */
	struct pipe_resource		*esgs_ring;
	struct pipe_resource		*gsvs_ring;
	struct pipe_resource		*tess_rings;
	union pipe_color_union		*border_color_table; /* in CPU memory, any endian */
	struct r600_resource		*border_color_buffer;
	union pipe_color_union		*border_color_map; /* in VRAM (slow access), little endian */
	unsigned			border_color_count;
	unsigned			num_vs_blit_sgprs;
	uint32_t			vs_blit_sh_data[SI_VS_BLIT_SGPRS_POS_TEXCOORD];

	/* Vertex and index buffers. */
	bool				vertex_buffers_dirty;
	bool				vertex_buffer_pointer_dirty;
	struct pipe_vertex_buffer	vertex_buffer[SI_NUM_VERTEX_BUFFERS];

	/* MSAA config state. */
	int				ps_iter_samples;
	bool				ps_uses_fbfetch;
	bool				smoothing_enabled;

	/* DB render state. */
	unsigned		ps_db_shader_control;
	unsigned		dbcb_copy_sample;
	bool			dbcb_depth_copy_enabled:1;
	bool			dbcb_stencil_copy_enabled:1;
	bool			db_flush_depth_inplace:1;
	bool			db_flush_stencil_inplace:1;
	bool			db_depth_clear:1;
	bool			db_depth_disable_expclear:1;
	bool			db_stencil_clear:1;
	bool			db_stencil_disable_expclear:1;
	bool			occlusion_queries_disabled:1;
	bool			generate_mipmap_for_depth:1;

	/* Emitted draw state. */
	bool			gs_tri_strip_adj_fix:1;
	bool			ls_vgpr_fix:1;
	int			last_index_size;
	int			last_base_vertex;
	int			last_start_instance;
	int			last_drawid;
	int			last_sh_base_reg;
	int			last_primitive_restart_en;
	int			last_restart_index;
	int			last_gs_out_prim;
	int			last_prim;
	int			last_multi_vgt_param;
	int			last_rast_prim;
	unsigned		last_sc_line_stipple;
	unsigned		current_vs_state;
	unsigned		last_vs_state;
	enum pipe_prim_type	current_rast_prim; /* primitive type after TES, GS */

	/* Scratch buffer */
	struct r600_atom	scratch_state;
	struct r600_resource	*scratch_buffer;
	unsigned		scratch_waves;
	unsigned		spi_tmpring_size;

	struct r600_resource	*compute_scratch_buffer;

	/* Emitted derived tessellation state. */
	/* Local shader (VS), or HS if LS-HS are merged. */
	struct si_shader	*last_ls;
	struct si_shader_selector *last_tcs;
	int			last_num_tcs_input_cp;
	int			last_tes_sh_base;
	bool			last_tess_uses_primid;
	unsigned		last_num_patches;

	/* Debug state. */
	bool			is_debug;
	struct si_saved_cs	*current_saved_cs;
	uint64_t		dmesg_timestamp;
	unsigned		apitrace_call_number;

	/* Other state */
	bool need_check_render_feedback;
	bool			decompression_enabled;

	bool			vs_writes_viewport_index;
	bool			vs_disables_clipping_viewport;

	/* Precomputed IA_MULTI_VGT_PARAM */
	union si_vgt_param_key  ia_multi_vgt_param_key;
	unsigned		ia_multi_vgt_param[SI_NUM_VGT_PARAM_STATES];

	/* Bindless descriptors. */
	struct si_descriptors	bindless_descriptors;
	struct util_idalloc	bindless_used_slots;
	unsigned		num_bindless_descriptors;
	bool			bindless_descriptors_dirty;
	bool			graphics_bindless_pointer_dirty;
	bool			compute_bindless_pointer_dirty;

	/* Allocated bindless handles */
	struct hash_table	*tex_handles;
	struct hash_table	*img_handles;

	/* Resident bindless handles */
	struct util_dynarray	resident_tex_handles;
	struct util_dynarray	resident_img_handles;

	/* Resident bindless handles which need decompression */
	struct util_dynarray	resident_tex_needs_color_decompress;
	struct util_dynarray	resident_img_needs_color_decompress;
	struct util_dynarray	resident_tex_needs_depth_decompress;

	/* Bindless state */
	bool			uses_bindless_samplers;
	bool			uses_bindless_images;

	/* MSAA sample locations.
	 * The first index is the sample index.
	 * The second index is the coordinate: X, Y. */
	float			sample_locations_1x[1][2];
	float			sample_locations_2x[2][2];
	float			sample_locations_4x[4][2];
	float			sample_locations_8x[8][2];
	float			sample_locations_16x[16][2];

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

	/* Queries. */
	/* Maintain the list of active queries for pausing between IBs. */
	int				num_occlusion_queries;
	int				num_perfect_occlusion_queries;
	struct list_head		active_queries;
	unsigned			num_cs_dw_queries_suspend;

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

	/* Copy one resource to another using async DMA. */
	void (*dma_copy)(struct pipe_context *ctx,
			 struct pipe_resource *dst,
			 unsigned dst_level,
			 unsigned dst_x, unsigned dst_y, unsigned dst_z,
			 struct pipe_resource *src,
			 unsigned src_level,
			 const struct pipe_box *src_box);

	void (*dma_clear_buffer)(struct si_context *sctx, struct pipe_resource *dst,
				 uint64_t offset, uint64_t size, unsigned value);
};

/* cik_sdma.c */
void cik_init_sdma_functions(struct si_context *sctx);

/* si_blit.c */
enum si_blitter_op /* bitmask */
{
	SI_SAVE_TEXTURES      = 1,
	SI_SAVE_FRAMEBUFFER   = 2,
	SI_SAVE_FRAGMENT_STATE = 4,
	SI_DISABLE_RENDER_COND = 8,
};

void si_blitter_begin(struct si_context *sctx, enum si_blitter_op op);
void si_blitter_end(struct si_context *sctx);
void si_init_blit_functions(struct si_context *sctx);
void si_decompress_textures(struct si_context *sctx, unsigned shader_mask);
void si_resource_copy_region(struct pipe_context *ctx,
			     struct pipe_resource *dst,
			     unsigned dst_level,
			     unsigned dstx, unsigned dsty, unsigned dstz,
			     struct pipe_resource *src,
			     unsigned src_level,
			     const struct pipe_box *src_box);
void si_decompress_dcc(struct si_context *sctx, struct r600_texture *rtex);
void si_blit_decompress_depth(struct pipe_context *ctx,
			      struct r600_texture *texture,
			      struct r600_texture *staging,
			      unsigned first_level, unsigned last_level,
			      unsigned first_layer, unsigned last_layer,
			      unsigned first_sample, unsigned last_sample);

/* si_buffer.c */
bool si_rings_is_buffer_referenced(struct si_context *sctx,
				   struct pb_buffer *buf,
				   enum radeon_bo_usage usage);
void *si_buffer_map_sync_with_rings(struct si_context *sctx,
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

/* si_clear.c */
void vi_dcc_clear_level(struct si_context *sctx,
			struct r600_texture *rtex,
			unsigned level, unsigned clear_value);
void si_init_clear_functions(struct si_context *sctx);

/* si_cp_dma.c */
#define SI_CPDMA_SKIP_CHECK_CS_SPACE	(1 << 0) /* don't call need_cs_space */
#define SI_CPDMA_SKIP_SYNC_AFTER	(1 << 1) /* don't wait for DMA after the copy */
#define SI_CPDMA_SKIP_SYNC_BEFORE	(1 << 2) /* don't wait for DMA before the copy (RAW hazards) */
#define SI_CPDMA_SKIP_GFX_SYNC		(1 << 3) /* don't flush caches and don't wait for PS/CS */
#define SI_CPDMA_SKIP_BO_LIST_UPDATE	(1 << 4) /* don't update the BO list */
#define SI_CPDMA_SKIP_ALL (SI_CPDMA_SKIP_CHECK_CS_SPACE | \
			   SI_CPDMA_SKIP_SYNC_AFTER | \
			   SI_CPDMA_SKIP_SYNC_BEFORE | \
			   SI_CPDMA_SKIP_GFX_SYNC | \
			   SI_CPDMA_SKIP_BO_LIST_UPDATE)

enum si_coherency {
	SI_COHERENCY_NONE, /* no cache flushes needed */
	SI_COHERENCY_SHADER,
	SI_COHERENCY_CB_META,
};

void si_cp_dma_wait_for_idle(struct si_context *sctx);
void si_clear_buffer(struct si_context *sctx, struct pipe_resource *dst,
		     uint64_t offset, uint64_t size, unsigned value,
		     enum si_coherency coher);
void si_copy_buffer(struct si_context *sctx,
		    struct pipe_resource *dst, struct pipe_resource *src,
		    uint64_t dst_offset, uint64_t src_offset, unsigned size,
		    unsigned user_flags);
void cik_prefetch_TC_L2_async(struct si_context *sctx, struct pipe_resource *buf,
			      uint64_t offset, unsigned size);
void cik_emit_prefetch_L2(struct si_context *sctx, bool vertex_stage_only);
void si_init_cp_dma_functions(struct si_context *sctx);

/* si_debug.c */
void si_save_cs(struct radeon_winsys *ws, struct radeon_winsys_cs *cs,
		struct radeon_saved_cs *saved, bool get_buffer_list);
void si_clear_saved_cs(struct radeon_saved_cs *saved);
void si_destroy_saved_cs(struct si_saved_cs *scs);
void si_auto_log_cs(void *data, struct u_log_context *log);
void si_log_hw_flush(struct si_context *sctx);
void si_log_draw_state(struct si_context *sctx, struct u_log_context *log);
void si_log_compute_state(struct si_context *sctx, struct u_log_context *log);
void si_init_debug_functions(struct si_context *sctx);
void si_check_vm_faults(struct si_context *sctx,
			struct radeon_saved_cs *saved, enum ring_type ring);
bool si_replace_shader(unsigned num, struct ac_shader_binary *binary);

/* si_dma.c */
void si_init_dma_functions(struct si_context *sctx);

/* si_dma_cs.c */
void si_need_dma_space(struct si_context *ctx, unsigned num_dw,
		       struct r600_resource *dst, struct r600_resource *src);
void si_flush_dma_cs(struct si_context *ctx, unsigned flags,
		     struct pipe_fence_handle **fence);
void si_screen_clear_buffer(struct si_screen *sscreen, struct pipe_resource *dst,
			    uint64_t offset, uint64_t size, unsigned value);

/* si_fence.c */
void si_gfx_write_event_eop(struct si_context *ctx,
			    unsigned event, unsigned event_flags,
			    unsigned data_sel,
			    struct r600_resource *buf, uint64_t va,
			    uint32_t new_fence, unsigned query_type);
unsigned si_gfx_write_fence_dwords(struct si_screen *screen);
void si_gfx_wait_fence(struct si_context *ctx,
		       uint64_t va, uint32_t ref, uint32_t mask);
void si_init_fence_functions(struct si_context *ctx);
void si_init_screen_fence_functions(struct si_screen *screen);
struct pipe_fence_handle *si_create_fence(struct pipe_context *ctx,
					  struct tc_unflushed_batch_token *tc_token);

/* si_get.c */
const char *si_get_family_name(const struct si_screen *sscreen);
void si_init_screen_get_functions(struct si_screen *sscreen);

/* si_gfx_cs.c */
void si_flush_gfx_cs(struct si_context *ctx, unsigned flags,
		     struct pipe_fence_handle **fence);
void si_begin_new_gfx_cs(struct si_context *ctx);
void si_need_gfx_cs_space(struct si_context *ctx);

/* r600_gpu_load.c */
void si_gpu_load_kill_thread(struct si_screen *sscreen);
uint64_t si_begin_counter(struct si_screen *sscreen, unsigned type);
unsigned si_end_counter(struct si_screen *sscreen, unsigned type,
			uint64_t begin);

/* si_compute.c */
void si_init_compute_functions(struct si_context *sctx);

/* si_perfcounters.c */
void si_init_perfcounters(struct si_screen *screen);

/* si_pipe.c */
bool si_check_device_reset(struct si_context *sctx);

/* si_query.c */
void si_init_screen_query_functions(struct si_screen *sscreen);
void si_init_query_functions(struct si_context *sctx);
void si_suspend_queries(struct si_context *sctx);
void si_resume_queries(struct si_context *sctx);

/* si_test_dma.c */
void si_test_dma(struct si_screen *sscreen);

/* si_uvd.c */
struct pipe_video_codec *si_uvd_create_decoder(struct pipe_context *context,
					       const struct pipe_video_codec *templ);

struct pipe_video_buffer *si_video_buffer_create(struct pipe_context *pipe,
						 const struct pipe_video_buffer *tmpl);

/* si_viewport.c */
void si_update_vs_viewport_state(struct si_context *ctx);
void si_init_viewport_functions(struct si_context *ctx);

/* r600_texture.c */
bool si_prepare_for_dma_blit(struct si_context *sctx,
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
void si_eliminate_fast_color_clear(struct si_context *sctx,
				   struct r600_texture *rtex);
void si_texture_discard_cmask(struct si_screen *sscreen,
			      struct r600_texture *rtex);
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
void vi_disable_dcc_if_incompatible_format(struct si_context *sctx,
					   struct pipe_resource *tex,
					   unsigned level,
					   enum pipe_format view_format);
struct pipe_surface *si_create_surface_custom(struct pipe_context *pipe,
					      struct pipe_resource *texture,
					      const struct pipe_surface *templ,
					      unsigned width0, unsigned height0,
					      unsigned width, unsigned height);
unsigned si_translate_colorswap(enum pipe_format format, bool do_endian_swap);
void vi_separate_dcc_try_enable(struct si_context *sctx,
				struct r600_texture *tex);
void vi_separate_dcc_start_query(struct si_context *sctx,
				 struct r600_texture *tex);
void vi_separate_dcc_stop_query(struct si_context *sctx,
				struct r600_texture *tex);
void vi_separate_dcc_process_and_reset_stats(struct pipe_context *ctx,
					     struct r600_texture *tex);
bool si_texture_disable_dcc(struct si_context *sctx,
			    struct r600_texture *rtex);
void si_init_screen_texture_functions(struct si_screen *sscreen);
void si_init_context_texture_functions(struct si_context *sctx);


/*
 * common helpers
 */

static inline void
si_context_add_resource_size(struct si_context *sctx, struct pipe_resource *r)
{
	struct r600_resource *res = (struct r600_resource *)r;

	if (res) {
		/* Add memory usage for need_gfx_cs_space */
		sctx->vram += res->vram_usage;
		sctx->gtt += res->gart_usage;
	}
}

static inline void
si_invalidate_draw_sh_constants(struct si_context *sctx)
{
	sctx->last_base_vertex = SI_BASE_VERTEX_UNKNOWN;
}

static inline void
si_set_atom_dirty(struct si_context *sctx,
		  struct r600_atom *atom, bool dirty)
{
	unsigned bit = 1 << atom->id;

	if (dirty)
		sctx->dirty_atoms |= bit;
	else
		sctx->dirty_atoms &= ~bit;
}

static inline bool
si_is_atom_dirty(struct si_context *sctx,
		  struct r600_atom *atom)
{
	unsigned bit = 1 << atom->id;

	return sctx->dirty_atoms & bit;
}

static inline void
si_mark_atom_dirty(struct si_context *sctx,
		   struct r600_atom *atom)
{
	si_set_atom_dirty(sctx, atom, true);
}

static inline struct si_shader_ctx_state *si_get_vs(struct si_context *sctx)
{
	if (sctx->gs_shader.cso)
		return &sctx->gs_shader;
	if (sctx->tes_shader.cso)
		return &sctx->tes_shader;

	return &sctx->vs_shader;
}

static inline struct tgsi_shader_info *si_get_vs_info(struct si_context *sctx)
{
	struct si_shader_ctx_state *vs = si_get_vs(sctx);

	return vs->cso ? &vs->cso->info : NULL;
}

static inline struct si_shader* si_get_vs_state(struct si_context *sctx)
{
	if (sctx->gs_shader.cso)
		return sctx->gs_shader.cso->gs_copy_shader;

	struct si_shader_ctx_state *vs = si_get_vs(sctx);
	return vs->current ? vs->current : NULL;
}

static inline bool si_can_dump_shader(struct si_screen *sscreen,
				      unsigned processor)
{
	return sscreen->debug_flags & (1 << processor);
}

static inline bool si_extra_shader_checks(struct si_screen *sscreen,
					  unsigned processor)
{
	return (sscreen->debug_flags & DBG(CHECK_IR)) ||
	       si_can_dump_shader(sscreen, processor);
}

static inline bool si_get_strmout_en(struct si_context *sctx)
{
	return sctx->streamout.streamout_enabled ||
	       sctx->streamout.prims_gen_query_enabled;
}

static inline unsigned
si_optimal_tcc_alignment(struct si_context *sctx, unsigned upload_size)
{
	unsigned alignment, tcc_cache_line_size;

	/* If the upload size is less than the cache line size (e.g. 16, 32),
	 * the whole thing will fit into a cache line if we align it to its size.
	 * The idea is that multiple small uploads can share a cache line.
	 * If the upload size is greater, align it to the cache line size.
	 */
	alignment = util_next_power_of_two(upload_size);
	tcc_cache_line_size = sctx->screen->info.tcc_cache_line_size;
	return MIN2(alignment, tcc_cache_line_size);
}

static inline void
si_saved_cs_reference(struct si_saved_cs **dst, struct si_saved_cs *src)
{
	if (pipe_reference(&(*dst)->reference, &src->reference))
		si_destroy_saved_cs(*dst);

	*dst = src;
}

static inline void
si_make_CB_shader_coherent(struct si_context *sctx, unsigned num_samples,
			   bool shaders_read_metadata)
{
	sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_CB |
		       SI_CONTEXT_INV_VMEM_L1;

	if (sctx->chip_class >= GFX9) {
		/* Single-sample color is coherent with shaders on GFX9, but
		 * L2 metadata must be flushed if shaders read metadata.
		 * (DCC, CMASK).
		 */
		if (num_samples >= 2)
			sctx->flags |= SI_CONTEXT_INV_GLOBAL_L2;
		else if (shaders_read_metadata)
			sctx->flags |= SI_CONTEXT_INV_L2_METADATA;
	} else {
		/* SI-CI-VI */
		sctx->flags |= SI_CONTEXT_INV_GLOBAL_L2;
	}
}

static inline void
si_make_DB_shader_coherent(struct si_context *sctx, unsigned num_samples,
			   bool include_stencil, bool shaders_read_metadata)
{
	sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_DB |
		       SI_CONTEXT_INV_VMEM_L1;

	if (sctx->chip_class >= GFX9) {
		/* Single-sample depth (not stencil) is coherent with shaders
		 * on GFX9, but L2 metadata must be flushed if shaders read
		 * metadata.
		 */
		if (num_samples >= 2 || include_stencil)
			sctx->flags |= SI_CONTEXT_INV_GLOBAL_L2;
		else if (shaders_read_metadata)
			sctx->flags |= SI_CONTEXT_INV_L2_METADATA;
	} else {
		/* SI-CI-VI */
		sctx->flags |= SI_CONTEXT_INV_GLOBAL_L2;
	}
}

static inline bool
si_can_sample_zs(struct r600_texture *tex, bool stencil_sampler)
{
	return (stencil_sampler && tex->can_sample_s) ||
	       (!stencil_sampler && tex->can_sample_z);
}

static inline bool
si_htile_enabled(struct r600_texture *tex, unsigned level)
{
	return tex->htile_offset && level == 0;
}

static inline bool
vi_tc_compat_htile_enabled(struct r600_texture *tex, unsigned level)
{
	assert(!tex->tc_compatible_htile || tex->htile_offset);
	return tex->tc_compatible_htile && level == 0;
}

static inline unsigned si_get_ps_iter_samples(struct si_context *sctx)
{
	if (sctx->ps_uses_fbfetch)
		return sctx->framebuffer.nr_samples;

	return sctx->ps_iter_samples;
}

static inline unsigned si_get_total_colormask(struct si_context *sctx)
{
	if (sctx->queued.named.rasterizer->rasterizer_discard)
		return 0;

	struct si_shader_selector *ps = sctx->ps_shader.cso;
	if (!ps)
		return 0;

	unsigned colormask = sctx->framebuffer.colorbuf_enabled_4bit &
			     sctx->queued.named.blend->cb_target_mask;

	if (!ps->info.properties[TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS])
		colormask &= ps->colors_written_4bit;
	else if (!ps->colors_written_4bit)
		colormask = 0; /* color0 writes all cbufs, but it's not written */

	return colormask;
}

/**
 * Return true if there is enough memory in VRAM and GTT for the buffers
 * added so far.
 *
 * \param vram      VRAM memory size not added to the buffer list yet
 * \param gtt       GTT memory size not added to the buffer list yet
 */
static inline bool
radeon_cs_memory_below_limit(struct si_screen *screen,
			     struct radeon_winsys_cs *cs,
			     uint64_t vram, uint64_t gtt)
{
	vram += cs->used_vram;
	gtt += cs->used_gart;

	/* Anything that goes above the VRAM size should go to GTT. */
	if (vram > screen->info.vram_size)
		gtt += vram - screen->info.vram_size;

	/* Now we just need to check if we have enough GTT. */
	return gtt < screen->info.gart_size * 0.7;
}

/**
 * Add a buffer to the buffer list for the given command stream (CS).
 *
 * All buffers used by a CS must be added to the list. This tells the kernel
 * driver which buffers are used by GPU commands. Other buffers can
 * be swapped out (not accessible) during execution.
 *
 * The buffer list becomes empty after every context flush and must be
 * rebuilt.
 */
static inline void radeon_add_to_buffer_list(struct si_context *sctx,
					     struct radeon_winsys_cs *cs,
					     struct r600_resource *rbo,
					     enum radeon_bo_usage usage,
					     enum radeon_bo_priority priority)
{
	assert(usage);
	sctx->ws->cs_add_buffer(
		cs, rbo->buf,
		(enum radeon_bo_usage)(usage | RADEON_USAGE_SYNCHRONIZED),
		rbo->domains, priority);
}

/**
 * Same as above, but also checks memory usage and flushes the context
 * accordingly.
 *
 * When this SHOULD NOT be used:
 *
 * - if si_context_add_resource_size has been called for the buffer
 *   followed by *_need_cs_space for checking the memory usage
 *
 * - if si_need_dma_space has been called for the buffer
 *
 * - when emitting state packets and draw packets (because preceding packets
 *   can't be re-emitted at that point)
 *
 * - if shader resource "enabled_mask" is not up-to-date or there is
 *   a different constraint disallowing a context flush
 */
static inline void
radeon_add_to_gfx_buffer_list_check_mem(struct si_context *sctx,
					struct r600_resource *rbo,
					enum radeon_bo_usage usage,
					enum radeon_bo_priority priority,
					bool check_mem)
{
	if (check_mem &&
	    !radeon_cs_memory_below_limit(sctx->screen, sctx->gfx_cs,
					  sctx->vram + rbo->vram_usage,
					  sctx->gtt + rbo->gart_usage))
		si_flush_gfx_cs(sctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);

	radeon_add_to_buffer_list(sctx, sctx->gfx_cs, rbo, usage, priority);
}

#define PRINT_ERR(fmt, args...) \
	fprintf(stderr, "EE %s:%d %s - " fmt, __FILE__, __LINE__, __func__, ##args)

#endif
