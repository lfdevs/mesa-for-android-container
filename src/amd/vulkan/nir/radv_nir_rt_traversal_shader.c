/*
 * Copyright © 2025 Valve Corporation
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "bvh/bvh.h"
#include "nir/radv_nir_rt_common.h"
#include "nir/radv_nir_rt_stage_common.h"
#include "nir/radv_nir_rt_stage_cps.h"
#include "nir/radv_nir_rt_traversal_shader.h"
#include "nir_builder.h"
#include "radv_device.h"
#include "radv_meta_nir.h"
#include "radv_physical_device.h"
#include "radv_rra.h"

/* Variables only used internally to ray traversal. This is data that describes
 * the current state of the traversal vs. what we'd give to a shader.  e.g. what
 * is the instance we're currently visiting vs. what is the instance of the
 * closest hit. */
struct traversal_vars {
   struct radv_nir_rt_traversal_result result;

   /* RT pipeline-specific traversal vars */
   nir_variable *ahit_isec_count;

   /* Variables backing the nir_deref_instrs of radv_ray_traversal_args used in the common RT traversal loop. */
   nir_variable *origin;
   nir_variable *dir;
   nir_variable *inv_dir;
   nir_variable *sbt_offset_and_flags;
   nir_variable *instance_addr;
   nir_variable *bvh_base;
   nir_variable *stack;
   nir_variable *top_stack;
   nir_variable *stack_low_watermark;
   nir_variable *current_node;
   nir_variable *previous_node;
   nir_variable *parent_node;
   nir_variable *instance_top_node;
   nir_variable *instance_bottom_node;
   nir_variable *second_iteration;
};

struct anyhit_shader_vars {
   nir_variable *ahit_accept;
   nir_variable *ahit_terminate;
   nir_variable *shader_record_ptr;

   /* Only used in intersection shaders */
   nir_variable *terminated;
   nir_variable *opaque;

   /* Original parameters to traversal. Needed in any-hit/intersection inlining */
   nir_variable *origin;
   nir_variable *dir;
   nir_variable *tmin;
   nir_variable *cull_mask_and_flags;
};

/* Parameters passed through to an inlined any-hit/intersection shader */
struct traversal_inlining_params {
   struct radv_device *device;

   radv_nir_ahit_isec_preprocess_cb preprocess;
   void *preprocess_data;

   struct traversal_vars *trav_vars;
   struct radv_nir_rt_traversal_result *candidate;
   struct anyhit_shader_vars *anyhit_vars;
};

/* Data about ray traversal passed through to AABB/Intersection callbacks */
struct traversal_data {
   struct radv_device *device;
   struct radv_nir_rt_traversal_params *params;
   struct traversal_vars trav_vars;

   struct radv_ray_tracing_pipeline *pipeline;
};

static void
init_traversal_result(nir_shader *shader, struct radv_nir_rt_traversal_result *result)
{
   result->sbt_index = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_sbt_index");
   result->tmax = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "traversal_tmax");
   result->hit = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "traversal_hit");
   result->primitive_addr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "traversal_primitive_addr");
   result->primitive_id = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_primitive_id");
   result->geometry_id_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_geometry_id_and_flags");
   result->instance_addr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "traversal_instance_addr");
   result->hit_kind = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_hit_kind");

   result->barycentrics = NULL;
}

static void
copy_traversal_result(nir_builder *b, struct radv_nir_rt_traversal_result *dst,
                      struct radv_nir_rt_traversal_result *src)
{
   nir_store_var(b, dst->sbt_index, nir_load_var(b, src->sbt_index), 0x1);
   nir_store_var(b, dst->tmax, nir_load_var(b, src->tmax), 0x1);
   nir_store_var(b, dst->hit, nir_load_var(b, src->hit), 0x1);
   nir_store_var(b, dst->primitive_addr, nir_load_var(b, src->primitive_addr), 0x1);
   nir_store_var(b, dst->primitive_id, nir_load_var(b, src->primitive_id), 0x1);
   nir_store_var(b, dst->geometry_id_and_flags, nir_load_var(b, src->geometry_id_and_flags), 0x1);
   nir_store_var(b, dst->instance_addr, nir_load_var(b, src->instance_addr), 0x1);
   nir_store_var(b, dst->hit_kind, nir_load_var(b, src->hit_kind), 0x1);
}

static void
map_traversal_result(struct hash_table *var_remap, const struct radv_nir_rt_traversal_result *src,
                     const struct radv_nir_rt_traversal_result *dst)
{
   _mesa_hash_table_insert(var_remap, src->sbt_index, dst->sbt_index);
   _mesa_hash_table_insert(var_remap, src->tmax, dst->tmax);
   _mesa_hash_table_insert(var_remap, src->hit, dst->hit);
   _mesa_hash_table_insert(var_remap, src->primitive_addr, dst->primitive_addr);
   _mesa_hash_table_insert(var_remap, src->primitive_id, dst->primitive_id);
   _mesa_hash_table_insert(var_remap, src->geometry_id_and_flags, dst->geometry_id_and_flags);
   _mesa_hash_table_insert(var_remap, src->instance_addr, dst->instance_addr);
   _mesa_hash_table_insert(var_remap, src->hit_kind, dst->hit_kind);
}

static void
init_traversal_vars(nir_shader *shader, struct traversal_vars *vars)
{
   init_traversal_result(shader, &vars->result);

   vars->ahit_isec_count = NULL;

   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);

   vars->origin = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "traversal_origin");
   vars->dir = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "traversal_dir");
   vars->inv_dir = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "traversal_inv_dir");
   vars->sbt_offset_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_sbt_offset_and_flags");
   vars->instance_addr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
   vars->bvh_base = nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "traversal_bvh_base");
   vars->stack = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_stack_ptr");
   vars->top_stack = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_top_stack_ptr");
   vars->stack_low_watermark =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "traversal_stack_low_watermark");
   vars->current_node = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "current_node;");
   vars->previous_node = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "previous_node");
   vars->parent_node = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "parent_node");
   vars->instance_top_node = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "instance_top_node");
   vars->instance_bottom_node =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "instance_bottom_node");
   vars->second_iteration = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "second_iteration");
}

/* Maps traversal state variables from one traversal_vars to another.
 */
static void
map_traversal_vars(struct hash_table *var_remap, const struct traversal_vars *src, const struct traversal_vars *dst)
{
   if (dst->ahit_isec_count)
      _mesa_hash_table_insert(var_remap, src->ahit_isec_count, dst->ahit_isec_count);

   map_traversal_result(var_remap, &src->result, &dst->result);

   _mesa_hash_table_insert(var_remap, src->origin, dst->origin);
   _mesa_hash_table_insert(var_remap, src->dir, dst->dir);
   _mesa_hash_table_insert(var_remap, src->inv_dir, dst->inv_dir);
   _mesa_hash_table_insert(var_remap, src->sbt_offset_and_flags, dst->sbt_offset_and_flags);
   _mesa_hash_table_insert(var_remap, src->instance_addr, dst->instance_addr);
   _mesa_hash_table_insert(var_remap, src->bvh_base, dst->bvh_base);
   _mesa_hash_table_insert(var_remap, src->stack, dst->stack);
   _mesa_hash_table_insert(var_remap, src->top_stack, dst->top_stack);
   _mesa_hash_table_insert(var_remap, src->stack_low_watermark, dst->stack_low_watermark);
   _mesa_hash_table_insert(var_remap, src->current_node, dst->current_node);
   _mesa_hash_table_insert(var_remap, src->previous_node, dst->previous_node);
   _mesa_hash_table_insert(var_remap, src->parent_node, dst->parent_node);
   _mesa_hash_table_insert(var_remap, src->instance_top_node, dst->instance_top_node);
   _mesa_hash_table_insert(var_remap, src->instance_bottom_node, dst->instance_bottom_node);
   _mesa_hash_table_insert(var_remap, src->second_iteration, dst->second_iteration);
}

static void
init_anyhit_vars(nir_shader *shader, struct anyhit_shader_vars *vars)
{
   vars->ahit_accept = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "ahit_accept");
   vars->ahit_terminate = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "ahit_terminate");
   vars->shader_record_ptr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "ahit_shader_record");
   vars->terminated = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "intersection_terminate");
   vars->opaque = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "intersection_opaque");
   vars->origin =
      nir_variable_create(shader, nir_var_shader_temp, glsl_vector_type(GLSL_TYPE_FLOAT, 3), "param_origin");
   vars->dir =
      nir_variable_create(shader, nir_var_shader_temp, glsl_vector_type(GLSL_TYPE_FLOAT, 3), "param_dir");
   vars->tmin = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "ahit_tmin");
   vars->cull_mask_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "ahit_cull_mask_and_flags");
}

static void
map_anyhit_vars(struct hash_table *var_remap, const struct anyhit_shader_vars *src,
                const struct anyhit_shader_vars *dst)
{
   _mesa_hash_table_insert(var_remap, src->ahit_accept, dst->ahit_accept);
   _mesa_hash_table_insert(var_remap, src->ahit_terminate, dst->ahit_terminate);
   _mesa_hash_table_insert(var_remap, src->shader_record_ptr, dst->shader_record_ptr);
   _mesa_hash_table_insert(var_remap, src->terminated, dst->terminated);
   _mesa_hash_table_insert(var_remap, src->opaque, dst->opaque);
   _mesa_hash_table_insert(var_remap, src->origin, dst->origin);
   _mesa_hash_table_insert(var_remap, src->dir, dst->dir);
   _mesa_hash_table_insert(var_remap, src->tmin, dst->tmin);
   _mesa_hash_table_insert(var_remap, src->cull_mask_and_flags, dst->cull_mask_and_flags);
}

static bool
lower_ahit_isec_intrinsics(nir_builder *b, nir_intrinsic_instr *intr, void *_params)
{
   b->cursor = nir_after_instr(&intr->instr);

   struct traversal_inlining_params *params = _params;

   nir_def *ret = NULL;
   switch (intr->intrinsic) {
      /* When any-hit shaders are invoked, the traversal ray origin/direction is in object space */
   case nir_intrinsic_load_ray_object_origin:
      ret = nir_load_var(b, params->trav_vars->origin);
      break;
   case nir_intrinsic_load_ray_object_direction:
      ret = nir_load_var(b, params->trav_vars->dir);
      break;
   case nir_intrinsic_load_ray_world_origin:
      ret = nir_load_var(b, params->anyhit_vars->origin);
      break;
   case nir_intrinsic_load_ray_world_direction:
      ret = nir_load_var(b, params->anyhit_vars->dir);
      break;
   case nir_intrinsic_load_shader_record_ptr:
      ret = nir_load_var(b, params->anyhit_vars->shader_record_ptr);
      break;
   case nir_intrinsic_load_intersection_opaque_amd:
      ret = nir_load_var(b, params->anyhit_vars->opaque);
      break;
   case nir_intrinsic_load_ray_t_max:
      ret = nir_load_var(b, params->candidate->tmax);
      break;
   case nir_intrinsic_load_ray_t_min:
      ret = nir_load_var(b, params->anyhit_vars->tmin);
      break;
   case nir_intrinsic_load_ray_instance_custom_index:
      ret = radv_load_custom_instance(params->device, b, nir_load_var(b, params->candidate->instance_addr));
      break;
   case nir_intrinsic_load_primitive_id:
      ret = nir_load_var(b, params->candidate->primitive_id);
      break;
   case nir_intrinsic_load_instance_id:
      ret = radv_load_instance_id(params->device, b, nir_load_var(b, params->candidate->instance_addr));
      break;
   case nir_intrinsic_load_ray_hit_kind:
      ret = nir_load_var(b, params->candidate->hit_kind);
      break;
   case nir_intrinsic_load_ray_flags:
      ret = nir_iand_imm(b, nir_load_var(b, params->anyhit_vars->cull_mask_and_flags), 0xFFFFFF);
      break;
   case nir_intrinsic_load_cull_mask:
      ret = nir_ushr_imm(b, nir_load_var(b, params->anyhit_vars->cull_mask_and_flags), 24);
      break;
   case nir_intrinsic_load_ray_geometry_index: {
      ret = nir_load_var(b, params->candidate->geometry_id_and_flags);
      ret = nir_iand_imm(b, ret, 0xFFFFFFF);
      break;
   }
   case nir_intrinsic_load_ray_world_to_object: {
      unsigned c = nir_intrinsic_column(intr);
      nir_def *instance_node_addr = nir_load_var(b, params->candidate->instance_addr);
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(params->device, b, instance_node_addr, wto_matrix);

      nir_def *vals[3];
      for (unsigned i = 0; i < 3; ++i)
         vals[i] = nir_channel(b, wto_matrix[i], c);

      ret = nir_vec(b, vals, 3);
      break;
   }
   case nir_intrinsic_load_ray_object_to_world: {
      unsigned c = nir_intrinsic_column(intr);
      nir_def *otw_matrix[3];
      radv_load_otw_matrix(params->device, b, nir_load_var(b, params->candidate->instance_addr), otw_matrix);
      ret = nir_vec3(b, nir_channel(b, otw_matrix[0], c), nir_channel(b, otw_matrix[1], c),
                     nir_channel(b, otw_matrix[2], c));
      break;
   }
   case nir_intrinsic_ignore_ray_intersection: {
      nir_store_var(b, params->anyhit_vars->ahit_accept, nir_imm_false(b), 0x1);

      /* The if is a workaround to avoid having to fix up control flow manually */
      nir_push_if(b, nir_imm_true(b));
      nir_jump(b, nir_jump_return);
      nir_pop_if(b, NULL);
      nir_instr_remove(&intr->instr);
      return true;
   }
   case nir_intrinsic_terminate_ray: {
      nir_store_var(b, params->anyhit_vars->ahit_accept, nir_imm_true(b), 0x1);
      nir_store_var(b, params->anyhit_vars->ahit_terminate, nir_imm_true(b), 0x1);

      /* The if is a workaround to avoid having to fix up control flow manually */
      nir_push_if(b, nir_imm_true(b));
      nir_jump(b, nir_jump_return);
      nir_pop_if(b, NULL);
      nir_instr_remove(&intr->instr);
      return true;
   }
   case nir_intrinsic_report_ray_intersection: {
      nir_def *in_range = nir_iand(b, nir_fge(b, nir_load_var(b, params->trav_vars->result.tmax), intr->src[0].ssa),
                                   nir_fge(b, intr->src[0].ssa, nir_load_var(b, params->anyhit_vars->tmin)));
      nir_def *terminated = nir_load_var(b, params->anyhit_vars->terminated);
      nir_push_if(b, nir_iand(b, in_range, nir_inot(b, terminated)));
      {
         nir_store_var(b, params->anyhit_vars->ahit_accept, nir_imm_true(b), 0x1);
         nir_store_var(b, params->candidate->tmax, intr->src[0].ssa, 1);
         nir_store_var(b, params->candidate->hit_kind, intr->src[1].ssa, 1);
         nir_def *terminate_on_first_hit =
            nir_test_mask(b, nir_load_var(b, params->anyhit_vars->cull_mask_and_flags), SpvRayFlagsTerminateOnFirstHitKHRMask);
         nir_store_var(b, params->anyhit_vars->terminated,
                       nir_ior(b, terminate_on_first_hit, nir_load_var(b, params->anyhit_vars->ahit_terminate)), 1);
      }
      nir_pop_if(b, NULL);
      nir_instr_remove(&intr->instr);
      return true;
   }
   case nir_intrinsic_load_ray_triangle_vertex_positions: {
      nir_def *primitive_addr = nir_load_var(b, params->candidate->primitive_addr);
      ret = radv_load_vertex_position(params->device, b, primitive_addr, nir_intrinsic_column(intr));
      break;
   }
   default:
      return false;
   }
   assert(ret);

   nir_def_replace(&intr->def, ret);
   return true;
}

/* Insert an inlined shader into the traversal shader. */
static void
insert_inlined_shader(nir_builder *b, struct traversal_inlining_params *params, nir_shader *shader, nir_def *idx,
                      uint32_t call_idx)
{
   struct hash_table *var_remap = _mesa_pointer_hash_table_create(NULL);

   /* Since we call lower_ahit_isec_intrinsics before actually inlining the shader,
    * the variables in 'params' won't be accessible yet. Duplicate the variables
    * present in 'params' inside the inlined shader, then use var_remap to map the
    * duplicates to the original variables passed through in 'params'.
    */
   struct traversal_inlining_params src_params = {
      .device = params->device,
   };

   struct traversal_vars src_trav_vars;
   struct radv_nir_rt_traversal_result src_candidate;
   struct anyhit_shader_vars src_anyhit_vars;

   init_traversal_vars(shader, &src_trav_vars);
   map_traversal_vars(var_remap, &src_trav_vars, params->trav_vars);
   src_params.trav_vars = &src_trav_vars;
   init_traversal_result(shader, &src_candidate);
   map_traversal_result(var_remap, &src_candidate, params->candidate);
   src_params.candidate = &src_candidate;
   init_anyhit_vars(shader, &src_anyhit_vars);
   map_anyhit_vars(var_remap, &src_anyhit_vars, params->anyhit_vars);
   src_params.anyhit_vars = &src_anyhit_vars;

   nir_opt_dead_cf(shader);

   nir_shader_intrinsics_pass(shader, lower_ahit_isec_intrinsics, nir_metadata_control_flow, &src_params);

   nir_lower_returns(shader);
   nir_opt_dce(shader);

   radv_nir_inline_constants(b->shader, shader);

   nir_push_if(b, nir_ieq_imm(b, idx, call_idx));
   nir_inline_function_impl(b, nir_shader_get_entrypoint(shader), NULL, var_remap);
   nir_pop_if(b, NULL);
}

static nir_function_impl *
lower_any_hit_for_intersection(nir_shader *any_hit)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(any_hit);

   /* Any-hit shaders need three parameters */
   assert(impl->function->num_params == 0);
   nir_parameter params[] = {
      {
         /* A pointer to a boolean value for whether or not the hit was
          * accepted.
          */
         .num_components = 1,
         .bit_size = 32,
      },
      {
         /* The hit T value */
         .num_components = 1,
         .bit_size = 32,
      },
      {
         /* The hit kind */
         .num_components = 1,
         .bit_size = 32,
      },
      {
         /* Scratch offset */
         .num_components = 1,
         .bit_size = 32,
      },
   };
   impl->function->num_params = ARRAY_SIZE(params);
   impl->function->params = ralloc_array(any_hit, nir_parameter, ARRAY_SIZE(params));
   memcpy(impl->function->params, params, sizeof(params));

   nir_builder build = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &build;

   nir_def *commit_ptr = nir_load_param(b, 0);
   nir_def *hit_t = nir_load_param(b, 1);
   nir_def *hit_kind = nir_load_param(b, 2);
   nir_def *scratch_offset = nir_load_param(b, 3);

   nir_deref_instr *commit = nir_build_deref_cast(b, commit_ptr, nir_var_function_temp, glsl_bool_type(), 0);

   nir_foreach_block_safe (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         switch (instr->type) {
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_ignore_ray_intersection:
               b->cursor = nir_instr_remove(&intrin->instr);
               /* We put the newly emitted code inside a dummy if because it's
                * going to contain a jump instruction and we don't want to
                * deal with that mess here.  It'll get dealt with by our
                * control-flow optimization passes.
                */
               nir_store_deref(b, commit, nir_imm_false(b), 0x1);
               nir_push_if(b, nir_imm_true(b));
               nir_jump(b, nir_jump_return);
               nir_pop_if(b, NULL);
               break;

            case nir_intrinsic_terminate_ray:
               /* The "normal" handling of terminateRay works fine in
                * intersection shaders.
                */
               break;

            case nir_intrinsic_load_ray_t_max:
               nir_def_replace(&intrin->def, hit_t);
               break;

            case nir_intrinsic_load_ray_hit_kind:
               nir_def_replace(&intrin->def, hit_kind);
               break;

            /* We place all any_hit scratch variables after intersection scratch variables.
             * For that reason, we increment the scratch offset by the intersection scratch
             * size. For call_data, we have to subtract the offset again.
             *
             * Note that we don't increase the scratch size as it is already reflected via
             * the any_hit stack_size.
             */
            case nir_intrinsic_load_scratch:
               b->cursor = nir_before_instr(instr);
               nir_src_rewrite(&intrin->src[0], nir_iadd_nuw(b, scratch_offset, intrin->src[0].ssa));
               break;
            case nir_intrinsic_store_scratch:
               b->cursor = nir_before_instr(instr);
               nir_src_rewrite(&intrin->src[1], nir_iadd_nuw(b, scratch_offset, intrin->src[1].ssa));
               break;
            case nir_intrinsic_load_rt_arg_scratch_offset_amd:
               b->cursor = nir_after_instr(instr);
               nir_def *arg_offset = nir_isub(b, &intrin->def, scratch_offset);
               nir_def_rewrite_uses_after(&intrin->def, arg_offset);
               break;

            default:
               break;
            }
            break;
         }
         case nir_instr_type_jump: {
            nir_jump_instr *jump = nir_instr_as_jump(instr);
            if (jump->type == nir_jump_halt) {
               b->cursor = nir_instr_remove(instr);
               nir_jump(b, nir_jump_return);
            }
            break;
         }

         default:
            break;
         }
      }
   }

   nir_validate_shader(any_hit, "after initial any-hit lowering");

   nir_lower_returns_impl(impl);

   nir_validate_shader(any_hit, "after lowering returns");

   return impl;
}

/* Inline the any_hit shader into the intersection shader so we don't have
 * to implement yet another shader call interface here. Neither do any recursion.
 */
static void
nir_lower_intersection_shader(nir_shader *intersection, nir_shader *any_hit)
{
   void *dead_ctx = ralloc_context(NULL);

   nir_function_impl *any_hit_impl = NULL;
   struct hash_table *any_hit_var_remap = NULL;
   if (any_hit) {
      any_hit = nir_shader_clone(dead_ctx, any_hit);
      NIR_PASS(_, any_hit, nir_opt_dce);

      radv_nir_inline_constants(intersection, any_hit);

      any_hit_impl = lower_any_hit_for_intersection(any_hit);
      any_hit_var_remap = _mesa_pointer_hash_table_create(dead_ctx);
   }

   nir_function_impl *impl = nir_shader_get_entrypoint(intersection);

   nir_builder build = nir_builder_create(impl);
   nir_builder *b = &build;

   b->cursor = nir_before_impl(impl);

   nir_variable *commit = nir_local_variable_create(impl, glsl_bool_type(), "ray_commit");
   nir_store_var(b, commit, nir_imm_false(b), 0x1);

   nir_foreach_block_safe (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_report_ray_intersection)
            continue;

         b->cursor = nir_instr_remove(&intrin->instr);
         nir_def *hit_t = intrin->src[0].ssa;
         nir_def *hit_kind = intrin->src[1].ssa;
         nir_def *min_t = nir_load_ray_t_min(b);
         nir_def *max_t = nir_load_ray_t_max(b);

         /* bool commit_tmp = false; */
         nir_variable *commit_tmp = nir_local_variable_create(impl, glsl_bool_type(), "commit_tmp");
         nir_store_var(b, commit_tmp, nir_imm_false(b), 0x1);

         nir_push_if(b, nir_iand(b, nir_fge(b, hit_t, min_t), nir_fge(b, max_t, hit_t)));
         {
            /* Any-hit defaults to commit */
            nir_store_var(b, commit_tmp, nir_imm_true(b), 0x1);

            if (any_hit_impl != NULL) {
               nir_push_if(b, nir_inot(b, nir_load_intersection_opaque_amd(b)));
               {
                  nir_def *params[] = {
                     &nir_build_deref_var(b, commit_tmp)->def,
                     hit_t,
                     hit_kind,
                     nir_imm_int(b, intersection->scratch_size),
                  };
                  nir_inline_function_impl(b, any_hit_impl, params, any_hit_var_remap);
               }
               nir_pop_if(b, NULL);
            }

            nir_push_if(b, nir_load_var(b, commit_tmp));
            {
               nir_report_ray_intersection(b, 1, hit_t, hit_kind);
            }
            nir_pop_if(b, NULL);
         }
         nir_pop_if(b, NULL);

         nir_def *accepted = nir_load_var(b, commit_tmp);
         nir_def_rewrite_uses(&intrin->def, accepted);
      }
   }
   nir_progress(true, impl, nir_metadata_none);

   /* We did some inlining; have to re-index SSA defs */
   nir_index_ssa_defs(impl);

   /* Eliminate the casts introduced for the commit return of the any-hit shader. */
   NIR_PASS(_, intersection, nir_opt_deref);

   ralloc_free(dead_ctx);
}

static void
radv_ray_tracing_group_ahit_info(struct radv_ray_tracing_group *group, uint32_t *shader_index, uint32_t *handle_index,
                                 struct radv_rt_case_data *data)
{
   if (group->type == VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR) {
      *shader_index = group->any_hit_shader;
      *handle_index = group->handle.any_hit_index;
   }
}

static void
radv_build_ahit_case(nir_builder *b, nir_def *sbt_idx, struct radv_ray_tracing_group *group,
                     struct radv_rt_case_data *data)
{
   struct traversal_inlining_params *params = data->param_data;

   nir_shader *nir_stage =
      radv_pipeline_cache_handle_to_nir(data->device, data->pipeline->stages[group->any_hit_shader].nir);
   assert(nir_stage);

   params->preprocess(nir_stage, params->preprocess_data);

   insert_inlined_shader(b, params, nir_stage, sbt_idx, group->handle.any_hit_index);
   ralloc_free(nir_stage);
}

static void
radv_ray_tracing_group_isec_info(struct radv_ray_tracing_group *group, uint32_t *shader_index, uint32_t *handle_index,
                                 struct radv_rt_case_data *data)
{
   if (group->type == VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR) {
      *shader_index = group->intersection_shader;
      *handle_index = group->handle.intersection_index;
   }
}

static void
radv_build_isec_case(nir_builder *b, nir_def *sbt_idx, struct radv_ray_tracing_group *group,
                     struct radv_rt_case_data *data)
{
   struct traversal_inlining_params *params = data->param_data;

   nir_shader *nir_stage =
      radv_pipeline_cache_handle_to_nir(data->device, data->pipeline->stages[group->intersection_shader].nir);
   assert(nir_stage);

   params->preprocess(nir_stage, params->preprocess_data);

   nir_shader *any_hit_stage = NULL;
   if (group->any_hit_shader != VK_SHADER_UNUSED_KHR) {
      any_hit_stage =
         radv_pipeline_cache_handle_to_nir(data->device, data->pipeline->stages[group->any_hit_shader].nir);
      assert(any_hit_stage);

      params->preprocess(any_hit_stage, params->preprocess_data);

      /* reserve stack size for any_hit before it is inlined */
      data->pipeline->stages[group->any_hit_shader].stack_size = any_hit_stage->scratch_size;

      nir_lower_intersection_shader(nir_stage, any_hit_stage);
      ralloc_free(any_hit_stage);
   }

   insert_inlined_shader(b, params, nir_stage, sbt_idx, group->handle.intersection_index);
   ralloc_free(nir_stage);
}

static nir_def *
radv_build_token_begin(nir_builder *b, struct traversal_data *data, nir_def *hit,
                       enum radv_packed_token_type token_type, nir_def *token_size, uint32_t max_token_size)
{
   struct radv_rra_trace_data *rra_trace = &data->device->rra_trace;
   assert(rra_trace->ray_history_addr);
   assert(rra_trace->ray_history_buffer_size >= max_token_size);

   nir_def *ray_history_addr = nir_imm_int64(b, rra_trace->ray_history_addr);

   nir_def *launch_id = nir_load_ray_launch_id(b);

   nir_def *trace = nir_imm_true(b);
   for (uint32_t i = 0; i < 3; i++) {
      nir_def *remainder = nir_umod_imm(b, nir_channel(b, launch_id, i), rra_trace->ray_history_resolution_scale);
      trace = nir_iand(b, trace, nir_ieq_imm(b, remainder, 0));
   }
   nir_push_if(b, trace);

   static_assert(offsetof(struct radv_ray_history_header, offset) == 0, "Unexpected offset");
   nir_def *base_offset = nir_global_atomic(b, 32, ray_history_addr, token_size, .atomic_op = nir_atomic_op_iadd);

   /* Abuse the dword alignment of token_size to add an invalid bit to offset. */
   trace = nir_ieq_imm(b, nir_iand_imm(b, base_offset, 1), 0);

   nir_def *in_bounds = nir_ule_imm(b, base_offset, rra_trace->ray_history_buffer_size - max_token_size);
   /* Make sure we don't overwrite the header in case of an overflow. */
   in_bounds = nir_iand(b, in_bounds, nir_uge_imm(b, base_offset, sizeof(struct radv_ray_history_header)));

   nir_push_if(b, nir_iand(b, trace, in_bounds));

   nir_def *dst_addr = nir_iadd(b, ray_history_addr, nir_u2u64(b, base_offset));

   nir_def *launch_size = nir_load_ray_launch_size(b);

   nir_def *launch_id_comps[3];
   nir_def *launch_size_comps[3];
   for (uint32_t i = 0; i < 3; i++) {
      launch_id_comps[i] = nir_udiv_imm(b, nir_channel(b, launch_id, i), rra_trace->ray_history_resolution_scale);
      launch_size_comps[i] = nir_udiv_imm(b, nir_channel(b, launch_size, i), rra_trace->ray_history_resolution_scale);
   }

   nir_def *global_index =
      nir_iadd(b, launch_id_comps[0],
               nir_iadd(b, nir_imul(b, launch_id_comps[1], launch_size_comps[0]),
                        nir_imul(b, launch_id_comps[2], nir_imul(b, launch_size_comps[0], launch_size_comps[1]))));
   nir_def *launch_index_and_hit = nir_bcsel(b, hit, nir_ior_imm(b, global_index, 1u << 29u), global_index);
   nir_store_global(b, nir_ior_imm(b, launch_index_and_hit, token_type << 30), dst_addr, .align_mul = 4);

   return nir_iadd_imm(b, dst_addr, 4);
}

static void
radv_build_token_end(nir_builder *b)
{
   nir_pop_if(b, NULL);
   nir_pop_if(b, NULL);
}

static void
radv_build_end_trace_token(nir_builder *b, struct traversal_data *data, nir_def *iteration_instance_count)
{
   nir_def *hit = nir_load_var(b, data->trav_vars.result.hit);
   nir_def *token_size = nir_bcsel(b, hit, nir_imm_int(b, sizeof(struct radv_packed_end_trace_token)),
                                   nir_imm_int(b, offsetof(struct radv_packed_end_trace_token, primitive_id)));

   nir_def *dst_addr = radv_build_token_begin(b, data, hit, radv_packed_token_end_trace, token_size,
                                              sizeof(struct radv_packed_end_trace_token));
   {
      nir_store_global(b, data->params->accel_struct, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 8);

      nir_def *dispatch_indices =
         ac_nir_load_smem(b, 2, nir_imm_int64(b, data->device->rra_trace.ray_history_addr),
                          nir_imm_int(b, offsetof(struct radv_ray_history_header, dispatch_index)), 4, 0);
      nir_def *dispatch_index = nir_iadd(b, nir_channel(b, dispatch_indices, 0), nir_channel(b, dispatch_indices, 1));
      nir_def *dispatch_and_flags = nir_iand_imm(b, data->params->cull_mask_and_flags, 0xFFFF);
      dispatch_and_flags = nir_ior(b, dispatch_and_flags, dispatch_index);
      nir_store_global(b, dispatch_and_flags, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 4);

      nir_def *shifted_cull_mask = nir_iand_imm(b, data->params->cull_mask_and_flags, 0xFF000000);

      nir_def *packed_args = data->params->sbt_offset;
      packed_args = nir_ior(b, packed_args, nir_ishl_imm(b, data->params->sbt_stride, 4));
      packed_args = nir_ior(b, packed_args, nir_ishl_imm(b, data->params->miss_index, 8));
      packed_args = nir_ior(b, packed_args, shifted_cull_mask);
      nir_store_global(b, packed_args, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 4);

      nir_store_global(b, data->params->origin, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 12);

      nir_store_global(b, data->params->tmin, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 4);

      nir_store_global(b, data->params->direction, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 12);

      nir_store_global(b, data->params->tmax, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 4);

      nir_store_global(b, iteration_instance_count, dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 4);

      nir_store_global(b, nir_load_var(b, data->trav_vars.ahit_isec_count), dst_addr, .align_mul = 4);
      dst_addr = nir_iadd_imm(b, dst_addr, 4);

      nir_push_if(b, hit);
      {
         nir_store_global(b, nir_load_var(b, data->trav_vars.result.primitive_id), dst_addr, .align_mul = 4);
         dst_addr = nir_iadd_imm(b, dst_addr, 4);

         nir_def *geometry_id =
            nir_iand_imm(b, nir_load_var(b, data->trav_vars.result.geometry_id_and_flags), 0xFFFFFFF);
         nir_store_global(b, geometry_id, dst_addr, .align_mul = 4);
         dst_addr = nir_iadd_imm(b, dst_addr, 4);

         nir_def *instance_id_and_hit_kind =
            nir_load_global(b, 1, 32,
                            nir_iadd_imm(b, nir_load_var(b, data->trav_vars.result.instance_addr),
                                         offsetof(struct radv_bvh_instance_node, instance_id)));
         instance_id_and_hit_kind =
            nir_ior(b, instance_id_and_hit_kind, nir_ishl_imm(b, nir_load_var(b, data->trav_vars.result.hit_kind), 24));
         nir_store_global(b, instance_id_and_hit_kind, dst_addr, .align_mul = 4);
         dst_addr = nir_iadd_imm(b, dst_addr, 4);

         nir_store_global(b, nir_load_var(b, data->trav_vars.result.tmax), dst_addr, .align_mul = 4);
         dst_addr = nir_iadd_imm(b, dst_addr, 4);
      }
      nir_pop_if(b, NULL);
   }
   radv_build_token_end(b);
}

static void
handle_candidate_triangle(nir_builder *b, struct radv_triangle_intersection *intersection,
                          const struct radv_ray_traversal_args *args, const struct radv_ray_flags *ray_flags)
{
   struct traversal_data *data = args->data;

   nir_def *geometry_id = nir_iand_imm(b, intersection->base.geometry_id_and_flags, 0xfffffff);
   nir_def *sbt_idx =
      nir_iadd(b,
               nir_iadd(b, data->params->sbt_offset,
                        nir_iand_imm(b, nir_load_var(b, data->trav_vars.sbt_offset_and_flags), 0xffffff)),
               nir_imul(b, data->params->sbt_stride, geometry_id));

   nir_def *hit_kind = nir_bcsel(b, intersection->frontface, nir_imm_int(b, 0xFE), nir_imm_int(b, 0xFF));

   /* Barycentrics are in hit attribute storage - they need special backup handling */
   nir_def *prev_barycentrics = nir_load_var(b, data->trav_vars.result.barycentrics);

   struct radv_nir_rt_traversal_result candidate_result;
   init_traversal_result(b->shader, &candidate_result);

   candidate_result.barycentrics = data->trav_vars.result.barycentrics;

   nir_store_var(b, candidate_result.hit, nir_imm_true(b), 0x1);
   nir_store_var(b, candidate_result.barycentrics, intersection->barycentrics, 0x3);
   nir_store_var(b, candidate_result.primitive_addr, intersection->base.node_addr, 1);
   nir_store_var(b, candidate_result.primitive_id, intersection->base.primitive_id, 1);
   nir_store_var(b, candidate_result.geometry_id_and_flags, intersection->base.geometry_id_and_flags, 1);
   nir_store_var(b, candidate_result.tmax, intersection->t, 0x1);
   nir_store_var(b, candidate_result.instance_addr, nir_load_var(b, data->trav_vars.instance_addr), 0x1);
   nir_store_var(b, candidate_result.hit_kind, hit_kind, 0x1);
   nir_store_var(b, candidate_result.sbt_index, sbt_idx, 0x1);

   struct anyhit_shader_vars ahit_vars;
   init_anyhit_vars(b->shader, &ahit_vars);

   nir_store_var(b, ahit_vars.ahit_accept, nir_imm_true(b), 0x1);
   nir_store_var(b, ahit_vars.ahit_terminate, nir_imm_false(b), 0x1);
   nir_store_var(b, ahit_vars.origin, data->params->origin, 0x7);
   nir_store_var(b, ahit_vars.dir, data->params->direction, 0x7);
   nir_store_var(b, ahit_vars.tmin, data->params->tmin, 0x1);
   nir_store_var(b, ahit_vars.cull_mask_and_flags, data->params->cull_mask_and_flags, 0x1);

   nir_push_if(b, nir_inot(b, intersection->base.opaque));
   {
      struct radv_nir_sbt_data sbt_data = radv_nir_load_sbt_entry(b, sbt_idx, SBT_HIT, SBT_ANY_HIT_IDX);
      nir_store_var(b, ahit_vars.shader_record_ptr, sbt_data.shader_record_ptr, 0x1);

      struct traversal_inlining_params inlining_params = {
         .device = data->device,
         .trav_vars = &data->trav_vars,
         .candidate = &candidate_result,
         .anyhit_vars = &ahit_vars,
         .preprocess = data->params->preprocess_ahit_isec,
         .preprocess_data = data->params->cb_data,
      };

      struct radv_rt_case_data case_data = {
         .device = data->device,
         .pipeline = data->pipeline,
         .param_data = &inlining_params,
      };

      if (data->trav_vars.ahit_isec_count)
         nir_store_var(b, data->trav_vars.ahit_isec_count,
                       nir_iadd_imm(b, nir_load_var(b, data->trav_vars.ahit_isec_count), 1), 0x1);

      radv_visit_inlined_shaders(
         b, sbt_data.shader_addr,
         !(data->pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR),
         &case_data, radv_ray_tracing_group_ahit_info, radv_build_ahit_case);
   }
   nir_pop_if(b, NULL);

   nir_push_if(b, nir_load_var(b, ahit_vars.ahit_accept));
   {
      copy_traversal_result(b, &data->trav_vars.result, &candidate_result);
      nir_def *ray_terminated = nir_load_var(b, ahit_vars.ahit_terminate);
      nir_break_if(b, nir_ior(b, ray_flags->terminate_on_first_hit, ray_terminated));
   }
   nir_push_else(b, NULL);
   {
      nir_store_var(b, data->trav_vars.result.barycentrics, prev_barycentrics, 0x1);
   }
   nir_pop_if(b, NULL);
}

static void
handle_candidate_aabb(nir_builder *b, struct radv_leaf_intersection *intersection,
                      const struct radv_ray_traversal_args *args)
{
   struct traversal_data *data = args->data;

   nir_def *geometry_id = nir_iand_imm(b, intersection->geometry_id_and_flags, 0xfffffff);
   nir_def *sbt_idx =
      nir_iadd(b,
               nir_iadd(b, data->params->sbt_offset,
                        nir_iand_imm(b, nir_load_var(b, data->trav_vars.sbt_offset_and_flags), 0xffffff)),
               nir_imul(b, data->params->sbt_stride, geometry_id));

   struct radv_nir_rt_traversal_result candidate_result;
   struct anyhit_shader_vars ahit_vars;

   init_traversal_result(b->shader, &candidate_result);
   init_anyhit_vars(b->shader, &ahit_vars);

   /* For AABBs the intersection shader writes the hit kind, and only does it if it is the
    * next closest hit candidate. */
   candidate_result.hit_kind = data->trav_vars.result.hit_kind;

   nir_store_var(b, candidate_result.hit, nir_imm_true(b), 0x1);
   nir_store_var(b, candidate_result.primitive_addr, intersection->node_addr, 1);
   nir_store_var(b, candidate_result.primitive_id, intersection->primitive_id, 1);
   nir_store_var(b, candidate_result.geometry_id_and_flags, intersection->geometry_id_and_flags, 1);
   nir_store_var(b, candidate_result.tmax, nir_load_var(b, data->trav_vars.result.tmax), 0x1);
   nir_store_var(b, candidate_result.instance_addr, nir_load_var(b, data->trav_vars.instance_addr), 0x1);
   nir_store_var(b, candidate_result.sbt_index, sbt_idx, 0x1);
   nir_store_var(b, ahit_vars.ahit_accept, nir_imm_false(b), 0x1);
   nir_store_var(b, ahit_vars.ahit_terminate, nir_imm_false(b), 0x1);
   nir_store_var(b, ahit_vars.origin, data->params->origin, 0x7);
   nir_store_var(b, ahit_vars.dir, data->params->direction, 0x7);
   nir_store_var(b, ahit_vars.tmin, data->params->tmin, 0x1);
   nir_store_var(b, ahit_vars.cull_mask_and_flags, data->params->cull_mask_and_flags, 0x1);
   nir_store_var(b, ahit_vars.terminated, nir_imm_false(b), 0x1);
   nir_store_var(b, ahit_vars.opaque, intersection->opaque, 0x1);

   if (data->trav_vars.ahit_isec_count)
      nir_store_var(b, data->trav_vars.ahit_isec_count,
                    nir_iadd_imm(b, nir_load_var(b, data->trav_vars.ahit_isec_count), 1 << 16), 0x1);

   struct radv_nir_sbt_data sbt_data = radv_nir_load_sbt_entry(b, sbt_idx, SBT_HIT, SBT_INTERSECTION_IDX);
   nir_store_var(b, ahit_vars.shader_record_ptr, sbt_data.shader_record_ptr, 0x1);

   struct traversal_inlining_params inlining_params = {
      .device = data->device,
      .trav_vars = &data->trav_vars,
      .candidate = &candidate_result,
      .anyhit_vars = &ahit_vars,
      .preprocess = data->params->preprocess_ahit_isec,
      .preprocess_data = data->params->cb_data,
   };

   struct radv_rt_case_data case_data = {
      .device = data->device,
      .pipeline = data->pipeline,
      .param_data = &inlining_params,
   };

   radv_visit_inlined_shaders(
      b, sbt_data.shader_addr,
      !(data->pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_RAY_TRACING_NO_NULL_INTERSECTION_SHADERS_BIT_KHR),
      &case_data, radv_ray_tracing_group_isec_info, radv_build_isec_case);

   nir_push_if(b, nir_load_var(b, ahit_vars.ahit_accept));
   {
      copy_traversal_result(b, &data->trav_vars.result, &candidate_result);
      nir_break_if(b, nir_load_var(b, ahit_vars.terminated));
   }
   nir_pop_if(b, NULL);
}

static void
store_stack_entry(nir_builder *b, nir_def *index, nir_def *value, const struct radv_ray_traversal_args *args)
{
   nir_store_shared(b, value, index, .base = 0, .align_mul = 4);
}

static nir_def *
load_stack_entry(nir_builder *b, nir_def *index, const struct radv_ray_traversal_args *args)
{
   return nir_load_shared(b, 1, 32, index, .base = 0, .align_mul = 4);
}

struct radv_nir_rt_traversal_result
radv_build_traversal(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline, nir_builder *b,
                     struct radv_nir_rt_traversal_params *params, struct radv_ray_tracing_stage_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_variable *barycentrics =
      nir_variable_create(b->shader, nir_var_ray_hit_attrib, glsl_vector_type(GLSL_TYPE_FLOAT, 2), "barycentrics");
   barycentrics->data.driver_location = 0;

   struct traversal_data data = {
      .device = device,
      .params = params,
      .pipeline = pipeline,
   };
   init_traversal_vars(b->shader, &data.trav_vars);
   data.trav_vars.result.barycentrics = barycentrics;

   struct radv_ray_traversal_vars trav_vars_args = {
      .tmax = nir_build_deref_var(b, data.trav_vars.result.tmax),
      .origin = nir_build_deref_var(b, data.trav_vars.origin),
      .dir = nir_build_deref_var(b, data.trav_vars.dir),
      .inv_dir = nir_build_deref_var(b, data.trav_vars.inv_dir),
      .bvh_base = nir_build_deref_var(b, data.trav_vars.bvh_base),
      .stack = nir_build_deref_var(b, data.trav_vars.stack),
      .top_stack = nir_build_deref_var(b, data.trav_vars.top_stack),
      .stack_low_watermark = nir_build_deref_var(b, data.trav_vars.stack_low_watermark),
      .current_node = nir_build_deref_var(b, data.trav_vars.current_node),
      .previous_node = nir_build_deref_var(b, data.trav_vars.previous_node),
      .parent_node = nir_build_deref_var(b, data.trav_vars.parent_node),
      .instance_top_node = nir_build_deref_var(b, data.trav_vars.instance_top_node),
      .instance_bottom_node = nir_build_deref_var(b, data.trav_vars.instance_bottom_node),
      .second_iteration = nir_build_deref_var(b, data.trav_vars.second_iteration),
      .instance_addr = nir_build_deref_var(b, data.trav_vars.instance_addr),
      .sbt_offset_and_flags = nir_build_deref_var(b, data.trav_vars.sbt_offset_and_flags),
   };

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *bvh_offset;
   nir_push_if(b, nir_ine_imm(b, params->accel_struct, 0));
   {
      bvh_offset = nir_load_global(
         b, 1, 32, nir_iadd_imm(b, params->accel_struct, offsetof(struct radv_accel_struct_header, bvh_offset)),
         .access = ACCESS_NON_WRITEABLE);
      nir_store_var(b, data.trav_vars.current_node, nir_imm_int(b, RADV_BVH_ROOT_NODE), 0x1);
   }
   nir_push_else(b, NULL);
   {
      nir_store_var(b, data.trav_vars.current_node,
                    nir_imm_int(b, radv_use_bvh_stack_rtn(pdev) ? RADV_BVH_STACK_TERMINAL_NODE : RADV_BVH_INVALID_NODE),
                    0x1);
   }
   nir_pop_if(b, NULL);
   bvh_offset = nir_if_phi(b, bvh_offset, zero);

   nir_def *root_bvh_base = nir_iadd(b, params->accel_struct, nir_u2u64(b, bvh_offset));
   root_bvh_base = build_addr_to_node(device, b, root_bvh_base, params->cull_mask_and_flags);

   nir_def *stack_idx = nir_load_local_invocation_index(b);
   uint32_t stack_stride;

   if (radv_use_bvh_stack_rtn(pdev)) {
      stack_idx = radv_build_bvh_stack_rtn_addr(b, pdev, pdev->rt_wave_size, 0, MAX_STACK_ENTRY_COUNT);
      stack_stride = 1;
   } else {
      stack_idx = nir_imul_imm(b, stack_idx, sizeof(uint32_t));
      stack_stride = pdev->rt_wave_size * sizeof(uint32_t);
   }

   nir_store_var(b, data.trav_vars.result.hit, nir_imm_false(b), 1);
   nir_store_var(b, data.trav_vars.result.tmax, params->tmax, 1);

   nir_store_var(b, data.trav_vars.origin, params->origin, 7);
   nir_store_var(b, data.trav_vars.dir, params->direction, 7);
   nir_store_var(b, data.trav_vars.inv_dir, nir_frcp(b, params->direction), 7);
   nir_store_var(b, data.trav_vars.bvh_base, root_bvh_base, 1);

   nir_store_var(b, data.trav_vars.sbt_offset_and_flags, nir_imm_int(b, 0), 1);
   nir_store_var(b, data.trav_vars.instance_addr, nir_imm_int64(b, 0), 1);

   nir_store_var(b, data.trav_vars.stack, stack_idx, 1);
   nir_store_var(b, data.trav_vars.stack_low_watermark, nir_load_var(b, data.trav_vars.stack), 1);
   nir_store_var(b, data.trav_vars.previous_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
   nir_store_var(b, data.trav_vars.parent_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
   nir_store_var(b, data.trav_vars.instance_top_node, nir_imm_int(b, RADV_BVH_INVALID_NODE), 0x1);
   nir_store_var(b, data.trav_vars.instance_bottom_node, nir_imm_int(b, RADV_BVH_NO_INSTANCE_ROOT), 0x1);
   nir_store_var(b, data.trav_vars.second_iteration, nir_imm_false(b), 0x1);

   nir_store_var(b, data.trav_vars.top_stack, nir_imm_int(b, -1), 1);

   nir_variable *iteration_instance_count = NULL;
   if (device->rra_trace.ray_history_addr) {
      data.trav_vars.ahit_isec_count =
         nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "ahit_isec_count");
      iteration_instance_count =
         nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "iteration_instance_count");
      nir_store_var(b, iteration_instance_count, nir_imm_int(b, 0), 0x1);
      trav_vars_args.iteration_instance_count = nir_build_deref_var(b, iteration_instance_count);

      nir_store_var(b, data.trav_vars.ahit_isec_count, nir_imm_int(b, 0), 0x1);
   }

   struct radv_ray_traversal_args args = {
      .root_bvh_base = root_bvh_base,
      .flags = params->cull_mask_and_flags,
      .cull_mask = params->cull_mask_and_flags,
      .origin = params->origin,
      .tmin = params->tmin,
      .dir = params->direction,
      .vars = trav_vars_args,
      .stack_stride = stack_stride,
      .stack_entries = MAX_STACK_ENTRY_COUNT,
      .stack_base = 0,
      .ignore_cull_mask = params->ignore_cull_mask,
      .set_flags = info ? info->set_flags : 0,
      .unset_flags = info ? info->unset_flags : 0,
      .stack_store_cb = store_stack_entry,
      .stack_load_cb = load_stack_entry,
      .aabb_cb = (pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_AABBS_BIT_KHR)
                    ? NULL
                    : handle_candidate_aabb,
      .triangle_cb = (pipeline->base.base.create_flags & VK_PIPELINE_CREATE_2_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR)
                        ? NULL
                        : handle_candidate_triangle,
      .use_bvh_stack_rtn = radv_use_bvh_stack_rtn(pdev),
      .data = &data,
   };

   if (radv_use_bvh8(pdev))
      radv_build_ray_traversal_gfx12(device, b, &args);
   else
      radv_build_ray_traversal(device, b, &args);

   if (device->rra_trace.ray_history_addr)
      radv_build_end_trace_token(b, &data, nir_load_var(b, iteration_instance_count));

   nir_progress(true, nir_shader_get_entrypoint(b->shader), nir_metadata_none);
   radv_nir_lower_hit_attrib_derefs(b->shader);

   return data.trav_vars.result;
}

static void
preprocess_traversal_shader_ahit_isec(nir_shader *nir, void *_)
{
   /* Compiling a separate traversal shader is always done in CPS mode. */
   radv_nir_lower_rt_io_cps(nir);
}

nir_shader *
radv_build_traversal_shader(struct radv_device *device, struct radv_ray_tracing_pipeline *pipeline,
                            struct radv_ray_tracing_stage_info *info)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   /* Create the traversal shader as an intersection shader to prevent validation failures due to
    * invalid variable modes.*/
   nir_builder b = radv_meta_nir_init_shader(device, MESA_SHADER_INTERSECTION, "rt_traversal");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = pdev->rt_wave_size == 64 ? 8 : 4;
   b.shader->info.api_subgroup_size = pdev->rt_wave_size;
   b.shader->info.max_subgroup_size = pdev->rt_wave_size;
   b.shader->info.min_subgroup_size = pdev->rt_wave_size;
   b.shader->info.shared_size = pdev->rt_wave_size * MAX_STACK_ENTRY_COUNT * sizeof(uint32_t);

   /* Register storage for hit attributes during traversal */
   nir_variable *hit_attribs[RADV_MAX_HIT_ATTRIB_DWORDS];

   for (uint32_t i = 0; i < ARRAY_SIZE(hit_attribs); i++)
      hit_attribs[i] = nir_local_variable_create(nir_shader_get_entrypoint(b.shader), glsl_uint_type(), "ahit_attrib");

   struct radv_nir_rt_traversal_params params = {};

   if (info->tmin.state == RADV_RT_CONST_ARG_STATE_VALID)
      params.tmin = nir_imm_int(&b, info->tmin.value);
   else
      params.tmin = nir_load_ray_t_min(&b);

   if (info->tmax.state == RADV_RT_CONST_ARG_STATE_VALID)
      params.tmax = nir_imm_int(&b, info->tmax.value);
   else
      params.tmax = nir_load_ray_t_max(&b);

   if (info->sbt_offset.state == RADV_RT_CONST_ARG_STATE_VALID)
      params.sbt_offset = nir_imm_int(&b, info->sbt_offset.value);
   else
      params.sbt_offset = nir_load_sbt_offset_amd(&b);

   if (info->sbt_stride.state == RADV_RT_CONST_ARG_STATE_VALID)
      params.sbt_stride = nir_imm_int(&b, info->sbt_stride.value);
   else
      params.sbt_stride = nir_load_sbt_stride_amd(&b);

   /* initialize trace_ray arguments */
   params.accel_struct = nir_load_accel_struct_amd(&b);
   params.cull_mask_and_flags = nir_load_cull_mask_and_flags_amd(&b);
   params.origin = nir_load_ray_world_origin(&b);
   params.direction = nir_load_ray_world_direction(&b);

   params.preprocess_ahit_isec = preprocess_traversal_shader_ahit_isec;
   params.ignore_cull_mask = false;

   struct radv_nir_rt_traversal_result result = radv_build_traversal(device, pipeline, &b, &params, info);

   radv_nir_lower_hit_attribs(b.shader, hit_attribs, pdev->rt_wave_size);

   nir_push_if(&b, nir_load_var(&b, result.hit));
   {
      for (int i = 0; i < ARRAY_SIZE(hit_attribs); ++i)
         nir_store_hit_attrib_amd(&b, nir_load_var(&b, hit_attribs[i]), .base = i);

      nir_def *primitive_addr;
      if (info->has_position_fetch)
         primitive_addr = nir_load_var(&b, result.primitive_addr);
      else
         primitive_addr = nir_undef(&b, 1, 64);

      nir_execute_closest_hit_amd(&b, nir_load_var(&b, result.sbt_index), nir_load_var(&b, result.tmax), primitive_addr,
                                  nir_load_var(&b, result.primitive_id), nir_load_var(&b, result.instance_addr),
                                  nir_load_var(&b, result.geometry_id_and_flags), nir_load_var(&b, result.hit_kind));
   }
   nir_push_else(&b, NULL);
   {
      nir_execute_miss_amd(&b, params.tmax);
   }
   nir_pop_if(&b, NULL);

   nir_index_ssa_defs(nir_shader_get_entrypoint(b.shader));
   nir_progress(true, nir_shader_get_entrypoint(b.shader), nir_metadata_none);

   /* Lower and cleanup variables */
   NIR_PASS(_, b.shader, nir_lower_global_vars_to_local);
   NIR_PASS(_, b.shader, nir_lower_vars_to_ssa);

   return b.shader;
}
