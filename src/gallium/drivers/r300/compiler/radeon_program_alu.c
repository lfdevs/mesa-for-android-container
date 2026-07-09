/*
 * Copyright 2008 Nicolai Haehnle.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file
 *
 * Shareable transformations that transform "special" ALU instructions
 * into ALU instructions that are supported by hardware.
 *
 */

#include "radeon_program_alu.h"

#include "radeon_compiler.h"
#include "radeon_compiler_util.h"
#include "radeon_dataflow.h"
#include "util/compiler.h"

static struct rc_instruction *
emit1(struct radeon_compiler *c, struct rc_instruction *after, rc_opcode Opcode,
      struct rc_sub_instruction *base, struct rc_dst_register DstReg, struct rc_src_register SrcReg)
{
   struct rc_instruction *fpi = rc_insert_new_instruction(c, after);

   if (base) {
      memcpy(&fpi->U.I, base, sizeof(struct rc_sub_instruction));
   }

   fpi->U.I.Opcode = Opcode;
   fpi->U.I.DstReg = DstReg;
   fpi->U.I.SrcReg[0] = SrcReg;
   return fpi;
}

static struct rc_instruction *
emit2(struct radeon_compiler *c, struct rc_instruction *after, rc_opcode Opcode,
      struct rc_sub_instruction *base, struct rc_dst_register DstReg,
      struct rc_src_register SrcReg0, struct rc_src_register SrcReg1)
{
   struct rc_instruction *fpi = rc_insert_new_instruction(c, after);

   if (base) {
      memcpy(&fpi->U.I, base, sizeof(struct rc_sub_instruction));
   }

   fpi->U.I.Opcode = Opcode;
   fpi->U.I.DstReg = DstReg;
   fpi->U.I.SrcReg[0] = SrcReg0;
   fpi->U.I.SrcReg[1] = SrcReg1;
   return fpi;
}

static struct rc_dst_register
dstregtmpmask(int index, int mask)
{
   struct rc_dst_register dst = {0, 0, 0};
   dst.File = RC_FILE_TEMPORARY;
   dst.Index = index;
   dst.WriteMask = mask;
   return dst;
}

static const struct rc_src_register builtin_one = {
   .File = RC_FILE_NONE, .Index = 0, .Swizzle = RC_SWIZZLE_1111};

static const struct rc_src_register srcreg_undefined = {
   .File = RC_FILE_NONE, .Index = 0, .Swizzle = RC_SWIZZLE_XYZW};

static struct rc_src_register
srcreg(int file, int index)
{
   struct rc_src_register src = srcreg_undefined;
   src.File = file;
   src.Index = index;
   return src;
}

static struct rc_src_register
srcregswz(int file, int index, int swz)
{
   struct rc_src_register src = srcreg_undefined;
   src.File = file;
   src.Index = index;
   src.Swizzle = swz;
   return src;
}

static struct rc_src_register
absolute(struct rc_src_register reg)
{
   struct rc_src_register newreg = reg;
   newreg.Abs = 1;
   newreg.Negate = RC_MASK_NONE;
   return newreg;
}

static struct rc_src_register
negate(struct rc_src_register reg)
{
   struct rc_src_register newreg = reg;
   newreg.Negate = newreg.Negate ^ RC_MASK_XYZW;
   return newreg;
}

static struct rc_dst_register
new_dst_reg(struct radeon_compiler *c, struct rc_instruction *inst)
{
   unsigned tmp = rc_find_free_temporary(c);
   return dstregtmpmask(tmp, inst->U.I.DstReg.WriteMask);
}

static void
transform_DP2(struct radeon_compiler *c, struct rc_instruction *inst)
{
   struct rc_src_register src0 = inst->U.I.SrcReg[0];
   struct rc_src_register src1 = inst->U.I.SrcReg[1];
   src0.Negate &= ~(RC_MASK_Z | RC_MASK_W);
   src0.Swizzle &= ~(63 << (3 * 2));
   src0.Swizzle |= (RC_SWIZZLE_ZERO << (3 * 2)) | (RC_SWIZZLE_ZERO << (3 * 3));
   src1.Negate &= ~(RC_MASK_Z | RC_MASK_W);
   src1.Swizzle &= ~(63 << (3 * 2));
   src1.Swizzle |= (RC_SWIZZLE_ZERO << (3 * 2)) | (RC_SWIZZLE_ZERO << (3 * 3));
   emit2(c, inst->Prev, RC_OPCODE_DP3, &inst->U.I, inst->U.I.DstReg, src0, src1);
   rc_remove_instruction(inst);
}

static void
transform_RSQ(struct radeon_compiler *c, struct rc_instruction *inst)
{
   inst->U.I.SrcReg[0] = absolute(inst->U.I.SrcReg[0]);
}

static void
transform_KILP(struct radeon_compiler *c, struct rc_instruction *inst)
{
   inst->U.I.SrcReg[0] = negate(builtin_one);
   inst->U.I.Opcode = RC_OPCODE_KIL;
}

/**
 * Can be used as a transformation for @ref radeonClauseLocalTransform,
 * no userData necessary.
 *
 * Transforms RSQ to Radeon's native RSQ by explicitly setting
 * absolute value.
 *
 * @note should be applicable to R300 and R500 fragment programs.
 */
int
radeonTransformALU(struct radeon_compiler *c, struct rc_instruction *inst, void *unused)
{
   switch (inst->U.I.Opcode) {
   case RC_OPCODE_DP2: transform_DP2(c, inst); return 1;
   case RC_OPCODE_KILP: transform_KILP(c, inst); return 1;
   case RC_OPCODE_RSQ: transform_RSQ(c, inst); return 1;
   case RC_OPCODE_SEQ: UNREACHABLE("");
   case RC_OPCODE_SGE: UNREACHABLE("");
   case RC_OPCODE_SLT: UNREACHABLE("");
   case RC_OPCODE_SNE: UNREACHABLE("");
   default: return 0;
   }
}

static void
transform_r300_vertex_CMP(struct radeon_compiler *c, struct rc_instruction *inst)
{
   /* R5xx has a CMP, but we can use it only if it reads from less than
    * three different temps. */
   if (c->is_r500 && !rc_inst_has_three_diff_temp_srcs(inst))
      return;

   UNREACHABLE("");
}

static void
transform_r300_vertex_DP2(struct radeon_compiler *c, struct rc_instruction *inst)
{
   struct rc_instruction *next_inst = inst->Next;
   transform_DP2(c, inst);
   next_inst->Prev->U.I.Opcode = RC_OPCODE_DP4;
}

static void
transform_r300_vertex_DP3(struct radeon_compiler *c, struct rc_instruction *inst)
{
   struct rc_src_register src0 = inst->U.I.SrcReg[0];
   struct rc_src_register src1 = inst->U.I.SrcReg[1];
   src0.Negate &= ~RC_MASK_W;
   src0.Swizzle &= ~(7 << (3 * 3));
   src0.Swizzle |= RC_SWIZZLE_ZERO << (3 * 3);
   src1.Negate &= ~RC_MASK_W;
   src1.Swizzle &= ~(7 << (3 * 3));
   src1.Swizzle |= RC_SWIZZLE_ZERO << (3 * 3);
   emit2(c, inst->Prev, RC_OPCODE_DP4, &inst->U.I, inst->U.I.DstReg, src0, src1);
   rc_remove_instruction(inst);
}

/**
 * For use with rc_local_transform, this transforms non-native ALU
 * instructions of the r300 up to r500 vertex engine.
 */
int
r300_transform_vertex_alu(struct radeon_compiler *c, struct rc_instruction *inst, void *unused)
{
   switch (inst->U.I.Opcode) {
   case RC_OPCODE_CMP:
      transform_r300_vertex_CMP(c, inst);
      return 1;
   case RC_OPCODE_DP2:
      transform_r300_vertex_DP2(c, inst);
      return 1;
   case RC_OPCODE_DP3:
      transform_r300_vertex_DP3(c, inst);
      return 1;
   default:
      return 0;
   }
}
