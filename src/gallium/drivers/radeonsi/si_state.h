/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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
 *      Christian König <christian.koenig@amd.com>
 */

#ifndef SI_STATE_H
#define SI_STATE_H

#include "si_pm4.h"
#include "radeon/r600_pipe_common.h"

#define SI_NUM_GRAPHICS_SHADERS (PIPE_SHADER_TESS_EVAL+1)
#define SI_NUM_SHADERS (PIPE_SHADER_COMPUTE+1)

#define SI_MAX_ATTRIBS			16
#define SI_NUM_VERTEX_BUFFERS		SI_MAX_ATTRIBS
#define SI_NUM_SAMPLERS			32 /* OpenGL textures units per shader */
#define SI_NUM_CONST_BUFFERS		16
#define SI_NUM_IMAGES			16
#define SI_NUM_SHADER_BUFFERS		16

struct si_screen;
struct si_shader;

struct si_state_blend {
	struct si_pm4_state	pm4;
	uint32_t		cb_target_mask;
	bool			alpha_to_coverage;
	bool			alpha_to_one;
	bool			dual_src_blend;
	/* Set 0xf or 0x0 (4 bits) per render target if the following is
	 * true. ANDed with spi_shader_col_format.
	 */
	unsigned		blend_enable_4bit;
	unsigned		need_src_alpha_4bit;
};

struct si_state_rasterizer {
	struct si_pm4_state	pm4;
	/* poly offset states for 16-bit, 24-bit, and 32-bit zbuffers */
	struct si_pm4_state	pm4_poly_offset[3];
	bool			flatshade;
	bool			two_side;
	bool			multisample_enable;
	bool			force_persample_interp;
	bool			line_stipple_enable;
	unsigned		sprite_coord_enable;
	unsigned		pa_sc_line_stipple;
	unsigned		pa_cl_clip_cntl;
	unsigned		clip_plane_enable;
	bool			poly_stipple_enable;
	bool			line_smooth;
	bool			poly_smooth;
	bool			uses_poly_offset;
	bool			clamp_fragment_color;
	bool			rasterizer_discard;
	bool			scissor_enable;
	bool			clip_halfz;
};

struct si_dsa_stencil_ref_part {
	uint8_t			valuemask[2];
	uint8_t			writemask[2];
};

struct si_state_dsa {
	struct si_pm4_state		pm4;
	unsigned			alpha_func;
	struct si_dsa_stencil_ref_part	stencil_ref;
};

struct si_stencil_ref {
	struct r600_atom		atom;
	struct pipe_stencil_ref		state;
	struct si_dsa_stencil_ref_part	dsa_part;
};

struct si_vertex_element
{
	unsigned			count;
	unsigned			first_vb_use_mask;

	/* Two bits per attribute indicating the size of each vector component
	 * in bytes if the size 3-workaround must be applied.
	 */
	uint32_t			fix_size3;
	uint64_t			fix_fetch;

	uint32_t			rsrc_word3[SI_MAX_ATTRIBS];
	uint32_t			format_size[SI_MAX_ATTRIBS];
	struct pipe_vertex_element	elements[SI_MAX_ATTRIBS];
};

union si_state {
	struct {
		struct si_state_blend		*blend;
		struct si_state_rasterizer	*rasterizer;
		struct si_state_dsa		*dsa;
		struct si_pm4_state		*poly_offset;
		struct si_pm4_state		*ls;
		struct si_pm4_state		*hs;
		struct si_pm4_state		*es;
		struct si_pm4_state		*gs;
		struct si_pm4_state		*vgt_shader_config;
		struct si_pm4_state		*vs;
		struct si_pm4_state		*ps;
	} named;
	struct si_pm4_state	*array[0];
};

union si_state_atoms {
	struct {
		/* The order matters. */
		struct r600_atom *render_cond;
		struct r600_atom *streamout_begin;
		struct r600_atom *streamout_enable; /* must be after streamout_begin */
		struct r600_atom *framebuffer;
		struct r600_atom *msaa_sample_locs;
		struct r600_atom *db_render_state;
		struct r600_atom *msaa_config;
		struct r600_atom *sample_mask;
		struct r600_atom *cb_render_state;
		struct r600_atom *blend_color;
		struct r600_atom *clip_regs;
		struct r600_atom *clip_state;
		struct r600_atom *shader_userdata;
		struct r600_atom *scissors;
		struct r600_atom *viewports;
		struct r600_atom *stencil_ref;
		struct r600_atom *spi_map;
	} s;
	struct r600_atom *array[0];
};

#define SI_NUM_ATOMS (sizeof(union si_state_atoms)/sizeof(struct r600_atom*))

struct si_shader_data {
	struct r600_atom	atom;
	uint32_t		sh_base[SI_NUM_SHADERS];
};

/* Private read-write buffer slots. */
enum {
	SI_HS_RING_TESS_FACTOR,
	SI_HS_RING_TESS_OFFCHIP,

	SI_ES_RING_ESGS,
	SI_GS_RING_ESGS,

	SI_RING_GSVS,

	SI_VS_STREAMOUT_BUF0,
	SI_VS_STREAMOUT_BUF1,
	SI_VS_STREAMOUT_BUF2,
	SI_VS_STREAMOUT_BUF3,

	SI_HS_CONST_DEFAULT_TESS_LEVELS,
	SI_VS_CONST_CLIP_PLANES,
	SI_PS_CONST_POLY_STIPPLE,
	SI_PS_CONST_SAMPLE_POSITIONS,

	SI_NUM_RW_BUFFERS,
};

/* Indices into sctx->descriptors, laid out so that gfx and compute pipelines
 * are contiguous:
 *
 *  0 - rw buffers
 *  1 - vertex const buffers
 *  2 - vertex shader buffers
 *   ...
 *  5 - fragment const buffers
 *   ...
 *  21 - compute const buffers
 *   ...
 */
#define SI_SHADER_DESCS_CONST_BUFFERS  0
#define SI_SHADER_DESCS_SHADER_BUFFERS 1
#define SI_SHADER_DESCS_SAMPLERS       2
#define SI_SHADER_DESCS_IMAGES         3
#define SI_NUM_SHADER_DESCS            4

#define SI_DESCS_RW_BUFFERS            0
#define SI_DESCS_FIRST_SHADER          1
#define SI_DESCS_FIRST_COMPUTE         (SI_DESCS_FIRST_SHADER + \
                                        PIPE_SHADER_COMPUTE * SI_NUM_SHADER_DESCS)
#define SI_NUM_DESCS                   (SI_DESCS_FIRST_SHADER + \
                                        SI_NUM_SHADERS * SI_NUM_SHADER_DESCS)

/* This represents descriptors in memory, such as buffer resources,
 * image resources, and sampler states.
 */
struct si_descriptors {
	/* The list of descriptors in malloc'd memory. */
	uint32_t *list;
	/* The list in mapped GPU memory. */
	uint32_t *gpu_list;
	/* The size of one descriptor. */
	unsigned element_dw_size;
	/* The maximum number of descriptors. */
	unsigned num_elements;

	/* The buffer where the descriptors have been uploaded. */
	struct r600_resource *buffer;
	unsigned buffer_offset;

	/* Offset in CE RAM */
	unsigned ce_offset;

	/* elements of the list that are changed and need to be uploaded */
	unsigned dirty_mask;

	/* Whether the CE ram is dirty and needs to be reinitialized entirely
	 * before we can do partial updates. */
	bool ce_ram_dirty;

	/* The shader userdata offset within a shader where the 64-bit pointer to the descriptor
	 * array will be stored. */
	unsigned shader_userdata_offset;
};

struct si_sampler_views {
	struct pipe_sampler_view	*views[SI_NUM_SAMPLERS];
	struct si_sampler_state		*sampler_states[SI_NUM_SAMPLERS];

	/* The i-th bit is set if that element is enabled (non-NULL resource). */
	unsigned			enabled_mask;
};

struct si_buffer_resources {
	enum radeon_bo_usage		shader_usage; /* READ, WRITE, or READWRITE */
	enum radeon_bo_priority		priority;
	struct pipe_resource		**buffers; /* this has num_buffers elements */

	/* The i-th bit is set if that element is enabled (non-NULL resource). */
	unsigned			enabled_mask;
};

#define si_pm4_block_idx(member) \
	(offsetof(union si_state, named.member) / sizeof(struct si_pm4_state *))

#define si_pm4_state_changed(sctx, member) \
	((sctx)->queued.named.member != (sctx)->emitted.named.member)

#define si_pm4_bind_state(sctx, member, value) \
	do { \
		(sctx)->queued.named.member = (value); \
	} while(0)

#define si_pm4_delete_state(sctx, member, value) \
	do { \
		if ((sctx)->queued.named.member == (value)) { \
			(sctx)->queued.named.member = NULL; \
		} \
		si_pm4_free_state(sctx, (struct si_pm4_state *)(value), \
				  si_pm4_block_idx(member)); \
	} while(0)

/* si_descriptors.c */
void si_ce_reinitialize_all_descriptors(struct si_context *sctx);
void si_ce_enable_loads(struct radeon_winsys_cs *ib);
void si_set_mutable_tex_desc_fields(struct r600_texture *tex,
				    const struct radeon_surf_level *base_level_info,
				    unsigned base_level, unsigned first_level,
				    unsigned block_width, bool is_stencil,
				    uint32_t *state);
void si_get_pipe_constant_buffer(struct si_context *sctx, uint shader,
				 uint slot, struct pipe_constant_buffer *cbuf);
void si_get_shader_buffers(struct si_context *sctx, uint shader,
			   uint start_slot, uint count,
			   struct pipe_shader_buffer *sbuf);
void si_set_ring_buffer(struct pipe_context *ctx, uint slot,
			struct pipe_resource *buffer,
			unsigned stride, unsigned num_records,
			bool add_tid, bool swizzle,
			unsigned element_size, unsigned index_stride, uint64_t offset);
void si_init_all_descriptors(struct si_context *sctx);
bool si_upload_vertex_buffer_descriptors(struct si_context *sctx);
bool si_upload_graphics_shader_descriptors(struct si_context *sctx);
bool si_upload_compute_shader_descriptors(struct si_context *sctx);
void si_release_all_descriptors(struct si_context *sctx);
void si_all_descriptors_begin_new_cs(struct si_context *sctx);
void si_upload_const_buffer(struct si_context *sctx, struct r600_resource **rbuffer,
			    const uint8_t *ptr, unsigned size, uint32_t *const_offset);
void si_update_all_texture_descriptors(struct si_context *sctx);
void si_shader_change_notify(struct si_context *sctx);
void si_update_compressed_colortex_masks(struct si_context *sctx);
void si_emit_graphics_shader_userdata(struct si_context *sctx,
                                      struct r600_atom *atom);
void si_emit_compute_shader_userdata(struct si_context *sctx);
void si_set_rw_buffer(struct si_context *sctx,
		      uint slot, const struct pipe_constant_buffer *input);
/* si_state.c */
struct si_shader_selector;

void si_init_atom(struct si_context *sctx, struct r600_atom *atom,
		  struct r600_atom **list_elem,
		  void (*emit_func)(struct si_context *ctx, struct r600_atom *state));
void si_init_state_functions(struct si_context *sctx);
void si_init_screen_state_functions(struct si_screen *sscreen);
void
si_make_buffer_descriptor(struct si_screen *screen, struct r600_resource *buf,
			  enum pipe_format format,
			  unsigned offset, unsigned size,
			  uint32_t *state);
void
si_make_texture_descriptor(struct si_screen *screen,
			   struct r600_texture *tex,
			   bool sampler,
			   enum pipe_texture_target target,
			   enum pipe_format pipe_format,
			   const unsigned char state_swizzle[4],
			   unsigned first_level, unsigned last_level,
			   unsigned first_layer, unsigned last_layer,
			   unsigned width, unsigned height, unsigned depth,
			   uint32_t *state,
			   uint32_t *fmask_state);
struct pipe_sampler_view *
si_create_sampler_view_custom(struct pipe_context *ctx,
			      struct pipe_resource *texture,
			      const struct pipe_sampler_view *state,
			      unsigned width0, unsigned height0,
			      unsigned force_level);

/* si_state_shader.c */
bool si_update_shaders(struct si_context *sctx);
void si_init_shader_functions(struct si_context *sctx);
bool si_init_shader_cache(struct si_screen *sscreen);
void si_destroy_shader_cache(struct si_screen *sscreen);
void si_init_shader_selector_async(void *job, int thread_index);

/* si_state_draw.c */
void si_emit_cache_flush(struct si_context *sctx);
void si_ce_pre_draw_synchronization(struct si_context *sctx);
void si_ce_post_draw_synchronization(struct si_context *sctx);
void si_draw_vbo(struct pipe_context *ctx, const struct pipe_draw_info *dinfo);
void si_trace_emit(struct si_context *sctx);


static inline unsigned
si_tile_mode_index(struct r600_texture *rtex, unsigned level, bool stencil)
{
	if (stencil)
		return rtex->surface.stencil_tiling_index[level];
	else
		return rtex->surface.tiling_index[level];
}

#endif
