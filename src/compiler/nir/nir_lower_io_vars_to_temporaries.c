/*
 * Copyright © 2015 Intel Corporation
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
 * Implements a pass that lowers output and/or input variables to a
 * temporary plus an output variable with a single copy at each exit
 * point of the shader and/or an input variable with a single copy
 * at the entrance point of the shader.  This way the output variable
 * is only ever written once and/or input is only read once, and there
 * are no indirect outut/input accesses.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

struct lower_io_state {
   nir_shader *shader;
   nir_function_impl *entrypoint;
   struct exec_list old_outputs;
   struct exec_list old_inputs;
   struct exec_list new_outputs;
   struct exec_list new_inputs;

   /* map from temporary to new input */
   struct hash_table *input_map;
};

static void
emit_copies(nir_builder *b, struct exec_list *dest_vars,
            struct exec_list *src_vars, unsigned stream)
{
   assert(exec_list_length(dest_vars) == exec_list_length(src_vars));

   foreach_two_lists(dest_node, dest_vars, src_node, src_vars) {
      nir_variable *dest = exec_node_data(nir_variable, dest_node, node);
      nir_variable *src = exec_node_data(nir_variable, src_node, node);

      /* No need to copy the contents of a non-fb_fetch_output output variable
       * to the temporary allocated for it, since its initial value is
       * undefined.
       */
      if (src->data.mode == nir_var_shader_out &&
          !src->data.fb_fetch_output)
         continue;

      /* No need to copy the contents of a pixel-local output variable
       * to the temporary allocated for it, since its initial value is
       * undefined. Similarly, no need to copy data to a pixel-local
       * input variable since it's supposed to be read-only.
       */
      if (src->data.mode == nir_var_mem_pixel_local_out ||
          dest->data.mode == nir_var_mem_pixel_local_in)
         continue;

      /* Don't copy those variables before emit_vertex that are not emitted
       * by that emit_vertex instruction.
       */
      unsigned stream_mask = 0;

      if (dest->data.stream & NIR_STREAM_PACKED) {
         unsigned slots = glsl_count_dword_slots(src->type, false);

         assert(slots && slots <= 4);

         for (unsigned i = 0; i < slots; i++) {
            unsigned stream = (dest->data.stream >> (i * 2)) & 0x3;

            stream_mask |= BITFIELD_BIT(stream);
         }
      } else {
         assert(dest->data.stream < 4);
         stream_mask = BITFIELD_BIT(dest->data.stream);
      }

      if (!(BITFIELD_BIT(stream) & stream_mask))
         continue;

      /* Can't copy the contents of the temporary back to a read-only
       * interface variable.  The value of the temporary won't have been
       * modified by the shader anyway.
       */
      if (dest->data.read_only)
         continue;

      nir_copy_var(b, dest, src);
   }
}

static void
emit_output_copies_impl(struct lower_io_state *state, nir_function_impl *impl)
{
   nir_builder b = nir_builder_create(impl);

   if (state->shader->info.stage == MESA_SHADER_GEOMETRY) {
      /* For geometry shaders, we have to emit the output copies right
       * before each EmitVertex call.
       */
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic == nir_intrinsic_emit_vertex ||
                intrin->intrinsic == nir_intrinsic_emit_vertex_with_counter) {
               b.cursor = nir_before_instr(&intrin->instr);
               emit_copies(&b, &state->new_outputs, &state->old_outputs,
                           nir_intrinsic_stream_id(intrin));
            }
         }
      }
   } else if (impl == state->entrypoint) {
      b.cursor = nir_before_impl(impl);
      emit_copies(&b, &state->old_outputs, &state->new_outputs, 0);

      /* For all other shader types, we need to do the copies right before
       * the jumps to the end block.
       */
      set_foreach(&impl->end_block->predecessors, block_entry) {
         struct nir_block *block = (void *)block_entry->key;
         b.cursor = nir_after_block_before_jump(block);
         emit_copies(&b, &state->new_outputs, &state->old_outputs, 0);
      }
   }
}

/* For fragment shader inputs, when we lower to temporaries we'll invalidate
 * interpolateAt*() because now they'll be pointing to the temporary instead
 * of the actual variable. Since the caller presumably doesn't support
 * indirect indexing of inputs, we'll need to lower something like:
 *
 * in vec4 foo[3];
 *
 * ... = interpolateAtCentroid(foo[i]);
 *
 * to a sequence of interpolations that store to our temporary, then a
 * load at the end:
 *
 * in vec4 foo[3];
 * vec4 foo_tmp[3];
 *
 * foo_tmp[0] = interpolateAtCentroid(foo[0]);
 * foo_tmp[1] = interpolateAtCentroid(foo[1]);
 * ... = foo_tmp[i];
 */

/*
 * Recursively emit the interpolation instructions. Here old_interp_deref
 * refers to foo[i], temp_deref is foo_tmp[0/1], and new_interp_deref is
 * foo[0/1].
 */

static void
emit_interp(nir_builder *b, nir_deref_instr **old_interp_deref,
            nir_deref_instr *temp_deref, nir_deref_instr *new_interp_deref,
            nir_intrinsic_instr *interp)
{
   while (*old_interp_deref) {
      switch ((*old_interp_deref)->deref_type) {
      case nir_deref_type_struct:
         temp_deref =
            nir_build_deref_struct(b, temp_deref,
                                   (*old_interp_deref)->strct.index);
         new_interp_deref =
            nir_build_deref_struct(b, new_interp_deref,
                                   (*old_interp_deref)->strct.index);
         break;
      case nir_deref_type_array:
         if (nir_src_is_const((*old_interp_deref)->arr.index)) {
            temp_deref =
               nir_build_deref_array(b, temp_deref,
                                     (*old_interp_deref)->arr.index.ssa);
            new_interp_deref =
               nir_build_deref_array(b, new_interp_deref,
                                     (*old_interp_deref)->arr.index.ssa);
            break;
         } else {
            /* We have an indirect deref, so we have to emit interpolations
             * for every index. Recurse in case we have an array of arrays.
             */
            unsigned length = glsl_get_length(temp_deref->type);
            for (unsigned i = 0; i < length; i++) {
               nir_deref_instr *new_temp =
                  nir_build_deref_array_imm(b, temp_deref, i);
               nir_deref_instr *new_interp =
                  nir_build_deref_array_imm(b, new_interp_deref, i);

               emit_interp(b, old_interp_deref + 1, new_temp, new_interp,
                           interp);
            }

            return;
         }

      case nir_deref_type_var:
      case nir_deref_type_array_wildcard:
      case nir_deref_type_ptr_as_array:
      case nir_deref_type_cast:
         UNREACHABLE("bad deref type");
      }

      old_interp_deref++;
   }

   /* Now that we've constructed a fully-qualified deref with all the indirect
    * derefs replaced with direct ones, it's time to actually emit the new
    * interpolation instruction.
    */

   nir_intrinsic_instr *new_interp =
      nir_intrinsic_instr_create(b->shader, interp->intrinsic);

   new_interp->src[0] = nir_src_for_ssa(&new_interp_deref->def);
   if (interp->intrinsic == nir_intrinsic_interp_deref_at_sample ||
       interp->intrinsic == nir_intrinsic_interp_deref_at_offset ||
       interp->intrinsic == nir_intrinsic_interp_deref_at_vertex) {
      new_interp->src[1] = interp->src[1];
   }

   new_interp->num_components = interp->num_components;
   nir_def_init(&new_interp->instr, &new_interp->def,
                interp->def.num_components, interp->def.bit_size);

   nir_builder_instr_insert(b, &new_interp->instr);
   nir_store_deref(b, temp_deref, &new_interp->def,
                   (1 << interp->def.num_components) - 1);
}

static void
fixup_interpolation_instr(struct lower_io_state *state,
                          nir_intrinsic_instr *interp, nir_builder *b)
{
   nir_deref_path interp_path;
   nir_deref_path_init(&interp_path, nir_src_as_deref(interp->src[0]), NULL);

   b->cursor = nir_before_instr(&interp->instr);

   /* The original interpolation instruction should contain a deref path
    * starting with the original variable, which is now the temporary.
    */
   nir_deref_instr *temp_root = interp_path.path[0];

   /* Fish out the newly-created input variable. */
   assert(temp_root->deref_type == nir_deref_type_var);
   struct hash_entry *entry = _mesa_hash_table_search(state->input_map,
                                                      temp_root->var);
   assert(entry);
   nir_variable *input = entry->data;
   nir_deref_instr *input_root = nir_build_deref_var(b, input);

   /* We can't reuse the original temporary because it contains the original
    * value of the input that's used throughout the whole shader to replace
    * the original load_deref, so we can't overwrite it. Create a new
    * temporary that's only used for this interp_deref_at_* opcode.
    */
   char *var_name = ralloc_asprintf(NULL, "%s-interp", input->name);
   nir_variable *var = nir_local_variable_create(b->impl, input->type, var_name);
   ralloc_free(var_name);
   nir_deref_instr *new_temp_root = nir_build_deref_var(b, var);

   /* Emit the interpolation instructions. */
   emit_interp(b, interp_path.path + 1, new_temp_root, input_root, interp);

   /* Now the temporary contains the interpolation results, and we can just
    * load from it.
    *
    * Clone the original deref chain, but use the new temporary variable.
    */
   nir_deref_instr *new_temp_leaf =
      nir_clone_deref_instr(b, var, nir_src_as_deref(interp->src[0]));
   nir_def *load = nir_load_deref(b, new_temp_leaf);
   nir_def_replace(&interp->def, load);

   nir_deref_path_finish(&interp_path);
}

static void
fixup_interpolation(struct lower_io_state *state, nir_function_impl *impl,
                    nir_builder *b)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *interp = nir_instr_as_intrinsic(instr);

         if (interp->intrinsic == nir_intrinsic_interp_deref_at_centroid ||
             interp->intrinsic == nir_intrinsic_interp_deref_at_sample ||
             interp->intrinsic == nir_intrinsic_interp_deref_at_offset ||
             interp->intrinsic == nir_intrinsic_interp_deref_at_vertex) {
            fixup_interpolation_instr(state, interp, b);
         }
      }
   }
}

static void
emit_input_copies_impl(struct lower_io_state *state, nir_function_impl *impl)
{
   if (impl == state->entrypoint) {
      nir_builder b = nir_builder_at(nir_before_impl(impl));
      emit_copies(&b, &state->old_inputs, &state->new_inputs, 0);
      if (state->shader->info.stage == MESA_SHADER_FRAGMENT)
         fixup_interpolation(state, impl, &b);
   }
}

static const char *mode_str(nir_variable_mode mode)
{
   switch (mode) {
   case nir_var_shader_in:
      return "in";
   case nir_var_mem_pixel_local_in:
      return "pls_in";
   case nir_var_shader_out:
      return "out";
   case nir_var_mem_pixel_local_out:
      return "pls_out";
   case nir_var_mem_pixel_local_inout:
      return "pls_inout";
   default:
      UNREACHABLE("Invalid mode");
   }
}

static nir_variable *
create_shadow_temp(struct lower_io_state *state, nir_variable *var)
{
   nir_variable *nvar = nir_variable_create_zeroed(state->shader);
   memcpy(nvar, var, sizeof *nvar);
   nvar->data.cannot_coalesce = true;

   /* The original is now the temporary */
   nir_variable *temp = var;

   /* Reparent the name to the new variable */
   nir_variable_steal_name(state->shader, nvar, var);

   assert(nvar->constant_initializer == NULL && nvar->pointer_initializer == NULL);

   /* Give the original a new name with @<mode>-temp appended */
   const char *mode = mode_str(temp->data.mode);
   nir_variable_set_namef(state->shader, temp, "%s@%s-temp", mode, nvar->name);
   temp->data.mode = nir_var_shader_temp;
   temp->data.read_only = false;
   temp->data.fb_fetch_output = false;
   temp->data.compact = false;

   return nvar;
}

static void
move_variables_to_list(nir_shader *shader, nir_variable_mode mode,
                       struct exec_list *dst_list)
{
   nir_foreach_variable_with_modes_safe(var, shader, mode) {
      exec_node_remove(&var->node);
      exec_list_push_tail(dst_list, &var->node);
   }
}

bool
nir_lower_io_vars_to_temporaries(nir_shader *shader, nir_function_impl *entrypoint,
                                 nir_variable_mode modes)
{
   ASSERTED const nir_variable_mode supported_modes =
      nir_var_shader_in | nir_var_shader_out | nir_var_any_pixel_local;
   struct lower_io_state state;

   assert(!(modes & ~supported_modes));

   if (shader->info.stage != MESA_SHADER_VERTEX &&
       shader->info.stage != MESA_SHADER_TESS_EVAL &&
       shader->info.stage != MESA_SHADER_GEOMETRY &&
       shader->info.stage != MESA_SHADER_FRAGMENT) {
      return nir_no_progress(entrypoint);
   }

   state.shader = shader;
   state.entrypoint = entrypoint;
   state.input_map = _mesa_pointer_hash_table_create(NULL);

   exec_list_make_empty(&state.old_inputs);
   if (modes & nir_var_shader_in)
      move_variables_to_list(shader, nir_var_shader_in, &state.old_inputs);

   exec_list_make_empty(&state.old_outputs);
   if (modes & nir_var_shader_out)
      move_variables_to_list(shader, nir_var_shader_out, &state.old_outputs);

   /* All PLS vars are fragment shader outputs by nature, even if they
    * are flagged _in (AKA read-only).
    */
   if (modes & nir_var_mem_pixel_local_in)
      move_variables_to_list(shader, nir_var_mem_pixel_local_in,
                             &state.old_outputs);
   if (modes & nir_var_mem_pixel_local_out)
      move_variables_to_list(shader, nir_var_mem_pixel_local_out,
                             &state.old_outputs);
   if (modes & nir_var_mem_pixel_local_inout)
      move_variables_to_list(shader, nir_var_mem_pixel_local_inout,
                             &state.old_outputs);

   exec_list_make_empty(&state.new_inputs);
   exec_list_make_empty(&state.new_outputs);

   /* Walk over all of the outputs turn each output into a temporary and
    * make a new variable for the actual output.
    */
   nir_foreach_variable_in_list(var, &state.old_outputs) {
      nir_variable *output = create_shadow_temp(&state, var);
      exec_list_push_tail(&state.new_outputs, &output->node);
   }

   /* and same for inputs: */
   nir_foreach_variable_in_list(var, &state.old_inputs) {
      nir_variable *input = create_shadow_temp(&state, var);
      exec_list_push_tail(&state.new_inputs, &input->node);
      _mesa_hash_table_insert(state.input_map, var, input);
   }

   nir_foreach_function_impl(impl, shader) {
      if (modes & nir_var_shader_in)
         emit_input_copies_impl(&state, impl);

      if (modes & (nir_var_shader_out | nir_var_any_pixel_local))
         emit_output_copies_impl(&state, impl);

      nir_progress(true, impl, nir_metadata_control_flow);
   }

   exec_list_append(&shader->variables, &state.old_inputs);
   exec_list_append(&shader->variables, &state.old_outputs);
   exec_list_append(&shader->variables, &state.new_inputs);
   exec_list_append(&shader->variables, &state.new_outputs);

   nir_fixup_deref_modes(shader);

   _mesa_hash_table_destroy(state.input_map, NULL);
   return true;
}
