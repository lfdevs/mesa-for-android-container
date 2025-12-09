/*
 * Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include "nir.h"
#include "nir_tcs_info.h"
#include "nir_search_helpers.h"

static unsigned
get_tess_level_component(nir_intrinsic_instr *intr)
{
   unsigned location = nir_intrinsic_io_semantics(intr).location;

   return (location == VARYING_SLOT_TESS_LEVEL_INNER ? 4 : 0) +
          nir_intrinsic_component(intr);
}

static bool
is_tcs_output_barrier(nir_intrinsic_instr *intr)
{
   return intr->intrinsic == nir_intrinsic_barrier &&
          nir_intrinsic_memory_modes(intr) & nir_var_shader_out &&
          nir_intrinsic_memory_scope(intr) >= SCOPE_WORKGROUP &&
          nir_intrinsic_execution_scope(intr) >= SCOPE_WORKGROUP;
}

/* 32 patch outputs + 2 tess level outputs with 8 channels per output.
 * The last 4 channels are for high 16 bits of the first 4 channels.
 */
#define NUM_OUTPUTS     34
#define NUM_BITS    (NUM_OUTPUTS * 8)

struct writemasks {
   BITSET_DECLARE(chan_mask, NUM_BITS);
};

static unsigned
get_io_index(nir_io_semantics sem)
{
   return sem.location >= VARYING_SLOT_PATCH0 ?
            sem.location - VARYING_SLOT_PATCH0 :
            (32 + sem.location - VARYING_SLOT_TESS_LEVEL_OUTER);
}

static void
accum_result_defined_by_all_invocs(struct writemasks *outer_block_writemasks,
                                   struct writemasks *cond_block_writemasks,
                                   uint64_t *result_mask)
{
   struct writemasks tmp;

   /* tmp contains those channels that are only written conditionally.
    * Such channels can't be proven to be written by all invocations.
    *
    * tmp = cond_block_writemasks & ~outer_block_writemasks
    */
   BITSET_COPY(tmp.chan_mask, outer_block_writemasks->chan_mask);
   BITSET_NOT(tmp.chan_mask);
   BITSET_AND(tmp.chan_mask,
              cond_block_writemasks->chan_mask, tmp.chan_mask);

   /* Mark outputs as not written by all invocations if they are written
    * conditionally.
    */
   unsigned i;
   BITSET_FOREACH_SET(i, tmp.chan_mask, NUM_BITS) {
      *result_mask &= ~BITFIELD64_BIT(i / 8);
   }
}

static void
scan_cf_list_defined_by_all_invocs(struct exec_list *cf_list,
                                   struct writemasks *outer_block_writemasks,
                                   struct writemasks *cond_block_writemasks,
                                   uint64_t *result_mask, bool is_nested_cf)
{
   foreach_list_typed(nir_cf_node, cf_node, node, cf_list) {
      switch (cf_node->type) {
      case nir_cf_node_block:
         nir_foreach_instr(instr, nir_cf_node_as_block(cf_node)) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            if (is_tcs_output_barrier(intrin)) {
               /* This is a barrier. If it's in nested control flow, put this
                * in the too hard basket. In GLSL this is not possible but it is
                * in SPIR-V.
                */
               if (is_nested_cf) {
                  *result_mask = 0;
                  return;
               }

               /* The following case must be prevented:
                *    gl_TessLevelInner = ...;
                *    barrier();
                *    if (gl_InvocationID == 1)
                *       gl_TessLevelInner = ...;
                *
                * If you consider disjoint code segments separated by barriers,
                * each such segment that writes patch output channels should write
                * the same channels in all codepaths within that segment.
                */
               if (!BITSET_IS_EMPTY(outer_block_writemasks->chan_mask) ||
                   !BITSET_IS_EMPTY(cond_block_writemasks->chan_mask)) {
                  accum_result_defined_by_all_invocs(outer_block_writemasks,
                                                     cond_block_writemasks,
                                                     result_mask);

                  /* Analyze the next code segment from scratch. */
                  BITSET_ZERO(outer_block_writemasks->chan_mask);
                  BITSET_ZERO(cond_block_writemasks->chan_mask);
               }
               continue;
            }

            if (intrin->intrinsic == nir_intrinsic_store_output) {
               nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);

               if (sem.location == VARYING_SLOT_TESS_LEVEL_OUTER ||
                   sem.location == VARYING_SLOT_TESS_LEVEL_INNER ||
                   (sem.location >= VARYING_SLOT_PATCH0 &&
                    sem.location <= VARYING_SLOT_PATCH31)) {
                  unsigned index = get_io_index(sem);
                  unsigned writemask = nir_intrinsic_write_mask(intrin) <<
                                       (nir_intrinsic_component(intrin) +
                                        sem.high_16bits * 4);

                  u_foreach_bit(i, writemask) {
                     BITSET_SET(outer_block_writemasks->chan_mask, index * 8 + i);
                  }
               }
            }
         }
         break;

      case nir_cf_node_if: {
         struct writemasks then_writemasks = {0};
         struct writemasks else_writemasks = {0};
         nir_if *if_stmt = nir_cf_node_as_if(cf_node);

         scan_cf_list_defined_by_all_invocs(&if_stmt->then_list, &then_writemasks,
                                            cond_block_writemasks, result_mask,
                                            true);

         scan_cf_list_defined_by_all_invocs(&if_stmt->else_list, &else_writemasks,
                                            cond_block_writemasks, result_mask,
                                            true);

         if (!BITSET_IS_EMPTY(then_writemasks.chan_mask) ||
             !BITSET_IS_EMPTY(else_writemasks.chan_mask)) {
            /* If both statements write the same tess level channels,
             * we can say that the outer block writes them too.
             */
            struct writemasks tmp;

            /* outer_block_writemasks |= then_writemasks & else_writemasks */
            BITSET_AND(tmp.chan_mask,
                       then_writemasks.chan_mask, else_writemasks.chan_mask);
            BITSET_OR(outer_block_writemasks->chan_mask,
                      outer_block_writemasks->chan_mask, tmp.chan_mask);

            /* cond_block_writemasks |= then_writemasks | else_writemasks */
            BITSET_OR(tmp.chan_mask,
                      then_writemasks.chan_mask, else_writemasks.chan_mask);
            BITSET_OR(cond_block_writemasks->chan_mask,
                      cond_block_writemasks->chan_mask, tmp.chan_mask);
         }
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(cf_node);
         assert(!nir_loop_has_continue_construct(loop));

         scan_cf_list_defined_by_all_invocs(&loop->body, cond_block_writemasks,
                                            cond_block_writemasks, result_mask,
                                            true);
         break;
      }
      default:
         UNREACHABLE("unknown cf node type");
      }
   }
}

static void
scan_cf_list_for_invoc0(struct exec_list *cf_list,
                        struct writemasks *written_by_invoc0,
                        struct writemasks *read_by_invoc0,
                        struct writemasks *written_by_unknown_invoc,
                        struct writemasks *read_by_unknown_invoc,
                        bool is_inside_invoc0)
{
   foreach_list_typed(nir_cf_node, cf_node, node, cf_list) {
      switch (cf_node->type) {
      case nir_cf_node_block:
         nir_foreach_instr(instr, nir_cf_node_as_block(cf_node)) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            if (!nir_intrinsic_has_io_semantics(intrin))
               continue;

            nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
            if (!(sem.location == VARYING_SLOT_TESS_LEVEL_OUTER ||
                  sem.location == VARYING_SLOT_TESS_LEVEL_INNER ||
                  (sem.location >= VARYING_SLOT_PATCH0 &&
                   sem.location <= VARYING_SLOT_PATCH31)))
               continue;

            struct writemasks *masks = NULL;
            unsigned mask = 0;

            if (intrin->intrinsic == nir_intrinsic_store_output) {
               masks = is_inside_invoc0 ?
                          written_by_invoc0 : written_by_unknown_invoc;
               mask = nir_intrinsic_write_mask(intrin) <<
                      (nir_intrinsic_component(intrin) + sem.high_16bits * 4);
            } else if (intrin->intrinsic == nir_intrinsic_load_output) {
               masks = is_inside_invoc0 ? read_by_invoc0 : read_by_unknown_invoc;
               mask = nir_def_components_read(&intrin->def) <<
                      (nir_intrinsic_component(intrin) + sem.high_16bits * 4);
            } else {
               continue;
            }

            unsigned index = get_io_index(sem);

            u_foreach_bit(i, mask) {
               BITSET_SET(masks->chan_mask, index * 8 + i);
            }
         }
         break;

      case nir_cf_node_if: {
         nir_if *if_stmt = nir_cf_node_as_if(cf_node);
         nir_scalar cond = nir_scalar_resolved(if_stmt->condition.ssa, 0);
         bool then_is_invoc0 = is_inside_invoc0;

         if (!is_inside_invoc0 && nir_scalar_is_alu(cond)) {
            nir_op op = nir_scalar_alu_op(cond);

            if (op == nir_op_ieq) {
               nir_scalar src[] = { nir_scalar_chase_movs(nir_scalar_chase_alu_src(cond, 0)),
                                    nir_scalar_chase_movs(nir_scalar_chase_alu_src(cond, 1)) };
               for (unsigned i = 0; i < 2; i++) {
                  if (nir_scalar_is_const(src[i]) && nir_scalar_is_intrinsic(src[!i]) &&
                      nir_scalar_as_uint(src[i]) == 0 &&
                      nir_scalar_intrinsic_op(src[!i]) == nir_intrinsic_load_invocation_id) {
                     then_is_invoc0 = true;
                  }
               }
            } else if (op == nir_op_ilt || op == nir_op_ult) {
               nir_scalar src0 = nir_scalar_chase_movs(nir_scalar_chase_alu_src(cond, 0));
               nir_scalar src1 = nir_scalar_chase_movs(nir_scalar_chase_alu_src(cond, 1));
               if (nir_scalar_is_const(src1) && nir_scalar_is_intrinsic(src0) &&
                   nir_scalar_as_uint(src1) == 1 &&
                   nir_scalar_intrinsic_op(src0) == nir_intrinsic_load_invocation_id) {
                  then_is_invoc0 = true;
               }
            }
         }

         scan_cf_list_for_invoc0(&if_stmt->then_list, written_by_invoc0,
                                 read_by_invoc0, written_by_unknown_invoc,
                                 read_by_unknown_invoc, then_is_invoc0);

         scan_cf_list_for_invoc0(&if_stmt->else_list, written_by_invoc0,
                                 read_by_invoc0, written_by_unknown_invoc,
                                 read_by_unknown_invoc, is_inside_invoc0);
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(cf_node);
         assert(!nir_loop_has_continue_construct(loop));

         scan_cf_list_for_invoc0(&loop->body, written_by_invoc0,
                                 read_by_invoc0, written_by_unknown_invoc,
                                 read_by_unknown_invoc, is_inside_invoc0);
         break;
      }
      default:
         UNREACHABLE("unknown cf node type");
      }
   }
}

static void
analyze_patch_outputs(const struct nir_shader *nir, nir_tcs_info *info)
{
   assert(nir->info.stage == MESA_SHADER_TESS_CTRL);
   unsigned tess_levels_written =
      (nir->info.outputs_written & VARYING_BIT_TESS_LEVEL_OUTER ? 0x1 : 0) |
      (nir->info.outputs_written & VARYING_BIT_TESS_LEVEL_INNER ? 0x2 : 0);
   unsigned tess_levels_read =
      (nir->info.outputs_read & VARYING_BIT_TESS_LEVEL_OUTER ? 0x1 : 0) |
      (nir->info.outputs_read & VARYING_BIT_TESS_LEVEL_INNER ? 0x2 : 0);

   /* Trivial case, nothing to do. */
   if (nir->info.tess.tcs_vertices_out == 1) {
      info->patch_outputs_defined_by_all_invoc = nir->info.patch_outputs_written;
      info->patch_outputs_only_written_by_invoc0 = nir->info.patch_outputs_written;
      info->patch_outputs_only_read_by_invoc0 = nir->info.patch_outputs_read;
      info->all_invocations_define_tess_levels = true;
      info->tess_levels_defined_by_all_invoc = tess_levels_written;
      info->tess_levels_only_written_by_invoc0 = tess_levels_written;
      info->tess_levels_only_read_by_invoc0 = tess_levels_read;
      return;
   }

   /* The first part of this analysis determines patch_outputs_defined_by_all_invoc.
    * It works as follows:
    *
    * If all codepaths write patch outputs, we can say that all invocations
    * define patch output values. Whether a patch output value is defined is
    * determined for each component separately.
    */
   struct writemasks main_block_writemasks = {0}; /* if main block writes per-patch outputs */
   struct writemasks cond_block_writemasks = {0}; /* if cond block writes per-patch outputs */

   /* Initial value = true. Here the pass will accumulate results from
    * multiple segments surrounded by barriers. If patch outputs aren't
    * written at all, it's a shader bug and we don't care if this will be
    * true.
    */
   uint64_t result_mask = BITFIELD64_MASK(NUM_OUTPUTS);

   nir_foreach_function_impl(impl, nir) {
      scan_cf_list_defined_by_all_invocs(&impl->body, &main_block_writemasks,
                                         &cond_block_writemasks, &result_mask,
                                         false);
   }

   /* Accumulate the result for the last code segment separated by a
    * barrier.
    */
   if (!BITSET_IS_EMPTY(main_block_writemasks.chan_mask) ||
       !BITSET_IS_EMPTY(cond_block_writemasks.chan_mask)) {
      accum_result_defined_by_all_invocs(&main_block_writemasks,
                                         &cond_block_writemasks, &result_mask);
   }

   /* Unwritten outputs are always set. Only channels that are set
    * conditionally aren't set.
    */
   info->patch_outputs_defined_by_all_invoc =
      result_mask & nir->info.patch_outputs_written;
   info->tess_levels_defined_by_all_invoc =
      (result_mask >> 32) & tess_levels_written;
   info->all_invocations_define_tess_levels =
      info->tess_levels_defined_by_all_invoc == tess_levels_written;

   /* The second part of this analysis determines patch_outputs_written/read_by_invoc0.
    * It works as follows:
    *
    * If we are inside a conditional block with the condition "invocation_id == 0",
    * mark those patch output components that are stored inside it as being
    * defined by invocation 0. Same for reads. If we are not inside such
    * conditional blocks, mark those patch outputs components as being written
    * or read by unknown invocations.
    */
   struct writemasks written_by_invoc0 = {0};
   struct writemasks read_by_invoc0 = {0};
   struct writemasks written_by_unknown_invoc = {0};
   struct writemasks read_by_unknown_invoc = {0};

   nir_foreach_function_impl(impl, nir) {
      scan_cf_list_for_invoc0(&impl->body, &written_by_invoc0, &read_by_invoc0,
                              &written_by_unknown_invoc, &read_by_unknown_invoc,
                              false);
   }

   /* Reduce the masks to 1 bit per output. */
   for (unsigned i = 0; i < NUM_OUTPUTS; i++) {
      if (BITSET_GET_RANGE_INSIDE_WORD(written_by_invoc0.chan_mask, i * 8, i * 8 + 7) &&
          !BITSET_GET_RANGE_INSIDE_WORD(written_by_unknown_invoc.chan_mask, i * 8, i * 8 + 7)) {
         if (i < 32)
            info->patch_outputs_only_written_by_invoc0 |= BITFIELD_BIT(i);
         else
            info->tess_levels_only_written_by_invoc0 |= BITFIELD_BIT(i - 32);
      }

      if (BITSET_GET_RANGE_INSIDE_WORD(read_by_invoc0.chan_mask, i * 8, i * 8 + 7) &&
          !BITSET_GET_RANGE_INSIDE_WORD(read_by_unknown_invoc.chan_mask, i * 8, i * 8 + 7)) {
         if (i < 32)
            info->patch_outputs_only_read_by_invoc0 |= BITFIELD_BIT(i);
         else
            info->tess_levels_only_read_by_invoc0 |= BITFIELD_BIT(i - 32);
      }
   }
}

/* It's OK to pass UNSPECIFIED to prim and spacing. */
void
nir_gather_tcs_info(const nir_shader *nir, nir_tcs_info *info,
                    enum tess_primitive_mode prim,
                    enum gl_tess_spacing spacing)
{
   memset(info, 0, sizeof(*info));
   analyze_patch_outputs(nir, info);

   unsigned tess_level_writes_le_zero = 0;
   unsigned tess_level_writes_le_one = 0;
   unsigned tess_level_writes_le_two = 0;
   unsigned tess_level_writes_gt_two = 0;

   struct hash_table *range_ht = _mesa_pointer_hash_table_create(NULL);

   /* Gather barriers and which values are written to tess level outputs. */
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            if (is_tcs_output_barrier(intr)) {
               /* Only gather barriers outside control flow. */
               if (block->cf_node.parent->type == nir_cf_node_function)
                  info->always_executes_barrier = true;
               continue;
            }

            if (intr->intrinsic != nir_intrinsic_store_output)
               continue;

            unsigned location = nir_intrinsic_io_semantics(intr).location;
            if (location != VARYING_SLOT_TESS_LEVEL_OUTER &&
                location != VARYING_SLOT_TESS_LEVEL_INNER)
               continue;

            unsigned base_shift = get_tess_level_component(intr);
            unsigned writemask = nir_intrinsic_write_mask(intr);

            u_foreach_bit(i, writemask) {
               nir_scalar scalar = nir_scalar_resolved(intr->src[0].ssa, i);
               unsigned shift = base_shift + i;

               if (nir_scalar_is_const(scalar)) {
                  float f = nir_scalar_as_float(scalar);

                  if (f <= 0 || isnan(f))
                     tess_level_writes_le_zero |= BITFIELD_BIT(shift);
                  else if (f <= 1)
                     tess_level_writes_le_one |= BITFIELD_BIT(shift);
                  else if (f <= 2)
                     tess_level_writes_le_two |= BITFIELD_BIT(shift);
                  else
                     tess_level_writes_gt_two |= BITFIELD_BIT(shift);
                  continue;
               }

               /* nir_analyze_range only accepts an ALU src, so we have to
                * create a dummy ALU instruction. It's not inserted into
                * the shader.
                */
               nir_alu_instr *dummy_alu =
                  nir_alu_instr_create((nir_shader*)nir, nir_op_mov);
               dummy_alu->def.num_components = 1;
               dummy_alu->def.bit_size = scalar.def->bit_size;
               dummy_alu->src[0].src.ssa = scalar.def;
               dummy_alu->src[0].swizzle[0] = scalar.comp;

               const struct ssa_result_range r =
                  nir_analyze_range(range_ht, dummy_alu, 0);

               switch (r.range) {
               case unknown:
               case ge_zero:
               case ne_zero:
               default:
                  tess_level_writes_le_zero |= BITFIELD_BIT(shift);
                  tess_level_writes_le_one |= BITFIELD_BIT(shift);
                  tess_level_writes_le_two |= BITFIELD_BIT(shift);
                  tess_level_writes_gt_two |= BITFIELD_BIT(shift);
                  break;
               case lt_zero:
               case le_zero:
               case eq_zero:
                  tess_level_writes_le_zero |= BITFIELD_BIT(shift);
                  break;
               case gt_zero:
                  tess_level_writes_le_one |= BITFIELD_BIT(shift);
                  tess_level_writes_le_two |= BITFIELD_BIT(shift);
                  tess_level_writes_gt_two |= BITFIELD_BIT(shift);
                  break;
               }

               nir_instr_free(&dummy_alu->instr);
            }
         }
      }
   }

   ralloc_free(range_ht);

   /* Determine which outer tess level components can discard patches.
    * If the primitive type is unspecified, we have to assume the worst case.
    */
   unsigned min_outer, min_inner, max_outer, max_inner;
   mesa_count_tess_level_components(prim == TESS_PRIMITIVE_UNSPECIFIED ? TESS_PRIMITIVE_ISOLINES : prim,
                                    &min_outer, &min_inner);
   mesa_count_tess_level_components(prim, &max_outer, &max_inner);
   const unsigned min_valid_outer_comp_mask = BITFIELD_RANGE(0, min_outer);
   const unsigned max_valid_outer_comp_mask = BITFIELD_RANGE(0, max_outer);
   const unsigned max_valid_inner_comp_mask = BITFIELD_RANGE(4, max_inner);

   /* All tessellation levels are effectively 0 if the patch has at least one
    * outer tess level component either in the [-inf, 0] range or equal to NaN,
    * causing it to be discarded. Inner tess levels have no effect.
    */
   info->all_tess_levels_are_effectively_zero =
      tess_level_writes_le_zero & ~tess_level_writes_le_one &
      ~tess_level_writes_le_two & ~tess_level_writes_gt_two &
      min_valid_outer_comp_mask;

   const unsigned tess_level_writes_any =
      tess_level_writes_le_zero | tess_level_writes_le_one |
      tess_level_writes_le_two | tess_level_writes_gt_two;

   const bool outer_is_gt_zero_le_one =
      (tess_level_writes_le_one & ~tess_level_writes_le_zero &
       ~tess_level_writes_le_two & ~tess_level_writes_gt_two &
       max_valid_outer_comp_mask) ==
      (tess_level_writes_any & max_valid_outer_comp_mask);

   /* Whether the inner tess levels are in the [-inf, 1] range. */
   const bool inner_is_le_one =
      ((tess_level_writes_le_zero | tess_level_writes_le_one) &
       ~tess_level_writes_le_two & ~tess_level_writes_gt_two &
       max_valid_inner_comp_mask) ==
      (tess_level_writes_any & max_valid_inner_comp_mask);

   /* If the patch has tess level values set to 1 or equivalent numbers, it's
    * not discarded, but different things happen depending on the spacing.
    */
   switch (spacing) {
   case TESS_SPACING_EQUAL:
   case TESS_SPACING_FRACTIONAL_ODD:
   case TESS_SPACING_UNSPECIFIED:
      /* The tessellator clamps all tess levels greater than 0 to 1.
       * If all outer and inner tess levels are in the (0, 1] range, which is
       * effectively 1, untessellated patches are drawn.
       */
      info->all_tess_levels_are_effectively_one = outer_is_gt_zero_le_one &&
                                                  inner_is_le_one;
      break;

   case TESS_SPACING_FRACTIONAL_EVEN: {
      /* The tessellator clamps all tess levels to 2 (both outer and inner)
       * except outer tess level component 0 of isolines, which is clamped
       * to 1. If all outer tess levels are in the (0, 2] or (0, 1] range
       * (for outer[0] of isolines) and all inner tess levels are
       * in the [-inf, 2] range, it's the same as writing 1 to all tess
       * levels.
       */
      bool isolines_are_eff_one =
         /* The (0, 1] range of outer[0]. */
         (tess_level_writes_le_one & ~tess_level_writes_le_zero &
          ~tess_level_writes_le_two & ~tess_level_writes_gt_two & 0x1) ==
            (tess_level_writes_any & 0x1) &&
         /* The (0, 2] range of outer[1]. */
         ((tess_level_writes_le_one | tess_level_writes_le_two) &
          ~tess_level_writes_le_zero & ~tess_level_writes_gt_two & 0x2) ==
            (tess_level_writes_any & 0x2);

      bool triquads_are_eff_one =
         /* The (0, 2] outer range. */
         ((tess_level_writes_le_one | tess_level_writes_le_two) &
          ~tess_level_writes_le_zero & ~tess_level_writes_gt_two &
          max_valid_outer_comp_mask) ==
            (tess_level_writes_any & max_valid_outer_comp_mask) &&
         /* The [-inf, 2] inner range. */
         ((tess_level_writes_le_zero | tess_level_writes_le_one |
           tess_level_writes_le_two) & ~tess_level_writes_gt_two &
          max_valid_inner_comp_mask) ==
            (tess_level_writes_any & max_valid_inner_comp_mask);

      if (prim == TESS_PRIMITIVE_UNSPECIFIED) {
         info->all_tess_levels_are_effectively_one = isolines_are_eff_one &&
                                                     triquads_are_eff_one;
      } else if (prim == TESS_PRIMITIVE_ISOLINES) {
         info->all_tess_levels_are_effectively_one = isolines_are_eff_one;
      } else {
         info->all_tess_levels_are_effectively_one = triquads_are_eff_one;
      }
      break;
   }
   }

   assert(!info->all_tess_levels_are_effectively_zero ||
          !info->all_tess_levels_are_effectively_one);

   info->can_discard_patches =
      (tess_level_writes_le_zero & min_valid_outer_comp_mask) != 0;
}
