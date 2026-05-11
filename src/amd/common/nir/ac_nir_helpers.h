/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */


#ifndef AC_NIR_HELPERS_H
#define AC_NIR_HELPERS_H

#include "ac_nir.h"
#include "nir_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_NIR_STORE_IO(b, store_val, const_offset, write_mask, hi_16bit, func, ...) \
   do { \
      if ((store_val)->bit_size >= 32) { \
         const unsigned store_write_mask = (write_mask); \
         const unsigned store_const_offset = (const_offset); \
         func((b), (store_val), __VA_ARGS__); \
      } else { \
         u_foreach_bit(c, (write_mask)) { \
            const unsigned store_write_mask = 1; \
            const unsigned store_const_offset = (const_offset) + c * 4 + ((hi_16bit) ? 2 : 0); \
            nir_def *store_component = nir_channel(b, (store_val), c); \
            func((b), store_component, __VA_ARGS__); \
         } \
      } \
   } while (0)

#define AC_NIR_LOAD_IO(load, b, num_components, bit_size, hi_16bit, func, ...) \
   do { \
      const unsigned load_bit_size = MAX2(32, (bit_size)); \
      (load) = func((b), (num_components), load_bit_size, __VA_ARGS__); \
      if ((bit_size) < load_bit_size) { \
         if ((hi_16bit)) { \
            (load) = nir_unpack_32_2x16_split_y(b, load); \
         } else { \
            (load) = nir_unpack_32_2x16_split_x(b, load); \
         } \
      } \
   } while (0)

typedef struct
{
   /* GS output stream index, 2 bit per component */
   uint8_t stream;
   /* Bitmask of components used: 4 bits per slot, 1 bit per component. */
   uint8_t components_mask : 4;
   /* Bitmask of components that should be clamped. */
   uint8_t clamp_components_mask : 4;
   /* Bitmask of components that are used as varying, 1 bit per component. */
   uint8_t as_varying_mask : 4;
   /* Bitmask of components that are used as sysval, 1 bit per component. */
   uint8_t as_sysval_mask : 4;
   /* Prefix sum over all component masks. Used by the GS outputs in LDS for NGG GS.
    * This is set even if components_mask is 0, in which case it's the offset after the last output.
    */
   uint16_t packed_slot_gs_out_offset : 12;
   /* Prefix sum over all component masks. Used by XFB outputs in LDS for NGG VS and TES.
    * This is set even if xfb_components_mask is 0, in which case it's the offset after the last output.
    * For NGG GS, it's equal to packed_slot_gs_out_offset because NGG GS has all outputs in LDS.
    */
   uint16_t packed_slot_xfb_lds_offset : 12;
   /* Bitmask of components written by XFB: 4 bits per slot, 1 bit per component.
    * For NGG GS, it's equal to components_mask because NGG GS has all outputs in LDS.
    */
   uint8_t xfb_lds_components_mask : 4;
   /* Bitmask of components that store a constant value. */
   uint8_t const_mask : 4;
   /* Bitmask of components that store a non-constant value at least once. Used only during gathering. */
   uint8_t nonconst_mask : 4;
} ac_nir_prerast_per_output_info;

typedef struct
{
   /* Low and high 16 bits are packed into 32 bits. */
   nir_def *outputs[NUM_TOTAL_VARYING_SLOTS][4];
   uint32_t const_values[NUM_TOTAL_VARYING_SLOTS][4];

   nir_alu_type types[NUM_TOTAL_VARYING_SLOTS][4];
   ac_nir_prerast_per_output_info infos[NUM_TOTAL_VARYING_SLOTS];

   /* The size of all components, packed. */
   uint16_t total_packed_gs_out_size;
   uint16_t total_packed_xfb_lds_size;
} ac_nir_prerast_out;

typedef struct {
   nir_def *num_repacked_invocations;
   nir_def *repacked_invocation_index;
} ac_nir_wg_repack_result;

/* Maps I/O semantics to the actual location used by the lowering pass. */
typedef unsigned (*ac_nir_map_io_driver_location)(unsigned semantic);

/* Forward declaration of nir_builder so we don't have to include nir_builder.h here */
struct nir_builder;
typedef struct nir_builder nir_builder;

struct nir_xfb_info;
typedef struct nir_xfb_info nir_xfb_info;

/* Executed by ac_nir_cull when the current primitive is accepted. */
typedef void (*ac_nir_cull_accepted)(nir_builder *b, void *state);

nir_def *
ac_nir_unpack_value(nir_builder *b, nir_def *value, unsigned rshift, unsigned bitwidth);

void
ac_nir_store_var_components(nir_builder *b, nir_variable *var, nir_def *value,
                            unsigned component, unsigned writemask);

void
ac_nir_gather_prerast_store_output_info(nir_builder *b, nir_intrinsic_instr *intrin,
                                        ac_nir_prerast_out *out, bool gather_values);

void
ac_nir_export_primitive(nir_builder *b, nir_def *prim, nir_def *row);

void
ac_nir_export_position(nir_builder *b,
                       enum amd_gfx_level gfx_level,
                       uint32_t export_clipdist_mask,
                       bool dont_export_cull_distances,
                       bool write_pos_to_clipvertex,
                       bool no_param_export,
                       bool force_vrs,
                       uint64_t outputs_written,
                       ac_nir_prerast_out *out,
                       nir_def *row);

void
ac_nir_export_parameters(nir_builder *b,
                         const uint8_t *param_offsets,
                         uint64_t outputs_written,
                         uint16_t outputs_written_16bit,
                         ac_nir_prerast_out *out);

void
ac_nir_store_parameters_to_attr_ring(nir_builder *b,
                                     const uint8_t *param_offsets,
                                     const uint64_t outputs_written,
                                     const uint16_t outputs_written_16bit,
                                     ac_nir_prerast_out *out,
                                     nir_def *num_export_threads_in_wave);

nir_def *
ac_nir_calc_io_off(nir_builder *b, unsigned component, nir_def *io_offset, nir_def *base_stride,
                   unsigned component_stride, unsigned mapped_driver_location);

unsigned
ac_nir_map_io_location(unsigned location,
                       uint64_t mask,
                       ac_nir_map_io_driver_location map_io);

nir_def *
ac_nir_cull_primitive(nir_builder *b,
                      bool skip_viewport_state_culling,
                      bool use_point_tri_intersection,
                      nir_def *initially_accepted,
                      nir_def *pos[3][4],
                      unsigned num_vertices,
                      ac_nir_cull_accepted accept_func,
                      void *state);

void
ac_nir_sleep(nir_builder *b, unsigned num_cycles);

nir_def *
ac_average_samples(nir_builder *b, nir_def **samples, unsigned num_samples);

void
ac_optimization_barrier_vgpr_array(const struct radeon_info *info, nir_builder *b,
                                   nir_def **array, unsigned num_elements,
                                   unsigned num_components);

nir_def *
ac_get_global_ids(nir_builder *b, unsigned num_components, unsigned bit_size);

void
ac_nir_emit_legacy_streamout(nir_builder *b, unsigned stream, nir_xfb_info *info, ac_nir_prerast_out *out);

bool
ac_nir_gs_shader_query(nir_builder *b,
                       bool has_gen_prim_query,
                       bool has_gs_invocations_query,
                       bool has_gs_primitives_query,
                       unsigned num_vertices_per_primitive,
                       unsigned wave_size,
                       nir_def *vertex_count[4],
                       nir_def *primitive_count[4]);

nir_def *
ac_nir_pack_ngg_prim_exp_arg(nir_builder *b, unsigned num_vertices_per_primitives,
                             nir_def *vertex_indices[3], nir_def *is_null_prim,
                             enum amd_gfx_level gfx_level);

void
ac_nir_clamp_vertex_color_outputs(nir_builder *b, ac_nir_prerast_out *out);

void
ac_nir_ngg_alloc_vertices_and_primitives(nir_builder *b,
                                         nir_def *num_vtx,
                                         nir_def *num_prim,
                                         bool fully_culled_workaround);

void
ac_nir_create_output_phis(nir_builder *b,
                          const uint64_t outputs_written,
                          const uint64_t outputs_written_16bit,
                          ac_nir_prerast_out *out);

void
ac_nir_ngg_build_streamout_buffer_info(nir_builder *b,
                                       nir_xfb_info *info,
                                       enum amd_gfx_level gfx_level,
                                       bool has_xfb_prim_query,
                                       bool use_gfx12_xfb_intrinsic,
                                       nir_def *scratch_base,
                                       nir_def *tid_in_tg,
                                       nir_def *gen_prim[4],
                                       nir_def *so_buffer_ret[4],
                                       nir_def *buffer_offsets_ret[4],
                                       nir_def *emit_prim_ret[4]);

bool
ac_nir_is_const_output(ac_nir_prerast_out *pr_out, gl_varying_slot slot, unsigned component);

nir_def *
ac_nir_get_const_output(nir_builder *b, ac_nir_prerast_out *pr_out, gl_varying_slot slot,
                        unsigned component);

void
ac_nir_store_shared_xfb(nir_builder *b, nir_def *value, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                        gl_varying_slot slot, unsigned component);

nir_def *
ac_nir_load_shared_xfb(nir_builder *b, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                       gl_varying_slot slot, unsigned component);

void
ac_nir_store_shared_gs_out(nir_builder *b, nir_def *value, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                           gl_varying_slot slot, unsigned component);

nir_def *
ac_nir_load_shared_gs_out(nir_builder *b, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                          gl_varying_slot slot, unsigned component);

void
ac_nir_ngg_build_streamout_vertex(nir_builder *b, nir_xfb_info *info,
                                  unsigned stream, nir_def *so_buffer[4],
                                  nir_def *buffer_offsets[4],
                                  unsigned vertex_index, nir_def *vtx_lds_addr,
                                  ac_nir_prerast_out *pr_out);

void
ac_nir_repack_invocations_in_workgroup(nir_builder *b, nir_def **input_bool,
                                       ac_nir_wg_repack_result *results, const unsigned num_repacks,
                                       nir_def *lds_addr_base, unsigned max_num_waves,
                                       unsigned wave_size);

void
ac_nir_compute_prerast_packed_output_info(ac_nir_prerast_out *pr_out);

unsigned
ac_nir_gs_output_component_mask_with_stream(ac_nir_prerast_per_output_info *info, unsigned stream);

nir_shader *
ac_nir_create_gs_copy_shader(const nir_shader *gs_nir, ac_nir_lower_legacy_gs_options *options,
                             ac_nir_prerast_out *out);

#ifdef __cplusplus
}
#endif

#endif /* AC_NIR_HELPERS_H */
