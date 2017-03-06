/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
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
 *      Jerome Glisse
 */
#ifndef SI_PIPE_H
#define SI_PIPE_H

#include "si_shader.h"

#ifdef PIPE_ARCH_BIG_ENDIAN
#define SI_BIG_ENDIAN 1
#else
#define SI_BIG_ENDIAN 0
#endif

/* The base vertex and primitive restart can be any number, but we must pick
 * one which will mean "unknown" for the purpose of state tracking and
 * the number shouldn't be a commonly-used one. */
#define SI_BASE_VERTEX_UNKNOWN INT_MIN
#define SI_RESTART_INDEX_UNKNOWN INT_MIN
#define SI_NUM_SMOOTH_AA_SAMPLES 8
#define SI_GS_PER_ES 128

/* Instruction cache. */
#define SI_CONTEXT_INV_ICACHE		(R600_CONTEXT_PRIVATE_FLAG << 0)
/* SMEM L1, other names: KCACHE, constant cache, DCACHE, data cache */
#define SI_CONTEXT_INV_SMEM_L1		(R600_CONTEXT_PRIVATE_FLAG << 1)
/* VMEM L1 can optionally be bypassed (GLC=1). Other names: TC L1 */
#define SI_CONTEXT_INV_VMEM_L1		(R600_CONTEXT_PRIVATE_FLAG << 2)
/* Used by everything except CB/DB, can be bypassed (SLC=1). Other names: TC L2 */
#define SI_CONTEXT_INV_GLOBAL_L2	(R600_CONTEXT_PRIVATE_FLAG << 3)
/* Write dirty L2 lines back to memory (shader and CP DMA stores), but don't
 * invalidate L2. SI-CIK can't do it, so they will do complete invalidation. */
#define SI_CONTEXT_WRITEBACK_GLOBAL_L2	(R600_CONTEXT_PRIVATE_FLAG << 4)
/* Framebuffer caches. */
#define SI_CONTEXT_FLUSH_AND_INV_CB_META (R600_CONTEXT_PRIVATE_FLAG << 5)
#define SI_CONTEXT_FLUSH_AND_INV_DB_META (R600_CONTEXT_PRIVATE_FLAG << 6)
#define SI_CONTEXT_FLUSH_AND_INV_DB	(R600_CONTEXT_PRIVATE_FLAG << 7)
#define SI_CONTEXT_FLUSH_AND_INV_CB	(R600_CONTEXT_PRIVATE_FLAG << 8)
/* Engine synchronization. */
#define SI_CONTEXT_VS_PARTIAL_FLUSH	(R600_CONTEXT_PRIVATE_FLAG << 9)
#define SI_CONTEXT_PS_PARTIAL_FLUSH	(R600_CONTEXT_PRIVATE_FLAG << 10)
#define SI_CONTEXT_CS_PARTIAL_FLUSH	(R600_CONTEXT_PRIVATE_FLAG << 11)
#define SI_CONTEXT_VGT_FLUSH		(R600_CONTEXT_PRIVATE_FLAG << 12)
#define SI_CONTEXT_VGT_STREAMOUT_SYNC	(R600_CONTEXT_PRIVATE_FLAG << 13)

#define SI_CONTEXT_FLUSH_AND_INV_FRAMEBUFFER (SI_CONTEXT_FLUSH_AND_INV_CB | \
					      SI_CONTEXT_FLUSH_AND_INV_CB_META | \
					      SI_CONTEXT_FLUSH_AND_INV_DB | \
					      SI_CONTEXT_FLUSH_AND_INV_DB_META)

#define SI_MAX_BORDER_COLORS	4096

struct si_compute;
struct hash_table;
struct u_suballocator;

struct si_screen {
	struct r600_common_screen	b;
	unsigned			gs_table_depth;
	unsigned			tess_offchip_block_dw_size;
	bool				has_distributed_tess;
	bool				has_draw_indirect_multi;
	bool				has_ds_bpermute;

	/* Whether shaders are monolithic (1-part) or separate (3-part). */
	bool				use_monolithic_shaders;
	bool				record_llvm_ir;

	pipe_mutex			shader_parts_mutex;
	struct si_shader_part		*vs_prologs;
	struct si_shader_part		*vs_epilogs;
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
	pipe_mutex			shader_cache_mutex;
	struct hash_table		*shader_cache;

	/* Shader compiler queue for multithreaded compilation. */
	struct util_queue		shader_compiler_queue;
	LLVMTargetMachineRef		tm[4]; /* used by the queue only */
};

struct si_blend_color {
	struct r600_atom		atom;
	struct pipe_blend_color		state;
};

struct si_sampler_view {
	struct pipe_sampler_view	base;
        /* [0..7] = image descriptor
         * [4..7] = buffer descriptor */
	uint32_t			state[8];
	uint32_t			fmask_state[8];
	const struct radeon_surf_level	*base_level_info;
	unsigned			base_level;
	unsigned			block_width;
	bool is_stencil_sampler;
};

#define SI_SAMPLER_STATE_MAGIC 0x34f1c35a

struct si_sampler_state {
#ifdef DEBUG
	unsigned			magic;
#endif
	uint32_t			val[4];
};

struct si_cs_shader_state {
	struct si_compute		*program;
	struct si_compute		*emitted_program;
	unsigned			offset;
	bool				initialized;
	bool				uses_scratch;
};

struct si_textures_info {
	struct si_sampler_views		views;
	uint32_t			depth_texture_mask; /* which textures are depth */
	uint32_t			compressed_colortex_mask;
};

struct si_images_info {
	struct pipe_image_view		views[SI_NUM_IMAGES];
	uint32_t			compressed_colortex_mask;
	unsigned			enabled_mask;
};

struct si_framebuffer {
	struct r600_atom		atom;
	struct pipe_framebuffer_state	state;
	unsigned			nr_samples;
	unsigned			log_samples;
	unsigned			compressed_cb_mask;
	unsigned			colorbuf_enabled_4bit;
	unsigned			spi_shader_col_format;
	unsigned			spi_shader_col_format_alpha;
	unsigned			spi_shader_col_format_blend;
	unsigned			spi_shader_col_format_blend_alpha;
	unsigned			color_is_int8;
	unsigned			color_is_int10;
	unsigned			dirty_cbufs;
	bool				dirty_zsbuf;
	bool				any_dst_linear;
};

struct si_clip_state {
	struct r600_atom		atom;
	struct pipe_clip_state		state;
};

struct si_sample_locs {
	struct r600_atom	atom;
	unsigned		nr_samples;
};

struct si_sample_mask {
	struct r600_atom	atom;
	uint16_t		sample_mask;
};

/* A shader state consists of the shader selector, which is a constant state
 * object shared by multiple contexts and shouldn't be modified, and
 * the current shader variant selected for this context.
 */
struct si_shader_ctx_state {
	struct si_shader_selector	*cso;
	struct si_shader		*current;
};

struct si_context {
	struct r600_common_context	b;
	struct blitter_context		*blitter;
	void				*custom_dsa_flush;
	void				*custom_blend_resolve;
	void				*custom_blend_decompress;
	void				*custom_blend_fastclear;
	void				*custom_blend_dcc_decompress;
	struct si_screen		*screen;

	struct radeon_winsys_cs		*ce_ib;
	struct radeon_winsys_cs		*ce_preamble_ib;
	bool				ce_need_synchronization;
	struct u_suballocator		*ce_suballocator;

	struct si_shader_ctx_state	fixed_func_tcs_shader;
	LLVMTargetMachineRef		tm; /* only non-threaded compilation */
	bool				gfx_flush_in_progress;
	bool				compute_is_busy;

	/* Atoms (direct states). */
	union si_state_atoms		atoms;
	unsigned			dirty_atoms; /* mask */
	/* PM4 states (precomputed immutable states) */
	union si_state			queued;
	union si_state			emitted;

	/* Atom declarations. */
	struct si_framebuffer		framebuffer;
	struct si_sample_locs		msaa_sample_locs;
	struct r600_atom		db_render_state;
	struct r600_atom		msaa_config;
	struct si_sample_mask		sample_mask;
	struct r600_atom		cb_render_state;
	struct si_blend_color		blend_color;
	struct r600_atom		clip_regs;
	struct si_clip_state		clip_state;
	struct si_shader_data		shader_userdata;
	struct si_stencil_ref		stencil_ref;
	struct r600_atom		spi_map;

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
	struct si_vertex_element	*vertex_elements;
	unsigned			sprite_coord_enable;
	bool				flatshade;
	bool				do_update_shaders;

	/* shader descriptors */
	struct si_descriptors		vertex_buffers;
	struct si_descriptors		descriptors[SI_NUM_DESCS];
	unsigned			descriptors_dirty;
	unsigned			shader_pointers_dirty;
	unsigned			compressed_tex_shader_mask;
	struct si_buffer_resources	rw_buffers;
	struct si_buffer_resources	const_buffers[SI_NUM_SHADERS];
	struct si_buffer_resources	shader_buffers[SI_NUM_SHADERS];
	struct si_textures_info		samplers[SI_NUM_SHADERS];
	struct si_images_info		images[SI_NUM_SHADERS];

	/* other shader resources */
	struct pipe_constant_buffer	null_const_buf; /* used for set_constant_buffer(NULL) on CIK */
	struct pipe_resource		*esgs_ring;
	struct pipe_resource		*gsvs_ring;
	struct pipe_resource		*tf_ring;
	struct pipe_resource		*tess_offchip_ring;
	union pipe_color_union		*border_color_table; /* in CPU memory, any endian */
	struct r600_resource		*border_color_buffer;
	union pipe_color_union		*border_color_map; /* in VRAM (slow access), little endian */
	unsigned			border_color_count;

	/* Vertex and index buffers. */
	bool				vertex_buffers_dirty;
	bool				vertex_buffer_pointer_dirty;
	struct pipe_index_buffer	index_buffer;
	struct pipe_vertex_buffer	vertex_buffer[SI_NUM_VERTEX_BUFFERS];

	/* MSAA config state. */
	int				ps_iter_samples;
	bool				smoothing_enabled;

	/* DB render state. */
	bool			dbcb_depth_copy_enabled;
	bool			dbcb_stencil_copy_enabled;
	unsigned		dbcb_copy_sample;
	bool			db_flush_depth_inplace;
	bool			db_flush_stencil_inplace;
	bool			db_depth_clear;
	bool			db_depth_disable_expclear;
	bool			db_stencil_clear;
	bool			db_stencil_disable_expclear;
	unsigned		ps_db_shader_control;
	bool			occlusion_queries_disabled;

	/* Emitted draw state. */
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
	int			last_vtx_reuse_depth;
	int			current_rast_prim; /* primitive type after TES, GS */
	bool			gs_tri_strip_adj_fix;

	/* Scratch buffer */
	struct r600_resource	*scratch_buffer;
	bool			emit_scratch_reloc;
	unsigned		scratch_waves;
	unsigned		spi_tmpring_size;

	struct r600_resource	*compute_scratch_buffer;

	/* Emitted derived tessellation state. */
	struct si_shader	*last_ls; /* local shader (VS) */
	struct si_shader_selector *last_tcs;
	int			last_num_tcs_input_cp;
	int			last_tes_sh_base;
	unsigned		last_num_patches;

	/* Debug state. */
	bool			is_debug;
	struct radeon_saved_cs	last_gfx;
	struct r600_resource	*last_trace_buf;
	struct r600_resource	*trace_buf;
	unsigned		trace_id;
	uint64_t		dmesg_timestamp;
	unsigned		apitrace_call_number;

	/* Other state */
	bool need_check_render_feedback;
};

/* cik_sdma.c */
void cik_init_sdma_functions(struct si_context *sctx);

/* si_blit.c */
void si_init_blit_functions(struct si_context *sctx);
void si_decompress_graphics_textures(struct si_context *sctx);
void si_decompress_compute_textures(struct si_context *sctx);
void si_resource_copy_region(struct pipe_context *ctx,
			     struct pipe_resource *dst,
			     unsigned dst_level,
			     unsigned dstx, unsigned dsty, unsigned dstz,
			     struct pipe_resource *src,
			     unsigned src_level,
			     const struct pipe_box *src_box);

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

void si_copy_buffer(struct si_context *sctx,
		    struct pipe_resource *dst, struct pipe_resource *src,
		    uint64_t dst_offset, uint64_t src_offset, unsigned size,
		    unsigned user_flags);
void cik_prefetch_TC_L2_async(struct si_context *sctx, struct pipe_resource *buf,
			      uint64_t offset, unsigned size);
void si_init_cp_dma_functions(struct si_context *sctx);

/* si_debug.c */
void si_init_debug_functions(struct si_context *sctx);
void si_check_vm_faults(struct r600_common_context *ctx,
			struct radeon_saved_cs *saved, enum ring_type ring);
bool si_replace_shader(unsigned num, struct radeon_shader_binary *binary);

/* si_dma.c */
void si_init_dma_functions(struct si_context *sctx);

/* si_hw_context.c */
void si_context_gfx_flush(void *context, unsigned flags,
			  struct pipe_fence_handle **fence);
void si_begin_new_cs(struct si_context *ctx);
void si_need_cs_space(struct si_context *ctx);

/* si_compute.c */
void si_init_compute_functions(struct si_context *sctx);

/* si_perfcounters.c */
void si_init_perfcounters(struct si_screen *screen);

/* si_uvd.c */
struct pipe_video_codec *si_uvd_create_decoder(struct pipe_context *context,
					       const struct pipe_video_codec *templ);

struct pipe_video_buffer *si_video_buffer_create(struct pipe_context *pipe,
						 const struct pipe_video_buffer *tmpl);

/*
 * common helpers
 */

static inline void
si_invalidate_draw_sh_constants(struct si_context *sctx)
{
	sctx->last_base_vertex = SI_BASE_VERTEX_UNKNOWN;
}

static inline void
si_set_atom_dirty(struct si_context *sctx,
		  struct r600_atom *atom, bool dirty)
{
	unsigned bit = 1 << (atom->id - 1);

	if (dirty)
		sctx->dirty_atoms |= bit;
	else
		sctx->dirty_atoms &= ~bit;
}

static inline bool
si_is_atom_dirty(struct si_context *sctx,
		  struct r600_atom *atom)
{
	unsigned bit = 1 << (atom->id - 1);

	return sctx->dirty_atoms & bit;
}

static inline void
si_mark_atom_dirty(struct si_context *sctx,
		   struct r600_atom *atom)
{
	si_set_atom_dirty(sctx, atom, true);
}

static inline struct tgsi_shader_info *si_get_vs_info(struct si_context *sctx)
{
	if (sctx->gs_shader.cso)
		return &sctx->gs_shader.cso->info;
	else if (sctx->tes_shader.cso)
		return &sctx->tes_shader.cso->info;
	else if (sctx->vs_shader.cso)
		return &sctx->vs_shader.cso->info;
	else
		return NULL;
}

static inline struct si_shader* si_get_vs_state(struct si_context *sctx)
{
	if (sctx->gs_shader.current)
		return sctx->gs_shader.cso->gs_copy_shader;
	else if (sctx->tes_shader.current)
		return sctx->tes_shader.current;
	else
		return sctx->vs_shader.current;
}

static inline bool si_vs_exports_prim_id(struct si_shader *shader)
{
	if (shader->selector->type == PIPE_SHADER_VERTEX)
		return shader->key.part.vs.epilog.export_prim_id;
	else if (shader->selector->type == PIPE_SHADER_TESS_EVAL)
		return shader->key.part.tes.epilog.export_prim_id;
	else
		return false;
}

#endif
