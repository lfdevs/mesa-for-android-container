/*
 * Copyright © 2010 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_builder.h"

/**
 * Align16 3-source instructions cannot have scalar stride w/64-bit types.
 *
 * The Bspec says:
 *
 *    Replicate Control. This field is only present in three-source
 *    instructions, for each of the three source operands. It controls
 *    replication of the starting channel to all channels in the execution
 *    size. ChanSel does not apply when Replicate Control is set. This is
 *    applicable to 32b datatypes and 16b datatype. 64b datatypes cannot use
 *    the replicate control.
 *
 * In practice, this can only happen on Gfx9 with DF sources to MAD.  Since
 * the source is_scalar, this can be fixed by just making the stride=1. Also
 * clear is_scalar "just in case."
 */
bool
brw_lower_scalar_fp64_MAD(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   if (devinfo->ver != 9)
      return false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode == BRW_OPCODE_MAD &&
          inst->dst.type == BRW_TYPE_DF) {
         for (unsigned i = 0; i < 3; i++) {
            if (inst->src[i].is_scalar) {
               inst->src[i].is_scalar = false;
               inst->src[i].stride = 1;
               progress = true;
            }
         }
      }
   }

   return progress;
}

bool
brw_lower_load_payload(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe (block, brw_inst, inst, s.cfg) {
      if (inst->opcode != SHADER_OPCODE_LOAD_PAYLOAD)
         continue;

      brw_load_payload_inst *lp = inst->as_load_payload();

      assert(lp->dst.file == VGRF);
      assert(lp->saturate == false);
      brw_reg dst = lp->dst;

      const brw_builder ibld(lp);
      const brw_builder ubld = ibld.exec_all();

      for (uint8_t i = 0; i < lp->header_size;) {
         /* Number of header GRFs to initialize at once with a single MOV
          * instruction.
          */
         const unsigned n =
            (i + 1 < lp->header_size &&
             (lp->src[i].file == IMM ||
              (lp->src[i].is_contiguous() &&
               lp->src[i + 1].equals(byte_offset(lp->src[i], REG_SIZE))))) ?
            2 : 1;

         if (lp->src[i].file != BAD_FILE)
            ubld.group(8 * n, 0).MOV(retype(dst, BRW_TYPE_UD),
                                     retype(lp->src[i], BRW_TYPE_UD));

         dst = byte_offset(dst, n * REG_SIZE);
         i += n;
      }

      for (uint8_t i = lp->header_size; i < lp->sources; i++) {
         dst.type = lp->src[i].type;
         if (lp->src[i].file != BAD_FILE) {
            ibld.MOV(dst, lp->src[i]);
         }
         dst = offset(dst, ibld, 1);
      }

      lp->remove();
      progress = true;
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}

/**
 * Lower CSEL with unsupported types to CMP+SEL.
 *
 * Or, for unsigned ==/!= comparisons, simply change the types.
 */
bool
brw_lower_csel(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode != BRW_OPCODE_CSEL)
         continue;

      bool supported = false;
      enum brw_reg_type orig_type = inst->src[2].type;
      enum brw_reg_type new_type = orig_type;

      switch (orig_type) {
      case BRW_TYPE_F:
         /* Gfx9 CSEL can only do F */
         supported = true;
         break;
      case BRW_TYPE_HF:
      case BRW_TYPE_W:
      case BRW_TYPE_D:
         /* Gfx11+ CSEL can do HF, W, and D.  Note that we can't simply
          * retype integer ==/!= comparisons as float on earlier hardware
          * because it breaks for 0x8000000 and 0 (-0.0 == 0.0).
          */
         supported = devinfo->ver >= 11;
         break;
      case BRW_TYPE_UW:
      case BRW_TYPE_UD:
         /* CSEL doesn't support UW/UD but we can simply retype to use the
          * signed types when comparing with == or !=.
          */
         supported = devinfo->ver >= 11 &&
                     (inst->conditional_mod == BRW_CONDITIONAL_EQ ||
                      inst->conditional_mod == BRW_CONDITIONAL_NEQ);

         /* Bspec 47408, Gfx125+ CSEL does support the both signed and unsigned
          * integer types.
          */
         if (devinfo->verx10 < 125) {
            new_type = inst->src[2].type == BRW_TYPE_UD ?
                       BRW_TYPE_D : BRW_TYPE_W;
         }
         break;
      default:
         break;
      }

      if (!supported) {
         const brw_builder ibld(inst);

         /* CSEL: dst = src2 <op> 0 ? src0 : src1 */
         brw_reg zero = brw_imm_reg(orig_type);
         ibld.CMP(retype(brw_null_reg(), orig_type),
                  inst->src[2], zero, inst->conditional_mod);

         inst = brw_transform_inst(s, inst, BRW_OPCODE_SEL, 2);
         inst->predicate = BRW_PREDICATE_NORMAL;
         inst->conditional_mod = BRW_CONDITIONAL_NONE;
         progress = true;
      } else if (new_type != orig_type) {
         inst->src[0].type = new_type;
         inst->src[1].type = new_type;
         inst->src[2].type = new_type;
         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}

bool
brw_lower_sub_sat(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      const brw_builder ibld(inst);

      if (inst->opcode == SHADER_OPCODE_USUB_SAT ||
          inst->opcode == SHADER_OPCODE_ISUB_SAT) {
         /* The fundamental problem is the hardware performs source negation
          * at the bit width of the source.  If the source is 0x80000000D, the
          * negation is 0x80000000D.  As a result, subtractSaturate(0,
          * 0x80000000) will produce 0x80000000 instead of 0x7fffffff.  There
          * are at least three ways to resolve this:
          *
          * 1. Use the accumulator for the negated source.  The accumulator is
          *    33 bits, so our source 0x80000000 is sign-extended to
          *    0x1800000000.  The negation of which is 0x080000000.  This
          *    doesn't help for 64-bit integers (which are already bigger than
          *    33 bits).  There are also only 8 accumulators, so SIMD16 or
          *    SIMD32 instructions would have to be split into multiple SIMD8
          *    instructions.
          *
          * 2. Use slightly different math.  For any n-bit value x, we know (x
          *    >> 1) != -(x >> 1).  We can use this fact to only do
          *    subtractions involving (x >> 1).  subtractSaturate(a, b) ==
          *    subtractSaturate(subtractSaturate(a, (b >> 1)), b - (b >> 1)).
          *
          * 3. For unsigned sources, it is sufficient to replace the
          *    subtractSaturate with (a > b) ? a - b : 0.
          *
          * It may also be possible to use the SUBB instruction.  This
          * implicitly writes the accumulator, so it could only be used in the
          * same situations as #1 above.  It is further limited by only
          * allowing UD sources.
          */
         if (inst->exec_size == 8 && inst->src[0].type != BRW_TYPE_Q &&
             inst->src[0].type != BRW_TYPE_UQ) {
            brw_reg acc = retype(brw_acc_reg(inst->exec_size),
                                inst->src[1].type);

            ibld.MOV(acc, inst->src[1]);
            brw_inst *add = ibld.ADD(inst->dst, acc, inst->src[0]);
            add->saturate = true;
            add->src[0].negate = true;
         } else if (inst->opcode == SHADER_OPCODE_ISUB_SAT) {
            /* tmp = src1 >> 1;
             * dst = add.sat(add.sat(src0, -tmp), -(src1 - tmp));
             */
            brw_inst *add;

            brw_reg tmp = ibld.vgrf(inst->src[0].type);
            ibld.ASR(tmp, inst->src[1], brw_imm_ud(1));

            brw_reg s1_sub_t = ibld.ADD(inst->src[1], negate(tmp));
            brw_reg sat_s0_sub_t = ibld.ADD(inst->src[0], negate(tmp), &add);
            add->saturate = true;

            add = ibld.ADD(inst->dst, sat_s0_sub_t, negate(s1_sub_t));
            add->saturate = true;
         } else {
            /* a > b ? a - b : 0 */
            ibld.CMP(ibld.null_reg_d(), inst->src[0], inst->src[1],
                     BRW_CONDITIONAL_G);

            brw_inst *add = ibld.ADD(inst->dst, inst->src[0], inst->src[1]);
            add->src[1].negate = !add->src[1].negate;

            ibld.SEL(inst->dst, inst->dst, brw_imm_ud(0))
               ->predicate = BRW_PREDICATE_NORMAL;
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

/**
 * Transform barycentric vectors into the interleaved form expected by the PLN
 * instruction and returned by the Gfx7+ PI shared function.
 *
 * For channels 0-15 in SIMD16 mode they are expected to be laid out as
 * follows in the register file:
 *
 *    rN+0: X[0-7]
 *    rN+1: Y[0-7]
 *    rN+2: X[8-15]
 *    rN+3: Y[8-15]
 *
 * There is no need to handle SIMD32 here -- This is expected to be run after
 * SIMD lowering, since SIMD lowering relies on vectors having the standard
 * component layout.
 */
bool
brw_lower_barycentrics(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;

   if (s.stage != MESA_SHADER_FRAGMENT || devinfo->ver >= 20)
      return false;

   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->exec_size < 16)
         continue;

      const brw_builder ibld(inst);
      const brw_builder ubld = ibld.exec_all().group(8, 0);

      switch (inst->opcode) {
      case BRW_OPCODE_PLN: {
         assert(inst->exec_size == 16);
         const brw_reg tmp = ibld.vgrf(inst->src[1].type, 2);
         brw_reg srcs[4];

         for (unsigned i = 0; i < ARRAY_SIZE(srcs); i++)
            srcs[i] = horiz_offset(offset(inst->src[1], ibld, i % 2),
                                   8 * (i / 2));

         ubld.LOAD_PAYLOAD(tmp, srcs, ARRAY_SIZE(srcs), ARRAY_SIZE(srcs));

         inst->src[1] = tmp;
         progress = true;
         break;
      }
      case FS_OPCODE_INTERPOLATE_AT_SAMPLE:
      case FS_OPCODE_INTERPOLATE_AT_SHARED_OFFSET:
      case FS_OPCODE_INTERPOLATE_AT_PER_SLOT_OFFSET: {
         assert(inst->exec_size == 16);
         const brw_reg tmp = ibld.vgrf(inst->dst.type, 2);

         for (unsigned i = 0; i < 2; i++) {
            for (unsigned g = 0; g < inst->exec_size / 8; g++) {
               brw_inst *mov = ibld.after(inst).group(8, g)
                                  .MOV(horiz_offset(offset(inst->dst, ibld, i),
                                                    8 * g),
                                       offset(tmp, ubld, 2 * g + i));
               mov->predicate = inst->predicate;
               mov->predicate_inverse = inst->predicate_inverse;
               mov->flag_subreg = inst->flag_subreg;
            }
         }

         inst->dst = tmp;
         progress = true;
         break;
      }
      default:
         break;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

/**
 * Lower a derivative instruction as the floating-point difference of two
 * swizzles of the source, specified as \p swz0 and \p swz1.
 */
static bool
lower_derivative(brw_shader &s, brw_inst *inst,
                 unsigned swz0, unsigned swz1)
{
   const brw_builder ubld = brw_builder(inst).exec_all();
   const brw_reg tmp0 = ubld.vgrf(inst->src[0].type);
   const brw_reg tmp1 = ubld.vgrf(inst->src[0].type);

   ubld.emit(SHADER_OPCODE_QUAD_SWIZZLE, tmp0, inst->src[0], brw_imm_ud(swz0));
   ubld.emit(SHADER_OPCODE_QUAD_SWIZZLE, tmp1, inst->src[0], brw_imm_ud(swz1));

   inst = brw_transform_inst(s, inst, BRW_OPCODE_ADD);
   inst->src[0] = negate(tmp0);
   inst->src[1] = tmp1;

   return true;
}

/**
 * Lower derivative instructions on platforms where codegen cannot implement
 * them efficiently (i.e. XeHP).
 */
bool
brw_lower_derivatives(brw_shader &s)
{
   bool progress = false;

   if (s.devinfo->verx10 < 125)
      return false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode == FS_OPCODE_DDX_COARSE)
         progress |= lower_derivative(s, inst,
                                      BRW_SWIZZLE_XXXX, BRW_SWIZZLE_YYYY);

      else if (inst->opcode == FS_OPCODE_DDX_FINE)
         progress |= lower_derivative(s, inst,
                                      BRW_SWIZZLE_XXZZ, BRW_SWIZZLE_YYWW);

      else if (inst->opcode == FS_OPCODE_DDY_COARSE)
         progress |= lower_derivative(s, inst,
                                      BRW_SWIZZLE_XXXX, BRW_SWIZZLE_ZZZZ);

      else if (inst->opcode == FS_OPCODE_DDY_FINE)
         progress |= lower_derivative(s, inst,
                                      BRW_SWIZZLE_XYXY, BRW_SWIZZLE_ZWZW);
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

bool
brw_lower_find_live_channel(brw_shader &s)
{
   bool progress = false;

   bool packed_dispatch =
      brw_stage_has_packed_dispatch(s.devinfo, s.stage, s.max_polygons,
                                    s.prog_data);
   bool vmask =
      s.stage == MESA_SHADER_FRAGMENT &&
      brw_wm_prog_data(s.prog_data)->uses_vmask;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode != SHADER_OPCODE_FIND_LIVE_CHANNEL &&
          inst->opcode != SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL &&
          inst->opcode != SHADER_OPCODE_LOAD_LIVE_CHANNELS)
         continue;

      bool first = inst->opcode == SHADER_OPCODE_FIND_LIVE_CHANNEL;

      /* Getting the first active channel index is easy on Gfx8: Just find
       * the first bit set in the execution mask.  The register exists on
       * HSW already but it reads back as all ones when the current
       * instruction has execution masking disabled, so it's kind of
       * useless there.
       */

      const brw_builder ibld(inst);
      if (!inst->is_partial_write())
         ibld.emit_undef_for_dst(inst);

      const brw_builder ubld = brw_builder(inst).uniform();

      brw_reg exec_mask = ubld.vgrf(BRW_TYPE_UD);
      ubld.UNDEF(exec_mask);
      ubld.emit(SHADER_OPCODE_READ_ARCH_REG, exec_mask,
                                             retype(brw_mask_reg(0),
                                                    BRW_TYPE_UD));

      /* ce0 doesn't consider the thread dispatch mask (DMask or VMask),
       * so combine the execution and dispatch masks to obtain the true mask.
       *
       * If we're looking for the first live channel, and we have packed
       * dispatch, we can skip this step, as we know all dispatched channels
       * will appear at the front of the mask.
       */
      if (!(first && packed_dispatch)) {
         brw_reg mask = ubld.vgrf(BRW_TYPE_UD);
         ubld.UNDEF(mask);
         ubld.emit(SHADER_OPCODE_READ_ARCH_REG, mask,
                                                retype(brw_sr0_reg(vmask ? 3 : 2),
                                                       BRW_TYPE_UD));

         /* Quarter control has the effect of magically shifting the value of
          * ce0 so you'll get the first/last active channel relative to the
          * specified quarter control as result.
          */
         if (inst->group > 0)
            ubld.SHR(mask, mask, brw_imm_ud(align(inst->group, 8)));

         ubld.AND(mask, exec_mask, mask);
         exec_mask = mask;
      }

      switch (inst->opcode) {
      case SHADER_OPCODE_FIND_LIVE_CHANNEL:
         ubld.FBL(inst->dst, exec_mask);
         break;

      case SHADER_OPCODE_FIND_LAST_LIVE_CHANNEL: {
         brw_reg tmp = ubld.vgrf(BRW_TYPE_UD);
         ubld.UNDEF(tmp);
         ubld.LZD(tmp, exec_mask);
         ubld.ADD(inst->dst, negate(tmp), brw_imm_uw(31));
         break;
      }

      case SHADER_OPCODE_LOAD_LIVE_CHANNELS:
         ubld.MOV(inst->dst, exec_mask);
         break;

      default:
         UNREACHABLE("Impossible.");
      }

      inst->remove();
      progress = true;
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

/**
 * From the Skylake PRM Vol. 2a docs for sends:
 *
 *    "It is required that the second block of GRFs does not overlap with the
 *    first block."
 *
 * There are plenty of cases where we may accidentally violate this due to
 * having, for instance, both sources be the constant 0.  This little pass
 * just adds a new vgrf for the second payload and copies it over.
 */
bool
brw_lower_sends_overlapping_payload(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe (block, brw_inst, inst, s.cfg) {
      if (inst->opcode != SHADER_OPCODE_SEND)
         continue;

      brw_send_inst *send = inst->as_send();

      if (send->ex_mlen > 0 &&
          regions_overlap(send->src[SEND_SRC_PAYLOAD1],
                          send->mlen * REG_SIZE,
                          send->src[SEND_SRC_PAYLOAD2],
                          send->ex_mlen * REG_SIZE)) {
         const unsigned arg = send->mlen < send->ex_mlen ?
                              SEND_SRC_PAYLOAD1 : SEND_SRC_PAYLOAD2;
         const unsigned len = MIN2(send->mlen, send->ex_mlen);

         brw_reg tmp = retype(brw_allocate_vgrf_units(s, len), BRW_TYPE_UD);

         /* Sadly, we've lost all notion of channels and bit sizes at this
          * point.  Just WE_all it.
          */
         const brw_builder ibld = brw_builder(send).exec_all().group(16, 0);
         brw_reg copy_src = retype(send->src[arg], BRW_TYPE_UD);
         brw_reg copy_dst = tmp;
         for (unsigned i = 0; i < len; i += 2) {
            if (len == i + 1) {
               /* Only one register left; do SIMD8 */
               ibld.group(8, 0).MOV(copy_dst, copy_src);
            } else {
               ibld.MOV(copy_dst, copy_src);
            }
            copy_src = offset(copy_src, ibld, 1);
            copy_dst = offset(copy_dst, ibld, 1);
         }
         send->src[arg] = tmp;
         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

/**
 * Three source instruction must have a GRF destination register.
 * ARF NULL is not allowed.  Fix that up by allocating a temporary GRF.
 */
bool
brw_lower_3src_null_dest(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe (block, brw_inst, inst, s.cfg) {
      if (inst->is_3src(s.compiler) && inst->dst.is_null()) {
         inst->dst = retype(brw_allocate_vgrf_units(s, s.dispatch_width / 8),
                            inst->dst.type);
         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTION_DATA_FLOW |
                            BRW_DEPENDENCY_INSTRUCTION_DETAIL |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

static bool
unsupported_64bit_type(const intel_device_info *devinfo,
                       enum brw_reg_type type)
{
   return (!devinfo->has_64bit_float && type == BRW_TYPE_DF) ||
          (!devinfo->has_64bit_int && (type == BRW_TYPE_UQ ||
                                       type == BRW_TYPE_Q));
}

bool
brw_lower_bfloat_conversion(brw_shader &s, brw_inst *inst)
{
   assert(s.devinfo->has_bfloat16);
   assert(inst->dst.type == BRW_TYPE_BF || inst->src[0].type == BRW_TYPE_BF);

   if (inst->dst.type == inst->src[0].type) {
      /* Except for DPAS, instructions with only bfloat operands are
       * not supported, so just move the bits using UW.
       */
      inst->dst    = retype(inst->dst, BRW_TYPE_UW);
      inst->src[0] = retype(inst->src[0], BRW_TYPE_UW);
      return true;

   } else if (inst->dst.type == BRW_TYPE_BF &&
              byte_stride(inst->dst) == 2) {
      /* Converting to packed BF is not supported natively.  Using
       * ADD with -0.0f preserves NaN correctly.  Note +0.0f would
       * not work since it doesn't preserve -0.0f!
       */
      assert(inst->src[0].type == BRW_TYPE_F);
      inst = brw_transform_inst(s, inst, BRW_OPCODE_ADD);
      inst->src[1] = brw_imm_f(-0.0f);
      return true;

   } else if (inst->dst.type == BRW_TYPE_F &&
              byte_stride(inst->src[0]) != 2) {
      /* Converting from a unpacked BF is not supported natively. */
      const brw_builder ibld(inst);
      ibld.SHL(retype(inst->dst, BRW_TYPE_UD),
               retype(inst->src[0], BRW_TYPE_UW),
               brw_imm_uw(16));
      inst->remove();
      return true;
   }

   return false;
}

/**
 * Perform lowering to legalize the IR for various ALU restrictions.
 *
 * For example:
 * - Splitting 64-bit MOV/SEL into 2x32-bit where needed
 */
bool
brw_lower_alu_restrictions(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      switch (inst->opcode) {
      case BRW_OPCODE_MOV:
         if (unsupported_64bit_type(devinfo, inst->dst.type)) {
            assert(inst->dst.type == inst->src[0].type);
            assert(!inst->saturate);
            assert(!inst->src[0].abs);
            assert(!inst->src[0].negate);
            const brw_builder ibld(inst);

            enum brw_reg_type type = brw_type_with_size(inst->dst.type, 32);

            if (!inst->is_partial_write())
               ibld.emit_undef_for_dst(inst);

            ibld.MOV(subscript(inst->dst, type, 1),
                     subscript(inst->src[0], type, 1));
            ibld.MOV(subscript(inst->dst, type, 0),
                     subscript(inst->src[0], type, 0));

            inst->remove();
            progress = true;
         }

         if (inst->dst.type == BRW_TYPE_BF || inst->src[0].type == BRW_TYPE_BF)
            progress |= brw_lower_bfloat_conversion(s, inst);

         break;

      case BRW_OPCODE_MUL:
      case BRW_OPCODE_MAD: {
         /* BFloat16 restrictions:
          *
          *   "Bfloat16 not in Src1 of 2-source instructions involving
          *    multiplier."
          *
          * and
          *
          *   "Bfloat16 not allowed in Src2 of 3-source instructions
          *   involving multiplier."
          */
         brw_reg &last_src = inst->src[inst->sources - 1];
         if (last_src.type == BRW_TYPE_BF) {
            assert(devinfo->has_bfloat16);
            const brw_builder ibld = brw_builder(inst);

            brw_reg src2_as_f = ibld.vgrf(BRW_TYPE_F);
            brw_inst *conv = ibld.MOV(src2_as_f, last_src);
            brw_lower_bfloat_conversion(s, conv);
            last_src = src2_as_f;

            progress = true;
         }
         break;
      }

      case BRW_OPCODE_SEL:
         if (unsupported_64bit_type(devinfo, inst->dst.type)) {
            assert(inst->dst.type == inst->src[0].type);
            assert(!inst->saturate);
            assert(!inst->src[0].abs && !inst->src[0].negate);
            assert(!inst->src[1].abs && !inst->src[1].negate);
            assert(inst->conditional_mod == BRW_CONDITIONAL_NONE);
            const brw_builder ibld(inst);

            enum brw_reg_type type = brw_type_with_size(inst->dst.type, 32);

            if (!inst->is_partial_write())
               ibld.emit_undef_for_dst(inst);

            set_predicate(inst->predicate,
                          ibld.SEL(subscript(inst->dst, type, 0),
                                   subscript(inst->src[0], type, 0),
                                   subscript(inst->src[1], type, 0)));
            set_predicate(inst->predicate,
                          ibld.SEL(subscript(inst->dst, type, 1),
                                   subscript(inst->src[0], type, 1),
                                   subscript(inst->src[1], type, 1)));

            inst->remove();
            progress = true;
         }
         break;

      case SHADER_OPCODE_SHUFFLE:
      case SHADER_OPCODE_MOV_INDIRECT:
      case SHADER_OPCODE_BROADCAST:
         /* Gen12.5 adds the following region restriction:
          *
          *   "Vx1 and VxH indirect addressing for Float, Half-Float,
          *    Double-Float and Quad-Word data must not be used."
          *
          * We require the source and destination types to match so stomp to
          * an unsigned integer type.
          */
         assert(inst->src[0].type == inst->dst.type);
         inst->src[0].type = inst->dst.type = brw_type_with_size(BRW_TYPE_UD,
            brw_type_size_bits(inst->src[0].type));
         break;

      default:
         break;
      }
   }

   if (progress) {
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);
   }

   return progress;
}

brw_reg
brw_lower_vgrf_to_fixed_grf(const struct intel_device_info *devinfo,
                            const brw_inst *inst, const brw_reg &reg)
{
   if (reg.file != VGRF)
      return reg;

   brw_reg new_reg;

   if (reg.stride == 0) {
      new_reg = brw_vec1_grf(reg.nr, 0);
   } else if (reg.stride > 4) {
      assert(reg.stride * brw_type_size_bytes(reg.type) <= REG_SIZE);
      new_reg = brw_vecn_grf(1, reg.nr, 0);
      new_reg = stride(new_reg, reg.stride, 1, 0);
   } else {
      /* From the Haswell PRM:
       *
       *  "VertStride must be used to cross GRF register boundaries. This
       *   rule implies that elements within a 'Width' cannot cross GRF
       *   boundaries."
       *
       * The maximum width value that could satisfy this restriction is:
       */
      const unsigned reg_width =
         REG_SIZE / (reg.stride * brw_type_size_bytes(reg.type));

      /* Because the hardware can only split source regions at a whole
       * multiple of width during decompression (i.e. vertically), clamp
       * the value obtained above to the physical execution size of a
       * single decompressed chunk of the instruction:
       */
      const bool compressed = inst->dst.component_size(inst->exec_size) > REG_SIZE;
      const unsigned phys_width = compressed ? inst->exec_size / 2 :
                                  inst->exec_size;

      /* XXX - The equation above is strictly speaking not correct on
       *       hardware that supports unbalanced GRF writes -- On Gfx9+
       *       each decompressed chunk of the instruction may have a
       *       different execution size when the number of components
       *       written to each destination GRF is not the same.
       */

      const unsigned max_hw_width = 16;

      const unsigned width = MIN3(reg_width, phys_width, max_hw_width);
      new_reg = brw_vecn_grf(width, reg.nr, 0);
      new_reg = stride(new_reg, width * reg.stride, width, reg.stride);
   }

   new_reg = retype(new_reg, reg.type);
   new_reg = byte_offset(new_reg, reg.offset);
   new_reg.abs = reg.abs;
   new_reg.negate = reg.negate;
   new_reg.is_scalar = reg.is_scalar;

   return new_reg;
}

void
brw_lower_vgrfs_to_fixed_grfs(brw_shader &s)
{
   assert(s.grf_used || !"Must be called after register allocation");

   foreach_block_and_inst(block, brw_inst, inst, s.cfg) {
      assert(inst->dst.stride <= 4);
      inst->dst = brw_lower_vgrf_to_fixed_grf(s.devinfo, inst, inst->dst);
      for (int i = 0; i < inst->sources; i++) {
         inst->src[i] = brw_lower_vgrf_to_fixed_grf(s.devinfo, inst,
                                                    inst->src[i]);
      }
   }

   s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTION_DATA_FLOW |
                         BRW_DEPENDENCY_INSTRUCTION_DETAIL |
                         BRW_DEPENDENCY_VARIABLES);
}

static bool
brw_lower_send_gather_inst(brw_shader &s, brw_send_inst *inst)
{
   const intel_device_info *devinfo = s.devinfo;
   assert(devinfo->ver >= 30);

   const unsigned unit = reg_unit(devinfo);
   assert(unit == 2);

   assert(inst->opcode == SHADER_OPCODE_SEND_GATHER);
   assert(inst->sources > 2);
   assert(inst->src[2].file == BAD_FILE);

   unsigned count = 0;
   uint8_t regs[16] = {};

   const unsigned num_payload_sources = inst->sources - 3;
   assert(num_payload_sources > 0);

   /* Limited by Src0.Length in the SEND instruction. */
   assert(num_payload_sources < 16);

   for (unsigned i = 3; i < inst->sources; i++) {
      assert(inst->src[i].file == FIXED_GRF);
      assert(inst->src[i].nr % reg_unit(devinfo) == 0);

      unsigned nr = phys_nr(devinfo, inst->src[i]);
      assert(nr <= UINT8_MAX);
      regs[count++] = nr;
   }

   /* Fill out ARF scalar register with the physical register numbers
    * and use SEND_GATHER.
    */
   brw_builder ubld = brw_builder(inst).uniform();
   for (unsigned q = 0; q < DIV_ROUND_UP(count, 8); q++) {
      uint64_t v = 0;
      for (unsigned i = 0; i < 8; i++) {
         const uint64_t reg = regs[(q * 8) + i];
         v |= reg << (8 * i);
      }
      ubld.MOV(brw_s0(BRW_TYPE_UQ, q), brw_imm_uq(v));
   }

   inst->src[2] = brw_s0(BRW_TYPE_UD, 0);
   inst->mlen = count * unit;

   return true;
}

bool
brw_lower_send_gather(brw_shader &s)
{
   assert(s.devinfo->ver >= 30);
   assert(s.grf_used || !"Must be called after register allocation");

   bool progress = false;

   foreach_block_and_inst(block, brw_inst, inst, s.cfg) {
      if (inst->opcode == SHADER_OPCODE_SEND_GATHER)
         progress |= brw_lower_send_gather_inst(s, inst->as_send());
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

bool
brw_lower_load_subgroup_invocation(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode != SHADER_OPCODE_LOAD_SUBGROUP_INVOCATION)
         continue;

      const brw_builder abld =
         brw_builder(inst).annotate("SubgroupInvocation");
      const brw_builder ubld8 = abld.group(8, 0).exec_all();
      ubld8.UNDEF(inst->dst);

      if (inst->exec_size == 8) {
         assert(inst->dst.type == BRW_TYPE_UD);
         brw_reg uw = retype(inst->dst, BRW_TYPE_UW);
         ubld8.MOV(uw, brw_imm_v(0x76543210));
         ubld8.MOV(inst->dst, uw);
      } else {
         assert(inst->dst.type == BRW_TYPE_UW);
         ubld8.MOV(inst->dst, brw_imm_v(0x76543210));
         ubld8.ADD(byte_offset(inst->dst, 16), inst->dst, brw_imm_uw(8u));
         if (inst->exec_size > 16) {
            const brw_builder ubld16 = abld.group(16, 0).exec_all();
            ubld16.ADD(byte_offset(inst->dst, 32), inst->dst, brw_imm_uw(16u));
         }
      }

      inst->remove();
      progress = true;
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

bool
brw_lower_indirect_mov(brw_shader &s)
{
   bool progress = false;

   if (s.devinfo->ver < 20)
      return progress;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode == SHADER_OPCODE_MOV_INDIRECT) {
         if (brw_type_size_bytes(inst->src[0].type) > 1 &&
             brw_type_size_bytes(inst->dst.type) > 1) {
            continue;
         }

         assert(brw_type_size_bytes(inst->src[0].type) ==
                brw_type_size_bytes(inst->dst.type));

         const brw_builder ibld(inst);

         /* Extract unaligned part */
         uint16_t extra_offset = inst->src[0].offset & 0x1;
         brw_reg offset = ibld.ADD(inst->src[1], brw_imm_uw(extra_offset));

         /* Check if offset is odd or even so that we can choose either high or
          * low byte from the result.
          */
         brw_reg is_odd = ibld.AND(offset, brw_imm_ud(1));

         /* Make sure offset is word (2-bytes) aligned */
         offset = ibld.AND(offset, brw_imm_uw(~1));

         /* Indirect addressing(vx1 and vxh) not supported with UB/B datatype for
          * Src0, so change data type for src0 and dst to UW.
          */
         brw_reg dst = ibld.vgrf(BRW_TYPE_UW);

         /* Substract unaligned offset from src0 offset since we already
          * accounted unaligned part in the indirect byte offset.
          */
         brw_reg start = retype(inst->src[0], BRW_TYPE_UW);
         start.offset &= ~extra_offset;

         /* Adjust length to account extra offset. */
         assert(inst->src[2].file == IMM);
         brw_reg length = brw_imm_ud(inst->src[2].ud + extra_offset);

         ibld.emit(SHADER_OPCODE_MOV_INDIRECT, dst, start, offset, length);

         /* Select high byte if offset is odd otherwise select low byte. */
         brw_reg lo = ibld.AND(dst, brw_imm_uw(0xff));
         brw_reg hi = ibld.SHR(dst, brw_imm_uw(8));
         brw_reg result = ibld.vgrf(BRW_TYPE_UW);
         ibld.CSEL(result, hi, lo, is_odd, BRW_CONDITIONAL_NZ);

         /* Extra MOV needed here to convert back to the corresponding B type */
         ibld.MOV(inst->dst, result);

         inst->remove();
         progress = true;
      }
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS |
                            BRW_DEPENDENCY_VARIABLES);

   return progress;
}

