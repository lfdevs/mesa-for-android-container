/*
 * Copyright © 2016 Intel Corporation
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

/*
 * This lowering pass converts references to variables with loads/stores to
 * scratch space based on a few configurable parameters.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

static void
lower_load_store(nir_builder *b,
                 nir_intrinsic_instr *intrin,
                 glsl_type_size_align_func size_align)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   nir_def *offset =
      nir_iadd_imm(b, nir_build_deref_offset(b, deref, size_align),
                   var->data.location);

   unsigned align, UNUSED size;
   size_align(deref->type, &size, &align);

   if (intrin->intrinsic == nir_intrinsic_load_deref) {
      unsigned bit_size = intrin->def.bit_size;
      nir_def *value = nir_load_scratch(
         b, intrin->num_components, bit_size == 1 ? 32 : bit_size, offset, .align_mul = align);
      if (bit_size == 1)
         value = nir_b2b1(b, value);

      nir_def_rewrite_uses(&intrin->def, value);
   } else {
      assert(intrin->intrinsic == nir_intrinsic_store_deref);

      nir_def *value = intrin->src[1].ssa;
      if (value->bit_size == 1)
         value = nir_b2b32(b, value);

      nir_store_scratch(b, value, offset, .align_mul = align,
                        .write_mask = nir_intrinsic_write_mask(intrin));
   }

   nir_instr_remove(&intrin->instr);
   nir_deref_instr_remove_if_unused(deref);
}

static bool
only_used_for_load_store(nir_deref_instr *deref)
{
   nir_foreach_use(src, &deref->def) {
      if (!nir_src_use_instr(src))
         return false;
      if (nir_src_use_instr(src)->type == nir_instr_type_deref) {
         if (!only_used_for_load_store(nir_instr_as_deref(nir_src_use_instr(src))))
            return false;
      } else if (nir_src_use_instr(src)->type != nir_instr_type_intrinsic) {
         return false;
      } else {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(nir_src_use_instr(src));
         if (intrin->intrinsic != nir_intrinsic_load_deref &&
             intrin->intrinsic != nir_intrinsic_store_deref)
            return false;
      }
   }
   return true;
}

/**
 * Lowers indirect-addressed function temporary variables to scratch accesses
 * based on a driver-provided callback selecting which variables to lower.
 *
 * Most drivers need this in some form -- a large array may be larger than the
 * register space, so for an indirect store (not lowered to a series of csels
 * using nir_lower_indirect_derefs) you would simply not be able to register
 * allocate for the instruction.  In that case you want to move the whole array
 * to scratch memory and have the load/stores be handled using NIR scratch
 * intrinsics.
 *
 * The callback lets you make a global decision of which vars to spill based on
 * the set of indirect-addressed function temps.  If scheduling an instruction
 * could mean more than one array must be fully unspilled, then you might want
 * to decide which variables to spill as a maximum register pressure calculation
 * of variables you're going to leave as function temps.
 *
 * @scratch_layout_size_align function to use to compute the size and alignment
 *                             of the values in scratch space.
 * @cb driver callback that will be called if there are any candidates to spill.
 *     It will be passed the set of candidate nir_variables, along with the
 *     driver-provided @data, and any variables left in the set after the
 *     callback will be spilled to scratch.
 * @data driver data to pass to the callback The callback is passed the set of
 *       nir_variable pointers to consider.  Any variables not removed from the
 *       set will be spilled to scratch after the callback.
 */
bool
nir_lower_vars_to_scratch_global(nir_shader *shader,
                                 glsl_type_size_align_func scratch_layout_size_align,
                                 nir_lower_vars_to_scratch_cb cb, void *data)
{
   struct set *set = _mesa_pointer_set_create(NULL);

   /* First, we walk the instructions and flag any variables we want to lower
    * by removing them from their respective list and setting the mode to 0.
    */
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_deref &&
                intrin->intrinsic != nir_intrinsic_store_deref)
               continue;

            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (!nir_deref_mode_is_one_of(deref, nir_var_function_temp))
               continue;

            if (!nir_deref_instr_has_indirect(nir_src_as_deref(intrin->src[0])))
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            if (!var)
               continue;

            /* We set var->mode to 0 to indicate that a variable will be moved
             * to scratch.  Don't assign a scratch location twice.
             */
            if (var->data.mode == 0)
               continue;

            _mesa_set_add(set, var);
         }
      }
   }

   /* Have the driver pick which variables to lower (if any) */
   if (set->entries != 0)
      cb(set, data);

   if (set->entries == 0) {
      _mesa_set_destroy(set, NULL);
      return false;
   }

   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_deref)
               continue;

            nir_deref_instr *deref = nir_instr_as_deref(instr);

            if (nir_deref_instr_remove_if_unused(deref)) {
               progress = true;
               continue;
            }

            if (deref->deref_type != nir_deref_type_var)
               continue;

            struct set_entry *entry = _mesa_set_search(set, deref->var);
            if (!entry)
               continue;

            if (!only_used_for_load_store(deref))
               _mesa_set_remove(set, entry);
         }
      }
   }

   set_foreach(set, entry) {
      nir_variable *var = (void *)entry->key;

      /* Remove it from its list */
      exec_node_remove(&var->node);
      /* Invalid mode used to flag "moving to scratch" */
      var->data.mode = 0;

      /* We don't allocate space here as iteration in this loop is
       * non-deterministic due to the nir_variable pointers. */
      var->data.location = INT_MAX;
   }

   nir_foreach_function_impl(impl, shader) {
      nir_builder build = nir_builder_create(impl);

      bool impl_progress = false;
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_deref &&
                intrin->intrinsic != nir_intrinsic_store_deref)
               continue;

            nir_variable *var = nir_intrinsic_get_var(intrin, 0);
            /* Variables flagged for lowering above have mode == 0 */
            if (!var || var->data.mode)
               continue;

            if (var->data.location == INT_MAX) {
               unsigned var_size, var_align;
               scratch_layout_size_align(var->type, &var_size, &var_align);

               var->data.location = ALIGN_POT(shader->scratch_size, var_align);
               shader->scratch_size = var->data.location + var_size;
            }

            lower_load_store(&build, intrin, scratch_layout_size_align);
            impl_progress = true;
         }
      }

      progress |= nir_progress(impl_progress, impl,
                               nir_metadata_control_flow);
   }

   _mesa_set_destroy(set, NULL);

   return progress;
}

struct nir_lower_vars_to_scratch_state {
   int size_threshold;
   glsl_type_size_align_func variable_size_align;
};

/**
 * Callback for nir_lower_vars_to_scratch: Remove any vars from the set to spill
 * that are under the size threshold.
 */
static void
nir_lower_vars_to_scratch_size_cb(struct set *set, void *data)
{
   struct nir_lower_vars_to_scratch_state *state = data;

   set_foreach(set, entry) {
      nir_variable *var = (void *)entry->key;
      unsigned var_size, var_align;
      state->variable_size_align(var->type, &var_size, &var_align);
      if (var_size <= state->size_threshold)
         _mesa_set_remove(set, entry);
   }
}

/**
 * Lowers indirect-addressed function temporary variables to scratch accesses
 * based on a size threshold for variables to lower.
 *
 * See nir_lower_vars_to_scratch_global for more explanation.
*/
bool
nir_lower_vars_to_scratch(nir_shader *shader,
                          int size_threshold,
                          glsl_type_size_align_func variable_size_align,
                          glsl_type_size_align_func scratch_layout_size_align)
{
   struct nir_lower_vars_to_scratch_state state = {
      .size_threshold = size_threshold,
      .variable_size_align = variable_size_align,
   };

   return nir_lower_vars_to_scratch_global(shader, scratch_layout_size_align,
                                           nir_lower_vars_to_scratch_size_cb, &state);
}
