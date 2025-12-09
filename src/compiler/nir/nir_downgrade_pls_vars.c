/*
 * Copyright 2025 Collabora Ltd
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

/*
 * Shaders might declare PLS vars as inout but might just use them as in or out
 * but not both. This pass detects those cases and adjusts the variable/deref
 * modes accordingly.
 *
 * Should be called before nir_lower_io_vars_to_temporaries(), otherwise the
 * copy_derefs will be inserted, turning unused variables into used ones.
 * Should ideally be called after DCE to make sure we don't leave PLS inout
 * variables behind.
 */

static bool
update_pls_var_mode(struct nir_builder *b, nir_intrinsic_instr *intrin, void *_)
{
   nir_deref_instr *load_deref = NULL, *store_deref = NULL;
   bool progress = false;

   if (intrin->intrinsic == nir_intrinsic_load_deref) {
      load_deref = nir_src_as_deref(intrin->src[0]);
   } else if (intrin->intrinsic == nir_intrinsic_store_deref) {
      store_deref = nir_src_as_deref(intrin->src[0]);
   } else if (intrin->intrinsic == nir_intrinsic_copy_deref) {
      store_deref = nir_src_as_deref(intrin->src[0]);
      load_deref = nir_src_as_deref(intrin->src[1]);
   } else {
      return false;
   }

   if (load_deref && !(load_deref->modes & nir_var_any_pixel_local))
      load_deref = NULL;

   if (store_deref && !(store_deref->modes & nir_var_any_pixel_local))
      store_deref = NULL;

   nir_variable *in_var =
      load_deref ? nir_deref_instr_get_variable(load_deref) : NULL;
   nir_variable *out_var =
      store_deref ? nir_deref_instr_get_variable(store_deref) : NULL;

   if (in_var) {
      if (in_var->data.mode == 0) {
         in_var->data.mode = nir_var_mem_pixel_local_in;
         progress = true;
      } else if (in_var->data.mode == nir_var_mem_pixel_local_out) {
         in_var->data.mode = nir_var_mem_pixel_local_inout;
         progress = true;
      }
   }

   if (out_var) {
      if (out_var->data.mode == 0) {
         out_var->data.mode = nir_var_mem_pixel_local_out;
         progress = true;
      } else if (out_var->data.mode == nir_var_mem_pixel_local_in) {
         out_var->data.mode = nir_var_mem_pixel_local_inout;
         progress = true;
      }
   }

   return progress;
}

static bool
propagate_pls_var_mode(struct nir_builder *b, nir_intrinsic_instr *intrin, void *_)
{
   nir_deref_instr *load_deref = NULL, *store_deref = NULL;
   bool progress = false;

   if (intrin->intrinsic == nir_intrinsic_load_deref) {
      load_deref = nir_src_as_deref(intrin->src[0]);
   } else if (intrin->intrinsic == nir_intrinsic_store_deref) {
      store_deref = nir_src_as_deref(intrin->src[0]);
   } else if (intrin->intrinsic == nir_intrinsic_copy_deref) {
      store_deref = nir_src_as_deref(intrin->src[0]);
      load_deref = nir_src_as_deref(intrin->src[1]);
   } else {
      return false;
   }

   if (load_deref && !(load_deref->modes & nir_var_any_pixel_local))
      load_deref = NULL;

   if (store_deref && !(store_deref->modes & nir_var_any_pixel_local))
      store_deref = NULL;

   nir_variable *in_var =
      load_deref ? nir_deref_instr_get_variable(load_deref) : NULL;
   nir_variable *out_var =
      store_deref ? nir_deref_instr_get_variable(store_deref) : NULL;

   if (in_var && in_var->data.mode != load_deref->modes) {
      nir_deref_path path;
      nir_deref_path_init(&path, load_deref, NULL);

      for (unsigned i = 0; path.path[i]; i++) {
         if (path.path[i]->modes == in_var->data.mode)
            break;

         path.path[i]->modes = in_var->data.mode;
      }

      nir_deref_path_finish(&path);
      progress = true;
   }

   if (out_var && out_var->data.mode != store_deref->modes) {
      nir_deref_path path;
      nir_deref_path_init(&path, store_deref, NULL);

      for (unsigned i = 0; path.path[i]; i++) {
         if (path.path[i]->modes == out_var->data.mode)
            break;

         path.path[i]->modes = out_var->data.mode;
      }

      nir_deref_path_finish(&path);
      progress = true;
   }

   return progress;
}

bool
nir_downgrade_pls_vars(nir_shader *shader)
{
   bool progress = false;

   /* First we reset the mode of PLS inout vars. */
   nir_foreach_variable_with_modes(var, shader,
                                   nir_var_mem_pixel_local_inout) {
      var->data.mode = 0;
      progress = true;
   }

   if (!progress)
      return false;

   /* Then we re-apply a mode based on the actual accesses and
    * we propagate the new mode to all PLS derefs.
    */
   progress = false;
   if (nir_shader_intrinsics_pass(shader,
                                  update_pls_var_mode,
                                  nir_metadata_all,
                                  NULL)) {
      nir_shader_intrinsics_pass(shader,
                                 propagate_pls_var_mode,
                                 nir_metadata_all,
                                 NULL);
      progress = true;
   }

   /* Get rid of the PLS vars that were unused. */
   nir_foreach_variable_in_shader_safe(var, shader) {
      if (!var->data.mode) {
         exec_node_remove(&var->node);
         progress = true;
      }
   }

   return progress;
}
