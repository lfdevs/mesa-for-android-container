/*
 * Copyright (C) 2020 Google, Inc.
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nir.h"

/**
 * Return the intrinsic if it matches the mask in "modes", else return NULL.
 */
nir_intrinsic_instr *
nir_get_io_intrinsic(nir_instr *instr, nir_variable_mode modes,
                     nir_variable_mode *out_mode)
{
   if (instr->type != nir_instr_type_intrinsic)
      return NULL;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_primitive_input:
   case nir_intrinsic_load_input_vertex:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_per_vertex_input:
      *out_mode = nir_var_shader_in;
      return modes & nir_var_shader_in ? intr : NULL;
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_per_view_output:
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_view_output:
   case nir_intrinsic_store_per_primitive_output:
      *out_mode = nir_var_shader_out;
      return modes & nir_var_shader_out ? intr : NULL;
   default:
      return NULL;
   }
}

/**
 * Recompute the IO "base" indices from scratch to remove holes or to fix
 * incorrect base values due to changes in IO locations by using IO locations
 * to assign new bases. The mapping from locations to bases becomes
 * monotonically increasing.
 */
bool
nir_recompute_io_bases(nir_shader *nir, nir_variable_mode modes)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   bool make_colors_last = nir->options->io_options &
                           nir_io_assign_color_input_bases_after_all_other_inputs;

   BITSET_DECLARE(inputs, NUM_TOTAL_VARYING_SLOTS);
   BITSET_DECLARE(per_prim_inputs, NUM_TOTAL_VARYING_SLOTS);  /* FS only */
   /* radeonsi prefers color inputs to be last. */
   BITSET_DECLARE(color_inputs, 2); /* VARYING_SLOT_COL{0,1}, FS only */
   BITSET_DECLARE(dual_slot_inputs, NUM_TOTAL_VARYING_SLOTS); /* VS only */
   BITSET_DECLARE(outputs, NUM_TOTAL_VARYING_SLOTS);

   BITSET_ZERO(inputs);
   BITSET_ZERO(per_prim_inputs);
   BITSET_ZERO(color_inputs);
   BITSET_ZERO(dual_slot_inputs);
   BITSET_ZERO(outputs);

   /* Gather the bitmasks of used locations. */
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         nir_variable_mode mode;
         nir_intrinsic_instr *intr = nir_get_io_intrinsic(instr, modes, &mode);
         if (!intr)
            continue;

         nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
         unsigned num_slots = sem.num_slots;
         if (sem.medium_precision)
            num_slots = (num_slots + sem.high_16bits + 1) / 2;

         if (mode == nir_var_shader_in) {
            for (unsigned i = 0; i < num_slots; i++) {
               unsigned location = sem.location + i;
               /* GPU like AMD require per primitive inputs come after per
                * vertex inputs.
                */
               if (intr->intrinsic == nir_intrinsic_load_per_primitive_input ||
                   /* Some fragment shader input varying is per vertex when vertex
                    * pipeline, per primitive when mesh pipeline. In order to share
                    * the same fragment shader code, we move these varyings after
                    * other per vertex varyings by handling them like per primitive
                    * varyings here.
                    */
                   (nir->info.stage == MESA_SHADER_FRAGMENT &&
                    (location == VARYING_SLOT_PRIMITIVE_ID ||
                     location == VARYING_SLOT_VIEWPORT ||
                     location == VARYING_SLOT_LAYER)))
                  BITSET_SET(per_prim_inputs, location);
               else if (make_colors_last &&
                        nir->info.stage == MESA_SHADER_FRAGMENT &&
                        (location == VARYING_SLOT_COL0 ||
                         location == VARYING_SLOT_COL1))
                  BITSET_SET(color_inputs, location - VARYING_SLOT_COL0);
               else
                  BITSET_SET(inputs, location);

               if (sem.high_dvec2)
                  BITSET_SET(dual_slot_inputs, location);
            }
         } else if (!sem.dual_source_blend_index) {
            for (unsigned i = 0; i < num_slots; i++)
               BITSET_SET(outputs, sem.location + i);
         }
      }
   }

   const unsigned num_normal_inputs = BITSET_COUNT(inputs) +
                                      BITSET_COUNT(dual_slot_inputs) +
                                      BITSET_COUNT(color_inputs);
   assert(nir->info.stage == MESA_SHADER_FRAGMENT ||
          BITSET_COUNT(color_inputs) == 0);

   /* Renumber bases. */
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         nir_variable_mode mode;
         nir_intrinsic_instr *intr = nir_get_io_intrinsic(instr, modes, &mode);
         if (!intr)
            continue;

         nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
         unsigned num_slots = sem.num_slots;
         if (sem.medium_precision)
            num_slots = (num_slots + sem.high_16bits + 1) / 2;

         unsigned new_base;

         if (mode == nir_var_shader_in) {
            if (BITSET_TEST(per_prim_inputs, sem.location)){
               new_base = num_normal_inputs +
                          BITSET_PREFIX_SUM(per_prim_inputs, sem.location);
            } else if (make_colors_last &&
                       nir->info.stage == MESA_SHADER_FRAGMENT &&
                       (sem.location == VARYING_SLOT_COL0 ||
                        sem.location == VARYING_SLOT_COL1)) {
               new_base = BITSET_COUNT(inputs) +
                          BITSET_PREFIX_SUM(color_inputs,
                                            sem.location - VARYING_SLOT_COL0);
            } else {
               new_base = BITSET_PREFIX_SUM(inputs, sem.location) +
                          BITSET_PREFIX_SUM(dual_slot_inputs, sem.location) +
                          (sem.high_dvec2 ? 1 : 0);
            }
         } else if (sem.dual_source_blend_index) {
            new_base = BITSET_PREFIX_SUM(outputs, NUM_TOTAL_VARYING_SLOTS);
         } else {
            new_base = BITSET_PREFIX_SUM(outputs, sem.location);
         }

         if (nir_intrinsic_base(intr) != new_base) {
            nir_intrinsic_set_base(intr, new_base);
            progress = true;
         }
      }
   }



   if (modes & nir_var_shader_in) {
      unsigned num_inputs = BITSET_COUNT(inputs) + BITSET_COUNT(color_inputs) +
                            BITSET_COUNT(per_prim_inputs);

      if (nir->num_inputs != num_inputs) {
         nir->num_inputs = num_inputs;
         progress = true;
      }
   }

   if (modes & nir_var_shader_out) {
      unsigned num_outputs = BITSET_COUNT(outputs);

      if (nir->num_outputs != num_outputs) {
         nir->num_outputs = num_outputs;
         progress = true;
      }
   }

   return nir_progress(progress, impl, nir_metadata_all);
}
