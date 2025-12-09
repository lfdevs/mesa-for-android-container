/*
 * Copyright © 2014 Intel Corporation
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

#include "brw_shader.h"
#include "brw_cfg.h"
#include "brw_eu.h"
#include "util/half_float.h"

/** @file
 *
 * Implements a pass that propagates the conditional modifier from a CMP x 0.0
 * instruction into the instruction that generated x. For instance, in this
 * sequence
 *
 *    add(8)          g70<1>F    g69<8,8,1>F    4096F
 *    cmp.ge.f0(8)    null       g70<8,8,1>F    0F
 *
 * we can do the comparison as part of the ADD instruction directly:
 *
 *    add.ge.f0(8)    g70<1>F    g69<8,8,1>F    4096F
 *
 * If there had been a use of the flag register and another CMP using g70
 *
 *    add.ge.f0(8)    g70<1>F    g69<8,8,1>F    4096F
 *    (+f0) sel(8)    g71<F>     g72<8,8,1>F    g73<8,8,1>F
 *    cmp.ge.f0(8)    null       g70<8,8,1>F    0F
 *
 * we can recognize that the CMP is generating the flag value that already
 * exists and therefore remove the instruction.
 */

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

static bool
cmod_propagate_cmp_to_add(const intel_device_info *devinfo, brw_inst *inst)
{
   bool read_flag = false;
   const unsigned flags_written = inst->flags_written(devinfo);

   /* The floating point comparison can only be removed if we can prove that
    * either the addition is not ±Inf - (±Inf) or that ±Inf - (±Inf) compared
    * with zero has the same result as ±Inf compared with ±Inf.
    *
    * The former can only be proven at this point in compilation if src[1] is
    * an immediate value. Otherwise we can't know that nether value is
    * ±Inf. For the latter, consider this table:
    *
    *      A     B   A+(-B)  A<B  A-B<0  A<=B  A-B<=0  A>B  A-B>0  A>=B  A-B>=0  A==B  A-B==0  A!=B  A-B!=0
    *     Inf   Inf   NaN     F     F     T       F     F     F     T      F      T      F      F       T
    *     Inf  -Inf   Inf     F     F     F       F     T     T     T      T      F      F      T       T
    *    -Inf   Inf  -Inf     T     T     T       T     F     F     F      F      F      F      T       T
    *    -Inf  -Inf   NaN     F     F     T       F     F     F     T      F      T      F      F       T
    *
    * The column for A<B and A-B<0 is identical, and the column for A>B and
    * A-B>0 are identical.
    *
    * If src[1] is NaN, the transformation is always valid.
    */
   if (brw_type_is_float(inst->src[0].type)) {
      if (inst->conditional_mod != BRW_CONDITIONAL_L &&
          inst->conditional_mod != BRW_CONDITIONAL_G) {
         if (inst->src[1].file != IMM)
            return false;

         if (isinf(src_as_float(inst->src[1])) != 0)
            return false;
      }
   } else {
      /* This optimization can fail for integers.  For inputs a = 0x80000000,
       * b = 4, int(0x80000000) < 4, but int(0x80000000) - 4 overflows and
       * results in 0x7ffffffc.  that's not less than zero, so the flags get
       * set differently than for (a < b).
       *
       * However, it is safe if the comparison is Z or NZ.
       */
      if (inst->conditional_mod != BRW_CONDITIONAL_Z &&
          inst->conditional_mod != BRW_CONDITIONAL_NZ)
         return false;
   }

   foreach_inst_in_block_reverse_starting_from(brw_inst, scan_inst, inst) {
      if (scan_inst->opcode == BRW_OPCODE_ADD &&
          !scan_inst->predicate &&
          scan_inst->dst.is_contiguous() &&
          scan_inst->exec_size == inst->exec_size) {
         bool negate;

         /* A CMP is basically a subtraction.  The result of the
          * subtraction must be the same as the result of the addition.
          * This means that one of the operands must be negated.  So (a +
          * b) vs (a == -b) or (a + -b) vs (a == b).
          */
         if ((inst->src[0].equals(scan_inst->src[0]) &&
              inst->src[1].negative_equals(scan_inst->src[1])) ||
             (inst->src[0].equals(scan_inst->src[1]) &&
              inst->src[1].negative_equals(scan_inst->src[0]))) {
            negate = false;
         } else if ((inst->src[0].negative_equals(scan_inst->src[0]) &&
                     inst->src[1].equals(scan_inst->src[1])) ||
                    (inst->src[0].negative_equals(scan_inst->src[1]) &&
                     inst->src[1].equals(scan_inst->src[0]))) {
            negate = true;
         } else {
            goto not_match;
         }

         /* If the scan instruction writes a different flag register than the
          * instruction we're trying to propagate from, bail.
          *
          * FINISHME: The second part of the condition may be too strong.
          * Perhaps (scan_inst->flags_written() & flags_written) !=
          * flags_written?
          */
         if (scan_inst->flags_written(devinfo) != 0 &&
             scan_inst->flags_written(devinfo) != flags_written)
            goto not_match;

         /* From the Kaby Lake PRM Vol. 7 "Assigning Conditional Flags":
          *
          *    * Note that the [post condition signal] bits generated at
          *      the output of a compute are before the .sat.
          *
          * Paragraph about post_zero does not mention saturation, but
          * testing it on actual GPUs shows that conditional modifiers
          * are applied after saturation.
          *
          *    * post_zero bit: This bit reflects whether the final
          *      result is zero after all the clamping, normalizing,
          *      or format conversion logic.
          *
          * For signed types we don't care about saturation: it won't
          * change the result of conditional modifier.
          *
          * For floating and unsigned types there two special cases,
          * when we can remove inst even if scan_inst is saturated: G
          * and LE. Since conditional modifiers are just comparisons
          * against zero, saturating positive values to the upper
          * limit never changes the result of comparison.
          *
          * For negative values:
          * (sat(x) >  0) == (x >  0) --- false
          * (sat(x) <= 0) == (x <= 0) --- true
          *
          * Except for the x = NaN cases. sat(NaN) is 0, so add.sat.le of a
          * NaN result will be true. add.sat.g of a NaN result is false, so
          * the optimization is also incorrect when the second source of the
          * comparison is less than zero. All of the fsat(x) > is_negative
          * cases should have been eliminated in NIR.
          */
         const enum brw_conditional_mod cond =
            negate ? brw_swap_cmod(inst->conditional_mod)
            : inst->conditional_mod;

         if (scan_inst->saturate) {
            /* Note: Integer types can only have NZ or Z, so this path does
             * not need to worry about integer handling.
             */
            if (cond != BRW_CONDITIONAL_G)
               goto not_match;

            assert(!brw_type_is_int(scan_inst->dst.type));

            if (inst->src[1].file != IMM)
               goto not_match;

            double v = src_as_float(inst->src[1]);
            if (negate)
               v = -v;

            if (v < 0.0)
               goto not_match;
         }

         /* Otherwise, try propagating the conditional. */
         if (scan_inst->can_do_cmod(cond) &&
             ((!read_flag && scan_inst->conditional_mod == BRW_CONDITIONAL_NONE) ||
              scan_inst->conditional_mod == cond)) {
            scan_inst->conditional_mod = cond;
            scan_inst->flag_subreg = inst->flag_subreg;
            inst->remove();
            return true;
         }
         break;
      }

   not_match:
      if ((scan_inst->flags_written(devinfo) & flags_written) != 0)
         break;

      read_flag = read_flag ||
                  (scan_inst->flags_read(devinfo) & flags_written) != 0;
   }

   return false;
}

static bool
opt_cmod_propagation_local(const intel_device_info *devinfo, bblock_t *block)
{
   bool progress = false;

   foreach_inst_in_block_reverse_safe(brw_inst, inst, block) {
      if ((inst->opcode != BRW_OPCODE_AND &&
           inst->opcode != BRW_OPCODE_CMP &&
           inst->opcode != BRW_OPCODE_MOV) ||
          inst->predicate != BRW_PREDICATE_NONE ||
          !inst->dst.is_null() ||
          (inst->src[0].file != VGRF && inst->src[0].file != ATTR &&
           inst->src[0].file != UNIFORM))
         continue;

      /* An ABS source modifier can only be handled when processing a compare
       * with a value other than zero.
       */
      if (inst->src[0].abs &&
          (inst->opcode != BRW_OPCODE_CMP || inst->src[1].is_zero()))
         continue;

      /* Only an AND.NZ can be propagated. Propagating AND.Z would require
       * inverting the condition on the CMP. This changes both the flag value
       * and the register destination of the CMP. That result may be used
       * elsewhere, so we can't change its value on a whim.
       */
      if (inst->opcode == BRW_OPCODE_AND &&
          !(inst->src[1].is_one() &&
            inst->conditional_mod == BRW_CONDITIONAL_NZ &&
            !inst->src[0].negate))
         continue;

      /* A CMP with a second source of zero can match with anything.  A CMP
       * with a second source that is not zero can only match with an ADD
       * instruction.
       */
      if (inst->opcode == BRW_OPCODE_CMP && !inst->src[1].is_zero()) {
         if (cmod_propagate_cmp_to_add(devinfo, inst))
            progress = true;

         continue;
      }

      bool read_flag = false;
      const unsigned flags_written = inst->flags_written(devinfo);
      foreach_inst_in_block_reverse_starting_from(brw_inst, scan_inst, inst) {
         if (regions_overlap(scan_inst->dst, scan_inst->size_written,
                             inst->src[0], inst->size_read(devinfo, 0))) {
            /* If the scan instruction writes a different flag register than
             * the instruction we're trying to propagate from, bail.
             *
             * FINISHME: The second part of the condition may be too strong.
             * Perhaps (scan_inst->flags_written() & flags_written) !=
             * flags_written?
             */
            if (scan_inst->flags_written(devinfo) != 0 &&
                scan_inst->flags_written(devinfo) != flags_written)
               break;

            if (scan_inst->predicate ||
                !scan_inst->dst.is_contiguous() ||
                scan_inst->dst.offset != inst->src[0].offset ||
                scan_inst->exec_size != inst->exec_size)
               break;

            /* If the write mask is different we can't propagate. */
            if (scan_inst->force_writemask_all != inst->force_writemask_all)
               break;

            /* CMP's result is the same regardless of dest type. */
            if (inst->conditional_mod == BRW_CONDITIONAL_NZ &&
                scan_inst->opcode == BRW_OPCODE_CMP &&
                brw_type_is_int(inst->dst.type)) {
               inst->remove();
               progress = true;
               break;
            }

            /* If the AND wasn't handled by the previous case, it isn't safe
             * to remove it.
             */
            if (inst->opcode == BRW_OPCODE_AND)
               break;

            if (inst->opcode == BRW_OPCODE_MOV) {
               if (brw_type_is_float(scan_inst->dst.type)) {
                  /* If the destination type of scan_inst is floating-point,
                   * then:
                   *
                   * - The source of the MOV instruction must be the same
                   *   type.
                   *
                   * - The destination of the MOV instruction must be float
                   *   point with a size at least as large as the destination
                   *   of inst.  Size-reducing f2f conversions could cause
                   *   non-zero values to become zero, etc.
                   */
                  if (scan_inst->dst.type != inst->src[0].type)
                     break;

                  if (!brw_type_is_float(inst->dst.type))
                     break;

                  if (brw_type_size_bits(scan_inst->dst.type) >
                      brw_type_size_bits(inst->dst.type))
                     break;
               } else {
                  /* If the destination type of scan_inst is integer, then:
                   *
                   * - The source of the MOV instruction must be integer with
                   *   the same size.
                   *
                   * - If the conditional modifier is neither Z nor NZ, then
                   *   the source of the MOV instruction has to have same
                   *   signedness.
                   *
                   * - If the conditional modifier is Z or NZ, then the
                   *   destination type of inst must either be floating point
                   *   (of any size) or integer with a size at least as large
                   *   as the destination of inst.
                   *
                   * - If the conditional modifier is neither Z nor NZ, then the
                   *   destination type of inst must either be floating point
                   *   (of any size) or integer with a size at least as large
                   *   as the destination of inst and the same signedness.
                   */
                  if (!brw_type_is_int(inst->src[0].type) ||
                      brw_type_size_bits(scan_inst->dst.type) != brw_type_size_bits(inst->src[0].type))
                     break;

                  if (inst->conditional_mod != BRW_CONDITIONAL_Z &&
                      inst->conditional_mod != BRW_CONDITIONAL_NZ &&
                      brw_type_is_uint(inst->src[0].type) !=
                      brw_type_is_uint(scan_inst->dst.type))
                     break;

                  if (brw_type_is_int(inst->dst.type)) {
                     if (brw_type_size_bits(inst->dst.type) <
                         brw_type_size_bits(scan_inst->dst.type))
                        break;

                     if (inst->conditional_mod != BRW_CONDITIONAL_Z &&
                         inst->conditional_mod != BRW_CONDITIONAL_NZ &&
                         brw_type_is_uint(inst->dst.type) !=
                         brw_type_is_uint(scan_inst->dst.type))
                        break;
                  }
               }
            } else {
               /* Not safe to use inequality operators if the types are
                * different.
                */
               if (scan_inst->dst.type != inst->src[0].type &&
                   inst->conditional_mod != BRW_CONDITIONAL_Z &&
                   inst->conditional_mod != BRW_CONDITIONAL_NZ)
                  break;

               /* Comparisons operate differently for ints and floats */
               if (scan_inst->dst.type != inst->dst.type) {
                  /* Comparison result may be altered if the bit-size changes
                   * since that affects range, denorms, etc
                   */
                  if (brw_type_size_bits(scan_inst->dst.type) !=
                      brw_type_size_bits(inst->dst.type))
                     break;

                  if (brw_type_is_float(scan_inst->dst.type) !=
                      brw_type_is_float(inst->dst.type))
                     break;
               }
            }

            /* Knowing following:
             * - CMP writes to flag register the result of
             *   applying cmod to the `src0 - src1`.
             *   After that it stores the same value to dst.
             *   Other instructions first store their result to
             *   dst, and then store cmod(dst) to the flag
             *   register.
             * - inst is either CMP or MOV
             * - inst->dst is null
             * - inst->src[0] overlaps with scan_inst->dst
             * - inst->src[1] is zero
             * - scan_inst wrote to a flag register
             *
             * There can be three possible paths:
             *
             * - scan_inst is CMP:
             *
             *   Considering that src0 is either 0x0 (false),
             *   or 0xffffffff (true), and src1 is 0x0:
             *
             *   - If inst's cmod is NZ, we can always remove
             *     scan_inst: NZ is invariant for false and true. This
             *     holds even if src0 is NaN: .nz is the only cmod,
             *     that returns true for NaN.
             *
             *   - .g is invariant if src0 has a UD type
             *
             *   - .l is invariant if src0 has a D type
             *
             * - scan_inst and inst have the same cmod:
             *
             *   If scan_inst is anything than CMP, it already
             *   wrote the appropriate value to the flag register.
             *
             * - else:
             *
             *   We can change cmod of scan_inst to that of inst,
             *   and remove inst. It is valid as long as we make
             *   sure that no instruction uses the flag register
             *   between scan_inst and inst.
             */
            if (!inst->src[0].negate &&
                scan_inst->flags_written(devinfo)) {
               if (scan_inst->opcode == BRW_OPCODE_CMP) {
                  if ((inst->conditional_mod == BRW_CONDITIONAL_NZ) ||
                      (inst->conditional_mod == BRW_CONDITIONAL_G &&
                       inst->src[0].type == BRW_TYPE_UD) ||
                      (inst->conditional_mod == BRW_CONDITIONAL_L &&
                       inst->src[0].type == BRW_TYPE_D)) {
                     inst->remove();
                     progress = true;
                     break;
                  }
               } else if (scan_inst->conditional_mod == inst->conditional_mod) {
                  /* sel.cond will not write the flags. */
                  assert(scan_inst->opcode != BRW_OPCODE_SEL);
                  inst->remove();
                  progress = true;
                  break;
               } else if (!read_flag &&
                          scan_inst->can_do_cmod(inst->conditional_mod)) {
                  scan_inst->conditional_mod = inst->conditional_mod;
                  scan_inst->flag_subreg = inst->flag_subreg;
                  inst->remove();
                  progress = true;
                  break;
               }
            }

            /* The conditional mod of the CMP/CMPN instructions behaves
             * specially because the flag output is not calculated from the
             * result of the instruction, but the other way around, which
             * means that even if the condmod to propagate and the condmod
             * from the CMP instruction are the same they will in general give
             * different results because they are evaluated based on different
             * inputs.
             */
            if (scan_inst->opcode == BRW_OPCODE_CMP ||
                scan_inst->opcode == BRW_OPCODE_CMPN)
               break;

            /* From the Sky Lake PRM, Vol 2a, "Multiply":
             *
             *    "When multiplying integer data types, if one of the sources
             *     is a DW, the resulting full precision data is stored in
             *     the accumulator. However, if the destination data type is
             *     either W or DW, the low bits of the result are written to
             *     the destination register and the remaining high bits are
             *     discarded. This results in undefined Overflow and Sign
             *     flags. Therefore, conditional modifiers and saturation
             *     (.sat) cannot be used in this case."
             *
             * We just disallow cmod propagation on all integer multiplies.
             */
            if (!brw_type_is_float(scan_inst->dst.type) &&
                scan_inst->opcode == BRW_OPCODE_MUL)
               break;

            enum brw_conditional_mod cond =
               inst->src[0].negate ? brw_swap_cmod(inst->conditional_mod)
                                   : inst->conditional_mod;

            /* BFN supports a limited subset of possible conditions. It
             * notably lacks NZ. However, if the destination is unsigned, G
             * and NZ are equivalent.
             */
            if (scan_inst->opcode == BRW_OPCODE_BFN &&
                brw_type_is_uint(scan_inst->dst.type) &&
                cond == BRW_CONDITIONAL_NZ) {
               cond = BRW_CONDITIONAL_G;
            }

            /* From the Kaby Lake PRM Vol. 7 "Assigning Conditional Flags":
             *
             *    * Note that the [post condition signal] bits generated at
             *      the output of a compute are before the .sat.
             *
             * Paragraph about post_zero does not mention saturation, but
             * testing it on actual GPUs shows that conditional modifiers are
             * applied after saturation.
             *
             *    * post_zero bit: This bit reflects whether the final
             *      result is zero after all the clamping, normalizing,
             *      or format conversion logic.
             *
             * For this reason, no additional restrictions are necessary on
             * instructions with saturate.
             */

            /* Otherwise, try propagating the conditional. */
            if (scan_inst->can_do_cmod(cond) &&
                ((!read_flag && scan_inst->conditional_mod == BRW_CONDITIONAL_NONE) ||
                 scan_inst->conditional_mod == cond)) {
               scan_inst->conditional_mod = cond;
               scan_inst->flag_subreg = inst->flag_subreg;
               inst->remove();
               progress = true;
            }
            break;
         }

         if ((scan_inst->flags_written(devinfo) & flags_written) != 0)
            break;

         read_flag = read_flag ||
                     (scan_inst->flags_read(devinfo) & flags_written) != 0;
      }
   }

   return progress;
}

bool
brw_opt_cmod_propagation(brw_shader &s)
{
   bool progress = false;

   foreach_block_reverse(block, s.cfg) {
      progress = opt_cmod_propagation_local(s.devinfo, block) || progress;
   }

   if (progress) {
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);
   }

   return progress;
}
