/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_nir.h"
#include "nir/nir_builder.h"

static bool
lower_load_constant(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   /* Any constant-offset load_constant instructions should have been removed
    * by constant folding.
    */
   assert(!nir_src_is_const(intrin->src[0]));
   nir_def *offset = nir_iadd_imm(b, intrin->src[0].ssa,
                                      nir_intrinsic_base(intrin));

   unsigned load_size = intrin->def.num_components *
                        intrin->def.bit_size / 8;

   assert(load_size < b->shader->constant_data_size);
   unsigned max_offset = b->shader->constant_data_size - load_size;
   offset = nir_umin(b, offset, nir_imm_int(b, max_offset));

   nir_def *const_data_addr = nir_pack_64_2x32_split(b,
      nir_iadd(b,
         nir_load_reloc_const_intel(b, INTEL_SHADER_RELOC_CONST_DATA_ADDR_LOW),
         offset),
      nir_load_reloc_const_intel(b, INTEL_SHADER_RELOC_CONST_DATA_ADDR_HIGH));

   nir_def *data =
      nir_load_global_constant(b, intrin->def.num_components,
                               intrin->def.bit_size,
                               const_data_addr);

   nir_def_replace(&intrin->def, data);

   return true;
}

static bool
lower_base_workgroup_id(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *base_workgroup_id =
      anv_load_driver_uniform(b, 3, drv_data.cs.base_workgroup[0]);
   nir_def_replace(&intrin->def, base_workgroup_id);

   return true;
}

static bool
lower_subgroup_id(nir_builder *b, nir_intrinsic_instr *intrin,
                  const struct anv_physical_device *pdevice)
{
   if (pdevice->info.verx10 >= 125)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def_replace(&intrin->def,
                   anv_load_driver_uniform(b, 1, subgroup_id));

   return true;
}

static bool
lower_ray_query_globals(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *rq_globals = anv_load_driver_uniform(b, 1, drv_data.ray_query_globals);
   nir_def_replace(&intrin->def, rq_globals);

   return true;
}

static bool
lower_driver_values(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_constant:
      return lower_load_constant(b, intrin);
   case nir_intrinsic_load_base_workgroup_id:
      return lower_base_workgroup_id(b, intrin);
   case nir_intrinsic_load_subgroup_id:
      return lower_subgroup_id(b, intrin, data);
   case nir_intrinsic_load_ray_query_global_intel:
      return lower_ray_query_globals(b, intrin);
   default:
      return false;
   }
}

static bool
lower_num_workgroups(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_num_workgroups)
      return false;

   /* For those stages, HW will generate values through payload registers. */
   if (mesa_shader_stage_is_mesh(b->shader->info.stage))
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *num_workgroups =
      anv_load_driver_uniform(b, 3, drv_data.cs.num_workgroups[0]);

   nir_def *num_workgroups_indirect;
   nir_push_if(b, nir_ieq_imm(b, nir_channel(b, num_workgroups, 0), UINT32_MAX));
   {
      nir_def *addr = nir_pack_64_2x32_split(b,
                                             nir_channel(b, num_workgroups, 1),
                                             nir_channel(b, num_workgroups, 2));
      num_workgroups_indirect = nir_load_global_constant(b, 3, 32, addr);
   }
   nir_pop_if(b, NULL);

   num_workgroups = nir_if_phi(b, num_workgroups_indirect, num_workgroups);
   nir_def_replace(&intrin->def, num_workgroups);

   return true;
}

bool
anv_nir_lower_driver_values(nir_shader *shader,
                            const struct anv_physical_device *pdevice)
{
   bool progress = nir_shader_intrinsics_pass(shader,
                                              lower_driver_values,
                                              nir_metadata_control_flow,
                                              (void *)pdevice);
   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_num_workgroups,
                                          nir_metadata_none,
                                          (void *)pdevice);
   return progress;
}
