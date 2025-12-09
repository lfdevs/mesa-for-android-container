/*
 * Copyright © 2025 Valve Corporation
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_nir_rt_common.h"
#include "nir/radv_nir_rt_stage_common.h"
#include "nir/radv_nir_rt_stage_monolithic.h"
#include "nir_builder.h"
#include "radv_device.h"
#include "radv_physical_device.h"

struct chit_miss_inlining_params {
   struct radv_device *device;

   struct radv_nir_rt_traversal_params *trav_params;
   struct radv_nir_rt_traversal_result *trav_result;
   struct radv_nir_sbt_data *sbt;

   unsigned payload_offset;
};

struct chit_miss_inlining_vars {
   struct radv_device *device;

   nir_variable *shader_record_ptr;
   nir_variable *origin;
   nir_variable *direction;
   nir_variable *tmin;
   nir_variable *tmax;
   nir_variable *primitive_addr;
   nir_variable *primitive_id;
   nir_variable *geometry_id_and_flags;
   nir_variable *cull_mask_and_flags;
   nir_variable *instance_addr;
   nir_variable *hit_kind;
};

static void
init_chit_miss_inlining_vars(nir_shader *shader, struct chit_miss_inlining_vars *vars)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);

   vars->shader_record_ptr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "shader_record_ptr");
   vars->origin = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "origin");
   vars->direction = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "direction");
   vars->tmin = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "tmin");
   vars->tmax = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "tmax");
   vars->primitive_addr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "primitive_addr");
   vars->primitive_id = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "primitive_id");
   vars->geometry_id_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "geometry_id_and_flags");
   vars->cull_mask_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "cull_mask_and_flags");
   vars->instance_addr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
   vars->hit_kind = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "hit_kind");
}

static void
setup_chit_miss_inlining(struct chit_miss_inlining_vars *vars, const struct chit_miss_inlining_params *params,
                         nir_builder *b, nir_shader *chit, struct hash_table *var_remap)
{
   nir_shader *inline_target = b->shader;

   struct chit_miss_inlining_vars dst_vars;

   init_chit_miss_inlining_vars(inline_target, &dst_vars);
   init_chit_miss_inlining_vars(chit, vars);

   dst_vars.tmax = params->trav_result->tmax;
   dst_vars.primitive_addr = params->trav_result->primitive_addr;
   dst_vars.primitive_id = params->trav_result->primitive_id;
   dst_vars.geometry_id_and_flags = params->trav_result->geometry_id_and_flags;
   dst_vars.instance_addr = params->trav_result->instance_addr;
   dst_vars.hit_kind = params->trav_result->hit_kind;

   nir_store_var(b, dst_vars.shader_record_ptr, params->sbt->shader_record_ptr, 0x1);
   nir_store_var(b, dst_vars.origin, params->trav_params->origin, 0x7);
   nir_store_var(b, dst_vars.direction, params->trav_params->direction, 0x7);
   nir_store_var(b, dst_vars.tmin, params->trav_params->tmin, 0x1);
   nir_store_var(b, dst_vars.cull_mask_and_flags, params->trav_params->cull_mask_and_flags, 0x1);

   _mesa_hash_table_insert(var_remap, vars->shader_record_ptr, dst_vars.shader_record_ptr);
   _mesa_hash_table_insert(var_remap, vars->origin, dst_vars.origin);
   _mesa_hash_table_insert(var_remap, vars->direction, dst_vars.direction);
   _mesa_hash_table_insert(var_remap, vars->tmin, dst_vars.tmin);
   _mesa_hash_table_insert(var_remap, vars->tmax, dst_vars.tmax);
   _mesa_hash_table_insert(var_remap, vars->primitive_addr, dst_vars.primitive_addr);
   _mesa_hash_table_insert(var_remap, vars->primitive_id, dst_vars.primitive_id);
   _mesa_hash_table_insert(var_remap, vars->geometry_id_and_flags, dst_vars.geometry_id_and_flags);
   _mesa_hash_table_insert(var_remap, vars->cull_mask_and_flags, dst_vars.cull_mask_and_flags);
   _mesa_hash_table_insert(var_remap, vars->instance_addr, dst_vars.instance_addr);
   _mesa_hash_table_insert(var_remap, vars->hit_kind, dst_vars.hit_kind);
}

static bool
lower_rt_instruction_chit_miss(nir_builder *b, nir_intrinsic_instr *intr, void *_vars)
{
   struct chit_miss_inlining_vars *vars = _vars;

   b->cursor = nir_after_instr(&intr->instr);

   nir_def *ret = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ray_world_origin:
      ret = nir_load_var(b, vars->origin);
      break;
   case nir_intrinsic_load_ray_world_direction:
      ret = nir_load_var(b, vars->direction);
      break;
   case nir_intrinsic_load_shader_record_ptr:
      ret = nir_load_var(b, vars->shader_record_ptr);
      break;
   case nir_intrinsic_load_ray_t_max:
      ret = nir_load_var(b, vars->tmax);
      break;
   case nir_intrinsic_load_ray_t_min:
      ret = nir_load_var(b, vars->tmin);
      break;
   case nir_intrinsic_load_ray_instance_custom_index:
      ret = radv_load_custom_instance(vars->device, b, nir_load_var(b, vars->instance_addr));
      break;
   case nir_intrinsic_load_primitive_id:
      ret = nir_load_var(b, vars->primitive_id);
      break;
   case nir_intrinsic_load_instance_id:
      ret = radv_load_instance_id(vars->device, b, nir_load_var(b, vars->instance_addr));
      break;
   case nir_intrinsic_load_ray_hit_kind:
      ret = nir_load_var(b, vars->hit_kind);
      break;
   case nir_intrinsic_load_ray_flags:
      ret = nir_iand_imm(b, nir_load_var(b, vars->cull_mask_and_flags), 0xFFFFFF);
      break;
   case nir_intrinsic_load_cull_mask:
      ret = nir_ushr_imm(b, nir_load_var(b, vars->cull_mask_and_flags), 24);
      break;
   case nir_intrinsic_load_ray_geometry_index: {
      ret = nir_load_var(b, vars->geometry_id_and_flags);
      ret = nir_iand_imm(b, ret, 0xFFFFFFF);
      break;
   }
   case nir_intrinsic_load_ray_world_to_object: {
      unsigned c = nir_intrinsic_column(intr);
      nir_def *instance_node_addr = nir_load_var(b, vars->instance_addr);
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
      radv_load_otw_matrix(vars->device, b, nir_load_var(b, vars->instance_addr), otw_matrix);
      ret = nir_vec3(b, nir_channel(b, otw_matrix[0], c), nir_channel(b, otw_matrix[1], c),
                     nir_channel(b, otw_matrix[2], c));
      break;
   }
   case nir_intrinsic_load_ray_object_origin: {
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(vars->device, b, nir_load_var(b, vars->instance_addr), wto_matrix);
      ret = nir_build_vec3_mat_mult(b, nir_load_var(b, vars->origin), wto_matrix, true);
      break;
   }
   case nir_intrinsic_load_ray_object_direction: {
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(vars->device, b, nir_load_var(b, vars->instance_addr), wto_matrix);
      ret = nir_build_vec3_mat_mult(b, nir_load_var(b, vars->direction), wto_matrix, false);
      break;
   }
   case nir_intrinsic_load_ray_triangle_vertex_positions: {
      nir_def *primitive_addr = nir_load_var(b, vars->primitive_addr);
      ret = radv_load_vertex_position(vars->device, b, primitive_addr, nir_intrinsic_column(intr));
      break;
   }
   default:
      return false;
   }

   nir_def_replace(&intr->def, ret);
   return true;
}

static void
radv_ray_tracing_group_chit_info(struct radv_ray_tracing_group *group, uint32_t *shader_index, uint32_t *handle_index,
                                 struct radv_rt_case_data *data)
{
   if (group->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR) {
      *shader_index = group->recursive_shader;
      *handle_index = group->handle.closest_hit_index;
   }
}

static void
radv_ray_tracing_group_miss_info(struct radv_ray_tracing_group *group, uint32_t *shader_index, uint32_t *handle_index,
                                 struct radv_rt_case_data *data)
{
   if (group->type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR) {
      if (data->pipeline->stages[group->recursive_shader].stage != MESA_SHADER_MISS)
         return;

      *shader_index = group->recursive_shader;
      *handle_index = group->handle.general_index;
   }
}

static void
preprocess_shader_cb_monolithic(nir_shader *nir, void *_data)
{
   uint32_t *payload_offset = _data;

   NIR_PASS(_, nir, radv_nir_lower_ray_payload_derefs, *payload_offset);
}

void
radv_nir_lower_rt_io_monolithic(nir_shader *nir)
{
   uint32_t raygen_payload_offset = 0;
   preprocess_shader_cb_monolithic(nir, &raygen_payload_offset);
}

struct rt_variables {
   struct radv_device *device;
   const VkPipelineCreateFlags2 flags;

   uint32_t payload_offset;
   unsigned stack_size;

   nir_def *launch_sizes[3];
   nir_def *launch_ids[3];
   nir_def *shader_record_ptr;

   nir_variable *stack_ptr;
};

static void
radv_build_recursive_case(nir_builder *b, nir_def *idx, struct radv_ray_tracing_group *group,
                          struct radv_rt_case_data *data)
{
   nir_shader *shader =
      radv_pipeline_cache_handle_to_nir(data->device, data->pipeline->stages[group->recursive_shader].nir);
   assert(shader);

   struct chit_miss_inlining_params *params = data->param_data;

   struct chit_miss_inlining_vars vars = {
      .device = params->device,
   };

   struct hash_table *var_remap = _mesa_pointer_hash_table_create(NULL);
   setup_chit_miss_inlining(&vars, params, b, shader, var_remap);

   nir_opt_dead_cf(shader);

   preprocess_shader_cb_monolithic(shader, &params->payload_offset);

   nir_shader_intrinsics_pass(shader, lower_rt_instruction_chit_miss, nir_metadata_control_flow, &vars);

   nir_lower_returns(shader);
   nir_opt_dce(shader);

   radv_nir_inline_constants(b->shader, shader);

   nir_push_if(b, nir_ieq_imm(b, idx, group->handle.general_index));
   nir_inline_function_impl(b, nir_shader_get_entrypoint(shader), NULL, var_remap);
   nir_pop_if(b, NULL);
   ralloc_free(shader);
}

struct lower_rt_instruction_monolithic_state {
   struct radv_device *device;
   struct radv_ray_tracing_pipeline *pipeline;
   const VkRayTracingPipelineCreateInfoKHR *pCreateInfo;

   struct rt_variables *vars;
};

static bool
lower_rt_call_monolithic(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   b->cursor = nir_after_instr(&intr->instr);

   struct lower_rt_instruction_monolithic_state *state = data;
   const struct radv_physical_device *pdev = radv_device_physical(state->device);
   struct rt_variables *vars = state->vars;

   switch (intr->intrinsic) {
   case nir_intrinsic_execute_callable:
      /* It's allowed to place OpExecuteCallableKHR in a SPIR-V, even if the RT pipeline doesn't contain
       * any callable shaders. However, it's impossible to execute the instruction in a valid way, so just remove any
       * nir_intrinsic_execute_callable we encounter.
       */
      nir_instr_remove(&intr->instr);
      return true;
   case nir_intrinsic_trace_ray: {
      vars->payload_offset = nir_src_as_uint(intr->src[10]);

      nir_src cull_mask = intr->src[2];
      bool ignore_cull_mask = nir_src_is_const(cull_mask) && (nir_src_as_uint(cull_mask) & 0xFF) == 0xFF;

      /* Per the SPIR-V extension spec we have to ignore some bits for some arguments. */
      struct radv_nir_rt_traversal_params params = {
         .accel_struct = intr->src[0].ssa,
         .cull_mask_and_flags = nir_ior(b, nir_ishl_imm(b, cull_mask.ssa, 24), intr->src[1].ssa),
         .sbt_offset = nir_iand_imm(b, intr->src[3].ssa, 0xf),
         .sbt_stride = nir_iand_imm(b, intr->src[4].ssa, 0xf),
         .miss_index = nir_iand_imm(b, intr->src[5].ssa, 0xffff),
         .origin = intr->src[6].ssa,
         .tmin = intr->src[7].ssa,
         .direction = intr->src[8].ssa,
         .tmax = intr->src[9].ssa,
         .ignore_cull_mask = ignore_cull_mask,
         .preprocess_ahit_isec = preprocess_shader_cb_monolithic,
         .cb_data = &vars->payload_offset,
      };

      nir_def *stack_ptr = nir_load_var(b, vars->stack_ptr);
      nir_store_var(b, vars->stack_ptr, nir_iadd_imm(b, stack_ptr, b->shader->scratch_size), 0x1);

      struct radv_nir_rt_traversal_result result =
         radv_build_traversal(state->device, state->pipeline, b, &params, NULL);

      nir_store_var(b, vars->stack_ptr, stack_ptr, 0x1);

      struct chit_miss_inlining_params inline_params = {
         .device = state->device,
         .trav_params = &params,
         .trav_result = &result,
         .payload_offset = vars->payload_offset,
      };

      struct radv_rt_case_data case_data = {
         .device = state->device,
         .pipeline = state->pipeline,
         .param_data = &inline_params,
      };

      nir_push_if(b, nir_load_var(b, result.hit));
      {
         struct radv_nir_sbt_data hit_sbt =
            radv_nir_load_sbt_entry(b, nir_load_var(b, result.sbt_index), SBT_HIT, SBT_CLOSEST_HIT_IDX);
         inline_params.sbt = &hit_sbt;

         nir_def *should_return = nir_test_mask(b, params.cull_mask_and_flags, SpvRayFlagsSkipClosestHitShaderKHRMask);

         /* should_return is set if we had a hit but we won't be calling the closest hit
          * shader and hence need to return immediately to the calling shader. */
         nir_push_if(b, nir_inot(b, should_return));

         radv_visit_inlined_shaders(b, hit_sbt.shader_addr,
                                    !(state->pipeline->base.base.create_flags &
                                      VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR),
                                    &case_data, radv_ray_tracing_group_chit_info, radv_build_recursive_case);

         nir_pop_if(b, NULL);
      }
      nir_push_else(b, NULL);
      {
         struct radv_nir_sbt_data miss_sbt = radv_nir_load_sbt_entry(b, params.miss_index, SBT_MISS, SBT_GENERAL_IDX);
         inline_params.sbt = &miss_sbt;

         radv_visit_inlined_shaders(b, miss_sbt.shader_addr,
                                    !(vars->flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR),
                                    &case_data, radv_ray_tracing_group_miss_info, radv_build_recursive_case);
      }
      nir_pop_if(b, NULL);

      b->shader->info.shared_size =
         MAX2(b->shader->info.shared_size, pdev->rt_wave_size * MAX_STACK_ENTRY_COUNT * sizeof(uint32_t));

      nir_instr_remove(&intr->instr);
      return true;
   }
   default:
      return false;
   }
}

static bool
lower_rt_instruction_monolithic(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   b->cursor = nir_before_instr(&intr->instr);

   struct lower_rt_instruction_monolithic_state *state = data;
   struct rt_variables *vars = state->vars;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_shader_record_ptr: {
      nir_def_replace(&intr->def, vars->shader_record_ptr);
      return true;
   }
   case nir_intrinsic_load_ray_launch_size: {
      nir_def_replace(&intr->def, nir_vec(b, vars->launch_sizes, 3));
      return true;
   };
   case nir_intrinsic_load_ray_launch_id: {
      nir_def_replace(&intr->def, nir_vec(b, vars->launch_ids, 3));
      return true;
   }
   case nir_intrinsic_load_scratch: {
      nir_src_rewrite(&intr->src[0], nir_iadd_nuw(b, nir_load_var(b, vars->stack_ptr), intr->src[0].ssa));
      return true;
   }
   case nir_intrinsic_store_scratch: {
      nir_src_rewrite(&intr->src[1], nir_iadd_nuw(b, nir_load_var(b, vars->stack_ptr), intr->src[1].ssa));
      return true;
   }
   default:
      return false;
   }
}

static bool
radv_count_hit_attrib_slots(nir_builder *b, nir_intrinsic_instr *instr, void *data)
{
   uint32_t *count = data;
   if (instr->intrinsic == nir_intrinsic_load_hit_attrib_amd || instr->intrinsic == nir_intrinsic_store_hit_attrib_amd)
      *count = MAX2(*count, nir_intrinsic_base(instr) + 1);

   return false;
}

void
radv_nir_lower_rt_abi_monolithic(nir_shader *shader, const struct radv_shader_args *args, uint32_t *stack_size,
                                 struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_builder b = nir_builder_at(nir_before_impl(impl));

   struct rt_variables vars = {
      .device = device,
      .flags = pipeline->base.base.create_flags,
   };

   vars.stack_ptr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "stack_ptr");

   for (uint32_t i = 0; i < ARRAY_SIZE(vars.launch_sizes); i++)
      vars.launch_sizes[i] = ac_nir_load_arg(&b, &args->ac, args->ac.rt.launch_sizes[i]);
   for (uint32_t i = 0; i < ARRAY_SIZE(vars.launch_sizes); i++) {
      vars.launch_ids[i] = ac_nir_load_arg(&b, &args->ac, args->ac.rt.launch_ids[i]);
   }
   nir_def *record_ptr = ac_nir_load_arg(&b, &args->ac, args->ac.rt.shader_record);
   vars.shader_record_ptr = nir_pack_64_2x32(&b, record_ptr);
   nir_def *stack_ptr = ac_nir_load_arg(&b, &args->ac, args->ac.rt.dynamic_callable_stack_base);
   nir_store_var(&b, vars.stack_ptr, stack_ptr, 0x1);

   struct lower_rt_instruction_monolithic_state state = {
      .device = device,
      .pipeline = pipeline,
      .vars = &vars,
   };
   nir_shader_intrinsics_pass(shader, lower_rt_call_monolithic, nir_metadata_none, &state);
   nir_shader_intrinsics_pass(shader, lower_rt_instruction_monolithic, nir_metadata_none, &state);

   nir_index_ssa_defs(impl);

   uint32_t hit_attrib_count = 0;
   nir_shader_intrinsics_pass(shader, radv_count_hit_attrib_slots, nir_metadata_all, &hit_attrib_count);
   /* Register storage for hit attributes */
   STACK_ARRAY(nir_variable *, hit_attribs, hit_attrib_count);
   for (uint32_t i = 0; i < hit_attrib_count; i++)
      hit_attribs[i] = nir_local_variable_create(impl, glsl_uint_type(), "ahit_attrib");

   radv_nir_lower_hit_attribs(shader, hit_attribs, 0);

   vars.stack_size = MAX2(vars.stack_size, shader->scratch_size);
   *stack_size = MAX2(*stack_size, vars.stack_size);
   shader->scratch_size = 0;

   nir_progress(true, impl, nir_metadata_none);

   /* cleanup passes */
   NIR_PASS(_, shader, nir_lower_returns);
   NIR_PASS(_, shader, nir_lower_global_vars_to_local);
   NIR_PASS(_, shader, nir_lower_vars_to_ssa);

   STACK_ARRAY_FINISH(hit_attribs);
}
