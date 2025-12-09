/*
 * Copyright © 2016 Broadcom
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

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

/** @file nir_lower_io_to_scalar.c
 *
 * Replaces nir_load_input/nir_store_output operations with num_components !=
 * 1 with individual per-channel operations.
 */

static void
set_io_semantics(nir_intrinsic_instr *scalar_intr,
                 nir_intrinsic_instr *vec_intr, unsigned component)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(vec_intr);
   sem.gs_streams = (sem.gs_streams >> (component * 2)) & 0x3;
   nir_intrinsic_set_io_semantics(scalar_intr, sem);
}

static void
lower_load_input_to_scalar(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *loads[NIR_MAX_VEC_COMPONENTS];

   for (unsigned i = 0; i < intr->num_components; i++) {
      bool is_64bit = (nir_intrinsic_instr_dest_type(intr) & NIR_ALU_TYPE_SIZE_MASK) == 64;
      unsigned newi = is_64bit ? i * 2 : i;
      unsigned newc = nir_intrinsic_component(intr);
      nir_intrinsic_instr *chan_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);
      nir_def_init(&chan_intr->instr, &chan_intr->def, 1,
                   intr->def.bit_size);
      chan_intr->num_components = 1;

      if (intr->name)
         chan_intr->name = intr->name;
      nir_intrinsic_set_base(chan_intr, nir_intrinsic_base(intr));
      nir_intrinsic_set_component(chan_intr, (newc + newi) % 4);
      nir_intrinsic_set_dest_type(chan_intr, nir_intrinsic_dest_type(intr));
      set_io_semantics(chan_intr, intr, i);
      /* offset and vertex (if needed) */
      for (unsigned j = 0; j < nir_intrinsic_infos[intr->intrinsic].num_srcs; ++j)
         chan_intr->src[j] = nir_src_for_ssa(intr->src[j].ssa);
      if (newc + newi > 3) {
         nir_src *src = nir_get_io_offset_src(chan_intr);
         nir_def *offset = nir_iadd_imm(b, src->ssa, (newc + newi) / 4);
         *src = nir_src_for_ssa(offset);
      }

      nir_builder_instr_insert(b, &chan_intr->instr);

      loads[i] = &chan_intr->def;
   }

   nir_def_replace(&intr->def, nir_vec(b, loads, intr->num_components));
}

static void
lower_load_to_scalar(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *loads[NIR_MAX_VEC_COMPONENTS];

   for (unsigned i = 0; i < intr->num_components; i++) {
      nir_intrinsic_instr *chan_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);
      nir_def_init(&chan_intr->instr, &chan_intr->def, 1,
                   intr->def.bit_size);
      chan_intr->num_components = 1;

      if (intr->name)
         chan_intr->name = intr->name;
      nir_intrinsic_set_align_offset(chan_intr,
                                     (nir_intrinsic_align_offset(intr) +
                                      i * (intr->def.bit_size / 8)) %
                                        nir_intrinsic_align_mul(intr));
      nir_intrinsic_set_align_mul(chan_intr, nir_intrinsic_align_mul(intr));
      if (nir_intrinsic_has_access(intr))
         nir_intrinsic_set_access(chan_intr, nir_intrinsic_access(intr));
      if (nir_intrinsic_has_range(intr))
         nir_intrinsic_set_range(chan_intr, nir_intrinsic_range(intr));
      if (nir_intrinsic_has_range_base(intr))
         nir_intrinsic_set_range_base(chan_intr, nir_intrinsic_range_base(intr));
      if (nir_intrinsic_has_base(intr))
         nir_intrinsic_set_base(chan_intr, nir_intrinsic_base(intr));
      for (unsigned j = 0; j < nir_intrinsic_infos[intr->intrinsic].num_srcs - 1; j++)
         chan_intr->src[j] = nir_src_for_ssa(intr->src[j].ssa);

      /* increment offset per component */
      nir_io_offset offset =
         nir_io_offset_iadd(b, intr, i * (intr->def.bit_size / 8));
      nir_set_io_offset(chan_intr, offset);

      nir_builder_instr_insert(b, &chan_intr->instr);

      loads[i] = &chan_intr->def;
   }

   nir_def_replace(&intr->def, nir_vec(b, loads, intr->num_components));
}

static void
lower_store_output_to_scalar(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *value = intr->src[0].ssa;

   for (unsigned i = 0; i < intr->num_components; i++) {
      if (!(nir_intrinsic_write_mask(intr) & (1 << i)))
         continue;

      bool is_64bit = (nir_intrinsic_instr_src_type(intr, 0) & NIR_ALU_TYPE_SIZE_MASK) == 64;
      unsigned newi = is_64bit ? i * 2 : i;
      unsigned newc = nir_intrinsic_component(intr);
      unsigned new_component = (newc + newi) % 4;
      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      bool has_xfb = false;

      if (nir_intrinsic_has_io_xfb(intr)) {
         /* Find out which components are written via xfb. */
         for (unsigned c = 0; c <= new_component; c++) {
            nir_io_xfb xfb = c < 2 ? nir_intrinsic_io_xfb(intr) : nir_intrinsic_io_xfb2(intr);

            if (new_component < c + xfb.out[c % 2].num_components) {
               has_xfb = true;
               break;
            }
         }
      }

      /* After scalarization, some channels might not write anywhere - i.e.
       * they are not a sysval output, they don't feed the next shader, and
       * they don't write xfb. Don't create such stores.
       */
      bool tcs_read = b->shader->info.stage == MESA_SHADER_TESS_CTRL &&
                (sem.location >= VARYING_SLOT_VAR0_16BIT ?
                    b->shader->info.outputs_read_16bit &
                    BITFIELD_BIT(sem.location - VARYING_SLOT_VAR0_16BIT) :
                 sem.location >= VARYING_SLOT_PATCH0 ?
                    b->shader->info.patch_outputs_read &
                    BITFIELD_BIT(sem.location - VARYING_SLOT_PATCH0) :
                 b->shader->info.outputs_read & BITFIELD64_BIT(sem.location));
      if ((sem.no_sysval_output || !nir_slot_is_sysval_output(sem.location, MESA_SHADER_NONE)) &&
          ((sem.no_varying && !tcs_read) ||
           !nir_slot_is_varying(sem.location, MESA_SHADER_NONE)) &&
          !has_xfb)
         continue;

      nir_intrinsic_instr *chan_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);
      chan_intr->num_components = 1;

      if (intr->name)
         chan_intr->name = intr->name;
      nir_intrinsic_set_base(chan_intr, nir_intrinsic_base(intr));
      nir_intrinsic_set_write_mask(chan_intr, 0x1);
      nir_intrinsic_set_component(chan_intr, new_component);
      nir_intrinsic_set_src_type(chan_intr, nir_intrinsic_src_type(intr));
      set_io_semantics(chan_intr, intr, i);

      if (nir_intrinsic_has_io_xfb(intr)) {
         /* Scalarize transform feedback info. */
         for (unsigned c = 0; c <= new_component; c++) {
            nir_io_xfb xfb = c < 2 ? nir_intrinsic_io_xfb(intr) : nir_intrinsic_io_xfb2(intr);

            if (new_component < c + xfb.out[c % 2].num_components) {
               nir_io_xfb scalar_xfb;

               memset(&scalar_xfb, 0, sizeof(scalar_xfb));
               scalar_xfb.out[new_component % 2].num_components = is_64bit ? 2 : 1;
               scalar_xfb.out[new_component % 2].buffer = xfb.out[c % 2].buffer;
               scalar_xfb.out[new_component % 2].offset = xfb.out[c % 2].offset +
                                                          new_component - c;
               if (new_component < 2)
                  nir_intrinsic_set_io_xfb(chan_intr, scalar_xfb);
               else
                  nir_intrinsic_set_io_xfb2(chan_intr, scalar_xfb);
               break;
            }
         }
      }

      /* value */
      chan_intr->src[0] = nir_src_for_ssa(nir_channel(b, value, i));
      /* offset and vertex (if needed) */
      for (unsigned j = 1; j < nir_intrinsic_infos[intr->intrinsic].num_srcs; ++j)
         chan_intr->src[j] = nir_src_for_ssa(intr->src[j].ssa);
      if (newc + newi > 3) {
         nir_src *src = nir_get_io_offset_src(chan_intr);
         nir_def *offset = nir_iadd_imm(b, src->ssa, (newc + newi) / 4);
         *src = nir_src_for_ssa(offset);
      }

      nir_builder_instr_insert(b, &chan_intr->instr);
   }

   nir_instr_remove(&intr->instr);
}

static void
lower_store_to_scalar(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *value = intr->src[0].ssa;

   /* iterate wrmask instead of num_components to handle split components */
   u_foreach_bit(i, nir_intrinsic_write_mask(intr)) {
      nir_intrinsic_instr *chan_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);
      chan_intr->num_components = 1;

      if (intr->name)
         chan_intr->name = intr->name;
      nir_intrinsic_set_write_mask(chan_intr, 0x1);
      nir_intrinsic_set_align_offset(chan_intr,
                                     (nir_intrinsic_align_offset(intr) +
                                      i * (value->bit_size / 8)) %
                                        nir_intrinsic_align_mul(intr));
      nir_intrinsic_set_align_mul(chan_intr, nir_intrinsic_align_mul(intr));
      if (nir_intrinsic_has_access(intr))
         nir_intrinsic_set_access(chan_intr, nir_intrinsic_access(intr));
      if (nir_intrinsic_has_base(intr))
         nir_intrinsic_set_base(chan_intr, nir_intrinsic_base(intr));

      /* value */
      chan_intr->src[0] = nir_src_for_ssa(nir_channel(b, value, i));
      for (unsigned j = 1; j < nir_intrinsic_infos[intr->intrinsic].num_srcs - 1; j++)
         chan_intr->src[j] = nir_src_for_ssa(intr->src[j].ssa);

      /* increment offset per component */
      nir_io_offset offset =
         nir_io_offset_iadd(b, intr, i * (value->bit_size / 8));
      nir_set_io_offset(chan_intr, offset);

      nir_builder_instr_insert(b, &chan_intr->instr);
   }

   nir_instr_remove(&intr->instr);
}

struct scalarize_state {
   nir_variable_mode mask;
   nir_intrin_filter_cb filter;
   void *filter_data;
};

static bool
nir_lower_io_to_scalar_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct scalarize_state *state = data;

   if (intr->num_components == 1)
      return false;

   if ((intr->intrinsic == nir_intrinsic_load_input ||
        intr->intrinsic == nir_intrinsic_load_per_primitive_input ||
        intr->intrinsic == nir_intrinsic_load_per_vertex_input ||
        intr->intrinsic == nir_intrinsic_load_interpolated_input ||
        intr->intrinsic == nir_intrinsic_load_input_vertex) &&
       (state->mask & nir_var_shader_in) &&
       (!state->filter || state->filter(intr, state->filter_data))) {
      lower_load_input_to_scalar(b, intr);
      return true;
   }

   if ((intr->intrinsic == nir_intrinsic_load_output ||
        intr->intrinsic == nir_intrinsic_load_per_vertex_output ||
        intr->intrinsic == nir_intrinsic_load_per_view_output ||
        intr->intrinsic == nir_intrinsic_load_per_primitive_output) &&
       (state->mask & nir_var_shader_out) &&
       (!state->filter || state->filter(intr, state->filter_data))) {
      lower_load_input_to_scalar(b, intr);
      return true;
   }

   if (((intr->intrinsic == nir_intrinsic_load_ubo && (state->mask & nir_var_mem_ubo)) ||
        (intr->intrinsic == nir_intrinsic_load_ssbo && (state->mask & nir_var_mem_ssbo)) ||
        (intr->intrinsic == nir_intrinsic_load_global && (state->mask & nir_var_mem_global)) ||
        (intr->intrinsic == nir_intrinsic_load_shared && (state->mask & nir_var_mem_shared)) ||
        (intr->intrinsic == nir_intrinsic_load_push_constant && (state->mask & nir_var_mem_push_const))) &&
       (!state->filter || state->filter(intr, state->filter_data))) {
      lower_load_to_scalar(b, intr);
      return true;
   }

   if ((intr->intrinsic == nir_intrinsic_store_output ||
        intr->intrinsic == nir_intrinsic_store_per_vertex_output ||
        intr->intrinsic == nir_intrinsic_store_per_view_output ||
        intr->intrinsic == nir_intrinsic_store_per_primitive_output) &&
       state->mask & nir_var_shader_out &&
       (!state->filter || state->filter(intr, state->filter_data))) {
      lower_store_output_to_scalar(b, intr);
      return true;
   }

   if (((intr->intrinsic == nir_intrinsic_store_ssbo && (state->mask & nir_var_mem_ssbo)) ||
        (intr->intrinsic == nir_intrinsic_store_global && (state->mask & nir_var_mem_global)) ||
        (intr->intrinsic == nir_intrinsic_store_shared && (state->mask & nir_var_mem_shared))) &&
       (!state->filter || state->filter(intr, state->filter_data))) {
      lower_store_to_scalar(b, intr);
      return true;
   }

   return false;
}

bool
nir_lower_io_to_scalar(nir_shader *shader, nir_variable_mode mask, nir_intrin_filter_cb filter, void *filter_data)
{
   struct scalarize_state state = {
      mask,
      filter,
      filter_data
   };
   return nir_shader_intrinsics_pass(shader,
                                     nir_lower_io_to_scalar_instr,
                                     nir_metadata_control_flow,
                                     &state);
}
