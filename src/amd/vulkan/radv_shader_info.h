/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_SHADER_INFO_H
#define RADV_SHADER_INFO_H

#include <inttypes.h>
#include <stdbool.h>

#include "util/set.h"
#include "ac_nir.h"
#include "radv_constants.h"
#include "radv_shader_args.h"

struct radv_device;
struct nir_shader;
typedef struct nir_shader nir_shader;
struct radv_shader_layout;
struct radv_shader_stage_key;
enum radv_pipeline_type;
struct radv_shader_stage;

enum radv_shader_type {
   RADV_SHADER_TYPE_DEFAULT = 0,
   RADV_SHADER_TYPE_GS_COPY,
   RADV_SHADER_TYPE_TRAP_HANDLER,
   RADV_SHADER_TYPE_RT_PROLOG,
};

struct radv_vs_output_info {
   uint8_t vs_output_param_offset[VARYING_SLOT_MAX];
   uint8_t clip_dist_mask;
   uint8_t cull_dist_mask;
   uint8_t param_exports;
   uint8_t prim_param_exports;
   bool writes_pointsize;
   bool writes_layer;
   bool writes_layer_per_primitive;
   bool writes_viewport_index;
   bool writes_viewport_index_per_primitive;
   bool writes_primitive_shading_rate;
   bool writes_primitive_shading_rate_per_primitive;
   bool export_prim_id;
   bool export_prim_id_per_primitive;
};

struct radv_streamout_info {
   uint16_t strides[MAX_SO_BUFFERS];
   uint32_t enabled_stream_buffers_mask;
};

struct radv_legacy_gs_info {
   uint32_t gs_inst_prims_in_subgroup;
   uint32_t es_verts_per_subgroup;
   uint32_t gs_prims_per_subgroup;
   uint32_t esgs_itemsize;
   uint32_t lds_size;
   uint32_t esgs_ring_size;
   uint32_t gsvs_ring_size;
};

struct gfx10_ngg_info {
   uint16_t ngg_emit_size; /* in dwords */
   uint32_t hw_max_esverts;
   uint32_t max_gsprims;
   uint32_t max_out_verts;
   uint32_t prim_amp_factor;
   uint32_t vgt_esgs_ring_itemsize;
   uint32_t esgs_ring_size;
   uint32_t lds_size;
   bool max_vert_out_per_gs_instance;
};

struct radv_shader_info {
   uint32_t workgroup_size;
   uint32_t nir_shared_size;
   uint64_t inline_push_constant_mask;
   uint32_t push_constant_size;
   uint32_t desc_set_used_mask;
   uint32_t user_data_0;
   uint32_t num_tess_patches;
   uint32_t esgs_itemsize;       /* Only for VS or TES as ES */
   uint32_t ngg_lds_vertex_size; /* VS,TES: Cull+XFB, GS: GSVS size */
   uint64_t gs_inputs_read;  /* Mask of GS inputs read (only used by linked ES) */

   struct radv_userdata_locations user_sgprs_locs;
   struct radv_vs_output_info outinfo;

   uint8_t wave_size;
   uint8_t ngg_lds_scratch_size;
   mesa_shader_stage stage : 8;
   mesa_shader_stage next_stage : 8;
   enum radv_shader_type type : 4;

   bool can_inline_all_push_constants : 1;
   bool loads_push_constants : 1;
   bool loads_dynamic_offsets : 1;
   bool uses_view_index : 1;
   bool uses_invocation_id : 1;
   bool uses_prim_id : 1;
   bool is_ngg : 1;
   bool is_ngg_passthrough : 1;
   bool has_ngg_culling : 1;
   bool has_ngg_early_prim_export : 1;
   bool has_prim_query : 1;
   bool has_xfb_query : 1;
   bool force_vrs_per_vertex : 1;
   bool inputs_linked : 1;
   bool outputs_linked : 1;
   bool merged_shader_compiled_separately : 1; /* GFX9+ */
   bool force_indirect_descriptors : 1;

   struct {
      uint64_t tcs_inputs_via_temp;
      uint64_t tcs_inputs_via_lds;
      uint32_t vb_desc_usage_mask;
      uint32_t input_slot_usage_mask;
      uint32_t num_outputs; /* For NGG streamout only */
      uint8_t num_linked_outputs;
      uint8_t num_attributes;
      bool needs_draw_id : 1;
      bool needs_instance_id : 1;
      bool as_es : 1;
      bool as_ls : 1;
      bool tcs_in_out_eq : 1;
      bool needs_base_instance : 1;
      bool use_per_attribute_vb_descs : 1;
      bool has_prolog : 1;
      bool dynamic_inputs : 1;
      bool dynamic_num_verts_per_prim : 1;
   } vs;
   struct {
      uint8_t num_components_per_stream[4];
      uint32_t vertices_in;
      uint32_t vertices_out;
      uint32_t input_prim;
      uint32_t output_prim;
      uint32_t invocations;
      uint32_t es_type; /* GFX9: VS or TES */
      uint8_t num_linked_inputs;
      bool has_pipeline_stat_query;
   } gs;
   struct {
      uint32_t tcs_vertices_out;
      uint32_t num_outputs;            /* For NGG streamout only */
      uint8_t num_linked_inputs;       /* Number of reserved per-vertex input slots in VRAM. */
      uint8_t num_linked_patch_inputs; /* Number of reserved per-patch input slots in VRAM. */
      uint8_t num_linked_outputs;
      enum tess_primitive_mode _primitive_mode : 2;
      enum gl_tess_spacing spacing : 2;
      bool as_es : 1;
      bool ccw : 1;
      bool point_mode : 1;
      bool reads_tess_factors : 1;
   } tes;
   struct {
      uint32_t input_mask;
      uint32_t input_per_primitive_mask;
      uint32_t float32_shaded_mask;
      uint32_t explicit_shaded_mask;
      uint32_t explicit_strict_shaded_mask;
      uint32_t float16_shaded_mask;
      uint32_t float16_hi_shaded_mask;
      uint32_t num_inputs;
      uint8_t input_clips_culls_mask;
      bool uses_sample_shading : 1;
      bool needs_sample_positions : 1;
      bool needs_poly_line_smooth : 1;
      bool writes_memory : 1;
      bool writes_z : 1;
      bool writes_stencil : 1;
      bool writes_sample_mask : 1;
      bool writes_mrt0_alpha : 1;
      bool mrt0_is_dual_src : 1;
      bool exports_mrtz_via_epilog : 1;
      bool has_pcoord : 1;
      bool prim_id_input : 1;
      bool viewport_index_input : 1;
      bool can_discard : 1;
      bool early_fragment_test : 1;
      bool post_depth_coverage : 1;
      uint8_t reads_frag_coord_mask;
      uint8_t reads_sample_pos_mask;
      uint8_t depth_layout;
      bool reads_sample_mask_in : 1;
      bool reads_front_face : 1;
      bool reads_sample_id : 1;
      bool reads_frag_shading_rate : 1;
      bool reads_barycentric_model : 1;
      bool reads_persp_sample : 1;
      bool reads_persp_center : 1;
      bool reads_persp_centroid : 1;
      bool reads_linear_sample : 1;
      bool reads_linear_center : 1;
      bool reads_linear_centroid : 1;
      bool reads_fully_covered : 1;
      bool reads_pixel_coord : 1;
      bool reads_layer : 1;
      bool pops : 1; /* Uses Primitive Ordered Pixel Shading (fragment shader interlock) */
      bool pops_is_per_sample : 1;
      uint32_t spi_ps_input_ena;
      uint32_t spi_ps_input_addr;
      uint32_t colors_written; /* Mask of outputs written */
      uint32_t spi_shader_col_format;
      uint32_t cb_shader_mask;
      uint8_t color0_written;
      bool load_provoking_vtx : 1;
      bool load_rasterization_prim : 1;
      bool force_sample_iter_shading_rate : 1;
      bool uses_fbfetch_output : 1;
      bool allow_flat_shading : 1;

      bool has_epilog : 1;
   } ps;
   struct {
      uint32_t block_size[3];
      bool uses_block_id[3];
      bool uses_thread_id[3];
      bool uses_grid_size : 1;
      bool uses_local_invocation_idx : 1;

      bool uses_full_subgroups : 1;
      bool linear_taskmesh_dispatch : 1;
      bool has_query : 1; /* Task shader only */

      bool regalloc_hang_bug : 1;

      uint32_t derivative_group : 2;
   } cs;
   struct {
      ac_nir_tess_io_info io_info;
      uint64_t tes_inputs_read;
      uint64_t tes_patch_inputs_read;
      uint32_t tcs_vertices_out;
      uint32_t lds_size;         /* in bytes */
      uint8_t num_linked_inputs; /* Number of reserved per-vertex input slots in LDS. */
      enum gl_tess_spacing spacing;
      bool ccw;
      bool point_mode;
      bool tes_reads_tess_factors : 1;
   } tcs;
   struct {
      enum mesa_prim output_prim : 8; /* byte-size aligned */
      bool needs_ms_scratch_ring;
      bool has_task; /* If mesh shader is used together with a task shader. */
      bool has_query;
   } ms;

   struct radv_streamout_info so;

   union {
      struct radv_legacy_gs_info legacy_gs_info;
      struct gfx10_ngg_info ngg_info;
   };

   /* Precomputed register values. */
   struct {
      uint32_t pgm_lo;
      uint32_t pgm_rsrc1;
      uint32_t pgm_rsrc2;
      uint32_t pgm_rsrc3;

      union {
         struct {
            uint32_t spi_shader_late_alloc_vs;
            uint32_t spi_shader_pgm_rsrc3_vs;
            uint32_t vgt_reuse_off;
         } vs;

         struct {
            uint32_t vgt_esgs_ring_itemsize;
            uint32_t vgt_gs_instance_cnt;
            uint32_t vgt_gs_max_prims_per_subgroup;
            uint32_t vgt_gs_vert_itemsize[4];
            uint32_t vgt_gsvs_ring_itemsize;
            uint32_t vgt_gsvs_ring_offset[3];
         } gs;

         struct {
            uint32_t ge_cntl; /* Not fully precomputed. */
            uint32_t ge_max_output_per_subgroup;
            uint32_t ge_ngg_subgrp_cntl;
            uint32_t spi_shader_idx_format;
            uint32_t vgt_primitiveid_en;
            struct {
               uint32_t spi_shader_gs_meshlet_dim;
               uint32_t spi_shader_gs_meshlet_exp_alloc;
               uint32_t spi_shader_gs_meshlet_ctrl; /* GFX12+ */
            } ms;
         } ngg;

         struct {
            uint32_t db_shader_control;
            uint32_t pa_sc_shader_control;
            uint32_t spi_ps_in_control;
            uint32_t spi_shader_z_format;
            uint32_t spi_gs_out_config_ps;
            uint32_t pa_sc_hisz_control;
         } ps;

         struct {
            uint32_t compute_num_thread_x;
            uint32_t compute_num_thread_y;
            uint32_t compute_num_thread_z;
            uint32_t compute_resource_limits;
         } cs;
      };

      /* Common registers between stages. */
      uint32_t vgt_gs_max_vert_out;
      uint32_t vgt_gs_onchip_cntl;
      uint32_t spi_shader_pgm_rsrc3_gs;
      uint32_t spi_shader_pgm_rsrc4_gs;
      uint32_t ge_pc_alloc;
      uint32_t pa_cl_vs_out_cntl;
      uint32_t spi_vs_out_config;
      uint32_t spi_shader_pos_format;
      uint32_t vgt_gs_instance_cnt;
   } regs;
};

void radv_nir_shader_info_init(mesa_shader_stage stage, mesa_shader_stage next_stage, struct radv_shader_info *info);

void radv_nir_shader_info_pass(struct radv_device *device, const struct nir_shader *nir,
                               const struct radv_shader_layout *layout, const struct radv_shader_stage_key *stage_key,
                               const struct radv_graphics_state_key *gfx_state,
                               const enum radv_pipeline_type pipeline_type, bool consider_force_vrs,
                               struct radv_shader_info *info);

void radv_get_legacy_gs_info(const struct radv_device *device, struct radv_shader_info *es_info, struct radv_shader_info *gs_info);

void gfx10_get_ngg_info(const struct radv_device *device, struct radv_shader_info *es_info,
                        struct radv_shader_info *gs_info, struct gfx10_ngg_info *out);

void gfx10_ngg_set_esgs_ring_itemsize(const struct radv_device *device, struct radv_shader_info *es_info,
                                      struct radv_shader_info *gs_info, struct gfx10_ngg_info *out);

void radv_nir_shader_info_link(struct radv_device *device, const struct radv_graphics_state_key *gfx_state,
                               struct radv_shader_stage *stages);

enum ac_hw_stage radv_select_hw_stage(const struct radv_shader_info *const info, const enum amd_gfx_level gfx_level);

uint64_t radv_gather_unlinked_io_mask(const uint64_t nir_mask);

uint64_t radv_gather_unlinked_patch_io_mask(const uint64_t nir_io_mask, const uint32_t nir_patch_io_mask);

#endif /* RADV_SHADER_INFO_H */
