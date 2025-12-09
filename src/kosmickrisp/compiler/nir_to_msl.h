/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "nir.h"

enum pipe_format;

/* Assumes nir_shader_gather_info has been called beforehand. */
char *nir_to_msl(nir_shader *shader, void *mem_ctx,
                 uint64_t disabled_workarounds);

/* Call this after all API-specific lowerings. It will bring the NIR out of SSA
 * at the end */
bool msl_optimize_nir(struct nir_shader *nir);

/* Call this before all API-speicific lowerings, it will */
void msl_preprocess_nir(struct nir_shader *nir);

enum msl_tex_access_flag {
   MSL_ACCESS_SAMPLE = 0,
   MSL_ACCESS_READ,
   MSL_ACCESS_WRITE,
   MSL_ACCESS_READ_WRITE,
};

static inline enum msl_tex_access_flag
msl_convert_access_flag(enum gl_access_qualifier qual)
{
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
void msl_lower_nir_late(nir_shader *nir);
