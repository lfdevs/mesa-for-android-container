/*
 * Copyright (c) 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "brw_nir.h"

static nir_def *
load_input_vertex(struct nir_builder *b, nir_intrinsic_instr *intrin,
                  int vtx_index, uint8_t num_components)
{
   return nir_load_input_vertex(b,
                                num_components,
                                intrin->def.bit_size,
                                nir_imm_int(b, vtx_index),
                                intrin->src[0].ssa,
                                .base = nir_intrinsic_base(intrin),
                                .component = nir_intrinsic_component(intrin),
                                /* No dest_type means it's fs_interp_deltas,
                                 * and then we just want floats.
                                 */
                                .dest_type = nir_intrinsic_has_dest_type(intrin) ?
                                             nir_intrinsic_dest_type(intrin) :
                                             nir_type_float | intrin->def.bit_size,
                                .io_semantics = nir_intrinsic_io_semantics(intrin));
}

/* If an input is marked for constant interpolation, the HW will copy the
 * value of the provoking vertex to all components in the FS payload.
 * However, due to the way we have to program the provoking vertex state
 * to respect the order in which Vulkan says the per-vertex values should
 * come, we cannot count on that value being correct.
 * To work around that, we convert any load_input into a load_input_vertex
 * for the corresponding vertex index depending on the provoking vertex value.
 */
static bool
lower_flat_inputs(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_input)
      return false;

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *first_vtx = load_input_vertex(b, intrin, 0, intrin->def.num_components);
   nir_def *last_vtx = load_input_vertex(b, intrin, 2, intrin->def.num_components);

   nir_def *msaa_flags = nir_load_fs_msaa_intel(b);

   nir_def *last = nir_test_mask(b, msaa_flags, INTEL_MSAA_FLAG_PROVOKING_VERTEX_LAST);
   nir_def *input_vertex = nir_bcsel(b, last, last_vtx, first_vtx);

   nir_def_replace(&intrin->def, input_vertex);
   return true;
}

static nir_def *
get_bary_deltas(nir_builder *b, nir_intrinsic_instr *bary, nir_intrinsic_op op, unsigned interp_mode)
{
   nir_def *deltas;

   switch (op) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample:
      deltas = nir_load_barycentric(b, op, interp_mode);
      break;
   case nir_intrinsic_load_barycentric_at_sample:
      deltas = nir_load_barycentric_at_sample(b, bary->def.bit_size,
                                              bary->src[0].ssa,
                                              .interp_mode = interp_mode);
      break;
   case nir_intrinsic_load_barycentric_at_offset:
      deltas = nir_load_barycentric_at_offset(b, bary->def.bit_size,
                                              bary->src[0].ssa,
                                              .interp_mode = interp_mode);

      break;
   default:
      UNREACHABLE("invalid barycentric op");
   }

   return deltas;
}

/* Lower the coord version of bary intrinsics to the non-coord version,
 * then calculate the three components from the assumption they all add
 * up to 1.0.
 */
static bool
lower_coord_barycentrics(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   nir_intrinsic_op op;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_barycentric_coord_pixel:
      op = nir_intrinsic_load_barycentric_pixel;
      break;
   case nir_intrinsic_load_barycentric_coord_centroid:
      op = nir_intrinsic_load_barycentric_centroid;
      break;
   case nir_intrinsic_load_barycentric_coord_sample:
      op = nir_intrinsic_load_barycentric_sample;
      break;
   case nir_intrinsic_load_barycentric_coord_at_sample:
      op = nir_intrinsic_load_barycentric_at_sample;
      break;
   case nir_intrinsic_load_barycentric_coord_at_offset:
      op = nir_intrinsic_load_barycentric_at_offset;
      break;
   default:
      return false;
   }

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *delta_xy = get_bary_deltas(b, intrin, op,
                                       nir_intrinsic_interp_mode(intrin));

   nir_def *barys[3], *res;
   barys[0] = nir_fsub_imm(b, 1.0f,
                           nir_fadd(b,
                                    nir_channel(b, delta_xy, 0),
                                    nir_channel(b, delta_xy, 1)));
   barys[1] = nir_channel(b, delta_xy, 0);
   barys[2] = nir_channel(b, delta_xy, 1);

   res = nir_vec(b, barys, 3);
   nir_def_replace(&intrin->def, res);

   return true;
}

/* The HW can give us the interpolation deltas for inputs, or the per-vertex
 * values, but it does not mix them. If we have any per-vertex inputs, we need
 * to calculate the deltas for any old fashioned interpolated values
 * ourselves.
 */
static bool
lower_fs_interp_deltas(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_fs_input_interp_deltas)
      return false;

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *vertices[3] = {
      load_input_vertex(b, intrin, 0, 1),
      load_input_vertex(b, intrin, 2, 1),
      load_input_vertex(b, intrin, 1, 1),
   };
   nir_def *deltas[3] = {
      vertices[0],
      nir_fsub(b, vertices[1], vertices[0]),
      nir_fsub(b, vertices[2], vertices[0]),
   };
   nir_def *vec = nir_vec(b, deltas, 3);
   nir_def_replace(&intrin->def, vec);

   return true;
}

void
brw_nir_lower_fs_barycentrics(nir_shader *shader)
{
   NIR_PASS(_, shader, nir_shader_intrinsics_pass,
            lower_flat_inputs,
            nir_metadata_control_flow,
            NULL);

   NIR_PASS(_, shader, nir_shader_intrinsics_pass,
            lower_coord_barycentrics,
            nir_metadata_control_flow,
            NULL);

   NIR_PASS(_, shader, nir_shader_intrinsics_pass,
            lower_fs_interp_deltas,
            nir_metadata_control_flow,
            NULL);

   /* If there were any flat inputs, we lowered to per-vertex,
    * so change the interpolation mode here to avoid setting
    * them up for constant interpolation in the SBE setup.
    */
   nir_foreach_shader_in_variable(var, shader) {
      if (var->data.interpolation == INTERP_MODE_FLAT) {
         var->data.interpolation = INTERP_MODE_SMOOTH;
      }
   }
}
