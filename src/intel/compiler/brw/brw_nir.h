/*
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "brw_reg.h"
#include "compiler/nir/nir.h"
#include "brw_compiler.h"
#include "nir_builder.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRW_TEX_INSTR_FUSED_EU_DISABLE (1u << 30)

extern const struct nir_shader_compiler_options brw_scalar_nir_options;
struct brw_pass_tracker;

void
brw_fill_tess_info_from_shader_info(struct brw_tess_info *brw_info,
                                    const shader_info *shader_info);

int type_size_vec4(const struct glsl_type *type, bool bindless);
int type_size_dvec4(const struct glsl_type *type, bool bindless);

struct brw_mem_access_cb_data {
   const struct intel_device_info *devinfo;
};

static inline int
type_size_scalar_bytes(const struct glsl_type *type, bool bindless)
{
   return glsl_count_dword_slots(type, bindless) * 4;
}

static inline int
type_size_vec4_bytes(const struct glsl_type *type, bool bindless)
{
   return type_size_vec4(type, bindless) * 16;
}

struct brw_nir_compiler_opts {
   /* Soft floating point implementation shader */
   const nir_shader *softfp64;

   /* Whether robust image access is enabled */
   bool robust_image_access;

   /* Input vertices for TCS stage (0 means dynamic) */
   unsigned input_vertices;
};

/* UBO surface index can come in 2 flavors :
 *    - nir_intrinsic_resource_intel
 *    - anything else
 *
 * In the first case, checking that the surface index is const requires
 * checking resource_intel::src[1]. In any other case it's a simple
 * nir_src_is_const().
 *
 * This function should only be called on src[0] of load_ubo intrinsics.
 */
static inline bool
brw_nir_ubo_surface_index_is_pushable(nir_src src)
{
   nir_intrinsic_instr *intrin = nir_src_as_intrinsic(src);

   if (intrin && intrin->intrinsic == nir_intrinsic_resource_intel) {
      return (nir_intrinsic_resource_access_intel(intrin) &
              nir_resource_intel_pushable);
   }

   return nir_src_is_const(src);
}

/* This helper return the binding table index of a surface access (any
 * buffer/image/etc...). It works off the source of one of the intrinsics
 * (load_ubo, load_ssbo, store_ssbo, load_image, store_image, etc...).
 *
 * If the source is constant, then this is the binding table index. If we're
 * going through a resource_intel intel intrinsic, then we need to check
 * src[1] of that intrinsic.
 */
static inline unsigned
brw_nir_ubo_surface_index_get_bti(nir_src src)
{
   if (nir_src_is_const(src))
      return nir_src_as_uint(src);

   assert(nir_src_is_intrinsic(src));

   nir_intrinsic_instr *intrin = nir_def_as_intrinsic(src.ssa);
   if (!intrin || intrin->intrinsic != nir_intrinsic_resource_intel)
      return UINT32_MAX;

   /* In practice we could even drop this intrinsic because the bindless
    * access always operate from a base offset coming from a push constant, so
    * they can never be constant.
    */
   if (nir_intrinsic_resource_access_intel(intrin) &
       nir_resource_intel_bindless)
      return UINT32_MAX;

   if (!nir_src_is_const(intrin->src[1]))
      return UINT32_MAX;

   return nir_src_as_uint(intrin->src[1]);
}

/* Returns true if a fragment shader needs at least one render target */
static inline bool
brw_nir_fs_needs_null_rt(const struct intel_device_info *devinfo,
                         nir_shader *nir, bool alpha_to_coverage)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   /* Null-RT bit in the render target write extended descriptor is only
    * available on Gfx11+.
    */
   if (devinfo->ver < 11)
      return true;

   /* Depth/Stencil needs a valid render target even if there is no color
    * output.
    */
   if (nir->info.outputs_written & (BITFIELD_BIT(FRAG_RESULT_DEPTH) |
                                    BITFIELD_BIT(FRAG_RESULT_STENCIL) |
                                    BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)))
      return true;

   return alpha_to_coverage;
}

void brw_preprocess_nir(const struct brw_compiler *compiler,
                        nir_shader *nir,
                        const struct brw_nir_compiler_opts *opts);

bool
brw_nir_fence_shared_stores(nir_shader *shader);

void
brw_nir_link_shaders(const struct brw_compiler *compiler,
                     nir_shader *producer, nir_shader *consumer);

bool brw_nir_lower_cs_intrinsics(nir_shader *nir,
                                 const struct intel_device_info *devinfo,
                                 struct brw_cs_prog_data *prog_data);
bool brw_nir_lower_cs_subgroup_id(nir_shader *nir,
                                  const struct intel_device_info *devinfo,
                                  unsigned subgroup_id_offset);

bool brw_nir_lower_alpha_to_coverage(nir_shader *shader);
bool brw_needs_vertex_attributes_bypass(const nir_shader *shader);
void brw_nir_lower_fs_barycentrics(nir_shader *shader);

struct brw_lower_urb_cb_data {
   const struct intel_device_info *devinfo;

   /** Maximum amount of pushed data in bytes */
   unsigned max_push_bytes;

   /* If true, all access is guaranteed to be vec4 (128-bit) aligned.
    * offset and base are in units of 128-bit vec4 slots.
    *
    * If false, all access is guaranteed to be 32-bit aligned.
    * offset is in 32-bit units, but base is still in 128-bit vec4 units,
    */
   bool vec4_access;

   /** Map from VARYING_SLOT_* to a vec4 slot index */
   const int8_t *varying_to_slot;

   /** Stride in bytes between each vertex's worth of per-vertex varyings */
   unsigned per_vertex_stride;

   /** Do we need to use dynamic TES input bases (intel_nir_tess_field)? */
   bool dynamic_tes;

   /** Static offsets and sizes (in slots) for TES inputs */
   int tes_builtins_slot_offset;
   int tes_per_patch_slots;

   /* Offset in bytes to the start of the per-vertex section */
   uint32_t per_vertex_offset;

   /* Offset in bytes to the start of the per-primitive section */
   uint32_t per_primitive_offset;

   /* Stride in bytes between successive primitives */
   uint32_t per_primitive_stride;

   /** Stride in bytes between PrimitiveIndices[] array elements */
   uint32_t per_primitive_indices_stride;

   /**
    * Map from VARYING_SLOT_* to the offset in bytes within the
    * per-primitive section of the MUE.
    */
   const int *per_primitive_byte_offsets;
};

bool brw_nir_lower_inputs_to_urb_intrinsics(nir_shader *, const struct brw_lower_urb_cb_data *);

bool brw_nir_lower_outputs_to_urb_intrinsics(nir_shader *, const struct brw_lower_urb_cb_data *);
bool brw_nir_lower_deferred_urb_writes(nir_shader *nir,
                                       const struct intel_device_info *devinfo,
                                       const struct intel_vue_map *vue_map,
                                       unsigned extra_urb_slot_offset,
                                       unsigned gs_vertex_stride);

void brw_nir_opt_vectorize_urb(struct brw_pass_tracker *pt);

void brw_nir_lower_vs_inputs(nir_shader *nir);
void brw_nir_lower_gs_inputs(nir_shader *nir,
                             const struct intel_device_info *devinfo,
                             const struct intel_vue_map *vue_map,
                             unsigned *out_urb_read_length);
void brw_nir_lower_tes_inputs(nir_shader *nir,
                              const struct intel_device_info *devinfo,
                              const struct intel_vue_map *vue);
void brw_nir_lower_fs_inputs(nir_shader *nir,
                             const struct intel_device_info *devinfo,
                             const struct brw_fs_prog_key *key);
void brw_nir_lower_vue_outputs(nir_shader *nir);
void brw_nir_lower_tcs_inputs(nir_shader *nir,
                              const struct intel_device_info *devinfo,
                              const struct intel_vue_map *vue);
void brw_nir_lower_tcs_outputs(nir_shader *nir,
                               const struct intel_device_info *devinfo,
                               const struct intel_vue_map *vue,
                               enum tess_primitive_mode tes_primitive_mode);
void brw_nir_lower_mesh_outputs(nir_shader *nir,
                                const struct brw_mue_map *map);
void brw_nir_lower_fs_outputs(nir_shader *nir);
bool brw_nir_lower_fs_load_output(nir_shader *shader,
                                  const struct brw_fs_prog_key *key);

bool brw_nir_lower_frag_coord_z(nir_shader *nir,
                                const struct intel_device_info *devinfo);

bool brw_nir_lower_cmat(nir_shader *nir, unsigned subgroup_size);

struct brw_nir_lower_storage_image_opts {
   bool lower_loads;
   bool lower_stores;
   bool lower_stores_64bit;
   bool lower_loads_without_formats;
};

bool brw_nir_lower_storage_image(nir_shader *nir,
                                 const struct brw_compiler *compiler,
                                 const struct brw_nir_lower_storage_image_opts *opts);

bool brw_nir_lower_texel_address(nir_shader *shader,
                                 const struct intel_device_info *devinfo,
                                 enum isl_tiling tiling);

bool brw_nir_lower_mcs_fetch(nir_shader *shader,
                             const struct intel_device_info *devinfo);

bool brw_nir_texture_backend_opcode(nir_shader *shader,
                                    const struct intel_device_info *devinfo);

bool brw_nir_pre_lower_texture(nir_shader *nir,
                               const struct intel_device_info *devinfo);

bool brw_nir_lower_texture(nir_shader *nir);

bool brw_nir_lower_sample_index_in_coord(nir_shader *nir);

bool brw_nir_lower_immediate_offsets(nir_shader *shader);

bool brw_nir_lower_mem_access_bit_sizes(nir_shader *shader,
                                        const struct
                                        intel_device_info *devinfo);

bool brw_nir_lower_simd(nir_shader *nir);

void brw_postprocess_nir_opts(struct brw_pass_tracker *pt);

void brw_postprocess_nir_out_of_ssa(struct brw_pass_tracker *pt,
                                    bool debug_enabled);

static inline void
brw_postprocess_nir(struct brw_pass_tracker *pt,
                    bool debug_enabled)
{
   brw_postprocess_nir_opts(pt);
   brw_postprocess_nir_out_of_ssa(pt, debug_enabled);
}

bool brw_nir_apply_attribute_workarounds(nir_shader *nir,
                                         const uint8_t *attrib_wa_flags);

bool brw_nir_apply_trig_workarounds(nir_shader *nir);

bool brw_nir_limit_trig_input_range_workaround(nir_shader *nir);

bool brw_nir_apply_sqrt_workarounds(nir_shader *nir);

bool brw_nir_lower_fsign(nir_shader *nir);

bool brw_nir_opt_fsat(nir_shader *);

void brw_nir_apply_key(struct brw_pass_tracker *pt,
                       const struct brw_base_prog_key *key,
                       unsigned max_subgroup_size);

unsigned brw_nir_api_subgroup_size(const nir_shader *nir,
                                   unsigned hw_subgroup_size);

enum brw_conditional_mod brw_cmod_for_nir_comparison(nir_op op);
enum lsc_opcode lsc_op_for_nir_intrinsic(const nir_intrinsic_instr *intrin);
enum brw_reg_type brw_type_for_base_type(enum glsl_base_type base_type);
enum brw_reg_type brw_type_for_nir_type(const struct intel_device_info *devinfo,
                                        nir_alu_type type);

struct brw_nir_vectorize_mem_cb_data {
   const struct intel_device_info *devinfo;
};

bool brw_nir_should_vectorize_mem(unsigned align_mul, unsigned align_offset,
                                  unsigned bit_size,
                                  unsigned num_components,
                                  int64_t hole_size,
                                  nir_intrinsic_instr *low,
                                  nir_intrinsic_instr *high,
                                  void *data);

/**
 * Gets the size of a nir_load_*_uniform_block_intel after its lowered
 * by the backend to a block load message, note that page faults can
 * happen if this is not accounted for when using these intrinsics.
 */
static inline unsigned
brw_uniform_block_size(const struct intel_device_info *devinfo,
                       unsigned num_components)
{
   /* Round up to a supported block size, or to the nearest multiple of
    * 16 components if its any larger.
    */
   return num_components > 8 ? align(num_components, 16)
      : num_components > 4 ? 8
      : !devinfo->has_lsc ? 4
      : num_components;
}

void brw_nir_optimize(struct brw_pass_tracker *pt);

bool brw_nir_move_interpolation_to_top(nir_shader *nir);
nir_def *brw_nir_load_global_const(nir_builder *b,
                                       nir_intrinsic_instr *load_uniform,
                                       nir_def *base_addr,
                                       unsigned off);

void brw_nir_quick_pressure_estimate(nir_shader *nir,
                                     const struct intel_device_info *devinfo,
                                     unsigned simd_est[3]);

const struct glsl_type *brw_nir_get_var_type(const struct nir_shader *nir,
                                             nir_variable *var);

static inline nir_variable_mode
brw_nir_no_indirect_mask(mesa_shader_stage stage)
{
   nir_variable_mode indirect_mask = (nir_variable_mode) 0;

   switch (stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_FRAGMENT:
      indirect_mask |= nir_var_shader_in;
      break;

   default:
      /* Everything else can handle indirect inputs */
      break;
   }

   if (stage != MESA_SHADER_TESS_CTRL &&
       stage != MESA_SHADER_TASK &&
       stage != MESA_SHADER_MESH)
      indirect_mask |= nir_var_shader_out;

   return indirect_mask;
}

nir_variable *
brw_nir_find_complete_variable_with_location(nir_shader *shader,
                                             nir_variable_mode mode,
                                             int location);

nir_def *
brw_nir_vertex_attribute_offset(nir_builder *b,
                                nir_def *attr_idx,
                                const struct intel_device_info *devinfo);

static inline bool
brw_nir_mesh_shader_needs_wa_18019110168(const struct intel_device_info *devinfo,
                                         nir_shader *shader)
{
   return intel_needs_workaround(devinfo, 18019110168) &&
      (shader->info.outputs_written & (VARYING_BIT_CLIP_DIST0 |
                                       VARYING_BIT_CLIP_DIST1)) &&
      (shader->info.per_primitive_outputs & ~(VARYING_BIT_PRIMITIVE_INDICES |
                                              VARYING_BIT_PRIMITIVE_COUNT));
}

static inline bool
brw_nir_fragment_shader_needs_wa_18019110168(const struct intel_device_info *devinfo,
                                             enum intel_sometimes mesh_input,
                                             nir_shader *shader)
{
   return intel_needs_workaround(devinfo, 18019110168) &&
      mesh_input != INTEL_NEVER &&
      (shader->info.per_primitive_inputs != 0 ||
       (shader->info.inputs_read & VARYING_BIT_PRIMITIVE_ID));
}

void
brw_nir_mesh_convert_attrs_prim_to_vert(struct nir_shader *nir,
                                        struct brw_compile_mesh_params *params,
                                        int *wa_mapping);

bool
brw_nir_frag_convert_attrs_prim_to_vert(struct nir_shader *nir,
                                        const int *wa_mapping);

bool
brw_nir_frag_convert_attrs_prim_to_vert_indirect(struct nir_shader *nir,
                                                 const struct intel_device_info *devinfo,
                                                 struct brw_compile_fs_params *params);

unsigned
brw_nir_pack_vs_input(nir_shader *nir, struct brw_vs_prog_data *prog_data);

bool brw_nir_opt_divergent_atomics(nir_shader *shader, enum brw_divergent_atomics_flags flags);

#ifdef __cplusplus
}
#endif
