/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_builder.h"
#include "util/half_float.h"

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
      UNREACHABLE("Invalid integer type.");
   }
}

static double
src_as_float(const brw_reg &src)
{
   assert(src.file == IMM);

   switch (src.type) {
   case BRW_TYPE_HF:
      return _mesa_half_to_float((uint16_t)src.d);

   case BRW_TYPE_F:
      return src.f;

   case BRW_TYPE_DF:
      return src.df;

   default:
      UNREACHABLE("Invalid float type.");
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
      UNREACHABLE("Invalid integer type.");
   }
}

/**
 * Converts a MAD to an ADD by folding the multiplicand sources.
 */
static brw_inst *
fold_multiplicands_of_MAD(brw_shader &s, brw_inst *inst)
{
   assert(inst->opcode == BRW_OPCODE_MAD);
   assert (inst->src[1].file == IMM &&
           inst->src[2].file == IMM &&
           !brw_type_is_vector_imm(inst->src[1].type) &&
           !brw_type_is_vector_imm(inst->src[2].type));

   if (brw_type_is_int(inst->src[1].type)) {
      const uint64_t imm1 = src_as_uint(inst->src[1]);
      const uint64_t imm2 = src_as_uint(inst->src[2]);

      brw_reg product = brw_imm_ud(imm1 * imm2);

      inst->src[1] = retype(product,
                            brw_type_larger_of(inst->src[1].type,
                                               inst->src[2].type));
   } else {
      const double product = src_as_float(inst->src[1]) *
         src_as_float(inst->src[2]);

      switch (brw_type_larger_of(inst->src[1].type,
                                 inst->src[2].type)) {
      case BRW_TYPE_HF:
         inst->src[1] = retype(brw_imm_w(_mesa_float_to_half(product)),
                               BRW_TYPE_HF);
         break;

      case BRW_TYPE_F:
         inst->src[1] = brw_imm_f(product);
         break;

      case BRW_TYPE_DF:
         UNREACHABLE("float64 should be impossible.");
         break;

      default:
         UNREACHABLE("Invalid float type.");
      }
   }

   return brw_transform_inst(s, inst, BRW_OPCODE_ADD);
}

bool
brw_opt_constant_fold_instruction(brw_shader &s, brw_inst *inst)
{
   const intel_device_info *devinfo = s.devinfo;
   brw_reg result;

   result.file = BAD_FILE;

   switch (inst->opcode) {
   case BRW_OPCODE_ADD:
      if (inst->src[0].file != IMM || inst->src[1].file != IMM)
         break;

      if (brw_type_is_int(inst->src[0].type)) {
         const uint64_t src0 = src_as_uint(inst->src[0]);
         const uint64_t src1 = src_as_uint(inst->src[1]);

         result = brw_imm_for_type(src0 + src1, inst->dst.type);
      } else {
         assert(inst->src[0].type == BRW_TYPE_F);
         result = brw_imm_f(inst->src[0].f + inst->src[1].f);
      }

      break;

   case BRW_OPCODE_ADD3:
      if (inst->src[0].file == IMM &&
          inst->src[1].file == IMM &&
          inst->src[2].file == IMM) {
         const uint64_t src0 = src_as_uint(inst->src[0]);
         const uint64_t src1 = src_as_uint(inst->src[1]);
         const uint64_t src2 = src_as_uint(inst->src[2]);

         result = brw_imm_for_type(src0 + src1 + src2, inst->dst.type);
      }

      break;

   case BRW_OPCODE_AND:
      if (inst->src[0].file == IMM && inst->src[1].file == IMM) {
         const uint64_t src0 = src_as_uint(inst->src[0]);
         const uint64_t src1 = src_as_uint(inst->src[1]);

         result = brw_imm_for_type(src0 & src1, inst->dst.type);
         break;
      }

      break;

   case BRW_OPCODE_MAD:
      if (inst->src[0].file == IMM &&
          inst->src[1].file == IMM &&
          inst->src[2].file == IMM &&
          !brw_type_is_vector_imm(inst->src[0].type) &&
          !brw_type_is_vector_imm(inst->src[1].type) &&
          !brw_type_is_vector_imm(inst->src[2].type)) {
         inst = fold_multiplicands_of_MAD(s, inst);
         assert(inst->opcode == BRW_OPCODE_ADD);

         ASSERTED bool folded = brw_opt_constant_fold_instruction(s, inst);
         assert(folded);

         return true;
      }

      break;

   case BRW_OPCODE_MUL:
      if (brw_type_is_float(inst->src[1].type))
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
         result = brw_imm_d(0);
         break;
      }

      if (inst->src[0].file == IMM && inst->src[1].file == IMM) {
         const uint64_t src0 = src_as_uint(inst->src[0]);
         const uint64_t src1 = src_as_uint(inst->src[1]);

         result = brw_imm_for_type(src0 * src1, inst->dst.type);
         break;
      }
      break;

   case BRW_OPCODE_OR:
      if (inst->src[0].file == IMM && inst->src[1].file == IMM) {
         const uint64_t src0 = src_as_uint(inst->src[0]);
         const uint64_t src1 = src_as_uint(inst->src[1]);

         result = brw_imm_for_type(src0 | src1, inst->dst.type);
         break;
      }

      break;

   case BRW_OPCODE_SHL:
      if (inst->src[0].file == IMM && inst->src[1].file == IMM) {
         /* It's not currently possible to generate this, and this constant
          * folding does not handle it.
          */
         assert(!inst->saturate);

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
            UNREACHABLE("Invalid source size.");
         }

         result = retype(result, inst->dst.type);
      }
      break;

   case SHADER_OPCODE_BROADCAST:
      if (inst->src[0].file == IMM) {
         inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);

         inst->force_writemask_all = true;

         /* The destination of BROADCAST will always be is_scalar, so the
          * allocation will always be REG_SIZE * reg_unit. Adjust the
          * exec_size to match.
          */
         inst->exec_size = 8 * reg_unit(devinfo);
         assert(inst->size_written == inst->dst.component_size(inst->exec_size));

         return true;
      }
      break;

   case SHADER_OPCODE_SHUFFLE:
      if (inst->src[0].file == IMM)
         result = inst->src[0];

      break;

   case FS_OPCODE_DDX_COARSE:
   case FS_OPCODE_DDX_FINE:
   case FS_OPCODE_DDY_COARSE:
   case FS_OPCODE_DDY_FINE:
      if (is_uniform(inst->src[0]) || inst->src[0].is_scalar)
         result = retype(brw_imm_uq(0), inst->dst.type);

      break;

   default:
      break;
   }

   if (result.file != BAD_FILE) {
      assert(result.file == IMM);

      inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);

      inst->src[0] = result;
      return true;
   }

   return false;
}

bool
brw_opt_algebraic(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (brw_opt_constant_fold_instruction(s, inst)) {
         progress = true;
         continue;
      }

      switch (inst->opcode) {
      case BRW_OPCODE_ADD:
         if (brw_type_is_int(inst->src[1].type) &&
                    inst->src[1].is_zero()) {
            inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
            progress = true;
         }

         break;

      case BRW_OPCODE_ADD3: {
         const unsigned num_imm = (inst->src[0].file == IMM) +
                                  (inst->src[1].file == IMM) +
                                  (inst->src[2].file == IMM);

         /* If there is more than one immediate value, fold the values and
          * convert the instruction to either ADD or MOV.
          */
         assert(num_imm < 3);
         if (num_imm == 2) {
            uint64_t sum = 0;
            brw_reg src;

            for (unsigned i = 0; i < 3; i++) {
               if (inst->src[i].file == IMM) {
                  sum += src_as_uint(inst->src[i]);
               } else {
                  assert(src.file == BAD_FILE);
                  src = inst->src[i];
               }
            }

            assert(src.file != BAD_FILE);

            if (uint32_t(sum) == 0) {
               inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
               inst->src[0] = src;
            } else {
               inst = brw_transform_inst(s, inst, BRW_OPCODE_ADD);
               inst->src[0] = src;
               inst->src[1] = brw_imm_ud(sum);
            }

            progress = true;
         } else if (num_imm == 1) {
            /* If there is a single constant, and that constant is zero,
             * convert the instruction to regular ADD.
             */
            for (unsigned i = 0; i < 3; i++) {
               if (inst->src[i].is_zero()) {
                  inst->src[i] = inst->src[2];
                  inst = brw_transform_inst(s, inst, BRW_OPCODE_ADD);
                  progress = true;
                  break;
               }
            }
         }

         break;
      }

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
               UNREACHABLE("unimplemented: saturate mixed types");

            if (brw_reg_saturate_immediate(&inst->src[0])) {
               inst->saturate = false;
               progress = true;
            }
         }
         break;

      case BRW_OPCODE_MUL:
         if (brw_type_is_int(inst->src[0].type)){
            /* From the BDW PRM, Vol 2a, "mul - Multiply":
             *
             *    "When multiplying integer datatypes, if src0 is DW and src1
             *    is W, irrespective of the destination datatype, the
             *    accumulator maintains full 48-bit precision."
             *    ...
             *    "When multiplying integer data types, if one of the sources
             *    is a DW, the resulting full precision data is stored in the
             *    accumulator."
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

            for (unsigned i = 0; i < 2; i++) {
               bool found = false;

               /* a * 1 = a */
               if (inst->src[i].is_one()) {
                  found = true;
               } else if (inst->src[i].is_negative_one()) {
                  /* a * -1 = -a */
                  found = true;

                  /* If the source other than the -1 is immediate, just
                   * toggling the negation flag will not work. Due to the
                   * previous call to brw_constant_fold_instruction, this
                   * should not be possible.
                   */
                  assert(inst->src[1 - i].file != IMM);
                  inst->src[1 - i].negate = !inst->src[1 - i].negate;
               }

               if (found) {
                  /* If the literal 1 was src0, put the old src1 in src0. */
                  if (i == 0)
                     inst->src[0] = inst->src[1];

                  inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
                  progress = true;
                  break;
               }
            }
         }
         break;

      case BRW_OPCODE_NOT:
         /*    not.nz    null, g17
          *
          * becomes
          *
          *    mov.z     null, g17
          *
          * These are equivalent, but the latter is easier for cmod prop.
          */
         if (inst->dst.is_null() &&
             inst->conditional_mod != BRW_CONDITIONAL_NONE) {
            assert(!inst->src[0].abs);

            if (!inst->src[0].negate)
               inst->conditional_mod = brw_negate_cmod(inst->conditional_mod);

            inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
            inst->src[0].negate = false;
            progress = true;
         }
         break;

      case BRW_OPCODE_OR:
         if (inst->src[0].equals(inst->src[1]) || inst->src[1].is_zero()) {
            /* On Gfx8+, the OR instruction can have a source modifier that
             * performs logical not on the operand.  Cases of 'OR r0, ~r1, 0'
             * or 'OR r0, ~r1, ~r1' should become a NOT instead of a MOV.
             */
            if (inst->src[0].negate) {
               inst = brw_transform_inst(s, inst, BRW_OPCODE_NOT);
               inst->src[0].negate = false;
            } else {
               inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
            }
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
         /* Floating point SEL.CMOD may flush denorms to zero. We don't have
          * enough information at this point in compilation to know whether or
          * not it is safe to remove that.
          *
          * Integer SEL or SEL without a conditional modifier is just a fancy
          * MOV. Those are always safe to eliminate.
          */
         if (inst->src[0].equals(inst->src[1]) &&
             (!brw_type_is_float(inst->dst.type) ||
              inst->conditional_mod == BRW_CONDITIONAL_NONE)) {
            inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
            inst->predicate = BRW_PREDICATE_NONE;
            inst->predicate_inverse = false;
            inst->conditional_mod = BRW_CONDITIONAL_NONE;
            progress = true;
         } else if (inst->saturate && inst->src[1].file == IMM) {
            switch (inst->conditional_mod) {
            case BRW_CONDITIONAL_LE:
            case BRW_CONDITIONAL_L:
               switch (inst->src[1].type) {
               case BRW_TYPE_F:
                  if (inst->src[1].f >= 1.0f) {
                     inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
                     inst->conditional_mod = BRW_CONDITIONAL_NONE;
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
                     inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
                     inst->conditional_mod = BRW_CONDITIONAL_NONE;
                     progress = true;
                  }
                  break;
               default:
                  break;
               }
               break;
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
                  inst->src[0] = inst->src[1];
                  inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
                  progress = true;
                  break;
               case BRW_CONDITIONAL_LE:
                  /* This is a tautology. -abs(x) must be <= 0. */
                  inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
                  progress = true;
                  break;
               case BRW_CONDITIONAL_Z:
               case BRW_CONDITIONAL_NZ:
                  inst->src[2].negate = false;
                  inst->src[2].abs = false;
                  progress = true;
                  break;
               default:
                  UNREACHABLE("Impossible icsel condition.");
               }
            }
         }
         break;
      case BRW_OPCODE_MAD:
         if (inst->src[1].file == IMM &&
             inst->src[2].file == IMM &&
             !brw_type_is_vector_imm(inst->src[1].type) &&
             !brw_type_is_vector_imm(inst->src[2].type)) {
            fold_multiplicands_of_MAD(s, inst);

            /* This could result in (x + 0). For floats, we want to leave this
             * as an ADD so that a subnormal x will get flushed to zero.
             */
            assert(inst->opcode == BRW_OPCODE_ADD);
            progress = true;
            break;
         }

         if (inst->src[1].is_one()) {
            inst->src[1] = inst->src[2];
            inst = brw_transform_inst(s, inst, BRW_OPCODE_ADD);
            progress = true;
         } else if (inst->src[2].is_one()) {
            inst = brw_transform_inst(s, inst, BRW_OPCODE_ADD);
            progress = true;
         }
         break;
      case SHADER_OPCODE_BROADCAST:
         if (is_uniform(inst->src[0])) {
            inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);

            inst->force_writemask_all = true;

            /* The destination of BROADCAST will always be is_scalar, so the
             * allocation will always be REG_SIZE * reg_unit. Adjust the
             * exec_size to match.
             */
            inst->exec_size = 8 * reg_unit(devinfo);
            assert(inst->size_written == inst->dst.component_size(inst->exec_size));
            progress = true;
         } else if (inst->src[1].file == IMM) {
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
            inst->exec_size = 8 * reg_unit(devinfo);
            assert(inst->size_written == inst->dst.component_size(inst->exec_size));

            inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);

            progress = true;
         }
         break;

      case SHADER_OPCODE_SHUFFLE:
         if (is_uniform(inst->src[0])) {
            inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
            progress = true;
         } else if (inst->src[1].file == IMM) {
            const unsigned comp = inst->src[1].ud & (inst->exec_size - 1);
            inst->src[0] = component(inst->src[0], comp);
            inst = brw_transform_inst(s, inst, BRW_OPCODE_MOV);
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
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}
