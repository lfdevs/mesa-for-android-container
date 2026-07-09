/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "poly/geometry.h"
#include "nir.h"
#include "nir_xfb_info.h"
#include "shader_enums.h"

struct nir_def *poly_load_per_vertex_input(struct nir_builder *b,
                                           nir_intrinsic_instr *intr,
                                           struct nir_def *vertex);

nir_def *poly_nir_load_vertex_id(struct nir_builder *b, nir_def *id);

bool poly_nir_lower_sw_vs(struct nir_shader *s);

bool poly_nir_lower_vs_before_gs(struct nir_shader *vs);

struct poly_gs_info {
   /* Output primitive mode for geometry shaders */
   enum mesa_prim mode;

   /* Number of words per primitive in the count buffer */
   unsigned count_words;

   /* Per-input primitive stride of the output index buffer */
   unsigned max_indices;

   /* Whether the GS includes transform feedback at a compile-time level */
   bool xfb;

   /* Whether a prefix sum is required on the count outputs. Implies xfb */
   bool prefix_sum;

   /* Whether the GS writes to a stream other than stream #0 */
   bool multistream;

   /* Shape of the rasterization draw, named by the instance ID */
   enum poly_gs_shape shape;

   /* Static topology used if shape = POLY_GS_SHAPE_STATIC_INDEXED */
   uint8_t topology[64];
};

bool poly_nir_lower_gs(struct nir_shader *gs, struct nir_shader **gs_count,
                       struct nir_shader **gs_copy, struct nir_shader **pre_gs,
                       struct poly_gs_info *info);

bool poly_nir_lower_tcs(struct nir_shader *tcs,
                        bool can_ignore_shader_out_barriers);

bool poly_nir_lower_tes(struct nir_shader *tes, bool to_hw_vs);

uint64_t poly_tcs_per_vertex_outputs(const struct nir_shader *nir);

unsigned poly_tcs_output_stride(const struct nir_shader *nir);

bool poly_nir_lower_sysvals(struct nir_shader *nir);

struct poly_passthrough_gs_key {
   /* Bit mask of outputs written by the VS/TES, to be passed through */
   uint64_t outputs;

   /* Type of each output varying written by the VS/TES */
   nir_alu_type output_types[NUM_TOTAL_VARYING_SLOTS];

   /* Number of components written for each output by the VS/TES */
   unsigned output_components[NUM_TOTAL_VARYING_SLOTS];

   /* Clip/cull sizes, implies clip/cull written in output */
   uint8_t clip_distance_array_size;
   uint8_t cull_distance_array_size;

   /* Transform feedback buffer strides */
   uint8_t xfb_stride[MAX_XFB_BUFFERS];

   /* Decomposed primitive */
   enum mesa_prim prim;

   /* Transform feedback info. Must use hk_passthrough_gs_key_size to get the
    * key size */
    nir_xfb_info xfb_info;
 };

static inline size_t
poly_passthrough_gs_key_size(uint16_t output_count)
{
   return (sizeof(struct poly_passthrough_gs_key) - sizeof(nir_xfb_info)) +
      nir_xfb_info_size(output_count);
}


void poly_nir_passthrough_gs(struct nir_builder *b, const void *key_);
