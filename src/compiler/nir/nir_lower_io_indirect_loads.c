/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/* This pass expects IO intrinsics (load_input/load_output/...) and lowers
 * loads with indirect slot indexing to temp indexing. Supported cases:
 * - VS, TCS, TES, GS, FS inputs
 * - TCS outputs
 *
 * Indirect loads are replaced with direct loads whose results are stored in
 * a temp array, and the original load is replaced with an indirect load from
 * the temp array.
 *
 * Direct loads are typically inserted at the beginning of the shader, and only
 * the temp array loads are inserted at the place of the original indirect
 * load.
 *
 * The exceptions are TCS output loads and interpolate_at_* loads where
 * the direct loads are always inserted at the place of the original indirect
 * load.
 */

#include "nir.h"
#include "nir_builder.h"
#include <c99_alloca.h>

typedef struct {
   nir_variable *var[INTERP_MODE_COUNT];
   nir_variable *centroid[INTERP_MODE_COUNT];
   nir_variable *sample[INTERP_MODE_COUNT];
   unsigned first_comp;
   unsigned last_comp;
   bool declared;
} var_info;

typedef struct {
   nir_variable_mode modes;

   /* If arrays are loaded only once at the beginning, these are the local
    * variables.
    */
   var_info input[NUM_TOTAL_VARYING_SLOTS];
   var_info input_hi[NUM_TOTAL_VARYING_SLOTS];
   var_info output[NUM_TOTAL_VARYING_SLOTS];
   var_info output_hi[NUM_TOTAL_VARYING_SLOTS];
} lower_io_indir_loads_state;

static bool
is_compact(nir_shader *nir, nir_intrinsic_instr *intr)
{
   if (!nir->options->compact_arrays ||
       (nir->info.stage == MESA_SHADER_VERTEX && !nir_is_output_load(intr)))
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   return (sem.location >= VARYING_SLOT_CLIP_DIST0 &&
           sem.location <= VARYING_SLOT_CULL_DIST1) ||
          (nir->info.stage == MESA_SHADER_TESS_CTRL &&
          (sem.location == VARYING_SLOT_TESS_LEVEL_INNER ||
           sem.location == VARYING_SLOT_TESS_LEVEL_OUTER));
}

static var_info *
get_load_var(nir_intrinsic_instr *intr, lower_io_indir_loads_state *state)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   bool is_output = nir_is_output_load(intr);
   bool high = sem.high_dvec2 || sem.high_16bits;

   return is_output ? (high ? &state->output_hi[sem.location] :
                              &state->output[sem.location]) :
                      (high ? &state->input_hi[sem.location] :
                              &state->input[sem.location]);
}

static nir_variable **
get_load_once_variable(mesa_shader_stage stage, nir_intrinsic_instr *intr,
                       lower_io_indir_loads_state *state)
{
   if (intr->intrinsic == nir_intrinsic_load_interpolated_input) {
      /* FS input arrays are reloaded at every interpolate_at_offset/at_sample
       * because we assume that the same inputs can also be interpolated at
       * pixel/centroid.
       */
      nir_intrinsic_instr *baryc = nir_src_as_intrinsic(intr->src[0]);
      if (!baryc)
         return NULL;

      enum glsl_interp_mode interp = nir_intrinsic_interp_mode(baryc);

      switch (baryc->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         return &get_load_var(intr, state)->var[interp];
      case nir_intrinsic_load_barycentric_centroid:
         return &get_load_var(intr, state)->centroid[interp];
      case nir_intrinsic_load_barycentric_sample:
         return &get_load_var(intr, state)->sample[interp];
      default:
         return NULL;
      }
   } else if (intr->intrinsic == nir_intrinsic_load_per_vertex_input ||
              intr->intrinsic == nir_intrinsic_load_input_vertex) {
      /* The per-vertex input is loaded at the beginning (not in place) only
       * if the vertex index is constant.
       */
      if (!nir_scalar_is_const(nir_scalar_resolved(intr->src[0].ssa, 0)))
         return NULL;
   } else if (stage == MESA_SHADER_TESS_CTRL && nir_is_output_load(intr)) {
      /* TCS output arrays are reloaded at every indirect load. */
      return NULL;
   }

   /* Other inputs can be loaded at the beginning. */
   return &get_load_var(intr, state)->var[0];
}

static bool
gather_indirect_inputs(struct nir_builder *b, nir_intrinsic_instr *intr,
                       void *data)
{
   if (!nir_intrinsic_has_io_semantics(intr) ||
       !nir_intrinsic_infos[intr->intrinsic].has_dest ||
       nir_is_output_load(intr))
      return false;

   lower_io_indir_loads_state *state = (lower_io_indir_loads_state *)data;
   unsigned component = nir_intrinsic_component(intr);

   var_info *var = get_load_var(intr, state);
   unsigned first_comp = component + nir_def_first_component_read(&intr->def);
   unsigned last_comp = component + nir_def_last_component_read(&intr->def);

   if (!var->declared) {
      var->declared = true;
      var->first_comp = first_comp;
      var->last_comp = last_comp;
   } else {
      var->first_comp = MIN2(var->first_comp, first_comp);
      var->last_comp = MAX2(var->last_comp, last_comp);
   }
   return false;
}

static bool
lower_load(struct nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (!nir_intrinsic_has_io_semantics(intr) ||
       !nir_intrinsic_infos[intr->intrinsic].has_dest)
      return false;

   lower_io_indir_loads_state *state = (lower_io_indir_loads_state *)data;
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   bool is_output = nir_is_output_load(intr);
   bool compact = is_compact(b->shader, intr);
   bool progress = false;

   /* Lower indirect loads. This works for all loads (input, output,
    * interpolated) because we don't care what the load is. We just duplicate
    * it for each slot and set the offset/location.
    */
   if (state->modes & (is_output ? nir_var_shader_out : nir_var_shader_in)) {
      nir_scalar array_index =
         nir_scalar_resolved(nir_get_io_offset_src(intr)->ssa, 0);

      if (!nir_scalar_is_const(array_index)) {
         nir_variable *temp = NULL;
         nir_variable **array =
            get_load_once_variable(b->shader->info.stage, intr, state);
         unsigned first_comp, last_comp;
         unsigned intr_component = nir_intrinsic_component(intr);

         /* Load the array components that are actually used. If we load
          * them at the beginning, load all that are read anywhere
          * in the shader.
          */
         if (array) {
            var_info *var = get_load_var(intr, state);
            assert(var->declared);
            first_comp = var->first_comp;
            last_comp = var->last_comp;
         } else {
            first_comp = intr_component + nir_def_first_component_read(&intr->def);
            last_comp = intr_component + nir_def_last_component_read(&intr->def);
         }

         /* Some arrays are loaded only once at the beginning of the shader,
          * others are loaded at every indirect load (such as TCS output loads).
          */
         if (!array || !*array) {
            nir_def **elems = alloca(sizeof(nir_def*) * sem.num_slots);
            unsigned num_comp = last_comp - first_comp + 1;

            if (array)
               b->cursor = nir_before_impl(b->impl);
            else
               b->cursor = nir_before_instr(&intr->instr);

            nir_def *zero = nir_imm_int(b, 0);
            nir_def *src0 = NULL;

            /* Load the barycentrics if we are loading inputs at the beginning. */
            if (array &&
                intr->intrinsic == nir_intrinsic_load_interpolated_input) {
               nir_intrinsic_instr *baryc_intr = nir_src_as_intrinsic(intr->src[0]);

               src0 = nir_load_barycentric(b, baryc_intr->intrinsic,
                                            nir_intrinsic_interp_mode(baryc_intr));
            }

            /* Load the vertex index at the beginning if it's constant. */
            if (array &&
                (intr->intrinsic == nir_intrinsic_load_per_vertex_input ||
                 intr->intrinsic == nir_intrinsic_load_input_vertex)) {
               nir_scalar s = nir_scalar_resolved(intr->src[0].ssa, 0);
               assert(nir_scalar_is_const(s));
               src0 = nir_imm_int(b, nir_scalar_as_uint(s));
            }

            /* Load individual array elements. */
            for (unsigned i = 0; i < sem.num_slots; i++) {
               /* Create a new load for each slot. */
               nir_intrinsic_instr *new_load =
                  nir_intrinsic_instr_create(b->shader, intr->intrinsic);
               new_load->num_components = num_comp;
               nir_def_init(&new_load->instr, &new_load->def,
                            num_comp, intr->def.bit_size);
               nir_intrinsic_copy_const_indices(new_load, intr);

               /* Set the same srcs .. */
               for (unsigned src = 0;
                    src < nir_intrinsic_infos[intr->intrinsic].num_srcs; src++)
                  new_load->src[src] = nir_src_for_ssa(intr->src[src].ssa);

               /* .. but change the indirect index to 0. */
               new_load->src[nir_get_io_offset_src_number(intr)] =
                  nir_src_for_ssa(zero);

               nir_intrinsic_set_component(new_load, first_comp);

               /* Set barycentrics or the vertex index if we are loading inputs
                * at the beginning.
                */
               if (src0)
                  new_load->src[0] = nir_src_for_ssa(src0);

               /* and set IO semantics to the location of the array element. */
               nir_io_semantics new_sem = sem;
               new_sem.num_slots = 1;

               if (!compact) {
                  new_sem.location += i;
                  nir_intrinsic_set_base(new_load, nir_intrinsic_base(intr) + i);
               } else {
                  new_sem.location += i / 4;
                  nir_intrinsic_set_component(new_load, i % 4);
                  nir_intrinsic_set_base(new_load, nir_intrinsic_base(intr) + i / 4);
               }

               nir_intrinsic_set_io_semantics(new_load, new_sem);

               nir_builder_instr_insert(b, &new_load->instr);
               elems[i] = &new_load->def;
            }

            /* Put the array elements into a local array variable. */
            enum glsl_base_type base_type =
               intr->def.bit_size == 16 ? GLSL_TYPE_FLOAT16 : GLSL_TYPE_FLOAT;
            const glsl_type *type =
               glsl_array_type(glsl_vector_type(base_type, num_comp),
                               sem.num_slots, 0);
            if (!array)
               array = &temp; /* loaded at the load instead of the beginning */
            *array = nir_local_variable_create(b->impl, type, "");

            /* Fill the array with the loaded elements. */
            for (unsigned i = 0; i < sem.num_slots; i++) {
               nir_store_array_var_imm(b, *array, i, elems[i],
                                       BITFIELD_MASK(num_comp));
            }
         }

         b->cursor = nir_before_instr(&intr->instr);

         /* Get the indirect value and upsize the vector to the original size. */
         nir_def *value = nir_load_array_var(b, *array, array_index.def);

         value = nir_shift_channels(b, value,
                                    (int)first_comp - (int)intr_component,
                                    intr->def.num_components);

         nir_def_replace(&intr->def, value);
         progress = true;
      }
   }

   return progress;
}

static bool
lower_indirect_loads(nir_function_impl *impl, nir_variable_mode modes)
{
   lower_io_indir_loads_state *state = calloc(1, sizeof(*state));
   state->modes = modes;

   if (modes & nir_var_shader_in) {
      nir_function_intrinsics_pass(impl, gather_indirect_inputs,
                                   nir_metadata_all, state);
   }

   bool progress = nir_function_intrinsics_pass(impl, lower_load,
                                                nir_metadata_control_flow, state);
   free(state);
   return progress;
}

bool
nir_lower_io_indirect_loads(nir_shader *nir, nir_variable_mode modes)
{
   assert(modes & (nir_var_shader_in | nir_var_shader_out));
   assert(!(modes & ~(nir_var_shader_in | nir_var_shader_out)));
   assert((!(modes & nir_var_shader_out) ||
           nir->info.stage == MESA_SHADER_TESS_CTRL) &&
          "output lowering only supported for TCS");
   assert(nir->info.stage <= MESA_SHADER_FRAGMENT);

   bool progress = false;
   nir_foreach_function_impl(impl, nir) {
      progress |= lower_indirect_loads(impl, modes);
   }

   return progress;
}
