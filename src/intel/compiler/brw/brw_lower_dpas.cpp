/*
 * Copyright 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_builder.h"

static void
hf16_using_alu(const brw_builder &bld, brw_dpas_inst *dpas)
{
   const intel_device_info *devinfo = bld.shader->devinfo;

   /* We only intend to support configurations where the destination and
    * accumulator have the same type.
    */
   if (!dpas->src[0].is_null())
      assert(dpas->dst.type == dpas->src[0].type);

   assert(dpas->dst.type == BRW_TYPE_F ||
          dpas->dst.type == BRW_TYPE_HF);
   assert(dpas->src[1].type == BRW_TYPE_HF);
   assert(dpas->src[2].type == BRW_TYPE_HF);

   bool use_mac = devinfo->ver < 35;
   const brw_reg_type src0_type = dpas->dst.type;
   const brw_reg_type src1_type = BRW_TYPE_HF;
   const brw_reg_type src2_type = BRW_TYPE_HF;

   const brw_reg dest = dpas->dst;
   brw_reg src0 = dpas->src[0];
   const brw_reg src1 = retype(dpas->src[1], src1_type);
   const brw_reg src2 = retype(dpas->src[2], src2_type);

   const unsigned dest_stride = dpas->exec_size * brw_type_size_bytes(dest.type);

   brw_reg acc = retype(brw_acc_reg(bld.dispatch_width()), BRW_TYPE_F);
   if (devinfo->ver < 20) {
      /* FINISHME: The accumulator can actually hold 16 HF values. On Gfx12
       * there are two accumulators. It should be possible to do this in
       * SIMD16 or even SIMD32. I was unable to get this to work properly.
       */
      const unsigned acc_width = 8;
      acc = suboffset(retype(brw_acc_reg(dpas->exec_size), BRW_TYPE_UD),
                      dpas->group % acc_width);

      if (devinfo->verx10 >= 125)
         acc = subscript(acc, BRW_TYPE_HF, 0);
      else
         acc = retype(acc, BRW_TYPE_HF);
   }

   for (unsigned r = 0; r < dpas->rcount; r++) {
      brw_reg src2_r = byte_offset(src2, r * dpas->sdepth * 4);
      brw_reg dot = bld.vgrf(acc.type);

      for (unsigned subword = 0; subword < 2; subword++) {
         for (unsigned s = 0; s < dpas->sdepth; s++) {
            brw_reg src1_hf =
               subscript(retype(byte_offset(src1, s * dpas->exec_size *
                                                  brw_type_size_bytes(BRW_TYPE_UD)),
                                BRW_TYPE_UD),
                         src1_type, subword);
            brw_reg src2_hf =
               component(retype(src2_r, src2_type), s * 2 + subword);

            brw_reg mul_src1 = src1_hf;
            brw_reg mul_src2 = src2_hf;
            if (acc.type == BRW_TYPE_F) {
               mul_src1 = bld.vgrf(BRW_TYPE_F);
               mul_src2 = bld.vgrf(BRW_TYPE_F);
               bld.MOV(mul_src1, src1_hf);
               bld.MOV(mul_src2, src2_hf);
            }

            bool first = s == 0 && subword == 0;
            bool last = subword == 1 && (s + 1) == dpas->sdepth;

            /* The first multiply of the dot-product operation has to
             * explicitly write the accumulator register. The successive MAC
             * instructions will implicitly read *and* write the
             * accumulator. Those MAC instructions can also optionally
             * explicitly write some other register.
             *
             * On Gfx20, the accumulator write control bit is gone.  The
             * intermediate MACs have to explicitly write acc.
             *
             * On Gfx35+, MAC is gone, so use MAD.
             */
            brw_reg dst = dot;
            if (use_mac && !last) {
               if (first || acc.type == BRW_TYPE_F) {
                  dst = acc;
               } else {
                  /* As mentioned above, the MAC had an optional, explicit
                   * destination register. Various optimization passes are not
                   * clever enough to understand the intricacies of this
                   * instruction, so only write the result register on the final
                   * MAC in the sequence.
                   */
                  dst = retype(bld.null_reg_ud(), BRW_TYPE_HF);
               }
            }

            brw_inst *inst;
            if (first) {
               inst = bld.MUL(dst, mul_src1, mul_src2);
            } else if (use_mac) {
               inst = bld.MAC(dst, mul_src1, mul_src2);
            } else {
               inst = bld.MAD(dst, dot, mul_src1, mul_src2);
            }

            if (devinfo->ver < 20)
               inst->writes_accumulator = true;
         }
      }

      if (!src0.is_null()) {
         if (acc.type == BRW_TYPE_F) {
            brw_reg sum = bld.vgrf(BRW_TYPE_F);
            brw_reg src0_temp = bld.vgrf(BRW_TYPE_F);

            bld.MOV(src0_temp, byte_offset(src0, r * dest_stride));
            bld.ADD(sum, dot, src0_temp);
            bld.MOV(byte_offset(dest, r * dest_stride), sum);
         } else if (src0_type != BRW_TYPE_HF) {
            brw_reg temp = bld.vgrf(src0_type);

            bld.MOV(temp, dot);
            bld.ADD(byte_offset(dest, r * dest_stride),
                    temp,
                    byte_offset(src0, r * dest_stride));
         } else {
            bld.ADD(byte_offset(dest, r * dest_stride),
                    dot,
                    byte_offset(src0, r * dest_stride));
         }
      } else {
         bld.MOV(byte_offset(dest, r * dest_stride), dot);
      }
   }
}

static void
int8_using_dp4a(const brw_builder &bld, brw_dpas_inst *dpas)
{
   /* We only intend to support configurations where the destination and
    * accumulator have the same type.
    */
   if (!dpas->src[0].is_null())
      assert(dpas->dst.type == dpas->src[0].type);

   assert(dpas->src[1].type == BRW_TYPE_B ||
          dpas->src[1].type == BRW_TYPE_UB);
   assert(dpas->src[2].type == BRW_TYPE_B ||
          dpas->src[2].type == BRW_TYPE_UB);

   const brw_reg_type src1_type = dpas->src[1].type == BRW_TYPE_UB
      ? BRW_TYPE_UD : BRW_TYPE_D;

   const brw_reg_type src2_type = dpas->src[2].type == BRW_TYPE_UB
      ? BRW_TYPE_UD : BRW_TYPE_D;

   brw_reg dest = dpas->dst;
   brw_reg src0 = dpas->src[0];
   const brw_reg src1 = retype(dpas->src[1], src1_type);
   const brw_reg src2 = retype(dpas->src[2], src2_type);

   const unsigned dest_stride = reg_unit(bld.shader->devinfo) * REG_SIZE;

   for (unsigned r = 0; r < dpas->rcount; r++) {
      if (!src0.is_null()) {
         bld.MOV(dest, src0);
         src0 = byte_offset(src0, dest_stride);
      } else {
         bld.MOV(dest, retype(brw_imm_d(0), dest.type));
      }

      for (unsigned s = 0; s < dpas->sdepth; s++) {
         bld.DP4A(dest,
                  dest,
                  byte_offset(src1, s * dpas->exec_size * 4),
                  component(byte_offset(src2, r * dpas->sdepth * 4), s))
            ->saturate = dpas->saturate;
      }

      dest = byte_offset(dest, dest_stride);
   }
}

static void
int8_using_mul_add(const brw_builder &bld, brw_dpas_inst *dpas)
{
   /* We only intend to support configurations where the destination and
    * accumulator have the same type.
    */
   if (!dpas->src[0].is_null())
      assert(dpas->dst.type == dpas->src[0].type);

   assert(dpas->src[1].type == BRW_TYPE_B ||
          dpas->src[1].type == BRW_TYPE_UB);
   assert(dpas->src[2].type == BRW_TYPE_B ||
          dpas->src[2].type == BRW_TYPE_UB);

   const brw_reg_type src0_type = dpas->dst.type;

   const brw_reg_type src1_type = dpas->src[1].type == BRW_TYPE_UB
      ? BRW_TYPE_UD : BRW_TYPE_D;

   const brw_reg_type src2_type = dpas->src[2].type == BRW_TYPE_UB
      ? BRW_TYPE_UD : BRW_TYPE_D;

   brw_reg dest = dpas->dst;
   brw_reg src0 = dpas->src[0];
   const brw_reg src1 = retype(dpas->src[1], src1_type);
   const brw_reg src2 = retype(dpas->src[2], src2_type);

   const unsigned dest_stride = REG_SIZE;

   for (unsigned r = 0; r < dpas->rcount; r++) {
      if (!src0.is_null()) {
         bld.MOV(dest, src0);
         src0 = byte_offset(src0, dest_stride);
      } else {
         bld.MOV(dest, retype(brw_imm_d(0), dest.type));
      }

      for (unsigned s = 0; s < dpas->sdepth; s++) {
         brw_reg temp1 = bld.vgrf(BRW_TYPE_UD);
         brw_reg temp2 = bld.vgrf(BRW_TYPE_UD);
         brw_reg temp3 = bld.vgrf(BRW_TYPE_UD, 2);
         const brw_reg_type temp_type =
            (dpas->src[1].type == BRW_TYPE_B ||
             dpas->src[2].type == BRW_TYPE_B)
            ? BRW_TYPE_W : BRW_TYPE_UW;

         /* Expand 8 dwords of packed bytes into 16 dwords of packed
          * words.
          *
          * FINISHME: Gfx9 should not need this work around. Gfx11
          * may be able to use integer MAD. Both platforms may be
          * able to use MAC.
          */
         bld.group(32, 0).MOV(retype(temp3, temp_type),
                              retype(byte_offset(src2, r * REG_SIZE),
                                     dpas->src[2].type));

         bld.MUL(subscript(temp1, temp_type, 0),
                 subscript(retype(byte_offset(src1, s * REG_SIZE),
                                  BRW_TYPE_UD),
                           dpas->src[1].type, 0),
                 subscript(component(retype(temp3, BRW_TYPE_UD),
                                     s * 2),
                           temp_type, 0));

         bld.MUL(subscript(temp1, temp_type, 1),
                 subscript(retype(byte_offset(src1, s * REG_SIZE),
                                  BRW_TYPE_UD),
                           dpas->src[1].type, 1),
                 subscript(component(retype(temp3, BRW_TYPE_UD),
                                     s * 2),
                           temp_type, 1));

         bld.MUL(subscript(temp2, temp_type, 0),
                 subscript(retype(byte_offset(src1, s * REG_SIZE),
                                  BRW_TYPE_UD),
                           dpas->src[1].type, 2),
                 subscript(component(retype(temp3, BRW_TYPE_UD),
                                     s * 2 + 1),
                           temp_type, 0));

         bld.MUL(subscript(temp2, temp_type, 1),
                 subscript(retype(byte_offset(src1, s * REG_SIZE),
                                  BRW_TYPE_UD),
                           dpas->src[1].type, 3),
                 subscript(component(retype(temp3, BRW_TYPE_UD),
                                     s * 2 + 1),
                           temp_type, 1));

         bld.ADD(subscript(temp1, src0_type, 0),
                 subscript(temp1, temp_type, 0),
                 subscript(temp1, temp_type, 1));

         bld.ADD(subscript(temp2, src0_type, 0),
                 subscript(temp2, temp_type, 0),
                 subscript(temp2, temp_type, 1));

         bld.ADD(retype(temp1, src0_type),
                 retype(temp1, src0_type),
                 retype(temp2, src0_type));

         bld.ADD(dest, dest, retype(temp1, src0_type))
            ->saturate = dpas->saturate;
      }

      dest = byte_offset(dest, dest_stride);
   }
}

bool
brw_lower_dpas(brw_shader &v)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, v.cfg) {
      if (inst->opcode != BRW_OPCODE_DPAS)
         continue;

      brw_dpas_inst *dpas = inst->as_dpas();
      const unsigned exec_size = v.devinfo->ver >= 20 ? 16 : 8;
      const brw_builder bld = brw_builder(dpas).group(exec_size, 0).exec_all();

      if (brw_type_is_float(dpas->dst.type)) {
         hf16_using_alu(bld, dpas);
      } else {
         if (v.devinfo->ver >= 12) {
            int8_using_dp4a(bld, dpas);
         } else {
            int8_using_mul_add(bld, dpas);
         }
      }

      dpas->remove();
      progress = true;
   }

   if (progress)
      v.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}
