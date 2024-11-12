/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_fs.h"
#include "brw_fs_builder.h"

using namespace brw;

static uint64_t
src_as_uint(const brw_reg &src)
{
   assert(src.file == IMM);

   switch (src.type) {
   case BRW_TYPE_W:
      return (uint64_t)(int16_t)(src.ud & 0xffff);

   case BRW_TYPE_UW:
      return (uint64_t)(uint16_t)(src.ud & 0xffff);

   case BRW_TYPE_D:
      return (uint64_t)src.d;

   case BRW_TYPE_UD:
      return (uint64_t)src.ud;

   case BRW_TYPE_Q:
      return src.d64;

   case BRW_TYPE_UQ:
      return src.u64;

   default:
      unreachable("Invalid integer type.");
   }
}

static brw_reg
brw_imm_for_type(uint64_t value, enum brw_reg_type type)
{
   switch (type) {
   case BRW_TYPE_W:
      return brw_imm_w(value);

   case BRW_TYPE_UW:
      return brw_imm_uw(value);

   case BRW_TYPE_D:
      return brw_imm_d(value);

   case BRW_TYPE_UD:
      return brw_imm_ud(value);

   case BRW_TYPE_Q:
      return brw_imm_d(value);

   case BRW_TYPE_UQ:
      return brw_imm_uq(value);

   default:
      unreachable("Invalid integer type.");
   }
}

bool
brw_constant_fold_instruction(const intel_device_info *devinfo, fs_inst *inst)
{
   bool progress = false;

   switch (inst->opcode) {
   case BRW_OPCODE_ADD:
      if (inst->src[1].file != IMM)
         break;

      if (brw_type_is_int(inst->src[1].type) &&
          inst->src[1].is_zero()) {
         inst->opcode = BRW_OPCODE_MOV;
         inst->resize_sources(1);
         progress = true;
         break;
      }

      if (inst->src[0].file == IMM) {
         assert(inst->src[0].type == BRW_TYPE_F);
         inst->opcode = BRW_OPCODE_MOV;
         inst->src[0].f += inst->src[1].f;
         inst->resize_sources(1);
         progress = true;
         break;
      }

      break;


   case BRW_OPCODE_AND:
      if (inst->src[0].file == IMM && inst->src[1].file == IMM) {
         const uint64_t src0 = src_as_uint(inst->src[0]);
         const uint64_t src1 = src_as_uint(inst->src[1]);

         inst->opcode = BRW_OPCODE_MOV;
         inst->src[0] = brw_imm_for_type(src0 & src1, inst->dst.type);
         inst->resize_sources(1);
         progress = true;
         break;
      }

      break;

   case BRW_OPCODE_MUL:
      if ((inst->src[0].file != IMM && inst->src[1].file != IMM) ||
          brw_type_is_float(inst->src[1].type))
         break;

      /* From the BDW PRM, Vol 2a, "mul - Multiply":
       *
       *    "When multiplying integer datatypes, if src0 is DW and src1
       *    is W, irrespective of the destination datatype, the
       *    accumulator maintains full 48-bit precision."
       *    ...
       *    "When multiplying integer data types, if one of the sources
       *    is a DW, the resulting full precision data is stored in
       *    the accumulator."
       *
       * There are also similar notes in earlier PRMs.
       *
       * The MOV instruction can copy the bits of the source, but it
       * does not clear the higher bits of the accumulator. So, because
       * we might use the full accumulator in the MUL/MACH macro, we
       * shouldn't replace such MULs with MOVs.
       */
      if ((brw_type_size_bytes(inst->src[0].type) == 4 ||
           brw_type_size_bytes(inst->src[1].type) == 4) &&
          (inst->dst.is_accumulator() ||
           inst->writes_accumulator_implicitly(devinfo)))
         break;

      if (inst->src[0].is_zero() || inst->src[1].is_zero()) {
         inst->opcode = BRW_OPCODE_MOV;
         inst->src[0] = brw_imm_d(0);
         inst->resize_sources(1);
         progress = true;
         break;
      }

      /* a * 1 = a */
      if (inst->src[1].is_one()) {
         inst->opcode = BRW_OPCODE_MOV;
         inst->resize_sources(1);
         return true;
      }

      /* a * -1 = -a */
      if (inst->src[0].is_negative_one()) {
         inst->opcode = BRW_OPCODE_MOV;
         inst->src[0] = inst->src[1];
         inst->src[0].negate = !inst->src[0].negate;
         inst->resize_sources(1);
         progress = true;
         break;
      }

      if (inst->src[1].is_negative_one()) {
         inst->opcode = BRW_OPCODE_MOV;
         inst->src[0].negate = !inst->src[0].negate;
         inst->resize_sources(1);
         progress = true;
         break;
      }

      break;

   case BRW_OPCODE_OR:
      if (inst->src[0].file == IMM && inst->src[1].file == IMM) {
         const uint64_t src0 = src_as_uint(inst->src[0]);
         const uint64_t src1 = src_as_uint(inst->src[1]);

         inst->opcode = BRW_OPCODE_MOV;
         inst->src[0] = brw_imm_for_type(src0 | src1, inst->dst.type);
         inst->resize_sources(1);
         progress = true;
         break;
      }

      break;

   case BRW_OPCODE_SHL:
      if (inst->src[0].file == IMM && inst->src[1].file == IMM) {
         /* It's not currently possible to generate this, and this constant
          * folding does not handle it.
          */
         assert(!inst->saturate);

         brw_reg result;

         switch (brw_type_size_bytes(inst->src[0].type)) {
         case 2:
            result = brw_imm_uw(0x0ffff & (inst->src[0].ud << (inst->src[1].ud & 0x1f)));
            break;
         case 4:
            result = brw_imm_ud(inst->src[0].ud << (inst->src[1].ud & 0x1f));
            break;
         case 8:
            result = brw_imm_uq(inst->src[0].u64 << (inst->src[1].ud & 0x3f));
            break;
         default:
            /* Just in case a future platform re-enables B or UB types. */
            unreachable("Invalid source size.");
         }

         inst->opcode = BRW_OPCODE_MOV;
         inst->src[0] = retype(result, inst->dst.type);
         inst->resize_sources(1);

         progress = true;
      }
      break;

   default:
      break;
   }

#ifndef NDEBUG
   /* The function is only intended to do constant folding, so the result of
    * progress must be a MOV of an immediate value.
    */
   if (progress) {
      assert(inst->opcode == BRW_OPCODE_MOV);
      assert(inst->src[0].file == IMM);
   }
#endif

   return progress;
}

bool
brw_fs_opt_algebraic(fs_visitor &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst_safe(block, fs_inst, inst, s.cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_MOV:
         if ((inst->conditional_mod == BRW_CONDITIONAL_Z ||
              inst->conditional_mod == BRW_CONDITIONAL_NZ) &&
             inst->dst.is_null() &&
             (inst->src[0].abs || inst->src[0].negate)) {
            inst->src[0].abs = false;
            inst->src[0].negate = false;
            progress = true;
            break;
         }

         if (inst->src[0].file != IMM)
            break;

         if (inst->saturate) {
            /* Full mixed-type saturates don't happen.  However, we can end up
             * with things like:
             *
             *    mov.sat(8) g21<1>DF       -1F
             *
             * Other mixed-size-but-same-base-type cases may also be possible.
             */
            if (inst->dst.type != inst->src[0].type &&
                inst->dst.type != BRW_TYPE_DF &&
                inst->src[0].type != BRW_TYPE_F)
               assert(!"unimplemented: saturate mixed types");

            if (brw_reg_saturate_immediate(&inst->src[0])) {
               inst->saturate = false;
               progress = true;
            }
         }
         break;

      case BRW_OPCODE_MUL:
      case BRW_OPCODE_ADD:
      case BRW_OPCODE_AND:
         if (brw_constant_fold_instruction(devinfo, inst))
            progress = true;

         break;

      case BRW_OPCODE_OR:
         if (brw_constant_fold_instruction(devinfo, inst)) {
            progress = true;
         } else if (inst->src[0].equals(inst->src[1]) ||
                    inst->src[1].is_zero()) {
            /* On Gfx8+, the OR instruction can have a source modifier that
             * performs logical not on the operand.  Cases of 'OR r0, ~r1, 0'
             * or 'OR r0, ~r1, ~r1' should become a NOT instead of a MOV.
             */
            if (inst->src[0].negate) {
               inst->opcode = BRW_OPCODE_NOT;
               inst->src[0].negate = false;
            } else {
               inst->opcode = BRW_OPCODE_MOV;
            }
            inst->resize_sources(1);
            progress = true;
            break;
         }
         break;
      case BRW_OPCODE_CMP:
         if ((inst->conditional_mod == BRW_CONDITIONAL_Z ||
              inst->conditional_mod == BRW_CONDITIONAL_NZ) &&
             inst->src[1].is_zero() &&
             (inst->src[0].abs || inst->src[0].negate)) {
            inst->src[0].abs = false;
            inst->src[0].negate = false;
            progress = true;
            break;
         }
         break;
      case BRW_OPCODE_SEL:
         if (inst->src[0].equals(inst->src[1])) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->predicate = BRW_PREDICATE_NONE;
            inst->predicate_inverse = false;
            inst->resize_sources(1);
            progress = true;
         } else if (inst->saturate && inst->src[1].file == IMM) {
            switch (inst->conditional_mod) {
            case BRW_CONDITIONAL_LE:
            case BRW_CONDITIONAL_L:
               switch (inst->src[1].type) {
               case BRW_TYPE_F:
                  if (inst->src[1].f >= 1.0f) {
                     inst->opcode = BRW_OPCODE_MOV;
                     inst->conditional_mod = BRW_CONDITIONAL_NONE;
                     inst->resize_sources(1);
                     progress = true;
                  }
                  break;
               default:
                  break;
               }
               break;
            case BRW_CONDITIONAL_GE:
            case BRW_CONDITIONAL_G:
               switch (inst->src[1].type) {
               case BRW_TYPE_F:
                  if (inst->src[1].f <= 0.0f) {
                     inst->opcode = BRW_OPCODE_MOV;
                     inst->conditional_mod = BRW_CONDITIONAL_NONE;
                     inst->resize_sources(1);
                     progress = true;
                  }
                  break;
               default:
                  break;
               }
            default:
               break;
            }
         }
         break;
      case BRW_OPCODE_CSEL:
         if (brw_type_is_float(inst->dst.type)) {
            /* This transformation can both clean up spurious modifiers
             * (making assembly dumps easier to read) and convert GE with -abs
             * to LE with abs. See abs handling below.
             */
            if (inst->src[2].negate) {
               inst->conditional_mod = brw_swap_cmod(inst->conditional_mod);
               inst->src[2].negate = false;
               progress = true;
            }

            if (inst->src[2].abs) {
               switch (inst->conditional_mod) {
               case BRW_CONDITIONAL_Z:
               case BRW_CONDITIONAL_NZ:
                  inst->src[2].abs = false;
                  progress = true;
                  break;

               case BRW_CONDITIONAL_LE:
                  /* Converting to Z can help constant propagation into src0
                   * and src1.
                   */
                  inst->conditional_mod = BRW_CONDITIONAL_Z;
                  inst->src[2].abs = false;
                  progress = true;
                  break;

               default:
                  /* GE or L conditions with absolute value could be used to
                   * implement isnan(x) in CSEL. Transforming G with absolute
                   * value to NZ is **not** NaN safe.
                   */
                  break;
               }
            }
         } else if (brw_type_is_sint(inst->src[2].type)) {
            /* Integer transformations are more challenging than floating
             * point transformations due to INT_MIN == -(INT_MIN) ==
             * abs(INT_MIN).
             */
            if (inst->src[2].negate && inst->src[2].abs) {
               switch (inst->conditional_mod) {
               case BRW_CONDITIONAL_GE:
                  inst->src[2].negate = false;
                  inst->src[2].abs = false;
                  inst->conditional_mod = BRW_CONDITIONAL_Z;
                  progress = true;
                  break;
               case BRW_CONDITIONAL_L:
                  inst->src[2].negate = false;
                  inst->src[2].abs = false;
                  inst->conditional_mod = BRW_CONDITIONAL_NZ;
                  progress = true;
                  break;
               case BRW_CONDITIONAL_G:
                  /* This is a contradtion. -abs(x) cannot be > 0. */
                  inst->opcode = BRW_OPCODE_MOV;
                  inst->src[0] = inst->src[1];
                  inst->resize_sources(1);
                  progress = true;
                  break;
               case BRW_CONDITIONAL_LE:
                  /* This is a tautology. -abs(x) must be <= 0. */
                  inst->opcode = BRW_OPCODE_MOV;
                  inst->resize_sources(1);
                  progress = true;
                  break;
               case BRW_CONDITIONAL_Z:
               case BRW_CONDITIONAL_NZ:
                  inst->src[2].negate = false;
                  inst->src[2].abs = false;
                  progress = true;
                  break;
               default:
                  unreachable("Impossible icsel condition.");
               }
            }
         }
         break;
      case BRW_OPCODE_MAD:
         if (inst->src[0].type != BRW_TYPE_F ||
             inst->src[1].type != BRW_TYPE_F ||
             inst->src[2].type != BRW_TYPE_F)
            break;
         if (inst->src[1].is_one()) {
            inst->opcode = BRW_OPCODE_ADD;
            inst->src[1] = inst->src[2];
            inst->resize_sources(2);
            progress = true;
         } else if (inst->src[2].is_one()) {
            inst->opcode = BRW_OPCODE_ADD;
            inst->resize_sources(2);
            progress = true;
         }
         break;
      case BRW_OPCODE_SHL:
         if (brw_constant_fold_instruction(devinfo, inst))
            progress = true;
         break;

      case SHADER_OPCODE_BROADCAST:
         if (is_uniform(inst->src[0])) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->force_writemask_all = true;
            inst->resize_sources(1);
            progress = true;
         } else if (inst->src[1].file == IMM) {
            inst->opcode = BRW_OPCODE_MOV;
            /* It's possible that the selected component will be too large and
             * overflow the register.  This can happen if someone does a
             * readInvocation() from GLSL or SPIR-V and provides an OOB
             * invocationIndex.  If this happens and we some how manage
             * to constant fold it in and get here, then component() may cause
             * us to start reading outside of the VGRF which will lead to an
             * assert later.  Instead, just let it wrap around if it goes over
             * exec_size.
             */
            const unsigned comp = inst->src[1].ud & (inst->exec_size - 1);
            inst->src[0] = component(inst->src[0], comp);
            inst->force_writemask_all = true;
            inst->resize_sources(1);
            progress = true;
         }
         break;

      case SHADER_OPCODE_SHUFFLE:
         if (is_uniform(inst->src[0])) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->resize_sources(1);
            progress = true;
         } else if (inst->src[1].file == IMM) {
            inst->opcode = BRW_OPCODE_MOV;
            inst->src[0] = component(inst->src[0],
                                     inst->src[1].ud);
            inst->resize_sources(1);
            progress = true;
         }
         break;

      default:
	 break;
      }

      /* Ensure that the correct source has the immediate value. 2-source
       * instructions must have the immediate in src[1]. On Gfx12 and later,
       * some 3-source instructions can have the immediate in src[0] or
       * src[2]. It's complicated, so don't mess with 3-source instructions
       * here.
       */
      if (progress && inst->sources == 2 && inst->is_commutative()) {
         if (inst->src[0].file == IMM) {
            brw_reg tmp = inst->src[1];
            inst->src[1] = inst->src[0];
            inst->src[0] = tmp;
         }
      }
   }

   if (progress)
      s.invalidate_analysis(DEPENDENCY_INSTRUCTION_DATA_FLOW |
                            DEPENDENCY_INSTRUCTION_DETAIL);

   return progress;
}
