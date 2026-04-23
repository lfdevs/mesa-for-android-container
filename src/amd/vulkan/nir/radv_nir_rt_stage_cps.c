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
#include "nir/radv_nir_rt_stage_cps.h"

#include "ac_nir.h"
#include "aco_nir_call_attribs.h"
#include "radv_device.h"
#include "radv_nir_rt_stage_functions.h"
#include "radv_physical_device.h"
#include "radv_pipeline_rt.h"
#include "radv_shader.h"

static bool
radv_arg_def_is_unused(nir_def *def)
{
   nir_foreach_use (use, def) {
      nir_instr *use_instr = nir_src_parent_instr(use);
      if (use_instr->type == nir_instr_type_call)
         continue;
      if (use_instr->type == nir_instr_type_phi) {
         nir_cf_node *prev_node = nir_cf_node_prev(&use_instr->block->cf_node);
         if (!prev_node)
            return false;

         nir_phi_instr *phi = nir_instr_as_phi(use_instr);
         if (radv_arg_def_is_unused(&phi->def))
            continue;
      }

      return false;
   }

   return true;
}

static bool
radv_gather_unused_args_instr(nir_builder *b, nir_intrinsic_instr *instr, void *data)
{
   if (instr->intrinsic != nir_intrinsic_load_param)
      return false;

   if (!radv_arg_def_is_unused(&instr->def)) {
      /* This arg is used for more than passing data to the next stage. */
      struct radv_ray_tracing_stage_info *info = data;
      BITSET_CLEAR(info->unused_args, nir_intrinsic_param_idx(instr));
   }

   return false;
}

void
radv_gather_unused_args(struct radv_ray_tracing_stage_info *info, nir_shader *nir)
{
   nir_shader_intrinsics_pass(nir, radv_gather_unused_args_instr, nir_metadata_all, info);
}

/*
 * Global variables for an RT pipeline
 */
struct rt_variables {
   struct radv_device *device;
   const VkPipelineCreateFlags2 flags;

   nir_variable *shader_addr;
   nir_variable *traversal_addr;

   /* scratch offset of the argument area relative to stack_ptr */
   nir_variable *arg;
   nir_variable *stack_ptr;

   nir_variable *launch_sizes[3];
   nir_variable *launch_ids[3];

   /* global address of the SBT entry used for the shader */
   nir_variable *shader_record_ptr;

   /* trace_ray arguments */
   nir_variable *accel_struct;
   nir_variable *cull_mask_and_flags;
   nir_variable *sbt_offset;
   nir_variable *sbt_stride;
   nir_variable *miss_index;
   nir_variable *origin;
   nir_variable *tmin;
   nir_variable *direction;
   nir_variable *tmax;

   /* Properties of the primitive currently being visited. */
   nir_variable *primitive_addr;
   nir_variable *primitive_id;
   nir_variable *geometry_id_and_flags;
   nir_variable *instance_addr;
   nir_variable *hit_kind;

   unsigned stack_size;
};

static struct rt_variables
create_rt_variables(nir_shader *shader, struct radv_device *device, const VkPipelineCreateFlags2 flags)
{
   struct rt_variables vars = {
      .device = device,
      .flags = flags,
   };
   vars.shader_addr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "shader_addr");
   vars.traversal_addr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "traversal_addr");
   vars.arg = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "arg");
   vars.stack_ptr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "stack_ptr");
   vars.shader_record_ptr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "shader_record_ptr");

   vars.launch_sizes[0] = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "launch_size_x");
   vars.launch_sizes[1] = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "launch_size_y");
   vars.launch_sizes[2] = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "launch_size_z");

   vars.launch_ids[0] = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "launch_id_x");
   vars.launch_ids[1] = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "launch_id_y");
   vars.launch_ids[2] = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "launch_id_z");

   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   vars.accel_struct = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "accel_struct");
   vars.cull_mask_and_flags = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "cull_mask_and_flags");
   vars.sbt_offset = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "sbt_offset");
   vars.sbt_stride = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "sbt_stride");
   vars.miss_index = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "miss_index");
   vars.origin = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "ray_origin");
   vars.tmin = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "ray_tmin");
   vars.direction = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "ray_direction");
   vars.tmax = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "ray_tmax");

   vars.primitive_addr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "primitive_addr");
   vars.primitive_id = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "primitive_id");
   vars.geometry_id_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "geometry_id_and_flags");
   vars.instance_addr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
   vars.hit_kind = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "hit_kind");

   return vars;
}

static void
insert_rt_return(nir_builder *b, const struct rt_variables *vars)
{
   nir_store_var(b, vars->stack_ptr, nir_iadd_imm(b, nir_load_var(b, vars->stack_ptr), -16), 1);
   nir_store_var(b, vars->shader_addr, nir_load_scratch(b, 1, 64, nir_load_var(b, vars->stack_ptr), .align_mul = 16),
                 1);
}

struct radv_rt_shader_info {
   bool uses_launch_id;
   bool uses_launch_size;
};

struct radv_lower_rt_instruction_data {
   struct rt_variables *vars;
   struct radv_rt_shader_info *out_info;
};

static bool
radv_lower_rt_instruction(nir_builder *b, nir_instr *instr, void *_data)
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

   struct radv_lower_rt_instruction_data *data = _data;
   struct rt_variables *vars = data->vars;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *ret = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_rt_execute_callable: {
      uint32_t size = align(nir_intrinsic_stack_size(intr), 16);
      nir_def *ret_ptr = nir_load_resume_shader_address_amd(b, nir_intrinsic_call_idx(intr));

      nir_store_var(b, vars->stack_ptr, nir_iadd_imm_nuw(b, nir_load_var(b, vars->stack_ptr), size), 1);
      nir_store_scratch(b, ret_ptr, nir_load_var(b, vars->stack_ptr), .align_mul = 16);

      nir_store_var(b, vars->stack_ptr, nir_iadd_imm_nuw(b, nir_load_var(b, vars->stack_ptr), 16), 1);
      struct radv_nir_sbt_data sbt_data = radv_nir_load_sbt_entry(b, nir_load_param(b, RT_ARG_SBT_DESCRIPTORS),
                                                                  intr->src[0].ssa, SBT_CALLABLE, SBT_RECURSIVE_PTR);

      nir_store_var(b, vars->shader_addr, sbt_data.shader_addr, 0x1);
      nir_store_var(b, vars->shader_record_ptr, sbt_data.shader_record_ptr, 0x1);
      nir_store_var(b, vars->arg, nir_iadd_imm(b, intr->src[1].ssa, -size - 16), 1);

      vars->stack_size = MAX2(vars->stack_size, size + 16);
      break;
   }
   case nir_intrinsic_rt_trace_ray: {
      uint32_t size = align(nir_intrinsic_stack_size(intr), 16);
      nir_def *ret_ptr = nir_load_resume_shader_address_amd(b, nir_intrinsic_call_idx(intr));

      nir_store_var(b, vars->stack_ptr, nir_iadd_imm_nuw(b, nir_load_var(b, vars->stack_ptr), size), 1);
      nir_store_scratch(b, ret_ptr, nir_load_var(b, vars->stack_ptr), .align_mul = 16);

      nir_store_var(b, vars->stack_ptr, nir_iadd_imm_nuw(b, nir_load_var(b, vars->stack_ptr), 16), 1);

      nir_store_var(b, vars->shader_addr, nir_load_var(b, vars->traversal_addr), 1);
      nir_store_var(b, vars->arg, nir_iadd_imm(b, intr->src[10].ssa, -size - 16), 1);

      vars->stack_size = MAX2(vars->stack_size, size + 16);

      /* Per the SPIR-V extension spec we have to ignore some bits for some arguments. */
      nir_store_var(b, vars->accel_struct, intr->src[0].ssa, 0x1);
      nir_store_var(b, vars->cull_mask_and_flags, nir_ior(b, nir_ishl_imm(b, intr->src[2].ssa, 24), intr->src[1].ssa),
                    0x1);
      nir_store_var(b, vars->sbt_offset, nir_iand_imm(b, intr->src[3].ssa, 0xf), 0x1);
      nir_store_var(b, vars->sbt_stride, nir_iand_imm(b, intr->src[4].ssa, 0xf), 0x1);
      nir_store_var(b, vars->miss_index, nir_iand_imm(b, intr->src[5].ssa, 0xffff), 0x1);
      nir_store_var(b, vars->origin, intr->src[6].ssa, 0x7);
      nir_store_var(b, vars->tmin, intr->src[7].ssa, 0x1);
      nir_store_var(b, vars->direction, intr->src[8].ssa, 0x7);
      nir_store_var(b, vars->tmax, intr->src[9].ssa, 0x1);
      break;
   }
   case nir_intrinsic_rt_resume: {
      uint32_t size = align(nir_intrinsic_stack_size(intr), 16);

      nir_store_var(b, vars->stack_ptr, nir_iadd_imm(b, nir_load_var(b, vars->stack_ptr), -size), 1);
      break;
   }
   case nir_intrinsic_rt_return_amd: {
      if (b->shader->info.stage == MESA_SHADER_RAYGEN) {
         nir_terminate(b);
         break;
      }
      insert_rt_return(b, vars);
      break;
   }
   case nir_intrinsic_load_scratch: {
      nir_src_rewrite(&intr->src[0], nir_iadd_nuw(b, nir_load_var(b, vars->stack_ptr), intr->src[0].ssa));
      return true;
   }
   case nir_intrinsic_store_scratch: {
      nir_src_rewrite(&intr->src[1], nir_iadd_nuw(b, nir_load_var(b, vars->stack_ptr), intr->src[1].ssa));
      return true;
   }
   case nir_intrinsic_load_rt_arg_scratch_offset_amd: {
      ret = nir_load_var(b, vars->arg);
      break;
   }
   case nir_intrinsic_load_shader_record_ptr: {
      ret = nir_load_var(b, vars->shader_record_ptr);
      break;
   }
   case nir_intrinsic_load_ray_launch_size: {
      if (data->out_info)
         data->out_info->uses_launch_size = true;

      ret = nir_vec3(b, nir_load_var(b, vars->launch_sizes[0]), nir_load_var(b, vars->launch_sizes[1]),
                     nir_load_var(b, vars->launch_sizes[2]));
      break;
   };
   case nir_intrinsic_load_ray_launch_id: {
      if (data->out_info)
         data->out_info->uses_launch_id = true;

      ret = nir_vec3(b, nir_load_var(b, vars->launch_ids[0]), nir_load_var(b, vars->launch_ids[1]),
                     nir_load_var(b, vars->launch_ids[2]));
      break;
   }
   case nir_intrinsic_load_ray_t_min: {
      ret = nir_load_var(b, vars->tmin);
      break;
   }
   case nir_intrinsic_load_ray_t_max: {
      ret = nir_load_var(b, vars->tmax);
      break;
   }
   case nir_intrinsic_load_ray_world_origin: {
      ret = nir_load_var(b, vars->origin);
      break;
   }
   case nir_intrinsic_load_ray_world_direction: {
      ret = nir_load_var(b, vars->direction);
      break;
   }
   case nir_intrinsic_load_ray_instance_custom_index: {
      ret = radv_load_custom_instance(vars->device, b, nir_load_var(b, vars->instance_addr));
      break;
   }
   case nir_intrinsic_load_primitive_id: {
      ret = nir_load_var(b, vars->primitive_id);
      break;
   }
   case nir_intrinsic_load_ray_geometry_index: {
      ret = nir_load_var(b, vars->geometry_id_and_flags);
      ret = nir_iand_imm(b, ret, 0xFFFFFFF);
      break;
   }
   case nir_intrinsic_load_instance_id: {
      ret = radv_load_instance_id(vars->device, b, nir_load_var(b, vars->instance_addr));
      break;
   }
   case nir_intrinsic_load_ray_flags: {
      ret = nir_iand_imm(b, nir_load_var(b, vars->cull_mask_and_flags), 0xFFFFFF);
      break;
   }
   case nir_intrinsic_load_ray_hit_kind: {
      ret = nir_load_var(b, vars->hit_kind);
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
   case nir_intrinsic_load_cull_mask: {
      ret = nir_ushr_imm(b, nir_load_var(b, vars->cull_mask_and_flags), 24);
      break;
   }
   case nir_intrinsic_load_sbt_base_amd: {
      ret = nir_load_param(b, RT_ARG_SBT_DESCRIPTORS);
      break;
   }
   case nir_intrinsic_load_sbt_offset_amd: {
      ret = nir_load_var(b, vars->sbt_offset);
      break;
   }
   case nir_intrinsic_load_sbt_stride_amd: {
      ret = nir_load_var(b, vars->sbt_stride);
      break;
   }
   case nir_intrinsic_load_accel_struct_amd: {
      ret = nir_load_var(b, vars->accel_struct);
      break;
   }
   case nir_intrinsic_load_cull_mask_and_flags_amd: {
      ret = nir_load_var(b, vars->cull_mask_and_flags);
      break;
   }
   case nir_intrinsic_execute_closest_hit_amd: {
      nir_store_var(b, vars->tmax, intr->src[1].ssa, 0x1);
      nir_store_var(b, vars->primitive_addr, intr->src[2].ssa, 0x1);
      nir_store_var(b, vars->primitive_id, intr->src[3].ssa, 0x1);
      nir_store_var(b, vars->instance_addr, intr->src[4].ssa, 0x1);
      nir_store_var(b, vars->geometry_id_and_flags, intr->src[5].ssa, 0x1);
      nir_store_var(b, vars->hit_kind, intr->src[6].ssa, 0x1);

      struct radv_nir_sbt_data sbt_data = radv_nir_load_sbt_entry(b, nir_load_param(b, RT_ARG_SBT_DESCRIPTORS),
                                                                  intr->src[0].ssa, SBT_HIT, SBT_RECURSIVE_PTR);
      nir_store_var(b, vars->shader_addr, sbt_data.shader_addr, 0x1);
      nir_store_var(b, vars->shader_record_ptr, sbt_data.shader_record_ptr, 0x1);

      nir_def *should_return =
         nir_test_mask(b, nir_load_var(b, vars->cull_mask_and_flags), SpvRayFlagsSkipClosestHitShaderKHRMask);

      if (!(vars->flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR)) {
         should_return = nir_ior(b, should_return, nir_ieq_imm(b, nir_load_var(b, vars->shader_addr), 0));
      }

      /* should_return is set if we had a hit but we won't be calling the closest hit
       * shader and hence need to return immediately to the calling shader. */
      nir_push_if(b, should_return);
      insert_rt_return(b, vars);
      nir_pop_if(b, NULL);
      break;
   }
   case nir_intrinsic_execute_miss_amd: {
      nir_store_var(b, vars->tmax, intr->src[0].ssa, 0x1);
      nir_def *undef = nir_undef(b, 1, 32);
      nir_store_var(b, vars->primitive_id, undef, 0x1);
      nir_store_var(b, vars->instance_addr, nir_undef(b, 1, 64), 0x1);
      nir_store_var(b, vars->geometry_id_and_flags, undef, 0x1);
      nir_store_var(b, vars->hit_kind, undef, 0x1);
      nir_def *miss_index = nir_load_var(b, vars->miss_index);

      struct radv_nir_sbt_data sbt_data =
         radv_nir_load_sbt_entry(b, nir_load_param(b, RT_ARG_SBT_DESCRIPTORS), miss_index, SBT_MISS, SBT_RECURSIVE_PTR);
      nir_store_var(b, vars->shader_addr, sbt_data.shader_addr, 0x1);
      nir_store_var(b, vars->shader_record_ptr, sbt_data.shader_record_ptr, 0x1);

      if (!(vars->flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR)) {
         /* In case of a NULL miss shader, do nothing and just return. */
         nir_push_if(b, nir_ieq_imm(b, nir_load_var(b, vars->shader_addr), 0));
         insert_rt_return(b, vars);
         nir_pop_if(b, NULL);
      }

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

   if (ret)
      nir_def_rewrite_uses(&intr->def, ret);
   nir_instr_remove(&intr->instr);

   return true;
}

/* This lowers all the RT instructions that we do not want to pass on to the combined shader and
 * that we can implement using the variables from the shader we are going to inline into. */
static bool
lower_rt_instructions(nir_shader *shader, struct rt_variables *vars, struct radv_rt_shader_info *out_info)
{
   struct radv_lower_rt_instruction_data data = {
      .vars = vars,
      .out_info = out_info,
   };
   return nir_shader_instructions_pass(shader, radv_lower_rt_instruction, nir_metadata_none, &data);
}

static bool
lower_rt_derefs_cps(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   bool progress = false;

   nir_builder b;
   nir_def *arg_offset = NULL;

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_deref)
            continue;

         nir_deref_instr *deref = nir_instr_as_deref(instr);
         if (!nir_deref_mode_is(deref, nir_var_shader_call_data))
            continue;

         deref->modes = nir_var_function_temp;
         progress = true;

         if (deref->deref_type == nir_deref_type_var) {
            if (!arg_offset) {
               b = nir_builder_at(nir_before_impl(impl));
               arg_offset = nir_load_rt_arg_scratch_offset_amd(&b);
            }

            b.cursor = nir_before_instr(&deref->instr);
            nir_deref_instr *replacement =
               nir_build_deref_cast(&b, arg_offset, nir_var_function_temp, deref->var->type, 0);
            nir_def_replace(&deref->def, &replacement->def);
         }
      }
   }

   return nir_progress(progress, impl, nir_metadata_control_flow);
}

void
radv_nir_lower_rt_io_cps(nir_shader *nir)
{
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types, nir_var_function_temp | nir_var_shader_call_data,
            glsl_get_natural_size_align_bytes);

   NIR_PASS(_, nir, lower_rt_derefs_cps);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_function_temp, nir_address_format_32bit_offset);
}

static void
init_cps_function(nir_function *function, bool has_position_fetch, bool uses_descriptor_heap)
{
   function->num_params = has_position_fetch ? CPS_ARG_COUNT : CPS_ARG_COUNT - 1;
   function->params = rzalloc_array_size(function->shader, sizeof(nir_parameter), function->num_params);

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
   radv_nir_param_from_type(function->params + RAYGEN_ARG_TRAVERSAL_ADDR, glsl_uint64_t_type(), true, 0);
   radv_nir_param_from_type(function->params + RAYGEN_ARG_SHADER_RECORD_PTR, glsl_uint64_t_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_PAYLOAD_SCRATCH_OFFSET, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_STACK_PTR, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_ACCEL_STRUCT, glsl_uint64_t_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_CULL_MASK_AND_FLAGS, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_SBT_OFFSET, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_SBT_STRIDE, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_MISS_INDEX, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_RAY_ORIGIN, glsl_vector_type(GLSL_TYPE_UINT, 3), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_RAY_TMIN, glsl_float_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_RAY_DIRECTION, glsl_vector_type(GLSL_TYPE_UINT, 3), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_RAY_TMAX, glsl_float_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_PRIMITIVE_ID, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_INSTANCE_ADDR, glsl_uint64_t_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_GEOMETRY_ID_AND_FLAGS, glsl_uint_type(), false, 0);
   radv_nir_param_from_type(function->params + CPS_ARG_HIT_KIND, glsl_uint_type(), false, 0);

   if (has_position_fetch)
      radv_nir_param_from_type(function->params + CPS_ARG_PRIMITIVE_ADDR, glsl_uint64_t_type(), false, 0);

   function->driver_attributes =
      (uint32_t)ACO_NIR_CALL_ABI_RT_RECURSIVE | ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL | ACO_NIR_FUNCTION_ATTRIB_NORETURN;

   /* Entrypoints can't have parameters. Consider RT stages as callable functions */
   function->is_exported = true;
   function->is_entrypoint = false;
}

void
radv_nir_lower_rt_abi_cps(nir_shader *shader, const struct radv_shader_info *info, bool resume_shader,
                          struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline,
                          bool has_position_fetch, const struct radv_ray_tracing_stage_info *traversal_info)
{
   const bool uses_descriptor_heap = pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_DESCRIPTOR_HEAP_BIT_EXT;
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   /* The first raygen shader gets called by the prolog with the standard raygen signature. Only shaders called by the
    * first shader can use the CPS function signature.
    */
   if (shader->info.stage != MESA_SHADER_RAYGEN || resume_shader)
      init_cps_function(impl->function, has_position_fetch, uses_descriptor_heap);
   else
      radv_nir_init_rt_function_params(impl->function, MESA_SHADER_RAYGEN, 0, 0, uses_descriptor_heap);

   if (traversal_info) {
      unsigned idx;
      BITSET_FOREACH_SET (idx, traversal_info->unused_args, impl->function->num_params)
         impl->function->params[idx].driver_attributes |= ACO_NIR_PARAM_ATTRIB_DISCARDABLE;
   }

   struct rt_variables vars = create_rt_variables(shader, device, pipeline->base.base.create_flags);

   struct radv_rt_shader_info rt_info = {0};

   lower_rt_instructions(shader, &vars, &rt_info);

   shader->scratch_size = MAX2(shader->scratch_size, vars.stack_size);

   /* This can't use NIR_PASS because NIR_DEBUG=serialize,clone invalidates pointers. */
   nir_lower_returns(shader);

   /* initialize variables */
   nir_builder b = nir_builder_at(nir_before_impl(impl));

   nir_def *launch_size_vec = nir_load_param(&b, RT_ARG_LAUNCH_SIZE);
   nir_def *launch_id_vec = nir_load_param(&b, RT_ARG_LAUNCH_ID);
   for (unsigned i = 0; i < 3; ++i) {
      nir_store_var(&b, vars.launch_sizes[i], nir_channel(&b, launch_size_vec, i), 0x1);
      nir_store_var(&b, vars.launch_ids[i], nir_channel(&b, launch_id_vec, i), 0x1);
   }
   nir_store_var(&b, vars.traversal_addr, nir_load_param(&b, RAYGEN_ARG_TRAVERSAL_ADDR), 0x1);
   nir_store_var(&b, vars.shader_record_ptr, nir_load_param(&b, RAYGEN_ARG_SHADER_RECORD_PTR), 0x1);
   nir_store_var(&b, vars.shader_addr, nir_imm_int64(&b, 0), 0x1);

   if (shader->info.stage == MESA_SHADER_RAYGEN && !resume_shader) {
      impl->function->driver_attributes &= ~ACO_NIR_FUNCTION_ATTRIB_DIVERGENT_CALL;
      nir_store_var(&b, vars.stack_ptr, nir_imm_int(&b, 0), 0x1);
   } else {
      nir_store_var(&b, vars.stack_ptr, nir_load_param(&b, CPS_ARG_STACK_PTR), 0x1);
      nir_store_var(&b, vars.arg, nir_load_param(&b, CPS_ARG_PAYLOAD_SCRATCH_OFFSET), 0x1);
      nir_store_var(&b, vars.origin, nir_load_param(&b, CPS_ARG_RAY_ORIGIN), 0x7);
      nir_store_var(&b, vars.tmin, nir_load_param(&b, CPS_ARG_RAY_TMIN), 0x1);
      nir_store_var(&b, vars.direction, nir_load_param(&b, CPS_ARG_RAY_DIRECTION), 0x7);
      nir_store_var(&b, vars.tmax, nir_load_param(&b, CPS_ARG_RAY_TMAX), 0x1);
      nir_store_var(&b, vars.cull_mask_and_flags, nir_load_param(&b, CPS_ARG_CULL_MASK_AND_FLAGS), 0x1);
      nir_store_var(&b, vars.sbt_offset, nir_load_param(&b, CPS_ARG_SBT_OFFSET), 0x1);
      nir_store_var(&b, vars.sbt_stride, nir_load_param(&b, CPS_ARG_SBT_STRIDE), 0x1);
      nir_store_var(&b, vars.accel_struct, nir_load_param(&b, CPS_ARG_ACCEL_STRUCT), 0x1);
      nir_store_var(&b, vars.primitive_id, nir_load_param(&b, CPS_ARG_PRIMITIVE_ID), 0x1);
      nir_store_var(&b, vars.instance_addr, nir_load_param(&b, CPS_ARG_INSTANCE_ADDR), 0x1);
      if (has_position_fetch)
         nir_store_var(&b, vars.primitive_addr, nir_load_param(&b, CPS_ARG_PRIMITIVE_ADDR), 0x1);
      nir_store_var(&b, vars.geometry_id_and_flags, nir_load_param(&b, CPS_ARG_GEOMETRY_ID_AND_FLAGS), 0x1);
      nir_store_var(&b, vars.hit_kind, nir_load_param(&b, CPS_ARG_HIT_KIND), 0x1);

      if (traversal_info && traversal_info->miss_index.state == RADV_RT_CONST_ARG_STATE_VALID)
         nir_store_var(&b, vars.miss_index, nir_imm_int(&b, traversal_info->miss_index.value), 0x1);
      else
         nir_store_var(&b, vars.miss_index, nir_load_param(&b, CPS_ARG_MISS_INDEX), 0x1);
   }

   b.cursor = nir_after_impl(impl);

   /* tail-call next shader */
   nir_def *shader_addr = nir_load_var(&b, vars.shader_addr);
   nir_function *continuation_func = nir_function_create(shader, "continuation_func");
   init_cps_function(continuation_func, has_position_fetch, uses_descriptor_heap);

   unsigned param_count = continuation_func->num_params;
   nir_def **next_args = rzalloc_array_size(b.shader, sizeof(nir_def *), param_count);
   next_args[RT_ARG_LAUNCH_ID] = nir_load_param(&b, RT_ARG_LAUNCH_ID);
   next_args[RT_ARG_LAUNCH_SIZE] = nir_load_param(&b, RT_ARG_LAUNCH_SIZE);
   if (uses_descriptor_heap) {
      next_args[RT_ARG_HEAP_RESOURCE] = nir_load_param(&b, RT_ARG_HEAP_RESOURCE);
      next_args[RT_ARG_HEAP_SAMPLER] = nir_load_param(&b, RT_ARG_HEAP_SAMPLER);
   } else {
      next_args[RT_ARG_DESCRIPTORS] = nir_load_param(&b, RT_ARG_DESCRIPTORS);
      next_args[RT_ARG_DYNAMIC_DESCRIPTORS] = nir_load_param(&b, RT_ARG_DYNAMIC_DESCRIPTORS);
   }
   next_args[RT_ARG_PUSH_CONSTANTS] = nir_load_param(&b, RT_ARG_PUSH_CONSTANTS);
   next_args[RT_ARG_SBT_DESCRIPTORS] = nir_load_param(&b, RT_ARG_SBT_DESCRIPTORS);
   next_args[RAYGEN_ARG_TRAVERSAL_ADDR] = nir_load_var(&b, vars.traversal_addr);
   next_args[RAYGEN_ARG_SHADER_RECORD_PTR] = nir_load_var(&b, vars.shader_record_ptr);
   next_args[CPS_ARG_PAYLOAD_SCRATCH_OFFSET] = nir_load_var(&b, vars.arg);
   next_args[CPS_ARG_STACK_PTR] = nir_load_var(&b, vars.stack_ptr);
   next_args[CPS_ARG_RAY_ORIGIN] = nir_load_var(&b, vars.origin);
   next_args[CPS_ARG_RAY_TMIN] = nir_load_var(&b, vars.tmin);
   next_args[CPS_ARG_RAY_DIRECTION] = nir_load_var(&b, vars.direction);
   next_args[CPS_ARG_RAY_TMAX] = nir_load_var(&b, vars.tmax);
   next_args[CPS_ARG_CULL_MASK_AND_FLAGS] = nir_load_var(&b, vars.cull_mask_and_flags);
   next_args[CPS_ARG_SBT_OFFSET] = nir_load_var(&b, vars.sbt_offset);
   next_args[CPS_ARG_SBT_STRIDE] = nir_load_var(&b, vars.sbt_stride);
   next_args[CPS_ARG_MISS_INDEX] = nir_load_var(&b, vars.miss_index);
   next_args[CPS_ARG_ACCEL_STRUCT] = nir_load_var(&b, vars.accel_struct);
   next_args[CPS_ARG_PRIMITIVE_ID] = nir_load_var(&b, vars.primitive_id);
   next_args[CPS_ARG_INSTANCE_ADDR] = nir_load_var(&b, vars.instance_addr);
   next_args[CPS_ARG_PRIMITIVE_ADDR] = nir_load_var(&b, vars.primitive_addr);
   next_args[CPS_ARG_GEOMETRY_ID_AND_FLAGS] = nir_load_var(&b, vars.geometry_id_and_flags);
   next_args[CPS_ARG_HIT_KIND] = nir_load_var(&b, vars.hit_kind);
   nir_build_indirect_call(&b, continuation_func, shader_addr, param_count, next_args);

   nir_progress(true, impl, nir_metadata_none);

   /* cleanup passes */
   NIR_PASS(_, shader, nir_lower_global_vars_to_local);
   NIR_PASS(_, shader, nir_lower_vars_to_ssa);

   if (shader->info.stage == MESA_SHADER_CLOSEST_HIT || shader->info.stage == MESA_SHADER_INTERSECTION)
      NIR_PASS(_, shader, radv_nir_lower_rt_storage, NULL, NULL, NULL, info->wave_size);
}
