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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nir.h"
#include "shader_enums.h"

enum var_flags {
   var_all_invariant = 1 << 0,
   var_all_alias_invariant = 1 << 1,
   var_any_invariant = 1 << 2,
};

static void
add_src(nir_src *src, struct set *invariants)
{
   _mesa_set_add(invariants, src->ssa);
}

static bool
add_src_cb(nir_src *src, void *state)
{
   add_src(src, state);
   return true;
}

static bool
def_is_invariant(nir_def *def, struct set *invariants)
{
   return _mesa_set_search(invariants, def);
}

static void
add_cf_node(nir_cf_node *cf, struct set *invariants)
{
   if (cf->type == nir_cf_node_if) {
      nir_if *if_stmt = nir_cf_node_as_if(cf);
      add_src(&if_stmt->condition, invariants);
   }

   if (cf->parent)
      add_cf_node(cf->parent, invariants);
}

static bool
var_may_alias(nir_variable *var)
{
   if (var->data.mode == nir_var_mem_ssbo)
      return !(var->data.access & ACCESS_RESTRICT);
   else if (var->data.mode == nir_var_mem_shared)
      return var->data.aliased_shared_memory;
   return false;
}

static void
add_var(const nir_deref_instr *deref, struct set *invariants, uint8_t *var_invariant)
{
   /* It's possible for var to be NULL if, for instance, there's a cast
    * somewhere in the chain.
    */
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned modes = var ? var->data.mode : deref->modes;

   uint8_t flags = 0;
   if (var && !(var->data.access & ACCESS_NON_WRITEABLE)) {
      _mesa_set_add(invariants, var);

      flags = var_any_invariant;
      if (var_may_alias(var))
         flags |= var_all_alias_invariant;
   } else if (!var) {
      flags = var_any_invariant | var_all_invariant | var_all_alias_invariant;
   }
   u_foreach_bit(i, modes)
      var_invariant[i] |= flags;
}

static bool
var_is_invariant(const nir_deref_instr *deref, struct set *invariants, uint8_t *var_invariant)
{
   /* It's possible for var to be NULL if, for instance, there's a cast
    * somewhere in the chain.
    */
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned modes = var ? var->data.mode : deref->modes;

   unsigned global_idx = ffs(nir_var_mem_global) - 1;
   unsigned ssbo_idx = ffs(nir_var_mem_ssbo) - 1;
   uint8_t flags = 0;
   u_foreach_bit(i, modes) {
      flags |= var_invariant[i];
      if (i == ssbo_idx || i == global_idx)
         flags |= var_invariant[i == ssbo_idx ? global_idx : ssbo_idx];
   }
   if (flags & var_all_invariant)
      return true;

   if (var) {
      if (var_may_alias(var) && (flags & var_all_alias_invariant))
         return true;
      return var->data.invariant || _mesa_set_search(invariants, var);
   } else {
      return flags & var_any_invariant;
   }
}

static bool
is_image(nir_intrinsic_op intrin, bool read)
{
   switch (intrin) {
#define CASE(op)                           \
   case nir_intrinsic_image_deref_##op:    \
   case nir_intrinsic_image_##op:          \
   case nir_intrinsic_bindless_image_##op: \
   case nir_intrinsic_image_heap_##op:
      CASE(load)
      CASE(sparse_load)
      CASE(samples_identical)
      return read;
      CASE(store)
      return !read;
      CASE(atomic)
      CASE(atomic_swap)
      return true;
#undef CASE
   default:
      return false;
   }
}

static void
propagate_invariant_instr(nir_instr *instr, struct set *invariants, uint8_t *var_invariant)
{
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!def_is_invariant(&alu->def, invariants))
         break;

      alu->fp_math_ctrl |= nir_op_valid_fp_math_ctrl(alu->op, nir_fp_exact);
      nir_foreach_src(instr, add_src_cb, invariants);
      break;
   }

   case nir_instr_type_tex:
   case nir_instr_type_deref: {
      if (def_is_invariant(nir_instr_def(instr), invariants))
         nir_foreach_src(instr, add_src_cb, invariants);
      break;
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_copy_deref:
         /* If the destination is invariant then so is the source */
         if (var_is_invariant(nir_src_as_deref(intrin->src[0]), invariants, var_invariant))
            add_var(nir_src_as_deref(intrin->src[1]), invariants, var_invariant);
         break;

      case nir_intrinsic_load_deref:
         if (def_is_invariant(&intrin->def, invariants))
            add_var(nir_src_as_deref(intrin->src[0]), invariants, var_invariant);
         break;

      case nir_intrinsic_store_deref:
         if (var_is_invariant(nir_src_as_deref(intrin->src[0]), invariants, var_invariant))
            add_src(&intrin->src[1], invariants);
         break;

      default:
         /* Nothing to do */
         break;
      }

      if (is_image(intrin->intrinsic, false)) {
         uint8_t img_flags = 0, buf_flags = 0;
         if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_BUF) {
            buf_flags |= var_invariant[ffs(nir_var_mem_ssbo) - 1] |
                         var_invariant[ffs(nir_var_mem_global) - 1];
         } else {
            img_flags |= var_invariant[ffs(nir_var_image) - 1];
         }

         if ((img_flags & var_any_invariant) ||
             (buf_flags & (var_all_invariant | var_all_alias_invariant))) {
            add_src(&intrin->src[3], invariants);
            if (nir_intrinsic_infos[intrin->intrinsic].num_srcs > 4)
               add_src(&intrin->src[4], invariants);
         }
      }

      if (nir_intrinsic_infos[intrin->intrinsic].has_dest &&
          def_is_invariant(&intrin->def, invariants)) {
         if (nir_intrinsic_has_fp_math_ctrl(intrin)) {
            unsigned ctrl = nir_intrinsic_fp_math_ctrl(intrin) | nir_fp_exact;
            nir_intrinsic_set_fp_math_ctrl(intrin, ctrl);
         }

         nir_foreach_src(instr, add_src_cb, invariants);

         if (is_image(intrin->intrinsic, true)) {
            uint8_t img_flags = var_any_invariant | var_all_invariant | var_all_alias_invariant;
            uint8_t buf_flags = var_any_invariant | var_all_alias_invariant;
            if (nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_BUF)
               var_invariant[ffs(nir_var_mem_ssbo) - 1] |= buf_flags;
            else
               var_invariant[ffs(nir_var_image) - 1] |= img_flags;
         }
      }
      break;
   }

   case nir_instr_type_jump:
   case nir_instr_type_undef:
   case nir_instr_type_load_const:
   case nir_instr_type_cmat_call:
      break; /* Nothing to do */

   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      if (!def_is_invariant(&phi->def, invariants))
         break;

      nir_foreach_phi_src(src, phi) {
         add_src(&src->src, invariants);
         add_cf_node(&src->pred->cf_node, invariants);
      }
      break;
   }

   case nir_instr_type_call:
      UNREACHABLE("This pass must be run after function inlining");

   default:
      UNREACHABLE("Cannot have this instruction type");
   }
}

static bool
propagate_invariant_impl(nir_function_impl *impl, struct set *invariants)
{
   bool progress = false;

   uint8_t var_invariant[nir_num_variable_modes] = { 0 };

   nir_foreach_variable_in_shader(var, impl->function->shader) {
      if (var->data.invariant)
         var_invariant[ffs(var->data.mode) - 1] |= var_any_invariant;
   }

   nir_foreach_function_temp_variable(var, impl) {
      if (var->data.invariant)
         var_invariant[ffs(var->data.mode) - 1] |= var_any_invariant;
   }

   while (true) {
      uint32_t prev_entries = invariants->entries;
      uint8_t prev_var_invariant[nir_num_variable_modes];
      memcpy(prev_var_invariant, var_invariant, sizeof(var_invariant));

      nir_foreach_block_reverse(block, impl) {
         nir_foreach_instr_reverse(instr, block)
            propagate_invariant_instr(instr, invariants, var_invariant);
      }

      /* Keep running until we make no more progress. */
      if (invariants->entries > prev_entries ||
          memcmp(prev_var_invariant, var_invariant, sizeof(var_invariant))) {
         progress = true;
         continue;
      } else {
         break;
      }
   }

   return nir_progress(progress, impl,
                       nir_metadata_control_flow | nir_metadata_live_defs);
}

/* If invariant_prim=true, this pass considers all geometry-affecting
 * outputs as invariant. Doing this works around a common class of application
 * bugs appearing as flickering.
 */
bool
nir_propagate_invariant(nir_shader *shader, bool invariant_prim)
{
   /* Hash set of invariant things */
   struct set *invariants = _mesa_pointer_set_create(NULL);

   if (shader->info.stage != MESA_SHADER_FRAGMENT && invariant_prim) {
      nir_foreach_shader_out_variable(var, shader) {
         switch (var->data.location) {
         case VARYING_SLOT_POS:
         case VARYING_SLOT_PSIZ:
         case VARYING_SLOT_CLIP_DIST0:
         case VARYING_SLOT_CLIP_DIST1:
         case VARYING_SLOT_CULL_DIST0:
         case VARYING_SLOT_CULL_DIST1:
         case VARYING_SLOT_TESS_LEVEL_OUTER:
         case VARYING_SLOT_TESS_LEVEL_INNER:
            if (!var->data.invariant)
               _mesa_set_add(invariants, var);
            break;
         default:
            break;
         }
      }
   }

   bool progress = false;
   nir_foreach_function_impl(impl, shader) {
      if (propagate_invariant_impl(impl, invariants))
         progress = true;
   }

   _mesa_set_destroy(invariants, NULL);

   return progress;
}
