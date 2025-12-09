/*
 * Copyright © 2018 Intel Corporation
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
#include "brw_builder.h"

namespace {
   /* From the SKL PRM Vol 2a, "Move":
    *
    * "A mov with the same source and destination type, no source modifier,
    *  and no saturation is a raw move. A packed byte destination region (B
    *  or UB type with HorzStride == 1 and ExecSize > 1) can only be written
    *  using raw move."
    */
   bool
   is_byte_raw_mov(const brw_inst *inst)
   {
      return brw_type_size_bytes(inst->dst.type) == 1 &&
             inst->opcode == BRW_OPCODE_MOV &&
             inst->src[0].type == inst->dst.type &&
             !inst->saturate &&
             !inst->src[0].negate &&
             !inst->src[0].abs;
   }

   /*
    * Return an acceptable byte stride for the specified source of an
    * instruction affected by a regioning restriction.
    */
   unsigned
   required_src_byte_stride(const intel_device_info *devinfo, const brw_inst *inst,
                            unsigned i)
   {
      if (devinfo->has_bfloat16 && has_bfloat_operands(inst)) {
         return brw_type_size_bytes(inst->src[i].type);

      } else if (has_dst_aligned_region_restriction(devinfo, inst)) {
         return MAX2(brw_type_size_bytes(inst->dst.type),
                     byte_stride(inst->dst));

      } else if (has_subdword_integer_region_restriction(devinfo, inst,
                                                         &inst->src[i], 1)) {
         /* Use a stride of 32bits if possible, since that will guarantee that
          * the copy emitted to lower this region won't be affected by the
          * sub-dword integer region restrictions.  This may not be possible
          * for the second source of an instruction if we're required to use
          * packed data due to Wa_16012383669.
          */
         return (i == 1 ? brw_type_size_bytes(inst->src[i].type) : 4);

      } else {
         return byte_stride(inst->src[i]);
      }
   }

   /*
    * Return an acceptable byte sub-register offset for the specified source
    * of an instruction affected by a regioning restriction.
    */
   unsigned
   required_src_byte_offset(const intel_device_info *devinfo, const brw_inst *inst,
                            unsigned i)
   {
      if (has_dst_aligned_region_restriction(devinfo, inst)) {
         return reg_offset(inst->dst) % (reg_unit(devinfo) * REG_SIZE);

      } else if (has_subdword_integer_region_restriction(devinfo, inst,
                                                         &inst->src[i], 1)) {
         const unsigned dst_byte_stride =
            MAX2(byte_stride(inst->dst), brw_type_size_bytes(inst->dst.type));
         const unsigned src_byte_stride = required_src_byte_stride(devinfo, inst, i);
         const unsigned dst_byte_offset =
            reg_offset(inst->dst) % (reg_unit(devinfo) * REG_SIZE);
         const unsigned src_byte_offset =
            reg_offset(inst->src[i]) % (reg_unit(devinfo) * REG_SIZE);

         if (src_byte_stride > brw_type_size_bytes(inst->src[i].type)) {
            assert(src_byte_stride >= dst_byte_stride);
            /* The source is affected by the Xe2+ sub-dword integer regioning
             * restrictions.  For the case of source 0 BSpec#56640 specifies a
             * number of equations relating the source and destination
             * sub-register numbers in all cases where a source stride of
             * 32bits is allowed.  These equations have the form:
             *
             *   k * Dst.SubReg % m = Src.SubReg / l
             *
             * For some constants k, l and m different for each combination of
             * source and destination types and strides.  The expression in
             * the return statement below computes a valid source offset by
             * inverting the equation like:
             *
             *   Src.SubReg = l * k * (Dst.SubReg % m)
             *
             * and then scaling by the element type sizes in order to get an
             * expression in terms of byte offsets instead of sub-register
             * numbers.  It can be easily verified that in all cases listed on
             * the hardware spec where the source has a well-defined uniform
             * stride the product l*k is equal to the ratio between the source
             * and destination strides.
             */
            const unsigned m = 64 * dst_byte_stride / src_byte_stride;
            return dst_byte_offset % m * src_byte_stride / dst_byte_stride;
         } else {
            assert(src_byte_stride == brw_type_size_bytes(inst->src[i].type));
            /* A packed source is required, likely due to the stricter
             * requirements of the second source region.  The source being
             * packed guarantees that the region of the original instruction
             * will be valid, but the copy may break the regioning
             * restrictions.  Do our best to try to prevent that from
             * happening by making sure the offset of the temporary matches
             * the original source based on the same equation above -- However
             * that may not be sufficient if the source had a stride larger
             * than 32bits, lowering the copy recursively may be necessary.
             */
            return src_byte_offset * src_byte_stride / byte_stride(inst->src[i]);
         }

      } else {
         return reg_offset(inst->src[i]) % (reg_unit(devinfo) * REG_SIZE);
      }
   }

   /*
    * Return an acceptable byte stride for the destination of an instruction
    * that requires it to have some particular alignment.
    */
   unsigned
   required_dst_byte_stride(const intel_device_info *devinfo, const brw_inst *inst)
   {
      if (inst->dst.is_accumulator()) {
         /* If the destination is an accumulator, insist that we leave the
          * stride alone.  We cannot "fix" accumulator destinations by writing
          * to a temporary and emitting a MOV into the original destination.
          * For multiply instructions (our one use of the accumulator), the
          * MUL writes the full 66 bits of the accumulator whereas the MOV we
          * would emit only writes 33 bits and leaves the top 33 bits
          * undefined.
          *
          * It's safe to just require the original stride here because the
          * lowering pass will detect the mismatch in has_invalid_src_region
          * and fix the sources of the multiply instead of the destination.
          */
         return inst->dst.hstride * brw_type_size_bytes(inst->dst.type);

      } else if (devinfo->has_bfloat16 && has_bfloat_operands(inst)) {
         /* Prefer packed since it can be used as a source. */
         return brw_type_size_bytes(inst->dst.type);

      } else if (brw_type_size_bytes(inst->dst.type) < get_exec_type_size(inst) &&
          !is_byte_raw_mov(inst)) {
         return get_exec_type_size(inst);
      } else {
         /* Calculate the maximum byte stride and the minimum/maximum type
          * size across all source and destination operands we are required to
          * lower.
          */
         unsigned max_stride = inst->dst.stride * brw_type_size_bytes(inst->dst.type);
         unsigned min_size = brw_type_size_bytes(inst->dst.type);
         unsigned max_size = brw_type_size_bytes(inst->dst.type);

         for (unsigned i = 0; i < inst->sources; i++) {
            if (!is_uniform(inst->src[i]) && !inst->is_control_source(i)) {
               const unsigned size = brw_type_size_bytes(inst->src[i].type);
               max_stride = MAX2(max_stride, inst->src[i].stride * size);
               min_size = MIN2(min_size, size);
               max_size = MAX2(max_size, size);
            }
         }

         /* All operands involved in lowering need to fit in the calculated
          * stride.
          */
         assert(max_size <= 4 * min_size);

         /* Attempt to use the largest byte stride among all present operands,
          * but never exceed a stride of 4 since that would lead to illegal
          * destination regions during lowering.
          */
         return MIN2(max_stride, 4 * min_size);
      }
   }

   /*
    * Return an acceptable byte sub-register offset for the destination of an
    * instruction that requires it to be aligned to the sub-register offset of
    * the sources.
    */
   unsigned
   required_dst_byte_offset(const intel_device_info *devinfo, const brw_inst *inst)
   {
      assert(!brw_type_is_bfloat(inst->dst.type));

      for (unsigned i = 0; i < inst->sources; i++) {
         if (!is_uniform(inst->src[i]) && !inst->is_control_source(i))
            if (reg_offset(inst->src[i]) % (reg_unit(devinfo) * REG_SIZE) !=
                reg_offset(inst->dst) % (reg_unit(devinfo) * REG_SIZE))
               return 0;
      }

      return reg_offset(inst->dst) % (reg_unit(devinfo) * REG_SIZE);
   }

   /*
    * Return the closest legal execution type for an instruction on
    * the specified platform.
    */
   brw_reg_type
   required_exec_type(const intel_device_info *devinfo, const brw_inst *inst)
   {
      const brw_reg_type t = get_exec_type(inst);
      const bool has_64bit = brw_type_is_float(t) ?
         devinfo->has_64bit_float : devinfo->has_64bit_int;

      switch (inst->opcode) {
      case SHADER_OPCODE_SHUFFLE:
         /* IVB has an issue (which we found empirically) where it reads
          * two address register components per channel for indirectly
          * addressed 64-bit sources.
          *
          * From the Cherryview PRM Vol 7. "Register Region Restrictions":
          *
          *    "When source or destination datatype is 64b or operation is
          *    integer DWord multiply, indirect addressing must not be
          *    used."
          *
          * Work around both of the above and handle platforms that
          * don't support 64-bit types at all.
          */
         if ((!devinfo->has_64bit_int ||
              intel_device_info_is_9lp(devinfo) ||
              devinfo->ver >= 20) && brw_type_size_bytes(t) > 4)
            return BRW_TYPE_UD;
         else if (has_dst_aligned_region_restriction(devinfo, inst))
            return brw_int_type(brw_type_size_bytes(t), false);
         else
            return t;

      case SHADER_OPCODE_SEL_EXEC:
         if ((!has_64bit || devinfo->has_64bit_float_via_math_pipe) &&
             brw_type_size_bytes(t) > 4)
            return BRW_TYPE_UD;
         else
            return t;

      case SHADER_OPCODE_QUAD_SWIZZLE:
         if (has_dst_aligned_region_restriction(devinfo, inst))
            return brw_int_type(brw_type_size_bytes(t), false);
         else
            return t;

      case SHADER_OPCODE_CLUSTER_BROADCAST:
         /* From the Cherryview PRM Vol 7. "Register Region Restrictions":
          *
          *    "When source or destination datatype is 64b or operation is
          *    integer DWord multiply, indirect addressing must not be
          *    used."
          *
          * For MTL (verx10 == 125), float64 is supported, but int64 is not.
          * Therefore we need to lower cluster broadcast using 32-bit int ops.
          *
          * For gfx12.5+ platforms that support int64, the register regions
          * used by cluster broadcast aren't supported by the 64-bit pipeline.
          *
          * Work around the above and handle platforms that don't
          * support 64-bit types at all.
          */
         if ((!has_64bit || devinfo->verx10 >= 125 ||
              intel_device_info_is_9lp(devinfo) ||
              devinfo->ver >= 20) && brw_type_size_bytes(t) > 4)
            return BRW_TYPE_UD;
         else
            return brw_int_type(brw_type_size_bytes(t), false);

      default:
         return t;
      }
   }

   /*
    * Return whether the instruction has an unsupported channel bit layout
    * specified for the i-th source region.
    */
   bool
   has_invalid_src_region(const intel_device_info *devinfo, const brw_inst *inst,
                          unsigned i)
   {
      /* Wa_22016140776:
       *
       *    Scalar broadcast on HF math (packed or unpacked) must not be used.
       *    Compiler must use a mov instruction to expand the scalar value to
       *    a vector before using in a HF (packed or unpacked) math operation.
       */
      if (inst->is_math() && intel_needs_workaround(devinfo, 22016140776) &&
          is_uniform(inst->src[i]) && inst->src[i].type == BRW_TYPE_HF) {
         return true;
      }

      if (inst->is_send() || inst->is_control_source(i) ||
          inst->opcode == BRW_OPCODE_DPAS) {
         return false;
      }

      const unsigned dst_byte_offset = reg_offset(inst->dst) % (reg_unit(devinfo) * REG_SIZE);
      const unsigned src_byte_offset = reg_offset(inst->src[i]) % (reg_unit(devinfo) * REG_SIZE);

      if (devinfo->has_bfloat16 && has_bfloat_operands(inst)) {
         if (brw_type_is_bfloat(inst->src[i].type)) {
            const unsigned half_register = REG_SIZE * reg_unit(devinfo) / 2;
            const unsigned offset = reg_offset(inst->src[i]);

            /* Region restrictions described by PRM
             *
             *   Bfloat16 source must be packed.
             *
             *   Bfloat16 source must have register offset 0 or half of GRF register.
             */
            return !(byte_stride(inst->src[i]) == 2 && (offset == 0 || offset == half_register));
         } else {
            assert(inst->src[i].type == BRW_TYPE_F);
            /* Restrict Floats sources mixed with BFloats to also be aligned and packed. */
            return !is_uniform(inst->src[i]) && src_byte_offset != 0 && byte_stride(inst->src[i]) != 4;
         }
      }

      return (has_dst_aligned_region_restriction(devinfo, inst) &&
              !is_uniform(inst->src[i]) &&
              (byte_stride(inst->src[i]) != required_src_byte_stride(devinfo, inst, i) ||
               src_byte_offset != dst_byte_offset)) ||
             (has_subdword_integer_region_restriction(devinfo, inst) &&
              (byte_stride(inst->src[i]) != required_src_byte_stride(devinfo, inst, i) ||
               src_byte_offset != required_src_byte_offset(devinfo, inst, i)));
   }

   /*
    * Return whether the instruction has an unsupported channel bit layout
    * specified for the destination region.
    */
   bool
   has_invalid_dst_region(const intel_device_info *devinfo,
                          const brw_inst *inst)
   {
      if (inst->is_send()) {
         return false;

      } else if (devinfo->has_bfloat16 && has_bfloat_operands(inst)) {
         const unsigned stride = byte_stride(inst->dst);
         const unsigned offset = reg_offset(inst->dst);
         const unsigned half_register = REG_SIZE * reg_unit(devinfo) / 2;

         /* Region restrictions described by PRM
          *
          *   Packed bfloat16 destination must have register offset of 0 or half of GRF register.
          *
          *   Unpacked bfloat16 destination must have stride 2 and register offset 0 or 1.
          *
          * Note numbers above are in terms of elements (2 bytes).
          */
         if (inst->dst.type == BRW_TYPE_BF) {
            return !(stride == 2 && (offset == 0 || offset == half_register)) &&
                   !(stride == 4 && (offset == 0 || offset == 2));
         } else {
            assert(inst->dst.type == BRW_TYPE_F);
            /* Restrict Floats sources mixed with BFloats to also be aligned and packed. */
            return !(stride == 4 && offset == 0);
         }

      } else {
         const brw_reg_type exec_type = get_exec_type(inst);
         const unsigned dst_byte_offset = reg_offset(inst->dst) % (reg_unit(devinfo) * REG_SIZE);
         const bool is_narrowing_conversion = !is_byte_raw_mov(inst) &&
            brw_type_size_bytes(inst->dst.type) < brw_type_size_bytes(exec_type);

         return (has_dst_aligned_region_restriction(devinfo, inst) &&
                 (required_dst_byte_stride(devinfo, inst) != byte_stride(inst->dst) ||
                  required_dst_byte_offset(devinfo, inst) != dst_byte_offset)) ||
                (is_narrowing_conversion &&
                 required_dst_byte_stride(devinfo, inst) != byte_stride(inst->dst));
      }
   }

   /**
    * Return a non-zero value if the execution type of the instruction is
    * unsupported.  The destination and sources matching the returned mask
    * will be bit-cast to an integer type of appropriate size, lowering any
    * source or destination modifiers into separate MOV instructions.
    */
   unsigned
   has_invalid_exec_type(const intel_device_info *devinfo, const brw_inst *inst)
   {
      if (required_exec_type(devinfo, inst) != get_exec_type(inst)) {
         switch (inst->opcode) {
         case SHADER_OPCODE_SHUFFLE:
         case SHADER_OPCODE_QUAD_SWIZZLE:
         case SHADER_OPCODE_CLUSTER_BROADCAST:
         case SHADER_OPCODE_BROADCAST:
         case SHADER_OPCODE_MOV_INDIRECT:
            return 0x1;

         case SHADER_OPCODE_SEL_EXEC:
            return 0x3;

         default:
            UNREACHABLE("Unknown invalid execution type source mask.");
         }
      } else {
         return 0;
      }
   }

   /**
    * Return whether the instruction has an unsupported type conversion
    * that must be handled by expanding the source operand.
    */
   bool
   has_invalid_src_conversion(const intel_device_info *devinfo,
                              const brw_inst *inst)
   {
      /* Scalar byte to float conversion is not allowed on DG2+ */
      return devinfo->verx10 >= 125 &&
             inst->opcode == BRW_OPCODE_MOV &&
             brw_type_is_float(inst->dst.type) &&
             brw_type_size_bits(inst->src[0].type) == 8 &&
             is_uniform(inst->src[0]);
   }

   /*
    * Return whether the instruction has unsupported source modifiers
    * specified for the i-th source region.
    */
   bool
   has_invalid_src_modifiers(const intel_device_info *devinfo,
                             const brw_inst *inst, unsigned i)
   {
      return (!inst->can_do_source_mods(devinfo) &&
              (inst->src[i].negate || inst->src[i].abs)) ||
             ((has_invalid_exec_type(devinfo, inst) & (1u << i)) &&
              (inst->src[i].negate || inst->src[i].abs ||
               inst->src[i].type != get_exec_type(inst))) ||
             has_invalid_src_conversion(devinfo, inst);
   }

   /*
    * Return whether the instruction has an unsupported type conversion
    * specified for the destination.
    */
   bool
   has_invalid_conversion(const intel_device_info *devinfo, const brw_inst *inst)
   {
      switch (inst->opcode) {
      case BRW_OPCODE_MOV:
         return false;
      case BRW_OPCODE_SEL:
         return inst->dst.type != get_exec_type(inst);
      default:
         /* FIXME: We assume the opcodes not explicitly mentioned before just
          * work fine with arbitrary conversions, unless they need to be
          * bit-cast.
          */
         return has_invalid_exec_type(devinfo, inst) &&
                inst->dst.type != get_exec_type(inst);
      }
   }

   /**
    * Return whether the instruction has unsupported destination modifiers.
    */
   bool
   has_invalid_dst_modifiers(const intel_device_info *devinfo, const brw_inst *inst)
   {
      return (has_invalid_exec_type(devinfo, inst) &&
              (inst->saturate || inst->conditional_mod)) ||
             has_invalid_conversion(devinfo, inst);
   }

   /**
    * Return whether the instruction has non-standard semantics for the
    * conditional mod which don't cause the flag register to be updated with
    * the comparison result.
    */
   bool
   has_inconsistent_cmod(const brw_inst *inst)
   {
      return inst->opcode == BRW_OPCODE_SEL ||
             inst->opcode == BRW_OPCODE_CSEL ||
             inst->opcode == BRW_OPCODE_IF ||
             inst->opcode == BRW_OPCODE_WHILE;
   }

   bool
   lower_instruction(brw_shader *v, brw_inst *inst);
}

/**
 * Remove any modifiers from the \p i-th source region of the instruction,
 * including negate, abs and any implicit type conversion to the execution
 * type.  Instead any source modifiers will be implemented as a separate
 * MOV instruction prior to the original instruction.
 */
bool
brw_lower_src_modifiers(brw_shader &s, brw_inst *inst, unsigned i)
{
   assert(inst->components_read(i) == 1);
   assert(s.devinfo->has_integer_dword_mul ||
          inst->opcode != BRW_OPCODE_MUL ||
          brw_type_is_float(get_exec_type(inst)) ||
          MIN2(brw_type_size_bytes(inst->src[0].type), brw_type_size_bytes(inst->src[1].type)) >= 4 ||
          brw_type_size_bytes(inst->src[i].type) == get_exec_type_size(inst));

   const brw_builder ibld(inst);
   const brw_reg tmp = ibld.vgrf(get_exec_type(inst));

   lower_instruction(&s, ibld.MOV(tmp, inst->src[i]));
   inst->src[i] = tmp;

   return true;
}

namespace {
   /**
    * Remove any modifiers from the destination region of the instruction,
    * including saturate, conditional mod and any implicit type conversion
    * from the execution type.  Instead any destination modifiers will be
    * implemented as a separate MOV instruction after the original
    * instruction.
    */
   bool
   lower_dst_modifiers(brw_shader *v, brw_inst *inst)
   {
      const brw_builder ibld(inst);
      const brw_reg_type type = get_exec_type(inst);
      /* Not strictly necessary, but if possible use a temporary with the same
       * channel alignment as the current destination in order to avoid
       * violating the restrictions enforced later on by lower_src_region()
       * and lower_dst_region(), which would introduce additional copy
       * instructions into the program unnecessarily.
       */
      const unsigned stride =
         brw_type_size_bytes(inst->dst.type) * inst->dst.stride <= brw_type_size_bytes(type) ? 1 :
         brw_type_size_bytes(inst->dst.type) * inst->dst.stride / brw_type_size_bytes(type);
      brw_reg tmp = ibld.vgrf(type, stride);
      ibld.UNDEF(tmp);
      tmp = horiz_stride(tmp, stride);

      /* Emit a MOV taking care of all the destination modifiers. */
      brw_inst *mov = ibld.after(inst).MOV(inst->dst, tmp);
      mov->saturate = inst->saturate;
      if (!has_inconsistent_cmod(inst))
         mov->conditional_mod = inst->conditional_mod;
      if (inst->opcode != BRW_OPCODE_SEL) {
         mov->predicate = inst->predicate;
         mov->predicate_inverse = inst->predicate_inverse;
      }
      mov->flag_subreg = inst->flag_subreg;
      lower_instruction(v, mov);

      /* Point the original instruction at the temporary, and clean up any
       * destination modifiers.
       */
      assert(inst->size_written == inst->dst.component_size(inst->exec_size));
      inst->dst = tmp;
      inst->size_written = inst->dst.component_size(inst->exec_size);
      inst->saturate = false;
      if (!has_inconsistent_cmod(inst))
         inst->conditional_mod = BRW_CONDITIONAL_NONE;

      assert(!inst->flags_written(v->devinfo) || !mov->predicate);
      return true;
   }

   /**
    * Remove any non-trivial shuffling of data from the \p i-th source region
    * of the instruction.  Instead implement the region as a series of integer
    * copies into a temporary with the same channel layout as the destination.
    */
   bool
   lower_src_region(brw_shader *v, brw_inst *inst, unsigned i)
   {
      assert(inst->components_read(i) == 1);
      const intel_device_info *devinfo = v->devinfo;
      const brw_builder ibld(inst);
      const unsigned stride = required_src_byte_stride(devinfo, inst, i) /
                              brw_type_size_bytes(inst->src[i].type);
      assert(stride > 0);
      /* Calculate the size of the temporary allocation manually instead of
       * relying on the builder, since we may have to add some amount of
       * padding mandated by the hardware for Xe2+ instructions with sub-dword
       * integer regions.
       */
      const unsigned size =
         DIV_ROUND_UP(required_src_byte_offset(v->devinfo, inst, i) +
                      inst->exec_size * stride *
                      brw_type_size_bytes(inst->src[i].type),
                      reg_unit(devinfo) * REG_SIZE) * reg_unit(devinfo);
      brw_reg tmp = retype(brw_allocate_vgrf_units(*v, size), inst->src[i].type);
      ibld.UNDEF(tmp);
      tmp = byte_offset(horiz_stride(tmp, stride),
                        required_src_byte_offset(devinfo, inst, i));

      /* Emit a series of 32-bit integer copies with any source modifiers
       * cleaned up (because their semantics are dependent on the type).
       */
      const brw_reg_type raw_type = brw_int_type(MIN2(brw_type_size_bytes(tmp.type), 4),
                                                 false);
      const unsigned n = brw_type_size_bytes(tmp.type) / brw_type_size_bytes(raw_type);
      brw_reg raw_src = inst->src[i];
      raw_src.negate = false;
      raw_src.abs = false;

      for (unsigned j = 0; j < n; j++) {
	brw_inst *jnst = ibld.MOV(subscript(tmp, raw_type, j),
				 subscript(raw_src, raw_type, j));
	if (has_subdword_integer_region_restriction(devinfo, jnst)) {
           /* The copy isn't guaranteed to comply with all subdword integer
            * regioning restrictions in some cases.  Lower it recursively.
            */
	   lower_instruction(v, jnst);
        }
      }

      /* Point the original instruction at the temporary, making sure to keep
       * any source modifiers in the instruction.
       */
      brw_reg lower_src = tmp;
      lower_src.negate = inst->src[i].negate;
      lower_src.abs = inst->src[i].abs;
      inst->src[i] = lower_src;

      return true;
   }

   /**
    * Remove any non-trivial shuffling of data from the destination region of
    * the instruction.  Instead implement the region as a series of integer
    * copies from a temporary with a channel layout compatible with the
    * sources.
    */
   bool
   lower_dst_region(brw_shader *v, brw_inst *inst)
   {
      /* We cannot replace the result of an integer multiply which writes the
       * accumulator because MUL+MACH pairs act on the accumulator as a 66-bit
       * value whereas the MOV will act on only 32 or 33 bits of the
       * accumulator.
       */
      assert(inst->opcode != BRW_OPCODE_MUL || !inst->dst.is_accumulator() ||
             brw_type_is_float(inst->dst.type));

      const brw_builder ibld(inst);
      const unsigned stride = required_dst_byte_stride(v->devinfo, inst) /
                              brw_type_size_bytes(inst->dst.type);
      assert(stride > 0);
      brw_reg tmp = ibld.vgrf(inst->dst.type, stride);
      ibld.UNDEF(tmp);
      tmp = horiz_stride(tmp, stride);

      if (!inst->dst.is_null()) {
         /* Emit a series of 32-bit integer copies from the temporary into the
          * original destination.
          */
         const brw_reg_type raw_type =
            brw_int_type(MIN2(brw_type_size_bytes(tmp.type), 4), false);

         const unsigned n =
            brw_type_size_bytes(tmp.type) / brw_type_size_bytes(raw_type);

         if (inst->predicate && inst->opcode != BRW_OPCODE_SEL) {
            /* Note that in general we cannot simply predicate the copies on
             * the same flag register as the original instruction, since it
             * may have been overwritten by the instruction itself.  Instead
             * initialize the temporary with the previous contents of the
             * destination register.
             */
            for (unsigned j = 0; j < n; j++)
               ibld.MOV(subscript(tmp, raw_type, j),
                        subscript(inst->dst, raw_type, j));
         }

         for (unsigned j = 0; j < n; j++) {
            brw_inst *jnst = ibld.after(inst).MOV(subscript(inst->dst, raw_type, j),
                                                  subscript(tmp, raw_type, j));
            if (has_subdword_integer_region_restriction(v->devinfo, jnst)) {
               /* The copy isn't guaranteed to comply with all subdword integer
                * regioning restrictions in some cases.  Lower it recursively.
                */
               lower_instruction(v, jnst);
            }
         }

         /* If the destination was an accumulator, after lowering it will be a
          * GRF. Clear writes_accumulator for the instruction.
          */
         if (inst->dst.is_accumulator())
            inst->writes_accumulator = false;
      }

      /* Point the original instruction at the temporary, making sure to keep
       * any destination modifiers in the instruction.
       */
      assert(inst->size_written == inst->dst.component_size(inst->exec_size));
      inst->dst = tmp;
      inst->size_written = inst->dst.component_size(inst->exec_size);

      return true;
   }

   /**
    * Change sources and destination of the instruction to an
    * appropriate legal type, splitting the instruction into multiple
    * ones of smaller execution type if necessary, to be used in cases
    * where the execution type of an instruction is unsupported.
    */
   bool
   lower_exec_type(brw_shader *v, brw_inst *inst)
   {
      assert(inst->dst.type == get_exec_type(inst));
      const unsigned mask = has_invalid_exec_type(v->devinfo, inst);
      const brw_reg_type raw_type = required_exec_type(v->devinfo, inst);
      const unsigned n = get_exec_type_size(inst) / brw_type_size_bytes(raw_type);
      const brw_builder ibld(inst);

      brw_reg tmp = ibld.vgrf(inst->dst.type, inst->dst.stride);
      ibld.UNDEF(tmp);
      tmp = horiz_stride(tmp, inst->dst.stride);

      for (unsigned j = 0; j < n; j++) {
         brw_inst *sub_inst = brw_clone_inst(*v, inst);

         for (unsigned i = 0; i < inst->sources; i++) {
            if (mask & (1u << i)) {
               assert(inst->src[i].type == inst->dst.type);
               sub_inst->src[i] = subscript(inst->src[i], raw_type, j);
            }
         }

         sub_inst->dst = subscript(tmp, raw_type, j);

         assert(sub_inst->size_written == sub_inst->dst.component_size(sub_inst->exec_size));
         assert(!sub_inst->flags_written(v->devinfo) && !sub_inst->saturate);
         ibld.emit(sub_inst);

         brw_inst *mov = ibld.MOV(subscript(inst->dst, raw_type, j),
                                 subscript(tmp, raw_type, j));
         if (inst->opcode != BRW_OPCODE_SEL) {
            mov->predicate = inst->predicate;
            mov->predicate_inverse = inst->predicate_inverse;
         }
         lower_instruction(v, mov);
      }

      inst->remove();

      return true;
   }

   /**
    * Fast-path for very specific kinds of invalid regions.
    *
    * Gfx12.5+ does not allow moves of B or UB sources to floating-point
    * destinations. This restriction can be resolved more efficiently than by
    * the general lowering in lower_src_modifiers or lower_src_region.
    */
   void
   lower_src_conversion(brw_shader *v, brw_inst *inst)
   {
      const intel_device_info *devinfo = v->devinfo;
      const brw_builder ibld = brw_builder(inst).scalar_group();

      /* We only handle scalar conversions from small types for now. */
      assert(is_uniform(inst->src[0]));

      brw_reg tmp = ibld.vgrf(brw_type_with_size(inst->src[0].type, 32));
      brw_inst *mov = ibld.MOV(tmp, inst->src[0]);

      inst->src[0] = component(tmp, 0);

      /* Assert that neither the added MOV nor the original instruction will need
       * any additional lowering.
       */
      assert(!has_invalid_src_region(devinfo, mov, 0));
      assert(!has_invalid_src_modifiers(devinfo, mov, 0));
      assert(!has_invalid_dst_region(devinfo, mov));

      assert(!has_invalid_src_region(devinfo, inst, 0));
      assert(!has_invalid_src_modifiers(devinfo, inst, 0));
   }

   /**
    * Legalize the source and destination regioning controls of the specified
    * instruction.
    */
   bool
   lower_instruction(brw_shader *v, brw_inst *inst)
   {
      const intel_device_info *devinfo = v->devinfo;
      bool progress = false;

      /* BROADCAST is special. It's destination region is a bit of a lie, and
       * it gets lower in brw_eu_emit. For the purposes of region
       * restrictions, let's assume that the final code emission will do the
       * right thing. Doing a bunch of shuffling here is only going to make a
       * mess of things.
       */
      if (inst->opcode == SHADER_OPCODE_BROADCAST)
         return false;

      if (has_invalid_dst_modifiers(devinfo, inst))
         progress |= lower_dst_modifiers(v, inst);

      if (has_invalid_dst_region(devinfo, inst))
         progress |= lower_dst_region(v, inst);

      if (has_invalid_src_conversion(devinfo, inst)) {
         lower_src_conversion(v, inst);
         progress = true;
      }

      for (unsigned i = 0; i < inst->sources; i++) {
         if (has_invalid_src_modifiers(devinfo, inst, i))
            progress |= brw_lower_src_modifiers(*v, inst, i);

         if (has_invalid_src_region(devinfo, inst, i))
            progress |= lower_src_region(v, inst, i);
      }

      if (has_invalid_exec_type(devinfo, inst))
         progress |= lower_exec_type(v, inst);

      return progress;
   }
}

bool
brw_lower_regioning(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg)
      progress |= lower_instruction(&s, inst);

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}
