/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "compiler/nir/nir_builder.h"
#include "util/set.h"
#include "nir.h"

/**
 * Moves terminate{_if} intrinsics out of loops.
 *
 * This lowering turns:
 *
 *     loop {
 *        ...
 *        terminate_if(cond);
 *        ...
 *     }
 *
 * into:
 *
 *     reg = false
 *     loop {
 *        ...
 *        if (cond) {
 *           reg = true;
 *           break;
 *        }
 *        ...
 *     }
 *     terminate_if(reg);
 */
static bool
move_out_of_loop(nir_builder *b, nir_intrinsic_instr *instr)
{
   nir_cf_node *node = instr->instr.block->cf_node.parent;
   while (node && node->type != nir_cf_node_loop)
      node = node->parent;

   if (node == NULL)
      return false;

   /* Lower the loop to LCSSA form, so that we don't break SSA. */
   nir_convert_loop_to_lcssa(nir_cf_node_as_loop(node));

   /* Create phi instruction for the terminate condition. */
   nir_phi_instr *phi_instr = nir_phi_instr_create(b->shader);
   nir_def_init(&phi_instr->instr, &phi_instr->def, 1, 1);

   /* Set phi-src to 'false' for existing break conditions. */
   b->cursor = nir_before_cf_node(node);
   nir_def *false_val = nir_imm_false(b);
   nir_block *after_loop = nir_cf_node_cf_tree_next(node);
   set_foreach(&after_loop->predecessors, entry) {
      nir_phi_instr_add_src(phi_instr, (nir_block *)entry->key, false_val);
   }

   /* Break if terminate. */
   b->cursor = nir_instr_remove(&instr->instr);
   nir_def *cond = instr->intrinsic == nir_intrinsic_terminate_if
                      ? instr->src[0].ssa
                      : nir_imm_true(b);
   nir_push_if(b, cond);

   nir_jump(b, nir_jump_break);
   nir_block *break_block = nir_cursor_current_block(b->cursor);
   nir_pop_if(b, NULL);

   /* Add undef for existing phis and terminate condition for the new phi. */
   nir_insert_phi_undef(after_loop, break_block);
   nir_phi_instr_add_src(phi_instr, break_block, cond);

   /* Insert phi and new terminate instruction. */
   b->cursor = nir_after_phis(after_loop);
   nir_builder_instr_insert(b, &phi_instr->instr);
   nir_terminate_if(b, &phi_instr->def);

   return true;
}

static bool
lower_discard_if(nir_builder *b, nir_intrinsic_instr *instr, void *cb_data)
{
   nir_lower_discard_if_options options = *(nir_lower_discard_if_options *)cb_data;

   switch (instr->intrinsic) {
   case nir_intrinsic_demote_if:
      if (!(options & nir_lower_demote_if_to_cf))
         return false;
      break;
   case nir_intrinsic_terminate:
      return (options & nir_move_terminate_out_of_loops) &&
             move_out_of_loop(b, instr);
   case nir_intrinsic_terminate_if:
      if ((options & nir_move_terminate_out_of_loops) &&
          move_out_of_loop(b, instr))
         return true;
      if (!(options & nir_lower_terminate_if_to_cf))
         return false;
      break;
   default:
      return false;
   }

   b->cursor = nir_before_instr(&instr->instr);

   nir_if *if_stmt = nir_push_if(b, instr->src[0].ssa);
   switch (instr->intrinsic) {
   case nir_intrinsic_demote_if:
      nir_demote(b);
      break;
   case nir_intrinsic_terminate_if:
      nir_terminate(b);
      break;
   default:
      UNREACHABLE("bad intrinsic");
   }
   nir_pop_if(b, if_stmt);
   nir_instr_remove(&instr->instr);
   return true;

   /* a shader like this (shaders@glsl-fs-discard-04):

      uniform int j, k;

      void main()
      {
       for (int i = 0; i < j; i++) {
        if (i > k)
         continue;
        discard;
       }
       gl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);
      }



      will generate nir like:

      loop   {
         //snip
         if   ssa_11   {
            block   block_5:
            /   preds:   block_4   /
            vec1   32   ssa_17   =   iadd   ssa_50,   ssa_31
            /   succs:   block_7   /
         }   else   {
            block   block_6:
            /   preds:   block_4   /
            intrinsic   terminate   ()   () <-- not last instruction
            vec1   32   ssa_23   =   iadd   ssa_50,   ssa_31 <-- dead code loop itr increment
            /   succs:   block_7   /
         }
         //snip
      }

      which means that we can't assert like this:

      assert(instr->intrinsic != nir_intrinsic_terminate ||
             nir_block_last_instr(instr->instr.block) == &instr->instr);


      and it's unnecessary anyway since later optimizations will dce the
      instructions following the discard
    */

   return false;
}

bool
nir_lower_discard_if(nir_shader *shader, nir_lower_discard_if_options options)
{
   return nir_shader_intrinsics_pass(shader,
                                     lower_discard_if,
                                     nir_metadata_none,
                                     &options);
}
