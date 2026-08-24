/*
 * Copyright © 2026 Valve Corporation.
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"

static bool
lower_mesh_io_intrin(nir_builder *b,
                    nir_intrinsic_instr *intrin,
                    const struct lower_mesh_intrinsics_ctx *ctx)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *vtx = NULL, *offset = NULL, *data = NULL;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_primitive_output:
      vtx = intrin->src[0].ssa;
      offset = intrin->src[1].ssa;
      break;

   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_primitive_output:
      data = intrin->src[0].ssa;
      vtx = intrin->src[1].ssa;
      offset = intrin->src[2].ssa;
      break;

   default:
      UNREACHABLE("unknown intrinsic");
   }

   const bool is_per_primitive = intrin->intrinsic == nir_intrinsic_load_per_primitive_output ||
                                 intrin->intrinsic == nir_intrinsic_store_per_primitive_output;

   const bool is_store = data != NULL;
   nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);

   const bool is_primitive_indices = sem.location == VARYING_SLOT_PRIMITIVE_INDICES;

   const struct nak_nir_isbe_flags flags = {
      .access = is_primitive_indices ? NAK_ISBE_ACCESS_MAP : NAK_ISBE_ACCESS_ATTR,
      .output = true,
      .skew = !is_primitive_indices,
      .per_primitive = is_per_primitive,
   };

   uint32_t base_addr =
      nak_varying_mesh_skew_attr_addr(ctx->nak, sem.location) +
      4 * nir_intrinsic_component(intrin);

   uint32_t range;
   if (nir_src_is_const(nir_src_for_ssa(offset))) {
      uint32_t const_offset = nir_src_as_uint(nir_src_for_ssa(offset));
      /* Tighten the range */
      base_addr += const_offset * 16;
      range = 4 * intrin->num_components;

      if (const_offset != 0)
         offset = nir_imm_int(b, 0);
   } else {
      /* Offsets from NIR are in vec4's */
      offset = nir_imul_imm(b, offset, 16);
      range = (sem.num_slots - 1) * 16 + intrin->num_components * 4;
   }

   nir_def *isbe_offset;
   uint32_t stride;
   if (is_primitive_indices) {
      const uint32_t vertices_per_prim = mesa_vertices_per_prim(b->shader->info.mesh.primitive_type);

      /* Indices are 8 bits on hardware */
      isbe_offset = nir_iadd(b, offset, nir_iadd_imm(b, nir_imul_imm(b, vtx, vertices_per_prim), 4));
      stride = 1;
   } else {
      uint16_t skew_attr_offset = nak_mesh_skew_offset(ctx, sem.location, base_addr, is_per_primitive);
      nir_def *skew_start_offset;
      uint16_t skew_group_size;

      if (is_per_primitive) {
         skew_start_offset = nir_imm_int(b, nak_mesh_skew_vert_total_size(ctx));
         skew_group_size = nak_mesh_skew_prim_group_size(ctx);
      } else {
         skew_start_offset = nir_imm_int(b, 0);
         skew_group_size = nak_mesh_skew_vert_group_size(ctx);
      }

      /* Readjust offset to take into account SKEW groups */
      nir_def *offset_ajusted = nir_imul_imm(b, offset, NAK_MESH_SKEW_GROUP_COUNT);
      skew_start_offset = nir_iadd(b, skew_start_offset, nir_imul_imm(b, nir_udiv_imm(b, vtx, 32), skew_group_size));

      isbe_offset = nir_iadd(b, nir_iadd_imm(b, nir_iadd(b, nir_imul_imm(b, nir_imod_imm(b, vtx, 32), 4),
                                                         skew_start_offset),
                                             skew_attr_offset),
                             offset_ajusted);
      stride = 4 * NAK_MESH_SKEW_GROUP_COUNT;
   }

   if (is_store) {
      u_foreach_bit(c, nir_intrinsic_write_mask(intrin)) {
         nir_def *c_offset = nir_iadd_imm(b, isbe_offset, c * stride);
         nir_def *c_data = nir_channel(b, data, c);

         /* Handle indices conversion */
         if (is_primitive_indices)
            c_data = nir_u2u8(b, c_data);

         nir_isbewr_nv(b, c_data, c_offset, .range_base = base_addr,
                       .range = range, .flags = NAK_AS_U32(flags));
      }
   } else {
      const uint8_t bit_size = is_primitive_indices ? 8 : intrin->def.bit_size;

      nir_def *comps[NIR_MAX_VEC_COMPONENTS];
      for (uint32_t c = 0; c < intrin->num_components; c++) {
         nir_def *c_offset = nir_iadd_imm(b, isbe_offset, c * stride);
         nir_def *c_data =
            nir_isberd_nv(b, bit_size, c_offset, .range_base = base_addr,
                          .range = range, .flags = NAK_AS_U32(flags));

         /* Handle indices conversion */
         if (is_primitive_indices)
            c_data = nir_u2u32(b, c_data);

         comps[c] = c_data;
      }

      nir_def *dst = nir_vec(b, comps, intrin->num_components);
      nir_def_rewrite_uses(&intrin->def, dst);
   }

   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_set_vertex_and_primitive_count(nir_builder *b,
                                     nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *primitive_count = intrin->src[1].ssa;
   nir_def *offset = nir_imm_int(b, 0x3);

   const struct nak_nir_isbe_flags flags = {
      .access = NAK_ISBE_ACCESS_MAP,
      .output = true,
      .skew = false,
      .per_primitive = false,
   };

   nir_isbewr_nv(b, primitive_count, offset,
                 .flags = NAK_AS_U32(flags));

   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_load_workgroup_index(nir_builder *b,
                           nir_intrinsic_instr *intrin,
                           bool from_skew)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(b->shader);

   /* We need to make sure that this is read before any writes to allow ISBE
    * space sharing optimisation to happen */
   b->cursor = nir_before_impl(impl);

   const struct nak_nir_isbe_flags flags = {
      .access = NAK_ISBE_ACCESS_ATTR,
      .output = false,
      .skew = from_skew,
      .per_primitive = false,
   };

   nir_def *dst =  nir_isberd_nv(b, 32, nir_imm_int(b, 0),
                                 .range_base = NAK_ATTR_VERTEX_ID,
                                 .range = 4,
                                 .flags = NAK_AS_U32(flags));

   nir_def_rewrite_uses(&intrin->def, dst);
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_load_num_workgroups(nir_builder *b, nir_intrinsic_instr *intrin)
{
   /* If we are here, we have a task shader */
   b->cursor = nir_before_instr(&intrin->instr);

   const struct nak_nir_isbe_flags flags = {
      .access = NAK_ISBE_ACCESS_ATTR,
      .output = false,
      .skew = false,
      .per_primitive = false,
   };

   nir_def *x =
      nir_isberd_nv(b, 32, nir_imm_int(b, 0x8), .flags = NAK_AS_U32(flags),
                    .access = ACCESS_CAN_REORDER);
   nir_def *y =
      nir_isberd_nv(b, 32, nir_imm_int(b, 0xC), .flags = NAK_AS_U32(flags),
                    .access = ACCESS_CAN_REORDER);
   nir_def *z =
      nir_isberd_nv(b, 32, nir_imm_int(b, 0x10), .flags = NAK_AS_U32(flags),
                    .access = ACCESS_CAN_REORDER);
   nir_def *dst = nir_vec3(b, x, y, z);
   nir_def_rewrite_uses(&intrin->def, dst);
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_load_shared(nir_builder *b, nir_intrinsic_instr *intrin,
                  uint32_t base_offset)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *offset = intrin->src[0].ssa;

   const uint8_t bit_size = intrin->def.bit_size;
   assert(bit_size == 32 && intrin->def.num_components == 1);

   const uint32_t base = nir_intrinsic_base(intrin);
   const struct nak_nir_isbe_flags flags = {
      .access = NAK_ISBE_ACCESS_ATTR,
      .output = true,
      .skew = false,
      .per_primitive = false,
   };

   offset = nir_iadd_imm(b, offset, base_offset + base);
   nir_def *dst = nir_isberd_nv(b, 32, offset, .flags = NAK_AS_U32(flags));
   nir_def_rewrite_uses(&intrin->def, dst);
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_store_shared(nir_builder *b, nir_intrinsic_instr *intrin,
                   uint32_t base_offset)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *value = intrin->src[0].ssa;
   nir_def *offset = intrin->src[1].ssa;

   const uint8_t bit_size = value->bit_size;
   assert(bit_size == 32 &&
          nir_intrinsic_write_mask(intrin) == nir_component_mask(1));

   const uint32_t base = nir_intrinsic_base(intrin);
   const struct nak_nir_isbe_flags flags = {
      .access = NAK_ISBE_ACCESS_ATTR,
      .output = true,
      .skew = false,
      .per_primitive = false,
   };

   offset = nir_iadd_imm(b, offset, base_offset + base);
   nir_isbewr_nv(b, value, offset, .flags = NAK_AS_U32(flags));
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_load_task_payload(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *offset = intrin->src[0].ssa;

   const uint8_t bit_size = intrin->def.bit_size;
   assert(bit_size == 32 && intrin->def.num_components == 1);

   const uint32_t base = nir_intrinsic_base(intrin);
   const struct nak_nir_isbe_flags flags = {
      .access = NAK_ISBE_ACCESS_ATTR,
      .output = false,
      .skew = false,
      .per_primitive = false,
   };

   offset = nir_iadd_imm(b, offset, base);
   nir_def *dst = nir_isberd_nv(b, 32, offset, .flags = NAK_AS_U32(flags),
                                .access = ACCESS_CAN_REORDER);
   nir_def_rewrite_uses(&intrin->def, dst);
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_mesh_intrin(nir_builder *b,
                  nir_intrinsic_instr *intrin,
                  void *cb_data)
{
   const struct lower_mesh_intrinsics_ctx *ctx = cb_data;

   /* Shared memory is after attributes on mesh shaders */
   const uint32_t shared_memory_base = nak_mesh_skew_total_size(ctx);
   assert(shared_memory_base % 0x80 == 0);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_primitive_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_primitive_output:
      return lower_mesh_io_intrin(b, intrin, ctx);
   case nir_intrinsic_set_vertex_and_primitive_count:
      return lower_set_vertex_and_primitive_count(b, intrin);
   case nir_intrinsic_load_workgroup_index:
      return lower_load_workgroup_index(b, intrin, !ctx->has_task_shader);
   case nir_intrinsic_load_num_workgroups:
      return lower_load_num_workgroups(b, intrin);
   case nir_intrinsic_load_shared:
      return lower_load_shared(b, intrin, shared_memory_base);
   case nir_intrinsic_store_shared:
      return lower_store_shared(b, intrin, shared_memory_base);
   case nir_intrinsic_load_task_payload:
      return lower_load_task_payload(b, intrin);
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      UNREACHABLE(
         "Should have been lowered by nak_nir_lower_mesh_stages_shared_atomics");
   default:
      return false;
   }
}

struct lower_emulated_attributes_state {
   uint32_t viewport_shared_offset;
   uint32_t cullprimitive_shared_offset;
};

static bool
lower_emulated_attributes_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                                 void *_data)
{
   const struct lower_emulated_attributes_state *state = _data;
   nir_def *vtx = NULL, *offset = NULL;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_per_primitive_output:
      vtx = intrin->src[0].ssa;
      offset = intrin->src[1].ssa;
      break;

   case nir_intrinsic_store_per_primitive_output:
      vtx = intrin->src[1].ssa;
      offset = intrin->src[2].ssa;
      break;

   default:
      return false;
   }

   nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);

   if (sem.location != VARYING_SLOT_VIEWPORT &&
       sem.location != VARYING_SLOT_CULL_PRIMITIVE)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *shared_offset = nir_iadd(b, offset, nir_imul_imm(b, vtx, 4));

   if (sem.location == VARYING_SLOT_CULL_PRIMITIVE)
      shared_offset =
         nir_iadd_imm(b, shared_offset, state->cullprimitive_shared_offset);
   else
      shared_offset =
         nir_iadd_imm(b, shared_offset, state->viewport_shared_offset);

   if (intrin->intrinsic == nir_intrinsic_store_per_primitive_output) {
      nir_def *data = intrin->src[0].ssa;
      switch (sem.location) {
      case VARYING_SLOT_VIEWPORT:
         /* In case of Viewport, the data needs to be translated to a proper
          * mask value to map to ViewportMask */
         data = nir_ishl(b, nir_imm_int(b, 1), data);
         break;
      case VARYING_SLOT_CULL_PRIMITIVE:
         /* In case of CullPrimitive, the data is already a 32-bit value so no
          * translation is needed */
         break;
      default:
         UNREACHABLE("Should never happen");
      }

      nir_store_shared(b, data, shared_offset);
      nir_instr_remove(&intrin->instr);
   } else {
      /* Reading back isn't allowed by VK_EXT_mesh_shader but allowed by
       * VK_NV_mesh_shader. We support readback for completeness and in case we
       * add support for NV specific extension in the future */
      nir_def *data = nir_load_shared(b, 1, 32, shared_offset);
      switch (sem.location) {
      case VARYING_SLOT_VIEWPORT:
         /* In case of Viewport, find the first index that is set. */
         data = nir_find_lsb(b, data);
         break;
      case VARYING_SLOT_CULL_PRIMITIVE:
         /* In case of CullPrimitive, we check if no bits are set */
         data = nir_ine_imm(b, data, 0);
         break;
      default:
         UNREACHABLE("Should never happen");
      }
      nir_def_replace(&intrin->def, data);
   }

   return true;
}

bool
nak_nir_lower_mesh_emulated_attributes(nir_shader *nir)
{
   if (nir->info.stage != MESA_SHADER_MESH)
      return false;

   /* Only apply this pass when really needed */
   if ((nir->info.per_primitive_outputs &
        (VARYING_BIT_CULL_PRIMITIVE | VARYING_BIT_VIEWPORT)) == 0)
      return false;

   /* If we are here, we need to emulate Viewport / CullPrimitive with the
    * ViewportMask. This means if we need to always keep a shadow copy of the
    * ViewportMask and CullPrimitive in shared memory and write the actual
    * ViewportMask at the end of the shader. */
   bool progress = false;

   /* Reserve space for the Viewport and CullPrimitive shadow copies */
   uint32_t shared_memory_offset = nir->info.shared_size;
   nir->info.shared_size += 8 * nir->info.mesh.max_primitives_out;

   struct lower_emulated_attributes_state state = {
      .viewport_shared_offset = shared_memory_offset,
      .cullprimitive_shared_offset =
         shared_memory_offset + 4 * nir->info.mesh.max_primitives_out,
   };

   /* First we lower things to shared memory */
   progress |= nir_shader_intrinsics_pass(nir, lower_emulated_attributes_intrin,
                                          nir_metadata_control_flow, &state);

   /* Finally, we ensure that the shared region is init at the start of the
    * shader and we add primitive writes at the end of the shader to write the
    * real value depending on the culling state.*/
   if (progress) {
      nir_function_impl *impl = nir_shader_get_entrypoint(nir);
      nir_builder b = nir_builder_at(nir_before_impl(impl));
      nir_def *zero = nir_imm_int(&b, 0);
      nir_def *viewport_default = nir_imm_int(&b, 1 << 0);

      nir_def *lane_id =
         nak_nir_load_sysval(&b, NAK_SV_LANE_ID, ACCESS_CAN_REORDER);
      nir_push_if(&b, nir_ieq(&b, lane_id, zero));
      {
         for (uint32_t i = 0; i < nir->info.mesh.max_primitives_out; i++) {
            nir_store_shared(
               &b, viewport_default,
               nir_imm_int(&b, state.viewport_shared_offset + i * 4));
            nir_store_shared(
               &b, zero,
               nir_imm_int(&b, state.cullprimitive_shared_offset + i * 4));
         }
      }
      nir_pop_if(&b, NULL);
      nir_barrier(&b, SCOPE_WORKGROUP, SCOPE_WORKGROUP, NIR_MEMORY_ACQ_REL,
                  nir_var_mem_shared);

      b = nir_builder_at(nir_after_impl(impl));
      nir_barrier(&b, SCOPE_WORKGROUP, SCOPE_WORKGROUP, NIR_MEMORY_ACQ_REL,
                  nir_var_mem_shared);
      nir_push_if(&b, nir_ieq(&b, lane_id, zero));
      {
         for (uint32_t i = 0; i < nir->info.mesh.max_primitives_out; i++) {
            nir_def *viewport_mask = nir_load_shared(
               &b, 1, 32,
               nir_imm_int(&b, state.viewport_shared_offset + i * 4));
            nir_def *cull_primitive = nir_load_shared(
               &b, 1, 32,
               nir_imm_int(&b, state.cullprimitive_shared_offset + i * 4));

            viewport_mask = nir_bcsel(&b, nir_ine_imm(&b, cull_primitive, 0),
                                      zero, viewport_mask);
            nir_store_per_primitive_output(
               &b, viewport_mask, nir_imm_int(&b, i), zero, .base = 0,
               .src_type = nir_type_uint32,
               .io_semantics = (nir_io_semantics){
                  .location = VARYING_SLOT_VIEWPORT_MASK,
                  .num_slots = 1,
               });
         }
      }
      nir_pop_if(&b, NULL);
   }

   return progress;
}

bool
nak_nir_lower_mesh_intrinsics(nir_shader *nir,
                              struct lower_mesh_intrinsics_ctx *ctx)
{
   return nir_shader_intrinsics_pass(
      nir, lower_mesh_intrin, nir_metadata_block_index | nir_metadata_dominance,
      ctx);
}

static bool
lower_launch_mesh_workgroups(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *dim = intrin->src[0].ssa;
   nir_def *x = nir_channel(b, dim, 0);
   nir_def *y = nir_channel(b, dim, 1);
   nir_def *z = nir_channel(b, dim, 2);
   nir_def *task_count = nir_imul(b, nir_imul(b, x, y), z);

   const struct nak_nir_isbe_flags flags = {
      .access = NAK_ISBE_ACCESS_ATTR,
      .output = true,
      .skew = false,
      .per_primitive = false,
   };

   nir_isbewr_nv(b, task_count, nir_imm_int(b, 0x4),
                 .flags = NAK_AS_U32(flags));
   nir_isbewr_nv(b, x, nir_imm_int(b, 0x8), .flags = NAK_AS_U32(flags));
   nir_isbewr_nv(b, y, nir_imm_int(b, 0xC), .flags = NAK_AS_U32(flags));
   nir_isbewr_nv(b, z, nir_imm_int(b, 0x10), .flags = NAK_AS_U32(flags));
   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_task_intrin(nir_builder *b,
                  nir_intrinsic_instr *intrin,
                  void *cb_data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_shared:
      return lower_load_shared(b, intrin, 0);
   case nir_intrinsic_store_shared:
      return lower_store_shared(b, intrin, 0);
   case nir_intrinsic_load_workgroup_index:
      return lower_load_workgroup_index(b, intrin, true);
   case nir_intrinsic_launch_mesh_workgroups:
      return lower_launch_mesh_workgroups(b, intrin);
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      UNREACHABLE(
         "Should have been lowered by nak_nir_lower_mesh_stages_shared_atomics");
   case nir_intrinsic_load_task_payload:
   case nir_intrinsic_store_task_payload:
   case nir_intrinsic_task_payload_atomic:
   case nir_intrinsic_task_payload_atomic_swap:
      UNREACHABLE("Should have been lowered by nvk_nir_lower_task_shader");
   default:
      return false;
   }
}

bool
nak_nir_lower_task_intrinsics(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_task_intrin,
                                     nir_metadata_block_index |
                                     nir_metadata_dominance,
                                     NULL);
}
