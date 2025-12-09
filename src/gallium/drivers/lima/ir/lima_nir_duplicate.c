/*
 * Copyright (c) 2025 Lima Project
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
#include "lima_ir.h"

static bool
duplicate_def_at_use(nir_builder *b, nir_def *def, bool duplicate_for_ffma)
{
   nir_def *last_dupl = NULL;
   nir_instr *last_parent_instr = NULL;

   nir_foreach_use_including_if_safe(use_src, def) {
      nir_def *dupl;

      if (!nir_src_is_if(use_src) &&
          last_parent_instr == nir_src_parent_instr(use_src)) {
         dupl = last_dupl;
      } else {
         /* if ssa use, clone for the target block
          * if 'if use', clone where it is
          */
         if (nir_src_is_if(use_src)) {
            b->cursor = nir_before_def(def);
         } else {
            b->cursor = nir_before_instr(nir_src_parent_instr(use_src));
            last_parent_instr = nir_src_parent_instr(use_src);

            if (duplicate_for_ffma &&
                last_parent_instr->type == nir_instr_type_alu &&
                nir_instr_as_alu(last_parent_instr)->op == nir_op_ffma) {
               last_parent_instr = NULL;
            }
         }

         dupl = nir_instr_def(nir_instr_clone(b->shader, nir_def_instr(def)));
         nir_def_instr(dupl)->pass_flags = 1;

         nir_builder_instr_insert(b, nir_def_instr(dupl));
      }

      nir_src_rewrite(use_src, dupl);
      last_dupl = dupl;
   }

   nir_instr_remove(nir_def_instr(def));
   return true;
}

static bool
duplicate_modifier_alu(nir_builder *b, nir_alu_instr *alu, void *unused)
{

   if (alu->op != nir_op_fneg && alu->op != nir_op_fabs)
      return false;

   if (alu->instr.pass_flags)
      return false;

   nir_intrinsic_instr *itr = nir_src_as_intrinsic(alu->src[0].src);
   if (!itr)
      return false;

   if (itr->intrinsic != nir_intrinsic_load_input &&
       itr->intrinsic != nir_intrinsic_load_uniform)
      return false;

   return duplicate_def_at_use(b, &alu->def, false);
}

/* Duplicate load inputs for every user.
 * Helps by utilizing the load input instruction slots that would
 * otherwise stay empty, and reduces register pressure. */
bool
lima_nir_duplicate_modifiers(nir_shader *shader)
{
   nir_shader_clear_pass_flags(shader);

   return nir_shader_alu_pass(shader, duplicate_modifier_alu,
                              nir_metadata_control_flow, NULL);
}

static bool
duplicate_intrinsic(nir_builder *b, nir_intrinsic_instr *itr,
                    void *param)
{
   nir_intrinsic_op op = (nir_intrinsic_op)(uintptr_t)param;

   if (itr->intrinsic != op)
      return false;

   if (itr->instr.pass_flags)
      return false;

   return duplicate_def_at_use(b, &itr->def, false);
}

/* Duplicate load uniforms for every user.
 * Helps by utilizing the load uniform instruction slots that would
 * otherwise stay empty, and reduces register pressure. */
bool
lima_nir_duplicate_load_uniforms(nir_shader *shader)
{
   nir_shader_clear_pass_flags(shader);

   return nir_shader_intrinsics_pass(shader, duplicate_intrinsic,
                                     nir_metadata_control_flow,
                                     (void *)nir_intrinsic_load_uniform);
}

/* Duplicate load inputs for every user.
 * Helps by utilizing the load input instruction slots that would
 * otherwise stay empty, and reduces register pressure. */
bool
lima_nir_duplicate_load_inputs(nir_shader *shader)
{
   nir_shader_clear_pass_flags(shader);

   return nir_shader_intrinsics_pass(shader, duplicate_intrinsic,
                                     nir_metadata_control_flow,
                                     (void *)nir_intrinsic_load_input);
}

static bool
duplicate_load_const(nir_builder *b, nir_instr *instr, void *unused)
{
   if (instr->type != nir_instr_type_load_const)
      return false;

   if (instr->pass_flags)
      return false;

   /* Always clone consts for FFMA sources as well, since it will translate
    * into 2 PPIR ops and each may need its own const. Redundant consts
    * will be dropped by PPIR later
    */
   return duplicate_def_at_use(b, nir_instr_def(instr), true);
}

/* Duplicate load consts for every user.
 * Helps by utilizing the load const instruction slots that would
 * otherwise stay empty, and reduces register pressure. */
bool
lima_nir_duplicate_load_consts(nir_shader *shader)
{
   nir_shader_clear_pass_flags(shader);

   return nir_shader_instructions_pass(shader, duplicate_load_const,
                                       nir_metadata_control_flow, NULL);
}
