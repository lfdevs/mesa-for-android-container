/*
 * Copyright (C) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "nir_builder.h"

static bool
lower_vs_input_intr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_input)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *vtx_id = nir_load_raw_vertex_id(b);
   nir_def *ins_id = nir_load_instance_id(b);
   nir_def *offset = intr->src[0].ssa;

   const unsigned component = nir_intrinsic_component(intr);
   const nir_io_semantics io = nir_intrinsic_io_semantics(intr);

   /* Disregard the signedness of an integer, since loading 32-bits into a
    * 32-bit register should be bit exact so should not incur any clamping.
    * If we are reading as a u32, then it must be paired with an integer (u32
    * or s32) source, so use .auto32 to disregard.
    */
   nir_alu_type dest_type = nir_intrinsic_dest_type(intr);
   if (dest_type == nir_type_uint32 || dest_type == nir_type_int32)
      dest_type = 32;

   /* The driver pre-populates the base with a descriptor handle */
   nir_def *handle = nir_iadd_imm(b, offset, nir_intrinsic_base(intr));

   /* The hardware doesn't have a component field so we have to load it all
    * and drop unused components.
    */
   const unsigned num_components = intr->num_components + component;

   nir_def *res = nir_load_attr_pan(b, num_components, intr->def.bit_size,
                                    vtx_id, ins_id, handle,
                                    .dest_type = dest_type,
                                    .io_semantics = io);

   res = nir_shift_channels(b, res, -(int)component, intr->num_components);

   nir_def_replace(&intr->def, res);
   return true;
}

bool
pan_nir_lower_vs_inputs(nir_shader *shader, uint64_t gpu_id)
{
   return nir_shader_intrinsics_pass(shader, lower_vs_input_intr,
                                    nir_metadata_control_flow,
                                    (void *)&gpu_id);
}
