/*
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics to
 develop this 3D driver.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */


#ifndef BRW_STATE_H
#define BRW_STATE_H

#include "brw_context.h"

#ifdef __cplusplus
extern "C" {
#endif

enum intel_msaa_layout;

extern const struct brw_tracked_state brw_blend_constant_color;
extern const struct brw_tracked_state brw_cc_vp;
extern const struct brw_tracked_state brw_cc_unit;
extern const struct brw_tracked_state brw_clip_unit;
extern const struct brw_tracked_state brw_vs_pull_constants;
extern const struct brw_tracked_state brw_tcs_pull_constants;
extern const struct brw_tracked_state brw_tes_pull_constants;
extern const struct brw_tracked_state brw_gs_pull_constants;
extern const struct brw_tracked_state brw_wm_pull_constants;
extern const struct brw_tracked_state brw_cs_pull_constants;
extern const struct brw_tracked_state brw_constant_buffer;
extern const struct brw_tracked_state brw_curbe_offsets;
extern const struct brw_tracked_state brw_invariant_state;
extern const struct brw_tracked_state brw_fs_samplers;
extern const struct brw_tracked_state brw_gs_unit;
extern const struct brw_tracked_state brw_line_stipple;
extern const struct brw_tracked_state brw_aa_line_parameters;
extern const struct brw_tracked_state brw_binding_table_pointers;
extern const struct brw_tracked_state brw_depthbuffer;
extern const struct brw_tracked_state brw_polygon_stipple_offset;
extern const struct brw_tracked_state brw_polygon_stipple;
extern const struct brw_tracked_state brw_recalculate_urb_fence;
extern const struct brw_tracked_state brw_sf_unit;
extern const struct brw_tracked_state brw_sf_vp;
extern const struct brw_tracked_state brw_vs_samplers;
extern const struct brw_tracked_state brw_tcs_samplers;
extern const struct brw_tracked_state brw_tes_samplers;
extern const struct brw_tracked_state brw_gs_samplers;
extern const struct brw_tracked_state brw_cs_samplers;
extern const struct brw_tracked_state brw_cs_texture_surfaces;
extern const struct brw_tracked_state brw_vs_ubo_surfaces;
extern const struct brw_tracked_state brw_vs_abo_surfaces;
extern const struct brw_tracked_state brw_vs_image_surfaces;
extern const struct brw_tracked_state brw_tcs_ubo_surfaces;
extern const struct brw_tracked_state brw_tcs_abo_surfaces;
extern const struct brw_tracked_state brw_tcs_image_surfaces;
extern const struct brw_tracked_state brw_tes_ubo_surfaces;
extern const struct brw_tracked_state brw_tes_abo_surfaces;
extern const struct brw_tracked_state brw_tes_image_surfaces;
extern const struct brw_tracked_state brw_gs_ubo_surfaces;
extern const struct brw_tracked_state brw_gs_abo_surfaces;
extern const struct brw_tracked_state brw_gs_image_surfaces;
extern const struct brw_tracked_state brw_vs_unit;
extern const struct brw_tracked_state brw_renderbuffer_surfaces;
extern const struct brw_tracked_state brw_renderbuffer_read_surfaces;
extern const struct brw_tracked_state brw_texture_surfaces;
extern const struct brw_tracked_state brw_wm_binding_table;
extern const struct brw_tracked_state brw_gs_binding_table;
extern const struct brw_tracked_state brw_tes_binding_table;
extern const struct brw_tracked_state brw_tcs_binding_table;
extern const struct brw_tracked_state brw_vs_binding_table;
extern const struct brw_tracked_state brw_wm_ubo_surfaces;
extern const struct brw_tracked_state brw_wm_abo_surfaces;
extern const struct brw_tracked_state brw_wm_image_surfaces;
extern const struct brw_tracked_state brw_cs_ubo_surfaces;
extern const struct brw_tracked_state brw_cs_abo_surfaces;
extern const struct brw_tracked_state brw_cs_image_surfaces;
extern const struct brw_tracked_state brw_wm_unit;

extern const struct brw_tracked_state brw_psp_urb_cbs;

extern const struct brw_tracked_state brw_drawing_rect;
extern const struct brw_tracked_state brw_indices;
extern const struct brw_tracked_state brw_vertices;
extern const struct brw_tracked_state brw_index_buffer;
extern const struct brw_tracked_state brw_cs_state;
extern const struct brw_tracked_state gen7_cs_push_constants;
extern const struct brw_tracked_state gen6_binding_table_pointers;
extern const struct brw_tracked_state gen6_blend_state;
extern const struct brw_tracked_state gen6_clip_state;
extern const struct brw_tracked_state gen6_clip_vp;
extern const struct brw_tracked_state gen6_color_calc_state;
extern const struct brw_tracked_state gen6_depth_stencil_state;
extern const struct brw_tracked_state gen6_gs_state;
extern const struct brw_tracked_state gen6_gs_push_constants;
extern const struct brw_tracked_state gen6_gs_binding_table;
extern const struct brw_tracked_state gen6_multisample_state;
extern const struct brw_tracked_state gen6_renderbuffer_surfaces;
extern const struct brw_tracked_state gen6_sampler_state;
extern const struct brw_tracked_state gen6_scissor_state;
extern const struct brw_tracked_state gen6_sol_surface;
extern const struct brw_tracked_state gen6_sf_state;
extern const struct brw_tracked_state gen6_sf_vp;
extern const struct brw_tracked_state gen6_urb;
extern const struct brw_tracked_state gen6_viewport_state;
extern const struct brw_tracked_state gen6_vs_push_constants;
extern const struct brw_tracked_state gen6_vs_state;
extern const struct brw_tracked_state gen6_wm_push_constants;
extern const struct brw_tracked_state gen6_wm_state;
extern const struct brw_tracked_state gen7_depthbuffer;
extern const struct brw_tracked_state gen7_ds_state;
extern const struct brw_tracked_state gen7_gs_state;
extern const struct brw_tracked_state gen7_tcs_push_constants;
extern const struct brw_tracked_state gen7_hs_state;
extern const struct brw_tracked_state gen7_l3_state;
extern const struct brw_tracked_state gen7_ps_state;
extern const struct brw_tracked_state gen7_push_constant_space;
extern const struct brw_tracked_state gen7_sbe_state;
extern const struct brw_tracked_state gen7_sf_clip_viewport;
extern const struct brw_tracked_state gen7_sf_state;
extern const struct brw_tracked_state gen7_sol_state;
extern const struct brw_tracked_state gen7_te_state;
extern const struct brw_tracked_state gen7_tes_push_constants;
extern const struct brw_tracked_state gen7_urb;
extern const struct brw_tracked_state gen7_vs_state;
extern const struct brw_tracked_state gen7_wm_state;
extern const struct brw_tracked_state gen7_hw_binding_tables;
extern const struct brw_tracked_state haswell_cut_index;
extern const struct brw_tracked_state gen8_blend_state;
extern const struct brw_tracked_state gen8_disable_stages;
extern const struct brw_tracked_state gen8_ds_state;
extern const struct brw_tracked_state gen8_gs_state;
extern const struct brw_tracked_state gen8_hs_state;
extern const struct brw_tracked_state gen8_index_buffer;
extern const struct brw_tracked_state gen8_multisample_state;
extern const struct brw_tracked_state gen8_pma_fix;
extern const struct brw_tracked_state gen8_ps_blend;
extern const struct brw_tracked_state gen8_ps_extra;
extern const struct brw_tracked_state gen8_ps_state;
extern const struct brw_tracked_state gen8_wm_depth_stencil;
extern const struct brw_tracked_state gen8_wm_state;
extern const struct brw_tracked_state gen8_raster_state;
extern const struct brw_tracked_state gen8_sbe_state;
extern const struct brw_tracked_state gen8_sf_state;
extern const struct brw_tracked_state gen8_sf_clip_viewport;
extern const struct brw_tracked_state gen8_vertices;
extern const struct brw_tracked_state gen8_vf_topology;
extern const struct brw_tracked_state gen8_vs_state;
extern const struct brw_tracked_state brw_cs_work_groups_surface;

static inline bool
brw_state_dirty(const struct brw_context *brw,
                GLuint mesa_flags, uint64_t brw_flags)
{
   return ((brw->NewGLState & mesa_flags) |
           (brw->ctx.NewDriverState & brw_flags)) != 0;
}

/* brw_binding_tables.c */
void brw_upload_binding_table(struct brw_context *brw,
                              uint32_t packet_name,
                              const struct brw_stage_prog_data *prog_data,
                              struct brw_stage_state *stage_state);

/* brw_misc_state.c */
void brw_upload_invariant_state(struct brw_context *brw);
uint32_t
brw_depthbuffer_format(struct brw_context *brw);

void brw_upload_state_base_address(struct brw_context *brw);

/* gen8_depth_state.c */
void gen8_write_pma_stall_bits(struct brw_context *brw,
                               uint32_t pma_stall_bits);

/***********************************************************************
 * brw_state.c
 */
void brw_upload_render_state(struct brw_context *brw);
void brw_render_state_finished(struct brw_context *brw);
void brw_upload_compute_state(struct brw_context *brw);
void brw_compute_state_finished(struct brw_context *brw);
void brw_init_state(struct brw_context *brw);
void brw_destroy_state(struct brw_context *brw);
void brw_emit_select_pipeline(struct brw_context *brw,
                              enum brw_pipeline pipeline);

static inline void
brw_select_pipeline(struct brw_context *brw, enum brw_pipeline pipeline)
{
   if (unlikely(brw->last_pipeline != pipeline)) {
      assert(pipeline < BRW_NUM_PIPELINES);
      brw_emit_select_pipeline(brw, pipeline);
      brw->last_pipeline = pipeline;
   }
}

/***********************************************************************
 * brw_state_cache.c
 */

void brw_upload_cache(struct brw_cache *cache,
		      enum brw_cache_id cache_id,
		      const void *key,
		      GLuint key_sz,
		      const void *data,
		      GLuint data_sz,
		      const void *aux,
		      GLuint aux_sz,
		      uint32_t *out_offset, void *out_aux);

bool brw_search_cache(struct brw_cache *cache,
		      enum brw_cache_id cache_id,
		      const void *key,
		      GLuint key_size,
		      uint32_t *inout_offset, void *inout_aux);
void brw_state_cache_check_size( struct brw_context *brw );

void brw_init_caches( struct brw_context *brw );
void brw_destroy_caches( struct brw_context *brw );

/***********************************************************************
 * brw_state_batch.c
 */
#define BRW_BATCH_STRUCT(brw, s) \
   intel_batchbuffer_data(brw, (s), sizeof(*(s)), RENDER_RING)

void *__brw_state_batch(struct brw_context *brw,
                        enum aub_state_struct_type type,
                        int size,
                        int alignment,
                        int index,
                        uint32_t *out_offset);
#define brw_state_batch(brw, type, size, alignment, out_offset) \
   __brw_state_batch(brw, type, size, alignment, 0, out_offset)

/* brw_wm_surface_state.c */
void gen4_init_vtable_surface_functions(struct brw_context *brw);
uint32_t brw_get_surface_tiling_bits(uint32_t tiling);
uint32_t brw_get_surface_num_multisamples(unsigned num_samples);

uint32_t brw_format_for_mesa_format(mesa_format mesa_format);

GLuint translate_tex_target(GLenum target);

GLuint translate_tex_format(struct brw_context *brw,
                            mesa_format mesa_format,
			    GLenum srgb_decode);

int brw_get_texture_swizzle(const struct gl_context *ctx,
                            const struct gl_texture_object *t);

void brw_emit_buffer_surface_state(struct brw_context *brw,
                                   uint32_t *out_offset,
                                   drm_intel_bo *bo,
                                   unsigned buffer_offset,
                                   unsigned surface_format,
                                   unsigned buffer_size,
                                   unsigned pitch,
                                   bool rw);

void brw_update_texture_surface(struct gl_context *ctx,
                                unsigned unit, uint32_t *surf_offset,
                                bool for_gather, uint32_t plane);

uint32_t brw_update_renderbuffer_surface(struct brw_context *brw,
                                         struct gl_renderbuffer *rb,
                                         uint32_t flags, unsigned unit,
                                         uint32_t surf_index);

void brw_update_renderbuffer_surfaces(struct brw_context *brw,
                                      const struct gl_framebuffer *fb,
                                      uint32_t render_target_start,
                                      uint32_t *surf_offset);

/* gen7_wm_surface_state.c */
void gen7_check_surface_setup(uint32_t *surf, bool is_render_target);
void gen7_init_vtable_surface_functions(struct brw_context *brw);

/* gen8_ps_state.c */
void gen8_upload_ps_state(struct brw_context *brw,
                          const struct brw_stage_state *stage_state,
                          const struct brw_wm_prog_data *prog_data,
                          uint32_t fast_clear_op);

void gen8_upload_ps_extra(struct brw_context *brw,
                          const struct brw_wm_prog_data *prog_data);

/* gen7_sol_state.c */
void gen7_upload_3dstate_so_decl_list(struct brw_context *brw,
                                      const struct brw_vue_map *vue_map);
void gen8_upload_3dstate_so_buffers(struct brw_context *brw);

/* gen8_surface_state.c */

void gen8_init_vtable_surface_functions(struct brw_context *brw);

/* brw_sampler_state.c */
void brw_emit_sampler_state(struct brw_context *brw,
                            uint32_t *sampler_state,
                            uint32_t batch_offset_for_sampler_state,
                            unsigned min_filter,
                            unsigned mag_filter,
                            unsigned mip_filter,
                            unsigned max_anisotropy,
                            unsigned address_rounding,
                            unsigned wrap_s,
                            unsigned wrap_t,
                            unsigned wrap_r,
                            unsigned min_lod,
                            unsigned max_lod,
                            int lod_bias,
                            unsigned shadow_function,
                            bool non_normalized_coordinates,
                            uint32_t border_color_offset);

void brw_update_sampler_state(struct brw_context *brw,
                              GLenum target, bool tex_cube_map_seamless,
                              GLfloat tex_unit_lod_bias,
                              mesa_format format, GLenum base_format,
                              bool is_integer_format,
                              const struct gl_sampler_object *sampler,
                              uint32_t *sampler_state,
                              uint32_t batch_offset_for_sampler_state);

/* gen6_wm_state.c */
void
gen6_upload_wm_state(struct brw_context *brw,
                     const struct brw_wm_prog_data *prog_data,
                     const struct brw_stage_state *stage_state,
                     bool multisampled_fbo,
                     bool dual_source_blend_enable, bool kill_enable,
                     bool color_buffer_write_enable, bool msaa_enabled,
                     bool line_stipple_enable, bool polygon_stipple_enable,
                     bool statistic_enable);

/* gen6_sf_state.c */
void
calculate_attr_overrides(const struct brw_context *brw,
                         uint16_t *attr_overrides,
                         uint32_t *point_sprite_enables,
                         uint32_t *urb_entry_read_length,
                         uint32_t *urb_entry_read_offset);

/* gen6_surface_state.c */
void gen6_init_vtable_surface_functions(struct brw_context *brw);

/* brw_vs_surface_state.c */
void
brw_upload_pull_constants(struct brw_context *brw,
                          GLbitfield64 brw_new_constbuf,
                          const struct gl_program *prog,
                          struct brw_stage_state *stage_state,
                          const struct brw_stage_prog_data *prog_data);

/* gen7_vs_state.c */
void
gen7_upload_constant_state(struct brw_context *brw,
                           const struct brw_stage_state *stage_state,
                           bool active, unsigned opcode);

void gen7_rs_control(struct brw_context *brw, int enable);

void gen7_edit_hw_binding_table_entry(struct brw_context *brw,
                                      gl_shader_stage stage,
                                      uint32_t index,
                                      uint32_t surf_offset);
void gen7_update_binding_table_from_array(struct brw_context *brw,
                                          gl_shader_stage stage,
                                          const uint32_t* binding_table,
                                          int num_surfaces);
void gen7_enable_hw_binding_tables(struct brw_context *brw);
void gen7_disable_hw_binding_tables(struct brw_context *brw);
void gen7_reset_hw_bt_pool_offsets(struct brw_context *brw);

/* brw_interpolation_map.c */
void brw_setup_vue_interpolation(struct brw_context *brw);

/* brw_clip.c */
void brw_upload_clip_prog(struct brw_context *brw);

/* brw_sf.c */
void brw_upload_sf_prog(struct brw_context *brw);

bool brw_is_drawing_points(const struct brw_context *brw);
bool brw_is_drawing_lines(const struct brw_context *brw);

/* gen7_l3_state.c */
void
gen7_restore_default_l3_config(struct brw_context *brw);

static inline bool
use_state_point_size(const struct brw_context *brw)
{
   const struct gl_context *ctx = &brw->ctx;

   /* Section 14.4 (Points) of the OpenGL 4.5 specification says:
    *
    *    "If program point size mode is enabled, the derived point size is
    *     taken from the (potentially clipped) shader built-in gl_PointSize
    *     written by:
    *
    *        * the geometry shader, if active;
    *        * the tessellation evaluation shader, if active and no
    *          geometry shader is active;
    *        * the vertex shader, otherwise
    *
    *    and clamped to the implementation-dependent point size range.  If
    *    the value written to gl_PointSize is less than or equal to zero,
    *    or if no value was written to gl_PointSize, results are undefined.
    *    If program point size mode is disabled, the derived point size is
    *    specified with the command
    *
    *       void PointSize(float size);
    *
    *    size specifies the requested size of a point.  The default value
    *    is 1.0."
    *
    * The rules for GLES come from the ES 3.2, OES_geometry_point_size, and
    * OES_tessellation_point_size specifications.  To summarize: if the last
    * stage before rasterization is a GS or TES, then use gl_PointSize from
    * the shader if written.  Otherwise, use 1.0.  If the last stage is a
    * vertex shader, use gl_PointSize, or it is undefined.
    *
    * We can combine these rules into a single condition for both APIs.
    * Using the state point size when the last shader stage doesn't write
    * gl_PointSize satisfies GL's requirements, as it's undefined.  Because
    * ES doesn't have a PointSize() command, the state point size will
    * remain 1.0, satisfying the ES default value in the GS/TES case, and
    * the VS case (1.0 works for "undefined").  Mesa sets the program point
    * mode flag to always-enabled in ES, so we can safely check that, and
    * it'll be ignored for ES.
    *
    * _NEW_PROGRAM | _NEW_POINT
    * BRW_NEW_VUE_MAP_GEOM_OUT
    */
   return (!ctx->VertexProgram.PointSizeEnabled && !ctx->Point._Attenuated) ||
          (brw->vue_map_geom_out.slots_valid & VARYING_BIT_PSIZ) == 0;
}


#ifdef __cplusplus
}
#endif

#endif
