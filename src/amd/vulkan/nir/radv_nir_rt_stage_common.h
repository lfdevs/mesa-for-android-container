/*
 * Copyright © 2025 Valve Corporation
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

/* This file contains internal helpers for RT lowering shared between different lowering implementations. */

#ifndef MESA_RADV_NIR_RT_STAGE_COMMON_H
#define MESA_RADV_NIR_RT_STAGE_COMMON_H

#include "nir/radv_nir.h"
#include "ac_nir.h"
#include "radv_pipeline_cache.h"
#include "radv_pipeline_rt.h"

typedef struct nir_parameter nir_parameter;
typedef struct glsl_type glsl_type;

/*
 *
 * Common Constants
 *
 */

/* Traversal stack size. This stack is put in LDS and experimentally 16 entries results in best
 * performance. */
#define MAX_STACK_ENTRY_COUNT 16

#define RADV_RT_SWITCH_NULL_CHECK_THRESHOLD 3

/* Minimum number of inlined shaders to use binary search to select which shader to run. */
#define INLINED_SHADER_BSEARCH_THRESHOLD 16


/*
 *
 * Shader Inlining
 *
 */

struct radv_rt_case_data {
   struct radv_device *device;
   struct radv_ray_tracing_pipeline *pipeline;
   void *param_data;
};

typedef void (*radv_get_group_info)(struct radv_ray_tracing_group *, uint32_t *, uint32_t *,
                                    struct radv_rt_case_data *);
typedef void (*radv_insert_shader_case)(nir_builder *, nir_def *, struct radv_ray_tracing_group *,
                                        struct radv_rt_case_data *);

void radv_visit_inlined_shaders(nir_builder *b, nir_def *sbt_idx, bool can_have_null_shaders,
                                struct radv_rt_case_data *data, radv_get_group_info group_info,
                                radv_insert_shader_case shader_case);

/* Transfer inline constant data from src to dst, to prepare inlining src into dst */
void radv_nir_inline_constants(nir_shader *dst, nir_shader *src);


/*
 *
 *  SBT Helpers
 *
 */

struct radv_nir_sbt_data {
   /* For inlined shaders, the index/ID of the shader to be executed.
    * For separately-compiled shaders, an address to jump execution to.
    */
   nir_def *shader_addr;
   nir_def *shader_record_ptr;
};

enum radv_nir_sbt_type {
   SBT_RAYGEN = offsetof(VkTraceRaysIndirectCommand2KHR, raygenShaderRecordAddress),
   SBT_MISS = offsetof(VkTraceRaysIndirectCommand2KHR, missShaderBindingTableAddress),
   SBT_HIT = offsetof(VkTraceRaysIndirectCommand2KHR, hitShaderBindingTableAddress),
   SBT_CALLABLE = offsetof(VkTraceRaysIndirectCommand2KHR, callableShaderBindingTableAddress),
};

enum radv_nir_sbt_entry {
   SBT_RECURSIVE_PTR = offsetof(struct radv_pipeline_group_handle, recursive_shader_ptr),
   SBT_AHIT_ISEC_PTR = offsetof(struct radv_pipeline_group_handle, ahit_isec_ptr),
   SBT_GENERAL_IDX = offsetof(struct radv_pipeline_group_handle, general_index),
   SBT_CLOSEST_HIT_IDX = offsetof(struct radv_pipeline_group_handle, closest_hit_index),
   SBT_INTERSECTION_IDX = offsetof(struct radv_pipeline_group_handle, intersection_index),
   SBT_ANY_HIT_IDX = offsetof(struct radv_pipeline_group_handle, any_hit_index),
};

struct radv_nir_sbt_data radv_nir_load_sbt_entry(nir_builder *b, nir_def *base, nir_def *idx,
                                                 enum radv_nir_sbt_type binding, enum radv_nir_sbt_entry offset);

/*
 *
 * Common lowering passes
 *
 */

bool radv_nir_lower_rt_storage(nir_shader *shader, nir_deref_instr **hit_attribs, nir_deref_instr **payload_in,
                               nir_variable **payload_out, uint32_t workgroup_size);

void radv_nir_param_from_type(nir_parameter *param, const glsl_type *type, bool uniform, unsigned driver_attribs);
void radv_nir_return_param_from_type(nir_parameter *param, const glsl_type *type, bool uniform,
                                     unsigned driver_attribs);

/*
 *
 *  Ray Traversal Helpers
 *
 */

typedef void (*radv_nir_ahit_isec_preprocess_cb)(nir_shader *shader, void *data);

/* All parameters for performing ray traversal. */
struct radv_nir_rt_traversal_params {
   nir_def *accel_struct;
   nir_def *origin;
   nir_def *direction;
   nir_def *tmin;
   nir_def *tmax;
   nir_def *sbt_offset;
   nir_def *sbt_stride;
   nir_def *cull_mask_and_flags;
   nir_def *miss_index;

   bool ignore_cull_mask;
   uint32_t payload_size;
   uint32_t hit_attrib_size;

   radv_nir_ahit_isec_preprocess_cb preprocess_ahit_isec;

   /* User data passed to the inlining callback */
   void *cb_data;
};

/* Variables describing the result of the traversal loop. */
struct radv_nir_rt_traversal_result {
   nir_variable *sbt_index;
   nir_variable *tmax;
   nir_variable *hit;
   nir_variable *primitive_addr;
   nir_variable *primitive_id;
   nir_variable *geometry_id_and_flags;
   nir_variable *instance_addr;
   nir_variable *hit_kind;

   /* barycentrics are a bit special, because they're hit attributes (specifically, the first two hit attributes in
    * attribute storage) under the hood.
    * They're not considered in the init_traversal_result/copy_traversal_result helpers and need manual initialization
    * wherever used.
    */
   nir_variable *barycentrics;
};

struct radv_nir_rt_traversal_result radv_build_traversal(struct radv_device *device,
                                                         struct radv_ray_tracing_pipeline *pipeline, nir_builder *b,
                                                         struct radv_nir_rt_traversal_params *params,
                                                         struct radv_ray_tracing_stage_info *info);

void radv_build_rt_prolog(struct radv_device *device, struct radv_shader_stage *stage, bool uses_descriptor_heap,
                          struct radv_shader_debug_info *debug);

#endif // MESA_RADV_NIR_RT_STAGE_COMMON_H
