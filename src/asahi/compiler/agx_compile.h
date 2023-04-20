/*
 * Copyright 2018-2021 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#ifndef __AGX_PUBLIC_H_
#define __AGX_PUBLIC_H_

#include "compiler/nir/nir.h"
#include "util/u_dynarray.h"

struct agx_varyings_vs {
   /* The first index used for FP16 varyings. Indices less than this are treated
    * as FP32. This may require remapping slots to guarantee.
    */
   unsigned base_index_fp16;

   /* The total number of vertex shader indices output. Must be at least
    * base_index_fp16.
    */
   unsigned nr_index;

   /* If the slot is written, this is the base index that the first component
    * of the slot is written to.  The next components are found in the next
    * indices. If less than base_index_fp16, this is a 32-bit slot (with 4
    * indices for the 4 components), else this is a 16-bit slot (with 2
    * indices for the 4 components). This must be less than nr_index.
    *
    * If the slot is not written, this must be ~0.
    */
   unsigned slots[VARYING_SLOT_MAX];
};

/* Conservative bound, * 4 due to offsets (TODO: maybe worth eliminating
 * coefficient register aliasing?)
 */
#define AGX_MAX_CF_BINDINGS (VARYING_SLOT_MAX * 4)

struct agx_varyings_fs {
   /* Number of coefficient registers used */
   unsigned nr_cf;

   /* Number of coefficient register bindings */
   unsigned nr_bindings;

   /* Whether gl_FragCoord.z is read */
   bool reads_z;

   /* Coefficient register bindings */
   struct {
      /* Base coefficient register */
      unsigned cf_base;

      /* Slot being bound */
      gl_varying_slot slot;

      /* First component bound.
       *
       * Must be 2 (Z) or 3 (W) if slot == VARYING_SLOT_POS.
       */
      unsigned offset : 2;

      /* Number of components bound */
      unsigned count : 3;

      /* Is smooth shading enabled? If false, flat shading is used */
      bool smooth : 1;

      /* Perspective correct interpolation */
      bool perspective : 1;
   } bindings[AGX_MAX_CF_BINDINGS];
};

union agx_varyings {
   struct agx_varyings_vs vs;
   struct agx_varyings_fs fs;
};

struct agx_shader_info {
   union agx_varyings varyings;

   /* Number of uniforms */
   unsigned push_count;

   /* Does the shader have a preamble? If so, it is at offset preamble_offset.
    * The main shader is at offset main_offset. The preamble is executed first.
    */
   bool has_preamble;
   unsigned preamble_offset, main_offset;

   /* Does the shader read the tilebuffer? */
   bool reads_tib;

   /* Does the shader write point size? */
   bool writes_psiz;

   /* Does the shader control the sample mask? */
   bool writes_sample_mask;

   /* Depth layout, never equal to NONE */
   enum gl_frag_depth_layout depth_layout;

   /* Is colour output omitted? */
   bool no_colour_output;

   /* Shader is incompatible with triangle merging */
   bool disable_tri_merging;

   /* Shader needs a dummy sampler (for txf reads) */
   bool needs_dummy_sampler;

   /* Number of bindful textures used */
   unsigned nr_bindful_textures;

   /* Number of 16-bit registers used by the main shader and preamble
    * respectively.
    */
   unsigned nr_gprs, nr_preamble_gprs;
};

#define AGX_MAX_RTS (8)

enum agx_format {
   AGX_FORMAT_I8 = 0,
   AGX_FORMAT_I16 = 1,
   AGX_FORMAT_I32 = 2,
   AGX_FORMAT_F16 = 3,
   AGX_FORMAT_U8NORM = 4,
   AGX_FORMAT_S8NORM = 5,
   AGX_FORMAT_U16NORM = 6,
   AGX_FORMAT_S16NORM = 7,
   AGX_FORMAT_RGB10A2 = 8,
   AGX_FORMAT_SRGBA8 = 10,
   AGX_FORMAT_RG11B10F = 12,
   AGX_FORMAT_RGB9E5 = 13,

   /* Keep last */
   AGX_NUM_FORMATS,
};

struct agx_fs_shader_key {
   /* Normally, access to the tilebuffer must be guarded by appropriate fencing
    * instructions to ensure correct results in the presence of out-of-order
    * hardware optimizations. However, specially dispatched clear shaders are
    * not subject to these conditions and can omit the wait instructions.
    *
    * Must (only) be set for special clear shaders.
    *
    * Must not be used with sample mask writes (including discards) or
    * tilebuffer loads (including blending).
    */
   bool ignore_tib_dependencies;
};

struct agx_shader_key {
   /* Number of reserved preamble slots at the start */
   unsigned reserved_preamble;

   union {
      struct agx_fs_shader_key fs;
   };
};

void agx_preprocess_nir(nir_shader *nir, bool support_lod_bias);

void agx_compile_shader_nir(nir_shader *nir, struct agx_shader_key *key,
                            struct util_debug_callback *debug,
                            struct util_dynarray *binary,
                            struct agx_shader_info *out);

static const nir_shader_compiler_options agx_nir_options = {
   .lower_fdiv = true,
   .fuse_ffma16 = true,
   .fuse_ffma32 = true,
   .lower_flrp16 = true,
   .lower_flrp32 = true,
   .lower_fpow = true,
   .lower_fmod = true,
   .lower_bitfield_extract_to_shifts = true,
   .lower_bitfield_insert_to_shifts = true,
   .lower_ifind_msb = true,
   .lower_find_lsb = true,
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   .lower_scmp = true,
   .lower_isign = true,
   .lower_fsign = true,
   .lower_iabs = true,
   .lower_fdph = true,
   .lower_ffract = true,
   .lower_ldexp = true,
   .lower_pack_half_2x16 = true,
   .lower_pack_64_2x32 = true,
   .lower_unpack_half_2x16 = true,
   .lower_extract_byte = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_cs_local_index_to_id = true,
   .has_cs_global_id = true,
   .vectorize_io = true,
   .use_interpolated_input_intrinsics = true,
   .lower_rotate = true,
   .has_fsub = true,
   .has_isub = true,
   .use_scoped_barrier = true,
   .max_unroll_iterations = 32,
   .lower_uniforms_to_ubo = true,
   .force_indirect_unrolling_sampler = true,
   .force_indirect_unrolling =
      (nir_var_shader_in | nir_var_shader_out | nir_var_function_temp),
   .lower_int64_options =
      (nir_lower_int64_options) ~(nir_lower_iadd64 | nir_lower_imul_2x32_64),
   .lower_doubles_options = nir_lower_dmod,
};

#endif
