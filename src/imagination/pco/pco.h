/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_H
#define PCO_H

/**
 * \file pco.h
 *
 * \brief Main compiler interface header.
 */

#include "compiler/nir/nir.h"

/* Defines. */
#define PCO_REG_UNUSED (~0U)

/* Driver-specific forward-declarations. */
struct pvr_device_info;

/* Compiler-specific forward-declarations. */
typedef struct _pco_shader pco_shader;
typedef struct _pco_ctx pco_ctx;
typedef struct _pco_data pco_data;

pco_ctx *pco_ctx_create(const struct pvr_device_info *dev_info, void *mem_ctx);
void pco_ctx_setup_usclib(pco_ctx *ctx, const void *data, unsigned size);
void pco_ctx_update_dev_info(pco_ctx *ctx,
                             const struct pvr_device_info *dev_info);
const struct spirv_to_nir_options *pco_spirv_options(void);
const nir_shader_compiler_options *pco_nir_options(void);

void pco_preprocess_nir(pco_ctx *ctx, nir_shader *nir);
void pco_link_nir(pco_ctx *ctx,
                  nir_shader *producer,
                  nir_shader *consumer,
                  pco_data *producer_data,
                  pco_data *consumer_data);
void pco_rev_link_nir(pco_ctx *ctx, nir_shader *producer, nir_shader *consumer);
void pco_lower_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data);
void pco_postprocess_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data);

pco_shader *
pco_trans_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data, void *mem_ctx);
void pco_process_ir(pco_ctx *ctx, pco_shader *shader);
void pco_encode_ir(pco_ctx *ctx, pco_shader *shader);

pco_data *pco_shader_data(pco_shader *shader);

unsigned pco_shader_binary_size(pco_shader *shader);
const void *pco_shader_binary_data(pco_shader *shader);

void pco_validate_shader(pco_shader *shader, const char *when);

void pco_print_shader(pco_shader *shader, FILE *fp, const char *when);
void pco_print_binary(pco_shader *shader, FILE *fp, const char *when);

#include "compiler/nir/nir_builder.h"

typedef struct _pco_smp_params {
   nir_def *tex_state;
   nir_def *smp_state;

   nir_alu_type dest_type;

   enum glsl_sampler_dim sampler_dim;

   bool nncoords;
   nir_def *coords;
   nir_def *array_index;

   nir_def *proj;

   nir_def *lod_bias;
   nir_def *lod_replace;
   nir_def *lod_ddx;
   nir_def *lod_ddy;

   nir_def *addr_lo;
   nir_def *addr_hi;

   nir_def *offset;
   nir_def *ms_index;

   nir_def *write_data;

   bool sample_coeffs;
   bool sample_raw;
   unsigned sample_components;

   bool int_mode;
} pco_smp_params;
nir_intrinsic_instr *pco_emit_nir_smp(nir_builder *b, pco_smp_params *params);
#endif /* PCO_H */
