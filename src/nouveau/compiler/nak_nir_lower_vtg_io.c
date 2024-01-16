/*
 * Copyright © 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"

static bool
lower_vtg_io_intrin(nir_builder *b,
                    nir_intrinsic_instr *intrin,
                    void *cb_data)
{
   nir_def *vtx = NULL, *offset = NULL, *data = NULL;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_output:
      offset = intrin->src[0].ssa;
      break;

   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
      vtx = intrin->src[0].ssa;
      offset = intrin->src[1].ssa;
      break;

   case nir_intrinsic_store_output:
      data = intrin->src[0].ssa;
      offset = intrin->src[1].ssa;
      break;

   case nir_intrinsic_store_per_vertex_output:
      data = intrin->src[0].ssa;
      vtx = intrin->src[1].ssa;
      offset = intrin->src[2].ssa;
      break;

   default:
      return false;
   }

   const bool is_store = data != NULL;

   unsigned base = nir_intrinsic_base(intrin);
   unsigned range = nir_intrinsic_range(intrin);
   unsigned component = nir_intrinsic_component(intrin);

   bool is_output;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_vertex_input:
      is_output = false;
      break;

   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
      is_output = true;
      break;

   default:
      unreachable("Unknown NIR I/O intrinsic");
   }

   bool is_patch;
   switch (b->shader->info.stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_GEOMETRY:
      is_patch = false;
      break;

   case MESA_SHADER_TESS_CTRL:
      is_patch = is_output && vtx == NULL;
      break;

   case MESA_SHADER_TESS_EVAL:
      is_patch = !is_output && vtx == NULL;
      break;

   default:
      unreachable("Unknown shader stage");
   }

   nir_component_mask_t mask;
   if (is_store)
      mask = nir_intrinsic_write_mask(intrin);
   else
      mask = nir_component_mask(intrin->num_components);

   b->cursor = nir_before_instr(&intrin->instr);

   if (vtx != NULL && !is_output) {
      nir_def *info = nir_load_sysval_nv(b, 32,
                                         .base = NAK_SV_INVOCATION_INFO,
                                         .access = ACCESS_CAN_REORDER);
      nir_def *lo = nir_extract_u8_imm(b, info, 0);
      nir_def *hi = nir_extract_u8_imm(b, info, 2);
      nir_def *idx = nir_iadd(b, nir_imul(b, lo, hi), vtx);
      vtx = nir_isberd_nv(b, idx);
   }

   if (vtx == NULL)
      vtx = nir_imm_int(b, 0);

   unsigned addr = base + 4 * component;
   const bool offset_is_const = nir_src_is_const(nir_src_for_ssa(offset));
   if (offset_is_const) {
      unsigned const_offset = nir_src_as_uint(nir_src_for_ssa(offset));
      assert(const_offset % 16 == 0);
      addr += const_offset;

      /* Tighten the range */
      base = addr;
      range = 4 * intrin->num_components;

      if (const_offset != 0)
         offset = nir_imm_int(b, 0);
   }

   const struct nak_nir_attr_io_flags flags = {
      .output = is_output,
      .patch = is_patch,
      .phys = !offset_is_const && !is_patch,
   };

   uint32_t flags_u32;
   STATIC_ASSERT(sizeof(flags_u32) == sizeof(flags));
   memcpy(&flags_u32, &flags, sizeof(flags_u32));

   nir_def *dst_comps[NIR_MAX_VEC_COMPONENTS];
   while (mask) {
      const unsigned c = ffs(mask) - 1;
      unsigned comps = ffs(~(mask >> c)) - 1;
      assert(comps > 0);

      unsigned c_addr = addr + 4 * c;

      /* vec2 has to be vec2 aligned, vec3/4 have to be vec4 aligned.  We
       * don't have actual alignment information on these intrinsics but we
       * can assume that the indirect offset (if any) is a multiple of 16 so
       * we don't need to worry about that and can just look at c_addr.
       */
      comps = MIN2(comps, 4);
      if (c_addr & 0xf)
         comps = MIN2(comps, 2);
      if (c_addr & 0x7)
         comps = 1;
      assert(!(c_addr & 0x3));

      nir_def *c_offset = offset;
      if (flags.phys) {
         /* Physical addressing has to be scalar */
         comps = 1;

         /* Use al2p to compute a physical address */
         c_offset = nir_al2p_nv(b, offset, .base = c_addr,
                                .flags = flags_u32);
         c_addr = 0;
      }

      if (is_store) {
         nir_def *c_data = nir_channels(b, data, BITFIELD_RANGE(c, comps));
         nir_ast_nv(b, c_data, vtx, c_offset,
                    .base = c_addr,
                    .flags = flags_u32,
                    .range_base = base,
                    .range = range);
      } else {
         uint32_t access = flags.output ? 0 : ACCESS_CAN_REORDER;
         nir_def *c_data = nir_ald_nv(b, comps, vtx, c_offset,
                                      .base = c_addr,
                                      .flags = flags_u32,
                                      .range_base = base,
                                      .range = range,
                                      .access = access);
         for (unsigned i = 0; i < comps; i++)
            dst_comps[c + i] = nir_channel(b, c_data, i);
      }

      mask &= ~BITFIELD_RANGE(c, comps);
   }

   if (!is_store) {
      nir_def *dst = nir_vec(b, dst_comps, intrin->num_components);
      nir_def_rewrite_uses(&intrin->def, dst);
   }

   nir_instr_remove(&intrin->instr);

   return true;
}

bool
nak_nir_lower_vtg_io(nir_shader *nir, const struct nak_compiler *nak)
{
   return nir_shader_intrinsics_pass(nir, lower_vtg_io_intrin,
                                     nir_metadata_block_index |
                                     nir_metadata_dominance,
                                     NULL);
}
