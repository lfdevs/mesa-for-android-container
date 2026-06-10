/*
 * Copyright © 2026 Valve Corporation.
 * SPDX-License-Identifier: MIT
 */
#include "util/macros.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_defines.h"
#include "nvk_shader.h"
#include "shader_enums.h"

static bool
add_task_payload_base_offset(nir_builder *b, nir_intrinsic_instr *intrin,
                             void *data)
{
   const uint32_t *offset = data;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_task_payload:
   case nir_intrinsic_store_task_payload:
   case nir_intrinsic_task_payload_atomic:
   case nir_intrinsic_task_payload_atomic_swap:
      break;
   default:
      return false;
   }

   unsigned base = nir_intrinsic_base(intrin);
   nir_intrinsic_set_base(intrin, base + *offset);
   return true;
}

static bool
nvk_nir_lower_common_task_payload(nir_shader *nir)
{
   /* The first 0x20 bytes are used by launch_mesh_workgroups */
   uint32_t task_payload_reserved_size = 0x20;

   /* Take into account the reserved chunk in task memory */
   nir->info.task_payload_size += task_payload_reserved_size;

   /* Add the reserved chunk to every task payload accesses */
   return nir_shader_intrinsics_pass(nir, add_task_payload_base_offset,
                                     nir_metadata_all,
                                     &task_payload_reserved_size);
}

static bool
lower_set_vertex_and_primitive_count_intrin(nir_builder *b,
                                            nir_intrinsic_instr *intrin,
                                            UNUSED void *data)
{
   if (intrin->intrinsic != nir_intrinsic_set_vertex_and_primitive_count)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *local_invocation_index = nir_load_local_invocation_index(b);
   nir_push_if(b, nir_ieq(b, local_invocation_index, nir_imm_int(b, 0)));
   {
      nir_set_vertex_and_primitive_count(
         b, intrin->src[0].ssa, intrin->src[1].ssa, intrin->src[2].ssa);
   }
   nir_pop_if(b, NULL);

   nir_instr_remove(&intrin->instr);

   return true;
}

bool
nvk_nir_lower_mesh_shader(nir_shader *nir, VkShaderCreateFlagsEXT shader_flags)
{
   if (nir->info.stage != MESA_SHADER_MESH)
      return false;

   bool progress = false;

   if ((shader_flags & VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT) == 0)
      progress |= nvk_nir_lower_common_task_payload(nir);

   progress |= nir_shader_intrinsics_pass(
      nir, lower_set_vertex_and_primitive_count_intrin, nir_metadata_none,
      NULL);

   return progress;
}

static bool
launch_mesh_workgroups_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                              UNUSED void *data)
{
   if (intrin->intrinsic != nir_intrinsic_launch_mesh_workgroups)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *local_invocation_index = nir_load_local_invocation_index(b);
   nir_push_if(b, nir_ieq(b, local_invocation_index, nir_imm_int(b, 0)));
   {
      nir_launch_mesh_workgroups(b, intrin->src[0].ssa);
   }
   nir_pop_if(b, NULL);
   nir_instr_remove(&intrin->instr);

   return true;
}

static nir_intrinsic_op
task_payload_intrinsic_to_shared(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_load_task_payload:
      return nir_intrinsic_load_shared;
   case nir_intrinsic_store_task_payload:
      return nir_intrinsic_store_shared;
   case nir_intrinsic_task_payload_atomic:
      return nir_intrinsic_shared_atomic;
   case nir_intrinsic_task_payload_atomic_swap:
      return nir_intrinsic_shared_atomic_swap;
   default:
      return nir_num_intrinsics;
   }
}

static bool
lower_task_payload_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                          UNUSED void *data)
{
   nir_intrinsic_op new_op =
      task_payload_intrinsic_to_shared(intrin->intrinsic);
   if (new_op == nir_num_intrinsics)
      return false;

   intrin->intrinsic = new_op;
   return true;
}

static bool
add_shared_base_offset(nir_builder *b, nir_intrinsic_instr *intrin,
                       UNUSED void *data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      break;
   default:
      return false;
   }

   const uint32_t shared_memory_base = b->shader->info.task_payload_size;
   assert(shared_memory_base % 0x80 == 0);

   unsigned base = nir_intrinsic_base(intrin);
   nir_intrinsic_set_base(intrin, base + shared_memory_base);
   return true;
}

bool
nvk_nir_lower_task_shader(nir_shader *nir)
{
   if (nir->info.stage != MESA_SHADER_TASK)
      return false;

   bool progress = false;

   /* Apply common lowering for task payload */
   progress |= nvk_nir_lower_common_task_payload(nir);

   /* Ensure alignment based on ISBE mem lines size (128 bytes) */
   nir->info.task_payload_size = align(nir->info.task_payload_size, 128);

   /* Readjust shared memory size to include the task payload */
   nir->info.shared_size += nir->info.task_payload_size;

   /* Now move all shared memory after task payload range and lower task payload
    * to shared memory */
   progress |= nir_shader_intrinsics_pass(nir, add_shared_base_offset,
                                          nir_metadata_all, NULL);
   progress |= nir_shader_intrinsics_pass(nir, lower_task_payload_intrin,
                                          nir_metadata_all, NULL);

   /* Finally we ensure that launch_mesh_workgroups is only running on lane 0 */
   progress |= nir_shader_intrinsics_pass(nir, launch_mesh_workgroups_intrin,
                                          nir_metadata_none, NULL);

   return progress;
}
