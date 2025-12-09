/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_SHADER_INFO_H
#define SI_SHADER_INFO_H

#include "ac_nir.h"

#define SI_NUM_INTERP     32

enum si_color_output_type {
   SI_TYPE_ANY32,
   SI_TYPE_FLOAT16,
   SI_TYPE_INT16,
   SI_TYPE_UINT16,
};

/* Shader info from initial NIR before optimizations for shader variants. */
struct si_shader_info {
   struct {
      blake3_hash source_blake3;

      bool use_aco_amd:1;
      bool writes_memory:1;
      uint8_t api_subgroup_size;

      uint8_t num_ubos;
      uint8_t num_ssbos;
      uint8_t num_images;
      uint32_t textures_used;
      uint32_t image_buffers;
      uint32_t msaa_images;

      unsigned task_payload_size;
      uint16_t workgroup_size[3];
      bool workgroup_size_variable:1;
      enum gl_derivative_group derivative_group:2;

      uint8_t xfb_stride[MAX_XFB_BUFFERS];
      uint8_t num_inlinable_uniforms:4;

      union {
         struct {
            uint8_t blit_sgprs_amd:4;
            bool window_space_position:1;
         } vs;

         struct {
            enum tess_primitive_mode _primitive_mode;
            enum gl_tess_spacing spacing;
            uint8_t tcs_vertices_out;
            bool ccw:1;
            bool point_mode:1;
         } tess;

         struct {
            enum mesa_prim output_primitive;
            enum mesa_prim input_primitive;
            uint16_t vertices_out;
            uint8_t invocations;
         } gs;

         struct {
            bool uses_discard:1;
            bool uses_fbfetch_output:1;
            bool needs_coarse_quad_helper_invocations:1;
            bool uses_sample_shading:1;
            bool early_fragment_tests:1;
            bool post_depth_coverage:1;
            bool pixel_center_integer:1;
            enum gl_frag_depth_layout depth_layout:3;
         } fs;

         struct {
            uint8_t user_data_components_amd:4;
         } cs;

         struct {
            uint16_t max_vertices_out;
            uint16_t max_primitives_out;
         } mesh;

         struct {
            bool linear_taskmesh_dispatch : 1;
         } task;
      };
   } base;

   ac_nir_tess_io_info tess_io_info;

   uint32_t options; /* bitmask of SI_PROFILE_* */

   uint8_t num_inputs;
   uint8_t num_outputs;
   uint8_t input_semantic[PIPE_MAX_SHADER_INPUTS];
   uint8_t output_semantic[PIPE_MAX_SHADER_OUTPUTS];

   uint8_t num_vs_inputs;
   uint8_t num_vbos_in_user_sgprs;
   uint16_t enabled_streamout_buffer_mask;

   uint64_t inputs_read; /* "get_unique_index" bits */
   uint64_t tcs_inputs_via_temp;
   uint64_t tcs_inputs_via_lds;

   /* For VS before {TCS, TES, GS} and TES before GS. */
   uint64_t ls_es_outputs_written;     /* "get_unique_index" bits */
   uint64_t outputs_written_before_ps; /* "get_unique_index" bits */
   uint8_t num_tess_level_vram_outputs; /* max "get_unique_index_patch" + 1*/

   uint8_t clipdist_mask;
   bool has_clip_outputs;
   bool gs_writes_stream0;

   uint16_t esgs_vertex_stride;

   /* Set 0xf or 0x0 (4 bits) per each written output.
    * ANDed with spi_shader_col_format.
    */
   unsigned colors_written_4bit;

   int constbuf0_num_slots;
   uint8_t color_attr_index[2];
   uint8_t color_interpolate[2];
   uint8_t color_interpolate_loc[2];
   uint8_t colors_read; /**< which color components are read by the FS */
   uint8_t colors_written;
   uint16_t output_color_types; /**< Each bit pair is enum si_color_output_type */
   bool color0_writes_all_cbufs; /**< gl_FragColor */
   bool reads_samplemask;   /**< does fragment shader read sample mask? */
   bool reads_tess_factors; /**< If TES reads TESSINNER or TESSOUTER */
   bool writes_z;           /**< does fragment shader write Z value? */
   /* We need both because both can be present in different conditional blocks. */
   bool output_z_equals_input_z; /**< gl_FragDepth == gl_FragCoord.z for any write */
   bool output_z_is_not_input_z; /**< gl_FragDepth != gl_FragCoord.z for any write */
   bool writes_stencil;     /**< does fragment shader write stencil value? */
   bool writes_samplemask;  /**< does fragment shader write sample mask? */
   bool writes_edgeflag;    /**< vertex shader outputs edgeflag */
   bool uses_interp_color;
   bool uses_persp_center_color;
   bool uses_persp_centroid_color;
   bool uses_persp_sample_color;
   bool uses_persp_center;
   bool uses_persp_centroid;
   bool uses_persp_sample;
   bool uses_linear_center;
   bool uses_linear_centroid;
   bool uses_linear_sample;
   bool uses_interp_at_offset;
   bool uses_interp_at_sample;
   bool uses_primid;
   bool uses_frontface;
   bool uses_invocationid;
   bool uses_thread_id[3];
   bool uses_block_id[3];
   bool uses_variable_block_size;
   bool uses_grid_size;
   bool uses_tg_size;
   bool uses_atomic_ordered_add;
   bool writes_psize;
   bool writes_primid;
   bool writes_viewport_index;
   bool writes_layer;
   bool uses_bindless_samplers;
   bool uses_bindless_images;
   bool has_divergent_loop;

   /* A flag to check if vrs2x2 can be enabled to reduce number of
    * fragment shader invocations if flat shading.
    */
   bool allow_flat_shading;

   /* Optimization: if the texture bound to this texunit has been cleared to 1,
    * then the draw can be skipped (see si_draw_vbo_skip_noop). Initially the
    * value is 0xff (undetermined) and can be later changed to 0 (= false) or
    * texunit + 1.
    */
   uint8_t writes_1_if_tex_is_1;

   /* frag coord and sample pos per component read mask. */
   uint8_t reads_frag_coord_mask;
};

/* Temporary info used during shader variant compilation that's forgotten after compilation is
 * finished.
 */
struct si_temp_shader_variant_info {
   uint8_t vs_output_param_offset[NUM_TOTAL_VARYING_SLOTS];
   bool has_non_uniform_tex_access : 1;
   bool has_shadow_comparison : 1;
};

union si_ps_input_info {
   struct {
      uint8_t semantic;
      uint8_t interpolate;
      uint8_t fp16_lo_hi_valid;
   };
   uint32_t _unused; /* this just forces 4-byte alignment */
};

/* Final shader info from fully compiled and optimized shader variants. */
struct si_shader_variant_info {
   uint32_t vs_output_ps_input_cntl[NUM_TOTAL_VARYING_SLOTS];
   union si_ps_input_info ps_inputs[SI_NUM_INTERP];
   uint8_t num_ps_inputs;
   uint8_t num_ps_per_primitive_inputs;
   uint8_t num_ps_maybe_per_primitive_inputs;
   uint8_t ps_colors_read;
   uint8_t num_input_sgprs;
   uint8_t num_input_vgprs;
   bool uses_vmem_load_other : 1; /* all other VMEM loads and atomics with return */
   bool uses_vmem_sampler_or_bvh : 1;
   bool uses_instance_id : 1;
   bool uses_base_instance : 1;
   bool uses_draw_id : 1;
   bool uses_vs_state_indexed : 1; /* VS_STATE_INDEXED */
   bool uses_gs_state_provoking_vtx_first : 1;
   bool uses_gs_state_outprim : 1;
   bool writes_z : 1;
   bool writes_stencil : 1;
   bool writes_sample_mask : 1;
   bool uses_discard : 1;
   bool uses_mesh_scratch_ring : 1;
   uint8_t nr_pos_exports;
   uint8_t nr_param_exports;
   uint8_t nr_prim_param_exports;
   uint8_t clipdist_mask;
   uint8_t culldist_mask;
   uint8_t num_streamout_vec4s;
   uint8_t max_simd_waves;
   uint8_t ngg_lds_scratch_size;
   uint16_t private_mem_vgprs;
   uint32_t ngg_lds_vertex_size; /* VS,TES: Cull+XFB, GS: GSVS size */
   uint32_t shared_size;
   ac_nir_legacy_gs_info legacy_gs;
};

#endif
