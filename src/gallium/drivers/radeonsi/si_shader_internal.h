/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_SHADER_PRIVATE_H
#define SI_SHADER_PRIVATE_H

#include "si_shader.h"

#define SI_SPI_PS_INPUT_ADDR_FOR_PROLOG (       \
   S_0286D0_PERSP_SAMPLE_ENA(1) |               \
   S_0286D0_PERSP_CENTER_ENA(1) |               \
   S_0286D0_PERSP_CENTROID_ENA(1) |             \
   S_0286D0_LINEAR_SAMPLE_ENA(1) |              \
   S_0286D0_LINEAR_CENTER_ENA(1) |              \
   S_0286D0_LINEAR_CENTROID_ENA(1) |            \
   S_0286D0_FRONT_FACE_ENA(1) |                 \
   S_0286D0_ANCILLARY_ENA(1) |                  \
   S_0286D0_SAMPLE_COVERAGE_ENA(1) |            \
   S_0286D0_POS_FIXED_PT_ENA(1))

struct util_debug_callback;

struct si_shader_args {
   struct ac_shader_args ac;

   struct ac_arg const_and_shader_buffers;
   struct ac_arg samplers_and_images;

   /* For merged shaders, the per-stage descriptors for the stage other
    * than the one we're processing, used to pass them through from the
    * first stage to the second.
    */
   struct ac_arg other_const_and_shader_buffers;
   struct ac_arg other_samplers_and_images;

   struct ac_arg internal_bindings;
   struct ac_arg bindless_samplers_and_images;
   struct ac_arg small_prim_cull_info;
   struct ac_arg gs_attr_address;
   /* API VS */
   struct ac_arg vb_descriptors[5];
   /* VS state bits. See the VS_STATE_* and GS_STATE_* definitions. */
   struct ac_arg vs_state_bits;
   struct ac_arg vs_blit_inputs;

   /* API TCS & TES */
   struct ac_arg tes_offchip_addr;
   /* PS */
   struct ac_arg sample_locs[2];
   struct ac_arg alpha_reference;
   struct ac_arg color_start;
   /* CS */
   struct ac_arg block_size;
   struct ac_arg cs_user_data[2];
   struct ac_arg cs_shaderbuf[3];
   struct ac_arg cs_image[3];
   /* TS and MS */
   struct ac_arg task_ring_addr;
   struct ac_arg mesh_scratch_ring_addr;
};

struct si_nir_shader_ctx {
   struct si_shader *shader;
   struct si_shader_args args;
   struct si_temp_shader_variant_info temp_info;
   nir_shader *nir;
   nir_shader *gs_copy_shader;
   bool free_nir;
};

#define SI_NUM_LINKED_SHADERS 2

struct si_linked_shaders {
   /* Temporary si_shader for the first shader of merged shaders. */
   struct si_shader producer_shader;

   union {
      struct {
         struct si_nir_shader_ctx producer;
         struct si_nir_shader_ctx consumer;
      };
      struct si_nir_shader_ctx shader[SI_NUM_LINKED_SHADERS];
   };
};

struct nir_builder;
typedef struct nir_builder nir_builder;

struct nir_shader;
typedef struct nir_shader nir_shader;

/* si_shader.c */
bool si_is_multi_part_shader(struct si_shader *shader);
bool si_is_merged_shader(struct si_shader *shader);
enum ac_hw_stage si_select_hw_stage(const mesa_shader_stage stage, const union si_shader_key *const key,
                                    const enum amd_gfx_level gfx_level);
bool gfx10_ngg_export_prim_early(struct si_shader *shader);
unsigned si_calculate_needed_lds_size(enum amd_gfx_level gfx_level, struct si_shader *shader);

/* si_shader_args.c */
void si_init_shader_args(struct si_shader *shader, struct si_shader_args *args,
                         const shader_info *info);
void si_get_ps_prolog_args(struct si_shader_args *args,
                           const union si_shader_part_key *key);
void si_get_ps_epilog_args(struct si_shader_args *args,
                           const union si_shader_part_key *key,
                           struct ac_arg colors[MAX_DRAW_BUFFERS],
                           struct ac_arg *depth, struct ac_arg *stencil,
                           struct ac_arg *sample_mask);

struct nir_def;
typedef struct nir_def nir_def;

/* si_nir_*.c */
bool si_nir_clamp_shadow_comparison_value(nir_shader *nir);
bool si_nir_kill_outputs(nir_shader *nir, const union si_shader_key *key);
nir_def *si_nir_load_addr32_arg(struct si_screen *sscreen, struct si_shader_args *args,
                                nir_builder *b, struct ac_arg arg);
nir_def *si_nir_load_internal_binding(struct si_screen *sscreen, nir_builder *b,
                                      struct si_shader_args *args, unsigned slot,
                                      unsigned num_components);
bool si_nir_lower_abi(nir_shader *nir, struct si_shader *shader, struct si_shader_args *args);
bool si_nir_lower_color_inputs_to_sysvals(nir_shader *nir);
bool si_nir_lower_polygon_stipple(nir_shader *nir);
bool si_nir_lower_ps_color_inputs(nir_shader *nir, const union si_shader_key *key,
                                  const struct si_shader_info *info);
bool si_nir_lower_resource(nir_shader *nir, struct si_shader *shader,
                           struct si_shader_args *args);
bool si_nir_lower_vs_inputs(nir_shader *nir, struct si_shader *shader,
                            struct si_shader_args *args);
bool si_nir_mark_divergent_texture_non_uniform(struct nir_shader *nir);

/* si_shader_llvm.c */
bool si_llvm_compile_shader(struct si_screen *sscreen, struct ac_llvm_compiler *compiler,
                            struct si_shader *shader, struct si_linked_shaders *linked,
                            struct util_debug_callback *debug);
bool si_llvm_build_shader_part(struct si_screen *sscreen, mesa_shader_stage stage,
                               bool prolog, struct ac_llvm_compiler *compiler,
                               struct util_debug_callback *debug, const char *name,
                               struct si_shader_part *result);

/* si_shader_aco.c */
bool si_aco_compile_shader(struct si_shader *shader, struct si_linked_shaders *linked,
                           struct util_debug_callback *debug);
void si_aco_resolve_symbols(struct si_shader *shader, uint32_t *code_for_write,
                            const uint32_t *code_for_read, uint64_t scratch_va,
                            uint32_t const_offset);
bool si_aco_build_shader_part(struct si_screen *screen, mesa_shader_stage stage, bool prolog,
                              struct util_debug_callback *debug, const char *name,
                              struct si_shader_part *result);

/* si_shader_variant_info.c */
void si_get_shader_variant_info(struct si_shader *shader,
                                struct si_temp_shader_variant_info *temp_info, nir_shader *nir);
void si_get_late_shader_variant_info(struct si_shader *shader, struct si_shader_args *args,
                                     nir_shader *nir);
void si_set_spi_ps_input_config_for_separate_prolog(struct si_shader *shader);
void si_fixup_spi_ps_input_config(struct si_shader *shader);

#endif
