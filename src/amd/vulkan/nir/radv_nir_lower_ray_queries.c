/*
 * Copyright © 2022 Konstantin Seurer
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"

#include "util/hash_table.h"

#include "bvh/bvh.h"
#include "nir/radv_nir_rt_common.h"
#include "radv_debug.h"
#include "radv_nir.h"
#include "radv_shader.h"

/* Traversal stack size. Traversal supports backtracking so we can go deeper than this size if
 * needed. However, we keep a large stack size to avoid it being put into registers, which hurts
 * occupancy. */
#define MAX_SCRATCH_STACK_ENTRY_COUNT 76

enum radv_ray_intersection_field {
   radv_ray_intersection_primitive_addr,
   radv_ray_intersection_primitive_id,
   radv_ray_intersection_geometry_id_and_flags,
   radv_ray_intersection_instance_addr,
   radv_ray_intersection_intersection_type,
   radv_ray_intersection_opaque,
   radv_ray_intersection_frontface,
   radv_ray_intersection_sbt_offset_and_flags,
   radv_ray_intersection_barycentrics,
   radv_ray_intersection_t,
   radv_ray_intersection_field_count,
};

static const glsl_type *
radv_get_intersection_type()
{
   glsl_struct_field fields[radv_ray_intersection_field_count];

#define FIELD(field_name, field_type)                                                                                  \
   fields[radv_ray_intersection_##field_name] = (glsl_struct_field){                                                   \
      .type = field_type,                                                                                              \
      .name = #field_name,                                                                                             \
   }

   FIELD(primitive_addr, glsl_uint64_t_type());
   FIELD(primitive_id, glsl_uint_type());
   FIELD(geometry_id_and_flags, glsl_uint_type());
   FIELD(instance_addr, glsl_uint64_t_type());
   FIELD(intersection_type, glsl_uint_type());
   FIELD(opaque, glsl_bool_type());
   FIELD(frontface, glsl_bool_type());
   FIELD(sbt_offset_and_flags, glsl_uint_type());
   FIELD(barycentrics, glsl_vec2_type());
   FIELD(t, glsl_float_type());

#undef FIELD

   return glsl_struct_type(fields, radv_ray_intersection_field_count, "intersection", false);
}

enum radv_ray_query_field {
   radv_ray_query_root_bvh_base,
   radv_ray_query_flags,
   radv_ray_query_cull_mask,
   radv_ray_query_origin,
   radv_ray_query_tmin,
   radv_ray_query_direction,
   radv_ray_query_incomplete,
   radv_ray_query_candidate,
   radv_ray_query_closest,
   radv_ray_query_trav_origin,
   radv_ray_query_trav_direction,
   radv_ray_query_trav_bvh_base,
   radv_ray_query_trav_stack,
   radv_ray_query_trav_top_stack,
   radv_ray_query_trav_stack_low_watermark,
   radv_ray_query_trav_current_node,
   radv_ray_query_trav_previous_node,
   radv_ray_query_trav_parent_node,
   radv_ray_query_trav_instance_top_node,
   radv_ray_query_trav_instance_bottom_node,
   radv_ray_query_trav_second_iteration,
   radv_ray_query_stack,
   radv_ray_query_break_flag,
   radv_ray_query_field_count,
};

static const glsl_type *
radv_get_ray_query_type()
{
   glsl_struct_field fields[radv_ray_query_field_count];

   const glsl_type *intersection_type = radv_get_intersection_type();

#define FIELD(field_name, field_type)                                                                                  \
   fields[radv_ray_query_##field_name] = (glsl_struct_field){                                                          \
      .type = field_type,                                                                                              \
      .name = #field_name,                                                                                             \
   }

   FIELD(root_bvh_base, glsl_uint64_t_type());
   FIELD(flags, glsl_uint_type());
   FIELD(cull_mask, glsl_uint_type());
   FIELD(origin, glsl_vec_type(3));
   FIELD(tmin, glsl_float_type());
   FIELD(direction, glsl_vec_type(3));
   FIELD(incomplete, glsl_bool_type());
   FIELD(candidate, intersection_type);
   FIELD(closest, intersection_type);
   FIELD(trav_origin, glsl_vec_type(3));
   FIELD(trav_direction, glsl_vec_type(3));
   FIELD(trav_bvh_base, glsl_uint64_t_type());
   FIELD(trav_stack, glsl_uint_type());
   FIELD(trav_top_stack, glsl_uint_type());
   FIELD(trav_stack_low_watermark, glsl_uint_type());
   FIELD(trav_current_node, glsl_uint_type());
   FIELD(trav_previous_node, glsl_uint_type());
   FIELD(trav_parent_node, glsl_uint_type());
   FIELD(trav_instance_top_node, glsl_uint_type());
   FIELD(trav_instance_bottom_node, glsl_uint_type());
   FIELD(trav_second_iteration, glsl_bool_type());
   FIELD(stack, glsl_array_type(glsl_uint_type(), MAX_SCRATCH_STACK_ENTRY_COUNT, 0));
   FIELD(break_flag, glsl_bool_type());

#undef FIELD

   return glsl_struct_type(fields, radv_ray_query_field_count, "ray_query", false);
}

#define isec_deref(b, deref, field) nir_build_deref_struct(b, deref, radv_ray_intersection_##field)
#define isec_load(b, deref, field)  nir_load_deref(b, isec_deref(b, deref, field))
#define isec_store(b, deref, field, value)                                                                             \
   nir_store_deref(b, isec_deref(b, deref, field), value, BITFIELD_MASK(value->num_components))
#define isec_copy(b, dst, src, field) nir_copy_deref(b, isec_deref(b, dst, field), isec_deref(b, src, field))

#define rq_deref(b, deref, field) nir_build_deref_struct(b, deref, radv_ray_query_##field)
#define rq_load(b, deref, field)  nir_load_deref(b, rq_deref(b, deref, field))
#define rq_store(b, deref, field, value)                                                                               \
   nir_store_deref(b, rq_deref(b, deref, field), value, BITFIELD_MASK(value->num_components))

struct ray_query_vars {
   nir_variable *var;

   bool shared_stack;
   uint32_t shared_base;
   uint32_t stack_entries;

   nir_intrinsic_instr *initialize;
};

static void
init_ray_query_vars(nir_shader *shader, const glsl_type *opaque_type, struct ray_query_vars *dst, const char *base_name,
                    const struct radv_physical_device *pdev)
{
   memset(dst, 0, sizeof(*dst));

   uint32_t workgroup_size =
      shader->info.workgroup_size[0] * shader->info.workgroup_size[1] * shader->info.workgroup_size[2];
   uint32_t shared_stack_entries = shader->info.ray_queries == 1 ? 16 : 8;
   /* ds_bvh_stack* instructions use a fixed stride of 32 dwords. */
   if (radv_use_bvh_stack_rtn(pdev))
      workgroup_size = MAX2(workgroup_size, 32);
   uint32_t shared_stack_size = workgroup_size * shared_stack_entries * 4;
   uint32_t shared_offset = align(shader->info.shared_size, 4);
   if (shader->info.stage != MESA_SHADER_COMPUTE || glsl_type_is_array(opaque_type) ||
       shared_offset + shared_stack_size > pdev->max_shared_size) {
      dst->stack_entries = MAX_SCRATCH_STACK_ENTRY_COUNT;
   } else {
      dst->shared_stack = true;
      dst->shared_base = shared_offset;
      dst->stack_entries = shared_stack_entries;

      shader->info.shared_size = shared_offset + shared_stack_size;
   }

   const glsl_type *type = glsl_type_wrap_in_arrays(radv_get_ray_query_type(), opaque_type);
   dst->var = nir_variable_create(shader, nir_var_shader_temp, type, base_name);
}

static void
lower_ray_query(nir_shader *shader, nir_variable *ray_query, struct hash_table *ht,
                const struct radv_physical_device *pdev)
{
   struct ray_query_vars *vars = ralloc(ht, struct ray_query_vars);

   init_ray_query_vars(shader, ray_query->type, vars, ray_query->name == NULL ? "" : ray_query->name, pdev);

   _mesa_hash_table_insert(ht, ray_query, vars);
}

static void
copy_candidate_to_closest(nir_builder *b, nir_deref_instr *rq)
{
   nir_deref_instr *closest = rq_deref(b, rq, closest);
   nir_deref_instr *candidate = rq_deref(b, rq, candidate);

   isec_copy(b, closest, candidate, barycentrics);
   isec_copy(b, closest, candidate, primitive_addr);
   isec_copy(b, closest, candidate, geometry_id_and_flags);
   isec_copy(b, closest, candidate, instance_addr);
   isec_copy(b, closest, candidate, intersection_type);
   isec_copy(b, closest, candidate, opaque);
   isec_copy(b, closest, candidate, frontface);
   isec_copy(b, closest, candidate, sbt_offset_and_flags);
   isec_copy(b, closest, candidate, primitive_id);
   isec_copy(b, closest, candidate, t);
}

static void
insert_terminate_on_first_hit(nir_builder *b, nir_deref_instr *rq, const struct radv_ray_flags *ray_flags,
                              bool break_on_terminate)
{
   nir_def *terminate_on_first_hit;
   if (ray_flags)
      terminate_on_first_hit = ray_flags->terminate_on_first_hit;
   else
      terminate_on_first_hit = nir_test_mask(b, rq_load(b, rq, flags), SpvRayFlagsTerminateOnFirstHitKHRMask);

   nir_push_if(b, terminate_on_first_hit);
   {
      rq_store(b, rq, incomplete, nir_imm_false(b));
      if (break_on_terminate)
         nir_jump(b, nir_jump_break);
   }
   nir_pop_if(b, NULL);
}

static void
lower_rq_confirm_intersection(nir_builder *b, nir_intrinsic_instr *instr, nir_deref_instr *rq)
{
   copy_candidate_to_closest(b, rq);
   insert_terminate_on_first_hit(b, rq, NULL, false);
}

static void
lower_rq_generate_intersection(nir_builder *b, nir_intrinsic_instr *instr, nir_deref_instr *rq)
{
   nir_deref_instr *closest = rq_deref(b, rq, closest);

   nir_push_if(b, nir_iand(b, nir_fge(b, isec_load(b, closest, t), instr->src[1].ssa),
                           nir_fge(b, instr->src[1].ssa, rq_load(b, rq, tmin))));
   {
      copy_candidate_to_closest(b, rq);
      insert_terminate_on_first_hit(b, rq, NULL, false);
      isec_store(b, closest, t, instr->src[1].ssa);
   }
   nir_pop_if(b, NULL);
}

enum rq_intersection_type { intersection_type_none, intersection_type_triangle, intersection_type_aabb };

static void
lower_rq_initialize(nir_builder *b, nir_intrinsic_instr *instr, struct ray_query_vars *vars, nir_deref_instr *rq,
                    struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   nir_deref_instr *closest = rq_deref(b, rq, closest);
   nir_deref_instr *candidate = rq_deref(b, rq, candidate);

   rq_store(b, rq, flags, instr->src[2].ssa);
   rq_store(b, rq, cull_mask, nir_ishl_imm(b, instr->src[3].ssa, 24));

   rq_store(b, rq, origin, instr->src[4].ssa);
   rq_store(b, rq, trav_origin, instr->src[4].ssa);

   rq_store(b, rq, tmin, instr->src[5].ssa);

   rq_store(b, rq, direction, instr->src[6].ssa);
   rq_store(b, rq, trav_direction, instr->src[6].ssa);

   isec_store(b, closest, t, instr->src[7].ssa);
   isec_store(b, closest, intersection_type, nir_imm_int(b, intersection_type_none));

   nir_def *accel_struct = instr->src[1].ssa;

   /* Make sure that instance data loads don't hang in case of a miss by setting a valid initial address. */
   isec_store(b, closest, instance_addr, accel_struct);
   isec_store(b, candidate, instance_addr, accel_struct);

   nir_def *accel_struct_non_null = nir_ine_imm(b, accel_struct, 0);

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *bvh_offset = NULL;
   nir_push_if(b, accel_struct_non_null);
   {
      bvh_offset =
         nir_load_global(b, 1, 32, nir_iadd_imm(b, accel_struct, offsetof(struct radv_accel_struct_header, bvh_offset)),
                         .access = ACCESS_NON_WRITEABLE);
   }
   nir_pop_if(b, NULL);
   bvh_offset = nir_if_phi(b, bvh_offset, zero);

   nir_def *bvh_base = nir_iadd(b, accel_struct, nir_u2u64(b, bvh_offset));
   bvh_base = build_addr_to_node(device, b, bvh_base, instr->src[2].ssa);

   rq_store(b, rq, root_bvh_base, bvh_base);
   rq_store(b, rq, trav_bvh_base, bvh_base);

   if (vars->shared_stack) {
      if (radv_use_bvh_stack_rtn(pdev)) {
         uint32_t workgroup_size =
            b->shader->info.workgroup_size[0] * b->shader->info.workgroup_size[1] * b->shader->info.workgroup_size[2];
         nir_def *addr = radv_build_bvh_stack_rtn_addr(b, pdev, workgroup_size, vars->shared_base, vars->stack_entries);
         rq_store(b, rq, trav_stack, addr);
         rq_store(b, rq, trav_stack_low_watermark, addr);
      } else {
         nir_def *base_offset = nir_imul_imm(b, nir_load_local_invocation_index(b), sizeof(uint32_t));
         base_offset = nir_iadd_imm(b, base_offset, vars->shared_base);
         rq_store(b, rq, trav_stack, base_offset);
         rq_store(b, rq, trav_stack_low_watermark, base_offset);
      }
   } else {
      rq_store(b, rq, trav_stack, nir_imm_int(b, 0));
      rq_store(b, rq, trav_stack_low_watermark, nir_imm_int(b, 0));
   }

   rq_store(b, rq, trav_current_node, nir_imm_int(b, RADV_BVH_ROOT_NODE));
   rq_store(b, rq, trav_previous_node, nir_imm_int(b, RADV_BVH_INVALID_NODE));
   rq_store(b, rq, trav_parent_node, nir_imm_int(b, RADV_BVH_INVALID_NODE));
   rq_store(b, rq, trav_instance_top_node, nir_imm_int(b, RADV_BVH_INVALID_NODE));
   rq_store(b, rq, trav_instance_bottom_node, nir_imm_int(b, RADV_BVH_NO_INSTANCE_ROOT));
   rq_store(b, rq, trav_second_iteration, nir_imm_false(b));

   rq_store(b, rq, trav_top_stack, nir_imm_int(b, -1));

   rq_store(b, rq, incomplete, nir_iand_imm(b, accel_struct_non_null, !(instance->debug_flags & RADV_DEBUG_NO_RT)));

   vars->initialize = instr;
}

static nir_def *
lower_rq_load(struct radv_device *device, nir_builder *b, nir_intrinsic_instr *instr, nir_deref_instr *rq)
{
   bool committed = nir_intrinsic_committed(instr);

   nir_deref_instr *candidate = rq_deref(b, rq, candidate);
   nir_deref_instr *intersection = committed ? rq_deref(b, rq, closest) : candidate;

   uint32_t column = nir_intrinsic_column(instr);

   nir_ray_query_value value = nir_intrinsic_ray_query_value(instr);
   switch (value) {
   case nir_ray_query_value_flags:
      return rq_load(b, rq, flags);
   case nir_ray_query_value_intersection_barycentrics:
      return isec_load(b, intersection, barycentrics);
   case nir_ray_query_value_intersection_candidate_aabb_opaque:
      return nir_iand(b, isec_load(b, candidate, opaque),
                      nir_ieq_imm(b, isec_load(b, candidate, intersection_type), intersection_type_aabb));
   case nir_ray_query_value_intersection_front_face:
      return isec_load(b, intersection, frontface);
   case nir_ray_query_value_intersection_geometry_index:
      return nir_iand_imm(b, isec_load(b, intersection, geometry_id_and_flags), 0xFFFFFF);
   case nir_ray_query_value_intersection_instance_custom_index:
      return radv_load_custom_instance(device, b, isec_load(b, intersection, instance_addr));
   case nir_ray_query_value_intersection_instance_id:
      return radv_load_instance_id(device, b, isec_load(b, intersection, instance_addr));
   case nir_ray_query_value_intersection_instance_sbt_index:
      return nir_iand_imm(b, isec_load(b, intersection, sbt_offset_and_flags), 0xFFFFFF);
   case nir_ray_query_value_intersection_object_ray_direction: {
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(device, b, isec_load(b, intersection, instance_addr), wto_matrix);
      return nir_build_vec3_mat_mult(b, rq_load(b, rq, direction), wto_matrix, false);
   }
   case nir_ray_query_value_intersection_object_ray_origin: {
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(device, b, isec_load(b, intersection, instance_addr), wto_matrix);
      return nir_build_vec3_mat_mult(b, rq_load(b, rq, origin), wto_matrix, true);
   }
   case nir_ray_query_value_intersection_object_to_world: {
      nir_def *otw_matrix[3];
      radv_load_otw_matrix(device, b, isec_load(b, intersection, instance_addr), otw_matrix);
      return nir_vec3(b, nir_channel(b, otw_matrix[0], column), nir_channel(b, otw_matrix[1], column),
                      nir_channel(b, otw_matrix[2], column));
   }
   case nir_ray_query_value_intersection_primitive_index:
      return isec_load(b, intersection, primitive_id);
   case nir_ray_query_value_intersection_t:
      return isec_load(b, intersection, t);
   case nir_ray_query_value_intersection_type: {
      nir_def *intersection_type = isec_load(b, intersection, intersection_type);
      if (!committed)
         intersection_type = nir_iadd_imm(b, intersection_type, -1);

      return intersection_type;
   }
   case nir_ray_query_value_intersection_world_to_object: {
      nir_def *wto_matrix[3];
      radv_load_wto_matrix(device, b, isec_load(b, intersection, instance_addr), wto_matrix);

      nir_def *vals[3];
      for (unsigned i = 0; i < 3; ++i)
         vals[i] = nir_channel(b, wto_matrix[i], column);

      return nir_vec(b, vals, 3);
   }
   case nir_ray_query_value_tmin:
      return rq_load(b, rq, tmin);
   case nir_ray_query_value_world_ray_direction:
      return rq_load(b, rq, direction);
   case nir_ray_query_value_world_ray_origin:
      return rq_load(b, rq, origin);
   case nir_ray_query_value_intersection_triangle_vertex_positions: {
      nir_def *primitive_addr = isec_load(b, intersection, primitive_addr);
      return radv_load_vertex_position(device, b, primitive_addr, nir_intrinsic_column(instr));
   }
   default:
      UNREACHABLE("Invalid nir_ray_query_value!");
   }

   return NULL;
}

struct traversal_data {
   const struct radv_device *device;
   struct ray_query_vars *vars;
   nir_deref_instr *rq;
};

static void
handle_candidate_aabb(nir_builder *b, struct radv_leaf_intersection *intersection,
                      const struct radv_ray_traversal_args *args)
{
   struct traversal_data *data = args->data;

   nir_deref_instr *candidate = rq_deref(b, data->rq, candidate);

   isec_store(b, candidate, primitive_addr, intersection->node_addr);
   isec_store(b, candidate, primitive_id, intersection->primitive_id);
   isec_store(b, candidate, geometry_id_and_flags, intersection->geometry_id_and_flags);
   isec_store(b, candidate, opaque, intersection->opaque);
   isec_store(b, candidate, intersection_type, nir_imm_int(b, intersection_type_aabb));

   if (args->use_bvh_stack_rtn)
      rq_store(b, data->rq, break_flag, nir_imm_true(b));
   else
      nir_jump(b, nir_jump_break);
}

static void
handle_candidate_triangle(nir_builder *b, struct radv_triangle_intersection *intersection,
                          const struct radv_ray_traversal_args *args, const struct radv_ray_flags *ray_flags)
{
   struct traversal_data *data = args->data;

   nir_deref_instr *candidate = rq_deref(b, data->rq, candidate);

   isec_store(b, candidate, barycentrics, intersection->barycentrics);
   isec_store(b, candidate, primitive_addr, intersection->base.node_addr);
   isec_store(b, candidate, primitive_id, intersection->base.primitive_id);
   isec_store(b, candidate, geometry_id_and_flags, intersection->base.geometry_id_and_flags);
   isec_store(b, candidate, t, intersection->t);
   isec_store(b, candidate, opaque, intersection->base.opaque);
   isec_store(b, candidate, frontface, intersection->frontface);
   isec_store(b, candidate, intersection_type, nir_imm_int(b, intersection_type_triangle));

   nir_push_if(b, intersection->base.opaque);
   {
      copy_candidate_to_closest(b, data->rq);
      insert_terminate_on_first_hit(b, data->rq, ray_flags, true);
   }
   nir_push_else(b, NULL);
   {
      if (args->use_bvh_stack_rtn)
         rq_store(b, data->rq, break_flag, nir_imm_true(b));
      else
         nir_jump(b, nir_jump_break);
   }
   nir_pop_if(b, NULL);
}

static void
store_stack_entry(nir_builder *b, nir_def *index, nir_def *value, const struct radv_ray_traversal_args *args)
{
   struct traversal_data *data = args->data;

   if (data->vars->shared_stack)
      nir_store_shared(b, value, index, .base = 0, .align_mul = 4);
   else
      nir_store_deref(b, nir_build_deref_array(b, rq_deref(b, data->rq, stack), index), value, 0x1);
}

static nir_def *
load_stack_entry(nir_builder *b, nir_def *index, const struct radv_ray_traversal_args *args)
{
   struct traversal_data *data = args->data;

   if (data->vars->shared_stack)
      return nir_load_shared(b, 1, 32, index, .base = 0, .align_mul = 4);
   else
      return nir_load_deref(b, nir_build_deref_array(b, rq_deref(b, data->rq, stack), index));
}

static nir_def *
lower_rq_proceed(nir_builder *b, nir_intrinsic_instr *instr, struct ray_query_vars *vars, nir_deref_instr *rq,
                 struct radv_device *device)
{
   struct radv_physical_device *pdev = radv_device_physical(device);

   nir_deref_instr *closest = rq_deref(b, rq, closest);
   nir_deref_instr *candidate = rq_deref(b, rq, candidate);

   nir_metadata_require(nir_cf_node_get_function(&instr->instr.block->cf_node), nir_metadata_dominance);

   bool ignore_cull_mask = false;
   if (nir_block_dominates(vars->initialize->instr.block, instr->instr.block)) {
      nir_src cull_mask = vars->initialize->src[3];
      if (nir_src_is_const(cull_mask) && nir_src_as_uint(cull_mask) == 0xFF)
         ignore_cull_mask = true;
   }

   nir_variable *inv_dir = nir_local_variable_create(b->impl, glsl_vector_type(GLSL_TYPE_FLOAT, 3), "inv_dir");
   nir_store_var(b, inv_dir, nir_frcp(b, rq_load(b, rq, trav_direction)), 0x7);

   struct radv_ray_traversal_vars trav_vars = {
      .tmax = isec_deref(b, closest, t),
      .origin = rq_deref(b, rq, trav_origin),
      .dir = rq_deref(b, rq, trav_direction),
      .inv_dir = nir_build_deref_var(b, inv_dir),
      .bvh_base = rq_deref(b, rq, trav_bvh_base),
      .stack = rq_deref(b, rq, trav_stack),
      .top_stack = rq_deref(b, rq, trav_top_stack),
      .stack_low_watermark = rq_deref(b, rq, trav_stack_low_watermark),
      .current_node = rq_deref(b, rq, trav_current_node),
      .previous_node = rq_deref(b, rq, trav_previous_node),
      .parent_node = rq_deref(b, rq, trav_parent_node),
      .instance_top_node = rq_deref(b, rq, trav_instance_top_node),
      .instance_bottom_node = rq_deref(b, rq, trav_instance_bottom_node),
      .second_iteration = rq_deref(b, rq, trav_second_iteration),
      .instance_addr = isec_deref(b, candidate, instance_addr),
      .sbt_offset_and_flags = isec_deref(b, candidate, sbt_offset_and_flags),
      .break_flag = rq_deref(b, rq, break_flag),
   };

   struct traversal_data data = {
      .device = device,
      .vars = vars,
      .rq = rq,
   };

   struct radv_ray_traversal_args args = {
      .root_bvh_base = rq_load(b, rq, root_bvh_base),
      .flags = rq_load(b, rq, flags),
      .cull_mask = rq_load(b, rq, cull_mask),
      .origin = rq_load(b, rq, origin),
      .tmin = rq_load(b, rq, tmin),
      .dir = rq_load(b, rq, direction),
      .vars = trav_vars,
      .stack_entries = vars->stack_entries,
      .ignore_cull_mask = ignore_cull_mask,
      .stack_store_cb = store_stack_entry,
      .stack_load_cb = load_stack_entry,
      .aabb_cb = handle_candidate_aabb,
      .triangle_cb = handle_candidate_triangle,
      .data = &data,
   };

   if (vars->shared_stack) {
      args.use_bvh_stack_rtn = radv_use_bvh_stack_rtn(pdev);
      if (args.use_bvh_stack_rtn) {
         args.stack_stride = 1;
         args.stack_base = 0;
      } else {
         uint32_t workgroup_size =
            b->shader->info.workgroup_size[0] * b->shader->info.workgroup_size[1] * b->shader->info.workgroup_size[2];
         args.stack_stride = workgroup_size * 4;
         args.stack_base = vars->shared_base;
      }
   } else {
      args.stack_stride = 1;
      args.stack_base = 0;
   }

   rq_store(b, rq, break_flag, nir_imm_false(b));

   nir_push_if(b, rq_load(b, rq, incomplete));
   {
      nir_def *incomplete;
      if (radv_use_bvh8(pdev))
         incomplete = radv_build_ray_traversal_gfx12(device, b, &args);
      else
         incomplete = radv_build_ray_traversal(device, b, &args);
      rq_store(b, rq, incomplete, nir_iand(b, rq_load(b, rq, incomplete), incomplete));
   }
   nir_pop_if(b, NULL);

   return rq_load(b, rq, incomplete);
}

static void
lower_rq_terminate(nir_builder *b, nir_intrinsic_instr *instr, nir_deref_instr *rq)
{
   rq_store(b, rq, incomplete, nir_imm_false(b));
}

static nir_deref_instr *
radv_lower_opaque_ray_query_deref(nir_builder *b, nir_deref_instr *opaque_deref, nir_variable *var)
{
   if (opaque_deref->deref_type != nir_deref_type_array)
      return nir_build_deref_var(b, var);

   nir_deref_instr *outer_deref = radv_lower_opaque_ray_query_deref(b, nir_deref_instr_parent(opaque_deref), var);
   return nir_build_deref_array(b, outer_deref, opaque_deref->arr.index.ssa);
}

bool
radv_nir_lower_ray_queries(struct nir_shader *shader, struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   bool progress = false;
   struct hash_table *query_ht = _mesa_pointer_hash_table_create(NULL);

   nir_foreach_variable_in_list (var, &shader->variables) {
      if (!var->data.ray_query)
         continue;

      lower_ray_query(shader, var, query_ht, pdev);

      progress = true;
   }

   nir_foreach_function_impl (impl, shader) {
      nir_builder builder = nir_builder_create(impl);

      nir_foreach_variable_in_list (var, &impl->locals) {
         if (!var->data.ray_query)
            continue;

         lower_ray_query(shader, var, query_ht, pdev);

         progress = true;
      }

      nir_foreach_block (block, impl) {
         nir_foreach_instr_safe (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

            if (!nir_intrinsic_is_ray_query(intrinsic->intrinsic))
               continue;

            nir_deref_instr *ray_query_deref = nir_def_as_deref(intrinsic->src[0].ssa);

            struct ray_query_vars *vars =
               (struct ray_query_vars *)_mesa_hash_table_search(query_ht, nir_deref_instr_get_variable(ray_query_deref))
                  ->data;

            builder.cursor = nir_before_instr(instr);

            nir_deref_instr *rq = radv_lower_opaque_ray_query_deref(&builder, ray_query_deref, vars->var);

            nir_def *new_dest = NULL;

            switch (intrinsic->intrinsic) {
            case nir_intrinsic_rq_confirm_intersection:
               lower_rq_confirm_intersection(&builder, intrinsic, rq);
               break;
            case nir_intrinsic_rq_generate_intersection:
               lower_rq_generate_intersection(&builder, intrinsic, rq);
               break;
            case nir_intrinsic_rq_initialize:
               lower_rq_initialize(&builder, intrinsic, vars, rq, device);
               break;
            case nir_intrinsic_rq_load:
               new_dest = lower_rq_load(device, &builder, intrinsic, rq);
               break;
            case nir_intrinsic_rq_proceed:
               new_dest = lower_rq_proceed(&builder, intrinsic, vars, rq, device);
               break;
            case nir_intrinsic_rq_terminate:
               lower_rq_terminate(&builder, intrinsic, rq);
               break;
            default:
               UNREACHABLE("Unsupported ray query intrinsic!");
            }

            if (new_dest)
               nir_def_rewrite_uses(&intrinsic->def, new_dest);

            nir_instr_remove(instr);
            nir_instr_free(instr);

            progress = true;
         }
      }

      nir_progress(true, impl, nir_metadata_none);
   }

   ralloc_free(query_ht);

   if (progress) {
      NIR_PASS(_, shader, nir_split_struct_vars, nir_var_shader_temp);
      NIR_PASS(_, shader, nir_lower_global_vars_to_local);
      NIR_PASS(_, shader, nir_lower_vars_to_ssa);
   }

   return progress;
}
