/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nir.h"

enum pipe_format;

struct nir_to_msl_options {
   void *mem_ctx;
   uint64_t disabled_workarounds;

   /* Required to correctly declare fragment outputs. Shader may contain
    * shrinked writes which can lead to writting less components than the ones
    * the render target has. This leads to an incorrect calculation of the
    * component count for the render target formats. */
   uint8_t rts_component_count[MAX_DRAW_BUFFERS];
};

/* Assumes nir_shader_gather_info has been called beforehand. */
char *nir_to_msl(nir_shader *shader, struct nir_to_msl_options *options);

/* Call this after all API-specific lowerings. It will bring the NIR out of SSA
 * at the end */
bool msl_optimize_nir(struct nir_shader *nir);

/* Call this before all API-speicific lowerings, it will */
void msl_preprocess_nir(struct nir_shader *nir);

/* Call this before all API-specific lowerings. It will pre-process with
 * instruction workarounds based on the disabled workarounds bitmask. */
void msl_preprocess_nir_workarounds(struct nir_shader *nir,
                                    uint64_t disabled_workarounds);

enum msl_tex_access_flag {
   MSL_ACCESS_SAMPLE = 0,
   MSL_ACCESS_READ,
   MSL_ACCESS_WRITE,
   MSL_ACCESS_READ_WRITE,
};

static inline enum msl_tex_access_flag
msl_convert_access_flag(enum gl_access_qualifier qual)
{
   enum gl_access_qualifier readwrite =
      (ACCESS_NON_WRITEABLE | ACCESS_NON_READABLE);
   if ((qual & readwrite) == readwrite)
      return MSL_ACCESS_READ_WRITE;
   if (qual & ACCESS_NON_WRITEABLE)
      return MSL_ACCESS_READ;
   if (qual & ACCESS_NON_READABLE)
      return MSL_ACCESS_WRITE;
   return MSL_ACCESS_READ_WRITE;
}

bool msl_nir_fs_force_output_signedness(
   nir_shader *nir, enum pipe_format render_target_formats[MAX_DRAW_BUFFERS]);

bool msl_nir_vs_remove_point_size_write(nir_builder *b,
                                        nir_intrinsic_instr *intrin,
                                        void *data);

bool msl_nir_fs_remove_depth_write(nir_builder *b, nir_intrinsic_instr *intrin,
                                   void *data);

bool msl_lower_textures(nir_shader *s);

bool msl_lower_static_sample_mask(nir_shader *nir, uint32_t sample_mask);
bool msl_ensure_depth_write(nir_shader *nir);
bool msl_ensure_vertex_position_output(nir_shader *nir);
bool msl_nir_fs_io_types(nir_shader *nir);
bool msl_nir_vs_io_types(nir_shader *nir);
bool msl_nir_fake_guard_for_discards(struct nir_shader *nir);
bool msl_nir_lower_sample_shading(nir_shader *nir);
void msl_lower_nir_late(nir_shader *nir);

bool msl_gather_uses_per_draw_data(nir_shader *nir);

static const nir_shader_compiler_options kk_nir_options = {
   .lower_fdph = true,
   .has_fsub = true,
   .has_isub = true,
   .lower_extract_word = true,
   .lower_extract_byte = true,
   .lower_insert_word = true,
   .lower_insert_byte = true,
   .lower_fmod = true,
   .discard_is_demote = true,
   .instance_id_includes_base_index = true,
   .lower_device_index_to_zero = true,
   .lower_pack_64_2x32_split = true,
   .lower_unpack_64_2x32_split = true,
   .lower_pack_64_2x32 = true,
   .lower_pack_half_2x16 = true,
   .lower_pack_split = true,
   .lower_unpack_half_2x16 = true,
   .has_cs_global_id = true,
   .lower_fquantize2f16 = true,
   .lower_scmp = true,
   .lower_ifind_msb = true,
   .lower_ufind_msb = true,
   .lower_find_lsb = true,
   .has_uclz = true,
   .lower_mul_2x32_64 = true,
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   /* Metal does not support double. */
   .lower_doubles_options = (nir_lower_doubles_options)(~0),
   .lower_int64_options = nir_lower_ufind_msb64 | nir_lower_subgroup_shuffle64,
   .io_options = nir_io_mediump_is_32bit,
};
