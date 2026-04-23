/*
 * Copyright © 2025 Valve Corporation
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"

#include "nir/radv_nir.h"
#include "nir/radv_nir_rt_common.h"
#include "nir/radv_nir_rt_stage_common.h"
#include "nir/radv_nir_rt_stage_functions.h"

#include "radv_device.h"
#include "radv_physical_device.h"
#include "radv_shader.h"

#include "aco_nir_call_attribs.h"

#include "vk_pipeline.h"

static void
radv_nir_init_common_rt_params(nir_function *function, bool uses_descriptor_heap)
{
   radv_nir_param_from_type(function->params + RT_ARG_LAUNCH_ID, glsl_vector_type(GLSL_TYPE_UINT, 3), false, 0);
   radv_nir_param_from_type(function->params + RT_ARG_LAUNCH_SIZE, glsl_vector_type(GLSL_TYPE_UINT, 3), true, 0);
   if (uses_descriptor_heap) {
      radv_nir_param_from_type(function->params + RT_ARG_HEAP_RESOURCE, glsl_uint_type(), true, 0);
      radv_nir_param_from_type(function->params + RT_ARG_HEAP_SAMPLER, glsl_uint_type(), true, 0);
   } else {
      radv_nir_param_from_type(function->params + RT_ARG_DESCRIPTORS, glsl_uint_type(), true, 0);
      radv_nir_param_from_type(function->params + RT_ARG_DYNAMIC_DESCRIPTORS, glsl_uint_type(), true, 0);
   }
   radv_nir_param_from_type(function->params + RT_ARG_PUSH_CONSTANTS, glsl_uint_type(), true, 0);
   radv_nir_param_from_type(function->params + RT_ARG_SBT_DESCRIPTORS, glsl_uint64_t_type(), true, 0);
}

static void
radv_nir_init_traversal_params(nir_function *function, unsigned payload_size, bool uses_descriptor_heap)
{
   function->num_params = TRAVERSAL_ARG_PAYLOAD_BASE + DIV_ROUND_UP(payload_size, 4);
   function->params = rzalloc_array_size(function->shader, sizeof(nir_parameter), function->num_params);
   radv_nir_init_common_rt_params(function, uses_descriptor_heap);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_TRAVERSAL_ADDR, glsl_uint64_t_type(), true, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_SHADER_RECORD_PTR, glsl_uint64_t_type(), false, ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_ACCEL_STRUCT, glsl_uint64_t_type(), false, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_CULL_MASK_AND_FLAGS, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_SBT_OFFSET, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_SBT_STRIDE, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_MISS_INDEX, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_RAY_ORIGIN, glsl_vector_type(GLSL_TYPE_UINT, 3), false, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_RAY_TMIN, glsl_float_type(), false, 0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_RAY_DIRECTION, glsl_vector_type(GLSL_TYPE_UINT, 3), false,
                            0);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_RAY_TMAX, glsl_float_type(), false,
                            ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_PRIMITIVE_ADDR, glsl_uint64_t_type(), false, ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_PRIMITIVE_ID, glsl_uint_type(), false, ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_INSTANCE_ADDR, glsl_uint64_t_type(), false, ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_GEOMETRY_ID_AND_FLAGS, glsl_uint_type(), false,  ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
   radv_nir_param_from_type(function->params + TRAVERSAL_ARG_HIT_KIND, glsl_uint_type(), false, ACO_NIR_PARAM_ATTRIB_DISCARDABLE);
   for (unsigned i = 0; i < DIV_ROUND_UP(payload_size, 4); ++i) {
      radv_nir_return_param_from_type(function->params + TRAVERSAL_ARG_PAYLOAD_BASE + i, glsl_uint_type(), false, 0);
   }

   function->driver_attributes = ACO_NIR_CALL_ABI_TRAVERSAL;
   /* Entrypoints can't have parameters. Consider RT stages as callable functions */
   function->is_exported = true;
   function->is_entrypoint = false;
}

void
radv_nir_init_rt_function_params(nir_function *function, mesa_shader_stage stage, unsigned payload_size,
                                 unsigned hit_attrib_size, bool uses_descriptor_heap)
{
   unsigned payload_base = -1u;

   switch (stage) {
   case MESA_SHADER_RAYGEN:
      function->num_params = RAYGEN_ARG_COUNT;
      function->params = rzalloc_array_size(function->shader, sizeof(nir_parameter), function->num_params);
      radv_nir_init_common_rt_params(function, uses_descriptor_heap);
      radv_nir_param_from_type(function->params + RAYGEN_ARG_TRAVERSAL_ADDR, glsl_uint64_t_type(), true, 0);
      radv_nir_param_from_type(function->params + RAYGEN_ARG_SHADER_RECORD_PTR, glsl_uint64_t_type(), false, 0);
      function->driver_attributes = (uint32_t)ACO_NIR_CALL_ABI_RT_RECURSIVE | ACO_NIR_FUNCTION_ATTRIB_NORETURN;
      break;
   case MESA_SHADER_CALLABLE:
      function->num_params = RAYGEN_ARG_COUNT + DIV_ROUND_UP(payload_size, 4);
      function->params = rzalloc_array_size(function->shader, sizeof(nir_parameter), function->num_params);
      radv_nir_init_common_rt_params(function, uses_descriptor_heap);
      radv_nir_param_from_type(function->params + RAYGEN_ARG_TRAVERSAL_ADDR, glsl_uint64_t_type(), true, 0);
      radv_nir_param_from_type(function->params + RAYGEN_ARG_SHADER_RECORD_PTR, glsl_uint64_t_type(), false, 0);

      function->driver_attributes = (uint32_t)ACO_NIR_CALL_ABI_RT_RECURSIVE | ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL;
      payload_base = RAYGEN_ARG_COUNT;
      break;
   case MESA_SHADER_ANY_HIT:
   case MESA_SHADER_INTERSECTION:
      function->num_params =
         AHIT_ISEC_ARG_HIT_ATTRIB_PAYLOAD_BASE + DIV_ROUND_UP(hit_attrib_size, 4) + DIV_ROUND_UP(payload_size, 4);
      function->params = rzalloc_array_size(function->shader, sizeof(nir_parameter), function->num_params);
      radv_nir_init_common_rt_params(function, uses_descriptor_heap);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_SHADER_RECORD_PTR, glsl_uint64_t_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_CULL_MASK_AND_FLAGS, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_SBT_INDEX, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_RAY_ORIGIN, glsl_vector_type(GLSL_TYPE_UINT, 3), false,
                               0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_RAY_TMIN, glsl_float_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_RAY_DIRECTION, glsl_vector_type(GLSL_TYPE_UINT, 3),
                               false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_CANDIDATE_RAY_TMAX, glsl_float_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_PRIMITIVE_ADDR, glsl_uint64_t_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_PRIMITIVE_ID, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_INSTANCE_ADDR, glsl_uint64_t_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_GEOMETRY_ID_AND_FLAGS, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + AHIT_ISEC_ARG_OPAQUE, glsl_bool_type(), false, 0);
      radv_nir_return_param_from_type(function->params + AHIT_ISEC_ARG_HIT_KIND, glsl_uint_type(), false, 0);
      radv_nir_return_param_from_type(function->params + AHIT_ISEC_ARG_ACCEPT, glsl_bool_type(), false, 0);
      radv_nir_return_param_from_type(function->params + AHIT_ISEC_ARG_TERMINATE, glsl_bool_type(), false, 0);
      radv_nir_return_param_from_type(function->params + AHIT_ISEC_ARG_COMMITTED_RAY_TMAX, glsl_float_type(), false, 0);
      for (unsigned i = 0; i < DIV_ROUND_UP(hit_attrib_size, 4); ++i)
         radv_nir_return_param_from_type(function->params + AHIT_ISEC_ARG_HIT_ATTRIB_PAYLOAD_BASE + i, glsl_uint_type(),
                                         false, 0);

      function->driver_attributes = (uint32_t)ACO_NIR_CALL_ABI_AHIT_ISEC | ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL;
      payload_base = AHIT_ISEC_ARG_HIT_ATTRIB_PAYLOAD_BASE + DIV_ROUND_UP(hit_attrib_size, 4);
      break;
   case MESA_SHADER_CLOSEST_HIT:
   case MESA_SHADER_MISS:
      function->num_params = CHIT_MISS_ARG_PAYLOAD_BASE + DIV_ROUND_UP(payload_size, 4);
      function->params = rzalloc_array_size(function->shader, sizeof(nir_parameter), function->num_params);
      radv_nir_init_common_rt_params(function, uses_descriptor_heap);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_TRAVERSAL_ADDR, glsl_uint64_t_type(), true, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_SHADER_RECORD_PTR, glsl_uint64_t_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_ACCEL_STRUCT, glsl_uint64_t_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_CULL_MASK_AND_FLAGS, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_SBT_OFFSET, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_SBT_STRIDE, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_MISS_INDEX, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_RAY_ORIGIN, glsl_vector_type(GLSL_TYPE_UINT, 3), false,
                               0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_RAY_TMIN, glsl_float_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_RAY_DIRECTION, glsl_vector_type(GLSL_TYPE_UINT, 3),
                               false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_RAY_TMAX, glsl_float_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_PRIMITIVE_ADDR, glsl_uint64_t_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_PRIMITIVE_ID, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_INSTANCE_ADDR, glsl_uint64_t_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_GEOMETRY_ID_AND_FLAGS, glsl_uint_type(), false, 0);
      radv_nir_param_from_type(function->params + CHIT_MISS_ARG_HIT_KIND, glsl_uint_type(), false, 0);

      function->driver_attributes = (uint32_t)ACO_NIR_CALL_ABI_RT_RECURSIVE | ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL;
      payload_base = CHIT_MISS_ARG_PAYLOAD_BASE;
      break;
   default:
      UNREACHABLE("invalid RT stage");
   }

   if (payload_base != -1u) {
      for (unsigned i = 0; i < DIV_ROUND_UP(payload_size, 4); ++i)
         radv_nir_return_param_from_type(function->params + payload_base + i, glsl_uint_type(), false, 0);
   }

   /* Entrypoints can't have parameters. Consider RT stages as callable functions */
   function->is_exported = true;
   function->is_entrypoint = false;
}

/*
 * Global variables for an RT pipeline
 */
struct rt_variables {
   struct radv_device *device;
   const VkPipelineCreateFlags2 flags;

   /* Stage-dependent parameter indices */
   unsigned shader_record_ptr_param;
   unsigned traversal_addr_param;
   unsigned accel_struct_param;
   unsigned cull_mask_and_flags_param;
   unsigned sbt_offset_param;
   unsigned sbt_stride_param;
   unsigned miss_index_param;
   unsigned ray_origin_param;
   unsigned ray_tmin_param;
   unsigned ray_direction_param;
   unsigned ray_tmax_param;
   unsigned primitive_id_param;
   unsigned instance_addr_param;
   unsigned primitive_addr_param;
   unsigned geometry_id_and_flags_param;
   unsigned hit_kind_param;
   unsigned in_payload_base_param;

   /* Any-Hit/Intersection return params */
   nir_deref_instr *hit_kind;
   nir_deref_instr *accept;
   nir_deref_instr *terminate;
   nir_deref_instr *committed_ray_tmax;
   nir_deref_instr **hit_attrib_storage;

   nir_variable **out_payload_storage;
   unsigned payload_size;
   unsigned hit_attrib_size;

   nir_function *trace_ray_func;
   nir_function *chit_miss_func;
   nir_function *ahit_isec_func;
   nir_function *callable_func;

   unsigned stack_size;
};

static struct rt_variables
create_rt_variables(nir_shader *shader, struct radv_device *device, const VkPipelineCreateFlags2 flags,
                    unsigned max_payload_size, unsigned max_hit_attrib_size)
{
   const bool uses_descriptor_heap = flags & VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
   struct rt_variables vars = {
      .device = device,
      .flags = flags,
   };

   if (max_payload_size)
      vars.out_payload_storage = rzalloc_array_size(shader, DIV_ROUND_UP(max_payload_size, 4), sizeof(nir_variable *));
   vars.payload_size = max_payload_size;
   vars.hit_attrib_size = max_hit_attrib_size;
   for (unsigned i = 0; i < DIV_ROUND_UP(max_payload_size, 4); ++i) {
      vars.out_payload_storage[i] =
         nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "out_payload_storage");
   }

   nir_function *trace_ray_func = nir_function_create(shader, "trace_ray_func");
   radv_nir_init_traversal_params(trace_ray_func, max_payload_size, uses_descriptor_heap);
   vars.trace_ray_func = trace_ray_func;
   nir_function *ahit_isec_func = nir_function_create(shader, "ahit_isec_func");
   radv_nir_init_rt_function_params(ahit_isec_func, MESA_SHADER_ANY_HIT, max_payload_size, max_hit_attrib_size,
                                    uses_descriptor_heap);
   vars.ahit_isec_func = ahit_isec_func;
   nir_function *chit_miss_func = nir_function_create(shader, "chit_miss_func");
   radv_nir_init_rt_function_params(chit_miss_func, MESA_SHADER_CLOSEST_HIT, max_payload_size, max_hit_attrib_size,
                                    uses_descriptor_heap);
   vars.chit_miss_func = chit_miss_func;
   nir_function *callable_func = nir_function_create(shader, "callable_func");
   radv_nir_init_rt_function_params(callable_func, MESA_SHADER_CALLABLE, max_payload_size, max_hit_attrib_size,
                                    uses_descriptor_heap);
   vars.callable_func = callable_func;

   vars.shader_record_ptr_param = -1u;
   vars.traversal_addr_param = -1u;
   vars.accel_struct_param = -1u;
   vars.cull_mask_and_flags_param = -1u;
   vars.sbt_offset_param = -1u;
   vars.sbt_stride_param = -1u;
   vars.miss_index_param = -1u;
   vars.ray_origin_param = -1u;
   vars.ray_tmin_param = -1u;
   vars.ray_direction_param = -1u;
   vars.ray_tmax_param = -1u;
   vars.primitive_id_param = -1u;
   vars.instance_addr_param = -1u;
   vars.primitive_addr_param = -1u;
   vars.geometry_id_and_flags_param = -1u;
   vars.hit_kind_param = -1u;
   vars.in_payload_base_param = -1u;

   if (radv_is_traversal_shader(shader)) {
      vars.traversal_addr_param = TRAVERSAL_ARG_TRAVERSAL_ADDR;
      vars.shader_record_ptr_param = TRAVERSAL_ARG_SHADER_RECORD_PTR;
      vars.accel_struct_param = TRAVERSAL_ARG_ACCEL_STRUCT;
      vars.cull_mask_and_flags_param = TRAVERSAL_ARG_CULL_MASK_AND_FLAGS;
      vars.sbt_offset_param = TRAVERSAL_ARG_SBT_OFFSET;
      vars.sbt_stride_param = TRAVERSAL_ARG_SBT_STRIDE;
      vars.miss_index_param = TRAVERSAL_ARG_MISS_INDEX;
      vars.ray_origin_param = TRAVERSAL_ARG_RAY_ORIGIN;
      vars.ray_tmin_param = TRAVERSAL_ARG_RAY_TMIN;
      vars.ray_direction_param = TRAVERSAL_ARG_RAY_DIRECTION;
      vars.ray_tmax_param = TRAVERSAL_ARG_RAY_TMAX;
      vars.in_payload_base_param = TRAVERSAL_ARG_PAYLOAD_BASE;
   } else {
      switch (shader->info.stage) {
      case MESA_SHADER_CALLABLE:
         vars.in_payload_base_param = RAYGEN_ARG_COUNT;
         vars.shader_record_ptr_param = RAYGEN_ARG_SHADER_RECORD_PTR;
         vars.traversal_addr_param = RAYGEN_ARG_TRAVERSAL_ADDR;
         break;
      case MESA_SHADER_RAYGEN:
         vars.shader_record_ptr_param = RAYGEN_ARG_SHADER_RECORD_PTR;
         vars.traversal_addr_param = RAYGEN_ARG_TRAVERSAL_ADDR;
         break;
      case MESA_SHADER_ANY_HIT:
      case MESA_SHADER_INTERSECTION:
         vars.shader_record_ptr_param = AHIT_ISEC_ARG_SHADER_RECORD_PTR;
         vars.cull_mask_and_flags_param = AHIT_ISEC_ARG_CULL_MASK_AND_FLAGS;
         vars.ray_origin_param = AHIT_ISEC_ARG_RAY_ORIGIN;
         vars.ray_tmin_param = AHIT_ISEC_ARG_RAY_TMIN;
         vars.ray_direction_param = AHIT_ISEC_ARG_RAY_DIRECTION;
         vars.ray_tmax_param = AHIT_ISEC_ARG_CANDIDATE_RAY_TMAX;
         vars.primitive_id_param = AHIT_ISEC_ARG_PRIMITIVE_ID;
         vars.instance_addr_param = AHIT_ISEC_ARG_INSTANCE_ADDR;
         vars.primitive_addr_param = AHIT_ISEC_ARG_PRIMITIVE_ADDR;
         vars.geometry_id_and_flags_param = AHIT_ISEC_ARG_GEOMETRY_ID_AND_FLAGS;
         vars.in_payload_base_param = AHIT_ISEC_ARG_HIT_ATTRIB_PAYLOAD_BASE + DIV_ROUND_UP(max_hit_attrib_size, 4);
         break;
      case MESA_SHADER_CLOSEST_HIT:
      case MESA_SHADER_MISS:
         vars.traversal_addr_param = CHIT_MISS_ARG_TRAVERSAL_ADDR;
         vars.shader_record_ptr_param = CHIT_MISS_ARG_SHADER_RECORD_PTR;
         vars.accel_struct_param = CHIT_MISS_ARG_ACCEL_STRUCT;
         vars.cull_mask_and_flags_param = CHIT_MISS_ARG_CULL_MASK_AND_FLAGS;
         vars.sbt_offset_param = CHIT_MISS_ARG_SBT_OFFSET;
         vars.sbt_stride_param = CHIT_MISS_ARG_SBT_STRIDE;
         vars.miss_index_param = CHIT_MISS_ARG_MISS_INDEX;
         vars.ray_origin_param = CHIT_MISS_ARG_RAY_ORIGIN;
         vars.ray_tmin_param = CHIT_MISS_ARG_RAY_TMIN;
         vars.ray_direction_param = CHIT_MISS_ARG_RAY_DIRECTION;
         vars.ray_tmax_param = CHIT_MISS_ARG_RAY_TMAX;
         vars.primitive_id_param = CHIT_MISS_ARG_PRIMITIVE_ID;
         vars.instance_addr_param = CHIT_MISS_ARG_INSTANCE_ADDR;
         vars.primitive_addr_param = CHIT_MISS_ARG_PRIMITIVE_ADDR;
         vars.geometry_id_and_flags_param = CHIT_MISS_ARG_GEOMETRY_ID_AND_FLAGS;
         vars.hit_kind_param = CHIT_MISS_ARG_HIT_KIND;
         vars.in_payload_base_param = CHIT_MISS_ARG_PAYLOAD_BASE;
         break;
      default:
         break;
      }
   }
   return vars;
}

static bool
lower_rt_instruction(nir_builder *b, nir_instr *instr, void *_vars)
{
   if (instr->type == nir_instr_type_jump) {
      nir_jump_instr *jump = nir_instr_as_jump(instr);
      if (jump->type == nir_jump_halt) {
         jump->type = nir_jump_return;
         return true;
      }
      return false;
   } else if (instr->type != nir_instr_type_intrinsic) {
      return false;
   }

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   struct rt_variables *vars = _vars;
   const bool uses_descriptor_heap = vars->flags & VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *ret = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_execute_callable: {
      struct radv_nir_sbt_data sbt_data = radv_nir_load_sbt_entry(b, nir_load_param(b, RT_ARG_SBT_DESCRIPTORS),
                                                                  intr->src[0].ssa, SBT_CALLABLE, SBT_RECURSIVE_PTR);

      unsigned param_count = RAYGEN_ARG_COUNT + DIV_ROUND_UP(vars->payload_size, 4);
      nir_def **args = rzalloc_array_size(b->shader, sizeof(nir_def *), param_count);
      args[RT_ARG_LAUNCH_ID] = nir_load_param(b, RT_ARG_LAUNCH_ID);
      args[RT_ARG_LAUNCH_SIZE] = nir_load_param(b, RT_ARG_LAUNCH_SIZE);
      if (uses_descriptor_heap) {
         args[RT_ARG_HEAP_RESOURCE] = nir_load_param(b, RT_ARG_HEAP_RESOURCE);
         args[RT_ARG_HEAP_SAMPLER] = nir_load_param(b, RT_ARG_HEAP_SAMPLER);
      } else {
         args[RT_ARG_DESCRIPTORS] = nir_load_param(b, RT_ARG_DESCRIPTORS);
         args[RT_ARG_DYNAMIC_DESCRIPTORS] = nir_load_param(b, RT_ARG_DYNAMIC_DESCRIPTORS);
      }
      args[RT_ARG_PUSH_CONSTANTS] = nir_load_param(b, RT_ARG_PUSH_CONSTANTS);
      args[RT_ARG_SBT_DESCRIPTORS] = nir_load_param(b, RT_ARG_SBT_DESCRIPTORS);
      args[RAYGEN_ARG_TRAVERSAL_ADDR] = nir_undef(b, 1, 64);
      args[RAYGEN_ARG_SHADER_RECORD_PTR] = sbt_data.shader_record_ptr;
      for (unsigned i = 0; i < DIV_ROUND_UP(vars->payload_size, 4); ++i) {
         args[RAYGEN_ARG_COUNT + i] = nir_instr_def(&nir_build_deref_var(b, vars->out_payload_storage[i])->instr);
      }
      nir_build_indirect_call(b, vars->callable_func, sbt_data.shader_addr, param_count, args);
      break;
   }
   case nir_intrinsic_trace_ray: {
      nir_def *undef = nir_undef(b, 1, 32);
      /* Per the SPIR-V extension spec we have to ignore some bits for some arguments. */
      nir_def *cull_mask_and_flags = nir_ior(b, nir_ishl_imm(b, intr->src[2].ssa, 24), intr->src[1].ssa);
      nir_def *traversal_addr = nir_load_param(b, vars->traversal_addr_param);

      unsigned param_count = TRAVERSAL_ARG_PAYLOAD_BASE + DIV_ROUND_UP(vars->payload_size, 4);
      nir_def **args = rzalloc_array_size(b->shader, sizeof(nir_def *), param_count);
      args[RT_ARG_LAUNCH_ID] = nir_load_param(b, RT_ARG_LAUNCH_ID);
      args[RT_ARG_LAUNCH_SIZE] = nir_load_param(b, RT_ARG_LAUNCH_SIZE);
      args[RT_ARG_DESCRIPTORS] = nir_load_param(b, RT_ARG_DESCRIPTORS);
      if (uses_descriptor_heap) {
         args[RT_ARG_HEAP_RESOURCE] = nir_load_param(b, RT_ARG_HEAP_RESOURCE);
         args[RT_ARG_HEAP_SAMPLER] = nir_load_param(b, RT_ARG_HEAP_SAMPLER);
      } else {
         args[RT_ARG_DESCRIPTORS] = nir_load_param(b, RT_ARG_DESCRIPTORS);
         args[RT_ARG_DYNAMIC_DESCRIPTORS] = nir_load_param(b, RT_ARG_DYNAMIC_DESCRIPTORS);
      }
      args[RT_ARG_PUSH_CONSTANTS] = nir_load_param(b, RT_ARG_PUSH_CONSTANTS);
      args[RT_ARG_SBT_DESCRIPTORS] = nir_load_param(b, RT_ARG_SBT_DESCRIPTORS);
      args[TRAVERSAL_ARG_TRAVERSAL_ADDR] = traversal_addr;
      /* Traversal does not have a shader record. */
      args[TRAVERSAL_ARG_SHADER_RECORD_PTR] = nir_undef(b, 1, 64);
      args[TRAVERSAL_ARG_ACCEL_STRUCT] = intr->src[0].ssa;
      args[TRAVERSAL_ARG_CULL_MASK_AND_FLAGS] = cull_mask_and_flags;
      args[TRAVERSAL_ARG_SBT_OFFSET] = nir_iand_imm(b, intr->src[3].ssa, 0xf);
      args[TRAVERSAL_ARG_SBT_STRIDE] = nir_iand_imm(b, intr->src[4].ssa, 0xf);
      args[TRAVERSAL_ARG_MISS_INDEX] = nir_iand_imm(b, intr->src[5].ssa, 0xffff);
      args[TRAVERSAL_ARG_RAY_ORIGIN] = intr->src[6].ssa;
      args[TRAVERSAL_ARG_RAY_TMIN] = intr->src[7].ssa;
      args[TRAVERSAL_ARG_RAY_DIRECTION] = intr->src[8].ssa;
      args[TRAVERSAL_ARG_RAY_TMAX] = intr->src[9].ssa;
      args[TRAVERSAL_ARG_PRIMITIVE_ADDR] = nir_undef(b, 1, 64);
      args[TRAVERSAL_ARG_PRIMITIVE_ID] = undef;
      args[TRAVERSAL_ARG_INSTANCE_ADDR] = nir_undef(b, 1, 64);
      args[TRAVERSAL_ARG_GEOMETRY_ID_AND_FLAGS] = undef;
      args[TRAVERSAL_ARG_HIT_KIND] = undef;
      for (unsigned i = 0; i < DIV_ROUND_UP(vars->payload_size, 4); ++i) {
         args[TRAVERSAL_ARG_PAYLOAD_BASE + i] =
            nir_instr_def(&nir_build_deref_var(b, vars->out_payload_storage[i])->instr);
      }
      nir_build_indirect_call(b, vars->trace_ray_func, traversal_addr, param_count, args);
      break;
   }
   case nir_intrinsic_load_shader_record_ptr: {
      ret = nir_load_param(b, vars->shader_record_ptr_param);
      break;
   }
   case nir_intrinsic_load_ray_launch_size: {
      ret = nir_load_param(b, RT_ARG_LAUNCH_SIZE);
      break;
   };
   case nir_intrinsic_load_ray_launch_id: {
      ret = nir_load_param(b, RT_ARG_LAUNCH_ID);
      break;
   }
   case nir_intrinsic_load_ray_t_min: {
      ret = nir_load_param(b, vars->ray_tmin_param);
      break;
   }
   case nir_intrinsic_load_ray_t_max: {
      ret = nir_load_param(b, vars->ray_tmax_param);
      break;
   }
   case nir_intrinsic_load_ray_world_origin: {
      ret = nir_load_param(b, vars->ray_origin_param);
      break;
   }
   case nir_intrinsic_load_ray_world_direction: {
      ret = nir_load_param(b, vars->ray_direction_param);
      break;
   }
   case nir_intrinsic_load_ray_instance_custom_index: {
      ret = radv_load_custom_instance(vars->device, b, nir_load_param(b, vars->instance_addr_param));
      break;
   }
   case nir_intrinsic_load_primitive_id: {
      ret = nir_load_param(b, vars->primitive_id_param);
      break;
   }
   case nir_intrinsic_load_ray_geometry_index: {
      ret = nir_load_param(b, vars->geometry_id_and_flags_param);
      ret = nir_iand_imm(b, ret, 0xFFFFFFF);
      break;
   }
   case nir_intrinsic_load_instance_id: {
      ret = radv_load_instance_id(vars->device, b, nir_load_param(b, vars->instance_addr_param));
      break;
   }
   case nir_intrinsic_load_ray_flags: {
      ret = nir_iand_imm(b, nir_load_param(b, vars->cull_mask_and_flags_param), 0xFFFFFF);
      break;
   }
   case nir_intrinsic_load_ray_hit_kind: {
      if (vars->hit_kind_param == -1)
         ret = nir_load_deref(b, vars->hit_kind);
      else
         ret = nir_load_param(b, vars->hit_kind_param);
      break;
   }
   case nir_intrinsic_load_ray_world_to_object: {
      unsigned c = nir_intrinsic_column(intr);
      nir_def *instance_node_addr = nir_load_param(b, vars->instance_addr_param);
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(vars->device, b, instance_node_addr, wto_matrix);

      nir_def *vals[3];
      for (unsigned i = 0; i < 3; ++i)
         vals[i] = nir_channel(b, wto_matrix[i], c);

      ret = nir_vec(b, vals, 3);
      break;
   }
   case nir_intrinsic_load_ray_object_to_world: {
      unsigned c = nir_intrinsic_column(intr);
      nir_def *otw_matrix[3];
      radv_load_otw_matrix(vars->device, b, nir_load_param(b, vars->instance_addr_param), otw_matrix);
      ret = nir_vec3(b, nir_channel(b, otw_matrix[0], c), nir_channel(b, otw_matrix[1], c),
                     nir_channel(b, otw_matrix[2], c));
      break;
   }
   case nir_intrinsic_load_ray_object_origin: {
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(vars->device, b, nir_load_param(b, vars->instance_addr_param), wto_matrix);
      ret = nir_build_vec3_mat_mult(b, nir_load_param(b, vars->ray_origin_param), wto_matrix, true);
      break;
   }
   case nir_intrinsic_load_ray_object_direction: {
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(vars->device, b, nir_load_param(b, vars->instance_addr_param), wto_matrix);
      ret = nir_build_vec3_mat_mult(b, nir_load_param(b, vars->ray_direction_param), wto_matrix, false);
      break;
   }
   case nir_intrinsic_load_cull_mask: {
      ret = nir_ushr_imm(b, nir_load_param(b, vars->cull_mask_and_flags_param), 24);
      break;
   }
   case nir_intrinsic_load_ray_payload_ptr_amd: {
      ret = nir_load_param(b, vars->in_payload_base_param + nir_intrinsic_base(intr));
      break;
   }
   case nir_intrinsic_load_rt_descriptors_amd: {
      ret = nir_load_param(b, RT_ARG_DESCRIPTORS);
      break;
   }
   case nir_intrinsic_load_rt_heap_resource_amd: {
      ret = nir_load_param(b, RT_ARG_HEAP_RESOURCE);
      break;
   }
   case nir_intrinsic_load_rt_heap_sampler_amd: {
      ret = nir_load_param(b, RT_ARG_HEAP_SAMPLER);
      break;
   }
   case nir_intrinsic_load_rt_dynamic_descriptors_amd: {
      ret = nir_load_param(b, RT_ARG_DYNAMIC_DESCRIPTORS);
      break;
   }
   case nir_intrinsic_load_rt_push_constants_amd: {
      ret = nir_load_param(b, RT_ARG_PUSH_CONSTANTS);
      break;
   }
   case nir_intrinsic_load_sbt_base_amd: {
      ret = nir_load_param(b, RT_ARG_SBT_DESCRIPTORS);
      break;
   }
   case nir_intrinsic_load_sbt_offset_amd: {
      ret = nir_load_param(b, vars->sbt_offset_param);
      break;
   }
   case nir_intrinsic_load_sbt_stride_amd: {
      ret = nir_load_param(b, vars->sbt_stride_param);
      break;
   }
   case nir_intrinsic_load_accel_struct_amd: {
      ret = nir_load_param(b, vars->accel_struct_param);
      break;
   }
   case nir_intrinsic_load_cull_mask_and_flags_amd: {
      ret = nir_load_param(b, vars->cull_mask_and_flags_param);
      break;
   }
   case nir_intrinsic_load_intersection_opaque_amd: {
      ret = nir_load_param(b, AHIT_ISEC_ARG_OPAQUE);
      break;
   }
   case nir_intrinsic_execute_closest_hit_amd: {
      struct radv_nir_sbt_data sbt_data = radv_nir_load_sbt_entry(b, nir_load_param(b, RT_ARG_SBT_DESCRIPTORS),
                                                                  intr->src[0].ssa, SBT_HIT, SBT_RECURSIVE_PTR);

      nir_def *should_return =
         nir_test_mask(b, nir_load_param(b, vars->cull_mask_and_flags_param), SpvRayFlagsSkipClosestHitShaderKHRMask);

      if (!(vars->flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR)) {
         should_return = nir_ior(b, should_return, nir_ieq_imm(b, sbt_data.shader_addr, 0));
      }

      /* should_return is set if we had a hit but we won't be calling the closest hit
       * shader and hence need to return immediately to the calling shader. */
      nir_push_if(b, nir_inot(b, should_return));
      unsigned param_count = CHIT_MISS_ARG_PAYLOAD_BASE + DIV_ROUND_UP(vars->payload_size, 4);
      nir_def **args = rzalloc_array_size(b->shader, sizeof(nir_def *), param_count);
      args[RT_ARG_LAUNCH_ID] = nir_load_param(b, RT_ARG_LAUNCH_ID);
      args[RT_ARG_LAUNCH_SIZE] = nir_load_param(b, RT_ARG_LAUNCH_SIZE);
      if (uses_descriptor_heap) {
         args[RT_ARG_HEAP_RESOURCE] = nir_load_param(b, RT_ARG_HEAP_RESOURCE);
         args[RT_ARG_HEAP_SAMPLER] = nir_load_param(b, RT_ARG_HEAP_SAMPLER);
      } else {
         args[RT_ARG_DESCRIPTORS] = nir_load_param(b, RT_ARG_DESCRIPTORS);
         args[RT_ARG_DYNAMIC_DESCRIPTORS] = nir_load_param(b, RT_ARG_DYNAMIC_DESCRIPTORS);
      }
      args[RT_ARG_PUSH_CONSTANTS] = nir_load_param(b, RT_ARG_PUSH_CONSTANTS);
      args[RT_ARG_SBT_DESCRIPTORS] = nir_load_param(b, RT_ARG_SBT_DESCRIPTORS);
      args[CHIT_MISS_ARG_TRAVERSAL_ADDR] = nir_load_param(b, vars->traversal_addr_param);
      args[CHIT_MISS_ARG_SHADER_RECORD_PTR] = sbt_data.shader_record_ptr;
      args[CHIT_MISS_ARG_ACCEL_STRUCT] = nir_load_param(b, vars->accel_struct_param);
      args[CHIT_MISS_ARG_CULL_MASK_AND_FLAGS] = nir_load_param(b, vars->cull_mask_and_flags_param);
      args[CHIT_MISS_ARG_SBT_OFFSET] = nir_load_param(b, vars->sbt_offset_param);
      args[CHIT_MISS_ARG_SBT_STRIDE] = nir_load_param(b, vars->sbt_stride_param);
      args[CHIT_MISS_ARG_MISS_INDEX] = nir_load_param(b, vars->miss_index_param);
      args[CHIT_MISS_ARG_RAY_ORIGIN] = nir_load_param(b, vars->ray_origin_param);
      args[CHIT_MISS_ARG_RAY_TMIN] = nir_load_param(b, vars->ray_tmin_param);
      args[CHIT_MISS_ARG_RAY_DIRECTION] = nir_load_param(b, vars->ray_direction_param);
      args[CHIT_MISS_ARG_RAY_TMAX] = intr->src[1].ssa;
      args[CHIT_MISS_ARG_PRIMITIVE_ADDR] = intr->src[2].ssa;
      args[CHIT_MISS_ARG_PRIMITIVE_ID] = intr->src[3].ssa;
      args[CHIT_MISS_ARG_INSTANCE_ADDR] = intr->src[4].ssa;
      args[CHIT_MISS_ARG_GEOMETRY_ID_AND_FLAGS] = intr->src[5].ssa;
      args[CHIT_MISS_ARG_HIT_KIND] = intr->src[6].ssa;
      for (unsigned i = 0; i < DIV_ROUND_UP(vars->payload_size, 4); ++i) {
         args[CHIT_MISS_ARG_PAYLOAD_BASE + i] =
            nir_instr_def(&nir_build_deref_cast(b, nir_load_param(b, TRAVERSAL_ARG_PAYLOAD_BASE + i),
                                                nir_var_shader_call_data, glsl_uint_type(), 4)
                              ->instr);
      }
      nir_build_indirect_call(b, vars->chit_miss_func, sbt_data.shader_addr, param_count, args);
      nir_pop_if(b, NULL);
      break;
   }
   case nir_intrinsic_execute_miss_amd: {
      nir_def *undef = nir_undef(b, 1, 32);
      nir_def *miss_index = nir_load_param(b, vars->miss_index_param);
      struct radv_nir_sbt_data sbt_data =
         radv_nir_load_sbt_entry(b, nir_load_param(b, RT_ARG_SBT_DESCRIPTORS), miss_index, SBT_MISS, SBT_RECURSIVE_PTR);

      if (!(vars->flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR)) {
         /* In case of a NULL miss shader, do nothing and just return. */
         nir_push_if(b, nir_ine_imm(b, sbt_data.shader_addr, 0));
      }

      unsigned param_count = CHIT_MISS_ARG_PAYLOAD_BASE + DIV_ROUND_UP(vars->payload_size, 4);
      nir_def **args = rzalloc_array_size(b->shader, sizeof(nir_def *), param_count);
      args[RT_ARG_LAUNCH_ID] = nir_load_param(b, RT_ARG_LAUNCH_ID);
      args[RT_ARG_LAUNCH_SIZE] = nir_load_param(b, RT_ARG_LAUNCH_SIZE);
      if (uses_descriptor_heap) {
         args[RT_ARG_HEAP_RESOURCE] = nir_load_param(b, RT_ARG_HEAP_RESOURCE);
         args[RT_ARG_HEAP_SAMPLER] = nir_load_param(b, RT_ARG_HEAP_SAMPLER);
      } else {
         args[RT_ARG_DESCRIPTORS] = nir_load_param(b, RT_ARG_DESCRIPTORS);
         args[RT_ARG_DYNAMIC_DESCRIPTORS] = nir_load_param(b, RT_ARG_DYNAMIC_DESCRIPTORS);
      }
      args[RT_ARG_PUSH_CONSTANTS] = nir_load_param(b, RT_ARG_PUSH_CONSTANTS);
      args[RT_ARG_SBT_DESCRIPTORS] = nir_load_param(b, RT_ARG_SBT_DESCRIPTORS);
      args[CHIT_MISS_ARG_TRAVERSAL_ADDR] = nir_load_param(b, vars->traversal_addr_param);
      args[CHIT_MISS_ARG_SHADER_RECORD_PTR] = sbt_data.shader_record_ptr;
      args[CHIT_MISS_ARG_ACCEL_STRUCT] = nir_load_param(b, vars->accel_struct_param);
      args[CHIT_MISS_ARG_CULL_MASK_AND_FLAGS] = nir_load_param(b, vars->cull_mask_and_flags_param);
      args[CHIT_MISS_ARG_SBT_OFFSET] = nir_load_param(b, vars->sbt_offset_param);
      args[CHIT_MISS_ARG_SBT_STRIDE] = nir_load_param(b, vars->sbt_stride_param);
      args[CHIT_MISS_ARG_MISS_INDEX] = nir_load_param(b, vars->miss_index_param);
      args[CHIT_MISS_ARG_RAY_ORIGIN] = nir_load_param(b, vars->ray_origin_param);
      args[CHIT_MISS_ARG_RAY_TMIN] = nir_load_param(b, vars->ray_tmin_param);
      args[CHIT_MISS_ARG_RAY_DIRECTION] = nir_load_param(b, vars->ray_direction_param);
      args[CHIT_MISS_ARG_RAY_TMAX] = intr->src[0].ssa;
      args[CHIT_MISS_ARG_PRIMITIVE_ADDR] = nir_undef(b, 1, 64);
      args[CHIT_MISS_ARG_PRIMITIVE_ID] = undef;
      args[CHIT_MISS_ARG_INSTANCE_ADDR] = nir_undef(b, 1, 64);
      args[CHIT_MISS_ARG_GEOMETRY_ID_AND_FLAGS] = undef;
      args[CHIT_MISS_ARG_HIT_KIND] = undef;
      for (unsigned i = 0; i < DIV_ROUND_UP(vars->payload_size, 4); ++i) {
         args[CHIT_MISS_ARG_PAYLOAD_BASE + i] =
            nir_instr_def(&nir_build_deref_cast(b, nir_load_param(b, TRAVERSAL_ARG_PAYLOAD_BASE + i),
                                                nir_var_shader_call_data, glsl_uint_type(), 4)
                              ->instr);
      }
      nir_build_indirect_call(b, vars->chit_miss_func, sbt_data.shader_addr, param_count, args);

      if (!(vars->flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR))
         nir_pop_if(b, NULL);

      break;
   }
   case nir_intrinsic_ignore_ray_intersection: {
      nir_store_deref(b, vars->accept, nir_imm_false(b), 0x1);

      /* The if is a workaround to avoid having to fix up control flow manually */
      nir_push_if(b, nir_imm_true(b));
      nir_jump(b, nir_jump_return);
      nir_pop_if(b, NULL);
      break;
   }
   case nir_intrinsic_terminate_ray: {
      nir_store_deref(b, vars->accept, nir_imm_true(b), 0x1);
      nir_store_deref(b, vars->terminate, nir_imm_true(b), 0x1);

      /* If we're compiling an intersection shader that has an inlined any-hit shader,
       * nir_lower_intersection_shader will already have inserted the return back to the
       * intersection shader for us. Only insert a return if we're compiling just an any-hit
       * shader.
       */
      if (b->shader->info.stage == MESA_SHADER_ANY_HIT) {
         /* The if is a workaround to avoid having to fix up control flow manually */
         nir_push_if(b, nir_imm_true(b));
         nir_jump(b, nir_jump_return);
         nir_pop_if(b, NULL);
      }
      break;
   }
   case nir_intrinsic_report_ray_intersection: {
      nir_store_deref(b, vars->accept, nir_imm_true(b), 0x1);
      nir_store_deref(b, vars->hit_kind, intr->src[1].ssa, 0x1);

      nir_store_deref(b, vars->committed_ray_tmax, intr->src[0].ssa, 0x1);
      nir_def *terminate_on_first_hit =
         nir_test_mask(b, nir_load_param(b, AHIT_ISEC_ARG_CULL_MASK_AND_FLAGS), SpvRayFlagsTerminateOnFirstHitKHRMask);
      nir_def *terminate = nir_ior(b, terminate_on_first_hit, nir_load_deref(b, vars->terminate));
      nir_store_deref(b, vars->terminate, terminate, 0x1);

      nir_push_if(b, terminate);
      nir_jump(b, nir_jump_return);
      nir_pop_if(b, NULL);
      break;
   }
   case nir_intrinsic_load_ray_triangle_vertex_positions: {
      nir_def *primitive_addr = nir_load_param(b, vars->primitive_addr_param);
      ret = radv_load_vertex_position(vars->device, b, primitive_addr, nir_intrinsic_column(intr));
      break;
   }
   default:
      return false;
   }

   if (ret)
      nir_def_rewrite_uses(&intr->def, ret);
   nir_instr_remove(&intr->instr);

   return true;
}

/* Lower aliased ray payload variables. See radv_nir_lower_rt_io for a more detailed
 * explanation on ray payload storage.
 */
static void
lower_rt_deref_var(nir_shader *shader, nir_function_impl *impl, nir_instr *instr, struct hash_table *cloned_vars)
{
   nir_deref_instr *deref = nir_instr_as_deref(instr);
   nir_variable *var = deref->var;
   struct hash_entry *entry = _mesa_hash_table_search(cloned_vars, var);
   if (!(var->data.mode & nir_var_function_temp) && !entry)
      return;

   hash_table_foreach (cloned_vars, cloned_entry) {
      if (var == cloned_entry->data)
         return;
   }

   nir_variable *new_var;
   if (entry) {
      new_var = entry->data;
   } else {
      new_var = nir_variable_clone(var, shader);
      _mesa_hash_table_insert(cloned_vars, var, new_var);

      exec_node_remove(&var->node);
      var->data.mode = nir_var_shader_temp;
      exec_list_push_tail(&shader->variables, &var->node);

      exec_list_push_tail(&impl->locals, &new_var->node);
   }

   deref->modes = nir_var_shader_temp;

   nir_foreach_use_safe (use, nir_instr_def(instr)) {
      if (nir_src_is_if(use))
         continue;

      nir_instr *parent = nir_src_parent_instr(use);
      if (parent->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(parent);
      if (intrin->intrinsic != nir_intrinsic_trace_ray && intrin->intrinsic != nir_intrinsic_execute_callable &&
          intrin->intrinsic != nir_intrinsic_execute_closest_hit_amd &&
          intrin->intrinsic != nir_intrinsic_execute_miss_amd)
         continue;

      nir_builder b = nir_builder_at(nir_before_instr(parent));
      nir_deref_instr *old_deref = nir_build_deref_var(&b, var);
      nir_deref_instr *new_deref = nir_build_deref_var(&b, new_var);

      nir_copy_deref(&b, new_deref, old_deref);
      b.cursor = nir_after_instr(parent);
      nir_copy_deref(&b, old_deref, new_deref);

      nir_src_rewrite(use, nir_instr_def(&new_deref->instr));
   }
}

static bool
lower_rt_derefs_functions(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   bool progress = false;

   struct hash_table *cloned_vars = _mesa_pointer_hash_table_create(shader);

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_deref)
            continue;

         nir_deref_instr *deref = nir_instr_as_deref(instr);
         if (!nir_deref_mode_is(deref, nir_var_function_temp))
            continue;

         if (deref->deref_type == nir_deref_type_var) {
            lower_rt_deref_var(shader, impl, instr, cloned_vars);
            progress = true;
         } else {
            assert(deref->deref_type != nir_deref_type_cast);
            /* Parent modes might have changed, propagate change */
            nir_deref_instr *parent = nir_src_as_deref(deref->parent);
            if (parent->modes != deref->modes)
               deref->modes = parent->modes;
         }
      }
   }

   return nir_progress(progress, impl, nir_metadata_control_flow);
}

void
radv_nir_lower_rt_io_functions(nir_shader *nir)
{
   /* When compiling separately and using function calls, function parameters for ray payloads store the currently
    * active ray payload. The same parameters are reused for different ray payload types - the function signature
    * only allows for passing one ray payload (the active one) at a time. We model this in NIR by designating all ray
    * payload variables as aliased (every ray payload variable's driver location is 0).
    *
    * This doesn't quite match the SPIR-V semantics of different ray payload variables - each payload variable is in
    * a different location and can be written/read independently. It's lower_rt_derefs's job to accomodate this.
    * lower_rt_derefs duplicates all ray payload variables and marks the original one as a shader_temp variable,
    * in order to make the shader's payload read/writes operate on temporary copies that do not alias.
    * radv_nir_lower_ray_payload_derefs will then convert the aliased variables to proper payload loads/stores, which
    * later get lowered to function call parameters by `lower_rt_storage`.
    */
   NIR_PASS(_, nir, lower_rt_derefs_functions);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, radv_nir_lower_ray_payload_derefs, 0);
}

nir_function_impl *
radv_get_rt_shader_entrypoint(nir_shader *shader)
{
   nir_foreach_function_impl (impl, shader)
      if (impl->function->is_entrypoint || impl->function->is_exported)
         return impl;
   return NULL;
}

void
radv_nir_lower_rt_abi_functions(nir_shader *shader, const struct radv_shader_info *info, uint32_t payload_size,
                                uint32_t hit_attrib_size, struct radv_device *device,
                                struct radv_ray_tracing_pipeline *pipeline)
{
   const bool uses_descriptor_heap = pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_function *entrypoint_function = impl->function;

   if (radv_is_traversal_shader(shader))
      radv_nir_init_traversal_params(entrypoint_function, payload_size, uses_descriptor_heap);
   else
      radv_nir_init_rt_function_params(entrypoint_function, shader->info.stage, payload_size, hit_attrib_size,
                                       uses_descriptor_heap);

   struct rt_variables vars =
      create_rt_variables(shader, device, pipeline->base.base.create_flags, payload_size, hit_attrib_size);

   nir_builder b = nir_builder_at(nir_before_impl(impl));
   unsigned num_hit_attribs = DIV_ROUND_UP(hit_attrib_size, 4);
   if ((shader->info.stage == MESA_SHADER_INTERSECTION || shader->info.stage == MESA_SHADER_ANY_HIT) &&
       !radv_is_traversal_shader(shader)) {
      vars.hit_kind =
         nir_build_deref_cast(&b, nir_load_param(&b, AHIT_ISEC_ARG_HIT_KIND), nir_var_shader_temp, glsl_uint_type(), 4);
      vars.accept =
         nir_build_deref_cast(&b, nir_load_param(&b, AHIT_ISEC_ARG_ACCEPT), nir_var_shader_temp, glsl_bool_type(), 4);
      vars.terminate = nir_build_deref_cast(&b, nir_load_param(&b, AHIT_ISEC_ARG_TERMINATE), nir_var_shader_temp,
                                            glsl_bool_type(), 4);
      vars.committed_ray_tmax = nir_build_deref_cast(&b, nir_load_param(&b, AHIT_ISEC_ARG_COMMITTED_RAY_TMAX),
                                                     nir_var_shader_temp, glsl_float_type(), 4);
   }

   nir_shader_instructions_pass(shader, lower_rt_instruction, nir_metadata_none, &vars);

   /* This can't use NIR_PASS because NIR_DEBUG=serialize,clone invalidates pointers. */
   nir_lower_returns(shader);

   b.cursor = nir_before_impl(impl);
   nir_deref_instr **payload_in_storage =
      rzalloc_array_size(shader, sizeof(nir_deref_instr *), DIV_ROUND_UP(payload_size, 4));
   nir_deref_instr **hit_attrib_storage = NULL;

   if (vars.in_payload_base_param != -1u) {
      for (unsigned i = 0; i < DIV_ROUND_UP(payload_size, 4); ++i) {
         payload_in_storage[i] = nir_build_deref_cast(&b, nir_load_param(&b, vars.in_payload_base_param + i),
                                                      nir_var_shader_call_data, glsl_uint_type(), 4);
      }
   }
   if ((shader->info.stage == MESA_SHADER_INTERSECTION || shader->info.stage == MESA_SHADER_ANY_HIT) &&
       !radv_is_traversal_shader(shader)) {
      hit_attrib_storage = rzalloc_array_size(shader, sizeof(nir_deref_instr *), num_hit_attribs);
      for (unsigned i = 0; i < num_hit_attribs; ++i) {
         hit_attrib_storage[i] = nir_build_deref_cast(&b, nir_load_param(&b, AHIT_ISEC_ARG_HIT_ATTRIB_PAYLOAD_BASE + i),
                                                      nir_var_shader_temp, glsl_uint_type(), 4);
      }
   }

   nir_progress(true, impl, nir_metadata_none);

   /* cleanup passes */
   NIR_PASS(_, shader, radv_nir_lower_rt_storage, hit_attrib_storage, payload_in_storage, vars.out_payload_storage,
            info->wave_size);
   NIR_PASS(_, shader, nir_remove_dead_derefs);
   NIR_PASS(_, shader, nir_remove_dead_variables, nir_var_function_temp | nir_var_shader_call_data, NULL);
   NIR_PASS(_, shader, nir_lower_global_vars_to_local);
   NIR_PASS(_, shader, nir_lower_vars_to_ssa);
}
