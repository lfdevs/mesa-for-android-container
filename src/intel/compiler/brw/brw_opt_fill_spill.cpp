/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "brw_shader.h"
#include "brw_builder.h"

/**
 * \file
 *
 * Attempt to eliminate spurious fills and spills.
 *
 * NOTE: This pass is run after register allocation but before
 * brw_lower_vgrfs_to_fixed_grfs.
 */

static bool
scratch_intersects(const intel_device_info *devinfo,
                   const brw_scratch_inst *a, const brw_scratch_inst *b)
{
   const auto a_first = a->offset;
   const auto a_last = (a->opcode == SHADER_OPCODE_LSC_SPILL ?
                        a->size_read(devinfo, SPILL_SRC_PAYLOAD2) :
                        a->size_written) + a_first - 1;
   const auto b_first = b->offset;
   const auto b_last = (b->opcode == SHADER_OPCODE_LSC_SPILL ?
                        b->size_read(devinfo, SPILL_SRC_PAYLOAD2) :
                        b->size_written) + b_first - 1;

   return a_last >= b_first && b_last >= a_first;
}

static bool
scratch_superset(const intel_device_info *devinfo,
                   const brw_scratch_inst *super, const brw_scratch_inst *sub)
{
   const auto a_first = super->offset;
   const auto a_last = (super->opcode == SHADER_OPCODE_LSC_SPILL ?
                        super->size_read(devinfo, SPILL_SRC_PAYLOAD2) :
                        super->size_written) + a_first - 1;
   const auto b_first = sub->offset;
   const auto b_last = (sub->opcode == SHADER_OPCODE_LSC_SPILL ?
                        sub->size_read(devinfo, SPILL_SRC_PAYLOAD2) :
                        sub->size_written) + b_first - 1;

   return a_first <= b_first && a_last >= b_last;
}

bool
brw_opt_fill_and_spill(brw_shader &s)
{
   assert(s.grf_used > 0);

   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block(block, s.cfg) {
      bool block_progress = false;

      foreach_inst_in_block(brw_inst, inst, block) {
         if (inst->opcode != SHADER_OPCODE_LSC_SPILL)
            continue;

         const brw_reg spilled =
            brw_lower_vgrf_to_fixed_grf(devinfo, inst,
                                        inst->src[SPILL_SRC_PAYLOAD2]);

         /* Check for a fill from the same location while the register being
          * spilled still contains the data. In this case, replace the fill
          * with a simple move.
          */
         foreach_inst_in_block_starting_from(brw_inst, scan_inst, inst) {
            /* Write to the register being spilled invalidates the value. */
            const brw_reg scan_dst =
               brw_lower_vgrf_to_fixed_grf(devinfo, scan_inst, scan_inst->dst);

            if (regions_overlap(scan_dst, scan_inst->size_written,
                                spilled,
                                inst->size_read(devinfo, SPILL_SRC_PAYLOAD2))) {
               break;
            }

            /* Spill to the same location invalidates the value. */
            if (scan_inst->opcode == SHADER_OPCODE_LSC_SPILL &&
                scratch_intersects(devinfo, scan_inst->as_scratch(),
                                   inst->as_scratch())) {
               break;
            }

            /* Instruction is a fill from the same location as the spill. */
            if (scan_inst->opcode == SHADER_OPCODE_LSC_FILL &&
                scan_inst->force_writemask_all == inst->force_writemask_all &&
                scan_inst->as_scratch()->offset == inst->as_scratch()->offset) {
               /* This limitation is necessary because (currently) a spill may
                * be split into multiple writes while the correspoing fill is
                * implemented as a single transpose read. When this occurs,
                * this optimization pass would have to be smarter than it
                * currently is.
                *
                * FINISHME: This would not be an issue if the splitting
                * occured during spill lowering.
                */
               if (scan_inst->size_written != inst->size_read(devinfo, SPILL_SRC_PAYLOAD2))
                  continue;

               const unsigned reg_count = DIV_ROUND_UP(scan_inst->size_written, REG_SIZE);
               const unsigned max_reg_count = 2 * reg_unit(devinfo);

               /* If the resulting MOV would try to write more than 2
                * registers, skip the optimization.
                *
                * FINISHME: It shouldn't be hard to generate multiple MOV
                * instructions below to handle this case.
                */
               if (reg_count > max_reg_count)
                  continue;

               if (scan_inst->dst.equals(inst->src[SPILL_SRC_PAYLOAD2])) {
                  scan_inst = brw_transform_inst(s, scan_inst, BRW_OPCODE_NOP);
               } else {
                  scan_inst = brw_transform_inst(s, scan_inst, BRW_OPCODE_MOV);
                  scan_inst->src[0] = inst->src[SPILL_SRC_PAYLOAD2];
               }

               s.shader_stats.fill_count--;
               block_progress = true;
            }
         }

         /* Scan again. This time check whether there is a spill to the same
          * location without an intervening fill from that location. In this
          * case, the first spill is "killed" and can be removed.
          */
         foreach_inst_in_block_starting_from(brw_inst, scan_inst, inst) {
            if (scan_inst->opcode == SHADER_OPCODE_LSC_FILL &&
                scratch_intersects(devinfo, inst->as_scratch(),
                                   scan_inst->as_scratch())) {
               break;
            }

            if (scan_inst->opcode == SHADER_OPCODE_LSC_SPILL &&
                scratch_superset(devinfo, scan_inst->as_scratch(),
                                 inst->as_scratch())) {
               inst = brw_transform_inst(s, inst, BRW_OPCODE_NOP);
               s.shader_stats.spill_count--;
               block_progress = true;
               break;
            }
         }
      }

      /* Optimize multiple fills from the same offset in a single block. */
      foreach_inst_in_block(brw_inst, inst, block) {
         if (inst->opcode != SHADER_OPCODE_LSC_FILL)
            continue;

         brw_reg inst_dst = brw_lower_vgrf_to_fixed_grf(devinfo, inst,
                                                        inst->dst);

         foreach_inst_in_block_starting_from(brw_inst, scan_inst, inst) {
            /* Instruction is a fill from the same location as the previous
             * fill.
             */
            brw_reg scan_dst = brw_lower_vgrf_to_fixed_grf(devinfo, scan_inst,
                                                           scan_inst->dst);

            if (scan_inst->opcode == SHADER_OPCODE_LSC_FILL &&
                scan_inst->force_writemask_all == inst->force_writemask_all &&
                scan_inst->as_scratch()->offset == inst->as_scratch()->offset &&
                scan_inst->size_written == inst->size_written &&
                scan_inst->group == inst->group &&
                scan_inst->as_scratch()->use_transpose == inst->as_scratch()->use_transpose) {
               const unsigned reg_count = DIV_ROUND_UP(scan_inst->size_written, REG_SIZE);
               const unsigned max_reg_count = 2 * reg_unit(devinfo);

               /* If the resulting MOV would try to write more than 2
                * registers, skip the optimization.
                *
                * FINISHME: It shouldn't be hard to generate multiple MOV
                * instructions below to handle this case.
                */
               if (reg_count > max_reg_count)
                  continue;

               if (scan_dst.equals(inst_dst)) {
                  scan_inst = brw_transform_inst(s, scan_inst, BRW_OPCODE_NOP);
               } else {
                  /* This can occur for fills in wider SIMD modes. In SIMD32
                   * on Xe2, a fill to r16 followed by a fill to r17 from the
                   * same location can't be trivially replaced. The resulting
                   * `mov(32) r17, r16` would have the same problems of memcpy
                   * with overlapping ranges.
                   *
                   * FINISHME: This is fixable, but it required emitting two
                   * MOVs with hald SIMD size. It might also "just work" if
                   * scan_dst.nr < inst_dst.nr.
                   */
                  if (regions_overlap(scan_dst, scan_inst->size_written,
                                      inst_dst, inst->size_written)) {
                     break;
                  }

                  scan_inst = brw_transform_inst(s, scan_inst, BRW_OPCODE_MOV);
                  scan_inst->src[0] = inst->dst;
               }

               s.shader_stats.fill_count--;
               block_progress = true;
            } else {
               /* A spill to the same location invalidates the value. */
               if (scan_inst->opcode == SHADER_OPCODE_LSC_SPILL &&
                   scratch_intersects(devinfo, inst->as_scratch(),
                                      scan_inst->as_scratch())) {
                  break;
               }

               /* Write to the register being filled invalidates the value. */
               if (regions_overlap(scan_dst, scan_inst->size_written,
                                   inst_dst, inst->size_written)) {
                  break;
               }
            }
         }
      }

      if (block_progress) {
         foreach_inst_in_block_safe(brw_inst, inst, block) {
            if (inst->opcode == BRW_OPCODE_NOP)
               inst->remove();
         }

         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}
