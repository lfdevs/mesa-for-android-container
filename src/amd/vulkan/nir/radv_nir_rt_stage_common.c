/*
 * Copyright © 2025 Valve Corporation
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_nir_rt_stage_common.h"

#include "nir/radv_nir_rt_stage_functions.h"
#include "aco_nir_call_attribs.h"
#include "nir_builder.h"
#include "radv_device.h"
#include "radv_meta_nir.h"
#include "radv_physical_device.h"

struct radv_nir_sbt_data
radv_nir_load_sbt_entry(nir_builder *b, nir_def *base, nir_def *idx, enum radv_nir_sbt_type binding,
                        enum radv_nir_sbt_entry offset)
{
   struct radv_nir_sbt_data data;

   nir_def *desc = nir_pack_64_2x32(b, ac_nir_load_smem(b, 2, base, nir_imm_int(b, binding), 4, 0));

   nir_def *stride_offset = nir_imm_int(b, binding + (binding == SBT_RAYGEN ? 8 : 16));
   nir_def *stride = ac_nir_load_smem(b, 1, base, stride_offset, 4, 0);

   nir_def *addr = nir_iadd(b, desc, nir_u2u64(b, nir_iadd_imm(b, nir_imul(b, idx, stride), offset)));

   bool offset_is_addr = offset == SBT_RECURSIVE_PTR || offset == SBT_AHIT_ISEC_PTR;
   unsigned load_size = offset_is_addr ? 64 : 32;
   data.shader_addr = nir_load_global(b, 1, load_size, addr, .access = ACCESS_CAN_REORDER | ACCESS_NON_WRITEABLE);
   data.shader_record_ptr = nir_iadd_imm(b, addr, RADV_RT_HANDLE_SIZE - offset);

   return data;
}

void
radv_nir_inline_constants(nir_shader *dst, nir_shader *src)
{
   if (!src->constant_data_size)
      return;

   uint32_t old_constant_data_size = dst->constant_data_size;
   uint32_t base_offset = align(dst->constant_data_size, 64);
   dst->constant_data_size = base_offset + src->constant_data_size;
   dst->constant_data = rerzalloc_size(dst, dst->constant_data, old_constant_data_size, dst->constant_data_size);
   memcpy((char *)dst->constant_data + base_offset, src->constant_data, src->constant_data_size);

   if (!base_offset)
      return;

   uint32_t base_align_mul = base_offset ? 1 << (ffs(base_offset) - 1) : NIR_ALIGN_MUL_MAX;
   nir_foreach_block (block, nir_shader_get_entrypoint(src)) {
      nir_foreach_instr (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);
         if (intrinsic->intrinsic == nir_intrinsic_load_constant) {
            nir_intrinsic_set_base(intrinsic, base_offset + nir_intrinsic_base(intrinsic));

            uint32_t align_mul = nir_intrinsic_align_mul(intrinsic);
            uint32_t align_offset = nir_intrinsic_align_offset(intrinsic);
            align_mul = MIN2(align_mul, base_align_mul);
            nir_intrinsic_set_align(intrinsic, align_mul, align_offset % align_mul);
         }
      }
   }
}

struct inlined_shader_case {
   struct radv_ray_tracing_group *group;
   uint32_t call_idx;
};

static int
compare_inlined_shader_case(const void *a, const void *b)
{
   const struct inlined_shader_case *visit_a = a;
   const struct inlined_shader_case *visit_b = b;
   return visit_a->call_idx > visit_b->call_idx ? 1 : visit_a->call_idx < visit_b->call_idx ? -1 : 0;
}

static void
insert_inlined_range(nir_builder *b, nir_def *sbt_idx, radv_insert_shader_case shader_case,
                     struct radv_rt_case_data *data, struct inlined_shader_case *cases, uint32_t length)
{
   if (length >= INLINED_SHADER_BSEARCH_THRESHOLD) {
      nir_push_if(b, nir_ige_imm(b, sbt_idx, cases[length / 2].call_idx));
      {
         insert_inlined_range(b, sbt_idx, shader_case, data, cases + (length / 2), length - (length / 2));
      }
      nir_push_else(b, NULL);
      {
         insert_inlined_range(b, sbt_idx, shader_case, data, cases, length / 2);
      }
      nir_pop_if(b, NULL);
   } else {
      for (uint32_t i = 0; i < length; ++i)
         shader_case(b, sbt_idx, cases[i].group, data);
   }
}

void
radv_visit_inlined_shaders(nir_builder *b, nir_def *sbt_idx, bool can_have_null_shaders, struct radv_rt_case_data *data,
                           radv_get_group_info group_info, radv_insert_shader_case shader_case)
{
   struct inlined_shader_case *cases = calloc(data->pipeline->group_count, sizeof(struct inlined_shader_case));
   uint32_t case_count = 0;

   for (unsigned i = 0; i < data->pipeline->group_count; i++) {
      struct radv_ray_tracing_group *group = &data->pipeline->groups[i];

      uint32_t shader_index = VK_SHADER_UNUSED_KHR;
      uint32_t handle_index = VK_SHADER_UNUSED_KHR;
      group_info(group, &shader_index, &handle_index, data);
      if (shader_index == VK_SHADER_UNUSED_KHR)
         continue;

      /* Avoid emitting stages with the same shaders/handles multiple times. */
      bool duplicate = false;
      for (unsigned j = 0; j < i; j++) {
         uint32_t other_shader_index = VK_SHADER_UNUSED_KHR;
         uint32_t other_handle_index = VK_SHADER_UNUSED_KHR;
         group_info(&data->pipeline->groups[j], &other_shader_index, &other_handle_index, data);

         if (handle_index == other_handle_index) {
            duplicate = true;
            break;
         }
      }

      if (!duplicate) {
         cases[case_count++] = (struct inlined_shader_case){
            .group = group,
            .call_idx = handle_index,
         };
      }
   }

   qsort(cases, case_count, sizeof(struct inlined_shader_case), compare_inlined_shader_case);

   /* Do not emit 'if (sbt_idx != 0) { ... }' is there are only a few cases. */
   can_have_null_shaders &= case_count >= RADV_RT_SWITCH_NULL_CHECK_THRESHOLD;

   if (can_have_null_shaders)
      nir_push_if(b, nir_ine_imm(b, sbt_idx, 0));

   insert_inlined_range(b, sbt_idx, shader_case, data, cases, case_count);

   if (can_have_null_shaders)
      nir_pop_if(b, NULL);

   free(cases);
}

/* Lowers RT I/O vars to registers or shared memory. If hit_attribs is NULL, attributes are
 * lowered to shared memory. */
bool
radv_nir_lower_rt_storage(nir_shader *shader, nir_deref_instr **hit_attribs, nir_deref_instr **payload_in,
                          nir_variable **payload_out, uint32_t workgroup_size)
{
   bool progress = false;
   nir_function_impl *impl = radv_get_rt_shader_entrypoint(shader);

   nir_foreach_variable_with_modes (attrib, shader, nir_var_ray_hit_attrib) {
      attrib->data.mode = nir_var_shader_temp;
      progress = true;
   }

   nir_builder b = nir_builder_create(impl);

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_load_hit_attrib_amd &&
             intrin->intrinsic != nir_intrinsic_store_hit_attrib_amd &&
             intrin->intrinsic != nir_intrinsic_load_incoming_ray_payload_amd &&
             intrin->intrinsic != nir_intrinsic_store_incoming_ray_payload_amd &&
             intrin->intrinsic != nir_intrinsic_load_outgoing_ray_payload_amd &&
             intrin->intrinsic != nir_intrinsic_store_outgoing_ray_payload_amd)
            continue;

         progress = true;
         b.cursor = nir_after_instr(instr);

         if (intrin->intrinsic == nir_intrinsic_load_hit_attrib_amd ||
             intrin->intrinsic == nir_intrinsic_store_hit_attrib_amd) {
            nir_def *offset;
            if (!hit_attribs)
               offset = nir_imul_imm(
                  &b, nir_iadd_imm(&b, nir_load_subgroup_invocation(&b), nir_intrinsic_base(intrin) * workgroup_size),
                  sizeof(uint32_t));

            if (intrin->intrinsic == nir_intrinsic_load_hit_attrib_amd) {
               nir_def *ret;
               if (hit_attribs)
                  ret = nir_load_deref(&b, hit_attribs[nir_intrinsic_base(intrin)]);
               else
                  ret = nir_load_shared(&b, 1, 32, offset, .base = 0, .align_mul = 4);
               nir_def_rewrite_uses(nir_instr_def(instr), ret);
            } else {
               if (hit_attribs)
                  nir_store_deref(&b, hit_attribs[nir_intrinsic_base(intrin)], intrin->src->ssa, 0x1);
               else
                  nir_store_shared(&b, intrin->src->ssa, offset, .base = 0, .align_mul = 4);
            }
         } else if (intrin->intrinsic == nir_intrinsic_load_incoming_ray_payload_amd ||
                    intrin->intrinsic == nir_intrinsic_store_incoming_ray_payload_amd) {
            if (!payload_in)
               continue;
            if (intrin->intrinsic == nir_intrinsic_load_incoming_ray_payload_amd)
               nir_def_rewrite_uses(nir_instr_def(instr), nir_load_deref(&b, payload_in[nir_intrinsic_base(intrin)]));
            else
               nir_store_deref(&b, payload_in[nir_intrinsic_base(intrin)], intrin->src->ssa, 0x1);
         } else {
            if (!payload_out)
               continue;
            if (intrin->intrinsic == nir_intrinsic_load_outgoing_ray_payload_amd)
               nir_def_rewrite_uses(nir_instr_def(instr), nir_load_var(&b, payload_out[nir_intrinsic_base(intrin)]));
            else
               nir_store_var(&b, payload_out[nir_intrinsic_base(intrin)], intrin->src->ssa, 0x1);
         }
         nir_instr_remove(instr);
      }
   }

   if (!hit_attribs)
      shader->info.shared_size = MAX2(shader->info.shared_size, workgroup_size * RADV_MAX_HIT_ATTRIB_SIZE);

   return nir_progress(progress, impl, nir_metadata_control_flow);
}

void
radv_nir_param_from_type(nir_parameter *param, const glsl_type *type, bool uniform, unsigned driver_attribs)
{
   param->num_components = glsl_get_vector_elements(type);
   param->bit_size = glsl_get_bit_size(type);
   param->type = type;
   param->is_uniform = uniform;
   param->driver_attributes = driver_attribs;
}

void
radv_nir_return_param_from_type(nir_parameter *param, const glsl_type *type, bool uniform, unsigned driver_attribs)
{
   param->num_components = 1;
   param->bit_size = 32;
   param->type = type;
   param->is_uniform = uniform;
   param->driver_attributes = driver_attribs;
   param->is_return = true;
}

void
radv_build_rt_prolog(struct radv_device *device, struct radv_shader_stage *stage, bool uses_descriptor_heap,
                     struct radv_shader_debug_info *debug)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   nir_builder b = radv_meta_nir_init_shader(MESA_SHADER_COMPUTE, "rt_prolog");
   stage->stage = MESA_SHADER_COMPUTE;
   stage->nir = b.shader;
   stage->info.stage = MESA_SHADER_COMPUTE;
   stage->info.loads_push_constants = true;
   stage->info.loads_dynamic_offsets = true;
   stage->info.force_indirect_descriptors = true;
   stage->info.descriptor_heap = uses_descriptor_heap;
   stage->info.wave_size = pdev->rt_wave_size;
   stage->info.workgroup_size = stage->info.wave_size;
   stage->info.user_data_0 = R_00B900_COMPUTE_USER_DATA_0;
   stage->info.type = RADV_SHADER_TYPE_RT_PROLOG;
   stage->info.cs.block_size[0] = pdev->rt_wave_size;
   stage->info.cs.block_size[1] = 1;
   stage->info.cs.block_size[2] = 1;
   stage->info.cs.uses_thread_id[0] = true;
   for (unsigned i = 0; i < 3; i++)
      stage->info.cs.uses_block_id[i] = true;

   radv_declare_shader_args(device, NULL, &stage->info, MESA_SHADER_COMPUTE, MESA_SHADER_NONE, &stage->args, debug);
   stage->info.user_sgprs_locs = stage->args.user_sgprs_locs;

   b.shader->info.workgroup_size[0] = pdev->rt_wave_size;
   b.shader->info.api_subgroup_size = pdev->rt_wave_size;
   b.shader->info.max_subgroup_size = pdev->rt_wave_size;
   b.shader->info.min_subgroup_size = pdev->rt_wave_size;

   nir_function *raygen_function = nir_function_create(b.shader, "raygen_func");
   radv_nir_init_rt_function_params(raygen_function, MESA_SHADER_RAYGEN, 0, 0, uses_descriptor_heap);

   nir_def *descriptors, *dynamic_descriptors, *heap_resource, *heap_sampler;
   if (uses_descriptor_heap) {
      heap_resource = ac_nir_load_arg(&b, &stage->args.ac, stage->args.descriptors[RADV_HEAP_RESOURCE]);
      heap_sampler = ac_nir_load_arg(&b, &stage->args.ac, stage->args.descriptors[RADV_HEAP_SAMPLER]);
   } else {
      descriptors = ac_nir_load_arg(&b, &stage->args.ac, stage->args.descriptors[0]);
      dynamic_descriptors = ac_nir_load_arg(&b, &stage->args.ac, stage->args.ac.dynamic_descriptors);
   }

   nir_def *push_constants = ac_nir_load_arg(&b, &stage->args.ac, stage->args.ac.push_constants);
   nir_def *sbt_desc = nir_pack_64_2x32(&b, ac_nir_load_arg(&b, &stage->args.ac, stage->args.ac.rt.sbt_descriptors));
   nir_def *launch_size_addr = nir_pack_64_2x32(&b, ac_nir_load_arg(&b, &stage->args.ac, stage->args.ac.rt.launch_size_addr));
   nir_def *traversal_addr =
      nir_pack_64_2x32_split(&b, ac_nir_load_arg(&b, &stage->args.ac, stage->args.ac.rt.traversal_shader_addr),
                             nir_imm_int(&b, pdev->info.address32_hi));

   nir_def *raygen_sbt = nir_pack_64_2x32(&b, ac_nir_load_smem(&b, 2, sbt_desc, nir_imm_int(&b, 0), 4, 0));
   nir_def *launch_sizes = ac_nir_load_smem(&b, 3, launch_size_addr, nir_imm_int(&b, 0), 4, 0);

   nir_def *wg_id_vec = nir_load_workgroup_id(&b);
   nir_def *wg_ids[3] = {
      nir_channel(&b, wg_id_vec, 0),
      nir_channel(&b, wg_id_vec, 1),
      nir_channel(&b, wg_id_vec, 2),
   };

   nir_def *local_id = nir_channel(&b, nir_load_local_invocation_id(&b), 0);

   nir_def *unswizzled_id_x = nir_iadd(&b, nir_imul_imm(&b, wg_ids[0], pdev->rt_wave_size), local_id);
   nir_def *unswizzled_id_y = wg_ids[1];

   /* Swizzle ray launch IDs. We dispatch a 1D 32x1/64x1 workgroup natively. Many games dispatch
    * rays in a 2D grid and write RT results to an image indexed by the x/y launch ID.
    * In image space, a 1D workgroup maps to a 32/64-pixel wide line, which is inefficient for two
    * reasons:
    * - Image data is usually arranged on a Z-order curve, a long line makes for inefficient
    *   memory access patterns.
    * - Each wave working on a "line" in image space may increase divergence. It's better to trace
    *   rays in a small square, since that makes it more likely all rays hit the same or similar
    *   objects.
    *
    * It turns out arranging rays along a Z-order curve is best for both image access patterns and
    * ray divergence. Since image data is swizzled along a Z-order curve as well, swizzling the
    * launch ID should result in each lane accessing whole cachelines at once. For traced rays,
    * the Z-order curve means that each quad is arranged in a 2x2 square in image space as well.
    * Since the RT unit processes 4 lanes at a time, reducing divergence per quad may result in
    * better RT unit utilization (for example by the RT unit being able to skip the quad entirely
    * if all 4 lanes are inactive).
    *
    * To swizzle along a Z-order curve, treat the 1D lane ID as a morton code. Then, do the inverse
    * of morton code generation (i.e. deinterleaving the bits) to recover the x-y
    * coordinates on the Z-order curve.
    */

   /* Deinterleave bits - even bits go to swizzled_id_x, odd ones to swizzled_id_y */
   nir_def *swizzled_id_x = local_id;
   nir_def *swizzled_id_y = nir_ushr_imm(&b, local_id, 1);

   /* The deinterleaved bits are currently separated by single bit, like so:
    * ...0 0 0 A ? B ? C
    * Compact the deinterleaved bits by factor 2 to remove the padding, resulting in
    * ...0 0 0 0 0 A B C
    */
   nir_def *swizzled_id_shifted_x = nir_ushr_imm(&b, swizzled_id_x, 1);
   nir_def *swizzled_id_shifted_y = nir_ushr_imm(&b, swizzled_id_y, 1);
   swizzled_id_x = nir_bitfield_select(&b, nir_imm_int(&b, 0x11), swizzled_id_x, swizzled_id_shifted_x);
   swizzled_id_y = nir_bitfield_select(&b, nir_imm_int(&b, 0x11), swizzled_id_y, swizzled_id_shifted_y);

   swizzled_id_shifted_x = nir_ushr_imm(&b, swizzled_id_x, 2);
   swizzled_id_shifted_y = nir_ushr_imm(&b, swizzled_id_y, 2);
   swizzled_id_x = nir_bitfield_select(&b, nir_imm_int(&b, 0x3), swizzled_id_x, swizzled_id_shifted_x);
   swizzled_id_y = nir_bitfield_select(&b, nir_imm_int(&b, 0x3), swizzled_id_y, swizzled_id_shifted_y);

   uint32_t workgroup_width = 8;
   uint32_t workgroup_height = pdev->rt_wave_size == 32 ? 4 : 8;
   uint32_t workgroup_height_mask = workgroup_height - 1;

   /* Fix up the workgroup IDs after converting from 32x1/64x1 to 8x4/8x8. The X dimension of the
    * workgroup size gets divided by 4/8, while the Y dimension gets multiplied by the same amount.
    * Rearrange the workgroups to make up for that, by rounding the Y component of the workgroup ID
    * to the nearest multiple of 4/8. The remainder gets added to the X dimension, to make up for
    * the fact we divided the X component of the ID.
    */
   nir_def *wg_id_y_rem = nir_iand_imm(&b, wg_ids[1], workgroup_height_mask);
   nir_def *new_wg_start_x = nir_imul_imm(&b, wg_ids[0], pdev->rt_wave_size);
   new_wg_start_x = nir_iadd(&b, new_wg_start_x, nir_imul_imm(&b, wg_id_y_rem, workgroup_width));

   nir_def *new_wg_start_y = nir_iand_imm(&b, wg_ids[1], ~workgroup_height_mask);

   swizzled_id_x = nir_iadd(&b, swizzled_id_x, new_wg_start_x);
   swizzled_id_y = nir_iadd(&b, swizzled_id_y, new_wg_start_y);

   /* Round the launch size down to the nearest multiple of workgroup_height. If the workgroup ID
    * exceeds this, then the swizzled IDs' Y component will exceed the Y launch size and we have to
    * fall back to unswizzled IDs.
    */
   nir_def *y_wg_bound = nir_iand_imm(&b, nir_channel(&b, launch_sizes, 1), ~workgroup_height_mask);

   /* If parts of this wave would've exceeded the launch size in the X dimension, their threads will be masked out and
    * exec won't equal -1. In that case, using swizzled IDs is invalid.
    */
   nir_def *partial_oob_x = nir_ine_imm(&b, nir_ballot(&b, 1, pdev->rt_wave_size, nir_imm_true(&b)), -1);
   nir_def *partial_oob_y = nir_uge(&b, wg_ids[1], y_wg_bound);

   nir_def *partial_oob = nir_ior(&b, partial_oob_x, partial_oob_y);

   nir_def *id_x = nir_bcsel(&b, partial_oob, unswizzled_id_x, swizzled_id_x);
   nir_def *id_y = nir_bcsel(&b, partial_oob, unswizzled_id_y, swizzled_id_y);

   /* shaderGroupBaseAlignment is RADV_RT_HANDLE_SIZE */
   nir_def *raygen_addr = nir_pack_64_2x32(&b, ac_nir_load_smem(&b, 2, raygen_sbt, nir_imm_int(&b, 0), RADV_RT_HANDLE_SIZE, 0));
   nir_def *shader_record_ptr = nir_iadd_imm(&b, raygen_sbt, RADV_RT_HANDLE_SIZE);

   nir_def *params[RAYGEN_ARG_COUNT];
   params[RT_ARG_LAUNCH_ID] = nir_vec3(&b, id_x, id_y, wg_ids[2]);
   params[RT_ARG_LAUNCH_SIZE] = launch_sizes;
   if (uses_descriptor_heap) {
      params[RT_ARG_HEAP_RESOURCE] = heap_resource;
      params[RT_ARG_HEAP_SAMPLER] = heap_sampler;
   } else {
      params[RT_ARG_DESCRIPTORS] = descriptors;
      params[RT_ARG_DYNAMIC_DESCRIPTORS] = dynamic_descriptors;
   }
   params[RT_ARG_PUSH_CONSTANTS] = push_constants;
   params[RT_ARG_SBT_DESCRIPTORS] = sbt_desc;
   params[RAYGEN_ARG_SHADER_RECORD_PTR] = shader_record_ptr;
   params[RAYGEN_ARG_TRAVERSAL_ADDR] = traversal_addr;

   nir_build_indirect_call(&b, raygen_function, raygen_addr, RAYGEN_ARG_COUNT, params);
}
