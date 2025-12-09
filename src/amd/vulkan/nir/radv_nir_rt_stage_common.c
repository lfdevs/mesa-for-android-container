/*
 * Copyright © 2025 Valve Corporation
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_nir_rt_stage_common.h"
#include "nir_builder.h"

struct radv_nir_sbt_data
radv_nir_load_sbt_entry(nir_builder *b, nir_def *idx, enum radv_nir_sbt_type binding, enum radv_nir_sbt_entry offset)
{
   struct radv_nir_sbt_data data;

   nir_def *desc_base_addr = nir_load_sbt_base_amd(b);

   nir_def *desc = nir_pack_64_2x32(b, ac_nir_load_smem(b, 2, desc_base_addr, nir_imm_int(b, binding), 4, 0));

   nir_def *stride_offset = nir_imm_int(b, binding + (binding == SBT_RAYGEN ? 8 : 16));
   nir_def *stride = ac_nir_load_smem(b, 1, desc_base_addr, stride_offset, 4, 0);

   nir_def *addr = nir_iadd(b, desc, nir_u2u64(b, nir_iadd_imm(b, nir_imul(b, idx, stride), offset)));

   unsigned load_size = offset == SBT_RECURSIVE_PTR ? 64 : 32;
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

bool
radv_nir_lower_rt_derefs(nir_shader *shader)
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

/* Lowers hit attributes to registers or shared memory. If hit_attribs is NULL, attributes are
 * lowered to shared memory. */
bool
radv_nir_lower_hit_attribs(nir_shader *shader, nir_variable **hit_attribs, uint32_t workgroup_size)
{
   bool progress = false;
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

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
             intrin->intrinsic != nir_intrinsic_store_hit_attrib_amd)
            continue;

         progress = true;
         b.cursor = nir_after_instr(instr);

         nir_def *offset;
         if (!hit_attribs)
            offset = nir_imul_imm(
               &b, nir_iadd_imm(&b, nir_load_local_invocation_index(&b), nir_intrinsic_base(intrin) * workgroup_size),
               sizeof(uint32_t));

         if (intrin->intrinsic == nir_intrinsic_load_hit_attrib_amd) {
            nir_def *ret;
            if (hit_attribs)
               ret = nir_load_var(&b, hit_attribs[nir_intrinsic_base(intrin)]);
            else
               ret = nir_load_shared(&b, 1, 32, offset, .base = 0, .align_mul = 4);
            nir_def_rewrite_uses(nir_instr_def(instr), ret);
         } else {
            if (hit_attribs)
               nir_store_var(&b, hit_attribs[nir_intrinsic_base(intrin)], intrin->src->ssa, 0x1);
            else
               nir_store_shared(&b, intrin->src->ssa, offset, .base = 0, .align_mul = 4);
         }
         nir_instr_remove(instr);
      }
   }

   if (!hit_attribs)
      shader->info.shared_size = MAX2(shader->info.shared_size, workgroup_size * RADV_MAX_HIT_ATTRIB_SIZE);

   return nir_progress(progress, impl, nir_metadata_control_flow);
}
