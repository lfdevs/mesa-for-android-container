/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef ACO_SHADER_INFO_H
#define ACO_SHADER_INFO_H

#include "ac_shader_args.h"
#include "amd_family.h"
#include "shader_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACO_MAX_SO_OUTPUTS 64
#define ACO_MAX_SO_BUFFERS 4
#define ACO_MAX_VERTEX_ATTRIBS 32
#define ACO_MAX_VBS 32

struct aco_vs_input_state {
   uint32_t instance_rate_inputs;
   uint32_t nontrivial_divisors;
   uint32_t post_shuffle;
   /* Having two separate fields instead of a single uint64_t makes it easier to remove attributes
    * using bitwise arithmetic.
    */
   uint32_t alpha_adjust_lo;
   uint32_t alpha_adjust_hi;

   uint32_t divisors[ACO_MAX_VERTEX_ATTRIBS];
   uint8_t formats[ACO_MAX_VERTEX_ATTRIBS];
};

struct aco_vs_prolog_info {
   struct ac_arg inputs;
   struct aco_vs_input_state state;
   unsigned num_attributes;
   uint32_t misaligned_mask;
   bool is_ngg;
   gl_shader_stage next_stage;
};

struct aco_ps_epilog_info {
   struct ac_arg inputs[8];
   struct ac_arg pc;

   uint32_t spi_shader_col_format;

   /* Bitmasks, each bit represents one of the 8 MRTs. */
   uint8_t color_is_int8;
   uint8_t color_is_int10;

   bool mrt0_is_dual_src;
};

struct aco_shader_info {
   uint8_t wave_size;
   bool is_ngg;
   bool has_ngg_culling;
   bool has_ngg_early_prim_export;
   bool image_2d_view_of_3d;
   unsigned workgroup_size;
   struct {
      bool as_es;
      bool as_ls;
      bool tcs_in_out_eq;
      uint64_t tcs_temp_only_input_mask;
      bool use_per_attribute_vb_descs;
      uint32_t input_slot_usage_mask;
      bool has_prolog;
      bool dynamic_inputs;
   } vs;
   struct {
      uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
      uint8_t num_stream_output_components[4];
      uint8_t output_streams[VARYING_SLOT_VAR31 + 1];
      unsigned vertices_out;
   } gs;
   struct {
      uint32_t num_lds_blocks;
      unsigned tess_input_vertices;
   } tcs;
   struct {
      bool as_es;
   } tes;
   struct {
      struct aco_ps_epilog_info epilog;
      bool writes_z;
      bool writes_stencil;
      bool writes_sample_mask;
      bool has_epilog;
      uint32_t num_interp;
      unsigned spi_ps_input;

      /* Used to export alpha through MRTZ for alpha-to-coverage (GFX11+). */
      bool alpha_to_coverage_via_mrtz;
   } ps;
   struct {
      uint8_t subgroup_size;
      bool uses_full_subgroups;
   } cs;

   uint32_t gfx9_gs_ring_lds_size;

   bool is_trap_handler_shader;
};

enum aco_compiler_debug_level {
   ACO_COMPILER_DEBUG_LEVEL_PERFWARN,
   ACO_COMPILER_DEBUG_LEVEL_ERROR,
};

struct aco_compiler_options {
   bool robust_buffer_access;
   bool dump_shader;
   bool dump_preoptir;
   bool record_ir;
   bool record_stats;
   bool has_ls_vgpr_init_bug;
   bool load_grid_size_from_user_sgpr;
   bool optimisations_disabled;
   uint8_t enable_mrt_output_nan_fixup;
   bool wgp_mode;
   enum radeon_family family;
   enum amd_gfx_level gfx_level;
   uint32_t address32_hi;
   struct {
      void (*func)(void *private_data, enum aco_compiler_debug_level level, const char *message);
      void *private_data;
   } debug;
};

enum aco_statistic {
   aco_statistic_hash,
   aco_statistic_instructions,
   aco_statistic_copies,
   aco_statistic_branches,
   aco_statistic_latency,
   aco_statistic_inv_throughput,
   aco_statistic_vmem_clauses,
   aco_statistic_smem_clauses,
   aco_statistic_sgpr_presched,
   aco_statistic_vgpr_presched,
   aco_num_statistics
};

#ifdef __cplusplus
}
#endif
#endif
