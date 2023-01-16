/*
 * Copyright © 2021 Valve Corporation
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
 *
 */


#ifndef AC_NIR_H
#define AC_NIR_H

#include "nir.h"
#include "nir_builder.h"
#include "ac_shader_args.h"
#include "ac_shader_util.h"
#include "amd_family.h"
#include "pipe/p_state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   /* SPI_PS_INPUT_CNTL_i.OFFSET[0:4] */
   AC_EXP_PARAM_OFFSET_0 = 0,
   AC_EXP_PARAM_OFFSET_31 = 31,
   /* SPI_PS_INPUT_CNTL_i.DEFAULT_VAL[0:1] */
   AC_EXP_PARAM_DEFAULT_VAL_0000 = 64,
   AC_EXP_PARAM_DEFAULT_VAL_0001,
   AC_EXP_PARAM_DEFAULT_VAL_1110,
   AC_EXP_PARAM_DEFAULT_VAL_1111,
   AC_EXP_PARAM_UNDEFINED = 255, /* deprecated, use AC_EXP_PARAM_DEFAULT_VAL_0000 instead */
};

/* Maps I/O semantics to the actual location used by the lowering pass. */
typedef unsigned (*ac_nir_map_io_driver_location)(unsigned semantic);

/* Forward declaration of nir_builder so we don't have to include nir_builder.h here */
struct nir_builder;
typedef struct nir_builder nir_builder;

/* Executed by ac_nir_cull when the current primitive is accepted. */
typedef void (*ac_nir_cull_accepted)(nir_builder *b, void *state);

nir_ssa_def *
ac_nir_load_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg);

nir_ssa_def *
ac_nir_unpack_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg,
                  unsigned rshift, unsigned bitwidth);

nir_ssa_def *
ac_nir_calc_io_offset(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      nir_ssa_def *base_stride,
                      unsigned component_stride,
                      ac_nir_map_io_driver_location map_io);

bool ac_nir_optimize_outputs(nir_shader *nir, bool sprite_tex_disallowed,
                             int8_t slot_remap[NUM_TOTAL_VARYING_SLOTS],
                             uint8_t param_export_index[NUM_TOTAL_VARYING_SLOTS]);

void
ac_nir_lower_ls_outputs_to_mem(nir_shader *ls,
                               ac_nir_map_io_driver_location map,
                               bool tcs_in_out_eq,
                               uint64_t tcs_temp_only_inputs);

void
ac_nir_lower_hs_inputs_to_mem(nir_shader *shader,
                              ac_nir_map_io_driver_location map,
                              bool tcs_in_out_eq);

void
ac_nir_lower_hs_outputs_to_mem(nir_shader *shader,
                               ac_nir_map_io_driver_location map,
                               enum amd_gfx_level gfx_level,
                               bool tes_reads_tessfactors,
                               uint64_t tes_inputs_read,
                               uint64_t tes_patch_inputs_read,
                               unsigned num_reserved_tcs_outputs,
                               unsigned num_reserved_tcs_patch_outputs,
                               unsigned wave_size,
                               bool no_inputs_in_lds,
                               bool pass_tessfactors_by_reg,
                               bool emit_tess_factor_write);

void
ac_nir_lower_tes_inputs_to_mem(nir_shader *shader,
                               ac_nir_map_io_driver_location map);

void
ac_nir_lower_es_outputs_to_mem(nir_shader *shader,
                               ac_nir_map_io_driver_location map,
                               enum amd_gfx_level gfx_level,
                               unsigned esgs_itemsize);

void
ac_nir_lower_gs_inputs_to_mem(nir_shader *shader,
                              ac_nir_map_io_driver_location map,
                              enum amd_gfx_level gfx_level,
                              bool triangle_strip_adjacency_fix);

bool
ac_nir_lower_indirect_derefs(nir_shader *shader,
                             enum amd_gfx_level gfx_level);

typedef struct {
   enum radeon_family family;
   enum amd_gfx_level gfx_level;

   unsigned max_workgroup_size;
   unsigned wave_size;
   const uint8_t *vs_output_param_offset; /* GFX11+ */
   bool can_cull;
   bool disable_streamout;
   bool has_gen_prim_query;
   bool has_xfb_prim_query;

   /* VS */
   unsigned num_vertices_per_primitive;
   bool early_prim_export;
   bool passthrough;
   bool use_edgeflags;
   int primitive_id_location;
   uint32_t instance_rate_inputs;
   uint32_t clipdist_enable_mask;
   uint32_t user_clip_plane_enable_mask;

   /* GS */
   unsigned gs_out_vtx_bytes;
} ac_nir_lower_ngg_options;

void
ac_nir_lower_ngg_nogs(nir_shader *shader, const ac_nir_lower_ngg_options *options);

void
ac_nir_lower_ngg_gs(nir_shader *shader, const ac_nir_lower_ngg_options *options);

void
ac_nir_lower_ngg_ms(nir_shader *shader,
                    bool *out_needs_scratch_ring,
                    unsigned wave_size,
                    bool multiview);

void
ac_nir_apply_first_task_to_task_shader(nir_shader *shader);

void
ac_nir_lower_task_outputs_to_mem(nir_shader *shader,
                                 unsigned task_payload_entry_bytes,
                                 unsigned task_num_entries);

void
ac_nir_lower_mesh_inputs_to_mem(nir_shader *shader,
                                unsigned task_payload_entry_bytes,
                                unsigned task_num_entries);

nir_ssa_def *
ac_nir_cull_primitive(nir_builder *b,
                      nir_ssa_def *initially_accepted,
                      nir_ssa_def *pos[3][4],
                      unsigned num_vertices,
                      ac_nir_cull_accepted accept_func,
                      void *state);

bool
ac_nir_lower_global_access(nir_shader *shader);

bool ac_nir_lower_resinfo(nir_shader *nir, enum amd_gfx_level gfx_level);

typedef struct ac_nir_gs_output_info {
   const uint8_t *streams;
   const uint8_t *streams_16bit_lo;
   const uint8_t *streams_16bit_hi;

   const uint8_t *usage_mask;
   const uint8_t *usage_mask_16bit_lo;
   const uint8_t *usage_mask_16bit_hi;

   /* type for each 16bit slot component */
   nir_alu_type (*types_16bit_lo)[4];
   nir_alu_type (*types_16bit_hi)[4];

   /* map varying slot to driver location */
   const uint8_t *slot_to_location;
   const uint8_t *slot_to_location_16bit;
} ac_nir_gs_output_info;

nir_shader *
ac_nir_create_gs_copy_shader(const nir_shader *gs_nir,
                             bool disable_streamout,
                             ac_nir_gs_output_info *output_info);

void
ac_nir_lower_legacy_vs(nir_shader *nir, int primitive_id_location, bool disable_streamout);

bool
ac_nir_gs_shader_query(nir_builder *b,
                       bool has_gen_prim_query,
                       bool has_pipeline_stats_query,
                       unsigned num_vertices_per_primitive,
                       unsigned wave_size,
                       nir_ssa_def *vertex_count[4],
                       nir_ssa_def *primitive_count[4]);

void
ac_nir_lower_legacy_gs(nir_shader *nir,
                       bool has_gen_prim_query,
                       bool has_pipeline_stats_query,
                       ac_nir_gs_output_info *output_info);

#ifdef __cplusplus
}
#endif

#endif /* AC_NIR_H */
