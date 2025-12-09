/*
 * Copyright © 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "brw_shader.h"
#include "brw_cfg.h"
#include "brw_analysis.h"
#include "brw_builder.h"

/* Duplicated from brw_def_analysis::fully_defines. */
static bool
fully_defines(const brw_shader &s, brw_inst *inst)
{
   return s.alloc.sizes[inst->dst.nr] * REG_SIZE == inst->size_written &&
          !inst->is_partial_write();
}

bool
brw_insert_load_reg(brw_shader &s)
{
   bool progress = false;

   const brw_def_analysis &defs = s.def_analysis.require();

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      /* These should not exist yet. */
      assert(inst->opcode != SHADER_OPCODE_LOAD_REG);

      /* UNDEF/DPAS opcodes may have the right source and destination patterns
       * to have their sources replaced by load_reg, but these instructions
       * are special and / or wierd. They should not be modified.
       *
       * MOV_INDIRECT/BROADCAST read variable amount of GRF for src0. This
       * pass does not handle this properly at the moment.
       */
      if (inst->opcode == SHADER_OPCODE_UNDEF ||
          inst->opcode == BRW_OPCODE_DPAS ||
          inst->opcode == SHADER_OPCODE_MOV_INDIRECT ||
          inst->opcode == SHADER_OPCODE_BROADCAST) {
         continue;
      }

      /* If the destination is non-VGRF adding load_reg instructions will not
       * help. If the destination is already SSA, nothing needs to be done.
       */
      if (inst->dst.file != VGRF || defs.get(inst->dst) != NULL)
         continue;

      /* If there is a source that would cause def_analysis::update_for_reads
       * to mark the def as invalid, adding load_reg for the sources will not
       * help.
       */
      if (inst->reads_accumulator_implicitly())
         continue;

      bool bad_source = false;
      for (int i = 0; i < inst->sources; i++) {
         if (brw_reg_is_arf(inst->src[i], BRW_ARF_ADDRESS) ||
             brw_reg_is_arf(inst->src[i], BRW_ARF_ACCUMULATOR) ||
             brw_reg_is_arf(inst->src[i], BRW_ARF_FLAG)) {
            bad_source = true;
            break;
         }
      }

      if (bad_source)
         continue;

      /* If the instruction does not fully define the destination, adding
       * load_reg instructions will not help.
       */
      if (!fully_defines(s, inst))
         continue;

      if (inst->exec_size < 8)
         continue;

      assert(inst->exec_size == 8 || inst->exec_size == 16 ||
             inst->exec_size == 32);

      const unsigned mask = (inst->exec_size / 8) - 1;

      /* Replace any non-SSA sources with load_reg of the source. */
      const brw_builder bld = brw_builder(inst);
      for (int i = 0; i < inst->sources; i++) {
         /* LOAD_REG only operates on VGRF sources. If the source is not VGRF,
          * skip it.
          */
         if (inst->src[i].file != VGRF)
            continue;

         /* The source is already a def, so don't add a LOAD_REG. */
         if (defs.get(inst->src[i]) != NULL)
            continue;

         /* Cases of stride != 1 are difficult to handle correctly. For
          * example, when stride is 0, the source may have been written by
          * NoMask instruction that cannot be seen from here. In this case,
          * emitting a non-NoMask LOAD_REG may not actually copy the value
          * that the instruction is trying to read.
          *
          * This means that is_scalar sources in larger exec sizes are not
          * handled. Since enough information is available in the source, this
          * could be added later.
          */
         if (inst->src[i].stride != 1)
            continue;

         /* If the size of the VGRF allocation is not an even multiple of
          * the SIMD size, don't emit a load_reg. This can occur for sparse
          * texture loads. These will have SIMD-size values for the texture
          * data and a single SIMD1 register for the residency information.
          */
         if ((s.alloc.sizes[inst->src[i].nr] & mask) != 0)
            continue;

         brw_reg_type t =
            brw_type_with_size(BRW_TYPE_UD,
                               brw_type_size_bits(inst->src[i].type));
         brw_reg old_src = brw_vgrf(inst->src[i].nr, t);
         brw_reg new_src;

         /* Since the sources of a LOAD_REG will likely not be defs,
          * none of the existing optimizations passes will eliminate
          * redundant LOAD_REG instructions. Search back though this
          * block to find a LOAD_REG of the same value to avoid emitting
          * too many redundant instructions.
          */
         foreach_inst_in_block_reverse_starting_from(brw_inst, scan_inst, inst) {
            if (scan_inst->dst.file == old_src.file &&
                scan_inst->dst.nr == old_src.nr) {
               break;
            }

            if (scan_inst->opcode == SHADER_OPCODE_LOAD_REG &&
                scan_inst->exec_size == inst->exec_size &&
                scan_inst->force_writemask_all == inst->force_writemask_all &&
                old_src.equals(scan_inst->src[0])) {
               new_src = scan_inst->dst;
               break;
            }
         }

         if (new_src.file == BAD_FILE)
            new_src = bld.LOAD_REG(old_src);

         inst->src[i].nr = new_src.nr;
         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

bool
brw_lower_load_reg(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode == SHADER_OPCODE_LOAD_REG) {
         const brw_builder ibld = brw_builder(inst);

         const unsigned bytes = inst->size_written;
         const unsigned type_bytes = brw_type_size_bytes(inst->dst.type);
         const unsigned bytes_per_mov = inst->exec_size * type_bytes;

         for (unsigned i = 0; i < bytes; i += bytes_per_mov) {
            ibld.MOV(byte_offset(inst->dst, i),
                     byte_offset(inst->src[0], i));
         }

         inst->remove();
         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}
