/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

/* This pass moves intrinsics to the beginning of the shader. If an intrinsic
 * is non-movable, it's left as-is.
 *
 * The pass can move intrinsics, ALU, load_const, and undef to the top.
 * The last 3 instruction types are only moved to the top when their results
 * are used as sources by moved instructions. It preserves the relative order
 * of instructions that are moved.
 *
 * Used either as a scheduling optimization or to accommodate hw or compiler
 * backend limitations. You would typically use this if you don't use
 * nir_lower_io_vars_to_temporaries and want to move input loads to top,
 * but note that such global code motion passes often increase register usage.
 */

#include "nir.h"
#include "nir_builder.h"

typedef struct {
   nir_opt_move_to_top_options options;
   nir_function_impl *impl;
} opt_move_to_top_state;

#define PASS_FLAG_CAN_MOVE    BITFIELD_BIT(0)
#define PASS_FLAG_CANT_MOVE   BITFIELD_BIT(1)
#define PASS_FLAG_MOVED       BITFIELD_BIT(2)

static bool
can_move_src_to_top(nir_src *src, void *_state)
{
   opt_move_to_top_state *state = (opt_move_to_top_state *)_state;
   nir_instr *instr = nir_def_instr(src->ssa);

   assert(util_bitcount(instr->pass_flags & (PASS_FLAG_CANT_MOVE |
                                             PASS_FLAG_CAN_MOVE)) <= 1);

   if (instr->pass_flags & PASS_FLAG_CANT_MOVE)
      return false;
   if (instr->pass_flags & PASS_FLAG_CAN_MOVE)
      return true;

   /* If the instruction is already in the entry block, there is nothing to do. */
   if (state->options & nir_move_to_entry_block_only &&
       instr->block == nir_start_block(state->impl)) {
      /* Mark as already moved. */
      instr->pass_flags |= PASS_FLAG_CAN_MOVE | PASS_FLAG_MOVED;
      return true;
   }

   if (instr->type != nir_instr_type_alu &&
       instr->type != nir_instr_type_intrinsic &&
       instr->type != nir_instr_type_load_const &&
       instr->type != nir_instr_type_undef) {
      instr->pass_flags |= PASS_FLAG_CANT_MOVE;
      return false;
   }

   if (instr->type == nir_instr_type_intrinsic) {
      /* Only these intrinsics are movable to the top. */
      switch (nir_instr_as_intrinsic(instr)->intrinsic) {
      /* Input loads and its sources. */
      case nir_intrinsic_load_barycentric_pixel:
      case nir_intrinsic_load_barycentric_centroid:
      case nir_intrinsic_load_barycentric_sample:
      case nir_intrinsic_load_barycentric_at_offset:
      case nir_intrinsic_load_barycentric_at_sample:
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_interpolated_input:
      case nir_intrinsic_load_per_primitive_input:
      case nir_intrinsic_load_per_vertex_input:
      case nir_intrinsic_load_scalar_arg_amd:
         break;
      case nir_intrinsic_load_global_amd:
         if (!(nir_intrinsic_access(nir_instr_as_intrinsic(instr)) &
               ACCESS_SMEM_AMD)) {
            instr->pass_flags |= PASS_FLAG_CANT_MOVE;
            return false;
         }
         break;
      default:
         instr->pass_flags |= PASS_FLAG_CANT_MOVE;
         return false;
      }

      if (instr->block == nir_start_block(state->impl) &&
          !nir_intrinsic_can_reorder(nir_instr_as_intrinsic(instr))) {
         instr->pass_flags |= PASS_FLAG_CANT_MOVE;
         return false;
      }
   }

   if (instr->block != nir_start_block(state->impl) &&
       !nir_instr_can_speculate(instr)) {
      instr->pass_flags |= PASS_FLAG_CANT_MOVE;
      return false;
   }

   if (!nir_foreach_src(instr, can_move_src_to_top, state)) {
      instr->pass_flags |= PASS_FLAG_CANT_MOVE;
      return false;
   }

   instr->pass_flags |= PASS_FLAG_CAN_MOVE;
   return true;
}

static bool
move_src(nir_src *src, void *_state)
{
   nir_instr *instr = nir_def_instr(src->ssa);
   nir_builder *b = (nir_builder *)_state;

   if (instr->pass_flags & PASS_FLAG_MOVED)
      return true; /* already moved */

   nir_foreach_src(instr, move_src, b);
   nir_instr_move(b->cursor, instr);
   b->cursor = nir_after_instr(instr);
   instr->pass_flags |= PASS_FLAG_MOVED;
   return true;
}

static bool
handle_load(nir_builder *b, nir_intrinsic_instr *intr, void *_state)
{
   opt_move_to_top_state *state = (opt_move_to_top_state *)_state;
   bool move = false;

   if (state->options & nir_move_to_entry_block_only &&
       intr->instr.block == nir_start_block(b->impl))
        return false;

   /* If an intrinsic has a destination and it has IO semantics, it's
    * an input load. The specific intrinsics that are moved are
    * listed in can_move_src_to_top.
    */
   move |= state->options & nir_move_to_top_input_loads &&
           nir_intrinsic_has_io_semantics(intr) &&
           nir_intrinsic_infos[intr->intrinsic].has_dest &&
           !nir_is_output_load(intr);

   move |= state->options & nir_move_to_top_load_smem_amd &&
           (intr->intrinsic == nir_intrinsic_load_global_amd &&
            nir_intrinsic_access(intr) & ACCESS_SMEM_AMD);

   if (!move)
      return false;

   nir_src intr_as_src = nir_src_for_ssa(&intr->def);

   /* Initialize the cursor only once per function. */
   if (state->impl != b->impl) {
      if (state->options & nir_move_to_entry_block_only)
         b->cursor = nir_after_block(nir_start_block(b->impl));
      else
         b->cursor = nir_before_impl(b->impl);
      state->impl = b->impl;
   }

   if (!can_move_src_to_top(&intr_as_src, state))
      return false;

   move_src(&intr_as_src, b);
   return true;
}

bool
nir_opt_move_to_top(nir_shader *nir, nir_opt_move_to_top_options options)
{
   nir_shader_clear_pass_flags(nir);
   opt_move_to_top_state state = {options};
   return nir_shader_intrinsics_pass(nir, handle_load, nir_metadata_none,
                                     &state);
}
