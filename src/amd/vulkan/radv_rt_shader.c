/*
 * Copyright © 2021 Google
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"

#include "bvh/bvh.h"
#include "meta/radv_meta.h"
#include "radv_private.h"
#include "radv_rt_common.h"
#include "radv_shader.h"

/* Traversal stack size. This stack is put in LDS and experimentally 16 entries results in best
 * performance. */
#define MAX_STACK_ENTRY_COUNT 16

static bool
lower_rt_derefs(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   b.cursor = nir_before_cf_list(&impl->body);
   nir_ssa_def *arg_offset = nir_load_rt_arg_scratch_offset_amd(&b);

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
            b.cursor = nir_before_instr(&deref->instr);
            nir_deref_instr *replacement =
               nir_build_deref_cast(&b, arg_offset, nir_var_function_temp, deref->var->type, 0);
            nir_ssa_def_rewrite_uses(&deref->dest.ssa, &replacement->dest.ssa);
            nir_instr_remove(&deref->instr);
         }
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

/*
 * Global variables for an RT pipeline
 */
struct rt_variables {
   const VkRayTracingPipelineCreateInfoKHR *create_info;

   /* idx of the next shader to run in the next iteration of the main loop.
    * During traversal, idx is used to store the SBT index and will contain
    * the correct resume index upon returning.
    */
   nir_variable *idx;

   /* scratch offset of the argument area relative to stack_ptr */
   nir_variable *arg;

   nir_variable *stack_ptr;

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
   nir_variable *primitive_id;
   nir_variable *geometry_id_and_flags;
   nir_variable *instance_addr;
   nir_variable *hit_kind;
   nir_variable *opaque;

   /* Output variables for intersection & anyhit shaders. */
   nir_variable *ahit_accept;
   nir_variable *ahit_terminate;

   unsigned stack_size;
};

static struct rt_variables
create_rt_variables(nir_shader *shader, const VkRayTracingPipelineCreateInfoKHR *create_info)
{
   struct rt_variables vars = {
      .create_info = create_info,
   };
   vars.idx = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "idx");
   vars.arg = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "arg");
   vars.stack_ptr = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "stack_ptr");
   vars.shader_record_ptr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "shader_record_ptr");

   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   vars.accel_struct =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "accel_struct");
   vars.cull_mask_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "cull_mask_and_flags");
   vars.sbt_offset =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "sbt_offset");
   vars.sbt_stride =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "sbt_stride");
   vars.miss_index =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "miss_index");
   vars.origin = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "ray_origin");
   vars.tmin = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "ray_tmin");
   vars.direction = nir_variable_create(shader, nir_var_shader_temp, vec3_type, "ray_direction");
   vars.tmax = nir_variable_create(shader, nir_var_shader_temp, glsl_float_type(), "ray_tmax");

   vars.primitive_id =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "primitive_id");
   vars.geometry_id_and_flags =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "geometry_id_and_flags");
   vars.instance_addr =
      nir_variable_create(shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
   vars.hit_kind = nir_variable_create(shader, nir_var_shader_temp, glsl_uint_type(), "hit_kind");
   vars.opaque = nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "opaque");

   vars.ahit_accept =
      nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "ahit_accept");
   vars.ahit_terminate =
      nir_variable_create(shader, nir_var_shader_temp, glsl_bool_type(), "ahit_terminate");

   return vars;
}

/*
 * Remap all the variables between the two rt_variables struct for inlining.
 */
static void
map_rt_variables(struct hash_table *var_remap, struct rt_variables *src,
                 const struct rt_variables *dst)
{
   src->create_info = dst->create_info;

   _mesa_hash_table_insert(var_remap, src->idx, dst->idx);
   _mesa_hash_table_insert(var_remap, src->arg, dst->arg);
   _mesa_hash_table_insert(var_remap, src->stack_ptr, dst->stack_ptr);
   _mesa_hash_table_insert(var_remap, src->shader_record_ptr, dst->shader_record_ptr);

   _mesa_hash_table_insert(var_remap, src->accel_struct, dst->accel_struct);
   _mesa_hash_table_insert(var_remap, src->cull_mask_and_flags, dst->cull_mask_and_flags);
   _mesa_hash_table_insert(var_remap, src->sbt_offset, dst->sbt_offset);
   _mesa_hash_table_insert(var_remap, src->sbt_stride, dst->sbt_stride);
   _mesa_hash_table_insert(var_remap, src->miss_index, dst->miss_index);
   _mesa_hash_table_insert(var_remap, src->origin, dst->origin);
   _mesa_hash_table_insert(var_remap, src->tmin, dst->tmin);
   _mesa_hash_table_insert(var_remap, src->direction, dst->direction);
   _mesa_hash_table_insert(var_remap, src->tmax, dst->tmax);

   _mesa_hash_table_insert(var_remap, src->primitive_id, dst->primitive_id);
   _mesa_hash_table_insert(var_remap, src->geometry_id_and_flags, dst->geometry_id_and_flags);
   _mesa_hash_table_insert(var_remap, src->instance_addr, dst->instance_addr);
   _mesa_hash_table_insert(var_remap, src->hit_kind, dst->hit_kind);
   _mesa_hash_table_insert(var_remap, src->opaque, dst->opaque);
   _mesa_hash_table_insert(var_remap, src->ahit_accept, dst->ahit_accept);
   _mesa_hash_table_insert(var_remap, src->ahit_terminate, dst->ahit_terminate);
}

/*
 * Create a copy of the global rt variables where the primitive/instance related variables are
 * independent.This is needed as we need to keep the old values of the global variables around
 * in case e.g. an anyhit shader reject the collision. So there are inner variables that get copied
 * to the outer variables once we commit to a better hit.
 */
static struct rt_variables
create_inner_vars(nir_builder *b, const struct rt_variables *vars)
{
   struct rt_variables inner_vars = *vars;
   inner_vars.idx =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_idx");
   inner_vars.shader_record_ptr = nir_variable_create(
      b->shader, nir_var_shader_temp, glsl_uint64_t_type(), "inner_shader_record_ptr");
   inner_vars.primitive_id =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_primitive_id");
   inner_vars.geometry_id_and_flags = nir_variable_create(
      b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_geometry_id_and_flags");
   inner_vars.tmax =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_float_type(), "inner_tmax");
   inner_vars.instance_addr = nir_variable_create(b->shader, nir_var_shader_temp,
                                                  glsl_uint64_t_type(), "inner_instance_addr");
   inner_vars.hit_kind =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "inner_hit_kind");

   return inner_vars;
}

static void
insert_rt_return(nir_builder *b, const struct rt_variables *vars)
{
   nir_store_var(b, vars->stack_ptr, nir_iadd_imm(b, nir_load_var(b, vars->stack_ptr), -16), 1);
   nir_store_var(b, vars->idx,
                 nir_load_scratch(b, 1, 32, nir_load_var(b, vars->stack_ptr), .align_mul = 16), 1);
}

enum sbt_type {
   SBT_RAYGEN = offsetof(VkTraceRaysIndirectCommand2KHR, raygenShaderRecordAddress),
   SBT_MISS = offsetof(VkTraceRaysIndirectCommand2KHR, missShaderBindingTableAddress),
   SBT_HIT = offsetof(VkTraceRaysIndirectCommand2KHR, hitShaderBindingTableAddress),
   SBT_CALLABLE = offsetof(VkTraceRaysIndirectCommand2KHR, callableShaderBindingTableAddress),
};

enum sbt_entry {
   SBT_GENERAL_IDX = offsetof(struct radv_pipeline_group_handle, general_index),
   SBT_CLOSEST_HIT_IDX = offsetof(struct radv_pipeline_group_handle, closest_hit_index),
   SBT_INTERSECTION_IDX = offsetof(struct radv_pipeline_group_handle, intersection_index),
   SBT_ANY_HIT_IDX = offsetof(struct radv_pipeline_group_handle, any_hit_index),
};

static nir_ssa_def *
get_sbt_ptr(nir_builder *b, nir_ssa_def *idx, enum sbt_type binding)
{
   nir_ssa_def *desc_base_addr = nir_load_sbt_base_amd(b);

   nir_ssa_def *desc =
      nir_pack_64_2x32(b, nir_build_load_smem_amd(b, 2, desc_base_addr, nir_imm_int(b, binding)));

   nir_ssa_def *stride_offset = nir_imm_int(b, binding + (binding == SBT_RAYGEN ? 8 : 16));
   nir_ssa_def *stride =
      nir_pack_64_2x32(b, nir_build_load_smem_amd(b, 2, desc_base_addr, stride_offset));

   return nir_iadd(b, desc, nir_imul(b, nir_u2u64(b, idx), stride));
}

static void
load_sbt_entry(nir_builder *b, const struct rt_variables *vars, nir_ssa_def *idx,
               enum sbt_type binding, enum sbt_entry offset)
{
   nir_ssa_def *addr = get_sbt_ptr(b, idx, binding);

   nir_ssa_def *load_addr = nir_iadd_imm(b, addr, offset);
   nir_ssa_def *v_idx = nir_build_load_global(b, 1, 32, load_addr);

   nir_store_var(b, vars->idx, v_idx, 1);

   nir_ssa_def *record_addr = nir_iadd_imm(b, addr, RADV_RT_HANDLE_SIZE);
   nir_store_var(b, vars->shader_record_ptr, record_addr, 1);
}

/* This lowers all the RT instructions that we do not want to pass on to the combined shader and
 * that we can implement using the variables from the shader we are going to inline into. */
static void
lower_rt_instructions(nir_shader *shader, struct rt_variables *vars, unsigned call_idx_base)
{
   nir_builder b_shader;
   nir_builder_init(&b_shader, nir_shader_get_entrypoint(shader));

   nir_foreach_block (block, nir_shader_get_entrypoint(shader)) {
      nir_foreach_instr_safe (instr, block) {
         switch (instr->type) {
         case nir_instr_type_intrinsic: {
            b_shader.cursor = nir_before_instr(instr);
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            nir_ssa_def *ret = NULL;

            switch (intr->intrinsic) {
            case nir_intrinsic_rt_execute_callable: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16);
               uint32_t ret_idx = call_idx_base + nir_intrinsic_call_idx(intr) + 1;

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm_nuw(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), size), 1);
               nir_store_scratch(&b_shader, nir_imm_int(&b_shader, ret_idx),
                                 nir_load_var(&b_shader, vars->stack_ptr), .align_mul = 16);

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm_nuw(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), 16), 1);
               load_sbt_entry(&b_shader, vars, intr->src[0].ssa, SBT_CALLABLE, SBT_GENERAL_IDX);

               nir_store_var(&b_shader, vars->arg,
                             nir_iadd_imm(&b_shader, intr->src[1].ssa, -size - 16), 1);

               vars->stack_size = MAX2(vars->stack_size, size + 16);
               break;
            }
            case nir_intrinsic_rt_trace_ray: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16);
               uint32_t ret_idx = call_idx_base + nir_intrinsic_call_idx(intr) + 1;

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm_nuw(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), size), 1);
               nir_store_scratch(&b_shader, nir_imm_int(&b_shader, ret_idx),
                                 nir_load_var(&b_shader, vars->stack_ptr), .align_mul = 16);

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm_nuw(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), 16), 1);

               nir_store_var(&b_shader, vars->idx, nir_imm_int(&b_shader, 1), 1);
               nir_store_var(&b_shader, vars->arg,
                             nir_iadd_imm(&b_shader, intr->src[10].ssa, -size - 16), 1);

               vars->stack_size = MAX2(vars->stack_size, size + 16);

               /* Per the SPIR-V extension spec we have to ignore some bits for some arguments. */
               nir_store_var(&b_shader, vars->accel_struct, intr->src[0].ssa, 0x1);
               nir_store_var(&b_shader, vars->cull_mask_and_flags,
                             nir_ior(&b_shader, nir_ishl_imm(&b_shader, intr->src[2].ssa, 24),
                                     intr->src[1].ssa),
                             0x1);
               nir_store_var(&b_shader, vars->sbt_offset,
                             nir_iand_imm(&b_shader, intr->src[3].ssa, 0xf), 0x1);
               nir_store_var(&b_shader, vars->sbt_stride,
                             nir_iand_imm(&b_shader, intr->src[4].ssa, 0xf), 0x1);
               nir_store_var(&b_shader, vars->miss_index,
                             nir_iand_imm(&b_shader, intr->src[5].ssa, 0xffff), 0x1);
               nir_store_var(&b_shader, vars->origin, intr->src[6].ssa, 0x7);
               nir_store_var(&b_shader, vars->tmin, intr->src[7].ssa, 0x1);
               nir_store_var(&b_shader, vars->direction, intr->src[8].ssa, 0x7);
               nir_store_var(&b_shader, vars->tmax, intr->src[9].ssa, 0x1);
               break;
            }
            case nir_intrinsic_rt_resume: {
               uint32_t size = align(nir_intrinsic_stack_size(intr), 16);

               nir_store_var(
                  &b_shader, vars->stack_ptr,
                  nir_iadd_imm(&b_shader, nir_load_var(&b_shader, vars->stack_ptr), -size), 1);
               break;
            }
            case nir_intrinsic_rt_return_amd: {
               if (shader->info.stage == MESA_SHADER_RAYGEN) {
                  nir_terminate(&b_shader);
                  break;
               }
               insert_rt_return(&b_shader, vars);
               break;
            }
            case nir_intrinsic_load_scratch: {
               nir_instr_rewrite_src_ssa(
                  instr, &intr->src[0],
                  nir_iadd_nuw(&b_shader, nir_load_var(&b_shader, vars->stack_ptr),
                               intr->src[0].ssa));
               continue;
            }
            case nir_intrinsic_store_scratch: {
               nir_instr_rewrite_src_ssa(
                  instr, &intr->src[1],
                  nir_iadd_nuw(&b_shader, nir_load_var(&b_shader, vars->stack_ptr),
                               intr->src[1].ssa));
               continue;
            }
            case nir_intrinsic_load_rt_arg_scratch_offset_amd: {
               ret = nir_load_var(&b_shader, vars->arg);
               break;
            }
            case nir_intrinsic_load_shader_record_ptr: {
               ret = nir_load_var(&b_shader, vars->shader_record_ptr);
               break;
            }
            case nir_intrinsic_load_ray_t_min: {
               ret = nir_load_var(&b_shader, vars->tmin);
               break;
            }
            case nir_intrinsic_load_ray_t_max: {
               ret = nir_load_var(&b_shader, vars->tmax);
               break;
            }
            case nir_intrinsic_load_ray_world_origin: {
               ret = nir_load_var(&b_shader, vars->origin);
               break;
            }
            case nir_intrinsic_load_ray_world_direction: {
               ret = nir_load_var(&b_shader, vars->direction);
               break;
            }
            case nir_intrinsic_load_ray_instance_custom_index: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *custom_instance_and_mask = nir_build_load_global(
                  &b_shader, 1, 32,
                  nir_iadd_imm(&b_shader, instance_node_addr,
                               offsetof(struct radv_bvh_instance_node, custom_instance_and_mask)));
               ret = nir_iand_imm(&b_shader, custom_instance_and_mask, 0xFFFFFF);
               break;
            }
            case nir_intrinsic_load_primitive_id: {
               ret = nir_load_var(&b_shader, vars->primitive_id);
               break;
            }
            case nir_intrinsic_load_ray_geometry_index: {
               ret = nir_load_var(&b_shader, vars->geometry_id_and_flags);
               ret = nir_iand_imm(&b_shader, ret, 0xFFFFFFF);
               break;
            }
            case nir_intrinsic_load_instance_id: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               ret = nir_build_load_global(
                  &b_shader, 1, 32,
                  nir_iadd_imm(&b_shader, instance_node_addr,
                               offsetof(struct radv_bvh_instance_node, instance_id)));
               break;
            }
            case nir_intrinsic_load_ray_flags: {
               ret = nir_iand_imm(&b_shader, nir_load_var(&b_shader, vars->cull_mask_and_flags),
                                  0xFFFFFF);
               break;
            }
            case nir_intrinsic_load_ray_hit_kind: {
               ret = nir_load_var(&b_shader, vars->hit_kind);
               break;
            }
            case nir_intrinsic_load_ray_world_to_object: {
               unsigned c = nir_intrinsic_column(intr);
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[3];
               nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);

               nir_ssa_def *vals[3];
               for (unsigned i = 0; i < 3; ++i)
                  vals[i] = nir_channel(&b_shader, wto_matrix[i], c);

               ret = nir_vec(&b_shader, vals, 3);
               break;
            }
            case nir_intrinsic_load_ray_object_to_world: {
               unsigned c = nir_intrinsic_column(intr);
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *rows[3];
               for (unsigned r = 0; r < 3; ++r)
                  rows[r] = nir_build_load_global(
                     &b_shader, 4, 32,
                     nir_iadd_imm(&b_shader, instance_node_addr,
                                  offsetof(struct radv_bvh_instance_node, otw_matrix) + r * 16));
               ret =
                  nir_vec3(&b_shader, nir_channel(&b_shader, rows[0], c),
                           nir_channel(&b_shader, rows[1], c), nir_channel(&b_shader, rows[2], c));
               break;
            }
            case nir_intrinsic_load_ray_object_origin: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[3];
               nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);
               ret = nir_build_vec3_mat_mult(&b_shader, nir_load_var(&b_shader, vars->origin),
                                             wto_matrix, true);
               break;
            }
            case nir_intrinsic_load_ray_object_direction: {
               nir_ssa_def *instance_node_addr = nir_load_var(&b_shader, vars->instance_addr);
               nir_ssa_def *wto_matrix[3];
               nir_build_wto_matrix_load(&b_shader, instance_node_addr, wto_matrix);
               ret = nir_build_vec3_mat_mult(&b_shader, nir_load_var(&b_shader, vars->direction),
                                             wto_matrix, false);
               break;
            }
            case nir_intrinsic_load_intersection_opaque_amd: {
               ret = nir_load_var(&b_shader, vars->opaque);
               break;
            }
            case nir_intrinsic_load_cull_mask: {
               ret =
                  nir_ushr_imm(&b_shader, nir_load_var(&b_shader, vars->cull_mask_and_flags), 24);
               break;
            }
            case nir_intrinsic_ignore_ray_intersection: {
               nir_store_var(&b_shader, vars->ahit_accept, nir_imm_false(&b_shader), 0x1);

               /* The if is a workaround to avoid having to fix up control flow manually */
               nir_push_if(&b_shader, nir_imm_true(&b_shader));
               nir_jump(&b_shader, nir_jump_return);
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_terminate_ray: {
               nir_store_var(&b_shader, vars->ahit_accept, nir_imm_true(&b_shader), 0x1);
               nir_store_var(&b_shader, vars->ahit_terminate, nir_imm_true(&b_shader), 0x1);

               /* The if is a workaround to avoid having to fix up control flow manually */
               nir_push_if(&b_shader, nir_imm_true(&b_shader));
               nir_jump(&b_shader, nir_jump_return);
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_report_ray_intersection: {
               nir_push_if(
                  &b_shader,
                  nir_iand(
                     &b_shader,
                     nir_fge(&b_shader, nir_load_var(&b_shader, vars->tmax), intr->src[0].ssa),
                     nir_fge(&b_shader, intr->src[0].ssa, nir_load_var(&b_shader, vars->tmin))));
               {
                  nir_store_var(&b_shader, vars->ahit_accept, nir_imm_true(&b_shader), 0x1);
                  nir_store_var(&b_shader, vars->tmax, intr->src[0].ssa, 1);
                  nir_store_var(&b_shader, vars->hit_kind, intr->src[1].ssa, 1);
               }
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_load_sbt_offset_amd: {
               ret = nir_load_var(&b_shader, vars->sbt_offset);
               break;
            }
            case nir_intrinsic_load_sbt_stride_amd: {
               ret = nir_load_var(&b_shader, vars->sbt_stride);
               break;
            }
            case nir_intrinsic_load_accel_struct_amd: {
               ret = nir_load_var(&b_shader, vars->accel_struct);
               break;
            }
            case nir_intrinsic_load_cull_mask_and_flags_amd: {
               ret = nir_load_var(&b_shader, vars->cull_mask_and_flags);
               break;
            }
            case nir_intrinsic_execute_closest_hit_amd: {
               nir_store_var(&b_shader, vars->tmax, intr->src[1].ssa, 0x1);
               nir_store_var(&b_shader, vars->primitive_id, intr->src[2].ssa, 0x1);
               nir_store_var(&b_shader, vars->instance_addr, intr->src[3].ssa, 0x1);
               nir_store_var(&b_shader, vars->geometry_id_and_flags, intr->src[4].ssa, 0x1);
               nir_store_var(&b_shader, vars->hit_kind, intr->src[5].ssa, 0x1);
               load_sbt_entry(&b_shader, vars, intr->src[0].ssa, SBT_HIT, SBT_CLOSEST_HIT_IDX);

               nir_ssa_def *should_return =
                  nir_test_mask(&b_shader, nir_load_var(&b_shader, vars->cull_mask_and_flags),
                                SpvRayFlagsSkipClosestHitShaderKHRMask);

               if (!(vars->create_info->flags &
                     VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_CLOSEST_HIT_SHADERS_BIT_KHR)) {
                  should_return =
                     nir_ior(&b_shader, should_return,
                             nir_ieq_imm(&b_shader, nir_load_var(&b_shader, vars->idx), 0));
               }

               /* should_return is set if we had a hit but we won't be calling the closest hit
                * shader and hence need to return immediately to the calling shader. */
               nir_push_if(&b_shader, should_return);
               insert_rt_return(&b_shader, vars);
               nir_pop_if(&b_shader, NULL);
               break;
            }
            case nir_intrinsic_execute_miss_amd: {
               nir_store_var(&b_shader, vars->tmax, intr->src[0].ssa, 0x1);
               nir_ssa_def *undef = nir_ssa_undef(&b_shader, 1, 32);
               nir_store_var(&b_shader, vars->primitive_id, undef, 0x1);
               nir_store_var(&b_shader, vars->instance_addr, nir_ssa_undef(&b_shader, 1, 64), 0x1);
               nir_store_var(&b_shader, vars->geometry_id_and_flags, undef, 0x1);
               nir_store_var(&b_shader, vars->hit_kind, undef, 0x1);
               nir_ssa_def *miss_index = nir_load_var(&b_shader, vars->miss_index);
               load_sbt_entry(&b_shader, vars, miss_index, SBT_MISS, SBT_GENERAL_IDX);

               if (!(vars->create_info->flags &
                     VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_MISS_SHADERS_BIT_KHR)) {
                  /* In case of a NULL miss shader, do nothing and just return. */
                  nir_push_if(&b_shader,
                              nir_ieq_imm(&b_shader, nir_load_var(&b_shader, vars->idx), 0));
                  insert_rt_return(&b_shader, vars);
                  nir_pop_if(&b_shader, NULL);
               }

               break;
            }
            default:
               continue;
            }

            if (ret)
               nir_ssa_def_rewrite_uses(&intr->dest.ssa, ret);
            nir_instr_remove(instr);
            break;
         }
         case nir_instr_type_jump: {
            nir_jump_instr *jump = nir_instr_as_jump(instr);
            if (jump->type == nir_jump_halt) {
               b_shader.cursor = nir_instr_remove(instr);
               nir_jump(&b_shader, nir_jump_return);
            }
            break;
         }
         default:
            break;
         }
      }
   }

   nir_metadata_preserve(nir_shader_get_entrypoint(shader), nir_metadata_none);
}

static bool
lower_hit_attrib_deref(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_deref &&
       intrin->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   if (!nir_deref_mode_is(deref, nir_var_ray_hit_attrib))
      return false;

   assert(deref->deref_type == nir_deref_type_var);

   b->cursor = nir_after_instr(instr);

   if (intrin->intrinsic == nir_intrinsic_load_deref) {
      uint32_t num_components = intrin->dest.ssa.num_components;
      uint32_t bit_size = intrin->dest.ssa.bit_size;

      nir_ssa_def *components[NIR_MAX_VEC_COMPONENTS];

      for (uint32_t comp = 0; comp < num_components; comp++) {
         uint32_t offset = deref->var->data.driver_location + comp * bit_size / 8;
         uint32_t base = offset / 4;
         uint32_t comp_offset = offset % 4;

         if (bit_size == 64) {
            components[comp] = nir_pack_64_2x32_split(b, nir_load_hit_attrib_amd(b, .base = base),
                                                      nir_load_hit_attrib_amd(b, .base = base + 1));
         } else if (bit_size == 32) {
            components[comp] = nir_load_hit_attrib_amd(b, .base = base);
         } else if (bit_size == 16) {
            components[comp] = nir_channel(
               b, nir_unpack_32_2x16(b, nir_load_hit_attrib_amd(b, .base = base)), comp_offset / 2);
         } else if (bit_size == 8) {
            components[comp] = nir_channel(
               b, nir_unpack_bits(b, nir_load_hit_attrib_amd(b, .base = base), 8), comp_offset);
         } else {
            unreachable("Invalid bit_size");
         }
      }

      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_vec(b, components, num_components));
   } else {
      nir_ssa_def *value = intrin->src[1].ssa;
      uint32_t num_components = value->num_components;
      uint32_t bit_size = value->bit_size;

      for (uint32_t comp = 0; comp < num_components; comp++) {
         uint32_t offset = deref->var->data.driver_location + comp * bit_size / 8;
         uint32_t base = offset / 4;
         uint32_t comp_offset = offset % 4;

         nir_ssa_def *component = nir_channel(b, value, comp);

         if (bit_size == 64) {
            nir_store_hit_attrib_amd(b, nir_unpack_64_2x32_split_x(b, component), .base = base);
            nir_store_hit_attrib_amd(b, nir_unpack_64_2x32_split_y(b, component), .base = base + 1);
         } else if (bit_size == 32) {
            nir_store_hit_attrib_amd(b, component, .base = base);
         } else if (bit_size == 16) {
            nir_ssa_def *prev = nir_unpack_32_2x16(b, nir_load_hit_attrib_amd(b, .base = base));
            nir_ssa_def *components[2];
            for (uint32_t word = 0; word < 2; word++)
               components[word] = (word == comp_offset / 2) ? nir_channel(b, value, comp)
                                                            : nir_channel(b, prev, word);
            nir_store_hit_attrib_amd(b, nir_pack_32_2x16(b, nir_vec(b, components, 2)),
                                     .base = base);
         } else if (bit_size == 8) {
            nir_ssa_def *prev = nir_unpack_bits(b, nir_load_hit_attrib_amd(b, .base = base), 8);
            nir_ssa_def *components[4];
            for (uint32_t byte = 0; byte < 4; byte++)
               components[byte] =
                  (byte == comp_offset) ? nir_channel(b, value, comp) : nir_channel(b, prev, byte);
            nir_store_hit_attrib_amd(b, nir_pack_32_4x8(b, nir_vec(b, components, 4)),
                                     .base = base);
         } else {
            unreachable("Invalid bit_size");
         }
      }
   }

   nir_instr_remove(instr);
   return true;
}

static bool
lower_hit_attrib_derefs(nir_shader *shader)
{
   bool progress = nir_shader_instructions_pass(
      shader, lower_hit_attrib_deref, nir_metadata_block_index | nir_metadata_dominance, NULL);
   if (progress) {
      nir_remove_dead_derefs(shader);
      nir_remove_dead_variables(shader, nir_var_ray_hit_attrib, NULL);
   }

   return progress;
}

/* Lowers hit attributes to registers or shared memory. If hit_attribs is NULL, attributes are
 * lowered to shared memory. */
static void
lower_hit_attribs(nir_shader *shader, nir_variable **hit_attribs, uint32_t workgroup_size)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_foreach_variable_with_modes (attrib, shader, nir_var_ray_hit_attrib)
      attrib->data.mode = nir_var_shader_temp;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_load_hit_attrib_amd &&
             intrin->intrinsic != nir_intrinsic_store_hit_attrib_amd)
            continue;

         b.cursor = nir_after_instr(instr);

         nir_ssa_def *offset;
         if (!hit_attribs)
            offset = nir_imul_imm(&b,
                                  nir_iadd_imm(&b, nir_load_local_invocation_index(&b),
                                               nir_intrinsic_base(intrin) * workgroup_size),
                                  sizeof(uint32_t));

         if (intrin->intrinsic == nir_intrinsic_load_hit_attrib_amd) {
            nir_ssa_def *ret;
            if (hit_attribs)
               ret = nir_load_var(&b, hit_attribs[nir_intrinsic_base(intrin)]);
            else
               ret = nir_load_shared(&b, 1, 32, offset, .base = 0, .align_mul = 4);
            nir_ssa_def_rewrite_uses(nir_instr_ssa_def(instr), ret);
         } else {
            if (hit_attribs)
               nir_store_var(&b, hit_attribs[nir_intrinsic_base(intrin)], intrin->src->ssa, 0x1);
            else
               nir_store_shared(&b, intrin->src->ssa, offset, .base = 0, .align_mul = 4);
         }
         nir_instr_remove(instr);
      }
   }
}

static void
inline_constants(nir_shader *dst, nir_shader *src)
{
   if (!src->constant_data_size)
      return;

   uint32_t align_mul = 1;
   if (dst->constant_data_size) {
      nir_foreach_block (block, nir_shader_get_entrypoint(src)) {
         nir_foreach_instr (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);
            if (intrinsic->intrinsic == nir_intrinsic_load_constant)
               align_mul = MAX2(align_mul, nir_intrinsic_align_mul(intrinsic));
         }
      }
   }

   uint32_t old_constant_data_size = dst->constant_data_size;
   uint32_t base_offset = align(dst->constant_data_size, align_mul);
   dst->constant_data_size = base_offset + src->constant_data_size;
   dst->constant_data =
      rerzalloc_size(dst, dst->constant_data, old_constant_data_size, dst->constant_data_size);
   memcpy((char *)dst->constant_data + base_offset, src->constant_data, src->constant_data_size);

   if (!base_offset)
      return;

   nir_foreach_block (block, nir_shader_get_entrypoint(src)) {
      nir_foreach_instr (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);
         if (intrinsic->intrinsic == nir_intrinsic_load_constant)
            nir_intrinsic_set_base(intrinsic, base_offset + nir_intrinsic_base(intrinsic));
      }
   }
}

static void
insert_rt_case(nir_builder *b, nir_shader *shader, struct rt_variables *vars, nir_ssa_def *idx,
               uint32_t call_idx_base, uint32_t call_idx, unsigned stage_idx,
               struct radv_ray_tracing_module *groups)
{
   uint32_t workgroup_size = b->shader->info.workgroup_size[0] * b->shader->info.workgroup_size[1] *
                             b->shader->info.workgroup_size[2];

   struct hash_table *var_remap = _mesa_pointer_hash_table_create(NULL);

   nir_opt_dead_cf(shader);

   struct rt_variables src_vars = create_rt_variables(shader, vars->create_info);
   map_rt_variables(var_remap, &src_vars, vars);

   NIR_PASS_V(shader, lower_rt_instructions, &src_vars, call_idx_base);

   NIR_PASS(_, shader, nir_opt_remove_phis);
   NIR_PASS(_, shader, nir_lower_returns);
   NIR_PASS(_, shader, nir_opt_dce);

   /* The traversal shader has a call_idx of 1 */
   if (shader->info.stage == MESA_SHADER_CLOSEST_HIT || call_idx == 1)
      NIR_PASS_V(shader, lower_hit_attribs, NULL, workgroup_size);

   src_vars.stack_size = MAX2(src_vars.stack_size, shader->scratch_size);

   inline_constants(b->shader, shader);

   nir_push_if(b, nir_ieq_imm(b, idx, call_idx));
   nir_inline_function_impl(b, nir_shader_get_entrypoint(shader), NULL, var_remap);
   nir_pop_if(b, NULL);

   ralloc_free(var_remap);

   /* reserve stack sizes */
   for (uint32_t group_idx = 0; group_idx < vars->create_info->groupCount; group_idx++) {
      const VkRayTracingShaderGroupCreateInfoKHR *group = vars->create_info->pGroups + group_idx;

      if (stage_idx == group->generalShader || stage_idx == group->closestHitShader)
         groups[group_idx].stack_size.recursive_size =
            MAX2(groups[group_idx].stack_size.recursive_size, src_vars.stack_size);

      if (stage_idx == group->anyHitShader || stage_idx == group->intersectionShader)
         groups[group_idx].stack_size.non_recursive_size =
            MAX2(groups[group_idx].stack_size.non_recursive_size, src_vars.stack_size);
   }
}

static nir_shader *
parse_rt_stage(struct radv_device *device, const VkPipelineShaderStageCreateInfo *sinfo,
               const struct radv_pipeline_key *key)
{
   struct radv_pipeline_stage rt_stage;

   radv_pipeline_stage_init(sinfo, &rt_stage, vk_to_mesa_shader_stage(sinfo->stage));

   nir_shader *shader = radv_shader_spirv_to_nir(device, &rt_stage, key, false);

   if (shader->info.stage == MESA_SHADER_RAYGEN || shader->info.stage == MESA_SHADER_CLOSEST_HIT ||
       shader->info.stage == MESA_SHADER_CALLABLE || shader->info.stage == MESA_SHADER_MISS) {
      nir_block *last_block = nir_impl_last_block(nir_shader_get_entrypoint(shader));
      nir_builder b_inner;
      nir_builder_init(&b_inner, nir_shader_get_entrypoint(shader));
      b_inner.cursor = nir_after_block(last_block);
      nir_rt_return_amd(&b_inner);
   }

   NIR_PASS(_, shader, nir_split_struct_vars, nir_var_ray_hit_attrib);
   NIR_PASS(_, shader, nir_lower_indirect_derefs, nir_var_ray_hit_attrib, UINT32_MAX);
   NIR_PASS(_, shader, nir_split_array_vars, nir_var_ray_hit_attrib);

   NIR_PASS(_, shader, nir_lower_vars_to_explicit_types,
            nir_var_function_temp | nir_var_shader_call_data | nir_var_ray_hit_attrib,
            glsl_get_natural_size_align_bytes);

   NIR_PASS(_, shader, lower_rt_derefs);
   NIR_PASS(_, shader, lower_hit_attrib_derefs);

   NIR_PASS(_, shader, nir_lower_explicit_io, nir_var_function_temp,
            nir_address_format_32bit_offset);

   return shader;
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

   nir_builder build;
   nir_builder_init(&build, impl);
   nir_builder *b = &build;

   b->cursor = nir_before_cf_list(&impl->body);

   nir_ssa_def *commit_ptr = nir_load_param(b, 0);
   nir_ssa_def *hit_t = nir_load_param(b, 1);
   nir_ssa_def *hit_kind = nir_load_param(b, 2);
   nir_ssa_def *scratch_offset = nir_load_param(b, 3);

   nir_deref_instr *commit =
      nir_build_deref_cast(b, commit_ptr, nir_var_function_temp, glsl_bool_type(), 0);

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
               nir_ssa_def_rewrite_uses(&intrin->dest.ssa, hit_t);
               nir_instr_remove(&intrin->instr);
               break;

            case nir_intrinsic_load_ray_hit_kind:
               nir_ssa_def_rewrite_uses(&intrin->dest.ssa, hit_kind);
               nir_instr_remove(&intrin->instr);
               break;

            /* We place all any_hit scratch variables after intersection scratch variables.
             * For that reason, we increment the scratch offset by the intersection scratch
             * size. For call_data, we have to subtract the offset again.
             */
            case nir_intrinsic_load_scratch:
               b->cursor = nir_before_instr(instr);
               nir_instr_rewrite_src_ssa(instr, &intrin->src[0],
                                         nir_iadd_nuw(b, scratch_offset, intrin->src[0].ssa));
               break;
            case nir_intrinsic_store_scratch:
               b->cursor = nir_before_instr(instr);
               nir_instr_rewrite_src_ssa(instr, &intrin->src[1],
                                         nir_iadd_nuw(b, scratch_offset, intrin->src[1].ssa));
               break;
            case nir_intrinsic_load_rt_arg_scratch_offset_amd:
               b->cursor = nir_after_instr(instr);
               nir_ssa_def *arg_offset = nir_isub(b, &intrin->dest.ssa, scratch_offset);
               nir_ssa_def_rewrite_uses_after(&intrin->dest.ssa, arg_offset, arg_offset->parent_instr);
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
   void *dead_ctx = ralloc_context(intersection);

   nir_function_impl *any_hit_impl = NULL;
   struct hash_table *any_hit_var_remap = NULL;
   if (any_hit) {
      any_hit = nir_shader_clone(dead_ctx, any_hit);
      NIR_PASS(_, any_hit, nir_opt_dce);

      inline_constants(intersection, any_hit);

      any_hit_impl = lower_any_hit_for_intersection(any_hit);
      any_hit_var_remap = _mesa_pointer_hash_table_create(dead_ctx);
   }

   nir_function_impl *impl = nir_shader_get_entrypoint(intersection);

   nir_builder build;
   nir_builder_init(&build, impl);
   nir_builder *b = &build;

   b->cursor = nir_before_cf_list(&impl->body);

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
         nir_ssa_def *hit_t = nir_ssa_for_src(b, intrin->src[0], 1);
         nir_ssa_def *hit_kind = nir_ssa_for_src(b, intrin->src[1], 1);
         nir_ssa_def *min_t = nir_load_ray_t_min(b);
         nir_ssa_def *max_t = nir_load_ray_t_max(b);

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
                  nir_ssa_def *params[] = {
                     &nir_build_deref_var(b, commit_tmp)->dest.ssa,
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

         nir_ssa_def *accepted = nir_load_var(b, commit_tmp);
         nir_ssa_def_rewrite_uses(&intrin->dest.ssa, accepted);
      }
   }
   /* Any-hit scratch variables are placed after intersection scratch variables. */
   intersection->scratch_size += any_hit->scratch_size;
   nir_metadata_preserve(impl, nir_metadata_none);

   /* We did some inlining; have to re-index SSA defs */
   nir_index_ssa_defs(impl);

   /* Eliminate the casts introduced for the commit return of the any-hit shader. */
   NIR_PASS(_, intersection, nir_opt_deref);

   ralloc_free(dead_ctx);
}

/* Variables only used internally to ray traversal. This is data that describes
 * the current state of the traversal vs. what we'd give to a shader.  e.g. what
 * is the instance we're currently visiting vs. what is the instance of the
 * closest hit. */
struct rt_traversal_vars {
   nir_variable *origin;
   nir_variable *dir;
   nir_variable *inv_dir;
   nir_variable *sbt_offset_and_flags;
   nir_variable *instance_addr;
   nir_variable *hit;
   nir_variable *bvh_base;
   nir_variable *stack;
   nir_variable *top_stack;
   nir_variable *stack_low_watermark;
   nir_variable *current_node;
   nir_variable *previous_node;
   nir_variable *instance_top_node;
   nir_variable *instance_bottom_node;
};

static struct rt_traversal_vars
init_traversal_vars(nir_builder *b)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   struct rt_traversal_vars ret;

   ret.origin = nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "traversal_origin");
   ret.dir = nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "traversal_dir");
   ret.inv_dir =
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "traversal_inv_dir");
   ret.sbt_offset_and_flags = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(),
                                                  "traversal_sbt_offset_and_flags");
   ret.instance_addr =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint64_t_type(), "instance_addr");
   ret.hit = nir_variable_create(b->shader, nir_var_shader_temp, glsl_bool_type(), "traversal_hit");
   ret.bvh_base = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint64_t_type(),
                                      "traversal_bvh_base");
   ret.stack =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "traversal_stack_ptr");
   ret.top_stack = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(),
                                       "traversal_top_stack_ptr");
   ret.stack_low_watermark = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(),
                                                 "traversal_stack_low_watermark");
   ret.current_node =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "current_node;");
   ret.previous_node =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "previous_node");
   ret.instance_top_node =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "instance_top_node");
   ret.instance_bottom_node =
      nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "instance_bottom_node");
   return ret;
}

struct traversal_data {
   struct radv_device *device;
   const VkRayTracingPipelineCreateInfoKHR *createInfo;
   struct rt_variables *vars;
   struct rt_traversal_vars *trav_vars;
   nir_variable *barycentrics;

   struct radv_ray_tracing_module *groups;
   const struct radv_pipeline_key *key;
};

static void
visit_any_hit_shaders(struct radv_device *device,
                      const VkRayTracingPipelineCreateInfoKHR *pCreateInfo, nir_builder *b,
                      struct traversal_data *data, struct rt_variables *vars)
{
   nir_ssa_def *sbt_idx = nir_load_var(b, vars->idx);

   if (!(vars->create_info->flags & VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR))
      nir_push_if(b, nir_ine_imm(b, sbt_idx, 0));

   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &pCreateInfo->pGroups[i];
      uint32_t shader_id = VK_SHADER_UNUSED_KHR;

      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR:
         shader_id = group_info->anyHitShader;
         break;
      default:
         break;
      }
      if (shader_id == VK_SHADER_UNUSED_KHR)
         continue;

      /* Avoid emitting stages with the same shaders/handles multiple times. */
      bool is_dup = false;
      for (unsigned j = 0; j < i; ++j)
         if (data->groups[j].handle.any_hit_index == data->groups[i].handle.any_hit_index)
            is_dup = true;

      if (is_dup)
         continue;

      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[shader_id];
      nir_shader *nir_stage = parse_rt_stage(device, stage, data->key);

      insert_rt_case(b, nir_stage, vars, sbt_idx, 0, data->groups[i].handle.any_hit_index,
                     shader_id, data->groups);
      ralloc_free(nir_stage);
   }

   if (!(vars->create_info->flags & VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_ANY_HIT_SHADERS_BIT_KHR))
      nir_pop_if(b, NULL);
}

static void
handle_candidate_triangle(nir_builder *b, struct radv_triangle_intersection *intersection,
                          const struct radv_ray_traversal_args *args,
                          const struct radv_ray_flags *ray_flags)
{
   struct traversal_data *data = args->data;

   nir_ssa_def *geometry_id = nir_iand_imm(b, intersection->base.geometry_id_and_flags, 0xfffffff);
   nir_ssa_def *sbt_idx = nir_iadd(
      b,
      nir_iadd(b, nir_load_var(b, data->vars->sbt_offset),
               nir_iand_imm(b, nir_load_var(b, data->trav_vars->sbt_offset_and_flags), 0xffffff)),
      nir_imul(b, nir_load_var(b, data->vars->sbt_stride), geometry_id));

   nir_ssa_def *hit_kind =
      nir_bcsel(b, intersection->frontface, nir_imm_int(b, 0xFE), nir_imm_int(b, 0xFF));

   nir_ssa_def *prev_barycentrics = nir_load_var(b, data->barycentrics);
   nir_store_var(b, data->barycentrics, intersection->barycentrics, 0x3);

   nir_store_var(b, data->vars->ahit_accept, nir_imm_true(b), 0x1);
   nir_store_var(b, data->vars->ahit_terminate, nir_imm_false(b), 0x1);

   nir_push_if(b, nir_inot(b, intersection->base.opaque));
   {
      struct rt_variables inner_vars = create_inner_vars(b, data->vars);

      nir_store_var(b, inner_vars.primitive_id, intersection->base.primitive_id, 1);
      nir_store_var(b, inner_vars.geometry_id_and_flags, intersection->base.geometry_id_and_flags,
                    1);
      nir_store_var(b, inner_vars.tmax, intersection->t, 0x1);
      nir_store_var(b, inner_vars.instance_addr, nir_load_var(b, data->trav_vars->instance_addr),
                    0x1);
      nir_store_var(b, inner_vars.hit_kind, hit_kind, 0x1);

      load_sbt_entry(b, &inner_vars, sbt_idx, SBT_HIT, SBT_ANY_HIT_IDX);

      visit_any_hit_shaders(data->device, data->createInfo, b, args->data, &inner_vars);

      nir_push_if(b, nir_inot(b, nir_load_var(b, data->vars->ahit_accept)));
      {
         nir_store_var(b, data->barycentrics, prev_barycentrics, 0x3);
         nir_jump(b, nir_jump_continue);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);

   nir_store_var(b, data->vars->primitive_id, intersection->base.primitive_id, 1);
   nir_store_var(b, data->vars->geometry_id_and_flags, intersection->base.geometry_id_and_flags, 1);
   nir_store_var(b, data->vars->tmax, intersection->t, 0x1);
   nir_store_var(b, data->vars->instance_addr, nir_load_var(b, data->trav_vars->instance_addr),
                 0x1);
   nir_store_var(b, data->vars->hit_kind, hit_kind, 0x1);

   nir_store_var(b, data->vars->idx, sbt_idx, 1);
   nir_store_var(b, data->trav_vars->hit, nir_imm_true(b), 1);

   nir_ssa_def *ray_terminated = nir_load_var(b, data->vars->ahit_terminate);
   nir_push_if(b, nir_ior(b, ray_flags->terminate_on_first_hit, ray_terminated));
   {
      nir_jump(b, nir_jump_break);
   }
   nir_pop_if(b, NULL);
}

static void
handle_candidate_aabb(nir_builder *b, struct radv_leaf_intersection *intersection,
                      const struct radv_ray_traversal_args *args)
{
   struct traversal_data *data = args->data;

   nir_ssa_def *geometry_id = nir_iand_imm(b, intersection->geometry_id_and_flags, 0xfffffff);
   nir_ssa_def *sbt_idx = nir_iadd(
      b,
      nir_iadd(b, nir_load_var(b, data->vars->sbt_offset),
               nir_iand_imm(b, nir_load_var(b, data->trav_vars->sbt_offset_and_flags), 0xffffff)),
      nir_imul(b, nir_load_var(b, data->vars->sbt_stride), geometry_id));

   struct rt_variables inner_vars = create_inner_vars(b, data->vars);

   /* For AABBs the intersection shader writes the hit kind, and only does it if it is the
    * next closest hit candidate. */
   inner_vars.hit_kind = data->vars->hit_kind;

   nir_store_var(b, inner_vars.primitive_id, intersection->primitive_id, 1);
   nir_store_var(b, inner_vars.geometry_id_and_flags, intersection->geometry_id_and_flags, 1);
   nir_store_var(b, inner_vars.tmax, nir_load_var(b, data->vars->tmax), 0x1);
   nir_store_var(b, inner_vars.instance_addr, nir_load_var(b, data->trav_vars->instance_addr), 0x1);
   nir_store_var(b, inner_vars.opaque, intersection->opaque, 1);

   load_sbt_entry(b, &inner_vars, sbt_idx, SBT_HIT, SBT_INTERSECTION_IDX);

   nir_store_var(b, data->vars->ahit_accept, nir_imm_false(b), 0x1);
   nir_store_var(b, data->vars->ahit_terminate, nir_imm_false(b), 0x1);

   if (!(data->vars->create_info->flags &
         VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_INTERSECTION_SHADERS_BIT_KHR))
      nir_push_if(b, nir_ine_imm(b, nir_load_var(b, inner_vars.idx), 0));

   for (unsigned i = 0; i < data->createInfo->groupCount; ++i) {
      const VkRayTracingShaderGroupCreateInfoKHR *group_info = &data->createInfo->pGroups[i];
      uint32_t shader_id = VK_SHADER_UNUSED_KHR;
      uint32_t any_hit_shader_id = VK_SHADER_UNUSED_KHR;

      switch (group_info->type) {
      case VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR:
         shader_id = group_info->intersectionShader;
         any_hit_shader_id = group_info->anyHitShader;
         break;
      default:
         break;
      }
      if (shader_id == VK_SHADER_UNUSED_KHR)
         continue;

      /* Avoid emitting stages with the same shaders/handles multiple times. */
      bool is_dup = false;
      for (unsigned j = 0; j < i; ++j)
         if (data->groups[j].handle.intersection_index == data->groups[i].handle.intersection_index)
            is_dup = true;

      if (is_dup)
         continue;

      const VkPipelineShaderStageCreateInfo *stage = &data->createInfo->pStages[shader_id];
      nir_shader *nir_stage = parse_rt_stage(data->device, stage, data->key);

      nir_shader *any_hit_stage = NULL;
      if (any_hit_shader_id != VK_SHADER_UNUSED_KHR) {
         stage = &data->createInfo->pStages[any_hit_shader_id];
         any_hit_stage = parse_rt_stage(data->device, stage, data->key);

         nir_lower_intersection_shader(nir_stage, any_hit_stage);
         ralloc_free(any_hit_stage);
      }

      insert_rt_case(b, nir_stage, &inner_vars, nir_load_var(b, inner_vars.idx), 0,
                     data->groups[i].handle.intersection_index, shader_id, data->groups);
      ralloc_free(nir_stage);
   }

   if (!(data->vars->create_info->flags &
         VK_PIPELINE_CREATE_RAY_TRACING_NO_NULL_INTERSECTION_SHADERS_BIT_KHR))
      nir_pop_if(b, NULL);

   nir_push_if(b, nir_load_var(b, data->vars->ahit_accept));
   {
      nir_store_var(b, data->vars->primitive_id, intersection->primitive_id, 1);
      nir_store_var(b, data->vars->geometry_id_and_flags, intersection->geometry_id_and_flags, 1);
      nir_store_var(b, data->vars->tmax, nir_load_var(b, inner_vars.tmax), 0x1);
      nir_store_var(b, data->vars->instance_addr, nir_load_var(b, data->trav_vars->instance_addr),
                    0x1);

      nir_store_var(b, data->vars->idx, sbt_idx, 1);
      nir_store_var(b, data->trav_vars->hit, nir_imm_true(b), 1);

      nir_ssa_def *terminate_on_first_hit =
         nir_test_mask(b, args->flags, SpvRayFlagsTerminateOnFirstHitKHRMask);
      nir_ssa_def *ray_terminated = nir_load_var(b, data->vars->ahit_terminate);
      nir_push_if(b, nir_ior(b, terminate_on_first_hit, ray_terminated));
      {
         nir_jump(b, nir_jump_break);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
store_stack_entry(nir_builder *b, nir_ssa_def *index, nir_ssa_def *value,
                  const struct radv_ray_traversal_args *args)
{
   nir_store_shared(b, value, index, .base = 0, .align_mul = 4);
}

static nir_ssa_def *
load_stack_entry(nir_builder *b, nir_ssa_def *index, const struct radv_ray_traversal_args *args)
{
   return nir_load_shared(b, 1, 32, index, .base = 0, .align_mul = 4);
}

static nir_shader *
build_traversal_shader(struct radv_device *device,
                       const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                       struct radv_ray_tracing_module *groups, const struct radv_pipeline_key *key)
{
   /* Create the traversal shader as an intersection shader to prevent validation failures due to
    * invalid variable modes.*/
   nir_builder b = radv_meta_init_shader(device, MESA_SHADER_INTERSECTION, "rt_traversal");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = device->physical_device->rt_wave_size == 64 ? 8 : 4;
   b.shader->info.shared_size =
      device->physical_device->rt_wave_size * MAX_STACK_ENTRY_COUNT * sizeof(uint32_t);
   struct rt_variables vars = create_rt_variables(b.shader, pCreateInfo);

   /* Register storage for hit attributes */
   nir_variable *hit_attribs[RADV_MAX_HIT_ATTRIB_SIZE / sizeof(uint32_t)];

   for (uint32_t i = 0; i < ARRAY_SIZE(hit_attribs); i++)
      hit_attribs[i] = nir_local_variable_create(nir_shader_get_entrypoint(b.shader),
                                                 glsl_uint_type(), "ahit_attrib");

   nir_variable *barycentrics = nir_variable_create(
      b.shader, nir_var_ray_hit_attrib, glsl_vector_type(GLSL_TYPE_FLOAT, 2), "barycentrics");
   barycentrics->data.driver_location = 0;

   /* initialize trace_ray arguments */
   nir_ssa_def *accel_struct = nir_load_accel_struct_amd(&b);
   nir_ssa_def *cull_mask_and_flags = nir_load_cull_mask_and_flags_amd(&b);
   nir_store_var(&b, vars.cull_mask_and_flags, cull_mask_and_flags, 0x1);
   nir_store_var(&b, vars.sbt_offset, nir_load_sbt_offset_amd(&b), 0x1);
   nir_store_var(&b, vars.sbt_stride, nir_load_sbt_stride_amd(&b), 0x1);
   nir_store_var(&b, vars.origin, nir_load_ray_world_origin(&b), 0x7);
   nir_store_var(&b, vars.tmin, nir_load_ray_t_min(&b), 0x1);
   nir_store_var(&b, vars.direction, nir_load_ray_world_direction(&b), 0x7);
   nir_store_var(&b, vars.tmax, nir_load_ray_t_max(&b), 0x1);
   nir_store_var(&b, vars.arg, nir_load_rt_arg_scratch_offset_amd(&b), 0x1);
   nir_store_var(&b, vars.stack_ptr, nir_imm_int(&b, 0), 0x1);

   struct rt_traversal_vars trav_vars = init_traversal_vars(&b);

   nir_store_var(&b, trav_vars.hit, nir_imm_false(&b), 1);

   nir_ssa_def *bvh_offset = nir_build_load_global(
      &b, 1, 32,
      nir_iadd_imm(&b, accel_struct, offsetof(struct radv_accel_struct_header, bvh_offset)),
      .access = ACCESS_NON_WRITEABLE);
   nir_ssa_def *root_bvh_base = nir_iadd(&b, accel_struct, nir_u2u64(&b, bvh_offset));
   root_bvh_base = build_addr_to_node(&b, root_bvh_base);

   nir_store_var(&b, trav_vars.bvh_base, root_bvh_base, 1);

   nir_ssa_def *vec3ones = nir_channels(&b, nir_imm_vec4(&b, 1.0, 1.0, 1.0, 1.0), 0x7);

   nir_store_var(&b, trav_vars.origin, nir_load_var(&b, vars.origin), 7);
   nir_store_var(&b, trav_vars.dir, nir_load_var(&b, vars.direction), 7);
   nir_store_var(&b, trav_vars.inv_dir, nir_fdiv(&b, vec3ones, nir_load_var(&b, trav_vars.dir)), 7);
   nir_store_var(&b, trav_vars.sbt_offset_and_flags, nir_imm_int(&b, 0), 1);
   nir_store_var(&b, trav_vars.instance_addr, nir_imm_int64(&b, 0), 1);

   nir_store_var(&b, trav_vars.stack,
                 nir_imul_imm(&b, nir_load_local_invocation_index(&b), sizeof(uint32_t)), 1);
   nir_store_var(&b, trav_vars.stack_low_watermark, nir_load_var(&b, trav_vars.stack), 1);
   nir_store_var(&b, trav_vars.current_node, nir_imm_int(&b, RADV_BVH_ROOT_NODE), 0x1);
   nir_store_var(&b, trav_vars.previous_node, nir_imm_int(&b, RADV_BVH_INVALID_NODE), 0x1);
   nir_store_var(&b, trav_vars.instance_top_node, nir_imm_int(&b, RADV_BVH_INVALID_NODE), 0x1);
   nir_store_var(&b, trav_vars.instance_bottom_node, nir_imm_int(&b, RADV_BVH_NO_INSTANCE_ROOT),
                 0x1);

   nir_store_var(&b, trav_vars.top_stack, nir_imm_int(&b, -1), 1);

   struct radv_ray_traversal_vars trav_vars_args = {
      .tmax = nir_build_deref_var(&b, vars.tmax),
      .origin = nir_build_deref_var(&b, trav_vars.origin),
      .dir = nir_build_deref_var(&b, trav_vars.dir),
      .inv_dir = nir_build_deref_var(&b, trav_vars.inv_dir),
      .bvh_base = nir_build_deref_var(&b, trav_vars.bvh_base),
      .stack = nir_build_deref_var(&b, trav_vars.stack),
      .top_stack = nir_build_deref_var(&b, trav_vars.top_stack),
      .stack_low_watermark = nir_build_deref_var(&b, trav_vars.stack_low_watermark),
      .current_node = nir_build_deref_var(&b, trav_vars.current_node),
      .previous_node = nir_build_deref_var(&b, trav_vars.previous_node),
      .instance_top_node = nir_build_deref_var(&b, trav_vars.instance_top_node),
      .instance_bottom_node = nir_build_deref_var(&b, trav_vars.instance_bottom_node),
      .instance_addr = nir_build_deref_var(&b, trav_vars.instance_addr),
      .sbt_offset_and_flags = nir_build_deref_var(&b, trav_vars.sbt_offset_and_flags),
   };

   struct traversal_data data = {
      .device = device,
      .createInfo = pCreateInfo,
      .vars = &vars,
      .trav_vars = &trav_vars,
      .barycentrics = barycentrics,
      .groups = groups,
      .key = key,
   };

   struct radv_ray_traversal_args args = {
      .root_bvh_base = root_bvh_base,
      .flags = cull_mask_and_flags,
      .cull_mask = cull_mask_and_flags,
      .origin = nir_load_var(&b, vars.origin),
      .tmin = nir_load_var(&b, vars.tmin),
      .dir = nir_load_var(&b, vars.direction),
      .vars = trav_vars_args,
      .stack_stride = device->physical_device->rt_wave_size * sizeof(uint32_t),
      .stack_entries = MAX_STACK_ENTRY_COUNT,
      .stack_base = 0,
      .stack_store_cb = store_stack_entry,
      .stack_load_cb = load_stack_entry,
      .aabb_cb = (pCreateInfo->flags & VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR)
                    ? NULL
                    : handle_candidate_aabb,
      .triangle_cb = (pCreateInfo->flags & VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR)
                        ? NULL
                        : handle_candidate_triangle,
      .data = &data,
   };

   radv_build_ray_traversal(device, &b, &args);

   nir_metadata_preserve(nir_shader_get_entrypoint(b.shader), nir_metadata_none);
   lower_hit_attrib_derefs(b.shader);
   lower_hit_attribs(b.shader, hit_attribs, device->physical_device->rt_wave_size);

   /* Initialize follow-up shader. */
   nir_push_if(&b, nir_load_var(&b, trav_vars.hit));
   {
      for (int i = 0; i < ARRAY_SIZE(hit_attribs); ++i)
         nir_store_hit_attrib_amd(&b, nir_load_var(&b, hit_attribs[i]), .base = i);
      nir_execute_closest_hit_amd(
         &b, nir_load_var(&b, vars.idx), nir_load_var(&b, vars.tmax),
         nir_load_var(&b, vars.primitive_id), nir_load_var(&b, vars.instance_addr),
         nir_load_var(&b, vars.geometry_id_and_flags), nir_load_var(&b, vars.hit_kind));
   }
   nir_push_else(&b, NULL);
   {
      /* Only load the miss shader if we actually miss. It is valid to not specify an SBT pointer
       * for miss shaders if none of the rays miss. */
      nir_execute_miss_amd(&b, nir_load_var(&b, vars.tmax));
   }
   nir_pop_if(&b, NULL);

   /* Deal with all the inline functions. */
   nir_index_ssa_defs(nir_shader_get_entrypoint(b.shader));
   nir_metadata_preserve(nir_shader_get_entrypoint(b.shader), nir_metadata_none);

   /* Lower and cleanup variables */
   NIR_PASS_V(b.shader, nir_lower_global_vars_to_local);
   NIR_PASS_V(b.shader, nir_lower_vars_to_ssa);

   return b.shader;
}

static bool
should_move_rt_instruction(nir_intrinsic_op intrinsic)
{
   switch (intrinsic) {
   case nir_intrinsic_load_hit_attrib_amd:
   case nir_intrinsic_load_rt_arg_scratch_offset_amd:
   case nir_intrinsic_load_ray_flags:
   case nir_intrinsic_load_ray_object_origin:
   case nir_intrinsic_load_ray_world_origin:
   case nir_intrinsic_load_ray_t_min:
   case nir_intrinsic_load_ray_object_direction:
   case nir_intrinsic_load_ray_world_direction:
   case nir_intrinsic_load_ray_t_max:
      return true;
   default:
      return false;
   }
}

static void
move_rt_instructions(nir_shader *shader)
{
   nir_cursor target = nir_before_cf_list(&nir_shader_get_entrypoint(shader)->body);

   nir_foreach_block (block, nir_shader_get_entrypoint(shader)) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

         if (!should_move_rt_instruction(intrinsic->intrinsic))
            continue;

         nir_instr_move(target, instr);
      }
   }

   nir_metadata_preserve(nir_shader_get_entrypoint(shader),
                         nir_metadata_all & (~nir_metadata_instr_index));
}

nir_shader *
create_rt_shader(struct radv_device *device, const VkRayTracingPipelineCreateInfoKHR *pCreateInfo,
                 struct radv_ray_tracing_module *groups, const struct radv_pipeline_key *key)
{
   nir_builder b = radv_meta_init_shader(device, MESA_SHADER_RAYGEN, "rt_combined");
   b.shader->info.internal = false;
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = device->physical_device->rt_wave_size == 64 ? 8 : 4;
   b.shader->info.shared_size = device->physical_device->rt_wave_size * RADV_MAX_HIT_ATTRIB_SIZE;

   struct rt_variables vars = create_rt_variables(b.shader, pCreateInfo);
   load_sbt_entry(&b, &vars, nir_imm_int(&b, 0), SBT_RAYGEN, SBT_GENERAL_IDX);
   nir_store_var(&b, vars.stack_ptr, nir_load_rt_dynamic_callable_stack_base_amd(&b), 0x1);

   nir_loop *loop = nir_push_loop(&b);
   nir_ssa_def *idx = nir_load_var(&b, vars.idx);

   /* Insert traversal shader */
   nir_shader *traversal = build_traversal_shader(device, pCreateInfo, groups, key);
   b.shader->info.shared_size = MAX2(b.shader->info.shared_size, traversal->info.shared_size);
   assert(b.shader->info.shared_size <= 32768);
   insert_rt_case(&b, traversal, &vars, idx, 0, 1, -1u, groups);
   ralloc_free(traversal);

   unsigned call_idx_base = 1;
   for (unsigned i = 0; i < pCreateInfo->groupCount; ++i) {
      unsigned stage_idx = VK_SHADER_UNUSED_KHR;
      if (pCreateInfo->pGroups[i].type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
         stage_idx = pCreateInfo->pGroups[i].generalShader;
      else
         stage_idx = pCreateInfo->pGroups[i].closestHitShader;

      if (stage_idx == VK_SHADER_UNUSED_KHR)
         continue;

      /* Avoid emitting stages with the same shaders/handles multiple times. */
      bool is_dup = false;
      for (unsigned j = 0; j < i; ++j)
         if (groups[j].handle.general_index == groups[i].handle.general_index)
            is_dup = true;

      if (is_dup)
         continue;

      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[stage_idx];
      ASSERTED gl_shader_stage type = vk_to_mesa_shader_stage(stage->stage);
      assert(type == MESA_SHADER_RAYGEN || type == MESA_SHADER_CALLABLE ||
             type == MESA_SHADER_CLOSEST_HIT || type == MESA_SHADER_MISS);

      nir_shader *nir_stage = parse_rt_stage(device, stage, key);

      /* Move ray tracing system values to the top that are set by rt_trace_ray
       * to prevent them from being overwritten by other rt_trace_ray calls.
       */
      NIR_PASS_V(nir_stage, move_rt_instructions);

      const nir_lower_shader_calls_options opts = {
         .address_format = nir_address_format_32bit_offset,
         .stack_alignment = 16,
         .localized_loads = true,
         .vectorizer_callback = radv_mem_vectorize_callback,
      };
      uint32_t num_resume_shaders = 0;
      nir_shader **resume_shaders = NULL;
      nir_lower_shader_calls(nir_stage, &opts, &resume_shaders, &num_resume_shaders, nir_stage);

      insert_rt_case(&b, nir_stage, &vars, idx, call_idx_base, groups[i].handle.general_index,
                     stage_idx, groups);
      for (unsigned j = 0; j < num_resume_shaders; ++j) {
         insert_rt_case(&b, resume_shaders[j], &vars, idx, call_idx_base, call_idx_base + 1 + j,
                        stage_idx, groups);
      }

      ralloc_free(nir_stage);
      call_idx_base += num_resume_shaders;
   }

   nir_pop_loop(&b, loop);

   /* Deal with all the inline functions. */
   nir_index_ssa_defs(nir_shader_get_entrypoint(b.shader));
   nir_metadata_preserve(nir_shader_get_entrypoint(b.shader), nir_metadata_none);

   return b.shader;
}
